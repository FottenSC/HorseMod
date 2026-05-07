// ============================================================================
// Horse::EBTracer — Runtime tracer for the SC6 multi-hit transition pipeline.
//
// MECHANISM (verified via Ghidra, May 2026)
// =========================================
// Multi-hit moves in SC6 use a per-tick transition scheduler driven by
// per-cell bytecode running inside the inner stack VM:
//
//   STEP 1 — Cell A's bytecode authors a transition target via the
//   inner-VM CALLCOND opcode (0x25) dispatching to one of the
//   "OpcodeIf_XX" functions in PTR_LuxMoveVM_EvaluateIfOpcode_143e83a90:
//
//     - OpcodeIf_05/_07/_08/_0D  (1402FCB80 etc.) → tail-call
//                                LuxMoveVM_DecodeVariadicStreamArgs @ 1402FC930
//
//   In its IMMEDIATE path (when no OpcodeIf_15 wrapper is active),
//   DecodeVariadicStreamArgs writes:
//
//     lane[+0x5A] = packedMoveAddr   (target sub-cell ID)
//     lane[+0x68] = thresholdFloat   (anim frame at which transition fires)
//     lane[+0x64] = startTimeFloat
//     lane[+0x56] = currentLaneIdx mirror
//
//   STEP 2 — Per-tick inside LuxBattle_TickCharaMainSimulation, the
//   function LuxMoveVM_ExecuteOpStream @ 0x1402FDEA0 runs for each lane:
//
//     1. CheckMoveTransitionTiming() — read lane[+0x5A]; if != -1 AND
//        threshold <= other_lane.anim, call TransitionToMove() which
//        UNCONDITIONALLY clears chara+0x16EB and starts the new cell.
//     2. Walk effect-opcode table at lane+500 (16 × 0x24 entries).
//     3. RunBytecodeScript() on the lane's bank slot script — this is
//        where Cell A's per-tick bytecode authors lane[+0x5A].
//     4. CheckMoveTransitionTiming() AGAIN if DAT_14470de64 was set
//        (DecodeVariadicStreamArgs sets this when threshold==now).
//     5. AdvanceLaneFrame.
//
//   STEP 3 — TransitionToMove @ 0x1402FE350 clears the lane state,
//   resolves the new bank slot, runs the new cell's INIT bytecode, and
//   sets chara+0x2130 = 2 (committed).  16EB is cleared along the way.
//
// SETTER OF 16EB
// ==============
// Located via byte-pattern search of ModRM-displacement encodings
// (66 89 88 / 66 41 89 88 / etc.) — the writer uses BASE+0x16D0+REG
// indirect addressing through DrainPerFrameMotionFlagBuffer's mirror at
// 0x1402FD590, which copies chara+0x18B3 (gate) → chara+0x1925 (value)
// → chara+0x16EB (motion-flag bank).  ProcessHitReactionState writes
// the gate/value pair, the next-tick mirror propagates it.
//
// 4A+B (move 0x015A) NATIVE CADENCE
// ==================================
//   anim=0   Cell A start
//   anim=18  Cell A's master-window opens, hit fires, HitStop sets 16EB
//   anim=18-22  Cell A bytecode authors lane[+0x5A] = sub-cell B with
//               threshold=23 (computed from active-attack frame data)
//   anim=23  CheckMoveTransitionTiming → TransitionToMove(B), 16EB cleared
//   anim=37  Cell B fires hit, 16EB latches again
//   anim=41  Transition to Cell C, 16EB cleared
//   anim=55  Cell C fires hit
//   anim=N   Move ends, return to neutral
//
// STEP-MODE FAILURE MODE
// ======================
// Empirically: in HorseMod step mode, lane[+0x5A] STAYS -1 throughout
// the move.  This means the inner-VM bytecode authoring isn't running
// (or isn't running its CALLCOND 0x07/_08 emissions).  Because of that
// CheckMoveTransitionTiming has nothing to drain, no transitions happen,
// 16EB stays latched after the first hit, and only one hit registers.
//
// PURPOSE OF THIS TRACER (PHASE 2)
// ================================
// We've now identified the mechanism.  This tracer is upgraded to also
// log lane[+0x5A] and lane[+0xB4] transitions — that empirically tells
// us PER STEP whether bytecode authoring runs, whether the transition
// drains, or whether something else is interfering.  The hypothesis to
// validate is "in step mode the inner-VM CALLCOND 0x07 path is
// suppressed somewhere"; the fix candidate is to either:
//   a) Stop suppressing it (fix the speedval gate that's too aggressive)
//   b) Manually invoke CheckMoveTransitionTiming after each step tick
//      with synthetic args that match what the bytecode would emit
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "KHitWalker.hpp"
#include "SafeMemoryRead.hpp"

