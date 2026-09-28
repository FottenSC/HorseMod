// Extracted production continuation/arena control flow. Native dispatch, clocks,
// logging and hook services are stubs. NOT UObject lifetime/live recovery proof.
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

struct FastFail {};
[[noreturn]] static void TestFastFail(unsigned) {throw FastFail{};}
#define __fastfail TestFastFail
static DWORD test_thread=7;
static LONGLONG test_qpc=1000;
static unsigned sleeps{};
static DWORD TestThread(){return test_thread;}
static BOOL TestQpc(LARGE_INTEGER* value){value->QuadPart=test_qpc;return TRUE;}
static void TestSleep(DWORD){++sleeps;}
#define GetCurrentThreadId TestThread
#define QueryPerformanceCounter TestQpc
#define Sleep TestSleep
#define STR(x) x
namespace RC {
enum class LogLevel {Default,Warning};
struct Output {template<LogLevel, class... A>static void send(const char*,A...) {}};
}
template<class T>T& EngineField(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
template<class T>T& Field(void* p,std::size_t n){return EngineField<T>(p,n);}

struct Sc6ReplayWorld {
    struct Array {void* data{};std::int32_t count{},capacity{};};
    enum class Phase {Group5,Other};Phase phase_=Phase::Group5;
    struct TaskBoundary {
        enum class Phase {WaitGroup,Other};Phase phase_=Phase::WaitGroup;
        int group_=5,cleanup_group_=5;bool requested_wait_=true,pumping_=true;void* pending_task_{};
#include "consumer_host_task_boundary.inl"
    } task_groups_;
#include "consumer_host_arena_fields.inl"
#include "consumer_host_world_boundary.inl"
    void AttachArena();void DetachArena();
};
#include "consumer_host_arena.inl"

struct Sc6ReplayHost {
    struct Task {int owner_index=42,owner_generation=3;};
    struct Groups {Task task;bool held=true;const Task* held_consumer_task()const{return held?&task:nullptr;}};
    struct Executor {
        Groups groups;bool resume_ok=true;unsigned requests{},unbinds{};
        Sc6ReplayWorld* world{};
        Groups& task_groups(){return groups;}
        bool consumer_boundary(bool attached)const{return world && world->consumer_boundary(attached);}
        bool RequestConsumerResume(){++requests;return resume_ok;}
        void BindConsumerAdmission(std::nullptr_t){++unbinds;}
    } executor_;
    struct Guard {bool stop_ok=true;unsigned stops{};bool Stop(){++stops;return stop_ok;}};
    struct ConsumerHoldState {
        Guard guard;std::shared_ptr<int> checkpoint;
        std::uint64_t target=218,held_epoch=300,polls{};
        std::uint32_t hold_ms=100;LONGLONG qpc{};double wall_reference{};
        bool acquired=true,suspended{},completed{},failed{};
    };
    struct Simulation {struct Continuation{std::uint64_t tick=217;}value;const Continuation& continuation(){return value;}} sim;
    Simulation* simulation_=&sim;
    enum class ApplicationPhase {Idle,Engine,Tail};
    ApplicationPhase application_phase_=ApplicationPhase::Engine;
    std::unique_ptr<ConsumerHoldState> consumer_hold_=std::make_unique<ConsumerHoldState>();
    std::uintptr_t image_base_{};void* engine_{};unsigned depth_{};
    DWORD thread_=7;void* application_loop_=this;
    bool engine_post_deferred_{},application_active_{},idle{},world_idle_flag{},execute_ok=true;
    bool native_post_pending{},defer_ok=true,post_ok=true;
    unsigned deferred{},posts{},native_entries{},service_calls{},tail_calls{};
    Sc6ReplayWorld arena;Sc6ReplayWorld::MemoryArena ambient;
    std::array<std::byte,32> chunk{};
    static inline Sc6ReplayHost* active_{};
    bool (*defer_engine_post_)(void*)=&Defer;
    bool (*complete_engine_post_)(void*)=&Post;
    static bool Defer(void* p){auto& h=*static_cast<Sc6ReplayHost*>(p);if(!h.defer_ok||h.native_post_pending)return false;++h.deferred;h.native_post_pending=true;return true;}
    static bool Post(void* p){auto& h=*static_cast<Sc6ReplayHost*>(p);if(!h.post_ok||!h.native_post_pending)return false;++h.posts;h.native_post_pending=false;return true;}
    bool engine_idle()const{return idle;}
    bool world_idle()const{return world_idle_flag;}
    // External engine/dispatcher boundary stub. Production task dispatch itself
    // is independently tested by replay_component_dispatch_selftest.cpp.
    void AdvanceEngine(){
        arena.AttachArena();
        if(execute_ok){++native_entries;executor_.groups.held=false;consumer_hold_->completed=true;idle=true;world_idle_flag=true;}
        arena.DetachArena();
    }
    bool SuspendConsumerTask();bool AdvanceConsumerTask();bool RetireConsumerProbe();
    static void TickApplication(void*);
    Sc6ReplayHost(std::vector<std::byte>& image,std::array<std::byte,0x700>& engine){
        active_=this;image_base_=reinterpret_cast<std::uintptr_t>(image.data());engine_=engine.data();
        EngineField<double>(image.data(),0x415cd90)=0.001;
        EngineField<double>(image.data(),0x43b3778)=17.0;
        EngineField<bool>(image.data(),0x416a943)=false;EngineField<bool>(image.data(),0x416a944)=false;
        EngineField<std::uint8_t>(engine_,0x648)=0x40;EngineField<float>(engine_,0x64c)=60;
        ambient.top=reinterpret_cast<void*>(0x1234);ambient.marks=2;
        arena.mark_.arena=&ambient;arena.mark_.closed=false;
        executor_.world=&arena;
        arena.owned_arena_.mark=&arena.mark_;arena.owned_arena_.marks=1;
        Field<void*>(chunk.data(),0)=nullptr;Field<int>(chunk.data(),8)=16;
        arena.owned_arena_.chunk=chunk.data();
        consumer_hold_->checkpoint=std::make_shared<int>(1);
    }
};
#include "consumer_host_methods.inl"
#include "consumer_host_application_entry.inl"

#define CHECK(v,n) do{if(!(v)){std::printf("consumer host failure %d\n",n);return n;}}while(false)
template<class F>bool Fails(F&& f){try{f();}catch(const FastFail&){return true;}return false;}
int main(){
    std::vector<std::byte> image(0x43b4000);std::array<std::byte,0x700> engine{};
    {
        Sc6ReplayHost h(image,engine);test_qpc=1000;
        std::weak_ptr<int> lease=h.consumer_hold_->checkpoint;
        test_thread=8;CHECK(Fails([&]{h.SuspendConsumerTask();}),31);test_thread=7;
        CHECK(h.SuspendConsumerTask(),1);CHECK(h.deferred==1&&h.engine_post_deferred_,2);
        CHECK(Fails([&]{h.SuspendConsumerTask();}),3);
        test_qpc=1050;h.TickApplication(h.application_loop_);
        CHECK(h.native_entries==0&&h.posts==0&&h.tail_calls==0&&h.service_calls==0,4);
        CHECK(!h.application_active_&&h.executor_.groups.held&&!lease.expired(),5);
        test_thread=8;CHECK(Fails([&]{h.TickApplication(h.application_loop_);}),6);test_thread=7;
        CHECK(Fails([&]{h.TickApplication(nullptr);}),32);
        h.depth_=1;CHECK(Fails([&]{h.AdvanceConsumerTask();}),7);h.depth_=0;
        h.arena.task_groups_.cleanup_group_=4;
        CHECK(Fails([&]{h.AdvanceConsumerTask();}),29);h.arena.task_groups_.cleanup_group_=5;
        h.arena.AttachArena();CHECK(Fails([&]{h.AdvanceConsumerTask();}),30);h.arena.DetachArena();
        h.executor_.resume_ok=false;test_qpc=1200;h.TickApplication(h.application_loop_);
        CHECK(h.consumer_hold_->failed&&h.executor_.groups.held&&h.engine_post_deferred_&&h.native_post_pending,8);
        CHECK(h.native_entries==0&&h.posts==0&&!lease.expired(),9);
        const auto requests=h.executor_.requests;h.executor_.resume_ok=true;test_qpc=2000;
        h.TickApplication(h.application_loop_);
        CHECK(h.executor_.requests==requests&&h.executor_.groups.held&&h.posts==0&&h.tail_calls==0,10);
    }
    {
        Sc6ReplayHost h(image,engine);test_qpc=1000;CHECK(h.SuspendConsumerTask(),11);
        test_qpc=1200;h.TickApplication(h.application_loop_);
        CHECK(h.native_entries==1&&h.posts==1&&h.tail_calls==1&&h.service_calls==0,12);
        CHECK(!h.engine_post_deferred_&&!h.native_post_pending&&!h.application_active_,13);
        CHECK(!h.arena.arena_attached_&&h.ambient.top==reinterpret_cast<void*>(0x1234)&&h.ambient.marks==2,14);
        CHECK(h.arena.owned_arena_.chunk==h.chunk.data()&&h.arena.max_retained_arena_bytes_==32,15);
        CHECK(Fails([&]{h.AdvanceConsumerTask();}),16);
        CHECK(h.native_entries==1&&h.posts==1,17);
        h.engine_post_deferred_=true;CHECK(!h.RetireConsumerProbe(),18);h.engine_post_deferred_=false;
        h.consumer_hold_->guard.stop_ok=false;CHECK(!h.RetireConsumerProbe()&&h.consumer_hold_,19);
        std::weak_ptr<int> lease=h.consumer_hold_->checkpoint;
        h.consumer_hold_->guard.stop_ok=true;CHECK(h.RetireConsumerProbe(),20);
        CHECK(!h.consumer_hold_&&lease.expired()&&h.executor_.unbinds==1,21);
        CHECK(h.RetireConsumerProbe()&&h.executor_.unbinds==1,22);
    }
    {
        Sc6ReplayHost h(image,engine);test_qpc=1000;CHECK(h.SuspendConsumerTask(),23);
        h.execute_ok=false;test_qpc=1200;h.TickApplication(h.application_loop_);
        CHECK(h.depth_==0&&h.consumer_hold_->failed&&h.executor_.groups.held&&h.engine_post_deferred_,24);
        CHECK(h.posts==0&&h.tail_calls==0&&!h.arena.arena_attached_,25);
    }
    {
        Sc6ReplayHost h(image,engine);h.executor_.groups.held=false;
        CHECK(h.RetireConsumerProbe(),26);h.sim.value.tick=218;CHECK(!h.RetireConsumerProbe(),27);
        h.arena.AttachArena();h.ambient.marks=2;
        CHECK(Fails([&]{h.arena.DetachArena();}),28);
    }
    std::puts("consumer host control-flow PASS; no UObject ownership or live recovery claim");
}
