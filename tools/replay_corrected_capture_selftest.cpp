#include <memory>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cassert>
#define STR(x) x
namespace RC { enum class LogLevel {Default}; struct Output {template<LogLevel, class... A> static void send(A...) {}}; }
enum class FailureCode {None,CaptureFailed};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}};
struct Sc6ReplayParticleCopy {
 enum class Phase {ReadyA,Released};
 struct Witness {Phase phase=Phase::ReadyA;bool pending{},capture_sealed{};};
 Witness state;int undo=42;Witness witness()const{return state;}
 static bool capture_retirement_pending(){return false;}
};
struct Sc6ReplayHost {
 enum class CapturePhase {Idle,Preparing,Copying,Sealing,Ready,Retiring,Cancelled,Failed,Finishing};
 enum class InteriorPhase {Holding};
 enum class ParticleCopyAction {BeginWithoutReadbacks,Poll,RetireInFlight,Finish,RetireCaptures,SealCapture};
 struct Checkpoint {bool gpu=true;};
 struct {CapturePhase phase=CapturePhase::Preparing;FailureCode failure{};std::uint64_t tick=217,owned_bytes{},elapsed_us{};bool pending=true;} capture_operation_;
 struct Simulation {struct {std::uint64_t tick=217;} data;auto continuation(){return data;}} sim;
 Simulation* simulation_=&sim;
 std::unique_ptr<Sc6ReplayParticleCopy> particle_copy_=std::make_unique<Sc6ReplayParticleCopy>(),corrected_particle_copy_;
 bool corrected_capture_=true,capture_driving_{},surface_event_{},fail_capture{};
 std::atomic<bool> particle_command_pending_{},particle_command_failed_{},corrected_particle_command_failed_{};
 InteriorPhase interior_phase_=InteriorPhase::Holding;
 std::shared_ptr<Checkpoint> captured_checkpoint_;
 std::chrono::steady_clock::time_point capture_started_=std::chrono::steady_clock::now();
 ParticleCopyAction last{};unsigned queued{};
 std::unique_ptr<Sc6ReplayParticleCopy>& CaptureParticleCopyOwner() noexcept;
 bool CaptureCommandFailed() const noexcept;
 // This fixture isolates copy routing after a material-free boundary input;
 // it does not test historical admission or material completion. Those checks
 // run through the real guard/host in replay_vfx_completion_observation_selftest.
 bool CheckHistoricalMaterialBoundary() const noexcept {return true;}
 void AdvanceCaptureOperation() noexcept;
 bool CheckBinding(){return true;}std::size_t AdmissionBytes(){assert(!particle_command_pending_.load());return 100;}
 Status Capture(std::shared_ptr<Checkpoint>& p){if(fail_capture)return {FailureCode::CaptureFailed};p=std::make_shared<Checkpoint>();return {};}
 bool ParticleCopyExperiment(ParticleCopyAction a,Sc6ReplayParticleCopy::Witness*,bool*) {
   last=a;++queued;return true; // Native queue mocked; no completion is granted here.
 }
};
#include "corrected_capture.inl"
int main(){
 Sc6ReplayHost pending;pending.particle_command_pending_=true;
 pending.AdvanceCaptureOperation();assert(pending.queued==0);
 Sc6ReplayHost h;auto* b=h.particle_copy_.get();b->state.pending=true;
 h.AdvanceCaptureOperation();
 assert(h.last==Sc6ReplayHost::ParticleCopyAction::BeginWithoutReadbacks);
 assert(h.particle_copy_.get()==b && b->undo==42 && b->state.pending);
 // Successful capture is not ready until the independent operation retires.
 h.corrected_particle_copy_=std::make_unique<Sc6ReplayParticleCopy>();
 h.capture_operation_.phase=Sc6ReplayHost::CapturePhase::Copying;
 h.capture_operation_.failure=FailureCode::None;h.fail_capture=false;
 h.corrected_particle_copy_->state={Sc6ReplayParticleCopy::Phase::ReadyA,false,true};
 h.AdvanceCaptureOperation();assert(h.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Finishing);
 h.AdvanceCaptureOperation();assert(h.last==Sc6ReplayHost::ParticleCopyAction::Finish);
 assert(h.capture_operation_.phase!=Sc6ReplayHost::CapturePhase::Ready);
 h.corrected_particle_copy_->state.phase=Sc6ReplayParticleCopy::Phase::Released;
 h.AdvanceCaptureOperation();assert(h.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Ready);
 assert(h.captured_checkpoint_ && h.particle_copy_.get()==b && b->undo==42);
 h.corrected_particle_copy_=std::make_unique<Sc6ReplayParticleCopy>();
 h.corrected_particle_copy_->state.pending=true;
 h.capture_operation_.phase=Sc6ReplayHost::CapturePhase::Retiring;
 h.AdvanceCaptureOperation();assert(h.last==Sc6ReplayHost::ParticleCopyAction::RetireInFlight);
 h.corrected_particle_copy_->state.pending=false;
 h.AdvanceCaptureOperation();assert(h.last==Sc6ReplayHost::ParticleCopyAction::Finish);
 h.corrected_particle_copy_->state.phase=Sc6ReplayParticleCopy::Phase::Released;
 h.AdvanceCaptureOperation();assert(h.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Cancelled);
 assert(h.particle_copy_.get()==b && b->undo==42 && b->state.pending);
 // Failure of corrected capture must retire that lane, never the undo owner.
 h.capture_operation_.phase=Sc6ReplayHost::CapturePhase::Copying;
 h.corrected_particle_copy_->state={Sc6ReplayParticleCopy::Phase::ReadyA,false,true};
 h.fail_capture=true;h.AdvanceCaptureOperation();
 assert(h.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Retiring);
 h.AdvanceCaptureOperation();assert(h.last==Sc6ReplayHost::ParticleCopyAction::Finish);
 assert(h.particle_copy_.get()==b && b->undo==42 && b->state.pending);
 // Ordinary capture retains its existing command routing.
 Sc6ReplayHost ordinary;ordinary.corrected_capture_=false;ordinary.AdvanceCaptureOperation();
 assert(ordinary.last==Sc6ReplayHost::ParticleCopyAction::Finish);
}
