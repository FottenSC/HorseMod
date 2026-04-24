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
#include "SpeedControl.hpp"   // for current_value_static() — gates the
                              // hit-flash sticky countdown on whether
                              // the world is currently advancing

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

        // MoveStartCounter bumped once each time TransitionToMove fires
        // for any lane — useful for detecting move changes without
        // diffing PackedMoveAddr.  Written at 0x1402fe... in transition.
        constexpr uintptr_t MoveStartCounter       = 0x1350;   // u32
        // Live anim-frame float mirror written by ProcessHit from the
        // active lane's cursor+0x08 (so HUD code can read it without
        // chasing the cursor pointer).  Kept per-chara.
        constexpr uintptr_t LastHitAnimFrame       = 0x1360;   // float
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
        // capable of producing damage — exactly the predicate the
        // classifier (ResolveAttackVsHurtboxMask22 @ 0x14033C100)
        // uses to decide whether overlap should fire a hit:
        //
        //   is_per_frame_active = (node[+0x14] != 0)             // geom-hot
        //                      && ((node.CategoryMask & per_move_cell) != 0)
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
        //     whose categories overlap this node's authored CategoryMask.
        //   * Non-floor attacks (slot >= 18).  +0x14 already requires
        //     the slot bit in the cell, so the category intersection
        //     is the additional "and our move's flavor matches" check.
        //
        // Earlier versions of this filter mistakenly used:
        //   v1: only *sub_frame_cell — empty for simple moves, hid
        //       every attack.
        //   v2: +0x14 minus 0x3FFFD floor — hid the floor-slot
        //       attacks (body-attached hitboxes).
        //
        // Always false for hurtbox / body entries.
        bool        is_per_frame_active;

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
            // Problem we're investigating: red "just-got-hit" flash never
            // visibly fires in game.  Two concurrent interventions:
            //
            // (a) EDGE SCAN — read a wider window (0x1C00..0x1E00, 128 × i32)
            //     and log ANY transition from 0 -> non-zero.  This both
            //     validates the nominal 0x1C74 offset and surfaces the
            //     actual storage location if our assumed offset is wrong.
            //     Output is event-driven, not throttled by shouldLog() —
            //     if the signal exists at all we'll see it.
            //
            // (b) STICKY FLASH — the raw reaction flag is almost certainly
            //     a 1-frame pulse (~16ms at 60fps, hard to see).  Hold the
            //     "hot" state for N frames after raw goes non-zero so the
            //     colour change is actually visible.  walkList() now
            //     consumes sticky_reactions[] in place of reactions[].
            //
            // Per-player state is keyed by poseSelector (0 = P1, 1 = P2).
            const int pi_idx = (poseSelector < 2u)
                             ? static_cast<int>(poseSelector) : 0;

            // --- (a) Edge-scan diagnostic -------------------------------
            constexpr uintptr_t kReactionScanBase  = 0x1C00;
            constexpr int       kReactionScanCount = 128;   // 512 bytes
            int32_t* scan_prev = s_react_scan_prev[pi_idx];

            for (int i = 0; i < kReactionScanCount; ++i)
            {
                int32_t v = 0;
                SafeReadInt32(bytes + kReactionScanBase + i * 4, &v);
                if (v != 0 && scan_prev[i] == 0)
                {
                    const uintptr_t off = kReactionScanBase + i * 4;
                    const int rel_to_nominal =
                        i - static_cast<int>(
                            (ChaOffsets::PerHurtboxReactionState
                             - kReactionScanBase) / 4);
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[HorseMod.React] pi={} EDGE off=0x{:x} idx={} "
                            "val=0x{:08x} (rel-to-0x1C74={})\n"),
                        pi_idx, off, i,
                        static_cast<uint32_t>(v), rel_to_nominal);
                }
                scan_prev[i] = v;
            }

            // --- (b) Sticky flash (used for rendering) -------------------
            // Hold "hot" for `s_sticky_frames` GAME TICKS after raw goes
            // non-zero.  Driven by the dllmain UI slider; see
            // setStickyFrames().  0 frames = no sticky (raw 1-frame
            // pulse only).
            //
            // Time-source rationale: this function is called from the
            // cockpit (Slate UI) hook, which fires ONCE PER RENDER
            // FRAME — not once per game tick.  The game tick rate is
            // fixed at 60 Hz, but render frame rate is whatever the
            // player's display runs at (60 / 120 / 144 / …).  Before
            // 2026-04 we decremented the sticky counter once per call,
            // which meant:
            //   - At 60 Hz render: 15 ticks of sticky ≈ 250 ms ✓
            //   - At 120 Hz render: drains twice as fast (~125 ms) ✗
            //   - At 144 Hz render: drains 2.4× too fast (~104 ms)  ✗
            // We also used `SpeedControl::current_value_static() > 0`
            // as a "world advancing?" gate so freeze-frame wouldn't
            // drain the flash.  But SpeedControl::disable() didn't
            // reset the shared speedval back to 1.0f, so once the
            // user had ever enabled freeze, the gate would stick at
            // "frozen" and the flash would NEVER drain, even during
            // normal gameplay.  That was the 2026-04 "hit-flash never
            // clears" bug.
            //
            // Fix: drain by the chara's own GAME-TICK advance, read
            // from LuxMoveLaneState::TotalTickCounter (lane cursor
            // +0x458).  The MoveVM bumps that counter once per tick
            // the lane advances, and naturally stays constant under
            // freeze-frame (no tick = no advance = no drain).
            // Slow-motion still drains at wall-clock rate because
            // game ticks still fire, just less often — so at 0.25×
            // speed, 15 ticks of sticky ≈ 1 second of wall clock,
            // which matches the slower animation and lets the user
            // actually see the flash during slow-mo replays.
            //
            // The `speed_check_disabled` note that used to live here
            // about SpeedControl gating is obsolete.
            const int kStickyFrames =
                s_sticky_frames.load(std::memory_order_relaxed);

            // Read this chara's current game-tick counter.  Falls
            // back to "always advance" if the lane cursor is missing
            // (e.g. training-mode paused outside an active move).
            int32_t cur_tick = 0;
            bool    have_tick = false;
            {
                void* cursor = nullptr;
                if (SafeReadPtr(bytes + ChaOffsets::ActiveLaneStateCursorPtr,
                                &cursor))
                {
                    const auto cA = reinterpret_cast<uintptr_t>(cursor);
                    if (cA >= 0x10000ULL && cA <= 0x00007fffffffffffULL)
                    {
                        if (SafeReadInt32(
                                reinterpret_cast<uint8_t*>(cursor)
                                + LuxMoveLaneOffsets::TotalTickCounter,
                                &cur_tick))
                        {
                            have_tick = true;
                        }
                    }
                }
            }

            // Compute how many game ticks have elapsed since the
            // previous call.  Clamp: negative = counter reset (new
            // match), treat as 0; huge positive = treat as 1 to
            // avoid blowing past the sticky window on a single call.
            int tick_delta = 0;
            if (have_tick)
            {
                const int32_t prev = s_prev_lane_tick[pi_idx];
                if (prev >= 0)
                {
                    const int raw = cur_tick - prev;
                    tick_delta = (raw < 0)      ? 0
                               : (raw > 8)      ? 1   // counter jumped weirdly
                               :                 raw;
                }
                s_prev_lane_tick[pi_idx] = cur_tick;
            }
            // If we can't read the tick counter (no active move), fall
            // back to a per-cockpit-tick drain but gate on SpeedControl
            // just like the old path — this is the training-mode-idle
            // case where no move is active, so nothing can be hitting
            // anyway; sticky will still drain at render rate.
            else
            {
                tick_delta = (SpeedControl::current_value_static() > 0.0f)
                             ? 1 : 0;
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

            // --- Attack list -------------------------------------------------
            walkList(ue_chara, poseSelector, atk_head,
                     KHitList::Attack, active_cell, own_attack_mask,
                     reactions_hot, hurt_slot_count, verbose, visit);
            // --- Hurtbox list ------------------------------------------------
            walkList(ue_chara, poseSelector, hurt_head,
                     KHitList::Hurtbox, active_cell, own_attack_mask,
                     reactions_hot, hurt_slot_count, verbose, visit);
            // --- Body / pushbox list -----------------------------------------
            walkList(ue_chara, poseSelector, body_head,
                     KHitList::Body, active_cell, own_attack_mask,
                     reactions_hot, hurt_slot_count, verbose, visit);
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

        // Per-player previous-frame snapshot of the 0x1C00..0x1E00 scan
        // region — used by the edge-triggered diagnostic that verifies
        // whether PerHurtboxReactionState really lives at 0x1C74.
        static inline int32_t s_react_scan_prev[2][128] = {};

        // Per-player previous-tick snapshot of chara+0x44068→+0x458
        // (LuxMoveLaneState::TotalTickCounter) — a monotonic i32 that
        // the MoveVM bumps every game-tick the lane advances.  Used
        // to drive the sticky-flash drain on GAME ticks instead of
        // cockpit ticks, so the visible flash duration is consistent
        // regardless of the user's render refresh rate (60/120/144
        // Hz all produce the same "N game-frames visible" result) and
        // naturally pauses under freeze-frame without any SpeedControl
        // coupling.  -1 sentinel = no prior sample yet (first frame).
        static inline int32_t s_prev_lane_tick[2] = { -1, -1 };

        // Runtime-configurable sticky window length, in GAME ticks
        // (60fps assumed).  Default 15 ≈ 250ms; dllmain overrides it
        // on unreal-init + every slider drag.
        static inline std::atomic<int> s_sticky_frames{15};

        template <class Visit>
        static void walkList(void* chara,
                             uint32_t poseSelector,
                             void* head,
                             KHitList listKind,
                             void* /*activeAttackCell — cross-chara, unused*/,
                             uint64_t ownAttackMask,
                             const int32_t (&reactions)[22],
                             int32_t hurtSlotCount,
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
                // Raw +0x14 for diagnostic purposes.  Note this is a
                // valid live/cold signal only for attack nodes — the
                // per-frame update loop in TickHitResolution does not
                // touch hurtbox / body lists, so their +0x14 stays at
                // the deserialize-time default (always 1).  See
                // KHitDraw::geom_active doc for the full story.
                d.geom_active           = (activeGate != 0);

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
                }
                // Damage gate — slot bit set in THIS chara's own
                // active-attack-cell mask.  Narrower than +0x14: only
                // true when the current move's authored timeline has
                // lit this specific slot.  The UI can route "Live
                // attacks only" through this for a much tighter
                // filter; see dllmain m_hide_not_damage_active.
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
                d.is_per_frame_active = (listKind == KHitList::Attack &&
                                         activeGate != 0 &&
                                         (cat_mask & ownAttackMask) != 0);
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
