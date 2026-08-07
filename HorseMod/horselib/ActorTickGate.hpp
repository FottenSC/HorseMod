// ============================================================================
// Horse::ActorTickGate — bare-RET on Actor::Tick prologues that drive chara
// state independently of LuxBattle_PerFrameTick.  Companion to WorldTickGate
// (Site 9 = PerFrameTick) and ReplayClockGate (master-clock INC).
//
// Why this exists
// ---------------
// During match-replay viewing, several actors fire their own Actor::Tick
// chains every UE4 frame.  WorldTickGate's Site-9 hook only halts the
// simulation core (MoveVM, hit detection, frame counter); these sibling
// ticks keep advancing chara state, recorded-input cursors, round timers,
// and anim playback.  When freeze releases, the simulation jumps forward
// by the wallclock-equivalent number of buffered inputs and the chara
// settles into the idle pose.  Each site below is a tick path that has to
// be RET'd in lockstep with Site 9 to fully halt match-replay state.
//
// Sites
// -----
//   Site 11 — LuxBattleChara_Tick_AdvanceReplayFrame_OrLocal (0x1403F8410)
//             Chara-side replay tick.  In REPLAY mode dispatches
//             vtable[0x6A8] (Stage 2/3 input pipeline) and vtable[0x6C8]
//             (chara replay-state writer at chara+0x39C/+0x3A0/etc.).
//             THE primary leak path: pushes the recorded input every UE4
//             frame to the chara's active slot.  Without 11, freezing for
//             N seconds buffers N seconds of inputs that drain in a burst
//             on unfreeze, and stepping shows momentary inputs as held
//             continuously.  Old SpeedControl had this as Site 11; the
//             hook was dropped during the WorldTickGate rewrite.
//
//   Site 20 — ALuxBattleFrameInputLog::TickActor (0x1403FBDF0)
//             InputLog actor's tick.  Dispatches vtable[0x5F8] which
//             fans out to a sub-tick chain that advances counters at
//             InputLog+0x3AC and runs Stage 2/3 dispatch + master-clock
//             INC sibling.  ReplayClockGate gates only the INC; the rest
//             of the chain needs this entry-RET to stop fully.
//
//   Site 21 — ALuxBattleManager_Tick_MainStateMachine_At1461 (0x1403FBF30)
//             Calls SimulationLoop_UpdateInputAndRoundState (the catch-up
//             loop that consumes recorded inputs) then the round-over
//             check that flips BM state to round-end.  Without 21, a
//             wallclock-driven round timer trips round-over on unfreeze
//             and resets the chara to neutral.
//
//   Site 21b — ALuxBattleManager_Update_Impl (0x140437590)
//             Sibling BM tick that ticks the "BattleTime" /
//             "BattleSystemTime" FName timers via TickTimerHandle —
//             the round-timer driver.  Registered through a different
//             dispatch slot than Site 21, so 21 alone doesn't catch it.
//
//   Site 22 — ALuxBattleChara::TickActor (0x1403D0590)
//             Maegami hair, weapon mesh anim, SC charge gauge, parent-
//             class actor tick.  Long anim montages play out via this
//             path even with the SC6 sim frozen.
//
//   Site 22b — ALuxDemoHumanActor::TickActor (0x1404865B0) — derived class
//             Inherits ALuxBattleChara, used for match-replay cinematic
//             playback.  Body runs anim-track playback BEFORE tail-calling
//             the super; Site 22 only catches the super, not the body.
//
//   Site 22c — APreviewHumanActor::TickActor (0x140486C60) — derived class
//             Sibling of 22b (menu chara previews / cinematic paths).
//
// All entries are void-returning functions whose RSP at entry equals the
// caller's RSP (no prologue has executed yet), so bare-RET on entry is a
// clean no-op — the engine sees a tick that did nothing.
//
// Policy slot semantics (shared with WorldTickGate)
// -------------------------------------------------
//   slot == 0  : RET (frozen)
//   slot != 0  : execute the prologue and continue normally
//
// ActorTickGate does NOT decrement the slot — Site 9 (PerFrameTick) is the
// sole step-credit consumer.  Same rationale as ReplayClockGate's plate.
//
// This gate is a replay-freeze primitive, not a no-render optimization.
// DemoHuman and BattleChara ticks synchronize replay animation tracks and
// Unreal AnimInstances; suppressing them while timeline simulation advances
// produces correct gameplay snapshots with stale visible animation.
// ============================================================================

