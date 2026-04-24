// ============================================================================
// !!! DEPRECATED — DO NOT INCLUDE !!!
//
// This helper targets bit 0 of `chara+0x394`, which empirical user
// testing (and a re-read of LuxBattleChara_SyncAudioActiveState_From
// BattleFlags) confirmed is the chara's AUDIO-PLAY gate, not a world-
// tick pause.  Toggling it mutes audio without freezing the chara
// state machine — characters keep moving.
//
// The CE script's "Game Pause" cheat the original port was based on
// has the same bug.
//
// The CORRECT pause mechanism is the master VM-freeze byte at
// 0x1448462D0 (g_LuxBattle_VMFreezeByte).  Setting it to non-zero
// makes LuxMoveVM_GetTimeDilationScalar return 0.0, which propagates
// into every per-frame integrator and halts the simulation.
//
// HorseMod's "Freeze frame" UI now drives Horse::SpeedControl with
// speedval=0 instead — same effect (SpeedControl's site-3 patch
// overrides the time-dilation getter directly), no race conditions,
// no chara-pointer-dangling caveats.
//
// This file is kept on disk for historical reference and in case the
// chara+0x394 trampoline machinery becomes useful for some other
// chara-state manipulation later.  Its disassembly walk and bit
// semantics are still correct — only the "this is how to pause"
// claim was wrong.
//
// ============================================================================
//
// Horse::GamePause — freeze the SC6 battle simulation on a single frame,
// with optional one-shot frame-step.
//
// Origin
// ------
// Ported from somberness's CE table ("SC6nepafu.CT", the "Game Pause"
// cheat).  The CE table targets two sites in the same per-frame
// pause-evaluation function:
//
//   Site A — "gamepause": 7-byte AOB
//     8B 93 94 03 00 00 8B
//   Matches at SoulcaliburVI.exe+5BECD81:
//     +5BECD81: 8B 93 94 03 00 00      mov edx,[rbx+0x394]
//   This is the engine's READ of the pause-bitfield byte at the
//   battle-context object's +0x394.  The function at +5BECD5B sets up
//   rbx = rcx = battle-world context object, then reads/tests the
//   pause field across the next dozen instructions to decide what
//   per-frame work to skip.  Bit 0 of the byte is "world simulation
//   paused".
//
//   Site B — "gpause2": 6-byte AOB
//     09 BB 94 03 00 00
//   Matches an `or [rbx+0x394], edi` (sets bits) at one location and
//   the AOB pattern is reused for a second site at +0x1A which is
//   `21 BB 94 03 00 00` = `and [rbx+0x394], edi` (clears bits).
//   Both writes are NOPed so the engine stops modifying the pause
//   bitfield on its own — only OUR writes to the byte stick.
//
// Why a trampoline (not just a NOP) at site A
// -------------------------------------------
// We need to reach `rbx` (the battle-context pointer) from C++ to
// write the pause byte ourselves.  Reflection-by-name doesn't help —
// the object's UE4SS class name is unclear and the field is at a raw
// offset, not a UProperty.  The cheapest source of `rbx` is the
// engine's own per-frame read-pause-flags function: by injecting a
// `mov [storage], rbx` immediately before the original instruction
// runs, every game tick refreshes our captured pointer for free.
//
// Trampoline layout (18 bytes, allocated from CodeCave):
//   +0x00  48 89 1D xx xx xx xx                ; mov [rip+disp32], rbx
//   +0x07  8B 93 94 03 00 00                   ; orig: mov edx,[rbx+0x394]
//   +0x0D  E9 xx xx xx xx                      ; jmp back to (site + 6)
//
// Site patch (6 bytes):
//   E9 xx xx xx xx 90                          ; jmp <tramp>; nop pad
//
// We need the 0x90 because the original instruction is 6 bytes long
// and our JMP is 5 — leaving the trailing byte unpatched would create
// a stray opcode the CPU might fetch if execution ever lands one byte
// late (it shouldn't, but better safe).
//
// Why not just call set_paused from a cockpit hook
// ------------------------------------------------
// Because the cockpit's UMG Update event still fires every frame even
// when the engine is paused (Slate widget tick is independent of
// world-tick gating).  That's a feature here — it's how the frame-
// step button can re-set the pause bit one frame after clearing it.
// If the cockpit stopped ticking during pause, we'd be stuck unable
// to ever re-pause from C++.
//
// Frame-step semantics
// --------------------
// step_n_frames(N) increments a pending-frames counter; on_tick()
// drains it one frame per two cockpit ticks:
//
//   step(1)  (counter=1, expecting=false)
//   tick A   counter=1, expecting=false  ->  clear bit, expecting=true
//                                            ^ world ticks during render
//   tick B   counter=1, expecting=true   ->  set bit, counter=0
//                                            ^ world doesn't tick anymore
//
// step(N) just queues N — pending frames drain one-by-one.  Calling
// step() while a previous step is in flight (counter > 0) appends to
// the queue rather than clobbering it, so a held hotkey at 60 Hz
// yields ~30 fps slow-motion (1 advanced frame per 2 cockpit ticks)
// and rapid taps don't get dropped.
//
// step() is legal from a running state — leaves the game paused
// after N frames advance.  set_paused(true/false) cancels any
// pending step queue first so explicit "Resume" isn't fought by an
// in-flight step that would re-pause on the next tick.
//
// Limits
// ------
//   * Captured rbx is the battle context.  After the user leaves the
//     battle (returns to menu) the pointer is dangling.  Writing to
//     it would crash.  We don't currently invalidate on battle-end —
//     don't pause-then-leave-match.  TODO: gate writes on "trampoline
//     captured rbx within the last N frames" by adding a tick-count
//     write next to the rbx storage.
//   * Bit 0 only.  The engine uses bits 1/2/3 for menu / cinematic /
//     network states; we leave those alone.  If you want a "freeze
//     EVERYTHING" mode, pause+ this might need to also set bit 1 etc.
//
// Threading: same as other patch helpers — toggle and step from the
// game thread (cockpit hook or ImGui callback).
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
    class GamePause
    {
    public:
        // Sig-scan + cave-allocate trampoline + prepare all 3 patches.
        // Idempotent.  Returns true iff every step succeeded.
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved = true;
            m_resolved_ok = false;

            void* siteA = sig_scan_sc6(
                "8B 93 94 03 00 00 8B", "GamePause siteA (read)");
            void* siteB = sig_scan_sc6(
                "09 BB 94 03 00 00", "GamePause siteB (or)");
            if (!siteA || !siteB) return false;

            // Allocate 8 bytes of storage + 18 bytes of trampoline from
            // the cave.  Storage first so the trampoline's RIP-relative
            // mov has a stable backing slot.  Both must live within
            // ±2 GB of siteA, which CodeCave guarantees.
            void* storage = CodeCave::allocate(sizeof(uintptr_t),
                                               alignof(uintptr_t));
            constexpr size_t kTrampSize = 7 + 6 + 5;
            void* tramp = CodeCave::allocate(kTrampSize);
            if (!storage || !tramp)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.GamePause] cave allocation failed\n"));
                return false;
            }
            // Initialize storage to 0 so has_captured() is false until
            // the trampoline actually runs once.
            *static_cast<uintptr_t*>(storage) = 0;

            // ----- Trampoline body -----
            // Build into a local buffer then memcpy in one shot.
            uint8_t buf[kTrampSize];

            // Bytes 0-6: mov [rip+disp32], rbx
            // Encoding: REX.W (48) + opcode (89) + ModR/M (1D — mod=00,
            // reg=rbx=011, r/m=101 = "[RIP+disp32]" in 64-bit mode) +
            // 32-bit displacement.
            //
            // disp32 is computed from the END of this instruction
            // (tramp+7) to the storage slot.  Both addresses are inside
            // the same cave page so the difference is tiny.
            buf[0] = 0x48;
            buf[1] = 0x89;
            buf[2] = 0x1D;
            const int64_t disp_to_storage =
                  reinterpret_cast<int64_t>(storage)
                - (reinterpret_cast<int64_t>(tramp) + 7);
            if (disp_to_storage < INT32_MIN || disp_to_storage > INT32_MAX)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.GamePause] storage too far from tramp "
                        "(disp 0x{:x}) — CodeCave layout broke?\n"),
                    static_cast<uint64_t>(disp_to_storage));
                return false;
            }
            const int32_t d32 = static_cast<int32_t>(disp_to_storage);
            std::memcpy(&buf[3], &d32, sizeof(d32));

            // Bytes 7-12: original instruction we displaced (mov edx,
            // [rbx+0x394]).
            const uint8_t orig[6] = { 0x8B, 0x93, 0x94, 0x03, 0x00, 0x00 };
            std::memcpy(&buf[7], orig, sizeof(orig));

            // Bytes 13-17: jmp back to (siteA + 6).
            uint8_t jmp_back[5];
            void* back_target = static_cast<uint8_t*>(siteA) + 6;
            void* jmp_back_at = static_cast<uint8_t*>(tramp) + 13;
            if (!encode_jmp_rel32(jmp_back_at, back_target, jmp_back))
                return false;
            std::memcpy(&buf[13], jmp_back, sizeof(jmp_back));

            std::memcpy(tramp, buf, kTrampSize);
            ::FlushInstructionCache(::GetCurrentProcess(), tramp, kTrampSize);

            // ----- Site A patch: 5-byte JMP + 1-byte NOP pad -----
            // Original is 6 bytes; we're stuffing in a 5-byte rel32 JMP
            // so we pad to keep the next aligned instruction reachable.
            uint8_t site_patch[6];
            uint8_t jmp_to_tramp[5];
            if (!encode_jmp_rel32(siteA, tramp, jmp_to_tramp))
                return false;
            std::memcpy(&site_patch[0], jmp_to_tramp, 5);
            site_patch[5] = 0x90;
            if (!m_jmp_patch.prepare(siteA, site_patch, sizeof(site_patch)))
                return false;

            // ----- Site B patches: NOP both engine-side writers -----
            // Original at siteB+0x00 = `09 BB 94 03 00 00` (or  [rbx+0x394], edi)
            // Original at siteB+0x1A = `21 BB 94 03 00 00` (and [rbx+0x394], edi)
            // Both are 6 bytes.  NOP each so only our writes touch the
            // pause bitfield.
            constexpr uint8_t kNop6[6] = {
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            };
            auto* siteB_bytes = static_cast<uint8_t*>(siteB);
            if (!m_nop1.prepare(siteB_bytes + 0x00, kNop6, sizeof(kNop6)))
                return false;
            if (!m_nop2.prepare(siteB_bytes + 0x1A, kNop6, sizeof(kNop6)))
                return false;

            m_storage    = static_cast<uintptr_t*>(storage);
            m_trampoline = tramp;
            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.GamePause] resolved: siteA=0x{:x} "
                    "siteB=0x{:x} tramp=0x{:x} storage=0x{:x}\n"),
                reinterpret_cast<uintptr_t>(siteA),
                reinterpret_cast<uintptr_t>(siteB),
                reinterpret_cast<uintptr_t>(tramp),
                reinterpret_cast<uintptr_t>(storage));
            return true;
        }

        // Install all 3 patches.  Rolls back partially-applied state
        // on failure for the same reason as CamLock.
        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.GamePause] enable() before successful "
                        "resolve() — ignoring\n"));
                return false;
            }
            if (m_enabled) return true;

            BytePatch* patches[] = { &m_jmp_patch, &m_nop1, &m_nop2 };
            for (size_t i = 0; i < 3; ++i)
            {
                if (!patches[i]->enable())
                {
                    for (size_t j = 0; j < i; ++j) patches[j]->disable();
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.GamePause] enable() failed at "
                            "patch {} — rolled back\n"), i);
                    return false;
                }
            }
            m_enabled = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.GamePause] machinery enabled (no pause yet)\n"));
            return true;
        }

        // Restore patches.  Also clears any pending step state since
        // executing a step on a torn-down rbx would crash.
        void disable()
        {
            if (!m_enabled) return;
            // Best-effort un-pause first so the user doesn't get stuck
            // on a frozen frame after toggling the machinery off.
            set_paused(false);
            m_jmp_patch.disable();
            m_nop1.disable();
            m_nop2.disable();
            m_step_pending.store(0);
            m_step_expecting.store(false);
            m_enabled = false;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.GamePause] machinery disabled\n"));
        }

        // ----------------------------------------------------------------
        // Pause / step API.  All of these are no-ops if the trampoline
        // hasn't run yet (no captured rbx).  Caller can poll
        // has_captured() before showing user-visible "pause active"
        // state.
        // ----------------------------------------------------------------

        // Explicit pause toggle.  Cancels any in-flight step state so
        // the user's "Resume" intent isn't fought by a half-finished
        // step that would re-pause on the next tick.
        void set_paused(bool paused)
        {
            // Drop pending steps before flipping the bit so on_tick()
            // doesn't see counter > 0 and undo us.
            m_step_pending.store(0);
            m_step_expecting.store(false);

            uint8_t* b = pause_byte();
            if (!b) return;
            // Bit 0 only.  Read-modify-write to preserve bits 1/2/3
            // (menu / cinematic / network state) that the engine still
            // owns even with our NOPs in place — those bits are written
            // from other code sites we don't patch.
            //
            // Non-atomic RMW is fine: only the game thread writes here,
            // and the engine's bit-0 writers at the gpause2 sites are
            // NOPed so nothing else races on this field.
            uint8_t v = *b;
            if (paused) v |=  0x01;
            else        v &= ~0x01;
            *b = v;
        }

        bool is_paused() const
        {
            const uint8_t* b = const_cast<GamePause*>(this)->pause_byte();
            return b && (*b & 0x01) != 0;
        }

        // ----------------------------------------------------------------
        // Frame-step API.  Counter-based (not 3-state) so rapid calls
        // queue up frames instead of getting dropped while a previous
        // step is still in flight:
        //
        //   step(1) at t=0   counter=1 → tick clears bit, expecting=true
        //   step(1) at t=0+ε counter=2 (caller's repeat-key fires)
        //   tick advances    set bit, counter=1, expecting=false
        //   tick advances    clear bit, expecting=true (restart cycle for 2nd frame)
        //   tick advances    set bit, counter=0, expecting=false
        //
        // Each "world ticked one frame" cycle = 2 cockpit ticks (clear
        // then set), so holding F6 yields ~30 fps slow-motion at SC6's
        // native 60 fps.  step(N) advances exactly N frames.
        //
        // No is_paused() gate — calling step() from a running state is
        // legal and has the side-effect of leaving the game paused
        // after N frames advance.  Useful for "pause on hit": call
        // step(0) is a no-op; call step(1) when something interesting
        // happens to advance one more frame and freeze.
        // ----------------------------------------------------------------
        void step_one_frame()      { step_n_frames(1); }
        void step_n_frames(int n)
        {
            if (n <= 0) return;
            m_step_pending.fetch_add(n);
        }

        // Called from cockpit hook each frame.  Advances at most one
        // bit-flip per call: clears the bit on odd cycles, sets it on
        // even cycles, decrementing the pending counter on each set.
        // Cheap when idle (single relaxed atomic load).
        void on_tick()
        {
            if (!m_enabled) return;
            if (m_step_pending.load() == 0) return;
            uint8_t* b = pause_byte();
            if (!b) return;

            if (m_step_expecting.load())
            {
                // Last tick we cleared the bit; world has run one frame.
                // Re-pause and decrement the queue.
                *b |= 0x01;
                m_step_pending.fetch_sub(1);
                m_step_expecting.store(false);
            }
            else
            {
                // Clear the bit so the next render frame ticks the
                // world simulation.
                *b &= ~0x01;
                m_step_expecting.store(true);
            }
        }

        // Has the engine run our trampoline at least once?  Implies
        // we're inside an active battle — outside battle the function
        // at +5BECD5B doesn't fire.
        bool has_captured() const
        {
            return m_storage && *m_storage != 0;
        }

        bool is_enabled()  const { return m_enabled; }
        bool is_resolved() const { return m_resolved_ok; }

        // For the ImGui status line: number of frames still queued
        // to advance, including the in-flight one.
        int  step_pending() const
        {
            return m_step_pending.load() + (m_step_expecting.load() ? 1 : 0);
        }
        bool step_in_flight() const { return step_pending() > 0; }

    private:
        uint8_t* pause_byte()
        {
            if (!m_storage) return nullptr;
            const uintptr_t rbx = *m_storage;  // race-free: same thread writes
            if (rbx == 0) return nullptr;
            return reinterpret_cast<uint8_t*>(rbx + 0x394);
        }

        BytePatch m_jmp_patch{};   // site A: trampoline JMP
        BytePatch m_nop1{};        // site B+0x00: NOP the OR
        BytePatch m_nop2{};        // site B+0x1A: NOP the AND
        void*     m_trampoline   = nullptr;
        uintptr_t* m_storage     = nullptr;  // points into CodeCave page

        // Number of frames the user has requested to advance but haven't
        // been delivered yet.  on_tick() decrements this once per
        // delivered frame.
        std::atomic<int>  m_step_pending{0};
        // True between "we cleared the bit" and "we set it back".  The
        // engine ticks the world during this window.
        std::atomic<bool> m_step_expecting{false};

        bool m_resolved    = false;
        bool m_resolved_ok = false;
        bool m_enabled     = false;
    };

} // namespace Horse
