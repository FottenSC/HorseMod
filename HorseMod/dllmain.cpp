// ============================================================================
// HorseMod — SoulCalibur VI hit-volume visualiser (UE4SS C++ mod).
//
// What this shows
// ---------------
// Every frame, read the SC6 legacy Namco KHit linked lists on both charas
// and draw their contents as wire-frame volumes in world space using
// UWorld's ULineBatchComponent.
//
//   HURTBOXES  (KHit list at chara+0x444B8) — entries that RECEIVE
//              damage from opponent attacks.  On by default.  Flashes red
//              when that slot's PerHurtboxReactionState[i] is non-zero
//              (just got hit).  The hurtbox list holds every "receive"
//              volume — damage hurtboxes, throw-reach receivers,
//              proximity/auto-turn cages.  The engine's own classifier
//              doesn't sub-bucket these from the defender side; reactions
//              are decided per-slot based on which attacker category bits
//              are set in the incoming mask.  We no longer invent
//              size-based sub-buckets for this reason.
//
//   ATTACK     (KHit list at chara+0x44498) — entries that DEAL damage
//   BOXES      (or initiate a grab).  On by default.  Dim amber for
//              strikes; a node whose per-frame active gate (node+0x14) is
//              non-zero is drawn bright yellow (the "hot" attack for the
//              current frame — the tick sets this bit to
//              `(hotMask >> node[+0x17]) & 1`, meaning the MoveVM's hot
//              bitmap has this bone slot marked active).  Grab/throw
//              attacks are drawn magenta — the engine distinguishes them
//              from strikes via bits 31 and 55 of the CategoryMask at
//              node+0x08 (see LuxBattle_ResolveAttackVsHurtboxMask22
//              @ 0x14033C100).
//
//   BODY /     (KHit list at chara+0x44478) — entries that neither deal
//   PUSHBOX    nor receive damage.  Used by
//              LuxBattle_SolvePhysBodyCollision @ 0x14030CCF0 for
//              character-to-character physical pushing only.  OFF by
//              default (visually noisy — spacing context only).
//              Enable per-player from the ImGui tab.
//
// Historical note: earlier versions of this mod had the three list heads
// rotated (Attack↔Body↔Hurtbox off by one in the old plate on
// LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0).  That's why
// "attack" boxes used to appear to not make contact with the opponent —
// they were actually the pushbox list.  Corrected above.
//
// Visibility is per-player: P1 and P2 each get independent hurtbox /
// attack / body toggles, so e.g. you can hide P2's pushboxes while
// keeping P1's visible.
//
// All geometry comes from Pipeline 2 (legacy KHit chain) — the same data
// the hit resolver uses.  Weapon-trail Pipeline 1 (ActiveAttackSlot via
// TraceManager) is not drawn in this build; most hit volumes of interest
// (kicks, hurtboxes, bodies) are in Pipeline 2 anyway.
//
// Hotkeys
//   F5  toggle overlay on / off
//   F6  pause + step one frame.  First press latches Freeze-frame ON;
//       subsequent presses queue additional frames (held F6 yields
//       ~30fps slow-motion via UE4SS key auto-repeat).  Implementation
//       writes speedval = 0 / 1.0 alternation through Horse::SpeedControl
//       (the trampolined GetTimeDilationScalar override) — see the
//       frame_step_apply helper below.
//   F7  toggle free-fly camera.  Ansel-free: writes our own pose to
//       ALuxBattleCamera+0x410..+0x428 each cockpit tick while CamLock
//       holds off the engine's director.  Keyboard controls: WASD
//       translate, Q/E up/down, arrows or IJKL look (arrows may be
//       swallowed by SC6's RawInput handler — IJKL is the reliable
//       fallback), Shift/Ctrl for speed.  XInput pad 0 is also polled
//       (sticks translate/look, LT/RT vertical, LB/RB speed).  See
//       horselib/FreeCamera.hpp.
//
// ImGui tab ("HorseMod")
//   • overlay enable toggle (mirrors F5)
//   • per-list visibility toggles (hurtbox / attack / body)
//   • line thickness slider
//   • LineBatcher slot selector (Default / Persistent / Foreground)
//
// Everything else the earlier prototype had — predicate hooks, bounds
// traces, yarare watchers, process-event spies, cockpit-widget backend,
// capsule walker — has been removed.  If you want any of that back, pull
// it out of git history; it's all on dead paths for the "draw the real
// hitboxes" goal we're pursuing now.
//
// Ghidra references
//   LuxBattle_TickHitResolutionAndBodyCollision  @ 0x14033CCA0  (full plate)
//   ALuxBattleChara_GetBoneTransformForPose      @ 0x140462760
//   LuxSkeletalBoneIndex_Remap                   @ 0x140898140
//   KHitBase / KHitArea / KHitSphere / KHitFixArea — 0xA0 bytes each
// ============================================================================

#include "horselib/HorseLib.hpp"
#include "horselib/KHitWalker.hpp"
#include "horselib/LineBatcherBackend.hpp"
#include "horselib/NativeBinding.hpp"
#include "horselib/CamLock.hpp"
#include "horselib/FreeCamera.hpp"
#include "horselib/VFXOff.hpp"
#include "horselib/CharaInvis.hpp"
#include "horselib/SpeedControl.hpp"
// horselib/GamePause.hpp DEPRECATED — see "Freeze frame" UI block below
// for the SpeedControl-driven replacement.  The old chara+0x394
// trampoline targeted an audio-state bit, not the world-tick pause.

#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Input/KeyDef.hpp>
#include <Input/Handler.hpp>

#include <imgui.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

using namespace RC;
using namespace RC::Unreal;

// ----------------------------------------------------------------------------
class HorseMod final : public CppUserModBase
{
private:
    // Static live-instance pointer so the cockpit hook lambda can safely
    // no-op after destruction (game thread could fire after Restart All).
    static inline std::atomic<HorseMod*> s_instance{nullptr};

    // ---- Overlay state ----
    std::atomic<bool> m_enabled{false};

    // Per-player visibility toggles.  Hurtbox + Attack default ON, Body
    // defaults OFF (it's visually noisy — spacing context only).  Each
    // player indexed by PlayerIndex (0 = P1, 1 = P2).
    std::atomic<bool> m_show_p1_hurt{true};
    std::atomic<bool> m_show_p1_atk {true};
    std::atomic<bool> m_show_p1_body{false};
    std::atomic<bool> m_show_p2_hurt{true};
    std::atomic<bool> m_show_p2_atk {true};
    std::atomic<bool> m_show_p2_body{false};

    // Attack-role filters — shared across P1 and P2 (same engine-defined
    // partition on both sides).  Derived from the 64-bit CategoryMask at
    // node+0x08, the same split the classifier at 0x14033C100 uses:
    //
    //   Strike  — mask bits in 0xFF7FFFFF7FFFFFFF  (all bits except 31, 55).
    //   Throw   — mask bits in 0x0080000080000000 (bits 31, 55) = grabs.
    //
    // Both default ON — throws and strikes are equally useful to see.
    std::atomic<bool> m_show_atk_strike{true};
    std::atomic<bool> m_show_atk_throw {true};

    // ("Live attacks only" filter — tested node+0x14 — was removed
    //  2026-04.  Reason: the engine's hotMask always OR-s in a
    //  0x3FFFD floor (slots {0, 2..17}) so the gate passed ~20 body-
    //  anchored boxes unconditionally during neutral.  Users who want
    //  "only what's dealing damage" want m_hide_not_damage_active
    //  instead, which tests the actor-level classifier mask and is
    //  cold during neutral.)

    // "Damage-active only" filter — damage gate.  When true, skip
    // any attack node whose +0x17 slot bit is NOT set in this
    // chara's own active-attack-cell mask at *(u64*)(chara+0x44058).
    // That mask is written per-frame by the MoveVM from the active
    // move's authored timeline and is the same mask the classifier
    // at LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100 ANDs
    // against defender PerHurtboxBitmask entries to decide
    // reactions.
    //
    // Unlike the geometry gate above, this is NOT affected by the
    // 0x3FFFD always-on floor — during neutral the active-cell is
    // typically null (mask=0) and every attack box gets filtered
    // out, which is the behaviour you want for "show me only what's
    // ACTIVELY trying to hit right now".  Default OFF so the
    // overlay still lights up visibly on first launch; users flip
    // it on to answer "which of these is dealing damage this
    // frame?".
    std::atomic<bool> m_hide_not_damage_active{false};

    // "Per-frame damage-active only" filter — strictly tighter than
    // m_hide_not_damage_active.  When ON, hide every attack node whose
    // +0x17 slot bit is NOT set in the chara's PER-FRAME damage mask
    // (computed by mirroring the engine's pMoveVMCell lookup at
    // 0x14033cca0; see KHitWalker::readPerFrameDamageMask).
    //
    // Why this matters: m_hide_not_damage_active reads
    // **(chara+0x44058), which is set ONCE PER MOVE-SLOT (in
    // LuxMoveVM_SetActiveMoveSlot @ 0x140300c70) and stays constant
    // through startup / active / recovery.  As a result hitboxes
    // appear "damage-active" through the entire move duration even
    // though damage is only dealt during the active-frame window.
    //
    // The per-frame mask follows the move's authored sub-frame
    // timeline: it turns OFF during startup, ON during damage frames,
    // OFF during recovery — exactly what hitbox-data analysis wants.
    // Default OFF (additive over the existing toggles).
    std::atomic<bool> m_hide_not_per_frame_active{false};

    // "Addressable hurtboxes only" filter — hurtbox classifier gate.
    //
    // Correction to an earlier attempt at a +0x14-based "live hurtboxes"
    // filter: the per-frame update loop in
    // LuxBattle_TickHitResolutionAndBodyCollision (0x14033CCA0) only
    // walks chara+0x44498 = AttackListHead.  It does NOT touch the
    // hurtbox or body lists.  Hurtbox +0x14 is therefore pinned at its
    // deserialize-time default of 1 forever, and a +0x14-based filter
    // is a no-op for hurtboxes.
    //
    // The real mechanism that makes a hurtbox un-hittable from the
    // classifier's perspective is the ClassifierHurtboxBound field:
    //
    //     LuxBattle_ResolveAttackVsHurtboxMask22 iterates
    //         for (slotIndex = 0; slotIndex < chara+0x44494; ++slotIndex)
    //             test PerHurtboxBitmask[slotIndex] & attackerMask & ...
    //
    // UpdateAllKHitWorldCenters still OR's bits into
    // PerHurtboxBitmask[hurt->+0x17] regardless of slot, but if
    // hurt->+0x17 >= bound those bits land in an index the classifier
    // never reads — no reaction, no damage, visually alive but
    // engine-dead.
    //
    // GOTCHA: chara+0x44494 is NOT the hurtbox list's own slot count.
    // It's the ATTACK list's max-slot (ATTACK stream's pOutMaxSlot
    // written by Lux_KHitChk_DeserializeLinkedList), which the engine
    // reuses as the hurtbox-iteration bound.  On moves with few
    // attack slots but many hurtbox slots (dodges, pure movement,
    // block, throw-whiff) this bound will be smaller than the real
    // hurtbox max — and per-move hurtboxes authored at the tail will
    // be flagged unaddressable.  That's engine-truth: those hurtboxes
    // really won't produce a reaction.  But it means this toggle can
    // hide legitimate per-move hurtboxes that the mod user might
    // expect to see.  Users investigating "my move added a hurtbox
    // but I don't see it" should turn this OFF first.
    //
    // When this toggle is ON, we hide every hurtbox whose
    // bone_id_internal (+0x17) is outside the classifier range.  These
    // are typically body-wide "meta" volumes authored at high slots
    // (counter-hit detection, throw-whiff zones, state-specific
    // detection geometry) — exactly the kind of giant sphere a user
    // notices "doesn't get hit by anything".  Default OFF: the
    // unaddressable hurtboxes are still interesting data for modders,
    // and hiding them by default could mask legitimate per-move boxes
    // during dodge/movement moves (see gotcha above).
    std::atomic<bool> m_hide_unaddressable_hurt{false};

