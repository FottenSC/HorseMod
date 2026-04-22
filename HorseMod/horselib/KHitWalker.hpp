// ============================================================================
// Horse::KHitWalker — reads SC6's legacy KHit linked lists and emits
// world-space draw primitives for the ILineOverlay.
//
// Background
// ----------
// SC6 runs TWO parallel hit-volume pipelines (see Ghidra plate on
// LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0):
//
//   Pipeline 1 — Weapon-trail capsules (FLuxCapsule + ALuxTraceManager).
//                Only covers weapon attacks — sword, axe, whip.  Not kicks,
//                punches, hurtboxes, or body.
//   Pipeline 2 — Legacy Namco-port KHit linked lists.  Covers everything
//                else: attack boxes (kicks/punches/bodies), hurtboxes,
//                pushboxes, stage collision.
//
// This walker targets PIPELINE 2 — the legacy KHit data.  That's the
// source-of-truth for "hitboxes + hurtboxes" as asked for.
//
// Per-chara list heads (relative to ALuxBattleChara*, which is the same
// object as g_LuxBattle_CharaSlotP1 / P2):
//
//     +0x44478  KHitBase*  AttackListHead       (every attack node)
//     +0x44490  int32      AttackListCount
//     +0x44498  KHitBase*  HurtboxListHead      (every hurtbox node)
//     +0x444B0  int32      HurtboxListCount
//     +0x444B8  KHitBase*  BodyListHead         (pushbox / body volumes)
//     +0x444B4  int32      BodyListCount
//     +0x44048  KHitBase*  CurrentActiveAttackCell  (null unless attack hot)
//     +0x1c74   int32[22]  PerHurtboxReactionState (non-zero after a hit)
//
// KHitBase common header (0xA0 = 160 bytes total per node):
//     +0x00  void* Vtable
//     +0x08  u64   BoneBitFlag
//     +0x10  u32   Flags10                   (attack id / region id)
//     +0x14  u16   IsAreaFlag = 1
//     +0x16  u8    StreamTypeTag             (0=Sphere, 1=Area, 2=FixArea)
//     +0x17  u8    StreamSubIdOrBoneId       (internal bone id — REMAP)
//     +0x18  KHitBase* Next                  (list link, null-terminates)
//
// Subclass geometry:
//     KHitSphere  (tag 0): +0x30 center (vec3) + 1.0f  ;  radius vec4 @ +0x70
//     KHitArea    (tag 1): +0x30 center (vec3) + 1.0f  ;  +0x40 extents vec3 + 1.0f
//                          +0x50..+0x8F  rotation block (3 rows × 16 bytes)
//                          Bone-attached via the +0x17 bone id.
//     KHitFixArea (tag 2): +0x30/+0x40/+0x50 3x4 rot/scale rows (3x3 active,
//                          row W = 1.0f), translation @ +0x90.  World-space,
//                          no bone attachment.
//
// Pose selector
// -------------
// ALuxBattleChara_GetBoneTransformForPose takes a pose-bank selector.  In
// every sample call from GetTracePosition_Impl the selector equals the
// chara's player index (0 or 1).  We pass PlayerIndex through as the
// pose selector.
//
// Draw-geometry output
// --------------------
// The walker never draws directly — it yields draw primitives to a visitor
// callback so the caller decides colour / style / whether to draw at all.
// Three primitive flavours:
//
//   KHitDraw { kind = Box,    centre + 8 corners (world-space) }
//   KHitDraw { kind = Sphere, centre + radius (world-space) }
//
// All coordinates are in UE units, directly pluggable into LineBatcher.
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "NativeBinding.hpp"
#include "SafeMemoryRead.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <cmath>
#include <cstdint>

namespace Horse
{
    // ------------------------------------------------------------------
    // Per-chara offsets relative to ALuxBattleChara / g_LuxBattle_CharaSlot.
    // ------------------------------------------------------------------
    namespace ChaOffsets
    {
        constexpr uintptr_t CurrentActiveAttackCell = 0x44048;  // KHitBase*
        constexpr uintptr_t AttackListHead          = 0x44478;  // KHitBase*
        constexpr uintptr_t AttackListCount         = 0x44490;  // i32
        constexpr uintptr_t HurtboxListHead         = 0x44498;  // KHitBase*
        constexpr uintptr_t HurtboxListCount        = 0x444B0;  // i32
        constexpr uintptr_t BodyListHead            = 0x444B8;  // KHitBase*
        constexpr uintptr_t BodyListCount           = 0x444B4;  // i32
        constexpr uintptr_t PerHurtboxReactionState = 0x1C74;   // i32[22]
    }

    // ------------------------------------------------------------------
    // KHit node common header field offsets (same for all subclasses).
    // ------------------------------------------------------------------
    namespace KHitOffsets
    {
        constexpr uintptr_t Vtable            = 0x00;
        constexpr uintptr_t BoneBitFlag       = 0x08;
        constexpr uintptr_t Flags10           = 0x10;
        constexpr uintptr_t IsAreaFlag        = 0x14;  // u16
        constexpr uintptr_t StreamTypeTag     = 0x16;  // u8 (0/1/2)
        constexpr uintptr_t BoneIdByte        = 0x17;  // u8 internal bone id
        constexpr uintptr_t Next              = 0x18;

