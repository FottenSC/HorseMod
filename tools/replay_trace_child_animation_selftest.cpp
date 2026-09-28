#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
using Object=void;
struct Array {std::byte* data{};int count{},capacity{};};
template<class T>T& At(void* p,std::size_t offset){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
#include "trace_child_animation_write.inl"
static std::array<Array,2> destinations{};
static std::array<std::vector<std::byte>,2> backing;
static unsigned publish_calls{},copy_calls{},fault{};
static void Copy(void* pointer,const void* source,int count,int previous,int slack){
 ++copy_calls;auto* target=static_cast<Array*>(pointer);assert(target->capacity==previous);
 unsigned index=target==&destinations[0]?0:1;assert(target==&destinations[index]);
 backing[index].assign(std::size_t(count+slack)*0x70,std::byte{0xcc});
 target->data=backing[index].empty()?nullptr:backing[index].data();target->count=count;target->capacity=count+slack;
 // Native copying leaves padding and spare rows untouched. The production
 // handoff must use each destination's retained backing after allocation.
 for(int row=0;row<count;++row){
  std::memcpy(target->data+row*0x70,static_cast<const std::byte*>(source)+row*0x70,0x2d);
  std::memcpy(target->data+row*0x70+0x30,static_cast<const std::byte*>(source)+row*0x70+0x30,0x3c);
 }
 if(fault==1)++target->count;
 if(fault==2)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
}
static void Publish(void* owner,const Array* rows){
 ++publish_calls;assert(owner==reinterpret_cast<void*>(0x1234));
 if(fault==3)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
 for(auto& target:destinations)Copy(&target,rows->data,rows->count,target.capacity,0);
}
static void Entry(std::byte* at,const unsigned char* prologue,std::size_t bytes,
    const unsigned char* undo,std::size_t undo_bytes,void* target){
 std::memcpy(at,prologue,bytes);std::memcpy(at+bytes,undo,undo_bytes);at+=bytes+undo_bytes;
 const unsigned char jump[]{0x48,0xb8};std::memcpy(at,jump,2);std::memcpy(at+2,&target,8);at[10]=std::byte{0xff};at[11]=std::byte{0xe0};
}
int main(){
 auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,0x8e0000,MEM_RESERVE,PAGE_NOACCESS));assert(memory);
 assert(VirtualAlloc(memory+0x8d5000,4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 assert(VirtualAlloc(memory+0x8c8000,4096,MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 constexpr unsigned char publish[]{0x40,0x57,0x41,0x57,0x48,0x83,0xec,0x38,0x48,0x89,0x5c,0x24,0x50,0x48,0x8b,0xfa};
 constexpr unsigned char publish_undo[]{0x48,0x83,0xc4,0x38,0x41,0x5f,0x5f};
 constexpr unsigned char copy[]{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
 constexpr unsigned char copy_undo[]{0x48,0x83,0xc4,0x20,0x5f};
 Entry(memory+0x8d5450,publish,sizeof(publish),publish_undo,sizeof(publish_undo),reinterpret_cast<void*>(&Publish));
 Entry(memory+0x8c8a50,copy,sizeof(copy),copy_undo,sizeof(copy_undo),reinterpret_cast<void*>(&Copy));
 FlushInstructionCache(GetCurrentProcess(),memory+0x8c8000,0xe000);
 std::array<std::byte,336> primary;primary.fill(std::byte{0x51});
 std::array<std::byte,224> secondary;secondary.fill(std::byte{0x72});
 std::array<std::byte,336> complete_b;complete_b.fill(std::byte{0x19});const auto original_b=complete_b;
 const std::array<ChildAnimationRows,2> rows{{{&destinations[0],{primary.data(),2,3},primary.data()},
     {&destinations[1],{secondary.data(),1,2},secondary.data()}}};
 const auto base=reinterpret_cast<std::uintptr_t>(memory);
 assert(WriteChildAnimation(base,reinterpret_cast<void*>(0x1234),rows.data(),rows.size()));
 assert(publish_calls==1&&copy_calls==4&&destinations[0].count==2&&destinations[0].capacity==3);
 assert(destinations[1].count==1&&destinations[1].capacity==2);
 assert(!std::memcmp(destinations[0].data,primary.data(),primary.size()));
 assert(!std::memcmp(destinations[1].data,secondary.data(),secondary.size())&&complete_b==original_b);
 for(fault=1;fault<=3;++fault){destinations={};for(auto& b:backing)b.clear();publish_calls=copy_calls=0;
  assert(!WriteChildAnimation(base,reinterpret_cast<void*>(0x1234),rows.data(),rows.size()));
  assert(publish_calls==1&&complete_b==original_b);
 }
 VirtualFree(memory,0,MEM_RELEASE);
 std::puts("native trace animation handoff restores distinct retained destinations including padding; native faults reject");
}
