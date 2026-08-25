// ============================================================================
// HorseMod - SoulCalibur VI hit-volume visualiser (UE4SS C++ mod).
//
// What this shows
// ---------------
// Every frame, read the SC6 legacy Namco KHit linked lists on both charas
// and draw their contents as wire-frame volumes in world space using
// UWorld's ULineBatchComponent.
//
//   HURTBOXES  (KHit list at chara+0x444B8) - entries that RECEIVE
//              damage from opponent attacks.  On by default.  Flashes red
//              when that slot's PerHurtboxReactionState[i] is non-zero
//              (just got hit).  The hurtbox list holds every "receive"
//              volume - damage hurtboxes, throw-reach receivers,
//              proximity/auto-turn cages.  The engine's own classifier
//              doesn't sub-bucket these from the defender side; reactions
//              are decided per-slot based on which attacker category bits
//              are set in the incoming mask.  We no longer invent
//              size-based sub-buckets for this reason.
//
//   ATTACK     (KHit list at chara+0x44498) - entries that DEAL damage
//   BOXES      (or initiate a grab).  On by default.  Dim amber for
//              strikes; a node is drawn bright when the active-window
//              display predicate passes (geometry gate, active slot bit,
//              active-frame phase).  Post-hit re-hit lockout is ignored
//              for display so a hitbox looks the same on hit as on whiff.
//              Grab/throw attacks are drawn magenta - the engine distinguishes them
//              from strikes via bits 31 and 55 of the node+0x08 slot mask at
//              node+0x08 (see LuxBattle_ResolveAttackVsHurtboxMask22
//              @ 0x14033C100).
//
//   BODY /     (KHit list at chara+0x44478) - entries that neither deal
//   PUSHBOX    nor receive damage.  Used by
//              LuxBattle_SolvePhysBodyCollision @ 0x14030CCF0 for
//              character-to-character physical pushing only.  OFF by
//              default (visually noisy - spacing context only).
//              Enable per-player from the ImGui tab.
//
// Historical note: earlier versions of this mod had the three list heads
// rotated (Attack?Body?Hurtbox off by one in the old plate on
// LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0).  That's why
// "attack" boxes used to appear to not make contact with the opponent -
// they were actually the pushbox list.  Corrected above.
//
// Visibility is per-player: P1 and P2 each get independent hurtbox /
// attack / body toggles, so e.g. you can hide P2's pushboxes while
// keeping P1's visible.
//
// This build visualizes the authoritative legacy KHit chain used for
// attack/hurt/body resolution.  The separate weapon-trail TraceManager
// capsule path is intentionally not drawn in this pass.
//
// Hotkeys
//   F5  toggle overlay on / off
//   F6  pause + step one frame.  First press latches Freeze-frame ON;
//       subsequent presses queue additional frames (held F6 yields
//       ~30fps slow-motion via UE4SS key auto-repeat).  Implementation
//       writes speedval = 0 / 1.0 alternation through Horse::SpeedControl
//       (the trampolined GetTimeDilationScalar override) - see the
//       frame_step_apply helper below.
//   F7  toggle free-fly camera.  Ansel-free: writes our own pose to
//       ALuxBattleCamera+0x410..+0x428 each cockpit tick while CamLock
//       holds off the engine's director.  Keyboard controls: WASD
//       translate, Q/E up/down, arrows or IJKL look (arrows may be
//       swallowed by SC6's RawInput handler - IJKL is the reliable
//       fallback), Shift/Ctrl for speed.  XInput pad 0 is also polled
//       (sticks translate/look, LT/RT vertical, LB/RB speed).  See
//       horselib/FreeCamera.hpp.
//
// ImGui tab ("HorseMod")
//   - overlay enable toggle (mirrors F5)
//   - per-list visibility toggles (hurtbox / attack / body)
//   - line thickness slider
//   - Per-list renderer selector (Persistent trail / Normal foreground)
//
// Everything else the earlier prototype had - predicate hooks, bounds
// traces, yarare watchers, process-event spies, cockpit-widget backend,
// capsule walker - has been removed.  If you want any of that back, pull
// it out of git history; it's all on dead paths for the "draw the real
// hitboxes" goal we're pursuing now.
//
// Ghidra references
//   LuxBattle_TickHitResolutionAndBodyCollision  @ 0x14033CCA0  (full plate)
//   Lux_KHitChk_DeserializeLinkedList            @ 0x14030C940
//   LuxBattleChara_UpdateAllKHitWorldCenters     @ 0x14030D6A0
//   KHitSphere_UpdateFromAnimCell                @ 0x14030E2F0
//   KHitBase / KHitArea / KHitSphere / KHitFixArea native field layouts
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>

#include "horselib/HorseLib.hpp"
#include "horselib/KHitWalker.hpp"
#include "horselib/LineBatcherBackend.hpp"
#include "horselib/StageBoundaryOverlay.hpp"
#include "horselib/StageVisualSuppressor.hpp"
#include "horselib/NativeBinding.hpp"
#include "horselib/CamLock.hpp"
#include "horselib/FreeCamera.hpp"
#include "horselib/VFXOff.hpp"
#include "horselib/CharaInvis.hpp"
#include "horselib/SpeedControl.hpp"
#include "horselib/WorldTickGate.hpp"
#include "horselib/ActorTickGate.hpp"
#include "horselib/TimeDilationGate.hpp"
#include "horselib/WindRngGate.hpp"
#include "horselib/deterministic/Config.hpp"
#include "horselib/deterministic/DeterministicHookSet.hpp"
#include "horselib/deterministic/HgCpuRuntimeDiagnostics.hpp"
#include "horselib/deterministic/Sc6ReplayRuntime.hpp"
#include "horselib/deterministic/Schema.hpp"
#include "horselib/deterministic/StageBreakListenerDiagnostics.hpp"
#include "horselib/deterministic/UcrtRandBroker.hpp"
// Horse::GameImGui replaces UE4SS_ENABLE_IMGUI().  It renders HorseMod's
// ImGui tab INSIDE the game's own DX11 swap chain via a PolyHook-vtable-
// swap detour on IDXGISwapChain::Present.  This keeps Steam overlay
// working (the detour chains through Steam's pre-existing hook) and
// eliminates the focus-stealing external "UE4SS Debugging Tools" window
// that breaks Shift+Tab for the user.
#include "horselib/GameImGui/GameImGui.hpp"
#include "horselib/GameImGui/Toast.hpp"

// Persists toggle / slider state between game sessions.  Loaded once in
// the ctor (before any atomic is first read for rendering), saved
// periodically via on_update, and saved a final time in the dtor for
// graceful shutdown.
#include "horselib/ModSettings.hpp"

// Captures + replays a custom (X, Y, Z) chara pose on training-
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
// ELuxGamePresence enum value - Training/Replay/single-player are safe,
// online matches are not.  See horselib/GameMode.hpp for the full rationale
// and the enum mapping.
#include "horselib/GameMode.hpp"

// PolyHook x64Detour on ULuxUIBattleLauncher::Start (image+0x5EEB50).
// This is the chokepoint for ALL 5 BattleRule overrides - the detour
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

// horselib/GamePause.hpp REMOVED - was a 5-site trampoline patching the
// chara+0x394 audio-state bit instead of the world-tick pause we
// thought.  Superseded by the SpeedControl freeze-frame mechanism (see
// the "Freeze frame" UI block below) which writes speedval=0 / 1.0 to
// engage the dt-scale freeze + sites 1..16 + g_LuxBattle_VMFreezeByte.
// Removed 2026-04 - see git history for the implementation.
//
// horselib/BattlePauseRequest.hpp also REMOVED 2026-04-27 - turned out
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
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Input/KeyDef.hpp>
#include <Input/Handler.hpp>

#include "ImGuiFileDialog.h"

#include <imgui.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

// ----------------------------------------------------------------------------
#ifndef HORSEMOD_VERSION
#define HORSEMOD_VERSION "dev"
#endif

#ifndef HORSEMOD_SOURCE_COMMIT
#define HORSEMOD_SOURCE_COMMIT "unknown"
#endif

static const char* horsemod_window_title()
{
    return "HorseMod (Beta " HORSEMOD_VERSION ")";
}

static void horsemod_report_unsupported_legacy_options_once() noexcept
{
    static std::atomic<bool> reported{false};
    if (reported.exchange(true, std::memory_order_acq_rel)) return;

    const wchar_t* command_line = GetCommandLineW();
    if (!command_line) return;
    const bool has_legacy_option =
        wcsstr(command_line, L"--horsemod-replay-") != nullptr
        || wcsstr(command_line, L"--horsemod-rollback") != nullptr
        || wcsstr(command_line, L"--rollback-") != nullptr;
    if (has_legacy_option)
    {
        Output::send<LogLevel::Warning>(STR(
            "[HorseMod] legacy rollback/replay command-line options are "
            "unsupported and were ignored; use rollback.ini after the "
            "deterministic adapter is qualified\n"));
    }
}

static std::wstring horsemod_current_module_path() noexcept
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&horsemod_current_module_path),
            &module) ||
        !module)
    {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    return std::wstring(path, length);
}

// ----------------------------------------------------------------------------
class HorseMod;
static std::atomic<HorseMod*> g_horse_mod_instance{nullptr};

class HorseMod final : public CppUserModBase
{
private:
    // Static live-instance pointer so the cockpit hook lambda can safely
    // no-op after destruction (game thread could fire after Restart All).
    static inline std::atomic<HorseMod*> s_instance{nullptr};

    // ---- Overlay state ----
    std::atomic<bool> m_enabled{false};

    // Per-player visibility toggles.  Default on-launch layout is
    // "only P2's hurtboxes visible" - the most common starting point
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
    //   Box-visibility filter - single master toggle.
    //
    //   m_only_show_active   default ON   active-shape narrow filter
    //                                     applied to both lists:
    //                                       hits  ? selected active-window
    //                                              geometry, ignoring
    //                                              post-hit re-hit lockout
    //                                       hurts ? classifier_addressable
    //                                              && overlap_active
    //                                              && defender_can_react_engine
    //                                     Audit still records the stricter
    //                                     native damage-live predicate.
    //
    //   The two chara-wide gates (attacker_can_strike_engine /
    //   defender_can_react_engine - same boolean, dual-named) cover
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
    //                         (slot_bit_mask & chara[+0x44058]) != 0
    //                       - exact predicate of
    //                         LuxBattle_ResolveAttackVsHurtboxMask22
    //                         @ 0x14033C100 before firing damage.
    //   classifier_addressable = (slot < min(chara+0x44494, 22))
    //                       - slot index is within the classifier's
    //                         iteration range.  A box at slot >= cap
    //                         can never deal damage no matter what
    //                         its +0x14 says, because the for-loop in
    //                         ResolveAttackVsHurtboxMask22 won't read
    //                         its PerHurtboxBitmask entry.
    //   overlap_active      = (hurt_node[+0x14] != 0)
    //                       - same byte the engine's overlap loop in
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
    //                       - the three early-return gates of
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
    //   ON : re-apply hidden every frame - overrides any game-driven
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
    // ProcessEvent - no native-RVA binding needed.
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
    //        Per-match actual session lifecycle - gated on (1) and (2).
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
    // This runs independent of the F5 overlay toggle - the user said
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
    // memory" instructions to NOPs - see horselib/CamLock.hpp for the
    // full disassembly walk and the historical note on why the previous
    // CameraCache.POV-write approach didn't work (UMG widget tick runs
    // AFTER the renderer has already consumed the POV).
    //
    // Semantics:
    //   OFF: every store runs as normal - engine owns the camera.
    //   ON : 5 stores at site A and 5 stores at site B are NOPed.  The
    //        camera struct in memory keeps whatever location/rotation/FOV
    //        it had at the moment we toggled ON; nothing in the engine
    //        rewrites those fields for the duration.
    //
    // CamLock owns the live BytePatch state - it must outlive every
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
    // fight the director-cam.  Independent of Nvidia Ansel - no Ansel
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
    //     Neither moved the camera - the first is the wrong object,
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
    //     APlayerController::GetPlayerViewPoint @ 0x142046410 -
    //     the consumer invoked by ULocalPlayer::CalcSceneView - reads
    //     back from the SAME +0x410..+0x424 block on PCM.
    //
    //     So the renderer-authoritative POV data lives on the
    //     APlayerCameraManager, NOT the ALuxBattleCamera.  The
    //     actor's +0x410..+0x428 is a director-scratch block that
    //     nothing downstream reads - writing there looks like it
    //     works (memory persists) but has zero render-side effect.
    void*             m_cached_player_camera_manager = nullptr;
    // UE4SS reflection-side locators: a cached FindFirstOf handle for
    // APlayerController, revalidated by GlobalPtr::get on level
    // changes.  PCM is read as the "PlayerCameraManager" property
    // on the PC every tick (cheap - hashed FName lookup).
    Horse::GlobalPtr  m_player_controller{};

    // ---- Hide characters ----------------------------------------------------
    // Bytepatch port of somberness's CE "Invisible" cheat - see
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
    // every read of the visibility flag now produces "hidden" - eliminating
    // the race because we're INSIDE the read path, not racing the writes.
    //
    // Useful when you're diagnosing hitbox shapes on a specific move -
    // the character mesh and its skirt/cape/hair occlude the volumes.
    // Hitboxes are part of the gameplay skeleton, not the mesh, so they
    // keep updating fine while the mesh is invisible.
    Horse::CharaInvis m_chara_invis{};
    std::atomic<bool> m_hide_chara{false};

    // ---- Speed control (slow-motion / freeze) -------------------------------
    // Bytepatch port of somberness's CE "Speed control v2" cheat - see
    // horselib/SpeedControl.hpp for the full disassembly walk and the
    // user contract.
    //
    // 5 trampolines hijack every load of the engine's master delta-time /
    // time-dilation float and redirect it to a single user-controlled
    // `speedval` slot in the CodeCave.  Result: the LuxMoveVM simulation
    // (animations, hit timing, opcode-stream execution, motion-object
    // advancement) all scale uniformly with speedval.
    //
    //   speedval = 0.0   ? frozen
    //   speedval = 0.05  ? 20- slow-mo (great for active-frame inspection)
    //   speedval = 0.1   ? 10- slow-mo
    //   speedval = 0.5   ? half speed
    //   speedval = 1.0   ? normal
    //
    // Independent of the GamePause toggle and the F6 step hotkey - they
    // gate different mechanisms and stack cleanly.
    Horse::SpeedControl m_speed_control{};
    std::atomic<bool>   m_speed_enabled{false};
    std::atomic<float>  m_speed_value{1.0f};

    // ---- World-tick gate (PerFrameTick / Site 9 - moved here 2026-05-05) ---
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
    // Sibling gate for the replay master-clock INC instructions
    // (LuxBattleChara_VTable648_TickAndAdvanceReplayClock at 0x1403E1FC0
    // and the GatedBy4404 variant at 0x1403E2000).  Reads from the
    // WorldTickGate policy slot - when frozen, both INCs are skipped so
    // the master clock at ALuxBattleFrameInputLog+0x3A4 stays pinned.
    // Without this gate, match-replay viewing leaks: SimulationLoop's
    // catch-up loop keeps draining recorded inputs into BM input data,
    // round state machine advances, and on unfreeze Stage 3 fast-forwards
    // through the buffered inputs in one tick.  See
    // horselib/ReplayClockGate.hpp for the full plate.
    // Sibling gate for the surrounding Actor::Tick prologues that
    // WorldTickGate's single Site-9 hook misses:
    //   * ALuxBattleChara::TickActor (UE4 anim, hair, weapon mesh, SC
    //     gauge counter)
    //   * ALuxBattleManager::Tick / MainStateMachine_At1461 (round state
    //     machine, SimulationLoop catch-up)
    // Both bare-RET when WorldTickGate's policy slot is 0.  Without these,
    // long match-replay freezes settle the chara to the idle pose because
    // the BM round-over check trips on a wallclock-driven timer and the
    // chara mesh's anim montage plays out via the UE4-side actor tick.
    // See horselib/ActorTickGate.hpp for the full plate.
    Horse::ActorTickGate m_actor_tick_gate{};
    // Sibling gate that forces LuxMoveVM_GetTimeDilationScalar
    // (0x14030A8C0) to return 0.0 when WorldTickGate's policy slot is 0.
    // The function's normal-play fall-through path bypasses VMFreezeByte
    // entirely (returns chara+0x3500 directly), so VMFreezeByte=1 alone
    // doesn't halt P1 in match-replay watching.  This gate's entry-patch
    // returns 0 unconditionally during freeze, which forces every dt-
    // multiply integrator (MoveVM, physics, anim, FX) to produce 0
    // deltas - including UE4 anim instances that scale by the engine's
    // tick dilation.  See horselib/TimeDilationGate.hpp for the full
    // plate.
    Horse::TimeDilationGate m_time_dilation_gate{};
    Horse::WindRngGate m_wind_rng_gate{};
    // Previous-cockpit-tick snapshot of g_LuxBattle_FrameCounter.  Read
    // at the top of frame_step_apply() to detect whether PerFrameTick
    // ran since our last call - drives WorldTickGate's step-credit
    // drain on a real "did the world tick" signal instead of cockpit
    // hook timing.  Read/written exclusively from the cockpit pre-
    // tick (game thread); no atomic needed.
    uint32_t m_prev_frame_counter_value = 0;
    bool     m_prev_frame_counter_seen  = false;
    // Legacy SpeedControl write-dedupe state. Current time controls keep
    // SpeedControl disabled and drive WorldTickGate/ReplayClockGate/
    // ActorTickGate instead.
    float m_last_speed_target =
        std::numeric_limits<float>::quiet_NaN();

    // ---- SC6 NATIVE VM-FREEZE BYTE driver state ----------------------------
    // Tracks whether HorseMod has currently SET the native freeze byte at
    // imageBase + kRVA_LuxBattleVMFreezeRecord
    // (g_LuxBattle_VMFreezeRecord.bVMFreezeByte).
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
    // (activation glow ? AOE pulse ? recovery).  Muting audio mid-SC by
    // setting bit 2 of chara+0x394 stalls the state machine; the AOE phase
    // never fires, hitboxes never activate, the move "doesn't hit" anymore.
    //
    // Trade-off: without BattlePauseRequest the round timer ticks during
    // long replay freezes, eventually ending the round.  Acceptable for
    // now - the alternative was breaking gameplay-critical mechanics.  If
    // a clean round-timer halt is needed, the next investigation should
    // target the BattleTimeManager's actual tick path (BM+0x4F8) without
    // touching audio state.

    // ---- Suppress VFX -------------------------------------------------------
    // Bytepatch port of somberness's CE "VFX off" cheat - see
    // horselib/VFXOff.hpp for the full disassembly walk.  Replaces the
    // earlier per-frame DestroyAllVFx polling: that approach let each
    // VFX spawn for one frame before tearing it down (1-tick flashes
    // on every hit) and burned a UFunction call per tick.  The
    // bytepatch installs a midfunction trampoline that overrides the
    // engine's per-slot VFX-state writer to plant a sentinel constant
    // the renderer treats as culled - effects never become visible.
    //
    // Same toggle, same ImGui label.  No hot-path work; flip is a
    // single 5-byte JMP install.
    Horse::VFXOff     m_vfx_off{};
    std::atomic<bool> m_suppress_vfx{false};

    // Draws the deterministic J_StgHitChkData terrain/edge/wall triangles and
    // current breakable-stage presentation bounds. Controlled only by the
    // General tab checkbox; deliberately independent of the F5 overlay.
    std::atomic<bool> m_show_stage_boundary{false};
    Horse::StageBoundaryOverlay m_stage_boundary{};

    // Visual-only stage mesh hiding for inspecting hitboxes and the
    // stage boundary wireframe.  Independent of F5 and gameplay
    // collision; StageVisualSuppressor caches actor/component pointers
    // and reapplies at a low cadence instead of scanning every tick.
    Horse::StageVisualSuppressor m_stage_visuals{};
    std::atomic<bool> m_hide_stage_visuals{false};

    // ---- Freeze frame (WorldTickGate-driven) --------------------------------
    // Replaces the broken Horse::GamePause helper (which patched a chara
    // audio-flag bit at +0x394, not a world-pause).  The actual world-
    // tick pause in SC6 is the master VM-freeze byte at 0x1448462D0:
    // when non-zero, LuxMoveVM_GetTimeDilationScalar returns 0.0 and
    // every per-frame integrator (animation, opcode-stream, hit timing)
    // sees dt=0 and halts.  See the plate on g_LuxBattle_VMFreezeByte.
    //
    // Current implementation uses WorldTickGate plus replay/actor/time
    // sibling gates. Legacy SpeedControl remains disabled because several
    // replay AOBs are stale and their duties moved to the dedicated gates.
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
    // frame - first tick lifts the freeze, second tick re-applies it.
    std::atomic<int>  m_step_pending{0};
    std::atomic<bool> m_step_expecting{false};

    // Defensive frame-step resync: cockpit::Update can fire WITHOUT
    // the world ticking (UMG widget tick is independent of world tick
    // - see comment at the frame_step_apply callsite).  If Step Tick A
    // publishes speedval=1.0 but the world doesn't actually tick before
    // the next cockpit pre-hook (loading reentrance, paused redraw, a
    // doubled cockpit::Update call), pivoting to Step Tick B would
    // silently consume the user's F6 press.
    //
    // Witness: per-lane tick counter at lane+0x04 (int32) - the engine
    // increments this every world tick that processes the chara.  We
    // snapshot it on Step Tick A; on Step Tick B we re-read and only
    // pivot if at least one lane counter advanced.  If none advanced,
    // hold expecting=true and try again next cockpit tick.
    //
    // Cap holds at kStepDwellMax cockpit ticks to recover from a
    // stale or unmappable witness (e.g., chara struct destroyed during
    // a mode transition with pending > 0 - should be cleared by
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
    // differs from this value (i.e. SC6 transitioned modes - e.g.
    // training -> ranked -> training), we forcibly clear Freeze and
    // Slow-motion regardless of the "Auto disable online" gate.
    //
    // Why force the clear on EVERY transition (not just into PvP):
    //   1. SC6 destroys the old BattleManager + chara actors and
    //      builds new ones during a mode switch.  If freeze stays
    //      active across the transition, Site 9 (PerFrameTick entry-
    //      RET) blocks the new BattleManager's per-frame tick the
    //      moment its first chara fires the chain - including
    //      UpdateBattleCameraSynthesis, which is what the renderer
    //      reads to set the view matrix.  Result: black screen on
    //      training reload from a previous match.
    //   2. Slow-motion has the same hazard via Sites 1/3/4/5/6
    //      (dt-scale at math sites) - fractional dt during state-
    //      machine init can produce uninitialised camera / VFX
    //      state on the new mode's first frames.
    //   3. Once cleared, freeze/slow-mo STAY cleared (the user must
    //      manually re-engage them) - matching the user's mental
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
    // expects to see EACH integer frame exactly once - at fractional
    // dt it sees the same frame twice or skips entirely).
    //
    // New behaviour (frame-stepped slow-mo): each cockpit tick is a
    // hard 1.0 (full game frame) or 0.0 (freeze).  The accumulator
    // adds the slider value S each tick; when it crosses 1.0, that
    // tick is a "go" tick (target = 1.0), accumulator -= 1.0.
    // Otherwise it's a "stop" tick (target = 0.0).  Effective
    // average speed = S, but every game frame the engine sees is a
    // clean native-dt frame - hit cells advance one integer frame
    // at a time, multi-hit moves resolve correctly.
    //
    // Trade-off: slightly choppier visuals at low speeds (1 frame
    // every 4 ticks at S=0.25 = 15 fps effective).  But for analysis
    // and replay-watching, frame accuracy matters more than smooth
    // motion.  The choppiness is identical to repeatedly mashing
    // the Step-1 button at the right cadence - which is exactly
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
    // user can see the frame-step cadence at a glance - useful for
    // confirming the slider is actually doing what they expect at
    // very low speeds (e.g., 0.001x = one go-tick every ~1000
    // cockpit ticks - 17 seconds; without a live indicator the user
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
    // (~16ms at 60fps) - too short to see.  We extend it by holding the
    // hot state for `m_flash_frames` game frames before fading.
    //
    // KHitWalker drains the sticky by tracking g_LuxBattle_FrameCounter
    // (imageBase+0x470D0C4), which is incremented exactly once at the
    // end of LuxBattle_PerFrameTick.  Since Horse::WorldTickGate gates
    // PerFrameTick at its entry, the counter halts under freeze and
    // advances once per gate-released game frame under step / slow-mo
    // - the flash is held during freeze, drains one unit per F6 step,
    // and drains in lockstep with the slowed game clock during slow-mo.
    //
    // Default 15 frames - 250ms of native-speed gameplay.
    std::atomic<int> m_flash_frames{15};

    std::atomic<float> m_thickness{1.5f};

    // Per-feature line-batcher slot.  Hitboxes (Attack list) draw via
    // m_backend_hit; hurtboxes draw via m_backend_hurt.  When a feature
    // is set to Persistent, only engine-live hit/hurt boxes are routed
    // there; inactive boxes in the broad inspection view fall back to
    // fixed Foreground backends so they show once instead of smearing.
    // Body boxes are not hit-resolution volumes, so they never trail.
    std::atomic<Horse::LineBatcherSlot> m_slot_hit {Horse::LineBatcherSlot::Foreground};
    std::atomic<Horse::LineBatcherSlot> m_slot_hurt{Horse::LineBatcherSlot::Foreground};

    // Trail length in game frames for whichever backend is in the
    // Persistent slot.  Pushed to the backend each cockpit tick as
    // m_trail_frames / 60.0 seconds.  HorseMod advances that lifetime
    // from g_LuxBattle_FrameCounter, not wall-clock time, so freeze and
    // F6 step hold/drain the trail in game-frame units.  Range matches the slider 1..300
    // = 1 frame to 5 seconds of trail at 60 Hz.  Default 30 - 0.5 s,
    // long enough to be visibly useful for tracing a move without
    // drowning the screen in line history.  Used for both backends
    // when their slot == Persistent - Normal-slot backends ignore
    // this and stick to LineBatcherBackend::kDefaultLifetime.
    std::atomic<int> m_trail_frames{30};
    static constexpr int kKHitPersistentTrailLineBudget = 12000;
    static constexpr int kKHitPersistentTrailLineHeadroom = 4096;

    // Persistent trail cadence follows the game frame counter, not the
    // cockpit/render tick.  When HorseMod freeze holds PerFrameTick, this
    // counter stops, so persistent lines are neither duplicated nor aged.
    bool     m_have_trail_game_frame{false};
    uint32_t m_last_trail_game_frame{0};
    bool     m_have_trail_filter_state{false};
    bool     m_last_trail_only_active{true};

    // Diagnostic-only.  Logs attack spheres once per game frame so
    // externally-edited spheres can be compared against the rendered
    // centre/radius without permanently noisy UE4SS logs.
    //
    // Scuffle clue captured from hdr030_TEST.khd, move 328: the edited
    // test hitbox is on attack entry 1's General_1 / General_2 masks,
    // i.e. native category slots 56 and 57, not HitMisc::Big_Sphere.
    std::atomic<bool> m_khit_sphere_audit{false};
    std::atomic<bool> m_khit_sphere_audit_filter_move{false};
    std::atomic<int>  m_khit_sphere_audit_move{328};
    std::atomic<bool> m_khit_sphere_audit_filter_slots{false};
    std::atomic<int>  m_khit_sphere_audit_slot_a{56};
    std::atomic<int>  m_khit_sphere_audit_slot_b{57};
    bool     m_have_sphere_audit_frame{false};
    uint32_t m_last_sphere_audit_frame{0};
    int      m_khit_audit_attack_logs_this_frame{0};
    int      m_khit_audit_hurt_logs_this_frame{0};
    int      m_khit_audit_pair_logs_this_frame{0};
    int      m_khit_audit_calib_logs_this_frame{0};
    int      m_khit_audit_cluster_logs_this_frame{0};
    static constexpr int kMaxKHitAuditAttackLogsPerFrame = 96;
    static constexpr int kMaxKHitAuditHurtLogsPerFrame = 96;
    static constexpr int kMaxKHitAuditPairLogsPerFrame = 128;
    static constexpr int kMaxKHitAuditCalibLogsPerFrame = 64;
    static constexpr int kMaxKHitAuditClusterLogsPerFrame = 16;

    struct KHitRenderCalibrationPoint
    {
        bool native_ok = false;
        bool actor_ok = false;
        bool delta_ok = false;
        Horse::FVec3 native_root{};
        Horse::FVec3 converted_root{};
        Horse::FVec3 actor_root{};
        Horse::FVec3 delta{};
    };

    struct KHitRenderCalibrationFrame
    {
        KHitRenderCalibrationPoint point[2]{};
        bool has_common_delta = false;
        bool consistent = false;
        bool applied = false;
        float delta_distance = 0.0f;
        Horse::FVec3 common_delta{};
        Horse::FVec3 active_offset{};
        const wchar_t* status = L"missing";
        int samples = 0;
    };

    struct KHitRenderCalibrationState
    {
        bool valid = false;
        int samples = 0;
        Horse::FVec3 offset{};
    };

    KHitRenderCalibrationState m_khit_render_calibration{};
    Horse::Fn m_fn_khit_actor_location[2];

    // ---- Retrack-event overlay ----------------------------------------
    // When ON, watches each chara's facing yaw every cockpit tick and
    // prints a transient "Player N retrack event" line on screen
    // whenever the engine rotated that chara during a move (i.e. the
    // chara's facing changed appreciably while a move was active).
    //
    // -------------------------------------------------------------------
    // History - what was tried first and why it was wrong
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
    //                  fall-reaction cluster {0x0c..0x11, 0x29, 0x35} -
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
    // is NO "homing override" flag - moves that track the opponent
    // (homing throws, certain supers) implement that through some
    // other mechanism (likely the SLERP-weight system at
    // chara+0x971ac..+0x971b8 set up at move-start, or by the move
    // script writing chara+0x94 directly).
    //
    // So watching the gate flag-pair fired the overlay during knockback
    // and fall recoveries - which the user reported as "triggers in
    // unexpected places".  Confirmed: my interpretation was wrong.
    //
    // -------------------------------------------------------------------
    // Current implementation - direct yaw-delta detection
    // -------------------------------------------------------------------
    // Read chara+0x94 (facing yaw, written by ApplyFacingRotationDelta)
    // every cockpit tick, compute the per-tick delta against last
    // tick's snapshot, and fire when:
    //
    //   |yaw_delta| > kRetrackYawThresholdNorm   (= ~0.7- per tick)
    //   AND chara is in some move state          (chara+0x16E6 != 0)
    //
    // This catches "the engine rotated my chara appreciably during a
    // move" regardless of which internal mechanism produced the
    // rotation - homing-throw retrack, hit-reaction realignment, or
    // a move script's direct yaw write.  False positives are limited
    // by the threshold; brief sub-degree adjustments don't fire.
    //
    // The yaw value at chara+0x94 is normalised in [0, 1) where 1.0
    // == 360-.  See the plate on RetrackFacingTowardOpponent for the
    // unit convention; the integrator at +0x94 uses the same scale.
    //
    // Off by default - diagnostic feature, not gameplay-affecting.
    std::atomic<bool> m_show_retrack_events{false};

    // Yaw threshold in normalised units (1.0 == 360-).  ~0.002 == 0.72-.
    // Below this we treat the rotation as "noise" / fine-tune adjustment
    // and don't fire; above it we treat it as a real retrack event.
    // Tuned empirically - natural facing-maintenance during idle/walk
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
    //   - Retrack-event detector - formats "Player N retrack event" and
    //     pushes a string when an in-move yaw burst exceeds threshold.
    //   - Test button (General tab) - pushes "Hello World" to verify
    //     the overlay path is alive without needing a fight.
    //   - Any future C++ feature that wants to surface a transient
    //     diagnostic line on top of the game viewport.
    //
    // 8 slots - 1.5s lifetime is enough to show ~5 events per second
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
    // strings longer than the slot capacity, which is fine - these
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

