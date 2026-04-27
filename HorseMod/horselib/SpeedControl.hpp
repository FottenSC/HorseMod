// ============================================================================
// Horse::SpeedControl — global simulation-rate override via trampolines that
// hijack the engine's time-dilation reads.
//
// Origin
// ------
// Ported from somberness's CE table ("SC6nepafu.CT", "Speed control v2").
// The CE script targets 5 sites (one was a sixth in v2 that's a single-
// byte register-swap fix for low-speed rounding) where the engine reads
// either a global delta-time/time-scale float or the per-VM/per-actor
// time-dilation field at offset +0x2080.  Each load is replaced with a
// load from a single user-controlled `speedval` slot:
//
//   Site 1: KHit_SolvePendulumConstraint           movss xmm14, [global]
//   Site 2: (refactored away in current build)
//   Site 3: LuxMoveVM_GetTimeDilationScalar         movss xmm0,  [global]
//   Site 4: LuxMoveVM_AdvanceLinkedMotionObject     movss xmm0,  [pVM+0x2080]
//   Site 5: LuxMoveVM_ExecuteOpStream               movss [pVM+0x2080], xmm0
//                                                   (STORE re-purposed as
//                                                   load — the engine's
//                                                   value never makes it
//                                                   to +0x2080, but our
//                                                   speedval load lands
//                                                   in xmm0 which the
//                                                   engine doesn't read
//                                                   downstream of this
//                                                   point, so net == nuke
//                                                   the store harmlessly)
//   Site 6: LuxMoveVM_AdvanceLaneFrameStep          cvttss2si ecx, xmm4
//                                                   single-byte ModRM
//                                                   patch CC -> CB
//                                                   (xmm4 -> xmm3) so
//                                                   the int-cast picks
//                                                   up the smaller
//                                                   intermediate when
//                                                   speedval is tiny —
//                                                   prevents 0-frame
//                                                   advance rounding at
//                                                   speedval ≈ 0.001
//
//   Site 7: LuxMoveVM_PostATKDelayGate                ENTRY-HOOK trampoline
//                                                   (added 2026-04-25 to fix
//                                                   "delay between moves not
//                                                   frozen" bug)
//
//                                                   This function is called
//                                                   from LuxMoveVM_TickDriver
//                                                   BEFORE the GetTimeDilation
//                                                   gate at site 3. Sites 1/3/
//                                                   4/5 patch the *integrators*
//                                                   that read dt — but this
//                                                   function decrements an int
//                                                   counter (vmCtx+0xCF0,
//                                                   nPostATKRemainingDelayFrames)
//                                                   by literal 1 every call,
//                                                   no dt scaling. Result:
//                                                   when speedval=0 the chara
//                                                   animation freezes (good)
//                                                   but the move's post-ATK
//                                                   recovery countdown still
//                                                   drains in real time. The
//                                                   user reported this as
//                                                   "the delay between moves
//                                                   isn't frozen".
//
//                                                   Fix: hook the function's
//                                                   entry with a trampoline
//                                                   that early-returns 0
//                                                   (false, "still gated")
//                                                   when speedval == 0. The
//                                                   chara+0x16E6 pause-flag
//                                                   path the engine already
//                                                   uses for its own freezes
//                                                   takes the same exit so
//                                                   semantics match.
//
//   Site 9: LuxBattle_PerFrameTick                    BLANKET FREEZE entry-hook
//                                                   (added 2026-04-25 to fix
//                                                   "replay timer / replay
//                                                   inputs / round timer
//                                                   keep ticking under
//                                                   freeze" bug)
//
//                                                   Sites 1/3/4/5/6/7/8 are
//                                                   surgical — each fixes a
//                                                   specific subsystem.  But
//                                                   the SC6 battle tick has
//                                                   too many independently-
//                                                   driven counters (round
//                                                   timer, replay cursor,
//                                                   input ring age, replay
//                                                   chara-frame copier,
//                                                   chara+0x3508 hitstop,
//                                                   etc.) to chase one by
//                                                   one.  Site 9 takes the
//                                                   blunt instrument: hook
//                                                   the very TOP of
//                                                   LuxBattle_PerFrameTick
//                                                   and bare-RET when
//                                                   *speedval == 0.  The
//                                                   function is `void`,
//                                                   takes args via RCX, and
//                                                   the trampoline runs
//                                                   BEFORE the prologue
//                                                   pushes anything to the
//                                                   stack — so a plain RET
//                                                   leaves the stack
//                                                   untouched.
//
//                                                   What this freezes (now
//                                                   correctly): VM pump,
//                                                   per-chara input tick
//                                                   (live + replay), main
//                                                   simulation, hit
//                                                   resolution, secondary
//                                                   tick, hitstop scheduler,
//                                                   camera synthesis,
//                                                   camera fade schedules,
//                                                   stage wind, slot-param
//                                                   lerp, frame-counter,
//                                                   round/timer ticks.
//
//                                                   What still runs (good):
//                                                   UE4 actor tick chain
//                                                   (rendering), Slate
//                                                   widget tick (UI), DX11
//                                                   present hook (this is
//                                                   where HorseMod's ImGui
//                                                   speedval slider lives)
//                                                   — so the user can still
//                                                   un-freeze.
//
//                                                   Composes with sites
//                                                   1/3/4/5/6/7/8: those
//                                                   give proper slow-mo at
//                                                   0 < speedval < 1.  Site
//                                                   9 only fires at exactly
//                                                   speedval == 0.
//
//                                                   Frame-step compatibility:
//                                                   the cockpit-tick frame-
//                                                   step machinery sets
//                                                   speedval = 1.0 for one
//                                                   tick then back to 0.0.
//                                                   During that one tick
//                                                   site 9 doesn't fire,
//                                                   PerFrameTick runs
//                                                   normally, world advances
//                                                   one frame.  Exactly the
//                                                   semantics the user
//                                                   wants.
//
//   Site 8: LuxCameraAction_AdvancePlayback           MULSS-INSERT trampoline
//                                                   (added 2026-04-25 to fix
//                                                   "freeze + step desyncs
//                                                   from replay camera"
//                                                   bug)
//
//                                                   Move-cinematic cameras
//                                                   (Critical Edges, super
//                                                   flashes, knockdown
//                                                   reactions, replay
//                                                   playback) integrate
//                                                   their playback frame
//                                                   counter inside this
//                                                   function with a per-
//                                                   action speed at +0x80,
//                                                   NOT through
//                                                   GetTimeDilationScalar.
//                                                   So sites 1/3/4/5 don't
//                                                   touch it. Symptom in
//                                                   replay scrubbing: chara
//                                                   animations freeze fine
//                                                   but the cinematic
//                                                   camera continues to
//                                                   sweep, desyncing the
//                                                   composition.
//
//                                                   This site uses a
//                                                   different shape than
//                                                   sites 1/3/4/5: instead
//                                                   of replacing a load,
//                                                   we INSERT a MULSS that
//                                                   scales the per-action
//                                                   speed (already in xmm1
//                                                   after the engine's own
//                                                   freeze-blend selection)
//                                                   by speedval, then
//                                                   replicate the displaced
//                                                   MOVAPS+ADDSS and jump
//                                                   back. Multiplicative,
//                                                   so engine freeze and
//                                                   user freeze compose
//                                                   correctly: both must
//                                                   evaluate to nonzero
//                                                   for the camera to
//                                                   advance.
//
// User contract
// -------------
// Set speedval to:
//     0.0   = full freeze (stronger than GamePause — also halts audio
//             loops, particle lifetimes, camera sweeps because they
//             integrate against the same dt)
//     0.001 = 1000x slow-mo (one game-frame per ~17 real seconds)
//     0.01  = 100x slow-mo
//     0.1   = 10x slow-mo
//     0.5   = half speed
//     1.0   = normal
//     >1.0  = sped up (works but unbounded — at very high values the
//             physics solver may explode)
//
// The original CE author's hotkey presets only covered 0..1 in factor-
// of-10 steps; HorseMod exposes a continuous slider plus a few buttons
// for the common analysis values.
//
// Interaction with other features
// -------------------------------
//   * GamePause (chara+0x394 bit 0): independent.  Pause stops the chara
//     state machine via a different gate; SpeedControl scales every
//     dt-driven subsystem.  speedval=0 is essentially a stronger pause
//     that affects audio + particles too.
//   * Frame-step (F6): also independent.  Step advances one chara tick
//     regardless of speedval.  But if speedval=0 + paused + step, the
//     step's worth of time advancement is also scaled to 0 — practically
//     you'd want speedval back to 1.0 if frame-stepping.
//
// Limits
// ------
//   * 5 sites is what the CE script targets and what we mirror.  The
//     UE4 layer (Slate animations, post-process FX, render thread
//     timestep) runs off the engine's own GFrameTime which we don't
//     touch — so the LuxMoveVM simulation slows down, but UI feels
//     responsive.  This is a feature, not a bug, for hitbox analysis.
//   * speedval > ~10.0 may desynchronise networked play in online
//     matches.  Don't speed-hack online.
// ============================================================================

#pragma once

#include "BytePatch.hpp"
#include "CodeCave.hpp"
#include "SigScan.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <cstdint>
#include <cstring>

namespace Horse
{
    class SpeedControl
    {
    public:
        // Sig-scan all 5 trampoline sites + 1 byte-patch site, allocate
        // the shared speedval slot + 4 trampolines from CodeCave, prepare
        // every BytePatch.  Idempotent.  Returns true iff EVERY site
        // resolved — partial coverage would leave the engine in a half-
        // overridden state where some subsystems run at speedval and
        // others at native dt, producing ugly desync, so we'd rather
        // refuse the toggle entirely than ship that.
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved = true;
            m_resolved_ok = false;

