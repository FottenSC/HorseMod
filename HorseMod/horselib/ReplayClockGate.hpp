// ============================================================================
// Horse::ReplayClockGate — gate the replay master-clock INC instructions
// on the same policy slot that WorldTickGate uses for Site 9.
//
// Why this exists
// ---------------
// WorldTickGate hooks ONLY LuxBattle_PerFrameTick (Site 9) and bare-RETs it
// when frozen.  That successfully halts the chara simulation core
// (TickCharaMainSimulation, MoveVM advance, hit detection, frame counter).
// For training mode it's sufficient — the only thing driving chara state
// is PerFrameTick.
//
// For MATCH-REPLAY VIEWING it is NOT sufficient.  The replay master clock
// at ALuxBattleFrameInputLog+0x3A4 is INC'd from a SIBLING Actor::Tick
// chain (LuxBattleChara_VTable648_TickAndAdvanceReplayClock + GatedBy4404
// variant) every UE4 frame, regardless of WorldTickGate.  Then
// LuxBattleManager::Tick -> SimulationLoop_UpdateInputAndRoundState reads
// `delta = master_clock - lastApplied` and runs `delta` iterations of the
// catch-up loop per BM tick — applying recorded inputs to BM input data,
// advancing the round state machine, INCing nFrameAdvanceCounter, and
// draining the InputLog frame buffer.
//
// During HorseMod freeze in match replay, the chara simulation (PerFrameTick)
// is paused but the master clock keeps advancing, so SimulationLoop's catch-
// up loop runs every BM tick — buffering recorded inputs into chara+0x3C0
// ring, advancing round state, growing nFrameAdvanceCounter.  When freeze
// releases, the per-frame Stage-3 push (LuxBattleChara_ReplayPlayback_-
// PushInputsToActiveSlots @ 0x1403F6600) drains the accumulated buffer in
// one tick — visible as a fast-forward burst — AND the round state has
// drifted forward.
//
// The fix is to PIN the master clock by gating the INC instructions on the
// same policy slot that WorldTickGate maintains.  When the slot is 0
// (frozen) the INC is skipped; when the slot is non-zero (step credit)
// the INC executes normally.  This stops `delta` from growing during
// freeze, which short-circuits SimulationLoop's catch-up loop at the
// source — no input buffering, no round-state drift.
//
// Two sites
// ---------
// SC6 has TWO functions that INC the master clock with identical semantics:
//
//   Site R1: LuxBattleChara_VTable648_TickAndAdvanceReplayClock
//            @ 0x1403E1FC0, INC at +0x2A
//            unconditional INC after three vtable sub-tick calls.
//
//   Site R2: LuxBattleChara_VTable648_TickAndAdvanceReplayClock_GatedBy4404
//            @ 0x1403E2000, INC at +0x33
//            same INC, gated by a CMP byte [rcx+0x4404],0 prologue check
//            for the live-tick path; the INC itself is unconditional.
//
// Both take an ALuxBattleFrameInputLog* in RBX at the INC site, and the
// instruction is exactly `FF 83 A4 03 00 00` = `inc dword [rbx+0x3A4]`.
// Both have a clean epilogue (ADD RSP,0x20; POP RBX; RET) immediately
// after the INC.  We patch both with the same trampoline pattern.
//
// Policy slot semantics (shared with WorldTickGate)
// -------------------------------------------------
//   slot == 0   : skip the INC (frozen)
//   slot != 0   : execute the INC normally (running OR step-credit-armed)
//
// Sibling gates do NOT decrement the policy slot.  Site 9 (PerFrameTick)
// is the SOLE consumer of step credits.  Any tick-order-dependent skew
// (e.g. Site 9 fires first this UE4 frame, decrements slot to 0; Site R1
// fires next, sees 0, skips its INC) is acceptable: the simulation still
// advanced one frame, the master clock just lags one frame, and
// SimulationLoop's catch-up will reconcile on the next frame the master
// clock advances.  The CRITICAL property — "during freeze, master clock
// stays pinned" — is preserved regardless of tick order.
//
// Race notes
// ----------
// Same as WorldTickGate: the trampoline does a non-atomic read of the
// policy slot.  Concurrent writes (from F6 hotkey, cockpit pre-tick, or
// the C++ disable() path) are atomic on x86-64 aligned int32, so the
// trampoline either sees the old value or the new value — never a
// torn read.  Worst case is the trampoline observes a stale value and
// misses one INC; the next UE4 frame will reconcile.
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
    class ReplayClockGate
    {
    public:
        // Sig-scan both INC sites, allocate trampolines that read from the
        // shared `policy_slot` (passed in from WorldTickGate), prepare both
        // BytePatches.  Idempotent.
        //
        // `policy_slot` MUST point to a stable int32_t whose lifetime
        // outlasts this gate (typically WorldTickGate's cave-resident slot).
        // Returns false if either AOB doesn't match, the cave is exhausted,
        // or any rel32 displacement won't fit.
        bool resolve(int32_t* policy_slot)
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved    = true;
            m_resolved_ok = false;

            if (!policy_slot)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ReplayClockGate] resolve() got null policy "
                        "slot — call WorldTickGate.resolve() first and pass "
                        "its policy_slot_address()\n"));
                return false;
            }

            // Site R1: the unconditional advancer.  AOB locks the prologue
            // through the replay-clock INC at +0x2A.  A shorter prefix ending
            // at `call [rax+0x650]` also matches an unrelated engine helper,
            // so include the full vtable-call chain and semantic target INC.
            //
            // 48-byte AOB:
            //   40 53                push rbx
            //   48 83 EC 20          sub  rsp, 0x20
            //   48 8B 01             mov  rax, [rcx]
            //   48 8B D9             mov  rbx, rcx
            //   FF 90 50 06 00 00    call [rax+0x650]
            //   48 8B 03             mov  rax, [rbx]
            //   48 8B CB             mov  rcx, rbx
            //   FF 90 40 06 00 00    call [rax+0x640]
            //   48 8B 03             mov  rax, [rbx]
            //   48 8B CB             mov  rcx, rbx
            //   FF 90 48 06 00 00    call [rax+0x648]
            //   FF 83 A4 03 00 00    inc  dword [rbx+0x3A4]
            void* site_r1_entry = sig_scan_sc6(
                "40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 90 50 06 00 00 "
                "48 8B 03 48 8B CB FF 90 40 06 00 00 "
                "48 8B 03 48 8B CB FF 90 48 06 00 00 "
                "FF 83 A4 03 00 00",
                "ReplayClockGate Site R1 (VTable648_TickAndAdvanceReplayClock)");
            if (!site_r1_entry) return false;

            // Site R2: the gated-by-+0x4404 variant.  Same prologue first
            // 6 bytes, but byte 6 is `cmp byte [rcx+0x4404],0` instead of
            // `mov rax,[rcx]`.
            //
            // 18-byte AOB:
            //   40 53                push rbx
            //   48 83 EC 20          sub  rsp, 0x20
            //   80 B9 04 44 00 00 00 cmp  byte [rcx+0x4404], 0
            //   48 8B D9             mov  rbx, rcx
            //   48 8B 01             mov  rax, [rcx]
            void* site_r2_entry = sig_scan_sc6(
                "40 53 48 83 EC 20 80 B9 04 44 00 00 00 48 8B D9 48 8B 01",
                "ReplayClockGate Site R2 (VTable648_..._GatedBy4404)");
            if (!site_r2_entry) return false;

            void* site_r1_inc = static_cast<uint8_t*>(site_r1_entry) + 0x2A;
            void* site_r2_inc = static_cast<uint8_t*>(site_r2_entry) + 0x33;

            if (!build_site(site_r1_inc, policy_slot, m_patch_r1, "R1")) return false;
            if (!build_site(site_r2_inc, policy_slot, m_patch_r2, "R2")) return false;

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ReplayClockGate] resolved (R1 INC @ 0x{:x}, "
                    "R2 INC @ 0x{:x})\n"),
                reinterpret_cast<uintptr_t>(site_r1_inc),
                reinterpret_cast<uintptr_t>(site_r2_inc));
            return true;
        }

        // Apply both INC patches.  Both sites are toggled atomically with
        // respect to each other (within the limits of two separate
        // VirtualProtect calls — fine in practice since the patched pages
        // aren't being executed mid-toggle on any thread we care about).
        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayClockGate] enable() before resolve() — "
                        "ignoring\n"));
                return false;
            }
            if (m_enabled.load(std::memory_order_acquire)) return true;
            const bool ok_r1 = m_patch_r1.enable();
            const bool ok_r2 = m_patch_r2.enable();
            if (!ok_r1 || !ok_r2)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ReplayClockGate] BytePatch.enable() failed "
                        "(R1={}, R2={})\n"),
                    ok_r1 ? STR("ok") : STR("FAIL"),
                    ok_r2 ? STR("ok") : STR("FAIL"));
                // Roll back whichever succeeded so we don't leave a half-
                // applied state behind.
                if (ok_r1) m_patch_r1.disable();
                if (ok_r2) m_patch_r2.disable();
                return false;
            }
            m_enabled.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ReplayClockGate] enabled (both INC sites gated)\n"));
            return true;
        }

        // Revert both patches.
        void disable()
        {
            if (!m_enabled.load(std::memory_order_acquire)) return;
            m_patch_r1.disable();
            m_patch_r2.disable();
            m_enabled.store(false, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ReplayClockGate] disabled\n"));
        }

        bool is_enabled()  const { return m_enabled.load(std::memory_order_acquire); }
        bool is_resolved() const { return m_resolved_ok; }

    private:
        // Build one trampoline + BytePatch for a single INC site.  Returns
        // false if the cave is exhausted or rel32 displacements don't fit.
        //
        // Trampoline layout (21 bytes):
        //   [0x00] 8B 05 <disp32>           mov eax, [rip+policy]
        //   [0x06] 85 C0                    test eax, eax
        //   [0x08] 74 06                    je  +6   (jumps to JMP-back at 0x10)
        //   [0x0A] FF 83 A4 03 00 00        inc dword [rbx+0x3A4]   (replicated)
        //   [0x10] E9 <rel32>               jmp <site+0x6>          (back to function epilogue)
        //   [0x15]                          (end)
        //
        // Site patch (6 bytes): JMP rel32 to trampoline + 1 NOP.  The
        // displaced INC is preserved by the trampoline; nothing else in
        // the function depends on flags-after-INC, so skipping the INC
        // when frozen leaves correct state for the function's epilogue.
        static bool build_site(void* inc_site, int32_t* policy,
                               BytePatch& patch, const char* tag)
        {
            constexpr size_t kTrampSize = 21;
            void* tramp = CodeCave::allocate(kTrampSize);
            if (!tramp)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ReplayClockGate] {} cave alloc failed\n"),
                    RC::to_generic_string(tag));
                return false;
            }

            uint8_t buf[kTrampSize] = {0};
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

            // [0x08] je +6  -> targets offset 0x10 (the JMP-back)
            buf[off++] = 0x74;
            buf[off++] = 0x06;

            // [0x0A] inc dword [rbx+0x3A4]   (replicated 6 bytes)
            buf[off++] = 0xFF;
            buf[off++] = 0x83;
            buf[off++] = 0xA4;
            buf[off++] = 0x03;
            buf[off++] = 0x00;
            buf[off++] = 0x00;

            // [0x10] jmp rel32 -> inc_site + 6  (function epilogue)
            {
                uint8_t jmp_back[5];
                void* jmp_at      = static_cast<uint8_t*>(tramp) + off;
                void* back_target = static_cast<uint8_t*>(inc_site) + 6;
                if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                    return false;
                std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
                off += 5;
            }

            // [0x15] (end)

            std::memcpy(tramp, buf, off);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);

            // Site patch: 5-byte JMP rel32 to trampoline + 1 NOP over the
            // displaced 6-byte INC.
            uint8_t patch_buf[6];
            uint8_t jmp[5];
            if (!encode_jmp_rel32(inc_site, tramp, jmp)) return false;
            std::memcpy(patch_buf, jmp, 5);
            patch_buf[5] = 0x90; // NOP

            if (!patch.prepare(inc_site, patch_buf, 6))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ReplayClockGate] {} patch.prepare() failed\n"),
                    RC::to_generic_string(tag));
                return false;
            }
            return true;
        }

        BytePatch m_patch_r1{};
        BytePatch m_patch_r2{};
        bool      m_resolved    = false;
        bool      m_resolved_ok = false;
        std::atomic<bool> m_enabled{false};
    };

} // namespace Horse
