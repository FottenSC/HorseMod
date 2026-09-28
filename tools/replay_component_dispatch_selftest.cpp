// Production scheduler predicates, PumpOne and DispatchTask. Native queue and
// task entry are external-service probes, NOT lifetime/cancellation emulation.
#include "ReplayConsumerFailure.hpp"
#include "ReplayPhysicsStepConsumer.hpp"
#include "ReplayVfxExecutionScope.hpp"
#ifdef REPLAY_COMPONENT_COLLISION_BOUNDARY
#include "ReplayGroundBodyAdmission.hpp"
#include "ReplayPhysicsBodyInventory.hpp"
#include "ReplayPhysicsNotifications.hpp"
#include "ReplayPhysicsRemovalGuard.hpp"
#endif
#include <algorithm>
static_assert(std::min(1,2)==1); // Header must be safe before other Windows includes.
#define main SchedulerAdmissionBaseline
#include "replay_scheduler_consumer_selftest.cpp"
#undef main
#include <intrin.h>
#include <stdexcept>
#include <type_traits>
#include <cstring>
#include <thread>
#define private public
#include "Sc6ReplayTaskGroup.hpp"
#undef private
namespace Horse::Deterministic {
class Sc6ReplayExecutor {
public:
    bool interval_complete() const { throw std::logic_error("unexpected manager"); }
    bool tail_blocked() const { throw std::logic_error("unexpected manager"); }
};
namespace {
void* queued_task{};
unsigned native_entries{}, changed_entries{}, pops{};
std::uintptr_t image_base{};
Sc6ReplayTaskGroup* nested_group{};
bool nested_rejected{};
void ProbeNestedPump();
template<class T>T& Field(void* p,std::size_t n) {
    return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);
}
template<class R=void,class... A>R Native(std::uintptr_t,std::uintptr_t rva,A...) {
    if constexpr(std::is_same_v<R,void*>) {
        if(rva!=0xd2b9f0)throw std::logic_error("unexpected queue service");
        ++pops;auto* task=queued_task;queued_task=nullptr;return task;
    } else { throw std::logic_error("unexpected native service"); }
}
template<class R=void,class... A>R Virtual(void* task,std::size_t slot,A...) {
    if constexpr(std::is_same_v<R,void>) {
        const auto type=Field<std::uintptr_t>(task,0);
        if(slot!=8 || (type!=image_base+0x39cd9c0 && type!=image_base+0x3657428 && type!=image_base+0x36bc160))
            throw std::logic_error("unexpected native entry");
        ++native_entries;
        if(type!=image_base+0x39cd9c0)return;
        if(nested_group)ProbeNestedPump();
        auto* tick=Field<void*>(task,0x10);
        auto* owner=Field<void*>(tick,0x50);
        if(Field<std::uintptr_t>(tick,0)==image_base+0x3865f98
            && Field<std::uintptr_t>(Field<void*>(owner,0),0x300)==image_base+0x1bcdb90)
            ++changed_entries;
        // Intentionally no completion, refcount or pool emulation: reaching
        // this native entry is the failure under test, before any such work.
    } else { throw std::logic_error("unexpected virtual service"); }
}
#include "component_pump_depth.inl"
}
void Sc6ReplayTaskGroup::ResumeManagerTask(){throw std::logic_error("unexpected resume");}
void Sc6ReplayTaskGroup::ValidatePendingTask(){throw std::logic_error("unexpected manager validation");}
void Sc6ReplayTaskGroup::FinishManagerTask(){throw std::logic_error("unexpected finish");}
struct TerminalRejection { unsigned code; };
[[noreturn]] void RecordTerminalRejection(unsigned code) { throw TerminalRejection{code}; }
#ifndef REPLAY_COMPONENT_NATIVE_FASTFAIL
#define __fastfail RecordTerminalRejection
#endif
#include "component_pump.inl"
#include "component_consumer_request.inl"
#include "component_consumer_resume.inl"
#include "component_dispatch.inl"
#ifndef REPLAY_COMPONENT_NATIVE_FASTFAIL
#undef __fastfail
#endif
#ifdef REPLAY_COMPONENT_COLLISION_BOUNDARY
#include "component_collision_shader.inl"
#include "component_collision_domain.inl"
namespace {
unsigned collision_locks{},collision_unlocks{},body_reads{};
void CollisionLock(void*,const char*,unsigned) {++collision_locks;}
void CollisionUnlock(void*) {++collision_unlocks;}
void* ReadCollisionBody(void* values,const void* actor) {
    ++body_reads;
    std::memcpy(values,static_cast<const std::byte*>(actor)+0x100,0xe8);
    return values;
}
}
int CheckChangedCollisionConsumer(bool inventory_only=false) {
    // Controlled native services: scene lock, public body-value copy and queue
    // dispatch. Admission itself is the unmodified production collision/update
    // predicate. No task lease, native completion or B recovery is supplied.
    auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4500000,MEM_RESERVE,PAGE_NOACCESS));
    if(!image)return 130;
    struct ImageOwner {void* p;~ImageOwner(){VirtualFree(p,0,MEM_RELEASE);}} image_owner{image};
    image_base=reinterpret_cast<std::uintptr_t>(image);
    for(const auto page:{0x7000u,0x43000u,0x19d000u,0x204c000u,0x204d000u,0x39ef000u})
        if(!VirtualAlloc(image+page,0x1000,MEM_COMMIT,PAGE_READWRITE))return 131;
    const auto trampoline=[&](unsigned rva,std::uintptr_t target) {
        const std::array<unsigned char,2> prefix{0x48,0xb8},suffix{0xff,0xe0};
        std::memcpy(image+rva,prefix.data(),2);std::memcpy(image+rva+2,&target,8);
        std::memcpy(image+rva+10,suffix.data(),2);
    };
    trampoline(0x7810,reinterpret_cast<std::uintptr_t>(&ReadCollisionBody));
    trampoline(0x431d0,reinterpret_cast<std::uintptr_t>(&CollisionLock));
    trampoline(0x432b0,reinterpret_cast<std::uintptr_t>(&CollisionUnlock));
    for(const auto page:{0x7000u,0x43000u}) {
        DWORD old{};
        if(!VirtualProtect(image+page,0x1000,PAGE_EXECUTE_READ,&old)
            || !FlushInstructionCache(GetCurrentProcess(),image+page,0x1000))return 132;
    }
    std::memcpy(image+0x204cf60,collision_shader,sizeof(collision_shader));
    Field<std::uintptr_t>(image+0x19d008,0x330)=image_base+0x431d0;
    Field<std::uintptr_t>(image+0x19d008,0x338)=image_base+0x432b0;
    Field<std::uintptr_t>(image+0x39efa78,8)=image_base+0x2018570;
    alignas(16) std::array<std::byte,0x200> world{},physics{},actor{};
    alignas(16) std::array<std::byte,0x20> user{};
    alignas(16) std::array<std::byte,0x200> body{};
    alignas(16) std::array<std::byte,0x600> component{};
    alignas(16) std::array<std::byte,0x2600> scene{};
    alignas(16) std::array<std::byte,0x300> aabb{};
    alignas(16) std::array<std::byte,0x400> thread{};
    alignas(16) std::array<std::byte,0x60> tick{};
    alignas(16) std::array<std::byte,0x58> task{},changed_task{};
    alignas(16) std::array<std::byte,0x50> event{};
    std::array<std::uintptr_t,2> scene_owner{3,reinterpret_cast<std::uintptr_t>(physics.data())};
    std::uintptr_t actors=reinterpret_cast<std::uintptr_t>(actor.data()),sap=image_base+0x1ace30,volumes{};
    Field<void*>(world.data(),0x1c8)=physics.data();
    Field<unsigned>(physics.data(),4)=1;Field<short>(physics.data(),0xf8)=7;
    Field<std::uintptr_t>(scene.data(),0)=image_base+0x19d008;
    Field<void*>(scene.data(),8)=scene_owner.data();
    Field<void*>(scene.data(),0x738)=aabb.data();
    Field<std::uintptr_t>(scene.data(),0x1080)=image_base+0x204cf60;
    Field<void*>(scene.data(),0x2590)=&actors;Field<unsigned>(scene.data(),0x2598)=1;
    Field<void*>(aabb.data(),0xa8)=&volumes;Field<void*>(aabb.data(),0x100)=&sap;
    Field<unsigned>(aabb.data(),0x1cc)=0xffffffffu;
    Field<std::uintptr_t>(actor.data(),0)=image_base+0x19b7c0;
    Field<unsigned char>(actor.data(),0x100+0x9c)=1; // Unrelated kinematic body.
    Field<unsigned>(user.data(),0)=1;
    Field<void*>(user.data(),8)=body.data();
    Field<void*>(actor.data(),0x100+0x20)=user.data();
    Field<unsigned>(thread.data(),0x10)=2;
    Field<std::uintptr_t>(tick.data(),0)=image_base+0x39efa78;
    Field<void*>(tick.data(),0x50)=world.data();
    Field<std::uintptr_t>(task.data(),0)=image_base+0x39cd9c0;
    Field<void*>(task.data(),0x10)=tick.data();Field<void*>(task.data(),0x28)=world.data();
    Field<void*>(task.data(),0x40)=event.data();Field<unsigned char>(task.data(),0x38)=1;
    Field<void*>(tick.data(),0x18)=task.data();
    const auto collision_admit=[&](const char*& check,std::uintptr_t& rejected,
        ReplayPhysicsBodyInventory* inventory) {
        GroundCollisionFixture domain;domain.base=image_base;
        domain.world=reinterpret_cast<std::uintptr_t>(world.data());
        const auto accepted=domain.collision_update_domain(image_base,{},check,rejected,
            [&](short id,std::uintptr_t& out){out=reinterpret_cast<std::uintptr_t>(scene.data());return id==7;},
            [&](std::uintptr_t key){return key==reinterpret_cast<std::uintptr_t>(body.data())+0x138
                ?reinterpret_cast<std::uintptr_t>(component.data()):std::uintptr_t{};});
        if(inventory)*inventory=domain.body_inventory;
        return accepted;
    };
    const char* check="none";std::uintptr_t rejected{};
    ReplayPhysicsBodyInventory before_inventory{},after_inventory{};
    ReplayPhysicsStepConsumer::Diagnostic diagnostic;
    if(!ReplayPhysicsStepConsumer::Admit(image_base,reinterpret_cast<std::uintptr_t>(physics.data()),
        [](auto p,auto& value){return GroundCollisionFixture::read(p,value);},diagnostic)
        || !collision_admit(check,rejected,&before_inventory) || collision_locks!=1 || collision_unlocks!=1 || body_reads!=1) {
        std::printf("collision entry setup failed check=%s\n",check);return 133;
    }
    // This is the collision branch used at application entry, including the
    // zero-debris/possible-first-birth case. Full host admission is not modeled.
    Sc6ReplayTaskGroup group;group.base_=image_base;group.named_thread_=thread.data();group.pumping_=true;
    queued_task=task.data();group.PumpOne(true);
    if(native_entries!=1 || group.dispatched_tasks()!=1 || pops!=1)return 134;
    Field<unsigned char>(actor.data(),0x100+0x9c)=0; // Same actor becomes free before consumption.
    const auto actor_before=actor;const auto scene_before=scene;
    if(collision_admit(check,rejected,&after_inventory) || std::strcmp(check,"body_unrelated_dynamic")
        || rejected!=actors || actor!=actor_before || scene!=scene_before
        || collision_locks!=2 || collision_unlocks!=2 || body_reads!=2)return 135;
    if(inventory_only) {
        const auto delta=before_inventory.FirstDifference(after_inventory);
        if(before_inventory.count!=1 || after_inventory.count!=1 || !before_inventory.scan_complete
            || !after_inventory.scan_complete || delta.kind!=ReplayPhysicsBodyInventory::Difference::Changed
            || delta.before.actor!=actors || delta.after.actor!=actors
            || !(delta.before.body_flags&1) || (delta.after.body_flags&1))return 139;
        Field<unsigned char>(actor.data(),0x100+0x9c)=1;
        alignas(16) auto second_actor=actor;
        const auto second_address=reinterpret_cast<std::uintptr_t>(second_actor.data());
        std::array<std::uintptr_t,2> actor_table{actors,second_address};
        Field<void*>(scene.data(),0x2590)=actor_table.data();Field<unsigned>(scene.data(),0x2598)=2;
        ReplayPhysicsBodyInventory with_birth{};
        if(!collision_admit(check,rejected,&with_birth) || with_birth.count!=2 || !with_birth.scan_complete
            || before_inventory.FirstDifference(with_birth).kind!=ReplayPhysicsBodyInventory::Difference::Added)
            return 140;
        actor_table[0]=second_address;actor_table[1]=actors;
        ReplayPhysicsBodyInventory moved_slot{};
        if(!collision_admit(check,rejected,&moved_slot)
            || before_inventory.FirstDifference(moved_slot).kind!=ReplayPhysicsBodyInventory::Difference::Changed)
            return 144;
        actor_table[0]=second_address;Field<unsigned>(scene.data(),0x2598)=1;
        ReplayPhysicsBodyInventory after_death{};
        if(!collision_admit(check,rejected,&after_death) || after_death.count!=1 || !after_death.scan_complete
            || with_birth.FirstDifference(after_death).kind!=ReplayPhysicsBodyInventory::Difference::Removed)
            return 141;
        GroundCollisionFixture::component_serial=18; // Same pointers; different indexed owner generation.
        ReplayPhysicsBodyInventory reused_component{};
        if(!collision_admit(check,rejected,&reused_component)
            || after_death.FirstDifference(reused_component).kind!=ReplayPhysicsBodyInventory::Difference::Changed)
            return 142;
        std::puts("complete scene inventory detects changed, added and removed unrelated bodies");return 0;
    }
    changed_task=task;Field<void*>(tick.data(),0x18)=changed_task.data();
    const auto task_before=changed_task;const auto event_before=event;
    queued_task=changed_task.data();
    std::puts("entry collision admission=accepted; same unrelated body changed kinematic->free; current collision predicate=body_unrelated_dynamic");
    try {group.PumpOne(true);}
    catch(const TerminalRejection&) {
        std::puts("G1 RED: collision rejection is terminal containment, not recoverable cancellation");return 137;
    }
    if(native_entries!=1) {
        std::printf("G1 RED: changed unrelated free body reached PhysX start wrapper; native_entries=%u completed_tasks=%llu collision_checks=2\n",
            native_entries,static_cast<unsigned long long>(group.dispatched_tasks()));return 136;
    }
    if(group.dispatched_tasks()!=1 || changed_task!=task_before || event!=event_before
        || group.terminal_task_ || !group.held_consumer_task()
        || group.held_consumer_task()->task!=changed_task.data())return 138;
    // Retaining the popped task is only a before-effects prerequisite. It is
    // not graph/scene/application settlement and cannot certify complete B.
    std::puts("G0 RED: collision consumer held before native entry; native settlement and complete B unproven");return 143;
}
#endif
namespace {
void ProbeNestedPump() {
    auto* group=nested_group;nested_group=nullptr;
    try {group->PumpOne(true);}
    catch(const TerminalRejection&) {nested_rejected=true;}
}
}
// Dispatcher-interface checks only. The external hook below deliberately does
// not purport to establish native UObject/event ownership; shipped-native
// protection and lifecycle must be demonstrated separately before host binding.
int CheckRetainedDispatcher(std::byte* thread, std::byte* task, std::byte* tick, void* world) {
    Sc6ReplayTaskGroup group;group.base_=image_base;group.named_thread_=thread;group.pumping_=true;
    Field<unsigned>(thread,0x10)=2;
    Field<void*>(task,0x28)=world;Field<void*>(task,0x40)=world;
    Field<void*>(tick,0x18)=task;Field<unsigned char>(task,0x38)=1;
    bool valid=true;unsigned completions{};
    struct Context {bool* valid;unsigned* completions;} context{&valid,&completions};
    group.consumer_hooks_.context=&context;
    group.consumer_hooks_.acquire=[](void*,Sc6ReplayTaskGroup::ConsumerTask& t) noexcept {
        t.owner=reinterpret_cast<std::uintptr_t>(t.function)-0x110;
        t.owner_index=1;t.owner_generation=2;t.application_epoch=3;t.contract=4;
        return Sc6ReplayTaskGroup::ConsumerAdmission::Hold;
    };
    group.consumer_hooks_.validate=[](void* c,const auto&) noexcept {return *static_cast<Context*>(c)->valid;};
    group.consumer_hooks_.begin_execution=group.consumer_hooks_.validate;
    group.consumer_hooks_.completed=[](void* c,const auto&) noexcept {++*static_cast<Context*>(c)->completions;};
    const auto entries=native_entries, popped=pops;
    queued_task=task;group.PumpOne(true);
    if(!group.held_consumer_task() || group.dispatched_tasks() || native_entries!=entries || pops!=popped+1)return 80;
    queued_task=task; // Later work must remain queued even through failed resume.
    for(unsigned i=0;i<20;++i)group.PumpOne(true);
    valid=false;
    if(group.RequestConsumerResume())return 81;
    group.PumpOne(true);
    if(pops!=popped+1 || native_entries!=entries || completions || !group.held_consumer_task())return 82;
    valid=true;
    if(!group.RequestConsumerResume() || !group.RequestConsumerResume())return 83;
    nested_group=&group;group.PumpOne(true);
    queued_task=nullptr;
    if(!nested_rejected || native_entries!=entries+1 || completions!=1 || group.held_consumer_task()
        || group.dispatched_tasks()!=1 || Field<int>(thread,0x3e0)!=0)return 84;
    if(group.RequestConsumerResume())return 85;
    std::puts("retained dispatcher: repeated polls, invalid resume, exactly-once nested-entry exclusion PASS; interface only");
    return 0;
}

}
#include "ReplayPrerequisiteConsumer.hpp"
static int CheckPrerequisiteProtocol() {
    using P=Horse::Deterministic::ReplayPrerequisiteConsumer;
    P::Contract c{};
    c.owner={0x1000,1,2};c.source={0x2000,2,3};c.world={0x3000,3,4};
    c.tick=0x1110;c.level=0x4000;c.tick_table=0x5000;c.owner_table=0x6000;c.consumer=0x7000;
    c.count=1;c.edges[0]={2,3,0x2110};
    P::Task mesh{};mesh.task=0x8000;mesh.tick=c.tick;mesh.event=0x9000;mesh.world=0x3000;
    mesh.table=0xa000;mesh.epoch=7;mesh.admitted=mesh.queued=7;mesh.dependencies=1;
    mesh.os_thread=10;mesh.native_thread=mesh.desired_thread=mesh.payload_thread=2;
    mesh.flags=0x40;mesh.enabled=mesh.constructed=1;
    auto source=mesh;source.task=0xb000;source.tick=0x2110;source.event=0xc000;source.dependencies=0;
    auto removed=c;removed.count=0;removed.edges={};auto ready=mesh;ready.dependencies=0;
    P protocol;
    if(protocol.BeforeDispatch(c,removed,ready,0xa000,10)!=P::Admission::Reject)return 90;
    if(!protocol.BeforeRemove(c,c,mesh,source,source.task,0xa000,10))return 91;
    if(protocol.BeforeDispatch(c,c,ready,0xa000,10)!=P::Admission::Reject)return 92;
    auto bad=source;bad.event_head=1ull<<26;
    if(protocol.AfterRemove(removed,mesh,bad)||!protocol.AfterRemove(removed,mesh,source))return 93;
    if(protocol.BeforeDispatch(c,removed,ready,0xa000,11)!=P::Admission::Reject)return 94;
    if(protocol.BeforeDispatch(c,removed,ready,0xa000,10)!=P::Admission::DrainCurrentApplication)return 95;
    if(protocol.Completed(ready.task,8)||!protocol.Completed(ready.task,7)||protocol.Completed(ready.task,7))return 96;
    if(protocol.BeforeDispatch(c,removed,ready,0xa000,10)!=P::Admission::Reject)return 97;
    for(unsigned mutation=0;mutation<9;++mutation) {
        P rejected;auto contract=c;auto task=mesh;auto active=source.task;
        switch(mutation) {
            case 0:contract.policy.version++;break;
            case 1:contract.source.serial++;break;
            case 2:task.queued--;break;
            case 3:task.desired_thread=0xff;break;
            case 4:task.payload_thread=0x102;break;
            case 5:task.event_head=1ull<<26;break;
            case 6:task.dependencies=0;break;
            case 7:task.epoch++;break;
            case 8:active++;break;
        }
        if(rejected.BeforeRemove(contract,contract,task,source,active,0xa000,10))return 98;
    }
    // Independently observed native GT tokens, including mixed source/mesh
    // priorities. Exercise production mutation and dispatch boundaries; this
    // value protocol does not establish native ownership or task completion.
    for(unsigned mesh_priority=0;mesh_priority<2;++mesh_priority)
    for(unsigned source_priority=0;source_priority<2;++source_priority) {
        auto selected=mesh,producer=source;
        selected.flags=mesh_priority?0x5e:0x4e;
        producer.flags=source_priority?0x5e:0x4e;
        selected.desired_thread=selected.payload_thread=mesh_priority?0x202:2;
        producer.desired_thread=producer.payload_thread=source_priority?0x202:2;
        P observed;
        if(!observed.BeforeRemove(c,c,selected,producer,producer.task,0xa000,10)
            || !observed.AfterRemove(removed,selected,producer))return 105;
        auto released=selected;released.dependencies=0;
        if(observed.BeforeDispatch(c,removed,released,0xa000,10)!=P::Admission::DrainCurrentApplication
            || !observed.Completed(released.task,released.epoch))return 106;
        for(unsigned mutation=0;mutation<7;++mutation) {
            auto invalid=selected;
            switch(mutation) {
                case 0:invalid.desired_thread=invalid.payload_thread=0x402;break;
                case 1:invalid.desired_thread^=0x200;break;
                case 2:invalid.payload_thread^=0x200;break;
                case 3:invalid.flags^=0x10;break;
                case 4:invalid.native_thread=0x202;break;
                case 5:invalid.desired_thread=invalid.payload_thread=0x102;break;
                case 6:invalid.desired_thread=invalid.payload_thread=0xffffffff;break;
            }
            P rejected;
            if(rejected.BeforeRemove(c,c,invalid,producer,producer.task,0xa000,10))return 107;
            invalid.dependencies=0;
            if(rejected.BeforeDispatch(c,c,invalid,0xa000,10)!=P::Admission::Reject)return 108;
        }
    }
    std::puts("bounded prerequisite value protocol PASS; native ownership is a separate fixture");
    return 0;
}