    // ---- Weapon visibility override ----------------------------------------
    // When ON, force every ALuxBattleChara's weapon meshes hidden each
    // frame by calling SetWeaponVisibility(false) via UFunction reflection.
    // This is useful when inspecting hitboxes on characters with bulky
    // weapons (Nightmare's sword, Astaroth's axe) that otherwise occlude
    // the volumes we're drawing.
    //
    // Semantics:
    //   OFF: do nothing.  The game manages weapon visibility normally
    //        (cinematic cues, ring-out states, etc.).
    //   ON : re-apply hidden every frame — overrides any game-driven
    //        visibility change, so weapons stay gone while the toggle is
    //        held.
    //   ON -> OFF transition: call SetWeaponVisibility(true) once per
    //        chara so weapons return to visible; after that we stop
    //        touching the state and let the game run.
    //
    // We apply via the BlueprintCallable UFunction declared on
    // ALuxBattleChara.h:80:
    //     UFUNCTION(BlueprintCallable)
    //     void SetWeaponVisibility(bool bVisible);
    // which is a registered UFunction and therefore reachable through
    // ProcessEvent — no native-RVA binding needed.
    std::atomic<bool> m_hide_weapons{false};
    // Tracks the last state actually pushed to the game; lets us detect
    // the ON->OFF edge so we restore visibility exactly once.
    std::atomic<bool> m_last_applied_hide_weapons{false};

    // ---- Ansel "always allow photography" override -------------------------
    // SC6 gates NVIDIA Ansel (the in-engine freeze-frame / free-camera
    // photo mode) via three layers:
    //
    //   1. UAnselFunctionLibrary::SetIsPhotographyAllowed(bool)
    //        The bottom-layer switch the UE4 AnselIntegration plugin
    //        checks before it will even accept a capture hotkey.
    //        Declared BlueprintCallable in
    //        include/SoulCaliburVI/Ansel/Public/AnselFunctionLibrary.h:35.
    //
    //   2. ULuxGameInstance::SetLuxorAnselEnabled / SetAnselEnabled /
    //      SetAnselIsInPauseMenu
    //        SC6-level permission flags read by the battle manager and
    //        HUD.  When these are off the game masks Ansel even if the
    //        UE4 layer allows it.
    //
    //   3. ALuxBattleManager::RequestStartAnselSession / RequestEndAnselSession
    //        Per-match actual session lifecycle — gated on (1) and (2).
    //
    // Empirically the community-reliable way to un-gate Ansel in SC6
    // is to force layer (1) on every frame.  The SC6-layer (2) stubs
    // are either no-ops or always-false in the binary we have; the
    // UE4 layer is the real cliff.  When this toggle is ON we
    // re-apply SetIsPhotographyAllowed(true) every frame so any game
    // code that tries to disable it (menu transitions, ring-out,
    // cinematic cams) gets overridden back on before its effects are
    // visible.
    //
    // ON -> OFF transition: we call SetIsPhotographyAllowed(false)
    // exactly once so the game resumes managing the flag itself, then
    // stop touching it.  This matches the weapon-visibility semantics
    // above.
    //
    // This runs independent of the F5 overlay toggle — the user said
    // "always allows", so it fires from the hook pre-callback before
    // the enabled / NativeBinding-ready gates.
    std::atomic<bool> m_ansel_always_allowed{false};
    // Last state we actually pushed; used to detect the ON -> OFF
    // transition so we restore engine control exactly once.
    std::atomic<bool> m_last_applied_ansel_allowed{false};
    // Cached CDO of /Script/Ansel.Default__AnselFunctionLibrary.
    // The CDO is a persistent UObject that stays valid for the life
    // of the process, so we keep a raw pointer and revalidate with
    // UObject::IsReal before each use.  Same pattern as
    // Horse::Screen's GameplayStatics CDO cache.
    RC::Unreal::UObject* m_ansel_cdo = nullptr;
    // Cache slot for the UFunction* itself; shared across all frames.
    Horse::Fn m_fn_set_photo_allowed;

    // ---- Camera lock --------------------------------------------------------
    // Freeze the battle camera at its current pose while the toggle is
    // held.  Implemented by patching SC6's per-frame "commit POV to
    // memory" instructions to NOPs — see horselib/CamLock.hpp for the
    // full disassembly walk and the historical note on why the previous
    // CameraCache.POV-write approach didn't work (UMG widget tick runs
    // AFTER the renderer has already consumed the POV).
    //
    // Semantics:
    //   OFF: every store runs as normal — engine owns the camera.
    //   ON : 5 stores at site A and 5 stores at site B are NOPed.  The
    //        camera struct in memory keeps whatever location/rotation/FOV
    //        it had at the moment we toggled ON; nothing in the engine
    //        rewrites those fields for the duration.
    //
    // CamLock owns the live BytePatch state — it must outlive every
    // call site (kept until ~HorseMod) so the patches get cleanly
    // restored on mod unload.  Atomic for ImGui-thread reads against
    // the patch state (the patch-flip itself happens on the same
    // thread, so no race).
    Horse::CamLock    m_cam_lock{};
    std::atomic<bool> m_lock_camera{false};

    // ---- Free-fly camera ----------------------------------------------------
    // Writes the SC6 battle camera's pose (Location, Rotation, FOV) from
    // our own per-cockpit-tick state, driven by WASD + arrow keys.  Uses
    // CamLock internally to freeze engine writes so the input doesn't
    // fight the director-cam.  Independent of Nvidia Ansel — no Ansel
    // session is involved, so our hitbox overlay continues to render.
    //
    // Note: enabling free-camera implicitly enables CamLock; disabling
    // free-camera releases CamLock too.  If the user has ALSO manually
    // enabled "Lock camera", CamLock stays on after free-camera turns
    // off (set() only nudges it if it was internally activated).
    Horse::FreeCamera m_free_camera{};
    std::atomic<bool> m_free_camera_enabled{false};
    // Cached pointer to the local APlayerCameraManager, revalidated
    // each tick via PlayerController.PlayerCameraManager (UObject
    // property chain).  Null until battle has a live PC + PCM pair.
    //
    // HISTORY OF WHY THIS ISN'T "BattleCamera":
    //
    //   Iteration 1 (Gemini):
    //     Wrote to ALuxBattleCamera+0x410..+0x428 (from
    //     LuxBattleManager.BattleCamera) AND called
    //     K2_SetActorLocationAndRotation via ProcessEvent each tick.
    //     Neither moved the camera — the first is the wrong object,
    //     and the ProcessEvent call had a malformed params block
    //     (over-sized FHitResult shifted bTeleport off-offset).
    //
    //   Iteration 2:
    //     Removed the K2 call; kept writing to
    //     LuxBattleManager.BattleCamera+0x410.  Memory-persistence
    //     diagnostics confirmed our writes WERE landing on that
    //     object and nothing was stomping them, but the visual camera
    //     still didn't move.  That proved the write target was wrong.
    //
    //   Iteration 3 (current):
    //     Ghidra trace of UWorld::Tick @ 0x141f02230 shows the engine
    //     per-tick commit path is invoked as
    //       APlayerCameraManager_CommitPOV_NoInterp(plVar15[0x84])
    //     where plVar15 is an APlayerController and [0x84] (=+0x420)
    //     is the PlayerCameraManager field.  The 5 CamLock NOP
    //     targets all write to `this+0x410..+0x428` on that PCM.
    //     APlayerController::GetPlayerViewPoint @ 0x142046410 —
    //     the consumer invoked by ULocalPlayer::CalcSceneView — reads
    //     back from the SAME +0x410..+0x424 block on PCM.
    //
    //     So the renderer-authoritative POV data lives on the
    //     APlayerCameraManager, NOT the ALuxBattleCamera.  The
    //     actor's +0x410..+0x428 is a director-scratch block that
    //     nothing downstream reads — writing there looks like it
    //     works (memory persists) but has zero render-side effect.
    void*             m_cached_player_camera_manager = nullptr;
    // UE4SS reflection-side locators: a cached FindFirstOf handle for
    // APlayerController, revalidated by GlobalPtr::get on level
    // changes.  PCM is read as the "PlayerCameraManager" property
    // on the PC every tick (cheap — hashed FName lookup).
    Horse::GlobalPtr  m_player_controller{};

    // ---- Hide characters ----------------------------------------------------
    // Bytepatch port of somberness's CE "Invisible" cheat — see
    // horselib/CharaInvis.hpp for the full disassembly walk.
    //
    // Replaces the earlier per-frame SetCharacterVisibility(false) UFunction
    // re-apply loop.  That approach worked for normal moves but flickered
    // visibility ON for one frame on certain moves (Critical Edges, super
    // intros, transformations) because:
    //   * The cockpit hook fires during Slate tick (BEFORE world tick).
    //   * Engine's chara-tick during world tick would re-set the visibility
    //     flag back to "visible" as part of the move's state machine.
    //   * Render then drew the chara visible for that one frame.
    //   * Our next cockpit-tick re-hid it, producing the flicker.
    //
    // The new approach inverts the engine's own visibility-compare
    // instructions inside ALuxBattleChara_SyncMoveStateVisibility so that
    // every read of the visibility flag now produces "hidden" — eliminating
    // the race because we're INSIDE the read path, not racing the writes.
    //
    // Useful when you're diagnosing hitbox shapes on a specific move —
    // the character mesh and its skirt/cape/hair occlude the volumes.
    // Hitboxes are part of the gameplay skeleton, not the mesh, so they
    // keep updating fine while the mesh is invisible.
    Horse::CharaInvis m_chara_invis{};
    std::atomic<bool> m_hide_chara{false};

    // ---- Speed control (slow-motion / freeze) -------------------------------
    // Bytepatch port of somberness's CE "Speed control v2" cheat — see
    // horselib/SpeedControl.hpp for the full disassembly walk and the
    // user contract.
    //
    // 5 trampolines hijack every load of the engine's master delta-time /
    // time-dilation float and redirect it to a single user-controlled
    // `speedval` slot in the CodeCave.  Result: the LuxMoveVM simulation
    // (animations, hit timing, opcode-stream execution, motion-object
    // advancement) all scale uniformly with speedval.
    //
    //   speedval = 0.0   → frozen
    //   speedval = 0.05  → 20× slow-mo (great for active-frame inspection)
    //   speedval = 0.1   → 10× slow-mo
    //   speedval = 0.5   → half speed
    //   speedval = 1.0   → normal
    //
    // Independent of the GamePause toggle and the F6 step hotkey — they
    // gate different mechanisms and stack cleanly.
    Horse::SpeedControl m_speed_control{};
    std::atomic<bool>   m_speed_enabled{false};
    std::atomic<float>  m_speed_value{1.0f};

    // ---- Suppress VFX -------------------------------------------------------
    // Bytepatch port of somberness's CE "VFX off" cheat — see
    // horselib/VFXOff.hpp for the full disassembly walk.  Replaces the
    // earlier per-frame DestroyAllVFx polling: that approach let each
    // VFX spawn for one frame before tearing it down (1-tick flashes
    // on every hit) and burned a UFunction call per tick.  The
    // bytepatch installs a midfunction trampoline that overrides the
    // engine's per-slot VFX-state writer to plant a sentinel constant
    // the renderer treats as culled — effects never become visible.
    //
    // Same toggle, same ImGui label.  No hot-path work; flip is a
    // single 5-byte JMP install.
    Horse::VFXOff     m_vfx_off{};
    std::atomic<bool> m_suppress_vfx{false};

    // ---- Freeze frame (REWORKED — drives Horse::SpeedControl) ---------------
    // Replaces the broken Horse::GamePause helper (which patched a chara
    // audio-flag bit at +0x394, not a world-pause).  The actual world-
    // tick pause in SC6 is the master VM-freeze byte at 0x1448462D0:
    // when non-zero, LuxMoveVM_GetTimeDilationScalar returns 0.0 and
    // every per-frame integrator (animation, opcode-stream, hit timing)
    // sees dt=0 and halts.  See the plate on g_LuxBattle_VMFreezeByte.
    //
    // SpeedControl already overrides GetTimeDilationScalar at the source
    // (site 3 of its 5 patches), so we just write `speedval = 0.0f` to
    // freeze the world by the EXACT same mechanism the engine itself
    // uses for hitstop.  Frame-step is just "set speedval = 1.0f for
    // one cockpit tick, then back to 0.0f".
    //
    // No new patches; no new race conditions.  The existing SpeedControl
    // patches do all the work — this UI block just toggles the value.
    //
    // Interaction with the Slow-motion checkbox:
    //   * Freeze ON     -> speedval = 0.0  (highest priority)
    //   * Slow-mo ON    -> speedval = m_speed_value (slider)
    //   * Both OFF      -> SpeedControl disabled (no overhead)
    std::atomic<bool> m_freeze_frame{false};

