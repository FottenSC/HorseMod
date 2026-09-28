// Production preparation/admission with controlled allocator and lease services.
// This checks A-only tick routing and B ownership, not native registration.
#include <Windows.h>
#include <array>
#include <vector>
#include <span>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
enum class FailureCode {IllegalTransition,GenerationMismatch,UnsupportedContent,CapacityExceeded,ContextUnavailable,RestorePreflightFailed};
struct Status{bool v;bool ok()const{return v;}static Status success(){return{true};}static Status failure(FailureCode){return{false};}};
template<class T,std::size_t N>T Field(const std::array<std::byte,N>& b,std::size_t o){T v;std::memcpy(&v,b.data()+o,sizeof(v));return v;}
template<class T,std::size_t N>void Store(std::array<std::byte,N>& b,std::size_t o,const T& v){std::memcpy(b.data()+o,&v,sizeof(v));}
bool SameBytes(std::uintptr_t p,const void* q,std::size_t n){__try{return !n||std::memcmp(reinterpret_cast<void*>(p),q,n)==0;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool WriteBytes(std::uintptr_t p,const void* q,std::size_t n){__try{std::memcpy(reinterpret_cast<void*>(p),q,n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool weak_live=true;
bool WeakIsLive(std::uintptr_t,const void*){return weak_live;}
struct Sc6ReplayObjectLease {bool live=true;const void* object{};Status ValidateObject(const void* p)const{return {live&&p==object};}};
struct Sc6ReplayVfxState {
 struct ParticleBirth{std::uintptr_t component()const{return 0;}};
 struct ParticleBirthSet{static constexpr std::size_t capacity=32;std::span<const ParticleBirth> members()const{return{};}Status ValidateCurrentB()const{return{true};}};
 struct ReconstructionBinding{struct Identity{std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};}identity;const Sc6ReplayObjectLease* lease{};};
};
struct Sc6ReplaySchedulerState {
 struct Object{std::uintptr_t object{};std::array<int,2> weak{};};
 struct Set{std::array<std::byte,0x50> binding_layout{};std::vector<std::byte> slots;std::vector<unsigned> allocation_flags;std::vector<int> hash;};
 struct Level{std::uintptr_t address{};Object owner;std::array<Set,3> sets;std::vector<std::uintptr_t> cooldown_order;};
 struct Tick{std::uintptr_t address{};Object owner;std::array<std::byte,0x58> binding_layout{};std::vector<std::array<std::byte,16>> prerequisites;};
 struct RetainedPrimaryTicks{const void* context{};Status(*visit)(const void*,void*,bool(*)(void*,std::uintptr_t)){};};
 struct PreflightFailure{const char* check{};std::uintptr_t address{};std::size_t target_ticks{},current_ticks{},births{};struct Difference{std::uintptr_t tick{},owner{};bool current{};};std::array<Difference,8> differences;std::size_t difference_count{};};
 struct PreparedRestore{
  struct Patch{std::uintptr_t destination{};std::array<std::byte,0x58> bytes{},previous{};std::size_t size{};};
  struct Allocation{std::vector<std::byte> data;std::uintptr_t previous{};std::size_t previous_requested{};bool previous_is_added{};};
  static constexpr std::size_t excluded_capacity=48;
  struct AddedTick{std::uintptr_t address{},owner{};const Sc6ReplayObjectLease* lease{};std::array<std::byte,0x58> previous{};std::array<std::byte,16*16> constructor_prerequisites{};};
  std::array<AddedTick,excluded_capacity> added_ticks_;std::size_t added_count_{};
  std::array<std::uintptr_t,excluded_capacity> excluded_owners_{},excluded_ticks_{};
  std::size_t excluded_count_{};RetainedPrimaryTicks private_owners_{};Sc6ReplayVfxState::ParticleBirthSet birth_;
  const Sc6ReplaySchedulerState* previous_image_{};std::vector<Patch> patches_;std::vector<Allocation> allocations_;
  std::uintptr_t base_{};void* world_{};std::uint64_t epoch_{},original_epoch_{},target_epoch_{};unsigned thread_{};bool ready_{};
  std::size_t owned_bytes()const{return sizeof(*this)+patches_.capacity()*sizeof(Patch)+allocations_.capacity()*sizeof(Allocation);}
  // Controlled allocator: real preparation must pass exact displaced backing
  // and copy lengths. Allocation publication/retirement has separate tests.
  Status Allocate(std::size_t n,const void* source,std::size_t copied,std::uintptr_t old,std::size_t old_n,std::size_t,std::uintptr_t& out){
   assert(copied<=n&&bool(old)==bool(old_n));
   if(!n&&!old_n){out=0;return{true};}
   allocations_.push_back({std::vector<std::byte>(n),old,old_n});
   auto& entry=allocations_.back();if(copied)std::memcpy(entry.data.data(),source,copied);
   out=n?reinterpret_cast<std::uintptr_t>(entry.data.data()):0;return{true};
  }
  void Clear(){ready_=false;patches_.clear();allocations_.clear();previous_image_=nullptr;added_count_=0;}
  Status ValidateAddedTicks()const noexcept;
 };
 std::uintptr_t base_=1;std::uint64_t epoch_=5;std::vector<Level> levels_;std::vector<Tick> ticks_;
 std::size_t owned_bytes()const{return 2000;}
 Status ValidateBindings(std::uintptr_t,void*)const{return{true};}
 Status ValidateCurrentImage(std::uintptr_t,void*)const{return{true};}
 static bool RebaseEpochStamp(int stamp,std::uint64_t,std::uint64_t,int& out){out=stamp;return true;}
 Status PrepareRestore(const Sc6ReplaySchedulerState&,std::uintptr_t,void*,std::size_t,PreparedRestore&,PreflightFailure*,const Sc6ReplayVfxState::ParticleBirthSet*,RetainedPrimaryTicks,std::span<const Sc6ReplayVfxState::ReconstructionBinding>)const noexcept;
};
#include "scheduler_added_tick.inl"
int main(){
 using S=Sc6ReplaySchedulerState;
 S b;b.levels_.resize(1);b.levels_[0].address=0x5000;b.ticks_.resize(1);b.ticks_[0].address=0x2110;b.ticks_[0].owner={0x2000,{2,3}};
 S a=b;std::array<std::byte,0x400> component{};S::Tick fresh;fresh.owner={reinterpret_cast<std::uintptr_t>(component.data()),{8,9}};fresh.address=fresh.owner.object+0x110;
 Store(fresh.binding_layout,0x48,std::uintptr_t{0x5000});Store(fresh.binding_layout,0x50,fresh.owner.object);
 std::memcpy(component.data()+0x110,fresh.binding_layout.data(),fresh.binding_layout.size());
 Store(fresh.binding_layout,0xc,std::uint8_t{0x40});a.ticks_.push_back(fresh);
 Sc6ReplayObjectLease lease;lease.object=component.data();
 Sc6ReplayVfxState::ReconstructionBinding binding{{1,fresh.owner.object,{4,5},fresh.owner.weak},&lease};
 const auto run=[&](std::span<const Sc6ReplayVfxState::ReconstructionBinding> rows,S::PreparedRestore& out){return a.PrepareRestore(b,1,nullptr,1000000,out,nullptr,nullptr,{},rows).ok();};
 S::PreparedRestore out;assert(!run({},out)&&!out.ready_);assert(run({&binding,1},out));
 assert(out.ready_&&out.previous_image_==&b&&b.ticks_.size()==1&&out.added_count_==1);
 auto p=std::find_if(out.patches_.begin(),out.patches_.end(),[&](const auto& x){return x.destination==fresh.address;});assert(p!=out.patches_.end());
 assert(Field<std::uint8_t>(p->previous,0xc)==0&&Field<std::uint8_t>(p->bytes,0xc)==0x40);
 assert(out.ValidateAddedTicks().ok());component[0x11c]=std::byte{0x40};assert(!out.ValidateAddedTicks().ok());component[0x11c]=std::byte{};
 out.Clear();lease.live=false;assert(!run({&binding,1},out));lease.live=true;
 std::array duplicate{binding,binding};assert(!run(duplicate,out));
 auto wrong=binding;++wrong.identity.target_weak[1];assert(!run({&wrong,1},out));
 component[0x138]=std::byte{1};assert(!run({&binding,1},out));component[0x138]=std::byte{};
 component[0x11c]=std::byte{0x40};assert(!run({&binding,1},out));component[0x11c]=std::byte{};
 b.ticks_.push_back(fresh);assert(!run({&binding,1},out));b.ticks_.pop_back();
 assert(run({&binding,1},out)&&out.previous_image_==&b&&b.ticks_.size()==1);
 out.Clear();
 // Native trace construction appends one prerequisite on the common trace
 // component. Its capacity/backing is independent of the retained A rows.
 std::array<std::array<std::byte,16>,4> constructor{};
 std::memcpy(constructor[0].data(),b.ticks_[0].owner.weak.data(),8);
 Store(constructor[0],8,b.ticks_[0].address);
 constructor[3].fill(std::byte{0x6d}); // Retained unused native capacity.
 const auto original_constructor=constructor;
 auto native_prefix=fresh.binding_layout;Store(native_prefix,0xc,std::uint8_t{});
 Store(native_prefix,0x20,reinterpret_cast<std::uintptr_t>(constructor.data()));
 Store(native_prefix,0x28,1);Store(native_prefix,0x2c,4);
 std::memcpy(component.data()+0x110,native_prefix.data(),native_prefix.size());
 auto& target=a.ticks_.back();target.prerequisites={constructor[0]};
 Store(target.binding_layout,0x20,std::uintptr_t{0x12340000});
 Store(target.binding_layout,0x28,1);Store(target.binding_layout,0x2c,2);
 assert(run({&binding,1},out)&&out.ready_&&constructor==original_constructor);
 p=std::find_if(out.patches_.begin(),out.patches_.end(),[&](const auto& x){return x.destination==fresh.address;});
 assert(p!=out.patches_.end()&&Field<std::uintptr_t>(p->previous,0x20)==reinterpret_cast<std::uintptr_t>(constructor.data()));
 assert(Field<std::uintptr_t>(p->bytes,0x20)!=Field<std::uintptr_t>(p->previous,0x20));
 const auto allocation=std::find_if(out.allocations_.begin(),out.allocations_.end(),[&](const auto& x){return x.previous==reinterpret_cast<std::uintptr_t>(constructor.data());});
 assert(allocation!=out.allocations_.end()&&allocation->previous_requested==64&&allocation->data.size()==32&&allocation->previous_is_added);
 assert(!std::memcmp(allocation->data.data(),target.prerequisites.data(),16));
 assert(out.ValidateAddedTicks().ok());constructor[3][0]^=std::byte{1};assert(!out.ValidateAddedTicks().ok());constructor=original_constructor;
 out.Clear();weak_live=false;assert(!run({&binding,1},out));weak_live=true;
 constructor[0][0]^=std::byte{1};assert(!run({&binding,1},out));constructor=original_constructor;
 Store(constructor[0],8,std::uintptr_t{0x9999});assert(!run({&binding,1},out));constructor=original_constructor;
 assert(run({&binding,1},out)&&constructor==original_constructor&&b.ticks_.size()==1);
 out.Clear();auto reused=binding;reused.identity.source=reused.identity.target;
 assert(run({&reused,1},out)&&out.added_count_==1&&out.previous_image_==&b);
}
