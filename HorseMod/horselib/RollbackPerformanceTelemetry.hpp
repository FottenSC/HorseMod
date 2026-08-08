// ============================================================================
// Horse::RollbackPerformanceTelemetry
//
// Fixed-allocation, per-event performance evidence.  Release tooling derives
// percentile gates from these exact histograms rather than elapsed time
// divided across sparse status samples.
// ============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Horse
{
    static constexpr std::array<uint64_t, 12>
        kRollbackDurationBucketUpperNanoseconds {{
            250000ull, 500000ull, 1000000ull, 2000000ull,
            4000000ull, 8000000ull, 12000000ull, 16670000ull,
            20000000ull, 25000000ull, 33330000ull, 50000000ull,
        }};

    template <size_t BoundaryCount>
    struct RollbackFixedHistogram
    {
        std::array<uint64_t, BoundaryCount + 1> buckets {};
        uint64_t samples {0};
        uint64_t maximum {0};
        bool saturated {false};

        void observe(
            uint64_t value,
            const std::array<uint64_t, BoundaryCount>& boundaries) noexcept
        {
            if (samples == std::numeric_limits<uint64_t>::max())
            {
                saturated = true;
                return;
            }
            size_t bucket = 0;
            while (bucket < boundaries.size()
                && value > boundaries[bucket])
                ++bucket;
            if (buckets[bucket] == std::numeric_limits<uint64_t>::max())
            {
                saturated = true;
                return;
            }
            ++buckets[bucket];
            ++samples;
            if (value > maximum) maximum = value;
        }

        bool conserved() const noexcept
        {
            uint64_t total = 0;
            for (uint64_t bucket : buckets)
            {
                if (total > std::numeric_limits<uint64_t>::max() - bucket)
                    return false;
                total += bucket;
            }
            return !saturated && total == samples;
        }
    };

    using RollbackDurationHistogram = RollbackFixedHistogram<
        kRollbackDurationBucketUpperNanoseconds.size()>;

    struct RollbackDepthHistogram
    {
        static constexpr size_t kMaximumDepth = 60;
        std::array<uint64_t, kMaximumDepth + 2> buckets {};
        uint64_t samples {0};
        uint32_t maximum {0};
        bool saturated {false};

        void observe(uint32_t depth) noexcept
        {
            const size_t bucket = depth <= kMaximumDepth
                ? static_cast<size_t>(depth) : kMaximumDepth + 1;
            if (samples == std::numeric_limits<uint64_t>::max()
                || buckets[bucket] == std::numeric_limits<uint64_t>::max())
            {
                saturated = true;
                return;
            }
            ++samples;
            ++buckets[bucket];
            if (depth > maximum) maximum = depth;
        }

        bool conserved() const noexcept
        {
            uint64_t total = 0;
            for (uint64_t bucket : buckets)
            {
                if (total > std::numeric_limits<uint64_t>::max() - bucket)
                    return false;
                total += bucket;
            }
            return !saturated && total == samples;
        }
    };

    struct RollbackLeadHistogram
    {
        // Half-frame buckets spanning [-8,+8], with one underflow and one
        // overflow bucket.
        std::array<uint64_t, 35> buckets {};
        uint64_t samples {0};
        float minimum {0.0f};
        float maximum {0.0f};
        bool initialized {false};
        bool saturated {false};

        void observe(float frames) noexcept
        {
            int half_frames = static_cast<int>(frames * 2.0f);
            size_t bucket = 0;
            if (half_frames < -16) bucket = 0;
            else if (half_frames > 16) bucket = 34;
            else bucket = static_cast<size_t>(half_frames + 17);
            if (samples == std::numeric_limits<uint64_t>::max()
                || buckets[bucket] == std::numeric_limits<uint64_t>::max())
            {
                saturated = true;
                return;
            }
            ++samples;
            ++buckets[bucket];
            if (!initialized)
            {
                minimum = maximum = frames;
                initialized = true;
            }
            else
            {
                if (frames < minimum) minimum = frames;
                if (frames > maximum) maximum = frames;
            }
        }

        bool conserved() const noexcept
        {
            uint64_t total = 0;
            for (uint64_t bucket : buckets)
            {
                if (total > std::numeric_limits<uint64_t>::max() - bucket)
                    return false;
                total += bucket;
            }
            return !saturated && total == samples;
        }
    };

    enum class RollbackSaveClass : uint8_t
    {
        Baseline,
        Confirmed,
        Midpoint,
        End,
        Count,
    };

    struct RollbackPerformanceTelemetry
    {
        static constexpr size_t kSideEffectTypeCount = 4;
        RollbackDurationHistogram owned_tick {};
        RollbackDurationHistogram rollback_transaction {};
        RollbackDurationHistogram full_capture {};
        RollbackDurationHistogram evidence_capture {};
        RollbackDurationHistogram restore {};
        RollbackDurationHistogram verification {};
        RollbackDepthHistogram rollback_depth {};
        RollbackLeadHistogram frame_lead {};
        std::array<uint64_t,
            static_cast<size_t>(RollbackSaveClass::Count)> saves_by_class {};
        uint64_t exact_confirmed_lag_samples {0};
        uint64_t exact_confirmed_lag_total {0};
        uint32_t exact_confirmed_lag_maximum {0};
        uint64_t pair_confirmed_lag_samples {0};
        uint64_t pair_confirmed_lag_total {0};
        uint32_t pair_confirmed_lag_maximum {0};
        uint64_t pacing_holds {0};
        uint64_t pacing_forced_advances {0};
        std::array<uint64_t, kSideEffectTypeCount>
            effect_produced_to_eligible_samples {};
        std::array<uint64_t, kSideEffectTypeCount>
            effect_produced_to_eligible_total {};
        std::array<uint32_t, kSideEffectTypeCount>
            effect_produced_to_eligible_maximum {};
        std::array<uint64_t, kSideEffectTypeCount>
            effect_eligible_to_committed_samples {};
        std::array<uint64_t, kSideEffectTypeCount>
            effect_eligible_to_committed_total {};
        std::array<uint32_t, kSideEffectTypeCount>
            effect_eligible_to_committed_maximum {};

        void observe_duration(
            RollbackDurationHistogram& histogram,
            uint64_t nanoseconds) noexcept
        {
            histogram.observe(
                nanoseconds, kRollbackDurationBucketUpperNanoseconds);
        }

        void observe_lag(
            uint32_t lag, bool pair_confirmed) noexcept
        {
            uint64_t& samples = pair_confirmed
                ? pair_confirmed_lag_samples : exact_confirmed_lag_samples;
            uint64_t& total = pair_confirmed
                ? pair_confirmed_lag_total : exact_confirmed_lag_total;
            uint32_t& maximum = pair_confirmed
                ? pair_confirmed_lag_maximum : exact_confirmed_lag_maximum;
            if (samples != std::numeric_limits<uint64_t>::max()) ++samples;
            if (total <= std::numeric_limits<uint64_t>::max() - lag)
                total += lag;
            if (lag > maximum) maximum = lag;
        }

        void observe_effect_lag(
            size_t type, uint32_t produced_to_eligible,
            uint32_t eligible_to_committed) noexcept
        {
            if (type >= kSideEffectTypeCount) return;
            const auto observe = [](uint32_t lag, uint64_t& samples,
                                    uint64_t& total,
                                    uint32_t& maximum) noexcept {
                if (samples != std::numeric_limits<uint64_t>::max())
                    ++samples;
                if (total <= std::numeric_limits<uint64_t>::max() - lag)
                    total += lag;
                if (lag > maximum) maximum = lag;
            };
            observe(produced_to_eligible,
                effect_produced_to_eligible_samples[type],
                effect_produced_to_eligible_total[type],
                effect_produced_to_eligible_maximum[type]);
            observe(eligible_to_committed,
                effect_eligible_to_committed_samples[type],
                effect_eligible_to_committed_total[type],
                effect_eligible_to_committed_maximum[type]);
        }

        void reset_effect_lag() noexcept
        {
            effect_produced_to_eligible_samples = {};
            effect_produced_to_eligible_total = {};
            effect_produced_to_eligible_maximum = {};
            effect_eligible_to_committed_samples = {};
            effect_eligible_to_committed_total = {};
            effect_eligible_to_committed_maximum = {};
        }
    };
}
