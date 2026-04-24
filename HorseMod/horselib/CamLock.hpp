// ============================================================================
// Horse::CamLock — freeze the SC6 battle camera at its current pose by
// NOP-ing the engine's per-frame "commit camera POV to memory" stores.
//
// Origin
// ------
// Ported from a CheatEngine table by @somberness ("SC6nepafu.CT", the
// "Cam" cheat).  CE table approach summary:
//
//   Site A — primary: 9-byte AOB
//     F2 0F 11 87 10 04 00 00 F2
//   matches the first store of a sequence at SoulcaliburVI.exe+11EAB225:
//
//     +0x00 (8B) movsd  [rdi+0x410], xmm0   ; Location.X / Y      ← NOP
//     +0x08 (6B) movsd  xmm0, [rsp+0x3C]    ; load Z / pitch       LEAVE
//     +0x0E (8B) movsd  [rdi+0x41C], xmm0   ; Rotation.Pitch/Yaw  ← NOP
//     +0x16 (5B) movups xmm0, [rsp+0x48]    ; load tail FOV/etc.   LEAVE
//     +0x1B (6B) mov    [rdi+0x418], eax    ; Location.Z          ← NOP
//     +0x21 (4B) mov    eax, [rsp+0x44]                            LEAVE
//     +0x25 (6B) mov    [rdi+0x424], eax    ; Rotation.Roll       ← NOP
//     +0x2B (4B) mov    eax, [rsp+0x5C]                            LEAVE
//     +0x2F (7B) movups [rdi+0x428], xmm0   ; FOV + tail          ← NOP
//
// Site B — sibling: 9-byte AOB
//     F2 0F 11 83 10 04 00 00 F2
//   Same instruction shape but uses [rbx+...] instead of [rdi+...].  The
//   CE table NOPs both — empirically required, otherwise the camera
//   still moves on certain transitions (probably one is the ViewTarget
//   cache and the other is the LastFrame cache; either way patching both
//   is what the upstream cheat does and we mirror that).
//
//   Same 5-store / 4-load alternation at the same byte offsets as Site A,
//   so we reuse the offset table.
//
// Why this beats the previous UMG-tick approach
// ---------------------------------------------
// The earlier camera-lock attempt hooked CockpitBase_C::Update (a UMG
// widget tick) and tried to overwrite APlayerCameraManager::CameraCache
// .POV every frame.  Doesn't work — Slate widget tick fires AFTER the
// renderer has already consumed the POV in the same frame, so our writes
// are stale by the time they would matter.  Patching the assembly stops
// the ENGINE from writing the new POV in the first place; nothing
// downstream sees a fresh value to consume.
//
// Limits / known caveats
// ----------------------
//   * "Lock at current pose" semantics: toggle ON freezes whatever was
//     last committed; toggle OFF lets the engine resume writing.  No
//     fly-cam (yet) — that's a follow-up that needs us to capture rdi
//     at the injection point and expose the floats as ImGui sliders.
//   * Doesn't affect anything that draws via a separate scene-capture
//     component (rare in SC6) or anything whose camera path skips this
//     particular store sequence.  Cinematic cuts that re-call SetView
//     Target before our store sites are still seen — but the next frame
//     the patch is back to freezing.
//   * If SC6 patches and either AOB stops matching, the toggle becomes
//     a no-op and we log a warning at resolve() time.  Re-AOB and bump
//     the patterns below.
//
// Threading
// ---------
// resolve() / enable() / disable() are called from the ImGui callback
// (game thread).  See BytePatch.hpp for the threading argument.
// ============================================================================

#pragma once

#include "BytePatch.hpp"
#include "SigScan.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <array>
#include <cstdint>