    static bool canMatterThisFrame(const Horse::KHitDraw& d)
    {
        switch (d.list)
        {
            case Horse::KHitList::Attack:
                return d.is_per_frame_active && d.attacker_can_strike_engine;
            case Horse::KHitList::Hurtbox:
                return d.classifier_addressable &&
                       d.overlap_active &&
                       d.defender_can_react_engine;
            case Horse::KHitList::Body:
                return false;
        }
        return false;
    }

    static bool canRenderAttackShapeThisFrame(const Horse::KHitDraw& d)
    {
        if (d.list != Horse::KHitList::Attack ||
            !d.geom_active ||
            !d.attacker_can_strike_engine ||
            d.attack_mask_stale)
        {
            return false;
        }

        const bool primary_mask_selected =
            d.active_move_valid &&
            ((d.slot_bit_mask & d.primary_attack_mask) != 0);
        const bool primary_window_open =
            d.classify_enabled &&
            (d.attack_in_master_window ||
             d.engine_phase == Horse::KHitAttackPhase::Active);
        const bool primary_visual_ready =
            primary_mask_selected && primary_window_open;
        const bool alt_visual_ready =
            d.alt_classify_open &&
            ((d.slot_bit_mask & d.alt_attack_mask) != 0);

        // Deliberately ignore +0x16EB/+0x16FE here. Those bytes mean
        // "this active hitbox already connected and cannot deal damage
        // again yet"; for display, users expect the same active shape
        // they would have seen on whiff.
        return primary_visual_ready || alt_visual_ready;
    }

    static bool read_lux_battle_game_frame(uint32_t& out_frame) noexcept
    {
        constexpr uintptr_t kFrameCounterRVA = 0x470D0C4;
        const uintptr_t base = Horse::NativeBinding::imageBase();
        return base != 0 && Horse::SafeReadUInt32(
            reinterpret_cast<const void*>(base + kFrameCounterRVA),
            &out_frame);
    }

    static bool mask_has_slot(uint64_t mask, int slot) noexcept
    {
        return slot >= 0 && slot < 64 &&
               (((mask >> static_cast<unsigned>(slot)) & 1ull) != 0);
    }

    static bool khit_audit_move_matches(int wanted,
                                        bool has_move,
                                        int packed_move,
                                        int move_id_low11) noexcept
    {
        return wanted < 0 ||
               (has_move &&
                (packed_move == wanted || move_id_low11 == wanted));
    }

    bool khit_audit_matches_move_filter(
        const Horse::KHitDraw& d,
        const Horse::KHitWalker::LaneSnapshot* attacker_lane) const
    {
        if (m_khit_sphere_audit_filter_move.load(std::memory_order_relaxed))
        {
            const int wanted =
                m_khit_sphere_audit_move.load(std::memory_order_relaxed);
            if (d.list == Horse::KHitList::Hurtbox && attacker_lane)
            {
                const int packed =
                    static_cast<int>(attacker_lane->packed_move);
                const int low11 = packed & 0x7ff;
                return khit_audit_move_matches(
                    wanted, attacker_lane->has_move, packed, low11);
            }

            return khit_audit_move_matches(
                wanted,
                d.has_move_identity,
                static_cast<int>(d.active_packed_move),
                static_cast<int>(d.active_move_id_low11));
        }

        return true;
    }

    bool khit_audit_matches_slot_filter(const Horse::KHitDraw& d) const
    {
        if (m_khit_sphere_audit_filter_slots.load(std::memory_order_relaxed))
        {
            const int slot_a =
                m_khit_sphere_audit_slot_a.load(std::memory_order_relaxed);
            const int slot_b =
                m_khit_sphere_audit_slot_b.load(std::memory_order_relaxed);
            auto matches_slot = [&](int slot) {
                return slot >= 0 && slot < 64 &&
                       (static_cast<int>(d.bone_id_internal) == slot ||
                        mask_has_slot(d.slot_bit_mask, slot) ||
                        (d.defender_hurtbox_mask_valid &&
                         mask_has_slot(d.defender_hurtbox_attack_mask, slot)));
            };
            if (!matches_slot(slot_a) && !matches_slot(slot_b))
                return false;
        }

        return true;
    }

    bool khit_audit_matches_filter(
        const Horse::KHitDraw& d,
        const Horse::KHitWalker::LaneSnapshot* attacker_lane) const
    {
        return khit_audit_matches_move_filter(d, attacker_lane) &&
               khit_audit_matches_slot_filter(d);
    }

    static bool can_expose_khit_attack_for_audit(const Horse::KHitDraw& d)
    {
        return d.list == Horse::KHitList::Attack &&
               d.geom_active &&
               (d.attack_mask_selected ||
                d.attack_mask_stale ||
                d.accepted_overlap_this_frame ||
                d.accepted_exact_overlap_this_frame) &&
               d.attacker_can_strike_engine;
    }

    enum class KHitAuditLogBucket : uint8_t
    {
        Attack,
        HurtResult,
        OverlapPair,
        Calibration,
        AttackCluster,
    };

    bool consume_khit_audit_log_slot(KHitAuditLogBucket bucket)
    {
        switch (bucket)
        {
            case KHitAuditLogBucket::Attack:
                if (m_khit_audit_attack_logs_this_frame >=
                    kMaxKHitAuditAttackLogsPerFrame)
                    return false;
                ++m_khit_audit_attack_logs_this_frame;
                return true;
            case KHitAuditLogBucket::HurtResult:
                if (m_khit_audit_hurt_logs_this_frame >=
                    kMaxKHitAuditHurtLogsPerFrame)
                    return false;
                ++m_khit_audit_hurt_logs_this_frame;
                return true;
            case KHitAuditLogBucket::OverlapPair:
                if (m_khit_audit_pair_logs_this_frame >=
                    kMaxKHitAuditPairLogsPerFrame)
                    return false;
                ++m_khit_audit_pair_logs_this_frame;
                return true;
            case KHitAuditLogBucket::Calibration:
                if (m_khit_audit_calib_logs_this_frame >=
                    kMaxKHitAuditCalibLogsPerFrame)
                    return false;
                ++m_khit_audit_calib_logs_this_frame;
                return true;
            case KHitAuditLogBucket::AttackCluster:
                if (m_khit_audit_cluster_logs_this_frame >=
                    kMaxKHitAuditClusterLogsPerFrame)
                    return false;
                ++m_khit_audit_cluster_logs_this_frame;
                return true;
        }
        return false;
    }

    static Horse::FVec3 midpoint(const Horse::FVec3& a,
                                 const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{
            (a.X + b.X) * 0.5f,
            (a.Y + b.Y) * 0.5f,
            (a.Z + b.Z) * 0.5f,
        };
    }

    static Horse::FVec3 centroid3(const Horse::FVec3& a,
                                  const Horse::FVec3& b,
                                  const Horse::FVec3& c) noexcept
    {
        return Horse::FVec3{
            (a.X + b.X + c.X) / 3.0f,
            (a.Y + b.Y + c.Y) / 3.0f,
            (a.Z + b.Z + c.Z) / 3.0f,
        };
    }