            // ---- AOB scan all sites ----
            void* site1 = sig_scan_sc6(
                "F3 44 0F 10 35 ?? ?? ?? ?? 45 0F 28 EE",
                "SpeedControl site1 (KHit_SolvePendulumConstraint movss xmm14)");
            // Site 2 was the +rbp+0x2020 path; refactored out of the
            // current build, so we don't try to find it.
            void* site3 = sig_scan_sc6(
                "F3 0F 10 05 ?? ?? ?? ?? F3 0F 59 81",
                "SpeedControl site3 (GetTimeDilationScalar movss xmm0)");
            void* site4 = sig_scan_sc6(
                "F3 0F 10 81 80 20 00 00",
                "SpeedControl site4 (AdvanceLinkedMotionObject movss xmm0,[rcx+0x2080])");
            void* site5 = sig_scan_sc6(
                "F3 0F 11 86 80 20 00 00",
                "SpeedControl site5 (ExecuteOpStream movss [rsi+0x2080],xmm0 -> redirect to load)");
            void* site6 = sig_scan_sc6(
                "F3 0F 2C CC F3 41 0F 11 62 08",
                "SpeedControl site6 (AdvanceLaneFrameStep cvttss2si ecx,xmm4 -> xmm3)");
            // Site 7 — LuxMoveVM_PostATKDelayGate entry. AOB anchors on the
            // function prologue's full 8-byte preamble:
            //   48 83 EC 28              SUB RSP, 0x28
            //   48 8B 51 08              MOV RDX, [RCX+0x8]
            //   4C 8B D9                 MOV R11, RCX
            //   80 BA E6 16 00 00 00     CMP byte ptr [RDX+0x16E6], 0
            // We anchor 18 bytes deep (the unique CMP offset is what makes
            // this not match other 0x28-stack-frame functions) so the patch
            // doesn't false-match a similarly-prologued helper.
            void* site7 = sig_scan_sc6(
                "48 83 EC 28 48 8B 51 08 4C 8B D9 80 BA E6 16 00 00 00",
                "SpeedControl site7 (PostATKDelayGate entry-hook for freeze fix)");
            // Site 8 — LuxCameraAction_AdvancePlayback dt-scaling MULSS insert.
            // Anchors on the convergence point of the negative-speed branch:
            //   0F 28 C1                 MOVAPS xmm0, xmm1   (3B)
            //   F3 0F 58 41 28           ADDSS  xmm0, [rcx+0x28] (5B)
            //   0F 5B D2                 CVTDQ2PS xmm2, xmm2  (3B; trailing
            //                                                   anchor for
            //                                                   uniqueness)
            // The patch (8B) overwrites the MOVAPS+ADDSS pair with a JMP
            // rel32 (5B) + 3 NOPs.  The trampoline pre-multiplies xmm1 by
            // speedval, then re-runs the displaced MOVAPS+ADDSS, then JMPs
            // back to the CVTDQ2PS continuation.
            void* site8 = sig_scan_sc6(
                "0F 28 C1 F3 0F 58 41 28 0F 5B D2",
                "SpeedControl site8 (CameraAction_AdvancePlayback dt scale)");
            // Site 9 — LuxBattle_PerFrameTick entry, blanket freeze hook.
            // Anchors on the function's full prologue down to & including
            // the SUB RSP, 0x80 imm32 byte (the second LuxBattle_*
            // function in the binary has the same first 19 bytes but
            // SUB RSP, 0xC0 — including the imm32 disambiguates).
            void* site9 = sig_scan_sc6(
                "4C 8B DC 49 89 5B 10 49 89 6B 18 56 57 41 54 41 56 41 57 48 81 EC 80 00 00 00",
                "SpeedControl site9 (LuxBattle_PerFrameTick blanket-freeze entry-hook)");
            // ---- Replay-side actor-tick freeze hooks ----
            //
            // The SC6 replay system runs a parallel set of UE4 Actor::Tick
            // handlers that are NOT called from LuxBattle_PerFrameTick.
            // They tick directly off UE4's world tick scheduler.  Site 9
            // doesn't reach them, so even with PerFrameTick frozen the
            // replay system continues:
            //   * advancing its playback frame
            //   * copying recorded chara state into the BattleManager
            //   * notifying move-end and changing move state
            //
            // User-visible: "watching back replays with tick freeze the
            // inputs seem to keep playing.  The move a character was
            // doing changed 5 seconds after I froze the gameplay."
            //
            // Sites 10/11/12 are entry-hooks on the three known replay
            // tick handlers.  Each early-RETs when *speedval == 0.
            //
            // Site 10 — LuxReplayChara_Tick_CopyNextFrameToManager_SetMoveState4.
            // The frame-buffer copy: reads the current playback cursor
            // from the replay actor, indexes into a 0xC0-byte-stride
            // buffer, copies 192 bytes of chara snapshot into the
            // BattleManager at +0x1360..+0x141C, then SetMoveState(4).
            // This is the function that's making the chara's pose
            // change during freeze.
            void* site10 = sig_scan_sc6(
                "48 89 5C 24 10 57 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8D 8B 88 03 00 00",
                "SpeedControl site10 (LuxReplayChara_Tick_CopyNextFrameToManager freeze)");
            // Site 11 — LuxBattleChara_Tick_AdvanceReplayFrame_OrLocal.
            // Per-chara replay-frame dispatch.  When +0x4424 == 2
            // (replay mode), calls vtable[0x6c8] with the cursor at
            // +0x39C — that vtable function is what advances the chara
            // playback state.
            void* site11 = sig_scan_sc6(
                "40 53 48 83 EC 30 83 B9 00 44 00 00 00 48 8B D9 0F 84",
                "SpeedControl site11 (LuxBattleChara_Tick_AdvanceReplayFrame freeze)");
            // Site 12 — LuxBattleChara_Tick_CheckFlagsAndNotifyMoveEnded_State2.
            // Replay/playback state-end watcher.  Reads recorded flag
            // bits, calls SetMoveState(2) and NotifyCharaMoveEnded when
            // the recording's bit 9 fires.  Same UE4 Actor::Tick path,
            // independent of PerFrameTick.
            void* site12 = sig_scan_sc6(
                "48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F1 E8 ?? ?? ?? ?? 48 8D AE 88 03 00 00",
                "SpeedControl site12 (LuxBattleChara_Tick_CheckFlagsAndNotifyMoveEnded freeze)");
            // Site 13 — LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState.
            // The 4TH (and master) replay-side driver.  This function is
            // called from LuxBattleManager_Tick_MainStateMachine_At1461
            // when the BattleManager state byte at +0x1461 == 2 (active
            // battle state).  Inside, it reads the live frame-target at
            // BM->ReplayActor +0x3A0/+0x3A4, computes the catch-up delta,
            // and **LOOPS that many times** running per-chara input updates
            // (LuxBattleChara_UpdatePlayerInputData_FromRoundCache) and
            // round-state advances (LuxBattleManager_Tick_ProcessRoundStateSequence).
            //
            // Sites 10/11/12 stop the chara-side replay-state copy paths,
            // but +0x3A4 keeps advancing externally (probably from a UE4
            // input/timer actor) and SimulationLoop's catch-up loop keeps
            // running here.  When the user unfreezes, the loop processes
            // ALL the missed frames at once and the chara's move silently
            // changes to whatever-it-would-be-now.
            //
            // Hooking SimulationLoop's entry blocks the catch-up loop
            // entirely.  When the user unfreezes, +0x3A4 has advanced but
            // SimulationLoop notices the delta and processes them in one
            // burst — same behaviour as before, just without the silent
            // mid-freeze drift.  If users ALSO want "freeze means freeze
            // forever, no catch-up on unfreeze", that requires also
            // gating whatever advances +0x3A4 (a separate site, TBD).
            void* site13 = sig_scan_sc6(
                "4C 8B DC 53 48 81 EC E0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 B0 00 00 00",
                "SpeedControl site13 (BattleManager_Tick_SimulationLoop replay catch-up freeze)");
            // ---- Sites 14/15 — replay-clock INC freezers (RE-ENABLED) ----
            //
            // The chara+0x3A4 replay cursor is INC'd by two sibling
            // wrapper functions; either alone will keep the cursor moving
            // during freeze, which makes SimulationLoop's catch-up loop
            // (suppressed by site 13) fire a giant burst on unfreeze.
            //
            //   site14 -> LuxBattleChara_VTable648_TickAndAdvanceReplayClock
            //              @ 0x1403E1FC0  ; INC at function +0x2A = 0x1403E1FEA
            //   site15 -> LuxBattleChara_VTable648_TickAndAdvanceReplayClock_GatedBy4404
            //              @ 0x1403E2000  ; INC at function +0x33 = 0x1403E2033
            //
            // FIRST ATTEMPT (boot-crashed): hook the function ENTRY with
            // a freeze-entry-hook (bare-RET on speedval==0).  Failed at
            // boot, suspected reasons documented in git history (most
            // plausible: bare-RET corrupted state when these functions
            // were invoked during chara construction / pre-init paths).
            //
            // CURRENT APPROACH: patch only the `INC dword [RBX+0x3A4]`
            // instruction itself (mid-function, after prologue + vtable
            // calls have already run).  Trampoline does
            //   if (*speedval == 0) skip the INC;
            //   else                run the INC;
            //   jump back to the function's epilogue.
            //
            // Why this is safer than the entry hook:
            //   * No bare-RET — we never short-circuit the function.
            //   * Stack is balanced regardless of speedval (the prologue
            //     and vtable[0x650]/[0x640]/[0x648] calls always run; we
            //     only skip the +0x3A4 cursor advance at the very end).
            //   * Pre-init / vtable-construction paths through the
            //     function will execute the prologue + vtable calls
            //     normally, just without bumping the replay cursor — and
            //     during those paths the cursor isn't being consumed
            //     anyway, so skipping the INC is semantically harmless.
            //   * SEH unwind data only describes the prologue's effect
            //     on the stack; we leave the prologue alone, so unwind
            //     from anywhere in the function body still works.
            //
            // We re-use the existing function-start AOBs to LOCATE the
            // function, then derive the patch site as start + INC_offset.
            // (The INC bytes themselves `FF 83 A4 03 00 00` are identical
            // in both functions and not unique against a wider search,
            // so AOB-scanning the INC directly would be ambiguous.)
            void* site14_fn = sig_scan_sc6(
                "40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 90 50 06 00 00 48 8B 03 48 8B CB FF 90 40 06 00 00",
                "SpeedControl site14 (VTable648 unconditional clock advancer fn entry)");
            void* site15_fn = sig_scan_sc6(
                "40 53 48 83 EC 20 80 B9 04 44 00 00 00 48 8B D9 48 8B 01 75 18",
                "SpeedControl site15 (VTable648 gated-by-4404 clock advancer fn entry)");
            // INC offsets within each function (verified via Ghidra
            // disassembly).  See plate comments on those functions.
            void* site14 = site14_fn ? static_cast<uint8_t*>(site14_fn) + 0x2A
                                     : nullptr;
            void* site15 = site15_fn ? static_cast<uint8_t*>(site15_fn) + 0x33
                                     : nullptr;

            // ---- Site 16 — second-tier replay cursor (chara+0x4410) ----
            //
            // Discovered after sites 14/15 still didn't give "freeze means
            // freeze" semantics, particularly after restart-round.  There
            // is a *second* replay-cursor field on the chara — +0x4410 —
            // that gets bumped by a different code path entirely:
            //
            //   LuxBattleChara_AdvanceFrameCounter_At4410_WhileVtable6b0True
            //   @ 0x1403FE8F0
            //
            // The function loops on `chara->vtable[0x6B0]()` (a "buffered
            // frame ready?" predicate) and INCs +0x4410 on each iteration
            // until the predicate returns false.  It has TWO callers:
            //   (a) LuxBattleManager_Tick_SimulationLoop (covered by site 13)
            //   (b) LuxBattleChara_SetStageInitPhase_AndTrigger
            //       @ 0x1403F8380, which is reachable from a Blueprint
            //       UFunction RPC fired by "restart round".  Site 13 does
            //       NOT cover this path — it's not on the actor-tick chain.
            //
            // After restart-round, +0x4410 gets pushed forward by path (b)
            // outside any frozen tick.  The next time
            // LuxBattleChara_Tick_AdvanceReplayFrame_OrLocal runs, it sees
            //   chara+0x4410 > chara+0x440C + chara+0x3A4
            // and dispatches a live-tick burst on chara->vtable[0x650/640/
            // 6a0/6a8] — even though the user is "frozen".  That's the
            // "doesn't play like its supposed to" symptom.
            //
            // Same patch shape as 14/15: replace the 6-byte
            // `INC dword [rbx+0x4410]` (FF 83 10 44 00 00) at function
            // offset +0x30 with a JMP rel32 + NOP into a trampoline that
            // skips the INC when speedval == 0.
            //
            // Note: the helper agent reported the AOB with a typo
            // (`44 8B 8B` instead of `44 8B 89`); the version below is
            // verified against actual bytes via Ghidra read_memory.
            void* site16_fn = sig_scan_sc6(
                "40 53 48 83 EC 30 48 8B 01 48 8B D9 44 8B 89 10 44 00 00 44 8B 81 A0 03 00 00",
                "SpeedControl site16 (chara+0x4410 second-tier cursor advancer fn entry)");
            void* site16 = site16_fn ? static_cast<uint8_t*>(site16_fn) + 0x30
                                     : nullptr;

