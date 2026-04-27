// ============================================================================
// Horse::WorldPause — engage UE4's standard world-pause flag during HorseMod
// freeze, so UDemoNetDriver (the match-replay driver) halts at its source.
//
// Why this exists
// ---------------
// SoulCalibur VI's match-replay system uses UE4's standard UDemoNetDriver to
// play back saved replay files (.replay).  This is COMPLETELY SEPARATE from
// the chara/BM Actor::Tick chains that HorseMod's sites 1–22 catch:
//
//   UWorld::Tick
//     -> NetDriver->TickDispatch    <-- UDemoNetDriver lives here
//        -> reads packets from .replay file
//        -> writes replicated state ONTO actors (chara, BM, ReplayPlayer)
//     -> Actor::TickActor chain     <-- HorseMod sites 9-22 gate this
//
// Sites 1-22 fire AFTER the demo driver has already mutated actor properties,
// so they're useless against the demo replay.  Empirical evidence (user
// testing 2026-04-27): replay inputs differ between (HorseMod freeze used)
// and (no freeze) cycles — even with all 22 sites + VMFreezeByte engaged.
//
// The fix
// -------
// UE4's UDemoNetDriver respects the world's pause flag.  Standard UE4 source
// (DemoNetDriver.cpp):
//
//   void UDemoNetDriver::TickFlush(float DeltaSeconds) {
//     ...
//     if (World && World->IsGameWorld() &&
//         World->GetWorldSettings()->GetPauserPlayerState() != nullptr)
//     {
//         return;  // World is paused — skip the entire tick
//     }
//     ...
//   }
//
// Setting AWorldSettings::Pauser via UGameplayStatics::SetGamePaused(true)
// halts UDemoNetDriver at the standard UE4 hook point.  When HorseMod
// unfreezes, we call SetGamePaused(false) and the demo driver resumes from
// where it left off — so a 5-second freeze does NOT skip 5 seconds of the
// replay; it actually pauses replay playback.
//
// Side effects of UE4's SetGamePaused (vs. HorseMod's speedval=0):
// ----------------------------------------------------------------
//   * UWorld::Tick still runs but with deltaTime = 0 for all non-paused
//     actors.  Actors with `bTickEvenWhenPaused == true` continue ticking
//     with real deltaTime (these are the "OnTickWhenPaused" handlers
//     LuxBattleManager_RegisterOnTickWhenPaused_Delegates registers — the
//     six handlers including ALuxBattleFrameInputLog::OnTickWhenPaused).
//   * Pause indicator / HUD changes may appear (mostly cosmetic).
//   * Audio may attenuate or mute depending on per-class flags.
//
// HorseMod's existing speedval=0 mechanism continues to do its job —
// halting MoveVM/integrators/sites 1-22.  WorldPause adds a SECOND layer
// that specifically targets UDemoNetDriver (and incidentally any other
// World-tick-driven systems we haven't audited).  The two compose cleanly:
// when both engaged, basically NOTHING ticks except UE4's render thread
// and the bTickEvenWhenPaused-flagged actors.
//
// Threading
// ---------
// `engage()` and `release()` are called from the cockpit pre-hook (game
// thread).  ProcessEvent runs on the same thread.  The atomic `m_paused`
// flag dedupes redundant calls and is safe to read from any thread for
// status display.
//
// Recovery
// --------
// If SetGamePaused fails (e.g. no PlayerController during loading screen):
//   * `apply()` returns false without changing m_paused
//   * Next frame we retry.  No persistent bad state.
//
// If we engage() and then HorseMod is hot-unloaded without release():
//   * The world stays paused forever — bad UX.
//   * Caller (HorseMod main) must call release() in destructor.
// ============================================================================

#pragma once

#include "HorseLib.hpp"

#include <Mod/CppUserModBase.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>

namespace Horse
{
    class WorldPause
    {
    public:
        // Engage UE4's standard pause flag (= UGameplayStatics::SetGamePaused(true)).
        // Returns true if successfully engaged OR if already engaged.  Returns
        // false if the World/PlayerController/UFunction wasn't available yet
        // — caller should retry next frame.
        bool engage()  { return apply(true);  }
        bool release() { return apply(false); }

        // Idempotent toggle — set to the desired state if not already there.
        // Returns true if the world is in the requested state after the call.
        bool set(bool want_paused) { return apply(want_paused); }

