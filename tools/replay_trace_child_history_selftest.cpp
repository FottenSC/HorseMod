#define NOMINMAX
#include <Windows.h>
#include "ReplayTraceWeakReference.hpp"
#include "Sc6ReplayTraceChildFactory.hpp"
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <cstdio>
using namespace Horse::Deterministic;
struct Status {bool good=true;bool ok()const{return good;}static Status success(){return {};}};
struct Ref {std::byte* state{};std::byte* controller{};};
struct Id {void* object{};friend bool operator==(const Id&,const Id&)=default;};
struct State {Ref ref;Id actor,attachment,mesh,animation;};
struct Image {std::byte* address{};std::vector<std::byte> bytes;};
struct Array {std::byte* data{};int count{},capacity{};};
struct DynamicImage {std::array<std::byte,0x50> header{};std::vector<std::byte> bytes;};
template<class T>T& At(void* p,std::size_t offset){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
struct Fixture {
 using Sc6ReplayTraceState=Fixture;
 struct ChildSource {Ref historical;};
 struct FreshChild {
  enum class HistoryPhase {Empty,Writing,Ready,Failed};
  Sc6ReplayTraceChildFactory::Journal factory;
  State state;Ref historical,cached_parent;
  bool prepared=true,retired{},identity_retained=true;
  HistoryPhase history_phase=HistoryPhase::Empty;
  struct Lease {bool valid=true;Status Validate()const{return {valid};}}lease;
 };
 DWORD thread_=GetCurrentThreadId();std::uintptr_t base_{};
 std::vector<Image> values_,bindings_;std::vector<State> states_;
 std::array<DynamicImage,10> dynamic_{};std::size_t dynamic_count_{};
 bool b_valid=true;unsigned char* b_data{};std::size_t b_size{};
 Status ValidateValues()const {if(!b_valid)return {false};for(std::size_t i=0;i<b_size;++i)if(b_data[i]!=0x5a)return {false};return {};}
#include "trace_child_storage_alias.inl"
 bool Contains(const Ref& ref)const{return std::any_of(states_.begin(),states_.end(),[&](const auto& s){return s.ref.state==ref.state&&s.ref.controller==ref.controller;});}
 static Status Fail(const char*){return {false};}
#include "trace_retained_spans.inl"
#include "trace_fresh_history.inl"
};
static void Save(std::vector<Image>& into,void* owner,std::size_t offset,const void* value,std::size_t bytes){
 Image image{static_cast<std::byte*>(owner)+offset,std::vector<std::byte>(bytes)};
 std::memcpy(image.bytes.data(),value,bytes);into.push_back(std::move(image));
}
int main(){
 auto* old=static_cast<std::byte*>(VirtualAlloc(nullptr,0x5000,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));assert(old);
 std::array<std::byte,0x108> fresh{};std::array<std::array<std::byte,336>,3> arrays{};
 std::array<unsigned char,256> b_values{};b_values.fill(0x5a);
 Fixture a,b;b.b_data=b_values.data();b.b_size=b_values.size();
 Fixture::FreshChild child;child.historical={old+16,old};child.state.ref={fresh.data()+16,fresh.data()};
 child.factory.phase=Sc6ReplayTraceChildFactory::Phase::Returned;
 child.factory.reference={child.state.ref.state,child.state.ref.controller};
 At<ReplayTraceWeakController>(fresh.data(),0)={0x3362590,1,2};
 Fixture::ChildSource source{child.historical};auto* state=child.state.ref.state;
 for(const auto range:{std::pair{0u,2u},{0x20u,0x35u},{0x80u,8u},{0x98u,4u},
     {0xb0u,4u},{0xc0u,9u},{0xccu,0x24u},{0xf0u,1u},{0xf4u,4u}}){
  std::array<std::byte,0x35> payload{};payload.fill(std::byte{0x37});
  Save(a.values_,source.historical.state,range.first,payload.data(),range.second);
 }
 Ref parent{};Save(a.bindings_,source.historical.state,0xa0,&parent,sizeof(parent));
 unsigned ordinal{};
 for(const auto offset:{0x10u,0x70u,0x88u}) {
  Array captured{old+0x1000*(ordinal+1),2,3};At<Array>(state,offset)={arrays[ordinal].data(),0,3};
  Save(a.bindings_,source.historical.state,offset,&captured.data,8);
  Save(a.bindings_,source.historical.state,offset+12,&captured.capacity,4);
  Save(a.values_,source.historical.state,offset+8,&captured.count,4);
  std::array<std::byte,336> payload{};payload.fill(std::byte{static_cast<unsigned char>(0x41+ordinal)});
  Save(a.values_,captured.data,0,payload.data(),3*(offset==0x10?0x70:0x2c));++ordinal;
 }
 const auto original_a=a;const auto original_fresh=fresh;const auto original_child=child;
 assert(a.PrepareFreshChildHistory(source,b,child).ok());
 assert(child.history_phase==Fixture::FreshChild::HistoryPhase::Ready&&b.ValidateValues().ok());
 ordinal=0;
 for(const auto offset:{0x10u,0x70u,0x88u}) {
  const auto live=At<Array>(state,offset);assert(live.data==arrays[ordinal].data()&&live.count==2&&live.capacity==3);
  for(std::size_t i=0;i<3*(offset==0x10?0x70:0x2c);++i)assert(live.data[i]==std::byte{static_cast<unsigned char>(0x41+ordinal)});
  ++ordinal;
 }
 assert(!a.PrepareFreshChildHistory(source,b,child).ok());
 {
  a=original_a;fresh=original_fresh;child=original_child;
  // The native allocator may reuse an expired A allocation address. A owns
  // retained bytes, while complete B owns the live protected allocations.
  auto* reused=arrays[0].data();
  std::memcpy(a.bindings_[1].bytes.data(),&reused,sizeof(reused));
  a.values_[10].address=reused;
  assert(a.PrepareFreshChildHistory(source,b,child).ok());
  assert(b.ValidateValues().ok());
  fresh=original_fresh;child=original_child;
  b.values_.push_back({reused,std::vector<std::byte>(336)});
  const auto before=fresh;
  assert(!a.PrepareFreshChildHistory(source,b,child).ok()&&fresh==before);
  b.values_.clear();
 }
 {
  std::array<std::byte,0x108> parent_storage{};
  auto* control=reinterpret_cast<ReplayTraceWeakController*>(parent_storage.data());
  *control={0x3362590,1,1};
  const Ref retained_parent{parent_storage.data()+16,parent_storage.data()};
  a=original_a;fresh=original_fresh;child=original_child;
  a.states_={{retained_parent,{reinterpret_cast<void*>(1)},{},{reinterpret_cast<void*>(2)},{reinterpret_cast<void*>(3)}}};
  b.states_=a.states_;
  std::memcpy(a.bindings_[0].bytes.data(),&retained_parent,sizeof(retained_parent));
  assert(a.PrepareFreshChildHistory(source,b,child).ok());
  assert(control->strong==1&&control->weak==2&&At<Ref>(state,0xa0).controller==parent_storage.data());
  assert(child.cached_parent.state==retained_parent.state&&child.cached_parent.controller==retained_parent.controller);
  // Native child payload destruction drops exactly its cached weak reference.
  assert(control->ReleaseIdentity(0x3362590,+[](void*){assert(false);})&&control->weak==1);
  for(unsigned fault=0;fault<3;++fault){
   fresh=original_fresh;child=original_child;b.states_=a.states_;*control={0x3362590,1,1};
   if(fault==0)b.states_.clear();
   if(fault==1)b.states_[0].actor.object=reinterpret_cast<void*>(9);
   if(fault==2)control->strong=0;
   const auto before=fresh;
   assert(!a.PrepareFreshChildHistory(source,b,child).ok()&&fresh==before&&control->weak==1);
  }
  b.states_.clear();
 }
 for(unsigned fault=0;fault<6;++fault){
  a=original_a;fresh=original_fresh;child=original_child;b.b_valid=true;
  for(auto& row:arrays)row.fill(std::byte{0});
  switch(fault){
   case 0:At<Array>(state,0x88).capacity=2;break;
   case 1:a.values_.pop_back();break;
   case 2:a.values_.push_back(a.values_[0]);break;
   case 3:child.lease.valid=false;break;
   case 4:b.b_valid=false;break;
   case 5:At<Ref>(state,0xa0)={old+16,old};break;
  }
  const auto before=fresh;const auto before_arrays=arrays;
  assert(!a.PrepareFreshChildHistory(source,b,child).ok());
  assert(fresh==before&&arrays==before_arrays&&child.history_phase==Fixture::FreshChild::HistoryPhase::Empty);
 }
 VirtualFree(old,0,MEM_RELEASE);
 std::puts("fresh trace history uses retained bytes, preserves native headers and B, and preflights all writes");
}
