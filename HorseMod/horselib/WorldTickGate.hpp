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
//   > 0  : step credits remaining — PerFrameTick (and sibling gates)
//          run.  Caller drains the count by calling
//          consume_one_credit() once per OBSERVED PerFrameTick run;
//          the trampoline itself does NOT modify the slot.
//   < 0  : unused (treated as "always run" — defensive equivalent of
//          Native for any path that accidentally writes negative).
//
// "Native" mode is achieved by DISABLING the BytePatch (no hook).  The
// engine's original PerFrameTick prologue runs unconditionally, no
// policy slot consulted.
//
// Why C++ owns the decrement
// --------------------------
// Earlier the trampoline did `lock dec` on entry.  Cleaner-looking but
// it created an off-by-one bug whenever a sibling gate (e.g.
// ActorTickGate Site 11) fired LATER in the same UE4 tick frame than
// PerFrameTick: on the last credit of a burst, Site 9 would dec policy
// 1→0 and run, then the sibling would see 0 and bail.  Net visible
// effect: 10-step request advanced 9 replay frames.  Moving the
// decrement to the post-tick hook makes every gate observe the same
// slot value for the entire UE4 frame, so the credit count drains in
// lockstep with frames advanced.
//
// Race notes
// ----------
// All hot-path access is on the game thread.  The F6 hotkey runs on a
// UE4SS keyboard thread; add_step uses atomic fetch_add and
// consume_one_post_tick uses CAS, so a hotkey press concurrent with a
// post-tick decrement composes correctly — both contribute their full
// effect.
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

            // Trampoline layout (23 bytes):
            //   [0x00] 8B 05 <disp32>           mov eax, [rip+policy]
            //   [0x06] 85 C0                    test eax, eax
            //   [0x08] 74 0C                    je +0x0C   (-> ret at 0x16)
            //   [0x0A] 4C 8B DC                 mov r11, rsp        (replicated)
            //   [0x0D] 49 89 5B 10              mov [r11+0x10], rbx (replicated)
            //   [0x11] E9 <rel32>               jmp site9+7
            //   [0x16] C3                       ret
            //
            // The trampoline ONLY checks the policy slot — it does NOT
            // decrement.  The decrement is owned by C++-side code that
            // calls consume_one_post_tick() once per UE4 frame from the
            // cockpit post-hook.  This guarantees every sibling gate
            // (ActorTickGate's Site 11/20/21/21b/22/22b/22c) reads the
            // SAME policy value for the entire UE4 frame, regardless of
            // tick order.  Earlier the trampoline did `lock dec` on
            // entry, which had a subtle off-by-one: on the LAST tick of
            // an N-step burst, Site 9 would dec policy 1→0 and run, but
            // any sibling gate firing later in the same UE4 frame would
            // see policy=0 and bail.  ActorTickGate Site 11 (chara
            // replay-frame advance / vtable[0x6C8]) bailing on the last
            // tick meant the per-chara replay state writer didn't fire,
            // so MoveVM advanced its frame counter using the previous
            // step's stale buffered input — net visible effect: 10
            // step credits produced 9 forward replay frames.
            //
            // Why bare RET is safe at this hook point: site9 is the very
            // top of LuxBattle_PerFrameTick, BEFORE its first prologue
            // instruction (mov r11, rsp).  RSP is exactly as the caller
            // passed it; the function is `void`; bare RET returns straight
            // back to the caller with the stack untouched.
            constexpr size_t kTrampSize = 23;
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

            // [0x08] je +0x0C -> ret at offset 0x16
            buf[off++] = 0x74;
            buf[off++] = 0x0C;

            // [0x0A] mov r11, rsp   (replicated original prologue byte 0..2)
            buf[off++] = 0x4C;
            buf[off++] = 0x8B;
            buf[off++] = 0xDC;

            // [0x0D] mov [r11+0x10], rbx   (replicated original prologue byte 3..6)
            buf[off++] = 0x49;
            buf[off++] = 0x89;
            buf[off++] = 0x5B;
            buf[off++] = 0x10;

            // [0x11] jmp rel32 -> site9 + 7
            {
                uint8_t jmp_back[5];
                void* jmp_at      = static_cast<uint8_t*>(tramp) + off;
                void* back_target = static_cast<uint8_t*>(site9) + 7;
                if (!encode_jmp_rel32(jmp_at, back_target, jmp_back))
                    return false;
                std::memcpy(&buf[off], jmp_back, sizeof(jmp_back));
                off += 5;
            }

            // [0x16] ret
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

        // Add `n` step credits.  While the slot holds a positive value,
        // every PerFrameTick call (and every sibling gate) sees policy>0
        // and runs.  The slot is decremented once per UE4 frame by
        // consume_one_post_tick() called from a cockpit-post hook, so
        // a credit count of N drains over exactly N UE4 frames.
        //
        // Composes correctly with concurrent F6 presses: fetch_add is
        // atomic on x86-64 aligned int32, so two presses arriving on
        // different ticks just stack up in the slot.  No credits are
        // silently dropped on the C++ side.
        void add_step(int32_t n = 1) noexcept
        {
            if (n <= 0 || !m_policy) return;
            std::atomic_ref<int32_t>(*m_policy)
                .fetch_add(n, std::memory_order_acq_rel);
        }

        // Decrement the policy slot by one IFF positive.  The caller
        // should invoke this exactly once per game-frame OBSERVED to
        // have ticked — typically by watching for a change in
        // g_LuxBattle_FrameCounter (incremented at the end of
        // LuxBattle_PerFrameTick) since the last cockpit pre-hook.
        // Tying drain to that counter (rather than cockpit hook
        // timing) keeps the credit count perfectly aligned with
        // PerFrameTick runs regardless of UE4 actor tick ordering.
        //
        // No-op when policy is already 0 (steady-state freeze) or when
        // the gate hasn't been resolved yet.  Atomic w.r.t. add_step()
        // and the trampoline's load.
        void consume_one_credit() noexcept
        {
            if (!m_policy) return;
            std::atomic_ref<int32_t> ref(*m_policy);
            // Compare-exchange loop so we never wrap below 0 if a
            // concurrent set_frozen() lands between the load and the
            // store.
            int32_t cur = ref.load(std::memory_order_acquire);
            while (cur > 0)
            {
                if (ref.compare_exchange_weak(cur, cur - 1,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                    return;
                // cur was reloaded by compare_exchange_weak on failure;
                // loop until either cur <= 0 or the CAS succeeds.
            }
        }

        int32_t policy() const noexcept
        {
            if (!m_policy) return 0;
            return std::atomic_ref<int32_t>(*m_policy)
                .load(std::memory_order_acquire);
        }

        // Raw pointer to the cave-resident int32_t policy slot.  Exposed
        // so sibling gates (e.g. ReplayClockGate) can build trampolines
        // that read the same atomic without owning their own slot — they
        // observe the freeze/step state set here and gate themselves
        // accordingly.  Returns nullptr until resolve() succeeds.
        //
        // Sibling gates should ONLY READ the slot, never decrement —
        // step-credit consumption is the sole responsibility of this gate
        // (sites that share-decrement risk consuming credits faster than
        // intended when the same UE4 frame fires multiple gated paths).
        int32_t* policy_slot_address() const noexcept { return m_policy; }

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
