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
#include "ReplayDebugTrace.hpp"
#include "SafeMemoryRead.hpp"

#include <polyhook2/Detour/x64Detour.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdio>
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
        static constexpr uintptr_t kRVA_UpdateOpponentAngles = 0x305E50;
        static constexpr uintptr_t kRVA_SolveBonePose       = 0x2EDB90;
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
            install_one(m_detour_update_opponent, m_tramp_update_opponent,
                        base + kRVA_UpdateOpponentAngles,
                        reinterpret_cast<uint64_t>(&detour_update_opponent),
                        STR("UpdateOpponentRelativeAngles"));
            install_one(m_detour_solve_pose, m_tramp_solve_pose,
                        base + kRVA_SolveBonePose,
                        reinterpret_cast<uint64_t>(&detour_solve_pose),
                        STR("SolveBonePose"));
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
            if (m_detour_update_opponent) { m_detour_update_opponent->unHook(); m_detour_update_opponent.reset(); }
            if (m_detour_solve_pose) { m_detour_solve_pose->unHook(); m_detour_solve_pose.reset(); }
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
        std::unique_ptr<PLH::x64Detour> m_detour_update_opponent;
        std::unique_ptr<PLH::x64Detour> m_detour_solve_pose;
        std::unique_ptr<PLH::x64Detour> m_detour_damage;
        std::unique_ptr<PLH::x64Detour> m_detour_block;

        uint64_t m_tramp_mainsim{0};
        uint64_t m_tramp_hitres{0};
        uint64_t m_tramp_secondary{0};
        uint64_t m_tramp_hitstop{0};
        uint64_t m_tramp_opstream{0};
        uint64_t m_tramp_hitstate{0};
        uint64_t m_tramp_finalize{0};
        uint64_t m_tramp_update_opponent{0};
        uint64_t m_tramp_solve_pose{0};
        uint64_t m_tramp_damage{0};
        uint64_t m_tramp_block{0};

        static constexpr uintptr_t kCharaVfxEffectAnchorOffset = 0x95FA0;

        static void* safe_provider_index(void* provider, int index) noexcept
        {
            void* vtable = nullptr;
            void* fn_raw = nullptr;
            if (!provider || !SafeReadPtr(provider, &vtable) || !vtable
                || !SafeReadPtr(reinterpret_cast<uint8_t*>(vtable) + 0x28,
                                &fn_raw) || !fn_raw)
                return nullptr;

            using Fn = void*(__fastcall*)(void*, int);
            void* result = nullptr;
            __try
            {
                result = reinterpret_cast<Fn>(fn_raw)(provider, index);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                result = nullptr;
            }
            return result;
        }

        static uint64_t hash_live_bytes(const void* src,
                                        size_t bytes,
                                        bool* ok) noexcept
        {
            if (ok) *ok = false;
            if (!src || bytes == 0 || bytes > 0x100) return 0;
            uint8_t buf[0x100]{};
            if (!SafeReadBytes(src, buf, bytes)) return 0;
            if (ok) *ok = true;
            return ReplayTraceFields::fnv1a64(buf, bytes);
        }

        static void add_provider_hashes(ReplayTraceFields& f,
                                        const char* prefix,
                                        uint8_t* chara) noexcept
        {
            if (!prefix || !chara) return;

            auto add_hash = [&](const char* suffix,
                                const void* ptr,
                                size_t bytes) noexcept
            {
                bool ok = false;
                const uint64_t h = hash_live_bytes(ptr, bytes, &ok);
                char key[96]{};
                std::snprintf(key, sizeof(key), "%s_%s_ok", prefix, suffix);
                f.boolean(key, ok);
                std::snprintf(key, sizeof(key), "%s_%s_hash", prefix, suffix);
                f.hex(key, static_cast<uintptr_t>(h));
            };

            void* primary_root = safe_provider_index(chara + 0x35A0, 0);
            void* primary_bone1 = safe_provider_index(chara + 0x35A0, 1);
            add_hash("primary_root0_matrix", primary_root, 0x40);
            add_hash("primary_root1_matrix", primary_bone1, 0x40);
            add_hash("primary_root1_translation",
                     primary_bone1 ? static_cast<uint8_t*>(primary_bone1) + 0x30
                                   : nullptr,
                     0x10);
            add_hash("primary_delta_5f0",
                     primary_root ? static_cast<uint8_t*>(primary_root) + 0x5F0
                                  : nullptr,
                     0x40);
            add_hash("primary_sub_de4",
                     primary_root ? static_cast<uint8_t*>(primary_root) + 0xDE4
                                  : nullptr,
                     0x40);

            void* secondary_root = safe_provider_index(chara + 0x27760, 0);
            void* secondary_bone1 = safe_provider_index(chara + 0x27760, 1);
            add_hash("secondary_root1_matrix", secondary_bone1, 0x40);
            add_hash("secondary_delta_5f0",
                     secondary_root ? static_cast<uint8_t*>(secondary_root) + 0x5F0
                                    : nullptr,
                     0x40);
        }

        static int player_index_for_chara(void* chara) noexcept
        {
            if (!chara) return -1;
            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                if (KHitWalker::charaSlotFromGlobal(pi) == chara)
                    return static_cast<int>(pi);
            }
            return -1;
        }

        static void emit_chara_lifecycle(const char* stage,
                                         const char* phase,
                                         void* chara) noexcept
        {
            if (!chara) return;
            uint8_t* c = reinterpret_cast<uint8_t*>(chara);
            const int pi = player_index_for_chara(chara);

            float pos_x = 0.0f, pos_z = 0.0f, step_x = 0.0f, step_z = 0.0f;
            float facing = 0.0f, opp_dist = 0.0f, opp_angle = 0.0f;
            float ground_x = 0.0f, ground_z = 0.0f, one_shot_x = 0.0f;
            float one_shot_z = 0.0f, root_x = 0.0f, root_z = 0.0f;
            float clip_frame = 0.0f;
            uint32_t move_id = 0;
            int32_t replay_frame = -1, replay_master = -1;
            (void)SafeReadFloat(c + 0xA0, &pos_x);
            (void)SafeReadFloat(c + 0xA8, &pos_z);
            (void)SafeReadFloat(c + 0xC0, &step_x);
            (void)SafeReadFloat(c + 0xC8, &step_z);
            (void)SafeReadFloat(c + 0x94, &facing);
            (void)SafeReadFloat(c + 0x15A0, &opp_dist);
            (void)SafeReadFloat(c + 0x15A4, &opp_angle);
            (void)SafeReadFloat(c + 0x140, &ground_x);
            (void)SafeReadFloat(c + 0x148, &ground_z);
            (void)SafeReadFloat(c + 0x150, &one_shot_x);
            (void)SafeReadFloat(c + 0x158, &one_shot_z);
            (void)SafeReadFloat(c + 0x180, &root_x);
            (void)SafeReadFloat(c + 0x188, &root_z);
            (void)SafeReadFloat(c + 0x2B47C, &clip_frame);
            (void)SafeReadUInt32(c + 0x324, &move_id);
            (void)SafeReadInt32(c + 0x3A0, &replay_frame);
            (void)SafeReadInt32(c + 0x3A4, &replay_master);

            ReplayTraceFields f;
            f.string("stage", stage ? stage : "?")
             .string("phase", phase ? phase : "?")
             .integer("player", pi >= 0 ? pi + 1 : 0)
             .hex("chara", reinterpret_cast<uintptr_t>(chara))
             .integer("replay_frame", replay_frame)
             .integer("replay_master", replay_master)
             .uinteger("move_id", move_id)
             .real("clip_frame", clip_frame)
             .real("pos_x", pos_x)
             .real("pos_z", pos_z)
             .real("step_x", step_x)
             .real("step_z", step_z)
             .real("facing", facing)
             .real("opponent_distance", opp_dist)
             .real("opponent_angle", opp_angle)
             .real("ground_vel_x", ground_x)
             .real("ground_vel_z", ground_z)
             .real("one_shot_x", one_shot_x)
             .real("one_shot_z", one_shot_z)
             .real("root_delta_x", root_x)
             .real("root_delta_z", root_z);
            add_provider_hashes(f, "provider", c);
            ReplayDebugTrace::instance().event("native_chara_lifecycle", f);
        }

        static void emit_lifecycle_slots(const char* stage,
                                         const char* phase) noexcept
        {
            for (uint32_t pi = 0; pi < 2; ++pi)
                emit_chara_lifecycle(stage, phase,
                                     KHitWalker::charaSlotFromGlobal(pi));
        }

        static void* read_slot_arg_chara(int64_t* args) noexcept
        {
            void* chara = nullptr;
            if (args) (void)SafeReadPtr(args, &chara);
            return chara;
        }

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
            void* chara = read_slot_arg_chara(args);
            emit_chara_lifecycle("TickCharaMainSimulation", "enter", chara);
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t*);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_mainsim);
            orig(args);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("MainSim"), before, after, lb, la);
            emit_chara_lifecycle("TickCharaMainSimulation", "exit", chara);
        }

        // TickHitResolutionAndBodyCollision: void()
        static void __fastcall detour_hitres()
        {
            emit_lifecycle_slots("TickHitResolutionAndBodyCollision", "enter");
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)();
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_hitres);
            orig();
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("HitRes"), before, after, lb, la);
            emit_lifecycle_slots("TickHitResolutionAndBodyCollision", "exit");
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
            emit_chara_lifecycle("FinalizeTickPoseAndState", "enter",
                                 reinterpret_cast<void*>(chara));
            LaneSnap lb[2]; capture_lanes(lb);
            const int before = snapshot_16eb();
            using Fn = void(__fastcall*)(int64_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_finalize);
            orig(chara);
            LaneSnap la[2]; capture_lanes(la);
            const int after = snapshot_16eb();
            log_transition(STR("FinalizePose"), before, after, lb, la);
            emit_chara_lifecycle("FinalizeTickPoseAndState", "exit",
                                 reinterpret_cast<void*>(chara));
        }

        // UpdateOpponentRelativeAngles: void(chara)
        static void __fastcall detour_update_opponent(int64_t chara)
        {
            emit_chara_lifecycle("UpdateOpponentRelativeAngles", "enter",
                                 reinterpret_cast<void*>(chara));
            using Fn = void(__fastcall*)(int64_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_update_opponent);
            orig(chara);
            emit_chara_lifecycle("UpdateOpponentRelativeAngles", "exit",
                                 reinterpret_cast<void*>(chara));
        }

        // SolveBonePose: void(vfxEffectAnchorBlock, primaryProviderBuffer, flags)
        static void __fastcall detour_solve_pose(int64_t anchor,
                                                  float* primary,
                                                  uint32_t flags)
        {
            void* chara = anchor
                ? reinterpret_cast<void*>(
                    static_cast<uintptr_t>(anchor) - kCharaVfxEffectAnchorOffset)
                : nullptr;
            emit_chara_lifecycle("SolveBonePose", "enter", chara);
            using Fn = void(__fastcall*)(int64_t, float*, uint32_t);
            Fn orig = reinterpret_cast<Fn>(instance().m_tramp_solve_pose);
            orig(anchor, primary, flags);
            emit_chara_lifecycle("SolveBonePose", "exit", chara);
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