int CheckObservedDispatcher(std::byte* thread,std::byte* task,std::byte* tick,void* world) {
    using namespace Horse::Deterministic;
    Sc6ReplayTaskGroup group;group.base_=image_base;group.named_thread_=thread;group.pumping_=true;
    Field<unsigned>(thread,0x10)=2;Field<void*>(task,0x28)=world;Field<void*>(task,0x40)=world;
    struct Context {unsigned scopes{},closed{},completed{};bool in_pop{};Sc6ReplayTaskGroup* group;} state{0,0,0,false,&group};
    group.consumer_hooks_.context=&state;
    group.consumer_hooks_.pop_scope=[](void* p,bool enter) noexcept {
        auto& s=*static_cast<Context*>(p);
        if(s.in_pop==enter || s.group->executing_consumer_task())std::abort();
        s.in_pop=enter;if(enter)++s.scopes;else ++s.closed;
    };
    group.consumer_hooks_.acquire=[](void* p,Sc6ReplayTaskGroup::ConsumerTask& t) noexcept {
        if(static_cast<Context*>(p)->in_pop)std::abort();
        t.owner=reinterpret_cast<std::uintptr_t>(t.function)-0x110;
        t.owner_index=1;t.owner_generation=2;t.application_epoch=3;t.contract=4;
        return Sc6ReplayTaskGroup::ConsumerAdmission::ForwardObserved;
    };
    group.consumer_hooks_.completed=[](void* p,const auto& t) noexcept {
        auto& s=*static_cast<Context*>(p);
        if(s.in_pop || s.group->executing_consumer_task() || !t.task || t.application_epoch!=3)std::abort();
        ++s.completed;
    };
    const auto entries=native_entries;
    queued_task=task;group.PumpOne(true);
    if(state.scopes!=1 || state.closed!=1 || state.in_pop || state.completed!=1
        || native_entries!=entries+1 || group.dispatched_tasks()!=1 || group.executing_consumer_task())return 86;
    std::puts("observed dispatcher: queue scope closes before native entry; one post-native completion PASS");
    return 0;
}
int main(int argc,char** argv) {
    if(argc>1 && std::strcmp(argv[1],"scheduler")==0)return SchedulerAdmissionBaseline();
    if(!Horse::Deterministic::ReplayConsumerFailure::Start(L"consumer_failure.json","dispatch-fixture"))return 100;
#ifdef REPLAY_COMPONENT_COLLISION_BOUNDARY
    if(argc>1 && std::strcmp(argv[1],"collision-inventory-change")==0)
        return Horse::Deterministic::CheckChangedCollisionConsumer(true);
    if(argc>1 && std::strcmp(argv[1],"collision-changed-free-body")==0)
        return Horse::Deterministic::CheckChangedCollisionConsumer();
#endif
    if(const auto result=CheckPrerequisiteProtocol())return result;
    using namespace Horse::Deterministic;
    auto* image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4500000,MEM_RESERVE,PAGE_NOACCESS));
    if(!image)return 73;
    struct ImageOwner {void* p;~ImageOwner(){VirtualFree(p,0,MEM_RELEASE);}} image_owner{image};
    if(!VirtualAlloc(image+0x3865000,0x1000,MEM_COMMIT,PAGE_READWRITE))return 74;
    image_base=reinterpret_cast<std::uintptr_t>(image);
    *reinterpret_cast<std::uintptr_t*>(image+0x3865f98+8)=image_base+0x1d43230;
    alignas(16) std::array<std::byte,0x900> component{},world{};
    alignas(16) std::array<std::byte,0x180> level{};
    alignas(16) std::array<std::byte,0x400> table{},thread{};
    alignas(16) std::array<std::byte,0x58> task{},changed_task{};
    const auto owner=reinterpret_cast<std::uintptr_t>(component.data());
    const auto w=reinterpret_cast<std::uintptr_t>(world.data());
    const auto l=reinterpret_cast<std::uintptr_t>(level.data());
    Store(component,0,reinterpret_cast<std::uintptr_t>(table.data()));
    Store(table,0x300,image_base+0x1d652a0);
    Store(component,0x110,image_base+0x3865f98);
    Store(component,0x158,l);Store(component,0x160,owner);Store(world,0x778,l);
    Sc6ReplaySchedulerState admission;admission.base_=image_base;
    admission.world_.object=w;admission.levels_.push_back({l,{w,{}}});
    if(!admission.CaptureTick(owner+0x110,l,100000).ok()
        || !admission.ValidateBindings(image_base,world.data()).ok())return 71;
    // Identity/GC and full host application admission are NOT proven here.
    Store(task,0,image_base+0x39cd9c0);
    Store(task,0x10,component.data()+0x110);
    Sc6ReplayTaskGroup group;group.base_=image_base;
    group.named_thread_=thread.data();group.pumping_=true;
    queued_task=task.data();group.PumpOne(true);
    if(native_entries!=1 || changed_entries || pops!=1
        || group.dispatched_tasks()!=1)return 72;
    WIN32_FILE_ATTRIBUTE_DATA file_state{};
    if(!GetFileAttributesExW(L"consumer_failure.json",GetFileExInfoStandard,&file_state)
        || file_state.nFileSizeLow || file_state.nFileSizeHigh)return 101;
    if(argc>1) {
        if(std::strncmp(argv[1],"substep-",8)==0) {
            // These probes expose native wrapper entry only. They do not
            // emulate its delegate dispatch, event completion or refcounts.
            const bool repeat=std::strstr(argv[1],"repeat")!=nullptr;
            const bool null_scene=std::strstr(argv[1],"null")!=nullptr;
            const bool final=std::strstr(argv[1],"final")!=nullptr;
            const bool unrelated=std::strstr(argv[1],"unrelated")!=nullptr;
            const bool empty=std::strstr(argv[1],"empty")!=nullptr;
            const bool invoke_changed=std::strstr(argv[1],"invoke")!=nullptr;
            const auto task_rva=repeat?0x36bc160u:0x3657428u;
            const auto callable_rva=repeat?0x3510a68u:0x39767c0u;
            for(const auto rva:{task_rva,callable_rva})
                if(!VirtualAlloc(image+(rva&~0xfffu),0x1000,MEM_COMMIT,PAGE_READWRITE))return 120;
            Field<std::uintptr_t>(image+task_rva,8)=image_base+(repeat?0x15ecb80:0x14b0ff0);
            Field<std::uintptr_t>(image+callable_rva,0x38)=image_base+0x2d72f0;
            Field<std::uintptr_t>(image+callable_rva,0x68)=image_base+(repeat?0x2017dc0:0x2017de0);
            alignas(16) std::array<std::byte,0x80> delegate_task{},event{};
            alignas(16) std::array<std::byte,0xf0> context{};
            alignas(16) std::array<std::byte,0x400> scene{};
            alignas(16) std::array<std::byte,0x30> callable{};
            Field<std::uintptr_t>(delegate_task.data(),0)=image_base+task_rva;
            Field<void*>(delegate_task.data(),0x30)=callable.data();
            Field<int>(delegate_task.data(),0x40)=3;
            Field<unsigned>(delegate_task.data(),8)=Field<unsigned>(delegate_task.data(),0x50)=repeat?0x202:2;
            Field<void*>(delegate_task.data(),0x68)=event.data();
            Field<std::uintptr_t>(callable.data(),0)=image_base+callable_rva;
            Field<void*>(callable.data(),8)=context.data();
            Field<std::uintptr_t>(callable.data(),0x10)=image_base+(unrelated?0x123456:repeat?0x2055bc0:0x20555e0);
            Field<std::uint64_t>(callable.data(),0x20)=42;
            Field<unsigned>(context.data(),0xa0)=2;
            Field<unsigned>(context.data(),0xc4)=final?0xffffffffu:1;
            Field<void*>(context.data(),0xb0)=event.data();
            Field<void*>(context.data(),0xc8)=event.data();
            Field<void*>(context.data(),0xd0)=null_scene?nullptr:scene.data();
            Field<void*>(scene.data(),0x370)=context.data();
            queued_task=delegate_task.data();group.PumpOne(true);
            if(native_entries!=2 || group.dispatched_tasks()!=2)return 121;
            Field<int>(scene.data(),0xd0)=1; // New unsupported substep callback.
            if(empty)Field<int>(delegate_task.data(),0x40)=0;
            if(invoke_changed)Field<std::uintptr_t>(image+callable_rva,0x68)=image_base+0x123456;
            const auto task_before=delegate_task;
            const auto context_before=context;
            const auto event_before=event;
            queued_task=delegate_task.data();
            std::puts("changed substep callback queued; unchanged native entry count=2");std::fflush(stdout);
            try {group.PumpOne(true);}
            catch(const TerminalRejection&) {
                if(null_scene || final || unrelated || empty || native_entries!=2 || group.dispatched_tasks()!=2
                    || delegate_task!=task_before || context!=context_before || event!=event_before
                    || group.terminal_task_!=delegate_task.data())return 122;
                std::puts("substep callback rejected before native wrapper; task/context/event unchanged; NOT recovery");return 0;
            }
            if(null_scene || final || unrelated || empty)return native_entries==3 && group.dispatched_tasks()==3?0:123;
            std::puts("G1 RED: changed substep callback reached native wrapper");return 124;
        }
        if(argv[1][0]=='p') {
            // Actual PhysX start task (not the two Physics2D tick tables).
            // External native wrapper service only counts entry; no native
            // callback, event completion or owner lifetime is granted here.
            if(!VirtualAlloc(image+0x39ef000,0x1000,MEM_COMMIT,PAGE_READWRITE))return 110;
            Field<std::uintptr_t>(image+0x39efa78,8)=image_base+0x2018570;
            alignas(16) std::array<std::byte,0x60> physics_tick{};
            alignas(16) std::array<std::byte,0x100> scene{};
            Field<std::uintptr_t>(physics_tick.data(),0)=image_base+0x39efa78;
            Field<void*>(physics_tick.data(),0x50)=world.data();
            Field<void*>(task.data(),0x10)=physics_tick.data();
            Field<void*>(world.data(),0x1c8)=scene.data();
            const bool null_scene=std::strcmp(argv[1],"physics-null")==0;
            const bool vehicle_mode=std::strcmp(argv[1],"physics-vehicle")==0
                || std::strcmp(argv[1],"physics-active-vehicle")==0;
            alignas(16) std::array<std::byte,0x80> vehicle{},rows{};
            alignas(16) std::array<std::array<std::byte,0x30>,2> callables{};
            if(null_scene)Field<void*>(world.data(),0x1c8)=nullptr;
            if(vehicle_mode) {
                if(!VirtualAlloc(image+0x40e9000,0x1000,MEM_COMMIT,PAGE_READWRITE)
                    || !VirtualAlloc(image+0x3510000,0x1000,MEM_COMMIT,PAGE_READWRITE))return 114;
                auto* map=image+0x40e90a0;
                Field<void*>(map,0)=rows.data();Field<int>(map,8)=Field<int>(map,0xc)=1;
                Field<unsigned>(map,0x10)=1;Field<int>(map,0x28)=1;Field<int>(map,0x2c)=128;
                Field<void*>(rows.data(),0)=scene.data();Field<void*>(rows.data(),8)=vehicle.data();
                Field<std::uintptr_t>(image+0x3510a68,0x68)=image_base+0x2017dc0;
                for(unsigned side=0;side<2;++side) {
                    auto* collection=scene.data()+(side?0x80:0x10);auto* callable=callables[side].data();
                    Field<void*>(collection,0x20)=callable;Field<int>(collection,0x30)=3;Field<int>(collection,0x50)=1;
                    Field<std::uintptr_t>(callable,0)=image_base+0x3510a68;
                    Field<void*>(callable,8)=vehicle.data();
                    Field<std::uintptr_t>(callable,0x10)=image_base+(side?0x30fe7a0:0x30fc2b0);
                    Field<std::uint64_t>(callable,0x20)=Field<std::uint64_t>(vehicle.data(),0x60+side*8)=101+side;
                }
            }
            queued_task=task.data();group.PumpOne(true);
            if(native_entries!=2 || group.dispatched_tasks()!=2)return 111;
            if(null_scene || std::strcmp(argv[1],"physics-vehicle")==0)return 0;
            // A new unknown step callback appears after entry admission. The
            // existing native wrapper must not publish physics work first.
            if(vehicle_mode)Field<int>(vehicle.data(),0x10)=1;
            else Field<int>(scene.data(),0x60)=1;
            const auto task_before=task;
            const auto scene_before=scene;
            queued_task=task.data();
            std::puts("changed physics callback queued; unchanged native entry count=2");std::fflush(stdout);
            try{group.PumpOne(true);}
            catch(const TerminalRejection&) {
                if(native_entries!=2 || group.dispatched_tasks()!=2 || task!=task_before || scene!=scene_before
                    || group.terminal_task_!=task.data())return 112;
                std::puts("physics callback rejected before native wrapper; task/scene unchanged; NOT recovery");return 0;
            }
            std::puts("G1 RED: changed physics callback reached native wrapper");return 113;
        }
        if(argv[1][0]=='v')return 0;
        if(argv[1][0]=='c') {
            std::atomic<unsigned> ready{};
            std::array<std::thread,8> workers;
            for(unsigned i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{
                ready.fetch_add(1);while(ready.load()!=workers.size())YieldProcessor();
                ReplayConsumerFailure::Record r{};r.site=ReplayConsumerFailure::Site::DispatchAdmission;r.reason=100+i;
                ReplayConsumerFailure::Write(r);
                // Another terminal caller must wait until the winning record
                // is flushed, even when it exits the whole process immediately.
                __fastfail(FAST_FAIL_INVALID_ARG);
            });
            for(auto& worker:workers)worker.join();return 104;
        }
        group.consumer_hooks_.acquire=[](void*,auto&)noexcept{return Sc6ReplayTaskGroup::ConsumerAdmission::Reject;};
        queued_task=task.data();
        try{group.PumpOne(true);}catch(const TerminalRejection&){return native_entries==1?0:102;}
        return 103;
    }
