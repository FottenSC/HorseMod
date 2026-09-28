// Actual frame-sync routing/control flow; native fence, queue and OS-event
// services are independent probes, not an emulation of native/GPU ownership.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#define private public
#include "Sc6ReplayTaskGroup.hpp"
#undef private
namespace Horse::Deterministic {
namespace {
template<class T>T& Field(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
template<class T>std::uintptr_t Word(T v){if constexpr(std::is_pointer_v<T>)return reinterpret_cast<std::uintptr_t>(v);else return static_cast<std::uintptr_t>(v);}
std::byte* image{};
std::array<std::byte,0x50> created{},older{};
std::array<std::uintptr_t,3> state{};
int graph{},os_event{};
unsigned begins{},pumps{},drains{},health{},flushes{},waits{},bridges{},returned{},released{},native_wait{};
bool busy{},complete{},timeout{},disabled_timeout{},cached_timeout{},false_signal{},mutate_slot{};
double clock_value{};
void* released_event{};
[[noreturn]]void Fail(unsigned){throw std::runtime_error("terminal invariant");}
template<class R=void,class... A>R Native(std::uintptr_t,std::uintptr_t rva,A...args){
    const std::array<std::uintptr_t,sizeof...(A)> a{Word(args)...};
    if constexpr(std::is_same_v<R,void>) {
        if(rva==0x21876d0)throw std::runtime_error("RED frame sync enters native unowned GT drain");
        if(rva==0x15e9510){++begins;if(Field<bool>(image,0x4351840))*reinterpret_cast<void**>(a[0])=created.data();return;}
        if(rva==0x15e9e30){++health;if(mutate_slot && health==2)Field<void*>(image,0x41457d8)=older.data();return;}
        if(rva==0x15ef6f0){++flushes;return;}
        if(rva==0x15efd70){if(a[0]!=Word(&graph)||a[1]!=Word(&os_event)||a[3]!=2)throw std::runtime_error("bad completion bridge");++bridges;return;}
        if(rva==0xd31540){if(a[0]!=Word(&os_event)||waits<2)throw std::runtime_error("early OS-event return");++returned;return;}
        if(rva==0x15efec0){if(a[1])throw std::runtime_error("unowned native wait drain");++native_wait;return;}
        if(rva==0x3119d9c){if(cached_timeout)throw std::runtime_error("initialized negative CRT epoch reset");*reinterpret_cast<int*>(a[0])=-1;return;}
        if(rva==0x3119d3c){*reinterpret_cast<int*>(a[0])=1;return;}
    } else if constexpr(std::is_same_v<R,void*>) {
        if(rva==0xd24400)return &graph;
        if(rva==0xd28aa0)return &os_event;
        if(rva==0xd470f0)return &graph;
    } else if constexpr(std::is_same_v<R,bool>) {
        if(rva==0xd4ebe0)return true;
        if(rva==0xdd02f0)return disabled_timeout;
    } else if constexpr(std::is_same_v<R,const wchar_t*>) {
        if(rva==0xda6170)return L"";
    }
    throw std::runtime_error("unexpected native service");
}
template<class R=void,class... A>R Virtual(void* object,std::size_t slot,A...args){
    const std::array<std::uintptr_t,sizeof...(A)> a{Word(args)...};
    if constexpr(std::is_same_v<R,bool>) {
        if(object==&graph && slot==0x20 && a[0]==2)return busy;
        if(object==&os_event && slot==0x20 && a[0]==7 && !a[1]) {
            ++waits;
            if(waits>=2 && (!timeout || disabled_timeout || cached_timeout)) {
                if(!false_signal)Field<std::uint64_t>(created.data(),8)=1ull<<26;return true;
            }
            return false;
        }
    }
    throw std::runtime_error("unexpected virtual service");
}
bool EventComplete(void* p){return (InterlockedCompareExchange64(&Field<LONG64>(p,8),0,0)&(1ll<<26))!=0;}
}
void Sc6ReplayTaskGroup::BeginPump(){++drains;pumping_=true;}
void Sc6ReplayTaskGroup::PumpOne(bool idle){if(!idle)throw std::runtime_error("blocking drain");++pumps;if(pumps%2==0)pumping_=false;}
void Sc6ReplayTaskGroup::ReleaseEvent(void* event){if(event){++released;released_event=event;}}
#ifdef REPLAY_OWNED_FRAME_SYNC
double Sc6ReplayTaskGroup::FrameWaitClock()const{return clock_value++;}
void Sc6ReplayTaskGroup::ReportFrameWaitTimeout(double){throw std::runtime_error("native timeout");}
#define __fastfail Fail
#include "Sc6ReplayTaskGroup.FrameSync.inl"
#undef __fastfail
#endif
struct World {
    Sc6ReplayTaskGroup tasks;
    void SyncFrame(std::uintptr_t base,void* state,bool lag){
#ifdef REPLAY_OWNED_FRAME_SYNC
        tasks.SyncFrame(base,state,lag);
#else
        throw std::runtime_error("unreachable owned service before fix");
#endif
    }
};
template<class R=void,class... A>R EngineNative(std::uintptr_t b,std::uintptr_t r,A...a){return Native<R>(b,r,a...);}
template<class T>T& EngineField(void* p,std::size_t n){return Field<T>(p,n);}
struct Host {
    std::uintptr_t image_base_{};World executor_;
    void Run(){auto* image=reinterpret_cast<void*>(image_base_);
#include "frame_sync_call.inl"
    }
};
}
int main(int argc,char** argv){
    using namespace Horse::Deterministic;
    image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4400000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 70;
    const char mode=argc>1?argv[1][0]:'p';
    busy=mode=='b';complete=mode=='c';timeout=mode=='t'||mode=='x'||mode=='e';disabled_timeout=mode=='x';
    cached_timeout=mode=='e';false_signal=mode=='s';mutate_slot=mode=='m';
    int lag=mode=='l';Field<int*>(image,0x41457f8)=&lag;
    Field<bool>(image,0x4351840)=mode!='d';
    Field<unsigned>(image,0x4090840)=7;Field<int>(image,0x4090844)=timeout?0:60000;
    Field<double>(image,0x34d0c40)=0.001;
    Field<int>(image,0x4351b88)=mode=='n'?1:0;
    if(cached_timeout){Field<int>(image,0x4351b90)=-4;Field<bool>(image,0x4351b8c)=true;}
    Field<std::uint64_t>(created.data(),8)=complete?1ull<<26:0;
    Field<std::uint64_t>(older.data(),8)=1ull<<26;
    auto* slots=reinterpret_cast<std::uintptr_t*>(image+0x41457d8);
    slots[0]=Word(older.data());slots[1]=Word(older.data());slots[2]=mode=='i'?2:0;
    Host host;host.image_base_=Word(image);
    try {host.Run();
        if(mode=='t'||mode=='i'||mode=='s'||mode=='m')return 71;
        if(begins!=1)return 72;
        if(mode=='b'||mode=='n') {
            if(native_wait!=1 || waits || returned || released || drains!=(mode=='b'?0u:1u))return 73;
        } else if(mode=='d') {
            if(drains!=1 || health || released || slots[0]!=Word(older.data()))return 74;
        } else if(mode=='c'||mode=='l') {
            if(drains!=1 || health!=1 || waits || released!=1 || slots[lag]!=0)return 75;
            if(lag && (slots[0]!=Word(created.data()) || slots[2]!=1 || released_event!=older.data()))return 76;
        } else {
            if(drains!=3 || health!=3 || waits!=2 || bridges!=1 || returned!=1 || released
                || slots[0]!=Word(created.data()) || Field<int>(image,0x4351b88)!=0)return 77;
        }
        std::puts("owned frame sync preserves drains, lag cursor, native wait lease and fence branch lifetime PASS");return 0;
    }catch(const std::runtime_error& error){
        if(mode=='t' && waits==1 && bridges==1 && !returned && !released && slots[0]==Word(created.data())){
            std::puts("timeout preserves pending OS-event and fence ownership PASS");return 0;}
        if(mode=='i' && !begins && !released){std::puts("invalid cursor rejects before native submission PASS");return 0;}
        if((mode=='s'||mode=='m') && bridges==1 && !returned && !released){std::puts("false signal or mutated fence slot rejects with wait ownership retained PASS");return 0;}
        std::puts(error.what());return 81;
    }
}