            // ---- Site 19 — LuxBattleChara_ReplayPlayback_PushInputsToActiveSlots entry-RET ----
            //
            // THE MATCH-REPLAY INPUT PUSHER.  The user pointed out that
            // SC6 has TWO replay systems: training-mode (KeyRecorder, our
            // disabled site 17) AND full-match-replay (saved .replay
            // files, distinct pipeline).  The latter pushes inputs via
            // this function — which has a CATCH-UP loop that pushes
            // multiple frames per call when the cursor lags.
            //
            // Found via Ghidra search after user's "investigate the wrong
            // place" hint.  An EXISTING plate on this function (added in
            // an earlier session) explicitly predicted:
            //
            //   "If users still report replay-input drift under freeze
            //    AFTER site 9 is deployed, this function would be the
            //    next surgical candidate: hook the 'while cursor <
            //    target' loop body to consult speedval."
            //
            // The plate's reasoning: PerFrameTick (gated by site 9) is
            // ONE caller, but the chara's UE4 Actor::Tick can reach
            // here through other vtable paths that site 9 doesn't
            // touch.  Match-replay viewing is one such path.
            //
            // Function body summary:
            //   while (chara[0x76] < min(chara[0x3A4]+1, chara[0x3B4])) {
            //       for each player slot, decode a 16-byte input from the
            //         per-chara replay ring at chara+0x3C0 + slot*0x200
            //         + (cursor & 0x1FF) * 0x10
            //       LuxMove_ForEachSlot_SendParam_IfActive(chara, slots,
            //         decoded_inputs)
            //       chara[0x3B0]++   // advance cursor
            //   }
            //
            // Each loop iter PUSHES inputs to active LuxMove slots and
            // INCREMENTS the cursor.  If the cursor was lagging by N
            // frames, ALL N get pushed in this single call — exactly
            // the symptom the user described as "many copies after
            // 1-tick step / forwarding".
            //
            // FIX: bare-RET on speedval == 0 at function entry.  Same
            // shape as site 10/11/12.  The function is `void(chara*)`
            // MS x64 — bare-RET with no stack adjustment is correct
            // because we hook BEFORE the function's PUSH RBX.
            //
            // Prologue: PUSH RBX + SUB RSP,0x50 = 5 bytes.  Anchor uses
            // a wider 29-byte AOB (push+sub+security cookie load+xor+
            // store+mov rbx,rcx+mov ecx,[rcx+0x3B0]) to disambiguate.
            // Anchor AOB (30 bytes — includes the REX prefix on PUSH RBX
            // and the correct ModRM byte 0x89 for `MOV ECX,[RCX+0x3B0]`):
            //   40 53                  push rbx        (REX + opcode = 2B)
            //   48 83 EC 50            sub rsp, 0x50   (4B)
            //   48 8B 05 ?? ?? ?? ??   mov rax, [rip+security_cookie]
            //   48 33 C4               xor rax, rsp
            //   48 89 44 24 28         mov [rsp+0x28], rax
            //   48 8B D9               mov rbx, rcx
            //   8B 89 B0 03 00 00      mov ecx, [rcx+0x3B0]   (cursor)
            void* site19 = sig_scan_sc6(
                "40 53 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 28 48 8B D9 8B 89 B0 03 00 00",
                "SpeedControl site19 (LuxBattleChara_ReplayPlayback_PushInputsToActiveSlots — match-replay input pusher freeze)");

            // ---- Site 20 — LuxReplay_ConsumeDecodedInputPackets_AndUpdateCache entry-RET ----
            //
            // SECOND BREAKTHROUGH (2026-04-26): Site 19 blocks PUSH (stage 3
            // of replay input pipeline) but the CACHE FILLER (stage 2)
            // wasn't gated.  During freeze, decoded inputs accumulate in
            // the chara ring at chara+0x3C0..+0x43C0.  On step,
            // PushInputs's catch-up loop reads ALL accumulated entries
            // and pushes them to the display in one game frame =
            // "many duplicates after step forward".
            //
            // The replay input pipeline:
            //   Stage 1: LuxReplay_DecodeInputPackets — parses replay
            //            file packets into BM+0x460 buffer
            //   Stage 2: THIS FUNCTION — drains decoded buffer, writes
            //            16-byte entries into chara ring at chara+0x3C0
            //            per slot per packet
            //   Stage 3: LuxBattleChara_ReplayPlayback_PushInputsToActiveSlots
            //            (site 19) — pushes cached ring entries to
            //            LuxMove slots → display
            //
            // FIX: bare-RET on speedval==0 at function entry.  When
            // frozen, cache fill stops, ring stays at pre-freeze state,
            // PushInputs has no catch-up to do, step produces 1 input.
            //
            // Anchor AOB (15 bytes — push rdi + sub rsp + security cookie + xor):
            //   57                    push rdi
            //   48 83 EC 60           sub rsp, 0x60
            //   48 8B 05 ?? ?? ?? ?? mov rax, [rip+security_cookie]
            //   48 33 C4              xor rax, rsp
            void* site20 = sig_scan_sc6(
                "57 48 83 EC 60 48 8B 05 ?? ?? ?? ?? 48 33 C4",
                "SpeedControl site20 (LuxReplay_ConsumeDecodedInputPackets_AndUpdateCache — replay cache filler freeze)");

            // ---- Site 21 — LuxBattleManager_Tick_MainStateMachine_At1461 entry-RET ----
            //
            // THE BATTLE-MANAGER ACTOR::TICK WRAPPER.  This function is the
            // BM's UE4 Actor::Tick entry point.  It:
            //   - dispatches by state byte at BM+0x1461 (states 0/1/2/3)
            //   - in state 2 (active battle): calls SimulationLoop (Site 13)
            //   - bumps the BM+0x1440 counter when BM+0x1480 == 2 and
            //     dispatches vtable[0x138] (online sync path)
            //   - TAIL-CALLS AActor_TickActor(pBM, deltaTime) at the END
            //
            // The tail-call to AActor_TickActor fans out to ALL registered
            // component ticks — including the ALuxBattleFrameInputLog
            // actor (BM+0x478) which has its own AActor::Tick.  Site 13
            // gates SimulationLoop's INNER catch-up loop, but NOTHING in
            // the existing 16+19+20 patches catches AActor_TickActor's
            // component fan-out.  That fan-out runs every UE4 frame
            // regardless of HorseMod freeze, advancing whatever state the
            // FrameInputLog/ReplayPlayer/etc. hold internally.
            //
            // SYMPTOM (user 2026-04-26):
            //   "freezing the game, the replay seems to keep playing in
            //    the background and when I unpause it just plays the
            //    inputs that wouldve played if I never paused"
            //
            // FIX: bare-RET on speedval==0 at function entry.  Same shape
            // as sites 9/10/11/12/13/19/20.  When frozen:
            //   - state machine doesn't advance
            //   - SimulationLoop doesn't run (already gated, defense in depth)
            //   - online-sync vtable[0x138] doesn't dispatch
            //   - AActor_TickActor never fires → component ticks halt
            //   - FrameInputLog's internal tick (the suspected leak path)
            //     stops running
            //
            // FRAME-STEP COMPATIBILITY: identical to sites 9-13.  When
            // speedval=1 for a single frame, the gate opens, the entire
            // BM Actor::Tick chain runs once, and back to frozen.
            //
            // Risks during freeze:
            //   - Online disconnect detection paused (irrelevant for
            //     replay viewing — single-player offline context).
            //   - Round-over transitions (state 2 -> 3) paused.  Only
            //     matters if the user freezes EXACTLY on the final blow,
            //     edge case.
            //
            // Prologue (verified via mcp__ghidra-mcp__read_memory):
            //   40 53           push rbx       (REX prefix + opcode = 2B)
            //   48 83 EC 30     sub rsp, 0x30  (4B)
            //   48 8B D9        mov rbx, rcx   (3B — into trampoline)
            //   0F 29 74 24 20  movaps [rsp+0x20], xmm6  (5B — into trampoline)
            //   0F B6 89 61 14 00 00  movzx ecx, [rcx+0x1461]  (state byte read)
            //
            // The +0x1461 state-byte read is unique to this function in
            // the entire binary (no other code reads BM+0x1461 with this
            // movzx pattern), so the AOB is unambiguous.
            //
            // orig_len = 6 (prologue PUSH RBX with REX + SUB RSP,0x30).
            // The bare-RET hook fires BEFORE PUSH RBX so RSP is unmodified —
            // safe to RET directly without stack adjustment.
            void* site21 = sig_scan_sc6(
                "40 53 48 83 EC 30 48 8B D9 0F 29 74 24 20 0F B6 89 61 14 00 00",
                "SpeedControl site21 (LuxBattleManager_Tick_MainStateMachine_At1461 — BM Actor::Tick freeze, fixes background-replay leak)");

            // ---- Site 22 — ALuxBattleChara::TickActor entry-RET ----
            //
            // THE CHARA UE4 ACTOR::TICK ENTRY POINT @ 0x1403D0590.
            // Per the architecture plate on LuxBattle_PerFrameTick, the
            // chara tick pipeline is:
            //
            //   UE4 World::Tick
            //     -> ALuxBattleChara::TickActor (0x1403D0590)  <-- THIS SITE
            //        -> LuxBattle_PerFrameTick (Site 9)        gated
            //        -> LuxBattleChara_Tick_AdvanceReplayFrame_OrLocal (Site 11)
            //        -> LuxReplayChara_Tick_CopyNextFrameToManager_SetMoveState4 (Site 10)
            //        -> LuxBattleChara_Tick_CheckFlagsAndNotifyMoveEnded_State2 (Site 12)
            //        -> ~20 other LuxBattleChara_Tick_* handlers  UN-GATED
            //        -> ALuxCharaActor_TickActor                  UN-GATED
            //        -> FUN_141c2a1d0 (SetActorTransform)         UN-GATED
            //        -> ALuxBattleChara_TickMaegamiHairStateMachine UN-GATED
            //        -> ALuxBattleChara_SyncMoveStateVisibility   UN-GATED
            //
            // Sites 9/10/11/12 only catch a FRACTION of TickActor's
            // children.  The "~20 other handlers" comment in the plate
            // is conservative — TickActor's body has many sub-system
            // calls (weapon updates, hair state machine, anim
            // synchronization, replay snapshot copy, etc.).  Any one
            // of them could mutate input-related state during freeze.
            //
            // SYMPTOM (user 2026-04-27):
            //   "the inputs of both characters doesnt match whats supposed
            //    to happen in the replay" — timer fine, position fine,
            //    but inputs played after a freeze cycle differ from
            //    what they'd be without freeze.  Most plausible cause:
            //    one of the un-gated TickActor children is mutating
            //    chara input-cache state.
            //
            // FIX: bare-RET on speedval==0 at function entry.  This is
            // the NUCLEAR option — gates the ENTIRE chara tick chain in
            // one shot.  Composes with Site 21 (BM Actor::Tick) for
            // total coverage of both pipelines 1 and 2.
            //
            // Risks during freeze:
            //   - Hair / weapon-mesh anim updates frozen (visual; correct
            //     for a freeze).
            //   - SetActorTransform skipped (chara doesn't move; correct).
            //   - All ~20 LuxBattleChara_Tick_* handlers stop (sim halt).
            //
            // Composes correctly with frame-step: when speedval=1 for one
            // frame, the ENTIRE chain runs once.  Same as sites 9/13/21.
            //
            // Prologue (verified via mcp__ghidra-mcp__read_memory):
            //   40 55           push rbp        (REX + opcode = 2B)
            //   53              push rbx        (1B)
            //   56              push rsi        (1B)
            //   41 54           push r12        (2B)
            //   41 55           push r13        (2B)
            //   48 8D AC 24 B0 FE FF FF   lea rbp, [rsp-0x150]  (8B)
            //   48 81 EC 50 02 00 00      sub rsp, 0x250        (7B)
            //
            // The first 6 bytes (push rbp + push rbx + push rsi + push r12)
            // displace cleanly — boundary lands AFTER push r12 and BEFORE
            // push r13, so no instruction is split mid-encoding.
            //
            // orig_len = 6.  Bare-RET at entry is safe because RSP is
            // unmodified at the hook point (no pushes have run yet) and
            // the function returns void.
            void* site22 = sig_scan_sc6(
                "40 55 53 56 41 54 41 55 48 8D AC 24 B0 FE FF FF 48 81 EC 50 02 00 00",
                "SpeedControl site22 (ALuxBattleChara::TickActor — chara Actor::Tick freeze, gates ALL ~20 sub-handlers at the root)");

