#include <array>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(x) do{if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Sc6ReplayVfxState{struct PreparedEmitterSet{bool dead{};mutable unsigned queries{};bool FreshComponentRetired(std::uintptr_t,const std::array<int,2>&)const{++queries;return dead;}};};
struct Sc6ReplayParticleCopy{
 enum class Phase{Installed,Other,Recovered};struct{Phase phase{Phase::Installed};bool native_write_uncommitted{true};}witness_;
 struct{bool done{true};bool retired()const{return done;}}completion_;
 bool coordinate_execution_started_{};
 bool needs_cpu_reconstruction()const{return false;}
 struct Row{bool fresh{},native_retired{};std::uintptr_t component{};std::array<int,2> component_weak{};};
 std::vector<Row> coordinates_{{true,false,0xdeadbeef,{1,2}}};
 bool SealFreshCoordinateRetirement(const Sc6ReplayVfxState::PreparedEmitterSet&)noexcept;
};
#include "coordinate_retirement.inl"
enum class RestoreOperationPhase{Recovering,Recovered,Failed};enum class FailureCode{UndoFailed};
enum class InteriorPhase{Holding,Failed};enum class ParticleCopyAction{RestoreUndo};enum class SurfaceCommand{UndoCheckpoint};
struct Host {
 struct Operation{RestoreOperationPhase phase{RestoreOperationPhase::Recovering};FailureCode failure{};bool pending{};const char* participant{};}operation;
 struct Transaction{bool execution{true},surface_dirty{};std::vector<int> particle_bindings{1};Sc6ReplayVfxState::PreparedEmitterSet gpu;}transaction;
 Sc6ReplayParticleCopy storage;Sc6ReplayParticleCopy* particle_copy_{&storage};InteriorPhase interior_phase_{InteriorPhase::Holding};
 unsigned queues{},cpu_undos{};
 bool queue(ParticleCopyAction){REQUIRE(storage.coordinates_[0].native_retired);++queues;storage.witness_.native_write_uncommitted=false;storage.witness_.phase=Sc6ReplayParticleCopy::Phase::Recovered;return true;}
 bool QueueSurface(SurfaceCommand){return false;}void UndoHistoricalRestore(){++cpu_undos;}
 void Drive(){const auto& copy=storage.witness_;
#include "coordinate_recovery_order.inl"
 }
};

int main(){Sc6ReplayParticleCopy copy;Sc6ReplayVfxState::PreparedEmitterSet gpu;
 REQUIRE(copy.SealFreshCoordinateRetirement(gpu));REQUIRE(!gpu.queries&&!copy.coordinates_[0].native_retired);
 copy.completion_.done=false;REQUIRE(!copy.SealFreshCoordinateRetirement(gpu));copy.completion_.done=true;
 copy.witness_.phase=Sc6ReplayParticleCopy::Phase::Other;REQUIRE(!copy.SealFreshCoordinateRetirement(gpu));copy.witness_.phase=Sc6ReplayParticleCopy::Phase::Installed;
 copy.coordinate_execution_started_=true;REQUIRE(!copy.SealFreshCoordinateRetirement(gpu));REQUIRE(!copy.coordinates_[0].native_retired);
 gpu.dead=true;REQUIRE(copy.SealFreshCoordinateRetirement(gpu));REQUIRE(copy.coordinates_[0].native_retired);
 {Host host;host.storage.coordinate_execution_started_=true;host.transaction.gpu.dead=true;
 host.Drive();REQUIRE(host.queues==1&&host.transaction.gpu.queries==1);host.Drive();REQUIRE(host.cpu_undos==1&&host.queues==1);}
 {Host host;host.storage.coordinate_execution_started_=true;host.Drive();REQUIRE(!host.queues&&host.operation.phase==RestoreOperationPhase::Failed);}
 std::puts("unstarted coordinate undo and retirement before GPU undo passed");}
