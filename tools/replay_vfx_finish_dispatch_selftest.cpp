// Actual completion admission and the existing observer's Dispatch owner.
// External indexed objects/detour installation only; the original service marks
// the pre-copy boundary. It is not ProcessEvent, receiver lifetime or B recovery.
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <tuple>
#include <vector>
#include <string_view>
#include <thread>
#include "ReplaySeekOwnership.hpp"
#include "Types.hpp"
#include "ReplayConsumerFailure.hpp"
static std::uintptr_t observer_base,dispatch_entry;
static unsigned installations;
std::uint64_t ProbeInstall(std::uint64_t,std::uint64_t);
#include "NativeReplayVfxCompletionObservation.hpp"
using namespace Horse::Deterministic;

struct Row {
    std::array<std::int32_t,2> weak;
    std::uint64_t name;
    friend bool operator==(const Row&,const Row&)=default;
};
static_assert(sizeof(Row)==16);
struct Header {Row* data;std::int32_t count,capacity;};
static_assert(sizeof(Header)==16);
static constexpr std::array<Row,2> admitted{{{{7,11},21},{{8,12},21}}};

// Production predicates and arming are extracted verbatim by the runner.
// Only external object/function services and the enclosing storage are modeled.
static bool CopyBytes(void* to,const void* from,std::size_t size) {
    std::memcpy(to,from,size);return true;
}
struct Sc6ReplayVfxState {
    using CompletionOwnerCheck=bool(*)(const void*,bool,const std::array<std::int32_t,2>&,std::uint64_t);
    struct Component {struct {std::size_t count{};std::array<std::array<std::byte,16>,16> entries{};} completions;};
    struct Table {std::size_t count{};std::unique_ptr<std::byte[]> bytes;std::int32_t native_capacity{};};
    std::vector<Component> components_;
    std::array<Table,3> tables_;
    std::array<std::int32_t,2> manager_weak_{};
    std::uintptr_t base_{},manager_{};bool valid_{true};DWORD thread_=GetCurrentThreadId();
    Status ValidateCompletionOwnership(const void*,CompletionOwnerCheck,const char** = nullptr)const noexcept;
    struct PreparedManager {
        bool ready_{true},published_{true},write_complete_{true},undo_started_{},execution_settled_{};
        Sc6ReplayVfxState* target_{};Sc6ReplayVfxState* current_{};
        struct Array {void* data{};std::int32_t count{},capacity{};};
        std::array<Array,5> arrays_{};std::uint64_t epoch_{};
        Status ReadFinishDispatchBinding(ReplayVfxFinishDispatch::Binding&) const noexcept;
    };
};
#include "vfx_finish_admission.inl"
#include "vfx_finish_binding.inl"
// The exact retained-membership decision is production ValidateObject below.
// GC entry and native registry/liveness services are controlled, not a fixture
// owner predicate wired into the host. Missing ownership must reach host refusal.
static bool gc_available=true;
static unsigned gc_entries,gc_leaves;
static bool EnterAdmission(std::uintptr_t) {
    if(!gc_available)return false;
    ++gc_entries;return true;
}
static void LeaveAdmission(std::uintptr_t) {++gc_leaves;}
struct Sc6ReplayObjectLease {
    struct Control {
        struct Reference {void* object{};void* original{};};
        std::vector<Reference> references;std::uintptr_t base{};
        bool registered{true},live{true},member{true};
        bool Live()const{return live;}bool Membership()const{return member;}
    } control;
    const Control* control_=&control;
    bool registered()const{return control_ && control_->registered;}
    Status ValidateObject(const void*)const noexcept;
};
#include "vfx_finish_object_ownership.inl"
struct Sc6ReplayTraceState {
    using Object=RC::Unreal::UObject;
    struct Id {Object* object{};int index{},serial{};};
    struct Root {Id component;};
    bool captured_{true};DWORD thread_=GetCurrentThreadId();std::vector<Root> roots_;
    Sc6ReplayObjectLease lease_;
#include "vfx_finish_trace_ownership.inl"
};
// Logging is external to the host's actual decision; preserve the return status.
#define STR(x) x
namespace RC {
    enum class LogLevel {Warning};
    inline const char* to_generic_string(const char* p){return p;}
    struct Output {template<LogLevel,class... A>static void send(A...){}};
}
struct Sc6ReplayHost {
    struct CompletionOwnerContext {std::uintptr_t base;const Sc6ReplayTraceState* traces;};
    struct Checkpoint {Sc6ReplayVfxState vfx;};
    struct Execution {
        ReplaySeekOwnership ownership{624,624};CompletionOwnerContext finish_context{};
        bool finish_armed{};
        ReplayVfxFinishDispatch finish_guard;
        enum class Participant {HandedOff};std::array<Participant,1> participants{};
        enum class Render {HandedOff};Render render=Render::HandedOff;
        enum class Quarantine {Installed};Quarantine quarantine=Quarantine::Installed;
    };
    struct Transaction;
    using HistoricalRestore=Transaction;
    struct Transaction {
        Checkpoint* target{};Sc6ReplayTraceState* traces{};
        using Execution=Sc6ReplayHost::Execution;
        enum class FreshRenderPhase {Ready};FreshRenderPhase fresh_render_phase=FreshRenderPhase::Ready;
        std::vector<int> fresh_particles;
        Sc6ReplayVfxState::PreparedManager manager;std::unique_ptr<Execution> execution=std::make_unique<Execution>();
        const Sc6ReplayTraceState& TargetTraces()const{return *traces;}
    };
    Transaction* historical_restore_{};std::uintptr_t image_base_{};
    bool checkpoint_restoring_{true};
    struct ParticleCopy {
        bool particle_render_ready()const{return true;}bool execution_started()const{return true;}
        struct Witness {bool pending{};};Witness witness()const{return {};}
    } copy;
    ParticleCopy* particle_copy_=&copy;
    struct Simulation {struct {std::uint64_t tick{};} value;const auto& continuation(){return value;}};
    Simulation* simulation_{};
    template<class T>static T& EngineField(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
    static bool CheckParticleCompletionReceiver(const void*,bool,const std::array<std::int32_t,2>&,std::uint64_t);
    Status ValidateParticleCompletionOwnership(const Sc6ReplayVfxState&,const Sc6ReplayTraceState&)const;
    Status ArmHistoricalFinishGuard();
    bool HistoricalExecutionAdmitted(bool=false)const noexcept;
};
#include "vfx_finish_host_ownership.inl"
#include "vfx_finish_host_arm.inl"
#include "vfx_finish_host_execution.inl"

static Header* expected_collection;
static void* expected_argument;
static unsigned native_entries;
static std::array<Row,2> copied;
static int copied_count;
static ReplayVfxFinishDispatch* pinned_guard;
static bool reenter_dispatch;
static void NativeSnapshot(void* collection,void* argument) {
    if(collection!=expected_collection || argument!=expected_argument || GetLastError()!=0x661)std::abort();
    if(pinned_guard && pinned_guard->Stop())std::abort(); // native call pins its context, without holding the lock
    const auto& live=*static_cast<Header*>(collection);
    ++native_entries;
    // A rejection after this marker is too late: native is about to snapshot
    // every live row. Never invoke a proposed fixture-side guard here.
    std::printf("native-snapshot count=%d argument=preserved\n",live.count);std::fflush(stdout);
    if(reenter_dispatch)reinterpret_cast<void(*)(void*,void*)>(dispatch_entry)(collection,argument);
    if(live.count<0 || live.count>2)std::abort();
    copied_count=live.count;
    if(live.count)std::memcpy(copied.data(),live.data,live.count*sizeof(Row));
    SetLastError(0x771);
}

static void UnexpectedOriginal() {std::abort();}
std::uint64_t ProbeInstall(std::uint64_t target,std::uint64_t entry) {
    ++installations;
    if(target==observer_base+0x3d74d0) {
        if(dispatch_entry || !entry)std::abort(); // exactly one production owner
        dispatch_entry=entry;
        return reinterpret_cast<std::uint64_t>(&NativeSnapshot);
    }
    return reinterpret_cast<std::uint64_t>(&UnexpectedOriginal);
}
// Production signature constants seed external image storage for installation.
// This verifies routing, not native signature provenance (covered separately).
struct ObserverImage {
#include "vfx_finish_observer_sites.inl"
    static bool Start(bool install,bool diagnostics) {
        observer_base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4440000,
            MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!observer_base)return false;
        for(std::size_t i=0;i<rvas_.size();++i)
            std::memcpy(reinterpret_cast<void*>(observer_base+rvas_[i]),signatures_[i].data(),signatures_[i].size());
        if(!install)return true;
        return NativeReplayVfxCompletionObservation::InstallStartup(observer_base,true)
            && installations==rvas_.size() && dispatch_entry
            && (!diagnostics || NativeReplayVfxCompletionObservation::Start(L"vfx_completion_observation.json","vfx-finish-dispatch-fixture"));
    }
};

