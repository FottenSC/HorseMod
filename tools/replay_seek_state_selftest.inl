// Compile the production orchestration against controlled asynchronous owners.
// These tests exercise its actions, not a second implementation of its policy.
#include "deterministic/Sc6ReplayHost.hpp"
#include "deterministic/ReplaySeekOwnership.hpp"
#include "replay_qualification_mod/ReplayIndexCompletion.hpp"
// The native2503 pre-combat ->2504 combat transition must be armable; only
// the actual target can authorize capture. Wrong/inactive/intro targets reject.
static_assert(ReplayQualification::CanArmIndexCheckpoint(2503,2504,true));
static_assert(!ReplayQualification::CanArmIndexCheckpoint(2504,2504,true));
static_assert(!ReplayQualification::CanArmIndexCheckpoint(2502,2504,true));
static_assert(!ReplayQualification::CanArmIndexCheckpoint(2503,2504,false));
static_assert(!ReplayQualification::CanArmIndexCheckpoint(UINT64_MAX,0,true));
static_assert(ReplayQualification::IsCombatIndexCheckpoint(2504,2504,true,2,2));
static_assert(!ReplayQualification::IsCombatIndexCheckpoint(2503,2504,true,2,2));
static_assert(!ReplayQualification::IsCombatIndexCheckpoint(2504,2504,false,2,2));
static_assert(!ReplayQualification::IsCombatIndexCheckpoint(2504,2504,true,1,1));
static_assert(!ReplayQualification::IsCombatIndexCheckpoint(2504,2504,true,2,1));
static_assert(!ReplayQualification::IsCombatIndexCheckpoint(2504,2504,true,1,2));
// A completed native index must not time out while a long seek executes.
// Keep the arming and final retirement waits bounded by the endpoint driver.
static_assert(ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Idle));
static_assert(ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Arming));
static_assert(ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::EndHold));
static_assert(ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Done));
static_assert(!ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Seeking));
static_assert(!ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Executing));
static_assert(!ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Holding));
static_assert(!ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Tails));
static_assert(!ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Releasing));
static_assert(!ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::AwaitingUiRequest));
static_assert(!ReplayQualification::AwaitingIndexEndpoint(ReplayQualification::IndexSeek::Resuming));
static_assert(ReplayQualification::AllowsLateRecoveryHold(true,true,2,0,3,3,3163));
static_assert(!ReplayQualification::AllowsLateRecoveryHold(false,true,2,0,3,3,3163));
static_assert(!ReplayQualification::AllowsLateRecoveryHold(true,false,2,0,3,3,3163));
static_assert(!ReplayQualification::AllowsLateRecoveryHold(true,true,2,0,1,1,3163));
static_assert(!ReplayQualification::AllowsLateRecoveryHold(true,true,2,4,3,3,3163));
static_assert(!ReplayQualification::AllowsLateRecoveryHold(true,true,2,0,3,3,0));
namespace ReplaySeekStateTest {
using namespace Horse::Deterministic;
using PublicHost=Horse::Deterministic::Sc6ReplayHost;
struct Sc6ReplayParticleCopy {
    static inline bool retiring{};
    static bool capture_retirement_pending() noexcept {return retiring;}
};
struct Sc6ReplayHost {
    std::size_t memory_limit_{Horse::Deterministic::Schema::replay_timeline_memory_limit};
    using SeekPhase=PublicHost::SeekPhase; using SeekAction=PublicHost::SeekAction;
    using CheckpointOwnership=PublicHost::CheckpointOwnership;
    using SeekReleaseResult=PublicHost::SeekReleaseResult;
    using SeekWitness=PublicHost::SeekWitness; using SeekObserver=PublicHost::SeekObserver;
    using InteriorWitness=PublicHost::InteriorWitness; using InteriorObserver=PublicHost::InteriorObserver;
    using InteriorPhase=PublicHost::InteriorPhase; using PauseBoundary=PublicHost::PauseBoundary;
    using CapturePhase=PublicHost::CapturePhase; using TickAdvancePhase=PublicHost::TickAdvancePhase;
    using CaptureAction=PublicHost::CaptureAction;using CaptureWitness=PublicHost::CaptureWitness;
    using TickAdvanceWitness=PublicHost::TickAdvanceWitness;
    using RestoreOperationAction=PublicHost::RestoreOperationAction;
    using RestoreOperationPhase=PublicHost::RestoreOperationPhase;
    using RestoreOperationWitness=PublicHost::RestoreOperationWitness;
    enum class ApplicationPhase {Idle}; enum class SurfaceCommand {Draw,Install};
    struct Checkpoint:std::enable_shared_from_this<Checkpoint> {
        bool valid=true,gpu=true,surface=true,task=false,event=false;
        bool supported=true;
        struct {bool live=true,admission_busy=false;mutable unsigned checks{};
            Status ValidateRetainedOwners() const {++checks;return admission_busy?Status::failure(FailureCode::ContextUnavailable):live?Status::success():Status::failure(FailureCode::GenerationMismatch);}
            bool retained_owners_live() const {return ValidateRetainedOwners().ok();}} vfx;
        bool historical_target_shape_supported() const {return valid && supported;}
        std::uint64_t session=1;
        PauseBoundary boundary=PauseBoundary::CompletedApplication;
        struct {Sc6ReplayExecutor::Phase phase=Sc6ReplayExecutor::Phase::Idle;std::uint64_t tick=205;} execution;
    };
    using CheckpointHandle=std::shared_ptr<const Checkpoint>;
    // Display-policy negatives use the real methods in replay_surface_publication_selftest.
    bool RestoreCheckpointDisplay(const Checkpoint& image) const {return image.surface;}
    ReplayTickIndex index_;
    PublicHost::IndexCheckpointWitness index_checkpoint_{};
    bool cancel_index_on_service{};unsigned index_selections{};
    void ServiceIndexControls(){if(cancel_index_on_service)index_.Cancel();}
    void FinishIndexCheckpointResume(){}
    void SelectInitialIndexCheckpoint(){++index_selections;}
    void FinishIndexAtApplicationBoundary() noexcept;
    std::optional<std::uint64_t> queued_indexed_seek_;
    FailureCode indexed_seek_failure_{};
    static Status RequestIndexedSeek(std::uint64_t) noexcept;
    static Status RequestIndexedRound(int) noexcept;
    void DriveIndexedSeekIntent() noexcept;
    void TraceIndexedSeek(std::uint64_t,bool,std::uint64_t) const noexcept {}
    mutable unsigned same_tick_completions{};
    void TraceIndexedSeekAlreadyHeld(std::uint64_t tick) const noexcept {if(tick!=simulation_->state.tick)std::abort();++same_tick_completions;}
    struct {SeekWitness witness{};std::shared_ptr<const Checkpoint> checkpoint;
        void* context{};SeekObserver observer{};std::optional<std::uint64_t> advance_failure_tick,preparation_failure_checkpoint;bool cancel{},restore_requested{};} seek_;
    struct Simulation {struct {std::uint64_t tick=210;} state;
        auto& continuation(){return state;}} simulation_storage;
    Simulation* simulation_=&simulation_storage;
    struct {bool idle(){return true;}} executor_;
    CaptureWitness capture_operation_{};
    bool corrected_capture_{};
    struct {bool correcting{};std::array<CheckpointHandle,8> replacement;std::array<std::uint64_t,8> replacement_generations{};bool rebuild_complete{};std::unique_ptr<Horse::Deterministic::ReplayRollingTelemetry> telemetry_storage=std::make_unique<Horse::Deterministic::ReplayRollingTelemetry>();Horse::Deterministic::ReplayRollingTelemetry& telemetry=*telemetry_storage;} rolling_;
    bool DriveRollingReplacement(){return false;}
    unsigned capture_cancels{},capture_releases{};
    static bool CaptureOperation(CaptureAction action,CaptureWitness*) {
        auto& h=*active_;
        if(action==CaptureAction::Cancel){++h.capture_cancels;h.capture_operation_.phase=CapturePhase::Retiring;return true;}
        if(action==CaptureAction::Release){++h.capture_releases;h.corrected_capture_=false;h.capture_operation_={};return true;}
        return false;
    }
    struct Restore {RestoreOperationWitness witness;std::optional<ReplaySeekOwnership> ownership;bool preparation_retiring{},preparing{},execution{},material_failed{};};
    std::unique_ptr<Restore> historical_restore_;
    // This orchestration fixture supplies a material-free native owner. The
    // material predicate itself is exercised by the production-guard fixture.
    bool CheckHistoricalMaterialBoundary()const {return historical_restore_ && !historical_restore_->material_failed;}
    struct Copy {bool blocked=false;struct {bool birth_prepared=true;} state;
        bool blocks_resume(){return blocked;} auto& witness(){return state;}} copy;
    Copy* particle_copy_=&copy;
    std::atomic_bool particle_command_pending_=false;
    bool checkpoint_restoring_=false,seek_driving_=false,depth_=false,surface_event_=false;
    bool valid_binding=true,cancel_allowed=true,release_allowed=true,settle_allowed=true,request_allowed=true;
    std::shared_ptr<const Checkpoint> released_seek_checkpoint_;
    bool seek_retirement_pending_{},seek_retirement_failed_{},retirement_blocked{};
    bool engine_post_deferred_{};
    unsigned publications=0,commits=0,cancels=0,advances=0;
    std::uint64_t bytes=1024,checkpoint_session_=1;
    decltype(GetCurrentThreadId()) thread_=GetCurrentThreadId();
    InteriorPhase interior_phase_=InteriorPhase::Holding;
    PauseBoundary pause_boundary_=PauseBoundary::CompletedApplication;
    ApplicationPhase application_phase_=ApplicationPhase::Idle;
    SurfaceCommand surface_command_=SurfaceCommand::Draw;
    void* interior_context_{};InteriorObserver interior_observer_{};bool interior_resume_requested_=false;
    std::uint64_t interior_ui_resume_baseline_{},interior_ui_step_baseline_{},interior_ui_cancel_baseline_{},requested_tick{};
    TickAdvanceWitness tick_advance_{};
    std::vector<std::weak_ptr<const Checkpoint>> retained_checkpoints_;
    std::vector<CheckpointHandle> checkpoint_pins_;
    using IndexCheckpointHealth=PublicHost::IndexCheckpointHealth;
    std::array<IndexCheckpointHealth,32> index_checkpoint_health_{};
    mutable unsigned health_invalidations{},health_deferrals{},health_valid{};
    mutable IndexCheckpointHealth last_health;
    void TraceIndexCheckpointHealth(const Checkpoint&,const IndexCheckpointHealth& health) const noexcept {
        last_health=health;
        if(health.phase==IndexCheckpointHealth::Phase::Invalid)++health_invalidations;
        else if(health.phase==IndexCheckpointHealth::Phase::Deferred)++health_deferrals;
        else ++health_valid;
    }
    inline static Sc6ReplayHost* active_{};
    Sc6ReplayHost(){active_=this;}
    bool CheckBinding(){return valid_binding;} bool engine_idle()const{return true;}
    bool world_idle()const{return true;}
    std::uint64_t AdmissionBytes(){return bytes;}
    std::size_t AdmissionRemaining(){return bytes<Schema::replay_timeline_memory_limit?Schema::replay_timeline_memory_limit-bytes:0;}
    static Status OwnCheckpoint(CheckpointOwnership,const Checkpoint*,std::size_t*) noexcept;
    Status RetireRejectedIndexCheckpoints() noexcept;
    unsigned invalid_index_retirements{};
    void TraceRejectedIndexCheckpoint(const Checkpoint&) const noexcept {++const_cast<Sc6ReplayHost*>(this)->invalid_index_retirements;}
    bool SeekOwnsExecution()const{return seek_.witness.phase!=SeekPhase::Idle;}
    bool session_exiting{};
    bool SessionExitRequested()const{return session_exiting;}
    bool HistoricalExecutionAdmitted() const noexcept {
        return historical_restore_ && historical_restore_->ownership
            && historical_restore_->ownership->phase()==ReplaySeekOwnership::Phase::ExecutionActive;
    }
    static TickAdvanceWitness CompleteApplicationToHold(void*,InteriorObserver){
        if(!active_->settle_allowed) return {TickAdvancePhase::Failed,FailureCode::IllegalTransition};
        return active_->tick_advance_={TickAdvancePhase::Settling};}
    TickAdvanceWitness AdvanceToTick(std::uint64_t tick,void*,InteriorObserver){requested_tick=tick;++advances;return {TickAdvancePhase::Advancing};}
    static bool RestoreOperation(RestoreOperationAction action,const Checkpoint*,RestoreOperationWitness* out){
        auto& h=*active_; using A=RestoreOperationAction;using P=RestoreOperationPhase;
        if(action==A::Request){if(!h.request_allowed){out->failure=FailureCode::RestorePreflightFailed;return false;}
            h.historical_restore_=std::make_unique<Restore>();
            h.historical_restore_->witness.phase=P::Preparing;h.historical_restore_->witness.pending=true;
            h.historical_restore_->ownership.emplace(h.seek_.witness.undo_tick,h.seek_.witness.target);}
        if(!h.historical_restore_)return false;
        auto& w=h.historical_restore_->witness;*out=w;
        if(action==A::Cancel){++h.cancels;if(!h.cancel_allowed || w.commit_decided)return false;
            if(h.historical_restore_->ownership) {
                const auto phase=h.historical_restore_->ownership->phase();
                if(phase==ReplaySeekOwnership::Phase::Recovering || phase==ReplaySeekOwnership::Phase::Recovered)
                    if(!w.AcceptsIdempotentCancellation(h.historical_restore_->ownership->RequestCancellation()))return false;
            }
            if(w.phase!=P::Recovered){w.phase=P::Recovering;w.pending=true;}h.interior_phase_=InteriorPhase::Holding;}
        if(action==A::Publish){++h.publications;w.phase=P::Publishing;w.pending=true;}
        if(action==A::BeginExecution) {
            auto& owner=*h.historical_restore_->ownership;
            if(w.phase!=P::Held || !owner.PublishA() || !owner.ActivateExecution()) return false;
            w.ownership_phase=owner.phase();w.pending=false;
        }
        if(action==A::SettleTarget) {
            auto& owner=*h.historical_restore_->ownership;
            if(!owner.SettleC(h.simulation_->state.tick)) return false;
            w.ownership_phase=owner.phase();w.pending=false;
        }
        if(action==A::CompleteTarget || action==A::ResumeTarget) {
            auto& owner=*h.historical_restore_->ownership;
            if(h.surface_event_ || !(action==A::ResumeTarget?owner.BeginResumedTails():owner.BeginTargetTails())) return false;
            w.ownership_phase=owner.phase();w.pending=true;
        }
        if(action==A::Commit){
            if(!h.historical_restore_->ownership->BeginCommit()) return false;
            ++h.commits;w.phase=P::Committing;w.pending=true;w.commit_decided=true;
            w.ownership_phase=h.historical_restore_->ownership->phase();
        }
        *out=w;
        if(action==A::Release){if(w.pending || !h.release_allowed)return false;h.historical_restore_.reset();}
        return true;
    }
    static bool SeekOperation(SeekAction,const Checkpoint*,std::uint64_t,SeekWitness*,void* =nullptr,SeekObserver=nullptr) noexcept;
    static bool ObserveSeekHold(void*,const InteriorWitness&);
    void AdvanceSeek() noexcept;
    bool CanRetireSeekCheckpoint() const noexcept;
    CheckpointHandle EarlierSeekCheckpoint(std::uint64_t,std::uint64_t) const noexcept;
    void DiagnoseCheckpointOwner(const Checkpoint&) const noexcept {}
    bool retry_inputs=true;
    bool CanRetryHistoricalInputs(const Checkpoint&) const noexcept {return retry_inputs;}
    unsigned execution_fallback_traces{};
    void TraceSeekExecutionFallback(std::uint64_t,std::uint64_t,const RestoreOperationWitness&) noexcept {++execution_fallback_traces;}
    void TraceSeekPreparationFallback(std::uint64_t,std::uint64_t,FailureCode) const noexcept {}
    CheckpointHandle ResolveOwnedCheckpoint(const Checkpoint*) const noexcept;
    void TraceSeekCancellation(std::uint64_t,bool) const noexcept {}
    void TraceSeekStep(std::uint64_t,std::uint64_t) const noexcept {}
    void TraceSeekUiResume(std::uint64_t) const noexcept {}
    Status StepHistoricalExecution() {
        if(!historical_restore_ || !historical_restore_->ownership->StepFromSettled(simulation_->state.tick))
            return Status::failure(FailureCode::IllegalTransition);
        historical_restore_->witness.ownership_phase=historical_restore_->ownership->phase();
        AdvanceToTick(simulation_->state.tick+1,this,&ObserveSeekHold);return Status::success();
    }
    bool ArmSettlementFailure(std::uint64_t) noexcept {return HistoricalExecutionAdmitted();}
    void TraceSeekAdvanceFailure(std::uint64_t) const noexcept {}
    bool AdvanceSeekRetirement() noexcept {
        if(retirement_blocked || !CanRetireSeekCheckpoint()) return false;
        released_seek_checkpoint_.reset();seek_retirement_pending_=false;Sc6ReplayParticleCopy::retiring=false;return true;
    }
};
#include "../HorseMod/horselib/deterministic/Sc6ReplayHost.Seek.inl"
#include "../HorseMod/horselib/deterministic/Sc6ReplayHost.CheckpointOwnership.inl"
#include "../HorseMod/horselib/deterministic/Sc6ReplayHost.IndexedSeek.inl"
inline bool advance_failure_recovery_contract() {
    using H=Sc6ReplayHost;using P=H::SeekPhase;using R=H::RestoreOperationPhase;
    H h;auto image=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(image);
    H::SeekWitness witness{};
    if(!H::SeekOperation(H::SeekAction::Begin,nullptr,208,&witness)) return false;
    bool injection_armed=false;
    // Complete the controlled asynchronous publication/retirement owners.
    // The orchestration under test is the production Seek.inl, unchanged.
    for(unsigned update=0;update<24 && !h.advances;++update) {
        h.AdvanceSeek();
        if(h.historical_restore_) {
            auto& restore=h.historical_restore_->witness;
            if(restore.phase==R::Preparing) {
                restore.phase=R::Prepared;restore.pending=false;restore.particle_birth=h.copy.state.birth_prepared;
            }
            else if(restore.phase==R::Publishing) {
                restore.phase=R::Held;restore.pending=false;h.simulation_->state.tick=205;
            } else if(restore.phase==R::Committing) {restore.phase=R::Committed;restore.pending=false;}
        }
        if(h.seek_.witness.phase==P::Restored && !injection_armed) {
            if(H::SeekOperation(H::SeekAction::InjectAdvanceFailure,nullptr,205,&witness)
                || H::SeekOperation(H::SeekAction::InjectAdvanceFailure,nullptr,208,&witness)
                || !H::SeekOperation(H::SeekAction::InjectAdvanceFailure,nullptr,206,&witness)
                || H::SeekOperation(H::SeekAction::InjectAdvanceFailure,nullptr,207,&witness)) return false;
            injection_armed=true;
        }
    }
    if(h.advances!=1 || h.seek_.witness.phase!=P::Advancing || !injection_armed) {
        std::fprintf(stderr,"seek advance failure fixture did not reach advance phase=%u advances=%u\n",
            static_cast<unsigned>(h.seek_.witness.phase),h.advances);return false;
    }
    // Fail after a completed traversal, not during initial publication.
    h.simulation_->state.tick=206;
    h.tick_advance_={H::TickAdvancePhase::Held,FailureCode::None,205,208};
    h.AdvanceSeek();
    if(h.seek_.advance_failure_tick || h.seek_.witness.phase!=P::Failed
        || h.seek_.witness.failure!=FailureCode::AdvanceFailed) return false;
    const bool available=h.historical_restore_ && !h.seek_.witness.commit_decided && h.commits==0;
    const bool cancelling=H::SeekOperation(H::SeekAction::Cancel,nullptr,0,&witness);
    std::fprintf(stderr,"seek advance failure contract tick=206 commit_calls=%u B_owner=%s cancellation=%s\n",
        h.commits,h.historical_restore_?"present":"absent",cancelling?"accepted":"rejected");
    if(!available || !cancelling) return false;
    // Complete the asynchronous undo owner at B, not the failed C tick.
    h.simulation_->state.tick=h.seek_.witness.undo_tick;
    auto& restore=h.historical_restore_->witness;
    restore.phase=R::Recovered;restore.original_recovered=true;restore.pending=false;
    h.AdvanceSeek();
    std::fprintf(stderr,"seek advance failure undo original=%llu actual=%llu controlled_owner=true commits=%u\n",
        static_cast<unsigned long long>(h.seek_.witness.undo_tick),static_cast<unsigned long long>(h.simulation_->state.tick),h.commits);
    return h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered
        && !h.historical_restore_ && h.commits==0 && h.advances==1;
}
inline bool expired_index_retirement_contract(bool cancel, bool expired_after_target=false) {
    using H=Sc6ReplayHost;using P=H::SeekPhase;
    H h;ReplayTickIndex::Entry row{};
    if(!h.index_.Begin(row,16384).ok())return false;
    for(unsigned tick=1;tick<=230;++tick) {
        row.tick=row.native_tick=row.interval=tick;
        if(!h.index_.Append(row).ok() || !h.index_.CompleteInterval(tick,tick,tick==230).ok())return false;
    }
    h.index_.ObserveNativeFinish();if(!h.index_.FinishAtApplicationBoundary(230).ok())return false;
    h.simulation_->state.tick=230;
    auto valid=std::make_shared<H::Checkpoint>();valid->execution.tick=170;
    auto expired=std::make_shared<H::Checkpoint>();expired->execution.tick=expired_after_target?220:205;expired->vfx.live=false;
    auto future=std::make_shared<H::Checkpoint>();future->execution.tick=229;
    for(const auto& image:{valid,expired,future}) {
        h.retained_checkpoints_.push_back(image);h.checkpoint_pins_.push_back(image);
    }
    std::weak_ptr<const H::Checkpoint> expired_weak=expired;expired.reset();
    H::SeekWitness witness{};
    if(!H::SeekOperation(H::SeekAction::Begin,nullptr,208,&witness)
        || witness.checkpoint!=170 || witness.retained_owner_rejected==expired_after_target)return false;
    // Model native deferred destruction without making it synchronous. The
    // real OwnCheckpoint/AdvanceSeek must join that owner before requesting B.
    Sc6ReplayParticleCopy::retiring=true;h.retirement_blocked=true;
    h.AdvanceSeek();
    const bool retired=expired_weak.expired() && h.checkpoint_pins_.size()==2
        && h.invalid_index_retirements==1 && h.seek_retirement_pending_
        && !h.historical_restore_ && !h.seek_.restore_requested;
    std::fprintf(stderr,"expired index retirement before B: %s cancel=%d expired_after_target=%d\n",retired?"PASS":"FAIL",cancel,expired_after_target);
    if(!retired){Sc6ReplayParticleCopy::retiring=false;return false;}
    if(cancel && !H::SeekOperation(H::SeekAction::Cancel,nullptr,0,&witness))return false;
    for(unsigned update=0;update<3;++update)h.AdvanceSeek();
    if(h.historical_restore_ || h.publications || h.simulation_->state.tick!=230)return false;
    h.retirement_blocked=false;h.AdvanceSeek();
    const bool result=cancel ? h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered
        && !h.historical_restore_ : h.historical_restore_!=nullptr && h.seek_.restore_requested;
    return result && !h.seek_retirement_pending_ && !Sc6ReplayParticleCopy::retiring
        && h.checkpoint_pins_.size()==2 && h.checkpoint_pins_[0]==valid && h.checkpoint_pins_[1]==future
        && !h.publications && !h.commits && h.simulation_->state.tick==230;
}
inline bool indexing_owner_health_contract(bool busy) {
    using H=Sc6ReplayHost;H h;ReplayTickIndex::Entry row{};
    if(!h.index_.Begin(row,16384).ok())return false;
    for(unsigned tick=1;tick<=230;++tick){row.tick=row.native_tick=row.interval=tick;
        if(!h.index_.Append(row).ok() || !h.index_.CompleteInterval(tick,tick,busy && tick==230).ok())return false;}
    if(busy){h.index_.ObserveNativeFinish();if(!h.index_.FinishAtApplicationBoundary(230).ok())return false;}
    h.simulation_->state.tick=230;
    auto image=std::make_shared<H::Checkpoint>();image->execution.tick=170;
    image->vfx.live=busy;image->vfx.admission_busy=busy;
    h.retained_checkpoints_.push_back(image);h.checkpoint_pins_.push_back(image);
    const auto result=h.RetireRejectedIndexCheckpoints();
    const bool correct=result.ok() && h.checkpoint_pins_.size()==(busy?1u:0u)
        && h.invalid_index_retirements==(busy?0u:1u) && !h.historical_restore_ && !h.publications;
    std::fprintf(stderr,"Index owner health busy=%d recording=%d retained=%zu invalidations=%u result=%s\n",
        busy,!busy,h.checkpoint_pins_.size(),h.invalid_index_retirements,correct?"PASS":"FAIL");
    return correct;
}
inline bool indexing_retired_selection_contract() {
    using H=Sc6ReplayHost;H h;ReplayTickIndex::Entry row{};
    if(!h.index_.Begin(row,16384).ok())return false;
    for(unsigned tick=1;tick<=230;++tick){row.tick=row.native_tick=row.interval=tick;
        if(!h.index_.Append(row).ok() || !h.index_.CompleteInterval(tick,tick,tick==230).ok())return false;}
    auto valid=std::make_shared<H::Checkpoint>();valid->execution.tick=170;
    auto expired=std::make_shared<H::Checkpoint>();expired->execution.tick=205;expired->vfx.live=false;
    for(const auto& image:{valid,expired}){h.retained_checkpoints_.push_back(image);h.checkpoint_pins_.push_back(image);}
    h.simulation_->state.tick=230;
    if(!h.RetireRejectedIndexCheckpoints().ok() || !h.RetireRejectedIndexCheckpoints().ok())return false;
    expired.reset();
    h.index_.ObserveNativeFinish();if(!h.index_.FinishAtApplicationBoundary(230).ok())return false;
    H::SeekWitness witness{};
    const bool pass=H::SeekOperation(H::SeekAction::Begin,nullptr,208,&witness)
        && witness.checkpoint==170 && witness.retained_owner_rejected && witness.rejected_checkpoint==205;
    std::fprintf(stderr,"Retired index selection keeps invalidation receipt: %s\n",pass?"PASS":"FAIL");
    return pass;
}
inline bool indexing_owner_schedule_contract() {
    using H=Sc6ReplayHost;H h;ReplayTickIndex::Entry row{};
    if(!h.index_.Begin(row,16384).ok())return false;
    auto image=std::make_shared<H::Checkpoint>();image->execution.tick=100;
    h.retained_checkpoints_.push_back(image);h.checkpoint_pins_.push_back(image);
    h.simulation_->state.tick=170;
    if(!h.RetireRejectedIndexCheckpoints().ok() || image->vfx.checks!=1 || h.health_valid!=1)return false;
    image->vfx.live=false;h.simulation_->state.tick=199;
    if(!h.RetireRejectedIndexCheckpoints().ok() || image->vfx.checks!=1)return false;
    image->vfx.admission_busy=true;h.simulation_->state.tick=200;
    if(!h.RetireRejectedIndexCheckpoints().ok() || image->vfx.checks!=2 || h.health_deferrals!=1
        || h.last_health.last_valid!=170 || h.last_health.first_invalid || h.checkpoint_pins_.size()!=1)return false;
    image->vfx.admission_busy=false;h.simulation_->state.tick=229;
    if(!h.RetireRejectedIndexCheckpoints().ok() || image->vfx.checks!=2)return false;
    h.simulation_->state.tick=230;Sc6ReplayParticleCopy::retiring=true;
    if(!h.RetireRejectedIndexCheckpoints().ok() || image->vfx.checks!=3 || h.health_invalidations!=1
        || h.last_health.last_valid!=170 || h.last_health.first_invalid!=230 || !h.seek_retirement_pending_
        || h.checkpoint_pins_.size() || h.publications || h.historical_restore_)return false;
    for(unsigned i=0;i<3;++i)if(!h.RetireRejectedIndexCheckpoints().ok())return false;
    if(image->vfx.checks!=3 || h.health_invalidations!=1 || h.invalid_index_retirements!=1)return false;
    h.seek_retirement_failed_=true;
    if(h.RetireRejectedIndexCheckpoints().code!=FailureCode::PresentationFailed || h.invalid_index_retirements!=1)return false;
    h.seek_retirement_failed_=false;h.retirement_blocked=true;
    if(h.AdvanceSeekRetirement() || !h.seek_retirement_pending_)return false;
    h.retirement_blocked=false;
    if(!h.AdvanceSeekRetirement() || h.seek_retirement_pending_ || h.publications)return false;
    Sc6ReplayParticleCopy::retiring=false;
    std::fprintf(stderr,"Index sampling: valid170, deferred200, invalid230; one retirement and no B/publication PASS\n");
    return true;
}
inline bool corrected_capture_recovery_contract() {
    Sc6ReplayHost h;h.corrected_capture_=true;h.capture_operation_.phase=Sc6ReplayHost::CapturePhase::Copying;
    h.seek_.witness.phase=Sc6ReplayHost::SeekPhase::Advancing;h.seek_.restore_requested=true;
    h.historical_restore_=std::make_unique<Sc6ReplayHost::Restore>();
    h.historical_restore_->witness.phase=Sc6ReplayHost::RestoreOperationPhase::Held;
    Sc6ReplayHost::SeekWitness receipt;
    if(!Sc6ReplayHost::SeekOperation(Sc6ReplayHost::SeekAction::Cancel,nullptr,0,&receipt) || h.cancels)return false;
    h.AdvanceSeek();
    if(h.capture_cancels!=1 || h.cancels)return false;
    h.AdvanceSeek(); // Pending GPU completion does not permit native undo.
    if(h.capture_cancels!=1 || h.cancels)return false;
    h.capture_operation_.phase=Sc6ReplayHost::CapturePhase::Cancelled;
    h.AdvanceSeek();
    if(h.capture_releases!=1 || h.cancels || h.corrected_capture_)return false;
    h.AdvanceSeek();
    return h.cancels==1;
}

inline void run(){
    if(!corrected_capture_recovery_contract())std::abort();
    if(!indexing_owner_schedule_contract() || !indexing_retired_selection_contract())std::abort();
    if(!indexing_owner_health_contract(true) || !indexing_owner_health_contract(false))std::abort();
    if(!expired_index_retirement_contract(false) || !expired_index_retirement_contract(true))std::abort();
    if(!expired_index_retirement_contract(false,true) || !expired_index_retirement_contract(true,true))std::abort();
    {
        const auto require=[](bool ok){if(!ok)std::abort();};
        using I=PublicHost::InteriorPhase;using T=PublicHost::TickAdvancePhase;
        require(PublicHost::ContinueAfterPauseMonitor(I::Holding,I::Holding,T::Releasing));
        require(PublicHost::ContinueAfterPauseMonitor(I::Releasing,I::Releasing,T::Releasing));
        require(!PublicHost::ContinueAfterPauseMonitor(I::Holding,I::Releasing,T::Releasing));
        require(!PublicHost::ContinueAfterPauseMonitor(I::Releasing,I::Resumed,T::Releasing));
        require(!PublicHost::ContinueAfterPauseMonitor(I::Holding,I::Holding,T::Stepping));
        require(!PublicHost::ContinueAfterPauseMonitor(I::Holding,I::Holding,T::Settling));
        require(!PublicHost::ContinueAfterPauseMonitor(I::Holding,I::Failed,T::Failed));
    }

    {
        ReplayTickIndex index;ReplayTickIndex::Entry row{};
        const auto check=[](bool ok){if(!ok)std::abort();};
        check(index.Begin(row,4096).ok());row.tick=row.native_tick=1;row.interval=1;
        check(index.Append(row).ok() && index.CompleteInterval(1,1,true).ok());
        index.ObserveNativeFinish();check(index.FinishAtApplicationBoundary(1).ok());
        const auto* storage=index.entries().data();
        index.Fail(FailureCode::ContextUnavailable); // Native end-hold binding/admission failed.
        check(index.witness().phase==ReplayTickIndex::Phase::Failed
            && index.witness().failure==FailureCode::ContextUnavailable && index.entries().data()==storage);
        index.SettleSourceRevision(0); // Matching inputs cannot resurrect an invalid native session.
        check(index.witness().phase==ReplayTickIndex::Phase::Failed);
        check(index.ReleaseAfterOwners(false).ok() && index.entries().data()==storage);
        check(index.ReleaseAfterOwners(true).ok() && index.witness().phase==ReplayTickIndex::Phase::Empty);
    }
    for(bool commit:{false,true}) {
        Sc6ReplayHost h;auto& index=h.index_;
        const auto check=[](bool ok){if(!ok)std::abort();};
        ReplayTickIndex::Entry row{};
        check(index.Begin(row,4096,7).ok());row.tick=row.native_tick=1;row.interval=1;
        check(index.Append(row).ok() && index.CompleteInterval(1,1,true).ok());
        index.ObserveNativeFinish();check(index.FinishAtApplicationBoundary(1).ok());
        const auto* storage=index.entries().data();const auto bytes=index.storage_bytes();
        check(Sc6ReplayHost::RequestIndexedSeek(1).ok());
        index.SuspendSourceRevision(8); // A published/revised while complete B is retained.
        check(index.witness().phase==ReplayTickIndex::Phase::SuspendedRevision);
        check(!Sc6ReplayHost::RequestIndexedSeek(1).ok());
        index.SuspendSourceRevision(9); // A second edit cannot overwrite the original history identity.
        index.SuspendSourceRevision(7); // B source installed, but undo/tails still pending.
        check(index.witness().phase==ReplayTickIndex::Phase::SuspendedRevision);
        check(index.entries().data()==storage && index.storage_bytes()==bytes && index.entries()[1]==row);
        index.SettleSourceRevision(commit?9:7); // Actual C revision, not the saved A revision.
        check(index.witness().phase==(commit?ReplayTickIndex::Phase::Failed:ReplayTickIndex::Phase::Complete));
        check(Sc6ReplayHost::RequestIndexedSeek(1).ok()==!commit);
        if(commit) {index.SettleSourceRevision(7);check(index.witness().phase==ReplayTickIndex::Phase::Failed);}
        check(index.ReleaseAfterOwners(false).ok());index.SettleSourceRevision(7);
        check(index.witness().phase==ReplayTickIndex::Phase::Releasing && index.entries().data()==storage);
        check(index.ReleaseAfterOwners(true).ok());index.SettleSourceRevision(7);
        check(index.witness().phase==ReplayTickIndex::Phase::Empty);
    }
    {
        using H=Sc6ReplayHost;using P=H::SeekPhase;using O=ReplaySeekOwnership;
        H h;const auto check=[](bool ok){if(!ok)std::abort();};
        check(!H::RequestIndexedSeek(208).ok());
        ReplayTickIndex::Entry row{};check(h.index_.Begin(row,16384).ok());
        check(!H::RequestIndexedSeek(208).ok()); // Partial map cannot drive controls.
        for(unsigned tick=1;tick<=230;++tick) {
            row.tick=row.native_tick=row.interval=tick;row.round=tick>=100?1:0;
            check(h.index_.Append(row).ok());check(h.index_.CompleteInterval(tick,tick,tick==230).ok());
        }
        h.index_.ObserveNativeFinish();check(h.index_.FinishAtApplicationBoundary(230).ok());
        for(const auto phase:{H::InteriorPhase::Idle,H::InteriorPhase::Arming,H::InteriorPhase::Armed,H::InteriorPhase::Releasing,H::InteriorPhase::Failed}) {
            h.interior_phase_=phase;
            check(!H::RequestIndexedSeek(208).ok() && !h.queued_indexed_seek_);
            check(!H::RequestIndexedRound(-1).ok() && !h.queued_indexed_seek_);
        }
        h.interior_phase_=H::InteriorPhase::Holding;
        check(!H::RequestIndexedSeek(231).ok());
        check(H::RequestIndexedRound(-1).ok() && h.queued_indexed_seek_==0);
        check(!H::RequestIndexedRound(1).ok());
        // A render event can defer same-tick completion beyond the update
        // that accepted UI input. The completion receipt belongs to the
        // actual queue consumer and must appear exactly once after the wait.
        check(H::RequestIndexedSeek(210).ok());
        h.surface_event_=true;h.DriveIndexedSeekIntent();
        check(h.queued_indexed_seek_==210 && !h.same_tick_completions);
        h.surface_event_=false;h.DriveIndexedSeekIntent();
        check(!h.queued_indexed_seek_ && h.same_tick_completions==1 && h.seek_.witness.phase==P::Idle);
        h.DriveIndexedSeekIntent();
        check(h.same_tick_completions==1 && !h.publications && !h.commits && !h.advances);
        auto image=std::make_shared<H::Checkpoint>();h.retained_checkpoints_={image};
        check(H::RequestIndexedSeek(208).ok());check(H::RequestIndexedSeek(209).ok());
        h.surface_event_=true;h.DriveIndexedSeekIntent();
        check(h.queued_indexed_seek_==209 && h.seek_.witness.phase==P::Idle);
        h.surface_event_=false;h.DriveIndexedSeekIntent();
        check(!h.queued_indexed_seek_ && h.seek_.witness.target==209 && h.seek_.witness.phase==P::Preparing);
        check(H::RequestIndexedSeek(206).ok());h.DriveIndexedSeekIntent();
        check(h.queued_indexed_seek_==206 && h.seek_.witness.target==209 && h.publications==0);
        // Controlled native owners settle the first request; the production
        // queue must request its tails and retain B before replacing anything.
        h.simulation_->state.tick=209;h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;
        h.historical_restore_=std::make_unique<H::Restore>();
        auto& restore=*h.historical_restore_;restore.ownership.emplace(220,209);
        check(restore.ownership->PublishA());check(restore.ownership->ActivateExecution());check(restore.ownership->SettleC(209));
        restore.witness.phase=H::RestoreOperationPhase::Held;
        h.DriveIndexedSeekIntent();
        check(h.seek_.witness.pending && h.queued_indexed_seek_==206 && h.commits==0
            && restore.ownership->phase()==O::Phase::CompletingTargetTails && restore.ownership->retains_undo());
        check(H::RequestIndexedSeek(207).ok());h.DriveIndexedSeekIntent();
        check(h.queued_indexed_seek_==207 && h.seek_.witness.target==209 && h.commits==0);
        // Only explicit completion by those owners permits the next Begin.
        h.historical_restore_.reset();h.seek_.witness.pending=false;h.seek_.witness.commit_decided=true;
        h.DriveIndexedSeekIntent();
        check(!h.queued_indexed_seek_ && h.seek_.witness.target==207 && h.seek_.witness.phase==P::Preparing);
        check(H::RequestIndexedSeek(206).ok());h.seek_.witness.phase=P::Failed;
        h.seek_.witness.failure=FailureCode::AdvanceFailed;h.DriveIndexedSeekIntent();
        check(!h.queued_indexed_seek_ && h.indexed_seek_failure_==FailureCode::AdvanceFailed && h.seek_.witness.phase==P::Failed);
        h.seek_.witness.phase=P::Idle;check(H::RequestIndexedSeek(206).ok());
        h.index_.InvalidateSourceRevision();h.DriveIndexedSeekIntent();
        check(!h.queued_indexed_seek_ && h.indexed_seek_failure_==FailureCode::GenerationMismatch && h.seek_.witness.phase==P::Idle);
    }
    for(const bool reject_recovery:{false,true}) {
        using H=Sc6ReplayHost;using P=H::SeekPhase;using R=H::RestoreOperationPhase;
        H h;unsigned check_number{};const auto check=[&](bool ok){++check_number;if(!ok){
            std::cerr<<"interior indexed replacement check="<<check_number<<" reject_recovery="<<reject_recovery<<'\n';std::abort();}};
        ReplayTickIndex::Entry row{};check(h.index_.Begin(row,16384).ok());
        for(unsigned tick=1;tick<=230;++tick) {
            row.tick=row.native_tick=row.interval=tick;
            check(h.index_.Append(row).ok());check(h.index_.CompleteInterval(tick,tick,tick==230).ok());
        }
        h.index_.ObserveNativeFinish();check(h.index_.FinishAtApplicationBoundary(230).ok());
        auto image=std::make_shared<H::Checkpoint>();h.retained_checkpoints_={image};
        H::SeekWitness witness{};check(H::SeekOperation(H::SeekAction::Begin,nullptr,208,&witness));
        h.AdvanceSeek();check(h.historical_restore_ && h.seek_.restore_requested);
        auto& restore=*h.historical_restore_;auto& owner=*restore.ownership;
        check(owner.PublishA() && owner.ActivateExecution() && owner.SettleC(208));
        restore.witness.phase=R::Held;restore.witness.pending=false;
        h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;
        h.simulation_->state.tick=208;h.pause_boundary_=H::PauseBoundary::SimulationTick;
        h.tick_advance_.phase=H::TickAdvancePhase::CompletionBlocked;
        h.cancel_allowed=!reject_recovery;
        check(H::RequestIndexedSeek(207).ok());h.DriveIndexedSeekIntent();
        check(h.cancels==1 && h.commits==0 && h.advances==0 && h.simulation_->state.tick==208 && owner.retains_undo());
        if(reject_recovery) {
            check(!h.queued_indexed_seek_ && h.indexed_seek_failure_==FailureCode::IllegalTransition
                && h.historical_restore_ && h.seek_.witness.phase==P::Held);continue;
        }
        check(h.queued_indexed_seek_==207 && h.seek_.witness.phase==P::Recovering && h.seek_.witness.pending);
        check(H::RequestIndexedSeek(206).ok());h.DriveIndexedSeekIntent();
        check(h.queued_indexed_seek_==206 && h.cancels==1 && h.seek_.witness.target==208);
        // Native recovery owns quiescing (which may finish a repeat), B
        // installation, and retirement. The production queue cannot skip it.
        check(owner.BeginRecovery() && owner.CompleteRecoveryTails(209) && owner.CompleteRecovery(210));
        restore.witness.phase=R::Recovered;restore.witness.pending=false;restore.witness.original_recovered=true;
        restore.witness.original_tick=210;h.simulation_->state.tick=210;
        h.pause_boundary_=H::PauseBoundary::CompletedApplication;h.tick_advance_={};
        h.release_allowed=false;h.AdvanceSeek();h.DriveIndexedSeekIntent();
        check(h.historical_restore_ && h.queued_indexed_seek_==206 && h.seek_.witness.phase==P::Recovering);
        h.release_allowed=true;h.AdvanceSeek();
        check(!h.historical_restore_ && h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered);
        h.DriveIndexedSeekIntent();
        // AdvanceSeek polls the underlying idempotent cancellation once on
        // each of the two recovery updates; the queue requested it only once.
        check(!h.queued_indexed_seek_ && h.seek_.witness.phase==P::Preparing && h.seek_.witness.target==206
            && h.seek_.witness.undo_tick==210 && h.cancels==3 && h.commits==0 && h.advances==0);
    }
    {
        Sc6ReplayHost h;using O=Sc6ReplayHost::CheckpointOwnership;
        const auto check=[](bool ok){if(!ok) std::abort();};
        auto image=std::make_shared<Sc6ReplayHost::Checkpoint>();auto* address=image.get();
        std::weak_ptr<const Sc6ReplayHost::Checkpoint> weak=image;h.retained_checkpoints_.push_back(image);
        std::size_t count{};
        h.bytes=Schema::replay_timeline_memory_limit;
        check(!Sc6ReplayHost::OwnCheckpoint(O::Retain,address,&count).ok() && count==0);
        h.bytes=1024;
        check(Sc6ReplayHost::OwnCheckpoint(O::Retain,address,&count).ok() && count==1);
        check(Sc6ReplayHost::OwnCheckpoint(O::Retain,address,&count).ok() && count==1);
        image.reset();check(!weak.expired()); // Only the host now owns A.
        Sc6ReplayHost::SeekWitness witness;
        check(Sc6ReplayHost::SeekOperation(Sc6ReplayHost::SeekAction::Begin,nullptr,208,&witness));
        check(Sc6ReplayHost::OwnCheckpoint(O::Release,address,&count).ok() && count==0 && !weak.expired());
        h.seek_.checkpoint.reset();check(weak.expired());
        check(!Sc6ReplayHost::OwnCheckpoint(O::Retain,address,&count).ok()); // No stale dereference.
        check(!Sc6ReplayHost::OwnCheckpoint(O::Release,address,&count).ok());
    }
    using H=Sc6ReplayHost;using A=H::SeekAction;using P=H::SeekPhase;using R=H::RestoreOperationPhase;
    const auto require=[](bool value){if(!value){std::cerr<<"replay seek transition test failed\n";std::abort();}};
    for(bool cancel:{false,true}) {
        H h;auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(cp);
        H::SeekWitness w;require(H::SeekOperation(A::Begin,nullptr,205,&w));
        for(unsigned update=0;update<24 && h.seek_.witness.phase!=P::Held;++update) {
            h.AdvanceSeek();
            if(!h.historical_restore_)continue;
            auto& r=h.historical_restore_->witness;
            if(r.phase==R::Preparing){r.phase=R::Prepared;r.pending=false;r.particle_birth=h.copy.state.birth_prepared;}
            else if(r.phase==R::Publishing){r.phase=R::Held;r.pending=false;h.simulation_->state.tick=205;}
        }
        require(h.seek_.witness.phase==P::Held && !h.seek_.witness.pending && h.advances==0 && h.commits==0);
        require(h.historical_restore_ && h.historical_restore_->ownership->retains_undo()
            && h.historical_restore_->ownership->phase()==ReplaySeekOwnership::Phase::CSettled);
        require(H::SeekOperation(cancel?A::Cancel:A::CompleteTarget,nullptr,0,&w));
        require(w.pending && !w.commit_decided);
        require(h.historical_restore_ && h.historical_restore_->ownership->retains_undo() && h.commits==0 && h.advances==0);
        if(!cancel)require(h.historical_restore_->ownership->phase()==ReplaySeekOwnership::Phase::CompletingTargetTails);
    }
    {
        H h;auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(cp);
        h.simulation_->state.tick=205;H::SeekWitness w;
        // Forward seeking from a previously restored checkpoint must retain B
        // at the original tick, without an unowned preliminary traversal.
        require(H::SeekOperation(A::Begin,nullptr,208,&w));
        require(w.checkpoint==205 && w.undo_tick==205 && h.advances==0 && h.commits==0);
        h.AdvanceSeek();require(h.historical_restore_ && h.historical_restore_->ownership->original_tick()==205);
        require(H::SeekOperation(A::Cancel,nullptr,0,&w));
        require(h.advances==0 && h.commits==0);
    }
    {
        H h;auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(cp);
        h.simulation_->state.tick=204;H::SeekWitness w;
        require(!H::SeekOperation(A::Begin,nullptr,208,&w));
        require(!h.historical_restore_ && h.advances==0 && h.commits==0);
    }
    for(int mode=0;mode<11;++mode) {
        H h;auto earlier=std::make_shared<H::Checkpoint>(),nearest=std::make_shared<H::Checkpoint>();
        nearest->execution.tick=207;h.retained_checkpoints_={earlier,nearest};H::SeekWitness w;
        require(H::SeekOperation(A::Begin,mode==1?nearest.get():nullptr,220,&w));h.AdvanceSeek();
        auto& tx=*h.historical_restore_;tx.execution=true;
        auto& r=tx.witness;r.phase=R::Failed;r.pending=false;r.original_tick=210;
        r.failure=FailureCode::UnsupportedContent;r.participant="execution_protected_particle_lifetime";
        r.ownership_phase=ReplaySeekOwnership::Phase::FailedRecoverable;
        h.seek_.witness.phase=P::Advancing;h.simulation_->state.tick=217;
        if(mode==2)h.retained_checkpoints_={nearest};
        if(mode==3)h.retry_inputs=false;
        if(mode==4)h.cancel_allowed=false;
        if(mode==5)r.participant="unproven_owner";
        h.AdvanceSeek();
        if(mode>=1 && mode<=5) {
            require(h.seek_.witness.phase==P::Failed && h.historical_restore_ && h.seek_.checkpoint==nearest
                && h.commits==0 && h.execution_fallback_traces==0);continue;
        }
        require(h.seek_.witness.phase==P::RetryingExecution && h.cancels==1 && h.seek_.checkpoint==nearest);
        h.AdvanceSeek();require(h.cancels==1 && h.historical_restore_ && h.seek_.witness.execution_fallbacks==0);
        if(mode==10) {
            r.phase=R::Failed;r.failure=FailureCode::UndoFailed;h.AdvanceSeek();
            require(h.seek_.witness.phase==P::Failed && h.historical_restore_ && h.cancels==1);continue;
        }
        h.simulation_->state.tick=210;r.phase=R::Recovered;r.pending=false;r.original_recovered=true;
        r.executed_ticks=r.executed_intervals=10;
        if(mode==6)r.original_recovered=false;
        if(mode==7)require(H::SeekOperation(A::Cancel,nullptr,0,&w));
        if(mode==8)h.retained_checkpoints_={nearest}; // Earlier owner disappears during recovery.
        if(mode==9)h.retry_inputs=false; // Changed admission cannot be lost during the asynchronous wait.
        h.release_allowed=false;h.AdvanceSeek();
        if(mode==6) {require(h.seek_.witness.phase==P::Failed && h.historical_restore_);continue;}
        require(h.historical_restore_ && h.seek_.checkpoint==nearest && h.execution_fallback_traces==0);
        h.release_allowed=true;h.AdvanceSeek();
        if(mode==7) {
            require(h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered
                && !h.historical_restore_ && h.seek_.witness.execution_fallbacks==0);continue;
        }
        if(mode==8 || mode==9) {
            require(h.seek_.witness.phase==P::Failed && !h.historical_restore_ && h.commits==0
                && h.seek_.witness.original_recovered && !h.seek_.witness.pending
                && h.seek_.witness.recovered_execution_ticks==10 && h.seek_.witness.recovered_execution_intervals==10
                && h.seek_.checkpoint==nearest && h.execution_fallback_traces==0);
            h.simulation_->state.tick=211;
            require(!H::SeekOperation(A::Cancel,nullptr,0,&w));
            h.simulation_->state.tick=210;
            require(H::SeekOperation(A::Cancel,nullptr,0,&w) && w.phase==P::Cancelled && w.original_recovered);
            require(H::SeekOperation(A::Cancel,nullptr,0,&w) && h.cancels==1);
            require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::AcceptedPending);
            h.AdvanceSeek();
            require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::Completed
                && !h.seek_.checkpoint && !h.seek_retirement_pending_ && h.commits==0);
            continue;
        }
        require(h.seek_.witness.phase==P::Preparing && h.seek_.checkpoint==earlier && !h.historical_restore_
            && h.released_seek_checkpoint_==nearest && h.seek_retirement_pending_ && h.commits==0
            && h.seek_.witness.execution_fallbacks==1 && h.seek_.witness.preparation_fallbacks==0
            && h.seek_.witness.fallback_execution_ticks==10 && h.seek_.witness.fallback_execution_intervals==10
            && h.execution_fallback_traces==1 && h.seek_.witness.undo_tick==210 && h.seek_.witness.target==220);
        h.retirement_blocked=true;h.AdvanceSeek();require(!h.historical_restore_);
        h.retirement_blocked=false;h.AdvanceSeek();require(h.historical_restore_ && !h.seek_retirement_pending_);
    }
    for(int mode=0;mode<7;++mode) {
        H h;auto earlier=std::make_shared<H::Checkpoint>(),nearest=std::make_shared<H::Checkpoint>();
        nearest->execution.tick=207;h.retained_checkpoints_={earlier,nearest};H::SeekWitness w;
        require(H::SeekOperation(A::Begin,mode==1?nearest.get():nullptr,208,&w));
        require(!H::SeekOperation(A::InjectPreparationFailure,nullptr,206,&w));
        require(H::SeekOperation(A::InjectPreparationFailure,nullptr,207,&w)==(mode!=1));
        require(!H::SeekOperation(A::InjectPreparationFailure,nullptr,207,&w));
        h.AdvanceSeek();require(h.historical_restore_!=nullptr);
        auto& operation=*h.historical_restore_;
        operation.ownership.reset();operation.preparation_retiring=true;
        operation.witness.phase=R::Failed;operation.witness.pending=false;
        operation.witness.failure=FailureCode::GenerationMismatch;operation.witness.original_tick=210;
        if(mode==2) {h.seek_.witness.phase=P::Publishing;operation.witness.ownership_phase=ReplaySeekOwnership::Phase::APublished;}
        if(mode==3) h.cancel_allowed=false;
        if(mode==6) operation.execution=true; // Partially constructed execution owner is not preparation-only.
        h.AdvanceSeek();
        if(mode==1 || mode==2 || mode==3 || mode==6) {
            require(h.seek_.witness.phase==P::Failed && h.historical_restore_ && h.seek_.checkpoint==nearest);
            require(h.cancels==(mode==3?1u:0u));continue;
        }
        require(h.seek_.witness.phase==P::RetryingPreparation && h.cancels==1 && h.seek_.checkpoint==nearest);
        h.AdvanceSeek();require(h.seek_.witness.phase==P::RetryingPreparation && h.cancels==1);
        operation.witness.phase=R::Recovered;operation.witness.pending=false;operation.witness.original_recovered=true;
        if(mode==4) {
            require(H::SeekOperation(A::Cancel,nullptr,0,&w));h.AdvanceSeek();
            require(h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered
                && !h.historical_restore_ && h.seek_.checkpoint==nearest && h.seek_.witness.preparation_fallbacks==0);
            continue;
        }
        if(mode==5) {
            operation.witness.original_recovered=false;h.AdvanceSeek();
            require(h.seek_.witness.phase==P::Failed && h.historical_restore_ && h.seek_.checkpoint==nearest);continue;
        }
        h.release_allowed=false;h.AdvanceSeek();
        require(h.historical_restore_ && h.seek_.checkpoint==nearest && h.seek_.witness.phase==P::RetryingPreparation);
        h.release_allowed=true;h.AdvanceSeek();
        require(!h.historical_restore_ && h.seek_.checkpoint==earlier && h.released_seek_checkpoint_==nearest
            && h.seek_retirement_pending_ && h.seek_.witness.phase==P::Preparing
            && h.seek_.witness.preparation_fallbacks==1 && h.seek_.witness.last_preparation_failure==FailureCode::GenerationMismatch
            && h.seek_.witness.target==208 && h.seek_.witness.undo_tick==210 && h.publications==0 && h.commits==0);
        h.retirement_blocked=true;h.AdvanceSeek();require(!h.historical_restore_ && h.seek_retirement_pending_);
        h.retirement_blocked=false;h.AdvanceSeek();require(h.historical_restore_ && !h.seek_retirement_pending_);
        h.historical_restore_->ownership.reset();h.historical_restore_->preparation_retiring=true;
        h.historical_restore_->witness.phase=R::Failed;h.historical_restore_->witness.pending=false;
        h.historical_restore_->witness.failure=FailureCode::GenerationMismatch;
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Failed && h.cancels==1 && h.seek_.checkpoint==earlier);
    }
    for(int mode=0;mode<8;++mode) {
        // Actual AdvanceSeek must hand a completed, never-published preparation
        // cancellation back to native Cancel. Acceptance is not B recovery:
        // the controlled dependency remains pending until its explicit receipt.
        H h;h.seek_.cancel=true;h.seek_.restore_requested=true;h.seek_.witness.phase=P::Recovering;
        h.seek_.witness.undo_tick=210;
        h.historical_restore_=std::make_unique<H::Restore>();
        auto& operation=*h.historical_restore_;
        operation.preparation_retiring=true;
        auto& r=operation.witness;r.phase=R::Failed;r.failure=FailureCode::Cancelled;
        r.original_tick=210;r.pending=false;
        if(mode==1)r.pending=true;
        if(mode==2)operation.preparing=true;
        if(mode==3)operation.execution=true;
        if(mode==4)r.ownership_phase=ReplaySeekOwnership::Phase::APublished;
        if(mode==5)operation.preparation_retiring=false;
        if(mode==6)r.failure=FailureCode::UndoFailed;
        if(mode==7)h.cancel_allowed=false;
        h.AdvanceSeek();
        if(mode) {
            require(h.seek_.witness.phase==P::Failed && h.historical_restore_
                && h.cancels==(mode==7?1u:0u) && !h.seek_.witness.original_recovered);
            const auto attempts=h.cancels;h.AdvanceSeek();require(h.cancels==attempts);
            continue;
        }
        require(h.seek_.witness.phase==P::Recovering && h.cancels==1
            && h.historical_restore_ && !h.seek_.witness.original_recovered && h.commits==0);
        r.phase=R::Recovered;r.pending=true;r.original_recovered=false;
        h.AdvanceSeek();require(h.historical_restore_ && !h.seek_.witness.original_recovered);
        r.pending=false;r.original_recovered=true;h.release_allowed=false;
        h.AdvanceSeek();require(h.historical_restore_ && h.seek_.witness.phase==P::Recovering);
        h.release_allowed=true;h.AdvanceSeek();
        require(!h.historical_restore_ && h.seek_.witness.phase==P::Cancelled
            && h.seek_.witness.original_recovered && !h.interior_resume_requested_ && h.commits==0);
    }
    {
        H h;h.seek_.cancel=true;h.seek_.restore_requested=true;h.seek_.witness.phase=P::Recovering;
        h.historical_restore_=std::make_unique<H::Restore>();
        h.historical_restore_->witness.phase=R::Failed;
        h.historical_restore_->witness.failure=FailureCode::UndoFailed;
        h.AdvanceSeek();h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Failed && h.cancels==0 && h.historical_restore_);
        H::SeekWitness witness{};
        require(H::SeekOperation(A::Cancel,nullptr,0,&witness) && h.cancels==1);
    }
    {
        ReplaySeekOwnership owner(220,208);
        using O=ReplaySeekOwnership::Phase;
        require(owner.retains_undo() && !owner.BeginCommit() && !owner.ActivateExecution());
        require(owner.PublishA() && !owner.PublishA() && !owner.BeginCommit());
        require(owner.ActivateExecution() && !owner.SettleC(207) && !owner.BeginCommit());
        require(owner.SettleC(208) && owner.phase()==O::CSettled && owner.retains_undo());
        // Exact interior readiness is not completed application/render work.
        require(!owner.BeginCommit() && !owner.CompleteTargetTails(207));
        require(!owner.CompleteTargetTails(209) && owner.retains_undo());
        require(owner.BeginResumedTails() && owner.CompleteTargetTails(209) && owner.completed_tail_tick()==209);
        require(owner.BeginCommit() && !owner.retains_undo() && !owner.BeginRecovery());
        owner.Fail();require(owner.phase()==O::FailedTerminal && !owner.BeginRecovery());
    }
    {
        ReplaySeekOwnership owner(220,208);
        require(owner.PublishA() && owner.ActivateExecution());
        owner.Fail();require(owner.retains_undo() && owner.BeginRecovery());
        // Quiescing a failed interior advance may complete another traversal.
        // Recovery must still return to the retained B coordinate, not C.
        require(!owner.CompleteRecovery(220) && owner.CompleteRecoveryTails(207));
        require(!owner.CompleteRecovery(207) && !owner.BeginCommit());
        require(owner.CompleteRecovery(220) && !owner.retains_undo());
    }
    {
        ReplaySeekOwnership owner(220,208);
        using C=ReplaySeekOwnership::Cancellation;
        require(owner.PublishA() && owner.ActivateExecution() && owner.SettleC(208));
        require(owner.RequestCancellation()==C::Quiesce && owner.RequestCancellation()==C::Quiesce);
        require(owner.CompleteRecoveryTails(209));
        require(owner.RequestCancellation()==C::Undo && owner.phase()==ReplaySeekOwnership::Phase::Recovering);
        require(owner.RequestCancellation()==C::Undo && owner.CompleteRecovery(220));
        require(owner.RequestCancellation()==C::Recovered && !owner.retains_undo());
        ReplaySeekOwnership committed(220,208);
        require(committed.PublishA() && committed.ActivateExecution() && committed.SettleC(208)
            && committed.CompleteTargetTails(208) && committed.BeginCommit());
        require(committed.RequestCancellation()==C::Rejected && committed.CompleteCommit());
        require(committed.RequestCancellation()==C::Rejected);
    }
    const auto begin=[&](H& h){auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(cp);
        H::SeekWitness w;require(H::SeekOperation(A::Begin,cp.get(),208,&w));};
    for(bool cancel:{false,true}) {
        // Rollback returns to B's coordinate through an explicitly owned A.
        // Equal coordinates do not establish either publication or completion.
        H h;auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(cp);
        H::SeekWitness w;
        require(!H::SeekOperation(A::Begin,nullptr,210,&w));
        require(!H::SeekOperation(A::Begin,reinterpret_cast<const H::Checkpoint*>(1),210,&w));
        cp->execution.tick=210;require(!H::SeekOperation(A::Begin,cp.get(),210,&w));cp->execution.tick=205;
        require(H::SeekOperation(A::Begin,cp.get(),210,&w));
        require(w.origin==210 && w.undo_tick==210 && w.target==210 && w.checkpoint==205 && !w.automatic_selection);
        h.AdvanceSeek();auto& native=h.historical_restore_->witness;
        require(h.historical_restore_->ownership->retains_undo() && h.publications==0 && h.commits==0);
        native.phase=R::Prepared;native.pending=false;native.particle_birth=h.copy.state.birth_prepared;h.AdvanceSeek();
        require(h.publications==1 && h.commits==0);
        native.phase=R::Held;native.pending=false;h.simulation_->state.tick=205;
        for(unsigned update=0;update<4 && h.seek_.witness.phase!=P::Restored;++update)h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Restored && h.commits==0);
        h.AdvanceSeek();require(h.advances==1 && h.requested_tick==210 && h.historical_restore_->ownership->retains_undo());
        h.simulation_->state.tick=210;h.tick_advance_.phase=H::TickAdvancePhase::Held;h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Held && h.commits==0 && h.historical_restore_->ownership->retains_undo());
        require(H::SeekOperation(cancel?A::Cancel:A::CompleteTarget,nullptr,0,&w));
        require(w.pending && !w.commit_decided && h.commits==0 && h.historical_restore_->ownership->retains_undo());
    }
    {
        // Exercise production Begin -> request rejection -> the same Resume
        // observer used by the UI. The underlying restore admission is the
        // fixture boundary; dispatch and deferred release are production code.
        H h;h.request_allowed=false;begin(h);const auto tick=h.simulation_->state.tick;
        const std::weak_ptr<const H::Checkpoint> pin=h.seek_.checkpoint;
        h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Failed && !h.seek_.witness.pending && !h.historical_restore_);
        H::InteriorWitness held{};held.tick=tick;held.ui_step_requests=1;
        require(!H::ObserveSeekHold(&h,held) && !pin.expired() && h.advances==0);
        held.ui_cancel_requests=1;
        require(!H::ObserveSeekHold(&h,held) && h.seek_.witness.phase==P::Failed && h.cancels==0);
        held.ui_resume_requests=1;h.copy.blocked=true;
        require(!H::ObserveSeekHold(&h,held) && !h.seek_retirement_pending_ && !pin.expired());
        h.copy.blocked=false;h.retirement_blocked=true;
        require(!H::ObserveSeekHold(&h,held) && h.seek_retirement_pending_ && !pin.expired()
            && h.seek_.witness.release_result==H::SeekReleaseResult::AcceptedPending);
        h.AdvanceSeek();
        require(!H::ObserveSeekHold(&h,held) && !pin.expired() && h.seek_.witness.pending);
        h.retirement_blocked=false;h.AdvanceSeek();
        require(pin.expired() && h.seek_.witness.failure==FailureCode::RestorePreflightFailed
            && h.seek_.witness.release_result==H::SeekReleaseResult::Completed);
        require(H::ObserveSeekHold(&h,held) && !h.SeekOwnsExecution() && h.advances==0
            && h.simulation_->state.tick==tick && h.publications==0 && h.commits==0);
        h.request_allowed=true;begin(h);h.AdvanceSeek();require(h.historical_restore_!=nullptr);
    }
    {
        H h;begin(h);h.AdvanceSeek();h.seek_.witness.phase=P::Failed;h.seek_.witness.pending=false;
        H::InteriorWitness held{};held.ui_resume_requests=1;
        require(!H::ObserveSeekHold(&h,held) && h.historical_restore_ && !h.seek_retirement_pending_
            && h.seek_.witness.release_result==H::SeekReleaseResult::NotRequested);
    }
    {
        H h;begin(h);h.AdvanceSeek();
        auto& owner=*h.historical_restore_->ownership;
        require(owner.BeginRecovery() && owner.CompleteRecoveryTails(170));
        auto& native=h.historical_restore_->witness;
        native.phase=R::Recovered;native.pending=true;native.original_recovered=false;
        h.seek_.cancel=true;h.seek_.witness.phase=P::Recovering;
        h.simulation_->state.tick=h.seek_.witness.undo_tick;
        for(unsigned update=0;update<4;++update) {
            require(native.AcceptsIdempotentCancellation(owner.RequestCancellation()));
            h.AdvanceSeek();
            require(h.seek_.witness.phase==P::Recovering && h.historical_restore_
                && !h.seek_.witness.original_recovered && !h.interior_resume_requested_ && owner.retains_undo());
        }
        native.pending=false;
        require(!native.AcceptsIdempotentCancellation(owner.RequestCancellation()));
        native.pending=true;native.original_recovered=true;
        require(!native.AcceptsIdempotentCancellation(owner.RequestCancellation()));
        native.original_recovered=false;native.commit_decided=true;
        require(!native.AcceptsIdempotentCancellation(owner.RequestCancellation()));
        native.commit_decided=false;
        require(owner.CompleteRecovery(h.seek_.witness.undo_tick));
        native.pending=false;native.original_recovered=true;
        require(native.AcceptsIdempotentCancellation(owner.RequestCancellation()));
        h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered && !h.historical_restore_);
    }
    {
        H h;begin(h);h.AdvanceSeek();h.seek_.witness.phase=P::Failed;
        h.historical_restore_->witness.phase=R::Failed;
        h.historical_restore_->witness.pending=false;
        H::InteriorWitness held{};held.tick=220;held.ui_cancel_requests=1;
        held.surface_pending=true;
        require(!H::ObserveSeekHold(&h,held) && h.cancels==0 && h.interior_ui_cancel_baseline_==0);
        held.surface_pending=false;
        h.cancel_allowed=false;
        require(!H::ObserveSeekHold(&h,held) && h.cancels==1 && h.seek_.witness.phase==P::Failed);
        h.cancel_allowed=true;
        require(!H::ObserveSeekHold(&h,held) && h.cancels==1); // One click is one admission attempt.
        held.ui_cancel_requests=2;
        require(!H::ObserveSeekHold(&h,held) && h.cancels==2 && h.seek_.witness.phase==P::Recovering);
        require(!h.interior_resume_requested_ && h.historical_restore_);
        h.historical_restore_->witness.phase=R::Recovered;h.historical_restore_->witness.pending=false;
        h.historical_restore_->witness.original_recovered=true;h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Cancelled && !h.historical_restore_);
        h.seek_.observer=[](void*,const H::SeekWitness&,const H::InteriorWitness&) {};
        held.ui_resume_requests=1;
        require(!H::ObserveSeekHold(&h,held) && h.SeekOwnsExecution());
        h.AdvanceSeek();
        require(H::ObserveSeekHold(&h,held) && !h.SeekOwnsExecution() && !h.seek_retirement_pending_);
    }
    {
        H h;ReplayTickIndex::Entry row{};require(h.index_.Begin(row,512*sizeof(row)).ok());
        for(unsigned tick=1;tick<=210;++tick) {row.tick=row.native_tick=tick;row.interval=1;require(h.index_.Append(row).ok());}
        require(h.index_.CompleteInterval(1,210,true).ok());h.index_.ObserveNativeFinish();
        require(h.index_.FinishAtApplicationBoundary(210).ok());
        h.simulation_->state.tick=210;h.seek_.witness.phase=P::Held;h.seek_.witness.target=210;
        H::InteriorWitness endpoint{};endpoint.tick=210;endpoint.ui_resume_requests=1;endpoint.ui_step_requests=1;
        const auto before_advances=h.advances;
        H::SeekWitness rejected;
        require(!H::SeekOperation(A::Step,nullptr,0,&rejected));
        require(!H::ObserveSeekHold(&h,endpoint) && h.advances==before_advances
            && h.seek_.witness.phase==P::Held && !h.seek_.witness.pending && h.commits==0);
        h.seek_={};h.simulation_->state.tick=220;
        auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(cp);H::SeekWitness w;
        require(!H::SeekOperation(A::Begin,cp.get(),211,&w) && !h.SeekOwnsExecution() && !h.historical_restore_);
        require(H::SeekOperation(A::Begin,cp.get(),208,&w));
    }
    {
        H h;ReplayTickIndex::Entry row{};require(h.index_.Begin(row,512*sizeof(row)).ok());
        require(h.index_.Cancel());require(h.index_.ReleaseAfterOwners(false).ok());
        auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_.push_back(cp);H::SeekWitness w;
        require(!H::SeekOperation(A::Begin,cp.get(),208,&w) && !h.SeekOwnsExecution());
        require(!H::SeekOperation(A::Begin,nullptr,208,&w) && !h.historical_restore_);
        require(h.index_.ReleaseAfterOwners(true).ok());
        require(H::SeekOperation(A::Begin,cp.get(),208,&w));
    }
    {
        H h;auto earlier=std::make_shared<H::Checkpoint>(),unsupported=std::make_shared<H::Checkpoint>();
        unsupported->execution.tick=207;unsupported->supported=false;
        h.retained_checkpoints_={earlier,unsupported};H::SeekWitness w;
        require(!H::SeekOperation(A::Begin,unsupported.get(),208,&w) && !h.SeekOwnsExecution());
        require(H::SeekOperation(A::Begin,nullptr,208,&w) && w.checkpoint==205 && w.target==208);
        require(h.seek_.checkpoint==earlier && h.publications==0 && !h.historical_restore_);
    }
    {
        H h;H::SeekWitness w;
        // A nonnull public address is not permission to dereference it. This
        // exercises production Seek.inl with an inaccessible address and an
        // expired registry entry, without invoking any native owner.
        require(!H::SeekOperation(A::Begin,reinterpret_cast<const H::Checkpoint*>(1),208,&w));
        auto expired=std::make_shared<H::Checkpoint>();const auto* address=expired.get();
        h.retained_checkpoints_.push_back(expired);expired.reset();
        require(!H::SeekOperation(A::Begin,address,208,&w));
        require(!h.SeekOwnsExecution() && !h.historical_restore_ && !h.publications && !h.advances);
    }
    {
        H h;auto earlier=std::make_shared<H::Checkpoint>(),expired=std::make_shared<H::Checkpoint>();
        earlier->execution.tick=170;expired->execution.tick=205;expired->vfx.live=false;
        h.simulation_->state.tick=220;h.retained_checkpoints_={earlier,expired};H::SeekWitness w;
        require(H::SeekOperation(A::Begin,nullptr,208,&w) && w.checkpoint==170
            && w.automatic_selection && w.retained_owner_rejected && w.rejected_checkpoint==205);
        require(h.seek_.checkpoint==earlier && h.publications==0 && !h.historical_restore_);
    }
    {
        H h;auto cp=std::make_shared<H::Checkpoint>();h.retained_checkpoints_={cp};H::SeekWitness w;
        require(H::SeekOperation(A::Begin,nullptr,208,&w));
        h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;h.seek_.witness.commit_decided=true;
        h.simulation_->state.tick=208;h.pause_boundary_=H::PauseBoundary::SimulationTick;
        h.seek_.context=&h;
        // The original image can cease to be a valid historical target while
        // its already-committed seek still owns a correct interior hold.
        cp->vfx.live=false;cp->supported=false;
        require(H::SeekOperation(A::Begin,nullptr,208,&w));
        require(w.phase==P::Held && w.commit_decided && h.seek_.checkpoint==cp && h.seek_.context==&h
            && h.tick_advance_.phase==H::TickAdvancePhase::Idle && !h.seek_retirement_pending_
            && h.publications==0 && h.advances==0 && !h.historical_restore_);
        require(!H::SeekOperation(A::Begin,nullptr,207,&w));
        h.valid_binding=false;require(!H::SeekOperation(A::Begin,nullptr,208,&w));h.valid_binding=true;
        h.seek_.witness.pending=true;require(!H::SeekOperation(A::Begin,nullptr,208,&w));h.seek_.witness.pending=false;
        h.seek_retirement_pending_=true;require(!H::SeekOperation(A::Begin,nullptr,208,&w));h.seek_retirement_pending_=false;
        h.seek_.witness.phase=P::Cancelled;require(!H::SeekOperation(A::Begin,nullptr,208,&w));
        require(h.simulation_->state.tick==208 && h.publications==0 && !h.historical_restore_);
    }
    {
        H h;auto expired=std::make_shared<H::Checkpoint>();expired->vfx.live=false;
        h.retained_checkpoints_={expired};H::SeekWitness w;
        require(!H::SeekOperation(A::Begin,nullptr,208,&w) && !h.SeekOwnsExecution());
        // Explicit requests retain complete-B preparation/recovery semantics;
        // the selection filter cannot turn their rejection into silent fallback.
        require(H::SeekOperation(A::Begin,expired.get(),208,&w)
            && !w.automatic_selection && !w.retained_owner_rejected);
    }
    {
        H h;h.surface_event_=true;
        require(h.CanRetireSeekCheckpoint());
        h.pause_boundary_=H::PauseBoundary::SimulationTick;require(!h.CanRetireSeekCheckpoint());
        h.interior_phase_=H::InteriorPhase::Releasing;require(!h.CanRetireSeekCheckpoint());
        h.interior_phase_=H::InteriorPhase::Resumed;require(h.CanRetireSeekCheckpoint());
        h.engine_post_deferred_=true;require(!h.CanRetireSeekCheckpoint());
    }
    {
        H h;h.simulation_->state.tick=208;
        h.historical_restore_=std::make_unique<H::Restore>();
        auto& r=*h.historical_restore_;r.ownership.emplace(220,208);
        require(r.ownership->PublishA() && r.ownership->ActivateExecution() && r.ownership->SettleC(208));
        r.witness.phase=R::Held;r.witness.ownership_phase=r.ownership->phase();
        h.seek_.witness.phase=P::Held;h.seek_.witness.target=208;h.seek_.witness.undo_tick=220;h.seek_.restore_requested=true;
        bool called=false;h.seek_.context=&called;
        h.seek_.observer=[](void* p,const H::SeekWitness&,const H::InteriorWitness&){*static_cast<bool*>(p)=true;};
        H::InteriorWitness held{};held.tick=208;held.ui_step_requests=1;
        require(!H::ObserveSeekHold(&h,held) && called && h.advances==1 && h.requested_tick==209);
        require(h.seek_.witness.phase==P::Advancing && h.seek_.witness.pending && h.commits==0
            && r.ownership->retains_undo() && r.ownership->original_tick()==220);
        H::SeekWitness w;require(!H::SeekOperation(A::Step,nullptr,0,&w));
        h.simulation_->state.tick=209;h.tick_advance_.phase=H::TickAdvancePhase::Held;h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Held && !h.seek_.witness.pending && r.ownership->target_tick()==209 && h.commits==0);
        require(H::SeekOperation(A::Cancel,nullptr,0,&w) && h.cancels==1);
    }
    {
        H h;begin(h);h.AdvanceSeek();require(h.historical_restore_!=nullptr);
        auto& r=h.historical_restore_->witness;r.phase=R::Prepared;r.pending=false;r.particle_birth=true;
        h.surface_event_=true;h.AdvanceSeek();require(h.publications==0);
        h.surface_event_=false;h.AdvanceSeek();require(h.publications==1);
        h.AdvanceSeek();require(h.publications==1 && h.commits==0);
        r.phase=R::Held;r.pending=true;h.AdvanceSeek();require(h.commits==0);
        r.pending=false;h.AdvanceSeek();require(h.seek_.witness.phase==P::AwaitingCommit && h.commits==0);
        h.AdvanceSeek();require(h.commits==0 && h.historical_restore_);
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Restored && h.advances==0);
        h.AdvanceSeek();require(h.advances==1);
        h.simulation_->state.tick=208;h.tick_advance_.phase=H::TickAdvancePhase::Held;h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Held && !h.seek_.witness.pending && h.commits==0);
        require(h.historical_restore_->ownership->retains_undo());
        H::SeekWitness w;
        require(H::SeekOperation(A::Begin,nullptr,208,&w) && h.advances==1 && h.commits==0);
        H::RestoreOperationWitness premature{};
        require(!H::RestoreOperation(H::RestoreOperationAction::Commit,nullptr,&premature) && h.commits==0);
        h.surface_event_=true;h.surface_command_=H::SurfaceCommand::Install;
        require(!H::SeekOperation(A::Release,nullptr,0,&w));
        h.surface_event_=false;
        const auto pin=h.seek_.checkpoint;
        require(H::SeekOperation(A::Release,nullptr,0,&w));
        require(w.release_result==H::SeekReleaseResult::AcceptedPending);
        require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::AcceptedPending);
        require(w.pending && h.commits==0 && h.historical_restore_->ownership->retains_undo());
        require(h.seek_.checkpoint==pin && !H::SeekOperation(A::CompleteTarget,nullptr,0,&w));
        require(!H::SeekOperation(A::Begin,nullptr,214,&w) && h.seek_.checkpoint==pin && h.commits==0);
        // Paused completion cannot silently drain a repeat beyond target.
        require(!h.historical_restore_->ownership->CompleteTargetTails(209));
        require(h.historical_restore_->ownership->CompleteTargetTails(208));
        H::RestoreOperationWitness committing{};
        require(H::RestoreOperation(H::RestoreOperationAction::Commit,nullptr,&committing));
        h.AdvanceSeek();require(!H::SeekOperation(A::Cancel,nullptr,0,&w));
        require(h.historical_restore_->ownership->CompleteCommit());
        r.phase=R::Committed;r.pending=false;
        h.release_allowed=false;h.AdvanceSeek();require(h.historical_restore_!=nullptr);
        h.release_allowed=true;h.AdvanceSeek();require(!h.historical_restore_ && !h.seek_.witness.pending);
        h.pause_boundary_=H::PauseBoundary::CompletedApplication;
        require(H::SeekOperation(A::CompleteTarget,nullptr,0,&w) && h.seek_.checkpoint==pin && !h.seek_retirement_pending_);
        h.surface_event_=true;h.surface_command_=H::SurfaceCommand::Draw;require(H::SeekOperation(A::Release,nullptr,0,&w));
        require(h.surface_event_ && w.target==208 && h.SeekOwnsExecution() && w.release_result==H::SeekReleaseResult::AcceptedPending);
        h.retirement_blocked=true;h.AdvanceSeek();require(h.seek_.witness.pending);
        h.surface_event_=false;h.retirement_blocked=false;h.AdvanceSeek();
        require(h.seek_.witness.release_result==H::SeekReleaseResult::Completed);
        require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::Completed && !h.SeekOwnsExecution());
        require(!h.seek_retirement_pending_ && !h.released_seek_checkpoint_);
    }
    {
        H h;begin(h);h.AdvanceSeek();auto& r=h.historical_restore_->witness;
        r.phase=R::Failed;r.pending=false;h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Failed);
        h.AdvanceSeek();h.AdvanceSeek();
        require(h.historical_restore_ && h.seek_.witness.pending);
        H::SeekWitness w;require(H::SeekOperation(A::Cancel,nullptr,0,&w));
        r.phase=R::Recovered;r.original_recovered=true;r.pending=false;
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered);
    }
    {
        H h;h.simulation_->state.tick=208;
        h.seek_.witness.phase=P::Held;h.seek_.witness.target=208;h.seek_.witness.undo_tick=220;
        h.seek_.restore_requested=true;h.historical_restore_=std::make_unique<H::Restore>();
        auto& r=*h.historical_restore_;r.ownership.emplace(220,208);
        require(r.ownership->PublishA() && r.ownership->ActivateExecution() && r.ownership->SettleC(208));
        r.witness.phase=R::Held;r.witness.ownership_phase=r.ownership->phase();
        H::SeekWitness w;
        require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::AcceptedPending);
        require(r.ownership->retains_undo() && h.commits==0);
        require(H::SeekOperation(A::Cancel,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::NotRequested);
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Recovering && h.historical_restore_);
        h.simulation_->state.tick=220;r.witness.phase=R::Recovered;r.witness.pending=false;r.witness.original_recovered=true;
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered && h.commits==0);
    }
    for(bool settle:{false,true}) {
        H h;begin(h);const auto pin=h.seek_.checkpoint;
        h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;h.pause_boundary_=H::PauseBoundary::SimulationTick;
        h.settle_allowed=settle;H::SeekWitness w;
        require(H::SeekOperation(A::Release,nullptr,0,&w)==settle);
        require(w.release_result==(settle?H::SeekReleaseResult::AcceptedPending:H::SeekReleaseResult::Rejected));
        require(h.seek_.checkpoint==pin && !h.seek_retirement_pending_);
        if(!settle) {require(!h.seek_.witness.pending);continue;}
        h.AdvanceSeek();require(h.seek_.checkpoint==pin && h.seek_.witness.pending);
        require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::AcceptedPending);
        h.simulation_->state.tick=212;h.tick_advance_.phase=H::TickAdvancePhase::Settled;
        h.pause_boundary_=H::PauseBoundary::CompletedApplication;
        h.AdvanceSeek();require(h.seek_retirement_pending_ && h.released_seek_checkpoint_==pin);
        h.retirement_blocked=true;h.AdvanceSeek();require(h.seek_.witness.pending);
        h.retirement_blocked=false;h.AdvanceSeek();
        require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::Completed);
        require(!h.SeekOwnsExecution() && !h.seek_retirement_pending_ && h.simulation_->state.tick==212);
        require(H::SeekOperation(A::Release,nullptr,0,&w) && w.release_result==H::SeekReleaseResult::Completed);
    }
    for(bool spontaneous:{false,true}) {
        H h;begin(h);h.AdvanceSeek();auto& r=h.historical_restore_->witness;
        r.phase=spontaneous?R::Recovering:R::Failed;r.pending=true;
        h.AdvanceSeek();H::SeekWitness w;
        if(!spontaneous){require(h.seek_.witness.phase==P::Failed);h.cancel_allowed=false;
            require(!H::SeekOperation(A::Cancel,nullptr,0,&w));h.cancel_allowed=true;
            require(H::SeekOperation(A::Cancel,nullptr,0,&w));}
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Recovering);
        r.phase=R::Recovered;r.pending=false;r.original_recovered=true;h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Cancelled && h.seek_.witness.original_recovered && !h.historical_restore_);
    }
    {
        H h;begin(h);h.AdvanceSeek();h.bytes=Schema::replay_timeline_memory_limit+1;
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Failed);
        H::SeekWitness w;require(H::SeekOperation(A::Cancel,nullptr,0,&w));
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Recovering && h.seek_.witness.pending);
        auto& r=h.historical_restore_->witness;r.phase=R::Recovered;r.pending=false;r.original_recovered=true;
        h.AdvanceSeek();require(h.seek_.witness.phase==P::Cancelled && !h.historical_restore_);
    }
    for(auto completed:{210ull,212ull}) {
        H h;h.pause_boundary_=H::PauseBoundary::SimulationTick;begin(h);H::SeekWitness w;
        require(H::SeekOperation(A::Cancel,nullptr,0,&w));h.AdvanceSeek();
        require(h.seek_.witness.phase==P::CompletingApplication && h.seek_.witness.pending);
        h.tick_advance_.phase=H::TickAdvancePhase::Settled;h.simulation_->state.tick=completed;
        h.AdvanceSeek();h.AdvanceSeek();require(h.seek_.witness.phase==P::Cancelled);
        require(h.seek_.witness.undo_tick==completed && h.seek_.witness.original_recovered);
        require(!h.historical_restore_);
    }
    for(auto publication:{R::Prepared,R::Publishing,R::Held}) {
        H h;begin(h);h.AdvanceSeek();auto& r=h.historical_restore_->witness;
        r.phase=publication;r.pending=publication==R::Publishing;
        h.seek_.witness.undo_tick=212;H::SeekWitness w;
        require(H::SeekOperation(A::Cancel,nullptr,0,&w) && h.cancels==1);
        h.AdvanceSeek();require(h.commits==0 && h.publications==0 && h.seek_.witness.pending);
        r.phase=R::Recovered;r.pending=false;r.original_recovered=true;h.AdvanceSeek();
        require(h.seek_.witness.phase==P::Cancelled && !h.seek_.witness.original_recovered);
    }
    {
        H h;begin(h);h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;
        bool stable=false;h.seek_.context=&stable;
        h.seek_.observer=[](void* context,const H::SeekWitness& observed,const H::InteriorWitness&){
            H::SeekWitness released;
            const bool accepted=H::SeekOperation(A::Release,nullptr,0,&released);
            *static_cast<bool*>(context)=accepted && observed.phase==P::Held && observed.target==208;
        };
        require(!H::ObserveSeekHold(&h,{}) && stable && h.SeekOwnsExecution());
        h.AdvanceSeek();require(!H::ObserveSeekHold(&h,{}) && stable && !h.SeekOwnsExecution());
    }
    {
        H h;begin(h);h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;
        H::InteriorWitness held{};held.tick=208;held.ui_step_requests=2;
        h.surface_event_=true;h.surface_command_=H::SurfaceCommand::Install;
        require(!H::ObserveSeekHold(&h,held) && h.advances==0 && h.SeekOwnsExecution());
        h.surface_event_=false;
        require(!H::ObserveSeekHold(&h,held) && h.advances==0 && h.SeekOwnsExecution());
        require(h.seek_retirement_pending_ && h.released_seek_checkpoint_);
        h.AdvanceSeek();
        require(!H::ObserveSeekHold(&h,held) && h.advances==1 && h.requested_tick==209 && !h.SeekOwnsExecution());
    }
    {
        H h;begin(h);h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;
        H::InteriorWitness held{};held.tick=208;held.ui_resume_requests=1;
        require(!H::ObserveSeekHold(&h,held) && h.SeekOwnsExecution() && h.advances==0);
        h.AdvanceSeek();require(H::ObserveSeekHold(&h,held) && !h.SeekOwnsExecution() && h.advances==0);
    }
    {
        H h;begin(h);auto cp=h.seek_.checkpoint;
        h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;h.seek_.witness.commit_decided=true;
        h.simulation_->state.tick=208;h.pause_boundary_=H::PauseBoundary::SimulationTick;
        H::SeekWitness w;
        require(H::SeekOperation(A::Begin,cp.get(),208,&w));
        require(w.phase==P::Held && h.tick_advance_.phase==H::TickAdvancePhase::Idle);
        auto other=std::make_shared<H::Checkpoint>();other->valid=false;h.retained_checkpoints_.push_back(other);
        require(!H::SeekOperation(A::Begin,other.get(),207,&w));
        require(!H::SeekOperation(A::Begin,cp.get(),204,&w));
        require(h.seek_.witness.phase==P::Held && h.seek_.checkpoint==cp);
        h.settle_allowed=false;
        require(!H::SeekOperation(A::Begin,cp.get(),207,&w));
        require(h.seek_.witness.phase==P::Held && h.seek_.witness.target==208 && h.seek_.checkpoint==cp);
        h.settle_allowed=true;
        require(H::SeekOperation(A::Begin,cp.get(),207,&w));
        require(w.phase==P::CompletingApplication && w.origin==208 && w.target==207 && !w.commit_decided);
        require(!h.seek_retirement_pending_ && h.seek_.checkpoint==cp);
        require(!H::SeekOperation(A::Begin,cp.get(),206,&w));
        h.tick_advance_.phase=H::TickAdvancePhase::Settled;h.simulation_->state.tick=210;
        h.AdvanceSeek();h.AdvanceSeek();
        require(h.seek_.witness.undo_tick==210 && h.historical_restore_);
    }
    {
        H h;auto earlier=std::make_shared<H::Checkpoint>(),nearest=std::make_shared<H::Checkpoint>();
        nearest->execution.tick=207;
        h.retained_checkpoints_={earlier,nearest};H::SeekWitness w;
        require(H::SeekOperation(A::Begin,nullptr,208,&w) && w.checkpoint==207);
        h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;h.seek_.witness.commit_decided=true;
        h.simulation_->state.tick=208;h.pause_boundary_=H::PauseBoundary::SimulationTick;
        std::weak_ptr<const H::Checkpoint> old=nearest;nearest.reset();
        require(H::SeekOperation(A::Begin,nullptr,206,&w));
        require(w.phase==P::CompletingApplication && w.checkpoint==205 && h.seek_.checkpoint==earlier);
        require(h.seek_retirement_pending_ && !old.expired());
        h.AdvanceSeek();require(!old.expired() && !h.historical_restore_);
        h.tick_advance_.phase=H::TickAdvancePhase::Settled;
        h.pause_boundary_=H::PauseBoundary::CompletedApplication;h.retirement_blocked=true;
        h.AdvanceSeek();h.AdvanceSeek();
        require(!old.expired() && !h.historical_restore_ && h.seek_.witness.pending);
        require(!H::SeekOperation(A::Release,nullptr,0,&w));
        h.retirement_blocked=false;h.AdvanceSeek();
        require(old.expired() && !h.seek_retirement_pending_ && h.historical_restore_);
    }
    {
        H h;auto earlier=std::make_shared<H::Checkpoint>(),nearest=std::make_shared<H::Checkpoint>();
        nearest->execution.tick=207;
        h.retained_checkpoints_={earlier,nearest};H::SeekWitness w;
        require(H::SeekOperation(A::Begin,nullptr,208,&w) && w.checkpoint==207);
        h.seek_.witness.phase=P::Held;h.seek_.witness.pending=false;h.seek_.witness.commit_decided=true;
        h.simulation_->state.tick=208;h.pause_boundary_=H::PauseBoundary::SimulationTick;
        require(H::SeekOperation(A::Begin,nullptr,209,&w));
        require(w.phase==P::CompletingApplication && w.origin==208 && w.target==209 && w.checkpoint==207);
        require(h.seek_.checkpoint==nearest && !h.seek_retirement_pending_ && !w.commit_decided);
    }
}
} // namespace ReplaySeekStateTest