        // Each KHit node is 0x80 (128) bytes — verified empirically:
        //   node->next - node == 0x80 exactly in the scratch pool.
        //
        // Layout (Ghidra-confirmed via KHitSphere_UpdateWorldCenter @
        // 0x14030E1A0 and KHitArea_UpdateWorldCenters @ 0x14030E480):
        //
        //     +0x00  vtable
        //     +0x08  BoneBitFlag   (u64, 1<<boneId)
        //     +0x10  flags10       (u32)
        //     +0x14  IsAreaFlag=1  (u16)
        //     +0x16  StreamTypeTag (u8, 0=Sphere 1=Area 2=FixArea)
        //     +0x17  BoneId        (u8, pre-remap)
        //     +0x18  Next          (KHit*, null-terminates list)
        //     +0x20  nextDelta     (i64, 0x80 in practice)
        //     +0x30  LocalCenter   (vec3+1, bone-local)
        //     +0x40  Mirror/Extents(vec3+1, sphere mirrors or area half-extents)
        //     +0x50  WorldCenterCur  (vec3+1) — THIS frame, in Namco world
        //     +0x60  WorldCenterPrev (vec3+1) — previous frame
        //     +0x70  Radius (float) + aux floats
        //
        // The game does: prev <- cur; cur <- FMatrix*local using the chara's
        // skeletal-mesh pose matrix (itself in Namco battle-world space, NOT
        // UE4 world).  So both +0x50 and +0x60 are in *legacy Namco* world —
        // they look like UE4 world values in the hex dump only by coincidence
        // (Y-up, metres).  We don't read them; we build UE4 world positions
        // ourselves via GetBoneTransformForPose (Option B below).
        constexpr uintptr_t LocalCenter       = 0x30;  // vec3 bone-local
        constexpr uintptr_t MirrorOrExtents   = 0x40;  // vec3
        constexpr uintptr_t WorldCenterCur    = 0x50;  // vec3 Namco-world current
        constexpr uintptr_t WorldCenterPrev   = 0x60;  // vec3 Namco-world previous
        constexpr uintptr_t Radius            = 0x70;  // float (Lux units)

        // KHitSphere layout (stream_tag == 0):
        //   +0x30  bone-local center   (vec3 + pad)
        //   +0x50  CURRENT world-space center   (FMatrix * +0x30)
        //   +0x60  PREVIOUS world-space center  (last tick's +0x50)
        //   +0x70  radius (float)
        //   +0x7C  UE4 remapped bone index (u32)  ← Sphere-ONLY
        //
        // See KHitSphere_UpdateWorldCenter @ 0x14030E1A0:
        //   node[+0x50] = poseMatrixArray[node[+0x7C]] * node[+0x30]
        constexpr uintptr_t Sphere_UE4BoneIndex = 0x7C;  // u32

        // KHitArea layout (stream_tag == 1) — ENTIRELY DIFFERENT from Sphere:
        //   +0x30  bone-local P1 (one diagonal corner of the OBB)
        //   +0x40  bone-local P2 (other diagonal corner)
        //   +0x50  world P1 buf-A    (double-buffered w/ toggle @ 14470DEC4)
        //   +0x60  world P2 buf-A
        //   +0x70  world P1 buf-B
        //   +0x80  world P2 buf-B
        //   +0x90  UE4 remapped bone idx for P1 (u32)  ← attach P1
        //   +0x94  UE4 remapped bone idx for P2 (u32)  ← attach P2
        //
        // Each endpoint lives in a possibly-different bone's local frame.
        // The "box" is the axis-aligned bounding box of the two transformed
        // world points (capsule-ish swept volume when the bones differ).
        // See KHitArea_UpdateWorldCenters @ 0x14030E480.
        constexpr uintptr_t Area_LocalP1        = 0x30;
        constexpr uintptr_t Area_LocalP2        = 0x40;
        constexpr uintptr_t Area_UE4BoneIndexA  = 0x90;  // u32
        constexpr uintptr_t Area_UE4BoneIndexB  = 0x94;  // u32

        // Backwards-compat aliases (old names still referenced in comments).
        constexpr uintptr_t Area_Center         = 0x30;
        constexpr uintptr_t Area_Extents        = 0x40;
        constexpr uintptr_t Sphere_Center       = 0x30;
        constexpr uintptr_t Sphere_Radius       = 0x70;
        // Legacy alias — used to be the one-size-fits-all bone idx field
        // under the mistaken assumption both classes stored it at +0x7C.
        // Keep for callers that haven't migrated yet, but prefer the
        // subclass-specific names above.
        constexpr uintptr_t UE4BoneIndex        = Sphere_UE4BoneIndex;
    }

    // ------------------------------------------------------------------
    // Option B — the SC6-native way to convert a KHit local centre into UE4
    // world space, mirroring ALuxTraceManager_GetTracePosition_Impl
    // (0x1408D0BB0) which is the game's own capsule-to-world converter.
    //
    //   int ueBone = LuxSkeletalBoneIndex_Remap(node->BoneId);     // +0x17
    //   FTransform64 bone;
    //   ALuxBattleChara_GetBoneTransformForPose(&bone, chara, poseSelector,
    //                                           ueBone);
    //   FVector scaled  = node->LocalCenter * g_LuxCmToUEScale;     // ×10
    //   scaled *= bone.Scale;                                       // per-axis
    //   FVector world = bone.Translation
    //                 + Quat::Rotate(bone.Rot, scaled);
    //
    // The ×10 constant lives in the binary at 0x143E8A418 (symbol
    // g_LuxCmToUEScale).  The remaining ~×10 factor is baked into the
    // FTransform's Scale3D (the skeletal mesh component scale).
    //
    // Radius is scaled by the same g_LuxCmToUEScale (uniform approximation —
    // the exact game uses the max bone Scale component but spheres are
    // visually indistinguishable).
    //
    // Scale factor note
    // -----------------
    // The literal float at 0x143E8A418 is 10.0f (g_LuxCmToUEScale), but the
    // empirically-correct scale to reach UE world cm for *our* pipeline is
    // 100.  That's because the bone FMatrix we get back from
    // GetBoneTransformForPose has row-scale ≈ 1.0 (the actor's component
    // scale), NOT ≈ 10.  In the game's own capsule path the factor-of-10
    // shows up either baked into the skeletal component scale or applied
    // at a different layer (inside the physics trace call).  Measurements:
    //   - Body pushbox radius 0.26 × 100 = 26 UE-cm  (body-sized — matches)
    //   - Hurtbox local offset (0.03,0,0.12) × 100  → 12 UE-cm forward of
    //     pelvis (correct torso height on Kilik/Grøh).
    // ------------------------------------------------------------------
    constexpr float kLuxCmToUE = 100.0f;

