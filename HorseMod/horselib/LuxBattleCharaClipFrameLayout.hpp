#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    // Partial native FLuxBattleChara layout.  Ghidra/assembly verification:
    // LuxMoveVM_AdvanceCharaAnimClipPlayer @ 0x14037C2F0 and
    // FUN_1402D5560 both access the current animation clip clock at +0x2B27C.
    // Keep the large unknown prefix explicit so offsetof, rather than a
    // duplicated numeric constant, is the production address contract.
    struct LuxBattleCharaClipFrameLayout
    {
        std::array<std::byte, 0x2B27C> abBeforeCurrentClipFrame {};
        float flCurrentClipFrame {0.0f};
    };

    inline constexpr uintptr_t kLuxBattleCharaCurrentClipFrameOffset =
        offsetof(LuxBattleCharaClipFrameLayout, flCurrentClipFrame);

    inline float ReadLuxBattleCharaCurrentClipFrameForTest(
        const void* chara_bytes) noexcept
    {
        float value = 0.0f;
        std::memcpy(
            &value,
            static_cast<const std::byte*>(chara_bytes)
                + kLuxBattleCharaCurrentClipFrameOffset,
            sizeof(value));
        return value;
    }
}
