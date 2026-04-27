// ============================================================================
// Horse::BattleTimeFreeze — sets BM->bForciblyStopBattleTime = true during
// HorseMod freeze, halting SC6's round-timer countdown via the engine's
// own "stop battle time" mechanism.
//
// THE FINAL PIECE OF THE PUZZLE
// -----------------------------
// User report 2026-04-27 (third clarification):
//   "If I pause the game for long enough both characters just stand still
//    until the time runs out"
//
// Translation: HorseMod freeze IS correctly halting the chara/replay state
// (chars don't move = correct), BUT the ROUND TIMER continues to count
// down.  When the timer reaches 0 the round ends, regardless of replay
// playback completion.  Earlier "inputs don't match" was a downstream
// symptom — the replay was being terminated by round-timer expiration
// before its recorded inputs could finish playing.
//
// WHY HORSEMOD'S OTHER GATES DON'T CATCH THIS
// --------------------------------------------
// Sites 1-22 catch chara/BM Actor::Tick chains.  VMFreezeByte halts
// MoveVM math.  None of these touch the BattleTimeManager (BM+0x4F8)
// — a SEPARATE actor that owns the round timer and ticks it in real
// wallclock time.
//
// THE FIX
// -------
// SC6's ALuxBattleManager has a UProperty named `bForciblyStopBattleTime`
// (verified via Z_Construct_UClass_ALuxBattleManager_Statics @
// 0x14094bf30, string referenced at 0x143386bf8).  The flag is registered
// as a BoolProperty and the engine's tick code checks it — that's literally
// what "Forcibly Stop Battle Time" means.
//
// HorseMod sets BM->bForciblyStopBattleTime = true on freeze, false on
// unfreeze.  Composes with sites 1-16 + VMFreezeByte (which already
// correctly freeze chara state).  This is the ONE missing piece.
//
// IMPLEMENTATION
// --------------
// Use UE4SS reflection (Horse::Obj::getPtr<bool>) to access the property
// directly.  No ProcessEvent overhead — it's a direct property write to
// the in-memory bool field.  Idempotent via a state-tracking atomic.
//
// Recovery
// --------
// On HorseMod teardown, release() restores the flag to false.  Critical
// for hot-unload safety: leaving the flag stuck at true would freeze
// every subsequent battle's timer until the user restarted SC6.
// ============================================================================

#pragma once

#include "HorseLib.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>

namespace Horse
{
    class BattleTimeFreeze
    {
    public:
        bool engage()  { return apply(true);  }   // forcibly stop battle time
        bool release() { return apply(false); }   // restore normal countdown

        bool we_engaged() const { return m_stopped.load(std::memory_order_acquire); }

    private:
        bool apply(bool want_stopped)
        {
            using namespace RC;
            using namespace RC::Unreal;

            if (m_stopped.load(std::memory_order_acquire) == want_stopped)
                return true;

            // Resolve the live BM via the cached UE4SS reflection lookup.
            Horse::Obj bm = m_lux.battleManager();
            if (!bm) return false;

            // bForciblyStopBattleTime is a BoolProperty — UE4 stores these
            // as bitfield bytes.  Horse::Obj::getPtr<bool> returns a ptr
            // into the actor's memory at the property's reflected offset.
            //
            // We use the property name (NOT a hardcoded offset) so this
            // is robust to layout shifts between SC6 patches — UE4
            // reflection is the source of truth.
            bool* p = bm.getPtr<bool>(L"bForciblyStopBattleTime");
            if (!p)
            {
                Output::send<LogLevel::Warning>(
                    STR("[Horse.BattleTimeFreeze] bForciblyStopBattleTime "
                        "property not found on BM — engine may have changed "
                        "the property name in this build\n"));
                return false;
            }

            *p = want_stopped;

            // CRITICAL: ALSO disable the BattleTimeManager actor's tick.
            // Setting bForciblyStopBattleTime alone proved insufficient
            // (verified empirically — the flag does get set, but the
            // round timer still ticks down).  Either the engine doesn't
            // check this flag on every tick, or the bool-bitfield write
            // is hitting the wrong bit position.
            //
            // BattleTimeManager is at BM+0x4F8 (verified via Ghidra
            // analysis of Z_Construct_UClass_ALuxBattleManager_Statics).
            // Disabling its tick via SetActorTickEnabled(false) halts
            // the timer at its source — the actor that owns the
            // round-timer countdown.
            Horse::Obj time_mgr = bm.getObj(L"BattleTimeManager");
            if (time_mgr)
            {
                struct TickParams { uint32_t bEnabled; } tp{};
                tp.bEnabled = want_stopped ? 0u : 1u;
                time_mgr.callRaw(m_set_tick_fn, L"SetActorTickEnabled", &tp);
            }

            m_stopped.store(want_stopped, std::memory_order_release);

            Output::send<LogLevel::Verbose>(
                STR("[Horse.BattleTimeFreeze] BM->bForciblyStopBattleTime = {} "
                    "(BM=0x{:x} flag_addr=0x{:x} time_mgr=0x{:x})\n"),
                want_stopped ? STR("true") : STR("false"),
                reinterpret_cast<uint64_t>(bm.raw()),
                reinterpret_cast<uint64_t>(p),
                reinterpret_cast<uint64_t>(time_mgr.raw()));
            return true;
        }

        std::atomic<bool> m_stopped{false};
        Horse::Lux        m_lux;
        Horse::Fn         m_set_tick_fn;  // cached SetActorTickEnabled UFunction
    };

} // namespace Horse
