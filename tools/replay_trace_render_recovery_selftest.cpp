// Actual production recovery method; native calls and object leases are controlled.
#include <Windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
struct Status {bool good{true};bool ok() const{return good;}static Status success(){return {};}};
namespace RC {enum class LogLevel {Default};struct Output {template<LogLevel,class...T>static void send(T...){}};}
#define STR(x) x
struct Object {std::array<std::byte,0x1000> memory{};};
struct Id {Object* object{};};
struct State {Id mesh;};
struct Image {std::byte* address{};std::vector<std::byte> bytes;};
template<class T> T& At(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
#include "trace_render_journal.inl"
static unsigned calls{},fail_call{};
struct Fixture {
 using Sc6ReplayTraceState=Fixture;
 struct Retirement {}; // No private-child proof is supplied by this rendering-only fixture.
 std::uintptr_t base_{};void* world_{};std::vector<State> states_;std::vector<Image> values_;
 bool preparation=true,valid=true;
 static inline Fixture* active{};
 static inline bool verification_fault{};
 static Status Fail(const char*){return {false};}
 Status PrepareExecution(const Fixture&,const Fixture&,const Retirement* private_b=nullptr)const{return {preparation && !private_b};}
 Status ValidateValues()const{return {valid};}
 Status ValidateBindings()const{return {valid};}
 static bool RecreateVisibleMesh(std::uintptr_t,void* p) {
  ++calls;if(calls==fail_call)return false;
  At<void*>(p,0xa18)=p;At<void*>(p,0x790)=p;
  if(verification_fault && calls==2)active->valid=false;
  return true;
 }
 static bool RetireHiddenMesh(std::uintptr_t,void* p) {
  ++calls;if(calls==fail_call)return false;
  At<void*>(p,0xa18)=nullptr;At<void*>(p,0x790)=nullptr;return true;
 }
#include "trace_render_method.inl"
};
int main(){
 auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x3900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
 const auto base=reinterpret_cast<std::uintptr_t>(memory);
 const unsigned char create[]{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x83,0xb9,0x10,9,0};
 const unsigned char destroy[]{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xd2,0x80,0xfd,0xff,0x48,0x8d};
 std::memcpy(memory+0x1dc0b20,create,sizeof(create));std::memcpy(memory+0x1dc1b70,destroy,sizeof(destroy));
 At<std::uintptr_t>(memory+0x38829c0,0x298)=base+0x1dc0b20;At<std::uintptr_t>(memory+0x38829c0,0x2b0)=base+0x1dc1b70;
 At<std::uintptr_t>(memory+0x38829c0,0x840)=base+0x2d2bc0;
 for(unsigned failure=0;failure<4;++failure){
  std::array<Object,4> objects{};Fixture f;f.base_=base;f.world_=&objects[2];At<void*>(f.world_,0x168)=&objects[3];
  for(unsigned i=0;i<2;++i){auto* p=&objects[i];At<std::uintptr_t>(p,0)=base+0x38829c0;At<unsigned>(p,0x188)=3;
   At<void*>(p,0x1c8)=f.world_;At<void*>(p,0x910)=&objects[3];At<void*>(p,0xa18)=p;At<void*>(p,0x790)=p;
   f.states_.push_back({{p}});f.values_.push_back({reinterpret_cast<std::byte*>(p)+0x240,std::vector<std::byte>(4)});
  }
  Fixture current,target;RenderRecovery journal;calls=0;fail_call=failure<3?failure:0;
  f.preparation=false;assert(!f.RetireHiddenRenderingForUndo(current,target,journal).ok() && calls==0);
  f.preparation=true;current.valid=failure!=3;const auto first=f.RetireHiddenRenderingForUndo(current,target,journal);
  const auto before=calls;const auto second=f.RetireHiddenRenderingForUndo(current,target,journal);
  if(failure && (first.ok() || second.ok() || calls!=before)){std::printf("uncertain native retirement repeated: failure=%u calls=%u -> %u\n",failure,before,calls);return 41;}
  if(!failure && (!first.ok() || !second.ok() || calls!=2))return 42;
 }
 for(unsigned failure=0;failure<4;++failure){
  std::array<Object,4> objects{};Fixture f;f.base_=base;f.world_=&objects[2];At<void*>(f.world_,0x168)=&objects[3];
  for(unsigned i=0;i<2;++i){auto* p=&objects[i];At<std::uintptr_t>(p,0)=base+0x38829c0;
   At<unsigned>(p,0x188)=3;At<unsigned>(p,0x240)=0x10;At<unsigned char>(p,0xa40)=1;
   At<void*>(p,0x1c8)=f.world_;At<void*>(p,0x910)=&objects[3];f.states_.push_back({{p}});
  }
  Fixture::active=&f;Fixture::verification_fault=failure==3;RenderRecovery journal;calls=0;fail_call=failure<3?failure:0;
  At<void*>(&objects[1],0xec8)=&objects[3];
  assert(!f.ReconstructVisibleRenderingForUndo(journal).ok() && calls==0 && journal.step==RenderRecovery::Step::Empty);
  At<void*>(&objects[1],0xec8)=nullptr;
  At<unsigned char>(&objects[1],0xa40)=0;
  assert(!f.ReconstructVisibleRenderingForUndo(journal).ok() && calls==0);
  At<unsigned char>(&objects[1],0xa40)=1;
  const auto first=f.ReconstructVisibleRenderingForUndo(journal);const auto before=calls;
  const auto second=f.ReconstructVisibleRenderingForUndo(journal);
  if(failure && (first.ok() || second.ok() || calls!=before || journal.step!=RenderRecovery::Step::Failed))return 51;
  if(!failure && (!first.ok() || !second.ok() || calls!=2 || journal.step!=RenderRecovery::Step::Submitted))return 52;
 }
 VirtualFree(memory,0,MEM_RELEASE);std::puts("trace native recovery: preflight, partial acquisition and poisoned retry pass");
}