    static float distance3(const Horse::FVec3& a,
                           const Horse::FVec3& b) noexcept
    {
        const float dx = a.X - b.X;
        const float dy = a.Y - b.Y;
        const float dz = a.Z - b.Z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    static Horse::FVec3 add3(const Horse::FVec3& a,
                             const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{a.X + b.X, a.Y + b.Y, a.Z + b.Z};
    }

    static Horse::FVec3 sub3(const Horse::FVec3& a,
                             const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{a.X - b.X, a.Y - b.Y, a.Z - b.Z};
    }

    static Horse::FVec3 scale3(const Horse::FVec3& v,
                               float scale) noexcept
    {
        return Horse::FVec3{v.X * scale, v.Y * scale, v.Z * scale};
    }

    static bool normalize3(const Horse::FVec3& v,
                           Horse::FVec3& out) noexcept
    {
        const float len = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        if (len <= 0.001f)
            return false;
        out = scale3(v, 1.0f / len);
        return true;
    }

    static float length3(const Horse::FVec3& v) noexcept
    {
        return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    }

    static bool is_sane_vec3(const Horse::FVec3& v) noexcept
    {
        constexpr float kMaxReasonableCoord = 10000000.0f;
        return std::isfinite(v.X) && std::isfinite(v.Y) &&
               std::isfinite(v.Z) &&
               std::fabsf(v.X) < kMaxReasonableCoord &&
               std::fabsf(v.Y) < kMaxReasonableCoord &&
               std::fabsf(v.Z) < kMaxReasonableCoord;
    }

    static bool is_meaningful_offset(const Horse::FVec3& offset) noexcept
    {
        return length3(offset) > 0.001f;
    }

    static void apply_render_offset_to_khit_draw(
        Horse::KHitDraw& d,
        const Horse::FVec3& offset) noexcept
    {
        if (!is_meaningful_offset(offset))
            return;

        switch (d.kind)
        {
            case Horse::KHitKind::Sphere:
                d.centre = add3(d.centre, offset);
                break;

            case Horse::KHitKind::AreaSpine:
                d.spine_p1_world = add3(d.spine_p1_world, offset);
                d.spine_p2_world = add3(d.spine_p2_world, offset);
                d.prev_p1_world = add3(d.prev_p1_world, offset);
                d.prev_p2_world = add3(d.prev_p2_world, offset);
                break;

            case Horse::KHitKind::FixAreaTri:
                for (Horse::FVec3& corner : d.corners)
                    corner = add3(corner, offset);
                break;
        }
    }

    static void apply_render_offset_to_khit_draws(
        std::vector<Horse::KHitDraw> (&draws)[2],
        const Horse::FVec3& offset) noexcept
    {
        if (!is_meaningful_offset(offset))
            return;

        for (auto& player_draws : draws)
        {
            for (Horse::KHitDraw& d : player_draws)
                apply_render_offset_to_khit_draw(d, offset);
        }
    }

    static float max_float(float a, float b) noexcept
    {
        return (a > b) ? a : b;
    }

    struct KHitAuditShapeMetrics
    {
        Horse::FVec3 native_center{};
        Horse::FVec3 ue_center{};
        float native_radius = 0.0f;
        float ue_radius = 0.0f;
    };

    struct KHitAuditCharaPose
    {
        bool ok = false;
        Horse::FVec3 native_pos{};
        Horse::FVec3 ue_pos{};
        uint8_t slot_byte = 0xff;
        bool distance_ok = false;
        float opponent_distance = 0.0f;
    };

    static Horse::FVec3 audit_battle_to_ue_render_world(
        const Horse::FVec3& battleWorld) noexcept
    {
        // Keep audit coordinates identical to KHitWalker render coordinates:
        // native KHit world buffers already use battle Y as vertical.
        return Horse::FVec3{
            battleWorld.X * Horse::kBattleToUE,
            battleWorld.Z * Horse::kBattleToUE,
            battleWorld.Y * Horse::kBattleToUE
        };
    }

    static float dot3(const Horse::FVec3& a,
                      const Horse::FVec3& b) noexcept
    {
        return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    }

    static Horse::FVec3 cross3(const Horse::FVec3& a,
                               const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{
            a.Y * b.Z - a.Z * b.Y,
            a.Z * b.X - a.X * b.Z,
            a.X * b.Y - a.Y * b.X
        };
    }

    static KHitAuditCharaPose read_khit_audit_chara_pose(
        void* chara) noexcept
    {
        KHitAuditCharaPose pose{};
        if (!chara)
            return pose;

        auto* base = reinterpret_cast<uint8_t*>(chara);
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint8_t slot_byte = 0xff;
        const bool ok =
            Horse::SafeReadFloat(base + 0x0C0, &x) &&
            Horse::SafeReadFloat(base + 0x0C4, &y) &&
            Horse::SafeReadFloat(base + 0x0C8, &z) &&
            Horse::SafeReadUInt8(base + 0x23C, &slot_byte);
        if (!ok)
            return pose;

        pose.ok = true;
        pose.native_pos = Horse::FVec3{x, y, z};
        pose.ue_pos = audit_battle_to_ue_render_world(pose.native_pos);
        pose.slot_byte = slot_byte;
        pose.distance_ok =
            Horse::KHitWalker::readOpponentDistance(
                chara, pose.opponent_distance);
        return pose;
    }

    KHitRenderCalibrationPoint read_khit_render_calibration_point(
        void* chara,
        int player_index)
    {
        KHitRenderCalibrationPoint point{};
        const KHitAuditCharaPose pose = read_khit_audit_chara_pose(chara);
        if (pose.ok && is_sane_vec3(pose.native_pos) &&
            is_sane_vec3(pose.ue_pos))
        {
            point.native_ok = true;
            point.native_root = pose.native_pos;
            point.converted_root = pose.ue_pos;
        }

        if (chara)
        {
            auto* obj = reinterpret_cast<UObject*>(chara);
            if (obj && UObject::IsReal(obj))
            {
                Horse::Obj actor{obj};
                const int fn_index =
                    (player_index >= 0 && player_index < 2)
                        ? player_index
                        : 0;
                const Horse::FVec3 actor_root =
                    actor.callVec3Any(m_fn_khit_actor_location[fn_index],
                                      L"GetActorLocation",
                                      L"K2_GetActorLocation");
                if (is_sane_vec3(actor_root))
                {
                    point.actor_ok = true;
                    point.actor_root = actor_root;
                }
            }
        }

        if (point.native_ok && point.actor_ok)
        {
            point.delta = sub3(point.actor_root, point.converted_root);
            point.delta_ok = is_sane_vec3(point.delta);
        }

        return point;
    }

    KHitRenderCalibrationFrame read_khit_render_calibration_frame(
        void* const (&slot_charas)[2])
    {
        KHitRenderCalibrationFrame frame{};
        frame.point[0] =
            read_khit_render_calibration_point(slot_charas[0], 0);
        frame.point[1] =
            read_khit_render_calibration_point(slot_charas[1], 1);
        return frame;
    }

    void update_khit_render_calibration(
        KHitRenderCalibrationFrame& frame) noexcept
    {
        constexpr float kMaxPlayerDeltaDisagreementCm = 25.0f;
        constexpr float kMaxRollingOffsetDriftCm = 35.0f;
        constexpr int kMinStableSamples = 10;
        constexpr int kMaxStableSamples = 60;

        frame.samples = m_khit_render_calibration.samples;

        if (!frame.point[0].delta_ok || !frame.point[1].delta_ok)
        {
            m_khit_render_calibration.valid = false;
            m_khit_render_calibration.samples = 0;
            frame.status = L"missing";
            frame.samples = 0;
            return;
        }

        frame.has_common_delta = true;
        frame.delta_distance =
            distance3(frame.point[0].delta, frame.point[1].delta);
        frame.common_delta =
            midpoint(frame.point[0].delta, frame.point[1].delta);

        if (frame.delta_distance > kMaxPlayerDeltaDisagreementCm)
        {
            m_khit_render_calibration.valid = false;
            m_khit_render_calibration.samples = 0;
            frame.consistent = false;
            frame.status = L"inconsistent";
            frame.samples = 0;
            return;
        }

        frame.consistent = true;
        KHitRenderCalibrationState& state = m_khit_render_calibration;
        const bool has_prior = state.samples > 0;
        const float drift = has_prior
            ? distance3(frame.common_delta, state.offset)
            : 0.0f;
        if (has_prior && drift > kMaxRollingOffsetDriftCm)
        {
            state.valid = false;
            state.samples = 1;
            state.offset = frame.common_delta;
            frame.status = L"warming";
            frame.samples = state.samples;
            return;
        }

        if (!has_prior)
        {
            state.offset = frame.common_delta;
            state.samples = 1;
        }
        else
        {
            const int next_samples =
                (std::min)(state.samples + 1, kMaxStableSamples);
            const float weight_old =
                static_cast<float>(next_samples - 1) /
                static_cast<float>(next_samples);
            const float weight_new = 1.0f / static_cast<float>(next_samples);
            state.offset = add3(scale3(state.offset, weight_old),
                                scale3(frame.common_delta, weight_new));
            state.samples = next_samples;
        }

        state.valid = state.samples >= kMinStableSamples;
        frame.samples = state.samples;
        frame.applied = state.valid;
        frame.active_offset = state.valid ? state.offset : Horse::FVec3{};
        frame.status = state.valid ? L"applied" : L"warming";
    }

    static KHitAuditShapeMetrics audit_shape_metrics(
        const Horse::KHitDraw& d) noexcept
    {
        KHitAuditShapeMetrics m{};
        switch (d.kind)
        {
            case Horse::KHitKind::Sphere:
                m.native_center = d.native_centre;
                m.ue_center = d.centre;
                m.native_radius = d.native_radius;
                m.ue_radius = d.radius;
                break;

            case Horse::KHitKind::AreaSpine:
                m.native_center =
                    midpoint(d.native_spine_p1_world,
                             d.native_spine_p2_world);
                m.ue_center = midpoint(d.spine_p1_world,
                                       d.spine_p2_world);
                m.native_radius = max_float(
                    distance3(m.native_center, d.native_spine_p1_world),
                    distance3(m.native_center, d.native_spine_p2_world));
                m.ue_radius = max_float(
                    distance3(m.ue_center, d.spine_p1_world),
                    distance3(m.ue_center, d.spine_p2_world));
                if (d.has_prev_spine)
                {
                    const Horse::FVec3 native_prev_center =
                        midpoint(d.native_prev_p1_world,
                                 d.native_prev_p2_world);
                    const Horse::FVec3 ue_prev_center =
                        midpoint(d.prev_p1_world, d.prev_p2_world);
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, native_prev_center));
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, d.native_prev_p1_world));
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, d.native_prev_p2_world));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, ue_prev_center));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, d.prev_p1_world));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, d.prev_p2_world));
                }
                break;

            case Horse::KHitKind::FixAreaTri:
                m.native_center = centroid3(d.native_corners[0],
                                            d.native_corners[1],
                                            d.native_corners[2]);
                m.ue_center = centroid3(d.corners[0],
                                        d.corners[1],
                                        d.corners[2]);
                for (int i = 0; i < 3; ++i)
                {
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, d.native_corners[i]));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, d.corners[i]));
                }
                break;
        }
        return m;
    }

    static bool khit_sphere_pair_overlaps_native(
        const Horse::KHitDraw& attack,
        const Horse::KHitDraw& hurt,
        float* out_native_margin = nullptr) noexcept
    {
        if (attack.kind != Horse::KHitKind::Sphere ||
            hurt.kind != Horse::KHitKind::Sphere)
        {
            if (out_native_margin)
                *out_native_margin = 0.0f;
            return false;
        }

        const float native_dist =
            distance3(attack.native_centre, hurt.native_centre);
        const float native_rsum =
            attack.native_radius + hurt.native_radius;
        const float native_margin = native_rsum - native_dist;
        if (out_native_margin)
            *out_native_margin = native_margin;

        // The defender's accepted mask is keyed by attacker slot, not by
        // individual KHit node.  Require exact local sphere/sphere contact
        // before crediting a same-slot sphere as the accepted visual pair.
        constexpr float kNativeOverlapEpsilon = 0.005f;
        return native_margin >= -kNativeOverlapEpsilon;
    }

    static bool khit_pair_has_exact_geometry(
        const Horse::KHitDraw& attack,
        const Horse::KHitDraw& hurt) noexcept
    {
        if (attack.kind == Horse::KHitKind::Sphere &&
            hurt.kind == Horse::KHitKind::Sphere)
        {
            return khit_sphere_pair_overlaps_native(attack, hurt);
        }

        return false;
    }

    static bool khit_pair_geometry_plausible(
        const Horse::KHitDraw& attack,
        const Horse::KHitDraw& hurt) noexcept
    {
        if (attack.kind == Horse::KHitKind::Sphere &&
            hurt.kind == Horse::KHitKind::Sphere)
        {
            return khit_pair_has_exact_geometry(attack, hurt);
        }

        // Area/fix-area native incoming masks prove that an attacker slot
        // touched this hurtbox slot, but they do not identify a unique node
        // when same-slot non-sphere candidates exist. Treat these as broad
        // attribution only; mark_khit_accepted_overlap_candidates decides
        // whether a pair can be promoted to reaction-exact.
        return true;
    }

    void service_khit_sphere_audit_frame(bool have_game_frame,
                                         uint32_t game_frame)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
        {
            m_have_sphere_audit_frame = false;
            m_khit_audit_attack_logs_this_frame = 0;
            m_khit_audit_hurt_logs_this_frame = 0;
            m_khit_audit_pair_logs_this_frame = 0;
            m_khit_audit_calib_logs_this_frame = 0;
            m_khit_audit_cluster_logs_this_frame = 0;
            return;
        }

        const uint32_t audit_frame = have_game_frame
            ? game_frame
            : static_cast<uint32_t>(m_update_calls);
        if (!m_have_sphere_audit_frame ||
            audit_frame != m_last_sphere_audit_frame)
        {
            m_have_sphere_audit_frame = true;
            m_last_sphere_audit_frame = audit_frame;
            m_khit_audit_attack_logs_this_frame = 0;
            m_khit_audit_hurt_logs_this_frame = 0;
            m_khit_audit_pair_logs_this_frame = 0;
            m_khit_audit_calib_logs_this_frame = 0;
            m_khit_audit_cluster_logs_this_frame = 0;
        }
    }

    void maybe_log_khit_audit(
        const Horse::KHitDraw& d,
        int player,
        bool matters_this_frame,
        bool have_game_frame,
        uint32_t game_frame,
        Horse::LineBatcherSlot renderer_slot,
        const Horse::KHitWalker::LaneSnapshot* attacker_lane)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
            return;

        const bool is_attack_audit =
            d.list == Horse::KHitList::Attack &&
            can_expose_khit_attack_for_audit(d);
        const bool has_raw_or_final_reaction =
            d.raw_reaction_state != 0 ||
            d.final_hit_result_code != 0;
        const bool slot_filter_enabled =
            m_khit_sphere_audit_filter_slots.load(std::memory_order_relaxed);
        const bool slot_filter_match =
            !slot_filter_enabled || khit_audit_matches_slot_filter(d);
        const bool accepted_watch_summary =
            slot_filter_enabled &&
            slot_filter_match &&
            d.accepted_overlap_this_frame;
        const bool is_hit_result_audit =
            d.list == Horse::KHitList::Hurtbox &&
            (has_raw_or_final_reaction ||
             d.reaction_overlap_this_frame ||
             d.reaction_hot ||
             accepted_watch_summary);
        if (!is_attack_audit && !is_hit_result_audit)
            return;

        const bool move_filter_match =
            khit_audit_matches_move_filter(d, attacker_lane);
        const bool has_nonzero_incoming_mask =
            d.defender_hurtbox_mask_valid &&
            d.defender_hurtbox_attack_mask != 0;
        if (!move_filter_match &&
            !(is_hit_result_audit &&
              (has_nonzero_incoming_mask || has_raw_or_final_reaction)))
            return;

        if (is_hit_result_audit)
        {
            if (!has_raw_or_final_reaction &&
                !d.reaction_overlap_this_frame &&
                !accepted_watch_summary)
            {
                return;
            }

            if (!consume_khit_audit_log_slot(
                    KHitAuditLogBucket::HurtResult))
                return;

            const int attacker_player = (player == 0) ? 2 : 1;
            const bool attacker_has_move =
                attacker_lane && attacker_lane->has_move;
            const int attacker_packed = attacker_has_move
                ? static_cast<int>(attacker_lane->packed_move)
                : -1;
            const int attacker_low11 = attacker_has_move
                ? (attacker_packed & 0x7ff)
                : -1;
            const wchar_t* summary_kind =
                d.raw_reaction_state != 0
                    ? L"raw"
                    : (d.final_hit_result_code != 0
                        ? L"final"
                        : (d.reaction_overlap_this_frame
                            ? L"reaction_candidate"
                            : L"accepted"));
            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.HurtResult] frame_ok={} frame={} "
                    "summary={} def_p={} atk_p={} atk_move=0x{:04x}/{} "
                    "hurt_node=0x{:x} "
                    "hurt_slot={} raw_react={} sticky_react={} final={} "
                    "mask_valid={} incoming=0x{:016x} filter_slots=({}, {}) "
                    "move_filter_match={} phase={} defender_can_react={} "
                    "overlap={} addressable={} accepted={} accepted_bits=0x{:016x} "
                    "accepted_pairs={} accepted_ambiguous={} "
                    "accepted_exact={} accepted_exact_bits=0x{:016x} "
                    "accepted_exact_pairs={} "
                    "reaction={} reaction_bits=0x{:016x} "
                    "reaction_pairs={} reaction_unique={} reaction_ambiguous={} "
                    "reaction_unresolved={}\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                summary_kind,
                player + 1,
                attacker_player,
                attacker_has_move
                    ? static_cast<unsigned>(attacker_packed & 0xffff)
                    : 0xffffu,
                attacker_low11,
                d.source_node,
                d.hurtbox_slot,
                d.raw_reaction_state,
                d.reaction_state,
                d.final_hit_result_code,
                d.defender_hurtbox_mask_valid,
                d.defender_hurtbox_attack_mask,
                m_khit_sphere_audit_slot_a.load(std::memory_order_relaxed),
                m_khit_sphere_audit_slot_b.load(std::memory_order_relaxed),
                move_filter_match,
                static_cast<int>(d.engine_phase),
                d.defender_can_react_engine,
                d.overlap_active,
                d.classifier_addressable,
                d.accepted_overlap_this_frame,
                d.accepted_overlap_matched_bits,
                d.accepted_overlap_pair_count,
                d.accepted_overlap_ambiguous,
                d.accepted_exact_overlap_this_frame,
                d.accepted_exact_overlap_matched_bits,
                d.accepted_exact_overlap_pair_count,
                d.reaction_overlap_this_frame,
                d.reaction_overlap_matched_bits,
                d.reaction_overlap_pair_count,
                d.raw_reaction_state != 0 &&
                    d.reaction_overlap_pair_count == 1,
                d.reaction_overlap_ambiguous ||
                    (d.raw_reaction_state != 0 &&
                     d.reaction_overlap_pair_count > 1),
                d.raw_reaction_state != 0 &&
                    d.reaction_overlap_pair_count == 0 &&
                    !d.reaction_overlap_ambiguous);
            return;
        }

        if (!move_filter_match)
            return;
        if (!khit_audit_matches_slot_filter(d))
            return;
        if (!consume_khit_audit_log_slot(KHitAuditLogBucket::Attack))
            return;

        if (d.kind == Horse::KHitKind::Sphere)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.Attack] frame_ok={} frame={} p={} "
                    "move=0x{:04x}/{} sub=0x{:04x} node=0x{:x} kind=sphere "
                    "slot={} renderer={} matters={} geom={} mask_selected={} "
                    "active_move_valid={} mask_stale={} "
                    "phase={} classifier_ready={} gates=({},{},{},{}) "
                    "primary=0x{:016x} alt_open={} alt=0x{:016x} "
                    "slot_bit=0x{:016x} accepted={} "
                    "accepted_bits=0x{:016x} accepted_pairs={} "
                    "accepted_exact={} accepted_exact_bits=0x{:016x} "
                    "accepted_exact_pairs={} "
                    "reaction={} reaction_bits=0x{:016x} reaction_pairs={} "
                    "native=({:.3f},{:.3f},{:.3f}) "
                    "ue=({:.1f},{:.1f},{:.1f}) local=({:.3f},{:.3f},{:.3f}) "
                    "auth_local=({:.3f},{:.3f},{:.3f}) "
                    "local_delta=({:.3f},{:.3f},{:.3f}) "
                    "radius_native={:.3f} "
                    "radius_auth={:.3f} radius_scale={:.3f} "
                    "anim_modified={} radius_ue={:.1f}\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                player + 1,
                d.has_move_identity ? d.active_packed_move : 0xFFFFu,
                d.has_move_identity
                    ? static_cast<int>(d.active_move_id_low11)
                    : -1,
                d.move_subframe_id,
                d.source_node,
                static_cast<int>(d.bone_id_internal),
                static_cast<int>(renderer_slot),
                matters_this_frame,
                d.geom_active,
                d.attack_mask_selected,
                d.active_move_valid,
                d.attack_mask_stale,
                static_cast<int>(d.engine_phase),
                d.attack_classifier_ready,
                d.classify_enabled,
                d.attack_in_master_window,
                d.attack_lockout_a,
                d.attack_lockout_b,
                d.primary_attack_mask,
                d.alt_classify_open,
                d.alt_attack_mask,
                d.slot_bit_mask,
                d.accepted_overlap_this_frame,
                d.accepted_overlap_matched_bits,
                d.accepted_overlap_pair_count,
                d.accepted_exact_overlap_this_frame,
                d.accepted_exact_overlap_matched_bits,
                d.accepted_exact_overlap_pair_count,
                d.reaction_overlap_this_frame,
                d.reaction_overlap_matched_bits,
                d.reaction_overlap_pair_count,
                d.native_centre.X,
                d.native_centre.Y,
                d.native_centre.Z,
                d.centre.X,
                d.centre.Y,
                d.centre.Z,
                d.native_live_local_centre.X,
                d.native_live_local_centre.Y,
                d.native_live_local_centre.Z,
                d.native_authored_local_centre.X,
                d.native_authored_local_centre.Y,
                d.native_authored_local_centre.Z,
                d.sphere_live_local_delta.X,
                d.sphere_live_local_delta.Y,
                d.sphere_live_local_delta.Z,
                d.native_radius,
                d.native_authored_radius,
                d.sphere_live_radius_scale,
                d.sphere_anim_modified,
                d.radius);
            return;
        }

        if (d.kind == Horse::KHitKind::AreaSpine)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.Attack] frame_ok={} frame={} p={} "
                    "move=0x{:04x}/{} sub=0x{:04x} node=0x{:x} kind=area "
                    "slot={} renderer={} matters={} geom={} mask_selected={} "
                    "active_move_valid={} mask_stale={} "
                    "phase={} classifier_ready={} gates=({},{},{},{}) "
                    "primary=0x{:016x} alt_open={} alt=0x{:016x} "
                    "slot_bit=0x{:016x} accepted={} "
                    "accepted_bits=0x{:016x} accepted_pairs={} "
                    "accepted_exact={} accepted_exact_bits=0x{:016x} "
                    "accepted_exact_pairs={} "
                    "reaction={} reaction_bits=0x{:016x} reaction_pairs={} "
                    "native_p1=({:.3f},{:.3f},{:.3f}) "
                    "native_p2=({:.3f},{:.3f},{:.3f}) "
                    "native_prev_p1=({:.3f},{:.3f},{:.3f}) "
                    "native_prev_p2=({:.3f},{:.3f},{:.3f}) has_prev={} "
                    "ue_p1=({:.1f},{:.1f},{:.1f}) ue_p2=({:.1f},{:.1f},{:.1f})\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                player + 1,
                d.has_move_identity ? d.active_packed_move : 0xFFFFu,
                d.has_move_identity
                    ? static_cast<int>(d.active_move_id_low11)
                    : -1,
                d.move_subframe_id,
                d.source_node,
                static_cast<int>(d.bone_id_internal),
                static_cast<int>(renderer_slot),
                matters_this_frame,
                d.geom_active,
                d.attack_mask_selected,
                d.active_move_valid,
                d.attack_mask_stale,
                static_cast<int>(d.engine_phase),
                d.attack_classifier_ready,
                d.classify_enabled,
                d.attack_in_master_window,
                d.attack_lockout_a,
                d.attack_lockout_b,
                d.primary_attack_mask,
                d.alt_classify_open,
                d.alt_attack_mask,
                d.slot_bit_mask,
                d.accepted_overlap_this_frame,
                d.accepted_overlap_matched_bits,
                d.accepted_overlap_pair_count,
                d.accepted_exact_overlap_this_frame,
                d.accepted_exact_overlap_matched_bits,
                d.accepted_exact_overlap_pair_count,
                d.reaction_overlap_this_frame,
                d.reaction_overlap_matched_bits,
                d.reaction_overlap_pair_count,
                d.native_spine_p1_world.X,
                d.native_spine_p1_world.Y,
                d.native_spine_p1_world.Z,
                d.native_spine_p2_world.X,
                d.native_spine_p2_world.Y,
                d.native_spine_p2_world.Z,
                d.native_prev_p1_world.X,
                d.native_prev_p1_world.Y,
                d.native_prev_p1_world.Z,
                d.native_prev_p2_world.X,
                d.native_prev_p2_world.Y,
                d.native_prev_p2_world.Z,
                d.has_prev_spine,
                d.spine_p1_world.X,
                d.spine_p1_world.Y,
                d.spine_p1_world.Z,
                d.spine_p2_world.X,
                d.spine_p2_world.Y,
                d.spine_p2_world.Z);
            return;
        }

        Output::send<LogLevel::Default>(
            STR("[HorseMod.KHitAudit.Attack] frame_ok={} frame={} p={} "
                "move=0x{:04x}/{} sub=0x{:04x} node=0x{:x} kind=fixarea "
                "slot={} renderer={} matters={} geom={} mask_selected={} "
                "active_move_valid={} mask_stale={} "
                "phase={} classifier_ready={} gates=({},{},{},{}) "
                "primary=0x{:016x} alt_open={} alt=0x{:016x} "
                "slot_bit=0x{:016x} accepted={} "
                "accepted_bits=0x{:016x} accepted_pairs={} "
                "accepted_exact={} accepted_exact_bits=0x{:016x} "
                "accepted_exact_pairs={} "
                "reaction={} reaction_bits=0x{:016x} reaction_pairs={} "
                "native_p1=({:.3f},{:.3f},{:.3f}) "
                "native_p2=({:.3f},{:.3f},{:.3f}) "
                "native_p3=({:.3f},{:.3f},{:.3f}) "
                "ue_p1=({:.1f},{:.1f},{:.1f}) ue_p2=({:.1f},{:.1f},{:.1f}) "
                "ue_p3=({:.1f},{:.1f},{:.1f})\n"),
            have_game_frame,
            have_game_frame ? game_frame : 0,
            player + 1,
            d.has_move_identity ? d.active_packed_move : 0xFFFFu,
            d.has_move_identity
                ? static_cast<int>(d.active_move_id_low11)
                : -1,
            d.move_subframe_id,
            d.source_node,
            static_cast<int>(d.bone_id_internal),
            static_cast<int>(renderer_slot),
            matters_this_frame,
            d.geom_active,
            d.attack_mask_selected,
            d.active_move_valid,
            d.attack_mask_stale,
            static_cast<int>(d.engine_phase),
            d.attack_classifier_ready,
            d.classify_enabled,
            d.attack_in_master_window,
            d.attack_lockout_a,
            d.attack_lockout_b,
            d.primary_attack_mask,
            d.alt_classify_open,
            d.alt_attack_mask,
            d.slot_bit_mask,
            d.accepted_overlap_this_frame,
            d.accepted_overlap_matched_bits,
            d.accepted_overlap_pair_count,
            d.accepted_exact_overlap_this_frame,
            d.accepted_exact_overlap_matched_bits,
            d.accepted_exact_overlap_pair_count,
            d.reaction_overlap_this_frame,
            d.reaction_overlap_matched_bits,
            d.reaction_overlap_pair_count,
            d.native_corners[0].X,
            d.native_corners[0].Y,
            d.native_corners[0].Z,
            d.native_corners[1].X,
            d.native_corners[1].Y,
            d.native_corners[1].Z,
            d.native_corners[2].X,
            d.native_corners[2].Y,
            d.native_corners[2].Z,
            d.corners[0].X,
            d.corners[0].Y,
            d.corners[0].Z,
            d.corners[1].X,
            d.corners[1].Y,
            d.corners[1].Z,
            d.corners[2].X,
            d.corners[2].Y,
                            d.corners[2].Z);
    }

    static const char* khit_kind_short(Horse::KHitKind kind) noexcept
    {
        switch (kind)
        {
            case Horse::KHitKind::Sphere:     return "S";
            case Horse::KHitKind::AreaSpine:  return "A";
            case Horse::KHitKind::FixAreaTri: return "F";
        }
        return "?";
    }

    void maybe_log_khit_attack_clusters(
        const std::vector<Horse::KHitDraw> (&draws)[2],
        bool have_game_frame,
        uint32_t game_frame,
        Horse::LineBatcherSlot hit_renderer_slot)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
            return;

        for (int player = 0; player < 2; ++player)
        {
            std::vector<const Horse::KHitDraw*> nodes;
            nodes.reserve(draws[player].size());
            int selected_count = 0;
            int ready_count = 0;
            int stale_count = 0;
            int modified_sphere_count = 0;

            for (const Horse::KHitDraw& d : draws[player])
            {
                if (d.list != Horse::KHitList::Attack ||
                    !d.geom_active ||
                    !khit_audit_matches_move_filter(d, nullptr) ||
                    !khit_audit_matches_slot_filter(d))
                {
                    continue;
                }

                const bool selected_for_audit =
                    d.attack_mask_selected ||
                    d.attack_mask_stale ||
                    d.accepted_overlap_this_frame ||
                    d.accepted_exact_overlap_this_frame;
                if (!selected_for_audit)
                    continue;

                nodes.push_back(&d);
                if (d.attack_mask_selected)
                    ++selected_count;
                if (d.attack_classifier_ready)
                    ++ready_count;
                if (d.attack_mask_stale)
                    ++stale_count;
                if (d.kind == Horse::KHitKind::Sphere &&
                    d.sphere_anim_modified)
                {
                    ++modified_sphere_count;
                }
            }

            if (nodes.size() < 2)
                continue;
            if (!consume_khit_audit_log_slot(
                    KHitAuditLogBucket::AttackCluster))
                return;

            const Horse::KHitDraw& first = *nodes.front();
            std::string slot_summary;
            slot_summary.reserve(1024);
            size_t emitted = 0;
            for (const Horse::KHitDraw* node : nodes)
            {
                if (!node || slot_summary.size() >= 1400)
                    break;

                char item[192]{};
                if (node->kind == Horse::KHitKind::Sphere)
                {
                    std::snprintf(
                        item, sizeof(item),
                        "%s%d:r=%.3f,scale=%.3f,d=(%.3f,%.3f,%.3f)%s%s",
                        emitted ? ";" : "",
                        static_cast<int>(node->bone_id_internal),
                        node->native_radius,
                        node->sphere_live_radius_scale,
                        node->sphere_live_local_delta.X,
                        node->sphere_live_local_delta.Y,
                        node->sphere_live_local_delta.Z,
                        node->sphere_anim_modified ? ",mod" : "",
                        node->attack_mask_stale ? ",stale" : "");
                }
                else
                {
                    std::snprintf(
                        item, sizeof(item),
                        "%s%d:%s%s",
                        emitted ? ";" : "",
                        static_cast<int>(node->bone_id_internal),
                        khit_kind_short(node->kind),
                        node->attack_mask_stale ? ",stale" : "");
                }
                slot_summary += item;
                ++emitted;
            }
            if (emitted < nodes.size())
                slot_summary += ";...";

            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.AttackCluster] frame_ok={} "
                    "frame={} p={} move=0x{:04x}/{} sub=0x{:04x} "
                    "renderer={} active_move_valid={} primary=0x{:016x} "
                    "node_count={} selected_count={} ready_count={} "
                    "stale_count={} modified_spheres={} slots={}\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                player + 1,
                first.has_move_identity ? first.active_packed_move : 0xFFFFu,
                first.has_move_identity
                    ? static_cast<int>(first.active_move_id_low11)
                    : -1,
                first.move_subframe_id,
                static_cast<int>(hit_renderer_slot),
                first.active_move_valid,
                first.primary_attack_mask,
                static_cast<int>(nodes.size()),
                selected_count,
                ready_count,
                stale_count,
                modified_sphere_count,
                RC::to_generic_string(slot_summary.c_str()));
        }
    }

    static void mark_khit_accepted_overlap_candidates(
        std::vector<Horse::KHitDraw> (&draws)[2])
    {
        struct KHitOverlapCandidate
        {
            Horse::KHitDraw* attack = nullptr;
            uint64_t matched_bits = 0;
            bool exact_geometry = false;
            bool same_slot_ambiguous = false;
        };

        for (auto& player_draws : draws)
        {
            for (Horse::KHitDraw& d : player_draws)
            {
                d.accepted_overlap_this_frame = false;
                d.accepted_overlap_matched_bits = 0;
                d.accepted_overlap_pair_count = 0;
                d.accepted_overlap_ambiguous = false;
                d.accepted_exact_overlap_this_frame = false;
                d.accepted_exact_overlap_matched_bits = 0;
                d.accepted_exact_overlap_pair_count = 0;
                d.reaction_overlap_this_frame = false;
                d.reaction_overlap_matched_bits = 0;
                d.reaction_overlap_pair_count = 0;
                d.reaction_overlap_ambiguous = false;
            }
        }

        for (int defender = 0; defender < 2; ++defender)
        {
            const int attacker = (defender == 0) ? 1 : 0;
            for (Horse::KHitDraw& hurt : draws[defender])
            {
                if (hurt.list != Horse::KHitList::Hurtbox ||
                    !hurt.defender_hurtbox_mask_valid ||
                    hurt.defender_hurtbox_attack_mask == 0)
                {
                    continue;
                }

                std::vector<KHitOverlapCandidate> candidates;
                candidates.reserve(draws[attacker].size());
                for (Horse::KHitDraw& attack : draws[attacker])
                {
                    if (attack.list != Horse::KHitList::Attack ||
                        !attack.geom_active)
                    {
                        continue;
                    }

                    const uint64_t matched_bits =
                        hurt.defender_hurtbox_attack_mask &
                        attack.slot_bit_mask;
                    if (matched_bits == 0)
                        continue;
                    if (!khit_pair_geometry_plausible(attack, hurt))
                        continue;

                    candidates.push_back(KHitOverlapCandidate{
                        &attack,
                        matched_bits,
                        khit_pair_has_exact_geometry(attack, hurt),
                        false
                    });
                }

                for (size_t i = 0; i < candidates.size(); ++i)
                {
                    for (size_t j = i + 1; j < candidates.size(); ++j)
                    {
                        if ((candidates[i].matched_bits &
                             candidates[j].matched_bits) == 0)
                        {
                            continue;
                        }
                        candidates[i].same_slot_ambiguous = true;
                        candidates[j].same_slot_ambiguous = true;
                    }
                }

                for (KHitOverlapCandidate& c : candidates)
                {
                    Horse::KHitDraw& attack = *c.attack;

                    attack.accepted_overlap_this_frame = true;
                    attack.accepted_overlap_matched_bits |= c.matched_bits;
                    ++attack.accepted_overlap_pair_count;
                    if (c.same_slot_ambiguous)
                        attack.accepted_overlap_ambiguous = true;
                    if (c.exact_geometry)
                    {
                        attack.accepted_exact_overlap_this_frame = true;
                        attack.accepted_exact_overlap_matched_bits |=
                            c.matched_bits;
                        ++attack.accepted_exact_overlap_pair_count;
                    }

                    hurt.accepted_overlap_this_frame = true;
                    hurt.accepted_overlap_matched_bits |= c.matched_bits;
                    ++hurt.accepted_overlap_pair_count;
                    if (c.same_slot_ambiguous)
                        hurt.accepted_overlap_ambiguous = true;
                    if (c.exact_geometry)
                    {
                        hurt.accepted_exact_overlap_this_frame = true;
                        hurt.accepted_exact_overlap_matched_bits |=
                            c.matched_bits;
                        ++hurt.accepted_exact_overlap_pair_count;
                    }

                    if (hurt.raw_reaction_state != 0 &&
                        canMatterThisFrame(attack))
                    {
                        const bool can_promote_to_reaction =
                            c.exact_geometry || !c.same_slot_ambiguous;
                        if (!can_promote_to_reaction)
                        {
                            attack.reaction_overlap_ambiguous = true;
                            hurt.reaction_overlap_ambiguous = true;
                            continue;
                        }

                        attack.reaction_overlap_this_frame = true;
                        attack.reaction_overlap_matched_bits |= c.matched_bits;
                        ++attack.reaction_overlap_pair_count;

                        hurt.reaction_overlap_this_frame = true;
                        hurt.reaction_overlap_matched_bits |= c.matched_bits;
                        ++hurt.reaction_overlap_pair_count;
                    }
                }
            }
        }
    }

    void maybe_log_khit_overlap_pairs(
        const std::vector<Horse::KHitDraw> (&draws)[2],
        const Horse::KHitWalker::LaneSnapshot (&lane_snapshots)[2],
        const KHitRenderCalibrationFrame& render_calib,
        bool have_game_frame,
        uint32_t game_frame,
        Horse::LineBatcherSlot hit_renderer_slot,
        Horse::LineBatcherSlot hurt_renderer_slot)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
            return;

        for (int defender = 0; defender < 2; ++defender)
        {
            const int attacker = (defender == 0) ? 1 : 0;
            const auto* attacker_lane = &lane_snapshots[attacker];
            const bool attacker_has_move =
                attacker_lane && attacker_lane->has_move;
            const int attacker_packed = attacker_has_move
                ? static_cast<int>(attacker_lane->packed_move)
                : -1;
            const int attacker_low11 = attacker_has_move
                ? (attacker_packed & 0x7ff)
                : -1;

            for (const Horse::KHitDraw& hurt : draws[defender])
            {
                if (hurt.list != Horse::KHitList::Hurtbox ||
                    !hurt.defender_hurtbox_mask_valid ||
                    hurt.defender_hurtbox_attack_mask == 0)
                {
                    continue;
                }
                const bool move_filter_match =
                    khit_audit_matches_move_filter(hurt, attacker_lane);

                for (const Horse::KHitDraw& attack : draws[attacker])
                {
                    if (attack.list != Horse::KHitList::Attack)
                        continue;

                    const uint64_t matched_bits =
                        hurt.defender_hurtbox_attack_mask &
                        attack.slot_bit_mask;
                    if (matched_bits == 0)
                        continue;
                    if (!attack.geom_active)
                        continue;
                    if (!khit_pair_geometry_plausible(attack, hurt))
                        continue;

                    if (m_khit_sphere_audit_filter_slots.load(
                            std::memory_order_relaxed) &&
                        !khit_audit_matches_slot_filter(attack) &&
                        !khit_audit_matches_slot_filter(hurt))
                    {
                        continue;
                    }
                    if (!consume_khit_audit_log_slot(
                            KHitAuditLogBucket::OverlapPair))
                        return;

                    const bool atk_matters = canMatterThisFrame(attack);
                    const bool hurt_matters =
                        canMatterThisFrame(hurt) ||
                        hurt.raw_reaction_state != 0;
                    const bool reaction_candidate =
                        (hurt.reaction_overlap_matched_bits &
                         attack.slot_bit_mask) != 0;
                    const bool exact_geometry =
                        khit_pair_has_exact_geometry(attack, hurt);
                    const bool accepted_exact_pair =
                        (hurt.accepted_exact_overlap_matched_bits &
                         attack.slot_bit_mask) != 0;
                    const bool accepted_only =
                        accepted_exact_pair && !reaction_candidate;

                    const KHitAuditShapeMetrics atk_m =
                        audit_shape_metrics(attack);
                    const KHitAuditShapeMetrics hurt_m =
                        audit_shape_metrics(hurt);
                    const float native_dist =
                        distance3(atk_m.native_center,
                                  hurt_m.native_center);
                    const float ue_dist =
                        distance3(atk_m.ue_center,
                                  hurt_m.ue_center);
                    const float native_rsum =
                        atk_m.native_radius + hurt_m.native_radius;
                    const float ue_rsum =
                        atk_m.ue_radius + hurt_m.ue_radius;
                    const KHitAuditCharaPose atk_pose =
                        read_khit_audit_chara_pose(
                            Horse::KHitWalker::charaSlotFromGlobal(
                                static_cast<uint32_t>(attacker)));
                    const KHitAuditCharaPose def_pose =
                        read_khit_audit_chara_pose(
                            Horse::KHitWalker::charaSlotFromGlobal(
                                static_cast<uint32_t>(defender)));

                    Horse::FVec3 native_contact = midpoint(
                        atk_m.native_center, hurt_m.native_center);
                    bool native_contact_valid = false;
                    Horse::FVec3 native_contact_dir{};
                    if (normalize3(sub3(hurt_m.native_center,
                                        atk_m.native_center),
                                   native_contact_dir))
                    {
                        const Horse::FVec3 attack_shell =
                            add3(atk_m.native_center,
                                 scale3(native_contact_dir,
                                        atk_m.native_radius));
                        const Horse::FVec3 hurt_shell =
                            sub3(hurt_m.native_center,
                                 scale3(native_contact_dir,
                                        hurt_m.native_radius));
                        native_contact = midpoint(attack_shell, hurt_shell);
                        native_contact_valid = true;
                    }
                    const Horse::FVec3 ue_contact =
                        audit_battle_to_ue_render_world(native_contact);

                    Horse::FVec3 attacker_to_defender_axis{};
                    bool attacker_to_defender_axis_valid = false;
                    if (atk_pose.ok && def_pose.ok)
                    {
                        Horse::FVec3 flat_delta =
                            sub3(def_pose.native_pos, atk_pose.native_pos);
                        flat_delta.Y = 0.0f;
                        attacker_to_defender_axis_valid =
                            normalize3(flat_delta,
                                       attacker_to_defender_axis);
                    }
                    const Horse::FVec3 attacker_right_axis{
                        attacker_to_defender_axis.Z,
                        0.0f,
                        -attacker_to_defender_axis.X
                    };
                    auto signed_forward = [&](const Horse::FVec3& p) {
                        return attacker_to_defender_axis_valid
                            ? dot3(sub3(p, atk_pose.native_pos),
                                   attacker_to_defender_axis)
                            : 0.0f;
                    };
                    auto signed_right = [&](const Horse::FVec3& p) {
                        return attacker_to_defender_axis_valid
                            ? dot3(sub3(p, atk_pose.native_pos),
                                   attacker_right_axis)
                            : 0.0f;
                    };
                    auto signed_up = [&](const Horse::FVec3& p) {
                        return atk_pose.ok ? (p.Y - atk_pose.native_pos.Y)
                                           : 0.0f;
                    };

                    Output::send<LogLevel::Default>(
                        STR("[HorseMod.KHitAudit.OverlapPair] frame_ok={} "
                            "frame={} def_p={} atk_p={} "
                            "atk_move=0x{:04x}/{} "
                            "hurt_node=0x{:x} hurt_kind={} hurt_slot={} "
                            "atk_node=0x{:x} atk_kind={} atk_slot={} "
                            "atk_slot_bit=0x{:016x} matched=0x{:016x} "
                            "incoming=0x{:016x} move_filter_match={} "
                            "raw_react={} sticky_react={} final={} "
                            "atk_phase={} atk_geom={} atk_mask_selected={} "
                            "atk_active_move_valid={} atk_mask_stale={} "
                            "atk_matters={} hurt_matters={} "
                            "reaction_candidate={} reaction_pair_count={} "
                            "exact_geometry={} accepted_exact={} "
                            "accepted_only={} accepted_ambiguous={} "
                            "reaction_ambiguous={} "
                            "renderer_hit={} renderer_hurt={} "
                            "atk_local=({:.3f},{:.3f},{:.3f}) "
                            "native_atk=({:.3f},{:.3f},{:.3f}) "
                            "native_hurt=({:.3f},{:.3f},{:.3f}) "
                            "native_dist={:.3f} native_rsum={:.3f} "
                            "native_margin={:.3f} "
                            "ue_atk=({:.1f},{:.1f},{:.1f}) "
                            "ue_hurt=({:.1f},{:.1f},{:.1f}) "
                            "ue_dist={:.1f} ue_rsum={:.1f} "
                            "ue_margin={:.1f} "
                            "atk_radius_native={:.3f} "
                            "hurt_radius_native={:.3f} "
                            "atk_radius_ue={:.1f} hurt_radius_ue={:.1f}\n"),
                        have_game_frame,
                        have_game_frame ? game_frame : 0,
                        defender + 1,
                        attacker + 1,
                        attacker_has_move
                            ? static_cast<unsigned>(attacker_packed & 0xffff)
                            : 0xffffu,
                        attacker_low11,
                        hurt.source_node,
                        static_cast<int>(hurt.kind),
                        hurt.hurtbox_slot,
                        attack.source_node,
                        static_cast<int>(attack.kind),
                        static_cast<int>(attack.bone_id_internal),
                        attack.slot_bit_mask,
                        matched_bits,
                        hurt.defender_hurtbox_attack_mask,
                        move_filter_match,
                        hurt.raw_reaction_state,
                        hurt.reaction_state,
                        hurt.final_hit_result_code,
                        static_cast<int>(attack.engine_phase),
                        attack.geom_active,
                        attack.attack_mask_selected,
                        attack.active_move_valid,
                        attack.attack_mask_stale,
                        atk_matters,
                        hurt_matters,
                        reaction_candidate,
                        hurt.reaction_overlap_pair_count,
                        exact_geometry,
                        accepted_exact_pair,
                        accepted_only,
                        attack.accepted_overlap_ambiguous ||
                            hurt.accepted_overlap_ambiguous,
                        attack.reaction_overlap_ambiguous ||
                            hurt.reaction_overlap_ambiguous,
                        static_cast<int>(hit_renderer_slot),
                        static_cast<int>(hurt_renderer_slot),
                        attack.native_live_local_centre.X,
                        attack.native_live_local_centre.Y,
                        attack.native_live_local_centre.Z,
                        atk_m.native_center.X,
                        atk_m.native_center.Y,
                        atk_m.native_center.Z,
                        hurt_m.native_center.X,
                        hurt_m.native_center.Y,
                        hurt_m.native_center.Z,
                        native_dist,
                        native_rsum,
                        native_rsum - native_dist,
                        atk_m.ue_center.X,
                        atk_m.ue_center.Y,
                        atk_m.ue_center.Z,
                        hurt_m.ue_center.X,
                        hurt_m.ue_center.Y,
                        hurt_m.ue_center.Z,
                        ue_dist,
                        ue_rsum,
                        ue_rsum - ue_dist,
                        atk_m.native_radius,
                        hurt_m.native_radius,
                        atk_m.ue_radius,
                        hurt_m.ue_radius);

                    Output::send<LogLevel::Default>(
                        STR("[HorseMod.KHitAudit.OverlapSpace] frame_ok={} "
                            "frame={} def_p={} atk_p={} "
                            "atk_node=0x{:x} hurt_node=0x{:x} "
                            "atk_pose_ok={} def_pose_ok={} "
                            "atk_pos=({:.3f},{:.3f},{:.3f}) "
                            "def_pos=({:.3f},{:.3f},{:.3f}) "
                            "atk_slot_byte={} def_slot_byte={} "
                            "atk_opp_dist_ok={} atk_opp_dist={:.3f} "
                            "axis_ok={} atk_to_def_axis=({:.3f},{:.3f},{:.3f}) "
                            "contact_ok={} native_contact=({:.3f},{:.3f},{:.3f}) "
                            "ue_contact=({:.1f},{:.1f},{:.1f}) "
                            "atk_center_rel=({:.3f},{:.3f},{:.3f}) "
                            "contact_rel=({:.3f},{:.3f},{:.3f}) "
                            "hurt_center_rel=({:.3f},{:.3f},{:.3f}) "
                            "atk_per_frame={} atk_can_strike={} atk_matters={} "
                            "hurt_addressable={} hurt_overlap={} "
                            "hurt_can_react={} hurt_matters={}\n"),
                        have_game_frame,
                        have_game_frame ? game_frame : 0,
                        defender + 1,
                        attacker + 1,
                        attack.source_node,
                        hurt.source_node,
                        atk_pose.ok,
                        def_pose.ok,
                        atk_pose.native_pos.X,
                        atk_pose.native_pos.Y,
                        atk_pose.native_pos.Z,
                        def_pose.native_pos.X,
                        def_pose.native_pos.Y,
                        def_pose.native_pos.Z,
                        static_cast<unsigned>(atk_pose.slot_byte),
                        static_cast<unsigned>(def_pose.slot_byte),
                        atk_pose.distance_ok,
                        atk_pose.opponent_distance,
                        attacker_to_defender_axis_valid,
                        attacker_to_defender_axis.X,
                        attacker_to_defender_axis.Y,
                        attacker_to_defender_axis.Z,
                        native_contact_valid,
                        native_contact.X,
                        native_contact.Y,
                        native_contact.Z,
                        ue_contact.X,
                        ue_contact.Y,
                        ue_contact.Z,
                        signed_forward(atk_m.native_center),
                        signed_right(atk_m.native_center),
                        signed_up(atk_m.native_center),
                        signed_forward(native_contact),
                        signed_right(native_contact),
                        signed_up(native_contact),
                        signed_forward(hurt_m.native_center),
                        signed_right(hurt_m.native_center),
                        signed_up(hurt_m.native_center),
                        attack.is_per_frame_active,
                        attack.attacker_can_strike_engine,
                        canMatterThisFrame(attack),
                        hurt.classifier_addressable,
                        hurt.overlap_active,
                        hurt.defender_can_react_engine,
                        canMatterThisFrame(hurt));

                    if ((hurt.raw_reaction_state != 0 ||
                         reaction_candidate) &&
                        consume_khit_audit_log_slot(
                            KHitAuditLogBucket::Calibration))
                    {
                        const KHitRenderCalibrationPoint& atk_cal =
                            render_calib.point[attacker];
                        const KHitRenderCalibrationPoint& def_cal =
                            render_calib.point[defender];
                        const Horse::FVec3 candidate_ue =
                            midpoint(atk_m.ue_center, hurt_m.ue_center);
                        const Horse::FVec3 draw_contact_ue =
                            add3(ue_contact, render_calib.active_offset);

                        Output::send<LogLevel::Default>(
                            STR("[HorseMod.KHitRenderCalib] frame_ok={} "
                                "frame={} status={} applied={} samples={} "
                                "consistent={} delta_dist={:.1f} "
                                "offset=({:.1f},{:.1f},{:.1f}) "
                                "def_p={} atk_p={} atk_node=0x{:x} "
                                "hurt_node=0x{:x} reaction_candidate={} "
                                "raw_react={} final={} "
                                "atk_native_root=({:.3f},{:.3f},{:.3f}) "
                                "atk_root_ue=({:.1f},{:.1f},{:.1f}) "
                                "atk_actor_ue=({:.1f},{:.1f},{:.1f}) "
                                "atk_delta=({:.1f},{:.1f},{:.1f}) "
                                "def_native_root=({:.3f},{:.3f},{:.3f}) "
                                "def_root_ue=({:.1f},{:.1f},{:.1f}) "
                                "def_actor_ue=({:.1f},{:.1f},{:.1f}) "
                                "def_delta=({:.1f},{:.1f},{:.1f}) "
                                "candidate_ue=({:.1f},{:.1f},{:.1f}) "
                                "native_contact=({:.3f},{:.3f},{:.3f}) "
                                "converted_contact=({:.1f},{:.1f},{:.1f}) "
                                "draw_contact=({:.1f},{:.1f},{:.1f})\n"),
                            have_game_frame,
                            have_game_frame ? game_frame : 0,
                            render_calib.status,
                            render_calib.applied,
                            render_calib.samples,
                            render_calib.consistent,
                            render_calib.delta_distance,
                            render_calib.active_offset.X,
                            render_calib.active_offset.Y,
                            render_calib.active_offset.Z,
                            defender + 1,
                            attacker + 1,
                            attack.source_node,
                            hurt.source_node,
                            reaction_candidate,
                            hurt.raw_reaction_state,
                            hurt.final_hit_result_code,
                            atk_cal.native_root.X,
                            atk_cal.native_root.Y,
                            atk_cal.native_root.Z,
                            atk_cal.converted_root.X,
                            atk_cal.converted_root.Y,
                            atk_cal.converted_root.Z,
                            atk_cal.actor_root.X,
                            atk_cal.actor_root.Y,
                            atk_cal.actor_root.Z,
                            atk_cal.delta.X,
                            atk_cal.delta.Y,
                            atk_cal.delta.Z,
                            def_cal.native_root.X,
                            def_cal.native_root.Y,
                            def_cal.native_root.Z,
                            def_cal.converted_root.X,
                            def_cal.converted_root.Y,
                            def_cal.converted_root.Z,
                            def_cal.actor_root.X,
                            def_cal.actor_root.Y,
                            def_cal.actor_root.Z,
                            def_cal.delta.X,
                            def_cal.delta.Y,
                            def_cal.delta.Z,
                            candidate_ue.X,
                            candidate_ue.Y,
                            candidate_ue.Z,
                            native_contact.X,
                            native_contact.Y,
                            native_contact.Z,
                            ue_contact.X,
                            ue_contact.Y,
                            ue_contact.Z,
                            draw_contact_ue.X,
                            draw_contact_ue.Y,
                            draw_contact_ue.Z);
                    }
                }
            }
        }
    }

    void clear_persistent_khit_trails()
    {
        if (m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
            (void)m_backend_hit.clearLines();
        if (m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
            (void)m_backend_hurt.clearLines();
        m_have_trail_game_frame = false;
    }

    void hide_khit_overlay_lines()
    {
        m_backend_hit.hideAll();
        m_backend_hurt.hideAll();
        m_backend_hit_once.hideAll();
        m_backend_hurt_once.hideAll();
        m_khit_render_calibration = {};
        m_have_trail_game_frame = false;
    }

    // (Secondary attack-role filter / shouldShowAttackRole was removed
    // 2026-04 along with the UI for it.  Strike vs Throw partitioning
    // turned out to be more noise than signal for practical hitbox
    // inspection - users just want "show all attack volumes" or
    // "show none," which the master Attacks per-player checkbox
    // already covers.  If you want them back, the engine split is
    // documented at KHitAttackRole + the classifier at
    // LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100.)

    // ==================================================================
    // Settings persistence - file-backed via Horse::ModSettings.
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
    // periodically from on_update (every ~120 frames - 2s at 60 FPS)
    // so slider drags don't spam the disk, and once more from the
    // dtor so the final state lands on disk on graceful shutdown.
    //
    // What we DON'T persist: runtime state (m_update_calls, hook
    // bookkeeping), transient toggles (m_freeze_frame, m_step_pending,
    // overlay visibility - user wants overlay hidden on launch
    // regardless), diagnostic-only flags.
    //
    // Key-naming convention: snake_case, descriptive over short, no
    // prefix.  Old/renamed settings are safe to leave in the file -
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
        // Default: master narrow ON, both per-list overrides OFF - gives
        // the engine-truth "what's hitting RIGHT NOW" view on first
        // launch.  The legacy keys (`damage_active_only`,
        // `show_unused_hurtboxes`) are silently dropped; users who had
        // them set to non-default values will land on the new defaults.
        m_only_show_active       .store(S.get_bool ("only_show_active",     true ));
        m_flash_frames           .store(S.get_int  ("hit_flash_frames",      15   ));
        m_thickness              .store(S.get_float("thickness",             1.5f ));
        // Per-feature line-batcher slot.  Hitboxes default to Foreground
        // (always-on-top, the only sensible choice - Persistent would
        // pile up unreadable trails).  Hurtboxes also default to
        // Foreground but the user can flip them to Persistent to trace
        // a chara's hurtbox path through a move.  The legacy single
        // key "line_batcher_slot" from before the split is silently
        // ignored - old enum values aren't valid in the new 2-entry
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
        m_show_stage_boundary    .store(S.get_bool ("show_stage_boundary",   false));
        m_hide_stage_visuals     .store(S.get_bool ("hide_stage_visuals",    false));
        m_show_retrack_events    .store(S.get_bool ("show_retrack_events",   false));

        // --- Reset override -----------------------------------------
        // Captured pose persists across reboots so the user can resume
        // training from the same custom starting position.  The toggle
        // itself does NOT persist - it's deliberately reset to OFF on
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
    // flag - cheap to call every on_update tick.
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
        S.set("show_stage_boundary",   m_show_stage_boundary.load());
        S.set("hide_stage_visuals",    m_hide_stage_visuals.load());
        S.set("show_retrack_events",   m_show_retrack_events.load());

        // --- Reset override ----------------------------------------
        // The toggle is deliberately NOT persisted - see the matching
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
            }
        }

        // HorseMod online policy persists across reboots so the user's
        // chosen modded-lobby ruleset survives a restart.  Unlike the
        // reset-override toggle, this one IS persistent - it's a
        // long-lived "what kind of online matches do I want" pref,
        // not a session-scoped behaviour.
        S.set("online_policy",
              static_cast<int>(Horse::OnlineRules::instance().current_policy()));
        // GameMode "Auto disable online" - see load path for the
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
    int                          m_engine_fallback_last_cockpit_calls = 0;
    int                          m_engine_fallback_missed_ticks = 0;
    bool                         m_engine_fallback_logged = false;

    // Reset-override UFunction hook bookkeeping.
    //
    // We don't actually know which UFunction the user's reset bind invokes -
    // SC6 has at least four candidate paths that all eventually run the
    // training-mode position-reset chain:
    //
    //   /Script/LuxorGame.LuxBattleManager:TrainingModePositionReset
    //   /Script/LuxorGame.LuxBattleManager:RestartBattle
    //   /Script/LuxorGame.LuxBattleManager:RestartBattleImmediately
    //   /Script/LuxorGame.LuxBattleFunctionLibrary:RequestTrainingModeBattleReset
    //
    // The previous attempt hooked only TrainingModePositionReset and the
    // post-hook never fired - the user's bind takes a different path.
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

    // Last status for the Labbing tab's Copy/Paste pose JSON buttons.
    // ImGui-only state; UI thread reads + writes the same fields, no
    // atomics needed.  Empty string means "no message yet".
    std::string                  m_reset_pose_io_status;
    bool                         m_reset_pose_io_ok = false;

    // Tick counter for throttled settings persistence.  on_update
    // bumps this every frame and calls save_persisted_settings()
    // every kSaveEveryNFrames - batching slider-drag updates into
    // one disk write per ~2 seconds.  See ctor for constant value.
    int                          m_save_tick    = 0;
    static constexpr int         kSaveEveryNFrames = 120;

    Horse::Lux                 m_lux;
    Horse::Deterministic::Sc6ReplayRuntime m_replay_native_runtime{m_lux};
    Horse::Deterministic::DeterministicHookSet m_deterministic_hooks{};

    // Configured backends can target Persistent for active hit/hurt trails.
    // The *_once backends stay Foreground so inactive boxes in broad view
    // draw for the current frame only instead of entering the trail.
    Horse::LineBatcherBackend  m_backend_hit;
    Horse::LineBatcherBackend  m_backend_hurt;
    Horse::LineBatcherBackend  m_backend_hit_once;
    Horse::LineBatcherBackend  m_backend_hurt_once;
    Horse::LineBatcherBackend  m_backend_stage;

    // In-game ImGui overlay token (see on_unreal_init / dtor).  Non-zero
    // after Horse::GameImGui::register_tab returns; passed to
    // unregister_tab on teardown.
    uint64_t m_gameimgui_tab_token = 0;
    uint64_t m_gameimgui_toast_token = 0;
    bool m_gameimgui_init_pending = false;
    bool m_gameimgui_init_attempted = false;
    int m_gameimgui_init_delay_ticks_remaining = 0;
    static constexpr int kGameImGuiDeferredInstallTicks = 180;
    std::atomic<bool> m_gameimgui_toggle_key_down{false};

    RC::Unreal::Hook::GlobalCallbackId m_engine_tick_callback_id{
        RC::Unreal::Hook::ERROR_ID};

    // Nav-bootstrap flag: set to true when the overlay transitions from
    // hidden?shown, consumed by render_hitboxes_tab which then calls
    // ImGui::SetKeyboardFocusHere() on the master F5 toggle.  Forces
    // ImGui to assign a NavId and activate the nav highlight without
    // the user having to press Square/X first.  Without this, ImGui
    // shows the window focused but has no NavId to highlight, so the
    // D-pad appears to do nothing until a "menu" key press kicks nav
    // into gear by side effect.
    bool m_nav_bootstrap_pending = false;
    static constexpr int kHorseModTabCount = 5;
    int m_current_tab = 0;
    std::atomic<int> m_requested_tab{-1};

    // One-shot log flags so UE4SS.log doesn't fill with repeats.
    bool m_logged_native_missing = false;
    // Free-camera diagnostic one-shots: first time we successfully resolve
    // the PlayerCameraManager (so the user can confirm we're targeting
    // a real object), and first time we fall back to the direct +0x420
    // offset read (means reflection couldn't find the property name -
    // unlikely but survivable).
    bool m_logged_pcm_resolve  = false;
    bool m_logged_pcm_fallback = false;
    Horse::Deterministic::Config m_deterministic_config{};
    Horse::Deterministic::FailureCode m_deterministic_failure{
        Horse::Deterministic::FailureCode::None};
    std::unique_ptr<Horse::Deterministic::HgCpuRuntimeDiagnostics>
        m_hgcpu_runtime_diagnostics;
    std::unique_ptr<Horse::Deterministic::StageBreakListenerRuntimeDiagnostics>
        m_stage_break_listener_diagnostics;
    bool m_hgcpu_diagnostic_failure_logged = false;
    bool m_stage_break_listener_failure_logged = false;
    bool m_deterministic_config_present = false;
    Horse::Deterministic::Status m_replay_native_runtime_status{
        Horse::Deterministic::FailureCode::ContextUnavailable};
    Horse::Deterministic::Status m_frame_fencepost_hook_status{
        Horse::Deterministic::FailureCode::ContextUnavailable};
    Horse::Deterministic::UcrtRandBroker m_ucrt_rand_broker{};
    std::atomic<Horse::Deterministic::FailureCode> m_frame_fencepost_failure{
        Horse::Deterministic::FailureCode::None};
    std::atomic<std::uintptr_t> m_frame_fencepost_manager{};
    std::atomic<std::uint32_t> m_frame_fencepost_last_frame{};
    std::atomic<std::uint64_t> m_frame_fencepost_observations{};
    std::atomic<std::uint64_t> m_frame_fencepost_entries{};
    std::atomic<std::uint64_t> m_frame_fencepost_repeats{};
    std::atomic<std::uint64_t> m_frame_fencepost_generations{};
    std::atomic<std::uint16_t> m_frame_fencepost_last_read_mask{};
    std::atomic<Horse::Deterministic::FailureCode> m_replay_exit_failure{
        Horse::Deterministic::FailureCode::None};
    std::atomic<std::uintptr_t> m_replay_exit_state{};
    std::atomic<std::uint64_t> m_replay_exit_observations{};
    std::atomic<std::uint32_t> m_frame_fencepost_expected_thread{};
    bool m_frame_fencepost_first_observation_logged{};
    bool m_frame_fencepost_incomplete_logged{};
    bool m_frame_fencepost_failure_logged{};
    bool m_replay_exit_first_observation_logged{};
    bool m_replay_exit_failure_logged{};
    std::atomic<std::uint64_t> m_candidate_checkpoint_logged_count{};
    std::atomic<std::uint64_t> m_candidate_batch_entry_logged_count{};
    std::atomic<std::uint64_t> m_native_batch_evidence_logged_intervals{};
    std::atomic_bool m_candidate_checkpoint_first_failure_logged{};
    std::atomic_bool m_candidate_batch_entry_first_failure_logged{};
    std::size_t m_owned_correction_probe_index{};
    static constexpr std::uint32_t kForcedQualificationCorrections = 600;
    static constexpr std::uint64_t kForcedQualificationDepth = 7;
    struct ForcedCorrectionQualification
    {
        static constexpr std::uint64_t bucket_width_ns = 50'000;
        static constexpr std::size_t bucket_count = 336;
        std::array<std::uint32_t, bucket_count> buckets{};
        std::uint32_t completed{};
        std::uint64_t generation{};
        std::uint64_t first_frame{};
        std::uint64_t last_frame{};
        std::uint64_t maximum_ns{};
        std::size_t checkpoint_bytes_begin{};
        std::size_t batch_entry_bytes_begin{};
        std::size_t forced_history_bytes_begin{};
        std::uint64_t suppressed_stage_wall_calls{};
        std::uint64_t suppressed_stage_barrier_calls{};
        std::uint64_t semantic_stage_dispatch_calls{};
        std::uint64_t suppressed_audio_calls{};
        std::uint64_t suppressed_audio_stop_all_calls{};
        std::uint64_t suppressed_particle_spawn_calls{};
        std::uint64_t suppressed_particle_finished_binds{};
        std::uint64_t unknown_particle_routes{};
        std::uint64_t verified_audio_batches{};
        std::uint64_t audio_sequence_mismatches{};
        std::uint64_t presentation_failures{};
        Horse::Deterministic::Snapshot expected_scratch{};
        Horse::Deterministic::FailureCode failure{
            Horse::Deterministic::FailureCode::None};
        bool active{};
        bool reported{};

        void Record(std::uint64_t value) noexcept
        {
            const auto bucket = static_cast<std::size_t>((std::min)(
                value / bucket_width_ns,
                static_cast<std::uint64_t>(bucket_count - 1)));
            ++buckets[bucket];
            maximum_ns = (std::max)(maximum_ns, value);
        }
        [[nodiscard]] std::uint64_t P99() const noexcept
        {
            if (completed == 0) return 0;
            const auto target = (completed * 99u + 99u) / 100u;
            std::uint32_t cumulative{};
            for (std::size_t index = 0; index < buckets.size(); ++index)
            {
                cumulative += buckets[index];
                if (cumulative >= target)
                    return (index + 1) * bucket_width_ns;
            }
            return bucket_count * bucket_width_ns;
        }
    } m_forced_correction_qualification{};
    std::atomic<std::uint64_t> m_seek_request_frame{UINT64_MAX};
    std::atomic<std::uint64_t> m_seek_request_sequence{};
    std::uint64_t m_seek_handled_sequence{};
    std::uint64_t m_seek_pending_sequence{};
    std::uint32_t m_seek_defer_count{};
    bool m_seek_request_active{};
    std::atomic_bool m_resume_divergence_logged{};
    std::atomic<std::uint64_t> m_seek_completed_target{};
    std::atomic<std::uint64_t> m_seek_completed_source{};
    std::atomic<std::uint64_t> m_seek_completed_verified{};

    bool service_owned_seek_request() noexcept
    {
        const auto timeline = m_replay_native_runtime.timeline_status();
        if (m_seek_request_active)
        {
            if (timeline.failure != Horse::Deterministic::FailureCode::None)
            {
                m_seek_request_active = false;
                m_seek_completed_target.store(0, std::memory_order_release);
                const auto hash_prefix = [](const Horse::Deterministic::CanonicalHash& hash)
                {
                    std::uint64_t value{};
                    std::memcpy(&value, hash.data(), sizeof(value));
                    return value;
                };
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] owned replay seek resume failed status={} "
                    "coordinate={} component_mask=0x{:x} wind_mask=0x{:x} "
                    "expected_hash_prefix=0x{:016x} "
                    "observed_hash_prefix=0x{:016x} verified_frames={}\n"),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(
                            timeline.failure))),
                    timeline.resume_failure_coordinate.frame,
                    timeline.resume_component_difference_mask,
                    timeline.resume_wind_difference_mask,
                    hash_prefix(timeline.resume_expected_hash),
                    hash_prefix(timeline.resume_observed_hash),
                    timeline.resumed_frames_verified);
                return true;
            }
            if (!timeline.resume_validation_active)
            {
                m_seek_request_active = false;
                m_seek_completed_source.store(
                    timeline.resume_source_end.frame, std::memory_order_relaxed);
                m_seek_completed_verified.store(
                    timeline.resumed_frames_verified, std::memory_order_relaxed);
                m_seek_completed_target.store(
                    timeline.resume_target.frame, std::memory_order_release);
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] owned replay seek resume verified "
                    "target={} source_end={} verified_frames={} final={}\n"),
                    timeline.resume_target.frame,
                    timeline.resume_source_end.frame,
                    timeline.resumed_frames_verified,
                    timeline.last_coordinate.frame);
            }
            return true;
        }

        const auto sequence = m_seek_request_sequence.load(
            std::memory_order_acquire);
        if (sequence == m_seek_handled_sequence) return false;
        if (sequence != m_seek_pending_sequence)
        {
            m_seek_pending_sequence = sequence;
            m_seek_defer_count = 0;
        }
        const auto requested_frame = m_seek_request_frame.load(
            std::memory_order_acquire);
        if (timeline.partial
            || timeline.failure != Horse::Deterministic::FailureCode::None
            || timeline.last_coordinate.generation == 0
            || requested_frame >= timeline.last_coordinate.frame)
        {
            m_seek_handled_sequence = sequence;
            const auto failure = Horse::Deterministic::FailureCode::IllegalTransition;
            m_frame_fencepost_failure.store(failure, std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] owned replay seek request rejected target={} "
                "current={} partial={} failure={}\n"),
                requested_frame, timeline.last_coordinate.frame,
                timeline.partial,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(timeline.failure))));
            return true;
        }

        const Horse::Deterministic::FrameCoordinate target{
            timeline.last_coordinate.generation, requested_frame};
        const auto status = m_replay_native_runtime.ExecuteOwnedStateSeek(
            target, m_deterministic_hooks);
        if (!status.ok())
        {
            if (status.code
                    == Horse::Deterministic::FailureCode::ContextUnavailable
                && m_seek_defer_count < 120)
            {
                ++m_seek_defer_count;
                if (m_seek_defer_count == 1)
                {
                    Output::send<LogLevel::Default>(STR(
                        "[HorseMod] owned replay seek deferred target={} "
                        "source_end={} transient_context=true\n"),
                        target.frame, timeline.last_coordinate.frame);
                }
                return true;
            }
            m_seek_handled_sequence = sequence;
            m_frame_fencepost_failure.store(status.code, std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] owned replay seek request failed target={} "
                "source_end={} status={} component_mask=0x{:x} "
                "native_mask=0x{:x} input_scalar_mask=0x{:x} "
                "input_cache_chunk={} game_time={}/{} update_time={}/{} "
                "recorder_time={}/{} cache_row={} "
                "cache_expected={}/{}/{}/{} cache_observed={}/{}/{}/{} "
                "move_dispatch={:016x}/{:016x}->{:016x}/{:016x} "
                "vfx_edges_p1={}/{}/{}/{}->{}/{}/{}/{} "
                "vfx_edges_p2={}/{}/{}/{}->{}/{}/{}/{} "
                "wind_mask=0x{:x} identity_issue={} identity={}/{}\n"),
                target.frame, timeline.last_coordinate.frame,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                m_replay_native_runtime.timeline_status().
                    resume_component_difference_mask,
                m_replay_native_runtime.timeline_status().
                    resume_native_difference_mask,
                m_replay_native_runtime.timeline_status().
                    resume_input_scalar_difference_mask,
                m_replay_native_runtime.timeline_status().
                    resume_first_input_cache_chunk,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_scalars[5],
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_scalars[5],
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_scalars[7],
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_scalars[7],
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_scalars[8],
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_scalars[8],
                m_replay_native_runtime.timeline_status().
                    resume_first_input_cache_row,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.game_round,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.frame_index,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.input_value,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.filled,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.game_round,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.frame_index,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.input_value,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.filled,
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[0],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[1],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[0],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[1],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[2],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[3],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[4],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[5],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[2],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[3],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[4],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[5],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[6],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[7],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[8],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[9],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[6],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[7],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[8],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[9],
                m_replay_native_runtime.timeline_status().
                    resume_wind_difference_mask,
                m_replay_native_runtime.timeline_status().identity_issue,
                m_replay_native_runtime.timeline_status().identity_expected,
                m_replay_native_runtime.timeline_status().identity_observed);
            return true;
        }
        m_seek_handled_sequence = sequence;
        m_seek_defer_count = 0;
        m_seek_request_active = target != timeline.last_coordinate;
        // The independent hook-health cursor observes the same rewritten
        // native counter. Rebase it atomically so the first resumed frame is
        // still required to be exactly target+1 rather than misreported as a
        // spontaneous generation reset.
        m_frame_fencepost_last_frame.store(
            static_cast<std::uint32_t>(target.frame),
            std::memory_order_release);
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] owned replay seek restored target={} source_end={} "
            "resume_validation={}\n"),
            target.frame, timeline.last_coordinate.frame,
            m_seek_request_active);
        return true;
    }

    void service_owned_correction_probe() noexcept
    {
        static constexpr std::array<std::uint64_t, 3> depths{1, 6, 11};
        static constexpr std::array<std::uint64_t, 3> trigger_frames{
            180, 270, 330};
        if (!m_deterministic_config.trace
            || !m_deterministic_config.correction_probe
            || m_owned_correction_probe_index >= depths.size())
        {
            return;
        }
        const auto timeline = m_replay_native_runtime.timeline_status();
        const auto index = m_owned_correction_probe_index;
        if (timeline.partial || timeline.failure
                != Horse::Deterministic::FailureCode::None
            || timeline.captured_frames < trigger_frames[index]
            || timeline.last_coordinate.frame + 1 < depths[index])
        {
            return;
        }

        Horse::Deterministic::Snapshot expected{};
        auto status = m_replay_native_runtime.CaptureCurrentCanonical(expected);
        Horse::Deterministic::OwnedCorrectionResult result{};
        if (status.ok())
        {
            const Horse::Deterministic::FrameCoordinate earliest{
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame - depths[index] + 1};
            status = m_replay_native_runtime.ExecuteOwnedCorrection(
                earliest, expected.canonical_hash, m_deterministic_hooks, result);
        }
        if (!status.ok())
        {
            m_owned_correction_probe_index = depths.size();
            m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] owned correction probe failed depth={} status={} "
                "primary={} undo={} detail={} index={} base={} final={} "
                "batches={} coordinates={} total_us={} undo_restored={} "
                "restore_samples={}/{}/{}/{} diff_mask=0x{:x} "
                "input_diff={}@{}/{}@{} rng_mask=0x{:x} wind_mask=0x{:x}\n"),
                depths[index],
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.primary_failure))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.undo_failure))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::native_candidate_validation_issue_name(
                        result.primary_validation.issue))),
                result.primary_validation.index,
                result.resimulation_base.frame,
                result.final_coordinate.frame,
                result.replayed_batches,
                result.replayed_coordinates,
                result.total_ns / 1000,
                result.undo_restored,
                result.primary_performance.local_restore.samples,
                result.primary_performance.typed_restore.samples,
                result.primary_performance.wind_restore.samples,
                result.primary_performance.ucrt_restore.samples,
                result.undo_comparison_mask,
                result.input_scalar_difference_count,
                result.first_input_scalar_difference,
                result.input_cache_difference_count,
                result.first_input_cache_difference,
                result.rng_difference_mask,
                result.wind_difference_mask);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] correction interbatch diff batch={} mask=0x{:x} "
                "frame_mask=0x{:x} hgcpu={}@{} motion={}@{} "
                "final_hgcpu={}@{} final_motion={}@{} "
                "source=0x{:x}+{} stream={}/{}\n"),
                result.first_interbatch_difference_batch,
                result.first_interbatch_difference_mask,
                result.first_interbatch_frame_difference_mask,
                result.interbatch_local_difference_count,
                result.first_interbatch_local_difference,
                result.interbatch_motion_difference_count,
                result.first_interbatch_motion_difference,
                result.final_local_difference_count[0],
                result.first_final_local_difference[0],
                result.final_local_difference_count[1],
                result.first_final_local_difference[1],
                result.first_interbatch_local_source.source_address,
                result.first_interbatch_local_difference
                    - result.first_interbatch_local_source.stream_offset,
                result.first_interbatch_local_source.stream_offset,
                result.first_interbatch_local_source.size);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] correction local source fighters=0x{:x}/0x{:x} "
                "offsets={}/{} source_rva=0x{:x}\n"),
                result.diagnostic_fighter_roots[0],
                result.diagnostic_fighter_roots[1],
                static_cast<std::int64_t>(
                    result.first_interbatch_local_source.source_address
                    + (result.first_interbatch_local_difference
                        - result.first_interbatch_local_source.stream_offset)
                    - result.diagnostic_fighter_roots[0]),
                static_cast<std::int64_t>(
                    result.first_interbatch_local_source.source_address
                    + (result.first_interbatch_local_difference
                        - result.first_interbatch_local_source.stream_offset)
                    - result.diagnostic_fighter_roots[1]),
                result.first_interbatch_local_source.source_address
                    + (result.first_interbatch_local_difference
                        - result.first_interbatch_local_source.stream_offset)
                    - result.diagnostic_image_base);
            if (result.first_input_cache_difference != UINT32_MAX)
            {
                const auto& expected_row = result.expected_input_cache_row;
                const auto& observed_row = result.observed_input_cache_row;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction input/RNG diff row={} "
                    "expected={}/{}/0x{:x}/{} observed={}/{}/0x{:x}/{} "
                    "lcg=0x{:x}/0x{:x} lfsr_index={}/{} wind0=0x{:x}/0x{:x}\n"),
                    result.first_input_cache_difference,
                    expected_row.game_round, expected_row.frame_index,
                    expected_row.input_value, expected_row.filled,
                    observed_row.game_round, observed_row.frame_index,
                    observed_row.input_value, observed_row.filled,
                    result.expected_rng.lcg, result.observed_rng.lcg,
                    result.expected_rng.lfsr_index,
                    result.observed_rng.lfsr_index,
                    result.expected_rng.wind[0], result.observed_rng.wind[0]);
            }
            if (result.rng_difference_mask != 0
                || result.wind_difference_mask != 0)
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction RNG state "
                    "lcg=0x{:x}/0x{:x} lfsr_index={}/{} "
                    "xorshift=0x{:x},0x{:x},0x{:x}/0x{:x},0x{:x},0x{:x} "
                    "wind=0x{:x},0x{:x},0x{:x},0x{:x},0x{:x},0x{:x}/"
                    "0x{:x},0x{:x},0x{:x},0x{:x},0x{:x},0x{:x}\n"),
                    result.expected_rng.lcg, result.observed_rng.lcg,
                    result.expected_rng.lfsr_index,
                    result.observed_rng.lfsr_index,
                    result.expected_rng.xorshift[0],
                    result.expected_rng.xorshift[1],
                    result.expected_rng.xorshift[2],
                    result.observed_rng.xorshift[0],
                    result.observed_rng.xorshift[1],
                    result.observed_rng.xorshift[2],
                    result.expected_rng.wind[0], result.expected_rng.wind[1],
                    result.expected_rng.wind[2], result.expected_rng.wind[3],
                    result.expected_rng.wind[4], result.expected_rng.wind[5],
                    result.observed_rng.wind[0], result.observed_rng.wind[1],
                    result.observed_rng.wind[2], result.observed_rng.wind[3],
                    result.observed_rng.wind[4], result.observed_rng.wind[5]);
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction wind detail lfsr_word={} "
                    "nodes={}/{} first_node={} kind={}/{} semantic={} "
                    "byte=0x{:x}/0x{:x} derived={} output={}\n"),
                    result.first_lfsr_difference,
                    result.expected_wind_node_count,
                    result.observed_wind_node_count,
                    result.first_wind_node_difference,
                    result.expected_wind_node_kind,
                    result.observed_wind_node_kind,
                    result.first_wind_semantic_difference,
                    result.expected_wind_difference_byte,
                    result.observed_wind_difference_byte,
                    result.first_wind_derived_difference,
                    result.first_wind_output_difference);
                const auto& ie = result.first_interbatch_expected_wind;
                const auto& io = result.first_interbatch_observed_wind;
                const auto& fe = result.final_expected_wind;
                const auto& fo = result.final_observed_wind;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction wind schedule interbatch "
                    "kind={}/{} present={}/{} life=0x{:x}/0x{:x} "
                    "tick={}/{} prepared={}/{} active={}/{} "
                    "step=0x{:x}/0x{:x} repeat={}/{} "
                    "lfsr_index={}/{} final_life=0x{:x}/0x{:x} "
                    "final_tick={}/{} final_active={}/{}\n"),
                    ie.kind, io.kind, ie.present, io.present,
                    ie.life_bits, io.life_bits,
                    ie.oscillator_tick, io.oscillator_tick,
                    ie.prepared, io.prepared, ie.active, io.active,
                    ie.frame_step_bits, io.frame_step_bits,
                    ie.repeat_count, io.repeat_count,
                    result.first_interbatch_expected_rng.lfsr_index,
                    result.first_interbatch_observed_rng.lfsr_index,
                    fe.life_bits, fo.life_bits,
                    fe.oscillator_tick, fo.oscillator_tick,
                    fe.active, fo.active);
                const auto& bg = result.base_wind_graph;
                const auto& eg = result.first_interbatch_expected_wind_graph;
                const auto& og = result.first_interbatch_observed_wind_graph;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction wind graph "
                    "root(base={}/{}/{} expected={}/{}/{} observed={}/{}/{}) "
                    "nodes={}/{}/{} callback_hash=0x{:x}/0x{:x}/0x{:x} "
                    "life0-3=0x{:x},0x{:x},0x{:x},0x{:x}/"
                    "0x{:x},0x{:x},0x{:x},0x{:x}/"
                    "0x{:x},0x{:x},0x{:x},0x{:x} "
                    "tick0-3={},{},{},{}/{},{},{},{}/{},{},{},{}\n"),
                    bg.active_bank, bg.pending_count, bg.callback_count,
                    eg.active_bank, eg.pending_count, eg.callback_count,
                    og.active_bank, og.pending_count, og.callback_count,
                    bg.node_count, eg.node_count, og.node_count,
                    bg.callback_hash, eg.callback_hash, og.callback_hash,
                    bg.nodes[0].life_bits, bg.nodes[1].life_bits,
                    bg.nodes[2].life_bits, bg.nodes[3].life_bits,
                    eg.nodes[0].life_bits, eg.nodes[1].life_bits,
                    eg.nodes[2].life_bits, eg.nodes[3].life_bits,
                    og.nodes[0].life_bits, og.nodes[1].life_bits,
                    og.nodes[2].life_bits, og.nodes[3].life_bits,
                    bg.nodes[0].oscillator_tick, bg.nodes[1].oscillator_tick,
                    bg.nodes[2].oscillator_tick, bg.nodes[3].oscillator_tick,
                    eg.nodes[0].oscillator_tick, eg.nodes[1].oscillator_tick,
                    eg.nodes[2].oscillator_tick, eg.nodes[3].oscillator_tick,
                    og.nodes[0].oscillator_tick, og.nodes[1].oscillator_tick,
                    og.nodes[2].oscillator_tick, og.nodes[3].oscillator_tick);
            }
            if (result.failed_batch_index != SIZE_MAX)
            {
                const auto& envelope = result.failed_envelope;
                const auto& before = result.failed_batch_result.before;
                const auto& after = result.failed_batch_result.after;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction batch mismatch batch={} failure={} "
                    "observed_coordinates={} before(frame={}/{} input_round={}/{} "
                    "input_time={}/{} cursor_round={}/{} cursor_time={}/{} "
                    "main={}/{} round={}/{}) after(frame={}/{} input_round={}/{} "
                    "input_time={}/{} cursor_round={}/{} cursor_time={}/{} "
                    "main={}/{} round={}/{})\n"),
                    result.failed_batch_index,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(
                            result.failed_batch_result.failure))),
                    result.failed_batch_result.observed_coordinates,
                    before.frame_counter, envelope.native_frame_before,
                    before.input_game_round, envelope.input_round_before,
                    before.input_game_time, envelope.input_time_before,
                    before.manager_game_round_cursor,
                    envelope.manager_round_cursor_before,
                    before.manager_game_time_cursor,
                    envelope.manager_time_cursor_before, before.main_state,
                    envelope.main_state_before, before.round_state,
                    envelope.round_state_before,
                    after.frame_counter, envelope.native_frame_after,
                    after.input_game_round, envelope.input_round_after,
                    after.input_game_time, envelope.input_time_after,
                    after.manager_game_round_cursor,
                    envelope.manager_round_cursor_after,
                    after.manager_game_time_cursor,
                    envelope.manager_time_cursor_after, after.main_state,
                    envelope.main_state_after, after.round_state,
                    envelope.round_state_after);
            }
            return;
        }
        ++m_owned_correction_probe_index;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] owned correction probe passed depth={} base={} "
            "final={} batches={} coordinates={} undo_capture_us={} "
            "restore_us={} resim_us={} verify_us={} total_us={} "
            "restore_phase_us(local={}/{} typed={}/{} wind={}/{} "
            "ucrt={}/{} total={}/{})\n"),
            depths[index], result.resimulation_base.frame,
            result.final_coordinate.frame, result.replayed_batches,
            result.replayed_coordinates, result.undo_capture_ns / 1000,
            result.restore_ns / 1000, result.resimulation_ns / 1000,
            result.verification_ns / 1000, result.total_ns / 1000,
            result.primary_performance.local_restore.p99_ns / 1000,
            result.primary_performance.local_restore.maximum_ns / 1000,
            result.primary_performance.typed_restore.p99_ns / 1000,
            result.primary_performance.typed_restore.maximum_ns / 1000,
            result.primary_performance.wind_restore.p99_ns / 1000,
            result.primary_performance.wind_restore.maximum_ns / 1000,
            result.primary_performance.ucrt_restore.p99_ns / 1000,
            result.primary_performance.ucrt_restore.maximum_ns / 1000,
            result.primary_performance.total_restore.p99_ns / 1000,
            result.primary_performance.total_restore.maximum_ns / 1000);
    }

    void service_forced_depth7_qualification() noexcept
    {
        auto& qualification = m_forced_correction_qualification;
        if (!m_deterministic_config.trace
            || !m_deterministic_config.forced_depth7_qualification
            || qualification.reported)
        {
            return;
        }
        const auto timeline = m_replay_native_runtime.timeline_status();
        if (timeline.partial
            || timeline.failure != Horse::Deterministic::FailureCode::None
            || timeline.round_state_frame <= 16
            || timeline.unpause_countdown != 0
            || timeline.last_coordinate.generation == 0
            || timeline.last_coordinate.frame < kForcedQualificationDepth)
        {
            return;
        }
        if (!qualification.active)
        {
            qualification.active = true;
            qualification.generation = timeline.last_coordinate.generation;
            qualification.first_frame = timeline.last_coordinate.frame;
            qualification.checkpoint_bytes_begin = timeline.checkpoint_bytes;
            qualification.batch_entry_bytes_begin =
                timeline.batch_entry_checkpoint_bytes;
            qualification.forced_history_bytes_begin =
                m_replay_native_runtime.forced_qualification_bytes();
            m_replay_native_runtime.ResetCapturePerformanceWindow();
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] forced depth-7 qualification started "
                "generation={} frame={} target={} normal_render=true\n"),
                qualification.generation, qualification.first_frame,
                kForcedQualificationCorrections);
        }
        if (timeline.last_coordinate.generation != qualification.generation)
        {
            qualification.failure =
                Horse::Deterministic::FailureCode::GenerationMismatch;
        }
        Horse::Deterministic::OwnedCorrectionResult result{};
        auto status = qualification.failure
                == Horse::Deterministic::FailureCode::None
            ? m_replay_native_runtime.CaptureCurrentCanonical(
                qualification.expected_scratch)
            : Horse::Deterministic::Status::failure(qualification.failure);
        if (status.ok())
        {
            const Horse::Deterministic::FrameCoordinate earliest{
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame
                    - kForcedQualificationDepth + 1};
            status = m_replay_native_runtime.ExecuteOwnedCorrection(
                earliest, qualification.expected_scratch.canonical_hash,
                m_deterministic_hooks, result);
        }
        const bool exact_depth = status.ok()
            && result.replayed_coordinates == kForcedQualificationDepth
            && result.final_coordinate.frame
                == result.resimulation_base.frame + kForcedQualificationDepth;
        if (!exact_depth && status.ok())
            status = Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::IllegalTransition);
        if (!status.ok())
        {
            qualification.failure = status.code;
            qualification.reported = true;
            m_frame_fencepost_failure.store(status.code,
                std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] forced depth-7 qualification failed "
                "completed={} frame={} status={} primary={} undo={} "
                "coordinates={} base={} final={} total_us={} "
                "diff_mask=0x{:x} local_diff={} local_count={} "
                "motion_diff={} motion_count={} input_scalars={}@{}={}->{} "
                "input_cache={} rng_mask=0x{:x} wind_mask=0x{:x} "
                "move_dispatch={:016x}/{:016x}->{:016x}/{:016x} "
                "wind_node={} kind={}->{} semantic={} byte={}->{} "
                "interbatch_mask=0x{:x} interbatch_batch={} "
                "audio_expected={} audio_observed={} "
                "audio_route=0x{:08x}->0x{:08x} "
                "audio_payload=0x{:08x}->0x{:08x} "
                "audio_position=0x{:08x}->0x{:08x} "
                "audio_remap={}/{} 0x{:016x}->0x{:016x} "
                "audio_source={}/{} 0x{:016x}->0x{:016x} "
                "audio_direct={}/{} "
                "remap_entry=0x{:02x}:{}/{}->0x{:02x}:{}/{} "
                "selector_base={}/{}/{}/{} selector_undo={}/{}/{}/{} "
                "audio_sequence_mismatches={} presentation_failures={}\n"),
                qualification.completed, timeline.last_coordinate.frame,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.primary_failure))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.undo_failure))),
                result.replayed_coordinates, result.resimulation_base.frame,
                result.final_coordinate.frame, result.total_ns / 1000,
                result.undo_comparison_mask,
                result.first_final_local_difference[0],
                result.final_local_difference_count[0],
                result.first_final_local_difference[1],
                result.final_local_difference_count[1],
                result.input_scalar_difference_count,
                result.first_input_scalar_difference,
                result.expected_input_scalar_word,
                result.observed_input_scalar_word,
                result.input_cache_difference_count,
                result.rng_difference_mask, result.wind_difference_mask,
                result.expected_move_dispatch[0],
                result.expected_move_dispatch[1],
                result.observed_move_dispatch[0],
                result.observed_move_dispatch[1],
                result.first_wind_node_difference,
                result.expected_wind_node_kind,
                result.observed_wind_node_kind,
                result.first_wind_semantic_difference,
                result.expected_wind_difference_byte,
                result.observed_wind_difference_byte,
                result.first_interbatch_difference_mask,
                result.first_interbatch_difference_batch,
                result.failed_envelope.battle_audio_dispatches,
                result.failed_batch_result.suppressed_audio_calls,
                result.failed_envelope.battle_audio_route_hash,
                result.failed_batch_result.suppressed_audio_route_hash,
                result.failed_envelope.battle_audio_payload_hash,
                result.failed_batch_result.suppressed_audio_payload_hash,
                result.failed_envelope.battle_audio_position_hash,
                result.failed_batch_result.suppressed_audio_position_hash,
                result.failed_envelope.battle_audio_remap_calls,
                result.failed_batch_result.suppressed_audio_remap_calls,
                result.failed_envelope.battle_audio_remap_hash,
                result.failed_batch_result.suppressed_audio_remap_hash,
                result.failed_envelope.battle_audio_source_calls,
                result.failed_batch_result.suppressed_audio_source_calls,
                result.failed_envelope.battle_audio_source_hash,
                result.failed_batch_result.suppressed_audio_source_hash,
                result.failed_envelope.battle_audio_direct_dispatches,
                result.failed_batch_result.suppressed_audio_calls,
                result.failed_envelope.battle_audio_remap_entry_mask,
                result.failed_envelope.battle_audio_remap_entry_values[0],
                result.failed_envelope.battle_audio_remap_entry_values[1],
                result.failed_batch_result.suppressed_audio_remap_entry_mask,
                result.failed_batch_result.suppressed_audio_remap_entry_values[0],
                result.failed_batch_result.suppressed_audio_remap_entry_values[1],
                result.base_audio_selector.observed_count,
                result.base_audio_selector.alternations[0],
                result.base_audio_selector.alternations[1],
                result.base_audio_selector.round_generation,
                result.undo_audio_selector.observed_count,
                result.undo_audio_selector.alternations[0],
                result.undo_audio_selector.alternations[1],
                result.undo_audio_selector.round_generation,
                result.failed_batch_result.audio_sequence_mismatches,
                result.failed_batch_result.presentation_failures);
            return;
        }
        qualification.Record(result.total_ns);
        qualification.suppressed_stage_wall_calls +=
            result.suppressed_stage_wall_calls;
        qualification.suppressed_stage_barrier_calls +=
            result.suppressed_stage_barrier_calls;
        qualification.semantic_stage_dispatch_calls +=
            result.semantic_stage_dispatch_calls;
        qualification.suppressed_audio_calls += result.suppressed_audio_calls;
        qualification.suppressed_audio_stop_all_calls +=
            result.suppressed_audio_stop_all_calls;
        qualification.suppressed_particle_spawn_calls +=
            result.suppressed_particle_spawn_calls;
        qualification.suppressed_particle_finished_binds +=
            result.suppressed_particle_finished_binds;
        qualification.unknown_particle_routes +=
            result.unknown_particle_routes;
        qualification.verified_audio_batches += result.verified_audio_batches;
        qualification.audio_sequence_mismatches +=
            result.audio_sequence_mismatches;
        qualification.presentation_failures += result.presentation_failures;
        ++qualification.completed;
        qualification.last_frame = timeline.last_coordinate.frame;
        if (qualification.completed < kForcedQualificationCorrections) return;

        const auto final_timeline = m_replay_native_runtime.timeline_status();
        const auto capture_performance =
            m_replay_native_runtime.capture_performance();
        const auto p99 = qualification.P99();
        const bool performance_ok = p99 < 16'670'000;
        const bool capture_ok = capture_performance.total_capture.p99_ns
                <= 500'000
            && capture_performance.total_capture.maximum_ns <= 1'000'000;
        qualification.reported = true;
        if (!performance_ok || !capture_ok)
        {
            qualification.failure =
                Horse::Deterministic::FailureCode::PerformanceBudgetExceeded;
            m_frame_fencepost_failure.store(qualification.failure,
                std::memory_order_release);
        }
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] forced depth-7 qualification {} completed={} "
            "generation={} frames={}-{} cycle_p99_us={} cycle_max_us={} "
            "capture_samples={} capture_p99_us={} capture_max_us={} "
            "checkpoint_bytes={}->{} batch_entry_bytes={}->{} "
            "forced_history_bytes={}->{} "
            "stage_wall_suppressed={} stage_barrier_suppressed={} "
            "stage_semantic_dispatches={} audio_suppressed={} "
            "audio_stop_all_suppressed={} "
            "particle_spawn_suppressed={} particle_bind_suppressed={} "
            "particle_unknown_routes={} "
            "audio_batches_verified={} audio_sequence_mismatches={} "
            "presentation_failures={} canonical_convergence=exact "
            "presentation_terminal_coverage=incomplete\n"),
            qualification.failure == Horse::Deterministic::FailureCode::None
                ? STR("passed") : STR("failed"),
            qualification.completed, qualification.generation,
            qualification.first_frame, qualification.last_frame,
            p99 / 1000, qualification.maximum_ns / 1000,
            capture_performance.total_capture.samples,
            capture_performance.total_capture.p99_ns / 1000,
            capture_performance.total_capture.maximum_ns / 1000,
            qualification.checkpoint_bytes_begin,
            final_timeline.checkpoint_bytes,
            qualification.batch_entry_bytes_begin,
            final_timeline.batch_entry_checkpoint_bytes,
            qualification.forced_history_bytes_begin,
            m_replay_native_runtime.forced_qualification_bytes(),
            qualification.suppressed_stage_wall_calls,
            qualification.suppressed_stage_barrier_calls,
            qualification.semantic_stage_dispatch_calls,
            qualification.suppressed_audio_calls,
            qualification.suppressed_audio_stop_all_calls,
            qualification.suppressed_particle_spawn_calls,
            qualification.suppressed_particle_finished_binds,
            qualification.unknown_particle_routes,
            qualification.verified_audio_batches,
            qualification.audio_sequence_mismatches,
            qualification.presentation_failures);
    }

    void observe_hgcpu_diagnostic(std::uint32_t frame) noexcept
    {
        if (!m_hgcpu_runtime_diagnostics
            || m_hgcpu_runtime_diagnostics->complete())
        {
            return;
        }
        const auto status = m_hgcpu_runtime_diagnostics->Observe(
            Horse::NativeBinding::imageBase(), frame);
        if (!status.ok()
            && status.code != Horse::Deterministic::FailureCode::ContextUnavailable
            && !m_hgcpu_diagnostic_failure_logged)
        {
            m_hgcpu_diagnostic_failure_logged = true;
            const auto failure = Horse::Deterministic::failure_code_name(status.code);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] HgCpu runtime coverage diagnostic failed: {}\n"),
                RC::to_generic_string(std::string(failure)));
        }
    }

    static bool append_stage_break_actor_list(
        const Horse::TArrHdr* list,
        Horse::Deterministic::StageBreakActorKind kind,
        std::array<Horse::Deterministic::StageBreakActorRef, 64>& output,
        std::size_t& count) noexcept
    {
        __try
        {
            if (list == nullptr || list->Num == 0) return true;
            if (list->Data == nullptr || list->Num < 0 || list->Max < list->Num
                || list->Num > 64
                || count + static_cast<std::size_t>(list->Num) > output.size())
            {
                return false;
            }
            auto* const* entries = static_cast<RC::Unreal::UObject* const*>(
                list->Data);
            for (std::int32_t index = 0; index < list->Num; ++index)
            {
                output[count++] = {
                    kind, reinterpret_cast<std::uintptr_t>(entries[index])};
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void observe_stage_break_listener_diagnostic(std::uint32_t frame) noexcept
    {
        if (!m_stage_break_listener_diagnostics
            || m_stage_break_listener_diagnostics->complete())
        {
            return;
        }
        Horse::Deterministic::Status status = Horse::Deterministic::Status::failure(
            Horse::Deterministic::FailureCode::ContextUnavailable);
        Horse::Obj battle_manager = m_lux.battleManager();
        Horse::Obj stage_manager = battle_manager
            ? battle_manager.getObj(L"BattleStageActorManager") : Horse::Obj{};
        if (!stage_manager) return;
        std::array<Horse::Deterministic::StageBreakActorRef, 64> actors{};
        std::size_t actor_count{};
        const bool valid_lists = append_stage_break_actor_list(
            stage_manager.getPtr<Horse::TArrHdr>(L"BreakableWallActorList"),
            Horse::Deterministic::StageBreakActorKind::Wall, actors, actor_count)
            && append_stage_break_actor_list(
                stage_manager.getPtr<Horse::TArrHdr>(L"BarrierActorList"),
                Horse::Deterministic::StageBreakActorKind::Barrier,
                actors, actor_count);
        if (!valid_lists)
        {
            status = Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::InvalidConfiguration);
        }
        else if (actor_count != 0)
        {
            status = m_stage_break_listener_diagnostics->Observe(
                Horse::NativeBinding::imageBase(), frame,
                std::span{actors.data(), actor_count});
        }
        if (!status.ok()
            && status.code != Horse::Deterministic::FailureCode::ContextUnavailable
            && !m_stage_break_listener_failure_logged)
        {
            m_stage_break_listener_failure_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] stage-break listener diagnostic failed: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))));
        }
    }

    static void on_frame_fencepost(
        void* user,
        const Horse::Deterministic::FrameFencepostObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
        {
            return;
        }
        self->m_frame_fencepost_entries.fetch_add(1, std::memory_order_acq_rel);
        self->m_frame_fencepost_last_read_mask.store(
            observation.read_mask, std::memory_order_release);
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }
        if (observation.read_mask
            != Horse::Deterministic::Schema::Sc6FrameLayout::
                required_observation_read_mask)
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::ContextUnavailable,
                std::memory_order_release);
            return;
        }

        const auto capture = self->m_replay_native_runtime.ObserveFrame(observation);
        if (!capture.ok())
        {
            self->m_frame_fencepost_failure.store(
                capture.code, std::memory_order_release);
            const auto failed = self->m_replay_native_runtime.timeline_status();
            if (capture.code
                    == Horse::Deterministic::FailureCode::StateHashMismatch
                && failed.resume_failure_coordinate.generation != 0)
            {
                if (self->m_resume_divergence_logged.exchange(
                        true, std::memory_order_acq_rel))
                {
                    return;
                }
                std::uint64_t expected_prefix{};
                std::uint64_t observed_prefix{};
                std::memcpy(&expected_prefix,
                    failed.resume_expected_hash.data(), sizeof(expected_prefix));
                std::memcpy(&observed_prefix,
                    failed.resume_observed_hash.data(), sizeof(observed_prefix));
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] resumed canonical frame diverged "
                    "coordinate={} verified_before={} component_mask=0x{:x} "
                    "wind_mask=0x{:x} wind_semantic_chunk={} "
                    "wind_node_expected={}/{:08x}/{}/{}/{}/{:08x}/{} "
                    "wind_node_observed={}/{:08x}/{}/{}/{}/{:08x}/{} "
                    "expected_hash_prefix=0x{:016x} "
                    "observed_hash_prefix=0x{:016x}\n"),
                    failed.resume_failure_coordinate.frame,
                    failed.resumed_frames_verified,
                    failed.resume_component_difference_mask,
                    failed.resume_wind_difference_mask,
                    failed.resume_first_wind_semantic_chunk,
                    failed.resume_expected_wind_node.kind,
                    failed.resume_expected_wind_node.life_bits,
                    failed.resume_expected_wind_node.oscillator_tick,
                    failed.resume_expected_wind_node.prepared,
                    failed.resume_expected_wind_node.active,
                    failed.resume_expected_wind_node.frame_step_bits,
                    failed.resume_expected_wind_node.repeat_count,
                    failed.resume_observed_wind_node.kind,
                    failed.resume_observed_wind_node.life_bits,
                    failed.resume_observed_wind_node.oscillator_tick,
                    failed.resume_observed_wind_node.prepared,
                    failed.resume_observed_wind_node.active,
                    failed.resume_observed_wind_node.frame_step_bits,
                    failed.resume_observed_wind_node.repeat_count,
                    expected_prefix, observed_prefix);
            }
            else if (capture.code
                    == Horse::Deterministic::FailureCode::CaptureFailed
                && !self->m_resume_divergence_logged.exchange(
                    true, std::memory_order_acq_rel))
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] canonical frame capture failed coordinate={} "
                    "phase={} animation={} observed=0x{:x}\n"),
                    failed.canonical_capture_failure_coordinate.frame,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::candidate_capture_phase_name(
                            failed.canonical_capture_phase))),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::chara_animation_topology_issue_name(
                            failed.canonical_animation_topology_issue))),
                    failed.canonical_animation_topology_observed);
            }
            else if (!self->m_resume_divergence_logged.exchange(
                         true, std::memory_order_acq_rel))
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] canonical frame capture rejected status={} "
                    "coordinate={} phase={} animation={} observed=0x{:x} "
                    "identity_issue={} expected=0x{:x} actual=0x{:x}\n"),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(capture.code))),
                    failed.canonical_capture_failure_coordinate.frame,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::candidate_capture_phase_name(
                            failed.canonical_capture_phase))),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::chara_animation_topology_issue_name(
                            failed.canonical_animation_topology_issue))),
                    failed.canonical_animation_topology_observed,
                    failed.identity_issue, failed.identity_expected,
                    failed.identity_observed);
            }
            return;
        }
        const auto timeline = self->m_replay_native_runtime.timeline_status();
        if (!timeline.resume_validation_active)
        {
            self->observe_hgcpu_diagnostic(observation.frame_counter);
            self->observe_stage_break_listener_diagnostic(
                observation.frame_counter);
        }
        const auto logged_checkpoints =
            self->m_candidate_checkpoint_logged_count.load(std::memory_order_acquire);
        if (self->m_deterministic_config.trace
            && timeline.captured_checkpoints > logged_checkpoints)
        {
            self->m_candidate_checkpoint_logged_count.store(
                timeline.captured_checkpoints, std::memory_order_release);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] candidate checkpoint captured count={} generation={} "
                "frame={} bytes={} wind_nodes={} capture_p99_us={} "
                "capture_max_us={} store_p99_us={} typed_p99_us={} "
                "local_p99_us={} hgcpu_p99_us={} motion_p99_us={} "
                "wind_p99_us={} encode_p99_us={}\n"),
                timeline.captured_checkpoints,
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame,
                timeline.checkpoint_bytes,
                timeline.checkpoint_wind_nodes,
                timeline.checkpoint_capture_p99_ns / 1000,
                timeline.checkpoint_capture_max_ns / 1000,
                timeline.checkpoint_store_p99_ns / 1000,
                timeline.checkpoint_adapter_performance.typed_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.local_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.hgcpu_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.motion_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.wind_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.encode.p99_ns / 1000);
        }
        if (timeline.checkpoint_failure != Horse::Deterministic::FailureCode::None
            && !self->m_candidate_checkpoint_first_failure_logged.exchange(
                true, std::memory_order_acq_rel))
        {
            const auto failure = Horse::Deterministic::failure_code_name(
                timeline.checkpoint_failure);
            const auto& diagnostic = timeline.checkpoint_validation;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] candidate checkpoint capture unavailable: {} "
                "phase={} detail={} animation={} animation_ptr=0x{:x} fighters=0x{:x}/0x{:x} "
                "index={} observed={}/{} expected={}/{}\n"),
                RC::to_generic_string(std::string(failure)),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::candidate_capture_phase_name(
                        timeline.checkpoint_capture_phase))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::native_candidate_validation_issue_name(
                        diagnostic.issue))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::chara_animation_topology_issue_name(
                        timeline.checkpoint_animation_topology_issue))),
                timeline.checkpoint_animation_observed,
                timeline.checkpoint_animation_fighters[0],
                timeline.checkpoint_animation_fighters[1],
                diagnostic.index, diagnostic.observed_a, diagnostic.observed_b,
                diagnostic.expected_a, diagnostic.expected_b);
        }

        const std::uintptr_t prior_manager = self->m_frame_fencepost_manager.exchange(
            observation.battle_manager, std::memory_order_acq_rel);
        const std::uint64_t prior_count = self->m_frame_fencepost_observations.fetch_add(
            1, std::memory_order_acq_rel);
        const std::uint32_t prior_frame = self->m_frame_fencepost_last_frame.exchange(
            observation.frame_counter, std::memory_order_acq_rel);
        if (prior_manager != observation.battle_manager)
        {
            self->m_frame_fencepost_generations.fetch_add(
                1, std::memory_order_relaxed);
        }
        else if (prior_count != 0
                 && observation.frame_counter != prior_frame + 1)
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::AdvanceFailed,
                std::memory_order_release);
        }
        if (observation.repeat_pending != 0)
        {
            self->m_frame_fencepost_repeats.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    static void on_outer_tick(
        void* user,
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
            return;
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }
        const auto status = self->m_replay_native_runtime.ObserveOuterTick(
            observation);
        if (!status.ok())
        {
            self->m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
            return;
        }

        const auto timeline = self->m_replay_native_runtime.timeline_status();
        const std::uint64_t completed_intervals = timeline.captured_frames / 600;
        std::uint64_t logged_intervals =
            self->m_native_batch_evidence_logged_intervals.load(
                std::memory_order_acquire);
        if (self->m_deterministic_config.trace && completed_intervals != 0
            && completed_intervals > logged_intervals
            && self->m_native_batch_evidence_logged_intervals.compare_exchange_strong(
                logged_intervals, completed_intervals,
                std::memory_order_acq_rel))
        {
            Horse::Deterministic::UcrtRandBrokerImage ucrt_image{};
            const auto ucrt_status = self->m_ucrt_rand_broker.Capture(
                observation.thread_id, ucrt_image);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] native fencepost evidence frames={} repeats={} "
                "same_time={} cursor_mismatches={} round_state_frame={} "
                "input_filter_observations={} input_filter_mutations={} "
                "input_filter_invocations_max={} identity_rebaselines={} unpause={} "
                "pending_move_state={} batches={} zero_batches={} "
                "multi_batches={} batch_repeats={} batch_same_time={} max_batch={} "
                "max_input_delta={} input_generation_changes={} "
                "batch_accounting_mismatches={} entry_uncovered={} "
                "entry_gap_max={} resim_distance_max={} fp_samples={} "
                "fp_control_mismatches={} fp_status_mismatches={} "
                "fp_x87_status_mismatches={} fp_mxcsr_status_mismatches={} "
                "fp_before=0x{:04x}/0x{:04x}/0x{:08x} "
                "fp_after=0x{:04x}/0x{:04x}/0x{:08x} "
                "ucrt_mode={} ucrt_status={} ucrt_owner_thread={} "
                "ucrt_seeded={} ucrt_draws={} "
                "ucrt_state=0x{:08x}\n"),
                timeline.captured_frames,
                timeline.repeat_requests,
                timeline.same_native_time_coordinates,
                timeline.cursor_mismatches,
                timeline.round_state_frame,
                timeline.input_filter_observations,
                timeline.input_filter_mutations,
                timeline.maximum_input_filter_invocation_ordinal,
                timeline.identity_rebaselines,
                timeline.unpause_countdown,
                static_cast<unsigned int>(timeline.pending_move_state),
                timeline.native_batches,
                timeline.zero_coordinate_batches,
                timeline.multi_coordinate_batches,
                timeline.batch_repeat_coordinates,
                timeline.batch_same_input_time_coordinates,
                timeline.maximum_coordinates_per_batch,
                timeline.maximum_input_delta_per_batch,
                timeline.batch_input_generation_changes,
                timeline.batch_frame_accounting_mismatches,
                timeline.coordinates_without_batch_entry_checkpoint,
                timeline.maximum_batch_entry_checkpoint_gap,
                timeline.maximum_resim_distance_from_batch_entry,
                timeline.fp_samples,
                timeline.fp_control_mismatches,
                timeline.fp_status_mismatches,
                timeline.fp_x87_status_mismatches,
                timeline.fp_mxcsr_status_mismatches,
                timeline.fp_last_before.x87_control,
                timeline.fp_last_before.x87_status,
                timeline.fp_last_before.mxcsr,
                timeline.fp_last_after.x87_control,
                timeline.fp_last_after.x87_status,
                timeline.fp_last_after.mxcsr,
                static_cast<unsigned int>(self->m_ucrt_rand_broker.mode()),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(ucrt_status.code))),
                self->m_ucrt_rand_broker.owner_thread_id(),
                ucrt_image.seeded,
                ucrt_image.draws,
                ucrt_image.state);
        }
        if (!self->service_owned_seek_request())
        {
            self->service_forced_depth7_qualification();
            if (!self->m_deterministic_config.forced_depth7_qualification)
                self->service_owned_correction_probe();
        }
    }

    static void on_outer_tick_begin(
        void* user,
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
            return;
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }
        const auto status = self->m_replay_native_runtime.ObserveOuterTickBegin(
            observation);
        if (!status.ok())
        {
            self->m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
            return;
        }
        const auto timeline = self->m_replay_native_runtime.timeline_status();
        const auto logged = self->m_candidate_batch_entry_logged_count.load(
            std::memory_order_acquire);
        if (self->m_deterministic_config.trace
            && timeline.captured_batch_entry_checkpoints > logged)
        {
            self->m_candidate_batch_entry_logged_count.store(
                timeline.captured_batch_entry_checkpoints,
                std::memory_order_release);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] candidate batch-entry checkpoint captured count={} "
                "generation={} frame={} bytes={} wind_nodes={} "
                        "capture_p99_us={} capture_max_us={} store_p99_us={} "
                        "typed_p99_us={} local_p99_us={} hgcpu_p99_us={} "
                        "motion_p99_us={} wind_p99_us={} "
                "encode_p99_us={}\n"),
                timeline.captured_batch_entry_checkpoints,
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame,
                timeline.batch_entry_checkpoint_bytes,
                timeline.batch_entry_wind_nodes,
                timeline.batch_entry_capture_p99_ns / 1000,
                timeline.batch_entry_capture_max_ns / 1000,
                timeline.batch_entry_store_p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.typed_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.local_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.hgcpu_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.motion_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.wind_capture.p99_ns / 1000,
                timeline.batch_entry_adapter_performance.encode.p99_ns / 1000);
        }
        if (timeline.batch_entry_checkpoint_failure
                != Horse::Deterministic::FailureCode::None
            && !self->m_candidate_batch_entry_first_failure_logged.exchange(
                true, std::memory_order_acq_rel))
        {
            const auto failure = Horse::Deterministic::failure_code_name(
                timeline.batch_entry_checkpoint_failure);
            const auto& diagnostic = timeline.batch_entry_checkpoint_validation;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] candidate batch-entry checkpoint unavailable: {} "
                "phase={} detail={} animation={} animation_ptr=0x{:x} fighters=0x{:x}/0x{:x} "
                "index={} observed={}/{} expected={}/{}\n"),
                RC::to_generic_string(std::string(failure)),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::candidate_capture_phase_name(
                        timeline.batch_entry_capture_phase))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::native_candidate_validation_issue_name(
                        diagnostic.issue))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::chara_animation_topology_issue_name(
                        timeline.batch_entry_animation_topology_issue))),
                timeline.batch_entry_animation_observed,
                timeline.batch_entry_animation_fighters[0],
                timeline.batch_entry_animation_fighters[1],
                diagnostic.index, diagnostic.observed_a, diagnostic.observed_b,
                diagnostic.expected_a, diagnostic.expected_b);
        }
    }

    static void on_outer_tick_prepare(
        void* user,
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr) return;
        const auto status = self->m_replay_native_runtime.PrepareResumeOuterTick(
            observation.battle_manager, observation.thread_id);
        if (!status.ok())
            self->m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
    }

    static void on_replay_exit(
        void* user,
        const Horse::Deterministic::ReplayExitObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
        {
            return;
        }
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_replay_exit_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }

        // This callback runs before Replay PostTick mutates camera, fighters,
        // or the queued world mode. Remove the observed native identity first.
        self->m_frame_fencepost_manager.store(0, std::memory_order_release);
        self->m_frame_fencepost_last_frame.store(0, std::memory_order_release);
        self->m_replay_native_runtime.ObserveReplayExit();
        self->m_owned_correction_probe_index = 0;
        self->m_forced_correction_qualification = {};
        self->m_seek_request_active = false;
        self->m_resume_divergence_logged.store(false, std::memory_order_release);
        self->m_seek_completed_target.store(0, std::memory_order_release);
        self->m_seek_completed_source.store(0, std::memory_order_release);
        self->m_seek_completed_verified.store(0, std::memory_order_release);
        self->m_seek_handled_sequence = self->m_seek_request_sequence.load(
            std::memory_order_acquire);
        self->m_seek_pending_sequence = self->m_seek_handled_sequence;
        self->m_seek_defer_count = 0;
        self->m_replay_exit_state.store(
            observation.replay_state, std::memory_order_release);
        self->m_replay_exit_observations.fetch_add(1, std::memory_order_acq_rel);
    }

    void service_frame_fencepost_diagnostics() noexcept
    {
        const std::uint64_t entries =
            m_frame_fencepost_entries.load(std::memory_order_acquire);
        const std::uint64_t observations =
            m_frame_fencepost_observations.load(std::memory_order_acquire);
        if (entries != 0 && observations == 0
            && m_frame_fencepost_last_read_mask.load(std::memory_order_acquire)
                != Horse::Deterministic::Schema::Sc6FrameLayout::
                    required_observation_read_mask
            && !m_frame_fencepost_incomplete_logged)
        {
            m_frame_fencepost_incomplete_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] frame-fencepost invoked but native reads "
                "were incomplete mask=0x{:x}\n"),
                m_frame_fencepost_last_read_mask.load(std::memory_order_acquire));
        }
        if (observations != 0 && !m_frame_fencepost_first_observation_logged)
        {
            m_frame_fencepost_first_observation_logged = true;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] frame-fencepost first observation frame={} "
                "manager=0x{:x}\n"),
                m_frame_fencepost_last_frame.load(std::memory_order_acquire),
                m_frame_fencepost_manager.load(std::memory_order_acquire));
        }

        const auto failure = m_frame_fencepost_failure.load(
            std::memory_order_acquire);
        if (failure != Horse::Deterministic::FailureCode::None
            && !m_frame_fencepost_failure_logged)
        {
            m_frame_fencepost_failure_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] frame-fencepost observation failed: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(failure))));
        }

        const std::uint64_t replay_exits =
            m_replay_exit_observations.load(std::memory_order_acquire);
        if (replay_exits != 0 && !m_replay_exit_first_observation_logged)
        {
            m_replay_exit_first_observation_logged = true;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] replay-exit invalidated native identity "
                "before PostTick state=0x{:x}\n"),
                m_replay_exit_state.load(std::memory_order_acquire));
        }

        const auto replay_failure = m_replay_exit_failure.load(
            std::memory_order_acquire);
        if (replay_failure != Horse::Deterministic::FailureCode::None
            && !m_replay_exit_failure_logged)
        {
            m_replay_exit_failure_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] replay-exit observation failed: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(replay_failure))));
        }
    }
