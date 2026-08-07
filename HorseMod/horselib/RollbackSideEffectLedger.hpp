// ============================================================================
// Horse::RollbackSideEffectLedger
//
// Confirmed-frame terminal-presentation ledger. Native deterministic
// schedulers and listener-hub broadcasts keep running during rollback. Proven
// irreversible terminals and stateful character subscribers enqueue stable
// value records; live UObject identities are resolved only after confirmation.
// ============================================================================

#pragma once

#include "RollbackFrameStamp.hpp"
#include "RollbackStateHash.hpp"

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
        Camera,
        ConfirmedTransition,
    };

    static constexpr size_t kRollbackSideEffectTypeCount = 4;
    // The 38 listener-hub lanes retain their historical diagnostic indices.
    // Lanes 38..40 are lower, Ghidra-proven stage presentation terminals;
    // 41 is the audio create/stop terminal, 42 carries pointer-free VFX
    // semantic create, exact logical disable/remove, and confirmed-only manager
    // time-dilation/visibility-filter transactions, and 43 carries stable
    // character-presentation callback values resolved back to the two sealed
    // lifecycle-local actors only at confirmation.
    // Production never enqueues the listener-hub broadcasts themselves.
    static constexpr size_t kRollbackSideEffectLaneCount = 44;
    using RollbackSideEffectLaneCounts = std::array<
        std::array<uint64_t, kRollbackSideEffectLaneCount>,
        kRollbackSideEffectTypeCount>;

    constexpr bool RollbackNativeCorrectionBypassesSideEffectLedger(
        bool native_correction_only,
        RollbackSideEffectType type) noexcept
    {
        return native_correction_only
            && type == RollbackSideEffectType::ConfirmedTransition;
    }

    constexpr bool RollbackSideEffectsEnabledForOwnedRound(
        bool presentation_hooks_installed,
        bool hook_owns_tick,
        bool native_correction_only) noexcept
    {
        return presentation_hooks_installed
            && hook_owns_tick
            && !native_correction_only;
    }

    struct RollbackSideEffectEvent
    {
        uint64_t epoch {0};
        uint32_t frame {0};
        RollbackSideEffectType type {RollbackSideEffectType::Audio};
        uint8_t lane {0};
        // Audit-only provenance. It is deliberately excluded from the event
        // identity/digest because predictor and nonpredictor peers can take
        // different rollback paths while committing the same native event.
        uint32_t fixture_generation {0};
        uint64_t idempotency_key {0};
        uint16_t payload_bytes {0};
        std::array<uint8_t, 96> payload {};
    };

    inline uint64_t ComputeRollbackSideEffectIdentityKey(
        RollbackSideEffectType type,
        uint8_t lane,
        const void* payload,
        size_t bytes,
        uint32_t ordinal) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(static_cast<uint8_t>(type));
        hash.add_scalar(lane);
        hash.add_scalar(ordinal);
        if (payload && bytes) hash.add_bytes(payload, bytes);
        return hash.value ? hash.value : 1;
    }

    inline uint64_t ComputeRollbackSideEffectEventDigest(
        uint32_t frame,
        RollbackSideEffectType type,
        uint8_t lane,
        uint64_t idempotency_key,
        const void* payload,
        size_t payload_bytes) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(frame);
        hash.add_scalar(static_cast<uint8_t>(type));
        hash.add_scalar(lane);
        hash.add_scalar(idempotency_key);
        hash.add_scalar(static_cast<uint16_t>(payload_bytes));
        if (payload && payload_bytes) hash.add_bytes(payload, payload_bytes);
        return hash.value;
    }

    inline uint64_t ComputeRollbackSideEffectEventDigest(
        const RollbackSideEffectEvent& event) noexcept
    {
        return ComputeRollbackSideEffectEventDigest(
            event.frame, event.type, event.lane, event.idempotency_key,
            event.payload.data(), event.payload_bytes);
    }

    class RollbackSideEffectOrdinalDomains
    {
    public:
        void reset() noexcept { m_next = {}; }

        uint32_t next(RollbackSideEffectType type, uint8_t lane) noexcept
        {
            const size_t type_index = static_cast<size_t>(type);
            if (type_index >= kRollbackSideEffectTypeCount
                || lane >= kRollbackSideEffectLaneCount)
                return ~uint32_t {0};
            return m_next[type_index][lane]++;
        }

    private:
        std::array<std::array<uint32_t, kRollbackSideEffectLaneCount>,
            kRollbackSideEffectTypeCount> m_next {};
    };

    using RollbackSideEffectCommitFn = bool(*)(
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
        std::array<uint64_t, kRollbackSideEffectTypeCount> queued_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount> discarded_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount> committed_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            committed_digest_by_type {};
        RollbackSideEffectLaneCounts queued_by_lane {};
        RollbackSideEffectLaneCounts discarded_by_lane {};
        RollbackSideEffectLaneCounts committed_by_lane {};
        RollbackSideEffectLaneCounts committed_digest_by_lane {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_queued_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_discarded_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_committed_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_committed_digest_by_type {};
        const char* failure {"ok"};
    };

    struct RollbackSideEffectConfirmedCheckpoint
    {
        bool valid {false};
        uint64_t epoch {0};
        uint32_t frame {0};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            queued_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            discarded_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            committed_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            committed_digest_by_type {};
        RollbackSideEffectLaneCounts queued_by_lane {};
        RollbackSideEffectLaneCounts discarded_by_lane {};
        RollbackSideEffectLaneCounts committed_by_lane {};
        RollbackSideEffectLaneCounts committed_digest_by_lane {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_queued_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_discarded_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_committed_by_type {};
        std::array<uint64_t, kRollbackSideEffectTypeCount>
            fixture_committed_digest_by_type {};
    };

    inline bool RollbackFixtureTaggedEvidenceValid(
        const std::array<uint64_t, kRollbackSideEffectTypeCount>& queued,
        const std::array<uint64_t, kRollbackSideEffectTypeCount>& discarded,
        const std::array<uint64_t, kRollbackSideEffectTypeCount>& committed,
        const std::array<uint64_t, kRollbackSideEffectTypeCount>&
            committed_digest,
        bool predictor,
        bool require_all_types = true) noexcept
    {
        for (size_t type = 0; type < kRollbackSideEffectTypeCount; ++type)
        {
            if (predictor)
            {
                if (queued[type] != discarded[type] + committed[type]
                    || (committed[type] == 0)
                        != (committed_digest[type] == 0)
                    || (require_all_types
                        && (queued[type] == 0 || committed[type] == 0)))
                    return false;
            }
            else if (queued[type] != 0 || discarded[type] != 0
                || committed[type] != 0 || committed_digest[type] != 0)
            {
                return false;
            }
        }
        return true;
    }

    struct RollbackFixtureEffectWitnessState
    {
        bool valid {false};
        uint64_t epoch {0};
        uint32_t load_frame {0};
        std::array<uint64_t, kRollbackSideEffectTypeCount> discarded_count {};
        std::array<uint64_t, kRollbackSideEffectTypeCount> discarded_digest {};
    };

    class RollbackFixtureEffectWitness
    {
    public:
        void reset() noexcept { m_state = {}; }

        bool begin_load(
            uint64_t epoch,
            uint32_t frame,
            const std::array<uint64_t, kRollbackSideEffectTypeCount>&
                discarded_count,
            const std::array<uint64_t, kRollbackSideEffectTypeCount>&
                discarded_digest) noexcept
        {
            if (epoch == 0) return false;
            if (m_state.valid) return m_state.epoch == epoch;
            m_state = {};
            m_state.valid = true;
            m_state.epoch = epoch;
            m_state.load_frame = frame;
            m_state.discarded_count = discarded_count;
            m_state.discarded_digest = discarded_digest;
            for (size_t type = 0; type < kRollbackSideEffectTypeCount; ++type)
            {
                if ((discarded_count[type] == 0)
                    != (discarded_digest[type] == 0))
                {
                    reset();
                    return false;
                }
            }
            return true;
        }

        const RollbackFixtureEffectWitnessState& state() const noexcept
        {
            return m_state;
        }

    private:
        RollbackFixtureEffectWitnessState m_state {};
    };

    template<size_t EventsPerFrame = 32, size_t FrameBucketCount = 64>
    class RollbackSideEffectLedger
    {
        static_assert(EventsPerFrame != 0,
            "side-effect frame bucket must hold events");
        static_assert(FrameBucketCount >= 2
            && (FrameBucketCount & (FrameBucketCount - 1)) == 0,
            "side-effect frame bucket count must be a power of two");

        struct FrameBucket
        {
            uint64_t epoch {0};
            uint32_t frame {0};
            bool valid {false};
            bool committed {false};
            size_t count {0};
            // Number of terminal operations that have returned success.  A
            // failed commit leaves the cursor on the failed event so an
            // explicit diagnostic retry can never replay an earlier native
            // terminal from the same frame.
            size_t commit_cursor {0};
            std::array<RollbackSideEffectEvent, EventsPerFrame> events {};
        };

    public:
        bool frame_is_confirmed(
            uint64_t epoch, uint32_t frame) const noexcept
        {
            return m_last_confirmed_valid
                && m_last_confirmed_epoch == epoch
                && (frame == m_last_confirmed_frame
                    || RollbackFrameIsBefore(
                        frame, m_last_confirmed_frame));
        }

        bool enqueue(
            uint64_t epoch,
            uint32_t frame,
            RollbackSideEffectType type,
            uint64_t idempotency_key,
            const void* payload,
            uint16_t payload_bytes,
            uint8_t lane = 0,
            uint32_t fixture_generation = 0) noexcept
        {
            if (epoch == 0 || idempotency_key == 0
                || static_cast<size_t>(type)
                    >= kRollbackSideEffectTypeCount
                || lane >= kRollbackSideEffectLaneCount
                || payload_bytes > RollbackSideEffectEvent {}.payload.size()
                || (payload_bytes != 0 && !payload))
            {
                m_report.ok = false;
                m_report.failure = "invalid-side-effect";
                return false;
            }
            FrameBucket& bucket = bucket_for(frame);
            if (m_last_confirmed_valid
                && m_last_confirmed_epoch == epoch
                && (frame == m_last_confirmed_frame
                    || RollbackFrameIsBefore(
                        frame, m_last_confirmed_frame)))
            {
                if (bucket.valid && bucket.committed
                    && bucket.epoch == epoch && bucket.frame == frame)
                {
                    for (size_t i = 0; i < bucket.count; ++i)
                    {
                        const RollbackSideEffectEvent& event =
                            bucket.events[i];
                        if (event.type == type
                            && event.idempotency_key == idempotency_key
                            && event.fixture_generation
                                == fixture_generation)
                        {
                            ++m_report.duplicates;
                            return true;
                        }
                    }
                }
                m_report.ok = false;
                m_report.failure = "side-effect-after-confirmation";
                return false;
            }
            if (bucket.valid
                && (bucket.epoch != epoch || bucket.frame != frame))
            {
                if (!bucket.committed && bucket.count != 0)
                {
                    m_report.ok = false;
                    m_report.overflow = true;
                    m_report.failure = "side-effect-frame-bucket-collision";
                    return false;
                }
                bucket = {};
            }
            if (!bucket.valid)
            {
                bucket.valid = true;
                bucket.epoch = epoch;
                bucket.frame = frame;
            }
            if (bucket.committed)
            {
                for (size_t i = 0; i < bucket.count; ++i)
                {
                    const RollbackSideEffectEvent& event = bucket.events[i];
                    if (event.type == type
                        && event.idempotency_key == idempotency_key
                        && event.fixture_generation == fixture_generation)
                    {
                        ++m_report.duplicates;
                        return true;
                    }
                }
                m_report.ok = false;
                m_report.failure = "side-effect-after-confirmation";
                return false;
            }
            for (size_t i = 0; i < bucket.count; ++i)
            {
                const RollbackSideEffectEvent& event = bucket.events[i];
                if (event.type == type
                    && event.idempotency_key == idempotency_key)
                {
                    if (event.fixture_generation != fixture_generation)
                    {
                        m_report.ok = false;
                        m_report.failure =
                            "side-effect-fixture-provenance-mismatch";
                        return false;
                    }
                    ++m_report.duplicates;
                    return true;
                }
            }
            if (bucket.count == EventsPerFrame)
            {
                m_report.ok = false;
                m_report.overflow = true;
                m_report.failure = "side-effect-frame-bucket-overflow";
                return false;
            }
            RollbackSideEffectEvent& event = bucket.events[bucket.count++];
            event = {};
            event.epoch = epoch;
            event.frame = frame;
            event.type = type;
            event.lane = lane;
            event.fixture_generation = fixture_generation;
            event.idempotency_key = idempotency_key;
            event.payload_bytes = payload_bytes;
            if (payload_bytes)
                std::memcpy(event.payload.data(), payload, payload_bytes);
            ++m_report.queued;
            const size_t type_index = static_cast<size_t>(type);
            ++m_report.queued_by_type[type_index];
            ++m_report.queued_by_lane[type_index][lane];
            if (fixture_generation != 0)
                ++m_report.fixture_queued_by_type[type_index];
            return true;
        }

        void rollback_from(uint64_t epoch, uint32_t frame) noexcept
        {
            for (FrameBucket& bucket : m_buckets)
            {
                if (bucket.valid && !bucket.committed
                    && bucket.epoch == epoch
                    && RollbackFrameAtOrAfter(bucket.frame, frame))
                {
                    m_report.discarded += bucket.count;
                    for (size_t i = 0; i < bucket.count; ++i)
                    {
                        const auto& event = bucket.events[i];
                        const size_t type = static_cast<size_t>(event.type);
                        ++m_report.discarded_by_type[type];
                        ++m_report.discarded_by_lane[type][event.lane];
                        if (event.fixture_generation != 0)
                            ++m_report.fixture_discarded_by_type[type];
                    }
                    bucket = {};
                }
            }
        }

        void rollback_after(uint64_t epoch, uint32_t frame) noexcept
        {
            for (FrameBucket& bucket : m_buckets)
            {
                if (bucket.valid && !bucket.committed
                    && bucket.epoch == epoch
                    && RollbackFrameIsAfter(bucket.frame, frame))
                {
                    m_report.discarded += bucket.count;
                    for (size_t i = 0; i < bucket.count; ++i)
                    {
                        const auto& event = bucket.events[i];
                        const size_t type = static_cast<size_t>(event.type);
                        ++m_report.discarded_by_type[type];
                        ++m_report.discarded_by_lane[type][event.lane];
                        if (event.fixture_generation != 0)
                            ++m_report.fixture_discarded_by_type[type];
                    }
                    bucket = {};
                }
            }
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
            if (epoch == 0)
            {
                m_report.ok = false;
                m_report.failure = "invalid-side-effect-confirmation-epoch";
                return false;
            }
            if (m_last_confirmed_valid
                && m_last_confirmed_epoch == epoch)
            {
                if (frame == m_last_confirmed_frame) return true;
                if (RollbackFrameIsBefore(frame, m_last_confirmed_frame))
                {
                    m_report.ok = false;
                    m_report.failure = "backward-side-effect-confirmation";
                    return false;
                }
            }
            else
            {
                for (const FrameBucket& bucket : m_buckets)
                {
                    if (bucket.valid && !bucket.committed
                        && bucket.count != 0 && bucket.epoch != epoch)
                    {
                        m_report.ok = false;
                        m_report.failure =
                            "side-effect-epoch-transition-with-pending";
                        return false;
                    }
                }
            }

            // Ring storage order is unrelated to frame order after wrap. Find
            // the next logical frame repeatedly. The bounded bucket count
            // keeps this allocation-free O(N^2) scan small and deterministic.
            bool have_cursor = m_last_confirmed_valid
                && m_last_confirmed_epoch == epoch;
            uint32_t cursor = have_cursor ? m_last_confirmed_frame : 0;
            while (true)
            {
                FrameBucket* next = nullptr;
                uint32_t best_distance = 0;
                for (FrameBucket& bucket : m_buckets)
                {
                    if (!bucket.valid || bucket.committed
                        || bucket.epoch != epoch
                        || (bucket.frame != frame
                            && !RollbackFrameIsBefore(bucket.frame, frame)))
                        continue;
                    if (have_cursor
                        && !RollbackFrameIsAfter(bucket.frame, cursor))
                    {
                        m_report.ok = false;
                        m_report.failure =
                            "uncommitted-side-effect-before-frontier";
                        return false;
                    }
                    const uint32_t distance = have_cursor
                        ? RollbackFrameDistance(bucket.frame, cursor)
                        : RollbackFrameDistance(frame, bucket.frame);
                    if (!next
                        || (have_cursor
                            ? distance < best_distance
                            : distance > best_distance))
                    {
                        next = &bucket;
                        best_distance = distance;
                    }
                }
                if (!next) break;

                for (size_t i = next->commit_cursor; i < next->count; ++i)
                {
                    const RollbackSideEffectEvent& event = next->events[i];
                    if (!commit(event, context))
                    {
                        m_report.ok = false;
                        m_report.failure = "side-effect-commit-failed";
                        return false;
                    }
                    const size_t type = static_cast<size_t>(event.type);
                    // Keep the peer digest set-like for compatibility, while
                    // native commits themselves now execute chronologically.
                    m_report.committed_digest_by_type[type] ^=
                        ComputeRollbackSideEffectEventDigest(event);
                    ++m_report.committed_by_type[type];
                    m_report.committed_digest_by_lane[type][event.lane] ^=
                        ComputeRollbackSideEffectEventDigest(event);
                    ++m_report.committed_by_lane[type][event.lane];
                    if (event.fixture_generation != 0)
                    {
                        ++m_report.fixture_committed_by_type[type];
                        m_report.fixture_committed_digest_by_type[type] ^=
                            ComputeRollbackSideEffectEventDigest(event);
                    }
                    ++next->commit_cursor;
                    ++m_report.committed;
                }
                next->committed = true;
                cursor = next->frame;
                have_cursor = true;
            }
            m_last_confirmed_valid = true;
            m_last_confirmed_epoch = epoch;
            m_last_confirmed_frame = frame;
            return true;
        }

        bool project_confirmed_through(
            uint64_t epoch,
            uint32_t frame,
            RollbackSideEffectConfirmedCheckpoint& checkpoint) const noexcept
        {
            checkpoint = {};
            if (epoch == 0
                || (m_last_confirmed_valid
                    && (m_last_confirmed_epoch != epoch
                        || RollbackFrameIsAfter(
                            m_last_confirmed_frame, frame))))
            {
                return false;
            }
            checkpoint.valid = true;
            checkpoint.epoch = epoch;
            checkpoint.frame = frame;
            checkpoint.queued_by_type = m_report.queued_by_type;
            checkpoint.discarded_by_type = m_report.discarded_by_type;
            checkpoint.committed_by_type = m_report.committed_by_type;
            checkpoint.committed_digest_by_type =
                m_report.committed_digest_by_type;
            checkpoint.queued_by_lane = m_report.queued_by_lane;
            checkpoint.discarded_by_lane = m_report.discarded_by_lane;
            checkpoint.committed_by_lane = m_report.committed_by_lane;
            checkpoint.committed_digest_by_lane =
                m_report.committed_digest_by_lane;
            checkpoint.fixture_queued_by_type =
                m_report.fixture_queued_by_type;
            checkpoint.fixture_discarded_by_type =
                m_report.fixture_discarded_by_type;
            checkpoint.fixture_committed_by_type =
                m_report.fixture_committed_by_type;
            checkpoint.fixture_committed_digest_by_type =
                m_report.fixture_committed_digest_by_type;
            for (const FrameBucket& bucket : m_buckets)
            {
                if (!bucket.valid || bucket.committed
                    || bucket.epoch != epoch
                    || (bucket.frame != frame
                        && !RollbackFrameIsBefore(bucket.frame, frame)))
                {
                    continue;
                }
                for (size_t i = 0; i < bucket.count; ++i)
                {
                    const RollbackSideEffectEvent& event = bucket.events[i];
                    const size_t type = static_cast<size_t>(event.type);
                    checkpoint.committed_digest_by_type[type] ^=
                        ComputeRollbackSideEffectEventDigest(event);
                    ++checkpoint.committed_by_type[type];
                    checkpoint.committed_digest_by_lane[type][event.lane] ^=
                        ComputeRollbackSideEffectEventDigest(event);
                    ++checkpoint.committed_by_lane[type][event.lane];
                    if (event.fixture_generation != 0)
                    {
                        ++checkpoint.fixture_committed_by_type[type];
                        checkpoint.fixture_committed_digest_by_type[type] ^=
                            ComputeRollbackSideEffectEventDigest(event);
                    }
                }
            }
            return true;
        }

        void clear() noexcept
        {
            m_report = {};
            m_last_confirmed_valid = false;
            m_last_confirmed_epoch = 0;
            m_last_confirmed_frame = 0;
            for (FrameBucket& bucket : m_buckets) bucket = {};
        }

        size_t pending() const noexcept
        {
            size_t count = 0;
            for (const FrameBucket& bucket : m_buckets)
                if (bucket.valid && !bucket.committed)
                    count += bucket.count;
            return count;
        }
        const RollbackSideEffectLedgerReport& report() const noexcept
        {
            return m_report;
        }

        bool pending_frame_range(
            RollbackSideEffectType type,
            uint32_t& first,
            uint32_t& last,
            uint32_t& count) const noexcept
        {
            first = 0;
            last = 0;
            count = 0;
            bool found = false;
            for (const FrameBucket& bucket : m_buckets)
            {
                if (!bucket.valid || bucket.committed) continue;
                for (size_t i = 0; i < bucket.count; ++i)
                {
                    if (bucket.events[i].type != type) continue;
                    if (!found)
                    {
                        first = bucket.frame;
                        last = bucket.frame;
                        found = true;
                    }
                    else
                    {
                        if (RollbackFrameIsBefore(bucket.frame, first))
                            first = bucket.frame;
                        if (RollbackFrameIsAfter(bucket.frame, last))
                            last = bucket.frame;
                    }
                    ++count;
                }
            }
            return found;
        }

        bool pending_frame_range_after(
            uint64_t epoch,
            RollbackSideEffectType type,
            uint32_t boundary,
            uint32_t& first,
            uint32_t& last,
            uint32_t& count) const noexcept
        {
            first = 0;
            last = 0;
            count = 0;
            bool found = false;
            for (const FrameBucket& bucket : m_buckets)
            {
                if (!bucket.valid || bucket.committed || bucket.epoch != epoch
                    || !RollbackFrameIsAfter(bucket.frame, boundary))
                    continue;
                for (size_t i = 0; i < bucket.count; ++i)
                {
                    if (bucket.events[i].type != type) continue;
                    if (!found)
                    {
                        first = bucket.frame;
                        last = bucket.frame;
                        found = true;
                    }
                    else
                    {
                        if (RollbackFrameIsBefore(bucket.frame, first))
                            first = bucket.frame;
                        if (RollbackFrameIsAfter(bucket.frame, last))
                            last = bucket.frame;
                    }
                    ++count;
                }
            }
            return found;
        }

        bool pending_witness_after(
            uint64_t epoch,
            RollbackSideEffectType type,
            uint32_t boundary,
            uint32_t& count,
            uint64_t& digest) const noexcept
        {
            count = 0;
            digest = 0;
            for (const FrameBucket& bucket : m_buckets)
            {
                if (!bucket.valid || bucket.committed || bucket.epoch != epoch
                    || !RollbackFrameIsAfter(bucket.frame, boundary))
                    continue;
                for (size_t i = 0; i < bucket.count; ++i)
                {
                    const RollbackSideEffectEvent& event = bucket.events[i];
                    if (event.type != type) continue;
                    ++count;
                    digest ^= ComputeRollbackSideEffectEventDigest(event);
                }
            }
            return count != 0;
        }

    private:
        FrameBucket& bucket_for(uint32_t frame) noexcept
        {
            return m_buckets[frame & (FrameBucketCount - 1u)];
        }

        std::array<FrameBucket, FrameBucketCount> m_buckets {};
        RollbackSideEffectLedgerReport m_report {};
        bool m_last_confirmed_valid {false};
        uint64_t m_last_confirmed_epoch {0};
        uint32_t m_last_confirmed_frame {0};
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
