// ============================================================================
// Horse::NativeBinding — typed access to SoulCalibur VI native functions.
//
// Why this exists
// ---------------
// A UE4SS mod normally calls game behaviour via UFunction reflection
// (ProcessEvent).  That only works when the UFunction is registered on
// the class we're calling it on.  Two SC6 code paths we need here aren't
// reachable that way:
//
//   * ALuxBattleChara::GetBoneTransformForPose  @ image + 0x462760
//     Not registered as a UFunction at all — pure C++ method.
//   * LuxSkeletalBoneIndex_Remap                @ image + 0x898140
//     Private static helper; also not a UFunction.
//
// Together these two let us walk a KHitArea / FLuxCapsule entry, take the
// internal BoneId byte, remap it to a UE4 skeleton bone index, and resolve
// a 64-byte FTransform for that bone at a given pose — everything we need
// to render a bone-attached OBB or capsule in world space.
//
// Verification
// ------------
// resolve() is called once from on_unreal_init (game thread).  It reads
// the live image base of SoulcaliburVI.exe and computes raw function
// pointers from it.  No signature check — if SC6 ever ships a new build
// with different RVAs, we'll find out via a crash, not a silent miss.
// Rebuilding against a new RVA is a one-line edit in this file.
//
// Thread-safety
// -------------
// Reading the function pointers after resolve() is lock-free.  The
// underlying functions are themselves NOT thread-safe — call them only
// from the game thread (the cockpit update pre-hook is where we do this).
// ============================================================================

#pragma once

#include "HorseLib.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <cstdint>

namespace Horse
{
    // ------------------------------------------------------------------
    // Native function signatures.  Both take __fastcall by default on
    // MSVC x64 (no shadow-space hijinks).
    // ------------------------------------------------------------------

    // ALuxBattleChara_GetBoneTransformForPose does NOT return a UE4
    // FTransform (quat/vec/vec) despite its name — empirical and Ghidra
    // evidence confirm it returns a 64-byte FMatrix (4×4 affine, row-major):
    //
    //   +0x00  Row 0  (scaled X axis of rotation; +1 pad float)
    //   +0x10  Row 1  (scaled Y axis of rotation; +1 pad float)
    //   +0x20  Row 2  (scaled Z axis of rotation; +1 pad float)
    //   +0x30  Row 3  (Translation.XYZ; W = 1.0)
    //
    // Identity = 4×4 identity matrix.  The scale is baked into the row
    // magnitudes of the 3×3 rotation part (the game's own
    // FTransform_SplitIntoQuatVecVec @ 0x1403CCFD0 calls
    // Matrix3x3_ExtractScaleAndNormalize on rows 0-2 to recover it).
    //
    // World-space point lookup (row-vector convention, matches UE4 FMatrix):
    //   world.X = local.X * M[0][0] + local.Y * M[1][0] + local.Z * M[2][0]
    //           + M[3][0]
    //   world.Y = local.X * M[0][1] + local.Y * M[1][1] + local.Z * M[2][1]
    //           + M[3][1]
    //   world.Z = local.X * M[0][2] + local.Y * M[1][2] + local.Z * M[2][2]
    //           + M[3][2]
    //
    // The `g_LuxCmToUEScale` (10.0) factor still needs to be applied to the
    // KHit bone-local centre BEFORE the matrix multiply, because the
    // rotation part of M is extracted-scale ≈ 1.0 (the actor's skeletal
    // component scale) and does NOT include the cm→UE conversion.
    struct FMatrix64
    {
        float M[4][4];   // row-major; rows 0-2 rotation+scale, row 3 trans
    };
    static_assert(sizeof(FMatrix64) == 64, "FMatrix must be 64 bytes");

    // Backwards-compat alias — older headers referred to this as
    // FTransform64 before we realised it's actually an FMatrix.
    using FTransform64 = FMatrix64;

    // FMatrix* ALuxBattleChara_GetBoneTransformForPose(
    //     FMatrix*         OutMatrix,        // 64-byte buffer, caller-supplied
    //     UObject*         chara,            // AActor UObject* (heap) — MUST
    //                                        // be a real UObject, not the
    //                                        // static CharaSlot pointer, or
    //                                        // the function silently hits
    //                                        // its identity-fallback branch.
    //     uint32_t         PoseSelector,     // 0/1 = P1/P2 bank in practice
    //     uint32_t         BoneIndex);       // UE4 bone idx — REMAP FIRST
    using GetBoneTransformForPoseFn =
        FMatrix64*(__fastcall*)(FMatrix64* OutMatrix,
                                void* chara,
                                uint32_t PoseSelector,
                                uint32_t BoneIndex);