    // Frame-step state machine, driven from on_cockpit_update_pre.
    // Same shape as the old GamePause::on_tick() machine but the
    // "clear bit" / "set bit" actions are now "set speedval = 1.0" /
    // "set speedval = base".  Two cockpit ticks per advanced game
    // frame — first tick lifts the freeze, second tick re-applies it.
    std::atomic<int>  m_step_pending{0};
    std::atomic<bool> m_step_expecting{false};

    // Red "just got hit" sticky flash duration, in COCKPIT TICKS.
    //
    // The underlying PerHurtboxReactionState signal is a ~1-frame pulse
    // (~16ms at 60fps) — too short to see.  We extend it by holding the
    // hot state for `m_flash_frames` ticks before fading.
    //
    // The countdown advances inside KHitWalker only when the world is
    // actually ticking (SpeedControl::current_value_static() > 0) — so
    // when "Freeze frame" is on (speedval = 0), the sticky countdown
    // pauses and the red flash stays visible for as long as the user
    // holds the freeze.  Frame-step (F6) advances the world by one
    // engine tick, which the cockpit hook sees as one un-frozen tick,
    // which decrements the sticky by one — so stepping ages the flash
    // exactly as the user expects.
    //
    // Slow-motion (0 < speedval < 1) is treated as "world running" and
    // the sticky decrements at the cockpit-tick rate, so at e.g. 0.1x
    // speed the flash drains 10x faster than the game-frame equivalent
    // would suggest.  Acceptable trade-off for keeping the slider unit
    // intuitive (= ticks of normal-speed gameplay).
    //
    // Default 15 frames ≈ 250ms at 60fps — same visible duration as
    // the previous ms-based slider's 250ms default.
    std::atomic<int> m_flash_frames{15};

    std::atomic<float> m_thickness{2.0f};
    std::atomic<Horse::LineBatcherSlot> m_slot{Horse::LineBatcherSlot::Foreground};

    // Look up the current show-flag for (player, list).  Inlined in the
    // hot path; pi outside [0,1] falls through to P1's settings.
    bool shouldShow(int pi, Horse::KHitList list) const
    {
        const bool is_p2 = (pi == 1);
        switch (list)
        {
            case Horse::KHitList::Hurtbox:
                return (is_p2 ? m_show_p2_hurt : m_show_p1_hurt).load();
            case Horse::KHitList::Attack:
                return (is_p2 ? m_show_p2_atk  : m_show_p1_atk ).load();
            case Horse::KHitList::Body:
                return (is_p2 ? m_show_p2_body : m_show_p1_body).load();
        }
        return false;
    }

    // Secondary gate for Attack-list entries: after the master Attacks
    // toggle passes, consult the engine-derived role filter.  NotAttack
    // always passes (used for Hurtbox / Body — handled at the outer
    // switch, never reaches this gate in practice).
    bool shouldShowAttackRole(Horse::KHitAttackRole r) const
    {
        switch (r)
        {
            case Horse::KHitAttackRole::Strike: return m_show_atk_strike.load();
            case Horse::KHitAttackRole::Throw:  return m_show_atk_throw .load();
            case Horse::KHitAttackRole::NotAttack:
            default:                             return true;
        }
    }

    // ---- Hook / backend ----
    bool                         m_hook_registered = false;
    std::pair<int32_t, int32_t>  m_hook_ids{};
    StringType                   m_hook_path;
    int                          m_poll_counter = 0;
    int                          m_update_calls = 0;
    int                          m_diag_tick    = 0;

    Horse::Lux                 m_lux;
    Horse::LineBatcherBackend  m_backend;

    // One-shot log flags so UE4SS.log doesn't fill with repeats.
    bool m_logged_native_missing = false;
    // Free-camera diagnostic one-shots: first time we successfully resolve
    // the PlayerCameraManager (so the user can confirm we're targeting
    // a real object), and first time we fall back to the direct +0x420
    // offset read (means reflection couldn't find the property name —
    // unlikely but survivable).
    bool m_logged_pcm_resolve  = false;
    bool m_logged_pcm_fallback = false;

public:
    HorseMod() : CppUserModBase()
    {
        ModName        = STR("HorseMod");
        ModVersion     = STR("0.10.0");
        ModDescription = STR("SC6 KHit hitbox / hurtbox / body visualiser.");
        ModAuthors     = STR("horse");

        Input::ModifierKeyArray no_mods{};
        no_mods.fill(Input::ModifierKey::MOD_KEY_START_OF_ENUM);

        register_keydown_event(Input::Key::F5, no_mods, [this]() {
            bool s = !m_enabled.load();
            m_enabled.store(s);
            Output::send<LogLevel::Verbose>(STR("[HorseMod] overlay {}\n"),
                s ? STR("ON") : STR("OFF"));
            if (!s) m_backend.hideAll();
        });

        // F6 — single-frame step.  Lazily turns on Freeze-frame on first
        // press so the user doesn't need to touch the ImGui tab.  Holding
        // F6 yields ~30 fps slow-motion via UE4SS's keyboard auto-repeat:
        // each press queues one frame; the cockpit-hook state machine
        // drains them one per two cockpit ticks (see frame_step_apply).
        register_keydown_event(Input::Key::F7, no_mods, [this]() {
            bool s = !m_free_camera_enabled.load();
            m_free_camera_enabled.store(s);
            Output::send<LogLevel::Verbose>(STR("[HorseMod] free-camera {}\n"),
                s ? STR("ON") : STR("OFF"));
        });

        register_keydown_event(Input::Key::F6, no_mods, [this]() {
            // First press while running: latch Freeze ON so the user
            // sees an immediate freeze and the step actually advances
            // a SINGLE frame instead of letting the engine free-run.
            //
            // Note: the HUD "Step 1 (F6)" button (see render_tab_impl)
            // does NOT latch freeze — it only adds to step_pending and
            // is greyed out unless Freeze is already on.  That's by
            // design: the button path expects the user to have opened
            // the HUD and turned on Freeze deliberately, whereas the
            // hotkey path is the "just press F6 and it works"
            // convenience entry-point.
            if (!m_freeze_frame.load())
            {
                m_freeze_frame.store(true);
            }
            m_step_pending.fetch_add(1);
        });

        register_tab(STR("HorseMod"), [](CppUserModBase* inst) {
            static_cast<HorseMod*>(inst)->render_tab_impl();
        });

        s_instance.store(this);
        Output::send<LogLevel::Verbose>(
            STR("[HorseMod] ctor v0.10.0 (KHit walker)\n"));
    }

    ~HorseMod() override
    {
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor ENTER\n"));

        // Zero instance pointer FIRST so any in-flight hook sees null.
        s_instance.store(nullptr);

        if (m_hook_registered && !m_hook_path.empty())
        {
            UObjectGlobals::UnregisterHook(m_hook_path, m_hook_ids);
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor unregistered cockpit hook pre={} post={}\n"),
                m_hook_ids.first, m_hook_ids.second);
        }
        // m_cam_lock will restore any active patches via its own dtor
        // when our member destruction runs after this body returns.
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor EXIT\n"));
    }

    auto on_ui_init() -> void override { UE4SS_ENABLE_IMGUI() }

    auto on_unreal_init() -> void override
    {
        // Resolve SC6 native function RVAs now that the game image is loaded:
        //   ALuxBattleChara_GetBoneTransformForPose @ image + 0x462760
        //   LuxSkeletalBoneIndex_Remap              @ image + 0x898140
        // These are used by KHitWalker to transform KHitArea OBBs into
        // world space via the owning chara's bone pose.
        Horse::NativeBinding::resolve();

        // Push the default hit-flash duration into the walker so it's
        // correct on frame 0 without the user having to touch the
        // slider.  Now in cockpit ticks (was ms / 60Hz before).
        Horse::KHitWalker::setStickyFrames(m_flash_frames.load());

        // Install the WH_KEYBOARD_LL hook and start the RawInput worker
        // thread eagerly so all our input polling works from the first
        // cockpit tick.  Lazy init would also work but would skip any
        // keys pressed before the first free-cam enable.  Both sources
        // run for the life of the process; no teardown needed here.
        (void)Horse::LowLevelKeyInput::instance();
        (void)Horse::RawInputSource::instance();
        Output::send<LogLevel::Verbose>(
            STR("[HorseMod] input sources: LL-hook={} RawInput={}\n"),
            Horse::LowLevelKeyInput::instance().hook_installed()
                ? STR("installed") : STR("FAILED"),
            Horse::RawInputSource::instance().ready()
                ? STR("ready") : STR("initialising..."));
    }

    auto on_update() -> void override
    {
        if (m_hook_registered) return;
        if (++m_poll_counter < 60) return;
        m_poll_counter = 0;
        try_register_cockpit_hook();
    }

