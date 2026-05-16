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
//     +0x44478  KHitBase*  BodyListHead         (neither deal nor receive
//                                                damage — pushbox used by
//                                                LuxBattle_SolvePhysBodyCollision
//                                                @ 0x14030CCF0 for
//                                                character-to-character
//                                                physical pushing.  Proof:
//                                                tick @ 0x14033CCA0 calls
//                                                SolvePhysBodyCollision with
//                                                CharaSlot+0x44078, and inside
//                                                SolvePhys iterates
//                                                param_1+0x400 = chara+0x44478.)
//     +0x44498  KHitBase*  AttackListHead       (deal damage or initiate a
//                                                grab.  The CategoryMask at
//                                                node+0x08 drives classifier
//                                                decisions.  Proof: tick pass
//                                                in LuxBattleChara_
//                                                UpdateAllKHitWorldCenters
//                                                @ 0x14030D6A0 iterates this
//                                                list as the ATTACKER side,
//                                                OR'ing each node's +0x08
//                                                into opponent's
//                                                PerHurtboxBitmask slot.
//                                                Also: tick @ 0x14033CCA0
//                                                activates nodes here via
//                                                `*(node+0x14) =
//                                                (hotMask >> node[+0x17]) & 1`.)
//     +0x444B8  KHitBase*  HurtboxListHead      (receive damage.  Each
//                                                node's +0x17 BoneId byte
//                                                indexes PerHurtboxBitmask[i]
//                                                and PerHurtboxReactionState[i].
//                                                Proof:
//                                                UpdateAllKHitWorldCenters
//                                                iterates this list as the
//                                                DEFENDER side, using node
//                                                +0x17 as the slot index.)
//     +0x44494  int32      ClassifierHurtboxBound (a.k.a. AttackMaxSlot —
//                                                written by the ATTACK
//                                                stream's deserialiser as
//                                                pOutMaxSlot.  The engine
//                                                reuses this field as the
//                                                loop bound in
//                                                LuxBattle_ResolveAttackVsHurtbox
//                                                Mask22 @ 0x14033C100 when
//                                                iterating PerHurtboxBitmask[i]
//                                                and PerHurtboxReactionState[i].
//                                                So the same u32 serves two
//                                                roles: max attack slot for
//                                                THIS chara, AND hurtbox
//                                                iteration bound.  Hurtbox-
//                                                list own max-slot lives at
//                                                chara+0x444B4 and is not
//                                                read by the classifier.)
//     +0x44048  KHitBase*  CurrentActiveAttackCell  (CROSS-CHARA: copied
//                                                from OPPONENT chara's
//                                                +0x44058 each tick, so
//                                                comparing it to nodes in
//                                                THIS chara's attack list
//                                                will never match.  Use
//                                                node+0x14 (engine's own
//                                                per-frame active gate) to
//                                                decide "hot" instead.)
//     +0x44078  u64[22]    PerHurtboxBitmask    (defender-side aggregation —
//                                                bitmask of attacking
//                                                categories hitting hurtbox i)
//     +0x1c74   int32[22]  PerHurtboxReactionState (classifier output:
//                                                0=None 1=Hit 2=BlockedLow
//                                                3=BlockedHigh 4=MH_Loser
//                                                6=Tech 8=MH_Winner 9=AirHit
//                                                10=MH_Trade B=WallSplat
//                                                C=Stagger — see enum
//                                                LuxHitReactionState in
//                                                Ghidra)
//
// Historical note
// ---------------
// Earlier revisions of this file (and the corresponding Ghidra plate on
// LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0) had the
// three list heads rotated:  +0x44478 was labelled "AttackListHead",
// +0x44498 was labelled "HurtboxListHead", +0x444B8 was labelled
// "BodyListHead".  That mislabelling is why attack boxes in the overlay
// appeared to never make contact with the opponent on hit — the boxes
// labelled "attack" were the pushbox list, not the damage list.  The
// labels above are the corrected mapping, cross-verified in Ghidra.
//
// Engine-derived role categorisation (the RIGHT way to categorise)
// ----------------------------------------------------------------
// Instead of inventing size-based buckets, we follow the engine's own
// partition.  Two pieces fit together:
//
//   (1) At stream-deserialise time, BOTH KHitChk_InitSphereFromStream
//       @ 0x14030E0D0 and KHitChk_InitAreaFromStream @ 0x14030E3A0 do:
//
//           node[+0x08] = 1ULL << (streamByte[2] & 0x3F);
//           node[+0x17] = streamByte[2];
//
//       So node+0x08 is a SINGLE-BIT value whose position is taken
//       directly from the authored +0x17 slot index (0..63).
//
//   (2) At classify time, LuxBattle_ResolveAttackVsHurtboxMask22
//       @ 0x14033C100 partitions the 64-slot mask space into two
//       disjoint regions:
//
//           THROW / GRAB  — bits 31 and 55           (0x80000080000000)
//           STRIKE        — every other bit          (0xFF7FFFFF7FFFFFFF)
//
// Combining the two: an Attack-list node authored with +0x17 == 31 or
// 55 IS a throw; anything else IS a strike.  The classifier also does
// a throw pre-scan (any throw bit present in the active-move mask ->
// grab-transition logic fires before the per-hurtbox strike loop),
// which is why we treat throw bits as taking priority in
// ClassifyAttackRole below.  We expose the same split in the UI as
// "Strike" and "Throw" toggles gating the AttackList only.
//
// The three lists therefore answer the user's three questions directly:
//   * AttackList  → entries that DEAL damage (or initiate a grab if the
//                   throw bits are set in the CategoryMask).
//   * HurtboxList → entries that RECEIVE damage / reactions.
//   * BodyList    → "other" — character-to-character pushing (physics).
//                   Not involved in hit resolution.
//
// KHitBase common header (0xA0 = 160 bytes total per node):
//     +0x00  void* Vtable
//     +0x08  u64   PerAttackerBit  = 1ULL << (authored_slot & 0x3F)
//                                    (SlotByte is stream byte[2], mirrored
//                                     into +0x17 as well)
//     +0x10  u32   Node_Flags10              AUTHORED, WRITE-ONLY.
//                                            Copied verbatim from the compiled
//                                            stream (dword at byte offset 4)
//                                            by all three init paths
//                                            (InitSphereFromStream,
//                                             InitAreaFromStream, inlined
//                                             FixArea branch in
//                                             Lux_KHitChk_DeserializeLinkedList).
//                                            GHIDRA AUDIT (Apr 2026):
//                                              no runtime reader in the hit
//                                              pipeline — TickHitResolution,
//                                              UpdateAllKHitWorldCenters,
//                                              OverlapTest_vt10,
//                                              Sphere/Area UpdateWorldCenter,
//                                              ResolveAttackVsHurtboxMask22,
//                                              and the net Write/Read
//                                              serialisers all skip this
//                                              field. Preserved Namco moveset-
//                                              editor round-trip metadata;
//                                              DO NOT classify or gate boxes
//                                              on it. Classification lives in
//                                              PerAttackerBit (+0x08) slot
//                                              partitioning +
//                                              ReactionCategoryByte
//                                              (chara+0x1992).
//     +0x14  u16   ActiveThisFrame           GeometryActiveGate — written
//                                            per-frame by
//                                            LuxBattle_TickHitResolutionAnd
//                                            BodyCollision from the MoveVM
//                                            hotMask:
//                                              node[+0x14] =
//                                                (hotMask >> node[+0x17]) & 1
//                                            hotMask has a permanent floor
//                                            of 0x3FFFD (slots {0, 2..17}
//                                            always on). UpdateAllKHitWorld
//                                            Centers short-circuits the
//                                            overlap loop on both attacker
//                                            and defender when this is 0.
//     +0x16  u8    StreamTypeTag             (0=Sphere, 1=Area, 2=FixArea)
//     +0x17  u8    SubIdOrBoneId             per-node slot byte 0..63. For
//                                            attack nodes == slot index
//                                            (drives the Strike/Throw
//                                            partition in the 64-bit
//                                            CategoryMask). For hurt/body
//                                            nodes == defender bone slot,
//                                            used directly as index into
//                                            PerHurtboxBitmask[22] at
//                                            chara+0x44078. Values 6/7
//                                            additionally trigger the
//                                            ground-clamp branch.
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

#include <atomic>
#include <cmath>
#include <cstdint>

namespace Horse
{
    // ------------------------------------------------------------------
    // Per-chara offsets relative to ALuxBattleChara / g_LuxBattle_CharaSlot.
    // ------------------------------------------------------------------
    namespace ChaOffsets
    {
        // Active-cell pointers for the classifier pipeline.  Relationship
        // (verified in LuxBattle_TickHitResolutionAndBodyCollision
        // @ 0x14033CCA0):
        //
        //   chara[+0x44058] = OwnActiveAttackCell
        //     THIS chara's currently-active attack cell pointer, written
        //     by the MoveVM from the current move's authored per-frame
        //     timeline.  The pointee's first u64 is the 64-bit attacker
        //     slot mask (which slots are "live for damage" this frame).
        //
        //   chara[+0x44048] = OpponentActiveAttackCellCopy
        //     Copy of the OPPONENT's +0x44058, pulled each tick so
        //     LuxBattle_ResolveAttackVsHurtboxMask22 can read a single
        //     u64 mask from its own chara pointer without crossing
        //     actors.  DO NOT pointer-compare this against nodes in
        //     THIS chara's attack list — it belongs to the other chara's
        //     list.
        //
        //   chara[+0x44060] / +0x44050 = counter-hit / alt-path mirror
        //     of the above pair (same shape; drives throw-tier & CH
        //     reseeding logic).
        //
        // The engine itself uses node+0x14 (see KHitOffsets::IsActiveThisFrame
        // below) as the per-frame geometry/overlap gate; +0x44048 is the
        // damage gate.  Both have to be set for a hit to fire.
        constexpr uintptr_t OwnActiveAttackCell           = 0x44058;  // u64** -> u64 mask at [0]
        constexpr uintptr_t OpponentActiveAttackCellCopy  = 0x44048;  // u64** -> u64 mask at [0]
        constexpr uintptr_t CurrentActiveAttackCell       = 0x44048;  // legacy alias

        // --- Adjacent hit/move state pointers in the 0x44040..0x44070 cluster ---
        // Mapped 2026-04 (Ghidra pass).  Each is an 8-byte pointer into
        // the MoveVM bank, not a u64 mask.  See plate comments on the
        // respective setters/readers for the full story.
        //
        //   +0x44040  PrimaryAttackCellPtr
        //     Frozen snapshot of the attack cell at move START (same value
        //     as +0x44058 at transition, never updated per-sub-slot).
        //     Read by LuxBattle_ApplyDamageFromPendingHit and
        //     TickHitResolution as a "damage-window-expired" predicate
        //     (`cell==NULL || cell[+0x3a]==0`).  Cleared by
        //     LuxBattle_InitializeMatchRoundState.
        //
        //   +0x44050  OpponentNonAttackMoveDescrCopy  (short[3]*)
        //     Per-tick MIRROR of opponent's +0x44060, written by
        //     TickHitResolution (0x14033da11/da51/daa5).  Consumed only
        //     by downstream reaction/classifier code — the damage path
        //     at ApplyDamageFromPendingHit re-reads the authoritative
        //     attacker-side +0x44060 instead.
        //
        //   +0x44060  NonAttackMoveDescrPtr  (short[3]*)
        //     (NB earlier tentatively named "CounterHitDescr" — that's
        //     a misnomer.)  Set by LuxMoveVM_TransitionToMove's ALT
        //     branch when the move is flagged non-attack (bit 0x1000 in
        //     cell shortAddr).  Points into
        //     `bankBase + bank[+0x14] + (subIdx & 0xffffefff)*6`.
        //     Record layout (decoded 2026-04 from ApplyDamageFromPendingHit
        //     at 0x1402ffba5):
        //         [0]  i16  DamageMultiplier   — cast to float,
        //                                       multiplied into
        //                                       PlayerExtraSkill damage-
        //                                       reduction factor.
        //         [1]  i16  PassthroughTag     — copied to defender
        //                                       chara+0x210c (paired
        //                                       with +0x20f6/+0x210a
        //                                       tags from TransitionToMove).
        //         [2]  i16  DurationTicks      — `(float)i16/60.0f` secs,
        //                                       stored into attacker+0x414
        //                                       (the trailing damage bucket
        //                                       that feeds +0x3f8 each tick).
        //     Triggers: non-damaging supers/SC finishers, stance transitions,
        //     GI/parry transitions, movement moves with a scripted
        //     damage kicker.  NOT counter-hit-specific.
        //
        //   +0x44068  ActiveLaneStateCursorPtr  (LuxMoveLaneState*)
        //     Points at `chara + laneIdx*0x468 + 0x444f0` (running lane
        //     block).  `cursor[+8]` is the current animation-frame
        //     float counter.  Written by TransitionToMove, read by
        //     CaptureHitAreaState, ProcessHit (copies cursor[+8] into
        //     chara+0x1360), and EvaluateAndTriggerSlowMotion.
        //
        //   +0x44070  LastHitSourceCellLo48  (48-bit packed)
        //     6-byte snapshot (dword + word) written by
        //     LuxBattleChara_ProcessHit from the defender's +0x44048
        //     (= attacker's cell copy).  Companion at +0x43da8/+0x43dac.
        //     Opaque hit-id — only 48 bits stored; NOT a derefable ptr.
        constexpr uintptr_t PrimaryAttackCellPtr          = 0x44040;  // void*
        constexpr uintptr_t OpponentNonAttackMoveDescrCopy = 0x44050; // short(*)[3]
        constexpr uintptr_t NonAttackMoveDescrPtr         = 0x44060;  // short(*)[3]
        constexpr uintptr_t ActiveLaneStateCursorPtr      = 0x44068;  // LuxMoveLaneState*
        constexpr uintptr_t LastHitSourceCellLo48         = 0x44070;  // u8[6]

        // --- Per-frame damage-mask lookup inputs ----------------------------
        // The mask at **(chara+0x44058) is set ONCE PER MOVE-SLOT (inside
        // LuxMoveVM_SetActiveMoveSlot @ 0x140300c70) and stays constant
        // across that slot's startup / active / recovery frames.  For a
        // frame-accurate "is this slot dealing damage RIGHT NOW" answer
        // we have to mirror the exact lookup TickHitResolutionAndBody
        // Collision (0x14033cca0) does each frame:
        //
        //   moveSubId = *(uint16_t*)(chara + 0x44dc2);     // current sub-frame id
        //   bankBase  = *(void**   )(chara + 0x455c0);     // MoveVM bank base
        //   subBank   = (moveSubId >> 12) & 0xF;            // 0..15 sub-bank index
        //   subOff    = *(uint16_t*)(bankBase + (subBank + 7)*4);
        //   subCnt    = *(uint16_t*)(bankBase + 0x1e + subBank*4);
        //   frameIdx  = moveSubId & 0x7FF;
        //   if (frameIdx < subCnt) {
        //       sfRec    = bankBase + (subOff + frameIdx)*0x48 + 0x30;
        //       cellBone = *(int16_t*)(sfRec + 0x3c);
        //       cell     = (uint64_t*)(bankBase
        //                             + *(uint32_t*)(bankBase + 0x10)
        //                             + cellBone * 0x70);
        //       perFrameMask = *cell;   // <-- per-frame damage gate
        //   }
        //
        // `bankBase` is the MoveVM's per-chara bank pointer (one per
        // chara, swapped on move change).  `moveSubId` advances each
        // game frame as the move animation plays; multiple sub-frames
        // can share the same `cellBone` when the move is in a hold.
        constexpr uintptr_t MoveCurrentSubFrameId   = 0x44dc2;  // uint16
        constexpr uintptr_t MoveBankBasePtr         = 0x455c0;  // void*

        // Body / pushbox list — pure physics, no damage.
        // Written / iterated by LuxBattle_SolvePhysBodyCollision @ 0x14030CCF0
        // via `chara + 0x44078 + 0x400 = chara + 0x44478`.
        constexpr uintptr_t BodyListHead            = 0x44478;
        constexpr uintptr_t BodyListCount           = 0x44470;  // head-0x8 (best-guess adjacency)

        // Attack list — deals damage (or initiates a grab if throw bits set
        // in the CategoryMask).  Iterated by UpdateAllKHitWorldCenters
        // @ 0x14030D6A0 as the ATTACKER side; each node's +0x08
        // CategoryMask is OR'd into opponent's PerHurtboxBitmask at the
        // slot given by node+0x17.  Tick @ 0x14033CCA0 activates nodes
        // here via `*(node+0x14) = (hotMask >> node[+0x17]) & 1`.
        constexpr uintptr_t AttackListHead          = 0x44498;
        constexpr uintptr_t AttackListCount         = 0x44490;          // head-0x8

        // Hurtbox list — receives damage.  Iterated by
        // UpdateAllKHitWorldCenters as the DEFENDER side;
        // each node's +0x17 BoneId byte indexes PerHurtboxBitmask[i]
        // and PerHurtboxReactionState[i].
        constexpr uintptr_t HurtboxListHead         = 0x444B8;
        constexpr uintptr_t HurtboxListCount        = 0x444B0;          // head-0x8

        // Hurtbox-list own max-slot — populated by
        // Lux_KHitChk_DeserializeLinkedList's pOutMaxSlot output when
        // the HURTBOX stream (move->+0x40) is parsed.  Holds
        // max(hurt->+0x17 + 1) across the current move's hurtbox set.
        // Read by nothing in the hit pipeline — the classifier uses
        // ClassifierHurtboxBound (below) instead.  We expose it so the
        // overlay can present an honest "this move has N hurtbox slots"
        // number distinct from the classifier's (possibly smaller) bound.
        constexpr uintptr_t HurtboxMaxSlot          = 0x444B4;  // i32

        // Classifier-side hurtbox iteration bound — read by
        // LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100 as the
        // loop-count for its per-slot PerHurtboxBitmask[] walk.
        //
        // HISTORICAL NAME: we previously called this "HurtboxSlotCount"
        // on the assumption the hurtbox deserializer wrote it.  It does
        // NOT.  Verification of LuxBattle_InitCharaSlotForMove_FirstRound
        // (0x1402D4070) shows the three Lux_KHitChk_DeserializeLinkedList
        // calls write pOutMaxSlot to:
        //    BODY    → chara+0x44484
        //    HURTBOX → chara+0x444B4   (HurtboxMaxSlot above)
        //    ATTACK  → chara+0x44494   (THIS FIELD)
        // So this field is literally the ATTACK list's max kind-tag + 1,
        // which the engine also reuses as the hurtbox-iteration bound
        // in the classifier.  Design rationale: attack and hurtbox
        // +0x17 bytes both index the SAME 22-entry kind enumeration
        // (PerHurtboxBitmask u64[22]), so the max-attack-kind gives
        // a natural upper bound for the classifier's per-kind walk of
        // the defender's reaction table.
        //
        // PRACTICAL CONSEQUENCE for the overlay: during moves with few
        // attack slots but many hurtbox slots (dodges, movement,
        // block, throw-whiff) this bound can be SMALLER than
        // HurtboxMaxSlot.  Hurtboxes whose +0x17 >= this bound will
        // have their overlap bits OR'd into PerHurtboxBitmask by
        // UpdateAllKHitWorldCenters but never read back by the
        // classifier — they're "engine-invisible" for damage.  The
        // "Hide unaddressable hurtboxes" UI toggle tests
        // (boneId < ClassifierHurtboxBound) to match this.
        constexpr uintptr_t ClassifierHurtboxBound  = 0x44494;  // i32
        // Back-compat alias — legacy code reads HurtboxSlotCount.
        constexpr uintptr_t HurtboxSlotCount        = 0x44494;  // i32

        // Defender-side aggregation mask — category bits of every ATTACK
        // node currently touching hurtbox i.  Filled by the pre-scan in
        // LuxBattle_ResolveAttackVsHurtboxMask22 before reaction classification.
        constexpr uintptr_t PerHurtboxBitmask       = 0x44078;  // u64[22]
        constexpr uintptr_t PerHurtboxReactionState = 0x1C74;   // i32[22]

        // --- MoveVM lane bases --------------------------------------------
        // Three 0x468-byte LuxMoveLaneState blocks live inline in the
        // chara struct at these offsets.  ActiveLaneStateCursorPtr at
        // +0x44068 points at whichever is currently driving the move.
        //   Lane 0 — primary active move (most strikes live here)
        //   Lane 1 — secondary lane (mirror / queued script)
        //   Lane 2 — stance / yarare / hit-reaction lane
        // See LuxMoveLaneOffsets below for fields within each lane.
        constexpr uintptr_t Lane0Base              = 0x444F0;  // LuxMoveLaneState
        constexpr uintptr_t Lane1Base              = 0x44958;  // LuxMoveLaneState
        constexpr uintptr_t Lane2Base              = 0x44DC0;  // LuxMoveLaneState

        // ----------------------------------------------------------------
        // Lane 2 alt-classify gates (the "counter-hit followup" channel).
        // ----------------------------------------------------------------
        // LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100 runs a
        // SECOND classifier pass after the primary one when ALL of:
        //   chara+0x1725  AltClassifyEnableGate       must be != 0
        //   chara+0x1726  AltInMasterWindow           must be != 0
        //   chara+0x1727  AltClassifyInhibitorA       must be == 0
        //   chara+0x1728  AltClassifyInhibitorB       must be == 0
        // are satisfied on the ATTACKER chara (i.e. THIS chara, when we're
        // visualising THIS chara's attack boxes).
        //
        // The followup pass resolves the attacker's Lane 2 cell:
        //   packedMove = i16  chara+0x44DC2  (Lane2.PackedMoveAddr)
        //   variantIdx = u32  chara+0x45220  (Lane2.AnimVariantIndex)
        //   bank       = ptr  chara+0x455C0  (MoveBank)
        //   slotPtr    = LuxMoveVM_ResolveBankSlot(bank, packedMove)
        //   cellBone   = i16 (variantIdx<6 ? slotPtr[+0x3C+variantIdx*2]
        //                                   : slotPtr[+0x3C])
        //   cellAddr   = bank + bank[+0x10] + cellBone*0x70
        //   altMask    = *(u64*)cellAddr
        // Then runs the classifier loop with attackerMask = altMask
        // instead of the primary cell's u64SlotMask.
        //
        // Used during Soul Charge alt-attacks, Guard Impact counters,
        // parry counters, stance-tech.  HorseMod's is_per_frame_active
        // filter currently checks ONLY the primary mask, so alt-channel
        // hits would visually appear "inactive" while the engine fires
        // them.  Reader below produces an OR of primary + alt masks
        // (gated on the four bytes above) that the filter can use.
        constexpr uintptr_t AltClassifyEnableGate  = 0x1725;   // u8
        constexpr uintptr_t AltInMasterWindow      = 0x1726;   // u8
        constexpr uintptr_t AltClassifyInhibitorA  = 0x1727;   // u8
        constexpr uintptr_t AltClassifyInhibitorB  = 0x1728;   // u8
        // Lane 2's PackedMoveAddr (i16) — Lane2Base + 0x02.
        constexpr uintptr_t Lane2_PackedMoveAddr   = 0x44DC2;
        // Lane 2's AnimVariantIndex (u32) — Lane2Base + 0x460.
        constexpr uintptr_t Lane2_AnimVariantIndex = 0x45220;

        // MoveStartCounter bumped once each time TransitionToMove fires
        // for any lane — useful for detecting move changes without
        // diffing PackedMoveAddr.  Written at 0x1402fe... in transition.
        constexpr uintptr_t MoveStartCounter       = 0x1350;   // u32
        // Live anim-frame float mirror written by ProcessHit from the
        // active lane's cursor+0x08 (so HUD code can read it without
        // chasing the cursor pointer).  Kept per-chara.
        constexpr uintptr_t LastHitAnimFrame       = 0x1360;   // float