    // int32_t LuxSkeletalBoneIndex_Remap(uint8_t internalBoneId);
    //   Returns UE4 bone index on success, 0xFFFFFFFF on failure.
    using LuxSkeletalBoneIndex_RemapFn =
        int32_t(__fastcall*)(uint8_t internalBoneId);

    // void LuxBattleChara_SetStartPosition(void* chara, float x, float y,
    //                                      float z);
    //
    // Verified calling convention (Ghidra disasm @ 140301e60):
    //   RCX  = chara*       (1st arg slot, integer/pointer)
    //   XMM1 = x            (2nd arg slot, float)
    //   XMM2 = y            (3rd arg slot, float)
    //   XMM3 = z            (4th arg slot, float)
    //
    // Writes the supplied (x, y, z) into all THREE redundant copies of
    // the chara's position in the chara struct:
    //   +0xa0 / +0xc0 / +0x2090   X
    //   +0xa4 / +0xc4 / +0x2094   Y  (render-Y additionally adds the
    //                                  per-stage offset DAT_143e8a33c)
    //   +0xa8 / +0xc8 / +0x2098   Z
    // Also clears post-impulse bookkeeping fields and walks the chara's
    // sub-component linked list at +0x29130, zeroing per-node state.
    //
    // This is the canonical "teleport this chara" call and the same one
    // every engine-internal reset path uses (RoundIntroSetup,
    // PositionCharasSymmetrically, ResetBothCharaPositionsAndFacing,
    // AllocAndInitCharaSlot, ...).  By calling it ourselves we get the
    // EXACT side-effects the engine expects — no missing fields, no
    // half-updated state machines.
    using LuxBattleChara_SetStartPositionFn =
        void(__fastcall*)(void* chara, float x, float y, float z);

    // Generic "void(launcher, bool)" signature shared by all 5 BattleRule
    // setters on ULuxUIBattleLauncher.  Each writes the bool to
    // BattleRule.<RuleName> in the launcher's data-table cache.
    using LuxUIBattleLauncher_SetBoolModeFn =
        void(__fastcall*)(void* launcher, bool bEnable);

    // ULuxUIBattleLauncher::Start signature.  param2 is a struct holding
    // the start parameters (FUIBattleLauncherStartParam — opaque to us;
    // we just pass it through unchanged when forwarding the call).
    using LuxUIBattleLauncher_StartFn =
        void(__fastcall*)(void* launcher, void* InStartParam);

    // void UActorComponent::MarkRenderStateDirty(this)
    //
    // Sets bRenderStateDirty on the component (ComponentFlags @ +0x188,
    // bit 0x20) and queues an end-of-frame render-state recreate.  The
    // scene proxy is destroyed and rebuilt from the component's current
    // state before the next frame draws.
    //
    // Why HorseMod calls this directly: SC6's Shipping build stripped
    // ULineBatchComponent's TickComponent override, so the stock-UE4
    // lifetime sweep that normally fires this on entry expiry doesn't
    // run.  PersistentLineBatcher (UWorld+0x48) is also never auto-
    // Flushed, so without an explicit MarkRenderStateDirty after we
    // append, the proxy stays frozen at whatever snapshot was last
    // built — visible as a delay between when a hit lands and when its
    // wireframe first shows.  Calling this once per frame after our
    // appends forces the rebuild.
    using UActorComponent_MarkRenderStateDirtyFn =
        void(__fastcall*)(void* component);

    // void ULuxUIGamePresenceUtil::SetPresence(ELuxGamePresence InPresence) [static]
    //
    // Single-byte parameter — the ELuxGamePresence enum value for the
    // current scene.  Microsoft x64 ABI: the byte is passed in CL (low
    // byte of RCX), zero-extended.  Used by Horse::GameMode for the
    // "Auto disable online" gate to track the user's current scene.
    //
    // We hook this with a PolyHook x64Detour rather than a UE4SS
    // RegisterHook because:
    //   - LuxUpdateGamePresenceFromSceneData (the scene-transition
    //     driver) calls this DIRECTLY in C++ at 0x14064a191 — never
    //     going through ProcessEvent — so a UFunction-level hook
    //     (which only intercepts ProcessEvent-routed dispatch) would
    //     never fire for the actual user-facing scene changes.
    //   - The exec_ wrapper at 0x140cb8e9c does route through the
    //     ProcessEvent path, but it's only used when Blueprint code
    //     explicitly calls `SetPresence`, which doesn't happen in
    //     practice during normal play.
    // A PolyHook detour on the C++ symbol catches both call paths.
    using LuxUIGamePresenceUtil_SetPresenceFn =
        void(__fastcall*)(uint8_t InPresence);

