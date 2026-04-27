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
#include "horselib/ReplayWatchpoints.hpp"
#include "horselib/WorldPause.hpp"
#include "horselib/ReplayPlayerGate.hpp"
#include "horselib/BattleTimeFreeze.hpp"
#include "horselib/BattlePauseRequest.hpp"
// Horse::GameImGui replaces UE4SS_ENABLE_IMGUI().  It renders HorseMod's
// ImGui tab INSIDE the game's own DX11 swap chain via a PolyHook-vtable-
// swap detour on IDXGISwapChain::Present.  This keeps Steam overlay
// working (the detour chains through Steam's pre-existing hook) and
// eliminates the focus-stealing external "UE4SS Debugging Tools" window
// that breaks Shift+Tab for the user.
#include "horselib/GameImGui/GameImGui.hpp"

// Persists toggle / slider state between game sessions.  Loaded once in
// the ctor (before any atomic is first read for rendering), saved
// periodically via on_update, and saved a final time in the dtor for
// graceful shutdown.
#include "horselib/ModSettings.hpp"

// Captures + replays a custom (X, Y, Z + side) chara pose on training-
// mode position reset.  Wired via a UFunction post-hook on
// /Script/LuxorGame.LuxBattleManager:TrainingModePositionReset (see
// hookup further down in HorseMod's ctor / on_update).
#include "horselib/ResetOverride.hpp"

// PolyHook x64Detour on LuxBattleChara_SetStartPosition.  This is the
// canonical chokepoint for every chara-teleport path the engine takes,
// so we install it here and let the captured pose override fire
// regardless of which UFunction (or non-UFunction) chain triggered the
// reset.  Empirically required because the user's training-mode reset
// bind goes through a path that does NOT touch any of the
// BlueprintCallable UFunctions we hooked first.
#include "horselib/SetStartPositionHook.hpp"

// Modded-lobby battle-rule overrides (SlipOut + stubs).  See the
// file-header doc comment for the full rationale and the requirement
// that BOTH peers run HorseMod for any selected policy to work
// without desync.  Hooks the relevant runtime rule-gate UFunctions
// and overrides their return values when the user has selected the
// matching HorsePolicy.
#include "horselib/OnlineRules.hpp"

// PolyHook x64Detour on ULuxUIBattleLauncher::Start (image+0x5EEB50).
// This is the chokepoint for ALL 5 BattleRule overrides — the detour
// calls the appropriate Set<X>Mode setter on the launcher BEFORE the
// original Start runs, writing our values into the launcher's data-
// table cache.  The original then reads our values when it builds the
// per-match rule set.  Works for every rule regardless of whether the
// lobby Blueprint itself called the corresponding Set*Mode UFunction.
#include "horselib/LuxBattleLauncherStartHook.hpp"
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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

// ----------------------------------------------------------------------------
// Build-date formatter used in the ImGui window title.
//
// __DATE__ is the C preprocessor's compile-time date string, format
// "Mmm DD YYYY" — month is the abbreviated English name (Jan/Feb/...
// /Dec), DD is the day of the month (space-padded to 2 columns for
// single-digit days, e.g. "Apr  3 2026"), YYYY is the year.
//
// We reformat to "DD-Mmm-YYYY" so the date is unambiguously day-first
// — avoids the perennial US-vs-rest-of-world confusion between e.g.
// "04/05" meaning April 5th vs May 4th.  Using the abbreviated month
// name (Apr, May, Jun, ...) instead of a numeric month makes the
// ordering self-evident regardless of the reader's locale.
//
// One-shot init: compiled once into HorseMod.dll, never changes for
// the life of the process.  Cached in a static char buffer so we can
// hand back a stable const char* to ImGui::Begin (which uses the
// pointer's value as the window-identity hash).
static const char* horsemod_window_title()
{
    static char buf[64];
    static bool init = false;
    if (!init)
    {
        // __DATE__ guarantees:
        //   d[0..2]  = month abbreviation (3 letters)
        //   d[3]     = ' '
        //   d[4..5]  = day-of-month, space-padded
        //   d[6]     = ' '
        //   d[7..10] = 4-digit year
        const char* d = __DATE__;

        // Day: replace leading space with '0' so the output is always
        // zero-padded to 2 digits ("03-Apr-2026", not " 3-Apr-2026").
        const char d1 = (d[4] == ' ') ? '0' : d[4];
        const char d2 = d[5];

        std::snprintf(buf, sizeof(buf),
                      "HorseMod (Beta %c%c-%c%c%c-%c%c%c%c)",
                      d1, d2,                  // day
                      d[0], d[1], d[2],        // month abbreviation
                      d[7], d[8], d[9], d[10]); // year
        init = true;
    }
    return buf;
}

// ----------------------------------------------------------------------------
class HorseMod final : public CppUserModBase
{
private:
    // Static live-instance pointer so the cockpit hook lambda can safely
    // no-op after destruction (game thread could fire after Restart All).
    static inline std::atomic<HorseMod*> s_instance{nullptr};

    // ---- Overlay state ----
    std::atomic<bool> m_enabled{false};

    // Per-player visibility toggles.  Default on-launch layout is
    // "only P2's hurtboxes visible" — the most common starting point
    // for frame-data practice where P2 is the training dummy and you
    // want to see their incoming-damage volumes without the visual
    // noise of P1's own attacks / hurtboxes.  User flips the rest on
    // per-session from the Hitboxes tab.  Each flag indexed by
    // PlayerIndex (0 = P1, 1 = P2).
    std::atomic<bool> m_show_p1_hurt{false};
    std::atomic<bool> m_show_p1_atk {true};
    std::atomic<bool> m_show_p1_body{false};
    std::atomic<bool> m_show_p2_hurt{true};
    std::atomic<bool> m_show_p2_atk {true};
    std::atomic<bool> m_show_p2_body{false};

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

    // "Show all Hitboxes" (UI name) / "per-frame damage filter"
    // (engine meaning) — when this is FALSE (default), we apply the
    // engine's per-frame damage-active filter to attack nodes: only
    // nodes whose +0x17 slot bit is set in the chara's PER-FRAME
    // damage mask are drawn.  That's the same test the engine's
    // ResolveAttackVsHurtboxMask22 uses to decide whether overlap
    // should fire a hit — so default-false gives the cleanest
    // "hitboxes I care about during frame-data work" view: they
    // turn OFF during startup, ON during damage frames, OFF during
    // recovery.
    //
    // When the user ticks "Show all Hitboxes" (flag becomes TRUE),
    // the filter is disabled — every attack node draws regardless
    // of whether it's currently capable of damage.  Useful for
    // inspecting body-attached / passive attack volumes that the
    // frame filter would hide.
    //
    // Semantic-flip-from-legacy note: pre-rename this atomic was
    // called m_hide_not_per_frame_active with the opposite polarity
    // (true=filter-on).  We flipped both the name and the boolean
    // polarity so the variable reads match the UI checkbox label —
    // "show all" == true, "filter active" == false.  Everywhere
    // that used to check `if (m_hide_not_per_frame_active.load())`
    // now checks `if (!m_show_all_hitboxes.load())`.
    std::atomic<bool> m_show_all_hitboxes{false};

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
    // "Show unused hurtboxes" filter.  Controls whether we draw
    // hurtboxes whose +0x17 slot is outside the engine's classifier
    // iteration bound (chara+0x44494 capped at 22) — nodes that the
    // classifier will never read reactions from, even if they
    // physically overlap an attack.  See the old m_hide_unused_hurt
    // commentary (rewritten below) for the full engine-truth
    // derivation; the semantics here are:
    //
    //   false (default) = HIDE the unused hurtboxes (cleaner default
    //                     for frame-data work; matches what the UI
    //                     checkbox would show to a first-time user
    //                     who hasn't ticked the toggle).
    //   true            = SHOW them (useful for modders checking
    //                     "is my new per-move hurtbox being flagged
    //                     unaddressable?").
    //
    // This is polarity-flipped from the predecessor
    // m_hide_unused_hurt, which had TRUE = hide.  The new name +
    // polarity match the UI label "Show unused hurtboxes", and the
    // default changed from "show all" to "hide unused" — a cleaner
    // default for most users, who only want engine-readable
    // hurtboxes in the overlay.
    std::atomic<bool> m_show_unused_hurt{false};

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
    // Last `target` value pushed into m_speed_control.set_value() by
    // frame_step_apply().  Used to dedupe redundant writes when the
    // requested speedval doesn't change (perf audit, 2026-04).  Init
    // and reset-on-disable to NaN so the first write of any active
    // span is always forced (NaN != anything is always true).  Read
    // and written exclusively from the cockpit-tick caller — no
    // atomic needed.
    float m_last_speed_target =
        std::numeric_limits<float>::quiet_NaN();

    // ---- Replay-state watchpoints (diagnostic) ------------------------------
    // Hardware-data-breakpoint logger that records every write to the
    // replay-state cursor fields, with the writer's RIP.  Built to find
    // the residual freeze-drift leak after sites 1-16 didn't fully
    // freeze replay watching.  See horselib/ReplayWatchpoints.hpp.
    //
    // OFF by default — incurs CPU exception per cursor write when ON.
    // User toggles via the ImGui debug surface; stays off across sessions.
    Horse::ReplayWatchpoints m_replay_watchpoints{};
    std::atomic<bool>        m_replay_watch_enabled{false};

    // ---- SC6 NATIVE VM-FREEZE BYTE driver state ----------------------------
    // Tracks whether HorseMod has currently SET the native freeze byte at
    // imageBase + 0x4862D0 (g_LuxBattle_VMFreezeRecord.bVMFreezeByte).
    //
    // Used by frame_step_apply() to:
    //   * Skip touching the byte entirely on the steady-state "freeze
    //     never requested" path (= byte should stay 0, no need to
    //     re-check the page each frame).
    //   * Avoid stomping on SC6's OWN hit-stop / cinematic freeze writes
    //     (when bVMFreezeByte is non-zero because SC6 set it, we don't
    //     want to clear it).
    //   * Recover gracefully from a SEH fault on the byte access by
    //     resetting the flag and falling back to the per-function bare-
    //     RET sites (sites 1..16).
    std::atomic<bool>        m_vm_freeze_byte_we_set{false};

    // ---- UE4 WORLD-PAUSE driver state (2026-04-27, this session) -----------
    // Engages UE4's STANDARD pause flag (AWorldSettings::Pauser) during
    // HorseMod freeze.  This is the ONLY way to halt UDemoNetDriver — the
    // UE4 standard match-replay system that SC6 uses (confirmed via 142
    // DemoNetDriver source-path strings in the binary).
    //
    // Sites 1-22 catch SC6's chara/BM Actor::Tick chains, but UDemoNetDriver
    // runs BEFORE actor ticks in UWorld::Tick — its TickFlush replays
    // packets and writes replicated state directly onto actors.  Setting
    // bGamePaused via UGameplayStatics::SetGamePaused makes
    // UDemoNetDriver::TickFlush early-return at the standard UE4 hook
    // point.  See horselib/WorldPause.hpp for full architecture rationale.
    //
    // Composes with VMFreezeByte: VMFreezeByte halts MoveVM math (positions,
    // animations); WorldPause halts the demo driver (input replay).  Both
    // engage on freeze, both release on unfreeze.  The two layers cover
    // the actor-tick-driven AND world-tick-driven systems respectively.
    Horse::WorldPause        m_world_pause{};

    // ---- BM->BattleReplayPlayer TICK GATE (2026-04-27, this session) ----
    // Disables the BM->BattleReplayPlayer actor's tick during HorseMod
    // freeze.  This is the MATCH-REPLAY playback driver actor — stored
    // at BM+0x488 as a reflected ObjectProperty (verified via Ghidra
    // analysis of Z_Construct_UClass_ALuxBattleManager_Statics).
    //
    // Why a separate gate: WorldPause sets bGamePaused, which UE4
    // standard actors honour.  But SC6's BattleReplayPlayer may have
    // bTickEvenWhenPaused == true (legacy SC6 actor flag), making it
    // continue to tick during world pause.  This gate calls
    // SetActorTickEnabled(false) directly on the actor instance —
    // bypassing the pause-flag check entirely.
    //
    // Composes with sites 1-22 + VMFreezeByte + WorldPause.  Each
    // layer catches a different leak path:
    //   - Sites 1-22:    chara/BM internal Tick chains
    //   - VMFreezeByte:  MoveVM math
    //   - WorldPause:    UE4-pause-respecting subsystems (e.g. demo driver)
    //   - ReplayPlayerGate: the specific BattleReplayPlayer actor that
    //                       drives match-replay playback regardless of
    //                       pause flags.
    Horse::ReplayPlayerGate  m_replay_player_gate{};