        // ----------------------------------------------------------------
        // Engine "this chara can react this frame" gates
        // ----------------------------------------------------------------
        // LuxBattle_ResolveAttackVsHurtboxMask22 (@ 0x14033C100) early-
        // returns when any of these chara-wide gates fail.  When the
        // resolver early-returns, the entire defender hurtbox list is
        // INERT for this frame regardless of geometry / +0x14 / slot
        // index — overlap bits may still be OR'd into PerHurtboxBitmask
        // by the geometry pass at 0x14030D6A0, but no reaction is ever
        // produced and no damage is applied.
        //
        // For the visualiser, that means "render box is real geometry
        // but engine cannot fire a reaction" — same observable as the
        // classifier_addressable failure mode, just driven from the
        // chara-state side instead of the slot-index side.

        // chara+0x19B0 — NoReactStateFlag (i16).  When this equals 6,
        // the resolver writes 0 to chara+0x43d9c and returns without
        // running ANY classification.  Verified site:
        //
        //   if (*(short *)(pDefenderChara + 0x19b0) == 6) {
        //       *(undefined4 *)(pDefenderChara + 0x43d9c) = 0;
        //       return;
        //   }
        //
        // The exact semantic of state 6 is opaque (likely "match-end /
        // round-transition / cinematic" — chara is held in pose but
        // not battle-active).  We treat any non-engine-runnable value
        // as a uniform "inert" signal.
        constexpr uintptr_t NoReactStateFlag       = 0x19B0;   // i16

        // chara+0x20B8 — IncapacitatedFlag (i16).  Resolver early-
        // return:
        //
        //   if (*(short *)(pDefenderChara + 0x20b8) != 0) return;
        //
        // Set during KO / round-loss / cinematic playback when the
        // chara should not classify hits.  Non-zero = inert.
        constexpr uintptr_t IncapacitatedFlag      = 0x20B8;   // i16

        // ----------------------------------------------------------------
        // Attack-phase / frame-window state (2026-05 Ghidra walk)
        // ----------------------------------------------------------------
        // SoulCalibur 6 moves don't deal damage every frame they're playing.
        // Like every fighting game, each move is partitioned into:
        //
        //     STARTUP   — pre-active animation frames (no hit volumes live
        //                 even though attack-list nodes geometrically exist)
        //     ACTIVE    — the frame window during which a successful overlap
        //                 actually fires a reaction
        //     RECOVERY  — post-active frames where you're locked in animation
        //                 but can no longer hit
        //
        // The engine partitions this via cell+0x36 / cell+0x38
        // (MasterWindowStart / MasterWindowEnd, both i16, in units of
        // animation frames, 60 Hz) on the currently-active LuxBattleAttackCell
        // (chara+0x44058).  LuxMoveVM_ClassifyHitboxFrameState (0x140300620)
        // — called every tick from
        // LuxBattle_PreTickStateSnapshotAndRoundDecision (0x14034FCE0) —
        // compares lane->CurrentAnimFrame (lane+0x08, float) against this
        // window and writes a 1/2/3 phase tag into chara+0x1980 (i16).
        //
        // PHASE TAG VALUES (chara+0x1980):
        //     0  = no active cell / classifier disabled (chara+0x16e5 == 0)
        //     1  = STARTUP   (curFrame < MasterWindowStart)
        //     2  = ACTIVE    (curFrame in [WindowStart, WindowEnd])
        //     3  = RECOVERY  (curFrame > MasterWindowEnd)
        //
        // PHASE TAG VS NODE +0x14 — important distinction for visualisers:
        //   Earlier KHitWalker docs treated node+0x14 as the per-frame
        //   active gate and node[+0x14] AND ownAttackMask as the per-frame
        //   damage predicate.  THIS IS WRONG for distinguishing startup vs
        //   active vs recovery.  TickHitResolutionAndBodyCollision
        //   (0x14033CCA0) writes node+0x14 from:
        //
        //     hotMask = 0x3FFFD                                    // floor
        //             | (animCellMask ? *animCellMask : 0)         // sub-frame
        //             | (ownActiveCell ? **ownActiveCell : 0);     // sub-slot
        //
        //   The animCellMask (per-sub-FRAME) is mostly empty for SC6 moves —
        //   Namco's pipeline carries the slot mask on the per-sub-SLOT
        //   cell instead (set by LuxMoveVM_SetActiveMoveSlot).  That slot
        //   mask doesn't change across the move's startup/active/recovery
        //   frames, so node+0x14 stays = 1 across the entire move once the
        //   slot is set.  For TRUE per-frame active timing, read the phase
        //   tag at chara+0x1980 instead.
        //
        // OUTPUT FLAGS (auxiliary, written by ClassifyHitboxFrameState):
        //     chara+0x16ea  bInMasterWindowFlag   : 1 iff phase == 2
        //                                           (cleared if either
        //                                            sub-window inhibitor
        //                                            at +0x16eb or +0x16fe
        //                                            is non-zero)
        //     chara+0x16ec  bPastMasterWindowFlag : 1 iff phase == 3 with
        //                                           reverse-direction
        //                                           sub-condition (used
        //                                           for counter-hit wind
        //                                           effect spawn)
        //
        // SUB-WINDOW BANK OUTPUT (per-bank "is the current frame in this
        // bank's authored sub-window right now"):
        //     chara+0x20CC  wSubWinActiveBank0  GroupId (0..15) when in sub-window
        //     chara+0x20CE  wSubWinActiveBank1  GroupId (16..31) when in sub-window
        //     chara+0x20D0  wSubWinActiveBank2  GroupId (32..47) when in sub-window
        //     chara+0x20D2  wSubWinActiveBank3  GroupId (48..63) when in sub-window
        //   These are 0xFFFF when the bank's sub-window is NOT active.  The
        //   bank corresponding to the current cell's HitboxGroupBitfield
        //   (cell+0x5e bits 0..10, mirrored to chara+0x20F6) is the one
        //   whose ID drives the cell's TYPE-of-hit (used by GetImpactCategory
        //   for trade arbitration).  For HorseMod the bank slot field is
        //   useful as a finer "is this exact sub-window's hitboxes live"
        //   check, complementing the master window.
        constexpr uintptr_t ClassifyEnableGate     = 0x16E5;   // u8 — gate
                                                                // (0 ⇒ phase=0)
        constexpr uintptr_t InMasterWindowFlag     = 0x16EA;   // u8 — phase==2
        constexpr uintptr_t SubWindowInhibitorA    = 0x16EB;   // u8
        constexpr uintptr_t PastMasterWindowFlag   = 0x16EC;   // u8 — phase==3
                                                                // with re-enter
        constexpr uintptr_t SubWindowInhibitorB    = 0x16FE;   // u8
        constexpr uintptr_t FrameWindowPhaseTag    = 0x1980;   // i16 (0/1/2/3)
        constexpr uintptr_t SubWinActiveBank0      = 0x20CC;   // u16
        constexpr uintptr_t SubWinActiveBank1      = 0x20CE;   // u16
        constexpr uintptr_t SubWinActiveBank2      = 0x20D0;   // u16
        constexpr uintptr_t SubWinActiveBank3      = 0x20D2;   // u16
        constexpr uintptr_t HitboxGroupBitfield    = 0x20F6;   // u16
                                                                // (cell+0x5e
                                                                //  mirror)
    }

    // ------------------------------------------------------------------
    // LuxBattleAttackCell — 0x70 bytes per cell, lives in the MoveBank.
    // chara+0x44058 points at the currently-active cell.  Decoded
    // 2026-05 via Ghidra pass on
    //   LuxMoveVM_SetActiveMoveSlot         (0x140300C70)
    //   LuxMoveVM_ClassifyHitboxFrameState  (0x140300620)
    //   LuxBattle_TickHitResolutionAndBodyCollision (0x14033CCA0)
    //
    // Only the fields HorseMod cares about are listed here — see the Ghidra
    // struct LuxBattleAttackCell for the complete 0x70-byte layout.
    // ------------------------------------------------------------------
    namespace LuxAttackCellOffsets
    {
        // u64 SlotMask (+0x00) — bitmask of authored hitbox slots that
        // are LIVE while this cell is the chara's currently-active cell.
        // OR'd into hotMask once per move-slot change in
        // LuxMoveVM_SetActiveMoveSlot, so it's the per-MOVE-SLOT layer
        // of the active gate (NOT per-frame — see ChaOffsets phase docs).
        constexpr uintptr_t SlotMask               = 0x00;   // u64

        // i16 MasterWindowStart (+0x36) and MasterWindowEnd (+0x38) —
        // the FIRST and LAST animation-frame indices for which this
        // cell's hitboxes are considered "active" by the engine.  Read
        // by ClassifyHitboxFrameState (0x140300620) and compared
        // against lane->CurrentAnimFrame to produce the phase tag at
        // chara+0x1980.
        //
        // *** THIS IS THE FRAME-DATA "active window" ***
        // For a move whose active frames are 14-16, MasterWindowStart=14
        // and MasterWindowEnd=16.  Outside this range the hitbox boxes
        // are still rendered (they're geometrically live at +0x14) but
        // overlap_test → reaction will fail because the chara's phase
        // tag is 1 (startup) or 3 (recovery).
        constexpr uintptr_t MasterWindowStart      = 0x36;   // i16
        constexpr uintptr_t MasterWindowEnd        = 0x38;   // i16

        // i16 BaseDamage (+0x3A) — damage figure used by ProcessHit.
        constexpr uintptr_t BaseDamage             = 0x3A;   // i16

        // u16 AttackFlags (+0x32) — high/mid/low/throw + blockability
        // bits; THE per-cell attack-attribute byte.  Consumed by
        // LuxMoveVM_EvaluateMoveTransition @ 0x14033E140 to decide
        // whether an overlap fires hit/block/whiff:
        //
        //   bit 0x001  HighBlockable   — standing block can block this
        //   bit 0x002  LowBlockable    — crouch block can block this
        //   bit 0x008  LowAttack       — must crouch-block; ducks under highs
        //   bit 0x010  MidAttack       — any stance blocks; can't duck
        //   bit 0x040  CrouchOnly      — move only valid if attacker crouched
        //   bit 0x080  HighAttack      — must stand-block; ducks under crouched defender
        //   bit 0x100  Special         — special framing rule
        //   bit 0x200  Unblockable     — no GI / no guard (BlockBypass_GuardBreak)
        //
        // Mapping note: 0x001 / 0x002 are SEPARATELY tested as the
        // "is this hit allowed to land while standing vs crouching"
        // gate in EvaluateMoveTransition; 0x008 / 0x010 / 0x080 are
        // the "what TIER is this attack" indicator for HUD overlays.
        // Both bit families can co-exist (a low attack typically has
        // 0x008 | 0x002 set so a crouch-block defender can block it).
        constexpr uintptr_t AttackFlags            = 0x32;   // u16

        // u16 InputCond (+0x34) — input precondition mask fed to
        // LuxMoveVM_EvaluateMoveInputCondition.  Controls move-state-
        // dependent routing (e.g. "this attack ONLY connects when
        // defender is in state X").  Most cells leave it zero; the
        // non-zero cases drive yarare-id routing during throw resolution.
        constexpr uintptr_t InputCond              = 0x34;   // u16

        // u16 HitboxGroupBitfield (+0x5E) — bits 0..10 select sub-window
        // GroupId 0..63 (4 banks of 16).  Mirrored to chara+0x20F6
        // by SetActiveMoveSlot.
        constexpr uintptr_t HitboxGroupBitfield    = 0x5E;   // u16
    }

    // ------------------------------------------------------------------
    // ELuxBattleAttackFlags — bit set on LuxBattleAttackCell+0x32.
    //
    // Source of truth: LuxMoveVM_EvaluateMoveTransition @ 0x14033E140
    // (see Ghidra plate).  Used during hit classification to decide
    // block vs hit vs whiff vs unblockable.  HorseMod stamps the active
    // cell's flags onto every attack KHitDraw so the renderer can
    // colour or label hitboxes by tier.
    // ------------------------------------------------------------------
    enum KHitAttackFlagBits : uint16_t
    {
        AtkFlag_HighBlockable    = 0x001,
        AtkFlag_LowBlockable     = 0x002,
        AtkFlag_LowAttack        = 0x008,
        AtkFlag_MidAttack        = 0x010,
        AtkFlag_CrouchOnly       = 0x040,
        AtkFlag_HighAttack       = 0x080,
        AtkFlag_Special          = 0x100,
        AtkFlag_Unblockable      = 0x200,
    };

    // High-level move-tier classification synthesised from AttackFlags.
    // Drives the engine's "did this hit on a {standing,crouching} defender"
    // decision in EvaluateMoveTransition + the throw pre-scan.
    enum class KHitAttackTier : uint8_t
    {
        Unknown       = 0,   // No attack-flag bits set (or no active cell)
        High          = 1,   // 0x080 HighAttack — ducks under crouched defender
        Mid           = 2,   // 0x010 MidAttack — blockable in any stance
        Low           = 3,   // 0x008 LowAttack — must crouch-block
        Unblockable   = 4,   // 0x200 Unblockable — no GI / no guard
        Special       = 5,   // 0x100 Special — special framing rule
    };

    // Map an AttackFlags u16 to its dominant tier.  Unblockable overrides
    // every other bit (no useful sub-classification matters when the move
    // can't be blocked).  Otherwise the explicit Low/Mid/High bits pick
    // the tier; Special is a fallback for moves that set 0x100 alone.
    inline KHitAttackTier ClassifyAttackTier(uint16_t flags) noexcept
    {
        if (flags & AtkFlag_Unblockable) return KHitAttackTier::Unblockable;
        if (flags & AtkFlag_LowAttack)   return KHitAttackTier::Low;
        if (flags & AtkFlag_MidAttack)   return KHitAttackTier::Mid;
        if (flags & AtkFlag_HighAttack)  return KHitAttackTier::High;
        if (flags & AtkFlag_Special)     return KHitAttackTier::Special;
        return KHitAttackTier::Unknown;
    }

    inline const char* KHitAttackTierName(KHitAttackTier t) noexcept
    {
        switch (t)
        {
            case KHitAttackTier::Unknown:     return "?";
            case KHitAttackTier::High:        return "High";
            case KHitAttackTier::Mid:         return "Mid";
            case KHitAttackTier::Low:         return "Low";
            case KHitAttackTier::Unblockable: return "Unblockable";
            case KHitAttackTier::Special:     return "Special";
        }
        return "?";
    }

    // Engine-truth attack-phase enum.  Strictly mirrors the i16 written
    // to chara+0x1980 by LuxMoveVM_ClassifyHitboxFrameState.
    //
    // SC6 fighting-game frame-data semantics:
    //   None     — not currently in a move that has an active cell, or
    //              the classifier is disabled (chara+0x16E5 == 0).  No
    //              phase information available; treat as "neutral".
    //   Startup  — animation has begun but the hit window has not yet
    //              opened.  Hitboxes geometrically exist (overlap_test
    //              still walks them) but cannot fire reactions.
    //   Active   — the frame range during which an overlap will fire
    //              a reaction.  This is THE "hitbox is live" window.
    //   Recovery — animation continues but the hit window has closed.
    //              No further reactions can fire from this cell.
    enum class KHitAttackPhase : uint8_t
    {
        None     = 0,
        Startup  = 1,
        Active   = 2,
        Recovery = 3,
    };

    inline const char* KHitAttackPhaseName(KHitAttackPhase p) noexcept
    {
        switch (p)
        {
            case KHitAttackPhase::None:     return "None";
            case KHitAttackPhase::Startup:  return "Startup";
            case KHitAttackPhase::Active:   return "Active";
            case KHitAttackPhase::Recovery: return "Recovery";
        }
        return "?";
    }

    // ------------------------------------------------------------------
    // Global battle-state gate
    // ------------------------------------------------------------------
    // Byte/dword at 0x144846410 (RVA 0x4846410) — checked at the very
    // top of LuxBattle_ResolveAttackVsHurtboxMask22:
    //
    //   if (DAT_144846410 == 0) return;
    //
    // Set non-zero by LuxBattle_BeginBattle / cleared during pause/
    // load.  When zero, the resolver doesn't run at all on either
    // chara, so EVERY hurtbox is inert globally.  Used by the
    // visualiser as one of three engine-truth gates that compose
    // KHitDraw::defender_can_react_engine.
    namespace BattleGlobalRVAs
    {
        constexpr uintptr_t BattleRunningGate      = 0x4846410; // u32
    }

    // ------------------------------------------------------------------
    // LuxMoveLaneState — 0x468 bytes per lane, 3 lanes per chara.
    // Decoded 2026-04 via Ghidra pass on
    //   LuxMoveVM_TransitionToMove        (0x1402FEC50)
    //   LuxMoveVM_AdvanceLaneFrameStep    (0x1402FFEB0)
    //   LuxMoveVM_CommitMoveEnd           (0x1402FCFB0)
    //   LuxBattleChara_ProcessHit         (0x140342780)
    //   LuxEffectCamera_EvaluateAndTrigger
    //       SlowMotion                    (0x14031D8F0)
    //
    // The most interesting field for HorseMod is +0x08 CurrentAnimFrame
    // — a live float counter advanced each tick by
    //   +0x08 += time_dilation * +0x30
    // Integer truncation gives "current move frame N".  Combined with
    // +0x10 AnimLengthFrames this lets the overlay display "frame N/M"
    // in real time without any custom hook — a plain pointer read per
    // tick.
    // ------------------------------------------------------------------
    namespace LuxMoveLaneOffsets
    {
        constexpr uintptr_t LaneIndex              = 0x00;  // i16 (0/1/2)
        constexpr uintptr_t PackedMoveAddr         = 0x02;  // i16 ((bank<<12)|slot; -1 = idle)
        constexpr uintptr_t TickCounter            = 0x04;  // i32 raw ticks since move start
        constexpr uintptr_t CurrentAnimFrame       = 0x08;  // float (THE frame counter)
        constexpr uintptr_t PrevAnimFrame          = 0x0C;  // float (tick-start snapshot)
        constexpr uintptr_t AnimLengthFrames       = 0x10;  // float (from bank cell +0x34)
        constexpr uintptr_t AtEndFlag              = 0x1A;  // u16 (1 at final frame)
        constexpr uintptr_t FrameDeltaThisTick     = 0x1C;  // i16 (frames advanced this tick)
        constexpr uintptr_t FrameStepFinished      = 0x24;  // u16 (1 once end reached)
        constexpr uintptr_t InTransitionFlag       = 0x26;  // u16 (1 during TransitionToMove)
        constexpr uintptr_t PlaybackSpeedCurrent   = 0x30;  // float (ramps to target)
        constexpr uintptr_t PlaybackSpeedTarget    = 0x34;  // float
        constexpr uintptr_t TotalTickCounter       = 0x458; // i32 (monotonic across advances)
        constexpr uintptr_t AnimVariantIndex       = 0x460; // u32 (0..5 bank variant)
        // Stride between lanes:
        constexpr uintptr_t Stride                 = 0x468;
    }

    // ------------------------------------------------------------------
    // KHit node common header field offsets (same for all subclasses).
    // ------------------------------------------------------------------
    namespace KHitOffsets
    {
        constexpr uintptr_t Vtable            = 0x00;
        // +0x08 is a SINGLE-BIT u64 written by the deserialisers as
        //     node[+0x08] = 1ULL << (node[+0x17] & 0x3F);
        // — i.e. it is fully derived from the +0x17 slot byte.  Same
        // value produced for every subclass; the interpretation is
        // role-dependent:
        //
        //   * AttackList entries  → PerAttackerBit.  Tells the classifier
        //                           which SLOT (bit position 0..63 in the
        //                           current-move 64-bit mask) this box
        //                           contributes to.  The bit's identity
        //                           (via the strike/throw partition
        //                           0x80000080000000 vs 0xFF7FFFFF7FFFFFFF)
        //                           determines whether the box is a
        //                           strike or a throw/grab.
        //   * HurtboxList entries → PerHurtboxBit.  OR'd into the
        //                           defender's PerHurtboxBitmask at
        //                           slot (= +0x17) during the tick
        //                           aggregation, so it's the receiving
        //                           side's complement of PerAttackerBit.
        //   * BodyList entries    → PerBodyBit.  Same shape; the body
        //                           pipeline uses it for the physics
        //                           pair de-dup table at +0x44278.
        //
        // We keep aliases for historical call sites that expected a
        // "CategoryMask"-style u64; they all resolve to +0x08.
        constexpr uintptr_t PerAttackerBit    = 0x08;   // Attack role  (1<<slotIdx)
        constexpr uintptr_t PerHurtboxBit     = 0x08;   // Hurtbox role (1<<boneSlot)
        constexpr uintptr_t BoneBitFlag       = 0x08;   // Hurtbox/Body (legacy alias)
        constexpr uintptr_t CategoryMask      = 0x08;   // Attack       (legacy alias)
        constexpr uintptr_t Flags10           = 0x10;
        // +0x14 is the engine's per-frame "active" GEOMETRY gate — a
        // i16 flag written every tick from the MoveVM hot bitmap via
        //     *(int16_t*)(node + 0x14) = (hotMask >> node[+0x17]) & 1
        // at LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0.
        //
        // Crucial caveat (2026-04 walk of 0x14033CCA0): hotMask is NOT
        // just the authored per-frame mask.  It's built as:
        //
        //     hotMask = 0x3FFFD                                 // FLOOR
        //             | (animCellMask ? *animCellMask : 0)
        //             | (ownActiveCell ? *ownActiveCell : 0);
        //
        // 0x3FFFD = 0b11_1111_1111_1111_1101 = slots {0, 2, 3, 4, ...,
        // 17} forced on every frame.  So any attack-list node whose
        // +0x17 is in that set ALWAYS has +0x14 = 1 regardless of what
        // move is playing.  Only slot 1 and slots 18..63 actually
        // respect the per-move timeline.
        //
        // Practical implication for mod authors and for the overlay:
        // +0x14 is a "geometry is live" gate, not a "damage is live"
        // gate.  A hit requires BOTH:
        //   (a) node +0x14 != 0   (geometry/overlap pass)
        //   (b) the node's +0x17 slot is also set in the classifier's
        //       move-mask  *(u64*)(chara + 0x44048)[0]
        // The screen crowding of "always-on" attack boxes (feet, hands,
        // body points) comes from (a) passing via the 0x3FFFD floor
        // while (b) is quietly empty during neutral frames.
        //
        // Ground-truth reader: the OR-aggregation loops at
        // LuxBattleChara_UpdateAllKHitWorldCenters @ 0x14030D6A0
        // short-circuit on `+0x14 != 0` for BOTH attacker and defender
        // before running the overlap test:
        //
        //     for (atk  in AttackList ) if (atk [+0x14] != 0)
        //       for (hurt in HurtboxList) if (hurt[+0x14] != 0)
        //         if (overlap(atk, hurt))
        //           defender.PerHurtboxBitmask[hurt[+0x17]] |= atk[+0x08];
        //
        // Earlier revisions called this field "IsAreaFlag" (it always
        // read as 1 in the dumps we took, because we only ever dumped
        // nodes that happened to be live at the time).  That was wrong.
        constexpr uintptr_t IsActiveThisFrame = 0x14;  // i16 — live-this-frame (geometry)
        constexpr uintptr_t IsAreaFlag        = 0x14;  // legacy alias

