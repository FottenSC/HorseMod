
#include <Windows.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "deterministic/ReplayStaticVectorField.hpp"
using namespace Horse::Deterministic;
template<class T>T& Field(std::uintptr_t p,std::size_t offset=0){return *reinterpret_cast<T*>(p+offset);}
struct Sc6ReplayVfxState {struct CoordinateRebuild{std::uintptr_t render{},descriptor{},vector_field_asset{};bool fresh{};std::uintptr_t source_render{};};};
struct Sc6ReplayParticleCopy {
 using LocalFields=ReplayStaticVectorFieldSet;LocalFields local_fields_a_,local_fields_b_;
 std::uintptr_t base_{},system_{};std::vector<Sc6ReplayVfxState::CoordinateRebuild> coordinates_;
 struct {bool native_write_uncommitted=true,undo_ready=true;}witness_;
 struct {bool done=true;bool retired()const{return done;}}completion_;
 bool ProjectFields() {
#include "fresh_static_projection.inl"
 return true;
 }
 bool CaptureLocalFields(LocalFields&)noexcept;
 bool LocalFieldOwnersBound()const noexcept;
 bool PublishLocalFields(const LocalFields&)noexcept;
};
#include "fresh_static_field.inl"
int main(){
 auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4400000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
 auto base=reinterpret_cast<std::uintptr_t>(memory);
 // Exact independently inspected native static-asset binding leaf. It borrows
 // asset+58 and clears ownership; it neither allocates nor touches RNG.
 const unsigned char leaf[]{0x48,0x8b,0x41,0x58,0x48,0x89,0x02,0xc6,0x82,0,1,0,0,0,0xc3};
 std::memcpy(memory+0x21a31f0,leaf,sizeof(leaf));Field<std::uintptr_t>(base+0x39e5068,0x230)=base+0x21a31f0;
 DWORD protection{};assert(VirtualProtect(memory+0x21a3000,4096,PAGE_EXECUTE_READ,&protection));
 FlushInstructionCache(GetCurrentProcess(),memory+0x21a31f0,sizeof(leaf));
 std::array<std::byte,0x260> old_render{},fresh{},b_render{};
 std::array<std::byte,0x80> asset{},resource{},descriptor{};
 const auto ptr=[](auto& a){return reinterpret_cast<std::uintptr_t>(a.data());};
 auto old=ptr(old_render),f=ptr(fresh),b=ptr(b_render),a=ptr(asset),res=ptr(resource),d=ptr(descriptor);
 Field<std::uintptr_t>(res)=base+0x39e5a90;Field<std::uintptr_t>(res,0x30)=777;
 for(auto o:{0x38,0x3c,0x40})Field<int>(res,o)=50;
 Field<std::uintptr_t>(a)=base+0x39e5068;Field<std::uintptr_t>(a,0x58)=res;Field<std::uintptr_t>(d,0x30)=a;
 for(auto p:{old,f,b}){Field<std::uintptr_t>(p)=base+0x394bfc0;Field<unsigned char>(p,0x253)=1;}
 Field<std::uintptr_t>(old,0xe0)=Field<std::uintptr_t>(b,0xe0)=res;
 Field<int>(f,0x240)=-1;
 Field<unsigned>(old,0x60)=19;Field<unsigned>(old,0x110)=23;Field<unsigned>(old,0x1d4)=29;Field<unsigned char>(old,0x1dc)=3;
 Field<unsigned>(b,0x60)=91;
 Sc6ReplayParticleCopy p;p.base_=base;p.local_fields_a_.captured=p.local_fields_b_.captured=true;
 p.local_fields_a_.count=p.local_fields_b_.count=1;
 assert(p.local_fields_a_.rows[0].Capture(base,old)&&p.local_fields_b_.rows[0].Capture(base,b));
 // Original source is no longer available; execute the actual typed projection.
 Field<unsigned char>(f,0x1dc)=0xf0;
 reinterpret_cast<void(*)(std::uintptr_t,std::uintptr_t)>(base+0x21a31f0)(a,f+0xe0);
 old_render.fill(std::byte{0xff});
 p.coordinates_.push_back({f,d,a,true,old});assert(p.ProjectFields());const auto saved_b=b_render;const auto saved_resource=resource;const auto saved_asset=asset;
 if(!p.LocalFieldOwnersBound()){std::puts("fresh static field incorrectly requires old A render and a matching B owner");return 1;}
 const auto empty=fresh;
 p.completion_.done=false;assert(!p.PublishLocalFields(p.local_fields_a_)&&fresh==empty);p.completion_.done=true;
 p.witness_.native_write_uncommitted=false;assert(!p.PublishLocalFields(p.local_fields_a_)&&fresh==empty);p.witness_.native_write_uncommitted=true;
 for(unsigned fault=0;fault<4;++fault){
  auto saved=fresh;auto field_asset=asset;
  if(fault==0)Field<std::uintptr_t>(a,0x58)=res+8;
  if(fault==1)Field<unsigned char>(f,0x1e0)=1;
  if(fault==2)Field<std::uintptr_t>(f,0xe0)=res+8;
  if(fault==3)Field<unsigned char>(f,0x253)=0;
  assert(!p.LocalFieldOwnersBound());fresh=saved;asset=field_asset;
 }
 assert(p.PublishLocalFields(p.local_fields_a_)&&p.local_fields_a_.rows[0].Matches(base));
 assert(Field<std::uintptr_t>(f,0xe0)==res&&!Field<unsigned char>(f,0x1e0));
 assert(Field<unsigned char>(f,0x1dc)==0xf3); // Preserve fresh upper bits; restore only native low flags.
 assert(p.PublishLocalFields(p.local_fields_a_)); // Idempotent after native pointer binding.
 assert(b_render==saved_b&&resource==saved_resource&&asset==saved_asset);
 Field<unsigned>(b,0x60)=123;assert(p.PublishLocalFields(p.local_fields_b_)&&b_render==saved_b);
 // Zero-tile emitters without a vector field retain native scalar parameters.
 // They are not consumed/cleared by 141FA5DB0 until tiles become active.
 for(auto p:{old,f,b}){Field<std::uintptr_t>(p)=base+0x394bfc0;Field<std::uintptr_t>(p,0xe0)=0;Field<unsigned char>(p,0x1e0)=0;Field<int>(p,0xc8)=Field<int>(p,0xd8)=0;Field<unsigned char>(p,0x253)=1;Field<int>(p,0x244)=0;}
 Field<std::uintptr_t>(d,0x30)=0;Field<unsigned char>(old,0x1dc)=0;
 Field<unsigned>(old,0x88)=0x3c888889;Field<unsigned>(old,0x90)=0x3d088889;Field<unsigned>(old,0x98)=0x3c888889;
 Field<unsigned>(b,0x88)=7;Field<unsigned>(b,0x90)=11;Field<unsigned>(b,0x98)=13;
 Sc6ReplayParticleCopy no_field;no_field.base_=base;no_field.local_fields_a_.captured=no_field.local_fields_b_.captured=true;
 no_field.local_fields_a_.count=no_field.local_fields_b_.count=1;
 if(!no_field.local_fields_a_.rows[0].Capture(base,old)){std::puts("zero-tile native render parameters omitted without vector field");return 2;}
 assert(no_field.local_fields_b_.rows[0].Capture(base,b));
 no_field.coordinates_.push_back({f,d,0,true,old});assert(no_field.ProjectFields());
 const auto no_field_b=b_render;const auto no_field_empty=fresh;
 no_field.completion_.done=false;assert(!no_field.PublishLocalFields(no_field.local_fields_a_)&&fresh==no_field_empty);no_field.completion_.done=true;
 no_field.witness_.undo_ready=false;assert(!no_field.PublishLocalFields(no_field.local_fields_a_)&&fresh==no_field_empty);no_field.witness_.undo_ready=true;
 assert(no_field.PublishLocalFields(no_field.local_fields_a_)&&no_field.local_fields_a_.rows[0].Matches(base)&&b_render==no_field_b);
 assert(Field<unsigned>(f,0x88)==0x3c888889&&Field<unsigned>(f,0x90)==0x3d088889&&Field<unsigned>(f,0x98)==0x3c888889);
 Field<std::uintptr_t>(f,0xe0)=res;assert(!no_field.LocalFieldOwnersBound());Field<std::uintptr_t>(f,0xe0)=0;
 Field<int>(f,0xc8)=1;assert(!no_field.LocalFieldOwnersBound());Field<int>(f,0xc8)=0;
 Field<unsigned>(b,0x88)=99;assert(no_field.PublishLocalFields(no_field.local_fields_b_)&&b_render==no_field_b);
 std::array<std::byte,0xa0> system{};std::array<std::uintptr_t,2> slots{b,0};
 no_field.system_=ptr(system);const auto registry=no_field.system_+0x40;
 Field<std::uintptr_t>(registry)=reinterpret_cast<std::uintptr_t>(slots.data());
 Field<int>(registry,8)=Field<int>(registry,0xc)=Field<int>(registry,0x28)=2;
 Field<unsigned>(registry,0x10)=1;Field<int>(b,0x240)=0;Field<unsigned char>(b,0x252)=0;
 Sc6ReplayParticleCopy::LocalFields captured;
 assert(no_field.CaptureLocalFields(captured)&&captured.count==1&&captured.rows[0].Matches(base));
 Field<int>(b,0xd8)=1;Sc6ReplayParticleCopy::LocalFields pending;
 assert(!no_field.CaptureLocalFields(pending));Field<int>(b,0xd8)=0;
 VirtualFree(memory,0,MEM_RELEASE);
}
