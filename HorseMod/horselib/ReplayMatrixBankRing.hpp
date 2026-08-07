#pragma once

#include <cstdint>

namespace Horse
{
    struct ReplayMatrixBankRingState
    {
        uint8_t active_slot {0xFF};
        uint8_t current_slot {0xFF};
        uint8_t previous_slot {0xFF};
        uint8_t reserved {0};
    };

    constexpr uint8_t PreviousReplayMatrixBankSlot(
        uint8_t active_slot) noexcept
    {
        return static_cast<uint8_t>((active_slot + 1u) % 3u);
    }

    constexpr bool ValidateReplayMatrixBankRingState(
        const ReplayMatrixBankRingState& state) noexcept
    {
        return state.active_slot < 3
            && state.current_slot == state.active_slot
            && state.previous_slot
                == PreviousReplayMatrixBankSlot(state.active_slot);
    }

    constexpr int32_t ReplayMatrixBankHistorySourceTick(
        int32_t restored_tick,
        uint8_t age) noexcept
    {
        return age >= 1 && age <= 2
                && restored_tick >= static_cast<int32_t>(age)
            ? restored_tick - static_cast<int32_t>(age)
            : -1;
    }

    struct ReplayMatrixBankFrameIdentity
    {
        int32_t tick {-1};
        int32_t seq {-1};
        int32_t round {-1};
        int32_t master {-1};
    };

    enum class ReplayMatrixBankHistorySourceDisposition : uint8_t
    {
        Required,
        RoundBoundary,
        Invalid,
    };

    constexpr bool ValidateReplayMatrixBankFrameIdentity(
        const ReplayMatrixBankFrameIdentity& identity) noexcept
    {
        return identity.tick >= 0
            && identity.seq >= 0
            && identity.round >= 0
            && identity.master >= 0;
    }

    constexpr ReplayMatrixBankHistorySourceDisposition
    ClassifyReplayMatrixBankHistorySource(
        const ReplayMatrixBankFrameIdentity& restored,
        const ReplayMatrixBankFrameIdentity& source,
        uint8_t age) noexcept
    {
        if (!ValidateReplayMatrixBankFrameIdentity(restored)
            || age < 1 || age > 2)
        {
            return ReplayMatrixBankHistorySourceDisposition::Invalid;
        }

        const int32_t expected_tick =
            ReplayMatrixBankHistorySourceTick(restored.tick, age);
        if (restored.master < age)
            return ReplayMatrixBankHistorySourceDisposition::RoundBoundary;
        if (expected_tick < 0)
            return ReplayMatrixBankHistorySourceDisposition::Invalid;
        if (!ValidateReplayMatrixBankFrameIdentity(source)
            || source.tick != expected_tick
            || source.round != restored.round
            || source.seq != restored.seq - age
            || source.master != restored.master - age)
        {
            return ReplayMatrixBankHistorySourceDisposition::Invalid;
        }
        return ReplayMatrixBankHistorySourceDisposition::Required;
    }
}