        // Permanent hotMask floor.  Slots forced on every frame by
        // LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0
        // before the per-move mask is OR'd in.  Slots in this mask
        // always have +0x14 = 1 regardless of move state.
        constexpr uint64_t kHotMaskAlwaysOn = 0x000000000003FFFDull;
        constexpr uintptr_t StreamTypeTag     = 0x16;  // u8 (0/1/2) — see
                                                       // KHitStreamType: the
                                                       // ONLY values are
                                                       // 0=Sphere, 1=Area,
                                                       // 2=FixArea.
        // +0x17 is authored as stream byte[2] and is SIMULTANEOUSLY:
        //   - the per-slot bit index for +0x08 (1ULL << (+0x17 & 0x3F))
        //   - a bone-like id used by LuxSkeletalBoneIndex_Remap for
        //     world-centre updates and for indexing the defender
        //     PerHurtboxBitmask array in UpdateAllKHitWorldCenters.
        //
        // Role-dependent interpretation:
        //   * Attack list : SLOT INDEX (0..63).  31 and 55 are the
        //                   throw/grab slots by engine convention; all
        //                   others are strikes.  See ClassifyAttackRole.
        //   * Hurtbox list: bone slot index (0..63) into defender
        //                   PerHurtboxBitmask / PerHurtboxReactionState.
        //   * Body list   : bone slot index, used for pushbox physics.
        //
        // Special values 6 and 7:
        //   UpdateAllKHitWorldCenters switches on (+0x17 == 6) and
        //   (+0x17 == 7) to trigger a ground-clamp pass (terrain sample
        //   at XZ -> snap Y).  Gated further by per-chara frameCtx
        //   flags so only the airborne foot clamps.
        //
        // *** MAJOR SEMANTIC CORRECTION (2026-04 Ghidra pass) ***
        // +0x17 is NOT a skeletal bone id.  It's a KHit-KIND/CATEGORY
        // tag in [0, ~22).  Every site that uses it (hotMask shift,
        // PerHurtboxBitmask index, +0x08 mask computation, special-
        // case 0x16=VFX-trigger in LuxMoveVM_TransitionToMove, 0x17
        // in terrain-contact-blend) treats it as a small enum, not a
        // bone index.  The 0x3FFFD "always-on floor" selects the
        // structural kinds (pushbox + standing hurtbox + passive
        // hitbox variants); bit 1 is deliberately excluded because
        // kind-1 is the move-driven active-attack category.  That's
        // also why PerHurtboxBitmask is exactly 22 wide — one slot
        // per kind.  Historical names "SubIdOrBoneId", "bone slot",
        // "BoneIdByte" are retained for back-compat but READ them
        // as "kind tag" semantically.  LuxSkeletalBoneIndex_Remap
        // (0x140898140) is the separate, unrelated function that
        // maps kind → skeletal bone id for rendering — hence the
        // earlier confusion.
        //
        // Known-kind inventory (partial, from xref walk):
        //   0    : passive structural (in floor)
        //   1    : MOVE-DRIVEN ACTIVE ATTACK (NOT in floor)
        //   2..5 : passive hurtbox tiers (in floor)
        //   6,7  : foot-anchored hit volumes (ground-clamp)
        //   8..17: other always-on structural volumes (in floor)
        //   18..21: move-specific extensions
        //   0x16=22: VFX-trigger marker (LuxMoveVM_TransitionToMove)
        //   0x17=23: terrain-contact-blend marker (Tick...Terrain...)
        constexpr uintptr_t KindTag           = 0x17;  // u8 0..~23 (KHit kind)
        constexpr uintptr_t SubIdOrBoneId     = 0x17;  // u8 (legacy alias — actually a kind tag)
        constexpr uintptr_t BoneIdByte        = 0x17;  // u8 (legacy alias — actually a kind tag)
        constexpr uintptr_t Next              = 0x18;

        // --- KHit subclass extension fields (verified 2026-04 via Ghidra
        //     vtable map: KHitBase_vftable @ 0x143E87838,
        //     KHitSphere @ 0x143E877F0, KHitArea @ 0x143E877A8,
        //     KHitFixArea @ 0x143E87760).
        //
        // KHitSphere (stream tag 0):
        //     +0x30  FVector  BoneLocalCenter      (mirrored at +0x40)
        //     +0x50  FVector  WorldCenterCurrent   (written each frame by
        //                                          KHitSphere_UpdateWorldCenter
        //                                          @ 0x14030E1A0 using bone
        //                                          matrix at BoneIndexUe4)
        //     +0x60  FVector  WorldCenterPrev      (previous-frame copy;
        //                                          used by sweep tests)
        //     +0x70  float    Radius               (may be scaled by anim cell)
        //     +0x74  float    RadiusAuthored       (original authored radius)
        //     +0x78  float    ContactImpulseScale  (pushbox contact force)
        //     +0x7C  uint32   BoneIndexUe4         (post-Remap index into
        //                                          the chara bone matrix)
        //     +0x7F  uint8    ActiveByte           (secondary active flag)
        //
        // KHitArea (stream tag 1) — SWEPT CAPSULE, uses double-buffer for
        // continuous-collision-detection across frames:
        //     +0x30  FVector  BoneLocalP1
        //     +0x40  FVector  BoneLocalP2
        //     +0x50..+0x6F    WorldSpaceBufA (P1, P2)
        //     +0x70..+0x8F    WorldSpaceBufB (P1, P2)
        //                     g_LuxKHitArea_DoubleBufferToggle selects
        //                     which half is cur vs prev each tick.  The
        //                     OverlapTest does 4-way segment/segment CCD
        //                     across both halves.
        //     +0x90  float    ContactImpulseScale
        //     +0x94  uint32   BoneIndexUe4_P2
        //     (BoneIndexUe4_P1 is written post-init by the deserializer.)
        //
        // KHitFixArea (stream tag 2) — STATIC OBB (no CCD):
        //   *** 2026-04 correction: these are NOT a 3×3 basis ***
        //   The deserializer writes each row as (vec3, 1.0f) — homogeneous
        //   POINTS, not direction vectors.  KHitFixArea_UpdateWorldCenter
        //   applies the full affine bone transform (including translation)
        //   to each, confirming point semantics.  The OBB is derived at
        //   overlap-test time in LuxBattle_BuildHitboxLocalMatrix by
        //   Gram-Schmidting `(P2-P1)` and `(P3-P1)` — i.e. P1/P2/P3 are
        //   three reference points describing the hitbox shape.
        //
        //     +0x30  FVector  BoneLocalPoint1   (P1 = origin / near corner)
        //     +0x40  FVector  BoneLocalPoint2   (P2 = far end along primary axis)
        //     +0x50  FVector  BoneLocalPoint3   (P3 = side reference for
        //                                        orthogonal axis)
        //     (+0x3C, +0x4C, +0x5C are all 1.0f homogeneous w)
        //     +0x60  FVector  WorldPoint1       (transformed P1)
        //     +0x70  FVector  WorldPoint2       (transformed P2)
        //     +0x80  FVector  WorldPoint3       (transformed P3)
        //     +0x90  uint32   BoneIndexUe4      (single bone idx)
        //     +0x94  float    ContactImpulseScale
        //
        // To render a correct OBB:
        //   X = normalize(WP2 - WP1)                    // primary axis
        //   sideRaw = WP3 - WP1
        //   Y = normalize(sideRaw - dot(sideRaw,X)*X)   // orthogonalised
        //   Z = cross(X, Y)
        //   lenX = |WP2 - WP1|
        //   lenY = lenZ = |sideRaw - dot(sideRaw,X)*X|  // game uses square
        //                                               // cross-section
        // Quicker alternative: draw two lines — WP1→WP2 (spine) and
        // WP1→WP3 (side).  Matches authored intent.
        //
        // HorseMod rendering uses these offsets to draw the authored
        // shapes — sphere at +0x50/r=+0x70, capsule endpoints at
        // +0x50/+0x58 (cur P1/P2), OBB basis at +0x60/+0x70/+0x80.
        constexpr uintptr_t SphereBoneLocalCenter   = 0x30;  // FVector
        constexpr uintptr_t SphereWorldCenterCur    = 0x50;  // FVector
        constexpr uintptr_t SphereWorldCenterPrev   = 0x60;  // FVector
        constexpr uintptr_t SphereRadius            = 0x70;  // float
        constexpr uintptr_t SphereRadiusAuthored    = 0x74;  // float
        constexpr uintptr_t SphereBoneIndexUe4      = 0x7C;  // uint32

        constexpr uintptr_t AreaBoneLocalP1         = 0x30;  // FVector
        constexpr uintptr_t AreaBoneLocalP2         = 0x40;  // FVector
        constexpr uintptr_t AreaWorldBufA           = 0x50;  // FVector[2]
        constexpr uintptr_t AreaWorldBufB           = 0x70;  // FVector[2]
        constexpr uintptr_t AreaBoneIndexUe4_P2     = 0x94;  // uint32

        // FixArea offsets — three reference POINTS in bone-local space
        // (each with 1.0f homogeneous w at +Nc), then their world-space
        // transforms, a bone idx, and a contact impulse scale.  See the
        // OBB derivation formula in the comment block above.
        constexpr uintptr_t FixAreaBoneLocalP1      = 0x30;  // FVector
        constexpr uintptr_t FixAreaBoneLocalP2      = 0x40;  // FVector
        constexpr uintptr_t FixAreaBoneLocalP3      = 0x50;  // FVector
        constexpr uintptr_t FixAreaWorldP1          = 0x60;  // FVector
        constexpr uintptr_t FixAreaWorldP2          = 0x70;  // FVector
        constexpr uintptr_t FixAreaWorldP3          = 0x80;  // FVector
        constexpr uintptr_t FixAreaBoneIndexUe4     = 0x90;  // uint32

        // Each KHit node is 0x80 (128) bytes — verified empirically:
        //   node->next - node == 0x80 exactly in the scratch pool.
        //
        // Layout (Ghidra-confirmed via KHitSphere_UpdateWorldCenter @
        // 0x14030E1A0 and KHitArea_UpdateWorldCenters @ 0x14030E480):
        //
        //     +0x00  vtable
        //     +0x08  PerAttackerBit (u64, 1ULL << (slot & 0x3F))
        //     +0x10  Node_Flags10   (u32)  AUTHORED, write-only — see the
        //                                  block at ~line 138 above. DO NOT
        //                                  use this for classification or
        //                                  visibility gating.
        //     +0x14  ActiveThisFrame(u16)  GeometryActiveGate — written per
        //                                  frame from MoveVM hotMask; 0 = the
        //                                  node is skipped by the overlap
        //                                  loop on both attacker and
        //                                  defender.
        //     +0x16  StreamTypeTag  (u8, 0=Sphere 1=Area 2=FixArea)
        //     +0x17  SubIdOrBoneId  (u8, slot 0..63, pre-remap bone id for
        //                                defender nodes; attack slot for
        //                                attack nodes)
        //     +0x18  Next           (KHit*, null-terminates list)
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
        //
        // *** WARNING: these offsets are SPHERE-ONLY. ***
        // For KHitArea  (tag 1): +0x30/+0x40 are bone-local P1/P2 (OBB
        //     diagonal corners in bone frame); +0x50..+0x8F is the
        //     double-buffered world P1/P2 pair for swept CCD.
        // For KHitFixArea (tag 2): +0x30/+0x40/+0x50 are THREE bone-local
        //     REFERENCE POINTS (each with w=1.0f homogeneous padding);
        //     +0x60/+0x70/+0x80 are their world-space transforms.  The
        //     OBB is derived at overlap time by Gram-Schmidt over
        //     (P2-P1) and (P3-P1) — there is no stored centre/extents
        //     on a FixArea.  Reading +0x40 as "MirrorOrExtents" on a
        //     FixArea produces garbage.  Use the subclass-specific
        //     offsets below.
        constexpr uintptr_t LocalCenter       = 0x30;  // vec3 bone-local (SPHERE)
        constexpr uintptr_t MirrorOrExtents   = 0x40;  // vec3 (SPHERE mirror only)
        constexpr uintptr_t WorldCenterCur    = 0x50;  // vec3 Namco-world current (SPHERE)
        constexpr uintptr_t WorldCenterPrev   = 0x60;  // vec3 Namco-world previous (SPHERE)
        constexpr uintptr_t Radius            = 0x70;  // float Lux units (SPHERE)

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

    // Box         — 8 corners (KHitArea — bone-local AABB rotated by bone).
    // Sphere      — centre + radius (KHitSphere).
    // FixAreaTri  — KHitFixArea's 3 reference points (P1/P2/P3 world).
    //               Drawn as two lines (spine P1→P2, side P1→P3) to
    //               show the authored spine + side-reference directly,
    //               matching the game's own BuildHitboxLocalMatrix
    //               construction.  See buildFixAreaWorld() + the
    //               KHitOffsets::FixAreaWorldP1/2/3 doc block.
    enum class KHitKind : uint8_t { Box = 0, Sphere = 1, FixAreaTri = 2 };
    enum class KHitList : uint8_t { Attack = 0, Hurtbox = 1, Body = 2 };

    // ------------------------------------------------------------------
    // Engine-derived role for an Attack-list entry.
    //
    // Source of truth: LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100.
    // The classifier splits the 64-slot attacker-mask space into two
    // disjoint regions:
    //
    //   Strike      — any bit in 0xFF7FFFFF7FFFFFFF  (all bits except 31, 55).
    //                 Resolved per-hurtbox; produces Hit / Block / MH / etc.
    //   Throw/Grab  — any bit in 0x80000080000000   (bits 31 and 55 only).
    //                 Pre-scanned as an all-or-nothing gate before the
    //                 strike loop runs.
    //
    // An Attack-list node's +0x08 is a SINGLE bit (1ULL << (+0x17 & 0x3F)),
    // so a per-node classification is: "which side of the partition does
    // my one bit fall on?".  Equivalently: does my +0x17 slot index equal
    // 31 or 55?  Either question produces the same answer.
    //
    // `NotAttack` is used for Hurtbox / Body list entries so the UI code
    // can gate by role without worrying about the list kind.
    //
    // An attack whose mask is zero (shouldn't happen on a live attack) is
    // treated as Strike by default — it will fail every classifier branch
    // and produce no reactions, but we still draw it as an attack box.
    // ------------------------------------------------------------------
    enum class KHitAttackRole : uint8_t
    {
        NotAttack = 0,  // hurtbox / body list entry
        Strike    = 1,  // mask & 0xFF7FFFFF7FFFFFFF != 0
        Throw     = 2,  // mask & 0x0080000080000000 != 0 (grab / throw)
    };

    // Bit masks for the attack-role partition.  See plate comment on
    // LuxBattle_ResolveAttackVsHurtboxMask22 for how the classifier uses
    // these.
    constexpr uint64_t kAttackRoleThrowMask  = 0x0080000080000000ull;
    constexpr uint64_t kAttackRoleStrikeMask = 0xFF7FFFFF7FFFFFFFull;

    // Classify an attack node's CategoryMask into a role.  Throw bits
    // take priority — if either grab bit is set, we call it a throw
    // regardless of strike-region content (matches the classifier's
    // pre-scan behaviour which bails out of the strike loop on any
    // throw bit).
    inline KHitAttackRole ClassifyAttackRole(uint64_t categoryMask)
    {
        if (categoryMask & kAttackRoleThrowMask)  return KHitAttackRole::Throw;
        if (categoryMask & kAttackRoleStrikeMask) return KHitAttackRole::Strike;
        // Mask is all zero — unusual but not fatal.  Default to Strike so
        // the overlay still renders the box.
        return KHitAttackRole::Strike;
    }

    struct KHitDraw
    {
        KHitKind    kind;
        KHitList    list;
        // True when this attack node passes the engine's GEOMETRY gate
        // at node+0x14 (set every tick by `(hotMask >> node[+0x17]) & 1`
        // at 0x14033CCA0).  NB: hotMask has a permanent floor of
        // 0x3FFFD so every attack with +0x17 in slots {0, 2..17} is
        // "is_current_attack == true" every single frame, even during
        // neutral.  This gate controls whether overlap testing runs;
        // it does NOT mean the attack is currently causing damage.
        // For the damage-live filter see `is_damage_active` below.
        //
        // Always false for hurtbox / body entries.
        bool        is_current_attack;

        // True when this attack node's +0x17 slot bit is ALSO set in
        // this chara's own active-attack-cell mask
        // (*(u64*)(chara+0x44058))[0].  That mask is set ONCE PER
        // MOVE-SLOT (in LuxMoveVM_SetActiveMoveSlot @ 0x140300c70) so
        // it stays constant across the whole move duration including
        // pre-damage startup AND post-damage recovery — which means
        // this filter shows hitboxes as "active" through the entire
        // move, not just the damage frames.  Always false for
        // hurtbox / body entries.
        //
        // For a strictly tighter "actually dealing damage THIS frame"
        // signal, use `is_per_frame_active` below instead.
        bool        is_damage_active;

        // True when the engine considers this attack node currently
        // capable of producing damage — the predicate the classifier
        // (ResolveAttackVsHurtboxMask22 @ 0x14033C100) uses to decide
        // whether overlap should fire a hit, AND'd with the engine's
        // own per-tick frame-window phase tag at chara+0x1980:
        //
        //   is_per_frame_active = (node[+0x14] != 0)                  // geom-hot
        //                      && ((node.CategoryMask & per_move_cell) != 0)
        //                      && (chara->FrameWindowPhase == Active)  // 2026-05
        //
        // *** PHASE TAG ADDITION (2026-05) ***
        // Earlier versions of this filter combined only the +0x14
        // geom-hot gate and the slot-mask intersection.  Both of those
        // are set PER-MOVE-SLOT, not per-game-frame, so the filter
        // showed hitboxes as "active" through the entire move
        // including STARTUP and RECOVERY frames.  That doesn't match
        // the fighting-game frame-data sense of "active frames" that
        // every move's hit window has.
        //
        // The engine itself partitions the move into 1=Startup /
        // 2=Active / 3=Recovery via cell+0x36 / cell+0x38 (Master
        // WindowStart / End), and writes the result into
        // chara+0x1980 every tick.  AND'ing in (phase == Active)
        // narrows the filter to ONLY the authored hit-window frames —
        // i.e. exactly when an overlap would fire a reaction.
        //
        // The per-move cell at **chara+0x44058 has TWO simultaneous
        // interpretations in the engine:
        //   * For the +0x14 hot gate: bit S = "slot S is hot this
        //     frame" — added on top of the 0x3FFFD floor and any
        //     per-frame sub-cell.
        //   * For the damage classifier: bit C = "category C is
        //     active this frame" — ANDed with the attack node's
        //     authored CategoryMask at node[+0x08] to decide if
        //     overlap → damage.
        //
        // Same 64 bits, different semantic.  This filter intersects
        // the attack's CategoryMask with the cell, mirroring the
        // classifier path.  That correctly handles both:
        //   * Floor-slot attacks (slots 0, 2..17 — body-attached
        //     hitboxes whose +0x14 is always set by the floor).
        //     Hidden during neutral (cell == 0); shown during moves
        //     whose categories overlap this node's authored CategoryMask
        //     AND only during the Active frames of those moves.
        //   * Non-floor attacks (slot >= 18).  +0x14 already requires
        //     the slot bit in the cell, so the category intersection
        //     is the additional "and our move's flavor matches" check;
        //     the phase tag is the additional "and we're in active
        //     frames" check.
        //
        // History of this filter (chronological):
        //   v1: only *sub_frame_cell — empty for simple moves, hid
        //       every attack.
        //   v2: +0x14 minus 0x3FFFD floor — hid the floor-slot
        //       attacks (body-attached hitboxes).
        //   v3: +0x14 != 0 AND (cat_mask & ownAttackMask) != 0 —
        //       worked, but spanned all three phases (the "active
        //       through entire move" bug).
        //   v4 (current): v3 AND (phase == Active) — narrows to
        //       authored hit-window frames only.
        //
        // Always false for hurtbox / body entries.
        bool        is_per_frame_active;

        // ==== Engine frame-window state (chara-side, NOT per-node) ====
        // The next four fields are stamped onto every KHitDraw produced
        // for a given chara on a given tick (they're constant across
        // every node on that chara that frame).  They mirror what
        // LuxMoveVM_ClassifyHitboxFrameState wrote into chara+0x1980,
        // chara+0x16EA, and the active cell's +0x36 / +0x38.

        // Engine-truth attack phase.  None when the chara has no active
        // cell to classify against; otherwise Startup / Active / Recovery
        // per the cell's MasterWindow vs lane->CurrentAnimFrame.  Use
        // this to render hitboxes differently per phase (e.g. dim during
        // startup, bright during active, fade during recovery), or to
        // gate visibility uniformly on `phase == Active`.
        //
        // Stamped on EVERY KHitDraw including hurtboxes / body — same
        // value for every node on the same chara this tick.  Useful for
        // the HUD's "ACTIVE" / "STARTUP" / "RECOVERY" text indicator.
        KHitAttackPhase engine_phase = KHitAttackPhase::None;

        // Boolean mirror of (engine_phase == Active) AND the engine's
        // sub-window inhibitors are quiet (chara+0x16EB == 0 &&
        // chara+0x16FE == 0).  Read from chara+0x16EA.  Strictly
        // identical to phase==Active in normal play; differs only
        // when the chara is hit-cancelled mid-move (inhibitors set,
        // phase still says Active because curFrame is still in
        // window).  HUD active indicators should prefer this for
        // exact engine-truth fidelity.
        bool        in_master_window = false;

        // MasterWindow start/end frames from the active cell (+0x36/+0x38).
        // 0/0 when no cell is active.  These are i16 in animation-frame
        // units (60 Hz).  Useful for HUD displays like
        // "frame N (active 14-16)".
        int16_t     master_window_start = 0;
        int16_t     master_window_end   = 0;

        // Raw `node[+0x14] != 0` for any list kind.  For ATTACK nodes
        // this is the GeometryActiveGate rewritten every tick from the
        // MoveVM hotMask by LuxBattle_TickHitResolutionAndBodyCollision
        // (0x14033CCA0).  For HURTBOX / BODY nodes the per-frame update
        // loop in that function does NOT iterate their lists at all —
        // +0x14 keeps whatever KHitChk_InitSphereFromStream /
        // InitAreaFromStream wrote at deserialize time (always 1).
        // Accordingly, `geom_active` is a useful "is this attack live?"
        // signal but CANNOT be used to tell whether a hurtbox is hittable
        // — every hurtbox reports geom_active==true.  For the real
        // "hittable by classifier?" predicate on the defender side,
        // see `classifier_addressable` below.
        bool        geom_active;

        // True iff this hurtbox's SubIdOrBoneId (+0x17) is inside the
        // classifier's iteration range — i.e. < ClassifierHurtboxBound
        // (chara+0x44494, clamped to 22).  The classifier at
        // LuxBattle_ResolveAttackVsHurtboxMask22 (0x14033C100) only
        // iterates slots 0..bound-1:
        //
        //     for (slotIndex = 0; slotIndex < bound; ++slotIndex)
        //         if (PerHurtboxBitmask[slotIndex] & attackerMask & strikeMask)
        //             ... write PerHurtboxReactionState[slotIndex] ...
        //
        // UpdateAllKHitWorldCenters still performs the overlap test and
        // OR's `atk->PerAttackerBit` into
        // `PerHurtboxBitmask[hurt->+0x17]`, but if that index is >=
        // bound the classifier never reads it, so no reaction is
        // produced and no damage is applied.  Visually this manifests
        // as a hurtbox that looks geometrically alive
        // (geom_active==true) yet never reacts to being struck — the
        // tell-tale of a "meta" hurtbox authored at slot >= 22 or
        // beyond the per-character bound.
        //
        // *** IMPORTANT gotcha: the "bound" at chara+0x44494 is the
        // defender's own *attack* list max-slot (ATTACK stream's
        // pOutMaxSlot), not the hurtbox list's max-slot.  See
        // ChaOffsets::ClassifierHurtboxBound for the full derivation.
        // Consequence: during moves with few attack slots but many
        // hurtbox slots (dodges, pure-movement, block, throw-whiff)
        // the bound can be smaller than the hurtbox list actually
        // needs, and hurtboxes at the tail get flagged unaddressable.
        // That's an honest reflection of engine behavior — those
        // hurtboxes really won't be rolled into a reaction — but it
        // means this flag is NOT "is this hurtbox geometrically live?"
        // (use geom_active for that).  It's strictly "will the
        // classifier ever read this slot?".
        //
        // Always true for attack and body nodes (the concept does not
        // apply — only the hurtbox list participates in the
        // slot-count-bounded classifier iteration).
        bool        classifier_addressable = true;

