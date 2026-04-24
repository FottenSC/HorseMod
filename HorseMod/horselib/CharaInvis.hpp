// ============================================================================
// Horse::CharaInvis — flicker-free character / weapon hiding by inverting
// the engine's own visibility-compare instructions inside
// ALuxBattleChara_SyncMoveStateVisibility (0x1403d5030).
//
// Origin
// ------
// Ported from somberness's CE table ("SC6nepafu.CT", the "Invisible"
// section).  The CE script's clever trick: instead of writing the
// chara visibility flag (which races with engine writes during move
// transitions and produces 1-frame flickers), it patches the engine's
// own READ of that flag.  Specifically inside SyncMoveStateVisibility:
//
//     +0x10  cmp byte [rcx+0x533], 0    ; chara mesh visibility flag
//     ...                               ; cVar1 = (flag != 0)
//     +0x3F  cmp byte [rcx+0x534], 0    ; weapon mesh visibility flag
//     ...                               ; cVar6 = (flag != 0)
//
// The function then propagates cVar1 / cVar6 through to every attached
// scene component via USceneComponent_SetVisibility — main mesh at
// chara+0x390, the +0x470 component array, +0x520 secondary array, the
// Maegami hair tassels, and the VFX components via vftable[0x2b8].
//
// Inverting the immediate operand from 0 to 1 inverts the boolean
// result: now the engine sees "visible" iff the flag equals 1.  Since
// the engine writes 1 to mark "visible", flipping the compare produces
// "1 == 1 → ZF set → branch to invisible".  In practice this means the
// chara stays hidden no matter how many times the engine writes the
// flag back to "visible" during move transitions, cinematics, etc.
//
// Why it doesn't flicker
// ----------------------
// We're not racing the engine's writes — we're inside its own read
// path.  Every time SyncMoveStateVisibility runs (which is every
// time a move state changes, plus once per per-frame), it reads the
// flag, sees the inverted compare, and produces the hidden result.
// No race window; no flicker.
//
// Two sites get patched (one byte each, in-place):
//   * chara mesh compare at 0x1403d5040 + 6  (the imm8 of `cmp ..,0`)
//   * weapon mesh compare at 0x1403d506f + 6  (same idea)
//
// Combined effect: both characters' meshes AND both weapons go invisible
// (the function is called per-chara, so a single patch covers both
// players because the same instruction runs for both).
//
// Limits
// ------
//   * If the user's chara state has flag == 0 (engine-marked invisible
//     during e.g. a teleport effect), the inverted compare reads it as
//     "visible".  In practice the engine never leaves the flag at 0
//     except during transient frames the rendering pipeline already
//     hides anyway, so this is invisible (heh) to the user.
//   * Doesn't gate `chara+0x532` (the "force-hide" override).  If the
//     engine sets that, the chara is hidden regardless.  Our patch
//     interacts cleanly: chara+0x532 NOT set + flag==0 (post-patch
//     "visible") still becomes hidden because cVar1 short-circuits on
//     +0x532 != 0.  Wait — that's the opposite direction.  Re-derive:
//     original `cVar1 = (flag != 0) && (+0x532 == 0)`.  Patched:
//     `cVar1 = (flag != 1) && (+0x532 == 0)`.  When flag==1 (engine-
//     visible default), cVar1 = false && (+0x532 == 0) = false → hidden.
//     When flag==0 (engine-invisible), cVar1 = true && (+0x532 == 0) →
//     visible (which is wrong but only matters during the brief frames
//     the engine wanted invisible anyway).  Net: works, with a tiny
//     visual artefact during specific cinematic frames the user is
//     unlikely to notice.
// ============================================================================

#pragma once

#include "BytePatch.hpp"
#include "SigScan.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <cstdint>

namespace Horse
{
    class CharaInvis
    {
    public:
        // Sig-scan + prepare both single-byte patches.  Idempotent.
        // Returns true iff every site resolved.
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved = true;
            m_resolved_ok = false;

