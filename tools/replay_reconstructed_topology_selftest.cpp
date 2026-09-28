#include <array>
#include <vector>
#include <span>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
enum class FailureCode {IllegalTransition,GenerationMismatch,UnsupportedContent,CapacityExceeded,ContextUnavailable};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
template<class T>T At(const void* p,std::size_t n){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+n,sizeof(v));return v;}
template<class T>void Put(void* p,std::size_t n,T v){std::memcpy(static_cast<std::byte*>(p)+n,&v,sizeof(v));}
unsigned GetCurrentThreadId(){return 7;}
bool SameIdentity(const void* a,const void* b){return !std::memcmp(a,b,12);}
bool Live(std::uintptr_t,const std::array<int,2>&,std::uintptr_t p){return p!=1;}
bool CopyBytes(void* a,const void*,std::size_t n){assert(n==8);std::uint64_t epoch=42;std::memcpy(a,&epoch,n);return true;}
struct Lease {bool live{true};Status Validate()const{return {live};}};
struct Sc6ReplayVfxState {
 struct Component {std::uintptr_t address;std::array<int,2> weak;};
 struct Slot {std::array<std::byte,0xc0> bytes{};std::array<int,2> actor_weak{};};
 struct ReconstructionBinding {struct Identity {std::uintptr_t source,target;std::array<int,2> source_weak,target_weak;} identity;};
 struct ParticleBirth {
  const Sc6ReplayVfxState *target_{},*current_{};void* battle_{};std::uintptr_t component_{};std::array<int,2> weak_{};std::uint64_t epoch_{};bool reconstructed_{};
  auto component()const{return component_;}
  Status ValidateLifetime()const noexcept;
 };
 struct ParticleBirthSet {
  static constexpr std::size_t capacity=32;std::array<ParticleBirth,capacity> members_{};std::size_t count_{};
  bool empty()const{return !count_;}auto members()const{return std::span(members_.data(),count_);}
  Status ValidateCurrentB()const {for(auto& b:members())if(!b.current_->held || !b.ValidateLifetime().ok())return {false};return {true};}
 };
 enum class Topology {Unchanged,SingleBirth,Births};
 struct RetainedSecondaryOwners {const void* context{};Status(*visit)(const void*,void*,bool(*)(void*,std::uintptr_t)){};};
 bool valid_{true},held{true},mapped{true};std::uintptr_t base_{10},manager_{20};std::array<int,2> manager_weak_{30,40};unsigned thread_{7};
 Lease* object_lease_{};std::array<int,2> counts_{};struct Slots:std::vector<Slot>{using std::vector<Slot>::operator=;const Slot* get()const{return data();}} slots_;std::size_t constructed_{};std::vector<Component> components_;
 Status ValidateReconstructionDependencies()const{return {mapped};}
 Status ValidateHeld(std::uintptr_t,void*)const{return {held && object_lease_->live};}
 Status ValidateReconstructionBindings(std::span<const ReconstructionBinding> b)const{return {mapped && b.size()==components_.size()};}
 Status PrepareRecoveryRetirement(const Sc6ReplayVfxState&,void*,ParticleBirthSet&)const noexcept;
 Status PrepareExecutionParticles(const Sc6ReplayVfxState&,void*,ParticleBirthSet&)const noexcept;
 Status PrepareTopology(const Sc6ReplayVfxState&,void*,Topology&,ParticleBirthSet&,RetainedSecondaryOwners,std::span<const ReconstructionBinding>)const noexcept;
};
#include "reconstructed_topology.inl"
#include "recovery_topology.inl"
Sc6ReplayVfxState::Slot slot(std::uintptr_t owner,int id){Sc6ReplayVfxState::Slot s;Put(s.bytes.data(),0,owner);Put(s.bytes.data(),8,id);s.actor_weak={int(owner),99};return s;}
int main(){
 Lease old{false},original;Sc6ReplayVfxState a,b;a.object_lease_=&old;b.object_lease_=&original;
 // Native completion removes A's first slot. B's survivor shifts to index0;
 // a newly born owner follows. Fresh replacement9 is absent from complete B.
 a.counts_={2,0};a.slots_={slot(1,100),slot(2,101)};a.components_={{1,{1,99}},{2,{2,99}}};
 b.counts_={2,0};b.slots_={slot(2,101),slot(3,102)};b.components_={{2,{2,99}},{3,{3,99}}};
 std::array<Sc6ReplayVfxState::ReconstructionBinding,2> bindings{{{{1,9,{1,99},{9,99}}},{{2,2,{2,99},{2,99}}}}};
 Sc6ReplayVfxState::Topology topology;Sc6ReplayVfxState::ParticleBirthSet births;
 auto prepare=[&]{births={};return a.PrepareTopology(b,&a,topology,births,{},bindings).ok();};
 if(!prepare()){std::puts("mapped death plus native slot compaction rejected");return 1;}
 assert(topology==Sc6ReplayVfxState::Topology::SingleBirth&&births.count_==1&&births.members_[0].component_==3&&births.members_[0].reconstructed_);
 assert(b.components_.size()==2&&b.components_[0].address==2&&b.components_[1].address==3);
 a.mapped=false;assert(!prepare());a.mapped=true;b.held=false;assert(!prepare());b.held=true;
 b.slots_[0]=slot(2,999);assert(!prepare());b.slots_[0]=slot(2,101);
 // A prior rollback recreates the same logical slot with a different native
 // component. Keep that exact B version separate from fresh A, even at equal IDs.
 b.slots_[1]=slot(3,100);assert(prepare()&&births.count_==1&&births.members_[0].component_==3);
 assert(At<int>(b.slots_[1].bytes.data(),8)==100&&At<std::uintptr_t>(b.slots_[1].bytes.data(),0)==3);
 b.slots_.push_back(slot(4,100));b.components_.push_back({4,{4,99}});b.counts_[0]=3;assert(!prepare());
 b.slots_.pop_back();b.components_.pop_back();b.counts_[0]=2;b.slots_[1]=slot(3,102);
 b.slots_[1].actor_weak={3,98};assert(!prepare());b.slots_[1]=slot(3,102);
 b.slots_.push_back(slot(2,101));b.counts_[0]=3;assert(!prepare());b.slots_.pop_back();b.counts_[0]=2;
 b.slots_[1]=slot(9,102);b.components_[1]={9,{9,99}};assert(!prepare());b.slots_[1]=slot(3,102);b.components_[1]={3,{3,99}};
 b.slots_[0]=slot(1,100);b.components_[0]={1,{1,99}};assert(!prepare());b.slots_[0]=slot(2,101);b.components_[0]={2,{2,99}};
 b.slots_.pop_back();b.components_.pop_back();b.counts_[0]=1;assert(prepare()&&births.empty());
 Sc6ReplayVfxState::RetainedSecondaryOwners empty{&a,[](const void*,void*,bool(*)(void*,std::uintptr_t)){return Status::success();}};
 births={};if(!a.PrepareTopology(b,&a,topology,births,empty,bindings).ok())return 2;
 empty.visit=[](const void*,void* p,bool(*accept)(void*,std::uintptr_t)){return Status{accept(p,77)};};
 births={};assert(!a.PrepareTopology(b,&a,topology,births,empty,bindings).ok());
 // A retains no ground root. B combines an independently retained secondary
 // ground owner with a primary birth while an expired A particle is rebuilt.
 // The production topology boundary must keep both B domains intact.
 const auto original_b=b;
 b.slots_.push_back(slot(3,102));b.components_.push_back({3,{3,99}});b.counts_[0]=2;
 b.slots_.push_back(slot(77,700));b.counts_[1]=1;
 std::uintptr_t private_root=77;
 Sc6ReplayVfxState::RetainedSecondaryOwners retained{&private_root,
  [](const void* p,void* out,bool(*accept)(void*,std::uintptr_t)){return Status{accept(out,*static_cast<const std::uintptr_t*>(p))};}};
 const auto combined_b=b;
 const auto combined=[&]{births={};return a.PrepareTopology(b,&a,topology,births,retained,bindings).ok();};
 if(!combined()){std::puts("reconstructed primary and private secondary B owners rejected");return 3;}
 assert(births.count_==1&&births.members_[0].component_==3&&births.members_[0].reconstructed_);
 assert(b.counts_==combined_b.counts_&&b.slots_.size()==combined_b.slots_.size());
 for(unsigned i=0;i<b.slots_.size();++i)assert(b.slots_[i].bytes==combined_b.slots_[i].bytes&&b.slots_[i].actor_weak==combined_b.slots_[i].actor_weak);
 private_root=78;assert(!combined()&&births.empty());private_root=77;
 b.slots_.back()=slot(78,700);assert(!combined());b=combined_b;
 b.counts_={3,0};assert(!combined());b=combined_b;
 b.components_.push_back({77,{77,99}});assert(!combined());b=combined_b;
 a.slots_.push_back(slot(77,700));a.counts_[1]=1;assert(!combined());a.slots_.pop_back();a.counts_[1]=0;
 b.slots_.push_back(slot(78,701));++b.counts_[1];assert(!combined());b=combined_b;
 const auto once=retained.visit;
 retained.visit=[](const void* p,void* out,bool(*accept)(void*,std::uintptr_t)){
  const auto owner=*static_cast<const std::uintptr_t*>(p);return Status{accept(out,owner)&&accept(out,owner)};};
 assert(!combined());retained.visit=once;
 b=original_b;
 births={};assert(!a.PrepareTopology(b,&a,topology,births,{},{}).ok());
 // Recovery selects actual C minus original B. Dead A and fresh survivors
 // cannot be conflated with B, even when native slots have compacted.
 b.constructed_=b.slots_.size();auto c=b;c.components_.push_back({9,{9,99}});c.slots_.push_back(slot(9,100));++c.counts_[0];c.constructed_=c.slots_.size();
 births={};assert(!a.PrepareTopology(c,&a,topology,births,{},{}).ok());
 assert(b.PrepareRecoveryRetirement(c,&a,births).ok() && births.count_==1);
 assert(births.members_[0].component_==9 && births.members_[0].target_==&b && !births.members_[0].reconstructed_);
 assert(births.members_[0].ValidateLifetime().ok());
 // Trace settlement consumes primary particles only. A new secondary ground
 // actor must not become a particle or grant whole-manager recovery admission.
 auto ground_c=c;ground_c.slots_.push_back(slot(77,700));ground_c.counts_[1]=1;ground_c.constructed_++;
 births={};assert(!b.PrepareRecoveryRetirement(ground_c,&a,births).ok()&&births.empty());
 if(!b.PrepareExecutionParticles(ground_c,&a,births).ok()) {
  std::puts("trace particle inventory rejected independent secondary birth");return 4;
 }
 assert(births.count_==1&&births.members_[0].component_==9);
 auto bad_primary=ground_c;bad_primary.counts_[0]++;bad_primary.counts_[1]--;
 births={};assert(!b.PrepareExecutionParticles(bad_primary,&a,births).ok()&&births.empty());
 for(unsigned fault=0;fault<6;++fault){auto bad=c;births={};
  switch(fault){case 0:bad.components_[0].weak[1]++;break;case 1:bad.slots_.back().actor_weak[1]++;break;
   case 2:bad.counts_[0]--;bad.counts_[1]++;break;case 3:bad.slots_.push_back(slot(99,200));bad.counts_[0]++;bad.constructed_++;break;
   case 4:bad.components_.push_back(bad.components_.back());break;case 5:original.live=false;break;}
  assert(!b.PrepareRecoveryRetirement(bad,&a,births).ok() && births.empty());original.live=true;
 }
}