public:
    bool RequestReplaySeek(std::uint64_t frame) noexcept
    {
        if (frame == UINT64_MAX) return false;
        m_seek_request_frame.store(frame, std::memory_order_release);
        m_seek_completed_target.store(0, std::memory_order_release);
        m_seek_request_sequence.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool GetReplaySeekableRange(
        std::uint64_t& generation, std::uint64_t& first_frame,
        std::uint64_t& last_frame) const noexcept
    {
        Horse::Deterministic::FrameCoordinate first{}, last{};
        if (!m_replay_native_runtime.GetSeekableRange(first, last)) return false;
        generation = first.generation;
        first_frame = first.frame;
        last_frame = last.frame;
        return true;
    }

    bool GetReplaySimulationPhase(
        std::int32_t& native_round, std::int32_t& native_time,
        std::uint32_t& round_state_frame,
        std::int32_t& unpause_countdown) const noexcept
    {
        const auto timeline = m_replay_native_runtime.timeline_status();
        if (timeline.canonical_frames == 0) return false;
        native_round = timeline.native_round;
        native_time = timeline.native_time;
        round_state_frame = timeline.round_state_frame;
        unpause_countdown = timeline.unpause_countdown;
        return true;
    }

    std::uint32_t GetReplaySeekStatus(
        std::uint64_t& target_frame,
        std::uint64_t& source_end_frame,
        std::uint64_t& verified_frames,
        std::uint16_t& failure) const noexcept
    {
        const auto timeline = m_replay_native_runtime.timeline_status();
        const auto completed_target = m_seek_completed_target.load(
            std::memory_order_acquire);
        if (completed_target != 0)
        {
            target_frame = completed_target;
            source_end_frame = m_seek_completed_source.load(
                std::memory_order_relaxed);
            verified_frames = m_seek_completed_verified.load(
                std::memory_order_relaxed);
            failure = 0;
            return 1;
        }
        target_frame = timeline.resume_target.frame;
        source_end_frame = timeline.resume_source_end.frame;
        verified_frames = timeline.resumed_frames_verified;
        const auto hook_failure = m_frame_fencepost_failure.load(
            std::memory_order_acquire);
        const auto effective_failure = timeline.failure
            != Horse::Deterministic::FailureCode::None
            ? timeline.failure : hook_failure;
        failure = static_cast<std::uint16_t>(effective_failure);
        if (effective_failure != Horse::Deterministic::FailureCode::None)
            return 3;
        if (timeline.resume_validation_active) return 2;
        if (timeline.resume_target.generation != 0) return 1;
        return 0;
    }

    HorseMod() : CppUserModBase()
    {
        ModName        = STR("HorseMod");
        ModVersion     = STR("0.10.0");
        ModDescription = STR("SC6 KHit hitbox / hurtbox / body visualiser.");
        ModAuthors     = STR("horse");

        horsemod_report_unsupported_legacy_options_once();

        const std::filesystem::path deterministic_config_path =
            std::filesystem::path(horsemod_current_module_path()).parent_path()
            / L"rollback.ini";
        m_deterministic_config_present =
            std::filesystem::exists(deterministic_config_path);
        auto deterministic_load =
            Horse::Deterministic::LoadConfig(deterministic_config_path);
        m_deterministic_config = deterministic_load.config;
        m_replay_native_runtime.SetForcedDepth7QualificationEnabled(
            m_deterministic_config.forced_depth7_qualification);
        if (deterministic_load.status.ok() && m_deterministic_config.trace)
        {
            const auto report_path = deterministic_config_path.parent_path()
                / L"reports" / L"deterministic"
                / L"hgcpu_coverage_runtime.md";
            m_hgcpu_runtime_diagnostics = std::make_unique<
                Horse::Deterministic::HgCpuRuntimeDiagnostics>(report_path);
            m_stage_break_listener_diagnostics = std::make_unique<
                Horse::Deterministic::StageBreakListenerRuntimeDiagnostics>(
                    report_path.parent_path() / L"stage_break_listener_topology.md");
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] deterministic runtime diagnostics armed; "
                "stock simulation remains active\n"));
        }
        if (!deterministic_load.status.ok())
        {
            m_deterministic_config.enabled = false;
            m_deterministic_failure = deterministic_load.status.code;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] rollback.ini is invalid; deterministic simulation "
                "remains disabled\n"));
        }
        else if (m_deterministic_config.enabled
                 && Horse::Deterministic::Schema::production_regions.empty())
        {
            m_deterministic_failure =
                Horse::Deterministic::FailureCode::AdapterUnqualified;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] rollback.ini requested activation, but no native "
                "state regions are qualified; stock behavior is unchanged\n"));
        }
        if (deterministic_load.status.ok()
            && !deterministic_load.diagnostics.empty()
            && m_deterministic_config_present)
        {
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] rollback.ini contained unsupported or invalid "
                "configuration; see the deterministic status panel\n"));
        }

        // The overlay Present hook still waits for Steam's first frames, but
        // SC6 may decide whether to poll XInput during title/menu bootstrap.
        // Install only the overlay's XInput gate here; rollback automation
        // uses native request-file orchestration, not OS/controller scripts.
        (void)Horse::GameImGui::XInputHook::instance().install();

        // Load persisted settings BEFORE any render path can observe
        // an atomic.  If settings.cfg is missing (first-run) each
        // get_* call returns its default argument, matching the
        // compiled-in defaults - functionally identical to the
        // pre-persistence behaviour on a clean install.
        load_persisted_settings();

        // Materialize a first-run settings.cfg immediately instead of
        // relying on the later on_update save tick.  This keeps fresh
        // Thunderstore profiles inspectable even if the user exits from
        // the title screen before the periodic save runs.
        save_persisted_settings();

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
        // LuxBattleFunctionLibrary - that's wrong and crashed startup
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
                // Hide on all KHit backends so neither persistent trails
                // nor one-frame fallback lines survive overlay-off.
                hide_khit_overlay_lines();
            }
        });

        // F6 - single-frame step.  Lazily turns on Freeze-frame on first
        // press so the user doesn't need to touch the ImGui tab.  Holding
        // F6 yields ~30 fps slow-motion via UE4SS's keyboard auto-repeat:
        // each press queues one frame; the cockpit-hook state machine
        // drains them one per two cockpit ticks (see frame_step_apply).
        //
        // F7 / F6 both honour the General-tab "Auto disable online"
        // gate - if we're in a Ranked / Casual match with the gate on,
        // pressing the hotkey is a no-op (with a one-line log so the
        // user knows their press was ignored, not lost).  This matches
        // the UI checkbox behaviour: the ImGui control is greyed out
        // and clicking does nothing; the hotkey shouldn't be a
        // back-door around that.
        register_keydown_event(Input::Key::F7, no_mods, [this]() {
            if (Horse::GameMode::instance().should_force_disable_features())
            {
                Output::send<LogLevel::Default>(
                    STR("[HorseMod] F7 ignored - Free-fly camera is "
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
            // does NOT latch freeze - it only adds to step_pending and
            // is greyed out unless Freeze is already on.  That's by
            // design: the button path expects the user to have opened
            // the HUD and turned on Freeze deliberately, whereas the
            // hotkey path is the "just press F6 and it works"
            // convenience entry-point.
            if (Horse::GameMode::instance().should_force_disable_features())
            {
                Output::send<LogLevel::Default>(
                    STR("[HorseMod] F6 ignored - Freeze frame is "
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
        // longer register our tab with UE4SS's external GUI - the tab
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
        // only reliable way to catch keys past NOLEGACY - this is
        // the same trick Horse::LowLevelKeyInput uses for F5/F6/F7.
        //
        // Back/Select on the gamepad also toggles the overlay; that
        // half is wired inside horselib/GameImGui/GamepadInput.hpp's
        // BACK-button edge detector.
        register_keydown_event(Input::Key::F2, no_mods, [this]() {
            if (m_gameimgui_toggle_key_down.exchange(
                    true, std::memory_order_acq_rel))
            {
                return;
            }

            bool v = !Horse::GameImGui::visible();
            Horse::GameImGui::set_visible(v);
            Output::send<LogLevel::Default>(
                STR("[HorseMod] F2 pressed - overlay {}\n"),
                v ? STR("SHOWN") : STR("HIDDEN"));
        });

        s_instance.store(this);
        Output::send<LogLevel::Default>(
            STR("[HorseMod] ctor v{} source={}\n"),
            RC::to_generic_string(HORSEMOD_VERSION),
            RC::to_generic_string(HORSEMOD_SOURCE_COMMIT));
    }

    ~HorseMod() override
    {
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor ENTER\n"));

        if (m_hgcpu_runtime_diagnostics)
            m_hgcpu_runtime_diagnostics->Finish();
        if (m_stage_break_listener_diagnostics)
            m_stage_break_listener_diagnostics->Finish();

        m_deterministic_hooks.Uninstall();
        m_ucrt_rand_broker.Stop();
        if (m_deterministic_config.trace)
        {
            const auto failure = m_frame_fencepost_failure.load(
                std::memory_order_acquire);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] frame-fencepost summary observed={} repeats={} "
                "generations={} replay_exits={} failure={}\n"),
                m_frame_fencepost_observations.load(std::memory_order_acquire),
                m_frame_fencepost_repeats.load(std::memory_order_acquire),
                m_frame_fencepost_generations.load(std::memory_order_acquire),
                m_replay_exit_observations.load(std::memory_order_acquire),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(failure))));
        }
        m_replay_native_runtime.Shutdown();

        // Final settings save - catches anything the periodic
        // on_update save would have missed in the last sub-2s window
        // before shutdown.  Crashes lose at most the most-recent
        // ~2-second window of changes; graceful exits lose nothing.
        save_persisted_settings();

        // Restore visual-only stage hiding before the UObject hook is
        // removed so a graceful unload cannot leave stage actors hidden.
        m_stage_visuals.restoreNow();

        // Zero instance pointer early so any in-flight hook sees null.
        s_instance.store(nullptr);

        if (m_engine_tick_callback_id != RC::Unreal::Hook::ERROR_ID)
        {
            (void)RC::Unreal::Hook::UnregisterCallback(
                m_engine_tick_callback_id);
            Output::send<LogLevel::Verbose>(STR(
                "[HorseMod] dtor unregistered engine tick callback id={}\n"),
                m_engine_tick_callback_id);
            m_engine_tick_callback_id = RC::Unreal::Hook::ERROR_ID;
        }

        // Tear down the in-game ImGui overlay BEFORE unregistering the
        // cockpit hook.  Order matters only loosely here, but calling
        // shutdown() synchronises: it unHooks the DXGI vtable (Present
        // calls immediately revert to whatever was installed before
        // us - usually Steam's hook directly), restores the game's
        // WndProc, and releases our D3D11 RTV.  After this returns no
        // further render_tab_impl calls can happen from our hook.
        if (m_gameimgui_tab_token)
        {
            Horse::GameImGui::unregister_tab(m_gameimgui_tab_token);
            m_gameimgui_tab_token = 0;
        }
        if (m_gameimgui_toast_token)
        {
            Horse::GameImGui::unregister_passive_draw_callback(
                m_gameimgui_toast_token);
            m_gameimgui_toast_token = 0;
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

        // Tear down the SetPresence post-hook so the lambda doesn't
        // fire on a freed cached path-string after dllmain unload.
        // Idempotent.
        Horse::GameMode::instance().uninstall_hook();

        // m_cam_lock will restore any active patches via its own dtor
        // when our member destruction runs after this body returns.
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor EXIT\n"));
    }

    // No on_ui_init() override - UE4SS_ENABLE_IMGUI() set up the shared
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
        // Register callbacks now, but delay the DXGI vtable swap for a
        // few game-thread updates.  Fresh Steam launches can still be
        // finishing gameoverlayrenderer64.dll's first Present path when
        // on_unreal_init runs; installing our vtable hook immediately can
        // let Steam re-enter Present through our slot and recurse until
        // stack overflow.  The delayed install gives Steam's code-patched
        // DXGI hook a few normal frames before HorseMod starts rendering.
        // -----------------------------------------------------------
        m_gameimgui_init_pending = true;
        m_gameimgui_init_attempted = false;
        m_gameimgui_init_delay_ticks_remaining =
            kGameImGuiDeferredInstallTicks;
        Output::send<LogLevel::Default>(
            STR("[HorseMod] scheduled GameImGui install after {} "
                "game-thread ticks\n"),
            kGameImGuiDeferredInstallTicks);
        m_gameimgui_tab_token = Horse::GameImGui::register_tab(
            L"HorseMod", [this] { this->render_tab_impl(); });
        m_gameimgui_toast_token =
            Horse::GameImGui::register_passive_draw_callback([] {
                return Horse::GameImGui::ToastManager::instance().draw();
            });

        RC::Unreal::Hook::FCallbackOptions engine_tick_opts{};
        engine_tick_opts.bReadonly = true;
        engine_tick_opts.OwnerModName = STR("HorseMod");
        engine_tick_opts.HookName = STR("OverlayService");
        m_engine_tick_callback_id =
            RC::Unreal::Hook::RegisterEngineTickPostCallback(
                [](RC::Unreal::Hook::TCallbackIterationData<void>&,
                   RC::Unreal::UEngine*, float, bool) {
                    HorseMod* self = s_instance.load(std::memory_order_acquire);
                    if (!self) return;
                    if (self->m_frame_fencepost_expected_thread.load(
                            std::memory_order_acquire) == 0)
                    {
                        self->m_frame_fencepost_expected_thread.store(
                            ::GetCurrentThreadId(), std::memory_order_release);
                    }
                    self->service_gameimgui_toggle_key_release();
                    self->service_gameimgui_deferred_install();
                    self->draw_line_overlays_after_battle_tick();
                }, engine_tick_opts);
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] engine tick overlay service registered id={}\n"),
            m_engine_tick_callback_id);

        // Resolve SC6 native RVAs now that the game image is loaded.  KHit
        // rendering reads native world buffers directly; the remaining
        // pointers cover reset/start-position, online rules, presence
        // tracking, line-batcher refresh, and throw-height prediction.
        Horse::NativeBinding::resolve();
        m_replay_native_runtime_status = m_replay_native_runtime.Initialize(
            Horse::NativeBinding::imageBase(), &m_ucrt_rand_broker);
        if (!m_replay_native_runtime_status.ok())
        {
            const auto failure = Horse::Deterministic::failure_code_name(
                m_replay_native_runtime_status.code);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] native replay bridge unavailable: {}; "
                "deterministic simulation remains disabled\n"),
                RC::to_generic_string(std::string(failure)));
        }
        if (m_deterministic_config.trace)
        {
            const auto ucrt_started = m_ucrt_rand_broker.Start();
            if (!ucrt_started.ok())
            {
                m_frame_fencepost_hook_status = ucrt_started;
                return;
            }
            m_frame_fencepost_expected_thread.store(
                0, std::memory_order_release);
            m_frame_fencepost_hook_status = m_deterministic_hooks.Install(
                Horse::NativeBinding::imageBase(),
                {this, &HorseMod::on_frame_fencepost,
                    &HorseMod::on_outer_tick_prepare,
                    &HorseMod::on_outer_tick_begin, &HorseMod::on_outer_tick,
                    &HorseMod::on_replay_exit},
                &m_ucrt_rand_broker);
            if (!m_frame_fencepost_hook_status.ok())
            {
                m_ucrt_rand_broker.Stop();
                const auto failure = Horse::Deterministic::failure_code_name(
                    m_frame_fencepost_hook_status.code);
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] frame-fencepost runtime proof unavailable: {}\n"),
                    RC::to_generic_string(std::string(failure)));
            }
            else
            {
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] deterministic lifecycle hooks armed; "
                    "stock simulation remains authoritative\n"));
            }
        }

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
        // chokepoint for ALL 5 BattleRule overrides - the launcher's
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
        // for the SlipOut policy specifically - every client runs the
        // chara-init function that calls
        // LuxBattleChara_HasSubProviderEntryOfType0x3e, so PolyHooking
        // it gives both peers the same answer regardless of which side
        // initiated the match.  See the file-header doc for the full
        // rationale and the link to the previous failed-test
        // investigation.
        Horse::HasSubProviderEntryHook::instance().install();

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

        service_presence_transition_safety("update");
        service_frame_fencepost_diagnostics();
        service_gameimgui_toggle_key_release();
        // IsInGameThread() throws until UE4SS records the game-thread id;
        // on_update can run before that during startup. Use only the old
        // API here so Thunderstore's UE4SS shimloader does not need the
        // newer IsInGameThreadRaw() export.
        const bool in_game_thread = []() noexcept {
            try { return RC::Unreal::IsInGameThread(); }
            catch (...) { return false; }
        }();
        if (in_game_thread
            && m_engine_tick_callback_id == RC::Unreal::Hook::ERROR_ID)
        {
            service_gameimgui_deferred_install();
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
        // and silent on retry - the LuxUIGamePresenceUtil class is a
        // BlueprintFunctionLibrary loaded very early, so this usually
        // succeeds on the first poll attempt.
        if (!game_mode_installed)
            (void)Horse::GameMode::instance().try_install_hook();
    }

