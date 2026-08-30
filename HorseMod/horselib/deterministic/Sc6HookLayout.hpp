#pragma once

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
}
