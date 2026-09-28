#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <array>
#include <cstring>
#include "../HorseMod/horselib/deterministic/ReplaySeekOwnership.hpp"
#include "../HorseMod/horselib/deterministic/ReplayPhysicsBodyInventory.hpp"
using Horse::Deterministic::ReplayPhysicsBodyInventory;
#define STR(x) x
namespace RC {enum class LogLevel {Default,Warning};const char* to_generic_string(const char* s){return s;}struct Output {
 inline static unsigned a617_probes{},a617_children{};
 template<LogLevel,class... T>static void send(const char* format,T...) {
  if(std::strstr(format,"ground body A617 probe"))++a617_probes;
  if(std::strstr(format,"ground body A617 child"))++a617_children;
 }};}
namespace Horse::GameImGui {struct PresentHook {inline static bool ready=true;static PresentHook& instance(){static PresentHook self;return self;}bool replay_surface_ready(){return ready;}};}
struct Sc6ReplayParticleConfiguration {static bool retirement_pending(){return false;}};
struct ReplayNiagaraObservation {static void World(std::uintptr_t){}};
struct ReplayConsumerFailure {static void Context(std::uint64_t,std::uint64_t){}};
struct Sc6ReplayGroundDebrisState {
#include "ground_update_diagnostic_fields.inl"
 inline static bool admitted=true;inline static unsigned calls{};
 struct Status {bool ok()const{return admitted;}};
 static Status ValidateUpdate(std::uintptr_t,void*,void*,UpdateDiagnostic& diagnostic){
  ++calls;diagnostic.body_inventory.Add({1,2,0,0,3,1,0,ReplayPhysicsBodyInventory::Role::UnrelatedDynamic});
  diagnostic.body_inventory.scan_complete=true;diagnostic.meshes=1;
  diagnostic.children[0]={3,4,5,6,7,0,0};return {};}
};
template<class T>T& EngineField(void*,unsigned){static T value{};return value;}
template<class... T>void EngineVirtual(T...){}
struct Sc6ReplayHost {
 enum class ApplicationPhase {Idle,Engine,Tail};
 enum class InteriorPhase {Idle,Arming,Armed,Holding,Releasing,Resumed,Failed};
 enum class PauseBoundary {SimulationTick,CompletedApplication};
 enum class TickAdvancePhase {Idle,Releasing,Failed,Arming,Advancing,Settling};
 enum class SeekPhase {Idle,Advancing};
 enum class FailureCode {AdvanceFailed,PresentationFailed,UnsupportedContent};
 enum class RestoreOperationAction {Cancel};
 struct RestoreOperationWitness {};
 struct Transaction {
  struct Execution {Horse::Deterministic::ReplaySeekOwnership ownership{624,617};} execution_storage,*execution{&execution_storage};
  struct Witness {bool commit_decided{};FailureCode failure{};const char* participant{};}witness;
 } transaction_storage,*historical_restore_{};
 struct TickAdvance {TickAdvancePhase phase{};FailureCode failure{};std::uint64_t target{};} tick_advance_;
 struct Sim {struct Continuation {std::uint64_t tick{};};std::uint64_t tick{};Continuation continuation(){return {tick};}} simulation_storage,*simulation_{&simulation_storage};
 struct Seek {struct Witness {SeekPhase phase{};std::int64_t engine_us{};}witness;}seek_;
 struct Executor {
  struct TaskGroups {bool held{};bool held_consumer_task(){return held;}} groups;
  TaskGroups& task_groups(){return groups;}
 } executor_;
 bool consumer_complete{};unsigned consumer_advances{},consumer_retirements{};
 bool AdvanceConsumerTask(){++consumer_advances;return consumer_complete;}
 bool RetireConsumerProbe(){++consumer_retirements;executor_.groups.held=false;return true;}
 inline static Sc6ReplayHost* active_{};
 bool application_active_{},seek_retirement_pending_{},application_idle_{},application_stop_requested_{};
 DWORD thread_{GetCurrentThreadId()};void* application_loop_{this};
 ApplicationPhase application_phase_{};InteriorPhase interior_phase_{};PauseBoundary pause_boundary_{};
 using Observer=void(*)();static void ObserveSeekHold(){}
 void* tick_advance_context_{};Observer tick_advance_observer_{};void* manager_{};void* world_{};void* surface_event_{};
 std::uintptr_t image_base_{};std::uint64_t interior_target_{},completed_applications_{1},completed_engines_{1};
 bool ground_update_pending_{},seek_driving_{},cancel_requested{},cancel_rejected{};unsigned entries{},tails{};
 ReplayPhysicsBodyInventory ground_body_inventory_{};std::uint64_t ground_body_inventory_session_{},ground_body_inventory_probe_session_{},checkpoint_session_{1};
 bool ServiceSessionExit(){return false;}bool CanRetireSeekCheckpoint(){return false;}
 void FinishIndexAtApplicationBoundary(){}bool AdvanceSeekRetirement(){return true;}
 bool IndexBoundaryRequiresCompletion(){return false;}void TrySuspendApplication(){interior_phase_=InteriorPhase::Holding;pause_boundary_=PauseBoundary::CompletedApplication;}
 bool HasInteriorContinuation(){return false;}void AdvanceInteriorHold(){}bool engine_idle(){return true;}
 bool ArmPause(std::uint64_t,void*,Observer,PauseBoundary){return true;}
 bool PollSurface(bool& complete){complete=true;return true;}
 // Controlled cancellation edge preserves the real API's seek-owner check;
 // the ownership state machine below is the production class.
 bool RestoreOperation(RestoreOperationAction,void*,RestoreOperationWitness*) {
  if(!seek_driving_ || cancel_rejected)return false;
  cancel_requested=true;return historical_restore_->execution->ownership.RequestCancellation()
      ==Horse::Deterministic::ReplaySeekOwnership::Cancellation::Quiesce;
 }
 bool AdmitGroundUpdate();
 void EnterApplication(){++entries;application_phase_=ApplicationPhase::Engine;}
 void FinishApplication(){++tails;application_phase_=ApplicationPhase::Idle;}
 bool Stop(){return true;}
 static void TickApplication(void*);
};
#include "ground_admit_application.inl"
#include "ground_tick_application.inl"
int main(){
 Sc6ReplayHost host;Sc6ReplayHost::active_=&host;
 Sc6ReplayGroundDebrisState::admitted=false;Sc6ReplayHost::TickApplication(&host);
 if(host.entries || host.tails){std::puts("ground consumer rejection admitted a native application");return 34;}
 assert(Sc6ReplayGroundDebrisState::calls==1 && !host.application_active_ && host.interior_phase_==Sc6ReplayHost::InteriorPhase::Failed);
 Sc6ReplayHost safe;Sc6ReplayHost::active_=&safe;Sc6ReplayGroundDebrisState::admitted=true;
 safe.simulation_storage.tick=617;
 Sc6ReplayHost::TickApplication(&safe);assert(safe.entries==1 && safe.tails==1 && !safe.application_active_);
 assert(safe.ground_body_inventory_.scan_complete && safe.ground_body_inventory_.count==1
     && safe.ground_body_inventory_session_==safe.checkpoint_session_);
 assert(RC::Output::a617_probes==1 && safe.ground_body_inventory_probe_session_==safe.checkpoint_session_);
 assert(RC::Output::a617_children==1);
 assert(safe.AdmitGroundUpdate() && RC::Output::a617_probes==1 && RC::Output::a617_children==1);
 Sc6ReplayHost recover;Sc6ReplayHost::active_=&recover;Sc6ReplayGroundDebrisState::admitted=false;
 recover.historical_restore_=&recover.transaction_storage;auto& owner=recover.historical_restore_->execution->ownership;
 assert(owner.PublishA() && owner.ActivateExecution());
 Horse::GameImGui::PresentHook::ready=false;
 const auto before=Sc6ReplayGroundDebrisState::calls;
 Sc6ReplayHost::TickApplication(&recover);Sc6ReplayHost::TickApplication(&recover);
 assert(Sc6ReplayGroundDebrisState::calls==before+1 && recover.ground_update_pending_ && !recover.cancel_requested && !recover.entries && owner.retains_undo());
 Horse::GameImGui::PresentHook::ready=true;Sc6ReplayHost::TickApplication(&recover);
 if(!recover.cancel_requested || recover.interior_phase_!=Sc6ReplayHost::InteriorPhase::Holding) {
  std::puts("ground admission failure did not enter the owning B recovery path");return 35;
 }
 assert(owner.retains_undo() && owner.phase()==Horse::Deterministic::ReplaySeekOwnership::Phase::RecoveryQuiescing);
 assert(!recover.entries && !recover.tails && !recover.seek_driving_ && !recover.ground_update_pending_);
 // The retained-consumer path resumes its existing application. It must not
 // admit a new ground update while the consumer is pending or at its tail.
 Sc6ReplayHost held;Sc6ReplayHost::active_=&held;held.executor_.groups.held=true;
 const auto before_consumer=Sc6ReplayGroundDebrisState::calls;
 Sc6ReplayHost::TickApplication(&held);
 assert(held.consumer_advances==1 && !held.entries && !held.tails && !held.application_active_);
 assert(Sc6ReplayGroundDebrisState::calls==before_consumer && !held.consumer_retirements);
 held.consumer_complete=true;Sc6ReplayHost::TickApplication(&held);
 assert(held.consumer_advances==2 && !held.entries && held.tails==1 && !held.application_active_);
 assert(Sc6ReplayGroundDebrisState::calls==before_consumer && held.consumer_retirements==1);
}