private:
    void try_register_cockpit_hook()
    {
        Horse::Obj cockpit = m_lux.cockpit();
        if (!cockpit) return;

        UClass* klass = cockpit.raw()->GetClassPrivate();
        if (!klass) return;

        m_hook_path = klass->GetPathName() + STR(":Update");
        Output::send<LogLevel::Verbose>(STR("[HorseMod] Registering hook: {}\n"), m_hook_path);

        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext& ctx, void*) {
                if (auto* self = s_instance.load(std::memory_order_acquire))
                    self->on_cockpit_update_pre(ctx.Context);
            };
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {};

        m_hook_ids        = UObjectGlobals::RegisterHook(m_hook_path, pre_cb, post_cb, nullptr);
        m_hook_registered = true;
        Output::send<LogLevel::Verbose>(STR("[HorseMod] hook pre={} post={}\n"),
            m_hook_ids.first, m_hook_ids.second);
    }

    // ---- Ansel "always allow photography" apply-per-frame helper -------
    // Pushes UAnselFunctionLibrary::SetIsPhotographyAllowed(bVisible)
    // via ProcessEvent when either the toggle is ON or we're on the
    // ON -> OFF edge (one-shot restore).  Called from the top of the
    // cockpit pre-hook so it runs every frame independent of the F5
    // overlay state.
    //
    // Safe to call before NativeBinding is resolved — this path is
    // pure UE4 reflection and does not touch SC6 RVAs.  Safe when no
    // battle chara exists (menu / loading) because the CDO always
    // exists once the Ansel module is loaded.
    void apply_ansel_override_if_needed()
    {
        using namespace RC;
        using namespace RC::Unreal;

        const bool now  = m_ansel_always_allowed.load();
        const bool last = m_last_applied_ansel_allowed.load();
        // Nothing to do while both the toggle and last-applied are off —
        // let the engine manage the flag itself.
        if (!now && !last) return;

        // Resolve / re-resolve CDO.  UObject::IsReal catches the case
        // where UObjectArray was rebuilt (rare, but survivable).
        if (!m_ansel_cdo || !UObject::IsReal(m_ansel_cdo))
        {
            m_ansel_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                STR("/Script/Ansel.Default__AnselFunctionLibrary"));
            if (!m_ansel_cdo)
            {
                // Ansel plugin not loaded in this build / run; silently
                // skip.  (One-shot log would be nice but is low value
                // — the toggle is visibly inert in that case.)
                return;
            }
        }

        UFunction* f = m_fn_set_photo_allowed.on(
            m_ansel_cdo, STR("SetIsPhotographyAllowed"));
        if (!f) return;

        // `now` = desired visibility state; `last` captures whether
        // we're on the restore edge (last=true, now=false → push false
        // once).
        struct { bool bIsPhotographyAllowed; } p{ now };
        m_ansel_cdo->ProcessEvent(f, &p);

        m_last_applied_ansel_allowed.store(now);
    }

    // ---- Frame-step + freeze-frame driver -------------------------------
    // Called every cockpit tick.  Computes the target speedval from the
    // user's Freeze and Slow-mo toggles, plus any pending step-frame
    // requests, and pushes it into Horse::SpeedControl.  Lazily
    // resolves+enables the SpeedControl patches on first need; lazily
    // disables them when nothing's forcing speed (so the engine runs at
    // native rate without our overhead).
    //
    // State machine for the step counter (mirrors the old GamePause one
    // but rewritten in terms of speedval):
    //
    //   click(F6)  m_step_pending++             (sets target=1.0 next tick)
    //   tick A     expecting=false: target=1.0; expecting=true
    //   tick B     expecting=true:  target=base; counter--; expecting=false
    //
    // Two cockpit ticks per advanced game frame because the engine reads
    // speedval inside its world tick — we lift the freeze, world ticks
    // once at full speed, we re-apply the freeze.
    void frame_step_apply()
    {
        const bool freeze     = m_freeze_frame.load();
        const bool slow_mo    = m_speed_enabled.load();
        const int  pending    = m_step_pending.load();
        const bool expecting  = m_step_expecting.load();

        // Compute the "base" target (what speedval should be when no
        // step is in flight).
        float base;
        if (freeze)         base = 0.0f;
        else if (slow_mo)   base = m_speed_value.load();
        else                base = 1.0f;

        // Step-state override.  Each step burns 2 cockpit ticks: first
        // tick lifts the freeze (speedval=1), second tick restores base
        // and decrements the counter.
        float target = base;
        if (pending > 0)
        {
            if (expecting)
            {
                // World ticked at full speed last engine frame; restore
                // base and consume one queued step.
                target = base;
                m_step_pending.fetch_sub(1);
                m_step_expecting.store(false);
            }
            else
            {
                // Lift the freeze for the next engine tick.
                target = 1.0f;
                m_step_expecting.store(true);
            }
        }

        // Decide whether SpeedControl needs to be active this frame.
        // Active iff anything's forcing the rate away from 1.0 (Freeze,
        // Slow-mo, or step-in-flight).  Otherwise let the engine run
        // at native rate without our patches in the way.
        const bool need_active =
            freeze || slow_mo || pending > 0 || expecting;

        if (need_active)
        {
            if (!m_speed_control.is_enabled())
            {
                if (!m_speed_control.is_resolved())
                    m_speed_control.resolve();
                m_speed_control.enable();
            }
            m_speed_control.set_value(target);
        }
        else
        {
            if (m_speed_control.is_enabled())
                m_speed_control.disable();
        }
    }

    // ------------------------------------------------------------------
    // Free-fly camera driver — wired into the cockpit pre-hook.
    //
    // Per-tick responsibilities:
    //   * Resolve the ALuxBattleCamera* from LuxBattleManager.BattleCamera
    //     (UObject property, stable within a battle, null between).
    //   * Handle UI-toggle edge transitions (ON → snapshot current pose +
    //     enable CamLock; OFF → release CamLock, drop our pose state).
    //   * If enabled, drive the camera-manager's POV cache from
    //     keyboard / gamepad input via FreeCamera::tick(), which
    //     writes directly to PCM+0x410..+0x428.  That block IS the
    //     renderer-facing POV — see the long comment on
    //     m_cached_player_camera_manager above for the Ghidra
    //     evidence and the iterations that led us to it.
    //
    // We DON'T early-return when the overlay F5 is off — the user may
    // want to fly the camera around to take screenshots without the
    // overlay, matching the Ansel-replacement use case.
    // ------------------------------------------------------------------
    void free_camera_apply()
    {
        // Resolve the APlayerCameraManager every tick — this is the
        // write target for Free-Fly pose data (see the long comment
        // on m_cached_player_camera_manager for why it is NOT the
        // ALuxBattleCamera from LuxBattleManager.BattleCamera).
        //
        // Primary path (reflection):
        //   find-first-of APlayerController → read its
        //   "PlayerCameraManager" UObject* property.
        // Fallback (direct offset):
        //   PC+0x420 is the native PlayerCameraManager field on
        //   APlayerController — this is the EXACT offset that
        //   UWorld::Tick @ 0x141f02230 reads when it invokes
        //   APlayerCameraManager_CommitPOV_NoInterp(pc[0x84]).
        //   If UE4SS reflection ever fails to find the property
        //   (e.g. a build where the name string is stripped) the
        //   direct-offset path still works.
        //
        // Both lookups are hashed FName-indexed / trivial pointer reads
        // and cached across frames via GlobalPtr::get / Obj::getObj —
        // GlobalPtr revalidates on level transitions automatically.
        void* pcm = nullptr;
        UObject* pc_raw = m_player_controller.get(L"PlayerController");
        if (pc_raw)
        {
            Horse::Obj pc_obj{pc_raw};
            Horse::Obj pcm_obj = pc_obj.getObj(L"PlayerCameraManager");
            if (pcm_obj)
            {
                pcm = pcm_obj.raw();
            }
            else
            {
                // Direct-offset fallback — matches the UWorld::Tick
                // read that feeds the engine's own commit path.
                auto* pc_bytes = reinterpret_cast<uint8_t*>(pc_raw);
                void* raw_pcm = *reinterpret_cast<void**>(pc_bytes + 0x420);
                if (raw_pcm) pcm = raw_pcm;
                if (!m_logged_pcm_fallback)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[HorseMod.FreeCamera] reflection didn't find "
                            "PlayerCameraManager property — using direct "
                            "offset fallback PC+0x420 -> 0x{:x}\n"),
                        reinterpret_cast<uintptr_t>(pcm));
                    m_logged_pcm_fallback = true;
                }
            }
        }
        // One-shot first-resolve log — captures BOTH the PC address
        // and the PCM address so the user (or Ghidra) can sanity-check
        // both pointers against whatever the engine reports at runtime.
        if (!m_logged_pcm_resolve && pcm)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod.FreeCamera] resolved PC=0x{:x} PCM=0x{:x} "
                    "(Ghidra-verified write target for +0x410..+0x428)\n"),
                reinterpret_cast<uintptr_t>(pc_raw),
                reinterpret_cast<uintptr_t>(pcm));
            m_logged_pcm_resolve = true;
        }
        m_cached_player_camera_manager = pcm;

        // Handle UI-toggle edge transitions.
        const bool want_on = m_free_camera_enabled.load();
        if (want_on != m_free_camera.is_enabled())
        {
            m_free_camera.set(want_on, m_cam_lock, pcm);
        }

        // Per-tick pose update (no-op if not enabled or pcm null).
        // This is the ONLY commit path — direct memcpy into the PCM's
        // FCameraCacheEntry.POV at +0x410..+0x428.  CamLock's 5 NOP
        // sites (all of which also target PCM+0x410..+0x428) stop the
        // engine from stomping our writes each tick.
        m_free_camera.tick(pcm);
    }

    // ------------------------------------------------------------------
    // CockpitBase_C::Update pre-hook.  Game thread, one call per frame.
    // ------------------------------------------------------------------
    void on_cockpit_update_pre(UObject* raw_cockpit)
    {
        ++m_update_calls;

        // Apply Ansel override first — independent of the overlay F5
        // gate and the NativeBinding-ready gate below, because the user
        // asked for it to be "always" on while the toggle is held.
        apply_ansel_override_if_needed();

        // Camera lock has NO per-frame helper here — it's implemented as
        // a runtime bytepatch (Horse::CamLock) that's flipped on/off
        // from the ImGui toggle.  The patch is a property of the
        // process, not the cockpit tick.

        // VFX suppression: same bytepatch story as camera lock — the
        // toggle is a process-state property, not a per-frame action.
        // No call needed here.

        // Frame-step + freeze-frame driver.  Computes the desired
        // speedval from the (Freeze, Slow-mo, step-counter) tuple and
        // pushes it into Horse::SpeedControl.  Must run here (not from
        // the ImGui callback) because cockpit::Update ticks even while
        // SpeedControl is at 0 (UMG widget tick is independent of
        // world tick), while the ImGui tab callback only runs when the
        // user has the menu open.
        frame_step_apply();

        // Free-camera driver.  Resolves ALuxBattleCamera* from the current
        // LuxBattleManager.BattleCamera property (null outside battle)
        // and feeds it to m_free_camera.tick() which polls keyboard and
        // writes the pose fields directly on the camera actor.  Running
        // this unconditionally (not gated by m_enabled) matches the other
        // "always on while toggled" features above.
        free_camera_apply();

        // Always-on heartbeat: once every ~2s print a single line so we can
        // confirm the hook is ticking even while the overlay is off.  This
        // separates "hook not firing" from "F5 not toggled".
        // DISABLED — noisy; re-enable if diagnosing "is the hook firing?".
#if 0
        if ((m_update_calls & 0x7F) == 1)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod.Tick] calls={} enabled={} pivot=0x{:x} "
                    "backend_ready={} native_ready={}\n"),
                m_update_calls,
                m_enabled.load() ? 1 : 0,
                reinterpret_cast<uintptr_t>(raw_cockpit),
                m_backend.isReady() ? 1 : 0,
                Horse::NativeBinding::isReady() ? 1 : 0);
        }
