// ============================================================================
// Horse::ReplayScrub - per-frame snapshot ring + scrub-back UI for SC6
// match-replay viewing.
//
// Why this exists
// ---------------
// SC6's .replay file format stores ONLY per-round chara checkpoints
// (FLuxBattleStateResetData.Rounds[N]) and per-frame inputs
// (FLuxBattleRecordingData.Rounds[N].Recorders[M]).  Playback re-runs the
// simulation from each round's start config, fed by the recorded input
// deltas.  There is NO per-frame full-state save in the file.
//
// Backward seek to an arbitrary frame therefore needs us to BUILD the
// per-frame state ourselves on the fly during normal forward playback.
// Strategy C from project_sc6_replay_scrub_design.md: keep a ring buffer
// of full HgCpuDirect snapshots (one per frame), then seek = restore the
// matching snapshot via LuxBattle_HgCpuDirect_ExecFinalizeAndPost.
//
// What's done by the engine vs. by us
// -----------------------------------
// SC6 already has a full snapshot/restore framework
// (LuxBattle_HgCpuDirect_*) used in production for the post-KO cinematic
// ring (LuxBattle_RoundResultCinematic_StateMachineTick @ 0x14037D670)
// and palette-variant rollback.  The engine writes ~80-100 KB of
// chara/global/timer/physics/VFX state per snapshot via an IBuffer-style
// abstraction; the buffer's vtable controls where the bytes go.
//
// Stock SC6 puts the buffer object inline in the session struct
// (g_LuxBattle_ActiveSessionDataPtr+0xa8 for variant-rollback,
// +0xAA120+0x488+slot*0x28018 for the cinematic ring).  We can't
// repurpose those without breaking their stock callers, so we provide
// our OWN buffer object exposing the same vtable contract, backed by a
// HorseMod-allocated ring of N x 0x28018 byte data slots.
//
// The IBuffer contract (verified via Ghidra walk of ExecMoveChangeAndPost
// @ 0x1403841E0 / ExecFinalizeAndPost @ 0x140384540 / RoundResultCinematic
// @ 0x14037D670) uses these vtable slots:
//
//   [+0x10]  Init(buf)                  - reset cursor
//   [+0x18]  BeginWrite(buf, ofs)       - set cursor for writing
//   [+0x20]  BeginRead(buf, ofs)        - set cursor for reading
//   [+0x28]  Write(buf, src, byteCount) - memcpy from src; advance cursor
//   [+0x30]  Read (buf, dst, byteCount) - memcpy to dst;   advance cursor
//   [+0x38]  GetCursorOffset(buf)       - return current cursor
//   [+0x40]  Validate(buf)              - return non-zero on success
//
// Slots [+0x00] / [+0x08] are destructor entries; engine-internal callers
// of the snapshot Exec* functions never invoke them, so we stub them.
//
// Lifecycle and presence gating
// -----------------------------
// Capturing only makes sense in Replay presence (the SC6 scene where the
// .replay deterministic-input system drives PerFrameTick).  Outside Replay
// the per-frame state isn't reproducible from a stored input stream so a
// snapshot ring would just be a waste of memory + per-frame CPU.
//
// We allocate the ring lazily on first entry into Replay presence and
// keep it allocated for the rest of the session (presence transitions
// only RESET the ring; we don't free until module shutdown).  At
// 600 frames (10s coverage at 60fps) the default footprint is
// 600 * 0x28018 = ~98 MB of resident memory.  Tunable via the Replay
// tab's "Capture seconds" slider.
//
// Threading
// ---------
// Capture runs from the cockpit pre-tick (game thread) gated on
// g_LuxBattle_FrameCounter advancing.  Seek runs from the same thread
// when the user drags the slider in the Replay-Scrub tab; the UI value
// is written to an atomic and consumed at the next cockpit pre-tick.
// No cross-thread mutation of the ring or buffer object.
// ============================================================================

#pragma once

