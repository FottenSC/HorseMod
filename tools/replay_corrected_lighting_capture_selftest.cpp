#include <Windows.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include "horselib/deterministic/ReplayLightingBinding.hpp"
using Horse::Deterministic::ReplayLightingBinding;
#define STR(x) x
namespace RC {enum class LogLevel {Default,Warning};struct Output {template<LogLevel,typename... T> static void send(T...) {}};}
template<class T>T& Field(std::uintptr_t p,std::size_t offset=0){return *reinterpret_cast<T*>(p+offset);}
static bool ReadBytes(void* out,const void* in,std::size_t n){if(n)std::memcpy(out,in,n);return true;}
static void Bind(void* weak,const void* object){std::memcpy(weak,&object,8);}
static void* Resolve(const void* weak){void* object;std::memcpy(&object,weak,8);return object;}
static unsigned retains{};
static void** Assign(void** out,void* value){*out=value;if(value)++retains;return out;}
static void Thunk(std::uintptr_t at,void* target){
 const unsigned char code[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
 std::memcpy(reinterpret_cast<void*>(at),code,sizeof(code));std::memcpy(reinterpret_cast<void*>(at+2),&target,8);
 FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),sizeof(code));
}
struct Host {
#include "lighting_capture_types.inl"
 std::uintptr_t base_{},scene_{};
 struct {std::size_t bytes{};} witness_;
 bool lighting_dirty_{},lighting_transferred_{};
 std::array<unsigned,64> capture_quarantined_{};std::size_t capture_quarantined_count_{};
 bool LightingPrimitiveSlots(LightingPrimitive& row){row.slot_count=1;row.slots[0]=row.address+0x58;return true;}
 bool RetainedUniformBytes(void* value,std::size_t& bytes){bytes=value?64:0;return true;}
 bool CaptureLightingUnchecked(LightingImage&,std::size_t);
};
#define LIGHTING_REJECT() return false
#include "lighting_capture_body.inl"
int main(){
 const auto base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x1600000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(base);
 Thunk(base+0xf7bad0,reinterpret_cast<void*>(&Bind));Thunk(base+0xf823f0,reinterpret_cast<void*>(&Resolve));Thunk(base+0x15b83f0,reinterpret_cast<void*>(&Assign));
 std::array<std::byte,0x1000> scene{};
 std::array<std::array<std::byte,0x100>,2> infos{};
 std::array<std::array<std::byte,0x200>,2> proxies{};
 std::array<std::array<std::byte,0x800>,2> components{};
 std::array<std::uintptr_t,2> pointers{};std::array<unsigned,2> ids{11,22};
 const auto s=reinterpret_cast<std::uintptr_t>(scene.data());
 Field<std::uintptr_t>(s,0xac8)=reinterpret_cast<std::uintptr_t>(pointers.data());Field<int>(s,0xad0)=2;
 Field<std::uintptr_t>(s,0xb38)=reinterpret_cast<std::uintptr_t>(ids.data());
 for(unsigned kind=0;kind<2;++kind){const auto map=s+0xda8+(kind?0x140:0x50);Field<int>(map,0x2c)=128;Field<int>(map,0x48)=1;Field<int>(map,0x38)=-1;}
 for(unsigned i=0;i<2;++i){
  const auto info=reinterpret_cast<std::uintptr_t>(infos[i].data()),proxy=reinterpret_cast<std::uintptr_t>(proxies[i].data()),component=reinterpret_cast<std::uintptr_t>(components[i].data());pointers[i]=info;
  Field<std::uintptr_t>(info,0xd8)=s;Field<int>(info,0xe4)=i;Field<std::uintptr_t>(info,8)=proxy;Field<std::uintptr_t>(info,0xe8)=component;Field<unsigned>(info,0x10)=ids[i];
  Field<std::uintptr_t>(proxy,0x118)=info;Field<std::uintptr_t>(proxy)=base+0x3956e80;
  Field<std::uintptr_t>(component,0x790)=proxy;Field<unsigned>(component,0x420)=ids[i];
 }
 // Production publishes null allocation/uniform and clears dirty for B-only
 // destinations before corrected execution. Its proxy remains alive for undo.
 Field<void*>(pointers[0],0x58)=reinterpret_cast<void*>(0x1234);
 const auto before_scene=scene;const auto before_infos=infos;
 Host original;original.base_=base;original.scene_=s;
 assert(original.CaptureLightingUnchecked(original.lighting_a_,1024*1024));assert(original.lighting_a_.owners.size()==2);
 Host corrected;corrected.base_=base;corrected.scene_=s;corrected.capture_quarantined_[0]=22;corrected.capture_quarantined_count_=1;
 assert(corrected.CaptureLightingUnchecked(corrected.lighting_a_,1024*1024));
 assert(corrected.lighting_a_.owners.size()==1 && corrected.lighting_a_.primitives.size()==1 && corrected.lighting_a_.uniforms.size()==1);
 assert(corrected.lighting_a_.primitives[0].id==11 && scene==before_scene && infos==before_infos && retains==2);
 Host undo;undo.base_=base;undo.scene_=s;undo.capture_quarantined_[0]=22;undo.capture_quarantined_count_=1;
 assert(undo.CaptureLightingUnchecked(undo.lighting_b_,1024*1024));assert(undo.lighting_b_.owners.size()==2);
 // Quarantine is a validated logical exclusion, not permission to discard a
 // still-published allocation, uniform, dirty work or missing native binding.
 for(unsigned failure=0;failure<4;++failure){
  Host invalid;invalid.base_=base;invalid.scene_=s;invalid.capture_quarantined_[0]=failure==3?33:22;invalid.capture_quarantined_count_=1;
  if(failure==0)Field<std::uintptr_t>(pointers[1],0x50)=0x1000;
  if(failure==1)Field<void*>(pointers[1],0x58)=reinterpret_cast<void*>(0x1234);
  if(failure==2)Field<unsigned char>(pointers[1],0xf2)=1;
  assert(!invalid.CaptureLightingUnchecked(invalid.lighting_a_,1024*1024));infos=before_infos;
 }
 // An allocation keyed by the quarantined ID is still native-owned work,
 // even when the scene-info publication itself is null.
 std::array<std::byte,0x18> entry{};std::array<std::byte,0x184> allocation{};
 const auto map=s+0xda8+0x140,e=reinterpret_cast<std::uintptr_t>(entry.data()),a=reinterpret_cast<std::uintptr_t>(allocation.data());
 Field<unsigned>(e)=22;Field<std::uintptr_t>(e,8)=a;Field<int>(e,0x10)=-1;
 Field<int>(a,0x3c)=1;Field<unsigned char>(a,0x181)=1;
 Field<std::uintptr_t>(map)=e;Field<int>(map,8)=1;Field<int>(map,0xc)=1;Field<unsigned>(map,0x10)=1;Field<int>(map,0x28)=1;Field<int>(map,0x38)=0;
 Host keyed;keyed.base_=base;keyed.scene_=s;keyed.capture_quarantined_[0]=22;keyed.capture_quarantined_count_=1;
 assert(!keyed.CaptureLightingUnchecked(keyed.lighting_a_,1024*1024));
 VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
}
