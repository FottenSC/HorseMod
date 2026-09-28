#include <array>
#include <algorithm>
#include <climits>
#include <vector>
#include <memory>
#include <span>
#include <cstdint>
#include <cassert>
#include <cstring>
#define NOMINMAX
#include <Windows.h>
#define STR(x) x
namespace RC {enum class LogLevel{Default};inline const char* to_generic_string(const char* value){return value;}struct Output{template<LogLevel,class... T>static void send(const char*,T...) {}};}
enum class FailureCode{IllegalTransition,CapacityExceeded,GenerationMismatch,PresentationFailed};
struct Status{bool good=true;FailureCode code{};bool ok()const{return good;}static Status success(){return {};}static Status failure(FailureCode){return {false};}};
struct ReplayGpuCompletion {
 enum class Result{Complete,Pending,DeviceFailure};Result value=Result::Complete;std::uint64_t serial=5;
 bool retired()const{return value!=Result::Pending;}auto result()const{return value;}auto submitted_serial()const{return serial;}
};
struct Sc6ReplayParticleCopy {struct Witness{};ReplayGpuCompletion completion;const auto& retirement_completion()const{return completion;}};
// This fixture selects trace-only content. Ground construction is a separate
// production fixture; an accidental call here fails instead of granting it.
struct Sc6ReplayGroundDebrisState {
 struct ColdRoot {
  unsigned phase{},flags{},primary_tick{},secondary_tick{},child_count{},material_count{};
  const char* check="unexpected_ground_construction";
  std::uintptr_t source{},object{},child_observed{},physics_state_offset{},body_configuration_offset{},pose_component{},pose_offset{},pose_expected{},pose_actual{},graph_callback{};
  bool detached{},graph_ready{},physics_ready{};std::size_t owned_bytes()const{return 0;}
 };
 Status ValidateCapturedOwners()const{return Status::failure(FailureCode::GenerationMismatch);}
 std::size_t root_count()const{return 1;}
 template<class... T>Status ConstructColdRoot(T&&...)const{return Status::failure(FailureCode::GenerationMismatch);}
 template<class... T>Status ConstructColdGraph(T&&...)const{return Status::failure(FailureCode::GenerationMismatch);}
 template<class... T>Status ConstructColdPhysics(T&&...)const{return Status::failure(FailureCode::GenerationMismatch);}
};
static unsigned constructions{},drains{},projections{};static bool gpu_complete{};
struct Sc6ReplayTraceState {
 struct Ref{void* state{};void* controller{};};
 struct Source{Ref historical;void* component{};void* parts{};void* kind{};unsigned parts_id{},kind_id{},charge{};void* mesh_asset{};void* animation_class{};};
 struct FreshChild{struct Factory{unsigned phase{};Ref reference;}factory;struct State{struct Id{void* object{};}actor,mesh;}state;bool prepared=true;};
 template<class F>Status VisitHistoricalChildSources(F f)const{return f(Source{});}
 Status ConstructFreshChild(const Source&,const Sc6ReplayTraceState&,std::size_t,FreshChild&)const{++constructions;return {};}
 Status PrepareFreshChildHistory(const Source&,const Sc6ReplayTraceState&,FreshChild&)const{return {};}
 Status PrepareFreshChildAnimation(const Source&,const Sc6ReplayTraceState&,FreshChild&,std::size_t)const{return {};}
 Status DrainFreshChildRenderWork(const Sc6ReplayTraceState&,std::span<FreshChild* const>)const{++drains;return {};}
 Status ProjectFreshChildren(const Sc6ReplayTraceState&,std::span<FreshChild* const>,std::size_t,Sc6ReplayTraceState&)const{++projections;return {gpu_complete};}
 Status CreateSurvivingOwnerView(std::span<FreshChild* const>,std::size_t,Sc6ReplayTraceState&)const{return {};}
};
struct Sc6ReplayHost {
 enum class ParticleCopyAction{DrainPrivateOwnerRetirement};
 struct HistoricalRestore{
  enum class FreshRenderPhase{Empty,Entered,Queued,Ready};
  FreshRenderPhase fresh_trace_render_phase=FreshRenderPhase::Empty;
  bool particle_inventory_ready{},fresh_trace_sources_ready{};std::uint64_t fresh_trace_render_serial{};
  struct Checkpoint{bool valid=true;struct Execution{unsigned tick=345;}execution;std::unique_ptr<Sc6ReplayTraceState> traces=std::make_unique<Sc6ReplayTraceState>();std::unique_ptr<Sc6ReplayGroundDebrisState> ground;}undo;
  std::unique_ptr<Checkpoint> target=std::make_unique<Checkpoint>();
  std::vector<std::unique_ptr<Sc6ReplayTraceState::FreshChild>> fresh_trace_children;
  bool fresh_ground_roots_prepared{};
  std::vector<std::unique_ptr<Sc6ReplayGroundDebrisState::ColdRoot>> fresh_ground_roots;
  std::unique_ptr<Sc6ReplayTraceState> trace_projection,trace_survivors;
 }operation;
 HistoricalRestore* historical_restore_=&operation;bool restore_preparation_driving_=true;
 Sc6ReplayParticleCopy copy;Sc6ReplayParticleCopy* particle_copy_=&copy;unsigned submissions{},advanced{};bool submit=true,failed{};
 std::size_t AdmissionRemaining()const{return 1<<20;}
 bool ParticleCopyExperiment(ParticleCopyAction,Sc6ReplayParticleCopy::Witness*,bool*){
  if(!submit)return false;++submissions;copy.completion.value=ReplayGpuCompletion::Result::Pending;++copy.completion.serial;return true;
 }
 Status PrepareFreshParticleOwners();
 void Drive(){auto& transaction=operation;const auto fail=[&](auto,const char*){failed=true;};
#include "trace_preparation_driver_gate.inl"
 ++advanced;
 }
};
#include "trace_host_preparation_order.inl"
namespace QueueTest {
using Object=void;
template<class T>T& At(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
struct FreshChild {
 struct Ref{void* state{};void* controller{};}historical;
 enum class AnimationPhase{Ready};AnimationPhase animation_phase=AnimationPhase::Ready;
 bool prepared=true,retired=false,identity_retained=true;
 struct State{struct Id{void* object{};}mesh;}state;
 struct Lease{bool live=true;Status Validate()const{return {live};}}lease;
};
#include "trace_fresh_render_queue.inl"
static void* resolved{};
static void* Resolve(const unsigned* weak){return weak[0]==1&&weak[1]==7?resolved:nullptr;}
void Test(){
 auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0xf83000,MEM_RESERVE,PAGE_NOACCESS));assert(image);
 auto* code=static_cast<std::byte*>(VirtualAlloc(image+0xf82000,4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(code);
 const unsigned char jump[]{0x48,0xb8};auto* entry=image+0xf823f0;std::memcpy(entry,jump,2);
 auto target=&Resolve;std::memcpy(entry+2,&target,8);entry[10]=std::byte{0xff};entry[11]=std::byte{0xe0};
 FlushInstructionCache(GetCurrentProcess(),entry,12);
 alignas(16) std::array<std::byte,0x400> world{};
 alignas(16) std::array<std::byte,0x1000> mesh{};
 auto* lock=reinterpret_cast<CRITICAL_SECTION*>(world.data()+0x270);InitializeCriticalSection(lock);
 FreshChild child;child.state.mesh.object=resolved=mesh.data();std::array<FreshChild*,1> children{&child};
 At<void*>(mesh.data(),0x1c8)=world.data();At<unsigned>(mesh.data(),0x188)=0x400000e3;
 std::array<unsigned,4> weak{1,7,0,0};auto* h=world.data()+0x1d0;
 At<void*>(h,0)=weak.data();At<int>(h,8)=At<int>(h,12)=At<int>(h,0x28)=1;
 At<unsigned>(h,0x10)=1;At<int>(h,0x2c)=128;
 const auto base=reinterpret_cast<std::uintptr_t>(image);
 assert(FreshChildRenderQueue(base,world.data(),children,false));
 assert(!FreshChildRenderQueue(base,world.data(),children,true));
 weak[1]=8;assert(!FreshChildRenderQueue(base,world.data(),children,false));weak[1]=7;
 child.lease.live=false;assert(!FreshChildRenderQueue(base,world.data(),children,false));child.lease.live=true;
 At<void*>(mesh.data(),0xec8)=mesh.data();assert(!FreshChildRenderQueue(base,world.data(),children,false));At<void*>(mesh.data(),0xec8)=nullptr;
 At<unsigned>(mesh.data(),0x188)=0x800000e3;assert(!FreshChildRenderQueue(base,world.data(),children,false));At<unsigned>(mesh.data(),0x188)=0x400000e3;
 std::memcpy(h+0x50,h,0x50);assert(!FreshChildRenderQueue(base,world.data(),children,false));std::memset(h+0x50,0,0x50);
 At<int>(h,0x34)=1;assert(!FreshChildRenderQueue(base,world.data(),children,false));At<int>(h,0x34)=0;
 At<unsigned>(h,0x10)=0;At<int>(h,8)=At<int>(h,0x28)=0;
 assert(!FreshChildRenderQueue(base,world.data(),children,true));
 At<unsigned>(mesh.data(),0x188)=3;
 assert(FreshChildRenderQueue(base,world.data(),children,true));
 DeleteCriticalSection(lock);VirtualFree(image,0,MEM_RELEASE);
}
}
namespace VisibilityTest {
struct Sc6ReplayVfxState {struct ReconstructionBinding{struct Identity{std::uintptr_t source{};}identity;};};
struct Sc6ReplayTraceState {
 using FreshChild=QueueTest::FreshChild;
 unsigned thread_=GetCurrentThreadId();std::uintptr_t base_=1;void* world_=this;
 struct Ref{void* state{};void* controller{};};
 struct State{Ref ref;struct Id{void* object{};}mesh;};
 std::vector<State> states_;
 struct Image {unsigned visibility=0x40f,id=936;std::uintptr_t mesh=42;bool available=true;}values_,bindings_;
 Status ValidateValues()const{return {};}
 static Status Fail(const char*){return {false};}
 template<class T>static bool RetainedField(const Image& image,void*,std::size_t offset,T& output){if(!image.available)return false;if(offset==0x240)output=static_cast<T>(image.visibility);else if(offset==0x420)output=static_cast<T>(image.id);else if(offset==0x910)output=static_cast<T>(image.mesh);else return false;return true;}
 static bool FreshChildRenderQueue(std::uintptr_t,void*,std::span<FreshChild* const>,bool){return true;}
 template<class T>static T& At(void* p,std::size_t offset){return QueueTest::At<T>(p,offset);}
 Status FreshChildMeshBinding(const FreshChild& child,Sc6ReplayVfxState::ReconstructionBinding& output)const{output.identity.source=0x12345678;return child.lease.Validate();}
#include "trace_fresh_dormant_source.inl"
 static unsigned calls;static bool setter_ok;
 static bool SetFreshChildVisibility(std::uintptr_t,void* mesh,unsigned visibility){++calls;if(!setter_ok)return false;QueueTest::At<unsigned>(mesh,0x240)=(QueueTest::At<unsigned>(mesh,0x240)&~0x10u)|(visibility&0x10);return true;}
 static bool RunFreshChildRenderWork(std::uintptr_t,void*){return true;}
#include "trace_fresh_render_drain.inl"
};
unsigned Sc6ReplayTraceState::calls{};bool Sc6ReplayTraceState::setter_ok=true;
void Test(){
 using namespace QueueTest;
 alignas(16) std::array<std::byte,0x1000> mesh{};
 FreshChild child;child.state.mesh.object=mesh.data();child.historical.state=reinterpret_cast<void*>(1);child.historical.controller=reinterpret_cast<void*>(2);
 std::array<FreshChild*,1> children{&child};Sc6ReplayTraceState target,current;
 current.world_=target.world_;target.states_.push_back({{child.historical.state,child.historical.controller},{reinterpret_cast<void*>(0x12345678)}});
 At<unsigned>(mesh.data(),0x240)=0x41f;
 assert(target.DrainFreshChildRenderWork(current,children).ok());
 assert(!(At<unsigned>(mesh.data(),0x240)&0x10)&&Sc6ReplayTraceState::calls==1);
 target.values_.visibility=0x41f;assert(target.DrainFreshChildRenderWork(current,children).ok());assert(At<unsigned>(mesh.data(),0x240)&0x10);
 target.values_.available=false;assert(!target.DrainFreshChildRenderWork(current,children).ok());target.values_.available=true;
 Sc6ReplayTraceState::setter_ok=false;assert(!target.DrainFreshChildRenderWork(current,children).ok());Sc6ReplayTraceState::setter_ok=true;
 child.lease.live=false;assert(!target.DrainFreshChildRenderWork(current,children).ok());child.lease.live=true;
 target.states_.clear();assert(!target.DrainFreshChildRenderWork(current,children).ok());
 target.values_.visibility=0x40f;At<unsigned>(mesh.data(),0x240)=0x40f;At<std::uintptr_t>(mesh.data(),0x910)=42;
 unsigned id{};std::uintptr_t asset{};
 assert(target.FreshChildDormantRenderSource(child,id,asset)&&id==936&&asset==42);
 for(unsigned fault=0;fault<10;++fault){
  auto bad=target;
  if(fault==0)bad.values_.visibility|=0x10;if(fault==1)bad.bindings_.id=0;
  if(fault==2)bad.bindings_.mesh=43;if(fault==3)bad.values_.available=false;
  if(fault==4)At<unsigned>(mesh.data(),0x240)|=0x10;if(fault==5)At<unsigned>(mesh.data(),0x188)=0x20;
  if(fault==6)At<void*>(mesh.data(),0x790)=mesh.data();if(fault==7)At<void*>(mesh.data(),0xa18)=mesh.data();
  if(fault==8)child.lease.live=false;if(fault==9)At<unsigned>(mesh.data(),0x3fc)=2;
  assert(!bad.FreshChildDormantRenderSource(child,id,asset));
  At<unsigned>(mesh.data(),0x240)=0x40f;At<unsigned>(mesh.data(),0x188)=At<unsigned>(mesh.data(),0x3fc)=0;
  At<void*>(mesh.data(),0x790)=At<void*>(mesh.data(),0xa18)=nullptr;child.lease.live=true;
 }

}
}
int main(){
 VisibilityTest::Test();
 QueueTest::Test();
 Sc6ReplayHost h;
 h.Drive();assert(!h.failed&&h.advanced==0&&projections==0&&constructions==1&&drains==1&&h.submissions==1);
 assert(h.PrepareFreshParticleOwners().ok()&&projections==0&&constructions==1&&drains==1&&h.submissions==1);
 h.copy.completion.value=ReplayGpuCompletion::Result::DeviceFailure;
 assert(!h.PrepareFreshParticleOwners().ok()&&projections==0);
 h.copy.completion.value=ReplayGpuCompletion::Result::Complete;--h.copy.completion.serial;
 assert(!h.PrepareFreshParticleOwners().ok()&&projections==0);
 ++h.copy.completion.serial;gpu_complete=true;
 assert(h.PrepareFreshParticleOwners().ok()&&projections==1&&constructions==1&&drains==1&&h.operation.trace_survivors);
}
