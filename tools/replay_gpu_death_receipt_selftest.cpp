#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <thread>
#include <vector>
SRWLOCK emitter_lifetime_lock=SRWLOCK_INIT;
template<class T>T Field(const void* p,std::size_t n){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+n,sizeof(v));return v;}
template<class T>void Put(void* p,std::size_t n,const T& v){std::memcpy(static_cast<std::byte*>(p)+n,&v,sizeof(v));}
enum class FailureCode {GenerationMismatch,IllegalTransition,ContextUnavailable,RestoreVerificationFailed,UndoFailed,CapacityExceeded};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
struct ReplayGpuCompletion {
 enum class Result {Complete,Pending,TimedOut,Cancelled,DeviceFailure,Error};
 std::uint64_t serial=1;Result state=Result::Complete;bool flight{};
 std::uint64_t submitted_serial()const{return serial;}bool retired()const{return !flight;}Result result()const{return state;}
};
struct Sc6ReplayCpuEmitterState {
 enum class Kind {Sprite,Mesh,Gpu};
 std::uintptr_t base_{};Kind kind_=Kind::Sprite;
 struct Binding {std::uintptr_t address{};std::array<int,2> weak{};};
 struct Prepared {
  enum class NativeLifetime {Live,Destroying,Destroyed,Invalid};
  std::uintptr_t base_{},execution_slots_{};std::array<Binding,9> bindings_{};
  void *gpu_target_{},*displaced_{},*object_{};unsigned thread_{};Kind kind_=Kind::Gpu;
  bool executing_=true,execution_settled_{},published_=true,component_replaced_=true;
  bool gpu_write_complete_{},gpu_undo_started_{},forgot{};std::size_t execution_budget_{};
  int execution_count_=1,execution_capacity_=1;std::size_t ordinal_{};
  NativeLifetime native_lifetime_=NativeLifetime::Live,component_lifetime_=NativeLifetime::Live;
  Prepared* lifetime_next_{};static inline Prepared* lifetime_head_{};
  const ReplayGpuCompletion* fresh_gpu_completion_{};std::uint64_t native_death_submission_{};bool fresh_gpu_retired_{};
  struct Allocation{void* address{};std::size_t charged{},requested{};};std::vector<Allocation> allocations_;
  unsigned lod_peak_{};std::uint64_t published_fingerprint_{};std::array<std::byte,0x2a0> gpu_undo_{};
  std::uint64_t BackingFingerprint()const noexcept;
  Status SettleFreshGpuDestruction()noexcept;Status ValidatePublished()const noexcept;
  Status SettleFreshCpuComponentDestruction()noexcept;
  Status SettleNativeDestruction(const Sc6ReplayCpuEmitterState*)noexcept;
  Status ReopenExecutionForUndo(const Sc6ReplayCpuEmitterState*)noexcept;
  Status UndoPublication()noexcept;
  void ForgetExecutionOwner(){forgot=true;}
  void RebindRootSlots(void*,void*){}
  bool native_destroyed()const{return native_lifetime_==NativeLifetime::Destroyed;}
  void* volatile* ResolveDestroyedSlot()const{return nullptr;}void* volatile* ResolveSlot()const{return nullptr;}
  bool gpu_storage()const{return kind_==Kind::Gpu;}
  unsigned DestructionEntryFailure(bool,unsigned)const noexcept;
  unsigned destruction_entry_failure_{},destruction_entries_{},destruction_returns_{};
  static void NativeDestruction(void*,bool,unsigned,bool)noexcept;
  bool ValidateGpuDestructionEntry(unsigned)const noexcept;
  static void NativeGpuDestruction(void*,unsigned,bool)noexcept;
  bool ValidateComponentDestructionEntry()const noexcept;
  static void NativeComponentDestruction(void*,bool)noexcept;
  bool FreshGpuDestructionWitness()const noexcept;
  bool FreshComponentDestructionWitness(std::uintptr_t,const std::array<int,2>&)const noexcept;
  bool FreshComponentRetirementWitness(std::uintptr_t,const std::array<int,2>&)const noexcept;
 };
 Status ValidateNativeGraph(const void*)const{return Status::failure(FailureCode::GenerationMismatch);}
 Status ValidateDisplaced(const Prepared&)const{return Status::failure(FailureCode::GenerationMismatch);}
};
bool Copy(void* destination,const void* source,std::size_t bytes){std::memcpy(destination,source,bytes);return true;}
std::uintptr_t Vtable(Sc6ReplayCpuEmitterState::Kind kind){return kind==Sc6ReplayCpuEmitterState::Kind::Gpu?0x394c100:kind==Sc6ReplayCpuEmitterState::Kind::Mesh?0x3949d88:0x3949b60;}
bool weak_live=true;std::uintptr_t component_address{};
std::uintptr_t ResolveWeak(const void* p){const auto value=Field<std::uintptr_t>(p,0);return weak_live||value!=component_address?value:0;}
#include "gpu_death_receipt.inl"
int main(){
 using P=Sc6ReplayCpuEmitterState::Prepared;using L=P::NativeLifetime;
 auto* thunk=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(thunk);
 const unsigned char code[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};std::memcpy(thunk,code,sizeof(code));Put(thunk,2,reinterpret_cast<std::uintptr_t>(&ResolveWeak));
 DWORD old{};assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));
 for(int scenario=0;scenario<8;++scenario){
  auto* component=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  auto* root=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(component&&root);
  std::array<void*,2> slots{root,reinterpret_cast<void*>(0x1234)};std::array<std::byte,0x2a0> header{};P p;p.thread_=GetCurrentThreadId();p.base_=reinterpret_cast<std::uintptr_t>(thunk)-0xf823f0;
  p.execution_count_=p.execution_capacity_=2;p.gpu_target_=root;p.object_=header.data();p.execution_slots_=reinterpret_cast<std::uintptr_t>(slots.data());
  auto& binding=p.bindings_[1];binding.address=reinterpret_cast<std::uintptr_t>(component);std::memcpy(binding.weak.data(),&binding.address,8);
  Put(root,0,p.base_+Vtable(Sc6ReplayCpuEmitterState::Kind::Gpu));Put(root,0x18,binding.address);
  Put(component,0xa50,p.execution_slots_);Put(component,0xa58,int{2});Put(component,0xa5c,int{2});weak_live=true;
  component_address=binding.address;std::array<std::byte,0xc0> lod{};auto& asset=p.bindings_[2];asset.address=reinterpret_cast<std::uintptr_t>(lod.data());std::memcpy(asset.weak.data(),&asset.address,8);
  ReplayGpuCompletion completion;p.fresh_gpu_completion_=&completion;p.allocations_.push_back({header.data(),header.size(),header.size()});
  P::lifetime_head_=&p;
  // A private header or unrelated root cannot provide the native receipt.
  P::NativeGpuDestruction(header.data(),1,false);P::NativeGpuDestruction(header.data(),1,true);assert(p.native_lifetime_==L::Live);
  if(scenario==1){P::NativeGpuDestruction(root,1,true);assert(p.native_lifetime_==L::Invalid);}
  else if(scenario==2){P::NativeGpuDestruction(root,0,false);P::NativeGpuDestruction(root,0,true);assert(p.native_lifetime_==L::Invalid);}
  else if(scenario==3){p.component_replaced_=false;P::NativeGpuDestruction(root,1,false);P::NativeGpuDestruction(root,1,true);assert(p.native_lifetime_==L::Live);}
  else {
   P::NativeGpuDestruction(root,1,false);assert(p.native_lifetime_==L::Destroying);
   assert(!p.FreshComponentDestructionWitness(binding.address,binding.weak));
   assert(VirtualProtect(root,4096,PAGE_NOACCESS,&old));slots[0]=nullptr;
   if(scenario==4){std::thread other([&]{P::NativeGpuDestruction(root,1,true);});other.join();assert(p.native_lifetime_==L::Invalid);}
   else {
    P::NativeGpuDestruction(root,1,true);assert(p.native_lifetime_==L::Destroyed&&p.native_death_submission_==1);
    ++completion.serial; // An event before component teardown remains too old.
    assert(!p.FreshComponentDestructionWitness(binding.address,binding.weak));
    if(scenario==7){
     // Native retired only the GPU slot. The mixed component and its CPU peer
     // remain active; the emitter page is inaccessible and must never be read.
     completion.serial=1;assert(!p.SettleFreshGpuDestruction().ok());completion.serial=2;
     completion.flight=true;assert(!p.SettleFreshGpuDestruction().ok());completion.flight=false;
     completion.state=ReplayGpuCompletion::Result::TimedOut;assert(!p.SettleFreshGpuDestruction().ok());completion.state=ReplayGpuCompletion::Result::Complete;
     slots[0]=reinterpret_cast<void*>(0x5678);assert(!p.SettleFreshGpuDestruction().ok());slots[0]=nullptr;
     Put(component,0xa58,int{1});assert(!p.SettleFreshGpuDestruction().ok());Put(component,0xa58,int{2});
     Put(component,0x188,unsigned{0x40000000});assert(!p.SettleFreshGpuDestruction().ok());Put(component,0x188,unsigned{});
     weak_live=false;assert(!p.SettleFreshGpuDestruction().ok());weak_live=true;
     assert(p.SettleFreshGpuDestruction().ok()&&p.fresh_gpu_retired_&&p.execution_settled_);
     assert(p.ValidatePublished().ok()&&!p.FreshComponentRetirementWitness(binding.address,binding.weak));
     assert(slots[0]==nullptr&&slots[1]==reinterpret_cast<void*>(0x1234));
    }
    else if(scenario==5){P::NativeComponentDestruction(component,true);assert(p.component_lifetime_==L::Invalid);}
    else {
     P::NativeComponentDestruction(component,false);assert(p.component_lifetime_==L::Destroying);
     weak_live=false;assert(VirtualProtect(component,4096,PAGE_NOACCESS,&old));
     P::NativeComponentDestruction(component,true);assert(p.component_lifetime_==L::Destroyed);
     assert(p.FreshComponentDestructionWitness(binding.address,binding.weak)&&p.native_death_submission_==2);
     assert(!p.SettleFreshGpuDestruction().ok()&&!p.fresh_gpu_retired_&&!p.execution_settled_);
     assert(!p.FreshComponentRetirementWitness(binding.address,binding.weak));
     ++completion.serial;
     for(auto failure:{ReplayGpuCompletion::Result::Pending,ReplayGpuCompletion::Result::TimedOut,ReplayGpuCompletion::Result::Cancelled,ReplayGpuCompletion::Result::DeviceFailure,ReplayGpuCompletion::Result::Error}) {
      completion.state=failure;assert(!p.SettleFreshGpuDestruction().ok()&&!p.fresh_gpu_retired_&&!p.execution_settled_);
     }
     completion.state=ReplayGpuCompletion::Result::Complete;completion.flight=true;
     assert(!p.SettleFreshGpuDestruction().ok());completion.flight=false;
     assert(p.SettleFreshGpuDestruction().ok()&&p.fresh_gpu_retired_&&p.execution_settled_&&p.allocations_.size()==1);
     assert(p.ValidatePublished().ok());
     assert(p.FreshComponentRetirementWitness(binding.address,binding.weak));
     bool wrong_thread=true;std::thread other([&]{wrong_thread=p.FreshComponentRetirementWitness(binding.address,binding.weak);});other.join();assert(!wrong_thread);
     p.published_=false;assert(!p.FreshComponentRetirementWitness(binding.address,binding.weak));p.published_=true;
     // A recorded retirement survives reuse of the query by another operation.
     completion.state=ReplayGpuCompletion::Result::Pending;p.execution_settled_=false;
     assert(p.SettleFreshGpuDestruction().ok());
     auto wrong=binding.weak;++wrong[1];assert(!p.FreshComponentDestructionWitness(binding.address,wrong));
     if(scenario==6){P::NativeGpuDestruction(root,1,true);assert(!p.FreshComponentDestructionWitness(binding.address,binding.weak));}
    }
   }
  }
  // No receipt changes publication, ownership, or execution settlement.
  assert(p.published_&&p.gpu_target_==root&&p.object_==header.data()&&p.allocations_.size()==1);
  P::lifetime_head_=nullptr;VirtualFree(component,0,MEM_RELEASE);VirtualFree(root,0,MEM_RELEASE);
 }
 {
  auto* component=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  auto* root=static_cast<std::byte*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(component&&root);
  P p;p.kind_=Sc6ReplayCpuEmitterState::Kind::Sprite;p.thread_=GetCurrentThreadId();p.base_=reinterpret_cast<std::uintptr_t>(thunk)-0xf823f0;p.object_=root;
  std::array<void*,1> slots{root};p.execution_slots_=reinterpret_cast<std::uintptr_t>(slots.data());
  auto& binding=p.bindings_[1];binding.address=reinterpret_cast<std::uintptr_t>(component);std::memcpy(binding.weak.data(),&binding.address,8);component_address=binding.address;weak_live=true;
  Put(root,0,p.base_+Vtable(p.kind_));Put(root,0x18,binding.address);Put(component,0xa50,p.execution_slots_);Put(component,0xa58,1);Put(component,0xa5c,1);
  std::array<std::byte,0xc0> lod{};auto& asset=p.bindings_[2];asset.address=reinterpret_cast<std::uintptr_t>(lod.data());std::memcpy(asset.weak.data(),&asset.address,8);
  ReplayGpuCompletion completion;p.fresh_gpu_completion_=&completion;p.allocations_.push_back({root,4096,4096});P::lifetime_head_=&p;
  P::NativeDestruction(root,false,1,false);assert(p.native_lifetime_==L::Destroying);
  assert(VirtualProtect(root,4096,PAGE_NOACCESS,&old));P::NativeDestruction(root,false,1,true);slots[0]=nullptr;
  assert(p.native_destroyed()&&!p.FreshComponentDestructionWitness(binding.address,binding.weak));
  P::NativeComponentDestruction(component,false);weak_live=false;assert(VirtualProtect(component,4096,PAGE_NOACCESS,&old));P::NativeComponentDestruction(component,true);
  if(!p.FreshComponentDestructionWitness(binding.address,binding.weak))return 2;
  assert(!p.FreshGpuDestructionWitness());
  assert(!p.SettleFreshCpuComponentDestruction().ok()&&!p.execution_settled_&&!p.allocations_.empty());
  ++completion.serial;
  for(auto failure:{ReplayGpuCompletion::Result::Pending,ReplayGpuCompletion::Result::TimedOut,ReplayGpuCompletion::Result::Cancelled,ReplayGpuCompletion::Result::DeviceFailure}) {
   completion.state=failure;assert(!p.SettleFreshCpuComponentDestruction().ok()&&!p.execution_settled_&&!p.allocations_.empty());
  }
  completion.state=ReplayGpuCompletion::Result::Complete;completion.flight=true;assert(!p.SettleFreshCpuComponentDestruction().ok());completion.flight=false;
  Put(lod.data(),0xb4,1);assert(!p.SettleFreshCpuComponentDestruction().ok());Put(lod.data(),0xb4,0);
  assert(p.SettleFreshCpuComponentDestruction().ok()&&p.allocations_.empty()&&p.ValidatePublished().ok());
  assert(p.FreshComponentRetirementWitness(binding.address,binding.weak));
  assert(p.SettleFreshCpuComponentDestruction().ok());
  assert(p.ReopenExecutionForUndo(nullptr).ok()&&!p.execution_settled_);
  assert(p.SettleNativeDestruction(nullptr).ok()&&p.execution_settled_);
  Sc6ReplayCpuEmitterState original;assert(!p.SettleNativeDestruction(&original).ok());
  assert(p.UndoPublication().ok()&&!p.published_&&!p.object_&&p.allocations_.empty()&&p.forgot);
  assert(p.UndoPublication().ok());
  P::lifetime_head_=nullptr;VirtualFree(component,0,MEM_RELEASE);VirtualFree(root,0,MEM_RELEASE);
 }
 VirtualFree(thunk,0,MEM_RELEASE);
}