            // ---- Site 18 — REMOVED 2026-04-26 (both v1 and v2 didn't work) ----
            //
            // v1 (entry-RET on speedval==0 of LuxBattle_InteractiveReplay_-
            // UpdateCamera): didn't fix the user-reported "duplicated
            // inputs in replay HUD" symptom.
            //
            // v2 (unconditional NOP of CALL LuxBattle_TickCharaInput inside
            // UpdateCamera): also didn't fix it, and risks causing harm
            // during normal replay watching by removing a real call.
            //
            // ROLLED BACK to baseline (sites 1..16 only).  The duplication
            // is in a DIFFERENT path that none of the existing 16 sites
            // catch — possibly in SC6's own VM freeze mechanism (see
            // g_LuxBattle_VMFreezeRecord + BattleAdvanceFlag in
            // LuxBattle_PerFrameTick).  Investigation pending.
            void* site18 = reinterpret_cast<void*>(0x1);  // sentinel — unused

            // ---- Site 17 — DISABLED 2026-04-26 (wrong target) ----
            //
            // Site 17 hooked ALuxBattleKeyRecorder::TickActor @ 0x1404353D0
            // on the theory that it was the "training/replay recorder
            // state machine" leaking during freeze.
            //
            // NEW EVIDENCE (re-decompile of the function body) shows it
            // is the TRAINING-MODE key-recorder, not a replay-watching
            // component.  Definitive strings inside the function:
            //   - L"trainingMode.recordType.slotNo"
            //   - L"TrainingSettingData"
            // And the class registration uses the enum
            //   ELuxBattleKeyRecordState { Inactive, Active,
            //     ReadyToRecord, Record, ReadyToPlayback, Playback }
            // — that's the training-mode "record P1's actions and replay
            // as P2 dummy" feature, NOT the "watch a saved match
            // replay" pipeline.
            //
            // USER-OBSERVED REGRESSION:
            // After site 17, the user enabled the in-replay input
            // display and saw inputs that DIFFER between freeze-on and
            // freeze-off cycles.  Site 17 was either (a) breaking the
            // recorder's idle housekeeping during replay watching (the
            // recorder may still tick passively even when not recording/
            // playing-back) or (b) introducing a transparency bug we
            // can't yet rule out.  Either way it's the wrong target.
            //
            // ROLLED BACK to remove the regression.  Sites 13-16 still
            // catch most of the original "freeze means freeze" cases.
            // Remaining "5 second pause + unpause = small drift" symptom
            // needs to be addressed by a DIFFERENT target — likely a
            // path inside the actual replay-state-restore code, not the
            // training-mode recorder.
            //
            // (Build/test machinery is preserved in git history — the
            // freeze-entry-hook helper still supports orig_len up to 24
            // for any future entry-hook with a long prologue.)
            if (!site1 || !site3 || !site4 || !site5 || !site6 || !site7
                || !site8 || !site9 || !site10 || !site11 || !site12
                || !site13 || !site14 || !site15 || !site16 || !site19
                || !site20 || !site21 || !site22)
                return false;

            // ---- Allocate the shared speedval + 4 trampolines ----
            // All four trampolines share one cave-resident float slot
            // (RIP-relative reachable from each trampoline since they
            // all live in the same near-module page).  Default to 1.0
            // (= no-op) so enabling without first writing a slider value
            // doesn't suddenly slow the game to a crawl.
            m_speedval = static_cast<float*>(
                CodeCave::allocate(sizeof(float), alignof(float)));
            if (!m_speedval) return false;
            *m_speedval = 1.0f;
            // Publish to the static accessor so other helpers (e.g.
            // KHitWalker's hit-flash sticky countdown) can query the
            // live speedval without a direct dependency on this class.
            // Using a plain raw pointer is safe because the cave page
            // never gets freed (see CodeCave.hpp design note) and only
            // one SpeedControl instance ever exists in HorseMod.
            s_speedval_global = m_speedval;

            // Trampoline sizes:
            //   xmm14 load (REX.R + opcode) = 9 bytes for the movss + 5 jmp = 14
            //   xmm0  load                  = 8 bytes for the movss + 5 jmp = 13
            // Cave allocation is done individually so each tramp lives at
            // its own address — that lets us hand each tramp's address
            // to its corresponding site patch independently.

            void* tramp1 = CodeCave::allocate(14);
            void* tramp3 = CodeCave::allocate(13);
            void* tramp4 = CodeCave::allocate(13);
            void* tramp5 = CodeCave::allocate(13);
            // Site 7 entry-hook trampoline: cmp/je/early-ret prelude (12B) +
            // 8 bytes of replicated original prologue + 5-byte JMP back = 25B.
            void* tramp7 = CodeCave::allocate(25);
            // Site 8 mulss-insert trampoline: 8B mulss + 3B movaps + 5B addss
            // + 5B jmp = 21B.
            void* tramp8 = CodeCave::allocate(21);
            // Site 9 entry-hook trampoline: 7B cmp + 2B jne + 1B ret + 7B
            // replicated prologue (mov r11,rsp + mov [r11+0x10],rbx) + 5B
            // jmp = 22B.
            void* tramp9 = CodeCave::allocate(22);
            // Sites 10..13 use the freeze_entry_hook_size() helper.
            //   site10: orig_len = 5  -> 20B trampoline
            //   site11: orig_len = 6  -> 21B trampoline
            //   site12: orig_len = 5  -> 20B trampoline
            //   site13: orig_len = 11 -> 26B trampoline
            void* tramp10 = CodeCave::allocate(freeze_entry_hook_size(5));
            void* tramp11 = CodeCave::allocate(freeze_entry_hook_size(6));
            void* tramp12 = CodeCave::allocate(freeze_entry_hook_size(5));
            void* tramp13 = CodeCave::allocate(freeze_entry_hook_size(11));
            // Site 18 — REMOVED, see resolve()'s site18 section for rationale.
            void* tramp18 = reinterpret_cast<void*>(0x1);  // sentinel — unused

            // Site 19 — entry-RET freeze hook on the match-replay input
            // pusher.  orig_len = 6 (push rbx WITH REX prefix = 40 53,
            // then sub rsp,0x50 = 48 83 EC 50).  Standard
            // freeze_entry_hook_size(6) = 21-byte trampoline.
            void* tramp19 = CodeCave::allocate(freeze_entry_hook_size(6));

            // Site 20 — entry-RET freeze hook on the replay cache filler.
            // orig_len = 5 (push rdi + sub rsp,0x60 = 57 48 83 EC 60).
            // Standard freeze_entry_hook_size(5) = 20-byte trampoline.
            void* tramp20 = CodeCave::allocate(freeze_entry_hook_size(5));

            // Site 21 — entry-RET freeze hook on the BM Actor::Tick wrapper.
            // orig_len = 6 (push rbx WITH REX prefix = 40 53, then
            // sub rsp,0x30 = 48 83 EC 30).  Standard
            // freeze_entry_hook_size(6) = 21-byte trampoline.  Fixes the
            // "replay keeps playing in background during freeze" symptom
            // by stopping the AActor_TickActor tail-call's component
            // dispatch.
            void* tramp21 = CodeCave::allocate(freeze_entry_hook_size(6));

            // Site 22 — entry-RET freeze hook on the chara Actor::Tick
            // root.  orig_len = 6 (push rbp+rbx+rsi+r12 = 40 55 53 56
            // 41 54).  Standard freeze_entry_hook_size(6) = 21-byte
            // trampoline.  This is the NUCLEAR chara-side freeze: gates
            // the entire UE4-driven chara tick chain in one shot,
            // including ~20 sub-handlers that sites 9-12 don't catch.
            void* tramp22 = CodeCave::allocate(freeze_entry_hook_size(6));
            // Sites 14/15 — conditional-INC trampolines (mid-function).
            // Layout (20B each):
            //   [0x00] 83 3D <disp32> 00     cmp dword [rip+disp_speedval], 0
            //   [0x07] 74 06                 je skip_inc                (+6)
            //   [0x09] FF 83 <off32>          inc dword [rbx+offX]       (replicated)
            //   [0x0F] E9 <rel32>             jmp back to (site + 6)
            //
            // Site 16 — DIFFERENT shape (25B): the patched INC sits inside
            // a WHILE loop that re-checks vtable[0x6B0] using the
            // (un-INC'd) chara+0x4410 value.  If we just skip the INC,
            // vtable[0x6B0] will keep returning true on the same +0x4410
            // → INFINITE LOOP.  So when speedval==0 we must instead jump
            // OUT of the loop entirely, to the function epilogue at
            // site16_fn+0x5F (= ADD RSP, 0x30; POP RBX; RET).
            //
            // Layout (25B):
            //   [0x00] 83 3D <disp32> 00     cmp dword [rip+disp_speedval], 0
            //   [0x07] 75 05                 jne do_inc                 (+5)
            //   [0x09] E9 <rel32>             jmp frozen_target  (= site16_fn + 0x5F)
            //   [0x0E] FF 83 10 44 00 00      inc dword [rbx+0x4410]  (replicated)
            //   [0x14] E9 <rel32>             jmp back to (site + 6)
            void* tramp14 = CodeCave::allocate(20);
            void* tramp15 = CodeCave::allocate(20);
            void* tramp16 = CodeCave::allocate(25);
            // (tramp17 disabled — see "Site 17 DISABLED" comment above)
            if (!tramp1 || !tramp3 || !tramp4 || !tramp5 || !tramp7
                || !tramp8 || !tramp9 || !tramp10 || !tramp11 || !tramp12
                || !tramp13 || !tramp14 || !tramp15 || !tramp16 || !tramp19
                || !tramp20 || !tramp21 || !tramp22)
                return false;

            // ---- Build trampolines ----
            // Site 1: movss xmm14, [rip+disp32] -> jmp back (9 bytes orig)
            if (!build_movss_xmm_load(
                    tramp1, /*xmm14=*/14, m_speedval,
                    static_cast<uint8_t*>(site1) + 9))   return false;
            // Site 3: movss xmm0, [rip+disp32] -> jmp back (8 bytes orig)
            if (!build_movss_xmm_load(
                    tramp3, /*xmm0=*/0,  m_speedval,
                    static_cast<uint8_t*>(site3) + 8))   return false;
            // Site 4: same shape — orig is movss xmm0,[rcx+0x2080] (8B)
            if (!build_movss_xmm_load(
                    tramp4, /*xmm0=*/0,  m_speedval,
                    static_cast<uint8_t*>(site4) + 8))   return false;
            // Site 5: orig is movss [rsi+0x2080],xmm0 (8B store) — we
            // replace the store with a load (CE's documented choice).
            if (!build_movss_xmm_load(
                    tramp5, /*xmm0=*/0,  m_speedval,
                    static_cast<uint8_t*>(site5) + 8))   return false;

