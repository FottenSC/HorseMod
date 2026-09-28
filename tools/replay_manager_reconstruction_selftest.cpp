#include <Windows.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
enum class FailureCode {IllegalTransition,GenerationMismatch,RestoreVerificationFailed,RestorePreflightFailed,ContextUnavailable};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
enum class RestoreSettlement {RecoverOriginal,CommitCurrent};
template<class T>T At(const void* p,std::size_t n){T v;std::memcpy(&v,static_cast<const std::byte*>(p)+n,sizeof(v));return v;}
struct Sc6ReplayVfxState {
 struct ComponentBinding {
  std::uintptr_t address{},tick_entry{},attach_parent{},particle_template{};std::array<int,2> weak{};
  std::uint64_t attach_socket{};bool lux{};unsigned active_flags{};
  std::array<unsigned,2> material_roots{};std::array<unsigned,2> completions{};std::array<std::byte,384> values{};
  std::array<std::uintptr_t,3> events{};
 };
 struct Span{std::size_t offset,bytes;bool lux;};
 static constexpr std::array component_spans{Span{0x200,4,false}};
 static constexpr std::array<unsigned,3> table_offsets{0x388,0x458,0x468};
 std::vector<ComponentBinding> components_;std::uintptr_t base_{},manager_{};int sequence_{};
 static inline std::vector<ComponentBinding> live;
 static Status CheckComponentBoundary(std::uintptr_t,const ComponentBinding& c){
  for(const auto& x:live)if(x.address==c.address && x.weak==c.weak && x.material_roots==c.material_roots && x.completions==c.completions && x.events==c.events)return Status::success();
  return Status::failure(FailureCode::GenerationMismatch);
 }
 struct PreparedManager {
  struct Array{void* data{};int count{},capacity{};};
  const Sc6ReplayVfxState *target_{},*current_{};std::vector<ComponentBinding> projected_components_,private_original_components_;
  bool reconstructed_{},executing_{},execution_settled_{};RestoreSettlement execution_settlement_{};
  std::array<Array,5> arrays_{},previous_{};std::array<std::byte,0x50> map_{},previous_map_{};
  Status ValidateComponents(bool,bool)const noexcept;bool Write(bool)noexcept;
 };
};
#include "manager_reconstruction.inl"
int main(){
 using V=Sc6ReplayVfxState;
 alignas(16) std::array<std::byte,0x500> manager{},bobject{},cobject{};
 V a,b;a.manager_=b.manager_=reinterpret_cast<std::uintptr_t>(manager.data());
 V::ComponentBinding source{};source.address=1;source.weak={1,2};source.tick_entry=7;source.particle_template=8;source.lux=true;source.material_roots={10,11};source.completions={12,13};source.active_flags=0x40000;
 unsigned avalue=17,bvalue=29;std::memcpy(source.values.data(),&avalue,4);a.components_.push_back(source);
 auto original=source;original.address=reinterpret_cast<std::uintptr_t>(bobject.data());original.weak={3,4};original.material_roots={20,21};original.active_flags=0x20000;std::memcpy(original.values.data(),&bvalue,4);b.components_.push_back(original);
 auto fresh=source;fresh.address=reinterpret_cast<std::uintptr_t>(cobject.data());fresh.weak={5,6};fresh.material_roots={30,31};
 V::live={original,fresh};V::PreparedManager p;p.target_=&a;p.current_=&b;p.reconstructed_=true;p.projected_components_={fresh};
 if(!p.ValidateComponents(true,false).ok()){std::puts("fresh projected owner rejected because original B has a different identity");return 1;}
 if(!p.Write(true)||At<unsigned>(cobject.data(),0x200)!=avalue||At<unsigned>(bobject.data(),0x200)!=0)return 2;
 if(!p.ValidateComponents(true,true).ok())return 3;
 // The private native owner existed before A publication. Its real inactive
 // values must return on an unexecuted undo, independently of complete B.
 auto cold=fresh;cold.active_flags=0;unsigned cold_value=53;std::memcpy(cold.values.data(),&cold_value,4);
 p.private_original_components_={cold};
 if(!p.Write(false)||At<unsigned>(cobject.data(),0x200)!=cold_value
    || (At<unsigned>(cobject.data(),0x188)&0x60000)) {
  std::puts("unexecuted manager undo left private A owner active");return 13;
 }
 if(!p.ValidateComponents(false,true).ok())return 14;
 unsigned wrong=0x40000;std::memcpy(cobject.data()+0x188,&wrong,4);
 if(p.ValidateComponents(false,true).ok())return 15;
 if(!p.Write(false))return 16;
 // Executed C lifetime may end; its cold pointer is no longer an undo target.
 p.executing_=true;p.private_original_components_[0].address=1;
 // Native C destruction is represented only by loss of liveness and inaccessible
 // C memory. Undo must select complete B, never dereference the historical A or C.
 V::live={original};p.projected_components_[0].address=1;
 if(!p.Write(false)||At<unsigned>(bobject.data(),0x200)!=bvalue||!p.ValidateComponents(false,true).ok())return 4;
 if(p.ValidateComponents(true,false).ok())return 5;
 // Settlement changes the target to actual C. Do not keep publishing the
 // pre-execution projection or remap a newly reused historical address.
 V observed;observed.manager_=a.manager_;observed.components_={fresh};observed.components_[0].values[0]=std::byte{41};
 V::live={original,fresh};p.target_=&observed;p.executing_=p.execution_settled_=true;p.execution_settlement_=RestoreSettlement::CommitCurrent;
 if(!p.ValidateComponents(true,false).ok()){std::puts("settled manager still reads the expired pre-execution projection");return 7;}
 if(!p.Write(true)||At<unsigned>(cobject.data(),0x200)!=41)return 8;
 if(!p.Write(false)||At<unsigned>(bobject.data(),0x200)!=bvalue)return 9;
 p.target_=&a;
 // The original strict path still rejects a fresh identity without a projection.
 p.reconstructed_=false;if(p.ValidateComponents(true,false).ok())return 6;
 V stable_a,stable_b;stable_a.components_={original};stable_b.components_={original};
 V::PreparedManager stable;stable.target_=&stable_a;stable.current_=&stable_b;V::live={original};
 if(!stable.ValidateComponents(true,false).ok())return 10;
 for(std::size_t i=0;i<3;++i) {
  stable_b.components_[0].events[i]=1;V::live={stable_b.components_[0]};
  if(stable.ValidateComponents(true,false).ok())return 11;
  stable_b.components_[0].events[i]=0;
 }
 V::live={original};if(!stable.ValidateComponents(true,false).ok())return 12;
 std::puts("actual manager value publication uses fresh C and undo uses original B after C lifetime ends");
}
