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
#include "horselib/deterministic/NativeReplaySessionEntry.hpp"
#include "horselib/deterministic/NativeReplayTraceTaskGuard.hpp"
#include "horselib/deterministic/NativeReplayNiagaraObservation.hpp"
#include "horselib/deterministic/NativeReplayVfxCompletionObservation.hpp"
#include "horselib/deterministic/NativeReplayMaterialTaskGuard.hpp"
#include "horselib/deterministic/DeterministicHookSet.hpp"
#include "horselib/deterministic/Sc6ReplayInputSource.hpp"
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

// GamePause.hpp and BattlePauseRequest.hpp were retired in April 2026.
// Their old "audio-only" explanation was disproven during rollback RE:
// provider+0x394 flows through 140438980 to the Unreal PauserPlayerState
// query and virtual world-pause setter. It also affects photography policy.
// Current frame stepping uses the dedicated world/actor/time gates; do not
// restore the old multi-site patches or confuse VM dt scaling with UE pause.

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
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

// ----------------------------------------------------------------------------
#include "horselib/deterministic/ReplayConsumerFailure.hpp"
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
static std::atomic<HMODULE> g_horse_mod_deferred_unload_pin{nullptr};
static std::atomic<bool> g_horse_mod_unload_guard_ready{false};

class HorseMod final : public CppUserModBase
{
private:
    // Static live-instance pointer so the cockpit hook lambda can safely
    // no-op after destruction (game thread could fire after Restart All).
#include "HorseModService.TypesAndState.inl"
#include "HorseModService.KHitAudit.inl"
#include "HorseModService.SettingsAndRequests.inl"
#include "HorseModService.QualificationDiagnostics.inl"
#include "HorseModService.PresentationDiagnostics.inl"
#include "HorseModService.ReplayCallbacks.inl"
#include "HorseModService.PublicApiAndLifetime.inl"
#include "HorseModService.RuntimeControls.inl"
#include "HorseModService.PresenceAndOverlay.inl"
#include "HorseModService.UiTabs.inl"
#include "HorseModService.LabbingAndGeneral.inl"
};

