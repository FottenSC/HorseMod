#define NOMINMAX
#include <Windows.h>
#include "ReplayTraceWeakReference.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <cstdio>
enum class FailureCode {None,IllegalTransition,UndoFailed,PresentationFailed};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}static Status success(){return {};}static Status failure(FailureCode c){return {c};}};
struct Sc6ReplayTraceChildFactory {enum class Phase {Empty,Returned,Failed};};
struct Sc6ReplayTraceState {struct Retirement {enum class Step {Ready,Finished};};};
struct ReplayGpuCompletion {enum class Result {Complete,DeviceFailure,TimedOut,Cancelled};};
// Trace-only cleanup leaves this domain empty. Accidental ground cleanup must
// fail; its native/lease/GPU contract has its own production-boundary fixture.
struct Sc6ReplayGroundDebrisState {
 struct ColdRoot {enum class Phase {Cold,Released};Phase phase=Phase::Cold;std::uint64_t retirement_serial{};};
 static Status DestroyColdRoot(ColdRoot&,std::uint64_t){return Status::failure(FailureCode::UndoFailed);}
 template<class T>static Status ReleaseColdRoot(ColdRoot&,const T&){return Status::failure(FailureCode::UndoFailed);}
};
using ReplayTraceWeakController=Horse::Deterministic::ReplayTraceWeakController;
struct Child {
 struct Factory {Sc6ReplayTraceChildFactory::Phase phase=Sc6ReplayTraceChildFactory::Phase::Returned;}factory;
 struct Retirement {Sc6ReplayTraceState::Retirement::Step step=Sc6ReplayTraceState::Retirement::Step::Ready;}retirement;
 struct Lease {unsigned releases{};Status Release(){++releases;return {};}}lease;
 ReplayTraceWeakController controller{0x3362590,1,2};
 struct State {struct Ref {void* controller{};}ref;}state;
 std::uint64_t retirement_serial{};std::size_t allowance=123;bool prepared=true,retired{},identity_retained=true;
 Child(){state.ref.controller=&controller;}
};
struct Trace {
 using FreshChild=Child;using Retirement=Sc6ReplayTraceState::Retirement;
 DWORD thread_=GetCurrentThreadId();std::uintptr_t base_{};unsigned calls{};static inline unsigned deletions{};
 static Status Fail(const char*){return Status::failure(FailureCode::UndoFailed);}
 static void DeleteExpiredController(void*){++deletions;}
 Status RetireFreshChild(Child& c){++calls;c.controller.strong=0;--c.controller.weak;c.retirement.step=Retirement::Step::Finished;return {};}
#include "trace_child_release.inl"
};
struct Sc6ReplayParticleCopy {
 struct Witness {bool native_write_uncommitted{};}w;
 struct Completion {bool complete{};ReplayGpuCompletion::Result value=ReplayGpuCompletion::Result::Complete;std::uint64_t serial=10;bool retired()const{return complete;}auto result()const{return value;}auto submitted_serial()const{return serial;}}completion;
 auto& witness(){return w;}auto& retirement_completion(){return completion;}
};
enum class ParticleCopyAction {DrainPrivateOwnerRetirement};
namespace RC {enum class LogLevel {Default};struct Output {template<LogLevel L>static void send(const char*){}};}
#define STR(x) x
struct Sc6ReplayHost {
 struct Operation {
  struct Publication {bool active{};bool published()const{return active;}}manager,gpu,cpu,scheduler;
  std::vector<int> fresh_particles;
  std::vector<std::unique_ptr<Child>> fresh_trace_children;
  std::vector<std::unique_ptr<Sc6ReplayGroundDebrisState::ColdRoot>> fresh_ground_roots;
  struct Undo {Trace* traces{};}undo;
 }operation;
 Trace b;Sc6ReplayParticleCopy copy;Sc6ReplayParticleCopy* particle_copy_=&copy;Operation* historical_restore_=&operation;
 unsigned drains{},transpositions{};
 Status TransposeFreshParticleScheduler(){++transpositions;return {};}
 bool ParticleCopyExperiment(ParticleCopyAction,Sc6ReplayParticleCopy::Witness*,bool*){++drains;return true;}
 Status RetirePreparedFreshParticles(bool&);
 Sc6ReplayHost(){operation.undo.traces=&b;operation.fresh_trace_children.push_back(std::make_unique<Child>());}
};
// Actual trace branches of host preparation cleanup; legacy particle branches
// are excluded by the extractor and this fixture has an empty particle set.
#include "trace_child_cleanup.inl"
int main(){
 {Sc6ReplayHost h;auto& c=*h.operation.fresh_trace_children[0];bool pending=false;
  assert(h.RetirePreparedFreshParticles(pending).ok()&&pending&&h.b.calls==1&&h.drains==1);
  assert(c.lease.releases==0&&!c.retired&&c.allowance==123&&c.identity_retained&&c.controller.weak==1);
  assert(h.RetirePreparedFreshParticles(pending).ok()&&pending&&h.b.calls==1&&h.drains==1);
  h.copy.completion.complete=true;
  assert(h.RetirePreparedFreshParticles(pending).ok()&&pending&&c.lease.releases==0);
  ++h.copy.completion.serial;h.copy.completion.complete=false;
  assert(h.RetirePreparedFreshParticles(pending).ok()&&pending&&c.lease.releases==0);
  h.copy.completion.complete=true;
  for(auto result:{ReplayGpuCompletion::Result::DeviceFailure,ReplayGpuCompletion::Result::TimedOut,ReplayGpuCompletion::Result::Cancelled}) {
   h.copy.completion.value=result;
   assert(!h.RetirePreparedFreshParticles(pending).ok()&&!c.retired&&c.lease.releases==0&&c.identity_retained);
  }
  h.copy.completion.value=ReplayGpuCompletion::Result::Complete;
  assert(h.RetirePreparedFreshParticles(pending).ok()&&!pending&&c.retired&&c.allowance==0&&c.lease.releases==1&&!c.identity_retained);
  assert(h.RetirePreparedFreshParticles(pending).ok()&&!pending&&c.lease.releases==1&&h.b.calls==1);
 }
 {Sc6ReplayHost h;auto& c=*h.operation.fresh_trace_children[0];c.factory.phase=Sc6ReplayTraceChildFactory::Phase::Empty;c.prepared=false;bool pending=true;
  assert(h.RetirePreparedFreshParticles(pending).ok()&&!pending&&c.retired&&h.b.calls==0&&h.drains==0&&c.lease.releases==0);
 }
 {Sc6ReplayHost h;auto& c=*h.operation.fresh_trace_children[0];c.factory.phase=Sc6ReplayTraceChildFactory::Phase::Failed;c.prepared=false;bool pending=false;
  assert(!h.RetirePreparedFreshParticles(pending).ok()&&!c.retired&&h.drains==0&&c.lease.releases==0);
 }
 {Sc6ReplayHost h;h.operation.scheduler.active=true;bool pending=false;
  assert(!h.RetirePreparedFreshParticles(pending).ok()&&h.b.calls==0&&h.transpositions==0);
 }
 std::puts("fresh trace cleanup retains lease until strictly newer GPU completion; empty, partial and repeated cleanup checked");
}
