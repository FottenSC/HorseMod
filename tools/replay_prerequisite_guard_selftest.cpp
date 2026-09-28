// Extracted production reader/native-entry predicates. Indexed object lookup
// is an external service. This fixture does not install a hook, arm a guard,
// acquire a lifetime lease, or claim native/B recovery ownership.
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <span>
#include <map>
#include <cstdint>
#include <cstdio>
#define private public
#include "Sc6ReplayTaskGroup.hpp"
#undef private
#include "ReplayPrerequisiteConsumer.hpp"
#include "ReplayConsumerFailure.hpp"
namespace RC::Unreal {
struct Item {
    void* object{};int serial{};bool valid=true;
    void* GetUObject(){return object;} bool IsValid(bool){return valid;}
    int GetSerialNumber(){return serial;}
};
struct FUObjectArray {
    inline static std::map<int,Item> items;
    static Item* IndexToObject(int i){const auto it=items.find(i);return it==items.end()?nullptr:&it->second;}
};
}
namespace Horse::Deterministic {
struct NativeReplayTraceTaskGuard {
    struct Identity {std::uintptr_t object{};int index{},serial{};};
    struct Binding {Identity component,manager,chara,scene,world;};
    struct ExecutionObserver {void* context;bool(*before)(void*,unsigned,void*,void*,void*)noexcept;bool(*after)(void*,unsigned,void*,void*,void*)noexcept;};
    std::uintptr_t named{};
    std::uintptr_t named_thread()const{return named;}
    bool BindExecution(std::uintptr_t,ExecutionObserver){std::abort();}
    bool Stop(){std::abort();}
};
}
#define private public
#define check(expression) do { if(!(expression)) std::abort(); } while(false)
#include "prerequisite_guard_body.inl"
#undef check
#undef private
template<class T>void Put(void* p,std::size_t n,T value){*reinterpret_cast<T*>(static_cast<std::byte*>(p)+n)=value;}
int main(int argc,char** argv) {
    using namespace Horse::Deterministic;
    using P=ReplayPrerequisiteConsumer;using G=NativeReplayPrerequisiteGuard;
    auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 71;
    const auto game=reinterpret_cast<std::uintptr_t>(image);
    std::array<std::array<std::byte,0x1000>,6> objects{};
    std::array<std::byte,0x400> named{};
    std::array<std::byte,0x58> mesh_task{},source_task{},mesh_event{},source_event{};
    std::array<P::Identity,6> ids{};
    for(unsigned i=0;i<ids.size();++i) {
        ids[i]={reinterpret_cast<std::uintptr_t>(objects[i].data()),static_cast<int>(i),static_cast<int>(i+10)};
        RC::Unreal::FUObjectArray::items[i]={objects[i].data(),static_cast<int>(i+10),true};
    }
    auto* mesh=objects[0].data();auto* source=objects[1].data();auto* manager=objects[2].data();
    auto* chara=objects[3].data();auto* scene=objects[4].data();auto* world=objects[5].data();
    Put(mesh,0,game+0x38829c0);Put(image,0x38829c0+0x300,game+0x1dafc60);
    Put(source,0,game+0x3360ca8);Put(manager,0,game+0x3361f98);
    Put(chara,0,game+0x3268078);Put(scene,0,game+0x3360370);
    Put(source,0x190,manager);Put(source,0x1c8,world);Put(source,0x490,chara);
    Put(manager,0x3a8,source);Put(chara,0x458,manager);Put(chara,0x168,scene);Put(scene,0x190,chara);
    Put(named.data(),0x10,2u);Put(image,0x4197170,std::uint64_t{7});
    P::Edge edge{ids[1].index,ids[1].serial,ids[1].object+0x110};
    for(unsigned i=0;i<2;++i) {
        auto* tick=objects[i].data()+0x110;auto* task=i?source_task.data():mesh_task.data();
        Put(tick,0,game+0x3865f98);Put(tick,0x48,world);Put(tick,0x50,objects[i].data());
        Put(tick,0xc,std::uint8_t{0x40});Put(tick,0xd,std::uint8_t{1});Put(tick,0x10,7);Put(tick,0x14,7);
        Put(tick,0x18,task);Put(task,0,game+0x39cd9c0);Put(task,8,2u);Put(task,0xc,i?0:1);
        Put(task,0x10,tick);Put(task,0x24,2u);Put(task,0x28,world);Put(task,0x38,std::uint8_t{1});
        Put(task,0x40,i?source_event.data():mesh_event.data());
    }
    Put(mesh,0x130,&edge);Put(mesh,0x138,1);Put(mesh,0x13c,1);
    G guard;guard.base_=game;guard.thread_=GetCurrentThreadId();guard.guard_.named=reinterpret_cast<std::uintptr_t>(named.data());
    Sc6ReplayTaskGroup tasks;Sc6ReplayTaskGroup::ConsumerTask active{};active.task=source_task.data();
    tasks.executing_consumer_task_=&active;guard.tasks_=&tasks;
    const auto convert=[](auto id){return NativeReplayTraceTaskGuard::Identity{id.object,id.index,id.serial};};
    for(auto& root:guard.roots_)root={convert(ids[1]),convert(ids[2]),convert(ids[3]),convert(ids[4]),convert(ids[5])};
    P::Contract expected{};expected.owner=ids[0];expected.source=ids[1];expected.world=ids[5];
    expected.tick=ids[0].object+0x110;expected.level=ids[5].object;expected.tick_table=game+0x3865f98;
    expected.owner_table=game+0x38829c0;expected.consumer=game+0x1dafc60;expected.edges[0]=edge;expected.count=1;
    guard.contracts_[0]=expected;guard.count_=1;
    if(argc>1) {
        if(!ReplayConsumerFailure::Start(L"consumer_failure.json","prerequisite-fixture"))return 90;
        guard.armed_=true;
        if(argv[1][0]=='q') {guard.Hooks().pop_scope(&guard,true);return 91;}
        if(argv[1][0]=='l' || argv[1][0]=='z' || argv[1][0]=='f') {
            alignas(8) std::array<std::byte,0x48> nodes{};
            alignas(8) std::array<std::byte,0xa00> sequencer{};
            tasks.executing_consumer_task_=nullptr;tasks.sequencer_=sequencer.data();
            Put(named.data(),0x3e0,1);
            auto* queue=named.data()+0x38;
            const bool empty=argv[1][0]=='z';
            Put(queue,0x80,std::uint64_t{0x4000001});
            Put(queue,0x108,std::uint64_t{empty?0x8000001ull:0x8000002ull});
            Put(nodes.data(),0x18,std::uint64_t{empty?0ull:0x4000002ull});
            Put(nodes.data(),0x38,std::uintptr_t{0xdeadbeef}); // Never dereference the queued payload.
            Put(image,0x415dae8,argv[1][0]=='f'?std::uintptr_t{1}:reinterpret_cast<std::uintptr_t>(nodes.data()));
            Put(sequencer.data(),0x980,mesh_event.data());Put(sequencer.data(),0x988,source_event.data());
            Put(sequencer.data(),0x9a8,2);Put(sequencer.data(),0x9ac,4);
            Put(mesh_event.data(),8,std::uint64_t{1ull<<26});
            const auto saved_named=named;const auto saved_nodes=nodes;const auto saved_events=mesh_event;
            if(guard.Before(24,queue,nullptr,nullptr))return 110;
            guard.Failure(ReplayConsumerFailure::Site::NativeBefore,24,queue,nullptr,nullptr);
            if(named!=saved_named || nodes!=saved_nodes || mesh_event!=saved_events)return 111;
            std::puts("native queue rejected; all queue/node/event bytes unchanged; queued payload never dereferenced");
            std::fflush(stdout);
            __fastfail(FAST_FAIL_INVALID_ARG);
        }
        Sc6ReplayTaskGroup::ConsumerTask candidate{};
        candidate.task=mesh_task.data();candidate.function=mesh+0x110;
        Put(mesh_task.data(),0xc,0);
        // Native142163BA0 GT priority encoding observed in the retained tick211
        // failure; task+8 and task+24 are tokens, named-thread+10 is still 2.
        if(argv[1][0]=='e' || argv[1][0]=='h' || argv[1][0]=='m' || argv[1][0]=='a'
            || argv[1][0]=='n' || argv[1][0]=='b') {
            Put(mesh,0x11c,std::uint8_t{0x5e});
            Put(mesh_task.data(),8,0x202u);Put(mesh_task.data(),0x24,0x202u);
            if(argv[1][0]=='h'){Put(mesh_task.data(),8,0x402u);Put(mesh_task.data(),0x24,0x402u);}
            if(argv[1][0]=='m')Put(mesh_task.data(),0x24,2u);
            if(argv[1][0]=='a')Put(mesh,0x11c,std::uint8_t{0x4e});
            if(argv[1][0]=='n')Put(named.data(),0x10,0x202u);
            if(argv[1][0]=='b'){Put(mesh_task.data(),8,2u);Put(mesh_task.data(),0x24,2u);}
        }
        if(argv[1][0]=='r') {candidate.task=source_task.data();candidate.function=source+0x110;Put(source,0x460,source);}
        if(argv[1][0]=='s')RC::Unreal::FUObjectArray::items[0].serial++;
        if(argv[1][0]=='p')Put(mesh,0x124,6);
        const auto admission=guard.Admit(candidate);
        if(argv[1][0]=='v' || argv[1][0]=='e')return admission==Sc6ReplayTaskGroup::ConsumerAdmission::Forward?0:92;
        if(admission!=Sc6ReplayTaskGroup::ConsumerAdmission::Reject)return 93;
        // A later generic terminal marker must not overwrite the first reason.
        ReplayConsumerFailure::Record later{};later.site=ReplayConsumerFailure::Site::DispatchAdmission;
        ReplayConsumerFailure::Write(later);
        __fastfail(FAST_FAIL_INVALID_ARG);
    }
    P::Contract observed{};
    if(!guard.Observe(expected,observed)||observed!=expected)return 72;
    RC::Unreal::FUObjectArray::items[0].serial++;
    if(guard.Observe(expected,observed))return 73;
    RC::Unreal::FUObjectArray::items[0].serial--;
    Put(source,0x460,source);
    if(guard.Before(12,mesh+0x110,source,source+0x110))return 74;
    Put(source,0x460,static_cast<void*>(nullptr));
    Put(mesh,0x124,6); // stale post-link queue stamp
    if(guard.Before(12,mesh+0x110,source,source+0x110))return 75;
    Put(mesh,0x124,7);
    if(!guard.Before(12,mesh+0x110,source,source+0x110))return 76;
    // Simulated observed delta only; shipped-native removal is another fixture.
    Put(mesh,0x130,static_cast<void*>(nullptr));Put(mesh,0x138,0);Put(mesh,0x13c,0);
    if(!guard.After(12,mesh+0x110)||!guard.changed())return 77;
    if(guard.Before(12,mesh+0x110,source,source+0x110))return 78;
    if(guard.Before(23,mesh,nullptr,nullptr))return 79;
    if(guard.Before(24,named.data()+0x38,nullptr,nullptr))return 80;
    if(!guard.Before(24,world,nullptr,nullptr))return 81;
    VirtualFree(image,0,MEM_RELEASE);
    std::puts("production prerequisite entry/read predicates PASS; no native ownership/recovery claim");return 0;
}