        // Engine-side overlap gate.  True iff the OVERLAP TEST in
        // LuxBattleChara_UpdateAllKHitWorldCenters (0x14030D6A0)
        // will actually consider this node when iterating
        // attacker x defender pairs:
        //
        //     for (atk in AttackList)
        //       if (*(short*)(atk+0x14) != 0)             ← attacker gate
        //         for (hurt in HurtList)
        //           if (*(short*)(hurt+0x14) != 0)        ← defender gate (THIS)
        //             if (overlap_test(...))
        //               PerHurtboxBitmask[hurt+0x17] |= atk[+0x08];
        //
        // For ATTACK nodes this is the per-frame "hot" flag the
        // engine writes every tick from `(hotMask >> +0x17) & 1`.
        //
        // For HURTBOX / BODY nodes this is the VM-controlled gate.
        // The hurt's +0x14 starts at whatever the move's hurtbox
        // stream deserializer wrote at load time, and is REWRITTEN
        // by the move-script VM via opcode 0x13AC dispatched in
        // LuxMoveVM_DispatchEffectOp (0x14037A160), which calls
        // LuxMoveVM_SetHurtboxSlotsActiveMask (0x140308D70) with a
        // 23-bit mask:
        //     for slot 0..22:
        //         if (slot < chara+0x444B4 / HurtboxMaxSlot)
        //             for (n = HurtList; n; n = n->next)
        //                 if (n[+0x17] == slot)
        //                     n[+0x14] = ~(mask & 1) & 1;
        //         mask >>= 1;
        //
        // Hurtboxes with +0x14 = 0 are NOT vestigial — they are
        // CONDITIONALLY-ACTIVE, slot-gated extended-reach hurtboxes
        // authored as default-off.  The per-move VM script flips
        // them on for specific frames (lunging stabs, parry
        // windows, soul-charge cancels, etc.) and back off after.
        // Their slot index +0x17 typically lies BEYOND the dual-
        // role bound at chara+0x44494 (the AttackMaxSlot the
        // classifier reuses as its hurtbox iteration ceiling), so
        // they only contribute to damage when both
        //   (a) +0x14 has been flipped on by the VM, AND
        //   (b) chara+0x44494 has been bumped wide enough to cover
        //       their slot — usually by the same per-move data
        //       that armed them.
        //
        // EMPIRICAL (2026-04-30): Geralt has two large rectangle
        // hurtboxes authored with +0x14 = 0.  They are off during
        // neutral / standing / blocking — invisible to the damage
        // classifier — and the per-move VM enables them for
        // specific Geralt moves where the extended reach is needed.
        // HorseMod renders them in CYAN to flag the VM-gated state.
        //
        // Identical to `geom_active` for the underlying value, but
        // semantically tagged for the hurtbox-list use case.  Kept
        // as a separate field so callers don't have to remember
        // that geom_active doubles as a hittability gate for hurts.
        bool        overlap_active = true;

        // True iff this is a HURTBOX node whose slot has been
        // explicitly DISABLED by the move-script's VM opcode 0x13AC
        // (LuxMoveVM_SetHurtboxSlotsActiveMask @ 0x140308D70).
        // i.e.:
        //   list == Hurtbox
        //   AND classifier_addressable == true (slot < AttackMaxSlot)
        //   AND overlap_active == false        (node+0x14 == 0)
        //
        // This is the structural marker for "this hurtbox slot is in
        // an i-frame / armor / parry window THIS TICK".  Distinct from
        // the cyan default-OFF case because that requires
        // !classifier_addressable; this requires the slot to be in
        // classifier range AND turned off.
        //
        // Always false for attack and body nodes (VM opcode 0x13AC
        // only affects the hurtbox list; the BODY-list sister opcode
        // 0x27 would touch body nodes but we don't currently expose
        // that as a separate field — body pushboxes are a physics
        // concern, not damage).
        //
        // For a chara-wide "this entire chara is in i-frames right now"
        // signal, see KHitWalker::HurtboxInvulState (call
        // readHurtboxInvulState(chara) once per tick).
        bool        is_invul_slot = false;

        // Chara-wide "the engine can fire reactions on this chara
        // this frame" gate.  Composed from three early-return
        // sites in LuxBattle_ResolveAttackVsHurtboxMask22:
        //   * Battle running          (DAT_144846410 != 0)
        //   * Not incapacitated/dead  (chara+0x20B8 == 0)
        //   * Not in no-react state   (chara+0x19B0 != 6)
        //
        // When false, the resolver early-returns BEFORE iterating
        // ANY hurtbox slot, so every hurtbox on this chara is
        // engine-inert regardless of its own +0x14 / slot index /
        // category mask.  Stamped once per chara walk; identical
        // value across every KHitDraw produced for this chara on
        // this tick.
        //
        // Examples of when this is false:
        //   * Round-end "round X! WIN!" cinematic — chara is
        //     visible but engine doesn't pump hits.
        //   * KO — defender sails through reaction frames; hurtbox
        //     list still rendered but won't take a fresh hit.
        //   * Pause / load / battle-not-yet-ready — global gate.
        //
        // The composite Geralt-style "is this hurtbox actually
        // hittable this frame" predicate becomes:
        //
        //   hittable = classifier_addressable
        //           && overlap_active
        //           && defender_can_react_engine;
        //
        // Used by the dllmain "Only show active boxes" filter as
        // an additional clause OR'd with the existing two — see
        // the toggle composition table in dllmain.cpp.
        bool        defender_can_react_engine = true;

        // Per-chara "this chara's attack-list is engine-actionable"
        // gate.  Same chara-wide gates as defender_can_react_engine
        // but applied to ATTACK boxes — an incapacitated / round-
        // ended chara can't deal damage either, so attack boxes
        // that LOOK live (their +0x14 is set, their slot bit is
        // active) are also engine-inert.  Stored as the same
        // boolean source for both lists; the filter uses it to
        // gate attack rendering via is_per_frame_active.
        //
        // Pragmatically equal to defender_can_react_engine — both
        // mean "the chara is in a frame where the engine pumps
        // hits on it" — but kept as a separately-named field for
        // self-documenting filter code.
        bool        attacker_can_strike_engine = true;

        bool        reaction_hot;        // hurtbox: PerHurtboxReactionState != 0
        uint32_t    flags10;             // +0x10 — Node_Flags10 (AUTHORED,
                                         // write-only metadata; see KHitBase
                                         // header doc block above. Captured
                                         // here only for hex-dump / debug —
                                         // runtime pipeline never reads it,
                                         // so do not treat it as semantic.)
        uint8_t     stream_tag;          // 0=sphere, 1=area, 2=fixarea
        uint8_t     bone_id_internal;    // raw BoneId byte pre-remap
        // Hurtbox classifier slot — the node's +0x17 (SubIdOrBoneId) when
        // in range [0,22), else -1.  This is the index used to look up
        // PerHurtboxReactionState / PerHurtboxBitmask in chara memory.
        // NOT the position in the linked-list walk — see the walker
        // assignment site for rationale.  -1 for non-hurtbox nodes.
        int         hurtbox_slot;

        // Raw u64 at node+0x08.  Meaning depends on `list`:
        //   Attack  → CategoryMask (what bits trigger what reactions)
        //   Hurt    → BoneBitFlag
        //   Body    → BoneBitFlag
        uint64_t    category_or_bone_mask = 0;

        // Engine-derived role for Attack-list entries.  Always NotAttack
        // for Hurtbox / Body so the UI can gate uniformly.
        KHitAttackRole attack_role = KHitAttackRole::NotAttack;

        // Defender-side reaction value for this hurtbox slot — the actual
        // enum value written by the classifier (0=None, 1=Hit, 2=BlockedLow,
        // 3=BlockedHigh, 4=MH_Loser, 6=Tech, 8=MH_Winner, 9=AirHit,
        // 10=MH_Trade, 0xB=WallSplat, 0xC=Stagger).  0 for non-hurtbox
        // entries.  `reaction_hot` is just (reaction_state != 0), extended
        // by the sticky-flash window.
        int32_t     reaction_state = 0;

        // Engine-truth Area geometry (kind == Box).
        // ============================================
        // The engine's KHitArea overlap test (KHitArea::OverlapTest @
        // 0x14030E4E0 → LuxBattle_TestPointInHitboxShape @ 0x14030C660 /
        // _TestSegmentHitsHitboxShape @ 0x14030C110) treats P1 and P2
        // as the TWO ENDPOINTS OF A 1D SPINE, NOT diagonal corners of
        // an OBB.  The shape it tests against is built at overlap time
        // by LuxBattle_BuildHitboxLocalMatrix @ 0x14030BBA0 from THREE
        // world-space points (P1, P2, P3) — primary axis from P1→P2,
        // secondary axis = orthogonal component of P3-P1.  The CROSS-
        // SECTION thickness comes from how far P3 sits off the spine,
        // and for an Area attacker P3 is always taken from the OTHER
        // FRAME's endpoint pair (cur uses prev_P2, prev uses cur_P1).
        // So a stationary Area has near-zero cross-section, and a
        // moving Area picks up cross-section thickness directly from
        // its motion delta.
        //
        // PopulateOverlapScratch builds TWO of these OBBs per tick:
        //     OBB-1 = (cur_P1,  cur_P2,  prev_P2)   — current spine, prev tip ref
        //     OBB-2 = (prev_P1, prev_P2, cur_P1 )   — previous spine, cur hilt ref
        // The defender shape is tested against both; an overlap with
        // EITHER counts as a hit.  Together these two OBBs cover the
        // swept quad spanning prev and cur spines.
        //
        // What HorseMod renders
        // ---------------------
        // We do NOT render the 8-corner OBB the engine constructs at
        // test time — that's a derived shape with motion-dependent
        // thickness; rendering it as a static box would mis-represent
        // any frame where motion is non-uniform.  Instead, we render
        // the SOURCE DATA the engine reads:
        //
        //   spine_p1_world, spine_p2_world   — cur-frame endpoints,
        //                                       always populated.
        //   prev_p1_world, prev_p2_world     — prev-frame endpoints,
        //                                       populated iff has_prev_
        //                                       spine == true (i.e. the
        //                                       per-node cache had a
        //                                       valid one-tick-old
        //                                       snapshot for THIS node).
        //
        // The draw routine renders the cur spine as a line, the prev
        // spine as a line in the same colour, and the two connecting
        // edges (cur_P1→prev_P1, cur_P2→prev_P2) to form the swept
        // quad outline.  Stationary: prev≈cur, the quad collapses to
        // a single line (correct — engine's effective volume is just
        // the spine extruded by the defender's radius).  Moving: the
        // quad opens up, visually communicating the swept envelope
        // the engine actually hit-tested this tick.
        //
        // Sphere and FixAreaTri kinds use the legacy corners[8] +
        // centre/radius fields below; the spine fields are valid
        // only for Box kind.
        FVec3       spine_p1_world{};   // cur-frame P1 in UE world
        FVec3       spine_p2_world{};   // cur-frame P2 in UE world
        FVec3       prev_p1_world{};    // prev-frame P1 (valid iff has_prev_spine)
        FVec3       prev_p2_world{};    // prev-frame P2 (valid iff has_prev_spine)
        bool        has_prev_spine = false;

        // Legacy 8-corner OBB slot — retained for FixAreaTri/Sphere
        // paths and as scratch space.  No longer driven by buildArea
        // World; the Box kind now uses spine_p?_world above.
        FVec3       corners[8];

        // Sphere geometry (kind == Sphere).
        FVec3       centre;
        float       radius;

        // ==== Active attack cell metadata (chara+0x44058 -> cell) ====
        // The currently-active LuxBattleAttackCell on THIS chara — the
        // same cell pointer that LuxBattle_ResolveAttackVsHurtboxMask22
        // (0x14033C100) reads (via the opponent's +0x44048 copy) and
        // hands to LuxMoveVM_EvaluateMoveTransition (0x14033E140) to
        // decide block/hit/whiff per slot.
        //
        // Both fields are stamped on EVERY KHitDraw produced for this
        // chara this tick (same value across every node).  For hurtbox
        // / body entries the values still reflect THIS chara's own
        // active cell — i.e. "if THIS chara strikes someone right now,
        // these are the flags they'd use".  Consumers that want the
        // attack-tier overlay on attack boxes can simply gate on
        // `list == Attack`.
        //
        // attack_flags = cell+0x32 (u16 bitfield, see KHitAttackFlagBits)
        // attack_input_cond = cell+0x34 (u16, EvaluateMoveInputCondition mask)
        // attack_tier = ClassifyAttackTier(attack_flags) for convenience
        //
        // 0 / Unknown when no cell is active (idle / non-attacking move).
        uint16_t        attack_flags      = 0;
        uint16_t        attack_input_cond = 0;
        KHitAttackTier  attack_tier       = KHitAttackTier::Unknown;

        // ==== Defender-side stance state (per-chara, per-tick) ====
        // These mirror the chara-state bytes EvaluateMoveTransition
        // (0x14033E140) reads when classifying an incoming hit.  Stamped
        // onto every KHitDraw on this chara so the overlay can show
        // "this chara is currently CROUCHING / BLOCKING / IN HITSTUN".
        //
        // defender_crouching    = chara+0x16D2 (base crouch state) OR
        //                         chara+0x16FC (alt) depending on chara+0x1701.
        //                         When set, EvaluateMoveTransition routes
        //                         through the "crouching defender" branch
        //                         (LowBlockable bit is the relevant gate).
        // defender_in_blockstun = chara+0x16DC (i8 != 0)
        // defender_in_hitstun   = chara+0x16DB (i8 != 0)
        // defender_actively_blocking = chara+0x16FD (i8 != 0).  Combined
        //                              with defender_blockable_this_frame
        //                              to drive the block-vs-hit decision.
        // defender_blockable_this_frame = chara+0x16F2 (u8, written by
        //                                 ResolveAttackVsHurtboxMask22
        //                                 from chara+0x16D1 / +0x16FD /
        //                                 +0x15AC / opp+0x2110).
        bool        defender_crouching             = false;
        bool        defender_crouching_base        = false;
        bool        defender_crouching_alt         = false;
        bool        defender_alt_lock_gate         = false;
        bool        defender_actively_blocking     = false;
        bool        defender_blockable_this_frame  = false;
        bool        defender_in_blockstun          = false;
        bool        defender_in_hitstun            = false;
        bool        defender_invul_a               = false;
        bool        defender_invul_b               = false;
        bool        defender_guard_broken          = false;
        bool        defender_guard_disabled        = false;
        bool        defender_force_allow           = false;

        // ==== Throw-dispatch height gate (per chara, per tick) ====
        // The engine's *geometric* hit pipeline (UpdateAllKHitWorldCenters +
        // ResolveAttackVsHurtboxMask22 throw pre-scan) DOES register a
        // throw when the cell's slot mask bit 31/55 is set and the cell's
        // AttackFlags pass EvaluateMoveTransition.  But the reaction-
        // DISPATCH layer (LuxMoveVM_TickPickAndDispatchReaction @
        // 0x1402DEF50) has a separate height-bucket gate that can silently
        // drop the post-effect yarare-id before any animation plays.
        //
        // Gate (verified in TickPickAndDispatchReaction):
        //   iSelf = GetCharaEffectiveHeight(defenderChara)
        //   iOpp  = GetCharaEffectiveHeight(attackerChara)
        //   skip_weight_pick = ((iOpp > 2 || iOpp == 0) && iSelf > 4)
        //   final_allow      = (iSelf < 5)
        //                    OR yarareId in {0x1F, 0x21, 0x20, 0x2C, 0x2D,
        //                                    0x2E..0x31, 0x3B, 0xE..0x10}
        //                    OR (intensity > 1 && yarareId in 0x28..0x2B)
        //                    OR (MoveStateId == 3 && chara+0x1982 != 0)
        //
        // For visualisation HorseMod surfaces:
        //   defender_effective_height  — iSelf (this chara's height bucket)
        //   attacker_effective_height  — iOpp (this chara's opponent height
        //                                via chara+0x973E8)
        //   throw_height_gate_ok       — `defender_effective_height < 5`,
        //                                the simple "would a normal throw
        //                                dispatch" predicate.  False when
        //                                the defender is tall and the
        //                                yarareId isn't in the allow-set —
        //                                exactly the "boxes touch but
        //                                throw whiffs" symptom.
        //
        // Same value stamped on every KHitDraw produced for this chara on
        // this tick.  When the defender is THIS chara, `throw_height_gate_ok`
        // tells you whether an incoming throw from the opponent would land.
        int32_t     defender_effective_height  = 0;
        int32_t     attacker_effective_height  = 0;
        bool        throw_height_gate_ok       = true;

        // ==== Final chara-wide hit-reaction result code (chara+0x43DA0) ====
        // Written by LuxBattle_ResolveAttackVsHurtboxMask22 + mutual-hit
        // arbitration each tick.  Single int that consolidates the per-slot
        // reaction outcome.  Switch values verified 2026-05-16 against the
        // ProcessHitReactionState @ 0x140342FF0 switch + ProcessHit's own
        // code-1 handling:
        //   0     no reaction this tick
        //   1     BLOCKED HIT (guard).  NOT "KO" — an earlier table mislabel.
        //         ProcessHitReactionState case 1 runs the per-hit stat
        //         counter (LuxBattle_RecordRoundWinStats — a misnomer; it
        //         is a 12-bucket hit-stat accumulator, not round-win logic)
        //         and the air-block terminate path.  ProcessHit groups
        //         code 1 with 0xB for TriggerActionState 100 (hit-response
        //         state) and with 0x13 for the stun-delta damage calc.
        //   4     standard hit (mutual-hit winner kicker)
        //   5     hit + counter-hit kicker
        //   6     standard hit (most common strike path; ProcessHitReactionState
        //         case 6 = the heavy per-hit-flag writer)
        //   7     counter-hit / tech special
        //   8     mutual-hit — winner, 1 category over loser (arbitration)
        //   9     mutual-hit — clash (cat 0) / winner dominates (arbitration)
        //   10    mutual-hit — trade (arbitration)
        //   0xB   Guard-Impact Crush
        //   0xC   air / guard-cancel (engine plate marks UNCERTAIN — verify)
        //   0xD,0xF  no-op pass-through cases in ProcessHitReactionState
        //   0xE   chip-only damage (ProcessHit has an explicit `== 0xe`
        //         branch; ProcessHitReactionState treats it as no-op)
        //   0x12  Air hit
        //   0x13  Wall splat
        //   0x14  Stagger
        //
        // The per-slot `reaction_state` field captures the classifier's
        // per-hurtbox-slot writes; this complementary field captures the
        // chara-wide consolidated outcome that downstream code (camera
        // shake, damage application, animation pick) reads.
        //
        // NOTE: this field is currently captured-but-unrendered — no code
        // in dllmain.cpp surfaces it.  Kept for the planned hit-by tracker.
        int32_t     final_hit_result_code      = 0;

        // ==== Lane 2 alt-classify (counter-hit followup channel) ====
        // ResolveAttackVsHurtboxMask22 runs a SECOND classifier pass
        // against the chara's Lane 2 cell mask when the four alt-gate
        // bytes (chara+0x1725..+0x1728) signal "open".  This is used
        // during Soul Charge alt-attacks, Guard Impact counters, parry
        // counters, and stance-tech moves.
        //
        // alt_classify_open: true iff
        //   chara+0x1725 != 0   AND
        //   chara+0x1726 != 0   AND
        //   chara+0x1727 == 0   AND
        //   chara+0x1728 == 0
        // Stamped on every KHitDraw produced for this chara on this
        // tick — identical for every node on the same chara.
        //
        // alt_attack_mask: the Lane 2 cell's u64 SlotMask, valid only
        // when alt_classify_open AND Lane 2 has a non-null PackedMoveAddr
        // AND the cell pointer resolves successfully.  Zero otherwise.
        //
        // The renderer uses this to extend is_per_frame_active: an
        // attack node is per-frame-active when its category bit lives
        // in (primary mask) OR (alt_classify_open AND alt_attack_mask).
        //
        // Always false / zero for hurtbox/body entries — alt-classify
        // is an attacker-side gate only.
        bool        alt_classify_open       = false;
        uint64_t    alt_attack_mask         = 0;
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

        // Read this chara's 64-bit "own active attack cell" mask — the
        // per-frame damage gate written by the MoveVM.  Layout:
        //
        //   cell_ptr       = *(void**)(chara + 0x44058);   // may be null
        //   attackerMask   = *(u64*)cell_ptr;              // 0 if ptr null
        //
        // This is the mask that `LuxBattle_ResolveAttackVsHurtboxMask22`
        // ANDs against defender PerHurtboxBitmask entries to decide
        // reactions.  Returns 0 if either dereference faults or the cell
        // pointer is null — 0 means "no attack currently live for
        // damage" and callers will then fail the per-bit test for any
        // slot, which is the correct behaviour.
        static uint64_t readOwnAttackMask(void* chara) noexcept
        {
            if (!chara) return 0;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);
            void* cell = nullptr;
            if (!SafeReadPtr(bytes + ChaOffsets::OwnActiveAttackCell, &cell))
                return 0;
            const auto c = reinterpret_cast<uintptr_t>(cell);
            if (c < 0x10000ULL || c > 0x00007fffffffffffULL) return 0;
            uint64_t mask = 0;
            if (!SafeReadUInt64(cell, &mask)) return 0;
            return mask;
        }

        // Read the global "battle is running" gate (DAT_144846410).
        // True iff non-zero.  Returns true on read failure (open
        // policy — false-positive shows extra boxes, false-negative
        // hides legitimate ones; we prefer the former for debug
        // overlay UX).
        //
        // Used by readDefenderEngineActive below.  Memoised within
        // a single call to forEachKHit since both charas read the
        // same global.
        static bool readBattleRunningGate() noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return true;
            uint32_t value = 0;
            if (!SafeReadUInt32(reinterpret_cast<const void*>(
                    base + BattleGlobalRVAs::BattleRunningGate), &value))
                return true;
            return value != 0;
        }

