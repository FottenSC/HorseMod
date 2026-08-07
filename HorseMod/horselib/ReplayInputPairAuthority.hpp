#pragma once

#include <cstdint>

namespace Horse
{
    enum class ReplayInputPairRepairResult : uint8_t
    {
        Inactive = 0,
        Exact,
        Repaired,
        Failed,
    };

    inline constexpr bool ReplayInputPairRepairPhaseActive(
        bool validation_step,
        bool fast_forward,
        bool native_warmup_to_target,
        bool previous_to_target) noexcept
    {
        return validation_step
            || (fast_forward && native_warmup_to_target
                && previous_to_target);
    }

    inline constexpr bool ReplayLatestEngineInputPairMatches(
        uint64_t live_p1,
        uint64_t live_p2,
        uint64_t expected_p1,
        uint64_t expected_p2) noexcept
    {
        return live_p1 == expected_p1 && live_p2 == expected_p2;
    }

    template <typename WritePair>
    ReplayInputPairRepairResult ReconcileReplayLatestEngineInputPair(
        uint64_t live_p1,
        uint64_t live_p2,
        uint64_t expected_p1,
        uint64_t expected_p2,
        WritePair&& write_pair) noexcept
    {
        if (ReplayLatestEngineInputPairMatches(
                live_p1, live_p2, expected_p1, expected_p2))
        {
            return ReplayInputPairRepairResult::Exact;
        }
        return write_pair(expected_p1, expected_p2)
            ? ReplayInputPairRepairResult::Repaired
            : ReplayInputPairRepairResult::Failed;
    }
}
