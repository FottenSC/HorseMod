// ============================================================================
// Horse::BattlePauseRequest — engages SC6's NATIVE in-game pause via the
// ULuxBattleFunctionLibrary::SetBattlePause UFunction.
//
// THE BREAKTHROUGH
// ----------------
// User report 2026-04-27: "the pause menu can pause [the replay], it also
// changes the playback icon to paused".  This means SC6 ALREADY has a
// working pause mechanism that correctly halts the replay — it's the one
// the in-game pause menu uses.
//
// Ghidra reverse-engineering revealed: when the user opens the pause menu,
// SC6 calls ULuxBattleFunctionLibrary::SetBattlePause(bPause=true, inType=N,
// WorldContext=...).  This is a static BlueprintFunctionLibrary UFunction
// — accessible via UE4SS reflection.
//
// The UFunction is registered at FUN_140936190.  Parameter struct (16B):
//   bool      bPause           — pause / unpause
//   uint8     inType            — enum value (pause-source identifier)
//   UObject*  WorldContext     — any UObject in the World
//
// Sibling UFunctions on the same library (all useful for HorseMod):
//   BattlePauseEnabled, GetBattleManager, GetBattlePauseController,
//   IsBattleOnline, IsBattleOnlineInputSync, IsBattlePaused,
//   IsBattlePlaying, IsFinishBlow, IsLocalUserControl, IsMatchFinished,
//   SetBattlePause       <-- THIS ONE
//   SetImmortality, SetSoulGaugeInfinity, SetUserInputCheck,
//   StepInBattlePause    <-- the frame-step function (future use!)
//
// WHY PRIOR ATTEMPTS FAILED
// -------------------------
// 1. WorldPause (UGameplayStatics::SetGamePaused): pauses UE4 world ticks
//    in the standard UE4 way, but SC6's BattleReplayPlayer uses a
//    different pause concept that doesn't respect bGamePaused.  Side
//    effect: broke unfreeze recovery.
// 2. ReplayPlayerGate (SetActorTickEnabled false on BM->BattleReplayPlayer):
//    halted the actor's tick but left it in a non-resumable state.
// 3. BattleTimeFreeze (set bForciblyStopBattleTime + disable BattleTime-
//    Manager tick): bool bitfield write hit the wrong bit; manager tick
//    disable didn't halt the round timer.
//
// The CORRECT mechanism is what SC6's own pause menu uses — and the
// engine ALREADY knows how to pause/unpause cleanly.  No need to
// reinvent the wheel.
//
// Recovery
// --------
// On HorseMod teardown, release() calls SetBattlePause(false, ...).
// Critical for hot-unload safety: leaving the battle paused forever
// requires the user to open the pause menu themselves to recover.
// ============================================================================

#pragma once

#include "HorseLib.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>

namespace Horse
{
    class BattlePauseRequest
    {
    public:
        // Engage SC6's native battle pause (= same path as pause menu).
        bool engage()  { return apply(true);  }
        bool release() { return apply(false); }

        bool we_engaged() const { return m_paused.load(std::memory_order_acquire); }

    private:
        bool apply(bool want_paused)
        {
            using namespace RC;
            using namespace RC::Unreal;

            // Hard-disable if we previously crashed.  Avoids a crash loop
            // where every freeze attempt re-faults until the user manually
            // disables the mod.
            if (m_disabled.load(std::memory_order_acquire))
                return false;

            if (m_paused.load(std::memory_order_acquire) == want_paused)
                return true;

            // Step 1: resolve the ULuxBattleFunctionLibrary CDO (the static
            // BlueprintFunctionLibrary instance UFunctions are dispatched
            // through).  Cached + revalidated each call.
            if (!m_lib_cdo || !UObject::IsReal(m_lib_cdo))
            {
                m_lib_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr,
                    STR("/Script/LuxorGame.Default__LuxBattleFunctionLibrary"));
                if (!m_lib_cdo)
                {
                    // Engine module not loaded yet (early init?).  Caller
                    // retries next frame.
                    Output::send<LogLevel::Warning>(
                        STR("[Horse.BattlePauseRequest] LuxBattleFunctionLibrary "
                            "CDO not found — retry next frame\n"));
                    return false;
                }
            }

            // Step 2: need a WorldContext UObject — any UObject in the
            // active world.  The BM is a stable choice during a battle.
            Horse::Obj bm = m_lux.battleManager();
            if (!bm)
            {
                // No battle yet — pause request is irrelevant.  Track
                // the desired state without ProcessEvent.
                m_paused.store(want_paused, std::memory_order_release);
                return false;
            }

