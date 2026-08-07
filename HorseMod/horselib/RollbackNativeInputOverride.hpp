#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    // The native engine publication uses two compact input words: current in
    // the low dword and the ordinary rising-edge word in the high dword.
    static constexpr uint64_t PackRollbackNativeEngineInput(
        uint32_t current, uint32_t previous) noexcept
    {
        const uint32_t rising = (previous ^ current) & current;
        return static_cast<uint64_t>(current)
            | (static_cast<uint64_t>(rising) << 32);
    }

    struct RollbackNativeInputOwnershipEvidence
    {
        bool exact_input_pair_published {false};
        bool native_per_frame_completed {false};
        bool engine_source_exact {false};
        bool consumer_action_edge_submitted {false};
        bool consumer_action_edge_preserved {false};
        uint8_t consumer_read_mask {0};
    };

    struct RollbackNativeInputActionEvidence
    {
        bool action_edge_submitted {false};
        bool action_edge_preserved {false};
    };

    static constexpr RollbackNativeInputActionEvidence
    EvaluateRollbackNativeInputActionEvidence(
        const std::array<uint64_t, 2>& source,
        const std::array<uint32_t, 2>& consumed_current,
        const std::array<uint32_t, 2>& consumed_edge,
        uint32_t action_mask) noexcept
    {
        // Any independently verified button bit is sufficient, but the same
        // bit must be present in the source current/rising words and in both
        // consumer publications. A held button or direction-only input cannot
        // establish ownership.
        RollbackNativeInputActionEvidence evidence {};
        if (!action_mask) return evidence;
        for (size_t slot = 0; slot < 2; ++slot)
        {
            const uint32_t source_current =
                static_cast<uint32_t>(source[slot]);
            const uint32_t source_edge =
                static_cast<uint32_t>(source[slot] >> 32);
            const uint32_t submitted =
                source_current & source_edge & action_mask;
            if (!submitted) continue;
            evidence.action_edge_submitted = true;
            if ((consumed_current[slot] & submitted) == submitted
                && (consumed_edge[slot] & submitted) == submitted)
            {
                evidence.action_edge_preserved = true;
            }
        }
        return evidence;
    }

    static constexpr bool IsRollbackNativeInputOwnershipVerified(
        const RollbackNativeInputOwnershipEvidence& evidence) noexcept
    {
        if (!evidence.exact_input_pair_published
            || !evidence.native_per_frame_completed
            || !evidence.engine_source_exact
            || !evidence.consumer_action_edge_submitted
            || !evidence.consumer_action_edge_preserved
            || evidence.consumer_read_mask != 0x3)
        {
            return false;
        }
        return true;
    }

}
