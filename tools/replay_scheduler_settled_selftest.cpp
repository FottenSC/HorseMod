// Production final scheduler validation; controlled native binding/fingerprint edges.
#include <Windows.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>
enum class FailureCode {IllegalTransition,GenerationMismatch,RestoreVerificationFailed,ContextUnavailable};
struct Status {bool v;bool ok()const{return v;}static Status success(){return{true};}static Status failure(FailureCode){return{false};}};
bool SameBytes(std::uintptr_t p,const void* q,std::size_t n){return !n || std::memcmp(reinterpret_cast<void*>(p),q,n)==0;}
bool WriteBytes(std::uintptr_t p,const void* q,std::size_t n){std::memcpy(reinterpret_cast<void*>(p),q,n);return true;}
struct Sc6ReplaySchedulerState {
 struct PublishedFailure{const char* check{};std::uintptr_t address{},owner{};std::size_t offset{};unsigned expected{},observed{};};
 struct Tick{std::uintptr_t address{};struct Owner{std::uintptr_t object{};}owner;};std::vector<Tick> ticks_;
 bool live=true,current=true;mutable unsigned binding_checks{},current_checks{};std::uint64_t epoch_=5;
 Status ValidateBindings(std::uintptr_t,void*)const{++binding_checks;return{live};}
 Status ValidateCurrentImage(std::uintptr_t,void*)const{++current_checks;return{live&&current};}
 struct PreparedRestore{
  struct Patch{std::uintptr_t destination{};std::array<std::byte,0x58> bytes{};std::size_t size{};};
  std::vector<Patch> patches_;std::vector<int> allocations_;
  bool ready_=true,published_=true,executing_=true,execution_settled_=true,execution_undo_started_{};
  unsigned thread_=GetCurrentThreadId();std::uintptr_t base_{};void* world_{};
  std::uint64_t epoch_=5,target_epoch_=5,previous_fingerprint_=19,published_fingerprint_=23;
  const Sc6ReplaySchedulerState* execution_image_{};const Sc6ReplaySchedulerState* previous_image_{};
  bool previous_intact=true,private_live=true;std::size_t excluded_count_{};std::array<std::uintptr_t,1> excluded_ticks_{},excluded_owners_{};
  struct Birth{bool live=true;Status ValidateLifetimes()const{return{live};}Status ValidateOwners()const{return{live};}}birth_;
  Status ValidatePrivateOwners()const{return{private_live};}
  bool PreviousFingerprint(std::uint64_t& out)const{out=previous_intact?19:20;return true;}
  static bool FingerprintImages(const std::vector<Patch>&,const std::vector<int>&,bool,std::uint64_t& out){out=23;return true;}
  std::uint64_t storage_fingerprint()const{return 23;}
 };
 Status ValidateExcludedTick(const PreparedRestore&,bool)const{return{true};}
 Status ValidatePublishedBacking(std::uintptr_t,void*,const PreparedRestore&,PublishedFailure* = nullptr)const noexcept;
};
#include "scheduler_settled.inl"
int main(){
 using S=Sc6ReplaySchedulerState;S a,b,c;S::PreparedRestore p;
 std::uint64_t native_epoch=5;p.base_=reinterpret_cast<std::uintptr_t>(&native_epoch)-0x4197170;
 p.execution_image_=&c;p.previous_image_=&b;
 const auto check=[&]{return a.ValidatePublishedBacking(p.base_,nullptr,p).ok();};
 assert(check());a.live=false;
 // Natural A-only death is valid after C settlement. A must not be dereferenced.
 const auto reads=a.binding_checks;assert(check());assert(a.binding_checks==reads);
 b.live=false;assert(!check());b.live=true;
 c.live=false;assert(!check());c.live=true;c.current=false;assert(!check());c.current=true;
 p.previous_intact=false;assert(!check());p.previous_intact=true;
 p.birth_.live=false;assert(!check());p.birth_.live=true;
 p.private_live=false;assert(!check());p.private_live=true;
 p.execution_settled_=false;assert(!check());p.execution_settled_=true;
 p.execution_undo_started_=true;assert(!check());p.execution_undo_started_=false;
 p.execution_image_=nullptr;assert(!check());p.execution_image_=&c;
 p.previous_image_=nullptr;assert(!check());p.previous_image_=&b;
 ++native_epoch;assert(!check());--native_epoch;
 ++p.target_epoch_;assert(!check());--p.target_epoch_;
 // Before execution the published A bindings still must be live.
 p.executing_=false;assert(!check());a.live=true;assert(check());
}