            // Step 3: cache + invoke SetBattlePause UFunction.
            auto* fn = m_set_pause_fn.on(m_lib_cdo, L"SetBattlePause");
            if (!fn)
            {
                Output::send<LogLevel::Warning>(
                    STR("[Horse.BattlePauseRequest] SetBattlePause UFunction "
                        "not found on LuxBattleFunctionLibrary CDO\n"));
                return false;
            }

            // Param struct (16B) per the EXEC FUNCTION READ ORDER (verified
            // by decompile of execSetBattlePause @ 0x140945640).
            //
            // SECOND ATTEMPT (would also have crashed): had bPause first.
            // UE4 reflection registers properties in C++ DECLARATION-REVERSE
            // order, but the params-struct LAYOUT follows the iteration
            // order = original declaration order.  The exec function reads:
            //   1. WorldContext (UObject*, 8 bytes)
            //   2. inType        (uint8 enum, 1 byte)
            //   3. bPause        (uint8 bool, 1 byte)
            // — confirming the C++ signature is:
            //   SetBattlePause(UObject* WorldContext, uint8 inType, bool bPause)
            //
            // Engine-expected layout:
            //   +0x00  WorldContext  (ObjectProperty, 8 bytes)
            //   +0x08  inType        (ByteProperty enum, 1 byte)
            //   +0x09  bPause        (BoolProperty, 1 byte)
            //   +0x0A  _pad          (6 bytes alignment)
            //   total = 16 bytes
            //
            // ALSO: inType=0 (User) early-returns during replay!  The
            // C++ impl LuxBattleManager_SetPauseState_OrBattleActive at
            // 0x1403F9180 checks `IsBattleActiveNotReplay` when inType==User
            // and aborts if we're in a replay.  Use inType=2 (Tutorial)
            // which skips the active-battle check.  Enum from FUN_14095da50:
            //   User         = 0   (returns early during replay — DON'T USE)
            //   Sync         = 1   (network sync)
            //   Tutorial     = 2   (works during replay — SAFE FOR US)
            //   FightRequest = 3   (mid-fight request)
            //   MAX          = 4
            struct Params {
                UObject* WorldContext;
                uint8_t  inType;
                uint8_t  bPause;
                uint8_t  _pad[6];
            } p{};
            static_assert(sizeof(Params) == 16,
                          "SetBattlePause params struct must be exactly 16 bytes "
                          "to match the UFunction registry");
            p.WorldContext = bm.raw();
            p.inType       = 2;            // ELuxBattlePauseType::Tutorial
            p.bPause       = want_paused ? 1u : 0u;

            // SEH-wrap the ProcessEvent — if the params struct layout
            // doesn't match what UE4 expects, the engine will crash
            // dereferencing into junk.  Catching the fault keeps the
            // game alive so we can iterate.
            //
            // NOTE: __try/__except can't share a function with C++
            // destructors, so we route through a static helper.
            const bool ok = safe_process_event(m_lib_cdo, fn, &p);
            if (!ok)
            {
                Output::send<LogLevel::Error>(
                    STR("[Horse.BattlePauseRequest] ProcessEvent FAULTED — "
                        "SetBattlePause params layout likely wrong.  "
                        "Disabling further attempts this session.\n"));
                m_disabled.store(true, std::memory_order_release);
                return false;
            }

            m_paused.store(want_paused, std::memory_order_release);

            Output::send<LogLevel::Verbose>(
                STR("[Horse.BattlePauseRequest] SetBattlePause(WorldContext=BM=0x{:x}, "
                    "inType=Tutorial, bPause={}) called via ULuxBattleFunctionLibrary\n"),
                reinterpret_cast<uint64_t>(bm.raw()),
                want_paused ? STR("true") : STR("false"));
            return true;
        }

        // SEH-wrapped ProcessEvent.  Lifted out so __try/__except can
        // live in a function without C++ object destructors.
        static bool safe_process_event(RC::Unreal::UObject* obj,
                                       RC::Unreal::UFunction* fn,
                                       void* params) noexcept
        {
            __try
            {
                obj->ProcessEvent(fn, params);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        std::atomic<bool>     m_paused{false};
        std::atomic<bool>     m_disabled{false};   // set after a fault — kill switch
        Horse::Lux            m_lux;
        RC::Unreal::UObject*  m_lib_cdo = nullptr;
        Horse::Fn             m_set_pause_fn;
    };

} // namespace Horse