#endif

        if (!m_enabled.load() || !raw_cockpit) return;
        if (!Horse::NativeBinding::isReady())
        {
            if (!m_logged_native_missing)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] NativeBinding not ready — KHit draw disabled\n"));
                m_logged_native_missing = true;
            }
            return;
        }

        Horse::Obj pivot{raw_cockpit};

        // Sync LineBatcher slot with the ImGui toggle.
        if (m_backend.slot() != m_slot.load())
            m_backend.setSlot(m_slot.load());
        m_backend.primeFrom(pivot);
        if (!m_backend.isReady()) return;

        m_backend.beginFrame();

        const float T = m_thickness.load();

        int charas_seen = 0;
        int nodes_drawn = 0;

        // ---- Weapon visibility snapshot ---------------------------------
        // Compute once per frame.  `apply_weapons` is true when we need to
        // actively push SetWeaponVisibility into the game this frame:
        //   * when the toggle is ON — re-apply every frame to overwrite
        //     any game-driven re-show (the engine can flip visibility as
        //     part of cinematic cues; we fight it back to hidden).
        //   * on the ON->OFF transition — call once with true to restore
        //     visibility, then stop touching it.
        const bool hide_weapons_now = m_hide_weapons.load();
        const bool was_hiding       = m_last_applied_hide_weapons.load();
        const bool apply_weapons    = hide_weapons_now || was_hiding;
        // Cache the UFunction resolution once.  ALuxBattleChara's
        // SetWeaponVisibility is declared BlueprintCallable, so it's a
        // regular reflection-reachable UFunction shared across all
        // instances of the class.
        static Horse::Fn s_fn_set_weapon_vis;

        // ---- Character-mesh visibility ----------------------------------
        // Now handled by Horse::CharaInvis bytepatches (see ImGui block
        // for the toggle).  No per-frame UFunction call here — the
        // patch lives inside the engine's own visibility-getter so it
        // works invariantly across all move states without flicker.

        m_lux.forEachChara([&](int i, Horse::Obj chara) {
            if (i >= 2) return;  // only P1 / P2; ignore spectators
            ++charas_seen;
            int32_t pi = chara.getValueOr<int32_t>(L"PlayerIndex", i);
            if (pi < 0 || pi > 1) pi = i;

            // Push the weapon-visibility state for this chara.  Done
            // first so it runs even if all list toggles are off below.
            if (apply_weapons)
            {
                struct { bool bVisible; } p{ !hide_weapons_now };
                chara.callRaw(s_fn_set_weapon_vis,
                              L"SetWeaponVisibility", &p);
            }

            // Snapshot this player's three toggles once per chara so the
            // inner-loop gate is branch-free.
            const bool show_hurt       = shouldShow(pi, Horse::KHitList::Hurtbox);
            const bool show_atk        = shouldShow(pi, Horse::KHitList::Attack );
            const bool show_body       = shouldShow(pi, Horse::KHitList::Body   );
            const bool hide_not_dmg    = m_hide_not_damage_active.load();
            const bool hide_not_pf     = m_hide_not_per_frame_active.load();
            const bool hide_unaddr_hurt = m_hide_unaddressable_hurt.load();
            if (!show_hurt && !show_atk && !show_body) return;

            // KHit +0x60 values are already in absolute UE4 world space
            // (Z-up, cm) — the game's per-frame update bakes chara position,
            // yaw, and bone-local into the matrix that wrote them.  The
            // walker just reads them raw; no xform parameter needed.
            Horse::KHitWalker::forEachKHit(
                chara.raw(),
                static_cast<uint32_t>(pi),
                [&](const Horse::KHitDraw& d) {
                    // Gate by list kind using THIS chara's per-player flags.
                    switch (d.list)
                    {
                        case Horse::KHitList::Hurtbox:
                            if (!show_hurt) return;
                            // "Addressable only" — skip hurtboxes whose
                            // +0x17 slot is outside ResolveAttackVs
                            // HurtboxMask22's iteration range
                            // (chara+0x44494 clamped to 22).  The engine
                            // will happily OR attacker bits into the
                            // PerHurtboxBitmask index corresponding to
                            // these nodes, but the classifier never
                            // reads those slots so no reaction is
                            // written and no damage lands — they're
                            // effectively engine-invisible.  See
                            // m_hide_unaddressable_hurt comment for the
                            // full derivation.
                            if (hide_unaddr_hurt && !d.classifier_addressable)
                                return;
                            break;
                        case Horse::KHitList::Attack:
                            if (!show_atk) return;
                            // Engine-derived sub-gate: strikes vs throws.
                            if (!shouldShowAttackRole(d.attack_role)) return;
                            // (The "Live attacks only" filter that tested
                            // node+0x14 was removed 2026-04 after Ghidra
                            // verified the 0x3FFFD floor made it too
                            // permissive during neutral — the filter was
                            // misleading more than helpful.  Use
                            // hide_not_dmg below for an engine-truth
                            // damage gate instead.)
                            // Damage gate — the actor-level own-attack
                            // mask that ResolveAttackVsHurtboxMask22 AND-s
                            // against the opponent's hurtbox mask before
                            // it fires a hit.  Unlike +0x14 this is empty
                            // during neutral and only lights up on real
                            // active frames.
                            if (hide_not_dmg && !d.is_damage_active) return;
                            // Per-frame damage gate — strictest of the
                            // three "is this attack live" filters.
                            // Hides the box during startup AND recovery
                            // within the same move-slot, leaving only
                            // the engine-authored damage-window frames
                            // visible.  Independent of the other two,
                            // so users can stack them or use just this.
                            if (hide_not_pf && !d.is_per_frame_active) return;
                            break;
                        case Horse::KHitList::Body:
                            if (!show_body) return;
                            break;
                    }

                    const Horse::FLinColor col = colourFor(d, pi);
                    Horse::DrawKHitDraw(m_backend, d, col, T);
                    ++nodes_drawn;
                });
        });

        // Commit the state we actually pushed to the game this frame.
        // Only update last-applied when we had at least one chara to push
        // to; otherwise we'd "lose" the pending transition (e.g. toggle
        // flips OFF between rounds while no chara exists — we'd never
        // get a chance to call SetWeaponVisibility(true) and weapons
        // would stay hidden).
        //
        // On next frame:
        //   * If hide_weapons_now is still true we keep re-hiding.
        //   * If it transitions true -> false we'll detect (was_hiding
        //     was true, now false) and apply once to restore.
        //   * If false -> false we skip entirely.
        if (charas_seen > 0)
        {
            m_last_applied_hide_weapons.store(hide_weapons_now);
        }

        m_backend.endFrame();

        // Once per ~2s: dump a summary so we can tell whether each stage
        // fired.  Parallel to the KHitWalker's shouldLog() throttle.
        // DISABLED — noisy; re-enable for end-to-end health checks.
#if 0
        if ((++m_diag_tick & 0x7F) == 1)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] frame pivot=0x{:x} backend_ready={} charas={} drawn={}\n"),
                reinterpret_cast<uintptr_t>(raw_cockpit),
                m_backend.isReady() ? 1 : 0,
                charas_seen,
                nodes_drawn);
        }
#else
        (void)charas_seen; (void)nodes_drawn; (void)raw_cockpit;
        ++m_diag_tick;
