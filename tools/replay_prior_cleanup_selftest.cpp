// Actual world StartTasks branch and task-group prior-cleanup control flow.
// Native scheduling/completion are external probes; no native ownership claim.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#define private public
#include "Sc6ReplayTaskGroup.hpp"
#undef private
namespace Horse::Deterministic {
namespace {
template<class T>T& Field(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
std::array<std::byte,0x40> manager{};
std::array<std::byte,0xa00> sequencer{};
std::array<std::array<std::byte,0x50>,6> events{};
std::array<void*,6> event_refs{};
unsigned prepares{},starts{},waits{},pumps{},clears{};
bool deliver_return{};
std::uintptr_t image{};
int option=1;
[[noreturn]] void Fail(unsigned){throw std::runtime_error("terminal invariant");}
template<class R=void,class... A>R Native(std::uintptr_t,std::uintptr_t rva,A... args){
    if constexpr(std::is_same_v<R,bool>) {if(rva==0xdd02f0)return false;}
    if constexpr(std::is_same_v<R,const wchar_t*>) {if(rva==0xda6170)return L"";}
    if constexpr(std::is_same_v<R,void*>) {if(rva==0x215f460)return manager.data();}
    if constexpr(std::is_same_v<R,void>) {
        if(rva==0x2027390){++prepares;return;}
        if(rva==0x15e3830){++clears;Field<int>(sequencer.data(),0x9a8)=0;return;}
    }
    throw std::runtime_error("unexpected native service");
}
template<class R=void,class... A>R Virtual(void*,std::size_t slot,A...){
    if constexpr(std::is_same_v<R,void>) {
        if(slot==0x18) {
            if(Field<int>(sequencer.data(),0x9a8))throw std::runtime_error("RED unowned native cleanup wait");
            ++starts;return;
        }
    }
    throw std::runtime_error("unexpected virtual service");
}
bool EventComplete(void* p){return (Field<std::uint64_t>(p,8)&(1ull<<26))!=0;}
}
bool Sc6ReplayTaskGroup::ParallelTasks()const{return true;}
void Sc6ReplayTaskGroup::BeginWait(EventArray* array){
    ++waits;waiting_events_=array;pumping_=true;return_task_=reinterpret_cast<void*>(0x7770);
    if(Field<unsigned>(manager.data(),0x28)!=0 || Field<unsigned>(manager.data(),0x2c)!=2
        || Field<unsigned char>(sequencer.data(),0x9b4)!=1
        || Field<unsigned char>(sequencer.data(),0x9b5)!=1
        || Field<unsigned char>(sequencer.data(),0x9b6)!=1)throw std::runtime_error("pre-wait publication missing");
}
void Sc6ReplayTaskGroup::PumpOne(bool){
    ++pumps;
    // Controlled external return-task delivery, independent of event contents.
    // Production must still validate actual events before releasing their refs.
    if(deliver_return){pumping_=false;return_task_=nullptr;}
}
#define __fastfail Fail
#include "prior_cleanup_methods.inl"
struct World {
    enum class Phase {StartTasks,PriorCleanup,Group0,CollectionConsumers};
    Phase phase_=Phase::StartTasks;
    std::uintptr_t base_{};void* world_{};float delta_=0.5f;int tick_type_=3;
    bool run_tasks_=true,paused_{};int levels_{};
    Sc6ReplayTaskGroup task_groups_;
    void AdvanceStart(){switch(phase_){
#include "prior_cleanup_world.inl"
    default:break;}}
};
#undef __fastfail
}
int main(int argc,char** argv){
    using namespace Horse::Deterministic;
    auto* storage=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4400000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!storage)return 70;image=reinterpret_cast<std::uintptr_t>(storage);
    for(const auto rva:{0x43b2070u,0x43b2088u,0x43b20a0u})Field<int*>(storage,rva)=&option;
    Field<void*>(manager.data(),8)=sequencer.data();
    for(unsigned i=0;i<events.size();++i){event_refs[i]=events[i].data();Field<unsigned>(events[i].data(),0x48)=23;}
    Field<void*>(sequencer.data(),0x9a0)=event_refs.data();
    Field<int>(sequencer.data(),0x9a8)=6;Field<int>(sequencer.data(),0x9ac)=24;
    std::array<std::byte,0x800> world_data{};
    World world;world.base_=image;world.world_=world_data.data();
    const bool empty=argc>1 && argv[1][0]=='z';
    const bool ready=argc>1 && argv[1][0]=='r';
    if(empty)Field<int>(sequencer.data(),0x9a8)=0;
    if(ready)for(auto& event:events)Field<std::uint64_t>(event.data(),8)=1ull<<26;
    try {
        world.AdvanceStart();
        if(empty) {
            if(starts!=1 || waits || clears || prepares!=1 || world.phase_!=World::Phase::Group0)return 77;
            std::puts("empty prior cleanup preserves original native start PASS");return 0;
        }
        if(starts || waits!=1 || prepares!=1 || clears || world.phase_!=World::Phase::PriorCleanup)return 71;
        for(unsigned i=0;i<5;++i)Field<std::uint64_t>(events[i].data(),8)=1ull<<26;
        world.AdvanceStart();
        if(starts || clears || Field<int>(sequencer.data(),0x9a8)!=6)return 72;
        if(argc>1 && argv[1][0]=='i')deliver_return=true;
        else {
            Field<std::uint64_t>(events[5].data(),8)=1ull<<26;
            world.AdvanceStart(); // Completed events alone must not skip the return task.
            if(starts || clears || Field<int>(sequencer.data(),0x9a8)!=6)return 73;
            if(argc>1 && argv[1][0]=='m')Field<unsigned>(manager.data(),0x28)=3;
            if(argc>1 && argv[1][0]=='c')option=0;
            if(argc>1 && argv[1][0]=='a')Field<int>(sequencer.data(),0x9a8)=5;
            if(argc>1 && argv[1][0]=='e')event_refs[5]=event_refs[4];
            if(argc>1 && argv[1][0]=='f')Field<unsigned char>(sequencer.data(),0x9b5)=0;
            deliver_return=true;
        }
        world.AdvanceStart();
        if(argc>1 && !ready)return 74;
        if(starts!=1 || clears!=1 || waits!=1 || prepares!=1 || world.phase_!=World::Phase::Group0
            || !world.task_groups_.idle() || world.task_groups_.waiting_events_)return 75;
        for(const auto& event:events)if(*reinterpret_cast<const unsigned*>(event.data()+0x48)!=23)return 76;
        std::puts("owned prior-cleanup wait: preserved native publication, pending events, return-task order and one native array release PASS");
    } catch(const std::runtime_error& error) {
        if(argc>1 && !ready && !empty && !starts && !clears && waits==1){std::puts("incomplete completion/context/config/array invariant rejects before release PASS");return 0;}
        std::puts(error.what());return 81;
    }
    VirtualFree(storage,0,MEM_RELEASE);return 0;
}
