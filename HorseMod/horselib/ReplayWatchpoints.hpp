// ============================================================================
// Horse::ReplayWatchpoints — diagnostic logger for replay-state writes.
//
// Why this exists
// ---------------
// After 16 freeze patches (sites 1–16), HorseMod's freeze still leaks during
// replay watching: with the in-replay input display enabled, the inputs
// shown differ between (freeze enabled) and (freeze disabled) cycles.
// Three parallel static-analysis investigations failed to identify the
// remaining writer with confidence.  The path forward is empirical:
// observe what writes the cursor fields during a freeze cycle.
//
// This helper installs **hardware data breakpoints** (DR0..DR3) on up to
// four addresses simultaneously and logs every write, with the writing
// instruction's RIP, into a lock-free ring buffer that the ImGui debug
// tab can display.  Hardware breakpoints are CPU-supported, single-byte/
// word-granularity, and zero-overhead when not triggered — perfect for
// a "what just wrote this address" probe.
//
// Watched addresses (all on ALuxBattleFrameInputLog at BM+0x478):
//   +0x39C : nPlaybackCursor   (UI-visible cursor)
//   +0x3A4 : nMasterClock      (the cursor sites 14/15 patch; INC'd once/frame)
//   +0x4410: nDrainCursorL2    (this is on the CHARA, not FrameInputLog —
//                               but we resolve it relative to the chara)
//   BM+0x148C: cursor mirror   (set by SimulationLoop, sites 13)
//
// We resolve target addresses lazily — at enable() time — by reading the
// global g_LuxBattle_CharaSlotP1 pointer (RVA 0x470DE90), walking through
// the chara's UE4 component (chara+0x388) into the BattleManager via
// LuxResolveBattleManagerFromComponent (0x14045FDC0), then dereferencing
// BM+0x478 for the FrameInputLog actor.  If any of these are null (no
// active battle), enable() refuses and the user is told to start a
// replay first.
//
// Mechanism
// ---------
// 1. AddVectoredExceptionHandler(first=1, &Veh) — installs a process-wide
//    VEH at the front of the chain.  Fires before SEH unwind, before the
//    debugger gets the exception.
//
// 2. SetThreadContext on the game thread — sets DR0..DR3 to the watched
//    addresses, sets DR7 control bits to enable each watchpoint as a
//    "data write" (RW=01) breakpoint with length matching field size
//    (LEN=11 for 4-byte int).
//
// 3. When a watched address is written, the CPU triggers
//    EXCEPTION_SINGLE_STEP at the END of the offending instruction.
//    Our VEH:
//      a. Confirms ExceptionRecord->ExceptionInformation indicates a
//         hardware data breakpoint hit (DR6 BD/B0..B3 bits in
//         ExceptionRecord->ExceptionAddress is the FAULTING instruction).
//      b. Captures: faulting RIP, the watched address, the new value,
//         current speedval (read via SpeedControl::current_value_static).
//      c. Pushes a log entry onto a lock-free ring buffer.
//      d. Sets the resume-flag (EFlags |= RF) in the ContextRecord so
//         the same instruction doesn't re-fire on resume.
//      e. Returns EXCEPTION_CONTINUE_EXECUTION.
//
// 4. disable() clears DR0..DR3 + DR7, removes the VEH.
//
// Threading
// ---------
// The DR registers are PER-THREAD.  We must SetThreadContext on the GAME
// thread (where simulation writes happen).  Any writes from OTHER threads
// (audio, render) are NOT caught — but those are unlikely sources for
// the leak we're hunting.  The cockpit hook gives us a callback ON the
// game thread, so we install/uninstall from there.
//
// The ring buffer is a fixed-size circular buffer protected by an
// std::atomic<uint32_t> head index.  Single-producer (the VEH on the
// game thread) and single-consumer (the ImGui render thread reading for
// display).  Lock-free; never blocks.
//
// Limits / caveats
// ----------------
// * Max 4 watchpoints simultaneously (DR0..DR3 hardware limit).
// * On Windows x64, intercepting hardware breakpoints requires no special
//   privileges.  The CPU enforces them.
// * If another component (debugger, anti-cheat) is using DR registers,
//   ours will be overwritten on the next SetThreadContext.  Test in a
//   clean environment.
// * Single-step (EXCEPTION_SINGLE_STEP) is also raised by INT 1 (TRAP
//   flag set).  We distinguish hardware-breakpoint hits by reading DR6
//   from the context — only B0..B3 bits indicate our watchpoints.
// * The ring buffer is fixed-size (256 entries).  Sustained-write
//   scenarios will lose the oldest entries.  For the use case ("what
//   wrote during a 5-second freeze"), 256 is plenty.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "SafeMemoryRead.hpp"
#include "SpeedControl.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>

