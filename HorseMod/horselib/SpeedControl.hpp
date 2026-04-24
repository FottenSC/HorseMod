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
            if (!site1 || !site3 || !site4 || !site5 || !site6) return false;

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
            if (!tramp1 || !tramp3 || !tramp4 || !tramp5) return false;

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

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.SpeedControl] resolved 5 patch sites + speedval "
                    "slot @ 0x{:x}\n"),
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
            BytePatch* patches[] = { &m_patch1, &m_patch3, &m_patch4,
                                     &m_patch5, &m_patch6 };
            for (size_t i = 0; i < 5; ++i)
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
        static bool prepare_jmp_patch(BytePatch& bp, void* site, void* tramp,
                                      size_t orig_len)
        {
            if (orig_len < 5 || orig_len > 16) return false;
            uint8_t buf[16];
            uint8_t jmp[5];
            if (!encode_jmp_rel32(site, tramp, jmp)) return false;
            std::memcpy(buf, jmp, 5);
            for (size_t i = 5; i < orig_len; ++i) buf[i] = 0x90;  // NOP pad
            return bp.prepare(site, buf, orig_len);
        }

        BytePatch  m_patch1{};   // KHit_SolvePendulumConstraint
        BytePatch  m_patch3{};   // GetTimeDilationScalar
        BytePatch  m_patch4{};   // AdvanceLinkedMotionObject
        BytePatch  m_patch5{};   // ExecuteOpStream
        BytePatch  m_patch6{};   // AdvanceLaneFrameStep (single-byte ModRM)
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