    class NativeBinding
    {
    public:
        // RVAs verified against the current Steam build (Ghidra image base
        // 0x140000000).  Re-verify after any SC6 patch.
        static constexpr uintptr_t kGetBoneTransformForPoseRVA       = 0x462760;
        static constexpr uintptr_t kLuxSkeletalBoneIndexRemapRVA     = 0x898140;
        static constexpr uintptr_t kLuxBattleCharaSetStartPositionRVA = 0x301E60;

        // ULuxUIBattleLauncher::Start (the "kick off the configured match"
        // chokepoint) and the 5 BattleRule setters it reads from.  All
        // verified via Ghidra registrar-table walk + decompile of each
        // setter's body.  See horselib/LuxBattleLauncherStartHook.hpp
        // for the hook design that uses these.
        //
        // Each setter writes a bool to BattleRule.<X> in the launcher's
        // data-table cache (this+0x50); Start later reads that cache
        // and converts each row into pushed mission skills via the
        // registrar at 0x5F6D20.  Call signature for all 5 setters:
        //   void __fastcall(void* launcher, bool bEnable)
        // Start signature:
        //   void __fastcall(void* launcher, void* InStartParam)
        static constexpr uintptr_t kLuxUIBattleLauncher_StartRVA              = 0x5EEB50;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetSlipOutModeRVA     = 0x5ED550;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetEndlessModeRVA     = 0x5EC390;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetDamageUpModeRVA    = 0x5EC190;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetNoRingOutModeRVA   = 0x5ECC70;
        static constexpr uintptr_t kLuxUIBattleLauncher_SetBlowUpModeRVA      = 0x5EB7F0;

        // ULuxUIGamePresenceUtil::SetPresence — hook target for the
        // scene-presence tracker (Horse::GameMode).
        static constexpr uintptr_t kLuxUIGamePresenceUtil_SetPresenceRVA      = 0x64F590;

        // UActorComponent::MarkRenderStateDirty — called by the line-
        // batcher backend after appending so the proxy rebuilds in time
        // for the next frame.
        static constexpr uintptr_t kUActorComponent_MarkRenderStateDirtyRVA   = 0x1D4E910;

