#include <array>
#include <vector>
#include <span>
#include <memory>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
enum class FailureCode {IllegalTransition,GenerationMismatch,UnsupportedContent,CapacityExceeded};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
unsigned GetCurrentThreadId(){return 7;}
bool CopyBytes(void* a,const void* b,std::size_t n){std::memcpy(a,b,n);return true;}
struct Sc6ReplayCpuEmitterState {
 struct ComponentReplacement {std::uintptr_t source{},target{};std::array<int,2> source_weak{},target_weak{};};
 struct Prepared {bool fresh{};static inline bool reject{};
  Status ValidateDestination(std::size_t,void* expected)const{return {!reject&&(!fresh||!expected)};}
  Status ValidateGpuDestination(std::size_t,void*)const{return {!reject&&!fresh};}
  Status ValidateFreshGpuDestination(std::size_t,void*)const{return {!reject&&fresh};}
 };
 std::size_t owned_bytes()const{return 64;}
 Status PrepareReplacement(std::size_t,Prepared& p)const{p.fresh=false;return {true};}
 Status PrepareReplacement(std::size_t,Prepared& p,const ComponentReplacement&)const{p.fresh=true;return {true};}
};
struct Lease {bool live{};Status Validate()const{return {live};}};
struct Sc6ReplayVfxState {
 struct ReconstructionBinding {Sc6ReplayCpuEmitterState::ComponentReplacement identity;};
 struct PreparedEmitterSet {
  struct Entry {std::unique_ptr<Sc6ReplayCpuEmitterState::Prepared> replacement;void* component{};std::size_t ordinal{};void* expected{};bool reconstructed{};Sc6ReplayCpuEmitterState::ComponentReplacement identity;};
  std::vector<Entry> entries_;std::vector<ReconstructionBinding> reconstruction_bindings_;const Sc6ReplayVfxState* reconstruction_source_{};bool gpu_storage_{};
  std::size_t owned_bytes()const{return sizeof(*this)+entries_.capacity()*sizeof(Entry)+reconstruction_bindings_.capacity()*sizeof(ReconstructionBinding);}
 };
 struct Header {const std::byte* data;int count,capacity;};
 struct Owner {Sc6ReplayCpuEmitterState image;std::uintptr_t component;std::size_t ordinal;};
 struct Component {std::uintptr_t address;std::vector<std::uintptr_t> emitters;};
 std::vector<Owner> gpu_owners_,cpu_emitters_;std::vector<Component> components_;bool valid_{true},mapping_valid{true};Lease* object_lease_;unsigned thread_{7};
 std::size_t owned_bytes()const{return 256;}
 Status ValidateReconstructionBindings(std::span<const ReconstructionBinding> b)const{return {mapping_valid&&b.size()==components_.size()};}
 Status PrepareCpuEmitters(std::size_t,PreparedEmitterSet&,std::span<const ReconstructionBinding>)const noexcept;
 Status PrepareGpuEmitters(std::size_t,PreparedEmitterSet&,std::span<const ReconstructionBinding>)const noexcept;
 Status PrepareEmitters(std::size_t,PreparedEmitterSet&,bool,std::span<const ReconstructionBinding>)const noexcept;
};
#include "mapped_emitter_set.inl"
int main(){
 using V=Sc6ReplayVfxState;Lease expired;V a;a.object_lease_=&expired;
 std::array<std::byte,0xa60> surviving{},fresh{};void* native_root=reinterpret_cast<void*>(123);
 V::Header h{reinterpret_cast<const std::byte*>(&native_root),1,1};std::memcpy(surviving.data()+0xa50,&h,sizeof(h));std::memcpy(fresh.data()+0xa50,&h,sizeof(h));
 const auto s=reinterpret_cast<std::uintptr_t>(surviving.data()),c=reinterpret_cast<std::uintptr_t>(fresh.data());
 a.components_={{1,{123}},{s,{123}}};a.gpu_owners_={{{},1,0}};a.cpu_emitters_={{{},s,0}};
 std::array<V::ReconstructionBinding,2> bindings{{{{1,c,{1,7},{9,7}}},{{s,s,{2,7},{2,7}}}}};
 V::PreparedEmitterSet cpu;
 if(!a.PrepareCpuEmitters(65536,cpu,bindings).ok()){std::puts("surviving CPU emitter incorrectly depends on expired GPU component lease");return 1;}
 assert(cpu.entries_.size()==1&&!cpu.entries_[0].reconstructed&&cpu.entries_[0].component==surviving.data()&&cpu.reconstruction_source_==&a);
 V::PreparedEmitterSet gpu;assert(a.PrepareGpuEmitters(65536,gpu,bindings).ok());
 assert(gpu.entries_.size()==1&&gpu.entries_[0].reconstructed&&gpu.entries_[0].component==fresh.data()&&gpu.entries_[0].expected==native_root);
 assert(a.gpu_owners_[0].component==1&&a.cpu_emitters_[0].component==s);
 V::PreparedEmitterSet rejected;assert(!a.PrepareCpuEmitters(65536,rejected,{}).ok());
 a.mapping_valid=false;assert(!a.PrepareCpuEmitters(65536,rejected,bindings).ok());a.mapping_valid=true;
 a.cpu_emitters_[0].component=1;assert(!a.PrepareCpuEmitters(65536,rejected,bindings).ok());a.cpu_emitters_[0].component=s;
 Sc6ReplayCpuEmitterState::Prepared::reject=true;assert(!a.PrepareCpuEmitters(65536,rejected,bindings).ok());Sc6ReplayCpuEmitterState::Prepared::reject=false;
 assert(!a.PrepareCpuEmitters(1,rejected,bindings).ok()&&rejected.entries_.empty());
 a.components_[0].emitters.push_back(456);assert(!a.PrepareGpuEmitters(65536,rejected,bindings).ok());
 std::array<void*,2> fresh_slots{native_root,nullptr};V::Header mixed_header{reinterpret_cast<const std::byte*>(fresh_slots.data()),2,2};
 std::memcpy(fresh.data()+0xa50,&mixed_header,sizeof(mixed_header));
 a.cpu_emitters_.push_back({{},1,1});
 V::PreparedEmitterSet mixed_cpu,mixed_gpu;
 assert(a.PrepareCpuEmitters(65536,mixed_cpu,bindings).ok());
 assert(mixed_cpu.entries_.size()==2&&mixed_cpu.entries_[1].reconstructed&&mixed_cpu.entries_[1].component==fresh.data()&&!mixed_cpu.entries_[1].expected);
 assert(a.PrepareGpuEmitters(65536,mixed_gpu,bindings).ok());
 assert(fresh_slots[0]==native_root&&!fresh_slots[1]); // Preparation never publishes.

}
