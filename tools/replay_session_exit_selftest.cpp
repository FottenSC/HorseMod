// Compile the production exit driver. Native owners are controlled here;
// this proves ordering/admission, not the cooked event or GPU implementation.
#include "deterministic/ReplayTickIndex.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <optional>
#define STR(x) x
namespace RC {
enum class LogLevel {Default,Warning};
struct Output {template<LogLevel,class... T> static void send(const char*,T...) {}};
namespace Unreal {struct UFunction {};struct UObject {unsigned publications{};void ProcessEvent(UFunction*,void*){++publications;}};}
}
namespace Horse::Deterministic {
struct Sc6ReplayParticleConfiguration {static inline bool pending{};static bool retirement_pending(){return pending;}};
struct DeterministicHookSet {static bool SetReplayExecutorEnabled(bool,bool,void*);};
struct Sc6ReplayHost {
#include "replay_session_exit_declarations.inl"
    enum class SeekPhase {Idle,Preparing,Held,Cancelled,Failed,Recovering};
    enum class SeekAction {Cancel,Release};
    enum class SeekReleaseResult {NotRequested,AcceptedPending,Completed};
    struct SeekWitness {SeekPhase phase{};bool commit_decided{},pending{};SeekReleaseResult release_result{};};
    struct SeekState {SeekWitness witness;bool cancel{};} seek_;
    enum class CapturePhase {Idle,Preparing};struct Capture {CapturePhase phase{};} capture_operation_;
    enum class InteriorPhase {Idle,Armed,Holding,Releasing,Resumed,Failed};
    enum class ApplicationPhase {Idle,Engine};
    enum class IndexAction {Release};
    struct Pending {bool value{};bool pending(){return value;}} index_checkpoint_;
    struct Simulation {struct State {std::uint64_t tick{210};} state;bool idle{true};bool interval_complete(){return idle;}const State& continuation(){return state;}} simulation;
    Simulation* simulation_{&simulation};
    SessionExitWitness session_exit_{SessionExitPhase::Requested,FailureCode::None,210};
    ReplayTickIndex index_;
    std::optional<std::uint64_t> queued_indexed_seek_{208};
    bool valid{true},binding{true},historical_restore_{},checkpoint_restoring_{},surface_event_{},seek_retirement_pending_{},seek_retirement_failed_{};
    std::atomic<bool> particle_command_pending_{};
    bool retire_safe{true},retire_complete{true},cancel_ok{true},release_ok{true},resume_ok{true},detach_ok{true},detached{true};
    bool interior_resume_requested_{};
    InteriorPhase interior_phase_{InteriorPhase::Idle};ApplicationPhase application_phase_{ApplicationPhase::Idle};
    unsigned cancellations{},releases{},resumes{},detach_attempts{},index_releases{};
    std::uint64_t completed_applications_{210};
    RC::Unreal::UObject scene;RC::Unreal::UFunction function;
    void* session_exit_scene_{&scene};void* session_exit_function_{&function};
    static inline Sc6ReplayHost* active_{};
    Sc6ReplayHost(){active_=this;}
    bool SessionExitRequested(){return session_exit_.phase!=SessionExitPhase::Idle;}
    bool ValidateSessionExit(){return valid;}bool CheckBinding(){return binding;}
    bool SeekOwnsExecution(){return seek_.witness.phase!=SeekPhase::Idle;}
    bool CanRetireSeekCheckpoint(){return retire_safe;}bool engine_idle(){return application_phase_==ApplicationPhase::Idle;}
    bool StopForDestruction(){return detached;}
    Status Resume(){++resumes;if(!resume_ok)return Status::failure(FailureCode::IllegalTransition);interior_resume_requested_=true;return Status::success();}
    bool ServiceSessionExit() noexcept;
    bool SeekOperation(SeekAction a,void*,unsigned,SeekWitness* out){
        if(a==SeekAction::Cancel){++cancellations;if(!cancel_ok)return false;seek_.cancel=true;seek_.witness.pending=true;seek_.witness.phase=SeekPhase::Recovering;}
        else {++releases;if(!release_ok)return false;
            if(seek_.witness.release_result==SeekReleaseResult::Completed){*out=seek_.witness;seek_={};return true;}
            seek_.witness.release_result=SeekReleaseResult::AcceptedPending;seek_.witness.pending=true;}
        *out=seek_.witness;return true;
    }
    bool IndexOperation(IndexAction,ReplayTickIndex::Witness* out,unsigned){++index_releases;index_.ReleaseAfterOwners(false);*out=index_.witness();return true;}
    void FinishIndexAtApplicationBoundary(){if(retire_complete)index_.ReleaseAfterOwners(true);else seek_retirement_pending_=true;}
    bool AdvanceSeekRetirement(){if(retire_complete)seek_retirement_pending_=false;return !seek_retirement_pending_;}
};
bool DeterministicHookSet::SetReplayExecutorEnabled(bool enabled,bool,void*) {
    auto& h=*Sc6ReplayHost::active_;++h.detach_attempts;if(enabled)std::abort();return h.detach_ok;
}
#include "replay_session_exit_service.inl"
}
using H=Horse::Deterministic::Sc6ReplayHost;
static void check(bool value){if(!value)std::abort();}
int main(){
    {H h;Horse::Deterministic::Sc6ReplayParticleConfiguration::pending=true;
        check(!h.ServiceSessionExit() && h.seek_retirement_pending_ && !h.detach_attempts && !h.scene.publications);
        Horse::Deterministic::Sc6ReplayParticleConfiguration::pending=false;h.seek_retirement_pending_=false;
        check(h.ServiceSessionExit() && h.scene.publications==1);}
    {H h;h.session_exit_.phase=H::SessionExitPhase::Idle;check(!h.ServiceSessionExit() && !h.detach_attempts && !h.scene.publications);}
    for(bool binding:{false,true}) {H h;h.valid=binding;h.binding=!binding;check(h.ServiceSessionExit() && h.session_exit_.phase==H::SessionExitPhase::Failed && !h.detach_attempts && !h.scene.publications);}
    {H h;h.index_checkpoint_.value=true;Horse::Deterministic::ReplayTickIndex::Entry row{};check(h.index_.Begin(row,1024).ok());
        check(!h.ServiceSessionExit() && h.index_.witness().phase==Horse::Deterministic::ReplayTickIndex::Phase::Cancelled && !h.detach_attempts);}
    {H h;h.historical_restore_=true;h.seek_.witness.phase=H::SeekPhase::Held;
        check(!h.ServiceSessionExit() && h.cancellations==1 && !h.releases && !h.scene.publications);
        check(!h.ServiceSessionExit() && h.cancellations==1 && !h.releases);
        h.historical_restore_=false;h.seek_.witness.phase=H::SeekPhase::Cancelled;h.seek_.witness.pending=false;
        check(!h.ServiceSessionExit() && h.releases==1 && !h.detach_attempts);
        h.seek_.witness.pending=false;h.seek_.witness.release_result=H::SeekReleaseResult::Completed;
        check(h.ServiceSessionExit() && h.scene.publications==1 && h.session_exit_.recovered_tick==210);
        check(h.ServiceSessionExit() && h.scene.publications==1);}
    {H h;h.historical_restore_=true;h.seek_.witness.phase=H::SeekPhase::Failed;h.cancel_ok=false;
        check(h.ServiceSessionExit() && h.cancellations==1 && h.historical_restore_ && !h.releases && !h.scene.publications);}
    {H h;h.seek_.witness={H::SeekPhase::Held,true,true};h.historical_restore_=true;
        check(!h.ServiceSessionExit() && !h.cancellations && !h.releases && !h.scene.publications);}
    {H h;h.capture_operation_.phase=H::CapturePhase::Preparing;
        check(!h.ServiceSessionExit() && !h.detach_attempts && !h.scene.publications);}
    {H h;Horse::Deterministic::ReplayTickIndex::Entry row{};check(h.index_.Begin(row,1024).ok());h.retire_complete=false;
        check(h.ServiceSessionExit() && h.seek_retirement_pending_ && !h.detach_attempts && !h.scene.publications);
        h.retire_complete=true;h.interior_phase_=H::InteriorPhase::Holding;
        check(!h.ServiceSessionExit() && h.resumes==1 && !h.detach_attempts && !h.scene.publications);
        check(!h.ServiceSessionExit() && h.resumes==1);
        h.interior_phase_=H::InteriorPhase::Releasing;check(!h.ServiceSessionExit() && !h.detach_attempts);
        h.interior_phase_=H::InteriorPhase::Resumed;h.application_phase_=H::ApplicationPhase::Engine;
        check(!h.ServiceSessionExit() && !h.detach_attempts);
        h.application_phase_=H::ApplicationPhase::Idle;h.detach_ok=false;
        check(h.ServiceSessionExit() && h.detach_attempts==1 && !h.scene.publications);
        h.detach_ok=true;h.detached=false;check(h.ServiceSessionExit() && !h.scene.publications);
        h.detached=true;check(h.ServiceSessionExit() && h.scene.publications==1);}
    {H h;h.interior_phase_=H::InteriorPhase::Failed;check(h.ServiceSessionExit() && !h.scene.publications);}
    std::puts("session exit ordering and retention passed");
}