            // ---- Prepare site patches ----
            // Each site patch = 5-byte jmp to its trampoline + NOPs to
            // fill the original instruction length.  Total patch size
            // matches the original instruction so the next instruction
            // boundary stays intact.
            if (!prepare_jmp_patch(m_patch1, site1, tramp1, /*orig_len=*/9)) return false;
            if (!prepare_jmp_patch(m_patch3, site3, tramp3, /*orig_len=*/8)) return false;
            if (!prepare_jmp_patch(m_patch4, site4, tramp4, /*orig_len=*/8)) return false;
            if (!prepare_jmp_patch(m_patch5, site5, tramp5, /*orig_len=*/8)) return false;

            // Site 6 is a single-byte ModRM swap: CC (xmm4) -> CB (xmm3).
            // It lives at site6 + 3 (the 4th byte of the cvttss2si).
            const uint8_t kModRMxmm3 = 0xCB;
            auto* patch6_addr = static_cast<uint8_t*>(site6) + 3;
            if (!m_patch6.prepare(patch6_addr, &kModRMxmm3, 1)) return false;

            // ---- Site 7: PostATKDelayGate entry-hook ----
            // Build the trampoline body (25 bytes, layout below) into a
            // local buffer and copy in one shot.  Layout — offsets are
            // relative to the trampoline start:
            //
            //   [0x00] 83 3D <disp32> 00     cmp dword [rip+disp], 0
            //   [0x07] 75 03                 jne +3  -> 0x0C
            //   [0x09] 33 C0                 xor eax, eax
            //   [0x0B] C3                    ret           ; return false
            //   [0x0C] 48 83 EC 28           sub rsp, 0x28 ; replicate
            //   [0x10] 48 8B 51 08           mov rdx,[rcx+8]; replicate
            //   [0x14] E9 <rel32>            jmp back to site7+8 = MOV R11,RCX
            //
            // Why it's safe to RET without unwinding: we hook BEFORE the
            // function's `sub rsp, 0x28`, so the stack pointer is still
            // exactly as the caller left it.  A bare `ret` returns
            // straight back to the caller with rax=0 (gate not passed).
            //
            // Why early-return false (not true) when frozen: the caller
            // (TickDriver) interprets a 0 return as "still waiting, keep
            // chara in phase 2".  A 1 return would advance the chara out
            // of phase 2 and into the main execute path — i.e. the move
            // would *resume* during freeze, the opposite of what we want.
            // Returning 0 on freeze means chara stays parked in phase 2
            // and resumes its countdown when the freeze is lifted.
            //
            // Fractional speedval (slow-mo, e.g. 0.5): currently treated
            // as "not frozen" — gate ticks at full rate.  Since the
            // post-ATK delay only spans 1..5 frames the perceived error
            // is small.  If we wanted true slow-mo behaviour here we'd
            // need an integer accumulator inside the trampoline, which
            // is more cave bytes and more state for marginal benefit.
            {
                uint8_t buf7[25] = {0};
                size_t off = 0;

                // [0x00..0x06] cmp dword [rip+disp32], 0
                buf7[off++] = 0x83;  // GROUP1 imm8 with /7 (cmp)
                buf7[off++] = 0x3D;  // ModRM: mod=00, reg=7, r/m=101 (RIP-rel)
                {
                    // disp32 from end-of-instruction (off+4) to speedval slot
                    const int64_t disp =
                          reinterpret_cast<int64_t>(m_speedval)
                        - (reinterpret_cast<int64_t>(tramp7) + off + 4 + 1);
                    if (disp < INT32_MIN || disp > INT32_MAX) return false;
                    const int32_t d32 = static_cast<int32_t>(disp);
                    std::memcpy(&buf7[off], &d32, sizeof(d32));
                    off += 4;
                }
                buf7[off++] = 0x00;  // imm8 = 0 (compare-against-zero)

                // [0x07..0x08] jne +3 (skip the xor/ret early-exit)
                buf7[off++] = 0x75;
                buf7[off++] = 0x03;

                // [0x09..0x0A] xor eax, eax
                buf7[off++] = 0x33;
                buf7[off++] = 0xC0;

                // [0x0B] ret  (return 0)
                buf7[off++] = 0xC3;

                // [0x0C..0x0F] sub rsp, 0x28  (original byte 0..3 of site7)
                buf7[off++] = 0x48;
                buf7[off++] = 0x83;
                buf7[off++] = 0xEC;
                buf7[off++] = 0x28;

                // [0x10..0x13] mov rdx, [rcx+8]  (original byte 4..7 of site7)
                buf7[off++] = 0x48;
                buf7[off++] = 0x8B;
                buf7[off++] = 0x51;
                buf7[off++] = 0x08;

                // [0x14..0x18] jmp rel32 -> site7 + 8 (continue at MOV R11,RCX)
                {
                    uint8_t jmp_back[5];
                    void* jmp_at      = static_cast<uint8_t*>(tramp7) + off;
                    void* back_target = static_cast<uint8_t*>(site7)  + 8;
                    if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                        return false;
                    std::memcpy(&buf7[off], jmp_back, sizeof(jmp_back));
                    off += 5;
                }

                std::memcpy(tramp7, buf7, off);
                ::FlushInstructionCache(::GetCurrentProcess(), tramp7, off);
            }

            // Site 7 patch: 5-byte JMP rel32 to tramp7 + 3 NOPs.  Total 8
            // bytes, exactly matching the displaced two original prologue
            // instructions (SUB RSP, 0x28 + MOV RDX, [RCX+8]) so the next
            // instruction boundary at site7+8 (MOV R11, RCX) is preserved.
            if (!prepare_jmp_patch(m_patch7, site7, tramp7, /*orig_len=*/8))
                return false;

            // ---- Site 8: AdvancePlayback dt-scaling MULSS insert ----
            // Trampoline layout (21 bytes):
            //   [0x00] F3 0F 59 0D <disp32>   mulss xmm1, [rip+disp32_speedval]
            //   [0x08] 0F 28 C1               movaps xmm0, xmm1   (orig)
            //   [0x0B] F3 0F 58 41 28         addss  xmm0,[rcx+0x28] (orig)
            //   [0x10] E9 <rel32>             jmp back to site8 + 8
            //
            // Why MULSS xmm1 (not xmm0): by the time we hit site8 the engine
            // has already chosen its dt source for xmm1 (per-action +0x80,
            // or g_OneFloat / g_VMFreezeRecord blend in the negative-speed
            // path) — see the disasm flow at 14032c780..14032c7ab.  Any
            // multiply on xmm0 would happen AFTER the addss to currentFrame
            // and corrupt the integration.  Multiplying the dt input
            // (xmm1) before the addss gives proper scaled-frame-advance
            // semantics:
            //
            //   speedval=0     → xmm1*0 = 0  → frame doesn't advance
            //   speedval=1     → xmm1 unchanged → normal speed
            //   speedval=0.5   → frame advances at half speed
            //   speedval=2     → frame advances at 2x speed
            //
            // This composes multiplicatively with the engine's own
            // freeze-blend at +0x88==0 path (which sets xmm1 from
            // g_LuxBattle_VMFreezeRecord.flOutBlendW1) — so a Critical-Edge
            // engine-freeze AND a HorseMod user-freeze both have to be
            // active for the camera to halt; either alone halts it.  That
            // matches the user's mental model: "freeze means freeze".
            {
                uint8_t buf8[21] = {0};
                size_t off = 0;

                // [0x00..0x07] mulss xmm1, [rip+disp32]
                buf8[off++] = 0xF3;
                buf8[off++] = 0x0F;
                buf8[off++] = 0x59;
                buf8[off++] = 0x0D;          // ModRM mod=00 reg=001 r/m=101
                {
                    // disp32 from end-of-instruction (off+4) to speedval
                    const int64_t disp =
                          reinterpret_cast<int64_t>(m_speedval)
                        - (reinterpret_cast<int64_t>(tramp8) + off + 4);
                    if (disp < INT32_MIN || disp > INT32_MAX) return false;
                    const int32_t d32 = static_cast<int32_t>(disp);
                    std::memcpy(&buf8[off], &d32, sizeof(d32));
                    off += 4;
                }

                // [0x08..0x0A] movaps xmm0, xmm1   (replicate)
                buf8[off++] = 0x0F;
                buf8[off++] = 0x28;
                buf8[off++] = 0xC1;

                // [0x0B..0x0F] addss xmm0, [rcx+0x28]   (replicate)
                buf8[off++] = 0xF3;
                buf8[off++] = 0x0F;
                buf8[off++] = 0x58;
                buf8[off++] = 0x41;
                buf8[off++] = 0x28;

                // [0x10..0x14] jmp rel32 -> site8 + 8 (CVTDQ2PS continuation)
                {
                    uint8_t jmp_back[5];
                    void* jmp_at      = static_cast<uint8_t*>(tramp8) + off;
                    void* back_target = static_cast<uint8_t*>(site8)  + 8;
                    if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                        return false;
                    std::memcpy(&buf8[off], jmp_back, sizeof(jmp_back));
                    off += 5;
                }

                std::memcpy(tramp8, buf8, off);
                ::FlushInstructionCache(::GetCurrentProcess(), tramp8, off);
            }

            // Site 8 patch: JMP rel32 + 3 NOPs over the displaced 8 bytes
            // (movaps xmm0,xmm1 + addss xmm0,[rcx+0x28]).
            if (!prepare_jmp_patch(m_patch8, site8, tramp8, /*orig_len=*/8))
                return false;

            // ---- Site 9: LuxBattle_PerFrameTick blanket-freeze entry-hook ----
            // Trampoline layout (22 bytes):
            //   [0x00] 83 3D <disp32> 00     cmp dword [rip+disp32_speedval], 0
            //   [0x07] 75 01                 jne +1   -> [0x0A] (skip the ret)
            //   [0x09] C3                    ret      ; speedval==0: bail
            //   [0x0A] 4C 8B DC               mov r11, rsp        (replicate)
            //   [0x0D] 49 89 5B 10            mov [r11+0x10], rbx (replicate)
            //   [0x11] E9 <rel32>             jmp back to site9 + 7
            //
            // Why bare RET (no stack adjustment): the hook fires BEFORE the
            // function's first prologue instruction, so RSP is still
            // whatever the caller passed in.  The function is `void` and
            // declared in PerFrameTick's calling convention (Microsoft
            // x64) so no return value setup is needed either.  Bare RET
            // is correct.
            //
            // What gets replicated: the FIRST two of the function's
            // prologue instructions:
            //   mov r11, rsp                  (3 bytes, sets up frame-base)
            //   mov [r11+0x10], rbx           (4 bytes, saves callee-saved RBX)
            // The jump-back lands at site9+7 (=PerFrameTick+7), which is
            // `mov [r11+0x18], rbp` and the rest of the original prologue.
            // From there execution proceeds normally.
            //
            // CAUTION on changing this: PerFrameTick's prologue is
            // hand-written by the compiler with specific stack-frame
            // expectations.  If the patch ever falsely allows the function
            // to enter with a partially-set-up frame and then ret early,
            // the stack would be corrupted.  The current "ret-before-any-
            // prologue-runs" placement is the only safe early-exit point.
            {
                uint8_t buf9[22] = {0};
                size_t off = 0;

                // [0x00..0x06] cmp dword [rip+disp32], 0
                buf9[off++] = 0x83;
                buf9[off++] = 0x3D;
                {
                    const int64_t disp =
                          reinterpret_cast<int64_t>(m_speedval)
                        - (reinterpret_cast<int64_t>(tramp9) + off + 4 + 1);
                    if (disp < INT32_MIN || disp > INT32_MAX) return false;
                    const int32_t d32 = static_cast<int32_t>(disp);
                    std::memcpy(&buf9[off], &d32, sizeof(d32));
                    off += 4;
                }
                buf9[off++] = 0x00;          // imm8 (compare against 0)

                // [0x07..0x08] jne +1   (skip the ret if speedval != 0)
                buf9[off++] = 0x75;
                buf9[off++] = 0x01;

                // [0x09] ret  (return early; void func, no value to set)
                buf9[off++] = 0xC3;

                // [0x0A..0x0C] mov r11, rsp   (replicate)
                buf9[off++] = 0x4C;
                buf9[off++] = 0x8B;
                buf9[off++] = 0xDC;

                // [0x0D..0x10] mov [r11+0x10], rbx   (replicate)
                buf9[off++] = 0x49;
                buf9[off++] = 0x89;
                buf9[off++] = 0x5B;
                buf9[off++] = 0x10;

                // [0x11..0x15] jmp rel32 -> site9 + 7
                {
                    uint8_t jmp_back[5];
                    void* jmp_at      = static_cast<uint8_t*>(tramp9) + off;
                    void* back_target = static_cast<uint8_t*>(site9)  + 7;
                    if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                        return false;
                    std::memcpy(&buf9[off], jmp_back, sizeof(jmp_back));
                    off += 5;
                }

                std::memcpy(tramp9, buf9, off);
                ::FlushInstructionCache(::GetCurrentProcess(), tramp9, off);
            }