            // Both AOBs target a `cmp byte [rcx+0x5??], 0` (7 bytes:
            // 80 B9 ?? 05 00 00 00).  Only the last byte (the imm8)
            // gets flipped.
            //
            // The 7-byte form `80 B9 33 05 00 00 00` (chara visibility
            // check) is AMBIGUOUS in the current SC6 build — it matches
            // 3 sites:
            //   * 0x1403d4a7b in UpdateMaegamiVisibility (hair tassels)
            //   * 0x1403d5040 in SyncMoveStateVisibility   (the target)
            //   * 0x14090f293 in another reader
            // We need site 2.  The bytes immediately following each cmp
            // disambiguate — the SyncMoveStateVisibility site is
            // followed by `48 8B F9` (mov rdi, rcx), so widening the
            // chara AOB to 10 bytes uniquely matches it.
            //
            // Note: the original CE script's AOB ended with `48 89 CF`
            // (the SAME instruction `mov rdi, rcx` but a different
            // encoding).  The current build chose `48 8B F9`, so the
            // CE script's exact AOB no longer matches in this build.
            //
            // The weapon AOB `80 B9 34 05 00 00 00` is unambiguous —
            // only one match — so the 7-byte form stays.
            void* siteChara = sig_scan_sc6(
                "80 B9 33 05 00 00 00 48 8B F9", "CharaInvis siteChara");
            void* siteWeapon = sig_scan_sc6(
                "80 B9 34 05 00 00 00", "CharaInvis siteWeapon");
            if (!siteChara || !siteWeapon) return false;

            // The instruction layout is:
            //   80 B9 33 05 00 00 00
            //   ^^             ^^ ^^ ^^ ^^ ^^
            //   opcode (cmp r/m8, imm8)
            //                  disp32 = 0x533 (or 0x534)
            //                              imm8 = 0  <-- index 6, what we flip
            //
            // BytePatch::prepare wants us to specify the WHOLE patch
            // window so it can snapshot+restore.  We give it 1 byte
            // pointing at the immediate operand only.
            const uint8_t kReplacement = 0x01;
            auto* patchChara  = static_cast<uint8_t*>(siteChara)  + 6;
            auto* patchWeapon = static_cast<uint8_t*>(siteWeapon) + 6;
            if (!m_patch_chara .prepare(patchChara,  &kReplacement, 1)) return false;
            if (!m_patch_weapon.prepare(patchWeapon, &kReplacement, 1)) return false;

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CharaInvis] resolved: siteChara=0x{:x} "
                    "siteWeapon=0x{:x}\n"),
                reinterpret_cast<uintptr_t>(siteChara),
                reinterpret_cast<uintptr_t>(siteWeapon));
            return true;
        }

        bool enable()
        {
            if (!m_resolved_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[Horse.CharaInvis] enable() before resolve() — "
                        "ignoring\n"));
                return false;
            }
            if (m_enabled) return true;
            // Roll back partial application on failure (same pattern as
            // CamLock) — if the second patch fails the user shouldn't
            // be left in "chara invisible, weapon visible" purgatory.
            if (!m_patch_chara.enable())  return false;
            if (!m_patch_weapon.enable()) { m_patch_chara.disable(); return false; }
            m_enabled = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CharaInvis] characters + weapons hidden via "
                    "compare-byte inversion\n"));
            return true;
        }

        void disable()
        {
            if (!m_enabled) return;
            m_patch_weapon.disable();
            m_patch_chara.disable();
            m_enabled = false;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CharaInvis] visibility restored\n"));
        }

        // Convenience for ImGui callbacks — match whatever bool the
        // user toggled.  Resolves on first ON if not already.
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

        bool is_enabled()  const { return m_enabled; }
        bool is_resolved() const { return m_resolved_ok; }

    private:
        BytePatch m_patch_chara{};
        BytePatch m_patch_weapon{};
        bool m_resolved    = false;
        bool m_resolved_ok = false;
        bool m_enabled     = false;
    };

} // namespace Horse