#pragma once

#include "BytePatch.hpp"
#include "CodeCave.hpp"
#include "SigScan.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace Horse
{
    class ActorTickGate
    {
    public:
        // Sig-scan both function entries, build entry-RET trampolines that
        // read from the shared `policy_slot`.  Idempotent.  Returns false
        // if either AOB doesn't match, the cave is exhausted, or any rel32
        // displacement won't fit.
        bool resolve(int32_t* policy_slot)
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved    = true;
            m_resolved_ok = false;

            if (!policy_slot)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ActorTickGate] resolve() got null policy "
                        "slot — call WorldTickGate.resolve() first and pass "
                        "its policy_slot_address()\n"));
                return false;
            }

            // Site 20: ALuxBattleFrameInputLog::TickActor prologue.  This
            // is the entry point for the entire InputLog tick chain that
            // drives Stage 3 input push + counter advances at +0x3AC.
            //
            //   40 53                 push rbx        (REX prefix + opcode)
            //   48 83 EC 20           sub  rsp, 0x20
            //   48 8B D9              mov  rbx, rcx
            //   E8 ?? ?? ?? ??        call AActor::TickActor super
            //   48 8B 03              mov  rax, [rbx]
            //   48 8B CB              mov  rcx, rbx
            //   48 83 C4 20           add  rsp, 0x20
            //   5B                    pop  rbx
            //
            // The first 9 bytes (push+sub+mov rbx,rcx) AND the post-call
            // shape `48 8B 03 48 8B CB` (mov rax,[rbx]; mov rcx,rbx) are
            // shared with at least one other actor's TickActor stub
            // (CCpuDirectAllGuardCount_StandGuardVtable_Tick @ RVA
            // 0x36B311).  The disambiguator is the trailing
            // `48 83 C4 20 5B` (add rsp,0x20; pop rbx) — present here
            // because this function tail-calls vtable[0x5F8], whereas
            // the lookalike has `call [rax+0x18]` and a different
            // epilogue.  Tighten the AOB any further and a future SC6
            // build that re-orders the post-call instructions could
            // break this scan; keep the disambiguator at the tail.
            //
            // The leading `40` REX prefix on `push rbx` is a Microsoft
            // compiler alignment artefact — the CPU ignores it on this
            // opcode but it's required for the AOB to land on the
            // function entry rather than the byte after.
            void* site20 = sig_scan_sc6(
                "40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B 03 "
                "48 8B CB 48 83 C4 20 5B",
                "ActorTickGate Site 20 (ALuxBattleFrameInputLog::TickActor)");
            if (!site20) return false;

            // Site 22: ALuxBattleChara::TickActor prologue.  AOB locks the
            // first 23 bytes through the SUB RSP, 0x250 — the imm32 and
            // surrounding LEA/SUB pattern is distinctive enough to avoid
            // false-matching any other actor's TickActor.
            //
            //   40 55                    push rbp        (REX + push rbp)
            //   53                       push rbx
            //   56                       push rsi
            //   41 54                    push r12        (REX + push r12)
            //   41 55                    push r13        (REX + push r13)
            //   48 8D AC 24 B0 FE FF FF  lea  rbp, [rsp-0x150]
            //   48 81 EC 50 02 00 00     sub  rsp, 0x250
            void* site22 = sig_scan_sc6(
                "40 55 53 56 41 54 41 55 48 8D AC 24 B0 FE FF FF 48 81 EC 50 02 00 00",
                "ActorTickGate Site 22 (ALuxBattleChara::TickActor)");
            if (!site22) return false;

            // Site 21: ALuxBattleManager_Tick_MainStateMachine_At1461.
            // Prologue: push rbx + sub rsp,0x30 + mov rbx,rcx + movaps save xmm6.
            //
            //   40 53                    push rbx        (REX + push rbx)
            //   48 83 EC 30              sub  rsp, 0x30
            //   48 8B D9                 mov  rbx, rcx
            //   0F 29 74 24 20           movaps [rsp+0x20], xmm6
            //   0F B6 89 61 14 00 00     movzx ecx, [rcx+0x1461]   (state byte)
            void* site21 = sig_scan_sc6(
                "40 53 48 83 EC 30 48 8B D9 0F 29 74 24 20 0F B6 89 61 14 00 00",
                "ActorTickGate Site 21 (BM MainStateMachine_At1461)");
            if (!site21) return false;

            // Site 20: displaces the first 6 bytes (REX + push rbx +
            // sub rsp,0x20 = 2 + 4).  After the patch the function
            // continues at site+6 (the MOV RBX, RCX instruction).  At
            // entry no callee-saved register has been touched yet, so
            // bare RET is a clean no-op — the engine sees a tick that
            // did nothing.
            if (!build_site(site20, /*orig_len=*/6, policy_slot,
                            m_patch_20, "Site 20 (FrameInputLog::TickActor)"))
                return false;

            // Site 22 displaces the first 6 bytes (push rbp + push rbx +
            // push rsi + push r12 = 2 + 1 + 1 + 2).  After the patch, the
            // function continues at site+6 (push r13 onwards).
            if (!build_site(site22, /*orig_len=*/6, policy_slot,
                            m_patch_22, "Site 22 (ChChara::TickActor)"))
                return false;

            // Site 21 displaces the first 6 bytes (push rbx + sub rsp,0x30
            // = 2 + 4).  After the patch, the function continues at site+6
            // (mov rbx, rcx onwards).
            if (!build_site(site21, /*orig_len=*/6, policy_slot,
                            m_patch_21, "Site 21 (BM MainStateMachine)"))
                return false;

            // Site 22b: ALuxDemoHumanActor::TickActor prologue.  This is
            // the DERIVED-CLASS override that UE4 actually dispatches when
            // a chara of the demo/replay variant is ticked.  The function
            // body runs anim playback BEFORE tail-calling
            // ALuxBattleChara::TickActor as super, so Site 22 alone can't
            // catch the body's anim advance.
            //
            //   48 8B C4              mov rax, rsp
            //   55                    push rbp
            //   56                    push rsi
            //   48 8D 68 A1           lea rbp, [rax-0x5F]
            //   48 81 EC A8 00 00 00  sub rsp, 0xA8
            //   80 B9 14 06 00 00 00  cmp byte [rcx+0x614], 0
            void* site22b = sig_scan_sc6(
                "48 8B C4 55 56 48 8D 68 A1 48 81 EC A8 00 00 00 80 B9 14 06 00 00 00",
                "ActorTickGate Site 22b (ALuxDemoHumanActor::TickActor)");
            if (!site22b) return false;

            // Site 22b: displaces the first 5 bytes (mov rax,rsp + push
            // rbp + push rsi = 3 + 1 + 1).  After the patch, the function
            // continues at site+5 (the LEA RBP, [RAX-0x5F] instruction
            // that depends on RAX = RSP set by the displaced MOV).
            if (!build_site(site22b, /*orig_len=*/5, policy_slot,
                            m_patch_22b, "Site 22b (DemoHumanActor::TickActor)"))
                return false;

            // Site 22c: APreviewHumanActor::TickActor prologue.  Sibling
            // of Site 22b (same anim-playback dispatch pattern, different
            // derived class).  Used for character preview in menus and
            // potentially other cinematic paths.
            //
            //   4C 8B DC                 mov r11, rsp
            //   55                       push rbp
            //   56                       push rsi
            //   49 8D 6B E8              lea rbp, [r11-0x18]
            //   48 81 EC 08 01 00 00     sub rsp, 0x108
            void* site22c = sig_scan_sc6(
                "4C 8B DC 55 56 49 8D 6B E8 48 81 EC 08 01 00 00",
                "ActorTickGate Site 22c (APreviewHumanActor::TickActor)");
            if (!site22c) return false;

            // Site 22c: displaces the first 5 bytes (mov r11,rsp + push
            // rbp + push rsi = 3 + 1 + 1).  After the patch, the function
            // continues at site+5 (the LEA RBP, [R11-0x18] that depends
            // on R11 = RSP set by the displaced MOV).
            if (!build_site(site22c, /*orig_len=*/5, policy_slot,
                            m_patch_22c, "Site 22c (PreviewHumanActor::TickActor)"))
                return false;

            // Site 11: LuxBattleChara_Tick_AdvanceReplayFrame_OrLocal.
            // Chara-side replay-frame advance — runs every UE4 frame
            // independent of PerFrameTick / chara TickActor / InputLog
            // tick.  In REPLAY mode dispatches vtable[0x6A8] (Stage 2/3
            // input pipeline) and vtable[0x6C8] (chara replay-state
            // writer) — the path that was pushing recorded inputs into
            // chara state every frame during freeze, producing the
            // "stepping shows held inputs / freeze drifts ahead" symptom.
            //
            //   40 53                    push rbx        (REX + opcode)
            //   48 83 EC 30              sub  rsp, 0x30
            //   83 B9 00 44 00 00 00     cmp  dword [rcx+0x4400], 0
            //   48 8B D9                 mov  rbx, rcx
            //
            // 16-byte AOB locks the prologue through the MOV RBX, RCX.
            // The leading `40` REX prefix on `push rbx` is required —
            // omitting it lands the match on the `53` byte one byte
            // into the function (same alignment-artefact prefix as
            // Site 20 / Site 22).
            void* site11 = sig_scan_sc6(
                "40 53 48 83 EC 30 83 B9 00 44 00 00 00 48 8B D9",
                "ActorTickGate Site 11 (Chara_Tick_AdvanceReplayFrame_OrLocal)");
            if (!site11) return false;

            // Site 11: displaces the first 6 bytes (REX + push rbx +
            // sub rsp,0x30 = 2 + 4).  After the patch the function
            // continues at site+6 (the CMP DWORD [RCX+0x4400], 0
            // instruction).  At entry no callee-saved register has been
            // touched yet, so bare RET is a clean no-op.
            if (!build_site(site11, /*orig_len=*/6, policy_slot,
                            m_patch_11, "Site 11 (Chara replay-frame advance)"))
                return false;

            // Site 21b: ALuxBattleManager_Update_Impl prologue.  Distinct
            // tick driver from Site 21 (MainStateMachine_At1461).  This is
            // what ticks "BattleTime" / "BattleSystemTime" FName timers
            // (the round timer) — uniquely matches the user's repro of
            // chara settling to idle after ~1 minute (= round duration).
            //
            //   48 8B C4              mov rax, rsp
            //   55                    push rbp
            //   41 56                 push r14
            //   48 8D 68 A1           lea rbp, [rax-0x5F]
            //   48 81 EC D8 00 00 00  sub rsp, 0xD8
            void* site21b = sig_scan_sc6(
                "48 8B C4 55 41 56 48 8D 68 A1 48 81 EC D8 00 00 00",
                "ActorTickGate Site 21b (ALuxBattleManager_Update_Impl)");
            if (!site21b) return false;

            // Site 21b: displaces the first 6 bytes (mov rax,rsp + push
            // rbp + push r14 = 3 + 1 + 2).  After the patch, the function
            // continues at site+6 (the LEA RBP, [RAX-0x5F] that depends
            // on RAX = RSP set by the displaced MOV).
            if (!build_site(site21b, /*orig_len=*/6, policy_slot,
                            m_patch_21b, "Site 21b (BM Update_Impl)"))
                return false;

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ActorTickGate] resolved (Site 20 @ 0x{:x}, "
                    "Site 11 @ 0x{:x}, Site 22 @ 0x{:x}, Site 21 @ 0x{:x}, "
                    "Site 22b @ 0x{:x}, Site 22c @ 0x{:x}, Site 21b @ 0x{:x})\n"),
                reinterpret_cast<uintptr_t>(site20),
                reinterpret_cast<uintptr_t>(site11),
                reinterpret_cast<uintptr_t>(site22),
                reinterpret_cast<uintptr_t>(site21),
                reinterpret_cast<uintptr_t>(site22b),
                reinterpret_cast<uintptr_t>(site22c),
                reinterpret_cast<uintptr_t>(site21b));
            return true;
        }

        // Apply all seven entry-RET patches.  On any failure roll back the
        // patches that did succeed so we don't leave a half-applied state
        // (the engine in a half-applied state would crash on the next
        // tick when a function with the JMP patch jumped to a trampoline
        // referencing an unmapped policy slot).
        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ActorTickGate] enable() before resolve() — "
                        "ignoring\n"));
                return false;
            }
            if (m_enabled.load(std::memory_order_acquire)) return true;
            const bool ok_20  = m_patch_20.enable();
            const bool ok_11  = m_patch_11.enable();
            const bool ok_22  = m_patch_22.enable();
            const bool ok_21  = m_patch_21.enable();
            const bool ok_22b = m_patch_22b.enable();
            const bool ok_22c = m_patch_22c.enable();
            const bool ok_21b = m_patch_21b.enable();
            if (!ok_20 || !ok_11 || !ok_22 || !ok_21 || !ok_22b ||
                !ok_22c || !ok_21b)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ActorTickGate] BytePatch.enable() failed "
                        "(Site 20={}, Site 11={}, Site 22={}, Site 21={}, "
                        "Site 22b={}, Site 22c={}, Site 21b={})\n"),
                    ok_20  ? STR("ok") : STR("FAIL"),
                    ok_11  ? STR("ok") : STR("FAIL"),
                    ok_22  ? STR("ok") : STR("FAIL"),
                    ok_21  ? STR("ok") : STR("FAIL"),
                    ok_22b ? STR("ok") : STR("FAIL"),
                    ok_22c ? STR("ok") : STR("FAIL"),
                    ok_21b ? STR("ok") : STR("FAIL"));
                if (ok_20)  m_patch_20.disable();
                if (ok_11)  m_patch_11.disable();
                if (ok_22)  m_patch_22.disable();
                if (ok_21)  m_patch_21.disable();
                if (ok_22b) m_patch_22b.disable();
                if (ok_22c) m_patch_22c.disable();
                if (ok_21b) m_patch_21b.disable();
                return false;
            }
            m_enabled.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ActorTickGate] enabled (InputLog + chara replay "
                    "advance + chara + BM + Demo + Preview TickActor + BM "
                    "Update_Impl entry-RETs gated)\n"));
            return true;
        }

        // Revert all seven patches.
        void disable()
        {
            if (!m_enabled.load(std::memory_order_acquire)) return;
            m_patch_20.disable();
            m_patch_11.disable();
            m_patch_22.disable();
            m_patch_21.disable();
            m_patch_22b.disable();
            m_patch_22c.disable();
            m_patch_21b.disable();
            m_enabled.store(false, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ActorTickGate] disabled\n"));
        }

        bool is_enabled()  const { return m_enabled.load(std::memory_order_acquire); }
        bool is_resolved() const { return m_resolved_ok; }

    private:
        // Build one entry-RET trampoline + BytePatch for a given function
        // entry.  Trampoline layout (16 + orig_len bytes):
        //
        //   [0x00] 8B 05 <disp32>      mov eax, [rip+policy]
        //   [0x06] 85 C0               test eax, eax
        //   [0x08] 75 01               jne +1   (skip the C3 below)
        //   [0x0A] C3                  ret      (bail when frozen)
        //   [0x0B] <orig_len bytes>    replicated original prologue
        //   [0x0B+N] E9 <rel32>        jmp <site+orig_len>
        //
        // Site patch: 5-byte JMP rel32 to trampoline + (orig_len - 5) NOPs.
        // We require orig_len >= 5 so the JMP fits.
        static bool build_site(void* site, size_t orig_len, int32_t* policy,
                               BytePatch& patch, const char* tag)
        {
            if (orig_len < 5)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ActorTickGate] {} orig_len {} < 5\n"),
                    RC::to_generic_string(tag), orig_len);
                return false;
            }

            const size_t kTrampSize = 16 + orig_len;
            void* tramp = CodeCave::allocate(kTrampSize);
            if (!tramp)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ActorTickGate] {} cave alloc failed\n"),
                    RC::to_generic_string(tag));
                return false;
            }

            uint8_t buf[64] = {0};
            if (kTrampSize > sizeof(buf))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ActorTickGate] {} trampoline buffer too small\n"),
                    RC::to_generic_string(tag));
                return false;
            }

            size_t off = 0;

            // [0x00] mov eax, [rip+disp32_policy]
            buf[off++] = 0x8B;
            buf[off++] = 0x05;
            {
                const int64_t disp =
                      reinterpret_cast<int64_t>(policy)
                    - (reinterpret_cast<int64_t>(tramp) + off + 4);
                if (disp < INT32_MIN || disp > INT32_MAX) return false;
                const int32_t d32 = static_cast<int32_t>(disp);
                std::memcpy(&buf[off], &d32, sizeof(d32));
                off += 4;
            }

            // [0x06] test eax, eax
            buf[off++] = 0x85;
            buf[off++] = 0xC0;

            // [0x08] jne +1   (skip the C3 RET when policy != 0)
            buf[off++] = 0x75;
            buf[off++] = 0x01;

            // [0x0A] ret
            buf[off++] = 0xC3;

            // [0x0B] replicated prologue bytes
            std::memcpy(&buf[off], site, orig_len);
            off += orig_len;

            // [0x0B + orig_len] jmp rel32 -> site + orig_len
            {
                uint8_t jmp_back[5];
                void* jmp_at      = static_cast<uint8_t*>(tramp) + off;
                void* back_target = static_cast<uint8_t*>(site) + orig_len;
                if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                    return false;
                std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
                off += 5;
            }

            std::memcpy(tramp, buf, off);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);

            // Site patch: 5-byte JMP to trampoline + (orig_len - 5) NOPs
            // so we don't leave half-decoded instructions when disabled.
            uint8_t patch_buf[16] = {0};
            uint8_t jmp[5];
            if (!encode_jmp_rel32(site, tramp, jmp)) return false;
            std::memcpy(patch_buf, jmp, 5);
            for (size_t i = 5; i < orig_len; ++i) patch_buf[i] = 0x90;

            if (!patch.prepare(site, patch_buf, orig_len))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ActorTickGate] {} patch.prepare() failed\n"),
                    RC::to_generic_string(tag));
                return false;
            }
            return true;
        }

        BytePatch m_patch_20{};   // ALuxBattleFrameInputLog::TickActor (input pipeline)
        BytePatch m_patch_11{};   // Chara_Tick_AdvanceReplayFrame_OrLocal (chara replay-state writer)
        BytePatch m_patch_22{};   // ALuxBattleChara::TickActor
        BytePatch m_patch_21{};   // BM MainStateMachine_At1461
        BytePatch m_patch_22b{};  // ALuxDemoHumanActor::TickActor (derived)
        BytePatch m_patch_22c{};  // APreviewHumanActor::TickActor (derived)
        BytePatch m_patch_21b{};  // ALuxBattleManager_Update_Impl (round timer)
        bool      m_resolved    = false;
        bool      m_resolved_ok = false;
        std::atomic<bool> m_enabled{false};
    };

} // namespace Horse
