#define NOMINMAX
#include <Windows.h>
#include <array>
#include <vector>
#include <span>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
enum class FailureCode {CapacityExceeded};
struct Status {bool v=true;bool ok()const{return v;}static Status success(){return{};}static Status failure(FailureCode){return{false};}};
static std::vector<void*> live;
struct Lease {
 std::vector<void*> objects;
 Status Acquire(std::uintptr_t,std::span<void* const> rows,std::size_t){objects.assign(rows.begin(),rows.end());return Validate();}
 Status Validate()const {for(auto* p:objects)if(std::find(live.begin(),live.end(),p)==live.end())return{false};return{};}
 std::size_t owned_bytes()const{return sizeof(*this)+objects.capacity()*sizeof(void*);}
};
struct Sc6ReplayTraceState {
 using Object=void;
 struct Id{Object* object{};int index{},serial{};friend bool operator==(const Id&,const Id&)=default;};
 struct Ref{std::byte* state{};std::byte* controller{};};
 struct Array{std::byte* data{};int count{},capacity{};};
 struct State{Ref ref;Id actor,attachment,mesh,animation;};
 struct Root{Id component,chara,manager,scene;};
 struct Image{std::byte* address{};std::vector<std::byte> bytes;std::uint32_t mutable_bits{};bool projected_child{};};
 struct DynamicImage{enum class Kind{Map,WeakReferences,ChildStrongReferences};std::array<std::byte,0x50> header{};std::vector<std::byte> bytes;Kind kind{};};
 struct FreshChild{
  enum class HistoryPhase{Ready};enum class AnimationPhase{Ready};
  State state;bool prepared=true,identity_retained=true,retired=false;
  HistoryPhase history_phase=HistoryPhase::Ready;AnimationPhase animation_phase=AnimationPhase::Ready;Lease lease;
 };
 std::vector<Root> roots_;std::vector<State> states_;std::vector<Image> values_,bindings_;std::vector<Ref> expired_;
 std::array<DynamicImage,10> dynamic_;std::size_t dynamic_count_{},payload_bytes_{},expired_bytes_{},budget_{};
 std::uintptr_t base_{};void* world_{};DWORD thread_=GetCurrentThreadId();bool captured_{},validation_only_{};Lease lease_;
 const Sc6ReplayTraceState* survivor_source_{};
 static inline unsigned writes{};
 template<class T>static T& At(void* p,std::size_t o){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+o);}
 static bool Live(const Id& id){return !id.object || std::find(live.begin(),live.end(),id.object)!=live.end();}
 static Status Fail(const char*){return{false};}
 bool OwnsExpired(const Ref&)const{return false;}bool RetainExpired(const Ref&){return false;}
 static bool Writable(const Image&){++writes;return true;}static bool Write(const Image&){++writes;return true;}
 Status ValidateValues()const{return ValidateBindings();}
 #include "trace_survivor_methods.inl"
};
int main(){
 using T=Sc6ReplayTraceState;
 auto* pages=static_cast<std::byte*>(VirtualAlloc(nullptr,0xa000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(pages);
 T a;a.base_=0x140000000;a.world_=reinterpret_cast<void*>(0x1234);a.captured_=true;
 const auto id=[&](std::size_t offset,int i){return T::Id{pages+offset,i,i+10};};
 a.roots_={{{id(0,0),id(0x1000,1),id(0x2000,2)}}};
 T::At<void*>(pages,0x1c8)=a.world_;T::At<void*>(pages+0x1000,0x458)=pages+0x2000;T::At<void*>(pages+0x2000,0x3a8)=pages;
 T::State common{{pages+0x3010,pages+0x3000},id(0x4000,4),{},id(0x5000,5),id(0x6000,6)};
 T::FreshChild child;child.state={{pages+0x7010,pages+0x7000},id(0x8000,8),{},id(0x9000,9),{}};
 a.states_={common,child.state};
 for(auto* control:{common.ref.controller,child.state.ref.controller}) {
  T::At<std::uintptr_t>(control,0)=a.base_+0x3362590;T::At<int>(control,8)=1;T::At<int>(control,12)=2;
 }
 live={pages,pages+0x1000,pages+0x2000,pages+0x4000,pages+0x5000,pages+0x6000,pages+0x8000,pages+0x9000};
 a.lease_.objects=live;child.lease.objects={pages+0x8000,pages+0x9000};
 const auto field=[&](std::size_t offset,bool projected){pages[offset]=std::byte{0x37};return T::Image{pages+offset,{std::byte{0x37}},0,projected};};
 a.values_={field(0x4100,false),field(0x8100,true)};a.bindings_=a.values_;a.payload_bytes_=4;
 a.dynamic_count_=1;auto& strong=a.dynamic_[0];strong.kind=T::DynamicImage::Kind::ChildStrongReferences;
 strong.bytes.resize(16);T::At<T::Ref>(strong.bytes.data(),0)=child.state.ref;T::At<T::Array>(strong.header.data(),0)={pages+0x7100,1,1};
 const auto saved_header=strong.header;const auto saved_bytes=strong.bytes;
 std::array<T::FreshChild*,1> children{&child};T view;
 assert(a.CreateSurvivingOwnerView(children,1<<20,view).ok());
 assert(view.validation_only_&&view.states_.size()==1&&view.states_[0].ref.state==common.ref.state);
 assert(view.values_.size()==1&&view.bindings_.size()==1&&view.ValidatePayloadAccounting());
 assert(view.dynamic_[0].bytes.empty()&&!T::At<int>(view.dynamic_[0].header.data(),8));
 assert(strong.header==saved_header&&strong.bytes==saved_bytes&&!a.validation_only_);
 assert(!view.Install().ok()&&T::writes==0);
 {T rejected;std::array duplicate{&child,&child};assert(!a.CreateSurvivingOwnerView(duplicate,1<<20,rejected).ok());}
 {T rejected;assert(!a.CreateSurvivingOwnerView(children,0,rejected).ok());}
 {T rejected;T::At<int>(strong.header.data(),8)=0;assert(!a.CreateSurvivingOwnerView(children,1<<20,rejected).ok());strong.header=saved_header;}
 live.resize(live.size()-2);DWORD previous{};assert(VirtualProtect(pages+0x7000,0x3000,PAGE_NOACCESS,&previous));
 assert(!a.ValidateBindings().ok()&&view.ValidateBindings().ok()); // No expired child lease/payload in common validation.
 assert(!view.Install().ok()&&T::writes==0);
 VirtualFree(pages,0,MEM_RELEASE);
}
