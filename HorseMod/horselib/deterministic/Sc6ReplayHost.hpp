#pragma once

#include <cstdint>
#include <memory>
#include <atomic>
#include <chrono>
#include <span>
#include <optional>
#include "ReplaySeekOwnership.hpp"
#include "Sc6ReplayWorld.hpp"
#include "Sc6ReplayExecutor.hpp"
#include "ReplaySourceState.hpp"
#include "ReplayRollingCorrections.hpp"
#include "ReplayRollingTelemetry.hpp"
#include "ReplayStatePolicy.hpp"
#include "ReplayTickIndex.hpp"
#include "ReplayRenderState.hpp"
#include "UcrtRandBroker.hpp"
#include "Sc6ReplayWorldState.hpp"
#include "Sc6ReplayVfxState.hpp"
#include "Sc6ReplaySchedulerState.hpp"
#include "Sc6ReplayParticleCopy.hpp"
#include "Sc6ReplayGroundDebrisState.hpp"

namespace RC::Unreal { class UEngine; }
namespace PLH { class x64Detour; }

namespace Horse::Deterministic
{
class Sc6CandidateCheckpointCapture;
class NativeWindConstruction;
class NativeReplayRendering;
class NativeReplayWidgetClock;
class NativeReplayCpuEmitterLifetime;
class Sc6ReplayParticleConfiguration;
class NativeReplayCallbackAdmission;
class Sc6ReplayInputSource;
struct ReplayInputOverride;
struct ReplayInputRevision;
class Sc6ReplayHudState;
class Sc6ReplayTraceState;
class Sc6ReplayVfxHandlerState;
class Sc6ReplaySourceRegistrationState;
class Sc6ReplayGroundDebrisState;
// Owns engine/world progression between UE4SS's existing engine callbacks.
// Requests remain game-thread only, outside the active execution body.
class Sc6ReplayHost final
{
public:
    Sc6ReplayHost();
    ~Sc6ReplayHost();
    bool Bind(std::uintptr_t image_base, void* manager, Sc6ReplayExecutor* simulation, bool yield_each_tick,
        UcrtRandBroker* broker = nullptr, void* storage_context = nullptr,
        std::size_t (*companion_storage)(void*) noexcept = nullptr) noexcept;
    // Diagnostic selection is admitted only before any checkpoint/transaction or tick.
    // The production default is unchanged unless this explicit call succeeds.
    static bool ConfigureMemoryBudget(std::size_t bytes, bool diagnostic) noexcept;
    bool Pause(bool paused) noexcept;
    Status Resume() noexcept;
    enum class ParticleCopyAction : std::uint8_t { Read, Begin, VerifyAtB, Poll, Cancel, Finish,
        PrepareUndo, InstallCaptured, RestoreUndo, CheckUncommittedRelease, PrepareBirthRegistration, InstallState,
        CommitState, CompleteRetirement, VerifyState, BeginWithoutReadbacks, RetireInFlight, SealCapture, RetireCaptures, ReopenCaptureAtB, PrepareRenderTarget,
        PrepareExecutionCoordinates, BeginExecution, DrainExecutionWork, SettleExecution, ContinueExecutionForUndo, CompleteNativeReconstruction, DrainPrivateOwnerRetirement, CompleteParticleRenderOwners };
    // Bounded diagnostic, independent from checkpoint installation. Read is
    // owner-thread only; other operations use ordered render/RHI dispatch.
    static bool ParticleCopyExperiment(ParticleCopyAction action,
        Sc6ReplayParticleCopy::Witness* output, bool* command_pending) noexcept;
    static bool BindParticleBirth(const Sc6ReplayVfxState::ParticleBirth& birth, std::size_t budget) noexcept;
    static bool BindParticleBirths(const Sc6ReplayVfxState::ParticleBirthSet& births, std::size_t budget) noexcept;
    bool CheckBinding() noexcept;
    bool Stop() noexcept;
    // Stop may accept deferred application-tail cleanup. Destruction requires
    // the later, fully detached state, not only that accepted request.
    bool StopForDestruction() noexcept;
    // Native scene stop is a parameterless asynchronous request. Delay its
    // publication until B recovery, retained-owner retirement and detachment.
    enum class SessionExitPhase : std::uint8_t { Idle, Requested, Recovering, Retiring, ReleasingHold, Detaching, Complete, Failed };
    struct SessionExitWitness {
        SessionExitPhase phase{}; FailureCode failure{};
        std::uint64_t requested_tick{}, recovered_tick{};
    };
    static bool ReadSessionExit(SessionExitWitness*) noexcept;
    enum class InteriorPhase : std::uint64_t { Idle, Arming, Armed, Holding, Releasing, Resumed, Failed };
    enum class PauseBoundary : std::uint8_t { SimulationTick, CompletedApplication, NextCompletedApplication };
    struct InteriorWitness
    {
        InteriorPhase phase{};
        std::uint64_t tick{}, epoch{}, application_updates{}, surface_frames{}, surface_bytes{};
        const void* pending_task{};
        const void* world{};
        bool surface_pending{};
        std::uint64_t ui_resume_requests{};
        PauseBoundary boundary{};
        std::uint64_t completed_applications{};
        bool application_idle{}, engine_idle{}, world_idle{}, arena_empty{};
        std::uint64_t ui_step_requests{};
        std::uint64_t ui_cancel_requests{};
    };
    using InteriorObserver = bool (*)(void*, const InteriorWitness&);
    // Notification only: unlike the owning observer, this subscription cannot
    // decide whether UI intent is consumed. It survives an adjacent UI step.
    using PauseMonitor = void (*)(void*, const InteriorWitness&);
    static bool SetPauseMonitor(void* context, PauseMonitor monitor) noexcept;
    bool ArmInteriorPause(std::uint64_t tick, void* context, InteriorObserver observer) noexcept;
    bool ArmApplicationPause(std::uint64_t tick, void* context, InteriorObserver observer) noexcept;
    InteriorWitness ReadInteriorWitness() const noexcept;
    struct SurfaceSnapshot;
    struct Checkpoint : std::enable_shared_from_this<Checkpoint>
    {
        ReplayStatePolicy::Stamp state_policy{};
        Snapshot gameplay;
        Sc6ReplayExecutor::Continuation execution{};
        ReplaySourceState source{};
        std::shared_ptr<const ReplayInputRevision> input_revision;
        UcrtRandBrokerImage ucrt{};
        ReplayRenderState rendering{};
        Sc6ReplayWorldState world;
        std::shared_ptr<Sc6ReplayHudState> hud;
        std::shared_ptr<Sc6ReplayTraceState> traces;
        std::shared_ptr<Sc6ReplayVfxHandlerState> vfx_handler;
        std::shared_ptr<Sc6ReplaySourceRegistrationState> source_registration;
        std::shared_ptr<Sc6ReplayGroundDebrisState> ground;
        Sc6ReplayWorldState::PhysicsBoundary physics;
        Sc6ReplaySchedulerState scheduler;
        Sc6ReplayVfxState vfx;
        std::vector<std::shared_ptr<const Sc6ReplayParticleConfiguration>> particle_configurations;
        // Process-local pre-overlay image, retained by the ordered render
        // owner. This is presentation only, never a simulation input.
        std::shared_ptr<const SurfaceSnapshot> surface;
        Sc6ReplayParticleCopy::CaptureHandle gpu;
        PauseBoundary boundary{PauseBoundary::SimulationTick};
        // Separate process-local leases. Initial restore support deliberately
        // rejects an expired world/task epoch instead of copying dead pointers.
        std::uint64_t session{}, epoch{};
        std::array<std::uint64_t,3> callback_stamp{};
        const void* task{};
        const void* event{};
        bool valid{};
        std::size_t owned_bytes() const noexcept;
        // Reject known unsupported captured shapes before selecting a target.
        // This does not replace live lifetime/membership transaction preflight.
        bool historical_target_shape_supported() const noexcept;
        bool rolling_display_omitted{}; // Rolling simulation checkpoint; no historical screenshot.
    };
    Status Capture(Checkpoint& output) noexcept;
    using CheckpointHandle = std::shared_ptr<const Checkpoint>;
    // Bounded ownership diagnostic. This reports suspension, never B recovery.
    static bool ArmConsumerTaskProbe(const Checkpoint*, std::uint64_t tick, std::uint32_t hold_ms) noexcept;
    static bool ReadConsumerMutationCandidate(void* source_task,void** mesh,void** source) noexcept;
    enum class RollingPhase : std::uint8_t { Idle, Advancing, Capturing, Ready, Seeking, Retiring, Complete, Failed };
    enum class RollingAction : std::uint8_t { Begin, Read, Release };
    struct RollingWitness {
        RollingPhase phase{}; FailureCode failure{};
        std::uint64_t first{}, current{}, target{}, cycles{}, requested{}, resimulated_ticks{}, peak_bytes{}, resimulated_intervals{};
        std::uint64_t source_revision{},checkpoint_generation{1},first_affected_tick{},corrections_admitted{},corrections_committed{},corrections_recovered{};
    };
    // Bounded local every-tick driver. The existing transaction owns all
    // publication, native execution, complete-B undo and GPU retirement.
    static bool RollingOperation(RollingAction,const Checkpoint*,std::uint64_t cycles,RollingWitness*) noexcept;
    static constexpr std::uint32_t rolling_schedule_protocol=1;
    static bool ProbeGroundMotion(std::uint32_t protocol,std::uint64_t expected_tick,
        Sc6ReplayGroundDebrisState::MotionProbeWitness*) noexcept;
    static bool BeginRollingSchedule(std::uint32_t protocol,const Checkpoint*,std::uint64_t cycles,
        std::span<const ReplayCorrectionRequest>,RollingWitness*) noexcept;
    // Process-local indexing observation, never part of restored simulation.
    // Positive samples are only an ownership filter, not restore admission.
    struct IndexCheckpointHealth {
        enum class Phase : std::uint8_t { Unchecked, ValidAtLastCheck, Deferred, Invalid };
        std::weak_ptr<const void> owner;
        Phase phase{};
        std::uint64_t session{},captured_tick{},last_check{},last_valid{},first_invalid{},next_check{};
        std::uint32_t valid_samples{},deferred_samples{};
        FailureCode failure{};
    };
    Status Capture(CheckpointHandle& output) noexcept;
    enum class CapturePhase : std::uint8_t { Idle, Copying, Sealing, Ready, Retiring, Cancelled, Failed, Preparing, Finishing };
    enum class CaptureAction : std::uint8_t { Begin, Read, Cancel, Take, Release, CheckTargetEligibility };
    struct CaptureWitness {
        CapturePhase phase{};
        FailureCode failure{};
        std::uint64_t tick{}, owned_bytes{}, elapsed_us{};
        bool pending{};
    };
    // Begin owns copy submission/completion and CPU capture across application
    // updates. Take publishes an immutable handle only after complete capture.
    static bool CaptureOperation(CaptureAction action,CaptureWitness* witness,
        CheckpointHandle* output=nullptr) noexcept;
    enum class RestorePhase : std::uint8_t { BeforeWrites, BeforeCommit };
    struct RestoreControl
    {
        // Owner-thread query only: no simulation mutation or nested core calls.
        // A request before commit rolls back the complete enclosing transaction.
        bool (*cancel_requested)(void*, RestorePhase) noexcept{};
        void* context{};
    };
    Status Restore(const Checkpoint& target, const RestoreControl* control = nullptr) noexcept;
    enum class RestoreOperationPhase : std::uint8_t { Empty, Prepared, Publishing, Held, Recovering, Recovered, Committing, Committed, Failed, Preparing };
    struct RestoreOperationWitness
    {
        RestoreOperationPhase phase{};
        FailureCode failure{};
        const char* participant{};
        std::uint64_t target_tick{}, original_tick{}, owned_bytes{};
        bool pending{}, original_recovered{}, commit_decided{};
        bool particle_birth{};
        std::size_t particle_birth_count{};
        std::optional<ReplaySeekOwnership::Phase> ownership_phase;
        std::uint64_t executed_ticks{}, executed_intervals{};
        bool AcceptsIdempotentCancellation(ReplaySeekOwnership::Cancellation cancellation) const noexcept {
            using C=ReplaySeekOwnership::Cancellation;
            if(commit_decided)return false;
            if(cancellation==C::Recovered)
                return phase==RestoreOperationPhase::Recovered && !pending && original_recovered;
            if(cancellation!=C::Undo)return false;
            // CPU B may be installed while GPU/reference retirement still
            // belongs to the seek-wide Recovering owner. This is not readiness.
            return phase==RestoreOperationPhase::Recovering
                || (phase==RestoreOperationPhase::Recovered && pending && !original_recovered);
        }
    };
    // Begin pins a shared checkpoint until Release. A raw, unowned checkpoint
    // is not admitted for asynchronous restoration. All calls are owner-thread
    // only; render work completes through the hold.
    enum class RestoreOperationAction : std::uint8_t { Begin, Read, Publish, Cancel, Commit, Release, Request, BeginExecution,
        SettleTarget, CompleteTarget, ResumeTarget };
    static bool RestoreOperation(RestoreOperationAction action, const Checkpoint* target,
        RestoreOperationWitness* output) noexcept;
    enum class TickAdvancePhase : std::uint8_t { Idle, Releasing, Arming, Advancing, Held, Failed, Stepping, Settling, Settled, CompletionBlocked };
    // A read-only monitor cannot prevent an already queued release from
    // consuming completion. Mutations must return to a fresh application update.
    static constexpr bool ContinueAfterPauseMonitor(InteriorPhase before,
        InteriorPhase after, TickAdvancePhase advance) noexcept {
        return before==after && (after==InteriorPhase::Holding || after==InteriorPhase::Releasing)
            && advance!=TickAdvancePhase::Settling && advance!=TickAdvancePhase::Stepping;
    }
    struct TickAdvanceWitness {
        TickAdvancePhase phase{};
        FailureCode failure{};
        std::uint64_t origin{}, target{};
    };
    // Asynchronous exact advance from a completed-application hold. Only Held
    // means target readiness; intermediate phases retain application ownership.
    TickAdvanceWitness AdvanceToTick(std::uint64_t target, void* context = nullptr,
        InteriorObserver observer = nullptr) noexcept;
    TickAdvanceWitness ReadTickAdvance() const noexcept;
    // A managed target completes owed work without another input/traversal;
    // CompletionBlocked retains its exact tick, task and B. Explicit Resume
    // or B recovery can drain further traversals. Before any transaction owns
    // B, preparation may finish the interval and capture B at that actual
    // completed tick; this is never reported as target-seek completion.
    static TickAdvanceWitness CompleteApplicationToHold(void* context=nullptr,
        InteriorObserver observer=nullptr) noexcept;
    enum class SeekPhase : std::uint8_t { Idle, CompletingApplication, Preparing, Publishing,
        AwaitingCommit, Committing, Restored, Advancing, Held, Recovering, Cancelled, Failed, RetryingPreparation, RetryingExecution };
    enum class SeekAction : std::uint8_t { Begin, Read, Cancel, Release, InjectAdvanceFailure, CompleteTarget, Step, InjectSettlementFailure, InjectPreparationFailure, ResumeTarget };
    enum class SeekReleaseResult : std::uint8_t { NotRequested, Rejected, AcceptedPending, Completed, RequiresResume };
    struct SeekWitness {
        SeekPhase phase{};
        FailureCode failure{};
        std::uint64_t origin{}, checkpoint{}, target{}, undo_tick{}, owned_bytes{};
        bool pending{}, commit_decided{}, original_recovered{};
        // Produced by the runtime's own lease implementation. Callers must
        // not validate a runtime-owned lease through another DLL's registry.
        std::uint64_t rejected_checkpoint{};
        bool automatic_selection{}, retained_owner_rejected{};
        // Wall time inside admitted application calls while advancing. Engine
        // time includes installed observers; frame_sync_us is a tail subset.
        std::uint64_t prefix_us{}, engine_us{}, tail_us{}, frame_sync_us{};
        // Native C work discarded by successful B recovery. Completing an
        // interior interval may contribute more than its initially held tick.
        std::uint64_t recovered_execution_ticks{}, recovered_execution_intervals{};
        // Release's bool reports acceptance. Completed alone permits the caller
        // to proceed after native tails and deferred checkpoint retirement.
        SeekReleaseResult release_result{};
        std::uint32_t preparation_fallbacks{};
        FailureCode last_preparation_failure{};
        std::uint32_t execution_fallbacks{};
        FailureCode last_execution_failure{};
        std::uint64_t fallback_execution_ticks{},fallback_execution_intervals{};
    };
    using SeekObserver=void (*)(void*,const SeekWitness&,const InteriorWitness&);
    // Seek from an immutable preceding checkpoint. A null checkpoint selects
    // the closest host-accounted image preceding target and the current tick;
    // a displaced pin retires after its pending application work. The caller owns retained handles
    // and validates target against its index; this API does not certify range.
    // Forward requests use the same B preparation/restore/resimulation contract.
    // Identical requests at a completed seek hold leave its observer unchanged.
    // This owns
    // the existing restore and exact-advance phases; it does not select or
    // certify an index. Cancel is rejected once commit becomes irreversible.
    // An interior request must first finish its pending application. Cancellation
    // then holds/reconstructs B at undo_tick, which can exceed origin. Only equal
    // ticks may report original_recovered; no snapshot of an expired native task
    // is invented. Cancellation remains pending until completion and full B undo.
    static bool SeekOperation(SeekAction action,const Checkpoint* checkpoint,std::uint64_t target,
        SeekWitness* witness,void* context=nullptr,SeekObserver observer=nullptr) noexcept;
    Status ReviseInputs(std::span<const ReplayInputOverride> edits,std::uint64_t expected,std::uint64_t& revision) noexcept;
    enum class IndexAction : std::uint8_t { Begin, Read, Cancel, Release, RetainCapture, BeginManaged };
    enum class IndexCheckpointPhase : std::uint8_t { Idle, AwaitingBoundary, AwaitingCapture, Capturing, RetiringScratch, Resuming, Complete, Failed };
    struct IndexCheckpointWitness {
        IndexCheckpointPhase phase{};
        FailureCode failure{};
        std::uint64_t requested_tick{}, captured_tick{}, capture_elapsed_us{}, owned_bytes{};
        bool pending() const noexcept {return phase!=IndexCheckpointPhase::Idle && phase!=IndexCheckpointPhase::Complete && phase!=IndexCheckpointPhase::Failed;}
    };
    // Adopt an existing completed-application hold, capture/retain that exact
    // indexed tick, and resume indexing only after scratch retirement. The
    // request replaces the owning hold observer; pause monitors stay separate.
    // Alternatively, arm the immediately following tick from active combat.
    // Cancellation then completes that boundary before retiring index owners.
    static Status RequestIndexCheckpoint(std::uint64_t held_tick) noexcept;
    static bool ReadIndexCheckpoint(IndexCheckpointWitness* witness) noexcept;
    static bool IndexOperation(IndexAction action, ReplayTickIndex::Witness* witness,
        std::size_t budget = 4 * 1024 * 1024) noexcept;
    static bool ReadIndexEntry(std::uint64_t tick, ReplayTickIndex::Entry* entry) noexcept;
    // Latest UI intent only. The application owner completes any preceding
    // transaction before admitting it; queueing never publishes checkpoint state.
    static Status RequestIndexedSeek(std::uint64_t tick) noexcept;
    static Status RequestIndexedRound(int direction) noexcept;
    enum class CheckpointOwnership : std::uint8_t { Retain, Release, ReleaseAll };
    // Host-owned immutable anchors for indexing/selection. Releasing a pin
    // never cancels a seek which already owns that image. Native retirement
    // remains deferred and charged by the existing application/render owner.
    static Status OwnCheckpoint(CheckpointOwnership action,const Checkpoint* checkpoint,
        std::size_t* retained_count) noexcept;
    // Compatibility for the original same-task repeat restore experiment.
    Status AdvancePendingRepeatToTick(std::uint64_t target) noexcept;
    bool paused() const noexcept { return paused_; }
    bool failed() const noexcept { return failed_; }
    std::uint64_t held_updates() const noexcept { return held_updates_; }
    std::uint64_t completed_worlds() const noexcept { return completed_worlds_; }
    bool world_idle() const noexcept { return executor_.idle(); }
    bool engine_idle() const noexcept { return engine_phase_ == EnginePhase::Idle; }
    std::uint64_t completed_engines() const noexcept { return completed_engines_; }
    std::uint64_t arena_scopes() const noexcept { return executor_.arena_scopes(); }
    std::uint64_t max_retained_arena_bytes() const noexcept { return executor_.max_retained_arena_bytes(); }
    bool arena_empty() const noexcept { return executor_.arena_empty(); }
    std::uint64_t arena_probe_checks() const noexcept { return executor_.arena_probe_checks(); }
    bool OwnsManagerEntry(void* manager) const noexcept
    { return depth_ && executor_.task_groups().OwnsManagerEntry(manager); }
    std::uint64_t manager_tasks() const noexcept { return executor_.task_groups().manager_tasks(); }
    std::uint64_t manager_yields() const noexcept { return executor_.task_groups().manager_yields(); }
    std::uint64_t completed_groups() const noexcept
    { return executor_.task_groups().completed_groups() - initial_groups_; }
    std::uint64_t dispatched_tasks() const noexcept
    { return executor_.task_groups().dispatched_tasks() - initial_tasks_; }

private:
    std::size_t memory_limit_{Schema::replay_timeline_memory_limit};
    bool motion_probe_used_{};
    struct RollingState {
        RollingWitness witness;
        ReplayRollingTelemetry telemetry;
        std::array<CheckpointHandle,8> checkpoints;
        std::array<CheckpointHandle,8> replacement;
        std::array<std::uint64_t,8> generations{},replacement_generations{};
        ReplayCorrectionSchedule::Handle schedule;
        ReplayRollingInputHistory input_history,replacement_history;
        std::shared_ptr<const ReplayInputRevision> proposed_revision;
        std::uint64_t proposed_revision_id{};
        std::size_t next_correction{};
        bool correcting{},correction_installed{},rebuild_complete{};
        ReplaySourceState source;
        std::shared_ptr<const ReplayInputRevision> revision;
        std::chrono::steady_clock::time_point cycle_started{}, forward_started{};
        std::uint64_t capture_us{}, forward_us{}, cycle_intervals{};
    } rolling_;
    bool ObserveRollingHold(const InteriorWitness&) noexcept;
    void ObserveRollingSeek(const SeekWitness&,const InteriorWitness&) noexcept;
    bool FailRolling(FailureCode,const char*) noexcept;
    void EmitRollingTelemetry() noexcept;
    bool AdvanceRollingForward() noexcept;
    bool StoreRollingCheckpoint(CheckpointHandle) noexcept;
    bool PrepareRollingCheckpointSlot() noexcept;
    bool HasRollingReplacementCapacity(std::size_t minimum_private_capture_bytes) const noexcept;
    bool ValidateRollingReplacement() const noexcept;
    bool CommitRollingReplacement() noexcept;
    void ObserveRollingInput(const Sc6ReplayExecutor::Continuation&) noexcept;
    bool DriveRollingReplacement() noexcept;
    SessionExitWitness session_exit_;
    std::uint64_t session_exit_hook_{};
    void* session_exit_function_{}; void* session_exit_scene_{}; void* session_exit_manager_{};
    std::int32_t session_exit_function_index_{},session_exit_function_serial_{};
    std::int32_t session_exit_scene_index_{},session_exit_scene_serial_{};
    std::int32_t session_exit_manager_index_{},session_exit_manager_serial_{};
    bool CanRequestMenuExit() const noexcept;
    bool RequestMenuExit() noexcept;
    bool BindSessionExit();
    bool UnbindSessionExit() noexcept;
    bool ValidateSessionExit() const noexcept;
    bool ServiceSessionExit() noexcept;
    bool SessionExitRequested() const noexcept { return session_exit_.phase!=SessionExitPhase::Idle; }
    ReplayTickIndex index_;
    enum class IndexCheckpointSelection : std::uint8_t { Explicit, FirstCombat, FirstChosen, NextRound, LastChosen, Done };
    IndexCheckpointSelection index_checkpoint_selection_{};
    std::int32_t index_checkpoint_round_{};
    unsigned index_checkpoint_replacement_attempts_{};
    void SelectInitialIndexCheckpoint() noexcept;
    bool IndexBoundaryRequiresCompletion() const noexcept;
    IndexCheckpointWitness index_checkpoint_;
    bool DriveIndexCheckpointHold(const InteriorWitness&) noexcept;
    void FinishIndexCheckpointResume() noexcept;
    std::optional<std::uint64_t> queued_indexed_seek_;
    FailureCode indexed_seek_failure_{};
    void DriveIndexedSeekIntent() noexcept;
    void TraceIndexedSeek(std::uint64_t,bool,std::uint64_t) const noexcept;
    void TraceIndexedSeekAlreadyHeld(std::uint64_t) const noexcept;
    ReplaySourceState index_source_{};
    std::uint64_t index_finish_hook_{};
    void* index_finish_function_{};
    std::int32_t index_finish_function_index_{}, index_finish_function_serial_{};
    bool ReadIndexObservation(const Sc6ReplayExecutor::Continuation&, ReplayTickIndex::Entry&) noexcept;
    bool IsRetainedSourceStopSafe() noexcept;
    static void ObserveIndexBoundary(void*, const Sc6ReplayExecutor::Continuation&, bool) noexcept;
    bool BindIndexFinish();
    bool UnbindIndex(bool retain_finish=false) noexcept;
    enum class IndexEndHold : std::uint8_t { Idle, AwaitingBoundary, Arming, Held };
    IndexEndHold index_end_hold_{};
    std::uint64_t index_end_tick_{};
    void FinishIndexAtApplicationBoundary() noexcept;
    void ServiceIndexControls() noexcept;
    struct SeekState {
        SeekWitness witness{};
        CheckpointHandle checkpoint;
        void* context{};
        SeekObserver observer{};
        // Explicit qualification request; never armed by replay controls.
        std::optional<std::uint64_t> advance_failure_tick;
        std::optional<std::uint64_t> preparation_failure_checkpoint;
        bool cancel{}, restore_requested{};
    } seek_;
    bool seek_driving_{};
    CheckpointHandle released_seek_checkpoint_;
    bool seek_retirement_pending_{}, seek_retirement_failed_{};
    bool AdvanceSeekRetirement() noexcept;
    bool CanRetireSeekCheckpoint() const noexcept;
    CheckpointHandle EarlierSeekCheckpoint(std::uint64_t before,std::uint64_t target) const noexcept;
    void DiagnoseCheckpointOwner(const Checkpoint&) const noexcept;
    Status ValidateParticleCompletionOwnership(const Checkpoint&) const;
    Status ValidateParticleCompletionOwnership(const Sc6ReplayVfxState&,const Sc6ReplayTraceState&) const;
    struct CompletionOwnerContext {std::uintptr_t base;const Sc6ReplayTraceState* traces;};
    static bool CheckParticleCompletionReceiver(const void*,bool,const std::array<std::int32_t,2>&,std::uint64_t);
    Status ArmHistoricalFinishGuard();