// ----------------------------------------------------------------------------
#define HORSE_MOD_API __declspec(dllexport)
extern "C"
{
    HORSE_MOD_API bool horsemod_start_physics_callback_observation(const wchar_t* path,const char* run)
    {
        return Horse::Deterministic::NativeReplayTraceTaskGuard::StartPhysicsObservation(path,run);
    }
    HORSE_MOD_API bool horsemod_read_physics_callback_observation_status(std::uint64_t* output,std::size_t count)
    {
        return Horse::Deterministic::ReplayPhysicsCallbackObservation::ReadStatus(output,count);
    }
    HORSE_MOD_API bool horsemod_close_physics_callback_observation_phase(std::uint64_t end_tick)
    {
        return Horse::Deterministic::ReplayPhysicsCallbackObservation::ClosePhase(end_tick);
    }
    HORSE_MOD_API bool horsemod_start_vfx_completion_observation(const wchar_t* path,const char* run)
    {
        return Horse::Deterministic::NativeReplayVfxCompletionObservation::Start(path,run);
    }
    HORSE_MOD_API bool horsemod_start_collection18_combat_observation(const wchar_t* path,const char* run)
    {
        return Horse::Deterministic::NativeReplayVfxCompletionObservation::StartCollection18Combat(path,run);
    }
    HORSE_MOD_API std::uint64_t horsemod_arm_collection18_combat_observation(const char* run,
        std::uintptr_t manager,std::uintptr_t player_slot,std::uintptr_t player,std::uint32_t frame,std::uint64_t epoch)
    {
        return Horse::Deterministic::NativeReplayVfxCompletionObservation::ArmCollection18Combat(run,manager,player_slot,player,frame,epoch);
    }
    HORSE_MOD_API void horsemod_invalidate_collection18_combat_observation(const char* run)
    {
        Horse::Deterministic::NativeReplayVfxCompletionObservation::InvalidateCollection18Combat(run);
    }
    HORSE_MOD_API bool horsemod_read_collection18_combat_return(const char* run,std::uint64_t* output,std::size_t count)
    {
        return Horse::Deterministic::NativeReplayVfxCompletionObservation::ReadCollection18CombatReturn(run,output,count);
    }
    HORSE_MOD_API bool horsemod_read_vfx_completion_observation_status(std::uint64_t* output,std::size_t count)
    {
        return Horse::Deterministic::NativeReplayVfxCompletionObservation::ReadStatus(output,count);
    }
    HORSE_MOD_API bool horsemod_close_vfx_completion_observation_phase(std::uint64_t end_tick)
    {
        return Horse::Deterministic::NativeReplayVfxCompletionObservation::ClosePhase(end_tick);
    }
    HORSE_MOD_API bool horsemod_read_collection18_observation_return(std::uint64_t* output,std::size_t count)
    {
        return Horse::Deterministic::NativeReplayVfxCompletionObservation::ReadCollection18Return(output,count);
    }
    HORSE_MOD_API bool horsemod_start_niagara_observation(const wchar_t* path,const char* run)
    {
        return Horse::Deterministic::NativeReplayNiagaraObservation::Start(path,run);
    }
    HORSE_MOD_API bool horsemod_read_niagara_observation_status(std::uint64_t* output,std::size_t count)
    {
        return Horse::Deterministic::ReplayNiagaraObservation::ReadStatus(output,count);
    }
    HORSE_MOD_API bool horsemod_start_consumer_failure_diagnostic(const wchar_t* path,const char* run)
    {
        return Horse::Deterministic::ReplayConsumerFailure::Start(path,run);
    }
    HORSE_MOD_API bool horsemod_configure_replay_memory_budget(std::size_t bytes, bool diagnostic)
    {
        return Horse::Deterministic::Sc6ReplayHost::ConfigureMemoryBudget(bytes, diagnostic);
    }
    HORSE_MOD_API bool horsemod_set_replay_executor_enabled(bool enabled, bool yield_every_tick, void* manager)
    {
        return Horse::Deterministic::DeterministicHookSet::SetReplayExecutorEnabled(enabled, yield_every_tick, manager);
    }
    HORSE_MOD_API bool horsemod_get_replay_executor_status(std::uint64_t* values, std::size_t count)
    {
        return Horse::Deterministic::DeterministicHookSet::ReadReplayExecutorStatus(values, count);
    }
    HORSE_MOD_API bool horsemod_set_replay_world_paused(bool paused)
    {
        return Horse::Deterministic::DeterministicHookSet::SetReplayWorldPaused(paused);
    }
    HORSE_MOD_API bool horsemod_arm_replay_application_pause(std::uint64_t tick, void* context,
        Horse::Deterministic::Sc6ReplayHost::InteriorObserver observer)
    {
        return Horse::Deterministic::DeterministicHookSet::ArmReplayApplicationPause(tick, context, observer);
    }
    HORSE_MOD_API bool horsemod_arm_consumer_task_probe(const Horse::Deterministic::Sc6ReplayHost::Checkpoint* image,
        std::uint64_t tick, std::uint32_t hold_ms)
    {
        return Horse::Deterministic::Sc6ReplayHost::ArmConsumerTaskProbe(image,tick,hold_ms);
    }
    HORSE_MOD_API bool horsemod_read_consumer_mutation_candidate(void* source_task,void** mesh,void** source)
    {
        return Horse::Deterministic::Sc6ReplayHost::ReadConsumerMutationCandidate(source_task,mesh,source);
    }
    HORSE_MOD_API bool horsemod_particle_copy_experiment(Horse::Deterministic::Sc6ReplayHost::ParticleCopyAction action,
        Horse::Deterministic::Sc6ReplayParticleCopy::Witness* witness, bool* command_pending)
    {
        return Horse::Deterministic::Sc6ReplayHost::ParticleCopyExperiment(action, witness, command_pending);
    }
    HORSE_MOD_API bool horsemod_arm_replay_interior_pause(std::uint64_t tick, void* context,
        Horse::Deterministic::Sc6ReplayHost::InteriorObserver observer)
    {
        return Horse::Deterministic::DeterministicHookSet::ArmReplayInteriorPause(tick, context, observer);
    }
    HORSE_MOD_API bool horsemod_bind_particle_birth(const Horse::Deterministic::Sc6ReplayVfxState::ParticleBirth* birth,
        std::size_t budget)
    {
        return birth && Horse::Deterministic::Sc6ReplayHost::BindParticleBirth(*birth, budget);
    }
    HORSE_MOD_API bool horsemod_get_replay_interior_witness(Horse::Deterministic::Sc6ReplayHost::InteriorWitness* witness)
    {
        return Horse::Deterministic::DeterministicHookSet::ReadReplayInteriorWitness(witness);
    }
    HORSE_MOD_API std::uint16_t horsemod_resume_replay_execution()
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::DeterministicHookSet::ResumeReplayExecution().code);
    }
    HORSE_MOD_API bool horsemod_get_replay_host_status(std::uint64_t* values, std::size_t count)
    {
        return Horse::Deterministic::DeterministicHookSet::ReadReplayHostStatus(values, count);
    }
    HORSE_MOD_API bool horsemod_set_replay_output_observer(void* owner,
        Horse::GameImGui::PresentHook::ReplayOutputObserver callback)
    {
        return Horse::GameImGui::PresentHook::instance().set_replay_output_observer(owner,callback);
    }
    HORSE_MOD_API bool horsemod_replay_capture_operation(
        Horse::Deterministic::Sc6ReplayHost::CaptureAction action,
        Horse::Deterministic::Sc6ReplayHost::CaptureWitness* witness,
        Horse::Deterministic::Sc6ReplayHost::CheckpointHandle* output)
    {
        return Horse::Deterministic::Sc6ReplayHost::CaptureOperation(action,witness,output);
    }
    HORSE_MOD_API std::uint16_t horsemod_capture_replay_checkpoint(Horse::Deterministic::Sc6ReplayHost::Checkpoint* checkpoint)
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::DeterministicHookSet::CaptureReplayCheckpoint(checkpoint).code);
    }
    // Read-only diagnostic boundary. Lease callback tables belong to this DLL;
    // consumers must not validate our capture through another compiled copy.
    HORSE_MOD_API bool horsemod_validate_replay_vfx_capture(std::uintptr_t base,void* battle,
        const Horse::Deterministic::Sc6ReplayVfxState* capture)
    {
        return capture && capture->ValidateHeld(base,battle).ok();
    }
    HORSE_MOD_API std::uint16_t horsemod_restore_replay_checkpoint(const Horse::Deterministic::Sc6ReplayHost::Checkpoint* checkpoint)
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::DeterministicHookSet::RestoreReplayCheckpoint(checkpoint).code);
    }
    HORSE_MOD_API bool horsemod_replay_restore_operation(
        Horse::Deterministic::Sc6ReplayHost::RestoreOperationAction action,
        const Horse::Deterministic::Sc6ReplayHost::Checkpoint* target,
        Horse::Deterministic::Sc6ReplayHost::RestoreOperationWitness* witness)
    {
        return Horse::Deterministic::Sc6ReplayHost::RestoreOperation(action, target, witness);
    }
    HORSE_MOD_API std::uint16_t horsemod_revise_replay_inputs(const Horse::Deterministic::ReplayInputOverride* edits,
        std::size_t count,std::uint64_t expected,std::uint64_t* revision)
    {
        if(!revision || !edits || !count || count>65536) return static_cast<std::uint16_t>(Horse::Deterministic::FailureCode::IllegalTransition);
        return static_cast<std::uint16_t>(Horse::Deterministic::DeterministicHookSet::ReviseReplayInputs({edits,count},expected,*revision).code);
    }
    HORSE_MOD_API bool horsemod_read_replay_tick(Horse::Deterministic::Sc6ReplayHost::TickAdvanceWitness* witness)
    {
        if (!witness) return false;
        *witness = Horse::Deterministic::DeterministicHookSet::ReadReplayTick();
        return witness->phase != Horse::Deterministic::Sc6ReplayHost::TickAdvancePhase::Failed;
    }
    HORSE_MOD_API bool horsemod_request_replay_tick(std::uint64_t tick, void* context,
        Horse::Deterministic::Sc6ReplayHost::InteriorObserver observer,
        Horse::Deterministic::Sc6ReplayHost::TickAdvanceWitness* witness)
    {
        if (!witness) return false;
        *witness = Horse::Deterministic::DeterministicHookSet::RequestReplayTick(tick, context, observer);
        return witness->phase != Horse::Deterministic::Sc6ReplayHost::TickAdvancePhase::Failed;
    }
    HORSE_MOD_API bool horsemod_set_replay_pause_monitor(void* context,
        Horse::Deterministic::Sc6ReplayHost::PauseMonitor monitor)
    {
        return Horse::Deterministic::Sc6ReplayHost::SetPauseMonitor(context,monitor);
    }
    HORSE_MOD_API bool horsemod_complete_replay_application(void* context,
        Horse::Deterministic::Sc6ReplayHost::InteriorObserver observer,
        Horse::Deterministic::Sc6ReplayHost::TickAdvanceWitness* witness)
    {
        if(!witness) return false;
        *witness=Horse::Deterministic::Sc6ReplayHost::CompleteApplicationToHold(context,observer);
        return witness->phase!=Horse::Deterministic::Sc6ReplayHost::TickAdvancePhase::Failed;
    }
    HORSE_MOD_API bool horsemod_replay_seek_operation(Horse::Deterministic::Sc6ReplayHost::SeekAction action,
        const Horse::Deterministic::Sc6ReplayHost::Checkpoint* checkpoint,std::uint64_t tick,
        Horse::Deterministic::Sc6ReplayHost::SeekWitness* witness,void* context,
        Horse::Deterministic::Sc6ReplayHost::SeekObserver observer)
    {
        return Horse::Deterministic::Sc6ReplayHost::SeekOperation(action,checkpoint,tick,witness,context,observer);
    }
    HORSE_MOD_API bool horsemod_replay_rolling_operation(Horse::Deterministic::Sc6ReplayHost::RollingAction action,
        const Horse::Deterministic::Sc6ReplayHost::Checkpoint* first,std::uint64_t cycles,
        Horse::Deterministic::Sc6ReplayHost::RollingWitness* witness)
    {
        return Horse::Deterministic::Sc6ReplayHost::RollingOperation(action,first,cycles,witness);
    }
    HORSE_MOD_API bool horsemod_begin_scheduled_rolling(std::uint32_t protocol,
        const Horse::Deterministic::Sc6ReplayHost::Checkpoint* first,std::uint64_t cycles,
        const Horse::Deterministic::ReplayCorrectionRequest* corrections,std::size_t count,
        Horse::Deterministic::Sc6ReplayHost::RollingWitness* witness)
    {
        if(!corrections || !count || count>Horse::Deterministic::ReplayCorrectionSchedule::maximum_corrections)return false;
        return Horse::Deterministic::Sc6ReplayHost::BeginRollingSchedule(protocol,first,cycles,{corrections,count},witness);
    }
    HORSE_MOD_API bool horsemod_probe_ground_motion(std::uint32_t protocol,std::uint64_t expected_tick,
        Horse::Deterministic::Sc6ReplayGroundDebrisState::MotionProbeWitness* witness)
    {
        return Horse::Deterministic::Sc6ReplayHost::ProbeGroundMotion(protocol,expected_tick,witness);
    }
    HORSE_MOD_API std::uint16_t horsemod_advance_replay_to_tick(std::uint64_t tick)
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::DeterministicHookSet::AdvanceReplayToTick(tick).code);
    }
    HORSE_MOD_API bool horsemod_replay_index_operation(Horse::Deterministic::Sc6ReplayHost::IndexAction action,
        Horse::Deterministic::ReplayTickIndex::Witness* witness, std::size_t budget)
    {
        return Horse::Deterministic::Sc6ReplayHost::IndexOperation(action, witness, budget);
    }
    HORSE_MOD_API bool horsemod_replay_index_entry(std::uint64_t tick, Horse::Deterministic::ReplayTickIndex::Entry* entry)
    {
        return Horse::Deterministic::Sc6ReplayHost::ReadIndexEntry(tick, entry);
    }
    HORSE_MOD_API std::uint16_t horsemod_request_index_checkpoint(std::uint64_t held_tick)
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::Sc6ReplayHost::RequestIndexCheckpoint(held_tick).code);
    }
    HORSE_MOD_API bool horsemod_read_index_checkpoint(Horse::Deterministic::Sc6ReplayHost::IndexCheckpointWitness* witness)
    {
        return Horse::Deterministic::Sc6ReplayHost::ReadIndexCheckpoint(witness);
    }
    HORSE_MOD_API std::uint16_t horsemod_request_indexed_replay_seek(std::uint64_t tick)
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::Sc6ReplayHost::RequestIndexedSeek(tick).code);
    }
    HORSE_MOD_API std::uint16_t horsemod_replay_checkpoint_ownership(
        Horse::Deterministic::Sc6ReplayHost::CheckpointOwnership action,
        const Horse::Deterministic::Sc6ReplayHost::Checkpoint* checkpoint,std::size_t* retained_count)
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::Sc6ReplayHost::OwnCheckpoint(action,checkpoint,retained_count).code);
    }
    HORSE_MOD_API std::uint16_t horsemod_restore_replay_checkpoint_cancellable(
        const Horse::Deterministic::Sc6ReplayHost::Checkpoint* checkpoint,
        const Horse::Deterministic::Sc6ReplayHost::RestoreControl* control)
    {
        return static_cast<std::uint16_t>(Horse::Deterministic::DeterministicHookSet::RestoreReplayCheckpoint(checkpoint, control).code);
    }
    HORSE_MOD_API CppUserModBase* start_mod()
    {
        if (auto* existing = g_horse_mod_instance.load(
                std::memory_order_acquire))
            return existing;
        HMODULE pin{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&start_mod), &pin))
        {
            HMODULE empty{};
            if (!g_horse_mod_deferred_unload_pin.compare_exchange_strong(
                    empty, pin, std::memory_order_acq_rel))
                FreeLibrary(pin);
            g_horse_mod_unload_guard_ready.store(true,
                std::memory_order_release);
        }
        else
        {
            g_horse_mod_unload_guard_ready.store(false,
                std::memory_order_release);
        }
        auto* mod = new HorseMod();
        g_horse_mod_instance.store(mod, std::memory_order_release);
        return mod;
    }

