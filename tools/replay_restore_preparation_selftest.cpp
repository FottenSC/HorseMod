// Compile the actual production preparation prefix. The omitted CPU capture
// body is an entry counter, so it cannot silently grant cancellation/recovery.
// Render work is a controlled pending dependency, not a GPU-completion proof.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#define NOMINMAX
#include <Windows.h>
#define STR(x) x
#define REQUIRE(x) do {if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace RC {
enum class LogLevel {Warning,Default};
struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};
template<class T>T to_generic_string(T v){return v;}
}
enum class FailureCode {None,IllegalTransition,GenerationMismatch,PresentationFailed,CapacityExceeded,Cancelled,UndoFailed};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}};
struct Sc6ReplayParticleCopy {
    enum class Phase {ReadyA,ReadyB,UndoReady,Released,ReadPrivateOwnerRetirement};
    struct Witness {Phase phase{Phase::UndoReady};bool pending{};} state;
    Witness witness()const{return state;}
    static bool capture_retirement_pending(){return false;}
};
struct Simulation {struct Continuation {unsigned tick{6538};};Continuation continuation()const{return {};}};
struct Wind {
    bool pending{},fail{};unsigned undos{},finishes{};
    bool PendingEnclosingWind()const{return pending;}
    Status UndoEnclosingWind(){++undos;return {fail?FailureCode::GenerationMismatch:FailureCode::None};}
    Status FinishEnclosingWind(){++finishes;pending=false;return {};}
};
struct Sc6ReplayGroundDebrisState {
    enum class Progress {Rejected,Pending,Complete,Poisoned};
    Progress result{Progress::Complete};bool retained{},release_fail{};unsigned recoveries{},releases{};
    Progress RecoverBodies(){++recoveries;return result;}
    Status ReleaseRecoveredOwners(){++releases;if(release_fail)return {FailureCode::GenerationMismatch};retained=false;return {};}
    const char* failed_check()const{return "controlled_ground_recovery";}
};
struct Sc6ReplayHost {
    enum class RestoreOperationPhase {Preparing,Failed};
    enum class InteriorPhase {Holding};
    enum class ParticleCopyAction {RetireInFlight,Finish,Poll,RetireCaptures,ReopenCaptureAtB,PrepareUndo};
    struct Witness {
        FailureCode failure{};const char* participant{};bool pending{true};
        unsigned target_tick{170},original_tick{6538};std::size_t owned_bytes{};
        RestoreOperationPhase phase{RestoreOperationPhase::Preparing};
    };
    struct Restore {Witness witness;bool preparing{true},preparation_retiring{},cancel{};Sc6ReplayGroundDebrisState ground;} operation;
    Restore* historical_restore_{&operation};
    Sc6ReplayParticleCopy copy;Sc6ReplayParticleCopy* particle_copy_{&copy};
    Wind wind;Wind* checkpoint_capture_{&wind};
    Simulation simulation;Simulation* simulation_{&simulation};
    bool surface_event_{},restore_preparation_driving_{},binding{true};
    std::atomic<bool> particle_command_pending_{},particle_command_failed_{};
    InteriorPhase interior_phase_{InteriorPhase::Holding};
    std::size_t memory_limit_{1ull<<30};unsigned cpu_entries{},admissions{};bool cpu_failure{};
    std::vector<ParticleCopyAction> actions;
    bool fresh_pending{},fresh_failure{};unsigned fresh_retirements{};
    Status RetirePreparedFreshParticles(bool& pending) {
        ++fresh_retirements;pending=fresh_pending;
        return {fresh_failure?FailureCode::GenerationMismatch:FailureCode::None};
    }
    bool CheckBinding()const{return binding;}
    std::size_t AdmissionBytes(){++admissions;return 12;}
    bool ParticleCopyExperiment(ParticleCopyAction action,Sc6ReplayParticleCopy::Witness*,bool*) {
        actions.push_back(action);return true;
    }
    Status RejectCpuPreparation() {
        auto& witness=operation.witness;const bool requested=operation.preparing;
#include "replay_preparation_failure_step.inl"
        return step(Status{FailureCode::GenerationMismatch},"controlled_cpu_preparation");
    }
    void AdvanceRestorePreparation() noexcept;
};
#include "replay_restore_preparation_prefix.inl"
#include "replay_ground_debris_recovery_selftest.inl"
struct GroundQueueAdmission {
    std::uintptr_t world{};
    struct BodyProof {std::uintptr_t actor{},body{};};
    std::array<BodyProof,32> body_proofs{};unsigned body_proof_count{};
    bool Read()const noexcept;
};
#include "replay_ground_queue_method.inl"
static void TestGroundQueueAdmission() {
    std::array<std::byte,0x200> world{};
    std::array<std::byte,0x550> owner{};
    std::array<std::byte,0xb0> sync{},async{};
    const auto put=[](auto& buffer,unsigned offset,auto value){std::memcpy(buffer.data()+offset,&value,sizeof(value));};
    const auto pointer=[](auto& value){return reinterpret_cast<std::uintptr_t>(value.data());};
    put(world,0x1c8,pointer(owner));put(owner,4,2u);put(owner,0x370,pointer(sync));
    put(owner,0x380,std::uintptr_t{0xa6bc4c650d62998aull});
    GroundQueueAdmission admission{pointer(world)};
    REQUIRE(admission.Read()); // Disabled async storage must never be followed.
    put(owner,0x318,5u);
    REQUIRE(admission.Read()); // Completed output list has its own B transaction.
    put(owner,0,std::uint8_t{1});
    REQUIRE(!admission.Read()); // Enabling the invalid binding must reject.
    put(owner,0x380,pointer(async));REQUIRE(admission.Read());
    put(async,8,1u);REQUIRE(!admission.Read()); // Real pending substep input.
}
// The actual recovered-host prefix must retire never-executed private A owners
// before Finish can release the render owner. Controlled native completion here
// proves orchestration only; the live cancellation proves native retirement.
namespace UnexecutedRecovery {
struct Host {
 enum class RestoreOperationPhase {Recovered,Failed};
 enum class InteriorPhase {Holding,Failed};
 enum class ParticleCopyAction {CompleteNativeReconstruction,Finish};
 enum class SurfaceCommand {FinishDisplayRecovery};
 struct Copy {
  enum class Phase {Recovered,Released};
  struct Witness {Phase phase=Phase::Recovered;bool native_write_uncommitted{};} value;
  Witness witness()const{return value;}
  bool started{},reconstruct{};
  bool execution_started()const{return started;}
  bool needs_cpu_reconstruction()const{return reconstruct;}
 } copy_owner;
 using Sc6ReplayParticleCopy=Copy;
 Copy* particle_copy_=&copy_owner;
 struct Operation {RestoreOperationPhase phase=RestoreOperationPhase::Recovered;FailureCode failure{};const char* participant{};bool pending=true;} operation;
 struct Transaction {bool execution=true,display_started{},display_finished{},display_finish_requested{};} transaction;
 InteriorPhase interior_phase_=InteriorPhase::Holding;
 bool fresh_pending=true,fresh_failure{};unsigned retirements{},finishes{},reconstructions{},surfaces{};
 Status RetirePreparedFreshParticles(bool& pending){++retirements;pending=fresh_pending;return {fresh_failure?FailureCode::GenerationMismatch:FailureCode::None};}
 bool QueueSurface(SurfaceCommand){++surfaces;return true;}
 void Advance() {
  const auto copy=copy_owner.value;
  const auto queue=[&](ParticleCopyAction action){if(action==ParticleCopyAction::Finish)++finishes;else ++reconstructions;};
#include "replay_unexecuted_recovery.inl"
 }
};
struct GateHost {
 using Copy=Host::Copy;using Sc6ReplayParticleCopy=Copy;
 using RestoreOperationPhase=Host::RestoreOperationPhase;using InteriorPhase=Host::InteriorPhase;
 enum class ParticleCopyAction {DrainPrivateOwnerRetirement};
 struct ReplaySeekOwnership {enum class Phase {Recovering,Other};Phase value=Phase::Recovering;Phase phase()const{return value;}};
 struct Execution {ReplaySeekOwnership ownership;};
 struct Transaction {
  std::unique_ptr<Execution> execution=std::make_unique<Execution>();
  struct Undo {bool valid=true;}undo;
  struct Witness {RestoreOperationPhase phase=RestoreOperationPhase::Recovered;unsigned original_tick=217;}witness;
  bool game_dirty{},source_dirty{},execution_dirty{},surface_dirty{},rendering_dirty{};
 };
 std::unique_ptr<Transaction> historical_restore_=std::make_unique<Transaction>();
 Copy copy_owner;
 struct Simulation {struct State {unsigned tick=217;}value;State continuation()const{return value;}}simulation;
 Simulation* simulation_=&simulation;
 InteriorPhase interior_phase_=InteriorPhase::Holding;
 bool surface_event_{},checkpoint_restoring_=true;std::atomic<bool> particle_command_pending_{};
 bool Admits() {
  auto* self=this;auto* copy=&copy_owner;auto action=ParticleCopyAction::DrainPrivateOwnerRetirement;
#include "replay_private_retirement_admission.inl"
  return true;
 }
};
void Test() {
 GateHost admitted;REQUIRE(admitted.Admits());
 for(unsigned bad=0;bad<16;++bad) {
  GateHost h;auto& r=*h.historical_restore_;
  switch(bad) {
  case 0:h.historical_restore_.reset();break;case 1:r.execution.reset();break;
  case 2:r.undo.valid=false;break;case 3:r.witness.phase=Host::RestoreOperationPhase::Failed;break;
  case 4:r.execution->ownership.value=GateHost::ReplaySeekOwnership::Phase::Other;break;
  case 5:h.copy_owner.started=true;break;case 6:h.copy_owner.value.native_write_uncommitted=true;break;
  case 7:h.copy_owner.value.phase=Host::Copy::Phase::Released;break;case 8:h.simulation.value.tick=210;break;
  case 9:r.game_dirty=true;break;case 10:r.source_dirty=true;break;case 11:r.execution_dirty=true;break;
  case 12:r.surface_dirty=true;break;case 13:r.rendering_dirty=true;break;
  case 14:h.surface_event_=true;break;case 15:h.particle_command_pending_=true;break;
  }
  REQUIRE(!h.Admits());
 }
 GateHost preparation;preparation.checkpoint_restoring_=false;preparation.historical_restore_.reset();REQUIRE(preparation.Admits());
 Host h;h.Advance();REQUIRE(h.retirements==1 && !h.finishes && h.operation.pending);
 h.fresh_pending=false;h.fresh_failure=true;h.Advance();
 REQUIRE(!h.finishes && h.operation.phase==Host::RestoreOperationPhase::Failed);
 Host done;done.fresh_pending=false;done.Advance();REQUIRE(done.retirements==1 && done.finishes==1);
 Host executed;executed.copy_owner.started=true;executed.Advance();REQUIRE(!executed.retirements && executed.finishes==1);
 Host reconstruction;reconstruction.copy_owner.reconstruct=true;reconstruction.Advance();
 REQUIRE(reconstruction.reconstructions==1 && !reconstruction.retirements && !reconstruction.finishes);
}
}
int main() {
    UnexecutedRecovery::Test();
    {
        Sc6ReplayHost h;h.operation.cancel=true;h.fresh_pending=true;
        h.AdvanceRestorePreparation();
        REQUIRE(h.actions.empty() && h.operation.preparing && h.operation.witness.pending && !h.cpu_entries);
        h.fresh_pending=false;h.fresh_failure=true;h.AdvanceRestorePreparation();
        REQUIRE(h.actions.empty() && h.operation.preparing && h.operation.witness.pending);
        h.fresh_failure=false;h.AdvanceRestorePreparation();
        REQUIRE(h.actions.size()==1 && h.actions[0]==Sc6ReplayHost::ParticleCopyAction::Finish);
    }
    {
        Sc6ReplayHost h;h.cpu_failure=true;h.operation.ground.retained=true;
        h.operation.ground.result=Sc6ReplayGroundDebrisState::Progress::Pending;
        h.AdvanceRestorePreparation();
        REQUIRE(h.operation.preparation_retiring && h.operation.preparing && h.operation.witness.pending);
        REQUIRE(h.operation.witness.phase==Sc6ReplayHost::RestoreOperationPhase::Preparing);
        h.copy.state.phase=Sc6ReplayParticleCopy::Phase::Released;h.AdvanceRestorePreparation();
        REQUIRE(h.operation.ground.retained && h.operation.witness.phase==Sc6ReplayHost::RestoreOperationPhase::Preparing);
        h.operation.ground.result=Sc6ReplayGroundDebrisState::Progress::Complete;h.AdvanceRestorePreparation();
        REQUIRE(!h.operation.ground.retained && !h.operation.preparing && !h.operation.witness.pending);
        REQUIRE(h.operation.witness.phase==Sc6ReplayHost::RestoreOperationPhase::Failed);
    }
    TestGroundQueueAdmission();
    ground_recovery_contract::Test();
    using H=Sc6ReplayHost;using Action=H::ParticleCopyAction;using Phase=Sc6ReplayParticleCopy::Phase;
    {
        H h;h.operation.cancel=true;h.copy.state.phase=Phase::Released;
        auto& ground=h.operation.ground;ground.retained=true;ground.result=Sc6ReplayGroundDebrisState::Progress::Pending;
        h.AdvanceRestorePreparation();
        REQUIRE(h.operation.preparing && h.operation.witness.pending && ground.retained && !ground.releases && !h.cpu_entries);
        ground.result=Sc6ReplayGroundDebrisState::Progress::Rejected;h.AdvanceRestorePreparation();
        REQUIRE(h.operation.preparing && ground.retained && !ground.releases);
        ground.result=Sc6ReplayGroundDebrisState::Progress::Complete;ground.release_fail=true;h.AdvanceRestorePreparation();
        REQUIRE(h.operation.preparing && ground.retained && ground.releases==1);
        ground.release_fail=false;h.AdvanceRestorePreparation();
        REQUIRE(!h.operation.preparing && !h.operation.witness.pending && !ground.retained && ground.releases==2 && !h.cpu_entries);
    }
    {
        H h;h.operation.cancel=true;h.AdvanceRestorePreparation();
        REQUIRE(!h.cpu_entries && !h.admissions && h.operation.preparation_retiring);
        REQUIRE(h.actions==std::vector<Action>{Action::Finish} && h.operation.preparing);
        h.copy.state.phase=Phase::Released;h.AdvanceRestorePreparation();
        REQUIRE(!h.operation.preparing && !h.operation.witness.pending && !h.cpu_entries);
        REQUIRE(h.operation.witness.failure==FailureCode::Cancelled);
    }
    {
        H h;h.operation.cancel=true;h.copy.state.pending=true;h.AdvanceRestorePreparation();
        REQUIRE(h.actions==std::vector<Action>{Action::RetireInFlight});
        REQUIRE(h.operation.preparing && h.operation.witness.pending && !h.cpu_entries);
        h.AdvanceRestorePreparation();REQUIRE(h.actions.back()==Action::RetireInFlight && !h.cpu_entries);
        h.copy.state.pending=false;h.AdvanceRestorePreparation();REQUIRE(h.actions.back()==Action::Finish);
    }
    {
        H h;h.operation.preparation_retiring=true;h.operation.witness.failure=FailureCode::GenerationMismatch;
        h.copy.state.phase=Phase::ReadPrivateOwnerRetirement;h.copy.state.pending=true;
        h.AdvanceRestorePreparation();REQUIRE(h.actions==std::vector<Action>{Action::Poll});
        REQUIRE(!h.fresh_retirements && h.operation.preparing && !h.cpu_entries);
        h.AdvanceRestorePreparation();REQUIRE(h.actions.back()==Action::Poll && !h.fresh_retirements);
        h.copy.state.pending=false;h.copy.state.phase=Phase::UndoReady;
        h.AdvanceRestorePreparation();REQUIRE(h.fresh_retirements==1 && h.actions.back()==Action::Finish);
    }
    {
        H h;h.operation.cancel=true;h.copy.state.phase=Phase::Released;h.wind.pending=h.wind.fail=true;
        h.AdvanceRestorePreparation();REQUIRE(h.operation.preparing && h.wind.pending && !h.wind.finishes);
        REQUIRE(!h.cpu_entries && h.operation.witness.participant);
        h.wind.fail=false;h.AdvanceRestorePreparation();
        REQUIRE(!h.operation.preparing && !h.wind.pending && h.wind.undos==2 && h.wind.finishes==1);
    }
    {
        H h;h.operation.cancel=true;h.particle_command_pending_=true;h.AdvanceRestorePreparation();
        REQUIRE(h.actions.empty() && h.operation.preparing && !h.cpu_entries);
        h.particle_command_pending_=false;h.AdvanceRestorePreparation();REQUIRE(h.actions.back()==Action::Finish);
    }
    {
        H h;h.AdvanceRestorePreparation();REQUIRE(h.cpu_entries==1 && h.actions.empty());
    }
    std::puts("Production restore preparation cancellation passed");
}
