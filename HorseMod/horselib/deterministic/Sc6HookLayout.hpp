#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic::Sc6HookLayout
{
// Return address immediately after LuxAudio_ResolveAndPlayCharaCue calls the
// voice terminal. Its cue-sheet argument is a process-local CRI slot.
inline constexpr std::uintptr_t battle_audio_chara_cue_terminal_return_rva =
    0x519a6d;

// Return address after LuxBattleManager_DispatchBattleEventByClass crosses
// into the shared-player voice-registration thunk. The dispatcher has already
// selected the exact class/shared owner from its live manager at this point.
inline constexpr std::uintptr_t battle_audio_dispatch_terminal_return_rva =
    0x519789;

// LuxMoveVM_ExecuteBankSlotScript is the synchronous ownership boundary for
// authored helper 0x321B, which performs Tira's probability-gated state19
// write. The signature is five saved registers plus its 0x50-byte frame.
inline constexpr std::uintptr_t movevm_execute_bank_slot_rva = 0x2fcc30;
inline constexpr std::array<std::byte, 11> movevm_execute_bank_slot_signature{
    std::byte{0x40}, std::byte{0x53}, std::byte{0x55}, std::byte{0x56},
    std::byte{0x57}, std::byte{0x41}, std::byte{0x56}, std::byte{0x48},
    std::byte{0x83}, std::byte{0xec}, std::byte{0x50},
};

// LuxMoveVM_CallCond_WriteCharaStateShort_14 loads the authored value and
// signed index before storing into fighter +0x197C + index*2.
inline constexpr std::uintptr_t movevm_write_chara_state_short_rva = 0x2fda30;
inline constexpr std::array<std::byte, 17>
    movevm_write_chara_state_short_signature{
        std::byte{0x41}, std::byte{0x0f}, std::byte{0xb7}, std::byte{0x40},
        std::byte{0x02}, std::byte{0x49}, std::byte{0x0f}, std::byte{0xbf},
        std::byte{0x10}, std::byte{0x66}, std::byte{0x89}, std::byte{0x84},
        std::byte{0x51}, std::byte{0x7c}, std::byte{0x19}, std::byte{0x00},
        std::byte{0x00},
    };
}
