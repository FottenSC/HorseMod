#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic::Sc6HookLayout
{
// SoulcaliburVI.exe 14034F610: seed >> 4, IAT srand, MT seed,
// seed & 0xfff, zero-count branch, IAT rand. No new detour at this entry.
inline constexpr std::array<std::byte, 48> crt_init_signature{
    std::byte{0x44},std::byte{0x8b},std::byte{0xf1},std::byte{0xc1},std::byte{0xe9},std::byte{0x04},
    std::byte{0xff},std::byte{0x15},std::byte{0xe4},std::byte{0xe1},std::byte{0xed},std::byte{0x02},
    std::byte{0x41},std::byte{0x8b},std::byte{0xce},std::byte{0xe8},std::byte{0xf4},std::byte{0xf9},std::byte{0xff},std::byte{0xff},
    std::byte{0x33},std::byte{0xff},std::byte{0x4c},std::byte{0x8d},std::byte{0x3d},std::byte{0x5b},std::byte{0x18},std::byte{0xdb},std::byte{0x03},
    std::byte{0x41},std::byte{0x81},std::byte{0xe6},std::byte{0xff},std::byte{0x0f},std::byte{0x00},std::byte{0x00},
    std::byte{0x0f},std::byte{0x86},std::byte{0x8e},std::byte{0x00},std::byte{0x00},std::byte{0x00},
    std::byte{0xff},std::byte{0x15},std::byte{0xa8},std::byte{0xe1},std::byte{0xed},std::byte{0x02}};
inline constexpr std::array<std::byte, 10> crt_warmup_loop_signature{
    std::byte{0x41},std::byte{0x83},std::byte{0xc6},std::byte{0xff},std::byte{0x0f},std::byte{0x85},
    std::byte{0x78},std::byte{0xff},std::byte{0xff},std::byte{0xff}};
inline constexpr std::array<std::byte, 5> crt_complete_call_signature{
    std::byte{0xe8},std::byte{0xdb},std::byte{0xf8},std::byte{0xff},std::byte{0xff}};
inline constexpr std::array<std::byte, 6> crt_movevm_call_signature{
    std::byte{0xff},std::byte{0x15},std::byte{0x0c},std::byte{0x68},std::byte{0xec},std::byte{0x02}};
inline constexpr std::uintptr_t input_producer_tick_rva = 0x3FBDF0;
inline constexpr std::uintptr_t input_sample_rva = 0x3F0680;
inline constexpr std::array<std::byte, 10> input_sample_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5C}, std::byte{0x24}, std::byte{0x08},
    std::byte{0x48}, std::byte{0x89}, std::byte{0x74}, std::byte{0x24}, std::byte{0x10}};
inline constexpr std::array<std::byte, 9> input_producer_tick_signature{
    std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xEC}, std::byte{0x20}, std::byte{0x48}, std::byte{0x8B},
    std::byte{0xD9}};
inline constexpr std::uintptr_t tutorial_tick_rva = 0x437F50;
inline constexpr std::uintptr_t tutorial_vtable_rva = 0x328F8D8;
inline constexpr std::array<std::byte, 13> tutorial_tick_signature{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x74}, std::byte{0x24},
    std::byte{0x10}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xEC}, std::byte{0x20}, std::byte{0x48}, std::byte{0x8B},
    std::byte{0xF9}};

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
// authored helpers 0x3250 and 0x3251, which perform Tira's probability-gated
// state19 writes. The signature is five saved registers plus its 0x50-byte
// frame.
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
