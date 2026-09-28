// Execute the production private-image projection. Native lease and live-prefix
// checks are controlled dependencies; hash lookup is independently exercised
// against the shipped routine by NativeCandidateRegionsSelfTest.
#include <array>
#include <vector>
#include <span>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <climits>
#include <utility>
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(int){return {false};}};
constexpr int invalid=1,capacity=2;
unsigned GetCurrentThreadId(){return 7;}
template<class T,std::size_t N>T Field(const std::array<std::byte,N>& x,std::size_t o){T v;std::memcpy(&v,x.data()+o,sizeof(v));return v;}
template<std::size_t N,class T>void put(std::array<std::byte,N>& x,std::size_t o,T v){std::memcpy(x.data()+o,&v,sizeof(v));}
struct Sc6ReplayVfxState {
 struct ReconstructionBinding {struct {std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};} identity;};
 mutable unsigned calls{};unsigned reject_call{};
 Status ValidateReconstructionBindings(std::span<const ReconstructionBinding>)const{return {++calls!=reject_call};}
};
struct Sc6ReplaySchedulerState {
 struct BindingFailure {const char* check{};};
 struct ObjectBinding {std::uintptr_t object{};std::array<int,2> weak{};} world_;
 struct {std::uintptr_t scene{},control{};} scene_;
 struct SetImage {std::array<std::byte,0x50> binding_layout{};std::vector<std::byte> slots;std::vector<unsigned> allocation_flags;std::vector<int> hash;};
 struct Level {ObjectBinding owner;std::uintptr_t address{};std::array<SetImage,3> sets;std::vector<std::uintptr_t> cooldown_order;};
 struct Tick {std::uintptr_t address{};ObjectBinding owner;std::array<std::byte,0x58> binding_layout{};std::vector<std::array<std::byte,16>> prerequisites;};
 std::uintptr_t base_{};std::uint64_t epoch_{};unsigned thread_{};std::vector<Level> levels_;std::vector<Tick> ticks_;
 static inline bool native_bindings_valid=true;
 std::size_t owned_bytes()const{return 100+levels_.size()*200+ticks_.size()*200;}
 Status BindScene(std::uintptr_t scene){scene_.scene=scene;scene_.control=99;return {true};}
 Status ValidateBindings(std::uintptr_t,void*,BindingFailure* =nullptr)const{return {native_bindings_valid};}
 Status ReconstructComponentBindings(const Sc6ReplayVfxState&,std::span<const Sc6ReplayVfxState::ReconstructionBinding>,std::size_t,Sc6ReplaySchedulerState&,BindingFailure* =nullptr)const noexcept;
 using ComponentBinding=Sc6ReplayVfxState::ReconstructionBinding;
 struct ComponentOwnerProof {const void* context{};Status(*validate)(const void*,std::span<const ComponentBinding>){};};
 Status ReconstructOwnedComponentBindings(ComponentOwnerProof,std::span<const ComponentBinding>,std::size_t,Sc6ReplaySchedulerState&,BindingFailure* =nullptr)const noexcept;
 bool SameLogicalImage(const Sc6ReplaySchedulerState&)const noexcept;
 static Status RehashTickSetStorage(std::span<std::byte>,std::span<const unsigned>,std::span<int>)noexcept;
};
#include "scheduler_reconstruction.inl"
int main(){
 Sc6ReplaySchedulerState a;a.base_=1;a.world_.object=2;a.thread_=7;a.epoch_=55;a.levels_.resize(1);a.ticks_.resize(2);
 auto& first=a.ticks_[0];first.address=0x1110;first.owner={0x1000,{10,11}};put(first.binding_layout,0x50,std::uintptr_t{0x1000});put(first.binding_layout,0x30,std::uintptr_t{0x2110});
 auto& second=a.ticks_[1];second.address=0x2110;second.owner={0x2000,{20,21}};put(second.binding_layout,0x50,std::uintptr_t{0x2000});
 second.prerequisites.resize(1);std::memcpy(second.prerequisites[0].data(),first.owner.weak.data(),8);put(second.prerequisites[0],8,first.address);
 a.levels_[0].cooldown_order={0x1110,0x2110};
 auto& set=a.levels_[0].sets[0];set.slots.resize(32);set.allocation_flags={3};set.hash={-1,-1};
 std::memcpy(set.slots.data(),&first.address,8);std::memcpy(set.slots.data()+16,&second.address,8);
 Sc6ReplayVfxState owners;
 std::array<Sc6ReplayVfxState::ReconstructionBinding,2> bindings{{{{0x1000,0x9000,{10,11},{90,91}}},{{0x2000,0x2000,{20,21},{20,21}}}}};
 Sc6ReplaySchedulerState out;
 if(!a.ReconstructComponentBindings(owners,bindings,100000,out).ok() || owners.calls!=2)return 1;
 if(out.ticks_[0].address!=0x9110 || out.ticks_[0].owner.object!=0x9000 || out.ticks_[0].owner.weak!=bindings[0].identity.target_weak)return 2;
 if(Field<std::uintptr_t>(out.ticks_[0].binding_layout,0x50)!=0x9000 || out.levels_[0].cooldown_order!=std::vector<std::uintptr_t>{0x9110,0x2110})return 3;
 if(Field<std::uintptr_t>(out.ticks_[1].prerequisites[0],8)!=0x9110 || std::memcmp(out.ticks_[1].prerequisites[0].data(),bindings[0].identity.target_weak.data(),8))return 4;
 if(a.ticks_[0].address!=0x1110 || Field<std::uintptr_t>(a.ticks_[1].prerequisites[0],8)!=0x1110 || a.levels_[0].sets[0].hash!=std::vector<int>{-1,-1})return 5;
 if(out.ticks_[1].address!=0x2110 || out.epoch_!=55 || out.thread_!=7)return 6;
 if(a.ReconstructComponentBindings(owners,bindings,100000,out).ok())return 7;
 Sc6ReplaySchedulerState empty;
 owners.calls=0;owners.reject_call=2;
 if(a.ReconstructComponentBindings(owners,bindings,100000,empty).ok() || empty.base_)return 8;
 owners.calls=0;owners.reject_call=0;
 Sc6ReplaySchedulerState::native_bindings_valid=false;
 if(a.ReconstructComponentBindings(owners,bindings,100000,empty).ok() || empty.base_)return 9;
 Sc6ReplaySchedulerState::native_bindings_valid=true;
 if(a.ReconstructComponentBindings(owners,bindings,1,empty).ok() || empty.base_)return 10;
 {
  unsigned calls{};
  Sc6ReplaySchedulerState::ComponentOwnerProof proof{&calls,[](const void* context,std::span<const Sc6ReplaySchedulerState::ComponentBinding>) -> Status {
   ++*const_cast<unsigned*>(static_cast<const unsigned*>(context));throw 1;
  }};
  Sc6ReplaySchedulerState rejected;
  if(a.ReconstructOwnedComponentBindings(proof,bindings,100000,rejected).ok() || calls!=1 || rejected.base_)return 25;
 }
 {
  auto reused=bindings;reused[0].identity.target=reused[0].identity.source;
  Sc6ReplaySchedulerState replacement;
  if(!a.ReconstructComponentBindings(owners,reused,100000,replacement).ok()
      || replacement.ticks_[0].owner.weak!=reused[0].identity.target_weak)return 23;
  if(std::memcmp(replacement.ticks_[1].prerequisites[0].data(),reused[0].identity.target_weak.data(),8))return 24;
 }
 {
  auto b=a;put(a.levels_[0].sets[0].binding_layout,0,std::uintptr_t{100});put(b.levels_[0].sets[0].binding_layout,0,std::uintptr_t{200});
  put(a.ticks_[1].binding_layout,0x20,std::uintptr_t{300});put(b.ticks_[1].binding_layout,0x20,std::uintptr_t{400});
  if(!a.SameLogicalImage(b))return 12;
  auto bad=b;++bad.ticks_[0].owner.weak[1];if(a.SameLogicalImage(bad))return 13;
  bad=b;bad.ticks_[0].binding_layout[0xc]=std::byte{64};if(a.SameLogicalImage(bad))return 14;
  bad=b;bad.ticks_[1].prerequisites[0][0]=std::byte{99};if(a.SameLogicalImage(bad))return 15;
  bad=b;std::reverse(bad.levels_[0].cooldown_order.begin(),bad.levels_[0].cooldown_order.end());if(a.SameLogicalImage(bad))return 16;
  bad=b;bad.levels_[0].sets[0].hash[0]=20;if(a.SameLogicalImage(bad))return 17;
  bad=b;bad.levels_[0].sets[0].allocation_flags[0]=1;if(a.SameLogicalImage(bad))return 18;
  bad=b;put(bad.ticks_[1].binding_layout,0x20,std::uintptr_t{});if(a.SameLogicalImage(bad))return 19;
  bad=b;++bad.epoch_;if(a.SameLogicalImage(bad))return 20;
  bad=b;bad.ticks_.push_back(first);if(a.SameLogicalImage(bad))return 21;
  bad=b;bad.levels_[0].sets[0].slots[0]^=std::byte{1};if(a.SameLogicalImage(bad))return 22;
 }
 a.ticks_[0].address=0x1300;
 if(a.ReconstructComponentBindings(owners,bindings,100000,empty).ok() || empty.base_)return 11;
}