namespace Horse
{
    // ================================================================
    // CamLock is split into TWO independently-controllable patch groups:
    //
    //   * m_primary_patches (10) — the two TickAndCommitPOV store rows
    //     at rdi/rbx +0x410..+0x428.  These are safe, well-understood,
    //     and let us freeze the camera with no known side effects.
    //     `enable()` / `disable()` control this group only.
    //
    //   * m_rotation_patches (7) — three extra writer functions:
    //       rotSite1: FUN_1420520f0  PerTickPOVUpdater — per Ghidra
    //                 decomp it's a *whole-pose* committer (Loc + Rot
    //                 + Roll written as one block), not a rotation-
    //                 only writer as its name suggests.  We NOP the
    //                 entire 29-byte store block, because leaving the
    //                 location writes alive stomps our Free-Fly
    //                 position writes every tick (the function reads
    //                 scratch at entry and writes back unconditionally).
    //       rotSite2: FUN_141f935b0  TargetFollowRotationWriter
    //                 (verified rotation-only by Ghidra decomp).
    //       rotSite3: FUN_141d27c80  SetPOV combined setter — ALSO a
    //                 whole-pose writer after further investigation.
    //                 Called per-tick by a camera-follow updater
    //                 (FUN_141d5bb90) that computes target-relative
    //                 Loc+Rot, so both location and rotation stores
    //                 stomp us if left alive.  NOPing all 4 stores.
    //
    //     `enable_rotation_patches()` / `disable_rotation_patches()`
    //     control this group.  OFF by default; Free-Fly opts in when
    //     enabled so both location AND rotation survive end-to-end.
    // ================================================================
    class CamLock
    {
    public:
        // ----------------------------------------------------------------
        // Sig-scan both groups' injection sites and prepare() each
        // store-NOP patch.  Idempotent.  Returns true iff every site
        // resolved AND every patch prepared (we treat a partial resolve
        // as failure because a half-applied lock is a worse user
        // experience than a missing one — they'd see drifting rotation
        // with frozen location and not know why).
        //
        // Safe to call before SC6 has a battle loaded; the AOBs live in
        // .text which is always present.
        // ----------------------------------------------------------------
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved = true;
            m_resolved_ok = false;

            // Build a 16-byte NOP buffer once; the largest store we patch
            // is 8 bytes so 16 is always enough.  Using a single static
            // buffer keeps prepare() trivially copyable.
            static constexpr uint8_t kNop16[16] = {
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            };

            // ----- Primary store rows (the TickAndCommitPOV commit path) -----
            // Site A — primary store row at +rdi+0x410..+0x428.
            void* siteA = sig_scan_sc6(
                "F2 0F 11 87 10 04 00 00 F2", "CamLock siteA");
            // Site B — sibling row at +rbx+0x410..+0x428.
            void* siteB = sig_scan_sc6(
                "F2 0F 11 83 10 04 00 00 F2", "CamLock siteB");

            if (!siteA || !siteB) return false;

            // Same byte-offset / NOP-count table for both sites — the
            // two instruction sequences are identical except for the
            // base register.
            //
            // (offset_from_site, nop_count) pairs.
            constexpr std::array<std::pair<size_t, size_t>, 5> kStores = {{
                {0x00, 8}, // movsd  [base+0x410], xmm0
                {0x0E, 8}, // movsd  [base+0x41C], xmm0
                {0x1B, 6}, // mov    [base+0x418], eax
                {0x25, 6}, // mov    [base+0x424], eax
                {0x2F, 7}, // movups [base+0x428], xmm0
            }};

            size_t primary_idx = 0;
            for (void* site : { siteA, siteB })
            {
                auto* base = static_cast<uint8_t*>(site);
                for (auto [off, n] : kStores)
                {
                    if (!m_primary_patches[primary_idx++].prepare(
                            base + off, kNop16, n))
                    {
                        RC::Output::send<RC::LogLevel::Error>(
                            STR("[Horse.CamLock] prepare() failed at "
                                "primary 0x{:x}+0x{:x}\n"),
                            reinterpret_cast<uintptr_t>(base), off);
                        return false;
                    }
                }
            }

