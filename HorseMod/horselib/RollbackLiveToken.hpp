#pragma once

#include <array>
#include <cstdint>

namespace Horse
{
    struct RollbackLiveToken
    {
        uintptr_t battle_manager {0};
        uintptr_t input_log {0};
        std::array<uintptr_t, 2> chara {};
        uintptr_t stage_actor_manager {0};
        uint64_t round_start_digest {0};
        uint64_t stage_actor_order_digest {0};
        uint32_t native_stage_identity {0};
        uint32_t input_log_frame {0};
        uint32_t round_ordinal {0};
        uint8_t presence {0xFF};
        uint8_t battle_main_state {0xFF};
        uint8_t battle_status {0xFF};
        bool pvp_active {false};
        bool auto_advance_armed {true};
        bool valid {false};
    };

    static inline bool RollbackLiveTokensExactlyMatch(
        const RollbackLiveToken& a, const RollbackLiveToken& b) noexcept
    {
        return a.battle_manager == b.battle_manager
            && a.input_log == b.input_log
            && a.chara == b.chara
            && a.stage_actor_manager == b.stage_actor_manager
            && a.round_start_digest == b.round_start_digest
            && a.stage_actor_order_digest == b.stage_actor_order_digest
            && a.native_stage_identity == b.native_stage_identity
            && a.input_log_frame == b.input_log_frame
            && a.round_ordinal == b.round_ordinal
            && a.presence == b.presence
            && a.battle_main_state == b.battle_main_state
            && a.battle_status == b.battle_status
            && a.pvp_active == b.pvp_active
            && a.auto_advance_armed == b.auto_advance_armed
            && a.valid == b.valid;
    }
}