        // Engine-truth predicate: "this chara's hurtbox list is
        // currently capable of producing a reaction if struck".
        // Composed from three early-return gates inside
        // LuxBattle_ResolveAttackVsHurtboxMask22 (@ 0x14033C100):
        //
        //   1. Battle is running       (DAT_144846410 != 0)
        //   2. chara+0x20B8 == 0       (NOT incapacitated)
        //   3. chara+0x19B0 != 6       (NOT in no-react state)
        //
        // If ANY gate fails the resolver returns without classifying,
        // so EVERY hurtbox on this chara is inert this frame.  The
        // visualiser uses this as a per-chara "wholesale inert"
        // signal that's stamped onto every KHitDraw produced for
        // the chara.
        //
        // Open-policy on read failure: treats unreadable state as
        // active.  We'd rather show a few extra boxes than hide
        // legitimate ones during a transient bad read.
        static bool readDefenderEngineActive(void* chara) noexcept
        {
            if (!readBattleRunningGate()) return false;
            if (!chara) return true;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);
            uint16_t incap = 0;
            if (SafeReadUInt16(bytes + ChaOffsets::IncapacitatedFlag,
                               &incap) && incap != 0)
                return false;
            uint16_t noreact = 0;
            if (SafeReadUInt16(bytes + ChaOffsets::NoReactStateFlag,
                               &noreact) && noreact == 6)
                return false;
            return true;
        }

        // Snapshot of the currently-active move-lane state.  Reads the
        // three most-interesting floats + move id + terminal flag from
        // the LuxMoveLaneState block the MoveVM is currently driving
        // (via chara+0x44068 ActiveLaneStateCursorPtr).  All fields
        // default to 0 / sentinel if any chase fails.
        //
        // Usage: call once per chara per frame for HUD display.
        // `has_move` is the only thing you need to gate visibility on —
        // it's false when no move is active (idle stance), so don't
        // show "frame 0/0".
        struct LaneSnapshot
        {
            bool     has_move       = false;  // false iff no active move
            int16_t  packed_move    = -1;     // PackedMoveAddr at +0x02
            int16_t  lane_index     = -1;     // LaneIndex at +0x00
            float    current_frame  = 0.0f;   // +0x08 (live advancing counter)
            float    length_frames  = 0.0f;   // +0x10 (move total length)
            float    playback_speed = 1.0f;   // +0x30 (current)
            int32_t  tick_counter   = 0;      // +0x04 (ticks since move start)
            bool     at_end         = false;  // +0x1A
            bool     finished       = false;  // +0x24
            bool     in_transition  = false;  // +0x26

            // ---- Attack-phase / frame-window state (chara-side, not lane) ----
            // Sourced from chara+0x1980 (FrameWindowPhaseTag) + cell+0x36/+0x38
            // (MasterWindowStart/End).  Computed per chara per tick by
            // LuxMoveVM_ClassifyHitboxFrameState.  Snapshotted here so HUD code
            // doesn't have to re-resolve the cell pointer.

            // Engine-truth attack phase: what the engine itself thinks the
            // currently-active move is doing this frame.
            KHitAttackPhase phase = KHitAttackPhase::None;

            // Master hit-window in animation frames (signed because cells
            // can author windows starting before frame 0 for chained moves
            // that share a parent timeline).  Both fields = 0 when phase
            // is None (no active cell to read from).
            int16_t  master_window_start = 0;  // cell+0x36
            int16_t  master_window_end   = 0;  // cell+0x38

            // True iff the current_frame integer is in
            // [master_window_start, master_window_end] AND the chara's
            // sub-window inhibitors are quiet (chara+0x16EB == 0 &&
            // chara+0x16FE == 0).  Strict mirror of the byte at
            // chara+0x16EA (bInMasterWindowFlag).  This is the boolean
            // form of "phase == Active" with the engine's exact gate
            // sequence applied.  Useful for the HUD's "ACTIVE" indicator.
            bool     in_master_window    = false;

            // Per-bank "current frame is in this bank's authored sub-window
            // right now" output (4 banks × 16 sub-windows each, indexed
            // by HitboxGroupBitfield bits 0..10).  -1 means the bank is
            // not currently active; otherwise the value is the GroupId
            // (0..15 within the bank).
            //   bank0_active_group = chara+0x20CC  (bank 0: GroupId  0..15)
            //   bank1_active_group = chara+0x20CE  (bank 1: GroupId 16..31)
            //   bank2_active_group = chara+0x20D0  (bank 2: GroupId 32..47)
            //   bank3_active_group = chara+0x20D2  (bank 3: GroupId 48..63)
            // Most moves only use bank 0; HUDs can ignore the rest.
            int16_t  bank0_active_group  = -1;
            int16_t  bank1_active_group  = -1;
            int16_t  bank2_active_group  = -1;
            int16_t  bank3_active_group  = -1;
        };

        static LaneSnapshot readLaneSnapshot(void* chara) noexcept
        {
            LaneSnapshot s{};
            if (!chara) return s;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);

            void* cursor = nullptr;
            if (!SafeReadPtr(bytes + ChaOffsets::ActiveLaneStateCursorPtr,
                             &cursor)) return s;
            const auto c = reinterpret_cast<uintptr_t>(cursor);
            if (c < 0x10000ULL || c > 0x00007fffffffffffULL) return s;
            auto* L = reinterpret_cast<uint8_t*>(cursor);

            uint16_t packedU = 0;
            if (!SafeReadUInt16(L + LuxMoveLaneOffsets::PackedMoveAddr,
                                &packedU)) return s;
            const int16_t packed = static_cast<int16_t>(packedU);
            if (packed == -1) return s;   // lane is idle — no active move

            s.has_move     = true;
            s.packed_move  = packed;
            uint16_t laneU = 0;
            SafeReadUInt16(L + LuxMoveLaneOffsets::LaneIndex, &laneU);
            s.lane_index = static_cast<int16_t>(laneU);
            SafeReadFloat(L + LuxMoveLaneOffsets::CurrentAnimFrame,
                          &s.current_frame);
            SafeReadFloat(L + LuxMoveLaneOffsets::AnimLengthFrames,
                          &s.length_frames);
            SafeReadFloat(L + LuxMoveLaneOffsets::PlaybackSpeedCurrent,
                          &s.playback_speed);
            SafeReadInt32(L + LuxMoveLaneOffsets::TickCounter,
                          &s.tick_counter);
            uint16_t atEnd = 0, fin = 0, inTr = 0;
            SafeReadUInt16(L + LuxMoveLaneOffsets::AtEndFlag,         &atEnd);
            SafeReadUInt16(L + LuxMoveLaneOffsets::FrameStepFinished, &fin);
            SafeReadUInt16(L + LuxMoveLaneOffsets::InTransitionFlag,  &inTr);
            s.at_end        = (atEnd != 0);
            s.finished      = (fin   != 0);
            s.in_transition = (inTr  != 0);

            // --- Attack-phase / frame-window snapshot ----------------
            // Read the engine's per-tick phase classifier output at
            // chara+0x1980 (i16, written by LuxMoveVM_Classify
            // HitboxFrameState).  Possible values are 0/1/2/3 mapping
            // to KHitAttackPhase {None, Startup, Active, Recovery}.
            // Anything outside that range falls through to None — the
            // engine never writes other values, but defensive coding
            // preserves overlay sanity if a torn read returns garbage.
            int16_t phaseRaw = 0;
            if (SafeReadInt16(bytes + ChaOffsets::FrameWindowPhaseTag,
                              &phaseRaw))
            {
                switch (phaseRaw)
                {
                    case 1:  s.phase = KHitAttackPhase::Startup;  break;
                    case 2:  s.phase = KHitAttackPhase::Active;   break;
                    case 3:  s.phase = KHitAttackPhase::Recovery; break;
                    default: s.phase = KHitAttackPhase::None;     break;
                }
            }

            // Read the boolean InMasterWindowFlag at chara+0x16EA.
            // Strict mirror of (phase == Active) with sub-window
            // inhibitors AND'd in — so this is the strictly-tighter
            // "engine considers attack live AND the inhibitors aren't
            // suppressing it" gate.  HUD active indicators should
            // prefer this over phase==Active for fidelity.
            uint8_t inWin = 0;
            if (SafeReadUInt8(bytes + ChaOffsets::InMasterWindowFlag,
                              &inWin))
                s.in_master_window = (inWin != 0);

            // Read MasterWindow start/end from the currently-active
            // attack cell (chara+0x44058 → cell).  When no cell is
            // active (idle / non-attacking move), both stay 0.
            void* cell = nullptr;
            if (SafeReadPtr(bytes + ChaOffsets::OwnActiveAttackCell,
                            &cell))
            {
                const auto cAddr = reinterpret_cast<uintptr_t>(cell);
                if (cAddr >= 0x10000ULL && cAddr <= 0x00007fffffffffffULL)
                {
                    auto* pCell = reinterpret_cast<uint8_t*>(cell);
                    int16_t winS = 0, winE = 0;
                    SafeReadInt16(pCell + LuxAttackCellOffsets::MasterWindowStart,
                                  &winS);
                    SafeReadInt16(pCell + LuxAttackCellOffsets::MasterWindowEnd,
                                  &winE);
                    s.master_window_start = winS;
                    s.master_window_end   = winE;
                }
            }

            // Per-bank sub-window outputs.  Each is u16 with sentinel
            // 0xFFFF meaning "this bank's sub-window is NOT live this
            // frame".  Otherwise the value is the GroupId within the
            // bank (0..15).  -1 in the snapshot mirrors the sentinel.
            auto readBank = [&](uintptr_t off, int16_t& out) {
                uint16_t raw = 0xFFFFu;
                if (!SafeReadUInt16(bytes + off, &raw))
                    raw = 0xFFFFu;
                out = (raw == 0xFFFFu) ? int16_t(-1)
                                       : static_cast<int16_t>(raw);
            };
            readBank(ChaOffsets::SubWinActiveBank0, s.bank0_active_group);
            readBank(ChaOffsets::SubWinActiveBank1, s.bank1_active_group);
            readBank(ChaOffsets::SubWinActiveBank2, s.bank2_active_group);
            readBank(ChaOffsets::SubWinActiveBank3, s.bank3_active_group);

            return s;
        }

        // ----------------------------------------------------------------
        // Standalone phase reader — when the caller already has a chara
        // pointer and just wants the engine's phase tag without paying
        // for a full LaneSnapshot.  Returns KHitAttackPhase::None on any
        // read failure (open policy: "treat as not in active frames").
        //
        // This is a 1-pointer-arithmetic + 1-SafeRead path — cheap enough
        // to call inside hot loops.  Used by the per-node attack-active
        // gate inside walkList().
        // ----------------------------------------------------------------
        static KHitAttackPhase readAttackPhase(void* chara) noexcept
        {
            if (!chara) return KHitAttackPhase::None;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);
            int16_t raw = 0;
            if (!SafeReadInt16(bytes + ChaOffsets::FrameWindowPhaseTag,
                               &raw))
                return KHitAttackPhase::None;
            switch (raw)
            {
                case 1: return KHitAttackPhase::Startup;
                case 2: return KHitAttackPhase::Active;
                case 3: return KHitAttackPhase::Recovery;
                default: return KHitAttackPhase::None;
            }
        }

        // ----------------------------------------------------------------
        // Read MasterWindowStart / MasterWindowEnd from the currently-
        // active attack cell.  Returns false if no cell is active or any
        // read fails; out parameters are left untouched in that case.
        // Both values are i16 in animation-frame units (60 Hz).
        // ----------------------------------------------------------------
        static bool readActiveCellMasterWindow(void* chara,
                                               int16_t* outStart,
                                               int16_t* outEnd) noexcept
        {
            if (!chara || !outStart || !outEnd) return false;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);
            void* cell = nullptr;
            if (!SafeReadPtr(bytes + ChaOffsets::OwnActiveAttackCell,
                             &cell))
                return false;
            const auto cAddr = reinterpret_cast<uintptr_t>(cell);
            if (cAddr < 0x10000ULL || cAddr > 0x00007fffffffffffULL)
                return false;
            auto* pCell = reinterpret_cast<uint8_t*>(cell);
            int16_t winS = 0, winE = 0;
            const bool a = SafeReadInt16(
                pCell + LuxAttackCellOffsets::MasterWindowStart, &winS);
            const bool b = SafeReadInt16(
                pCell + LuxAttackCellOffsets::MasterWindowEnd,   &winE);
            if (!a || !b) return false;
            *outStart = winS;
            *outEnd   = winE;
            return true;
        }

        // ----------------------------------------------------------------
        // Lane 2 alt-classify snapshot
        // ----------------------------------------------------------------
        // Returns the chara's Lane 2 ALT attacker mask AND whether the
        // four alt-classify gate bytes (chara+0x1725..+0x1728) are open.
        //
        // When `is_open` is true, the engine's ResolveAttackVsHurtboxMask22
        // will run a SECOND classifier pass against this chara's Lane 2
        // cell SlotMask in addition to the primary cell.  Slots whose
        // category bit lives in `lane2_mask` will fire reactions even
        // if the primary `OwnActiveAttackCell` doesn't have the bit.
        //
        // When `is_open` is false: `lane2_mask` returns 0 — the engine
        // doesn't read Lane 2 cells, so the alt path can't add active
        // bits.
        //
        // Used to extend is_per_frame_active for ATTACK nodes during
        // Soul Charge / GI / parry / stance-tech states.  See
        // ChaOffsets::AltClassifyEnableGate block above for the engine
        // path.
        struct Lane2AltClassifySnapshot
        {
            uint64_t lane2_mask = 0;
            bool     is_open    = false;
        };

        // Walk the four alt-classify gate bytes + (if open) resolve the
        // Lane 2 cell pointer and read its u64SlotMask.  Mirrors the
        // engine path in ResolveAttackVsHurtboxMask22 @ 0x14033C100.
        // ~5 SafeReads when gated open; 4 when not.  Cheap enough to
        // call once per chara per tick.
        static Lane2AltClassifySnapshot readLane2AltClassifySnapshot(
            void* chara) noexcept
        {
            Lane2AltClassifySnapshot s{};
            if (!chara) return s;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);

            // Gates first — bail early if not open.
            uint8_t gateEnable = 0, gateMW = 0, inhibA = 0, inhibB = 0;
            if (!SafeReadUInt8(bytes + ChaOffsets::AltClassifyEnableGate,
                               &gateEnable)) return s;
            if (gateEnable == 0) return s;
            if (!SafeReadUInt8(bytes + ChaOffsets::AltInMasterWindow,
                               &gateMW)) return s;
            if (gateMW == 0) return s;
            if (!SafeReadUInt8(bytes + ChaOffsets::AltClassifyInhibitorA,
                               &inhibA)) return s;
            if (inhibA != 0) return s;
            if (!SafeReadUInt8(bytes + ChaOffsets::AltClassifyInhibitorB,
                               &inhibB)) return s;
            if (inhibB != 0) return s;

            // Gates open — resolve Lane 2 cell.
            int16_t packedMoveRaw = 0;
            if (!SafeReadInt16(bytes + ChaOffsets::Lane2_PackedMoveAddr,
                               &packedMoveRaw)) return s;
            if (packedMoveRaw < 0)
            {
                s.is_open = true;     // alt gates are open but Lane 2 is idle
                return s;
            }
            const uint32_t packedMove = static_cast<uint32_t>(
                static_cast<uint16_t>(packedMoveRaw));

            void* bankBase = nullptr;
            if (!SafeReadPtr(bytes + ChaOffsets::MoveBankBasePtr, &bankBase))
                return s;
            const auto bbAddr = reinterpret_cast<uintptr_t>(bankBase);
            if (bbAddr < 0x10000ULL || bbAddr > 0x00007fffffffffffULL)
            {
                s.is_open = true;
                return s;
            }
            auto* bb = reinterpret_cast<uint8_t*>(bankBase);

            // ResolveBankSlot: slot table starts at bank+0x30, stride 0x48.
            //   bankIdx     = (packedMove >> 12) & 0xF      (0..3 valid)
            //   slotInBank  = packedMove & 0x7FF             (11-bit)
            //   startIdx    = bank.Bucket[bankIdx].StartIdx (u16 at
            //                 bank+0x1C + bankIdx*4)
            //   slotPtr     = bank + 0x30 + (startIdx + slotInBank)*0x48
            // Engine rejects bankIdx >= 4 (LuxMoveVM_ResolveBankSlot
            // early-out: `if (dwBankIdx < 4)`).
            const uint32_t bankIdx    = (packedMove >> 12) & 0xFu;
            const uint32_t slotInBank = packedMove & 0x7FFu;
            if (bankIdx >= 4)
            {
                s.is_open = true;
                return s;
            }
            uint16_t startIdx = 0;
            if (!SafeReadUInt16(bb + 0x1C + bankIdx * 4, &startIdx))
            {
                s.is_open = true;
                return s;
            }
            auto* slotPtr = bb + 0x30
                          + (static_cast<size_t>(startIdx) + slotInBank)
                          * 0x48;

            // Pick the variant index — if < 6, use slotPtr+0x3C+variant*2,
            // else fall back to slotPtr+0x3C.
            uint32_t variantIdx = 0;
            SafeReadUInt32(bytes + ChaOffsets::Lane2_AnimVariantIndex,
                           &variantIdx);
            int16_t cellBoneRaw = 0;
            const uintptr_t cellBoneAddr =
                reinterpret_cast<uintptr_t>(slotPtr) + 0x3C
                + (variantIdx < 6 ? variantIdx * 2 : 0);
            if (!SafeReadInt16(reinterpret_cast<const void*>(cellBoneAddr),
                               &cellBoneRaw))
            {
                s.is_open = true;
                return s;
            }
            if (cellBoneRaw < 0)
            {
                s.is_open = true;     // gates open but Lane 2 cell is null
                return s;
            }

            // Resolve cell address: bank + bank[+0x10] + cellBone*0x70.
            uint32_t cellTableOff = 0;
            if (!SafeReadUInt32(bb + 0x10, &cellTableOff))
            {
                s.is_open = true;
                return s;
            }
            auto* cellPtr = bb + cellTableOff
                          + static_cast<size_t>(cellBoneRaw) * 0x70;
            uint64_t mask = 0;
            if (!SafeReadUInt64(cellPtr, &mask))
            {
                s.is_open = true;
                return s;
            }
            s.is_open    = true;
            s.lane2_mask = mask;
            return s;
        }

        // ----------------------------------------------------------------
        // Read AttackFlags (+0x32) and InputCond (+0x34) from the currently-
        // active attack cell (chara+0x44058).  Both values default to 0
        // when no cell is active or reads fail.
        //
        // AttackFlags is the engine's per-cell attack-attribute bitmask —
        // see KHitAttackFlagBits.  It's the canonical "is this a high /
        // mid / low / throw / unblockable attack" classifier, consumed
        // by EvaluateMoveTransition @ 0x14033E140.  HorseMod stamps it
        // onto every attack KHitDraw so callers can tint or label
        // hitboxes by tier.
        // ----------------------------------------------------------------
        static void readActiveCellAttackFlags(void* chara,
                                              uint16_t* outFlags,
                                              uint16_t* outInputCond) noexcept
        {
            if (outFlags)     *outFlags     = 0;
            if (outInputCond) *outInputCond = 0;
            if (!chara) return;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);
            void* cell = nullptr;
            if (!SafeReadPtr(bytes + ChaOffsets::OwnActiveAttackCell,
                             &cell))
                return;
            const auto cAddr = reinterpret_cast<uintptr_t>(cell);
            if (cAddr < 0x10000ULL || cAddr > 0x00007fffffffffffULL)
                return;
            auto* pCell = reinterpret_cast<uint8_t*>(cell);
            if (outFlags)
                SafeReadUInt16(pCell + LuxAttackCellOffsets::AttackFlags,
                               outFlags);
            if (outInputCond)
                SafeReadUInt16(pCell + LuxAttackCellOffsets::InputCond,
                               outInputCond);
        }

        // ----------------------------------------------------------------
        // Snapshot of defender-side stance/state bytes consumed by
        // LuxMoveVM_EvaluateMoveTransition @ 0x14033E140 when classifying
        // an incoming hit.  Read once per chara per tick; same value
        // stamped onto every KHitDraw produced for the chara.
        //
        // EvaluateMoveTransition's stance selection rule (verified):
        //   if (chara+0x3494 & 0x08 == 0) {           // guard not disabled
        //     if (chara+0x16DC && (cell.AttackFlags & 0x03)) base   // blockstun + high/low attack
        //     else if (chara+0x16DB && (cell.AttackFlags & 0x80)) base // hitstun + high attack
        //     else use alt path below
        //   }
        //   if (chara+0x1701) use chara+0x16FC alt
        //   else              use chara+0x16D2 base
        //
        // HorseMod surfaces BOTH base and alt separately so consumers can
        // either render the "engine-committed" stance (base during stun)
        // or the "live input-derived" stance (alt during free play).
        // The convenience `crouching` field follows the engine's rule:
        // it picks alt iff `alt_lock_gate` is set, else base.
        // ----------------------------------------------------------------
        struct DefenderStanceSnapshot
        {
            // === Stance selection ===
            // Engine-resolved "is this chara crouching for the next incoming
            // hit" — uses base/alt per the EvaluateMoveTransition rule.
            // This is the boolean throws/highs/lows actually test against.
            bool crouching                = false;
            // Raw component bytes (for renderers that want both):
            bool crouching_base           = false;  // chara+0x16D2
            bool crouching_alt            = false;  // chara+0x16FC (live-input)
            bool alt_lock_gate            = false;  // chara+0x1701 (use alt iff set)

            // === Hit-react / block-state gates ===
            bool actively_blocking        = false;  // chara+0x16FD
            bool blockable_this_frame     = false;  // chara+0x16F2 (re-derived
                                                    //  at top of resolver from
                                                    //  +0x16D1, +0x16FD,
                                                    //  +0x15AC, opp+0x2110)
            bool in_blockstun             = false;  // chara+0x16DC
            bool in_hitstun               = false;  // chara+0x16DB

            // === Invul / no-react flags (EvaluateMoveTransition routes
            // through return-code 6 = whiff when either is set) ===
            bool invul_a                  = false;  // chara+0x16E9
            bool invul_b                  = false;  // chara+0x16D4

            // === Guard-broken state (routes return-code 7 / 0xB only) ===
            bool guard_broken             = false;  // chara+0x16D3

            // === Guard-disabled gate (chara+0x3494 bit 3) ===
            // Forces the alt-path stance selection unconditionally.  Set
            // by stance-locking moves (super-flash, certain transitions).
            bool guard_disabled           = false;  // chara+0x3494 & 0x08

            // === Force-allow flag — overrides AttackFlags level mismatch ===
            // When non-zero, EvaluateMoveTransition's "stance/level match"
            // check is bypassed and the attack always lands.  Used by
            // invincibility-cancel paths and certain super-armor scripts.
            bool force_allow              = false;  // chara+0x1724
        };

        static DefenderStanceSnapshot readDefenderStance(void* chara) noexcept
        {
            DefenderStanceSnapshot s{};
            if (!chara) return s;
            auto* b = reinterpret_cast<uint8_t*>(chara);
            uint8_t v = 0;
            uint16_t w = 0;

            // Stance component reads.
            if (SafeReadUInt8(b + 0x16D2, &v)) s.crouching_base    = (v != 0);
            if (SafeReadUInt8(b + 0x16FC, &v)) s.crouching_alt     = (v != 0);
            if (SafeReadUInt8(b + 0x1701, &v)) s.alt_lock_gate     = (v != 0);

            // Guard-disabled gate (single bit out of a u16).
            if (SafeReadUInt16(b + 0x3494, &w))
                s.guard_disabled = ((w & 0x08) != 0);

            // Engine's stance resolution: when alt_lock_gate is set AND the
            // chara is NOT locked into base by stun (caller-side cell-flag
            // checks decide the stun-lock; we mirror only the alt-vs-base
            // selection here — the stun-lock arm is captured by the
            // in_blockstun / in_hitstun fields below for the consumer's
            // own classification).  The simple selection that matches
            // EvaluateMoveTransition's terminal branch:
            //   crouching = alt_lock_gate ? crouching_alt : crouching_base
            // is adequate for "what's the chara's current commit stance"
            // because guard_disabled forces the alt path anyway.
            s.crouching = (s.alt_lock_gate || s.guard_disabled)
                            ? s.crouching_alt
                            : s.crouching_base;

            // Block / stun gates.
            if (SafeReadUInt8(b + 0x16FD, &v)) s.actively_blocking    = (v != 0);
            if (SafeReadUInt8(b + 0x16F2, &v)) s.blockable_this_frame = (v != 0);
            if (SafeReadUInt8(b + 0x16DC, &v)) s.in_blockstun         = (v != 0);
            if (SafeReadUInt8(b + 0x16DB, &v)) s.in_hitstun           = (v != 0);

            // Invul / guard-broken / force-allow.
            if (SafeReadUInt8(b + 0x16E9, &v)) s.invul_a              = (v != 0);
            if (SafeReadUInt8(b + 0x16D4, &v)) s.invul_b              = (v != 0);
            if (SafeReadUInt8(b + 0x16D3, &v)) s.guard_broken         = (v != 0);
            if (SafeReadUInt8(b + 0x1724, &v)) s.force_allow          = (v != 0);

            return s;
        }

        // ----------------------------------------------------------------
        // Throw-dispatch height gate snapshot.
        //
        // Calls LuxMoveVM_GetCharaEffectiveHeight on this chara (defender
        // perspective) AND on its opponent (attacker perspective).  The
        // opponent is reached via the cross-chara pointer at chara+0x973E8
        // — same field every engine site uses when looking up the
        // attacker from a defender VM context.
        //
        // `throw_height_gate_ok` is the simple "would a non-allow-listed
        // yarareId dispatch right now" predicate: defender_height < 5.
        // For throws specifically, the relevant yarareId is whatever the
        // throw cell stamped via ResolveAttackVsHurtboxMask22's pre-scan
        // (defender+0x212E) — most throw yarareIds are NOT in the
        // unconditional allow-set, so they only dispatch when this is true.
        //
        // Returns all-zeros / true when the native helper isn't resolved
        // (open-policy "engine permits it" default).
        // ----------------------------------------------------------------
        struct ThrowHeightSnapshot
        {
            int32_t defender_height  = 0;
            int32_t attacker_height  = 0;
            bool    gate_ok          = true;  // true iff defender_height < 5
        };

        static ThrowHeightSnapshot readThrowHeightSnapshot(void* chara) noexcept
        {
            ThrowHeightSnapshot s{};
            if (!chara) return s;
            if (!NativeBinding::hasGetCharaEffectiveHeight()) return s;
            s.defender_height = NativeBinding::getCharaEffectiveHeight(chara);

            // Resolve opponent via chara+0x973E8 (pointer-to-opp).  Use
            // SafeReadPtr because cross-chara reads can race during
            // initial chara-slot construction.
            auto* b = reinterpret_cast<uint8_t*>(chara);
            void* opp = nullptr;
            if (SafeReadPtr(b + 0x973E8, &opp))
            {
                const auto oAddr = reinterpret_cast<uintptr_t>(opp);
                if (oAddr >= 0x10000ULL && oAddr <= 0x00007fffffffffffULL)
                    s.attacker_height =
                        NativeBinding::getCharaEffectiveHeight(opp);
            }

            s.gate_ok = (s.defender_height < 5);
            return s;
        }

        // ----------------------------------------------------------------
        // Read chara+0x43DA0 — the chara-wide consolidated hit-reaction
        // result code.  See KHitDraw::final_hit_result_code for the value
        // table.  Returns 0 on read failure (correct default for "no
        // reaction this tick").
        // ----------------------------------------------------------------
        static int32_t readFinalHitResultCode(void* chara) noexcept
        {
            if (!chara) return 0;
            auto* b = reinterpret_cast<uint8_t*>(chara);
            int32_t v = 0;
            SafeReadInt32(b + 0x43DA0, &v);
            return v;
        }

        // ----------------------------------------------------------------
        // Hurtbox-slot invulnerability state.
        //
        // SC6's i-frames / armor / parry windows are implemented through
        // VM opcode 0x13AC (LuxMoveVM_DispatchEffectOp @ 0x140376B20)
        // which calls LuxMoveVM_SetHurtboxSlotsActiveMask @ 0x140308D70
        // with a 23-bit DISABLE mask.  POLARITY IS INVERTED:
        //   mask bit S = 1  ⇒  DISABLE every hurtbox node whose +0x17 == S
        //                      (clear node+0x14)
        //   mask bit S = 0  ⇒  ENABLE  every hurtbox node whose +0x17 == S
        //
        // The deserialiser sets node+0x14 = 1 by default, so opcode 0x13AC
        // is the mechanism that creates i-frames (full invul: mask =
        // 0x7FFFFF) or limb-armor windows (mask = single slot bit).
        //
        // The result of every 0x13AC firing in the move so far is
        // OBSERVABLE DIRECTLY from each hurtbox node's +0x14 field —
        // the engine has already applied the masks.  So invul detection
        // is a one-pass walk over the hurtbox list checking which slots
        // are currently disabled.
        //
        // We distinguish two structurally-different reasons a hurtbox
        // can have +0x14 == 0:
        //   (a) The hurtbox was authored as default-OFF (slot >=
        //       AttackMaxSlot bound).  These are character-specific
        //       "extended-reach" volumes that only turn ON during
        //       specific moves — the cyan rendering case in HorseMod.
        //       NOT i-frames.
        //   (b) The hurtbox is in classifier-addressable range AND
        //       has been disabled by VM opcode 0x13AC.  These ARE
        //       i-frame / armor windows.
        //
        // The HurtboxInvulState struct returns the (b) interpretation:
        // a per-slot bitmask of which classifier-addressable slots are
        // currently disabled, plus convenience predicates.
        // ----------------------------------------------------------------
        struct HurtboxInvulState
        {
            // Bitmask of slots in [0, classifier_bound) that are
            // currently disabled (node+0x14 == 0).  Each slot index
            // S corresponds to bit S in this mask.  Up to 23 bits.
            uint32_t disabled_slot_mask = 0;

            // Bitmask of slots that the chara has hurtbox nodes for,
            // regardless of whether they're enabled.  Lets the caller
            // distinguish "slot S is disabled because the move masked
            // it" from "no node exists at slot S in the first place".
            uint32_t present_slot_mask  = 0;

            // The classifier bound at chara+0x44494 (= AttackMaxSlot,
            // reused as the hurtbox iteration ceiling).  Slots >= this
            // are NOT considered for invul classification because the
            // engine itself never reads them in the resolver loop.
            int32_t  classifier_bound   = 0;

            // True iff at least one classifier-addressable hurtbox slot
            // is currently disabled.  This is the "the chara has SOME
            // armor / partial invul this frame" predicate.
            bool     any_invul_slot     = false;

            // True iff EVERY classifier-addressable hurtbox slot that
            // has a node at all is currently disabled.  This is the
            // "full-body i-frames" predicate (e.g. wakeup invul,
            // post-hit invul, super-flash).
            //
            // NB: false when the chara has no hurtbox nodes at all
            // (no move active / corrupt state) — we don't infer
            // invul from absence of data.
            bool     full_body_invul    = false;
        };

        // Walk the chara's hurtbox list once and compute the invul state.
        // Cheap: ~30 dependent loads typical (one per node), all SafeRead-
        // wrapped.  Call once per chara per tick from the cockpit hook.
        //
        // Returns a default-zeroed state on null chara or read failure —
        // both `any_invul_slot` and `full_body_invul` are false in that
        // case, which is the open-policy "treat as not invul" default
        // (consistent with the rest of the readers in this class).
        static HurtboxInvulState readHurtboxInvulState(void* chara) noexcept
        {
            HurtboxInvulState s{};
            if (!chara) return s;
            auto* bytes = reinterpret_cast<uint8_t*>(chara);

            // Load classifier bound, clamped to [0, 23] to match the
            // engine's slot-range cap.
            int32_t bound = 0;
            SafeReadInt32(bytes + ChaOffsets::ClassifierHurtboxBound,
                          &bound);
            if (bound < 0)  bound = 0;
            if (bound > 23) bound = 23;
            s.classifier_bound = bound;

            // Walk the hurtbox list head.
            void* head = nullptr;
            if (!SafeReadPtr(bytes + ChaOffsets::HurtboxListHead, &head))
                return s;

            // Hard cap on iteration length (defensive — mirrors the
            // walkList cap below).  Real hurtbox lists are <30 nodes.
            constexpr int kHurtListCap = 256;
            void* node = head;
            for (int i = 0; node != nullptr && i < kHurtListCap; ++i)
            {
                const auto nAddr = reinterpret_cast<uintptr_t>(node);
                if (nAddr < 0x10000ULL || nAddr > 0x00007fffffffffffULL)
                    break;
                auto* nbytes = reinterpret_cast<uint8_t*>(node);

                uint8_t  slot  = 0;
                uint16_t gate  = 0;
                if (!SafeReadUInt8 (nbytes + KHitOffsets::SubIdOrBoneId,
                                    &slot)) break;
                if (!SafeReadUInt16(nbytes + KHitOffsets::IsActiveThisFrame,
                                    &gate)) break;

                // Only count slots inside the classifier's iteration
                // range — slots >= bound are "engine-invisible" and
                // can't fire reactions whether they're enabled or not.
                if (slot < bound)
                {
                    const uint32_t bit = uint32_t(1) << (slot & 0x1F);
                    s.present_slot_mask |= bit;
                    if (gate == 0)
                        s.disabled_slot_mask |= bit;
                }

                void* next = nullptr;
                if (!SafeReadPtr(nbytes + KHitOffsets::Next, &next))
                    break;
                node = next;
            }

            s.any_invul_slot  = (s.disabled_slot_mask != 0);
            // full_body_invul: every present slot is disabled, AND
            // we have at least one present slot (avoid declaring
            // invul on an empty list).
            s.full_body_invul = (s.present_slot_mask != 0)
                             && (s.disabled_slot_mask == s.present_slot_mask);
            return s;
        }

        // Per-node damage predicate.  Returns true iff the bit at the
        // node's slot index (+0x17 & 0x3F) is set in the supplied own-
        // attack mask.  Defined on every KHit node; only meaningful
        // for attack-list entries.  Cheap enough to call for every
        // node on every frame.
        static bool slotBitInMask(uint8_t slotByte, uint64_t mask) noexcept
        {
            return (mask >> (slotByte & 0x3Fu)) & 1ull;
        }

        // Read the chara's PER-FRAME damage mask — strictly tighter than
        // readOwnAttackMask.  Mirrors the pMoveVMCell lookup that
        // LuxBattle_TickHitResolutionAndBodyCollision (0x14033cca0)
        // performs each frame to decide which slots are damage-active
        // for the current sub-frame of the active move.  Returns 0 if:
        //   - chara is null
        //   - the chara isn't currently in a move (bank pointer null)
        //   - any pointer-chase or bounds check fails (graceful — caller
        //     just sees "no slot is active", which renders nothing).
        //
        // All loads are SafeRead-wrapped because we're chasing into game
        // memory we don't own — the bank pointer, sub-frame record, and
        // cell record are all owned by the MoveVM and could be torn down
        // between our read and the engine's overwrite (e.g. mid-frame
        // move transition).  Reading torn data via SafeRead returns 0
        // for the offending qword; the worst-case observable effect is
        // a 1-frame "all hidden" flash on a move transition.
        //
        // Cost: ~6 dependent loads per chara per frame.  Negligible vs.
        // the per-node bone-transform call we already make per draw.
        //
        // CONSTANT REFERENCES (offsets confirmed against the current
        // build by Ghidra at 0x140300c70 plate; see ChaOffsets above):
        //   0x44dc2  uint16  current move sub-frame id
        //   0x455c0  void*   MoveVM bank-base pointer
        //   bank+0x10  uint32  cell-table byte offset (added to bank base)
        //   bank+(0x1c+sb*4)  uint16  sub-frame count for sub-bank `sb`
        //   bank+(0x1c+sb*4+4) uint16  sub-frame START offset (NB: original
        //                              expression `(sb+7)*4` decodes to
        //                              `0x1c + sb*4 + 4` = the entry
        //                              immediately AFTER the count)
        //   per sub-frame record (0x48 bytes, base = bank+0x30+(start+i)*0x48):
        //     +0x3c  int16  cell bone index (selects which 0x70-byte cell)
        //   per cell (0x70 bytes, base = bank + bank+0x10 + cellBone*0x70):
        //     +0x00  uint64 active-slot bitmask  <-- THE per-frame damage gate
        static uint64_t readPerFrameDamageMask(void* chara) noexcept
        {
            if (!chara) return 0;
            auto* b = reinterpret_cast<uint8_t*>(chara);

            // Step 1: chara state — current sub-frame id + bank base.
            uint16_t moveSubId = 0;
            if (!SafeReadUInt16(b + ChaOffsets::MoveCurrentSubFrameId,
                                &moveSubId)) return 0;
            void* bankBase = nullptr;
            if (!SafeReadPtr(b + ChaOffsets::MoveBankBasePtr, &bankBase))
                return 0;
            const auto bbAddr = reinterpret_cast<uintptr_t>(bankBase);
            if (bbAddr < 0x10000ULL || bbAddr > 0x00007fffffffffffULL)
                return 0;
            auto* bb = reinterpret_cast<uint8_t*>(bankBase);

            // Step 2: pick sub-bank from the upper nibble; read its
            // start-offset and count from the bank header.
            const uint16_t subBank  = (moveSubId >> 12) & 0xF;
            uint16_t subOff = 0;
            uint16_t subCnt = 0;
            if (!SafeReadUInt16(bb + (subBank + 7) * 4, &subOff)) return 0;
            if (!SafeReadUInt16(bb + 0x1e + subBank * 4, &subCnt)) return 0;

            // Step 3: bound-check the sub-frame index.  Out-of-range
            // means we're outside the move's authored timeline — most
            // commonly at the very start (frame 0) or after move-end
            // before the engine clears state.
            const uint16_t frameIdx = moveSubId & 0x7FF;
            if (frameIdx >= subCnt) return 0;

            // Step 4: locate the sub-frame record (0x48 bytes each;
            // payload starts at +0x30 from bank base, so each record
            // index `i` lives at bank + 0x30 + (subOff + i) * 0x48).
            auto* sfRec = bb + (static_cast<size_t>(subOff) + frameIdx) * 0x48 + 0x30;
            uint16_t cellBoneRaw = 0;
            if (!SafeReadUInt16(sfRec + 0x3c, &cellBoneRaw)) return 0;
            // The field is signed in the engine; high bit set = "no
            // active cell this sub-frame" sentinel.  Treat as "no bits
            // set" rather than wrapping into garbage during the cell-
            // table address arithmetic below.
            const int16_t cellBone = static_cast<int16_t>(cellBoneRaw);
            if (cellBone < 0) return 0;

            // Step 5: cell-table base lives at *(uint32*)(bankBase+0x10),
            // ADDED to bankBase (not subtracted).  Each cell is 0x70 bytes;
            // its first u64 is the active-slot bitmask.
            uint32_t cellTableOff = 0;
            if (!SafeReadUInt32(bb + 0x10, &cellTableOff)) return 0;
            auto* cellPtr = bb + cellTableOff
                          + static_cast<size_t>(cellBone) * 0x70;
            uint64_t mask = 0;
            if (!SafeReadUInt64(cellPtr, &mask)) return 0;
            return mask;
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

            // Pre-read THIS chara's own attack-cell mask — the 64-bit
            // per-move "slots dealing damage this frame" bitmap from
            // *(u64*)(chara + 0x44058).  Used per-node to compute
            // is_damage_active below.  Zero is a valid value (neutral
            // frame, no attack authored as live); every slot bit test
            // will then return false, which is what we want.
            const uint64_t own_attack_mask = readOwnAttackMask(list_chara);

            // Pre-compute the chara-wide "engine can fire reactions
            // on this chara this frame" gate.  Composed from three
            // early-return sites in
            // LuxBattle_ResolveAttackVsHurtboxMask22 (@ 0x14033C100):
            //   * Battle running          (DAT_144846410 != 0)
            //   * Not incapacitated       (chara+0x20B8 == 0)
            //   * Not in no-react state   (chara+0x19B0 != 6)
            //
            // When any fails, the entire defender hurtbox list is
            // wholesale inert this frame regardless of geometry,
            // +0x14, or slot index.  See KHitDraw::defender_can_react_engine
            // for the rationale.
            //
            // Reading once here amortises the cost across every
            // KHitDraw produced for this chara — three SafeRead's
            // total instead of three per node.
            const bool defender_can_react =
                readDefenderEngineActive(list_chara);

            // Pre-read THIS chara's Lane 2 alt-classify snapshot once
            // per call.  When the alt-gate bytes (chara+0x1725..+0x1728)
            // are all in the "open" configuration, the engine's
            // ResolveAttackVsHurtboxMask22 will fire a SECOND classifier
            // pass against this chara's Lane 2 cell SlotMask in addition
            // to the primary cell.  Pre-reading here amortises the
            // bank-resolution chain across all attack nodes in the walk.
            //
            // is_per_frame_active for ATTACK nodes uses this to extend
            // the per-cell mask check: an attack is "engine will fire
            // damage" if its category bit lives in EITHER the primary
            // own_attack_mask OR (alt is open AND bit is in
            // alt_snap.lane2_mask).  See KHitDraw::alt_classify_open
            // / alt_attack_mask docs for engine background.
            const Lane2AltClassifySnapshot alt_snap =
                readLane2AltClassifySnapshot(list_chara);

            // (Was: const uint64_t per_frame_mask = readPerFrameDamage
            // Mask(list_chara). Removed because the engine's per-frame
            // sub-frame cell turns out to be empty for most SC6 moves —
            // the engine reads it OR'd with the per-move cell, and
            // simple moves set bits only on the per-move cell.  The
            // is_per_frame_active filter now uses +0x14 minus the
            // 0x3FFFD floor instead.  readPerFrameDamageMask() helper
            // is kept on the class for diagnostic value but no longer
            // called from the hot path.)

            // Pre-read the per-hurtbox reaction state table (22 × i32).
            //
            // IMPORTANT indexing convention: `reactions[N]` is the reaction
            // written by the classifier for "hurtbox slot N", and slot N
            // refers to the VALUE of the node's +0x17 (SubIdOrBoneId) — not
            // to its position in the linked list.  See
            // LuxBattle_ResolveAttackVsHurtboxMask22 (0x14033C100) and the
            // paired write in LuxBattleChara_UpdateAllKHitWorldCenters
            // (0x14030D6A0) which OR's bits into PerHurtboxBitmask[hurt->+0x17].
            // When consuming this table, look up reactions[node->+0x17], NOT
            // reactions[list_index] — the walker learned this the hard way.
            int32_t reactions[22] = {};
            for (int i = 0; i < 22; ++i)
            {
                SafeReadInt32(bytes + ChaOffsets::PerHurtboxReactionState
                              + i * sizeof(int32_t),
                              &reactions[i]);
            }

            // ------------------------------------------------------------
            // Reaction-state diagnostic + sticky flash
            // ------------------------------------------------------------
            // STICKY FLASH design: the raw reaction flag is a 1-frame pulse
            // (~16ms at 60fps, hard to see).  Hold the "hot" state for N
            // frames after raw goes non-zero so the colour change is
            // actually visible.  walkList() consumes sticky_reactions[] in
            // place of raw reactions[].
            //
            // Diagnostic edge-scan: previously we scanned the 512-byte
            // window 0x1C00..0x1E00 to validate the 0x1C74 offset and
            // surface alternative storage locations.  Cross-validation
            // against the SC6 binary (Ghidra: LuxBattle_ResolveAttackVs-
            // HurtboxMask22 @ 0x14033C100) confirms PerHurtboxReactionState
            // is exactly i32[22] @ chara+0x1C74 (88 bytes).  The wide scan
            // is now restricted to those 22 slots:
            //   - read cost drops 6×
            //   - false matches in surrounding fields (e.g. unrelated
            //     i32s in the 0x1C00..0x1E00 range that happen to flip
            //     between frames) no longer pollute the [HorseMod.React]
            //     log
            //
            // Per-player state is keyed by poseSelector (0 = P1, 1 = P2).
            const int pi_idx = (poseSelector < 2u)
                             ? static_cast<int>(poseSelector) : 0;

            // --- Edge-scan diagnostic over the actual reaction array -----
            // Engine bound is dynamic (chara+0x44494, clamped to [0,22]
            // by the classifier loop), so we walk up to the kReactSlotMax
            // ceiling and ignore the rest.
            constexpr uintptr_t kReactionScanBase = ChaOffsets::PerHurtboxReactionState;
            constexpr int       kReactSlotMax     = 22;  // engine clamp
            int32_t* scan_prev = s_react_scan_prev[pi_idx];

            for (int i = 0; i < kReactSlotMax; ++i)
            {
                int32_t v = 0;
                SafeReadInt32(bytes + kReactionScanBase + i * 4, &v);
                if (v != 0 && scan_prev[i] == 0)
                {
                    const uintptr_t off = kReactionScanBase + i * 4;
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[HorseMod.React] pi={} EDGE off=0x{:x} slot={} "
                            "val=0x{:08x}\n"),
                        pi_idx, off, i, static_cast<uint32_t>(v));
                }
                scan_prev[i] = v;
            }

            // --- (b) Sticky flash (used for rendering) -------------------
            // Hold "hot" for `s_sticky_frames` GAME FRAMES after raw goes
            // non-zero.  Driven by the dllmain UI slider; see
            // setStickyFrames().  0 frames = no sticky (raw 1-frame
            // pulse only).
            //
            // Time source: g_LuxBattle_FrameCounter @ imageBase+0x470D0C4
            // (absolute 0x14470D0C4).  Incremented at the very end of
            // LuxBattle_PerFrameTick, which is the same function
            // Horse::WorldTickGate gates at the entry — so freeze and
            // slow-mo, frame-step, and native play all share a single
            // truth: "did the world tick this cockpit frame?".  Robust
            // properties:
            //   - Freeze (gate policy=0): PerFrameTick bails before the
            //     increment → counter unchanged → delta=0 → flash held.
            //   - Step / slow-mo: counter advances exactly once per
            //     game frame the gate releases → delta=1 per game frame
            //     observed by cockpit.
            //   - Native play: counter advances at game-tick rate
            //     (60 Hz) regardless of render rate (60/120/144 Hz).
            //
            // History: a previous version drained on
            // LuxMoveLaneState::TotalTickCounter (chara+0x44068→+0x458).
            // That counter is bumped INSIDE the inner advance loop
            // (LuxMoveVM_AdvanceLaneFrameStep — decompile via Ghidra MCP),
            // and LuxMoveVM_ExecuteOpStream re-enters the step routine
            // every time a move-cell transition completes — so during
            // hit reactions (which transition through several cells in
            // one game frame) the lane counter could bump 5–8 per game
            // frame, draining a 15-tick sticky in 2 frame-step presses.
            // The global frame counter has no such loop and increments
            // EXACTLY ONCE per PerFrameTick.
            const int kStickyFrames =
                s_sticky_frames.load(std::memory_order_relaxed);

            // Read the global game-frame counter.  Falls back to
            // tick_delta=0 (no drain) if NativeBinding isn't resolved
            // or the read faults — keeping the flash visible is
            // strictly better than draining at the wrong rate.
            constexpr uintptr_t kFrameCounterRVA = 0x470D0C4;
            uint32_t cur_frame = 0;
            bool     have_frame = false;
            {
                const uintptr_t base = NativeBinding::imageBase();
                if (base)
                {
                    have_frame = SafeReadUInt32(
                        reinterpret_cast<const void*>(base + kFrameCounterRVA),
                        &cur_frame);
                }
            }

            // Compute how many game frames have elapsed since the
            // previous call.  Clamps: negative = counter reset (new
            // match / round restart) → 0; > 8 = treat as 1 to avoid
            // blowing past the sticky window on a single call.
            int tick_delta = 0;
            if (have_frame)
            {
                const int64_t prev = s_prev_frame_counter[pi_idx];
                if (prev >= 0)
                {
                    const int64_t raw =
                        static_cast<int64_t>(cur_frame) - prev;
                    tick_delta = (raw < 0)  ? 0
                               : (raw > 8)  ? 1
                               :              static_cast<int>(raw);
                }
                s_prev_frame_counter[pi_idx] =
                    static_cast<int64_t>(cur_frame);
            }

            int32_t* sticky = s_sticky_reactions[pi_idx];
            for (int i = 0; i < 22; ++i)
            {
                if (reactions[i] != 0)
                {
                    sticky[i] = kStickyFrames;
                }
                else if (sticky[i] > 0 && tick_delta > 0)
                {
                    sticky[i] = (sticky[i] > tick_delta)
                              ? sticky[i] - tick_delta
                              : 0;
                }
            }

            // walkList consumes sticky values so `reaction_hot` stays true
            // for the flash window, not just the 1-frame pulse.
            int32_t reactions_hot[22];
            for (int i = 0; i < 22; ++i) reactions_hot[i] = sticky[i];

            // Read list heads + counts up-front for diagnostics AND use.
            void* body_head = nullptr;
            void* atk_head  = nullptr;
            void* hurt_head = nullptr;
            int32_t body_count = 0;
            int32_t atk_count  = 0;
            int32_t hurt_count = 0;
            int32_t hurt_slot_count = 0;  // classifier bound @ +0x44494
            SafeReadPtr  (bytes + ChaOffsets::BodyListHead,       &body_head);
            SafeReadPtr  (bytes + ChaOffsets::AttackListHead,     &atk_head);
            SafeReadPtr  (bytes + ChaOffsets::HurtboxListHead,    &hurt_head);
            SafeReadInt32(bytes + ChaOffsets::BodyListCount,      &body_count);
            SafeReadInt32(bytes + ChaOffsets::AttackListCount,    &atk_count);
            SafeReadInt32(bytes + ChaOffsets::HurtboxListCount,   &hurt_count);
            SafeReadInt32(bytes + ChaOffsets::HurtboxSlotCount,   &hurt_slot_count);

            const bool verbose = shouldLog();
            // Per-chara header + hex dump — DISABLED (noisy).  Re-enable
            // by flipping #if 0 -> #if 1 when re-investigating layout.