            // ----- Additional writers discovered in 2026-04 -----------------
            // Three extra functions write into the camera pose fields outside
            // the main TickAndCommitPOV commit row.  Before these were
            // patched, Free-Fly camera rotation appeared to update in our
            // state but the rendered view didn't change — because these
            // writers re-stomped the rotation every tick even while
            // CamLock was "on".
            //
            //   rotSite1 @ FUN_1420520f0 (PerTickPOVUpdater / look-input)
            //     Ghidra decomp (2026-04) revealed this function is NOT
            //     rotation-only — it's a *whole-pose* committer that
            //     writes Loc, Rot AND Roll as one block from registers
            //     loaded at the function entry:
            //
            //       0x142052271  F2 44 0F 11 83 10 04 00 00
            //                    MOVSD [rbx+0x410], XMM8        (LocXY, 9B)
            //       0x14205227A  F2 0F 11 B3 1C 04 00 00
            //                    MOVSD [rbx+0x41C], XMM6        (pitch+yaw, 8B)
            //       0x142052282  89 B3 18 04 00 00
            //                    MOV   [rbx+0x418], ESI          (LocZ, 6B)
            //       0x142052288  89 BB 24 04 00 00
            //                    MOV   [rbx+0x424], EDI          (roll, 6B)
            //
            //     29 bytes of consecutive stores.  We NOP the whole block
            //     so neither rotation NOR position can be stomped by this
            //     function.  This is what makes Free-Fly work end-to-end
            //     with rotation lock on.
            //
            //   rotSite2 @ FUN_141f935b0 (TargetFollowRotationWriter)
            //       0x00 + 8  movsd [rbx+0x41C], xmm0   ← NOP (pitch+yaw)
            //       0x0B + 6  mov   [rbx+0x424], eax    ← NOP (roll)
            //     Verified rotation-only; no +0x410/+0x418 writes.
            //
            //   rotSite3 @ FUN_141d27c80 (SetPOV combined setter)
            //       0x00 + 8  movsd [rcx+0x41C], xmm0   ← NOP (pitch+yaw)
            //       0x0C + 6  mov   [rcx+0x424], eax    ← NOP (roll)
            //     Leaf function; also writes +0x410/+0x418 but from a
            //     different parameter, so NOPing rotation-only here is
            //     safe (SetPOV's caller supplies target pose from game
            //     script, which we override downstream via our per-tick
            //     writes).
            //
            // These patches are PREPARED here but NOT applied by enable().
            // Callers that need rotation lock call
            // enable_rotation_patches() explicitly.  See class comment.
            //
            // Each AOB below is widened far enough to be unique in the
            // .text segment so sig_scan_sc6 returns a single match.