    enum class KHitKind : uint8_t { Box = 0, Sphere = 1 };
    enum class KHitList : uint8_t { Attack = 0, Hurtbox = 1, Body = 2 };

    struct KHitDraw
    {
        KHitKind    kind;
        KHitList    list;
        bool        is_current_attack;   // matches CurrentActiveAttackCell
        bool        reaction_hot;        // hurtbox: PerHurtboxReactionState != 0
        uint32_t    flags10;             // +0x10 — attack/hurt id
        uint8_t     stream_tag;          // 0=sphere, 1=area, 2=fixarea
        uint8_t     bone_id_internal;    // raw BoneId byte pre-remap
        int         hurtbox_slot;        // 0..21 for hurtbox list, -1 otherwise

        // Box geometry: 8 world-space corners in standard AABB ordering:
        //   0: -x -y -z    1: +x -y -z    2: +x +y -z    3: -x +y -z
        //   4: -x -y +z    5: +x -y +z    6: +x +y +z    7: -x +y +z
        FVec3       corners[8];

        // Sphere geometry (kind == Sphere).
        FVec3       centre;
        float       radius;
    };

    class KHitWalker
    {
    public:
        // Global CharaSlot RVAs — these are the pointer *variables* whose
        // contents are the live ALuxBattleChara*.  Verified in Ghidra:
        //
        //   g_LuxBattle_CharaSlotP1  @  0x14470DE90  (RVA 0x470DE90)
        //   g_LuxBattle_CharaSlotP2  @  0x14470DE98  (RVA 0x470DE98)
        //
        // Usage from LuxBattle_InitCharaSlotForMove_FirstRound:
        //   lVar12 = (&g_LuxBattle_CharaSlotP1)[playerIdx];
        //   Lux_KHitChk_DeserializeLinkedList(..., lVar12 + 0x44478);
        //
        // We read these to sanity-check that the UObject* we walked from
        // BattleCharaArray is the same object — and can also use them as a
        // fallback chara source if the UObject path is wrong.
        static constexpr uintptr_t kCharaSlotP1RVA = 0x470DE90;
        static constexpr uintptr_t kCharaSlotP2RVA = 0x470DE98;

