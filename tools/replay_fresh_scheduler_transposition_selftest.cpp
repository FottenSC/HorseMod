#include <array>
#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstdio>
enum class FailureCode{None,GenerationMismatch,RestoreVerificationFailed};
struct Status{FailureCode code{};bool ok()const{return code==FailureCode::None;}static Status success(){return {};}static Status failure(FailureCode c){return {c};}};
struct Lease{bool valid=true;Status ValidateObject(void*)const{return valid?Status{}:Status::failure(FailureCode::GenerationMismatch);}Status Validate()const{return ValidateObject(nullptr);}};
struct Sc6ReplayTraceChildFactory {enum class Phase {Empty,Returned,Failed};};
struct Sc6ReplayParticleComponentOwner{enum class Phase{Empty,Registered,Registering};};
struct Sc6ReplaySchedulerState {
 struct RetainedPrimaryTicks{const void* context{};Status(*visit)(const void*,void*,bool(*)(void*,std::uintptr_t)){};};
 struct PreflightFailure{};
 struct PreparedRestore{bool ready{},published{};std::size_t owned_bytes()const{return 16;}};
 static inline std::vector<int> native{2,3,9};static inline std::vector<int> calls;
 static inline unsigned generation=1;static inline bool commit_fail=false,adoption_bad=false;
 std::vector<int> logical{2,3};unsigned backing=1;
 std::size_t owned_bytes()const{return 16;}
 Status Capture(std::uintptr_t,void*,std::size_t,RetainedPrimaryTicks visitor,const Sc6ReplaySchedulerState*){
  calls.push_back(visitor.visit?1:5);
  if(visitor.visit){auto s=visitor.visit(visitor.context,nullptr,[](void*,std::uintptr_t c){return c==9;});if(!s.ok())return s;}
  logical=native;backing=generation;if(!visitor.visit && adoption_bad)logical.push_back(10);return {};
 }
 Status PrepareRestore(const Sc6ReplaySchedulerState& current,std::uintptr_t,void*,std::size_t,PreparedRestore& prepared,PreflightFailure*,void*,RetainedPrimaryTicks)const {
  calls.push_back(2);assert(current.logical==native && logical==std::vector<int>({2,3}));prepared.ready=true;return {};
 }
 Status PublishBacking(const Sc6ReplaySchedulerState&,std::uintptr_t,void*,std::size_t,PreparedRestore& prepared)const{
  calls.push_back(3);assert(prepared.ready);prepared.published=true;native=logical;++generation;return {};
 }
 Status CommitBacking(std::uintptr_t,void*,PreparedRestore& prepared)const{
  calls.push_back(4);assert(prepared.published);if(commit_fail)return Status::failure(FailureCode::GenerationMismatch);prepared={};return {};
 }
 bool SameLogicalImage(const Sc6ReplaySchedulerState& other)const{return logical==other.logical;}
};
struct Sc6ReplayHost {
 struct HistoricalRestore {
  struct Fresh{struct Registration{Sc6ReplayParticleComponentOwner::Phase phase=Sc6ReplayParticleComponentOwner::Phase::Registered;}registration;Lease lease;struct Cold{std::uintptr_t copy=9;}cold;};
  std::vector<std::unique_ptr<Fresh>> fresh_particles;
  struct Trace {struct Factory {Sc6ReplayTraceChildFactory::Phase phase=Sc6ReplayTraceChildFactory::Phase::Returned;}factory;bool prepared=true;Lease lease;struct State{struct Mesh{void* object=reinterpret_cast<void*>(9);}mesh;}state;};
  std::vector<std::unique_ptr<Trace>> fresh_trace_children;
  bool fresh_scheduler_ready{},fresh_scheduler_staged{},fresh_scheduler_published{},fresh_scheduler_committed{};
  Sc6ReplaySchedulerState fresh_scheduler_staging,fresh_scheduler_adopted;
  Sc6ReplaySchedulerState::PreparedRestore fresh_scheduler_transfer;
  struct Undo{Sc6ReplaySchedulerState scheduler;}undo;
 }operation;
 HistoricalRestore* historical_restore_=&operation;
 std::uintptr_t image_base_=0;void* world_=nullptr;
 std::size_t AdmissionRemaining()const{return 1000;}
 Status TransposeFreshParticleScheduler();
 Sc6ReplayHost(){operation.fresh_particles.push_back(std::make_unique<HistoricalRestore::Fresh>());Sc6ReplaySchedulerState::calls.clear();Sc6ReplaySchedulerState::native={2,3,9};Sc6ReplaySchedulerState::generation=1;Sc6ReplaySchedulerState::commit_fail=Sc6ReplaySchedulerState::adoption_bad=false;}
};
#include "fresh_scheduler_transposition.inl"
int main(){using S=Sc6ReplaySchedulerState;
 for(bool valid:{true,false}) {Sc6ReplayHost h;h.operation.fresh_particles.clear();
  h.operation.fresh_trace_children.push_back(std::make_unique<Sc6ReplayHost::HistoricalRestore::Trace>());
  h.operation.fresh_trace_children[0]->prepared=valid;
  assert(h.TransposeFreshParticleScheduler().ok()==valid);
  assert(valid?S::native==std::vector<int>({2,3}):!h.operation.fresh_scheduler_published);
 }
 {Sc6ReplayHost h;const auto b=h.operation.undo.scheduler.logical;assert(h.TransposeFreshParticleScheduler().ok());
  assert(h.operation.undo.scheduler.logical==b && h.operation.undo.scheduler.backing==2 && h.operation.fresh_scheduler_ready);
  assert(S::calls==std::vector<int>({1,2,3,4,5}) && S::native==b);
  const auto calls=S::calls;assert(h.TransposeFreshParticleScheduler().ok() && S::calls==calls);
 }
 {Sc6ReplayHost h;S::commit_fail=true;assert(!h.TransposeFreshParticleScheduler().ok());
  assert(h.operation.fresh_scheduler_transfer.published && !h.operation.fresh_scheduler_ready && h.operation.undo.scheduler.backing==1);
  S::commit_fail=false;assert(h.TransposeFreshParticleScheduler().ok());
  assert(S::calls==std::vector<int>({1,2,3,4,4,5}));
 }
 {Sc6ReplayHost h;S::adoption_bad=true;assert(!h.TransposeFreshParticleScheduler().ok());
  assert(h.operation.undo.scheduler.logical==std::vector<int>({2,3}) && h.operation.undo.scheduler.backing==1 && !h.operation.fresh_scheduler_ready);
  S::adoption_bad=false;assert(h.TransposeFreshParticleScheduler().ok() && S::calls==std::vector<int>({1,2,3,4,5,5}));
 }
 {Sc6ReplayHost h;h.operation.fresh_particles[0]->lease.valid=false;assert(!h.TransposeFreshParticleScheduler().ok());assert(S::calls==std::vector<int>({1}) && !h.operation.fresh_scheduler_published);}
 std::puts("Host scheduler transposition preserves logical B, retains failed publication and avoids repeated native mutation");
}