            // Site 9 patch: JMP rel32 (5 bytes) + 2 NOPs over the displaced
            // 7 bytes (mov r11,rsp + mov [r11+0x10],rbx).
            if (!prepare_jmp_patch(m_patch9, site9, tramp9, /*orig_len=*/7))
                return false;

            // ---- Sites 10/11/12: replay-side actor-tick freeze hooks ----
            // Each uses the prepare_freeze_entry_hook helper with the
            // function's first prologue bytes as the "orig_bytes" to
            // replicate.  Trampoline is the standard cmp/jne/ret + repl +
            // jmp pattern.

            // Site 10: LuxReplayChara_Tick (frame-buffer copy)
            //   Prologue: 48 89 5C 24 10  ;  mov [rsp+0x10], rbx (5B)
            {
                static constexpr uint8_t kOrig10[5] =
                    { 0x48, 0x89, 0x5C, 0x24, 0x10 };
                if (!prepare_freeze_entry_hook(
                        m_patch10, tramp10, m_speedval, site10,
                        kOrig10, sizeof(kOrig10)))
                    return false;
            }

            // Site 11: LuxBattleChara_Tick AdvanceReplayFrame
            //   Prologue: 40 53 48 83 EC 30  ;  push rbx + sub rsp,0x30 (6B)
            //
            // 6 bytes is the smallest patch that overwrites only the
            // PUSH+SUB pair without bisecting the next instruction
            // (CMP dword [rcx+0x4400], 0 at site11+6).
            {
                static constexpr uint8_t kOrig11[6] =
                    { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30 };
                if (!prepare_freeze_entry_hook(
                        m_patch11, tramp11, m_speedval, site11,
                        kOrig11, sizeof(kOrig11)))
                    return false;
            }

            // Site 12: LuxBattleChara_Tick CheckFlagsAndNotifyMoveEnded
            //   Prologue: 48 89 6C 24 10  ;  mov [rsp+0x10], rbp (5B)
            {
                static constexpr uint8_t kOrig12[5] =
                    { 0x48, 0x89, 0x6C, 0x24, 0x10 };
                if (!prepare_freeze_entry_hook(
                        m_patch12, tramp12, m_speedval, site12,
                        kOrig12, sizeof(kOrig12)))
                    return false;
            }

            // Site 13: BattleManager Tick SimulationLoop (the catch-up loop)
            //   Prologue (11B):
            //     4C 8B DC                 mov r11, rsp
            //     53                       push rbx
            //     48 81 EC E0 00 00 00     sub rsp, 0xE0
            //
            // 11 bytes is the largest orig_len our freeze-entry-hook
            // helper currently supports (buffer is sized [10+11+5]=26B).
            // Bare-RET in the freeze branch is safe because we install
            // BEFORE the PUSH RBX and SUB RSP — RSP is exactly as the
            // caller (LuxBattleManager_Tick_MainStateMachine) left it.
            {
                static constexpr uint8_t kOrig13[11] = {
                    0x4C, 0x8B, 0xDC,                   // mov r11, rsp
                    0x53,                                // push rbx
                    0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00  // sub rsp, 0xE0
                };
                if (!prepare_freeze_entry_hook(
                        m_patch13, tramp13, m_speedval, site13,
                        kOrig13, sizeof(kOrig13)))
                    return false;
            }

            // ---- Sites 14 & 15 & 16: conditional-INC patches ----
            // All three sites have IDENTICAL trampoline shape (just
            // different addresses + different cave slots + different
            // disp32 inside the INC).  Build with a small helper lambda.
            //
            // Encoding rationale per byte:
            //   0x83 0x3D <disp32> 0x00 : `cmp dword [rip+disp32], 0`
            //                             7-byte form for cmp-with-imm8.
            //   0x74 0x06               : `je rel8` skipping the next 6
            //                             bytes (the INC itself).
            //   0xFF 0x83 <off32>       : `inc dword [rbx+off32]`
            //                             — verbatim copy of the original
            //                             instruction we displaced.
            //                             off32 differs per site:
            //                               site14/15: 0x3A4 (replay clock)
            //                               site16:    0x4410 (2nd-tier cursor)
            //   0xE9 <rel32>            : 5-byte jmp back to (site + 6).
            auto build_inc_trampoline = [&](void* tramp, void* site,
                                            uint32_t cursor_off) -> bool
            {
                if (!tramp || !site) return false;
                uint8_t buf[20] = {0};
                size_t off = 0;

                // [0x00..0x06] cmp dword [rip+disp32], 0
                buf[off++] = 0x83;
                buf[off++] = 0x3D;
                {
                    // disp32 from end-of-instruction (off+4+1=7) to speedval
                    const int64_t disp =
                          reinterpret_cast<int64_t>(m_speedval)
                        - (reinterpret_cast<int64_t>(tramp) + off + 4 + 1);
                    if (disp < INT32_MIN || disp > INT32_MAX) return false;
                    const int32_t d32 = static_cast<int32_t>(disp);
                    std::memcpy(&buf[off], &d32, sizeof(d32));
                    off += 4;
                }
                buf[off++] = 0x00;                       // imm8 = 0

                // [0x07..0x08] je +6 (skip INC when speedval == 0)
                buf[off++] = 0x74;
                buf[off++] = 0x06;

                // [0x09..0x0E] inc dword [rbx+cursor_off]   (replicate)
                buf[off++] = 0xFF;
                buf[off++] = 0x83;
                std::memcpy(&buf[off], &cursor_off, sizeof(cursor_off));
                off += 4;

                // [0x0F..0x13] jmp rel32 -> site + 6 (continue at epilogue)
                {
                    uint8_t jmp_back[5];
                    void* jmp_at      = static_cast<uint8_t*>(tramp) + off;
                    void* back_target = static_cast<uint8_t*>(site)  + 6;
                    if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                        return false;
                    std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
                    off += 5;
                }

                std::memcpy(tramp, buf, off);
                ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);
                return true;
            };

            if (!build_inc_trampoline(tramp14, site14, 0x3A4))  return false;
            if (!build_inc_trampoline(tramp15, site15, 0x3A4))  return false;

            // ---- Site 16 — special "loop-break" trampoline ----
            // Unlike sites 14/15 (which sit inside straight-line code),
            // the INC at site 16 lives INSIDE a WHILE loop:
            //
            //   call vtable[0x6B0](this, +0x39C, +0x3A0, +0x4410)
            //   if !result: exit
            //   loop_body:
            //     INC +0x4410        ← patch site
            //     call vtable[0x6B0](this, +0x39C, +0x3A0, NEW +0x4410)
            //     if result: jmp loop_body
            //   exit  ← function_start + 0x5F
            //
            // If we merely skip the INC, the next iteration calls vtable
            // [0x6B0] with the SAME +0x4410 value that JUST returned true,
            // so it returns true again → infinite loop, game hangs.
            //
            // Correct freeze behaviour: when speedval==0 we need to BREAK
            // OUT of the loop entirely (jump to the function's epilogue
            // at function_start + 0x5F = site16_fn + 0x5F).
            {
                if (!site16_fn) return false;
                void* loop_break_target = static_cast<uint8_t*>(site16_fn) + 0x5F;

                uint8_t buf[25] = {0};
                size_t off = 0;

                // [0x00..0x06] cmp dword [rip+disp32], 0
                buf[off++] = 0x83;
                buf[off++] = 0x3D;
                {
                    const int64_t disp =
                          reinterpret_cast<int64_t>(m_speedval)
                        - (reinterpret_cast<int64_t>(tramp16) + off + 4 + 1);
                    if (disp < INT32_MIN || disp > INT32_MAX) return false;
                    const int32_t d32 = static_cast<int32_t>(disp);
                    std::memcpy(&buf[off], &d32, sizeof(d32));
                    off += 4;
                }
                buf[off++] = 0x00;

                // [0x07..0x08] jne do_inc (skip the loop-break jmp)
                buf[off++] = 0x75;
                buf[off++] = 0x05;          // skip 5 bytes (the jmp below)

                // [0x09..0x0D] jmp loop_break_target  (frozen exit)
                {
                    uint8_t jmp_break[5];
                    void* jmp_at = static_cast<uint8_t*>(tramp16) + off;
                    if (!encode_jmp_rel32(jmp_at, loop_break_target, jmp_break))
                        return false;
                    std::memcpy(&buf[off], jmp_break, sizeof(jmp_break));
                    off += 5;
                }

                // [0x0E..0x13] inc dword [rbx+0x4410]   (replicate)
                buf[off++] = 0xFF;
                buf[off++] = 0x83;
                {
                    const uint32_t inc_off = 0x4410;
                    std::memcpy(&buf[off], &inc_off, sizeof(inc_off));
                    off += 4;
                }

                // [0x14..0x18] jmp rel32 -> site16 + 6 (rejoin loop)
                {
                    uint8_t jmp_back[5];
                    void* jmp_at = static_cast<uint8_t*>(tramp16) + off;
                    void* back_target = static_cast<uint8_t*>(site16) + 6;
                    if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                        return false;
                    std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
                    off += 5;
                }

                std::memcpy(tramp16, buf, off);
                ::FlushInstructionCache(::GetCurrentProcess(), tramp16, off);
            }

            // Site 14/15/16 patches: 5-byte JMP rel32 + 1 NOP, replacing
            // the 6-byte INC instruction in-place.  No prologue or
            // epilogue bytes are touched, so the function's stack-frame
            // contract and SEH unwind metadata remain valid.
            if (!prepare_jmp_patch(m_patch14, site14, tramp14, /*orig_len=*/6))
                return false;
            if (!prepare_jmp_patch(m_patch15, site15, tramp15, /*orig_len=*/6))
                return false;
            if (!prepare_jmp_patch(m_patch16, site16, tramp16, /*orig_len=*/6))
                return false;

            // (Site 17 KeyRecorder hook disabled — see comment block above
            // with the "DISABLED 2026-04-26 (wrong target)" rationale.)

            // (Site 18 removed — see comment in resolve()'s site18 block.)

