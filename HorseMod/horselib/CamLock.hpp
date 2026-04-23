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
    class CamLock
    {
    public:
        // ----------------------------------------------------------------
        // Sig-scan both injection sites and prepare() each store-NOP
        // patch.  Idempotent.  Returns true iff every site resolved AND
        // every patch prepared (we treat a partial resolve as failure
        // because a half-applied lock is a worse user experience than
        // a missing one — they'd see drifting rotation with frozen
        // location and not know why).
        //
        // Safe to call before SC6 has a battle loaded; the AOBs live in
        // .text which is always present.
        // ----------------------------------------------------------------
        bool resolve()
        {
            if (m_resolved) return m_resolved_ok;
            m_resolved = true;
            m_resolved_ok = false;

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

            // Build a 16-byte NOP buffer once; the largest store we patch
            // is 8 bytes so 16 is always enough.  Using a single static
            // buffer keeps prepare() trivially copyable.
            static constexpr uint8_t kNop16[16] = {
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            };

            size_t patch_idx = 0;
            for (void* site : { siteA, siteB })
            {
                auto* base = static_cast<uint8_t*>(site);
                for (auto [off, n] : kStores)
                {
                    if (!m_patches[patch_idx++].prepare(base + off, kNop16, n))
                    {
                        RC::Output::send<RC::LogLevel::Error>(
                            STR("[Horse.CamLock] prepare() failed at "
                                "0x{:x}+0x{:x}\n"),
                            reinterpret_cast<uintptr_t>(base), off);
                        return false;
                    }
                }
            }

            m_resolved_ok = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] resolved 10 store-NOP patches across "
                    "2 sites\n"));
            return true;
        }

        // ----------------------------------------------------------------
        // Apply every prepared patch.  Rolls back already-applied
        // patches on the first failure so we don't leave the camera
        // half-locked (which would look like jittering rotation with
        // frozen position — extra-confusing).
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

            for (size_t i = 0; i < m_patches.size(); ++i)
            {
                if (!m_patches[i].enable())
                {
                    // Roll back any patches already enabled this call.
                    for (size_t j = 0; j < i; ++j) m_patches[j].disable();
                    RC::Output::send<RC::LogLevel::Error>(
                        STR("[Horse.CamLock] enable() failed at patch {} "
                            "— rolled back\n"), i);
                    return false;
                }
            }
            m_enabled = true;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] camera frozen\n"));
            return true;
        }

        // Restore every patch.  Best-effort: keeps going even if one
        // fails so we maximise the chance of leaving the image in a
        // recoverable state.
        void disable()
        {
            if (!m_enabled) return;
            for (auto& p : m_patches) p.disable();
            m_enabled = false;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[Horse.CamLock] camera released\n"));
        }

        // Convenience for ImGui callbacks: "make state match this bool".
        // Resolves on first ON if not already resolved.
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
        std::array<BytePatch, 10> m_patches{};
        bool m_resolved    = false; // resolve() has been attempted
        bool m_resolved_ok = false; // resolve() succeeded
        bool m_enabled     = false;
    };

} // namespace Horse
