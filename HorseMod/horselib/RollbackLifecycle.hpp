// ============================================================================
// Horse::RollbackLifecycle
//
// Builds the Horse-owned active-PVP lifecycle epoch from the native
// GetActiveBattleManager resolver and Ghidra-verified fields.
// ============================================================================

#pragma once

#include "GameMode.hpp"
#include "RollbackFrameStamp.hpp"
#include "RollbackSnapshot.hpp"
#include "RollbackStageSnapshot.hpp"
#include "SafeMemoryRead.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace Horse
{
    using RollbackGetActiveBattleManagerFn = void*(__fastcall*)();

    static inline uintptr_t ResolveRollbackActiveBattleManager(
        uintptr_t image_base) noexcept
    {
        if (!image_base) return 0;
        RollbackGetActiveBattleManagerFn fn =
            reinterpret_cast<RollbackGetActiveBattleManagerFn>(
                image_base + 0x564C30);
        __try
        {
            return reinterpret_cast<uintptr_t>(fn());
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static inline bool CaptureRollbackLifecycleEpoch(
        uintptr_t image_base,
        RollbackLifecycleEpoch& out) noexcept
    {
        out = {};
        out.presence = static_cast<uint8_t>(
            GameMode::instance().current_presence());
        out.pvp_active = out.presence == 7 || out.presence == 8;
        out.battle_manager =
            ResolveRollbackActiveBattleManager(image_base);
        if (!out.battle_manager)
            return false;

        void* input_log = nullptr;
        void* stage_actor_manager = nullptr;
        void* chara_p1 = nullptr;
        void* chara_p2 = nullptr;
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x478),
                &input_log)
            || !input_log
            || !SafeReadPtr(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x418),
                &stage_actor_manager)
            || !stage_actor_manager
            || !SafeReadPtr(
                reinterpret_cast<const void*>(
                    rollback_absolute_from_image_base(
                        image_base, 0x14470DE90ull)),
                &chara_p1)
            || !SafeReadPtr(
                reinterpret_cast<const void*>(
                    rollback_absolute_from_image_base(
                        image_base, 0x14470DE98ull)),
                &chara_p2)
            || !chara_p1
            || !chara_p2
            || !SafeReadUInt8(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1461),
                &out.battle_main_state)
            || !SafeReadUInt8(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1480),
                &out.battle_status)
            || !SafeReadUInt32(
                reinterpret_cast<const void*>(
                    reinterpret_cast<uintptr_t>(input_log) + 0x3A0),
                &out.input_log_frame))
        {
            return false;
        }
        out.input_log = reinterpret_cast<uintptr_t>(input_log);
        out.chara[0] = reinterpret_cast<uintptr_t>(chara_p1);
        out.chara[1] = reinterpret_cast<uintptr_t>(chara_p2);
        out.stage_actor_manager =
            reinterpret_cast<uintptr_t>(stage_actor_manager);

        std::array<uint8_t, 0xC0> round_start {};
        if (!SafeReadBytes(
                reinterpret_cast<const void*>(
                    out.battle_manager + 0x1360),
                round_start.data(),
                round_start.size()))
        {
            return false;
        }
        out.round_start_digest = RollbackHashRoundStartCanonical(
            round_start.data(), round_start.size());

        RollbackBreakableStageSnapshot stage {};
        const RollbackBreakableStageReport stage_report =
            CaptureRollbackBreakableStageSnapshot(
                out.stage_actor_manager, stage);
        if (!stage_report.ok)
            return false;
        out.stage_layout_digest = stage.stage_layout_digest;
        out.actor_set_digest = stage.actor_set_digest;

        void* auto_advance = reinterpret_cast<void*>(1);
        if (!SafeReadPtr(
                reinterpret_cast<const void*>(image_base + 0x4856728),
                &auto_advance))
        {
            return false;
        }
        out.auto_advance_armed = auto_advance != nullptr;
        out.valid = out.battle_manager != 0
            && out.input_log != 0
            && out.chara[0] != 0
            && out.chara[1] != 0
            && out.stage_actor_manager != 0
            && out.round_start_digest != 0
            && out.stage_layout_digest != 0
            && out.actor_set_digest != 0;
        return out.valid;
    }

    static inline bool RollbackLifecycleClockRegressed(
        const RollbackLifecycleEpoch& previous,
        const RollbackLifecycleEpoch& current) noexcept
    {
        return previous.valid
            && current.valid
            && previous.input_log == current.input_log
            && RollbackFrameIsBefore(
                current.input_log_frame, previous.input_log_frame);
    }
}
