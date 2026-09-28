#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <climits>
#define STR(x) x
namespace RC {enum class LogLevel {Warning};struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};}
using Object=void;
struct Id {Object* object{};};
struct State {Id actor,mesh,animation;};
struct FreshChild {State state;struct AnimationDestination{void* historical{};void* destination{};};
 std::vector<AnimationDestination> animation_arrays;void* animation_proxy{};void* animation_metadata{};};
struct Image {std::byte* address{};std::vector<std::byte> bytes;unsigned mutable_bits{};bool projected_child{};};
template<class T>T& At(void* p,std::size_t offset){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
#include "trace_child_projection_binding.inl"
#include "trace_child_animation_owner.inl"
int main(){
 auto* old=static_cast<std::byte*>(VirtualAlloc(nullptr,0x5000,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));assert(old);
 std::array<std::byte,0x5000> native{};
 State original{{old},{old+0x1000},{old+0x2000}};
 FreshChild fresh{{{native.data()},{native.data()+0x1000},{native.data()+0x2000}}};
 ChildProjectionContext context{&original,&fresh,old+0x3000,native.data()+0x3000};
 using Kind=ChildProjectionRegion::Kind;
 auto check=[&](Kind kind,std::size_t object,std::size_t offset,std::vector<std::byte> bytes,unsigned mask,bool expected){
  ChildProjectionRegion region{reinterpret_cast<std::uintptr_t>(old+object),reinterpret_cast<std::uintptr_t>(native.data()+object),0x1000,kind,&context};
  Image source{old+object+offset,bytes,mask},output{native.data()+object+offset,bytes,mask};const auto before=native;
  assert(ProjectChildBinding(region,source,output)==expected&&native==before);
  if(expected)assert(!std::memcmp(output.bytes.data(),output.address,output.bytes.size()));
  else assert(output.bytes==bytes);
 };
 const auto pointer=[](void* p){std::vector<std::byte> result(8);std::memcpy(result.data(),&p,8);return result;};
 At<void*>(native.data(),0x168)=fresh.state.mesh.object;
 check(Kind::Actor,0,0x168,pointer(original.mesh.object),0,true);
 At<void*>(native.data(),0x168)=fresh.state.animation.object;
 check(Kind::Actor,0,0x168,pointer(original.mesh.object),0,false);
 native[0x85]=std::byte{3};check(Kind::Actor,0,0x85,{std::byte{0x83}},0x80,true);
 check(Kind::Actor,0,0x85,{std::byte{0x81}},0x80,false);
 check(Kind::Actor,0,0x85,std::vector<std::byte>(8),0x80,false);
 At<unsigned>(native.data()+0x1000,0x420)=99;
 check(Kind::Mesh,0x1000,0x420,std::vector<std::byte>(4),0,true);
 At<void*>(native.data()+0x1000,0x910)=reinterpret_cast<void*>(0x1234);
 check(Kind::Mesh,0x1000,0x910,pointer(reinterpret_cast<void*>(0x1234)),0,true);
 check(Kind::Mesh,0x1000,0x910,pointer(reinterpret_cast<void*>(0x5678)),0,false);
 At<void*>(native.data()+0x1000,0xab0)=fresh.state.animation.object;
 check(Kind::Mesh,0x1000,0xab0,pointer(original.animation.object),0,true);
 check(Kind::Mesh,0x1000,0xab0,pointer(original.actor.object),0,false);
 // Equal scheduler bytes do not prove that a secondary tick is dormant.
 {
  auto* tick=native.data()+0x1000+0x7b0;
  std::memset(tick,0,0x58);At<std::uintptr_t>(tick,0)=0x3882140;tick[0xc]=std::byte{0x0c};
  std::vector<std::byte> prefix(tick,tick+0x58);
  check(Kind::Mesh,0x1000,0x7b0,prefix,0,true); // Never-enabled primitive post-physics tick, null target.
  tick[0xc]=prefix[0xc]=std::byte{0x0e};check(Kind::Mesh,0x1000,0x7b0,prefix,0,false);
  tick[0xc]=prefix[0xc]=std::byte{0x0c};
  At<void*>(tick,0x50)=fresh.state.mesh.object;check(Kind::Mesh,0x1000,0x7b0,prefix,0,false);
  At<void*>(tick,0x50)=nullptr;
  At<std::uintptr_t>(tick,0)=At<std::uintptr_t>(prefix.data(),0)=0x3882148;
  check(Kind::Mesh,0x1000,0x7b0,prefix,0,false);
 }
 {
  auto* tick=native.data()+0x1000+0xc58;
  std::memset(tick,0,0x58);At<std::uintptr_t>(tick,0)=0x38829a8;tick[0xc]=std::byte{0x0a};
  std::vector<std::byte> prefix(tick,tick+0x58);
  check(Kind::Mesh,0x1000,0xc58,prefix,0,true); // Cloth capability does not imply registration or Target.
  tick[0xc]=prefix[0xc]=std::byte{0x4a};check(Kind::Mesh,0x1000,0xc58,prefix,0,false);
  tick[0xc]=prefix[0xc]=std::byte{0x0a};
  At<void*>(tick,0x18)=At<void*>(prefix.data(),0x18)=reinterpret_cast<void*>(1);
  check(Kind::Mesh,0x1000,0xc58,prefix,0,false);
  At<void*>(tick,0x18)=At<void*>(prefix.data(),0x18)=nullptr;
  At<std::uintptr_t>(tick,0)=At<std::uintptr_t>(prefix.data(),0)=0x38829b0;
  check(Kind::Mesh,0x1000,0xc58,prefix,0,false);
 }
 // Projection must reject registration or pending work even when A and the
 // constructor happen to agree; those owners require scheduler settlement.
 {
  auto* tick=native.data()+0x1000+0xe08;
  std::memset(tick,0,0x58);At<std::uintptr_t>(tick,0)=0x3882990;tick[0xc]=std::byte{0x0e};
  std::vector<std::byte> prefix(tick,tick+0x58);
  check(Kind::Mesh,0x1000,0xe08,prefix,0,true);
  At<void*>(tick,0x50)=fresh.state.mesh.object;check(Kind::Mesh,0x1000,0xe08,prefix,0,false);
  At<void*>(tick,0x50)=nullptr;
  tick[0xc]=prefix[0xc]=std::byte{0x4e};check(Kind::Mesh,0x1000,0xe08,prefix,0,false);
  tick[0xc]=prefix[0xc]=std::byte{0x0e};
  At<void*>(tick,0x18)=At<void*>(prefix.data(),0x18)=reinterpret_cast<void*>(1);
  check(Kind::Mesh,0x1000,0xe08,prefix,0,false);
  At<void*>(tick,0x18)=At<void*>(prefix.data(),0x18)=nullptr;
  At<std::uintptr_t>(tick,0)=At<std::uintptr_t>(prefix.data(),0)=0x3882998;
  check(Kind::Mesh,0x1000,0xe08,prefix,0,false);
 }
 for(const auto offset:{0x7b0u,0xc58u,0xe08u}) {
  auto* tick=native.data()+0x1000+offset;
  std::memset(tick,0,0x58);At<void*>(tick,0x50)=fresh.state.mesh.object;
  std::vector<std::byte> prefix(tick,tick+0x58);
  At<void*>(prefix.data(),0x50)=original.mesh.object;
  check(Kind::Mesh,0x1000,offset,prefix,0,true);
  tick[0xc]=prefix[0xc]=std::byte{0x40};
  check(Kind::Mesh,0x1000,offset,prefix,0,false);
  tick[0xc]=prefix[0xc]=std::byte{};
  At<void*>(tick,0x18)=At<void*>(prefix.data(),0x18)=reinterpret_cast<void*>(0x1234);
  check(Kind::Mesh,0x1000,offset,prefix,0,false);
 }
 At<void*>(native.data()+0x2000,0x350)=context.new_proxy;
 check(Kind::Animation,0x2000,0x350,pointer(context.old_proxy),0,true);
 check(Kind::Animation,0x2000,0x350,pointer(old),0,false);
 At<void*>(native.data()+0x3000,0xa0)=fresh.state.animation.object;
 At<void*>(native.data()+0x3000,0xa8)=reinterpret_cast<void*>(0x7777);
 auto proxy=pointer(original.animation.object);const auto metadata=pointer(reinterpret_cast<void*>(0x7777));
 proxy.insert(proxy.end(),metadata.begin(),metadata.end());check(Kind::Proxy,0x3000,0xa0,proxy,0,true);
 At<void*>(native.data()+0x3000,0xa0)=reinterpret_cast<void*>(0x12345678);
 check(Kind::Proxy,0x3000,0xa0,proxy,0,false);
 At<void*>(native.data()+0x3000,0xa0)=fresh.state.animation.object;
 auto invalid_source=proxy;At<void*>(invalid_source.data(),0)=old;
 check(Kind::Proxy,0x3000,0xa0,invalid_source,0,false);
 At<void*>(native.data()+0x3000,0xa0)=original.animation.object;check(Kind::Proxy,0x3000,0xa0,proxy,0,false);
 At<void*>(native.data()+0x3000,0xa0)=fresh.state.animation.object;
 fresh.animation_proxy=context.new_proxy;fresh.animation_metadata=reinterpret_cast<void*>(0x7777);
 fresh.animation_arrays={{old+0x23f0,native.data()+0x23f0},{old+0x2500,native.data()+0x2500}};
 assert(ValidatePreparedChildAnimation(fresh,original));
 fresh.animation_arrays[1].destination=native.data()+0x2508;assert(!ValidatePreparedChildAnimation(fresh,original));
 fresh.animation_arrays[1].destination=native.data()+0x2500;
 At<void*>(native.data()+0x3000,0xa0)=reinterpret_cast<void*>(0x12345678);assert(!ValidatePreparedChildAnimation(fresh,original));
 At<void*>(native.data()+0x3000,0xa0)=fresh.state.animation.object;
 At<void*>(native.data()+0x2000,0x350)=native.data()+0x3100;assert(!ValidatePreparedChildAnimation(fresh,original));
 At<void*>(native.data()+0x2000,0x350)=context.new_proxy;
 At<void*>(native.data()+0x3000,0xa8)=reinterpret_cast<void*>(0x8888);check(Kind::Proxy,0x3000,0xa0,proxy,0,false);
 assert(!ValidatePreparedChildAnimation(fresh,original));
 At<void*>(native.data()+0x4000,0)=native.data()+0x4100;
 check(Kind::ArrayHeader,0x4000,0,pointer(old+0x4100),0,true);
 At<unsigned>(native.data()+0x4000,12)=5;
 check(Kind::ArrayHeader,0x4000,12,std::vector<std::byte>(4),0,false);
 check(Kind::ArrayPayload,0x4000,0,pointer(old+0x4100),0,false);
 check(Kind::State,0x4000,0x40,std::vector<std::byte>(8),0,false);
 {
  // Execute the production image-selection/value projection, including its
  // containing-region proof, against inaccessible old storage.
  std::vector<Image> bindings_;
  const auto a=reinterpret_cast<std::uintptr_t>(old);
  const auto b=reinterpret_cast<std::uintptr_t>(native.data());
  std::vector<ChildProjectionRegion> regions{{a,b,0x100,Kind::State,&context}};
  #include "trace_child_project_image.inl"
  Image saved{old+0xfc,std::vector<std::byte>(8,std::byte{0x12}),0},mapped=saved;
  const auto before=native;
  assert(!project(saved,mapped,false)); // Crosses the end of this physical owner.
  saved.address=old-4;mapped=saved;
  assert(!project(saved,mapped,false)); // Crosses the start of this physical owner.
  saved.address=old+0x40;mapped=saved;
  assert(project(saved,mapped,false)&&mapped.address==native.data()+0x40&&mapped.bytes==saved.bytes&&mapped.projected_child);
  saved.address=old+0x200;mapped=saved;
  assert(project(saved,mapped,false)&&mapped.address==saved.address); // Shared unrelated value.
  regions.push_back({a+0x40,b+0x40,16,Kind::ArrayHeader,&context});
  saved.address=old+0x48;saved.bytes.resize(4);mapped=saved;
  assert(project(saved,mapped,false)&&mapped.address==native.data()+0x48);
  regions.back().destination=b+0x80;mapped=saved;
  assert(!project(saved,mapped,false)); // Conflicting typed identities.
  assert(native==before);
 }
 VirtualFree(old,0,MEM_RELEASE);
 std::puts("typed trace projection maps native identity fields only; stale A memory is inaccessible and native state is untouched");
}
