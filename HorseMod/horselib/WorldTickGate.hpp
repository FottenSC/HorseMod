// ============================================================================
// Horse::WorldTickGate — single PerFrameTick (Site 9) gate that handles
// freeze + frame-step semantics independently of speedval / dt-multiply paths.
//
// Origin
// ------
// Replaces SpeedControl's Site 9 patch.  Validates the proposal "Replace
// SpeedControl with a single PerFrameTick gate" (2026-05-05).  Short version:
// the dual mechanism in SpeedControl (dt-multiply at sites 1/3/4/5/6/8 +
// entry-RET at site 9) was breaking multi-hit moves under frame-step
// because the dt-multiply sites still RUN their function bodies at dt=0
// when speedval=0 — the integrator produces 0 but the cell INIT path
// still executes against stale dt=0 state, contaminating the next step.
//
// New model: speedval STAYS at 1.0 always (so the dt-multiply sites are
// no-ops); freeze and step are the sole responsibility of this gate.
// No fractional dt anywhere in the simulation — every game frame is
// either fully run at native dt, or not run at all.
//
// Validation scope
// ----------------
// This first commit rewires Site 9 ONLY.  Sites 10..22 (replay-side actor-
// tick gates) still read speedval == 0; with speedval pinned to 1.0 they
// no longer fire under freeze, so replay-watch correctness regresses
// temporarily.  That's acceptable for offline-training multi-hit testing
// (no replay actor in training mode).  The "mechanical cleanup" step in
// the proposal is to also rewire 10..22 onto this gate; we'll do that
// after Site 9 alone is shown to fix Siegfried 4A+B in training.
//
// Policy state (cave-resident int32_t)
// ------------------------------------
//     0  : frozen — every PerFrameTick call bails (bare RET).
//   > 0  : step credits — each PerFrameTick call atomically dec's the
//          slot by 1 and runs the displaced prologue normally.
//   < 0  : unused (treated as "always run" — defensive equivalent of
//          Native for any path that accidentally writes negative).
//
// "Native" mode is achieved by DISABLING the BytePatch (no hook).  The
// engine's original PerFrameTick prologue runs unconditionally, no
// policy slot consulted.
//
// Race notes
// ----------
// The cockpit pre-hook (game thread) is sequenced with PerFrameTick (also
// game thread) — they cannot race.  The F6 hotkey runs on a UE4SS keyboard
// thread and can race with PerFrameTick: the trampoline does a non-atomic
// load-test-dec sequence, so a hotkey-issued add_step concurrent with a
// trampoline run can lose at most one credit.  Operationally that means
// the user has to press F6 again — acceptable.  If we ever care, swap the
// trampoline to a `lock cmpxchg` retry loop (~6 more bytes).
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
    class WorldTickGate
    {
    public:
        // Sig-scan Site 9 (LuxBattle_PerFrameTick prologue), allocate the
        // policy slot + trampoline, prepare the BytePatch.  Idempotent.
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved    = true;
            m_resolved_ok = false;

            // Same anchor SpeedControl used for Site 9 (now removed there).
            // 26-byte AOB locks the prologue down to & including the
            // SUB RSP, 0x80 imm32 — the second LuxBattle_* function in
            // the binary has identical first 19 bytes but SUB RSP, 0xC0,
            // so including the imm32 disambiguates.
            void* site9 = sig_scan_sc6(
                "4C 8B DC 49 89 5B 10 49 89 6B 18 56 57 41 54 41 56 41 57 48 81 EC 80 00 00 00",
                "WorldTickGate (LuxBattle_PerFrameTick entry-hook)");
            if (!site9) return false;

            m_policy = static_cast<int32_t*>(
                CodeCave::allocate(sizeof(int32_t), alignof(int32_t)));
            if (!m_policy) return false;
            // Start at "frozen" (0) so an enable-without-prior-set leaves
            // the engine paused rather than free-running.  The cockpit
            // pre-hook will overwrite this on its first call.
            policy_store_relaxed(0);

            // Trampoline layout (30 bytes):
            //   [0x00] 8B 05 <disp32>           mov eax, [rip+policy]
            //   [0x06] 85 C0                    test eax, eax
            //   [0x08] 74 13                    je +0x13   (-> ret at 0x1D)
            //   [0x0A] F0 FF 0D <disp32>        lock dec dword [rip+policy]
            //   [0x11] 4C 8B DC                 mov r11, rsp        (replicated)
            //   [0x14] 49 89 5B 10              mov [r11+0x10], rbx (replicated)
            //   [0x18] E9 <rel32>               jmp site9+7
            //   [0x1D] C3                       ret
            //
            // Why the je-then-dec ordering: we want a slot value of 0 to
            // mean "bail without modifying state" (frozen — no credits to
            // burn).  test+je on the original load short-circuits cleanly.
            // Any positive value falls through to the lock-dec, which
            // commits the consumption atomically on the same memory the
            // C++ side reads/writes via std::atomic_ref<int32_t>.
            //
            // Why bare RET is safe at this hook point: site9 is the very
            // top of LuxBattle_PerFrameTick, BEFORE its first prologue
            // instruction (mov r11, rsp).  RSP is exactly as the caller
            // passed it; the function is `void`; bare RET returns straight
            // back to the caller with the stack untouched.
            constexpr size_t kTrampSize = 30;
            void* tramp = CodeCave::allocate(kTrampSize);
            if (!tramp) return false;

            uint8_t buf[kTrampSize] = {0};
            size_t off = 0;

            // [0x00] mov eax, [rip+disp32_policy]
            buf[off++] = 0x8B;
            buf[off++] = 0x05;
            {
                const int64_t disp =
                      reinterpret_cast<int64_t>(m_policy)
                    - (reinterpret_cast<int64_t>(tramp) + off + 4);
                if (disp < INT32_MIN || disp > INT32_MAX) return false;
                const int32_t d32 = static_cast<int32_t>(disp);
                std::memcpy(&buf[off], &d32, sizeof(d32));
                off += 4;
            }

            // [0x06] test eax, eax
            buf[off++] = 0x85;
            buf[off++] = 0xC0;

            // [0x08] je +0x13 -> ret at offset 0x1D
            buf[off++] = 0x74;
            buf[off++] = 0x13;

            // [0x0A] lock dec dword [rip+disp32_policy]
            buf[off++] = 0xF0;
            buf[off++] = 0xFF;
            buf[off++] = 0x0D;
            {
                const int64_t disp =
                      reinterpret_cast<int64_t>(m_policy)
                    - (reinterpret_cast<int64_t>(tramp) + off + 4);
                if (disp < INT32_MIN || disp > INT32_MAX) return false;
                const int32_t d32 = static_cast<int32_t>(disp);
                std::memcpy(&buf[off], &d32, sizeof(d32));
                off += 4;
            }

            // [0x11] mov r11, rsp   (replicated original prologue byte 0..2)
            buf[off++] = 0x4C;
            buf[off++] = 0x8B;
            buf[off++] = 0xDC;

            // [0x14] mov [r11+0x10], rbx   (replicated original prologue byte 3..6)
            buf[off++] = 0x49;
            buf[off++] = 0x89;
            buf[off++] = 0x5B;
            buf[off++] = 0x10;

            // [0x18] jmp rel32 -> site9 + 7
            {
                uint8_t jmp_back[5];
                void* jmp_at      = static_cast<uint8_t*>(tramp) + off;
                void* back_target = static_cast<uint8_t*>(site9) + 7;
                if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                    return false;
                std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
                off += 5;
            }

            // [0x1D] ret
            buf[off++] = 0xC3;

            std::memcpy(tramp, buf, off);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);

            // Site patch: 5-byte JMP rel32 to tramp + 2 NOPs over the
            // displaced 7 bytes (mov r11,rsp + mov [r11+0x10], rbx).
            uint8_t patch_buf[7];
            uint8_t jmp[5];
            if (!encode_jmp_rel32(site9, tramp, jmp)) return false;
            std::memcpy(patch_buf, jmp, 5);
            patch_buf[5] = 0x90;
            patch_buf[6] = 0x90;
            if (!m_patch.prepare(site9, patch_buf, 7)) return false;

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.WorldTickGate] resolved (policy slot @ 0x{:x}, "
                    "tramp @ 0x{:x})\n"),
                reinterpret_cast<uintptr_t>(m_policy),
                reinterpret_cast<uintptr_t>(tramp));
            return true;
        }

        // Apply the Site 9 patch.  Resets policy to 0 (frozen) on enable
        // so a prior session's leftover step credits don't bleed through
        // and cause an unexpected world-tick on the first frame.
        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.WorldTickGate] enable() before resolve() — "
                        "ignoring\n"));
                return false;
            }
            if (m_enabled.load(std::memory_order_acquire)) return true;
            policy_store(0);
            if (!m_patch.enable())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.WorldTickGate] BytePatch.enable() failed\n"));
                return false;
            }
            m_enabled.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.WorldTickGate] enabled (policy=0 frozen)\n"));
            return true;
        }

        // Revert the Site 9 patch.  Resets policy to 0 on the way out so
        // any future readers of the slot (none, currently — but defensive)
        // see a sane "frozen" state rather than a stale step counter.
        void disable()
        {
            if (!m_enabled.load(std::memory_order_acquire)) return;
            m_patch.disable();
            policy_store(0);
            m_enabled.store(false, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.WorldTickGate] disabled\n"));
        }

        // Set policy = 0.  Every subsequent PerFrameTick call bails
        // (bare RET) until either step credits are added or the patch
        // is disabled.
        void set_frozen() noexcept { policy_store(0); }

        // Add `n` step credits.  Each PerFrameTick call atomically
        // decrements the slot by 1 and runs the displaced prologue.
        // After all credits are consumed the slot is 0 = frozen again.
        //
        // Composes correctly with concurrent F6 presses: fetch_add is
        // atomic on x86-64 aligned int32, so two presses interleaved
        // with a trampoline tick will end up with one credit consumed
        // and one credit pending (or both pending if neither tick fired
        // between them).  No credits are silently dropped at the C++
        // side; the only loss path is the inherent trampoline-vs-hotkey
        // race (see plate at top), which costs at most one credit per
        // collision.
        void add_step(int32_t n = 1) noexcept
        {
            if (n <= 0 || !m_policy) return;
            std::atomic_ref<int32_t>(*m_policy)
                .fetch_add(n, std::memory_order_acq_rel);
        }

        int32_t policy() const noexcept
        {
            if (!m_policy) return 0;
            return std::atomic_ref<int32_t>(*m_policy)
                .load(std::memory_order_acquire);
        }

        bool is_enabled()  const { return m_enabled.load(std::memory_order_acquire); }
        bool is_resolved() const { return m_resolved_ok; }

    private:
        void policy_store(int32_t v) noexcept
        {
            if (!m_policy) return;
            std::atomic_ref<int32_t>(*m_policy)
                .store(v, std::memory_order_release);
        }

        void policy_store_relaxed(int32_t v) noexcept
        {
            if (!m_policy) return;
            std::atomic_ref<int32_t>(*m_policy)
                .store(v, std::memory_order_relaxed);
        }

        BytePatch m_patch{};
        int32_t*  m_policy      = nullptr;
        bool      m_resolved    = false;
        bool      m_resolved_ok = false;
        std::atomic<bool> m_enabled{false};
    };

} // namespace Horse
