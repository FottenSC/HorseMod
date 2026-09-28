#include <array>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cassert>
#include <algorithm>
enum class FailureCode {None,IllegalTransition,GenerationMismatch,ContextUnavailable};
struct Status {FailureCode code{};bool ok()const{return code==FailureCode::None;}static Status success(){return {};}static Status failure(FailureCode c){return {c};}};
struct Lease {bool live=true,release_ok=true;unsigned releases{};Status ValidateObject(void*)const{return live?Status{}:Status::failure(FailureCode::GenerationMismatch);}Status Release(){++releases;return release_ok?Status{}:Status::failure(FailureCode::ContextUnavailable);}};
struct Sc6ReplayParticleCopy {enum class Phase{Installed,Released};struct Witness{Phase phase=Phase::Installed;bool pending{};}w;auto witness()const{return w;}};
struct Sc6ReplayHost {
 struct HistoricalRestore {
  struct Fresh {struct Binding{std::uintptr_t target=9;std::array<int,2> target_weak{9,1};}binding;Lease lease;bool slots_installed=true,retired{},adopted{},settled{},native_dead{};void* slots=reinterpret_cast<void*>(30);std::size_t slot_bytes=24,native_allowance=1024;struct Gpu{void* root{};};std::vector<Gpu> gpus;struct Render{std::size_t charged_bytes=1024;}render;};
  struct Gpu {unsigned roots=1;bool dirty=true,settled=true,dead=false;bool published()const{return dirty;}std::size_t size()const{return roots;}std::size_t FreshComponentRoots(std::uintptr_t)const{return roots;}const Gpu* replacement(std::size_t)const{return this;}bool execution_settled()const{return settled;}bool FreshComponentRetired(std::uintptr_t c,const std::array<int,2>& w)const{return dead && settled && c==9 && w==std::array<int,2>{9,1};}}gpu,cpu;
  struct Manager{bool dirty=true;bool published()const{return dirty;}}manager;
  struct Trace {bool retired{},adopted{};};std::vector<std::unique_ptr<Trace>> fresh_trace_children;
  struct TraceRelease {bool ready{};mutable unsigned releases{};Status ReleaseExecutedFreshChildOwnership(Trace& owner,bool commit) const {
   if(!ready)return Status::failure(FailureCode::IllegalTransition);++releases;owner.retired=!commit;owner.adopted=commit;return {};
  }}trace_release;
  struct Undo {TraceRelease* traces{};}undo{&trace_release};
  struct Execution{enum class Capture{Empty,Current};Capture capture=Capture::Current;TraceRelease traces;struct Current{bool live=true;bool RetainsComponent(void* c)const{return live && c==reinterpret_cast<void*>(9);}}current;};
  std::unique_ptr<Execution> execution=std::make_unique<Execution>();std::vector<std::unique_ptr<Fresh>> fresh_particles;
  bool native_retired{};
 }operation;
 HistoricalRestore* historical_restore_=&operation;Sc6ReplayParticleCopy copy;Sc6ReplayParticleCopy* particle_copy_=&copy;std::atomic<bool> particle_command_pending_{};
 Status SettleFreshParticleOwners();Status ReleaseFreshParticleOwnership(bool);
 Sc6ReplayHost(){operation.cpu.roots=0;operation.fresh_particles.push_back(std::make_unique<HistoricalRestore::Fresh>());operation.fresh_particles[0]->gpus={{reinterpret_cast<void*>(40)},{reinterpret_cast<void*>(48)}};}
};
#include "fresh_owner_release.inl"
int main(){
 for(bool cpu_only:{false,true})for(bool death:{false,true}){
  Sc6ReplayHost h;auto& op=h.operation;auto& owner=*op.fresh_particles[0];
  if(cpu_only){op.gpu.roots=0;op.cpu.roots=1;}auto& lifecycle=cpu_only?op.cpu:op.gpu;
  lifecycle.settled=false;assert(!h.SettleFreshParticleOwners().ok() && !owner.settled);lifecycle.settled=true;
  lifecycle.dead=death;op.execution->current.live=!death;owner.lease.live=!death;
  assert(h.SettleFreshParticleOwners().ok() && owner.settled && owner.native_dead==death);
  assert(!h.ReleaseFreshParticleOwnership(true).ok() && !owner.lease.releases);
  op.gpu.dirty=op.manager.dirty=false;op.native_retired=true;h.copy.w.phase=Sc6ReplayParticleCopy::Phase::Released;
  assert(!h.ReleaseFreshParticleOwnership(true).ok()&&!owner.lease.releases);op.cpu.dirty=false;
  h.particle_command_pending_=true;assert(!h.ReleaseFreshParticleOwnership(true).ok());h.particle_command_pending_=false;
  h.copy.w.pending=true;assert(!h.ReleaseFreshParticleOwnership(true).ok());h.copy.w.pending=false;
  if(!death)assert(!h.ReleaseFreshParticleOwnership(false).ok() && !owner.lease.releases);
  owner.lease.release_ok=false;assert(!h.ReleaseFreshParticleOwnership(true).ok() && owner.slots && !owner.adopted && !owner.retired);
  owner.lease.release_ok=true;assert(h.ReleaseFreshParticleOwnership(true).ok());
  assert(owner.retired==death && owner.adopted==!death && !owner.slots && owner.gpus.empty() && !owner.slot_bytes && !owner.native_allowance && !owner.render.charged_bytes);
  const auto calls=owner.lease.releases;assert(h.ReleaseFreshParticleOwnership(true).ok() && owner.lease.releases==calls);
 }
 Sc6ReplayHost h;h.operation.execution->current.live=false;assert(!h.SettleFreshParticleOwners().ok());
 h.operation.gpu.dead=true;h.operation.execution->current.live=true;assert(!h.SettleFreshParticleOwners().ok());
 h.operation.execution->current.live=false;h.operation.fresh_particles[0]->lease.live=false;assert(h.SettleFreshParticleOwners().ok());
 h.operation.gpu.dirty=h.operation.cpu.dirty=h.operation.manager.dirty=false;h.copy.w.phase=Sc6ReplayParticleCopy::Phase::Released;
 assert(h.ReleaseFreshParticleOwnership(false).ok() && h.operation.fresh_particles[0]->retired);
 for(bool commit:{false,true}) {
  Sc6ReplayHost t;auto& op=t.operation;op.fresh_particles.clear();
  auto& receiver=commit?op.execution->traces:op.trace_release;
  op.fresh_trace_children.push_back(std::make_unique<Sc6ReplayHost::HistoricalRestore::Trace>());
  op.gpu.dirty=op.cpu.dirty=op.manager.dirty=false;
  assert(!t.ReleaseFreshParticleOwnership(commit).ok()&&receiver.releases==0);
  t.copy.w.phase=Sc6ReplayParticleCopy::Phase::Released;
  assert(!t.ReleaseFreshParticleOwnership(commit).ok()&&receiver.releases==0);
  receiver.ready=true;
  if(commit)assert(!t.ReleaseFreshParticleOwnership(commit).ok()&&receiver.releases==0);
  op.native_retired=true;
  assert(t.ReleaseFreshParticleOwnership(commit).ok()&&receiver.releases==1);
  assert(t.ReleaseFreshParticleOwnership(commit).ok()&&receiver.releases==1);
 }
}
