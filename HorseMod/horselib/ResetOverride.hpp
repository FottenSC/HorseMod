// ============================================================================
// Horse::ResetOverride — overwrite character position + facing on training-
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
// overwrites each chara's position + side-facing flag AFTER the game's
// own reset has run — leaving the chara at the user-chosen pose.
//
// Capture / Override semantics
// ----------------------------
//   capture_both():   read each ALuxBattleChara's current world pose into
//                     two FCharaPose snapshots (per player slot).  Marks
//                     m_pose[i].has = true.  No-op if a chara slot is
//                     unavailable (e.g. before a match starts).
//
//   apply_to_charas(): write our captured poses back to each chara's
//                     position + facing fields.  Called by the post-hook
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
//   +0x23C  side flag (byte, 0=P1 side / 1=P2 side; controls facing
//                       direction in PositionCharasSymmetrically)
//   +0x2090 render-pose X
//   +0x2094 render-pose Y + DAT_143e8a33c
//   +0x2098 render-pose Z
//
// We write to the 0xa0 / 0xc0 / 0x2090 groups (the canonical position
// triples that LuxBattleChara_SetStartPosition itself updates), zero the
// velocity at +0x90/+0x94/+0x98, and update the side byte at +0x23C.
//
// What we DON'T touch (and why)
// ----------------------------
//   +0x22c / +0x230  these are derived from +0x94 and a constant.  The
//                    engine recomputes them itself on the very next tick
//                    once movement physics resume, so writing them here
//                    is redundant.
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
// SC6 represents per-chara facing as a single byte (P1 side vs P2 side)
// rather than a free yaw angle.  We capture that byte on capture_both()
// and write it back on apply, which lets the user "swap sides" by
// capturing while a chara is on the opposite side from its default.
// Captures from arbitrary mid-move yaws (e.g. mid-spin) are rounded to
// the nearest side flag — a free-yaw override would require touching
// the post-tick rotation writers (LuxBattleChara::SetCurrentRotation
// etc.) which are out of scope for v1.
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "SafeMemoryRead.hpp"
#include "KHitWalker.hpp"      // charaSlotFromGlobal()
#include "NativeBinding.hpp"   // LuxBattleChara_SetStartPosition wrapper

#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

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
            uint8_t side_flag{0};   // 0 = P1 side, 1 = P2 side
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
                        "{:.2f}) side={}\n"),
                    pi + 1, pose.pos_x, pose.pos_y, pose.pos_z,
                    static_cast<uint32_t>(pose.side_flag));
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
            RC::Output::send<RC::LogLevel::Default>(
                STR("[ResetOverride] post-hook fired (enabled={}, "
                    "p1.has={}, p2.has={}) — queuing apply for next "
                    "cockpit tick\n"),
                en ? 1 : 0,
                m_pose[0].has ? 1 : 0,
                m_pose[1].has ? 1 : 0);

            if (!en) return;

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

            std::lock_guard g(m_mutex);
            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                if (!m_pose[pi].has) continue;
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
                        "{:.2f}) side={} to chara at 0x{:X}\n"),
                    pi + 1,
                    m_pose[pi].pos_x, m_pose[pi].pos_y, m_pose[pi].pos_z,
                    static_cast<uint32_t>(m_pose[pi].side_flag),
                    reinterpret_cast<uintptr_t>(chara));
                write_chara_pose(chara, m_pose[pi]);
            }
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

        static constexpr std::ptrdiff_t kSideFlag = 0x23C;

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
            uint8_t side{};
            bool ok = true;
            ok &= SafeReadFloat(base + kCur_X, &x);
            ok &= SafeReadFloat(base + kCur_Y, &y);
            ok &= SafeReadFloat(base + kCur_Z, &z);
            ok &= SafeReadUInt8 (base + kSideFlag, &side);
            if (!ok) return false;

            out.pos_x     = x;
            out.pos_y     = y;
            out.pos_z     = z;
            out.side_flag = side;
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

            // Side / facing flag — independent of the position write.  The
            // engine's SetStartPosition does NOT touch +0x23C, so we always
            // do this ourselves regardless of which path wrote position.
            auto* base = reinterpret_cast<uint8_t*>(chara_void);
            *(base + kSideFlag) = p.side_flag;
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
    };
}
