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
// snapshot store would just be a waste of memory + per-frame CPU.
//
// We initialise the dedup store lazily on first entry into Replay
// presence and keep it for the rest of the session (presence transitions
// only RESET its contents; we don't tear it down until module shutdown).
// Capture is unbounded - it spans the whole replay - and self-stops at a
// 2 GB resident-memory ceiling; there is no user-set capture window.
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

#include "BytePatch.hpp"   // vtable-slot patcher for RenderSkipOverride
#include "CodeCave.hpp"    // direct PerFrameTick bypass trampoline
#include "GameMode.hpp"
#include "HorseLib.hpp"   // GlobalPtr for LuxBattleManager lookup
#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "ReplayScrubDiag.hpp"
#include "SafeMemoryRead.hpp"

#ifndef HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG
#define HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG 0
#endif

#ifndef HORSEMOD_REPLAY_ENABLE_INTERACTIVE_RESET_DIAG
#define HORSEMOD_REPLAY_ENABLE_INTERACTIVE_RESET_DIAG 0
#endif

#include <DynamicOutput/DynamicOutput.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
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

    private:
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
        bool  m_engaged = false;
        float m_saved   = 100.0f;
    };

    // ------------------------------------------------------------------
    // RenderSkipOverride - skips SC6's per-frame scene render during the
    // EXPERIMENTAL "Generate timeline" pass, so the replay fast-forward
    // is sim-bound instead of render-bound.
    //
    // Why this is the strongest fast-forward lever
    // --------------------------------------------
    // UGameEngine::Tick @ 0x141e38f70 first runs the world tick
    // (UWorld::Tick - the frame-counted replay simulation) and then, in
    // a SEPARATE block gated by a different condition, the scene redraw:
    //     GEngine->vtable[0x410](GEngine, bShouldPresent)   // RedrawViewports
    // Because the two are gated independently, the redraw can be
    // suppressed while the simulation keeps advancing once per UE4 tick.
    // SC6 replay viewing is render-bound well below 60fps, so removing
    // the redraw (not merely shrinking it, as ScreenPercentageOverride
    // does) is what lets the uncapped loop become sim-bound and actually
    // fast-forward.  This is the in-process equivalent of "simulate
    // headless": the whole engine still runs, but the dominant per-frame
    // cost - building and submitting the scene - is cut.
    //
    // Why this is simulation-SAFE
    // ---------------------------
    // RedrawViewports is pure OUTPUT: it draws already-evaluated state.
    // Animation/pose evaluation (which the hit detector samples bone
    // positions from) is a COMPONENT tick inside UWorld::Tick, NOT part
    // of RedrawViewports - so skipping the redraw does not skip anim
    // eval and cannot desync the deterministic replay.  The HgCpuDirect
    // snapshot (tick_capture) is a memory copy with no render dependency
    // either.  Only the on-screen picture is affected.
    //
    // Mechanism
    // ---------
    // engage() resolves GEngine's *runtime* vtable (so a UGameEngine
    // subclass vtable is handled correctly), verifies slot 0x410 still
    // points at the known RedrawViewports (imageBase + kRVA_RedrawViewports)
    // and, only then, patches that slot to vt_redraw_thunk via BytePatch.
    // disengage() restores the original pointer.  The thunk skips the
    // redraw for most frames but forwards to the real RedrawViewports 1
    // frame in kKeepAliveEveryN, so the OS does not flag the window
    // unresponsive and HorseMod's own ImGui overlay (drawn in the DXGI
    // Present hook) still refreshes often enough for "Stop" to stay
    // clickable.
    //
    // RedrawViewports @ 0x141e348f0 (verified decompile): reads
    // GEngine+0x618 (GameViewport), calls GameViewport->vtable[0x2c8],
    // then FViewport::Draw(GameViewport+0xa0, bShouldPresent).  It does
    // NOT call back through vtable[0x410], so the thunk forwarding to the
    // original cannot recurse.
    //
    // Threading: engage()/disengage() and the thunk all run on the game
    // thread - UGameEngine::Tick calls the redraw, and start/stop_-
    // generate_timeline run in the cockpit pre-tick, which is itself
    // inside that same UGameEngine::Tick (before the redraw block).
    // Single-threaded - the non-atomic m_engaged relies on that, matching
    // FrameCapOverride.
    // ------------------------------------------------------------------
    class RenderSkipOverride
    {
    public:
        // Non-copyable AND non-movable - and a de-facto SINGLETON.  The
        // redraw thunk (vt_redraw_thunk) has to be a plain static
        // function to live in a vtable slot, so it can reach the saved
        // original pointer + keep-alive counter ONLY through the static
        // members at the bottom of this class.  A second instance would
        // share that process-global state and corrupt the first.  The
        // one and only owner is ReplayScrub::m_render_skip.  (The sibling
        // FrameCapOverride / ScreenPercentageOverride are plain-value and
        // copyable; this class deliberately is not - it owns a BytePatch
        // and patches process-global engine state.)
        RenderSkipOverride()                                     = default;
        RenderSkipOverride(const RenderSkipOverride&)            = delete;
        RenderSkipOverride& operator=(const RenderSkipOverride&) = delete;

        static constexpr uintptr_t kRVA_GEngine         = 0x43B3068;
        static constexpr uintptr_t kVtOff_Redraw        = 0x410;
        // UGameEngine::RedrawViewports - the function GEngine vtable[0x410]
        // must still point at for engage() to patch.  Verified via the
        // UGameEngine vtable in .rdata (slot 0x268 = UGameEngine::Tick @
        // 0x141e38f70; slot 0x410 = 0x141e348f0).
        static constexpr uintptr_t kRVA_RedrawViewports = 0x1E348F0;
        // Render 1 frame in N during a skip pass: keeps the window
        // responsive and gives coarse visual progress.  N=16 costs ~6%
        // of the render it would otherwise save - negligible against the
        // multi-x speedup, and worth it for a clickable "Stop".
        static constexpr uint32_t  kKeepAliveEveryN     = 16;

        bool is_engaged() const noexcept { return m_engaged; }

        // Resolve + verify + patch GEngine's redraw vtable slot.  Returns
        // false (changing nothing) if GEngine / its vtable / the slot
        // can't be resolved, or the slot does not hold the expected
        // RedrawViewports - a guard against a wrong offset or an SC6
        // update silently corrupting the vtable.  Idempotent.
        bool engage() noexcept
        {
            if (m_engaged) return true;

            void** slot = resolve_redraw_slot();
            if (!slot)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.RenderSkip] engage failed - GEngine / "
                    "vtable unresolvable\n"));
                return false;
            }

            void* current = nullptr;
            if (!SafeReadPtr(slot, &current) || !current)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.RenderSkip] engage failed - redraw slot "
                    "unreadable\n"));
                return false;
            }

            const uintptr_t base = NativeBinding::imageBase();
            const uintptr_t want = base + kRVA_RedrawViewports;
            if (reinterpret_cast<uintptr_t>(current) != want)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.RenderSkip] engage failed - GEngine "
                    "vtable[0x410]=0x{:x} != expected RedrawViewports "
                    "0x{:x}; refusing to patch (SC6 build changed?)\n"),
                    reinterpret_cast<uintptr_t>(current), want);
                return false;
            }

            s_original.store(current, std::memory_order_release);
            s_call_count.store(0, std::memory_order_relaxed);

            // Patch the 8-byte slot to point at vt_redraw_thunk.  BytePatch
            // snapshots the original pointer (verified above to be the real
            // RedrawViewports) so disengage() puts it back exactly.
            const uintptr_t thunk =
                reinterpret_cast<uintptr_t>(&vt_redraw_thunk);
            uint8_t bytes[sizeof(uintptr_t)];
            std::memcpy(bytes, &thunk, sizeof(bytes));
            if (!m_patch.prepare(slot, bytes, sizeof(bytes))
                || !m_patch.enable())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.RenderSkip] engage failed - vtable slot "
                    "patch did not apply\n"));
                m_patch.disable();
                return false;
            }

            m_engaged = true;
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.RenderSkip] engaged - GEngine vtable[0x410] "
                "redraw -> skip thunk (render 1 frame in {})\n"),
                kKeepAliveEveryN);
            return true;
        }

        // Restore the original redraw pointer.  Idempotent and safe to
        // call when not engaged (no-op in that case).
        void disengage() noexcept
        {
            if (!m_engaged) return;
            m_engaged = false;   // clear first so a re-entrant call no-ops
            // If BytePatch::disable() fails (a VirtualProtect error -
            // near-impossible on a committed .rdata page, but reachable
            // during teardown) the vtable slot stays pointing at
            // vt_redraw_thunk and the game would render only 1 frame in
            // kKeepAliveEveryN until restart.  Report that loudly rather
            // than logging a false "restored".
            if (m_patch.disable())
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.RenderSkip] disengaged - redraw "
                    "restored\n"));
            else
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[ReplayScrub.RenderSkip] disengage FAILED - redraw "
                    "vtable slot NOT restored; rendering may stay "
                    "throttled until the game is restarted\n"));
        }

    private:
        using RedrawFn = void(__fastcall*)(void* self, uint8_t present);

        // Installed in GEngine's redraw vtable slot while a skip pass is
        // running.  Skips the scene redraw, except 1 frame in
        // kKeepAliveEveryN where it forwards to the real RedrawViewports.
        //
        // fetch_add returns the PRE-increment count, and engage() resets
        // it to 0, so the FIRST call after engage has n==0 and DOES draw
        // (frames 0, kN, 2*kN, ... render).  That first-frame draw is
        // intentional - it puts one frame on screen immediately so
        // engaging does not look like a hang.  Do NOT "fix" this into a
        // pre-increment: that would skip the first kN-1 frames and the
        // screen would visibly freeze the instant generation starts.
        static void __fastcall vt_redraw_thunk(void* self,
                                               uint8_t present) noexcept
        {
            const uint32_t n =
                s_call_count.fetch_add(1, std::memory_order_relaxed);
            if ((n % kKeepAliveEveryN) == 0)
            {
                void* orig = s_original.load(std::memory_order_acquire);
                if (orig)
                    reinterpret_cast<RedrawFn>(orig)(self, present);
            }
            // else: scene redraw skipped for this frame
        }

        // GEngine -> runtime vtable -> &vtable[0x410].  Reads the live
        // vtable pointer off the GEngine instance, so a UGameEngine
        // subclass with its own vtable is patched correctly.
        static void** resolve_redraw_slot() noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return nullptr;
            void* engine = nullptr;
            if (!SafeReadPtr(
                    reinterpret_cast<const void*>(base + kRVA_GEngine),
                    &engine) || !engine)
                return nullptr;
            void* vtable = nullptr;
            if (!SafeReadPtr(engine, &vtable) || !vtable)
                return nullptr;
            return reinterpret_cast<void**>(
                reinterpret_cast<uint8_t*>(vtable) + kVtOff_Redraw);
        }

        bool      m_engaged = false;
        BytePatch m_patch;
        // Static, not per-instance: vt_redraw_thunk is a plain function
        // (vtable-slot ABI) and can only reach these as statics - which
        // is what makes the class a singleton (see the note at the top).
        // Written/read on the game thread only (engage/disengage and the
        // thunk all run there); atomic purely as cheap insurance.
        static inline std::atomic<void*>    s_original  {nullptr};
        static inline std::atomic<uint32_t> s_call_count{0};
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
                // Bounds-check the chunk id against the pool.  A valid
                // store never holds an out-of-range id, but a mid-reset
                // alloc failure (drop_ring) could in principle leave a
                // stale id referencing a cleared pool - reject it rather
                // than read out of bounds and hand back a garbage region.
                const uint32_t id = m_ids[base + c];
                if (id >= m_pool->unique_chunks()) return false;
                const size_t n =
                    (m_region_len - off < ChunkPool::kChunkBytes)
                        ? (m_region_len - off) : ChunkPool::kChunkBytes;
                std::memcpy(dst + off, m_pool->chunk_ptr(id), n);
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
        // Keep this to the first tick only.  The old 128-tick window
        // gathered and memcmp'd every region on every early capture,
        // exactly when Generate/Capture needs maximum headroom.
        static constexpr size_t kSelfTestTicks = 1;

        ChunkPool*            m_pool            {nullptr};
        size_t                m_region_len      {0};
        size_t                m_chunks_per_tick {0};
        size_t                m_padded_len      {0};
        std::vector<uint32_t> m_ids;       // [tick*chunks_per_tick + chunk]
        std::vector<uint8_t>  m_scratch;   // padded staging / gather buffer
    };

    // ========================================================================
    // Horse::TagTimeline - append-only per-tick metadata, safe for the UI
    // reader.
    //
    // Each captured tick carries four i32 tags: capture sequence (the
    // canonical timeline coordinate), replay round, within-round wall frame,
    // and replay master clock.  The capture path (game thread) appends one
    // entry per committed snapshot; the UI thread reads committed entries to
    // draw the timeline bar.
    //
    // Storage is a fixed array of fixed-size heap blocks, not a std::vector:
    // a vector would reallocate its backing store as it grows, moving entries
    // out from under a concurrent UI reader.  With blocks, a committed
    // entry's address never moves, and the single published count is the
    // only thing a reader has to synchronise on.  Blocks are never freed
    // before destruction (a reader gated on a stale count could still be
    // mid-access) - clear() only resets the count.  Worst case after a
    // pathological 2 GB session is ~32 MB of retained blocks; a normal match
    // is 3-4 blocks (~0.5 MB).
    //
    // kMaxBlocks x kBlockTicks caps the tick count, but ReplayScrub's 2 GB
    // store ceiling is always reached first - this is a storage backstop,
    // not a user-visible limit.
    // ========================================================================
    class TagTimeline
    {
    public:
        static constexpr size_t kBlockTicks = 8192;
        static constexpr size_t kMaxBlocks  = 256;     // ~2.1 M ticks backstop

        // [game thread] Append one tick's tags.  The four values are written
        // before the count is published with release ordering, so a UI
        // reader that observes the bumped count (acquire) is guaranteed to
        // see all four.
        void append(int32_t seq, int32_t round,
                    int32_t frame, int32_t master,
                    int32_t demo_time_ms) noexcept
        {
            const size_t n = m_count.load(std::memory_order_relaxed);
            const size_t b = n / kBlockTicks;
            if (b >= kMaxBlocks) return;          // backstop; unreachable
            Block* blk = m_blocks[b].load(std::memory_order_relaxed);
            if (!blk)
            {
                blk = new (std::nothrow) Block;
                if (!blk) return;                 // drop the tick on OOM
                m_blocks[b].store(blk, std::memory_order_release);
            }
            Entry& e = blk->e[n % kBlockTicks];
            e.seq   .store(seq,    std::memory_order_relaxed);
            e.round .store(round,  std::memory_order_relaxed);
            e.frame .store(frame,  std::memory_order_relaxed);
            e.master.store(master, std::memory_order_relaxed);
            e.demo_time_ms.store(demo_time_ms,
                                 std::memory_order_relaxed);
            m_count.store(n + 1, std::memory_order_release);   // publish
        }

        size_t count() const noexcept
        {
            return m_count.load(std::memory_order_acquire);
        }

        // Read tick `t`'s tags.  Returns false if `t` is not (yet) a
        // committed tick.  Safe to call from the UI thread concurrently with
        // append().
        bool get(size_t t, int32_t& seq, int32_t& round,
                 int32_t& frame, int32_t& master) const noexcept
        {
            if (t >= count()) return false;
            Block* blk =
                m_blocks[t / kBlockTicks].load(std::memory_order_acquire);
            if (!blk) return false;
            const Entry& e = blk->e[t % kBlockTicks];
            seq    = e.seq   .load(std::memory_order_relaxed);
            round  = e.round .load(std::memory_order_relaxed);
            frame  = e.frame .load(std::memory_order_relaxed);
            master = e.master.load(std::memory_order_relaxed);
            return true;
        }

        // Read the absolute UE4 demo time captured for tick `t`.
        // Stored as milliseconds so readers do not need atomic<float>.
        // Returns a negative value when the driver was unavailable at
        // capture time.
        int32_t demo_time_ms(size_t t) const noexcept
        {
            if (t >= count()) return -1;
            Block* blk =
                m_blocks[t / kBlockTicks].load(std::memory_order_acquire);
            if (!blk) return -1;
            return blk->e[t % kBlockTicks].demo_time_ms.load(
                std::memory_order_relaxed);
        }

        bool demo_time_stats(int32_t& min_ms, int32_t& max_ms,
                             size_t& valid_count) const noexcept
        {
            const size_t n = count();
            min_ms = 0x7fffffff;
            max_ms = -1;
            valid_count = 0;
            for (size_t i = 0; i < n; ++i)
            {
                Block* blk =
                    m_blocks[i / kBlockTicks].load(
                        std::memory_order_acquire);
                if (!blk) continue;
                const int32_t ms =
                    blk->e[i % kBlockTicks].demo_time_ms.load(
                        std::memory_order_relaxed);
                if (ms < 0) continue;
                if (ms < min_ms) min_ms = ms;
                if (ms > max_ms) max_ms = ms;
                ++valid_count;
            }
            if (valid_count == 0)
            {
                min_ms = -1;
                max_ms = -1;
                return false;
            }
            return true;
        }

        // [game thread] Logical clear: the published count drops to 0, so
        // every entry becomes unreachable.  The block storage is kept and
        // overwritten by later appends (see the class plate).
        void clear() noexcept { m_count.store(0, std::memory_order_release); }

        // Resident bytes of the allocated blocks, for the store ceiling.
        // O(1): a block is allocated exactly when its first tick is
        // appended, so block count == ceil(count / kBlockTicks).
        size_t bytes() const noexcept
        {
            const size_t n = m_count.load(std::memory_order_acquire);
            return ((n + kBlockTicks - 1) / kBlockTicks) * sizeof(Block);
        }

        ~TagTimeline()
        {
            for (auto& b : m_blocks)
                delete b.load(std::memory_order_relaxed);
        }

    private:
        struct Entry
        {
            std::atomic<int32_t> seq, round, frame, master, demo_time_ms;
        };
        struct Block { Entry e[kBlockTicks]; };

        std::array<std::atomic<Block*>, kMaxBlocks> m_blocks {};
        std::atomic<size_t>                         m_count  {0};
    };

    // ------------------------------------------------------------------
    // SEH-guarded invoke of an engine snapshot Exec* function
    // (ExecMoveChangeAndPost / ExecFinalizeAndPost), called through the
    // buffer shim.  The restore direction writes deep into live engine
    // chara / global state; if the captured snapshot and the live engine
    // context have diverged - e.g. a seek issued just as the battle
    // actors are being torn down - that write can fault.  Catching it
    // here turns a hard process crash into a logged no-op.  Kept as a
    // tiny standalone function because MSVC forbids __try/__except in a
    // body that also needs C++ object unwinding (see SafeMemoryRead.hpp).
    static inline bool SafeInvokeExec(
        void* (__fastcall* fn)(HgCpuBufferShim*),
        HgCpuBufferShim* shim) noexcept
    {
        if (!fn || !shim) return false;
        __try
        {
            fn(shim);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static inline bool SafeInvokePerFrameTick(
        void (__fastcall* fn)(uintptr_t*),
        uintptr_t* args) noexcept
    {
        if (!fn || !args) return false;
        __try
        {
            fn(args);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static inline bool SafeInvokeNativeVoidPtr(
        void (__fastcall* fn)(void*),
        void* self) noexcept
    {
        if (!fn || !self) return false;
        __try
        {
            fn(self);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static inline bool SafeInvokeNativePtrIntReturnsPtr(
        void* (__fastcall* fn)(void*, int),
        void* self,
        int arg,
        void** out) noexcept
    {
        if (out) *out = nullptr;
        if (!fn || !self || !out) return false;
        __try
        {
            *out = fn(self, arg);
            return *out != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *out = nullptr;
            return false;
        }
    }

    // ------------------------------------------------------------------
    // ReplayScrub - the deduplicated snapshot store + UI driver.
    //
    // Captures every replay tick into a content-addressed dedup store
    // (ChunkPool + four RegionStores) keyed by an append-only tick index,
    // plus a single buffer-shim object re-targeted onto a staging /
    // gather buffer when capturing or restoring.  Capture is unbounded -
    // it covers the whole replay - up to a 2 GB resident-memory ceiling.
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

        enum class BattleStepMode : int
        {
            None = 0,
            RenderSkip = 1,
            DirectPerFrame = 2,
        };

        enum class ScrubMode : int
        {
            Idle = 0,
            Generated,
            Dragging,
            PausedPreview,
            NativeSeekQueued,
            NativeSeekSubmitted,
            NativeSeekSettling,
            NativeSeekLanded,
            NativeSeekFailed,
            Playing,
        };

        enum class NativeSeekStatus : int
        {
            Idle = 0,
            Queued,
            DeferredBusy,
            Submitted,
            Settling,
            ClockLanded,
            Landed,
            Failed,
        };

        enum class NativeSeekFailure : int
        {
            None = 0,
            FunctionUnresolved,
            DriverUnresolved,
            DriverBusy,
            CallFaulted,
            TaskNotObserved,
            SettleTimedOut,
            InvalidTarget,
            TimelineMissingDemoTime,
            NativeTimeSourceUnresolved,
            LegacyVerifyFailed,
            InteractiveReplayContextUnresolved,
            InteractiveReplayResetFaulted,
            InteractiveReplayRoundSelectFailed,
            InteractiveReplayFastForwardStalled,
            InteractiveReplayVerifyFailed,
            InteractiveReplayTargetPastMatchEnd,
            RoundResetDataUnavailable,
            Sc6ResetSnapshotReadFailed,
            Sc6ResetSnapshotWriteFailed,
            Sc6InputLogRestoreFailed,
            Sc6ReplayDataBlockRestoreFailed,
            Sc6ReplayCursorWriteFailed,
            Sc6ReplayPlayerCursorWriteFailed,
            Sc6SetMoveStateFaulted,
            Sc6InteractiveReplayResetFaultedDiagnostic,
            Sc6ResetDispatchFailed,
            CapturedSnapshotRestoreFailed,
            CapturedSnapshotCompareFailed,
            CapturedSnapshotValidationStepFailed,
            CapturedSnapshotSemanticRepairFailed,
            CapturedRestoreProbeMismatch,
            CapturedGameplayStepFailed,
            SemanticMismatch,
            CrossRoundResetContextUnavailable,
            CrossRoundResetDispatchFailed,
            OracleFieldOffsetUnproven,
            TimelineIncomplete,
            NotLanded,
            BattleManagerStatusNotActive,
        };

        enum class SeekApplyStatus : int
        {
            Failed = 0,
            Pending,
            Submitted,
            AppliedSc6Exact,
            AppliedLegacy,
        };

        enum class PreviewStatus : int
        {
            Idle = 0,
            Requested,
            Applied,
            Failed,
            SkippedUnsafe,
        };

        enum class ReplayScrubHoldKind : int
        {
            None = 0,
            UiParkOnly,
            RestoredFrameHold,
            ValidationStep,
            ManualDiagnosticFreeze,
        };

        struct ReplayScrubGatePolicy
        {
            bool world_tick_gate {false};
            bool replay_clock_gate {false};
            bool actor_tick_gate {false};
            bool time_dilation_gate {false};
            bool vm_freeze_byte {false};
            const char* reason {"None"};
        };

        enum class ReplayScrubGateReason : int
        {
            None = 0,
            UiParkOnly,
            RestoredFrameHold,
            ValidationStep,
        };

        struct ReplayTimelineView
        {
            int32_t earliest_seq {-1};
            int32_t latest_seq {-1};
            int32_t displayed_seq {-1};
            int32_t requested_seq {-1};
            int32_t landed_seq {-1};

            ScrubMode mode {ScrubMode::Idle};
            NativeSeekStatus native_status {NativeSeekStatus::Idle};
            NativeSeekFailure block_reason {NativeSeekFailure::None};

            bool paused {false};
            bool can_play {false};
            bool native_pending {false};
            bool preview_applied {false};
        };

        enum class SeekCommandKind : int
        {
            None = 0,
            RequestPreviewAndNativeSeek,
            StepToSeq,
            PauseAtLive,
            PlayFromSelected,
            Cancel,
        };

        struct UiPlayheadState
        {
            int32_t requested_seq {-1};
            int32_t displayed_seq {-1};
            bool dragging {false};
            bool wants_play {false};
        };

        struct PreviewState
        {
            int32_t seq {-1};
            int32_t round {-1};
            PreviewStatus status {PreviewStatus::Idle};
            NativeSeekFailure failure_reason {NativeSeekFailure::None};
        };

        struct NativeSeekState
        {
            int32_t requested_seq {-1};
            int32_t adjusted_seq {-1};
            int32_t target_ms {-1};
            int32_t round {-1};
            int32_t master {-1};

            NativeSeekStatus status {NativeSeekStatus::Idle};
            NativeSeekFailure failure {NativeSeekFailure::None};

            uintptr_t driver_ptr {0};
            int32_t settle_ticks_left {0};
            bool direct_driver_available {false};
            bool cvar_submitted {false};
            uint32_t generation {0};
        };

        enum class Sc6ContextFailure : int
        {
            None = 0,
            ImageBaseMissing,
            BattleManagerMissing,
            InputLogMissing,
            ReplayPlayerMissing,
            StateResetDataMissing,
            InteractiveReplayUnreadable,
            RoundResetSnapshotMissing,
        };

        enum class Sc6ContextSource : int
        {
            None = 0,
            ObjectRegistryBattleManager,
            WorldModePumpBattleManager,
        };

        struct Sc6ReplaySeekContext
        {
            Sc6ContextSource source {Sc6ContextSource::None};
            uintptr_t world_mode_pump {0};
            uintptr_t battle_manager {0};
            uintptr_t world_mode_pump_battle_manager {0};
            uintptr_t object_registry_battle_manager {0};
            uintptr_t sub_driver {0};
            uintptr_t input_log {0};
            uintptr_t replay_player {0};
            uintptr_t state_reset_data {0};
            uintptr_t interactive_replay {0};
            int32_t total_rounds {-1};
            int32_t current_round {-1};
            int32_t input_master {-1};
            int32_t battle_master {-1};
            Sc6ContextFailure failure {Sc6ContextFailure::None};
            bool battle_manager_ok {false};
            bool world_mode_pump_bm_ok {false};
            bool object_registry_bm_ok {false};
            bool input_log_ok {false};
            bool replay_player_ok {false};
            bool state_reset_data_ok {false};
            bool interactive_replay_ok {false};
            bool captured_round_reset_ok {false};
            bool readable {false};
        };

        struct GenerateStartReadiness
        {
            bool in_replay {false};
            bool context_ok {false};
            bool seek_context_ok {false};
            bool reset_source_ok {false};
            bool live_bm_reset_ok {false};
            bool state_reset_data_ok {false};
            bool clean_start {false};
            int32_t round {-1};
            int32_t input_master {-1};
            int32_t battle_master {-1};
            Sc6ContextSource seek_context_source {
                Sc6ContextSource::None};
            uintptr_t battle_manager {0};
            uintptr_t world_mode_pump_battle_manager {0};
            uintptr_t object_registry_battle_manager {0};
            uintptr_t input_log {0};
            uintptr_t world_mode_pump_input_log {0};
            uintptr_t object_registry_input_log {0};
            uintptr_t sub_driver {0};
            uintptr_t replay_player {0};
            uintptr_t state_reset_data {0};
            uintptr_t interactive_replay {0};
            Sc6ContextFailure seek_context_failure {
                Sc6ContextFailure::None};
            const char* reason {"unread"};
        };

        enum class Sc6SeekAuthority : int
        {
            CapturedSnapshotValidated = 0,
            NativeRoundReplayDiagnostic,
        };

        enum class ReplayInputAuthority : int
        {
            Unknown = 0,
            OfflineCharaReplayRing,
            InputLogSimulationCache,
        };

        enum class CapturedSeekValidationMode : int
        {
            None = 0,
            PreviousToTarget,
            TargetToNext,
            StaticTarget,
        };

        enum class Sc6ExactSeekPhase : int
        {
            Idle = 0,
            Queued,
            RestoreValidationOrigin,
            ValidateStepToTarget,
            CompareTargetSnapshot,
            RestoreTargetAfterValidation,
            ResetRound,
            FastForward,
            Verify,
            ClockLandedPlayBlocked,
            Landed,
            Failed,
            Cancelled,
        };

        struct Sc6ExactSeekJob
        {
            uint32_t generation {0};
            Sc6ExactSeekPhase phase {Sc6ExactSeekPhase::Idle};
            Sc6SeekAuthority authority {
                Sc6SeekAuthority::CapturedSnapshotValidated};
            int32_t requested_seq {-1};
            int32_t target_tick {-1};
            int32_t target_seq {-1};
            int32_t target_round {-1};
            int32_t target_master {-1};
            int32_t validation_origin_tick {-1};
            int32_t validation_origin_seq {-1};
            int32_t validation_origin_round {-1};
            int32_t validation_origin_master {-1};
            int32_t validation_compare_tick {-1};
            int32_t validation_compare_seq {-1};
            int32_t validation_compare_round {-1};
            int32_t validation_compare_master {-1};
            int32_t round_start_tick {-1};
            int32_t round_start_seq {-1};
            int32_t round_start_master {-1};
            int32_t frames_advanced {0};
            int32_t slices_serviced {0};
            int32_t stall_count {0};
            int32_t last_live_master {-1};
            int32_t native_step_requested_master {-1};
            int32_t native_step_last_observed_master {-1};
            int32_t native_step_requested_credits {0};
            int32_t native_step_granted_credits {0};
            int32_t native_step_observed_credits {0};
            int32_t native_step_stall_count {0};
            int32_t native_step_wait_services {0};
            NativeSeekFailure failure {NativeSeekFailure::None};
            NativeSeekFailure restore_target_block_failure {
                NativeSeekFailure::None};
            const char* label {"USER"};
            CapturedSeekValidationMode validation_mode {
                CapturedSeekValidationMode::None};
            bool service_logged {false};
            bool native_step_waiting {false};
            bool snapshot_validation_ok {false};
            bool needs_cross_round_reset {false};
            bool cross_round_reset_applied {false};
        };

        struct CapturedFrameRestoreReport
        {
            int32_t seq {-1};
            int32_t tick {-1};
            int32_t round {-1};
            int32_t master {-1};
            bool sim_restore_ok {false};
            bool input_log_restore_ok {false};
            bool rdb_restore_ok {false};
            bool extras_restore_ok {false};
            bool cursor_write_ok {false};
            bool replay_player_cursor_write_ok {false};
            NativeSeekFailure failure {NativeSeekFailure::None};
        };

        struct LiveCapturedFrameScratch
        {
            std::vector<uint8_t> sim;
            std::vector<uint8_t> input_log;
            std::vector<uint8_t> rdb;
            std::vector<uint8_t> extras;
            bool sim_ok {false};
            bool input_log_ok {false};
            bool rdb_ok {false};
            bool extras_ok {false};
        };

        struct CapturedFrameCompareReport
        {
            int32_t expected_seq {-1};
            int32_t expected_tick {-1};
            int32_t expected_round {-1};
            int32_t expected_master {-1};

            bool sim_match {false};
            bool input_log_match {false};
            bool rdb_match {false};
            bool extras_match {false};

            uint64_t expected_sim_hash {0};
            uint64_t live_sim_hash {0};
            uint64_t expected_input_log_hash {0};
            uint64_t live_input_log_hash {0};
            uint64_t expected_rdb_hash {0};
            uint64_t live_rdb_hash {0};
            uint64_t expected_extras_hash {0};
            uint64_t live_extras_hash {0};

            int32_t first_mismatch_region {-1};
            int32_t first_mismatch_offset {-1};
            uint8_t expected_byte {0};
            uint8_t live_byte {0};
            std::array<int32_t, 4> first_region_mismatch_offset
                {{-1, -1, -1, -1}};
            std::array<uint8_t, 4> first_region_expected_byte {{0, 0, 0, 0}};
            std::array<uint8_t, 4> first_region_live_byte {{0, 0, 0, 0}};
            int32_t first_ignored_mismatch_region {-1};
            int32_t first_ignored_mismatch_offset {-1};
            uint8_t first_ignored_expected_byte {0};
            uint8_t first_ignored_live_byte {0};
            uint32_t ignored_mismatch_count {0};
            const char* first_ignored_reason {"none"};
            bool strict_match {false};
            bool policy_match {false};
            bool ok {false};
            const char* reason {"unread"};
        };

        struct ReplayFrameOracleSnap
        {
            bool valid {false};
            int32_t seq {-1};
            int32_t round {-1};
            int32_t wall {-1};
            int32_t master {-1};
            int32_t last_round_result {0};
            uint64_t rng_state {0};
            ReplayScrubDiag::CharaMoveVmSnap p1 {};
            ReplayScrubDiag::CharaMoveVmSnap p2 {};
            uint64_t p1_input {0};
            uint64_t p2_input {0};
        };

        struct CapturedFrameOracleCompareReport
        {
            int32_t expected_seq {-1};
            int32_t expected_tick {-1};
            int32_t expected_round {-1};
            int32_t expected_master {-1};
            int32_t live_round {-1};
            int32_t live_master {-1};
            int player {-1};
            const char* field {"none"};
            const char* reason {"unread"};
            uint64_t expected_u64 {0};
            uint64_t live_u64 {0};
            float expected_float {0.0f};
            float live_float {0.0f};
            bool expected_valid {false};
            bool live_valid {false};
            bool p1_match {false};
            bool p2_match {false};
            bool input_match {false};
            bool rng_match {false};
            bool last_round_result_match {false};
            bool ok {false};
        };

        struct ReplayInputAuthorityReport
        {
            ReplayInputAuthority authority {ReplayInputAuthority::Unknown};
            bool current_input_ok {false};
            bool frame_input_ok {false};
            bool frame_input_diagnostic_only {true};
            bool latest_engine_input_ok {false};
            bool chara_replay_ring_ok {false};
            bool simulation_cache_ok {false};
            bool simulation_cache_diagnostic_only {true};
            int32_t active_count {-1};
            uint32_t active_mask {0};
            int32_t checked_slots {0};
            int32_t failing_slot {-1};
            uint32_t expected_current_input {0};
            uint32_t live_current_input {0};
            uint32_t frame_input_value {0};
            uint64_t expected_latest_engine_input {0};
            uint64_t live_latest_engine_input {0};
            bool ok {false};
            const char* reason {"unread"};
        };

        struct Sc6SeekVerifyReport
        {
            int32_t live_round {-1};
            int32_t replay_player_round {-1};
            int32_t input_master {-1};
            int32_t battle_master {-1};
            int32_t battle_master_delta {-1};
            uint8_t bm_main_state {0};
            uint8_t bm_status {0};
            int32_t active_count {-1};
            uint32_t active_mask {0};
            int32_t checked_slots {0};
            int32_t cache_slot {-1};
            int32_t cache_expected_frame_id {-1};
            int32_t cache_frame_id {-1};
            uint32_t cache_frame_index {0};
            uint32_t cache_input_value {0};
            uint32_t current_input_value {0};
            uint8_t cache_filled {0};
            bool cache_checked {false};
            bool bm_main_state_ok {false};
            bool bm_status_ok {false};
            bool round_master_ok {false};
            bool input_authority_ok {false};
            ReplayInputAuthority input_authority {
                ReplayInputAuthority::Unknown};
            const char* input_authority_reason {"unread"};
            int32_t input_authority_checked_slots {0};
            int32_t input_authority_slot {-1};
            uint32_t expected_current_input {0};
            uint32_t live_current_input {0};
            uint32_t frame_input_value {0};
            uint64_t expected_latest_engine_input {0};
            uint64_t live_latest_engine_input {0};
            bool current_input_ok {false};
            bool frame_input_ok {false};
            bool frame_input_diagnostic_only {true};
            bool latest_engine_input_ok {false};
            bool chara_replay_ring_ok {false};
            bool simulation_cache_ok {false};
            bool simulation_cache_diagnostic_only {true};
            bool ok {false};
            const char* reason {"unread"};
        };

        struct Sc6ResetApplyReport
        {
            uintptr_t battle_manager {0};
            uintptr_t input_log {0};
            uintptr_t replay_player {0};
            uintptr_t state_reset_data {0};
            uintptr_t reset_dst {0};
            int32_t target_round {-1};
            int32_t target_master {-1};
            int32_t origin_tick {-1};
            int32_t origin_seq {-1};
            int32_t origin_master {-1};
            int32_t origin_last_frame_id {-1};
            const char* reset_source {"none"};
            NativeSeekFailure failure {NativeSeekFailure::None};
            bool context_ok {false};
            bool reset_source_ok {false};
            bool reset_snapshot_read_ok {false};
            bool reset_snapshot_write_ok {false};
            bool input_log_restore_ok {false};
            bool rdb_restore_ok {false};
            bool set_move_state_ok {false};
            bool replay_cursor_write_ok {false};
            bool replay_player_cursor_write_ok {false};
        };

        // A round-boundary marker for the timeline UI: `seq` is the
        // capture sequence of the first snapshot of round `round`.
        // Returned by collect_round_markers().
        struct RoundMarker { int32_t seq; int32_t round; };

        // Runtime profile for the current/last Generate Timeline pass.
        // Values are accumulated on the game thread and read by the UI.
        struct TimelineGenProfile
        {
            bool     active;
            bool     experimental;
            bool     battle_step;
            bool     battle_step_probe;
            uint64_t frames;
            double   wall_seconds;
            double   ticks_per_second;
            double   avg_total_us;
            double   avg_sim_us;
            double   avg_inputlog_us;
            double   avg_rdb_us;
            double   avg_extras_us;
            double   avg_commit_us;
        };

        // Per-snapshot stride matches HgCpuDirect's allocator stride
        // (verified via post-KO cinematic ring at session+0xAA120+0x488).
        static constexpr size_t kSnapshotStride = 0x28018;

        // RVAs for the engine entry points (verified via Ghidra).
        static constexpr uintptr_t kRVA_ExecMoveChangeAndPost = 0x3841E0;
        static constexpr uintptr_t kRVA_ExecFinalizeAndPost   = 0x384540;
        static constexpr uintptr_t kRVA_LuxBattlePerFrameTick = 0x2DBC60;
        static constexpr uintptr_t kRVA_LuxBattleInteractiveReplayReset = 0x37E900;
        static constexpr uintptr_t kRVA_ALuxBattleManagerSetMoveState = 0x3F8370;
        static constexpr uintptr_t kRVA_FrameInputLogAdvanceReplayClock = 0x3E1FC0;
        static constexpr uintptr_t kRVA_BattleManagerSimulationLoop = 0x3FE520;
        static constexpr uintptr_t kRVA_FrameCounter          = 0x470D0C4;
        static constexpr uintptr_t kRVA_PerFrameCameraArgs    = 0x470D100;
        static constexpr uintptr_t kRVA_LatestEngineInput     = 0x4855700;
        static constexpr uintptr_t kRVA_InputRingBaseOffset   = 0x470DED0;
        static constexpr uintptr_t kRVA_CCpuCommandArray      = 0x4715400;
        static constexpr uintptr_t kRVA_PerPlayerInputRing    = 0x485E750;
        static constexpr uintptr_t kRVA_PerPlayerInputCursor  = 0x485EB20;
        static constexpr uintptr_t kRVA_LfsrState             = 0x485EB30;
        static constexpr uintptr_t kRVA_LfsrIndex             = 0x485EB94;
        static constexpr uintptr_t kRVA_DemoGotoTimeInSeconds = 0x1E0ECA0;

        static constexpr uintptr_t kWorldModePump_BattleManager_Off = 0x30;
        static constexpr uintptr_t kWorldModePump_SubDriver_Off     = 0x38;
        static constexpr uintptr_t kBM_InteractiveReplay_Off        = 0xAA120;
        static constexpr size_t    kRoundStartDataBytes             = 0xC0;
        static constexpr int32_t   kMaxSc6ReplayRounds              = 16;

        // Accuracy guard: setting demo.GotoTimeInSeconds may route to the
        // same engine scrub code, but a CVar write only proves that the
        // console variable accepted text.  It does not prove the active
        // UDemoNetDriver accepted an FGotoTimeInSecondsTask.  Keep strict
        // timeline seeks on the direct path where we can observe task
        // insertion before temporarily releasing tick gates.
        static constexpr bool kAllowUnobservedDemoGotoCVarFallback = false;

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
        static constexpr uintptr_t kCinematic_RingCount_Off  = 0x468;
        static constexpr uintptr_t kCinematic_RingCursor_Off = 0x46C;
        static constexpr uintptr_t kCinematic_RingTags_Off   = 0x470;    // int[5]
        static constexpr uintptr_t kCinematic_CurrentFrame_Off = 0x484;
        static constexpr uintptr_t kCinematic_PaletteCtrl_Off = 0xF0518; // int[4]

        // g_LuxBattle_LastRoundResultType @ 0x144846408 (i16).  0 while a
        // round is live; set non-zero (KO=1, LethalHit=2, RingOut=3,
        // TimeUp=4, ... 1..11) by LuxBattle_EvaluateRoundResult @
        // 0x140385440 when a round ends, and reset to 0 by
        // LuxBattle_ClearFrameFlags @ 0x140386464 at the next round's
        // start.  tick_generate_timeline uses it to stop generation the
        // moment the FINAL round ends - the real "match is decided" event.
        static constexpr uintptr_t kRVA_LastRoundResultType  = 0x4846408;

        // Per-chara match-progress fields, read via the kRVA_CharaSlotP1
        // / P2 globals defined further down.  chara+0x1314 (u16) is the
        // round-win count this match; chara+0x1318 (u32) the rounds
        // needed to win.  LuxBattle_EvaluateRoundResult @ 0x140385440
        // declares a match win when needed <= wins - the ReplayPlayer-
        // independent "match decided" truth used by match_decided().
        static constexpr uintptr_t kChara_RoundWins_Off      = 0x1314;
        static constexpr uintptr_t kChara_RoundsToWin_Off    = 0x1318;
        // PLAYER::vftable - the vtable LuxBattleChara_Ctor @ 0x140303810
        // writes at chara offset 0 (LEA RAX,[0x143E87698]).  A live
        // ALuxBattleChara has *(void**)chara == imageBase + this RVA; a
        // freed-and-reused slot does not.  Used as a liveness fingerprint
        // before the do_seek snapshot restore writes through the charas.
        static constexpr uintptr_t kRVA_CharaVTable          = 0x3E87698;

        // Capture is unbounded: the dedup store (ChunkPool + RegionStore)
        // keeps only DISTINCT 512-byte chunks, and most of a 0x28018-byte
        // snapshot is byte-identical tick-to-tick (stage geometry, configs,
        // move-data), so a whole replay's snapshots fit in a small fraction
        // of their raw size.  The only bound is a resident-memory ceiling:
        // when the store (dedup pool arena + per-tick chunk-id lists + tag
        // timeline) reaches kMaxStoreBytes, capture stops gracefully and the
        // timeline keeps everything captured so far.  2 GB covers any
        // realistic multi-round match with a wide margin - it exists only to
        // bound a pathological run.  The store is created on first Replay-
        // presence entry so users who never watch replays don't pay the
        // memory cost.
        static constexpr size_t kMaxStoreBytes = 2ull * 1024 * 1024 * 1024;

        static ReplayScrub& instance()
        {
            static ReplayScrub s;
            return s;
        }

        // Create the dedup store on first call; later calls are no-ops.
        // Should be called from on_unreal_init or the first cockpit tick
        // observing Replay presence.
        bool ensure_initialized()
        {
            if (m_initialized.load(std::memory_order_acquire))
                return true;

            // Resolve native function pointers via image base.  Done
            // lazily here so we don't fight startup races with
            // NativeBinding::resolve() being called from elsewhere.
            if (!resolve_natives()) return false;

            // Size the four RegionStores and clear the dedup pool + tag
            // timeline.  drop_ring() does exactly this; ensure_initialized
            // only additionally publishes m_initialized.
            drop_ring();
            m_have_last_counter = false;

            m_initialized.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub] initialised; deduplicating snapshot "
                    "store ready (sim {} B + InputLog {} B + RDB {} B + "
                    "extras {} B per tick, {} MB capture ceiling)\n"),
                kSnapshotStride, kIL_CaptureBytes, kRDB_Bytes, kExtras_Bytes,
                kMaxStoreBytes / (1024ull * 1024ull));
                RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub] build replay-accuracy-v12i "
                    "strict_sc6_exact=1 captured_seek_validate=1 "
                    "native_round_replay_diag=0 legacy_seek={} "
                    "legacy_preview={} cache_diag_only=1 "
                    "input_authority_verify=1 "
                    "frame_input_diag_only=1 "
                    "offline_replay_ring_gate=1 "
                    "reset_snapshots=1 gate_policy_split=1 "
                    "postgen_light_hold=1 stale_timeline_rebuild=1 "
                    "failed_seek_cleanup=1 manual_gate_split=1 "
                    "latest_input_restore=1 input_ring_restore=1 "
                    "rng_restore=1 required_extras=1 "
                    "capture_fail_abort=1 compare_diag_only=0 "
                    "raw_snapshot_diag_only=1 oracle_snapshot_verify=1 "
                    "cache_value_diag_only=1 cache_tag_diag_only=1 "
                    "native_call_seh_guard=1 playback_progress_diag=1 "
                    "cinematic_ring_seek_reset=1 "
                    "playback_result_guard=1 "
                    "exact_seek_round_start_guard=1 "
                    "cross_round_snapshot_seek=0 "
                    "cross_round_reset_seek=1 "
                    "cross_round_replay_frame_fast_forward=0 "
                    "drag_restore=0 strict_snapshot_verify=0 "
                    "policy_snapshot_verify=1 exact_subblock_restore=1 "
                    "strict_cache_verify=0 native_validation_step=1 "
                    "restore_probe=1 final_semantic_repair=1 "
                    "gameplay_step_oracle=1 "
                    "cross_round_reset_context=1 "
                    "no_raw_byte_gate=1\n"),
                kEnableLegacySeekDiagnostics ? 1 : 0,
                kEnableLegacySnapshotPreview ? 1 : 0);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ReplayScrub] deployment check: v12i loaded from "
                    "HorseMod main.dll\n"));
            {
                ReplayTraceFields f;
                 f.string("build", "replay-accuracy-v12i")
                 .boolean("strict_sc6_exact", true)
                 .boolean("captured_seek_validate", true)
                 .boolean("native_round_replay_diag", false)
                 .boolean("legacy_seek", kEnableLegacySeekDiagnostics)
                 .boolean("legacy_preview", kEnableLegacySnapshotPreview)
                 .boolean("cache_diag_only", true)
                 .boolean("input_authority_verify", true)
                 .boolean("frame_input_diag_only", true)
                 .boolean("offline_replay_ring_gate", true)
                 .boolean("reset_snapshots", true)
                 .boolean("gate_policy_split", true)
                 .boolean("postgen_light_hold", true)
                 .boolean("stale_timeline_rebuild", true)
                 .boolean("failed_seek_cleanup", true)
                 .boolean("manual_gate_split", true)
                 .boolean("latest_input_restore", true)
                 .boolean("input_ring_restore", true)
                 .boolean("rng_restore", true)
                 .boolean("required_extras", true)
                 .boolean("capture_fail_abort", true)
                 .boolean("cross_round_snapshot_seek", false)
                 .boolean("cross_round_reset_seek", true)
                 .boolean("drag_restore", false)
                 .boolean("compare_diag_only", false)
                 .boolean("raw_snapshot_diag_only", true)
                 .boolean("oracle_snapshot_verify", true)
                 .boolean("strict_snapshot_verify", false)
                 .boolean("policy_snapshot_verify", true)
                 .boolean("exact_subblock_restore", true)
                 .boolean("strict_cache_verify", false)
                 .boolean("native_validation_step", true)
                 .boolean("restore_probe", true)
                 .boolean("final_semantic_repair", true)
                 .boolean("gameplay_step_oracle", true)
                 .boolean("cross_round_reset_context", true)
                 .boolean("no_raw_byte_gate", true);
                ReplayDebugTrace::instance().event(
                    "replay_scrub_initialized", f);
            }
            return true;
        }

        // Tear down at module shutdown.
        void shutdown()
        {
            // Restore the engine frame cap + screen percentage + redraw
            // hook first - none of them must outlive the module if
            // generation was still running at unload.
            m_frame_cap.disengage();
            m_screen_pct.disengage();
            m_render_skip.disengage();
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

            // One master-clock read per cockpit tick.  The engine does
            // not advance between here and the end of this function (we
            // run in the cockpit PRE-tick), so every consumer below
            // reuses tick_master instead of re-resolving BM->IL+0x3A4.
            // The UI thread reads the cached copy via current_play_-
            // position() to extrapolate a smooth playhead post-seek.
            const int32_t tick_master = read_engine_master_clock();
            m_live_master_cached.store(tick_master, std::memory_order_release);

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
                                    tick_master);
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
                int32_t native_guard_ticks =
                    m_native_demo_seek_guard_ticks.load(
                        std::memory_order_acquire);
                if (native_guard_ticks > 0)
                {
                    m_native_demo_seek_guard_ticks.fetch_sub(
                        1, std::memory_order_acq_rel);
                }
                const bool native_seek_may_rebuild_actors =
                    native_guard_ticks > 0;

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
                {
                    if (native_seek_may_rebuild_actors)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[ReplayScrub] replay actor identity changed "
                            "during native demo seek; preserving generated "
                            "timeline (bm_changed={} rp_changed={} "
                            "guard_ticks={})\n"),
                            bm_changed ? 1 : 0, rp_changed ? 1 : 0,
                            native_guard_ticks);
                    }
                    else
                    {
                        reset_for_new_replay(
                            bm_changed ? "new BattleManager"
                                       : "new ReplayPlayer");
                    }
                }
                if (bm) m_last_bm_obj            = bm;
                if (rp) m_last_replay_player_obj = rp;
            }

            // Capture runs when passive capture is enabled OR a
            // "Generate timeline" pass is in progress.  Passive capture
            // is OFF by default (the per-frame snapshot is expensive);
            // the deliberate Generate pass is the normal way to fill
            // the ring.
            if (!m_capture_enabled.load(std::memory_order_acquire)
                && !(m_timeline_gen_state.load(std::memory_order_acquire)
                     == static_cast<int>(TimelineGenState::Generating)
                     && !m_gen_battle_step_generate.load(
                         std::memory_order_acquire)))
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
                m_last_master       = tick_master;
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
                m_last_master  = tick_master;
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
            const int32_t cur_master = tick_master;
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
            const bool cap_ok =
                capture_snapshot(static_cast<int32_t>(cur), true);
            if (m_timeline_gen_state.load(std::memory_order_acquire)
                    == static_cast<int>(TimelineGenState::Generating))
            {
                if (!cap_ok)
                {
                    const int32_t fails = ++m_gen_capture_fail_count;
                    if (fails == 1)
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub] Generate timeline capture failed "
                            "at startup/progress (wall={} master={} round={} "
                            "extras_reason={}); generation will stop if this "
                            "persists\n"),
                            cur, cur_master, read_current_round(),
                            RC::to_generic_string(
                                m_last_extras_failure
                                    ? m_last_extras_failure : "unknown"));
                    }
                    if (fails >= kGenMaxConsecutiveCaptureFailures)
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub] Generate timeline aborted - "
                            "capture failed {} consecutive frames "
                            "(last reason={})\n"),
                            fails,
                            RC::to_generic_string(
                                m_last_extras_failure
                                    ? m_last_extras_failure : "unknown"));
                        stop_generate_timeline("capture-failed", false);
                        return;
                    }
                }
                else
                {
                    m_gen_capture_fail_count = 0;
                }
            }
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
            // evolves after the native UDemoNetDriver seek request.  This
            // is the engine-side replay cursor; if it does not move, the
            // async checkpoint/packet task did not run.
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

        // Pre-gate service runs before dllmain's frame_step_apply().
        // This lets a newly queued captured seek restore its validation
        // origin and publish a one-frame validation request before the
        // gate driver grants credits for this cockpit tick.
        void service_pre_frame_gate()
        {
            if (!is_initialized()) return;
            service_ui_command();
            service_playback_result_guard();
            service_drag_preview();
            service_sc6_exact_seek_job();
            service_pending_demo_goto_time_seek(false);
        }

        // Post-gate service runs after frame_step_apply().  The seek job's
        // own wait guard prevents observing a just-granted credit before
        // SC6's world tick has actually consumed it, but this pass catches
        // immediate failures/state publications and keeps UI state fresh.
        void service_post_frame_gate()
        {
            if (!is_initialized()) return;
            service_sc6_exact_seek_job();
            service_native_demo_seek_settle();
            service_playback_progress_diag();
            publish_timeline_state();
        }

        // Back-compat wrapper for any older call sites.
        void service_seek_request()
        {
            service_pre_frame_gate();
            service_post_frame_gate();
        }

        void service_playback_progress_diag() noexcept
        {
            const bool playing =
                m_scrub_mode.load(std::memory_order_acquire)
                    == static_cast<int32_t>(ScrubMode::Playing)
                && !m_paused.load(std::memory_order_acquire);
            if (!playing)
            {
                m_playback_diag_last_master = -1;
                return;
            }

            const int32_t master = read_engine_master_clock();
            const int32_t round = read_current_round();
            const int32_t result = read_last_round_result();
            const int32_t is_playing_back = read_replay_is_playing_back();
            if (master < 0 && round < 0 && result == 0
                && is_playing_back < 0)
            {
                return;
            }

            bool should_log = false;
            if (m_playback_diag_last_master < 0)
                should_log = true;
            else if (master >= 0
                     && master - m_playback_diag_last_master >= 60)
                should_log = true;
            else if (master >= 0 && master < m_playback_diag_last_master)
                should_log = true;
            if (round != m_playback_diag_last_round
                || result != m_playback_diag_last_result
                || is_playing_back != m_playback_diag_last_is_playing)
            {
                should_log = true;
            }
            if (!should_log) return;

            int32_t bm_last_applied = -1;
            int32_t bm_last_frame_id = -1;
            int32_t bm_frame_advance = -1;
            uint8_t bm_move_state = 0;
            uint8_t bm_status = 0;
            uintptr_t bm_addr = 0;
            if (RC::Unreal::UObject* bm_obj =
                    m_bm_ptr.get(L"LuxBattleManager"))
            {
                uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
                bm_addr = reinterpret_cast<uintptr_t>(bm_obj);
                SafeReadInt32(bm + kBM_nReplayLastApplied_Off,
                              &bm_last_applied);
                SafeReadInt32(bm + kBM_nReplayLastFrameID_Off,
                              &bm_last_frame_id);
                SafeReadInt32(bm + kBM_nFrameAdvanceCounter_Off,
                              &bm_frame_advance);
                SafeReadUInt8(bm + kBM_bMoveStateByte_Off,
                              &bm_move_state);
                SafeReadUInt8(bm + kBM_bStatusByte_Off,
                              &bm_status);
            }

            const int32_t playhead = current_play_position();
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.playback] round={} master={} playhead={} "
                "result={} rp_playing={} bm=0x{:X} bm[lastApplied={} "
                "lastFrameID={} frameAdv={} moveState=0x{:X} status=0x{:X}] "
                "seek_target={} seek_master={}\n"),
                round, master, playhead, result, is_playing_back,
                bm_addr, bm_last_applied, bm_last_frame_id,
                bm_frame_advance, static_cast<unsigned>(bm_move_state),
                static_cast<unsigned>(bm_status),
                m_last_seek_target.load(std::memory_order_acquire),
                m_last_seek_master_tag.load(std::memory_order_acquire));

            ReplayTraceFields f;
            f.integer("round", round)
             .integer("master", master)
             .integer("playhead", playhead)
             .integer("last_round_result", result)
             .integer("replay_player_playing", is_playing_back)
             .hex("bm", bm_addr)
             .integer("bm_last_applied", bm_last_applied)
             .integer("bm_last_frame_id", bm_last_frame_id)
             .integer("bm_frame_advance", bm_frame_advance)
             .uinteger("bm_move_state", bm_move_state)
             .uinteger("bm_status", bm_status)
             .integer("seek_target",
                      m_last_seek_target.load(std::memory_order_acquire))
             .integer("seek_master",
                      m_last_seek_master_tag.load(std::memory_order_acquire));
            ReplayDebugTrace::instance().event("playback_progress", f);

            m_playback_diag_last_master = master;
            m_playback_diag_last_round = round;
            m_playback_diag_last_result = result;
            m_playback_diag_last_is_playing = is_playing_back;
        }

        void service_playback_result_guard() noexcept
        {
            const bool playing =
                m_scrub_mode.load(std::memory_order_acquire)
                    == static_cast<int32_t>(ScrubMode::Playing)
                && !m_paused.load(std::memory_order_acquire);
            if (!playing) return;

            if (m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating))
            {
                return;
            }
            if (!m_timeline_seek_data_valid.load(
                    std::memory_order_acquire)
                || has_pending_native_seek()
                || m_ui_dragging.load(std::memory_order_acquire))
            {
                return;
            }

            const int32_t round = read_current_round();
            const int32_t master =
                m_live_master_cached.load(std::memory_order_acquire);
            if (round < 0 || round >= kMaxSc6ReplayRounds || master < 0)
                return;

            const int32_t bypass_round =
                m_playback_result_guard_bypass_round.load(
                    std::memory_order_acquire);
            if (bypass_round >= 0 && bypass_round != round)
            {
                m_playback_result_guard_bypass_round.store(
                    -1, std::memory_order_release);
            }
            else if (bypass_round == round)
            {
                return;
            }

            const int32_t guard_seq =
                playback_result_guard_seq_for_round(round);
            if (guard_seq < 0) return;

            int32_t live_seq = find_slot_for_round_master(round, master);
            if (live_seq < 0) live_seq = current_play_position();
            live_seq = clamp_seq_to_timeline(live_seq);
            if (live_seq < guard_seq) return;

            const int32_t last_safe =
                m_playback_round_last_safe_seq[
                    static_cast<size_t>(round)];
            const int32_t first_result =
                m_playback_round_first_result_seq[
                    static_cast<size_t>(round)];

            m_paused.store(true, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::RestoredFrameHold),
                std::memory_order_release);
            m_playback_result_guard_paused.store(true,
                                                std::memory_order_release);
            m_playback_result_guard_pause_round.store(
                round, std::memory_order_release);
            m_playback_result_guard_pause_seq.store(
                live_seq, std::memory_order_release);
            publish_ui_target(live_seq);
            m_last_seek_target.store(live_seq, std::memory_order_release);
            m_last_seek_master_tag.store(master, std::memory_order_release);
            publish_native_status(NativeSeekStatus::Landed);
            publish_mode(ScrubMode::PausedPreview);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.playback_guard] paused before round result "
                "round={} live_seq={} guard_seq={} last_safe_seq={} "
                "first_result_seq={} master={}; press Play again to "
                "continue through the result\n"),
                round, live_seq, guard_seq, last_safe, first_result,
                master);

            ReplayTraceFields f;
            f.integer("round", round)
             .integer("live_seq", live_seq)
             .integer("guard_seq", guard_seq)
             .integer("last_safe_seq", last_safe)
             .integer("first_result_seq", first_result)
             .integer("master", master);
            ReplayDebugTrace::instance().event(
                "playback_result_guard_pause", f);
        }

        void reset_playback_result_guard_markers() noexcept
        {
            m_playback_round_first_safe_seq.fill(-1);
            m_playback_round_last_safe_seq.fill(-1);
            m_playback_round_first_result_seq.fill(-1);
            m_playback_result_guard_bypass_round.store(
                -1, std::memory_order_release);
            m_playback_result_guard_paused.store(
                false, std::memory_order_release);
            m_playback_result_guard_pause_round.store(
                -1, std::memory_order_release);
            m_playback_result_guard_pause_seq.store(
                -1, std::memory_order_release);
        }

        void clear_playback_result_guard_override() noexcept
        {
            m_playback_result_guard_bypass_round.store(
                -1, std::memory_order_release);
            m_playback_result_guard_paused.store(
                false, std::memory_order_release);
            m_playback_result_guard_pause_round.store(
                -1, std::memory_order_release);
            m_playback_result_guard_pause_seq.store(
                -1, std::memory_order_release);
        }

        void arm_playback_result_guard_override_if_needed() noexcept
        {
            if (!m_playback_result_guard_paused.load(
                    std::memory_order_acquire))
                return;

            const int32_t round =
                m_playback_result_guard_pause_round.load(
                    std::memory_order_acquire);
            if (round < 0 || round >= kMaxSc6ReplayRounds)
            {
                clear_playback_result_guard_override();
                return;
            }

            m_playback_result_guard_bypass_round.store(
                round, std::memory_order_release);
            m_playback_result_guard_paused.store(
                false, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.playback_guard] user resumed through "
                "round-result guard round={} seq={}\n"),
                round,
                m_playback_result_guard_pause_seq.load(
                    std::memory_order_acquire));
        }

        void update_playback_result_guard_markers(
            int32_t round,
            int32_t master,
            int32_t round_result) noexcept
        {
            if (round < 0 || round >= kMaxSc6ReplayRounds || master < 0)
                return;

            const int32_t seq = raw_latest_seq();
            if (seq < 0) return;

            const size_t r = static_cast<size_t>(round);
            if (round_result == 0)
            {
                if (master >= kRoundBoundarySeekGuardMaster)
                {
                    if (m_playback_round_first_safe_seq[r] < 0)
                        m_playback_round_first_safe_seq[r] = seq;
                    m_playback_round_last_safe_seq[r] = seq;
                }
                return;
            }

            if (m_playback_round_first_result_seq[r] < 0)
                m_playback_round_first_result_seq[r] = seq;
        }

        int32_t playback_result_guard_seq_for_round(
            int32_t round) const noexcept
        {
            if (round < 0 || round >= kMaxSc6ReplayRounds)
                return -1;

            const size_t r = static_cast<size_t>(round);
            const int32_t last_safe = m_playback_round_last_safe_seq[r];
            if (last_safe < 0) return -1;

            int32_t guard_seq = last_safe - kPostResultParkBackoffFrames;
            const int32_t first_safe = m_playback_round_first_safe_seq[r];
            if (first_safe >= 0 && guard_seq < first_safe)
                guard_seq = first_safe;
            return clamp_seq_to_timeline(guard_seq);
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

        void log_generate_locked_complete_once() const noexcept
        {
            static std::atomic<bool> s_logged{false};
            if (s_logged.exchange(true, std::memory_order_relaxed))
                return;
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub] Generate timeline ignored - current replay "
                "already has a complete generated timeline; load/restart "
                "replay to generate again\n"));
        }

        // [render thread] "Generate timeline" UI requests.  The UI runs
        // on the render/present thread; these post an atomic request
        // that tick_generate_timeline() services on the game thread (the
        // same request/handoff pattern as request_seek).
        void request_generate_timeline() noexcept
        {
            ReplayTraceFields f;
            f.string("mode", "normal").boolean("armed", false);
            ReplayDebugTrace::instance().event("generate_request", f);
            if (is_timeline_generation_locked_complete())
            {
                log_generate_locked_complete_once();
                return;
            }
            if (arm_generate_if_replay_already_advanced(kGenReqStart))
                return;
            m_gen_request.store(kGenReqStart, std::memory_order_release);
        }
        // As request_generate_timeline(), but the pass also skips the
        // scene redraw each frame (RenderSkipOverride) for a much faster
        // fast-forward - drives the "Experimental" button.
        void request_generate_timeline_experimental() noexcept
        {
            ReplayTraceFields f;
            f.string("mode", "experimental").boolean("armed", false);
            ReplayDebugTrace::instance().event("generate_request", f);
            if (is_timeline_generation_locked_complete())
            {
                log_generate_locked_complete_once();
                return;
            }
            if (arm_generate_if_replay_already_advanced(
                    kGenReqStartExperimental))
                return;
            m_gen_request.store(kGenReqStartExperimental,
                                std::memory_order_release);
        }
        // Launch a second experimental generation path that drives
        // the timeline by calling LuxBattle_PerFrameTick directly
        // (bypassing UE4 rendering) instead of replay fast-forward.
        void request_generate_timeline_experimental_battle_step() noexcept
        {
            ReplayTraceFields f;
            f.string("mode", "battle_step").boolean("armed", false);
            ReplayDebugTrace::instance().event("generate_request", f);
            if (is_timeline_generation_locked_complete())
            {
                log_generate_locked_complete_once();
                return;
            }
            if (arm_generate_if_replay_already_advanced(
                    kGenReqStartBattleStep))
                return;
            m_gen_request.store(kGenReqStartBattleStep,
                                std::memory_order_release);
        }
        // Internal one-shot helper for diagnostics: save-state,
        // run one direct PerFrameTick frame, then restore immediately.
        void request_generate_timeline_experimental_battle_step_probe() noexcept
        {
            m_gen_request.store(kGenReqBattleStepProbe,
                                std::memory_order_release);
        }
        void request_stop_generate_timeline() noexcept
        {
            if (m_gen_armed_hold_logged.load(std::memory_order_acquire))
            {
                m_paused.store(false, std::memory_order_release);
                m_hold_kind.store(
                    static_cast<int32_t>(ReplayScrubHoldKind::None),
                    std::memory_order_release);
            }
            m_gen_armed_mode.store(kGenReqNone, std::memory_order_release);
            reset_armed_generate_wait_log();
            m_gen_request.store(kGenReqStop, std::memory_order_release);
        }
        void cancel_generate_waiting() noexcept
        {
            const int prev = m_gen_armed_mode.exchange(
                kGenReqNone, std::memory_order_acq_rel);
            if (prev != kGenReqNone)
            {
                if (m_gen_armed_hold_logged.load(
                        std::memory_order_acquire))
                {
                    m_paused.store(false, std::memory_order_release);
                    m_hold_kind.store(
                        static_cast<int32_t>(ReplayScrubHoldKind::None),
                        std::memory_order_release);
                }
                reset_armed_generate_wait_log();
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] Generate timeline waiting cancelled\n"));
            }
        }

        // "Generate timeline" state accessor (2026-05-16) - the UI
        // reads this to choose the button label / status text.
        TimelineGenState timeline_gen_state() const noexcept
        {
            return static_cast<TimelineGenState>(
                m_timeline_gen_state.load(std::memory_order_acquire));
        }

        bool is_timeline_generation_locked_complete() const noexcept
        {
            return has_context_valid_completed_timeline();
        }

        bool has_completed_timeline() const noexcept
        {
            return timeline_gen_state() == TimelineGenState::Done
                && m_tags.count() > 0;
        }

        bool has_context_valid_completed_timeline() const noexcept
        {
            return has_completed_timeline()
                && m_timeline_context_valid.load(
                    std::memory_order_acquire);
        }

        bool has_stale_preserved_timeline() const noexcept
        {
            return has_completed_timeline()
                && !m_timeline_context_valid.load(
                    std::memory_order_acquire);
        }

        bool is_generate_armed_for_next_clean_start() const noexcept
        {
            return m_gen_armed_mode.load(std::memory_order_acquire)
                != kGenReqNone;
        }

        const char* generate_status_text() noexcept
        {
            if (is_generate_armed_for_next_clean_start())
            {
                if (m_gen_armed_hold_logged.load(
                        std::memory_order_acquire))
                    return "Waiting at replay start. Timeline will build automatically.";
                return "Waiting. Restart or reload the replay; timeline will build automatically.";
            }

            if (has_stale_preserved_timeline())
                return "Previous timeline is from an old replay. Restart or reload, then Generate will replace it.";

            const GenerateStartReadiness ready =
                read_generate_start_readiness();
            if (ready.context_ok && !ready.clean_start)
                return "Replay already started. Click Generate, then restart or reload the replay.";
            if (ready.clean_start
                && (!ready.seek_context_ok || !ready.reset_source_ok))
                return "Replay is still loading. Wait at the start before generating.";

            return "";
        }

        bool can_generate_timeline() noexcept
        {
            const GenerateStartReadiness ready =
                read_generate_start_readiness();
            return GameMode::instance().current_presence()
                    == GamePresence::Replay
                && timeline_gen_state() != TimelineGenState::Generating
                && !is_timeline_generation_locked_complete()
                && ready.context_ok
                && (!ready.clean_start
                    || (ready.seek_context_ok && ready.reset_source_ok));
        }

        const char* generate_block_reason() noexcept
        {
            if (timeline_gen_state() == TimelineGenState::Generating)
                return "Generation is already running.";
            if (is_timeline_generation_locked_complete())
                return "Timeline complete. Restart or load another replay to build it again.";
            if (has_stale_preserved_timeline())
                return "";
            const GenerateStartReadiness ready =
                read_generate_start_readiness();
            if (ready.context_ok
                && (!ready.clean_start
                    || (ready.seek_context_ok && ready.reset_source_ok)))
                return "";
            return ready.reason;
        }

        // True while the current generation pass is the EXPERIMENTAL
        // render-skipping one (only meaningful when timeline_gen_state()
        // == Generating).  The UI uses it for the status label.
        bool is_experimental_generation() const noexcept
        {
            return m_gen_mode.load(std::memory_order_acquire)
                == static_cast<int>(BattleStepMode::RenderSkip);
        }

        bool is_battle_step_generation() const noexcept
        {
            return m_gen_mode.load(std::memory_order_acquire)
                == static_cast<int>(BattleStepMode::DirectPerFrame);
        }

        TimelineGenProfile timeline_gen_profile() const noexcept
        {
            const bool active =
                m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating);
            const auto now = std::chrono::steady_clock::now();
            double wall = std::chrono::duration<double>(
                active ? (now - m_gen_started_at)
                       : (m_gen_finished_at - m_gen_started_at)).count();
            if (!(wall > 0.0)) wall = 0.0;

            const uint64_t frames =
                m_gen_profile_frames.load(std::memory_order_acquire);
            const double denom = frames ? static_cast<double>(frames) : 1.0;
            TimelineGenProfile p{};
            p.active = active;
            p.experimental =
                m_gen_mode.load(std::memory_order_acquire)
                    == static_cast<int>(BattleStepMode::RenderSkip);
            p.battle_step =
                m_gen_mode.load(std::memory_order_acquire)
                    == static_cast<int>(BattleStepMode::DirectPerFrame);
            p.battle_step_probe =
                m_gen_battle_step_probe.load(std::memory_order_acquire);
            p.frames = frames;
            p.wall_seconds = wall;
            p.ticks_per_second =
                (wall > 0.0) ? static_cast<double>(frames) / wall : 0.0;
            p.avg_total_us =
                static_cast<double>(m_gen_profile_total_us.load(
                    std::memory_order_acquire)) / denom;
            p.avg_sim_us =
                static_cast<double>(m_gen_profile_sim_us.load(
                    std::memory_order_acquire)) / denom;
            p.avg_inputlog_us =
                static_cast<double>(m_gen_profile_il_us.load(
                    std::memory_order_acquire)) / denom;
            p.avg_rdb_us =
                static_cast<double>(m_gen_profile_rdb_us.load(
                    std::memory_order_acquire)) / denom;
            p.avg_extras_us =
                static_cast<double>(m_gen_profile_extras_us.load(
                    std::memory_order_acquire)) / denom;
            p.avg_commit_us =
                static_cast<double>(m_gen_profile_commit_us.load(
                    std::memory_order_acquire)) / denom;
            return p;
        }

        // [game thread] Begin "Generate timeline": remove SC6's engine
        // frame-rate cap so the replay fast-forwards, with tick_capture()
        // filling the snapshot ring as it plays.  Called only by
        // tick_generate_timeline() servicing a UI request - never from
        // the render thread directly.  No-op (with a log line) unless
        // we're in the Replay viewer with the ring ready and capture on.
        void start_generate_timeline(bool experimental) noexcept
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
            if (is_timeline_generation_locked_complete())
            {
                log_generate_locked_complete_once();
                return;
            }
            const GenerateStartReadiness ready =
                read_generate_start_readiness();
            if (!ready.clean_start)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline blocked - replay is "
                    "not at start (reason={} context_ok={} round={} "
                    "input_master={} battle_master={} source={} "
                    "object_bm=0x{:X} object_il=0x{:X} wmp_bm=0x{:X} "
                    "wmp_il=0x{:X} chosen_bm=0x{:X} chosen_il=0x{:X} "
                    "seek_context_ok={} sub=0x{:X} rp=0x{:X} "
                    "reset=0x{:X} live_bm_reset_ok={})\n"),
                    RC::to_generic_string(ready.reason),
                    ready.context_ok ? 1 : 0, ready.round,
                    ready.input_master, ready.battle_master,
                    RC::to_generic_string(sc6_context_source_name(
                        ready.seek_context_source)),
                    ready.object_registry_battle_manager,
                    ready.object_registry_input_log,
                    ready.world_mode_pump_battle_manager,
                    ready.world_mode_pump_input_log,
                    ready.battle_manager, ready.input_log,
                    ready.seek_context_ok ? 1 : 0, ready.sub_driver,
                    ready.replay_player, ready.state_reset_data,
                    ready.live_bm_reset_ok ? 1 : 0);
                return;
            }
            if (!ready.seek_context_ok || !ready.reset_source_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline blocked - exact SC6 "
                    "reset source is not ready failure={} "
                    "source={} context_ok={} seek_context_ok={} round={} "
                    "input_master={} battle_master={} object_bm=0x{:X} "
                    "object_il=0x{:X} wmp_bm=0x{:X} wmp_il=0x{:X} "
                    "chosen_bm=0x{:X} chosen_il=0x{:X} sub=0x{:X} "
                    "rp=0x{:X} reset=0x{:X} irs=0x{:X} "
                    "live_bm_reset_ok={} state_reset_data_ok={}\n"),
                    RC::to_generic_string(sc6_context_failure_name(
                        ready.seek_context_failure)),
                    RC::to_generic_string(sc6_context_source_name(
                        ready.seek_context_source)),
                    ready.context_ok ? 1 : 0,
                    ready.seek_context_ok ? 1 : 0, ready.round,
                    ready.input_master, ready.battle_master,
                    ready.object_registry_battle_manager,
                    ready.object_registry_input_log,
                    ready.world_mode_pump_battle_manager,
                    ready.world_mode_pump_input_log,
                    ready.battle_manager, ready.input_log,
                    ready.sub_driver, ready.replay_player,
                    ready.state_reset_data, ready.interactive_replay,
                    ready.live_bm_reset_ok ? 1 : 0,
                    ready.state_reset_data_ok ? 1 : 0);
                return;
            }
            // Generation needs the replay PLAYING - tick_capture() skips
            // capture while paused.  Drop any pause/scrub state.  (The
            // passive-capture toggle is NOT required: tick_capture()
            // captures whenever a generation pass is running.)
            m_paused.store(false, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::None),
                std::memory_order_release);
            cancel_sc6_exact_seek("generation start");
            m_sc6_seek_job = Sc6ExactSeekJob{};

            if (!m_frame_cap.engage())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline failed - could not "
                    "remove the engine frame cap\n"));
                return;
            }
            // Make the uncapped loop sim-bound rather than render-bound
            // (SC6 replay viewing is render-limited well below 60fps, so
            // the frame cap alone does not fast-forward):
            //   experimental - skip the scene redraw entirely
            //     (RenderSkipOverride), the strongest fast-forward lever.
            //     If the redraw hook can't engage, fall back to the
            //     screen-percentage cut so the button still does
            //     something useful.
            //   normal - just drop the render resolution.
            // Either way generation still runs if this fails, just
            // render-limited.
            m_gen_mode.store(
                experimental
                    ? static_cast<int>(BattleStepMode::RenderSkip)
                    : static_cast<int>(BattleStepMode::None),
                std::memory_order_release);
            m_gen_battle_step_probe.store(false,
                                          std::memory_order_release);
            m_gen_battle_step_generate.store(false,
                                            std::memory_order_release);
            if (experimental)
            {
                if (!m_render_skip.engage())
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] experimental render-skip could not "
                        "engage - falling back to the screen-percentage "
                        "cut\n"));
                    m_screen_pct.engage();
                }
            }
            else
            {
                m_screen_pct.engage();
            }

            // A valid new generation starts from an empty timeline.  This
            // is intentionally after every guard and after frame-cap
            // engagement, so a failed start never destroys the timeline.
            drop_ring();
            m_timeline_seek_data_valid.store(false,
                                             std::memory_order_release);
            m_timeline_context_valid.store(false,
                                           std::memory_order_release);
            m_last_seek_target.store(-1, std::memory_order_release);
            m_usable_latest_seq.store(-1, std::memory_order_release);
            log_sc6_context_report(
                "GEN_START",
                resolve_sc6_replay_seek_context_report(),
                true);
            {
                const auto time_report =
                    ReplayScrubDiag::resolve_demo_time_source_report(true);
                ReplayScrubDiag::log_demo_time_source_report_once(
                    "GEN_START", time_report);
                const auto driver_report =
                    ReplayScrubDiag::resolve_demo_net_driver_report(true);
                ReplayScrubDiag::log_demo_driver_resolve_report_once(
                    "GEN_START", driver_report);
            }

            const int32_t m = read_engine_master_clock();
            m_timeline_gen_start_master.store(m, std::memory_order_release);
            m_timeline_gen_last_master.store(m, std::memory_order_release);
            m_gen_last_round = read_current_round();
            m_gen_max_round  = m_gen_last_round;
            m_gen_seen_progress       = false;
            m_gen_final_round_played  = false;
            m_gen_seen_playing_back   = false;
            m_gen_playback_gone_ticks = 0;
            m_gen_match_undecided_seen = false;
            m_gen_total_rounds        = -1;
            m_gen_final_round_first_safe_seq = -1;
            m_gen_final_round_last_safe_seq  = -1;
            reset_playback_result_guard_markers();
            m_gen_missing_demo_time_logged = false;
            m_gen_demo_time_recovered_logged = false;
            m_gen_capture_fail_count = 0;
            m_last_extras_failure = "none";
            const auto now = std::chrono::steady_clock::now();
            m_gen_started_at   = now;
            m_gen_finished_at  = now;
            m_gen_last_advance = now;
            reset_generation_profile();
            m_timeline_gen_state.store(
                static_cast<int>(TimelineGenState::Generating),
                std::memory_order_release);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] Generate timeline STARTED ({}) - frame cap "
                "removed; replay fast-forwarding (master={} clean_start=1 "
                "round={} input_master={} battle_master={} "
                "seek_context_ok={} reset_source_ok={} source={} "
                "live_bm_reset_ok={})\n"),
                RC::to_generic_string(experimental
                    ? "experimental: render skipped" : "normal"), m,
                ready.round, ready.input_master, ready.battle_master,
                ready.seek_context_ok ? 1 : 0,
                ready.reset_source_ok ? 1 : 0,
                RC::to_generic_string(sc6_context_source_name(
                    ready.seek_context_source)),
                ready.live_bm_reset_ok ? 1 : 0);
            {
                ReplayTraceFields f;
                f.string("mode", experimental ? "experimental" : "normal")
                 .integer("round", ready.round)
                 .integer("input_master", ready.input_master)
                 .integer("battle_master", ready.battle_master)
                 .integer("start_master", m)
                 .string("context_source", sc6_context_source_name(
                             ready.seek_context_source))
                 .boolean("seek_context_ok", ready.seek_context_ok)
                 .boolean("reset_source_ok", ready.reset_source_ok)
                 .boolean("live_bm_reset_ok", ready.live_bm_reset_ok)
                 .hex("bm", ready.battle_manager)
                 .hex("input_log", ready.input_log)
                 .hex("replay_player", ready.replay_player)
                 .hex("state_reset_data", ready.state_reset_data);
                ReplayDebugTrace::instance().event("generate_start", f);
            }
        }

        void start_generate_timeline_battle_step() noexcept
        {
            if (m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating))
                return;
            if (is_timeline_generation_locked_complete())
            {
                log_generate_locked_complete_once();
                return;
            }

            if (!is_initialized())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline (battle-step) "
                    "ignored - snapshot ring not initialised yet\n"));
                return;
            }
            if (GameMode::instance().current_presence()
                != GamePresence::Replay)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline (battle-step) "
                    "ignored - not in the Replay viewer\n"));
                return;
            }
            if (!m_exec_write || !m_exec_read)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline (battle-step) "
                    "failed - required native entry points unresolved\n"));
                return;
            }
            if (!m_per_frame_tick_bypass)
            {
                if (!resolve_per_frame_tick_bypass())
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] Generate timeline (battle-step) "
                        "failed - could not prepare direct-step bypass\n"));
                        return;
                }
            }
            if (!ensure_exp2_buffers())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline (battle-step) "
                    "failed - EXP2 buffers unavailable\n"));
                return;
            }
            if (!charas_alive())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline (battle-step) "
                    "ignored - battle charas are not both alive\n"));
                return;
            }
            const GenerateStartReadiness ready =
                read_generate_start_readiness();
            if (!ready.clean_start)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline (battle-step) blocked "
                    "- replay is not at start (reason={} context_ok={} "
                    "round={} input_master={} battle_master={} source={} "
                    "object_bm=0x{:X} object_il=0x{:X} wmp_bm=0x{:X} "
                    "wmp_il=0x{:X} chosen_bm=0x{:X} chosen_il=0x{:X} "
                    "seek_context_ok={} sub=0x{:X} rp=0x{:X} "
                    "reset=0x{:X} live_bm_reset_ok={})\n"),
                    RC::to_generic_string(ready.reason),
                    ready.context_ok ? 1 : 0, ready.round,
                    ready.input_master, ready.battle_master,
                    RC::to_generic_string(sc6_context_source_name(
                        ready.seek_context_source)),
                    ready.object_registry_battle_manager,
                    ready.object_registry_input_log,
                    ready.world_mode_pump_battle_manager,
                    ready.world_mode_pump_input_log,
                    ready.battle_manager, ready.input_log,
                    ready.seek_context_ok ? 1 : 0, ready.sub_driver,
                    ready.replay_player, ready.state_reset_data,
                    ready.live_bm_reset_ok ? 1 : 0);
                return;
            }
            if (!ready.seek_context_ok || !ready.reset_source_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] Generate timeline (battle-step) "
                    "blocked - exact SC6 reset source is not ready "
                    "failure={} source={} context_ok={} "
                    "seek_context_ok={} round={} input_master={} "
                    "battle_master={} object_bm=0x{:X} "
                    "object_il=0x{:X} wmp_bm=0x{:X} wmp_il=0x{:X} "
                    "chosen_bm=0x{:X} chosen_il=0x{:X} sub=0x{:X} "
                    "rp=0x{:X} reset=0x{:X} irs=0x{:X} "
                    "live_bm_reset_ok={} state_reset_data_ok={}\n"),
                    RC::to_generic_string(sc6_context_failure_name(
                        ready.seek_context_failure)),
                    RC::to_generic_string(sc6_context_source_name(
                        ready.seek_context_source)),
                    ready.context_ok ? 1 : 0,
                    ready.seek_context_ok ? 1 : 0, ready.round,
                    ready.input_master, ready.battle_master,
                    ready.object_registry_battle_manager,
                    ready.object_registry_input_log,
                    ready.world_mode_pump_battle_manager,
                    ready.world_mode_pump_input_log,
                    ready.battle_manager, ready.input_log,
                    ready.sub_driver, ready.replay_player,
                    ready.state_reset_data, ready.interactive_replay,
                    ready.live_bm_reset_ok ? 1 : 0,
                    ready.state_reset_data_ok ? 1 : 0);
                return;
            }

            m_paused.store(false, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::None),
                std::memory_order_release);
            cancel_sc6_exact_seek("generation start");
            m_sc6_seek_job = Sc6ExactSeekJob{};
            m_frame_cap.disengage();
            m_screen_pct.disengage();
            m_render_skip.disengage();

            m_gen_mode.store(
                static_cast<int>(BattleStepMode::DirectPerFrame),
                std::memory_order_release);
            m_gen_battle_step_probe.store(false,
                                           std::memory_order_release);
            m_gen_battle_step_generate.store(true,
                                            std::memory_order_release);
            m_exp2_transient_fail_count = 0;
            m_gen_capture_fail_count = 0;
            m_last_extras_failure = "none";

            drop_ring();
            m_timeline_seek_data_valid.store(false,
                                             std::memory_order_release);
            m_timeline_context_valid.store(false,
                                           std::memory_order_release);
            m_last_seek_target.store(-1, std::memory_order_release);
            m_usable_latest_seq.store(-1, std::memory_order_release);
            log_sc6_context_report(
                "GEN_START_BATTLE_STEP",
                resolve_sc6_replay_seek_context_report(),
                true);
            {
                const auto time_report =
                    ReplayScrubDiag::resolve_demo_time_source_report(true);
                ReplayScrubDiag::log_demo_time_source_report_once(
                    "GEN_START_BATTLE_STEP", time_report);
                const auto report =
                    ReplayScrubDiag::resolve_demo_net_driver_report(true);
                ReplayScrubDiag::log_demo_driver_resolve_report_once(
                    "GEN_START_BATTLE_STEP", report);
            }

            const int32_t m = read_engine_master_clock();
            m_timeline_gen_start_master.store(m, std::memory_order_release);
            m_timeline_gen_last_master.store(m, std::memory_order_release);
            m_gen_last_round = read_current_round();
            m_gen_max_round  = m_gen_last_round;
            m_gen_seen_progress       = false;
            m_gen_final_round_played  = false;
            m_gen_seen_playing_back   = false;
            m_gen_playback_gone_ticks = 0;
            m_gen_match_undecided_seen = false;
            m_gen_total_rounds        = -1;
            m_gen_final_round_first_safe_seq = -1;
            m_gen_final_round_last_safe_seq  = -1;
            reset_playback_result_guard_markers();
            m_gen_missing_demo_time_logged = false;
            m_gen_demo_time_recovered_logged = false;
            const auto now = std::chrono::steady_clock::now();
            m_gen_started_at   = now;
            m_gen_finished_at  = now;
            m_gen_last_advance = now;
            reset_generation_profile();
            m_have_last_counter = false;
            m_timeline_gen_state.store(
                static_cast<int>(TimelineGenState::Generating),
                std::memory_order_release);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] Generate timeline STARTED (experimental "
                "battle-step path) - direct PerFrameTick stepping "
                "(clean_start=1 round={} input_master={} battle_master={} "
                "seek_context_ok={} reset_source_ok={} source={} "
                "live_bm_reset_ok={})\n"),
                ready.round, ready.input_master, ready.battle_master,
                ready.seek_context_ok ? 1 : 0,
                ready.reset_source_ok ? 1 : 0,
                RC::to_generic_string(sc6_context_source_name(
                    ready.seek_context_source)),
                ready.live_bm_reset_ok ? 1 : 0);
            {
                ReplayTraceFields f;
                f.string("mode", "battle_step")
                 .integer("round", ready.round)
                 .integer("input_master", ready.input_master)
                 .integer("battle_master", ready.battle_master)
                 .integer("start_master", m)
                 .string("context_source", sc6_context_source_name(
                             ready.seek_context_source))
                 .boolean("seek_context_ok", ready.seek_context_ok)
                 .boolean("reset_source_ok", ready.reset_source_ok)
                 .boolean("live_bm_reset_ok", ready.live_bm_reset_ok)
                 .hex("bm", ready.battle_manager)
                 .hex("input_log", ready.input_log)
                 .hex("replay_player", ready.replay_player)
                 .hex("state_reset_data", ready.state_reset_data);
                ReplayDebugTrace::instance().event("generate_start", f);
            }
        }

        bool validate_generated_timeline_seek_data() noexcept
        {
            const size_t count = m_tags.count();
            if (count == 0)
            {
                m_timeline_seek_data_valid.store(
                    false, std::memory_order_release);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.capture] invalid complete timeline: "
                    "no committed frames\n"));
                return false;
            }

            std::array<bool, kMaxSc6ReplayRounds> seen{};
            std::array<int32_t, kMaxSc6ReplayRounds> first_seq{};
            std::array<int32_t, kMaxSc6ReplayRounds> first_master{};
            std::array<int32_t, kMaxSc6ReplayRounds> last_seq{};
            std::array<int32_t, kMaxSc6ReplayRounds> last_master{};
            first_seq.fill(-1);
            first_master.fill(-1);
            last_seq.fill(-1);
            last_master.fill(-1);

            bool ok = true;
            int32_t prev_seq = -1;
            bool oracle_ok = !m_oracle_capture_failed.load(
                std::memory_order_acquire)
                && m_oracle_frames.size() >= count;
            int32_t first_bad_oracle_seq = -1;
            for (size_t i = 0; i < count; ++i)
            {
                int32_t seq = -1, round = -1, frame = -1, master = -1;
                if (!m_tags.get(i, seq, round, frame, master))
                {
                    ok = false;
                    continue;
                }
                if (seq != static_cast<int32_t>(i)
                    || seq <= prev_seq
                    || round < 0
                    || round >= kMaxSc6ReplayRounds
                    || master < 0)
                {
                    ok = false;
                }
                else
                {
                    const size_t r = static_cast<size_t>(round);
                    if (!seen[r])
                    {
                        seen[r] = true;
                        first_seq[r] = seq;
                        first_master[r] = master;
                    }
                    else if (master < last_master[r])
                    {
                        ok = false;
                    }
                    last_seq[r] = seq;
                    last_master[r] = master;
                }
                prev_seq = seq;

                bool this_oracle_ok = false;
                if (i < m_oracle_frames.size())
                {
                    const ReplayFrameOracleSnap& oracle =
                        m_oracle_frames[i];
                    this_oracle_ok = oracle.valid
                        && oracle.seq == seq
                        && oracle.round == round
                        && oracle.master == master;
                }
                if (!this_oracle_ok)
                {
                    oracle_ok = false;
                    if (first_bad_oracle_seq < 0)
                        first_bad_oracle_seq = static_cast<int32_t>(i);
                }
            }
            if (!oracle_ok)
                ok = false;

            int32_t first_timeline_seq = -1, first_round = -1;
            int32_t first_frame = -1, first_timeline_master = -1;
            int32_t last_timeline_seq = -1, last_round = -1;
            int32_t last_frame = -1, last_timeline_master = -1;
            (void)m_tags.get(0, first_timeline_seq, first_round,
                             first_frame, first_timeline_master);
            (void)m_tags.get(count - 1, last_timeline_seq, last_round,
                             last_frame, last_timeline_master);

            int32_t round_count = 0;
            for (int32_t r = 0; r < kMaxSc6ReplayRounds; ++r)
            {
                if (!seen[static_cast<size_t>(r)]) continue;
                ++round_count;
                const bool reset_ok =
                    has_captured_round_reset_snapshot(r);
                bool il_origin_ok = false;
                bool rdb_origin_ok = false;
                const int32_t origin_seq =
                    m_sc6_round_reset_snapshot_seq[
                        static_cast<size_t>(r)];
                if (origin_seq >= 0
                    && static_cast<size_t>(origin_seq) < count)
                {
                    il_origin_ok =
                        m_il_store.gather(
                            static_cast<size_t>(origin_seq)) != nullptr;
                    rdb_origin_ok =
                        m_rdb_store.gather(
                            static_cast<size_t>(origin_seq)) != nullptr;
                }
                if (!reset_ok || !il_origin_ok || !rdb_origin_ok)
                    ok = false;

                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.capture] round={} first_seq={} "
                    "first_master={} last_seq={} last_master={} "
                    "last_safe_seq={} first_result_seq={} "
                    "reset_snapshot={} reset_origin_seq={} "
                    "reset_origin_master={} il_origin={} rdb_origin={}\n"),
                    r,
                    first_seq[static_cast<size_t>(r)],
                    first_master[static_cast<size_t>(r)],
                    last_seq[static_cast<size_t>(r)],
                    last_master[static_cast<size_t>(r)],
                    m_playback_round_last_safe_seq[
                        static_cast<size_t>(r)],
                    m_playback_round_first_result_seq[
                        static_cast<size_t>(r)],
                    reset_ok ? 1 : 0, origin_seq,
                    m_sc6_round_reset_snapshot_master[
                        static_cast<size_t>(r)],
                    il_origin_ok ? 1 : 0, rdb_origin_ok ? 1 : 0);
                {
                    ReplayTraceFields f;
                    f.integer("round", r)
                     .integer("first_seq", first_seq[static_cast<size_t>(r)])
                     .integer("first_master",
                              first_master[static_cast<size_t>(r)])
                     .integer("last_seq", last_seq[static_cast<size_t>(r)])
                     .integer("last_master",
                              last_master[static_cast<size_t>(r)])
                     .integer("last_safe_seq",
                              m_playback_round_last_safe_seq[
                                  static_cast<size_t>(r)])
                     .integer("first_result_seq",
                              m_playback_round_first_result_seq[
                                  static_cast<size_t>(r)])
                     .boolean("reset_snapshot", reset_ok)
                     .integer("reset_origin_seq", origin_seq)
                     .integer("reset_origin_master",
                              m_sc6_round_reset_snapshot_master[
                                  static_cast<size_t>(r)])
                     .boolean("il_origin", il_origin_ok)
                     .boolean("rdb_origin", rdb_origin_ok);
                    ReplayDebugTrace::instance().event(
                        "capture_round_summary", f);
                }
            }

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.capture] complete frames={} rounds={} "
                "first_seq={} first_master={} last_seq={} "
                "last_master={} oracle={} first_bad_oracle_seq={} "
                "seek_data_valid={}\n"),
                count, round_count, first_timeline_seq,
                first_timeline_master, last_timeline_seq,
                last_timeline_master, oracle_ok ? 1 : 0,
                first_bad_oracle_seq, ok ? 1 : 0);
            {
                ReplayTraceFields f;
                f.uinteger("frames", count)
                 .integer("rounds", round_count)
                 .integer("first_seq", first_timeline_seq)
                 .integer("first_master", first_timeline_master)
                 .integer("last_seq", last_timeline_seq)
                 .integer("last_master", last_timeline_master)
                 .boolean("oracle_ok", oracle_ok)
                 .integer("first_bad_oracle_seq", first_bad_oracle_seq)
                 .boolean("integrity_ok", ok);
                ReplayDebugTrace::instance().event("generate_complete", f);
                f.boolean("seekable", ok)
                 .boolean("oracle_records", oracle_ok)
                 .boolean("reset_snapshots_required", true)
                 .boolean("same_round_restore_probe_sample", false)
                 .boolean("cross_round_reset_context", true)
                 .string("reason", ok ? "ok"
                                      : "timeline-prerequisite-missing");
                ReplayDebugTrace::instance().event(
                    "generate_seekability_summary", f);
            }

            m_timeline_seek_data_valid.store(
                ok, std::memory_order_release);
            m_timeline_context_valid.store(
                ok, std::memory_order_release);
            if (!ok)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::RoundResetDataUnavailable);
                publish_mode(ScrubMode::NativeSeekFailed);
            }
            return ok;
        }

        // [game thread] End "Generate timeline": restore the frame cap
        // and set the end state.  reached_end=true -> Done (the timeline
        // covers a full pass); false -> Idle (cancelled before the end).
        void stop_generate_timeline(const char* reason,
                                    bool reached_end) noexcept
        {
            m_gen_battle_step_generate.store(false,
                                             std::memory_order_release);
            m_gen_battle_step_probe.store(false,
                                          std::memory_order_release);
            m_exp2_transient_fail_count = 0;
            m_gen_mode.store(static_cast<int>(BattleStepMode::None),
                             std::memory_order_release);
            const bool was_generating =
                m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating);

            m_frame_cap.disengage();
            m_screen_pct.disengage();
            m_render_skip.disengage();
            m_gen_finished_at = std::chrono::steady_clock::now();
            m_timeline_gen_state.store(
                static_cast<int>(reached_end ? TimelineGenState::Done
                                             : TimelineGenState::Idle),
                std::memory_order_release);
            if (!reached_end)
            {
                m_timeline_seek_data_valid.store(
                    false, std::memory_order_release);
                m_timeline_context_valid.store(false,
                                               std::memory_order_release);
                publish_native_status(NativeSeekStatus::Failed,
                                      NativeSeekFailure::TimelineIncomplete);
                publish_mode(ScrubMode::NativeSeekFailed);
            }

            // Completed generation parks the Replay UI target and holds
            // the replay with the light world/replay-clock gates only.
            // Broad actor/time/VM gates caused post-generation crawl, but
            // a pure UI park let the replay keep running into the menu.
            // Play remains disabled until a validated seek lands.  Queue
            // that landing seek for the safe park frame below; otherwise
            // completion leaves the UI at NotLanded forever and no Play /
            // Go control can unlock.  Only on reached_end: a user-
            // requested Stop, or an abnormal safety-timeout / left-replay,
            // must not change live replay state.
            if (was_generating && reached_end)
            {
                m_paused.store(true, std::memory_order_release);
                m_hold_kind.store(
                    static_cast<int32_t>(
                        ReplayScrubHoldKind::RestoredFrameHold),
                    std::memory_order_release);
                m_timeline_context_valid.store(true,
                                               std::memory_order_release);
                m_ui_park_no_gate_check_ticks.store(
                    0, std::memory_order_release);
                const bool seek_data_valid =
                    validate_generated_timeline_seek_data();

                int32_t park_seq = -1;
                if (m_gen_final_round_last_safe_seq >= 0)
                {
                    park_seq = m_gen_final_round_last_safe_seq
                               - kPostResultParkBackoffFrames;
                    if (park_seq < m_gen_final_round_first_safe_seq)
                        park_seq = m_gen_final_round_first_safe_seq;
                }
                if (park_seq < 0)
                    park_seq = raw_latest_seq();

                if (park_seq >= 0)
                {
                    m_usable_latest_seq.store(park_seq,
                                              std::memory_order_release);
                    if (seek_data_valid)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[ReplayScrub] Generate timeline parked UI at "
                            "seq={} (first_safe={} last_safe={}); native "
                            "seek queued\n"),
                            park_seq, m_gen_final_round_first_safe_seq,
                            m_gen_final_round_last_safe_seq);
                    }
                    else
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub] Generate timeline parked UI at "
                            "seq={} but seek data is invalid; releasing "
                            "replay hold and keeping Play blocked\n"),
                            park_seq);
                    }
                    if (seek_data_valid)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[ReplayScrub] Generate timeline parked with "
                            "lightweight replay hold; actor/time/VM freeze "
                            "gates disabled\n"));
                    }
                    publish_ui_target(park_seq);
                    if (seek_data_valid)
                    {
                        m_hold_kind.store(
                            static_cast<int32_t>(
                                ReplayScrubHoldKind::ValidationStep),
                            std::memory_order_release);
                        ++m_seek_generation;
                        m_native.generation = m_seek_generation;
                        m_native.requested_seq = park_seq;
                        m_native.adjusted_seq = -1;
                        m_native.failure = NativeSeekFailure::None;
                        m_native.direct_driver_available = false;
                        m_native.cvar_submitted = false;
                        publish_native_status(NativeSeekStatus::Queued,
                                              NativeSeekFailure::None);
                        publish_mode(ScrubMode::PausedPreview);
                        publish_preview_status(PreviewStatus::SkippedUnsafe);
                        (void)queue_sc6_exact_seek(park_seq, "GEN_PARK");
                    }
                    else
                    {
                        m_paused.store(false, std::memory_order_release);
                        m_hold_kind.store(
                            static_cast<int32_t>(
                                ReplayScrubHoldKind::None),
                            std::memory_order_release);
                        publish_native_status(
                            NativeSeekStatus::Failed,
                            NativeSeekFailure::RoundResetDataUnavailable);
                        publish_mode(ScrubMode::NativeSeekFailed);
                    }
                    write_last_round_result(0);
                }
                else
                {
                    m_paused.store(false, std::memory_order_release);
                    m_hold_kind.store(
                        static_cast<int32_t>(ReplayScrubHoldKind::None),
                        std::memory_order_release);
                }
            }

            if (was_generating)
            {
                const int32_t start = m_timeline_gen_start_master.load(
                    std::memory_order_acquire);
                const int32_t last = m_timeline_gen_last_master.load(
                    std::memory_order_acquire);
                {
                    ReplayTraceFields f;
                    f.string("reason", reason ? reason : "?")
                     .boolean("reached_end", reached_end)
                     .integer("start_master", start)
                     .integer("last_master", last)
                     .integer("advanced_frames",
                              (last >= start) ? (last - start) : 0)
                     .uinteger("ring_count", ring_count())
                     .boolean("timeline_seek_data_valid",
                              m_timeline_seek_data_valid.load(
                                  std::memory_order_acquire))
                     .boolean("timeline_context_valid",
                              m_timeline_context_valid.load(
                                  std::memory_order_acquire));
                    ReplayDebugTrace::instance().event(
                        reached_end ? "generate_stopped_complete"
                                    : "generate_stopped_incomplete", f);
                }
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] Generate timeline {} ({}) - advanced "
                    "{} replay frames; ring now holds {}\n"),
                    RC::to_generic_string(
                        reached_end ? "COMPLETE" : "stopped"),
                    RC::to_generic_string(reason ? reason : "?"),
                    (last >= start) ? (last - start) : 0,
                    ring_count());
                log_generation_profile(reason);
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
                start_generate_timeline(false);
            else if (req == kGenReqStartExperimental)
                start_generate_timeline(true);
            else if (req == kGenReqStartBattleStep)
                start_generate_timeline_battle_step();
            else if (req == kGenReqBattleStepProbe)
                run_battle_step_probe();
            else if (req == kGenReqStop)
                stop_generate_timeline("user", false);

            service_armed_generate_start();

            const bool generating =
                m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating);

            if (!generating)
            {
                // The frame cap must never stay removed while we are not
                // actively generating.  If any path cleared the gen
                // state without disengaging, fix it here so a single
                // cockpit tick always restores SC6's 60fps cap.
                if (m_frame_cap.is_engaged() || m_screen_pct.is_engaged()
                    || m_render_skip.is_engaged())
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] frame cap / screen-pct / render-skip "
                        "engaged outside generation - restoring\n"));
                    m_frame_cap.disengage();
                    m_screen_pct.disengage();
                    m_render_skip.disengage();
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

            // Memory ceiling reached: capture_snapshot has stopped folding
            // new snapshots into the dedup store (2 GB cap), so the
            // timeline is as complete as it can get - stop generation.
            // For a normal-length replay this never fires; the loop /
            // end-of-recording checks below are what stop generation when
            // the replay reaches its natural end.
            if (m_capture_ceiling_hit.load(std::memory_order_acquire))
            {
                stop_generate_timeline("memory-ceiling", true);
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
            const int32_t last_round_result = read_last_round_result();
            const int32_t is_playing_back   = read_replay_is_playing_back();
            update_playback_result_guard_markers(
                round, master, last_round_result);

            // nTotalRounds, latched: read_total_rounds() returns -1 while
            // the ReplayPlayer is briefly unresolvable (common right
            // after a scene transition).  Latching the first sane reading
            // keeps a transient -1 from dropping in_final_round mid-match
            // and letting generation run on into the menu.
            const int32_t tr_now = read_total_rounds();
            if (tr_now > 0) m_gen_total_rounds = tr_now;

            // In the FINAL round of the match?  CurrentRound is 0-based
            // and nTotalRounds is the recorded round count, so the last
            // round is CurrentRound + 1 >= nTotalRounds (matches the
            // game's own execIsExistNextRound predicate).  Gated on
            // m_gen_seen_progress so a generation started on a frozen
            // intro doesn't act on a not-yet-valid round index.
            const bool in_final_round =
                (m_gen_seen_progress && m_gen_total_rounds > 0
                 && round >= 0 && round + 1 >= m_gen_total_rounds);

            // Match decided -> stop.  g_LuxBattle_LastRoundResultType is 0
            // while a round is live and goes non-zero the instant
            // LuxBattle_EvaluateRoundResult scores a round end.  Once the
            // FINAL round has been seen live (m_gen_final_round_played),
            // its result going non-zero IS the match being won - stop
            // right there, before the post-KO cinematic and long before
            // the replay viewer exits to a menu.  The
            // m_gen_final_round_played latch keeps a stale result code
            // carried in from the previous round's end from false-firing
            // at the final round's very first ticks.
            if (in_final_round && last_round_result == 0)
            {
                m_gen_final_round_played = true;
                if (master >= kRoundBoundarySeekGuardMaster)
                {
                    const int32_t safe_seq = raw_latest_seq();
                    if (safe_seq >= 0)
                    {
                        if (m_gen_final_round_first_safe_seq < 0)
                            m_gen_final_round_first_safe_seq = safe_seq;
                        m_gen_final_round_last_safe_seq = safe_seq;
                    }
                }
            }
            if (m_gen_final_round_played && last_round_result != 0)
            {
                stop_generate_timeline("match-ended", true);
                return;
            }

            if (m_gen_battle_step_generate.load(std::memory_order_acquire))
            {
                run_battle_step_generate_slice();
                return;
            }

            // Match decided (ReplayPlayer-independent cross-check): a
            // player's round-win count reached the rounds-needed-to-win
            // threshold.  Catches the match end even when the ReplayPlayer
            // round-index signal above is unavailable.
            //
            // Latched against m_gen_match_undecided_seen: the win counts
            // live in the chara objects, and if a replay reuses a chara
            // slot a stale "already decided" count could be present at
            // generation start.  Only a false -> true TRANSITION (the
            // match observed undecided, then decided) is trusted.
            {
                const bool decided = match_decided();
                if (m_gen_seen_progress && !decided)
                    m_gen_match_undecided_seen = true;
                if (m_gen_match_undecided_seen && decided)
                {
                    stop_generate_timeline("match-decided", true);
                    return;
                }
            }

            // Backstop: the engine ended replay playback - ALuxBattle-
            // ReplayPlayer.bIsPlayingBack went 1 -> 0 and stayed 0 for
            // kGenPlaybackGoneTicks ticks.  The debounce means a
            // momentary mid-match dip can't false-stop; only a sustained
            // 0 (the replay genuinely finished) trips it.  Catches
            // replays that end without a normal final-round result.
            // is_playing_back == -1 means the actor was momentarily
            // unresolvable - hold the counter rather than count it.
            if (is_playing_back == 1)
            {
                m_gen_seen_playing_back   = true;
                m_gen_playback_gone_ticks = 0;
            }
            else if (is_playing_back == 0 && m_gen_seen_playing_back)
            {
                ++m_gen_playback_gone_ticks;
            }
            if (m_gen_playback_gone_ticks > kGenPlaybackGoneTicks)
            {
                stop_generate_timeline("playback-ended", true);
                return;
            }

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
            {
                m_gen_last_advance  = now;
                m_gen_seen_progress = true;
            }

            if (master >= 0)
                m_timeline_gen_last_master.store(
                    master, std::memory_order_release);
            if (round >= 0)
                m_gen_last_round = round;

            // End-of-recording: neither clock advanced for the stall
            // window (the replay stopped feeding frames).  A momentarily-
            // unreadable tick (master/round = -1) just fails to refresh
            // the timer rather than stopping outright, so one transient
            // bad read can't trigger a false "Done".
            //
            // This is a BACKSTOP to the match-ended / playback-ended
            // checks above (which fire on a state change, not a stall).
            // In the final round a shorter window is used - a stall there
            // can only be the match ending - while outside the final
            // round the long window stands so a legitimate round-
            // transition stall never false-stops.
            const double stuck_limit = in_final_round
                ? kGenFinalRoundStuckSeconds : kGenStuckSeconds;
            if (std::chrono::duration<double>(now - m_gen_last_advance)
                    .count() > stuck_limit)
            {
                stop_generate_timeline(
                    in_final_round ? "match-ended" : "end-of-recording",
                    true);
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

        int32_t clamp_seq_to_timeline(int32_t seq) const noexcept
        {
            const int32_t earliest = earliest_seq();
            const int32_t latest = latest_seq();
            if (earliest < 0 || latest < 0) return -1;
            if (seq < earliest) return earliest;
            if (seq > latest) return latest;
            return seq;
        }

        void ui_begin_drag() noexcept
        {
            clear_playback_result_guard_override();
            m_paused.store(true, std::memory_order_release);
            const bool keep_replay_held =
                has_context_valid_completed_timeline();
            m_hold_kind.store(
                static_cast<int32_t>(
                    keep_replay_held ? ReplayScrubHoldKind::RestoredFrameHold
                                     : ReplayScrubHoldKind::UiParkOnly),
                std::memory_order_release);
            m_resume_after_seek.store(false, std::memory_order_release);
            m_ui_dragging.store(true, std::memory_order_release);
            m_drag_preview_seq.store(
                m_ui_requested_seq.load(std::memory_order_acquire),
                std::memory_order_release);
            m_last_drag_preview_seq.store(-1, std::memory_order_release);
            m_ui_wants_play.store(false, std::memory_order_release);
            publish_mode(ScrubMode::Dragging);
        }

        void ui_drag_to_seq(int32_t seq) noexcept
        {
            seq = clamp_seq_to_timeline(seq);
            if (seq < 0) return;
            publish_ui_target(seq);
            m_drag_preview_seq.store(seq, std::memory_order_release);
            publish_native_failure_reason(NativeSeekFailure::None);
        }

        void ui_end_drag() noexcept
        {
            m_ui_dragging.store(false, std::memory_order_release);
            m_drag_preview_seq.store(-1, std::memory_order_release);
            m_last_drag_preview_seq.store(-1, std::memory_order_release);
            m_ui_wants_play.store(
                m_auto_resume_on_release.load(std::memory_order_acquire),
                std::memory_order_release);
            const int32_t seq = clamp_seq_to_timeline(
                m_ui_requested_seq.load(std::memory_order_acquire));
            if (seq >= 0 && has_context_valid_completed_timeline())
                post_seek_command(SeekCommandKind::RequestPreviewAndNativeSeek,
                                  seq);
            else if (seq >= 0)
            {
                m_ui_wants_play.store(false, std::memory_order_release);
                publish_native_status(NativeSeekStatus::Failed,
                                      NativeSeekFailure::TimelineIncomplete);
                publish_mode(ScrubMode::NativeSeekFailed);
            }
        }

        void ui_step_to_seq(int32_t seq) noexcept
        {
            seq = clamp_seq_to_timeline(seq);
            if (seq < 0) return;
            clear_playback_result_guard_override();
            m_paused.store(true, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::ValidationStep),
                std::memory_order_release);
            m_ui_dragging.store(false, std::memory_order_release);
            m_ui_wants_play.store(false, std::memory_order_release);
            publish_ui_target(seq);
            if (has_context_valid_completed_timeline())
                post_seek_command(SeekCommandKind::StepToSeq, seq);
            else
            {
                publish_native_status(NativeSeekStatus::Failed,
                                      NativeSeekFailure::TimelineIncomplete);
                publish_mode(ScrubMode::NativeSeekFailed);
            }
        }

        void ui_pause_at_live() noexcept
        {
            const int32_t live_round = read_current_round();
            const int32_t live_master =
                m_live_master_cached.load(std::memory_order_acquire);
            const int32_t exact_seq =
                find_slot_for_round_master(live_round, live_master);
            int32_t seq = exact_seq;
            if (seq < 0) seq = latest_seq();
            seq = clamp_seq_to_timeline(seq);
            if (seq >= 0) publish_ui_target(seq);
            if (exact_seq >= 0 && seq == exact_seq)
            {
                m_last_seek_target.store(seq, std::memory_order_release);
                if (live_master >= 0)
                    m_last_seek_master_tag.store(
                        live_master, std::memory_order_release);
                publish_native_status(NativeSeekStatus::Landed);
            }
            else
            {
                m_last_seek_target.store(-1, std::memory_order_release);
                m_last_seek_master_tag.store(-1, std::memory_order_release);
                publish_native_status(NativeSeekStatus::Failed,
                                      NativeSeekFailure::NotLanded);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] pause-at-live could not map live "
                    "round/master to an exact captured seq; Play remains "
                    "blocked (round={} master={} fallback_seq={})\n"),
                    live_round, live_master, seq);
            }
            m_paused.store(true, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::RestoredFrameHold),
                std::memory_order_release);
            m_ui_dragging.store(false, std::memory_order_release);
            m_ui_wants_play.store(false, std::memory_order_release);
            publish_mode(ScrubMode::PausedPreview);
        }

        void ui_request_play() noexcept
        {
            arm_playback_result_guard_override_if_needed();
            m_ui_wants_play.store(false, std::memory_order_release);
            if (!has_context_valid_completed_timeline())
            {
                publish_native_status(NativeSeekStatus::Failed,
                                      NativeSeekFailure::TimelineIncomplete);
                publish_mode(ScrubMode::NativeSeekFailed);
                return;
            }
            post_seek_command(SeekCommandKind::PlayFromSelected,
                              m_ui_requested_seq.load(
                                  std::memory_order_acquire));
        }

        void ui_cancel() noexcept
        {
            post_seek_command(SeekCommandKind::Cancel, -1);
        }

        ReplayTimelineView timeline_view() const noexcept
        {
            ReplayTimelineView v{};
            v.earliest_seq = earliest_seq();
            v.latest_seq = latest_seq();
            v.displayed_seq = clamp_seq_to_timeline(
                m_ui_displayed_seq.load(std::memory_order_acquire));
            if (v.displayed_seq < 0)
                v.displayed_seq = current_play_position();
            v.requested_seq = clamp_seq_to_timeline(
                m_ui_requested_seq.load(std::memory_order_acquire));
            v.landed_seq = clamp_seq_to_timeline(
                m_last_seek_target.load(std::memory_order_acquire));
            v.mode = static_cast<ScrubMode>(
                m_scrub_mode.load(std::memory_order_acquire));
            v.native_status = static_cast<NativeSeekStatus>(
                m_native_status.load(std::memory_order_acquire));
            v.block_reason = static_cast<NativeSeekFailure>(
                m_play_block_reason.load(std::memory_order_acquire));
            v.paused = is_paused();
            v.can_play = can_play_from_selected();
            v.native_pending = has_pending_native_seek()
                || v.native_status == NativeSeekStatus::Queued
                || v.native_status == NativeSeekStatus::DeferredBusy
                || v.native_status == NativeSeekStatus::Submitted
                || v.native_status == NativeSeekStatus::Settling;
            v.preview_applied =
                static_cast<PreviewStatus>(
                    m_preview_status.load(std::memory_order_acquire))
                == PreviewStatus::Applied;
            return v;
        }

        bool can_play_from_selected() const noexcept
        {
            const int32_t requested =
                m_ui_requested_seq.load(std::memory_order_acquire);
            const int32_t landed =
                m_last_seek_target.load(std::memory_order_acquire);
            return requested >= 0
                && has_context_valid_completed_timeline()
                && m_timeline_seek_data_valid.load(
                    std::memory_order_acquire)
                && landed == requested
                && static_cast<NativeSeekStatus>(
                       m_native_status.load(std::memory_order_acquire))
                    == NativeSeekStatus::Landed
                && static_cast<NativeSeekFailure>(
                       m_play_block_reason.load(std::memory_order_acquire))
                    == NativeSeekFailure::None;
        }

        NativeSeekFailure play_block_reason() const noexcept
        {
            return static_cast<NativeSeekFailure>(
                m_play_block_reason.load(std::memory_order_acquire));
        }

        // Request a seek to timeline position `target_seq` (a capture
        // sequence - see the m_tags doc).  Doesn't actually seek until the
        // next cockpit tick (see service_seek_request); the UI thread
        // just posts the request via this atomic.
        void request_seek(int32_t target_seq) noexcept
        {
            ui_step_to_seq(target_seq);
        }

        // Cancel any pause/scrub state - world resumes at the next
        // cockpit tick.  Equivalent to clicking Play.
        void cancel_scrub() noexcept
        {
            clear_scrub_state();
            m_resume_after_seek.store(false, std::memory_order_release);
            m_native_demo_seek_settle_ticks.store(
                0, std::memory_order_release);
            m_paused.store(false, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::None),
                std::memory_order_release);
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
            if (!v)
            {
                m_native_demo_seek_settle_ticks.store(
                    0, std::memory_order_release);
            }
            m_paused.store(v, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(v
                    ? ReplayScrubHoldKind::RestoredFrameHold
                    : ReplayScrubHoldKind::None),
                std::memory_order_release);
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
            ui_pause_at_live();
        }

        bool is_scrub_active() const noexcept
        {
            return is_paused()
                && m_native_demo_seek_settle_ticks.load(
                       std::memory_order_acquire) <= 0;
        }

        ReplayScrubGatePolicy replay_gate_policy() const noexcept
        {
            ReplayScrubGatePolicy p{};

            if (!is_paused())
                return p;

            if (m_native_demo_seek_settle_ticks.load(
                    std::memory_order_acquire) > 0)
                return p;

            const ReplayScrubHoldKind hold =
                static_cast<ReplayScrubHoldKind>(
                    m_hold_kind.load(std::memory_order_acquire));

            if (hold == ReplayScrubHoldKind::UiParkOnly)
            {
                p.reason = "UiParkOnly";
                return p;
            }

            if (sc6_exact_seek_phase_active(m_sc6_seek_job.phase)
                || hold == ReplayScrubHoldKind::ValidationStep)
            {
                const bool validation_step_tick =
                    m_sc6_seek_job.phase
                    == Sc6ExactSeekPhase::ValidateStepToTarget;
                p.world_tick_gate = true;
                p.replay_clock_gate = true;
                // The validation step must reproduce the captured full tick
                // graph. ActorTickGate shares WorldTickGate's policy slot and
                // can RET actor ticks that land after PerFrameTick consumes
                // the single validation credit, producing raw snapshot drift
                // even when the replay clock lands on the expected frame.
                p.actor_tick_gate = !validation_step_tick;
                p.reason = validation_step_tick
                    ? "ValidationStepNoActorGate"
                    : "ValidationStep";
                return p;
            }

            if (hold == ReplayScrubHoldKind::RestoredFrameHold)
            {
                p.world_tick_gate = true;
                p.replay_clock_gate = true;
                p.reason = "RestoredFrameHold";
                return p;
            }

            if (hold == ReplayScrubHoldKind::ManualDiagnosticFreeze)
            {
                p.world_tick_gate = true;
                p.replay_clock_gate = true;
                p.actor_tick_gate = true;
                p.time_dilation_gate = true;
                p.vm_freeze_byte = true;
                p.reason = "ManualDiagnosticFreeze";
                return p;
            }

            p.reason = "PausedNoGate";
            return p;
        }

        bool has_active_validation_step() const noexcept
        {
            return sc6_exact_seek_phase_active(m_sc6_seek_job.phase)
                || static_cast<ReplayScrubHoldKind>(
                       m_hold_kind.load(std::memory_order_acquire))
                   == ReplayScrubHoldKind::ValidationStep;
        }

        bool is_generation_done_reviewing() const noexcept
        {
            return has_context_valid_completed_timeline();
        }

        bool wants_time_controls_suspended() const noexcept
        {
            return has_active_validation_step()
                || timeline_gen_state() == TimelineGenState::Generating
                || m_gen_armed_hold_logged.load(
                    std::memory_order_acquire);
        }

        void check_ui_park_gate_state(bool world_gate,
                                      bool replay_gate,
                                      bool actor_gate,
                                      bool time_gate,
                                      bool vm_freeze) noexcept
        {
            int32_t ticks = m_ui_park_no_gate_check_ticks.load(
                std::memory_order_acquire);
            if (ticks <= 0) return;

            const ReplayScrubHoldKind hold =
                static_cast<ReplayScrubHoldKind>(
                    m_hold_kind.load(std::memory_order_acquire));
            if (hold != ReplayScrubHoldKind::UiParkOnly)
            {
                m_ui_park_no_gate_check_ticks.store(
                    0, std::memory_order_release);
                return;
            }

            m_ui_park_no_gate_check_ticks.store(
                ticks - 1, std::memory_order_release);
            if (!(world_gate || replay_gate || actor_gate
                  || time_gate || vm_freeze))
                return;

            RC::Output::send<RC::LogLevel::Error>(STR(
                "[ReplayScrub.gates] ERROR UiParkOnly has active gate "
                "world={} replay={} actor={} time={} vm={}\n"),
                world_gate ? 1 : 0, replay_gate ? 1 : 0,
                actor_gate ? 1 : 0, time_gate ? 1 : 0,
                vm_freeze ? 1 : 0);
        }

        int32_t consume_sc6_seek_native_step_request() noexcept
        {
            const int32_t requested =
                m_sc6_native_step_request.exchange(
                    0, std::memory_order_acq_rel);
            return requested > 0 ? requested : 0;
        }

        void notify_sc6_seek_native_step_granted(
            int32_t credits) noexcept
        {
            if (credits <= 0) return;
            const int32_t total =
                m_sc6_native_step_granted.fetch_add(
                    credits, std::memory_order_acq_rel)
                + credits;
            ReplayTraceFields f;
            f.integer("credits", credits)
             .integer("granted_total", total);
            ReplayDebugTrace::instance().event(
                "sc6_native_step_granted", f);
        }

        bool sc6_exact_seek_active() const noexcept
        {
            return sc6_exact_seek_phase_active(m_sc6_seek_job.phase);
        }

        bool auto_resume_on_release() const noexcept
        {
            return m_auto_resume_on_release.load(std::memory_order_acquire);
        }
        void set_auto_resume_on_release(bool v) noexcept
        {
            m_auto_resume_on_release.store(v, std::memory_order_release);
            if (!v)
                m_resume_after_seek.store(false, std::memory_order_release);
        }

        // Called by the UI when the user grabs the timeline playhead.
        // Always engages pause - the world freezes for the duration of
        // the drag regardless of any other state.
        void on_drag_start() noexcept
        {
            ui_begin_drag();
        }

        // Called by the UI when the user releases the playhead.  Default
        // behavior is review-safe: stay paused on the target frame and
        // require the user to click Play.  If auto-resume is explicitly
        // enabled, do not unpause while a UI-thread seek is still queued;
        // resume only after service_seek_request() applies that seek on
        // the game thread.
        void on_drag_end() noexcept
        {
            ui_end_drag();
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
        // After Generate Timeline, capture is usually OFF, so latest_seq()
        // is frozen.  While playback is running, map the live replay
        // (CurrentRound, master clock) back onto the generated tags instead
        // of pinning the UI to m_last_seek_target.  This avoids the old
        // broken "seek_seq + master delta" extrapolation, which failed at
        // round boundaries because master clocks rebase each round.
        //
        // Returns -1 if no useful position is available (no captures).
        int32_t current_play_position() const noexcept
        {
            int32_t requested = clamp_seq_to_timeline(
                m_ui_requested_seq.load(std::memory_order_acquire));
            if (requested >= 0
                && (is_paused() || has_pending_native_seek()))
            {
                return requested;
            }

            int32_t seeked =
                m_last_seek_target.load(std::memory_order_acquire);

            // Clamp a stale seek target to the current timeline: a
            // re-generate can shrink the timeline below a seq seeked
            // against the previous one, which would park the playhead
            // glyph off the end of the bar.
            const int32_t latest = latest_seq();
            if (seeked >= 0)
            {
                if (latest < 0)           seeked = -1;
                else if (seeked > latest) seeked = latest;
            }

            // Paused on a seeked snapshot: park the playhead there.
            if (is_paused() && seeked >= 0) return seeked;

            // Live capture running (passive capture, or a generation
            // pass): the newest snapshot tracks the current frame.
            const bool capturing =
                m_capture_enabled.load(std::memory_order_acquire) ||
                (m_timeline_gen_state.load(std::memory_order_acquire)
                 == static_cast<int>(TimelineGenState::Generating));
            if (capturing) return latest_seq();

            if (!is_paused())
            {
                const int32_t live_round = read_current_round();
                const int32_t live_master =
                    m_live_master_cached.load(std::memory_order_acquire);
                const int32_t live_seq =
                    find_slot_for_round_master(live_round, live_master);
                if (live_seq >= 0) return live_seq;
            }

            // Reviewing a finished timeline while paused, or while live
            // clocks are unreadable: park at the last seek target.
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
            const size_t  cnt_before = m_tags.count();
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
            ReplayScrubDiag::clear_cached_demo_driver();
            ReplayScrubDiag::clear_cached_demo_time_source();
            m_paused.store(false, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::None),
                std::memory_order_release);
            m_seek_request.store(kSeekIdle, std::memory_order_release);
            m_resume_after_seek.store(false, std::memory_order_release);
            m_pending_demo_seek_ms.store(-1, std::memory_order_release);
            m_pending_demo_seek_master.store(-1,
                                             std::memory_order_release);
            m_pending_demo_seek_seq.store(-1, std::memory_order_release);
            m_pending_demo_seek_round.store(-1, std::memory_order_release);
            m_pending_demo_seek_generation.store(
                0, std::memory_order_release);
            m_native_demo_seek_guard_ticks.store(0,
                                                 std::memory_order_release);
            m_native_demo_seek_settle_ticks.store(0,
                                                  std::memory_order_release);
            m_native_demo_seek_settle_ms.store(-1,
                                               std::memory_order_release);
            m_native_demo_seek_settle_seq.store(-1,
                                                std::memory_order_release);
            m_native_demo_seek_settle_master.store(-1,
                                                   std::memory_order_release);
            m_native_demo_seek_settle_generation.store(
                0, std::memory_order_release);
            m_pending_demo_seek_retry_ticks = 0;
            m_last_seek_target.store(-1, std::memory_order_release);
            m_usable_latest_seq.store(-1, std::memory_order_release);
            m_last_seek_master_tag.store(-1, std::memory_order_release);
            m_live_master_cached.store(-1, std::memory_order_release);
            m_post_seek_countdown.store(0, std::memory_order_release);
            // Cancel any in-progress timeline generation - restore the
            // engine frame cap + screen percentage + redraw hook and
            // reset the gen state so the button shows "Generate timeline"
            // again for the next replay.
            m_frame_cap.disengage();
            m_screen_pct.disengage();
            m_render_skip.disengage();
            m_timeline_gen_state.store(
                static_cast<int>(TimelineGenState::Idle),
                std::memory_order_release);
            m_timeline_context_valid.store(false,
                                           std::memory_order_release);
            m_gen_request.store(kGenReqNone, std::memory_order_release);
            m_gen_mode.store(static_cast<int>(BattleStepMode::None),
                             std::memory_order_release);
            m_gen_battle_step_generate.store(false,
                                             std::memory_order_release);
            m_gen_battle_step_probe.store(false,
                                          std::memory_order_release);
            m_exp2_transient_fail_count = 0;
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
            if (GameMode::instance().current_presence()
                    != GamePresence::Replay
                && is_generation_done_reviewing())
            {
                m_timeline_context_valid.store(
                    false, std::memory_order_release);
                m_sc6_native_step_request.store(
                    0, std::memory_order_release);
                m_seek_command_kind.store(
                    static_cast<int32_t>(SeekCommandKind::None),
                    std::memory_order_release);
                publish_native_status(
                    NativeSeekStatus::Failed,
                    NativeSeekFailure::InteractiveReplayContextUnresolved);
                publish_mode(ScrubMode::NativeSeekFailed);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] completed timeline preserved after "
                    "replay context changed; exact seek disabled until "
                    "replay reload\n"));
                return;
            }
            reset_for_new_replay("presence change");
        }

        // ---- UI accessors --------------------------------------------

        size_t ring_count()  const noexcept { return m_tags.count(); }
        int32_t live_frame() const noexcept
        {
            uint32_t c = 0;
            if (read_frame_counter(c)) return static_cast<int32_t>(c);
            return -1;
        }

        // Approximate resident bytes of the dedup snapshot store: pool
        // arena + every region's per-tick chunk-id list + tag timeline.
        // Drives the 2 GB capture ceiling and the UI memory readout.
        size_t store_bytes() const noexcept
        {
            return m_pool.bytes()
                 + m_sim_store.idlist_bytes()
                 + m_il_store.idlist_bytes()
                 + m_rdb_store.idlist_bytes()
                 + m_extras_store.idlist_bytes()
                 + m_tags.bytes();
        }

        // True once capture has stopped because the store reached the
        // 2 GB ceiling.  Cleared on the next replay / re-generate.
        bool capture_ceiling_hit() const noexcept
        {
            return m_capture_ceiling_hit.load(std::memory_order_acquire);
        }

        // Timeline coordinate accessors.  The canonical timeline coord is
        // the per-snapshot monotonic capture sequence - unique, gap-free,
        // monotonic across the WHOLE match including round boundaries.
        // Capture is append-only, so seq IS the tick index: the timeline
        // always spans seq 0 .. count-1.  earliest/latest return -1 when
        // empty.
        int32_t earliest_seq() const noexcept
        {
            return m_tags.count() ? 0 : -1;
        }
        int32_t latest_seq() const noexcept
        {
            const int32_t raw = raw_latest_seq();
            if (raw < 0) return -1;
            const int32_t usable =
                m_usable_latest_seq.load(std::memory_order_acquire);
            if (usable >= 0 && usable < raw) return usable;
            return raw;
        }

        int32_t raw_latest_seq() const noexcept
        {
            const size_t cnt = m_tags.count();
            return cnt ? static_cast<int32_t>(cnt - 1) : -1;
        }

        // Resolve a timeline seq to its captured (round, within-round
        // wall frame) for the UI's round-aware time display.  Returns
        // false if the seq isn't a captured tick.
        bool seq_tag_info(int32_t seq, int32_t& out_round,
                          int32_t& out_wall) const noexcept
        {
            const int32_t tick = find_slot_for_seq(seq);
            if (tick < 0) return false;
            int32_t s, m;
            return m_tags.get(static_cast<size_t>(tick),
                              s, out_round, out_wall, m);
        }

        // Collect the round-boundary markers for the timeline bar: one
        // entry per round present, each giving the seq of that round's
        // first captured snapshot.  The timeline is append-only and seq
        // is monotonic-by-capture, so a forward walk is already seq-
        // ascending - no sort needed.  Cheap; called once per UI render.
        std::vector<RoundMarker> collect_round_markers() const
        {
            std::vector<RoundMarker> out;
            const int32_t latest = latest_seq();
            if (latest < 0) return out;
            int32_t prev_round = -0x7fffffff;
            for (size_t k = 0; k <= static_cast<size_t>(latest); ++k)
            {
                int32_t s, r, f, m;
                if (!m_tags.get(k, s, r, f, m)) break;
                if (s < 0) continue;
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
        // Round-boundary snapshots are captured while SC6 is rebasing
        // per-round state (master clock, BM state bytes, replay actors).
        // Restoring the exact first frames of a new round has crashed in
        // the Generate-timeline path; seek a little into the round instead.
        static constexpr int32_t kRoundBoundarySeekGuardFrames = 30;
        // The 30 captured-tick guard was too narrow for a real R2 test:
        // the failing seek landed at round-local master_clock=53 and
        // crashed in the next main-sim pass.  For non-first rounds, keep
        // seeks out of the low replay-clock setup window as well.
        static constexpr int32_t kRoundBoundarySeekGuardMaster = 120;

        // Engine entry points.  Resolved via image base + RVA.
        using ExecWriteFn = void* (__fastcall*)(HgCpuBufferShim*);
        using ExecReadFn  = void* (__fastcall*)(HgCpuBufferShim*);
        using DemoGotoTimeFn = void (__fastcall*)(void*, float, void*);
        using PerFrameTickFn = void (__fastcall*)(uintptr_t*);
        using InteractiveReplayResetFn = void (__fastcall*)(void*);
        using BattleManagerSetMoveStateFn = void (__fastcall*)(void*, uint8_t);
        using NativeVoidPtrTickFn = void (__fastcall*)(void*);

        ExecWriteFn m_exec_write {nullptr};
        ExecReadFn  m_exec_read  {nullptr};
        DemoGotoTimeFn m_demo_goto_time {nullptr};
        PerFrameTickFn m_per_frame_tick_bypass {nullptr};
        InteractiveReplayResetFn m_interactive_replay_reset {nullptr};
        BattleManagerSetMoveStateFn m_battle_manager_set_move_state {nullptr};
        NativeVoidPtrTickFn m_frame_input_log_advance_replay_clock {nullptr};
        NativeVoidPtrTickFn m_battle_manager_simulation_loop {nullptr};
        NativeCallFault m_last_set_move_state_fault {};
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
        static constexpr uintptr_t kBM_bStatusByte_Off            = 0x1480;
        static constexpr uint8_t   kBM_MainStateActiveBattle      = 0x02;
        static constexpr uint8_t   kBM_StatusActiveBattle         = 0x02;
        static constexpr uintptr_t kBM_nReplayLastFrameID_Off     = 0x1488;
        static constexpr uintptr_t kBM_nReplayLastApplied_Off     = 0x148C;
        static constexpr uintptr_t kBM_nFrameAdvanceCounter_Off   = 0x1490;
        // +0x39C is the active-slot mask in the offline current-input and
        // cache writer paths. Earlier naming called it a playback cursor;
        // v12 treats it only as active-slot metadata.
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
        //   +0x1465  bSkipReplayCatchUp (set by native state-4 reset consumer)
        //   +0x1480  bStatusByte       (== 2 means active battle)
        //   +0x1490  nFrameAdvanceCounter (already captured via cursor sync)
        static constexpr uintptr_t kBM_bEnginePauseFlag_Off       = 0x12F3;
        static constexpr uintptr_t kBM_bMainStateMachineByte_Off  = 0x1461;
        static constexpr uintptr_t kBM_bSkipReplayCatchUp_Off     = 0x1465;
        // bMoveStateByte_Off and bStatusByte_Off are declared above.

        // PlayerRecordArray replay-menu bits.  Earlier notes treated
        // bit 9 as a per-frame movement gate, but Ghidra recheck on
        // 2026-05-18 showed the reader at 0x140435C20 is actually
        // ALuxBattleReplayPlayer round-reset navigation:
        // CurrentRound/StateResetData/TotalRounds, not frame motion.
        // Keep direct PRA writes under the disabled speculative gate.
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
        //   [0x80..0xE8)  chara replay-state fields
        //   [0xE8..0x208) ALuxBattleFrameInput per-slot input records
        //   [0x208..0x218) g_LuxBattle_LatestEngineInput_PerPlayer
        //   [0x218..0x230) g_LuxBattle_PerFrameCameraArgs
        //   [0x230..0x600) g_LuxBattle_PerPlayerInputRing entries
        //   [0x600..0x608) g_LuxBattle_PerPlayerInputRingCursor[2]
        //   [0x608..0x610) g_LuxBattle_InputRingBaseOffset_PerPlayer[2]
        //   [0x610..0x674) g_LuxBattle_LfsrState
        //   [0x674..0x678) g_dwLuxBattleLfsrIndex
        //   [0x678..0x738) g_LuxBattle_CCpuCommandArray
        //   [0x738..0x73C) BM+0x1490 nFrameAdvanceCounter
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
        // 2026-05-23: captured seek validation showed the first stable
        // post-step mismatch at HgCpuDirect sim+0x2148, which maps through
        // WriteCharaStateToSnapshot to live chara+0x2158
        // (dwRawButtonWord_2158).  Ghidra confirms LuxBattle_PerFrameTick
        // rebuilds that region from pArgs->pInputP1/P2, and those args are
        // mirrored into g_LuxBattle_LatestEngineInput_PerPlayer before the
        // chara input tick.  HgCpuDirect does not serialize these globals,
        // so validated captured seek must snapshot them alongside FrameInput.
        static constexpr size_t kExtras_Off_LatestEngineInput = 0x208;
        static constexpr size_t kExtras_LatestEngineInput_Bytes = 0x10;
        static constexpr size_t kExtras_Off_PerFrameCameraArgs = 0x218;
        static constexpr size_t kExtras_PerFrameCameraArgs_Bytes = 0x18;
        static constexpr size_t kInputRing_PlayerCount        = 2;
        static constexpr size_t kInputRing_EntriesPerPlayer   = 0x3D;
        static constexpr size_t kInputRing_EntryBytes         = 8;
        static constexpr size_t kInputRing_PlayerBytes =
            kInputRing_EntriesPerPlayer * kInputRing_EntryBytes;
        static constexpr size_t kExtras_Off_InputRingEntries  = 0x230;
        static constexpr size_t kExtras_InputRingEntries_Bytes =
            kInputRing_PlayerCount * kInputRing_PlayerBytes;
        static constexpr size_t kExtras_Off_InputRingCursor   = 0x600;
        static constexpr size_t kExtras_InputRingCursor_Bytes = 0x08;
        static constexpr size_t kExtras_Off_InputRingBase     = 0x608;
        static constexpr size_t kExtras_InputRingBase_Bytes   = 0x08;
        static constexpr size_t kExtras_Off_LfsrState         = 0x610;
        static constexpr size_t kExtras_LfsrState_Bytes       = 0x64;
        static constexpr size_t kExtras_Off_LfsrIndex         = 0x674;
        static constexpr size_t kExtras_LfsrIndex_Bytes       = 0x04;
        static constexpr size_t kExtras_Off_CCpuCommandArray  = 0x678;
        static constexpr size_t kExtras_CCpuCommandArray_Bytes = 0xC0;
        static constexpr size_t kExtras_Off_BM_FrameAdvance   = 0x738;
        static constexpr size_t kExtras_Bytes                 = 0x73C;

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
        static constexpr uintptr_t kRP_StateResetData_Off  = 0x3A8;
        // nTotalRounds: number of rounds in the recorded match.  Verified
        // via execIsExistNextRound @ 0x1409a5490, whose body is
        // RetVal = (CurrentRound + 1) < nTotalRounds - so the final round
        // is the one with CurrentRound == nTotalRounds - 1.
        static constexpr uintptr_t kRP_TotalRounds_Off     = 0x3B0;
        static constexpr uintptr_t kRP_IsPlayingBack_Off   = 0x3D0;

        // ----------------------------------------------------------------
        // Deduplicated snapshot store.
        //
        // Capture is append-only and unbounded (to a 2 GB ceiling): each
        // replay tick folds four fixed-length regions into RegionStores
        // backed by a shared ChunkPool.  The pool keeps every DISTINCT
        // 512-byte chunk once, so the redundant bulk of each snapshot
        // (stage geometry, configs, move-data - byte-identical tick to
        // tick) costs nothing after its first occurrence.  The tick index
        // is the canonical timeline coordinate and equals the capture seq.
        //
        // m_pool MUST out-live the RegionStores (they hold a back-pointer
        // to it) - declared first so it destructs last.
        ChunkPool m_pool;

        // Sim region: HgCpuDirect's 0x28018-byte per-snapshot blob (chara,
        // globals, terrain, camera, timer, motion, physics, VFX).
        RegionStore m_sim_store;

        // InputLog-state region: pInputLog+0x394..+0x4414, the full
        // replay-related state range (input cache, drain cursor, playback
        // cursor, master clock, and any hidden bookkeeping between).
        // Restored on seek alongside the sim snapshot so the engine's
        // whole replay-playback machinery thinks it is at the captured
        // frame.  (An earlier narrower +0x3C0..+0x43C0 capture proved
        // insufficient - see the kIL_CaptureStart_Off plate above.)
        RegionStore m_il_store;

        // Decoder-state region: the Stage 1 decoder's FLuxReplayDataBlock
        // (1021 bytes at *pBM+0x460) - file/decoded-buffer cursors,
        // working frame ID, running flag.  2026-05-11 finding: without
        // rewinding these on a backward seek the decoder stays at the
        // live-edge file position and feeds "inputs from later in the
        // round" onto the just-restored state-at-T.
        RegionStore m_rdb_store;

        // Extras region (round-end seek-back fix, 2026-05-14):
        // BlockInteractiveOps + cinematic head + BM internal state bytes +
        // FrameInput per-slot records + per-chara replay-state fields.
        // See the kExtras_* constants above for the exact layout.
        // Restored in lockstep so a backward seek from post-round into
        // mid-round un-gates LuxBattle_PerFrameTick's chara input tick,
        // otherwise blocked while WorldModePump's GetModeType still
        // returns 3 (round-end) post-restore.
        RegionStore m_extras_store;

        // Compact semantic oracle captured once per generated tick.  The
        // native HgCpuDirect byte stream contains reader-rebuilt pointer
        // and motion-helper regions, so full byte equality is diagnostic
        // only.  This oracle is the strict landing gate for user-visible
        // seek: round/master, replay inputs, RNG head, and gameplay-facing
        // per-chara MoveVM state must match the captured tick before Play
        // can enable.
        std::vector<ReplayFrameOracleSnap> m_oracle_frames;

        // Native SC6 round-reset snapshots copied from BattleManager+0x1360
        // as each round appears in the generated timeline.  These give exact
        // seek a reset source even if ALuxBattleReplayPlayer/StateResetData is
        // no longer discoverable after match completion.
        std::array<std::array<uint8_t, kRoundStartDataBytes>,
                   kMaxSc6ReplayRounds> m_sc6_round_reset_snapshots {};
        std::array<bool, kMaxSc6ReplayRounds>
            m_sc6_round_reset_snapshot_valid {};
        std::array<int32_t, kMaxSc6ReplayRounds>
            m_sc6_round_reset_snapshot_seq {};
        std::array<int32_t, kMaxSc6ReplayRounds>
            m_sc6_round_reset_snapshot_master {};
        std::array<int32_t, kMaxSc6ReplayRounds>
            m_sc6_round_reset_snapshot_last_frame_id {};
        std::atomic<bool> m_timeline_seek_data_valid {false};
        std::atomic<bool> m_timeline_context_valid {false};
        std::atomic<bool> m_oracle_capture_failed {false};
        std::atomic<int32_t> m_ui_park_no_gate_check_ticks {0};

        // Per-tick metadata, one entry per committed snapshot, appended
        // by the capture path and read locklessly by the UI thread (see
        // the TagTimeline plate).  ring_count() == m_tags.count().  Four
        // tags per tick:
        //
        //   seq    -- monotonic capture sequence (a HorseMod-side counter,
        //             +1 per capture).  THE canonical timeline coordinate:
        //             unique, gap-free and monotonic across the WHOLE
        //             match including round boundaries.  Equals the tick
        //             index.  The UI bar, playhead and seek all key off it.
        //   round  -- ALuxBattleReplayPlayer.CurrentRound at capture time.
        //             Drives the timeline's round-boundary markers +
        //             round-aware time display.
        //   frame  -- g_LuxBattle_FrameCounter (wall clock).  NOTE: this
        //             RESETS TO 0 at every round boundary (LuxBattle_-
        //             InitializeMatchRoundState @ 0x1402DBA92 zeroes it),
        //             so it is NOT a valid match-wide coordinate - kept
        //             only as the within-round frame number for display +
        //             the PRE_SEEK diagnostic.
        //   master -- pInputLog->nMasterClock (replay clock) at capture
        //             time.  Used by write_replay_cursors to sync the
        //             engine's InputLog cursors after a snapshot restore,
        //             and by the post-seek playhead extrapolation.  The
        //             engine reads recorded inputs indexed by master
        //             clock, so the restore MUST use this, not wall clock.
        TagTimeline m_tags;

        // Set true (once) when capture stops at the 2 GB store ceiling.
        // Cleared by drop_ring() on the next replay / re-generate.
        std::atomic<bool> m_capture_ceiling_hit {false};

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
        std::atomic<int32_t> m_hold_kind {
            static_cast<int32_t>(ReplayScrubHoldKind::None)};
        std::atomic<bool> m_auto_resume_on_release   {false};
        std::atomic<bool> m_resume_after_seek        {false};
        std::atomic<int32_t> m_seek_request          {kSeekIdle};

        // Replay timeline state-machine model.  The UI posts compact
        // intents; the cockpit/game thread owns the full structs and
        // publishes a small atomic view.  This separates "where the user
        // put the playhead" from "what preview restored" and "what
        // DemoNetDriver has actually landed."
        UiPlayheadState m_ui {};
        PreviewState    m_preview {};
        NativeSeekState m_native {};
        Sc6ExactSeekJob m_sc6_seek_job {};
        uint32_t        m_seek_generation {0};
        std::atomic<int32_t> m_sc6_native_step_request {0};
        std::atomic<int32_t> m_sc6_native_step_granted {0};

        std::atomic<int32_t> m_seek_command_kind {
            static_cast<int32_t>(SeekCommandKind::None)};
        std::atomic<int32_t> m_seek_command_seq {-1};
        std::atomic<int32_t> m_ui_displayed_seq {-1};
        std::atomic<int32_t> m_ui_requested_seq {-1};
        std::atomic<int32_t> m_drag_preview_seq {-1};
        std::atomic<int32_t> m_last_drag_preview_seq {-1};
        std::atomic<bool>    m_ui_dragging {false};
        std::atomic<bool>    m_ui_wants_play {false};
        std::atomic<int32_t> m_scrub_mode {
            static_cast<int32_t>(ScrubMode::Idle)};
        std::atomic<int32_t> m_native_status {
            static_cast<int32_t>(NativeSeekStatus::Idle)};
        std::atomic<int32_t> m_play_block_reason {
            static_cast<int32_t>(NativeSeekFailure::None)};
        std::atomic<int32_t> m_preview_status {
            static_cast<int32_t>(PreviewStatus::Idle)};

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
        // Diagnostic-only UE demo seek state.  SC6 replay viewer scrubbing
        // uses native SC6 round reset + deterministic PerFrameTick
        // fast-forward as the authority; UDemoNetDriver remains only behind
        // HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG for developer validation.
        std::atomic<int32_t> m_pending_demo_seek_ms     {-1};
        std::atomic<int32_t> m_pending_demo_seek_master {-1};
        std::atomic<int32_t> m_pending_demo_seek_seq    {-1};
        std::atomic<int32_t> m_pending_demo_seek_round  {-1};
        std::atomic<uint32_t> m_pending_demo_seek_generation {0};
        std::atomic<int32_t> m_native_demo_seek_guard_ticks {0};
        std::atomic<int32_t> m_native_demo_seek_settle_ticks {0};
        std::atomic<int32_t> m_native_demo_seek_settle_ms    {-1};
        std::atomic<int32_t> m_native_demo_seek_settle_seq   {-1};
        std::atomic<int32_t> m_native_demo_seek_settle_master {-1};
        std::atomic<uint32_t> m_native_demo_seek_settle_generation {0};
        int32_t              m_pending_demo_seek_retry_ticks {0};
        static constexpr int32_t kNativeDemoSeekGuardTicks = 600;
        static constexpr int32_t kNativeDemoSeekRetryTicks = 5;
        static constexpr int32_t kNativeDemoSeekSettleTicks = 120;
        static constexpr float kNativeSeekTimeToleranceSeconds =
            0.5f / 60.0f;
        static constexpr bool kEnableLegacySeekDiagnostics =
            HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG != 0;
        static constexpr bool kEnableLegacySnapshotPreview = false;
        static constexpr int32_t kSc6NativeStepCreditsPerRequest = 1;
        static constexpr int32_t kSc6NativeStepMaxStalls = 16;
        static constexpr int32_t kSc6SeekMaxFrames = 20000;

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
        // Normal UI/seeks clamp to this after Generate Timeline parks on
        // a pre-result frame, leaving any captured post-KO tail as
        // diagnostics-only data.
        std::atomic<int32_t> m_usable_latest_seq     {-1};

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
        int32_t m_playback_diag_last_master {-1};
        int32_t m_playback_diag_last_round {-1};
        int32_t m_playback_diag_last_result {0};
        int32_t m_playback_diag_last_is_playing {-1};
        std::array<int32_t, kMaxSc6ReplayRounds>
            m_playback_round_first_safe_seq {};
        std::array<int32_t, kMaxSc6ReplayRounds>
            m_playback_round_last_safe_seq {};
        std::array<int32_t, kMaxSc6ReplayRounds>
            m_playback_round_first_result_seq {};
        std::atomic<int32_t> m_playback_result_guard_bypass_round {-1};
        std::atomic<bool> m_playback_result_guard_paused {false};
        std::atomic<int32_t> m_playback_result_guard_pause_round {-1};
        std::atomic<int32_t> m_playback_result_guard_pause_seq {-1};

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
        RenderSkipOverride       m_render_skip;
        std::atomic<int>     m_gen_request               {0};
        std::atomic<int>     m_gen_armed_mode            {0};
        std::atomic<int32_t> m_gen_armed_last_log_round  {INT32_MIN};
        std::atomic<int32_t> m_gen_armed_last_log_master_bucket {INT32_MIN};
        std::atomic<int>     m_gen_armed_last_log_state  {0};
        std::atomic<bool>    m_gen_armed_hold_logged     {false};
        std::atomic<int>     m_timeline_gen_state        {0};
        std::atomic<int32_t> m_timeline_gen_start_master {0};
        std::atomic<int32_t> m_timeline_gen_last_master  {0};
        // Which generation mode is active while state==Generating:
        // 0=None, 1=RenderSkip, 2=DirectPerFrame.
        // Drives UI status text + profile flags.
        std::atomic<int>     m_gen_mode                  {0};
        // Experimental battle-step generation: direct PerFrameTick stepping.
        std::atomic<bool>    m_gen_battle_step_generate  {false};
        std::atomic<bool>    m_gen_battle_step_probe     {false};

        // m_gen_request values.
        static constexpr int kGenReqNone              = 0;
        static constexpr int kGenReqStart             = 1;
        static constexpr int kGenReqStop              = 2;
        static constexpr int kGenReqStartExperimental = 3;
        static constexpr int kGenReqStartBattleStep   = 4;
        static constexpr int kGenReqBattleStepProbe   = 5;

        // Keep direct-step loops responsive by capping work per game tick.
        static constexpr int32_t kExp2MaxFramesPerSlice = 32;
        static constexpr int64_t kExp2SliceBudgetUs     = 2400;
        static constexpr int32_t kExp2TransientFailureBudget = 4;

        // Auto-stop tuning.  Wall-clock based so it is independent of
        // the (now uncapped, hardware-dependent) frame rate.
        //   kGenStuckSeconds: master clock idle this long => end of the
        //     recording reached.  Set well above any legitimate
        //     mid-replay master-clock stall (round transition / KO
        //     cinematic) so generation never false-stops mid-stream.
        //     The prompt end signals are presence-change / loop /
        //     teardown; this stall timer is only the backstop for a
        //     replay that halts on a held end screen.
        //   kGenFinalRoundStuckSeconds: the shorter stall window applied
        //     once the replay is in its FINAL round (CurrentRound ==
        //     nTotalRounds-1).  In the last round a master-clock stall
        //     can only be the match ending - there is no next round to
        //     wait for - so generation stops promptly instead of idling
        //     the full kGenStuckSeconds on the post-KO cinematic / held
        //     end screen.  This is what makes "Generate timeline" stop
        //     when the match is won.
        //   kGenMaxSeconds: hard safety ceiling on a generation run.
        //     Sized to outlast a full multi-round match replayed at ~1x
        //     (a 3-round match is ~5 min) on slow hardware where
        //     generation barely beats real time - kGenStuckSeconds is
        //     the real end-of-recording detector, this is only the
        //     backstop for a true hang.  120 s used to truncate long
        //     replays mid-generation and falsely report "safety-timeout".
        static constexpr double kGenStuckSeconds           = 8.0;
        static constexpr double kGenFinalRoundStuckSeconds = 3.0;
        static constexpr double kGenMaxSeconds             = 600.0;
        static constexpr int32_t kGenMaxConsecutiveCaptureFailures = 8;
        static constexpr int32_t kPostResultParkBackoffFrames = 30;
        // Cockpit ticks bIsPlayingBack must stay 0 before the playback-
        // ended backstop trips - far beyond any momentary flicker.
        static constexpr int32_t kGenPlaybackGoneTicks     = 30;

        // Wall-clock marks for the auto-stop logic.  Touched only by the
        // game thread (start_generate_timeline / tick_generate_timeline),
        // so plain members - no atomics needed.
        std::chrono::steady_clock::time_point m_gen_started_at  {};
        std::chrono::steady_clock::time_point m_gen_finished_at {};
        std::chrono::steady_clock::time_point m_gen_last_advance{};
        std::atomic<uint64_t> m_gen_profile_frames    {0};
        std::atomic<uint64_t> m_gen_profile_total_us  {0};
        std::atomic<uint64_t> m_gen_profile_sim_us    {0};
        std::atomic<uint64_t> m_gen_profile_il_us     {0};
        std::atomic<uint64_t> m_gen_profile_rdb_us    {0};
        std::atomic<uint64_t> m_gen_profile_extras_us {0};
        std::atomic<uint64_t> m_gen_profile_commit_us {0};
        std::vector<uint8_t> m_exp2_sim_before;
        std::vector<uint8_t> m_exp2_sim_after;
        std::vector<uint8_t> m_exp2_il_before;
        std::vector<uint8_t> m_exp2_rdb_before;
        std::vector<uint8_t> m_exp2_extras_before;
        int32_t m_exp2_transient_fail_count {0};
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
        // Set true once the replay has actually advanced (master clock or
        // round) during THIS generation pass.  Gates the short final-round
        // stall window so a generation started while the replay is still
        // on a frozen intro can't immediately false-stop.
        bool m_gen_seen_progress {false};
        // Set true once the FINAL round has been observed live (in the
        // last round with no round-result code yet).  Gates the
        // match-ended stop so a stale result code carried in from the
        // previous round's end can't false-trigger at the final round's
        // start.  See tick_generate_timeline.
        bool m_gen_final_round_played {false};
        // Set true once ALuxBattleReplayPlayer.bIsPlayingBack has been
        // observed == 1 this pass; a sustained 0 afterwards signals the
        // engine ended replay playback (backstop match-end detector).
        bool m_gen_seen_playing_back {false};
        // Consecutive cockpit ticks bIsPlayingBack has read 0 (after
        // having been seen 1).  Debounces the playback-ended backstop so
        // a momentary mid-match dip can't false-stop generation.
        int32_t m_gen_playback_gone_ticks {0};
        // Set true once the match has been observed UNDECIDED this pass
        // (match_decided() == false while progressing).  Gates the
        // match-decided stop so a stale win-count left in a reused chara
        // slot can't false-fire before the replay has been seen live.
        bool m_gen_match_undecided_seen {false};
        // ALuxBattleReplayPlayer.nTotalRounds, latched the first time it
        // reads as a sane positive value this pass.  read_total_rounds()
        // returns -1 while the actor is briefly unresolvable (common just
        // after a scene transition); caching keeps a later -1 from
        // dropping in_final_round / the match-ended detector.
        int32_t m_gen_total_rounds {-1};
        int32_t m_gen_final_round_first_safe_seq {-1};
        int32_t m_gen_final_round_last_safe_seq  {-1};
        bool m_gen_missing_demo_time_logged {false};
        bool m_gen_demo_time_recovered_logged {false};
        int32_t m_gen_capture_fail_count {0};
        const char* m_last_extras_failure {"none"};

        // ---- Internals ------------------------------------------------

        void publish_mode(ScrubMode mode) noexcept
        {
            m_scrub_mode.store(static_cast<int32_t>(mode),
                               std::memory_order_release);
        }

        void publish_native_status(NativeSeekStatus status,
                                   NativeSeekFailure failure =
                                       NativeSeekFailure::None) noexcept
        {
            m_native.status = status;
            m_native.failure = failure;
            m_native_status.store(static_cast<int32_t>(status),
                                  std::memory_order_release);
            m_play_block_reason.store(static_cast<int32_t>(failure),
                                      std::memory_order_release);
        }

        void publish_native_failure_reason(
            NativeSeekFailure failure) noexcept
        {
            m_native.failure = failure;
            m_play_block_reason.store(static_cast<int32_t>(failure),
                                      std::memory_order_release);
        }

        void publish_preview_status(PreviewStatus status,
                                    NativeSeekFailure reason =
                                        NativeSeekFailure::None) noexcept
        {
            m_preview.status = status;
            m_preview.failure_reason = reason;
            m_preview_status.store(static_cast<int32_t>(status),
                                   std::memory_order_release);
        }

        void publish_ui_target(int32_t seq) noexcept
        {
            seq = clamp_seq_to_timeline(seq);
            if (seq < 0) return;
            m_ui_requested_seq.store(seq, std::memory_order_release);
            m_ui_displayed_seq.store(seq, std::memory_order_release);
        }

        void post_seek_command(SeekCommandKind kind, int32_t seq) noexcept
        {
            if (seq >= 0)
                seq = clamp_seq_to_timeline(seq);
            m_seek_command_seq.store(seq, std::memory_order_release);
            m_seek_command_kind.store(static_cast<int32_t>(kind),
                                      std::memory_order_release);
        }

        bool has_pending_native_seek() const noexcept
        {
            const NativeSeekStatus status =
                static_cast<NativeSeekStatus>(
                    m_native_status.load(std::memory_order_acquire));
            const Sc6ExactSeekPhase sc6_phase = m_sc6_seek_job.phase;
            const bool sc6_pending =
                sc6_exact_seek_phase_active(sc6_phase);
            return m_pending_demo_seek_ms.load(std::memory_order_acquire) >= 0
                || sc6_pending
                || status == NativeSeekStatus::Queued
                || status == NativeSeekStatus::DeferredBusy
                || status == NativeSeekStatus::Submitted
                || status == NativeSeekStatus::Settling;
        }

        void service_drag_preview() noexcept
        {
            if (!m_ui_dragging.load(std::memory_order_acquire))
            {
                m_drag_preview_seq.store(-1, std::memory_order_release);
                m_last_drag_preview_seq.store(-1,
                                              std::memory_order_release);
                return;
            }

            if (sc6_exact_seek_phase_active(m_sc6_seek_job.phase))
                return;

            const SeekCommandKind pending_kind =
                static_cast<SeekCommandKind>(
                    m_seek_command_kind.load(std::memory_order_acquire));
            if (pending_kind != SeekCommandKind::None)
                return;

            int32_t seq = clamp_seq_to_timeline(
                m_drag_preview_seq.load(std::memory_order_acquire));
            if (seq < 0) return;
            if (seq == m_last_drag_preview_seq.load(
                    std::memory_order_acquire))
                return;

            int32_t tick = find_slot_for_seq(seq);
            if (tick < 0)
            {
                publish_preview_status(PreviewStatus::Failed,
                                       NativeSeekFailure::InvalidTarget);
                m_last_drag_preview_seq.store(seq,
                                              std::memory_order_release);
                return;
            }

            int32_t preview_seq = -1, preview_round = -1,
                    preview_wall = -1, preview_master = -1;
            if (!m_tags.get(static_cast<size_t>(tick),
                            preview_seq, preview_round,
                            preview_wall, preview_master)
                || preview_seq < 0 || preview_round < 0
                || preview_master < 0)
            {
                publish_preview_status(PreviewStatus::Failed,
                                       NativeSeekFailure::InvalidTarget);
                m_last_drag_preview_seq.store(seq,
                                              std::memory_order_release);
                return;
            }

            // Dragging must be visual/UI-only.  Restoring captured frames
            // every mouse move was mutating live replay state while the
            // user scrubbed, and crash logs show this is unsafe near round
            // boundaries.  The real restore/validation happens once on
            // click/release through queue_sc6_exact_seek().
            publish_ui_target(preview_seq);
            m_preview.seq = preview_seq;
            m_preview.round = preview_round;
            m_last_drag_preview_seq.store(seq,
                                          std::memory_order_release);
            publish_preview_status(PreviewStatus::SkippedUnsafe);
            publish_mode(ScrubMode::Dragging);
            if (m_verbose_diag.load(std::memory_order_acquire))
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] drag preview UI-only seq={} tick={} "
                    "round={} master={}\n"),
                    preview_seq, tick, preview_round, preview_master);
            }
            {
                ReplayTraceFields f;
                f.integer("requested_seq", seq)
                 .integer("target_seq", preview_seq)
                 .integer("target_round", preview_round)
                 .integer("target_master", preview_master)
                 .integer("target_tick", tick)
                 .boolean("restore_skipped", true);
                ReplayDebugTrace::instance().event(
                    "drag_preview_ui_only", f);
            }
        }

        void clear_scrub_state() noexcept
        {
            m_ui = UiPlayheadState{};
            m_preview = PreviewState{};
            m_native = NativeSeekState{};
            m_sc6_seek_job = Sc6ExactSeekJob{};
            m_seek_generation = 0;
            m_seek_request.store(kSeekIdle, std::memory_order_release);
            m_resume_after_seek.store(false, std::memory_order_release);
            m_seek_command_kind.store(
                static_cast<int32_t>(SeekCommandKind::None),
                std::memory_order_release);
            m_seek_command_seq.store(-1, std::memory_order_release);
            m_ui_displayed_seq.store(-1, std::memory_order_release);
            m_ui_requested_seq.store(-1, std::memory_order_release);
            m_drag_preview_seq.store(-1, std::memory_order_release);
            m_last_drag_preview_seq.store(-1, std::memory_order_release);
            m_ui_dragging.store(false, std::memory_order_release);
            m_ui_wants_play.store(false, std::memory_order_release);
            clear_playback_result_guard_override();
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::None),
                std::memory_order_release);
            m_pending_demo_seek_ms.store(-1, std::memory_order_release);
            m_pending_demo_seek_master.store(-1, std::memory_order_release);
            m_pending_demo_seek_seq.store(-1, std::memory_order_release);
            m_pending_demo_seek_round.store(-1, std::memory_order_release);
            m_pending_demo_seek_generation.store(0,
                                                 std::memory_order_release);
            m_native_demo_seek_guard_ticks.store(0,
                                                 std::memory_order_release);
            m_native_demo_seek_settle_ticks.store(0,
                                                  std::memory_order_release);
            m_native_demo_seek_settle_ms.store(-1,
                                               std::memory_order_release);
            m_native_demo_seek_settle_seq.store(-1,
                                                std::memory_order_release);
            m_native_demo_seek_settle_master.store(-1,
                                                   std::memory_order_release);
            m_native_demo_seek_settle_generation.store(
                0, std::memory_order_release);
            m_pending_demo_seek_retry_ticks = 0;
            m_last_seek_target.store(-1, std::memory_order_release);
            m_last_seek_master_tag.store(-1, std::memory_order_release);
            m_playback_diag_last_master = -1;
            m_playback_diag_last_round = -1;
            m_playback_diag_last_result = 0;
            m_playback_diag_last_is_playing = -1;
            publish_mode(ScrubMode::Idle);
            publish_native_status(NativeSeekStatus::Idle);
            publish_preview_status(PreviewStatus::Idle);
        }

        void service_ui_command() noexcept
        {
            const SeekCommandKind kind = static_cast<SeekCommandKind>(
                m_seek_command_kind.exchange(
                    static_cast<int32_t>(SeekCommandKind::None),
                    std::memory_order_acq_rel));
            if (kind == SeekCommandKind::None) return;
            int32_t seq = m_seek_command_seq.exchange(
                -1, std::memory_order_acq_rel);
            if (seq >= 0) seq = clamp_seq_to_timeline(seq);

            switch (kind)
            {
            case SeekCommandKind::RequestPreviewAndNativeSeek:
            case SeekCommandKind::StepToSeq:
                if (seq < 0) return;
                if (m_sc6_seek_job.requested_seq == seq
                    && sc6_exact_seek_phase_active(m_sc6_seek_job.phase))
                {
                    RC::Output::send<RC::LogLevel::Verbose>(STR(
                        "[ReplayScrub.sc6seek] duplicate pending seek "
                        "ignored seq={} phase={}\n"),
                        seq, static_cast<int>(m_sc6_seek_job.phase));
                    return;
                }
                m_paused.store(true, std::memory_order_release);
                m_hold_kind.store(
                    static_cast<int32_t>(ReplayScrubHoldKind::ValidationStep),
                    std::memory_order_release);
                publish_ui_target(seq);
                ++m_seek_generation;
                m_native.generation = m_seek_generation;
                m_native.requested_seq = seq;
                m_native.adjusted_seq = -1;
                m_native.failure = NativeSeekFailure::None;
                m_native.direct_driver_available = false;
                m_native.cvar_submitted = false;
                publish_native_status(NativeSeekStatus::Queued,
                                      NativeSeekFailure::None);
                publish_mode(
                    m_ui_dragging.load(std::memory_order_acquire)
                        ? ScrubMode::Dragging
                        : ScrubMode::PausedPreview);
                publish_preview_status(PreviewStatus::SkippedUnsafe);
                if (!queue_sc6_exact_seek(seq, "USER"))
                {
                    // queue_sc6_exact_seek() publishes failure and restores
                    // a stable hold.  Do not leave the pre-queue
                    // ValidationStep hold active here.
                }
                break;

            case SeekCommandKind::PauseAtLive:
                ui_pause_at_live();
                break;

            case SeekCommandKind::PlayFromSelected:
                if (can_play_from_selected())
                {
                    (void)resume_play_if_battle_status_active("PLAY_BUTTON");
                }
                else
                {
                    m_ui_wants_play.store(false, std::memory_order_release);
                    m_paused.store(true, std::memory_order_release);
                    m_hold_kind.store(
                        static_cast<int32_t>(
                            ReplayScrubHoldKind::RestoredFrameHold),
                        std::memory_order_release);
                    NativeSeekFailure reason = m_native.failure;
                    if (reason == NativeSeekFailure::None)
                        reason = NativeSeekFailure::NotLanded;
                    m_play_block_reason.store(static_cast<int32_t>(reason),
                                              std::memory_order_release);
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] play blocked: requested_seq={} "
                        "landed_seq={} native_status={} reason={} "
                        "sc6_phase={} sc6_target_master={}\n"),
                        m_ui_requested_seq.load(std::memory_order_acquire),
                        m_last_seek_target.load(std::memory_order_acquire),
                        m_native_status.load(std::memory_order_acquire),
                        static_cast<int32_t>(reason),
                        static_cast<int32_t>(m_sc6_seek_job.phase),
                        m_sc6_seek_job.target_master);
                }
                break;

            case SeekCommandKind::Cancel:
                cancel_scrub();
                break;

            case SeekCommandKind::None:
                break;
            }
        }

        void publish_timeline_state() noexcept
        {
            int32_t displayed = clamp_seq_to_timeline(
                m_ui_displayed_seq.load(std::memory_order_acquire));
            if (displayed < 0) displayed = current_play_position();
            m_ui_displayed_seq.store(displayed, std::memory_order_release);
            const int32_t requested = clamp_seq_to_timeline(
                m_ui_requested_seq.load(std::memory_order_acquire));
            m_ui_requested_seq.store(requested, std::memory_order_release);

            if (!is_paused()
                && !has_pending_native_seek()
                && m_scrub_mode.load(std::memory_order_acquire)
                    != static_cast<int32_t>(ScrubMode::Playing))
            {
                publish_mode(ScrubMode::Generated);
            }
        }

        bool resolve_natives() noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            m_exec_write = reinterpret_cast<ExecWriteFn>(
                base + kRVA_ExecMoveChangeAndPost);
            m_exec_read  = reinterpret_cast<ExecReadFn>(
                base + kRVA_ExecFinalizeAndPost);
            m_demo_goto_time = reinterpret_cast<DemoGotoTimeFn>(
                base + kRVA_DemoGotoTimeInSeconds);
            m_interactive_replay_reset =
                reinterpret_cast<InteractiveReplayResetFn>(
                    base + kRVA_LuxBattleInteractiveReplayReset);
            m_battle_manager_set_move_state =
                reinterpret_cast<BattleManagerSetMoveStateFn>(
                    base + kRVA_ALuxBattleManagerSetMoveState);
            m_frame_input_log_advance_replay_clock =
                reinterpret_cast<NativeVoidPtrTickFn>(
                    base + kRVA_FrameInputLogAdvanceReplayClock);
            m_battle_manager_simulation_loop =
                reinterpret_cast<NativeVoidPtrTickFn>(
                    base + kRVA_BattleManagerSimulationLoop);
            m_frame_counter_addr =
                reinterpret_cast<const void*>(base + kRVA_FrameCounter);
            return m_exec_write != nullptr
                && m_exec_read  != nullptr
                && m_demo_goto_time != nullptr
                && m_battle_manager_set_move_state != nullptr
                && m_frame_input_log_advance_replay_clock != nullptr
                && m_battle_manager_simulation_loop != nullptr
                && m_frame_counter_addr != nullptr;
        }

        bool resolve_per_frame_tick_bypass() noexcept
        {
            if (m_per_frame_tick_bypass) return true;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            void* site = reinterpret_cast<void*>(
                base + kRVA_LuxBattlePerFrameTick);

            constexpr size_t kTrampSize = 12;
            uint8_t* tramp = static_cast<uint8_t*>(
                CodeCave::allocate(kTrampSize));
            if (!tramp) return false;

            size_t off = 0;
            const uint8_t prologue[7] =
                {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10};
            std::memcpy(tramp + off, prologue, sizeof(prologue));
            off += sizeof(prologue);

            uint8_t jmp[5] = {};
            if (!encode_jmp_rel32(tramp + off,
                                  static_cast<uint8_t*>(site) + 7, jmp))
                return false;
            std::memcpy(tramp + off, jmp, sizeof(jmp));
            off += sizeof(jmp);

            ::FlushInstructionCache(::GetCurrentProcess(), tramp, off);
            m_per_frame_tick_bypass =
                reinterpret_cast<PerFrameTickFn>(tramp);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.EXP2] direct PerFrameTick bypass ready "
                "(tramp=0x{:x}, target=0x{:x})\n"),
                reinterpret_cast<uintptr_t>(tramp),
                base + kRVA_LuxBattlePerFrameTick);
            return true;
        }

        // Reset the dedup store to empty: re-size the four RegionStores,
        // clear the shared ChunkPool and the tag timeline, and reset the
        // ceiling flag.  Used for first-time init (ensure_initialized) and
        // to discard a timeline that became chronologically discontinuous
        // - a new replay (on_presence_change), a replay restart inside the
        // viewer (CurrentRound jumps backward), or a re-generate.  Round-
        // to-round transitions do NOT drop the store - the timeline spans
        // the whole match.
        //
        // RegionStore::init() frees the previous chunk-id list, so after a
        // 2 GB session this genuinely returns the memory.  Game-thread
        // only; the UI thread only ever touches m_tags, and m_tags.clear()
        // is a single atomic store it tolerates concurrently.
        void drop_ring() noexcept
        {
            // Clear the shared pool and tag timeline BEFORE re-sizing the
            // RegionStores, so that if a store init somehow throws (only
            // the ~182 KB of staging buffers can - effectively
            // unreachable) the pool is already empty and consistent.  A
            // store left partly sized after such a failure can still hold
            // stale chunk ids, but RegionStore::load() bounds-checks every
            // id against the pool, so a later gather() safely returns
            // false rather than reading out of bounds.
            m_pool.clear();
            m_tags.clear();
            try { m_oracle_frames.clear(); }
            catch (...) {}
            m_sc6_round_reset_snapshot_valid.fill(false);
            m_sc6_round_reset_snapshot_seq.fill(-1);
            m_sc6_round_reset_snapshot_master.fill(-1);
            m_sc6_round_reset_snapshot_last_frame_id.fill(-1);
            reset_playback_result_guard_markers();
            m_timeline_seek_data_valid.store(
                false, std::memory_order_release);
            m_timeline_context_valid.store(
                false, std::memory_order_release);
            m_oracle_capture_failed.store(false,
                                          std::memory_order_release);
            m_ui_park_no_gate_check_ticks.store(
                0, std::memory_order_release);
            m_capture_ceiling_hit.store(false, std::memory_order_release);
            m_usable_latest_seq.store(-1, std::memory_order_release);
            m_pending_demo_seek_ms.store(-1, std::memory_order_release);
            m_pending_demo_seek_master.store(-1,
                                             std::memory_order_release);
            m_pending_demo_seek_seq.store(-1, std::memory_order_release);
            m_pending_demo_seek_round.store(-1, std::memory_order_release);
            m_pending_demo_seek_generation.store(
                0, std::memory_order_release);
            m_native_demo_seek_guard_ticks.store(0,
                                                 std::memory_order_release);
            m_native_demo_seek_settle_ticks.store(0,
                                                  std::memory_order_release);
            m_native_demo_seek_settle_ms.store(-1,
                                               std::memory_order_release);
            m_native_demo_seek_settle_seq.store(-1,
                                                std::memory_order_release);
            m_native_demo_seek_settle_master.store(-1,
                                                   std::memory_order_release);
            m_native_demo_seek_settle_generation.store(
                0, std::memory_order_release);
            m_pending_demo_seek_retry_ticks = 0;
            clear_scrub_state();
            ReplayScrubDiag::clear_cached_demo_driver();
            ReplayScrubDiag::clear_cached_demo_time_source();
            m_last_round = -1;
            try
            {
                m_sim_store   .init(&m_pool, kSnapshotStride);
                m_il_store    .init(&m_pool, kIL_CaptureBytes);
                m_rdb_store   .init(&m_pool, kRDB_Bytes);
                m_extras_store.init(&m_pool, kExtras_Bytes);
            }
            catch (const std::bad_alloc&)
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[ReplayScrub] store staging alloc failed\n"));
            }
        }

        // Full teardown for module shutdown / destruction.  For the dedup
        // store this is the same as a logical drop - the RegionStore /
        // ChunkPool / TagTimeline destructors release the rest - but it
        // is kept as a named entry point because shutdown() and the
        // destructor call it.
        void free_ring() noexcept
        {
            drop_ring();
            m_have_last_counter = false;
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

        // Read ALuxBattleReplayPlayer.nTotalRounds (rounds in the recorded
        // match, ReplayPlayer+0x3B0).  Returns -1 if the actor isn't
        // resolvable.  The replay's final round is CurrentRound ==
        // nTotalRounds - 1; used by tick_generate_timeline to stop the
        // moment the last round ends.
        int32_t read_total_rounds() const noexcept
        {
            RC::Unreal::UObject* rp =
                ReplayScrubDiag::replay_player_ptr().get(
                    L"LuxBattleReplayPlayer");
            if (!rp) return -1;
            int32_t n = -1;
            if (!SafeReadInt32(reinterpret_cast<const uint8_t*>(rp)
                                   + kRP_TotalRounds_Off, &n))
                return -1;
            return n;
        }

        // Read g_LuxBattle_LastRoundResultType (i16 @ imageBase +
        // kRVA_LastRoundResultType).  0 = a round is live; non-zero = a
        // round has ended.  Returns 0 on any read failure so a transient
        // miss can never false-signal "round ended".
        int32_t read_last_round_result() const noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return 0;
            int16_t v = 0;
            if (!SafeReadInt16(reinterpret_cast<const void*>(
                                   base + kRVA_LastRoundResultType), &v))
                return 0;
            return v;
        }

        bool write_last_round_result(int16_t v) const noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            return SafeWriteBytes(reinterpret_cast<void*>(
                                      base + kRVA_LastRoundResultType),
                                  &v, sizeof(v));
        }

        // Read ALuxBattleReplayPlayer.bIsPlayingBack (1-byte bool @
        // ReplayPlayer+0x3D0).  Returns 1 while the engine is playing the
        // replay back, 0 once it has stopped, -1 if the actor isn't
        // resolvable.
        int32_t read_replay_is_playing_back() const noexcept
        {
            RC::Unreal::UObject* rp =
                ReplayScrubDiag::replay_player_ptr().get(
                    L"LuxBattleReplayPlayer");
            if (!rp) return -1;
            uint8_t b = 0;
            if (!SafeReadUInt8(reinterpret_cast<const uint8_t*>(rp)
                                   + kRP_IsPlayingBack_Off, &b))
                return -1;
            return b ? 1 : 0;
        }

        // True once either player has won enough rounds to decide the
        // match: per-chara round-win count (chara+0x1314, u16) reaches the
        // rounds-needed-to-win threshold (chara+0x1318, u32) - the same
        // test LuxBattle_EvaluateRoundResult @ 0x140385440 applies.
        // ReplayPlayer-independent: it reads the g_LuxBattle_CharaSlotP1 /
        // P2 globals directly, so it works even when the ReplayPlayer
        // actor (used for the round-index signal) can't be resolved.
        // Returns false on any unresolved pointer / read failure, and a
        // zero threshold is treated as "not configured" so it can never
        // false-fire before the match has been set up.
        bool match_decided() const noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            const uintptr_t slots[2] =
                { base + kRVA_CharaSlotP1, base + kRVA_CharaSlotP2 };
            for (uintptr_t slot_addr : slots)
            {
                void* chara = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(slot_addr),
                                 &chara)
                    || !chara)
                    continue;
                const uint8_t* c = reinterpret_cast<const uint8_t*>(chara);
                uint16_t wins   = 0;
                uint32_t needed = 0;
                if (SafeReadUInt16(c + kChara_RoundWins_Off,   &wins)
                    && SafeReadUInt32(c + kChara_RoundsToWin_Off, &needed)
                    && needed > 0 && needed < 100
                    && wins >= needed)
                    return true;
            }
            return false;
        }

        // True only if BOTH battle chara slots resolve to live chara
        // objects.  do_seek's snapshot restore (m_exec_read) writes deep
        // into these objects; restoring into freed/dead charas is the
        // scrub-back crash path.  The g_LuxBattle_CharaSlotP1/P2 globals
        // are NOT nulled on match teardown, so a non-null pointer alone
        // is not enough - the offset-0 vtable is verified against
        // PLAYER::vftable (set by LuxBattleChara_Ctor), which a freed-
        // and-reused heap block will not carry.  SEH-guarded so a
        // dangling pointer that faults on the vtable read just returns
        // false.  (This cannot detect a block freed but not yet reused;
        // the SEH guard around m_exec_read is the backstop for that.)
        bool charas_alive() const noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            const uintptr_t expect_vt = base + kRVA_CharaVTable;
            const uintptr_t slots[2] =
                { base + kRVA_CharaSlotP1, base + kRVA_CharaSlotP2 };
            for (uintptr_t slot_addr : slots)
            {
                void* chara = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(slot_addr),
                                 &chara)
                    || !chara)
                    return false;
                void* vt = nullptr;
                if (!SafeReadPtr(chara, &vt)
                    || reinterpret_cast<uintptr_t>(vt) != expect_vt)
                    return false;
            }
            return true;
        }

        static uint64_t elapsed_us(
            std::chrono::steady_clock::time_point a,
            std::chrono::steady_clock::time_point b) noexcept
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    b - a).count());
        }

        void reset_generation_profile() noexcept
        {
            m_gen_profile_frames.store(0, std::memory_order_release);
            m_gen_profile_total_us.store(0, std::memory_order_release);
            m_gen_profile_sim_us.store(0, std::memory_order_release);
            m_gen_profile_il_us.store(0, std::memory_order_release);
            m_gen_profile_rdb_us.store(0, std::memory_order_release);
            m_gen_profile_extras_us.store(0, std::memory_order_release);
            m_gen_profile_commit_us.store(0, std::memory_order_release);
        }

        void add_generation_profile_sample(
            uint64_t total_us, uint64_t sim_us, uint64_t il_us,
            uint64_t rdb_us, uint64_t extras_us,
            uint64_t commit_us) noexcept
        {
            m_gen_profile_frames.fetch_add(1, std::memory_order_acq_rel);
            m_gen_profile_total_us.fetch_add(total_us,
                                             std::memory_order_acq_rel);
            m_gen_profile_sim_us.fetch_add(sim_us,
                                           std::memory_order_acq_rel);
            m_gen_profile_il_us.fetch_add(il_us,
                                          std::memory_order_acq_rel);
            m_gen_profile_rdb_us.fetch_add(rdb_us,
                                           std::memory_order_acq_rel);
            m_gen_profile_extras_us.fetch_add(extras_us,
                                              std::memory_order_acq_rel);
            m_gen_profile_commit_us.fetch_add(commit_us,
                                              std::memory_order_acq_rel);
        }

        void log_generation_profile(const char* reason) noexcept
        {
            const TimelineGenProfile p = timeline_gen_profile();
            if (p.frames == 0) return;
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] Generate timeline profile ({}) - "
                "{} frames in {:.3f}s = {:.1f} ticks/s; avg capture "
                "{:.1f} us (sim {:.1f}, InputLog {:.1f}, RDB {:.1f}, "
                "extras {:.1f}, commit {:.1f})\n"),
                RC::to_generic_string(reason ? reason : "?"),
                p.frames, p.wall_seconds, p.ticks_per_second,
                p.avg_total_us, p.avg_sim_us, p.avg_inputlog_us,
                p.avg_rdb_us, p.avg_extras_us, p.avg_commit_us);
        }

        static uint64_t hash_bytes64(const uint8_t* p,
                                     size_t n) noexcept
        {
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; i < n; ++i)
            {
                h ^= p[i];
                h *= 1099511628211ull;
            }
            return h;
        }

        bool ensure_exp2_buffers() noexcept
        {
            try
            {
                if (m_exp2_sim_before.size() != kSnapshotStride)
                    m_exp2_sim_before.assign(kSnapshotStride, 0);
                if (m_exp2_sim_after.size() != kSnapshotStride)
                    m_exp2_sim_after.assign(kSnapshotStride, 0);
                if (m_exp2_il_before.size() != kIL_CaptureBytes)
                    m_exp2_il_before.assign(kIL_CaptureBytes, 0);
                if (m_exp2_rdb_before.size() != kRDB_Bytes)
                    m_exp2_rdb_before.assign(kRDB_Bytes, 0);
                if (m_exp2_extras_before.size() != kExtras_Bytes)
                    m_exp2_extras_before.assign(kExtras_Bytes, 0);
                return true;
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
        }

        void evaluate_battle_step_generation_end(
            const std::chrono::steady_clock::time_point& now) noexcept
        {
            const int32_t master = read_engine_master_clock();
            const int32_t round  = read_current_round();
            const int32_t last_master =
                m_timeline_gen_last_master.load(std::memory_order_acquire);
            const int32_t last_round = m_gen_last_round;
            const int32_t last_round_result = read_last_round_result();
            const int32_t is_playing_back = read_replay_is_playing_back();
            update_playback_result_guard_markers(
                round, master, last_round_result);

            if (std::chrono::duration<double>(now - m_gen_started_at)
                    .count() > kGenMaxSeconds)
            {
                stop_generate_timeline("safety-timeout", false);
                return;
            }
            if (GameMode::instance().current_presence()
                != GamePresence::Replay)
            {
                stop_generate_timeline("left-replay", false);
                return;
            }
            if (m_capture_ceiling_hit.load(std::memory_order_acquire))
            {
                stop_generate_timeline("memory-ceiling", true);
                return;
            }

            const int32_t tr_now = read_total_rounds();
            if (tr_now > 0) m_gen_total_rounds = tr_now;

            const bool in_final_round =
                (m_gen_seen_progress && m_gen_total_rounds > 0
                 && round >= 0 && round + 1 >= m_gen_total_rounds);
            if (in_final_round && last_round_result == 0)
            {
                m_gen_final_round_played = true;
                if (master >= kRoundBoundarySeekGuardMaster)
                {
                    const int32_t safe_seq = raw_latest_seq();
                    if (safe_seq >= 0)
                    {
                        if (m_gen_final_round_first_safe_seq < 0)
                            m_gen_final_round_first_safe_seq = safe_seq;
                        m_gen_final_round_last_safe_seq = safe_seq;
                    }
                }
            }
            if (m_gen_final_round_played && last_round_result != 0)
            {
                stop_generate_timeline("match-ended", true);
                return;
            }

            {
                const bool decided = match_decided();
                if (m_gen_seen_progress && !decided)
                    m_gen_match_undecided_seen = true;
                if (m_gen_match_undecided_seen && decided)
                {
                    stop_generate_timeline("match-decided", true);
                    return;
                }
            }

            if (is_playing_back == 1)
            {
                m_gen_seen_playing_back   = true;
                m_gen_playback_gone_ticks = 0;
            }
            else if (is_playing_back == 0 && m_gen_seen_playing_back)
            {
                ++m_gen_playback_gone_ticks;
            }
            if (m_gen_playback_gone_ticks > kGenPlaybackGoneTicks)
            {
                stop_generate_timeline("playback-ended", true);
                return;
            }

            if (round > m_gen_max_round) m_gen_max_round = round;

            const bool round_known =
                (round >= 0 && last_round >= 0);
            const bool round_advanced = round_known && round > last_round;
            const bool master_advanced =
                (master >= 0 && last_master >= 0 && master > last_master);
            const bool master_rolled_back =
                (master >= 0 && last_master >= 0 && master < last_master);

            const bool round_looped =
                (round >= 0 && m_gen_max_round >= 0
                 && round < m_gen_max_round);
            if (round_looped
                || (master_rolled_back && round_known && !round_advanced))
            {
                stop_generate_timeline("replay-looped", true);
                return;
            }

            if (master_advanced || round_advanced)
            {
                m_gen_last_advance  = now;
                m_gen_seen_progress = true;
            }

            if (master >= 0)
                m_timeline_gen_last_master.store(
                    master, std::memory_order_release);
            if (round >= 0)
                m_gen_last_round = round;

            const double stuck_limit = in_final_round
                ? kGenFinalRoundStuckSeconds : kGenStuckSeconds;
            if (std::chrono::duration<double>(now - m_gen_last_advance)
                    .count() > stuck_limit)
            {
                stop_generate_timeline(
                    in_final_round ? "match-ended" : "end-of-recording",
                    true);
                return;
            }
        }

        bool run_battle_step_generate_one_frame() noexcept
        {
            auto on_transient_failure = [this](const char* reason) noexcept
            {
                const int32_t count =
                    ++m_exp2_transient_fail_count;
                if (count <= kExp2TransientFailureBudget)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(STR(
                        "[ReplayScrub.EXP2] direct-step generation transient "
                        "failure {} / {} for {}\n"),
                        count, kExp2TransientFailureBudget,
                        RC::to_generic_string(reason ? reason : "?"));
                }
                return count > kExp2TransientFailureBudget;
            };
            auto on_recovered = [this]() noexcept
            {
                m_exp2_transient_fail_count = 0;
            };

            if (!charas_alive())
            {
                stop_generate_timeline("battle-state-loss", false);
                return false;
            }
            if (m_gen_request.exchange(kGenReqNone,
                                       std::memory_order_acq_rel)
                == kGenReqStop)
            {
                stop_generate_timeline("user", false);
                return false;
            }
            if (!is_initialized() || !m_exec_write || !m_exec_read)
            {
                stop_generate_timeline("direct-generation-initialized-failed",
                                       false);
                return false;
            }
            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                stop_generate_timeline("direct-generation-no-imagebase",
                                       false);
                return false;
            }
            uint64_t input[2] = {};
            uint8_t camera_args[24] = {};
            const bool input_ok =
                SafeReadBytes(reinterpret_cast<const void*>(
                                 base + kRVA_LatestEngineInput),
                             input, sizeof(input));
            const bool camera_ok =
                SafeReadBytes(reinterpret_cast<const void*>(
                                 base + kRVA_PerFrameCameraArgs),
                             camera_args, sizeof(camera_args));
            if (!input_ok || !camera_ok)
            {
                if (on_transient_failure("engine input/camera read failed"))
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.EXP2] direct-step generation aborted - "
                        "engine input/camera reads failed repeatedly "
                        "(input_ok={} camera_ok={})\n"),
                        input_ok ? 1 : 0, camera_ok ? 1 : 0);
                    stop_generate_timeline("direct-step-read-failed", false);
                }
                return true;
            }
            const int32_t master_before = read_engine_master_clock();
            if (master_before < 0)
            {
                if (on_transient_failure("master clock unreadable"))
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.EXP2] direct-step generation "
                        "aborted - master clock unreadable repeatedly\n"));
                    stop_generate_timeline("direct-step-master-clock-unreadable",
                                           false);
                }
                return true;
            }

            uintptr_t args[3] = {
                reinterpret_cast<uintptr_t>(&input[0]),
                reinterpret_cast<uintptr_t>(&input[1]),
                reinterpret_cast<uintptr_t>(camera_args)
            };
            const bool step_ok =
                SafeInvokePerFrameTick(m_per_frame_tick_bypass, args);
            if (!step_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.EXP2] direct-step generation frame FAILED\n"));
                stop_generate_timeline("direct-step-failed", false);
                return false;
            }

            const auto t1 = std::chrono::steady_clock::now();
            uint32_t wall_after = 0;
            if (!read_frame_counter(wall_after))
            {
                if (on_transient_failure("frame counter unreadable"))
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.EXP2] direct-step generation aborted - "
                        "frame counter unreadable repeatedly\n"));
                    stop_generate_timeline("direct-step-framecounter-unreadable",
                                           false);
                }
                return true;
            }

            const bool should_commit =
                !m_gen_battle_step_probe.load(std::memory_order_acquire);
            if (!capture_snapshot(static_cast<int32_t>(wall_after),
                                 should_commit))
            {
                if (m_capture_ceiling_hit.load(std::memory_order_acquire))
                {
                    stop_generate_timeline("memory-ceiling", true);
                }
                else
                {
                    if (on_transient_failure("capture snapshot failed"))
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub.EXP2] direct-step generation "
                            "aborted - snapshot capture failed repeatedly\n"));
                        stop_generate_timeline("direct-step-capture-failed",
                                               false);
                    }
                    return true;
                }
            }
            if (!should_commit)
                return false;

            on_recovered();
            evaluate_battle_step_generation_end(t1);
            return m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating);
        }

        bool capture_snapshot(int32_t wall_tag, bool commit = true) noexcept
        {
            if (!is_initialized() || !m_exec_write) return false;
            const bool profile_generation =
                m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating);
            const bool do_commit = commit && !m_gen_battle_step_probe.load(
                std::memory_order_acquire);
            const auto t_total0 = std::chrono::steady_clock::now();
            uint64_t sim_us = 0;
            uint64_t il_us = 0;
            uint64_t rdb_us = 0;
            uint64_t extras_us = 0;
            uint64_t commit_us = 0;

            if (m_capture_ceiling_hit.load(std::memory_order_acquire))
                return false;

            if (store_bytes() >= kMaxStoreBytes)
            {
                if (do_commit)
                    m_capture_ceiling_hit.store(true,
                        std::memory_order_release);
                else
                    m_capture_ceiling_hit.store(
                        m_capture_ceiling_hit.load(std::memory_order_acquire)
                        || (store_bytes() >= kMaxStoreBytes),
                        std::memory_order_release);

                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] capture stopped - {} MB snapshot "
                    "ceiling reached; timeline holds {} frames\n"),
                    kMaxStoreBytes / (1024ull * 1024ull), m_tags.count());
                return false;
            }

            auto t0 = std::chrono::steady_clock::now();
            m_shim.retarget(m_sim_store.scratch(), kSnapshotStride);
            if (!SafeInvokeExec(m_exec_write, &m_shim))
                return false;
            auto t1 = std::chrono::steady_clock::now();
            sim_us = elapsed_us(t0, t1);

            t0 = std::chrono::steady_clock::now();
            if (!capture_input_cache(m_il_store.scratch()))
                return false;
            t1 = std::chrono::steady_clock::now();
            il_us = elapsed_us(t0, t1);

            t0 = std::chrono::steady_clock::now();
            capture_replay_data_block(m_rdb_store.scratch());
            t1 = std::chrono::steady_clock::now();
            rdb_us = elapsed_us(t0, t1);
            t0 = std::chrono::steady_clock::now();
            if (!capture_extras(m_extras_store.scratch()))
                return false;
            t1 = std::chrono::steady_clock::now();
            extras_us = elapsed_us(t0, t1);

            int32_t master_tag = -1;
            {
                const uintptr_t off =
                    kIL_nMasterClock_Off - kIL_CaptureStart_Off;
                std::memcpy(&master_tag, m_il_store.scratch() + off,
                            sizeof(master_tag));
            }
            if (master_tag < 0) return false;

            int32_t last_frame_id_tag = -1;
            {
                const uintptr_t off =
                    kIL_nLastFrameID_Off - kIL_CaptureStart_Off;
                std::memcpy(&last_frame_id_tag,
                            m_il_store.scratch() + off,
                            sizeof(last_frame_id_tag));
            }

            int32_t round_tag = 0;
            std::memcpy(&round_tag, m_extras_store.scratch()
                        + kExtras_Off_RP_CurrentRound, sizeof(round_tag));
            if (do_commit)
            {
                const int32_t next_seq =
                    static_cast<int32_t>(m_tags.count());
                capture_sc6_round_reset_snapshot_if_needed(
                    round_tag, next_seq, master_tag, last_frame_id_tag);
            }

            int32_t demo_time_ms = -1;
            ReplayScrubDiag::DemoTimeSourceSnap demo_time_snap{};
            {
                demo_time_snap =
                    ReplayScrubDiag::read_demo_time_source_fast();
                if (!(demo_time_snap.readable && demo_time_snap.time_sane)
                    && profile_generation && do_commit)
                {
                    const size_t committed_count = m_tags.count();
                    if (committed_count < 5
                        || (committed_count % 300u) == 0u)
                    {
                        demo_time_snap =
                            ReplayScrubDiag::read_demo_time_source();
                        if (demo_time_snap.readable
                            && demo_time_snap.time_sane
                            && !m_gen_demo_time_recovered_logged)
                        {
                            m_gen_demo_time_recovered_logged = true;
                            RC::Output::send<RC::LogLevel::Default>(STR(
                                "[ReplayScrub] Generate timeline native "
                                "demo time source recovered during capture: "
                                "source={} ptr=0x{:X} cur={:.3f}s "
                                "total={:.3f}s after {} committed tick(s)\n"),
                                RC::to_generic_string(
                                    ReplayScrubDiag::demo_driver_source_name(
                                        demo_time_snap.source)),
                                demo_time_snap.source_ptr,
                                demo_time_snap.raw_demo_cur_time,
                                demo_time_snap.raw_demo_total_time,
                                committed_count);
                        }
                    }
                }
                if (demo_time_snap.readable && demo_time_snap.time_sane)
                {
                    demo_time_ms = static_cast<int32_t>(
                        demo_time_snap.raw_demo_cur_time * 1000.0f + 0.5f);
                }
            }
            if (profile_generation && do_commit && demo_time_ms < 0
                && !m_gen_missing_demo_time_logged)
            {
                m_gen_missing_demo_time_logged = true;
                if (demo_time_snap.time_fields_readable
                    && !demo_time_snap.time_sane)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] Generate timeline found native "
                        "demo time source 0x{:X}, but DemoCurrentTime/"
                        "DemoTotalTime are not sane (cur={:.3f}s "
                        "total={:.3f}s); committed ticks will store "
                        "demo_ms=-1 until the time source is sane\n"),
                        demo_time_snap.source_ptr,
                        demo_time_snap.raw_demo_cur_time,
                        demo_time_snap.raw_demo_total_time);
                }
                else
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] Generate timeline has no native "
                        "demo time source yet; committed ticks will store "
                        "demo_ms=-1 until the fast time-source resolver "
                        "finds sane DemoCurrentTime/DemoTotalTime fields\n"));
                }
            }

            if (do_commit)
            {
                try
                {
                    t0 = std::chrono::steady_clock::now();
                    const bool c0 = m_sim_store.commit();
                    const bool c1 = m_il_store.commit();
                    const bool c2 = m_rdb_store.commit();
                    const bool c3 = m_extras_store.commit();
                    t1 = std::chrono::steady_clock::now();
                    commit_us = elapsed_us(t0, t1);
                    if (!(c0 && c1 && c2 && c3))
                    {
                        static std::atomic<bool> s_selftest_warned{false};
                        if (!s_selftest_warned.exchange(
                                true, std::memory_order_relaxed))
                            RC::Output::send<RC::LogLevel::Error>(STR(
                                "[ReplayScrub] ChunkPool self-test FAILED on "
                                "commit - snapshot dedup may be corrupt\n"));
                    }
                }
                catch (const std::bad_alloc&)
                {
                    if (!m_capture_ceiling_hit.exchange(
                            true, std::memory_order_acq_rel))
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[ReplayScrub] capture stopped - out of memory "
                            "folding snapshot; timeline holds {} frames\n"),
                            m_tags.count());
                    return false;
                }

                const int32_t seq_tag = static_cast<int32_t>(m_tags.count());
                m_tags.append(seq_tag, round_tag, wall_tag, master_tag,
                              demo_time_ms);
                (void)commit_oracle_frame(seq_tag, round_tag, wall_tag,
                                          master_tag);
                if (profile_generation)
                {
                    const auto t_total1 = std::chrono::steady_clock::now();
                    add_generation_profile_sample(
                        elapsed_us(t_total0, t_total1), sim_us, il_us,
                        rdb_us, extras_us, commit_us);
                }
                static std::atomic<bool> s_logged{false};
                if (!s_logged.exchange(true, std::memory_order_relaxed))
                {
                    RC::Output::send<RC::LogLevel::Default>(
                        STR("[ReplayScrub] first capture: seq={} round={} "
                            "wall_tag={} master_tag={} shim cursor after "
                            "Exec={} bytes demo_ms={}\n"),
                        seq_tag, round_tag, wall_tag, master_tag,
                        static_cast<unsigned long long>(m_shim.cursor()),
                        demo_time_ms);
                    ReplayTraceFields f;
                    f.integer("seq", seq_tag)
                     .integer("round", round_tag)
                     .integer("wall_tag", wall_tag)
                     .integer("master", master_tag)
                     .integer("demo_ms", demo_time_ms)
                     .uinteger("shim_cursor",
                               static_cast<uint64_t>(m_shim.cursor()));
                    ReplayDebugTrace::instance().event(
                        "capture_first_frame", f);
                }
            }

            return true;
        }

        ReplayFrameOracleSnap read_oracle_frame_snap(
            int32_t seq_tag,
            int32_t round_tag,
            int32_t wall_tag,
            int32_t master_tag) noexcept
        {
            ReplayFrameOracleSnap s{};
            s.seq = seq_tag;
            s.round = round_tag;
            s.wall = wall_tag;
            s.master = master_tag;
            s.last_round_result = read_last_round_result();
            s.p1 =
                ReplayScrubDiag::read_chara_movevm(0);
            s.p2 =
                ReplayScrubDiag::read_chara_movevm(1);
            ReplayScrubDiag::LatestEngineInputSnap input =
                ReplayScrubDiag::read_latest_engine_input();
            s.p1_input = input.p1_input;
            s.p2_input = input.p2_input;
            const uintptr_t base = NativeBinding::imageBase();
            if (base)
            {
                uint32_t rng0 = 0;
                uint32_t rng1 = 0;
                SafeReadUInt32(reinterpret_cast<const void*>(base + kRVA_LfsrState),
                               &rng0);
                SafeReadUInt32(reinterpret_cast<const void*>(base + kRVA_LfsrState + 4),
                               &rng1);
                s.rng_state = static_cast<uint64_t>(rng0)
                    | (static_cast<uint64_t>(rng1) << 32);
            }
            s.valid = s.round >= 0 && s.master >= 0
                && s.p1.readable && s.p2.readable && input.readable;
            return s;
        }

        void trace_oracle_frame(
            const ReplayFrameOracleSnap& snap,
            const char* event_name = "oracle_frame") noexcept
        {
            if (!ReplayDebugTrace::instance().enabled()) return;
            ReplayTraceFields f;
            f.integer("seq", snap.seq)
             .integer("round", snap.round)
             .integer("wall_tag", snap.wall)
             .integer("master", snap.master)
             .integer("last_round_result", snap.last_round_result)
             .uinteger("rng_state", snap.rng_state)
             .boolean("valid", snap.valid);
            add_oracle_player_fields(f, "p1", snap.p1, snap.p1_input);
            add_oracle_player_fields(f, "p2", snap.p2, snap.p2_input);
            ReplayDebugTrace::instance().event(
                event_name ? event_name : "oracle_frame", f);
        }

        bool commit_oracle_frame(int32_t seq_tag, int32_t round_tag,
                                 int32_t wall_tag,
                                 int32_t master_tag) noexcept
        {
            ReplayFrameOracleSnap snap =
                read_oracle_frame_snap(seq_tag, round_tag,
                                       wall_tag, master_tag);
            try
            {
                const size_t idx = static_cast<size_t>(seq_tag);
                if (m_oracle_frames.size() <= idx)
                    m_oracle_frames.resize(idx + 1);
                m_oracle_frames[idx] = snap;
            }
            catch (const std::bad_alloc&)
            {
                snap.valid = false;
                m_oracle_capture_failed.store(
                    true, std::memory_order_release);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] oracle store allocation failed; "
                    "captured seek verification will be unavailable\n"));
                trace_oracle_frame(snap);
                return false;
            }
            if (!snap.valid)
            {
                m_oracle_capture_failed.store(
                    true, std::memory_order_release);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] oracle capture invalid seq={} "
                    "round={} master={} p1={} p2={}; generated "
                    "timeline will not be seekable\n"),
                    seq_tag, round_tag, master_tag,
                    snap.p1.readable ? 1 : 0,
                    snap.p2.readable ? 1 : 0);
            }
            trace_oracle_frame(snap);
            return snap.valid;
        }

        static void add_oracle_player_fields(ReplayTraceFields& f,
                                             const char* prefix,
                                             const ReplayScrubDiag::CharaMoveVmSnap& s,
                                             uint64_t input) noexcept
        {
            std::string p(prefix ? prefix : "p");
            auto key = [&p](const char* suffix) { return p + suffix; };
            f.hex(key("_chara").c_str(), s.chara_ptr)
             .uinteger(key("_readable").c_str(), s.readable ? 1u : 0u)
             .real(key("_pos_x").c_str(), s.pos_x)
             .real(key("_pos_y").c_str(), s.pos_y)
             .real(key("_pos_z").c_str(), s.pos_z)
             .real(key("_facing").c_str(), s.facing)
             .integer(key("_life").c_str(), static_cast<int32_t>(s.health))
             .uinteger(key("_move_id").c_str(), s.current_move_id)
             .uinteger(key("_move_frame").c_str(), s.current_move_frame)
             .real(key("_clip_frame").c_str(), s.current_clip_frame)
             .uinteger(key("_hit_state").c_str(),
                       s.in_hitstun ? 1u : (s.in_blockstun ? 2u : 0u))
             .hex(key("_active_attack_cell").c_str(), s.active_attack_cell)
             .uinteger(key("_input").c_str(), input);
        }

        static float abs_float(float v) noexcept
        {
            return v < 0.0f ? -v : v;
        }

        static bool oracle_float_equal(float a, float b,
                                       float tolerance = 0.001f) noexcept
        {
            return abs_float(a - b) <= tolerance;
        }

        bool compare_oracle_player(
            int player,
            const ReplayScrubDiag::CharaMoveVmSnap& expected,
            const ReplayScrubDiag::CharaMoveVmSnap& live,
            uint64_t expected_input,
            uint64_t live_input,
            CapturedFrameOracleCompareReport& out) noexcept
        {
            auto fail_u64 = [&](const char* field,
                                uint64_t expected_value,
                                uint64_t live_value) noexcept -> bool
            {
                out.player = player;
                out.field = field;
                out.expected_u64 = expected_value;
                out.live_u64 = live_value;
                out.reason = field;
                return false;
            };
            auto fail_float = [&](const char* field,
                                  float expected_value,
                                  float live_value) noexcept -> bool
            {
                out.player = player;
                out.field = field;
                out.expected_float = expected_value;
                out.live_float = live_value;
                out.reason = field;
                return false;
            };
            auto diag_u64 = [&](const char* field,
                                uint64_t expected_value,
                                uint64_t live_value) noexcept
            {
                if (expected_value == live_value) return;
                ReplayTraceFields f;
                f.string("label", m_sc6_seek_job.label
                             ? m_sc6_seek_job.label : "?")
                 .integer("player", player)
                 .string("field", field)
                 .hex("expected_u64", expected_value)
                 .hex("live_u64", live_value)
                 .boolean("play_gate", false)
                 .string("reason", "diagnostic-only-unproven-authority");
                ReplayDebugTrace::instance().event(
                    "restore_integrity_oracle_diagnostic", f);
            };
            auto diag_float = [&](const char* field,
                                  float expected_value,
                                  float live_value) noexcept
            {
                if (oracle_float_equal(expected_value, live_value))
                    return;
                ReplayTraceFields f;
                f.string("label", m_sc6_seek_job.label
                             ? m_sc6_seek_job.label : "?")
                 .integer("player", player)
                 .string("field", field)
                 .real("expected_float", expected_value)
                 .real("live_float", live_value)
                 .boolean("play_gate", false)
                 .string("reason", "diagnostic-only-unproven-authority");
                ReplayDebugTrace::instance().event(
                    "restore_integrity_oracle_diagnostic", f);
            };

            if (!expected.readable || !live.readable)
                return fail_u64("chara-readable",
                                expected.readable ? 1u : 0u,
                                live.readable ? 1u : 0u);
            const uint32_t expected_input_low =
                static_cast<uint32_t>(expected_input & 0xFFFFFFFFu);
            const uint32_t live_input_low =
                static_cast<uint32_t>(live_input & 0xFFFFFFFFu);
            if (expected_input_low != live_input_low)
                return fail_u64("input", expected_input_low, live_input_low);
            diag_u64("input-high-bits", expected_input & 0xFFFFFFFF00000000ull,
                     live_input & 0xFFFFFFFF00000000ull);
            // Move identity/frame are gameplay semantics.  A seek that
            // advances clocks but leaves either player on the wrong move can
            // look landed, then resume with visibly wrong inputs/actions.
            if (expected.current_move_id != live.current_move_id)
                return fail_u64("move-id", expected.current_move_id,
                                live.current_move_id);
            if (expected.current_move_frame != live.current_move_frame)
                return fail_u64("move-frame", expected.current_move_frame,
                                live.current_move_frame);
            // Clip frame and active attack cell still have pointer/rebuild
            // noise in traces, so keep them diagnostic until their authority
            // is proven separately.
            diag_float("clip-frame", expected.current_clip_frame,
                       live.current_clip_frame);
            if (!oracle_float_equal(expected.pos_x, live.pos_x))
                return fail_float("pos-x", expected.pos_x, live.pos_x);
            if (!oracle_float_equal(expected.pos_y, live.pos_y))
                return fail_float("pos-y", expected.pos_y, live.pos_y);
            if (!oracle_float_equal(expected.pos_z, live.pos_z))
                return fail_float("pos-z", expected.pos_z, live.pos_z);
            if (!oracle_float_equal(expected.vel_x, live.vel_x))
                return fail_float("vel-x", expected.vel_x, live.vel_x);
            if (!oracle_float_equal(expected.vel_y, live.vel_y))
                return fail_float("vel-y", expected.vel_y, live.vel_y);
            if (!oracle_float_equal(expected.vel_z, live.vel_z))
                return fail_float("vel-z", expected.vel_z, live.vel_z);
            if (!oracle_float_equal(expected.facing, live.facing))
                return fail_float("facing", expected.facing, live.facing);
            if (!oracle_float_equal(expected.health, live.health))
                return fail_float("health", expected.health, live.health);
            if (expected.vm_paused != live.vm_paused)
                return fail_u64("vm-paused", expected.vm_paused,
                                live.vm_paused);
            if (expected.input_freeze_gate != live.input_freeze_gate)
                return fail_u64("input-freeze", expected.input_freeze_gate,
                                live.input_freeze_gate);
            if (expected.in_hitstun != live.in_hitstun)
                return fail_u64("hitstun", expected.in_hitstun,
                                live.in_hitstun);
            if (expected.in_blockstun != live.in_blockstun)
                return fail_u64("blockstun", expected.in_blockstun,
                                live.in_blockstun);
            diag_u64("active-attack-cell", expected.active_attack_cell,
                     live.active_attack_cell);
            return true;
        }

        bool compare_oracle_to_captured_tick(
            int32_t expected_tick,
            CapturedFrameOracleCompareReport& out) noexcept
        {
            out = CapturedFrameOracleCompareReport{};
            out.expected_tick = expected_tick;
            if (expected_tick < 0
                || static_cast<size_t>(expected_tick)
                    >= m_oracle_frames.size())
            {
                out.reason = "oracle-missing";
                return false;
            }

            const ReplayFrameOracleSnap& expected =
                m_oracle_frames[static_cast<size_t>(expected_tick)];
            out.expected_valid = expected.valid;
            out.expected_seq = expected.seq;
            out.expected_round = expected.round;
            out.expected_master = expected.master;
            if (!expected.valid)
            {
                out.reason = "oracle-invalid";
                return false;
            }

            ReplayFrameOracleSnap live = read_oracle_frame_snap(
                expected.seq, read_current_round(), expected.wall,
                read_engine_master_clock());
            out.live_valid = live.valid;
            out.live_round = live.round;
            out.live_master = live.master;
            trace_oracle_frame(live, "oracle_live_after_restore");
            if (!live.valid)
            {
                out.reason = "live-oracle-invalid";
                return false;
            }
            if (live.round != expected.round)
            {
                out.field = "round";
                out.expected_u64 = static_cast<uint64_t>(expected.round);
                out.live_u64 = static_cast<uint64_t>(live.round);
                out.reason = "round";
                return false;
            }
            if (live.master != expected.master)
            {
                out.field = "master";
                out.expected_u64 = static_cast<uint64_t>(expected.master);
                out.live_u64 = static_cast<uint64_t>(live.master);
                out.reason = "master";
                return false;
            }
            out.last_round_result_match =
                expected.last_round_result == live.last_round_result;
            if (!out.last_round_result_match)
            {
                out.field = "last-round-result";
                out.expected_u64 =
                    static_cast<uint64_t>(expected.last_round_result);
                out.live_u64 =
                    static_cast<uint64_t>(live.last_round_result);
                out.reason = "last-round-result";
                return false;
            }
            out.rng_match = expected.rng_state == live.rng_state;
            if (!out.rng_match)
            {
                out.field = "rng-state";
                out.expected_u64 = expected.rng_state;
                out.live_u64 = live.rng_state;
                out.reason = "rng-state";
                return false;
            }
            out.p1_match = compare_oracle_player(
                1, expected.p1, live.p1, expected.p1_input,
                live.p1_input, out);
            if (!out.p1_match) return false;
            out.p2_match = compare_oracle_player(
                2, expected.p2, live.p2, expected.p2_input,
                live.p2_input, out);
            if (!out.p2_match) return false;
            out.input_match = true;
            out.ok = true;
            out.reason = "ok";
            return true;
        }

        void trace_captured_oracle_compare(
            const CapturedFrameOracleCompareReport& report) noexcept
        {
            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .integer("expected_seq", report.expected_seq)
             .integer("expected_tick", report.expected_tick)
             .integer("expected_round", report.expected_round)
             .integer("expected_master", report.expected_master)
             .integer("live_round", report.live_round)
             .integer("live_master", report.live_master)
             .integer("player", report.player)
             .string("field", report.field ? report.field : "none")
             .string("reason", report.reason ? report.reason : "?")
             .hex("expected_u64", report.expected_u64)
             .hex("live_u64", report.live_u64)
             .real("expected_float", report.expected_float)
             .real("live_float", report.live_float)
             .boolean("expected_valid", report.expected_valid)
             .boolean("live_valid", report.live_valid)
             .boolean("p1_match", report.p1_match)
             .boolean("p2_match", report.p2_match)
             .boolean("input_match", report.input_match)
             .boolean("rng_match", report.rng_match)
             .boolean("last_round_result_match",
                      report.last_round_result_match)
             .boolean("ok", report.ok);
            ReplayDebugTrace::instance().event(
                "captured_oracle_compare", f);
            ReplayDebugTrace::instance().event(
                "restore_integrity_oracle", f);
        }

        void run_battle_step_generate_slice() noexcept
        {
            const auto slice_start = std::chrono::steady_clock::now();
            for (int32_t i = 0; i < kExp2MaxFramesPerSlice; ++i)
            {
                if (!m_gen_battle_step_generate.load(std::memory_order_acquire))
                    return;
                if (!run_battle_step_generate_one_frame())
                    return;
                if (elapsed_us(slice_start, std::chrono::steady_clock::now()) >
                    kExp2SliceBudgetUs
                    && i > 0)
                {
                    return;
                }
            }
        }

        void run_battle_step_probe() noexcept
        {
            if (!is_initialized() || !m_exec_write || !m_exec_read)
                return;
            if (GameMode::instance().current_presence()
                != GamePresence::Replay)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.EXP2] direct-step probe ignored - "
                    "not in the Replay viewer\n"));
                return;
            }
            if (!charas_alive())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.EXP2] direct-step probe ignored - "
                    "battle charas are not both alive\n"));
                return;
            }
            if (!resolve_per_frame_tick_bypass() || !ensure_exp2_buffers())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.EXP2] direct-step probe failed - "
                    "could not prepare trampoline/buffers\n"));
                return;
            }

            m_gen_battle_step_probe.store(true,
                                          std::memory_order_release);
            const auto t0 = std::chrono::steady_clock::now();

            uint32_t wall_before = 0;
            read_frame_counter(wall_before);
            const int32_t master_before = read_engine_master_clock();

            uint64_t input[2] = {};
            uint8_t camera_args[24] = {};
            const uintptr_t base = NativeBinding::imageBase();
            if (base)
            {
                SafeReadBytes(reinterpret_cast<const void*>(
                                  base + kRVA_LatestEngineInput),
                              input, sizeof(input));
                SafeReadBytes(reinterpret_cast<const void*>(
                                  base + kRVA_PerFrameCameraArgs),
                              camera_args, sizeof(camera_args));
            }

            std::memset(m_exp2_sim_before.data(), 0, kSnapshotStride);
            std::memset(m_exp2_sim_after.data(), 0, kSnapshotStride);
            std::memset(m_exp2_il_before.data(), 0, kIL_CaptureBytes);
            std::memset(m_exp2_rdb_before.data(), 0, kRDB_Bytes);
            std::memset(m_exp2_extras_before.data(), 0, kExtras_Bytes);

            bool pre_ok = true;
            m_shim.retarget(m_exp2_sim_before.data(), kSnapshotStride);
            pre_ok &= SafeInvokeExec(m_exec_write, &m_shim);
            pre_ok &= capture_input_cache(m_exp2_il_before.data());
            pre_ok &= capture_replay_data_block(m_exp2_rdb_before.data());
            pre_ok &= capture_extras(m_exp2_extras_before.data());
            if (!pre_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.EXP2] direct-step probe aborted - "
                    "pre-step state capture failed (wall={} master={})\n"),
                    wall_before, master_before);
                m_gen_battle_step_probe.store(false,
                                              std::memory_order_release);
                return;
            }

            const uint64_t pre_hash = hash_bytes64(
                m_exp2_sim_before.data(), kSnapshotStride);

            uintptr_t args[3] = {
                reinterpret_cast<uintptr_t>(&input[0]),
                reinterpret_cast<uintptr_t>(&input[1]),
                reinterpret_cast<uintptr_t>(camera_args)
            };
            const auto t_step0 = std::chrono::steady_clock::now();
            const bool step_ok =
                SafeInvokePerFrameTick(m_per_frame_tick_bypass, args);
            const auto t_step1 = std::chrono::steady_clock::now();

            m_shim.retarget(m_exp2_sim_after.data(), kSnapshotStride);
            const bool post_capture_ok =
                SafeInvokeExec(m_exec_write, &m_shim);
            const uint64_t post_hash = hash_bytes64(
                m_exp2_sim_after.data(), kSnapshotStride);

            uint32_t wall_after = 0;
            read_frame_counter(wall_after);
            const int32_t master_after = read_engine_master_clock();

            m_shim.retarget(m_exp2_sim_before.data(), kSnapshotStride);
            bool restore_ok = SafeInvokeExec(m_exec_read, &m_shim);
            restore_ok &= restore_input_cache(m_exp2_il_before.data());
            restore_ok &= restore_replay_data_block(m_exp2_rdb_before.data());
            if (charas_alive())
            {
                restore_ok &= restore_extras(
                    m_exp2_extras_before.data());
            }
            else
            {
                restore_ok = false;
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.EXP2] skipped extras restore - battle "
                    "charas changed or tore down during direct step\n"));
            }
            if (base)
            {
                restore_ok &= SafeWriteUInt32(
                    reinterpret_cast<void*>(base + kRVA_FrameCounter),
                    wall_before);
                restore_ok &= SafeWriteBytes(
                    reinterpret_cast<void*>(base + kRVA_LatestEngineInput),
                    input, sizeof(input));
                restore_ok &= SafeWriteBytes(
                    reinterpret_cast<void*>(base + kRVA_PerFrameCameraArgs),
                    camera_args, sizeof(camera_args));
            }

            const auto t1 = std::chrono::steady_clock::now();
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.EXP2] direct PerFrameTick probe {} - "
                "post_capture={} restore={} wall {}->{} master {}->{} input "
                "p1=0x{:X} p2=0x{:X} sim_hash 0x{:016X}->0x{:016X} "
                "step={} us total={} us\n"),
                RC::to_generic_string(step_ok ? "OK" : "FAILED"),
                post_capture_ok ? 1 : 0,
                restore_ok ? 1 : 0,
                wall_before, wall_after, master_before, master_after,
                input[0], input[1], pre_hash, post_hash,
                elapsed_us(t_step0, t_step1), elapsed_us(t0, t1));

            m_gen_battle_step_probe.store(false,
                                          std::memory_order_release);
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

        int32_t read_battle_manager_master_clock() noexcept
        {
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return -1;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            int32_t master = -1;
            SafeReadInt32(bm + kBM_nReplayLastApplied_Off, &master);
            return master;
        }

        bool read_battle_manager_status(uint8_t& status) noexcept
        {
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return false;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            return SafeReadUInt8(bm + kBM_bStatusByte_Off, &status);
        }

        bool read_battle_manager_main_state(uint8_t& main_state) noexcept
        {
            RC::Unreal::UObject* bm_obj = m_bm_ptr.get(L"LuxBattleManager");
            if (!bm_obj) return false;
            uint8_t* bm = reinterpret_cast<uint8_t*>(bm_obj);
            return SafeReadUInt8(bm + kBM_bMainStateMachineByte_Off,
                                 &main_state);
        }

        bool read_captured_bm_play_gate_for_tick(
            int32_t tick,
            uint8_t& main_state,
            uint8_t& status) noexcept
        {
            main_state = 0;
            status = 0;
            if (tick < 0) return false;
            const uint8_t* extras =
                m_extras_store.gather(static_cast<size_t>(tick));
            if (!extras) return false;
            main_state = extras[kExtras_Off_BM_MainState];
            status = extras[kExtras_Off_BM_StatusByte];
            return true;
        }

        bool resume_play_if_battle_status_active(const char* label) noexcept
        {
            uint8_t bm_main_state = 0;
            uint8_t bm_status = 0;
            if (!read_battle_manager_main_state(bm_main_state)
                || bm_main_state != kBM_MainStateActiveBattle
                || !read_battle_manager_status(bm_status)
                || bm_status != kBM_StatusActiveBattle)
            {
                m_ui_wants_play.store(false, std::memory_order_release);
                m_paused.store(true, std::memory_order_release);
                m_hold_kind.store(
                    static_cast<int32_t>(ReplayScrubHoldKind::RestoredFrameHold),
                    std::memory_order_release);
                publish_native_status(
                    NativeSeekStatus::Failed,
                    NativeSeekFailure::BattleManagerStatusNotActive);
                publish_mode(ScrubMode::NativeSeekFailed);

                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] play blocked: label={} requested_seq={} "
                    "landed_seq={} bm.main=0x{:X} bm.status=0x{:X} "
                    "required_main=0x{:X} required_status=0x{:X}\n"),
                    RC::to_generic_string(label ? label : "PLAY"),
                    m_ui_requested_seq.load(std::memory_order_acquire),
                    m_last_seek_target.load(std::memory_order_acquire),
                    static_cast<unsigned>(bm_main_state),
                    static_cast<unsigned>(bm_status),
                    static_cast<unsigned>(kBM_MainStateActiveBattle),
                    static_cast<unsigned>(kBM_StatusActiveBattle));

                ReplayTraceFields f;
                f.string("label", label ? label : "PLAY")
                 .integer("requested_seq",
                          m_ui_requested_seq.load(std::memory_order_acquire))
                  .integer("landed_seq",
                           m_last_seek_target.load(std::memory_order_acquire))
                  .uinteger("bm_main_state", bm_main_state)
                  .uinteger("bm_status", bm_status)
                  .uinteger("required_main_state", kBM_MainStateActiveBattle)
                  .uinteger("required_status", kBM_StatusActiveBattle)
                 .string("failure", native_seek_failure_name(
                     NativeSeekFailure::BattleManagerStatusNotActive));
                ReplayDebugTrace::instance().event(
                    "play_blocked_bm_status", f);
                return false;
            }

            m_ui_wants_play.store(false, std::memory_order_release);
            m_paused.store(false, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::None),
                std::memory_order_release);
            m_native_demo_seek_settle_ticks.store(
                0, std::memory_order_release);
            publish_mode(ScrubMode::Playing);
            publish_native_status(NativeSeekStatus::Landed);
            return true;
        }

        // Capture the engine's InputLog replay-state window
        // (pInputLog+0x394..+0x4414) verbatim into `dst` (the IL region's
        // staging buffer).  Called from capture_snapshot RIGHT AFTER the
        // HgCpuDirect simulation write, while the engine state is still at
        // the captured frame's master clock.
        //
        // Returns true on success, false if the BM/InputLog couldn't be
        // resolved (between-match transitions).  SEH-wrapped via
        // SafeReadBytes - the InputLog actor can be torn down during mode
        // transitions and a fault here shouldn't kill the process.
        bool capture_input_cache(uint8_t* dst) noexcept
        {
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
        // engine's live InputLog.  Used by generation/direct-step probes
        // and by diagnostic legacy seek builds only; normal replay seeks
        // use SC6 exact round reset + fast-forward authority.
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
        bool restore_input_cache(const uint8_t* src) noexcept
        {
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

        // Capture the Stage 1 decoder's full state (FLuxReplayDataBlock
        // at *(pBM+0x460), 1021 bytes) verbatim into `dst` (the decoder
        // region's staging buffer).
        //
        // This is the KEY missing piece identified 2026-05-11: the
        // decoder's read/write cursors (llFileReadCursor,
        // llDecodedBufferReadCursor, llDecodedBufferWriteCursor) control
        // which packets the engine consumes per frame.  Without restoring
        // them on seek, the decoder stays at the live-edge file position
        // and serves "later" packets to the restored state - which is
        // exactly the "plays inputs from later in the round" symptom.
        //
        // `dst` is pre-zeroed so a faulted capture (BM unresolvable, or a
        // fault mid-teardown) commits a clean blob rather than the prior
        // tick's staging bytes - the restore path needs zero cursors, not
        // a stale occupant's.
        bool capture_replay_data_block(uint8_t* dst) noexcept
        {
            if (!dst) return false;
            std::memset(dst, 0, kRDB_Bytes);

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
        bool restore_replay_data_block(const uint8_t* src) noexcept
        {
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

        static const char* sc6_context_failure_name(
            Sc6ContextFailure failure) noexcept
        {
            switch (failure)
            {
            case Sc6ContextFailure::None:
                return "None";
            case Sc6ContextFailure::ImageBaseMissing:
                return "ImageBaseMissing";
            case Sc6ContextFailure::BattleManagerMissing:
                return "BattleManagerMissing";
            case Sc6ContextFailure::InputLogMissing:
                return "InputLogMissing";
            case Sc6ContextFailure::ReplayPlayerMissing:
                return "ReplayPlayerMissing";
            case Sc6ContextFailure::StateResetDataMissing:
                return "StateResetDataMissing";
            case Sc6ContextFailure::InteractiveReplayUnreadable:
                return "InteractiveReplayUnreadable";
            case Sc6ContextFailure::RoundResetSnapshotMissing:
                return "RoundResetSnapshotMissing";
            default:
                return "Unknown";
            }
        }

        static const char* sc6_context_source_name(
            Sc6ContextSource source) noexcept
        {
            switch (source)
            {
            case Sc6ContextSource::None:
                return "None";
            case Sc6ContextSource::ObjectRegistryBattleManager:
                return "ObjectRegistryBattleManager";
            case Sc6ContextSource::WorldModePumpBattleManager:
                return "WorldModePumpBattleManager";
            default:
                return "Unknown";
            }
        }

        static const char* native_seek_failure_name(
            NativeSeekFailure failure) noexcept
        {
            switch (failure)
            {
            case NativeSeekFailure::None:
                return "None";
            case NativeSeekFailure::FunctionUnresolved:
                return "FunctionUnresolved";
            case NativeSeekFailure::DriverUnresolved:
                return "DriverUnresolved";
            case NativeSeekFailure::DriverBusy:
                return "DriverBusy";
            case NativeSeekFailure::CallFaulted:
                return "CallFaulted";
            case NativeSeekFailure::TaskNotObserved:
                return "TaskNotObserved";
            case NativeSeekFailure::SettleTimedOut:
                return "SettleTimedOut";
            case NativeSeekFailure::InvalidTarget:
                return "InvalidTarget";
            case NativeSeekFailure::TimelineMissingDemoTime:
                return "TimelineMissingDemoTime";
            case NativeSeekFailure::NativeTimeSourceUnresolved:
                return "NativeTimeSourceUnresolved";
            case NativeSeekFailure::LegacyVerifyFailed:
                return "LegacyVerifyFailed";
            case NativeSeekFailure::InteractiveReplayContextUnresolved:
                return "InteractiveReplayContextUnresolved";
            case NativeSeekFailure::InteractiveReplayResetFaulted:
                return "InteractiveReplayResetFaulted";
            case NativeSeekFailure::InteractiveReplayRoundSelectFailed:
                return "InteractiveReplayRoundSelectFailed";
            case NativeSeekFailure::InteractiveReplayFastForwardStalled:
                return "InteractiveReplayFastForwardStalled";
            case NativeSeekFailure::InteractiveReplayVerifyFailed:
                return "InteractiveReplayVerifyFailed";
            case NativeSeekFailure::InteractiveReplayTargetPastMatchEnd:
                return "InteractiveReplayTargetPastMatchEnd";
            case NativeSeekFailure::RoundResetDataUnavailable:
                return "RoundResetDataUnavailable";
            case NativeSeekFailure::Sc6ResetSnapshotReadFailed:
                return "Sc6ResetSnapshotReadFailed";
            case NativeSeekFailure::Sc6ResetSnapshotWriteFailed:
                return "Sc6ResetSnapshotWriteFailed";
            case NativeSeekFailure::Sc6InputLogRestoreFailed:
                return "Sc6InputLogRestoreFailed";
            case NativeSeekFailure::Sc6ReplayDataBlockRestoreFailed:
                return "Sc6ReplayDataBlockRestoreFailed";
            case NativeSeekFailure::Sc6ReplayCursorWriteFailed:
                return "Sc6ReplayCursorWriteFailed";
            case NativeSeekFailure::Sc6ReplayPlayerCursorWriteFailed:
                return "Sc6ReplayPlayerCursorWriteFailed";
            case NativeSeekFailure::Sc6SetMoveStateFaulted:
                return "Sc6SetMoveStateFaulted";
            case NativeSeekFailure::Sc6InteractiveReplayResetFaultedDiagnostic:
                return "Sc6InteractiveReplayResetFaultedDiagnostic";
            case NativeSeekFailure::Sc6ResetDispatchFailed:
                return "Sc6ResetDispatchFailed";
            case NativeSeekFailure::CapturedSnapshotRestoreFailed:
                return "CapturedSnapshotRestoreFailed";
            case NativeSeekFailure::CapturedSnapshotCompareFailed:
                return "CapturedSnapshotCompareFailed";
            case NativeSeekFailure::CapturedSnapshotValidationStepFailed:
                return "CapturedSnapshotValidationStepFailed";
            case NativeSeekFailure::CapturedSnapshotSemanticRepairFailed:
                return "CapturedSnapshotSemanticRepairFailed";
            case NativeSeekFailure::CapturedRestoreProbeMismatch:
                return "CapturedRestoreProbeMismatch";
            case NativeSeekFailure::CapturedGameplayStepFailed:
                return "CapturedGameplayStepFailed";
            case NativeSeekFailure::SemanticMismatch:
                return "SemanticMismatch";
            case NativeSeekFailure::CrossRoundResetContextUnavailable:
                return "CrossRoundResetContextUnavailable";
            case NativeSeekFailure::CrossRoundResetDispatchFailed:
                return "CrossRoundResetDispatchFailed";
            case NativeSeekFailure::OracleFieldOffsetUnproven:
                return "OracleFieldOffsetUnproven";
            case NativeSeekFailure::TimelineIncomplete:
                return "TimelineIncomplete";
            case NativeSeekFailure::NotLanded:
                return "NotLanded";
            case NativeSeekFailure::BattleManagerStatusNotActive:
                return "BattleManagerStatusNotActive";
            default:
                return "Unknown";
            }
        }

        static const char* sc6_seek_authority_name(
            Sc6SeekAuthority authority) noexcept
        {
            switch (authority)
            {
            case Sc6SeekAuthority::CapturedSnapshotValidated:
                return "CapturedSnapshotValidated";
            case Sc6SeekAuthority::NativeRoundReplayDiagnostic:
                return "NativeRoundReplayDiagnostic";
            default:
                return "Unknown";
            }
        }

        static const char* replay_input_authority_name(
            ReplayInputAuthority authority) noexcept
        {
            switch (authority)
            {
            case ReplayInputAuthority::OfflineCharaReplayRing:
                return "OfflineCharaReplayRing";
            case ReplayInputAuthority::InputLogSimulationCache:
                return "InputLogSimulationCache";
            case ReplayInputAuthority::Unknown:
            default:
                return "Unknown";
            }
        }

        static const char* captured_seek_validation_mode_name(
            CapturedSeekValidationMode mode) noexcept
        {
            switch (mode)
            {
            case CapturedSeekValidationMode::None:
                return "None";
            case CapturedSeekValidationMode::PreviousToTarget:
                return "prev_to_target";
            case CapturedSeekValidationMode::TargetToNext:
                return "target_to_next";
            case CapturedSeekValidationMode::StaticTarget:
                return "static_target";
            default:
                return "unknown";
            }
        }

        static const char* sc6_exact_seek_phase_name(
            Sc6ExactSeekPhase phase) noexcept
        {
            switch (phase)
            {
            case Sc6ExactSeekPhase::Idle: return "Idle";
            case Sc6ExactSeekPhase::Queued: return "Queued";
            case Sc6ExactSeekPhase::RestoreValidationOrigin:
                return "RestoreValidationOrigin";
            case Sc6ExactSeekPhase::ValidateStepToTarget:
                return "ValidateStepToTarget";
            case Sc6ExactSeekPhase::CompareTargetSnapshot:
                return "CompareTargetSnapshot";
            case Sc6ExactSeekPhase::RestoreTargetAfterValidation:
                return "RestoreTargetAfterValidation";
            case Sc6ExactSeekPhase::ResetRound: return "ResetRound";
            case Sc6ExactSeekPhase::FastForward: return "FastForward";
            case Sc6ExactSeekPhase::Verify: return "Verify";
            case Sc6ExactSeekPhase::ClockLandedPlayBlocked:
                return "ClockLandedPlayBlocked";
            case Sc6ExactSeekPhase::Landed: return "Landed";
            case Sc6ExactSeekPhase::Failed: return "Failed";
            case Sc6ExactSeekPhase::Cancelled: return "Cancelled";
            default: return "Unknown";
            }
        }

        static bool sc6_exact_seek_phase_active(
            Sc6ExactSeekPhase phase) noexcept
        {
            return phase == Sc6ExactSeekPhase::Queued
                || phase == Sc6ExactSeekPhase::RestoreValidationOrigin
                || phase == Sc6ExactSeekPhase::ValidateStepToTarget
                || phase == Sc6ExactSeekPhase::CompareTargetSnapshot
                || phase == Sc6ExactSeekPhase::RestoreTargetAfterValidation
                || phase == Sc6ExactSeekPhase::ResetRound
                || phase == Sc6ExactSeekPhase::FastForward
                || phase == Sc6ExactSeekPhase::Verify;
        }

        void trace_sc6_context(const char* event_name,
                               const Sc6ReplaySeekContext& ctx,
                               const char* label) noexcept
        {
            ReplayTraceFields f;
            f.string("label", label ? label : "?")
             .string("source", sc6_context_source_name(ctx.source))
             .boolean("readable", ctx.readable)
             .string("failure", sc6_context_failure_name(ctx.failure))
             .hex("wmp", ctx.world_mode_pump)
             .hex("bm", ctx.battle_manager)
             .hex("object_bm", ctx.object_registry_battle_manager)
             .hex("wmp_bm", ctx.world_mode_pump_battle_manager)
             .hex("sub_driver", ctx.sub_driver)
             .hex("input_log", ctx.input_log)
             .hex("replay_player", ctx.replay_player)
             .hex("state_reset_data", ctx.state_reset_data)
             .hex("interactive_replay", ctx.interactive_replay)
             .integer("total_rounds", ctx.total_rounds)
             .integer("current_round", ctx.current_round)
             .integer("input_master", ctx.input_master)
             .integer("battle_master", ctx.battle_master)
             .boolean("bm_ok", ctx.battle_manager_ok)
             .boolean("input_log_ok", ctx.input_log_ok)
             .boolean("replay_player_ok", ctx.replay_player_ok)
             .boolean("state_reset_data_ok", ctx.state_reset_data_ok)
             .boolean("interactive_replay_ok", ctx.interactive_replay_ok)
             .boolean("captured_round_reset_ok",
                      ctx.captured_round_reset_ok);
            ReplayDebugTrace::instance().event(event_name, f);
        }

        void trace_sc6_replay_state_checkpoint(
            const char* checkpoint,
            const Sc6ReplaySeekContext& ctx) noexcept
        {
            uint8_t bm_main_state = 0;
            uint8_t bm_move_state = 0;
            uint8_t bm_skip_catchup = 0;
            uint8_t bm_status = 0;
            uint8_t il_double_tick_guard = 0;
            int32_t bm_last_frame_id = -1;
            int32_t bm_last_applied = -1;
            int32_t bm_frame_advance = -1;
            uint32_t il_playback_cursor = 0;
            int32_t il_last_frame_id = -1;
            int32_t il_master = -1;
            std::array<uint8_t, kRoundStartDataBytes> reset_snapshot{};

            const bool bm_main_state_ok = ctx.battle_manager_ok
                && SafeReadUInt8(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_bMainStateMachineByte_Off),
                   &bm_main_state);
            const bool bm_move_state_ok = ctx.battle_manager_ok
                && SafeReadUInt8(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_bMoveStateByte_Off),
                   &bm_move_state);
            const bool bm_skip_catchup_ok = ctx.battle_manager_ok
                && SafeReadUInt8(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_bSkipReplayCatchUp_Off),
                   &bm_skip_catchup);
            const bool bm_status_ok = ctx.battle_manager_ok
                && SafeReadUInt8(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_bStatusByte_Off),
                   &bm_status);
            const bool bm_last_frame_id_ok = ctx.battle_manager_ok
                && SafeReadInt32(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_nReplayLastFrameID_Off),
                   &bm_last_frame_id);
            const bool bm_last_applied_ok = ctx.battle_manager_ok
                && SafeReadInt32(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_nReplayLastApplied_Off),
                   &bm_last_applied);
            const bool bm_frame_advance_ok = ctx.battle_manager_ok
                && SafeReadInt32(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_nFrameAdvanceCounter_Off),
                   &bm_frame_advance);
            const bool reset_snapshot_ok = ctx.battle_manager_ok
                && SafeReadBytes(reinterpret_cast<const void*>(
                       ctx.battle_manager + kBM_pReplayCharaSnapshot_Off),
                   reset_snapshot.data(), reset_snapshot.size());

            const bool il_playback_cursor_ok = ctx.input_log_ok
                && SafeReadUInt32(reinterpret_cast<const void*>(
                       ctx.input_log + kIL_dwPlaybackCursor_Off),
                   &il_playback_cursor);
            const bool il_last_frame_id_ok = ctx.input_log_ok
                && SafeReadInt32(reinterpret_cast<const void*>(
                       ctx.input_log + kIL_nLastFrameID_Off),
                   &il_last_frame_id);
            const bool il_master_ok = ctx.input_log_ok
                && SafeReadInt32(reinterpret_cast<const void*>(
                       ctx.input_log + kIL_nMasterClock_Off),
                   &il_master);
            const bool il_double_tick_guard_ok = ctx.input_log_ok
                && SafeReadUInt8(reinterpret_cast<const void*>(
                       ctx.input_log + kIL_bDoubleTickGuard_Off),
                   &il_double_tick_guard);

            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .string("checkpoint", checkpoint ? checkpoint : "?")
             .integer("requested_seq", m_sc6_seek_job.requested_seq)
             .integer("target_round", m_sc6_seek_job.target_round)
             .integer("target_master", m_sc6_seek_job.target_master)
             .hex("bm", ctx.battle_manager)
             .hex("input_log", ctx.input_log)
             .boolean("bm_main_state_ok", bm_main_state_ok)
             .integer("bm_main_state", bm_main_state)
             .boolean("bm_move_state_ok", bm_move_state_ok)
             .integer("bm_move_state", bm_move_state)
             .boolean("bm_skip_catchup_ok", bm_skip_catchup_ok)
             .integer("bm_skip_catchup", bm_skip_catchup)
             .boolean("bm_status_ok", bm_status_ok)
             .integer("bm_status", bm_status)
             .boolean("bm_last_frame_id_ok", bm_last_frame_id_ok)
             .integer("bm_last_frame_id", bm_last_frame_id)
             .boolean("bm_last_applied_ok", bm_last_applied_ok)
             .integer("bm_last_applied", bm_last_applied)
             .boolean("bm_frame_advance_ok", bm_frame_advance_ok)
             .integer("bm_frame_advance", bm_frame_advance)
             .boolean("il_playback_cursor_ok", il_playback_cursor_ok)
             .uinteger("il_playback_cursor", il_playback_cursor)
             .boolean("il_last_frame_id_ok", il_last_frame_id_ok)
             .integer("il_last_frame_id", il_last_frame_id)
             .boolean("il_master_ok", il_master_ok)
             .integer("il_master", il_master)
             .boolean("il_double_tick_guard_ok", il_double_tick_guard_ok)
             .integer("il_double_tick_guard", il_double_tick_guard)
             .boolean("reset_snapshot_ok", reset_snapshot_ok);
            if (reset_snapshot_ok)
                f.hash("reset_snapshot_hash", reset_snapshot.data(),
                       reset_snapshot.size());
            ReplayDebugTrace::instance().event(
                "sc6_replay_state_checkpoint", f);
        }

        void trace_seek_job_event(const char* event_name,
                                  const char* phase = nullptr) noexcept
        {
            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .string("phase", phase ? phase
                                     : sc6_exact_seek_phase_name(
                                           m_sc6_seek_job.phase))
             .string("authority", sc6_seek_authority_name(
                         m_sc6_seek_job.authority))
             .integer("job_generation", m_sc6_seek_job.generation)
             .integer("requested_seq", m_sc6_seek_job.requested_seq)
             .integer("target_seq", m_sc6_seek_job.target_seq)
             .integer("target_round", m_sc6_seek_job.target_round)
             .integer("target_master", m_sc6_seek_job.target_master)
             .integer("origin_seq", m_sc6_seek_job.validation_origin_seq)
             .integer("origin_master",
                      m_sc6_seek_job.validation_origin_master)
             .integer("compare_seq", m_sc6_seek_job.validation_compare_seq)
             .integer("compare_master",
                      m_sc6_seek_job.validation_compare_master)
             .string("validation_mode", captured_seek_validation_mode_name(
                         m_sc6_seek_job.validation_mode))
             .integer("round_start_seq", m_sc6_seek_job.round_start_seq)
             .integer("round_start_master",
                      m_sc6_seek_job.round_start_master)
             .integer("frames_advanced", m_sc6_seek_job.frames_advanced)
             .integer("slices_serviced", m_sc6_seek_job.slices_serviced)
             .integer("stall_count", m_sc6_seek_job.stall_count)
             .integer("native_step_requested_master",
                      m_sc6_seek_job.native_step_requested_master)
             .integer("native_step_last_observed_master",
                      m_sc6_seek_job.native_step_last_observed_master)
             .integer("native_step_requested_credits",
                      m_sc6_seek_job.native_step_requested_credits)
             .integer("native_step_granted_credits",
                      m_sc6_seek_job.native_step_granted_credits)
             .integer("native_step_observed_credits",
                      m_sc6_seek_job.native_step_observed_credits)
             .integer("native_step_stall_count",
                      m_sc6_seek_job.native_step_stall_count)
             .boolean("native_step_waiting",
                      m_sc6_seek_job.native_step_waiting)
             .string("failure", native_seek_failure_name(
                         m_sc6_seek_job.failure));
            ReplayDebugTrace::instance().event(event_name, f);
        }

        bool has_captured_round_reset_snapshot(int32_t round) const noexcept
        {
            return round >= 0 && round < kMaxSc6ReplayRounds
                && m_sc6_round_reset_snapshot_valid[
                    static_cast<size_t>(round)];
        }

        static int sc6_context_progress_score(
            const Sc6ReplaySeekContext& ctx) noexcept
        {
            if (ctx.readable) return 100;
            if (ctx.interactive_replay_ok) return 80;
            if (ctx.input_log_ok) return 60;
            if (ctx.battle_manager_ok) return 40;
            if (ctx.battle_manager) return 20;
            return 0;
        }

        Sc6ReplaySeekContext choose_better_sc6_context_failure(
            const Sc6ReplaySeekContext& a,
            const Sc6ReplaySeekContext& b) noexcept
        {
            if (a.readable) return a;
            if (b.readable) return b;
            const int as = sc6_context_progress_score(a);
            const int bs = sc6_context_progress_score(b);
            if (bs > as) return b;
            if (as > bs) return a;
            if (a.source == Sc6ContextSource::None) return b;
            return a;
        }

        Sc6ReplaySeekContext resolve_sc6_replay_seek_context_for_bm(
            Sc6ContextSource source,
            uintptr_t battle_manager,
            uintptr_t world_mode_pump,
            uintptr_t sub_driver) noexcept
        {
            Sc6ReplaySeekContext ctx{};
            ctx.source = source;
            ctx.world_mode_pump = world_mode_pump;
            ctx.sub_driver = sub_driver;
            ctx.battle_manager = battle_manager;
            // Ghidra recheck 2026-05-22:
            // LuxBattle_AdvanceWorldModePump uses +0x38 only as the
            // "drain round-result cinematic" presence guard.  The state
            // pointer passed to that native path is BattleManager+0xAA120
            // from WorldModePump+0x30.  Do not require +0x38 here; it can
            // be null after generation/parking even though the exact seek
            // context is still valid.
            if (!ctx.battle_manager)
            {
                ctx.failure = Sc6ContextFailure::BattleManagerMissing;
                return ctx;
            }
            ctx.battle_manager_ok = true;
            if (source == Sc6ContextSource::ObjectRegistryBattleManager)
                ctx.object_registry_bm_ok = true;
            else if (source == Sc6ContextSource::WorldModePumpBattleManager)
                ctx.world_mode_pump_bm_ok = true;

            void* il_raw = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                                 ctx.battle_manager
                                 + kBM_BattleFrameInputLog_Off),
                             &il_raw) || !il_raw)
            {
                ctx.failure = Sc6ContextFailure::InputLogMissing;
                return ctx;
            }
            ctx.input_log = reinterpret_cast<uintptr_t>(il_raw);
            ctx.input_log_ok = true;

            RC::Unreal::UObject* rp_obj =
                ReplayScrubDiag::replay_player_ptr().get(
                    L"LuxBattleReplayPlayer");
            if (rp_obj)
            {
                ctx.replay_player = reinterpret_cast<uintptr_t>(rp_obj);
                ctx.replay_player_ok = true;

                void* state_reset_raw = nullptr;
                if (SafeReadPtr(reinterpret_cast<const void*>(
                                    ctx.replay_player
                                    + kRP_StateResetData_Off),
                                &state_reset_raw) && state_reset_raw)
                {
                    ctx.state_reset_data =
                        reinterpret_cast<uintptr_t>(state_reset_raw);
                    ctx.state_reset_data_ok = true;
                }

                (void)SafeReadInt32(reinterpret_cast<const void*>(
                                        ctx.replay_player
                                        + kRP_TotalRounds_Off),
                                    &ctx.total_rounds);
                (void)SafeReadInt32(reinterpret_cast<const void*>(
                                        ctx.replay_player
                                        + kRP_CurrentRound_Off),
                                    &ctx.current_round);
            }
            (void)SafeReadInt32(reinterpret_cast<const void*>(
                                    ctx.input_log
                                    + kIL_nMasterClock_Off),
                                &ctx.input_master);
            (void)SafeReadInt32(reinterpret_cast<const void*>(
                                    ctx.battle_manager
                                    + kBM_nReplayLastApplied_Off),
                                &ctx.battle_master);

            ctx.interactive_replay =
                ctx.battle_manager + kBM_InteractiveReplay_Off;
            uint32_t ir_state = 0;
            ctx.interactive_replay_ok =
                SafeReadUInt32(reinterpret_cast<const void*>(
                                   ctx.interactive_replay),
                               &ir_state);

            for (int32_t round = 0; round < kMaxSc6ReplayRounds; ++round)
            {
                if (has_captured_round_reset_snapshot(round))
                {
                    ctx.captured_round_reset_ok = true;
                    break;
                }
            }

            if (!ctx.replay_player_ok)
                ctx.failure = Sc6ContextFailure::ReplayPlayerMissing;
            else if (!ctx.state_reset_data_ok)
                ctx.failure = Sc6ContextFailure::StateResetDataMissing;
            else
                ctx.failure = Sc6ContextFailure::None;

            ctx.readable = true;
            return ctx;
        }

        Sc6ReplaySeekContext resolve_sc6_replay_seek_context_report() noexcept
        {
            Sc6ReplaySeekContext base_failure{};
            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                base_failure.failure = Sc6ContextFailure::ImageBaseMissing;
                return base_failure;
            }

            const uintptr_t world_mode_pump = base + kRVA_WorldModePump;
            void* wmp_bm_raw = nullptr;
            void* sub_raw = nullptr;
            (void)SafeReadPtr(reinterpret_cast<const void*>(
                                  world_mode_pump
                                  + kWorldModePump_BattleManager_Off),
                              &wmp_bm_raw);
            (void)SafeReadPtr(reinterpret_cast<const void*>(
                                  world_mode_pump
                                  + kWorldModePump_SubDriver_Off),
                              &sub_raw);
            const uintptr_t wmp_bm =
                reinterpret_cast<uintptr_t>(wmp_bm_raw);
            const uintptr_t sub_driver =
                reinterpret_cast<uintptr_t>(sub_raw);
            const uintptr_t object_bm =
                reinterpret_cast<uintptr_t>(
                    m_bm_ptr.get(L"LuxBattleManager"));

            base_failure.world_mode_pump = world_mode_pump;
            base_failure.world_mode_pump_battle_manager = wmp_bm;
            base_failure.object_registry_battle_manager = object_bm;
            base_failure.sub_driver = sub_driver;
            base_failure.failure = Sc6ContextFailure::BattleManagerMissing;

            Sc6ReplaySeekContext best = base_failure;

            if (object_bm)
            {
                Sc6ReplaySeekContext object_ctx =
                    resolve_sc6_replay_seek_context_for_bm(
                        Sc6ContextSource::ObjectRegistryBattleManager,
                        object_bm, world_mode_pump, sub_driver);
                object_ctx.world_mode_pump_battle_manager = wmp_bm;
                object_ctx.object_registry_battle_manager = object_bm;
                if (object_ctx.readable)
                    return object_ctx;
                best = choose_better_sc6_context_failure(best, object_ctx);
            }

            if (wmp_bm && wmp_bm != object_bm)
            {
                Sc6ReplaySeekContext wmp_ctx =
                    resolve_sc6_replay_seek_context_for_bm(
                        Sc6ContextSource::WorldModePumpBattleManager,
                        wmp_bm, world_mode_pump, sub_driver);
                wmp_ctx.world_mode_pump_battle_manager = wmp_bm;
                wmp_ctx.object_registry_battle_manager = object_bm;
                if (wmp_ctx.readable)
                    return wmp_ctx;
                best = choose_better_sc6_context_failure(best, wmp_ctx);
            }

            return best;
        }

        bool resolve_sc6_replay_seek_context(
            Sc6ReplaySeekContext& ctx) noexcept
        {
            ctx = resolve_sc6_replay_seek_context_report();
            return ctx.readable;
        }

        void log_sc6_context_report(const char* label,
                                    const Sc6ReplaySeekContext& ctx,
                                    bool ok) noexcept
        {
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.sc6ctx] label={} ok={} failure={} "
                "source={} wmp=0x{:X} object_bm=0x{:X} "
                "wmp_bm=0x{:X} bm=0x{:X} bm_ok={} sub=0x{:X} "
                "inputLog=0x{:X} input_ok={} rp=0x{:X} rp_ok={} "
                "reset=0x{:X} reset_ok={} irs=0x{:X} irs_ok={} "
                "captured_reset_any={} totalRounds={} rpRound={} "
                "inputMaster={} bmMaster={}\n"),
                RC::to_generic_string(label ? label : "?"),
                ok ? 1 : 0,
                RC::to_generic_string(
                    sc6_context_failure_name(ctx.failure)),
                RC::to_generic_string(sc6_context_source_name(ctx.source)),
                ctx.world_mode_pump, ctx.object_registry_battle_manager,
                ctx.world_mode_pump_battle_manager, ctx.battle_manager,
                ctx.battle_manager_ok ? 1 : 0, ctx.sub_driver,
                ctx.input_log, ctx.input_log_ok ? 1 : 0,
                ctx.replay_player, ctx.replay_player_ok ? 1 : 0,
                ctx.state_reset_data, ctx.state_reset_data_ok ? 1 : 0,
                ctx.interactive_replay,
                ctx.interactive_replay_ok ? 1 : 0,
                ctx.captured_round_reset_ok ? 1 : 0,
                ctx.total_rounds, ctx.current_round,
                ctx.input_master, ctx.battle_master);
        }

        bool read_live_bm_round_reset_snapshot(
            uintptr_t battle_manager,
            std::array<uint8_t, kRoundStartDataBytes>& out) const noexcept
        {
            if (!battle_manager) return false;
            return SafeReadBytes(reinterpret_cast<const void*>(
                                     battle_manager
                                     + kBM_pReplayCharaSnapshot_Off),
                                 out.data(), out.size());
        }

        void capture_sc6_round_reset_snapshot_if_needed(
            int32_t round,
            int32_t seq,
            int32_t master,
            int32_t last_frame_id) noexcept
        {
            if (round < 0 || round >= kMaxSc6ReplayRounds)
                return;
            const size_t idx = static_cast<size_t>(round);
            if (m_sc6_round_reset_snapshot_valid[idx])
                return;

            Sc6ReplaySeekContext ctx =
                resolve_sc6_replay_seek_context_report();
            if (!ctx.battle_manager_ok)
                return;

            std::array<uint8_t, kRoundStartDataBytes> snap{};
            if (!read_live_bm_round_reset_snapshot(ctx.battle_manager, snap))
                return;

            m_sc6_round_reset_snapshots[idx] = snap;
            m_sc6_round_reset_snapshot_valid[idx] = true;
            m_sc6_round_reset_snapshot_seq[idx] = seq;
            m_sc6_round_reset_snapshot_master[idx] = master;
            m_sc6_round_reset_snapshot_last_frame_id[idx] =
                last_frame_id;
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.sc6reset] captured round reset snapshot "
                "round={} seq={} master={} last_frame_id={} "
                "bm=0x{:X} src=bm+1360\n"),
                round, seq, master, last_frame_id, ctx.battle_manager);
            ReplayTraceFields f;
            f.integer("round", round)
             .integer("seq", seq)
             .integer("master", master)
             .integer("last_frame_id", last_frame_id)
             .hex("bm", ctx.battle_manager)
             .string("source", "BattleManager+0x1360")
             .hash("reset_hash", snap.data(), snap.size())
             .uinteger("reset_size", snap.size());
            ReplayDebugTrace::instance().event(
                "capture_round_reset_snapshot", f);
        }

#if HORSEMOD_REPLAY_ENABLE_INTERACTIVE_RESET_DIAG
        bool diagnostic_safe_call_interactive_replay_reset(
            uintptr_t interactive_replay) noexcept
        {
            if (!m_interactive_replay_reset || !interactive_replay)
                return false;
            __try
            {
                m_interactive_replay_reset(
                    reinterpret_cast<void*>(interactive_replay));
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
#endif

        bool safe_call_battle_manager_set_move_state(
            uintptr_t battle_manager, uint8_t move_state) noexcept
        {
            m_last_set_move_state_fault = NativeCallFault{};
            if (!m_battle_manager_set_move_state || !battle_manager)
                return false;
            __try
            {
                m_battle_manager_set_move_state(
                    reinterpret_cast<void*>(battle_manager), move_state);
                return true;
            }
            __except (CaptureNativeCallFault(
                GetExceptionCode(), GetExceptionInformation(),
                &m_last_set_move_state_fault))
            {
                return false;
            }
        }

        int32_t find_first_slot_for_round(int32_t round) const noexcept
        {
            if (round < 0) return -1;
            const int32_t latest = latest_seq();
            if (latest < 0) return -1;
            for (int32_t k = 0; k <= latest; ++k)
            {
                int32_t s = -1, r = -1, f = -1, m = -1;
                if (!m_tags.get(static_cast<size_t>(k), s, r, f, m))
                    break;
                if (r == round && m >= 0)
                    return k;
            }
            return -1;
        }

        bool invoke_direct_sc6_frame_once() noexcept
        {
            if (!resolve_per_frame_tick_bypass())
                return false;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            uint64_t input[2] = {};
            uint8_t camera_args[24] = {};
            if (!SafeReadBytes(reinterpret_cast<const void*>(
                                   base + kRVA_LatestEngineInput),
                               input, sizeof(input)))
                return false;
            if (!SafeReadBytes(reinterpret_cast<const void*>(
                                   base + kRVA_PerFrameCameraArgs),
                               camera_args, sizeof(camera_args)))
                return false;

            uintptr_t args[3] = {
                reinterpret_cast<uintptr_t>(&input[0]),
                reinterpret_cast<uintptr_t>(&input[1]),
                reinterpret_cast<uintptr_t>(camera_args)
            };
            return SafeInvokePerFrameTick(m_per_frame_tick_bypass, args);
        }

        bool invoke_direct_sc6_replay_frame_once(
            uintptr_t battle_manager,
            uintptr_t input_log,
            const char* label) noexcept
        {
            if (!resolve_natives() || !battle_manager || !input_log)
                return false;

            int32_t master_before = -1;
            if (!SafeReadInt32(reinterpret_cast<const void*>(
                                   input_log + kIL_nMasterClock_Off),
                               &master_before))
                return false;

            if (!SafeInvokeNativeVoidPtr(
                    m_frame_input_log_advance_replay_clock,
                    reinterpret_cast<void*>(input_log)))
                return false;

            int32_t master_after = -1;
            if (!SafeReadInt32(reinterpret_cast<const void*>(
                                   input_log + kIL_nMasterClock_Off),
                               &master_after))
                return false;
            if (master_after <= master_before)
            {
                const int32_t forced_master = master_before + 1;
                if (!SafeWriteBytes(reinterpret_cast<void*>(
                                        input_log + kIL_nMasterClock_Off),
                                    &forced_master, sizeof(forced_master)))
                    return false;
                static std::atomic<int> s_forced_log_count{0};
                const int prior = s_forced_log_count.fetch_add(
                    1, std::memory_order_relaxed);
                if (prior < 16)
                {
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[ReplayScrub.sc6seek] replay-clock advancer "
                        "was gated; forced IL master {} -> {} label={}\n"),
                        master_before, forced_master,
                        RC::to_generic_string(label ? label : "?"));
                    ReplayTraceFields f;
                    f.string("label", label ? label : "?")
                     .integer("master_before", master_before)
                     .integer("master_after", master_after)
                     .integer("forced_master", forced_master);
                    ReplayDebugTrace::instance().event(
                        "sc6_replay_clock_forced_after_gated_advancer", f);
                }
            }
            if (!SafeInvokeNativeVoidPtr(
                    m_battle_manager_simulation_loop,
                    reinterpret_cast<void*>(battle_manager)))
                return false;
            return invoke_direct_sc6_frame_once();
        }

        GenerateStartReadiness read_generate_start_readiness() noexcept
        {
            GenerateStartReadiness out{};
            out.in_replay = GameMode::instance().current_presence()
                == GamePresence::Replay;
            if (!out.in_replay)
            {
                out.reason = "Enter the Replay viewer to generate a timeline.";
                return out;
            }

            const uintptr_t base = NativeBinding::imageBase();
            uintptr_t world_mode_pump = 0;
            uintptr_t sub_driver = 0;
            if (base)
            {
                world_mode_pump = base + kRVA_WorldModePump;
                void* wmp_bm_raw = nullptr;
                void* sub_raw = nullptr;
                (void)SafeReadPtr(reinterpret_cast<const void*>(
                                      world_mode_pump
                                      + kWorldModePump_BattleManager_Off),
                                  &wmp_bm_raw);
                (void)SafeReadPtr(reinterpret_cast<const void*>(
                                      world_mode_pump
                                      + kWorldModePump_SubDriver_Off),
                                  &sub_raw);
                out.world_mode_pump_battle_manager =
                    reinterpret_cast<uintptr_t>(wmp_bm_raw);
                sub_driver = reinterpret_cast<uintptr_t>(sub_raw);
                out.sub_driver = sub_driver;
                if (out.world_mode_pump_battle_manager)
                {
                    void* wmp_il_raw = nullptr;
                    (void)SafeReadPtr(reinterpret_cast<const void*>(
                                          out.world_mode_pump_battle_manager
                                          + kBM_BattleFrameInputLog_Off),
                                      &wmp_il_raw);
                    out.world_mode_pump_input_log =
                        reinterpret_cast<uintptr_t>(wmp_il_raw);
                }
            }

            RC::Unreal::UObject* bm_obj =
                m_bm_ptr.get(L"LuxBattleManager");
            out.object_registry_battle_manager =
                reinterpret_cast<uintptr_t>(bm_obj);
            if (out.object_registry_battle_manager)
            {
                void* object_il_raw = nullptr;
                (void)SafeReadPtr(reinterpret_cast<const void*>(
                                      out.object_registry_battle_manager
                                      + kBM_BattleFrameInputLog_Off),
                                  &object_il_raw);
                out.object_registry_input_log =
                    reinterpret_cast<uintptr_t>(object_il_raw);
            }

            Sc6ReplaySeekContext seek_ctx{};
            if (out.object_registry_battle_manager)
            {
                seek_ctx = resolve_sc6_replay_seek_context_for_bm(
                    Sc6ContextSource::ObjectRegistryBattleManager,
                    out.object_registry_battle_manager,
                    world_mode_pump, sub_driver);
                seek_ctx.object_registry_battle_manager =
                    out.object_registry_battle_manager;
                seek_ctx.world_mode_pump_battle_manager =
                    out.world_mode_pump_battle_manager;
            }
            else
            {
                seek_ctx = resolve_sc6_replay_seek_context_report();
            }

            out.seek_context_failure = seek_ctx.failure;
            out.seek_context_source = seek_ctx.source;
            out.battle_manager = seek_ctx.battle_manager;
            out.input_log = seek_ctx.input_log;
            out.interactive_replay = seek_ctx.interactive_replay;
            out.sub_driver = seek_ctx.sub_driver;
            out.replay_player = seek_ctx.replay_player;
            out.state_reset_data = seek_ctx.state_reset_data;
            out.state_reset_data_ok = seek_ctx.state_reset_data_ok;
            out.input_master = seek_ctx.input_master;
            out.battle_master = seek_ctx.battle_master;
            if (seek_ctx.readable)
                out.seek_context_ok = true;

            if (!out.battle_manager && out.object_registry_battle_manager)
                out.battle_manager = out.object_registry_battle_manager;
            if (!out.input_log && out.object_registry_input_log)
                out.input_log = out.object_registry_input_log;

            if (!out.battle_manager || !out.input_log)
            {
                out.reason =
                    "Replay is still loading. Try again in a moment.";
                return out;
            }

            if (out.input_master < 0)
                (void)SafeReadInt32(reinterpret_cast<const void*>(
                                        out.input_log
                                        + kIL_nMasterClock_Off),
                                    &out.input_master);
            if (out.battle_master < 0)
                (void)SafeReadInt32(reinterpret_cast<const void*>(
                                        out.battle_manager
                                        + kBM_nReplayLastApplied_Off),
                                    &out.battle_master);
            out.round = read_current_round();

            std::array<uint8_t, kRoundStartDataBytes> bm_reset{};
            out.live_bm_reset_ok =
                read_live_bm_round_reset_snapshot(out.battle_manager,
                                                  bm_reset);
            out.reset_source_ok =
                out.state_reset_data_ok
                || out.live_bm_reset_ok
                || has_captured_round_reset_snapshot(out.round);

            if (out.round < 0 || out.input_master < 0)
            {
                out.reason =
                    "Replay is still loading. Try again in a moment.";
                return out;
            }

            out.context_ok = true;

            if (out.round != 0)
            {
                out.reason =
                    "This replay already started. Click Generate, then restart or reload the replay.";
                return out;
            }
            if (out.input_master > 1)
            {
                out.reason =
                    "This replay already started. Click Generate, then restart or reload the replay.";
                return out;
            }
            if (out.battle_master >= 0)
            {
                const int32_t delta =
                    out.battle_master >= out.input_master
                        ? (out.battle_master - out.input_master)
                        : (out.input_master - out.battle_master);
                if (delta > 1)
                {
                    out.reason =
                        "This replay already started. Click Generate, then restart or reload the replay.";
                    return out;
                }
            }

            out.clean_start = true;
            if (!out.seek_context_ok)
            {
                out.reason =
                    "Replay is still loading. Wait at the start before generating.";
                return out;
            }
            if (!out.reset_source_ok)
            {
                out.reason =
                    "Replay is still loading. Wait at the start before generating.";
                return out;
            }

            out.reason = "";
            return out;
        }

        void reset_armed_generate_wait_log() noexcept
        {
            m_gen_armed_last_log_round.store(INT32_MIN,
                                             std::memory_order_release);
            m_gen_armed_last_log_master_bucket.store(
                INT32_MIN, std::memory_order_release);
            m_gen_armed_last_log_state.store(0,
                                             std::memory_order_release);
            m_gen_armed_hold_logged.store(false,
                                          std::memory_order_release);
        }

        void log_armed_generate_wait_if_needed(
            const GenerateStartReadiness& ready,
            int state) noexcept
        {
            const int32_t master_bucket =
                ready.input_master >= 0 ? ready.input_master / 300 : -1;
            const int32_t last_round =
                m_gen_armed_last_log_round.load(std::memory_order_acquire);
            const int32_t last_bucket =
                m_gen_armed_last_log_master_bucket.load(
                    std::memory_order_acquire);
            const int last_state =
                m_gen_armed_last_log_state.load(std::memory_order_acquire);

            if (last_round == ready.round
                && last_bucket == master_bucket
                && last_state == state)
                return;

            m_gen_armed_last_log_round.store(ready.round,
                                             std::memory_order_release);
            m_gen_armed_last_log_master_bucket.store(
                master_bucket, std::memory_order_release);
            m_gen_armed_last_log_state.store(state,
                                             std::memory_order_release);

            const char* prefix = state == 3
                ? "armed Generate waiting for exact SC6 reset source"
                : "armed Generate waiting for clean replay start";
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] {} (state={} reason={} failure={} "
                "source={} round={} input_master={} battle_master={} "
                "object_bm=0x{:X} object_il=0x{:X} wmp_bm=0x{:X} "
                "wmp_il=0x{:X} chosen_bm=0x{:X} chosen_il=0x{:X} "
                "seek_context_ok={} reset_source_ok={} "
                "live_bm_reset_ok={} state_reset_data_ok={})\n"),
                RC::to_generic_string(prefix), state,
                RC::to_generic_string(ready.reason ? ready.reason : ""),
                RC::to_generic_string(sc6_context_failure_name(
                    ready.seek_context_failure)),
                RC::to_generic_string(sc6_context_source_name(
                    ready.seek_context_source)),
                ready.round, ready.input_master, ready.battle_master,
                ready.object_registry_battle_manager,
                ready.object_registry_input_log,
                ready.world_mode_pump_battle_manager,
                ready.world_mode_pump_input_log,
                ready.battle_manager, ready.input_log,
                ready.seek_context_ok ? 1 : 0,
                ready.reset_source_ok ? 1 : 0,
                ready.live_bm_reset_ok ? 1 : 0,
                ready.state_reset_data_ok ? 1 : 0);
        }

        void hold_replay_at_clean_start_for_armed_generate(
            const GenerateStartReadiness& ready) noexcept
        {
            m_paused.store(true, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::RestoredFrameHold),
                std::memory_order_release);
            if (m_gen_armed_hold_logged.exchange(
                    true, std::memory_order_acq_rel))
                return;

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] armed Generate holding replay at clean "
                "start while exact reset source resolves "
                "(reason={} failure={} source={} round={} input_master={} "
                "battle_master={} object_bm=0x{:X} object_il=0x{:X} "
                "wmp_bm=0x{:X} wmp_il=0x{:X} chosen_bm=0x{:X} "
                "chosen_il=0x{:X} live_bm_reset_ok={} "
                "state_reset_data_ok={})\n"),
                RC::to_generic_string(ready.reason ? ready.reason : ""),
                RC::to_generic_string(sc6_context_failure_name(
                    ready.seek_context_failure)),
                RC::to_generic_string(sc6_context_source_name(
                    ready.seek_context_source)),
                ready.round, ready.input_master, ready.battle_master,
                ready.object_registry_battle_manager,
                ready.object_registry_input_log,
                ready.world_mode_pump_battle_manager,
                ready.world_mode_pump_input_log,
                ready.battle_manager, ready.input_log,
                ready.live_bm_reset_ok ? 1 : 0,
                ready.state_reset_data_ok ? 1 : 0);
        }

        void service_armed_generate_start() noexcept
        {
            const int mode = m_gen_armed_mode.load(
                std::memory_order_acquire);
            if (mode == kGenReqNone)
                return;

            if (m_timeline_gen_state.load(std::memory_order_acquire)
                == static_cast<int>(TimelineGenState::Generating))
                return;

            if (is_timeline_generation_locked_complete())
            {
                m_gen_armed_mode.store(kGenReqNone,
                                       std::memory_order_release);
                reset_armed_generate_wait_log();
                log_generate_locked_complete_once();
                return;
            }

            const GenerateStartReadiness ready =
                read_generate_start_readiness();
            if (!ready.in_replay)
                return;

            if (!ready.clean_start)
            {
                const int state = ready.context_ok ? 2 : 1;
                log_armed_generate_wait_if_needed(ready, state);
                return;
            }
            if (!ready.seek_context_ok || !ready.reset_source_ok)
            {
                hold_replay_at_clean_start_for_armed_generate(ready);
                log_armed_generate_wait_if_needed(ready, 3);
                return;
            }

            m_gen_armed_mode.store(kGenReqNone, std::memory_order_release);
            reset_armed_generate_wait_log();
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] armed Generate starting on clean replay "
                "start mode={} round={} input_master={} "
                "battle_master={} reset_source_ok={} source={} "
                "live_bm_reset_ok={}\n"),
                mode, ready.round, ready.input_master, ready.battle_master,
                ready.reset_source_ok ? 1 : 0,
                RC::to_generic_string(sc6_context_source_name(
                    ready.seek_context_source)),
                ready.live_bm_reset_ok ? 1 : 0);

            if (mode == kGenReqStart)
                start_generate_timeline(false);
            else if (mode == kGenReqStartExperimental)
                start_generate_timeline(true);
            else if (mode == kGenReqStartBattleStep)
                start_generate_timeline_battle_step();
            else
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] armed Generate ignored unknown mode={}\n"),
                    mode);
        }

        bool arm_generate_if_replay_already_advanced(int mode) noexcept
        {
            const GenerateStartReadiness ready =
                read_generate_start_readiness();
            if (!ready.in_replay || ready.clean_start || !ready.context_ok)
                return false;

            const int prev = m_gen_armed_mode.exchange(
                mode, std::memory_order_acq_rel);
            if (prev == mode)
                return true;

            reset_armed_generate_wait_log();
            if (prev != kGenReqNone)
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] Generate timeline waiting mode changed "
                    "{} -> {}\n"),
                    prev, mode);
                return true;
            }

            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub] Generate timeline armed for next clean "
                "replay start - current replay is already advanced "
                "(mode={} reason={} round={} input_master={} "
                "battle_master={} source={} object_bm=0x{:X} "
                "object_il=0x{:X} wmp_bm=0x{:X} wmp_il=0x{:X} "
                "chosen_bm=0x{:X} chosen_il=0x{:X} seek_context_ok={} "
                "reset_source_ok={} live_bm_reset_ok={})\n"),
                mode, RC::to_generic_string(ready.reason), ready.round,
                ready.input_master, ready.battle_master,
                RC::to_generic_string(sc6_context_source_name(
                    ready.seek_context_source)),
                ready.object_registry_battle_manager,
                ready.object_registry_input_log,
                ready.world_mode_pump_battle_manager,
                ready.world_mode_pump_input_log,
                ready.battle_manager, ready.input_log,
                ready.seek_context_ok ? 1 : 0,
                ready.reset_source_ok ? 1 : 0,
                ready.live_bm_reset_ok ? 1 : 0);
            ReplayTraceFields f;
            f.string("mode", mode == kGenReqStartExperimental
                         ? "experimental"
                         : (mode == kGenReqStartBattleStep
                                ? "battle_step" : "normal"))
             .boolean("armed", true)
             .string("reason", ready.reason ? ready.reason : "?")
             .integer("round", ready.round)
             .integer("input_master", ready.input_master)
             .integer("battle_master", ready.battle_master)
             .string("context_source", sc6_context_source_name(
                         ready.seek_context_source))
             .hex("object_bm", ready.object_registry_battle_manager)
             .hex("object_il", ready.object_registry_input_log)
             .hex("wmp_bm", ready.world_mode_pump_battle_manager)
             .hex("wmp_il", ready.world_mode_pump_input_log)
             .hex("chosen_bm", ready.battle_manager)
             .hex("chosen_il", ready.input_log)
             .boolean("seek_context_ok", ready.seek_context_ok)
             .boolean("reset_source_ok", ready.reset_source_ok)
             .boolean("live_bm_reset_ok", ready.live_bm_reset_ok);
            ReplayDebugTrace::instance().event("generate_request", f);
            return true;
        }

        bool choose_captured_seek_validation_origin(
            Sc6ExactSeekJob& job) noexcept
        {
            job.validation_mode = CapturedSeekValidationMode::StaticTarget;
            job.validation_origin_tick = job.target_tick;
            job.validation_origin_seq = job.target_seq;
            job.validation_origin_round = job.target_round;
            job.validation_origin_master = job.target_master;
            job.validation_compare_tick = job.target_tick;
            job.validation_compare_seq = job.target_seq;
            job.validation_compare_round = job.target_round;
            job.validation_compare_master = job.target_master;

            int32_t ns = -1, nr = -1, nw = -1, nm = -1;
            if (m_tags.get(static_cast<size_t>(job.target_tick + 1),
                           ns, nr, nw, nm)
                && nr == job.target_round
                && nm >= 0
                && nm == job.target_master + 1)
            {
                job.validation_mode =
                    CapturedSeekValidationMode::TargetToNext;
                job.validation_compare_tick = job.target_tick + 1;
                job.validation_compare_seq = ns;
                job.validation_compare_round = nr;
                job.validation_compare_master = nm;
                return true;
            }

            return true;
        }

        static constexpr int32_t kHgCpuPerCharaSnapshotBytes = 0x1400C;
        static constexpr int32_t kHgCpuPrimaryBlockStart = 0x0000;
        static constexpr int32_t kHgCpuMainBlockStart = 0x0080;
        static constexpr int32_t kHgCpuMotionBankBlockStart = 0x3590;
        static constexpr int32_t kHgCpuSecondaryMotionBlockStart = 0x4DD0;
        static constexpr int32_t kHgCpuMoveProviderBlockStart = 0x55D0;
        static constexpr int32_t kHgCpuMoveVelocityCacheBlockStart = 0x5620;
        static constexpr int32_t kHgCpuSelfPointerBlockStart = 0x5670;
        static constexpr int32_t kHgCpuLaneStateBlockStart = 0x56A0;
        static constexpr int32_t kHgCpuLaneHelperBlockStart = 0x63D8;
        static constexpr int32_t kHgCpuVfxEffectAnchorBlockStart = 0x6708;
        static constexpr int32_t kHgCpuFacingRetrackRampStart = 0x78F8;
        static constexpr int32_t kHgCpuAiResetSlotBlockStart = 0x790C;

        static int32_t hgcpu_snapshot_local_for_chara_offset(
            uintptr_t chara_off) noexcept
        {
            if (chara_off >= 0x10 && chara_off < 0x90)
                return kHgCpuPrimaryBlockStart
                    + static_cast<int32_t>(chara_off - 0x10);
            if (chara_off >= 0x90 && chara_off < 0x35A0)
                return kHgCpuMainBlockStart
                    + static_cast<int32_t>(chara_off - 0x90);
            return -1;
        }

        bool trace_restore_probe_chara_field(
            const char* checkpoint,
            const uint8_t* sim_blob,
            int32_t seq,
            int32_t tick,
            int player_index,
            uintptr_t chara_off,
            size_t bytes,
            const char* field_name) noexcept
        {
            if (!sim_blob || player_index < 0 || player_index > 1)
                return false;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            const uintptr_t slot_rva =
                (player_index == 0)
                    ? ReplayScrubDiag::kRVA_CharaSlotP1
                    : ReplayScrubDiag::kRVA_CharaSlotP2;
            void* chara_raw = nullptr;
            const bool chara_ok =
                SafeReadPtr(reinterpret_cast<const void*>(
                                base + slot_rva),
                            &chara_raw) && chara_raw;

            uint64_t live_u64 = 0;
            uint64_t expected_u64 = 0;
            uint64_t live_hash = 0;
            uint64_t expected_hash = 0;
            bool live_ok = false;
            bool expected_ok = false;

            const int32_t local =
                hgcpu_snapshot_local_for_chara_offset(chara_off);
            if (local >= 0)
            {
                const uint8_t* expected =
                    sim_blob
                    + player_index * kHgCpuPerCharaSnapshotBytes
                    + local;
                expected_hash = hash_bytes64(expected, bytes);
                expected_ok = true;
                const size_t copy_bytes = bytes > sizeof(expected_u64)
                    ? sizeof(expected_u64) : bytes;
                std::memcpy(&expected_u64, expected, copy_bytes);
            }

            std::array<uint8_t, 0x100> live_buf{};
            if (chara_ok && bytes <= live_buf.size())
            {
                live_ok = SafeReadBytes(
                    reinterpret_cast<const uint8_t*>(chara_raw)
                        + chara_off,
                    live_buf.data(), bytes);
                if (live_ok)
                {
                    live_hash = hash_bytes64(live_buf.data(), bytes);
                    const size_t copy_bytes = bytes > sizeof(live_u64)
                        ? sizeof(live_u64) : bytes;
                    std::memcpy(&live_u64, live_buf.data(), copy_bytes);
                }
            }

            ReplayTraceFields f;
            f.string("checkpoint", checkpoint ? checkpoint : "?")
             .integer("seq", seq)
             .integer("tick", tick)
             .integer("player", player_index + 1)
             .string("field", field_name ? field_name : "?")
             .hex("chara", reinterpret_cast<uintptr_t>(chara_raw))
             .integer("chara_offset", static_cast<int64_t>(chara_off))
             .integer("bytes", static_cast<int64_t>(bytes))
             .boolean("expected_ok", expected_ok)
             .boolean("live_ok", live_ok)
             .hex("expected_u64", expected_u64)
             .hex("live_u64", live_u64)
             .hex("expected_hash", expected_hash)
             .hex("live_hash", live_hash)
             .boolean("match", expected_ok && live_ok
                        && expected_hash == live_hash);
            ReplayDebugTrace::instance().event(
                "restore_probe_chara_field", f);
            return expected_ok && live_ok && expected_hash == live_hash;
        }

        bool resolve_hgcpu_local_live_ptr(
            uint8_t* chara,
            int32_t local,
            size_t bytes,
            uint8_t** out_ptr) noexcept
        {
            if (out_ptr) *out_ptr = nullptr;
            if (!chara || local < 0 || bytes == 0)
                return false;

            auto in_range = [&](int32_t start, int32_t size) noexcept {
                const int64_t rel = static_cast<int64_t>(local) - start;
                return rel >= 0
                    && rel + static_cast<int64_t>(bytes) <= size;
            };
            auto set_chara_ptr = [&](uintptr_t chara_off) noexcept -> bool {
                if (out_ptr) *out_ptr = chara + chara_off;
                return true;
            };

            if (in_range(kHgCpuPrimaryBlockStart, 0x80))
                return set_chara_ptr(0x10 + local);
            if (in_range(kHgCpuMainBlockStart, 0x3510))
                return set_chara_ptr(
                    0x90 + (local - kHgCpuMainBlockStart));

            auto resolve_vtable_region = [&](uintptr_t holder_off,
                                             int32_t start,
                                             int32_t size) noexcept -> bool {
                if (!in_range(start, size)) return false;
                void* vtable = nullptr;
                void* fn_raw = nullptr;
                void* region_base = nullptr;
                if (!SafeReadPtr(chara + holder_off, &vtable) || !vtable
                    || !SafeReadPtr(reinterpret_cast<uint8_t*>(vtable) + 0x28,
                                    &fn_raw)
                    || !fn_raw
                    || !SafeInvokeNativePtrIntReturnsPtr(
                        reinterpret_cast<void* (__fastcall*)(void*, int)>(
                            fn_raw),
                        chara + holder_off, 0, &region_base)
                    || !region_base)
                {
                    return false;
                }
                if (out_ptr)
                {
                    *out_ptr = reinterpret_cast<uint8_t*>(region_base)
                        + (local - start);
                }
                return true;
            };

            if (resolve_vtable_region(0x35A0,
                                      kHgCpuMotionBankBlockStart, 0x1840))
                return true;
            if (resolve_vtable_region(0x27760,
                                      kHgCpuSecondaryMotionBlockStart, 0x800))
                return true;

            if (in_range(kHgCpuMoveProviderBlockStart, 0x50))
                return set_chara_ptr(
                    0x2B3E0 + (local - kHgCpuMoveProviderBlockStart));
            if (in_range(kHgCpuMoveVelocityCacheBlockStart, 0x50))
                return set_chara_ptr(
                    0x43D80 + (local - kHgCpuMoveVelocityCacheBlockStart));
            if (in_range(kHgCpuSelfPointerBlockStart, 0x30))
                return set_chara_ptr(
                    0x43DF0 + (local - kHgCpuSelfPointerBlockStart));
            if (in_range(kHgCpuLaneStateBlockStart, 0xD38))
                return set_chara_ptr(
                    0x444F0 + (local - kHgCpuLaneStateBlockStart));
            if (in_range(kHgCpuLaneHelperBlockStart, 0x370))
                return set_chara_ptr(
                    0x45230 + (local - kHgCpuLaneHelperBlockStart));
            if (in_range(kHgCpuVfxEffectAnchorBlockStart, 0x11F0))
                return set_chara_ptr(
                    0x95FA0 + (local - kHgCpuVfxEffectAnchorBlockStart));
            if (in_range(kHgCpuFacingRetrackRampStart, 0x14))
                return set_chara_ptr(
                    0x971A8 + (local - kHgCpuFacingRetrackRampStart));
            if (in_range(kHgCpuAiResetSlotBlockStart, 0x60))
                return set_chara_ptr(
                    0x971E8 + (local - kHgCpuAiResetSlotBlockStart));

            return false;
        }

        bool trace_restore_probe_hgcpu_local_range(
            const char* checkpoint,
            const uint8_t* sim_blob,
            int32_t seq,
            int32_t tick,
            int player_index,
            int32_t local,
            size_t bytes,
            const char* field_name) noexcept
        {
            if (!sim_blob || player_index < 0 || player_index > 1
                || local < 0 || bytes == 0
                || local + static_cast<int32_t>(bytes)
                    > kHgCpuPerCharaSnapshotBytes)
                return false;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            const uintptr_t slot_rva =
                (player_index == 0)
                    ? ReplayScrubDiag::kRVA_CharaSlotP1
                    : ReplayScrubDiag::kRVA_CharaSlotP2;
            void* chara_raw = nullptr;
            const bool chara_ok =
                SafeReadPtr(reinterpret_cast<const void*>(base + slot_rva),
                            &chara_raw) && chara_raw;

            const uint8_t* expected = sim_blob
                + player_index * kHgCpuPerCharaSnapshotBytes + local;
            uint64_t expected_u64 = 0;
            uint64_t live_u64 = 0;
            const size_t copy_bytes = bytes > sizeof(expected_u64)
                ? sizeof(expected_u64) : bytes;
            std::memcpy(&expected_u64, expected, copy_bytes);
            const uint64_t expected_hash = hash_bytes64(expected, bytes);

            std::array<uint8_t, 0x100> live_buf{};
            uint64_t live_hash = 0;
            bool live_ok = false;
            uint8_t* live_ptr = nullptr;
            if (chara_ok
                && bytes <= live_buf.size()
                && resolve_hgcpu_local_live_ptr(
                    reinterpret_cast<uint8_t*>(chara_raw), local, bytes,
                    &live_ptr)
                && live_ptr)
            {
                live_ok = SafeReadBytes(live_ptr, live_buf.data(), bytes);
                if (live_ok)
                {
                    live_hash = hash_bytes64(live_buf.data(), bytes);
                    std::memcpy(&live_u64, live_buf.data(), copy_bytes);
                }
            }

            const std::string hint = describe_hgcpu_sim_offset(local);
            ReplayTraceFields f;
            f.string("checkpoint", checkpoint ? checkpoint : "?")
             .integer("seq", seq)
             .integer("tick", tick)
             .integer("player", player_index + 1)
             .string("field", field_name ? field_name : "?")
             .hex("chara", reinterpret_cast<uintptr_t>(chara_raw))
             .integer("local_offset", local)
             .integer("bytes", static_cast<int64_t>(bytes))
             .boolean("expected_ok", true)
             .boolean("live_ok", live_ok)
             .hex("expected_u64", expected_u64)
             .hex("live_u64", live_u64)
             .hex("expected_hash", expected_hash)
             .hex("live_hash", live_hash)
             .boolean("match", live_ok && expected_hash == live_hash)
             .string("sim_offset_hint", hint.c_str());
            ReplayDebugTrace::instance().event(
                "restore_probe_hgcpu_local", f);
            return live_ok && expected_hash == live_hash;
        }

        void trace_restore_probe_unproven_hgcpu_ranges(
            const char* checkpoint,
            const uint8_t* sim_blob,
            int32_t seq,
            int32_t tick) noexcept
        {
            for (int player = 0; player < 2; ++player)
            {
                (void)trace_restore_probe_hgcpu_local_range(
                    checkpoint, sim_blob, seq, tick, player,
                    0x90, 0x10, "pos-main-0x90-chara+0xA0");
                (void)trace_restore_probe_hgcpu_local_range(
                    checkpoint, sim_blob, seq, tick, player,
                    0x4374, 0x20, "motion-bank-subblock+0xDE4");
                (void)trace_restore_probe_hgcpu_local_range(
                    checkpoint, sim_blob, seq, tick, player,
                    kHgCpuFacingRetrackRampStart, 0x14,
                    "facingRetrackRamp-chara+0x971A8");
            }
        }

        bool trace_restore_probe_chara_fields(
            const char* checkpoint,
            const uint8_t* sim_blob,
            int32_t seq,
            int32_t tick) noexcept
        {
            bool ok = true;
            for (int player = 0; player < 2; ++player)
            {
                ok &= trace_restore_probe_chara_field(
                    checkpoint, sim_blob, seq, tick, player,
                    0x244, 4, "chara+0x244");
                ok &= trace_restore_probe_chara_field(
                    checkpoint, sim_blob, seq, tick, player,
                    0x248, 4, "chara+0x248");
                ok &= trace_restore_probe_chara_field(
                    checkpoint, sim_blob, seq, tick, player,
                    ReplayScrubDiag::kChara_nCurrentMoveId_Off,
                    4, "nCurrentMoveId");
                ok &= trace_restore_probe_chara_field(
                    checkpoint, sim_blob, seq, tick, player,
                    0x1FF8, 0x88, "movevm-preserved-0x1FF8");
            }
            trace_restore_probe_unproven_hgcpu_ranges(
                checkpoint, sim_blob, seq, tick);
            return ok;
        }

        bool restore_hgcpu_final_semantic_repair(
            const uint8_t* sim_blob,
            const char* label,
            int32_t seq,
            int32_t tick) noexcept
        {
            if (!sim_blob) return false;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            bool ok = true;
            const uintptr_t slot_rvas[2] = {
                ReplayScrubDiag::kRVA_CharaSlotP1,
                ReplayScrubDiag::kRVA_CharaSlotP2,
            };

            (void)trace_restore_probe_chara_fields(
                "before-final-semantic-repair", sim_blob, seq, tick);

            for (int pi = 0; pi < 2; ++pi)
            {
                void* chara_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(
                                     base + slot_rvas[pi]),
                                 &chara_raw) || !chara_raw)
                {
                    ok = false;
                    continue;
                }

                uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);
                const int32_t snap_base =
                    pi * kHgCpuPerCharaSnapshotBytes;
                auto repair_bytes =
                    [&](uintptr_t chara_off, size_t bytes) noexcept
                {
                    const int32_t local =
                        hgcpu_snapshot_local_for_chara_offset(chara_off);
                    if (local < 0)
                    {
                        ok = false;
                        return;
                    }
                    ok &= SafeWriteBytes(
                        c + chara_off,
                        sim_blob + snap_base + local,
                        bytes);
                };

                repair_bytes(0x244, 4);
                repair_bytes(0x248, 4);
                repair_bytes(ReplayScrubDiag::kChara_nCurrentMoveId_Off, 4);
                repair_bytes(0x1FF8, 0x88);
            }

            const bool probe_ok = trace_restore_probe_chara_fields(
                "after-final-semantic-repair", sim_blob, seq, tick);
            ReplayTraceFields f;
            f.string("label", label ? label : "?")
             .integer("seq", seq)
             .integer("tick", tick)
             .boolean("write_ok", ok)
             .boolean("probe_ok", probe_ok);
            ReplayDebugTrace::instance().event(
                "post_read_semantic_repair", f);
            if (!ok || !probe_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] final semantic repair failed "
                    "label={} seq={} tick={} write_ok={} probe_ok={}\n"),
                    RC::to_generic_string(label ? label : "?"),
                    seq, tick, ok ? 1 : 0, probe_ok ? 1 : 0);
            }
            return ok && probe_ok;
        }

        bool restore_oracle_semantic_overlay_for_tick(
            int32_t tick,
            const char* label,
            int32_t seq) noexcept
        {
            if (tick < 0
                || static_cast<size_t>(tick) >= m_oracle_frames.size())
                return false;

            const ReplayFrameOracleSnap& oracle =
                m_oracle_frames[static_cast<size_t>(tick)];
            if (!oracle.valid) return false;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            const uint8_t* sim_blob =
                m_sim_store.gather(static_cast<size_t>(tick));
            bool ok = true;
            const uintptr_t slot_rvas[2] = {
                ReplayScrubDiag::kRVA_CharaSlotP1,
                ReplayScrubDiag::kRVA_CharaSlotP2,
            };
            const ReplayScrubDiag::CharaMoveVmSnap* snaps[2] = {
                &oracle.p1, &oracle.p2,
            };

            auto trace_source_u64 = [this, sim_blob, label, seq, tick](
                int player,
                uintptr_t chara_off,
                size_t bytes,
                const char* field,
                uint64_t oracle_value) noexcept
            {
                uint64_t sim_value = 0;
                bool sim_ok = false;
                const int32_t local =
                    hgcpu_snapshot_local_for_chara_offset(chara_off);
                if (sim_blob && local >= 0 && bytes <= sizeof(sim_value))
                {
                    const uint8_t* src = sim_blob
                        + player * kHgCpuPerCharaSnapshotBytes
                        + local;
                    std::memcpy(&sim_value, src, bytes);
                    sim_ok = true;
                }

                ReplayTraceFields f;
                f.string("label", label ? label : "?")
                 .integer("seq", seq)
                 .integer("tick", tick)
                 .integer("player", player + 1)
                 .string("field", field ? field : "?")
                 .integer("chara_offset",
                          static_cast<int64_t>(chara_off))
                 .integer("bytes", static_cast<int64_t>(bytes))
                 .boolean("sim_ok", sim_ok)
                 .hex("sim_value", sim_value)
                 .hex("oracle_value", oracle_value)
                 .boolean("match", sim_ok && sim_value == oracle_value);
                ReplayDebugTrace::instance().event(
                    "oracle_semantic_overlay_source", f);

                static std::atomic<int> s_mismatch_logs{0};
                if (sim_ok && sim_value != oracle_value
                    && s_mismatch_logs.load(std::memory_order_relaxed) < 16)
                {
                    const int prior = s_mismatch_logs.fetch_add(
                        1, std::memory_order_relaxed);
                    if (prior < 16)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[ReplayScrub.sc6seek] oracle semantic overlay "
                            "source mismatch label={} seq={} tick={} "
                            "player={} field={} sim=0x{:X} oracle=0x{:X}\n"),
                            RC::to_generic_string(label ? label : "?"),
                            seq, tick, player + 1,
                            RC::to_generic_string(field ? field : "?"),
                            sim_value, oracle_value);
                    }
                }
            };

            for (int pi = 0; pi < 2; ++pi)
            {
                const ReplayScrubDiag::CharaMoveVmSnap& s = *snaps[pi];
                if (!s.readable || s.current_move_id == 0xFFFFFFFFu)
                {
                    ok = false;
                    continue;
                }

                void* chara_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(
                                     base + slot_rvas[pi]),
                                 &chara_raw) || !chara_raw)
                {
                    ok = false;
                    continue;
                }

                uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);

                trace_source_u64(
                    pi, ReplayScrubDiag::kChara_nCurrentMoveId_Off,
                    sizeof(s.current_move_id), "move-id",
                    s.current_move_id);

                // Keep the oracle overlay conservative.  The first runtime
                // test proved HgCpuDirect's serialized P2 scalar layout does
                // not agree with the live oracle for broad MoveVM fields
                // (position, health, flags).  Writing those fields made the
                // seek pass verification but left gameplay suspect and the
                // process later died during validation.  Only patch the field
                // that originally blocked Play; let the native snapshot own
                // the rest of the gameplay state.
                ok &= SafeWriteUInt32(
                    c + ReplayScrubDiag::kChara_nCurrentMoveId_Off,
                    s.current_move_id);
            }

            ReplayTraceFields f;
            f.string("label", label ? label : "?")
             .integer("seq", seq)
             .integer("tick", tick)
             .integer("oracle_seq", oracle.seq)
             .integer("oracle_round", oracle.round)
             .integer("oracle_master", oracle.master)
             .boolean("ok", ok);
            ReplayDebugTrace::instance().event(
                "oracle_semantic_overlay_restore", f);
            if (!ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] oracle semantic overlay restore "
                    "failed label={} seq={} tick={}\n"),
                    RC::to_generic_string(label ? label : "?"), seq, tick);
            }
            return ok;
        }

        void trace_semantic_authority_for_tick(
            const char* checkpoint,
            const uint8_t* sim_blob,
            int32_t tick,
            const char* label,
            int32_t seq) noexcept
        {
            if (!ReplayDebugTrace::instance().enabled()) return;
            if (tick < 0
                || static_cast<size_t>(tick) >= m_oracle_frames.size())
                return;

            const ReplayFrameOracleSnap& oracle =
                m_oracle_frames[static_cast<size_t>(tick)];
            if (!oracle.valid) return;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return;

            const uintptr_t slot_rvas[2] = {
                ReplayScrubDiag::kRVA_CharaSlotP1,
                ReplayScrubDiag::kRVA_CharaSlotP2,
            };
            const ReplayScrubDiag::CharaMoveVmSnap* snaps[2] = {
                &oracle.p1, &oracle.p2,
            };

            auto trace_u64 = [&](int player,
                                 const ReplayScrubDiag::CharaMoveVmSnap& snap,
                                 uintptr_t chara_off,
                                 size_t bytes,
                                 const char* field,
                                 uint64_t oracle_value,
                                 bool play_gate) noexcept
            {
                uint64_t sim_value = 0;
                bool sim_ok = false;
                const int32_t local =
                    hgcpu_snapshot_local_for_chara_offset(chara_off);
                if (sim_blob && local >= 0 && bytes <= sizeof(sim_value))
                {
                    const uint8_t* src = sim_blob
                        + player * kHgCpuPerCharaSnapshotBytes
                        + local;
                    std::memcpy(&sim_value, src, bytes);
                    sim_ok = true;
                }

                uint64_t live_value = 0;
                bool live_ok = false;
                void* chara_raw = nullptr;
                if (SafeReadPtr(reinterpret_cast<const void*>(
                                    base + slot_rvas[player]),
                                &chara_raw) && chara_raw
                    && bytes <= sizeof(live_value))
                {
                    live_ok = SafeReadBytes(
                        reinterpret_cast<const uint8_t*>(chara_raw)
                            + chara_off,
                        &live_value, bytes);
                }

                ReplayTraceFields f;
                f.string("checkpoint", checkpoint ? checkpoint : "?")
                 .string("label", label ? label : "?")
                 .integer("seq", seq)
                 .integer("tick", tick)
                 .integer("oracle_seq", oracle.seq)
                 .integer("oracle_round", oracle.round)
                 .integer("oracle_master", oracle.master)
                 .integer("player", player + 1)
                 .string("field", field ? field : "?")
                 .integer("chara_offset",
                          static_cast<int64_t>(chara_off))
                 .integer("bytes", static_cast<int64_t>(bytes))
                 .hex("chara", snap.chara_ptr)
                 .boolean("oracle_readable", snap.readable)
                 .boolean("sim_ok", sim_ok)
                 .boolean("live_ok", live_ok)
                 .hex("oracle_value", oracle_value)
                 .hex("sim_value", sim_value)
                 .hex("live_value", live_value)
                 .boolean("sim_matches_oracle",
                          sim_ok && sim_value == oracle_value)
                 .boolean("live_matches_oracle",
                          live_ok && live_value == oracle_value)
                 .boolean("play_gate", play_gate);
                ReplayDebugTrace::instance().event(
                    "semantic_authority_field", f);
            };

            auto trace_float = [&](int player,
                                   const ReplayScrubDiag::CharaMoveVmSnap& snap,
                                   uintptr_t chara_off,
                                   const char* field,
                                   float oracle_value,
                                   bool play_gate) noexcept
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &oracle_value, sizeof(bits));
                trace_u64(player, snap, chara_off, sizeof(bits), field,
                          bits, play_gate);
            };

            auto trace_move_frame = [&](
                int player,
                const ReplayScrubDiag::CharaMoveVmSnap& snap) noexcept
            {
                uint64_t live_value = 0;
                bool live_ok = false;
                void* chara_raw = nullptr;
                if (SafeReadPtr(reinterpret_cast<const void*>(
                                    base + slot_rvas[player]),
                                &chara_raw) && chara_raw)
                {
                    float live_clip = 0.0f;
                    live_ok = SafeReadFloat(
                        reinterpret_cast<const uint8_t*>(chara_raw)
                            + ReplayScrubDiag::kChara_flCurrentClipFrame_Off,
                        &live_clip);
                    if (live_ok && live_clip >= 0.0f)
                        live_value = static_cast<uint32_t>(live_clip);
                }

                ReplayTraceFields f;
                f.string("checkpoint", checkpoint ? checkpoint : "?")
                 .string("label", label ? label : "?")
                 .integer("seq", seq)
                 .integer("tick", tick)
                 .integer("oracle_seq", oracle.seq)
                 .integer("oracle_round", oracle.round)
                 .integer("oracle_master", oracle.master)
                 .integer("player", player + 1)
                 .string("field", "move-frame")
                 .integer("chara_offset", static_cast<int64_t>(
                     ReplayScrubDiag::kChara_flCurrentClipFrame_Off))
                 .integer("bytes", 4)
                 .hex("chara", snap.chara_ptr)
                 .boolean("oracle_readable", snap.readable)
                 .boolean("sim_ok", false)
                 .boolean("live_ok", live_ok)
                 .hex("oracle_value", snap.current_move_frame)
                 .hex("sim_value", 0)
                 .hex("live_value", live_value)
                 .boolean("sim_matches_oracle", false)
                 .boolean("live_matches_oracle",
                          live_ok && live_value == snap.current_move_frame)
                 .boolean("play_gate", true)
                 .string("reason", "derived-from-clip-frame");
                ReplayDebugTrace::instance().event(
                    "semantic_authority_field", f);
            };

            for (int pi = 0; pi < 2; ++pi)
            {
                const ReplayScrubDiag::CharaMoveVmSnap& s = *snaps[pi];
                trace_u64(pi, s,
                          ReplayScrubDiag::kChara_nCurrentMoveId_Off,
                          sizeof(s.current_move_id), "move-id",
                          s.current_move_id, true);
                trace_move_frame(pi, s);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flCurrentClipFrame_Off,
                            "clip-frame", s.current_clip_frame, false);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flSelfPos_X_Off,
                            "pos-x", s.pos_x, true);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flSelfPos_Y_Off,
                            "pos-y", s.pos_y, true);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flSelfPos_Z_Off,
                            "pos-z", s.pos_z, true);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flMoveVelocity_X_Off,
                            "vel-x", s.vel_x, true);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flMoveVelocity_Y_Off,
                            "vel-y", s.vel_y, true);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flMoveVelocity_Z_Off,
                            "vel-z", s.vel_z, true);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flBodyFacing_Off,
                            "facing", s.facing, true);
                trace_float(pi, s,
                            ReplayScrubDiag::kChara_flCurHealth_Off,
                            "health", s.health, true);
                trace_u64(pi, s, ReplayScrubDiag::kChara_bVMPaused_Off,
                          sizeof(s.vm_paused), "vm-paused",
                          s.vm_paused, true);
                trace_u64(pi, s,
                          ReplayScrubDiag::kChara_bInputFreezeGate_Off,
                          sizeof(s.input_freeze_gate), "input-freeze",
                          s.input_freeze_gate, true);
                trace_u64(pi, s,
                          ReplayScrubDiag::kChara_bInHitstunFlag_Off,
                          sizeof(s.in_hitstun), "hitstun",
                          s.in_hitstun, true);
                trace_u64(pi, s,
                          ReplayScrubDiag::kChara_bInBlockstunFlag_Off,
                          sizeof(s.in_blockstun), "blockstun",
                          s.in_blockstun, true);
            }
        }

        bool restore_hgcpu_post_read_exact_chara_fields(
            const uint8_t* sim_blob) noexcept
        {
            if (!sim_blob) return false;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            bool ok = true;
            const uintptr_t slot_rvas[2] = {
                ReplayScrubDiag::kRVA_CharaSlotP1,
                ReplayScrubDiag::kRVA_CharaSlotP2,
            };

            for (int pi = 0; pi < 2; ++pi)
            {
                void* chara_raw = nullptr;
                if (!SafeReadPtr(reinterpret_cast<const void*>(
                                     base + slot_rvas[pi]),
                                 &chara_raw) || !chara_raw)
                {
                    ok = false;
                    continue;
                }

                uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);
                const int32_t snap_base =
                    pi * kHgCpuPerCharaSnapshotBytes;

                auto restore_chara_bytes =
                    [&](uintptr_t chara_off, size_t bytes) noexcept
                {
                    const int32_t local =
                        hgcpu_snapshot_local_for_chara_offset(chara_off);
                    if (local < 0)
                    {
                        ok = false;
                        return;
                    }
                    ok &= SafeWriteBytes(
                        c + chara_off,
                        sim_blob + snap_base + local,
                        bytes);
                };

                // LuxBattle_HgCpuDirect_ReadCharaStateFromSnapshot
                // intentionally preserves several live-side fields after
                // reading the main chara block.  That is correct for the
                // engine's internal rollback callers, but replay timeline
                // seek needs the exact captured frame.
                restore_chara_bytes(0x244, 4);
                restore_chara_bytes(0x248, 4);
                restore_chara_bytes(0x324, 4);
                restore_chara_bytes(0x1FF8, 0x88);

                // The reader restores these sub-objects and then native
                // fixup code can rebuild parts of them.  Re-apply the
                // captured bytes so the restored frame is what generation
                // actually captured, without running a simulation step.
                void* motion_bank = nullptr;
                void* secondary_motion = nullptr;
                void* vtable = nullptr;
                void* fn_raw = nullptr;
                if (SafeReadPtr(c + 0x35A0, &vtable)
                    && vtable
                    && SafeReadPtr(reinterpret_cast<uint8_t*>(vtable) + 0x28,
                                   &fn_raw)
                    && fn_raw
                    && SafeInvokeNativePtrIntReturnsPtr(
                        reinterpret_cast<void* (__fastcall*)(void*, int)>(
                            fn_raw),
                        c + 0x35A0, 0, &motion_bank)
                    && motion_bank)
                {
                    ok &= SafeWriteBytes(
                        motion_bank,
                        sim_blob + snap_base + kHgCpuMotionBankBlockStart,
                        0x1840);
                }
                else
                {
                    ok = false;
                }

                vtable = nullptr;
                fn_raw = nullptr;
                if (SafeReadPtr(c + 0x27760, &vtable)
                    && vtable
                    && SafeReadPtr(reinterpret_cast<uint8_t*>(vtable) + 0x28,
                                   &fn_raw)
                    && fn_raw
                    && SafeInvokeNativePtrIntReturnsPtr(
                        reinterpret_cast<void* (__fastcall*)(void*, int)>(
                            fn_raw),
                        c + 0x27760, 0, &secondary_motion)
                    && secondary_motion)
                {
                    ok &= SafeWriteBytes(
                        secondary_motion,
                        sim_blob + snap_base
                            + kHgCpuSecondaryMotionBlockStart,
                        0x800);
                }
                else
                {
                    ok = false;
                }
            }

            if (!ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] exact chara post-read restore "
                    "failed; selected frame cannot be trusted\n"));
            }
            return ok;
        }

        void trace_captured_restore_failure(
            const char* label,
            const CapturedFrameRestoreReport& out,
            const char* stage,
            const char* detail,
            int32_t requested_tick,
            bool tag_ok,
            bool sim_blob_ok,
            bool input_log_blob_ok,
            bool rdb_blob_ok,
            bool extras_blob_ok) noexcept
        {
            const int32_t live_round = read_current_round();
            const int32_t live_master = read_engine_master_clock();
            Sc6ReplaySeekContext ctx{};
            const bool ctx_ok = resolve_sc6_replay_seek_context(ctx);

            ReplayTraceFields f;
            f.string("label", label ? label : "?")
             .string("stage", stage ? stage : "?")
             .string("detail", detail ? detail : "?")
             .integer("requested_tick", requested_tick)
             .integer("seq", out.seq)
             .integer("tick", out.tick)
             .integer("round", out.round)
             .integer("master", out.master)
             .string("failure", native_seek_failure_name(out.failure))
             .integer("live_round", live_round)
             .integer("live_master", live_master)
             .boolean("tag_ok", tag_ok)
             .boolean("sim_blob_ok", sim_blob_ok)
             .boolean("input_log_blob_ok", input_log_blob_ok)
             .boolean("rdb_blob_ok", rdb_blob_ok)
             .boolean("extras_blob_ok", extras_blob_ok)
             .boolean("sim_restore_ok", out.sim_restore_ok)
             .boolean("input_log_restore_ok", out.input_log_restore_ok)
             .boolean("rdb_restore_ok", out.rdb_restore_ok)
             .boolean("extras_restore_ok", out.extras_restore_ok)
             .boolean("cursor_write_ok", out.cursor_write_ok)
             .boolean("rp_cursor_write_ok",
                      out.replay_player_cursor_write_ok)
             .integer("job_generation", m_sc6_seek_job.generation)
             .integer("requested_seq", m_sc6_seek_job.requested_seq)
             .integer("target_seq", m_sc6_seek_job.target_seq)
             .integer("target_round", m_sc6_seek_job.target_round)
             .integer("target_master", m_sc6_seek_job.target_master)
             .integer("origin_seq",
                      m_sc6_seek_job.validation_origin_seq)
             .integer("origin_master",
                      m_sc6_seek_job.validation_origin_master)
             .integer("compare_seq",
                      m_sc6_seek_job.validation_compare_seq)
             .integer("compare_master",
                      m_sc6_seek_job.validation_compare_master)
             .string("validation_mode",
                     captured_seek_validation_mode_name(
                         m_sc6_seek_job.validation_mode))
             .boolean("needs_cross_round_reset",
                      m_sc6_seek_job.needs_cross_round_reset)
             .boolean("cross_round_reset_applied",
                      m_sc6_seek_job.cross_round_reset_applied)
             .boolean("ctx_ok", ctx_ok)
             .hex("bm", ctx.battle_manager)
             .hex("input_log", ctx.input_log)
             .hex("replay_player", ctx.replay_player)
             .hex("state_reset_data", ctx.state_reset_data)
             .integer("ctx_current_round", ctx.current_round)
             .integer("ctx_input_master", ctx.input_master)
             .integer("ctx_battle_master", ctx.battle_master)
             .boolean("bm_ok", ctx.battle_manager_ok)
             .boolean("input_log_ok", ctx.input_log_ok)
             .boolean("replay_player_ok", ctx.replay_player_ok)
             .boolean("state_reset_data_ok", ctx.state_reset_data_ok)
             .boolean("interactive_replay_ok", ctx.interactive_replay_ok);
            ReplayDebugTrace::instance().event(
                "captured_seek_restore_failed_detail", f);

            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub.sc6seek] captured restore detail "
                "label={} stage={} detail={} requested_tick={} seq={} "
                "round={} master={} failure={} live_round={} "
                "live_master={} tag={} blobs[sim={} il={} rdb={} "
                "extras={}] steps[sim={} il={} rdb={} extras={} "
                "cursor={} rp={}] job[target_seq={} target_round={} "
                "target_master={} mode={} cross_round={} reset_applied={}] "
                "ctx[ok={} bm=0x{:X} il=0x{:X} rp=0x{:X} "
                "state_reset=0x{:X} ctx_round={} ctx_input_master={} "
                "ctx_battle_master={} bm_ok={} il_ok={} rp_ok={} "
                "reset_ok={} ir_ok={}]\n"),
                RC::to_generic_string(label ? label : "?"),
                RC::to_generic_string(stage ? stage : "?"),
                RC::to_generic_string(detail ? detail : "?"),
                requested_tick, out.seq, out.round, out.master,
                RC::to_generic_string(native_seek_failure_name(out.failure)),
                live_round, live_master, tag_ok ? 1 : 0,
                sim_blob_ok ? 1 : 0,
                input_log_blob_ok ? 1 : 0,
                rdb_blob_ok ? 1 : 0,
                extras_blob_ok ? 1 : 0,
                out.sim_restore_ok ? 1 : 0,
                out.input_log_restore_ok ? 1 : 0,
                out.rdb_restore_ok ? 1 : 0,
                out.extras_restore_ok ? 1 : 0,
                out.cursor_write_ok ? 1 : 0,
                out.replay_player_cursor_write_ok ? 1 : 0,
                m_sc6_seek_job.target_seq,
                m_sc6_seek_job.target_round,
                m_sc6_seek_job.target_master,
                RC::to_generic_string(captured_seek_validation_mode_name(
                    m_sc6_seek_job.validation_mode)),
                m_sc6_seek_job.needs_cross_round_reset ? 1 : 0,
                m_sc6_seek_job.cross_round_reset_applied ? 1 : 0,
                ctx_ok ? 1 : 0,
                static_cast<unsigned long long>(ctx.battle_manager),
                static_cast<unsigned long long>(ctx.input_log),
                static_cast<unsigned long long>(ctx.replay_player),
                static_cast<unsigned long long>(ctx.state_reset_data),
                ctx.current_round, ctx.input_master, ctx.battle_master,
                ctx.battle_manager_ok ? 1 : 0,
                ctx.input_log_ok ? 1 : 0,
                ctx.replay_player_ok ? 1 : 0,
                ctx.state_reset_data_ok ? 1 : 0,
                ctx.interactive_replay_ok ? 1 : 0);
        }

        bool restore_captured_frame_for_seek(
            int32_t tick,
            const char* label,
            CapturedFrameRestoreReport& out) noexcept
        {
            out = CapturedFrameRestoreReport{};
            out.tick = tick;
            if (!is_initialized() || !m_exec_read || tick < 0)
            {
                out.failure = NativeSeekFailure::InvalidTarget;
                trace_captured_restore_failure(
                    label, out, "preflight", "not-initialized-or-bad-tick",
                    tick, false, false, false, false, false);
                return false;
            }

            int32_t wall = -1;
            if (!m_tags.get(static_cast<size_t>(tick),
                            out.seq, out.round, wall, out.master)
                || out.seq < 0 || out.round < 0 || out.master < 0)
            {
                out.failure = NativeSeekFailure::InvalidTarget;
                trace_captured_restore_failure(
                    label, out, "tag-lookup", "missing-or-invalid-tag",
                    tick, false, false, false, false, false);
                return false;
            }

            const uint8_t* sim_blob =
                m_sim_store.gather(static_cast<size_t>(tick));
            const uint8_t* il_blob =
                m_il_store.gather(static_cast<size_t>(tick));
            const uint8_t* rdb_blob =
                m_rdb_store.gather(static_cast<size_t>(tick));
            const uint8_t* extras_blob =
                m_extras_store.gather(static_cast<size_t>(tick));
            if (!sim_blob || !il_blob || !rdb_blob || !extras_blob)
            {
                out.failure = NativeSeekFailure::InvalidTarget;
                trace_captured_restore_failure(
                    label, out, "snapshot-gather", "missing-captured-blob",
                    tick, true, sim_blob != nullptr, il_blob != nullptr,
                    rdb_blob != nullptr, extras_blob != nullptr);
                return false;
            }

            m_shim.retarget(const_cast<uint8_t*>(sim_blob),
                            kSnapshotStride);
            out.sim_restore_ok = SafeInvokeExec(m_exec_read, &m_shim);
            if (!out.sim_restore_ok)
            {
                out.failure =
                    NativeSeekFailure::CapturedSnapshotRestoreFailed;
                trace_captured_restore_failure(
                    label, out, "sim-exec-read", "exec-finalize-and-post",
                    tick, true, true, true, true, true);
                return false;
            }
            (void)trace_restore_probe_chara_fields(
                "after-ExecFinalizeAndPost", sim_blob, out.seq, out.tick);
            trace_semantic_authority_for_tick(
                "after-ExecFinalizeAndPost", sim_blob, out.tick,
                label, out.seq);
            out.sim_restore_ok =
                restore_hgcpu_post_read_exact_chara_fields(sim_blob);
            if (!out.sim_restore_ok)
            {
                out.failure =
                    NativeSeekFailure::CapturedSnapshotRestoreFailed;
                trace_captured_restore_failure(
                    label, out, "post-read-chara-fields",
                    "restore-hgcpu-post-read-exact-fields",
                    tick, true, true, true, true, true);
                return false;
            }
            (void)trace_restore_probe_chara_fields(
                "after-post-read-subblock-restore",
                sim_blob, out.seq, out.tick);
            trace_semantic_authority_for_tick(
                "after-post-read-subblock-restore", sim_blob, out.tick,
                label, out.seq);

            out.input_log_restore_ok = restore_input_cache(il_blob);
            if (!out.input_log_restore_ok)
            {
                out.failure = NativeSeekFailure::Sc6InputLogRestoreFailed;
                trace_captured_restore_failure(
                    label, out, "input-log-restore", "restore-input-cache",
                    tick, true, true, true, true, true);
                return false;
            }
            (void)trace_restore_probe_chara_fields(
                "after-input-log-restore", sim_blob, out.seq, out.tick);
            trace_semantic_authority_for_tick(
                "after-input-log-restore", sim_blob, out.tick,
                label, out.seq);

            out.rdb_restore_ok = restore_replay_data_block(rdb_blob);
            if (!out.rdb_restore_ok)
            {
                out.failure =
                    NativeSeekFailure::Sc6ReplayDataBlockRestoreFailed;
                trace_captured_restore_failure(
                    label, out, "rdb-restore", "restore-replay-data-block",
                    tick, true, true, true, true, true);
                return false;
            }
            (void)trace_restore_probe_chara_fields(
                "after-rdb-restore", sim_blob, out.seq, out.tick);
            trace_semantic_authority_for_tick(
                "after-rdb-restore", sim_blob, out.tick, label, out.seq);

            out.extras_restore_ok = restore_extras(extras_blob, true);
            if (!out.extras_restore_ok)
            {
                out.failure = NativeSeekFailure::CapturedSnapshotRestoreFailed;
                trace_captured_restore_failure(
                    label, out, "extras-restore", "restore-extras",
                    tick, true, true, true, true, true);
                return false;
            }
            (void)trace_restore_probe_chara_fields(
                "after-extras-restore", sim_blob, out.seq, out.tick);
            trace_semantic_authority_for_tick(
                "after-extras-restore", sim_blob, out.tick,
                label, out.seq);
            reset_round_result_cinematic_ring_after_seek(
                label, out.seq, out.round, out.master);
            (void)trace_restore_probe_chara_fields(
                "after-cinematic-reset", sim_blob, out.seq, out.tick);
            trace_semantic_authority_for_tick(
                "after-cinematic-reset", sim_blob, out.tick,
                label, out.seq);

            int32_t snapshot_last_frame_id = -1;
            std::memcpy(&snapshot_last_frame_id,
                        il_blob + (kIL_nLastFrameID_Off
                                   - kIL_CaptureStart_Off),
                        sizeof(snapshot_last_frame_id));
            int32_t snapshot_frame_advance = -1;
            std::memcpy(&snapshot_frame_advance,
                        extras_blob + kExtras_Off_BM_FrameAdvance,
                        sizeof(snapshot_frame_advance));

            Sc6ReplaySeekContext ctx{};
            (void)resolve_sc6_replay_seek_context(ctx);
            out.cursor_write_ok = write_replay_cursors(
                out.master, snapshot_last_frame_id, ctx.battle_manager,
                snapshot_frame_advance);
            if (!out.cursor_write_ok)
            {
                out.failure = NativeSeekFailure::Sc6ReplayCursorWriteFailed;
                trace_captured_restore_failure(
                    label, out, "cursor-write", "write-replay-cursors",
                    tick, true, true, true, true, true);
                return false;
            }

            float captured_time = static_cast<float>(out.master) / 60.0f;
            int32_t captured_round = out.round;
            std::memcpy(&captured_time,
                        extras_blob + kExtras_Off_RP_CurrentTime,
                        sizeof(captured_time));
            std::memcpy(&captured_round,
                        extras_blob + kExtras_Off_RP_CurrentRound,
                        sizeof(captured_round));
            if (captured_round < 0) captured_round = out.round;
            out.replay_player_cursor_write_ok =
                write_replay_player_cursor(
                    captured_round, out.master, captured_time, true,
                    ctx.replay_player);
            if (!out.replay_player_cursor_write_ok)
            {
                out.failure =
                    NativeSeekFailure::Sc6ReplayPlayerCursorWriteFailed;
                trace_captured_restore_failure(
                    label, out, "replay-player-cursor-write",
                    "write-replay-player-cursor",
                    tick, true, true, true, true, true);
                return false;
            }
            (void)trace_restore_probe_chara_fields(
                "after-cursor-writes", sim_blob, out.seq, out.tick);
            trace_semantic_authority_for_tick(
                "after-cursor-writes", sim_blob, out.tick,
                label, out.seq);
            if (!restore_hgcpu_final_semantic_repair(
                    sim_blob, label, out.seq, out.tick))
            {
                out.failure =
                    NativeSeekFailure::CapturedSnapshotSemanticRepairFailed;
                trace_captured_restore_failure(
                    label, out, "final-semantic-repair",
                    "restore-hgcpu-final-semantic-repair",
                    tick, true, true, true, true, true);
                return false;
            }
            trace_semantic_authority_for_tick(
                "after-final-semantic-repair", sim_blob, out.tick,
                label, out.seq);
            if (!restore_oracle_semantic_overlay_for_tick(
                    out.tick, label, out.seq))
            {
                out.failure =
                    NativeSeekFailure::CapturedSnapshotSemanticRepairFailed;
                trace_captured_restore_failure(
                    label, out, "oracle-moveid-overlay",
                    "restore-oracle-semantic-overlay",
                    tick, true, true, true, true, true);
                return false;
            }
            trace_semantic_authority_for_tick(
                "after-oracle-moveid-overlay", sim_blob, out.tick,
                label, out.seq);

            const int32_t live_round = read_current_round();
            const int32_t live_master = read_engine_master_clock();
            const int32_t master_delta =
                (live_master >= out.master)
                    ? (live_master - out.master)
                    : (out.master - live_master);
            if ((live_round >= 0 && live_round != out.round)
                || live_master < 0 || master_delta > 1)
            {
                out.failure =
                    NativeSeekFailure::CapturedSnapshotRestoreFailed;
                trace_captured_restore_failure(
                    label, out, "round-master-sanity",
                    "live-round-or-master-mismatch",
                    tick, true, true, true, true, true);
                return false;
            }

            out.failure = NativeSeekFailure::None;
            uint64_t latest_input_p1 = 0;
            uint64_t latest_input_p2 = 0;
            std::memcpy(&latest_input_p1,
                        extras_blob + kExtras_Off_LatestEngineInput,
                        sizeof(latest_input_p1));
            std::memcpy(&latest_input_p2,
                        extras_blob + kExtras_Off_LatestEngineInput
                            + sizeof(latest_input_p1),
                        sizeof(latest_input_p2));
            const uint64_t latest_input_hash = hash_bytes64(
                extras_blob + kExtras_Off_LatestEngineInput,
                kExtras_LatestEngineInput_Bytes);
            const uint64_t camera_args_hash = hash_bytes64(
                extras_blob + kExtras_Off_PerFrameCameraArgs,
                kExtras_PerFrameCameraArgs_Bytes);
            const uint64_t input_ring_hash = hash_bytes64(
                extras_blob + kExtras_Off_InputRingEntries,
                kExtras_InputRingEntries_Bytes);
            const uint64_t lfsr_hash = hash_bytes64(
                extras_blob + kExtras_Off_LfsrState,
                kExtras_LfsrState_Bytes);
            uint32_t input_cursor_p1 = 0;
            uint32_t input_cursor_p2 = 0;
            uint32_t input_base_p1 = 0;
            uint32_t input_base_p2 = 0;
            uint32_t lfsr_index = 0;
            std::memcpy(&input_cursor_p1,
                        extras_blob + kExtras_Off_InputRingCursor,
                        sizeof(input_cursor_p1));
            std::memcpy(&input_cursor_p2,
                        extras_blob + kExtras_Off_InputRingCursor
                            + sizeof(input_cursor_p1),
                        sizeof(input_cursor_p2));
            std::memcpy(&input_base_p1,
                        extras_blob + kExtras_Off_InputRingBase,
                        sizeof(input_base_p1));
            std::memcpy(&input_base_p2,
                        extras_blob + kExtras_Off_InputRingBase
                            + sizeof(input_base_p1),
                        sizeof(input_base_p2));
            std::memcpy(&lfsr_index,
                        extras_blob + kExtras_Off_LfsrIndex,
                        sizeof(lfsr_index));
            ReplayTraceFields f;
            f.string("label", label ? label : "?")
             .integer("seq", out.seq)
             .integer("tick", out.tick)
             .integer("round", out.round)
             .integer("master", out.master)
             .boolean("sim_restore_ok", out.sim_restore_ok)
             .boolean("input_log_restore_ok", out.input_log_restore_ok)
             .boolean("rdb_restore_ok", out.rdb_restore_ok)
             .boolean("extras_restore_ok", out.extras_restore_ok)
             .boolean("cursor_write_ok", out.cursor_write_ok)
             .boolean("rp_cursor_write_ok",
                      out.replay_player_cursor_write_ok)
             .integer("live_round", live_round)
             .integer("live_master", live_master)
             .hex("latest_engine_input_p1", latest_input_p1)
             .hex("latest_engine_input_p2", latest_input_p2)
             .hex("latest_engine_input_hash", latest_input_hash)
             .hex("per_frame_camera_args_hash", camera_args_hash)
             .hex("input_ring_hash", input_ring_hash)
             .integer("input_ring_cursor_p1", input_cursor_p1)
             .integer("input_ring_cursor_p2", input_cursor_p2)
             .integer("input_ring_base_p1", input_base_p1)
             .integer("input_ring_base_p2", input_base_p2)
             .hex("lfsr_hash", lfsr_hash)
             .integer("lfsr_index", lfsr_index);
            ReplayDebugTrace::instance().event(
                "captured_seek_restore", f);
            return true;
        }

        bool capture_live_frame_for_compare(
            LiveCapturedFrameScratch& out) noexcept
        {
            out = LiveCapturedFrameScratch{};
            if (!is_initialized() || !m_exec_write)
                return false;
            try
            {
                out.sim.assign(kSnapshotStride, 0);
                out.input_log.assign(kIL_CaptureBytes, 0);
                out.rdb.assign(kRDB_Bytes, 0);
                out.extras.assign(kExtras_Bytes, 0);
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }

            m_shim.retarget(out.sim.data(), kSnapshotStride);
            out.sim_ok = SafeInvokeExec(m_exec_write, &m_shim);
            out.input_log_ok = capture_input_cache(out.input_log.data());
            out.rdb_ok = capture_replay_data_block(out.rdb.data());
            out.extras_ok = capture_extras(out.extras.data());
            return out.sim_ok && out.input_log_ok
                && out.rdb_ok && out.extras_ok;
        }

        static int32_t first_mismatch_offset(
            const uint8_t* expected,
            const uint8_t* live,
            size_t bytes,
            uint8_t& expected_byte,
            uint8_t& live_byte) noexcept
        {
            if (!expected || !live) return -1;
            for (size_t i = 0; i < bytes; ++i)
            {
                if (expected[i] != live[i])
                {
                    expected_byte = expected[i];
                    live_byte = live[i];
                    return static_cast<int32_t>(i);
                }
            }
            return -1;
        }

        struct CapturedCompareIgnoreRange
        {
            int region {-1};
            int32_t offset {0};
            int32_t size {0};
            const char* reason {"unspecified"};
        };

        static bool captured_compare_offset_ignored(
            int region,
            int32_t offset,
            const char** reason_out = nullptr) noexcept
        {
            // HgCpuDirect serializes each chara as 0x1400C bytes.  Ghidra
            // shows LuxBattle_PerFrameTick and LuxBattle_TickCharaInput
            // rebuild the live input/command-history state during the
            // validation step.  These bytes are useful diagnostics, but the
            // authority for landing is round/master plus semantic gameplay
            // oracle and offline replay input-authority verification.
            static constexpr int32_t kPerCharaSnapshotBytes = 0x1400C;
            static constexpr int32_t kMotionInputLocalStart = 0x16C0;
            static constexpr int32_t kMotionInputLocalSize = 0x180;
            static constexpr int32_t kCharaInputLocalStart = 0x2144;
            static constexpr int32_t kCharaInputLocalSize = 0x1020;
            static constexpr int32_t kMotionBankLocalStart = 0x3D90;
            static constexpr int32_t kMotionBankLocalSize = 0x40;
            static constexpr int32_t kLookAtTargetALocalStart = 0x290;
            static constexpr int32_t kLookAtTargetBLocalStart = 0x2C0;
            static constexpr int32_t kLookAtSourceALocalStart = 0x2A0;
            static constexpr int32_t kLookAtSourceBLocalStart = 0x2D0;
            static constexpr int32_t kLookAtTargetLocalSize = 0x10;
            static constexpr int32_t kReadFixedSelfPtrLocalStart = 0x5670;
            static constexpr int32_t kReadFixedLanePtrsLocalStartA = 0x5778;
            static constexpr int32_t kReadFixedLanePtrsLocalStartB = 0x5BE0;
            static constexpr int32_t kVfxEffectAnchorLocalStart = 0x6708;
            static constexpr int32_t kVfxEffectAnchorLocalSize = 0x11F0;
            static constexpr CapturedCompareIgnoreRange kRanges[] = {
                {0, kMotionInputLocalStart, kMotionInputLocalSize,
                 "rebuilt FLuxBattleChara motion-input flag/history P1"},
                {0, kPerCharaSnapshotBytes + kMotionInputLocalStart,
                 kMotionInputLocalSize,
                 "rebuilt FLuxBattleChara motion-input flag/history P2"},
                {0, kCharaInputLocalStart, kCharaInputLocalSize,
                 "rebuilt FLuxBattleChara input/command-history window P1"},
                {0, kPerCharaSnapshotBytes + kCharaInputLocalStart,
                 kCharaInputLocalSize,
                 "rebuilt FLuxBattleChara input/command-history window P2"},
                {0, kMotionBankLocalStart, kMotionBankLocalSize,
                 "rebuilt chara+0x35A0 motion-bank interpolation P1"},
                {0, kPerCharaSnapshotBytes + kMotionBankLocalStart,
                 kMotionBankLocalSize,
                 "rebuilt chara+0x35A0 motion-bank interpolation P2"},
                {0, kLookAtTargetALocalStart, kLookAtTargetLocalSize,
                  "UpdateLookAtIKTarget rebuilds chara+0x2A0 look-at IK/VFX target vector A P1"},
                {0, kPerCharaSnapshotBytes + kLookAtTargetALocalStart,
                 kLookAtTargetLocalSize,
                 "UpdateLookAtIKTarget rebuilds chara+0x2A0 look-at IK/VFX target vector A P2"},
                {0, kLookAtSourceALocalStart, kLookAtTargetLocalSize,
                 "UpdateLookAtIKTarget rebuilds chara+0x2B0 look-at IK/VFX source vector A P1"},
                {0, kPerCharaSnapshotBytes + kLookAtSourceALocalStart,
                 kLookAtTargetLocalSize,
                 "UpdateLookAtIKTarget rebuilds chara+0x2B0 look-at IK/VFX source vector A P2"},
                {0, kLookAtTargetBLocalStart, kLookAtTargetLocalSize,
                 "UpdateLookAtIKTarget rebuilds chara+0x2D0 look-at IK/VFX target vector B P1"},
                {0, kPerCharaSnapshotBytes + kLookAtTargetBLocalStart,
                 kLookAtTargetLocalSize,
                 "UpdateLookAtIKTarget rebuilds chara+0x2D0 look-at IK/VFX target vector B P2"},
                {0, kLookAtSourceBLocalStart, kLookAtTargetLocalSize,
                 "UpdateLookAtIKTarget rebuilds chara+0x2E0 look-at IK/VFX source/temp vector B P1"},
                {0, kPerCharaSnapshotBytes + kLookAtSourceBLocalStart,
                 kLookAtTargetLocalSize,
                 "UpdateLookAtIKTarget rebuilds chara+0x2E0 look-at IK/VFX source/temp vector B P2"},
                {0, kReadFixedSelfPtrLocalStart, 0x30,
                 "native reader rewrites chara+0x43DF0 self-pointer block P1"},
                {0, kPerCharaSnapshotBytes + kReadFixedSelfPtrLocalStart,
                 0x30,
                 "native reader rewrites chara+0x43DF0 self-pointer block P2"},
                {0, kReadFixedLanePtrsLocalStartA, 0x10,
                 "native reader rewrites lane-state helper pointers P1"},
                {0, kPerCharaSnapshotBytes + kReadFixedLanePtrsLocalStartA,
                 0x10,
                 "native reader rewrites lane-state helper pointers P2"},
                {0, kReadFixedLanePtrsLocalStartB, 0x10,
                 "native reader rewrites lane-state helper pointers P1"},
                {0, kPerCharaSnapshotBytes + kReadFixedLanePtrsLocalStartB,
                  0x10,
                  "native reader rewrites lane-state helper pointers P2"},
                {0, kVfxEffectAnchorLocalStart,
                 kVfxEffectAnchorLocalSize,
                 "native reader/VFX dispatcher canonicalizes chara+0x95FA0 effect-anchor block P1"},
                {0, kPerCharaSnapshotBytes + kVfxEffectAnchorLocalStart,
                 kVfxEffectAnchorLocalSize,
                 "native reader/VFX dispatcher canonicalizes chara+0x95FA0 effect-anchor block P2"},
                {1, static_cast<int32_t>(0x3A8 - kIL_CaptureStart_Off),
                 8,
                 "InputLog pRecordedFrameBuffer is engine-owned and not restored"},
                {3, 0, static_cast<int32_t>(kExtras_Off_BlockInteractive),
                 "unused legacy WorldModePump extras bytes"},
                {3, static_cast<int32_t>(kExtras_Off_CinematicHead),
                 static_cast<int32_t>(kExtras_CinematicHead_Bytes),
                 "round-result cinematic metadata is reset after seek"},
            };
            for (const CapturedCompareIgnoreRange& range : kRanges)
            {
                if (region == range.region
                    && offset >= range.offset
                    && offset < range.offset + range.size)
                {
                    if (reason_out) *reason_out = range.reason;
                    return true;
                }
            }
            if (reason_out) *reason_out = nullptr;
            return false;
        }

        static std::string hex_window_around(
            const uint8_t* data,
            size_t bytes,
            int32_t center,
            size_t radius = 8) noexcept
        {
            if (!data || bytes == 0 || center < 0)
                return {};
            const size_t c = static_cast<size_t>(center);
            if (c >= bytes) return {};
            const size_t start = c > radius ? c - radius : 0;
            const size_t end = (c + radius + 1 < bytes)
                ? c + radius + 1 : bytes;
            std::string out;
            out.reserve((end - start) * 3);
            char buf[4] = {};
            for (size_t i = start; i < end; ++i)
            {
                if (!out.empty()) out.push_back(' ');
                std::snprintf(buf, sizeof(buf), "%02X",
                              static_cast<unsigned>(data[i]));
                out += buf;
            }
            return out;
        }

        static std::string describe_hgcpu_sim_offset(
            int32_t offset) noexcept
        {
            if (offset < 0) return {};
            // ExecMoveChangeAndPost/WriteCharaStateToSnapshot serializes two
            // chara records in the 0x28018-byte HgCpuDirect buffer.  Ghidra
            // shows the first record layout starts with chara+0x10 for 0x80
            // bytes, then chara+0x90 for 0x3510 bytes, then secondary
            // pointed sub-blocks.  This hint is diagnostic only.
            static constexpr int32_t kPerCharaSnapshotBytes = 0x1400C;
            const int32_t player =
                (offset >= kPerCharaSnapshotBytes) ? 2 : 1;
            const int32_t local = offset % kPerCharaSnapshotBytes;
            char buf[160] = {};
            if (local < 0x80)
            {
                const int32_t live_off = 0x10 + local;
                if (live_off == 0x8C)
                {
                    std::snprintf(buf, sizeof(buf),
                                  "P%d HgCpuDirect local=0x%X live_chara+0x8C "
                                  "(primary header dword; InitData clears, "
                                  "volatility not yet proven)",
                                  player, local);
                }
                else
                {
                    std::snprintf(buf, sizeof(buf),
                                  "P%d HgCpuDirect local=0x%X live_chara+0x%X "
                                  "(primary header block)",
                                  player, local, live_off);
                }
            }
            else if (local < 0x3590)
            {
                const int32_t live_off = 0x90 + (local - 0x80);
                if (live_off >= 0x2A0 && live_off < 0x2B0)
                {
                    std::snprintf(buf, sizeof(buf),
                                  "P%d HgCpuDirect local=0x%X live_chara+0x%X "
                                  "(look-at IK/VFX smoothed target vector A; "
                                  "UpdateLookAtIKTarget rebuilds from bones)",
                                  player, local, live_off);
                }
                else if (live_off >= 0x2B0 && live_off < 0x2C0)
                {
                    std::snprintf(buf, sizeof(buf),
                                  "P%d HgCpuDirect local=0x%X live_chara+0x%X "
                                  "(look-at IK/VFX source vector A; "
                                  "UpdateLookAtIKTarget rebuilds from bones)",
                                  player, local, live_off);
                }
                else if (live_off >= 0x2D0 && live_off < 0x2E0)
                {
                    std::snprintf(buf, sizeof(buf),
                                  "P%d HgCpuDirect local=0x%X live_chara+0x%X "
                                  "(look-at IK/VFX smoothed target vector B; "
                                  "UpdateLookAtIKTarget rebuilds from bones)",
                                  player, local, live_off);
                }
                else if (live_off >= 0x2E0 && live_off < 0x2F0)
                {
                    std::snprintf(buf, sizeof(buf),
                                  "P%d HgCpuDirect local=0x%X live_chara+0x%X "
                                  "(look-at IK/VFX source/temp vector B; "
                                  "UpdateLookAtIKTarget rebuilds from bones)",
                                  player, local, live_off);
                }
                else
                {
                    std::snprintf(buf, sizeof(buf),
                                  "P%d HgCpuDirect local=0x%X live_chara+0x%X "
                                  "(main chara state; includes input words "
                                  "around +0x2158)",
                                  player, local, live_off);
                }
            }
            else if (local < 0x4DD0)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X subblock+0x%X "
                              "(chara+0x35A0 pointed motion/bone state)",
                              player, local, local - 0x3590);
            }
            else if (local < 0x55D0)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X subblock+0x%X "
                              "(chara+0x27760 pointed state)",
                              player, local, local - 0x4DD0);
            }
            else if (local < kHgCpuMoveVelocityCacheBlockStart)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x2B3E0+0x%X "
                              "(move provider ref slot)",
                              player, local,
                              local - kHgCpuMoveProviderBlockStart);
            }
            else if (local < kHgCpuSelfPointerBlockStart)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x43D80+0x%X "
                              "(move velocity/cache block)",
                              player, local,
                              local - kHgCpuMoveVelocityCacheBlockStart);
            }
            else if (local < kHgCpuLaneStateBlockStart)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x43DF0+0x%X "
                              "(native reader self-pointer block)",
                              player, local,
                              local - kHgCpuSelfPointerBlockStart);
            }
            else if (local < kHgCpuLaneHelperBlockStart)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x444F0+0x%X "
                              "(LuxMoveLaneState block)",
                              player, local,
                              local - kHgCpuLaneStateBlockStart);
            }
            else if (local < kHgCpuVfxEffectAnchorBlockStart)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x45230+0x%X "
                              "(lane helper block)",
                              player, local,
                              local - kHgCpuLaneHelperBlockStart);
            }
            else if (local < kHgCpuFacingRetrackRampStart)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x95FA0+0x%X "
                              "(VFX/effect-anchor block)",
                              player, local,
                              local - kHgCpuVfxEffectAnchorBlockStart);
            }
            else if (local < kHgCpuAiResetSlotBlockStart)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x971A8+0x%X "
                              "(facingRetrackRamp; gameplay-facing retrack state)",
                              player, local,
                              local - kHgCpuFacingRetrackRampStart);
            }
            else if (local < 0x796C)
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X live_chara+0x971E8+0x%X "
                              "(MoveVM AI/reset slot state)",
                              player, local,
                              local - kHgCpuAiResetSlotBlockStart);
            }
            else
            {
                std::snprintf(buf, sizeof(buf),
                              "P%d HgCpuDirect local=0x%X "
                              "(unmapped serialized region)",
                              player, local);
            }
            return std::string(buf);
        }

        bool compare_live_frame_to_captured_tick(
            int32_t expected_tick,
            const LiveCapturedFrameScratch& live,
            CapturedFrameCompareReport& out) noexcept
        {
            out = CapturedFrameCompareReport{};
            out.expected_tick = expected_tick;
            int32_t wall = -1;
            if (expected_tick < 0
                || !m_tags.get(static_cast<size_t>(expected_tick),
                               out.expected_seq, out.expected_round,
                               wall, out.expected_master))
            {
                out.reason = "invalid-target";
                return false;
            }

            const uint8_t* sim_expected =
                m_sim_store.gather(static_cast<size_t>(expected_tick));
            const uint8_t* il_expected =
                m_il_store.gather(static_cast<size_t>(expected_tick));
            const uint8_t* rdb_expected =
                m_rdb_store.gather(static_cast<size_t>(expected_tick));
            const uint8_t* extras_expected =
                m_extras_store.gather(static_cast<size_t>(expected_tick));
            if (!sim_expected || !il_expected
                || !rdb_expected || !extras_expected
                || !live.sim_ok || !live.input_log_ok
                || !live.rdb_ok || !live.extras_ok)
            {
                out.reason = "capture-missing";
                return false;
            }

            out.expected_sim_hash =
                hash_bytes64(sim_expected, kSnapshotStride);
            out.live_sim_hash =
                hash_bytes64(live.sim.data(), kSnapshotStride);
            out.expected_input_log_hash =
                hash_bytes64(il_expected, kIL_CaptureBytes);
            out.live_input_log_hash =
                hash_bytes64(live.input_log.data(), kIL_CaptureBytes);
            out.expected_rdb_hash =
                hash_bytes64(rdb_expected, kRDB_Bytes);
            out.live_rdb_hash =
                hash_bytes64(live.rdb.data(), kRDB_Bytes);
            out.expected_extras_hash =
                hash_bytes64(extras_expected, kExtras_Bytes);
            out.live_extras_hash =
                hash_bytes64(live.extras.data(), kExtras_Bytes);

            bool sim_strict = true;
            bool il_strict = true;
            bool rdb_strict = true;
            bool extras_strict = true;
            auto region_match =
                [&out](int region, const uint8_t* expected,
                       const uint8_t* got, size_t bytes,
                       bool& strict_region) noexcept -> bool {
                    strict_region = true;
                    bool policy_ok = true;
                    for (size_t i = 0; i < bytes; ++i)
                    {
                        if (expected[i] == got[i]) continue;
                        strict_region = false;
                        const int32_t offset = static_cast<int32_t>(i);
                        const char* ignore_reason = nullptr;
                        if (captured_compare_offset_ignored(
                                region, offset, &ignore_reason))
                        {
                            ++out.ignored_mismatch_count;
                            if (out.first_ignored_mismatch_region < 0)
                            {
                                out.first_ignored_mismatch_region = region;
                                out.first_ignored_mismatch_offset = offset;
                                out.first_ignored_expected_byte =
                                    expected[i];
                                out.first_ignored_live_byte = got[i];
                                out.first_ignored_reason =
                                    ignore_reason ? ignore_reason : "ignored";
                            }
                            continue;
                        }

                        policy_ok = false;
                        if (region >= 0 && region < 4
                            && out.first_region_mismatch_offset[
                                static_cast<size_t>(region)] < 0)
                        {
                            const size_t region_index =
                                static_cast<size_t>(region);
                            out.first_region_mismatch_offset[region_index] =
                                offset;
                            out.first_region_expected_byte[region_index] =
                                expected[i];
                            out.first_region_live_byte[region_index] =
                                got[i];
                        }
                        if (out.first_mismatch_region < 0)
                        {
                            out.first_mismatch_region = region;
                            out.first_mismatch_offset = offset;
                            out.expected_byte = expected[i];
                            out.live_byte = got[i];
                        }
                    }
                    return policy_ok;
                };

            out.sim_match = region_match(
                0, sim_expected, live.sim.data(), kSnapshotStride,
                sim_strict);
            out.input_log_match = region_match(
                1, il_expected, live.input_log.data(), kIL_CaptureBytes,
                il_strict);
            out.rdb_match = region_match(
                2, rdb_expected, live.rdb.data(), kRDB_Bytes,
                rdb_strict);
            out.extras_match = region_match(
                3, extras_expected, live.extras.data(), kExtras_Bytes,
                extras_strict);
            out.ok = out.sim_match && out.input_log_match
                && out.rdb_match && out.extras_match;
            out.strict_match =
                sim_strict && il_strict && rdb_strict && extras_strict;
            out.policy_match = out.ok;
            out.reason = out.strict_match
                ? "ok"
                : (out.ok ? "policy-ignored-volatile"
                          : "byte-mismatch");
            return out.ok;
        }

        void trace_captured_snapshot_compare(
            const CapturedFrameCompareReport& report) noexcept
        {
            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .integer("expected_seq", report.expected_seq)
             .integer("expected_tick", report.expected_tick)
             .integer("expected_round", report.expected_round)
             .integer("expected_master", report.expected_master)
             .boolean("sim_match", report.sim_match)
             .boolean("input_log_match", report.input_log_match)
             .boolean("rdb_match", report.rdb_match)
             .boolean("extras_match", report.extras_match)
             .hex("expected_sim_hash", report.expected_sim_hash)
             .hex("live_sim_hash", report.live_sim_hash)
             .hex("expected_input_log_hash",
                  report.expected_input_log_hash)
             .hex("live_input_log_hash", report.live_input_log_hash)
             .hex("expected_rdb_hash", report.expected_rdb_hash)
             .hex("live_rdb_hash", report.live_rdb_hash)
             .hex("expected_extras_hash", report.expected_extras_hash)
             .hex("live_extras_hash", report.live_extras_hash)
              .integer("first_mismatch_region",
                       report.first_mismatch_region)
              .integer("first_mismatch_offset",
                       report.first_mismatch_offset)
              .integer("expected_byte", report.expected_byte)
              .integer("live_byte", report.live_byte)
              .integer("sim_first_mismatch_offset",
                       report.first_region_mismatch_offset[0])
              .integer("sim_first_expected_byte",
                       report.first_region_expected_byte[0])
              .integer("sim_first_live_byte",
                       report.first_region_live_byte[0])
              .integer("input_log_first_mismatch_offset",
                       report.first_region_mismatch_offset[1])
              .integer("input_log_first_expected_byte",
                       report.first_region_expected_byte[1])
              .integer("input_log_first_live_byte",
                       report.first_region_live_byte[1])
              .integer("rdb_first_mismatch_offset",
                       report.first_region_mismatch_offset[2])
              .integer("rdb_first_expected_byte",
                       report.first_region_expected_byte[2])
              .integer("rdb_first_live_byte",
                       report.first_region_live_byte[2])
              .integer("extras_first_mismatch_offset",
                       report.first_region_mismatch_offset[3])
              .integer("extras_first_expected_byte",
                       report.first_region_expected_byte[3])
              .integer("extras_first_live_byte",
                       report.first_region_live_byte[3])
              .boolean("strict_match", report.strict_match)
             .boolean("policy_match", report.policy_match)
             .integer("ignored_mismatch_count",
                      static_cast<int64_t>(report.ignored_mismatch_count))
             .integer("first_ignored_mismatch_region",
                      report.first_ignored_mismatch_region)
             .integer("first_ignored_mismatch_offset",
                      report.first_ignored_mismatch_offset)
             .integer("first_ignored_expected_byte",
                      report.first_ignored_expected_byte)
             .integer("first_ignored_live_byte",
                      report.first_ignored_live_byte)
             .string("first_ignored_reason",
                     report.first_ignored_reason
                         ? report.first_ignored_reason : "none")
             .boolean("ok", report.ok)
             .string("reason", report.reason ? report.reason : "?");
            ReplayDebugTrace::instance().event(
                "captured_snapshot_compare", f);
        }

        void trace_captured_snapshot_mismatch_detail(
            const CapturedFrameCompareReport& report,
            const LiveCapturedFrameScratch& live) noexcept
        {
            const bool using_ignored =
                report.ok
                && report.first_ignored_mismatch_region >= 0
                && report.first_ignored_mismatch_offset >= 0;
            const int32_t detail_region = using_ignored
                ? report.first_ignored_mismatch_region
                : report.first_mismatch_region;
            const int32_t detail_offset = using_ignored
                ? report.first_ignored_mismatch_offset
                : report.first_mismatch_offset;
            if (detail_region < 0 || detail_offset < 0)
                return;

            const uint8_t* expected = nullptr;
            const uint8_t* got = nullptr;
            size_t bytes = 0;
            const char* region = "unknown";
            switch (detail_region)
            {
            case 0:
                expected = m_sim_store.gather(
                    static_cast<size_t>(report.expected_tick));
                got = live.sim.data();
                bytes = kSnapshotStride;
                region = "sim";
                break;
            case 1:
                expected = m_il_store.gather(
                    static_cast<size_t>(report.expected_tick));
                got = live.input_log.data();
                bytes = kIL_CaptureBytes;
                region = "input_log";
                break;
            case 2:
                expected = m_rdb_store.gather(
                    static_cast<size_t>(report.expected_tick));
                got = live.rdb.data();
                bytes = kRDB_Bytes;
                region = "rdb";
                break;
            case 3:
                expected = m_extras_store.gather(
                    static_cast<size_t>(report.expected_tick));
                got = live.extras.data();
                bytes = kExtras_Bytes;
                region = "extras";
                break;
            default:
                return;
            }
            if (!expected || !got || bytes == 0) return;

            const std::string expected_near =
                hex_window_around(expected, bytes,
                                  detail_offset);
            const std::string live_near =
                hex_window_around(got, bytes,
                                  detail_offset);
            const std::string sim_hint =
                (detail_region == 0)
                    ? describe_hgcpu_sim_offset(detail_offset)
                    : std::string{};

            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .integer("expected_seq", report.expected_seq)
             .integer("expected_tick", report.expected_tick)
             .integer("expected_round", report.expected_round)
             .integer("expected_master", report.expected_master)
             .string("region", region)
             .integer("offset", detail_offset)
             .integer("expected_byte",
                      using_ignored
                          ? report.first_ignored_expected_byte
                          : report.expected_byte)
             .integer("live_byte",
                      using_ignored
                          ? report.first_ignored_live_byte
                          : report.live_byte)
             .boolean("ignored_by_policy", using_ignored)
             .string("ignore_reason",
                     using_ignored && report.first_ignored_reason
                         ? report.first_ignored_reason : "none")
             .string("nearby_expected", expected_near.c_str())
             .string("nearby_live", live_near.c_str());
            if (!sim_hint.empty())
                f.string("sim_offset_hint", sim_hint.c_str());
            ReplayDebugTrace::instance().event(
                "captured_snapshot_mismatch_detail", f);
        }

        void cancel_sc6_exact_seek(const char* reason) noexcept
        {
            if (sc6_exact_seek_phase_active(m_sc6_seek_job.phase))
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.sc6seek] cancelled seq={} label={} "
                    "reason={}\n"),
                    m_sc6_seek_job.requested_seq,
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    RC::to_generic_string(reason ? reason : "?"));
            }
            m_sc6_seek_job.phase = Sc6ExactSeekPhase::Cancelled;
            m_sc6_native_step_request.store(
                0, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::RestoredFrameHold),
                std::memory_order_release);
        }

        void finish_failed_seek_queue(NativeSeekFailure reason,
                                      int32_t requested_seq,
                                      const char* label) noexcept
        {
            if (reason == NativeSeekFailure::None)
                reason = NativeSeekFailure::InvalidTarget;

            Sc6ExactSeekJob failed{};
            failed.generation = m_seek_generation;
            failed.phase = Sc6ExactSeekPhase::Failed;
            failed.failure = reason;
            failed.requested_seq = requested_seq;
            failed.target_seq = requested_seq;
            failed.label = label ? label : "USER";
            m_sc6_seek_job = failed;
            m_ui_wants_play.store(false, std::memory_order_release);
            m_sc6_native_step_request.store(
                0, std::memory_order_release);

            const ReplayScrubHoldKind next_hold =
                has_context_valid_completed_timeline()
                    ? ReplayScrubHoldKind::RestoredFrameHold
                    : (has_completed_timeline()
                           ? ReplayScrubHoldKind::UiParkOnly
                           : ReplayScrubHoldKind::None);
            m_hold_kind.store(static_cast<int32_t>(next_hold),
                              std::memory_order_release);
            m_paused.store(next_hold != ReplayScrubHoldKind::None,
                           std::memory_order_release);

            publish_native_status(NativeSeekStatus::Failed, reason);
            publish_mode(ScrubMode::NativeSeekFailed);
            {
                ReplayTraceFields f;
                f.string("label", label ? label : "USER")
                 .integer("requested_seq", requested_seq)
                 .string("failure", native_seek_failure_name(reason))
                 .string("next_hold",
                         next_hold == ReplayScrubHoldKind::RestoredFrameHold
                             ? "RestoredFrameHold"
                             : (next_hold == ReplayScrubHoldKind::UiParkOnly
                                    ? "UiParkOnly" : "None"))
                 .boolean("timeline_done", has_completed_timeline())
                 .boolean("timeline_context_valid",
                          m_timeline_context_valid.load(
                              std::memory_order_acquire))
                 .boolean("timeline_seek_data_valid",
                          m_timeline_seek_data_valid.load(
                              std::memory_order_acquire));
                ReplayDebugTrace::instance().event(
                    "captured_seek_queue_failed", f);
            }
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub.sc6seek] seek queue failed label={} seq={} "
                "reason={} next_hold={}\n"),
                RC::to_generic_string(label ? label : "USER"),
                requested_seq,
                RC::to_generic_string(native_seek_failure_name(reason)),
                RC::to_generic_string(
                    next_hold == ReplayScrubHoldKind::RestoredFrameHold
                        ? "RestoredFrameHold"
                        : (next_hold == ReplayScrubHoldKind::UiParkOnly
                               ? "UiParkOnly"
                               : "None")));
        }

        bool queue_sc6_exact_seek(int32_t requested_seq,
                                  const char* label) noexcept
        {
            if (!has_context_valid_completed_timeline()
                || !m_timeline_seek_data_valid.load(
                    std::memory_order_acquire))
            {
                finish_failed_seek_queue(
                    NativeSeekFailure::TimelineIncomplete,
                    requested_seq, label);
                return false;
            }

            const int32_t original_requested_seq = requested_seq;
            int32_t tick = find_slot_for_seq(requested_seq);
            if (tick < 0)
            {
                finish_failed_seek_queue(NativeSeekFailure::InvalidTarget,
                                         requested_seq, label);
                return false;
            }

            const int32_t original_tick = tick;
            tick = adjust_seek_tick_away_from_round_boundary(tick);

            int32_t seq_tag = -1, round_tag = -1,
                    wall_tag = -1, master_tag = -1;
            if (!m_tags.get(static_cast<size_t>(tick),
                            seq_tag, round_tag, wall_tag, master_tag)
                || seq_tag < 0 || round_tag < 0 || master_tag < 0)
            {
                finish_failed_seek_queue(NativeSeekFailure::InvalidTarget,
                                         requested_seq, label);
                return false;
            }

            if (tick != original_tick || seq_tag != original_requested_seq)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] internal seek adjustment "
                    "suppressed label={} requested_seq={} candidate_seq={} "
                    "candidate_round={} candidate_master={} requested_tick={} "
                    "candidate_tick={}\n"),
                    RC::to_generic_string(label ? label : "USER"),
                    original_requested_seq, seq_tag, round_tag, master_tag,
                    original_tick, tick);
                {
                    ReplayTraceFields f;
                    f.string("label", label ? label : "USER")
                     .integer("requested_seq", original_requested_seq)
                     .integer("candidate_seq", seq_tag)
                     .integer("candidate_round", round_tag)
                     .integer("candidate_master", master_tag)
                     .integer("requested_tick", original_tick)
                     .integer("candidate_tick", tick);
                    ReplayDebugTrace::instance().event(
                        "captured_seek_adjustment_suppressed", f);
                }
                tick = original_tick;
                requested_seq = original_requested_seq;
                seq_tag = original_requested_seq;
                if (!m_tags.get(static_cast<size_t>(tick),
                                seq_tag, round_tag, wall_tag, master_tag)
                    || seq_tag < 0 || round_tag < 0 || master_tag < 0)
                {
                    finish_failed_seek_queue(
                        NativeSeekFailure::InvalidTarget,
                        requested_seq, label);
                    return false;
                }
            }

            uint8_t captured_main_state = 0;
            uint8_t captured_status = 0;
            if (!read_captured_bm_play_gate_for_tick(
                    tick, captured_main_state, captured_status)
                || captured_main_state != kBM_MainStateActiveBattle
                || captured_status != kBM_StatusActiveBattle)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] target is not a playable exact "
                    "state label={} requested_seq={} target_seq={} round={} "
                    "master={} bm.main=0x{:X} bm.status=0x{:X} "
                    "required_main=0x{:X} required_status=0x{:X}\n"),
                    RC::to_generic_string(label ? label : "USER"),
                    original_requested_seq, seq_tag, round_tag, master_tag,
                    static_cast<unsigned>(captured_main_state),
                    static_cast<unsigned>(captured_status),
                    static_cast<unsigned>(kBM_MainStateActiveBattle),
                    static_cast<unsigned>(kBM_StatusActiveBattle));
                ReplayTraceFields f;
                f.string("label", label ? label : "USER")
                 .integer("requested_seq", original_requested_seq)
                 .integer("target_seq", seq_tag)
                 .integer("target_tick", tick)
                 .integer("target_round", round_tag)
                 .integer("target_master", master_tag)
                 .uinteger("captured_main_state", captured_main_state)
                 .uinteger("captured_status", captured_status)
                 .uinteger("required_main_state", kBM_MainStateActiveBattle)
                 .uinteger("required_status", kBM_StatusActiveBattle)
                 .string("failure", native_seek_failure_name(
                     NativeSeekFailure::BattleManagerStatusNotActive));
                ReplayDebugTrace::instance().event(
                    "captured_seek_target_not_playable", f);
                finish_failed_seek_queue(
                    NativeSeekFailure::BattleManagerStatusNotActive,
                    requested_seq, label);
                return false;
            }

            const int32_t live_round_before_seek = read_current_round();
            const bool cross_round_seek =
                live_round_before_seek >= 0
                && live_round_before_seek != round_tag;

            if (m_sc6_seek_job.requested_seq == requested_seq
                && sc6_exact_seek_phase_active(m_sc6_seek_job.phase))
                return true;

            if (sc6_exact_seek_phase_active(m_sc6_seek_job.phase))
                cancel_sc6_exact_seek("new target");

            m_sc6_seek_job = Sc6ExactSeekJob{};
            m_sc6_seek_job.generation = m_seek_generation;
            m_sc6_seek_job.phase =
                Sc6ExactSeekPhase::RestoreValidationOrigin;
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::ValidationStep),
                std::memory_order_release);
            m_sc6_seek_job.authority =
                Sc6SeekAuthority::CapturedSnapshotValidated;
            m_sc6_seek_job.requested_seq = requested_seq;
            m_sc6_seek_job.target_tick = tick;
            m_sc6_seek_job.target_seq = seq_tag;
            m_sc6_seek_job.target_round = round_tag;
            m_sc6_seek_job.target_master = master_tag;
            m_sc6_seek_job.label = label ? label : "USER";
            m_sc6_seek_job.needs_cross_round_reset = cross_round_seek;
            if (!choose_captured_seek_validation_origin(m_sc6_seek_job))
            {
                finish_failed_seek_queue(
                    NativeSeekFailure::CapturedGameplayStepFailed,
                                          requested_seq, label);
                return false;
            }
            m_sc6_seek_job.native_step_observed_credits =
                m_sc6_native_step_granted.load(
                    std::memory_order_acquire);
            m_sc6_native_step_request.store(
                0, std::memory_order_release);

            m_native.requested_seq = requested_seq;
            m_native.adjusted_seq = seq_tag;
            m_native.target_ms = -1;
            m_native.round = round_tag;
            m_native.master = master_tag;
            m_native.failure = NativeSeekFailure::None;
            m_native.direct_driver_available = false;
            m_native.cvar_submitted = false;
            publish_native_status(NativeSeekStatus::Queued,
                                  NativeSeekFailure::None);
            publish_mode(ScrubMode::NativeSeekQueued);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.sc6seek] captured seek queued label={} "
                "seq={} round={} master={} tick={} mode={} origin_seq={} "
                "origin_master={} compare_seq={} compare_master={} "
                "generation={} live_round={} cross_round_reset={}\n"),
                RC::to_generic_string(m_sc6_seek_job.label),
                requested_seq, round_tag, master_tag, tick,
                RC::to_generic_string(captured_seek_validation_mode_name(
                    m_sc6_seek_job.validation_mode)),
                m_sc6_seek_job.validation_origin_seq,
                m_sc6_seek_job.validation_origin_master,
                m_sc6_seek_job.validation_compare_seq,
                m_sc6_seek_job.validation_compare_master,
                m_sc6_seek_job.generation,
                live_round_before_seek, cross_round_seek ? 1 : 0);
            {
                ReplayTraceFields f;
                f.string("label", m_sc6_seek_job.label)
                 .integer("requested_seq", requested_seq)
                 .integer("target_seq", seq_tag)
                 .integer("target_round", round_tag)
                 .integer("target_master", master_tag)
                 .integer("target_tick", tick)
                 .integer("origin_seq",
                          m_sc6_seek_job.validation_origin_seq)
                 .integer("origin_master",
                          m_sc6_seek_job.validation_origin_master)
                 .integer("compare_seq",
                          m_sc6_seek_job.validation_compare_seq)
                 .integer("compare_master",
                          m_sc6_seek_job.validation_compare_master)
                 .string("validation_mode",
                         captured_seek_validation_mode_name(
                             m_sc6_seek_job.validation_mode))
                 .integer("job_generation", m_sc6_seek_job.generation)
                 .integer("live_round_before_seek", live_round_before_seek)
                 .boolean("cross_round_snapshot", false)
                 .boolean("cross_round_reset", cross_round_seek)
                 .string("status", "Queued")
                 .string("failure", "None");
                ReplayDebugTrace::instance().event(
                    "captured_seek_queued", f);
            }
            if (cross_round_seek)
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.sc6seek] cross-round seek will apply "
                    "native round reset context before captured restore "
                    "label={} seq={} live_round={} target_round={} "
                    "target_master={} origin_seq={} origin_master={}\n"),
                    RC::to_generic_string(m_sc6_seek_job.label),
                    requested_seq, live_round_before_seek, round_tag,
                    master_tag,
                    m_sc6_seek_job.validation_origin_seq,
                    m_sc6_seek_job.validation_origin_master);
                ReplayTraceFields f;
                f.string("label", m_sc6_seek_job.label)
                 .integer("requested_seq", requested_seq)
                 .integer("live_round", live_round_before_seek)
                 .integer("target_round", round_tag)
                 .integer("target_master", master_tag)
                 .integer("target_tick", tick)
                 .integer("origin_seq",
                          m_sc6_seek_job.validation_origin_seq)
                 .integer("origin_master",
                          m_sc6_seek_job.validation_origin_master)
                 .boolean("cross_round_reset", true);
                ReplayDebugTrace::instance().event(
                    "cross_round_reset_context_queued", f);
            }
            return true;
        }

        bool verify_round_master_landing(
            const Sc6ExactSeekJob& job,
            const Sc6ReplaySeekContext& ctx,
            Sc6SeekVerifyReport& out) noexcept
        {
            out.live_round = read_current_round();
            out.replay_player_round = ctx.current_round;
            out.input_master = ctx.input_master;
            out.battle_master = ctx.battle_master;
            out.battle_master_delta =
                (out.battle_master >= job.target_master)
                    ? (out.battle_master - job.target_master)
                    : (job.target_master - out.battle_master);

            if (job.requested_seq
                != m_ui_requested_seq.load(std::memory_order_acquire))
            {
                out.reason = "stale-request";
                return false;
            }
            if (ctx.replay_player_ok
                && out.replay_player_round != job.target_round)
            {
                out.reason = "replay-player-round";
                return false;
            }
            if (out.live_round >= 0 && out.live_round != job.target_round)
            {
                out.reason = "live-round";
                return false;
            }
            if (out.input_master != job.target_master)
            {
                out.reason = "input-master";
                return false;
            }
            if (out.battle_master < 0 || out.battle_master_delta > 1)
            {
                out.reason = "battle-master";
                return false;
            }

            out.round_master_ok = true;
            return true;
        }

        void trace_offline_chara_ring_diagnostic(
            const Sc6ExactSeekJob& job,
            ReplayInputAuthorityReport& report) noexcept
        {
            const uint8_t* expected_extras = nullptr;
            if (job.target_tick >= 0)
                expected_extras =
                    m_extras_store.gather(static_cast<size_t>(
                        job.target_tick));

            const uintptr_t base = NativeBinding::imageBase();
            bool all_readable = expected_extras != nullptr && base != 0;
            bool all_match = all_readable;
            for (int slot = 0; slot < 2; ++slot)
            {
                const size_t blob_off = (slot == 0)
                    ? kExtras_Off_P1_CharaReplay
                    : kExtras_Off_P2_CharaReplay;
                uint8_t live[kExtras_CharaReplay_Bytes] = {};
                bool live_ok = false;
                uintptr_t chara_ptr = 0;
                if (base)
                {
                    void* chara_raw = nullptr;
                    const uintptr_t slot_rva = (slot == 0)
                        ? ReplayScrubDiag::kRVA_CharaSlotP1
                        : ReplayScrubDiag::kRVA_CharaSlotP2;
                    if (SafeReadPtr(reinterpret_cast<const void*>(
                                        base + slot_rva),
                                    &chara_raw) && chara_raw)
                    {
                        chara_ptr = reinterpret_cast<uintptr_t>(chara_raw);
                        live_ok = SafeReadBytes(
                            reinterpret_cast<const uint8_t*>(chara_raw)
                                + kChara_ReplayState_Start,
                            live, sizeof(live));
                    }
                }

                const uint8_t* expected = expected_extras
                    ? expected_extras + blob_off : nullptr;
                const uint64_t expected_hash = expected
                    ? hash_bytes64(expected, kExtras_CharaReplay_Bytes) : 0;
                const uint64_t live_hash = live_ok
                    ? hash_bytes64(live, sizeof(live)) : 0;
                const bool match = expected && live_ok
                    && expected_hash == live_hash;
                all_readable = all_readable && expected && live_ok;
                all_match = all_match && match;

                ReplayTraceFields f;
                f.string("label", job.label ? job.label : "?")
                 .integer("requested_seq", job.requested_seq)
                 .integer("target_tick", job.target_tick)
                 .integer("target_round", job.target_round)
                 .integer("target_master", job.target_master)
                 .integer("slot", slot)
                 .hex("chara", chara_ptr)
                 .boolean("readable", expected && live_ok)
                 .hex("expected_hash", expected_hash)
                 .hex("live_hash", live_hash)
                 .boolean("match", match)
                 .boolean("diagnostic_only", true)
                 .string("reason",
                         "chara-replay-ring-layout-not-final-gate");
                ReplayDebugTrace::instance().event(
                    "offline_chara_ring_verify", f);
            }

            report.chara_replay_ring_ok = all_readable && all_match;
        }

        bool verify_replay_input_authority(
            const Sc6ExactSeekJob& job,
            const Sc6ReplaySeekContext& ctx,
            ReplayInputAuthorityReport& report) noexcept
        {
            report = ReplayInputAuthorityReport{};
            report.authority = ReplayInputAuthority::OfflineCharaReplayRing;
            report.simulation_cache_diagnostic_only = true;

            if (job.target_tick < 0
                || static_cast<size_t>(job.target_tick)
                    >= m_oracle_frames.size())
            {
                report.reason = "oracle-missing";
                return false;
            }

            const ReplayFrameOracleSnap& expected =
                m_oracle_frames[static_cast<size_t>(job.target_tick)];
            if (!expected.valid)
            {
                report.reason = "oracle-invalid";
                return false;
            }

            int32_t active_count = -1;
            uint32_t active_mask = 0;
            const bool active_count_ok = SafeReadInt32(
                reinterpret_cast<const void*>(ctx.input_log + 0x398),
                &active_count);
            const bool active_mask_ok = SafeReadUInt32(
                reinterpret_cast<const void*>(ctx.input_log + 0x39C),
                &active_mask);
            report.active_count = active_count;
            report.active_mask = active_mask;
            if (!active_count_ok || !active_mask_ok)
            {
                report.reason = "active-metadata-read";
                return false;
            }
            if (active_count < 1 || active_count > 2
                || (active_mask & ~0x3u) != 0
                || (active_mask & ((1u << active_count) - 1u)) == 0)
            {
                report.reason = "active-metadata";
                return false;
            }

            ReplayScrubDiag::LatestEngineInputSnap live_latest =
                ReplayScrubDiag::read_latest_engine_input();
            if (!live_latest.readable)
            {
                report.reason = "latest-engine-input-read";
                return false;
            }

            void* frame_input_raw = nullptr;
            const bool frame_input_ptr_ok =
                SafeReadPtr(reinterpret_cast<const void*>(
                                ctx.battle_manager
                                    + kBM_FrameInputActor_Off),
                            &frame_input_raw)
                && frame_input_raw != nullptr;
            const uintptr_t frame_input = frame_input_ptr_ok
                ? reinterpret_cast<uintptr_t>(frame_input_raw)
                : 0;

            bool current_ok = true;
            bool latest_ok = true;
            bool frame_ok = frame_input_ptr_ok;
            for (int slot = 0; slot < active_count; ++slot)
            {
                if ((active_mask & (1u << slot)) == 0) continue;
                const uint64_t expected_latest = (slot == 0)
                    ? expected.p1_input : expected.p2_input;
                const uint64_t live_latest_value = (slot == 0)
                    ? live_latest.p1_input : live_latest.p2_input;
                const uint32_t expected_current =
                    static_cast<uint32_t>(expected_latest & 0xFFFFFFFFu);
                const uint32_t live_latest_current =
                    static_cast<uint32_t>(live_latest_value & 0xFFFFFFFFu);

                uint32_t current_input = 0;
                if (!SafeReadUInt32(reinterpret_cast<const void*>(
                                        ctx.input_log + 0x3B8
                                            + static_cast<uintptr_t>(slot) * 4),
                                    &current_input))
                {
                    report.reason = "current-input-read";
                    report.failing_slot = slot;
                    return false;
                }

                uint32_t frame_input_value = 0;
                if (frame_input != 0
                    && !SafeReadUInt32(reinterpret_cast<const void*>(
                                           frame_input
                                               + kFI_SlotRecords_Start
                                               + static_cast<uintptr_t>(slot)
                                                   * kFI_SlotRecord_Stride),
                                       &frame_input_value))
                {
                    frame_ok = false;
                }
                else if (frame_input == 0)
                {
                    frame_ok = false;
                }

                report.failing_slot = slot;
                report.expected_current_input = expected_current;
                report.live_current_input = current_input;
                report.frame_input_value = frame_input_value;
                report.expected_latest_engine_input = expected_latest;
                report.live_latest_engine_input = live_latest_value;

                if (current_input != expected_current)
                {
                    current_ok = false;
                    report.reason = "current-input";
                    break;
                }
                if (live_latest_current != expected_current)
                {
                    latest_ok = false;
                    report.reason = "latest-engine-input";
                    break;
                }
                if (frame_input_value != expected_current)
                {
                    frame_ok = false;
                }

                ++report.checked_slots;
            }

            report.current_input_ok = current_ok;
            report.latest_engine_input_ok = latest_ok;
            report.frame_input_ok = frame_ok;
            report.frame_input_diagnostic_only = true;
            trace_offline_chara_ring_diagnostic(job, report);

            report.ok = current_ok && latest_ok && report.checked_slots > 0;
            if (!report.ok)
            {
                if (report.checked_slots <= 0 && current_ok && latest_ok)
                    report.reason = "no-active-input-slots";
                return false;
            }

            report.failing_slot = -1;
            report.reason = "ok";
            return true;
        }

        bool trace_inputlog_cache_diagnostic(
            const Sc6ExactSeekJob& job,
            const Sc6ReplaySeekContext& ctx,
            Sc6SeekVerifyReport& out) noexcept
        {
            int32_t active_count = out.active_count;
            uint32_t active_mask = out.active_mask;
            if (active_count < 0)
            {
                (void)SafeReadInt32(reinterpret_cast<const void*>(
                                        ctx.input_log + 0x398),
                                    &active_count);
                (void)SafeReadUInt32(reinterpret_cast<const void*>(
                                         ctx.input_log + 0x39C),
                                     &active_mask);
            }

            int32_t live_last_frame_id = -1;
            if (!SafeReadInt32(reinterpret_cast<const void*>(
                                   ctx.input_log + kIL_nLastFrameID_Off),
                               &live_last_frame_id))
            {
                out.cache_checked = true;
                out.simulation_cache_ok = false;
                return false;
            }

            bool all_ok = true;
            if (active_count < 1 || active_count > 2)
                all_ok = false;
            else
            {
                for (int slot = 0; slot < active_count; ++slot)
                {
                    if ((active_mask & (1u << slot)) == 0) continue;
                    const uintptr_t entry =
                        ctx.input_log + kIL_InputCacheStart_Off
                        + static_cast<uintptr_t>(slot) * 0x2000
                        + static_cast<uintptr_t>(
                              job.target_master & 0x1FF) * 0x10;
                    int32_t frame_id = -1;
                    uint32_t frame_index = 0;
                    uint32_t input_value = 0;
                    uint32_t current_input = 0;
                    uint8_t filled = 0;
                    const bool read_ok =
                        SafeReadInt32(reinterpret_cast<const void*>(entry),
                                      &frame_id)
                        && SafeReadUInt32(
                               reinterpret_cast<const void*>(entry + 4),
                               &frame_index)
                        && SafeReadUInt32(
                               reinterpret_cast<const void*>(entry + 8),
                               &input_value)
                        && SafeReadUInt8(
                               reinterpret_cast<const void*>(entry + 0xC),
                               &filled)
                        && SafeReadUInt32(
                               reinterpret_cast<const void*>(
                                   ctx.input_log + 0x3B8
                                   + static_cast<uintptr_t>(slot) * 4),
                               &current_input);
                    out.cache_checked = true;
                    ++out.checked_slots;
                    out.cache_slot = slot;
                    out.cache_expected_frame_id = live_last_frame_id;
                    out.cache_frame_id = frame_id;
                    out.cache_frame_index = frame_index;
                    out.cache_input_value = input_value;
                    out.current_input_value = current_input;
                    out.cache_filled = filled;

                    const bool slot_ok = read_ok
                        && frame_id == live_last_frame_id
                        && static_cast<int32_t>(frame_index)
                            == job.target_master
                        && filled != 0
                        && input_value == current_input;
                    all_ok = all_ok && slot_ok;
                    if (!slot_ok)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[ReplayScrub.sc6seek] InputLog cache "
                            "diagnostic mismatch label={} seq={} slot={} "
                            "frame_id={}/{} frame_index={} "
                            "target_master={} filled={} cache_input=0x{:X} "
                            "current_input=0x{:X} "
                            "ignored_for_offline_replay=1\n"),
                            RC::to_generic_string(job.label
                                ? job.label : "?"),
                            job.requested_seq, slot, frame_id,
                            live_last_frame_id, frame_index,
                            job.target_master,
                            static_cast<unsigned>(filled),
                            input_value, current_input);
                    }
                    ReplayTraceFields f;
                    f.string("label", job.label ? job.label : "?")
                     .integer("requested_seq", job.requested_seq)
                     .integer("target_round", job.target_round)
                     .integer("target_master", job.target_master)
                     .integer("slot", slot)
                     .integer("frame_id", frame_id)
                     .integer("expected_frame_id", live_last_frame_id)
                     .integer("frame_index", frame_index)
                     .integer("filled", filled)
                     .hex("cache_input", input_value)
                     .hex("current_input", current_input)
                     .boolean("ok", slot_ok)
                     .boolean("diagnostic_only", true)
                     .string("reason",
                             slot_ok ? "ok"
                                     : "offline-replay-cache-not-authority");
                    ReplayDebugTrace::instance().event(
                        "inputlog_cache_diagnostic", f);
                }
            }
            out.simulation_cache_ok = all_ok && out.checked_slots > 0;
            out.simulation_cache_diagnostic_only = true;
            return out.simulation_cache_ok;
        }

        bool verify_sc6_exact_landing(const Sc6ExactSeekJob& job,
                                      Sc6SeekVerifyReport& out) noexcept
        {
            out = Sc6SeekVerifyReport{};
            Sc6ReplaySeekContext ctx{};
            if (!resolve_sc6_replay_seek_context(ctx))
            {
                out.reason = "context";
                return false;
            }

            if (!verify_round_master_landing(job, ctx, out))
                return false;

            ReplayInputAuthorityReport input_report{};
            if (!verify_replay_input_authority(job, ctx, input_report))
            {
                out.input_authority = input_report.authority;
                out.input_authority_ok = false;
                out.input_authority_reason = input_report.reason;
                out.input_authority_checked_slots =
                    input_report.checked_slots;
                out.input_authority_slot = input_report.failing_slot;
                out.expected_current_input =
                    input_report.expected_current_input;
                out.live_current_input =
                    input_report.live_current_input;
                out.frame_input_value = input_report.frame_input_value;
                out.expected_latest_engine_input =
                    input_report.expected_latest_engine_input;
                out.live_latest_engine_input =
                    input_report.live_latest_engine_input;
                out.current_input_ok = input_report.current_input_ok;
                out.frame_input_ok = input_report.frame_input_ok;
                out.frame_input_diagnostic_only =
                    input_report.frame_input_diagnostic_only;
                out.latest_engine_input_ok =
                    input_report.latest_engine_input_ok;
                out.chara_replay_ring_ok =
                    input_report.chara_replay_ring_ok;
                out.active_count = input_report.active_count;
                out.active_mask = input_report.active_mask;
                out.reason = input_report.reason;
                ReplayTraceFields f;
                f.string("label", job.label ? job.label : "?")
                 .integer("requested_seq", job.requested_seq)
                 .integer("target_round", job.target_round)
                 .integer("target_master", job.target_master)
                 .string("authority",
                         replay_input_authority_name(
                             input_report.authority))
                 .boolean("current_input_ok",
                          input_report.current_input_ok)
                 .boolean("frame_input_ok",
                          input_report.frame_input_ok)
                 .boolean("frame_input_diagnostic_only",
                          input_report.frame_input_diagnostic_only)
                 .boolean("latest_engine_input_ok",
                          input_report.latest_engine_input_ok)
                 .boolean("chara_replay_ring_ok",
                          input_report.chara_replay_ring_ok)
                 .boolean("simulation_cache_diagnostic_only", true)
                 .integer("checked_slots", input_report.checked_slots)
                 .integer("failing_slot", input_report.failing_slot)
                 .hex("expected_current_input",
                      input_report.expected_current_input)
                 .hex("live_current_input",
                      input_report.live_current_input)
                 .hex("frame_input_value", input_report.frame_input_value)
                 .hex("expected_latest_engine_input",
                      input_report.expected_latest_engine_input)
                 .hex("live_latest_engine_input",
                      input_report.live_latest_engine_input)
                 .boolean("ok", false)
                 .string("reason", input_report.reason
                             ? input_report.reason : "?");
                ReplayDebugTrace::instance().event(
                    "input_authority_verify", f);
                return false;
            }

            out.input_authority = input_report.authority;
            out.input_authority_ok = true;
            out.input_authority_reason = input_report.reason;
            out.input_authority_checked_slots =
                input_report.checked_slots;
            out.input_authority_slot = input_report.failing_slot;
            out.expected_current_input =
                input_report.expected_current_input;
            out.live_current_input = input_report.live_current_input;
            out.frame_input_value = input_report.frame_input_value;
            out.expected_latest_engine_input =
                input_report.expected_latest_engine_input;
            out.live_latest_engine_input =
                input_report.live_latest_engine_input;
            out.current_input_ok = input_report.current_input_ok;
            out.frame_input_ok = input_report.frame_input_ok;
            out.frame_input_diagnostic_only =
                input_report.frame_input_diagnostic_only;
            out.latest_engine_input_ok =
                input_report.latest_engine_input_ok;
            out.chara_replay_ring_ok =
                input_report.chara_replay_ring_ok;
            out.active_count = input_report.active_count;
            out.active_mask = input_report.active_mask;

            (void)trace_inputlog_cache_diagnostic(job, ctx, out);

            ReplayTraceFields f;
            f.string("label", job.label ? job.label : "?")
             .integer("requested_seq", job.requested_seq)
             .integer("target_round", job.target_round)
             .integer("target_master", job.target_master)
             .string("authority",
                     replay_input_authority_name(input_report.authority))
             .boolean("current_input_ok", input_report.current_input_ok)
             .boolean("frame_input_ok", input_report.frame_input_ok)
             .boolean("frame_input_diagnostic_only",
                      input_report.frame_input_diagnostic_only)
             .boolean("latest_engine_input_ok",
                      input_report.latest_engine_input_ok)
             .boolean("chara_replay_ring_ok",
                      input_report.chara_replay_ring_ok)
             .boolean("simulation_cache_ok", out.simulation_cache_ok)
             .boolean("simulation_cache_diagnostic_only", true)
             .integer("checked_slots", input_report.checked_slots)
             .integer("failing_slot", input_report.failing_slot)
             .hex("expected_current_input",
                  input_report.expected_current_input)
             .hex("live_current_input", input_report.live_current_input)
             .hex("frame_input_value", input_report.frame_input_value)
             .hex("expected_latest_engine_input",
                  input_report.expected_latest_engine_input)
             .hex("live_latest_engine_input",
                  input_report.live_latest_engine_input)
             .boolean("ok", true)
             .string("reason", input_report.reason
                         ? input_report.reason : "ok");
            ReplayDebugTrace::instance().event(
                "input_authority_verify", f);

            out.ok = true;
            out.reason = "ok";
            return true;
        }

        void fail_sc6_exact_seek(NativeSeekFailure failure,
                                 const Sc6SeekVerifyReport* report =
                                     nullptr) noexcept
        {
            const Sc6ExactSeekPhase failed_phase = m_sc6_seek_job.phase;
            m_sc6_seek_job.failure = failure;
            m_sc6_seek_job.phase = Sc6ExactSeekPhase::Failed;
            m_ui_wants_play.store(false, std::memory_order_release);
            m_sc6_native_step_request.store(
                0, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::RestoredFrameHold),
                std::memory_order_release);
            publish_native_status(NativeSeekStatus::Failed, failure);
            publish_mode(ScrubMode::NativeSeekFailed);
            if (report)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] verify failed seq={} label={} "
                    "reason={} round live={} rp={} target={} "
                    "input_master live={} target={} bm_master={} "
                    "bm_delta={} active_count={} active_mask=0x{:X} "
                    "input_authority={} input_ok={} "
                    "input_reason={} input_slots={} input_slot={} "
                    "expected_current=0x{:X} live_current=0x{:X} "
                    "frame_input=0x{:X} expected_latest=0x{:X} "
                    "live_latest=0x{:X} "
                    "cache_checked={} checked_slots={} cache_slot={} "
                    "cache_frame_id={}/{} cache_frame_index={} "
                    "cache_input=0x{:X} current_input=0x{:X} "
                    "cache_filled={}\n"),
                    m_sc6_seek_job.requested_seq,
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    RC::to_generic_string(report->reason
                        ? report->reason : "?"),
                    report->live_round, report->replay_player_round,
                    m_sc6_seek_job.target_round, report->input_master,
                    m_sc6_seek_job.target_master, report->battle_master,
                    report->battle_master_delta, report->active_count,
                    report->active_mask,
                    RC::to_generic_string(replay_input_authority_name(
                        report->input_authority)),
                    report->input_authority_ok ? 1 : 0,
                    RC::to_generic_string(report->input_authority_reason
                        ? report->input_authority_reason : "?"),
                    report->input_authority_checked_slots,
                    report->input_authority_slot,
                    report->expected_current_input,
                    report->live_current_input,
                    report->frame_input_value,
                    report->expected_latest_engine_input,
                    report->live_latest_engine_input,
                    report->cache_checked ? 1 : 0,
                    report->checked_slots,
                    report->cache_slot, report->cache_frame_id,
                    report->cache_expected_frame_id,
                    report->cache_frame_index,
                    report->cache_input_value,
                    report->current_input_value,
                    static_cast<unsigned>(report->cache_filled));
                ReplayTraceFields f;
                f.string("phase", sc6_exact_seek_phase_name(failed_phase))
                 .string("failure", native_seek_failure_name(failure))
                 .integer("requested_seq", m_sc6_seek_job.requested_seq)
                 .string("label", m_sc6_seek_job.label
                             ? m_sc6_seek_job.label : "?")
                 .integer("target_round", m_sc6_seek_job.target_round)
                 .integer("target_master", m_sc6_seek_job.target_master)
                 .string("reason", report->reason ? report->reason : "?")
                 .integer("live_round", report->live_round)
                 .integer("replay_player_round",
                          report->replay_player_round)
                 .integer("input_master", report->input_master)
                 .integer("battle_master", report->battle_master)
                 .integer("battle_master_delta",
                          report->battle_master_delta)
                 .integer("active_count", report->active_count)
                 .hex("active_mask", report->active_mask)
                 .boolean("round_master_ok", report->round_master_ok)
                 .string("input_authority",
                         replay_input_authority_name(
                             report->input_authority))
                 .boolean("input_authority_ok",
                          report->input_authority_ok)
                 .string("input_authority_reason",
                         report->input_authority_reason
                             ? report->input_authority_reason : "?")
                 .integer("input_authority_checked_slots",
                          report->input_authority_checked_slots)
                 .integer("input_authority_slot",
                          report->input_authority_slot)
                 .hex("expected_current_input",
                      report->expected_current_input)
                 .hex("live_current_input", report->live_current_input)
                 .hex("frame_input_value", report->frame_input_value)
                 .hex("expected_latest_engine_input",
                      report->expected_latest_engine_input)
                 .hex("live_latest_engine_input",
                      report->live_latest_engine_input)
                 .boolean("current_input_ok", report->current_input_ok)
                 .boolean("frame_input_ok", report->frame_input_ok)
                 .boolean("frame_input_diagnostic_only",
                          report->frame_input_diagnostic_only)
                 .boolean("latest_engine_input_ok",
                          report->latest_engine_input_ok)
                 .boolean("chara_replay_ring_ok",
                          report->chara_replay_ring_ok)
                 .boolean("simulation_cache_ok",
                          report->simulation_cache_ok)
                 .boolean("simulation_cache_diagnostic_only",
                          report->simulation_cache_diagnostic_only)
                 .boolean("cache_checked", report->cache_checked)
                 .integer("checked_slots", report->checked_slots)
                 .integer("cache_slot", report->cache_slot)
                 .integer("cache_frame_id", report->cache_frame_id)
                 .integer("cache_expected_frame_id",
                          report->cache_expected_frame_id)
                 .integer("cache_frame_index", report->cache_frame_index)
                 .hex("cache_input_value", report->cache_input_value)
                 .hex("current_input_value",
                      report->current_input_value)
                 .integer("cache_filled", report->cache_filled);
                ReplayDebugTrace::instance().event("sc6_failed", f);
                if (m_sc6_seek_job.authority
                    == Sc6SeekAuthority::CapturedSnapshotValidated)
                    ReplayDebugTrace::instance().event(
                        "captured_seek_failed", f);
            }
            else
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] failed seq={} label={} "
                    "phase={} reason={} target_round={} "
                    "target_master={} frames_advanced={}\n"),
                    m_sc6_seek_job.requested_seq,
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    static_cast<int>(failed_phase),
                    RC::to_generic_string(
                        native_seek_failure_name(failure)),
                    m_sc6_seek_job.target_round,
                    m_sc6_seek_job.target_master,
                    m_sc6_seek_job.frames_advanced);
                trace_seek_job_event("sc6_failed");
                if (m_sc6_seek_job.authority
                    == Sc6SeekAuthority::CapturedSnapshotValidated)
                    trace_seek_job_event("captured_seek_failed");
            }
        }

        void block_sc6_exact_seek_after_clock_landed(
            NativeSeekFailure failure,
            const CapturedFrameOracleCompareReport* oracle_report) noexcept
        {
            if (failure == NativeSeekFailure::None)
                failure = NativeSeekFailure::SemanticMismatch;

            const Sc6ExactSeekPhase blocked_phase = m_sc6_seek_job.phase;
            m_sc6_seek_job.failure = failure;
            m_sc6_seek_job.phase = Sc6ExactSeekPhase::ClockLandedPlayBlocked;
            m_ui_wants_play.store(false, std::memory_order_release);
            m_paused.store(true, std::memory_order_release);
            m_sc6_native_step_request.store(
                0, std::memory_order_release);
            m_hold_kind.store(
                static_cast<int32_t>(ReplayScrubHoldKind::RestoredFrameHold),
                std::memory_order_release);

            m_last_seek_target.store(m_sc6_seek_job.requested_seq,
                                     std::memory_order_release);
            m_last_seek_master_tag.store(m_sc6_seek_job.target_master,
                                         std::memory_order_release);
            m_live_master_cached.store(m_sc6_seek_job.target_master,
                                       std::memory_order_release);
            m_native.adjusted_seq = m_sc6_seek_job.requested_seq;
            m_native.round = m_sc6_seek_job.target_round;
            m_native.master = m_sc6_seek_job.target_master;
            m_native.target_ms = -1;

            publish_native_status(NativeSeekStatus::ClockLanded, failure);
            publish_mode(ScrubMode::PausedPreview);

            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub.sc6seek] clock landed but Play blocked "
                "label={} seq={} compare_seq={} phase={} reason={} "
                "field={} player={} expected=0x{:X} live=0x{:X} "
                "expected_f={:.6f} live_f={:.6f}\n"),
                RC::to_generic_string(m_sc6_seek_job.label
                    ? m_sc6_seek_job.label : "?"),
                m_sc6_seek_job.requested_seq,
                oracle_report ? oracle_report->expected_seq
                              : m_sc6_seek_job.validation_compare_seq,
                RC::to_generic_string(sc6_exact_seek_phase_name(
                    blocked_phase)),
                RC::to_generic_string(native_seek_failure_name(failure)),
                RC::to_generic_string(
                    oracle_report && oracle_report->field
                        ? oracle_report->field : "?"),
                oracle_report ? oracle_report->player : -1,
                oracle_report ? oracle_report->expected_u64 : 0,
                oracle_report ? oracle_report->live_u64 : 0,
                oracle_report ? oracle_report->expected_float : 0.0f,
                oracle_report ? oracle_report->live_float : 0.0f);

            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .integer("requested_seq", m_sc6_seek_job.requested_seq)
             .integer("target_round", m_sc6_seek_job.target_round)
             .integer("target_master", m_sc6_seek_job.target_master)
             .integer("compare_seq",
                      oracle_report ? oracle_report->expected_seq
                                    : m_sc6_seek_job.validation_compare_seq)
             .integer("compare_master",
                      oracle_report ? oracle_report->expected_master
                                    : m_sc6_seek_job.validation_compare_master)
             .string("phase", sc6_exact_seek_phase_name(blocked_phase))
             .string("status", "ClockLanded")
             .string("failure", native_seek_failure_name(failure))
             .string("field", oracle_report && oracle_report->field
                                 ? oracle_report->field : "?")
             .integer("player", oracle_report ? oracle_report->player : -1)
             .hex("expected", oracle_report
                                ? oracle_report->expected_u64 : 0)
             .hex("live", oracle_report ? oracle_report->live_u64 : 0)
             .real("expected_f", oracle_report
                                    ? oracle_report->expected_float : 0.0)
             .real("live_f", oracle_report
                                ? oracle_report->live_float : 0.0)
             .string("reason", oracle_report && oracle_report->reason
                                 ? oracle_report->reason : "?");
            ReplayDebugTrace::instance().event(
                "captured_seek_play_blocked", f);
        }

        void schedule_target_restore_after_validation(
            const char* reason,
            const CapturedFrameOracleCompareReport* oracle_report = nullptr,
            NativeSeekFailure block_after_restore =
                NativeSeekFailure::None)
            noexcept
        {
            const int32_t previous_compare_seq =
                m_sc6_seek_job.validation_compare_seq;
            const int32_t previous_compare_master =
                m_sc6_seek_job.validation_compare_master;

            m_sc6_seek_job.validation_compare_tick =
                m_sc6_seek_job.target_tick;
            m_sc6_seek_job.validation_compare_seq =
                m_sc6_seek_job.target_seq;
            m_sc6_seek_job.validation_compare_round =
                m_sc6_seek_job.target_round;
            m_sc6_seek_job.validation_compare_master =
                m_sc6_seek_job.target_master;
            m_sc6_seek_job.restore_target_block_failure =
                block_after_restore;
            m_sc6_seek_job.phase =
                Sc6ExactSeekPhase::RestoreTargetAfterValidation;

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.sc6seek] scheduling exact target restore "
                "label={} seq={} mode={} reason={} previous_compare_seq={} "
                "previous_compare_master={} target_master={}\n"),
                RC::to_generic_string(m_sc6_seek_job.label
                    ? m_sc6_seek_job.label : "?"),
                m_sc6_seek_job.requested_seq,
                RC::to_generic_string(captured_seek_validation_mode_name(
                    m_sc6_seek_job.validation_mode)),
                RC::to_generic_string(reason ? reason : "?"),
                previous_compare_seq, previous_compare_master,
                m_sc6_seek_job.target_master);

            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .integer("requested_seq", m_sc6_seek_job.requested_seq)
             .integer("target_seq", m_sc6_seek_job.target_seq)
             .integer("target_round", m_sc6_seek_job.target_round)
             .integer("target_master", m_sc6_seek_job.target_master)
             .integer("previous_compare_seq", previous_compare_seq)
             .integer("previous_compare_master", previous_compare_master)
             .string("validation_mode",
                     captured_seek_validation_mode_name(
                         m_sc6_seek_job.validation_mode))
              .string("reason", reason ? reason : "?")
              .string("block_after_restore",
                      native_seek_failure_name(block_after_restore))
              .string("field", oracle_report && oracle_report->field
                                  ? oracle_report->field : "none")
             .integer("player", oracle_report ? oracle_report->player : -1)
             .hex("expected", oracle_report
                                ? oracle_report->expected_u64 : 0)
             .hex("live", oracle_report ? oracle_report->live_u64 : 0)
             .real("expected_f", oracle_report
                                    ? oracle_report->expected_float : 0.0)
             .real("live_f", oracle_report
                                ? oracle_report->live_float : 0.0);
            ReplayDebugTrace::instance().event(
                "captured_seek_target_restore_scheduled", f);
        }

        void fail_sc6_exact_seek_reset(
            NativeSeekFailure failure,
            const Sc6ResetApplyReport& report) noexcept
        {
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub.sc6seek] failed label={} seq={} "
                "phase=ResetRound reason={} bm=0x{:X} inputLog=0x{:X} "
                "rp=0x{:X} stateReset=0x{:X} resetDst=0x{:X} "
                "target_round={} target_master={} origin_tick={} "
                "origin_seq={} origin_master={} origin_last_frame_id={} "
                "reset_source={} context_ok={} reset_source_ok={} "
                "snapshot_read={} snapshot_write={} il_restore={} "
                "rdb_restore={} set_move_state={} cursor_write={} "
                "rp_cursor_write={}\n"),
                RC::to_generic_string(m_sc6_seek_job.label
                    ? m_sc6_seek_job.label : "?"),
                m_sc6_seek_job.requested_seq,
                RC::to_generic_string(native_seek_failure_name(failure)),
                report.battle_manager, report.input_log,
                report.replay_player, report.state_reset_data,
                report.reset_dst, report.target_round,
                report.target_master, report.origin_tick,
                report.origin_seq, report.origin_master,
                report.origin_last_frame_id,
                RC::to_generic_string(report.reset_source
                    ? report.reset_source : "none"),
                report.context_ok ? 1 : 0,
                report.reset_source_ok ? 1 : 0,
                report.reset_snapshot_read_ok ? 1 : 0,
                report.reset_snapshot_write_ok ? 1 : 0,
                report.input_log_restore_ok ? 1 : 0,
                report.rdb_restore_ok ? 1 : 0,
                report.set_move_state_ok ? 1 : 0,
                report.replay_cursor_write_ok ? 1 : 0,
                report.replay_player_cursor_write_ok ? 1 : 0);
            ReplayTraceFields f;
            f.string("phase", "ResetRound")
             .string("failure", native_seek_failure_name(failure))
             .string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .integer("requested_seq", m_sc6_seek_job.requested_seq)
             .hex("bm", report.battle_manager)
             .hex("input_log", report.input_log)
             .hex("replay_player", report.replay_player)
             .hex("state_reset_data", report.state_reset_data)
             .hex("reset_dst", report.reset_dst)
             .integer("target_round", report.target_round)
             .integer("target_master", report.target_master)
             .integer("origin_tick", report.origin_tick)
             .integer("origin_seq", report.origin_seq)
             .integer("origin_master", report.origin_master)
             .integer("origin_last_frame_id",
                      report.origin_last_frame_id)
             .string("reset_source", report.reset_source
                         ? report.reset_source : "none")
             .boolean("context_ok", report.context_ok)
             .boolean("reset_source_ok", report.reset_source_ok)
             .boolean("reset_snapshot_read",
                      report.reset_snapshot_read_ok)
             .boolean("reset_snapshot_write",
                      report.reset_snapshot_write_ok)
             .boolean("il_restore", report.input_log_restore_ok)
             .boolean("rdb_restore", report.rdb_restore_ok)
             .boolean("set_move_state", report.set_move_state_ok)
             .boolean("cursor_write", report.replay_cursor_write_ok)
             .boolean("rp_cursor_write",
                      report.replay_player_cursor_write_ok);
            ReplayDebugTrace::instance().event("sc6_failed", f);
            fail_sc6_exact_seek(failure);
        }

        bool apply_cross_round_reset_context(
            const Sc6ReplaySeekContext& ctx,
            Sc6ResetApplyReport& reset_report) noexcept
        {
            reset_report = Sc6ResetApplyReport{};
            reset_report.battle_manager = ctx.battle_manager;
            reset_report.input_log = ctx.input_log;
            reset_report.replay_player = ctx.replay_player;
            reset_report.state_reset_data = ctx.state_reset_data;
            reset_report.target_round = m_sc6_seek_job.target_round;
            reset_report.target_master = m_sc6_seek_job.target_master;
            reset_report.reset_dst =
                ctx.battle_manager + kBM_pReplayCharaSnapshot_Off;
            reset_report.context_ok =
                ctx.battle_manager_ok && ctx.input_log_ok
                && ctx.interactive_replay_ok;

            if (!reset_report.context_ok)
            {
                reset_report.failure =
                    NativeSeekFailure::CrossRoundResetContextUnavailable;
                return false;
            }

            const int32_t origin_tick =
                find_first_slot_for_round(m_sc6_seek_job.target_round);
            if (origin_tick < 0)
            {
                reset_report.failure =
                    NativeSeekFailure::CrossRoundResetContextUnavailable;
                return false;
            }

            int32_t origin_seq = -1, origin_round = -1;
            int32_t origin_wall = -1, origin_master = -1;
            if (!m_tags.get(static_cast<size_t>(origin_tick),
                            origin_seq, origin_round, origin_wall,
                            origin_master)
                || origin_round != m_sc6_seek_job.target_round
                || origin_master < 0)
            {
                reset_report.failure =
                    NativeSeekFailure::CrossRoundResetContextUnavailable;
                return false;
            }

            reset_report.origin_tick = origin_tick;
            reset_report.origin_seq = origin_seq;
            reset_report.origin_master = origin_master;

            std::array<uint8_t, kRoundStartDataBytes> round_start{};
            bool reset_read = false;
            const char* reset_source = "none";
            if (ctx.state_reset_data_ok
                && m_sc6_seek_job.target_round >= 0
                && (ctx.total_rounds <= 0
                    || m_sc6_seek_job.target_round < ctx.total_rounds))
            {
                const uintptr_t reset_src =
                    ctx.state_reset_data
                    + static_cast<uintptr_t>(m_sc6_seek_job.target_round)
                        * kRoundStartDataBytes;
                reset_read = SafeReadBytes(
                    reinterpret_cast<const void*>(reset_src),
                    round_start.data(), round_start.size());
                if (reset_read) reset_source = "StateResetData";
            }
            if (!reset_read
                && has_captured_round_reset_snapshot(
                    m_sc6_seek_job.target_round))
            {
                round_start =
                    m_sc6_round_reset_snapshots[
                        static_cast<size_t>(m_sc6_seek_job.target_round)];
                reset_read = true;
                reset_source = "CapturedBM1360";
            }
            reset_report.reset_source = reset_source;
            reset_report.reset_snapshot_read_ok = reset_read;
            if (!reset_read)
            {
                reset_report.failure =
                    NativeSeekFailure::RoundResetDataUnavailable;
                return false;
            }
            reset_report.reset_source_ok = true;

            if (!SafeWriteBytes(
                    reinterpret_cast<void*>(reset_report.reset_dst),
                    round_start.data(), round_start.size()))
            {
                reset_report.failure =
                    NativeSeekFailure::Sc6ResetSnapshotWriteFailed;
                return false;
            }
            reset_report.reset_snapshot_write_ok = true;

            const uint8_t* il_blob =
                m_il_store.gather(static_cast<size_t>(origin_tick));
            const uint8_t* rdb_blob =
                m_rdb_store.gather(static_cast<size_t>(origin_tick));
            const uint8_t* extras_blob =
                m_extras_store.gather(static_cast<size_t>(origin_tick));
            reset_report.input_log_restore_ok =
                restore_input_cache(il_blob);
            reset_report.rdb_restore_ok =
                restore_replay_data_block(rdb_blob);
            if (!reset_report.input_log_restore_ok)
            {
                reset_report.failure =
                    NativeSeekFailure::Sc6InputLogRestoreFailed;
                return false;
            }
            if (!reset_report.rdb_restore_ok)
            {
                reset_report.failure =
                    NativeSeekFailure::Sc6ReplayDataBlockRestoreFailed;
                return false;
            }

            int32_t origin_last_frame_id = -1;
            if (il_blob)
                std::memcpy(&origin_last_frame_id,
                            il_blob + (kIL_nLastFrameID_Off
                                       - kIL_CaptureStart_Off),
                            sizeof(origin_last_frame_id));
            int32_t origin_frame_advance = -1;
            if (extras_blob)
                std::memcpy(&origin_frame_advance,
                            extras_blob + kExtras_Off_BM_FrameAdvance,
                            sizeof(origin_frame_advance));
            reset_report.origin_last_frame_id = origin_last_frame_id;

            if (!safe_call_battle_manager_set_move_state(
                    ctx.battle_manager, 4))
            {
                reset_report.failure =
                    NativeSeekFailure::CrossRoundResetDispatchFailed;
                return false;
            }
            reset_report.set_move_state_ok = true;

            reset_report.replay_cursor_write_ok =
                write_replay_cursors(
                    origin_master, origin_last_frame_id,
                    ctx.battle_manager, origin_frame_advance);
            if (!reset_report.replay_cursor_write_ok)
            {
                reset_report.failure =
                    NativeSeekFailure::Sc6ReplayCursorWriteFailed;
                return false;
            }

            reset_report.replay_player_cursor_write_ok =
                write_replay_player_cursor(
                    m_sc6_seek_job.target_round, origin_master,
                    static_cast<float>(origin_master) / 60.0f,
                    true, ctx.replay_player);
            if (!reset_report.replay_player_cursor_write_ok)
            {
                reset_report.failure =
                    NativeSeekFailure::Sc6ReplayPlayerCursorWriteFailed;
                return false;
            }

            reset_round_result_cinematic_ring_after_seek(
                m_sc6_seek_job.label, origin_seq,
                m_sc6_seek_job.target_round, origin_master);

            ReplayTraceFields f;
            f.string("label", m_sc6_seek_job.label
                         ? m_sc6_seek_job.label : "?")
             .integer("requested_seq", m_sc6_seek_job.requested_seq)
             .integer("target_round", m_sc6_seek_job.target_round)
             .integer("target_master", m_sc6_seek_job.target_master)
             .integer("origin_tick", origin_tick)
             .integer("origin_seq", origin_seq)
             .integer("origin_master", origin_master)
             .hex("bm", ctx.battle_manager)
             .hex("input_log", ctx.input_log)
             .hex("replay_player", ctx.replay_player)
             .hex("state_reset_data", ctx.state_reset_data)
             .string("reset_source", reset_source)
             .boolean("reset_snapshot_write", true)
             .boolean("il_restore", reset_report.input_log_restore_ok)
             .boolean("rdb_restore", reset_report.rdb_restore_ok)
             .boolean("set_move_state", reset_report.set_move_state_ok)
             .boolean("cursor_write",
                      reset_report.replay_cursor_write_ok)
             .boolean("rp_cursor_write",
                      reset_report.replay_player_cursor_write_ok)
             .boolean("ok", true);
            ReplayDebugTrace::instance().event(
                "cross_round_reset_context", f);
            return true;
        }

        void service_sc6_exact_seek_job() noexcept
        {
            if (m_sc6_seek_job.phase == Sc6ExactSeekPhase::Idle
                || m_sc6_seek_job.phase == Sc6ExactSeekPhase::Landed
                || m_sc6_seek_job.phase
                    == Sc6ExactSeekPhase::ClockLandedPlayBlocked
                || m_sc6_seek_job.phase == Sc6ExactSeekPhase::Failed
                || m_sc6_seek_job.phase == Sc6ExactSeekPhase::Cancelled)
                return;

            if (m_sc6_seek_job.generation != m_seek_generation)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] stale job failed label={} "
                    "seq={} job_generation={} current_generation={}\n"),
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    m_sc6_seek_job.requested_seq,
                    m_sc6_seek_job.generation, m_seek_generation);
                trace_seek_job_event("sc6_failed", "StaleGeneration");
                fail_sc6_exact_seek(NativeSeekFailure::InvalidTarget);
                return;
            }

            if (!m_sc6_seek_job.service_logged)
            {
                m_sc6_seek_job.service_logged = true;
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.sc6seek] service label={} seq={} "
                    "phase={} generation={}\n"),
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    m_sc6_seek_job.requested_seq,
                    static_cast<int>(m_sc6_seek_job.phase),
                    m_sc6_seek_job.generation);
                trace_seek_job_event("sc6_seek_service");
            }

            Sc6ReplaySeekContext ctx{};
            if (!resolve_sc6_replay_seek_context(ctx))
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] context unresolved label={} "
                    "seq={} phase={} wmp=0x{:X} bm=0x{:X} sub=0x{:X} "
                    "inputLog=0x{:X} replayPlayer=0x{:X} "
                    "stateReset=0x{:X} irs=0x{:X} totalRounds={} "
                    "rpRound={} inputMaster={} bmMaster={}\n"),
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    m_sc6_seek_job.requested_seq,
                    static_cast<int>(m_sc6_seek_job.phase),
                    ctx.world_mode_pump, ctx.battle_manager,
                    ctx.sub_driver, ctx.input_log, ctx.replay_player,
                    ctx.state_reset_data, ctx.interactive_replay,
                    ctx.total_rounds, ctx.current_round,
                    ctx.input_master, ctx.battle_master);
                log_sc6_context_report(
                    m_sc6_seek_job.label ? m_sc6_seek_job.label : "?",
                    ctx, false);
                trace_sc6_context(
                    "sc6_context", ctx,
                    m_sc6_seek_job.label ? m_sc6_seek_job.label : "?");
                fail_sc6_exact_seek(
                    NativeSeekFailure::InteractiveReplayContextUnresolved);
                return;
            }
            trace_sc6_context(
                "sc6_context", ctx,
                m_sc6_seek_job.label ? m_sc6_seek_job.label : "?");

            if (m_sc6_seek_job.phase
                == Sc6ExactSeekPhase::RestoreValidationOrigin)
            {
                publish_native_status(NativeSeekStatus::Settling);
                publish_mode(ScrubMode::NativeSeekSettling);
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .integer("target_seq", m_sc6_seek_job.target_seq)
                     .integer("target_round",
                              m_sc6_seek_job.target_round)
                     .integer("target_master",
                              m_sc6_seek_job.target_master)
                     .integer("origin_seq",
                              m_sc6_seek_job.validation_origin_seq)
                     .integer("origin_master",
                              m_sc6_seek_job.validation_origin_master)
                     .integer("compare_seq",
                              m_sc6_seek_job.validation_compare_seq)
                     .integer("compare_master",
                              m_sc6_seek_job.validation_compare_master)
                     .string("validation_mode",
                             captured_seek_validation_mode_name(
                                 m_sc6_seek_job.validation_mode));
                    ReplayDebugTrace::instance().event(
                        "captured_seek_origin_selected", f);
                }

                if (m_sc6_seek_job.needs_cross_round_reset
                    && !m_sc6_seek_job.cross_round_reset_applied)
                {
                    trace_sc6_replay_state_checkpoint(
                        "before-cross-round-reset-context", ctx);
                    Sc6ResetApplyReport reset_report{};
                    if (!apply_cross_round_reset_context(
                            ctx, reset_report))
                    {
                        NativeSeekFailure failure =
                            reset_report.failure;
                        if (failure == NativeSeekFailure::None)
                            failure = NativeSeekFailure::
                                CrossRoundResetContextUnavailable;
                        fail_sc6_exact_seek_reset(failure, reset_report);
                        return;
                    }
                    m_sc6_seek_job.cross_round_reset_applied = true;
                    trace_sc6_replay_state_checkpoint(
                        "after-cross-round-reset-context", ctx);
                }

                CapturedFrameRestoreReport restore{};
                if (!restore_captured_frame_for_seek(
                        m_sc6_seek_job.validation_origin_tick,
                        m_sc6_seek_job.label, restore))
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] captured restore failed "
                        "label={} seq={} origin_tick={} origin_seq={} "
                        "origin_master={} reason={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        m_sc6_seek_job.validation_origin_tick,
                        m_sc6_seek_job.validation_origin_seq,
                        m_sc6_seek_job.validation_origin_master,
                        RC::to_generic_string(native_seek_failure_name(
                            restore.failure)));
                    fail_sc6_exact_seek(
                        restore.failure == NativeSeekFailure::None
                            ? NativeSeekFailure::
                                CapturedSnapshotRestoreFailed
                            : restore.failure);
                    return;
                }
                if (m_sc6_seek_job.needs_cross_round_reset)
                    trace_sc6_replay_state_checkpoint(
                        "after-cross-round-origin-restore", ctx);

                m_sc6_seek_job.native_step_requested_master =
                    restore.master;
                m_sc6_seek_job.native_step_last_observed_master =
                    restore.master;
                m_sc6_seek_job.native_step_requested_credits = 0;
                m_sc6_seek_job.native_step_granted_credits =
                    m_sc6_native_step_granted.load(
                        std::memory_order_acquire);
                m_sc6_seek_job.native_step_observed_credits =
                    m_sc6_seek_job.native_step_granted_credits;
                m_sc6_seek_job.native_step_stall_count = 0;
                m_sc6_seek_job.native_step_wait_services = 0;
                m_sc6_seek_job.native_step_waiting = false;

                if (m_sc6_seek_job.validation_mode
                    == CapturedSeekValidationMode::StaticTarget)
                {
                    m_sc6_seek_job.phase =
                        Sc6ExactSeekPhase::CompareTargetSnapshot;
                }
                else
                {
                    m_sc6_seek_job.phase =
                        Sc6ExactSeekPhase::ValidateStepToTarget;
                }
            }

            if (m_sc6_seek_job.phase
                == Sc6ExactSeekPhase::ValidateStepToTarget)
            {
                const SeekCommandKind pending_kind =
                    static_cast<SeekCommandKind>(
                        m_seek_command_kind.load(
                            std::memory_order_acquire));
                if (pending_kind != SeekCommandKind::None)
                    return;

                if (m_sc6_seek_job.native_step_waiting)
                {
                    const int32_t granted_total =
                        m_sc6_native_step_granted.load(
                            std::memory_order_acquire);
                    if (granted_total
                        <= m_sc6_seek_job.native_step_observed_credits)
                        return;
                    if (m_sc6_seek_job.native_step_wait_services <= 0)
                    {
                        m_sc6_seek_job.native_step_wait_services = 1;
                        return;
                    }

                    const int32_t before =
                        m_sc6_seek_job.native_step_requested_master;
                    const int32_t live_master =
                        read_engine_master_clock();
                    const int32_t live_round = read_current_round();
                    const int32_t observed_credits =
                        granted_total
                        - m_sc6_seek_job.native_step_observed_credits;
                    m_sc6_seek_job.native_step_observed_credits =
                        granted_total;
                    m_sc6_seek_job.native_step_granted_credits =
                        granted_total;
                    m_sc6_seek_job.frames_advanced +=
                        observed_credits > 0 ? observed_credits : 1;
                    m_sc6_seek_job.native_step_last_observed_master =
                        live_master;
                    m_sc6_seek_job.native_step_waiting = false;
                    m_sc6_seek_job.native_step_wait_services = 0;

                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[ReplayScrub.sc6seek] captured validation step "
                        "observed label={} master {} -> {} expected={} "
                        "round={} target_round={} credits={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        before, live_master,
                        m_sc6_seek_job.validation_compare_master,
                        live_round, m_sc6_seek_job.validation_compare_round,
                        observed_credits);
                    {
                        ReplayTraceFields f;
                        f.string("label", m_sc6_seek_job.label
                                     ? m_sc6_seek_job.label : "?")
                         .integer("requested_seq",
                                  m_sc6_seek_job.requested_seq)
                         .integer("target_round",
                                  m_sc6_seek_job.target_round)
                         .integer("target_master",
                                  m_sc6_seek_job.target_master)
                         .integer("compare_seq",
                                  m_sc6_seek_job.validation_compare_seq)
                         .integer("compare_master",
                                  m_sc6_seek_job.validation_compare_master)
                         .integer("master_before", before)
                         .integer("master_after", live_master)
                         .integer("live_round", live_round)
                         .integer("credits", observed_credits)
                         .integer("stall_count",
                                  m_sc6_seek_job.native_step_stall_count);
                        ReplayDebugTrace::instance().event(
                            "captured_seek_validation_step_observed", f);
                    }

                    if (live_round >= 0
                        && live_round
                            != m_sc6_seek_job.validation_compare_round)
                    {
                        if (m_sc6_seek_job.validation_compare_tick
                            != m_sc6_seek_job.target_tick)
                        {
                            schedule_target_restore_after_validation(
                                "validation-step-round-mismatch",
                                nullptr,
                                NativeSeekFailure::CapturedGameplayStepFailed);
                            return;
                        }
                        fail_sc6_exact_seek(
                            NativeSeekFailure::CapturedGameplayStepFailed);
                        return;
                    }
                    if (live_master
                        != m_sc6_seek_job.validation_compare_master)
                    {
                        if (m_sc6_seek_job.validation_compare_tick
                            != m_sc6_seek_job.target_tick)
                        {
                            schedule_target_restore_after_validation(
                                "validation-step-master-mismatch",
                                nullptr,
                                NativeSeekFailure::CapturedGameplayStepFailed);
                            return;
                        }
                        fail_sc6_exact_seek(
                            NativeSeekFailure::CapturedGameplayStepFailed);
                        return;
                    }
                    m_sc6_seek_job.phase =
                        Sc6ExactSeekPhase::CompareTargetSnapshot;
                }
                else
                {
                    const int32_t live_master = read_engine_master_clock();
                    uint8_t bm_main_state = 0;
                    uint8_t bm_status = 0;
                    if (!read_battle_manager_main_state(bm_main_state)
                        || bm_main_state != kBM_MainStateActiveBattle
                        || !read_battle_manager_status(bm_status)
                        || bm_status != kBM_StatusActiveBattle)
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub.sc6seek] native validation step "
                            "skipped label={} seq={} master={} "
                            "bm.main=0x{:X} bm.status=0x{:X}; "
                            "blocking exact Play\n"),
                            RC::to_generic_string(m_sc6_seek_job.label
                                ? m_sc6_seek_job.label : "?"),
                            m_sc6_seek_job.requested_seq,
                            live_master,
                            static_cast<unsigned>(bm_main_state),
                            static_cast<unsigned>(bm_status));

                        ReplayTraceFields f;
                        f.string("label", m_sc6_seek_job.label
                                     ? m_sc6_seek_job.label : "?")
                         .integer("requested_seq",
                                  m_sc6_seek_job.requested_seq)
                         .integer("target_round",
                                  m_sc6_seek_job.target_round)
                         .integer("target_master",
                                  m_sc6_seek_job.target_master)
                         .integer("compare_seq",
                                  m_sc6_seek_job.validation_compare_seq)
                         .integer("compare_master",
                                  m_sc6_seek_job.validation_compare_master)
                          .integer("master_before", live_master)
                          .uinteger("bm_main_state", bm_main_state)
                          .uinteger("bm_status", bm_status)
                          .uinteger("required_main_state",
                                    kBM_MainStateActiveBattle)
                          .uinteger("required_status",
                                    kBM_StatusActiveBattle)
                         .string("reason", "battle-manager-not-active");
                        ReplayDebugTrace::instance().event(
                            "captured_seek_validation_step_skipped", f);
                        fail_sc6_exact_seek(
                            NativeSeekFailure::BattleManagerStatusNotActive);
                        return;
                    }
                    m_sc6_seek_job.native_step_requested_master =
                        live_master;
                    m_sc6_seek_job.native_step_requested_credits =
                        kSc6NativeStepCreditsPerRequest;
                    m_sc6_seek_job.native_step_granted_credits =
                        m_sc6_native_step_granted.load(
                            std::memory_order_acquire);
                    m_sc6_seek_job.native_step_wait_services = 0;
                    m_sc6_seek_job.native_step_waiting = true;
                    m_sc6_native_step_request.store(
                        kSc6NativeStepCreditsPerRequest,
                        std::memory_order_release);
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .integer("target_round",
                              m_sc6_seek_job.target_round)
                     .integer("target_master",
                              m_sc6_seek_job.target_master)
                     .integer("compare_seq",
                              m_sc6_seek_job.validation_compare_seq)
                     .integer("compare_master",
                              m_sc6_seek_job.validation_compare_master)
                     .integer("master_before", live_master)
                     .integer("credits",
                              kSc6NativeStepCreditsPerRequest);
                    ReplayDebugTrace::instance().event(
                        "captured_seek_validation_step_requested", f);
                    return;
                }
            }

            if (m_sc6_seek_job.phase
                == Sc6ExactSeekPhase::CompareTargetSnapshot)
            {
                LiveCapturedFrameScratch live{};
                CapturedFrameCompareReport report{};
                const bool captured =
                    capture_live_frame_for_compare(live);
                const bool compare_policy_ok = captured
                    && compare_live_frame_to_captured_tick(
                        m_sc6_seek_job.validation_compare_tick,
                        live, report);
                if (captured)
                {
                    trace_captured_snapshot_compare(report);
                    trace_captured_snapshot_mismatch_detail(report, live);
                }
                if (!captured)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] captured snapshot compare "
                        "capture failed label={} seq={} compare_seq={} "
                        "(blocking exact Play)\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        m_sc6_seek_job.validation_compare_seq);
                    if (m_sc6_seek_job.validation_compare_tick
                        != m_sc6_seek_job.target_tick)
                        schedule_target_restore_after_validation(
                            "compare-frame-capture-failed",
                            nullptr,
                            NativeSeekFailure::CapturedSnapshotCompareFailed);
                    else
                        block_sc6_exact_seek_after_clock_landed(
                            NativeSeekFailure::CapturedSnapshotCompareFailed,
                            nullptr);
                    return;
                }

                CapturedFrameOracleCompareReport oracle_report{};
                const bool oracle_ok = compare_oracle_to_captured_tick(
                    m_sc6_seek_job.validation_compare_tick,
                    oracle_report);
                trace_captured_oracle_compare(oracle_report);
                if (m_sc6_seek_job.validation_mode
                    != CapturedSeekValidationMode::StaticTarget)
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .integer("compare_seq",
                              m_sc6_seek_job.validation_compare_seq)
                     .integer("compare_master",
                              m_sc6_seek_job.validation_compare_master)
                     .string("validation_mode",
                             captured_seek_validation_mode_name(
                                 m_sc6_seek_job.validation_mode))
                     .boolean("ok", oracle_ok)
                     .string("field", oracle_report.field
                                 ? oracle_report.field : "none")
                     .string("reason", oracle_report.reason
                                 ? oracle_report.reason : "?");
                    ReplayDebugTrace::instance().event(
                        "gameplay_step_oracle", f);
                }
                const bool compare_unavailable = captured
                    && !compare_policy_ok
                    && report.first_mismatch_region < 0
                    && report.first_ignored_mismatch_region < 0;
                if (compare_unavailable)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] captured raw snapshot "
                        "compare unavailable label={} seq={} compare_seq={} "
                        "reason={} "
                        "(blocking exact Play)\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        m_sc6_seek_job.validation_compare_seq,
                        RC::to_generic_string(report.reason
                            ? report.reason : "?"));
                    if (m_sc6_seek_job.validation_compare_tick
                        != m_sc6_seek_job.target_tick)
                        schedule_target_restore_after_validation(
                            "compare-frame-raw-compare-unavailable",
                            &oracle_report,
                            oracle_ok
                                ? NativeSeekFailure::
                                      CapturedSnapshotCompareFailed
                                : NativeSeekFailure::SemanticMismatch);
                    else
                        block_sc6_exact_seek_after_clock_landed(
                            oracle_ok
                                ? NativeSeekFailure::
                                      CapturedSnapshotCompareFailed
                                : NativeSeekFailure::SemanticMismatch,
                            &oracle_report);
                    return;
                }
                if (captured && !compare_policy_ok && !compare_unavailable)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] captured raw snapshot "
                        "differs label={} seq={} compare_seq={} "
                        "region={} offset=0x{:X} expected=0x{:02X} "
                        "live=0x{:02X} reason={} ignored_mismatches={} "
                        "strict_match={} policy_match={} "
                        "(diagnostic only; oracle gates exact Play)\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        m_sc6_seek_job.validation_compare_seq,
                        report.first_mismatch_region,
                        report.first_mismatch_offset,
                        static_cast<unsigned>(report.expected_byte),
                        static_cast<unsigned>(report.live_byte),
                        RC::to_generic_string(report.reason
                            ? report.reason : "?"),
                        report.ignored_mismatch_count,
                        report.strict_match ? 1 : 0,
                        report.policy_match ? 1 : 0);
                }
                if (!oracle_ok)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] captured oracle compare "
                        "failed label={} seq={} compare_seq={} "
                        "field={} player={} expected=0x{:X} live=0x{:X} "
                        "expected_f={:.6f} live_f={:.6f} reason={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        m_sc6_seek_job.validation_compare_seq,
                        RC::to_generic_string(oracle_report.field
                            ? oracle_report.field : "?"),
                        oracle_report.player,
                        oracle_report.expected_u64,
                        oracle_report.live_u64,
                        oracle_report.expected_float,
                        oracle_report.live_float,
                        RC::to_generic_string(oracle_report.reason
                            ? oracle_report.reason : "?"));

                    // PreviousToTarget is a diagnostic native-step check:
                    // it proves whether SC6 can synthesize the target from
                    // the prior captured frame.  If that step lands the
                    // clock but not all gameplay semantics, still restore
                    // the exact target snapshot and make the final Play gate
                    // depend on the target oracle below.  TargetToNext is
                    // different: a failure there means resume-from-target is
                    // already proven unsafe, so keep it as a hard block.
                    if (m_sc6_seek_job.validation_mode
                        == CapturedSeekValidationMode::PreviousToTarget)
                    {
                        schedule_target_restore_after_validation(
                            "prev-to-target-step-semantic-mismatch",
                            &oracle_report);
                    }
                    else
                    {
                        if (m_sc6_seek_job.validation_compare_tick
                            != m_sc6_seek_job.target_tick)
                            schedule_target_restore_after_validation(
                                "compare-frame-semantic-mismatch",
                                &oracle_report,
                                NativeSeekFailure::SemanticMismatch);
                        else
                            block_sc6_exact_seek_after_clock_landed(
                                NativeSeekFailure::SemanticMismatch,
                                &oracle_report);
                        return;
                    }
                }

                if (m_sc6_seek_job.needs_cross_round_reset
                    && m_sc6_seek_job.validation_mode
                        == CapturedSeekValidationMode::StaticTarget)
                {
                    m_sc6_seek_job.snapshot_validation_ok = true;
                    trace_sc6_replay_state_checkpoint(
                        "cross-round-target-static-verify", ctx);
                    m_sc6_seek_job.phase = Sc6ExactSeekPhase::Verify;
                }

                if (m_sc6_seek_job.phase
                    == Sc6ExactSeekPhase::RestoreTargetAfterValidation)
                {
                    // Fall through into the restore block below.
                }
                else if (m_sc6_seek_job.phase
                         == Sc6ExactSeekPhase::Verify)
                {
                    // Cross-round static target already restored and passed
                    // the semantic oracle; final verification below decides
                    // whether it is playable.
                }
                else if (m_sc6_seek_job.validation_mode
                         == CapturedSeekValidationMode::PreviousToTarget)
                {
                    schedule_target_restore_after_validation(
                        "prev-to-target-step-ok");
                }
                else if (m_sc6_seek_job.validation_mode
                         == CapturedSeekValidationMode::TargetToNext
                         && m_sc6_seek_job.validation_compare_tick
                            != m_sc6_seek_job.target_tick)
                {
                    schedule_target_restore_after_validation(
                        "target-to-next-step-ok");
                }
                else
                {
                    m_sc6_seek_job.snapshot_validation_ok = true;
                    m_sc6_seek_job.phase = Sc6ExactSeekPhase::Verify;
                }
            }

            if (m_sc6_seek_job.phase
                == Sc6ExactSeekPhase::RestoreTargetAfterValidation)
            {
                CapturedFrameRestoreReport restore{};
                if (!restore_captured_frame_for_seek(
                        m_sc6_seek_job.target_tick,
                        m_sc6_seek_job.label, restore))
                {
                    fail_sc6_exact_seek(
                        restore.failure == NativeSeekFailure::None
                            ? NativeSeekFailure::
                                CapturedSnapshotRestoreFailed
                            : restore.failure);
                    return;
                }
                if (m_sc6_seek_job.needs_cross_round_reset)
                    trace_sc6_replay_state_checkpoint(
                        "after-cross-round-target-restore", ctx);

                LiveCapturedFrameScratch live{};
                CapturedFrameCompareReport report{};
                const bool captured =
                    capture_live_frame_for_compare(live);
                const bool compare_policy_ok = captured
                    && compare_live_frame_to_captured_tick(
                        m_sc6_seek_job.target_tick, live, report);
                if (captured)
                {
                    trace_captured_snapshot_compare(report);
                    trace_captured_snapshot_mismatch_detail(report, live);
                }
                if (!captured)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] target snapshot restore "
                        "capture failed label={} seq={} "
                        "(blocking exact Play)\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq);
                    block_sc6_exact_seek_after_clock_landed(
                        NativeSeekFailure::CapturedSnapshotCompareFailed,
                        nullptr);
                    return;
                }

                CapturedFrameOracleCompareReport oracle_report{};
                const bool oracle_ok = compare_oracle_to_captured_tick(
                    m_sc6_seek_job.target_tick, oracle_report);
                trace_captured_oracle_compare(oracle_report);
                const bool compare_unavailable = captured
                    && !compare_policy_ok
                    && report.first_mismatch_region < 0
                    && report.first_ignored_mismatch_region < 0;
                if (compare_unavailable)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] target raw snapshot "
                        "compare unavailable label={} seq={} reason={} "
                        "(blocking exact Play)\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        RC::to_generic_string(report.reason
                            ? report.reason : "?"));
                    block_sc6_exact_seek_after_clock_landed(
                        NativeSeekFailure::CapturedSnapshotCompareFailed,
                        &oracle_report);
                    return;
                }
                if (captured && !compare_policy_ok && !compare_unavailable)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] target raw snapshot "
                        "differs label={} seq={} region={} "
                        "offset=0x{:X} expected=0x{:02X} live=0x{:02X} "
                        "reason={} ignored_mismatches={} strict_match={} "
                        "policy_match={} "
                        "(diagnostic only; oracle gates exact Play)\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        report.first_mismatch_region,
                        report.first_mismatch_offset,
                        static_cast<unsigned>(report.expected_byte),
                        static_cast<unsigned>(report.live_byte),
                        RC::to_generic_string(report.reason
                            ? report.reason : "?"),
                        report.ignored_mismatch_count,
                        report.strict_match ? 1 : 0,
                        report.policy_match ? 1 : 0);
                }
                if (!oracle_ok)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] target oracle compare "
                        "failed label={} seq={} field={} player={} "
                        "expected=0x{:X} live=0x{:X} expected_f={:.6f} "
                        "live_f={:.6f} reason={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        RC::to_generic_string(oracle_report.field
                            ? oracle_report.field : "?"),
                        oracle_report.player,
                        oracle_report.expected_u64,
                        oracle_report.live_u64,
                        oracle_report.expected_float,
                        oracle_report.live_float,
                        RC::to_generic_string(oracle_report.reason
                            ? oracle_report.reason : "?"));
                    block_sc6_exact_seek_after_clock_landed(
                        NativeSeekFailure::SemanticMismatch,
                        &oracle_report);
                    return;
                }
                if (m_sc6_seek_job.restore_target_block_failure
                    != NativeSeekFailure::None)
                {
                    const NativeSeekFailure failure =
                        m_sc6_seek_job.restore_target_block_failure;
                    m_sc6_seek_job.restore_target_block_failure =
                        NativeSeekFailure::None;
                    block_sc6_exact_seek_after_clock_landed(
                        failure, &oracle_report);
                    return;
                }
                m_sc6_seek_job.snapshot_validation_ok = true;
                m_sc6_seek_job.phase = Sc6ExactSeekPhase::Verify;
            }

            if (m_sc6_seek_job.phase == Sc6ExactSeekPhase::Queued)
            {
                if (m_sc6_seek_job.authority
                        == Sc6SeekAuthority::NativeRoundReplayDiagnostic
                    && !kEnableLegacySeekDiagnostics)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] blocked diagnostic native "
                        "round reset path in normal build label={} seq={} "
                        "round={} master={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        m_sc6_seek_job.target_round,
                        m_sc6_seek_job.target_master);
                    fail_sc6_exact_seek(NativeSeekFailure::InvalidTarget);
                    return;
                }
                if (ctx.total_rounds > 0
                    && m_sc6_seek_job.target_round >= ctx.total_rounds)
                {
                    fail_sc6_exact_seek(NativeSeekFailure::InvalidTarget);
                    return;
                }

                const int32_t round_start_tick =
                    find_first_slot_for_round(m_sc6_seek_job.target_round);
                if (round_start_tick < 0
                    || round_start_tick > m_sc6_seek_job.target_tick)
                {
                    fail_sc6_exact_seek(NativeSeekFailure::InvalidTarget);
                    return;
                }

                int32_t start_seq = -1, start_round = -1,
                        start_wall = -1, start_master = -1;
                if (!m_tags.get(static_cast<size_t>(round_start_tick),
                                start_seq, start_round, start_wall,
                                start_master))
                {
                    fail_sc6_exact_seek(NativeSeekFailure::InvalidTarget);
                    return;
                }

                m_sc6_seek_job.round_start_tick = round_start_tick;
                m_sc6_seek_job.round_start_seq = start_seq;
                m_sc6_seek_job.round_start_master = start_master;
                m_sc6_seek_job.phase = Sc6ExactSeekPhase::ResetRound;
                publish_native_status(NativeSeekStatus::Settling);
                publish_mode(ScrubMode::NativeSeekSettling);
            }

            if (m_sc6_seek_job.phase == Sc6ExactSeekPhase::ResetRound)
            {
                const uintptr_t reset_dst =
                    ctx.battle_manager + kBM_pReplayCharaSnapshot_Off;
                Sc6ResetApplyReport reset_report{};
                reset_report.context_ok = true;
                reset_report.battle_manager = ctx.battle_manager;
                reset_report.input_log = ctx.input_log;
                reset_report.replay_player = ctx.replay_player;
                reset_report.state_reset_data = ctx.state_reset_data;
                reset_report.reset_dst = reset_dst;
                reset_report.target_round = m_sc6_seek_job.target_round;
                reset_report.target_master = m_sc6_seek_job.target_master;
                reset_report.origin_tick = m_sc6_seek_job.round_start_tick;
                reset_report.origin_seq = m_sc6_seek_job.round_start_seq;
                reset_report.origin_master =
                    m_sc6_seek_job.round_start_master;

                std::array<uint8_t, kRoundStartDataBytes> round_start{};
                const char* reset_source = "none";
                bool reset_read = false;
                int32_t origin_tick = m_sc6_seek_job.round_start_tick;
                int32_t origin_seq = m_sc6_seek_job.round_start_seq;
                int32_t origin_master = m_sc6_seek_job.round_start_master;
                if (ctx.state_reset_data_ok
                    && (ctx.total_rounds <= 0
                        || m_sc6_seek_job.target_round < ctx.total_rounds))
                {
                    const uintptr_t reset_src =
                        ctx.state_reset_data
                        + static_cast<uintptr_t>(
                            m_sc6_seek_job.target_round)
                            * kRoundStartDataBytes;
                    reset_read = SafeReadBytes(
                        reinterpret_cast<const void*>(reset_src),
                        round_start.data(), round_start.size());
                    if (reset_read)
                        reset_source = "ReplayPlayerStateResetData";
                }
                if (!reset_read
                    && has_captured_round_reset_snapshot(
                        m_sc6_seek_job.target_round))
                {
                    const size_t round_idx = static_cast<size_t>(
                        m_sc6_seek_job.target_round);
                    round_start =
                        m_sc6_round_reset_snapshots[round_idx];
                    reset_read = true;
                    reset_source = "CapturedBM1360";
                    if (m_sc6_round_reset_snapshot_seq[round_idx] >= 0
                        && m_sc6_round_reset_snapshot_seq[round_idx]
                            <= m_sc6_seek_job.target_tick)
                    {
                        origin_tick =
                            m_sc6_round_reset_snapshot_seq[round_idx];
                        origin_seq =
                            m_sc6_round_reset_snapshot_seq[round_idx];
                        origin_master =
                            m_sc6_round_reset_snapshot_master[round_idx];
                    }
                }
                reset_report.reset_source = reset_source;
                reset_report.origin_tick = origin_tick;
                reset_report.origin_seq = origin_seq;
                reset_report.origin_master = origin_master;
                reset_report.reset_snapshot_read_ok = reset_read;
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .integer("round", m_sc6_seek_job.target_round)
                     .integer("target_master",
                              m_sc6_seek_job.target_master)
                     .string("reset_source", reset_source)
                     .hex("dst", reset_dst)
                     .integer("origin_seq", origin_seq)
                     .integer("origin_master", origin_master)
                     .boolean("ok", reset_read);
                    if (reset_read)
                        f.hash("reset_hash", round_start.data(),
                               round_start.size())
                         .uinteger("reset_size", round_start.size());
                    ReplayDebugTrace::instance().event(
                        "sc6_reset_source_chosen", f);
                }
                if (!reset_read)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] reset source unavailable "
                        "label={} seq={} round={} rp=0x{:X} "
                        "stateReset=0x{:X} stateResetOk={} "
                        "capturedResetOk={} totalRounds={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        m_sc6_seek_job.requested_seq,
                        m_sc6_seek_job.target_round,
                        ctx.replay_player, ctx.state_reset_data,
                        ctx.state_reset_data_ok ? 1 : 0,
                        has_captured_round_reset_snapshot(
                            m_sc6_seek_job.target_round) ? 1 : 0,
                        ctx.total_rounds);
                    fail_sc6_exact_seek_reset(
                        NativeSeekFailure::RoundResetDataUnavailable,
                        reset_report);
                    return;
                }
                reset_report.reset_source_ok = true;
                if (!SafeWriteBytes(reinterpret_cast<void*>(reset_dst),
                                    round_start.data(),
                                    round_start.size()))
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .hex("dst", reset_dst)
                     .uinteger("bytes", round_start.size())
                     .boolean("ok", false);
                    ReplayDebugTrace::instance().event(
                        "sc6_reset_snapshot_write", f);
                    fail_sc6_exact_seek_reset(
                        NativeSeekFailure::Sc6ResetSnapshotWriteFailed,
                        reset_report);
                    return;
                }
                reset_report.reset_snapshot_write_ok = true;
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .hex("dst", reset_dst)
                     .uinteger("bytes", round_start.size())
                     .hash("reset_hash", round_start.data(),
                           round_start.size())
                     .boolean("ok", true);
                    ReplayDebugTrace::instance().event(
                        "sc6_reset_snapshot_write", f);
                }

                const uint8_t* il_blob = m_il_store.gather(
                    static_cast<size_t>(origin_tick));
                const uint8_t* rdb_blob = m_rdb_store.gather(
                    static_cast<size_t>(origin_tick));
                const bool il_ok = restore_input_cache(il_blob);
                const bool rdb_ok = restore_replay_data_block(rdb_blob);
                reset_report.input_log_restore_ok = il_ok;
                reset_report.rdb_restore_ok = rdb_ok;
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("origin_seq", origin_seq)
                     .integer("origin_master", origin_master)
                     .boolean("ok", il_ok)
                     .uinteger("bytes", kIL_CaptureBytes);
                    if (il_blob) f.hash("hash", il_blob, kIL_CaptureBytes);
                    ReplayDebugTrace::instance().event(
                        "sc6_input_log_restore", f);
                }
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("origin_seq", origin_seq)
                     .integer("origin_master", origin_master)
                     .boolean("ok", rdb_ok)
                     .uinteger("bytes", kRDB_Bytes);
                    if (rdb_blob) f.hash("hash", rdb_blob, kRDB_Bytes);
                    ReplayDebugTrace::instance().event(
                        "sc6_rdb_restore", f);
                }
                if (!il_ok)
                {
                    fail_sc6_exact_seek_reset(
                        NativeSeekFailure::Sc6InputLogRestoreFailed,
                        reset_report);
                    return;
                }
                if (!rdb_ok)
                {
                    fail_sc6_exact_seek_reset(
                        NativeSeekFailure::Sc6ReplayDataBlockRestoreFailed,
                        reset_report);
                    return;
                }

                int32_t start_last_frame_id = 0;
                if (il_blob)
                    std::memcpy(&start_last_frame_id,
                                il_blob + (kIL_nLastFrameID_Off
                                           - kIL_CaptureStart_Off),
                                sizeof(start_last_frame_id));
                reset_report.origin_last_frame_id = start_last_frame_id;

                if (!safe_call_battle_manager_set_move_state(
                        ctx.battle_manager, 4))
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .hex("bm", ctx.battle_manager)
                     .integer("move_state", 4)
                     .boolean("ok", false);
                    ReplayDebugTrace::instance().add_fault_fields(
                        f, m_last_set_move_state_fault);
                    ReplayDebugTrace::instance().event(
                        "sc6_set_move_state", f);
                    fail_sc6_exact_seek_reset(
                        NativeSeekFailure::Sc6SetMoveStateFaulted,
                        reset_report);
                    return;
                }
                reset_report.set_move_state_ok = true;
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .hex("bm", ctx.battle_manager)
                     .integer("move_state", 4)
                     .boolean("ok", true);
                    ReplayDebugTrace::instance().event(
                        "sc6_set_move_state", f);
                }

                if (!write_replay_cursors(origin_master,
                                          start_last_frame_id,
                                          ctx.battle_manager))
                {
                    fail_sc6_exact_seek_reset(
                        NativeSeekFailure::Sc6ReplayCursorWriteFailed,
                        reset_report);
                    return;
                }
                reset_report.replay_cursor_write_ok = true;

                if (!write_replay_player_cursor(
                        m_sc6_seek_job.target_round,
                        origin_master,
                        static_cast<float>(origin_master) / 60.0f,
                        true,
                        ctx.replay_player))
                {
                    fail_sc6_exact_seek_reset(
                        NativeSeekFailure::Sc6ReplayPlayerCursorWriteFailed,
                        reset_report);
                    return;
                }
                reset_report.replay_player_cursor_write_ok = true;
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("input_master", origin_master)
                     .integer("battle_master", origin_master)
                     .integer("replay_round",
                              m_sc6_seek_job.target_round)
                     .boolean("cursor_write",
                              reset_report.replay_cursor_write_ok)
                     .boolean("rp_cursor_write",
                              reset_report.replay_player_cursor_write_ok);
                    ReplayDebugTrace::instance().event(
                        "sc6_cursor_write", f);
                }

                m_sc6_seek_job.last_live_master =
                    read_engine_master_clock();
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.sc6seek] reset dispatched label={} seq={} "
                    "round={} target_master={} bm=0x{:X} inputLog=0x{:X} "
                    "irs=bm+AA120=0x{:X} round_start_seq={} "
                    "round_start_master={} reset_source={} il_restore={} "
                    "rdb_restore={}\n"),
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    m_sc6_seek_job.requested_seq,
                    m_sc6_seek_job.target_round,
                    m_sc6_seek_job.target_master, ctx.battle_manager,
                    ctx.input_log, ctx.interactive_replay,
                    origin_seq,
                    origin_master,
                    RC::to_generic_string(reset_source),
                    il_ok ? 1 : 0, rdb_ok ? 1 : 0);
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .integer("round", m_sc6_seek_job.target_round)
                     .integer("target_master",
                              m_sc6_seek_job.target_master)
                     .hex("bm", ctx.battle_manager)
                     .hex("input_log", ctx.input_log)
                     .hex("interactive_replay", ctx.interactive_replay)
                     .integer("origin_seq", origin_seq)
                     .integer("origin_master", origin_master)
                     .string("reset_source", reset_source)
                     .boolean("il_restore", il_ok)
                     .boolean("rdb_restore", rdb_ok);
                    ReplayDebugTrace::instance().event(
                        "sc6_reset_dispatched", f);
                }
                reset_round_result_cinematic_ring_after_seek(
                    m_sc6_seek_job.label, origin_seq,
                    m_sc6_seek_job.target_round, origin_master);

                const int32_t live_round_after_reset =
                    read_current_round();
                if ((live_round_after_reset >= 0
                        && live_round_after_reset
                            != m_sc6_seek_job.target_round)
                    || m_sc6_seek_job.last_live_master < 0
                    || m_sc6_seek_job.last_live_master
                        > m_sc6_seek_job.target_master)
                {
                    fail_sc6_exact_seek(
                        NativeSeekFailure::
                            InteractiveReplayRoundSelectFailed);
                    return;
                }
                m_sc6_seek_job.phase =
                    (m_sc6_seek_job.last_live_master
                     >= m_sc6_seek_job.target_master)
                        ? Sc6ExactSeekPhase::Verify
                        : Sc6ExactSeekPhase::FastForward;
                m_sc6_seek_job.native_step_requested_master =
                    m_sc6_seek_job.last_live_master;
                m_sc6_seek_job.native_step_last_observed_master =
                    m_sc6_seek_job.last_live_master;
                m_sc6_seek_job.native_step_requested_credits = 0;
                m_sc6_seek_job.native_step_granted_credits =
                    m_sc6_native_step_granted.load(
                        std::memory_order_acquire);
                m_sc6_seek_job.native_step_observed_credits =
                    m_sc6_seek_job.native_step_granted_credits;
                m_sc6_seek_job.native_step_stall_count = 0;
                m_sc6_seek_job.native_step_wait_services = 0;
                m_sc6_seek_job.native_step_waiting = false;
            }

            if (m_sc6_seek_job.phase == Sc6ExactSeekPhase::FastForward)
            {
                int32_t live_master = read_engine_master_clock();
                int32_t live_round = read_current_round();
                const SeekCommandKind pending_kind =
                    static_cast<SeekCommandKind>(
                        m_seek_command_kind.load(
                            std::memory_order_acquire));
                if (pending_kind != SeekCommandKind::None)
                    return;
                if (m_sc6_seek_job.generation != m_seek_generation)
                {
                    fail_sc6_exact_seek(NativeSeekFailure::InvalidTarget);
                    return;
                }
                if (live_round >= 0
                    && live_round != m_sc6_seek_job.target_round)
                {
                    fail_sc6_exact_seek(
                        NativeSeekFailure::
                            InteractiveReplayTargetPastMatchEnd);
                    return;
                }
                if (live_master < 0)
                {
                    fail_sc6_exact_seek(
                        NativeSeekFailure::
                            InteractiveReplayFastForwardStalled);
                    return;
                }
                if (live_master >= m_sc6_seek_job.target_master)
                {
                    m_sc6_seek_job.phase = Sc6ExactSeekPhase::Verify;
                }
                else if (m_sc6_seek_job.authority
                         == Sc6SeekAuthority::NativeRoundReplayDiagnostic)
                {
                    constexpr int32_t kDirectFramesPerService = 8;
                    const int32_t before = live_master;
                    int32_t advanced_this_service = 0;
                    int32_t remaining =
                        m_sc6_seek_job.target_master - live_master;
                    if (remaining > kDirectFramesPerService)
                        remaining = kDirectFramesPerService;

                    while (remaining > 0
                           && live_master
                                < m_sc6_seek_job.target_master)
                    {
                        if (!invoke_direct_sc6_replay_frame_once(
                                ctx.battle_manager, ctx.input_log,
                                m_sc6_seek_job.label))
                        {
                            RC::Output::send<RC::LogLevel::Warning>(STR(
                                "[ReplayScrub.sc6seek] replay-frame "
                                "fast-forward "
                                "faulted/stalled label={} master={} "
                                "target={} round={} frames_advanced={}\n"),
                                RC::to_generic_string(
                                    m_sc6_seek_job.label
                                        ? m_sc6_seek_job.label : "?"),
                                live_master,
                                m_sc6_seek_job.target_master,
                                live_round,
                                m_sc6_seek_job.frames_advanced);
                            fail_sc6_exact_seek(
                                NativeSeekFailure::
                                    InteractiveReplayFastForwardStalled);
                            return;
                        }

                        ++m_sc6_seek_job.frames_advanced;
                        ++advanced_this_service;
                        --remaining;

                        live_round = read_current_round();
                        live_master = read_engine_master_clock();
                        if (live_round >= 0
                            && live_round
                                != m_sc6_seek_job.target_round)
                        {
                            fail_sc6_exact_seek(
                                NativeSeekFailure::
                                    InteractiveReplayTargetPastMatchEnd);
                            return;
                        }
                        if (live_master < 0)
                        {
                            fail_sc6_exact_seek(
                                NativeSeekFailure::
                                    InteractiveReplayFastForwardStalled);
                            return;
                        }
                        if (m_sc6_seek_job.frames_advanced
                            > kSc6SeekMaxFrames)
                        {
                            fail_sc6_exact_seek(
                                NativeSeekFailure::
                                    InteractiveReplayFastForwardStalled);
                            return;
                        }
                    }

                    if (live_master > before)
                    {
                        m_sc6_seek_job.stall_count = 0;
                        m_sc6_seek_job.native_step_stall_count = 0;
                    }
                    else
                    {
                        ++m_sc6_seek_job.stall_count;
                        ++m_sc6_seek_job.native_step_stall_count;
                    }
                    ++m_sc6_seek_job.slices_serviced;

                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[ReplayScrub.sc6seek] replay-frame fast-forward "
                        "observed label={} master {} -> {} target={} "
                        "round={} frames_this={} frames_total={} "
                        "stalls={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        before, live_master,
                        m_sc6_seek_job.target_master, live_round,
                        advanced_this_service,
                        m_sc6_seek_job.frames_advanced,
                        m_sc6_seek_job.native_step_stall_count);
                    {
                        ReplayTraceFields f;
                        f.string("label", m_sc6_seek_job.label
                                     ? m_sc6_seek_job.label : "?")
                         .integer("requested_seq",
                                  m_sc6_seek_job.requested_seq)
                         .integer("target_round",
                                  m_sc6_seek_job.target_round)
                         .integer("target_master",
                                  m_sc6_seek_job.target_master)
                         .integer("master_before", before)
                         .integer("master_after", live_master)
                         .integer("live_round", live_round)
                         .integer("frames_this",
                                  advanced_this_service)
                         .integer("frames_total",
                                  m_sc6_seek_job.frames_advanced)
                         .integer("stall_count",
                                  m_sc6_seek_job.native_step_stall_count);
                        ReplayDebugTrace::instance().event(
                            "sc6_replay_frame_fast_forward_observed", f);
                    }

                    if (advanced_this_service > 0
                        && live_master <= before)
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub.sc6seek] replay-frame "
                            "fast-forward made no clock progress; aborting "
                            "label={} master={} target={} round={}\n"),
                            RC::to_generic_string(m_sc6_seek_job.label
                                ? m_sc6_seek_job.label : "?"),
                            live_master, m_sc6_seek_job.target_master,
                            live_round);
                        fail_sc6_exact_seek(
                            NativeSeekFailure::
                                InteractiveReplayFastForwardStalled);
                        return;
                    }

                    if (m_sc6_seek_job.native_step_stall_count
                        > kSc6NativeStepMaxStalls)
                    {
                        fail_sc6_exact_seek(
                            NativeSeekFailure::
                                InteractiveReplayFastForwardStalled);
                        return;
                    }
                    if (live_master >= m_sc6_seek_job.target_master)
                        m_sc6_seek_job.phase = Sc6ExactSeekPhase::Verify;
                    else
                        return;
                }
                else if (m_sc6_seek_job.native_step_waiting)
                {
                    const int32_t granted_total =
                        m_sc6_native_step_granted.load(
                            std::memory_order_acquire);
                    if (granted_total
                        <= m_sc6_seek_job.native_step_observed_credits)
                        return;

                    // frame_step_apply grants the credit before the
                    // native actor/replay-clock path has necessarily run
                    // for this UE frame.  Wait for one more cockpit service
                    // before judging whether the master clock advanced.
                    if (m_sc6_seek_job.native_step_wait_services <= 0)
                    {
                        m_sc6_seek_job.native_step_wait_services = 1;
                        return;
                    }

                    const int32_t before =
                        m_sc6_seek_job.native_step_requested_master;
                    const int32_t observed_credits =
                        granted_total
                        - m_sc6_seek_job.native_step_observed_credits;
                    m_sc6_seek_job.native_step_observed_credits =
                        granted_total;
                    m_sc6_seek_job.native_step_granted_credits =
                        granted_total;
                    m_sc6_seek_job.frames_advanced +=
                        observed_credits > 0 ? observed_credits : 1;
                    m_sc6_seek_job.native_step_last_observed_master =
                        live_master;
                    m_sc6_seek_job.native_step_waiting = false;
                    m_sc6_seek_job.native_step_wait_services = 0;

                    if (live_master > before)
                    {
                        m_sc6_seek_job.stall_count = 0;
                        m_sc6_seek_job.native_step_stall_count = 0;
                    }
                    else
                    {
                        ++m_sc6_seek_job.stall_count;
                        ++m_sc6_seek_job.native_step_stall_count;
                    }

                    ++m_sc6_seek_job.slices_serviced;
                    const bool default_log =
                        live_master > before
                        || m_sc6_seek_job.native_step_stall_count > 0
                        || (m_sc6_seek_job.slices_serviced % 30) == 0;
                    if (default_log)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[ReplayScrub.sc6seek] native step observed "
                            "label={} master {} -> {} target={} credits={} "
                            "frames_advanced={} stalls={}\n"),
                            RC::to_generic_string(m_sc6_seek_job.label
                                ? m_sc6_seek_job.label : "?"),
                            before, live_master,
                            m_sc6_seek_job.target_master,
                            observed_credits,
                            m_sc6_seek_job.frames_advanced,
                            m_sc6_seek_job.native_step_stall_count);
                    }
                    else
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(STR(
                            "[ReplayScrub.sc6seek] native step observed "
                            "label={} master {} -> {} target={} credits={} "
                            "frames_advanced={} stalls={}\n"),
                            RC::to_generic_string(m_sc6_seek_job.label
                                ? m_sc6_seek_job.label : "?"),
                            before, live_master,
                            m_sc6_seek_job.target_master,
                            observed_credits,
                            m_sc6_seek_job.frames_advanced,
                            m_sc6_seek_job.native_step_stall_count);
                    }
                    {
                        ReplayTraceFields f;
                        f.string("label", m_sc6_seek_job.label
                                     ? m_sc6_seek_job.label : "?")
                         .integer("requested_seq",
                                  m_sc6_seek_job.requested_seq)
                         .integer("target_round",
                                  m_sc6_seek_job.target_round)
                         .integer("target_master",
                                  m_sc6_seek_job.target_master)
                         .integer("master_before", before)
                         .integer("master_after", live_master)
                         .integer("credits", observed_credits)
                         .integer("frames_total",
                                  m_sc6_seek_job.frames_advanced)
                         .integer("stall_count",
                                  m_sc6_seek_job.native_step_stall_count);
                        ReplayDebugTrace::instance().event(
                            "sc6_native_step_observed", f);
                    }

                    if (m_sc6_seek_job.frames_advanced
                            > kSc6SeekMaxFrames
                        || m_sc6_seek_job.native_step_stall_count
                            > kSc6NativeStepMaxStalls)
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub.sc6seek] native step stalled "
                            "label={} master={} target={} stalls={}\n"),
                            RC::to_generic_string(m_sc6_seek_job.label
                                ? m_sc6_seek_job.label : "?"),
                            live_master,
                            m_sc6_seek_job.target_master,
                            m_sc6_seek_job.native_step_stall_count);
                        fail_sc6_exact_seek(
                            NativeSeekFailure::
                                InteractiveReplayFastForwardStalled);
                        return;
                    }

                    if (live_master >= m_sc6_seek_job.target_master)
                        m_sc6_seek_job.phase = Sc6ExactSeekPhase::Verify;
                    else
                        return;
                }
                else
                {
                    m_sc6_seek_job.native_step_requested_master =
                        live_master;
                    m_sc6_seek_job.native_step_requested_credits =
                        kSc6NativeStepCreditsPerRequest;
                    m_sc6_seek_job.native_step_granted_credits =
                        m_sc6_native_step_granted.load(
                            std::memory_order_acquire);
                    m_sc6_seek_job.native_step_wait_services = 0;
                    m_sc6_seek_job.native_step_waiting = true;
                    m_sc6_native_step_request.store(
                        kSc6NativeStepCreditsPerRequest,
                        std::memory_order_release);
                    RC::Output::send<RC::LogLevel::Verbose>(STR(
                        "[ReplayScrub.sc6seek] native step requested "
                        "label={} master={} target={} credits={}\n"),
                        RC::to_generic_string(m_sc6_seek_job.label
                            ? m_sc6_seek_job.label : "?"),
                        live_master,
                        m_sc6_seek_job.target_master,
                        kSc6NativeStepCreditsPerRequest);
                    {
                        ReplayTraceFields f;
                        f.string("label", m_sc6_seek_job.label
                                     ? m_sc6_seek_job.label : "?")
                         .integer("requested_seq",
                                  m_sc6_seek_job.requested_seq)
                         .integer("target_round",
                                  m_sc6_seek_job.target_round)
                         .integer("target_master",
                                  m_sc6_seek_job.target_master)
                         .integer("master_before", live_master)
                         .integer("credits",
                                  kSc6NativeStepCreditsPerRequest)
                         .integer("stall_count",
                                  m_sc6_seek_job.native_step_stall_count);
                        ReplayDebugTrace::instance().event(
                            "sc6_native_step_requested", f);
                    }
                    return;
                }
            }

            if (m_sc6_seek_job.phase == Sc6ExactSeekPhase::Verify)
            {
                if (m_sc6_seek_job.authority
                        == Sc6SeekAuthority::CapturedSnapshotValidated
                    && !m_sc6_seek_job.snapshot_validation_ok)
                {
                    fail_sc6_exact_seek(
                        NativeSeekFailure::CapturedSnapshotCompareFailed);
                    return;
                }
                Sc6SeekVerifyReport report{};
                if (!verify_sc6_exact_landing(m_sc6_seek_job, report))
                {
                    fail_sc6_exact_seek(
                        NativeSeekFailure::InteractiveReplayVerifyFailed,
                        &report);
                    return;
                }
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .integer("target_round",
                              m_sc6_seek_job.target_round)
                     .integer("target_master",
                              m_sc6_seek_job.target_master)
                     .integer("live_round", report.live_round)
                     .integer("replay_player_round",
                              report.replay_player_round)
                     .integer("input_master", report.input_master)
                      .integer("battle_master", report.battle_master)
                      .integer("battle_master_delta",
                               report.battle_master_delta)
                      .boolean("round_master_ok", report.round_master_ok)
                      .string("input_authority",
                              replay_input_authority_name(
                                  report.input_authority))
                      .boolean("input_authority_ok",
                               report.input_authority_ok)
                      .string("input_authority_reason",
                              report.input_authority_reason
                                  ? report.input_authority_reason : "?")
                      .integer("input_authority_checked_slots",
                               report.input_authority_checked_slots)
                      .integer("input_authority_slot",
                               report.input_authority_slot)
                      .hex("expected_current_input",
                           report.expected_current_input)
                      .hex("live_current_input", report.live_current_input)
                      .hex("frame_input_value", report.frame_input_value)
                      .hex("expected_latest_engine_input",
                           report.expected_latest_engine_input)
                      .hex("live_latest_engine_input",
                           report.live_latest_engine_input)
                      .boolean("current_input_ok",
                               report.current_input_ok)
                      .boolean("frame_input_ok", report.frame_input_ok)
                      .boolean("frame_input_diagnostic_only",
                               report.frame_input_diagnostic_only)
                      .boolean("latest_engine_input_ok",
                               report.latest_engine_input_ok)
                      .boolean("chara_replay_ring_ok",
                               report.chara_replay_ring_ok)
                      .boolean("simulation_cache_ok",
                               report.simulation_cache_ok)
                      .boolean("simulation_cache_diagnostic_only",
                               report.simulation_cache_diagnostic_only)
                      .boolean("cache_checked", report.cache_checked)
                      .integer("checked_slots", report.checked_slots)
                      .integer("cache_slot", report.cache_slot)
                     .integer("cache_frame_id", report.cache_frame_id)
                     .integer("cache_expected_frame_id",
                              report.cache_expected_frame_id)
                     .integer("cache_frame_index",
                              report.cache_frame_index)
                     .hex("cache_input_value", report.cache_input_value)
                     .hex("current_input_value",
                          report.current_input_value)
                     .integer("cache_filled", report.cache_filled)
                      .boolean("ok", true)
                      .string("reason", report.reason
                                  ? report.reason : "ok");
                    ReplayDebugTrace::instance().event("sc6_verify", f);
                    ReplayDebugTrace::instance().event(
                        "final_landing_verify", f);
                }

                m_last_seek_target.store(m_sc6_seek_job.requested_seq,
                                         std::memory_order_release);
                m_last_seek_master_tag.store(
                    m_sc6_seek_job.target_master,
                    std::memory_order_release);
                m_live_master_cached.store(m_sc6_seek_job.target_master,
                                           std::memory_order_release);
                m_native.adjusted_seq = m_sc6_seek_job.requested_seq;
                m_native.round = m_sc6_seek_job.target_round;
                m_native.master = m_sc6_seek_job.target_master;
                m_native.target_ms = -1;
                m_sc6_seek_job.phase = Sc6ExactSeekPhase::Landed;
                m_sc6_native_step_request.store(
                    0, std::memory_order_release);
                m_hold_kind.store(
                    static_cast<int32_t>(
                        ReplayScrubHoldKind::RestoredFrameHold),
                    std::memory_order_release);
                publish_native_status(NativeSeekStatus::Landed);
                publish_mode(ScrubMode::NativeSeekLanded);
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.sc6seek] captured seek landed label={} "
                    "seq={} round={} master={} validation={} "
                    "frames_advanced={} bm_master={} input_authority={} "
                    "input_slots={} cache_checked={} cache_ok={} "
                    "cache_diagnostic_only={} cache_slots={}\n"),
                    RC::to_generic_string(m_sc6_seek_job.label
                        ? m_sc6_seek_job.label : "?"),
                    m_sc6_seek_job.requested_seq,
                    m_sc6_seek_job.target_round,
                    m_sc6_seek_job.target_master,
                    RC::to_generic_string(
                        captured_seek_validation_mode_name(
                            m_sc6_seek_job.validation_mode)),
                    m_sc6_seek_job.frames_advanced,
                    report.battle_master,
                    RC::to_generic_string(replay_input_authority_name(
                        report.input_authority)),
                    report.input_authority_checked_slots,
                    report.cache_checked ? 1 : 0,
                    report.simulation_cache_ok ? 1 : 0,
                    report.simulation_cache_diagnostic_only ? 1 : 0,
                    report.checked_slots);
                {
                    ReplayTraceFields f;
                    f.string("label", m_sc6_seek_job.label
                                 ? m_sc6_seek_job.label : "?")
                     .integer("requested_seq",
                              m_sc6_seek_job.requested_seq)
                     .integer("target_round",
                              m_sc6_seek_job.target_round)
                     .integer("target_master",
                              m_sc6_seek_job.target_master)
                     .string("validation_mode",
                             captured_seek_validation_mode_name(
                                 m_sc6_seek_job.validation_mode))
                     .integer("frames_advanced",
                              m_sc6_seek_job.frames_advanced)
                      .integer("battle_master", report.battle_master)
                      .string("input_authority",
                              replay_input_authority_name(
                                  report.input_authority))
                      .boolean("frame_input_diagnostic_only",
                               report.frame_input_diagnostic_only)
                      .integer("input_authority_checked_slots",
                               report.input_authority_checked_slots)
                      .boolean("cache_checked", report.cache_checked)
                      .boolean("simulation_cache_ok",
                               report.simulation_cache_ok)
                      .boolean("simulation_cache_diagnostic_only",
                               report.simulation_cache_diagnostic_only)
                      .integer("checked_slots", report.checked_slots);
                    ReplayDebugTrace::instance().event(
                        "captured_seek_landed", f);
                }
                if (m_ui_wants_play.load(std::memory_order_acquire)
                    && m_auto_resume_on_release.load(
                        std::memory_order_acquire))
                {
                    (void)resume_play_if_battle_status_active(
                        m_sc6_seek_job.label ? m_sc6_seek_job.label
                                             : "AUTO_RESUME");
                }
            }
        }

        // DIAGNOSTIC LEGACY ONLY. This whole section contains the old
        // snapshot/DemoNetDriver seek machinery, including round-boundary
        // adjustment. It is compiled out in normal builds because strict
        // replay accuracy must never retarget the user's selected seq.
#if HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG
        SeekApplyStatus apply_sc6_exact_seek(size_t target_tick,
                                             int32_t seq_tag,
                                             int32_t round_tag,
                                             int32_t master_tag) noexcept
        {
            if (round_tag < 0 || master_tag < 0)
                return SeekApplyStatus::Failed;

            Sc6ReplaySeekContext ctx{};
            if (!resolve_sc6_replay_seek_context(ctx))
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InteractiveReplayContextUnresolved);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] context unresolved for "
                    "seq={} round={} master={}\n"),
                    seq_tag, round_tag, master_tag);
                return SeekApplyStatus::Failed;
            }

            if (round_tag >= ctx.total_rounds)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InvalidTarget);
                return SeekApplyStatus::Failed;
            }

            const int32_t round_start_tick =
                find_first_slot_for_round(round_tag);
            if (round_start_tick < 0
                || static_cast<size_t>(round_start_tick) > target_tick)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InvalidTarget);
                return SeekApplyStatus::Failed;
            }

            int32_t round_start_seq = -1, round_start_round = -1,
                    round_start_wall = -1, round_start_master = -1;
            if (!m_tags.get(static_cast<size_t>(round_start_tick),
                            round_start_seq, round_start_round,
                            round_start_wall, round_start_master))
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InvalidTarget);
                return SeekApplyStatus::Failed;
            }

            const uintptr_t reset_src =
                ctx.state_reset_data
                + static_cast<uintptr_t>(round_tag) * kRoundStartDataBytes;
            const uintptr_t reset_dst =
                ctx.battle_manager + kBM_pReplayCharaSnapshot_Off;
            std::array<uint8_t, kRoundStartDataBytes> round_start{};
            if (!SafeReadBytes(reinterpret_cast<const void*>(reset_src),
                               round_start.data(), round_start.size())
                || !SafeWriteBytes(reinterpret_cast<void*>(reset_dst),
                                   round_start.data(),
                                   round_start.size()))
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InteractiveReplayResetFaulted);
                return SeekApplyStatus::Failed;
            }

            const uint8_t* il_blob =
                m_il_store.gather(static_cast<size_t>(round_start_tick));
            const uint8_t* rdb_blob =
                m_rdb_store.gather(static_cast<size_t>(round_start_tick));
            const bool il_ok = restore_input_cache(il_blob);
            const bool rdb_ok = restore_replay_data_block(rdb_blob);

            int32_t start_last_frame_id = 0;
            if (il_blob)
            {
                std::memcpy(&start_last_frame_id,
                            il_blob + (kIL_nLastFrameID_Off
                                       - kIL_CaptureStart_Off),
                            sizeof(start_last_frame_id));
            }
            write_replay_cursors(round_start_master, start_last_frame_id);
            write_replay_player_cursor(
                round_tag, round_start_master,
                static_cast<float>(round_start_master) / 60.0f,
                true);

#if HORSEMOD_REPLAY_ENABLE_INTERACTIVE_RESET_DIAG
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub.sc6seek] DIAGNOSTIC InteractiveReplay_Reset "
                "path used seq={} round={} master={} irs=0x{:X}\n"),
                seq_tag, round_tag, master_tag, ctx.interactive_replay);
            if (!diagnostic_safe_call_interactive_replay_reset(
                    ctx.interactive_replay))
            {
                publish_native_failure_reason(
                    NativeSeekFailure::
                        Sc6InteractiveReplayResetFaultedDiagnostic);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] InteractiveReplay_Reset "
                    "faulted irs=0x{:X} bm=0x{:X}\n"),
                    ctx.interactive_replay, ctx.battle_manager);
                return SeekApplyStatus::Failed;
            }
#endif

            if (!safe_call_battle_manager_set_move_state(
                    ctx.battle_manager, 4))
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InteractiveReplayResetFaulted);
                return SeekApplyStatus::Failed;
            }

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.sc6seek] start seq={} round={} master={} "
                "bm=0x{:X} inputLog=0x{:X} sub=0x{:X} "
                "irs=bm+AA120=0x{:X} round_start_seq={} "
                "round_start_master={} il_restore={} rdb_restore={}\n"),
                seq_tag, round_tag, master_tag, ctx.battle_manager,
                ctx.input_log, ctx.sub_driver, ctx.interactive_replay,
                round_start_seq, round_start_master,
                il_ok ? 1 : 0, rdb_ok ? 1 : 0);

            int32_t live_round = read_current_round();
            int32_t live_master = read_engine_master_clock();
            if (live_round != round_tag || live_master < 0
                || live_master > master_tag)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InteractiveReplayRoundSelectFailed);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] round-start verify failed: "
                    "target round={} master={} live_round={} "
                    "live_master={} start_master={}\n"),
                    round_tag, master_tag, live_round, live_master,
                    round_start_master);
                return SeekApplyStatus::Failed;
            }

            constexpr int32_t kSc6SeekFramesPerSlice = 240;
            constexpr int32_t kSc6SeekStallLimit = 8;
            constexpr int32_t kSc6SeekMaxFrames = 20000;
            int32_t frames_advanced = 0;
            int32_t stall_count = 0;

            while (live_master < master_tag)
            {
                const int32_t before = live_master;
                int32_t remaining = master_tag - live_master;
                if (remaining > kSc6SeekFramesPerSlice)
                    remaining = kSc6SeekFramesPerSlice;
                for (int32_t i = 0; i < remaining; ++i)
                {
                    if (!invoke_direct_sc6_frame_once())
                    {
                        publish_native_failure_reason(
                            NativeSeekFailure::
                                InteractiveReplayFastForwardStalled);
                        return SeekApplyStatus::Failed;
                    }
                    ++frames_advanced;
                    if (frames_advanced > kSc6SeekMaxFrames)
                    {
                        publish_native_failure_reason(
                            NativeSeekFailure::
                                InteractiveReplayFastForwardStalled);
                        return SeekApplyStatus::Failed;
                    }
                }

                live_round = read_current_round();
                live_master = read_engine_master_clock();
                if (live_round != round_tag)
                {
                    publish_native_failure_reason(
                        NativeSeekFailure::
                            InteractiveReplayTargetPastMatchEnd);
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub.sc6seek] fast-forward crossed "
                        "round: target round={} master={} live_round={} "
                        "live_master={} frames_advanced={}\n"),
                        round_tag, master_tag, live_round, live_master,
                        frames_advanced);
                    return SeekApplyStatus::Failed;
                }
                if (live_master <= before)
                {
                    ++stall_count;
                    if (stall_count > kSc6SeekStallLimit)
                    {
                        publish_native_failure_reason(
                            NativeSeekFailure::
                                InteractiveReplayFastForwardStalled);
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub.sc6seek] fast-forward stalled: "
                            "master {} -> {} target={} stalls={} "
                            "frames_advanced={}\n"),
                            before, live_master, master_tag, stall_count,
                            frames_advanced);
                        return SeekApplyStatus::Failed;
                    }
                }
                else
                {
                    stall_count = 0;
                }

                RC::Output::send<RC::LogLevel::Verbose>(STR(
                    "[ReplayScrub.sc6seek] fast-forward slice master "
                    "{} -> {} target={} frames_advanced={}\n"),
                    before, live_master, master_tag, frames_advanced);
            }

            const int32_t battle_master = read_battle_manager_master_clock();
            const int32_t master_delta =
                (battle_master >= master_tag)
                    ? (battle_master - master_tag)
                    : (master_tag - battle_master);
            if (live_round != round_tag
                || live_master != master_tag
                || battle_master < 0
                || master_delta > 1)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::InteractiveReplayVerifyFailed);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub.sc6seek] verify failed seq={} "
                    "target round={} master={} live_round={} "
                    "input_master={} bm_master={} master_delta={}\n"),
                    seq_tag, round_tag, master_tag, live_round,
                    live_master, battle_master, master_delta);
                return SeekApplyStatus::Failed;
            }

            m_last_seek_target.store(seq_tag, std::memory_order_release);
            m_last_seek_master_tag.store(master_tag,
                                         std::memory_order_release);
            m_native.adjusted_seq = seq_tag;
            m_native.round = round_tag;
            m_native.master = master_tag;
            m_native.target_ms = -1;
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub.sc6seek] landed exact seq={} round={} "
                "master={} frames_advanced={} input_master={} "
                "bm_master={}\n"),
                seq_tag, round_tag, master_tag, frames_advanced,
                live_master, battle_master);
            return SeekApplyStatus::AppliedSc6Exact;
        }
#endif

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

        bool capture_input_ring_state(
            uintptr_t base,
            uint8_t* dst) noexcept
        {
            if (!base || !dst) return false;

            // Ghidra confirms g_LuxBattle_PerPlayerInputRing is the
            // contiguous ring entry storage itself.  The reset/cinematic
            // path clears it by walking qwords from 0x14485E750 for
            // 2 * 0x3D entries; it is not an array of heap pointers.
            const bool entries_ok =
                SafeReadBytes(reinterpret_cast<const void*>(
                                  base + kRVA_PerPlayerInputRing),
                              dst + kExtras_Off_InputRingEntries,
                              kExtras_InputRingEntries_Bytes);
            if (!entries_ok)
                m_last_extras_failure = "input-ring-entries";

            const bool cursor_ok =
                SafeReadBytes(reinterpret_cast<const void*>(
                                  base + kRVA_PerPlayerInputCursor),
                              dst + kExtras_Off_InputRingCursor,
                              kExtras_InputRingCursor_Bytes);
            if (entries_ok && !cursor_ok)
                m_last_extras_failure = "input-ring-cursor";

            const bool base_ok =
                SafeReadBytes(reinterpret_cast<const void*>(
                                  base + kRVA_InputRingBaseOffset),
                              dst + kExtras_Off_InputRingBase,
                              kExtras_InputRingBase_Bytes);
            if (entries_ok && cursor_ok && !base_ok)
                m_last_extras_failure = "input-ring-base";

            return entries_ok && cursor_ok && base_ok;
        }

        bool restore_input_ring_state(
            uintptr_t base,
            const uint8_t* src) noexcept
        {
            if (!base || !src) return false;

            bool ok = true;
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_PerPlayerInputRing),
                                 src + kExtras_Off_InputRingEntries,
                                 kExtras_InputRingEntries_Bytes);
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_PerPlayerInputCursor),
                                 src + kExtras_Off_InputRingCursor,
                                 kExtras_InputRingCursor_Bytes);
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_InputRingBaseOffset),
                                 src + kExtras_Off_InputRingBase,
                                 kExtras_InputRingBase_Bytes);
            return ok;
        }

        bool capture_required_rollback_globals(
            uintptr_t base,
            uint8_t* dst) noexcept
        {
            if (!base || !dst) return false;

            m_last_extras_failure = "none";
            bool ok = true;
            auto require = [&](bool step_ok, const char* reason) noexcept
            {
                if (!step_ok)
                {
                    ok = false;
                    if (m_last_extras_failure == nullptr
                        || std::strcmp(m_last_extras_failure, "none") == 0)
                    {
                        m_last_extras_failure = reason;
                    }
                }
            };

            require(SafeReadBytes(reinterpret_cast<const void*>(
                                      base + kRVA_LatestEngineInput),
                                  dst + kExtras_Off_LatestEngineInput,
                                  kExtras_LatestEngineInput_Bytes),
                    "latest-engine-input");
            require(SafeReadBytes(reinterpret_cast<const void*>(
                                      base + kRVA_PerFrameCameraArgs),
                                  dst + kExtras_Off_PerFrameCameraArgs,
                                  kExtras_PerFrameCameraArgs_Bytes),
                    "per-frame-camera-args");
            require(capture_input_ring_state(base, dst), "input-ring");
            require(SafeReadBytes(reinterpret_cast<const void*>(
                                      base + kRVA_LfsrState),
                                  dst + kExtras_Off_LfsrState,
                                  kExtras_LfsrState_Bytes),
                    "lfsr-state");
            require(SafeReadBytes(reinterpret_cast<const void*>(
                                      base + kRVA_LfsrIndex),
                                  dst + kExtras_Off_LfsrIndex,
                                  kExtras_LfsrIndex_Bytes),
                    "lfsr-index");
            require(SafeReadBytes(reinterpret_cast<const void*>(
                                      base + kRVA_CCpuCommandArray),
                                  dst + kExtras_Off_CCpuCommandArray,
                                  kExtras_CCpuCommandArray_Bytes),
                    "cpu-command-array");
            return ok;
        }

        bool restore_required_rollback_globals(
            uintptr_t base,
            const uint8_t* src) noexcept
        {
            if (!base || !src) return false;

            bool ok = true;
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_LatestEngineInput),
                                 src + kExtras_Off_LatestEngineInput,
                                 kExtras_LatestEngineInput_Bytes);
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_PerFrameCameraArgs),
                                 src + kExtras_Off_PerFrameCameraArgs,
                                 kExtras_PerFrameCameraArgs_Bytes);
            ok &= restore_input_ring_state(base, src);
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_LfsrState),
                                 src + kExtras_Off_LfsrState,
                                 kExtras_LfsrState_Bytes);
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_LfsrIndex),
                                 src + kExtras_Off_LfsrIndex,
                                 kExtras_LfsrIndex_Bytes);
            ok &= SafeWriteBytes(reinterpret_cast<void*>(
                                     base + kRVA_CCpuCommandArray),
                                 src + kExtras_Off_CCpuCommandArray,
                                 kExtras_CCpuCommandArray_Bytes);
            return ok;
        }

        // Capture BlockInteractiveOps + cinematic head + BM state bytes +
        // FrameInput / per-chara replay state into `dst` (the extras
        // region's staging buffer, kExtras_Bytes).  The blob layout is
        // documented at kExtras_Off_* above.
        //
        // Round-end seek-back fix (2026-05-14): without this, a backward
        // seek from post-KO into mid-round leaves WorldModePump's mode
        // pointer at "round-result", which re-publishes mode==3 to
        // MasterModeFlag next tick, gating PerFrameTick's chara input
        // tick OFF via the BattleAdvanceFlag check.  Captures the live
        // state of all four sub-fields; restores them surgically on seek.
        bool capture_extras(uint8_t* dst) noexcept
        {
            if (!dst) return false;
            // Pre-zero the whole staging blob: capture_extras fills only
            // specific sub-ranges and the rest must read back as zero.
            // The old per-slot ring was alloc-zeroed once; staging
            // buffers are reused tick-to-tick so they are zeroed here.
            std::memset(dst, 0, kExtras_Bytes);
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            bool required_ok = capture_required_rollback_globals(base, dst);

            // WorldModePump: NOT captured here (2026-05-16 fix).  The
            // engine's own HgCpuDirect snapshot already captures the mode
            // pointers (g_LuxBattle_WorldModePump.pCurrentMode /
            // pQueuedNextMode) as RELOCATABLE references and restores them
            // correctly - see the matching note in restore_extras().
            // HorseMod capturing them as a raw 64-byte blob was redundant,
            // and the raw restore caused a seek use-after-free; the
            // kExtras_Off_WorldModePump bytes are now left unused.

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
                int32_t frame_advance = -1;
                if (SafeReadInt32(bm + kBM_nFrameAdvanceCounter_Off,
                                  &frame_advance))
                    std::memcpy(dst + kExtras_Off_BM_FrameAdvance,
                                &frame_advance, sizeof(frame_advance));

                // Historical PlayerRecordArray diagnostics.  Ghidra
                // recheck showed PRA+0x398 bits 8/9 participate in
                // ReplayPlayer round-reset navigation, not per-frame
                // character movement.  Keep the bytes for comparisons,
                // but normal seek no longer restores or forces them.
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
                    // Capture 0x120 bytes covering both slot records in
                    // one SEH-guarded copy (was 72 separate SafeReadUInt32
                    // calls).  Pre-zero so a fault mid-teardown leaves a
                    // clean blob rather than the prior occupant's bytes.
                    std::memset(dst + kExtras_Off_FrameInput_Slots, 0,
                                kFI_SlotRecords_Bytes);
                    required_ok &= SafeReadBytes(
                        fi + kFI_SlotRecords_Start,
                        dst + kExtras_Off_FrameInput_Slots,
                        kFI_SlotRecords_Bytes);

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
            // Zero the three RP extras fields first: a null actor or a
            // failed read must leave a safe default (round 0) rather than
            // stale bytes from the staging buffer's prior tick.
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
                        // One SEH-guarded copy of the whole replay-state
                        // window (was 13 separate SafeReadUInt32 calls).
                        // Pre-zero so a fault mid-teardown leaves a clean
                        // blob rather than the prior occupant's bytes.
                        std::memset(dst + blob_off, 0,
                                    kExtras_CharaReplay_Bytes);
                        SafeReadBytes(c + kChara_ReplayState_Start,
                                      dst + blob_off,
                                      kExtras_CharaReplay_Bytes);
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
            return required_ok;
        }

        // Restore the captured extras blob into the engine.  The default
        // legacy/probe mode writes only the historically safe minimum.  The
        // captured seek path passes `full_for_validated_seek=true` and then
        // validates the restored state byte-for-byte before enabling Play.
        bool restore_extras(
            const uint8_t* src,
            bool full_for_validated_seek = false) noexcept
        {
            if (!src) return false;

            const uintptr_t base = NativeBinding::imageBase();
            bool ok = true;
            if (full_for_validated_seek)
                ok &= restore_required_rollback_globals(base, src);
            if (full_for_validated_seek && base)
            {
                SafeWriteBytes(reinterpret_cast<void*>(
                                   base + kRVA_BlockInteractiveOps),
                               src + kExtras_Off_BlockInteractive, 4);

                void* session_ptr = nullptr;
                SafeReadPtr(reinterpret_cast<const void*>(
                                base + kRVA_ActiveSessionDataPtr),
                            &session_ptr);
                if (session_ptr)
                {
                    uint8_t* cin =
                        reinterpret_cast<uint8_t*>(session_ptr)
                        + kCinematic_State_Off;
                    SafeWriteBytes(cin, src + kExtras_Off_CinematicHead,
                                   kExtras_CinematicHead_Bytes);
                }
            }

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
                            "main=0x{:X} move=0x{:X} status=0x{:X} "
                            "pause=0x{:X} frameAdv={}\n"),
                        static_cast<unsigned>(src[kExtras_Off_BM_MainState]),
                        static_cast<unsigned>(src[kExtras_Off_BM_MoveState]),
                        static_cast<unsigned>(src[kExtras_Off_BM_StatusByte]),
                        static_cast<unsigned>(src[kExtras_Off_BM_EnginePause]),
                        [&]() noexcept {
                            int32_t v = -1;
                            std::memcpy(&v, src + kExtras_Off_BM_FrameAdvance,
                                        sizeof(v));
                            return v;
                        }());
                }

                if (full_for_validated_seek)
                {
                    void* pra_raw = nullptr;
                    if (SafeReadPtr(bm + kBM_PlayerRecordArray_Off,
                                    &pra_raw) && pra_raw)
                    {
                        uint8_t* pra = reinterpret_cast<uint8_t*>(pra_raw);
                        SafeWriteBytes(pra + kPRA_FieldAt394_Off,
                                       src + kExtras_Off_PRA_P0_394, 4);
                        SafeWriteBytes(pra + kPRA_FieldAt398_Off,
                                       src + kExtras_Off_PRA_P0_398, 4);
                        SafeWriteBytes(pra + kPRA_PlayerStride
                                           + kPRA_FieldAt394_Off,
                                       src + kExtras_Off_PRA_P1_394, 4);
                        SafeWriteBytes(pra + kPRA_PlayerStride
                                           + kPRA_FieldAt398_Off,
                                       src + kExtras_Off_PRA_P1_398, 4);
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
                        const bool fi_write_ok = SafeWriteBytes(
                            fi + kFI_SlotRecords_Start,
                            src + kExtras_Off_FrameInput_Slots,
                            kFI_SlotRecords_Bytes);
                        if (full_for_validated_seek)
                            ok &= fi_write_ok;

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
                    else if (full_for_validated_seek)
                    {
                        ok = false;
                    }
                }
                else if (full_for_validated_seek)
                {
                    ok = false;
                }
            }

            if (full_for_validated_seek && base)
            {
                for (int pi = 0; pi < 2; ++pi)
                {
                    const uintptr_t slot_rva = (pi == 0)
                        ? ReplayScrubDiag::kRVA_CharaSlotP1
                        : ReplayScrubDiag::kRVA_CharaSlotP2;
                    void* chara_raw = nullptr;
                    if (!SafeReadPtr(reinterpret_cast<const void*>(
                                         base + slot_rva),
                                     &chara_raw) || !chara_raw)
                    {
                        ok = false;
                        continue;
                    }
                    uint8_t* c = reinterpret_cast<uint8_t*>(chara_raw);
                    const size_t blob_off = (pi == 0)
                        ? kExtras_Off_P1_CharaReplay
                        : kExtras_Off_P2_CharaReplay;
                    ok &= SafeWriteBytes(c + kChara_ReplayState_Start,
                                         src + blob_off,
                                         kExtras_CharaReplay_Bytes);
                }
            }
            return ok;
        }

        // The native round-result cinematic owns a private five-entry
        // HgCpuDirect ring inside g_LuxBattle_ActiveSessionDataPtr+0xAA120.
        // A captured seek restores battle/InputLog/RDB state, but not this
        // ~1 MB cinematic ring.  Leaving cross-round ring metadata intact can
        // make LuxBattle_RoundResultCinematic_StateMachineTick choose an old
        // full snapshot when the restored round ends.  Reset only metadata
        // and small control words; the native state-1 path can rebuild fresh
        // entries during resumed playback.
        bool reset_round_result_cinematic_ring_after_seek(
            const char* label,
            int32_t seq,
            int32_t round,
            int32_t master) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;

            void* session_ptr = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                                 base + kRVA_ActiveSessionDataPtr),
                             &session_ptr) || !session_ptr)
                return false;

            uint8_t* cin = reinterpret_cast<uint8_t*>(session_ptr)
                           + kCinematic_State_Off;
            int32_t zero = 0;
            int32_t tags[5] = {0, 0, 0, 0, 0};
            int32_t ctrl[4] = {0, 0, 0, 0};
            bool ok = true;
            ok &= SafeWriteBytes(cin + kCinematic_RingCount_Off,
                                 &zero, sizeof(zero));
            ok &= SafeWriteBytes(cin + kCinematic_RingCursor_Off,
                                 &zero, sizeof(zero));
            ok &= SafeWriteBytes(cin + kCinematic_RingTags_Off,
                                 tags, sizeof(tags));
            ok &= SafeWriteBytes(cin + kCinematic_CurrentFrame_Off,
                                 &master, sizeof(master));
            ok &= SafeWriteBytes(cin + kCinematic_PaletteCtrl_Off,
                                 ctrl, sizeof(ctrl));

            static std::atomic<int> s_log_count{0};
            const int prior = s_log_count.fetch_add(
                ok ? 1 : 0, std::memory_order_relaxed);
            if (ok && prior < 8)
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub.cinematic] reset seek-local "
                    "round-result ring label={} seq={} round={} "
                    "master={} session=0x{:X} cin=0x{:X}\n"),
                    RC::to_generic_string(label ? label : "?"),
                    seq, round, master,
                    reinterpret_cast<uintptr_t>(session_ptr),
                    reinterpret_cast<uintptr_t>(cin));
            }
            return ok;
        }

        // Map a timeline seq to its tick index.  Capture is append-only
        // and seq is gap-free from 0, so seq IS the tick index: this is a
        // clamp, not a search.  A too-new target clamps to the latest
        // tick; returns -1 if the timeline is empty or the target is
        // negative.
        int32_t find_slot_for_seq(int32_t target_seq) const noexcept
        {
            const size_t cnt = m_tags.count();
            if (cnt == 0 || target_seq < 0) return -1;
            const int32_t latest = latest_seq();
            if (latest < 0) return -1;
            if (target_seq > latest) return latest;
            return target_seq;
        }

        int32_t find_slot_for_round_master(int32_t round,
                                           int32_t master) const noexcept
        {
            if (round < 0 || master < 0) return -1;
            const int32_t latest = latest_seq();
            if (latest < 0) return -1;

            int32_t best_seq = -1;
            int32_t best_master = -1;
            for (int32_t k = 0; k <= latest; ++k)
            {
                int32_t s = -1, r = -1, f = -1, m = -1;
                if (!m_tags.get(static_cast<size_t>(k), s, r, f, m)) break;
                if (r != round || m < 0 || m > master) continue;
                if (m >= best_master)
                {
                    best_master = m;
                    best_seq = s;
                }
            }
            return best_seq;
        }

        // Avoid restoring snapshots from the round-transition window.
        // The UI marker for "R2" points at the first captured tick whose
        // round tag changed, but those first ticks are not stable restore
        // points: SC6 has just rebased replay clocks and is rebuilding
        // round-local state.  Return either the original tick or a nearby
        // tick safely inside the same round.
        int32_t adjust_seek_tick_away_from_round_boundary(
            int32_t tick) const noexcept
        {
            if (tick <= 0) return tick;

            const size_t cnt = m_tags.count();
            if (cnt == 0 || static_cast<size_t>(tick) >= cnt) return tick;

            int32_t seq = -1, round = -1, wall = -1, master = -1;
            if (!m_tags.get(static_cast<size_t>(tick),
                            seq, round, wall, master))
                return tick;

            size_t next_round = static_cast<size_t>(tick) + 1;
            while (next_round < cnt)
            {
                int32_t ns = -1, nr = -1, nf = -1, nm = -1;
                if (!m_tags.get(next_round, ns, nr, nf, nm)) break;
                if (nr != round) break;
                ++next_round;
            }

            // The last few snapshots before a round tag change are part of
            // the same transition hazard as the first snapshots after it:
            // BM/replay actors are being parked for round teardown.  If the
            // user clicks just before the R2 marker, move deeper into the
            // current round instead of restoring the teardown edge.
            if (next_round < cnt)
            {
                const size_t frames_to_next = next_round
                    - static_cast<size_t>(tick);
                if (frames_to_next
                    <= static_cast<size_t>(kRoundBoundarySeekGuardFrames))
                {
                    if (static_cast<size_t>(tick)
                        >= static_cast<size_t>(kRoundBoundarySeekGuardFrames))
                    {
                        const size_t adjusted = static_cast<size_t>(tick)
                            - static_cast<size_t>(
                                kRoundBoundarySeekGuardFrames);
                        int32_t as = -1, ar = -1, af = -1, am = -1;
                        if (m_tags.get(adjusted, as, ar, af, am)
                            && ar == round)
                            return static_cast<int32_t>(adjusted);
                    }
                    return tick;
                }
            }

            size_t start = static_cast<size_t>(tick);
            while (start > 0)
            {
                int32_t ps = -1, pr = -1, pf = -1, pm = -1;
                if (!m_tags.get(start - 1, ps, pr, pf, pm)) return tick;
                if (pr != round) break;
                --start;
            }

            // start==0 is the first round's initial capture, not a
            // mid-match transition from one round object graph to another.
            if (start == 0) return tick;

            const size_t frames_into_round =
                static_cast<size_t>(tick) - start;

            // Two independent transition signals have proven unsafe:
            // the first committed ticks after the round tag changes, and
            // low round-local replay clocks even after more than 30 ticks.
            // The 2026-05-18 crash restored master_clock=53 / lastFID=1.
            if (frames_into_round
                    >= static_cast<size_t>(kRoundBoundarySeekGuardFrames)
                && master >= kRoundBoundarySeekGuardMaster)
                return tick;

            size_t adjusted = start
                + static_cast<size_t>(kRoundBoundarySeekGuardFrames);
            if (adjusted >= cnt) adjusted = cnt - 1;

            int32_t as = -1, ar = -1, af = -1, am = -1;
            if (!m_tags.get(adjusted, as, ar, af, am) || ar != round)
                return tick;

            while (adjusted + 1 < cnt
                   && am < kRoundBoundarySeekGuardMaster)
            {
                int32_t ns = -1, nr = -1, nf = -1, nm = -1;
                if (!m_tags.get(adjusted + 1, ns, nr, nf, nm)
                    || nr != round)
                    break;
                ++adjusted;
                as = ns;
                ar = nr;
                af = nf;
                am = nm;
            }

            if (am < kRoundBoundarySeekGuardMaster)
                return tick;

            return static_cast<int32_t>(adjusted);
        }

        bool native_demo_time_is_trusted_for_seek(
            size_t tick,
            int32_t demo_ms,
            int32_t seq_tag,
            int32_t master_tag) noexcept
        {
            if (demo_ms < 0) return false;

            int32_t min_ms = -1;
            int32_t max_ms = -1;
            size_t valid_count = 0;
            const bool have_stats =
                m_tags.demo_time_stats(min_ms, max_ms, valid_count);
            const size_t total_count = m_tags.count();
            const int32_t span_ms =
                have_stats && max_ms >= min_ms ? (max_ms - min_ms) : -1;

            // A real UDemoNetDriver's DemoCurrentTime is absolute replay
            // time and must advance over a generated match.  A plain
            // UWorld.NetDriver in SC6 has readable zeros at the same offsets;
            // accepting that false-positive turns every target into
            // demo.GotoTimeInSeconds 0.000 and lets the parked replay run out
            // while the settle window is open.
            const bool mature_timeline = total_count >= 300;
            const bool progressed =
                have_stats
                && valid_count >= (mature_timeline ? total_count / 2 : 1)
                && span_ms >= (mature_timeline ? 500 : 0);
            const bool zero_late_target =
                demo_ms == 0 && (master_tag > 60 || tick > 60);
            if (progressed && !zero_late_target)
                return true;

            static std::atomic<bool> s_warned_untrusted_demo_time{false};
            if (!s_warned_untrusted_demo_time.exchange(
                    true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] timeline native demo timestamps are "
                    "untrusted; native seek is blocked so pause gates stay "
                    "closed (seq={} tick={} master={} demo_ms={} "
                    "valid={} count={} min_ms={} max_ms={} span_ms={})\n"),
                    seq_tag, tick, master_tag, demo_ms, valid_count,
                    total_count, min_ms, max_ms, span_ms);
            }
            return false;
        }

        PreviewStatus apply_preview_snapshot(int32_t target_seq,
                                             uint32_t generation) noexcept
        {
            (void)target_seq;
            (void)generation;
            if (!kEnableLegacySnapshotPreview)
            {
                publish_preview_status(PreviewStatus::SkippedUnsafe);
                return PreviewStatus::SkippedUnsafe;
            }
#if HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG
            if (!is_initialized() || !m_exec_read)
            {
                publish_preview_status(PreviewStatus::Failed,
                                       NativeSeekFailure::InvalidTarget);
                return PreviewStatus::Failed;
            }
            int32_t tick = find_slot_for_seq(target_seq);
            if (tick < 0)
            {
                publish_preview_status(PreviewStatus::Failed,
                                       NativeSeekFailure::InvalidTarget);
                return PreviewStatus::Failed;
            }

            tick = adjust_seek_tick_away_from_round_boundary(tick);
            int32_t seq_tag = -1, round_tag = -1,
                    wall_tag = -1, master_tag = -1;
            if (!m_tags.get(static_cast<size_t>(tick),
                            seq_tag, round_tag, wall_tag, master_tag))
            {
                publish_preview_status(PreviewStatus::Failed,
                                       NativeSeekFailure::InvalidTarget);
                return PreviewStatus::Failed;
            }
            if (generation != m_seek_generation)
                return PreviewStatus::SkippedUnsafe;
            if (m_ui_requested_seq.load(std::memory_order_acquire)
                != target_seq)
                return PreviewStatus::SkippedUnsafe;

            const uint8_t* sim_blob =
                m_sim_store.gather(static_cast<size_t>(tick));
            if (!sim_blob)
            {
                publish_preview_status(PreviewStatus::Failed,
                                       NativeSeekFailure::InvalidTarget);
                return PreviewStatus::Failed;
            }

            publish_preview_status(PreviewStatus::Requested);
            m_shim.retarget(const_cast<uint8_t*>(sim_blob), kSnapshotStride);
            if (!SafeInvokeExec(m_exec_read, &m_shim))
            {
                m_preview.seq = seq_tag;
                m_preview.round = round_tag;
                publish_preview_status(PreviewStatus::Failed,
                                       NativeSeekFailure::CallFaulted);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] preview failed: snapshot restore faulted "
                    "(seq={} tick={} round={} master={})\n"),
                    seq_tag, tick, round_tag, master_tag);
                return PreviewStatus::Failed;
            }

            const uint8_t* il_blob =
                m_il_store.gather(static_cast<size_t>(tick));
            restore_input_cache(il_blob);
            restore_replay_data_block(
                m_rdb_store.gather(static_cast<size_t>(tick)));
            restore_extras(m_extras_store.gather(static_cast<size_t>(tick)));

            m_preview.seq = seq_tag;
            m_preview.round = round_tag;
            publish_preview_status(PreviewStatus::Applied);
            if (generation == m_seek_generation
                && m_ui_requested_seq.load(std::memory_order_acquire)
                    == target_seq)
            {
                publish_ui_target(seq_tag);
            }
            if (m_verbose_diag.load(std::memory_order_acquire))
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] preview applied seq={} round={} "
                    "master={} (visual-only; native status={})\n"),
                    seq_tag, round_tag, master_tag,
                    m_native_status.load(std::memory_order_acquire));
            }
            return PreviewStatus::Applied;
#else
            publish_preview_status(PreviewStatus::SkippedUnsafe);
            return PreviewStatus::SkippedUnsafe;
#endif
        }

#if HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG
        SeekApplyStatus start_native_seek_for_seq(int32_t target_seq) noexcept
        {
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub] DIAGNOSTIC LEGACY seek wrapper used "
                "target_seq={} - this path cannot enable Play\n"),
                target_seq);
            const SeekApplyStatus status = do_seek_to_seq(target_seq);
            switch (status)
            {
            case SeekApplyStatus::Submitted:
                if (m_native_demo_seek_settle_ticks.load(
                        std::memory_order_acquire) > 0)
                {
                    publish_native_status(NativeSeekStatus::Settling);
                    publish_mode(ScrubMode::NativeSeekSettling);
                }
                else
                {
                    publish_native_status(NativeSeekStatus::Submitted);
                    publish_mode(ScrubMode::NativeSeekSubmitted);
                }
                break;
            case SeekApplyStatus::Pending:
            {
                NativeSeekFailure reason = m_native.failure;
                if (reason == NativeSeekFailure::None)
                    reason = m_native.direct_driver_available
                        ? NativeSeekFailure::DriverBusy
                        : NativeSeekFailure::DriverUnresolved;
                publish_native_status(NativeSeekStatus::Queued, reason);
                publish_mode(ScrubMode::NativeSeekQueued);
                break;
            }
            case SeekApplyStatus::AppliedLegacy:
            case SeekApplyStatus::AppliedSc6Exact:
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] DIAGNOSTIC LEGACY seek produced an "
                    "applied status, but diagnostic paths cannot mark "
                    "native Landed or enable Play\n"));
                publish_native_status(NativeSeekStatus::Failed,
                                      NativeSeekFailure::NotLanded);
                publish_mode(ScrubMode::NativeSeekFailed);
                break;
            case SeekApplyStatus::Failed:
            default:
            {
                NativeSeekFailure reason = m_native.failure;
                if (reason == NativeSeekFailure::None)
                    reason = NativeSeekFailure::InvalidTarget;
                publish_native_status(NativeSeekStatus::Failed, reason);
                publish_mode(ScrubMode::NativeSeekFailed);
                break;
            }
            }
            return status;
        }
#endif

        // Restore the simulation from the captured tick `target_seq`
        // (clamped to the timeline).  Does NOT touch m_paused - pause
        // state is purely UI-driven (drag-start sets it, drag-end
        // optionally clears it, Play/Pause and step buttons set it
        // explicitly).
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
        // No-op if the timeline is empty.
#if HORSEMOD_REPLAY_ENABLE_LEGACY_SEEK_DIAG
        SeekApplyStatus do_seek_to_seq(int32_t target_seq) noexcept
        {
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayScrub] DIAGNOSTIC LEGACY seek path used "
                "target_seq={} - may adjust for inspection only and "
                "cannot be replay authority\n"),
                target_seq);
            if (!is_initialized() || !m_exec_read)
                return SeekApplyStatus::Failed;
            int32_t tick = find_slot_for_seq(target_seq);
            if (tick < 0) return SeekApplyStatus::Failed;
            const int32_t requested_tick = tick;
            int32_t requested_seq = -1, requested_round = -1,
                    requested_wall = -1, requested_master = -1;
            (void)m_tags.get(static_cast<size_t>(tick),
                             requested_seq, requested_round,
                             requested_wall, requested_master);

            tick = adjust_seek_tick_away_from_round_boundary(tick);
            if (tick != requested_tick)
            {
                int32_t adjusted_seq = -1, adjusted_round = -1,
                        adjusted_wall = -1, adjusted_master = -1;
                (void)m_tags.get(static_cast<size_t>(tick),
                                 adjusted_seq, adjusted_round,
                                 adjusted_wall, adjusted_master);
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] seek target_seq={} adjusted away from "
                    "round boundary: tick {}(round={} master={}) -> "
                    "{}(round={} master={})\n"),
                    target_seq, requested_tick, requested_round,
                    requested_master, tick, adjusted_round,
                    adjusted_master);
            }

            int32_t seq_tag = -1, round_tag = -1,
                    wall_tag = -1, master_tag = -1;
            if (!m_tags.get(static_cast<size_t>(tick),
                            seq_tag, round_tag, wall_tag, master_tag))
                return SeekApplyStatus::Failed;
            const int32_t visible_requested =
                m_ui_requested_seq.load(std::memory_order_acquire);
            if (visible_requested != target_seq
                && visible_requested != seq_tag)
            {
                return SeekApplyStatus::Pending;
            }
            if (visible_requested == target_seq && seq_tag != target_seq)
            {
                publish_ui_target(seq_tag);
                m_native.requested_seq = seq_tag;
            }

            // Defence in depth: capture_snapshot never commits a tick
            // whose master clock was unreadable, so this should never
            // fire.  If it ever does, refuse the seek rather than
            // poison the engine cursors with -1.
            if (master_tag < 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[ReplayScrub] seek refused: tick={} wall_tag={} "
                        "has master_tag=-1 (capture-time master clock "
                        "was unreadable)\n"),
                    tick, wall_tag);
                return SeekApplyStatus::Failed;
            }

            const int32_t demo_ms = m_tags.demo_time_ms(
                static_cast<size_t>(tick));
            constexpr bool rp_cursor_seek = false;
            constexpr bool native_demo_seek = kEnableLegacySeekDiagnostics;
            const bool has_native_demo_time = (demo_ms >= 0);
            const int32_t native_target_ms =
                has_native_demo_time ? demo_ms : -1;
            const float replay_player_seconds =
                static_cast<float>(master_tag) / 60.0f;
            const uint32_t seek_generation = m_native.generation;

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
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] seek begin target_seq={} "
                    "requested_tick={} tick={} round={} wall_tag={} "
                    "master_tag={} paused={} rp_cursor={} native_demo={} "
                    "demo_ms={} native_target_ms={}\n"),
                    target_seq, requested_tick, tick, round_tag, wall_tag,
                    master_tag,
                    m_paused.load(std::memory_order_acquire) ? 1 : 0,
                    rp_cursor_seek ? 1 : 0,
                    native_demo_seek ? 1 : 0, demo_ms, native_target_ms);

                // Full replay-system state before the restore; pairs
                // with POST_SEEK below for a side-by-side log diff.
                dump_replay_state("PRE_SEEK", seq_tag);
                ReplayScrubDiag::dump_full("PRE_SEEK");
            }

            // Primary seek path: let UE4's replay driver rebuild the
            // checkpoint/packet stream.  This must run before any legacy
            // HgCpuDirect restore because that restore writes the current
            // live chara graph and can fault when the target is in another
            // round/session context.  Native seek is async; if the driver
            // is busy, keep the newest requested time and retry on later
            // cockpit ticks.
            if (native_demo_seek)
            {
                const bool trusted_native_demo_time =
                    has_native_demo_time
                    && native_demo_time_is_trusted_for_seek(
                        static_cast<size_t>(tick), demo_ms, seq_tag,
                        master_tag);
                if (!trusted_native_demo_time)
                {
                    static std::atomic<bool> s_logged_sc6_exact{false};
                    if (!s_logged_sc6_exact.exchange(
                            true, std::memory_order_relaxed))
                    {
                        RC::Output::send<RC::LogLevel::Warning>(STR(
                            "[ReplayScrub] UDemoNetDriver time is absent or "
                            "untrusted in this SC6 replay viewer path; using "
                            "native SC6 round reset + deterministic "
                            "PerFrameTick fast-forward. Play remains blocked "
                            "until round/master verify exactly.\n"));
                    }
                    m_native.direct_driver_available = false;
                    m_native.cvar_submitted = false;
                    m_native.requested_seq = target_seq;
                    m_native.adjusted_seq = seq_tag;
                    m_native.target_ms = -1;
                    m_native.round = round_tag;
                    m_native.master = master_tag;
                    return apply_sc6_exact_seek(
                        static_cast<size_t>(tick), seq_tag, round_tag,
                        master_tag);
                }
                else
                {

                    const bool direct_driver_available =
                        ReplayScrubDiag::read_demo_net_driver().readable;
                    m_native.direct_driver_available =
                        direct_driver_available;
                    m_native.cvar_submitted = false;
                    m_native.requested_seq = target_seq;
                    m_native.adjusted_seq = seq_tag;
                    m_native.target_ms = native_target_ms;
                    m_native.round = round_tag;
                    m_native.master = master_tag;
                    const bool native_submitted =
                        request_demo_goto_time_seek_ms(native_target_ms,
                                                       master_tag, round_tag,
                                                       seq_tag,
                                                       seek_generation);

                    if (native_submitted)
                    {
                        if (verbose)
                        {
                            dump_replay_state(
                                "POST_NATIVE_SEEK_REQUEST", seq_tag);
                            ReplayScrubDiag::dump_full(
                                "POST_NATIVE_SEEK_REQUEST");
                            m_post_seek_countdown.store(
                                kDefaultPostSeekDumpFrames,
                                std::memory_order_release);
                            m_last_movevm_p1 =
                                ReplayScrubDiag::read_chara_movevm(0);
                            m_last_movevm_p2 =
                                ReplayScrubDiag::read_chara_movevm(1);
                            uint32_t cur_seed = 0;
                            read_frame_counter(cur_seed);
                            m_diag_last_wall   = cur_seed;
                            m_diag_last_master = master_tag;
                            m_diag_last_paused =
                                m_paused.load(std::memory_order_acquire);
                        }
                        return SeekApplyStatus::Submitted;
                    }

                    // Keep the newest native request pending and do not write
                    // legacy state over the replay driver.  A visual snapshot
                    // preview can make a broken native seek look fixed while
                    // the engine's real playback cursor is still wrong; the
                    // default path must either queue UDemoNetDriver work or
                    // remain visibly unresolved.
                    return SeekApplyStatus::Pending;
                }
            }

            // Legacy snapshot restore path.  This remains available only
            // when native DemoNetDriver seek is explicitly disabled.  It
            // writes deep battle/chara state, so keep the live-context
            // checks here.
            if (read_engine_master_clock() < 0 || !charas_alive())
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] legacy seek refused: battle context not "
                    "live (BM/InputLog/chara torn down)\n"));
                return SeekApplyStatus::Failed;
            }

            const int32_t live_round = read_current_round();
            const int32_t live_round_result = read_last_round_result();
            if (live_round >= 0 && round_tag != live_round)
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] legacy cross-round snapshot restore: "
                    "live_round={} target_round={} live_result={}\n"),
                    live_round, round_tag, live_round_result);
            }

            // Step 1: Restore full simulation state (chara, globals,
            // terrain, camera, timer, motion, physics, VFX) to the
            // snapshot frame.  gather() reconstructs the tick's sim
            // region byte-for-byte into the sim store's scratch buffer;
            // the shim reads ~80-100 KB of it back into the engine.
            const uint8_t* sim_blob =
                m_sim_store.gather(static_cast<size_t>(tick));
            if (!sim_blob) return SeekApplyStatus::Failed;
            m_shim.retarget(const_cast<uint8_t*>(sim_blob), kSnapshotStride);
            // SEH-guarded: if the restore faults (captured state vs. live
            // context mismatch) abort the seek instead of crashing.
            if (!SafeInvokeExec(m_exec_read, &m_shim))
            {
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] seek aborted: snapshot restore faulted "
                    "(tick={}) - engine state left as-is\n"), tick);
                return SeekApplyStatus::Failed;
            }

            // Step 2: Restore the captured InputLog state window
            // (pInputLog+0x394..+0x4414).  Reproduces the engine's
            // entire replay-playback bookkeeping at the captured
            // frame: cache entries, drain cursor, double-tick guard,
            // playback cursor, master/last-frame IDs, and any other
            // hidden state in the +0x394..+0x4414 byte range.  The
            // gathered pointer is held for Step 5's nLastFrameID read -
            // nothing re-gathers the IL store between here and there.
            const uint8_t* il_blob =
                m_il_store.gather(static_cast<size_t>(tick));
            restore_input_cache(il_blob);

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
            restore_replay_data_block(
                m_rdb_store.gather(static_cast<size_t>(tick)));

            // Step 4: Restore round-end-fix extras (BlockInteractiveOps +
            // cinematic head + BM state bytes + FrameInput / per-chara
            // replay state).
            //
            // Done BEFORE the BM cursor write below so the BM state-
            // byte restore doesn't clobber values the cursor sync
            // depends on.  See capture_extras / restore_extras for the
            // exact byte ranges; the round-end seek-back failure mode
            // (characters frozen post-restore because PerFrameTick's
            // BattleAdvanceFlag check still sees mode==3) is what this
            // step fixes.
            restore_extras(m_extras_store.gather(static_cast<size_t>(tick)));

            // Step 5: Sync the BM-side replay cursors (BM+0x148C
            // nReplayLastApplied AND BM+0x1488 nReplayLastFrameID).
            // These are the only BM fields outside both the
            // HgCpuDirect chara/global window AND the InputLog/
            // DecoderBlock state we just restored.
            //
            // Read the snapshot's nLastFrameID from the IL blob
            // gathered in Step 2 (offset +0xC within the blob since the
            // capture starts at +0x394 and nLastFrameID is at +0x3A0).
            // We must write BM+0x1488 to THIS value, not to master_tag,
            // because SimulationLoop's mismatch check compares the
            // BM-side cache against the live IL field which is the
            // snapshot value after restore_input_cache.
            int32_t snapshot_last_frame_id = -1;
            if (il_blob)
            {
                std::memcpy(&snapshot_last_frame_id,
                            il_blob + (kIL_nLastFrameID_Off
                                       - kIL_CaptureStart_Off),
                            sizeof(snapshot_last_frame_id));
            }
            write_replay_cursors(master_tag, snapshot_last_frame_id);
            write_replay_player_cursor(round_tag, master_tag,
                                       replay_player_seconds, true);

            // Step 6 (EXPERIMENTAL): rewind the UE4-level playback
            // Step 6 moved out of restore_extras after the 2026-05-18
            // post-generate test: captured ReplayPlayer extras can be
            // zero/stale after match end.  We now derive the cursor from
            // the seek target's tags and only write the three RP fields.

            m_last_seek_target.store(seq_tag, std::memory_order_release);
            // Record the master clock value the engine is now at so the
            // UI playhead can extrapolate forward as master advances.
            m_last_seek_master_tag.store(master_tag, std::memory_order_release);

            const int32_t live_round_after = read_current_round();
            const int32_t live_master_after = read_engine_master_clock();
            const int32_t master_delta =
                (live_master_after >= master_tag)
                    ? (live_master_after - master_tag)
                    : (master_tag - live_master_after);
            if (live_round_after != round_tag
                || live_master_after < 0
                || master_delta > 1)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::LegacyVerifyFailed);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] SC6 replay-state seek verification "
                    "failed: seq={} round {}->{} master {}->{} "
                    "master_delta={} tick={}\n"),
                    seq_tag, live_round_after, round_tag,
                    live_master_after, master_tag, master_delta, tick);
                return SeekApplyStatus::Failed;
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
                    STR("[ReplayScrub] seek target_seq={} -> tick={} "
                        "round={} wall_tag={} master_tag={} (paused={})  "
                        "armed post-seek tick dump for {} frames\n"),
                    target_seq, tick, round_tag, wall_tag, master_tag,
                    m_paused.load(std::memory_order_acquire) ? 1 : 0,
                    kDefaultPostSeekDumpFrames);
            }
            return SeekApplyStatus::AppliedLegacy;
        }
#endif

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
            {
                // Find the tick matching this hint frame and report its
                // captured master_tag; -1 if not on the timeline.
                const int32_t s = find_slot_for_seq(hint_frame);
                int32_t s_seq, s_round, s_wall, s_master;
                if (s >= 0
                    && m_tags.get(static_cast<size_t>(s),
                                  s_seq, s_round, s_wall, s_master))
                    slot_master_tag = s_master;
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

            // Dump PlayerRecordArrayPtr+0x394/+0x398 bits for both
            // players.  These were a false lead for movement resume, but
            // remain useful when comparing round-reset state.
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
        bool write_replay_cursors(int32_t master_clock,
                                  int32_t last_frame_id,
                                  uintptr_t battle_manager = 0,
                                  int32_t frame_advance = -1) noexcept
        {
            // Resolve BM via UObjectGlobals (cached in m_bm_ptr after
            // first call).  Returns null if the battle manager isn't
            // alive yet (between matches).
            if (!battle_manager)
            {
                RC::Unreal::UObject* bm_obj =
                    m_bm_ptr.get(L"LuxBattleManager");
                battle_manager = reinterpret_cast<uintptr_t>(bm_obj);
            }
            if (!battle_manager) return false;
            uint8_t* bm = reinterpret_cast<uint8_t*>(battle_manager);

            // BM-side writes.  Plain stores - the BM struct lives in
            // ordinary heap memory; no SEH wrapping needed.
            if (!SafeWriteBytes(bm + kBM_nReplayLastApplied_Off,
                                &master_clock, sizeof(master_clock))
                || !SafeWriteBytes(bm + kBM_nReplayLastFrameID_Off,
                                   &last_frame_id,
                                   sizeof(last_frame_id)))
                return false;

            if (frame_advance >= 0
                && !SafeWriteBytes(bm + kBM_nFrameAdvanceCounter_Off,
                                   &frame_advance,
                                   sizeof(frame_advance)))
                return false;

            // First-fire log so the user can confirm the cursor sync
            // path is alive.
            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ReplayScrub] first cursor sync; master_clock={} "
                        "last_frame_id={} frame_advance={} BM=0x{:X} "
                        "BM->nReplayLastApplied + BM->nReplayLastFrameID "
                        "+ BM->nFrameAdvanceCounter written\n"),
                    master_clock, last_frame_id,
                    frame_advance,
                    reinterpret_cast<uintptr_t>(bm));
            }
            return true;
        }

        bool write_replay_player_cursor(int32_t round_tag,
                                        int32_t master_clock,
                                        float current_time_seconds,
                                        bool force = false,
                                        uintptr_t replay_player = 0) noexcept
        {
            if (!force) return true;
            if (round_tag < 0 || master_clock < 0) return false;

            if (!replay_player)
            {
                RC::Unreal::UObject* rp_obj =
                    ReplayScrubDiag::replay_player_ptr().get(
                        L"LuxBattleReplayPlayer");
                replay_player = reinterpret_cast<uintptr_t>(rp_obj);
            }
            if (!replay_player) return true;

            uint8_t* rp = reinterpret_cast<uint8_t*>(replay_player);
            const uint8_t is_playing = 1;

            if (!SafeWriteBytes(rp + kRP_CurrentRound_Off,
                                &round_tag, sizeof(round_tag))
                || !SafeWriteBytes(rp + kRP_CurrentTime_Off,
                                   &current_time_seconds,
                                   sizeof(current_time_seconds))
                || !SafeWriteBytes(rp + kRP_IsPlayingBack_Off,
                                   &is_playing, sizeof(is_playing)))
                return false;

            static std::atomic<bool> s_logged{false};
            if (!s_logged.exchange(true, std::memory_order_relaxed))
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] first ReplayPlayer cursor sync; "
                    "CurrentRound={} CurrentTime={:.4f} "
                    "bIsPlayingBack=1 (round-local master_clock={})\n"),
                    round_tag, current_time_seconds, master_clock);
            }
            return true;
        }

        static bool demo_driver_has_pending_goto(
            const ReplayScrubDiag::DemoNetDriverSnap& s) noexcept
        {
            return s.raw_busy_791 != 0
                || s.raw_current_task_7b8 != 0
                || s.raw_task_count_7b0 > 0;
        }

        void begin_native_demo_seek_settle(int32_t target_ms,
                                           int32_t seq_tag,
                                           int32_t master_clock,
                                           uint32_t generation) noexcept
        {
            m_native_demo_seek_settle_ms.store(target_ms,
                                               std::memory_order_release);
            m_native_demo_seek_settle_seq.store(seq_tag,
                                                std::memory_order_release);
            m_native_demo_seek_settle_master.store(master_clock,
                                                   std::memory_order_release);
            m_native_demo_seek_settle_generation.store(
                generation, std::memory_order_release);
            m_native_demo_seek_settle_ticks.store(
                kNativeDemoSeekSettleTicks, std::memory_order_release);
            publish_native_status(NativeSeekStatus::Settling);
            publish_mode(ScrubMode::NativeSeekSettling);
            if (m_paused.load(std::memory_order_acquire))
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] native seek queued while paused; "
                    "temporarily releasing tick gates for up to {} frames "
                    "so UE4 can rebuild to {:.3f}s, then pause will "
                    "re-freeze on the landed state\n"),
                    kNativeDemoSeekSettleTicks,
                    static_cast<float>(target_ms) / 1000.0f);
            }
        }

        void service_native_demo_seek_settle() noexcept
        {
            int32_t ticks = m_native_demo_seek_settle_ticks.load(
                std::memory_order_acquire);
            if (ticks <= 0) return;

            bool landed = false;
            const int32_t target_ms = m_native_demo_seek_settle_ms.load(
                std::memory_order_acquire);
            const float target_seconds =
                static_cast<float>(target_ms) / 1000.0f;

            ReplayScrubDiag::DemoTimeSourceSnap time_source =
                ReplayScrubDiag::read_demo_time_source_fast();
            if (!time_source.readable)
                time_source = ReplayScrubDiag::read_demo_time_source();

            ReplayScrubDiag::DemoNetDriverSnap d =
                ReplayScrubDiag::read_demo_net_driver_fast();
            if (!d.readable)
                d = ReplayScrubDiag::read_demo_net_driver();

            const bool busy = d.readable
                && (demo_driver_has_pending_goto(d)
                    || d.raw_loading_794 != 0);
            const float delta =
                time_source.raw_demo_cur_time > target_seconds
                    ? time_source.raw_demo_cur_time - target_seconds
                    : target_seconds - time_source.raw_demo_cur_time;
            const bool time_close =
                time_source.readable
                && time_source.time_sane
                && target_ms >= 0
                && delta <= kNativeSeekTimeToleranceSeconds;
            const int32_t live_round = read_current_round();
            const int32_t live_master = read_engine_master_clock();
            const int32_t target_round = m_native.round;
            const int32_t target_master =
                m_native_demo_seek_settle_master.load(
                    std::memory_order_acquire);
            const int32_t master_delta =
                (live_master >= target_master)
                    ? (live_master - target_master)
                    : (target_master - live_master);
            const bool round_exact =
                target_round >= 0 && live_round >= 0
                && live_round == target_round;
            const bool master_exact =
                target_master >= 0 && live_master >= 0
                && master_delta <= 1;
            landed = !busy && time_close && round_exact && master_exact;

            if (landed || ticks <= 1)
            {
                m_native_demo_seek_settle_ticks.store(
                    0, std::memory_order_release);
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayScrub] native seek settle {} after {} frame(s) "
                    "(seq={} generation={} current_generation={} "
                    "time_source=0x{:X} cur={:.3f}s target={:.3f}s "
                    "delta={:.6f}s tolerance={:.6f}s round {}->{} "
                    "master {}->{} master_delta={} driver=0x{:X} "
                    "busy={} tasks={} current_task=0x{:X})\n"),
                    RC::to_generic_string(landed
                        ? "landed exact" : "timed out"),
                    kNativeDemoSeekSettleTicks - ticks + 1,
                    m_native_demo_seek_settle_seq.load(
                        std::memory_order_acquire),
                    m_native_demo_seek_settle_generation.load(
                        std::memory_order_acquire),
                    m_seek_generation,
                    time_source.source_ptr,
                    time_source.raw_demo_cur_time,
                    target_seconds,
                    delta,
                    kNativeSeekTimeToleranceSeconds,
                    live_round,
                    target_round,
                    live_master,
                    target_master,
                    master_delta,
                    d.driver_ptr,
                    busy ? 1 : 0,
                    d.raw_task_count_7b0, d.raw_current_task_7b8);
                const int32_t settle_seq =
                    m_native_demo_seek_settle_seq.load(
                        std::memory_order_acquire);
                const int32_t settle_master =
                    m_native_demo_seek_settle_master.load(
                        std::memory_order_acquire);
                const uint32_t settle_generation =
                    m_native_demo_seek_settle_generation.load(
                        std::memory_order_acquire);
                const int32_t requested =
                    m_ui_requested_seq.load(std::memory_order_acquire);
                if (landed && settle_generation == m_seek_generation
                    && settle_seq == requested)
                {
                    m_last_seek_target.store(settle_seq,
                                             std::memory_order_release);
                    m_last_seek_master_tag.store(settle_master,
                                                 std::memory_order_release);
                    m_live_master_cached.store(settle_master,
                                               std::memory_order_release);
                    m_native.adjusted_seq = settle_seq;
                    m_native.master = settle_master;
                    publish_native_status(NativeSeekStatus::Landed);
                    publish_mode(ScrubMode::NativeSeekLanded);
                    if (m_ui_wants_play.load(std::memory_order_acquire)
                        && m_auto_resume_on_release.load(
                            std::memory_order_acquire))
                    {
                        (void)resume_play_if_battle_status_active(
                            "NATIVE_DEMO_AUTO_RESUME");
                    }
                }
                else if (landed)
                {
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[ReplayScrub] ignored stale native seek land: "
                        "settle_seq={} requested_seq={} generation={} "
                        "current_generation={}\n"),
                        settle_seq, requested, settle_generation,
                        m_seek_generation);
                }
                else if (settle_generation == m_seek_generation
                         && settle_seq == requested)
                {
                    if (!time_source.readable)
                    {
                        publish_native_status(
                            NativeSeekStatus::Failed,
                            NativeSeekFailure::NativeTimeSourceUnresolved);
                        publish_mode(ScrubMode::NativeSeekFailed);
                        return;
                    }
                    publish_native_status(NativeSeekStatus::Failed,
                                          NativeSeekFailure::SettleTimedOut);
                    publish_mode(ScrubMode::NativeSeekFailed);
                }
                return;
            }

            m_native_demo_seek_settle_ticks.store(
                ticks - 1, std::memory_order_release);
        }

        bool safe_call_demo_goto_time(void* driver,
                                      float target_seconds) noexcept
        {
            if (!m_demo_goto_time || !driver) return false;
            __try
            {
                m_demo_goto_time(driver, target_seconds, nullptr);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                ReplayScrubDiag::clear_cached_demo_driver();
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] native DemoNetDriver seek call faulted; "
                    "cleared cached driver and will retry/fallback "
                    "(driver=0x{:X} target={:.3f}s)\n"),
                    reinterpret_cast<uintptr_t>(driver), target_seconds);
                return false;
            }
        }

        bool request_demo_goto_time_seek_ms(int32_t target_ms,
                                            int32_t master_clock,
                                            int32_t round_tag,
                                            int32_t seq_tag,
                                            uint32_t generation) noexcept
        {
            const bool had_pending =
                m_pending_demo_seek_ms.load(std::memory_order_acquire) >= 0;
            m_pending_demo_seek_ms.store(target_ms,
                                         std::memory_order_release);
            m_pending_demo_seek_master.store(master_clock,
                                             std::memory_order_release);
            m_pending_demo_seek_seq.store(seq_tag,
                                          std::memory_order_release);
            m_pending_demo_seek_round.store(round_tag,
                                            std::memory_order_release);
            m_pending_demo_seek_generation.store(
                generation, std::memory_order_release);
            if (!had_pending)
                m_pending_demo_seek_retry_ticks = 0;
            if (m_pending_demo_seek_retry_ticks <= 0)
                return service_pending_demo_goto_time_seek(true);
            return false;
        }

        bool service_pending_demo_goto_time_seek(bool log_wait) noexcept
        {
            const int32_t target_ms =
                m_pending_demo_seek_ms.load(std::memory_order_acquire);
            if (target_ms < 0) return false;
            if (!log_wait && m_pending_demo_seek_retry_ticks > 0)
            {
                --m_pending_demo_seek_retry_ticks;
                return false;
            }

            const int32_t master_clock =
                m_pending_demo_seek_master.load(std::memory_order_acquire);
            const int32_t seq_tag =
                m_pending_demo_seek_seq.load(std::memory_order_acquire);
            const int32_t round_tag =
                m_pending_demo_seek_round.load(std::memory_order_acquire);
            const uint32_t generation =
                m_pending_demo_seek_generation.load(
                    std::memory_order_acquire);
            const float target_seconds =
                static_cast<float>(target_ms) / 1000.0f;

            if (try_queue_demo_goto_time_seek_seconds(
                    target_seconds, master_clock, round_tag, seq_tag,
                    log_wait))
            {
                m_pending_demo_seek_ms.store(-1,
                                             std::memory_order_release);
                m_pending_demo_seek_master.store(-1,
                                                 std::memory_order_release);
                m_pending_demo_seek_seq.store(-1,
                                              std::memory_order_release);
                m_pending_demo_seek_round.store(-1,
                                                std::memory_order_release);
                m_pending_demo_seek_generation.store(
                    0, std::memory_order_release);
                m_pending_demo_seek_retry_ticks = 0;
                begin_native_demo_seek_settle(
                    target_ms, seq_tag, master_clock, generation);
                return true;
            }
            else
            {
                m_pending_demo_seek_retry_ticks =
                    kNativeDemoSeekRetryTicks;
                return false;
            }
        }

        bool try_queue_demo_goto_time_seek_seconds(float target_seconds,
                                                   int32_t master_clock,
                                                   int32_t round_tag,
                                                   int32_t seq_tag,
                                                   bool log_wait) noexcept
        {
            if (!kEnableLegacySeekDiagnostics) return true;
            if (target_seconds < 0.0f) return true;
            if (!m_demo_goto_time)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::FunctionUnresolved);
                if (kAllowUnobservedDemoGotoCVarFallback)
                {
                    if (try_queue_demo_goto_time_seek_cvar(
                            target_seconds, master_clock, round_tag,
                            seq_tag))
                    {
                        m_native.cvar_submitted = true;
                        m_native.direct_driver_available = false;
                        return true;
                    }
                }
                if (log_wait)
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] native DemoNetDriver seek pending: "
                        "direct function pointer unresolved; unobserved CVar "
                        "fallback is disabled for strict replay accuracy "
                        "(seq={} master_clock={} "
                        "target={:.3f}s rva=0x{:X} image_base=0x{:X})\n"),
                        seq_tag, master_clock, target_seconds,
                        kRVA_DemoGotoTimeInSeconds,
                        NativeBinding::imageBase());
                return false;
            }
            const ReplayScrubDiag::DemoDriverResolveReport resolve_report =
                ReplayScrubDiag::resolve_demo_net_driver_report(true);
            const ReplayScrubDiag::DemoNetDriverSnap before =
                resolve_report.snap;
            if (!before.readable)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::DriverUnresolved);
                m_native.direct_driver_available = false;
                m_native.driver_ptr = 0;
                if (log_wait)
                {
                    ReplayScrubDiag::log_demo_driver_resolve_report_once(
                        "SEEK_UNRESOLVED", resolve_report);
                    RC::Output::send<RC::LogLevel::Warning>(STR(
                        "[ReplayScrub] native DemoNetDriver seek pending: "
                        "direct driver unresolved "
                        "(seq={} master_clock={} target={:.3f}s "
                        "source={} attempts={})\n"),
                        seq_tag, master_clock, target_seconds,
                        RC::to_generic_string(
                            ReplayScrubDiag::demo_driver_source_name(
                                resolve_report.source)),
                        resolve_report.attempt_count);
                }
                if (kAllowUnobservedDemoGotoCVarFallback
                    && try_queue_demo_goto_time_seek_cvar(
                           target_seconds, master_clock, round_tag, seq_tag))
                {
                    m_native.cvar_submitted = true;
                    return true;
                }
                return false;
            }

            // Ghidra: UDemoNetDriver::GotoTimeInSeconds silently drops a
            // request when an FGotoTimeInSecondsTask is already queued
            // or driver+0x791 is set.  Do not call in that state: keep
            // the newest requested target pending and retry later.
            if (demo_driver_has_pending_goto(before))
            {
                publish_native_failure_reason(
                    NativeSeekFailure::DriverBusy);
                if (log_wait)
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[ReplayScrub] native DemoNetDriver seek deferred: "
                        "driver busy seq={} target={:.3f}s "
                        "[+791=0x{:02X} +794=0x{:02X} +7A8=0x{:X} "
                        "+7B0={} +7B4={} +7B8=0x{:X}]\n"),
                        seq_tag, target_seconds,
                        static_cast<unsigned>(before.raw_busy_791),
                        static_cast<unsigned>(before.raw_loading_794),
                        before.raw_task_data_7a8,
                        before.raw_task_count_7b0,
                        before.raw_task_max_7b4,
                        before.raw_current_task_7b8);
                return false;
            }

            if (!safe_call_demo_goto_time(
                    reinterpret_cast<void*>(before.driver_ptr),
                    target_seconds))
            {
                publish_native_failure_reason(
                    NativeSeekFailure::CallFaulted);
                return false;
            }
            m_native.direct_driver_available = true;
            m_native.driver_ptr = before.driver_ptr;

            ReplayScrubDiag::DemoNetDriverSnap after{};
            const bool after_readable =
                ReplayScrubDiag::read_demo_driver_raw(
                    reinterpret_cast<void*>(before.driver_ptr), after);
            const bool task_observed =
                after_readable
                && (after.raw_task_count_7b0 > before.raw_task_count_7b0
                    || after.raw_current_task_7b8
                        != before.raw_current_task_7b8
                    || after.raw_busy_791 != before.raw_busy_791);
            if (!task_observed)
            {
                publish_native_failure_reason(
                    NativeSeekFailure::TaskNotObserved);
                RC::Output::send<RC::LogLevel::Warning>(STR(
                    "[ReplayScrub] native DemoNetDriver seek call returned "
                    "but no FGotoTimeInSecondsTask was observed; retrying "
                    "later (seq={} master_clock={} target={:.3f}s "
                    "driver=0x{:X} before[+791=0x{:02X} +794=0x{:02X} "
                    "+7A8=0x{:X} +7B0={} +7B4={} +7B8=0x{:X}] "
                    "after_readable={} after[+791=0x{:02X} +794=0x{:02X} "
                    "+7A8=0x{:X} +7B0={} +7B4={} +7B8=0x{:X}])\n"),
                    seq_tag, master_clock, target_seconds, before.driver_ptr,
                    static_cast<unsigned>(before.raw_busy_791),
                    static_cast<unsigned>(before.raw_loading_794),
                    before.raw_task_data_7a8,
                    before.raw_task_count_7b0,
                    before.raw_task_max_7b4,
                    before.raw_current_task_7b8,
                    after_readable ? 1 : 0,
                    static_cast<unsigned>(after.raw_busy_791),
                    static_cast<unsigned>(after.raw_loading_794),
                    after.raw_task_data_7a8,
                        after.raw_task_count_7b0,
                        after.raw_task_max_7b4,
                        after.raw_current_task_7b8);
                if (kAllowUnobservedDemoGotoCVarFallback
                    && try_queue_demo_goto_time_seek_cvar(
                           target_seconds, master_clock, round_tag, seq_tag))
                {
                    m_native.cvar_submitted = true;
                    m_native.direct_driver_available = false;
                    return true;
                }
                return false;
            }

            m_native_demo_seek_guard_ticks.store(
                kNativeDemoSeekGuardTicks, std::memory_order_release);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] native DemoNetDriver::GotoTimeInSeconds "
                "submitted; "
                "seq={} master_clock={} target={:.3f}s "
                "driver=0x{:X} rawCur {:.3f}->{:.3f} "
                "raw+791 0x{:02X}->0x{:02X} "
                "raw+794 0x{:02X}->0x{:02X} "
                "taskData 0x{:X}->0x{:X} "
                "tasks {}->{} max {}->{} "
                "currentTask 0x{:X}->0x{:X}\n"),
                seq_tag, master_clock, target_seconds, after.driver_ptr,
                before.raw_demo_cur_time, after.raw_demo_cur_time,
                static_cast<unsigned>(before.raw_busy_791),
                static_cast<unsigned>(after.raw_busy_791),
                static_cast<unsigned>(before.raw_loading_794),
                static_cast<unsigned>(after.raw_loading_794),
                before.raw_task_data_7a8, after.raw_task_data_7a8,
                before.raw_task_count_7b0, after.raw_task_count_7b0,
                before.raw_task_max_7b4, after.raw_task_max_7b4,
                before.raw_current_task_7b8,
                after.raw_current_task_7b8);
            return true;
        }

        bool try_queue_demo_goto_time_seek_cvar(float target_seconds,
                                                int32_t master_clock,
                                                int32_t round_tag,
                                                int32_t seq_tag) noexcept
        {
            void* cvar =
                ScreenPercentageOverride::find_cvar(
                    L"demo.GotoTimeInSeconds");
            if (!cvar) return false;

            wchar_t value[32]{};
            if (swprintf_s(value, L"%.3f", target_seconds) <= 0)
                return false;

            if (!ScreenPercentageOverride::cvar_set(cvar, value))
                return false;

            m_native_demo_seek_guard_ticks.store(
                kNativeDemoSeekGuardTicks, std::memory_order_release);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayScrub] native demo.GotoTimeInSeconds CVar "
                "submitted; seq={} master_clock={} target={:.3f}s "
                "(direct DemoNetDriver pointer unresolved)\n"),
                seq_tag, master_clock, target_seconds);
            return true;
        }
    };

}  // namespace Horse