#if 0
            if (verbose)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[HorseMod.KHit] pi={} ue_chara=0x{:x} slot=0x{:x} "
                        "using_slot={} body=0x{:x} atk=0x{:x} hurt=0x{:x} "
                        "bodyN={} atkN={} hurtN={} hurtSlots={}\n"),
                    poseSelector,
                    reinterpret_cast<uintptr_t>(ue_chara),
                    reinterpret_cast<uintptr_t>(slot_chara),
                    used_slot ? 1 : 0,
                    reinterpret_cast<uintptr_t>(body_head),
                    reinterpret_cast<uintptr_t>(atk_head),
                    reinterpret_cast<uintptr_t>(hurt_head),
                    body_count, atk_count, hurt_count, hurt_slot_count);

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
#endif

            // ----------------------------------------------------------------
            // Engine frame-window state — pre-read once per chara per tick.
            // ----------------------------------------------------------------
            // chara+0x1980 is the engine's per-tick attack-phase classifier
            // output (1=Startup, 2=Active, 3=Recovery, 0=no active cell).
            // Updated every tick by LuxBattle_PreTickStateSnapshot...
            // (0x14034FCE0) via LuxMoveVM_ClassifyHitboxFrameState
            // (0x140300620) before TickHitResolution runs, so by the time
            // we read it from the cockpit / Slate hook the value is the
            // latest engine-truth.
            //
            // Reading once here amortises the read cost across all three
            // list walks (attack / hurtbox / body) for this chara.  The
            // phase value is constant across every node on this chara
            // for this tick, so the pre-read is the right shape.
            const KHitAttackPhase chara_phase = readAttackPhase(list_chara);

            // Active cell's MasterWindow start/end (cell+0x36/+0x38).  Used
            // for HUD frame-data display.  Both = 0 when no cell is active
            // (e.g. neutral / non-attacking move).
            int16_t mwin_start = 0, mwin_end = 0;
            readActiveCellMasterWindow(list_chara, &mwin_start, &mwin_end);

            // Boolean from chara+0x16EA — same as (phase == Active) AND
            // sub-window inhibitors quiet.  HUD active indicators should
            // prefer this for exact engine-truth fidelity (it folds in
            // the inhibitor short-circuits the phase tag alone doesn't).
            uint8_t in_master_window_byte = 0;
            SafeReadUInt8(bytes + ChaOffsets::InMasterWindowFlag,
                          &in_master_window_byte);
            const bool in_master_window = (in_master_window_byte != 0);

            // Active-cell attack flags (cell+0x32) and input cond (cell+0x34).
            // Stamped onto every attack KHitDraw so callers can colour /
            // label hitboxes by tier (High / Mid / Low / Unblockable).
            // Both = 0 when no cell is active.
            uint16_t cell_attack_flags = 0, cell_input_cond = 0;
            readActiveCellAttackFlags(list_chara, &cell_attack_flags,
                                      &cell_input_cond);
            const KHitAttackTier cell_attack_tier =
                ClassifyAttackTier(cell_attack_flags);

            // Defender-side stance state.  Stamped onto every KHitDraw on
            // this chara so the HUD can show the chara's current stance
            // alongside the rendered hitbox/hurtbox geometry.  These are
            // the bytes EvaluateMoveTransition reads when classifying an
            // incoming hit — see the readDefenderStance() doc block.
            const DefenderStanceSnapshot stance =
                readDefenderStance(list_chara);

            // Throw-dispatch height gate snapshot.  Surfaces the dispatch-
            // layer gate that can silently drop throws even after geometry
            // overlap + classifier register them.  See KHitDraw fields
            // `throw_height_gate_ok` / `defender_effective_height` for
            // the full doc.
            const ThrowHeightSnapshot throw_height =
                readThrowHeightSnapshot(list_chara);

            // Chara-wide final hit-reaction result code (chara+0x43DA0).
            // Complements the per-slot reaction_state by capturing the
            // mutual-hit-arbitration consolidated outcome.  See KHitDraw
            // `final_hit_result_code` doc for the value table.
            const int32_t final_hit_result =
                readFinalHitResultCode(list_chara);

            // --- Attack list -------------------------------------------------
            walkList(ue_chara, poseSelector, atk_head,
                     KHitList::Attack, active_cell, own_attack_mask,
                     reactions_hot, hurt_slot_count, defender_can_react,
                     chara_phase, mwin_start, mwin_end, in_master_window,
                     cell_attack_flags, cell_input_cond, cell_attack_tier,
                     stance, throw_height, final_hit_result,
                     alt_snap,
                     verbose, visit);
            // --- Hurtbox list ------------------------------------------------
            walkList(ue_chara, poseSelector, hurt_head,
                     KHitList::Hurtbox, active_cell, own_attack_mask,
                     reactions_hot, hurt_slot_count, defender_can_react,
                     chara_phase, mwin_start, mwin_end, in_master_window,
                     cell_attack_flags, cell_input_cond, cell_attack_tier,
                     stance, throw_height, final_hit_result,
                     alt_snap,
                     verbose, visit);
            // --- Body / pushbox list -----------------------------------------
            walkList(ue_chara, poseSelector, body_head,
                     KHitList::Body, active_cell, own_attack_mask,
                     reactions_hot, hurt_slot_count, defender_can_react,
                     chara_phase, mwin_start, mwin_end, in_master_window,
                     cell_attack_flags, cell_input_cond, cell_attack_tier,
                     stance, throw_height, final_hit_result,
                     alt_snap,
                     verbose, visit);
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

        // Configure the sticky-flash hold window, in frames.  Called from
        // the dllmain UI (ms slider) at startup and whenever the user
        // drags the duration knob.  Clamped to [0, 600] — 600 frames at
        // 60fps is 10 seconds, well past the UI's 1s cap but cheap to
        // enforce.
        static void setStickyFrames(int frames)
        {
            if (frames < 0)   frames = 0;
            if (frames > 600) frames = 600;
            s_sticky_frames.store(frames, std::memory_order_relaxed);
        }
        static int stickyFrames()
        {
            return s_sticky_frames.load(std::memory_order_relaxed);
        }

    private:
        // Per-player sticky flash state — held for `s_sticky_frames`
        // frames after raw PerHurtboxReactionState goes non-zero so the
        // red flash lasts long enough to be visible at 60fps.
        static inline int32_t s_sticky_reactions[2][22] = {};

        // Per-player previous-frame snapshot of the PerHurtboxReactionState
        // i32[22] array (chara+0x1C74).  Drives the edge-triggered
        // diagnostic that surfaces 0->non-zero transitions.  Sized to the
        // engine's hard cap of 22 slots (matches kReactSlotMax in tick()).
        // Was 128 historically when we were probing for the storage
        // location; now that it's confirmed via Ghidra cross-reference,
        // sized to fit exactly.
        static inline int32_t s_react_scan_prev[2][22] = {};

        // Per-player previous-cockpit snapshot of g_LuxBattle_FrameCounter
        // (imageBase+0x470D0C4).  The global is incremented exactly once
        // per LuxBattle_PerFrameTick, which Horse::WorldTickGate gates
        // at the entry — so this counter halts under freeze, advances
        // one-per-game-frame under step, and advances at the gate's
        // slowed cadence under slow-mo.  Per-player only because each
        // chara's sticky array is per-player and we want each pi to
        // converge from its own history (drains stay synced across
        // ticks because both pi values read the same global).
        // -1 sentinel = no prior sample yet (first frame).
        static inline int64_t s_prev_frame_counter[2] = { -1, -1 };

        // Runtime-configurable sticky window length, in GAME ticks
        // (60fps assumed).  Default 15 ≈ 250ms; dllmain overrides it
        // on unreal-init + every slider drag.
        static inline std::atomic<int> s_sticky_frames{15};

        // ----------------------------------------------------------------
        // Per-node Area-SPINE cache for engine-truth swept rendering.
        //
        // Why: KHitArea::OverlapTest @ 0x14030E4E0 tests against a shape
        // built from THREE world-space points (cur_P1, cur_P2, prev_P2
        // for OBB-1; prev_P1, prev_P2, cur_P1 for OBB-2 — see plate on
        // PopulateOverlapScratch @ 0x14030E610).  The visualiser needs
        // the prev-frame endpoint pair to render the engine's swept
        // hit volume faithfully.
        //
        // Cache structure: each entry stores BOTH the previous-game-tick
        // endpoints AND the current-game-tick endpoints, plus the game
        // tick we last updated at.  Shift logic:
        //   * lastTick == curTick       → same game tick (multiple render
        //                                  frames within one game tick).
        //                                  DO NOT shift; serve existing
        //                                  cur/prev and overwrite cur with
        //                                  fresh sample (idempotent — the
        //                                  bone is frozen so this is a
        //                                  redundant write, but harmless).
        //   * lastTick + 1 == curTick   → one game tick advanced.  Shift:
        //                                  prev = cur, cur = fresh.
        //                                  has_prev = true.
        //   * gap > 1                   → multi-tick gap (round restart,
        //                                  off-screen, walker disabled).
        //                                  Reset: prev = cur = fresh.
        //                                  has_prev = false.
        //
        // Why the shift-on-tick-advance design: the cockpit hook fires
        // every render frame (60–144 Hz typical), but the game's tick
        // counter only advances when LuxBattle_PerFrameTick runs (60 Hz,
        // halted during pause / freeze).  An earlier "single-buffer"
        // design overwrote prev on every cockpit fire, which meant:
        //
        //   * At >60 Hz render: swept quad flashed for one render frame
        //     per tick, then collapsed to cur-only for the remaining
        //     renders in that tick — visible flicker.
        //   * Under frame-step: the first render after a step showed the
        //     quad, then subsequent paused renders hid prev.  User
        //     stepping and inspecting saw "no prev was drawn this tick"
        //     even though the engine did sweep.
        //
        // The dual-buffer design here serves the SAME cached prev for
        // every render frame within one game tick, so the swept quad
        // persists until the game advances another tick.  Frame-step
        // works correctly: step → tick advances → cache shifts → quad
        // shows the engine's actual prev→cur sweep, and STAYS visible
        // for as long as the user inspects that paused tick.
        //
        // Cache layout: linear-probe hash table, 256 slots per player,
        // keyed by node pointer.  P1 and P2 KHit nodes live in disjoint
        // per-player global scratch regions (DAT_14485ebd0 +
        // pi*0x4000), so pointer collisions across players are impossible.
        //
        // Memory: 2 players × 256 slots × (8 byte ptr + 4 byte tick +
        // 48 byte four-vec3 + 1 byte flag) = ~31 KB.
        struct AreaSpineCacheEntry
        {
            void*    node      = nullptr;
            uint32_t lastTick  = 0;
            FVec3    cur_p1    = {};
            FVec3    cur_p2    = {};
            FVec3    prev_p1   = {};
            FVec3    prev_p2   = {};
            bool     has_prev  = false;
        };
        static constexpr int kAreaSpineCacheSize = 256;
        static inline AreaSpineCacheEntry s_area_spine_cache[2][kAreaSpineCacheSize] = {};

        // Hash a node pointer down to a cache-slot index.  Linear probe
        // for collisions; returns the index of either an existing entry
        // for `node` or the first empty/evictable slot.  Tries up to 8
        // probes before giving up (returns -1) — far more than needed
        // for our 40-ish-nodes-per-chara workload.
        static int findAreaSpineCacheSlot(uint32_t pi, void* node)
        {
            if (pi >= 2) return -1;
            auto& bucket = s_area_spine_cache[pi];
            const uintptr_t h = reinterpret_cast<uintptr_t>(node);
            // Mix the pointer bits — KHit nodes are 0x80-byte stride so
            // raw masking would put consecutive nodes in adjacent slots.
            const uint32_t mixed = static_cast<uint32_t>(
                (h >> 7) ^ (h >> 15) ^ (h >> 23));
            int idx = static_cast<int>(mixed % kAreaSpineCacheSize);
            for (int i = 0; i < 8; ++i)
            {
                AreaSpineCacheEntry& e = bucket[idx];
                if (e.node == node || e.node == nullptr) return idx;
                idx = (idx + 1) % kAreaSpineCacheSize;
            }
            return -1; // give up — visualisation will just lack prev this tick
        }

        // Read the global game-frame counter once for the spine cache.
        // Mirrors the read in forEachKHit but isolated so buildAreaWorld
        // can call it without plumbing the value through every helper.
        static uint32_t readGameFrameCounter() noexcept
        {
            constexpr uintptr_t kFrameCounterRVA = 0x470D0C4;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return 0;
            uint32_t cur = 0;
            if (!SafeReadUInt32(reinterpret_cast<const void*>(
                    base + kFrameCounterRVA), &cur))
                return 0;
            return cur;
        }

        template <class Visit>
        static void walkList(void* chara,
                             uint32_t poseSelector,
                             void* head,
                             KHitList listKind,
                             void* /*activeAttackCell — cross-chara, unused*/,
                             uint64_t ownAttackMask,
                             const int32_t (&reactions)[22],
                             int32_t hurtSlotCount,
                             bool charaEngineCanReact,
                             // Engine attack-phase tag (chara+0x1980)
                             // pre-read once per chara per tick.  Stamped
                             // onto every KHitDraw and AND'd into the
                             // is_per_frame_active filter.  See
                             // KHitDraw::engine_phase for the rationale.
                             KHitAttackPhase charaPhase,
                             // Active cell's MasterWindow start/end (cell+
                             // 0x36/+0x38) pre-read once per chara per tick.
                             // 0/0 if no active cell.  Stamped onto every
                             // KHitDraw for HUD use.
                             int16_t  charaMasterWindowStart,
                             int16_t  charaMasterWindowEnd,
                             // Boolean from chara+0x16EA — same as
                             // (phase == Active) AND inhibitors quiet.
                             bool     charaInMasterWindow,
                             // Active cell metadata (cell+0x32 AttackFlags
                             // + cell+0x34 InputCond + derived tier).
                             // Pre-read once per chara per tick.  Stamped
                             // onto every KHitDraw on this chara so the
                             // renderer can colour/label hitboxes by
                             // tier (high/mid/low/unblockable).
                             uint16_t        charaAttackFlags,
                             uint16_t        charaInputCond,
                             KHitAttackTier  charaAttackTier,
                             // Defender stance/state snapshot.  Pre-read
                             // once per chara per tick.  Stamped onto
                             // every KHitDraw so the HUD can show the
                             // defender's current stance and block-state
                             // alongside the rendered geometry.
                             const DefenderStanceSnapshot& charaStance,
                             // Throw-dispatch height gate snapshot.  Same
                             // pre-read-once pattern; stamped on every
                             // KHitDraw.  See readThrowHeightSnapshot().
                             const ThrowHeightSnapshot& charaThrowHeight,
                             // Chara-wide final hit-result code from
                             // chara+0x43DA0.  Pre-read; stamped on every
                             // KHitDraw.  Value table in KHitDraw doc.
                             int32_t         charaFinalHitResultCode,
                             // Chara-wide Lane 2 alt-classify snapshot,
                             // pre-read once per chara per tick.  When
                             // `is_open` is true and an attack node's
                             // category bit falls in `lane2_mask`, the
                             // engine will fire damage via the alt
                             // classifier pass even if the bit is NOT in
                             // the primary cell's mask.  is_per_frame_active
                             // for ATTACK nodes OR's this into the
                             // per-cell check.  Stamped onto every
                             // KHitDraw as `alt_classify_open` /
                             // `alt_attack_mask` (always identical
                             // across every node of this chara on this
                             // tick — chara-wide state).
                             const Lane2AltClassifySnapshot& charaAltSnap,
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
                uint16_t activeGate= 0;   // +0x14, engine's per-frame live bit
                uint32_t flags10   = 0;
                uint64_t cat_mask  = 0;   // +0x08, CategoryMask or BoneBitFlag
                void*    next      = nullptr;
                if (!SafeReadUInt8(nbytes + KHitOffsets::StreamTypeTag, &streamTag))
                    break;
                if (!SafeReadUInt8(nbytes + KHitOffsets::BoneIdByte, &boneId))
                    break;
                SafeReadUInt16(nbytes + KHitOffsets::IsActiveThisFrame, &activeGate);
                SafeReadUInt32(nbytes + KHitOffsets::Flags10, &flags10);
                SafeReadUInt64(nbytes + KHitOffsets::CategoryMask, &cat_mask);
                SafeReadPtr(nbytes + KHitOffsets::Next, &next);
                ++walked;

                // Build the common draw-prim fields.
                KHitDraw d{};
                d.list                  = listKind;
                // Geometry gate — engine's own +0x14 flag.  True for
                // ALL slots in the 0x3FFFD always-on floor every
                // frame; see note on KHitDraw::is_current_attack and
                // the doc for KHitOffsets::IsActiveThisFrame.
                d.is_current_attack     = (listKind == KHitList::Attack &&
                                           activeGate != 0);
                // Raw +0x14 for diagnostic purposes.
                //
                // ATTACK nodes: this is the engine's per-frame "hot"
                // flag, updated each tick by
                //   *(node+0x14) = (hotMask >> node[+0x17]) & 1
                // in LuxBattle_TickHitResolutionAndBodyCollision
                // @ 0x14033CCA0.  hotMask = 0x3FFFD | animCellMask |
                // ownActiveCellMask, so the floor pins slots {0, 2..17}
                // hot every frame.
                //
                // HURTBOX / BODY nodes: NOT touched per-frame.  The
                // value persists from whatever the move's hurtbox-
                // stream deserialiser wrote at load time.  Most
                // characters use 1 (overlap considered every frame),
                // but SOME hurtboxes are authored with +0x14 = 0 —
                // typically character-specific "decoy" boxes that
                // exist as visual reference but are engine-DISABLED.
                //
                // Per the overlap loop in
                // LuxBattleChara_UpdateAllKHitWorldCenters
                // @ 0x14030D6A0, the defender-side `if (hurt[+0x14]
                // != 0)` is a HARD GATE — a hurtbox with +0x14 = 0
                // is skipped entirely from the overlap test, so its
                // PerHurtboxBitmask slot never gets attacker bits
                // OR'd in, and the classifier never fires a reaction
                // for it.  Verified 2026-04-30 with Geralt's two
                // large rectangle hurtboxes.
                d.geom_active           = (activeGate != 0);

                // For hurtbox nodes specifically: the same +0x14
                // doubles as the OVERLAP-TEST GATE.  See the
                // overlap_active doc on KHitDraw for the engine
                // truth.  Stored as a separate field so the filter
                // logic in dllmain doesn't have to remember the
                // geom_active dual-purpose.
                d.overlap_active        = (activeGate != 0);

                // is_invul_slot — true iff this is a HURTBOX node whose
                // slot has been disabled by the move-script's VM opcode
                // 0x13AC (LuxMoveVM_SetHurtboxSlotsActiveMask).  This is
                // the per-node marker for "this slot is in an i-frame /
                // armor / parry window THIS TICK".  See KHitDraw::is_
                // invul_slot doc and the readHurtboxInvulState helper
                // for the chara-wide companion predicate.
                //
                // Composition (set later, after classifier_addressable
                // is computed for hurtbox nodes — see the hurtbox
                // branch below):
                //   list == Hurtbox AND
                //   classifier_addressable AND
                //   activeGate == 0
                // For attack / body nodes the field stays false (the
                // VM opcode that drives this only operates on the
                // hurtbox list; body has its own sister opcode that
                // we don't currently surface).

                // Chara-wide engine gate (per-call, same for every
                // node on this chara this tick).  Composed from
                // the three early-return sites in the resolver —
                // see KHitDraw::defender_can_react_engine doc and
                // the readDefenderEngineActive() helper.  Stored
                // on every list kind because the same gate also
                // disables ATTACK boxes (an incapacitated chara
                // doesn't deal damage either) and BODY pushboxes
                // (still resolved by the physics solver, but the
                // overlay's "show only engine-active" toggle
                // applies uniformly).
                d.defender_can_react_engine  = charaEngineCanReact;
                d.attacker_can_strike_engine = charaEngineCanReact;

                // Classifier addressability (hurtbox only).  True when
                // `boneId < ClassifierHurtboxBound` AND `boneId < 22` —
                // the exact predicate ResolveAttackVsHurtboxMask22 uses
                // for its inner loop bound.  A hurtbox failing this
                // test is geometrically live (overlaps tested, bits
                // OR'd into PerHurtboxBitmask[boneId]) but its slot is
                // outside the classifier's iteration range, so no
                // reaction will ever be written and no damage will
                // ever be dealt — the invisible "meta-hurtbox" trap.
                //
                // Note: `hurtSlotCount` here is read from
                // chara+0x44494 (ClassifierHurtboxBound), which in
                // this build is actually the ATTACK list's max-slot
                // reused as the hurtbox iteration bound.  See the
                // ChaOffsets::ClassifierHurtboxBound doc block for
                // why — short version: moves with small attack lists
                // can cause legitimate per-move hurtboxes at high
                // slot indices to be flagged unaddressable.
                //
                // For non-hurtbox lists we leave the default (true) —
                // the concept doesn't apply.
                if (listKind == KHitList::Hurtbox)
                {
                    const int32_t cap = (hurtSlotCount > 22) ? 22
                                      : (hurtSlotCount < 0) ?  0
                                      :  hurtSlotCount;
                    d.classifier_addressable =
                        (static_cast<int32_t>(boneId) < cap);

                    // is_invul_slot: this hurtbox slot is in classifier
                    // range AND has been disabled by VM opcode 0x13AC.
                    // The deserialiser sets +0x14 = 1 by default, so
                    // a hurtbox in addressable range with +0x14 == 0
                    // is necessarily in an i-frame / armor / parry
                    // window — the move-script VM has explicitly
                    // turned this slot off.
                    d.is_invul_slot = d.classifier_addressable
                                   && (activeGate == 0);
                }
                // Damage gate — slot bit set in THIS chara's own
                // active-attack-cell mask.  Treats chara[+0x44058] as
                // a slot bitmap (one of its two engine interpretations)
                // and tests bit (boneId & 0x3F).  Carried as a separate
                // field so consumers that want a "slot is in the
                // current move's plan" predicate can use it directly;
                // is_per_frame_active below adds the +0x14 hot gate
                // and category-mask intersection on top for the
                // strictly tighter "engine will fire damage" gate.
                // (The legacy "Damage-active only" UI toggle that
                // routed this flag was removed 2026-05 in favour of
                // the unified "Only show active boxes" filter, which
                // uses is_per_frame_active.)
                d.is_damage_active      = (listKind == KHitList::Attack &&
                                           slotBitInMask(boneId,
                                                         ownAttackMask));
                // Per-frame damage gate — mirrors the classifier
                // predicate at ResolveAttackVsHurtboxMask22 (0x14033C100):
                //   capable_of_damage iff (+0x14 != 0)
                //                       && ((node.CategoryMask & per_move_cell) != 0)
                //
                // This handles BOTH cases correctly:
                //   * Floor-slot attacks (slots 0, 2..17, body-attached
                //     hitboxes whose +0x14 is always set by the floor)
                //     get the category-intersection check, which hides
                //     them during neutral and during moves whose
                //     authored category set doesn't overlap.
                //   * Non-floor attacks already passed through +0x14's
                //     slot-bit gate, so the category intersection is
                //     just an extra "and our move is doing this kind
                //     of damage" filter.
                //
                // See KHitDraw::is_per_frame_active for the dual-
                // interpretation explanation (same 64 bits used as
                // either slot mask or category mask depending on the
                // engine site).
                // Stamp the chara-wide engine phase + master-window
                // info onto every KHitDraw produced this tick.  These
                // fields are constant across every node on this chara
                // for this tick — see KHitDraw doc for rationale.
                d.engine_phase          = charaPhase;
                d.in_master_window      = charaInMasterWindow;
                d.master_window_start   = charaMasterWindowStart;
                d.master_window_end     = charaMasterWindowEnd;

                // Active cell attack metadata — see KHitDraw doc block
                // "Active attack cell metadata".  Same value on every
                // node on this chara this tick.
                d.attack_flags             = charaAttackFlags;
                d.attack_input_cond        = charaInputCond;
                d.attack_tier              = charaAttackTier;

                // Defender stance / state — see KHitDraw doc block
                // "Defender-side stance state".
                d.defender_crouching             = charaStance.crouching;
                d.defender_crouching_base        = charaStance.crouching_base;
                d.defender_crouching_alt         = charaStance.crouching_alt;
                d.defender_alt_lock_gate         = charaStance.alt_lock_gate;
                d.defender_actively_blocking     = charaStance.actively_blocking;
                d.defender_blockable_this_frame  = charaStance.blockable_this_frame;
                d.defender_in_blockstun          = charaStance.in_blockstun;
                d.defender_in_hitstun            = charaStance.in_hitstun;
                d.defender_invul_a               = charaStance.invul_a;
                d.defender_invul_b               = charaStance.invul_b;
                d.defender_guard_broken          = charaStance.guard_broken;
                d.defender_guard_disabled        = charaStance.guard_disabled;
                d.defender_force_allow           = charaStance.force_allow;

                // Throw-dispatch height gate (see KHitDraw doc block).
                d.defender_effective_height = charaThrowHeight.defender_height;
                d.attacker_effective_height = charaThrowHeight.attacker_height;
                d.throw_height_gate_ok      = charaThrowHeight.gate_ok;

                // Chara-wide final hit-reaction result code (chara+0x43DA0).
                d.final_hit_result_code     = charaFinalHitResultCode;

                // Chara-wide Lane 2 alt-classify state — stamp on every
                // KHitDraw for both attack and hurt/body kinds (the
                // value is constant across all nodes on this chara this
                // tick). Hurtbox/body kinds won't use it for filtering
                // but the renderer / HUD might display the open flag.
                d.alt_classify_open  = charaAltSnap.is_open;
                d.alt_attack_mask    = charaAltSnap.lane2_mask;

                // is_per_frame_active — narrowed (2026-05) to the engine's
                // own per-tick "active frames" predicate by AND'ing in
                // (charaPhase == Active).  Without this, the existing
                // gates (+0x14 hot AND slot-mask intersect) span the
                // entire move because both sources are per-MOVE-SLOT,
                // not per-game-frame.  See KHitDraw::is_per_frame_active
                // doc above for the full reasoning + history.
                //
                // EXTENDED 2026-05-16: also accept the bit if the chara's
                // Lane 2 alt-classify is gated open AND the attack's
                // category bit lives in the Lane 2 cell mask.  This
                // covers Soul Charge alt-attacks, Guard Impact counters,
                // parry counters, and stance-tech where the engine
                // fires damage via a second classifier pass against the
                // Lane 2 cell — slots authored as live in Lane 2 but
                // not in the primary cell would otherwise be incorrectly
                // hidden by the "Only show active boxes" filter.
                const bool primary_active =
                    (cat_mask & ownAttackMask) != 0;
                const bool alt_active =
                    charaAltSnap.is_open &&
                    (cat_mask & charaAltSnap.lane2_mask) != 0;
                d.is_per_frame_active = (listKind == KHitList::Attack &&
                                         activeGate != 0 &&
                                         (primary_active || alt_active) &&
                                         charaPhase == KHitAttackPhase::Active);
                d.stream_tag            = streamTag;
                d.bone_id_internal      = boneId;
                d.flags10               = flags10;
                d.category_or_bone_mask = cat_mask;
                // Hurtbox slot is the node's authored +0x17 (SubIdOrBoneId),
                // NOT the linked-list position.  The engine's classifier at
                // 0x14033C100 writes PerHurtboxReactionState[slotIndex] where
                // slotIndex mirrors the index used in PerHurtboxBitmask, and
                // UpdateAllKHitWorldCenters OR's attacker bits into
                // PerHurtboxBitmask[hurt->+0x17].  So the reaction table is
                // keyed by +0x17.  Using list_index here would cause visual
                // cross-talk: a hurtbox whose own +0x17 is >= ClassifierHurtboxBound
                // (never addressable by the classifier) would spuriously flash
                // whenever some OTHER hurtbox with +0x17 == its list-position
                // got hit.  Observed in-game with Grøh's whole-body meta sphere.
                d.hurtbox_slot          = (listKind == KHitList::Hurtbox &&
                                           boneId < 22)
                                            ? static_cast<int32_t>(boneId)
                                            : -1;

                // Engine-derived role — only meaningful for attacks.
                d.attack_role = (listKind == KHitList::Attack)
                                ? ClassifyAttackRole(cat_mask)
                                : KHitAttackRole::NotAttack;

                // Defender-side reaction lookup.  `reactions[]` is the
                // sticky-flash-extended view of PerHurtboxReactionState;
                // we copy the raw enum value through so colourFor can
                // shade by reaction type if it wants to.
                if (d.hurtbox_slot >= 0)
                {
                    d.reaction_state = reactions[d.hurtbox_slot];
                    d.reaction_hot   = (d.reaction_state != 0);
                }

                // Verbose diagnostic — dump the bone FMatrix we get back
                // for the first bone-attached node of each list.  DISABLED
                // wholesale: the scaffolding here calls the native bone
                // transform (side effect) just to feed a log, which we now
                // skip.  Re-enable to validate GetBoneTransformForPose.
#if 0
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
#endif

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

                // ------------------------------------------------------------
                // BELOW-GROUND DIAGNOSTIC (2026-05-15)
                // ------------------------------------------------------------
                // When a sphere renders with its centre Z below the visible
                // floor (treated as Z <= -5 UE cm to absorb terrain-pad noise),
                // dump the identifying fields so the user can correlate the
                // overlay to a specific bone/slot/move.  Captures:
                //   - +0x17 KindTag (slot index, drives PerHurtboxBitmask /
                //                    classifier addressability; 6/7 trigger
                //                    ground-clamp branch)
                //   - +0x7C UE4 bone index (which skeletal bone the sphere
                //                            is attached to in UE-space)
                //   - +0x70 live radius (post-anim-cell scale + post-ground-
                //                        clamp inflation)
                //   - +0x74 authored radius
                //   - +0x50..+0x5B Namco world center (engine's authoritative
                //                  position — includes ground-clamp pull on
                //                  slot 6/7; the bone-matrix path we render
                //                  from does NOT include this)
                //   - rendered UE world centre (what the overlay drew)
                //
                // De-dups per (chara_id, list_kind, kind_tag, ue_bone_idx)
                // tuple so log spam stays bounded.  Each unique tuple is
                // logged exactly once per mod lifetime — a "first sighting"
                // record.  The cap is generous: 256 unique tuples is more
                // than the entire SC6 chara roster's combined slot inventory.
                if (ok && streamTag == 0 && d.centre.Z <= -5.0f)
                {
                    // Read Namco-world center (+0x50..+0x5B) so we can compare
                    // to the bone-matrix-derived UE position we just rendered.
                    // If these differ significantly after Namco->UE conversion
                    // (Z->X, X->Y, Y->Z, ×100), the ground-clamp branch has
                    // fired for this node.
                    float namco_x = 0.0f, namco_y = 0.0f, namco_z = 0.0f;
                    SafeReadFloat(nbytes + KHitOffsets::SphereWorldCenterCur + 0,
                                  &namco_x);
                    SafeReadFloat(nbytes + KHitOffsets::SphereWorldCenterCur + 4,
                                  &namco_y);
                    SafeReadFloat(nbytes + KHitOffsets::SphereWorldCenterCur + 8,
                                  &namco_z);
                    // Namco (X, Y, Z) -> UE (Z, X, Y) ×100.  Matches the
                    // axis-swap used in buildFixAreaWorld.
                    const float engine_ue_x = namco_z * 100.0f;
                    const float engine_ue_y = namco_x * 100.0f;
                    const float engine_ue_z = namco_y * 100.0f;

                    float authored_radius = 0.0f;
                    SafeReadFloat(nbytes + KHitOffsets::SphereRadiusAuthored,
                                  &authored_radius);

                    // De-dup key: (pi, list, kind_tag, bone_idx).  Pack into
                    // a single u32 for a small linear-probe seen-set.
                    const uint32_t key =
                          (static_cast<uint32_t>(poseSelector & 1) << 28)
                        | (static_cast<uint32_t>(listKind) << 24)
                        | (static_cast<uint32_t>(boneId) << 16)
                        | (ueBoneDbg & 0xFFFFu);
                    constexpr int kSeenCap = 256;
                    static uint32_t s_below_seen[kSeenCap] = {};
                    static int      s_below_count = 0;
                    bool already = false;
                    for (int i = 0; i < s_below_count && i < kSeenCap; ++i)
                    {
                        if (s_below_seen[i] == key) { already = true; break; }
                    }
                    if (!already && s_below_count < kSeenCap)
                    {
                        s_below_seen[s_below_count++] = key;
                        const bool likely_ground_clamp =
                            (boneId == 6 || boneId == 7);
                        const float engine_vs_render_dz =
                            engine_ue_z - d.centre.Z;
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.BelowGround] pi={} list={} kind=0x{:02x}"
                                " ueBone=0x{:x} clamp_path={}"
                                " r_live={:.2f} r_auth={:.2f}"
                                " render_UE=({:.1f},{:.1f},{:.1f})"
                                " engine_UE=({:.1f},{:.1f},{:.1f})"
                                " dZ_engine_minus_render={:.2f}\n"),
                            poseSelector,
                            static_cast<int>(listKind),
                            static_cast<int>(boneId),
                            ueBoneDbg,
                            likely_ground_clamp ? STR("YES") : STR("no"),
                            d.radius, authored_radius * kLuxCmToUE,
                            d.centre.X, d.centre.Y, d.centre.Z,
                            engine_ue_x, engine_ue_y, engine_ue_z,
                            engine_vs_render_dz);
                    }
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
                // DISABLED (noisy: 10+ lines per list per chara per tick).
                // Re-enable to reverse KHit subclass layouts.
