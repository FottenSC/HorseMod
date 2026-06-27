// ============================================================================
// Horse::ResetOverride — overwrite character position on training-
// mode position reset.
//
// What this does
// --------------
// SC6's training mode has a "reset position" bind (default: Select on a
// pad) that snaps both characters back to round-start positions.  Internally
// the BattleManager UFunction TrainingModePositionReset() drives the reset
// chain: PositionCharasByRoundConfig -> PositionCharasSymmetrically ->
// LuxBattleChara_SetStartPosition for each chara.
//
// When this module is enabled and the user has captured a pose via
// capture_both(), our UFunction post-hook on TrainingModePositionReset
// overwrites each chara's position AFTER the game's own reset has run,
// leaving the chara at the user-chosen pose.
//
// Capture / Override semantics
// ----------------------------
//   capture_both():   read each ALuxBattleChara's current world pose into
//                     two FCharaPose snapshots (per player slot).  Marks
//                     m_pose[i].has = true.  No-op if a chara slot is
//                     unavailable (e.g. before a match starts).
//
//   apply_to_charas(): write our captured poses back to each chara's
//                     position fields.  Called by the post-hook
//                     when m_enabled is true and m_pose[i].has is true.
//                     Players whose pose hasn't been captured are left
//                     alone (no clobber of vanilla reset).
//
// Chara struct offsets (verified via Ghidra on
// LuxBattleChara_SetStartPosition @ 0x140301e60 and
// LuxBattle_PositionCharasSymmetrically @ 0x140302670 — see the plate
// comments on those addresses for the full breakdown):
//
//   +0x090  movement velocity Z   (cleared on reset to 0)
//   +0x094  movement velocity X   (re-derived from start pos on reset)
//   +0x098  movement velocity Y   (cleared on reset to 0)
//   +0x0A0  start-position X      (round-spawn target)
//   +0x0A4  start-position Y
//   +0x0A8  start-position Z
//   +0x0C0  current-position X    (game-thread pose)
//   +0x0C4  current-position Y
//   +0x0C8  current-position Z
//   +0x22C  facing-derived X      (= +0x094 + chara[+0x96554])
//   +0x23C  battle slot byte      (0=P1 slot, 1=P2 slot; maintained
//                                  by the engine, not a free facing flag)
//   +0x2090 render-pose X
//   +0x2094 render-pose Y + DAT_143e8a33c
//   +0x2098 render-pose Z
//
// We write to the 0xa0 / 0xc0 / 0x2090 groups (the canonical position
// triples that LuxBattleChara_SetStartPosition itself updates) and zero
// the velocity at +0x90/+0x94/+0x98.
//
// What we DON'T touch (and why)
// ----------------------------
//   +0x22c / +0x230  these are derived from +0x94 and a constant.  The
//                    engine recomputes them itself on the very next tick
//                    once movement physics resume, so writing them here
//                    is redundant.
//   +0x23c           runtime validation showed this is the battle slot byte
//                    for live ALuxBattleChara objects.  Do not write it from
//                    user-captured poses; corrupting it breaks per-slot VM /
//                    input selection.
//   +0x96554         per-chara facing offset constant — read by the
//                    engine, not written.
//
// Threading
// ---------
// All state lives in this singleton.  Reads and writes use a mutex
// because capture (called from UI thread on button press) and apply
// (called from game thread inside the UFunction post-hook) can race.
// The two operations are short and the contention is negligible.
//
// CRITICAL: tick() must NOT hold m_mutex across the call into the
// engine's SetStartPosition.  That path is hot-patched by
// SetStartPositionHook, whose helper re-enters this singleton via
// get_pose() to look up the captured pose for the chara it just
// matched against the player slot.  If tick() held the lock during
// the engine call, get_pose() would attempt a second acquisition of
// the same non-recursive std::mutex on the same thread — UB on the
// C++ standard, deadlock in practice on Win10+/MSVC where std::mutex
// is backed by SRWLock.  The symptom in UE4SS.log: a single
// "writing P1 pose ..." line followed by silence as the thread hangs
// inside helper -> get_pose's lock acquire (and the game freezes
// until the user kills the process).  tick() therefore snapshots
// m_pose[] under the lock, releases, then runs the per-slot writes
// against the snapshot — get_pose() inside the hooked engine call
// then locks fresh with no contention.
//
// Deferred-apply
// --------------
// apply_to_charas() does NOT write directly — it queues a "pending
// apply" counter that's drained by tick() from the cockpit pre-hook
// one frame later.  Why: when the reset is the first one after a
// character swap, the LuxBattleChara at the static slot has been
// torn down and is mid-rebuild.  Calling the engine's SetStartPosition
// helper on a half-built chara walks a not-yet-populated sub-component
// list at +0x29130 and raises an unhandled C++ exception
// (0xe06d7363) that brings down the process — see UE4SS.log
// 2026-05-08 20:25:04 incident.  By the next cockpit tick the
// world tick has finished rebuilding the chara, so the apply is
// safe.  As a belt-and-suspenders, tick() also validates that
// chara+0x29130 holds a non-null pointer before invoking the engine
// helper, and skips the slot otherwise.
//
// Note on facing override
// -----------------------
// This feature no longer attempts to persist facing.  Earlier builds treated
// chara+0x23C as a side/facing byte; runtime validation showed it is the
// battle slot byte.  A real facing override should target a separate,
// validated facing field instead of rewriting +0x23C.
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "SafeMemoryRead.hpp"
#include "KHitWalker.hpp"      // charaSlotFromGlobal()
#include "NativeBinding.hpp"   // LuxBattleChara_SetStartPosition wrapper

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace Horse
{
    class ResetOverride
    {
    public:
        // Per-player captured pose.  has=false until the user has clicked
        // "Capture current pose" while that player slot was occupied.
        struct FCharaPose
        {
            float   pos_x{0.0f};
            float   pos_y{0.0f};
            float   pos_z{0.0f};
            bool    has{false};
        };

        static ResetOverride& instance()
        {
            static ResetOverride s;
            return s;
        }

        // ---- Toggle -----------------------------------------------------

        bool enabled() const noexcept
        {
            return m_enabled.load(std::memory_order_relaxed);
        }
        void set_enabled(bool v) noexcept
        {
            m_enabled.store(v, std::memory_order_relaxed);
            if (!v)
                m_apply_delay.store(0, std::memory_order_release);
        }

        // ---- Capture ----------------------------------------------------

        // Snapshot both players' current pose.  Returns true if at
        // least one chara slot was found and captured.  Players whose
        // chara pointer is null (e.g. between matches) keep their
        // previous captured pose if any.
        bool capture_both()
        {
            std::lock_guard g(m_mutex);
            int captured = 0;
            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                void* chara = KHitWalker::charaSlotFromGlobal(pi);
                if (!chara) continue;

                FCharaPose pose{};
                if (!read_chara_pose(chara, pose)) continue;

                m_pose[pi] = pose;
                ++captured;

                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[ResetOverride] captured P{} pos=({:.2f}, {:.2f}, "
                        "{:.2f})\n"),
                    pi + 1, pose.pos_x, pose.pos_y, pose.pos_z);
            }
            return captured > 0;
        }

        // ---- Read / write captured state (for UI + persistence) --------

        FCharaPose get_pose(int player_idx) const
        {
            std::lock_guard g(m_mutex);
            if (player_idx < 0 || player_idx > 1) return FCharaPose{};
            return m_pose[player_idx];
        }
        void set_pose(int player_idx, const FCharaPose& p)
        {
            std::lock_guard g(m_mutex);
            if (player_idx < 0 || player_idx > 1) return;
            m_pose[player_idx] = p;
        }

        // Drop the captured pose for both players (if the user wants to
        // reset to vanilla behaviour without disabling the toggle).
        void clear_captured()
        {
            std::lock_guard g(m_mutex);
            m_pose[0] = FCharaPose{};
            m_pose[1] = FCharaPose{};
        }

        // ---- Apply ------------------------------------------------------

        // Called from the BattleManager:TrainingModePositionReset post-
        // hook.  Does NOT write directly — queues a deferred apply that
        // tick() drains from the cockpit pre-hook one frame later.  See
        // the "Deferred-apply" plate at the top of the file for why.
        //
        // Safe to call when no match is active or no pose is captured;
        // tick() checks both before doing anything.
        void apply_to_charas()
        {
            // Log unconditionally so we can see in UE4SS.log whether
            // the hook is firing at all when the user presses their
            // reset bind.  If we never see this line, the bind is
            // not invoking TrainingModePositionReset and we're
            // hooking the wrong function.
            const bool en = enabled();
            bool p1_has = false;
            bool p2_has = false;
            {
                std::lock_guard g(m_mutex);
                p1_has = m_pose[0].has;
                p2_has = m_pose[1].has;
            }

            if (!en)
            {
                m_apply_delay.store(0, std::memory_order_release);
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ResetOverride] post-hook ignored (enabled=0, "
                        "p1.has={}, p2.has={})\n"),
                    p1_has ? 1 : 0,
                    p2_has ? 1 : 0);
                return;
            }
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ResetOverride] post-hook fired (enabled={}, "
                    "p1.has={}, p2.has={}) — queuing apply for next "
                    "cockpit tick\n"),
                en ? 1 : 0,
                p1_has ? 1 : 0,
                p2_has ? 1 : 0);

            // Counter semantics: 0 = no pending apply, >0 = ticks left
            // before the queued write fires.  We arm with 2 so the
            // very first cockpit pre-hook on the same frame as the
            // post-hook decrements to 1 (no apply), and the cockpit
            // pre-hook of the FOLLOWING frame decrements to 0 (apply).
            // That gives the engine one full frame to finish any
            // chara reconstruction triggered by a character swap.
            //
            // Multiple post-hook fires in the same chain just rearm
            // the counter; the apply still runs once.
            m_apply_delay.store(2, std::memory_order_release);
        }

        void request_trail_clear() noexcept
        {
            m_trail_clear_requested.store(true, std::memory_order_release);
        }

        bool consume_trail_clear_request() noexcept
        {
            return m_trail_clear_requested.exchange(
                false, std::memory_order_acq_rel);
        }

        // Called once per cockpit pre-hook (game thread, UMG tick —
        // AFTER the world tick has run).  Drains the deferred-apply
        // counter set by apply_to_charas() and, when it elapses, runs
        // the actual chara writes with a per-slot validation gate
        // that skips half-built charas.
        //
        // Cheap fast path: if no apply is pending the counter is 0
        // and we return after a single relaxed load.
        void tick()
        {
            int d = m_apply_delay.load(std::memory_order_acquire);
            if (d == 0) return;
            if (d > 1)
            {
                m_apply_delay.store(d - 1, std::memory_order_release);
                return;
            }
            // d == 1: time to apply.
            m_apply_delay.store(0, std::memory_order_release);

            if (!enabled()) return;

            // Snapshot the captured poses under the lock, then drop it.
            // See the "Threading" plate at the top of this file: holding
            // m_mutex across write_chara_pose() would re-enter this same
            // mutex through SetStartPositionHook::helper -> get_pose()
            // (recursive lock on a non-recursive std::mutex = deadlock
            // on Win10+/MSVC SRWLock-backed std::mutex).
            FCharaPose snap[2];
            {
                std::lock_guard g(m_mutex);
                snap[0] = m_pose[0];
                snap[1] = m_pose[1];
            }

            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                if (!snap[pi].has) continue;
                void* chara = KHitWalker::charaSlotFromGlobal(pi);
                if (!chara) continue;

                // Validate: the chara's sub-component list head at
                // +0x29130 must be readable AND non-null.  The engine
                // helper SetStartPosition walks that list and fails
                // an internal invariant (raises 0xe06d7363) if it's
                // null or unmapped — which is exactly the post-swap
                // mid-rebuild state.  Skip rather than crash; the
                // user's next reset bind will retry once the chara
                // has finished initialising.
                void* subcomp = nullptr;
                const auto* subcomp_addr =
                    reinterpret_cast<const uint8_t*>(chara) + kSubcomp;
                if (!SafeReadPtr(subcomp_addr, &subcomp) || !subcomp)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[ResetOverride] skip P{} apply: chara=0x{:X} "
                            "sub-component list at +0x{:X} is null or "
                            "unreadable (mid-init from character swap?)\n"),
                        pi + 1,
                        reinterpret_cast<uintptr_t>(chara),
                        static_cast<uint64_t>(kSubcomp));
                    continue;
                }

                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ResetOverride] writing P{} pose ({:.2f}, {:.2f}, "
                        "{:.2f}) to chara at 0x{:X}\n"),
                    pi + 1,
                    snap[pi].pos_x, snap[pi].pos_y, snap[pi].pos_z,
                    reinterpret_cast<uintptr_t>(chara));
                write_chara_pose(chara, snap[pi]);
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[ResetOverride] P{} write OK\n"), pi + 1);
            }
        }

        // ---- Clipboard JSON serialisation (Labbing tab Copy/Paste) ------
        //
        // Compact one-line JSON, e.g.
        //   {"v":1,"p1":{"x":1.95,"y":0.93,"z":-5.09},
        //          "p2":{"x":3.08,"y":1.09,"z":-3.98}}
        //
        // Slots whose has-flag is false are omitted from the output and
        // accepted as "absent" on input (paste of a P1-only snapshot
        // leaves the existing P2 capture untouched).  The version field
        // is mandatory on input — bumping it lets us break the shape
        // later without silently mis-importing old payloads.
        //
        // Validation on parse:
        //   - well-formed shape (the keys we look for are present)
        //   - "v" == 1
        //   - every present player has x / y / z (finite floats)
        //   - at least one of p1 / p2 is present
        // Legacy payloads may include a "side" key; it is ignored because
        // +0x23C is the battle slot byte, not a user pose field.
        // Out-of-bounds / NaN / Inf values are rejected.  Position
        // legality (inside the stage, on the ground, etc.) is NOT
        // checked — pasted poses are trusted to be sane.

        static std::string poses_to_json()
        {
            FCharaPose snap[2];
            {
                std::lock_guard g(instance().m_mutex);
                snap[0] = instance().m_pose[0];
                snap[1] = instance().m_pose[1];
            }
            return build_json(snap[0], snap[1]);
        }

        // Replace m_pose[] from a JSON string.  Returns true on success.
        // On failure, fills `error_out` with a short reason and leaves
        // captured poses untouched.  An empty/missing player block is
        // not a failure — it just means "don't change that slot".
        static bool poses_from_json(std::string_view json,
                                    std::string& error_out)
        {
            FCharaPose p1{}, p2{};
            bool any_present = false;
            if (!parse_json(json, p1, p2, any_present, error_out))
                return false;
            if (!any_present)
            {
                error_out = "no p1/p2 block present";
                return false;
            }
            // Apply atomically — nothing else holds m_mutex during a
            // paste (UI thread only), so the lock is just for memory
            // ordering against the apply path.
            std::lock_guard g(instance().m_mutex);
            if (p1.has) instance().m_pose[0] = p1;
            if (p2.has) instance().m_pose[1] = p2;
            return true;
        }

    private:
        ResetOverride() = default;
        ~ResetOverride() = default;
        ResetOverride(const ResetOverride&)            = delete;
        ResetOverride& operator=(const ResetOverride&) = delete;

        // ---- Chara struct offsets ---------------------------------------
        //
        // Documented on LuxBattleChara_SetStartPosition (0x140301e60) and
        // LuxBattle_PositionCharasSymmetrically (0x140302670) plate
        // comments in the SC6 binary's Ghidra DB.

        static constexpr std::ptrdiff_t kVel_X    = 0x094;
        static constexpr std::ptrdiff_t kVel_Y    = 0x098;
        static constexpr std::ptrdiff_t kVel_Z    = 0x090;

        static constexpr std::ptrdiff_t kStart_X  = 0x0A0;
        static constexpr std::ptrdiff_t kStart_Y  = 0x0A4;
        static constexpr std::ptrdiff_t kStart_Z  = 0x0A8;

        static constexpr std::ptrdiff_t kCur_X    = 0x0C0;
        static constexpr std::ptrdiff_t kCur_Y    = 0x0C4;
        static constexpr std::ptrdiff_t kCur_Z    = 0x0C8;

        static constexpr std::ptrdiff_t kRender_X = 0x2090;
        static constexpr std::ptrdiff_t kRender_Y = 0x2094;
        static constexpr std::ptrdiff_t kRender_Z = 0x2098;

        // Sub-component linked-list head walked by the engine's
        // SetStartPosition helper.  Used as a "is this chara fully
        // constructed?" sentinel by tick().  Sourced from the plate
        // comment on LuxBattleChara_SetStartPosition (0x140301e60).
        static constexpr std::ptrdiff_t kSubcomp  = 0x29130;

        // ---- Read / write helpers ---------------------------------------

        static bool read_chara_pose(void* chara_void, FCharaPose& out)
        {
            auto* base = reinterpret_cast<uint8_t*>(chara_void);

            float x{}, y{}, z{};
            bool ok = true;
            ok &= SafeReadFloat(base + kCur_X, &x);
            ok &= SafeReadFloat(base + kCur_Y, &y);
            ok &= SafeReadFloat(base + kCur_Z, &z);
            if (!ok) return false;

            out.pos_x     = x;
            out.pos_y     = y;
            out.pos_z     = z;
            out.has       = true;
            return true;
        }

        static void write_chara_pose(void* chara_void, const FCharaPose& p)
        {
            // Prefer the engine's own teleport helper — it writes all three
            // position triples, zeros velocity, walks the sub-component
            // linked list at +0x29130, and applies the per-stage render-Y
            // offset (DAT_143e8a33c) which we can't replicate from outside.
            //
            // If for some reason the native helper isn't resolved (early-init
            // race, post-patch RVA mismatch), fall back to the direct memory
            // writes — strictly worse (no sub-component reset, no render-Y
            // offset) but better than no-op.
            const bool native_ok =
                NativeBinding::setCharaStartPosition(chara_void,
                                                     p.pos_x, p.pos_y, p.pos_z);

            if (!native_ok)
            {
                auto* base = reinterpret_cast<uint8_t*>(chara_void);

                // Zero the movement-velocity vector so the chara holds at
                // our pose for the first physics tick after the reset.
                *reinterpret_cast<float*>(base + kVel_X) = 0.0f;
                *reinterpret_cast<float*>(base + kVel_Y) = 0.0f;
                *reinterpret_cast<float*>(base + kVel_Z) = 0.0f;

                // Write all three position copies.
                *reinterpret_cast<float*>(base + kStart_X)  = p.pos_x;
                *reinterpret_cast<float*>(base + kStart_Y)  = p.pos_y;
                *reinterpret_cast<float*>(base + kStart_Z)  = p.pos_z;

                *reinterpret_cast<float*>(base + kCur_X)    = p.pos_x;
                *reinterpret_cast<float*>(base + kCur_Y)    = p.pos_y;
                *reinterpret_cast<float*>(base + kCur_Z)    = p.pos_z;

                *reinterpret_cast<float*>(base + kRender_X) = p.pos_x;
                *reinterpret_cast<float*>(base + kRender_Y) = p.pos_y;
                *reinterpret_cast<float*>(base + kRender_Z) = p.pos_z;
            }

            (void)chara_void;
        }

        // ---- JSON build helpers (called from poses_to_json) ----

        static std::string format_float(float v)
        {
            char buf[32];
            int n = std::snprintf(buf, sizeof(buf), "%.6g",
                                  static_cast<double>(v));
            if (n <= 0) return "0";
            return std::string(buf, static_cast<size_t>(n));
        }

        static void append_player(std::string& out,
                                  const char* key,
                                  const FCharaPose& p)
        {
            if (!p.has) return;
            out += ",\"";
            out += key;
            out += "\":{\"x\":";
            out += format_float(p.pos_x);
            out += ",\"y\":";
            out += format_float(p.pos_y);
            out += ",\"z\":";
            out += format_float(p.pos_z);
            out += '}';
        }

        static std::string build_json(const FCharaPose& p1,
                                      const FCharaPose& p2)
        {
            std::string out = "{\"v\":1";
            append_player(out, "p1", p1);
            append_player(out, "p2", p2);
            out += '}';
            return out;
        }

        // ---- JSON parse helpers (called from poses_from_json) ----

        // Find "<key>":{ ... } and return the inner contents (no braces).
        // Returns empty string_view if the block isn't present (not an
        // error — caller decides what's mandatory).
        static std::string_view find_object(std::string_view s,
                                            const char* key)
        {
            std::string needle = "\"";
            needle += key;
            needle += "\"";
            size_t pos = s.find(needle);
            if (pos == std::string_view::npos) return {};
            pos = s.find(':', pos + needle.size());
            if (pos == std::string_view::npos) return {};
            // Skip whitespace, find opening brace.
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'
                                       || s[pos] == ':'))
                ++pos;
            if (pos >= s.size() || s[pos] != '{') return {};
            const size_t open = pos;
            int depth = 0;
            for (size_t i = open; i < s.size(); ++i)
            {
                if (s[i] == '{') ++depth;
                else if (s[i] == '}')
                {
                    if (--depth == 0)
                        return s.substr(open + 1, i - open - 1);
                }
            }
            return {};
        }

        // Find "<key>":<number> in `s`.  On success writes the parsed
        // value to `out` and returns true.  Rejects NaN / Inf.
        static bool find_number(std::string_view s,
                                const char* key,
                                float& out)
        {
            std::string needle = "\"";
            needle += key;
            needle += "\"";
            size_t pos = s.find(needle);
            if (pos == std::string_view::npos) return false;
            pos = s.find(':', pos + needle.size());
            if (pos == std::string_view::npos) return false;
            ++pos;
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
                ++pos;
            // strtof needs a NUL-terminated buffer.
            std::string num(s.substr(pos));
            const char* begin = num.c_str();
            char* endp = nullptr;
            const float v = std::strtof(begin, &endp);
            if (endp == begin) return false;       // no digits
            if (!std::isfinite(v)) return false;
            out = v;
            return true;
        }

        static bool parse_player(std::string_view block, FCharaPose& out)
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!find_number(block, "x",    x))    return false;
            if (!find_number(block, "y",    y))    return false;
            if (!find_number(block, "z",    z))    return false;
            out.pos_x     = x;
            out.pos_y     = y;
            out.pos_z     = z;
            out.has       = true;
            return true;
        }

        static bool parse_json(std::string_view json,
                               FCharaPose& p1, FCharaPose& p2,
                               bool& any_present_out,
                               std::string& error_out)
        {
            // Trim leading whitespace; require '{' as first non-ws char.
            size_t i = 0;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t'
                                        || json[i] == '\r' || json[i] == '\n'))
                ++i;
            if (i >= json.size() || json[i] != '{')
            {
                error_out = "expected '{' at start";
                return false;
            }
            float ver_f = 0.0f;
            if (!find_number(json, "v", ver_f))
            {
                error_out = "missing version field 'v'";
                return false;
            }
            if (static_cast<int>(ver_f) != 1)
            {
                error_out = "unsupported version (expected 1)";
                return false;
            }
            const auto p1_block = find_object(json, "p1");
            const auto p2_block = find_object(json, "p2");
            if (!p1_block.empty())
            {
                if (!parse_player(p1_block, p1))
                {
                    error_out = "p1 block invalid (missing/bad x/y/z)";
                    return false;
                }
            }
            if (!p2_block.empty())
            {
                if (!parse_player(p2_block, p2))
                {
                    error_out = "p2 block invalid (missing/bad x/y/z)";
                    return false;
                }
            }
            any_present_out = p1.has || p2.has;
            return true;
        }

        std::atomic<bool>  m_enabled{false};
        mutable std::mutex m_mutex;
        FCharaPose         m_pose[2]{};

        // Deferred-apply counter.  See apply_to_charas() / tick() and the
        // "Deferred-apply" plate at the top of the file.  0 = no pending
        // apply; >0 = cockpit ticks remaining before the queued write
        // fires.  Atomic because apply_to_charas() runs on the world-tick
        // game thread (UFunction post-hook) and tick() runs on the
        // UMG-tick game thread (cockpit pre-hook).
        std::atomic<int>   m_apply_delay{0};

        // Coalesced request set by the native SetStartPosition hook when
        // a player chara teleport is about to happen.  The cockpit renderer
        // drains it after priming the line-batcher backends so old
        // PersistentLineBatcher trail entries are removed before the first
        // post-teleport boxes are drawn.
        std::atomic<bool>  m_trail_clear_requested{false};
    };
}
