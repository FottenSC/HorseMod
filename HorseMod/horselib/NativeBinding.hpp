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

    class NativeBinding
    {
    public:
        // RVAs verified against the current Steam build (Ghidra image base
        // 0x140000000).  Re-verify after any SC6 patch.
        static constexpr uintptr_t kGetBoneTransformForPoseRVA       = 0x462760;
        static constexpr uintptr_t kLuxSkeletalBoneIndexRemapRVA     = 0x898140;
        static constexpr uintptr_t kLuxBattleCharaSetStartPositionRVA = 0x301E60;

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

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[HorseMod.NativeBinding] image base 0x{:x}  "
                    "GetBoneTransformForPose -> 0x{:x}  "
                    "LuxSkeletalBoneIndex_Remap -> 0x{:x}  "
                    "LuxBattleChara_SetStartPosition -> 0x{:x}\n"),
                s_image_base,
                reinterpret_cast<uintptr_t>(s_get_bone_transform),
                reinterpret_cast<uintptr_t>(s_bone_index_remap),
                reinterpret_cast<uintptr_t>(s_set_start_position));
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
