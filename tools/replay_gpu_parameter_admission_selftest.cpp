#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include "deterministic/ReplayStaticVectorField.hpp"
#define STR(x) x
namespace RC {enum class LogLevel{Warning};struct Output{template<LogLevel,class...T>static void send(T&&...){}};}
template<class T>T& Field(std::uintptr_t p,std::size_t o=0){return *reinterpret_cast<T*>(p+o);}
using namespace Horse::Deterministic;
struct Sc6ReplayParticleCopy {
 std::uintptr_t base_=0x140000000,system_{},pool_{};
 struct {std::uintptr_t reconstruction_owner{};unsigned reconstruction_issue{},scene_field_slots{},scene_field_checks{},phase{};bool scene_fields_empty{};}witness_;
 bool birth_fields_retained_{},registry_matches{};std::size_t birth_count_{};
 struct Birth{std::uintptr_t render;};std::array<Birth,1> births_{};
 ReplayStaticVectorFieldSet local_fields_a_;
 bool BirthRegistryMatchesB()const{return registry_matches;}
 bool AdmitRenderReconstruction(bool)noexcept;
};
#include "gpu_parameter_admission.inl"
bool Deferred(unsigned issue,bool birth){
 struct{bool particle_birth;}witness{birth};struct{unsigned reconstruction_issue;}reconstruction{issue};
#include "gpu_parameter_deferred.inl"
 return pending_birth_fields;
}
int main(){
 std::array<std::byte,0x100> system{};std::array<std::byte,0x200> pool{};std::array<std::byte,0x260> render{};
 const auto ptr=[](auto& a){return reinterpret_cast<std::uintptr_t>(a.data());};
 Sc6ReplayParticleCopy p;p.system_=ptr(system);p.pool_=ptr(pool);const auto r=ptr(render),registry=p.system_+0x40;
 Field<int>(p.system_,0x38)=-1;Field<unsigned char>(p.pool_,0x50)=Field<unsigned char>(p.pool_,0xa8)=1;
 std::array<std::uintptr_t,1> slots{r};Field<std::uintptr_t>(registry)=reinterpret_cast<std::uintptr_t>(slots.data());
 Field<int>(registry,8)=Field<int>(registry,0xc)=Field<int>(registry,0x28)=1;Field<unsigned>(registry,0x10)=1;
 Field<std::uintptr_t>(r)=p.base_+0x394bfc0;Field<unsigned char>(r,0x253)=1;
 Field<unsigned>(r,0x88)=0x3c888889;Field<unsigned>(r,0x90)=0x3d088889;Field<unsigned>(r,0x98)=0x3c888889;
 const auto original=render;
 assert(!p.AdmitRenderReconstruction(false)&&p.witness_.reconstruction_issue==6);
 p.birth_count_=1;p.births_[0].render=r;p.birth_fields_retained_=p.registry_matches=true;
 if(!p.AdmitRenderReconstruction(true)){std::puts("retained B-only no-field parameters rejected despite exact exclusion ownership");return 1;}
 assert(render==original&&Deferred(6,true)&&Deferred(5,true));
 assert(!Deferred(6,false)&&!Deferred(7,true));
 p.registry_matches=false;assert(!p.AdmitRenderReconstruction(true));p.registry_matches=true;
 p.births_[0].render=r+8;assert(!p.AdmitRenderReconstruction(true));p.births_[0].render=r;
 p.birth_fields_retained_=false;assert(!p.AdmitRenderReconstruction(true));p.birth_fields_retained_=true;
 for(auto offset:{0xc8,0xd8}){Field<int>(r,offset)=1;assert(!p.AdmitRenderReconstruction(true));Field<int>(r,offset)=0;}
 // A-owned no-field parameters use the typed retained snapshot independently.
 assert(p.local_fields_a_.rows[0].Capture(p.base_,r));p.local_fields_a_.captured=true;p.local_fields_a_.count=1;
 assert(p.AdmitRenderReconstruction(false)&&render==original);
}