    bool CanRetryHistoricalInputs(const Checkpoint&) const noexcept;
    void TraceSeekExecutionFallback(std::uint64_t,std::uint64_t,const RestoreOperationWitness&) const noexcept;
    void TraceSeekPreparationFallback(std::uint64_t rejected,std::uint64_t selected,FailureCode failure) const noexcept;
    Status RetireRejectedIndexCheckpoints() noexcept;
    void TraceRejectedIndexCheckpoint(const Checkpoint& image) const noexcept;
    void TraceIndexCheckpointHealth(const Checkpoint&,const IndexCheckpointHealth&) const noexcept;
    CheckpointHandle ResolveOwnedCheckpoint(const Checkpoint* address) const noexcept;
    void TraceSeekCancellation(std::uint64_t tick, bool accepted) const noexcept;
    void TraceSeekStep(std::uint64_t origin,std::uint64_t target) const noexcept;
    void TraceSeekUiResume(std::uint64_t tick) const noexcept;
    void TraceSeekAdvanceFailure(std::uint64_t tick) const noexcept;
    bool SeekOwnsExecution() const noexcept {return seek_.witness.phase!=SeekPhase::Idle;}
    void AdvanceSeek() noexcept;
    static bool ObserveSeekHold(void*,const InteriorWitness&);
    TickAdvanceWitness tick_advance_;
    void* tick_advance_context_{};
    InteriorObserver tick_advance_observer_{};
    void* storage_context_{};
    std::size_t (*companion_storage_)(void*) noexcept {};
    std::unique_ptr<Sc6CandidateCheckpointCapture> checkpoint_capture_;
    std::unique_ptr<Checkpoint> restore_undo_;
    CaptureWitness capture_operation_{};
    CheckpointHandle captured_checkpoint_;
    bool capture_driving_{}, restore_preparation_driving_{};
    bool corrected_capture_{};
    bool BeginCorrectedCapture(CaptureWitness* witness) noexcept;
    std::unique_ptr<Sc6ReplayParticleCopy>& CaptureParticleCopyOwner() noexcept;
    bool CaptureCommandFailed() const noexcept;
    std::chrono::steady_clock::time_point capture_started_{};
    void AdvanceCaptureOperation() noexcept;
    std::vector<std::weak_ptr<const Checkpoint>> retained_checkpoints_;
    std::vector<CheckpointHandle> checkpoint_pins_;
    // Fixed metadata is charged by sizeof(host); weak references acquire no
    // native leases and cannot resurrect a retired checkpoint/control block.
    std::array<IndexCheckpointHealth,32> index_checkpoint_health_{};
    std::size_t AdmissionBytes(const Checkpoint* pending = nullptr) const noexcept;
    std::size_t AdmissionRemaining(const Checkpoint* pending = nullptr) const noexcept;
    bool RegisterCheckpoint(const Checkpoint& checkpoint);
    bool checkpoint_restoring_{};
    struct HistoricalRestore;
    static bool ProtectParticleLifecycle(void*,void*,bool) noexcept;
    bool HistoricalParticleAbortPending() const noexcept;
    bool HistoricalConsumerAbortPending() const noexcept;
    Status ArmHistoricalConsumerGuard();
    bool StopHistoricalConsumerGuard();
    std::unique_ptr<HistoricalRestore> historical_restore_;
    Status RequestHistoricalRestore(const Checkpoint& target);
    void AdvanceRestorePreparation() noexcept;
    Status PrepareHistoricalRestore(const Checkpoint& target);
    Status PublishHistoricalRestore() noexcept;
    Status UndoHistoricalRestore() noexcept;
    Status PrepareHistoricalPhysicsForExecution() noexcept;
    Status BeginHistoricalCpuExecution();
    Status SettleHistoricalCpuExecution();
    Status RequestHistoricalExecution();
    Status StepHistoricalExecution();
    Status ReopenHistoricalCpuForUndo();
    bool ArmSettlementFailure(std::uint64_t participants) noexcept;
    void AdvanceHistoricalExecution();
    bool CheckHistoricalMaterialBoundary(bool c_only_retirement=false) noexcept;
    bool MaterialCopyTransitionAllowed() noexcept;
    void PublishHistoricalOwnership() noexcept;
    Status CaptureHistoricalExecution(bool retiring);
    bool HistoricalExecutionAdmitted(bool allow_settled = false) const noexcept;
    bool CanReleaseHistoricalRestore() const noexcept;
    bool HistoricalNativeOwnersReleased() const noexcept;
    Status PrepareFreshParticleOwners();
    Status BeginFreshParticleRenderOwners();
    Status SettleFreshParticleOwners();
    Status ReleaseFreshParticleOwnership(bool commit);
    Status TransposeFreshParticleScheduler();
    Status PrepareFreshSchedulerTarget();
    Status RetirePreparedFreshParticles(bool& pending);
    void AdvanceHistoricalRestore() noexcept;
    UcrtRandBroker* checkpoint_broker_{};
    void* manager_{};
    std::int32_t manager_index_{}, manager_serial_{};
    std::uint64_t checkpoint_session_{};
    static inline std::atomic<std::uint64_t> next_checkpoint_session_{};
    bool ValidManager() const noexcept;
    Status CaptureCheckpointUnchecked(Checkpoint& output);
    bool CaptureCheckpointDisplay(Checkpoint& output) const noexcept;
    std::shared_ptr<const SurfaceSnapshot> RestoreCheckpointDisplay(const Checkpoint& target) const noexcept;
    Status CaptureParticleConfigurations(Checkpoint& output) noexcept;
    Status ValidateCheckpointLease(const Checkpoint& checkpoint) noexcept;
    Status ReadCheckpointSource(ReplaySourceState& source) noexcept;
    bool CheckSourceRegistration(const ReplaySourceState& source,bool trace) noexcept;
    static void* ResolveCheckpointSource(void* context) noexcept;
    Sc6ReplayExecutor* simulation_{};
    bool yield_each_tick_{};
    using EnginePost = bool (*)(void*);
    EnginePost defer_engine_post_{}, complete_engine_post_{};
    bool engine_post_deferred_{};
    InteriorPhase interior_phase_{};
    PauseBoundary pause_boundary_{};
    InteriorObserver interior_observer_{};
    PauseMonitor pause_monitor_{};
    void* pause_monitor_context_{};
    void* interior_context_{};
    std::uint64_t interior_target_{}, interior_updates_{}, interior_epoch_{};
    double interior_wall_reference_{};
    std::int64_t interior_qpc_{};
    bool interior_previous_visible_{};
    bool interior_resume_requested_{};
    std::uint64_t interior_ui_resume_baseline_{};
    std::uint64_t interior_ui_step_baseline_{};
    std::uint64_t interior_ui_cancel_baseline_{};
    std::chrono::steady_clock::time_point next_surface_{};
    bool TrySuspendInterior();
    bool ArmPause(std::uint64_t tick, void* context, InteriorObserver observer, PauseBoundary boundary) noexcept;
    bool BeginHold(bool defer_post);
    void TrySuspendApplication();
    bool AdmitGroundUpdate();
    bool ground_update_pending_{};
    ReplayPhysicsBodyInventory ground_body_inventory_{};
    std::uint64_t ground_body_inventory_session_{};
    std::uint64_t ground_body_inventory_probe_session_{};
    void AdvanceInteriorHold();
    bool HasInteriorContinuation() const noexcept;
    enum class SurfaceCommand : std::uint8_t { Arm, Draw, Release, ParticleCopy, RetainCheckpoint,
        InstallCheckpoint, UndoCheckpoint, RetireHeldDisplay, ReleaseForAdvance,
        PublishCheckpoint, FinishDisplayCommit, FinishDisplayRecovery } surface_command_{};
    std::size_t surface_arm_budget_{}; // GT admission; consumed only by the queued render task.
    std::shared_ptr<SurfaceSnapshot> held_surface_;
    std::unique_ptr<Sc6ReplayParticleCopy> particle_copy_;
    std::unique_ptr<Sc6ReplayParticleCopy> corrected_particle_copy_;
    bool particle_command_corrected_{};
    std::atomic<bool> corrected_particle_command_failed_{};
    ParticleCopyAction particle_copy_action_{};
    Sc6ReplayParticleCopy::CaptureIdentity particle_capture_identity_{};
    std::size_t particle_capture_budget_{};
    bool ReopenCapturedImageAtB() noexcept;
    Sc6ReplayVfxState::ParticleBirthSet particle_birth_;
    std::array<Sc6ReplayVfxState::ParticleBirth::RenderBinding, Sc6ReplayVfxState::ParticleBirthSet::capacity> particle_birth_render_{};
    std::size_t particle_birth_render_count_{};
    std::size_t particle_birth_budget_{};
    ReplayGpuCompletion::Clock::time_point particle_publication_deadline_{};
    std::atomic<bool> particle_command_pending_{};
    std::atomic<bool> particle_command_failed_{};
    void QueueParticleCopyCommand();
    static void ExecuteParticleCopyCommand(void* list, void* command) noexcept;
    void* surface_event_{};
    std::atomic<bool> surface_result_{};
    bool QueueSurface(SurfaceCommand command);
    bool PollSurface(bool& complete);
    static void RunSurfaceTask(void* owner);
    bool ValidWorld() const noexcept;
    bool ValidEngineContext() const noexcept;
    bool ResolveWorld(void* manager) noexcept;
    static bool TickEngine(void* owner, RC::Unreal::UEngine* engine, float delta, bool idle_mode) noexcept;
    inline static Sc6ReplayHost* active_{};
    bool registered_{};
    // GuardedMain has no task pump between these calls. The application tail
    // belongs to the logical frame, including its engine-epoch increment.
    enum class ApplicationPhase : std::uint8_t { Idle, Engine, Tail } application_phase_{};
    std::unique_ptr<PLH::x64Detour> application_hook_;
    std::unique_ptr<NativeWindConstruction> wind_construction_;
    std::unique_ptr<NativeReplayRendering> replay_rendering_;
    std::unique_ptr<NativeReplayWidgetClock> widget_clock_;
    std::unique_ptr<NativeReplayCpuEmitterLifetime> emitter_lifetime_;
    std::unique_ptr<NativeReplayCallbackAdmission> callback_admission_;
    struct ConsumerHoldState;
    std::unique_ptr<ConsumerHoldState> consumer_hold_;
    bool SuspendConsumerTask();
    bool AdvanceConsumerTask();
    bool RetireConsumerProbe();
    std::unique_ptr<Sc6ReplayInputSource> input_source_;
    std::uint64_t application_original_{};
    void* application_loop_{};
    void* application_media_{};
    bool application_idle_{}, application_active_{}, application_stop_requested_{};
    std::uint64_t completed_applications_{};
    std::uint64_t unpaced_applications_{}, unpaced_admission_rejections_{};
    static void TickApplication(void* loop);
    bool BindApplication();
    bool UnbindApplication();
    void EnterApplication();
    bool EnterUnpacedApplicationTime(void* engine);
    void FinishApplication();
    void DispatchApplicationRenderTask(std::uintptr_t builder_rva);
    bool DirectApplicationRendering() const;
    using EngineBody = bool(*)(void*, RC::Unreal::UEngine*, float, bool) noexcept;
    using SetEngineBody = bool(*)(void*, EngineBody);
    SetEngineBody set_engine_body_{};
    std::uintptr_t image_base_{};
    Sc6ReplayWorld executor_;
    void* world_{};
    void* engine_{};
    void* bound_context_{};
    std::uint64_t bound_context_handle_{};
    std::int32_t object_index_{}, object_serial_{};
    std::int32_t engine_index_{}, engine_serial_{};
    std::uint32_t thread_{}, depth_{};
    std::uint64_t held_updates_{};
    std::uint64_t completed_worlds_{};
    std::uint64_t initial_groups_{}, initial_tasks_{};
    bool paused_{}, failed_{};

    enum class EnginePhase : std::uint8_t
    {
        Idle, Entry, Context, World, WorldTail, NextContext, TickUnbound,
        Viewport, ResetWindow, Render, Device, Audio, RenderClock, Finish
    } engine_phase_{EnginePhase::Idle};
    float engine_delta_{};
    bool idle_mode_{}, any_world_running_{}, engine_world_bound_{};
    int context_index_{};
    std::byte* context_{};
    std::uint64_t saved_context_handle_{}, completed_engines_{};
    void DrainEngine(float delta, bool idle_mode);
    void AdvanceEngine();
    void EnterEngine();
    bool TickEngineWorld();
    void TickEngineWorldTail();
    void DispatchCauseEvent();
    void ResetEngineWindow();
    void* RendererModule();
    void DispatchEngineRenderClock();
};
}
