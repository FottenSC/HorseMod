// Actual scheduler capture/validation, with controlled identity and storage edges.
#include <Windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "ReplayComponentTickConsumer.hpp"
enum class FailureCode {ContextUnavailable,CapacityExceeded,RestorePreflightFailed,GenerationMismatch,UnsupportedContent};
struct Status { bool value; bool ok() const {return value;} static Status success(){return{true};} static Status failure(FailureCode){return{false};} };
constexpr auto unavailable=FailureCode::ContextUnavailable,capacity=FailureCode::CapacityExceeded,invalid=FailureCode::RestorePreflightFailed;
template<class T> bool Read(std::uintptr_t p,T& v){__try{std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
template<class T,std::size_t N>T Field(const std::array<std::byte,N>& b,std::size_t o){T v;std::memcpy(&v,b.data()+o,sizeof(v));return v;}
template<class T,std::size_t N>void Store(std::array<std::byte,N>& b,std::size_t o,T v){std::memcpy(b.data()+o,&v,sizeof(v));}
template<class T>bool ResizeCopy(std::vector<T>& out,std::uintptr_t p,std::size_t n,std::size_t remaining){if(n>remaining/sizeof(T))return false;out.resize(n);if(n)std::memcpy(out.data(),reinterpret_cast<void*>(p),n*sizeof(T));return true;}
bool FindScene(std::uintptr_t,std::uintptr_t,std::uintptr_t&,std::uintptr_t&){return false;}
#include "scheduler_consumer_helper.inl"
struct Sc6ReplaySchedulerState {
 struct ObjectBinding{std::uintptr_t object{};std::array<int,2> weak{};};
 struct Tick{std::uintptr_t address{};ObjectBinding owner;std::array<std::byte,0x58> binding_layout{};std::vector<std::array<std::byte,16>> prerequisites;};
 struct Level{std::uintptr_t address{};ObjectBinding owner;};
 struct BindingFailure{const char* check{};std::uintptr_t tick{},owner{},expected{},actual{};std::array<int,2> weak{};std::size_t offset{};};
 std::uintptr_t base_{};unsigned thread_=GetCurrentThreadId();ObjectBinding world_;std::vector<Tick> ticks_;std::vector<Level> levels_;
 struct{std::uintptr_t scene{},control{};}scene_;
 std::size_t owned_bytes()const{return ticks_.capacity()*sizeof(Tick);}
 bool IsLiveObject(const ObjectBinding& b)const noexcept{return b.object!=0;}
 Status BindObject(std::uintptr_t p,ObjectBinding& b){b.object=p;return{p!=0};}
 Status BindScene(std::uintptr_t){return{false};}
 Status CaptureTick(std::uintptr_t,std::uintptr_t,std::size_t);
 Status ValidateBindings(std::uintptr_t,void*,BindingFailure* = nullptr)const noexcept;
};
#include "scheduler_consumer_capture.inl"
#include "scheduler_consumer_validate.inl"
int main(){
 std::array<std::byte,0x900> component{},world{};std::array<std::byte,0x180> level{};
 std::array<std::byte,0x400> table{};
 const auto owner=reinterpret_cast<std::uintptr_t>(component.data()), w=reinterpret_cast<std::uintptr_t>(world.data()), l=reinterpret_cast<std::uintptr_t>(level.data()), vt=reinterpret_cast<std::uintptr_t>(table.data());
 const std::uintptr_t base=0x140000000;
 Store(component,0,vt);Store(table,0x300,base+0x1d652a0);
 Store(component,0x110,base+0x3865f98);Store(component,0x158,l);Store(component,0x160,owner);Store(world,0x778,l);
 const auto image=[&]{Sc6ReplaySchedulerState s;s.base_=base;s.world_.object=w;s.levels_.push_back({l,{w,{}}});return s;};
 auto good=image();if(!good.CaptureTick(owner+0x110,l,100000).ok()||!good.ValidateBindings(base,world.data()).ok())return 60;
 // No system/active bit is needed: an admitted owner can activate later.
 Store(table,0x300,base+0x1bcdb90);auto bad=image();
 if(bad.CaptureTick(owner+0x110,l,100000).ok())return 61;
 // Same UObject and tick prefix; its derived dispatch changed after capture.
 if(good.ValidateBindings(base,world.data()).ok())return 62;
 if(good.CaptureTick(owner+0x110,l,100000).ok())return 67;
 Store(table,0x300,base+0x1d652a0);if(!good.ValidateBindings(base,world.data()).ok())return 63;
 // Known Niagara table must reject even if its dispatch slot is replaced.
 Store(component,0,base+0x37fa218);bad=image();if(bad.CaptureTick(owner+0x110,l,100000).ok())return 64;
 Store(component,0,vt);Store(table,0x300,std::uintptr_t{});bad=image();if(bad.CaptureTick(owner+0x110,l,100000).ok())return 65;
 Store(table,0x300,base+0x1d652a0);Store(component,0,std::uintptr_t{1});bad=image();if(bad.CaptureTick(owner+0x110,l,100000).ok())return 66;
 std::puts("scheduler Niagara capture and changed-dispatch admission PASS; no native tick suppressed");
 return 0;
}