            // ---- Site 19 — match-replay input pusher entry-RET ----
            // Standard prepare_freeze_entry_hook with orig_len = 6
            // (push rbx WITH REX prefix = 40 53, then sub rsp,0x50 =
            // 48 83 EC 50).  Bare-RET at entry is safe because RSP
            // is unmodified at the hook point and the function returns
            // void.
            {
                static constexpr uint8_t kOrig19[6] =
                    { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x50 };
                if (!prepare_freeze_entry_hook(
                        m_patch19, tramp19, m_speedval, site19,
                        kOrig19, sizeof(kOrig19)))
                    return false;
            }

            // ---- Site 20 — replay CACHE FILLER entry-RET ----
            // Standard prepare_freeze_entry_hook with orig_len = 5
            // (push rdi + sub rsp,0x60 = 57 48 83 EC 60).  Bare-RET
            // at entry is safe because RSP is unmodified at the hook
            // point and the function returns void.
            //
            // Blocking the cache filler means the chara ring at
            // chara+0x3C0..+0x43C0 stops accumulating decoded inputs
            // during freeze.  PushInputs (site 19) then has no
            // catch-up to do on step → 1 step = 1 input.
            {
                static constexpr uint8_t kOrig20[5] =
                    { 0x57, 0x48, 0x83, 0xEC, 0x60 };
                if (!prepare_freeze_entry_hook(
                        m_patch20, tramp20, m_speedval, site20,
                        kOrig20, sizeof(kOrig20)))
                    return false;
            }

            // ---- Site 21 — BM Actor::Tick wrapper entry-RET ----
            // Standard prepare_freeze_entry_hook with orig_len = 6
            // (push rbx WITH REX prefix = 40 53, then sub rsp,0x30 =
            // 48 83 EC 30).  Bare-RET at entry is safe because RSP
            // is unmodified at the hook point and the function returns
            // void (the tail-call to AActor_TickActor doesn't modify
            // the caller's stack contract).
            //
            // Blocking this stops the AActor_TickActor tail-call's
            // component-tick fan-out, which is the suspected leak path
            // for "replay continues to play in background during freeze".
            {
                static constexpr uint8_t kOrig21[6] =
                    { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30 };
                if (!prepare_freeze_entry_hook(
                        m_patch21, tramp21, m_speedval, site21,
                        kOrig21, sizeof(kOrig21)))
                    return false;
            }

