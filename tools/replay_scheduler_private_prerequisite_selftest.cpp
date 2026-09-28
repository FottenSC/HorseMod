#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cassert>
#include <algorithm>
// Execute the actual release boundary. Native Free is a recording dependency;
// all payload pages are inaccessible and no historical UObject is consulted.
static std::vector<void*> freed;
static void NativeFree(void* p){assert(std::find(freed.begin(),freed.end(),p)==freed.end());freed.push_back(p);}
struct Sc6ReplaySchedulerState {
 struct PreparedRestore {
  struct Allocation {void* data{};void* previous{};bool previous_is_added{};};
  struct Patch {};
  std::vector<Allocation> allocations_;std::vector<Patch> patches_;
  bool published_{},allocation_ownership_lost_{},ready_{},executing_{},execution_settled_{},execution_undo_started_{};
  std::uintptr_t base_{};void* world_{};std::uint64_t epoch_{},target_epoch_{},published_fingerprint_{},original_epoch_{},previous_fingerprint_{};
  std::size_t excluded_count_{},added_count_{},execution_reservation_{};
  const void* previous_image_{};const void* execution_image_{};
  int birth_{},private_owners_{};std::array<int,1> excluded_ticks_{},excluded_owners_{},added_ticks_{};
  void Clear() noexcept;
 };
};
#include "scheduler_private_prerequisite.inl"
int main(){
 auto* pages=static_cast<std::byte*>(VirtualAlloc(nullptr,0x5000,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS));assert(pages);
 void* b=pages;void* constructor=pages+0x1000;void* c=pages+0x2000;void* active=pages+0x3000;
 using P=Sc6ReplaySchedulerState::PreparedRestore;
 const auto initialize=[&](P& p){p.base_=reinterpret_cast<std::uintptr_t>(&NativeFree)-0xd46a00;p.allocations_={{c,b,false},{active,constructor,true}};freed.clear();};
 P p;initialize(p);p.Clear();assert((freed==std::vector<void*>{c,active})); // Unpublished preparation: original buffers still native.
 initialize(p);p.published_=true;p.executing_=true;p.Clear();assert(freed.empty()&&p.allocations_.size()==2);
 p.published_=false;p.allocation_ownership_lost_=true;p.Clear();assert(freed.empty()&&p.allocations_.size()==2);
 p.allocation_ownership_lost_=false;p.execution_settled_=true;p.execution_undo_started_=true;
 p.Clear();assert(freed.size()==3&&std::find(freed.begin(),freed.end(),constructor)!=freed.end()
  &&std::find(freed.begin(),freed.end(),b)==freed.end());
 p.Clear();assert(freed.size()==3); // Repeated release never frees again.
 initialize(p);p.executing_=true;p.execution_settled_=true;
 for(auto& allocation:p.allocations_)std::swap(allocation.data,allocation.previous); // Actual CommitBacking handoff.
 p.Clear();assert((freed==std::vector<void*>{b,constructor})); // C now native; free only displaced allocations.
 initialize(p);p.executing_=false;p.execution_settled_=false;p.execution_undo_started_=false;
 p.Clear();assert((freed==std::vector<void*>{c,active})); // Undo before execution restores the original constructor header.
 VirtualFree(pages,0,MEM_RELEASE);
}