#ifndef REPLAY_COMPONENT_NATIVE_FASTFAIL
    const auto retained_result=CheckRetainedDispatcher(thread.data(),task.data(),component.data()+0x110,world.data());
    if(retained_result)return retained_result;
    if(const auto observed=CheckObservedDispatcher(thread.data(),task.data(),component.data()+0x110,world.data()))return observed;
    native_entries=1;pops=1;
#endif
    Store(table,0x300,image_base+0x1bcdb90);
    changed_task=task;
    const auto before=changed_task;
    queued_task=changed_task.data();
    std::puts("changed consumer queued; unchanged native entry count=1");std::fflush(stdout);
    try { group.PumpOne(true); }
    catch(const TerminalRejection& terminal) {
        if(terminal.code!=FAST_FAIL_INVALID_ARG || native_entries!=1 || changed_entries
            || pops!=2 || group.dispatched_tasks()!=1 || group.pending_manager_task()
            || changed_task!=before || group.dispatch_outcome()!=Sc6ReplayTaskGroup::DispatchOutcome::TerminalFailure
            || group.terminal_task_!=changed_task.data())return 75;
        std::puts("terminal rejection before native entry; task bytes and completed counter unchanged; NOT recovery");
        return 0;
    }
    if(changed_entries) {
        std::puts("G1 RED: admitted scheduler consumer changed; production pump forwards task to native entry");
        return 68;
    }
    return 0;
}