            // -- rotSite1: 29-byte whole-pose commit in FUN_1420520f0.  AOB
            //    is the full 29-byte store sequence starting at LocXY.
            //    One patch, 29 NOPs.
            void* rotSite1 = sig_scan_sc6(
                "F2 44 0F 11 83 10 04 00 00 "  // movsd [rbx+0x410], xmm8
                "F2 0F 11 B3 1C 04 00 00 "     // movsd [rbx+0x41C], xmm6
                "89 B3 18 04 00 00 "           // mov   [rbx+0x418], esi
                "89 BB 24 04",                 // mov   [rbx+0x424], edi (trunc)
                "CamLock rotSite1 (PerTickPOVUpdater)");
            if (!rotSite1) return false;
            // 29 NOPs — larger than our static kNop16 buffer, so use a
            // local 32-byte buffer populated at resolve time.
            static constexpr uint8_t kNop32[32] = {
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            };
            if (!m_rotation_patches[0].prepare(rotSite1, kNop32, 29))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[Horse.CamLock] prepare() failed at "
                        "rotSite1 0x{:x} (full pose commit)\n"),
                    reinterpret_cast<uintptr_t>(rotSite1));
                return false;
            }

            // -- rotSite2: rotation-only (2 stores).
            void* rotSite2 = sig_scan_sc6(
                "F2 0F 11 83 1C 04 00 00 8B 40 08 89 83 24 04",
                "CamLock rotSite2 (TargetFollowRot)");
            if (!rotSite2) return false;
            {
                auto* base = static_cast<uint8_t*>(rotSite2);
                // Pitch+yaw at offset 0 (8 bytes).
                if (!m_rotation_patches[1].prepare(base, kNop16, 8))
                {
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.CamLock] prepare() failed at "
                            "rot-site 0x{:x} (pitch+yaw)\n"),
                        reinterpret_cast<uintptr_t>(base));
                    return false;
                }
                // Roll at offset 0x0B (6 bytes).
                if (!m_rotation_patches[2].prepare(base + 0x0B, kNop16, 6))
                {
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.CamLock] prepare() failed at "
                            "rot-site 0x{:x}+0xB (roll)\n"),
                        reinterpret_cast<uintptr_t>(base));
                    return false;
                }
            }

            // -- rotSite3: FULL POSE in LuxBattleCamera_SetPOV_LocRotCombined
            //    (0x141d27c80).  Ghidra decomp revealed this setter is called
            //    from FUN_141d5bb90 every tick (a per-tick camera-follow
            //    updater), passing target-derived Loc + Rot as params.
            //    Since SetPOV writes both Loc AND Rot from its params,
            //    leaving ONLY the rotation writes NOPed lets its location
            //    writes stomp our Free-Fly position every tick — which
            //    manifests as "UI position updates but camera stays
            //    frozen in game".  NOPing all 4 stores fixes it.
            //
            //    Function layout (function start at 0x141d27c80):
            //      +0x07  F2 0F 11 81 10 04 00 00   MOVSD [rcx+0x410], XMM0  LocXY (8B)
            //      +0x0F  89 81 18 04 00 00          MOV   [rcx+0x418], EAX   LocZ  (6B)
            //      +0x1A  F2 0F 11 81 1C 04 00 00   MOVSD [rcx+0x41C], XMM0  P+Y   (8B)
            //      +0x26  89 81 24 04 00 00          MOV   [rcx+0x424], EAX   Roll  (6B)
            //
            //    Non-contiguous: xmm0/eax loads sit between the stores and
            //    MUST run (otherwise the subsequent stores write garbage
            //    registers).  So we NOP each store individually.  The AOB
            //    anchors on the pitch+yaw movsd for uniqueness; we compute
            //    the 3 other store offsets relative to it.
            void* rotSite3_anchor = sig_scan_sc6(
                "F2 0F 11 81 1C 04 00 00 41 8B 40 08 89 81 24 04",
                "CamLock rotSite3 (SetPOV anchor @ pitch+yaw)");
            if (!rotSite3_anchor) return false;
            {
                auto* anchor = static_cast<uint8_t*>(rotSite3_anchor);
                // The anchor IS the pitch+yaw store (offset 0x1A in the
                // function).  The other stores live at known offsets
                // *relative to the anchor*:
                //   -0x13  LocXY store (8B)
                //   -0x0B  LocZ  store (6B)
                //   +0x00  pitch+yaw store (8B)  <-- anchor
                //   +0x0C  roll store (6B)
                struct Store { std::ptrdiff_t off; size_t n; const char* tag; };
                const Store kSetPOVStores[4] = {
                    { -0x13, 8, "SetPOV LocXY"    },
                    { -0x0B, 6, "SetPOV LocZ"     },
                    {  0x00, 8, "SetPOV pitch+yaw"},
                    {  0x0C, 6, "SetPOV roll"     },
                };
                for (size_t i = 0; i < 4; ++i)
                {
                    const auto& s = kSetPOVStores[i];
                    if (!m_rotation_patches[3 + i].prepare(
                            anchor + s.off, kNop16, s.n))
                    {
                        RC::Output::send<RC::LogLevel::Error>(
                            STR("[Horse.CamLock] prepare() failed at "
                                "rotSite3 store (i={})\n"), i);
                        return false;
                    }
                }
            }

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] resolved 17 store-NOP patches across "
                    "5 sites (10 primary + 7 extra: 1 whole-pose at "
                    "FUN_1420520f0, 2 rotation-only at rotSite2, 4 full-"
                    "pose stores at rotSite3/SetPOV).  Rotation group is "
                    "opt-in and not applied by enable()\n"));
            return true;
        }

        // ----------------------------------------------------------------
        // Apply the 10 primary patches.  Rolls back already-applied
        // patches on the first failure so we don't leave the camera
        // half-locked (which would look like jittering rotation with
        // frozen position — extra-confusing).
        //
        // Does NOT enable the 3 rotation-only sites; call
        // enable_rotation_patches() separately for that.
        // ----------------------------------------------------------------
        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.CamLock] enable() before successful "
                        "resolve() — ignoring\n"));
                return false;
            }
            if (m_enabled) return true;

            for (size_t i = 0; i < m_primary_patches.size(); ++i)
            {
                if (!m_primary_patches[i].enable())
                {
                    // Roll back any patches already enabled this call.
                    for (size_t j = 0; j < i; ++j)
                        m_primary_patches[j].disable();
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.CamLock] enable() failed at primary "
                            "patch {} — rolled back\n"), i);
                    return false;
                }
            }
            m_enabled = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] camera position frozen "
                    "(rotation={})\n"),
                m_rotation_enabled ? 1 : 0);
            return true;
        }

        // Restore every primary patch.  Best-effort: keeps going even
        // if one fails so we maximise the chance of leaving the image
        // in a recoverable state.  Does NOT touch the rotation group;
        // callers that own a rotation enable() must call
        // disable_rotation_patches() themselves.
        void disable()
        {
            if (!m_enabled) return;
            for (auto& p : m_primary_patches) p.disable();
            m_enabled = false;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] camera position released\n"));
        }

        // ----------------------------------------------------------------
        // Rotation-group enable/disable.  Opt-in because this group
        // has been observed to freeze position writes too on some
        // installs (see class comment).  Safe to call independently of
        // enable()/disable(): the groups don't share state.
        // ----------------------------------------------------------------
        bool enable_rotation_patches()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.CamLock] enable_rotation_patches() before "
                        "successful resolve() — ignoring\n"));
                return false;
            }
            if (m_rotation_enabled) return true;

            for (size_t i = 0; i < m_rotation_patches.size(); ++i)
            {
                if (!m_rotation_patches[i].enable())
                {
                    for (size_t j = 0; j < i; ++j)
                        m_rotation_patches[j].disable();
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.CamLock] enable_rotation_patches() "
                            "failed at rot patch {} — rolled back\n"), i);
                    return false;
                }
            }
            m_rotation_enabled = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] rotation writers NOPed\n"));
            return true;
        }

        void disable_rotation_patches()
        {
            if (!m_rotation_enabled) return;
            for (auto& p : m_rotation_patches) p.disable();
            m_rotation_enabled = false;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] rotation writers restored\n"));
        }

        // Convenience for ImGui callbacks: "make primary-group state
        // match this bool".  Resolves on first ON if not already
        // resolved.  Does NOT toggle the rotation group — that's a
        // separate UI checkbox.
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

        // Parallel convenience for the rotation group.
        void set_rotation_patches(bool on)
        {
            if (on)
            {
                if (!m_resolved) resolve();
                enable_rotation_patches();
            }
            else
            {
                disable_rotation_patches();
            }
        }

        bool is_enabled()           const { return m_enabled; }
        bool is_rotation_enabled()  const { return m_rotation_enabled; }
        bool is_resolved()          const { return m_resolved_ok; }

    private:
        // 10 patches for the two primary TickAndCommitPOV rows (5 per
        // site × 2 sites).  Enabled by enable() / disable().
        std::array<BytePatch, 10> m_primary_patches{};
        // 7 patches for the three extra writer sites:
        //   [0]     rotSite1 — 29-byte whole-pose commit in FUN_1420520f0
        //                     (LuxBattleCamera_TickAndCommitFullPose)
        //   [1, 2]  rotSite2 — pitch+yaw and roll in FUN_141f935b0
        //                     (LuxBattleCamera_WriteTargetFollowRotation)
        //   [3..6]  rotSite3 — full pose (LocXY, LocZ, pitch+yaw, roll)
        //                     in FUN_141d27c80 (LuxBattleCamera_SetPOV_*)
        //                     Needed because SetPOV is called every
        //                     tick from a camera-follow updater that
        //                     otherwise stomps our Free-Fly position.
        // Enabled separately by enable_rotation_patches() /
        // disable_rotation_patches().
        std::array<BytePatch, 7>  m_rotation_patches{};

        bool m_resolved         = false; // resolve() has been attempted
        bool m_resolved_ok      = false; // resolve() succeeded
        bool m_enabled          = false; // primary group active
        bool m_rotation_enabled = false; // rotation group active
    };

} // namespace Horse
