// ============================================================================
// Horse::ReplayPlayerGate — disables the BM's BattleReplayPlayer actor's
// tick during HorseMod freeze.
//
// Why this exists
// ---------------
// SoulCalibur VI's match-replay system is driven by an actor stored on the
// BM at a SPECIFIC reflected UProperty offset, identified via Ghidra
// analysis of Z_Construct_UClass_ALuxBattleManager_Statics:
//
//   BM+0x480   BattleReplayRecorder      (UObject* — recording-side actor)
//   BM+0x488   BattleReplayPlayer        (UObject* — PLAYBACK-side actor)
//   BM+0x4B8   BattleKeyRecorder         (TRAINING mode — different actor!)
//   BM+0x4C8   BattleTrainingReplayPlayer (training-mode replay — different!)
//
// During match-replay viewing, BM->BattleReplayPlayer is non-null and is
// the actor that drives playback.  Its TickActor advances StreamTime and
// applies recorded inputs to the chara/MoveVM.
//
// HorseMod's freeze (sites 1-22 + VMFreezeByte + WorldPause) catches:
//   - chara/BM Actor::Tick chains (sites 9-22)
//   - MoveVM math (VMFreezeByte)
//   - UE4 standard pause (WorldPause via SetGamePaused)
//
// What it DOESN'T catch:
//   - BM->BattleReplayPlayer's own Actor::Tick — it's a SEPARATE actor
//     registered with UWorld, ticking independently.  When SetGamePaused
//     is engaged, UE4 pauses the world tick — BUT if the ReplayPlayer
//     has bTickEvenWhenPaused == true (legacy SC6 actor flag), it
//     continues to tick during pause.
//
// This helper bypasses the question by directly disabling the actor's
// tick via SetActorTickEnabled(false) at the actor instance.  This sets
// PrimaryActorTick.bAllowTickOnDedicatedServer = false transiently,
// halting the actor regardless of any tick-when-paused flag.
//
// Mechanism
// ---------
// 1. Resolve BM via Horse::Lux::battleManager() (already cached).
// 2. Read BM->BattleReplayPlayer (a UObject* property).
// 3. Cache the UFunction* for AActor::SetActorTickEnabled.
// 4. On engage(): ProcessEvent SetActorTickEnabled(false).
// 5. On release(): ProcessEvent SetActorTickEnabled(true).
//
// Idempotent and dedupe-by-flag.  No-op when:
//   - BM is null (no battle in progress)
//   - BM->BattleReplayPlayer is null (not a replay viewing context)
//   - SetActorTickEnabled UFunction not found
//
// Recovery
// --------
// On HorseMod teardown, release() is called from the destructor to
// re-enable the actor's tick.  Idempotent — won't fight UE4 if
// the tick is already enabled.
// ============================================================================

#pragma once

#include "HorseLib.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>

namespace Horse
{
    class ReplayPlayerGate
    {
    public:
        // Disable the BM->BattleReplayPlayer's tick.  Returns true if the
        // call was made successfully; false if BM/actor/UFunction wasn't
        // available (caller retries next frame).  Idempotent.
        bool engage()  { return apply(false); }  // tick OFF during freeze
        bool release() { return apply(true);  }  // tick ON normally

        bool we_engaged() const { return m_disabled.load(std::memory_order_acquire); }

    private:
        bool apply(bool tick_enabled)
        {
            using namespace RC;
            using namespace RC::Unreal;

            const bool want_disabled = !tick_enabled;
            if (m_disabled.load(std::memory_order_acquire) == want_disabled)
                return true;

            // Step 1: get the BM via UE4SS reflection cache.
            Horse::Obj bm = m_lux.battleManager();
            if (!bm) return false;

            // Step 2: read BM->BattleReplayPlayer.  This is a UObject*
            // ObjectProperty at +0x488 per Z_Construct_UClass_ALuxBattle-
            // Manager_Statics.  Horse::Obj::getObj does the property
            // lookup by name (UE4 reflection).
            Horse::Obj player = bm.getObj(L"BattleReplayPlayer");
            if (!player)
            {
                // No replay player active — likely not a match-replay
                // viewing context.  Mark the desired state without a
                // ProcessEvent so we don't retry every frame.
                m_disabled.store(want_disabled, std::memory_order_release);
                return false;
            }

            // Step 3: call SetActorTickEnabled(bEnabled) on the player.
            // UE4 standard signature: void AActor::SetActorTickEnabled(bool).
            // Params struct layout (UE4 ABI): one packed bool input.
            struct Params {
                uint32_t bEnabled;
            } p{};
            p.bEnabled = tick_enabled ? 1u : 0u;

            player.callRaw(m_fn, L"SetActorTickEnabled", &p);

            m_disabled.store(want_disabled, std::memory_order_release);

            Output::send<LogLevel::Verbose>(
                STR("[Horse.ReplayPlayerGate] BM->BattleReplayPlayer "
                    "SetActorTickEnabled({}) (player=0x{:x})\n"),
                tick_enabled ? STR("true") : STR("false"),
                reinterpret_cast<uint64_t>(player.raw()));
            return true;
        }

        std::atomic<bool> m_disabled{false};
        Horse::Lux        m_lux;
        Horse::Fn         m_fn;
    };

} // namespace Horse
