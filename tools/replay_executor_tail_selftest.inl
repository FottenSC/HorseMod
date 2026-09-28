namespace ReplayExecutorTailTest {
using namespace Horse::Deterministic;
using E=Sc6ReplayExecutor;
template<class T> T& Field(void* object,std::size_t offset) {return *reinterpret_cast<T*>(static_cast<std::byte*>(object)+offset);}
static unsigned callbacks{}, traversals{}, outer_tails{}, decisions{};
static std::byte* image{};
static bool restart{};
static std::array<std::byte,0x180> move{};
static void Callback(void*) {++callbacks;}
static void Tick(void*,void*) {++traversals;++Field<std::uint32_t>(image,0x470d0c4);}
static void Post(void*) {}
static void Outer(void*,float) {++outer_tails;}
static std::uint8_t Round(void*) {++decisions;return restart;}
static void* Move(void*) {Field<int>(move.data(),0x150)=1;return move.data();}
static bool EventComplete(void* event) {return *static_cast<bool*>(event);}
// Compile the actual pending-task validation and resume boundary against the
// production executor. Native completion is observed, not invoked on fake UE
// tasks. This is a scheduler contract fixture, not a gameplay equivalence test.
struct Sc6ReplayTaskGroup {
    E* simulation_{};std::byte* pending_task_{};bool yield_each_tick_{true};
    std::byte* named_thread_{};
    unsigned manager_yields_{},finishes{};
    void ValidatePendingTask();void ResumeManagerTask();
    void FinishManagerTask() {expect(simulation_->interval_complete(),"task completion requires native interval tail");++finishes;pending_task_=nullptr;}
};
#include "deterministic/Sc6ReplayTaskGroup.Resume.inl"
static void Run() {
    image=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    expect(image!=nullptr,"executor fixture address space");if(!image)return;
    const auto bind=[](std::size_t rva,auto function) {
        const std::uint8_t prefix[]={0x48,0xb8};std::memcpy(image+rva,prefix,2);
        const auto address=reinterpret_cast<std::uintptr_t>(function);std::memcpy(image+rva+2,&address,8);
        image[rva+10]=std::byte{0xff};image[rva+11]=std::byte{0xe0};
    };
    bind(0x3999c0,&Callback);bind(0x3fce80,&Post);bind(0x3fca60,&Post);
    bind(0x562320,&Outer);bind(0x3f2840,&Round);bind(0x3f00b0,&Move);
    FlushInstructionCache(GetCurrentProcess(),image,0x4800000);
    for(int mode=0;mode<4;++mode) {
        callbacks=traversals=outer_tails=decisions=0;restart=mode==2;
        std::array<std::byte,0x1700> manager{};std::array<void*,32> vtable{};vtable[0x68/8]=reinterpret_cast<void*>(&Tick);
        void* provider=vtable.data();Field<void*>(manager.data(),0x1450)=&provider;
        E e;ReplayExecutorTestAccess::Prepare(e,image,manager.data(),E::Phase::RepeatDecision,mode==1?2:1);
        Field<std::uint8_t>(manager.data(),0x1462)=mode==0;
        Field<int>(manager.data(),0x14f0)=1;
        std::array<std::byte,0x50> task{},owner{};bool completed=false;
        Field<void*>(task.data(),0x10)=owner.data();Field<void*>(owner.data(),0x18)=task.data();
        Field<std::uint8_t>(task.data(),0x38)=1;Field<void*>(task.data(),0x40)=&completed;
        std::array<std::byte,0x18> named_thread{}; // Native thread id at +0x10.
        Sc6ReplayTaskGroup group{&e,task.data()};
        group.named_thread_=named_thread.data();
        ReplaySeekOwnership ownership(210,208);expect(ownership.PublishA() && ownership.ActivateExecution()
            && ownership.SettleC(208) && ownership.BeginTargetTails(),"prepare exact tail ownership");
        e.SetTailOnly(true);group.ResumeManagerTask();
        expect(e.continuation().tick==208 && traversals==0,"paused completion never traverses gameplay");
        expect(ownership.retains_undo() && !ownership.CompleteTargetTails(209),"B retained and overshoot rejected");
        if(mode==3) {
            expect(e.interval_complete() && group.finishes==1 && outer_tails==1 && callbacks==1,
                "last-input epilogues finish exactly once before task completion");
            expect(ownership.CompleteTargetTails(208),"exact completed tails admitted");
        } else {
            const auto boundary=e.continuation();const auto callback_count=callbacks;const auto decision_count=decisions;
            expect(e.tail_blocked() && group.pending_task_==task.data() && !completed && group.finishes==0,
                "future work retains the actual pending task and incomplete event");
            for(int update=0;update<5;++update) {e.SetTailOnly(true);group.ResumeManagerTask();}
            expect(e.continuation()==boundary && callbacks==callback_count && decisions==decision_count
                && !completed && group.pending_task_==task.data(),"repeated external completion attempts do not duplicate work");
            expect(ownership.DeferTargetTails() && ownership.retains_undo(),"deferred completion remains cancellable");
            if(mode==0) {
                auto recovery=ownership;
                expect(recovery.RequestCancellation()==ReplaySeekOwnership::Cancellation::Quiesce
                    && recovery.retains_undo() && !recovery.BeginCommit(),"deferred completion admits cancellation with B retained");
                e.SetTailOnly(false);group.ResumeManagerTask();
                expect(e.continuation().tick==209 && traversals==1 && callbacks==1 && !completed,
                    "explicit execution resumes the owed repeat exactly once");
                group.ResumeManagerTask();
                expect(e.interval_complete() && group.finishes==1 && outer_tails==1 && callbacks==2
                    && traversals==1,"recovery completes actual native interval tails before installing B");
                expect(ownership.BeginResumedTails() && ownership.CompleteTargetTails(209),"only explicit resumed ownership admits later tick");
                expect(recovery.CompleteRecoveryTails(e.continuation().tick) && recovery.retains_undo()
                    && !recovery.CompleteRecovery(209) && recovery.CompleteRecovery(210),
                    "disposable C completion at209 must recover original B210, never commit C");
            } else expect(boundary.phase==(mode==1?E::Phase::PublishInput:E::Phase::RestartSetup),
                "completion stops before future input publication or native round reset");
        }
        e.SetTailOnly(false);e.Stop();
    }
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
}
}
