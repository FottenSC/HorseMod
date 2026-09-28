#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
template<class T>T& Field(std::uintptr_t p,std::size_t n=0){return *reinterpret_cast<T*>(p+n);}
struct Owner {
 bool live=true;
 bool RetainedRenderBinding(std::uintptr_t,const std::array<int,2>&,unsigned,std::uintptr_t,bool dormant)const{return live;}
};
struct LightingPrimitive {std::uintptr_t component{},proxy_type{};unsigned slot_count{1},id{789};std::array<int,2> weak{1,1};std::array<std::uintptr_t,1> proxy_inputs{42};};
struct Fixture {
 bool lighting_executing_{},lighting_transferred_{};std::uintptr_t base_=0x140000000;
 struct {bool undo_ready=true;} witness_;
 Owner owner;Owner* trace_render_owner_=&owner;
 bool FreshTraceRenderBinding(const LightingPrimitive&)const{return false;}
#include "pending_trace_method.inl"
};
int main(){
 Fixture f;std::array<std::byte,0xb00> mesh{};LightingPrimitive row;
 row.component=reinterpret_cast<std::uintptr_t>(mesh.data());row.proxy_type=f.base_+0x39af350;
 // B-dormant, then A-visible before native reconstruction. Neither may follow
 // historical proxy pointers; the native completion path must bind real ones.
 if(!f.PendingTraceTargetBinding(row,true)){std::puts("retained dormant B target rejected before A publication");return 1;}
 Field<unsigned>(row.component,0x240)=0x10;
 if(!f.PendingTraceTargetBinding(row,true))return 2;
 for(unsigned i=0;i<6;++i){
  f.owner.live=true;f.witness_.undo_ready=true;f.lighting_executing_=false;f.lighting_transferred_=false;row.slot_count=1;
  Field<std::uintptr_t>(row.component,0x790)=0;
  if(i==0)f.owner.live=false;if(i==1)f.witness_.undo_ready=false;if(i==2)f.lighting_executing_=true;
  if(i==3)f.lighting_transferred_=true;if(i==4)row.slot_count=2;if(i==5)Field<std::uintptr_t>(row.component,0x790)=row.component;
  if(f.PendingTraceTargetBinding(row,true))return 10+i;
 }
 std::puts("pending trace admission preserves owner, phase and absent-resource guards");
}
