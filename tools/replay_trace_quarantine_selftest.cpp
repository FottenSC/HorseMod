#include <array>
#include <vector>
#include <span>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define STR(x) x
#define REQUIRE(x) do{if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace RC{enum class LogLevel{Warning};struct Output{template<LogLevel,class...T>static void send(const char*,T...){}};}
struct Owner {bool dormant{true},binding{true};bool RetainedDormantRenderBinding(std::uintptr_t)const{return dormant;}bool RetainedRenderBinding(std::uintptr_t,const std::array<int,2>&,unsigned,std::uintptr_t,bool)const{return binding;}};
struct LightingPrimitive{std::uintptr_t component{},proxy_type{};unsigned id{},slot_count{};std::array<int,2>weak{};std::array<std::uintptr_t,1>proxy_inputs{};};
struct Sc6ReplayParticleCopy{
 std::uintptr_t base_{0x140000000};struct{bool captured{true};std::vector<LightingPrimitive>primitives;}lighting_b_;
 struct{bool done{true};bool retired()const{return done;}}completion_;
 struct{bool native_write_uncommitted{true};}witness_;Owner owner;Owner* trace_render_owner_{&owner};
 bool AppendQuarantinedTracePrimitive(std::uintptr_t,std::span<unsigned>,std::size_t&)const noexcept;
};
#include "trace_quarantine.inl"
int main(){Sc6ReplayParticleCopy f;std::array<unsigned,2> ids{7,9};std::size_t count=1;
 REQUIRE(f.AppendQuarantinedTracePrimitive(123,ids,count));REQUIRE(count==1&&ids[0]==7&&ids[1]==9);
 f.owner.dormant=false;REQUIRE(!f.AppendQuarantinedTracePrimitive(123,ids,count));
 f.owner.dormant=true;f.completion_.done=false;REQUIRE(!f.AppendQuarantinedTracePrimitive(123,ids,count));f.completion_.done=true;
 f.lighting_b_.primitives.push_back({123,f.base_+0x39af350,12,1});REQUIRE(f.AppendQuarantinedTracePrimitive(123,ids,count));REQUIRE(count==2&&ids[1]==12);
 count=1;f.owner.binding=false;REQUIRE(!f.AppendQuarantinedTracePrimitive(123,ids,count));REQUIRE(count==1);
 f.owner.binding=true;f.lighting_b_.primitives.push_back(f.lighting_b_.primitives[0]);REQUIRE(!f.AppendQuarantinedTracePrimitive(123,ids,count));
 std::puts("absent primitive requires dormant owner; present primitive retains exact quarantine admission");}
