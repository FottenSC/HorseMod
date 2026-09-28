#define NOMINMAX
#include "Sc6ReplayTraceChildFactory.hpp"
#include "ReplayTraceWeakReference.hpp"
#include <array>
#include <cassert>
#include <cstdio>
using Factory=Horse::Deterministic::Sc6ReplayTraceChildFactory;
static Factory::Journal* active{};static unsigned calls{},fault{};
static std::array<std::byte,0x108> control{};
static Factory::Reference* Native(void* component,Factory::Reference* output,void* parts,int setting,unsigned char kind){
 ++calls;assert(active&&active->phase==Factory::Phase::Calling&&output==&active->reference);
 assert(component==reinterpret_cast<void*>(0x1234)&&parts==reinterpret_cast<void*>(0x5678)&&setting==1&&kind==30);
 if(fault==1)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
 *output={control.data()+16,control.data()};
 if(fault==2)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
 if(fault==3)return nullptr;
 if(fault==4)output->state+=1;
 return output;
}
int main(){
 using Weak=Horse::Deterministic::ReplayTraceWeakController;
 auto* lifetime=static_cast<std::byte*>(VirtualAlloc(nullptr,8192,MEM_RESERVE,PAGE_NOACCESS));assert(lifetime);
 assert(VirtualAlloc(lifetime,4096,MEM_COMMIT,PAGE_READWRITE));
 auto* identity=reinterpret_cast<Weak*>(lifetime+4096-sizeof(Weak));
 *identity={0x1234,1,1};static unsigned deleted{};deleted=0;
 const auto dispose=+[](void*){++deleted;};
 assert(identity->RetainLiveIdentity(0x1234)&&identity->strong==1&&identity->weak==2);
 assert(identity->ReleaseIdentity(0x1234,dispose)&&identity->strong==1&&identity->weak==1&&deleted==0);
 assert(!identity->ReleaseIdentity(0x1234,dispose));
 assert(identity->RetainLiveIdentity(0x1234));
 // Completed native destruction removes strong and implicit weak ownership.
 // State starts on PAGE_NOACCESS; only the retained controller remains valid.
 identity->strong=0;--identity->weak;
 assert(identity->Expired(0x1234)&&!identity->RetainLiveIdentity(0x1234));
 assert(identity->ReleaseIdentity(0x1234,dispose)&&deleted==1&&identity->strong==0);
 assert(!identity->ReleaseIdentity(0x1234,dispose)&&deleted==1);
 for(auto bad:{Weak{0x9999,1,1},Weak{0x1234,-1,1},Weak{0x1234,1,0},Weak{0x1234,1,INT_MAX}})
  assert(!bad.RetainLiveIdentity(0x1234));
 VirtualFree(lifetime,0,MEM_RELEASE);
 auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4190000,MEM_RESERVE,PAGE_NOACCESS));assert(memory);
 auto base=reinterpret_cast<std::uintptr_t>(memory);auto* entry=memory+0x8d1c00;
 assert(VirtualAlloc(memory+0x8d1000,4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 // Exact verified native prologue, immediately undone before tail-calling the
 // controlled factory. This exercises production signature/ABI/journal code.
 unsigned char code[]{0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,
  0x48,0x8d,0xac,0x24,0x30,0xff,0xff,0xff,0x48,0x81,0xec,0xd0,0x01,0x00,0x00,
  0x48,0x81,0xc4,0xd0,0x01,0x00,0x00,0x41,0x5f,0x41,0x5e,0x41,0x5d,0x5f,0x5e,0x5b,0x5d,
  0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
 auto native=&Native;std::memcpy(code+49,&native,8);std::memcpy(entry,code,sizeof(code));FlushInstructionCache(GetCurrentProcess(),entry,sizeof(code));
 assert(Factory::Signature(base));
 assert(VirtualAlloc(memory+0x418a000,4096,MEM_COMMIT,PAGE_READWRITE));
 assert(VirtualAlloc(memory+0x3362000,4096,MEM_COMMIT,PAGE_READWRITE));
 const std::uint32_t radius_source=0x4aea6000;
 std::memcpy(memory+0x33625b4,&radius_source,4);
 std::array<std::byte,0xa0> mesh{};
 const std::uint32_t imported[]{0,0,0,0x44c80000,0x44c80000,0x44c80000,0x452d3480};
 const std::uint32_t extended[]{0,0,0,0x44c80000,0x44c80000,0x44c80000,0x44c80000};
 std::memcpy(mesh.data()+0x50,imported,sizeof(imported));
 std::memcpy(mesh.data()+0x6c,extended,sizeof(extended));
 const auto original=mesh;
 // Native construction rewrites shared asset bounds. Reject any case where
 // that write could alter retained B, before entering the native factory.
 for(std::size_t offset=0x50;offset<mesh.size();++offset){
  mesh=original;mesh[offset]^=std::byte{1};const auto before=mesh;
  Factory::Journal rejected;rejected.mesh_asset=reinterpret_cast<std::uintptr_t>(mesh.data());active=&rejected;calls=0;
  assert(!Factory::Construct(base,0x1234,0x5678,30,rejected));
  assert(calls==0&&rejected.phase==Factory::Phase::Empty&&mesh==before);
 }
 mesh=original;
 for(const auto offset:{0x418ac48u,0x418ac4cu,0x418ac50u,0x33625b4u}) {
  memory[offset]^=std::byte{1};
  assert(!Factory::PreservesSharedMeshBounds(base,reinterpret_cast<std::uintptr_t>(mesh.data())));
  memory[offset]^=std::byte{1};
 }
 assert(!Factory::PreservesSharedMeshBounds(base,0));
 assert(!Factory::PreservesSharedMeshBounds(base,UINTPTR_MAX));
 assert(!Factory::PreservesSharedMeshBounds(base,reinterpret_cast<std::uintptr_t>(memory+0x1000)));
 assert(!Factory::PreservesSharedMeshBounds(UINTPTR_MAX,reinterpret_cast<std::uintptr_t>(mesh.data())));
 for(fault=0;fault<5;++fault){Factory::Journal journal;active=&journal;calls=0;
  journal.mesh_asset=reinterpret_cast<std::uintptr_t>(mesh.data());
  const auto ok=Factory::Construct(base,0x1234,0x5678,30,journal);
  assert(calls==1&&ok==(fault==0));
  assert(journal.phase==(fault?Factory::Phase::Failed:Factory::Phase::Returned));
  if(fault!=1)assert(journal.reference.controller==control.data());
  assert(!Factory::Construct(base,0x1234,0x5678,30,journal)&&calls==1);
 }
 Factory::Journal journal;active=&journal;calls=0;
 entry[0]=std::byte{0x90};assert(!Factory::Signature(base));assert(!Factory::Construct(base,0x1234,0x5678,30,journal));
 assert(journal.phase==Factory::Phase::Empty&&calls==0);
 assert(!Factory::Signature(0)&&!Factory::Signature(UINTPTR_MAX));
 VirtualFree(memory,0,MEM_RELEASE);
 std::puts("trace child factory preserves stable output and poisons partial/exceptional acquisition; retry and wrong signature reject");
}