            // ---- Site 22 — chara Actor::Tick root entry-RET ----
            // Standard prepare_freeze_entry_hook with orig_len = 6
            // (push rbp+rbx+rsi+r12 = 40 55 53 56 41 54).  Bare-RET at
            // entry is safe because RSP is unmodified at the hook
            // point — the function does many pushes after this but
            // none have happened yet.  Function returns void.
            //
            // Blocking this gates the ENTIRE chara UE4 Actor::Tick
            // chain: PerFrameTick (Site 9), AdvanceReplayFrame_OrLocal
            // (Site 11), CopyNextFrameToManager (Site 10),
            // CheckFlagsAndNotifyMoveEnded (Site 12), ALL ~20 other
            // LuxBattleChara_Tick_* sub-handlers, ALuxCharaActor_TickActor,
            // SetActorTransform, hair state machine, and SyncMoveStateVisibility.
            //
            // Sites 9-12 stay as defense-in-depth.  Site 22 is the
            // root-level catch-all for anything they miss.
            {
                static constexpr uint8_t kOrig22[6] =
                    { 0x40, 0x55, 0x53, 0x56, 0x41, 0x54 };
                if (!prepare_freeze_entry_hook(
                        m_patch22, tramp22, m_speedval, site22,
                        kOrig22, sizeof(kOrig22)))
                    return false;
            }

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.SpeedControl] resolved 18 patch sites (1/3/4/5/6 "
                    "+ 7=PostATKDelayGate "
                    "+ 8=CameraAction_AdvancePlayback "
                    "+ 9=PerFrameTick blanket-freeze "
                    "+ 10/11/12=replay actor-tick freezes "
                    "+ 13=BattleManager SimulationLoop catch-up freeze "
                    "+ 14/15=replay-clock INC freezers (chara+0x3A4) "
                    "+ 16=second-tier cursor INC freezer (chara+0x4410); "
                    "site 17=KeyRecorder TickActor disabled — wrong target; "
                    "+ 19=match-replay input pusher (PushInputsToActiveSlots) "
                    "+ 20=replay cache filler (ConsumeDecodedInputPackets) "
                    "+ 21=BM Actor::Tick wrapper (MainStateMachine_At1461 — "
                    "blocks AActor_TickActor fan-out, fixes background-replay leak) "
                    "+ 22=chara Actor::Tick root (ALuxBattleChara::TickActor — "
                    "nuclear gate over ALL chara-side sub-handlers including "
                    "the ~20 ungated by sites 9-12)) "
                    "+ speedval slot @ 0x{:x}\n"),
                reinterpret_cast<uintptr_t>(m_speedval));
            return true;
        }

        // Apply all patches.  Rolls back partial application on failure
        // (same defensive pattern as CamLock / CharaInvis).
        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.SpeedControl] enable() before resolve() — "
                        "ignoring\n"));
                return false;
            }
            if (m_enabled) return true;
            // 2026-04-27: Sites 19, 20, 21, 22 temporarily REMOVED from
            // patches[] for binary-search debugging.  User reports input
            // desync after freeze cycles persists through all four;
            // removing them tests whether they were causing or fixing
            // the issue.  If desync persists → the leak is elsewhere
            // entirely.  If desync is gone → one of these was the
            // actual cause and was breaking the input pipeline.
            // Patches still RESOLVE in resolve() so we can re-enable
            // them quickly by editing this list.
            BytePatch* patches[] = { &m_patch1, &m_patch3, &m_patch4,
                                     &m_patch5, &m_patch6, &m_patch7,
                                     &m_patch8, &m_patch9, &m_patch10,
                                     &m_patch11, &m_patch12, &m_patch13,
                                     &m_patch14, &m_patch15, &m_patch16 };
            // (m_patch17 disabled — see comment in resolve())
            // (m_patch18 disabled — both v1 and v2 didn't fix the
            //  "duplicated inputs" symptom; rolled back to baseline)
            // (m_patch19 = match-replay input pusher freeze, stage 3)
            // (m_patch20 = match-replay cache filler freeze, stage 2 —
            //  fixes "many duplicates after step forward" by stopping
            //  ring accumulation during freeze)
            constexpr size_t kPatchCount = sizeof(patches)/sizeof(patches[0]);
            for (size_t i = 0; i < kPatchCount; ++i)
            {
                if (!patches[i]->enable())
                {
                    for (size_t j = 0; j < i; ++j) patches[j]->disable();
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.SpeedControl] enable() failed at "
                            "patch {} — rolled back\n"), i);
                    return false;
                }
            }
            m_enabled = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.SpeedControl] enabled (speedval = {:.4f})\n"),
                *m_speedval);
            return true;
        }

        void disable()
        {
            if (!m_enabled) return;
            // Reverse-order disable, matching the enable() install order.
            // Sites 7..16 first because their trampolines read speedval;
            // safer to revert the read sites before zeroing the slot below.
            // (m_patch17 disabled — see comment in resolve())
            // (m_patch19/20/21/22 temporarily removed from patches[] for
            //  binary-search debugging — see comment in enable())
            m_patch16.disable();
            m_patch15.disable();
            m_patch14.disable();
            m_patch13.disable();
            m_patch12.disable();
            m_patch11.disable();
            m_patch10.disable();
            m_patch9.disable();
            m_patch8.disable();
            m_patch7.disable();
            m_patch6.disable();
            m_patch5.disable();
            m_patch4.disable();
            m_patch3.disable();
            m_patch1.disable();
            m_enabled = false;
            // IMPORTANT: reset the shared speedval slot back to 1.0f
            // BEFORE returning.  The slot is exposed to the rest of
            // HorseMod via current_value_static() / s_speedval_global,
            // and callers (notably KHitWalker's sticky-flash gate)
            // use it as a "is the world advancing?" signal:
            //
            //     world_advancing = current_value_static() > 0.0f;
            //
            // If we leave *m_speedval at whatever the user last set
            // (e.g. 0 for freeze, 0.25 for slow-mo), the static
            // accessor will keep reporting that value even after the
            // patches are reverted — so KHitWalker will believe the
            // world is still paused / slowed and the red hit-flash
            // sticky countdown will never drain.  Concrete symptom:
            // "I pressed freeze once, disabled it, and now every
            // hurtbox stays red forever."  Priming the slot to 1.0f
            // on disable() matches the semantic "no speed override
            // active" for anyone who reads it downstream.
            if (m_speedval) *m_speedval = 1.0f;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.SpeedControl] disabled\n"));
        }

        // Convenience for ImGui callbacks.  Resolves on first ON.
        void set(bool on)
        {
            if (on)
            {
                if (!m_resolved) resolve();
                enable();
            }
            else
            {
                disable();
            }
        }

        // Live-update the speedval (read by every patched site each
        // frame the engine executes them).  Safe to call any time —
        // the storage slot exists from resolve() onward, regardless of
        // whether patches are enabled.  When patches are off, writes
        // are inert (no engine code reads the slot).
        void set_value(float v)  { if (m_speedval) *m_speedval = v; }
        float get_value() const  { return m_speedval ? *m_speedval : 1.0f; }
        float* value_ptr() const { return m_speedval; }

        // ----------------------------------------------------------------
        // Static "current speedval" probe — for callers that need to
        // know the live game-tick rate without taking a direct
        // dependency on this class.  Returns 1.0f if no SpeedControl
        // instance has resolve()'d yet (treated as "engine running at
        // native rate").  Safe to call from any thread; the cave-
        // resident slot lives for the process lifetime and writes/reads
        // are 4-byte atomic on x86_64.
        //
        // Used by KHitWalker to gate the hit-flash sticky countdown:
        // when the world is frozen (speedval = 0), the countdown
        // pauses so the red flash stays visible until the user
        // unfreezes or steps a frame.
        static float current_value_static() noexcept
        {
            float* p = s_speedval_global;
            return p ? *p : 1.0f;
        }

        bool is_enabled()  const { return m_enabled; }
        bool is_resolved() const { return m_resolved_ok; }

    private:
        // Build a `movss xmmN, [rip+disp32_to_speedval]; jmp rel32 -> back_target`
        // trampoline at `tramp`.  Handles the REX.R prefix for xmm8..15.
        // Returns false on RIP-relative or rel32-jmp range overflow
        // (CodeCave guarantees both fit, but we check defensively).
        static bool build_movss_xmm_load(void* tramp,
                                          uint8_t xmm_idx,
                                          const float* speedval,
                                          void* back_target)
        {
            auto* p = static_cast<uint8_t*>(tramp);
            size_t off = 0;

            // Encode `movss xmmN, [rip+disp32]`:
            //   xmm0..7   ->  F3 0F 10 mod00_reg=N_rm=101 disp32       (8B)
            //   xmm8..15  ->  F3 44 0F 10 mod00_reg=(N-8)_rm=101 disp32 (9B)
            // ModRM byte = (mod=00 << 6) | (reg << 3) | (rm=101=5)
            //            = 0x05 | ((reg & 7) << 3)
            //
            // Why the prefix split: REX.R extends the reg field's 4th
            // bit to address xmm8..15.  Same opcode otherwise.
            p[off++] = 0xF3;
            if (xmm_idx >= 8)
            {
                p[off++] = 0x44;          // REX.R
                xmm_idx  = static_cast<uint8_t>(xmm_idx - 8);
            }
            p[off++] = 0x0F;
            p[off++] = 0x10;
            p[off++] = static_cast<uint8_t>(0x05 | ((xmm_idx & 7) << 3));
            // disp32 from end of this instruction to the speedval slot.
            const int64_t disp = reinterpret_cast<int64_t>(speedval)
                               - (reinterpret_cast<int64_t>(p) + off + 4);
            if (disp < INT32_MIN || disp > INT32_MAX) return false;
            const int32_t d32 = static_cast<int32_t>(disp);
            std::memcpy(&p[off], &d32, sizeof(d32));
            off += 4;

            // Encode `jmp rel32` back to `back_target`.
            uint8_t jmp_back[5];
            if (!encode_jmp_rel32(p + off, back_target, jmp_back)) return false;
            std::memcpy(&p[off], jmp_back, sizeof(jmp_back));
            off += 5;

            // Cache flush so the CPU re-fetches what we just wrote.
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);
            return true;
        }

        // Build a 5-byte `jmp rel32` to `tramp` at `site`, padded with
        // NOPs out to `orig_len` so the next instruction boundary is
        // preserved.  Hands the fully-formed replacement to the
        // BytePatch's prepare so it'll snapshot the originals and
        // store the replacement for enable/disable.
        //
        // Upper bound 24 matches prepare_freeze_entry_hook's kMaxOrig
        // (site 17's ALuxBattleKeyRecorder hook needs orig_len=18).
        static bool prepare_jmp_patch(BytePatch& bp, void* site, void* tramp,
                                      size_t orig_len)
        {
            if (orig_len < 5 || orig_len > 24) return false;
            uint8_t buf[24];
            uint8_t jmp[5];
            if (!encode_jmp_rel32(site, tramp, jmp)) return false;
            std::memcpy(buf, jmp, 5);
            for (size_t i = 5; i < orig_len; ++i) buf[i] = 0x90;  // NOP pad
            return bp.prepare(site, buf, orig_len);
        }

        // Build a "freeze entry-hook" trampoline at `tramp` and a matching
        // BytePatch into `bp` for `site`.  Behaviour at run-time:
        //
        //   if (*speedval == 0)            // user freeze active
        //       return immediately;        // (bare RET, void func)
        //   else
        //       run the displaced original prologue bytes
        //       jump back into the function at site + orig_len;
        //
        // Used to gate UE4 Actor::Tick handlers and other functions that
        // run independently of LuxBattle_PerFrameTick (so site 9 doesn't
        // catch them).  Caller hands us:
        //   * tramp     — cave-allocated trampoline buffer of size
        //                 freeze_entry_hook_size(orig_len) bytes.
        //   * bp        — BytePatch slot to populate with site patch.
        //   * site      — function entry to hook.
        //   * orig_bytes / orig_len — displaced bytes to replicate.
        //                 orig_len must be in [5, 11].
        //
        // Trampoline layout (10 + orig_len + 5 = 15 + orig_len bytes):
        //   [0x00] 83 3D <disp32> 00     cmp dword [rip+disp32_speedval], 0
        //   [0x07] 75 01                 jne +1   (skip the ret)
        //   [0x09] C3                    ret      (frozen: bail)
        //   [0x0A] <orig_bytes>           replicate displaced prologue
        //   [0x0A+N] E9 <rel32>           jmp back to site + N
        //
        // Why bare RET is safe: we install BEFORE the first prologue
        // instruction, so RSP is exactly as the caller passed it.  The
        // function's return type must be `void` (or any value-discarded
        // tick caller — UE4 actor ticks fit) for this to be correct.
        static bool prepare_freeze_entry_hook(BytePatch& bp,
                                              void* tramp,
                                              const float* speedval,
                                              void* site,
                                              const uint8_t* orig_bytes,
                                              size_t orig_len)
        {
            // Upper bound 24 chosen to comfortably accommodate the
            // largest known prologue we hook (site 17's
            // ALuxBattleKeyRecorder TickActor prologue is 18 bytes:
            // PUSH RBP/RBX/R14 + MOV RBP,RSP + SUB RSP,0x80 + MOV RBX,RCX).
            // If a future site needs >24 bytes, bump kMaxOrig and
            // verify the buf[] size below stays within stack budget.
            constexpr size_t kMaxOrig = 24;
            if (orig_len < 5 || orig_len > kMaxOrig) return false;
            constexpr size_t kPrelude = 7 + 2 + 1;   // cmp + jne + ret = 10
            const  size_t kJumpBack   = 5;
            const  size_t kTotal      = kPrelude + orig_len + kJumpBack;

            // Build trampoline body in a local buffer.
            uint8_t buf[kPrelude + kMaxOrig + kJumpBack];
            size_t off = 0;

            // [0x00..0x06] cmp dword [rip+disp32], 0
            buf[off++] = 0x83;
            buf[off++] = 0x3D;
            {
                // disp32 from end-of-instruction (off+4+1=7) to speedval
                const int64_t disp =
                      reinterpret_cast<int64_t>(speedval)
                    - (reinterpret_cast<int64_t>(tramp) + off + 4 + 1);
                if (disp < INT32_MIN || disp > INT32_MAX) return false;
                const int32_t d32 = static_cast<int32_t>(disp);
                std::memcpy(&buf[off], &d32, sizeof(d32));
                off += 4;
            }
            buf[off++] = 0x00;                       // imm8 = 0

            // [0x07..0x08] jne +1 (skip the RET when speedval != 0)
            buf[off++] = 0x75;
            buf[off++] = 0x01;

            // [0x09] ret (return early; void func, stack untouched)
            buf[off++] = 0xC3;

            // [0x0A..0x09+orig_len] replicated original prologue
            std::memcpy(&buf[off], orig_bytes, orig_len);
            off += orig_len;

            // [0x0A+orig_len..] jmp rel32 back to site + orig_len
            uint8_t jmp_back[5];
            void* jmp_at      = static_cast<uint8_t*>(tramp) + off;
            void* back_target = static_cast<uint8_t*>(site)  + orig_len;
            if (!encode_jmp_rel32(jmp_at, back_target, jmp_back)) return false;
            std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
            off += kJumpBack;

            // Commit and flush instruction cache.
            std::memcpy(tramp, buf, off);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);

            // Site patch: 5-byte JMP rel32 + NOP pad to orig_len.
            return prepare_jmp_patch(bp, site, tramp, orig_len);
        }

        // Required cave-allocate size for a freeze-entry-hook trampoline
        // covering `orig_len` displaced bytes.  Match the helper's
        // (kPrelude=10) + orig_len + (kJumpBack=5) accounting.
        static constexpr size_t freeze_entry_hook_size(size_t orig_len)
        {
            return 10 + orig_len + 5;
        }

        BytePatch  m_patch1{};   // KHit_SolvePendulumConstraint
        BytePatch  m_patch3{};   // GetTimeDilationScalar
        BytePatch  m_patch4{};   // AdvanceLinkedMotionObject
        BytePatch  m_patch5{};   // ExecuteOpStream
        BytePatch  m_patch6{};   // AdvanceLaneFrameStep (single-byte ModRM)
        BytePatch  m_patch7{};   // PostATKDelayGate entry-hook (freeze fix)
        BytePatch  m_patch8{};   // CameraAction_AdvancePlayback dt-scale (replay-camera fix)
        BytePatch  m_patch9{};   // PerFrameTick blanket-freeze (replay-timer / replay-input fix)
        BytePatch  m_patch10{};  // LuxReplayChara::Tick (replay frame-buffer copy)
        BytePatch  m_patch11{};  // LuxBattleChara::Tick AdvanceReplayFrame
        BytePatch  m_patch12{};  // LuxBattleChara::Tick CheckFlagsAndNotifyMoveEnded
        BytePatch  m_patch13{};  // LuxBattleManager::Tick SimulationLoop (catch-up loop)
        BytePatch  m_patch14{};  // VTable648 unconditional clock advancer (INC chara+0x3A4) — mid-fn
        BytePatch  m_patch15{};  // VTable648 gated-by-4404 clock advancer (INC chara+0x3A4) — mid-fn
        BytePatch  m_patch16{};  // 2nd-tier cursor advancer (INC chara+0x4410, restart-round path) — mid-fn
        // m_patch17 (KeyRecorder TickActor freeze) — DISABLED 2026-04-26.
        // KeyRecorder is the training-mode key-record/playback actor
        // (proven by L"trainingMode.recordType.slotNo" string inside the
        // function body), not a replay-watching driver.  Hooking it
        // caused a regression in the in-replay input display.  Field
        // kept here so a future re-attempt with a different gating
        // mechanism can re-use the slot.
        BytePatch  m_patch17{};
        // Site 18 — InteractiveReplay_UpdateCamera entry-RET freeze.
        // (DISABLED — both v1 and v2 didn't fix anything; see resolve())
        BytePatch  m_patch18{};
        // Site 19 — LuxBattleChara_ReplayPlayback_PushInputsToActiveSlots.
        // The match-replay input pusher with the catch-up loop.  An
        // earlier-session plate explicitly predicted this would be the
        // next site if drift persisted after site 9.  Fixes the
        // "inputs duplicated each frame in replay" symptom that the
        // user described after sites 1..16 were already in place.
        // The function is reached via the chara's UE4 Actor::Tick
        // through vtable paths that site 9's PerFrameTick gate doesn't
        // catch — separate codepath for full-match replay viewing.
        BytePatch  m_patch19{};
        // Site 20 — LuxReplay_ConsumeDecodedInputPackets_AndUpdateCache.
        // The CACHE FILLER (stage 2 of the replay input pipeline).
        // Site 19 blocks stage 3 (PUSH) but the cache filler kept
        // running during freeze, accumulating ~300 entries in the
        // chara ring at chara+0x3C0 over a 5s freeze.  On step,
        // PushInputs's catch-up loop pushed all the cached entries
        // = "many duplicates after step forward".  Site 20 blocks the
        // cache fill so the ring stays at pre-freeze state.
        BytePatch  m_patch20{};
        // Site 21 — LuxBattleManager_Tick_MainStateMachine_At1461.
        // The BattleManager Actor::Tick wrapper.  Tail-calls
        // AActor_TickActor at the end, which fans out to all
        // registered component ticks INCLUDING the FrameInputLog
        // (BM+0x478) actor's own tick.  Sites 13/19/20 only gate
        // chara-side and SimulationLoop paths; the AActor_TickActor
        // fan-out was the un-gated leak path keeping the replay
        // advancing during freeze.  Symptom user described:
        // "freezing the game, the replay seems to keep playing in
        //  the background and when I unpause it just plays the
        //  inputs that wouldve played if I never paused".
        BytePatch  m_patch21{};
        // Site 22 — ALuxBattleChara::TickActor (the chara Actor::Tick
        // root @ 0x1403D0590).  Sites 9/10/11/12 only gate ~4 of
        // TickActor's sub-handlers; the architecture plate notes
        // "~20 other LuxBattleChara_Tick_* handlers" plus
        // ALuxCharaActor_TickActor, SetActorTransform, hair anim,
        // SyncMoveStateVisibility — all UN-GATED.  Any of these
        // could mutate input-cache state during freeze.  Site 22
        // is the nuclear chara-side gate: bare-RET on speedval==0
        // freezes the entire chara tick chain at the root.
        BytePatch  m_patch22{};
        float*     m_speedval     = nullptr;
        bool       m_resolved     = false;
        bool       m_resolved_ok  = false;
        bool       m_enabled      = false;

        // Process-wide pointer to the live speedval slot.  Set in
        // resolve() once allocation succeeds; persists thereafter.
        // See current_value_static() for the rationale.
        static inline float* s_speedval_global = nullptr;
    };

} // namespace Horse