    // ---- BATTLE-TIME (round timer) FREEZE (2026-04-27, this session) ----
    // Sets BM->bForciblyStopBattleTime = true during HorseMod freeze.
    // SC6's OWN flag for halting the round-timer countdown — engine
    // already has logic respecting this flag (literally named "Forcibly
    // Stop Battle Time").
    //
    // ROOT CAUSE (user clarification 2026-04-27):
    //   "If I pause the game for long enough both characters just stand
    //    still until the time runs out"
    //
    // Sites 1-16 + VMFreezeByte correctly halt chara state.  But the
    // BattleTimeManager (BM+0x4F8) keeps ticking the round timer.  When
    // it hits 0 the round ends — terminating the replay.  Earlier
    // "inputs don't match" symptom was downstream of this — the replay
    // was being CUT SHORT before its recorded inputs could finish.
    Horse::BattleTimeFreeze  m_battle_time_freeze{};

    // ---- BATTLE PAUSE REQUEST (THE BREAKTHROUGH) ---------------------------
    // Calls ULuxBattleFunctionLibrary::SetBattlePause via UE4SS reflection.
    // This is the EXACT UFunction SC6's in-game pause menu uses to pause
    // a replay (verified via Ghidra: registered at FUN_140936190, signature
    // SetBattlePause(bool bPause, byte inType, UObject* WorldContext)).
    //
    // User report 2026-04-27: "the pause menu can pause [the replay], it
    // also changes the playback icon to paused".  The engine's NATIVE
    // pause mechanism works correctly; HorseMod just needs to invoke it
    // via the same UFunction the menu does.
    //
    // See horselib/BattlePauseRequest.hpp for full architecture rationale.
    Horse::BattlePauseRequest m_battle_pause_request{};

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

    std::atomic<float> m_thickness{1.5f};

    // Per-feature line-batcher slot.  Hitboxes (Attack list) draw via
    // m_backend_hit; hurtboxes + body (Hurtbox + Body lists) draw via
    // m_backend_hurt.  Splitting them lets the user trail hurtboxes
    // (Persistent) while keeping hitboxes always-on-top (Foreground).
    // Both default to Foreground; Persistent is unsuitable for hitboxes
    // because the trail accumulates faster than the eye can disambiguate.
    std::atomic<Horse::LineBatcherSlot> m_slot_hit {Horse::LineBatcherSlot::Foreground};
    std::atomic<Horse::LineBatcherSlot> m_slot_hurt{Horse::LineBatcherSlot::Foreground};

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

    // (Secondary attack-role filter / shouldShowAttackRole was removed
    // 2026-04 along with the UI for it.  Strike vs Throw partitioning
    // turned out to be more noise than signal for practical hitbox
    // inspection — users just want "show all attack volumes" or
    // "show none," which the master Attacks per-player checkbox
    // already covers.  If you want them back, the engine split is
    // documented at KHitAttackRole + the classifier at
    // LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100.)

    // ==================================================================
    // Settings persistence — file-backed via Horse::ModSettings.
    // ==================================================================
    //
    // Load: call once from the ctor BEFORE any render path reads an
    // atomic.  Reads <mod_folder>/settings.cfg, populates each atomic
    // from its persisted value, falls back to the compiled-in default
    // argument when the key is missing (fresh install, or we added a
    // new setting after the file was written).
    //
    // Save: sync every persisted atomic back into the ModSettings
    // map, then ModSettings::save_if_dirty() does the actual disk
    // write (only if something changed since the last save).  Called
    // periodically from on_update (every ~120 frames ≈ 2s at 60 FPS)
    // so slider drags don't spam the disk, and once more from the
    // dtor so the final state lands on disk on graceful shutdown.
    //
    // What we DON'T persist: runtime state (m_update_calls, hook
    // bookkeeping), transient toggles (m_freeze_frame, m_step_pending,
    // overlay visibility — user wants overlay hidden on launch
    // regardless), diagnostic-only flags.
    //
    // Key-naming convention: snake_case, descriptive over short, no
    // prefix.  Old/renamed settings are safe to leave in the file —
    // ModSettings preserves unknown keys across saves.
    void load_persisted_settings()
    {
        auto& S = Horse::ModSettings::instance();
        S.load();

        // --- Hitboxes tab -----------------------------------------
        m_enabled                .store(S.get_bool ("master_overlay",        false));
        m_show_p1_hurt           .store(S.get_bool ("show_p1_hurt",          false));
        m_show_p1_atk            .store(S.get_bool ("show_p1_hitboxes",      true ));
        m_show_p1_body           .store(S.get_bool ("show_p1_body",          false));
        m_show_p2_hurt           .store(S.get_bool ("show_p2_hurt",          true ));
        m_show_p2_atk            .store(S.get_bool ("show_p2_hitboxes",      true ));
        m_show_p2_body           .store(S.get_bool ("show_p2_body",          false));
        m_hide_not_damage_active .store(S.get_bool ("damage_active_only",    false));
        m_show_all_hitboxes      .store(S.get_bool ("show_all_hitboxes",     false));
        m_show_unused_hurt       .store(S.get_bool ("show_unused_hurtboxes", false));
        m_flash_frames           .store(S.get_int  ("hit_flash_frames",      15   ));
        m_thickness              .store(S.get_float("thickness",             1.5f ));
        // Per-feature line-batcher slot.  Hitboxes default to Foreground
        // (always-on-top, the only sensible choice — Persistent would
        // pile up unreadable trails).  Hurtboxes also default to
        // Foreground but the user can flip them to Persistent to trace
        // a chara's hurtbox path through a move.  The legacy single
        // key "line_batcher_slot" from before the split is silently
        // ignored — old enum values aren't valid in the new 2-entry
        // enum and the user has to pick again from the new combos.
        m_slot_hit .store(static_cast<Horse::LineBatcherSlot>(
            S.get_int("line_batcher_slot_hit",
                      static_cast<int>(Horse::LineBatcherSlot::Foreground))));
        m_slot_hurt.store(static_cast<Horse::LineBatcherSlot>(
            S.get_int("line_batcher_slot_hurt",
                      static_cast<int>(Horse::LineBatcherSlot::Foreground))));

        // --- Camera tab -------------------------------------------
        m_ansel_always_allowed   .store(S.get_bool ("ansel_always_allowed",  false));
        m_lock_camera            .store(S.get_bool ("lock_camera",           false));
        m_free_camera.move_speed() = S.get_float("free_camera_move_speed", 20.0f);
        m_free_camera.look_speed() = S.get_float("free_camera_look_speed",  1.5f);
        m_free_camera.fov_deg()    = S.get_float("free_camera_fov",        70.0f);

        // --- Time tab ---------------------------------------------
        m_speed_enabled          .store(S.get_bool ("slow_motion_enabled",   false));
        m_speed_value            .store(S.get_float("slow_motion_value",     1.0f ));

        // --- General tab ------------------------------------------
        m_hide_weapons           .store(S.get_bool ("hide_weapons",          false));
        m_hide_chara             .store(S.get_bool ("hide_characters",       false));
        m_suppress_vfx           .store(S.get_bool ("suppress_vfx",          false));

        // --- Reset override -----------------------------------------
        // Captured pose persists across reboots so the user can resume
        // training from the same custom starting position.  The toggle
        // itself does NOT persist — it's deliberately reset to OFF on
        // every game start so a stale capture from a previous session
        // can't surprise the user with an unexpected teleport on the
        // first reset bind they press.  The user has to consciously
        // re-enable it to opt in.
        {
            auto& ro = Horse::ResetOverride::instance();
            ro.set_enabled(false);
            for (int pi = 0; pi < 2; ++pi)
            {
                Horse::ResetOverride::FCharaPose p{};
                std::string base = "reset_override_p";
                base += static_cast<char>('1' + pi);
                p.has = S.get_bool((base + "_has").c_str(), false);
                if (!p.has) continue;
                p.pos_x     = S.get_float((base + "_x").c_str(),    0.0f);
                p.pos_y     = S.get_float((base + "_y").c_str(),    0.0f);
                p.pos_z     = S.get_float((base + "_z").c_str(),    0.0f);
                p.side_flag = static_cast<uint8_t>(
                    S.get_int((base + "_side").c_str(),    0));
                ro.set_pose(pi, p);
            }
        }

        // Persisted HorseMod online policy.  Defaults to Vanilla so a
        // first-launch user with the mod installed gets vanilla
        // multiplayer behaviour; they have to consciously pick a
        // policy from the Online section in the General tab.
        Horse::OnlineRules::instance().set_policy(
            static_cast<Horse::HorsePolicy>(
                S.get_int("online_policy",
                    static_cast<int>(Horse::HorsePolicy::Vanilla))));
    }

    // Mirror every persisted atomic into the ModSettings map, then ask
    // ModSettings to write the file if anything changed since the
    // last save.  Set() calls diff internally, so idempotent calls on
    // unchanged values are O(map-lookup) and don't touch the dirty
    // flag — cheap to call every on_update tick.
    void save_persisted_settings()
    {
        auto& S = Horse::ModSettings::instance();

        // Hitboxes tab
        S.set("master_overlay",        m_enabled.load());
        S.set("show_p1_hurt",          m_show_p1_hurt.load());
        S.set("show_p1_hitboxes",      m_show_p1_atk.load());
        S.set("show_p1_body",          m_show_p1_body.load());
        S.set("show_p2_hurt",          m_show_p2_hurt.load());
        S.set("show_p2_hitboxes",      m_show_p2_atk.load());
        S.set("show_p2_body",          m_show_p2_body.load());
        S.set("damage_active_only",    m_hide_not_damage_active.load());
        S.set("show_all_hitboxes",     m_show_all_hitboxes.load());
        S.set("show_unused_hurtboxes", m_show_unused_hurt.load());
        S.set("hit_flash_frames",      m_flash_frames.load());
        S.set("thickness",             m_thickness.load());
        S.set("line_batcher_slot_hit",  static_cast<int>(m_slot_hit.load()));
        S.set("line_batcher_slot_hurt", static_cast<int>(m_slot_hurt.load()));

        // Camera tab
        S.set("ansel_always_allowed",  m_ansel_always_allowed.load());
        S.set("lock_camera",           m_lock_camera.load());
        S.set("free_camera_move_speed", m_free_camera.move_speed());
        S.set("free_camera_look_speed", m_free_camera.look_speed());
        S.set("free_camera_fov",       m_free_camera.fov_deg());

        // Time tab
        S.set("slow_motion_enabled",   m_speed_enabled.load());
        S.set("slow_motion_value",     m_speed_value.load());

        // General tab
        S.set("hide_weapons",          m_hide_weapons.load());
        S.set("hide_characters",       m_hide_chara.load());
        S.set("suppress_vfx",          m_suppress_vfx.load());

        // --- Reset override ----------------------------------------
        // The toggle is deliberately NOT persisted — see the matching
        // load_persisted_settings block for the rationale (start each
        // session with the override OFF; user must opt in).  We still
        // persist the captured pose so a previously-set custom spawn
        // is one click away.
        {
            auto& ro = Horse::ResetOverride::instance();
            for (int pi = 0; pi < 2; ++pi)
            {
                const auto p = ro.get_pose(pi);
                std::string base = "reset_override_p";
                base += static_cast<char>('1' + pi);
                S.set((base + "_has").c_str(),  p.has);
                S.set((base + "_x").c_str(),    p.pos_x);
                S.set((base + "_y").c_str(),    p.pos_y);
                S.set((base + "_z").c_str(),    p.pos_z);
                S.set((base + "_side").c_str(), static_cast<int>(p.side_flag));
            }
        }

        // HorseMod online policy persists across reboots so the user's
        // chosen modded-lobby ruleset survives a restart.  Unlike the
        // reset-override toggle, this one IS persistent — it's a
        // long-lived "what kind of online matches do I want" pref,
        // not a session-scoped behaviour.
        S.set("online_policy",
              static_cast<int>(Horse::OnlineRules::instance().current_policy()));

        S.save_if_dirty();
    }

