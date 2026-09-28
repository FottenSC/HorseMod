#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>
enum class FailureCode {IllegalTransition,GenerationMismatch};
struct Status {bool good;bool ok()const{return good;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
template<class T>T At(const void* p,std::size_t o){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+o,sizeof(v));return v;}
struct Sc6ReplayVfxState {
 struct Component {std::uintptr_t address;bool lux;std::vector<std::uintptr_t> emitters;};
 struct Gpu {std::uintptr_t component;};
 struct Slot {std::array<std::byte,0xc0> bytes;};
 std::vector<Component> components_;std::vector<Gpu> gpu_owners_;std::vector<Slot> slots_;
 std::array<int,2> counts_{};std::size_t constructed_{};
 mutable unsigned validations{};unsigned fail_validation{};
 Status ValidateRetainedOwners()const{return {++validations!=fail_validation};}
 Status VisitSingleGpuComponents(void*,Status(*)(void*,std::uintptr_t))const noexcept;
};
#include "particle_configuration_selection.inl"
struct Visit {std::vector<std::uintptr_t> seen;std::uintptr_t reject{};};
Status collect(void* p,std::uintptr_t c){auto& v=*static_cast<Visit*>(p);v.seen.push_back(c);return {c!=v.reject};}
int main(){
 Sc6ReplayVfxState v;
 // All are retained world owners. Only primary-manager single-GPU Lux owners
 // have the bounded configuration constructor contract. An unrelated outer
 // must not be offered to that constructor merely because it owns one GPU.
 v.components_={{10,true,{0,11}},{20,true,{21}},{30,true,{31}},{40,true,{41,42}},{50,false,{51}},{60,true,{61}}};
 v.gpu_owners_={{10},{20},{30},{40},{40},{50}}; //40 has two GPU roots;60 is CPU-only.
 v.slots_.resize(5);v.counts_={4,1};v.constructed_=5;
 const std::array<std::uintptr_t,5> actors{10,40,50,60,30};
 for(unsigned i=0;i<actors.size();++i)std::memcpy(v.slots_[i].bytes.data(),&actors[i],8);
 Visit result;
 if(!v.VisitSingleGpuComponents(&result,collect).ok() || result.seen!=std::vector<std::uintptr_t>{10,40,60} || v.validations!=2)return 1;
 result={};result.reject=10;v.validations=0;
 if(v.VisitSingleGpuComponents(&result,collect).ok() || result.seen!=std::vector<std::uintptr_t>{10})return 2;
 result={};v.validations=0;v.fail_validation=1;
 if(v.VisitSingleGpuComponents(&result,collect).ok() || !result.seen.empty())return 3;
 v.validations=0;v.fail_validation=2;
 if(v.VisitSingleGpuComponents(&result,collect).ok())return 4;
 v.validations=0;v.fail_validation=0;v.counts_[0]=6;result={};
 if(v.VisitSingleGpuComponents(&result,collect).ok() || !result.seen.empty())return 5;
}