#if 0
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
#endif

                if (ok)
                {
                    // Per-hurtbox and per-strike diagnostics — DISABLED
                    // (fire per-node across both charas, up to ~40 lines
                    // per ~2s).  Re-enable for deep-dive layout/role work.
#if 0
                    if (verbose && listKind == KHitList::Hurtbox)
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.Hurt]  listIdx={:2} slot={:2} tag={} "
                                "+0x17=0x{:02x}({}) slotCount={} addr={} "
                                "flags10=0x{:08x} boneMask=0x{:016x} react={}\n"),
                            list_index, d.hurtbox_slot, streamTag, boneId,
                            static_cast<int>(boneId), hurtSlotCount,
                            d.classifier_addressable ? STR("Y") : STR("N"),
                            flags10, cat_mask, d.reaction_state);
                    }

                    if (verbose && listKind == KHitList::Attack)
                    {
                        const auto* role_str =
                            (d.attack_role == KHitAttackRole::Throw)  ? STR("throw")  :
                            (d.attack_role == KHitAttackRole::Strike) ? STR("strike") :
                                                                        STR("-"     );
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[HorseMod.Atk]   idx={:2} tag={} bone=0x{:02x} "
                                "flags10=0x{:08x} catMask=0x{:016x} role={} "
                                "active(gate@+0x14)={} ({})\n"),
                            list_index, streamTag, boneId, flags10,
                            cat_mask, role_str,
                            activeGate,
                            d.is_current_attack ? STR("live") : STR("cold"));
                    }