    // ---- Hook / backend ----
    bool                         m_hook_registered = false;
    std::pair<int32_t, int32_t>  m_hook_ids{};
    StringType                   m_hook_path;
    int                          m_poll_counter = 0;
    int                          m_update_calls = 0;
    int                          m_diag_tick    = 0;

    // Reset-override UFunction hook bookkeeping.
    //
    // We don't actually know which UFunction the user's reset bind invokes —
    // SC6 has at least four candidate paths that all eventually run the
    // training-mode position-reset chain:
    //
    //   /Script/LuxorGame.LuxBattleManager:TrainingModePositionReset
    //   /Script/LuxorGame.LuxBattleManager:RestartBattle
    //   /Script/LuxorGame.LuxBattleManager:RestartBattleImmediately
    //   /Script/LuxorGame.LuxBattleFunctionLibrary:RequestTrainingModeBattleReset
    //
    // The previous attempt hooked only TrainingModePositionReset and the
    // post-hook never fired — the user's bind takes a different path.
    // Rather than guess, we register hooks on ALL of them and let the one
    // that fires identify itself in the log via the custom_data ptr.
    // Multiple firings are harmless: apply_to_charas() is idempotent
    // (writes the same captured pose to the same chara struct).
    //
    // Each slot is registered independently as soon as its containing class
    // is loaded; on_update polls until all slots are registered.  Failed
    // class lookups (class not yet loaded into UObject array) just retry
    // next tick, same way try_register_cockpit_hook works.
    struct ResetHookSlot
    {
        StringType class_path;          // gate: StaticFindObject of this UClass must succeed
        StringType func_path;           // RegisterHook key + custom_data tag + UnregisterHook key
        bool       registered = false;
        std::pair<int32_t, int32_t> ids{};
    };
    std::vector<ResetHookSlot>   m_reset_slots;

    // Tick counter for throttled settings persistence.  on_update
    // bumps this every frame and calls save_persisted_settings()
    // every kSaveEveryNFrames — batching slider-drag updates into
    // one disk write per ~2 seconds.  See ctor for constant value.
    int                          m_save_tick    = 0;
    static constexpr int         kSaveEveryNFrames = 120;

    Horse::Lux                 m_lux;

    // Two backends so the hitbox slot and the hurtbox/body slot can
    // independently target different UWorld batchers.  Each owns its
    // own UWorld+LBC pointer caches; both prime each frame from the
    // same pivot (the cockpit) but resolve to different LBC offsets
    // (UWorld+0x48 vs UWorld+0x50) per their slot setting.
    Horse::LineBatcherBackend  m_backend_hit;
    Horse::LineBatcherBackend  m_backend_hurt;

    // In-game ImGui overlay token (see on_unreal_init / dtor).  Non-zero
    // after Horse::GameImGui::register_tab returns; passed to
    // unregister_tab on teardown.
    uint64_t m_gameimgui_tab_token = 0;

    // Nav-bootstrap flag: set to true when the overlay transitions from
    // hidden→shown, consumed by render_hitboxes_tab which then calls
    // ImGui::SetKeyboardFocusHere() on the master F5 toggle.  Forces
    // ImGui to assign a NavId and activate the nav highlight without
    // the user having to press Square/X first.  Without this, ImGui
    // shows the window focused but has no NavId to highlight, so the
    // D-pad appears to do nothing until a "menu" key press kicks nav
    // into gear by side effect.
    bool m_nav_bootstrap_pending = false;

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

        // Load persisted settings BEFORE any render path can observe
        // an atomic.  If settings.cfg is missing (first-run) each
        // get_* call returns its default argument, matching the
        // compiled-in defaults — functionally identical to the
        // pre-persistence behaviour on a clean install.
        load_persisted_settings();

        // Populate reset-hook candidate list.  Registration is attempted
        // (and retried) from on_update once each slot's containing class
        // is loaded.  See the ResetHookSlot doc-comment for why we hook
        // multiple paths instead of just one.
        //
        // Class-path verification (cross-checked against UHTHeaderDump):
        //   LuxBattleManager : TrainingModePositionReset, RestartBattle,
        //                      RestartBattleImmediately
        //   LuxBattleGameMode: RequestTrainingModeBattleReset(side)
        // Earlier builds put RequestTrainingModeBattleReset on
        // LuxBattleFunctionLibrary — that's wrong and crashed startup
        // because UE4SS's RegisterHook(StringType) dereferences the
        // result of StaticFindObject<UFunction*> without a null check
        // when the path doesn't resolve.
        m_reset_slots = {
            { STR("/Script/LuxorGame.LuxBattleManager"),
              STR("/Script/LuxorGame.LuxBattleManager:TrainingModePositionReset") },
            { STR("/Script/LuxorGame.LuxBattleManager"),
              STR("/Script/LuxorGame.LuxBattleManager:RestartBattle") },
            { STR("/Script/LuxorGame.LuxBattleManager"),
              STR("/Script/LuxorGame.LuxBattleManager:RestartBattleImmediately") },
            { STR("/Script/LuxorGame.LuxBattleGameMode"),
              STR("/Script/LuxorGame.LuxBattleGameMode:RequestTrainingModeBattleReset") },
        };

        Input::ModifierKeyArray no_mods{};
        no_mods.fill(Input::ModifierKey::MOD_KEY_START_OF_ENUM);

        register_keydown_event(Input::Key::F5, no_mods, [this]() {
            bool s = !m_enabled.load();
            m_enabled.store(s);
            Output::send<LogLevel::Verbose>(STR("[HorseMod] overlay {}\n"),
                s ? STR("ON") : STR("OFF"));
            if (!s)
            {
                // Hide on both backends so neither leaves stray lines
                // when the user toggles the overlay off.
                m_backend_hit.hideAll();
                m_backend_hurt.hideAll();
            }
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

        // NOTE: the old UE4SS register_tab(...) call lived here.  We no
        // longer register our tab with UE4SS's external GUI — the tab
        // is now hosted in-game via Horse::GameImGui (see on_unreal_init
        // below).  Removing the UE4SS registration means the HorseMod
        // tab no longer appears in the separate "UE4SS Debugging Tools"
        // window; it draws directly into the SC6 window instead.

        // F2 toggles the in-game ImGui overlay visibility.
        //
        // Why UE4SS's register_keydown_event (and NOT a WndProc hook
        // inside GameImGui): SC6 registers RawInput with the
        // RIDEV_NOLEGACY flag, which suppresses WM_KEYDOWN on the
        // game HWND at the OS level.  UE4SS's keydown_event uses a
        // WH_KEYBOARD_LL low-level hook underneath, which is the
        // only reliable way to catch keys past NOLEGACY — this is
        // the same trick Horse::LowLevelKeyInput uses for F5/F6/F7.
        //
        // Back/Select on the gamepad also toggles the overlay; that
        // half is wired inside horselib/GameImGui/GamepadInput.hpp's
        // BACK-button edge detector.
        register_keydown_event(Input::Key::F2, no_mods, []() {
            const bool v = !Horse::GameImGui::visible();
            Horse::GameImGui::set_visible(v);
            Output::send<LogLevel::Default>(
                STR("[HorseMod] F2 pressed — overlay {}\n"),
                v ? STR("SHOWN") : STR("HIDDEN"));
        });

        s_instance.store(this);
        Output::send<LogLevel::Verbose>(
            STR("[HorseMod] ctor v0.10.0 (KHit walker)\n"));
    }

    ~HorseMod() override
    {
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor ENTER\n"));

        // Final settings save — catches anything the periodic
        // on_update save would have missed in the last sub-2s window
        // before shutdown.  Crashes lose at most the most-recent
        // ~2-second window of changes; graceful exits lose nothing.
        save_persisted_settings();

        // Zero instance pointer FIRST so any in-flight hook sees null.
        s_instance.store(nullptr);

        // Tear down the in-game ImGui overlay BEFORE unregistering the
        // cockpit hook.  Order matters only loosely here, but calling
        // shutdown() synchronises: it unHooks the DXGI vtable (Present
        // calls immediately revert to whatever was installed before
        // us — usually Steam's hook directly), restores the game's
        // WndProc, and releases our D3D11 RTV.  After this returns no
        // further render_tab_impl calls can happen from our hook.
        if (m_gameimgui_tab_token)
        {
            Horse::GameImGui::unregister_tab(m_gameimgui_tab_token);
            m_gameimgui_tab_token = 0;
        }
        Horse::GameImGui::shutdown();

        if (m_hook_registered && !m_hook_path.empty())
        {
            UObjectGlobals::UnregisterHook(m_hook_path, m_hook_ids);
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor unregistered cockpit hook pre={} post={}\n"),
                m_hook_ids.first, m_hook_ids.second);
        }
        for (auto& slot : m_reset_slots)
        {
            if (!slot.registered) continue;
            UObjectGlobals::UnregisterHook(slot.func_path, slot.ids);
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor unregistered reset hook {} pre={} post={}\n"),
                slot.func_path, slot.ids.first, slot.ids.second);
            slot.registered = false;
        }

        // Tear down the C++-level SetStartPosition detour cleanly so the
        // reloaded mod (e.g. dev iteration) doesn't double-hook on its
        // next install.  Idempotent if install never succeeded.
        Horse::SetStartPositionHook::instance().uninstall();

        // Tear down all online-rules UFunction hooks (SlipOut + any
        // future implemented rules).  Idempotent.
        Horse::OnlineRules::instance().uninstall_hooks();

        // Tear down the launcher-Start PolyHook detour cleanly so a
        // hot-reload of the mod doesn't double-hook on its next install.
        // Idempotent if install never succeeded.
        Horse::LuxBattleLauncherStartHook::instance().uninstall();

        // Release the world pause if HorseMod engaged it.  Defensive
        // cleanup even though frame_step_apply no longer engages it —
        // earlier session might have, and leaving the world paused
        // forever after hot-unload is the worst possible UX.
        if (m_world_pause.we_engaged())
        {
            (void)m_world_pause.release();
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor released world-pause flag\n"));
        }

        // Release the replay-player tick gate (defensive — same rationale).
        if (m_replay_player_gate.we_engaged())
        {
            (void)m_replay_player_gate.release();
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor released BattleReplayPlayer tick gate\n"));
        }

        // CRITICAL: release the bForciblyStopBattleTime flag if engaged.
        // Leaving this stuck at true would freeze the round timer for
        // EVERY subsequent battle until SC6 is restarted — worse than
        // the bug we're fixing.  Idempotent: no-op if not engaged.
        if (m_battle_time_freeze.we_engaged())
        {
            (void)m_battle_time_freeze.release();
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor released bForciblyStopBattleTime flag\n"));
        }

        // CRITICAL: release the battle pause if engaged.  Leaving the
        // game in a paused state on hot-unload would force the user to
        // open the in-game pause menu themselves to recover.
        if (m_battle_pause_request.we_engaged())
        {
            (void)m_battle_pause_request.release();
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor released SetBattlePause request\n"));
        }

