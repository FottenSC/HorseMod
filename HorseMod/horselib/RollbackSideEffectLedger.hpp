// ============================================================================
// Horse::RollbackSideEffectLedger
//
// Confirmed-frame presentation ledger. Native deterministic schedulers keep
// running during rollback; external audio/VFX/particle dispatch is represented
// by idempotent events and committed once after peer confirmation.
// ============================================================================

#pragma once

#include "RollbackFrameStamp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    enum class RollbackSideEffectType : uint8_t
    {
        Audio,
        Vfx,
        Particle,
        BarrierPresentation,
        WallPresentation,
    };

    struct RollbackSideEffectEvent
    {
        uint64_t epoch {0};
        uint32_t frame {0};
        RollbackSideEffectType type {RollbackSideEffectType::Audio};
        uint64_t idempotency_key {0};
        uint16_t payload_bytes {0};
        std::array<uint8_t, 64> payload {};
    };

    using RollbackSideEffectCommitFn = void(*)(
        const RollbackSideEffectEvent& event,
        void* context);

    struct RollbackSideEffectLedgerReport
    {
        bool ok {true};
        bool overflow {false};
        uint64_t queued {0};
        uint64_t duplicates {0};
        uint64_t discarded {0};
        uint64_t committed {0};
        const char* failure {"ok"};
    };

    template<size_t Capacity = 2048, size_t CommittedCapacity = 4096>
    class RollbackSideEffectLedger
    {
        struct CommittedKey
        {
            uint64_t epoch {0};
            uint32_t frame {0};
            RollbackSideEffectType type {RollbackSideEffectType::Audio};
            uint64_t key {0};
            bool valid {false};
        };

    public:
        bool enqueue(
            uint64_t epoch,
            uint32_t frame,
            RollbackSideEffectType type,
            uint64_t idempotency_key,
            const void* payload,
            uint16_t payload_bytes) noexcept
        {
            if (epoch == 0 || idempotency_key == 0
                || payload_bytes > RollbackSideEffectEvent {}.payload.size()
                || (payload_bytes != 0 && !payload))
            {
                m_report.ok = false;
                m_report.failure = "invalid-side-effect";
                return false;
            }
            if (contains(epoch, frame, type, idempotency_key)
                || was_committed(epoch, frame, type, idempotency_key))
            {
                ++m_report.duplicates;
                return true;
            }
            if (m_count == Capacity)
            {
                m_report.ok = false;
                m_report.overflow = true;
                m_report.failure = "side-effect-ledger-overflow";
                return false;
            }
            RollbackSideEffectEvent& event = m_events[m_count++];
            event = {};
            event.epoch = epoch;
            event.frame = frame;
            event.type = type;
            event.idempotency_key = idempotency_key;
            event.payload_bytes = payload_bytes;
            if (payload_bytes)
                std::memcpy(event.payload.data(), payload, payload_bytes);
            ++m_report.queued;
            return true;
        }

        void rollback_from(uint64_t epoch, uint32_t frame) noexcept
        {
            size_t write = 0;
            for (size_t read = 0; read < m_count; ++read)
            {
                const RollbackSideEffectEvent& event = m_events[read];
                const bool discard = event.epoch == epoch
                    && RollbackFrameAtOrAfter(event.frame, frame);
                if (discard)
                {
                    ++m_report.discarded;
                    continue;
                }
                if (write != read) m_events[write] = event;
                ++write;
            }
            m_count = write;
        }

        void rollback_after(uint64_t epoch, uint32_t frame) noexcept
        {
            size_t write = 0;
            for (size_t read = 0; read < m_count; ++read)
            {
                const RollbackSideEffectEvent& event = m_events[read];
                const bool discard = event.epoch == epoch
                    && RollbackFrameIsAfter(event.frame, frame);
                if (discard)
                {
                    ++m_report.discarded;
                    continue;
                }
                if (write != read) m_events[write] = event;
                ++write;
            }
            m_count = write;
        }

        bool confirm_through(
            uint64_t epoch,
            uint32_t frame,
            RollbackSideEffectCommitFn commit,
            void* context) noexcept
        {
            if (!commit)
            {
                m_report.ok = false;
                m_report.failure = "missing-side-effect-commit";
                return false;
            }
            size_t write = 0;
            for (size_t read = 0; read < m_count; ++read)
            {
                const RollbackSideEffectEvent& event = m_events[read];
                const bool confirmed = event.epoch == epoch
                    && (event.frame == frame
                        || RollbackFrameIsBefore(event.frame, frame));
                if (!confirmed)
                {
                    if (write != read) m_events[write] = event;
                    ++write;
                    continue;
                }
                if (!was_committed(event.epoch, event.frame,
                                   event.type, event.idempotency_key))
                {
                    commit(event, context);
                    remember_committed(event);
                    ++m_report.committed;
                }
                else
                {
                    ++m_report.duplicates;
                }
            }
            m_count = write;
            return true;
        }

        void clear() noexcept
        {
            m_count = 0;
            m_committed_count = 0;
            m_next_committed = 0;
            m_report = {};
            for (CommittedKey& key : m_committed) key = {};
        }

        size_t pending() const noexcept { return m_count; }
        const RollbackSideEffectLedgerReport& report() const noexcept
        {
            return m_report;
        }

    private:
        bool contains(uint64_t epoch, uint32_t frame,
                      RollbackSideEffectType type, uint64_t key) const noexcept
        {
            for (size_t i = 0; i < m_count; ++i)
            {
                const auto& event = m_events[i];
                if (event.epoch == epoch && event.frame == frame
                    && event.type == type && event.idempotency_key == key)
                    return true;
            }
            return false;
        }

        bool was_committed(uint64_t epoch, uint32_t frame,
                           RollbackSideEffectType type,
                           uint64_t key) const noexcept
        {
            for (size_t i = 0; i < m_committed_count; ++i)
            {
                const auto& committed = m_committed[i];
                if (committed.valid && committed.epoch == epoch
                    && committed.frame == frame && committed.type == type
                    && committed.key == key)
                    return true;
            }
            return false;
        }

        void remember_committed(const RollbackSideEffectEvent& event) noexcept
        {
            CommittedKey& key = m_committed[m_next_committed];
            key.epoch = event.epoch;
            key.frame = event.frame;
            key.type = event.type;
            key.key = event.idempotency_key;
            key.valid = true;
            m_next_committed = (m_next_committed + 1) % CommittedCapacity;
            if (m_committed_count < CommittedCapacity) ++m_committed_count;
        }

        std::array<RollbackSideEffectEvent, Capacity> m_events {};
        std::array<CommittedKey, CommittedCapacity> m_committed {};
        size_t m_count {0};
        size_t m_committed_count {0};
        size_t m_next_committed {0};
        RollbackSideEffectLedgerReport m_report {};
    };

    struct RollbackResimContext
    {
        bool active {false};
        uint64_t epoch {0};
        uint32_t frame {0};
    };

    static inline RollbackResimContext& CurrentRollbackResimContext() noexcept
    {
        static thread_local RollbackResimContext context {};
        return context;
    }

    class RollbackResimScope
    {
    public:
        RollbackResimScope(uint64_t epoch, uint32_t frame) noexcept
            : m_previous(CurrentRollbackResimContext())
        {
            CurrentRollbackResimContext() = {true, epoch, frame};
        }

        ~RollbackResimScope() noexcept
        {
            CurrentRollbackResimContext() = m_previous;
        }

        RollbackResimScope(const RollbackResimScope&) = delete;
        RollbackResimScope& operator=(const RollbackResimScope&) = delete;

    private:
        RollbackResimContext m_previous {};
    };
}
