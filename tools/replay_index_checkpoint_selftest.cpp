#include "deterministic/ReplayTickIndex.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <array>
unsigned GetCurrentThreadId(){return 1;}
#define STR(x) x
namespace RC {enum class LogLevel {Default,Warning};struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};}
namespace Horse::GameImGui {
struct PresentHook {
    bool cancel{},can_cancel{},pause{},can_pause{},pause_rejected{};unsigned phase{},failure{};std::uint64_t tick{};
    static PresentHook& instance(){static PresentHook ui;return ui;}
    bool take_replay_index_cancel(){return std::exchange(cancel,false);}
    bool take_replay_pause_request(){return std::exchange(pause,false);}
    void publish_replay_pause_availability(bool allowed,std::uint64_t current){can_pause=allowed;tick=current;}
    void report_replay_pause_request(bool accepted){pause_rejected=!accepted;if(accepted)can_pause=false;}
    void publish_replay_index_progress(unsigned p,std::uint64_t t,unsigned f,bool c){phase=p;tick=t;failure=f;can_cancel=c;}
};
}
namespace Horse::Deterministic {
inline std::int32_t native_world_mode=2;
template<class T> T EngineField(void*,std::uintptr_t offset){if(offset!=0x4846364)std::abort();return static_cast<T>(native_world_mode);}
struct Sc6ReplayParticleCopy {
    enum class Phase {Empty,ReadyA,Released};struct Witness {Phase phase{};};
    static inline bool retiring{};static bool capture_retirement_pending(){return retiring;}
};
struct Sc6ReplayHost {
    inline static Sc6ReplayHost* active_{};
    enum class InteriorPhase {Idle,Arming,Armed,Holding,Releasing,Resumed,Failed};
    enum class PauseBoundary {SimulationTick,CompletedApplication,NextCompletedApplication};
    struct InteriorWitness {
        InteriorPhase phase{InteriorPhase::Holding};bool surface_pending{};
        PauseBoundary boundary{PauseBoundary::CompletedApplication};
        bool application_idle{true},engine_idle{true},world_idle{true},arena_empty{true};
        const void* pending_task{};std::uint64_t tick{170};
    };
    using InteriorObserver=bool(*)(void*,const InteriorWitness&);
    using PauseMonitor=void(*)(void*,const InteriorWitness&);
    enum class IndexEndHold {Idle,Arming};IndexEndHold index_end_hold_{};
    PauseMonitor pause_monitor_{};void* pause_monitor_context_{};
    static bool SetPauseMonitor(void*,PauseMonitor) noexcept;
    enum class CapturePhase {Idle,Preparing,Ready,Retiring,Cancelled,Failed};
    enum class CaptureAction {Read,CheckTargetEligibility,Begin,Cancel,Release};
    struct CaptureWitness {CapturePhase phase{};FailureCode failure{};std::uint64_t elapsed_us{},owned_bytes{};};
    enum class IndexAction {RetainCapture,Release};
    enum class ParticleCopyAction {Read,Finish};
#include "replay_index_checkpoint_declarations.inl"
    static Status RequestIndexCheckpoint(std::uint64_t) noexcept;
    static bool ReadIndexCheckpoint(IndexCheckpointWitness*) noexcept;
    bool DriveIndexCheckpointHold(const InteriorWitness&) noexcept;
    void FinishIndexCheckpointResume() noexcept;
#include "replay_index_checkpoint_selection.inl"
    struct {std::int32_t recording_count{2};} index_source_;
    std::int32_t index_checkpoint_round_{};
    unsigned index_checkpoint_replacement_attempts_{};
    struct IndexCheckpointHealth {
        enum class Phase {Unchecked,ValidAtLastCheck,Deferred,Invalid};
        Phase phase{};std::uint64_t session{},captured_tick{},first_invalid{};
    };
    std::array<IndexCheckpointHealth,32> index_checkpoint_health_{};
    std::uint64_t checkpoint_session_{1};
    bool seek_retirement_failed_{};
    void SelectInitialIndexCheckpoint() noexcept;
    bool IndexBoundaryRequiresCompletion() const noexcept;
    enum class ApplicationPhase { Idle, Engine }; ApplicationPhase application_phase_{};
    bool idle_engine{true};bool engine_idle() const{return idle_engine;}
    std::uint64_t interior_target_{},index_end_tick_{};
    bool exiting{};std::uint32_t phase_tick{6};
    bool SessionExitRequested(){return exiting;}
    void ServiceIndexControls() noexcept;
    void TrySuspendApplication();
    bool particle_abort{},consumer_abort{};
    bool HistoricalParticleAbortPending() const {return particle_abort;}
    bool HistoricalConsumerAbortPending() const {return consumer_abort;}
    enum class TickAdvancePhase {Idle,Settling};
    struct {TickAdvancePhase phase{};} tick_advance_;
    void* tick_advance_context_{};InteriorObserver tick_advance_observer_{};
    static bool ObserveSeekHold(void*,const InteriorWitness&){return false;}
    ReplayTickIndex index_;IndexCheckpointWitness index_checkpoint_;
    CaptureWitness capture_operation_;Sc6ReplayParticleCopy::Witness copy;
    unsigned thread_{1},depth_{},pins{},takes{},finishes{},cancels{},releases{};
    bool historical_restore_{},checkpoint_restoring_{},surface_event_{},seek_retirement_pending_{};
    bool particle_copy_{},binding{true},retire_safe{true},seek_owned{},finish_ok{true},retain_ok{true};
    std::atomic<bool> particle_command_pending_{},particle_command_failed_{};
    FailureCode eligibility{};
    InteriorPhase interior_phase_{InteriorPhase::Holding};PauseBoundary pause_boundary_{PauseBoundary::CompletedApplication};
    void* interior_context_{};InteriorObserver interior_observer_{};
    struct Simulation {struct Continuation {std::uint64_t tick{170};} state;const Continuation& continuation() const{return state;}} simulation;
    Simulation* simulation_{&simulation};
    std::uintptr_t image_base_{};bool arm_ok{true};unsigned arms{};
    bool ArmPause(std::uint64_t tick,void* context,InteriorObserver observer,PauseBoundary boundary){
        ++arms;if(!arm_ok || interior_phase_!=InteriorPhase::Resumed || tick!=simulation.state.tick+1)return false;
        interior_target_=tick;interior_context_=context;interior_observer_=observer;pause_boundary_=boundary;interior_phase_=InteriorPhase::Arming;return true;
    }
    bool SeekOwnsExecution(){return seek_owned;}bool CheckBinding(){return binding;}bool CanRetireSeekCheckpoint(){return retire_safe;}
    Sc6ReplayHost(){
        active_=this;Sc6ReplayParticleCopy::retiring=false;native_world_mode=2;
        Horse::GameImGui::PresentHook::instance()={};
        ReplayTickIndex::Entry row{};row.source_active=1;row.round_state=2;if(!index_.Begin(row,32768).ok())std::abort();
        for(unsigned i=1;i<=170;++i){row.tick=row.native_tick=row.interval=i;if(!index_.Append(row).ok() || !index_.CompleteInterval(i,i,false).ok())std::abort();}
    }
    static bool ReadIndexEntry(std::uint64_t tick,ReplayTickIndex::Entry* out){
        auto rows=active_->index_.entries();if(tick>=rows.size())return false;*out=rows[tick];out->round_tick=active_->phase_tick;return true;
    }
    static bool CaptureOperation(CaptureAction action,CaptureWitness* out,void*){
        auto& h=*active_;
        switch(action){
        case CaptureAction::CheckTargetEligibility:*out={CapturePhase::Idle,h.eligibility};return true;
        case CaptureAction::Begin:h.capture_operation_.phase=CapturePhase::Preparing;break;
        case CaptureAction::Cancel:h.capture_operation_.phase=CapturePhase::Retiring;++h.cancels;break;
        case CaptureAction::Release:h.capture_operation_={};break;
        default:break;
        }
        *out=h.capture_operation_;return true;
    }
    static bool IndexOperation(IndexAction action,ReplayTickIndex::Witness* out,std::size_t){
        auto& h=*active_;
        if(action==IndexAction::RetainCapture){
            if(!h.retain_ok){h.index_.Fail(FailureCode::CapacityExceeded);*out=h.index_.witness();return false;}
            ++h.takes;++h.pins;h.capture_operation_={};
        }else{++h.releases;h.pins=0;h.index_.ReleaseAfterOwners(false);}
        *out=h.index_.witness();return true;
    }
    static bool ParticleCopyExperiment(ParticleCopyAction action,Sc6ReplayParticleCopy::Witness* out,bool* pending){
        auto& h=*active_;
        if(action==ParticleCopyAction::Finish){++h.finishes;if(!h.finish_ok)return false;h.particle_command_failed_=false;h.particle_command_pending_=true;}
        *out=h.copy;*pending=h.particle_command_pending_.load();
        return action!=ParticleCopyAction::Read || !h.particle_command_failed_;
    }
};
#include "deterministic/Sc6ReplayHost.IndexCheckpoint.inl"
#include "replay_index_pause_monitor.inl"
#include "replay_index_controls.inl"
#include "replay_playback_pause_selection.inl"
}
using namespace Horse::Deterministic;
using H=Sc6ReplayHost;using P=H::IndexCheckpointPhase;using C=H::CapturePhase;using G=Sc6ReplayParticleCopy::Phase;
void check(bool ok,int line){if(!ok){std::fprintf(stderr,"failed line %d\n",line);std::abort();}}
#define REQUIRE(x) check((x),__LINE__)
void begin(H& h){REQUIRE(H::RequestIndexCheckpoint(170).ok());REQUIRE(h.index_checkpoint_.pending());REQUIRE(h.interior_context_==&h && h.interior_observer_);}
void ready(H& h){h.capture_operation_.phase=C::Ready;h.particle_copy_=true;h.copy.phase=G::ReadyA;}
void gpu_done(H& h){h.copy.phase=G::Released;h.particle_command_pending_=false;}
void finish_cancel(H& h){
    REQUIRE(h.index_checkpoint_.phase==P::Resuming && h.index_checkpoint_.pending());
    h.interior_phase_=H::InteriorPhase::Resumed;h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_.pending());
    h.index_.ReleaseAfterOwners(true);h.FinishIndexCheckpointResume();
    REQUIRE(h.index_checkpoint_.phase==P::Complete && !h.pins && h.index_checkpoint_.failure==FailureCode::Cancelled);
}
void finish_held_cancel(H& h,const H::InteriorWitness& held,FailureCode expected=FailureCode::Cancelled){
    REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Releasing && h.index_checkpoint_.phase==P::RetiringScratch);
    REQUIRE(!h.DriveIndexCheckpointHold(held)); // Native hold persists through pin retirement.
    h.index_.ReleaseAfterOwners(true);REQUIRE(h.DriveIndexCheckpointHold(held));
    h.interior_phase_=H::InteriorPhase::Resumed;h.FinishIndexCheckpointResume();
    REQUIRE(h.index_checkpoint_.phase==P::Complete && !h.pins && h.index_checkpoint_.failure==expected);
}
int main(){
    // A retired last placement must not permanently exclude its native round.
    // The production selector owns the request; no fixture grants a request.
    for(unsigned blocked=0;blocked<9;++blocked) {
        H h;auto row=h.index_.entries().back();
        for(unsigned tick=171;tick<=206;++tick) {
            row.tick=row.native_tick=row.interval=tick;
            REQUIRE(h.index_.Append(row).ok() && h.index_.CompleteInterval(tick,tick,false).ok());
        }
        h.simulation.state.tick=205;h.interior_phase_=H::InteriorPhase::Resumed;
        h.index_checkpoint_selection_=H::IndexCheckpointSelection::Done;
        h.index_checkpoint_={P::Complete,FailureCode::None,170,170};
        h.index_checkpoint_health_[0]={H::IndexCheckpointHealth::Phase::Invalid,1,170,175};
        if(blocked==1)h.seek_retirement_pending_=true;
        if(blocked==2)h.seek_retirement_failed_=true;
        if(blocked==3)h.index_checkpoint_health_[0].phase=H::IndexCheckpointHealth::Phase::Deferred;
        if(blocked==4)h.index_checkpoint_health_[0].session=2;
        if(blocked==5)h.simulation.state.tick=204;
        if(blocked==6)h.index_checkpoint_replacement_attempts_=2;
        if(blocked==7)REQUIRE(h.index_.Cancel());
        if(blocked==8)h.index_checkpoint_selection_=H::IndexCheckpointSelection::Explicit;
        h.SelectInitialIndexCheckpoint();
        if(blocked){REQUIRE(!h.arms);continue;}
        REQUIRE(h.arms==1 && h.index_checkpoint_.requested_tick==206
            && h.index_checkpoint_replacement_attempts_==1 && !h.takes && !h.releases);
        h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==1);
    }
    // Compile the actual production boundary-selection prefix. An invalid C
    // cannot be held until its admitted application completes, and an existing
    // hold must not be armed again on every UI update.
    for(bool consumer:{false,true}) {
        H h;h.particle_abort=!consumer;h.consumer_abort=consumer;h.interior_phase_=H::InteriorPhase::Resumed;
        h.application_phase_=H::ApplicationPhase::Engine;h.TrySuspendApplication();
        REQUIRE(h.tick_advance_.phase==H::TickAdvancePhase::Idle);
        h.application_phase_=H::ApplicationPhase::Idle;h.idle_engine=false;h.TrySuspendApplication();
        REQUIRE(h.tick_advance_.phase==H::TickAdvancePhase::Idle);
        h.idle_engine=true;h.TrySuspendApplication();
        REQUIRE(h.tick_advance_.phase==H::TickAdvancePhase::Settling);
        REQUIRE(h.tick_advance_context_==&h && h.tick_advance_observer_==&H::ObserveSeekHold);
        h.tick_advance_.phase=H::TickAdvancePhase::Idle;h.interior_phase_=H::InteriorPhase::Holding;
        h.TrySuspendApplication();REQUIRE(h.tick_advance_.phase==H::TickAdvancePhase::Idle);
    }

    // Compile the production request owner and boundary-selection prefix. The
    // native tail and surface completion are still separate live obligations.
    for(unsigned blocked=0;blocked<6;++blocked) {
        H h;
        auto row=h.index_.entries().back();
        for(unsigned tick=171;tick<=180;++tick){
            row.tick=row.native_tick=row.interval=tick;
            REQUIRE(h.index_.Append(row).ok());
            REQUIRE(h.index_.CompleteInterval(tick,tick,tick==180).ok());
        }
        h.index_.ObserveNativeFinish();REQUIRE(h.index_.FinishAtApplicationBoundary(180).ok());
        h.interior_phase_=H::InteriorPhase::Resumed;
        auto& ui=Horse::GameImGui::PresentHook::instance();ui.pause=true;
        if(blocked==1)h.seek_owned=true;
        if(blocked==2)h.surface_event_=true;
        if(blocked==3)h.particle_command_pending_=true;
        if(blocked==4)h.exiting=true;
        if(blocked==5)h.simulation.state.tick=180;
        h.ServiceIndexControls();REQUIRE(!ui.pause);
        if(blocked){REQUIRE(ui.pause_rejected && !h.arms);continue;}
        REQUIRE(!ui.pause_rejected && h.arms==1 && h.interior_target_==171
            && h.pause_boundary_==H::PauseBoundary::NextCompletedApplication);
        h.interior_phase_=H::InteriorPhase::Armed;
        h.TrySuspendApplication();REQUIRE(h.pause_boundary_==H::PauseBoundary::NextCompletedApplication);
        h.simulation.state.tick=173;h.application_phase_=H::ApplicationPhase::Engine;
        h.TrySuspendApplication();REQUIRE(h.interior_target_==171);
        h.application_phase_=H::ApplicationPhase::Idle;h.idle_engine=false;
        h.TrySuspendApplication();REQUIRE(h.interior_target_==171);
        h.idle_engine=true;h.TrySuspendApplication();
        REQUIRE(h.interior_target_==173 && h.pause_boundary_==H::PauseBoundary::CompletedApplication
            && h.IndexBoundaryRequiresCompletion());
        // Existing exact requests still retain their original target even
        // when the current tick is later. Native admission must reject them.
        h.interior_target_=171;h.TrySuspendApplication();REQUIRE(h.interior_target_==171);
    }
    H::InteriorWitness held;
    {
        H h;h.interior_target_=170;h.interior_phase_=H::InteriorPhase::Armed;
        h.index_checkpoint_={P::AwaitingBoundary,FailureCode::None,170,0};
        for(unsigned update=0;update<100;++update)REQUIRE(h.IndexBoundaryRequiresCompletion() && h.simulation.state.tick==170);
        h.simulation.state.tick=169;REQUIRE(!h.IndexBoundaryRequiresCompletion());
        h.simulation.state.tick=171;REQUIRE(h.IndexBoundaryRequiresCompletion()); // Route overshoot to strict admission rejection, never another interval.
        h.simulation.state.tick=170;h.idle_engine=false;REQUIRE(!h.IndexBoundaryRequiresCompletion());h.idle_engine=true;
        h.application_phase_=H::ApplicationPhase::Engine;REQUIRE(!h.IndexBoundaryRequiresCompletion());h.application_phase_=H::ApplicationPhase::Idle;
        h.pause_boundary_=H::PauseBoundary::SimulationTick;REQUIRE(!h.IndexBoundaryRequiresCompletion());h.pause_boundary_=H::PauseBoundary::CompletedApplication;
        h.index_checkpoint_.phase=P::Complete;REQUIRE(!h.IndexBoundaryRequiresCompletion());
        h.index_end_hold_=H::IndexEndHold::Arming;h.index_end_tick_=170;REQUIRE(h.IndexBoundaryRequiresCompletion());
        h.interior_phase_=H::InteriorPhase::Holding;REQUIRE(!h.IndexBoundaryRequiresCompletion());
    }
    {
        H h;h.simulation.state.tick=169;h.interior_phase_=H::InteriorPhase::Resumed;
        h.SelectInitialIndexCheckpoint();REQUIRE(!h.arms); // Explicit callers retain their own placement.
        h.index_checkpoint_selection_=H::IndexCheckpointSelection::FirstCombat;
        h.phase_tick=5;h.SelectInitialIndexCheckpoint();REQUIRE(!h.arms);
        h.phase_tick=6;native_world_mode=6;h.SelectInitialIndexCheckpoint();REQUIRE(!h.arms);
        native_world_mode=2;h.exiting=true;h.SelectInitialIndexCheckpoint();REQUIRE(!h.arms);
        h.exiting=false;h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==1 && h.index_checkpoint_.requested_tick==170);
        h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==1);
    }
    {
        H h;h.simulation.state.tick=169;h.interior_phase_=H::InteriorPhase::Resumed;
        h.index_checkpoint_selection_=H::IndexCheckpointSelection::FirstCombat;h.arm_ok=false;
        h.SelectInitialIndexCheckpoint();REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Failed);
        REQUIRE(h.index_checkpoint_.phase==P::Idle && !h.pins && !h.takes);
    }
    {
        H h;h.index_checkpoint_selection_=H::IndexCheckpointSelection::FirstChosen;
        h.eligibility=FailureCode::UnsupportedContent;begin(h);REQUIRE(h.DriveIndexCheckpointHold(held));
        h.interior_phase_=H::InteriorPhase::Resumed;h.FinishIndexCheckpointResume();
        REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Failed && !h.pins && !h.takes);
    }
    for(bool unsupported:{false,true}) {
        // Completed optional round1 placement must not skip round2 or prevent
        // later rounds from being considered. Use the production completion
        // method; pending native retirement remains covered below.
        H h;h.index_source_.recording_count=5;h.index_checkpoint_round_=1;
        h.index_checkpoint_selection_=H::IndexCheckpointSelection::LastChosen;
        h.index_checkpoint_.phase=P::Resuming;h.interior_phase_=H::InteriorPhase::Resumed;
        if(unsupported)h.index_checkpoint_.failure=FailureCode::UnsupportedContent;
        h.FinishIndexCheckpointResume();
        REQUIRE(h.index_checkpoint_selection_==H::IndexCheckpointSelection::NextRound);
        REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Recording);
    }
    for(unsigned scenario=0;scenario<8;++scenario) {
        // Execute both real production capture/retirement transitions. Native
        // component data is stubbed; phase admission and all waits are real.
        using Selection=H::IndexCheckpointSelection;
        H h;h.index_checkpoint_selection_=Selection::FirstCombat;
        if(scenario>=4)h.index_source_.recording_count=5;
        h.simulation.state.tick=169;h.interior_phase_=H::InteriorPhase::Resumed;
        h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==1);
        h.simulation.state.tick=170;h.interior_phase_=H::InteriorPhase::Holding;
        REQUIRE(!h.DriveIndexCheckpointHold(held));ready(h);
        REQUIRE(!h.DriveIndexCheckpointHold(held));gpu_done(h);
        REQUIRE(h.DriveIndexCheckpointHold(held));h.interior_phase_=H::InteriorPhase::Resumed;
        h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_selection_==Selection::NextRound && h.pins==1);
        h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==1); // Same round never fills the second slot.
        auto row=h.index_.entries().back();
        for(unsigned tick=171;tick<=180;++tick) {
            row.tick=row.native_tick=row.interval=tick;row.round=1;
            REQUIRE(h.index_.Append(row).ok() && h.index_.CompleteInterval(tick,tick,false).ok());
        }
        h.simulation.state.tick=179;h.phase_tick=5;h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==1);
        h.phase_tick=6;h.surface_event_=true;h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==1);
        h.surface_event_=false;h.SelectInitialIndexCheckpoint();
        REQUIRE(h.arms==2 && h.index_checkpoint_.requested_tick==180 && h.index_checkpoint_selection_==Selection::LastChosen);
        auto later=held;later.tick=180;h.simulation.state.tick=180;h.interior_phase_=H::InteriorPhase::Holding;
        if(scenario==1) {
            h.eligibility=FailureCode::UnsupportedContent;
            REQUIRE(h.DriveIndexCheckpointHold(later));h.interior_phase_=H::InteriorPhase::Resumed;
            h.FinishIndexCheckpointResume();REQUIRE(h.pins==1 && h.index_checkpoint_selection_==Selection::Done
                && h.index_.witness().phase==ReplayTickIndex::Phase::Recording);continue;
        }
        REQUIRE(!h.DriveIndexCheckpointHold(later));ready(h);
        if(scenario==2)h.retain_ok=false;
        REQUIRE(!h.DriveIndexCheckpointHold(later));
        if(scenario==2) {
            REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Failed && h.pins==1
                && h.index_checkpoint_.failure==FailureCode::CapacityExceeded);continue;
        }
        REQUIRE(h.pins==2);h.FinishIndexCheckpointResume();
        REQUIRE(h.index_checkpoint_selection_==Selection::LastChosen); // Pending GPU work is not completion.
        if(scenario==3) {REQUIRE(h.index_.Cancel());gpu_done(h);REQUIRE(!h.DriveIndexCheckpointHold(later));finish_held_cancel(h,later);continue;}
        gpu_done(h);REQUIRE(h.DriveIndexCheckpointHold(later));h.interior_phase_=H::InteriorPhase::Resumed;
        h.FinishIndexCheckpointResume();REQUIRE(h.pins==2);
        if(scenario<4) {
            REQUIRE(h.index_checkpoint_selection_==Selection::Done);
            h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==2 && !h.releases);continue;
        }
        REQUIRE(h.index_checkpoint_selection_==Selection::NextRound);
        for(unsigned tick=181;tick<=200;++tick) {
            row.tick=row.native_tick=row.interval=tick;row.round=tick<=190?2:3;
            REQUIRE(h.index_.Append(row).ok() && h.index_.CompleteInterval(tick,tick,false).ok());
        }
        h.simulation.state.tick=189;h.SelectInitialIndexCheckpoint();
        REQUIRE(h.arms==3 && h.index_checkpoint_selection_==Selection::LastChosen);
        later.tick=190;h.simulation.state.tick=190;h.interior_phase_=H::InteriorPhase::Holding;
        if(scenario==5) {
            h.eligibility=FailureCode::UnsupportedContent;
            REQUIRE(h.DriveIndexCheckpointHold(later));h.interior_phase_=H::InteriorPhase::Resumed;
            h.FinishIndexCheckpointResume();REQUIRE(h.pins==2 && h.index_checkpoint_selection_==Selection::NextRound
                && h.index_.witness().phase==ReplayTickIndex::Phase::Recording);continue;
        }
        REQUIRE(!h.DriveIndexCheckpointHold(later));ready(h);
        if(scenario==6)h.retain_ok=false;
        REQUIRE(!h.DriveIndexCheckpointHold(later));
        if(scenario==6) {
            REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Failed && h.pins==2
                && h.index_checkpoint_.failure==FailureCode::CapacityExceeded);continue;
        }
        REQUIRE(h.pins==3);h.FinishIndexCheckpointResume();
        REQUIRE(h.index_checkpoint_selection_==Selection::LastChosen);
        if(scenario==7) {REQUIRE(h.index_.Cancel());gpu_done(h);REQUIRE(!h.DriveIndexCheckpointHold(later));finish_held_cancel(h,later);continue;}
        gpu_done(h);REQUIRE(h.DriveIndexCheckpointHold(later));h.interior_phase_=H::InteriorPhase::Resumed;
        h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_selection_==Selection::NextRound && h.pins==3);
        h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==3 && !h.releases);
        for(unsigned round=3;round<=4;++round) {
            const unsigned target=170+round*10;
            if(round==4)for(unsigned tick=201;tick<=210;++tick) {
                row.tick=row.native_tick=row.interval=tick;row.round=4;
                REQUIRE(h.index_.Append(row).ok() && h.index_.CompleteInterval(tick,tick,false).ok());
            }
            h.simulation.state.tick=target-1;h.SelectInitialIndexCheckpoint();REQUIRE(h.arms==round+1);
            later.tick=target;h.simulation.state.tick=target;h.interior_phase_=H::InteriorPhase::Holding;
            REQUIRE(!h.DriveIndexCheckpointHold(later));ready(h);
            REQUIRE(!h.DriveIndexCheckpointHold(later));REQUIRE(h.pins==round+1);
            h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_selection_==Selection::LastChosen);
            gpu_done(h);REQUIRE(h.DriveIndexCheckpointHold(later));h.interior_phase_=H::InteriorPhase::Resumed;
            h.FinishIndexCheckpointResume();
            REQUIRE(h.index_checkpoint_selection_==(round==4?Selection::Done:Selection::NextRound) && h.pins==round+1);
        }
    }
    {
        H h;auto& ui=Horse::GameImGui::PresentHook::instance();h.interior_phase_=H::InteriorPhase::Resumed;
        h.ServiceIndexControls();REQUIRE(ui.phase==1 && ui.can_cancel && ui.tick==170);
        ui.cancel=true;h.ServiceIndexControls();REQUIRE(ui.phase==2 && !ui.cancel && h.releases==1);
        REQUIRE(h.simulation.state.tick==170 && h.index_.witness().phase==ReplayTickIndex::Phase::Releasing);
        h.index_.ReleaseAfterOwners(true);ui.cancel=true;h.ServiceIndexControls();REQUIRE(ui.phase==0 && !ui.cancel);
    }
    {
        H h;auto& ui=Horse::GameImGui::PresentHook::instance();h.capture_operation_.phase=C::Preparing;
        ui.cancel=true;h.ServiceIndexControls();REQUIRE(ui.cancel && !ui.can_cancel && !h.releases);
        h.capture_operation_.phase=C::Idle;h.interior_phase_=H::InteriorPhase::Resumed;
        h.ServiceIndexControls();REQUIRE(!ui.cancel && h.releases==1);
    }
    {
        H h;begin(h);auto& ui=Horse::GameImGui::PresentHook::instance();ui.cancel=true;
        h.ServiceIndexControls();REQUIRE(!ui.cancel && !h.releases && h.index_.witness().phase==ReplayTickIndex::Phase::Cancelled);
        REQUIRE(!h.DriveIndexCheckpointHold(held));finish_held_cancel(h,held);
    }
    {
        H h;h.simulation.state.tick=169;h.interior_phase_=H::InteriorPhase::Resumed;
        REQUIRE(!H::RequestIndexCheckpoint(171).ok() && !h.arms);
        native_world_mode=6;REQUIRE(!H::RequestIndexCheckpoint(170).ok() && !h.arms);
        native_world_mode=2;h.arm_ok=false;REQUIRE(!H::RequestIndexCheckpoint(170).ok() && !h.index_checkpoint_.pending());
        const auto monitor=+[](void*,const H::InteriorWitness&){};
        REQUIRE(!H::SetPauseMonitor(&h,monitor));
        h.arm_ok=true;begin(h);REQUIRE(h.index_checkpoint_.phase==P::AwaitingBoundary && !h.index_checkpoint_.captured_tick);
        REQUIRE(H::SetPauseMonitor(&h,monitor));REQUIRE(!H::SetPauseMonitor(&h,monitor));
        REQUIRE(H::SetPauseMonitor(nullptr,nullptr));
        REQUIRE(!H::RequestIndexCheckpoint(170).ok());
        auto before=held;before.phase=H::InteriorPhase::Resumed;before.tick=169;
        REQUIRE(!h.DriveIndexCheckpointHold(before) && h.capture_operation_.phase==C::Idle);
        h.simulation.state.tick=170;h.interior_phase_=H::InteriorPhase::Holding;
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.index_checkpoint_.phase==P::Capturing);
    }
    {
        H h;const auto monitor=+[](void*,const H::InteriorWitness&){};
        h.interior_phase_=H::InteriorPhase::Resumed;
        REQUIRE(!H::SetPauseMonitor(&h,monitor)); // EnginePost source observation precedes host arming.
        h.index_end_hold_=H::IndexEndHold::Arming;
        REQUIRE(!H::SetPauseMonitor(&h,monitor)); // An intent is not an armed native boundary.
        h.interior_phase_=H::InteriorPhase::Arming;
        REQUIRE(H::SetPauseMonitor(&h,monitor));
        REQUIRE(!H::SetPauseMonitor(&h,monitor)); // No competing monitor owner.
        REQUIRE(H::SetPauseMonitor(nullptr,nullptr));
        h.interior_phase_=H::InteriorPhase::Armed;
        REQUIRE(H::SetPauseMonitor(&h,monitor));
        REQUIRE(H::SetPauseMonitor(nullptr,nullptr));
        REQUIRE(h.index_.Cancel());
        REQUIRE(!H::SetPauseMonitor(&h,monitor)); // Cancellation cannot attach an end monitor.
    }
    {
        H h;h.simulation.state.tick=169;h.interior_phase_=H::InteriorPhase::Resumed;begin(h);
        REQUIRE(h.index_.Cancel());h.simulation.state.tick=170;h.interior_phase_=H::InteriorPhase::Holding;
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.capture_operation_.phase==C::Idle && !h.takes);
        finish_held_cancel(h,held);
    }
    {
        H h;h.interior_phase_=H::InteriorPhase::Resumed;REQUIRE(!H::RequestIndexCheckpoint(170).ok());
        h.interior_phase_=H::InteriorPhase::Holding;REQUIRE(!H::RequestIndexCheckpoint(171).ok());
        h.surface_event_=true;REQUIRE(!H::RequestIndexCheckpoint(170).ok());h.surface_event_=false;
        begin(h);REQUIRE(!H::RequestIndexCheckpoint(170).ok());REQUIRE(!h.DriveIndexCheckpointHold(held));
        for(unsigned i=0;i<20;++i)REQUIRE(!h.DriveIndexCheckpointHold(held));
        ready(h);REQUIRE(!h.DriveIndexCheckpointHold(held) && h.pins==1 && h.takes==1 && h.finishes==1);
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.finishes==1);gpu_done(h);
        REQUIRE(h.DriveIndexCheckpointHold(held) && h.index_checkpoint_.phase==P::Resuming);
        h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_.pending());
        h.interior_phase_=H::InteriorPhase::Resumed;h.surface_event_=true;h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_.pending());
        h.surface_event_=false;h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_.phase==P::Complete && h.pins==1 && !h.releases);
    }
    for(unsigned point=0;point<4;++point){
        H h;begin(h);
        if(point){REQUIRE(!h.DriveIndexCheckpointHold(held));h.particle_copy_=true;h.copy.phase=G::ReadyA;}
        if(point>=2)ready(h);
        if(point==3){REQUIRE(!h.DriveIndexCheckpointHold(held));REQUIRE(h.pins==1);}
        REQUIRE(h.index_.Cancel());
        if(point==0)REQUIRE(!h.DriveIndexCheckpointHold(held));
        else {
            REQUIRE(!h.DriveIndexCheckpointHold(held));
            if(point<3){REQUIRE(h.capture_operation_.phase==C::Retiring && h.cancels==1);h.capture_operation_.phase=C::Cancelled;}
            else REQUIRE(h.pins==1 && !h.releases);
            gpu_done(h);REQUIRE(!h.DriveIndexCheckpointHold(held));
        }
        finish_held_cancel(h,held);
    }
    {
        H h;begin(h);REQUIRE(!h.DriveIndexCheckpointHold(held));ready(h);REQUIRE(!h.DriveIndexCheckpointHold(held));gpu_done(h);
        REQUIRE(h.DriveIndexCheckpointHold(held));h.interior_phase_=H::InteriorPhase::Resumed;
        REQUIRE(h.index_.Cancel());h.FinishIndexCheckpointResume();REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Releasing);
        finish_cancel(h);
    }
    {
        H h;h.eligibility=FailureCode::UnsupportedContent;begin(h);REQUIRE(h.DriveIndexCheckpointHold(held));
        REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Recording && !h.takes && !h.particle_copy_);
        h.interior_phase_=H::InteriorPhase::Resumed;h.FinishIndexCheckpointResume();REQUIRE(h.index_checkpoint_.failure==FailureCode::UnsupportedContent && !h.index_checkpoint_.pending());
    }
    {
        H h;begin(h);REQUIRE(!h.DriveIndexCheckpointHold(held));ready(h);h.finish_ok=false;
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.index_checkpoint_.phase==P::RetiringScratch && h.pins==1);
        REQUIRE(h.index_checkpoint_.pending() && h.index_checkpoint_.failure==FailureCode::PresentationFailed);
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.finishes==2 && !h.releases);
        h.finish_ok=true;REQUIRE(!h.DriveIndexCheckpointHold(held) && h.particle_command_pending_);
        gpu_done(h);REQUIRE(!h.DriveIndexCheckpointHold(held));
        finish_held_cancel(h,held,FailureCode::PresentationFailed);
    }
    {
        H h;begin(h);REQUIRE(!h.DriveIndexCheckpointHold(held));ready(h);
        REQUIRE(!h.DriveIndexCheckpointHold(held));
        h.particle_command_pending_=false;h.particle_command_failed_=true;
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.finishes==2 && h.pins==1 && !h.releases);
        REQUIRE(h.index_checkpoint_.pending() && h.index_checkpoint_.failure==FailureCode::PresentationFailed);
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.finishes==2);
        gpu_done(h);REQUIRE(!h.DriveIndexCheckpointHold(held));
        finish_held_cancel(h,held,FailureCode::PresentationFailed);
    }
    {
        H h;begin(h);REQUIRE(!h.DriveIndexCheckpointHold(held));ready(h);
        REQUIRE(!h.DriveIndexCheckpointHold(held));gpu_done(h);h.particle_command_failed_=true;
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.releases==1);
        REQUIRE(h.index_.witness().phase==ReplayTickIndex::Phase::Releasing);
        // The failed-command receipt is not cleared by the host. Only the
        // actual render owner can acknowledge its next successful command.
        h.particle_command_failed_=false;
        finish_held_cancel(h,held,FailureCode::PresentationFailed);
    }
    {
        H h;begin(h);REQUIRE(!h.DriveIndexCheckpointHold(held));ready(h);h.finish_ok=false;
        REQUIRE(!h.DriveIndexCheckpointHold(held));h.binding=false;h.finish_ok=true;
        REQUIRE(!h.DriveIndexCheckpointHold(held) && h.index_checkpoint_.phase==P::Failed);
        REQUIRE(h.finishes==1 && h.pins==1 && !h.releases);
    }
    std::puts("Production index checkpoint handoff, GPU wait, cancellation and retirement passed");
}