private:
    void service_gameimgui_toggle_key_release() noexcept
    {
        if (!m_gameimgui_toggle_key_down.load(std::memory_order_acquire))
            return;

        const bool async_down = (::GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        const bool ll_down = Horse::LowLevelKeyInput::instance().is_down(VK_F2);
        const bool raw_down = Horse::RawInputSource::instance().is_down(VK_F2);
        if (!async_down && !ll_down && !raw_down)
        {
            m_gameimgui_toggle_key_down.store(false,
                                              std::memory_order_release);
        }
    }

    void service_gameimgui_deferred_install()
    {
        if (!m_gameimgui_init_pending || m_gameimgui_init_attempted) return;
        if (m_gameimgui_init_delay_ticks_remaining > 0)
        {
            --m_gameimgui_init_delay_ticks_remaining;
            return;
        }

        m_gameimgui_init_pending = false;
        m_gameimgui_init_attempted = true;
        if (!Horse::GameImGui::initialize())
        {
            Output::send<LogLevel::Error>(
                STR("[HorseMod] Horse::GameImGui::initialize() failed; "
                    "the in-game ImGui overlay will not appear.\n"));
            return;
        }

        Output::send<LogLevel::Default>(
            STR("[HorseMod] deferred GameImGui install complete\n"));
    }

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
        // at line 810 (Function->GetFunc()) - null deref crashes the
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

        // Wrap RegisterHook in try/catch - the underlying UE4SS code
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
            // Don't set m_hook_registered - poll loop will skip this
            // path on retry once the underlying issue is fixed.  In
            // practice an exception here means the engine version is
            // wrong and we won't recover, but we'd rather log forever
            // than crash forever.
            return;
        }

        // RegisterHook returns {0, 0} for the global-script-hook path
        // (UObjectGlobals.cpp:842 increments before assigning, so the
        // smallest legitimate ID is 1).  An all-zero pair is therefore
        // a sentinel for "registration silently no-op'd" - defensively
        // refuse to mark registered so we keep retrying.
        if (m_hook_ids.first == 0 && m_hook_ids.second == 0)
        {
            Output::send<LogLevel::Warning>(
                STR("[HorseMod] RegisterHook returned (0,0) for '{}' - "
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
    // LuxBattleChara_SetStartPosition) for that path - the right spot to
    // overwrite the chara pose with the user's captured override.
    //
    // Multi-path rationale: the user's reset bind goes through a UFunction
    // we can't determine statically (they may have rebound it; SC6's
    // training-mode UI may dispatch via a different entry point depending
    // on context).  We register on every plausible candidate and the one
    // that fires logs its identity via the custom_data tag - both for our
    // diagnosis here and for the user to see in UE4SS.log.
    //
    // Each slot's class lookup gates that slot independently - failed
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
                const bool reset_override_enabled =
                    Horse::ResetOverride::instance().enabled();
                if (reset_override_enabled)
                {
                    Output::send<LogLevel::Default>(
                        STR("[HorseMod] reset hook fired: {}\n"),
                        path ? path : STR("(unknown path)"));
                }
                else
                {
                    Output::send<LogLevel::Verbose>(
                        STR("[HorseMod] reset hook ignored while disabled: {}\n"),
                        path ? path : STR("(unknown path)"));
                }

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
                // Class not yet registered - try again next poll tick.
                continue;
            }

            // CRITICAL: also verify the UFunction exists before calling
            // RegisterHook(path).  UE4SS's path-overload of RegisterHook
            // (UObjectGlobals.cpp:859) calls StaticFindObject<UFunction*>
            // and then immediately dereferences the result via
            // Function->GetFunc() - so a null-result (function-not-found)
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
                        "'{}' not found on class '{}' - typo or wrong class? "
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
                        "for '{}' - treating as failure.\n"),
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
    // Safe to call before NativeBinding is resolved - this path is
    // pure UE4 reflection and does not touch SC6 RVAs.  Safe when no
    // ----------------------------------------------------------------
    // Online-match feature gate - force-disable the four "competitive"
    // features (lock camera, free-fly, freeze frame, slow motion) when
    // the user is in a Ranked / Casual online match AND has "Auto
    // disable online" enabled in the General tab.  Idempotent - calling
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
    //     interfere with actor lifecycle - the engine's camera
    //     stores still update the underlying memory, our patch just
    //     no-ops the writer.  Carrying it across a transition is
    //     harmless.
    //   - Free-fly camera owns the camera-lock state machine; it
    //     too is harmless across transitions because the cockpit
    //     hook has its own resolve-on-first-use logic for the new
    //     mode's camera manager.
    //
    // Freeze + slow-mo are different because they install into
    // PerFrameTick / replay tick / cursor advance - exactly the
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
        // World-tick gate: same hazard as the SpeedControl patches -
        // a presence transition rebuilds BattleManager + chara actors,
        // and a stale gate "frozen" state would block PerFrameTick on
        // the new mode's first tick (= black screen).  Disable so the
        // engine runs at native rate during the transition; user re-
        // engages freeze/step manually after the new mode loads.
        // Disable the sibling gates first (they READ the WorldTickGate
        // policy slot, so leaving them enabled past the gate's disable
        // would be harmless but pointless).
        if (m_actor_tick_gate.is_enabled())
            m_actor_tick_gate.disable();
        if (m_time_dilation_gate.is_enabled())
            m_time_dilation_gate.disable();
        if (m_wind_rng_gate.is_enabled())
            m_wind_rng_gate.disable();
        if (m_world_tick_gate.is_enabled())
            m_world_tick_gate.disable();
    }

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
            // Don't call m_free_camera.set(false, ...) directly here -
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
        // is off - and we're about to force that off too).
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
        if (m_actor_tick_gate.is_enabled())
            m_actor_tick_gate.disable();
        if (m_time_dilation_gate.is_enabled())
            m_time_dilation_gate.disable();
        if (m_wind_rng_gate.is_enabled())
            m_wind_rng_gate.disable();
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
        // back to 1.0 - see SpeedControl::disable()).
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
        // Nothing to do while both the toggle and last-applied are off -
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
                // - the toggle is visibly inert in that case.)
                return;
            }
        }

        UFunction* f = m_fn_set_photo_allowed.on(
            m_ansel_cdo, STR("SetIsPhotographyAllowed"));
        if (!f) return;

        // `now` = desired visibility state; `last` captures whether
        // we're on the restore edge (last=true, now=false ? push false
        // once).
        struct { bool bIsPhotographyAllowed; } p{ now };
        m_ansel_cdo->ProcessEvent(f, &p);

        m_last_applied_ansel_allowed.store(now);
    }

    // ---- Frame-step + freeze-frame driver -------------------------------
    // Called every cockpit tick. Computes gate policy from the user's Freeze
    // and Slow-mo toggles, plus pending step-frame requests, and publishes it
    // to WorldTickGate plus replay/actor/time sibling gates.
    //
    // ==========================================================================
    // STEP PROTOCOL (current state, 2026-05)
    // ==========================================================================
    // The cockpit pre-hook fires on the UMG widget tick. Current stepping no
    // longer depends on cockpit timing or speedval writes: m_step_pending is
    // drained into WorldTickGate credits, and PerFrameTick consumes exactly
    // one credit per native game frame.
    //
    // KNOWN OPEN ISSUES (2026-05):
    //   * Multi-hit moves only register the FIRST hit when frame-stepped
    //     in training mode.  Sites 19/20/21/22 (replay-pipeline gates +
    //     chara TickActor entry-RET) are enabled but did not fix this.
    //     A speculative BattleAdvanceFlag override (force flOutBlendW0=1,
    //     nOutModeTag=0 at step Tick A to defeat the AND-of-three gate
    //     at PerFrameTick step 3) was tried and reverted - empirically
    //     didn't fix it, AND had side effects during normal gameplay.
    //     Root cause is deeper in the hit-classifier or per-cell hit-mask
    //     advance path; needs targeted investigation.
    //   * Held inputs may not refresh correctly during step.  Likely
    //     related to the multi-hit miss above (shared upstream cause).
    //   * GetTimeDilationScalar Path A (chara+0x3510 < 0 = super-freeze /
    //     soul-charge cinematic / KO replay) is engine-controlled.
    //   * AdvanceLaneFrameStep advances by dt - pLane[+0x30] (PlaybackSpeed).
    //     Moves with non-unity playback speed advance by != 1.0 anim
    //     frames per step.  Matches native gameplay; by-engine design.
    //
    // State machine:
    //   click(F6)  m_step_pending++
    //   cockpit    add m_step_pending to WorldTickGate credits
    //   PerFrameTick consumes one credit and runs once at native dt

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
    // current snapshot fails) - the conservative "assume ticked"
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
        const bool freeze = m_freeze_frame.load();
        const bool slow_mo = m_speed_enabled.load();
        const int pending = m_step_pending.exchange(0);

        if (freeze || pending > 0)
        {
            if (!m_world_tick_gate.is_resolved())
                m_world_tick_gate.resolve();
            if (m_world_tick_gate.is_resolved()
                && !m_world_tick_gate.is_enabled())
                m_world_tick_gate.enable();
            if (!m_actor_tick_gate.is_resolved())
                m_actor_tick_gate.resolve(
                    m_world_tick_gate.policy_slot_address());
            if (m_actor_tick_gate.is_resolved()
                && !m_actor_tick_gate.is_enabled())
                m_actor_tick_gate.enable();
            if (!m_time_dilation_gate.is_resolved())
                m_time_dilation_gate.resolve(
                    m_world_tick_gate.policy_slot_address());
            if (m_time_dilation_gate.is_resolved()
                && !m_time_dilation_gate.is_enabled())
                m_time_dilation_gate.enable();
            if (pending > 0)
                m_world_tick_gate.add_step(pending);
            if (m_speed_control.is_enabled())
                m_speed_control.disable();
            m_last_tick_kind.store(
                static_cast<uint8_t>(pending > 0
                    ? TickKind::Go
                    : TickKind::Stop),
                std::memory_order_release);
            return;
        }

        if (m_world_tick_gate.is_enabled())
            m_world_tick_gate.disable();
        if (m_actor_tick_gate.is_enabled())
            m_actor_tick_gate.disable();
        if (m_time_dilation_gate.is_enabled())
            m_time_dilation_gate.disable();

        if (slow_mo)
        {
            if (!m_speed_control.is_resolved())
                m_speed_control.resolve();
            if (!m_speed_control.is_enabled())
                m_speed_control.enable();
            m_speed_control.set_value(m_speed_value.load());
        }
        else if (m_speed_control.is_enabled())
        {
            m_speed_control.disable();
        }
        m_last_tick_kind.store(
            static_cast<uint8_t>(slow_mo
                ? TickKind::Go
                : TickKind::Inactive),
            std::memory_order_release);
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

    // try_force_battle_advance_flag: REMOVED 2026-05-02 - see comment
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
    // for each chara, if (16E5==1 && 16EA==0 && 16EB==1) ? clear 16EB to
    // 0.  This emulates the engine's between-active-phases cleanup that
    // would normally come out of hit-stop end.  Conditions:
    //
    //   16E5==1   = the chara IS attacking.  Don't clear lockout if not
    //                (e.g. defender just past a hit they took - irrelevant).
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
    // (B) and (C) work just like native - IF the threshold is reached.
    // (A) is the one that consistently breaks during step: hit-stop
    // queues, but the scheduler that consumes the queue gates on
    // VMFreezeByte and on a tight tick cadence that the step rhythm
    // disrupts; the diagnostic log on Siegfried 4A+B shows
    // chara+0x3500 stays at 1.0 and +0x3508 stays at -1 throughout the
    // master window - hit-stop never engages - so 16EB latches at the
    // first hit and stays latched forever.
    //
    // STRATEGY
    // --------
    // This helper is a SAFETY NET, not a reimplementation.  It only
    // fires when ALL of the following hold:
    //   * Chara is attacking            (16E5=1)
    //   * Lockout is latched            (16EB=1)
    //   * Hit-stop is NOT running       (3508 <= 0)
    //                                    - hit-stop would naturally pace
    //                                      and clear 16EB on its own
    //   * No 16EB-conditional override  (lane[+0x5E] == -1 on lane 0/1)
    //                                    - engine path (B) handles this
    //
    // When all gate, we apply the cadence-counted clear: count step
    // ticks where 16EA=0 (engine forced it off due to 16EB), and after
    // kEBLockoutDelay ticks, clear 16EB so the next tick's classifier
    // can re-arm 16EA and the resolver can fire another hit.  This
    // emulates the timing that hit-stop would have produced.
    //
    // The cadence is DATA-DRIVEN per cell, derived from
    // cell+0x46 (HitstunStandingNormal) - read every step, divided by 4.
    // RATIONALE: SC6 doesn't expose an explicit "frames between hits"
    // field anywhere I could locate via static analysis; what IS
    // authored on every cell is hitstun (the defender's stun frames).
    // In fighting-game design, hit-stop (the attacker's stop frames
    // between hits) is typically ~1/4 of hitstun, so we use that as
    // our derived cadence.  For Siegfried 4A+B (cell+0x46 = 30):
    //   K = 30/4 = 7  ? 8-frame cycle ? hits at anim 18/26/34
    //                    in the [17..39] master window = 3 hits.
    // For shorter-hitstun moves the cadence shrinks proportionally;
    // for single-hit moves the cell's master window is short enough
    // that the second cycle never lands in the active phase, so they
    // still fire only once.  No per-move table required.
    //
    // CAVEAT: this is a HEURISTIC (the /4 ratio).  The engine's exact
    // multi-hit pacing for moves like 4A+B uses a mechanism I could
    // not isolate via static byte search of all common store
    // encodings - the 16EB latch isn't written via direct disp32, it
    // appears to come from indirect addressing or a struct-stamp path
    // (probably inside the hit-application chain in
    // LuxBattleChara_*).  If a move under-/over-fires, the formula
    // is the lever - change /4 to /3 (faster) or /5 (slower).
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
                // when the timer expires.  Don't interfere - even
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
                // Check both lanes - the active attack could be on
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
                // that path - clearing 16EB heuristically here would let
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
                // concern as 0x5A - let the engine's transition path
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

                // 16E5=1, 16EB=1, 16EA=0 - engine forced 16EA off
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
                // Faulted - chara pointer not mapped this frame.  Skip.
                s_eb_lockout_delay[pi] = 0;
            }
        }
    }

    // ------------------------------------------------------------------
    // Free-fly camera driver - wired into the cockpit pre-hook.
    //
    // Per-tick responsibilities:
    //   * Resolve the ALuxBattleCamera* from LuxBattleManager.BattleCamera
    //     (UObject property, stable within a battle, null between).
    //   * Handle UI-toggle edge transitions (ON ? snapshot current pose +
    //     enable CamLock; OFF ? release CamLock, drop our pose state).
    //   * If enabled, drive the camera-manager's POV cache from
    //     keyboard / gamepad input via FreeCamera::tick(), which
    //     writes directly to PCM+0x410..+0x428.  That block IS the
    //     renderer-facing POV - see the long comment on
    //     m_cached_player_camera_manager above for the Ghidra
    //     evidence and the iterations that led us to it.
    //
    // We DON'T early-return when the overlay F5 is off - the user may
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

        // Resolve the APlayerCameraManager every tick - this is the
        // write target for Free-Fly pose data (see the long comment
        // on m_cached_player_camera_manager for why it is NOT the
        // ALuxBattleCamera from LuxBattleManager.BattleCamera).
        //
        // Primary path (reflection):
        //   find-first-of APlayerController ? read its
        //   "PlayerCameraManager" UObject* property.
        // Fallback (direct offset):
        //   PC+0x420 is the native PlayerCameraManager field on
        //   APlayerController - this is the EXACT offset that
        //   UWorld::Tick @ 0x141f02230 reads when it invokes
        //   APlayerCameraManager_CommitPOV_NoInterp(pc[0x84]).
        //   If UE4SS reflection ever fails to find the property
        //   (e.g. a build where the name string is stripped) the
        //   direct-offset path still works.
        //
        // Both lookups are hashed FName-indexed / trivial pointer reads
        // and cached across frames via GlobalPtr::get / Obj::getObj.
        // GlobalPtr::get throttles its O(N) revalidation scan (see
        // HorseLib.hpp); the presence-transition block above
        // invalidate()s m_player_controller so a torn-down PC is
        // re-resolved promptly instead of at the next throttle tick.
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
                // Direct-offset fallback - matches the UWorld::Tick
                // read that feeds the engine's own commit path.
                auto* pc_bytes = reinterpret_cast<uint8_t*>(pc_raw);
                void* raw_pcm = *reinterpret_cast<void**>(pc_bytes + 0x420);
                if (raw_pcm) pcm = raw_pcm;
                if (!m_logged_pcm_fallback)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[HorseMod.FreeCamera] reflection didn't find "
                            "PlayerCameraManager property - using direct "
                            "offset fallback PC+0x420 -> 0x{:x}\n"),
                        reinterpret_cast<uintptr_t>(pcm));
                    m_logged_pcm_fallback = true;
                }
            }
        }
        // One-shot first-resolve log - captures BOTH the PC address
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
        // This is the ONLY commit path - direct memcpy into the PCM's
        // FCameraCacheEntry.POV at +0x410..+0x428.  CamLock's 5 NOP
        // sites (all of which also target PCM+0x410..+0x428) stop the
        // engine from stomping our writes each tick.
        m_free_camera.tick(pcm);
    }

    void service_presence_transition_safety(const char* source)
    {
        const uint8_t cur = static_cast<uint8_t>(
            Horse::GameMode::instance().current_presence());
        const uint8_t prev = m_last_seen_presence.exchange(
            cur, std::memory_order_acq_rel);
        if (prev == cur)
            return;

        using GMP = Horse::GamePresence;
        const GMP from = static_cast<GMP>(prev);
        const GMP to   = static_cast<GMP>(cur);
        if (from != GMP::Unknown)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] presence transition {} -> {} - "
                    "force-clearing Freeze frame + Slow-motion + "
                    "step queue (manual re-enable required, source={})\n"),
                Horse::presence_name(from),
                Horse::presence_name(to),
                RC::to_generic_string(source ? source : "?"));
        }

        clear_time_features_on_transition();

        // Drop the cached battle-level globals.  LuxBattleManager /
        // CockpitBase / PlayerController are torn down across this
        // transition; invalidating forces the next GlobalPtr::get() to
        // re-resolve immediately.
        m_lux.invalidate();
        m_player_controller.invalidate();
        m_backend_hit.invalidate();
        m_backend_hurt.invalidate();
        m_backend_hit_once.invalidate();
        m_backend_hurt_once.invalidate();
        m_backend_stage.invalidate();
        m_stage_boundary.invalidate();
        m_stage_visuals.invalidate();
    }

    // ------------------------------------------------------------------
    // Line overlays that must sample completed battle state.
    //
    // CockpitBase_C::Update can run before LuxBattle_PerFrameTick on a UE
    // frame.  KHit node buffers are refreshed inside that battle tick, so
    // drawing from the cockpit pre-hook can show previous native KHit
    // positions while the later collision pass already uses the new ones.
    // This post-engine-tick path samples after the battle tick has had its
    // chance to flip Area buffers, update Sphere/FixArea world points, apply
    // Sphere anim-cell modifiers, and resolve hits.
    // ------------------------------------------------------------------
    static bool can_draw_battle_overlays_for_presence(
        Horse::GamePresence presence) noexcept
    {
        using GMP = Horse::GamePresence;
        switch (presence)
        {
            case GMP::ShinEdgeMaster:
            case GMP::Chronicle:
            case GMP::Arcade:
            case GMP::Versus:
            case GMP::Training:
            case GMP::RankMatch:
            case GMP::CasualMatch:
            case GMP::Replay:
            case GMP::Tournament:
                return true;
            case GMP::MainMenu:
            case GMP::Creation:
            case GMP::Ranking:
            case GMP::Museum:
            case GMP::Options:
            case GMP::Unknown:
                return false;
        }
        return false;
    }

    void draw_line_overlays_after_battle_tick()
    {
        if (!can_draw_battle_overlays_for_presence(
                Horse::GameMode::instance().current_presence()))
        {
            m_have_sphere_audit_frame = false;
            m_khit_render_calibration = {};
            return;
        }

        const bool wants_line_overlay =
            m_show_stage_boundary.load() || m_enabled.load();
        if (!wants_line_overlay)
        {
            m_have_sphere_audit_frame = false;
            m_khit_render_calibration = {};
            return;
        }

        Horse::Obj pivot = m_lux.cockpit();
        if (!pivot) return;

        const bool native_ready = Horse::NativeBinding::isReady();
        if (!native_ready)
        {
            if ((m_enabled.load() || m_show_stage_boundary.load()) &&
                !m_logged_native_missing)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] NativeBinding not ready - overlay draw disabled\n"));
                m_logged_native_missing = true;
            }
            return;
        }

        // Render cadence and gameplay cadence are deliberately separate.
        // Freeze-frame halts g_LuxBattle_FrameCounter, but the overlay still
        // has to redraw every engine-post callback so foreground/current
        // hitboxes remain visible.  The game-frame counter is used only below
        // for persistent trail sampling and lifetime aging.

        if (m_show_stage_boundary.load())
        {
            if (m_backend_stage.slot() != Horse::LineBatcherSlot::Foreground)
                m_backend_stage.setSlot(Horse::LineBatcherSlot::Foreground);
            m_backend_stage.setLifetime(Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_stage.primeFrom(pivot);
            if (m_backend_stage.isReady())
            {
                Horse::Obj bm = m_lux.battleManager();
                Horse::Obj stageManager =
                    bm ? bm.getObj(L"BattleStageActorManager") : Horse::Obj{};
                m_backend_stage.beginFrame();
                (void)m_stage_boundary.draw(m_backend_stage, stageManager);
                m_backend_stage.endFrame();
            }
        }

        if (!m_enabled.load()) return;

        const Horse::LineBatcherSlot desired_hit_slot  = m_slot_hit.load();
        const Horse::LineBatcherSlot desired_hurt_slot = m_slot_hurt.load();
        const bool only_active_this_frame = m_only_show_active.load();

        bool trail_filter_changed = false;
        if (m_have_trail_filter_state)
        {
            trail_filter_changed =
                only_active_this_frame != m_last_trail_only_active;
        }
        m_last_trail_only_active = only_active_this_frame;
        m_have_trail_filter_state = true;

        // Sync each configured backend's slot with the per-feature ImGui
        // toggles and prime all KHit backends this frame.  *_once backends
        // are fixed Foreground fallbacks for inactive boxes when the
        // configured backend is Persistent.
        bool trail_slot_changed = false;
        bool clear_hit_trail_after_prime = false;
        bool clear_hurt_trail_after_prime = false;
        if (m_backend_hit.slot() != desired_hit_slot)
        {
            if (m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hit.clearLines();
            m_backend_hit.setSlot(desired_hit_slot);
            clear_hit_trail_after_prime =
                desired_hit_slot == Horse::LineBatcherSlot::Persistent;
            trail_slot_changed = true;
        }
        if (m_backend_hurt.slot() != desired_hurt_slot)
        {
            if (m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hurt.clearLines();
            m_backend_hurt.setSlot(desired_hurt_slot);
            clear_hurt_trail_after_prime =
                desired_hurt_slot == Horse::LineBatcherSlot::Persistent;
            trail_slot_changed = true;
        }
        if (m_backend_hit_once.slot() != Horse::LineBatcherSlot::Foreground)
            m_backend_hit_once.setSlot(Horse::LineBatcherSlot::Foreground);
        if (m_backend_hurt_once.slot() != Horse::LineBatcherSlot::Foreground)
            m_backend_hurt_once.setSlot(Horse::LineBatcherSlot::Foreground);
        if (trail_slot_changed)
            m_have_trail_game_frame = false;

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
                desired_hit_slot == Horse::LineBatcherSlot::Persistent
                    ? trail_seconds
                    : Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_hurt.setLifetime(
                desired_hurt_slot == Horse::LineBatcherSlot::Persistent
                    ? trail_seconds
                    : Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_hit_once.setLifetime(
                Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_hurt_once.setLifetime(
                Horse::LineBatcherBackend::kDefaultLifetime);
        }

        m_backend_hit.primeFrom(pivot);
        m_backend_hurt.primeFrom(pivot);
        m_backend_hit_once.primeFrom(pivot);
        m_backend_hurt_once.primeFrom(pivot);

        // All KHit backends must be ready to proceed - partial readiness
        // could split active trails from one-frame inactive boxes and
        // visually misrepresent the current move state.
        if (!m_backend_hit.isReady() || !m_backend_hurt.isReady() ||
            !m_backend_hit_once.isReady() || !m_backend_hurt_once.isReady())
            return;

        if (clear_hit_trail_after_prime || clear_hurt_trail_after_prime ||
            trail_filter_changed)
        {
            if ((clear_hit_trail_after_prime || trail_filter_changed) &&
                m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hit.clearLines();
            if ((clear_hurt_trail_after_prime || trail_filter_changed) &&
                m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hurt.clearLines();
            m_have_trail_game_frame = false;
        }

        if (Horse::ResetOverride::instance().consume_trail_clear_request())
        {
            // Clears persistent lines and restarts cadence so the first
            // post-teleport engine-post tick appends a fresh trail entry.
            clear_persistent_khit_trails();
        }

        uint32_t trail_game_frame = 0;
        const bool have_trail_game_frame =
            read_lux_battle_game_frame(trail_game_frame);

        uint32_t trail_frames_elapsed = 0;
        bool append_persistent_this_tick = true;
        if (have_trail_game_frame)
        {
            if (m_have_trail_game_frame)
            {
                trail_frames_elapsed =
                    trail_game_frame - m_last_trail_game_frame;
                append_persistent_this_tick = trail_frames_elapsed != 0;
            }
            m_last_trail_game_frame = trail_game_frame;
            m_have_trail_game_frame = true;
        }
        else
        {
            m_have_trail_game_frame = false;
        }
        service_khit_sphere_audit_frame(have_trail_game_frame,
                                        trail_game_frame);

        if (trail_frames_elapsed > 0)
        {
            const float game_seconds =
                static_cast<float>(trail_frames_elapsed) / 60.0f;
            m_backend_hit.advanceLifetime(game_seconds);
            m_backend_hurt.advanceLifetime(game_seconds);
        }
        auto trim_persistent_trails = [&](int target_lines) {
            if (target_lines < 0)
                target_lines = 0;
            if (m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hit.trimOldestLines(target_lines);
            if (m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hurt.trimOldestLines(target_lines);
        };
        if (append_persistent_this_tick)
        {
            trim_persistent_trails(
                kKHitPersistentTrailLineBudget -
                kKHitPersistentTrailLineHeadroom);
        }

        m_backend_hit.beginFrame();
        m_backend_hurt.beginFrame();
        m_backend_hit_once.beginFrame();
        m_backend_hurt_once.beginFrame();

        const float T = m_thickness.load();
        void* slot_charas[2] = {
            Horse::KHitWalker::charaSlotFromGlobal(0),
            Horse::KHitWalker::charaSlotFromGlobal(1),
        };
        KHitRenderCalibrationFrame khit_render_calib =
            read_khit_render_calibration_frame(slot_charas);
        update_khit_render_calibration(khit_render_calib);

        Horse::KHitWalker::LaneSnapshot lane_snapshots[2] = {
            Horse::KHitWalker::readLaneSnapshot(slot_charas[0]),
            Horse::KHitWalker::readLaneSnapshot(slot_charas[1]),
        };
        std::vector<Horse::KHitDraw> khit_draws[2];
        khit_draws[0].reserve(96);
        khit_draws[1].reserve(96);

        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* slot_chara = slot_charas[pi];
            if (!slot_chara) continue;

            Horse::KHitWalker::forEachKHit(
                slot_chara,
                pi,
                [&](const Horse::KHitDraw& d) {
                    khit_draws[pi].push_back(d);
                });
        }

        mark_khit_accepted_overlap_candidates(khit_draws);
        if (khit_render_calib.applied)
            apply_render_offset_to_khit_draws(khit_draws,
                                              khit_render_calib.active_offset);

        maybe_log_khit_overlap_pairs(
            khit_draws, lane_snapshots, khit_render_calib,
            have_trail_game_frame, trail_game_frame,
            desired_hit_slot, desired_hurt_slot);
        maybe_log_khit_attack_clusters(
            khit_draws, have_trail_game_frame, trail_game_frame,
            desired_hit_slot);

        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            const int player = static_cast<int>(pi);
            const auto* audit_attacker_lane =
                &lane_snapshots[(pi == 0u) ? 1u : 0u];
            const bool show_hurt =
                shouldShow(player, Horse::KHitList::Hurtbox);
            const bool show_atk =
                shouldShow(player, Horse::KHitList::Attack);
            const bool show_body =
                shouldShow(player, Horse::KHitList::Body);

            // Snapshot the master visibility filter (see
            // m_only_show_active block).  Damage/audit truth still uses
            // canMatterThisFrame(); attack rendering uses a visual-active
            // predicate so hitboxes do not disappear just because they
            // already connected and native re-hit lockout is set.
            const bool only_active = only_active_this_frame;
            if (!show_hurt && !show_atk && !show_body) continue;

            for (const Horse::KHitDraw& d : khit_draws[pi])
            {
                const bool matters_this_frame = canMatterThisFrame(d);
                Horse::LineBatcherSlot renderer_slot =
                    Horse::LineBatcherSlot::Foreground;
                switch (d.list)
                {
                    case Horse::KHitList::Attack:
                        renderer_slot = m_backend_hit.slot();
                        break;
                    case Horse::KHitList::Hurtbox:
                    case Horse::KHitList::Body:
                        renderer_slot = m_backend_hurt.slot();
                        break;
                }
                maybe_log_khit_audit(
                    d, player, matters_this_frame,
                    have_trail_game_frame, trail_game_frame,
                    renderer_slot, audit_attacker_lane);

                // Audit is observability only. Accepted-only overlap stays
                // out of the visual damage highlight; bright red is reserved
                // for a current native hurtbox reaction/damage pulse.
                const bool hurt_damage_highlight =
                    d.list == Horse::KHitList::Hurtbox &&
                    (d.reaction_overlap_this_frame ||
                     d.raw_reaction_hot);
                const bool attack_active_display =
                    canRenderAttackShapeThisFrame(d);
                const bool visible_when_filtered =
                    matters_this_frame ||
                    attack_active_display ||
                    hurt_damage_highlight;
                switch (d.list)
                {
                    case Horse::KHitList::Hurtbox:
                        if (!show_hurt) continue;
                        if (only_active && !visible_when_filtered)
                            continue;
                        break;
                    case Horse::KHitList::Attack:
                        if (!show_atk) continue;
                        if (only_active && !visible_when_filtered)
                            continue;
                        break;
                    case Horse::KHitList::Body:
                        if (!show_body) continue;
                        break;
                }

                const Horse::FLinColor col = colourFor(d, player);
                const bool trail_sample_eligible =
                    matters_this_frame ||
                    attack_active_display ||
                    hurt_damage_highlight;
                Horse::LineBatcherBackend* trail_backend = nullptr;
                Horse::LineBatcherBackend* current_backend = nullptr;
                switch (d.list)
                {
                    case Horse::KHitList::Attack:
                        renderer_slot = m_backend_hit.slot();
                        if (renderer_slot ==
                            Horse::LineBatcherSlot::Persistent)
                        {
                            if (trail_sample_eligible &&
                                append_persistent_this_tick)
                            {
                                trail_backend = &m_backend_hit;
                            }
                            current_backend = &m_backend_hit_once;
                        }
                        else
                        {
                            current_backend = &m_backend_hit;
                        }
                        break;
                    case Horse::KHitList::Hurtbox:
                        renderer_slot = m_backend_hurt.slot();
                        if (renderer_slot ==
                            Horse::LineBatcherSlot::Persistent)
                        {
                            if (trail_sample_eligible &&
                                append_persistent_this_tick)
                            {
                                trail_backend = &m_backend_hurt;
                            }
                            current_backend = &m_backend_hurt_once;
                        }
                        else
                        {
                            current_backend = &m_backend_hurt;
                        }
                        break;
                    case Horse::KHitList::Body:
                        renderer_slot = m_backend_hurt.slot();
                        current_backend =
                            (renderer_slot ==
                                 Horse::LineBatcherSlot::Persistent)
                                ? &m_backend_hurt_once
                                : &m_backend_hurt;
                        break;
                }
                if (trail_backend)
                    Horse::DrawKHitDrawTrailSample(*trail_backend, d, col, T);
                if (current_backend)
                {
                    if (d.list == Horse::KHitList::Hurtbox)
                        Horse::DrawKHitDrawCompact(
                            *current_backend, d, col, T);
                    else
                        Horse::DrawKHitDraw(*current_backend, d, col, T);
                }
            }
        }

        trim_persistent_trails(kKHitPersistentTrailLineBudget);
        m_backend_hit.endFrame();
        m_backend_hurt.endFrame();
        m_backend_hit_once.endFrame();
        m_backend_hurt_once.endFrame();
    }

    // ------------------------------------------------------------------
    // CockpitBase_C::Update pre-hook.  Game thread, one call per frame.
    // ------------------------------------------------------------------
    void on_cockpit_update_pre(UObject* raw_cockpit)
    {
        ++m_update_calls;

        service_presence_transition_safety("cockpit");

        // Drain the ResetOverride deferred-apply queue.  Cheap no-op
        // when no reset is pending.  Must run BEFORE any other tick
        // logic so the user's captured pose is visible to camera /
        // rendering this frame.  See ResetOverride.hpp's "Deferred-
        // apply" plate for why we don't write directly from the
        // reset post-hook.
        Horse::ResetOverride::instance().tick();

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
        // We force-disable these every tick (idempotent calls - if the
        // feature is already off, the disable is a no-op) so even if
        // the user finds a way to flip the underlying atomic via some
        // other code path, the next cockpit tick clamps it back off.
        // The UI side is gated separately (see render_camera_tab and
        // render_time_tab) - both checkboxes go BeginDisabled() while
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
        //     online play - both peers opt in)
        if (Horse::GameMode::instance().should_force_disable_features())
        {
            apply_online_forced_disable();
        }

        // Apply Ansel override first - independent of the overlay F5
        // gate and the NativeBinding-ready gate below, because the user
        // asked for it to be "always" on while the toggle is held.
        apply_ansel_override_if_needed();

        // Camera lock has NO per-frame helper here - it's implemented as
        // a runtime bytepatch (Horse::CamLock) that's flipped on/off
        // from the ImGui toggle.  The patch is a property of the
        // process, not the cockpit tick.

        // VFX suppression: same bytepatch story as camera lock - the
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

        if (!raw_cockpit) return;

        m_stage_visuals.tick(m_hide_stage_visuals.load());

        // KHit/stage line-overlay drawing runs from the engine tick post
        // callback, after the native battle tick has refreshed KHit world
        // buffers.  Cockpit pre-hook remains responsible for controls,
        // weapon visibility, and retrack-event HUD state.

        int charas_seen = 0;

        // ---- Weapon visibility snapshot ---------------------------------
        // Compute once per frame.  `apply_weapons` is true when we need to
        // actively push SetWeaponVisibility into the game this frame:
        //   * when the EFFECTIVE toggle is ON - re-apply every frame to
        //     overwrite any game-driven re-show (the engine can flip
        //     visibility as part of cinematic cues; we fight it back).
        //   * on the EFFECTIVE ON -> OFF transition - call once with
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
        // and the weapons stay VISIBLE - opposite of what the user
        // asked for.
        //
        // Fix: when hide_chara is on, the patch ALREADY hides weapons
        // (CharaInvis patches both +0x533 chara-mesh and +0x534
        // weapon-mesh comparators).  So we suppress our own writes
        // entirely - let the engine's per-move-state writes settle the
        // flag back to 1 (its normal "visible" default) and let the
        // patch invert that to "invisible" the way it's designed to.
        //
        // The transition tracking (last_applied) still runs against
        // the EFFECTIVE state so that toggling hide_chara ON while
        // hide_weapons was previously hiding gets correctly accounted
        // for - we write `true` once on that edge to flip +0x534 back
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
        // for the toggle).  No per-frame UFunction call here - the
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
            // Read chara+0x94 (facing yaw, in [0,1) normalised, 1.0=360-)
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
            // flags - short version: the original flag-pair check fired
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

                // Rising edge: not-retracking ? retracking.  Only push
                // a banner if the user has the overlay enabled - keeps
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

        });

        // Commit the state we actually pushed to the game this frame.
        // Only update last-applied when we had at least one chara to push
        // to; otherwise we'd "lose" the pending transition (e.g. toggle
        // flips OFF between rounds while no chara exists - we'd never
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
        (void)raw_cockpit;
    }

    // ------------------------------------------------------------------
    // Colour scheme (engine-role driven, not size-heuristic)
    //   Hurtboxes - green (receive volumes).  Bright red only for a
    //               current raw-frame reaction/damage candidate.  Sticky
    //               recent-hit memory is muted so accepted-only overlap
    //               cannot masquerade as damage.
    //   Attacks   - amber (strike) / magenta (throw/grab).  Hot (the
    //               currently-active cell) overrides to bright yellow for
    //               strikes or bright pink for throws so you can still see
    //               which one is live.
    //   Body/push - dim blue.  These are not involved in damage.
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
                if (d.reaction_overlap_this_frame ||
                    d.raw_reaction_hot)
                {
                    return Horse::FLinColor{ 1.0f, 0.15f, 0.15f, 1.0f };
                }
                if (d.reaction_hot)
                {
                    return Horse::FLinColor{ 0.70f * player_tint,
                                             0.20f,
                                             0.18f * player_tint, 0.45f };
                }

                // Chara-wide engine-frozen state.  Battle not
                // running, chara incapacitated / dead, or chara in
                // no-react state 6.  Resolver early-returns BEFORE
                // touching this hurtbox's slot, so it cannot fire
                // a reaction regardless of geometry / +0x14 / slot
                // index.  Show as DIM GREY ("authored, but engine
                // is frozen on this chara right now") so the
                // distinction from cyan "classifier ignores this slot"
                // cases is visible.
                //
                // Reached only when the master engine-live
                // filter is OFF - the narrow filter hides these
                // boxes by default.
                if (!d.defender_can_react_engine)
                {
                    return Horse::FLinColor{ 0.45f * player_tint,
                                             0.45f,
                                             0.45f * player_tint, 0.5f };
                }

                // Classifier-ignored hurtbox.  This box may be real
                // geometry, but its slot index is outside the current
                // classifier iteration range, so the damage resolver
                // will not read it this frame.  Many of these are
                // move-script extended-reach / meta hurtboxes, but the
                // user-facing truth is simpler: the classifier ignores
                // this slot right now.
                //
                // Colour them in CYAN tones so the user can see
                // them flip on/off across frames:
                //   bright cyan  = ignored slot AND currently on
                //                  (overlap_active == true)
                //   dim cyan     = ignored slot AND currently off
                //                  (overlap_active == false; only
                //                  visible when the engine-live filter
                //                  is OFF, since the narrow filter
                //                  would otherwise skip them)
                //
                // Detection: classifier_addressable captures `slot < cap`,
                // so `!classifier_addressable` is exactly "resolver will
                // not read this slot".
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

                // Classifier-addressable but +0x14 == 0 - the slot
                // IS in range but the engine's overlap loop will skip
                // this node.  Full-body i-frames get a distinct purple
                // tint; otherwise render dim green for a per-slot
                // disable / armor-style window.
                if (!d.overlap_active)
                {
                    if (d.full_body_invul)
                    {
                        return Horse::FLinColor{ 0.70f * player_tint,
                                                 0.45f,
                                                 0.95f, 0.75f };
                    }
                    return Horse::FLinColor{ 0.20f * player_tint,
                                             0.50f,
                                             0.20f * player_tint, 0.5f };
                }

                // Unified green for normal classifier-addressable
                // hurtbox entries - the engine doesn't sub-
                // categorise these from the defender side.
                return Horse::FLinColor{ 0.25f * player_tint,
                                         0.95f,
                                         0.35f * player_tint, 1.0f };
            }

            case Horse::KHitList::Attack:
            {
                const bool is_throw =
                    (d.attack_role == Horse::KHitAttackRole::Throw);
                const bool visually_active =
                    canRenderAttackShapeThisFrame(d);

                if (d.reaction_overlap_this_frame)
                    return Horse::FLinColor{ 1.0f, 1.0f, 1.0f, 1.0f };

                // Throws keep the pink/magenta scheme - tier doesn't
                // apply to grabs.  Hot vs cold variants only.  When the
                // engine's throw-height gate would reject this throw
                // against the current defender (defender too tall and
                // throw's yarareId not in the unconditional allow-set
                // - see KHitDraw::throw_height_gate_ok), desaturate to
                // grey-ish to signal "boxes overlap but throw dispatch
                // will reject this defender height."  Other stance /
                // transition gates are already accounted for before a
                // throw becomes engine-live; this colour is specifically
                // the late height dispatch rule.
                if (is_throw)
                {
                    const bool gate_fail = !d.throw_height_gate_ok;
                    if (gate_fail)
                    {
                        return visually_active
                            ? Horse::FLinColor{ 0.65f, 0.50f, 0.60f, 0.85f }
                            : Horse::FLinColor{ 0.45f * player_tint,
                                                0.35f,
                                                0.40f * player_tint, 0.45f };
                    }
                    return visually_active
                        ? Horse::FLinColor{ 1.0f, 0.30f, 0.85f, 1.0f }  // hot throw = pink
                        : Horse::FLinColor{ 0.85f * player_tint,
                                            0.15f,
                                            0.70f * player_tint, 0.6f }; // cold throw = magenta
                }

                // Strikes - colour by AttackFlags tier (engine-truth
                // classification of high/mid/low/unblockable from
                // cell+0x32 read in EvaluateMoveTransition + ProcessHit).
                // The data has been on every KHitDraw since the
                // 2026-05-15 audit; this routes it into rendering.
                //
                //   High        - red-orange  (must block standing)
                //   Mid         - amber       (blockable any stance)
                //   Low         - sky-blue    (must block crouching)
                //   Unblockable - magenta-red (must dodge - GI-immune
                //                              when bit 0x200 is set)
                //   Special     - light cyan  (special framing rule)
                //   Unknown     - amber       (fallback to legacy)
                //
                // Hot (live this frame) variants are full saturation;
                // cold variants are tinted by player_tint with 0.6 alpha.
                const Horse::KHitAttackTier tier = d.attack_tier;
                if (visually_active)
                {
                    // Hot strike - pop out from other strikes.
                    switch (tier)
                    {
                        case Horse::KHitAttackTier::High:
                            return Horse::FLinColor{ 1.0f, 0.40f, 0.15f, 1.0f };
                        case Horse::KHitAttackTier::Low:
                            return Horse::FLinColor{ 0.35f, 0.75f, 1.0f, 1.0f };
                        case Horse::KHitAttackTier::Unblockable:
                            return Horse::FLinColor{ 1.0f, 0.10f, 0.55f, 1.0f };
                        case Horse::KHitAttackTier::Special:
                            return Horse::FLinColor{ 0.55f, 1.0f, 0.95f, 1.0f };
                        case Horse::KHitAttackTier::Mid:
                        case Horse::KHitAttackTier::Unknown:
                        default:
                            return Horse::FLinColor{ 1.0f, 1.0f, 0.25f, 1.0f };
                    }
                }
                // Cold strike.
                switch (tier)
                {
                    case Horse::KHitAttackTier::High:
                        return Horse::FLinColor{ 0.95f * player_tint,
                                                 0.30f,
                                                 0.10f, 0.6f };
                    case Horse::KHitAttackTier::Low:
                        return Horse::FLinColor{ 0.20f * player_tint,
                                                 0.55f * player_tint,
                                                 0.85f, 0.6f };
                    case Horse::KHitAttackTier::Unblockable:
                        return Horse::FLinColor{ 0.85f * player_tint,
                                                 0.10f,
                                                 0.45f * player_tint, 0.6f };
                    case Horse::KHitAttackTier::Special:
                        return Horse::FLinColor{ 0.40f * player_tint,
                                                 0.80f * player_tint,
                                                 0.80f, 0.6f };
                    case Horse::KHitAttackTier::Mid:
                    case Horse::KHitAttackTier::Unknown:
                    default:
                        return Horse::FLinColor{ 1.0f * player_tint,
                                                 0.55f * player_tint,
                                                 0.10f, 0.6f };  // legacy amber
                }
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
            s.short_label  = "Online match - features locked";
            s.tooltip_body =
                "Auto disable online: ON. In a Ranked/Casual match - "
                "Lock-cam, Free-fly, Freeze, Slow-mo are locked off.";
        }
        else
        {
            s.colour       = ImVec4{0.30f, 0.90f, 0.40f, 1.0f};
            s.short_label  = "All features available";
            s.tooltip_body =
                "Auto disable online: ON. Scene safe - all features "
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

        // Tooltip on hover - manual hit-test since the square isn't an
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
    // recent event at the top and grows downward - newer entries hide
    // older ones if more fired in a short burst, which is the right
    // visual cue (the latest matters more).
    //
    // Drawing is FOREGROUND so the lines appear above both the game
    // and any ImGui windows.  Costs one std::array sweep + at most
    // kHudTextEventCount AddText calls per frame regardless of what's
    // happening on screen - cheap.
    //
    // The buffer is the shared overlay queue for arbitrary on-screen
    // text events (retrack-event detector, "Hello World" test button,
    // future C++-side diagnostic banners).  We don't gate on the
    // retrack toggle here because the queue is generic - gating
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
    // ImGui panel - single window split into four topical tabs.
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
        // Always-on overlays first - these draw to GetForegroundDrawList
        // unconditionally so they show up regardless of whether the
        // HorseMod window is open / collapsed / hidden via F2.  Anything
        // that needs to appear on top of the game without the user
        // having to interact with our panel goes here.
        // ----------------------------------------------------------------
        draw_hud_text_overlay();

        // -----------------------------------------------------------------
        // Gamepad-first friendliness - ONE-SHOT focus claim on show
        // -----------------------------------------------------------------
        // When the overlay flips hidden ? shown (F2, Back-button, etc.)
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
        //   1. CLICK EATING - `IsWindowFocused(_RootAndChildWindows)`
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
        //   2. STUCK BOOTSTRAP - m_nav_bootstrap_pending was set true
        //      every frame the focus check failed.  If the user was on
        //      a non-Hitboxes tab when the bootstrap fired, the flag
        //      was never consumed (only render_hitboxes_tab clears it).
        //      Then the moment the user navigated to Hitboxes,
        //      SetKeyboardFocusHere() snapped focus onto the F5
        //      checkbox - eating any in-flight click on a different
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

        // Window title carries the package version so users can tell which
        // build is loaded when triaging bug reports.
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
        //   GREY     gating toggle off              - all features available
        //   GREEN    gating on, scene safe          - all features available
        //   RED      gating on, in Ranked / Casual  - 4 features force-disabled
        //   YELLOW   presence not yet resolved      - gate inactive
        //
        // The square is drawn into the WINDOW draw list (clipped to the
        // title bar rect) so it composites correctly with ImGui's own
        // title-bar rendering.  Tooltip uses IsMouseHoveringRect since
        // ImDrawList lines / rects don't go through the normal
        // input-claim path.
        draw_title_bar_status_indicator();

        // 2. L1 / R1 (shoulder) cycle tabs.  Two pieces of state:
        //    - m_current_tab mirrors whichever tab is ACTUALLY showing
        //      (updated by whichever BeginTabItem returns true this
        //      frame).
        //    - requested_tab is a one-frame switch target, consumed
        //      from m_requested_tab so shoulder presses can select the
        //      target tab without racing the renderer.
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
        int requested_tab = m_requested_tab.exchange(
            -1, std::memory_order_relaxed);
        if (!ImGui::IsAnyItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, /*repeat=*/false))
            {
                requested_tab =
                    (m_current_tab + kHorseModTabCount - 1)
                    % kHorseModTabCount;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, /*repeat=*/false))
            {
                requested_tab =
                    (m_current_tab + 1) % kHorseModTabCount;
            }
        }

        if (ImGui::BeginTabBar("##horsemod_tabs"))
        {
            auto tab_item = [&](const char* label, int idx, auto&& body) {
                ImGuiTabItemFlags flags = 0;
                if (requested_tab == idx)
                {
                    flags |= ImGuiTabItemFlags_SetSelected;
                }
                if (ImGui::BeginTabItem(label, nullptr, flags))
                {
                    // Sync "what's actually visible" back to
                    // m_current_tab.  Does NOT touch requested_tab,
                    // so the SetSelected flag still gets applied to
                    // the target tab later in the iteration.
                    m_current_tab = idx;
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

        // Unconditionally clear m_nav_bootstrap_pending at the end of
        // every frame - even if the visible tab wasn't render_hitboxes_-
        // tab and didn't consume it.  Without this clear the flag would
        // be sticky across multiple frames in the "Camera/Time/General
        // tab is visible when the user shows the overlay" case, and
        // would then steal focus the moment the user navigated to the
        // Hitboxes tab (eating any in-flight click).  Clearing here
        // means: bootstrap is best-effort - if you happen to be on the
        // Hitboxes tab when the overlay shows, focus snaps to F5; on
        // any other tab the bootstrap is harmlessly dropped.
        m_nav_bootstrap_pending = false;

        ImGui::End();
    }

    void reset_khit_audit_cadence() noexcept
    {
        m_have_sphere_audit_frame = false;
        m_khit_audit_attack_logs_this_frame = 0;
        m_khit_audit_hurt_logs_this_frame = 0;
        m_khit_audit_pair_logs_this_frame = 0;
        m_khit_audit_calib_logs_this_frame = 0;
        m_khit_audit_cluster_logs_this_frame = 0;
    }

    void render_khit_audit_log_options()
    {
        bool audit = m_khit_sphere_audit.load();
        if (ImGui::Checkbox("KHit audit log", &audit))
        {
            m_khit_sphere_audit.store(audit);
            reset_khit_audit_cadence();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Logs native KHit attack shapes, defender hit-result masks, "
            "and paired attacker/hurtbox geometry to UE4SS.log once per "
            "game frame. Use OverlapPair lines to compare native vs UE "
            "centers for the exact incoming bit the engine accepted. "
            "This does not make extra boxes visible.");

        if (!audit)
            return;

        ImGui::Indent();

        bool filter_move = m_khit_sphere_audit_filter_move.load();
        if (ImGui::Checkbox("Move filter##sphere_audit_move_on",
                            &filter_move))
        {
            m_khit_sphere_audit_filter_move.store(filter_move);
            reset_khit_audit_cadence();
        }
        ImGui::SameLine();
        int move = m_khit_sphere_audit_move.load();
        ImGui::PushItemWidth(100.0f);
        if (ImGui::InputInt("Move id##sphere_audit_move", &move, 0, 0))
        {
            if (move < -1) move = -1;
            if (move > 0xFFFF) move = 0xFFFF;
            m_khit_sphere_audit_move.store(move);
            reset_khit_audit_cadence();
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Matches either the low 11-bit move id or the packed "
            "move value. Use 328 for hdr030_TEST.khd move 328.");

        bool filter_slots = m_khit_sphere_audit_filter_slots.load();
        if (ImGui::Checkbox("Slot filter##sphere_audit_slot_on",
                            &filter_slots))
        {
            m_khit_sphere_audit_filter_slots.store(filter_slots);
            reset_khit_audit_cadence();
        }
        ImGui::SameLine();
        int slot_a = m_khit_sphere_audit_slot_a.load();
        int slot_b = m_khit_sphere_audit_slot_b.load();
        ImGui::PushItemWidth(70.0f);
        if (ImGui::InputInt("A##sphere_audit_slot_a", &slot_a, 0, 0))
        {
            slot_a = std::clamp(slot_a, -1, 63);
            m_khit_sphere_audit_slot_a.store(slot_a);
            reset_khit_audit_cadence();
        }
        ImGui::SameLine();
        if (ImGui::InputInt("B##sphere_audit_slot_b", &slot_b, 0, 0))
        {
            slot_b = std::clamp(slot_b, -1, 63);
            m_khit_sphere_audit_slot_b.store(slot_b);
            reset_khit_audit_cadence();
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "For attack nodes, matches node slot or node+0x08 slot "
            "bit. For hurt-result lines, the full incoming mask is "
            "logged whenever native wrote a nonzero mask, even if "
            "the move filter misses.");

        if (ImGui::Button("Use 328 / all active slots"))
        {
            m_khit_sphere_audit_filter_move.store(true);
            m_khit_sphere_audit_move.store(328);
            m_khit_sphere_audit_filter_slots.store(false);
            m_khit_sphere_audit_slot_a.store(56);
            m_khit_sphere_audit_slot_b.store(57);
            reset_khit_audit_cadence();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Preset for hdr030_TEST.khd move 328. Slot filtering is "
            "disabled so Area/FixArea or slot 21 contributors are "
            "not hidden while debugging the huge edited hitbox.");

        ImGui::Unindent();
    }

    // ==================================================================
    // Hitboxes tab - the core feature.  Master F5 toggle with live
    // status line, per-player move-frame display, KHit list checkboxes
    // (hurt / attack / body for P1 + P2), attack-role filters
    // (strike / throw) and the three engine-derived damage filters,
    // hit-flash duration slider, LineBatcher render options.
    // ==================================================================
    void render_hitboxes_tab()
    {
        // Nav bootstrap - see m_nav_bootstrap_pending doc comment.
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
                hide_khit_overlay_lines();
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
            ImGui::TextDisabled("(setting up - check UE4SS.log if this persists)");
        }
        else if (!m_hook_registered)
        {
            ImGui::TextDisabled("(waiting for a match to start)");
        }
        else if (!m_backend_hit.isReady() || !m_backend_hurt.isReady() ||
                 !m_backend_hit_once.isReady() ||
                 !m_backend_hurt_once.isReady())
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
            if (ImGui::Checkbox("Only boxes that can matter this frame",
                                &only_active))
            {
                m_only_show_active.store(only_active);
                clear_persistent_khit_trails();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Default analysis view.\n\n"
                "Hitboxes: shown only on engine damage frames.\n"
                "Hurtboxes: shown only when the classifier can read the "
                "slot, the overlap gate is on, and the defender can react.\n\n"
                "Turn this off to inspect authored boxes, ignored slots, "
                "partial armor, and full-body i-frame states.");
            ImGui::TextDisabled(
                "Colors: hurt green/red, ignored slot cyan, per-slot off "
                "dim green, full-body i-frames purple, no-react grey; "
                "throws grey when height dispatch rejects.");
        }

        // --- Hit-flash duration -----------------------------------------
        // The raw PerHurtboxReactionState signal is a ~1-frame pulse
        // (~16ms at 60fps) - too short to see.  This slider extends the
        // visible red flash by holding the "hot" state for N GAME FRAMES
        // before fading.  0 = disable the sticky entirely (raw 1-frame
        // pulse only).
        //
        // The drain is keyed on g_LuxBattle_FrameCounter (incremented
        // at the end of LuxBattle_PerFrameTick), so it tracks the same
        // tick the rest of the simulation does:
        //   * Freeze frame ON  ? counter halts ? flash held indefinitely.
        //   * F6 step          ? counter +1   ? flash drains by 1.
        //   * Slow-mo at S-    ? counter advances at S- wall rate, so
        //                        the flash visibly persists 1/S- longer
        //                        in real time (matching the slowed anim).
        //   * Native play      ? counter advances at 60Hz regardless of
        //                        render rate, so 15 frames = 250ms on
        //                        any monitor (60/120/144Hz).
        //
        // 15 frames - 250ms at 60fps; 60 frames - 1 second; the slider
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
            // (depth-tested trail plus a current-frame foreground copy) and
            // Normal (always-on-top, lines clear each frame - clean read
            // of the current state).  The third historical entry "Default"
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
                    "Normal: current frame, always on top. Persistent: "
                    "active hitboxes trail while the current hitbox also "
                    "draws on top each frame.");

            int hurt_idx = static_cast<int>(m_slot_hurt.load());
            if (ImGui::Combo("Hurtbox renderer", &hurt_idx, slot_names, 2))
                m_slot_hurt.store(static_cast<Horse::LineBatcherSlot>(hurt_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Normal: current frame, always on top. Persistent: "
                    "active hurtboxes trail while the current hurtbox also "
                    "draws on top each frame.");

            // Trail length - only meaningful when at least one renderer
            // is set to Persistent.  Hidden otherwise to keep the UI
            // free of inert controls.  Persistent batchers accumulate only
            // engine-live hit/hurt trail samples. Current active boxes,
            // inactive broad-view boxes, and body boxes are routed to
            // one-frame Foreground fallbacks. Dense moves are line-capped
            // and old trail samples are trimmed before the renderer stalls.
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
                    "game frames (60/sec). Lifetime decrements only "
                    "when SC6's game-frame counter advances, so freeze "
                    "holds the trail and F6 step drains one frame. "
                    "Only active hit/hurt boxes enter the trail; current "
                    "attack boxes still redraw detailed once per render "
                    "frame, while hurtboxes and trail samples use compact "
                    "rings. Dense moves auto-trim old trail samples to "
                    "protect FPS.");
            }

        }
    }

    // ==================================================================
    // Camera tab - pose lock (position + rotation group), Free-fly
    // camera (F7) with its sub-controls (move/look/FOV sliders, live
    // pose readout, memory-verify line, input diagnostics), and Ansel
    // always-allowed.  All independent of the F5 hitbox overlay.
    // ==================================================================
    void render_camera_tab()
    {
        // --- Always allow Ansel camera -----------------------------------
        // Runs independent of the F5 hitbox overlay.  Kept at the top of
        // the Camera tab (rather than buried under Free-fly's sub-controls)
        // because it's a single checkbox with no state to inspect - the
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
            "Independent of the F5 overlay - you can use Ansel with\n"
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
            // state machine - letting the user poke the checkbox then
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
            // ONLINE GATE specifically - a strong visual cue that the
            // gate (not a normal "no value" path) is blocking the
            // toggle.  Strikethrough is reserved for the online-gate
            // case so the existing "free-fly owns the lock" disabled
            // state still looks like a regular grey-out.
            if (online_locked) draw_disabled_strikethrough();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                online_locked
                    ? "Disabled - you're in a Ranked or Casual online\n"
                      "match and the General tab's \"Auto disable online\"\n"
                      "toggle is on.\n\n"
                      "Camera locking will be available again when the\n"
                      "match ends, or turn the \"Auto disable online\"\n"
                      "toggle off in the General tab to override (not\n"
                      "recommended for online play)."
                : fc_on
                    ? "Disabled while Free-fly camera is on - free-fly\n"
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
            // It's still useful internally - Free-fly camera enables
            // it automatically while it's active so arrow-key look
            // works - but exposing it as a separate user toggle was
            // confusing.  Free-fly now owns the rotation lock entirely.

            // Status line - friendly summary of whether the camera
            // is currently locked.  Free-fly turning on the lock
            // counts as "active" here so the user sees feedback when
            // free-cam mode is engaged.
            if (!m_cam_lock.is_resolved() && lc)
            {
                ImGui::TextDisabled(
                    "(camera lock couldn't find its hook points - "
                    "see UE4SS.log for details)");
            }
            else if (m_cam_lock.is_enabled() &&
                     m_cam_lock.is_rotation_enabled())
            {
                ImGui::TextDisabled(
                    "(camera fully locked - position + rotation)");
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
            // Nvidia Ansel - the hitbox overlay stays visible because
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
                        "Disabled - you're in a Ranked or Casual online\n"
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
                    "  Shift       5- faster  |  Ctrl  0.2- slower\n"
                    "(If the arrow keys don't respond, use IJKL\n"
                    " instead - the game grabs arrows on some\n"
                    " screens.)\n\n"
                    "Controller (player 1):\n"
                    "  Left stick    move\n"
                    "  Right stick   look\n"
                    "  LT / RT       move down / up\n"
                    "  LB / RB       0.2- / 5- speed\n\n"
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
                        "Hold Shift (or RB) for 5- this speed, Ctrl\n"
                        "(or LB) for 0.2-.");

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

                    // Live pose readout - handy for reproducing shots.
                    ImGui::TextDisabled(
                        "position (%.1f, %.1f, %.1f)  rotation (%.1f, %.1f, %.1f)",
                        m_free_camera.loc_x(),
                        m_free_camera.loc_y(),
                        m_free_camera.loc_z(),
                        m_free_camera.pitch(),
                        m_free_camera.yaw(),
                        m_free_camera.roll());

                    // On-screen memory persistence check - read the
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
                        "(waiting for a match - free-cam needs an "
                        "active camera)");
                }
            }
    }

    // ==================================================================
    // Time tab - Freeze frame (WorldTickGate hard stop), Step 1 / Step N
    // buttons for deterministic frame-stepping under freeze, and the
    // gate-driven Slow-motion slider + preset buttons (0.001x..1.0x).
    // ==================================================================
    void render_time_tab()
    {
        // --- Live move-frame display -------------------------------------
        // Deref chara+0x44068 ActiveLaneStateCursorPtr and show
        // CurrentAnimFrame / AnimLengthFrames for each player.  Costs
        // ~4 safe reads per frame per player - negligible.
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
                            s.primary_entry_script_bracket ? "  [entry]" : "",
                            s.finished      ? "  [done]" :
                            s.at_end        ? "  [end]"  : "");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Live readout of which move this player is in:\n"
                    "  current-frame / total-frames\n"
                    "  move ID (internal hex value)\n"
                    "  lane (which attack slot is active)\n"
                    "  speed (playback multiplier - 1.00x = normal)\n"
                    "  [entry] = primary move-entry setup/script bracket\n"
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

            // --- Freeze frame (WorldTickGate-driven) ------------------
            // frame_step_apply() resolves/enables WorldTickGate and sibling
            // gates on the next cockpit tick. Frame-step adds gate credits;
            // it no longer writes SpeedControl speedval or resolves legacy
            // SpeedControl replay AOBs.
            const bool time_online_locked =
                Horse::GameMode::instance().should_force_disable_features();
            bool ff = m_freeze_frame.load();
            if (time_online_locked) ImGui::BeginDisabled(true);
            if (ImGui::Checkbox("Freeze frame", &ff))
            {
                m_freeze_frame.store(ff);
                // No explicit resolve/enable here - frame_step_apply
                // does the lazy enable on the next cockpit tick when
                // it sees freeze=true.
            }
            if (time_online_locked) ImGui::EndDisabled();
            if (time_online_locked) draw_disabled_strikethrough();
            if (ImGui::IsItemHovered() && time_online_locked)
            {
                ImGui::SetTooltip(
                    "Disabled - you're in a Ranked or Casual online\n"
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
            // engine-state gating needed - the SpeedControl patches
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
                    "Turn on Freeze frame first - stepping only\n"
                    "makes sense when the world is paused.");
            }

            // Status line - "paused" / "stepping" / arming.
            if (const int q = m_step_pending.load(); q > 0)
            {
                ImGui::TextDisabled("(advancing %d more frame%s)",
                                    q, q == 1 ? "" : "s");
            }
            else if (ff)
            {
                ImGui::TextDisabled(
                    m_world_tick_gate.is_resolved()
                        ? "(paused - press F6 to advance one frame)"
                        : "(arming WorldTickGate on next cockpit tick)");
            }

            // --- Speed control (slow-motion / freeze) ------------------
            // Replaces the engine's master delta-time reads with a load
            // from a single user-controlled float.  Independent of the
            // Freeze-frame toggle above - Freeze gates the chara state
            // machine, this gates ALL dt-driven subsystems (animation,
            // hit timing, particles within the MoveVM scope).
            //
            // Slow-motion is implemented as a WorldTickGate cadence.  The
            // checkbox flips desired state; frame_step_apply resolves/enables
            // the actual gates on the next cockpit tick.
            {
                // The whole slow-motion block (checkbox + slider +
                // preset buttons) is locked while the online gate is
                // engaged - disabling just the checkbox would leave
                // the slider/presets clickable, and clicking a preset
                // would still mutate m_speed_value (harmless while locked,
                // but confusing UI).  Wrapping the whole block keeps the
                // visual state honest.
                const bool sm_online_locked =
                    Horse::GameMode::instance().should_force_disable_features();
                if (sm_online_locked) ImGui::BeginDisabled(true);

                bool sc_on = m_speed_enabled.load();
                if (ImGui::Checkbox("Slow-motion", &sc_on))
                {
                    m_speed_enabled.store(sc_on);
                    if (!sc_on && m_speed_control.is_enabled())
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
                // Tooltip - picks the gate-locked text or the normal
                // explanation depending on state.  Only shown while the
                // checkbox is hovered (Slider/Presets get their own
                // tooltips below).
                if (ImGui::IsItemHovered() && sm_online_locked)
                {
                    ImGui::SetTooltip(
                        "Disabled - you're in a Ranked or Casual online\n"
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
                // (this tick is a "go" tick - full game frame) and
                // red (this tick is a "stop" tick - frozen).  Lets
                // the user visually confirm the slider is producing
                // the cadence they expect, especially important at
                // very low speeds (e.g., 0.01x = one go tick every
                // 100 cockpit ticks - 1.6 sec - without this dot
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
                                "GO tick - this cockpit tick is\n"
                                "advancing the game by one full\n"
                                "native-dt frame.";
                            break;
                        case TickKind::Stop:
                            col = ImVec4{0.95f, 0.30f, 0.30f, 1.0f};
                            hover_text =
                                "STOP tick - this cockpit tick is\n"
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

                // Slider controls the WorldTickGate cadence.  We still allow
                // drag while off so the user can pre-set their target value
                // before flipping on.
                //
                // Range capped at 1.0 because the frame-stepped
                // implementation can't tick the world MORE than once
                // per cockpit tick - values >1.0 silently behave as
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

                // Effective rate readout - shown below the slider so
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
                    }
                    ImGui::SameLine();
                }
                ImGui::NewLine();

                if (sm_online_locked) ImGui::EndDisabled();

                if (sc_on)
                {
                    ImGui::TextDisabled(
                        "(gate-driven cadence at %.3fx; SpeedControl idle)",
                        m_speed_value.load());
                }
            }

    }

    static bool read_labbing_live_distance(float& out) noexcept
    {
        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* chara = Horse::KHitWalker::charaSlotFromGlobal(pi);
            float distance = 0.0f;
            if (Horse::KHitWalker::readOpponentDistance(chara, distance))
            {
                out = distance;
                return true;
            }
        }
        return false;
    }

    static void draw_labbing_attack_frame_row(const char* label, int pi)
    {
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
        const bool has_window =
            s.phase != Horse::KHitAttackPhase::None ||
            s.master_window_start != 0 ||
            s.master_window_end != 0;

        if (has_window)
        {
            ImGui::Text(
                "%3d / %3d  move=0x%04X  phase=%s  active=%d-%d%s",
                curI, totI,
                static_cast<uint16_t>(s.packed_move),
                Horse::KHitAttackPhaseName(s.phase),
                static_cast<int>(s.master_window_start),
                static_cast<int>(s.master_window_end),
                s.in_master_window ? "  [live]" : "");
        }
        else
        {
            ImGui::Text(
                "%3d / %3d  move=0x%04X  phase=%s  active=--",
                curI, totI,
                static_cast<uint16_t>(s.packed_move),
                Horse::KHitAttackPhaseName(s.phase));
        }

        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Current move animation frame from the active MoveVM lane.\n"
            "phase is the engine's startup / active / recovery tag.\n"
            "[live] means the strict in-master-window flag is set, so\n"
            "the active frame gate is open after sub-window inhibitors.");
    }

    // ==================================================================
    // Labbing tab - training-mode utilities for practising specific
    // setups: capture a custom reset pose and have the in-game training
    // position-reset bind warp both players back to it.
    // ==================================================================
    void render_labbing_tab()
    {
            // --- Attack animation frame ------------------------------
            // Same engine-truth source as the Time tab, with the
            // attack-window phase included so a raw animation frame
            // is not mistaken for an active hit frame.
            ImGui::TextUnformatted("Attack animation frame");
            draw_labbing_attack_frame_row("P1:", 0);
            draw_labbing_attack_frame_row("P2:", 1);
            ImGui::Separator();

            // --- Reset position override -----------------------------
            // When enabled and the user has captured a pose, our post-
            // hook on TrainingModePositionReset replays the captured
            // (X, Y, Z) for both players after the engine's
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
                    "Snapshot both characters' positions. "
                    "Persistent across restarts.");

                ImGui::SameLine();
                if (ImGui::Button("Clear captured pose"))
                {
                    ro.clear_captured();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Forget the captured pose.");

                // ---- Copy / Paste captured-pose JSON --------------------
                // Compact one-line JSON of the captured pose so users can
                // share setups (Discord, notes) without re-capturing.
                // Format documented in ResetOverride::poses_to_json /
                // poses_from_json.  Only "expected numbers in expected
                // places" validation - does NOT verify the position is
                // legal on the current stage.
                if (ImGui::Button("Copy position"))
                {
                    const std::string js =
                        Horse::ResetOverride::poses_to_json();
                    ImGui::SetClipboardText(js.c_str());
                    m_reset_pose_io_status = "copied to clipboard";
                    m_reset_pose_io_ok     = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Copy both captured poses to the clipboard as JSON.");

                ImGui::SameLine();
                if (ImGui::Button("Paste position"))
                {
                    const char* clip = ImGui::GetClipboardText();
                    if (!clip || !*clip)
                    {
                        m_reset_pose_io_status = "clipboard empty";
                        m_reset_pose_io_ok     = false;
                    }
                    else
                    {
                        std::string err;
                        const bool ok =
                            Horse::ResetOverride::poses_from_json(
                                std::string_view{clip}, err);
                        if (ok)
                        {
                            m_reset_pose_io_status = "pasted OK";
                            m_reset_pose_io_ok     = true;
                            Output::send<LogLevel::Default>(
                                STR("[HorseMod] reset-override pose "
                                    "pasted from clipboard\n"));
                        }
                        else
                        {
                            m_reset_pose_io_status = "paste failed: " + err;
                            m_reset_pose_io_ok     = false;
                            Output::send<LogLevel::Warning>(
                                STR("[HorseMod] reset-override paste "
                                    "rejected: {}\n"),
                                RC::to_generic_string(err));
                        }
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Replace captured poses from JSON in the clipboard. "
                    "P1 / P2 are independent - pasting a P1-only payload "
                    "leaves the existing P2 capture untouched.");

                if (!m_reset_pose_io_status.empty())
                {
                    const ImVec4 colour = m_reset_pose_io_ok
                        ? ImVec4{0.55f, 0.85f, 0.55f, 1.0f}
                        : ImVec4{0.95f, 0.55f, 0.35f, 1.0f};
                    ImGui::TextColored(colour, "%s",
                                       m_reset_pose_io_status.c_str());
                }

                // Per-player readout of what's currently captured.
                for (int pi = 0; pi < 2; ++pi)
                {
                    const auto p = ro.get_pose(pi);
                    if (p.has)
                    {
                        ImGui::TextDisabled(
                            "P%d  pos=(%.1f, %.1f, %.1f)",
                            pi + 1, p.pos_x, p.pos_y, p.pos_z);
                    }
                    else
                    {
                        ImGui::TextDisabled("P%d  not captured yet", pi + 1);
                    }
                }

                float live_distance = 0.0f;
                if (read_labbing_live_distance(live_distance))
                {
                    ImGui::TextDisabled("Live distance  %.2f", live_distance);
                }
                else
                {
                    ImGui::TextDisabled("Live distance  --");
                }

                const bool any_reset_registered = std::any_of(
                    m_reset_slots.begin(), m_reset_slots.end(),
                    [](const ResetHookSlot& s) { return s.registered; });
                if (!any_reset_registered)
                {
                    ImGui::TextDisabled(
                        "(waiting for training-reset hook - start a match)");
                }
            }

    }

    void render_general_tab()
    {
            // ---- Online safety gate (TOP of General - primary control) ----
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
                // indicator's state in plain text - same colour, same
                // tooltip body - so users who prefer reading text over
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

            // --- Stage boundary -----------------------------------------
            // Draws the live LuxBattle frame-bounds geometry used by terrain,
            // edge, and wall logic. This is intentionally independent of the
            // F5 hitbox overlay toggle.
            {
                bool sb = m_show_stage_boundary.load();
                if (ImGui::Checkbox("Stage boundary", &sb))
                    m_show_stage_boundary.store(sb);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Draw exact LuxBattle terrain triangles plus current "
                    "breakable-stage presentation bounds. Orange: ring/wall "
                    "clearance; blue: floor/ceiling; cyan: edge/ring-out; "
                    "purple: point-sampled special terrain; grey: excluded "
                    "scan entries. Unaffected by F5.");
            }

            {
                bool hsv = m_hide_stage_visuals.load();
                if (ImGui::Checkbox("Hide stage visuals", &hsv))
                    m_hide_stage_visuals.store(hsv);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Hide rendered stage meshes so hitboxes and stage "
                    "wireframes are easier to see. Gameplay collision is "
                    "unchanged.");
            }

            ImGui::Separator();

            // --- Hide weapons -------------------------------------------
            // Force hide both charas' weapons so they stop occluding the
            // hitbox overlay.  Calls SetWeaponVisibility(false) every frame
            // while on (so the game's own show-triggers don't sneak weapons
            // back in); calls SetWeaponVisibility(true) once on OFF to
            // restore.  Applies only while the overlay is enabled - if F5
            // turns the mod off, weapons stay in whatever state the engine
            // last set (typically visible).
            //
            // When "Hide characters" is also on, this control is greyed
            // out: CharaInvis already hides both chara mesh AND weapons
            // via a bytepatch that's incompatible with our per-frame
            // SetWeaponVisibility writes (the patch inverts the meaning
            // of the +0x534 weapon-flag, so writing 0 produces "visible"
            // - opposite of what we want).  See the apply-loop comment
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
            // inside ALuxBattleChara_SyncMoveStateVisibility - the
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
                    "(couldn't hook character visibility - see UE4SS.log)");
            }

            // --- Suppress VFX ------------------------------------------
            // Bytepatch port of somberness's CE "VFX off" cheat.
            // Patches the engine's per-slot VFX-state writer to plant a
            // sentinel constant the renderer culls - effects never
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
                    "(couldn't hook the VFX system - see UE4SS.log)");
            }

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Developer##general_developer"))
            {
                ImGui::TextUnformatted("Deterministic simulation");
                ImGui::TextDisabled("Lifecycle: Disabled");
                ImGui::TextDisabled(
                    "Native manifest: %zu qualified regions",
                    Horse::Deterministic::Schema::production_regions.size());
                if (m_replay_native_runtime.ready())
                {
                    ImGui::TextDisabled(
                        "Replay native bridge: signature verified (inactive)");
                }
                else
                {
                    const auto bridge_failure =
                        Horse::Deterministic::failure_code_name(
                            m_replay_native_runtime_status.code);
                    ImGui::TextDisabled(
                        "Replay native bridge: unavailable (%.*s)",
                        static_cast<int>(bridge_failure.size()),
                        bridge_failure.data());
                }
                if (m_deterministic_config.trace)
                {
                    const auto timeline = m_replay_native_runtime.timeline_status();
                    ImGui::TextDisabled(
                        "Frame fencepost: %s (observed=%llu, repeats=%llu, "
                        "generations=%llu, replay exits=%llu)",
                        m_deterministic_hooks.installed() ? "armed" : "unavailable",
                        static_cast<unsigned long long>(
                            m_frame_fencepost_observations.load()),
                        static_cast<unsigned long long>(
                            m_frame_fencepost_repeats.load()),
                        static_cast<unsigned long long>(
                            m_frame_fencepost_generations.load()),
                        static_cast<unsigned long long>(
                            m_replay_exit_observations.load()));
                    ImGui::TextDisabled(
                        "Replay timeline: frames=%llu sessions=%llu generations=%llu "
                        "landing=%llu landing_mib=%.2f entries=%llu entry_mib=%.2f "
                        "round=%d native_time=%d%s",
                        static_cast<unsigned long long>(timeline.captured_frames),
                        static_cast<unsigned long long>(timeline.sessions),
                        static_cast<unsigned long long>(timeline.generations),
                        static_cast<unsigned long long>(timeline.captured_checkpoints),
                        static_cast<double>(timeline.checkpoint_bytes) / (1024.0 * 1024.0),
                        static_cast<unsigned long long>(
                            timeline.captured_batch_entry_checkpoints),
                        static_cast<double>(timeline.batch_entry_checkpoint_bytes)
                            / (1024.0 * 1024.0),
                        timeline.native_round,
                        timeline.native_time,
                        timeline.partial ? " (memory limit reached)" : "");
                    ImGui::TextDisabled(
                        "Native fencepost: repeats=%llu same_time=%llu "
                        "cursor_mismatch=%llu round_state_frame=%u unpause=%d "
                        "pending_move=%u",
                        static_cast<unsigned long long>(timeline.repeat_requests),
                        static_cast<unsigned long long>(
                            timeline.same_native_time_coordinates),
                        static_cast<unsigned long long>(timeline.cursor_mismatches),
                        timeline.round_state_frame,
                        timeline.unpause_countdown,
                        static_cast<unsigned int>(timeline.pending_move_state));
                    ImGui::TextDisabled(
                        "Native batches: total=%llu zero=%llu multi=%llu "
                        "repeat_coords=%llu same_time_coords=%llu max=%u input_delta_max=%u "
                        "accounting_mismatch=%llu",
                        static_cast<unsigned long long>(timeline.native_batches),
                        static_cast<unsigned long long>(
                            timeline.zero_coordinate_batches),
                        static_cast<unsigned long long>(
                            timeline.multi_coordinate_batches),
                        static_cast<unsigned long long>(
                            timeline.batch_repeat_coordinates),
                        static_cast<unsigned long long>(
                            timeline.batch_same_input_time_coordinates),
                        timeline.maximum_coordinates_per_batch,
                        timeline.maximum_input_delta_per_batch,
                        static_cast<unsigned long long>(
                            timeline.batch_frame_accounting_mismatches));
                    ImGui::TextDisabled(
                        "Resim bases: uncovered=%llu entry_gap_max=%llu "
                        "distance_max=%llu",
                        static_cast<unsigned long long>(
                            timeline.coordinates_without_batch_entry_checkpoint),
                        static_cast<unsigned long long>(
                            timeline.maximum_batch_entry_checkpoint_gap),
                        static_cast<unsigned long long>(
                            timeline.maximum_resim_distance_from_batch_entry));
                    if (timeline.checkpoint_failure
                        != Horse::Deterministic::FailureCode::None)
                    {
                        const auto failure = Horse::Deterministic::failure_code_name(
                            timeline.checkpoint_failure);
                        ImGui::TextDisabled(
                            "Candidate checkpoint unavailable: %.*s",
                            static_cast<int>(failure.size()), failure.data());
                    }
                    if (timeline.batch_entry_checkpoint_failure
                        != Horse::Deterministic::FailureCode::None)
                    {
                        const auto failure = Horse::Deterministic::failure_code_name(
                            timeline.batch_entry_checkpoint_failure);
                        ImGui::TextDisabled(
                            "Batch-entry checkpoint unavailable: %.*s",
                            static_cast<int>(failure.size()), failure.data());
                    }
                    const auto probe_failure = m_frame_fencepost_failure.load(
                        std::memory_order_acquire);
                    if (probe_failure != Horse::Deterministic::FailureCode::None)
                    {
                        const auto failure = Horse::Deterministic::failure_code_name(
                            probe_failure);
                        ImGui::TextDisabled(
                            "Frame fencepost failure: %.*s",
                            static_cast<int>(failure.size()), failure.data());
                    }
                }
                ImGui::TextDisabled(
                    "Config: %s (window=%u, delay=%u, trace=%s)",
                    m_deterministic_config_present ? "loaded" : "missing",
                    m_deterministic_config.rollback_window,
                    m_deterministic_config.input_delay,
                    m_deterministic_config.trace ? "true" : "false");
                if (m_deterministic_failure
                    != Horse::Deterministic::FailureCode::None)
                {
                    const auto failure = Horse::Deterministic::failure_code_name(
                        m_deterministic_failure);
                    ImGui::TextDisabled(
                        "Terminal gate: %.*s",
                        static_cast<int>(failure.size()), failure.data());
                }
                ImGui::TextDisabled(
                    "Replay seek is exposed only through the qualification "
                    "fencepost API; online ownership remains fail-closed.");
                render_khit_audit_log_options();
            }

    }
};

