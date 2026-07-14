// ============================================================================
// Horse::RollbackHistoricalCameraArgs
//
// Bounded game-thread history for LuxBattle_PerFrameTick's 24-byte camera
// argument. Newly simulated frames capture the intercepted argument once;
// repeated or rollback simulation of the same frame reuses the original bytes.
// ============================================================================

#pragma once

#include "RollbackStateHash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    static constexpr size_t kRollbackHistoricalCameraArgsBytes = 24;

    enum class RollbackHistoricalCameraArgsFailure : uint8_t
    {
        None,
        InvalidArgument,
        MissingRollbackFrame,
        RetentionCollision,
        IntegrityMismatch,
    };

    struct RollbackHistoricalCameraArgsReport
    {
        bool ok {false};
        bool captured {false};
        bool replayed {false};
        RollbackHistoricalCameraArgsFailure failure {
            RollbackHistoricalCameraArgsFailure::None};
    };

    template<size_t Capacity = 128>
    class RollbackHistoricalCameraArgs
    {
    public:
        static_assert(Capacity > 60,
            "camera history must exceed the maximum rollback window");

        using Bytes = std::array<
            uint8_t, kRollbackHistoricalCameraArgsBytes>;

        void clear() noexcept
        {
            m_entries = {};
        }

        RollbackHistoricalCameraArgsReport select(
            uint64_t epoch,
            uint32_t frame,
            bool rolling_back,
            const Bytes* intercepted,
            Bytes& selected) noexcept
        {
            RollbackHistoricalCameraArgsReport report {};
            if (epoch == 0 || (!rolling_back && !intercepted))
            {
                report.failure =
                    RollbackHistoricalCameraArgsFailure::InvalidArgument;
                return report;
            }

            Entry& entry = m_entries[frame % Capacity];
            if (entry.valid && entry.epoch == epoch && entry.frame == frame)
            {
                if (RollbackHashBytes(entry.bytes.data(), entry.bytes.size())
                    != entry.hash)
                {
                    report.failure =
                        RollbackHistoricalCameraArgsFailure::IntegrityMismatch;
                    return report;
                }
                selected = entry.bytes;
                report.ok = true;
                report.replayed = true;
                return report;
            }

            if (rolling_back)
            {
                report.failure = RollbackHistoricalCameraArgsFailure::
                    MissingRollbackFrame;
                return report;
            }

            if (entry.valid && entry.epoch == epoch)
            {
                // Equal modulo slots are separated by at least Capacity when
                // advancing normally. Refuse stale/out-of-order replacement;
                // it could discard a frame still inside the rollback horizon.
                if (frame <= entry.frame
                    || static_cast<uint64_t>(frame - entry.frame) < Capacity)
                {
                    report.failure = RollbackHistoricalCameraArgsFailure::
                        RetentionCollision;
                    return report;
                }
            }

            entry = {};
            entry.epoch = epoch;
            entry.frame = frame;
            entry.bytes = *intercepted;
            entry.hash = RollbackHashBytes(
                entry.bytes.data(), entry.bytes.size());
            entry.valid = entry.hash != 0;
            if (!entry.valid)
            {
                report.failure =
                    RollbackHistoricalCameraArgsFailure::IntegrityMismatch;
                return report;
            }
            selected = entry.bytes;
            report.ok = true;
            report.captured = true;
            return report;
        }

    private:
        struct Entry
        {
            uint64_t epoch {0};
            uint32_t frame {0};
            Bytes bytes {};
            uint64_t hash {0};
            bool valid {false};
        };

        std::array<Entry, Capacity> m_entries {};
    };
}
