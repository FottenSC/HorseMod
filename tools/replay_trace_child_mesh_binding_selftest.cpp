#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
#include "HorseMod/horselib/deterministic/Sc6ReplayTraceChildFactory.hpp"
namespace Horse::Deterministic {
struct Status {bool value;bool ok()const{return value;}static Status success(){return{true};}};
struct Lease {bool valid=true,registration=true,game_thread_admission=true;Status Validate()const{return{valid&&registration&&game_thread_admission};}bool registered()const{return registration;}};
struct ReplayGpuCompletion {
 enum class Result {Complete,Pending,TimedOut,Cancelled,DeviceFailure};
 bool done=true;Result value=Result::Complete;std::uint64_t serial=2;
 bool retired()const{return done;}Result result()const{return value;}std::uint64_t submitted_serial()const{return serial;}
};
struct Sc6ReplayVfxState {
 struct ReconstructionBinding {
  struct {std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};} identity;
  const Lease* lease{};
 };
};
struct Trace {
 using Sc6ReplayTraceState=Trace;
 using Object=void;
 struct Id {Object* object{};int index{},serial{};friend bool operator==(const Id&,const Id&)=default;};
 struct Ref {std::byte* state{};std::byte* controller{};};
 struct Array {std::byte* data{};int count{},capacity{};};
 struct DynamicImage {enum class Kind {ChildStrongReferences};Kind kind{};std::array<std::byte,16> header{};std::vector<std::byte> bytes;};
 struct State {Ref ref;Id actor,attachment,mesh,animation;};
 struct Root {Id component;};
 struct FreshChild {
  enum class HistoryPhase {Empty,Ready};enum class AnimationPhase {Empty,Ready};
  enum class StrongPhase {Private,Published};StrongPhase strong_phase=StrongPhase::Published;
  enum class ExecutionOutcome {None,Recovered,Committed};ExecutionOutcome execution_outcome=ExecutionOutcome::None;
  bool adopted{};
  Sc6ReplayTraceChildFactory::Journal factory;State state;Ref historical,cached_parent;Lease lease;Array strong;
  bool prepared=true,retired=false,identity_retained=true;
  HistoryPhase history_phase=HistoryPhase::Ready;AnimationPhase animation_phase=AnimationPhase::Ready;
 };
 bool captured_=true;DWORD thread_=GetCurrentThreadId();std::uintptr_t base_=0x140000000;
 void* world_{};std::array<DynamicImage,1> dynamic_{};std::size_t dynamic_count_{};
 std::vector<State> states_;std::vector<Root> roots_;std::vector<Id> live;
 template<class T>static T& At(void* p,std::size_t o){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+o);}
 bool Live(const Id& id)const{return std::find(live.begin(),live.end(),id)!=live.end();}
 Status ValidateBindings()const {for(const auto& state:states_)if(!Live(state.actor)||!Live(state.mesh)||!Live(state.animation))return{false};return{true};}
 static Status Fail(const char*){return{false};}
 #include "trace_child_mesh_binding.inl"
};
}
int main(){
 using namespace Horse::Deterministic;
 auto* old=static_cast<std::byte*>(VirtualAlloc(nullptr,0x5000,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS));assert(old);
 std::array<std::byte,0x108> control{};
 std::array<std::byte,0x410> actor{};std::array<std::byte,0xf00> mesh{};
 std::array<std::byte,0x400> animation{},root{};
 Trace source;Trace::State original;
 original.ref={old+16,old};original.mesh={old+0x1000,1,10};source.states_={original};
 Trace::FreshChild child;child.historical=original.ref;
 child.factory.phase=Sc6ReplayTraceChildFactory::Phase::Returned;
 child.factory.reference={control.data()+16,control.data()};
 child.factory.component=reinterpret_cast<std::uintptr_t>(root.data());
 child.state.ref={control.data()+16,control.data()};child.state.actor={actor.data(),2,20};
 child.state.mesh={mesh.data(),3,30};child.state.animation={animation.data(),4,40};
 source.roots_={{{root.data(),5,50}}};
 source.live={child.state.actor,child.state.mesh,child.state.animation,source.roots_[0].component};
 Trace::At<std::uintptr_t>(control.data(),0)=source.base_+0x3362590;
 Trace::At<int>(control.data(),8)=1;Trace::At<int>(control.data(),12)=2;
 Trace::At<void*>(control.data()+16,8)=actor.data();Trace::At<void*>(actor.data(),0x398)=mesh.data();
 Trace::At<void*>(mesh.data(),0x190)=actor.data();Trace::At<std::uintptr_t>(mesh.data(),0)=source.base_+0x38829c0;
 Trace::At<void*>(mesh.data(),0xab0)=animation.data();
 Sc6ReplayVfxState::ReconstructionBinding binding;
 assert(source.FreshChildMeshBinding(child,binding).ok());
 child.lease.game_thread_admission=false;
 assert(!source.FreshChildMeshBinding(child,binding).ok());
 assert(source.FreshChildRenderBinding(child,binding).ok());
 child.lease.registration=false;assert(!source.FreshChildRenderBinding(child,binding).ok());child.lease.registration=true;
 child.lease.game_thread_admission=true;
 assert((binding.identity.source==reinterpret_cast<std::uintptr_t>(original.mesh.object)
  &&binding.identity.target==reinterpret_cast<std::uintptr_t>(mesh.data())&&binding.lease==&child.lease
  &&binding.identity.source_weak==std::array<int,2>({1,10})&&binding.identity.target_weak==std::array<int,2>({3,30})));
 const auto saved=binding;
 const auto rejects=[&]{assert(!source.FreshChildMeshBinding(child,binding).ok());assert(!std::memcmp(&saved,&binding,sizeof(binding)));};
 child.lease.valid=false;rejects();child.lease.valid=true;
 Trace::At<int>(control.data(),8)=0;rejects();Trace::At<int>(control.data(),8)=1;
 Trace::At<int>(control.data(),12)=1;rejects();Trace::At<int>(control.data(),12)=2;
 Trace::At<void*>(actor.data(),0x398)=animation.data();rejects();Trace::At<void*>(actor.data(),0x398)=mesh.data();
 source.live.pop_back();rejects();source.live.push_back(source.roots_[0].component);
 child.state.mesh.serial++;rejects();child.state.mesh.serial--;
 child.historical.controller=old+0x100;rejects();child.historical.controller=old;
 child.animation_phase=Trace::FreshChild::AnimationPhase::Empty;rejects();child.animation_phase=Trace::FreshChild::AnimationPhase::Ready;
 child.retired=true;rejects();child.retired=false;
 assert(source.FreshChildMeshBinding(child,binding).ok());
 {
  Trace common,current;current.states_={child.state};current.live=source.live;
  common.live=source.live;current.dynamic_count_=1;
  child.strong={reinterpret_cast<std::byte*>(&child.factory.reference),0,1};
  auto& strong=current.dynamic_[0];strong.bytes.resize(16);Trace::At<Trace::Ref>(strong.bytes.data(),0)=child.state.ref;
  Trace::At<int>(strong.header.data(),8)=1;
  ReplayGpuCompletion completion;bool is_dead=true;
  assert(common.ValidateFreshChildExecution(child,current,completion,1,is_dead).ok()&&!is_dead);
  const auto reject=[&]{is_dead=true;assert(!common.ValidateFreshChildExecution(child,current,completion,1,is_dead).ok()&&is_dead);};
  completion.serial=1;reject();completion.serial=2;
  completion.done=false;reject();completion.done=true;
  for(auto result:{ReplayGpuCompletion::Result::Pending,ReplayGpuCompletion::Result::TimedOut,
      ReplayGpuCompletion::Result::Cancelled,ReplayGpuCompletion::Result::DeviceFailure}) {completion.value=result;reject();}
  completion.value=ReplayGpuCompletion::Result::Complete;
  Trace::At<Trace::Ref>(child.state.ref.state,0xa0).controller=old;reject();Trace::At<Trace::Ref>(child.state.ref.state,0xa0)={};
  // The expired state begins on an inaccessible page. Only its anchored
  // 16-byte controller header remains readable; actor/mesh are inaccessible too.
  auto* pages=static_cast<std::byte*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(pages);
  auto* expired=pages+4096-16;DWORD protection{};assert(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&protection));
  auto dying=child;dying.factory.reference={expired+16,expired};dying.state.ref={expired+16,expired};
  dying.state.actor={old+0x1000,20,200};dying.state.mesh={old+0x2000,30,300};
  dying.strong.data=reinterpret_cast<std::byte*>(&dying.factory.reference);dying.lease.valid=false;
  Trace::At<std::uintptr_t>(expired,0)=common.base_+0x3362590;Trace::At<int>(expired,8)=0;Trace::At<int>(expired,12)=1;
  current.states_.clear();strong.bytes.clear();Trace::At<int>(strong.header.data(),8)=0;
  is_dead=false;assert(common.ValidateFreshChildExecution(dying,current,completion,1,is_dead).ok()&&is_dead);
  common.live.push_back(dying.state.mesh);assert(!common.ValidateFreshChildExecution(dying,current,completion,1,is_dead).ok());common.live.pop_back();
  strong.bytes.resize(16);Trace::At<Trace::Ref>(strong.bytes.data(),0)=dying.state.ref;Trace::At<int>(strong.header.data(),8)=1;
  assert(!common.ValidateFreshChildExecution(dying,current,completion,1,is_dead).ok());strong.bytes.clear();Trace::At<int>(strong.header.data(),8)=0;
  dying.identity_retained=false;assert(!common.ValidateFreshChildExecution(dying,current,completion,1,is_dead).ok());
  VirtualFree(pages,0,MEM_RELEASE);
 }
 VirtualFree(old,0,MEM_RELEASE);
}
