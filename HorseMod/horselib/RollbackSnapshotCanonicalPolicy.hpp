#pragma once

#include <cstdint>

namespace Horse
{
    enum class RollbackCanonicalPolicy : uint8_t
    {
        AllBytes,
        LuxMoveSchedStateArray,
        LuxBattleWorldModeControl,
        LuxBattleInputRing,
        LuxBattleInputRingCursor,
        LuxBattleNativeFrameCounter,
        LuxBattleCollisionCooldown,
        LuxBattleCollisionOwner,
    };

    static constexpr uint32_t RollbackCollisionCooldownRemaining(
        uint32_t current_frame,
        uint32_t last_dispatch_frame) noexcept
    {
        const bool eligible = current_frame < last_dispatch_frame
            || static_cast<uint32_t>(last_dispatch_frame + 3u)
                < current_frame;
        return eligible ? 0u : static_cast<uint32_t>(
            last_dispatch_frame + 4u - current_frame);
    }
}
