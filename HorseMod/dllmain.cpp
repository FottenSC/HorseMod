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
#include "horselib/WorldTickGate.hpp"
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

// SEH-wrapped pointer / scalar dereference helpers.  Used by the retrack-
// event overlay to read chara+0x94 (yaw float) and chara+0x16E6 (motion
// flag) without crashing if a chara pointer goes stale mid-tick (e.g. a
// mode transition destroys the BattleManager between forEachChara
// emitting it and us reading the bytes off it).
#include "horselib/SafeMemoryRead.hpp"

// Modded-lobby battle-rule overrides (SlipOut + stubs).  See the
// file-header doc comment for the full rationale and the requirement
// that BOTH peers run HorseMod for any selected policy to work
// without desync.  Hooks the relevant runtime rule-gate UFunctions
// and overrides their return values when the user has selected the
// matching HorsePolicy.
#include "horselib/OnlineRules.hpp"

// Self-disable in online matches against humans (RankMatch / CasualMatch).
// Hooks ULuxUIGamePresenceUtil::SetPresence to track the current scene's
// ELuxGamePresence enum value — Training/Replay/single-player are safe,
// online matches are not.  See horselib/GameMode.hpp for the full rationale
// and the enum mapping.
#include "horselib/GameMode.hpp"

// PolyHook x64Detour on ULuxUIBattleLauncher::Start (image+0x5EEB50).
// This is the chokepoint for ALL 5 BattleRule overrides — the detour
// calls the appropriate Set<X>Mode setter on the launcher BEFORE the
// original Start runs, writing our values into the launcher's data-
// table cache.  The original then reads our values when it builds the
// per-match rule set.  Works for every rule regardless of whether the
// lobby Blueprint itself called the corresponding Set*Mode UFunction.
#include "horselib/LuxBattleLauncherStartHook.hpp"