#include "HorseModService.Uninstall.inl"

    HORSE_MOD_API bool horsemod_request_replay_seek(
        std::uint64_t target_frame)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && mod->RequestReplaySeek(target_frame);
    }

    HORSE_MOD_API bool horsemod_set_replay_history_capture_required(
        bool required)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->SetReplayHistoryCaptureRequired(required);
    }

    HORSE_MOD_API bool horsemod_capture_replay_qualification_terminal_evidence()
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->CaptureReplayQualificationTerminalEvidence();
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

    HORSE_MOD_API bool horsemod_get_replay_seek_metrics(
        std::uint64_t* validation_ns,
        std::uint64_t* resimulation_coordinates)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && validation_ns != nullptr
            && resimulation_coordinates != nullptr
            && mod->GetReplaySeekMetrics(
                *validation_ns, *resimulation_coordinates);
    }

    HORSE_MOD_API bool horsemod_get_replay_canonical_state(
        std::uint64_t* generation, std::uint64_t* frame,
        std::byte* hash, std::size_t hash_size)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && generation != nullptr && frame != nullptr
            && mod->GetReplayCanonicalState(
                *generation, *frame, hash, hash_size);
    }

    HORSE_MOD_API bool horsemod_get_replay_canonical_components(
        std::uint64_t* components, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->GetReplayCanonicalComponents(components, count);
    }

    HORSE_MOD_API bool horsemod_get_replay_presentation_coverage(
        std::uint64_t* counts, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->GetReplayPresentationCoverage(counts, count);
    }

    HORSE_MOD_API bool horsemod_get_replay_presentation_identity(
        std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->GetReplayPresentationIdentity(values, count);
    }

    HORSE_MOD_API bool horsemod_get_replay_audio_batch_identity(
        std::size_t batch_index, std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->GetReplayAudioBatchIdentity(batch_index, values, count);
    }

    HORSE_MOD_API bool horsemod_get_replay_audio_dispatch_identity(
        std::size_t batch_index, std::size_t dispatch_index,
        std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && mod->GetReplayAudioDispatchIdentity(
            batch_index, dispatch_index, values, count);
    }

    HORSE_MOD_API bool horsemod_get_replay_audio_terminal_identity(
        std::size_t batch_index, std::size_t terminal_index,
        std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && mod->GetReplayAudioTerminalIdentity(
            batch_index, terminal_index, values, count);
    }

    HORSE_MOD_API bool horsemod_get_replay_qualification_health(
        std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->GetReplayQualificationHealth(values, count);
    }

    HORSE_MOD_API bool horsemod_reset_replay_qualification_health()
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && mod->ResetReplayQualificationHealth();
    }

    HORSE_MOD_API bool horsemod_get_replay_gameplay_rng_coverage(
        std::uint64_t* counts, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->GetReplayGameplayRngCoverage(counts, count);
    }

    HORSE_MOD_API bool horsemod_get_qualification_clock_v1(
        std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && mod->GetQualificationClock(values, count);
    }

    HORSE_MOD_API bool horsemod_request_qualification_stage_terminal(
        std::uint32_t operation)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr
            && mod->RequestQualificationStageTerminal(operation);
    }

    HORSE_MOD_API std::uint32_t
    horsemod_get_qualification_stage_terminal_status(std::uint32_t* frame)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && frame != nullptr
            ? mod->GetQualificationStageTerminalStatus(*frame) : 0;
    }

    HORSE_MOD_API std::uint32_t horsemod_get_forced_qualification_status()
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr ? mod->GetForcedQualificationStatus() : 0;
    }

    HORSE_MOD_API bool horsemod_arm_replay_qualification_cycle_v1(
        const char* run_id, std::size_t run_id_size,
        std::uint32_t depth, std::uint32_t location)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && run_id != nullptr
            && mod->ArmReplayQualificationCycle(
                std::string_view(run_id, run_id_size), depth, location);
    }

    HORSE_MOD_API bool horsemod_arm_replay_qualification_group_v1(
        const char* run_id, std::size_t run_id_size,
        std::uint32_t location, std::uint32_t anchors,
        std::uint32_t repeats_per_anchor)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && run_id != nullptr
            && mod->ArmReplayQualificationGroup(
                std::string_view(run_id, run_id_size), location, anchors,
                repeats_per_anchor);
    }

    HORSE_MOD_API std::uint32_t
    horsemod_get_replay_qualification_group_row_report_v1(
        const char* run_id, std::size_t run_id_size,
        std::uint32_t row_index, std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && run_id != nullptr
            ? mod->GetReplayQualificationGroupRowReport(
                std::string_view(run_id, run_id_size), row_index,
                values, count) : 0;
    }

    HORSE_MOD_API std::uint32_t
    horsemod_get_replay_qualification_cycle_report_v1(
        const char* run_id, std::size_t run_id_size,
        std::uint64_t* values, std::size_t count)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && run_id != nullptr
            ? mod->GetReplayQualificationCycleReport(
                std::string_view(run_id, run_id_size), values, count) : 0;
    }

    HORSE_MOD_API bool horsemod_disarm_replay_qualification_cycle_v1(
        const char* run_id, std::size_t run_id_size)
    {
        auto* mod = g_horse_mod_instance.load(std::memory_order_acquire);
        return mod != nullptr && run_id != nullptr
            && mod->DisarmReplayQualificationCycle(
                std::string_view(run_id, run_id_size));
    }

}
