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
        // hook.  Walks both chara slots and overwrites their pose with
        // our captured values.  Per-player no-op if pose.has is false
        // (so partial captures don't clobber the un-captured player).
        //
        // Safe to call when no match is active — charaSlotFromGlobal
        // returns null in that case and we early-out.
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
                    "p1.has={}, p2.has={})\n"),
                en ? 1 : 0,
                m_pose[0].has ? 1 : 0,
                m_pose[1].has ? 1 : 0);

            if (!enabled()) return;

            std::lock_guard g(m_mutex);
            for (uint32_t pi = 0; pi < 2; ++pi)
            {
                if (!m_pose[pi].has) continue;
                void* chara = KHitWalker::charaSlotFromGlobal(pi);
                if (!chara) continue;
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
    };
}