// ----------------------------------------------------------------------------
#define HORSE_MOD_API __declspec(dllexport)
extern "C"
{
    HORSE_MOD_API CppUserModBase* start_mod()
    {
        auto* mod = new HorseMod();
        g_horse_mod_instance.store(mod, std::memory_order_release);
        return mod;
    }

    HORSE_MOD_API void uninstall_mod(CppUserModBase* mod)
    {
        auto* expected = static_cast<HorseMod*>(mod);
        (void)g_horse_mod_instance.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel);
        delete mod;
    }

    HORSE_MOD_API bool horsemod_request_replay_seek(
        std::uint64_t target_frame)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && mod->RequestReplaySeek(target_frame);
    }

    HORSE_MOD_API bool horsemod_get_replay_seekable_range(
        std::uint64_t* generation, std::uint64_t* first_frame,
        std::uint64_t* last_frame)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && generation != nullptr && first_frame != nullptr
            && last_frame != nullptr
            && mod->GetReplaySeekableRange(
                *generation, *first_frame, *last_frame);
    }

    HORSE_MOD_API std::uint32_t horsemod_get_replay_seek_status(
        std::uint64_t* target_frame,
        std::uint64_t* source_end_frame,
        std::uint64_t* verified_frames,
        std::uint16_t* failure)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        if (mod == nullptr || target_frame == nullptr
            || source_end_frame == nullptr || verified_frames == nullptr
            || failure == nullptr)
        {
            return 0;
        }
        return mod->GetReplaySeekStatus(*target_frame, *source_end_frame,
            *verified_frames, *failure);
    }

    HORSE_MOD_API bool horsemod_get_replay_simulation_phase(
        std::int32_t* native_round, std::int32_t* native_time,
        std::uint32_t* round_state_frame, std::int32_t* unpause_countdown)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && native_round != nullptr && native_time != nullptr
            && round_state_frame != nullptr && unpause_countdown != nullptr
            && mod->GetReplaySimulationPhase(*native_round, *native_time,
                *round_state_frame, *unpause_countdown);
    }
}
