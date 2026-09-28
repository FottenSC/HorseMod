#include <array>
#include <vector>
#include <span>
#include <memory>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cassert>
enum class FailureCode {IllegalTransition,GenerationMismatch,UnsupportedContent,CapacityExceeded};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
template<class T>T At(const void* p,std::size_t n){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+n,sizeof(v));return v;}
static bool old_live{};
bool Live(std::uintptr_t,const std::array<int,2>&,std::uintptr_t){return old_live;}
struct Lease {bool live{true};Status Validate()const{return {live};}};
struct Sc6ReplayVfxState {
 struct ReconstructionBinding {struct Identity {std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};} identity;const Lease* lease{};};
 struct ReconstructionRequest {std::uintptr_t source{};std::array<int,2> weak{};std::size_t emitter_count{},gpu_ordinal{};std::array<std::size_t,128> gpu_ordinals{};std::size_t gpu_count{};};
 struct ReconstructionFailure {std::uintptr_t source{};std::size_t roots{},gpu{},managed{},slots{};bool lux{},source_live{};};
 struct ReconstructionFactory {void* context{};Status(*construct)(void*,const ReconstructionRequest&,ReconstructionBinding&){};};
 struct ParticleBirthSet {static constexpr std::size_t capacity=32;};
 struct Component {std::uintptr_t address;std::array<int,2> weak;std::vector<std::uintptr_t> emitters;bool lux{true};};
 struct Gpu {std::uintptr_t component;std::size_t ordinal;};
 struct Slot {std::array<std::byte,0xc0> bytes{};};
 std::vector<Component> components_;std::vector<Gpu> gpu_owners_,cpu_emitters_;std::vector<Slot> slots_;std::array<int,2> counts_{};
 std::unique_ptr<Lease> object_lease_=std::make_unique<Lease>();bool valid_{true},dependencies{true},mapped{true};std::uintptr_t base_{10},manager_{20};std::array<int,2> manager_weak_{3,4};unsigned thread_{7};
 Status ValidateReconstructionDependencies()const{return {dependencies};}
 Status ValidateReconstructionBindings(std::span<const ReconstructionBinding> rows)const {
  if(!mapped || rows.size()!=components_.size())return {false};
  for(auto& row:rows)if(!row.lease || !row.lease->live)return {false};return {true};
 }
 Status PrepareReconstructionBindings(const Sc6ReplayVfxState&,std::size_t,std::vector<ReconstructionBinding>&,ReconstructionFactory,ReconstructionFailure* failure=nullptr)const noexcept;
};
#include "reconstruction_inventory.inl"
Status Shape(std::size_t live,std::size_t gpu_count,std::size_t cpu_count) {
#include "emitter_shape.inl"
return Status::success();}
int main(){
 assert(Shape(3,0,3).ok()&&Shape(3,1,2).ok());
 if(!Shape(3,2,1).ok())return 2;
 assert(!Shape(0,0,0).ok()&&!Shape(3,0,2).ok());
 using V=Sc6ReplayVfxState;V a,b;a.object_lease_->live=false;
 a.components_={{1,{1,99},{0,123}},{2,{2,99},{456}}};b.components_={{2,{2,99},{456}},{3,{3,99},{789}}};
 a.gpu_owners_={{1,1}};a.counts_={1,0};a.slots_.resize(1);std::uintptr_t owner=1;std::memcpy(a.slots_[0].bytes.data(),&owner,8);
 struct Factory {unsigned calls{},expected_gpu_count=1;bool fail{},bad_source{},bad_target{},cpu_only{};Lease lease;};Factory state;
 V::ReconstructionFactory factory{&state,[](void* context,const V::ReconstructionRequest& request,V::ReconstructionBinding& binding){
  auto& f=*static_cast<Factory*>(context);++f.calls;
  assert(request.source==1&&request.emitter_count==2&&request.gpu_ordinal==(f.cpu_only?2u:1u));
  assert(request.gpu_count==f.expected_gpu_count);
  if(request.gpu_count==2)assert(request.gpu_ordinals[0]==0&&request.gpu_ordinals[1]==1);
  binding={{f.bad_source?7u:request.source,f.bad_target?3u:9u,request.weak,{9,99}},&f.lease};
  return Status{!f.fail};
 }};
 V::ReconstructionFailure failure{};
 std::vector<V::ReconstructionBinding> rows;
 auto prepare=[&]{rows.clear();state.calls=0;return a.PrepareReconstructionBindings(b,65536,rows,factory,&failure).ok();};
 assert(prepare()&&state.calls==1&&rows.size()==2);
 assert(rows[0].identity.target==9&&rows[0].lease==&state.lease&&rows[1].identity.target==2&&rows[1].lease==b.object_lease_.get());
 assert(b.components_.size()==2&&b.components_[0].address==2&&b.components_[1].address==3);
 old_live=true;assert(!prepare()&&!state.calls);assert(failure.source==1&&failure.source_live&&failure.roots==1&&failure.gpu==1&&failure.managed==1);old_live=false;
 a.dependencies=false;assert(!prepare()&&!state.calls);a.dependencies=true;
 b.object_lease_->live=false;assert(!prepare()&&!state.calls);b.object_lease_->live=true;
 b.components_[0].weak[1]=98;assert(!prepare()&&!state.calls);b.components_[0].weak[1]=99;
 a.gpu_owners_[0].ordinal=2;assert(!prepare()&&!state.calls);a.gpu_owners_[0].ordinal=1;
 a.components_[0].emitters[0]=456;assert(!prepare()&&!state.calls);assert(failure.source==1&&failure.roots==2&&failure.gpu==1&&failure.slots==2&&!failure.source_live);a.components_[0].emitters[0]=0;
 a.components_[0].emitters[0]=456;a.cpu_emitters_={{1,0}};
 assert(prepare()&&state.calls==1); // Mixed captured roots are typed and complete.
 a.cpu_emitters_.push_back({1,0});assert(!prepare()&&!state.calls);a.cpu_emitters_.pop_back();
 a.cpu_emitters_[0].ordinal=1;assert(!prepare()&&!state.calls);a.cpu_emitters_.clear();a.components_[0].emitters[0]=0;
 state.fail=true;assert(!prepare()&&state.calls==1&&rows.empty());state.fail=false;
 state.bad_source=true;assert(!prepare());state.bad_source=false;state.bad_target=true;assert(!prepare());state.bad_target=false;
 state.lease.live=false;assert(!prepare());state.lease.live=true;
 a.mapped=false;assert(!prepare());a.mapped=true;
 rows.clear();state.calls=0;assert(!a.PrepareReconstructionBindings(b,1,rows,factory).ok()&&!state.calls);
 state.expected_gpu_count=2;a.components_[0].emitters[0]=456;a.gpu_owners_.insert(a.gpu_owners_.begin(),{1,0});
 assert(prepare()&&state.calls==1); // Both GPU ordinals retain complete typed images.
 a.gpu_owners_.erase(a.gpu_owners_.begin());a.components_[0].emitters[0]=0;
 a.gpu_owners_.clear();a.cpu_emitters_={{1,1}};state.cpu_only=true;state.expected_gpu_count=0;assert(prepare()&&state.calls==1);
 assert(prepare());assert(!a.PrepareReconstructionBindings(b,65536,rows,factory).ok());
}