        // Returns true if WE have engaged the pause (not whether the world
        // is currently paused — UE4 might pause it for other reasons we
        // don't track).
        bool we_engaged() const { return m_paused.load(std::memory_order_acquire); }

    private:
        bool apply(bool want_paused)
        {
            // Dedupe: if we already drove this state, no-op.  Prevents
            // re-walking UE4SS reflection caches every frame during a
            // long freeze hold.
            if (m_paused.load(std::memory_order_acquire) == want_paused)
                return true;

            using namespace RC;
            using namespace RC::Unreal;

            // Lazy-resolve the GameplayStatics CDO.  IsReal-revalidates
            // each call so we recover after level changes.
            if (!m_gs_cdo || !UObject::IsReal(m_gs_cdo))
            {
                m_gs_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.Default__GameplayStatics"));
                if (!m_gs_cdo)
                {
                    // Engine module not loaded yet (during early init?).
                    // Caller will retry next frame.
                    return false;
                }
            }

            // Need ANY UObject in the World as the WorldContextObject.
            // PlayerController is the conventional choice.
            UObject* pc = m_pc.get(L"PlayerController");
            if (!pc)
            {
                // No PC — likely on title screen or between maps.  No
                // replay running anyway, so no work needed.  Mark our
                // intended state without a ProcessEvent so we don't
                // retry every frame on the menu.
                if (!want_paused)
                {
                    m_paused.store(false, std::memory_order_release);
                }
                return false;
            }

            // Cache the SetGamePaused UFunction.  The Fn helper memoises
            // by class identity, so this is a single hash lookup after
            // the first call.
            auto* fn = m_fn.on(m_gs_cdo, L"SetGamePaused");
            if (!fn)
            {
                Output::send<LogLevel::Warning>(
                    STR("[Horse.WorldPause] UGameplayStatics::SetGamePaused "
                        "UFunction not found — cannot engage demo-driver pause\n"));
                return false;
            }

            // UE4 ProcessEvent params layout for SetGamePaused (from
            // GameplayStatics.gen.cpp): WorldContextObject (UObject*),
            // bPaused (bool packed as uint32 in the ABI), bReturnValue
            // (bool — true if the request was accepted).
            //
            // The struct must be 4-byte aligned and exactly match the
            // UFunction's parameter layout, otherwise ProcessEvent will
            // read garbage.  This layout is stable across all UE4 4.21
            // builds we target.
            struct Params {
                UObject* WorldContextObject;
                uint32_t bPaused;
                uint32_t bReturnValue;
            } p{};
            p.WorldContextObject = pc;
            p.bPaused = want_paused ? 1u : 0u;

            m_gs_cdo->ProcessEvent(fn, &p);

            m_paused.store(want_paused, std::memory_order_release);

            // Log every state change so we can verify the engage/release
            // is actually firing during a freeze cycle.  If this log line
            // is absent during a HorseMod freeze, WorldPause isn't doing
            // its job — likely PC not yet resolved (return false above)
            // or m_gs_cdo lookup failed.
            Output::send<LogLevel::Verbose>(
                STR("[Horse.WorldPause] SetGamePaused({}) called via UE4SS reflection "
                    "(pc=0x{:x} fn=0x{:x})\n"),
                want_paused ? STR("true") : STR("false"),
                reinterpret_cast<uint64_t>(pc),
                reinterpret_cast<uint64_t>(fn));
            return true;
        }

        // ProcessEvent runs on the cockpit-tick thread.  m_paused is
        // also read by the ImGui status surface on the same thread —
        // atomic just future-proofs against any cross-thread query.
        std::atomic<bool>          m_paused{false};

        // Cached PlayerController via FindFirstOf, revalidated by IsReal
        // each call.  Provided by HorseLib::GlobalPtr.
        Horse::GlobalPtr           m_pc;

        // Cached GameplayStatics CDO (= the static-default object that
        // owns SetGamePaused as a UFunction).  /Script/Engine.Default__-
        // GameplayStatics is a stable engine-side object.
        RC::Unreal::UObject*       m_gs_cdo = nullptr;

        // Cached UFunction*.  Horse::Fn handles the GetFunctionByNameIn-
        // Chain memoisation.
        Horse::Fn                  m_fn;
    };

} // namespace Horse