#endif

                    // ALWAYS-ON, but one-shot per unique slot: fire a
                    // debug line the FIRST time we observe each Throw-role
                    // attack node.  Confirms the bit-31/55 classification
                    // actually identifies grabs/throws in-game.  See
                    // LuxBattle_ResolveAttackVsHurtboxMask22 (0x14033C100)
                    // for why these bits mean "throw" (they drive the
                    // yarare-id copy that syncs paired throw animations).
                    //
                    // Dedup uses a 64-bit static "seen" mask keyed by the
                    // node's +0x17 slot, so we log at most once per slot
                    // index per mod lifetime.
                    if (listKind == KHitList::Attack &&
                        d.attack_role == KHitAttackRole::Throw)
                    {
                        static std::atomic<uint64_t> s_throw_seen{0};
                        const uint64_t slotBit =
                            1ull << (static_cast<uint32_t>(boneId) & 63u);
                        const uint64_t prev =
                            s_throw_seen.fetch_or(slotBit,
                                                  std::memory_order_relaxed);
                        if ((prev & slotBit) == 0)
                        {
                            RC::Output::send<RC::LogLevel::Verbose>(
                                STR("[HorseMod.Throw] FIRST SEEN pi={} "
                                    "listIdx={} slot={} catMask=0x{:016x} "
                                    "active(gate@+0x14)={} ({})\n"),
                                poseSelector, list_index,
                                static_cast<int>(boneId),
                                cat_mask, activeGate,
                                d.is_current_attack ? STR("live") : STR("cold"));
                        }
                    }

                    visit(static_cast<const KHitDraw&>(d));
                    ++emitted;
                }

                node = next;
            }

            // Per-list summary — DISABLED (6 lines per log tick across
            // both charas × 3 lists).
#if 0
            if (verbose)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[HorseMod.KHit]   list={} head=0x{:x} walked={} emitted={}\n"),
                    static_cast<int>(listKind),
                    reinterpret_cast<uintptr_t>(head),
                    walked, emitted);
            }
#endif
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

        // KHitArea: a 1D SPINE (P1 → P2) attached to a bone.  P1 at
        // +0x30 and P2 at +0x40 are AUTHORED ENDPOINTS — not diagonal
        // corners of an OBB.  The engine's overlap test
        // (KHitArea::OverlapTest @ 0x14030E4E0) reads them as the
        // endpoints of the attacker's spine, then builds the actual hit
        // shape at test time from THREE points via Gram-Schmidt
        // (cur_P1, cur_P2, prev_P2 for one OBB; prev_P1, prev_P2,
        // cur_P1 for the other — see PopulateOverlapScratch @
        // 0x14030E610 + BuildHitboxLocalMatrix @ 0x14030BBA0).  The
        // cross-section thickness of those OBBs is derived from how
        // far the bone moved between frames, so a stationary spine
        // tests as zero-thickness and a moving spine picks up
        // thickness equal to its motion delta.
        //
        // Historical note
        // ---------------
        // Earlier revisions of this function rendered an 8-corner OBB
        // derived from min/max permutations of bone-local P1/P2 in
        // bone-local space.  That interpretation was incorrect — it
        // produced a thin AABB along the spine that VISUALLY resembled
        // a weapon but did NOT match the engine's actual hit shape.
        // 2026-05-14 audit traced this to a misreading of the
        // deserializer; the engine's overlap test uses P1/P2 as spine
        // endpoints (1D), not OBB diagonals.
        //
        // What we render now
        // -------------------
        // Just the engine's SOURCE DATA:
        //   * Cur spine = cur_P1_world → cur_P2_world (always drawn)
        //   * Prev spine = prev_P1_world → prev_P2_world (drawn iff the
        //     per-node cache holds a one-tick-old snapshot for THIS node)
        //   * Two connecting edges: cur_P1→prev_P1 and cur_P2→prev_P2
        //     (drawn iff has_prev_spine — together with the two spines
        //     they outline the swept quad the engine just hit-tested)
        //
        // Stationary attacks: prev ≈ cur, quad collapses to a near-
        // single line.  Moving attacks: quad opens up, visually
        // communicating the swept envelope.  This matches the engine's
        // behavior: zero-motion ⇒ zero cross-section, motion ⇒
        // cross-section proportional to motion delta.
        //
        // Two-bone caveat
        // ----------------
        // P1 and P2 can attach to DIFFERENT bones (+0x90 holds bone A
        // for P1, +0x94 holds bone B for P2).  Common case is same
        // bone, but for swept-limb volumes the two endpoints can be
        // on different bones.  We resolve each endpoint through its
        // own bone transform.
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

            // Cur spine endpoints in UE world.  P1 through bone A
            // (read from +0x90), P2 through bone B (read from +0x94).
            // Same-bone case (the common authoring) is a degenerate
            // case of this — boneA == boneB and the spine sweeps
            // through that single bone's orientation.
            out.spine_p1_world = LiftBoneLocalToWorld(boneA, localP1);
            out.spine_p2_world = LiftBoneLocalToWorld(boneB, localP2);
            out.kind = KHitKind::Box;

            // Initialise prev fields to cur so any consumer that
            // ignores has_prev_spine still gets sane data (a quad
            // collapsed to a line).
            out.prev_p1_world  = out.spine_p1_world;
            out.prev_p2_world  = out.spine_p2_world;
            out.has_prev_spine = false;

            // -- Spine cache (per-node prev-frame retention) -------------
            // See AreaSpineCacheEntry doc above for the three-state
            // shift algorithm (same tick / +1 tick / multi-tick gap).
            // Critical: the cockpit hook can fire many render frames
            // within a single game tick (especially under pause /
            // frame-step / >60 Hz monitors), so the shift must only
            // happen when the GAME TICK actually advances, not on
            // every cockpit fire.  Serving the same cached prev for
            // every render frame within one tick is what makes the
            // swept quad persist while the user inspects a paused
            // frame.
            const uint32_t curTick = readGameFrameCounter();
            const int cacheIdx = findAreaSpineCacheSlot(
                pose, const_cast<void*>(static_cast<const void*>(node)));
            if (cacheIdx >= 0)
            {
                AreaSpineCacheEntry& entry = s_area_spine_cache[pose][cacheIdx];
                if (entry.node != node)
                {
                    // Fresh slot — node not seen here before, or evicted.
                    entry.node      = const_cast<void*>(static_cast<const void*>(node));
                    entry.cur_p1    = out.spine_p1_world;
                    entry.cur_p2    = out.spine_p2_world;
                    entry.prev_p1   = out.spine_p1_world;
                    entry.prev_p2   = out.spine_p2_world;
                    entry.has_prev  = false;
                    entry.lastTick  = curTick;
                }
                else if (entry.lastTick == curTick)
                {
                    // Same game tick — multiple cockpit fires within one
                    // tick.  Do NOT shift prev.  The bone is frozen this
                    // tick so refreshing cur with the same value is a
                    // no-op; we still update it for paranoia in case the
                    // bone matrix path returns a slightly-different
                    // value (e.g. floating-point noise).
                    entry.cur_p1    = out.spine_p1_world;
                    entry.cur_p2    = out.spine_p2_world;
                }
                else if (entry.lastTick + 1 == curTick)
                {
                    // Game advanced exactly one tick — shift cur to
                    // prev, take the fresh values as the new cur.
                    // has_prev becomes true.
                    entry.prev_p1   = entry.cur_p1;
                    entry.prev_p2   = entry.cur_p2;
                    entry.cur_p1    = out.spine_p1_world;
                    entry.cur_p2    = out.spine_p2_world;
                    entry.has_prev  = true;
                    entry.lastTick  = curTick;
                }
                else
                {
                    // Multi-tick gap (round restart, off-screen, walker
                    // disabled).  Reset — the cached values describe a
                    // position that isn't the engine's actual previous
                    // tick, so showing them would be misleading.
                    entry.cur_p1    = out.spine_p1_world;
                    entry.cur_p2    = out.spine_p2_world;
                    entry.prev_p1   = out.spine_p1_world;
                    entry.prev_p2   = out.spine_p2_world;
                    entry.has_prev  = false;
                    entry.lastTick  = curTick;
                }

                // Hand out the cached prev to the renderer regardless
                // of which branch above we took.  When has_prev is
                // false, prev fields equal cur and the renderer
                // collapses the quad to a single spine line.
                out.prev_p1_world  = entry.prev_p1;
                out.prev_p2_world  = entry.prev_p2;
                out.has_prev_spine = entry.has_prev;
            }
            return true;
        }

        // KHitFixArea: authored as THREE reference points in bone-local
        // space, transformed each tick to world space by
        // KHitFixArea_UpdateWorldCenter @ 0x14030E690.  The engine does:
        //
        //     WP1 = bone * BLP1 + bone.translation   (+0x60)
        //     WP2 = bone * BLP2 + bone.translation   (+0x70)
        //     WP3 = bone * BLP3 + bone.translation   (+0x80)
        //
        // We just pick up the world-space triplet, convert Namco → UE,
        // and hand it to the draw helper as a FixAreaTri kind.  The
        // draw helper emits a spine (P1→P2) + side-reference (P1→P3)
        // pair, which is the authored data directly.  For a true OBB
        // hull draw, see the commented formula in the KHitOffsets
        // FixArea block — the spine/side visualisation is simpler and
        // more faithful to the author's intent.
        //
        // Earlier revisions of this function incorrectly read +0x40
        // as "extents" and +0x50 as "centre", producing a wildly-
        // mispositioned AABB.  See the 2026-04 investigation notes.
        static bool buildFixAreaWorld(const uint8_t* node, KHitDraw& out)
        {
            FVec3 namcoP1, namcoP2, namcoP3;
            if (!readVec3(node + KHitOffsets::FixAreaWorldP1, namcoP1))
                return false;
            if (!readVec3(node + KHitOffsets::FixAreaWorldP2, namcoP2))
                return false;
            if (!readVec3(node + KHitOffsets::FixAreaWorldP3, namcoP3))
                return false;

            // Axis swap: Namco (X=right, Y=up, Z=fwd) → UE (X=fwd, Y=right, Z=up).
            auto toUE = [](const FVec3& n) {
                return FVec3{ n.Z * kLuxCmToUE,
                              n.X * kLuxCmToUE,
                              n.Y * kLuxCmToUE };
            };
            // Stash into corners[0..2] so we don't bloat the KHitDraw
            // struct for every node.  The draw helper reads three
            // specific slots based on the kind tag.
            out.corners[0] = toUE(namcoP1);
            out.corners[1] = toUE(namcoP2);
            out.corners[2] = toUE(namcoP3);
            out.kind = KHitKind::FixAreaTri;
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
            // KHitArea — engine-truth spine + swept-quad outline.  The
            // engine treats P1/P2 as 1D spine endpoints; the actual hit
            // shape is built at overlap-test time from THREE points
            // (the cur spine plus the OTHER frame's tip / hilt as the
            // side reference, see KHitDraw::spine_p1_world docs).  We
            // render the SOURCE DATA:
            //
            //   * Cur spine     : spine_p1_world → spine_p2_world
            //                     (always drawn)
            //   * Prev spine    : prev_p1_world  → prev_p2_world
            //                     (drawn iff has_prev_spine, otherwise
            //                      identical to cur and the line
            //                      would overdraw)
            //   * Two connectors: cur_P1→prev_P1 and cur_P2→prev_P2
            //                     (close the swept quad; drawn iff
            //                      has_prev_spine)
            //
            // Stationary attacks: prev ≈ cur, quad collapses to one
            // line — engine-truth (zero-motion ⇒ zero cross-section).
            // Moving attacks: quad opens up, visually communicating
            // the swept envelope that the engine hit-tested this
            // tick.
            // All 4 quad edges use the caller's color uniformly.  3 of
            // the 4 lines correspond to vectors the engine literally
            // consumes (cur spine = OBB-1 primary axis, prev spine =
            // OBB-2 primary axis, P1 connector = OBB-2 side reference);
            // the 4th (P2 connector) is a "courtesy" closure that
            // completes the quad outline without itself being an engine-
            // computed vector.  All 4 share the same endpoints the
            // engine reads, so together they outline the swept region
            // the engine's two OBBs effectively cover.
            overlay.drawLine(d.spine_p1_world, d.spine_p2_world,
                             color, thickness);
            if (d.has_prev_spine)
            {
                overlay.drawLine(d.prev_p1_world, d.prev_p2_world,
                                 color, thickness);
                overlay.drawLine(d.spine_p1_world, d.prev_p1_world,
                                 color, thickness);
                overlay.drawLine(d.spine_p2_world, d.prev_p2_world,
                                 color, thickness);
            }
            return;
        }

        if (d.kind == KHitKind::FixAreaTri)
        {
            // KHitFixArea — draw the two authored axes:
            //   corners[0] = WP1 (spine origin / near corner)
            //   corners[1] = WP2 (far end along primary axis)
            //   corners[2] = WP3 (side reference)
            //
            // This reflects the data the game itself stores.  The
            // OBB the engine uses for overlap is derived at test
            // time via Gram-Schmidt of (WP2-WP1) and (WP3-WP1);
            // drawing the two lines is faithful to the authored
            // intent without having to reconstruct the hull.
            const auto& v = d.corners;
            overlay.drawLine(v[0], v[1], color, thickness);  // spine
            overlay.drawLine(v[0], v[2], color, thickness);  // side
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