// PolyHook x64Detour on LuxBattleChara_HasSubProviderEntryOfType0x3e
// (image+0x3F2990).  This is the SlipOut runtime gate that BOTH the
// host's chara init AND the joiner's chara init read during match
// start to populate the per-chara cache at chara+0x488.  Hooking here
// is universal: both clients run the same hook, both get the same
// answer, no host-vs-joiner asymmetry.  See the file-header doc for
// the full rationale (this hook supersedes the data-table-write
// approach for the SlipOut policy specifically).
#include "horselib/HasSubProviderEntryHook.hpp"
#include "horselib/EBTracer.hpp"
// horselib/GamePause.hpp REMOVED — was a 5-site trampoline patching the
// chara+0x394 audio-state bit instead of the world-tick pause we
// thought.  Superseded by the SpeedControl freeze-frame mechanism (see
// the "Freeze frame" UI block below) which writes speedval=0 / 1.0 to
// engage the dt-scale freeze + sites 1..16 + g_LuxBattle_VMFreezeByte.
// Removed 2026-04 — see git history for the implementation.
//
// horselib/BattlePauseRequest.hpp also REMOVED 2026-04-27 — turned out
// to invoke the same audio-mute path, breaking Soul Charge mid-move.
// See the member-list block where m_battle_pause_request used to live
// for the full forensic.

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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

    // ----------------------------------------------------------------
    //   Box-visibility filter — single master toggle.
    //
    //   m_only_show_active   default ON   engine-truth narrow filter
    //                                     applied to both lists:
    //                                       hits  → is_per_frame_active
    //                                              && attacker_can_strike_engine
    //                                       hurts → classifier_addressable
    //                                              && overlap_active
    //                                              && defender_can_react_engine
    //                                     (what the classifier actually
    //                                      acts on)
    //
    //   The two chara-wide gates (attacker_can_strike_engine /
    //   defender_can_react_engine — same boolean, dual-named) cover
    //   the resolver's three early-return sites that disable
    //   reaction processing wholesale:
    //     * Battle running         (DAT_144846410 != 0)
    //     * Not incapacitated/dead (chara+0x20B8 == 0)
    //     * Not in no-react state  (chara+0x19B0 != 6)
    //   When any fails, EVERY hurtbox on this chara is inert
    //   regardless of slot index / +0x14 / category mask.  Examples:
    //   round-end "WIN" cinematic, KO recovery, paused / loading.
    //
    // Engine-truth predicates:
    //   is_per_frame_active = (attack_node[+0x14] != 0) &&
    //                         (cat_mask & chara[+0x44058]) != 0
    //                       — exact predicate of
    //                         LuxBattle_ResolveAttackVsHurtboxMask22
    //                         @ 0x14033C100 before firing damage.
    //   classifier_addressable = (slot < min(chara+0x44494, 22))
    //                       — slot index is within the classifier's
    //                         iteration range.  A box at slot >= cap
    //                         can never deal damage no matter what
    //                         its +0x14 says, because the for-loop in
    //                         ResolveAttackVsHurtboxMask22 won't read
    //                         its PerHurtboxBitmask entry.
    //   overlap_active      = (hurt_node[+0x14] != 0)
    //                       — same byte the engine's overlap loop in
    //                         LuxBattleChara_UpdateAllKHitWorldCenters
    //                         @ 0x14030D6A0 gates iteration on.
    //                         Initialised to 1 by
    //                         Lux_KHitChk_DeserializeLinkedList; can
    //                         be flipped per-frame by MoveVM opcode
    //                         0x13AC (LuxMoveVM_SetHurtboxSlots-
    //                         ActiveMask @ 0x140308D70).
    //   defender_can_react_engine = (DAT_144846410 != 0) &&
    //                                (chara+0x20B8 == 0) &&
    //                                (chara+0x19B0 != 6)
    //                       — the three early-return gates of
    //                         LuxBattle_ResolveAttackVsHurtboxMask22.
    //                         When any fails, the whole resolver
    //                         skips and no slot is read.  Same
    //                         boolean is exposed as
    //                         attacker_can_strike_engine for
    //                         self-documenting attack-side filter
    //                         code (an incapacitated chara doesn't
    //                         deal damage either).
    // ----------------------------------------------------------------
    std::atomic<bool> m_only_show_active{true };

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
    std::atomic<bool> m_ansel_always_allowed{true};
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

    // ---- World-tick gate (PerFrameTick / Site 9 — moved here 2026-05-05) ---
    // Single PerFrameTick gate driving freeze + frame-step semantics
    // independently of the speedval / dt-multiply path.  See
    // horselib/WorldTickGate.hpp for the full plate.  In step+freeze mode
    // we set speedval = 1.0 (so the dt-multiply sites at 1/3/4/5/6/8 are
    // no-ops, eliminating the dt=0 contamination that was breaking multi-
    // hit moves under frame-step) and let this gate be the sole source of
    // "skip this frame" by holding an int32_t step-credit slot:
    //   policy = 0       -> bail every PerFrameTick call (frozen)
    //   policy = N > 0   -> next N PerFrameTick calls atomic-dec and run
    Horse::WorldTickGate m_world_tick_gate{};
    // Last `target` value pushed into m_speed_control.set_value() by
    // frame_step_apply().  Used to dedupe redundant writes when the
    // requested speedval doesn't change (perf audit, 2026-04).  Init
    // and reset-on-disable to NaN so the first write of any active
    // span is always forced (NaN != anything is always true).  Read
    // and written exclusively from the cockpit-tick caller — no
    // atomic needed.
    float m_last_speed_target =
        std::numeric_limits<float>::quiet_NaN();

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

    // BattlePauseRequest REMOVED 2026-04-27 ----------------------------------
    // Discovery: ULuxBattleFunctionLibrary::SetBattlePause is NOT the engine's
    // pause path.  The C++ impl at LuxBattleManager_SetPauseState_OrBattle-
    // Active @ 0x1403F9180 calls LuxBattleChara_SetBitFlag0x394_NotifyMove-
    // Ended which the Ghidra plate explicitly documents as AUDIO STATE
    // ("bit 2 = audio-force-mute"), NOT world-tick pause.  The plate also
    // says: "The REAL world-tick pause is g_LuxBattle_VMFreezeByte @
    // 0x1448462D0 ... HorseMod should be writing to g_LuxBattle_VMFreezeByte
    // directly for the actual freeze."  HorseMod already does that via
    // m_vm_freeze_byte_we_set above.
    //
    // The Soul-Charge break: SC has audio-cue-driven phase transitions
    // (activation glow → AOE pulse → recovery).  Muting audio mid-SC by
    // setting bit 2 of chara+0x394 stalls the state machine; the AOE phase
    // never fires, hitboxes never activate, the move "doesn't hit" anymore.
    //
    // Trade-off: without BattlePauseRequest the round timer ticks during
    // long replay freezes, eventually ending the round.  Acceptable for
    // now — the alternative was breaking gameplay-critical mechanics.  If
    // a clean round-timer halt is needed, the next investigation should
    // target the BattleTimeManager's actual tick path (BM+0x4F8) without
    // touching audio state.

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

    // Per-step diagnostic logger toggle (Time-tab checkbox).  When on,
    // every F6 step emits two UE4SS.log lines — "pre" right before the
    // world ticks at speedval=1.0, "post" right after — with key chara
    // fields the hit classifier reads (chara+0x16E5 attack-active,
    // +0x16EA ready-to-hit, +0x44058 own-cell, +0x44048 mirror-cell,
    // +0x4495A move slot id, +0x44DC2 sub-frame cell idx, +0x3500
    // per-chara time scale, +0x3508 hitstop counter, plus lane-1
    // anim-frame).  Lets the user diff "what changed across a single
    // game frame" against expectations and spot which field stops
    // updating between the first and second hit of a multi-hit move.
    std::atomic<bool> m_step_diag_enabled{false};
    std::atomic<int>  m_step_diag_seq{0};

    // Defensive frame-step resync: cockpit::Update can fire WITHOUT
    // the world ticking (UMG widget tick is independent of world tick
    // — see comment at the frame_step_apply callsite).  If Step Tick A
    // publishes speedval=1.0 but the world doesn't actually tick before
    // the next cockpit pre-hook (loading reentrance, paused redraw, a
    // doubled cockpit::Update call), pivoting to Step Tick B would
    // silently consume the user's F6 press.
    //
    // Witness: per-lane tick counter at lane+0x04 (int32) — the engine
    // increments this every world tick that processes the chara.  We
    // snapshot it on Step Tick A; on Step Tick B we re-read and only
    // pivot if at least one lane counter advanced.  If none advanced,
    // hold expecting=true and try again next cockpit tick.
    //
    // Cap holds at kStepDwellMax cockpit ticks to recover from a
    // stale or unmappable witness (e.g., chara struct destroyed during
    // a mode transition with pending > 0 — should be cleared by
    // clear_time_features_on_transition but we belt-and-suspender it).
    struct StepWorldTickWitness
    {
        bool    valid = false;
        int32_t p0_lane0_tickctr = 0;
        int32_t p0_lane1_tickctr = 0;
        int32_t p1_lane0_tickctr = 0;
        int32_t p1_lane1_tickctr = 0;
    };
    StepWorldTickWitness m_step_witness {};
    uint32_t             m_step_dwell   = 0;
    static constexpr uint32_t kStepDwellMax = 10;

    // Presence-transition tracker.  Stores the GamePresence we last
    // observed in on_cockpit_update_pre.  Whenever the live presence
    // differs from this value (i.e. SC6 transitioned modes — e.g.
    // training -> ranked -> training), we forcibly clear Freeze and
    // Slow-motion regardless of the "Auto disable online" gate.
    //
    // Why force the clear on EVERY transition (not just into PvP):
    //   1. SC6 destroys the old BattleManager + chara actors and
    //      builds new ones during a mode switch.  If freeze stays
    //      active across the transition, Site 9 (PerFrameTick entry-
    //      RET) blocks the new BattleManager's per-frame tick the
    //      moment its first chara fires the chain — including
    //      UpdateBattleCameraSynthesis, which is what the renderer
    //      reads to set the view matrix.  Result: black screen on
    //      training reload from a previous match.
    //   2. Slow-motion has the same hazard via Sites 1/3/4/5/6
    //      (dt-scale at math sites) — fractional dt during state-
    //      machine init can produce uninitialised camera / VFX
    //      state on the new mode's first frames.
    //   3. Once cleared, freeze/slow-mo STAY cleared (the user must
    //      manually re-engage them) — matching the user's mental
    //      model of "these are temporary debug tools, not persistent
    //      settings".
    //
    // Initialised to Unknown (0xFF) so the first observed presence
    // counts as a transition (Unknown -> something) and triggers a
    // safety clear at session start, in case the previous shutdown
    // somehow left freeze persisted in settings.cfg.
    std::atomic<uint8_t> m_last_seen_presence{
        static_cast<uint8_t>(Horse::GamePresence::Unknown)};

    // Frame-stepped slow-motion accumulator.
    //
    // Old behaviour (dt-scale slow-mo): writes a fractional speedval
    // like 0.5 into the codecave; the dt-multiply patches at sites
    // 1/3/4/5/6 scale dt accordingly.  Visually smooth but breaks
    // multi-hit moves: SC6's MoveVM stores hit cells per integer
    // frame, and a fractional dt accumulator drifts past hit
    // boundaries unpredictably (one tick advances by 0.5, next by
    // 1.0 once accum crosses, but the per-frame-cell hit detector
    // expects to see EACH integer frame exactly once — at fractional
    // dt it sees the same frame twice or skips entirely).
    //
    // New behaviour (frame-stepped slow-mo): each cockpit tick is a
    // hard 1.0 (full game frame) or 0.0 (freeze).  The accumulator
    // adds the slider value S each tick; when it crosses 1.0, that
    // tick is a "go" tick (target = 1.0), accumulator -= 1.0.
    // Otherwise it's a "stop" tick (target = 0.0).  Effective
    // average speed = S, but every game frame the engine sees is a
    // clean native-dt frame — hit cells advance one integer frame
    // at a time, multi-hit moves resolve correctly.
    //
    // Trade-off: slightly choppier visuals at low speeds (1 frame
    // every 4 ticks at S=0.25 = 15 fps effective).  But for analysis
    // and replay-watching, frame accuracy matters more than smooth
    // motion.  The choppiness is identical to repeatedly mashing
    // the Step-1 button at the right cadence — which is exactly
    // what users were asking for when they said "frame stepping
    // works but slow-mo doesn't".
    //
    // Range: only affects S in (0, 1].  S >= 1 produces target=1
    // every tick (full speed, no point slowing past native).  S <= 0
    // collapses to freeze (target=0 every tick), same as the
    // dedicated freeze toggle.
    //
    // Reset on slow-mo OFF -> ON edges so the cadence starts clean
    // (otherwise an in-flight accumulator could produce a one-tick
    // glitch at the resume).
    float m_slow_mo_accumulator {0.0f};

    // Most recent cockpit-tick decision from frame_step_apply().
    // Read by render_time_tab() to show a live cadence indicator
    // that flickers between "GO" (green) and "STOP" (red) so the
    // user can see the frame-step cadence at a glance — useful for
    // confirming the slider is actually doing what they expect at
    // very low speeds (e.g., 0.001x = one go-tick every ~1000
    // cockpit ticks ≈ 17 seconds; without a live indicator the user
    // would have no visual confirmation the system is alive).
    //
    // Atomic because it's written from the cockpit hook thread and
    // read from the render thread.  uint8_t enum values:
    //   0 = inactive    (slow-mo off / native speed)
    //   1 = stop tick   (target == 0.0)
    //   2 = go tick     (target == 1.0)
    enum class TickKind : uint8_t { Inactive = 0, Stop = 1, Go = 2 };
    std::atomic<uint8_t> m_last_tick_kind{
        static_cast<uint8_t>(TickKind::Inactive)};

    // Red "just got hit" sticky flash duration, in GAME FRAMES.
    //
    // The underlying PerHurtboxReactionState signal is a ~1-frame pulse
    // (~16ms at 60fps) — too short to see.  We extend it by holding the
    // hot state for `m_flash_frames` game frames before fading.
    //
    // KHitWalker drains the sticky by tracking g_LuxBattle_FrameCounter
    // (imageBase+0x470D0C4), which is incremented exactly once at the
    // end of LuxBattle_PerFrameTick.  Since Horse::WorldTickGate gates
    // PerFrameTick at its entry, the counter halts under freeze and
    // advances once per gate-released game frame under step / slow-mo
    // — the flash is held during freeze, drains one unit per F6 step,
    // and drains in lockstep with the slowed game clock during slow-mo.
    //
    // Default 15 frames ≈ 250ms of native-speed gameplay.
    std::atomic<int> m_flash_frames{15};

    std::atomic<float> m_thickness{1.5f};

    // Per-feature line-batcher slot.  Hitboxes (Attack list) draw via
    // m_backend_hit; hurtboxes + body (Hurtbox + Body lists) draw via
    // m_backend_hurt.  Splitting them lets the user trail hurtboxes
    // (Persistent) while keeping hitboxes always-on-top (Normal).
    // Both default to Foreground; Persistent is unsuitable for hitboxes
    // because the trail accumulates faster than the eye can disambiguate.
    std::atomic<Horse::LineBatcherSlot> m_slot_hit {Horse::LineBatcherSlot::Foreground};
    std::atomic<Horse::LineBatcherSlot> m_slot_hurt{Horse::LineBatcherSlot::Foreground};

    // Trail length in game frames for whichever backend is in the
    // Persistent slot.  Pushed to the backend each cockpit tick as
    // m_trail_frames / 60.0 seconds (RemainingLifeTime is wall-clock
    // in the engine's batcher tick).  Range matches the slider 1..300
    // = 1 frame to 5 seconds of trail at 60 Hz.  Default 30 ≈ 0.5 s,
    // long enough to be visibly useful for tracing a move without
    // drowning the screen in line history.  Used for both backends
    // when their slot == Persistent — Normal-slot backends ignore
    // this and stick to LineBatcherBackend::kDefaultLifetime.
    std::atomic<int> m_trail_frames{30};

    // ---- Retrack-event overlay ----------------------------------------
    // When ON, watches each chara's facing yaw every cockpit tick and
    // prints a transient "Player N retrack event" line on screen
    // whenever the engine rotated that chara during a move (i.e. the
    // chara's facing changed appreciably while a move was active).
    //
    // -------------------------------------------------------------------
    // History — what was tried first and why it was wrong
    // -------------------------------------------------------------------
    // Initial implementation watched chara+0x16E6 / chara+0x16E1 for
    // an "active retrack" gate equivalent to:
    //   active = (chara[+0x16E6] != 0) && (chara[+0x16E1] != 0)
    // based on a misreading of LuxBattleChara_RetrackFacingTowardOpponent
    // @ 0x140369450, which uses those two bytes as its early-return
    // gate.  That gate IS real, but the SEMANTICS of the two flags is:
    //
    //   chara+0x16E6 = motion-input flag #0x16  (set during most moves;
    //                  caller writes are widespread, not specifically
    //                  "move-locks-facing")
    //   chara+0x16E1 = motion-input flag #0x11  (part of the
    //                  fall-reaction cluster {0x0c..0x11, 0x29, 0x35} —
    //                  toggled by LuxBattle_ComputeHitReactionParams
    //                  @ 0x140343b90 case 0xd, which is a SPECIFIC
    //                  knockback / recovery type)
    //
    // The retrack gate's actual meaning is therefore:
    //
    //   gate-blocks = (in-some-non-walk-state) && (NOT-in-fall-reaction)
    //
    // i.e. retracking RUNS during idle/walk, AND during fall-reactions
    // mid-move; it's BLOCKED during normal mid-move animation.  There
    // is NO "homing override" flag — moves that track the opponent
    // (homing throws, certain supers) implement that through some
    // other mechanism (likely the SLERP-weight system at
    // chara+0x971ac..+0x971b8 set up at move-start, or by the move
    // script writing chara+0x94 directly).
    //
    // So watching the gate flag-pair fired the overlay during knockback
    // and fall recoveries — which the user reported as "triggers in
    // unexpected places".  Confirmed: my interpretation was wrong.
    //
    // -------------------------------------------------------------------
    // Current implementation — direct yaw-delta detection
    // -------------------------------------------------------------------
    // Read chara+0x94 (facing yaw, written by ApplyFacingRotationDelta)
    // every cockpit tick, compute the per-tick delta against last
    // tick's snapshot, and fire when:
    //
    //   |yaw_delta| > kRetrackYawThresholdNorm   (= ~0.7° per tick)
    //   AND chara is in some move state          (chara+0x16E6 != 0)
    //
    // This catches "the engine rotated my chara appreciably during a
    // move" regardless of which internal mechanism produced the
    // rotation — homing-throw retrack, hit-reaction realignment, or
    // a move script's direct yaw write.  False positives are limited
    // by the threshold; brief sub-degree adjustments don't fire.
    //
    // The yaw value at chara+0x94 is normalised in [0, 1) where 1.0
    // == 360°.  See the plate on RetrackFacingTowardOpponent for the
    // unit convention; the integrator at +0x94 uses the same scale.
    //
    // Off by default — diagnostic feature, not gameplay-affecting.
    std::atomic<bool> m_show_retrack_events{false};

    // Yaw threshold in normalised units (1.0 == 360°).  ~0.002 == 0.72°.
    // Below this we treat the rotation as "noise" / fine-tune adjustment
    // and don't fire; above it we treat it as a real retrack event.
    // Tuned empirically — natural facing-maintenance during idle/walk
    // produces sub-millidegree fluctuations; homing moves and hit
    // reactions produce multi-degree-per-tick rotations that easily
    // clear this bar.
    static constexpr float kRetrackYawThresholdNorm = 0.002f;

    // Per-player state for edge detection: previous tick's yaw, and
    // whether we were in a "retracking" state last tick (so we fire
    // ONE event per movement burst, not one per tick of it).  Indexed
    // by PlayerIndex (0 = P1, 1 = P2).
    float m_prev_yaw[2]        = {0.0f, 0.0f};
    bool  m_have_prev_yaw[2]   = {false, false};   // have we sampled yet?
    bool  m_was_retracking[2]  = {false, false};

    // Small ring buffer of recent on-screen text events.  Each entry
    // carries a fixed-size text payload and the ImGui::GetTime()
    // timestamp it fired at; the renderer iterates the buffer every
    // frame and draws every entry whose age is < kHudTextEventLifetime.
    //
    // Used by:
    //   • Retrack-event detector — formats "Player N retrack event" and
    //     pushes a string when an in-move yaw burst exceeds threshold.
    //   • Test button (General tab) — pushes "Hello World" to verify
    //     the overlay path is alive without needing a fight.
    //   • Any future C++ feature that wants to surface a transient
    //     diagnostic line on top of the game viewport.
    //
    // 8 slots × 1.5s lifetime is enough to show ~5 events per second
    // (an upper bound for human-perceivable distinct events) without
    // truncation.  Older entries get overwritten FIFO-style; the
    // renderer skips entries older than the lifetime cap, so
    // wraparound is invisible.
    //
    // text_len < 0 marks an empty slot (initial state and post-clear).
    // The fixed 56-byte text buffer avoids any heap allocation in the
    // push hot path, which keeps the per-tick retrack-detection code
    // allocation-free.  56 chars covers messages like
    // "Player 2 retrack event" (22 chars) and "Hello World" (11 chars)
    // with plenty of headroom for future formatting.
    //
    // No atomic / mutex because the writer (m_lux.forEachChara on the
    // game thread, plus render_tab_impl from the test button on the
    // same game thread) and the reader (render_tab_impl) are all the
    // SAME thread per Horse::GameImGui's threading docs.
    struct HudTextEvent
    {
        char   text[56] = {};       // null-terminated; empty when len < 0
        int    text_len = -1;       // -1 = empty slot, else strlen(text)
        double time     = 0.0;      // ImGui::GetTime() at push moment
    };
    static constexpr size_t kHudTextEventCount    = 8;
    static constexpr double kHudTextEventLifetime = 1.5;   // seconds
    HudTextEvent m_hud_text_events[kHudTextEventCount]{};
    size_t       m_hud_text_event_head = 0;

    // Push a transient text line onto the overlay queue.  Truncates
    // strings longer than the slot capacity, which is fine — these
    // are user-facing diagnostic banners, not log lines.  Safe to
    // call from any game-thread code (cockpit hook, button handler,
    // detector).
    void push_hud_text_event(const char* msg)
    {
        if (!msg) return;
        auto& slot   = m_hud_text_events[m_hud_text_event_head];
        const auto n = std::min<size_t>(
            std::strlen(msg), sizeof(slot.text) - 1);
        std::memcpy(slot.text, msg, n);
        slot.text[n] = '\0';
        slot.text_len = static_cast<int>(n);
        slot.time     = ImGui::GetTime();
        m_hud_text_event_head =
            (m_hud_text_event_head + 1) % kHudTextEventCount;
    }

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
        // Box-visibility filter triple (see m_only_show_active block).
        // Default: master narrow ON, both per-list overrides OFF — gives
        // the engine-truth "what's hitting RIGHT NOW" view on first
        // launch.  The legacy keys (`damage_active_only`,
        // `show_unused_hurtboxes`) are silently dropped; users who had
        // them set to non-default values will land on the new defaults.
        m_only_show_active       .store(S.get_bool ("only_show_active",     true ));
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
        m_trail_frames           .store(S.get_int  ("persistent_trail_frames", 30   ));

        // --- Camera tab -------------------------------------------
        m_ansel_always_allowed   .store(S.get_bool ("ansel_always_allowed",  true ));
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
        m_show_retrack_events    .store(S.get_bool ("show_retrack_events",   false));

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

        // GameMode "Auto disable online" toggle.  Default ON.
        // Persists so a user who deliberately turns it off doesn't
        // have to re-disable on every launch.  See
        // horselib/GameMode.hpp for the full rationale.
        Horse::GameMode::instance().set_auto_disable_online(
            S.get_bool("gamemode_auto_disable_online", true));
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
        S.set("only_show_active",      m_only_show_active.load());
        S.set("hit_flash_frames",      m_flash_frames.load());
        S.set("thickness",             m_thickness.load());
        S.set("line_batcher_slot_hit",  static_cast<int>(m_slot_hit.load()));
        S.set("line_batcher_slot_hurt", static_cast<int>(m_slot_hurt.load()));
        S.set("persistent_trail_frames", m_trail_frames.load());

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
        S.set("show_retrack_events",   m_show_retrack_events.load());

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

        // GameMode "Auto disable online" — see load path for the
        // default rationale.
        S.set("gamemode_auto_disable_online",
              Horse::GameMode::instance().auto_disable_online());

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
        //
        // F7 / F6 both honour the General-tab "Auto disable online"
        // gate — if we're in a Ranked / Casual match with the gate on,
        // pressing the hotkey is a no-op (with a one-line log so the
        // user knows their press was ignored, not lost).  This matches
        // the UI checkbox behaviour: the ImGui control is greyed out
        // and clicking does nothing; the hotkey shouldn't be a
        // back-door around that.
        register_keydown_event(Input::Key::F7, no_mods, [this]() {
            if (Horse::GameMode::instance().should_force_disable_features())
            {
                Output::send<LogLevel::Default>(
                    STR("[HorseMod] F7 ignored — Free-fly camera is "
                        "auto-disabled in online matches.\n"));
                return;
            }
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
            if (Horse::GameMode::instance().should_force_disable_features())
            {
                Output::send<LogLevel::Default>(
                    STR("[HorseMod] F6 ignored — Freeze frame is "
                        "auto-disabled in online matches.\n"));
                return;
            }
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

        // Tear down the SlipOut runtime-gate PolyHook detour.
        // Idempotent if install never succeeded.
        Horse::HasSubProviderEntryHook::instance().uninstall();
        Horse::EBTracer::instance().uninstall();

        // Tear down the SetPresence post-hook so the lambda doesn't
        // fire on a freed cached path-string after dllmain unload.
        // Idempotent.
        Horse::GameMode::instance().uninstall_hook();

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
        // chokepoint for ALL 5 BattleRule overrides — the launcher's
        // Start method reads the data-table cache and applies the
        // per-match rules; we hook it to write our desired
        // BattleRule.<X> values into that cache right before the
        // original runs.  Caveat: this only fires on the HOST in
        // online lobbies (the joiner's match init bypasses the
        // launcher.Start path).  Sufficient for offline / training but
        // for online SlipOut we also install HasSubProviderEntryHook
        // below which IS host/joiner symmetric.
        Horse::LuxBattleLauncherStartHook::instance().install();

        // Install the C++-level "is slip-out suppressed?" runtime-gate
        // hook.  This is the deeper, host-joiner-symmetric override
        // for the SlipOut policy specifically — every client runs the
        // chara-init function that calls
        // LuxBattleChara_HasSubProviderEntryOfType0x3e, so PolyHooking
        // it gives both peers the same answer regardless of which side
        // initiated the match.  See the file-header doc for the full
        // rationale and the link to the previous failed-test
        // investigation.
        Horse::HasSubProviderEntryHook::instance().install();

        // Diagnostic: install runtime tracer that hooks the four
        // top-level engine functions in the world tick to identify
        // which one writes chara+0x16EB (multi-hit lockout) — static
        // analysis can't find it.  Logs 0->1 transitions per chara
        // per function.  Once the writer is identified, this can be
        // removed and the natural mechanism replicated in step mode.
        Horse::EBTracer::instance().install();

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
        const bool game_mode_installed =
            Horse::GameMode::instance().hook_installed();
        if (m_hook_registered && all_reset_registered
            && online_rules_installed && game_mode_installed)
            return;
        if (++m_poll_counter < 60) return;
        m_poll_counter = 0;
        if (!m_hook_registered)        try_register_cockpit_hook();
        if (!all_reset_registered)     try_register_reset_hooks();
        if (!online_rules_installed)
            Horse::OnlineRules::instance().try_install_hooks();
        // GameMode: hook SetPresence so we know which scene the user
        // is in (Training / Replay / online match / etc).  Idempotent
        // and silent on retry — the LuxUIGamePresenceUtil class is a
        // BlueprintFunctionLibrary loaded very early, so this usually
        // succeeds on the first poll attempt.
        if (!game_mode_installed)
            (void)Horse::GameMode::instance().try_install_hook();
    }

private:
    void try_register_cockpit_hook()
    {
        Horse::Obj cockpit = m_lux.cockpit();
        if (!cockpit) return;

        UClass* klass = cockpit.raw()->GetClassPrivate();
        if (!klass) return;

        m_hook_path = klass->GetPathName() + STR(":Update");

        // CRITICAL: pre-validate the UFunction exists before calling
        // RegisterHook(path).  UE4SS's path overload (UObjectGlobals.cpp
        // line 859) calls StaticFindObject<UFunction*> with no null check
        // and then dereferences the result inside the UFunction* overload
        // at line 810 (Function->GetFunc()) — null deref crashes the
        // game.  Worse, even with a valid UFunction the inner overload
        // can THROW std::runtime_error if the function isn't FUNC_Native
        // and isn't ProcessInternal-routed (line 855).  An uncaught
        // exception across the DLL boundary tears down the whole mod
        // (and on some MSVC configs, the host process).
        //
        // Pre-checking here means a not-yet-loaded class just retries
        // next poll tick.  The try/catch around RegisterHook below
        // catches the FUNC_Native mismatch case and downgrades it to a
        // log line so we don't crash the game on an unexpected mod /
        // engine version mismatch.
        UFunction* fn = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, m_hook_path);
        if (!fn)
        {
            // Class was found but its :Update UFunction isn't loaded yet.
            // Will retry next poll tick.  Log only at Verbose so we don't
            // spam the log during the seconds-long window before the
            // cockpit blueprint finishes registering.
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] Cockpit UFunction '{}' not yet loaded; "
                    "will retry on next poll tick.\n"), m_hook_path);
            return;
        }

        Output::send<LogLevel::Verbose>(STR("[HorseMod] Registering hook: {}\n"), m_hook_path);

        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext& ctx, void*) {
                if (auto* self = s_instance.load(std::memory_order_acquire))
                    self->on_cockpit_update_pre(ctx.Context);
            };
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {};

        // Wrap RegisterHook in try/catch — the underlying UE4SS code
        // throws std::runtime_error if the UFunction isn't a hookable
        // shape (see UObjectGlobals.cpp:855).  We don't want that
        // exception escaping into UE4SS's mod loop.
        try
        {
            m_hook_ids = UObjectGlobals::RegisterHook(m_hook_path, pre_cb, post_cb, nullptr);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(
                STR("[HorseMod] RegisterHook threw on '{}': {}\n"),
                m_hook_path, RC::to_generic_string(e.what()));
            // Don't set m_hook_registered — poll loop will skip this
            // path on retry once the underlying issue is fixed.  In
            // practice an exception here means the engine version is
            // wrong and we won't recover, but we'd rather log forever
            // than crash forever.
            return;
        }

        // RegisterHook returns {0, 0} for the global-script-hook path
        // (UObjectGlobals.cpp:842 increments before assigning, so the
        // smallest legitimate ID is 1).  An all-zero pair is therefore
        // a sentinel for "registration silently no-op'd" — defensively
        // refuse to mark registered so we keep retrying.
        if (m_hook_ids.first == 0 && m_hook_ids.second == 0)
        {
            Output::send<LogLevel::Warning>(
                STR("[HorseMod] RegisterHook returned (0,0) for '{}' — "
                    "treating as failure.\n"), m_hook_path);
            return;
        }

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

            // RegisterHook can throw std::runtime_error if the resolved
            // UFunction isn't a hookable shape (see UObjectGlobals.cpp:855).
            // The pre-check above covers the common "not loaded yet" case
            // but not e.g. a Blueprint-only UFunction that doesn't qualify
            // as ProcessInternal-routed.  Keep the exception inside this
            // function rather than letting it climb out into UE4SS.
            try
            {
                slot.ids = UObjectGlobals::RegisterHook(
                    slot.func_path, pre_cb, post_cb, tag);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(
                    STR("[HorseMod] Reset-hook RegisterHook threw on '{}': {}\n"),
                    slot.func_path, RC::to_generic_string(e.what()));
                // Mark this slot registered=false so we retry next
                // poll tick if the underlying issue is transient.
                continue;
            }
            if (slot.ids.first == 0 && slot.ids.second == 0)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] Reset-hook RegisterHook returned (0,0) "
                        "for '{}' — treating as failure.\n"),
                    slot.func_path);
                continue;
            }
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
    // ----------------------------------------------------------------
    // Online-match feature gate — force-disable the four "competitive"
    // features (lock camera, free-fly, freeze frame, slow motion) when
    // the user is in a Ranked / Casual online match AND has "Auto
    // disable online" enabled in the General tab.  Idempotent — calling
    // disable() on an already-disabled feature is a no-op.  Called from
    // on_cockpit_update_pre BEFORE the normal apply_* / frame_step_apply
    // / free_camera_apply chain so the rest of those helpers see the
    // already-cleared atomics and produce no work.
    //
    // Each branch logs once on the OFF transition (was-on + now-forced-
    // off) at Default level so the user can confirm the gate engaged.
    // After that, while the match continues, repeated calls are silent
    // because the underlying atomic is already false.
    // ----------------------------------------------------------------
    // Subset of apply_online_forced_disable() that ONLY clears the
    // time-related features (freeze frame, slow-motion, step queue).
    // Called from the presence-transition watcher in
    // on_cockpit_update_pre as a safety net against black-screen /
    // broken-camera-init bugs that happen when SpeedControl patches
    // stay applied while SC6 tears down + rebuilds BattleManager and
    // chara actors during a mode change.
    //
    // Why we don't clear camera-lock + free-fly here (unlike the
    // full apply_online_forced_disable):
    //   - Camera lock is a static bytepatch (CamLock).  It doesn't
    //     interfere with actor lifecycle — the engine's camera
    //     stores still update the underlying memory, our patch just
    //     no-ops the writer.  Carrying it across a transition is
    //     harmless.
    //   - Free-fly camera owns the camera-lock state machine; it
    //     too is harmless across transitions because the cockpit
    //     hook has its own resolve-on-first-use logic for the new
    //     mode's camera manager.
    //
    // Freeze + slow-mo are different because they install into
    // PerFrameTick / replay tick / cursor advance — exactly the
    // paths that get re-entered from the new mode's chara
    // initialization.  Suppressing those during init breaks setup.
    void clear_time_features_on_transition()
    {
        if (m_freeze_frame.load() || m_step_pending.load() > 0)
        {
            m_freeze_frame.store(false);
            m_step_pending.store(0);
            m_step_expecting.store(false);
            m_step_witness.valid = false;
            m_step_dwell = 0;
        }
        if (m_speed_enabled.load() || m_speed_control.is_enabled())
        {
            m_speed_enabled.store(false);
            m_speed_control.disable();
        }
        // World-tick gate: same hazard as the SpeedControl patches —
        // a presence transition rebuilds BattleManager + chara actors,
        // and a stale gate "frozen" state would block PerFrameTick on
        // the new mode's first tick (= black screen).  Disable so the
        // engine runs at native rate during the transition; user re-
        // engages freeze/step manually after the new mode loads.
        if (m_world_tick_gate.is_enabled())
            m_world_tick_gate.disable();
    }

    // Per-tick enforcement of all four gated features while the
    // online safety gate is engaged.
    //
    // All four (Lock camera, Free-fly, Freeze, Slow-motion) follow
    // the same pattern:
    //   1. Set to OFF the first time the gate fires this match
    //      (the per-tick clamp is idempotent — once off, the inner
    //      `if` short-circuits on subsequent ticks).
    //   2. LOCKED OFF for the duration of the gate's engagement
    //      (UI shows them struck-through + BeginDisabled; if any
    //      back-door write somehow flips the atom, the next tick
    //      clamps it back).
    //   3. Stay off after the gate disengages — nothing re-stores
    //      them automatically.  The user manually re-engages.
    //
    // This matches the user's mental model: "auto-disable means
    // these are unavailable in online matches, and I'll re-enable
    // them myself if I want them after the match ends".
    void apply_online_forced_disable()
    {
        // ---- Lock camera position --------------------------------
        // Two pieces of state to keep coherent:
        //   - m_lock_camera (the user's "preferred" toggle state)
        //   - m_cam_lock    (the actual BytePatch enable/disable)
        // We force m_cam_lock off and clear m_lock_camera so the UI
        // checkbox (which reads m_cam_lock.is_enabled()) shows OFF
        // and the persisted setting reflects the gate-induced state.
        if (m_cam_lock.is_enabled() || m_lock_camera.load())
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Lock camera position\n"));
            m_lock_camera.store(false);
            m_cam_lock.set(false);
        }

        // ---- Free-fly camera -------------------------------------
        // Toggling m_free_camera_enabled OFF here causes free_camera_apply()
        // to take its "want_off" branch on the next call, which releases
        // the underlying CamLock + restores the engine camera path.
        if (m_free_camera_enabled.load() || m_free_camera.is_enabled())
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Free-fly camera\n"));
            m_free_camera_enabled.store(false);
            // Don't call m_free_camera.set(false, ...) directly here —
            // it needs the live PCM pointer which free_camera_apply
            // already resolves.  Letting that helper do the actual
            // state-machine work keeps the two ownership rules
            // coherent (free_camera_apply is the ONLY place that calls
            // m_free_camera.set).
        }

        // ---- Freeze frame ----------------------------------------
        // Clear the freeze atomic and any pending step queue.  The
        // frame_step_apply driver picks this up on the next tick and
        // restores speedval to the slow-mo base (or 1.0 if slow-mo
        // is off — and we're about to force that off too).
        if (m_freeze_frame.load() || m_step_pending.load() > 0)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Freeze frame\n"));
            m_freeze_frame.store(false);
            m_step_pending.store(0);
            m_step_expecting.store(false);
            m_step_witness.valid = false;
            m_step_dwell = 0;
        }
        if (m_world_tick_gate.is_enabled())
        {
            // Don't double-log if the freeze-frame branch above already
            // covered the user-visible "force-disabling" message; the gate
            // disable is implementation detail of the same feature.
            m_world_tick_gate.disable();
        }

        // ---- Slow motion -----------------------------------------
        // Match the UI checkbox callback's behaviour for "slow motion
        // turned off": clear m_speed_enabled, then disable the
        // SpeedControl patches (which resets the shared speedval
        // back to 1.0 — see SpeedControl::disable()).
        if (m_speed_enabled.load() || m_speed_control.is_enabled())
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Slow motion\n"));
            m_speed_enabled.store(false);
            m_speed_control.disable();
        }
    }

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
    // ==========================================================================
    // STEP PROTOCOL (current state, 2026-05)
    // ==========================================================================
    // The cockpit pre-hook fires on the UMG widget tick — AFTER the world
    // tick of the same UE4 frame (UWorld::Tick → FTickTaskManager → SlateApp
    // → UMG → render).  So any speedval write here takes effect on the NEXT
    // UE4 frame's actor ticks.  That timing constraint dictates the 2-tick
    // step state machine below.
    //
    // STEP TICK A (pre-hook fires; world will tick at speedval=1.0 next):
    //   write speedval = 1.0
    //   clear VMFreezeByte if HorseMod owns it
    //   set m_step_expecting = true
    //
    // STEP TICK B (next pre-hook; world will be frozen on next world tick):
    //   write speedval = 0.0
    //   set VMFreezeByte = 1 if not currently owned
    //   pending--, m_step_expecting = false
    //
    // KNOWN OPEN ISSUES (2026-05):
    //   * Multi-hit moves only register the FIRST hit when frame-stepped
    //     in training mode.  Sites 19/20/21/22 (replay-pipeline gates +
    //     chara TickActor entry-RET) are enabled but did not fix this.
    //     A speculative BattleAdvanceFlag override (force flOutBlendW0=1,
    //     nOutModeTag=0 at step Tick A to defeat the AND-of-three gate
    //     at PerFrameTick step 3) was tried and reverted — empirically
    //     didn't fix it, AND had side effects during normal gameplay.
    //     Root cause is deeper in the hit-classifier or per-cell hit-mask
    //     advance path; needs targeted investigation.
    //   * Held inputs may not refresh correctly during step.  Likely
    //     related to the multi-hit miss above (shared upstream cause).
    //   * GetTimeDilationScalar Path A (chara+0x3510 < 0 = super-freeze /
    //     soul-charge cinematic / KO replay) bypasses speedval.  Stepping
    //     during these phases advances at engine-controlled rates instead
    //     of 1.0 × PlaybackSpeed.
    //   * AdvanceLaneFrameStep advances by dt × pLane[+0x30] (PlaybackSpeed).
    //     Moves with non-unity playback speed advance by != 1.0 anim
    //     frames per step.  Matches native gameplay; by-engine design.
    //
    // State machine (2-tick cycle):
    //
    //   click(F6)  m_step_pending++             (sets target=1.0 next tick)
    //   tick A     expecting=false: target=1.0; expecting=true
    //   tick B     expecting=true:  target=base; counter--; expecting=false
    //
    // Two cockpit ticks per advanced game frame because the engine reads
    // speedval inside its world tick — we lift the freeze, world ticks
    // once at full speed, we re-apply the freeze.

    // Snapshot the step-mode world-tick witness (per-lane tick counters
    // at lane+0x04 for both charas).  Returns true if at least one
    // counter was successfully read.  Marks `out.valid` accordingly so
    // a later compare can short-circuit on "no usable snapshot".
    bool capture_step_world_tick_witness(StepWorldTickWitness& out) noexcept
    {
        out = StepWorldTickWitness{};
        bool any = false;
        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* chara = Horse::KHitWalker::charaSlotFromGlobal(pi);
            if (!chara) continue;
            auto* b = reinterpret_cast<const uint8_t*>(chara);
            int32_t l0 = 0, l1 = 0;
            const bool ok0 = Horse::SafeReadInt32(b + 0x444F0 + 0x04, &l0);
            const bool ok1 = Horse::SafeReadInt32(b + 0x44958 + 0x04, &l1);
            if (!ok0 || !ok1) continue;
            if (pi == 0) { out.p0_lane0_tickctr = l0; out.p0_lane1_tickctr = l1; }
            else         { out.p1_lane0_tickctr = l0; out.p1_lane1_tickctr = l1; }
            any = true;
        }
        out.valid = any;
        return any;
    }

    // Compare current witness against `prev`.  Returns true when the
    // world has ticked since `prev` was captured (any lane counter
    // changed), OR when we cannot measure (no prior snapshot or
    // current snapshot fails) — the conservative "assume ticked"
    // fallback avoids permanent state-machine lockup if the chara
    // struct disappears mid-step.
    bool world_ticked_since(const StepWorldTickWitness& prev) noexcept
    {
        if (!prev.valid) return true;
        StepWorldTickWitness cur{};
        if (!capture_step_world_tick_witness(cur)) return true;
        return prev.p0_lane0_tickctr != cur.p0_lane0_tickctr
            || prev.p0_lane1_tickctr != cur.p0_lane1_tickctr
            || prev.p1_lane0_tickctr != cur.p1_lane0_tickctr
            || prev.p1_lane1_tickctr != cur.p1_lane1_tickctr;
    }

    void frame_step_apply()
    {
        const bool freeze     = m_freeze_frame.load();
        const bool slow_mo    = m_speed_enabled.load();
        const int  pending    = m_step_pending.load();
        // (m_step_expecting / m_step_witness / m_step_dwell are dormant —
        // the 2-tick state machine they belonged to is replaced by the
        // WorldTickGate-driven path.  Field removal is proposal step 4-5.)

        // -------------------------------------------------------------------
        // Compute target speedval for this cockpit tick.
        //
        // Priority chain (highest first):
        //   1. Frame-step in flight        — alternate 1.0 / base over 2 ticks
        //   2. Freeze                       — target = 0.0
        //   3. Frame-stepped slow-motion    — target = 0.0 or 1.0 per tick
        //                                     (controlled by m_slow_mo_accumulator;
        //                                      see member's plate for rationale)
        //   4. Otherwise                    — target = 1.0 (native speed)
        //
        // The frame-stepped slow-mo replaces the old dt-scale slow-mo
        // (which used to write fractional speedvals like 0.5).  Every
        // tick is now a CLEAN 0.0 or 1.0 — no fractional dt, so
        // multi-hit move cells resolve at integer frame boundaries
        // exactly like they would at native speed.  See the member's
        // plate for the trade-off discussion.
        // -------------------------------------------------------------------
        float target;
        bool  need_active;
        bool  gate_drives_this_tick = false;

        if (pending > 0 || freeze)
        {
            // ---- WORLD-TICK-GATE-DRIVEN PATH (2026-05-05) ---------------
            // Freeze + frame-step are now driven by Horse::WorldTickGate
            // (single PerFrameTick gate).  speedval stays at 1.0 (the
            // dt-multiply sites at 1/3/4/5/6/8 become no-ops, eliminating
            // the dt=0 contamination that was breaking multi-hit moves
            // under frame-step), and the gate's int32_t step-credit slot
            // is the sole "skip this frame" mechanism.
            //
            // F6 press cadence:
            //   * Each F6 press bumps m_step_pending (existing hotkey
            //     handler is unchanged).
            //   * Here we DRAIN m_step_pending into the gate as step
            //     credits — each PerFrameTick call atomically decrements
            //     the slot and runs the displaced prologue.
            //
            // After credits are exhausted the slot is 0 = frozen again,
            // PerFrameTick bails until the next F6 press.  No 2-tick
            // state machine, no witness/dwell counter, no VMFreezeByte
            // engagement, no try_clear_multi_hit_lockout_for_step hack.
            //
            // SpeedControl stays DISABLED in this path: dt-multiply sites
            // (1/3/4/5/6/8) and replay-side sites (10/11/12/13+) only
            // fire when SpeedControl is enabled, and we don't want any
            // of them touching state during freeze/step.
            //
            // KHitWalker's hit-flash drain is now keyed on
            // g_LuxBattle_FrameCounter (incremented at the end of
            // PerFrameTick), so it tracks the gate exactly: counter
            // halts under freeze, advances by 1 per step credit
            // consumed.  No SpeedControl coupling.
            if (!m_world_tick_gate.is_resolved())
                m_world_tick_gate.resolve();
            if (m_world_tick_gate.is_resolved() &&
                !m_world_tick_gate.is_enabled())
                m_world_tick_gate.enable();

            if (pending > 0)
            {
                // Move ALL pending presses into the gate at once.  add_step
                // is atomic (fetch_add on the int32_t slot via atomic_ref),
                // so we won't race the trampoline's lock-dec on the same
                // memory.  exchange clears m_step_pending to 0 — F6 hotkey
                // presses that arrive between this and the next cockpit
                // tick land in m_step_pending and get committed next time.
                const int n = m_step_pending.exchange(0);
                if (n > 0) m_world_tick_gate.add_step(n);
            }
            // else: pure freeze with no NEW presses this tick.  Do NOT write
            // 0 to the slot — the slot is the LIVE step-credit counter (the
            // trampoline lock-decs it each world tick), so a Step N command
            // from a previous cockpit tick may still have credits draining.
            // Writing 0 here would clobber e.g. "9 credits remaining" and
            // collapse a Step-10 into a Step-1.  Steady-state behaviour:
            //   * Just-enabled gate: WorldTickGate::enable() already wrote
            //     policy=0, so we start frozen with no extra work.
            //   * After the trampoline drains all credits: slot lands at 0
            //     naturally; subsequent ticks bail without anyone touching
            //     it from C++.

            // Stale 2-tick state machine fields — reset so a future
            // path that reads them doesn't pick up garbage from the old
            // mode.  The full removal of expecting/witness/dwell is
            // proposal step 4-5 (cleanup phase).
            m_step_expecting.store(false);
            m_step_witness.valid = false;
            m_step_dwell         = 0;
            m_slow_mo_accumulator = 0.0f;

            // SpeedControl stays out of the picture while the gate drives.
            target                = 1.0f;
            need_active           = false;
            gate_drives_this_tick = true;
        }
        else if (slow_mo)
        {
            // ---- SLOW-MO ROUTED THROUGH THE GATE (2026-05-05) ----------
            // The accumulator-driven cadence (each cockpit tick decides
            // "go" or "stop") used to write speedval = 1.0 / 0.0 and rely
            // on Site 9 to bail PerFrameTick on the 0 ticks.  With Site 9
            // moved out of SpeedControl, that fall-through stops working
            // — speedval = 0 lets PerFrameTick run, but the dt-multiply
            // sites at 1/3/4/5/6/8 produce dt=0 inside it, re-introducing
            // the same contamination that broke multi-hit moves under
            // frame-step.  Instead, drive the cadence through the
            // WorldTickGate exactly like F6 step does: "go" tick =
            // add_step(1), "stop" tick = set_frozen().
            //
            // Net effect: at S=0.5, half the cockpit ticks each produce
            // ONE PerFrameTick at native dt (= half-rate world advance),
            // the other half bail at the gate (= world frozen).  Every
            // game frame the engine sees is integer-dt — multi-hit moves
            // resolve correctly even in slow-mo.  Trade-off (per the
            // proposal's slow-mo plate): visuals are choppier than the
            // old fractional-dt slow-mo at very low slider values; for
            // hitbox analysis this is the better trade.
            const float S = m_speed_value.load();
            if (S >= 1.0f)
            {
                // Slider at or past native speed — no slowdown to apply.
                // Run at full speed, gate stays disabled so the engine's
                // PerFrameTick prologue runs unconditionally (= no per-
                // tick patch flipping in the steady state).
                target                  = 1.0f;
                m_slow_mo_accumulator   = 0.0f;
                need_active             = false;
            }
            else
            {
                // S in [0.0, 1.0): use the gate.  Ensure it's resolved +
                // enabled, then publish the tick decision (add_step on
                // "go", set_frozen on "stop") via the same atomics the
                // freeze/step path uses.
                if (!m_world_tick_gate.is_resolved())
                    m_world_tick_gate.resolve();
                if (m_world_tick_gate.is_resolved() &&
                    !m_world_tick_gate.is_enabled())
                    m_world_tick_gate.enable();

                bool go_this_tick;
                if (S <= 0.0f)
                {
                    // Slider at zero — collapse to freeze for this tick.
                    // Same end-state as the dedicated freeze toggle, kept
                    // here so the slider's edges have no discontinuity.
                    go_this_tick          = false;
                    m_slow_mo_accumulator = 0.0f;
                }
                else
                {
                    // Step-based cadence.  Accumulate slider value;
                    // emit a "go" tick whenever the accumulator crosses
                    // 1.0, otherwise emit a "stop" tick.
                    const float new_accum = m_slow_mo_accumulator + S;
                    if (new_accum >= 1.0f)
                    {
                        go_this_tick          = true;
                        m_slow_mo_accumulator = new_accum - 1.0f;
                    }
                    else
                    {
                        go_this_tick          = false;
                        m_slow_mo_accumulator = new_accum;
                    }
                }

                if (go_this_tick)
                    m_world_tick_gate.add_step(1);
                else
                    m_world_tick_gate.set_frozen();

                target                = 1.0f;       // never write speedval=0
                need_active           = false;
                gate_drives_this_tick = true;
            }
        }
        else
        {
            // No freeze, no slow-mo, no step queued — native speed.
            target                  = 1.0f;
            m_slow_mo_accumulator   = 0.0f;
            need_active             = false;
        }

        // ---- World-tick gate disengage --------------------------------
        // When this tick is NOT gate-driven (native or pure slow-mo), the
        // gate must be disabled so the engine's PerFrameTick prologue runs
        // unconditionally.  Idempotent — disable() is a no-op when already
        // disabled, no per-tick patch flipping in the steady state.
        if (!gate_drives_this_tick && m_world_tick_gate.is_enabled())
            m_world_tick_gate.disable();

        // Publish the tick kind for the render-thread UI cadence
        // indicator.  Only meaningful while slow-mo is on; other
        // states map to Inactive so the UI shows a neutral state
        // (no flickering during freeze, native, or step-only).
        {
            TickKind kind = TickKind::Inactive;
            if (slow_mo && !freeze && pending == 0 && gate_drives_this_tick)
            {
                const float S = m_speed_value.load();
                if (S > 0.0f && S < 1.0f)
                {
                    // gate.policy() > 0 means we just published a step
                    // credit (this tick will run); == 0 means we set
                    // frozen (this tick will bail).  This mirrors the
                    // old `target >= 0.5` key but reads the actual
                    // decision we just committed to the gate.
                    kind = (m_world_tick_gate.policy() > 0)
                               ? TickKind::Go
                               : TickKind::Stop;
                }
            }
            m_last_tick_kind.store(static_cast<uint8_t>(kind),
                                   std::memory_order_release);
        }

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
            // The gate-driven path NEVER engages SC6's internal VM freeze:
            // setting bVMFreezeByte=1 makes LuxMoveVM_GetTimeDilationScalar
            // return 0 for ALL callers, which is exactly the "dt=0
            // contamination" the new gate is meant to eliminate.  When the
            // gate drives this tick, force want_freeze=false so we'll
            // actively clear the byte if we previously owned it (e.g. a
            // prior slow-mo session that engaged it).
            const bool want_freeze =
                gate_drives_this_tick ? false : (target == 0.0f);
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

        // BattleAdvanceFlag override: REMOVED 2026-05-02.
        //   Earlier hypothesis: PerFrameTick's BattleAdvanceFlag gate
        //   (step 3 of 10, AND-of-three on flOutBlendW0=0,
        //   nOutModeTag=2, MasterModeFlag=3) was flipping to 0 during
        //   step Tick A from stale VMFreezeRecord state and skipping
        //   LuxBattle_TickCharaInput.  Override force-wrote flOutBlendW0=1
        //   and nOutModeTag=0 at every cockpit tick where target==1.0.
        //
        //   Empirically didn't fix the multi-hit-miss bug, AND has the
        //   side effect of overriding the engine's hit-stop blend output
        //   during NORMAL gameplay (target=1.0 in native play too), which
        //   can disrupt hit-stop visuals and the move-VM's hit-stop-aware
        //   logic.  Reverted.  The actual root cause of multi-hit miss
        //   is deeper in the hit-classifier path; investigation pending.

        // BattlePauseRequest call removed 2026-04-27 — the underlying
        // ULuxBattleFunctionLibrary::SetBattlePause UFunction merely sets
        // an audio-state bit at chara+0x394 (per the Ghidra plate on
        // LuxBattleChara_SyncAudioActiveState_FromBattleFlags), it doesn't
        // actually halt the world tick.  Empirically observed to break
        // Soul Charge mid-move because SC's audio-cue-driven phase
        // transitions stall when audio is force-muted.  Sites 1-16 +
        // VMFreezeByte (above) remain the actual freeze mechanism — see
        // the BattlePauseRequest removal comment in the member list.
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

    // try_force_battle_advance_flag: REMOVED 2026-05-02 — see comment
    // block in frame_step_apply for the rationale (didn't fix multi-hit
    // miss, had side effects during normal gameplay).

    // ------------------------------------------------------------------
    // Multi-hit lockout-clear workaround (2026-05).
    // ------------------------------------------------------------------
    // The classifier (LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100)
    // gates hits on the ATTACKER chara's flag tuple:
    //
    //   attacker[+0x16E5] != 0   // attack-active: in an attack
    //   attacker[+0x16EA] != 0   // ready-to-hit:  in an ACTIVE PHASE
    //   attacker[+0x16EB] == 0   // lockout:       no hit yet this phase
    //   attacker[+0x16FE] == 0   // lockout 2
    //   *attacker[+0x44058] != 0 // own-cell pointer
    //
    // When a hit lands the engine sets +0x16EA=1, +0x16EB=1 atomically.
    // In NATIVE PLAY between hits of a multi-hit move, hit-stop fires
    // (chara+0x3500 -> 0, chara+0x3508 > 0); hit-stop ending re-triggers
    // sub-handlers that clear +0x16EB back to 0 so the NEXT active phase
    // can register a hit.
    //
    // The user's frame-step diagnostic log (Siegfried move 0x015A,
    // lane=1, 1.00x) shows that during stepping HIT-STOP NEVER ENGAGES:
    // chara+0x3500 stays at 1.0 and chara+0x3508 stays at -1 throughout.
    // Hit-stop is the natural clearer; without it +0x16EB latches to 1
    // forever after the first hit and the multi-hit lockout gate blocks
    // all subsequent hit-classifier passes.  Symptom: every step past
    // the first hit's frame fails the gate -> "the move only hits once".
    //
    // FIX: at every step Tick A (pre-hook BEFORE the next world tick),
    // for each chara, if (16E5==1 && 16EA==0 && 16EB==1) → clear 16EB to
    // 0.  This emulates the engine's between-active-phases cleanup that
    // would normally come out of hit-stop end.  Conditions:
    //
    //   16E5==1   = the chara IS attacking.  Don't clear lockout if not
    //                (e.g. defender just past a hit they took — irrelevant).
    //   16EA==0   = no active phase right now.  Clearing during an active
    //                phase would let a single hitbox register repeatedly
    //                within the SAME phase (double-hit bug).
    //   16EB==1   = lockout is currently latched.  Skip if already clear.
    //
    // Also clears +0x16FE (the secondary lockout flag in the same gate
    // tuple) under the same condition.  Sites 1402fd04b and 1402fd054
    // in the binary write both +0x16EB and +0x16FE in the same
    // CommitMoveEnd code path, confirming they're a paired-clear.
    //
    // SEH-wrapped because the chara pointer can be null mid-load.
    //
    // ----- Multi-hit lockout clearer (gated, cadence-tracked) ------------
    // BACKGROUND
    // ----------
    // SC6 has at least three native multi-hit mechanisms:
    //
    //   (A) HIT-STOP PACED.  The bytecode dispatches a hit-stop opcode
    //       (LuxMoveVM_DispatchEffectOp branch at 0x1403794a0) which
    //       queues +0x3504/+0x350c, committed by
    //       LuxBattle_TickHitStopSchedulerAndInputMirror to
    //       +0x3500/+0x3508.  The 1-tick decrement of +0x3508 paces
    //       hits; on its way to 0 a sub-handler clears +0x16EB so the
    //       next active phase can register a hit.
    //
    //   (B) TRANSITION-MOVE PACED.  The move bytecode authors a
    //       16EB-conditional transition target at lane+0x5E, so when
    //       +0x16EB latches, LuxMoveVM_CheckMoveTransitionTiming
    //       overrides the default target with that one and (when the
    //       lane+0x68 threshold meets the other lane's anim cursor)
    //       calls TransitionToMove, whose snapshot section
    //       unconditionally clears +0x16EB.
    //
    //   (C) DEFAULT-TRANSITION PACED.  The move authors lane+0x5A but
    //       NOT lane+0x5E (e.g. Siegfried's 4A+B with default target
    //       0x150 and threshold 46 frames).  CheckMoveTransitionTiming
    //       still fires TransitionToMove when the threshold lands and
    //       16EB clears as part of that.
    //
    // In step mode the speedval=1.0 tick runs the full simulation, so
    // (B) and (C) work just like native — IF the threshold is reached.
    // (A) is the one that consistently breaks during step: hit-stop
    // queues, but the scheduler that consumes the queue gates on
    // VMFreezeByte and on a tight tick cadence that the step rhythm
    // disrupts; the diagnostic log on Siegfried 4A+B shows
    // chara+0x3500 stays at 1.0 and +0x3508 stays at -1 throughout the
    // master window — hit-stop never engages — so 16EB latches at the
    // first hit and stays latched forever.
    //
    // STRATEGY
    // --------
    // This helper is a SAFETY NET, not a reimplementation.  It only
    // fires when ALL of the following hold:
    //   * Chara is attacking            (16E5=1)
    //   * Lockout is latched            (16EB=1)
    //   * Hit-stop is NOT running       (3508 <= 0)
    //                                    — hit-stop would naturally pace
    //                                      and clear 16EB on its own
    //   * No 16EB-conditional override  (lane[+0x5E] == -1 on lane 0/1)
    //                                    — engine path (B) handles this
    //
    // When all gate, we apply the cadence-counted clear: count step
    // ticks where 16EA=0 (engine forced it off due to 16EB), and after
    // kEBLockoutDelay ticks, clear 16EB so the next tick's classifier
    // can re-arm 16EA and the resolver can fire another hit.  This
    // emulates the timing that hit-stop would have produced.
    //
    // The cadence is DATA-DRIVEN per cell, derived from
    // cell+0x46 (HitstunStandingNormal) — read every step, divided by 4.
    // RATIONALE: SC6 doesn't expose an explicit "frames between hits"
    // field anywhere I could locate via static analysis; what IS
    // authored on every cell is hitstun (the defender's stun frames).
    // In fighting-game design, hit-stop (the attacker's stop frames
    // between hits) is typically ~1/4 of hitstun, so we use that as
    // our derived cadence.  For Siegfried 4A+B (cell+0x46 = 30):
    //   K = 30/4 = 7  → 8-frame cycle → hits at anim 18/26/34
    //                    in the [17..39] master window = 3 hits.
    // For shorter-hitstun moves the cadence shrinks proportionally;
    // for single-hit moves the cell's master window is short enough
    // that the second cycle never lands in the active phase, so they
    // still fire only once.  No per-move table required.
    //
    // CAVEAT: this is a HEURISTIC (the /4 ratio).  The engine's exact
    // multi-hit pacing for moves like 4A+B uses a mechanism I could
    // not isolate via static byte search of all common store
    // encodings — the 16EB latch isn't written via direct disp32, it
    // appears to come from indirect addressing or a struct-stamp path
    // (probably inside the hit-application chain in
    // LuxBattleChara_*).  If a move under-/over-fires, the formula
    // is the lever — change /4 to /3 (faster) or /5 (slower).
    static constexpr int kEBLockoutDivisor = 4;
    static constexpr int kEBLockoutFallback = 7;   // when cell read fails
    static constexpr int kEBLockoutMin = 2;        // never below this
    static constexpr int kEBLockoutMax = 30;       // never above this
    static inline int s_eb_lockout_delay[2] = {0, 0};

    static void try_clear_multi_hit_lockout_for_step() noexcept
    {
        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* chara = Horse::KHitWalker::charaSlotFromGlobal(pi);
            if (!chara)
            {
                s_eb_lockout_delay[pi] = 0;
                continue;
            }
            __try
            {
                auto* b = reinterpret_cast<volatile uint8_t*>(chara);
                const uint8_t v_16e5 = b[0x16E5];
                const uint8_t v_16ea = b[0x16EA];
                const uint8_t v_16eb = b[0x16EB];

                // Not attacking, or not locked out: reset cadence.
                if (v_16e5 == 0 || v_16eb == 0)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: hit-stop engaged.  When chara+0x3508 > 0 the
                // engine is in hit-stop and will naturally clear 16EB
                // when the timer expires.  Don't interfere — even
                // clearing 16EB during hit-stop would let the next
                // tick fire another hit through hit-stop, breaking
                // engine semantics.
                int32_t v_3508 = -1;
                std::memcpy(&v_3508,
                            const_cast<const uint8_t*>(b) + 0x3508,
                            sizeof(v_3508));
                if (v_3508 > 0)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: engine has 16EB-conditional transition override
                // authored on the active lane.  CheckMoveTransitionTiming
                // will swap target to lane[+0x5E] when 16EB is latched,
                // and once the threshold is reached TransitionToMove
                // clears 16EB itself.  We must not race that path.
                //
                // Check both lanes — the active attack could be on
                // either.  Lane 0 = chara+0x444F0, lane 1 = +0x44958.
                int16_t lane0_2F = -1, lane1_2F = -1;
                std::memcpy(&lane0_2F,
                            const_cast<const uint8_t*>(b) + 0x444F0 + 0x5E,
                            sizeof(lane0_2F));
                std::memcpy(&lane1_2F,
                            const_cast<const uint8_t*>(b) + 0x44958 + 0x5E,
                            sizeof(lane1_2F));
                if (lane0_2F != -1 || lane1_2F != -1)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: engine has authored a default transition target
                // on the active lane (lane[+0x5A]).  When the bytecode
                // emits CALLCOND 0x07 via LuxMoveVM_DecodeVariadicStreamArgs
                // (instruction at 0x1402FCAF6: MOV [RBX+0x5a], R9W) and
                // the lane[+0x68] threshold is reached, TransitionToMove
                // fires and clears chara+0x16EB itself.  We must not race
                // that path — clearing 16EB heuristically here would let
                // the resolver fire on the current (wrong) cell instead
                // of the cell the engine is about to switch to.
                int16_t lane0_5A = -1, lane1_5A = -1;
                std::memcpy(&lane0_5A,
                            const_cast<const uint8_t*>(b) + 0x444F0 + 0x5A,
                            sizeof(lane0_5A));
                std::memcpy(&lane1_5A,
                            const_cast<const uint8_t*>(b) + 0x44958 + 0x5A,
                            sizeof(lane1_5A));
                if (lane0_5A != -1 || lane1_5A != -1)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: deferred transition target authored via the
                // CALLCOND 0x15 wrapper path (lane[+0xB4]).  Same race
                // concern as 0x5A — let the engine's transition path
                // own the 16EB clear when it has work queued.
                int16_t lane0_B4 = -1, lane1_B4 = -1;
                std::memcpy(&lane0_B4,
                            const_cast<const uint8_t*>(b) + 0x444F0 + 0xB4,
                            sizeof(lane0_B4));
                std::memcpy(&lane1_B4,
                            const_cast<const uint8_t*>(b) + 0x44958 + 0xB4,
                            sizeof(lane1_B4));
                if (lane0_B4 != -1 || lane1_B4 != -1)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // 16EA simultaneously set means the hit JUST fired this
                // step; reset the delay counter so we start counting
                // from this fresh latch.
                if (v_16ea != 0)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // 16E5=1, 16EB=1, 16EA=0 — engine forced 16EA off
                // because of the lockout; classic between-hits state.
                // Compute the per-move cadence threshold from the
                // active cell's HitstunStandingNormal (cell+0x46).
                int delay_threshold = kEBLockoutFallback;
                void* cell_ptr = nullptr;
                std::memcpy(&cell_ptr,
                            const_cast<const uint8_t*>(b) + 0x44058,
                            sizeof(cell_ptr));
                if (cell_ptr)
                {
                    int16_t hitstun = 0;
                    auto* cb = reinterpret_cast<const volatile uint8_t*>(cell_ptr);
                    __try
                    {
                        std::memcpy(&hitstun,
                                    const_cast<const uint8_t*>(cb) + 0x46,
                                    sizeof(hitstun));
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        hitstun = 0;
                    }
                    if (hitstun > 0)
                    {
                        int k = hitstun / kEBLockoutDivisor;
                        if (k < kEBLockoutMin) k = kEBLockoutMin;
                        if (k > kEBLockoutMax) k = kEBLockoutMax;
                        delay_threshold = k;
                    }
                }

                s_eb_lockout_delay[pi]++;
                if (s_eb_lockout_delay[pi] >= delay_threshold)
                {
                    b[0x16EB] = 0;
                    b[0x16FE] = 0;
                    s_eb_lockout_delay[pi] = 0;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Faulted — chara pointer not mapped this frame.  Skip.
                s_eb_lockout_delay[pi] = 0;
            }
        }
    }

    // ------------------------------------------------------------------
    // Per-step diagnostic logger (Time-tab checkbox-gated).
    // ------------------------------------------------------------------
    // Snapshots the chara fields the hit classifier
    // (LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100) reads to
    // decide whether to register a hit, plus the lane-1 anim cursor.
    // Emits ONE LINE PER CHARA per call.  Caller invokes twice per F6
    // step:
    //   "pre"  — before the world tick at speedval=1.0
    //   "post" — after  the world tick at speedval=1.0
    //
    // Reading a "pre"/"post" pair tells you which field changed across
    // exactly one game frame.  For multi-hit miss diagnosis: between
    // the missed hits, fields like +0x16E5/+0x16EA/+0x44058/+0x44048
    // should toggle.  If they don't, that's the leak.
    //
    // All reads SEH-wrapped; a faulting field shows as "??".
    static void log_frame_step_diag(const char* phase, int seq) noexcept
    {
        // Phase string is fixed "pre" or "post" — use a wide-string
        // literal directly instead of a runtime char->wide conversion
        // (UE4SS's STR macro only works on literals).
        const auto phase_w = (phase[0] == 'p' && phase[1] == 'o')
                                ? STR("post")
                                : STR("pre");

        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* chara = Horse::KHitWalker::charaSlotFromGlobal(pi);
            if (!chara) continue;
            auto* b = reinterpret_cast<const uint8_t*>(chara);

            uint8_t  v_16e5 = 0xFF, v_16ea = 0xFF, v_16eb = 0xFF,
                     v_16fe = 0xFF, v_16d2 = 0xFF;
            void*    v_44058 = nullptr;
            void*    v_44048 = nullptr;
            uint64_t v_44058_mask = 0, v_44048_mask = 0;
            int16_t  v_44dc2 = -1, v_4495a = -1;
            float    v_3500 = -1.0f;
            int32_t  v_3508 = -1, v_44db8 = -1;
            float    lane1_anim = -1.0f;
            int16_t  lane1_delta = -1;
            int32_t  lane1_tickctr = -1;
            uint16_t lane1_atend = 0xFFFF, lane1_finished = 0xFFFF;

            using namespace Horse;
            uint8_t kind = 0xFF;
            SafeReadUInt8 (b + 0x23C,   &kind);
            SafeReadUInt8 (b + 0x16E5,  &v_16e5);
            SafeReadUInt8 (b + 0x16EA,  &v_16ea);
            SafeReadUInt8 (b + 0x16EB,  &v_16eb);
            SafeReadUInt8 (b + 0x16FE,  &v_16fe);
            SafeReadUInt8 (b + 0x16D2,  &v_16d2);
            SafeReadPtr   (b + 0x44058, &v_44058);
            SafeReadPtr   (b + 0x44048, &v_44048);
            if (v_44058) SafeReadUInt64(v_44058, &v_44058_mask);
            if (v_44048) SafeReadUInt64(v_44048, &v_44048_mask);
            SafeReadInt16 (b + 0x44DC2, &v_44dc2);
            SafeReadInt16 (b + 0x4495A, &v_4495a);
            SafeReadFloat (b + 0x3500,  &v_3500);
            SafeReadInt32 (b + 0x3508,  &v_3508);
            SafeReadInt32 (b + 0x44DB8, &v_44db8);
            // Lane 1: chara + 0x44958.
            const uint8_t* lane1 = b + 0x44958;
            SafeReadInt32 (lane1 + 0x04, &lane1_tickctr);
            SafeReadFloat (lane1 + 0x08, &lane1_anim);
            SafeReadUInt16(lane1 + 0x1A, &lane1_atend);
            SafeReadInt16 (lane1 + 0x1C, &lane1_delta);
            SafeReadUInt16(lane1 + 0x24, &lane1_finished);

            // CheckMoveTransitionTiming-relevant fields (lane 1 + lane 0).
            // The function early-exits if lane+0x5A == -1 and otherwise
            // uses lane+0x68 as the threshold compared against the OTHER
            // lane's anim frame.  If this function isn't firing CommitMoveEnd
            // (which clears 16EB), one of these is the cause.
            int16_t lane1_2D = -1, lane1_2E = -1, lane1_2F = -1, lane1_30 = -1;
            int16_t lane1_2B = -1;
            float   lane1_68 = -1.0f;
            float   lane0_anim = -1.0f, lane0_20 = -1.0f;
            int16_t lane0_idx = -1, lane0_slot = -1;
            // param_2 is short*, so [0x2D] = byte offset 0x5A, [0x2E] = 0x5C,
            // [0x2F] = 0x5E, [0x30] = 0x60, [0x2B] = 0x56, [0x34] = 0x68.
            SafeReadInt16 (lane1 + 0x5A, &lane1_2D);
            SafeReadInt16 (lane1 + 0x5C, &lane1_2E);
            SafeReadInt16 (lane1 + 0x5E, &lane1_2F);
            SafeReadInt16 (lane1 + 0x60, &lane1_30);
            SafeReadInt16 (lane1 + 0x56, &lane1_2B);
            SafeReadFloat (lane1 + 0x68, &lane1_68);
            const uint8_t* lane0 = b + 0x444F0;
            SafeReadFloat (lane0 + 0x08, &lane0_anim);
            SafeReadFloat (lane0 + 0x20, &lane0_20);
            SafeReadInt16 (lane0 + 0x00, &lane0_idx);
            SafeReadInt16 (lane0 + 0x02, &lane0_slot);

            // Active-lane cursor: chara+0x44068 points to whichever lane is
            // currently driving the move.  Multi-hit logic depends on this
            // pointer matching the lane CheckMoveTransitionTiming sees.
            void* active_lane_cursor = nullptr;
            SafeReadPtr   (b + 0x44068, &active_lane_cursor);

            // chara+0x2130 — MoveExecState (1=running, 2=committed-after-
            // TransitionToMove, 0=cleared by TerminateCurrentMove).  If
            // this ever flips to 2 mid-multi-hit, TransitionToMove fired.
            int32_t v_2130 = -1;
            SafeReadInt32(b + 0x2130, &v_2130);

            // lane1[+0x2C] (= chara + 0x44984): CheckMoveTransitionTiming
            // writes 1 here ONLY AFTER successfully calling TransitionToMove
            // (asm 1402fde6b).  Persistent post-call marker — if this is
            // ever 1 during step, the engine's natural transition fired.
            int32_t lane1_2C = -1;
            SafeReadInt32(lane1 + 0x2C, &lane1_2C);

            RC::Output::send<RC::LogLevel::Default>(
                STR("[FStep] {} #{:>3} P{} kind={:02x}: lane1.anim={:7.2f} d={:+d} tick={} end={}/{} "
                    "16e5={:02x} 16ea={:02x} 16eb={:02x} 16fe={:02x} 16d2={:02x} "
                    "058={:#x}|m={:#018x} 048={:#x}|m={:#018x} "
                    "44dc2={:#06x} 4495a={:#06x} 44db8={} 3500={:.3f} 3508={}\n"),
                phase_w,
                seq, pi + 1, kind,
                lane1_anim, static_cast<int>(lane1_delta), lane1_tickctr,
                static_cast<int>(lane1_atend), static_cast<int>(lane1_finished),
                v_16e5, v_16ea, v_16eb, v_16fe, v_16d2,
                reinterpret_cast<uintptr_t>(v_44058),
                static_cast<unsigned long long>(v_44058_mask),
                reinterpret_cast<uintptr_t>(v_44048),
                static_cast<unsigned long long>(v_44048_mask),
                static_cast<uint16_t>(v_44dc2),
                static_cast<uint16_t>(v_4495a),
                v_44db8, v_3500, v_3508);

            // Second line: transition-timing fields the multi-hit
            // mechanism depends on.  2130/L1+0x2C are post-call markers
            // for the TransitionToMove path (see comments above).
            RC::Output::send<RC::LogLevel::Default>(
                STR("[FStep tx ] {} #{:>3} P{}: activeLane={:#x} "
                    "L1[+5A]={:#06x} [+5C]={:#06x} [+5E]={:#06x} [+60]={:#06x} "
                    "[+56(idxCopy)]={:#06x} [+68(threshold)]={:7.2f} "
                    "[+2C(txfired)]={} 2130(execState)={} | "
                    "L0 idx={} slot={:#06x} anim={:7.2f} +0x20={:7.2f}\n"),
                phase_w, seq, pi + 1,
                reinterpret_cast<uintptr_t>(active_lane_cursor),
                static_cast<uint16_t>(lane1_2D),
                static_cast<uint16_t>(lane1_2E),
                static_cast<uint16_t>(lane1_2F),
                static_cast<uint16_t>(lane1_30),
                static_cast<uint16_t>(lane1_2B),
                lane1_68,
                lane1_2C, v_2130,
                static_cast<int>(lane0_idx),
                static_cast<uint16_t>(lane0_slot),
                lane0_anim, lane0_20);

            // Dump the 0x70-byte LuxBattleAttackCell at chara+0x44058
            // (P1 only, only on "pre" snapshot, only once per step pair).
            // Lets us see the cell's full structure (master window, sub-
            // windows, attack flags, slot mask reference, etc.) without
            // needing to figure out which dump file holds Siegfried's
            // move bank.  The cell's first 8 bytes are the slot mask we
            // already log; the rest contains MasterWindowStart at +0x36,
            // MasterWindowEnd at +0x38, BaseDamage at +0x3A, etc.
            if (pi == 0 && phase[0] == 'p' && phase[1] == 'r' && v_44058)
            {
                uint8_t cell[0x70] = {};
                if (Horse::SafeReadBytes(v_44058, cell, sizeof(cell)))
                {
                    // Format as 7 lines of 16 bytes.
                    for (int row = 0; row < 7; ++row)
                    {
                        const int o = row * 16;
                        RC::Output::send<RC::LogLevel::Default>(
                            STR("[FStep cell] +{:#04x}: "
                                "{:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} "
                                "{:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}\n"),
                            o,
                            cell[o+0],  cell[o+1],  cell[o+2],  cell[o+3],
                            cell[o+4],  cell[o+5],  cell[o+6],  cell[o+7],
                            cell[o+8],  cell[o+9],  cell[o+10], cell[o+11],
                            cell[o+12], cell[o+13], cell[o+14], cell[o+15]);
                    }
                }
            }
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

        // ----------------------------------------------------------------
        // PRESENCE-TRANSITION SAFETY CLEAR
        // ----------------------------------------------------------------
        // Detect ANY scene-presence transition (training -> ranked,
        // ranked -> training, training -> menu, etc.) and on the
        // change, force-clear Freeze and Slow-motion.  This protects
        // against the black-screen / broken-camera-init class of bug
        // that happens when SpeedControl patches stay applied while
        // SC6 tears down the old BattleManager / chara actors and
        // builds new ones.  See m_last_seen_presence's plate above
        // for the full rationale.
        //
        // Crucially: this fires REGARDLESS of the "Auto disable
        // online" toggle — even with the gate off, mode transitions
        // still tear down state that would crash with active patches.
        // The toggle is for ONLINE-MATCH safety, not transition
        // safety; the two concerns happen to overlap on freeze/slow-
        // mo so we run both checks.
        //
        // Once cleared, freeze/slow-mo STAY cleared (no auto-restore
        // on the way back) — the user must re-engage manually.  This
        // matches the user's reported mental model and the comment
        // block on m_last_seen_presence.
        {
            const uint8_t cur = static_cast<uint8_t>(
                Horse::GameMode::instance().current_presence());
            const uint8_t prev = m_last_seen_presence.exchange(
                cur, std::memory_order_acq_rel);
            if (prev != cur)
            {
                using GMP = Horse::GamePresence;
                const GMP from = static_cast<GMP>(prev);
                const GMP to   = static_cast<GMP>(cur);
                // Skip the "init unknown -> first seen" case for
                // logging — it's not a real user-visible transition,
                // just the first time the SetPresence hook fires.
                // We DO still clear freeze/slow-mo on the first fire
                // (see plate on m_last_seen_presence) — the log
                // suppression is purely cosmetic.
                if (from != GMP::Unknown)
                {
                    Output::send<LogLevel::Default>(
                        STR("[HorseMod] presence transition {} -> {} — "
                            "force-clearing Freeze frame + Slow-motion + "
                            "step queue (manual re-enable required)\n"),
                        Horse::presence_name(from),
                        Horse::presence_name(to));
                }
                clear_time_features_on_transition();
            }
        }

        // ----------------------------------------------------------------
        // ONLINE-MATCH FEATURE GATE
        // ----------------------------------------------------------------
        // If the user's "Auto disable online" toggle is on AND we're in
        // a Ranked or Casual online match, force-disable a specific
        // subset of features that would give the user an unfair
        // perceptual / simulation-rate advantage:
        //
        //   - Lock camera position
        //   - Free-fly camera
        //   - Freeze frame
        //   - Slow motion
        //
        // We force-disable these every tick (idempotent calls — if the
        // feature is already off, the disable is a no-op) so even if
        // the user finds a way to flip the underlying atomic via some
        // other code path, the next cockpit tick clamps it back off.
        // The UI side is gated separately (see render_camera_tab and
        // render_time_tab) — both checkboxes go BeginDisabled() while
        // this predicate is true.
        //
        // NOT gated by this:
        //   - Hitbox overlay (single-player visualization, no
        //     gameplay effect)
        //   - Weapon / chara visibility, VFX suppression (local
        //     visual state, no opponent impact)
        //   - Ansel always-allowed (local photography, no opponent
        //     impact)
        //   - Reset position override (only fires on training-mode
        //     reset events the engine doesn't dispatch in matches)
        //   - Online rule overrides (the intended use case for
        //     online play — both peers opt in)
        if (Horse::GameMode::instance().should_force_disable_features())
        {
            apply_online_forced_disable();
        }

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

        // Push the per-line lifetime: Persistent backends use the user-
        // configured trail length (m_trail_frames game frames at 60Hz),
        // Normal backends stick to the engine-debug default (~6 frames).
        // Re-pushed every tick so a slider drag is immediately reflected
        // on the next appended line.  setLifetime() is a single float
        // store; cheap to call unconditionally.
        {
            const float trail_seconds =
                static_cast<float>(m_trail_frames.load()) / 60.0f;
            m_backend_hit.setLifetime(
                m_slot_hit.load() == Horse::LineBatcherSlot::Persistent
                    ? trail_seconds
                    : Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_hurt.setLifetime(
                m_slot_hurt.load() == Horse::LineBatcherSlot::Persistent
                    ? trail_seconds
                    : Horse::LineBatcherBackend::kDefaultLifetime);
        }

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

            // ---- Retrack-event edge detection ---------------------------
            // Read chara+0x94 (facing yaw, in [0,1) normalised, 1.0=360°)
            // and chara+0x16E6 (a motion-input flag that's set during
            // most moves) every cockpit tick.  Compute per-tick yaw
            // delta against last tick's snapshot, then fire on the
            // rising edge "in-move + |delta| > threshold".
            //
            // SafeRead* wraps the dereference in __try/__except so a
            // destroyed chara during a mode-transition tick can't AV
            // the cockpit hook.  Failures default the values to 0,
            // which collapses to "no event" (safe state).
            //
            // See the field doc on m_show_retrack_events for why we're
            // measuring yaw-delta directly instead of watching gate
            // flags — short version: the original flag-pair check fired
            // on hit-fall reactions, not on what the user calls
            // "retrack events".
            {
                auto* base = reinterpret_cast<const uint8_t*>(chara.raw());

                // Wrap-aware delta on a [0,1) circular axis.  Engine-
                // produced retracks never wrap by more than a tiny
                // amount per frame, so we bring the raw delta into
                // (-0.5, +0.5] and take its magnitude.
                float yaw_now = 0.0f;
                Horse::SafeReadFloat(base + 0x94, &yaw_now);

                uint8_t in_move = 0;
                Horse::SafeReadUInt8(base + 0x16E6, &in_move);

                bool retracking_now = false;
                if (m_have_prev_yaw[pi])
                {
                    float d = yaw_now - m_prev_yaw[pi];
                    if (d >  0.5f) d -= 1.0f;
                    if (d < -0.5f) d += 1.0f;
                    if (d < 0.0f)  d  = -d;
                    retracking_now =
                        (in_move != 0) && (d > kRetrackYawThresholdNorm);
                }
                m_prev_yaw[pi]      = yaw_now;
                m_have_prev_yaw[pi] = true;

                const bool was = m_was_retracking[pi];

                // Rising edge: not-retracking → retracking.  Only push
                // a banner if the user has the overlay enabled — keeps
                // the buffer empty (and no stale times) for users who
                // never enable it.
                if (retracking_now && !was &&
                    m_show_retrack_events.load(std::memory_order_relaxed))
                {
                    char msg[40];
                    std::snprintf(msg, sizeof(msg),
                                  "Player %d retrack event", pi + 1);
                    push_hud_text_event(msg);
                }
                m_was_retracking[pi] = retracking_now;
            }

            // Snapshot this player's three toggles once per chara so the
            // inner-loop gate is branch-free.
            const bool show_hurt       = shouldShow(pi, Horse::KHitList::Hurtbox);
            const bool show_atk        = shouldShow(pi, Horse::KHitList::Attack );
            const bool show_body       = shouldShow(pi, Horse::KHitList::Body   );
            // Snapshot the master visibility filter (see
            // m_only_show_active block).  Composition:
            //
            //   hits  hidden if  only_active &&
            //                    (!d.is_per_frame_active ||
            //                     !d.attacker_can_strike_engine)
            //   hurts hidden if  only_active &&
            //                    (!d.classifier_addressable ||
            //                     !d.overlap_active ||
            //                     !d.defender_can_react_engine)
            //
            // The hurt predicate is an OR of three engine-truth gates:
            //
            //   classifier_addressable  — the slot index (+0x17) is
            //     less than chara+0x44494 (= AttackMaxSlot, reused as
            //     the classifier's hurt-iteration ceiling).  A box at
            //     slot >= cap can NEVER deal damage this frame because
            //     LuxBattle_ResolveAttackVsHurtboxMask22's outer for
            //     loop won't read its PerHurtboxBitmask slot.
            //     [Geralt block-hold rectangle case.]
            //
            //   overlap_active           — the byte at +0x14 is
            //     non-zero, so UpdateAllKHitWorldCenters' overlap loop
            //     will OR attacker bits into PerHurtboxBitmask[slot].
            //
            //   defender_can_react_engine — chara-wide gate covering
            //     the three early-return sites of the resolver:
            //     battle-running global, chara+0x20B8 (incapacitated),
            //     chara+0x19B0 (no-react state, == 6).  When any
            //     fails, EVERY hurtbox on this chara is inert this
            //     frame regardless of geometry / +0x14 / slot index.
            //     Covers KO cinematics, round-end "WIN" pose,
            //     paused / loading frames.
            //
            // Any gate failing means "this hurtbox cannot land a
            // reaction this frame," which is the engine-truth answer
            // to "Only show active boxes."  All three must pass for
            // damage, so hiding when ANY fails matches what the
            // engine actually does.
            //
            // Master OFF (only_active==false) draws everything
            // authored on either list regardless of these predicates.
            const bool only_active = m_only_show_active.load();
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
                            // Hurtbox-side narrow filter (engine truth
                            // — see the long comment at the toggle
                            // snapshot above for the OR rationale).
                            //
                            //   classifier_addressable  : slot < cap?
                            //     If false, classifier loop ignores
                            //     this slot; box can't deal damage no
                            //     matter what its +0x14 says.  These
                            //     are the cyan-coloured VM-gated
                            //     extension hurtboxes (e.g. Geralt's
                            //     two large rectangles, slot >=
                            //     AttackMaxSlot).
                            //
                            //   overlap_active           : +0x14 != 0?
                            //     If false, engine's overlap loop
                            //     skips the box; no attacker bits get
                            //     OR'd, classifier finds nothing.
                            //
                            //   defender_can_react_engine: chara-wide?
                            //     If false, resolver early-returns
                            //     (battle not running, chara
                            //     incapacitated/dead, no-react state
                            //     6).  Whole hurtbox list is inert
                            //     this frame — KO cinematics,
                            //     round-end pose, paused / loading.
                            //
                            // ALL three must pass for the engine to
                            // fire a reaction this frame, so the
                            // OR-of-failures gate matches engine-
                            // truth.
                            if (only_active &&
                                (!d.classifier_addressable ||
                                 !d.overlap_active ||
                                 !d.defender_can_react_engine))
                                return;
                            break;
                        case Horse::KHitList::Attack:
                            if (!show_atk) return;
                            // Hitbox-side narrow filter (engine truth).
                            //
                            // is_per_frame_active = (+0x14 != 0) AND
                            //   (cat_mask & chara[+0x44058]) != 0 — the
                            // exact predicate LuxBattle_Resolve-
                            // AttackVsHurtboxMask22 (0x14033C100)
                            // applies before firing damage.  Hides
                            // boxes during startup / recovery, leaving
                            // only the engine-authored damage frames.
                            //
                            // (The legacy "Damage-active only" toggle
                            // tested is_damage_active — same per-move
                            // cell but the slot-bit interpretation,
                            // broader than is_per_frame_active.  It was
                            // dropped 2026-05 because in default state
                            // it was strictly weaker than the per-frame
                            // gate that's already on.)
                            //
                            // attacker_can_strike_engine: same chara-
                            // wide gate as the hurtbox side.  An
                            // incapacitated / round-ended chara
                            // doesn't deal damage even if its attack
                            // box otherwise looks live, so the
                            // engine-truth filter must include this
                            // gate too.
                            if (only_active &&
                                (!d.is_per_frame_active ||
                                 !d.attacker_can_strike_engine))
                                return;
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

                // Chara-wide engine-frozen state.  Battle not
                // running, chara incapacitated / dead, or chara in
                // no-react state 6.  Resolver early-returns BEFORE
                // touching this hurtbox's slot, so it cannot fire
                // a reaction regardless of geometry / +0x14 / slot
                // index.  Show as DIM GREY ("authored, but engine
                // is frozen on this chara right now") so the
                // distinction from the "vanishingly addressable
                // but inert" cyan VM-gated case is visible.
                //
                // Reached only when the master "Only show active"
                // filter is OFF — the narrow filter hides these
                // boxes by default.
                if (!d.defender_can_react_engine)
                {
                    return Horse::FLinColor{ 0.45f * player_tint,
                                             0.45f,
                                             0.45f * player_tint, 0.5f };
                }

                // VM-gated extended-reach hurtbox.  Authored with
                // +0x14 = 0 as the default off-state, flipped on
                // per-frame by the move-VM via opcode 0x13AC (see
                // LuxMoveVM_SetHurtboxSlotsActiveMask
                // @ 0x140308D70).  Slot index +0x17 is typically
                // beyond chara+0x44494 (AttackMaxSlot, reused as
                // the classifier's iteration ceiling) so they only
                // contribute to damage when the per-move data has
                // armed both +0x14 and the iteration bound.
                //
                // Colour them in CYAN tones so the user can see
                // them flip on/off across frames:
                //   bright cyan  = VM-gated AND currently on
                //                  (overlap_active == true)
                //   dim cyan     = VM-gated AND currently off
                //                  (overlap_active == false; only
                //                  visible when "Only show active
                //                  boxes" is OFF, since the narrow
                //                  filter would otherwise skip them)
                //
                // Detection: a hurtbox is "VM-gated" iff its slot
                // index is at or beyond the classifier bound.  The
                // classifier_addressable flag captures `slot < cap`
                // already, so `!classifier_addressable` is the same
                // signal the agent investigation identified.
                if (!d.classifier_addressable)
                {
                    return d.overlap_active
                        ? Horse::FLinColor{ 0.30f * player_tint,
                                            0.95f,
                                            1.0f, 1.0f }   // bright cyan = live geometry, slot OOB
                        : Horse::FLinColor{ 0.20f * player_tint,
                                            0.45f,
                                            0.55f, 0.6f }; // dim cyan = +0x14 off + slot OOB
                }

                // Classifier-addressable but +0x14 == 0 — the slot
                // IS in range but the engine's overlap loop will
                // skip this node.  Authored "off by default,
                // flipped on by the move-VM" extended reach inside
                // the addressable range.  Render in dim green to
                // tell users "this hurtbox exists but is currently
                // disabled by opcode 0x13AC."
                if (!d.overlap_active)
                {
                    return Horse::FLinColor{ 0.20f * player_tint,
                                             0.50f,
                                             0.20f * player_tint, 0.5f };
                }

                // Unified green for normal classifier-addressable
                // hurtbox entries — the engine doesn't sub-
                // categorise these from the defender side.
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
    // Online-gate UI helpers
    // ------------------------------------------------------------------
    // Centralised look-up of the colour + tooltip text used by both the
    // title-bar status indicator and the "Auto disable online" status
    // line at the top of the General tab.  Returning by value keeps the
    // call sites free of the four-way state switch they used to inline.
    //
    // Bundled into one helper so colour and tooltip can never drift out
    // of sync (the previous header banner had label/colour twinned in
    // separate switch branches and we'd hit the same drift if we left
    // each call site to compute its own state).
    struct OnlineStatusUI
    {
        ImVec4      colour;
        const char* short_label;   // 1-line, used by general-tab status row
        const char* tooltip_body;  // multi-line, used by both indicator + status row
    };

    static OnlineStatusUI compute_online_status_ui()
    {
        using GMP = Horse::GamePresence;
        auto& gm = Horse::GameMode::instance();
        const GMP  p          = gm.current_presence();
        const bool gating_on  = gm.auto_disable_online();
        const bool forced     = gm.should_force_disable_features();

        OnlineStatusUI s;
        if (!gating_on)
        {
            s.colour       = ImVec4{0.65f, 0.65f, 0.65f, 1.0f};
            s.short_label  = "Auto-disable OFF";
            s.tooltip_body =
                "Auto disable online: OFF. All features available.";
        }
        else if (p == GMP::Unknown)
        {
            s.colour       = ImVec4{0.95f, 0.85f, 0.20f, 1.0f};
            s.short_label  = "Presence unknown";
            s.tooltip_body =
                "Auto disable online: ON. Scene presence not yet "
                "observed; gate inactive.";
        }
        else if (forced)
        {
            s.colour       = ImVec4{1.00f, 0.30f, 0.30f, 1.0f};
            s.short_label  = "Online match — features locked";
            s.tooltip_body =
                "Auto disable online: ON. In a Ranked/Casual match — "
                "Lock-cam, Free-fly, Freeze, Slow-mo are locked off.";
        }
        else
        {
            s.colour       = ImVec4{0.30f, 0.90f, 0.40f, 1.0f};
            s.short_label  = "All features available";
            s.tooltip_body =
                "Auto disable online: ON. Scene safe — all features "
                "available.";
        }
        return s;
    }

    // Draws a small colored square in the active window's title bar,
    // positioned just to the right of the title text.  Hover over the
    // square shows the current online-gate status as a tooltip.
    //
    // Why ForegroundDrawList: the title bar is rendered by ImGui after
    // user content for this window, so a normal window-draw-list
    // submission can be overdrawn by the title bar.  Using the
    // foreground draw list guarantees our square sits ON TOP of the
    // title bar at all times.
    //
    // Tooltip uses IsMouseHoveringRect because raw ImDrawList primitives
    // bypass ImGui's input-claim path; the normal IsItemHovered() flow
    // doesn't apply to ad-hoc draw calls.
    static void draw_title_bar_status_indicator()
    {
        const OnlineStatusUI s = compute_online_status_ui();

        const ImVec2 wpos      = ImGui::GetWindowPos();
        const float  frame_h   = ImGui::GetFrameHeight();
        // Square sized relative to the title bar height so it scales
        // nicely on different DPI / font configurations.
        const float  sq_size   = frame_h * 0.55f;
        const float  pad_x     = 6.0f;
        // Rough left padding before the title text starts: collapse-
        // arrow (~frame_h) + a bit of breathing room.  Then we add the
        // title-text width to land the square just past the title.
        const float  title_w   = ImGui::CalcTextSize(horsemod_window_title()).x;
        const float  square_x  = wpos.x + frame_h + 4.0f + title_w + pad_x;
        const float  square_y  = wpos.y + (frame_h - sq_size) * 0.5f;

        const ImVec2 sq_min{square_x, square_y};
        const ImVec2 sq_max{square_x + sq_size, square_y + sq_size};

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(sq_min, sq_max, ImGui::GetColorU32(s.colour), 2.0f);
        // Thin black outline so the square is visible against any
        // title-bar background colour theme.
        dl->AddRect(sq_min, sq_max, IM_COL32(0, 0, 0, 200), 2.0f, 0, 1.0f);

        // Tooltip on hover — manual hit-test since the square isn't an
        // ImGui item.  Slight padding around the rect so the user
        // doesn't have to be pixel-precise.
        const ImVec2 hover_min{sq_min.x - 2.0f, sq_min.y - 2.0f};
        const ImVec2 hover_max{sq_max.x + 2.0f, sq_max.y + 2.0f};
        if (ImGui::IsMouseHoveringRect(hover_min, hover_max, /*clip=*/false))
        {
            ImGui::SetTooltip("%s", s.tooltip_body);
        }
    }

    // After rendering a force-disabled checkbox / slider, draw a
    // horizontal line across its label area so the user has a strong
    // visual cue ("crossed out") in addition to ImGui's normal
    // BeginDisabled greying.  Call AFTER EndDisabled and BEFORE the
    // next item submission so GetItemRectMin/Max still references the
    // checkbox we just drew.
    //
    // The line skips past the leading frame-height square (the
    // checkbox's tickbox) so the strikethrough visually crosses only
    // the label text, leaving the box itself unobscured.
    static void draw_disabled_strikethrough()
    {
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 rmax = ImGui::GetItemRectMax();
        const float y     = (rmin.y + rmax.y) * 0.5f;
        const float x0    = rmin.x + ImGui::GetFrameHeight() + 4.0f;
        const float x1    = rmax.x;
        // Use the disabled-text colour so the line tracks ImGui's theme
        // (light themes get a darker line, dark themes a lighter one).
        const ImU32 col   = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(x0, y), ImVec2(x1, y), col, 1.5f);
    }

    // Walk m_hud_text_events[] and draw every entry that's still within
    // its lifetime onto the foreground draw list, fading alpha linearly
    // from 100% at fire-time to 0% at lifetime expiry.  Stacks the most
    // recent event at the top and grows downward — newer entries hide
    // older ones if more fired in a short burst, which is the right
    // visual cue (the latest matters more).
    //
    // Drawing is FOREGROUND so the lines appear above both the game
    // and any ImGui windows.  Costs one std::array sweep + at most
    // kHudTextEventCount AddText calls per frame regardless of what's
    // happening on screen — cheap.
    //
    // The buffer is the shared overlay queue for arbitrary on-screen
    // text events (retrack-event detector, "Hello World" test button,
    // future C++-side diagnostic banners).  We don't gate on the
    // retrack toggle here because the queue is generic — gating
    // happens at the push site (only retrack pushes are gated by
    // m_show_retrack_events).
    void draw_hud_text_overlay()
    {
        const double now = ImGui::GetTime();

        struct Live { const char* text; double age_s; };
        Live live[kHudTextEventCount];
        size_t n_live = 0;
        for (const auto& ev : m_hud_text_events)
        {
            if (ev.text_len < 0) continue;
            const double age = now - ev.time;
            if (age < 0.0 || age > kHudTextEventLifetime) continue;
            live[n_live++] = Live{ev.text, age};
        }
        if (n_live == 0) return;

        // Newest first.
        std::sort(live, live + n_live,
                  [](const Live& a, const Live& b) { return a.age_s < b.age_s; });

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        ImVec2 origin{24.0f, 24.0f};

        for (size_t i = 0; i < n_live; ++i)
        {
            const float t = static_cast<float>(
                std::clamp(live[i].age_s / kHudTextEventLifetime, 0.0, 1.0));
            const uint32_t alpha = static_cast<uint32_t>(255.0f * (1.0f - t));
            const ImU32 colour   = IM_COL32(255, 220, 0, alpha);

            const ImU32 shadow = IM_COL32(0, 0, 0, alpha / 2);
            dl->AddText(ImVec2(origin.x + 1, origin.y + 1), shadow, live[i].text);
            dl->AddText(origin, colour, live[i].text);

            origin.y += line_h;
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
        // ----------------------------------------------------------------
        // Always-on overlays first — these draw to GetForegroundDrawList
        // unconditionally so they show up regardless of whether the
        // HorseMod window is open / collapsed / hidden via F2.  Anything
        // that needs to appear on top of the game without the user
        // having to interact with our panel goes here.
        // ----------------------------------------------------------------
        draw_hud_text_overlay();

        // -----------------------------------------------------------------
        // Gamepad-first friendliness — ONE-SHOT focus claim on show
        // -----------------------------------------------------------------
        // When the overlay flips hidden → shown (F2, Back-button, etc.)
        // we claim window focus + set m_nav_bootstrap_pending so the
        // currently-visible tab can run a single ImGui::SetKeyboardFocus-
        // Here() against its primary widget.  That's it.  No per-frame
        // re-claim, no focus-loss watchdog.
        //
        // History (so this comment doesn't get re-broken):
        // ------------------------------------------------
        // Earlier versions of this code ALSO ran a per-frame
        // `if (!IsWindowFocused) { SetWindowFocus(); m_nav_bootstrap_-
        // pending = true; }` block right after `Begin()`.  The intent was
        // "if focus drifted away for any reason, get it back".  In
        // practice that block caused two user-visible bugs:
        //
        //   1. CLICK EATING — `IsWindowFocused(_RootAndChildWindows)`
        //      can transiently return false during the same frame ImGui
        //      is processing a click on one of our widgets (popups,
        //      child regions, even regular checkbox state transitions
        //      can trigger a one-frame "focus is moving" window).
        //      Calling SetWindowFocus() in that window competes with
        //      the in-flight click and causes the click to be lost ~10%
        //      of the time.  Reported as "sometimes when opening the
        //      mod menu it lags quite a bit for letting me click on
        //      things."
        //
        //   2. STUCK BOOTSTRAP — m_nav_bootstrap_pending was set true
        //      every frame the focus check failed.  If the user was on
        //      a non-Hitboxes tab when the bootstrap fired, the flag
        //      was never consumed (only render_hitboxes_tab clears it).
        //      Then the moment the user navigated to Hitboxes,
        //      SetKeyboardFocusHere() snapped focus onto the F5
        //      checkbox — eating any in-flight click on a different
        //      widget.
        //
        // The fix below addresses both: bootstrap is one-shot, fires only
        // on the show edge, and is unconditionally cleared at the end of
        // each render_tab_impl regardless of which tab was visible.
        const bool just_shown = Horse::GameImGui::g_overlay_just_shown.exchange(
                false, std::memory_order_relaxed);
        if (just_shown)
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

        // ---------------------------------------------------------------
        // Title-bar online-match status indicator
        // ---------------------------------------------------------------
        // Replaces the previous full-width banner with a small colored
        // square drawn IN the title bar, just to the right of the
        // window title text.  Hover for a tooltip explaining the
        // current state and the gate's effect.
        //
        // Four colour states (same semantics as the old banner):
        //   GREY     gating toggle off              — all features available
        //   GREEN    gating on, scene safe          — all features available
        //   RED      gating on, in Ranked / Casual  — 4 features force-disabled
        //   YELLOW   presence not yet resolved      — gate inactive
        //
        // The square is drawn into the WINDOW draw list (clipped to the
        // title bar rect) so it composites correctly with ImGui's own
        // title-bar rendering.  Tooltip uses IsMouseHoveringRect since
        // ImDrawList lines / rects don't go through the normal
        // input-claim path.
        draw_title_bar_status_indicator();

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
        constexpr int kNumTabs = 5;
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
            tab_item("Labbing",  3, [this] { render_labbing_tab(); });
            tab_item("General",  4, [this] { render_general_tab(); });

            ImGui::EndTabBar();
        }

        // Clear the requested-tab marker AFTER the tab bar finishes so
        // ImGui has processed the SetSelected flag for this frame.
        // Leaving it set would re-apply SetSelected on the next frame
        // too, which ImGui handles gracefully but wastes cycles.
        s_requested_tab = -1;

        // Unconditionally clear m_nav_bootstrap_pending at the end of
        // every frame — even if the visible tab wasn't render_hitboxes_-
        // tab and didn't consume it.  Without this clear the flag would
        // be sticky across multiple frames in the "Camera/Time/General
        // tab is visible when the user shows the overlay" case, and
        // would then steal focus the moment the user navigated to the
        // Hitboxes tab (eating any in-flight click).  Clearing here
        // means: bootstrap is best-effort — if you happen to be on the
        // Hitboxes tab when the overlay shows, focus snaps to F5; on
        // any other tab the bootstrap is harmlessly dropped.
        m_nav_bootstrap_pending = false;

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
                    "Show this player's hurtboxes (volumes that take "
                    "damage). Green; flash red on hit.");
            }
            ImGui::SameLine();
            {
                bool a = atk.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Hitboxes##%s", id_suffix);
                if (ImGui::Checkbox(tag, &a)) atk.store(a);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Show this player's hitboxes (volumes that deal "
                    "damage). Strikes amber/yellow, throws magenta/pink.");
            }
            ImGui::SameLine();
            {
                bool b = body.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Body##%s", id_suffix);
                if (ImGui::Checkbox(tag, &b)) body.store(b);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Show this player's pushbox (used for spacing, "
                    "not damage). Dim blue.");
            }
            ImGui::PopID();
        };

        per_player_row("P1",
                       m_show_p1_hurt, m_show_p1_atk, m_show_p1_body, "p1");
        per_player_row("P2",
                       m_show_p2_hurt, m_show_p2_atk, m_show_p2_body, "p2");

        ImGui::Spacing();

        // --- Box-visibility filter ---------------------------------------
        // Single master toggle.  See the m_only_show_active block at the
        // top of this class for the engine-truth predicates.
        {
            bool only_active = m_only_show_active.load();
            if (ImGui::Checkbox("Only show active boxes", &only_active))
                m_only_show_active.store(only_active);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Show only the boxes the engine is actually using "
                "this frame (engine-truth narrow filter).");
        }

        // --- Hit-flash duration -----------------------------------------
        // The raw PerHurtboxReactionState signal is a ~1-frame pulse
        // (~16ms at 60fps) — too short to see.  This slider extends the
        // visible red flash by holding the "hot" state for N GAME FRAMES
        // before fading.  0 = disable the sticky entirely (raw 1-frame
        // pulse only).
        //
        // The drain is keyed on g_LuxBattle_FrameCounter (incremented
        // at the end of LuxBattle_PerFrameTick), so it tracks the same
        // tick the rest of the simulation does:
        //   * Freeze frame ON  → counter halts → flash held indefinitely.
        //   * F6 step          → counter +1   → flash drains by 1.
        //   * Slow-mo at S×    → counter advances at S× wall rate, so
        //                        the flash visibly persists 1/S× longer
        //                        in real time (matching the slowed anim).
        //   * Native play      → counter advances at 60Hz regardless of
        //                        render rate, so 15 frames = 250ms on
        //                        any monitor (60/120/144Hz).
        //
        // 15 frames ≈ 250ms at 60fps; 60 frames ≈ 1 second; the slider
        // caps at 60.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Hit-flash duration");
        {
            int frames = m_flash_frames.load();
            if (ImGui::SliderInt("frames##flashdur", &frames, 0, 60, "%d frames"))
            {
                if (frames < 0)  frames = 0;
                if (frames > 60) frames = 60;
                m_flash_frames.store(frames);
                Horse::KHitWalker::setStickyFrames(frames);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "How long the red hit-flash stays visible, in game "
                "frames (60/sec). Held during freeze; drains 1 per "
                "F6 step. 0 disables.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Render");
        {
            float t = m_thickness.load();
            if (ImGui::SliderFloat("Thickness", &t, 0.5f, 8.0f, "%.1f"))
                m_thickness.store(t);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Line thickness for the wireframes.");

            // Per-feature renderer combos.  Two entries each: Persistent
            // (depth-tested, lines accumulate over time — useful for
            // tracing a chara's path through a move) and Normal
            // (always-on-top, lines clear each frame — clean read of
            // the current state).  The third historical entry "Default"
            // (UWorld+0x40, depth-tested per-frame) was removed because
            // its lines disappeared behind characters, which defeats
            // the purpose of an overlay.
            //
            // Enum order matches LineBatcherSlot: Persistent=0, Normal=1.
            const char* slot_names[2] = {
                "Persistent (trail)",
                "Normal",
            };

            int hit_idx = static_cast<int>(m_slot_hit.load());
            if (ImGui::Combo("Hitbox renderer", &hit_idx, slot_names, 2))
                m_slot_hit.store(static_cast<Horse::LineBatcherSlot>(hit_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Normal: always on top. Persistent: lines "
                    "accumulate over time.");

            int hurt_idx = static_cast<int>(m_slot_hurt.load());
            if (ImGui::Combo("Hurtbox renderer", &hurt_idx, slot_names, 2))
                m_slot_hurt.store(static_cast<Horse::LineBatcherSlot>(hurt_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Normal: always on top. Persistent: lines "
                    "accumulate (useful for tracing a move's path).");

            // Trail length — only meaningful when at least one renderer
            // is set to Persistent.  Hidden otherwise to keep the UI
            // free of inert controls.  Works identically with "Only
            // show active boxes" enabled: the persistent batcher
            // accumulates ONLY the active-frame draws, producing a
            // trail of where the active hit/hurt boxes actually were
            // — which is the most useful read for move analysis.
            const bool any_persistent =
                m_slot_hit.load()  == Horse::LineBatcherSlot::Persistent ||
                m_slot_hurt.load() == Horse::LineBatcherSlot::Persistent;
            if (any_persistent)
            {
                int trail = m_trail_frames.load();
                if (ImGui::SliderInt("Trail frames##trail", &trail,
                                     1, 300, "%d frames"))
                {
                    if (trail < 1)   trail = 1;
                    if (trail > 300) trail = 300;
                    m_trail_frames.store(trail);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "How long Persistent-slot lines stay visible, in "
                    "game frames (60/sec). Lifetime decrements in "
                    "wall-clock time, so freeze and slow-mo extend "
                    "the visible trail. Works with 'Only show active "
                    "boxes' — the trail then shows just the active-"
                    "frame footprint of each hit/hurt box.");
            }
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
            const bool online_locked =
                Horse::GameMode::instance().should_force_disable_features();
            bool lc = m_cam_lock.is_enabled();
            const bool any_disabled = fc_on || online_locked;
            if (any_disabled) ImGui::BeginDisabled(true);
            if (ImGui::Checkbox("Lock camera position", &lc))
            {
                m_lock_camera.store(lc);
                m_cam_lock.set(lc);
            }
            if (any_disabled) ImGui::EndDisabled();
            // Strike through the label when force-disabled BY THE
            // ONLINE GATE specifically — a strong visual cue that the
            // gate (not a normal "no value" path) is blocking the
            // toggle.  Strikethrough is reserved for the online-gate
            // case so the existing "free-fly owns the lock" disabled
            // state still looks like a regular grey-out.
            if (online_locked) draw_disabled_strikethrough();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                online_locked
                    ? "Disabled — you're in a Ranked or Casual online\n"
                      "match and the General tab's \"Auto disable online\"\n"
                      "toggle is on.\n\n"
                      "Camera locking will be available again when the\n"
                      "match ends, or turn the \"Auto disable online\"\n"
                      "toggle off in the General tab to override (not\n"
                      "recommended for online play)."
                : fc_on
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
                const bool ff_online_locked =
                    Horse::GameMode::instance().should_force_disable_features();
                bool fc = m_free_camera_enabled.load();
                if (ff_online_locked) ImGui::BeginDisabled(true);
                if (ImGui::Checkbox("Free-fly camera (F7)", &fc))
                    m_free_camera_enabled.store(fc);
                if (ff_online_locked) ImGui::EndDisabled();
                if (ff_online_locked) draw_disabled_strikethrough();
                if (ImGui::IsItemHovered() && ff_online_locked)
                {
                    ImGui::SetTooltip(
                        "Disabled — you're in a Ranked or Casual online\n"
                        "match and the General tab's \"Auto disable online\"\n"
                        "toggle is on.\n\n"
                        "Free-fly camera will be available again when the\n"
                        "match ends, or turn the \"Auto disable online\"\n"
                        "toggle off in the General tab to override (not\n"
                        "recommended for online play).");
                }
                else if (ImGui::IsItemHovered()) ImGui::SetTooltip(
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
            const bool time_online_locked =
                Horse::GameMode::instance().should_force_disable_features();
            bool ff = m_freeze_frame.load();
            if (time_online_locked) ImGui::BeginDisabled(true);
            if (ImGui::Checkbox("Freeze frame", &ff))
            {
                m_freeze_frame.store(ff);
                // No explicit resolve/enable here — frame_step_apply
                // does the lazy enable on the next cockpit tick when
                // it sees freeze=true.
            }
            if (time_online_locked) ImGui::EndDisabled();
            if (time_online_locked) draw_disabled_strikethrough();
            if (ImGui::IsItemHovered() && time_online_locked)
            {
                ImGui::SetTooltip(
                    "Disabled — you're in a Ranked or Casual online\n"
                    "match and the General tab's \"Auto disable online\"\n"
                    "toggle is on.\n\n"
                    "Freeze frame will be available again when the\n"
                    "match ends, or turn the \"Auto disable online\"\n"
                    "toggle off in the General tab to override (not\n"
                    "recommended for online play).");
            }
            else if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "The game will be pause while this is checked. This "
                "option will also be enabled if you step x frames "
                "forward");

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

            // ---- Per-step diagnostic logger (debug aid) ----------------
            // UI hidden — underlying logger machinery (m_step_diag_*) and
            // settings persistence remain intact, so the feature can be
            // toggled by editing settings.cfg or by un-#if-ing this block.
#if 0
            // Off by default.  When on, every F6 step writes two lines
            // to UE4SS.log — one BEFORE the step's world tick, one
            // AFTER — listing the chara fields the hit classifier
            // consults.  Used to diagnose multi-hit-miss / held-input
            // bugs by diffing the pre/post snapshots.  Heavy: noisy log,
            // ~16 bytes per chara per snapshot * 2 charas * 2 phases =
            // ~64 reads per step.  Leave off for normal play.
            {
                bool diag = m_step_diag_enabled.load();
                if (ImGui::Checkbox("Per-step diagnostic log", &diag))
                {
                    m_step_diag_enabled.store(diag);
                    if (diag)
                    {
                        // Reset sequence counter so the log starts at #1
                        // for this enable cycle.
                        m_step_diag_seq.store(0);
                        Output::send<LogLevel::Default>(
                            STR("[FStep] per-step diagnostic ENABLED — "
                                "next F6 step will log pre/post chara "
                                "fields to UE4SS.log\n"));
                    }
                    else
                    {
                        Output::send<LogLevel::Default>(
                            STR("[FStep] per-step diagnostic disabled\n"));
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Debug aid for diagnosing multi-hit / held-input\n"
                    "bugs.  When enabled, each F6 step writes two lines\n"
                    "to UE4SS.log (a pre-step snapshot and a post-step\n"
                    "snapshot) listing chara+0x16E5/0x16EA/0x44058/etc.\n"
                    "— the fields the hit classifier reads.  Diff a\n"
                    "pre/post pair to see what changed across one game\n"
                    "frame.  Leave OFF for normal play.");
            }
#endif

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
                // The whole slow-motion block (checkbox + slider +
                // preset buttons) is locked while the online gate is
                // engaged — disabling just the checkbox would leave
                // the slider/presets clickable, and clicking a preset
                // would still mutate m_speed_value (harmless because
                // m_speed_control.is_enabled() would be false, but
                // confusing UI).  Wrapping the whole block keeps the
                // visual state honest.
                const bool sm_online_locked =
                    Horse::GameMode::instance().should_force_disable_features();
                if (sm_online_locked) ImGui::BeginDisabled(true);

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
                // Strike through the Slow-motion checkbox label when
                // the online gate is the reason for disable.  Drawn
                // BEFORE EndDisabled would normally pop styling, but
                // here we strike before the tooltip / extra widgets
                // so the visual cue lines up with the checkbox row
                // and not with anything below it.
                if (sm_online_locked) draw_disabled_strikethrough();
                // Tooltip — picks the gate-locked text or the normal
                // explanation depending on state.  Only shown while the
                // checkbox is hovered (Slider/Presets get their own
                // tooltips below).
                if (ImGui::IsItemHovered() && sm_online_locked)
                {
                    ImGui::SetTooltip(
                        "Disabled — you're in a Ranked or Casual online\n"
                        "match and the General tab's \"Auto disable online\"\n"
                        "toggle is on.\n\n"
                        "Slow-motion will be available again when the\n"
                        "match ends, or turn the \"Auto disable online\"\n"
                        "toggle off in the General tab to override (not\n"
                        "recommended for online play).");
                }
                else if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Run the game in slow motion");

                // ---- Live cadence dot --------------------------
                // Small colored square on the SAME ROW as the
                // checkbox + slider that flickers between green
                // (this tick is a "go" tick — full game frame) and
                // red (this tick is a "stop" tick — frozen).  Lets
                // the user visually confirm the slider is producing
                // the cadence they expect, especially important at
                // very low speeds (e.g., 0.01x = one go tick every
                // 100 cockpit ticks ≈ 1.6 sec — without this dot
                // the user has no feedback the system is alive).
                //
                // Drawn as a custom-rendered dummy item so its
                // colour reads from m_last_tick_kind (an atomic
                // updated by frame_step_apply on the cockpit thread)
                // rather than via PushStyleColor + ImGui::TextUnformatted
                // which would only convey two of the three states.
                ImGui::SameLine();
                {
                    const auto kind = static_cast<TickKind>(
                        m_last_tick_kind.load(std::memory_order_acquire));
                    ImVec4 col;
                    const char* hover_text = nullptr;
                    switch (kind)
                    {
                        case TickKind::Go:
                            col = ImVec4{0.30f, 0.90f, 0.40f, 1.0f};
                            hover_text =
                                "GO tick — this cockpit tick is\n"
                                "advancing the game by one full\n"
                                "native-dt frame.";
                            break;
                        case TickKind::Stop:
                            col = ImVec4{0.95f, 0.30f, 0.30f, 1.0f};
                            hover_text =
                                "STOP tick — this cockpit tick is\n"
                                "fully frozen.  The next 'go' tick\n"
                                "fires once the accumulator crosses\n"
                                "1.0 (slider value adds per tick).";
                            break;
                        case TickKind::Inactive:
                        default:
                            col = ImVec4{0.50f, 0.50f, 0.50f, 0.6f};
                            hover_text =
                                "Slow-motion not active or running\n"
                                "at native speed (slider >= 1.0).";
                            break;
                    }
                    const float dot = ImGui::GetTextLineHeight() * 0.6f;
                    const ImVec2 cur = ImGui::GetCursorScreenPos();
                    const float y_off = (ImGui::GetFrameHeight() - dot) * 0.5f;
                    ImVec2 dmin{cur.x + 2.0f, cur.y + y_off};
                    ImVec2 dmax{dmin.x + dot, dmin.y + dot};
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        dmin, dmax, ImGui::GetColorU32(col), 2.0f);
                    ImGui::GetWindowDrawList()->AddRect(
                        dmin, dmax, IM_COL32(0, 0, 0, 200), 2.0f, 0, 1.0f);
                    ImGui::Dummy(ImVec2(dot + 4.0f, ImGui::GetFrameHeight()));
                    if (hover_text && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", hover_text);
                }

                // Slider only meaningful when the patches are live; we
                // still allow drag while off so the user can pre-set
                // their target value before flipping on.
                //
                // Range capped at 1.0 because the frame-stepped
                // implementation can't tick the world MORE than once
                // per cockpit tick — values >1.0 silently behave as
                // 1.0 (see frame_step_apply's slow-mo branch).
                //
                // Logarithmic scale gives finer resolution at the
                // low end where users spend most of their time
                // (analysis ranges 0.05x..0.25x).  Linear from
                // 0.5x..1.0x where small differences matter less.
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                float sv = m_speed_value.load();
                if (ImGui::SliderFloat("##speedval", &sv, 0.0f, 1.0f,
                                       "%.3fx",
                                       ImGuiSliderFlags_Logarithmic))
                {
                    if (sv < 0.0f) sv = 0.0f;
                    if (sv > 1.0f) sv = 1.0f;
                    m_speed_value.store(sv);
                    if (m_speed_control.is_enabled())
                        m_speed_control.set_value(sv);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Effective game-frame rate as a fraction of\n"
                    "native (60 fps).  Logarithmic so the analysis\n"
                    "range 0.05x..0.25x has finer drag resolution\n"
                    "than the casual range 0.5x..1.0x.\n\n"
                    "0.5x  = every 2nd tick is a game frame  (~30 fps)\n"
                    "0.25x = every 4th tick is a game frame  (~15 fps)\n"
                    "0.1x  = every 10th tick is a game frame (~6 fps)\n"
                    "0.05x = every 20th tick is a game frame (~3 fps)");

                // Effective rate readout — shown below the slider so
                // the user sees the cadence they're picking in
                // human-readable units without having to do mental
                // arithmetic.
                {
                    const float S_ui = m_speed_value.load();
                    if (S_ui >= 1.0f)
                    {
                        ImGui::TextDisabled(
                            "Effective: native speed (~60 fps).");
                    }
                    else if (S_ui <= 0.0f)
                    {
                        ImGui::TextDisabled(
                            "Effective: frozen (slider at 0.0x).");
                    }
                    else
                    {
                        const float fps_eff   = 60.0f * S_ui;
                        const float every_n   = 1.0f / S_ui;
                        // Round display: show "every N ticks" only
                        // for clean integer ratios; otherwise show
                        // the float ratio at one decimal.
                        if (std::abs(every_n - std::round(every_n)) < 0.05f)
                        {
                            ImGui::TextDisabled(
                                "Effective: 1 frame every %d ticks  (~%.1f fps).",
                                static_cast<int>(std::round(every_n)),
                                fps_eff);
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "Effective: 1 frame every %.2f ticks  (~%.1f fps).",
                                every_n, fps_eff);
                        }
                    }
                }

                // Preset buttons for common hitbox-analysis speeds.
                // 0.25x and 0.125x added for finer analysis without
                // having to drag the log slider; the very-slow ones
                // (0.001x / 0.01x) kept for "essentially paused but
                // still creeping" moments.
                struct Preset { const char* label; float value; };
                static const Preset kPresets[] = {
                    {"Freeze##sp",   0.0f   },
                    {"0.01x##sp",    0.01f  },
                    {"0.1x##sp",     0.1f   },
                    {"0.125x##sp",   0.125f },
                    {"0.25x##sp",    0.25f  },
                    {"0.5x##sp",     0.5f   },
                    {"1x##sp",       1.0f   },
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

                if (sm_online_locked) ImGui::EndDisabled();

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

    }

    // ==================================================================
    // Labbing tab — training-mode utilities for practising specific
    // setups: capture a custom reset pose and have the in-game training
    // position-reset bind warp both players back to it.
    // ==================================================================
    void render_labbing_tab()
    {
            // --- Reset position override -----------------------------
            // When enabled and the user has captured a pose, our post-
            // hook on TrainingModePositionReset replays the captured
            // (X, Y, Z + side) for both players after the engine's
            // own reset has run.  Press the in-game training-reset
            // bind (default Select on a pad) to trigger.
            ImGui::TextUnformatted("Reset position override");
            {
                auto& ro = Horse::ResetOverride::instance();
                bool ro_on = ro.enabled();
                if (ImGui::Checkbox("Override reset position", &ro_on))
                {
                    ro.set_enabled(ro_on);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Send both players to the captured pose on the next "
                    "training-mode reset. Capture one below first.");

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
                    "Snapshot both characters' position and side. "
                    "Persistent across restarts.");

                ImGui::SameLine();
                if (ImGui::Button("Clear captured pose"))
                {
                    ro.clear_captured();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Forget the captured pose.");

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
            // ---- Online safety gate (TOP of General — primary control) ----
            // The single master toggle for HorseMod's online auto-disable
            // behaviour.  Placed at the top of the General tab because:
            //   1. It governs whether four features in OTHER tabs (Camera,
            //      Time) get force-disabled, so the user needs to find it
            //      WITHOUT first hunting through unrelated controls.
            //   2. The colour-coded status indicator in the title bar
            //      (next to the window name) reflects this toggle's
            //      effect; placing the toggle near the top gives a clear
            //      visual link from the indicator to the control.
            //
            // When ON and the game enters Ranked / Casual matchmaking,
            // these four are force-disabled and their UI struck-through:
            //   - Lock camera position    (Camera tab)
            //   - Free-fly camera         (Camera tab, F7)
            //   - Freeze frame            (Time tab, F6)
            //   - Slow motion             (Time tab)
            //
            // Other features (hitbox overlay, character / weapon
            // visibility, VFX suppression, online rule overrides, reset-
            // position override) are unaffected.  See horselib/GameMode.hpp
            // for the full rationale behind this gated subset.
            {
                auto& gm = Horse::GameMode::instance();
                bool gating = gm.auto_disable_online();
                if (ImGui::Checkbox(
                        "Auto disable online",
                        &gating))
                {
                    gm.set_auto_disable_online(gating);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Force-disable Lock camera, Free-fly, Freeze "
                    "frame, and Slow motion in Ranked/Casual matches. "
                    "Indicator next to the window title shows the "
                    "current state.");

                // Friendly status row that mirrors the title-bar
                // indicator's state in plain text — same colour, same
                // tooltip body — so users who prefer reading text over
                // squinting at a 12-pixel square can see exactly what
                // the gate is doing right now.
                const OnlineStatusUI s = compute_online_status_ui();
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, s.colour);
                ImGui::TextUnformatted(s.short_label);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", s.tooltip_body);

                // Hook-installed warning surfaces here at the top of
                // the General tab so it's impossible to miss.  If the
                // SetPresence hook never installed (very rare), the
                // gate has no signal and stays inactive regardless of
                // the user's selection above.
                if (!gm.hook_installed())
                {
                    ImGui::TextColored(
                        ImVec4{1.0f, 0.45f, 0.20f, 1.0f},
                        "Warning: presence hook not yet installed.");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Presence hook hasn't installed yet. Resolves "
                        "a few seconds into engine init.");
                }
            }
            ImGui::Separator();

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
                        "Already covered by \"Hide characters\".");
                }
                else
                {
                    ImGui::SetTooltip(
                        "Hide both characters' weapons. Only applies "
                        "while the F5 overlay is enabled.");
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
                "Hide both characters' models. Hitboxes and gameplay "
                "still work normally.");

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
                "Suppress hit sparks and particle VFX for a cleaner "
                "view.");

            if (!m_vfx_off.is_resolved() && sv)
            {
                ImGui::TextDisabled(
                    "(couldn't hook the VFX system — see UE4SS.log)");
            }

            ImGui::Spacing();
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