        // Resolve once.  Idempotent; subsequent calls are no-ops.
        static void resolve()
        {
            if (s_resolved) return;
            s_resolved = true;

            HMODULE mod = ::GetModuleHandleW(L"SoulcaliburVI.exe");
            if (!mod)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[HorseMod.NativeBinding] GetModuleHandleW(SoulcaliburVI.exe) "
                        "returned null — cannot resolve native RVAs\n"));
                return;
            }

            s_image_base = reinterpret_cast<uintptr_t>(mod);

            s_get_bone_transform =
                reinterpret_cast<GetBoneTransformForPoseFn>(
                    s_image_base + kGetBoneTransformForPoseRVA);

            s_bone_index_remap =
                reinterpret_cast<LuxSkeletalBoneIndex_RemapFn>(
                    s_image_base + kLuxSkeletalBoneIndexRemapRVA);

            s_set_start_position =
                reinterpret_cast<LuxBattleChara_SetStartPositionFn>(
                    s_image_base + kLuxBattleCharaSetStartPositionRVA);

            // Online-rules infrastructure — Start chokepoint + 5 setters.
            s_launcher_start =
                reinterpret_cast<LuxUIBattleLauncher_StartFn>(
                    s_image_base + kLuxUIBattleLauncher_StartRVA);
            s_set_slipout_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetSlipOutModeRVA);
            s_set_endless_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetEndlessModeRVA);
            s_set_damage_up_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetDamageUpModeRVA);
            s_set_no_ringout_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetNoRingOutModeRVA);
            s_set_blowup_mode =
                reinterpret_cast<LuxUIBattleLauncher_SetBoolModeFn>(
                    s_image_base + kLuxUIBattleLauncher_SetBlowUpModeRVA);

            // GameMode infrastructure — scene-presence hook target.
            s_set_presence =
                reinterpret_cast<LuxUIGamePresenceUtil_SetPresenceFn>(
                    s_image_base + kLuxUIGamePresenceUtil_SetPresenceRVA);

            s_mark_render_state_dirty =
                reinterpret_cast<UActorComponent_MarkRenderStateDirtyFn>(
                    s_image_base + kUActorComponent_MarkRenderStateDirtyRVA);

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[HorseMod.NativeBinding] image base 0x{:x}  "
                    "GetBoneTransformForPose -> 0x{:x}  "
                    "LuxSkeletalBoneIndex_Remap -> 0x{:x}  "
                    "LuxBattleChara_SetStartPosition -> 0x{:x}  "
                    "Launcher::Start -> 0x{:x}  "
                    "SetSlipOutMode -> 0x{:x}  SetEndlessMode -> 0x{:x}  "
                    "SetDamageUpMode -> 0x{:x}  SetNoRingOutMode -> 0x{:x}  "
                    "SetBlowUpMode -> 0x{:x}  "
                    "SetPresence -> 0x{:x}\n"),
                s_image_base,
                reinterpret_cast<uintptr_t>(s_get_bone_transform),
                reinterpret_cast<uintptr_t>(s_bone_index_remap),
                reinterpret_cast<uintptr_t>(s_set_start_position),
                reinterpret_cast<uintptr_t>(s_launcher_start),
                reinterpret_cast<uintptr_t>(s_set_slipout_mode),
                reinterpret_cast<uintptr_t>(s_set_endless_mode),
                reinterpret_cast<uintptr_t>(s_set_damage_up_mode),
                reinterpret_cast<uintptr_t>(s_set_no_ringout_mode),
                reinterpret_cast<uintptr_t>(s_set_blowup_mode),
                reinterpret_cast<uintptr_t>(s_set_presence));
        }

        // Typed wrappers — safer than raw fn ptr calls at the use site.
        static bool getBoneTransform(void* chara,
                                     uint32_t poseSelector,
                                     uint32_t ue4BoneIdx,
                                     FTransform64& out)
        {
            if (!s_get_bone_transform || !chara) return false;
            s_get_bone_transform(&out, chara, poseSelector, ue4BoneIdx);
            return true;
        }

        static int32_t remapBoneId(uint8_t internalBoneId)
        {
            if (!s_bone_index_remap) return -1;
            return s_bone_index_remap(internalBoneId);
        }

        // Calls the engine's own teleport helper.  This is the canonical
        // reset-position path used by RoundIntroSetup,
        // PositionCharasSymmetrically, ResetBothCharaPositionsAndFacing,
        // AllocAndInitCharaSlot, etc. — writing all three position triples
        // (+0xA0 / +0xC0 / +0x2090), zeroing post-impulse bookkeeping, and
        // walking the chara's sub-component list at +0x29130.
        //
        // Returns false if either the function pointer or the chara is null.
        // Does NOT set the side-flag at +0x23C — that's a separate write
        // (the engine maintains it via PositionCharasSymmetrically, not
        // SetStartPosition).
        static bool setCharaStartPosition(void* chara, float x, float y, float z)
        {
            if (!s_set_start_position || !chara) return false;
            s_set_start_position(chara, x, y, z);
            return true;
        }

        // ---- Online rules: launcher + setter wrappers --------------------
        // Each Set*Mode setter writes into the launcher's data-table cache;
        // when the launcher's Start() runs later, those values drive which
        // mission skills get pushed.  We call the setters from inside our
        // PolyHook detour on Start (see LuxBattleLauncherStartHook) right
        // before the original runs, so the data table contains our chosen
        // values when Start reads them.
        static void setSlipOutMode(void* launcher, bool bEnable)
        {
            if (s_set_slipout_mode && launcher)
                s_set_slipout_mode(launcher, bEnable);
        }
        static void setEndlessMode(void* launcher, bool bEnable)
        {
            if (s_set_endless_mode && launcher)
                s_set_endless_mode(launcher, bEnable);
        }
        static void setDamageUpMode(void* launcher, bool bEnable)
        {
            if (s_set_damage_up_mode && launcher)
                s_set_damage_up_mode(launcher, bEnable);
        }
        static void setNoRingOutMode(void* launcher, bool bEnable)
        {
            if (s_set_no_ringout_mode && launcher)
                s_set_no_ringout_mode(launcher, bEnable);
        }
        static void setBlowUpMode(void* launcher, bool bEnable)
        {
            if (s_set_blowup_mode && launcher)
                s_set_blowup_mode(launcher, bEnable);
        }
        static uintptr_t launcherStartAddress()
        {
            return reinterpret_cast<uintptr_t>(s_launcher_start);
        }
        static bool hasLauncherStart() { return s_launcher_start != nullptr; }

        // ULuxUIGamePresenceUtil::SetPresence — accessor for the
        // GameMode tracker's PolyHook detour.  The function pointer
        // itself is exposed both as a callable (s_set_presence) and
        // as a raw address (setPresenceAddress) since PolyHook needs
        // the latter as its hook target.
        static uintptr_t setPresenceAddress()
        {
            return reinterpret_cast<uintptr_t>(s_set_presence);
        }
        static bool hasSetPresence() { return s_set_presence != nullptr; }

        // Force a scene-proxy rebuild on the given component.  No-op
        // if the binding hasn't resolved or the pointer is null.
        static void markRenderStateDirty(void* component)
        {
            if (s_mark_render_state_dirty && component)
                s_mark_render_state_dirty(component);
        }

        static bool isReady()      { return s_get_bone_transform != nullptr
                                         && s_bone_index_remap   != nullptr; }
        static bool hasSetStartPosition() { return s_set_start_position != nullptr; }
        static uintptr_t imageBase() { return s_image_base; }

    private:
        static inline bool                          s_resolved           = false;
        static inline uintptr_t                     s_image_base         = 0;
        static inline GetBoneTransformForPoseFn     s_get_bone_transform = nullptr;
        static inline LuxSkeletalBoneIndex_RemapFn  s_bone_index_remap   = nullptr;
        static inline LuxBattleChara_SetStartPositionFn s_set_start_position = nullptr;

        // Online-rules infrastructure.
        static inline LuxUIBattleLauncher_StartFn        s_launcher_start       = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_slipout_mode     = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_endless_mode     = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_damage_up_mode   = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_no_ringout_mode  = nullptr;
        static inline LuxUIBattleLauncher_SetBoolModeFn  s_set_blowup_mode      = nullptr;

        // GameMode infrastructure — only the address is used (PolyHook's
        // hook target).  Storing as a typed pointer mostly for symmetry
        // with the other natives.
        static inline LuxUIGamePresenceUtil_SetPresenceFn s_set_presence        = nullptr;

        static inline UActorComponent_MarkRenderStateDirtyFn s_mark_render_state_dirty = nullptr;
    };

    // ------------------------------------------------------------------
    // Math helper — transform a local-space point by an FMatrix64
    // (4×4 row-major affine).
    //
    //   world.X = local.X * M[0][0] + local.Y * M[1][0] + local.Z * M[2][0]
    //           + M[3][0]
    //   world.Y = local.X * M[0][1] + local.Y * M[1][1] + local.Z * M[2][1]
    //           + M[3][1]
    //   world.Z = local.X * M[0][2] + local.Y * M[1][2] + local.Z * M[2][2]
    //           + M[3][2]
    //
    // Rows 0-2 carry both rotation AND scale (row magnitudes are the
    // component scales).  Row 3 is translation.  This is algebraically
    // identical to the quat-rotate path in ALuxTraceManager_
    // GetTracePosition_Impl (0x1408D0BB0) which extracts scale, normalises
    // the 3×3, converts to quat, then does quat.Rotate(local * scale) +
    // trans — just without the extract/normalise/convert overhead.
    // ------------------------------------------------------------------
    inline FVec3 TransformPoint(const FMatrix64& m, const FVec3& local)
    {
        const float rx = local.X * m.M[0][0] + local.Y * m.M[1][0]
                       + local.Z * m.M[2][0];
        const float ry = local.X * m.M[0][1] + local.Y * m.M[1][1]
                       + local.Z * m.M[2][1];
        const float rz = local.X * m.M[0][2] + local.Y * m.M[1][2]
                       + local.Z * m.M[2][2];

        // Apply translation (row 3).
        return FVec3{ rx + m.M[3][0], ry + m.M[3][1], rz + m.M[3][2] };
    }

} // namespace Horse