namespace Horse
{
    class ReplayWatchpoints
    {
    public:
        // Single log entry — captured by the VEH, displayed by ImGui.
        struct Entry
        {
            uint64_t timestamp_qpc;   // QueryPerformanceCounter at hit
            uint64_t writer_rip;      // address of the instruction that wrote
            uint64_t watched_addr;    // which of the 4 addresses fired
            uint32_t value_after;     // value at watched_addr after the write
            float    speedval_at_hit; // current speedval (0 = HorseMod frozen)
        };

        // Resolve target addresses, register VEH, enable DR registers.
        // The caller MUST supply the live BattleManager actor pointer —
        // we don't try to resolve it ourselves because the chara at
        // g_LuxBattle_CharaSlotP1 is a data-only struct (its embedded
        // UE4 sub-object at +0x388 is uninitialized, so the canonical
        // chara→ResolveBM(chara+0x388) path returns null).  Caller
        // typically uses Horse::Lux::battleManager().raw() for this.
        //
        // Returns false if bm is null, target resolution fails, or VEH
        // registration fails.  Idempotent — second enable() while
        // already-enabled is a no-op.
        bool enable(void* bm)
        {
            if (m_enabled.load(std::memory_order_acquire)) return true;

            if (!resolve_targets(bm))
            {
                // resolve_targets() logs which specific step failed.  Don't
                // duplicate the message here.
                return false;
            }

            // Install VEH at the front of the chain (first=1) so we see the
            // exception before any other handler.  The VEH stays registered
            // for the LIFETIME of the process — we never RemoveVectored-
            // ExceptionHandler.  When m_enabled is false, the VEH callback
            // returns EXCEPTION_CONTINUE_SEARCH immediately (cheap no-op).
            // This avoids a race where disable() removed the VEH while the
            // cockpit tick had just re-armed DRs and a breakpoint was about
            // to fire — without a handler the exception goes unhandled and
            // crashes the game.
            if (!m_veh_handle)
            {
                m_veh_handle = ::AddVectoredExceptionHandler(
                    1, &ReplayWatchpoints::Veh);
                if (!m_veh_handle)
                {
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.ReplayWatchpoints] AddVectoredExceptionHandler "
                            "failed (GLE={})\n"),
                        static_cast<uint64_t>(::GetLastError()));
                    return false;
                }
            }

            // Set DR0..DR3 + DR7 on the current thread.  If this thread
            // isn't the one that'll execute the watched writes, tick()
            // (called from the cockpit pre-hook on the game thread) will
            // re-apply on the next frame — so this can fail benignly here
            // and still work end-to-end.
            (void)set_dr_registers(m_targets[0], m_targets[1],
                                   m_targets[2], m_targets[3]);

            // Publish self-pointer to the VEH so it can find our ring
            // buffer.  Set BEFORE m_enabled so the VEH never sees enabled
            // with a stale s_active.
            s_active = this;
            m_enabled.store(true, std::memory_order_release);

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ReplayWatchpoints] enabled — watching {:x} {:x} {:x} {:x}\n"),
                m_targets[0], m_targets[1], m_targets[2], m_targets[3]);
            return true;
        }

        // Disable the watchpoints.  Safe from any thread.
        //
        // Sequence (the order matters for crash-safety):
        //   1. Mark m_enabled = false (release).  Future tick() and Veh()
        //      calls will short-circuit immediately.
        //   2. Clear DR0..DR3 on the current thread.  If the OTHER thread
        //      had its DRs set, tick() (which re-arms every frame) will
        //      now write zeros there too.
        //   3. Leave the VEH registered (lifetime-of-process).  Removing
        //      it would race against in-flight exceptions; leaving it as
        //      a no-op handler costs ~10 ns per UNRELATED exception (rare
        //      in normal play) and zero when no exception fires.
        //   4. Leave s_active set so any in-flight Veh() call that already
        //      passed the m_enabled check can still complete safely.
        //      (Next enable() will overwrite it; matching the no-op VEH
        //      pattern, this is a non-issue.)
        void disable()
        {
            if (!m_enabled.load(std::memory_order_acquire)) return;
            // ORDER: m_enabled false FIRST, so tick()/Veh() see it before
            // we touch anything else.
            m_enabled.store(false, std::memory_order_release);
            // Then clear DR registers on the current thread.  The cockpit
            // tick will re-clear them on its next firing (since m_enabled
            // is now false, tick() is a no-op — no re-arming).
            (void)set_dr_registers(0, 0, 0, 0);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ReplayWatchpoints] disabled\n"));
        }

        bool is_enabled() const
        {
            return m_enabled.load(std::memory_order_acquire);
        }

        // Returns the four watched addresses (or 0s if not resolved).
        // For ImGui display.
        const std::array<uintptr_t, 4>& targets() const { return m_targets; }

        // Snapshot the latest N entries (newest first) for ImGui display.
        // Returns the number actually copied (may be less than max if the
        // ring hasn't filled yet).  Single-consumer expected (the ImGui
        // render thread).
        size_t snapshot_recent(Entry* out, size_t max) const
        {
            const uint32_t head = m_head.load(std::memory_order_acquire);
            const size_t   filled = m_filled.load(std::memory_order_acquire);
            const size_t   count  = (filled < max) ? filled : max;
            for (size_t i = 0; i < count; ++i)
            {
                // head points at next-to-write; entry (head-1) is newest.
                const uint32_t idx = (head + kRingMask - i) & kRingMask;
                out[i] = m_ring[idx];
            }
            return count;
        }

        // Reset the ring buffer (clear log without re-arming).
        void clear_log()
        {
            m_head.store(0, std::memory_order_release);
            m_filled.store(0, std::memory_order_release);
            m_drain_cursor.store(0, std::memory_order_release);
        }

        // Per-frame game-thread maintenance.  Called from the cockpit
        // pre-hook (which fires once per game tick on the same thread that
        // executes the cursor writes).  Two jobs:
        //
        //   (1) Re-arm DR0..DR3 + DR7.  The CPU's debug registers are
        //       per-thread and SOMETIMES get cleared by the OS or other
        //       components (anti-cheat, debugger).  Re-applying them every
        //       frame guarantees the breakpoints survive context switches.
        //   (2) Drain the ring buffer to UE4SS.log.  Done here (not from
        //       the ImGui tab) so the log fills even when the menu is
        //       closed or on a different tab.
        //
        // Both are no-ops if !m_enabled.  Idempotent — set_dr_registers
        // just writes the same values again if nothing changed.
        void tick()
        {
            if (!m_enabled.load(std::memory_order_acquire)) return;
            (void)set_dr_registers(m_targets[0], m_targets[1],
                                   m_targets[2], m_targets[3]);
            drain_to_log();
        }

        // Drain new ring entries to UE4SS.log.  MUST be called from the
        // game-thread (cockpit hook or ImGui callback), NOT from inside
        // the VEH — RC::Output does fmt + locks + file I/O which can
        // deadlock against locks the game thread already holds at fault
        // time.
        //
        // Single-consumer expected.  Tracks position via m_drain_cursor;
        // each call emits exactly the entries pushed since the last call.
        // If the producer has lapped us (more than kRingSize entries since
        // last drain), logs an OVERFLOW notice and skips ahead.
        void drain_to_log()
        {
            const uint32_t cur_head = m_head.load(std::memory_order_acquire);
            uint32_t       cur      = m_drain_cursor.load(std::memory_order_relaxed);
            if (cur == cur_head) return;
            const uint32_t lag = cur_head - cur;  // wraps correctly
            if (lag > kRingSize)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayWatchpoints] OVERFLOW: lost {} entries "
                        "(producer lapped consumer)\n"),
                    static_cast<uint64_t>(lag - kRingSize));
                cur = cur_head - kRingSize;
            }
            const uintptr_t base = NativeBinding::imageBase();
            while (cur != cur_head)
            {
                const Entry& e = m_ring[cur & kRingMask];
                int slot = -1;
                for (int i = 0; i < 4; ++i)
                    if (m_targets[i] == e.watched_addr) { slot = i; break; }
                const int64_t rva = base
                    ? static_cast<int64_t>(e.writer_rip)
                          - static_cast<int64_t>(base)
                    : -1;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[Horse.ReplayWatchpoints] hit slot={} addr=0x{:x} "
                        "val=0x{:x} speedval={:.3f} rip=0x{:x} rva=0x{:x}\n"),
                    slot,
                    static_cast<uint64_t>(e.watched_addr),
                    static_cast<uint64_t>(e.value_after),
                    e.speedval_at_hit,
                    static_cast<uint64_t>(e.writer_rip),
                    static_cast<uint64_t>(rva));
                ++cur;
            }
            m_drain_cursor.store(cur_head, std::memory_order_release);
        }

    private:
        static constexpr size_t kRingSize = 256;          // power of 2
        static constexpr uint32_t kRingMask = kRingSize - 1;

        std::array<Entry, kRingSize> m_ring{};
        std::atomic<uint32_t>        m_head{0};         // next write index (producer)
        std::atomic<size_t>          m_filled{0};       // total entries pushed (capped at kRingSize)
        std::atomic<uint32_t>        m_drain_cursor{0}; // next index to drain to log (consumer)

        std::array<uintptr_t, 4> m_targets{0, 0, 0, 0};
        PVOID                    m_veh_handle = nullptr;
        // Atomic so disable() (potentially on the ImGui input thread) can
        // make the disabled state visible to the cockpit tick (game thread)
        // and to the VEH (whichever thread triggers the breakpoint) without
        // a lock.  Plain-bool reads/writes from multiple threads = TOCTOU
        // race and was the cause of a use-after-free crash in the previous
        // build (cockpit tick re-armed DRs after disable() cleared them,
        // then disable() removed the VEH, and the next breakpoint landed
        // with no handler).
        std::atomic<bool>        m_enabled{false};

        // Single-instance pointer for the VEH callback to find us.
        // The VEH is a free function (Windows API), can't capture `this`,
        // so we use a static pointer.  Only one ReplayWatchpoints exists
        // in the process so this is safe.
        static inline ReplayWatchpoints* s_active = nullptr;

        // Resolve the four target addresses given a BattleManager actor.
        // Returns true if all four resolved to non-null/sane values.
        // On failure, logs which step failed with the relevant pointer
        // values so we can debug post-mortem from UE4SS.log.
        bool resolve_targets(void* bm)
        {
            // Step 1: image base (used for the chara slot read below).
            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayWatchpoints] resolve step 1 FAILED: "
                        "NativeBinding::imageBase() returned 0\n"));
                return false;
            }

            // Step 2: caller-supplied BM pointer must be non-null.
            if (!bm)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayWatchpoints] resolve step 2 FAILED: "
                        "caller passed bm=null (Horse::Lux::battleManager() "
                        "found no LuxBattleManager — start a replay first)\n"));
                return false;
            }

            // Step 3: read g_LuxBattle_CharaSlotP1 (RVA 0x470DE90) for the
            // chara — used ONLY for the chara+0x4410 target.  KHitWalker
            // uses this same global and reads chara fields successfully
            // (e.g. chara+0x44058), so the offset 0x4410 should be safe
            // even if the chara's embedded UE4 sub-object at +0x388
            // happens to be uninitialized.
            void* chara = nullptr;
            const uintptr_t slot_addr = base + 0x470DE90;
            if (!SafeReadPtr(reinterpret_cast<const void*>(slot_addr), &chara))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayWatchpoints] resolve step 3 FAILED: "
                        "SafeReadPtr at 0x{:x} faulted\n"),
                    static_cast<uint64_t>(slot_addr));
                return false;
            }
            if (!chara)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayWatchpoints] resolve step 3 FAILED: "
                        "g_LuxBattle_CharaSlotP1 @ 0x{:x} is null\n"),
                    static_cast<uint64_t>(slot_addr));
                return false;
            }

            // Step 4: BM+0x478 → ALuxBattleFrameInputLog actor.
            void* frame_input_log = nullptr;
            if (!SafeReadPtr(reinterpret_cast<uint8_t*>(bm) + 0x478,
                             &frame_input_log))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayWatchpoints] resolve step 4 FAILED: "
                        "BM+0x478 read faulted (bm=0x{:x})\n"),
                    reinterpret_cast<uint64_t>(bm));
                return false;
            }
            if (!frame_input_log)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.ReplayWatchpoints] resolve step 4 FAILED: "
                        "ALuxBattleFrameInputLog ptr at BM+0x478 is null "
                        "(bm=0x{:x}) — replay actor not yet spawned\n"),
                    reinterpret_cast<uint64_t>(bm));
                return false;
            }

            // Compute the four target addresses.
            auto* il = reinterpret_cast<uint8_t*>(frame_input_log);
            auto* bm_b = reinterpret_cast<uint8_t*>(bm);
            auto* ch_b = reinterpret_cast<uint8_t*>(chara);

            // FOURTH-WAVE WATCH SET — RECORDED INPUT BUFFER CONTENTS.
            // (Reverted from set 5 after a startup-crash report; this set
            //  is the previously-tested-stable configuration.)
            //
            // Three prior watch sets (cursors, BM mirrors, RNG/live-input)
            // ALL confirmed frozen correctly during the freeze window.
            // User still reports drift, so the drift must be in fields
            // none of the prior sets covered.  The next strongest
            // candidate: the recorded-input-frame BUFFER itself at
            // *(FrameInputLog+0x3A8).  If something WRITES into the
            // recorded-frame buffer during freeze (e.g. live input being
            // recorded over recorded input, or a buffer-shuffling pass),
            // then on unfreeze the displayed inputs at the same cursor
            // would be different from a non-frozen viewing.
            //
            // Buffer layout: 0xC0-byte entries, indexed by cursor.  We
            // pick a few entry indices to spot-check.  Indices outside
            // the recorded range will fault on read but our SafeRead
            // wrappers handle that.
            //
            //   slot 0: buffer[0]   (first recorded frame)
            //   slot 1: buffer[100] (frame 100)
            //   slot 2: buffer[500] (frame 500)
            //   slot 3: chara+0x95710 — MoveVM-related ptr/counter
            //                            (visible as written in PerFrameTick)
            //
            // If buffer entries get written during freeze, that's the
            // leak.  If chara+0x95710 changes during freeze, MoveVM is
            // partially running.
            void* p_buffer = nullptr;
            if (!SafeReadPtr(reinterpret_cast<uint8_t*>(frame_input_log) + 0x3A8,
                             &p_buffer))
                p_buffer = nullptr;
            if (p_buffer)
            {
                auto* buf = reinterpret_cast<uint8_t*>(p_buffer);
                m_targets[0] = reinterpret_cast<uintptr_t>(buf + 0 * 0xC0);
                m_targets[1] = reinterpret_cast<uintptr_t>(buf + 100 * 0xC0);
                m_targets[2] = reinterpret_cast<uintptr_t>(buf + 500 * 0xC0);
            }
            else
            {
                // Buffer not allocated yet (no replay loaded?) — fall back
                // to FrameInputLog cursors so the watchpoint at least arms.
                m_targets[0] = reinterpret_cast<uintptr_t>(il + 0x39C);
                m_targets[1] = reinterpret_cast<uintptr_t>(il + 0x3A4);
                m_targets[2] = reinterpret_cast<uintptr_t>(il + 0x4410);
            }
            m_targets[3] = reinterpret_cast<uintptr_t>(ch_b + 0x95710); // chara MoveVM field
            (void)bm_b;

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.ReplayWatchpoints] resolved: chara=0x{:x} bm=0x{:x} "
                    "fil=0x{:x}; targets=[0x{:x} 0x{:x} 0x{:x} 0x{:x}]\n"),
                reinterpret_cast<uint64_t>(chara),
                reinterpret_cast<uint64_t>(bm),
                reinterpret_cast<uint64_t>(frame_input_log),
                static_cast<uint64_t>(m_targets[0]),
                static_cast<uint64_t>(m_targets[1]),
                static_cast<uint64_t>(m_targets[2]),
                static_cast<uint64_t>(m_targets[3]));

            return true;
        }

        // Set DR0..DR3 to the four addresses; configure DR7 for write-only,
        // 4-byte length, all four enabled (local).  Pass 0 for an address
        // to disable that slot.
        bool set_dr_registers(uintptr_t a0, uintptr_t a1,
                              uintptr_t a2, uintptr_t a3)
        {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            HANDLE hThread = ::GetCurrentThread();

            if (!::GetThreadContext(hThread, &ctx))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ReplayWatchpoints] GetThreadContext failed "
                        "(GLE={})\n"),
                    static_cast<uint64_t>(::GetLastError()));
                return false;
            }

            ctx.Dr0 = a0;
            ctx.Dr1 = a1;
            ctx.Dr2 = a2;
            ctx.Dr3 = a3;

            // DR7 layout (low-word relevant bits):
            //   bit 0: L0 enable
            //   bit 2: L1 enable
            //   bit 4: L2 enable
            //   bit 6: L3 enable
            // Per-watchpoint condition (DR7 bits 16+4*N..17+4*N):
            //   00 = execute, 01 = write, 11 = read+write
            // Per-watchpoint length (DR7 bits 18+4*N..19+4*N):
            //   00 = 1B, 01 = 2B, 10 = 8B, 11 = 4B
            //
            // We want write-only, 4-byte for all four:
            //   condition = 01 (write)
            //   length    = 11 (4 bytes)
            // Per slot, the 4-bit nibble at bits (16+4*N) is 1101 (0xD).
            // So DR7 = 0xDDDD0055 = enable L0..L3 + 0xD per slot.
            //
            // Disable any slot whose address is 0:
            uint64_t dr7 = 0;
            if (a0) { dr7 |= 0x1ULL << 0;  dr7 |= 0xDULL << 16; }
            if (a1) { dr7 |= 0x1ULL << 2;  dr7 |= 0xDULL << 20; }
            if (a2) { dr7 |= 0x1ULL << 4;  dr7 |= 0xDULL << 24; }
            if (a3) { dr7 |= 0x1ULL << 6;  dr7 |= 0xDULL << 28; }
            ctx.Dr7 = dr7;

            // DR6 reset isn't strictly necessary (CPU sets it; we only read)
            // but clearing pre-existing flags avoids false hits.
            ctx.Dr6 = 0;

            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (!::SetThreadContext(hThread, &ctx))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.ReplayWatchpoints] SetThreadContext failed "
                        "(GLE={})\n"),
                    static_cast<uint64_t>(::GetLastError()));
                return false;
            }
            return true;
        }

        // Append an entry to the ring (single-producer assumed — the VEH
        // runs on the thread that took the exception, which is always
        // the game thread for our hardware breakpoints).
        void push_entry(const Entry& e)
        {
            const uint32_t idx = m_head.load(std::memory_order_relaxed) & kRingMask;
            m_ring[idx] = e;
            m_head.store(m_head.load(std::memory_order_relaxed) + 1,
                         std::memory_order_release);
            const size_t prev = m_filled.load(std::memory_order_relaxed);
            if (prev < kRingSize)
                m_filled.store(prev + 1, std::memory_order_release);
        }

        // The vectored exception handler.  Free function (Windows API).
        // Filters to data-write hardware breakpoints only.  Logs and
        // continues.
        static LONG CALLBACK Veh(PEXCEPTION_POINTERS ep)
        {
            // Only handle single-step / hardware-breakpoint exceptions.
            if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
                return EXCEPTION_CONTINUE_SEARCH;

            // Read DR6 from the context to figure out WHICH watchpoint hit.
            // Bits B0..B3 (low 4 bits) are set per slot that triggered.
            const uint64_t dr6 = ep->ContextRecord->Dr6;
            const uint64_t hit_bits = dr6 & 0xFULL;
            if (hit_bits == 0)
                return EXCEPTION_CONTINUE_SEARCH;  // not our breakpoint

            ReplayWatchpoints* self = s_active;
            if (!self)
                return EXCEPTION_CONTINUE_SEARCH;  // never armed
            if (!self->m_enabled.load(std::memory_order_acquire))
                return EXCEPTION_CONTINUE_SEARCH;  // disabled — drop the hit

            // Determine which slot fired (lowest set bit wins if multiple).
            int slot = -1;
            for (int i = 0; i < 4; ++i)
                if (hit_bits & (1ULL << i)) { slot = i; break; }
            if (slot < 0 || self->m_targets[slot] == 0)
                return EXCEPTION_CONTINUE_SEARCH;

            // Capture a log entry.
            Entry e{};
            LARGE_INTEGER qpc{};
            ::QueryPerformanceCounter(&qpc);
            e.timestamp_qpc  = static_cast<uint64_t>(qpc.QuadPart);
            // ExceptionAddress is the address of the instruction that
            // FAULTED.  For data breakpoints, the CPU raises the
            // exception AFTER the offending instruction completes —
            // so RIP in the context points to the NEXT instruction.
            // The faulting instruction is at RIP-(insn_len), but we
            // log RIP as-is and let the user disassemble around it.
            e.writer_rip   = ep->ExceptionRecord->ExceptionAddress
                              ? reinterpret_cast<uint64_t>(ep->ExceptionRecord->ExceptionAddress)
                              : ep->ContextRecord->Rip;
            e.watched_addr = self->m_targets[slot];

            // Read the value AFTER the write (the new value).  Safe-wrap
            // because we're inside an exception handler — a fault here
            // would be VERY bad, so use SafeRead.
            uint32_t v = 0;
            (void)SafeReadInt32(reinterpret_cast<const void*>(e.watched_addr),
                                reinterpret_cast<int32_t*>(&v));
            e.value_after = v;

            e.speedval_at_hit = SpeedControl::current_value_static();

            self->push_entry(e);

            // Set the Resume Flag (RF) in EFlags so the instruction
            // doesn't re-fire on resume.  Per Intel SDM, RF suppresses
            // the next instruction's data-breakpoint check.
            ep->ContextRecord->EFlags |= 0x10000;  // bit 16 = RF

            return EXCEPTION_CONTINUE_EXECUTION;
        }
    };

} // namespace Horse