#include "GameMode.hpp"
#include "HorseLib.hpp"   // GlobalPtr for LuxBattleManager lookup
#include "NativeBinding.hpp"
#include "ReplayScrubDiag.hpp"
#include "SafeMemoryRead.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Horse
{
    // ------------------------------------------------------------------
    // HgCpuBufferShim - HorseMod's implementation of the engine's
    // IBuffer-style vtable contract.
    //
    // The engine treats the buffer as opaque after constructing it;
    // ExecMoveChangeAndPost / ExecFinalizeAndPost reach in only via
    // vtable slots [+0x10]..[+0x40].  As long as our object's first 8
    // bytes point at a vtable that fulfils that contract, the engine's
    // own per-state writers (WriteCharaStateToSnapshot, WriteGlobal-
    // BattleStateToSnapshot, etc.) will memcpy bytes through us
    // transparently.
    //
    // Layout enforced by engine: vtable pointer at offset 0.  The rest
    // is purely internal bookkeeping for our impl.
    // ------------------------------------------------------------------
    class HgCpuBufferShim
    {
    public:
        HgCpuBufferShim() noexcept
            : m_vtable_ptr(s_vtable),
              m_data(nullptr),
              m_capacity(0),
              m_cursor(0)
        {}

        // Re-target the shim at a different backing slot in the ring.
        // Must be called before Init/BeginWrite/BeginRead from the
        // engine path.  The data pointer + capacity are HorseMod-owned;
        // the shim never allocates.
        void retarget(uint8_t* data, size_t capacity) noexcept
        {
            m_data     = data;
            m_capacity = capacity;
            m_cursor   = 0;
        }

        size_t cursor()   const noexcept { return m_cursor; }
        size_t capacity() const noexcept { return m_capacity; }

    private:
        // CRITICAL: must remain at offset 0.  The engine reads
        // *(void**)buf to get the vtable pointer.
        const void* const* m_vtable_ptr;
        uint8_t*           m_data;
        size_t             m_capacity;
        size_t             m_cursor;

        // 9-slot vtable matching the engine's IBuffer interface.
        // Function pointer initialisation is done in vtable_init() via
        // a Meyers-singleton; static const arrays of function pointers
        // can't be constexpr-initialised in C++17.
        static const void* const* s_vtable;

        static const void* const* build_vtable() noexcept
        {
            static const void* table[9] = {
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_dtor1),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_dtor2),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_init),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_begin_write),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_begin_read),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_write),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_read),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_get_cursor),
                reinterpret_cast<const void*>(&HgCpuBufferShim::vt_validate),
            };
            return table;
        }

        // Engine never calls dtors on the buffer object, but stub them
        // anyway so a stray vtable[0] dereference doesn't crash.
        static void __fastcall vt_dtor1(HgCpuBufferShim* /*self*/) noexcept {}
        static void __fastcall vt_dtor2(HgCpuBufferShim* /*self*/) noexcept {}

        static void __fastcall vt_init(HgCpuBufferShim* self) noexcept
        {
            self->m_cursor = 0;
        }

        static void __fastcall
        vt_begin_write(HgCpuBufferShim* self, int64_t offset) noexcept
        {
            self->m_cursor = static_cast<size_t>(offset);
        }

        static void __fastcall
        vt_begin_read(HgCpuBufferShim* self, int64_t offset) noexcept
        {
            self->m_cursor = static_cast<size_t>(offset);
        }

        // Write contract: copy `bytes` from src into the buffer at the
        // current cursor; advance cursor.  Engine ignores the return
        // (used by stock impls as an offset / status echo).  Truncate
        // silently on overflow - the 0x28018 stride is generous enough
        // that overflow indicates a layout bug, not a transient state.
        static int64_t __fastcall
        vt_write(HgCpuBufferShim* self, const void* src, size_t bytes) noexcept
        {
            if (!self->m_data || !src) return 0;
            if (self->m_cursor + bytes > self->m_capacity)
            {
                // Should never happen with the 0x28018 budget.  Log
                // once per session if it does (overflow elide bug).
                static std::atomic<bool> s_logged{false};
                if (!s_logged.exchange(true, std::memory_order_relaxed))
                {
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[ReplayScrub] HgCpuBufferShim overflow: cursor={} "
                            "+ bytes={} > capacity={}\n"),
                        self->m_cursor, bytes, self->m_capacity);
                }
                return 0;
            }
            std::memcpy(self->m_data + self->m_cursor, src, bytes);
            const int64_t prev = static_cast<int64_t>(self->m_cursor);
            self->m_cursor += bytes;
            return prev;
        }

        // Read contract: copy `bytes` from buffer at current cursor into
        // dst; advance cursor.
        static int64_t __fastcall
        vt_read(HgCpuBufferShim* self, void* dst, size_t bytes) noexcept
        {
            if (!self->m_data || !dst) return 0;
            if (self->m_cursor + bytes > self->m_capacity)
            {
                static std::atomic<bool> s_logged{false};
                if (!s_logged.exchange(true, std::memory_order_relaxed))
                {
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[ReplayScrub] HgCpuBufferShim read overflow: "
                            "cursor={} + bytes={} > capacity={}\n"),
                        self->m_cursor, bytes, self->m_capacity);
                }
                return 0;
            }
            std::memcpy(dst, self->m_data + self->m_cursor, bytes);
            const int64_t prev = static_cast<int64_t>(self->m_cursor);
            self->m_cursor += bytes;
            return prev;
        }

        static int64_t __fastcall
        vt_get_cursor(HgCpuBufferShim* self) noexcept
        {
            return static_cast<int64_t>(self->m_cursor);
        }

        // Engine-side validation: returning non-zero says "buffer is
        // OK".  Stock impls validate header parity / sequence numbers;
        // we just return 1 since our own integrity is guaranteed by
        // the cursor checks above.
        static int32_t __fastcall vt_validate(HgCpuBufferShim* /*self*/) noexcept
        {
            return 1;
        }
    };

    // Static vtable storage.  Lazily built via build_vtable() so the
    // function-pointer array is initialised once at first construction
    // and shared across all shim instances.
    inline const void* const* HgCpuBufferShim::s_vtable = build_vtable();

    // ------------------------------------------------------------------
    // FrameCapOverride - temporarily removes SC6's engine frame-rate cap.
    //
    // Why this is the correct fast-forward mechanism
    // ----------------------------------------------
    // SC6's per-frame loop is paced by UEngine::UpdateTimeAndHandleMax-
    // TickRate @ 0x142189040 (called once per FEngineLoop::Tick).  It
    // sleeps each frame for (1/cap - elapsed), where
    //     cap = bUseFixedFrameRate ? FixedFrameRate : GetMaxTickRate()
    //
    // The match-replay simulation is FRAME-COUNTED, not time-integrated:
    // every UE4 world tick advances the replay master clock by exactly
    // +1 (LuxBattleChara_VTable648_TickAndAdvanceReplayClock @
    // 0x1403E1FC0) and runs the WHOLE input pipeline once - decode ->
    // cache fill (UpdateInputCache_LocalMode) -> SimulationLoop catch-up
    // -> chara PerFrameTick.  So if the engine loop ticks faster, the
    // replay plays back proportionally faster and every stage stays in
    // 1:1 lockstep: the cache is filled and consumed once per tick (no
    // fill-rate mismatch), and the engine's own end-of-recording
    // handling still runs normally (no overshoot crash).
    //
    // This is why frame-cap removal succeeds where the earlier
    // master-clock-bump approach failed: bumping the clock made
    // SimulationLoop's catch-up loop consume K cache entries per UE4
    // frame while the writer produced only 1 (-> stale input -> frozen
    // chars), and bumping past the recording end read out of bounds
    // (-> crash).  Running more *whole* UE4 ticks has neither problem.
    //
    // engage() forces bUseFixedFrameRate ON and FixedFrameRate to a huge
    // value, so (1/cap - elapsed) is always <= 0 and the per-frame sleep
    // is skipped - the loop then runs as fast as render+sim allow.
    // disengage() restores the exact original engine fields.
    //
    // Verified addresses (image base 0x140000000)
    // -------------------------------------------
    //   GEngine global ptr slot : 0x1443B3068  (RVA 0x43B3068) - confirmed
    //       via FEngineLoop_Init [WRITE] / FEngineLoop_Tick [READ] xrefs.
    //   UEngine+0x648 : uint32 bitfield; bit 6 (0x40) = bUseFixedFrameRate
    //   UEngine+0x64C : float  FixedFrameRate
    //       Both verified in the UpdateTimeAndHandleMaxTickRate decompile
    //       (`*(uint*)(this+0x648) >> 6 & 1` selects FixedFrameRate at
    //       `*(float*)(this+0x64c)` as the sleep-rate target).
    //
    // Threading: every method here is game-thread only.  The UI (render
    // thread) must drive this exclusively via ReplayScrub::request_-
    // generate_timeline() / request_stop_generate_timeline() (atomic
    // handoff) - the non-atomic m_engaged / m_saved_* members rely on
    // that single-thread invariant.
    // ------------------------------------------------------------------
    class FrameCapOverride
    {
    public:
        static constexpr uintptr_t kRVA_GEngine        = 0x43B3068;
        static constexpr uintptr_t kOff_FixedRateFlags = 0x648;   // uint32 bitfield
        static constexpr uintptr_t kOff_FixedFrameRate = 0x64C;   // float
        static constexpr uint32_t  kBit_UseFixedRate   = 0x40;    // bit 6
        static constexpr float     kUncappedRate       = 5000.0f;

        bool is_engaged() const noexcept { return m_engaged; }

        // Resolve GEngine, save the two pacing fields, write the
        // uncapped values.  Returns false (changing nothing) if GEngine
        // isn't resolvable or the fields aren't accessible.  Idempotent:
        // a second engage() while already engaged is a no-op.
        bool engage() noexcept
        {
            if (m_engaged) return true;

            void* engine = resolve_engine();
            if (!engine)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.FrameCap] engage failed - GEngine "
                    "null/unreadable\n"));
                return false;
            }

            auto* e = static_cast<uint8_t*>(engine);
            uint32_t flags = 0;
            float    rate  = 0.0f;
            if (!SafeReadUInt32(e + kOff_FixedRateFlags, &flags)
                || !SafeReadFloat(e + kOff_FixedFrameRate, &rate))
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.FrameCap] engage failed - engine "
                    "pacing fields unreadable (engine=0x{:x})\n"),
                    reinterpret_cast<uintptr_t>(engine));
                return false;
            }

            m_saved_flags = flags;
            m_saved_rate  = rate;

            const bool ok =
                SafeWriteUInt32(e + kOff_FixedRateFlags,
                                flags | kBit_UseFixedRate)
                && SafeWriteFloat(e + kOff_FixedFrameRate, kUncappedRate);
            if (!ok)
            {
                // Roll back any partial write before giving up.
                SafeWriteUInt32(e + kOff_FixedRateFlags, flags);
                SafeWriteFloat(e + kOff_FixedFrameRate, rate);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.FrameCap] engage failed - engine "
                    "pacing fields not writable\n"));
                return false;
            }

            m_engaged = true;
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.FrameCap] engaged - engine=0x{:x} saved "
                "flags=0x{:x} rate={} -> forced bUseFixedFrameRate ON, "
                "FixedFrameRate={} (60fps cap removed)\n"),
                reinterpret_cast<uintptr_t>(engine),
                flags, rate, kUncappedRate);
            return true;
        }

        // Restore the saved engine fields.  Idempotent and safe to call
        // when not engaged (no-op in that case).
        void disengage() noexcept
        {
            if (!m_engaged) return;
            m_engaged = false;   // clear first so a re-entrant call no-ops

            void* engine = resolve_engine();
            if (!engine)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.FrameCap] disengage - GEngine "
                    "null/unreadable, frame cap NOT restored\n"));
                return;
            }
            auto* e = static_cast<uint8_t*>(engine);
            SafeWriteUInt32(e + kOff_FixedRateFlags, m_saved_flags);
            SafeWriteFloat(e + kOff_FixedFrameRate, m_saved_rate);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.FrameCap] disengaged - restored "
                "flags=0x{:x} rate={}\n"),
                m_saved_flags, m_saved_rate);
        }

    private:
        static void* resolve_engine() noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return nullptr;
            void* engine = nullptr;
            if (!SafeReadPtr(
                    reinterpret_cast<const void*>(base + kRVA_GEngine),
                    &engine))
                return nullptr;
            return engine;
        }

        bool     m_engaged     = false;
        uint32_t m_saved_flags = 0;
        float    m_saved_rate  = 0.0f;
    };

    // ------------------------------------------------------------------
    // ScreenPercentageOverride - drops r.ScreenPercentage during
    // "Generate timeline" so the fast-forward is not render-bound.
    //
    // Why this is needed alongside FrameCapOverride
    // ---------------------------------------------
    // SC6 replay viewing is render/sim-bound BELOW 60fps, so removing
    // the frame cap alone does not fast-forward - the loop just runs at
    // the render-limited native rate (measured ~28fps during a
    // generation pass).  Rendering the 3D scene at a low internal
    // resolution makes each frame cheap to DRAW, so the loop becomes
    // sim-bound and can iterate well above 60Hz - that is what actually
    // fast-forwards the frame-counted replay.  The user is not watching
    // during generation, so the low resolution is invisible; it is
    // restored when generation ends.
    //
    // Console-variable machinery (verified via FUN_1430C3B20 disasm +
    // FEngineLoop_Tick):
    //   g_pIConsoleManager      : global ptr, RVA 0x415CD80
    //   IConsoleManager  +0x90  : FindConsoleVariable(const TCHAR*)
    //   IConsoleVariable +0x60  : Set(const TCHAR*, uint32 flags)
    //   IConsoleVariable +0x40  : GetValueAddress() for a FLOAT CVar
    //                             (an INT CVar's is +0x38 - the vtable
    //                              layout differs per CVar value type)
    //
    // Threading: engage()/disengage() run on the game thread only
    // (start/stop_generate_timeline).  CVar Set from the game thread is
    // the normal path and handles render-thread shadow propagation.
    // ------------------------------------------------------------------
    class ScreenPercentageOverride
    {
    public:
        static constexpr uintptr_t kRVA_ConsoleManager = 0x415CD80;
        static constexpr uint32_t  kSetByConsole       = 0x8000000;

        bool is_engaged() const noexcept { return m_engaged; }

        // Drop r.ScreenPercentage to a low value.  Best-effort: returns
        // false (changing nothing) if the CVar isn't resolvable, in
        // which case generation still runs - just render-bound.
        bool engage() noexcept
        {
            if (m_engaged) return true;
            void* cvar = find_cvar(L"r.ScreenPercentage");
            if (!cvar)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.ScreenPct] r.ScreenPercentage not "
                    "found - generation will be render-bound\n"));
                return false;
            }
            m_saved = 100.0f;
            if (const float* p = cvar_value_ptr(cvar))
                m_saved = *p;
            // Sanity clamp: if the read looks wrong (non-float CVar, or
            // 0 = "use default"), restore to 100 rather than a nonsense
            // value - never restore the user to a 1% render scale.
            if (!(m_saved >= 10.0f && m_saved <= 200.0f))
                m_saved = 100.0f;
            if (!cvar_set(cvar, L"25"))
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.ScreenPct] r.ScreenPercentage Set "
                    "failed\n"));
                return false;
            }
            m_engaged = true;
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.ScreenPct] engaged - r.ScreenPercentage "
                "{} -> 25 (low-res render for fast generation)\n"),
                m_saved);
            return true;
        }

        // Restore the saved r.ScreenPercentage.  Idempotent.
        void disengage() noexcept
        {
            if (!m_engaged) return;
            m_engaged = false;
            void* cvar = find_cvar(L"r.ScreenPercentage");
            if (!cvar) return;
            // Restore as a clamped 3-digit string (no library needed).
            int v = static_cast<int>(m_saved + 0.5f);
            if (v < 1)   v = 1;
            if (v > 200) v = 200;
            const wchar_t buf[4] = {
                static_cast<wchar_t>(L'0' + (v / 100) % 10),
                static_cast<wchar_t>(L'0' + (v / 10)  % 10),
                static_cast<wchar_t>(L'0' +  v        % 10),
                L'\0' };
            cvar_set(cvar, buf);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.ScreenPct] disengaged - r.ScreenPercentage "
                "restored to {}\n"), m_saved);
        }

    private:
        using FindCVarFn = void* (*)(void* mgr, const wchar_t* name);
        using CVarSetFn  = void  (*)(void* cvar, const wchar_t* val,
                                     uint32_t flags);
        using CVarValFn  = void* (*)(void* cvar);

        static void* find_cvar(const wchar_t* name) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return nullptr;
            void* mgr = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                    base + kRVA_ConsoleManager), &mgr) || !mgr)
                return nullptr;
            void* vt = nullptr;
            if (!SafeReadPtr(mgr, &vt) || !vt) return nullptr;
            void* fn = nullptr;
            if (!SafeReadPtr(static_cast<uint8_t*>(vt) + 0x90, &fn) || !fn)
                return nullptr;
            return reinterpret_cast<FindCVarFn>(fn)(mgr, name);
        }
        static const float* cvar_value_ptr(void* cvar) noexcept
        {
            void* vt = nullptr;
            if (!SafeReadPtr(cvar, &vt) || !vt) return nullptr;
            void* fn = nullptr;
            // +0x40 = GetValueAddress for a FLOAT CVar (r.ScreenPercentage
            // is float; its value lives at cvar+0x68).  An INT CVar's
            // value-address getter is +0x38 - a different vtable layout;
            // do NOT use +0x38 here.  Verified: on a float CVar's vtable
            // +0x38 is a `return 0` stub (0x1402D9BF0 = XOR EAX,EAX;RET)
            // and +0x40 is 0x1408D06F0 = LEA RAX,[RCX+0x68];RET.
            if (!SafeReadPtr(static_cast<uint8_t*>(vt) + 0x40, &fn) || !fn)
                return nullptr;
            return reinterpret_cast<const float*>(
                reinterpret_cast<CVarValFn>(fn)(cvar));
        }
        static bool cvar_set(void* cvar, const wchar_t* val) noexcept
        {
            void* vt = nullptr;
            if (!SafeReadPtr(cvar, &vt) || !vt) return false;
            void* fn = nullptr;
            if (!SafeReadPtr(static_cast<uint8_t*>(vt) + 0x60, &fn) || !fn)
                return false;
            reinterpret_cast<CVarSetFn>(fn)(cvar, val, kSetByConsole);
            return true;
        }

        bool  m_engaged = false;
        float m_saved   = 100.0f;
    };

    // ========================================================================
    // Horse::ChunkPool - content-addressed deduplicating byte store.
    //
    // A replay tick's snapshot is ~177 KB but mostly redundant tick-to-tick:
    // whole-match-static data (stage geometry, configs, move-data pointers) is
    // byte-identical every tick, and even the "dynamic" InputLog cache ring
    // changes only ~one 16-byte entry per tick.  Storing the full blob per
    // tick is what caps the timeline at ~2 minutes of RAM.
    //
    // ChunkPool cuts captured regions into fixed kChunkBytes chunks, hashes
    // each, and keeps every DISTINCT chunk exactly once.  A tick then costs
    // only a list of 4-byte chunk ids plus whatever genuinely-new chunk
    // content it introduced.  Restore gathers a tick's chunks by id into a
    // contiguous buffer - byte-identical to the original capture (lossless),
    // so the existing restore path is unaffected.
    //
    // Game-thread only: capture (tick_capture) and seek (service_seek_request)
    // both run on the cockpit pre-tick, and never concurrently.
    // ========================================================================
    class ChunkPool
    {
    public:
        // 512 B balances dedup granularity (smaller = more shared chunks)
        // against id-list overhead (smaller = more ids/tick).  Must be a
        // multiple of 8 - hash64 reads the chunk as uint64 words.
        static constexpr size_t kChunkBytes = 512;

        // Intern kChunkBytes bytes at `p` (must be 8-byte aligned); returns
        // the id of the pool chunk holding exactly that content, adding a
        // new chunk only if this content has not been seen before.
        uint32_t intern(const uint8_t* p)
        {
            const uint64_t h = hash64(p);
            const auto range = m_index.equal_range(h);
            for (auto it = range.first; it != range.second; ++it)
            {
                // Hash hit - verify the full bytes.  A 64-bit hash
                // collision is astronomically rare, but an unverified
                // one would silently corrupt a restored frame, so the
                // hash is never trusted on its own.
                if (std::memcmp(chunk_ptr(it->second), p, kChunkBytes) == 0)
                    return it->second;
            }
            const uint32_t id =
                static_cast<uint32_t>(m_chunks.size() / kChunkBytes);
            m_chunks.insert(m_chunks.end(), p, p + kChunkBytes);
            m_index.emplace(h, id);
            return id;
        }

        const uint8_t* chunk_ptr(uint32_t id) const
        {
            return m_chunks.data()
                 + static_cast<size_t>(id) * kChunkBytes;
        }

        size_t unique_chunks() const { return m_chunks.size() / kChunkBytes; }

        // Approximate resident bytes (arena + index), for UI / logging.
        size_t bytes() const
        {
            return m_chunks.capacity()
                 + m_index.size()
                       * (sizeof(uint64_t) + sizeof(uint32_t) + 16);
        }

        void clear()
        {
            m_chunks.clear();
            m_chunks.shrink_to_fit();
            m_index.clear();
        }

    private:
        // FNV-1a over the chunk as uint64 words.  Non-cryptographic and
        // fast (~7 us for a 177 KB tick); every hash hit is byte-verified
        // in intern(), so a collision costs a wasted compare, never
        // correctness.
        static uint64_t hash64(const uint8_t* p)
        {
            uint64_t h = 1469598103934665603ull;
            const uint64_t* w = reinterpret_cast<const uint64_t*>(p);
            for (size_t i = 0; i < kChunkBytes / sizeof(uint64_t); ++i)
            {
                h ^= w[i];
                h *= 1099511628211ull;
            }
            return h;
        }

        std::vector<uint8_t>                        m_chunks;  // chunk arena
        std::unordered_multimap<uint64_t, uint32_t> m_index;   // hash -> id
    };

    // ========================================================================
    // Horse::RegionStore - one deduplicated, append-only per-tick region.
    //
    // Backs a single capture region (sim / InputLog / decoder / extras) with
    // a shared ChunkPool.  Each tick's region is a fixed byte length, so the
    // chunk count per tick is fixed and a tick is stored as that many chunk
    // ids.  Append-only: tick index == storage order == timeline capture seq.
    // ========================================================================
    class RegionStore
    {
    public:
        // `region_len` is the exact captured byte length of this region.
        void init(ChunkPool* pool, size_t region_len)
        {
            m_pool            = pool;
            m_region_len      = region_len;
            m_chunks_per_tick =
                (region_len + ChunkPool::kChunkBytes - 1)
                    / ChunkPool::kChunkBytes;
            m_padded_len      = m_chunks_per_tick * ChunkPool::kChunkBytes;
            m_ids.clear();
            m_ids.shrink_to_fit();
            m_scratch.assign(m_padded_len, 0);
        }

        size_t region_len() const { return m_region_len; }
        size_t tick_count() const
        {
            return m_chunks_per_tick
                 ? m_ids.size() / m_chunks_per_tick : 0;
        }

        // Writable region_len-byte staging buffer.  Capture code fills this,
        // then calls commit() to fold it into the pool as the next tick.
        uint8_t* scratch() { return m_scratch.data(); }

        // Fold the current scratch contents in as the next tick.  Returns
        // false ONLY if the optional self-test (first kSelfTestTicks ticks)
        // detects a chunk/gather round-trip mismatch - which would mean a
        // ChunkPool bug and must never happen in practice.
        bool commit()
        {
            // Zero the pad tail so the final (partial) chunk hashes
            // deterministically regardless of stale scratch bytes.
            if (m_padded_len > m_region_len)
                std::memset(m_scratch.data() + m_region_len, 0,
                            m_padded_len - m_region_len);

            const size_t tick = tick_count();
            for (size_t c = 0; c < m_chunks_per_tick; ++c)
                m_ids.push_back(m_pool->intern(
                    m_scratch.data() + c * ChunkPool::kChunkBytes));

            // Lossless self-test on the first few ticks: gather the tick
            // just stored and confirm it reproduces the scratch byte-for-
            // byte.  Cheap, and proves the chunk/gather path before any of
            // it is trusted for a real restore.
            if (tick < kSelfTestTicks)
            {
                std::vector<uint8_t> rt(m_region_len, 0);
                if (!load(tick, rt.data())
                    || std::memcmp(rt.data(), m_scratch.data(),
                                   m_region_len) != 0)
                    return false;
            }
            return true;
        }

        // Gather tick `tick`'s region into `dst` (region_len bytes).
        // Returns false if `tick` is out of range.
        bool load(size_t tick, uint8_t* dst) const
        {
            if (tick >= tick_count()) return false;
            const size_t base = tick * m_chunks_per_tick;
            size_t off = 0;
            for (size_t c = 0; c < m_chunks_per_tick; ++c)
            {
                const size_t n =
                    (m_region_len - off < ChunkPool::kChunkBytes)
                        ? (m_region_len - off) : ChunkPool::kChunkBytes;
                std::memcpy(dst + off,
                            m_pool->chunk_ptr(m_ids[base + c]), n);
                off += n;
            }
            return true;
        }

        // Gather tick `tick` into the internal scratch and return it, for
        // callers that need a transient read-only view (the restore path).
        // Returns nullptr if out of range.  Not const: writes the scratch.
        // Safe because capture and restore never overlap (both game-thread,
        // different cockpit-tick phases).
        const uint8_t* gather(size_t tick)
        {
            if (!load(tick, m_scratch.data())) return nullptr;
            return m_scratch.data();
        }

        // Drop all stored ticks (keeps the init() sizing).  The shared
        // ChunkPool is cleared separately by the owner.
        void clear() { m_ids.clear(); }

        size_t idlist_bytes() const
        {
            return m_ids.capacity() * sizeof(uint32_t);
        }

    private:
        static constexpr size_t kSelfTestTicks = 128;

        ChunkPool*            m_pool            {nullptr};
        size_t                m_region_len      {0};
        size_t                m_chunks_per_tick {0};
        size_t                m_padded_len      {0};
        std::vector<uint32_t> m_ids;       // [tick*chunks_per_tick + chunk]
        std::vector<uint8_t>  m_scratch;   // padded staging / gather buffer
    };

    // ------------------------------------------------------------------
    // ReplayScrub - the snapshot ring + UI driver.
    //
    // Allocates a ring of N x 0x28018-byte data slots (HgCpuDirect's
    // per-snapshot stride) and a single buffer-shim object that we
    // re-target onto each slot when capturing or restoring.
    // ------------------------------------------------------------------
    class ReplayScrub
    {
    public:
        // Timeline auto-generation state (2026-05-16).  Used by the
        // "Generate timeline" UI button to populate the snapshot ring
        // by running the replay at max safe speed until the engine
        // clamps at end-of-recording.  Declared at top of class so
        // both accessor methods and external UI code can reference it.
        enum class TimelineGenState : int { Idle = 0, Generating = 1, Done = 2 };

        // A round-boundary marker for the timeline UI: `seq` is the
        // capture sequence of the first snapshot of round `round`.
        // Returned by collect_round_markers().
        struct RoundMarker { int32_t seq; int32_t round; };

        // Per-snapshot stride matches HgCpuDirect's allocator stride
        // (verified via post-KO cinematic ring at session+0xAA120+0x488).
        static constexpr size_t kSnapshotStride = 0x28018;

        // RVAs for the engine entry points (verified via Ghidra).
        static constexpr uintptr_t kRVA_ExecMoveChangeAndPost = 0x3841E0;
        static constexpr uintptr_t kRVA_ExecFinalizeAndPost   = 0x384540;
        static constexpr uintptr_t kRVA_FrameCounter          = 0x470D0C4;

        // ROUND-END SEEK-BACK fix (2026-05-14): the HgCpuDirect global
        // snapshot covers g_LuxBattle_VMFreezeRecord, the 320-byte region
        // including g_LuxBattle_WorldModePump_MasterModeFlag, plus various
        // tables / VFX state.  But it does NOT cover:
        //
        //   * g_LuxBattle_WorldModePump struct @ 0x144843ED0 (the state
        //     machine that publishes MasterModeFlag).  At round end it
        //     transitions to a round-result mode object; without
        //     restoring this, MasterModeFlag re-publishes 3 next tick
        //     even after our restore wrote 2, gating chara input tick
        //     OFF via the BattleAdvanceFlag check at the top of
        //     LuxBattle_PerFrameTick.
        //   * g_LuxBattle_BlockInteractiveOps @ 0x1448463F8 - set by
        //     the round-result cinematic; blocks several interactive ops.
        //   * The cinematic state machine head at session+0xAA120 -
        //     state/triggers/frame-counter that drives the post-KO
        //     replay cinematic.  Leaving it in state=2 (cinematic
        //     playback) after a seek back to mid-round runs cinematic
        //     animation over the restored state.
        //   * Several BM internal state bytes (bMainStateMachineByte,
        //     bMoveStateByte, bStatusByte, bEnginePauseFlag) which gate
        //     SimulationLoop.
        //
        // These together cause the "characters just look at each other"
        // symptom when seeking from post-round back into the round.
        // Capture/restore them via a parallel ring (kExtras_Bytes per
        // slot; ~656 bytes per slot at 7200 slots = ~4.5 MB extra).
        static constexpr uintptr_t kRVA_WorldModePump        = 0x4843ED0;
        static constexpr uintptr_t kRVA_BlockInteractiveOps  = 0x48463F8;
        static constexpr uintptr_t kRVA_ActiveSessionDataPtr = 0x4843F00;
        static constexpr uintptr_t kCinematic_State_Off      = 0xAA120;  // session offset

        // Default ring size: 60 seconds @ 60fps - covers a typical SC6
        // round (versus default round timer = 60s).  Tunable via UI;
        // higher values trade memory for longer scrub-back range.
        // Each snapshot is 0x28018 bytes (~164 KB), so:
        //     300 frames =  ~49 MB (  5 s coverage)
        //     600 frames =  ~98 MB ( 10 s coverage)
        //    1800 frames = ~295 MB ( 30 s coverage)
        //    3600 frames = ~590 MB ( 60 s coverage = default)
        //    5400 frames = ~885 MB ( 90 s coverage)
        //    7200 frames = ~1.13 GB (120 s coverage = max)
        // Allocation is deferred until first Replay-presence entry so
        // users who never watch replays don't pay the memory cost.
        static constexpr size_t kDefaultRingFrames = 3600;
        static constexpr size_t kMaxRingFrames     = 7200;
        static constexpr size_t kMinRingFrames     = 60;     // 1 s minimum

        static ReplayScrub& instance()
        {
            static ReplayScrub s;
            return s;
        }

        // Allocate the ring on first call; subsequent calls are no-ops
        // unless the requested size differs - in which case we free
        // and re-allocate.  Should be called from on_unreal_init or
        // first cockpit tick observing Replay presence.
        bool ensure_initialized(size_t ring_frames = kDefaultRingFrames)
        {
            if (m_initialized.load(std::memory_order_acquire)
                && m_ring_frames == ring_frames)
                return true;

            // Resolve native function pointers via image base.  Done
            // lazily here so we don't fight startup races with
            // NativeBinding::resolve() being called from elsewhere.
            if (!resolve_natives()) return false;

            // Drop the initialized flag BEFORE tearing down ring storage.
            // The UI thread reads ring_count()/earliest_seq()/latest_-
            // seq() unconditionally inside render_replay_tab and is
            // only gated by is_initialized() at the outer scope; flipping
            // the flag here closes the window where free_ring() has
            // already cleared the atomic-vectors / nulled m_ring_frames
            // but is_initialized() still reads true, which would crash
            // earliest_seq() on an out-of-bounds vector subscript.
            m_initialized.store(false, std::memory_order_release);

            // (Re-)allocate.  Calling this with a different ring size
            // discards captured snapshots; that's fine - sizing is a
            // user-driven action, not steady-state.
            free_ring();
            if (!alloc_ring(ring_frames)) return false;

            m_initialized.store(true, std::memory_order_release);
            const size_t sim_mb =
                (ring_frames * kSnapshotStride) / (1024ull * 1024ull);
            const size_t il_mb =
                (ring_frames * kIL_CaptureBytes) / (1024ull * 1024ull);
            const size_t rdb_mb =
                (ring_frames * kRDB_Bytes) / (1024ull * 1024ull);
            const size_t extras_kb =
                (ring_frames * kExtras_Bytes) / 1024ull;
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub] initialised; ring={} frames "
                    "(sim ~{} MB + InputLog ~{} MB + RDB ~{} MB + "
                    "extras ~{} KB = ~{} MB total)\n"),
                ring_frames, sim_mb, il_mb, rdb_mb, extras_kb,
                sim_mb + il_mb + rdb_mb);
            return true;
        }

        // Tear down at module shutdown.
        void shutdown()
        {
            // Restore the engine frame cap + screen percentage first -
            // they must never outlive the module if generation was
            // still running at unload.
            m_frame_cap.disengage();
            m_screen_pct.disengage();
            free_ring();
            m_initialized.store(false, std::memory_order_release);
        }

        bool is_initialized() const noexcept
        {
            return m_initialized.load(std::memory_order_acquire);
        }

        // Per-cockpit-tick driver.  Reads the SC6 global frame counter
        // (imageBase + 0x470D0C4) and, if it advanced, captures a
        // snapshot into the next ring slot.  Cheap on idle frames -
        // single SafeReadUInt32 + comparison; no allocation.
        //
        // Capture is gated by:
        //   * Tracker initialised
        //   * Capture-enabled toggle (m_capture_enabled)
        //   * Not paused (m_paused) - the world is frozen anyway under
        //     pause so the frame counter wouldn't advance, but the
        //     explicit gate makes the intent obvious.
        //   * Presence == Replay
        //   * Frame counter advanced since last observation
        void tick_capture()
        {
            if (!is_initialized()) return;

            // Update the cached live master clock UNCONDITIONALLY each
            // cockpit tick.  The UI thread reads this via current_play-
            // _position() to extrapolate a smooth playhead during
            // forward play post-seek.  Cheap (one SafeReadInt32 via
            // BM->IL+0x3A4); no side effects.
            {
                const int32_t m = read_engine_master_clock();
                m_live_master_cached.store(m, std::memory_order_release);
            }

            // Post-seek tick logging runs UNCONDITIONALLY (even when
            // paused / capture disabled / outside Replay presence) so
            // we observe state across the full window after a seek -
            // including the moments where the engine has stopped
            // advancing master clock (e.g. UDemoNetDriver at EOF, the
            // "stand still" failure mode the user reported).  The
            // dump itself is cheap (~20 safe reads), and the countdown
            // bounds it to a fixed number of ticks per seek.
            const int32_t cd_top =
                m_post_seek_countdown.load(std::memory_order_acquire);
            if (cd_top > 0)
            {
                uint32_t cur_top = 0;
                read_frame_counter(cur_top);
                tick_post_seek_dump(cd_top, static_cast<int32_t>(cur_top),
                                    read_engine_master_clock());
                m_post_seek_countdown.store(cd_top - 1,
                                            std::memory_order_release);
            }

            // New-replay detection (runs regardless of the pause /
            // capture-enabled toggles, so a replay swapped while paused
            // is still caught).  The 'Replay' presence value covers the
            // whole replay section - the browser AND playback both
            // report Replay - so closing one replay and opening another
            // produces NO presence transition; on_presence_change()
            // never fires for a replay->replay swap.  Each replay
            // playback is a fresh level load with a fresh BattleManager,
            // so a changed BM while still in Replay presence means a new
            // replay was loaded: do the full new-replay reset.
            if (GameMode::instance().current_presence()
                == GamePresence::Replay)
            {
                RC::Unreal::UObject* bm =
                    m_bm_ptr.get(L"LuxBattleManager");
                RC::Unreal::UObject* rp =
                    ReplayScrubDiag::replay_player_ptr().get(
                        L"LuxBattleReplayPlayer");
                // BM and ReplayPlayer are independent fresh-per-replay
                // actors - tracking both means a heap-address reuse on
                // one cannot hide a replay swap.
                const bool bm_changed =
                    (bm && m_last_bm_obj && bm != m_last_bm_obj);
                const bool rp_changed =
                    (rp && m_last_replay_player_obj
                     && rp != m_last_replay_player_obj);
                if (bm_changed || rp_changed)
                    reset_for_new_replay(bm_changed ? "new BattleManager"
                                                    : "new ReplayPlayer");
                if (bm) m_last_bm_obj            = bm;
                if (rp) m_last_replay_player_obj = rp;
            }

            // Capture runs when passive capture is enabled OR a
            // "Generate timeline" pass is in progress.  Passive capture
            // is OFF by default (the per-frame snapshot is expensive);
            // the deliberate Generate pass is the normal way to fill
            // the ring.
            if (!m_capture_enabled.load(std::memory_order_acquire)
                && m_timeline_gen_state.load(std::memory_order_acquire)
                       != static_cast<int>(TimelineGenState::Generating))
                return;
            // While paused (= world frozen via WorldTickGate), the
            // global frame counter doesn't advance, so the
            // counter-delta gate below also blocks capture.  The
            // explicit pause check is redundant but cheap and makes
            // the intent obvious.
            if (m_paused.load(std::memory_order_acquire))           return;

            const auto presence = GameMode::instance().current_presence();
            if (presence != GamePresence::Replay) return;

            uint32_t cur = 0;
            if (!read_frame_counter(cur)) return;

            if (!m_have_last_counter)
            {
                m_last_counter      = cur;
                m_last_master       = read_engine_master_clock();
                m_last_round        = read_current_round();
                m_have_last_counter = true;
                // Do NOT capture on the first tick - the simulation
                // may still be initialising on first-replay-frame.
                return;
            }
            if (cur == m_last_counter) return;         // wall halted (paused)
            if (cur < m_last_counter)
            {
                // Wall counter (g_LuxBattle_FrameCounter) reset to 0.
                // The engine zeroes it at EVERY round boundary
                // (LuxBattle_InitializeMatchRoundState @ 0x1402DBA92),
                // so a backward step here is normal mid-match.  It is
                // NOT a timeline discontinuity - the seq tag spans the
                // whole match - so we KEEP the ring across round
                // transitions and just rebaseline the capture clocks.
                //
                // The exception is a genuine restart: the replay viewer
                // rewound to an earlier round (CurrentRound decreased).
                // This is the BACKUP new-replay signal - the primary is
                // the BattleManager-identity check above; this covers
                // the rare case where a fresh replay's BM happens to
                // reuse the previous BM's heap address.
                const int32_t round_now = read_current_round();
                if (round_now >= 0 && m_last_round >= 0
                    && round_now < m_last_round)
                {
                    reset_for_new_replay("round index reset");
                    return;
                }
                m_last_counter = cur;
                m_last_master  = read_engine_master_clock();
                if (round_now >= 0) m_last_round = round_now;
                return;
            }

            // Wall advanced.  Now also check whether the engine's
            // REPLAY MASTER CLOCK actually advanced - the wall counter
            // ticks every UE4 frame regardless of replay state, but
            // the master clock only advances when the replay system is
            // actually playing forward.  If master is stuck (e.g.
            // post-seek when the input pipeline can't deliver new
            // moves), there's no useful frame state to snapshot - skip
            // capture and keep the existing ring intact so the user
            // can re-scrub through the still-good captures.
            const int32_t cur_master = read_engine_master_clock();
            if (cur_master < 0)
            {
                // Master unreadable (BM/IL torn down).  Track wall but
                // don't capture or update master baseline.
                m_last_counter = cur;
                return;
            }
            if (cur_master <= m_last_master)
            {
                // Master not advancing (or rolled back, e.g. from a
                // backward seek we just executed).  Don't capture.
                // If the master rolled back, update the baseline so
                // we detect future forward advances; if just stuck,
                // keep the baseline so we still detect resumption.
                m_last_counter = cur;
                if (cur_master < m_last_master)
                    m_last_master = cur_master;
                return;
            }

            // Both wall AND master advanced - real forward play.
            // Capture exactly ONE snapshot per cockpit tick.
            capture_snapshot(static_cast<int32_t>(cur));
            m_last_counter = cur;
            m_last_master  = cur_master;

            // Periodic BASELINE diagnostic dump (~once per second at
            // 60fps) - verbose-only.  Unconditional logging here was
            // steady-state log noise during normal replay viewing.
            if (m_verbose_diag.load(std::memory_order_acquire)
                && (cur % 60u) == 0u)
            {
                dump_replay_state("BASELINE", static_cast<int32_t>(cur));
                ReplayScrubDiag::dump_full("BASELINE_FULL");
            }

            // On-demand force dump (UI button).  Fires regardless of
            // verbose mode.  Single-shot - reset the request flag.
            if (m_force_diag_request.exchange(false,
                    std::memory_order_acq_rel))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub] === FORCE DIAG DUMP "
                        "(wall={}, master={}) ===\n"),
                    cur, cur_master);
                dump_replay_state("FORCE_DUMP", static_cast<int32_t>(cur));
                ReplayScrubDiag::dump_full("FORCE_DUMP");
            }
            // (post-seek countdown dump moved to the top of tick_capture
            // so it fires even while paused / master stuck / outside
            // Replay presence)
        }

        // Per-tick post-seek delta dump.  Reads both chara MoveVM
        // states, compares against the last-seen snapshot, and logs a
        // single CHANGED / UNCHANGED line per chara.  Cheap (~10 safe
        // reads per chara, no allocations).
        //
        // What we're looking for:
        //   - CHANGED moveID  = MoveVM advanced to a new move (good:
        //                       engine is ticking)
        //   - CHANGED pos     = chara is physically moving (good)
        //   - UNCHANGED both  = simulation frozen (bad: confirms the
        //                       "characters complete current move and
        //                       freeze" symptom)
        //   - moveID drift over many ticks while pos stays put = end
        //                       of move; should transition to neutral
        void tick_post_seek_dump(int32_t countdown_now,
                                 int32_t wall, int32_t master) noexcept
        {
            const int32_t t_after_seek =
                kDefaultPostSeekDumpFrames - countdown_now + 1;

            ReplayScrubDiag::CharaMoveVmSnap p1 =
                ReplayScrubDiag::read_chara_movevm(0);
            ReplayScrubDiag::CharaMoveVmSnap p2 =
                ReplayScrubDiag::read_chara_movevm(1);

            // Pause-state transition detection: log a one-line marker
            // every time m_paused flips, so we can correlate "user
            // pressed Play at tick T" with subsequent state changes.
            // This is the most critical missing diagnostic - we had
            // zero log data for the post-play window in the prior run.
            const bool now_paused =
                m_paused.load(std::memory_order_acquire);
            if (now_paused != m_diag_last_paused)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] PAUSE_TRANSITION cd={} t+{} "
                        "wall={} master={} {} -> {}  paused_atomic={}\n"),
                    countdown_now, t_after_seek, wall, master,
                    RC::to_generic_string(
                        m_diag_last_paused ? "PAUSED" : "PLAYING"),
                    RC::to_generic_string(
                        now_paused ? "PAUSED" : "PLAYING"),
                    now_paused ? 1 : 0);
                m_diag_last_paused = now_paused;
            }

            // Compute deltas vs the last tick.  Show explicit "+N" /
            // "STUCK" markers so the failure mode (wall advances but
            // master doesn't, or vice versa) jumps out at a glance.
            const int32_t wall_delta =
                static_cast<int32_t>(wall) -
                static_cast<int32_t>(m_diag_last_wall);
            const int32_t master_delta =
                (m_diag_last_master < 0 || master < 0)
                  ? 0
                  : (master - m_diag_last_master);

            // BM input pair @ +0x14A8 and current input @ +0x1498.
            // Stage 3 / UpdatePlayerInputData write these; if they
            // stay 0 every tick post-play, no input is reaching the
            // chara even though the engine is calling its tick path.
            uint32_t bm_input_p1_held  = 0, bm_input_p1_edge = 0;
            uint32_t bm_input_p2_held  = 0, bm_input_p2_edge = 0;
            uint32_t bm_cur_input_p1   = 0, bm_cur_input_p2  = 0;
            RC::Unreal::UObject* bm_obj =
                m_bm_ptr.get(L"LuxBattleManager");
            if (bm_obj)
            {
                uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
                // BM+0x14A8 is a TArray<...> header in some readings
                // and a direct uint64 pair in others; dump as raw
                // uint32 quads from both +0x14A8 (held|edge x P1) and
                // +0x14B0 (held|edge x P2), plus the BM+0x1498
                // PrevInputArray pointer's first 8 bytes.
                void* p_pair_base = nullptr;
                SafeReadPtr(bm + 0x14A8, &p_pair_base);
                if (p_pair_base)
                {
                    uint8_t* pb = reinterpret_cast<uint8_t*>(p_pair_base);
                    SafeReadUInt32(pb + 0x00, &bm_input_p1_held);
                    SafeReadUInt32(pb + 0x04, &bm_input_p1_edge);
                    SafeReadUInt32(pb + 0x08, &bm_input_p2_held);
                    SafeReadUInt32(pb + 0x0C, &bm_input_p2_edge);
                }
                void* p_cur = nullptr;
                SafeReadPtr(bm + 0x1498, &p_cur);
                if (p_cur)
                {
                    uint8_t* pcb = reinterpret_cast<uint8_t*>(p_cur);
                    SafeReadUInt32(pcb + 0x00, &bm_cur_input_p1);
                    SafeReadUInt32(pcb + 0x04, &bm_cur_input_p2);
                }
            }

            // PlayerRecordArray gate-bit probe (2026-05-14): read
            // pra+0x398 for P0 and P1 to verify whether the replay-
            // advance forward bit (0x200) is set per tick.  If our
            // restore set bit 9 = 1 but the engine cleared it on the
            // next tick, we'll see it drop from 0x200 to 0 here.
            uint32_t pra_p0_398 = 0, pra_p1_398 = 0;
            if (bm_obj)
            {
                uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
                void* pra_raw = nullptr;
                if (SafeReadPtr(bm + kBM_PlayerRecordArray_Off, &pra_raw)
                    && pra_raw)
                {
                    uint8_t* pra = reinterpret_cast<uint8_t*>(pra_raw);
                    SafeReadUInt32(pra + kPRA_FieldAt398_Off, &pra_p0_398);
                    SafeReadUInt32(pra + kPRA_PlayerStride + kPRA_FieldAt398_Off,
                                   &pra_p1_398);
                }
            }

            // Sample three chara+0x3C0 ring entries per chara at slots
            // (master & 0x1FF), ((master-1) & 0x1FF), ((master+1) &
            // 0x1FF).  This is the per-CHARA ring (separate from the
            // pInputLog cache at IL+0x3C0).  Layout: frame_id(4) +
            // packet_cursor(4) + input_value(4) + filled_byte(1).
            // Reads from the chara slot pointers.
            uint32_t p1_ring_in [3] = {0, 0, 0};
            uint32_t p2_ring_in [3] = {0, 0, 0};
            uint32_t p1_ring_idx[3] = {0, 0, 0};
            uint32_t p2_ring_idx[3] = {0, 0, 0};
            if (p1.chara_ptr)
            {
                uint8_t* c = reinterpret_cast<uint8_t*>(p1.chara_ptr);
                for (int k = -1; k <= 1; ++k) {
                    int32_t idx = master + k;
                    if (idx < 0) continue;
                    const size_t slot = static_cast<size_t>(idx) & 0x1FF;
                    SafeReadUInt32(c + 0x3C4 + slot * 0x10,
                                   &p1_ring_idx[k+1]);
                    SafeReadUInt32(c + 0x3C8 + slot * 0x10,
                                   &p1_ring_in[k+1]);
                }
            }
            if (p2.chara_ptr)
            {
                uint8_t* c = reinterpret_cast<uint8_t*>(p2.chara_ptr);
                for (int k = -1; k <= 1; ++k) {
                    int32_t idx = master + k;
                    if (idx < 0) continue;
                    const size_t slot = static_cast<size_t>(idx) & 0x1FF;
                    SafeReadUInt32(c + 0x3C4 + slot * 0x10,
                                   &p2_ring_idx[k+1]);
                    SafeReadUInt32(c + 0x3C8 + slot * 0x10,
                                   &p2_ring_in[k+1]);
                }
            }

            // 2026-05-15: read g_LuxBattle_LatestEngineInput_PerPlayer.
            // This is the LIVE per-frame input source that PerFrameTick
            // mirrors from args[0]/args[1] each tick.  If it stays at
            // 0/0 post-seek while the simulation ticks normally, the
            // upstream input dispatcher has stopped feeding inputs.
            ReplayScrubDiag::LatestEngineInputSnap ei =
                ReplayScrubDiag::read_latest_engine_input();

            // 2026-05-16 PIPELINE STAGE PROBES.  Read intermediate-stage
            // values to distinguish which stage is failing post-seek:
            //
            //   Stage A: [BM+0x450]+0x3E0 / +0x470  (ALuxBattleFrameInput
            //            per-slot input record - UPSTREAM source)
            //   Stage B: IL+0x3B8 / +0x3BC          (per-slot current input
            //            written by RefreshPerSlotCurrentInput_To3B8)
            //   Stage C: cache[slot][master & 0x1FF]  (filled by
            //            UpdateInputCache_LocalMode; read by
            //            GetCachedRoundValue_ByIndex)
            //
            // Interpretation:
            //   - Stage A varies per tick + Stage B varies per tick + Stage C
            //     valid -> live writer chain is healthy -> chars should move
            //   - Stage A fixed at snapshot value, Stage B mirrors -> live
            //     writer of BM+0x450+0x3E0 NOT firing -> need to find/wake it
            //   - Stage A varies, Stage B doesn't -> RefreshPerSlot writer
            //     not firing -> vtable[0x638] dispatch gate is failing
            //   - Stage B varies, Stage C doesn't match -> cache writer
            //     vtable[0xCA0] not firing -> dispatcher gate failing
            uint32_t bm450_p0 = 0, bm450_p1 = 0;
            uint32_t il3b8_p0 = 0, il3b8_p1 = 0;
            int32_t  cache_p0_fid = -1, cache_p0_idx = -1;
            uint32_t cache_p0_input = 0;
            uint8_t  cache_p0_filled = 0;
            int32_t  cache_p1_fid = -1, cache_p1_idx = -1;
            uint32_t cache_p1_input = 0;
            uint8_t  cache_p1_filled = 0;
            if (bm_obj)
            {
                uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
                // Stage A: ALuxBattleFrameInput per-slot input
                void* fi_raw = nullptr;
                if (SafeReadPtr(bm + kBM_FrameInputActor_Off, &fi_raw)
                    && fi_raw)
                {
                    uint8_t* fi = reinterpret_cast<uint8_t*>(fi_raw);
                    SafeReadUInt32(fi + kFI_SlotRecords_Start + 0, &bm450_p0);
                    SafeReadUInt32(fi + kFI_SlotRecords_Start
                                      + kFI_SlotRecord_Stride, &bm450_p1);
                }
                // Stage B + C: read from FrameInputLog
                void* il_raw = nullptr;
                if (SafeReadPtr(bm + kBM_BattleFrameInputLog_Off, &il_raw)
                    && il_raw)
                {
                    uint8_t* il = reinterpret_cast<uint8_t*>(il_raw);
                    SafeReadUInt32(il + 0x3B8, &il3b8_p0);
                    SafeReadUInt32(il + 0x3BC, &il3b8_p1);
                    // Cache entry at slot (master & 0x1FF) for each player.
                    // Cache base IL+0x3C0, per-slot stride 0x2000, per-entry
                    // stride 0x10.  Entry layout: nFrameID(4)/dwFrameIndex(4)
                    // /dwInputValue(4)/bFilled(1).
                    if (master >= 0) {
                        const size_t idx = static_cast<size_t>(master) & 0x1FF;
                        uint8_t* p0_entry = il + 0x3C0 + idx * 0x10;
                        uint8_t* p1_entry = il + 0x3C0 + 0x2000 + idx * 0x10;
                        SafeReadInt32 (p0_entry + 0,  &cache_p0_fid);
                        SafeReadInt32 (p0_entry + 4,  &cache_p0_idx);
                        SafeReadUInt32(p0_entry + 8,  &cache_p0_input);
                        SafeReadUInt8 (p0_entry + 12, &cache_p0_filled);
                        SafeReadInt32 (p1_entry + 0,  &cache_p1_fid);
                        SafeReadInt32 (p1_entry + 4,  &cache_p1_idx);
                        SafeReadUInt32(p1_entry + 8,  &cache_p1_input);
                        SafeReadUInt8 (p1_entry + 12, &cache_p1_filled);
                    }
                }
            }

            // Header line with global counters + deltas every tick.
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] POST_SEEK_TICK cd={} t+{} wall={} "
                    "(d{:+d}) master={} (d{:+d}) paused={} "
                    "BM_input[P1=0x{:X}|0x{:X} P2=0x{:X}|0x{:X}] "
                    "BM_cur[P1=0x{:X} P2=0x{:X}] "
                    "PRA[P0+0x398=0x{:X} P1+0x398=0x{:X}] "
                    "EngineInput[P1=0x{:X} P2=0x{:X}]\n"),
                countdown_now, t_after_seek, wall, wall_delta,
                master, master_delta, now_paused ? 1 : 0,
                bm_input_p1_held, bm_input_p1_edge,
                bm_input_p2_held, bm_input_p2_edge,
                bm_cur_input_p1, bm_cur_input_p2,
                pra_p0_398, pra_p1_398,
                ei.p1_input, ei.p2_input);

            // 2026-05-16 PIPELINE STAGE LINE: gated behind verbose_diag
            // since the bug (chars stuck post-seek) appears resolved.
            // Keep the read code unconditional (cheap) but skip the log
            // emit to avoid flooding the log per tick.
            if (m_verbose_diag.load(std::memory_order_acquire))
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] POST_SEEK_TICK cd={} t+{} "
                    "StageA(BM+0x450+3E0/470)[P0=0x{:X} P1=0x{:X}] "
                    "StageB(IL+0x3B8/3BC)[P0=0x{:X} P1=0x{:X}] "
                    "StageC@cache[{}][P0:fid={} idx={} val=0x{:X} f={}] "
                    "[P1:fid={} idx={} val=0x{:X} f={}]\n"),
                countdown_now, t_after_seek,
                bm450_p0, bm450_p1,
                il3b8_p0, il3b8_p1,
                master >= 0 ? (master & 0x1FF) : -1,
                cache_p0_fid, cache_p0_idx, cache_p0_input,
                static_cast<unsigned>(cache_p0_filled),
                cache_p1_fid, cache_p1_idx, cache_p1_input,
                static_cast<unsigned>(cache_p1_filled));

            // Chara+0x3C0 ring contents.  In replay viewing this ring
            // is the CHARA-SIDE input buffer (separate from IL+0x3C0
            // which is online-only).  If Stage 2 is filling the ring,
            // we should see non-zero `in` values at slot (master &
            // 0x1FF).  All-zeros = Stage 2 isn't running.
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] POST_SEEK_TICK cd={} t+{} "
                    "P1ring@M-1:[idx={} in=0x{:X}] "
                    "@M:[idx={} in=0x{:X}] "
                    "@M+1:[idx={} in=0x{:X}]  "
                    "P2ring@M-1:[idx={} in=0x{:X}] "
                    "@M:[idx={} in=0x{:X}] "
                    "@M+1:[idx={} in=0x{:X}]\n"),
                countdown_now, t_after_seek,
                p1_ring_idx[0], p1_ring_in[0],
                p1_ring_idx[1], p1_ring_in[1],
                p1_ring_idx[2], p1_ring_in[2],
                p2_ring_idx[0], p2_ring_in[0],
                p2_ring_idx[1], p2_ring_in[1],
                p2_ring_idx[2], p2_ring_in[2]);

            // Demo netdriver state every 30 ticks (~0.5s) during the
            // post-seek window so we can see how DemoCurrentTime
            // evolves IF it ever exists.  We expect this to stay null
            // in SC6 (UDemoNetDriver hypothesis was disproven 2026-05-12
            // testing); kept in for completeness in case it shows up
            // in some other game mode.
            if ((countdown_now % 30) == 0)
            {
                ReplayScrubDiag::DemoNetDriverSnap d =
                    ReplayScrubDiag::read_demo_net_driver();
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.diag] POST_SEEK_TICK cd={} t+{} "
                        "Demo[ptr=0x{:X} cur={:.3f}s tot={:.3f}s "
                        "frame={} play={} rec={}]\n"),
                    countdown_now, t_after_seek,
                    d.driver_ptr, d.demo_cur_time, d.demo_total_time,
                    d.demo_frame_num,
                    d.bIsPlaying ? 1 : 0, d.bIsRecording ? 1 : 0);
            }

            const bool p1_changed = p1.changed_from(m_last_movevm_p1);
            const bool p2_changed = p2.changed_from(m_last_movevm_p2);

            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.diag] POST_SEEK_TICK cd={} t+{} "
                    "P1[{} moveID=0x{:X} pos=({:.1f},{:.1f},{:.1f}) "
                    "atkCell=0x{:X}] "
                    "P2[{} moveID=0x{:X} pos=({:.1f},{:.1f},{:.1f}) "
                    "atkCell=0x{:X}]\n"),
                countdown_now, t_after_seek,
                RC::to_generic_string(p1_changed ? "CHG" : "---"),
                static_cast<unsigned>(p1.current_move_id),
                p1.pos_x, p1.pos_y, p1.pos_z, p1.active_attack_cell,
                RC::to_generic_string(p2_changed ? "CHG" : "---"),
                static_cast<unsigned>(p2.current_move_id),
                p2.pos_x, p2.pos_y, p2.pos_z, p2.active_attack_cell);

            m_last_movevm_p1 = p1;
            m_last_movevm_p2 = p2;
            m_diag_last_wall = static_cast<uint32_t>(wall);
            m_diag_last_master = master;
        }

        // Service any deferred-seek request the UI posted.  Called
        // from the same cockpit pre-tick that runs tick_capture().
        // Doing this inside the cockpit hook (instead of from the UI
        // thread) ensures the ExecFinalizeAndPost call always lands
        // on the game thread between PerFrameTicks.
        void service_seek_request()
        {
            // Resize first - if a resize lands this tick the ring's
            // capacity changes and any seek target from before the
            // resize is stale anyway, so process resize, then seek.
            service_resize_request();
            if (!is_initialized()) return;
            const int32_t target = m_seek_request.exchange(
                kSeekIdle, std::memory_order_acq_rel);
            if (target == kSeekIdle) return;
            do_seek_to_seq(target);
        }

        // Post a resize request from the UI (render thread).  The
        // request is consumed on the next cockpit pre-tick (game
        // thread) so the actual ring teardown / realloc happens on a
        // single thread without racing capture / seek.
        void request_resize(size_t ring_frames) noexcept
        {
            if (ring_frames < kMinRingFrames) ring_frames = kMinRingFrames;
            if (ring_frames > kMaxRingFrames) ring_frames = kMaxRingFrames;
            m_pending_resize_frames.store(
                ring_frames, std::memory_order_release);
        }

        // ---- Diagnostic UI accessors -------------------------------
        bool verbose_diag() const noexcept
        {
            return m_verbose_diag.load(std::memory_order_acquire);
        }
        void set_verbose_diag(bool v) noexcept
        {
            m_verbose_diag.store(v, std::memory_order_release);
        }

        // UI "Force diagnostic dump" button.  Triggers a single
        // ReplayScrubDiag::dump_full on the next cockpit tick.  Works
        // even when verbose_diag is OFF (it's an on-demand snapshot).
        void request_force_diag() noexcept
        {
            m_force_diag_request.store(true, std::memory_order_release);
        }

        // Write ALuxBattleReplayPlayer.CurrentTime/CurrentRound/
        // bIsPlayingBack on each seek via DIRECT BYTE WRITES at the
        // verified Ghidra struct offsets.  Default ON (2026-05-14): the
        // 21:47 session log confirmed this write is what actually
        // rewinds the engine's replay playback head and makes recorded
        // inputs route to the chara post-seek.  Without it, BM_input
        // stays at 0 every tick post-seek and chars finish current
        // move then idle.  Caveat: target_round is hardcoded to 0.
        bool use_replay_player_seek() const noexcept
        {
            return m_use_replay_player_seek.load(std::memory_order_acquire);
        }
        void set_use_replay_player_seek(bool v) noexcept
        {
            m_use_replay_player_seek.store(v, std::memory_order_release);
        }

        // 2026-05-15 granular debug toggles (ultrathink session).
        bool enable_chara_4400_restore() const noexcept
        {
            return m_enable_chara_4400_restore.load(std::memory_order_acquire);
        }
        void set_enable_chara_4400_restore(bool v) noexcept
        {
            m_enable_chara_4400_restore.store(v, std::memory_order_release);
        }

        bool force_pra_forward_bit_on_seek() const noexcept
        {
            return m_force_pra_forward_bit_on_seek.load(std::memory_order_acquire);
        }
        void set_force_pra_forward_bit_on_seek(bool v) noexcept
        {
            m_force_pra_forward_bit_on_seek.store(v, std::memory_order_release);
        }

        bool force_isplayingback_on_seek() const noexcept
        {
            return m_force_isplayingback_on_seek.load(std::memory_order_acquire);
        }
        void set_force_isplayingback_on_seek(bool v) noexcept
        {
            m_force_isplayingback_on_seek.store(v, std::memory_order_release);
        }

        bool enable_speculative_restore() const noexcept
        {
            return m_enable_speculative_restore.load(std::memory_order_acquire);
        }
        void set_enable_speculative_restore(bool v) noexcept
        {
            m_enable_speculative_restore.store(v, std::memory_order_release);
        }

        // [render thread] "Generate timeline" UI requests.  The UI runs
        // on the render/present thread; these post an atomic request
        // that tick_generate_timeline() services on the game thread (the
        // same request/handoff pattern as request_seek / request_resize).
        void request_generate_timeline() noexcept
        {
            m_gen_request.store(kGenReqStart, std::memory_order_release);
        }
        void request_stop_generate_timeline() noexcept
        {
            m_gen_request.store(kGenReqStop, std::memory_order_release);
        }

        // "Generate timeline" state accessor (2026-05-16) - the UI
        // reads this to choose the button label / status text.
        TimelineGenState timeline_gen_state() const noexcept
        {
            return static_cast<TimelineGenState>(
                m_timeline_gen_state.load(std::memory_order_acquire));
        }

        // [game thread] Begin "Generate timeline": remove SC6's engine
        // frame-rate cap so the replay fast-forwards, with tick_capture()
        // filling the snapshot ring as it plays.  Called only by
        // tick_generate_timeline() servicing a UI request - never from
        // the render thread directly.  No-op (with a log line) unless
        // we're in the Replay viewer with the ring ready and capture on.
        void start_generate_timeline() noexcept
        {
            if (m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating))
                return;   // already running

            if (!is_initialized())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline ignored - snapshot "
                    "ring not initialised yet\n"));
                return;
            }
            if (GameMode::instance().current_presence()
                != GamePresence::Replay)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline ignored - not in "
                    "the Replay viewer\n"));
                return;
            }
            // Generation needs the replay PLAYING - tick_capture() skips
            // capture while paused.  Drop any pause/scrub state.  (The
            // passive-capture toggle is NOT required: tick_capture()
            // captures whenever a generation pass is running.)
            m_paused.store(false, std::memory_order_release);

            if (!m_frame_cap.engage())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline failed - could not "
                    "remove the engine frame cap\n"));
                return;
            }
            // Best-effort: drop the render resolution so the uncapped
            // loop is sim-bound rather than render-bound (SC6 replay
            // viewing is render-limited well below 60fps, so the frame
            // cap alone does not fast-forward).  Generation still runs
            // if this fails - just render-limited.
            m_screen_pct.engage();

            // Re-generate support: discard any previous timeline so this
            // pass starts from an EMPTY ring.  Without this, re-generating
            // over an already-full ring would trip the ring-full auto-stop
            // (tick_generate_timeline) on its very first tick and capture
            // nothing.  Placed AFTER every early-return check above, so a
            // generation that fails to start (not initialised / not in
            // Replay / frame-cap engage failed) never destroys the
            // existing timeline.  drop_ring() is the cheap logical-empty
            // (no multi-hundred-MB memset); m_last_seek_target is cleared
            // too so the playhead doesn't park on a stale seq carried over
            // from the discarded timeline.
            drop_ring();
            m_last_seek_target.store(-1, std::memory_order_release);

            const int32_t m = read_engine_master_clock();
            m_timeline_gen_start_master.store(m, std::memory_order_release);
            m_timeline_gen_last_master.store(m, std::memory_order_release);
            m_gen_last_round = read_current_round();
            m_gen_max_round  = m_gen_last_round;
            const auto now = std::chrono::steady_clock::now();
            m_gen_started_at   = now;
            m_gen_last_advance = now;
            m_timeline_gen_state.store(
                static_cast<int>(TimelineGenState::Generating),
                std::memory_order_release);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] Generate timeline STARTED - frame cap "
                "removed; replay fast-forwarding (master={})\n"), m);
        }

        // [game thread] End "Generate timeline": restore the frame cap
        // and set the end state.  reached_end=true -> Done (the timeline
        // covers a full pass); false -> Idle (cancelled before the end).
        void stop_generate_timeline(const char* reason,
                                    bool reached_end) noexcept
        {
            const bool was_generating =
                m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating);

            m_frame_cap.disengage();
            m_screen_pct.disengage();
            m_timeline_gen_state.store(
                static_cast<int>(reached_end ? TimelineGenState::Done
                                             : TimelineGenState::Idle),
                std::memory_order_release);

            if (was_generating)
            {
                const int32_t start = m_timeline_gen_start_master.load(
                    std::memory_order_acquire);
                const int32_t last = m_timeline_gen_last_master.load(
                    std::memory_order_acquire);
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] Generate timeline {} ({}) - advanced "
                    "{} replay frames; ring now holds {}\n"),
                    RC::to_generic_string(
                        reached_end ? "COMPLETE" : "stopped"),
                    RC::to_generic_string(reason ? reason : "?"),
                    (last >= start) ? (last - start) : 0,
                    ring_count());
            }
        }

        // [game thread] Per-cockpit-tick driver for "Generate timeline".
        // Called every cockpit pre-tick (next to tick_capture).
        //
        // Responsibilities:
        //   1. Service the UI's start/stop request (render -> game
        //      thread handoff via m_gen_request).
        //   2. While Generating: watch the replay master clock and
        //      auto-stop when the replay reaches its end, when the user
        //      leaves the Replay viewer or disables capture, or after a
        //      hard wall-clock safety limit.
        //   3. Safety net: if the frame cap is somehow engaged while we
        //      are NOT Generating, restore it.
        //
        // The fast-forward itself needs no per-tick work here - removing
        // the engine frame cap (FrameCapOverride) makes UE4's own loop
        // run faster, and tick_capture() fills the ring exactly as in 1x
        // playback.  This function only starts/stops that and detects
        // the end of the recording.
        void tick_generate_timeline() noexcept
        {
            // ---- service UI request (render thread -> game thread) ----
            const int req = m_gen_request.exchange(
                kGenReqNone, std::memory_order_acq_rel);
            if (req == kGenReqStart)
                start_generate_timeline();
            else if (req == kGenReqStop)
                stop_generate_timeline("user", false);

            const bool generating =
                m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating);

            if (!generating)
            {
                // The frame cap must never stay removed while we are not
                // actively generating.  If any path cleared the gen
                // state without disengaging, fix it here so a single
                // cockpit tick always restores SC6's 60fps cap.
                if (m_frame_cap.is_engaged() || m_screen_pct.is_engaged())
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] frame cap / screen-pct engaged "
                        "outside generation - restoring\n"));
                    m_frame_cap.disengage();
                    m_screen_pct.disengage();
                }
                return;
            }

            // ---- auto-stop checks --------------------------------------
            const auto now = std::chrono::steady_clock::now();

            // Hard wall-clock safety limit: even a long replay at 1x is
            // well under this; exceeding it means something stalled.
            if (std::chrono::duration<double>(now - m_gen_started_at)
                    .count() > kGenMaxSeconds)
            {
                // Hard ceiling hit - this is an abnormal stall, not a
                // normal completion, so end as Idle (reached_end=false)
                // rather than reporting a misleading "Done".
                stop_generate_timeline("safety-timeout", false);
                return;
            }
            // Left the Replay viewer.  on_presence_change() usually beats
            // us to this, but cover the gap.
            if (GameMode::instance().current_presence()
                != GamePresence::Replay)
            {
                stop_generate_timeline("left-replay", false);
                return;
            }

            // Ring full: generation's job is to FILL the snapshot ring.
            // Once it holds ring_frames() snapshots every further capture
            // just evicts an older one (the ring wraps), so a full ring
            // already IS a complete timeline - stop here.  This is the
            // primary stop for replays at least as long as the ring; the
            // loop / end-of-recording checks below cover shorter replays
            // whose content never fills it.  Without this the 2026-05-16
            // test ran generation ~13.5 s (~2.2 ring-fills) past the end
            // until the user stopped it by hand.
            if (ring_count() >= ring_frames())
            {
                stop_generate_timeline("ring-full", true);
                return;
            }

            // ---- multi-round-aware progress / end detection ----------
            // The replay master clock advances per UE4 tick but REBASES
            // toward 0 at each round boundary (the engine zeroes
            // per-round state), so a master rollback is NOT "replay
            // ended" - it is usually just the next round starting.
            // CurrentRound is the reliable cross-round signal: it climbs
            // 0,1,2,... through the match and only drops on a loop /
            // restart.
            const int32_t master = read_engine_master_clock();
            const int32_t round  = read_current_round();
            const int32_t last_master =
                m_timeline_gen_last_master.load(std::memory_order_acquire);
            const int32_t last_round = m_gen_last_round;

            // Track the highest round reached this pass.  Rounds only
            // climb during forward play, so a later round below this max
            // is an unambiguous loop/restart signal.
            if (round > m_gen_max_round) m_gen_max_round = round;

            const bool round_known =
                (round >= 0 && last_round >= 0);
            const bool round_advanced = round_known && round > last_round;
            // Replay looped/restarted: the round index dropped below the
            // highest round reached this pass.  Keying off the running
            // max (not just last_round) makes this robust to a single
            // missed transition tick - a bare round<last_round test
            // missed the loop whenever CurrentRound read -1 on the loop
            // frame, so generation ran on past the end until stopped by
            // hand.
            const bool round_looped =
                (round >= 0 && m_gen_max_round >= 0
                 && round < m_gen_max_round);
            const bool master_advanced =
                (master >= 0 && last_master >= 0 && master > last_master);
            const bool master_rolled_back =
                (master >= 0 && last_master >= 0 && master < last_master);

            // Replay looped / restarted - one full pass is captured, so
            // stop.  Two forms:
            //   * multi-round replay: the round index jumped backward;
            //   * single-round replay looping: the master clock rolled
            //     back WITHOUT a round bump (the round stays put but the
            //     clock rebases to 0 for the re-play).
            // A master rollback that coincides with a round bump is just
            // the next round starting - NOT a loop.  The master-rollback
            // form also requires round_known: if CurrentRound was
            // momentarily unreadable this tick we cannot distinguish a
            // loop from a round boundary, so we do not stop on it (a
            // genuine loop is re-detected on a later tick, or caught by
            // the stuck timer / 120s ceiling).
            if (round_looped
                || (master_rolled_back && round_known && !round_advanced))
            {
                stop_generate_timeline("replay-looped", true);
                return;
            }

            // "Alive" = progress since the last tick, by either the
            // master clock climbing OR a new round starting (at a round
            // boundary the master clock rebases, so a round bump is
            // progress on its own).
            if (master_advanced || round_advanced)
                m_gen_last_advance = now;

            if (master >= 0)
                m_timeline_gen_last_master.store(
                    master, std::memory_order_release);
            if (round >= 0)
                m_gen_last_round = round;

            // End-of-recording: neither clock advanced for
            // kGenStuckSeconds (the replay stopped feeding frames).  A
            // momentarily-unreadable tick (master/round = -1) just fails
            // to refresh the timer rather than stopping outright, so one
            // transient bad read can't trigger a false "Done".
            if (std::chrono::duration<double>(now - m_gen_last_advance)
                    .count() > kGenStuckSeconds)
            {
                stop_generate_timeline("end-of-recording", true);
                return;
            }
        }

        // ---- UI / settings -------------------------------------------

        // Master capture toggle.  ON: capture every Replay-presence
        // frame.  OFF: ring is frozen at whatever's already there;
        // useful while reviewing without overwriting older content.
        void  set_capture_enabled(bool v) noexcept
        {
            m_capture_enabled.store(v, std::memory_order_release);
        }
        bool  capture_enabled() const noexcept
        {
            return m_capture_enabled.load(std::memory_order_acquire);
        }

        // Request a seek to timeline position `target_seq` (a capture
        // sequence - see m_seq_tags).  Doesn't actually seek until the
        // next cockpit tick (see service_seek_request); the UI thread
        // just posts the request via this atomic.
        void request_seek(int32_t target_seq) noexcept
        {
            m_seek_request.store(target_seq, std::memory_order_release);
        }

        // Cancel any pause/scrub state - world resumes at the next
        // cockpit tick.  Equivalent to clicking Play.
        void cancel_scrub() noexcept
        {
            m_paused.store(false, std::memory_order_release);
        }

        // ---- Pause / play / drag state model -------------------------
        //
        // The world is frozen iff `m_paused` is true.  Three things flip
        // it:
        //   * Play / Pause button toggles it directly
        //   * Drag-start auto-sets it (= "freeze while I'm scrubbing")
        //   * Drag-end conditionally clears it (= "auto-resume on release"
        //     toggle controls whether release flips back to playing)
        //
        // dllmain's frame_step_apply ORs `is_paused()` into its existing
        // freeze decision, so the WorldTickGate machinery that already
        // handles m_freeze_frame works for us transparently.

        bool is_paused() const noexcept
        {
            return m_paused.load(std::memory_order_acquire);
        }
        void set_paused(bool v) noexcept
        {
            m_paused.store(v, std::memory_order_release);
        }

        // Engage pause AND anchor the playhead at the current live
        // edge.  Used by the Pause button: without anchoring, a
        // play->pause transition would leave m_last_seek_target
        // pointing at whatever the user PREVIOUSLY seeked to (possibly
        // many seconds back), and the UI's playhead would jump
        // backward.  Setting m_last_seek_target = latest_seq() makes
        // current_play_position() report the live edge while paused.
        // Idempotent if called while already paused.
        void mark_paused_at_live() noexcept
        {
            const int32_t L = latest_seq();
            if (L >= 0)
                m_last_seek_target.store(L, std::memory_order_release);
            m_paused.store(true, std::memory_order_release);
        }

        // Back-compat alias - dllmain's frame_step_apply still calls
        // is_scrub_active() to decide whether to engage freeze.
        // Equivalent to is_paused() under the simplified model.
        bool is_scrub_active() const noexcept { return is_paused(); }

        bool auto_resume_on_release() const noexcept
        {
            return m_auto_resume_on_release.load(std::memory_order_acquire);
        }
        void set_auto_resume_on_release(bool v) noexcept
        {
            m_auto_resume_on_release.store(v, std::memory_order_release);
        }

        // Called by the UI when the user grabs the timeline playhead.
        // Always engages pause - the world freezes for the duration of
        // the drag regardless of any other state.
        void on_drag_start() noexcept
        {
            m_paused.store(true, std::memory_order_release);
        }

        // Called by the UI when the user releases the playhead.  When
        // auto-resume is on (default), the world un-freezes and replay
        // playback continues from the seeked frame (the cursor sync
        // we wrote in write_replay_cursors makes the resume clean).
        // When auto-resume is off, the world stays frozen and the user
        // must click Play to resume.
        void on_drag_end() noexcept
        {
            if (m_auto_resume_on_release.load(std::memory_order_acquire))
                m_paused.store(false, std::memory_order_release);
        }

        // ---- Current playhead position --------------------------------
        //
        // Returns the timeline seq the playhead should display:
        //   * Paused on a seeked snapshot       -> that seek's seq.
        //   * Live-capturing (passive capture
        //     on, or a generation pass running) -> latest_seq(): the
        //     newest snapshot IS the current frame.
        //   * Reviewing a finished timeline     -> the last seek's seq.
        //
        // The third case is the one the 2026-05-16 test exposed.  After
        // a "Generate timeline" pass capture is OFF, so latest_seq() is
        // FROZEN at the end of the generated range - returning it would
        // pin the playhead to the timeline's right edge no matter where
        // the user scrubbed (the reported "playhead only snaps to one
        // end" bug).  With capture off, the last seek target is the best
        // position estimate we have, so the playhead parks where the
        // user left it instead of jumping.
        //
        // (An earlier design extrapolated "seek_seq + master-clock delta"
        // during post-seek playback; that stalled the playhead for a
        // whole round whenever playback crossed a round boundary - the
        // master clock rebases toward 0 there - so it was removed.  A
        // capture-free playhead that ADVANCES during post-seek playback
        // would need a round-immune frame counter; deferred.)
        //
        // Returns -1 if no useful position is available (no captures).
        int32_t current_play_position() const noexcept
        {
            const int32_t seeked =
                m_last_seek_target.load(std::memory_order_acquire);

            // Paused on a seeked snapshot: park the playhead there.
            if (is_paused() && seeked >= 0) return seeked;

            // Live capture running (passive capture, or a generation
            // pass): the newest snapshot tracks the current frame.
            const bool capturing =
                m_capture_enabled.load(std::memory_order_acquire) ||
                (m_timeline_gen_state.load(std::memory_order_acquire)
                 == static_cast<int>(TimelineGenState::Generating));
            if (capturing) return latest_seq();

            // Reviewing a finished timeline: latest_seq() is frozen and
            // unrelated to playback, so park at the last seek target.
            if (seeked >= 0) return seeked;
            return latest_seq();
        }

        // Most-recent successful seek target (the frame the engine is
        // actually parked on after restore).  -1 if none yet.
        int32_t last_seek_target() const noexcept
        {
            return m_last_seek_target.load(std::memory_order_acquire);
        }

        // Shared "this is a different replay/match now" reset: drop the
        // snapshot ring, cancel any pending seek, exit scrub mode,
        // restore the engine frame cap and reset timeline-generation
        // state.  Cockpit-thread only - safe to mutate without locks.
        //
        // Called from two places:
        //   * on_presence_change()  - a scene-presence transition.
        //   * tick_capture()        - a new replay was loaded while the
        //     presence value stayed 'Replay'.  This matters because the
        //     replay BROWSER and replay PLAYBACK share one presence
        //     value (Replay), so closing one replay and opening another
        //     produces NO presence transition - on_presence_change()
        //     never fires for a replay->replay swap.
        void reset_for_new_replay(const char* reason) noexcept
        {
            const size_t  cnt_before = m_count.load(std::memory_order_relaxed);
            const int32_t last_seek  =
                m_last_seek_target.load(std::memory_order_relaxed);
            drop_ring();
            m_have_last_counter      = false;
            m_last_bm_obj            = nullptr;
            m_last_replay_player_obj = nullptr;
            m_last_round             = -1;
            // Drop the throttled GlobalPtr caches: the BM and
            // ReplayPlayer actors are torn down / replaced across this
            // reset, so force the next get() to re-resolve immediately
            // rather than serve the stale pointer until GlobalPtr's
            // revalidation timer next fires.
            m_bm_ptr.invalidate();
            ReplayScrubDiag::replay_player_ptr().invalidate();
            m_paused.store(false, std::memory_order_release);
            m_seek_request.store(kSeekIdle, std::memory_order_release);
            m_last_seek_target.store(-1, std::memory_order_release);
            m_last_seek_master_tag.store(-1, std::memory_order_release);
            m_live_master_cached.store(-1, std::memory_order_release);
            m_post_seek_countdown.store(0, std::memory_order_release);
            // Cancel any in-progress timeline generation - restore the
            // engine frame cap + screen percentage and reset the gen
            // state so the button shows "Generate timeline" again for
            // the next replay.
            m_frame_cap.disengage();
            m_screen_pct.disengage();
            m_timeline_gen_state.store(
                static_cast<int>(TimelineGenState::Idle),
                std::memory_order_release);
            m_gen_request.store(kGenReqNone, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub] reset_for_new_replay ({}): dropped {} "
                    "captures, cleared paused/seek/gen state "
                    "(last_seek={})\n"),
                RC::to_generic_string(reason ? reason : "?"),
                cnt_before, last_seek);
        }

        // External signal: scene presence changed.  Any captured
        // snapshots are from a different match (or no match) now -
        // delegate to the shared reset.  Same-thread caller (cockpit
        // pre-tick).
        void on_presence_change() noexcept
        {
            reset_for_new_replay("presence change");
        }

        // ---- UI accessors --------------------------------------------

        size_t ring_frames() const noexcept { return m_ring_frames; }
        size_t ring_count()  const noexcept
        {
            return m_count.load(std::memory_order_relaxed);
        }
        int32_t live_frame() const noexcept
        {
            uint32_t c = 0;
            if (read_frame_counter(c)) return static_cast<int32_t>(c);
            return -1;
        }

        // Timeline coordinate accessors.  The canonical timeline coord
        // is the per-snapshot monotonic capture sequence (m_seq_tags) -
        // unique, gap-free, monotonic across the WHOLE match including
        // round boundaries.  earliest/latest return the seq of the
        // oldest / newest snapshot still in the ring, or -1 if empty.
        //
        // Race-protection check (unified 2026-05-13 audit):
        //   free_ring() runs on the game thread and tears down the ring
        //   in this order: m_count=0 (release) -> m_head=0 ->
        //   m_ring_frames=0 (non-atomic) -> tag vectors cleared.  The UI
        //   thread can observe these out of order, so the
        //   `cap > m_seq_tags.size()` check catches the window where
        //   m_ring_frames still reads N but the vector is already empty.
        int32_t earliest_seq() const noexcept
        {
            const size_t cap  = m_ring_frames;
            const size_t cnt  = ring_count();
            if (cap == 0 || cnt == 0 || cap > m_seq_tags.size()) return -1;
            const size_t head = m_head.load(std::memory_order_relaxed);
            const size_t idx  = (head + cap - cnt) % cap;
            return m_seq_tags[idx].load(std::memory_order_relaxed);
        }
        int32_t latest_seq() const noexcept
        {
            const size_t cap  = m_ring_frames;
            const size_t cnt  = ring_count();
            if (cap == 0 || cnt == 0 || cap > m_seq_tags.size()) return -1;
            const size_t head = m_head.load(std::memory_order_relaxed);
            const size_t idx  = (head + cap - 1) % cap;
            return m_seq_tags[idx].load(std::memory_order_relaxed);
        }

        // Resolve a timeline seq to its captured (round, within-round
        // wall frame) for the UI's round-aware time display.  Returns
        // false if the seq isn't held in the ring.
        bool seq_tag_info(int32_t seq, int32_t& out_round,
                          int32_t& out_wall) const noexcept
        {
            const int32_t slot = find_slot_for_seq(seq);
            if (slot < 0) return false;
            out_round = m_round_tags[static_cast<size_t>(slot)]
                          .load(std::memory_order_acquire);
            out_wall  = m_frame_tags[static_cast<size_t>(slot)]
                          .load(std::memory_order_acquire);
            return true;
        }

        // Collect the round-boundary markers for the timeline bar: one
        // entry per round present in the ring, each giving the seq of
        // that round's first captured snapshot.  The ring is FIFO and
        // seq is monotonic-by-capture, so a FIFO walk is already
        // seq-ascending - no sort needed.  Cheap; called once per UI
        // render.
        std::vector<RoundMarker> collect_round_markers() const
        {
            std::vector<RoundMarker> out;
            const size_t cap = m_ring_frames;
            const size_t cnt = ring_count();
            if (cap == 0 || cnt == 0 || cap > m_seq_tags.size()
                || cap > m_round_tags.size())
                return out;
            const size_t head = m_head.load(std::memory_order_relaxed);
            int32_t prev_round = -0x7fffffff;
            for (size_t k = 0; k < cnt; ++k)
            {
                const size_t idx = (head + cap - cnt + k) % cap;
                const int32_t s =
                    m_seq_tags[idx].load(std::memory_order_acquire);
                if (s < 0) continue;
                const int32_t r =
                    m_round_tags[idx].load(std::memory_order_acquire);
                if (r != prev_round)
                {
                    out.push_back(RoundMarker{ s, r });
                    prev_round = r;
                }
            }
            return out;
        }

    private:
        ReplayScrub() = default;
        // shutdown() (not just free_ring()) so a hot-unload mid-
        // generation can't leave the engine frame cap removed / the
        // screen percentage dropped with no mod left to restore them.
        ~ReplayScrub() { shutdown(); }
        ReplayScrub(const ReplayScrub&) = delete;
        ReplayScrub& operator=(const ReplayScrub&) = delete;

        // Sentinel for "no seek pending".
        static constexpr int32_t kSeekIdle = -1;

        // Engine entry points.  Resolved via image base + RVA.
        using ExecWriteFn = void* (__fastcall*)(HgCpuBufferShim*);
        using ExecReadFn  = void* (__fastcall*)(HgCpuBufferShim*);

        ExecWriteFn m_exec_write {nullptr};
        ExecReadFn  m_exec_read  {nullptr};
        const void* m_frame_counter_addr {nullptr};

        // Single shim, re-targeted onto each ring slot in turn.  The
        // engine treats the buffer as opaque so we can repurpose it
        // freely between calls.  Storing it as a member rather than
        // on-stack means the vtable pointer's lifetime matches the
        // tracker's, which matches the ring's.
        HgCpuBufferShim m_shim {};

        // Cached BM lookup for the post-restore replay-cursor write.
        // GlobalPtr::get() throttles its O(N) revalidation scan (see
        // HorseLib.hpp); reset_for_new_replay() invalidate()s this on a
        // replay swap / presence change so a torn-down BM is re-resolved
        // promptly rather than at the next throttle tick.
        GlobalPtr m_bm_ptr {};

        // Engine field offsets used by the cursor-sync write +
        // diagnostic dump.  Verified against Ghidra structs
        // ALuxBattleManager_Partial and ALuxBattleFrameInputLog (see
        // project_sc6_replay_scrub_design.md and
        // project_sc6_hgcpu_buffer_contract.md memory entries).
        // pBM->pReplayDataBlock @ BM+0x460.  Indirects to a
        // FLuxReplayDataBlock (1021 bytes per Ghidra struct), which
        // holds the Stage 1 decoder state - file read cursor, decoded
        // buffer write cursor, Stage 2's read cursor, working frame
        // ID, working playback cursor, running flag, etc.
        //
        // CRITICAL FOR SCRUB-BACK (2026-05-11 finding): when the user
        // pauses at frame F and scrubs back to frame T (F > T+1), the
        // engine's decoder is still at file-position F.  Our
        // HgCpuDirect restore sets chara state to T, our InputLog
        // restore brings IL fields back, but the DECODER's read
        // cursor (llDecodedBufferReadCursor @ rdb+0x3C0) is still
        // pointing at byte F*8 in the decoded buffer.  Whatever
        // mechanism consumes decoded packets next will fetch packets
        // for frames F+1, F+2, ... onto the just-restored state-at-T
        // - which is exactly the "plays inputs from later in the
        // round" symptom the user reported.
        //
        // Capture/restore the full 1021 bytes of pReplayDataBlock per
        // snapshot.  This includes pointer fields (pDecodedPacketBuffer
        // @ +0x3A8, pFileBuffer @ +0x3C8, pVerifyPtr @ +0x3A0); those
        // are stable for the replay-session's lifetime so
        // overwriting them with their captured-time values is
        // effectively a no-op (same address).  Cross-session
        // mismatches are not a concern because on_presence_change
        // clears the ring before any new session can start.
        static constexpr uintptr_t kBM_pReplayDataBlock_Off       = 0x460;
        static constexpr size_t    kRDB_Bytes                     = 1021;
        static constexpr uintptr_t kBM_BattleFrameInputLog_Off    = 0x478;
        static constexpr uintptr_t kBM_pReplayCharaSnapshot_Off   = 0x1360;
        static constexpr uintptr_t kBM_bMoveStateByte_Off         = 0x1463;
        static constexpr uintptr_t kBM_nReplayLastFrameID_Off     = 0x1488;
        static constexpr uintptr_t kBM_nReplayLastApplied_Off     = 0x148C;
        static constexpr uintptr_t kBM_nFrameAdvanceCounter_Off   = 0x1490;
        static constexpr uintptr_t kIL_dwPlaybackCursor_Off       = 0x39C;
        static constexpr uintptr_t kIL_nLastFrameID_Off           = 0x3A0;
        static constexpr uintptr_t kIL_nMasterClock_Off           = 0x3A4;
        static constexpr uintptr_t kIL_nTotalRecordedFrames_Off   = 0x3B0;
        // Per-player input cache - rolling 512-entry window of recorded
        // inputs.  Read by LuxBattleManager_GetCachedRoundValue_ByIndex
        // @ 0x1403F0720.  Lives WITHIN the kIL_CaptureStart_Off..
        // kIL_CaptureEnd_Off window below, so it's snapshotted as part
        // of the broader InputLog state capture.  Kept here for the
        // diagnostic dump's slot-by-slot read of cache contents.
        //
        // Layout: [2 players][512 entries][16-byte FLuxReplayInputCacheEntry]
        //   per entry: int FrameID, uint FrameIndex, uint InputValue, uint Aux
        // Offset:      pInputLog + 0x3C0 + (frameIndex & 0x1FF) * 0x10
        //                                + playerIdx * 0x2000
        // Total size:  0x4000 bytes (16 KB).
        static constexpr uintptr_t kIL_InputCacheStart_Off        = 0x3C0;
        static constexpr uintptr_t kIL_bDoubleTickGuard_Off       = 0x4404;
        static constexpr uintptr_t kIL_nDrainCursor_Off           = 0x4410;

        // FULL InputLog replay-state capture window.
        //
        // 2026-05-11 finding: restoring only the +0x3C0..+0x43C0
        // input-cache window didn't fix backward-seek playback - the
        // cache holds backward-looking entries that match the engine
        // for frame K-1 but not for K, K+1, ... so playback halts on
        // the second frame.  And BASELINE logs show
        // BM[lastApplied==master] universally, meaning the
        // SimulationLoop catch-up loop (which reads the cache via
        // GetCachedRoundValue_ByIndex) isn't even running during
        // normal forward playback - inputs must reach the chara via
        // a DIFFERENT InputLog-driven path.
        //
        // We don't yet know that path's exact mechanism, so we
        // capture the WHOLE InputLog state (from the first replay-
        // related field at +0x394 through the last documented field
        // at +0x4414) and restore it verbatim on seek.  This covers
        // any hidden bookkeeping the engine maintains internally and
        // matches the snapshot-restore approach we already use for
        // chara/global state via HgCpuDirect.
        //
        // Start at +0x394 (= dwForwardReverseBitfield, the first
        // replay-related field per the Ghidra struct).  Avoids the
        // UObject header and the UE4Component pointer at +0x388,
        // which are engine-managed - touching them risks breaking GC
        // identity / component graph.
        //
        // End at +0x4418 (exclusive = nMinStoreFrameIndex+4).  Range
        // covers the full replay-pipeline state span:
        //   +0x394  dwForwardReverseBitfield
        //   +0x398  bEnable
        //   +0x39C  dwPlaybackCursor
        //   +0x3A0  nLastFrameID
        //   +0x3A4  nMasterClock
        //   +0x3A8  pRecordedFrameBuffer  (pointer-skipped on restore)
        //   +0x3B0  nTotalRecordedFrames
        //   +0x3C0..+0x43BF  FLuxReplayInputCacheEntry[1024]
        //   +0x4400  dwOnlineActive
        //   +0x4404  bDoubleTickGuard
        //   +0x4410  nDrainCursor
        //   +0x4414  nMinStoreFrameIndex (added 2026-05-13 audit)
        // Total: 0x4418 - 0x394 = 0x4084 = 16,516 bytes per slot.
        // At 3600 slots ~57 MB extra.  Excludes online-only deque +
        // critical-section state at 0x4424+.
        static constexpr uintptr_t kIL_CaptureStart_Off = 0x394;
        static constexpr uintptr_t kIL_CaptureEnd_Off   = 0x4418;
        static constexpr size_t    kIL_CaptureBytes     =
            kIL_CaptureEnd_Off - kIL_CaptureStart_Off;
        // Per-chara replay cursors that live OUTSIDE the
        // WriteCharaStateToSnapshot regions (so they are NOT restored
        // by ExecFinalizeAndPost and must be written by us):
        static constexpr uintptr_t kChara_nReplayLookupKey_Off    = 0x43F4;
        static constexpr uintptr_t kChara_nReplayEnableFlag_Off   = 0x4400;
        static constexpr uintptr_t kChara_nReplayFrameOffset_Off  = 0x440C;
        static constexpr uintptr_t kChara_nReplayFrameTotal_Off   = 0x4410;
        static constexpr uintptr_t kChara_nReplayFrameTarget_Off  = 0x4414;
        static constexpr uintptr_t kChara_nReplayConsumerCursor_Off = 0x4420;
        static constexpr uintptr_t kChara_bCharaMode_Off          = 0x4424;
        // Chara-side cursors that ARE inside the snapshot regions (so
        // ExecFinalizeAndPost already wrote them) - dumped for
        // diagnostic comparison so we can verify the restore actually
        // did what we expected:
        static constexpr uintptr_t kChara_nReplayCursor_Off       = 0x39C;
        static constexpr uintptr_t kChara_nReplayLastFrameID_Off  = 0x3A0;
        static constexpr uintptr_t kChara_nReplayMasterClock_Off  = 0x3A4;
        static constexpr uintptr_t kChara_nInputCursorRing_Off    = 0x3B0;
        static constexpr uintptr_t kChara_nReplayFrameCount_Off   = 0x3B4;

        // RVAs for the chara slot pointers (used to walk both charas
        // when we sync the per-chara cursors).
        static constexpr uintptr_t kRVA_CharaSlotP1 = 0x470DE90;
        static constexpr uintptr_t kRVA_CharaSlotP2 = 0x470DE98;

        // BM internal-state byte offsets that gate SimulationLoop +
        // chara tick.  HgCpuDirect doesn't restore the BM actor's
        // internal bytes; without restoring these, post-round bytes
        // can persist after a backward seek and gate chara forward
        // play OFF.
        //
        //   +0x12F3  bEnginePauseFlag  (SimulationLoop early-return when nonzero)
        //   +0x1461  bMainStateMachineByte
        //   +0x1463  bMoveStateByte    (5=playing, 6=stopping, 4=replay-driven)
        //   +0x1480  bStatusByte       (== 2 means active battle)
        //   +0x1490  nFrameAdvanceCounter (already captured via cursor sync)
        static constexpr uintptr_t kBM_bEnginePauseFlag_Off       = 0x12F3;
        static constexpr uintptr_t kBM_bMainStateMachineByte_Off  = 0x1461;
        // bMoveStateByte_Off already declared above as 0x1463
        static constexpr uintptr_t kBM_bStatusByte_Off            = 0x1480;

        // PlayerRecordArray gate bits (replay-resume fix, 2026-05-14):
        // LuxReplayChara_Tick_CopyNextFrameToManager_SetMoveState4 reads
        // PRA+(playerIndex*0xA8)+0x398 and early-returns if neither
        // rewind bit (8) nor forward bit (9) is set.  HorseMod's scrub
        // un-pause doesn't touch these bits, so post-seek the function
        // returns without ever incrementing dwPlaybackCursor.  Capture
        // both players' +0x394 and +0x398 fields per-snapshot so the
        // restore preserves whatever bit pattern was natural at the
        // captured frame (typically bit 9 = 1 during forward play).
        static constexpr uintptr_t kBM_PlayerRecordArray_Off  = 0x440;
        static constexpr uintptr_t kPRA_FieldAt394_Off        = 0x394;
        static constexpr uintptr_t kPRA_FieldAt398_Off        = 0x398;
        static constexpr uintptr_t kPRA_PlayerStride          = 0xA8;
        static constexpr uint32_t  kPRA_RewindBit             = 0x100;   // bit 8
        static constexpr uint32_t  kPRA_ForwardBit            = 0x200;   // bit 9

        // Layout of the per-slot "extras" blob (round-end + PRA-bit
        // resume fix, 2026-05-14):
        //   [0x00..0x40)  WorldModePump struct (64 bytes)
        //   [0x40..0x44)  g_LuxBattle_BlockInteractiveOps (4 bytes)
        //   [0x44..0x54)  RoundResultCinematic head (16 bytes)
        //   [0x54..0x64)  BM state bytes (4 x 4-byte slots, 1 byte each)
        //   [0x64..0x68)  PRA P0 +0x394
        //   [0x68..0x6C)  PRA P0 +0x398 (forward/rewind bits)
        //   [0x6C..0x70)  PRA P1 +0x394
        //   [0x70..0x74)  PRA P1 +0x398 (forward/rewind bits)
        //   [0x74..0x78)  ALuxBattleReplayPlayer.CurrentTime  (float seconds)
        //   [0x78..0x7C)  ALuxBattleReplayPlayer.CurrentRound (int32)
        //   [0x7C..0x7D)  ALuxBattleReplayPlayer.bIsPlayingBack (bool)
        //   [0x7D..0x80)  pad
        // Total: 0x80 = 128 bytes per slot.  At 7200 slots = ~900 KB.
        //
        // The ReplayPlayer state at the end IS THE PLAYBACK CURSOR (2026-05-15
        // user reframing): the BP-level replay menu reads CurrentTime each
        // tick to decide which recorded frame to dispatch.  Capturing the
        // LIVE value at snapshot time means we restore exactly what the
        // engine had at that frame - no derivation, no hardcoded round 0.
        static constexpr size_t kExtras_Off_WorldModePump   = 0x00;
        static constexpr size_t kExtras_WorldModePump_Bytes = 0x40;
        static constexpr size_t kExtras_Off_BlockInteractive = 0x40;
        static constexpr size_t kExtras_Off_CinematicHead   = 0x44;
        static constexpr size_t kExtras_CinematicHead_Bytes = 0x10;
        static constexpr size_t kExtras_Off_BM_MainState    = 0x54;
        static constexpr size_t kExtras_Off_BM_MoveState    = 0x58;
        static constexpr size_t kExtras_Off_BM_StatusByte   = 0x5C;
        static constexpr size_t kExtras_Off_BM_EnginePause  = 0x60;
        static constexpr size_t kExtras_Off_PRA_P0_394      = 0x64;
        static constexpr size_t kExtras_Off_PRA_P0_398      = 0x68;
        static constexpr size_t kExtras_Off_PRA_P1_394      = 0x6C;
        static constexpr size_t kExtras_Off_PRA_P1_398      = 0x70;
        static constexpr size_t kExtras_Off_RP_CurrentTime  = 0x74;
        static constexpr size_t kExtras_Off_RP_CurrentRound = 0x78;
        static constexpr size_t kExtras_Off_RP_IsPlayingBack = 0x7C;
        // 2026-05-15: per-chara replay-state fields at chara+0x43F4..+0x4428.
        // These fields are OUT OF HgCpuDirect's snapshot range (which ends
        // at chara+0x35A0) and are read by Stage 2 of the replay input
        // pipeline (LuxReplay_ConsumeDecodedInputPackets @ 0x1403F63B0)
        // for packet validation - specifically nReplayFrameTarget_at0x4414
        // gates whether decoded packets are accepted into the chara ring.
        // Without restoring these post-seek, Stage 2 may reject packets
        // after the cached window runs out -> chars idle.
        // Per chara: 0x43F4..+0x4428 = 0x34 = 52 bytes.  Two charas = 104.
        // Layout per chara: dwLookupKey/dwEnableFlag/dwFrameOffset/
        //  dwFrameTotal/dwFrameTarget/dwConsumerCursor/bCharaMode all packed
        //  in this range (per FLuxBattleChara struct in Ghidra).
        static constexpr size_t kExtras_CharaReplay_Bytes   = 0x34;
        static constexpr size_t kExtras_Off_P1_CharaReplay  = 0x80;
        static constexpr size_t kExtras_Off_P2_CharaReplay  = 0xB4;

        // 2026-05-16: ALuxBattleFrameInput per-slot input record (at BM+0x450).
        // This is the UPSTREAM source of the entire offline match-replay
        // input pipeline:
        //   BM+0x450 (FrameInput actor ptr) + 0x3E0 + slot*0x90
        //     -> read by LuxBattleManager_GetCurrentInputForSlot_FromBM0x450
        //        @ 0x1403F0680 (the writer's reader)
        //     -> writer LuxBattleManager_RefreshPerSlotCurrentInput_To3B8
        //        @ 0x1403FDF30 writes to pInputLog+0x3B8+slot*4
        //     -> cache writer LuxBattleManager_UpdateInputCache_LocalMode
        //        @ 0x1403F2AB0 reads +0x3B8, writes pInputLog+0x3C0 cache
        //     -> reader LuxBattleManager_UpdateCommandPlayerInput_At14c8_14d0
        //        @ 0x1403FE960 ALSO reads +0x3E0 directly, writes BM+0x14C8
        //
        // CRITICAL: Stage 3 (LuxBattleChara_ReplayPlayback_PushInputsToActiveSlots
        // @ 0x1403F6600) does NOT write to [BM+0x450]+0x3E0.  Confirmed by full
        // decompile inspection.  In match-replay viewing, the live-input writer
        // is gated OFF, so [BM+0x450]+0x3E0 contains STALE pre-replay-viewing
        // controller data and stays stale until forced.
        //
        // After HorseMod seek-back, this stale value is mirrored through:
        //   [BM+0x450]+0x3E0 (stale) -> pInputLog+0x3B8 (stale) -> cache (stale)
        //   -> BM+0x14A8/+0x14C8 (stale) -> chara MoveVM (does stale input
        //   for 7 frames until something clears the chain).
        //
        // This is the proximate cause of "EngineInput=0x9 for 7 frames then 0".
        // Restoring [BM+0x450]+0x3E0..+0x500 from the snapshot gives the
        // pipeline a known-good per-slot input value at the seek frame.
        //
        // Layout per slot (0x90 stride, slot 0 at +0x3E0, slot 1 at +0x470):
        //   +0x00 (u32)  bitfield held button mask (the +0x3E0 field readers
        //                read)
        //   +0x04 (u16)  direction bits (1=W, 2=A, 4=D, 7=any; read by SC
        //                trigger logic)
        //   +0x08..+0x90 other per-slot state (timers, counters, edge bits;
        //                exact layout not fully reverse-engineered, but bytes
        //                from snapshot are correct by construction)
        //
        // Range: 0x120 bytes = 2 slots * 0x90.  No heap pointers in this range
        // per Ctor analysis (ALuxActor_Ctor_D @ 0x1403ABF10 zero-inits with
        // sentinels but no allocations).  Safe for bytewise restore.
        static constexpr size_t kExtras_FrameInput_Bytes      = 0x120;
        static constexpr size_t kExtras_Off_FrameInput_Slots  = 0xE8;
        static constexpr size_t kExtras_Bytes                 = 0x208;

        // Chara replay-state field range (NOT in HgCpuDirect snapshot).
        static constexpr uintptr_t kChara_ReplayState_Start = 0x43F4;
        static constexpr uintptr_t kChara_ReplayState_End   = 0x4428;
        static_assert(kChara_ReplayState_End - kChara_ReplayState_Start
                      == kExtras_CharaReplay_Bytes,
                      "chara replay state size mismatch");

        // ALuxBattleFrameInput field offsets (verified via decompile of
        // 0x1403F0680, 0x1403FE960, 0x1403F0B90 readers).
        static constexpr uintptr_t kBM_FrameInputActor_Off  = 0x450;
        static constexpr uintptr_t kFI_SlotRecords_Start    = 0x3E0;
        static constexpr uintptr_t kFI_SlotRecord_Stride    = 0x90;
        static constexpr size_t    kFI_SlotRecord_Count     = 2;
        static constexpr size_t    kFI_SlotRecords_Bytes    =
            kFI_SlotRecord_Count * kFI_SlotRecord_Stride;
        static_assert(kFI_SlotRecords_Bytes == kExtras_FrameInput_Bytes,
                      "FrameInput per-slot records size mismatch");

        // ALuxBattleReplayPlayer field offsets (verified via
        // ALuxBattleReplayPlayer_RegisterProperties @ 0x14097beb0).
        static constexpr uintptr_t kRP_CurrentRound_Off    = 0x39C;
        static constexpr uintptr_t kRP_CurrentTime_Off     = 0x3A0;
        static constexpr uintptr_t kRP_IsPlayingBack_Off   = 0x3D0;

        // Backing storage: one large allocation, sliced into N x
        // kSnapshotStride byte slots.  Single allocation simplifies
        // the malloc story and keeps slots contiguous in memory for
        // cache locality.
        std::unique_ptr<uint8_t[]> m_data {};
        size_t m_ring_frames {0};

        // PARALLEL ring of InputLog-state snapshots.  One ~16 KB blob
        // per ring slot covering pInputLog+0x394..+0x4414 (= the full
        // replay-related state range, including the input cache, drain
        // cursor, and any hidden bookkeeping in between).  Restored on
        // seek alongside the HgCpuDirect simulation snapshot so the
        // engine's whole replay-playback machinery thinks it's at the
        // captured frame.
        //
        // Previously this was a narrower 16 KB capture of just the
        // input cache at +0x3C0..+0x43C0; that proved insufficient
        // (see kIL_CaptureStart_Off plate above).
        //
        // Memory cost: kIL_CaptureBytes x ring_frames = ~57 MB at the
        // default 3600 frames.
        std::unique_ptr<uint8_t[]> m_input_log_ring {};

        // PARALLEL ring of FLuxReplayDataBlock snapshots - the Stage
        // 1 decoder's full 1021-byte state struct (at *pBM+0x460).
        // Capturing this brings along the DECODER cursors -
        // llFileReadCursor, llDecodedBufferReadCursor, llDecodedBuffer-
        // WriteCursor, working frame ID, etc. - so a backward seek
        // can rewind the decoder to the captured frame's position
        // and the engine's per-frame input consumer reads the right
        // packets afterwards.
        //
        // 2026-05-11 finding: without this, the engine's decoder
        // stays at the live-edge file position; post-seek the engine
        // applies "inputs from later in the round" to the just-
        // restored state-at-T, producing the "characters do moves
        // from frame F onwards on top of state at T" symptom.
        //
        // Memory cost: 1021 x ring_frames = ~3.6 MB at 3600 frames.
        // Negligible compared to the simulation snapshot ring.
        std::unique_ptr<uint8_t[]> m_rdb_ring {};

        // PARALLEL ring of "extras" - the round-end-fix state (2026-05-14):
        // WorldModePump struct + BlockInteractiveOps + cinematic head +
        // BM internal state bytes.  See kExtras_* constants above for the
        // exact layout.  Capture/restore in lockstep with the HgCpuDirect
        // snapshot so a backward seek from post-round into mid-round
        // restores the world-mode state machine to the in-round mode
        // pointer, clears the cinematic-block flag, and resets BM bytes -
        // un-gating LuxBattle_PerFrameTick's chara input tick which is
        // otherwise blocked when WorldModePump's GetModeType still
        // returns 3 (round-end) post-restore.
        //
        // Memory cost: kExtras_Bytes x ring_frames = ~700 KB at 7200
        // frames.  Negligible.
        std::unique_ptr<uint8_t[]> m_extras_ring {};

        // Per-slot tags.  Atomic because the UI thread reads the
        // timeline accessors while the cockpit thread updates them on
        // capture.  FOUR tags per slot:
        //
        //   m_seq_tags    -- monotonic capture sequence (a HorseMod-side
        //                    counter, +1 per capture).  THE canonical
        //                    timeline coordinate: unique, gap-free and
        //                    monotonic across the WHOLE match including
        //                    round boundaries.  The UI bar, playhead and
        //                    seek all key off this.
        //   m_round_tags  -- ALuxBattleReplayPlayer.CurrentRound at
        //                    capture time.  Drives the timeline's
        //                    round-boundary markers + round-aware time
        //                    display.
        //   m_frame_tags  -- g_LuxBattle_FrameCounter (wall clock).
        //                    NOTE: this RESETS TO 0 at every round
        //                    boundary (LuxBattle_InitializeMatchRoundState
        //                    @ 0x1402DBA92 zeroes it), so it is NOT a
        //                    valid match-wide coordinate - it is kept
        //                    only as the within-round frame number for
        //                    display + the PRE_SEEK diagnostic.
        //   m_master_tags -- pInputLog->nMasterClock (replay clock) at
        //                    capture time.  Used by write_replay_cursors
        //                    to sync the engine's InputLog cursors after
        //                    a snapshot restore, and by the post-seek
        //                    playhead extrapolation.  The engine reads
        //                    recorded inputs indexed by master clock, so
        //                    the restore MUST use this, not wall clock.
        std::vector<std::atomic<int32_t>> m_seq_tags    {};
        std::vector<std::atomic<int32_t>> m_round_tags  {};
        std::vector<std::atomic<int32_t>> m_frame_tags  {};
        std::vector<std::atomic<int32_t>> m_master_tags {};

        // Ring head: index of the slot the NEXT capture will write
        // into.  Atomic for the same reason as the tag arrays.
        std::atomic<size_t>  m_head  {0};
        std::atomic<size_t>  m_count {0};

        // Capture-loop state (cockpit thread only).
        uint32_t m_last_counter      {0};
        int32_t  m_last_master       {-1};
        int32_t  m_last_round        {-1};
        bool     m_have_last_counter {false};

        // The BattleManager + ReplayPlayer observed on the previous
        // tick.  Each replay playback is a fresh level load with fresh
        // actors, so a changed BM OR a changed ReplayPlayer while
        // presence stays 'Replay' means a new replay was loaded (the
        // browser and playback share the 'Replay' presence value).  Two
        // independent identities are tracked so a heap-address reuse on
        // one actor cannot mask a replay swap.  Compared by address only
        // - never dereferenced - so a stale value from a destroyed actor
        // is harmless.  Cockpit thread only.
        RC::Unreal::UObject* m_last_bm_obj            {nullptr};
        RC::Unreal::UObject* m_last_replay_player_obj {nullptr};

        // Monotonic capture-sequence counter.  Stamped into m_seq_tags
        // on each capture; reset to 0 only when the ring is dropped
        // (new replay / presence change).  Cockpit thread only.
        int32_t  m_next_seq          {0};

        // Toggles + flags.
        std::atomic<bool> m_initialized              {false};
        // Passive per-frame capture.  OFF by default: the per-frame
        // HgCpuDirect snapshot is expensive, and capturing it every
        // frame of plain replay viewing dropped the framerate.
        // "Generate timeline" is the normal way to build the ring;
        // tick_capture() captures whenever a generation pass runs,
        // regardless of this flag.  Turn ON only to also capture during
        // ordinary 1x viewing (accepting the per-frame cost).
        std::atomic<bool> m_capture_enabled          {false};
        std::atomic<bool> m_paused                   {false};
        std::atomic<bool> m_auto_resume_on_release   {true};
        std::atomic<int32_t> m_seek_request          {kSeekIdle};
        // Deferred resize request from the UI (render thread) -- picked
        // up by the cockpit pre-tick (game thread) so ring teardown +
        // realloc runs on a single thread.  0 = no request pending.
        // Without this, dragging the capture-window slider could race
        // ensure_initialized() against tick_capture()/do_seek_to_seq
        // and dereference a freed m_data / cleared vector.
        std::atomic<size_t>  m_pending_resize_frames  {0};

        // Verbose diagnostics toggle (UI checkbox).  When ON:
        //   - every BASELINE dump also calls ReplayScrubDiag::dump_full
        //   - every cockpit tick logs a per-tick MoveVM delta line
        //     while m_post_seek_countdown > 0
        //   - seek operations log PRE_SEEK / POST_SEEK / POST_SEEK_T+N
        //     for N in [1..m_post_seek_dump_frames] so we can see if
        //     UDemoNetDriver overwrites our restore (or fails to)
        std::atomic<bool>    m_verbose_diag           {false};

        // Write ALuxBattleReplayPlayer.CurrentTime + .CurrentRound +
        // .bIsPlayingBack on each seek (direct byte writes at verified
        // Ghidra offsets).
        //
        // Default ON (2026-05-15 reset to architectural-first thinking):
        //
        // What we're TRYING to do: skip to the closest captured save
        // point AND move the engine's replay-playback cursor to that
        // save point's master clock.
        //
        // Cursors we already restore (via IL window):
        //   pInputLog->nMasterClock @ +0x3A4
        //   pInputLog->dwPlaybackCursor @ +0x39C
        // PLUS BM+0x148C/+0x1488 cursor pair sync.
        //
        // But the BP-level replay menu dispatches recorded frames using
        // ALuxBattleReplayPlayer.CurrentTime (replicated UE4 property
        // 0x20080000020815 at actor+0x3A0).  If we don't rewind THAT,
        // the BP keeps dispatching live-edge frames into the restored
        // chara state - which manifests as "chara finishes current
        // move and idles" (no useful new dispatch lands).
        //
        // Earlier sessions where CurrentTime got reset (via UE4SS
        // reflection that happened to work) showed playback resuming
        // post-seek.  Sessions where it didn't reset showed chars idle.
        // The strongest empirical signal we have.
        //
        // Caveat: target_round is hardcoded to 0 in the caller; multi-
        // round replays may need per-snapshot round capture.  TODO.
        std::atomic<bool>    m_use_replay_player_seek {true};

        // Set to N (default 600 = 10 seconds @ 60fps) right after a
        // seek; decremented every cockpit tick.  Per-tick detail
        // logging runs while > 0.  10 seconds is long enough to see
        // what happens AFTER the user un-pauses (the previous 60-frame
        // window expired before the user pressed play, leaving us no
        // log data for the post-play state - the critical failure
        // window).  Cap is 1800 (30s).
        std::atomic<int32_t> m_post_seek_countdown    {0};
        static constexpr int32_t kDefaultPostSeekDumpFrames = 600;
        static constexpr int32_t kMaxPostSeekDumpFrames     = 1800;

        // Last-seen pause flag for transition logging.  When this
        // differs from m_paused on a tick, we log the transition + a
        // FORCE_DUMP-equivalent so we capture the moment the user
        // pressed Play / Pause.
        bool m_diag_last_paused {false};

        // Last-seen wall-counter + master-clock so the per-tick post-
        // seek log can show deltas explicitly (the wall stops moving
        // during pause; master stops moving when replay decoder
        // halts).  Single producer = game thread.
        uint32_t m_diag_last_wall   {0};
        int32_t  m_diag_last_master {-1};

        // One-shot diagnostic dump request from UI button.  Consumed at
        // the next cockpit tick.
        std::atomic<bool>    m_force_diag_request     {false};

        // Last-seen MoveVM state per chara, used to detect changes
        // tick-to-tick during post-seek logging.  Single producer (game
        // thread) so no atomic needed.
        ReplayScrubDiag::CharaMoveVmSnap m_last_movevm_p1 {};
        ReplayScrubDiag::CharaMoveVmSnap m_last_movevm_p2 {};

        // Most-recent successfully-applied seek target (= frame_tag of
        // the ring slot we restored from).  Used by current_play_-
        // position() to drive the playhead while paused.
        std::atomic<int32_t> m_last_seek_target      {-1};

        // 2026-05-14 UI-playhead fix: track the master clock at last
        // seek + the LIVE engine master clock so the UI can extrapolate
        // a smooth playhead during forward play post-seek.
        //
        // Old design pinned the UI playhead to m_last_seek_target
        // forever after any seek, on the assumption that engine
        // couldn't resume forward play.  User-observed behavior
        // (game timer ticking down while playhead stuck at seek
        // target) proves the simulation IS running forward post-seek;
        // therefore the playhead should advance with it.
        //
        // m_live_master_cached is updated every cockpit tick from
        // read_engine_master_clock() so the UI thread can read a
        // stable value without racing the game thread.  Both fields
        // are atomic for the UI-vs-game-thread access.
        std::atomic<int32_t> m_last_seek_master_tag {-1};
        std::atomic<int32_t> m_live_master_cached   {-1};

        // 2026-05-15 SAFETY GATE.  Defaults to FALSE: restore_extras
        // skips ALL speculative writes (WorldModePump, BlockInteractive-
        // Ops, cinematic head, BM state bytes, PRA bits, ReplayPlayer
        // state).  The 15:24 crash on un-pause-after-seek was caused
        // by one of these speculative writes corrupting engine state.
        //
        // When false, do_seek_to_seq still does:
        //   * HgCpuDirect chara/global restore (ExecFinalizeAndPost)
        //   * IL window restore (verified engine consumer)
        //   * RDB selective cursor restore (verified engine consumer)
        //   * BM cursor pair sync (verified engine consumer)
        //
        // To re-enable speculative writes one at a time for binary
        // searching the crash cause, set this true and add individual
        // sub-gates inside restore_extras.
        std::atomic<bool>    m_enable_speculative_restore {false};

        // 2026-05-15 (ultrathink): Granular debug toggles.  These let the
        // user bisect which restore is needed/harmful, since the conflicting
        // Ghidra plates make it ambiguous which speculative fields drive
        // playback and which corrupt state.
        //
        // - m_enable_chara_4400_restore: chara+0x43F4..+0x4428 (52 B/chara).
        //   The FLuxBattleChara struct labels these as "replay state cursors"
        //   (dwReplayLookupKey/EnableFlag/FrameOffset/FrameTotal/FrameTarget/
        //   ConsumerCursor/bCharaMode).  An older Ghidra plate empirically
        //   observed VFX-byte values in these fields during match-replay
        //   viewing (bCharaMode=14/197/63 not 5/2) and concluded they are
        //   "repurposed for VFX".  Default ON - restoring captured bytes is
        //   correct if the snapshot's values were the right state at that
        //   moment, regardless of whether semantically "cursors" or "VFX".
        //   Toggle off to test if restoring them is harmful.
        //
        // - m_force_pra_forward_bit_on_seek: write PRA+0x398 bit 9 (0x200) = 1
        //   immediately after seek.  Bit 9 = FORWARD play request.  Per
        //   CopyNextFrameToManager_SetMoveState4 plate, this bit gates the
        //   ReplayPlayer's per-frame snapshot dispatcher.  Default OFF.
        //   Toggle ON if you suspect post-seek the engine needs the bit set
        //   to resume forward play.
        //
        // - m_force_isplayingback_on_seek: write ReplayPlayer+0x3D0 = 1
        //   immediately after seek.  Forces bIsPlayingBack = true so the
        //   engine doesn't think replay has ended.  Default OFF.
        //
        // Each toggle is exposed in HorseMod's Replay tab UI for bisection.
        std::atomic<bool>    m_enable_chara_4400_restore     {true};
        std::atomic<bool>    m_force_pra_forward_bit_on_seek {false};
        std::atomic<bool>    m_force_isplayingback_on_seek   {false};

        // 2026-05-16 "Generate timeline" - fast-forward via frame-cap
        // removal.  See the FrameCapOverride plate above for the full
        // mechanism: removing SC6's engine FixedFrameRate cap makes
        // UE4's own loop tick faster, so the frame-counted replay sim
        // plays back proportionally faster with every pipeline stage in
        // 1:1 lockstep - tick_capture() then fills the ring exactly as
        // in 1x playback, just sooner.
        //
        // States (TimelineGenState, declared public at the class top):
        //   Idle:        no generation in progress
        //   Generating:  frame cap removed, replay fast-forwarding
        //   Done:        generation finished (auto-stopped at the end
        //                of the recording / replay loop)
        //
        // m_gen_request is the render-thread -> game-thread handoff:
        // the UI posts kGenReqStart/kGenReqStop, tick_generate_timeline()
        // consumes it on the game thread.
        FrameCapOverride         m_frame_cap;
        ScreenPercentageOverride m_screen_pct;
        std::atomic<int>     m_gen_request               {0};
        std::atomic<int>     m_timeline_gen_state        {0};
        std::atomic<int32_t> m_timeline_gen_start_master {0};
        std::atomic<int32_t> m_timeline_gen_last_master  {0};

        // m_gen_request values.
        static constexpr int kGenReqNone  = 0;
        static constexpr int kGenReqStart = 1;
        static constexpr int kGenReqStop  = 2;

        // Auto-stop tuning.  Wall-clock based so it is independent of
        // the (now uncapped, hardware-dependent) frame rate.
        //   kGenStuckSeconds: master clock idle this long => end of the
        //     recording reached.  Set well above any legitimate
        //     mid-replay master-clock stall (round transition / KO
        //     cinematic) so generation never false-stops mid-stream.
        //     The prompt end signals are presence-change / loop /
        //     teardown; this stall timer is only the backstop for a
        //     replay that halts on a held end screen.
        //   kGenMaxSeconds: hard safety ceiling on a generation run.
        static constexpr double kGenStuckSeconds = 8.0;
        static constexpr double kGenMaxSeconds   = 120.0;

        // Wall-clock marks for the auto-stop logic.  Touched only by the
        // game thread (start_generate_timeline / tick_generate_timeline),
        // so plain members - no atomics needed.
        std::chrono::steady_clock::time_point m_gen_started_at  {};
        std::chrono::steady_clock::time_point m_gen_last_advance{};
        // CurrentRound observed on the previous generation tick - drives
        // multi-round loop detection (a backward jump = replay looped).
        int32_t m_gen_last_round {-1};
        // Highest CurrentRound seen across THIS generation pass.  Rounds
        // only climb during forward play, so observing round <
        // m_gen_max_round means the replay looped/restarted - a robust
        // loop signal that does NOT depend on catching the exact
        // transition tick (the bare last_round comparison missed it when
        // CurrentRound read -1 on the loop frame, so generation ran on
        // past the end until the user stopped it manually).
        int32_t m_gen_max_round {-1};

        // ---- Internals ------------------------------------------------

        bool resolve_natives() noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            m_exec_write = reinterpret_cast<ExecWriteFn>(
                base + kRVA_ExecMoveChangeAndPost);
            m_exec_read  = reinterpret_cast<ExecReadFn>(
                base + kRVA_ExecFinalizeAndPost);
            m_frame_counter_addr =
                reinterpret_cast<const void*>(base + kRVA_FrameCounter);
            return m_exec_write != nullptr
                && m_exec_read  != nullptr
                && m_frame_counter_addr != nullptr;
        }

        bool alloc_ring(size_t ring_frames) noexcept
        {
            if (ring_frames == 0) return false;
            if (ring_frames > kMaxRingFrames) ring_frames = kMaxRingFrames;
            // Allocate both rings before exposing either - on
            // mid-allocation OOM we want to leave the tracker fully
            // uninitialised rather than half-allocated.
            try
            {
                const size_t total       = ring_frames * kSnapshotStride;
                const size_t il_total    = ring_frames * kIL_CaptureBytes;

                m_data.reset(new uint8_t[total]);
                std::memset(m_data.get(), 0, total);

                // Parallel InputLog-state ring.  If this alloc throws,
                // the catch handler MUST drop m_data too - otherwise
                // we leak ~590 MB until the next successful
                // ensure_initialized() call.
                m_input_log_ring.reset(new uint8_t[il_total]);
                std::memset(m_input_log_ring.get(), 0, il_total);

                // Parallel decoder-state ring (FLuxReplayDataBlock at
                // *pBM+0x460).  Tiny relative to the other rings -
                // 1021 bytes per slot.
                const size_t rdb_total = ring_frames * kRDB_Bytes;
                m_rdb_ring.reset(new uint8_t[rdb_total]);
                std::memset(m_rdb_ring.get(), 0, rdb_total);

                // Parallel extras ring (round-end seek-back fix,
                // 2026-05-14): WorldModePump + BlockInteractiveOps +
                // cinematic head + BM internal state bytes.  ~100 bytes
                // per slot.
                const size_t extras_total = ring_frames * kExtras_Bytes;
                m_extras_ring.reset(new uint8_t[extras_total]);
                std::memset(m_extras_ring.get(), 0, extras_total);

                m_seq_tags    = std::vector<std::atomic<int32_t>>(ring_frames);
                m_round_tags  = std::vector<std::atomic<int32_t>>(ring_frames);
                m_frame_tags  = std::vector<std::atomic<int32_t>>(ring_frames);
                m_master_tags = std::vector<std::atomic<int32_t>>(ring_frames);
                for (auto& tag : m_seq_tags)    tag.store(-1, std::memory_order_relaxed);
                for (auto& tag : m_round_tags)  tag.store(-1, std::memory_order_relaxed);
                for (auto& tag : m_frame_tags)  tag.store(-1, std::memory_order_relaxed);
                for (auto& tag : m_master_tags) tag.store(-1, std::memory_order_relaxed);
                m_head.store(0,  std::memory_order_relaxed);
                m_count.store(0, std::memory_order_relaxed);
                m_ring_frames        = ring_frames;
                m_have_last_counter  = false;
                m_last_round         = -1;
                m_next_seq           = 0;
                return true;
            }
            catch (const std::bad_alloc&)
            {
                // Drop any partial allocation so we don't leak earlier
                // sub-allocations if a later one threw.
                m_data.reset();
                m_input_log_ring.reset();
                m_rdb_ring.reset();
                m_extras_ring.reset();
                m_seq_tags.clear();
                m_round_tags.clear();
                m_frame_tags.clear();
                m_master_tags.clear();
                m_ring_frames = 0;
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[ReplayScrub] ring alloc failed for {} frames "
                        "(sim ~{} MB + InputLog ~{} MB + RDB ~{} MB)\n"),
                    ring_frames,
                    (ring_frames * kSnapshotStride) / (1024ull * 1024ull),
                    (ring_frames * kIL_CaptureBytes) / (1024ull * 1024ull),
                    (ring_frames * kRDB_Bytes) / (1024ull * 1024ull));
                return false;
            }
        }

        void free_ring() noexcept
        {
            // Mark the ring empty BEFORE tearing down the storage so a
            // concurrent UI read of earliest/latest_seq() observes
            // count==0 before it tries to subscript a cleared vector.
            // (ensure_initialized() also flips m_initialized=false
            // around the teardown.  free_ring() is additionally
            // reachable from the destructor; the reset_for_new_replay()
            // path uses drop_ring() instead, which keeps the storage -
            // belt-and-braces.)
            m_count.store(0, std::memory_order_release);
            m_head.store(0,  std::memory_order_release);
            m_ring_frames        = 0;
            m_have_last_counter  = false;
            m_last_round         = -1;
            m_next_seq           = 0;
            m_data.reset();
            m_extras_ring.reset();
            m_input_log_ring.reset();
            m_rdb_ring.reset();
            m_seq_tags.clear();
            m_round_tags.clear();
            m_frame_tags.clear();
            m_master_tags.clear();
        }

        // Drop captured contents but keep the allocation.  Used when the
        // timeline becomes chronologically discontinuous: a new replay
        // (on_presence_change) or a replay restart inside the viewer
        // (CurrentRound jumps backward).  Round-to-round transitions do
        // NOT drop the ring - the timeline spans the whole match.
        //
        // We DO NOT zero m_input_log_ring (~16 KB x ring_frames =
        // ~57 MB memset).  The old blobs become unreachable because
        // every find_slot_for_seq() call first checks m_seq_tags[i] >= 0
        // - we set them all to -1 here, so no slot is reachable until
        // capture_snapshot overwrites it.
        void drop_ring() noexcept
        {
            for (auto& tag : m_seq_tags)    tag.store(-1, std::memory_order_relaxed);
            for (auto& tag : m_round_tags)  tag.store(-1, std::memory_order_relaxed);
            for (auto& tag : m_frame_tags)  tag.store(-1, std::memory_order_relaxed);
            for (auto& tag : m_master_tags) tag.store(-1, std::memory_order_relaxed);
            m_head.store(0,  std::memory_order_relaxed);
            m_count.store(0, std::memory_order_relaxed);
            m_next_seq   = 0;
            m_last_round = -1;
        }

        bool read_frame_counter(uint32_t& out) const noexcept
        {
            if (!m_frame_counter_addr) return false;
            return SafeReadUInt32(m_frame_counter_addr, &out);
        }

        // Read ALuxBattleReplayPlayer.CurrentRound (the replay's round
        // index, ReplayPlayer+0x39C).  Returns -1 if the actor isn't
        // resolvable (between matches / teardown).  Uses the shared
        // GlobalPtr cache from ReplayScrubDiag.
        int32_t read_current_round() const noexcept
        {
            RC::Unreal::UObject* rp =
                ReplayScrubDiag::replay_player_ptr().get(
                    L"LuxBattleReplayPlayer");
            if (!rp) return -1;
            int32_t r = -1;
            if (!SafeReadInt32(reinterpret_cast<const uint8_t*>(rp)
                                   + kRP_CurrentRound_Off, &r))
                return -1;
            return r;
        }

        // Consume any pending resize request posted by the UI thread
        // (render thread) via request_resize().  Runs on the game
        // thread from service_seek_request(), so the actual teardown
        // + realloc is single-threaded with respect to tick_capture
        // and do_seek_to_seq which both run on the same thread.
        // No-op when no request is pending.
        void service_resize_request() noexcept
        {
            const size_t req = m_pending_resize_frames.exchange(
                0, std::memory_order_acq_rel);
            if (req == 0) return;
            // No change needed if the ring is already the requested
            // size.  Reading m_ring_frames is safe here - this method
            // is only ever called from the cockpit pre-tick (game
            // thread), the same thread that writes m_ring_frames.
            if (is_initialized() && m_ring_frames == req) return;
            // 2026-05-16: cancel any in-progress timeline generation
            // before reallocating the ring - the realloc discards every
            // captured snapshot, so finishing the fast-forward into a
            // fresh ring would be pointless.  Restores the frame cap.
            if (m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating))
            {
                stop_generate_timeline("ring-resize", false);
            }
            ensure_initialized(req);
        }

        // Capture a snapshot of the current sim state into the next
        // ring slot, tagged with `wall_tag` (= g_LuxBattle_FrameCounter)
        // AND the engine's replay master_clock value AT CAPTURE TIME.
        // See m_master_tags doc for why we need both clocks.
        void capture_snapshot(int32_t wall_tag) noexcept
        {
            if (!m_data || !m_exec_write) return;

            const size_t cap   = m_ring_frames;
            const size_t slot  = m_head.load(std::memory_order_relaxed);
            uint8_t* slot_data = m_data.get() + slot * kSnapshotStride;

            // Re-target the shim at this slot's data, reset cursor,
            // and let the engine fill it.  ExecMoveChangeAndPost is a
            // bounded ~80-100 KB structured copy (verified via decompile
            // @ 0x1403841E0) - sub-millisecond, not a framerate cost.
            m_shim.retarget(slot_data, kSnapshotStride);
            m_exec_write(&m_shim);

            // Capture the engine's InputLog replay-state window in
            // lockstep with the simulation snapshot, so a future
            // restore reproduces every replay-machinery field
            // (including the cache, drain cursor, double-tick guard,
            // playback cursor, master clock, etc.) for the captured
            // frame.
            if (!capture_input_cache(slot))
            {
                // InputLog wasn't readable.  m_exec_write already
                // overwrote m_data[slot], so this slot's sim / IL /
                // extras are now mutually inconsistent - invalidate its
                // seq tag so find_slot_for_seq() can never select it.
                // (m_head / m_count were not advanced, so the FIFO
                // accessors never saw it anyway.)
                m_seq_tags[slot].store(-1, std::memory_order_release);
                return;
            }

            // Capture the Stage 1 decoder's state (file/buffer
            // cursors, working frame ID, etc.) in lockstep with the
            // simulation snapshot.  If the BM has no decoder block
            // (between-match transitions) we still keep the slot's
            // simulation + InputLog data - the decoder-state restore
            // will just be a no-op for that slot.
            capture_replay_data_block(slot);

            // Capture the round-end-fix extras (WorldModePump struct
            // pointer + cinematic head + BM internal state bytes).
            // If any sub-capture faults (between-match teardown), the
            // blob is left zeroed; restore_extras handles that case
            // by skipping writes to invalid targets.
            capture_extras(slot);

            // Derive master_tag FROM the captured IL blob's
            // nMasterClock field rather than reading the engine
            // separately.  This guarantees the two are in lockstep -
            // there's no race window where the engine ticks between
            // reading master_clock and reading the IL bytes.
            const uint8_t* il_blob = input_log_slot_ptr(slot);
            int32_t master_tag = -1;
            if (il_blob)
            {
                const uintptr_t off =
                    kIL_nMasterClock_Off - kIL_CaptureStart_Off;
                std::memcpy(&master_tag, il_blob + off, sizeof(master_tag));
            }
            if (master_tag < 0)
            {
                // Bad capture-time master clock - same inconsistent-slot
                // situation as the InputLog-unreadable path above.
                m_seq_tags[slot].store(-1, std::memory_order_release);
                return;
            }

            // Read CurrentRound back from the extras blob that
            // capture_extras() just populated (no extra engine read) -
            // it drives the timeline's round markers - and stamp this
            // capture's monotonic sequence number (the timeline coord).
            int32_t round_tag = 0;
            if (m_extras_ring)
            {
                std::memcpy(&round_tag,
                            m_extras_ring.get() + slot * kExtras_Bytes
                                + kExtras_Off_RP_CurrentRound,
                            sizeof(round_tag));
            }
            const int32_t seq_tag = m_next_seq++;

            m_round_tags [slot].store(round_tag,  std::memory_order_release);
            m_frame_tags [slot].store(wall_tag,   std::memory_order_release);
            m_master_tags[slot].store(master_tag, std::memory_order_release);
            // Store the seq tag LAST: find_slot_for_seq() and the UI
            // treat seq >= 0 as "slot valid", so the other three tags
            // must be visible before seq is.
            m_seq_tags   [slot].store(seq_tag,    std::memory_order_release);
            m_head.store((slot + 1) % cap, std::memory_order_release);
            const size_t cnt = m_count.load(std::memory_order_relaxed);
            if (cnt < cap)
                m_count.store(cnt + 1, std::memory_order_release);

            // First-fire log so the user can confirm the capture
            // path is alive without enabling Verbose.
            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub] first capture: seq={} round={} "
                        "wall_tag={} master_tag={} shim cursor after "
                        "Exec={} bytes\n"),
                    seq_tag, round_tag, wall_tag, master_tag,
                    static_cast<unsigned long long>(m_shim.cursor()));
            }
        }

        // Read pInputLog->nMasterClock via UE4SS reflection +
        // SafeReadInt32.  Returns -1 on any failure (BM not alive,
        // InputLog null, fault during read).
        int32_t read_engine_master_clock() noexcept
        {
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return -1;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            void* il = nullptr;
            if (!SafeReadPtr(bm + kBM_BattleFrameInputLog_Off, &il) || !il)
                return -1;
            int32_t master = -1;
            SafeReadInt32(reinterpret_cast<uint8_t*>(il)
                          + kIL_nMasterClock_Off, &master);
            return master;
        }

        // Resolve a pointer into the per-slot InputLog-state blob.
        uint8_t* input_log_slot_ptr(size_t slot) noexcept
        {
            if (!m_input_log_ring || slot >= m_ring_frames) return nullptr;
            return m_input_log_ring.get() + slot * kIL_CaptureBytes;
        }

        // Capture the engine's InputLog replay-state window
        // (pInputLog+0x394..+0x4414) verbatim into our ring slot.
        // Called from capture_snapshot RIGHT AFTER the HgCpuDirect
        // simulation write, while the engine state is still at the
        // captured frame's master clock.
        //
        // Returns true on success, false if the BM/InputLog couldn't
        // be resolved (between-match transitions).  SEH-wrapped via
        // SafeReadBytes - the InputLog actor can be torn down during
        // mode transitions and a fault here shouldn't kill the
        // process.
        bool capture_input_cache(size_t slot) noexcept
        {
            uint8_t* dst = input_log_slot_ptr(slot);
            if (!dst) return false;

            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return false;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            void* il = nullptr;
            if (!SafeReadPtr(bm + kBM_BattleFrameInputLog_Off, &il) || !il)
                return false;
            uint8_t* src =
                reinterpret_cast<uint8_t*>(il) + kIL_CaptureStart_Off;
            return SafeReadBytes(src, dst, kIL_CaptureBytes);
        }

        // Restore a previously-captured InputLog-state blob into the
        // engine's live InputLog.  Called from do_seek_to_seq RIGHT
        // AFTER the HgCpuDirect simulation restore so the engine's
        // replay-playback machinery (cache, drain cursor, double-tick
        // guard, and any other hidden bookkeeping in the +0x394..
        // +0x4414 window) is fully rewound to match the snapshot
        // frame.
        //
        // POINTER SKIP: pRecordedFrameBuffer @ pInputLog+0x3A8 is a
        // pointer to engine-managed memory.  Within a session it's
        // stable, but if the engine re-allocates between capture and
        // restore (round transition, reload, or any other reset we
        // haven't traced), overwriting it with the stale captured
        // value points the engine at freed memory.  The 2026-05-11
        // user-test crash trace stopped right after a series of
        // seeks - most likely cause was exactly this category of
        // use-after-free.  Skip the 8-byte pointer; restore the two
        // surrounding spans separately.
        //
        // SEH-wrapped via SafeWriteBytes: the writes go to engine
        // memory and could fault if the InputLog actor was torn down
        // between the SafeReadPtr resolve and the write.
        bool restore_input_cache(size_t slot) noexcept
        {
            const uint8_t* src = input_log_slot_ptr(slot);
            if (!src) return false;

            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return false;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            void* il = nullptr;
            if (!SafeReadPtr(bm + kBM_BattleFrameInputLog_Off, &il) || !il)
                return false;
            uint8_t* il_b = reinterpret_cast<uint8_t*>(il);

            // Split write around pRecordedFrameBuffer @ +0x3A8 (8 bytes
            // pointer).  Span1 covers +0x394..+0x3A8; span2 covers
            // +0x3B0..+0x4414.  All offsets in the blob are aligned to
            // the capture start at +0x394.
            constexpr uintptr_t kSkipPtrOff   = 0x3A8;
            constexpr size_t    kSkipPtrBytes = 8;
            const uintptr_t s1_off = kIL_CaptureStart_Off;            // 0x394
            const size_t    s1_bytes = kSkipPtrOff - s1_off;           // 0x14
            const uintptr_t s2_off = kSkipPtrOff + kSkipPtrBytes;      // 0x3B0
            const size_t    s2_bytes = kIL_CaptureEnd_Off - s2_off;    // 0x4064

            bool ok = true;
            ok &= SafeWriteBytes(il_b + s1_off,
                                 src + (s1_off - kIL_CaptureStart_Off),
                                 s1_bytes);
            ok &= SafeWriteBytes(il_b + s2_off,
                                 src + (s2_off - kIL_CaptureStart_Off),
                                 s2_bytes);
            return ok;
        }

        // Resolve a pointer into the per-slot decoder-state blob.
        uint8_t* rdb_slot_ptr(size_t slot) noexcept
        {
            if (!m_rdb_ring || slot >= m_ring_frames) return nullptr;
            return m_rdb_ring.get() + slot * kRDB_Bytes;
        }

        // Capture the Stage 1 decoder's full state (FLuxReplayDataBlock
        // at *(pBM+0x460), 1021 bytes) verbatim into our ring slot.
        //
        // This is the KEY missing piece identified 2026-05-11: the
        // decoder's read/write cursors (llFileReadCursor,
        // llDecodedBufferReadCursor, llDecodedBufferWriteCursor)
        // control which packets the engine consumes per frame.
        // Without restoring them on seek, the decoder stays at the
        // live-edge file position and serves "later" packets to the
        // restored state - which is exactly the "plays inputs from
        // later in the round" symptom we saw.
        bool capture_replay_data_block(size_t slot) noexcept
        {
            uint8_t* dst = rdb_slot_ptr(slot);
            if (!dst) return false;

            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return false;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            void* rdb = nullptr;
            if (!SafeReadPtr(bm + kBM_pReplayDataBlock_Off, &rdb) || !rdb)
                return false;
            return SafeReadBytes(rdb, dst, kRDB_Bytes);
        }

        // Restore a captured decoder-state blob, SURGICALLY.  Only the
        // cursor and working-state fields are written back; pointer
        // fields (pVerifyPtr @ +0x3A0, pDecodedPacketBuffer @ +0x3A8,
        // pFileBuffer @ +0x3C8) and buffer-size fields (+0x3B0,
        // +0x3D0, +0x3D8) are SKIPPED.
        //
        // 2026-05-11 crash investigation: blindly memcpy'ing the full
        // 1021 bytes caused a crash on un-pause after a drag-scrub.
        // Most likely the pointer fields were stale (different round,
        // re-allocated buffer, or a subtle within-session change we
        // don't fully understand) and overwriting them pointed the
        // engine's decoder at freed memory.  Buffer-size fields might
        // also have changed if the engine resized the decoded buffer.
        //
        // Selective field list (verified vs the Ghidra struct):
        //   +0x3B8  int64  llDecodedBufferWriteCursor  (Stage 1 write pos)
        //   +0x3C0  int64  llDecodedBufferReadCursor   (Stage 2 read pos)
        //   +0x3E0  int64  llFileReadCursor            (Stage 1 file pos)
        //   +0x3F0  u16    wWorkFrameID
        //   +0x3F2  u16    wWorkCursor
        //   +0x3F4  u16    wWorkP1Input
        //   +0x3F6  u16    wWorkP2Input
        //   +0x3FC  u8     bRunningFlag
        bool restore_replay_data_block(size_t slot) noexcept
        {
            const uint8_t* src = rdb_slot_ptr(slot);
            if (!src) return false;

            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return false;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            void* rdb_raw = nullptr;
            if (!SafeReadPtr(bm + kBM_pReplayDataBlock_Off, &rdb_raw) || !rdb_raw)
                return false;
            uint8_t* rdb = reinterpret_cast<uint8_t*>(rdb_raw);

            // Restore the cursor / working-state fields.  Three
            // contiguous spans plus one trailing byte; each is a
            // single SafeWriteBytes call to keep SEH-handling tidy.
            bool ok = true;
            ok &= SafeWriteBytes(rdb + 0x3B8, src + 0x3B8, 16); // write+read cursors
            ok &= SafeWriteBytes(rdb + 0x3E0, src + 0x3E0, 8);  // file read cursor
            ok &= SafeWriteBytes(rdb + 0x3F0, src + 0x3F0, 8);  // 4x wWork ushorts
            ok &= SafeWriteBytes(rdb + 0x3FC, src + 0x3FC, 1);  // bRunningFlag
            return ok;
        }

        // Resolve a pointer into the per-slot extras blob.
        uint8_t* extras_slot_ptr(size_t slot) noexcept
        {
            if (!m_extras_ring || slot >= m_ring_frames) return nullptr;
            return m_extras_ring.get() + slot * kExtras_Bytes;
        }

        // -----------------------------------------------------------------
        // Empirical-validation probe (2026-05-14).
        //
        // GOAL: determine, from runtime data, which chara byte ranges in
        // the HgCpuDirect "gap" (chara+0x35A0..+0x44478) are STABLE
        // (pointers/vtables -> safe to overwrite or no-op) vs VARYING
        // (per-frame data -> may need to be captured/restored).
        //
        // CONTEXT: LuxBattleChara_Ctor @ 0x140303810 writes
        // `param_1[0x6b4] = CMatrixBankImpl<768>::vftable` at byte
        // offset 0x35A0.  LuxBattle_InitBoneTransformBuffers sets up
        // three 0xC000-byte bone-transform buffers at chara+~0x3600,
        // chara+~0xF600, chara+~0x1B600 (inline in the chara struct).
        // So chara+0x35A0..+0x27600 is bone-transform territory, NOT a
        // replay-input ring tail as a previous hypothesis suggested.
        //
        // WHAT WE LOG: at captures 1, 60, 600, 1800 (~0s, 1s, 10s, 30s
        // into Replay presence), dump 8 bytes at several chara offsets
        // for both P1 and P2.  Comparing values across the samples
        // reveals which fields are stable vs varying.
        //
        // OFFSETS PROBED:
        //   0x35A0  expected = CMatrixBankImpl<768> vtable pointer
        //                      (stable: same ~0x7FF6xxxx vtable address)
        //   0x35A8  expected = MatrixBank's buffer-A pointer
        //                      (stable: chara+~0x3600 within the chara)
        //   0x3600  expected = bone matrix data (varying frame-to-frame)
        //   0x43F4  dwReplayLookupKey - test if stable or varying
        //   0x4400  dwReplayEnableFlag - test if 0 (replay-viewing dead)
        //                                       or 1 (active replay)
        //   0x4414  dwReplayFrameTarget - test if a sensible frame ID
        //                                 or random
        //   0x4424  bCharaMode - test if 5/2 (state-machine) or random
        // -----------------------------------------------------------------
        std::atomic<uint64_t> m_chara_probe_count {0};

        void chara_probe_log_if_due() noexcept
        {
            // Decide whether this capture is a sample point.
            const uint64_t n = m_chara_probe_count.fetch_add(
                1, std::memory_order_relaxed) + 1;
            const bool due =
                (n == 1)    || (n == 60)   ||
                (n == 600)  || (n == 1800) || (n == 3600);
            if (!due) return;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return;
            const uintptr_t slot_addrs[2] = {
                base + kRVA_CharaSlotP1, base + kRVA_CharaSlotP2,
            };

            for (int pi = 0; pi < 2; ++pi)
            {
                void* chara_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(slot_addrs[pi]),
                                 &chara_raw) || !chara_raw)
                    continue;
                const uint8_t* c = reinterpret_cast<const uint8_t*>(chara_raw);

                uint64_t v_35A0 = 0, v_35A8 = 0, v_3600 = 0;
                uint32_t v_43F4 = 0, v_4400 = 0, v_440C = 0, v_4410 = 0;
                uint32_t v_4414 = 0, v_4420 = 0;
                uint8_t  v_4424 = 0;
                SafeReadUInt64(c + 0x35A0, &v_35A0);
                SafeReadUInt64(c + 0x35A8, &v_35A8);
                SafeReadUInt64(c + 0x3600, &v_3600);
                SafeReadUInt32(c + 0x43F4, &v_43F4);
                SafeReadUInt32(c + 0x4400, &v_4400);
                SafeReadUInt32(c + 0x440C, &v_440C);
                SafeReadUInt32(c + 0x4410, &v_4410);
                SafeReadUInt32(c + 0x4414, &v_4414);
                SafeReadUInt32(c + 0x4420, &v_4420);
                SafeReadUInt8 (c + 0x4424, &v_4424);

                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.probe] sample={} P{} chara=0x{:X} "
                        "+0x35A0=0x{:X} +0x35A8=0x{:X} +0x3600=0x{:X} "
                        "+0x43F4=0x{:X} +0x4400=0x{:X} +0x440C=0x{:X} "
                        "+0x4410=0x{:X} +0x4414=0x{:X} +0x4420=0x{:X} "
                        "+0x4424=0x{:X}\n"),
                    n, pi + 1, reinterpret_cast<uintptr_t>(chara_raw),
                    v_35A0, v_35A8, v_3600,
                    v_43F4, v_4400, v_440C, v_4410, v_4414, v_4420,
                    static_cast<unsigned>(v_4424));
            }
        }

        // Capture WorldModePump + BlockInteractiveOps + cinematic head +
        // BM state bytes into a single per-slot blob (kExtras_Bytes).
        // The blob layout is documented at kExtras_Off_* above.
        //
        // Round-end seek-back fix (2026-05-14): without this, a backward
        // seek from post-KO into mid-round leaves WorldModePump's mode
        // pointer at "round-result", which re-publishes mode==3 to
        // MasterModeFlag next tick, gating PerFrameTick's chara input
        // tick OFF via the BattleAdvanceFlag check.  Captures the live
        // state of all four sub-fields; restores them surgically on seek.
        void capture_extras(size_t slot) noexcept
        {
            uint8_t* dst = extras_slot_ptr(slot);
            if (!dst) return;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return;

            // WorldModePump struct (64 bytes).  Static .data location;
            // pointer-shaped fields (mode pointers, BM ptr, sub-driver
            // ptr) point at long-lived static instances that survive
            // round transitions, so the captured pointer values remain
            // valid at restore time within the same session.
            SafeReadBytes(reinterpret_cast<const void*>(base + kRVA_WorldModePump),
                          dst + kExtras_Off_WorldModePump,
                          kExtras_WorldModePump_Bytes);

            // BlockInteractiveOps (4 bytes).
            SafeReadBytes(reinterpret_cast<const void*>(base + kRVA_BlockInteractiveOps),
                          dst + kExtras_Off_BlockInteractive, 4);

            // RoundResultCinematic head (16 bytes: state / triggers / frame
            // counter).  The cinematic state struct lives at
            // *g_LuxBattle_ActiveSessionDataPtr + kCinematic_State_Off.
            // Between matches the session ptr can be null; in that case
            // we leave this slot's cinematic bytes zeroed.
            void* session_ptr = nullptr;
            SafeReadPtr(reinterpret_cast<const void*>(base + kRVA_ActiveSessionDataPtr),
                        &session_ptr);
            if (session_ptr)
            {
                uint8_t* cin = reinterpret_cast<uint8_t*>(session_ptr) + kCinematic_State_Off;
                SafeReadBytes(cin,
                              dst + kExtras_Off_CinematicHead,
                              kExtras_CinematicHead_Bytes);
            }

            // BM internal state bytes.  Stored as 4-byte aligned slots
            // so the layout is regular (only the low byte of each slot
            // is meaningful).
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (bm_obj)
            {
                uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
                uint8_t b = 0;
                if (SafeReadUInt8(bm + kBM_bMainStateMachineByte_Off, &b))
                    dst[kExtras_Off_BM_MainState] = b;
                if (SafeReadUInt8(bm + kBM_bMoveStateByte_Off, &b))
                    dst[kExtras_Off_BM_MoveState] = b;
                if (SafeReadUInt8(bm + kBM_bStatusByte_Off, &b))
                    dst[kExtras_Off_BM_StatusByte] = b;
                if (SafeReadUInt8(bm + kBM_bEnginePauseFlag_Off, &b))
                    dst[kExtras_Off_BM_EnginePause] = b;

                // PlayerRecordArray gate bits (2026-05-14 fix).
                // pPRA = *(BM+0x440); both players' +0x394/+0x398
                // are captured.  See CopyNextFrameToManager_SetMoveState4
                // @ 0x140435C20 plate for why bit 9 of +0x398 gates the
                // replay's per-frame snapshot copier.
                void* pra_raw = nullptr;
                if (SafeReadPtr(bm + kBM_PlayerRecordArray_Off, &pra_raw) && pra_raw)
                {
                    uint8_t* pra = reinterpret_cast<uint8_t*>(pra_raw);
                    uint32_t v_p0_394 = 0, v_p0_398 = 0;
                    uint32_t v_p1_394 = 0, v_p1_398 = 0;
                    if (SafeReadUInt32(pra + kPRA_FieldAt394_Off, &v_p0_394))
                        std::memcpy(dst + kExtras_Off_PRA_P0_394, &v_p0_394, 4);
                    if (SafeReadUInt32(pra + kPRA_FieldAt398_Off, &v_p0_398))
                        std::memcpy(dst + kExtras_Off_PRA_P0_398, &v_p0_398, 4);
                    if (SafeReadUInt32(pra + kPRA_PlayerStride + kPRA_FieldAt394_Off, &v_p1_394))
                        std::memcpy(dst + kExtras_Off_PRA_P1_394, &v_p1_394, 4);
                    if (SafeReadUInt32(pra + kPRA_PlayerStride + kPRA_FieldAt398_Off, &v_p1_398))
                        std::memcpy(dst + kExtras_Off_PRA_P1_398, &v_p1_398, 4);

                    // First-capture diagnostic: surface the LIVE PRA
                    // bits at the moment of our very first snapshot.
                    // If bit 9 (0x200) is NEVER set in any capture
                    // (because user only paused via mod, never via
                    // game's replay menu), restoring captured bits to
                    // 0 won't enable replay advance and we need to
                    // force bit 9 = 1 instead of restoring.
                    static std::atomic<bool> s_pra_capture_logged{false};
                    if (!s_pra_capture_logged.exchange(true, std::memory_order_relaxed))
                    {
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[ReplayScrub] first PRA capture (live): "
                                "P0+0x394=0x{:X} P0+0x398=0x{:X} "
                                "(fwd_bit9={} rwd_bit8={}) "
                                "P1+0x394=0x{:X} P1+0x398=0x{:X} "
                                "(fwd_bit9={} rwd_bit8={})\n"),
                            v_p0_394, v_p0_398,
                            (v_p0_398 & kPRA_ForwardBit) ? 1 : 0,
                            (v_p0_398 & kPRA_RewindBit)  ? 1 : 0,
                            v_p1_394, v_p1_398,
                            (v_p1_398 & kPRA_ForwardBit) ? 1 : 0,
                            (v_p1_398 & kPRA_RewindBit)  ? 1 : 0);
                    }
                }

                // 2026-05-16 BREAKTHROUGH: capture BM+0x450 ALuxBattleFrameInput
                // per-slot input records.  [BM+0x450]+0x3E0+slot*0x90 is the
                // UPSTREAM source of the entire offline match-replay input
                // pipeline (see kExtras_Off_FrameInput_Slots plate above).
                //
                // Stage 3 doesn't write here - so post-seek-back this struct
                // contains stale live-edge data unless we restore from snapshot.
                // The "EngineInput=0x9 for 7 frames then 0" failure mode is
                // explained by stale [BM+0x450]+0x3E0 propagating through
                // the cache chain.
                void* fi_raw = nullptr;
                if (SafeReadPtr(bm + kBM_FrameInputActor_Off, &fi_raw) && fi_raw)
                {
                    const uint8_t* fi = reinterpret_cast<const uint8_t*>(fi_raw);
                    // Capture 0x120 bytes covering both slot records.
                    for (size_t off = 0;
                         off + 4 <= kFI_SlotRecords_Bytes;
                         off += 4)
                    {
                        uint32_t v32 = 0;
                        if (SafeReadUInt32(fi + kFI_SlotRecords_Start + off, &v32))
                            std::memcpy(dst + kExtras_Off_FrameInput_Slots + off,
                                        &v32, 4);
                    }

                    static std::atomic<bool> s_fi_capture_logged{false};
                    if (!s_fi_capture_logged.exchange(
                            true, std::memory_order_relaxed))
                    {
                        uint32_t p0_input = 0, p1_input = 0;
                        std::memcpy(&p0_input,
                                    dst + kExtras_Off_FrameInput_Slots + 0, 4);
                        std::memcpy(&p1_input,
                                    dst + kExtras_Off_FrameInput_Slots
                                        + kFI_SlotRecord_Stride, 4);
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[ReplayScrub] first FrameInput capture: "
                                "BM+0x450=0x{:X} P0_input(+0x3E0)=0x{:X} "
                                "P1_input(+0x470)=0x{:X} ({} bytes total)\n"),
                            reinterpret_cast<uintptr_t>(fi_raw),
                            p0_input, p1_input,
                            static_cast<unsigned>(kFI_SlotRecords_Bytes));
                    }
                }
            }

            // ALuxBattleReplayPlayer playback-cursor capture
            // (2026-05-15 architectural reset): capture the LIVE
            // CurrentTime / CurrentRound / bIsPlayingBack at snapshot
            // moment.  These ARE the BP-level playback cursor that the
            // replay menu reads each tick to decide which recorded
            // frame to dispatch.  Restoring captured values (not
            // derived ones) means the engine resumes from exactly the
            // playback head it was at when we snapshotted.
            //
            // The actor lookup uses the shared GlobalPtr cache from
            // ReplayScrubDiag (revalidates each call).  Between matches
            // the actor can be null - we then leave the captured RP
            // bytes zeroed, and restore_extras will skip the write.
            // Zero the three RP extras fields first: round_tag is now a
            // timeline coordinate (m_round_tags), so on a wrapped slot a
            // null actor or a failed read must leave a safe default
            // (round 0) rather than stale bytes from the slot's prior
            // occupant.
            {
                const int32_t z = 0;
                std::memcpy(dst + kExtras_Off_RP_CurrentTime,  &z, 4);
                std::memcpy(dst + kExtras_Off_RP_CurrentRound, &z, 4);
                dst[kExtras_Off_RP_IsPlayingBack] = 0;
            }
            if (RC::Unreal::UObject* rp_obj =
                    ReplayScrubDiag::replay_player_ptr().get(
                        L"LuxBattleReplayPlayer"))
            {
                uint8_t* a = reinterpret_cast<uint8_t*>(rp_obj);
                float   t = 0.0f;
                int32_t r = 0;
                uint8_t p = 0;
                if (SafeReadFloat(a + kRP_CurrentTime_Off,   &t))
                    std::memcpy(dst + kExtras_Off_RP_CurrentTime,   &t, 4);
                if (SafeReadInt32(a + kRP_CurrentRound_Off,  &r))
                    std::memcpy(dst + kExtras_Off_RP_CurrentRound,  &r, 4);
                if (SafeReadUInt8(a + kRP_IsPlayingBack_Off, &p))
                    dst[kExtras_Off_RP_IsPlayingBack] = p;
            }

            // 2026-05-15 (ultrathink): Capture chara replay-state fields
            // at chara+0x43F4..+0x4428 (52 bytes each, both charas).
            // These fields are NOT covered by HgCpuDirect (which captures
            // only chara+0x90..+0x35A0).  They include dwReplayFrameTarget
            // at +0x4414 which Stage 2 of the input pipeline reads to
            // validate decoded packets.  Without restoring, post-seek
            // Stage 2 may reject packets once the cache window runs out.
            {
                const uintptr_t base = NativeBinding::imageBase();
                if (base)
                {
                    for (int pi = 0; pi < 2; ++pi)
                    {
                        const uintptr_t slot_rva = (pi == 0)
                            ? ReplayScrubDiag::kRVA_CharaSlotP1
                            : ReplayScrubDiag::kRVA_CharaSlotP2;
                        void* chara_raw = nullptr;
                        if (!SafeReadPtr(
                                reinterpret_cast<const void*>(base + slot_rva),
                                &chara_raw) || !chara_raw)
                            continue;
                        const uint8_t* c = reinterpret_cast<const uint8_t*>(chara_raw);
                        const size_t blob_off = (pi == 0)
                            ? kExtras_Off_P1_CharaReplay
                            : kExtras_Off_P2_CharaReplay;
                        // SafeReadBytes is not exposed; use repeated
                        // SafeReadUInt32 for each 4-byte chunk in the
                        // range, then 1 byte for the trailing bCharaMode.
                        uint32_t buf32 = 0;
                        for (size_t off = 0;
                             off + 4 <= kExtras_CharaReplay_Bytes;
                             off += 4)
                        {
                            if (SafeReadUInt32(c + kChara_ReplayState_Start + off,
                                               &buf32))
                                std::memcpy(dst + blob_off + off, &buf32, 4);
                        }
                    }
                }
            }

            // Empirical-validation probe (2026-05-14): log chara
            // bytes at several offsets a few times per session so the
            // user can verify which fields are STABLE (pointers) vs
            // VARYING (data) before we commit to capturing/restoring
            // any of them.  Sample at captures 1, 60, 600, 1800 (~0s,
            // 1s, 10s, 30s).  Cheap, single-shot per sample.
            chara_probe_log_if_due();
        }

        // Restore the round-end-fix extras blob into the engine.  Called
        // from do_seek_to_seq AFTER the HgCpuDirect + IL + RDB restores.
        // Surgical writes to four distinct memory locations.
        void restore_extras(size_t slot) noexcept
        {
            const uint8_t* src = extras_slot_ptr(slot);
            if (!src) return;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return;

            // 2026-05-15 SAFETY GATE: speculative restores caused crashes
            // on un-pause after seek (PRA bits with garbage values,
            // bIsPlayingBack=0x5F being a packed-bool container, etc.).
            // The "safe baseline" was: HgCpuDirect chara restore + IL
            // window + RDB cursors + BM cursor pair only.
            //
            // 2026-05-15 ROUND-STATUS FIX: empirical evidence from
            // 22:00-22:01 testing showed BM->bStatusByte stayed at 0x3
            // (round-end) after seeking from late-round to early-round.
            // This kept the engine in round-end mode -> no input flow,
            // chars idle.  The 4 BM state bytes (mainState/moveState/
            // status/enginePause) are PLAIN HEAP WRITES to a valid BM
            // pointer (we just succeeded restoring IL state through it),
            // not the crashy ones - PRA bits, ReplayPlayer cursor, and
            // WorldModePump pointer-write are what caused the crash.
            // Apply ONLY the 4 BM bytes always; gate the rest.
            RC::Unreal::UObject* bm_obj_minimal = m_bm_ptr.get(L"LuxBattleManager");
            if (bm_obj_minimal)
            {
                uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj_minimal);
                bm[kBM_bMainStateMachineByte_Off] = src[kExtras_Off_BM_MainState];
                bm[kBM_bMoveStateByte_Off]        = src[kExtras_Off_BM_MoveState];
                bm[kBM_bStatusByte_Off]           = src[kExtras_Off_BM_StatusByte];
                bm[kBM_bEnginePauseFlag_Off]      = src[kExtras_Off_BM_EnginePause];

                static std::atomic<bool> s_logged_bm_minimal{false};
                if (!s_logged_bm_minimal.exchange(true, std::memory_order_relaxed))
                {
                    RC::Output::send<RC::LogLevel::Default>(
                        STR("[ReplayScrub] first BM minimal restore: "
                            "main=0x{:X} move=0x{:X} status=0x{:X} pause=0x{:X}\n"),
                        static_cast<unsigned>(src[kExtras_Off_BM_MainState]),
                        static_cast<unsigned>(src[kExtras_Off_BM_MoveState]),
                        static_cast<unsigned>(src[kExtras_Off_BM_StatusByte]),
                        static_cast<unsigned>(src[kExtras_Off_BM_EnginePause]));
                }
            }

            // 2026-05-15 (ultrathink): restore chara replay-state fields
            // at chara+0x43F4..+0x4428.  These are plain heap writes to
            // valid chara structs (we just restored chara state via
            // HgCpuDirect through these same pointers, just at lower
            // offsets).
            //
            // Stage 2 of the replay input pipeline reads
            // chara->nReplayFrameTarget_at0x4414 to validate decoded
            // packets.  Without restoring this, post-seek Stage 2 may
            // reject all packets once the cached window runs out.
            //
            // Gated by m_enable_chara_4400_restore (default ON).  Older
            // Ghidra plate observed these fields contain VFX bytes in
            // match-replay viewing - if restoring them is harmful, the
            // user can toggle this off via the Replay tab.
            if (m_enable_chara_4400_restore.load(std::memory_order_acquire))
            {
                const uintptr_t base = NativeBinding::imageBase();
                if (base)
                {
                    for (int pi = 0; pi < 2; ++pi)
                    {
                        const uintptr_t slot_rva = (pi == 0)
                            ? ReplayScrubDiag::kRVA_CharaSlotP1
                            : ReplayScrubDiag::kRVA_CharaSlotP2;
                        void* chara_raw = nullptr;
                        if (!SafeReadPtr(
                                reinterpret_cast<const void*>(base + slot_rva),
                                &chara_raw) || !chara_raw)
                            continue;
                        uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);
                        const size_t blob_off = (pi == 0)
                            ? kExtras_Off_P1_CharaReplay
                            : kExtras_Off_P2_CharaReplay;
                        SafeWriteBytes(c + kChara_ReplayState_Start,
                                       src + blob_off,
                                       kExtras_CharaReplay_Bytes);
                    }

                    static std::atomic<bool> s_logged_chara_replay{false};
                    if (!s_logged_chara_replay.exchange(
                            true, std::memory_order_relaxed))
                    {
                        uint32_t p1_target = 0, p2_target = 0;
                        std::memcpy(&p1_target,
                                    src + kExtras_Off_P1_CharaReplay +
                                          (0x4414 - kChara_ReplayState_Start),
                                    4);
                        std::memcpy(&p2_target,
                                    src + kExtras_Off_P2_CharaReplay +
                                          (0x4414 - kChara_ReplayState_Start),
                                    4);
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[ReplayScrub] first chara replay-state "
                                "restore: P1 nFrameTarget=0x{:X} "
                                "P2 nFrameTarget=0x{:X} "
                                "({} bytes/chara)\n"),
                            p1_target, p2_target,
                            static_cast<unsigned>(kExtras_CharaReplay_Bytes));
                    }
                }
            }

            // 2026-05-16 BREAKTHROUGH: restore [BM+0x450]+0x3E0..+0x500
            // (ALuxBattleFrameInput per-slot input records).  This is the
            // UPSTREAM source of the cache writer's input value.  Without
            // restoring this, post-seek the writer reads stale live-edge
            // data, and the cache mirrors it for ~7 frames before the chain
            // self-clears.  Static analysis confirmed:
            //   - Stage 3 doesn't write here
            //   - No heap pointers in the restored range
            //   - Bytewise restore is safe
            //
            // This is the proximate fix for the "EngineInput=0x9 for 7
            // frames then 0" post-seek failure pattern.
            {
                RC::Unreal::UObject* bm_obj_fi =
                    m_bm_ptr.get(L"LuxBattleManager");
                if (bm_obj_fi)
                {
                    uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj_fi);
                    void* fi_raw = nullptr;
                    if (SafeReadPtr(bm + kBM_FrameInputActor_Off, &fi_raw)
                        && fi_raw)
                    {
                        uint8_t* fi = reinterpret_cast<uint8_t*>(fi_raw);
                        SafeWriteBytes(fi + kFI_SlotRecords_Start,
                                       src + kExtras_Off_FrameInput_Slots,
                                       kFI_SlotRecords_Bytes);

                        static std::atomic<bool> s_logged_fi_restore{false};
                        if (!s_logged_fi_restore.exchange(
                                true, std::memory_order_relaxed))
                        {
                            uint32_t p0_input = 0, p1_input = 0;
                            std::memcpy(&p0_input,
                                        src + kExtras_Off_FrameInput_Slots, 4);
                            std::memcpy(&p1_input,
                                        src + kExtras_Off_FrameInput_Slots
                                            + kFI_SlotRecord_Stride, 4);
                            RC::Output::send<RC::LogLevel::Default>(
                                STR("[ReplayScrub] first FrameInput restore: "
                                    "BM+0x450=0x{:X} P0_input(+0x3E0)=0x{:X} "
                                    "P1_input(+0x470)=0x{:X} ({} bytes)\n"),
                                reinterpret_cast<uintptr_t>(fi_raw),
                                p0_input, p1_input,
                                static_cast<unsigned>(kFI_SlotRecords_Bytes));
                        }
                    }
                }
            }

            // Risky speculative writes (WorldModePump pointer, PRA bits,
            // ReplayPlayer cursor) stay gated.  These caused the
            // 2026-05-15 crash.
            if (!m_enable_speculative_restore.load(std::memory_order_acquire))
            {
                static std::atomic<bool> s_logged_skip{false};
                if (!s_logged_skip.exchange(true, std::memory_order_relaxed))
                {
                    RC::Output::send<RC::LogLevel::Default>(
                        STR("[ReplayScrub] restore_extras: applied 4 BM "
                            "state bytes; SKIPPED risky writes (WorldModePump "
                            "ptr, PRA bits, ReplayPlayer cursor) due to "
                            "safety gate closed after 2026-05-15 crash.\n"));
                }
                return;
            }

            // Snapshot the captured WorldModePump's current-mode pointer
            // BEFORE writing so the first-fire log can show the value.
            // This is the field whose corruption / staleness causes the
            // "round-end stuck" symptom; logging it once per session
            // lets the user verify the captured pointer is sensible.
            void* captured_mode_ptr = nullptr;
            std::memcpy(&captured_mode_ptr,
                        src + kExtras_Off_WorldModePump,
                        sizeof(captured_mode_ptr));

            // Snapshot LIVE values pre-write for the first-fire log.
            void* live_mode_ptr = nullptr;
            uint32_t live_block_interactive = 0xFFFFFFFFu;
            SafeReadPtr(reinterpret_cast<const void*>(base + kRVA_WorldModePump),
                        &live_mode_ptr);
            SafeReadUInt32(reinterpret_cast<const void*>(base + kRVA_BlockInteractiveOps),
                           &live_block_interactive);

            // WorldModePump struct.  Writing the mode pointer puts the
            // state machine back at the captured mid-round mode object;
            // next AdvanceWorldModePump tick publishes that mode's
            // GetModeType() to MasterModeFlag, un-gating PerFrameTick.
            SafeWriteBytes(reinterpret_cast<void*>(base + kRVA_WorldModePump),
                           src + kExtras_Off_WorldModePump,
                           kExtras_WorldModePump_Bytes);

            // BlockInteractiveOps.  Cleared at capture-time if we
            // captured mid-round (interactive ops were allowed), so
            // restoring the captured value un-blocks chara controls
            // even if the live engine had set it to 1 during the
            // round-end cinematic.
            SafeWriteBytes(reinterpret_cast<void*>(base + kRVA_BlockInteractiveOps),
                           src + kExtras_Off_BlockInteractive, 4);

            // Cinematic state head.
            void* session_ptr = nullptr;
            SafeReadPtr(reinterpret_cast<const void*>(base + kRVA_ActiveSessionDataPtr),
                        &session_ptr);
            if (session_ptr)
            {
                uint8_t* cin = reinterpret_cast<uint8_t*>(session_ptr) + kCinematic_State_Off;
                SafeWriteBytes(cin,
                               src + kExtras_Off_CinematicHead,
                               kExtras_CinematicHead_Bytes);
            }

            // BM state bytes already written above (always applied).
            // Below we only do the speculative-gate-open writes (PRA + RP).
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (bm_obj)
            {
                uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);

                // PlayerRecordArray gate bits restore.  Direct dword
                // writes to the live PRA fields.  Forward bit (0x200)
                // must be set for CopyNextFrameToManager to advance
                // dwPlaybackCursor and dispatch the next-frame snapshot.
                void* pra_raw = nullptr;
                if (SafeReadPtr(bm + kBM_PlayerRecordArray_Off, &pra_raw) && pra_raw)
                {
                    uint8_t* pra = reinterpret_cast<uint8_t*>(pra_raw);
                    SafeWriteBytes(pra + kPRA_FieldAt394_Off,
                                   src + kExtras_Off_PRA_P0_394, 4);
                    SafeWriteBytes(pra + kPRA_FieldAt398_Off,
                                   src + kExtras_Off_PRA_P0_398, 4);
                    SafeWriteBytes(pra + kPRA_PlayerStride + kPRA_FieldAt394_Off,
                                   src + kExtras_Off_PRA_P1_394, 4);
                    SafeWriteBytes(pra + kPRA_PlayerStride + kPRA_FieldAt398_Off,
                                   src + kExtras_Off_PRA_P1_398, 4);

                    // First-fire log: surface the captured forward/rewind
                    // bits so the user can see if the captured snapshot
                    // had a valid forward-play state.
                    static std::atomic<bool> s_pra_logged{false};
                    if (!s_pra_logged.exchange(true, std::memory_order_relaxed))
                    {
                        uint32_t p0_398 = 0, p1_398 = 0;
                        std::memcpy(&p0_398, src + kExtras_Off_PRA_P0_398, 4);
                        std::memcpy(&p1_398, src + kExtras_Off_PRA_P1_398, 4);
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[ReplayScrub] first PRA restore: "
                                "P0+0x398=0x{:X} (rewind={} forward={}) "
                                "P1+0x398=0x{:X} (rewind={} forward={})\n"),
                            p0_398,
                            (p0_398 & kPRA_RewindBit)  ? 1 : 0,
                            (p0_398 & kPRA_ForwardBit) ? 1 : 0,
                            p1_398,
                            (p1_398 & kPRA_RewindBit)  ? 1 : 0,
                            (p1_398 & kPRA_ForwardBit) ? 1 : 0);
                    }
                }
            }

            // ALuxBattleReplayPlayer playback-cursor restore (2026-05-15
            // architectural reset).  THIS is the cursor the BP-level
            // replay menu reads each tick to dispatch recorded frames.
            // Without this, the menu keeps reading the live-edge frame
            // and dispatches it into our restored chara state -
            // observable symptom: "chars finish current move and idle".
            //
            // Writes captured CurrentTime + CurrentRound verbatim
            // (no master_tag / 60.0f derivation, no hardcoded round 0).
            // Forces bIsPlayingBack to whatever the capture had (which
            // should be 1 since captures are gated on !m_paused, and
            // forward play has bIsPlayingBack=1).
            float   captured_rp_time  = 0.0f;
            int32_t captured_rp_round = 0;
            uint8_t captured_rp_play  = 0;
            std::memcpy(&captured_rp_time,  src + kExtras_Off_RP_CurrentTime,  4);
            std::memcpy(&captured_rp_round, src + kExtras_Off_RP_CurrentRound, 4);
            captured_rp_play = src[kExtras_Off_RP_IsPlayingBack];

            float   live_rp_time  = -1.0f;
            int32_t live_rp_round = -1;
            uint8_t live_rp_play  = 0;
            if (RC::Unreal::UObject* rp_obj =
                    ReplayScrubDiag::replay_player_ptr().get(
                        L"LuxBattleReplayPlayer"))
            {
                uint8_t* a = reinterpret_cast<uint8_t*>(rp_obj);
                SafeReadFloat(a + kRP_CurrentTime_Off,   &live_rp_time);
                SafeReadInt32(a + kRP_CurrentRound_Off,  &live_rp_round);
                SafeReadUInt8(a + kRP_IsPlayingBack_Off, &live_rp_play);
                // Restore captured values verbatim.  Don't force
                // bIsPlayingBack=1 unconditionally - if the capture
                // had it 1 (normal case), we write 1; if for some
                // reason it had 0, we preserve that.
                SafeWriteBytes(a + kRP_CurrentTime_Off,   src + kExtras_Off_RP_CurrentTime,   4);
                SafeWriteBytes(a + kRP_CurrentRound_Off,  src + kExtras_Off_RP_CurrentRound,  4);
                SafeWriteBytes(a + kRP_IsPlayingBack_Off, src + kExtras_Off_RP_IsPlayingBack, 1);
            }

            // First-fire log: confirm the round-end-fix path fires + show
            // captured vs live mode-pointer values so a stale/invalid
            // capture is visible at a glance.  Single-shot per session
            // to avoid log spam.
            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub] first extras restore: "
                        "WorldModePump.pCurrentMode live=0x{:X} -> "
                        "captured=0x{:X}, BlockInteractiveOps live={} -> "
                        "captured={}, BM_State[main=0x{:X} move=0x{:X} "
                        "status=0x{:X} ePause=0x{:X}], "
                        "ReplayPlayer[CurrentTime live={:.4f} -> captured={:.4f}, "
                        "CurrentRound live={} -> captured={}, "
                        "bIsPlayingBack live={} -> captured={}]\n"),
                    reinterpret_cast<uintptr_t>(live_mode_ptr),
                    reinterpret_cast<uintptr_t>(captured_mode_ptr),
                    live_block_interactive,
                    *reinterpret_cast<const uint32_t*>(src + kExtras_Off_BlockInteractive),
                    static_cast<unsigned>(src[kExtras_Off_BM_MainState]),
                    static_cast<unsigned>(src[kExtras_Off_BM_MoveState]),
                    static_cast<unsigned>(src[kExtras_Off_BM_StatusByte]),
                    static_cast<unsigned>(src[kExtras_Off_BM_EnginePause]),
                    live_rp_time, captured_rp_time,
                    live_rp_round, captured_rp_round,
                    static_cast<unsigned>(live_rp_play),
                    static_cast<unsigned>(captured_rp_play));
            }
        }

        // Find the ring slot whose seq tag matches `target_seq`
        // exactly, or the closest <= target_seq.  Returns -1 if no
        // acceptable slot.  Visit-once linear scan over the ring; cheap
        // on a one-shot user action.  Bounds-checked against
        // m_seq_tags.size() so a concurrent free_ring() teardown can't
        // fault the subscript.
        int32_t find_slot_for_seq(int32_t target_seq) const noexcept
        {
            int32_t best_slot = -1;
            int32_t best_tag  = -1;
            const size_t cap = m_ring_frames;
            if (cap > m_seq_tags.size()) return -1;
            for (size_t i = 0; i < cap; ++i)
            {
                const int32_t tag =
                    m_seq_tags[i].load(std::memory_order_acquire);
                if (tag < 0) continue;
                if (tag > target_seq) continue;
                if (tag > best_tag)
                {
                    best_tag  = tag;
                    best_slot = static_cast<int32_t>(i);
                }
            }
            return best_slot;
        }

        // Restore the simulation from the ring slot whose seq tag
        // is closest <= target_seq.  Does NOT touch m_paused - pause state
        // is purely UI-driven (drag-start sets it, drag-end optionally
        // clears it, Play/Pause and step buttons set it explicitly).
        //
        // Why no pause-management here: there's a thread race between
        // the UI thread (which fires on_drag_end on mouse release) and
        // the game thread (which runs service_seek_request -> this
        // function on the next cockpit tick).  If this function
        // unconditionally set m_paused=true, an in-flight seek that
        // landed AFTER on_drag_end would re-engage pause and silently
        // defeat the auto-resume contract.  Keeping pause UI-driven
        // closes the race: the UI's last action wins.
        //
        // No-op if no acceptable slot exists (e.g. seek beyond captured
        // range or before any captures were taken).
        void do_seek_to_seq(int32_t target_seq) noexcept
        {
            if (!m_data || !m_exec_read) return;
            const int32_t slot = find_slot_for_seq(target_seq);
            if (slot < 0) return;

            uint8_t* slot_data =
                m_data.get() + static_cast<size_t>(slot) * kSnapshotStride;

            const int32_t seq_tag    = m_seq_tags[static_cast<size_t>(slot)]
                                         .load(std::memory_order_acquire);
            const int32_t wall_tag   = m_frame_tags[static_cast<size_t>(slot)]
                                         .load(std::memory_order_acquire);
            const int32_t master_tag = m_master_tags[static_cast<size_t>(slot)]
                                         .load(std::memory_order_acquire);

            // Defence in depth: capture_snapshot now skips slots that
            // failed to read the master clock, so this should never
            // fire.  If it ever does, refuse the seek rather than
            // poison the engine cursors with -1.
            if (master_tag < 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[ReplayScrub] seek refused: slot={} wall_tag={} "
                        "has master_tag=-1 (capture-time master clock "
                        "was unreadable)\n"),
                    slot, wall_tag);
                return;
            }

            // Seek diagnostics (verbose-only).  At scrub-drag rates a
            // seek fires every frame, so unconditional PRE/POST_SEEK
            // dumps + the 600-frame per-tick POST_SEEK_TICK trace were
            // the dominant replay-viewing log-I/O cost (~76% of the
            // log, the "laggy playback" the user reported).  Gated
            // behind the Verbose-log toggle: silent in normal use, full
            // diagnostics when the user opts in.
            const bool verbose =
                m_verbose_diag.load(std::memory_order_acquire);
            if (verbose)
            {
                // Full replay-system state before the restore; pairs
                // with POST_SEEK below for a side-by-side log diff.
                dump_replay_state("PRE_SEEK", seq_tag);
                ReplayScrubDiag::dump_full("PRE_SEEK");
            }

            // Step 1: Restore full simulation state (chara, globals,
            // terrain, camera, timer, motion, physics, VFX) to the
            // snapshot frame.  This is the heavy lifting - writes
            // ~80-100 KB of engine state.
            m_shim.retarget(slot_data, kSnapshotStride);
            m_exec_read(&m_shim);

            // Step 2: Restore the captured InputLog state window
            // (pInputLog+0x394..+0x4414).  Reproduces the engine's
            // entire replay-playback bookkeeping at the captured
            // frame: cache entries, drain cursor, double-tick guard,
            // playback cursor, master/last-frame IDs, and any other
            // hidden state in the +0x394..+0x4414 byte range.
            restore_input_cache(static_cast<size_t>(slot));

            // Step 3: Restore the Stage 1 decoder's state (*pBM+0x460,
            // FLuxReplayDataBlock, 1021 bytes).  This rewinds the
            // file-read cursor, decoded-buffer read/write cursors,
            // and working-frame state to where the decoder had them
            // at the snapshot frame.  Without this, the engine's
            // per-frame input consumer keeps reading packets from
            // the LIVE-edge file position, so the chara state
            // (restored to T) gets fed inputs from frame F onwards
            // - the "plays inputs from later in the round" symptom
            // the user reported on 2026-05-11.
            restore_replay_data_block(static_cast<size_t>(slot));

            // Step 4: Restore round-end-fix extras (WorldModePump +
            // BlockInteractiveOps + cinematic head + BM state bytes).
            //
            // Done BEFORE the BM cursor write below so the BM state-
            // byte restore doesn't clobber values the cursor sync
            // depends on.  See capture_extras / restore_extras for the
            // exact byte ranges; the round-end seek-back failure mode
            // (characters frozen post-restore because PerFrameTick's
            // BattleAdvanceFlag check still sees mode==3) is what this
            // step fixes.
            restore_extras(static_cast<size_t>(slot));

            // Step 5: Sync the BM-side replay cursors (BM+0x148C
            // nReplayLastApplied AND BM+0x1488 nReplayLastFrameID).
            // These are the only BM fields outside both the
            // HgCpuDirect chara/global window AND the InputLog/
            // DecoderBlock state we just restored.
            //
            // Read the snapshot's nLastFrameID from the captured IL
            // blob (offset +0xC within the blob since the capture
            // starts at +0x394 and nLastFrameID is at +0x3A0).  We
            // must write BM+0x1488 to THIS value, not to master_tag,
            // because SimulationLoop's mismatch check compares the
            // BM-side cache against the live IL field which is the
            // snapshot value after restore_input_cache.
            int32_t snapshot_last_frame_id = -1;
            if (const uint8_t* il_blob =
                    input_log_slot_ptr(static_cast<size_t>(slot)))
            {
                std::memcpy(&snapshot_last_frame_id,
                            il_blob + (kIL_nLastFrameID_Off
                                       - kIL_CaptureStart_Off),
                            sizeof(snapshot_last_frame_id));
            }
            write_replay_cursors(master_tag, snapshot_last_frame_id);

            // Step 6 (EXPERIMENTAL): rewind the UE4-level playback
            // (Old Step 6 removed 2026-05-15.) The ReplayPlayer
            // CurrentTime/CurrentRound/bIsPlayingBack write now lives
            // in restore_extras() above and uses the per-snapshot
            // CAPTURED live values (not master_tag/60 derivation and
            // not hardcoded round 0).  Cleaner: one place, accurate,
            // multi-round safe.

            m_last_seek_target.store(seq_tag, std::memory_order_release);
            // Record the master clock value the engine is now at so the
            // UI playhead can extrapolate forward as master advances.
            m_last_seek_master_tag.store(master_tag, std::memory_order_release);

            // 2026-05-15 (ultrathink): user-toggleable force-writes for
            // bisection.  Each independent of the safety-gated
            // restore_extras path.
            //
            // Force PRA bit 9 (FORWARD play) = 1 on both players.  Per
            // CopyNextFrameToManager_SetMoveState4 plate, this bit gates
            // the ReplayPlayer's per-frame snapshot dispatcher.  If the
            // engine post-seek isn't dispatching, this might be needed.
            if (m_force_pra_forward_bit_on_seek.load(std::memory_order_acquire))
            {
                RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
                if (bm_obj)
                {
                    uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
                    void* pra_raw = nullptr;
                    if (SafeReadPtr(bm + kBM_PlayerRecordArray_Off, &pra_raw)
                        && pra_raw)
                    {
                        uint8_t* pra = reinterpret_cast<uint8_t*>(pra_raw);
                        for (int p = 0; p < 2; ++p)
                        {
                            uint32_t v = 0;
                            if (SafeReadUInt32(pra + p * kPRA_PlayerStride
                                                   + kPRA_FieldAt398_Off, &v))
                            {
                                v |= kPRA_ForwardBit;
                                SafeWriteBytes(pra + p * kPRA_PlayerStride
                                                  + kPRA_FieldAt398_Off,
                                               &v, 4);
                            }
                        }
                        static std::atomic<bool> s_logged_force_fwd{false};
                        if (!s_logged_force_fwd.exchange(
                                true, std::memory_order_relaxed))
                        {
                            RC::Output::send<RC::LogLevel::Default>(
                                STR("[ReplayScrub] first force-write of "
                                    "PRA bit 9 (FORWARD) on seek\n"));
                        }
                    }
                }
            }

            // Force bIsPlayingBack = 1 on ReplayPlayer.  Tells engine
            // "we're playing back; don't end the replay".
            if (m_force_isplayingback_on_seek.load(std::memory_order_acquire))
            {
                if (RC::Unreal::UObject* rp_obj =
                        ReplayScrubDiag::replay_player_ptr().get(
                            L"LuxBattleReplayPlayer"))
                {
                    uint8_t* a = reinterpret_cast<uint8_t*>(rp_obj);
                    const uint8_t one = 1;
                    // RP+0x3D0 = bIsPlayingBack (BoolProperty, 1 byte).
                    // Verified via ALuxBattleReplayPlayer_RegisterProperties
                    // @ 0x14097beb0.
                    SafeWriteBytes(a + 0x3D0, &one, 1);
                    static std::atomic<bool> s_logged_force_play{false};
                    if (!s_logged_force_play.exchange(
                            true, std::memory_order_relaxed))
                    {
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[ReplayScrub] first force-write of "
                                "bIsPlayingBack=1 on seek\n"));
                    }
                }
            }

            if (verbose)
            {
                // Dump again so we can diff against PRE_SEEK and the
                // BASELINE-at-tag log.  Any field whose value is wrong
                // here is a candidate for a "playback broke after
                // seeking" investigation.
                dump_replay_state("POST_SEEK", wall_tag);
                ReplayScrubDiag::dump_full("POST_SEEK");

                // Arm the per-tick post-seek dump for a tick-by-tick
                // view over the next kDefaultPostSeekDumpFrames frames.
                m_post_seek_countdown.store(kDefaultPostSeekDumpFrames,
                                            std::memory_order_release);
                // Seed the last-seen snapshots so the first tick's
                // CHANGED/UNCHANGED line compares against POST_SEEK
                // state, not against stale pre-seek data.
                m_last_movevm_p1 = ReplayScrubDiag::read_chara_movevm(0);
                m_last_movevm_p2 = ReplayScrubDiag::read_chara_movevm(1);
                // Seed wall/master/paused so first-tick deltas + the
                // pause-transition marker start from POST_SEEK state.
                uint32_t cur_seed = 0;
                read_frame_counter(cur_seed);
                m_diag_last_wall   = cur_seed;
                m_diag_last_master = master_tag;
                m_diag_last_paused =
                    m_paused.load(std::memory_order_acquire);

                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub] seek target_seq={} -> slot={} "
                        "wall_tag={} master_tag={} (paused={})  armed "
                        "post-seek tick dump for {} frames\n"),
                    target_seq, slot, wall_tag, master_tag,
                    m_paused.load(std::memory_order_acquire) ? 1 : 0,
                    kDefaultPostSeekDumpFrames);
            }
        }

        // -----------------------------------------------------------------
        // Diagnostic dump - logs the full replay-system state to
        // UE4SS.log.  Called pre- and post-seek to compare what the
        // restore produced against what the engine had at the same
        // frame during forward playback, plus periodically during
        // forward playback itself for baseline reference data.
        //
        // Three labels in use:
        //   "BASELINE"  - periodic forward-playback dump (every 60
        //                 frames, ~once per second)
        //   "PRE_SEEK"  - state right before ExecFinalizeAndPost runs
        //   "POST_SEEK" - state after ExecFinalizeAndPost +
        //                 write_replay_cursors
        //
        // To diff: grep UE4SS.log for "[ReplayScrub.dump]", line up
        // the BASELINE log nearest to the seek target_frame against
        // the POST_SEEK log; any field that differs is something the
        // restore + cursor-sync didn't fully recreate.
        // -----------------------------------------------------------------
        void dump_replay_state(const char* label,
                               int32_t hint_frame) noexcept
        {
            // Resolve BM via UObjectGlobals (cached).
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.dump] {} hint={} BM=null\n"),
                    RC::to_generic_string(label), hint_frame);
                return;
            }
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);

            // BM-side fields.
            int32_t  bm_lastApplied   = 0;
            int32_t  bm_lastFrameID   = 0;
            int32_t  bm_frameAdvance  = 0;
            uint8_t  bm_moveState     = 0;
            void*    il_ptr           = nullptr;
            SafeReadInt32 (bm + kBM_nReplayLastApplied_Off,   &bm_lastApplied);
            SafeReadInt32 (bm + kBM_nReplayLastFrameID_Off,   &bm_lastFrameID);
            SafeReadInt32 (bm + kBM_nFrameAdvanceCounter_Off, &bm_frameAdvance);
            SafeReadUInt8 (bm + kBM_bMoveStateByte_Off,       &bm_moveState);
            SafeReadPtr   (bm + kBM_BattleFrameInputLog_Off,  &il_ptr);

            // InputLog fields.
            uint32_t il_dwPlaybackCursor = 0;
            int32_t  il_nLastFrameID     = 0;
            int32_t  il_nMasterClock     = 0;
            int32_t  il_nTotalFrames     = 0;
            uint8_t  il_doubleTick       = 0;
            int32_t  il_drainCursor      = 0;
            if (il_ptr)
            {
                uint8_t* il = reinterpret_cast<uint8_t*>(il_ptr);
                SafeReadUInt32(il + kIL_dwPlaybackCursor_Off,  &il_dwPlaybackCursor);
                SafeReadInt32 (il + kIL_nLastFrameID_Off,      &il_nLastFrameID);
                SafeReadInt32 (il + kIL_nMasterClock_Off,      &il_nMasterClock);
                SafeReadInt32 (il + kIL_nTotalRecordedFrames_Off, &il_nTotalFrames);
                SafeReadUInt8 (il + kIL_bDoubleTickGuard_Off,  &il_doubleTick);
                SafeReadInt32 (il + kIL_nDrainCursor_Off,      &il_drainCursor);
            }

            uint32_t global_counter = 0;
            read_frame_counter(global_counter);

            // For seek-time dumps, also surface the slot's stored
            // master_tag so the diff with the BM-side IL[master=...]
            // makes the bug-mode obvious at a glance.
            int32_t slot_master_tag = -1;
            if (m_ring_frames > 0)
            {
                // Find the slot matching this hint frame and report
                // its captured master_tag; -1 if not in ring.
                const int32_t s = find_slot_for_seq(hint_frame);
                if (s >= 0)
                    slot_master_tag = m_master_tags[static_cast<size_t>(s)]
                                         .load(std::memory_order_acquire);
            }

            // UI playhead diagnostic (2026-05-15): surface what
            // current_play_position() would return RIGHT NOW.  Lets us
            // verify the UI extrapolation math by reading the log.
            const int32_t ui_seek_target = m_last_seek_target.load(
                std::memory_order_acquire);
            const int32_t ui_seek_master = m_last_seek_master_tag.load(
                std::memory_order_acquire);
            const int32_t ui_live_master = m_live_master_cached.load(
                std::memory_order_acquire);
            const int32_t ui_playhead    = current_play_position();
            const int     ui_paused      = is_paused() ? 1 : 0;

            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub.dump] {} hint={} g_FrameCounter={} "
                    "slot_master={} "
                    "BM[lastApplied={} lastFrameID={} frameAdv={} moveState={}] "
                    "IL[playCur={} lastFrame={} master={} total={} dblTick={} drain={}] "
                    "UI[seek_target={} seek_master={} live_master_cached={} "
                    "playhead={} paused={}]\n"),
                RC::to_generic_string(label),
                hint_frame, global_counter, slot_master_tag,
                bm_lastApplied, bm_lastFrameID, bm_frameAdvance,
                static_cast<int>(bm_moveState),
                il_dwPlaybackCursor, il_nLastFrameID, il_nMasterClock,
                il_nTotalFrames, static_cast<int>(il_doubleTick),
                il_drainCursor,
                ui_seek_target, ui_seek_master, ui_live_master,
                ui_playhead, ui_paused);

            // Dump PlayerRecordArrayPtr+0x394/+0x398 button bits for
            // both players.  These drive CopyNextFrameToManager_-
            // SetMoveState4: bit 8 of +0x398 = REWIND, bit 9 = FORWARD.
            // Logging these is the cheapest way to find out if the
            // engine is signalling forward-play through this channel,
            // which we don't yet understand in replay-viewing mode.
            void* pra = nullptr;
            SafeReadPtr(bm + 0x440, &pra);
            if (pra)
            {
                uint8_t* praB = reinterpret_cast<uint8_t*>(pra);
                uint32_t p0_394 = 0, p0_398 = 0, p1_394 = 0, p1_398 = 0;
                SafeReadUInt32(praB + 0x394,         &p0_394);
                SafeReadUInt32(praB + 0x398,         &p0_398);
                SafeReadUInt32(praB + 0xA8 + 0x394,  &p1_394);
                SafeReadUInt32(praB + 0xA8 + 0x398,  &p1_398);
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.dump] {} PRA P0[+394=0x{:X} +398=0x{:X}] "
                        "P1[+394=0x{:X} +398=0x{:X}]  (bit8=rewind,bit9=forward)\n"),
                    RC::to_generic_string(label),
                    p0_394, p0_398, p1_394, p1_398);
            }

            // Sample the input cache around the engine's current
            // master clock to verify entries are present (forward
            // playback) or absent (post-seek before our restore
            // landed).  Compare the entry's FrameIndex field with the
            // requested index - they should match for the engine's
            // lookup to succeed.
            if (il_ptr)
            {
                uint8_t* il = reinterpret_cast<uint8_t*>(il_ptr);
                const int32_t probe_center =
                    (il_nMasterClock > 0) ? il_nMasterClock : hint_frame;
                // 3 sample points: master-1, master, master+1
                const int32_t probes[3] = {probe_center - 1,
                                           probe_center,
                                           probe_center + 1};
                for (int pi = 0; pi < 2; ++pi)
                {
                    int32_t  cache_fid[3]   = {-1, -1, -1};
                    uint32_t cache_fidx[3]  = {0, 0, 0};
                    uint32_t cache_input[3] = {0, 0, 0};
                    for (int k = 0; k < 3; ++k)
                    {
                        const int32_t frame_idx = probes[k];
                        if (frame_idx < 0) continue;
                        const uintptr_t bucket =
                            kIL_InputCacheStart_Off
                          + static_cast<size_t>(pi) * 0x2000
                          + (static_cast<size_t>(frame_idx) & 0x1FF) * 0x10;
                        SafeReadInt32 (il + bucket + 0, &cache_fid[k]);
                        SafeReadUInt32(il + bucket + 4, &cache_fidx[k]);
                        SafeReadUInt32(il + bucket + 8, &cache_input[k]);
                    }
                    RC::Output::send<RC::LogLevel::Default>(
                        STR("[ReplayScrub.dump] {} P{} cache@M-1:[fid={} fidx={} in=0x{:X}] "
                            "@M:[fid={} fidx={} in=0x{:X}] @M+1:[fid={} fidx={} in=0x{:X}]\n"),
                        RC::to_generic_string(label), pi + 1,
                        cache_fid[0], cache_fidx[0], cache_input[0],
                        cache_fid[1], cache_fidx[1], cache_input[1],
                        cache_fid[2], cache_fidx[2], cache_input[2]);
                }
            }

            // Per-chara fields.
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return;
            const uintptr_t slot_addrs[2] = {
                base + kRVA_CharaSlotP1, base + kRVA_CharaSlotP2,
            };
            for (int pi = 0; pi < 2; ++pi)
            {
                void* chara_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(slot_addrs[pi]),
                                 &chara_raw) || !chara_raw)
                    continue;
                uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);

                int32_t  c_replayCursor    = 0;
                int32_t  c_replayLastFID   = 0;
                int32_t  c_replayMaster    = 0;
                int32_t  c_inputRingCur    = 0;
                int32_t  c_replayFrameCnt  = 0;
                int32_t  c_lookupKey       = 0;
                int32_t  c_enableFlag      = 0;
                int32_t  c_frameOffset     = 0;
                int32_t  c_frameTotal      = 0;
                int32_t  c_frameTarget     = 0;
                int32_t  c_consumerCursor  = 0;
                uint8_t  c_charaMode       = 0;
                SafeReadInt32(c + kChara_nReplayCursor_Off,        &c_replayCursor);
                SafeReadInt32(c + kChara_nReplayLastFrameID_Off,   &c_replayLastFID);
                SafeReadInt32(c + kChara_nReplayMasterClock_Off,   &c_replayMaster);
                SafeReadInt32(c + kChara_nInputCursorRing_Off,     &c_inputRingCur);
                SafeReadInt32(c + kChara_nReplayFrameCount_Off,    &c_replayFrameCnt);
                SafeReadInt32(c + kChara_nReplayLookupKey_Off,     &c_lookupKey);
                SafeReadInt32(c + kChara_nReplayEnableFlag_Off,    &c_enableFlag);
                SafeReadInt32(c + kChara_nReplayFrameOffset_Off,   &c_frameOffset);
                SafeReadInt32(c + kChara_nReplayFrameTotal_Off,    &c_frameTotal);
                SafeReadInt32(c + kChara_nReplayFrameTarget_Off,   &c_frameTarget);
                SafeReadInt32(c + kChara_nReplayConsumerCursor_Off,&c_consumerCursor);
                SafeReadUInt8(c + kChara_bCharaMode_Off,           &c_charaMode);

                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub.dump] {} P{} cursor={} lastFID={} master={} "
                        "ringCur={} frameCnt={} lookupKey={} enable={} "
                        "frameOff={} frameTotal={} frameTarget={} "
                        "consumer={} mode={}\n"),
                    RC::to_generic_string(label), pi + 1,
                    c_replayCursor, c_replayLastFID, c_replayMaster,
                    c_inputRingCur, c_replayFrameCnt, c_lookupKey,
                    c_enableFlag, c_frameOffset, c_frameTotal,
                    c_frameTarget, c_consumerCursor,
                    static_cast<int>(c_charaMode));
            }
        }

    private:

        // Sync the BM-side cursors (BM+0x148C nReplayLastApplied AND
        // BM+0x1488 nReplayLastFrameID).
        //
        // The InputLog-side cursors (dwPlaybackCursor, nLastFrameID,
        // nMasterClock) are NO LONGER written here - they live inside
        // the +0x394..+0x4414 InputLog-state capture window and are
        // already restored by restore_input_cache() to their captured
        // (snapshot-time) values, which by construction equal
        // master_clock for the master-clock field.  Writing them again
        // would either be redundant (same value) or silently override
        // the captured value with a slightly-off one if a race
        // occurred between master-clock read and IL-state read.
        //
        // The BM struct is NOT in the IL capture window so we still
        // need to write its cached copies here.  Two writes:
        //
        //   1. BM+0x148C nReplayLastApplied = master_clock
        //      Makes SimulationLoop see delta=0 on the next tick: the
        //      Actor::Tick chain INCs IL->nMasterClock to M+1, delta =
        //      (M+1) - M = 1, engine reads input for frame M+1 and
        //      applies it to the state-at-M restored by
        //      ExecFinalizeAndPost.
        //
        //   2. BM+0x1488 nReplayLastFrameID = (captured IL->nLastFrameID)
        //      SimulationLoop's catch-up prelude does:
        //        if (BM+0x1488 != IL+0x3A0) BM+0x148C = 0  // RESET
        //      then BM+0x1488 = IL+0x3A0.  After our restore IL+0x3A0
        //      is the snapshot value but BM+0x1488 still holds the
        //      LIVE-edge value, so the mismatch fires and our
        //      master_clock write to BM+0x148C is silently zeroed.
        //      The next iteration then computes delta = M - 0 = M and
        //      runs M iterations of ProcessRoundStateSequence + frame-
        //      advance-counter increments in a single tick - a fast-
        //      forward burst that drifts the round state machine and
        //      timer counter forward.  Syncing BM+0x1488 here closes
        //      this hole.
        //
        // See the GetCachedRoundValue_ByIndex @ 0x1403F0720 plate +
        // LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState
        // @ 0x1403FE520 plate for the full contract.
        void write_replay_cursors(int32_t master_clock,
                                  int32_t last_frame_id) noexcept
        {
            // Resolve BM via UObjectGlobals (cached in m_bm_ptr after
            // first call).  Returns null if the battle manager isn't
            // alive yet (between matches).
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);

            // BM-side writes.  Plain stores - the BM struct lives in
            // ordinary heap memory; no SEH wrapping needed.
            *reinterpret_cast<int32_t*>(bm + kBM_nReplayLastApplied_Off) =
                master_clock;
            *reinterpret_cast<int32_t*>(bm + kBM_nReplayLastFrameID_Off) =
                last_frame_id;

            // First-fire log so the user can confirm the cursor sync
            // path is alive.
            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub] first cursor sync; master_clock={} "
                        "last_frame_id={} BM=0x{:X} BM->nReplayLastApplied "
                        "+ BM->nReplayLastFrameID written\n"),
                    master_clock, last_frame_id,
                    reinterpret_cast<uintptr_t>(bm));
            }
        }
    };

}  // namespace Horse