        // Read g_LuxBattle_CharaSlotP{1,2} from its global slot address.
        // Returns nullptr if NativeBinding isn't resolved or the read faults.
        static void* charaSlotFromGlobal(uint32_t playerIdx)
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return nullptr;
            const uintptr_t slot_addr = base
                + (playerIdx == 0 ? kCharaSlotP1RVA : kCharaSlotP2RVA);
            void* chara = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(slot_addr), &chara))
                return nullptr;
            return chara;
        }

        // Walk all three KHit lists on `chara` and yield draw primitives
        // to `visit(const KHitDraw&)`.
        //
        // `chara` is the UObject* (AActor) pulled from BattleCharaArray —
        // a heap-allocated ALuxBattleChara UObject.  SC6 uses TWO distinct
        // chara representations:
        //
        //   * The UObject (heap, this `chara` parameter): owns the UWorld
        //     context that GetBoneTransformForPose requires to find the
        //     skeletal pose provider via BattleManager.
        //   * The static slot at g_LuxBattle_CharaSlotP{1,2} (in-image .bss):
        //     holds the KHit linked-list heads at slot+0x44478/+0x44498/
        //     +0x444B8.  Not a UObject — has no UWorld outer chain.
        //
        // We use them for DIFFERENT calls:
        //   - List walking        -> slot (or UObject if slot unavailable)
        //   - GetBoneTransformForPose -> UObject (must be UObject-shaped)
        //
        // Earlier builds of this walker tried to route everything through
        // one pointer; GetBoneTransformForPose then silently took the
        // identity-fallback branch (see plate at 0x140462760) because the
        // slot address fails GetWorldContextFromObject.  That showed up in
        // the [HorseMod.Bone] diagnostic as Trans=(0,1,0) Rot=(1,0,0,0)
        // Scale=(0,0,1) — the DAT_1440712e0 identity constant.
        template <class Visit>
        static void forEachKHit(void* chara,
                                uint32_t poseSelector,
                                Visit&& visit)
        {
            if (!chara)
            {
                if (shouldLog()) RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[HorseMod.KHit] chara=null\n"));
                return;
            }
            if (!NativeBinding::isReady())
            {
                if (shouldLog()) RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[HorseMod.KHit] native-not-ready chara=0x{:x}\n"),
                    reinterpret_cast<uintptr_t>(chara));
                return;
            }

            // UObject chara — used for bone-transform lookups because
            // GetBoneTransformForPose walks the UWorld context.
            void* ue_chara = chara;

            // Slot chara — used for KHit list walking.  UObject+0x44478 has
            // been observed to contain garbage / unrelated pointers on this
            // build; the real lists live on the static CharaSlot.
            void* slot_chara = charaSlotFromGlobal(poseSelector);
            void* list_chara = chara;
            bool   used_slot = false;
            {
                auto sc = reinterpret_cast<uintptr_t>(slot_chara);
                if (sc >= 0x10000ULL && sc <= 0x00007fffffffffffULL)
                {
                    list_chara = slot_chara;
                    used_slot = (slot_chara != chara);
                }
            }

            auto* bytes = reinterpret_cast<uint8_t*>(list_chara);

            // Pre-read the "current hot attack" cell so we can mark
            // which attack entry is live this frame.
            void* active_cell = nullptr;
            SafeReadPtr(bytes + ChaOffsets::CurrentActiveAttackCell, &active_cell);

            // Pre-read the per-hurtbox reaction state table (22 × i32).
            int32_t reactions[22] = {};
            for (int i = 0; i < 22; ++i)
            {
                SafeReadInt32(bytes + ChaOffsets::PerHurtboxReactionState
                              + i * sizeof(int32_t),
                              &reactions[i]);
            }

            // Read list heads + counts up-front for diagnostics AND use.
            void* atk_head  = nullptr;
            void* hurt_head = nullptr;
            void* body_head = nullptr;
            int32_t atk_count  = 0;
            int32_t hurt_count = 0;
            int32_t body_count = 0;
            SafeReadPtr  (bytes + ChaOffsets::AttackListHead,    &atk_head);
            SafeReadPtr  (bytes + ChaOffsets::HurtboxListHead,   &hurt_head);
            SafeReadPtr  (bytes + ChaOffsets::BodyListHead,      &body_head);
            SafeReadInt32(bytes + ChaOffsets::AttackListCount,   &atk_count);
            SafeReadInt32(bytes + ChaOffsets::HurtboxListCount,  &hurt_count);
            SafeReadInt32(bytes + ChaOffsets::BodyListCount,     &body_count);

            const bool verbose = shouldLog();
            if (verbose)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[HorseMod.KHit] pi={} ue_chara=0x{:x} slot=0x{:x} "
                        "using_slot={} atk=0x{:x} hurt=0x{:x} body=0x{:x} "
                        "atkN={} hurtN={} bodyN={}\n"),
                    poseSelector,
                    reinterpret_cast<uintptr_t>(ue_chara),
                    reinterpret_cast<uintptr_t>(slot_chara),
                    used_slot ? 1 : 0,
                    reinterpret_cast<uintptr_t>(atk_head),
                    reinterpret_cast<uintptr_t>(hurt_head),
                    reinterpret_cast<uintptr_t>(body_head),
                    atk_count, hurt_count, body_count);

                // Hex-dump the 80-byte region bracketing the KHit fields
                // so we can eyeball what the struct really looks like.
                // We print two u64 values per line (16 bytes), starting
                // at chara+0x44470.
                constexpr uintptr_t dump_start = 0x44470;
                for (int row = 0; row < 5; ++row)
                {
                    uintptr_t off = dump_start + row * 16;
                    uint64_t a = 0, b = 0;
                    SafeReadUInt64(bytes + off,     &a);
                    SafeReadUInt64(bytes + off + 8, &b);
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[HorseMod.KHit]    +0x{:x}: 0x{:016x} 0x{:016x}\n"),
                        off, a, b);
                }
            }

            // --- Attack list -------------------------------------------------
            walkList(ue_chara, poseSelector, atk_head,
                     KHitList::Attack, active_cell, reactions, verbose, visit);
            // --- Hurtbox list ------------------------------------------------
            walkList(ue_chara, poseSelector, hurt_head,
                     KHitList::Hurtbox, active_cell, reactions, verbose, visit);
            // --- Body / pushbox list -----------------------------------------
            walkList(ue_chara, poseSelector, body_head,
                     KHitList::Body, active_cell, reactions, verbose, visit);
        }

        // Throttle log output to ~twice per second at 60 FPS.
        // Increments only once per forEachKHit invocation.
        static bool shouldLog()
        {
            static int s_ticks = 0;
            ++s_ticks;
            // Log on the first tick, then ~every 128 invocations.
            return (s_ticks & 0x7F) == 1;
        }

    private:
        template <class Visit>
        static void walkList(void* chara,
                             uint32_t poseSelector,
                             void* head,
                             KHitList listKind,
                             void* activeAttackCell,
                             const int32_t (&reactions)[22],
                             bool verbose,
                             Visit&& visit)
        {
            // Hard cap on list traversal so a corrupt list pointer can't
            // loop forever.  Real lists rarely exceed ~30 entries.
            constexpr int kMaxNodes = 128;

            void* node = head;
            int  list_index = 0;
            int  walked     = 0;
            int  emitted    = 0;
            for (int i = 0; i < kMaxNodes && node; ++i, ++list_index)
            {
                // Guard: pointer must be plausibly in the user heap.
                auto n = reinterpret_cast<uintptr_t>(node);
                if (n < 0x10000ULL || n > 0x00007fffffffffffULL) break;

                auto* nbytes = reinterpret_cast<uint8_t*>(node);

                // Read the common header.
                uint8_t  streamTag = 0xFF;
                uint8_t  boneId    = 0;
                uint32_t flags10   = 0;
                void*    next      = nullptr;
                if (!SafeReadUInt8(nbytes + KHitOffsets::StreamTypeTag, &streamTag))
                    break;
                if (!SafeReadUInt8(nbytes + KHitOffsets::BoneIdByte, &boneId))
                    break;
                SafeReadUInt32(nbytes + KHitOffsets::Flags10, &flags10);
                SafeReadPtr(nbytes + KHitOffsets::Next, &next);
                ++walked;

                // Build the common draw-prim fields.
                KHitDraw d{};
                d.list              = listKind;
                d.is_current_attack = (listKind == KHitList::Attack &&
                                       node == activeAttackCell);
                d.stream_tag        = streamTag;
                d.bone_id_internal  = boneId;
                d.flags10           = flags10;
                d.hurtbox_slot      = (listKind == KHitList::Hurtbox &&
                                       list_index < 22) ? list_index : -1;
                d.reaction_hot      = (d.hurtbox_slot >= 0) &&
                                       reactions[d.hurtbox_slot] != 0;

                // Verbose diagnostic — dump the bone FMatrix we get back
                // for the first bone-attached node of each list.  Prints
                // all four rows (row 3 is the translation).  If row 3 is
                // non-zero and matches the character's world position then
                // the native call is healthy.
                if (verbose && emitted == 0 && (streamTag == 0 || streamTag == 1))
                {
                    // Read the correct bone-idx slot per subclass.
                    uint32_t ueBonePre = 0xFFFFFFFFu;
                    uint32_t ueBoneB   = 0xFFFFFFFFu;
                    if (streamTag == 0)
                    {
                        SafeReadUInt32(
                            nbytes + KHitOffsets::Sphere_UE4BoneIndex,
                            &ueBonePre);
                    }
                    else
                    {
                        SafeReadUInt32(
                            nbytes + KHitOffsets::Area_UE4BoneIndexA,
                            &ueBonePre);
                        SafeReadUInt32(
                            nbytes + KHitOffsets::Area_UE4BoneIndexB,
                            &ueBoneB);
                    }
                    const int32_t ueBoneRemap = NativeBinding::remapBoneId(boneId);
                    FMatrix64 tx{};
                    bool got = false;
                    if (ueBonePre != 0xFFFFFFFFu && ueBonePre <= 4096u)
                    {
                        got = NativeBinding::getBoneTransform(
                            chara, poseSelector, ueBonePre, tx);
                    }

                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[HorseMod.Bone] chara=0x{:x} pose={} tag={} "
                            "internalBone=0x{:02x} ueBone_pre=0x{:x} "
                            "ueBone_B=0x{:x} ueBone_remap={} xform_ok={}\n"),
                        reinterpret_cast<uintptr_t>(chara),
                        poseSelector, streamTag, boneId, ueBonePre, ueBoneB,
                        ueBoneRemap, got ? 1 : 0);
                    for (int r = 0; r < 4; ++r)
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.Bone]   M[{}] = "
                                "({:.3f},{:.3f},{:.3f},{:.3f})\n"),
                            r,
                            tx.M[r][0], tx.M[r][1], tx.M[r][2], tx.M[r][3]);
                    }
                }

                // Build geometry per subclass.
                //
                // Sphere and Area are bone-attached — they need the chara +
                // poseSelector to call GetBoneTransformForPose.  FixArea is
                // static world geometry with no bone, so we use the legacy
                // Namco-world value at +0x50 directly.
                bool ok = false;
                // Skip reason codes:
                //   0 = not skipped
                //   1 = sphere (tag 0) build returned false
                //   2 = area   (tag 1) build returned false
                //   3 = fixarea(tag 2) build returned false
                //   9 = unknown stream tag (not 0/1/2)
                int skip_code = 0;
                uint32_t ueBoneDbg = 0xFFFFFFFFu;
                SafeReadUInt32(nbytes + KHitOffsets::UE4BoneIndex, &ueBoneDbg);

                switch (streamTag)
                {
                    case 0:
                        ok = buildSphereWorld(chara, poseSelector,
                                              nbytes, boneId, d);
                        if (!ok) skip_code = 1;
                        break;
                    case 1:
                        ok = buildAreaWorld(chara, poseSelector,
                                            nbytes, boneId, d);
                        if (!ok) skip_code = 2;
                        break;
                    case 2:
                        ok = buildFixAreaWorld(nbytes, d);
                        if (!ok) skip_code = 3;
                        break;
                    default:
                        skip_code = 9;
                        break;
                }

                // Log the first 4 skipped nodes per list so we can see what
                // kind of nodes are being rejected and why.  Common causes:
                //   * stream_tag outside {0,1,2} — new subclass we haven't
                //     implemented yet (likely for AttackExt/MoveExtent).
                //   * +0x7C UE4 bone idx = 0xFFFFFFFF — "no attachment"
                //     marker.  Happens for nodes authored world-space.
                //   * +0x7C out-of-range — corrupt scratch or wrong layout.
                if (verbose && !ok)
                {
                    static int s_skip_logged_per_list[3] = {0, 0, 0};
                    const int li = static_cast<int>(listKind);
                    if (li >= 0 && li < 3 && s_skip_logged_per_list[li] < 4)
                    {
                        ++s_skip_logged_per_list[li];
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.KHit]   SKIP list={} idx={} tag={} "
                                "bone=0x{:02x} ueBone7C=0x{:x} code={}\n"),
                            li, list_index, streamTag, boneId, ueBoneDbg,
                            skip_code);
                    }
                }

                // Raw node hex-dump for the first AREA (tag=1) node we
                // encounter across any list.  Fires once per mod lifetime
                // so we can eyeball the true +0x30/+0x40/+0x90/+0x94
                // layout of KHitArea and compare against Ghidra's plate.
                if (verbose && streamTag == 1)
                {
                    static bool s_dumped_area = false;
                    if (!s_dumped_area)
                    {
                        s_dumped_area = true;
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.KHit]  AREA-DUMP node@0x{:x} "
                                "list={} bone=0x{:x}\n"),
                            reinterpret_cast<uintptr_t>(node),
                            static_cast<int>(listKind), boneId);
                        for (int row = 0; row < 10; ++row)
                        {
                            uintptr_t off = row * 16;
                            uint64_t a = 0, b = 0;
                            SafeReadUInt64(nbytes + off,     &a);
                            SafeReadUInt64(nbytes + off + 8, &b);
                            float f0=0, f1=0, f2=0, f3=0;
                            SafeReadFloat(nbytes + off + 0,  &f0);
                            SafeReadFloat(nbytes + off + 4,  &f1);
                            SafeReadFloat(nbytes + off + 8,  &f2);
                            SafeReadFloat(nbytes + off + 12, &f3);
                            RC::Output::send<RC::LogLevel::Verbose>(
                                STR("[HorseMod.KHit]    A+0x{:02x}: "
                                    "0x{:016x} 0x{:016x}  "
                                    "f=({:.3f},{:.3f},{:.3f},{:.3f})\n"),
                                off, a, b, f0, f1, f2, f3);
                        }
                        uint32_t bA = 0xFFFFFFFFu, bB = 0xFFFFFFFFu;
                        SafeReadUInt32(nbytes + 0x90, &bA);
                        SafeReadUInt32(nbytes + 0x94, &bB);
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.KHit]    A+0x90 boneA=0x{:x}  "
                                "A+0x94 boneB=0x{:x}\n"),
                            bA, bB);
                    }
                }

                // Raw node hex-dump for the first emitted node of each list.
                // Prints 0xA0 bytes = 10 rows of 16 bytes.  Use this to
                // reverse the real KHit struct layout.
                if (verbose && emitted == 0)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[HorseMod.KHit]     node@0x{:x} list={} raw "
                            "hdr tag={} bone=0x{:x} flags10=0x{:x} next=0x{:x}\n"),
                        reinterpret_cast<uintptr_t>(node),
                        static_cast<int>(listKind),
                        streamTag, boneId, flags10,
                        reinterpret_cast<uintptr_t>(next));

                    for (int row = 0; row < 10; ++row)
                    {
                        uintptr_t off = row * 16;
                        uint64_t a = 0, b = 0;
                        SafeReadUInt64(nbytes + off,     &a);
                        SafeReadUInt64(nbytes + off + 8, &b);
                        // Also decode as 4 floats per 16-byte row to see
                        // whether the row is pos/rot data.
                        float f0=0, f1=0, f2=0, f3=0;
                        SafeReadFloat(nbytes + off + 0,  &f0);
                        SafeReadFloat(nbytes + off + 4,  &f1);
                        SafeReadFloat(nbytes + off + 8,  &f2);
                        SafeReadFloat(nbytes + off + 12, &f3);
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.KHit]       +0x{:02x}: "
                                "0x{:016x} 0x{:016x}  "
                                "f=({:.2f},{:.2f},{:.2f},{:.2f})\n"),
                            off, a, b, f0, f1, f2, f3);
                    }
                }

                if (ok)
                {
                    visit(static_cast<const KHitDraw&>(d));
                    ++emitted;
                }

                node = next;
            }

            if (verbose)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[HorseMod.KHit]   list={} head=0x{:x} walked={} emitted={}\n"),
                    static_cast<int>(listKind),
                    reinterpret_cast<uintptr_t>(head),
                    walked, emitted);
            }
        }

        // ---- geometry builders ----

        // Read a vec3 at `addr` into `out`.  Returns true on success.
        static bool readVec3(const uint8_t* addr, FVec3& out)
        {
            float buf[3] = {};
            for (int i = 0; i < 3; ++i)
            {
                if (!SafeReadFloat(addr + i * sizeof(float), &buf[i]))
                    return false;
            }
            out = FVec3{ buf[0], buf[1], buf[2] };
            return true;
        }

        // Fetch the world-space bone FMatrix for a given UE4 bone index.
        // Returns false on bad index, faulty read, or native-call failure.
        static bool fetchBoneMatrix(void* chara,
                                    uint32_t poseSelector,
                                    uint32_t ueBoneIdx,
                                    FMatrix64& out_xform)
        {
            if (ueBoneIdx == 0xFFFFFFFFu) return false;
            // Sanity clamp — any skeleton with more than 4096 bones would
            // be absurd; this catches garbage reads cheaply.
            if (ueBoneIdx > 4096u) return false;
            return NativeBinding::getBoneTransform(
                chara, poseSelector, ueBoneIdx, out_xform);
        }

        // Resolve a KHit node's bone attachment into a UE4 bone FMatrix
        // (4×4 affine) in world space using the SPHERE layout — i.e. the
        // pre-remapped UE4 bone index at +0x7C.  Matches the game's own
        // KHitSphere_UpdateWorldCenter and is valid for first-node-of-list
        // cases where the raw +0x17 byte is 0 (which our remap path would
        // reject as "invalid").
        //
        // Area nodes do NOT store their bone idx at +0x7C — see
        // resolveAreaBoneTransforms below.
        static bool resolveSphereBoneTransform(void* chara,
                                               uint32_t poseSelector,
                                               const uint8_t* node,
                                               FMatrix64& out_xform)
        {
            uint32_t ueBone = 0xFFFFFFFFu;
            if (!SafeReadUInt32(node + KHitOffsets::Sphere_UE4BoneIndex,
                                &ueBone))
                return false;
            return fetchBoneMatrix(chara, poseSelector, ueBone, out_xform);
        }

        // Area has two bone attachments — one per diagonal corner.  Reads
        // both and returns the two matrices.  Returns false if either
        // read/lookup fails.
        static bool resolveAreaBoneTransforms(void* chara,
                                              uint32_t poseSelector,
                                              const uint8_t* node,
                                              FMatrix64& outA,
                                              FMatrix64& outB)
        {
            uint32_t ueBoneA = 0xFFFFFFFFu;
            uint32_t ueBoneB = 0xFFFFFFFFu;
            if (!SafeReadUInt32(node + KHitOffsets::Area_UE4BoneIndexA,
                                &ueBoneA)) return false;
            if (!SafeReadUInt32(node + KHitOffsets::Area_UE4BoneIndexB,
                                &ueBoneB)) return false;
            if (!fetchBoneMatrix(chara, poseSelector, ueBoneA, outA))
                return false;
            // P2's bone is often the same as P1's, but not required.  If B
            // fails (e.g. out-of-range garbage) fall back to A so we still
            // render something sensible.
            if (!fetchBoneMatrix(chara, poseSelector, ueBoneB, outB))
                outB = outA;
            return true;
        }

        // Pre-scale a bone-local SC6 point by g_LuxCmToUEScale (10.0) before
        // running it through the bone FMatrix.  The matrix's row magnitudes
        // are the actor's skeletal component scale (≈1.0) — they don't
        // include the cm→UE conversion, so we do it here.
        static FVec3 LiftBoneLocalToWorld(const FMatrix64& bone,
                                          const FVec3& boneLocal)
        {
            const FVec3 scaled = { boneLocal.X * kLuxCmToUE,
                                   boneLocal.Y * kLuxCmToUE,
                                   boneLocal.Z * kLuxCmToUE };
            return TransformPoint(bone, scaled);
        }

        // Approximate the uniform 3×3 row scale of a bone FMatrix by
        // averaging the three row magnitudes.  Used to size spheres.
        static float rowScaleMean(const FMatrix64& m)
        {
            auto mag = [&](int r) {
                return std::sqrt(m.M[r][0] * m.M[r][0]
                               + m.M[r][1] * m.M[r][1]
                               + m.M[r][2] * m.M[r][2]);
            };
            return (mag(0) + mag(1) + mag(2)) / 3.0f;
        }

        // KHitSphere: transform the bone-local centre at +0x30 through the
        // bone's world FMatrix.  Matches SC6's ALuxTraceManager path.
        static bool buildSphereWorld(void* chara, uint32_t pose,
                                     const uint8_t* node,
                                     uint8_t /*internalBoneId*/,
                                     KHitDraw& out)
        {
            FMatrix64 bone{};
            if (!resolveSphereBoneTransform(chara, pose, node, bone))
                return false;

            FVec3 local;
            if (!readVec3(node + KHitOffsets::LocalCenter, local)) return false;

            float radius = 0.0f;
            if (!SafeReadFloat(node + KHitOffsets::Radius, &radius)) return false;
            if (radius < 0.0f) radius = -radius;

            out.kind   = KHitKind::Sphere;
            out.centre = LiftBoneLocalToWorld(bone, local);
            out.radius = radius * kLuxCmToUE * rowScaleMean(bone);
            return true;
        }

        // KHitArea: an OBB axis-aligned in BONE-LOCAL space.  P1 at +0x30
        // and P2 at +0x40 are the two DIAGONAL CORNERS of this box — NOT
        // two separate world-space endpoints.  The game stores two bone
        // indices (+0x90 for P1, +0x94 for P2) because each corner can in
        // principle live on a different bone, but the common case is
        // bone A == bone B (a simple bone-local AABB).
        //
        // To render: generate all 8 corners by permuting per-axis min/max
        // of (P1, P2) in BONE-LOCAL SPACE, then transform each through
        // bone A's FMatrix.  This produces a rotated OBB that follows the
        // bone orientation — correct for same-bone case and a reasonable
        // approximation when bone A != bone B (rare, for swept limb
        // volumes).
        //
        // Why this interpretation (vs. the world-AABB-of-two-points path
        // we tried first): fighters have LOTS of bone-oriented hurtboxes
        // (around arms/legs/torso).  A world-axis-aligned AABB wouldn't
        // rotate with the bone — hurtboxes would "float" off the body
        // when a bone twists.  An OBB in bone-local space is what visually
        // hugs the body through animation.
        //
        // See KHitArea_UpdateWorldCenters @ 0x14030E480 for the
        // two-bone-index storage and deserialization.
        static bool buildAreaWorld(void* chara, uint32_t pose,
                                   const uint8_t* node,
                                   uint8_t /*internalBoneId*/,
                                   KHitDraw& out)
        {
            FMatrix64 boneA{}, boneB{};
            if (!resolveAreaBoneTransforms(chara, pose, node, boneA, boneB))
                return false;

            FVec3 localP1, localP2;
            if (!readVec3(node + KHitOffsets::Area_LocalP1, localP1))
                return false;
            if (!readVec3(node + KHitOffsets::Area_LocalP2, localP2))
                return false;

            // Per-axis min/max in BONE-LOCAL space — this is what makes
            // the OBB axis-aligned in the bone's frame.
            const float minLX = std::fmin(localP1.X, localP2.X);
            const float minLY = std::fmin(localP1.Y, localP2.Y);
            const float minLZ = std::fmin(localP1.Z, localP2.Z);
            const float maxLX = std::fmax(localP1.X, localP2.X);
            const float maxLY = std::fmax(localP1.Y, localP2.Y);
            const float maxLZ = std::fmax(localP1.Z, localP2.Z);

            // 8 corners in standard ordering:
            //   bit 0 = X (0:min, 1:max), bit 1 = Y, bit 2 = Z
            // Transform each through bone A.  (boneB is unused in the
            // same-bone case; if P1/P2 attach to different bones we'd
            // need to pick or blend, but approximating with A is fine
            // for visualisation.)
            for (int i = 0; i < 8; ++i)
            {
                const float lx = (i & 1) ? maxLX : minLX;
                const float ly = (i & 2) ? maxLY : minLY;
                const float lz = (i & 4) ? maxLZ : minLZ;
                out.corners[i] = LiftBoneLocalToWorld(
                    boneA, FVec3{ lx, ly, lz });
            }

            // Silence unused warning (kept for future refinement where we
            // might blend A/B per-corner based on bit mask).
            (void)boneB;

            out.kind = KHitKind::Box;
            return true;
        }

        // KHitFixArea: static world-space stage volumes, no bone attachment.
        // The Namco-world centre at +0x50 is valid for these (no chara pose
        // baked in, since the FMatrix for fixed areas is identity in the
        // local→world slot).  We still need the g_LuxCmToUEScale lift and
        // the Namco→UE axis swap to reach UE world cm.
        static bool buildFixAreaWorld(const uint8_t* node, KHitDraw& out)
        {
            FVec3 namcoCentre, namcoExtents;
            if (!readVec3(node + KHitOffsets::WorldCenterCur, namcoCentre))
                return false;
            if (!readVec3(node + KHitOffsets::MirrorOrExtents, namcoExtents))
                return false;

            // Axis swap: Namco (X=right, Y=up, Z=fwd) → UE (X=fwd, Y=right, Z=up).
            auto toUE = [](const FVec3& n) {
                return FVec3{ n.Z * kLuxCmToUE,
                              n.X * kLuxCmToUE,
                              n.Y * kLuxCmToUE };
            };
            const FVec3 c = toUE(namcoCentre);
            const FVec3 eRaw = toUE(namcoExtents);
            const FVec3 e = { std::fabs(eRaw.X),
                              std::fabs(eRaw.Y),
                              std::fabs(eRaw.Z) };

            for (int i = 0; i < 8; ++i)
            {
                const float sx = (i & 1) ? +1.0f : -1.0f;
                const float sy = (i & 2) ? +1.0f : -1.0f;
                const float sz = (i & 4) ? +1.0f : -1.0f;
                out.corners[i] = FVec3{ c.X + e.X * sx,
                                        c.Y + e.Y * sy,
                                        c.Z + e.Z * sz };
            }
            out.kind = KHitKind::Box;
            return true;
        }
    };

    // ------------------------------------------------------------------
    // Draw helper — expand a KHitDraw into ILineOverlay segments.
    // Split out so callers can decide colours / thickness per-node.
    // ------------------------------------------------------------------
    inline void DrawKHitDraw(ILineOverlay& overlay,
                             const KHitDraw& d,
                             const FLinColor& color,
                             float thickness)
    {
        if (d.kind == KHitKind::Box)
        {
            const auto& v = d.corners;
            // Bottom face.
            overlay.drawLine(v[0], v[1], color, thickness);
            overlay.drawLine(v[1], v[3], color, thickness);
            overlay.drawLine(v[3], v[2], color, thickness);
            overlay.drawLine(v[2], v[0], color, thickness);
            // Top face.
            overlay.drawLine(v[4], v[5], color, thickness);
            overlay.drawLine(v[5], v[7], color, thickness);
            overlay.drawLine(v[7], v[6], color, thickness);
            overlay.drawLine(v[6], v[4], color, thickness);
            // Verticals.
            overlay.drawLine(v[0], v[4], color, thickness);
            overlay.drawLine(v[1], v[5], color, thickness);
            overlay.drawLine(v[2], v[6], color, thickness);
            overlay.drawLine(v[3], v[7], color, thickness);
            return;
        }

        // Sphere: approximate with 3 axis-aligned rings at centre.
        // Each ring is N segments; 16 looks smooth at normal camera distance.
        constexpr int N = 16;
        constexpr float TWO_PI = 6.283185307179586f;
        auto ring = [&](int axis) {
            FVec3 prev{};
            for (int i = 0; i <= N; ++i)
            {
                const float a = TWO_PI * static_cast<float>(i)
                              / static_cast<float>(N);
                const float c = d.radius * std::cosf(a);
                const float s = d.radius * std::sinf(a);
                FVec3 p;
                switch (axis)
                {
                    case 0: p = FVec3{d.centre.X,     d.centre.Y + c, d.centre.Z + s}; break;
                    case 1: p = FVec3{d.centre.X + c, d.centre.Y,     d.centre.Z + s}; break;
                    default: p = FVec3{d.centre.X + c, d.centre.Y + s, d.centre.Z};    break;
                }
                if (i > 0) overlay.drawLine(prev, p, color, thickness);
                prev = p;
            }
        };
        ring(0); ring(1); ring(2);
    }

} // namespace Horse