#endif
    }

    // ------------------------------------------------------------------
    // Colour scheme (engine-role driven, not size-heuristic)
    //   Hurtboxes — green (receive volumes).  Bright red when reaction_hot
    //               (this slot just got hit — sticky-extended).
    //   Attacks   — amber (strike) / magenta (throw/grab).  Hot (the
    //               currently-active cell) overrides to bright yellow for
    //               strikes or bright pink for throws so you can still see
    //               which one is live.
    //   Body/push — dim blue.  These are not involved in damage.
    // A subtle per-player hue nudge keeps P1 / P2 distinguishable when
    // they overlap visually.
    // ------------------------------------------------------------------
    static Horse::FLinColor colourFor(const Horse::KHitDraw& d, int pi)
    {
        const float player_tint = (pi == 1) ? 0.80f : 1.0f;

        switch (d.list)
        {
            case Horse::KHitList::Hurtbox:
            {
                if (d.reaction_hot)
                    return Horse::FLinColor{ 1.0f, 0.15f, 0.15f, 1.0f };

                // Unified green for all hurtbox entries — the engine
                // doesn't sub-categorise these from the defender side.
                return Horse::FLinColor{ 0.25f * player_tint,
                                         0.95f,
                                         0.35f * player_tint, 1.0f };
            }

            case Horse::KHitList::Attack:
            {
                const bool is_throw =
                    (d.attack_role == Horse::KHitAttackRole::Throw);

                if (d.is_current_attack)
                {
                    // Hot (live this frame) — max-saturation colour so it
                    // pops out of the other attack boxes on the same chara.
                    return is_throw
                        ? Horse::FLinColor{ 1.0f, 0.30f, 0.85f, 1.0f }  // hot throw = pink
                        : Horse::FLinColor{ 1.0f, 1.0f, 0.25f, 1.0f }; // hot strike = yellow
                }
                // Cold.
                return is_throw
                    ? Horse::FLinColor{ 0.85f * player_tint,
                                        0.15f,
                                        0.70f * player_tint, 0.6f }   // cold throw = magenta
                    : Horse::FLinColor{ 1.0f * player_tint,
                                        0.55f * player_tint,
                                        0.10f, 0.6f };                // cold strike = amber
            }

            case Horse::KHitList::Body:
            default:
                return Horse::FLinColor{ 0.25f,
                                         0.45f * player_tint,
                                         1.0f * player_tint, 0.5f };
        }
    }

    // ------------------------------------------------------------------
    // ImGui tab
    // ------------------------------------------------------------------
    void render_tab_impl()
    {
        bool enabled = m_enabled.load();
        if (ImGui::Checkbox("Overlay enabled (F5)", &enabled))
        {
            m_enabled.store(enabled);
            if (!enabled) m_backend.hideAll();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("ticks:%d  hook:%s  lbc:%s  native:%s",
            m_update_calls,
            m_hook_registered                ? "OK" : "pending",
            m_backend.isReady()              ? "OK" : "idle",
            Horse::NativeBinding::isReady()  ? "OK" : "miss");

        // --- Live move-frame display -------------------------------------
        // Deref chara+0x44068 ActiveLaneStateCursorPtr and show
        // CurrentAnimFrame / AnimLengthFrames for each player.  Costs
        // ~4 safe reads per frame per player — negligible.
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Move frame");
            auto row = [](const char* label, int pi) {
                void* chara = Horse::KHitWalker::charaSlotFromGlobal(
                    static_cast<uint32_t>(pi));
                const auto s = Horse::KHitWalker::readLaneSnapshot(chara);
                ImGui::TextUnformatted(label);
                ImGui::SameLine(48.0f);
                if (!s.has_move)
                {
                    ImGui::TextDisabled("idle");
                    return;
                }
                const int curI = static_cast<int>(s.current_frame);
                const int totI = static_cast<int>(s.length_frames);
                ImGui::Text("%3d / %3d  move=0x%04X  lane=%d  speed=%.2fx%s%s",
                            curI, totI,
                            static_cast<uint16_t>(s.packed_move),
                            static_cast<int>(s.lane_index),
                            s.playback_speed,
                            s.in_transition ? "  [T]" : "",
                            s.finished      ? "  [done]" :
                            s.at_end        ? "  [end]"  : "");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "LuxMoveLaneState at *(chara+0x44068):\n"
                    "  +0x02  PackedMoveAddr  = ((bank<<12)|slot)\n"
                    "  +0x04  TickCounter\n"
                    "  +0x08  CurrentAnimFrame (advanced each tick by\n"
                    "         time_dilation * PlaybackSpeedCurrent)\n"
                    "  +0x10  AnimLengthFrames (bank cell +0x34)\n"
                    "  +0x1A  AtEndFlag        ([end])\n"
                    "  +0x24  FrameStepFinished([done])\n"
                    "  +0x26  InTransitionFlag ([T])\n"
                    "  +0x30  PlaybackSpeedCurrent");
            };
            row("P1:", 0);
            row("P2:", 1);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("KHit lists — per player");
        ImGui::TextDisabled(
            "Hurtboxes + Attacks default ON.  Body / pushbox default OFF "
            "(noisy — spacing context only).");

        auto per_player_row = [](const char* label,
                                 std::atomic<bool>& hurt,
                                 std::atomic<bool>& atk,
                                 std::atomic<bool>& body,
                                 const char* id_suffix)
        {
            ImGui::PushID(id_suffix);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(80.0f);
            {
                bool h = hurt.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Hurtboxes##%s", id_suffix);
                if (ImGui::Checkbox(tag, &h)) hurt.store(h);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit HurtboxListHead (chara+0x444B8).\n"
                    "Volumes that RECEIVE damage.  Each node's BoneId byte\n"
                    "at +0x17 indexes PerHurtboxBitmask[22] (+0x44078) and\n"
                    "PerHurtboxReactionState[22] (+0x1C74).  Green; red\n"
                    "flash on reaction-state non-zero (just got hit),\n"
                    "sticky-extended per the 'Hit-flash duration' slider.");
            }
            ImGui::SameLine();
            {
                bool a = atk.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Attacks##%s", id_suffix);
                if (ImGui::Checkbox(tag, &a)) atk.store(a);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit AttackListHead (chara+0x44498).\n"
                    "Volumes that DEAL damage (or initiate grabs).\n"
                    "Strike: amber cold, yellow when active.\n"
                    "Throw : magenta cold, pink when active.\n"
                    "\"Active\" = engine's per-frame gate at node+0x14\n"
                    "(set by the tick to (hotMask >> node[+0x17]) & 1).\n"
                    "When active, the node's CategoryMask is OR'd into\n"
                    "the opponent's PerHurtboxBitmask this tick.");
            }
            ImGui::SameLine();
            {
                bool b = body.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Body##%s", id_suffix);
                if (ImGui::Checkbox(tag, &b)) body.store(b);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit BodyListHead (chara+0x44478).\n"
                    "Character-to-character physical pushing only — used\n"
                    "by LuxBattle_SolvePhysBodyCollision @ 0x14030CCF0.\n"
                    "Not involved in damage resolution.  Dim blue.");
            }
            ImGui::PopID();
        };

        per_player_row("P1",
                       m_show_p1_hurt, m_show_p1_atk, m_show_p1_body, "p1");
        per_player_row("P2",
                       m_show_p2_hurt, m_show_p2_atk, m_show_p2_body, "p2");

        // Quick-action buttons for the common cases.
        if (ImGui::SmallButton("All ON"))
        {
            m_show_p1_hurt.store(true); m_show_p1_atk.store(true); m_show_p1_body.store(true);
            m_show_p2_hurt.store(true); m_show_p2_atk.store(true); m_show_p2_body.store(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("All OFF"))
        {
            m_show_p1_hurt.store(false); m_show_p1_atk.store(false); m_show_p1_body.store(false);
            m_show_p2_hurt.store(false); m_show_p2_atk.store(false); m_show_p2_body.store(false);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Defaults (no Body)"))
        {
            m_show_p1_hurt.store(true); m_show_p1_atk.store(true); m_show_p1_body.store(false);
            m_show_p2_hurt.store(true); m_show_p2_atk.store(true); m_show_p2_body.store(false);
        }

        // --- Attack role filters (engine-derived) -----------------------
        // Source: LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100
        // partitions the 64-bit CategoryMask at node+0x08 into two disjoint
        // bit regions.  We expose the same partition here.
        ImGui::Spacing();
        ImGui::TextUnformatted("Attack roles (shared)");
        ImGui::TextDisabled(
            "Engine-derived from CategoryMask bits at node+0x08.\n"
            "Strike = normal attacks (all category bits except 31, 55).\n"
            "Throw  = grabs / throws (mask bits 31 or 55).");
        {
            bool s = m_show_atk_strike.load();
            if (ImGui::Checkbox("Strike##atkrole", &s)) m_show_atk_strike.store(s);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Normal attacks that damage on contact with a hurtbox.\n"
                "Mask bits in 0xFF7FFFFF7FFFFFFF (all except bit 31/55).\n"
                "Drawn amber (cold) / yellow (currently active).");
            ImGui::SameLine();
            bool t = m_show_atk_throw.load();
            if (ImGui::Checkbox("Throw / Grab##atkrole", &t)) m_show_atk_throw.store(t);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Grab attacks (throws, command grabs).  The classifier\n"
                "pre-scans for bits 31 or 55 in the CategoryMask and\n"
                "handles them as an all-or-nothing gate before the\n"
                "per-hurtbox strike loop runs.\n"
                "Mask: 0x0080000080000000.\n"
                "Drawn magenta (cold) / pink (currently active).");
        }
        if (ImGui::SmallButton("Strikes only"))
        {
            m_show_atk_strike.store(true);
            m_show_atk_throw .store(false);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Throws only"))
        {
            m_show_atk_strike.store(false);
            m_show_atk_throw .store(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Both roles"))
        {
            m_show_atk_strike.store(true);
            m_show_atk_throw .store(true);
        }

        // Spacer before the damage-gate filters.
        ImGui::Spacing();

        // --- Damage-active only (actor-level own-attack cell) -----------
        // Tighter than +0x14.  Reads *chara[+0x44058] (the own-attack
        // active cell) and tests bit (node[+0x17] & 0x3F) — the same
        // per-slot bit the engine uses in ResolveAttackVsHurtboxMask22
        // when deciding whether to fire a hit against the opponent's
        // hurtbox mask.  Unlike the +0x14 geometry gate this cell is
        // zero during neutral — so "Damage-active only" gives the cleanest
        // "is this box doing anything RIGHT NOW?" view.
        {
            bool dg = m_hide_not_damage_active.load();
            if (ImGui::Checkbox("Damage-active only (chara+0x44058 bit)", &dg))
                m_hide_not_damage_active.store(dg);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "When on, hide attack-list nodes whose slot bit is not\n"
                "set in the actor's own-attack-active cell at\n"
                "*chara[+0x44058].  This is the mask that\n"
                "ResolveAttackVsHurtboxMask22 AND-s against the opponent's\n"
                "PerHurtboxBitmask — if the bit is clear, the node\n"
                "physically cannot damage this tick, regardless of\n"
                "geometry.\n\n"
                "Distinction from the +0x14 filter above:\n"
                " • +0x14 is the per-node GEOMETRY gate.  Subject to the\n"
                "   MoveVM 0x3FFFD floor — slots {0, 2..17} force-on every\n"
                "   frame, so ~20 cold boxes still pass it in neutral.\n"
                " • +0x44058 is the ACTOR-level DAMAGE gate.  Empty\n"
                "   during neutral; lights up only on real active frames.\n\n"
                "On : strongest 'currently hitting' filter.\n"
                "Off: show all geometry-live attacks (respects +0x14 if\n"
                "     that filter is also on).");
        }

        // --- Per-frame damage-active only (classifier predicate) ---------
        // Mirrors the engine's classifier at ResolveAttackVsHurtboxMask22
        // (0x14033C100):
        //   capable_of_damage = (+0x14 != 0) AND
        //                       ((node.CategoryMask & per_move_cell) != 0)
        //
        // The category-mask intersection correctly handles body-
        // attached attack boxes that live at floor slots (0, 2..17).
        // Their +0x14 is always set by the engine's floor, so the
        // earlier "+0x14 minus floor" version of this filter wrongly
        // hid them.  The category check brings them back: hidden
        // during neutral (cell==0), shown during moves whose
        // authored category set overlaps the node's CategoryMask.
        {
            bool pf = m_hide_not_per_frame_active.load();
            if (ImGui::Checkbox("Per-frame damage-active only", &pf))
                m_hide_not_per_frame_active.store(pf);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "When on, hide attack-list nodes that don't currently\n"
                "satisfy the engine's damage-classifier predicate:\n"
                "  capable iff (+0x14 != 0)\n"
                "         AND (node.CategoryMask & per_move_cell != 0)\n"
                "(the same test ResolveAttackVsHurtboxMask22 uses to\n"
                "decide whether overlap should fire a hit).\n\n"
                "Compared to 'Damage-active only' above:\n"
                " • Damage-active uses a slot-bit check on the per-\n"
                "   move cell — stays on through the whole move slot.\n"
                " • This uses the category-mask intersection, which\n"
                "   tracks active-frame transitions more closely AND\n"
                "   correctly handles body-attached attack boxes (at\n"
                "   floor slots 0/2-17 whose +0x14 is always set).\n\n"
                "On  : show only attacks the engine considers capable\n"
                "      of producing damage right now.  Pair with Freeze\n"
                "      frame / F6 step for frame-data analysis.\n"
                "Off : ignore this filter (other gates still run).");
        }

        // --- Addressable hurtboxes only (classifier-range gate) ----------
        // Hide hurtboxes whose +0x17 slot is outside the classifier's
        // iteration range at ResolveAttackVsHurtboxMask22 — i.e.
        // bone_id_internal >= ClassifierHurtboxBound (chara+0x44494,
        // capped at 22).  Those hurtboxes participate in overlap
        // testing and the engine does OR attacker bits into their
        // corresponding PerHurtboxBitmask slot, but the classifier's
        // for-loop bound means it never reads that slot — so the
        // hurtbox can never produce a reaction, flash red, or cause
        // damage.
        //
        // *** GOTCHA (2026-04): chara+0x44494 is NOT the hurtbox list's
        // own slot count.  It's the ATTACK list's max-slot, written by
        // the ATTACK stream deserialiser in InitCharaSlotForMove.
        // The engine reuses it as the hurtbox-iteration bound.  During
        // moves with few attack slots (dodges, pure-movement, block,
        // throw-whiff), the bound can be SMALLER than the move's
        // hurtbox list needs, and real per-move hurtboxes at high slot
        // indices will be flagged unaddressable.  That's an honest
        // reflection of engine behaviour (those hurtboxes truly won't
        // react), but it can LOOK like "HorseMod is hiding my new
        // per-move hurtboxes" — which is the most common reason for
        // the classic "my move added a hurtbox but I don't see it"
        // complaint.  Users with this symptom should turn this toggle
        // OFF to confirm the boxes exist in the list.
        //
        // (An earlier revision of this UI exposed a +0x14-based "live
        // hurtboxes only" filter — that was a mistake; hurtboxes
        // don't go through TickHitResolution's per-frame +0x14
        // update, so their +0x14 is pinned at 1 forever and such a
        // filter is a no-op.)
        ImGui::Spacing();
        {
            bool uh = m_hide_unaddressable_hurt.load();
            if (ImGui::Checkbox("Classifier-addressable hurtboxes only", &uh))
                m_hide_unaddressable_hurt.store(uh);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "When on, hide hurtboxes whose per-node slot byte at\n"
                "+0x17 is >= the classifier's iteration bound\n"
                "(chara+0x44494, capped at 22).\n\n"
                "Why this matters: LuxBattle_ResolveAttackVsHurtboxMask22\n"
                "iterates only slots 0..bound-1 when reading\n"
                "PerHurtboxBitmask.  A hurtbox authored at slot >= bound\n"
                "will have attacker bits OR'd into its PerHurtboxBitmask\n"
                "entry by UpdateAllKHitWorldCenters, but the classifier\n"
                "never looks at that entry — no reaction is written, no\n"
                "damage is applied.  Visually the hurtbox is alive but\n"
                "functionally it's engine-invisible.\n\n"
                "GOTCHA: the bound at 0x44494 is the ATTACK list's\n"
                "max-slot (not the hurtbox list's).  During dodges,\n"
                "movement, block, or throw-whiff moves that have few\n"
                "attack slots, the bound can be small and real\n"
                "per-move hurtboxes at high indices will disappear\n"
                "from the overlay with this toggle on.  If your move\n"
                "'added a new hurtbox' that isn't showing, try turning\n"
                "this OFF first.\n\n"
                "Off: show every hurtbox including unaddressable ones.\n"
                "On : show only hurtboxes the classifier actually reads\n"
                "     — the 'can this hurtbox ever produce a reaction?'\n"
                "     view.");
        }

        // --- Weapon visibility override ---------------------------------
        // Force hide both charas' weapons so they stop occluding the
        // hitbox overlay.  Calls SetWeaponVisibility(false) every frame
        // while on (so the game's own show-triggers don't sneak weapons
        // back in); calls SetWeaponVisibility(true) once on OFF to
        // restore.  Applies only while the overlay is enabled — if F5
        // turns the mod off, weapons stay in whatever state the engine
        // last set (typically visible).
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Scene");
        {
            bool hw = m_hide_weapons.load();
            if (ImGui::Checkbox("Hide weapons", &hw))
                m_hide_weapons.store(hw);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Hide both characters' weapon meshes while the overlay\n"
                "is enabled.  Useful when a bulky weapon (Nightmare's\n"
                "sword, Astaroth's axe) occludes the hitbox volumes you\n"
                "want to inspect.\n\n"
                "Uses ALuxBattleChara::SetWeaponVisibility(bool) — a\n"
                "BlueprintCallable UFunction on the battle chara class.\n"
                "Re-applied every frame while on, so any game-driven\n"
                "visibility change (cinematic cues) is overridden.\n"
                "Turning the toggle off calls SetWeaponVisibility(true)\n"
                "once per chara and then leaves the state alone.");

            // --- Always allow Ansel camera -----------------------------
            bool aa = m_ansel_always_allowed.load();
            if (ImGui::Checkbox("Always allow Ansel camera", &aa))
                m_ansel_always_allowed.store(aa);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Force NVIDIA Ansel (the free-camera photo mode) to be\n"
                "available at all times by calling\n"
                "UAnselFunctionLibrary::SetIsPhotographyAllowed(true)\n"
                "every frame.  SC6 normally blocks Ansel outside of\n"
                "specific states (menus, cinematics, ring-out); with\n"
                "this on you can trigger the Ansel hotkey any time.\n\n"
                "Runs independent of the F5 overlay toggle.  Turning\n"
                "this off calls SetIsPhotographyAllowed(false) exactly\n"
                "once so the engine resumes managing the flag itself.");

            // --- Lock camera position -----------------------------------
            // Bytepatch-based: NOPs the engine's per-frame stores into
            // the camera struct, so whatever pose the camera is in at
            // toggle-ON time stays put until OFF.  See
            // horselib/CamLock.hpp for the disassembly walk and the
            // history of why the previous CameraCache.POV-write
            // approach didn't work.
            //
            // UI binding: read directly from the live CamLock state
            // rather than from m_lock_camera.  Free-fly camera toggles
            // CamLock on/off behind the scenes, so if we bound to the
            // separate `m_lock_camera` atomic the checkbox could drift
            // out of sync with reality ("checkbox off but camera is
            // locked because free-fly turned it on").  Additionally we
            // grey-out the checkbox while free-fly is active because
            // its underlying CamLock is being driven by the free-fly
            // state machine — letting the user poke the checkbox then
            // would cause a fight between the two owners.
            const bool fc_on = m_free_camera_enabled.load();
            bool lc     = m_cam_lock.is_enabled();
            bool lc_rot = m_cam_lock.is_rotation_enabled();
            if (fc_on) ImGui::BeginDisabled(true);
            if (ImGui::Checkbox("Lock camera position", &lc))
            {
                m_lock_camera.store(lc);
                m_cam_lock.set(lc);
            }
            if (fc_on) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                fc_on
                    ? "Disabled while Free-fly camera is on — free-fly\n"
                      "owns the camera lock while it's active.  Turn\n"
                      "free-fly off first to toggle this manually."
                    : "Freeze the camera at its current pose (location,\n"
                      "rotation, and FOV).  Patches SC6's per-frame\n"
                      "camera-commit instructions (TickAndCommitPOV\n"
                      "store row at ALuxBattleCamera +0x410..+0x428)\n"
                      "to NOPs while held; turning OFF restores the\n"
                      "originals.\n\n"
                      "To re-frame: toggle off, move the camera to where\n"
                      "you want it, toggle back on.\n\n"
                      "Runs independent of the F5 overlay toggle.\n"
                      "Ported from somberness's CE table (the 'Cam'\n"
                      "cheat) — 10 NOPs across 2 sites (primary group).\n"
                      "See 'Lock camera rotation' below for additional\n"
                      "writers outside this primary commit row.");

            // Companion checkbox for the rotation-group patches — the
            // three extra writer functions found via Ghidra that write
            // to the same pose fields outside the primary commit row.
            // Splitting them lets the user engage/disengage rotation
            // lock independently from position lock, and makes it easy
            // to diagnose a regression where rotation patches interact
            // badly with some other game path (just turn rot lock off).
            if (fc_on) ImGui::BeginDisabled(true);
            if (ImGui::Checkbox("Lock camera rotation (experimental)",
                                &lc_rot))
            {
                m_cam_lock.set_rotation_patches(lc_rot);
            }
            if (fc_on) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                fc_on
                    ? "Disabled while Free-fly camera is on — free-fly\n"
                      "owns the rotation-lock group while it's active."
                    : "Additionally NOPs three external writer functions\n"
                      "that re-stomp camera pose outside the primary\n"
                      "commit row:\n"
                      "  rotSite1 (FUN_1420520f0) — per-tick whole-pose\n"
                      "    writer.  NOPing it is required for arrow-key\n"
                      "    look in Free-fly to take effect.  Ghidra\n"
                      "    verified this function writes LocXY+LocZ\n"
                      "    along with rotation, so we NOP all 29 bytes\n"
                      "    of its store block to avoid position stomps.\n"
                      "  rotSite2 (FUN_141f935b0) — target-follow rot\n"
                      "    writer.  Rotation-only.\n"
                      "  rotSite3 (FUN_141d27c80) — SetPOV rotation\n"
                      "    component.  Rotation-only; leaves the\n"
                      "    location path in SetPOV intact so game\n"
                      "    scripts that set camera position still work.\n\n"
                      "Opt-in because enabling these without also having\n"
                      "'Lock camera position' on can make the camera\n"
                      "appear to behave unpredictably.  Free-fly enables\n"
                      "this automatically when it turns on.");

            // Combined status line.
            if (!m_cam_lock.is_resolved() && (lc || lc_rot))
            {
                ImGui::TextDisabled(
                    "(camera-lock AOB did not resolve — check log)");
            }
            else if (m_cam_lock.is_enabled() ||
                     m_cam_lock.is_rotation_enabled())
            {
                const int n_prim = m_cam_lock.is_enabled()          ? 10 : 0;
                const int n_rot  = m_cam_lock.is_rotation_enabled() ?  7 : 0;
                ImGui::TextDisabled(
                    "(camera lock active — %d/%d stores NOPed: "
                    "primary=%s rotation=%s)",
                    n_prim + n_rot, 17,
                    m_cam_lock.is_enabled()          ? "on"  : "off",
                    m_cam_lock.is_rotation_enabled() ? "on"  : "off");
            }

            // --- Free-fly camera (Ansel replacement) --------------------
            // Built-in WASD + arrow-key fly camera.  Uses CamLock to
            // freeze the engine's camera stores then writes the pose
            // ourselves each cockpit tick.  Works WITHOUT invoking
            // Nvidia Ansel — the hitbox overlay stays visible because
            // SC6's `r.Photography.InSession` CVar never gets set.
            ImGui::Spacing();
            {
                bool fc = m_free_camera_enabled.load();
                if (ImGui::Checkbox("Free-fly camera (F7)", &fc))
                    m_free_camera_enabled.store(fc);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Alternative to Nvidia Ansel for free-camera movement.\n"
                    "Writes the pose directly on ALuxBattleCamera's\n"
                    "+0x410..+0x428 fields while CamLock holds off the\n"
                    "engine.  No Ansel session, no r.Photography.In\n"
                    "Session CVar, no overlay disable.\n\n"
                    "Keyboard (game window must be focused):\n"
                    "  W / S     forward / back along view\n"
                    "  A / D     strafe left / right\n"
                    "  E / Q     up / down (world Z)\n"
                    "  ↑ ↓ ← →   or IJKL — pitch / yaw\n"
                    "  Shift     5× speed   |   Ctrl   0.2× speed\n"
                    "(arrows may be dead if SC6 claims them via\n"
                    " RawInput NOLEGACY — fall back to IJKL in that case)\n\n"
                    "Controller (XInput-0):\n"
                    "  Left stick   translate\n"
                    "  Right stick  look\n"
                    "  LT / RT      down / up\n"
                    "  LB / RB      0.2× / 5× speed\n\n"
                    "Enabling implicitly turns on 'Lock camera position'\n"
                    "(and turning free-camera OFF releases it).  You can\n"
                    "still re-frame by toggling off → moving the engine\n"
                    "camera → toggling back on.");

                // Sub-controls, only visible when free-cam is on to
                // avoid cluttering the Scene section.
                if (fc)
                {
                    float mv = m_free_camera.move_speed();
                    if (ImGui::SliderFloat("move (cm/frame)", &mv,
                                           2.0f, 100.0f, "%.1f"))
                        m_free_camera.move_speed() = mv;

                    float lk = m_free_camera.look_speed();
                    if (ImGui::SliderFloat("look (deg/frame)", &lk,
                                           0.2f, 6.0f, "%.2f"))
                        m_free_camera.look_speed() = lk;

                    float fv = m_free_camera.fov_deg();
                    if (ImGui::SliderFloat("FOV (deg)", &fv,
                                           20.0f, 120.0f, "%.0f"))
                        m_free_camera.fov_deg() = fv;

                    // Live pose readout — handy for reproducing shots.
                    ImGui::TextDisabled(
                        "pos=(%.1f, %.1f, %.1f)  rot=(%.1f, %.1f, %.1f)",
                        m_free_camera.loc_x(),
                        m_free_camera.loc_y(),
                        m_free_camera.loc_z(),
                        m_free_camera.pitch(),
                        m_free_camera.yaw(),
                        m_free_camera.roll());

                    // On-screen memory persistence check — read the
                    // camera-manager memory live and compare to our
                    // expected pose.  Makes "is our write actually
                    // reaching memory?" debuggable without reading log
                    // files.
                    if (m_cached_player_camera_manager)
                    {
                        auto* A = reinterpret_cast<uint8_t*>(
                            m_cached_player_camera_manager);
                        auto readF = [A](std::ptrdiff_t off) {
                            float v = 0.0f;
                            std::memcpy(&v, A + off, sizeof(v));
                            return v;
                        };
                        const float mem_lx = readF(0x410);
                        const float mem_ly = readF(0x414);
                        const float mem_lz = readF(0x418);
                        const float mem_p  = readF(0x41C);
                        const float mem_y  = readF(0x420);
                        const float mem_r  = readF(0x424);
                        const float want_lx = m_free_camera.loc_x();
                        const float want_ly = m_free_camera.loc_y();
                        const float want_lz = m_free_camera.loc_z();
                        const float want_p  = m_free_camera.pitch();
                        const float want_y  = m_free_camera.yaw();
                        const float want_r  = m_free_camera.roll();
                        const bool loc_match =
                            mem_lx == want_lx && mem_ly == want_ly &&
                            mem_lz == want_lz;
                        const bool rot_match =
                            mem_p == want_p && mem_y == want_y &&
                            mem_r == want_r;
                        ImGui::TextDisabled(
                            "mem: pcm=0x%p  loc%s=(%.1f, %.1f, %.1f)  "
                            "rot%s=(%.1f, %.1f, %.1f)",
                            m_cached_player_camera_manager,
                            loc_match ? " ==" : " !=",
                            mem_lx, mem_ly, mem_lz,
                            rot_match ? " ==" : " !=",
                            mem_p, mem_y, mem_r);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                            "Live read of APlayerCameraManager+0x410..+0x424.\n"
                            "'==' means memory matches what free-fly wrote\n"
                            "last tick.  '!=' means some writer stomped us\n"
                            "between ticks — tells us there's still an\n"
                            "unpatched writer.  If the memory-read MATCHES\n"
                            "but the camera still doesn't move visually,\n"
                            "the render path has moved — retrace in Ghidra.\n"
                            "(Historical: an earlier revision wrote to the\n"
                            "ALuxBattleCamera actor instead of the PCM and\n"
                            "got persisted=1 / camera=locked forever.)");
                    }
                    else
                    {
                        ImGui::TextDisabled(
                            "mem: <no PlayerCameraManager resolved>");
                    }
                    // Controller-present indicator.  Helpful for "is
                    // XInput actually seeing my pad?"  Updates live as
                    // you plug/unplug.
                    ImGui::TextDisabled(
                        "pad: %s",
                        Horse::FreeCamera::controllerConnected()
                            ? "connected (XInput-0)"
                            : "not detected");
                    // Input-source status: two sources OR-merged.
                    // If both read DOWN/init, keys won't register and
                    // the camera can't be driven from the keyboard —
                    // check the log for install failures.
                    ImGui::TextDisabled(
                        "kbd: LL=%s  RawInput=%s",
                        Horse::LowLevelKeyInput::instance().hook_installed()
                            ? "up" : "DOWN",
                        Horse::RawInputSource::instance().ready()
                            ? "up" : "init");

                    // Input diagnostic — dump raw arrow-key state from
                    // both paths every ~2s.  Used to tell WHY arrows
                    // might not register (LL hook not seeing them vs
                    // GetAsyncKeyState not seeing them vs both fine
                    // but something downstream is eating the move).
                    bool diag = m_free_camera.m_diagnostic.load();
                    if (ImGui::Checkbox("Diagnostic log (arrow keys)", &diag))
                        m_free_camera.m_diagnostic.store(diag);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Print raw keyboard-state values every ~2s to the\n"
                        "UE4SS log.  Each line shows (LL hook / RawInput)\n"
                        "for the arrow cluster plus W (as a known-working\n"
                        "control).\n\n"
                        "Expected while holding VK_UP:\n"
                        "  UP: ll=1 ri=1  W: ll=0 ri=0  (arrows working)\n"
                        "  UP: ll=0 ri=1  (RawInput seeing them; LL missed)\n"
                        "  UP: ll=0 ri=0  (nothing visible — event isn't\n"
                        "                  reaching our process at all;\n"
                        "                  Steam Input mapping is the likely\n"
                        "                  culprit — remap arrows to None in\n"
                        "                  your SC6 Steam-Input profile)\n\n"
                        "Leave off during normal use.");

                    // Memory-persistence diagnostic — tells us whether
                    // our pose writes actually stay resident in the
                    // camera actor between ticks.  Critical for
                    // diagnosing the "UI updates but camera locked in
                    // game" symptom: if `persisted=0` we have an
                    // unpatched writer somewhere; if `persisted=1` the
                    // renderer consumes pose from a different cache
                    // and CamLock patches can't fix it.
                    bool memdiag = m_free_camera.m_mem_diagnostic.load();
                    if (ImGui::Checkbox("Diagnostic log (memory verify)",
                                        &memdiag))
                        m_free_camera.m_mem_diagnostic.store(memdiag);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Print camera-memory verification every ~2s: what\n"
                        "we wrote last tick vs what's in memory NOW.\n\n"
                        "Reads:\n"
                        "  persisted=1  Our writes stayed resident; any\n"
                        "               visual freeze is NOT from a writer\n"
                        "               stomping us — the renderer probably\n"
                        "               consumes pose from a different\n"
                        "               cache (e.g. PlayerCameraManager\n"
                        "               CameraCache.POV).\n"
                        "  persisted=0  Some code wrote the camera fields\n"
                        "               between our ticks.  The 'deltas='\n"
                        "               line shows WHICH field was stomped\n"
                        "               — if only rot fields changed the\n"
                        "               culprit is a rotation writer we\n"
                        "               haven't found; if pos fields too,\n"
                        "               there's an additional pose writer.\n\n"
                        "Check the UE4SS log (bottom-left panel or file).\n"
                        "Leave off during normal use.");
                }

                // Status line for missing PlayerCameraManager (idle menu etc.).
                if (fc && !m_cached_player_camera_manager)
                {
                    ImGui::TextDisabled(
                        "(no APlayerCameraManager — outside a match?)");
                }
            }

            // --- Freeze frame (REWORKED — drives Horse::SpeedControl) --
            // Sets the SpeedControl speedval to 0 to freeze the world,
            // back to its base value (Slow-mo slider or 1.0) on
            // unfreeze.  Frame-step temporarily lifts the freeze for
            // one cockpit-tick cycle — see frame_step_apply.  The old
            // chara+0x394 bytepatch approach was wrong (audio bit, not
            // world pause); see plate on g_LuxBattle_VMFreezeByte.
            bool ff = m_freeze_frame.load();
            if (ImGui::Checkbox("Freeze frame", &ff))
            {
                m_freeze_frame.store(ff);
                // No explicit resolve/enable here — frame_step_apply
                // does the lazy enable on the next cockpit tick when
                // it sees freeze=true.
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Freeze the battle simulation in place.  World stops\n"
                "ticking; UMG/render keeps going so you can rotate the\n"
                "free-camera (with Ansel) or step through frames.\n\n"
                "Mechanism: forces SpeedControl's speedval to 0.  This\n"
                "is the same byte the engine itself uses for hitstop —\n"
                "every per-frame integrator (animation, opcode-stream,\n"
                "hit timing) reads dt=0 and halts.\n\n"
                "Stacks with Slow-mo (Freeze wins while held; releases\n"
                "back to the slider value).  Frame-step (button or F6)\n"
                "temporarily runs the world at 1x for one tick.");

            // Step-frame controls.  m_step_pending++ queues frames so
            // mashing the button (or holding F6) is lossless.  No
            // engine-state gating needed — the SpeedControl patches
            // are independent of battle context and work as soon as
            // they resolve, which the frame_step_apply driver does
            // lazily on first non-1.0 target.
            ImGui::BeginDisabled(!ff);
            if (ImGui::Button("Step 1 (F6)"))
            {
                m_step_pending.fetch_add(1);
            }
            ImGui::SameLine();
            static int s_step_n = 10;
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::InputInt("##stepn", &s_step_n, 0))
            {
                if (s_step_n < 1)   s_step_n = 1;
                if (s_step_n > 600) s_step_n = 600;
            }
            ImGui::SameLine();
            if (ImGui::Button("Step N"))
            {
                if (s_step_n > 0) m_step_pending.fetch_add(s_step_n);
            }
            ImGui::EndDisabled();

            if (!ff && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Enable Freeze frame first.");
            }

            // Status / debug line.
            if (m_speed_control.is_resolved())
            {
                if (const int q = m_step_pending.load(); q > 0)
                {
                    ImGui::TextDisabled("(stepping… %d frame%s queued)",
                                        q, q == 1 ? "" : "s");
                }
                else if (ff)
                {
                    ImGui::TextDisabled("(paused — F6 to step)");
                }
            }
            else if (ff)
            {
                ImGui::TextDisabled(
                    "(speed-control AOB did not resolve — check log)");
            }

            // --- Speed control (slow-motion / freeze) ------------------
            // Replaces the engine's master delta-time reads with a load
            // from a single user-controlled float.  Independent of the
            // Freeze-frame toggle above — Freeze gates the chara state
            // machine, this gates ALL dt-driven subsystems (animation,
            // hit timing, particles within the MoveVM scope).
            //
            // First click on the checkbox installs the patches lazily;
            // subsequent slider drags just live-update the float.  The
            // preset buttons use common analysis values; type any value
            // 0.0..2.0 in the slider for fine tuning.
            {
                bool sc_on = m_speed_enabled.load();
                if (ImGui::Checkbox("Slow-motion", &sc_on))
                {
                    m_speed_enabled.store(sc_on);
                    if (sc_on && !m_speed_control.is_enabled())
                    {
                        if (!m_speed_control.is_resolved())
                            m_speed_control.resolve();
                        // Push current slider value into speedval BEFORE
                        // enabling so the first frame after enable reads
                        // the right rate (default 1.0 = no-op).
                        m_speed_control.set_value(m_speed_value.load());
                        m_speed_control.enable();
                    }
                    else if (!sc_on)
                    {
                        m_speed_control.disable();
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Scale the simulation rate.  Hijacks 5 engine reads of\n"
                    "the master delta-time / time-dilation float and\n"
                    "redirects them to the slider value below.\n\n"
                    "Independent of Freeze frame — both stack cleanly.\n\n"
                    "Ported from somberness's CE 'Speed control v2' cheat.");

                // Slider only meaningful when the patches are live; we
                // still allow drag while off so the user can pre-set
                // their target value before flipping on.
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                float sv = m_speed_value.load();
                if (ImGui::SliderFloat("##speedval", &sv, 0.0f, 2.0f, "%.3fx"))
                {
                    if (sv < 0.0f) sv = 0.0f;
                    if (sv > 2.0f) sv = 2.0f;
                    m_speed_value.store(sv);
                    if (m_speed_control.is_enabled())
                        m_speed_control.set_value(sv);
                }

                // Preset buttons for common hitbox-analysis speeds.
                // Values match the CE author's hotkey presets plus a
                // 0.5 added for general "slow but watchable" use.
                struct Preset { const char* label; float value; };
                static const Preset kPresets[] = {
                    {"Freeze##sp",   0.0f },
                    {"0.001x##sp",   0.001f },
                    {"0.01x##sp",    0.01f },
                    {"0.1x##sp",     0.1f },
                    {"0.5x##sp",     0.5f },
                    {"1x##sp",       1.0f },
                };
                for (const auto& p : kPresets)
                {
                    if (ImGui::SmallButton(p.label))
                    {
                        m_speed_value.store(p.value);
                        if (m_speed_control.is_enabled())
                            m_speed_control.set_value(p.value);
                    }
                    ImGui::SameLine();
                }
                ImGui::NewLine();

                if (!m_speed_control.is_resolved() && sc_on)
                {
                    ImGui::TextDisabled(
                        "(speed-control AOBs did not resolve — check log)");
                }
                else if (m_speed_control.is_enabled())
                {
                    ImGui::TextDisabled("(active — speedval = %.3fx)",
                                        m_speed_value.load());
                }
            }

            // --- Hide characters (bytepatch, no flicker) ---------------
            // Inverts the engine's own visibility-compare instructions
            // inside ALuxBattleChara_SyncMoveStateVisibility — the
            // chara stays hidden through every move state including
            // critical edges and transformations that previously caused
            // 1-frame flickers.  See horselib/CharaInvis.hpp.
            bool hc = m_hide_chara.load();
            if (ImGui::Checkbox("Hide characters", &hc))
            {
                m_hide_chara.store(hc);
                m_chara_invis.set(hc);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Hide both characters' meshes (and attached components —\n"
                "hair tassels, skirts, accessories) while leaving\n"
                "simulation fully active.  Hitboxes are part of the\n"
                "gameplay skeleton, not the mesh, so they keep updating\n"
                "fine while the model is invisible.\n\n"
                "Implemented as two single-byte patches inside\n"
                "ALuxBattleChara_SyncMoveStateVisibility — flips the\n"
                "engine's own visibility-compare instructions so the\n"
                "renderer reads 'hidden' regardless of move-state\n"
                "writes.  No flicker on critical edges, supers, or\n"
                "transformations.\n\n"
                "Ported from somberness's CE 'Invisible' cheat.");

            if (!m_chara_invis.is_resolved() && hc)
            {
                ImGui::TextDisabled(
                    "(chara-invis AOB did not resolve — check log)");
            }

            // --- Suppress VFX ------------------------------------------
            // Bytepatch port of somberness's CE "VFX off" cheat.
            // Patches the engine's per-slot VFX-state writer to plant a
            // sentinel constant the renderer culls — effects never
            // become visible.  See horselib/VFXOff.hpp.
            bool sv = m_suppress_vfx.load();
            if (ImGui::Checkbox("Suppress VFX", &sv))
            {
                m_suppress_vfx.store(sv);
                m_vfx_off.set(sv);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Disable hit-effect / spark / particle VFX by patching\n"
                "the engine's per-frame VFX-slot writer to plant a\n"
                "sentinel value the renderer treats as culled.\n\n"
                "Strictly stronger than the previous DestroyAllVFx\n"
                "polling approach: effects don't get a 1-frame flash on\n"
                "first spawn, and there's no per-tick UFunction call.\n\n"
                "Toggle OFF to restore.  Runs independent of the F5\n"
                "overlay toggle.  Ported from somberness's CE table.");

            if (!m_vfx_off.is_resolved() && sv)
            {
                ImGui::TextDisabled(
                    "(VFX-off AOB did not resolve — check log)");
            }
        }

        // --- Hit-flash duration -----------------------------------------
        // The raw PerHurtboxReactionState signal is a ~1-frame pulse
        // (~16ms at 60fps) — too short to see.  This slider extends the
        // visible red flash by holding the "hot" state for N cockpit
        // ticks before fading.  0 = disable the sticky entirely (raw
        // 1-frame pulse only).
        //
        // Counted in COCKPIT TICKS, with the countdown gated on
        // SpeedControl::current_value_static() > 0 — so when "Freeze
        // frame" is on, the sticky pauses with the rest of the world
        // and the flash stays visible until the user unfreezes or
        // steps a frame.  Each F6 step decrements the counter by 1.
        //
        // 15 ticks ≈ 250ms at 60fps (the previous default).  60 ticks
        // = ~1 second of normal-speed gameplay, which is the slider
        // cap.  Slow-mo (speedval < 1) treats every cockpit tick as
        // a "decrement" tick — so the visible duration in wall-clock
        // seconds shrinks under slow-mo.  If you want a long-visible
        // flash to inspect during slow-mo, just bump this slider.
        ImGui::Spacing();
        ImGui::TextUnformatted("Hit-flash duration");
        {
            int frames = m_flash_frames.load();
            if (ImGui::SliderInt("frames##flashdur", &frames, 0, 60, "%d ticks"))
            {
                if (frames < 0)  frames = 0;
                if (frames > 60) frames = 60;
                m_flash_frames.store(frames);
                Horse::KHitWalker::setStickyFrames(frames);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "How long to hold the red 'just got hit' flash after\n"
                "the engine's PerHurtboxReactionState signal fires.\n"
                "0 = no sticky (1-frame pulse, usually invisible).\n\n"
                "Counted in cockpit ticks (≈ frames at 60fps).  The\n"
                "countdown PAUSES while Freeze frame is on, so the\n"
                "flash stays visible during inspection.  Each F6 step\n"
                "decrements the counter by 1 game frame's worth.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Render");
        {
            float t = m_thickness.load();
            if (ImGui::SliderFloat("Thickness", &t, 0.5f, 8.0f, "%.1f"))
                m_thickness.store(t);

            const char* slot_names[3] = {
                "Default (depth-tested)",
                "Persistent",
                "Foreground (on top)",
            };
            int slot_idx = static_cast<int>(m_slot.load());
            if (ImGui::Combo("LineBatcher slot", &slot_idx, slot_names, 3))
                m_slot.store(static_cast<Horse::LineBatcherSlot>(slot_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Which of UWorld's three ULineBatchComponents to append to.\n"
                    "  Default     — depth-tested, occluded by world geometry\n"
                    "  Persistent  — depth-tested, identical behaviour per-frame\n"
                    "  Foreground  — NO depth test, always on top (recommended)");
        }
    }
};

// ----------------------------------------------------------------------------
#define HORSE_MOD_API __declspec(dllexport)
extern "C"
{
    HORSE_MOD_API CppUserModBase* start_mod()                    { return new HorseMod(); }
    HORSE_MOD_API void             uninstall_mod(CppUserModBase* m) { delete m; }
}