        // m_cam_lock will restore any active patches via its own dtor
        // when our member destruction runs after this body returns.
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor EXIT\n"));
    }

    // No on_ui_init() override — UE4SS_ENABLE_IMGUI() set up the shared
    // ImGui context + allocator for the UE4SS external window.  We host
    // our own ImGui context inside Horse::GameImGui (see on_unreal_init),
    // so we skip UE4SS's wiring entirely.  The allocator remains the
    // default (malloc/free via ImGui), which is fine for an isolated
    // context.

    auto on_unreal_init() -> void override
    {
        // -----------------------------------------------------------
        // In-game ImGui overlay bring-up.
        //
        // Ordering rationale: on_unreal_init runs after UE has
        // constructed its D3D device + swap chain and, importantly,
        // after Steam's gameoverlayrenderer64.dll has installed its
        // Present hook during the game's initial DLL loading.  Our
        // PresentHook::install() reads the DXGI vtable and performs a
        // PolyHook VFuncSwap — chaining ON TOP of Steam's hook rather
        // than fighting it.  The actual per-frame rendering and
        // WndProc attachment both kick in lazily on the first hooked
        // Present() (see GameImGui.hpp for the bootstrap callback).
        // -----------------------------------------------------------
        if (!Horse::GameImGui::initialize())
        {
            Output::send<LogLevel::Error>(
                STR("[HorseMod] Horse::GameImGui::initialize() failed; "
                    "the in-game ImGui overlay will not appear.\n"));
        }
        m_gameimgui_tab_token = Horse::GameImGui::register_tab(
            L"HorseMod", [this] { this->render_tab_impl(); });

        // Resolve SC6 native function RVAs now that the game image is loaded:
        //   ALuxBattleChara_GetBoneTransformForPose @ image + 0x462760
        //   LuxSkeletalBoneIndex_Remap              @ image + 0x898140
        //   LuxBattleChara_SetStartPosition         @ image + 0x301E60
        // These are used by KHitWalker to transform KHitArea OBBs into
        // world space via the owning chara's bone pose, and by
        // SetStartPositionHook to override the chara teleport target.
        Horse::NativeBinding::resolve();

        // Install the C++-level chara-teleport hook.  This is the
        // workhorse for the "Override reset position" feature: every
        // engine reset path (round intro, training-mode reset bind,
        // RestartBattle, ResetBothCharaPositionsAndFacing, ...) funnels
        // through LuxBattleChara_SetStartPosition, so a single PolyHook
        // x64Detour there catches every trigger including the user's
        // raw-input training-reset bind that bypasses the BlueprintCallable
        // UFunction layer.  See horselib/SetStartPositionHook.hpp for
        // the full design rationale.
        Horse::SetStartPositionHook::instance().install();

        // Install the C++-level launcher-Start hook.  This is the
        // chokepoint for the Online Rules feature: the launcher's Start
        // method reads the data-table cache and applies all per-match
        // rules; we hook it to write our desired BattleRule.<X> values
        // into that cache right before the original runs.  Works for
        // SlipOut / NoRingOut / EndlessMode / DamageUp / BlowUp uniformly
        // and doesn't depend on whether the lobby Blueprint calls the
        // corresponding Set*Mode UFunctions.
        Horse::LuxBattleLauncherStartHook::instance().install();

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
        // Throttled settings persistence.  Runs every frame so we
        // catch changes regardless of hook-registration state (the
        // early-return below would otherwise skip it after the
        // cockpit hook is registered).  save_persisted_settings
        // fills the ModSettings map and asks it to save_if_dirty;
        // unchanged values are an O(map-lookup) no-op inside set(),
        // so the only actual disk I/O happens when a user toggled
        // something since the last save.
        if (++m_save_tick >= kSaveEveryNFrames)
        {
            m_save_tick = 0;
            save_persisted_settings();
        }

        const bool all_reset_registered = std::all_of(
            m_reset_slots.begin(), m_reset_slots.end(),
            [](const ResetHookSlot& s) { return s.registered; });
        const bool online_rules_installed =
            Horse::OnlineRules::instance().hooks_installed();
        if (m_hook_registered && all_reset_registered && online_rules_installed)
            return;
        if (++m_poll_counter < 60) return;
        m_poll_counter = 0;
        if (!m_hook_registered)        try_register_cockpit_hook();
        if (!all_reset_registered)     try_register_reset_hooks();
        if (!online_rules_installed)
            Horse::OnlineRules::instance().try_install_hooks();
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

    // Register a post-hook on each reset-related UFunction in m_reset_slots.
    //
    // Each post-hook fires AFTER the engine has run the round-intro position
    // chain (PositionCharasByRoundConfig -> PositionCharasSymmetrically ->
    // LuxBattleChara_SetStartPosition) for that path — the right spot to
    // overwrite the chara pose with the user's captured override.
    //
    // Multi-path rationale: the user's reset bind goes through a UFunction
    // we can't determine statically (they may have rebound it; SC6's
    // training-mode UI may dispatch via a different entry point depending
    // on context).  We register on every plausible candidate and the one
    // that fires logs its identity via the custom_data tag — both for our
    // diagnosis here and for the user to see in UE4SS.log.
    //
    // Each slot's class lookup gates that slot independently — failed
    // lookups (class not yet loaded) just retry next poll tick, same way
    // try_register_cockpit_hook does.
    void try_register_reset_hooks()
    {
        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {};
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void* custom_data) {
                // Identify which path fired via the tag we passed at
                // registration time (the slot's func_path c_str()).
                const wchar_t* path = static_cast<const wchar_t*>(custom_data);
                Output::send<LogLevel::Default>(
                    STR("[HorseMod] reset hook fired: {}\n"),
                    path ? path : STR("(unknown path)"));

                // Apply the captured pose.  Idempotent if multiple hooks
                // fire on the same reset (engine may chain through more
                // than one of these UFunctions for a single user press).
                Horse::ResetOverride::instance().apply_to_charas();
            };

        for (auto& slot : m_reset_slots)
        {
            if (slot.registered) continue;

            UClass* klass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, slot.class_path);
            if (!klass)
            {
                // Class not yet registered — try again next poll tick.
                continue;
            }

            // CRITICAL: also verify the UFunction exists before calling
            // RegisterHook(path).  UE4SS's path-overload of RegisterHook
            // (UObjectGlobals.cpp:859) calls StaticFindObject<UFunction*>
            // and then immediately dereferences the result via
            // Function->GetFunc() — so a null-result (function-not-found)
            // crashes the game with a null deref.
            //
            // Pre-checking here means a wrong/typo'd path just logs a
            // warning and skips that slot; the rest of the mod loads
            // unscathed.  We only log once (slot stays unregistered but
            // we mark it so we don't retry a known-bad path forever).
            UFunction* fn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, slot.func_path);
            if (!fn)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] Reset-override hook SKIPPED: UFunction "
                        "'{}' not found on class '{}' — typo or wrong class? "
                        "Will retry on next poll tick.\n"),
                    slot.func_path, slot.class_path);
                continue;
            }

            // c_str() is stable for the lifetime of the wstring, which
            // outlives the hook (m_reset_slots vector is never reassigned
            // after ctor population).  Pass it as custom_data so the
            // post-hook can identify which path triggered it.
            void* tag = const_cast<wchar_t*>(slot.func_path.c_str());

            slot.ids = UObjectGlobals::RegisterHook(
                slot.func_path, pre_cb, post_cb, tag);
            slot.registered = true;
            Output::send<LogLevel::Default>(
                STR("[HorseMod] Reset-override hook registered: {} (pre={} post={})\n"),
                slot.func_path, slot.ids.first, slot.ids.second);
        }
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
            // Skip the codecave write when the requested speedval is
            // unchanged from last tick (perf audit, 2026-04).  Steady-
            // state freeze (target=0) and steady-state slow-mo at a
            // fixed slider value would otherwise re-write the same
            // float ~60×/s.  Cheap individually but cumulative noise.
            // First tick after enable() carries m_last_speed_target =
            // NaN, and `NaN != target` is always true, so the first
            // write is forced.
            if (target != m_last_speed_target)
            {
                m_speed_control.set_value(target);
                m_last_speed_target = target;
            }
        }
        else
        {
            if (m_speed_control.is_enabled())
                m_speed_control.disable();
            // Reset to NaN so the next entry into need_active forces
            // a fresh push, even if `target` happens to match the last
            // value we wrote before disabling.
            m_last_speed_target = std::numeric_limits<float>::quiet_NaN();
        }

        // ---- SC6 NATIVE VM-FREEZE BYTE driver (2026-04, this session) -----
        // Engages SC6's INTERNAL VM freeze (the same mechanism hit-stop
        // and round-end cinematics use).  See Ghidra plate on
        // LuxBattle_TickHitStopSchedulerAndInputMirror for the full
        // architecture: setting g_LuxBattle_VMFreezeRecord.bVMFreezeByte
        // (at imageBase + 0x4862D0) to non-zero makes
        // LuxMoveVM_GetTimeDilationScalar return 0 for ALL callers,
        // halting every per-frame integrator (VM, opcodes, physics,
        // anims, FX dispatchers).
        //
        // SAFETY (post crash-on-load fix):
        //   1. Only TOUCH the byte when our state implies we WANT to
        //      change SC6's native freeze.  Steady-state "no freeze
        //      ever requested" path skips entirely.  Avoids stomping
        //      on SC6's own hit-stop/cinematic freeze writes during
        //      load/transitions.
        //   2. State-change-only — only emit a write when the desired
        //      state DIFFERS from our last write.
        //   3. SEH-wrapped via the static helper (try_write_vm_freeze_byte);
        //      __try/__except can't live in this function because of
        //      C++ destructors in scope.
        {
            const bool want_freeze = (target == 0.0f);
            const bool currently_owned = m_vm_freeze_byte_we_set.load();
            if ((want_freeze || currently_owned) &&
                want_freeze != currently_owned)
            {
                const uintptr_t base = Horse::NativeBinding::imageBase();
                if (base)
                {
                    if (try_write_vm_freeze_byte(
                            reinterpret_cast<volatile uint8_t*>(base + 0x4862D0),
                            want_freeze ? 1u : 0u))
                    {
                        m_vm_freeze_byte_we_set.store(want_freeze);
                    }
                    else
                    {
                        // Fault on access — disable our ownership flag
                        // and fall back to the per-function bare-RET
                        // sites (1..16).  Don't keep retrying.
                        m_vm_freeze_byte_we_set.store(false);
                    }
                }
            }
        }

        // ---- UE4 WORLD-PAUSE driver (2026-04-27, this session) -----------
        // Engages UE4's STANDARD AWorldSettings::Pauser flag whenever
        // HorseMod is freezing.  This is the ONLY mechanism that halts
        // UE4's UDemoNetDriver (the match-replay net driver that SC6 uses
        // — confirmed via 142 DemoNetDriver source-path strings in the
        // binary).
        //
        // WHY THIS IS NEEDED:
        // Sites 1-22 + VMFreezeByte catch SC6-specific Lux code in the
        // chara/BM Actor::Tick chains.  But UDemoNetDriver runs in
        // UWorld::Tick BEFORE any actor tick — it reads packets from the
        // .replay file and writes replicated state directly onto actors
        // via standard UE4 replication.  No actor-tick gate can stop it;
        // by the time TickActor fires, the chara's input has already
        // been mutated by the demo replication.
        //
        // SYMPTOM CAUGHT (user 2026-04-27):
        //   "the inputs of both characters doesnt match whats supposed
        //    to happen in the replay" — timer fine, position fine, but
        //    inputs played after a freeze cycle differ from no-freeze
        //    reference.  This is exactly UDemoNetDriver continuing to
        //    play recorded packets during the freeze window.
        //
        // FIX:
        // UE4's UDemoNetDriver::TickFlush early-returns when the world
        // is paused (standard UE4 source).  Setting AWorldSettings::Pauser
        // via UGameplayStatics::SetGamePaused(true) halts it at the
        // standard UE4 hook point.  When unfrozen, we set
        // SetGamePaused(false) and the demo driver resumes from where
        // it left off — no skip, no desync.
        //
        // SAFETY:
        //   * WorldPause::set() dedupes by atomic flag — no redundant
        //     ProcessEvent calls when state hasn't changed.
        //   * Returns false silently if PlayerController/UFunction not
        //     yet resolved (e.g. during loading screen or title menu);
        //     caller retries next frame.
        //   * No SEH wrapper: ProcessEvent on a validated UFunction with
        //     a correctly-laid-out params struct is safe.  IsReal is
        //     checked on the cached UObjects each call.
        // WORLD PAUSE + REPLAY PLAYER GATE — DISABLED 2026-04-27 ----------
        //
        // Both helpers were added to address the "input mismatch after
        // freeze" symptom.  After deploying BattleTimeFreeze, user tested
        // and reported: "I freeze for long enough, unpause, the game timer
        // ticks down but both players arent moving" — meaning the chars
        // were correctly frozen during freeze (good!) but couldn't resume
        // after unfreeze.
        //
        // Most likely cause: SetGamePaused(true) and/or SetActorTickEnabled
        // (false) on BattleReplayPlayer corrupted the demo driver's resume
        // path.  SC6's standard freeze (sites 1-16 + VMFreezeByte) ALONE
        // already correctly freezes chara/replay state — the user's earlier
        // "input mismatch" was a downstream symptom of the round-timer
        // leak, NOT actual chara state advancement.
        //
        // Reverting to: sites 1-16 + VMFreezeByte + BattleTimeFreeze.
        // The members stay declared so we can re-enable surgically if a
        // future symptom shows we need them again.
        //
        //   const bool want_pause = (target == 0.0f);
        //   (void)m_world_pause.set(want_pause);
        //   if (want_pause) (void)m_replay_player_gate.engage();
        //   else            (void)m_replay_player_gate.release();

        // ---- BATTLE-TIME (round timer) FREEZE — DISABLED 2026-04-27 ------
        // Setting bForciblyStopBattleTime + disabling BattleTimeManager
        // tick was empirically insufficient (user tested: timer still
        // ticked down).  Replaced with BattlePauseRequest below which
        // uses the engine's own pause UFunction.  Member kept for future
        // use; calls commented out.
        //   if (target == 0.0f) (void)m_battle_time_freeze.engage();
        //   else                (void)m_battle_time_freeze.release();

        // ---- BATTLE PAUSE REQUEST (THE FIX) ---------------------------
        // Invokes ULuxBattleFunctionLibrary::SetBattlePause via UE4SS
        // reflection — the EXACT mechanism SC6's in-game pause menu uses
        // to pause a replay.  User confirmed the in-game pause menu
        // correctly halts replay playback and changes the playback icon
        // to "paused" — meaning the engine's native pause works.  This
        // helper just calls that same pause function from HorseMod's
        // freeze hook so we get the SAME behaviour as the menu pause.
        //
        // Verified via Ghidra:
        //   SetBattlePause UFunction registered at FUN_140936190
        //   on ULuxBattleFunctionLibrary (BlueprintFunctionLibrary).
        //   Sibling UFunctions on the same library that we may use later:
        //     IsBattlePaused()       — query pause state
        //     StepInBattlePause()    — single-frame-step a paused battle
        //                              (could replace HorseMod's frame-step
        //                              once stable)
        //
        // See horselib/BattlePauseRequest.hpp for full rationale.
        {
            const bool want_pause = (target == 0.0f);
            if (want_pause)
                (void)m_battle_pause_request.engage();
            else
                (void)m_battle_pause_request.release();
        }
    }

    // SEH-wrapped single-byte write to g_LuxBattle_VMFreezeRecord.bVMFreezeByte.
    // Lifted to a static helper because __try/__except can't share a
    // function body with C++ destructors (frame_step_apply has plenty).
    // Returns true on successful write; false if the access faulted.
    static bool try_write_vm_freeze_byte(volatile uint8_t* p, uint8_t value) noexcept
    {
        __try
        {
            *p = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
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
        // ---------------- Performance gate (perf audit, 2026-04) ----------
        // Skip the per-tick PlayerCameraManager reflection chain when
        // free-fly is neither user-requested nor currently engaged.
        // The chain (FindFirstOf<APlayerController> + PlayerCameraManager
        // FName-indexed property-chain walk on each tick) is amortised
        // cheap once cached, but it's still wasted work for the common
        // case of a user that never presses F7.  We drop straight to
        // ~0 ns on those frames and only revive the resolution chain
        // once the user actually engages free-fly.
        //
        // Correctness: when both gates are false, m_free_camera.tick()
        // would early-return anyway (FreeCamera.hpp:537), and there's
        // no UI consumer of m_cached_player_camera_manager that needs
        // a value here (the HUD memory-verify panel is only meaningful
        // while free-fly is on, and in the OFF state we want it to
        // read "no PCM resolved" rather than a stale pointer).
        const bool want_on = m_free_camera_enabled.load();
        if (!want_on && !m_free_camera.is_enabled())
        {
            m_cached_player_camera_manager = nullptr;
            return;
        }

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

        // Handle UI-toggle edge transitions.  `want_on` was captured at
        // the top of the function for the perf gate; reuse it here so
        // we observe a single consistent snapshot of the toggle on this
        // tick (avoids any chance of a TOCTOU split between the gate
        // check and the edge handler).
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

        // Replay-state watchpoints maintenance.  Re-arms DR0..DR3 (per-
        // thread, can be cleared by OS) and drains the captured-write
        // ring buffer to UE4SS.log.  No-op if the user hasn't enabled
        // the debug toggle.  Must run from this hook (not the ImGui
        // tab) so log output happens even when the menu is closed.
        m_replay_watchpoints.tick();

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
                (m_backend_hit.isReady() && m_backend_hurt.isReady()) ? 1 : 0,
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

        // Sync each backend's slot with the per-feature ImGui toggles
        // and prime both this frame.  setSlot() invalidates the cached
        // LBC pointer when the slot changes; primeFrom() re-resolves it
        // from the (current) UWorld.  Idempotent when nothing changed.
        if (m_backend_hit.slot()  != m_slot_hit.load())
            m_backend_hit.setSlot(m_slot_hit.load());
        if (m_backend_hurt.slot() != m_slot_hurt.load())
            m_backend_hurt.setSlot(m_slot_hurt.load());
        m_backend_hit.primeFrom(pivot);
        m_backend_hurt.primeFrom(pivot);

        // Both must be ready to proceed — partial readiness would let
        // (e.g.) hitboxes draw without their hurtbox counterparts on
        // the same frame, which would visually misrepresent the state
        // of a move and confuse a viewer.
        if (!m_backend_hit.isReady() || !m_backend_hurt.isReady()) return;

        m_backend_hit.beginFrame();
        m_backend_hurt.beginFrame();

        const float T = m_thickness.load();

        int charas_seen = 0;
        int nodes_drawn = 0;

        // ---- Weapon visibility snapshot ---------------------------------
        // Compute once per frame.  `apply_weapons` is true when we need to
        // actively push SetWeaponVisibility into the game this frame:
        //   * when the EFFECTIVE toggle is ON — re-apply every frame to
        //     overwrite any game-driven re-show (the engine can flip
        //     visibility as part of cinematic cues; we fight it back).
        //   * on the EFFECTIVE ON -> OFF transition — call once with
        //     true to restore visibility, then stop touching it.
        //
        // CONFLICT WITH "Hide characters"
        // -------------------------------
        // CharaInvis (m_hide_chara) bytepatches the engine's read of
        // chara+0x534 inside SyncMoveStateVisibility from `cmp [..],0`
        // to `cmp [..],1`.  That inverts the boolean: with the patch
        // active, flag=1 (engine "visible") reads as invisible, and
        // flag=0 (engine "invisible") reads as visible.
        //
        // SetWeaponVisibility(false) writes 0 to +0x534.  When both
        // toggles are on, the patched compare reads `0 == 1 -> visible`
        // and the weapons stay VISIBLE — opposite of what the user
        // asked for.
        //
        // Fix: when hide_chara is on, the patch ALREADY hides weapons
        // (CharaInvis patches both +0x533 chara-mesh and +0x534
        // weapon-mesh comparators).  So we suppress our own writes
        // entirely — let the engine's per-move-state writes settle the
        // flag back to 1 (its normal "visible" default) and let the
        // patch invert that to "invisible" the way it's designed to.
        //
        // The transition tracking (last_applied) still runs against
        // the EFFECTIVE state so that toggling hide_chara ON while
        // hide_weapons was previously hiding gets correctly accounted
        // for — we write `true` once on that edge to flip +0x534 back
        // to 1, which the patch then reads as invisible.  Without that
        // restore step, +0x534 would stay at 0 (our last write) and
        // the patch's "0 -> visible" inversion would briefly show the
        // weapon for the few frames before the engine's own state
        // machine writes 1 again.
        const bool hide_weapons_raw = m_hide_weapons.load();
        const bool hide_chara_now   = m_hide_chara.load();
        const bool hide_weapons_now = hide_weapons_raw && !hide_chara_now;
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
            const bool hide_not_dmg     = m_hide_not_damage_active.load();
            // "Show all Hitboxes" unchecked (default) means APPLY the
            // per-frame damage-active filter; checked means skip it
            // and show every attack node regardless.  The inverted
            // polarity vs the old m_hide_not_per_frame_active lines
            // up with the UI label.
            const bool hide_not_pf      = !m_show_all_hitboxes.load();
            // "Show unused hurtboxes" unchecked (default) means HIDE
            // hurtboxes outside the classifier range; checked means
            // show them.  Same inverted-polarity pattern as
            // m_show_all_hitboxes.
            const bool hide_unused_hurt = !m_show_unused_hurt.load();
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
                            // "Hide unused hurtboxes" — skip hurtboxes
                            // whose +0x17 slot is outside
                            // ResolveAttackVsHurtboxMask22's iteration
                            // range (chara+0x44494 clamped to 22).
                            // The engine will happily OR attacker
                            // bits into the PerHurtboxBitmask index
                            // corresponding to these nodes, but the
                            // classifier never reads those slots so
                            // no reaction is written and no damage
                            // lands — they're effectively engine-
                            // invisible.  See m_hide_unused_hurt
                            // comment for the full derivation.
                            if (hide_unused_hurt && !d.classifier_addressable)
                                return;
                            break;
                        case Horse::KHitList::Attack:
                            if (!show_atk) return;
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
                    // Route by list kind so the user can have hitboxes
                    // and hurtboxes drawn into different UWorld batchers
                    // (e.g. hurtboxes Persistent for trail, hitboxes
                    // Foreground for clarity).  Body shares the hurt
                    // backend because it's logically a defensive volume.
                    Horse::LineBatcherBackend& b =
                        (d.list == Horse::KHitList::Attack)
                            ? m_backend_hit
                            : m_backend_hurt;
                    Horse::DrawKHitDraw(b, d, col, T);
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

        m_backend_hit.endFrame();
        m_backend_hurt.endFrame();

        // Once per ~2s: dump a summary so we can tell whether each stage
        // fired.  Parallel to the KHitWalker's shouldLog() throttle.
        // DISABLED — noisy; re-enable for end-to-end health checks.
#if 0
        if ((++m_diag_tick & 0x7F) == 1)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] frame pivot=0x{:x} backend_ready={} charas={} drawn={}\n"),
                reinterpret_cast<uintptr_t>(raw_cockpit),
                (m_backend_hit.isReady() && m_backend_hurt.isReady()) ? 1 : 0,
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
    // ImGui panel — single window split into four topical tabs.
    //
    //   Hitboxes  master F5, live move-frame, KHit lists, attack-role
    //             / damage filters, hit-flash slider, render options
    //   Camera    pose lock (pos + rot), Free-fly (F7), Ansel
    //   Time      freeze frame, frame-step, slow-motion
    //   General   catch-all: visibility overrides (weapons / chara
    //             / VFX) and anything else not specific to the other
    //             three tabs
    //
    // render_tab_impl() is just the dispatch shell; each tab's widgets
    // live in its own render_*_tab() method so the monolithic 900-line
    // panel is now four ~200-line focused ones.
    // ------------------------------------------------------------------
    void render_tab_impl()
    {
        // -----------------------------------------------------------------
        // Gamepad-first friendliness
        // -----------------------------------------------------------------
        // 1. Focus our window whenever the overlay (re-)appears OR
        //    whenever it's visible but somehow lost focus.  We use
        //    two signals:
        //
        //    a. g_overlay_just_shown — set by WndProcHook::set_visible
        //       on the hidden→shown edge, no matter how it was
        //       triggered (F2 or Back or any future hotkey).  This is
        //       reliable even across hide/show cycles, which a local
        //       static inside render_tab_impl can't observe because
        //       render_tab_impl doesn't run while hidden.
        //
        //    b. A post-Begin IsWindowFocused check — if we're visible
        //       but our window isn't focused (e.g. the user clicked
        //       outside, a popup grabbed focus, something else
        //       competed), ask for focus on the NEXT frame.
        //
        //    Both paths set m_nav_bootstrap_pending, which is
        //    consumed inside render_hitboxes_tab by
        //    ImGui::SetKeyboardFocusHere() on the F5 checkbox.
        //    Focusing the WINDOW alone isn't enough to give ImGui's
        //    nav system a NavId to highlight — SetKeyboardFocusHere
        //    actively claims focus for a specific widget, which also
        //    clears NavDisableHighlight so the D-pad responds
        //    immediately (no "hold Square" workaround needed).
        if (Horse::GameImGui::g_overlay_just_shown.exchange(
                false, std::memory_order_relaxed))
        {
            ImGui::SetNextWindowFocus();
            m_nav_bootstrap_pending = true;
        }

        // Window title carries the build date (DD-Mmm-YYYY) so users
        // running the dev mod can tell which build they're on — useful
        // when triaging bug reports on Discord ("which date is your
        // overlay window showing?").  Computed once from __DATE__ —
        // see horsemod_window_title() at the top of this TU.
        if (!ImGui::Begin(horsemod_window_title()))
        {
            ImGui::End();
            return;
        }

        // Focus-loss detection.  If the overlay is visible but our
        // window has lost nav focus for any reason, re-claim it.
        // Uses the root-child hierarchy (_RootAndChildWindows) so an
        // ImGui popup or child widget spawned by our own UI doesn't
        // falsely register as "someone else is focused."
        //
        // The re-claim is deferred to the NEXT frame by setting
        // m_nav_bootstrap_pending and using SetWindowFocus("HorseMod")
        // at the top of the next render_tab_impl — in-place re-focus
        // from inside the window we're currently rendering is
        // technically allowed but has brought up odd edge cases in
        // the past.  Deferring is safer and costs one frame of
        // visually-lost highlight.
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            m_nav_bootstrap_pending = true;
            ImGui::SetWindowFocus();
        }

        // 2. L1 / R1 (shoulder) cycle tabs.  Two pieces of state:
        //    - s_current_tab mirrors whichever tab is ACTUALLY showing
        //      (updated by whichever BeginTabItem returns true this
        //      frame).
        //    - s_requested_tab is the index to switch TO on this frame
        //      only (-1 = no switch).  We compute it from s_current_tab
        //      on an L1/R1 edge and clear it at the bottom.
        //
        //    This separation avoids a bug where the currently-visible
        //    BeginTabItem's sync-back would clobber our requested-tab
        //    value during the per-tab iteration, causing the
        //    SetSelected flag to never be applied to the target tab.
        //    Symptom was: R1 from any tab > 0 would "bounce back" to
        //    the first tab on every press, because the target tab
        //    never received the focus hand-off.
        //
        //    L1/R1 are suppressed while a widget is actively being
        //    edited (dragging a slider) so they keep their stock
        //    ImGui "tweak slower / faster" role in that context.
        constexpr int kNumTabs = 4;
        static int s_current_tab   = 0;
        static int s_requested_tab = -1;
        if (!ImGui::IsAnyItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, /*repeat=*/false))
            {
                s_requested_tab = (s_current_tab + kNumTabs - 1) % kNumTabs;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, /*repeat=*/false))
            {
                s_requested_tab = (s_current_tab + 1) % kNumTabs;
            }
        }

        if (ImGui::BeginTabBar("##horsemod_tabs"))
        {
            auto tab_item = [&](const char* label, int idx, auto&& body) {
                ImGuiTabItemFlags flags = 0;
                if (s_requested_tab == idx)
                {
                    flags |= ImGuiTabItemFlags_SetSelected;
                }
                if (ImGui::BeginTabItem(label, nullptr, flags))
                {
                    // Sync "what's actually visible" back to
                    // s_current_tab.  Does NOT touch s_requested_tab,
                    // so the SetSelected flag still gets applied to
                    // the target tab later in the iteration.
                    s_current_tab = idx;
                    body();
                    ImGui::EndTabItem();
                }
            };

            tab_item("Hitboxes", 0, [this] { render_hitboxes_tab(); });
            tab_item("Camera",   1, [this] { render_camera_tab(); });
            tab_item("Time",     2, [this] { render_time_tab(); });
            tab_item("General",  3, [this] { render_general_tab(); });

            ImGui::EndTabBar();
        }

        // Clear the requested-tab marker AFTER the tab bar finishes so
        // ImGui has processed the SetSelected flag for this frame.
        // Leaving it set would re-apply SetSelected on the next frame
        // too, which ImGui handles gracefully but wastes cycles.
        s_requested_tab = -1;

        ImGui::End();
    }

    // ==================================================================
    // Hitboxes tab — the core feature.  Master F5 toggle with live
    // status line, per-player move-frame display, KHit list checkboxes
    // (hurt / attack / body for P1 + P2), attack-role filters
    // (strike / throw) and the three engine-derived damage filters,
    // hit-flash duration slider, LineBatcher render options.
    // ==================================================================
    void render_hitboxes_tab()
    {
        // Nav bootstrap — see m_nav_bootstrap_pending doc comment.
        // Called BEFORE the checkbox so ImGui applies focus to it.
        // Cleared immediately so subsequent frames don't keep
        // stealing focus from wherever the user has navigated to.
        if (m_nav_bootstrap_pending)
        {
            ImGui::SetKeyboardFocusHere();
            m_nav_bootstrap_pending = false;
        }
        bool enabled = m_enabled.load();
        if (ImGui::Checkbox("Overlay enabled (F5)", &enabled))
        {
            m_enabled.store(enabled);
            if (!enabled)
            {
                m_backend_hit.hideAll();
                m_backend_hurt.hideAll();
            }
        }
        // Belt-and-suspenders: SetItemDefaultFocus registers the F5
        // checkbox as the fallback nav target when the tab bar
        // switches between tabs (ImGui picks this widget when there
        // are no previous-nav hints in the new tab).
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        // Friendly readiness summary.  If anything's still
        // initialising, say which thing and (almost always) the user
        // just needs to start a match for the rest to come online.
        if (!Horse::NativeBinding::isReady())
        {
            ImGui::TextDisabled("(setting up — check UE4SS.log if this persists)");
        }
        else if (!m_hook_registered)
        {
            ImGui::TextDisabled("(waiting for a match to start)");
        }
        else if (!m_backend_hit.isReady() || !m_backend_hurt.isReady())
        {
            ImGui::TextDisabled("(waiting for the battle scene)");
        }
        else
        {
            ImGui::TextDisabled("(ready)");
        }

        ImGui::Separator();

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
                    "Show this player's hurtboxes — the volumes that\n"
                    "RECEIVE damage.\n\n"
                    "Drawn green; flash red for a moment whenever the\n"
                    "hurtbox just got hit (the flash length is the\n"
                    "'Hit-flash duration' slider below).");
            }
            ImGui::SameLine();
            {
                bool a = atk.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Hitboxes##%s", id_suffix);
                if (ImGui::Checkbox(tag, &a)) atk.store(a);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Show this player's hitboxes — the volumes that\n"
                    "DEAL damage (or initiate grabs).\n\n"
                    "Strikes: amber when idle, yellow when actually\n"
                    "dealing damage this frame.\n"
                    "Throws / grabs: magenta when idle, pink when\n"
                    "actually active this frame.");
            }
            ImGui::SameLine();
            {
                bool b = body.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Body##%s", id_suffix);
                if (ImGui::Checkbox(tag, &b)) body.store(b);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Show this player's body / push-box — the volume\n"
                    "used for character-to-character physical pushing.\n"
                    "Not involved in damage, only in spacing.\n\n"
                    "Drawn dim blue.  Off by default because it clutters\n"
                    "the view during most frame-data work.");
            }
            ImGui::PopID();
        };

        per_player_row("P1",
                       m_show_p1_hurt, m_show_p1_atk, m_show_p1_body, "p1");
        per_player_row("P2",
                       m_show_p2_hurt, m_show_p2_atk, m_show_p2_body, "p2");

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
            if (ImGui::Checkbox("Damage-active only", &dg))
                m_hide_not_damage_active.store(dg);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "On : show only the hitboxes that can actually deal\n"
                "     damage right now.  Everything else is hidden,\n"
                "     even if it's drawn on the character mesh.\n"
                "Off (default): show every hitbox the current move\n"
                "     has, including ones that aren't dealing damage\n"
                "     this frame.\n\n"
                "Use this for a clean view of what's hitting right\n"
                "now — pair with Freeze frame or slow-motion for\n"
                "frame-data analysis.");
        }

        // --- Show all Hitboxes (disables the per-frame damage filter) ---
        // When OFF (default), we apply the engine's classifier
        // predicate at ResolveAttackVsHurtboxMask22 (0x14033C100):
        //
        //   capable_of_damage = (+0x14 != 0) AND
        //                       ((node.CategoryMask & per_move_cell) != 0)
        //
        // …and hide any attack node that fails it.  The category-
        // mask intersection correctly handles body-attached attack
        // boxes at floor slots (0, 2..17) whose +0x14 is always set:
        // they're hidden during neutral (cell==0) and shown only
        // during moves whose authored category set overlaps the
        // node's CategoryMask.
        //
        // When ON, the filter is bypassed entirely — every attack
        // node draws regardless of whether the engine currently
        // considers it capable of producing damage.  Useful when
        // inspecting passive or static attack volumes the default
        // frame-data view would hide.
        {
            bool show_all = m_show_all_hitboxes.load();
            if (ImGui::Checkbox("Show all Hitboxes", &show_all))
                m_show_all_hitboxes.store(show_all);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Off (default): hide hitboxes that aren't currently\n"
                "capable of damaging the opponent.  Cleaner view\n"
                "during startup and recovery frames — only the\n"
                "\"live\" portion of each move shows up.\n\n"
                "On : show every hitbox a move has, including\n"
                "passive / body-attached ones that never deal\n"
                "damage.  Useful if you want to inspect a move's\n"
                "complete hitbox layout rather than just its\n"
                "damaging frames.\n\n"
                "Works alongside the 'Damage-active only' filter\n"
                "above — both can be on at the same time.");
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
        {
            bool show_unused = m_show_unused_hurt.load();
            if (ImGui::Checkbox("Show unused hurtboxes", &show_unused))
                m_show_unused_hurt.store(show_unused);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Off (default): hide hurtboxes that the game\n"
                "considers \"unused\" — ones that are drawn on the\n"
                "character mesh but can never actually produce a\n"
                "reaction or take damage.  Keeps the overlay clean.\n\n"
                "On : show those hurtboxes anyway.\n\n"
                "Note for modders: some dodge / movement / block\n"
                "moves can make legitimate hurtboxes look \"unused\"\n"
                "here because of how the engine counts its slots.\n"
                "If a hurtbox you expect to see is missing, turn this\n"
                "ON to confirm it actually exists.");
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
        ImGui::Separator();
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
                "How long the red flash stays visible when a hurtbox\n"
                "gets hit.  Measured in game frames (≈ 60 per second).\n\n"
                "0 = no flash (the underlying signal lasts one frame\n"
                "and is essentially invisible).\n"
                "Default 15 = about a quarter-second of flash.\n"
                "Cap 60 = a full second at normal speed.\n\n"
                "While Freeze frame is on, the timer pauses so the\n"
                "flash stays visible until you unfreeze or step a\n"
                "frame — useful for inspecting who hit whom.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Render");
        {
            float t = m_thickness.load();
            if (ImGui::SliderFloat("Thickness", &t, 0.5f, 8.0f, "%.1f"))
                m_thickness.store(t);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Line thickness for the drawn hitbox / hurtbox /\n"
                    "body wireframes.  Default 1.5 is a good balance;\n"
                    "bump it up if you're recording or screenshotting\n"
                    "and want the lines to read clearly at a distance.");

            // Per-feature renderer combos.  Two entries each: Persistent
            // (depth-tested, lines accumulate over time — useful for
            // tracing a chara's path through a move) and Foreground
            // (always-on-top, lines clear each frame — clean read of
            // the current state).  The third historical entry "Default"
            // (UWorld+0x40, depth-tested per-frame) was removed because
            // its lines disappeared behind characters, which defeats
            // the purpose of an overlay.
            const char* slot_names[2] = {
                "Persistent (trail)",
                "Foreground (always visible)",
            };

            int hit_idx = static_cast<int>(m_slot_hit.load());
            if (ImGui::Combo("Hitbox renderer", &hit_idx, slot_names, 2))
                m_slot_hit.store(static_cast<Horse::LineBatcherSlot>(hit_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "How hitboxes are rendered against the 3D scene.\n\n"
                    "  Foreground (recommended): hitbox lines always\n"
                    "  draw on top of everything else.  Best for\n"
                    "  reading the exact shape of a move's strike\n"
                    "  volume frame by frame.\n\n"
                    "  Persistent: hitbox lines accumulate over time\n"
                    "  rather than refreshing each frame.  Rarely\n"
                    "  useful for hitboxes — the trail piles up faster\n"
                    "  than the eye can disambiguate.");

            int hurt_idx = static_cast<int>(m_slot_hurt.load());
            if (ImGui::Combo("Hurtbox renderer", &hurt_idx, slot_names, 2))
                m_slot_hurt.store(static_cast<Horse::LineBatcherSlot>(hurt_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "How hurtboxes (and the body box) are rendered\n"
                    "against the 3D scene.\n\n"
                    "  Foreground (recommended): hurtbox lines always\n"
                    "  draw on top of everything else.  Clean read of\n"
                    "  the chara's current defensive volumes.\n\n"
                    "  Persistent: hurtbox lines accumulate over time.\n"
                    "  Useful for tracing how a chara's hurtbox sweeps\n"
                    "  through space across the duration of a move —\n"
                    "  the trail visualises the full path.");
        }
    }

    // ==================================================================
    // Camera tab — pose lock (position + rotation group), Free-fly
    // camera (F7) with its sub-controls (move/look/FOV sliders, live
    // pose readout, memory-verify line, input diagnostics), and Ansel
    // always-allowed.  All independent of the F5 hitbox overlay.
    // ==================================================================
    void render_camera_tab()
    {
        // --- Always allow Ansel camera -----------------------------------
        // Runs independent of the F5 hitbox overlay.  Kept at the top of
        // the Camera tab (rather than buried under Free-fly's sub-controls)
        // because it's a single checkbox with no state to inspect — the
        // user either wants Ansel always available or not.
        bool aa = m_ansel_always_allowed.load();
        if (ImGui::Checkbox("Always allow Ansel camera", &aa))
            m_ansel_always_allowed.store(aa);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Force NVIDIA Ansel (the built-in free-camera photo\n"
            "mode) to be available at all times.\n\n"
            "Normally SC6 only allows Ansel in specific situations\n"
            "(menus, cinematics, ring-out).  With this on you can\n"
            "trigger the Ansel hotkey any time, even mid-match.\n\n"
            "Independent of the F5 overlay — you can use Ansel with\n"
            "or without the hitbox overlay enabled.");

        ImGui::Separator();

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
            bool lc = m_cam_lock.is_enabled();
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
                      "takes over the camera lock while it's active.\n"
                      "Turn free-fly off first to toggle this manually."
                    : "Freeze the camera at its current position, angle,\n"
                      "and zoom level.  The game's own camera system\n"
                      "stops moving it until you turn this off.\n\n"
                      "Useful for framing a specific moment: turn this\n"
                      "OFF, let the game move the camera where you\n"
                      "want it, then turn ON to hold that shot.\n\n"
                      "Independent of the F5 overlay.");

            // Lock camera rotation has been removed from the UI.
            // It's still useful internally — Free-fly camera enables
            // it automatically while it's active so arrow-key look
            // works — but exposing it as a separate user toggle was
            // confusing.  Free-fly now owns the rotation lock entirely.

            // Status line — friendly summary of whether the camera
            // is currently locked.  Free-fly turning on the lock
            // counts as "active" here so the user sees feedback when
            // free-cam mode is engaged.
            if (!m_cam_lock.is_resolved() && lc)
            {
                ImGui::TextDisabled(
                    "(camera lock couldn't find its hook points — "
                    "see UE4SS.log for details)");
            }
            else if (m_cam_lock.is_enabled() &&
                     m_cam_lock.is_rotation_enabled())
            {
                ImGui::TextDisabled(
                    "(camera fully locked — position + rotation)");
            }
            else if (m_cam_lock.is_enabled())
            {
                ImGui::TextDisabled("(camera position locked)");
            }
            else if (m_cam_lock.is_rotation_enabled())
            {
                ImGui::TextDisabled("(camera rotation locked)");
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
                    "Take manual control of the camera and fly it\n"
                    "around freely.  Unlike Ansel this keeps the\n"
                    "hitbox overlay visible and the match running.\n\n"
                    "Keyboard (game window must be focused):\n"
                    "  W / S       move forward / back\n"
                    "  A / D       strafe left / right\n"
                    "  E / Q       move up / down\n"
                    "  Arrows or IJKL   look around\n"
                    "  Shift       5× faster  |  Ctrl  0.2× slower\n"
                    "(If the arrow keys don't respond, use IJKL\n"
                    " instead — the game grabs arrows on some\n"
                    " screens.)\n\n"
                    "Controller (player 1):\n"
                    "  Left stick    move\n"
                    "  Right stick   look\n"
                    "  LT / RT       move down / up\n"
                    "  LB / RB       0.2× / 5× speed\n\n"
                    "Turning this on also locks the camera\n"
                    "automatically; turning it off releases the\n"
                    "lock.  To re-frame a shot, toggle OFF, let\n"
                    "the game move the camera where you want,\n"
                    "then toggle back ON.");

                // Sub-controls, only visible when free-cam is on to
                // avoid cluttering the Camera tab.
                if (fc)
                {
                    float mv = m_free_camera.move_speed();
                    if (ImGui::SliderFloat("Move speed", &mv,
                                           2.0f, 100.0f, "%.1f"))
                        m_free_camera.move_speed() = mv;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "How fast WASD / left-stick moves the camera.\n"
                        "Hold Shift (or RB) for 5× this speed, Ctrl\n"
                        "(or LB) for 0.2×.");

                    float lk = m_free_camera.look_speed();
                    if (ImGui::SliderFloat("Look speed", &lk,
                                           0.2f, 6.0f, "%.2f"))
                        m_free_camera.look_speed() = lk;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "How fast the arrow keys / IJKL / right-stick\n"
                        "rotate the camera view.  Same Shift / Ctrl\n"
                        "multipliers as Move speed.");

                    float fv = m_free_camera.fov_deg();
                    if (ImGui::SliderFloat("Field of view", &fv,
                                           20.0f, 120.0f, "%.0f"))
                        m_free_camera.fov_deg() = fv;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Camera field of view in degrees.  Lower =\n"
                        "zoomed-in / telephoto look, higher = wide-\n"
                        "angle / fisheye look.  70 is the game's\n"
                        "default.");

                    // Live pose readout — handy for reproducing shots.
                    ImGui::TextDisabled(
                        "position (%.1f, %.1f, %.1f)  rotation (%.1f, %.1f, %.1f)",
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
                    // Connection status: keeping these (they're genuinely
                    // useful when input suddenly stops working) but
                    // rewriting the labels in plain English.
                    ImGui::TextDisabled(
                        "Controller: %s",
                        Horse::FreeCamera::controllerConnected()
                            ? "connected"
                            : "not detected");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Whether a controller is currently reporting\n"
                        "input to the game.  If 'not detected' but\n"
                        "you ARE pressing buttons, Steam Input or the\n"
                        "controller driver may not be passing the\n"
                        "input to SC6.");

                    ImGui::TextDisabled(
                        "Keyboard: %s",
                        (Horse::LowLevelKeyInput::instance().hook_installed() ||
                         Horse::RawInputSource::instance().ready())
                            ? "responding"
                            : "not responding");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Whether the keyboard-input paths the free-\n"
                        "camera uses are currently alive.  If 'not\n"
                        "responding', movement / look keys won't\n"
                        "register.");

                    // Developer-mode "Log key input" and
                    // "Log camera pose" checkboxes have been removed
                    // from the UI.  The underlying atomics still
                    // exist on Horse::FreeCamera, so the diagnostics
                    // can still be flipped from C++ if anyone is
                    // chasing a bug, but the panel stays clean of
                    // debug-only checkboxes for typical users.
                }

                // Friendly status when free-cam is on but we don't
                // have a camera to drive (menus, idle, loading).
                if (fc && !m_cached_player_camera_manager)
                {
                    ImGui::TextDisabled(
                        "(waiting for a match — free-cam needs an "
                        "active camera)");
                }
            }
    }

    // ==================================================================
    // Time tab — Freeze frame (drives SpeedControl speedval=0 for a
    // hard world-tick stop), Step 1 / Step N buttons for deterministic
    // frame-stepping under freeze, and the Slow-motion slider + preset
    // buttons (0.001x..1.0x).  Both toggles are independent; Freeze
    // wins while held and releases back to Slow-mo's value.
    // ==================================================================
    void render_time_tab()
    {
        // --- Live move-frame display -------------------------------------
        // Deref chara+0x44068 ActiveLaneStateCursorPtr and show
        // CurrentAnimFrame / AnimLengthFrames for each player.  Costs
        // ~4 safe reads per frame per player — negligible.
        //
        // Lives on the Time tab because it's the frame-data readout you
        // watch while driving Freeze frame / Slow-mo: "I paused at frame
        // 7 of 30 of move 0x1234, lane 2, playback 0.5x."
        {
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
                    "Live readout of which move this player is in:\n"
                    "  current-frame / total-frames\n"
                    "  move ID (internal hex value)\n"
                    "  lane (which attack slot is active)\n"
                    "  speed (playback multiplier — 1.00x = normal)\n"
                    "  [T] = mid-transition between moves\n"
                    "  [done] / [end] = move has finished playing\n"
                    "\n"
                    "Useful alongside Freeze frame and Slow-motion:\n"
                    "pause the world, read the frame number off this\n"
                    "line, then step to inspect exactly what's active\n"
                    "on that frame.");
            };
            row("P1:", 0);
            row("P2:", 1);
        }

        ImGui::Separator();

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
                "Pause the battle — animations, hitboxes, everything\n"
                "stops moving.  The game itself keeps running (menus\n"
                "still work, you can rotate the free-camera) but no\n"
                "frames advance.\n\n"
                "Use the Step buttons below, or the F6 hotkey, to\n"
                "advance one frame at a time.  Holding F6 gives you\n"
                "slow-motion via auto-repeat.\n\n"
                "Works together with Slow-motion: Freeze always wins\n"
                "while it's on, and when you turn it off the game\n"
                "resumes at whatever Slow-motion speed was set.");

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
                ImGui::SetTooltip(
                    "Turn on Freeze frame first — stepping only\n"
                    "makes sense when the world is paused.");
            }

            // Status line — "paused" / "stepping" / unresolved.
            if (m_speed_control.is_resolved())
            {
                if (const int q = m_step_pending.load(); q > 0)
                {
                    ImGui::TextDisabled("(advancing %d more frame%s…)",
                                        q, q == 1 ? "" : "s");
                }
                else if (ff)
                {
                    ImGui::TextDisabled("(paused — press F6 to advance one frame)");
                }
            }
            else if (ff)
            {
                ImGui::TextDisabled(
                    "(couldn't hook the game's timing — see UE4SS.log)");
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
                    "Run the battle at a slower (or faster) speed.\n"
                    "Use the slider to the right, or the preset\n"
                    "buttons below for common analysis speeds:\n"
                    "\n"
                    "  Freeze = effectively paused (0x).\n"
                    "  0.1x   = very slow-motion, good for\n"
                    "           frame-by-frame inspection.\n"
                    "  0.5x   = half speed — readable without\n"
                    "           stepping.\n"
                    "  1x     = normal speed.\n"
                    "\n"
                    "Independent of Freeze frame — both can be on at\n"
                    "the same time; Freeze just wins while it's held.");

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
                        "(couldn't hook the game's timing — see UE4SS.log)");
                }
                else if (m_speed_control.is_enabled())
                {
                    ImGui::TextDisabled("(running at %.3fx speed)",
                                        m_speed_value.load());
                }
            }

            // --- Replay-state watchpoints (debug) -----------------------
            // Diagnostic: install hardware data breakpoints on the four
            // replay-cursor fields (chara+0x39C, chara+0x3A4, chara+0x4410,
            // BM+0x148C) and log every write with the writing
            // instruction's RIP.  Use to identify the residual leak path
            // that causes replay drift through HorseMod's freeze.
            //
            // ENABLE only briefly during a test run — every cursor write
            // costs an exception round-trip (~few hundred ns).  Disable
            // before normal play.
            ImGui::Separator();
            ImGui::TextUnformatted("Replay-state watchpoints (DEBUG)");
            {
                bool wp = m_replay_watch_enabled.load();
                if (ImGui::Checkbox("Watch replay cursors##rwp", &wp))
                {
                    m_replay_watch_enabled.store(wp);
                    if (wp)
                    {
                        // Resolve BM via UE4SS reflection — the static-image
                        // chara global (g_LuxBattle_CharaSlotP1) doesn't have
                        // an initialized embedded UE4 sub-object at +0x388,
                        // so the canonical chara→ResolveBM path returns
                        // null.  Horse::Lux uses FindFirstOf("LuxBattleManager")
                        // which works regardless of the chara state.
                        Horse::Obj bm_obj = m_lux.battleManager();
                        void* bm_raw = bm_obj ? static_cast<void*>(bm_obj.raw())
                                              : nullptr;
                        if (!m_replay_watchpoints.enable(bm_raw))
                            m_replay_watch_enabled.store(false);
                    }
                    else
                    {
                        m_replay_watchpoints.disable();
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Install x86 hardware breakpoints (DR0..DR3) on the\n"
                    "four replay-cursor fields and log every write to\n"
                    "the in-mod ring buffer below.\n\n"
                    "Use to identify what's still mutating replay state\n"
                    "while HorseMod's freeze is engaged:\n"
                    "  1. Enter a replay\n"
                    "  2. Enable this checkbox\n"
                    "  3. Press F6 to freeze\n"
                    "  4. Wait 5 seconds\n"
                    "  5. Press F6 to unfreeze\n"
                    "  6. Inspect the log: any entries with\n"
                    "     'speedval=0.000' indicate the leak.\n\n"
                    "Costs a CPU exception per cursor write (~few\n"
                    "hundred ns).  Turn OFF before normal play.");

                if (m_replay_watchpoints.is_enabled())
                {
                    // Drain new ring entries to UE4SS.log every frame the
                    // ImGui surface renders.  Game-thread context, no UE4
                    // locks held — safe place to do the I/O the VEH must
                    // not.  See ReplayWatchpoints::drain_to_log() comment.
                    m_replay_watchpoints.drain_to_log();

                    const auto& tg = m_replay_watchpoints.targets();
                    ImGui::TextDisabled(
                        "Watching: 0x%llX (cursor)  0x%llX (clock)\n"
                        "          0x%llX (chara+0x4410)  0x%llX (BM mirror)",
                        static_cast<unsigned long long>(tg[0]),
                        static_cast<unsigned long long>(tg[1]),
                        static_cast<unsigned long long>(tg[2]),
                        static_cast<unsigned long long>(tg[3]));

                    if (ImGui::SmallButton("Clear log##rwp"))
                        m_replay_watchpoints.clear_log();

                    // Show most-recent N entries.  Newest first.
                    static constexpr size_t kShowMax = 32;
                    Horse::ReplayWatchpoints::Entry buf[kShowMax];
                    const size_t n =
                        m_replay_watchpoints.snapshot_recent(buf, kShowMax);

                    if (n == 0)
                    {
                        ImGui::TextDisabled("(no writes recorded yet)");
                    }
                    else
                    {
                        ImGui::Text("Recent writes (%zu shown):", n);
                        const uintptr_t mod_base =
                            Horse::NativeBinding::imageBase();
                        for (size_t i = 0; i < n; ++i)
                        {
                            const auto& e = buf[i];
                            // Map RIP back to a module-relative offset
                            // for easy Ghidra paste.
                            const long long rva =
                                mod_base
                                    ? static_cast<long long>(e.writer_rip)
                                          - static_cast<long long>(mod_base)
                                    : -1;
                            // Slot index for display (0..3) based on
                            // matching watched_addr.
                            int slot = -1;
                            for (int k = 0; k < 4; ++k)
                                if (tg[k] == e.watched_addr) { slot = k; break; }
                            ImGui::Text(
                                "[%2zu] slot=%d  addr=0x%llX  val=0x%X  "
                                "speedval=%.3f  RIP=0x%llX  (RVA 0x%llX)",
                                i, slot,
                                static_cast<unsigned long long>(e.watched_addr),
                                e.value_after,
                                e.speedval_at_hit,
                                static_cast<unsigned long long>(e.writer_rip),
                                static_cast<unsigned long long>(rva));
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled(
                        "(disabled — enable above, then start a replay)");
                }
            }

    }

    // ==================================================================
    // General tab — catch-all for visibility overrides and other
    // toggles that don't fit the Hitboxes / Camera / Time split.
    // Hide weapons (per-frame
    // SetWeaponVisibility call), Hide characters (bytepatch on
    // SyncMoveStateVisibility), Suppress VFX (per-slot VFX writer
    // bytepatch).  All runtime-independent of the F5 overlay toggle.
    // ==================================================================
    void render_general_tab()
    {
            // --- Hide weapons -------------------------------------------
            // Force hide both charas' weapons so they stop occluding the
            // hitbox overlay.  Calls SetWeaponVisibility(false) every frame
            // while on (so the game's own show-triggers don't sneak weapons
            // back in); calls SetWeaponVisibility(true) once on OFF to
            // restore.  Applies only while the overlay is enabled — if F5
            // turns the mod off, weapons stay in whatever state the engine
            // last set (typically visible).
            //
            // When "Hide characters" is also on, this control is greyed
            // out: CharaInvis already hides both chara mesh AND weapons
            // via a bytepatch that's incompatible with our per-frame
            // SetWeaponVisibility writes (the patch inverts the meaning
            // of the +0x534 weapon-flag, so writing 0 produces "visible"
            // — opposite of what we want).  See the apply-loop comment
            // in render_tab_impl for the full breakdown.
            const bool hide_chara_active = m_hide_chara.load();
            bool hw = m_hide_weapons.load();
            ImGui::BeginDisabled(hide_chara_active);
            if (ImGui::Checkbox("Hide weapons", &hw))
                m_hide_weapons.store(hw);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                if (hide_chara_active)
                {
                    ImGui::SetTooltip(
                        "Already covered by \"Hide characters\" — that\n"
                        "toggle hides weapons along with the body mesh\n"
                        "via the same engine-level visibility patch.\n\n"
                        "Disable \"Hide characters\" first if you want\n"
                        "to use this independently (e.g. show the body\n"
                        "but hide the weapon).");
                }
                else
                {
                    ImGui::SetTooltip(
                        "Hide both characters' weapons.  Handy for inspecting\n"
                        "hitboxes on characters with bulky weapons (Nightmare's\n"
                        "sword, Astaroth's axe) that would otherwise block the\n"
                        "view of the volumes you're trying to see.\n\n"
                        "Only applies while the F5 hitbox overlay is enabled.\n"
                        "Turning this off restores the weapons.");
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
                "Hide both characters' models entirely — body, hair,\n"
                "clothing, all accessories.  The match still runs\n"
                "normally (hitboxes keep updating, damage still\n"
                "applies), you just can't see the fighters.\n\n"
                "Useful for isolating the hitbox overlay from the\n"
                "visual noise of animation / costume detail when\n"
                "recording or taking reference screenshots.\n\n"
                "No flicker during supers, critical edges, or\n"
                "transformations — the characters stay hidden the\n"
                "whole time.");

            if (!m_chara_invis.is_resolved() && hc)
            {
                ImGui::TextDisabled(
                    "(couldn't hook character visibility — see UE4SS.log)");
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
                "Suppress hit-effect flashes, sparks, and particle\n"
                "VFX.  Good for a cleaner view of the hitbox overlay\n"
                "during fast exchanges where spark effects would\n"
                "otherwise fill the screen.\n\n"
                "Effects don't even appear for a single frame when\n"
                "this is on.  Turn off to restore them.  Independent\n"
                "of the F5 overlay.");

            if (!m_vfx_off.is_resolved() && sv)
            {
                ImGui::TextDisabled(
                    "(couldn't hook the VFX system — see UE4SS.log)");
            }

            // --- Reset position override -----------------------------
            // When enabled and the user has captured a pose, our post-
            // hook on TrainingModePositionReset replays the captured
            // (X, Y, Z + side) for both players after the engine's
            // own reset has run.  Press the in-game training-reset
            // bind (default Select on a pad) to trigger.
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted("Reset position override");
            {
                auto& ro = Horse::ResetOverride::instance();
                bool ro_on = ro.enabled();
                if (ImGui::Checkbox("Override reset position", &ro_on))
                {
                    ro.set_enabled(ro_on);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "When on, the next time you press the in-game\n"
                    "training-mode position-reset bind, both players\n"
                    "are sent to the position you captured below\n"
                    "instead of the default round-spawn pose.\n\n"
                    "Off : the game's default reset behaviour applies.\n"
                    "Capture a pose first (button below) before turning\n"
                    "this on, otherwise it does nothing.");

                if (ImGui::Button("Capture current position"))
                {
                    const bool ok = ro.capture_both();
                    if (ok)
                    {
                        Output::send<LogLevel::Default>(
                            STR("[HorseMod] reset-override pose captured\n"));
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[HorseMod] reset-override capture failed "
                                "(no active match?)\n"));
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Snapshot both characters' current world position\n"
                    "and which side they're facing.  This becomes the\n"
                    "pose used by the override on the next reset.\n\n"
                    "Move each character to the position / side you\n"
                    "want (using normal movement), then press this.\n"
                    "The capture is persistent — it survives game\n"
                    "restarts via the mod's settings.cfg.");

                ImGui::SameLine();
                if (ImGui::Button("Clear captured pose"))
                {
                    ro.clear_captured();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Forget the captured pose for both players, so\n"
                    "the override checkbox above falls back to a\n"
                    "no-op until you capture again.");

                // Per-player readout of what's currently captured.
                for (int pi = 0; pi < 2; ++pi)
                {
                    const auto p = ro.get_pose(pi);
                    if (p.has)
                    {
                        ImGui::TextDisabled(
                            "P%d  pos=(%.1f, %.1f, %.1f)  side=%s",
                            pi + 1, p.pos_x, p.pos_y, p.pos_z,
                            p.side_flag ? "right" : "left");
                    }
                    else
                    {
                        ImGui::TextDisabled("P%d  not captured yet", pi + 1);
                    }
                }

                const bool any_reset_registered = std::any_of(
                    m_reset_slots.begin(), m_reset_slots.end(),
                    [](const ResetHookSlot& s) { return s.registered; });
                if (!any_reset_registered)
                {
                    ImGui::TextDisabled(
                        "(waiting for training-reset hook — start a match)");
                }
            }

            // ---- Online (modded lobbies) ---------------------------------
            // The HorseMod custom Room policy.  Controls which (if any)
            // online battle-rule override is active.  See
            // horselib/OnlineRules.hpp for the architecture and the
            // hard requirement that BOTH peers run HorseMod with the
            // same policy selected — no out-of-band sync is implemented
            // in v1, so the user is responsible for coordinating with
            // their opponent over Discord / voice / etc.
            ImGui::Separator();
            ImGui::TextUnformatted("Online (modded lobbies)");

            {
                auto& rules = Horse::OnlineRules::instance();
                const auto current = rules.current_policy();

                // Build the dropdown items each frame.  Order: Vanilla
                // first as the safe default, then SlipOut (the v1
                // working policy), then the stub policies marked with
                // "(coming soon)" suffix.  Order matches the
                // HorsePolicy enum value sequence so an index lookup
                // round-trips cleanly.
                static const Horse::HorsePolicy kOrder[] = {
                    Horse::HorsePolicy::Vanilla,
                    Horse::HorsePolicy::SlipOut,
                    Horse::HorsePolicy::NoRingOut,
                    Horse::HorsePolicy::EndlessMode,
                    Horse::HorsePolicy::DamageUp,
                    Horse::HorsePolicy::BlowUp,
                };
                static_assert(
                    sizeof(kOrder) / sizeof(kOrder[0])
                        == Horse::kHorsePolicyCount,
                    "kOrder must list every HorsePolicy enum value");

                // Find the current selection's index in our display order.
                int current_idx = 0;
                for (int i = 0; i < Horse::kHorsePolicyCount; ++i)
                {
                    if (kOrder[i] == current) { current_idx = i; break; }
                }

                if (ImGui::BeginCombo("HorseMod policy",
                                       Horse::OnlineRules::policy_name(
                                           kOrder[current_idx])))
                {
                    for (int i = 0; i < Horse::kHorsePolicyCount; ++i)
                    {
                        const auto p = kOrder[i];
                        const bool selected = (i == current_idx);
                        // Stub policies are still selectable (so the
                        // user can see them in the menu) but typing a
                        // disabled marker into the label keeps the
                        // user from expecting a working effect.
                        if (ImGui::Selectable(
                                Horse::OnlineRules::policy_name(p),
                                selected))
                        {
                            rules.set_policy(p);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "%s",
                                Horse::OnlineRules::policy_tooltip(p));
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "%s",
                        Horse::OnlineRules::policy_tooltip(current));

                // Status line — three states:
                //   * Hooks not yet installed (UE class not loaded).
                //   * Hooks installed but Vanilla policy → no override.
                //   * Hooks installed and a non-Vanilla policy active.
                if (!rules.hooks_installed())
                {
                    ImGui::TextDisabled(
                        "(installing hooks — start a match scene)");
                }
                else if (current == Horse::HorsePolicy::Vanilla)
                {
                    ImGui::TextDisabled(
                        "Vanilla mode — no rule overrides active.");
                }
                else
                {
                    ImGui::TextColored(
                        ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                        "Active: %s — both peers MUST have HorseMod\n"
                        "with this policy or the connection will drop.",
                        Horse::OnlineRules::policy_name(current));
                }
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