int main(int argc,char** argv) {
    if(argc<2 || argc>3)return 1;
    const std::string_view mode=argv[1],diagnostic=argc==3?argv[2]:"open";
    const bool empty=mode=="empty",unrelated=mode=="unrelated";
    const bool receiver=mode=="receiver",name=mode=="fname-number";
    const bool admission=mode.starts_with("admit-");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(!ReplayConsumerFailure::Start(L"consumer_failure.json","vfx-finish-dispatch-fixture"))return 3;
    alignas(8) std::array<std::byte,0x400> manager{},other_manager{};
    std::array<RC::Unreal::UObject,2> receivers{};
    std::array<RC::Unreal::UFunction,2> functions{};
    if(!ObserverImage::Start(mode!="admit-missing-hook",diagnostic!="off"))return 7;
    *reinterpret_cast<std::uintptr_t*>(manager.data())=observer_base+0x3356f68;
    *reinterpret_cast<int*>(manager.data()+0xc)=1;
    RC::Unreal::FUObjectArray::items[1]={reinterpret_cast<RC::Unreal::UObject*>(manager.data()),31};
    Sc6ReplayTraceState traces;
    for(unsigned i=0;i<2;++i) {
        receivers[i].index=admitted[i].weak[0];receivers[i].function=&functions[i];
        functions[i].pointer=reinterpret_cast<void*>(observer_base+0xc40570);
        RC::Unreal::FUObjectArray::items[admitted[i].weak[0]]={&receivers[i],admitted[i].weak[1]};
        traces.roots_.push_back({{&receivers[i],admitted[i].weak[0],admitted[i].weak[1]}});
        traces.lease_.control.references.push_back({&receivers[i],&receivers[i]});
    }
    auto live=admitted;
    auto* collection=reinterpret_cast<Header*>(manager.data()+0x388);
    *collection={empty?nullptr:live.data(),empty?0:2,empty?0:2};
    if(mode=="unreadable") {
        collection->data=static_cast<Row*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!collection->data)return 13;
        std::memcpy(collection->data,live.data(),sizeof(live));
    }
    Sc6ReplayHost::Checkpoint target,undo;
    for(auto* checkpoint:{&target,&undo}) {
        auto& image=checkpoint->vfx;image.base_=observer_base;
        image.manager_=reinterpret_cast<std::uintptr_t>(manager.data());image.manager_weak_={1,31};
        image.tables_[0].count=collection->count;image.tables_[0].native_capacity=collection->capacity;
        image.tables_[0].bytes=std::make_unique<std::byte[]>(sizeof(live));
        std::memcpy(image.tables_[0].bytes.get(),admitted.data(),sizeof(live));
    }
    Sc6ReplayHost::Transaction transaction;transaction.target=&target;transaction.traces=&traces;
    transaction.manager.target_=&target.vfx;transaction.manager.current_=&undo.vfx;
    transaction.manager.arrays_[2]={collection->data,collection->count,collection->capacity};
    Sc6ReplayHost host;host.image_base_=observer_base;host.historical_restore_=&transaction;
    auto& execution=*transaction.execution;
    if(!execution.ownership.PublishA())return 9;
    if(mode=="admit-membership")traces.roots_.pop_back();
    if(mode=="admit-receiver")++RC::Unreal::FUObjectArray::items[8].serial;
    if(mode=="admit-fname-number")++functions[1].name.number;
    if(mode=="admit-function")functions[1].pointer=nullptr;
    if(mode=="admit-duplicate") {
        std::memcpy(target.vfx.tables_[0].bytes.get()+16,admitted.data(),16);live[1]=live[0];
    }
    // Neither captured rows nor indexed live receiver/function identity changes.
    // Only the owner required to admit those receivers is made unsupported.
    if(mode=="admit-owner-unregistered")traces.lease_.control.registered=false;
    if(mode=="admit-owner-registry")traces.lease_.control.member=false;
    if(mode=="admit-owner-missing")traces.lease_.control.references.pop_back();
    if(mode=="admit-owner-replaced")traces.lease_.control.references.back().object=&receivers[0];
    if(mode=="admit-owner-invalidated")traces.lease_.control.live=false;
    if(mode=="admit-owner-gc-busy")gc_available=false;
    const auto armed=host.ArmHistoricalFinishGuard();
    if(gc_entries!=gc_leaves)return 17;
    if(admission) {
        if(armed.ok() || armed.code!=FailureCode::UnsupportedContent || execution.finish_armed
            || native_entries || !execution.ownership.retains_undo()
            || std::memcmp(undo.vfx.tables_[0].bytes.get(),admitted.data(),sizeof(admitted))) {
            std::fprintf(stderr,"unsupported finish admission: mode=%s accepted=%d B_retained=%d native_entries=%u\n",
                argv[1],armed.ok(),execution.ownership.retains_undo(),native_entries);
            return 10;
        }
        std::puts("production host finish admission rejected; B comparison unchanged; native_entries=0");return 0;
    }
    if(!armed.ok())return 4;
    if(mode=="execution-admission") {
        if(!execution.ownership.ActivateExecution() || !host.HistoricalExecutionAdmitted()
            || !execution.finish_guard.Stop())return 15;
        execution.finish_armed=false;
        if(host.HistoricalExecutionAdmitted() || !execution.ownership.retains_undo())return 16;
        std::puts("production execution admission requires armed finish guard; B still retained");return 0;
    }
    std::puts("production host finish arming and captured admission passed; indexed receiver/function predicate exercised");std::fflush(stdout);
    // Baseline is the prepared allocation plus immutable A bytes. Optional
    // diagnostics may be off, closed or exhausted while this owner stays armed.
    std::uint64_t argument=0x123456789abcdef0ull;
    expected_collection=collection;expected_argument=&argument;pinned_guard=&execution.finish_guard;
    if(diagnostic=="closed")NativeReplayVfxCompletionObservation::ClosePhase(624);
    if(diagnostic=="exhausted") {
        for(unsigned i=0;i<257;++i) {
            SetLastError(0x661);reinterpret_cast<void(*)(void*,void*)>(dispatch_entry)(collection,&argument);
            if(GetLastError()!=0x771)return 11;
        }
        std::puts("diagnostic exhaustion controls complete");std::fflush(stdout);native_entries=0;
    }
    // Mutate LIVE backing after real host arming; both captured images stay put.
    if(receiver)++live[1].weak[1];
    if(name)live[1].name+=1ull<<32;
    if(mode=="epoch")++*reinterpret_cast<std::uint64_t*>(observer_base+0x4197170);
    if(mode=="manager")++RC::Unreal::FUObjectArray::items[1].serial;
    if(mode=="receiver-lifetime")++RC::Unreal::FUObjectArray::items[8].serial;
    if(mode=="function")functions[1].pointer=nullptr;
    if(mode=="membership")traces.roots_.pop_back();
    if(mode=="count")--collection->count;
    if(mode=="capacity")++collection->capacity;
    if(mode=="backing")collection->data=reinterpret_cast<Row*>(1);
    if(mode=="order")std::swap(live[0],live[1]);
    if(mode=="duplicate")live[1]=live[0];
    if(mode=="unreadable") {
        DWORD previous{};
        if(!VirtualProtect(collection->data,4096,PAGE_NOACCESS,&previous))return 14;
    }
    reenter_dispatch=mode=="reentry";
    if(unrelated) {
        expected_collection=reinterpret_cast<Header*>(other_manager.data()+0x388);
        *expected_collection={live.data(),2,2};pinned_guard=nullptr;
    }
    const auto before_manager=manager;const auto before_rows=live;
    SetLastError(0x661);
    if(mode=="thread") {
        std::thread worker([&]{SetLastError(0x661);reinterpret_cast<void(*)(void*,void*)>(dispatch_entry)(expected_collection,&argument);});
        worker.join();return 12;
    }
    reinterpret_cast<void(*)(void*,void*)>(dispatch_entry)(expected_collection,&argument);
    const auto returned_error=GetLastError();if(returned_error!=0x771)return 8;
    if(manager!=before_manager || live!=before_rows || argument!=0x123456789abcdef0ull
        || std::memcmp(target.vfx.tables_[0].bytes.get(),admitted.data(),sizeof(admitted))
        || std::memcmp(undo.vfx.tables_[0].bytes.get(),admitted.data(),sizeof(admitted))
        || !execution.ownership.retains_undo() || !execution.finish_guard.Stop())return 5;
    if(native_entries!=1 || copied_count!=expected_collection->count || (!empty && copied!=live))return 6;
    std::printf("forwarding-identity collection=%llu storage=%llu payload=%llu thread=%lu\n",
        reinterpret_cast<unsigned long long>(expected_collection),reinterpret_cast<unsigned long long>(expected_collection->data),
        reinterpret_cast<unsigned long long>(&argument),GetCurrentThreadId());
    std::printf("forwarding-values entries=%u rows=%d last_error=%lu receiver_checks=%u\n",
        native_entries,copied_count,returned_error,RC::Unreal::receiver_checks);
    std::fflush(stdout);
    if(mode!="unchanged" && !empty && !unrelated) {
        std::puts("G1 RED: unsupported live completion reached native snapshot after admission");return 125;
    }
    std::puts("unchanged native forwarding PASS; no ownership/completion/recovery claim");return 0;
}
