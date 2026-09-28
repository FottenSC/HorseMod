#include <Windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cassert>
enum class FailureCode{IllegalTransition,GenerationMismatch,UnsupportedContent,CapacityExceeded};
struct Status{bool good;bool ok()const{return good;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
struct Sc6ReplayCpuEmitterState {
 struct ComponentReplacement{std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};};
 struct FreshGpuOwner{std::uintptr_t descriptor{};bool constructed{},retired{};};
};
bool Bind(std::uintptr_t,std::array<int,2>& weak,std::uintptr_t target){weak={int(target),77};return target!=0;}
struct Sc6ReplayVfxState {
 struct PreparedEmitterSet {
  struct Receipt{bool dead=true,retired=true;bool FreshComponentDestructionWitness(std::uintptr_t,const std::array<int,2>&)const{return dead;}bool FreshComponentRetirementWitness(std::uintptr_t,const std::array<int,2>&)const{return dead&&retired;}};
  struct Entry{bool reconstructed=true;Sc6ReplayCpuEmitterState::ComponentReplacement identity;Receipt* replacement;};
  std::vector<Entry> entries_;
  std::size_t FreshComponentRoots(std::uintptr_t target)const{std::size_t n{};for(const auto& e:entries_)n+=e.reconstructed&&e.identity.target==target;return n;}
  bool FreshComponentDeath(std::uintptr_t,const std::array<int,2>&)const noexcept;
  bool FreshComponentRetired(std::uintptr_t,const std::array<int,2>&)const noexcept;
 };
 struct ComponentBinding{std::uintptr_t address;std::array<int,2> weak;};
 struct Image{std::uintptr_t descriptor;
  Status ConstructFreshGpuOwner(const Sc6ReplayCpuEmitterState::ComponentReplacement&,std::size_t budget,Sc6ReplayCpuEmitterState::FreshGpuOwner& out)const{
   if(out.constructed||budget<100)return Status::failure(FailureCode::CapacityExceeded);out={descriptor,true,false};return Status::success();
  }
  Status QueueFreshGpuRetirement(const Sc6ReplayCpuEmitterState::ComponentReplacement&,Sc6ReplayCpuEmitterState::FreshGpuOwner& out)const{
   if(!out.constructed||out.retired||out.descriptor!=descriptor)return Status::failure(FailureCode::GenerationMismatch);out.retired=true;return Status::success();
  }
 };
 struct GpuOwner{std::uintptr_t component;std::size_t ordinal;Image image;};struct Cpu{std::uintptr_t component;};
 bool valid_=true;unsigned thread_=GetCurrentThreadId();std::uintptr_t base_=0x140000000;
 std::vector<ComponentBinding> components_;std::vector<GpuOwner> gpu_owners_;std::vector<Cpu> cpu_emitters_;
 Status ConstructFreshParticleOwner(std::uintptr_t,std::uintptr_t,std::size_t,Sc6ReplayCpuEmitterState::ComponentReplacement&,Sc6ReplayCpuEmitterState::FreshGpuOwner&,std::size_t=SIZE_MAX)const noexcept;
 Status QueueFreshGpuRetirement(const Sc6ReplayCpuEmitterState::ComponentReplacement&,Sc6ReplayCpuEmitterState::FreshGpuOwner&,std::size_t=SIZE_MAX)const noexcept;
};
#include "multi_gpu_factory.inl"
#include "multi_gpu_settlement.inl"
Status MissingComponent(const Sc6ReplayVfxState::PreparedEmitterSet* reconstructed,const Sc6ReplayVfxState::PreparedEmitterSet* reconstructed_cpu,
 std::uintptr_t component,const std::array<int,2>& weak){
 struct{std::uintptr_t address;std::array<int,2> weak;}old{component,weak};
#include "cpu_component_manager.inl"
 return Status::success();
}
int main(){
 Sc6ReplayVfxState image;image.components_={{1,{1,77}}};image.gpu_owners_={{1,2,{200}},{1,5,{500}}};
 Sc6ReplayCpuEmitterState::ComponentReplacement binding;Sc6ReplayCpuEmitterState::FreshGpuOwner first,second;
 assert(!image.ConstructFreshParticleOwner(1,9,200,binding,first).ok()&&!first.constructed);
 assert(image.ConstructFreshParticleOwner(1,9,200,binding,first,2).ok()&&first.descriptor==200);
 assert(binding.source==1&&binding.target==9&&binding.source_weak==image.components_[0].weak);
 const auto retained=first;
 assert(!image.ConstructFreshParticleOwner(1,9,99,binding,second,5).ok()&&!second.constructed&&first.descriptor==retained.descriptor);
 assert(image.ConstructFreshParticleOwner(1,9,200,binding,second,5).ok()&&second.descriptor==500);
 assert(!image.QueueFreshGpuRetirement(binding,first,5).ok()&&!first.retired);
 assert(image.QueueFreshGpuRetirement(binding,first,2).ok()&&first.retired&&!second.retired);
 assert(image.QueueFreshGpuRetirement(binding,second,5).ok()&&second.retired);
 assert(!image.QueueFreshGpuRetirement(binding,second,5).ok());
 Sc6ReplayCpuEmitterState::FreshGpuOwner absent;assert(!image.ConstructFreshParticleOwner(1,9,200,binding,absent,4).ok());
 image.gpu_owners_.push_back(image.gpu_owners_[0]);assert(!image.ConstructFreshParticleOwner(1,9,200,binding,absent,2).ok());
 using Set=Sc6ReplayVfxState::PreparedEmitterSet;Set::Receipt one,two;Set set;
 set.entries_={{true,binding,&one},{true,binding,&two}};
 if(!set.FreshComponentDeath(9,binding.target_weak)||!set.FreshComponentRetired(9,binding.target_weak))return 2;
 two.retired=false;assert(set.FreshComponentDeath(9,binding.target_weak)&&!set.FreshComponentRetired(9,binding.target_weak));
 two.dead=false;assert(!set.FreshComponentDeath(9,binding.target_weak));two={};
 set.entries_[1].identity.target_weak[1]++;assert(!set.FreshComponentDeath(9,binding.target_weak));
 set.entries_.clear();assert(!set.FreshComponentDeath(9,binding.target_weak)&&!set.FreshComponentRetired(9,binding.target_weak));
 Set cpu;cpu.entries_={{true,binding,&one},{true,binding,&two}};
 assert(MissingComponent(&set,&cpu,9,binding.target_weak).ok());
 assert(!MissingComponent(&set,nullptr,9,binding.target_weak).ok());
 two.dead=false;assert(!MissingComponent(&set,&cpu,9,binding.target_weak).ok());two={};
 set.entries_={{true,binding,&one}};one.dead=false;assert(!MissingComponent(&set,&cpu,9,binding.target_weak).ok());one={};
 assert(MissingComponent(&set,&cpu,9,binding.target_weak).ok());
}