#include <polyhook2/Detour/x64Detour.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace Horse
{
    class EBTracer
    {
    public:
        // RVAs verified via Ghidra (image base = 0x140000000).
        static constexpr uintptr_t kRVA_TickCharaMainSim    = 0x34DA70;
        static constexpr uintptr_t kRVA_TickHitResolution   = 0x33CCA0;
        static constexpr uintptr_t kRVA_TickCharaSecondary  = 0x341CB0;
        static constexpr uintptr_t kRVA_TickHitStopSched    = 0x34D500;
        // MainSim sub-function hooks — narrow down which clears 16EB.
        static constexpr uintptr_t kRVA_ExecuteOpStream     = 0x2FDEA0;
        static constexpr uintptr_t kRVA_TickHitStateSM      = 0x308EC0;
        static constexpr uintptr_t kRVA_FinalizePose        = 0x305B50;
        static constexpr uintptr_t kRVA_TickDamageBehavior  = 0x34E900;
        static constexpr uintptr_t kRVA_UpdateBlockState    = 0x34E820;

        static EBTracer& instance()
        {
            static EBTracer s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[EBTracer] image base unresolved\n"));
                return false;
            }

            install_one(m_detour_mainsim,    m_tramp_mainsim,
                        base + kRVA_TickCharaMainSim,
                        reinterpret_cast<uint64_t>(&detour_mainsim),
                        STR("TickCharaMainSimulation"));
            install_one(m_detour_hitres,     m_tramp_hitres,
                        base + kRVA_TickHitResolution,
                        reinterpret_cast<uint64_t>(&detour_hitres),
                        STR("TickHitResolution"));
            install_one(m_detour_secondary,  m_tramp_secondary,
                        base + kRVA_TickCharaSecondary,
                        reinterpret_cast<uint64_t>(&detour_secondary),
                        STR("TickCharaSecondaryAndDecorators"));
            install_one(m_detour_hitstop,    m_tramp_hitstop,
                        base + kRVA_TickHitStopSched,
                        reinterpret_cast<uint64_t>(&detour_hitstop),
                        STR("TickHitStopScheduler"));

            // ---- MainSim sub-function hooks ----
            install_one(m_detour_opstream,   m_tramp_opstream,
                        base + kRVA_ExecuteOpStream,
                        reinterpret_cast<uint64_t>(&detour_opstream),
                        STR("ExecuteOpStream"));
            install_one(m_detour_hitstate,   m_tramp_hitstate,
                        base + kRVA_TickHitStateSM,
                        reinterpret_cast<uint64_t>(&detour_hitstate),
                        STR("TickHitState"));
            install_one(m_detour_finalize,   m_tramp_finalize,
                        base + kRVA_FinalizePose,
                        reinterpret_cast<uint64_t>(&detour_finalize),
                        STR("FinalizePose"));
            install_one(m_detour_damage,     m_tramp_damage,
                        base + kRVA_TickDamageBehavior,
                        reinterpret_cast<uint64_t>(&detour_damage),
                        STR("TickDamageBehavior"));
            install_one(m_detour_block,      m_tramp_block,
                        base + kRVA_UpdateBlockState,
                        reinterpret_cast<uint64_t>(&detour_block),
                        STR("UpdateBlockState"));

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[EBTracer] installed — will log 16EB transitions per "
                    "engine function\n"));
            return true;
        }

        void uninstall()
        {
            if (!m_installed.exchange(false)) return;
            if (m_detour_mainsim)   { m_detour_mainsim->unHook();   m_detour_mainsim.reset(); }
            if (m_detour_hitres)    { m_detour_hitres->unHook();    m_detour_hitres.reset();  }
            if (m_detour_secondary) { m_detour_secondary->unHook(); m_detour_secondary.reset(); }
            if (m_detour_hitstop)   { m_detour_hitstop->unHook();   m_detour_hitstop.reset(); }
            if (m_detour_opstream)  { m_detour_opstream->unHook();  m_detour_opstream.reset(); }
            if (m_detour_hitstate)  { m_detour_hitstate->unHook();  m_detour_hitstate.reset(); }
            if (m_detour_finalize)  { m_detour_finalize->unHook();  m_detour_finalize.reset(); }
            if (m_detour_damage)    { m_detour_damage->unHook();    m_detour_damage.reset(); }
            if (m_detour_block)     { m_detour_block->unHook();     m_detour_block.reset(); }
        }

    private:
        EBTracer() = default;
        ~EBTracer() { uninstall(); }
        EBTracer(const EBTracer&) = delete;
        EBTracer& operator=(const EBTracer&) = delete;

        std::atomic<bool> m_installed{false};

        std::unique_ptr<PLH::x64Detour> m_detour_mainsim;
        std::unique_ptr<PLH::x64Detour> m_detour_hitres;
        std::unique_ptr<PLH::x64Detour> m_detour_secondary;
        std::unique_ptr<PLH::x64Detour> m_detour_hitstop;
        std::unique_ptr<PLH::x64Detour> m_detour_opstream;
        std::unique_ptr<PLH::x64Detour> m_detour_hitstate;
        std::unique_ptr<PLH::x64Detour> m_detour_finalize;
        std::unique_ptr<PLH::x64Detour> m_detour_damage;
        std::unique_ptr<PLH::x64Detour> m_detour_block;

        uint64_t m_tramp_mainsim{0};
        uint64_t m_tramp_hitres{0};
        uint64_t m_tramp_secondary{0};
        uint64_t m_tramp_hitstop{0};
        uint64_t m_tramp_opstream{0};
        uint64_t m_tramp_hitstate{0};
        uint64_t m_tramp_finalize{0};
        uint64_t m_tramp_damage{0};
        uint64_t m_tramp_block{0};

        // Read 16EB on both chara slots; return packed (P1<<8 | P2)
        // with -1 sentinel on failure.
        static int snapshot_16eb()
        {
            uint8_t p1 = 0xFF, p2 = 0xFF;
            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                void* chara = KHitWalker::charaSlotFromGlobal(pi);
                if (!chara) continue;
                uint8_t v = 0xFF;
                if (SafeReadUInt8(reinterpret_cast<const uint8_t*>(chara) + 0x16EB, &v))
                {
                    if (pi == 0) p1 = v;
                    else         p2 = v;
                }
            }
            return (static_cast<int>(p1) << 8) | static_cast<int>(p2);
        }

        // Snapshot of the bytecode-authored transition staging area on
        // both lanes (0x444F0 + 0x468*idx) of both charas.  These are the
        // fields CheckMoveTransitionTiming reads to decide whether to
        // fire TransitionToMove and clear 16EB.  Empty (-1) means the
        // bytecode hasn't authored a transition.  See LaneSnap below.
        struct LaneSnap
        {
            int16_t L0_5A{-1};   // lane0 immediate transition target
            int16_t L0_B4{-1};   // lane0 deferred transition target
            int16_t L1_5A{-1};
            int16_t L1_B4{-1};
            float   L0_anim{-1.0f};
            float   L1_anim{-1.0f};
        };

        static LaneSnap snapshot_lanes(uint32_t pi)
        {
            LaneSnap s;
            void* chara = KHitWalker::charaSlotFromGlobal(pi);
            if (!chara) return s;
            const auto* b = reinterpret_cast<const uint8_t*>(chara);
            // Lane 0 base = chara + 0x444F0
            // Lane 1 base = chara + 0x44958
            (void)SafeReadInt16(b + 0x444F0 + 0x5A, &s.L0_5A);
            (void)SafeReadInt16(b + 0x444F0 + 0xB4, &s.L0_B4);
            (void)SafeReadInt16(b + 0x44958 + 0x5A, &s.L1_5A);
            (void)SafeReadInt16(b + 0x44958 + 0xB4, &s.L1_B4);
            (void)SafeReadFloat(b + 0x444F0 + 0x08, &s.L0_anim);
            (void)SafeReadFloat(b + 0x44958 + 0x08, &s.L1_anim);
            return s;
        }

        // Returns true if any lane field changed across before/after.
        static bool lanes_changed(const LaneSnap& a, const LaneSnap& b)
        {
            return a.L0_5A != b.L0_5A || a.L0_B4 != b.L0_B4
                || a.L1_5A != b.L1_5A || a.L1_B4 != b.L1_B4;
        }

        static void log_transition(const wchar_t* fn_name, int before, int after,
                                    const LaneSnap (&lanes_before)[2],
                                    const LaneSnap (&lanes_after)[2])
        {
            const bool e16eb_changed = (before != after);
            const bool lane_changed = lanes_changed(lanes_before[0], lanes_after[0])
                                   || lanes_changed(lanes_before[1], lanes_after[1]);
            if (!e16eb_changed && !lane_changed) return;

            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                const uint8_t b = (pi == 0) ? ((before >> 8) & 0xFF) : (before & 0xFF);
                const uint8_t a = (pi == 0) ? ((after  >> 8) & 0xFF) : (after  & 0xFF);
                const LaneSnap& lb = lanes_before[pi];
                const LaneSnap& la = lanes_after[pi];
                const bool ec_eb = (b != a);
                const bool ec_l  = lanes_changed(lb, la);
                if (!ec_eb && !ec_l) continue;

                // Read context: lane1.anim, 16E5 (attacking), 16EA, 18B3 (source).
                uint8_t v_16e5 = 0xFF, v_16ea = 0xFF, v_18b3 = 0xFF;
                int32_t v_2130 = -1;
                int32_t v_3508 = -1;
                void* chara = KHitWalker::charaSlotFromGlobal(pi);
                if (chara)
                {
                    auto* b8 = reinterpret_cast<const uint8_t*>(chara);
                    SafeReadUInt8(b8 + 0x16E5, &v_16e5);
                    SafeReadUInt8(b8 + 0x16EA, &v_16ea);
                    SafeReadUInt8(b8 + 0x18B3, &v_18b3);
                    SafeReadInt32(b8 + 0x2130, &v_2130);
                    SafeReadInt32(b8 + 0x3508, &v_3508);
                }
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[EBTracer] {} P{}: 16EB {:02x}->{:02x} "
                        "L0_5A {}->{} L0_B4 {}->{} L1_5A {}->{} L1_B4 {}->{} "
                        "L0a={:6.2f} L1a={:6.2f} "
                        "16e5={:02x} 16ea={:02x} 18b3={:02x} 2130={} 3508={}\n"),
                    fn_name, pi + 1, b, a,
                    lb.L0_5A, la.L0_5A, lb.L0_B4, la.L0_B4,
                    lb.L1_5A, la.L1_5A, lb.L1_B4, la.L1_B4,
                    la.L0_anim, la.L1_anim,
                    v_16e5, v_16ea, v_18b3, v_2130, v_3508);
            }
        }

        static void capture_lanes(LaneSnap (&out)[2])
        {
            out[0] = snapshot_lanes(0);
            out[1] = snapshot_lanes(1);
        }

        // Detours — call original via trampoline, log 16EB delta.
        // Engine functions are __fastcall void(...).  The caller may
        // pass 0, 1, or 2 args depending on which subsystem.  We pass
        // through registers via raw asm-stub-equivalent here using
        // function-pointer call with the matching prototype.

        // TickCharaMainSimulation: void(longlong* args)
        static void __fastcall detour_mainsim(int64_t* args)
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t*);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_mainsim);
            orig(args);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("MainSim"), before, after, lb, la);
        }

        // TickHitResolutionAndBodyCollision: void()
        static void __fastcall detour_hitres()
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)();
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_hitres);
            orig();
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("HitRes"), before, after, lb, la);
        }

        // TickCharaSecondaryAndDecorators: void(longlong*)
        static void __fastcall detour_secondary(int64_t* args)
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t*);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_secondary);
            orig(args);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("Secondary"), before, after, lb, la);
        }

        // TickHitStopSchedulerAndInputMirror: void()
        static void __fastcall detour_hitstop()
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)();
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_hitstop);
            orig();
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("HitStop"), before, after, lb, la);
        }

        // ExecuteOpStream: void(pVM, laneIdx, ?, ?)
        static void __fastcall detour_opstream(void* pVM, int laneIdx,
                                                 uint64_t a3, uint64_t a4)
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(void*, int, uint64_t, uint64_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_opstream);
            orig(pVM, laneIdx, a3, a4);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("ExecOpStream"), before, after, lb, la);
        }

        // TickHitStateStateMachine: void(chara)
        static void __fastcall detour_hitstate(int64_t chara)
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_hitstate);
            orig(chara);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("HitStateSM"), before, after, lb, la);
        }

        // FinalizeTickPoseAndState: void(chara)
        static void __fastcall detour_finalize(int64_t chara)
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_finalize);
            orig(chara);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("FinalizePose"), before, after, lb, la);
        }

        // TickDamageAndBehaviorLock: void(chara, opp)
        static void __fastcall detour_damage(int64_t chara, int64_t opp)
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t, int64_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_damage);
            orig(chara, opp);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("DamageBehavior"), before, after, lb, la);
        }

        // UpdateBlockStateStochastic: void(chara, opp, opp2)
        static void __fastcall detour_block(int64_t chara, int64_t opp,
                                              int64_t opp2)
        {
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t, int64_t, int64_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_block);
            orig(chara, opp, opp2);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("BlockState"), before, after, lb, la);
        }

        bool install_one(std::unique_ptr<PLH::x64Detour>& detour,
                         uint64_t& trampoline,
                         uintptr_t target,
                         uint64_t hook_fn,
                         const wchar_t* name)
        {
            trampoline = 0;
            detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                hook_fn,
                &trampoline);
            if (!detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[EBTracer] hook failed for {} (target=0x{:X})\n"),
                    name, target);
                detour.reset();
                return false;
            }
            return true;
        }
    };
}
