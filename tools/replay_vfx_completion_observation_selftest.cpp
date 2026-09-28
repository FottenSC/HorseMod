// Native observation and material-boundary contracts. Startup, observers,
// material wrappers and extracted host decisions are production code. The external
// detour service routes the real RVAs to native probes if no hook exists.
// No fixture observer, admission predicate, serialization, or callback lease.
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <string>
#include <intrin.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <type_traits>
#include <vector>
#include <filesystem>
#include <format>
#include "replay_qualification_mod/ReplayFailureProtocol.hpp"
#include "Types.hpp"
#include "ReplaySeekOwnership.hpp"

static std::uintptr_t image;
static std::array<std::uint64_t,34> entries{};
static unsigned installations;
static const char* mode="normal";
static bool trace_start_direct{};
namespace TraceStartDiagnosticFixture {static void __fastcall NativeRequestStart(void*,void*);}
namespace TraceCreationFixture {
struct Pair {std::uintptr_t state{},controller{};};
static void __fastcall NativeStart(void*,void*);
static Pair* __fastcall NativeFactory(void*,Pair*,void*,int,unsigned char);
static void* __fastcall NativeConstruct(void*,void*,std::uint64_t,std::uint32_t,void*,bool,void*);
static int Run();
}
std::uint64_t ProbeInstall(std::uint64_t,std::uint64_t);
bool ProbeHook(std::uint64_t);
void ProbePublished(std::uint64_t);
void ProbeMaterialCode(std::uint64_t,std::uint64_t);

// These existing, separately tested owners do not own either target RVA.
// Their stubs cannot install an observation hook or manufacture a witness.
namespace Horse::Deterministic {
bool VerifiedReplayExecutable(){return std::strcmp(mode,"material-settlement-startup-identity")!=0;}
struct NativeReplayTraceTaskGuard {
    static bool InstallStartup(std::uintptr_t,bool){return true;}
    static std::size_t ProcessOwnedBytes(){return 0;}
};
struct NativeReplayNiagaraObservation {
    static bool InstallStartup(std::uintptr_t,bool){return true;}
    static std::size_t ProcessOwnedBytes(){return 0;}
};
}

// The pre-implementation fallback is retained in the immutable observer RED.
// This fixture now compiles the actual observer, with no fixture substitute.
static thread_local bool forbid_observer_reads{};
static std::atomic<unsigned> forbidden_observer_reads{};
static void (*shape_read_intervention)(std::uintptr_t,SIZE_T,bool){};
static BOOL WINAPI ProbeReadProcessMemory(HANDLE process,LPCVOID address,LPVOID out,SIZE_T bytes,SIZE_T* copied) {
    if(forbid_observer_reads)++forbidden_observer_reads;
    if(shape_read_intervention)shape_read_intervention(reinterpret_cast<std::uintptr_t>(address),bytes,false);
    const auto result=::ReadProcessMemory(process,address,out,bytes,copied);
    const auto error=GetLastError();
    if(shape_read_intervention)shape_read_intervention(reinterpret_cast<std::uintptr_t>(address),bytes,true);
    SetLastError(error);return result;
}
#define ReadProcessMemory ProbeReadProcessMemory
// A scheduler interposition only: perform the real unlock first, then allow an
// off-thread production builder to run before the caller consumes Clear. No
// guard records, receipt result, or native mutation permission is substituted.
static void (*material_after_unlock)(){};
static void WINAPI ProbeMaterialUnlock(PSRWLOCK lock) {
    ::ReleaseSRWLockExclusive(lock);
    if(material_after_unlock)material_after_unlock();
}
#define ReleaseSRWLockExclusive ProbeMaterialUnlock
#include "NativeReplayMaterialTaskGuard.hpp"
#undef ReleaseSRWLockExclusive
#include "NativeReplayVfxCompletionObservation.hpp"
#include "NativeReplayTraceStartDiagnostic.hpp"
#undef ReadProcessMemory

namespace RC {bool IsInitialCppModStartupThread(){return std::strcmp(mode,"material-settlement-startup-late")!=0;}}
namespace RC {const std::string& to_generic_string(const std::string& value){return value;}}
enum class LogLevel {Default,Error};
struct Output {template<LogLevel,class...T>static void send(T...) {}};
#define STR(x) x
#define HORSE_MOD_API
#include "vfx_observation_exports.inl"
template<class T> static T ResolveHorseModExport(const char* name) {
#include "vfx_observation_resolver.inl"
    return nullptr;
}
static void ProductionCompletion() {
    struct {std::string run_id{"vfx-observation-fixture"};} request_;
    struct {std::uint32_t frame{337};} trajectory_last_;
    if(!std::strcmp(mode,"writers-capacity-full"))trajectory_last_.frame=483;
#include "vfx_observation_completion.inl"
}
static HMODULE ProbeModule(void*){return reinterpret_cast<HMODULE>(image);}
static void ProductionStartup() {
#define GetModuleHandleW ProbeModule
#include "vfx_material_startup.inl"
#include "vfx_observation_startup.inl"
#undef GetModuleHandleW
}

struct Row {
    std::int32_t index,serial;std::uint64_t name;
    friend bool operator==(const Row&,const Row&)=default;
};
struct Header {Row* data;std::int32_t count,capacity;};
static_assert(sizeof(Row)==16 && sizeof(Header)==16);
alignas(8) static std::array<std::byte,0x400> manager{};
alignas(8) static std::array<std::byte,0x40> receiver{};
static std::array<Row,2> rows{};
static Header* collection;
static constexpr std::uint64_t descriptor=0x123456789abcdef0ull;
static constexpr std::uint64_t function_name=0x100000015ull;
static std::uint32_t payload=0xaabbccdd;
static unsigned registrations;
static std::atomic<unsigned> dispatches{};
static std::atomic<DWORD> overlap_worker{};
static HANDLE worker_entered{},worker_release{};
static bool nested;
static bool expire_on_return;
static bool truncate_rows;
static void Dispatch();
static void Prune(void* c);
static void* CopyCollection(void* c,void* source);
static unsigned prunes{},copies{};
static bool helpers_in_dispatch{},close_in_copy{},expire_in_copy{};
static bool full_rows{};
static Header source_collection{};
static void* copy_result{};
static void* expected_writer_collection{};
alignas(8) static std::array<std::byte,0xa88> particle{};
static unsigned disables{},completions{},tails{};
alignas(8) static std::array<std::byte,0x1d0> attachment{};
static void* attachment_argument=attachment.data();
static bool attachment_ready{};
static unsigned attachment_callbacks{};
static void CompleteParticle();
static void NativeDisable(void*,void*,unsigned char);
alignas(8) static std::array<std::byte,0x50> owner_task{},particle_task{};
alignas(8) static std::array<std::byte,0x58> owner_tick{},particle_tick{};
alignas(8) static std::array<std::byte,0x110> disable_hub{};
alignas(8) static std::array<std::byte,18*0x40> hub_entries{};
alignas(8) static std::array<std::byte,0x30> hub_callback_a{},hub_callback_b{};
alignas(8) static std::array<std::byte,0x40> hub_receiver_b{};
static void RunTask(void* task);
static unsigned owner_recursion{};
static void RegisterFromTask();
template<class T> static T& Field(void* object,std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object)+offset);
}

// Only the owner's surrounding storage/types are reduced here. Both the
// callback body and input-array helpers below are extracted unchanged from
// production. No fixture-side observer, generation counter or veto is present.
namespace Horse::Deterministic {
enum class OwnedBatchPresentationMode : std::uint8_t {VerifyRecorded,CaptureCorrected};
struct CallbackFixtureRequest {
    std::vector<InputPair> inputs,corrected_inputs;
    bool native_input_producer{};
    OwnedBatchPresentationMode presentation_mode{};
};
struct CallbackFixtureResult {
    unsigned observed_coordinates{},filter_invocations{};
    FailureCode failure{};
};
struct CallbackFixtureExecution {
    CallbackFixtureRequest* request{};CallbackFixtureResult* result{};
    unsigned invocations_for_coordinate{};
};
struct CallbackFixtureObservation {std::uintptr_t battle_manager{};};
struct CallbackFixtureCapture {
    CallbackFixtureObservation* observation{};CallbackFixtureExecution* owned{};
    PlayerInput pre_filter_inputs[2]{},post_filter_inputs[2]{};
    unsigned input_filter_invocations{};bool input_filter_observed{};
};
class DeterministicHookSet {
public:
    using CallbackExecutorFn=void(__fastcall*)(void*,void*);
    inline static std::atomic<unsigned> callbacks_in_flight_{};
    inline static std::atomic<DeterministicHookSet*> active_{};
    inline static std::atomic<std::uint64_t> callback_executor_trampoline_global_{};
    inline static thread_local CallbackFixtureCapture* active_outer_capture_{};
    std::uint64_t callback_executor_trampoline_{};
    static void __fastcall CallbackExecutorDetour(void*,void*) noexcept;
};
template<class T> static bool SafeRead(std::uintptr_t p,T& out) noexcept {
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&out,sizeof(out),&copied)
        && copied==sizeof(out);
}
#include "vfx_callback_inputs.inl"
#include "vfx_callback_executor.inl"
}

static void* callback_input_collection{};
static void* callback_trace_collection{};
static void* callback_hub_pages{};
static void* callback_descriptor_page{};
static unsigned callback_unrelated{},callback_input{},callback_trace{},callback_appends{},hub_removes{};
static std::array<unsigned,12> writer_native_calls{};
static unsigned native_writer_depth{};
static bool c18_pages_revoked{};
static unsigned recycled_depth{};
static std::thread recycled_worker{};
static bool Mode(const char* suffix) {return !std::strcmp(mode+13,suffix);}
static bool MaterialShapeMode() {return !std::strncmp(mode+13,"material-shape",14);}
static bool ZeroSerialShapeMode() {return !std::strncmp(mode,"collection18-material-shape-zero",32);}
static bool CorrelationMode() {return std::strstr(mode,"collection18-material-shape-zero-correlation-")==mode;}
static bool InlineZeroSerialShapeMode() {return std::strstr(mode,"collection18-material-shape-zero-inline")==mode;}
static bool CombatSelectionMode() {return std::strstr(mode,"collection18-material-shape-zero-combat-")==mode;}
static bool RejectedCombatSelection() {return CombatSelectionMode() && !Mode("material-shape-zero-combat-prearm");}
static bool MaterialMultiBucketMode() {
    return Mode("material-providers-multibucket") || Mode("material-providers-multibucket-chain");
}
static bool MaterialCensusMode() {
    return Mode("material-providers") || Mode("material-providers-unoccupied") || MaterialMultiBucketMode()
        || MaterialShapeMode();
}
// Controlled input memory only. No material receipt, selection predicate or
// replacement C18 observer is implemented by this fixture.
static constexpr std::size_t material_graph_bytes=0x15000;
static std::byte* material_graph{};
static std::array<std::byte,material_graph_bytes> material_graph_before{};
static std::array<std::byte,0x6000> material_hub_before{};
static std::array<std::byte,0x1c> material_descriptor_before{};
static std::array<std::byte,0x50> material_registry_before{};
static unsigned shape_world_calls{},shape_header_reads{},shape_resource_reads{},shape_hub_flags_reads{},shape_listener_flag_reads{};
static unsigned combat_epoch_reads{};
static DWORD shape_page_access{};
static void ShapeReadIntervention(std::uintptr_t address,SIZE_T bytes,bool after) {
    if(!after && address==image+0x4197170 && bytes==8 && ++combat_epoch_reads==4
        && Mode("material-shape-zero-combat-double-sample")) {
        // Two native reads during arm, then change between entry samples.
        Field<std::uint64_t>(reinterpret_cast<void*>(image),0x4197170)=708;
    }
    const auto hub=reinterpret_cast<std::uintptr_t>(callback_hub_pages);
    const auto collection=reinterpret_cast<std::uintptr_t>(callback_trace_collection);
    if(!after && address==hub+8 && bytes==4 && ++shape_hub_flags_reads==2) {
        // Invalidate the indexed entry after the selecting identity read,
        // before the independent diagnostic identity read (no receipt edits).
        if(Mode("material-shape-zero-inline-hub-identity"))RC::Unreal::FUObjectArray::items[4].object=material_graph;
        if(Mode("material-shape-zero-inline-entry-serial"))++RC::Unreal::FUObjectArray::items[4].serial;
    }
    if(address==collection+0x40 && bytes==8 && Mode("material-shape-zero-inline-header-unreadable")) {
        DWORD old{};
        if(!VirtualProtect(callback_hub_pages,0x1000,after?shape_page_access:PAGE_NOACCESS,&old))std::abort();
        if(!after)shape_page_access=old;
    }
    if(address==collection+0x64 && bytes==4 && Mode("material-shape-zero-inline-recursion"))
        Field<int>(callback_trace_collection,0x64)=after?0:1;
    auto* listener=material_graph+0x1000; // first of the two heap rows in native reverse order
    if(address==reinterpret_cast<std::uintptr_t>(listener)+8 && bytes==4) {
        if(!after && ++shape_listener_flag_reads==1) {
            // The initial weak lookup has finished. Change actual indexed
            // input before the independent object sample, never its result.
            if(Mode("material-shape-zero-listener-sample-serial"))++RC::Unreal::FUObjectArray::items[8].serial;
            if(Mode("material-shape-zero-listener-sample-index")) {
                RC::Unreal::FUObjectArray::items[23]=RC::Unreal::FUObjectArray::items[8];
                RC::Unreal::FUObjectArray::items[8].object=nullptr;
                Field<int>(listener,0xc)=23;Field<int>(material_graph_before.data()+0x1000,0xc)=23;
            }
        }
        if(Mode("material-shape-zero-listener-object-unreadable")) {
            DWORD old{};
            if(!VirtualProtect(listener,0x1000,after?shape_page_access:PAGE_NOACCESS,&old))std::abort();
            if(!after)shape_page_access=old;
        }
    }
    const auto mid=reinterpret_cast<std::uintptr_t>(material_graph+0xd000);
    if(!after && ((address>=1 && address<=4)
        || (address>=reinterpret_cast<std::uintptr_t>(material_graph+0x10b00)
            && address<reinterpret_cast<std::uintptr_t>(material_graph+0x10c40))))++shape_resource_reads;
    if(address!=mid+0xb8 || bytes!=16)return;
    if(!after) {
        ++shape_header_reads;
        if((Mode("material-shape-unreadable") || Mode("material-shape-zero-unreadable"))
            && !VirtualProtect(reinterpret_cast<void*>(mid),0x1000,PAGE_NOACCESS,&shape_page_access))std::abort();
    } else {
        if(Mode("material-shape-unreadable") || Mode("material-shape-zero-unreadable")) {
            DWORD old{};
            if(!VirtualProtect(reinterpret_cast<void*>(mid),0x1000,shape_page_access,&old))std::abort();
        }
        if(Mode("material-shape-header-during") && shape_header_reads==1) {
            Field<int>(material_graph+0xd000,0xc4)=9;
            Field<int>(material_graph_before.data()+0xd000,0xc4)=9;
        }
    }
}
static std::filesystem::path QualificationRoot(){return std::filesystem::current_path();}
static bool ProductionDiagnosticStartup() {
    std::map<std::string,std::string> fields{{"version","19"},{"run_id","vfx-observation-fixture"},
        {"replay_path","fixture.replay"},{"watch_frames","360"},{"capture_mode","trajectory"},
        {"c18_diagnostic","true"},{"c18_selection","combat_170_220"},{"skip_intros","true"},{"include_setup","true"}};
    bool selected{};const auto original=fields;
    if(!ReadReplayC18DiagnosticProtocol(fields,selected) || !selected || fields!=original)std::abort();
    for(const auto* key:{"probe_consumer_task","probe_application_pause","probe_historical_restore",
        "host_seek","executor_yield_every_tick","consumer_mutation","unknown"}) {
        fields=original;fields[key]="true";selected=true;
        if(ReadReplayC18DiagnosticProtocol(fields,selected) || selected)std::abort();
    }
    for(const auto* capture:{"executor","trajectory_match","executor_match"}) {
        fields=original;fields["capture_mode"]=capture;
        if(ReadReplayC18DiagnosticProtocol(fields,selected))std::abort();
    }
    fields=original;fields["version"]="13";
    if(ReadReplayC18DiagnosticProtocol(fields,selected))std::abort();
    fields=original;fields["version"]="18";
    if(ReadReplayC18DiagnosticProtocol(fields,selected))std::abort();
    fields=original;fields.erase("c18_selection");
    if(ReadReplayC18DiagnosticProtocol(fields,selected))std::abort();
    fields=original;fields["watch_frames"]="361";
    if(ReadReplayC18DiagnosticProtocol(fields,selected))std::abort();
    fields=original;fields["flush_startup_loading"]="true";
    if(!ReadReplayC18DiagnosticProtocol(fields,selected) || !selected)std::abort();
    bool bound_=true;const bool startup_request_valid=true;
    struct {bool c18_diagnostic{true};std::string run_id{"vfx-observation-fixture"};} startup_request;
    [&] {
#include "vfx_c18_diagnostic_startup.inl"
    }();
    std::puts("ordinary C18 protocol/startup PASS: executor_and_unknown_fields_rejected=1");
    return bound_;
}
// External native combat memory, never an observer receipt or arm substitute.
alignas(16) static std::array<std::byte,0x4000> combat_context{};
struct CombatObject {
    template<class T>T* GetValuePtrByPropertyNameInChain(const char*) {
        return reinterpret_cast<T*>(reinterpret_cast<std::byte*>(this)+0x400);
    }
};
// Match the production type's namespace; a bridge-local alias hid a build error.
namespace RC::Unreal {using UObject=::CombatObject;}
struct ReplayTrajectorySample {
    std::uintptr_t replay_player{};
    bool source_active{};
    int round{},world_mode{};
    unsigned char manager_phase{},round_state{};
    std::uint32_t frame{},round_frame{};
};
template<class T>static bool ReadSchedulerValue(std::uintptr_t address,T& value) {
    SIZE_T copied{};return ::ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&copied)
        && copied==sizeof(value);
}
struct CombatOutput {
    template<LogLevel,class...T>static void send(const char* format,T...args) {
        std::fputs(std::vformat(format,std::make_format_args(args...)).c_str(),stdout);
    }
};
struct CombatBridge {
    struct {bool c18_diagnostic{true};std::string run_id{"vfx-observation-fixture"};} request_;
    bool c18_diagnostic_logged_=false;
    std::uint64_t c18_diagnostic_arm_{},c18_diagnostic_epoch_{};
    std::uintptr_t c18_diagnostic_player_{};
    bool replay_scene_ready_{true},battle_terminate_observed_{},boundary_failed_{};
    CombatObject* battle_manager_=reinterpret_cast<CombatObject*>(combat_context.data());
    const char* failure{};
    void Fail(const char* reason) {
#include "vfx_c18_diagnostic_fail.inl"
        failure=reason;
    }
#define GetModuleHandleW ProbeModule
#define Output CombatOutput
#include "vfx_c18_diagnostic_sample.inl"
#undef Output
#undef GetModuleHandleW
};
static CombatBridge combat_bridge;
static ReplayTrajectorySample CombatSample(unsigned frame=170,std::uint64_t epoch=700) {
    auto* m=combat_context.data();auto* p=m+0x2000;
    Field<std::uint32_t>(reinterpret_cast<void*>(image),0x470d0c4)=frame;
    Field<std::uint64_t>(reinterpret_cast<void*>(image),0x4197170)=epoch;
    Field<int>(reinterpret_cast<void*>(image),0x4846364)=2;
    Field<unsigned char>(m,0x1461)=2;Field<unsigned char>(m,0x1480)=2;Field<unsigned>(m,0x1490)=41+frame-170;
    Field<unsigned char>(p,0x398)=1;Field<int>(p,0x39c)=0;
    return {reinterpret_cast<std::uintptr_t>(p),true,0,2,2,2,frame,41+frame-170};
}
static void SetupCombatContext() {
    auto* m=combat_context.data();auto* p=m+0x2000;
    // Independent native evidence: InitializeALuxBattleManagerObject1403DC7F0
    // installs14327AA20 at1403DC817.143356F68 belongs to the separate VFX manager.
    const auto table=Mode("material-shape-zero-combat-arm-vfx")?0x3356f68u:
        Mode("material-shape-zero-combat-arm-unknown")?0x2000u:0x327aa20u;
    Field<std::uintptr_t>(m,0)=image+table;Field<int>(m,0xc)=24;
    std::printf("combat fixture manager_table_rva=%x indexed_slot=24\n",table);
    Field<std::uintptr_t>(p,0)=image+0x3357000;Field<int>(p,0xc)=25;
    RC::Unreal::FUObjectArray::items[24]={m,61};RC::Unreal::FUObjectArray::items[25]={p,62};
    Field<void*>(m,0x400)=p;Field<std::uintptr_t>(p,0x390)=image+0x3290d20;
}
static void ProductionDiagnosticReturn() {
    // The bridge reads only its live combat inputs, never the revoked C18 graph.
    const auto sample=CombatSample(177,707);
    if(!combat_bridge.ObserveC18CombatSample(sample) || !combat_bridge.c18_diagnostic_logged_)std::abort();
    std::uint64_t receipt[5]{};
    if(!horsemod_read_collection18_observation_return(receipt,5))std::abort();
    std::printf("c18 owned return entry=%llu returned=%llu thread=%llu collection=%llu descriptor=%llu\n",
        receipt[0],receipt[1],receipt[2],receipt[3],receipt[4]);
}
static_assert(sizeof(combat_context)+2*material_graph_bytes+sizeof(material_hub_before)
    +sizeof(material_descriptor_before)+sizeof(material_registry_before)<256*1024);
static std::uint32_t FixtureWorldRegistryBucket(std::uintptr_t world,std::uint32_t buckets) {
    // Independent fixture seeding from native lookup142018870 / rehash141D01080.
    // Every intermediate is uint32_t; never call the production census here.
    std::uint32_t i=static_cast<std::uint32_t>(world>>4);
    std::uint32_t a=(0x9e3779b9u-i)^(i<<8);
    std::uint32_t b=(0u-(a+i))^(a>>13);
    std::uint32_t c=((i-a)-b)^(b>>12);
    a=((a-c)-b)^(c<<16);
    b=((b-a)-c)^(a>>5);
    c=((c-a)-b)^(b>>3);
    a=((a-c)-b)^(c<<10);
    return (((b-a)-c)^(a>>15))&(buckets-1);
}
static void* FixtureMaterialWorld(void* engine,void* context,int lookup_mode) {
    // External GetWorldFromContextObject service, not a battle-manager lookup.
    // Production must still traverse the real-shaped sparse/hash registry.
    if(engine!=material_graph+0x10800 || lookup_mode!=1
        || (context!=material_graph && context!=material_graph+0x1000))std::abort();
    auto* level=Field<void*>(context,0x20);
    if(level!=material_graph+0x2000)std::abort();
    if(MaterialShapeMode())++shape_world_calls;
    if(Mode("material-shape-zero-inline-listener") && shape_world_calls==1) {
        // Replace the already sampled inline listener before the next capture.
        auto* callable=static_cast<std::byte*>(callback_hub_pages)+0x3030;
        Field<void*>(callback_trace_collection,0x20)=callable;
        Field<void*>(material_hub_before.data()+0x810,0x20)=callable;
    }
    const bool capacity=std::strstr(mode,"material-shape-zero-capacity-")!=nullptr;
    if(MaterialShapeMode() && shape_world_calls==((InlineZeroSerialShapeMode() || capacity)?2u:3u)) {
        // One inline or two heap listeners per complete capture. Change native
        // input at the first lookup of the second capture, never a receipt.
        if(Mode("material-shape-header") || Mode("material-shape-zero-header") || Mode("material-shape-zero-inline-header")) {
            Field<int>(material_graph+0xd000,0xc4)=9;
            Field<int>(material_graph_before.data()+0xd000,0xc4)=9;
        }
        if(Mode("material-shape-generation"))++RC::Unreal::FUObjectArray::items[20].serial;
        if(Mode("material-shape-zero-hub-serial") || Mode("material-shape-zero-inline-hub-serial"))++RC::Unreal::FUObjectArray::items[4].serial;
        if(Mode("material-shape-zero-mid-serial"))++RC::Unreal::FUObjectArray::items[20].serial;
        if(Mode("material-shape-zero-hub-pointer") || Mode("material-shape-zero-inline-hub-pointer"))RC::Unreal::FUObjectArray::items[4].object=material_graph;
        if(Mode("material-shape-zero-mid-index")) {
            Field<int>(material_graph+0xd000,0xc)=21;Field<int>(material_graph_before.data()+0xd000,0xc)=21;
        }
        if(Mode("material-shape-zero-flags")) {
            Field<unsigned>(material_graph+0xd000,8)=1;Field<unsigned>(material_graph_before.data()+0xd000,8)=1;
        }
        if(Mode("material-shape-zero-collection-header") || Mode("material-shape-zero-inline-collection-header")) {
            Field<int>(callback_trace_collection,0x54)=63;Field<int>(material_hub_before.data()+0x810,0x54)=63;
        }
        if(Mode("material-shape-zero-inline-storage")) {
            auto* storage=static_cast<std::byte*>(callback_hub_pages)+0x1000;
            Field<void*>(callback_trace_collection,0x40)=storage;
            Field<void*>(material_hub_before.data()+0x810,0x40)=storage;
        }
        if(Mode("material-shape-zero-topology")) {
            Field<void*>(material_graph+0x9000,0x910)=nullptr;
            Field<void*>(material_graph_before.data()+0x9000,0x910)=nullptr;
        }
        if(capacity) {
            // Same occupancy, different raw proxy/cache pointers must still
            // invalidate agreement. Only native input changes between samples.
            const unsigned offset=Mode("material-shape-zero-capacity-proxy")?0xf8:
                Mode("material-shape-zero-capacity-cache")?0x1a8:0;
            if(offset) {
                Field<std::uintptr_t>(material_graph+0xd000,offset)=5;
                Field<std::uintptr_t>(material_graph_before.data()+0xd000,offset)=5;
            }
            if(Mode("material-shape-zero-capacity-header")) {
                Field<int>(material_graph+0xd000,0xc4)=9;
                Field<int>(material_graph_before.data()+0xd000,0xc4)=9;
            }
            if(Mode("material-shape-zero-capacity-serial"))++RC::Unreal::FUObjectArray::items[20].serial;
            if(Mode("material-shape-zero-capacity-topology")) {
                // Replace the final selected slot's MID with its indexed alias
                // peer while retaining total slot/shape counts.
                Field<void*>(material_graph+0x11400,71*8)=material_graph+0xe000;
                Field<void*>(material_graph_before.data()+0x11400,71*8)=material_graph+0xe000;
            }
        }
    }
    return Field<void*>(level,0xc0);
}
static void SetupMaterialCensus() {
    // Native contract: rollback-g1-finish-material-native-contract-2026-09-27.md.
    // Two registered world-context receivers select fighter[1], not themselves.
    // Both trace arrays have one provider; their material identities alias.
    // Same independent native table/invoke bytes as the existing one-arg
    // observer control. A table address alone does not validate its layout.
    constexpr std::uintptr_t listener_slots[]{0x10acd10,0x549650,0x2fc0b0,0x549650,
        0x301490,0x3f18b0,0x2f08bb0,0x3c3b60,0x3f0670,0x8955b0,
        0x17dfea0,0x1e33070,0x3bbce0,0x3ef060};
    for(unsigned j=0;j<14;++j)
        Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x374b760),j*8)=image+listener_slots[j];
    constexpr unsigned char listener_invoke[]{72,137,92,36,8,72,137,116,36,16,87,72,131,236,32,72,139,217,72,139,242,72,131,193,8,232,114,51,185,0,72,133,192,116,40,72,141,75,8,232,100,51,185,0,76,139,67,16};
    std::memcpy(reinterpret_cast<void*>(image+0x3ef060),listener_invoke,sizeof(listener_invoke));
    material_graph=static_cast<std::byte*>(VirtualAlloc(nullptr,material_graph_bytes,
        MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!material_graph)std::abort();
    // Four actual indexed world identities are selected from 64 disjoint,
    // bounded candidate addresses in the existing world page. Prefer a nonzero
    // bucket collision; even if all collide in bucket0, pigeonhole guarantees
    // a pair. Only fixture layout is selected here, never the observed manager.
    std::array<unsigned,4> world_offsets{0x40,0,0x80,0xc0};
    if(MaterialMultiBucketMode()) {
        std::array<int,16> first{};first.fill(-1);bool collision{};
        for(unsigned offset=0;offset<0x1000;offset+=0x40) {
            const auto bucket=FixtureWorldRegistryBucket(
                reinterpret_cast<std::uintptr_t>(material_graph+0x3000+offset),16);
            if(first[bucket]<0)first[bucket]=static_cast<int>(offset);
            else {
                world_offsets[1]=static_cast<unsigned>(first[bucket]);world_offsets[3]=offset;
                collision=true;if(bucket)break;
            }
        }
        if(!collision)std::abort();
        unsigned offset{};
        for(const auto row:{0u,2u}) {
            while(offset==world_offsets[1] || offset==world_offsets[3])offset+=0x40;
            world_offsets[row]=offset;offset+=0x40;
        }
    }
    auto object=[](unsigned page,int index,std::uintptr_t table_rva=0,unsigned offset=0) {
        auto* p=material_graph+page*0x1000+offset;
        // The generic external UObject table is not a certified native class.
        // Only the explicit receiver/root/actor/provider RVAs are layout claims;
        // material objects model indexed identities, not MID setter/type proof.
        Field<std::uintptr_t>(p,0)=image+(table_rva?table_rva:0x2000);
        Field<int>(p,0xc)=index;
        RC::Unreal::FUObjectArray::items[index]={p,100+index};return p;
    };
    // UE registration1409A8680 / constructor1403ABFF0 identify the receiver
    // as LuxBattleTraceEventHandler. Descriptor selection separately finds Chara.
    const auto receiver_table=Mode("material-shape-chara-receiver-handler") || Mode("material-shape-zero-chara-receiver-handler")?0x326b8d8u:
        Mode("material-shape-chara-receiver-chara") || Mode("material-shape-zero-chara-receiver-chara")?0x3268078u:
        Mode("material-shape-chara-receiver-unknown") || Mode("material-shape-zero-chara-receiver-unknown")?0x2000u:0x326b2e0u;
    const auto fighter_table=Mode("material-shape-chara-fighter-handler") || Mode("material-shape-zero-chara-fighter-handler")?0x326b8d8u:
        Mode("material-shape-chara-fighter-unknown") || Mode("material-shape-zero-chara-fighter-unknown")?0x2000u:0x3268078u;
    auto* listener0=object(0,7,receiver_table);
    auto* listener1=object(1,8,receiver_table);
    // Input-only rejection probes. No access through these table values is
    // permitted: the exact class predicate must reject before the getter.
    if(Mode("material-shape-zero-type-zero-rva"))Field<std::uintptr_t>(listener1,0)=image;
    if(Mode("material-shape-zero-type-below-base"))Field<std::uintptr_t>(listener1,0)=image-1;
    if(Mode("material-shape-zero-type-wide"))Field<std::uintptr_t>(listener1,0)=image+0x100000000ull;
    if(Mode("material-shape-zero-type-null"))Field<std::uintptr_t>(listener1,0)=0;
    if(Mode("material-shape-zero-type-reentry") || Mode("material-shape-zero-type-close"))
        Field<std::uintptr_t>(listener1,0)=image+0x326b8d8;
    auto* level=object(2,9);auto* world=object(3,10,0,world_offsets[1]);
    // 1403EF7A0 returns the world-registry row's ALuxBattleManager (+08),
    // whose constructor1403DC7F0 installs14327AA20, not the VFX manager table.
    const auto battle_table=Mode("material-shape-registry-vfx") || Mode("material-shape-zero-registry-vfx")?0x3356f68u:
        Mode("material-shape-registry-unknown") || Mode("material-shape-zero-registry-unknown")?0x2000u:0x327aa20u;
    auto* battle=object(4,11,battle_table);
    std::printf("material registry target table_rva=0x%x index=11\n",battle_table);
    auto* fighter=object(5,12,fighter_table);
    std::printf("material character tables receiver=0x%x fighter=0x%x distinct_indexed_objects=1\n",receiver_table,fighter_table);
    auto* trace_manager=object(6,13);auto* root=object(7,14,0x3360ca8);
    auto* actor0=object(8,15,0x3361660);auto* mesh0=object(9,16,0x38829c0);
    auto* actor1=object(10,17,0x3361660);auto* mesh1=object(11,18,0x38829c0);
    auto* asset=object(12,19);auto* override_mid=object(13,20);auto* fallback_mid=object(14,21);
    auto* empty_particle=object(15,22);auto* data=material_graph+0x10000;
    if(ZeroSerialShapeMode()) {
        RC::Unreal::FUObjectArray::items[4].serial=0;
        // Weak delegate receivers retain their actual positive generations.
        // Raw indexed route objects are deliberately lazy-serial objects.
        for(unsigned i=9;i<=21;++i)RC::Unreal::FUObjectArray::items[i].serial=0;
    }
    if(MaterialShapeMode()) {
        Field<std::uintptr_t>(override_mid,0)=image+0x391ee70;
        Field<std::uintptr_t>(fallback_mid,0)=image+0x391ee70; // asset MID is out of shape scope
        Field<void*>(override_mid,0xb8)=data+0xb00;
        Field<int>(override_mid,0xc0)=1;Field<int>(override_mid,0xc4)=8;
        // Occupancy only: deliberately unreadable targets must never be chased.
        Field<std::uintptr_t>(override_mid,0xf0)=1;
        Field<std::uintptr_t>(override_mid,0xf8)=2;
        Field<std::uintptr_t>(override_mid,0x150)=3;
        Field<std::uintptr_t>(override_mid,0x1a8)=4;
        if(Mode("material-shape-pointer"))Field<void*>(override_mid,0xb8)=nullptr;
        shape_read_intervention=&ShapeReadIntervention;
    }
    Field<void*>(listener0,0x20)=level;Field<void*>(listener1,0x20)=level;
    Field<void*>(fighter,0x20)=level;Field<void*>(level,0xc0)=world;
    // +458 on both listeners remains null. Only the selected fighter owns it.
    Field<void*>(battle,0x390)=data+0x40;
    Field<int>(battle,0x398)=2;Field<int>(battle,0x39c)=2;
    Field<void*>(data,0x40)=listener0;Field<void*>(data,0x48)=fighter;
    Field<void*>(fighter,0x458)=trace_manager;Field<void*>(trace_manager,0x3a8)=root;
    Field<void*>(root,0x1c8)=world;Field<void*>(root,0x490)=fighter;
    Field<unsigned>(root,0x498)=1;
    // 14406E4E0: sparse rows + occupancy + hash heads, never a dense TArray.
    auto* registry=reinterpret_cast<void*>(image+0x406e4e0);
    Field<void*>(registry,0)=data;
    Field<int>(registry,8)=1;Field<int>(registry,0xc)=1;
    Field<unsigned>(registry,0x10)=Mode("material-providers-unoccupied")?0u:1u;
    Field<int>(registry,0x28)=1;Field<int>(registry,0x2c)=128;
    Field<int>(registry,0x30)=-1;Field<int>(registry,0x34)=0;
    Field<int>(registry,0x38)=0;Field<int>(registry,0x3c)=-1;
    Field<unsigned>(registry,0x48)=1; // one bucket: every world hashes to zero
    if(!MaterialMultiBucketMode()) {
        Field<void*>(data,0)=world;Field<void*>(data,8)=battle;
        Field<int>(data,0x10)=-1;Field<unsigned>(data,0x14)=0;
    } else {
        // Native sizing141CF3D90: four live rows require16 buckets (not four).
        // Keep backing/heads inside the existing graph, away from trace inputs.
        auto* registry_rows=data+0x900;auto* heads=data+0xa00;
        Field<void*>(registry,0)=registry_rows;
        Field<int>(registry,8)=4;Field<int>(registry,0xc)=4;
        Field<unsigned>(registry,0x10)=0xf;Field<int>(registry,0x28)=4;
        Field<int>(registry,0x38)=-1;Field<void*>(registry,0x40)=heads;
        Field<unsigned>(registry,0x48)=16;
        std::array<std::byte*,4> worlds{},managers{};unsigned extra{};
        for(unsigned row=0;row<4;++row) {
            if(row==1) {worlds[row]=world;managers[row]=battle;}
            else {
                worlds[row]=object(3,23+extra,0,world_offsets[row]);
                managers[row]=object(4,26+extra,0x327aa20,(extra+1)*0x400);++extra;
            }
        }
        for(unsigned bucket=0;bucket<16;++bucket)Field<int>(heads,bucket*4)=-1;
        // Rehash occupied rows in increasing slot order, prepending each row
        // to its computed bucket. Row3 collides ahead of the selected row1.
        for(unsigned row=0;row<4;++row) {
            const auto bucket=FixtureWorldRegistryBucket(reinterpret_cast<std::uintptr_t>(worlds[row]),16);
            auto* entry=registry_rows+row*0x18;
            Field<void*>(entry,0)=worlds[row];Field<void*>(entry,8)=managers[row];
            Field<int>(entry,0x10)=Field<int>(heads,bucket*4);
            Field<std::uint32_t>(entry,0x14)=bucket;Field<int>(heads,bucket*4)=row;
        }
        std::array<std::byte,0x18> selected_row{};
        std::memcpy(selected_row.data(),registry_rows+0x18,selected_row.size());
        // Independent negative: one collision link becomes a self-cycle.
        // Exact selected world/manager row bytes remain present but unreachable.
        if(Mode("material-providers-multibucket-chain"))Field<int>(registry_rows+3*0x18,0x10)=3;
        if(std::memcmp(selected_row.data(),registry_rows+0x18,selected_row.size()))std::abort();
        const auto bucket=FixtureWorldRegistryBucket(reinterpret_cast<std::uintptr_t>(world),16);
        std::printf("material registry buckets=%u rows=%d occupancy=%u free_count=%d target_row=1 head=%d target_bytes_unchanged=1\n",
            Field<unsigned>(registry,0x48),Field<int>(registry,8),Field<unsigned>(registry,0x10),
            Field<int>(registry,0x34),Field<int>(heads,bucket*4));
        for(unsigned row=0;row<4;++row) {
            auto* entry=registry_rows+row*0x18;
            std::printf("material registry row=%u world=%llu manager=%llu bucket=%u next=%d\n",row,
                static_cast<unsigned long long>(Field<std::uintptr_t>(entry,0)),
                static_cast<unsigned long long>(Field<std::uintptr_t>(entry,8)),
                Field<unsigned>(entry,0x14),Field<int>(entry,0x10));
        }
    }
    for(unsigned i=0;i<2;++i) {
        auto* actor=i?actor1:actor0;auto* mesh=i?mesh1:mesh0;
        auto* ref=data+0x80+i*0x20;auto* controller=data+0x100+i*0x200;
        Field<void*>(root,0x418+i*0x10)=ref;
        Field<int>(root,0x420+i*0x10)=1;Field<int>(root,0x424+i*0x10)=1;
        Field<void*>(ref,0)=controller+0x10;Field<void*>(ref,8)=controller;
        Field<std::uintptr_t>(controller,0)=image+0x3362590;
        Field<int>(controller,8)=1;Field<int>(controller,12)=1;
        Field<void*>(controller+0x10,8)=actor;
        Field<void*>(actor,0x168)=mesh;Field<void*>(actor,0x398)=mesh;
        Field<void*>(mesh,0x190)=actor;Field<void*>(mesh,0x910)=asset;
        Field<void*>(mesh,0x808)=data+0x500+i*0x20;
        Field<int>(mesh,0x810)=2;Field<int>(mesh,0x814)=2;
        Field<void*>(data,0x500+i*0x20)=override_mid; // slot1 is null: asset fallback
        auto* callable=static_cast<std::byte*>(callback_hub_pages)+0x3000+i*0x30;
        Field<int>(callable,8)=7+i;Field<int>(callable,12)=107+i;
    }
    Field<void*>(asset,0xa0)=data+0x600;
    Field<int>(asset,0xa8)=2;Field<int>(asset,0xac)=2;
    // Slot0's asset material differs from its overriding MID. Slot1 falls back.
    Field<void*>(data,0x600)=fallback_mid;Field<void*>(data,0x630)=fallback_mid;
    if(Mode("material-shape-overflow") || Mode("material-shape-zero-overflow")) {
        Field<void*>(asset,0xa0)=material_graph+0x12000;
        Field<int>(asset,0xa8)=49;Field<int>(asset,0xac)=49;
        for(unsigned i=0;i<2;++i) {
            auto* mesh=i?mesh1:mesh0;
            auto* overrides=material_graph+0x11000+i*0x400;
            Field<void*>(mesh,0x808)=overrides;
            Field<int>(mesh,0x810)=49;Field<int>(mesh,0x814)=49;
            for(unsigned slot=0;slot<49;++slot)Field<void*>(overrides,slot*8)=override_mid;
        }
    }
    if(std::strstr(mode,"material-shape-zero-capacity-")) {
        // Native input only: one heap listener, two ordered providers, and two
        // aliased indexed MIDs. Occurrences must not be deduplicated by MID.
        const unsigned occurrences=Mode("material-shape-zero-capacity-limit")?192u:
            Mode("material-shape-zero-capacity-overflow")?193u:145u;
        const unsigned slots=(occurrences+1)/2;
        Field<int>(callback_trace_collection,0x50)=1;
        Field<void*>(asset,0xa0)=material_graph+0x12000;
        Field<int>(asset,0xa8)=slots;Field<int>(asset,0xac)=slots;
        for(unsigned i=0;i<2;++i) {
            auto* mesh=i?mesh1:mesh0;
            auto* overrides=material_graph+0x11000+i*0x400;
            Field<void*>(mesh,0x808)=overrides;
            const unsigned count=i?occurrences-slots:slots;
            Field<int>(mesh,0x810)=count;Field<int>(mesh,0x814)=count;
            for(unsigned slot=0;slot<count;++slot)
                Field<void*>(overrides,slot*8)=((i+slot)&1)?fallback_mid:override_mid;
        }
        Field<void*>(fallback_mid,0xb8)=data+0xb00;
        Field<int>(fallback_mid,0xc0)=2;Field<int>(fallback_mid,0xc4)=9;
        Field<std::uintptr_t>(fallback_mid,0x100)=3;
        for(unsigned i=0;i<12;++i)Field<std::uintptr_t>(fallback_mid,0x150+i*8)=(i&1)?4:0;
        std::printf("shape capacity native occurrences=%u indexed_mids=2 heap_listeners=1 providers=2\n",occurrences);
    }
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x38829c0),0x628)=image+0x1dc8a90;
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x38829c0),0x4e8)=image+0x1dc8220;
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x326b8d8),0x138)=image+0x1c204e0;
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x3268078),0x138)=image+0x1c204e0;
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x326b2e0),0x138)=image+0x1c204e0;
    Field<void*>(reinterpret_cast<void*>(image),0x43b3068)=data+0x800;
    // Native world lookup is an external service; no 3EF7A0 manager shortcut.
    auto* jump=reinterpret_cast<unsigned char*>(image+0x21784f0);
    jump[0]=0x48;jump[1]=0xb8;
    const auto target=reinterpret_cast<std::uint64_t>(&FixtureMaterialWorld);
    std::memcpy(jump+2,&target,8);jump[10]=0xff;jump[11]=0xe0;
    DWORD old{};if(!VirtualProtect(jump,12,PAGE_EXECUTE_READ,&old))std::abort();
    FlushInstructionCache(GetCurrentProcess(),jump,12);
    // Exact semantic descriptor fields; padding supplies no configuration.
    Field<int>(callback_descriptor_page,0)=1;
    Field<unsigned char>(callback_descriptor_page,0xc)=0;
    Field<unsigned char>(callback_descriptor_page,0x15)=0x12;
    if(Mode("material-shape-zero-weak")) {
        RC::Unreal::FUObjectArray::items[7].serial=0;
        Field<int>(static_cast<std::byte*>(callback_hub_pages)+0x3000,12)=0;
    }
    if(std::strstr(mode,"collection18-material-shape-zero-listener-")==mode) {
        auto* callable=static_cast<std::byte*>(callback_hub_pages)+0x3030;
        if(Mode("material-shape-zero-listener-weak-zero"))Field<int>(callable,12)=0;
        if(Mode("material-shape-zero-listener-weak-negative"))Field<int>(callable,12)=-1;
        if(Mode("material-shape-zero-listener-index-negative"))Field<int>(callable,8)=-1;
        if(Mode("material-shape-zero-listener-index-missing"))Field<int>(callable,8)=32;
        if(Mode("material-shape-zero-listener-index-null"))RC::Unreal::FUObjectArray::items[8].object=nullptr;
        if(Mode("material-shape-zero-listener-stale"))++RC::Unreal::FUObjectArray::items[8].serial;
        if(Mode("material-shape-zero-listener-object-flags"))Field<unsigned>(listener1,8)=0x18000;
        if(Mode("material-shape-zero-listener-table"))Field<std::uintptr_t>(listener1,0)=image+1;
        std::printf("c18 listener native input case=%s heap_rows=2 first_receiver_index=8\n",mode);
    }
    if(InlineZeroSerialShapeMode()) {
        // Verified first insertion: capacity one with no heap allocation.
        // Seed native input only; the real callback owner must interpret it.
        std::memcpy(callback_trace_collection,static_cast<std::byte*>(callback_hub_pages)+0x1000,0x40);
        Field<void*>(callback_trace_collection,0x40)=nullptr;
        Field<int>(callback_trace_collection,0x50)=1;
        Field<int>(callback_trace_collection,0x54)=1;
        if(Mode("material-shape-zero-inline-negative-count"))Field<int>(callback_trace_collection,0x50)=-1;
        if(Mode("material-shape-zero-inline-zero-capacity"))Field<int>(callback_trace_collection,0x54)=0;
        if(Mode("material-shape-zero-inline-multirow")) {
            Field<int>(callback_trace_collection,0x50)=2;Field<int>(callback_trace_collection,0x54)=2;
        }
        if(Mode("material-shape-zero-inline-extra-capacity"))Field<int>(callback_trace_collection,0x54)=2;
        if(Mode("material-shape-zero-inline-over-capacity"))Field<int>(callback_trace_collection,0x54)=65;
        if(Mode("material-shape-zero-inline-empty-capacity"))Field<int>(callback_trace_collection,0x50)=0;
        if(Mode("material-shape-zero-inline-hub-vtable"))Field<std::uintptr_t>(callback_hub_pages,0)=image+1;
        if(Mode("material-shape-zero-inline-dispatcher-vtable"))Field<std::uintptr_t>(callback_hub_pages,0x28)=image+1;
        std::printf("c18 native inline fixture: count=%d capacity=%d heap=null listeners=1\n",
            Field<int>(callback_trace_collection,0x50),Field<int>(callback_trace_collection,0x54));
    }
    if(Field<void*>(empty_particle,0xa98) || Field<int>(empty_particle,0xaa0)
        || Field<void*>(empty_particle,0xaa8) || Field<int>(empty_particle,0xab0))std::abort();
    std::memcpy(material_graph_before.data(),material_graph,material_graph_bytes);
    std::memcpy(material_hub_before.data(),callback_hub_pages,material_hub_before.size());
    std::memcpy(material_descriptor_before.data(),callback_descriptor_page,material_descriptor_before.size());
    std::memcpy(material_registry_before.data(),registry,material_registry_before.size());
    std::printf("material fixture graph=%llu registry=%llu particle_roots_empty=1\n",
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(material_graph)),
        static_cast<unsigned long long>(image+0x406e4e0));
}
static void CheckMaterialCensusInputs() {
    if(std::memcmp(material_graph,material_graph_before.data(),material_graph_bytes)
        || std::memcmp(callback_hub_pages,material_hub_before.data(),material_hub_before.size())
        || std::memcmp(callback_descriptor_page,material_descriptor_before.data(),material_descriptor_before.size())
        || std::memcmp(reinterpret_cast<void*>(image+0x406e4e0),material_registry_before.data(),material_registry_before.size()))std::abort();
}
static void HubRemoveCall(void*);
static void AppendCall(void*,bool=false);
static void NativeRowBacking(void*,int,unsigned,std::uintptr_t);
template<class Fn,class... Args> static auto WriterCall(unsigned slot,Fn native,Args... args) {
    const auto fn=entries[slot]?reinterpret_cast<Fn>(entries[slot]):native;
    SetLastError(0x66b);
    if constexpr(std::is_void_v<std::invoke_result_t<Fn,Args...>>) {
        fn(args...);forbid_observer_reads=false;if(GetLastError()!=0x77b)std::abort();
    } else {
        auto result=fn(args...);forbid_observer_reads=false;if(GetLastError()!=0x77b)std::abort();return result;
    }
}
static void NativeCompact(void* c,bool threshold) {
    if(GetLastError()!=0x66b || !threshold || !c)std::abort();
    if(Mode("preexisting") && !callback_trace)
        reinterpret_cast<void(*)(void*,void*)>(image+0x400a80)(c,callback_descriptor_page);
    if(Mode("cross-thread") && !callback_trace) {
        SetEvent(worker_entered);if(WaitForSingleObject(worker_release,5000)!=WAIT_OBJECT_0)std::abort();
    }
    if(Mode("startup-active") && !callback_trace) {
        if(!Horse::Deterministic::NativeReplayVfxCompletionObservation::Start(
            L"vfx_completion_observation.json","vfx-observation-fixture"))std::abort();
        reinterpret_cast<void(*)(void*,void*)>(image+0x400a80)(c,callback_descriptor_page);
    }
    if(Mode("recycled-parent") && !callback_trace) {
        if(c!=callback_trace_collection) {
            SetEvent(worker_entered);if(WaitForSingleObject(worker_release,5000)!=WAIT_OBJECT_0)std::abort();
        } else if(++recycled_depth==1) {
            SetEvent(worker_release);recycled_worker.join(); // frees lower active slot
            WriterCall(11,&NativeCompact,c,true); // child reuses it under parent in higher slot
        } else reinterpret_cast<void(*)(void*,void*)>(image+0x400a80)(c,callback_descriptor_page);
    }
    ++writer_native_calls[1];SetLastError(0x77b);
}
static void* NativeOneArgumentAppend(void* c,void* out,void* target,void* callable) {
    if(GetLastError()!=0x66b || target!=receiver.data() || !callable)std::abort();
    ++writer_native_calls[0];
    ++native_writer_depth;
    // Verified14043D25A: stride16 allocation on stack-local callable storage,
    // BEFORE compaction/count publication. The observer must account for it.
    alignas(8) std::array<std::byte,0x40> temporary{};
    WriterCall(21,&NativeRowBacking,temporary.data(),0,3u,std::uintptr_t{0x10});
    if(Mode("active-overflow") && native_writer_depth<34)AppendCall(c);
    if(Mode("close-inside") && callback_appends==0)ProductionCompletion();
    WriterCall(11,&NativeCompact,c,true);
    --native_writer_depth;
    ++callback_appends;
    auto* storage=Field<std::byte*>(c,0x40);
    if(!storage)storage=static_cast<std::byte*>(c);
    const auto count=Field<int>(c,0x50);
    if(count<0 || count>=64)std::abort();
    Field<void*>(storage+count*0x40,0x20)=static_cast<std::byte*>(callback_hub_pages)+0x3000+count*0x30;
    Field<int>(storage+count*0x40,0x30)=3;Field<int>(c,0x50)=count+1;
    *static_cast<std::uint64_t*>(out)=0x2200+callback_appends;
    if(Mode("binder-out-revoked")) {DWORD old{};if(!VirtualProtect(out,0x1000,PAGE_NOACCESS,&old))std::abort();}
    forbid_observer_reads=true;SetLastError(0x77b);return out;
}
static void AppendCall(void* c,bool unknown) {
    std::uint64_t out{};
    auto* pointer=Mode("binder-out-revoked")?static_cast<std::uint64_t*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)):&out;
    if(!pointer || WriterCall(10,&NativeOneArgumentAppend,c,pointer,receiver.data(),reinterpret_cast<void*>(image+(unknown?0x123:0x3c5360)))!=pointer
        || (!Mode("binder-out-revoked") && out!=0x2200+callback_appends))std::abort();
    if(pointer!=&out)VirtualFree(pointer,0,MEM_RELEASE);
}
static void NativeRange(void* c,int index,int count,bool shrink) {
    if(GetLastError()!=0x66b || index!=0 || count!=1 || shrink)std::abort();
    ++writer_native_calls[2];auto* rows=Field<std::byte*>(c,0x40);
    // Native1403A1910 destroys the destination, copies the tail over its SAME
    // ADDRESS, then decrements count. It does not preserve insertion order.
    std::memcpy(rows,rows+(Field<int>(c,0x50)-1)*0x40,0x40);
    --Field<int>(c,0x50);SetLastError(0x77b);
}
static void NativeBacking(void* c,int old_count,unsigned capacity,std::uintptr_t stride) {
    if(GetLastError()!=0x66b || old_count!=Field<int>(c,0x50) || capacity!=64 || stride!=0x40)std::abort();
    ++writer_native_calls[3]; // controlled allocator reuses exactly the same address
    SetLastError(0x77b);
}
static void NativeCollectionDestroy(void* c) {
    if(GetLastError()!=0x66b)std::abort();++writer_native_calls[4];
    auto* rows=Field<std::byte*>(c,0x40);Field<int>(rows,0x30)=0;
    Field<void*>(rows,0x20)=nullptr;SetLastError(0x77b);
}
static void NativeHubDestroy(void* hub) {
    if(GetLastError()!=0x66b || hub!=callback_hub_pages)std::abort();++writer_native_calls[5];
    WriterCall(14,&NativeCollectionDestroy,static_cast<std::byte*>(hub)+0x810);SetLastError(0x77b);
}
static void NativeGrow(void* c,int old_count) {
    if(GetLastError()!=0x66b)std::abort();++writer_native_calls[6];
    Field<int>(c,0x54)=64;WriterCall(13,&NativeBacking,c,old_count,64u,std::uintptr_t{0x40});SetLastError(0x77b);
}
static void NativeCapacity(void* c,unsigned capacity) {
    if(GetLastError()!=0x66b)std::abort();++writer_native_calls[7];
    Field<int>(c,0x54)=capacity;WriterCall(13,&NativeBacking,c,Field<int>(c,0x50),capacity,std::uintptr_t{0x40});SetLastError(0x77b);
}
static void NativeRowClear(void* row) {
    if(GetLastError()!=0x66b)std::abort();++writer_native_calls[8];
    Field<int>(row,0x30)=0;Field<void*>(row,0x20)=nullptr;SetLastError(0x77b);
}
static void NativeRowBacking(void* row,int preserve,unsigned count,std::uintptr_t stride) {
    if(GetLastError()!=0x66b || preserve || count!=3 || stride!=0x10)std::abort();
    ++writer_native_calls[11]; // controlled realloc returns the original address
    SetLastError(0x77b);
}
static void* NativeRowResize(void* row,int bytes) {
    if(GetLastError()!=0x66b || bytes!=0x28)std::abort();++writer_native_calls[10];
    //140399420 calls the old delegate destructor BEFORE checking count. Equal
    // unit counts skip allocation; they do not skip the destructive operation.
    auto* storage=Field<void*>(row,0x20);
    Field<std::uint64_t>(storage,0x20)=0;
    if(Field<int>(row,0x30)!=3)WriterCall(21,&NativeRowBacking,row,0,3u,std::uintptr_t{0x10});
    Field<int>(row,0x30)=3;
    if(Mode("writer-return-revoked")) {DWORD old{};
        if(!VirtualProtect(callback_hub_pages,0x6000,PAGE_NOACCESS,&old))std::abort();c18_pages_revoked=true;}
    forbid_observer_reads=true;SetLastError(0x77b);return storage;
}
static void NativeRowCopy(void* source,void* destination) {
    if(GetLastError()!=0x66b)std::abort();++writer_native_calls[9];
    // Model1417DFEA0 rebuilding the destination while preserving the source
    // weak identity/function/handle. Matching bytes do not reveal this event.
    std::array<std::byte,0x28> saved{};std::memcpy(saved.data(),source,saved.size());
    auto* storage=Field<void*>(destination,0x20);
    if(WriterCall(20,&NativeRowResize,destination,0x28)!=storage)std::abort();
    Field<void*>(destination,0x20)=storage;Field<int>(destination,0x30)=3;
    std::memcpy(storage,saved.data(),saved.size());SetLastError(0x77b);
}
namespace C18CorrelationProbe {
static unsigned starts{},helpers{};
static unsigned count_calls{},getter_calls{},setter_calls{};
static thread_local unsigned depth{};
static constexpr std::uint64_t name=0x100000015ull;
static std::array<std::uint32_t,4> bits{0x80000000,0x3f800000,0x7fc01234,0x3e800000};
static float* unreadable_color{};
static DWORD selected_thread{};
static float* Color() {return unreadable_color?unreadable_color:reinterpret_cast<float*>(bits.data());}
static void Start();
static bool SlotMode() {return !std::strncmp(mode,"collection18-material-shape-zero-correlation-slots",49);}
static bool SlotCase(const char* suffix) {return std::string(mode)==std::string("collection18-material-shape-zero-correlation-slots-")+suffix;}
static bool RowMode() {return std::strstr(mode,"collection18-material-shape-zero-correlation-slots-row")==mode;}
static bool RowCase(const char* suffix) {return std::string(mode)==std::string("collection18-material-shape-zero-correlation-slots-row-")+suffix;}
static std::byte* row_pages{};
static unsigned row_dispatches{},changed_rows{},equal_rows{};
static void NativeMaterialDispatch(void* mid,void* row) {
    auto expected=bits;if(RowCase("color"))expected[0]^=1;
    if(GetLastError()!=0xa24 || (mid!=material_graph+0xe000 && mid!=material_graph+0xd000)
        || (!RowCase("unreadable") && (Field<std::uint64_t>(row,0)!=name+unsigned(RowCase("fname") || RowCase("unstable"))
            || std::memcmp(static_cast<std::byte*>(row)+8,expected.data(),16))))std::abort();
    ++row_dispatches;
    if(row_dispatches==1 && (RowCase("reentry") || RowCase("concurrent"))) {
        const auto nested=[mid,row] {
            SetLastError(0xa24);reinterpret_cast<void(*)(void*,void*)>(entries[31])(mid,row);
            if(GetLastError()!=0xb24)std::abort();forbid_observer_reads=false;
        };
        if(RowCase("concurrent")){std::thread worker(nested);worker.join();}else nested();
    }
    if(RowCase("invalidate"))Horse::Deterministic::NativeReplayVfxCompletionObservation::InvalidateCollection18Combat("vfx-observation-fixture");
    if(RowCase("close")) {
        Horse::Deterministic::NativeReplayVfxCompletionObservation::ClosePhase(338);
        if(!CopyFileW(L"vfx_completion_observation.json",L"row_dispatch_pending.json",FALSE))std::abort();
    }
    DWORD old{};if(!VirtualProtect(row,0x1000,PAGE_NOACCESS,&old))std::abort();
    forbid_observer_reads=true;SetLastError(0xb24);
}
static void RowSetter(void* mid,float* color) {
    auto* row=row_pages+(mid==material_graph+0xd000?0x1000:0);
    bool changed=false;
    for(unsigned i=0;i<4;++i)changed|=Field<float>(row,8+i*4)!=color[i];
    if(!changed){++equal_rows;return;}
    ++changed_rows;std::memcpy(row+8,color,16);
    if(RowCase("fname"))++Field<std::uint64_t>(row,0);
    if(RowCase("color"))Field<std::uint32_t>(row,8)^=1;
    auto* saved_read=shape_read_intervention;
    if(RowCase("unstable"))shape_read_intervention=[](std::uintptr_t address,SIZE_T size,bool after) {
        if(address==reinterpret_cast<std::uintptr_t>(row_pages) && size==24 && after
            && Field<std::uint64_t>(row_pages,0)==name)++Field<std::uint64_t>(row_pages,0);
    };
    const unsigned count=RowCase("overflow")?65:RowCase("multi")?2:1;
    for(unsigned i=0;i<count;++i) {
        DWORD old{};
        if((i || RowCase("unreadable"))
            && !VirtualProtect(row,0x1000,RowCase("unreadable")?PAGE_NOACCESS:PAGE_READWRITE,&old))std::abort();
        SetLastError(0xa24);
        reinterpret_cast<void(*)(void*,void*)>(image+(RowCase("republisher")?0x1f1a580:0x1f1e500))(
            RowCase("mid")?material_graph+0xd000:mid,row);
        if(GetLastError()!=0xb24)std::abort();forbid_observer_reads=false;
    }
    shape_read_intervention=saved_read;
    // The native dispatch revoked this row. Neither native tail nor observer
    // return bookkeeping reads it; the next setter has a separate row/page.
}
static int NativeCount(void* provider) {
    if(GetLastError()!=0xa21 || provider!=material_graph+0x9000)std::abort();
    ++count_calls;auto* asset=Field<void*>(provider,0x910);
    const int result=asset?Field<int>(asset,0xa8):0;
    SetLastError(0xb21);return result;
}
static void* NativeGet(void* provider,int slot) {
    if(GetLastError()!=0xa22 || provider!=material_graph+0x9000 || slot< -1 || slot>2)std::abort();
    ++getter_calls;
    if(slot==0 && getter_calls==1 && (SlotCase("reentry") || SlotCase("concurrent"))) {
        const auto nested=[] {
            SetLastError(0xa22);
            auto* value=reinterpret_cast<void*(*)(void*,int)>(entries[29])(material_graph+0x9000,0);
            if(GetLastError()!=0xb22 || value!=material_graph+0xe000)std::abort();forbid_observer_reads=false;
        };
        if(SlotCase("concurrent")){std::thread worker(nested);worker.join();}else nested();
    }
    // External native service follows root's verified override-first lookup.
    // The helper below changes native inputs AFTER the entry census; no
    // fixture receipt or synthetic observer is supplied to production.
    void* result=nullptr;
    if(slot>=0 && slot<Field<int>(provider,0x810))result=Field<void*>(Field<void*>(provider,0x808),std::size_t(slot)*8);
    auto* asset=Field<void*>(provider,0x910);
    if(!result && asset && slot>=0 && slot<Field<int>(asset,0xa8))
        result=Field<void*>(Field<void*>(asset,0xa0),std::size_t(slot)*0x30);
    if(SlotCase("invalidate"))Horse::Deterministic::NativeReplayVfxCompletionObservation::InvalidateCollection18Combat("vfx-observation-fixture");
    if(SlotCase("close"))Horse::Deterministic::NativeReplayVfxCompletionObservation::ClosePhase(337);
    if(SlotCase("revoked") && result) {
        DWORD old{};if(!VirtualProtect(result,0x1000,PAGE_NOACCESS,&old))std::abort();
    }
    forbid_observer_reads=true;SetLastError(0xb22);return result;
}
static void* NativeGetFallthrough(void* provider,int slot) {
    if(slot<0 || slot>=Field<int>(provider,0x810))std::abort();
    return NativeGet(provider,slot);
}
static void* NativeGetBranch(void* provider,int slot) {
    if(slot>=0 && slot<Field<int>(provider,0x810))std::abort();
    return NativeGet(provider,slot);
}
static void NativeSet(void* mid,std::uint64_t key,float* color) {
    auto expected=bits;if(SlotCase("color"))expected[0]^=1;
    if(GetLastError()!=0xa23 || (RowMode()?mid!=material_graph+0xe000 && mid!=material_graph+0xd000
        :mid!=material_graph+(SlotCase("mid")?0xd000:0xe000))
        || key!=name+unsigned(SlotCase("name")) || std::memcmp(color,expected.data(),16))std::abort();
    ++setter_calls;if(RowMode())RowSetter(mid,color);
    forbid_observer_reads=true;SetLastError(0xb23);
}
static void Slots() {
    if(SlotCase("signature-wrapper"))Field<unsigned char>(reinterpret_cast<void*>(image+0x1f45940),0)=0x48;
    // Execute real call/return PCs and the independently verified wrapper's
    // stack copy. No return-address intrinsic or production receipt is mocked.
    auto* getter_thunk=reinterpret_cast<unsigned char*>(image+0x8d5870);
    std::memset(getter_thunk,0x90,0x28);
    const unsigned char sub[]{0x48,0x83,0xec,0x28},add[]{0x48,0x83,0xc4,0x28,0xc3};
    std::memcpy(getter_thunk,sub,4);
    getter_thunk[0x1c]=0xff;getter_thunk[0x1d]=0x15;
    const std::int32_t indirect=0x8d5900-0x8d5892;
    std::memcpy(getter_thunk+0x1e,&indirect,4);std::memcpy(getter_thunk+0x22,add,5);
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x8d5900),0)=entries[29]?entries[29]:reinterpret_cast<std::uintptr_t>(&NativeGet);
    auto* setter_thunk=reinterpret_cast<unsigned char*>(image+0x8d58c0);
    std::memset(setter_thunk,0x90,0x28);std::memcpy(setter_thunk,sub,4);setter_thunk[0x18]=0xe8;
    const std::int32_t relative=0x1f45940-0x8d58dd;
    std::memcpy(setter_thunk+0x19,&relative,4);std::memcpy(setter_thunk+0x1d,add,5);
    if(entries[30]!=image+0x1f1e420) {
        auto* lower=reinterpret_cast<unsigned char*>(image+0x1f1e420);
        lower[0]=0x48;lower[1]=0xb8;
        const auto destination=entries[30]?entries[30]:reinterpret_cast<std::uintptr_t>(&NativeSet);
        std::memcpy(lower+2,&destination,8);lower[10]=0xff;lower[11]=0xe0;
    }
    const unsigned char wrapper_epilogue[]{0x48,0x83,0xc4,0x38,0xc3};
    std::memcpy(reinterpret_cast<void*>(image+0x1f45957),wrapper_epilogue,5);
    if(RowMode()) {
        row_pages=static_cast<std::byte*>(VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!row_pages)std::abort();
        for(unsigned n=0;n<2;++n) {
            auto* row=row_pages+n*0x1000;Field<std::uint64_t>(row,0)=name;
            if((n==1 && !RowCase("both")) || RowCase("none"))std::memcpy(row+8,bits.data(),16);
        }
        auto* dispatch_thunk=reinterpret_cast<unsigned char*>(image+0x1f1e500);
        std::memset(dispatch_thunk,0x90,0x30);std::memcpy(dispatch_thunk,sub,4);dispatch_thunk[0x23]=0xe8;
        const std::int32_t dispatch_relative=0x1f05150-0x1f1e528;
        std::memcpy(dispatch_thunk+0x24,&dispatch_relative,4);std::memcpy(dispatch_thunk+0x28,add,5);
        auto* republisher=reinterpret_cast<unsigned char*>(image+0x1f1a580);
        std::memset(republisher,0x90,0x30);std::memcpy(republisher,sub,4);republisher[0x25]=0xe8;
        const std::int32_t republish_relative=0x1f05150-0x1f1a5aa;
        std::memcpy(republisher+0x26,&republish_relative,4);std::memcpy(republisher+0x2a,add,5);
        if(entries[31]!=image+0x1f05150) {
            auto* dispatch=reinterpret_cast<unsigned char*>(image+0x1f05150);
            dispatch[0]=0x48;dispatch[1]=0xb8;
            const auto destination=entries[31]?entries[31]:reinterpret_cast<std::uintptr_t>(&NativeMaterialDispatch);
            std::memcpy(dispatch+2,&destination,8);dispatch[10]=0xff;dispatch[11]=0xe0;
        }
    }
    DWORD old{};
    for(const auto page:{0x8d5000u,0x1f45000u,0x1f1e000u,0x1f05000u,0x1f1a000u})
        if(!VirtualProtect(reinterpret_cast<void*>(image+page),0x1000,PAGE_EXECUTE_READWRITE,&old))std::abort();
    if(!FlushInstructionCache(GetCurrentProcess(),nullptr,0))std::abort();
    auto* provider=material_graph+0x9000;
    auto* overrides=Field<void*>(provider,0x808);
    auto* asset_rows=Field<void*>(Field<void*>(provider,0x910),0xa0);
    auto* saved_override=Field<void*>(overrides,0);
    auto* saved_fallback=Field<void*>(asset_rows,0x30);
    Field<void*>(overrides,0)=material_graph+0xe000;
    Field<void*>(asset_rows,0x30)=RowMode()?material_graph+0xd000:SlotCase("alias")?material_graph+0xe000:nullptr;
    const auto get_count=reinterpret_cast<int(*)(void*)>(entries[28]?entries[28]:reinterpret_cast<std::uintptr_t>(&NativeCount));
    const auto get=reinterpret_cast<void*(*)(void*,int)>(getter_thunk);
    const auto set=reinterpret_cast<void(*)(void*,std::uint64_t,float*)>(setter_thunk);
    for(int slot=0;;++slot) {
        SetLastError(0xa21);const auto count=get_count(material_graph+0x9000);
        if(GetLastError()!=0xb21 || count!=2)std::abort();
        if(slot>=count)break;
        SetLastError(0xa22);auto* mid=get(material_graph+0x9000,slot);
        if(GetLastError()!=0xb22 || mid!=((!slot || SlotCase("alias"))?material_graph+0xe000:RowMode()?material_graph+0xd000:nullptr))std::abort();
        forbid_observer_reads=false;
        if(mid) {
            auto color=bits;if(SlotCase("color"))color[0]^=1;
            const auto invoke=SlotCase("caller")?reinterpret_cast<void(*)(void*,std::uint64_t,float*)>(image+0x1f45940):set;
            SetLastError(0xa23);invoke(SlotCase("mid")?material_graph+0xd000:mid,
                name+unsigned(SlotCase("name")),reinterpret_cast<float*>(color.data()));
            if(GetLastError()!=0xb23)std::abort();forbid_observer_reads=false;
        }
    }
    if(SlotCase("overflow"))for(unsigned i=0;i<130;++i) {
        SetLastError(0xa22);if(get(provider,0)!=material_graph+0xe000 || GetLastError()!=0xb22)std::abort();
        forbid_observer_reads=false;
    }
    if(SlotCase("revoked") && !VirtualProtect(material_graph+0xe000,0x1000,PAGE_READWRITE,&old))std::abort();
    Field<void*>(overrides,0)=saved_override;Field<void*>(asset_rows,0x30)=saved_fallback;
    if(RowMode()) {
        if(RowCase("orphan")) {
            if(!VirtualProtect(row_pages,0x1000,PAGE_READWRITE,&old))std::abort();
            SetLastError(0xa24);
            reinterpret_cast<void(*)(void*,void*)>(image+0x1f1e500)(material_graph+0xe000,row_pages);
            if(GetLastError()!=0xb24)std::abort();forbid_observer_reads=false;
        }
        if(!VirtualFree(row_pages,0,MEM_RELEASE))std::abort();row_pages=nullptr;
        std::printf("C18 row native setters=%u changed=%u equal=%u dispatches=%u rows_released=1\n",
            setter_calls,changed_rows,equal_rows,row_dispatches);
    }
}
static void NativeHelper(void* actor,std::uint64_t key,float* color) {
    if(GetLastError()!=0xa12 || key!=name || color!=Color()
        || actor!=material_graph+0x8000)std::abort();
    ++helpers;
    if(SlotMode())Slots();
    if(Mode("material-shape-zero-correlation-close"))Horse::Deterministic::NativeReplayVfxCompletionObservation::ClosePhase(337);
    if(Mode("material-shape-zero-correlation-invalidate"))Horse::Deterministic::NativeReplayVfxCompletionObservation::InvalidateCollection18Combat("vfx-observation-fixture");
    if(Mode("material-shape-zero-correlation-callback-reentry") && helpers==1) {
        SetLastError(0x66c);
        Horse::Deterministic::DeterministicHookSet::CallbackExecutorDetour(callback_trace_collection,callback_descriptor_page);
    }
    if(Mode("material-shape-zero-correlation-reentry") && depth==1)Start();
    forbid_observer_reads=true;SetLastError(0xb12);
}
static void Helper() {
    const auto saved=Field<std::uintptr_t>(material_graph+0x8000,0x168);
    if(Mode("material-shape-zero-correlation-link-mismatch"))Field<std::uintptr_t>(material_graph+0x8000,0x168)=0;
    SetLastError(0xa12);
    reinterpret_cast<void(*)(void*,std::uint64_t,float*)>(entries[27])(
        material_graph+0x8000,name,Color());
    if(GetLastError()!=0xb12)std::abort();forbid_observer_reads=false;
    Field<std::uintptr_t>(material_graph+0x8000,0x168)=saved;
}
static void NativeStart(void* component,void* request) {
    if(GetLastError()!=0xa11 || component!=material_graph+0x7000 || request!=callback_descriptor_page)std::abort();
    ++starts;++depth;
    if(Mode("material-shape-zero-correlation-concurrent") && GetCurrentThreadId()==selected_thread) {
        std::thread worker(Start);worker.join();
    }
    if(!Mode("material-shape-zero-correlation-no-helper")) {
        const unsigned count=Mode("material-shape-zero-correlation-overflow")?65:SlotMode()?1:2;
        for(unsigned i=0;i<count;++i)Helper();
    }
    --depth;forbid_observer_reads=true;SetLastError(0xb11);
}
static void Start() {
    SetLastError(0xa11);
    reinterpret_cast<void(*)(void*,void*)>(entries[26])(material_graph+0x7000,callback_descriptor_page);
    if(GetLastError()!=0xb11)std::abort();forbid_observer_reads=false;
}
static void Run() {
    selected_thread=GetCurrentThreadId();
    if(RowMode())bits={1082130432,1081291571,1040217591,1065353216};
    if(Mode("material-shape-zero-correlation-unreadable")) {
        unreadable_color=static_cast<float*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));
        if(!unreadable_color)std::abort();
    }
    if(Mode("material-shape-zero-correlation-thread")) {std::thread worker(Start);worker.join();}
    else if(Mode("material-shape-zero-correlation-orphan"))Helper();
    else if(!Mode("material-shape-zero-correlation-no-start"))Start();
    std::printf("C18 correlation native starts=%u helpers=%u component=%llu actor=%llu provider=%llu name=%llu\n",
        starts,helpers,reinterpret_cast<std::uintptr_t>(material_graph+0x7000),
        reinterpret_cast<std::uintptr_t>(material_graph+0x8000),
        reinterpret_cast<std::uintptr_t>(material_graph+0x9000),name);
    if(SlotMode())std::printf("C18 material slots native count=%u getter=%u setter=%u actual_mid=%llu\n",
        count_calls,getter_calls,setter_calls,reinterpret_cast<std::uintptr_t>(material_graph+0xe000));
    SetLastError(0x66c);
    if(unreadable_color) {if(!VirtualFree(unreadable_color,0,MEM_RELEASE))std::abort();unreadable_color=nullptr;SetLastError(0x66c);}
}
}
static void NativeCallbackExecutor(void* c,void* argument) {
    using namespace Horse::Deterministic;
    if(c==callback_input_collection) {
        ++callback_input;
        auto* data=Field<PlayerInput*>(argument,0);
        data[0].held^=0x20;data[1].rising^=0x40;
    } else if(c==callback_trace_collection) {
        ++callback_trace;
        if(MaterialShapeMode() && GetLastError()!=0x66c)std::abort();
        if(ZeroSerialShapeMode()) {
            std::uint64_t premature[5]{};
            if(horsemod_read_collection18_observation_return(premature,5) || GetLastError()!=0x66c)std::abort();
        }
        if(Mode("unreadable-header")) {DWORD old{};if(!VirtualProtect(callback_hub_pages,0x1000,PAGE_READWRITE,&old))std::abort();}
        if(argument!=callback_descriptor_page)std::abort();
        if(Field<int>(c,0x64))return; // controlled nested native broadcast forwards too
        if(MaterialCensusMode())CheckMaterialCensusInputs();
        ++Field<int>(c,0x64);
        if(CorrelationMode())C18CorrelationProbe::Run();
        if(Mode("material-shape-zero-reentry") || Mode("material-shape-zero-type-reentry"))DeterministicHookSet::CallbackExecutorDetour(c,argument);
        if(Mode("material-shape-zero-overlap")) {
            std::thread worker([c,argument]{SetLastError(0x66c);DeterministicHookSet::CallbackExecutorDetour(c,argument);});
            worker.join();
        }
        if(Mode("material-shape-zero-close") || Mode("material-shape-zero-type-close"))NativeReplayVfxCompletionObservation::ClosePhase(210);
        if(Mode("material-shape-zero-writer"))WriterCall(11,&NativeCompact,c,true);
        auto* rows=Field<std::byte*>(c,0x40);
        if(Mode("same-address")) {
            auto* previous=Field<void*>(rows,0x20);
            WriterCall(12,&NativeRange,c,0,1,false);
            if(Field<std::byte*>(c,0x40)!=rows || Field<void*>(rows,0x20)==previous)std::abort();
            AppendCall(c);if(Field<int>(c,0x50)!=2)std::abort();
        } else if(Mode("same-handle-copy")) {
            std::array<std::byte,0x40> saved{};std::memcpy(saved.data(),rows,saved.size());
            const auto handle=Field<std::uint64_t>(Field<void*>(rows,0x20),0x20);
            WriterCall(19,&NativeRowCopy,Field<void*>(rows,0x20),rows);
            if(std::memcmp(saved.data(),rows,saved.size()) || Field<std::uint64_t>(Field<void*>(rows,0x20),0x20)!=handle)std::abort();
        } else if(Mode("remove"))HubRemoveCall(c);
        else if(Mode("reentry"))DeterministicHookSet::CallbackExecutorDetour(c,argument);
        else if(Mode("overflow"))for(unsigned i=0;i<34;++i)AppendCall(c);
        else if(Mode("overflow-cross")) {
            std::thread worker([c]{for(unsigned i=0;i<32;++i)WriterCall(11,&NativeCompact,c,true);});
            worker.join();WriterCall(11,&NativeCompact,c,true);
        }
        else if(Mode("backing"))WriterCall(13,&NativeBacking,c,2,64u,std::uintptr_t{0x40});
        else if(Mode("destroy"))WriterCall(14,&NativeCollectionDestroy,c);
        else if(Mode("hub-destroy"))WriterCall(15,&NativeHubDestroy,callback_hub_pages);
        else if(Mode("grow"))WriterCall(16,&NativeGrow,c,2);
        else if(Mode("capacity"))WriterCall(17,&NativeCapacity,c,64u);
        else if(Mode("row-clear"))WriterCall(18,&NativeRowClear,rows);
        else if(Mode("row-resize") || Mode("writer-return-revoked"))WriterCall(20,&NativeRowResize,rows,0x28);
        else if(Mode("row-backing"))WriterCall(21,&NativeRowBacking,rows,0,3u,std::uintptr_t{0x10});
        else if(Mode("unrelated-writer")) {
            alignas(8) std::array<std::byte,0x100> other{};Field<int>(other.data(),0x50)=0;
            AppendCall(other.data());
        } else if(!MaterialCensusMode() && !Mode("stable") && !Mode("preexisting") && !Mode("cross-thread")
            && !Mode("over-capacity") && !Mode("unreadable-header") && !Mode("hub-generation-mismatch")
            && !Mode("startup-active") && !Mode("recycled-parent") && !Mode("first-occurrence"))AppendCall(c,Mode("unknown"));
        if(Mode("close-inside"))AppendCall(c);
        if(Mode("recursive-append") && (Field<int>(c,0x50)!=3 || callback_appends!=1))std::abort();
        if(!c18_pages_revoked)--Field<int>(c,0x64);
        DWORD old{};
        if(MaterialCensusMode()) {
            CheckMaterialCensusInputs();
            if(!VirtualProtect(material_graph,material_graph_bytes,PAGE_NOACCESS,&old))std::abort();
            std::puts("material native forwarding PASS: input_bytes_unchanged=1 graph_revoked=1");
        }
        // Native return bookkeeping must not read any borrowed descriptor,
        // collection, row or callback storage. Revoke these actual pages.
        if(!VirtualProtect(callback_hub_pages,0x6000,PAGE_NOACCESS,&old)
            || !VirtualProtect(callback_descriptor_page,0x1000,PAGE_NOACCESS,&old))std::abort();
        forbid_observer_reads=true;
        if(MaterialShapeMode())SetLastError(0x77c);
    } else ++callback_unrelated;
}
static int Collection18AppendBoundary() {
    using namespace Horse::Deterministic;
    DeterministicHookSet owner{};
    owner.callback_executor_trampoline_=reinterpret_cast<std::uint64_t>(&NativeCallbackExecutor);
    DeterministicHookSet::active_.store(&owner);
    alignas(8) std::array<std::byte,0x1280> battle{};
    CallbackFixtureObservation observation{reinterpret_cast<std::uintptr_t>(battle.data())};
    CallbackFixtureCapture capture{};capture.observation=&observation;
    DeterministicHookSet::active_outer_capture_=&capture;
    callback_input_collection=battle.data()+Schema::Sc6FrameLayout::manager_input_filter_callbacks;
    PlayerInput input[2]{{1,2},{3,4}};
    struct {PlayerInput* data;int count,capacity;} input_header{input,2,2};
    DeterministicHookSet::CallbackExecutorDetour(reinterpret_cast<void*>(1),nullptr);
    DeterministicHookSet::CallbackExecutorDetour(callback_input_collection,&input_header);
    if(callback_unrelated!=1 || callback_input!=1 || !capture.input_filter_observed
        || capture.input_filter_invocations!=1 || capture.pre_filter_inputs[0]!=PlayerInput{1,2}
        || capture.post_filter_inputs[0]!=PlayerInput{0x21,2}
        || capture.post_filter_inputs[1]!=PlayerInput{3,0x44})return 80;
    callback_hub_pages=VirtualAlloc(nullptr,0x6000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    callback_descriptor_page=VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!callback_hub_pages || !callback_descriptor_page)return 81;
    auto* hub=static_cast<std::byte*>(callback_hub_pages);
    callback_trace_collection=hub+0x810;
    Field<std::uintptr_t>(hub,0)=image+0x337c2c8;
    Field<int>(hub,0xc)=4;RC::Unreal::FUObjectArray::items[4]={hub,41};
    Field<std::uintptr_t>(hub,0x28)=image+0x337c4f8;
    Field<void*>(callback_trace_collection,0x40)=hub+0x1000;
    Field<int>(callback_trace_collection,0x50)=2;
    Field<int>(callback_trace_collection,0x54)=64;
    if(Mode("unknown-layout") || Mode("first-occurrence"))Field<std::uintptr_t>(hub,0x28)=image+1;
    if(Mode("over-capacity"))Field<int>(callback_trace_collection,0x50)=65;
    if(Mode("hub-generation-mismatch"))RC::Unreal::FUObjectArray::items[4]={receiver.data(),42};
    for(unsigned i=0;i<64;++i) {
        auto* row=hub+0x1000+i*0x40;auto* callable=hub+0x3000+i*0x30;
        Field<void*>(row,0x20)=callable;Field<int>(row,0x30)=3;
        Field<std::uintptr_t>(callable,0)=image+0x374b760;
        Field<int>(callable,8)=7;Field<int>(callable,12)=11;
        Field<std::uintptr_t>(callable,0x10)=image+0x3c5360;
        Field<std::uint64_t>(callable,0x20)=0x1100+i;
    }
    Field<std::uintptr_t>(receiver.data(),0)=image+0x326b2e0;
    Field<int>(callback_descriptor_page,0)=0;
    Field<unsigned char>(callback_descriptor_page,4)=1;
    Field<int>(callback_descriptor_page,8)=30;
    Field<int>(callback_descriptor_page,0x10)=8;
    Field<unsigned char>(callback_descriptor_page,0x14)=1;
    Field<float>(callback_descriptor_page,0x18)=1.f;
    if(MaterialCensusMode())SetupMaterialCensus();
#ifdef HORSE_REAL_MATERIAL_DETOUR
    if(CorrelationMode() && entries[29]==image+0x1dc8220) {
        // Real relocated JS (negative), native JGE (out of override range),
        // and fallthrough; outside selection, hence no synthetic child receipt.
        for(int slot:{-1,2,0}) {
            SetLastError(0xa22);
            auto* result=reinterpret_cast<void*(*)(void*,int)>(entries[29])(material_graph+0x9000,slot);
            if(GetLastError()!=0xb22 || result!=(slot==0?material_graph+0xd000:nullptr))std::abort();
            forbid_observer_reads=false;
        }
        C18CorrelationProbe::getter_calls=0;
        std::puts("actual getter trampoline JS/JGE/fallthrough PASS outside selected interval");
    }
#endif
    // Real return PC400A9A for the production owner, not a fixture override of
    // _ReturnAddress. The synthetic image is the external native call service.
    auto* jump=reinterpret_cast<unsigned char*>(image+0x1000);
    jump[0]=0x48;jump[1]=0xb8;
    const auto detour=reinterpret_cast<std::uint64_t>(&DeterministicHookSet::CallbackExecutorDetour);
    std::memcpy(jump+2,&detour,8);jump[10]=0xff;jump[11]=0xe0;
    auto* thunk=reinterpret_cast<unsigned char*>(image+0x400a80);
    std::memset(thunk,0x90,0x20);const unsigned char prologue[]{0x48,0x83,0xec,0x28};
    std::memcpy(thunk,prologue,4);thunk[0x15]=0xe8;
    const auto relative=static_cast<std::int32_t>(0x1000-0x400a9a);
    std::memcpy(thunk+0x16,&relative,4);
    const unsigned char epilogue[]{0x48,0x83,0xc4,0x28,0xc3};
    std::memcpy(thunk+0x1a,epilogue,5);
    DWORD old{};
    if(!VirtualProtect(jump,0x1000,PAGE_EXECUTE_READ,&old)
        || !VirtualProtect(reinterpret_cast<void*>(image+0x400000),0x1000,PAGE_EXECUTE_READ,&old))return 82;
    FlushInstructionCache(GetCurrentProcess(),jump,12);FlushInstructionCache(GetCurrentProcess(),thunk,0x20);
    if(Mode("unreadable-header") && !VirtualProtect(callback_hub_pages,0x1000,PAGE_NOACCESS,&old))std::abort();
    if(ZeroSerialShapeMode()) {
        SetupCombatContext();
        if(Mode("material-shape-zero-combat-prearm")) {
            const auto setup=CombatSample(23,553);
            if(!combat_bridge.ObserveC18CombatSample(setup) || combat_bridge.c18_diagnostic_arm_)std::abort();
        } else {
            const bool reject=Mode("material-shape-zero-combat-arm-vfx") || Mode("material-shape-zero-combat-arm-unknown");
            const bool accepted=combat_bridge.ObserveC18CombatSample(CombatSample());
            if(accepted==reject || bool(combat_bridge.c18_diagnostic_arm_)==reject)std::abort();
        }
        if(!Mode("material-shape-zero-combat-prearm"))CombatSample(177,707);
        if(Mode("material-shape-zero-combat-scene")) {
            combat_bridge.replay_scene_ready_=false;
            if(combat_bridge.ObserveC18CombatSample(CombatSample(177,707)))std::abort();
        }
        if(Mode("material-shape-zero-combat-replay")) {
            auto sample=CombatSample(177,707);sample.source_active=false;
            Field<unsigned char>(combat_context.data()+0x2000,0x398)=0;
            if(combat_bridge.ObserveC18CombatSample(sample))std::abort();
        }
        if(Mode("material-shape-zero-combat-missing")) {
            if(combat_bridge.ObserveC18CombatSample(CombatSample(221,751)))std::abort();
        }
        if(Mode("material-shape-zero-combat-epoch"))Field<std::uint64_t>(reinterpret_cast<void*>(image),0x4197170)=699;
        if(Mode("material-shape-zero-combat-late"))Field<unsigned>(reinterpret_cast<void*>(image),0x470d0c4)=221;
        if(Mode("material-shape-zero-combat-player"))Field<void*>(combat_context.data(),0x400)=nullptr;
        if(Mode("material-shape-zero-combat-generation"))++RC::Unreal::FUObjectArray::items[24].serial;
        if(Mode("material-shape-zero-combat-phase"))Field<unsigned char>(combat_context.data(),0x1480)=0;
    }
    if(MaterialShapeMode())SetLastError(0x66c);
    if(Mode("material-shape-zero-combat-thread")) {
        std::thread worker([thunk]{
            SetLastError(0x66c);
            reinterpret_cast<void(*)(void*,void*)>(thunk)(callback_trace_collection,callback_descriptor_page);
            if(GetLastError()!=0x77c)std::abort();
        });worker.join();
        SetLastError(0x77c);
    }
    else if(Mode("preexisting") || Mode("startup-active"))WriterCall(11,&NativeCompact,callback_trace_collection,true);
    else if(Mode("recycled-parent")) {
        worker_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);worker_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        recycled_worker=std::thread([]{WriterCall(11,&NativeCompact,reinterpret_cast<void*>(1),true);});
        if(WaitForSingleObject(worker_entered,5000)!=WAIT_OBJECT_0)std::abort();
        WriterCall(11,&NativeCompact,callback_trace_collection,true);
        CloseHandle(worker_entered);CloseHandle(worker_release);
    }
    else if(Mode("cross-thread")) {
        worker_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);worker_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::thread worker([]{WriterCall(11,&NativeCompact,callback_trace_collection,true);});
        if(WaitForSingleObject(worker_entered,5000)!=WAIT_OBJECT_0)std::abort();
        reinterpret_cast<void(*)(void*,void*)>(thunk)(callback_trace_collection,callback_descriptor_page);
        SetEvent(worker_release);worker.join();CloseHandle(worker_entered);CloseHandle(worker_release);
    } else reinterpret_cast<void(*)(void*,void*)>(thunk)(callback_trace_collection,callback_descriptor_page);
    if(MaterialShapeMode()) {
        if(GetLastError()!=0x77c || shape_resource_reads)std::abort();
        std::printf("material shape LastError PASS: incoming=0x66c outgoing=0x77c resource_reads=%u header_reads=%u world_calls=%u\n",
            shape_resource_reads,shape_header_reads,shape_world_calls);
    }
    if(Mode("material-shape-zero-combat-prearm")) {
        std::uint64_t setup_receipt[5]{};
        const bool ready=horsemod_read_collection18_observation_return(setup_receipt,5);
        std::printf("combat selection pre-arm native return ready=%u forwarded=%u\n",ready,callback_trace);
        if(ready)return 89; // a setup occurrence must not consume combat selection
        forbid_observer_reads=false;
        if(!VirtualProtect(callback_hub_pages,0x6000,PAGE_READWRITE,&old)
            || !VirtualProtect(callback_descriptor_page,0x1000,PAGE_READWRITE,&old)
            || !VirtualProtect(material_graph,material_graph_bytes,PAGE_READWRITE,&old))std::abort();
        if(!combat_bridge.ObserveC18CombatSample(CombatSample())) {
            std::printf("production combat arm rejected: %s\n",combat_bridge.failure?combat_bridge.failure:"missing");
            return 90;
        }
        CombatSample(177,707);SetLastError(0x66c);
        reinterpret_cast<void(*)(void*,void*)>(thunk)(callback_trace_collection,callback_descriptor_page);
        if(GetLastError()!=0x77c)std::abort();
    }
    if(RejectedCombatSelection()) {
        std::uint64_t receipt[13]{};
        if(Mode("material-shape-zero-combat-post-scene")) {
            if(!horsemod_read_collection18_combat_return("vfx-observation-fixture",receipt,13))std::abort();
            combat_bridge.replay_scene_ready_=false;
            if(combat_bridge.ObserveC18CombatSample(CombatSample(177,707)))std::abort();
        }
        if(horsemod_read_collection18_combat_return("vfx-observation-fixture",receipt,13)
            || combat_bridge.c18_diagnostic_logged_)std::abort();
        std::printf("combat selection rejected: forwarded=%u bridge_failure=%s after_return_reads=%u\n",
            callback_trace,combat_bridge.failure?combat_bridge.failure:"none",forbidden_observer_reads.load());
    } else if(ZeroSerialShapeMode() && !Mode("material-shape-zero-correlation-invalidate")
        && !C18CorrelationProbe::SlotCase("invalidate") && !C18CorrelationProbe::RowCase("invalidate"))ProductionDiagnosticReturn(); // C18 input pages already revoked
    forbid_observer_reads=false;
    if(Mode("first-occurrence")) {
        auto read=[](auto& bytes) {
            HANDLE file=CreateFileW(L"vfx_completion_observation.json",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            DWORD size{};if(file==INVALID_HANDLE_VALUE || GetFileSize(file,nullptr)>=bytes.size()
                || !ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&size,nullptr))std::abort();
            CloseHandle(file);return size;
        };
        std::array<char,32768> before{},after{};const auto count=read(before);
        if(!VirtualProtect(callback_hub_pages,0x6000,PAGE_READWRITE,&old)
            || !VirtualProtect(callback_descriptor_page,0x1000,PAGE_READWRITE,&old))std::abort();
        Field<std::uintptr_t>(hub,0x28)=image+0x337c4f8;
        reinterpret_cast<void(*)(void*,void*)>(thunk)(callback_trace_collection,callback_descriptor_page);
        forbid_observer_reads=false;
        if(read(after)!=count || before!=after)std::abort();
        std::puts("first occurrence bytes preserved");
    }
    if(callback_trace!=((Mode("reentry") || Mode("first-occurrence") || Mode("material-shape-zero-reentry")
        || Mode("material-shape-zero-type-reentry") || Mode("material-shape-zero-overlap") || Mode("material-shape-zero-combat-prearm")
        || Mode("material-shape-zero-correlation-callback-reentry"))?2u:1u) || DeterministicHookSet::callbacks_in_flight_.load())return 83;
    if(forbidden_observer_reads.load())return 85;
    // Verify the external API probe catches even a silently failed guarded
    // read from a revoked page; then distinguish that control from production.
    forbid_observer_reads=true;char byte{};SIZE_T copied{};
    if(ProbeReadProcessMemory(GetCurrentProcess(),callback_hub_pages,&byte,1,&copied)
        || forbidden_observer_reads.load()!=1)return 86;
    forbid_observer_reads=false;forbidden_observer_reads=0;
    std::puts("return read attempts=0 guarded-read control=1");
    DeterministicHookSet::active_outer_capture_=nullptr;DeterministicHookSet::active_.store(nullptr);
    ProductionCompletion();
    std::puts("callback owner forwarding PASS: unrelated=1 input=1 collection18=1");
    if(Mode("recursive-append"))std::puts("native append PASS: count=2->3 depth=1 calls=1 borrowed_pages_revoked=1");
    std::printf("writer native PASS: executor=%u append=%u remove=%u pages_revoked=1 sites=",callback_trace,callback_appends,hub_removes);
    for(auto n:writer_native_calls)std::printf("%u,",n);std::puts("");
    return 0;
}
static unsigned hub_adds{};
static std::uintptr_t NativeHubRemove(void* c,void* receiver_arg) {
    if(GetLastError()!=0x669 || receiver_arg!=receiver.data())std::abort();
    ++hub_removes;
    if(c==callback_trace_collection) {
        auto* row=Field<std::byte*>(c,0x40);Field<int>(row,0x30)=0;Field<void*>(row,0x20)=nullptr;
    }
    if(c==disable_hub.data()+0xa0) {
        Field<int>(hub_entries.data(),0x30)=0;
        Field<void*>(hub_entries.data(),0x20)=reinterpret_cast<void*>(1);
    }
    SetLastError(0x779);return 0xabcdef0123456789ull;
}
static void HubRemoveCall(void* c) {
    const auto target=entries[9]?entries[9]:reinterpret_cast<std::uint64_t>(&NativeHubRemove);
    SetLastError(0x669);
    if(reinterpret_cast<std::uintptr_t(*)(void*,void*)>(target)(c,receiver.data())!=0xabcdef0123456789ull
        || GetLastError()!=0x779)std::abort();
}
static void* NativeHubRegister(void* c,void* out,void* target,void* callable,unsigned char bound) {
    if(GetLastError()!=0x66a || target!=receiver.data() || callable!=reinterpret_cast<void*>(image+0x3c5250) || bound!=1)std::abort();
    ++hub_adds;*static_cast<std::uint64_t*>(out)=0x789;
    if(!std::strcmp(mode,"producers-owners-hub-nested"))HubRemoveCall(c);
    SetLastError(0x77a);return out;
}
static void NativeHub(void* hub,void* request) {
    if(hub!=disable_hub.data()+0x28 || request!=&payload || GetLastError()!=0x667)std::abort();
    const auto call=entries[4]?entries[4]:reinterpret_cast<std::uint64_t>(&NativeDisable);
    SetLastError(0x666);
    reinterpret_cast<void(*)(void*,void*,unsigned char)>(call)(receiver.data(),request,1);
    if(GetLastError()!=0x776)std::abort();
    if(!std::strncmp(mode,"producers-owners-hub",20)) {
        if(!std::strcmp(mode,"producers-owners-hub-close"))ProductionCompletion();
        if(!std::strcmp(mode,"producers-owners-hub-writers") || !std::strcmp(mode,"producers-owners-hub-nested")) {
            std::uint64_t handle{};const auto fn=entries[8]?entries[8]:reinterpret_cast<std::uint64_t>(&NativeHubRegister);
            SetLastError(0x66a);
            if(reinterpret_cast<void*(*)(void*,void*,void*,void*,unsigned char)>(fn)(disable_hub.data()+0xa0,&handle,receiver.data(),reinterpret_cast<void*>(image+0x3c5250),1)!=&handle
                || handle!=0x789 || GetLastError()!=0x77a)std::abort();
            HubRemoveCall(disable_hub.data()+0xa0);
        } else if(!std::strcmp(mode,"producers-owners-hub-threaded")) {
            std::thread other([]{HubRemoveCall(disable_hub.data()+0xa0);});other.join();
        } else if(!std::strcmp(mode,"producers-owners-hub-overflow")) {
            for(unsigned i=0;i<17;++i)HubRemoveCall(disable_hub.data()+0xa0);
        }
        // Unrelated collections and calls after broadcast return must forward
        // without joining the selected collection's writer inventory.
        HubRemoveCall(manager.data());
    }
    if(!std::strcmp(mode,"producers-owners-hub-onearg")) {
        // The native dependency may retire/reuse the callback and receiver.
        // The witness must describe its pre-original copy, without return reads.
        std::memset(hub_callback_b.data(),0xcd,hub_callback_b.size());
        Field<void*>(hub_entries.data()+0x40,0x20)=reinterpret_cast<void*>(1);
        Field<int>(hub_receiver_b.data(),0xc)=-1;
        RC::Unreal::FUObjectArray::items[11].serial=72;
    }
    SetLastError(0x777);
}
static void NativeTask(void* payload_address,unsigned thread,void* completion_reference) {
    auto* task=static_cast<std::byte*>(payload_address)-0x10;
    if(completion_reference!=task+0x40 || thread!=2 || GetLastError()!=0x668)std::abort();
    if(task==owner_task.data()) {
        if((!std::strcmp(mode,"producers-owners-registration-nested") && owner_recursion++<1)
            || (!std::strcmp(mode,"producers-owners-registration-overflow") && owner_recursion++<5)) {
            RunTask(task);SetLastError(0x778);return;
        }
        if(!std::strncmp(mode,"producers-owners-registration",29))RegisterFromTask();
        if(!std::strcmp(mode,"producers-owners-overflow") && owner_recursion++<4) {
            RunTask(task);SetLastError(0x778);return;
        }
        const auto call=entries[7]?entries[7]:reinterpret_cast<std::uint64_t>(&NativeHub);
        SetLastError(0x667);reinterpret_cast<void(*)(void*,void*)>(call)(disable_hub.data()+0x28,&payload);
        if(GetLastError()!=0x777)std::abort();
    } else if(task==particle_task.data())CompleteParticle();
    else std::abort();
    // Model native completion/recycling. Observation must not reread on return.
    std::memset(task,0xcd,0x50);SetLastError(0x778);
}
static void RunTask(void* task) {
    if(task==owner_task.data() && (!std::strcmp(mode,"producers-owners-custom-entry")
        || !std::strcmp(mode,"producers-owners-custom-resume") || !std::strncmp(mode,"producers-owners-hub",20)
        || !std::strncmp(mode,"producers-owners-registration-custom-",36))) {
        // Execute the exact witness declaration extracted from the production
        // manager branch/continuation. Native task body is deliberately bypassed.
        using Horse::Deterministic::ReplayVfxExecutionScope;
        auto* pending_task_=static_cast<std::byte*>(task);
        unsigned current_thread=2;
        alignas(8) std::array<std::byte,0x18> named_thread_storage{};
        void* named_thread_=named_thread_storage.data();
        Field<unsigned>(named_thread_,0x10)=2;
        if(!std::strcmp(mode,"producers-owners-custom-entry") || !std::strncmp(mode,"producers-owners-hub",20)
            || !std::strcmp(mode,"producers-owners-registration-custom-entry")) {
#include "vfx_owner_manager_entry.inl"
            SetLastError(0x668);NativeTask(pending_task_+0x10,2,pending_task_+0x40);
        } else {
#include "vfx_owner_manager_resume.inl"
            SetLastError(0x668);NativeTask(pending_task_+0x10,2,pending_task_+0x40);
        }
        if(GetLastError()!=0x778)std::abort();return;
    }
    const auto call=entries[6]?entries[6]:reinterpret_cast<std::uint64_t>(&NativeTask);
    SetLastError(0x668);reinterpret_cast<void(*)(void*,unsigned,void*)>(call)(static_cast<std::byte*>(task)+0x10,2,static_cast<std::byte*>(task)+0x40);
    if(GetLastError()!=0x778)std::abort();
}
static void NativeCompleteParticle(void* component) {
    if(component!=particle.data() || GetLastError()!=0x665)std::abort();
    ++completions;
    // First destructive effect consumes the entry-owned async reference.
    *reinterpret_cast<std::uintptr_t*>(particle.data()+0xa70)=0;
    Dispatch();
    if(!std::strcmp(mode,"producers-close"))ProductionCompletion();
    if(!std::strcmp(mode,"producers-crash"))ExitProcess(0);
    SetLastError(0x775);
}
static void NativeDisable(void* handler,void* event,unsigned char resolve) {
    if(handler!=receiver.data() || event!=&payload || resolve!=1 || GetLastError()!=0x666)std::abort();
    ++disables;
    if(!std::strcmp(mode,"producers-threaded")){std::thread unrelated([]{Dispatch();});unrelated.join();}
    if(!std::strcmp(mode,"producers-owners-threaded")) {
        std::thread unrelated([]{CompleteParticle();});unrelated.join();
    }
    if(!std::strncmp(mode,"producers-owners",16))RunTask(particle_task.data());
    else CompleteParticle();
    // Controlled dependency models the synchronous reflected tail; it cannot
    // manufacture a production producer scope or attribution record.
    ++tails;Dispatch();SetLastError(0x776);
}
static void CompleteParticle() {
    const auto target=entries[5]?entries[5]:reinterpret_cast<std::uint64_t>(&NativeCompleteParticle);
    SetLastError(0x665);reinterpret_cast<void(*)(void*)>(target)(particle.data());
    if(GetLastError()!=0x775)std::abort();
}

// ABI: RCX collection, RDX UObject*, R8 unused descriptor, R9 full FName; void.
// Only the external append is simulated, not native weak resolution/pruning.
static void NativeRegister(void* c,void* object,std::uint64_t d,std::uint64_t name) {
    if(c!=collection || object!=receiver.data() || d!=descriptor || name!=function_name || GetLastError()!=0x661)
        std::abort();
    ++registrations;
    rows[0]={7,11,name};collection->count=1;
    // Reentry must survive: an observer may not hold its write lock over native.
    if(nested)Dispatch();
    SetLastError(0x771);
}
static void RegisterFromTask() {
    const auto call=[] {
        const auto target=entries[0]?entries[0]:reinterpret_cast<std::uint64_t>(&NativeRegister);
        SetLastError(0x661);
        reinterpret_cast<void(*)(void*,void*,std::uint64_t,std::uint64_t)>(target)(collection,receiver.data(),descriptor,function_name);
        if(GetLastError()!=0x771)std::abort();
    };
    if(!std::strcmp(mode,"producers-owners-registration-generation"))RC::Unreal::FUObjectArray::items[7].serial=12;
    if(!std::strcmp(mode,"producers-owners-registration-thread")) {std::thread worker(call);worker.join();}
    else call();
    if(!std::strcmp(mode,"producers-owners-registration-close"))ProductionCompletion();
    if(!std::strcmp(mode,"producers-owners-registration-capacity"))for(unsigned i=0;i<16;++i)call();
    RC::Unreal::FUObjectArray::items[7].serial=11;
    std::puts("registration inside native task forwarded");
}
// ABI: RCX collection, RDX ProcessEvent parameter buffer; void.
static void NativeDispatch(void* c,void* argument) {
    if(c==attachment.data()+0x1b8) {
        if(argument!=&attachment_argument || GetLastError()!=0x662)std::abort();
        ++attachment_callbacks;SetLastError(0x772);return;
    }
    if(c!=collection || argument!=&payload || collection->count!=(truncate_rows?6:full_rows?4:1)
        || collection->data[0].index!=7 || collection->data[0].name!=function_name || GetLastError()!=0x662)std::abort();
    ++dispatches;
    if(attachment_ready) {
        const auto call=entries[1]?entries[1]:reinterpret_cast<std::uint64_t>(&NativeDispatch);
        SetLastError(0x662);
        reinterpret_cast<void(*)(void*,void*)>(call)(attachment.data()+0x1b8,&attachment_argument);
        if(GetLastError()!=0x772)std::abort();
    }
    if(helpers_in_dispatch) {
        Prune(c);
        if(CopyCollection(c,&source_collection)!=copy_result)std::abort();
    }
    // Marker at the native snapshot boundary. No fixture guard runs here.
    std::printf("native-snapshot serial=%d thread=%lu\n",rows[0].serial,GetCurrentThreadId());
    std::fflush(stdout);
    if(!std::strcmp(mode,"phase-close-pending") && nested)ProductionCompletion();
    if((!std::strcmp(mode,"phase-close-overlap") || !std::strcmp(mode,"writers-overlap"))
        && GetCurrentThreadId()==overlap_worker.load()) {
        SetEvent(worker_entered);
        if(WaitForSingleObject(worker_release,5000)!=WAIT_OBJECT_0)std::abort();
    }
    if(!std::strcmp(mode,"crash"))ExitProcess(0);
    if(expire_on_return) {
        RC::Unreal::FUObjectArray::items[1].object=nullptr;
        collection->data=reinterpret_cast<Row*>(1);
    }
    SetLastError(0x772);
}
// Verified helper ABIs. Deliberately return a distinct sentinel to catch a
// wrapper synthesizing destination instead of forwarding the original RAX.
static void NativePrune(void* c) {
    if(c!=expected_writer_collection || !c || GetLastError()!=0x663)std::abort();
    ++prunes;auto* h=static_cast<Header*>(c);
    if(h->count) --h->count;
    SetLastError(0x773);
}
static void* NativeCopy(void* c,void* source) {
    if(c!=expected_writer_collection || !c || source!=&source_collection || GetLastError()!=0x664)std::abort();
    ++copies;*static_cast<Header*>(c)=*static_cast<Header*>(source);
    if(close_in_copy)ProductionCompletion();
    if(expire_in_copy) {
        RC::Unreal::FUObjectArray::items[1].serial=32;
        static_cast<Header*>(c)->data=reinterpret_cast<Row*>(1);
    }
    SetLastError(0x774);return copy_result;
}
// Isolated settlement slice: actual extracted host/particle decisions, with
// independently scheduled external native services. No trace admission bypass,
// material completion predicate, synthetic B recovery, or deferred-release model.
namespace MaterialSettlement {
using Horse::Deterministic::NativeReplayMaterialTaskGuard;
using Horse::Deterministic::RestoreSettlement;
using Horse::Deterministic::ReplaySeekOwnership;
using Horse::Deterministic::Status;
using Horse::Deterministic::FailureCode;
static void Require(bool value) {if(!value)std::abort();}
namespace RC {
enum class LogLevel {Default,Warning};
struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};
static const char* to_generic_string(const char* text){return text;}
}
struct ReplayGpuCompletion {
    using Clock=std::chrono::steady_clock;
    enum class Result {Pending,Complete,TimedOut,Cancelled,Error};
    Result result=Result::Pending;bool in_flight{},signaled{};
    bool retired()const {return !in_flight;}
    HRESULT Submit(void*,Clock::time_point) {Require(!in_flight);in_flight=true;return S_OK;}
    Result Poll(void*,Clock::time_point) {
        if(signaled){in_flight=false;result=Result::Complete;}return result;
    }
    HRESULT error()const {return S_OK;}
};
struct Sc6ReplayParticleCopy {
#include "vfx_material_particle_phase.inl"
    struct Witness {
        Phase phase=Phase::Installed;
        bool native_write_uncommitted=true,execution_work_complete{},deadline_expired{};
        bool timeout_retired{},cancel_retired{};std::size_t bytes{};
        bool reflection_retained=true,target_prepared=true;
    } witness_;
    const Witness& witness()const {return witness_;}
    struct Identity {
        std::uint64_t session=1,tick=210,epoch=1,interval=210,source=1,source_revision{};
        std::uint8_t execution_phase{};
    };
    using Image=std::shared_ptr<Identity>;
    Image image_=std::make_shared<Identity>();
    const Image& captured_image()const {return image_;}
    static const Identity* identity(const Image& image){return image.get();}
    ReplayGpuCompletion completion_;
    ReplayGpuCompletion::Clock::time_point request_deadline_;
    struct Context {void* Get(){return nullptr;}} context_;
    std::uintptr_t base_=image,world_=1;
    std::uint64_t execution_boundary_epoch_{};int execution_selection_{};
    bool execution_started_=true,execution_settled_{},registry_executing_{},birth_dirty_{};
    bool lighting_executing_{},lighting_execution_settled_{},visibility_executing_{},visibility_execution_settled_{};
    RestoreSettlement execution_settlement_=RestoreSettlement::RecoverOriginal;
    // Other participant domains are empty in this boundary fixture. These
    // services cannot read the native material scheduler or certify its work.
    struct Registry {
        bool settled()const {std::abort();}
        bool SettleExecution(){std::abort();}
    } birth_registry_storage_;
    bool Bindings(void* world,bool)const {return world==reinterpret_cast<void*>(world_);}
    bool PrepareTransfer()const {return true;}
    bool BirthOwnersBinding()const {return true;}
    bool CaptureExecutionRegistry(){std::abort();}
    bool BirthRegistryMatchesExcluded(){std::abort();}
    bool SettleLightingExecution(std::size_t,RestoreSettlement){std::abort();}
    bool SettleVisibilityExecution(std::size_t){std::abort();}
    bool RetainExecutionReflection(std::size_t)const {return true;}
    bool ReadSelection(int& selection)const {selection=0;return true;}
    bool ReadBytes(void* out,const void* from,std::size_t size)const {
        SIZE_T copied{};return ::ReadProcessMemory(GetCurrentProcess(),from,out,size,&copied)&&copied==size;
    }
    void Fail(HRESULT){std::abort();}
    void StartRetirementProbe(bool){std::abort();}
    bool CanRetireAfterExecutionDrain()const {std::abort();}
    bool SettleExecution(std::size_t,RestoreSettlement)noexcept;
    bool DrainExecutionWork()noexcept;
    void Poll()noexcept;
};
#include "vfx_material_particle_methods.inl"
static std::array<unsigned,3> native_retirements{};
static unsigned material_pending_at_mutation{};
static void ObserveRetirementMutation();
static std::uint64_t host_clock=100;
static std::uint64_t HostClock(){return host_clock;}
struct Sc6ReplayExecutor {enum class Phase : std::uint8_t {Idle};};
struct ReplayTickIndex {
    enum class Phase {Idle,Recording,Releasing};
    struct Witness {Phase phase=Phase::Idle;} state;
    const Witness& witness()const {return state;}
};
struct PreparationReached {};
static void (*trace_start_teardown_probe)(){};
struct Sc6ReplayHost {
#include "vfx_material_capture_types.inl"
    InteriorPhase interior_phase_=InteriorPhase::Holding;
    CaptureWitness capture_operation_{};
    struct Seek {SeekWitness witness;} seek_;
    bool corrected_capture_{};
    // Native-shaped inputs for the complete Request and direct Prepare
    // admission prefixes. These independent domains contain no material
    // predicate, producer exclusion, or successful preparation substitute.
    struct Checkpoint : std::enable_shared_from_this<Checkpoint> {
        bool valid=true;std::uint64_t session=1,epoch=1,source=1;
        Sc6ReplayParticleCopy::Image gpu;
        bool hud=true,traces=true,vfx_handler=true,display=true;
        struct Gameplay {std::vector<std::byte> bytes{std::byte{1}};} gameplay;
        PauseBoundary boundary=PauseBoundary::CompletedApplication;
        bool task{},event{};
        struct Execution {Sc6ReplayExecutor::Phase phase=Sc6ReplayExecutor::Phase::Idle;
            std::uint64_t tick=210,interval=210;} execution;
        struct Revision {std::uint64_t id{};};std::shared_ptr<Revision> input_revision;
        std::array<std::uint64_t,3> callback_stamp{};
    };
    using CheckpointHandle=std::shared_ptr<const Checkpoint>;
    ReplayTickIndex index_;
    bool checkpoint_restoring_{},depth_{},surface_event_{};
    std::atomic<bool> particle_command_pending_{};
    PauseBoundary pause_boundary_=PauseBoundary::CompletedApplication;
    enum class ApplicationPhase {Idle};ApplicationPhase application_phase_=ApplicationPhase::Idle;
    struct Executor {bool idle()const{return true;}bool arena_empty()const{return true;}} executor_;
    std::uint64_t checkpoint_session_=1;
    bool CheckBinding()const{return true;}bool engine_idle()const{return true;}
    static bool RestoreCheckpointDisplay(const Checkpoint& target){return target.display;}
    std::size_t AdmissionRemaining()const{return 1024*1024;}
    struct InputSource {Status Validate(std::uint64_t,const std::shared_ptr<Checkpoint::Revision>&)const{return Status::success();}} source_;
    InputSource* input_source_=&source_;
    struct CallbackAdmission {
        Status Validate(const std::array<std::uint64_t,3>&)const{return Status::success();}
        std::array<std::uint64_t,3> stamp()const{return {};}
        struct Diagnostic {const char* check="fixture";unsigned owner{},count{};};
        Diagnostic diagnostic()const{return {};}
    } callbacks_;
    CallbackAdmission* callback_admission_=&callbacks_;
    struct TraceRetirement {
        template<class... T>Status RetireHiddenRenderingForUndo(T&&...){ObserveRetirementMutation();++native_retirements[0];return Status::success();}
        template<class... T>Status RetireAddedForUndo(T&&...){++native_retirements[2];return Status::success();}
        int PrivateChildren(){return 0;}
    };
    struct HistoricalRestore {
#include "vfx_material_receipt_fields.inl"
        struct Witness {
            FailureCode failure{};const char* participant{};
            RestoreOperationPhase phase=RestoreOperationPhase::Held;
            bool pending{},original_recovered{};
            std::uint64_t target_tick{},original_tick{};
        } witness;
        CheckpointHandle target;bool preparing{};
        TraceRetirement traces;
        struct Undo {TraceRetirement owner;TraceRetirement* traces=&owner;int vfx{};} undo;
        int SurvivingTraces(){return 0;}
        struct Execution {
            enum class Render {Retained,CoordinatesPending,CoordinatesComplete,HandedOff,DrainPending,Drained,Settled};
            enum class Capture {Empty,RetiringOwners,Current};
            enum class Participant {Retained,HandedOff,Settled};
            Render render=Render::DrainPending;Capture capture=Capture::Current;
            std::array<Participant,11> participants{};
            std::optional<unsigned> settlement_failure_after;
            ReplaySeekOwnership ownership{217,217};
            struct Retirement {bool admitted{};} retirement;
            TraceRetirement traces;int trace_render_retirement{},trace_retirement{};
            struct Births {
                template<class... T>Status RetireCurrentForUndo(T&&...){++native_retirements[1];return Status::success();}
            } births;
        } value;
        Execution* execution=&value;
        // External native teardown service. The extracted production fields
        // must keep Start blocked while later transaction members destruct.
        struct NativeTeardown {
            ~NativeTeardown(){if(trace_start_teardown_probe)trace_start_teardown_probe();}
        } native_teardown;
    };
    std::unique_ptr<HistoricalRestore> historical_restore_;
    explicit Sc6ReplayHost(bool internal_boundary=true)
        : historical_restore_(internal_boundary?std::make_unique<HistoricalRestore>():nullptr) {}
    Status OpenTraceStartBoundary() {
        Require(!historical_restore_);
        if(trace_start_direct) {
            const bool requested=false;
#include "vfx_trace_start_direct_operation.inl"
            return Status::success();
        }
#include "vfx_trace_start_operation.inl"
        historical_restore_=std::move(operation);
        return Status::success();
    }
    Sc6ReplayParticleCopy copy_;Sc6ReplayParticleCopy* particle_copy_=&copy_;
    std::size_t particle_capture_budget_=1024*1024;unsigned settlement_commands{};
    bool command_queued{};
    bool CheckHistoricalMaterialBoundary(bool c_only_retirement=false) noexcept;
    bool MaterialCopyTransitionAllowed() noexcept;
    bool CanReleaseHistoricalRestore() const noexcept;
    Status RequestHistoricalRestore(const Checkpoint&);
    Status PrepareHistoricalRestore(const Checkpoint&);
    // Other native owner release conditions are an independent external
    // dependency in this isolated fixture. They do not certify material work.
    bool HistoricalNativeOwnersReleased() const noexcept {return true;}
    void PublishHistoricalOwnership(){}
    struct Simulation {
        struct Continuation {std::uint64_t tick=217;} value;
        const auto& continuation()const {return value;}
        bool interval_complete()const{return true;}
    } simulation_storage_;
    Simulation* simulation_=&simulation_storage_;
    Status CaptureHistoricalExecution(bool){std::abort();}
    void ExecuteSettlement() {
        Require(command_queued);command_queued=false;
        auto* self=this;auto* copy=particle_copy_;bool success=true;
        switch(ParticleCopyAction::SettleExecution) {
#include "vfx_material_surface_settlement.inl"
        }
        Require(success);
    }
    void DriveSettlement() {
        auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
        using Render=HistoricalRestore::Execution::Render;
        const bool recovery=false;const auto copy=particle_copy_->witness_;
        const auto fail=[](Status,const char*){std::abort();};
        const auto queue=[&](ParticleCopyAction action) {
            Require(action==ParticleCopyAction::SettleExecution);++settlement_commands;
            Require(!command_queued);command_queued=true;return true;
        };
#include "vfx_material_host_settlement.inl"
    }
    void DriveRetirement() {
        auto& transaction=*historical_restore_;auto& execution=*transaction.execution;
        const auto fail=[](Status,const char*){std::abort();};
#include "vfx_material_host_retirement.inl"
    }
};
#define GetTickCount64 HostClock
#include "vfx_material_host_guard.inl"
#include "vfx_material_host_release.inl"
#include "vfx_material_host_request.inl"
#include "vfx_material_host_prepare.inl"
#undef GetTickCount64

// Controlled external builders and task service. Only the production wrappers
// write production records; scheduling cannot start until the builder returns.
// The callback bodies model the verified ABI and recycle-before-RET boundary,
// but make no claim about actual GPU resources, B contents or recovery.
static bool Case(const char* suffix) {return !std::strcmp(mode+20,suffix);}
struct VectorRow {std::uint64_t key;std::array<float,4> color;};
static_assert(sizeof(VectorRow)==0x18);
alignas(8) static std::array<std::array<std::byte,0x150>,3> proxies{};
static std::array<VectorRow,3> proxy_rows{};
struct NativeTask {void* address{};unsigned family{},entries{},returns{};bool submitted{},revoked{};};
static std::vector<NativeTask> tasks;
static std::vector<void*> allocations;
static unsigned current_task{},revoked{},builder_calls{};
static void* reuse_next{};
static std::atomic<bool> builder_entered{},builder_release{};
static constexpr std::uint64_t key=0x100000019ull;
static constexpr std::array<float,4> color{0.25f,0.5f,0.75f,1.0f};
static void RunBody(unsigned);
static unsigned Build(unsigned);
static void Submit(unsigned);
static void Recycle(void* task) {
    std::memset(task,0xcd,0x50);DWORD old{};
    Require(VirtualProtect(task,0x1000,PAGE_NOACCESS,&old)!=FALSE);
    tasks[current_task].revoked=true;++revoked;
}
static void __fastcall NativeVector(void* task) {
    Require(GetLastError()==0x881);
    Require(current_task<tasks.size() && tasks[current_task].address==task
        && tasks[current_task].family==0 && tasks[current_task].entries++==0);
    const auto copied_key=Field<std::uint64_t>(task,0x28);
    const auto copied_color=Field<std::array<float,4>>(task,0x30);
    for(unsigned i=0;i<3;++i) {
        auto* proxy=Field<void*>(task,0x10+i*8);
        Require(proxy==proxies[i].data());
        auto* row=Field<VectorRow*>(proxy,0x138);
        Require(row==&proxy_rows[i] && row->key==copied_key);row->color=copied_color;
    }
    Recycle(task);
    if(Case("reuse") && current_task==0) {
        // The native pool has recycled g1, but its production wrapper has not
        // returned. Register g2 at X before callback(g1)'s return bookkeeping.
        reuse_next=task;Submit(Build(0));
    }
    forbid_observer_reads=true;SetLastError(0x991);
}
static void __fastcall NativeRefresh(void* task) {
    Require(GetLastError()==0x882);
    Require(current_task<tasks.size() && tasks[current_task].address==task
        && tasks[current_task].family==1 && tasks[current_task].entries++==0);
    Require(Field<void*>(task,0x10)==proxies[0].data());
    // Refresh is independently scheduled. No vector-before-refresh assertion
    // or fixture-defined completion of its downstream resources is supplied.
    Recycle(task);forbid_observer_reads=true;SetLastError(0x992);
}
static void Submit(unsigned index) {
    Require(index<tasks.size() && !tasks[index].submitted);tasks[index].submitted=true;
    auto* task=tasks[index].address;
    if(tasks[index].family==0) {
        for(unsigned i=0;i<3;++i)Field<void*>(task,0x10+i*8)=proxies[i].data();
        Field<std::uint64_t>(task,0x28)=key;Field<std::array<float,4>>(task,0x30)=color;
    } else Field<void*>(task,0x10)=proxies[0].data();
}
static void RunBody(unsigned index) {
    Require(index<tasks.size() && tasks[index].submitted && !tasks[index].returns);
    current_task=index;auto* task=tasks[index].address;
    const auto table=Field<std::uintptr_t>(task,0);
    const auto callback=Field<std::uintptr_t>(reinterpret_cast<void*>(table),8);
    const auto family=tasks[index].family;
    const auto reads_before=forbidden_observer_reads.load();
    SetLastError(0x881+family);
    reinterpret_cast<void(__fastcall*)(void*)>(callback)(task);
    Require(GetLastError()==0x991+family);
    forbid_observer_reads=false;Require(forbidden_observer_reads.load()==reads_before);
    ++tasks[index].returns; // Saved scheduler index, never a recycled-task read.
}
static void* NativeBuild(unsigned family,void* output,void* event,unsigned thread) {
    Require(GetLastError()==0x661+family && thread==0xff && !event);++builder_calls;
    void* task=reuse_next;reuse_next=nullptr;
    if(task) {DWORD old{};Require(VirtualProtect(task,0x1000,PAGE_READWRITE,&old)!=FALSE);std::memset(task,0,0x1000);}
    else {task=VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Require(task);allocations.push_back(task);}
    Field<std::uintptr_t>(task,0)=image+(family?0x3920258:0x39150e8);
    tasks.push_back({task,family});
    Field<void*>(output,0)=task;Field<void*>(output,8)=event;Field<unsigned>(output,16)=thread;
    if(Case("active-builder")) {
        builder_entered.store(true,std::memory_order_release);
        while(!builder_release.load(std::memory_order_acquire))std::this_thread::yield();
    }
    if(Case("early-callback")) {Submit(unsigned(tasks.size()-1));RunBody(unsigned(tasks.size()-1));}
    SetLastError(0x771+family);
    // An adversarial native service return checks that wrappers preserve RAX,
    // rather than synthesizing out or out[0]. Real audited builders return out.
    return Case("return-value")?reinterpret_cast<void*>(0x12340000+family):output;
}
static void* __fastcall NativeVectorBuild(void* out,void* event,unsigned thread) {return NativeBuild(0,out,event,thread);}
static void* __fastcall NativeRefreshBuild(void* out,void* event,unsigned thread) {return NativeBuild(1,out,event,thread);}
static unsigned Build(unsigned family) {
    const auto index=unsigned(tasks.size());
    const auto entry=entries[24+family]?entries[24+family]
        :reinterpret_cast<std::uintptr_t>(family?&NativeRefreshBuild:&NativeVectorBuild);
    std::array<std::uintptr_t,3> output{};
    SetLastError(0x661+family);
    auto* result=reinterpret_cast<void*(__fastcall*)(void*,void*,unsigned)>(entry)(output.data(),nullptr,0xff);
    Require(result==(Case("return-value")?reinterpret_cast<void*>(0x12340000+family):output.data()));
    Require(GetLastError()==0x771+family && output[0]==reinterpret_cast<std::uintptr_t>(tasks[index].address)
        && output[1]==0 && output[2]==0xff);
    return index;
}
static void Setup(bool corrupt=true) {
    // Independent byte strings retained in root's architecture prompt, checked
    // against executable f8904e4b...e04a553. Never copied from guard constants.
    const char* signatures[]{
        "4056574883ec4848895c2440488bf148896c243833db4c89642430458be04c89742428488bea4c897c2420895c247048",
        "4056574883ec4848895c2440488bf148896c243833db4c89642430458be04c89742428488bea4c897c2420895c247048",
        "48895c241048896c24184889742420574883ec20488bf9e8147b6dff488b57284c8d4730488b4f10e873f4feff488b4f",
        "48895c241048896c24184889742420574883ec20488bf9e844ad6bff488b4f10e8cbbeffff488d059cd49e01c6471800",
        // Root's independent Ghidra entry bytes, 2026-09-27. The final Start
        // byte is part of the next instruction, not an instruction boundary.
        "48894c2408555357415541564157488d6c24d14881ecd80000004533ed0f29bc24a00000004c89a93c0400004c8bfa44",
        "48895c241848896c24204889542410564883ec30488bf1498be8488b899803000033db488b01ff902806000085c00f8e"};
    constexpr std::uintptr_t rvas[]{0x1f15740,0x1f304e0,0x1f15fc0,0x1f32d90,0x8d8c40,0x8d5840};
    for(unsigned i=0;i<6;++i)for(unsigned b=0;b<48;++b) {
        char pair[]{signatures[i][2*b],signatures[i][2*b+1],0};
        Field<unsigned char>(reinterpret_cast<void*>(image+rvas[i]),b)=static_cast<unsigned char>(std::strtoul(pair,nullptr,16));
    }
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x39150e8),8)=image+0x1f15fc0;
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x3920258),8)=image+0x1f32d90;
    if(CorrelationMode()) {
        const char* material_signatures[]{"85d2781b3b91100800007d13488b8108080000",
            "488954241053564883ec2848897c2450","4883ec38410f10004c8d4424200f29442420e8c98afdff",
            "405341564881ec88000000803ddec6440200"};
        const std::uintptr_t material_rvas[]{0x1dc8220,0x1f1e420,0x1f45940,0x1f05150};
        for(unsigned i=0;i<4;++i)for(unsigned b=0;b<std::strlen(material_signatures[i])/2;++b) {
            char pair[]{material_signatures[i][2*b],material_signatures[i][2*b+1],0};
            Field<unsigned char>(reinterpret_cast<void*>(image+material_rvas[i]),b)=static_cast<unsigned char>(std::strtoul(pair,nullptr,16));
        }
        Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x38829c0),0x4e8)=image+0x1dc8220;
        if(C18CorrelationProbe::SlotCase("signature-get"))Field<unsigned char>(reinterpret_cast<void*>(image+0x1dc8220),18)^=1;
        if(C18CorrelationProbe::SlotCase("signature-set"))Field<unsigned char>(reinterpret_cast<void*>(image+0x1f1e420),15)^=1;
        if(C18CorrelationProbe::SlotCase("signature-wrapper"))Field<unsigned char>(reinterpret_cast<void*>(image+0x1f45940),0)^=1;
        if(C18CorrelationProbe::SlotCase("table"))Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x38829c0),0x4e8)=image+0x1dc8221;
        if(C18CorrelationProbe::SlotCase("span")) {
            // Incoming branch expands the copied prologue beyond the verified
            // range. The production preflight must refuse before hook().
            auto* p=reinterpret_cast<unsigned char*>(image+0x1dc8220);
            std::memset(p+19,0x90,81);p[40]=0xeb;p[41]=0xd6;
        }
        if(C18CorrelationProbe::RowCase("signature"))Field<unsigned char>(reinterpret_cast<void*>(image+0x1f05150),17)^=1;
        if(C18CorrelationProbe::RowCase("span")) {
            auto* p=reinterpret_cast<unsigned char*>(image+0x1f05150);
            std::memset(p+18,0x90,82);p[40]=0xeb;p[41]=0xd6;
        }
    }
    if(corrupt && !std::strncmp(mode+20,"startup-signature-",18)) {
        const auto site=std::atoi(mode+38);Require(site>=0 && site<6);
        Field<unsigned char>(reinterpret_cast<void*>(image+rvas[site]),47)^=1;
    }
    if(corrupt && Case("startup-table"))Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x3920258),8)=image+0x1f15fc0;
    if(corrupt && Case("start-veto-coverage-lost"))
        Field<unsigned char>(reinterpret_cast<void*>(image+0x8d8c40),47)^=1;
    Field<std::uintptr_t>(reinterpret_cast<void*>(image),0x4166720)=corrupt && Case("startup-workers")?1:0;
    for(unsigned i=0;i<3;++i) {
        proxy_rows[i]={key,{}};Field<VectorRow*>(proxies[i].data(),0x138)=&proxy_rows[i];
        Field<int>(proxies[i].data(),0x140)=Field<int>(proxies[i].data(),0x144)=1;
    }
}
static void InstallCallbacks() {
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x39150e8),8)=entries[22]?entries[22]:reinterpret_cast<std::uintptr_t>(&NativeVector);
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x3920258),8)=entries[23]?entries[23]:reinterpret_cast<std::uintptr_t>(&NativeRefresh);
}
static void Cleanup() {
    for(unsigned i=0;i<tasks.size();++i)if(!tasks[i].returns) {
        if(!tasks[i].submitted)Submit(i);RunBody(i);
    }
    for(const auto& task:tasks)Require(task.entries==1 && task.returns==1 && task.revoked);
    for(auto* task:allocations)Require(VirtualFree(task,0,MEM_RELEASE)!=FALSE);
}
static NativeReplayMaterialTaskGuard::Receipt race_receipt;
static void ObserveRetirementMutation() {
    if(!Case("transition-race") && !Case("transition-race-prepare"))return;
    // Observation only. The host has already chosen to call native retirement;
    // this dependency never vetoes it or changes the material receipt.
    const auto sample=NativeReplayMaterialTaskGuard::Inspect(race_receipt);
    material_pending_at_mutation+=unsigned(sample.builders!=0 || sample.tasks!=0);
}
static HANDLE race_start{},race_done{};
static DWORD race_host_thread{};
static bool race_injected{};
static void ScheduleBuilderAfterUnlock() {
    if(GetCurrentThreadId()!=race_host_thread || race_injected)return;
    race_injected=true;
    Require(SetEvent(race_start)!=FALSE);
    // Bound the fixture wait. A timeout is only scheduling observation, never
    // producer cancellation or permission to release a proxy.
    const auto result=WaitForSingleObject(race_done,1000);
    Require(result==WAIT_OBJECT_0 || result==WAIT_TIMEOUT);
}
static int TransitionRace() {
    InstallCallbacks();Sc6ReplayHost host(false);
    // Other settlement tests deliberately start at an internal boundary. P1
    // must enter through production admission, with no existing transaction.
    Require(!host.historical_restore_);
    const bool direct=Case("transition-race-prepare");
    host.copy_.witness_.phase=direct?Sc6ReplayParticleCopy::Phase::UndoReady:Sc6ReplayParticleCopy::Phase::ReadyA;
    const auto b_phase=host.copy_.witness_.phase;
    auto* const b=host.particle_copy_;
    auto target=std::make_shared<Sc6ReplayHost::Checkpoint>();target->gpu=host.copy_.captured_image();
    race_receipt=NativeReplayMaterialTaskGuard::BeginOperation();
    Require(race_receipt.admitted);
    std::optional<Status> admission;
    bool preparation_reached{};
    try {
        admission=direct?host.PrepareHistoricalRestore(*target):host.RequestHistoricalRestore(*target);
    } catch(const PreparationReached&) {preparation_reached=true;}
    const bool rejected=admission && admission->code==FailureCode::UnsupportedContent;
    const bool crossed=preparation_reached || (admission && admission->ok());
    if(crossed) {
        // On the old implementation, keep the original downstream race.
        // These participant transitions do not grant material exclusion.
        Require(host.historical_restore_ && host.CheckHistoricalMaterialBoundary());
        auto& owner=host.historical_restore_->execution->ownership;
        Require(owner.PublishA() && owner.ActivateExecution() && owner.BeginRecovery());
        race_receipt=host.historical_restore_->material;
    }
    race_start=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    race_done=CreateEventW(nullptr,TRUE,FALSE,nullptr);Require(race_start && race_done);
    race_host_thread=GetCurrentThreadId();
    // Install before thread creation; keep the pointer stable through join.
    material_after_unlock=&ScheduleBuilderAfterUnlock;
    std::thread producer([] {
        Require(WaitForSingleObject(race_start,3000)==WAIT_OBJECT_0);
        Submit(Build(0));Require(SetEvent(race_done)!=FALSE);
    });
    if(crossed)host.DriveRetirement(); // Real predicate and native call sites.
    else {
        // Even rejection must leave the real builder/callback forwarding
        // untouched. Trigger its schedule after a real Inspect unlock; no
        // transaction was admitted to perform the destructive operation.
        NativeReplayMaterialTaskGuard::Inspect(race_receipt);
    }
    Require(WaitForSingleObject(race_done,3000)==WAIT_OBJECT_0);producer.join();
    material_after_unlock=nullptr;
    CloseHandle(race_start);CloseHandle(race_done);
    Require(tasks.size()==1 && builder_calls==1);
    const unsigned mutations=native_retirements[0]+native_retirements[1]+native_retirements[2];
    const auto* owner=host.historical_restore_?&host.historical_restore_->execution->ownership:nullptr;
    const bool retained=host.particle_copy_==b && host.copy_.witness_.phase==b_phase
        && host.copy_.witness_.native_write_uncommitted && (!owner || owner->retains_undo());
    std::printf("material transition race injected=%u builder_forwarded=%u pending_at_mutation=%u destructive_calls=%u admission_rejected=%u B_retained=%u commit_decided=%u transaction_created=%u\n",
        unsigned(race_injected),builder_calls,material_pending_at_mutation,mutations,
        unsigned(rejected),unsigned(retained),unsigned(owner && owner->commit_decided()),unsigned(bool(host.historical_restore_)));
    Cleanup(); // Forward the callback once after retaining the failure sample.
    Require(tasks[0].returns==1 && !forbidden_observer_reads.load());return 0;
}

// The selected 1408D5840 producer has already obtained a raw MID from its
// provider before either guarded builder. Model that external dependency, not
// the getter's unextracted ABI or an invented AddRef/Outer/GC lease. In
// particular, keeping this input allocation alive is NOT host-owned exclusion.
static int BorrowBeforeBuilder() {
    using Guard=NativeReplayMaterialTaskGuard;
    InstallCallbacks();Sc6ReplayHost host(false);
    const bool direct=Case("borrow-before-builder-prepare");
    host.copy_.witness_.phase=direct?Sc6ReplayParticleCopy::Phase::UndoReady:Sc6ReplayParticleCopy::Phase::ReadyA;
    const auto b_phase=host.copy_.witness_.phase;
    auto* const b=host.particle_copy_;
    auto target=std::make_shared<Sc6ReplayHost::Checkpoint>();target->gpu=host.copy_.captured_image();
    const auto receipt=Guard::BeginOperation();Require(receipt.admitted);
    struct SelectedMid {void* proxy;};
    SelectedMid selected{proxies[0].data()};
    unsigned provider_calls{};
    const auto provider_lookup=[&]() -> SelectedMid* {++provider_calls;return &selected;};
    std::atomic<bool> borrowed{};
    const auto acquired=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    const auto resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    const auto submitted=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Require(acquired && resume && submitted);
    std::thread producer([&] {
        auto* mid=provider_lookup();auto* proxy=mid->proxy;
        borrowed.store(true,std::memory_order_release);
        Require(SetEvent(acquired)!=FALSE);
        // Scheduling barrier only; never consumed by any production predicate.
        // A timeout aborts the fixture, it cannot cancel a native obligation.
        Require(WaitForSingleObject(resume,3000)==WAIT_OBJECT_0);
        Require(mid==&selected && proxy==proxies[0].data());
        Submit(Build(0));Submit(Build(1));
        mid=nullptr;proxy=nullptr;borrowed.store(false,std::memory_order_release);
        Require(SetEvent(submitted)!=FALSE);
    });
    Require(WaitForSingleObject(acquired,3000)==WAIT_OBJECT_0);
    const auto attempt=[&](const char* stage) {
        // Both calls are the existing verbatim production excerpts. The direct
        // Prepare sentinel reports crossing its prefix, never full preparation.
        const auto sample=Guard::Inspect(receipt);
        std::optional<Status> result;bool preparation_reached{};
        try {
            result=direct?host.PrepareHistoricalRestore(*target):host.RequestHistoricalRestore(*target);
        } catch(const PreparationReached&) {preparation_reached=true;}
        const bool crossed=preparation_reached || (result && result->ok());
        if(crossed && !std::strcmp(stage,"borrowed")) {
            // Expose an unsafe admission through the same production retirement
            // decision as P1; these participant transitions grant no MID lease.
            Require(bool(host.historical_restore_));
            auto& owner=host.historical_restore_->execution->ownership;
            Require(owner.PublishA() && owner.ActivateExecution() && owner.BeginRecovery());
            host.DriveRetirement();
        }
        const auto* owner=host.historical_restore_?&host.historical_restore_->execution->ownership:nullptr;
        const bool retained=host.particle_copy_==b && host.copy_.witness_.phase==b_phase
            && host.copy_.captured_image()==target->gpu && host.copy_.witness_.native_write_uncommitted
            && host.copy_.witness_.reflection_retained && host.copy_.witness_.target_prepared
            && (!owner || owner->retains_undo());
        std::printf("material raw borrow stage=%s held=%u state=%u builders=%llu tasks=%llu builder_calls=%u "
            "rejected=%u prefix_crossed=%u destructive_calls=%u B_retained=%u commit_decided=%u transaction_created=%u\n",
            stage,unsigned(borrowed.load(std::memory_order_acquire)),unsigned(sample.state),sample.builders,sample.tasks,
            builder_calls,unsigned(result && result->code==FailureCode::UnsupportedContent),unsigned(crossed),
            native_retirements[0]+native_retirements[1]+native_retirements[2],unsigned(retained),
            unsigned(owner && owner->commit_decided()),unsigned(bool(host.historical_restore_)));
    };
    attempt("borrowed"); // Raw pointer live; neither builder has entered.
    Require(SetEvent(resume)!=FALSE);
    Require(WaitForSingleObject(submitted,3000)==WAIT_OBJECT_0);producer.join();
    CloseHandle(acquired);CloseHandle(resume);CloseHandle(submitted);
    Require(provider_calls==1 && builder_calls==2 && tasks.size()==2);
    attempt("submitted");
    RunBody(0);attempt("vector_returned"); // Refresh still independently pending.
    RunBody(1);attempt("cpu_returned");
    // No particle query is signaled; no proxy queue is drained by this fixture.
    // 141F14AC0/141F1A4D0 and 144344848 -> 1415DC470 are NOT represented by
    // either callback's return. New borrowing/reentry after exclusion cannot
    // be scheduled: production exposes no exclusion acquisition boundary.
    Require(!host.copy_.completion_.signaled && !host.copy_.witness_.execution_work_complete);
    Cleanup();
    Require(tasks[0].entries==1 && tasks[0].returns==1 && tasks[1].entries==1 && tasks[1].returns==1
        && !forbidden_observer_reads.load());
    std::puts("material raw borrow forwarding provider=1 vector_builder=1 refresh_builder=1 vector_callback=1 refresh_callback=1");
    std::puts("material raw borrow limits: producer_exclusion=unrepresented post_exclusion_borrow=unrepresented "
        "same_thread_reentry=unrepresented reset_reinitialize=unrepresented deferred_release_gpu=unrepresented "
        "complete_B_contents=unrepresented");
    return 0;
}

// Exact verified native entry ABIs routed by the same external detour service
// as the task fixture. Probes do native-shaped work, never grant a guard token.
namespace TraceProducer {
using Guard=NativeReplayMaterialTaskGuard;
static Sc6ReplayHost* host;
static std::atomic<unsigned> starts{},helpers{},writes{},lookups{},paused{};
static HANDLE acquired{},resume{};
static unsigned expected_paused{};
static thread_local unsigned start_depth{},helper_depth{};
static int component_storage,request_storage,actor_storage;
static std::array<float,4> input_color{1,2,3,4};
static constexpr std::uint64_t parameter=0xfedcba9876543210ull;
static void CallStart();
static void Observe(const char* stage) {
    const auto sample=Guard::Inspect(host->historical_restore_->material);
    const auto late=Guard::BeginOperation();
    std::printf("trace producer stage=%s state=%u builders=%llu tasks=%llu late_admitted=%u command_allowed=%u\n",
        stage,unsigned(sample.state),sample.builders,sample.tasks,unsigned(late.admitted),
        unsigned(host->MaterialCopyTransitionAllowed()));
    const auto& p=sample.producers;
    std::printf("trace counts stage=%s starts=%llu helpers=%llu start_returns=%llu helper_returns=%llu "
        "active_starts=%llu active_helpers=%llu threads=%u depth=%u peak=%u reentries=%llu concurrent=%llu orphan=%llu\n",
        stage,p.entries[0],p.entries[1],p.returns[0],p.returns[1],p.active[0],p.active[1],
        p.active_threads,p.this_thread_depth,p.peak_depth,p.reentries,p.concurrent_entries,p.helpers_without_start);
}
static void __fastcall NativeHelper(void* actor,std::uint64_t name,float* color) {
    Require(GetLastError()==0xa12 && actor==&actor_storage && name==parameter && color==input_color.data());
    ++helpers;++helper_depth;Observe("helper_before_lookup");
    ++lookups;auto* borrowed=proxies[0].data();Observe("helper_borrowed");
    if(Case("trace-producer-reentry") && helper_depth==1)CallStart();
    if(Case("trace-producer-material")) {
        Submit(Build(0));Submit(Build(1));Observe("helper_submitted");
    }
    if(expected_paused) {
        if(paused.fetch_add(1)+1==expected_paused)Require(SetEvent(acquired)!=FALSE);
        Require(WaitForSingleObject(resume,3000)==WAIT_OBJECT_0);
    }
    Require(borrowed==proxies[0].data());
    --helper_depth;
    // An exit wrapper must not rediscover identity by reading borrowed input.
    forbid_observer_reads=true;SetLastError(0xb12);
}
static void CallHelper() {
    SetLastError(0xa12);
    reinterpret_cast<void(__fastcall*)(void*,std::uint64_t,float*)>(
        entries[27]?entries[27]:reinterpret_cast<std::uintptr_t>(&NativeHelper))(
            &actor_storage,parameter,input_color.data());
    Require(GetLastError()==0xb12);forbid_observer_reads=false;
}
static void __fastcall NativeStart(void* component,void* request) {
    Require(GetLastError()==0xa11 && component==&component_storage && request==&request_storage);
    ++starts;++start_depth;
    if(!Case("trace-producer-capacity"))Observe("start_before_write");
    ++writes;
    if(Case("trace-producer-capacity")) {
        if(start_depth<130)CallStart();
    } else {CallHelper();Observe("start_after_helper");}
    --start_depth;
    forbid_observer_reads=true;SetLastError(0xb11);
}
static void CallStart() {
    SetLastError(0xa11);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(
        entries[26]?entries[26]:reinterpret_cast<std::uintptr_t>(&NativeStart))(&component_storage,&request_storage);
    Require(GetLastError()==0xb11);forbid_observer_reads=false;
}
static bool late_injected{};
static void LateProducerAfterUnlock() {
    if(late_injected)return;
    late_injected=true;CallStart();
}
static std::unique_ptr<Sc6ReplayHost> startup_owner;
static std::thread startup_producer;
static void DuringInstall() {
    // Called by the external hook service AFTER publishing both trampoline
    // outputs, but BEFORE InstallStartup can declare full coverage.
    startup_owner=std::make_unique<Sc6ReplayHost>();host=startup_owner.get();
    if(Case("startup-producer-active")) {
        acquired=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Require(acquired && resume);expected_paused=1;
        startup_producer=std::thread(CallStart);
        Require(WaitForSingleObject(acquired,3000)==WAIT_OBJECT_0);
    } else {CallStart();host=nullptr;startup_owner.reset();}
}
static void AfterInstall() {
    Observe("startup_held");Require(SetEvent(resume)!=FALSE);startup_producer.join();expected_paused=0;
    CloseHandle(acquired);CloseHandle(resume);Observe("startup_returned");
    host=nullptr;startup_owner.reset();
}
static int Run() {
    InstallCallbacks();Sc6ReplayHost owner;host=&owner;
    auto& ownership=host->historical_restore_->execution->ownership;
    Require(ownership.PublishA() && ownership.ActivateExecution() && ownership.BeginRecovery());
    if(Case("trace-producer-concurrent")) {
        acquired=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Require(acquired && resume);expected_paused=2;
        std::thread first(CallStart),second(CallStart);
        Require(WaitForSingleObject(acquired,3000)==WAIT_OBJECT_0);
        Observe("concurrent_held");
        const auto late=Guard::BeginOperation();
        Require(SetEvent(resume)!=FALSE);first.join();second.join();expected_paused=0;
        CloseHandle(acquired);CloseHandle(resume);
        std::printf("trace late receipt admitted=%u returned_state=%u\n",unsigned(late.admitted),unsigned(Guard::Inspect(late).state));
    } else if(Case("trace-producer-late-snapshot")) {
        // The production predicate samples Clear, then a real producer runs at
        // its unlock before that sample is consumed. Observation is NOT a lease.
        material_after_unlock=&LateProducerAfterUnlock;
        const bool sampled_command=host->MaterialCopyTransitionAllowed();
        material_after_unlock=nullptr;
        std::printf("trace late snapshot injected=%u sampled_command=%u current_state=%u\n",
            unsigned(late_injected),unsigned(sampled_command),unsigned(Guard::Inspect(host->historical_restore_->material).state));
    } else if(Case("trace-producer-orphan"))CallHelper();
    else {
        if(Case("trace-producer-observer-closed"))Require(Horse::Deterministic::NativeReplayVfxCompletionObservation::ClosePhase(337));
        CallStart();
    }
    Observe("returned");host->DriveRetirement();
    if(Case("trace-producer-material")) {
        Require(tasks.size()==2);RunBody(0);Observe("vector_returned");
        RunBody(1);Observe("callbacks_returned");host->DriveRetirement();
        Require(!host->copy_.completion_.signaled && !host->copy_.witness_.execution_work_complete);
        Cleanup();
        std::printf("trace material tasks builders=%u vector_returns=%u refresh_returns=%u GPU_receipt=0\n",
            builder_calls,tasks[0].returns,tasks[1].returns);
    }
    const auto fresh=Guard::BeginOperation();
    std::printf("trace fresh receipt state=%u C_only_state=%u\n",unsigned(Guard::Inspect(fresh).state),
        unsigned(Guard::InspectCOnlyRetirement(fresh).state));
    std::printf("trace producer forwarding starts=%u helpers=%u writes=%u lookups=%u reads_after_return=%u "
        "retirements=%u B_retained=%u committed=%u failed=%u historical_supported=%u\n",
        starts.load(),helpers.load(),writes.load(),lookups.load(),forbidden_observer_reads.load(),
        native_retirements[0]+native_retirements[1]+native_retirements[2],
        unsigned(ownership.retains_undo()),unsigned(ownership.commit_decided()),
        unsigned(host->historical_restore_->material_failed),unsigned(Guard::HistoricalRestoreSupported()));
    host=nullptr;return 0;
}
}

namespace TraceStartVeto {
using Guard=NativeReplayMaterialTaskGuard;
static std::array<std::byte,0x448> component{};
static std::array<std::byte,0x30> request{};
static unsigned native_calls{};
static HANDLE entered{},resume{};
static bool check_reentry{};
static void Call();
static void __fastcall NativeStart(void* object,void* input) {
    Require(object==component.data() && input==request.data() && GetLastError()==0xa11);
    for(std::size_t i=0;i<request.size();++i)Require(request[i]==std::byte(i+1));
    // Independent first-effect sentinel. It must never be reached with an
    // operation veto. No callback, fake lease or counter grants this outcome.
    std::puts("trace Start native mutation");
    ++native_calls;component[0x43c]=std::byte{1};
    if(check_reentry) {
        Sc6ReplayHost nested(false);
        Require(!nested.OpenTraceStartBoundary().ok() && !nested.historical_restore_);
    }
    if(entered) {
        Require(SetEvent(entered)!=FALSE);
        Require(WaitForSingleObject(resume,3000)==WAIT_OBJECT_0);
    }
    forbid_observer_reads=true;SetLastError(0xb11);
}
static void Call() {
    SetLastError(0xa11);
    const auto entry=entries[26]?entries[26]:reinterpret_cast<std::uintptr_t>(&NativeStart);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(entry)(component.data(),request.data());
    Require(GetLastError()==0xb11);forbid_observer_reads=false;
}
static void AfterUnlock() {material_after_unlock=nullptr;Call();}
static int Run() {
    Require(!Guard::HistoricalRestoreSupported());
    for(std::size_t i=0;i<request.size();++i)request[i]=std::byte(i+1);
    const auto* name=mode+20+11; // material-settlement-start-veto-
    std::puts("trace Start veto exercise begins historical_supported=0");
    Sc6ReplayHost owner(false);
    if(Case("start-veto-preexisting")) {
        entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Require(entered && resume);
        std::thread producer(Call);
        Require(WaitForSingleObject(entered,3000)==WAIT_OBJECT_0);
        Require(!owner.OpenTraceStartBoundary().ok() && !owner.historical_restore_);
        Require(SetEvent(resume)!=FALSE);producer.join();
        CloseHandle(entered);CloseHandle(resume);entered=resume=nullptr;
        Require(owner.OpenTraceStartBoundary().ok());owner.historical_restore_.reset();
    } else if(Case("start-veto-reentry")) {
        check_reentry=true;Call();check_reentry=false;
        Require(owner.OpenTraceStartBoundary().ok());owner.historical_restore_.reset();
    } else if(Case("start-veto-coverage-lost")) {
        Require(!owner.OpenTraceStartBoundary().ok() && !owner.historical_restore_);Call();
    } else {
        Require(owner.OpenTraceStartBoundary().ok() && owner.historical_restore_);
        Require(owner.historical_restore_->value.ownership.retains_undo());
        if(Case("start-veto-released")) {owner.historical_restore_.reset();Call();}
        else if(Case("start-veto-duplicate") || Case("start-veto-duplicate-retained")) {
            Sc6ReplayHost duplicate(false);
            Require(!duplicate.OpenTraceStartBoundary().ok() && !duplicate.historical_restore_);
            if(Case("start-veto-duplicate-retained"))Call();
            owner.historical_restore_.reset();Call();
        } else if(Case("start-veto-target-tails") || Case("start-veto-recovery") || Case("start-veto-failed")) {
            auto& ownership=owner.historical_restore_->value.ownership;
            Require(ownership.PublishA() && ownership.ActivateExecution());
            if(Case("start-veto-target-tails"))Require(ownership.SettleC(217) && ownership.BeginTargetTails());
            if(Case("start-veto-recovery"))Require(ownership.BeginRecovery());
            if(Case("start-veto-failed"))ownership.Fail();
            Require(ownership.retains_undo() && !ownership.commit_decided());Call();
        } else if(Case("start-veto-worker")) {std::thread producer(Call);producer.join();}
        else if(Case("start-veto-after-unlock")) {
            material_after_unlock=&AfterUnlock;
            Guard::Inspect(owner.historical_restore_->material);
            material_after_unlock=nullptr;
        } else if(Case("start-veto-teardown")) {
            trace_start_teardown_probe=&Call;owner.historical_restore_.reset();trace_start_teardown_probe=nullptr;
        } else {Require(Case("start-veto-active"));Call();}
    }
    Require(native_calls==1 && forbidden_observer_reads.load()==0);
    std::printf("trace Start veto control PASS case=%s historical_supported=0\n",name);
    return 0;
}
}

// Internal retirement is deliberately exercised independently of the blanket
// Request/Prepare rejection. The selected external producer holds a raw MID
// before either guarded builder, so CPU-idle is not permission to destroy C.
static int BorrowAtRetirement() {
    using Guard=NativeReplayMaterialTaskGuard;
    InstallCallbacks();Sc6ReplayHost host;
    auto& transaction=*host.historical_restore_;
    auto& owner=transaction.execution->ownership;
    Require(owner.PublishA() && owner.ActivateExecution() && owner.BeginRecovery());
    struct Mid {void* proxy;} mid{proxies[0].data()};
    unsigned provider_calls{};std::atomic<bool> held{};
    const auto acquired=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    const auto resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Require(acquired && resume);
    const bool reentrant=Case("borrow-at-retirement-same-thread");
    const auto observe=[&](const Guard::Snapshot& before) {
        std::printf("material C-only raw borrow held=%u builders=%llu tasks=%llu destructive_calls=%u "
            "B_retained=%u commit_decided=%u failed=%u\n",unsigned(held.load()),before.builders,before.tasks,
            native_retirements[0]+native_retirements[1]+native_retirements[2],
            unsigned(owner.retains_undo()),unsigned(owner.commit_decided()),unsigned(transaction.material_failed));
        std::printf("material C-only terminal command_allowed=%u release_allowed=%u\n",
            unsigned(host.MaterialCopyTransitionAllowed()),unsigned(host.CanReleaseHistoricalRestore()));
    };
    const auto producer_body=[&] {
        ++provider_calls;auto* borrowed=&mid;auto* proxy=borrowed->proxy;
        held.store(true,std::memory_order_release);Require(SetEvent(acquired)!=FALSE);
        // Reenter the actual retirement decision while this stack owns the raw
        // borrow. No fixture lock/token makes reentry safe or grants ownership.
        if(reentrant) {
            const auto before=Guard::Inspect(transaction.material);
            host.DriveRetirement();
            observe(before);
        }
        Require(WaitForSingleObject(resume,3000)==WAIT_OBJECT_0);
        Require(borrowed==&mid && proxy==proxies[0].data());
        Submit(Build(0));Submit(Build(1));held.store(false,std::memory_order_release);
    };
    if(reentrant) {
        Require(SetEvent(resume)!=FALSE);producer_body();
        CloseHandle(acquired);CloseHandle(resume);
        Cleanup();Require(provider_calls==1 && builder_calls==2 && tasks[0].returns==1 && tasks[1].returns==1);
        std::puts("material C-only forwarding provider=1 builders=2 callbacks=2");return 0;
    }
    std::thread producer(producer_body);
    Require(WaitForSingleObject(acquired,3000)==WAIT_OBJECT_0);
    const auto before=Guard::Inspect(transaction.material);
    Require(held.load(std::memory_order_acquire) && !before.builders && !before.tasks);
    host.DriveRetirement();
    observe(before);
    Require(SetEvent(resume)!=FALSE);producer.join();CloseHandle(acquired);CloseHandle(resume);
    Cleanup();Require(provider_calls==1 && builder_calls==2 && tasks[0].returns==1 && tasks[1].returns==1);
    std::puts("material C-only forwarding provider=1 builders=2 callbacks=2");
    return 0;
}

// Complete reached corrected-capture host path, distinct from the isolated
// settlement fixture above. Native readback/checkpoint contents are controlled
// dependencies; no dependency may classify material work or release either
// retained owner. Excluded phase/action tails abort if they become reachable.
namespace CorrectedCapture {
template<class T>static T& EngineField(void* object,std::size_t offset) {return Field<T>(object,offset);}
struct Sc6ReplayParticleCopy : MaterialSettlement::Sc6ReplayParticleCopy {
    struct Witness {
        Phase phase=Phase::ReadyA;
        bool pending{},capture_sealed=true,native_write_uncommitted{},installed_copy_complete{};
        std::uintptr_t reconstruction_owner{};const char* reconstruction_issue="fixture";
    } state;
    unsigned finish_calls{},retire_calls{};
    const Witness& witness()const {return state;}
    static bool capture_retirement_pending(){return false;}
    // These bodies must never run in an unsupported material transition. No
    // fake successful Finish/Released or recovery can make the test pass.
    bool Finish(){++finish_calls;return false;}
    void RetireInFlight(){++retire_calls;Require(!state.pending);}
    void Poll(){Require(!state.pending);}
};
struct Sc6ReplayHost {
#include "vfx_material_capture_types.inl"
    enum class ApplicationPhase {Idle};
    enum class SurfaceCommand {ParticleCopy};
    struct Checkpoint {bool gpu=true;};
    using CheckpointHandle=std::shared_ptr<const Checkpoint>;
    struct HistoricalRestore {
#include "vfx_material_receipt_fields.inl"
        struct Witness {
            RestoreOperationPhase phase=RestoreOperationPhase::Held;
            FailureCode failure{};const char* participant{};
            bool pending{},original_recovered{};
        } witness;
        struct Execution {ReplaySeekOwnership ownership{217,217};};
        std::unique_ptr<Execution> execution=std::make_unique<Execution>();
        struct Undo {bool valid=true;std::array<std::uint64_t,3> callback_stamp{};} undo;
        bool preparing{},preparation_retiring{};
    };
    std::unique_ptr<HistoricalRestore> historical_restore_=std::make_unique<HistoricalRestore>();
    std::unique_ptr<Sc6ReplayParticleCopy> particle_copy_=std::make_unique<Sc6ReplayParticleCopy>();
    std::unique_ptr<Sc6ReplayParticleCopy> corrected_particle_copy_=std::make_unique<Sc6ReplayParticleCopy>();
    bool corrected_capture_=true,capture_driving_{},surface_event_{},particle_command_corrected_{};
    bool seek_driving_=true,restore_preparation_driving_{},depth_{},seek_retirement_pending_{},seek_retirement_failed_{},engine_post_deferred_{};
    bool checkpoint_restoring_=true;
    std::atomic<bool> particle_command_pending_{},particle_command_failed_{},corrected_particle_command_failed_{};
    std::uintptr_t image_base_=image;DWORD thread_=GetCurrentThreadId();
    InteriorPhase interior_phase_=InteriorPhase::Holding;
    PauseBoundary pause_boundary_=PauseBoundary::CompletedApplication;
    ApplicationPhase application_phase_=ApplicationPhase::Idle;
    CaptureWitness capture_operation_{CapturePhase::Copying,FailureCode::None,217,0,0,true};
    CheckpointHandle captured_checkpoint_;
    std::chrono::steady_clock::time_point capture_started_=std::chrono::steady_clock::now();
    ParticleCopyAction particle_copy_action_=ParticleCopyAction::Read;
    int particle_birth_{},particle_birth_render_{};unsigned particle_birth_render_count_{};
    std::size_t particle_birth_budget_{},memory_limit_=1024*1024;
    struct Simulation {struct {std::uint64_t tick=217;} state;const auto& continuation(){return state;}} sim;
    Simulation* simulation_=&sim;
    struct CallbackAdmission {Status Validate(const std::array<std::uint64_t,3>&){std::abort();}};
    CallbackAdmission* callback_admission_{};
    struct Seek {SeekWitness witness;bool cancel{};CheckpointHandle checkpoint;} seek_;
    struct Rolling {bool correcting{},rebuild_complete{};std::array<CheckpointHandle,8> replacement{};
        std::array<std::uint64_t,8> replacement_generations{};} rolling_;
    TickAdvanceWitness tick_advance_{};
    unsigned finish_queued{},checkpoint_copies{},command_completions{};
    inline static Sc6ReplayHost* active_{};
    Sc6ReplayHost(){active_=this;seek_.witness.phase=SeekPhase::Advancing;seek_.witness.pending=true;}
    bool CheckBinding()const {return true;}
    bool engine_idle()const {return true;}bool world_idle()const {return true;}
    bool SeekOwnsExecution()const {return true;}
    std::size_t AdmissionBytes()const {Require(!particle_command_pending_.load());return 4096;}
    Status Capture(CheckpointHandle& output) {
        Require(corrected_particle_copy_->state.capture_sealed && !corrected_particle_copy_->state.pending);
        ++checkpoint_copies;output=std::make_shared<Checkpoint>();return Status::success();
    }
    bool QueueSurface(SurfaceCommand command) {
        Require(command==SurfaceCommand::ParticleCopy && !surface_event_ && particle_command_pending_.load());
        surface_event_=true;if(particle_copy_action_==ParticleCopyAction::Finish)++finish_queued;return true;
    }
    // Every unvisited dependency aborts: none supplies successful recovery,
    // owner retirement, target publication, or material completion.
    void AdvanceRestorePreparation(){std::abort();}
    bool HistoricalNativeOwnersReleased()const noexcept {std::abort();}
    bool CanRetireSeekCheckpoint(){std::abort();}
    bool AdvanceSeekRetirement(){std::abort();}
    bool DriveRollingReplacement(){std::abort();}
    static bool SeekOperation(SeekAction,const Checkpoint*,std::uint64_t,SeekWitness*){std::abort();}
    void PublishHistoricalOwnership(){}
    bool CheckHistoricalMaterialBoundary(bool c_only_retirement=false)noexcept;
    bool MaterialCopyTransitionAllowed()noexcept;
    bool CanReleaseHistoricalRestore()const noexcept;
    std::unique_ptr<Sc6ReplayParticleCopy>& CaptureParticleCopyOwner()noexcept;
    bool CaptureCommandFailed()const noexcept;
    static bool CaptureOperation(CaptureAction,CaptureWitness*,CheckpointHandle* =nullptr)noexcept;
    static bool ParticleCopyExperiment(ParticleCopyAction,Sc6ReplayParticleCopy::Witness*,bool*)noexcept;
    static void ExecuteParticleCopyCommand(void*,void*)noexcept;
    void AdvanceCaptureOperation()noexcept;
    void AdvanceHistoricalRestore()noexcept;
    void AdvanceSeek()noexcept;
    void Service() {
#include "vfx_material_capture_service.inl"
    }
    void CompleteCommand() {
        if(!particle_command_pending_.load())return;
        Require(surface_event_);alignas(8) std::array<std::byte,24> command{};
        EngineField<Sc6ReplayHost*>(command.data(),16)=this;
        ExecuteParticleCopyCommand(nullptr,command.data());
        Require(!particle_command_pending_.load());++command_completions;
        surface_event_=false; // External scheduler event completes after callback RET.
    }
};
#define GetTickCount64 HostClock
#include "vfx_material_host_guard.inl"
#include "vfx_material_host_release.inl"
#include "vfx_material_capture_driver.inl"
#include "vfx_material_capture_actions.inl"
#include "vfx_material_capture_queue.inl"
#include "vfx_material_capture_command.inl"
#include "vfx_material_capture_restore.inl"
#include "vfx_material_capture_seek.inl"
#undef GetTickCount64
static int Run() {
    InstallCallbacks();Sc6ReplayHost host;auto* const b=host.particle_copy_.get();
    auto* const provisional=host.corrected_particle_copy_.get();
    auto* const transaction=host.historical_restore_.get();auto& ownership=transaction->execution->ownership;
    Require(transaction->material.admitted && ownership.PublishA() && ownership.ActivateExecution());
    b->state.phase=Sc6ReplayParticleCopy::Phase::Installed;b->state.native_write_uncommitted=true;
    host.Service();Require(host.checkpoint_copies==1 && host.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Finishing);
    auto* const checkpoint=host.captured_checkpoint_.get();Require(checkpoint);
    host.Service();Require(host.finish_queued==1 && host.particle_command_pending_.load()
        && host.particle_command_corrected_);
    // Material appears after the actual corrected Finish has been queued but
    // before its render command runs. Even a fixed game-thread classifier must
    // therefore exercise the real command deferral/completion bookkeeping.
    // No direct call to CheckHistoricalMaterialBoundary is allowed below:
    // the actual host scheduling must discover and propagate this failure.
    Submit(Build(0));Submit(Build(1));
    if(Case("corrected-coverage-lost")) {
        // Independent native callback without a tracked builder: exercise
        // production coverage loss, never write the guard's state directly.
        std::array<std::uintptr_t,3> output{};SetLastError(0x661);
        NativeVectorBuild(output.data(),nullptr,0xff);Submit(2);RunBody(2);
    }
    if(Case("corrected-cancel-returned")) {
        host.seek_.cancel=true;host.rolling_.correcting=true;
        host.rolling_.replacement[0]=host.captured_checkpoint_;
    }
    host.Service(); // Includes the real seek -> capture Cancel admission route.
    host.CompleteCommand(); // Exercise the render-side deferral.
    if(!Case("corrected-timeout") && !Case("corrected-coverage-lost")) {RunBody(1);RunBody(0);}
    for(unsigned turn=0;turn<8;++turn) {
        if(Case("corrected-timeout"))host_clock+=1001;
        host.Service();host.CompleteCommand();
    }
    const bool history=transaction->material_failed
        && transaction->witness.phase==Sc6ReplayHost::RestoreOperationPhase::Failed
        && transaction->witness.failure==FailureCode::UnsupportedContent;
    const bool capture=host.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Failed
        && host.capture_operation_.failure==FailureCode::UnsupportedContent;
    const bool seek=host.seek_.witness.phase==Sc6ReplayHost::SeekPhase::Failed
        && host.seek_.witness.failure==FailureCode::UnsupportedContent;
    const bool retained=host.particle_copy_.get()==b && host.corrected_particle_copy_.get()==provisional
        && host.historical_restore_.get()==transaction && ownership.retains_undo()
        && host.captured_checkpoint_.get()==checkpoint && host.corrected_capture_
        && (!Case("corrected-cancel-returned") || host.rolling_.replacement[0].get()==checkpoint)
        && b->state.native_write_uncommitted && !host.CanReleaseHistoricalRestore()
        && b->retire_calls==0 && provisional->retire_calls==0;
    const auto queued=host.finish_queued;
    std::printf("corrected material terminal history=%u capture=%u seek=%u finish_queued=%u finish_native=%u owners_retained=%u commit_decided=%u recovered=%u deferred=%u callbacks=%u\n",
        unsigned(history),unsigned(capture),unsigned(seek),queued,b->finish_calls+provisional->finish_calls,
        unsigned(retained),unsigned(ownership.commit_decided()),unsigned(transaction->witness.original_recovered),
        unsigned(transaction->material_command_deferred.load()),tasks[0].returns+tasks[1].returns);
    host.Service();host.CompleteCommand();
    const bool stable=history && capture && seek && host.finish_queued==queued
        && host.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Failed
        && host.seek_.witness.phase==Sc6ReplayHost::SeekPhase::Failed;
    std::printf("corrected terminal remains stable=%u\n",unsigned(stable));
    Sc6ReplayHost::CaptureWitness rejected{};
    const bool cancel_rejected=!host.CaptureOperation(Sc6ReplayHost::CaptureAction::Cancel,&rejected)
        && rejected.phase==Sc6ReplayHost::CapturePhase::Failed && rejected.failure==FailureCode::UnsupportedContent;
    const bool release_rejected=!host.CaptureOperation(Sc6ReplayHost::CaptureAction::Release,&rejected)
        && rejected.phase==Sc6ReplayHost::CapturePhase::Failed && rejected.failure==FailureCode::UnsupportedContent;
    Cleanup();Require(tasks[0].returns==1 && tasks[1].returns==1 && !forbidden_observer_reads.load());
    host.Service();host.CompleteCommand();
    const bool late_retained=host.particle_copy_.get()==b && host.corrected_particle_copy_.get()==provisional
        && host.historical_restore_.get()==transaction && host.captured_checkpoint_.get()==checkpoint
        && (!Case("corrected-cancel-returned") || host.rolling_.replacement[0].get()==checkpoint)
        && host.corrected_capture_ && ownership.retains_undo() && !host.CanReleaseHistoricalRestore()
        && transaction->material_failed && transaction->witness.failure==FailureCode::UnsupportedContent
        && host.capture_operation_.phase==Sc6ReplayHost::CapturePhase::Failed
        && host.capture_operation_.failure==FailureCode::UnsupportedContent
        && host.seek_.witness.phase==Sc6ReplayHost::SeekPhase::Failed
        && host.seek_.witness.failure==FailureCode::UnsupportedContent && host.finish_queued==queued
        && b->finish_calls==0 && provisional->finish_calls==0 && b->retire_calls==0 && provisional->retire_calls==0;
    std::printf("corrected terminal cancel_rejected=%u release_rejected=%u late_callbacks_retained=%u\n",
        unsigned(cancel_rejected),unsigned(release_rejected),unsigned(late_retained));
    return 0;
}
}
static int Run() {
    using Guard=NativeReplayMaterialTaskGuard;using State=Guard::State;
    using Render=Sc6ReplayHost::HistoricalRestore::Execution::Render;
    if(!std::strncmp(mode+20,"start-veto-",11))return TraceStartVeto::Run();
    if(!std::strncmp(mode+20,"trace-producer-",15))return TraceProducer::Run();
    if(Case("transition-race") || Case("transition-race-prepare"))return TransitionRace();
    if(Case("borrow-before-builder") || Case("borrow-before-builder-prepare"))return BorrowBeforeBuilder();
    if(Case("borrow-at-retirement") || Case("borrow-at-retirement-same-thread"))return BorrowAtRetirement();
    if(!std::strncmp(mode+20,"corrected-",10))return CorrectedCapture::Run();
    if(!std::strncmp(mode+20,"startup-",8)) {
        Require(!Guard::BeginOperation().admitted);
        const auto installed=installations;
        if(Case("startup-producer-active"))TraceProducer::AfterInstall();
        // Remove external signature/table/worker faults before retry. The
        // failed startup itself must remain unavailable, not merely re-fail
        // on the same corrupted input or on the fixture's callback routing.
        Setup(false);
        if(!Case("startup-identity"))Require(!Guard::InstallStartup(image,true) && installations==installed);
        InstallCallbacks();
        if(Case("startup-partial")) {Submit(Build(0));RunBody(0);Cleanup();Require(builder_calls==1);}
        if(Case("startup-partial-start") || Case("startup-partial-helper"))TraceProducer::Run();
        if(Case("startup-partial-start") || Case("startup-partial-helper")
            || Case("startup-producer-returned") || Case("startup-producer-active")) {
            const auto sample=Guard::Inspect(Guard::BeginOperation());const auto& p=sample.producers;
            std::printf("trace startup coverage=%u starts=%llu helpers=%llu start_returns=%llu helper_returns=%llu "
                "active=%llu native_starts=%u native_helpers=%u reads_after_return=%u\n",unsigned(sample.state),
                p.entries[0],p.entries[1],p.returns[0],p.returns[1],p.active[0]+p.active[1],
                TraceProducer::starts.load(),TraceProducer::helpers.load(),forbidden_observer_reads.load());
        }
        std::printf("material startup rejected pinned_hooks=%u retry_rejected=%u\n",
            installed>=22?installed-22:0,unsigned(!Case("startup-identity")));return 0;
    }
    InstallCallbacks();
    Sc6ReplayHost host;auto& transaction=*host.historical_restore_;
    Require(transaction.material.admitted);
    Require(host.CanReleaseHistoricalRestore()); // Empty material baseline control only.
    if(Case("observer-closed"))Require(Horse::Deterministic::NativeReplayVfxCompletionObservation::ClosePhase(337));
    auto& ownership=transaction.value.ownership;
    Require(ownership.PublishA() && ownership.ActivateExecution() && ownership.SettleC(217) && ownership.BeginTargetTails());
    Field<std::uint64_t>(reinterpret_cast<void*>(image),0x4197170)=3;
    Require(host.copy_.DrainExecutionWork());host.copy_.Poll();
    if(Case("active-builder")) {
        std::thread worker([]{Submit(Build(0));});
        while(!builder_entered.load(std::memory_order_acquire))std::this_thread::yield();
        auto sample=Guard::Inspect(transaction.material);
        Require(sample.state==State::CpuPending && sample.builders==1 && sample.tasks==0);
        host.DriveRetirement();Require(native_retirements==std::array<unsigned,3>{});
        builder_release.store(true,std::memory_order_release);worker.join();
        sample=Guard::Inspect(transaction.material);Require(sample.builders==0 && sample.tasks==1);
    } else if(Case("unknown")) {
        std::array<std::uintptr_t,3> output{};SetLastError(0x661);
        NativeVectorBuild(output.data(),nullptr,0xff);Submit(0);RunBody(0);
        Require(Guard::Inspect(transaction.material).state==State::CoverageLost);
    } else if(Case("overflow")) {
        for(unsigned i=0;i<1025;++i)Submit(Build(i%2));
        Require(builder_calls==1025 && Guard::Inspect(transaction.material).state==State::CoverageLost);
    } else if(Case("duplicate")) {
        Submit(Build(0));reuse_next=tasks[0].address;Submit(Build(0));
        Require(Guard::Inspect(transaction.material).state==State::CoverageLost);
        // Controlled illegal reuse has two callbacks targeting one allocation.
        // First return revokes it; reopen only for the independent native body.
        RunBody(0);DWORD old{};Require(VirtualProtect(tasks[1].address,0x1000,PAGE_READWRITE,&old)!=FALSE);
        std::memset(tasks[1].address,0,0x1000);Field<std::uintptr_t>(tasks[1].address,0)=image+0x39150e8;
        tasks[1].submitted=false;Submit(1);RunBody(1);
    } else if(Case("early-callback")) {
        Build(0);Require(Guard::Inspect(transaction.material).state==State::CoverageLost);
    } else if(Case("reuse") || Case("return-value")) {
        Submit(Build(0));RunBody(0);
        const auto sample=Guard::Inspect(transaction.material);
        if(Case("reuse"))Require(tasks.size()==2 && sample.tasks==1 && sample.state==State::CpuPending);
        else Require(sample.tasks==0 && sample.state==State::Unsupported);
    } else if(!Case("no-material") && !Case("post-queue")) {
        Submit(Build(0));Submit(Build(1));
    }
    host.DriveSettlement();Require(!host.settlement_commands);
    if(!Case("particle-pending")) {
        host.copy_.completion_.signaled=true;host.copy_.Poll();Require(host.copy_.completion_.retired());
        if(Case("vector-returned"))RunBody(0);
        if(Case("refresh-returned"))RunBody(1);
        if(Case("both-returned")) {RunBody(1);RunBody(0);} // Deliberately reverse native service order.
        host.DriveSettlement();
        if(Case("post-queue")) {
            Require(host.command_queued);Submit(Build(0));Submit(Build(1));
            host.ExecuteSettlement();Require(transaction.material_command_deferred.load() && !host.copy_.execution_settled_);
            host.DriveSettlement();
        } else if(host.command_queued)host.ExecuteSettlement();
    }
    if(Case("no-material")) {
        Require(transaction.value.render==Render::Settled && host.copy_.execution_settled_);
        // This proves CPU-idle settlement only. No fixture-supplied provider
        // census grants exclusion of uninstrumented raw MID producers.
        Require(!transaction.material_failed && Guard::Inspect(transaction.material).state==State::Clear);
        host.DriveRetirement();Require(native_retirements==std::array<unsigned,3>{});
        Require(transaction.material_failed && !host.CanReleaseHistoricalRestore()
            && !std::strcmp(transaction.witness.participant,"material_C_only_producer_uncovered")
            && ownership.retains_undo() && !ownership.commit_decided());
        std::puts("CPU-idle progress render_settled=1 C_only_rejected=1 retirements=0 B_retained=1");
    } else {
        host.DriveRetirement();Require(native_retirements==std::array<unsigned,3>{});
        Require(transaction.value.render!=Render::Settled && !host.copy_.execution_settled_ && !ownership.commit_decided()
            && !host.CanReleaseHistoricalRestore());
        unsigned vector_returns{},refresh_returns{};
        for(const auto& task:tasks)(task.family?refresh_returns:vector_returns)+=task.returns;
        std::printf("material CPU boundary particle_complete=%u vector_returns=%u refresh_returns=%u pending_bodies=%u render_settled=0\n",
            unsigned(host.copy_.witness_.execution_work_complete),vector_returns,refresh_returns,
            unsigned(tasks.size())-vector_returns-refresh_returns);
        if(Case("timeout")) {
            Require(transaction.witness.pending && !transaction.material_failed);
            host_clock+=5001;Require(!host.CheckHistoricalMaterialBoundary());
            Require(transaction.material_failed && !std::strcmp(transaction.witness.participant,"material_cpu_timeout"));
        }
    }
    Cleanup();
    if(!host.copy_.completion_.retired()) {host.copy_.completion_.signaled=true;host.copy_.Poll();}
    if(!Case("no-material")) {
        Require(!host.CheckHistoricalMaterialBoundary() && transaction.material_failed
            && transaction.witness.phase==Sc6ReplayHost::RestoreOperationPhase::Failed
            && !transaction.witness.original_recovered && !ownership.commit_decided()
            && ownership.retains_undo() && host.copy_.witness_.native_write_uncommitted);
        Require(!host.MaterialCopyTransitionAllowed());
        const auto sample=Guard::Inspect(transaction.material);
        Require(sample.state==State::Unsupported || sample.state==State::CoverageLost);
        std::printf("material terminal state=%u builders=%llu tasks=%llu failed_session=1 owners_retained=1 retirements=0\n",
            unsigned(sample.state),sample.builders,sample.tasks);
    }
    if(tasks.size()==2 && tasks[0].family==0 && tasks[1].family==1)
        std::puts("material callbacks forwarded once: vector=1 refresh=1 task_pages_revoked=2");
    std::printf("material guard control PASS case=%s builder_calls=%u callback_returns=%u forbidden_return_reads=%u\n",
        mode+20,builder_calls,revoked,forbidden_observer_reads.load());
    if(!Case("observer-disabled"))ProductionCompletion();
    std::puts("isolated settlement only: admission_unexercised=1 recovery_unexercised=1 downstream_unproved=1");
    return 0;
}

}

bool ProbeHook(std::uint64_t target) {
    if(std::strstr(mode,"creation-install"))return target-image!=0x8c8f40;
    if(C18CorrelationProbe::RowCase("install"))return target-image!=0x1f05150;
    if(C18CorrelationProbe::SlotCase("hook-get"))return target-image!=0x1dc8220;
    if(C18CorrelationProbe::SlotCase("hook-set"))return target-image!=0x1f1e420;
    if(!std::strcmp(mode,"material-settlement-startup-partial"))return target-image!=0x1f15fc0;
    if(!std::strcmp(mode,"material-settlement-startup-partial-start"))return target-image!=0x8d8c40;
    if(!std::strcmp(mode,"material-settlement-startup-partial-helper"))return target-image!=0x8d5840;
    return true;
}
void ProbePublished(std::uint64_t target) {
    if(target-image==0x8d5840 && (!std::strcmp(mode,"material-settlement-startup-producer-returned")
        || !std::strcmp(mode,"material-settlement-startup-producer-active")))MaterialSettlement::TraceProducer::DuringInstall();
}
void ProbeMaterialCode(std::uint64_t target,std::uint64_t native) {
    const auto jump=[](unsigned char* p,std::uint64_t destination) {
        p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&destination,8);p[10]=0xff;p[11]=0xe0;
    };
    auto* p=reinterpret_cast<unsigned char*>(target);
    if(target-image==0x1dc8220) {
        // Preserve all 19 verified bytes. Both native short branches target
        // +31; fallthrough after the load at +12 reaches +19. Distinct tail
        // probes reject an incorrectly skipped/taken branch before forwarding
        // once to the external getter implementation with original RCX/EDX.
        jump(p+19,reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeGetFallthrough));
        jump(p+31,reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeGetBranch));
    } else if(target-image==0x1f05150) {
        // Preserve the entire supplied prefix, including the native RIP-relative
        // CMP after the 11-byte prologue. Undo only the external native frame.
        const unsigned char unwind[]{0x48,0x81,0xc4,0x88,0x00,0x00,0x00,0x41,0x5e,0x5b};
        std::memcpy(p+18,unwind,10);jump(p+28,native);
    } else if(target-image==0x8d1c00) {
        // Independent native prefix through SUB RSP,1D0; restore its saved
        // frame before tail-calling the external native dependency.
        const unsigned char unwind[]{0x48,0x81,0xc4,0xd0,0x01,0x00,0x00,0x41,0x5f,0x41,0x5e,0x41,0x5d,0x5f,0x5e,0x5b,0x5d};
        std::memcpy(p+30,unwind,sizeof(unwind));jump(p+30+sizeof(unwind),native);
    } else if(target-image==0x8c8f40) {
        const unsigned char unwind[]{0x48,0x83,0xc4,0x60,0x5f,0x48,0x8b,0x74,0x24,0x10,0x48,0x8b,0x5c,0x24,0x08};
        std::memcpy(p+20,unwind,sizeof(unwind));jump(p+20+sizeof(unwind),native);
    } else {
        const unsigned char unwind[]{0x48,0x83,0xc4,0x28,0x5e,0x5b};
        std::memcpy(p+16,unwind,6);jump(p+22,native);
    }
    DWORD old{};
    if(!VirtualProtect(p,0x1000,PAGE_EXECUTE_READWRITE,&old)
        || !FlushInstructionCache(GetCurrentProcess(),p,0x1000))std::abort();
}
std::uint64_t ProbeInstall(std::uint64_t target,std::uint64_t entry) {
    const auto rva=target-image;
    constexpr std::array<std::uintptr_t,34> sites{0x8c9120,0x3d74d0,0x3ea210,0x9481e0,0x3c5250,0x1f73990,0x215d250,0x400590,0x3a3b70,0x3ca7a0,
        0x43d210,0x399df0,0x3a1910,0x2050570,0x3ae520,0x912df0,0x2050940,0x3a1c50,0x3a22d0,0x17dfea0,0x399420,0x3a1a70,
        0x1f15fc0,0x1f32d90,0x1f15740,0x1f304e0,0x8d8c40,0x8d5840,0x1dc8a90,0x1dc8220,0x1f1e420,0x1f05150,0x8d1c00,0x8c8f40};
    const auto slot=std::find(sites.begin(),sites.end(),rva)-sites.begin();
    if(slot==sites.size() || entries[slot])std::abort(); // competing executor hooks forbidden
    entries[slot]=entry;++installations;
    const std::array<std::uint64_t,34> natives{
        reinterpret_cast<std::uint64_t>(&NativeRegister),reinterpret_cast<std::uint64_t>(&NativeDispatch),
        reinterpret_cast<std::uint64_t>(&NativePrune),reinterpret_cast<std::uint64_t>(&NativeCopy),
        reinterpret_cast<std::uint64_t>(&NativeDisable),reinterpret_cast<std::uint64_t>(&NativeCompleteParticle),
        reinterpret_cast<std::uint64_t>(&NativeTask),reinterpret_cast<std::uint64_t>(&NativeHub),
        reinterpret_cast<std::uint64_t>(&NativeHubRegister),reinterpret_cast<std::uint64_t>(&NativeHubRemove),
        reinterpret_cast<std::uint64_t>(&NativeOneArgumentAppend),reinterpret_cast<std::uint64_t>(&NativeCompact),
        reinterpret_cast<std::uint64_t>(&NativeRange),reinterpret_cast<std::uint64_t>(&NativeBacking),
        reinterpret_cast<std::uint64_t>(&NativeCollectionDestroy),reinterpret_cast<std::uint64_t>(&NativeHubDestroy),
        reinterpret_cast<std::uint64_t>(&NativeGrow),reinterpret_cast<std::uint64_t>(&NativeCapacity),
        reinterpret_cast<std::uint64_t>(&NativeRowClear),reinterpret_cast<std::uint64_t>(&NativeRowCopy),
        reinterpret_cast<std::uint64_t>(&NativeRowResize),reinterpret_cast<std::uint64_t>(&NativeRowBacking),
        reinterpret_cast<std::uint64_t>(&MaterialSettlement::NativeVector),reinterpret_cast<std::uint64_t>(&MaterialSettlement::NativeRefresh),
        reinterpret_cast<std::uint64_t>(&MaterialSettlement::NativeVectorBuild),reinterpret_cast<std::uint64_t>(&MaterialSettlement::NativeRefreshBuild),
        reinterpret_cast<std::uint64_t>(&MaterialSettlement::TraceProducer::NativeStart),
        reinterpret_cast<std::uint64_t>(&MaterialSettlement::TraceProducer::NativeHelper),
        reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeCount),
        reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeGet),
        reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeSet),
        reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeMaterialDispatch),
        reinterpret_cast<std::uint64_t>(&TraceCreationFixture::NativeFactory),
        reinterpret_cast<std::uint64_t>(&TraceCreationFixture::NativeConstruct)};
    if(std::strstr(mode,"trace-diagnostic-creation-") && slot==26)
        return reinterpret_cast<std::uint64_t>(&TraceCreationFixture::NativeStart);
    if(CorrelationMode() && slot==26)return reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeStart);
    if(CorrelationMode() && slot==27)return reinterpret_cast<std::uint64_t>(&C18CorrelationProbe::NativeHelper);
    if(!std::strncmp(mode,"material-settlement-start-veto-",31) && slot==26)
        return reinterpret_cast<std::uint64_t>(&MaterialSettlement::TraceStartVeto::NativeStart);
    if(!std::strncmp(mode,"trace-diagnostic-request-",25) && slot==26)
        return reinterpret_cast<std::uint64_t>(&TraceStartDiagnosticFixture::NativeRequestStart);
    return natives[slot];
}
static void Prune(void* c) {
    const auto target=entries[2]?entries[2]:reinterpret_cast<std::uint64_t>(&NativePrune);
    expected_writer_collection=c;
    SetLastError(0x663);reinterpret_cast<void(*)(void*)>(target)(c);
    if(GetLastError()!=0x773)std::abort();
}
static void* CopyCollection(void* c,void* source) {
    const auto target=entries[3]?entries[3]:reinterpret_cast<std::uint64_t>(&NativeCopy);
    expected_writer_collection=c;
    SetLastError(0x664);auto* result=reinterpret_cast<void*(*)(void*,void*)>(target)(c,source);
    if(GetLastError()!=0x774)std::abort();return result;
}
static void Dispatch() {
    const auto target=entries[1]?entries[1]:reinterpret_cast<std::uint64_t>(&NativeDispatch);
    SetLastError(0x662);
    reinterpret_cast<void(*)(void*,void*)>(target)(collection,&payload);
    if(GetLastError()!=0x772)std::abort();
}

namespace TraceStartDiagnosticFixture {
// Only native storage, indexed objects, logging and read interventions are
// supplied here. Selection, census, generation checks and formatting are the
// production diagnostic. No attachment ownership/admission is granted.
struct Array {std::uintptr_t data{};int count{},capacity{};};
struct Pair {std::uintptr_t state{},controller{};};
alignas(8) static std::array<std::byte,0x500> root{},fighter{},owner{};
alignas(8) static std::array<std::array<std::byte,0x200>,272> objects{};
alignas(8) static std::array<std::array<std::byte,0x100>,273> controllers{};
static std::array<std::array<Pair,260>,2> refs{};
static std::array<std::array<std::uintptr_t,2>,272> members{};
static std::array<unsigned,9> bits{};
static std::array<std::uintptr_t,2> listener{},listener_table{};
static std::uintptr_t listener_pointer{};
static std::array<unsigned char,0x30> request{};
static bool intervened{};
static bool Case(const char* value){return !std::strcmp(mode+17,value);}
static bool ZeroCase(){return !std::strncmp(mode+17,"zero-",5);}
static void Check(bool value){if(!value)std::abort();}
static void Object(void* p,int index,std::uintptr_t table) {
    Field<std::uintptr_t>(p,0)=image+table;Field<int>(p,0xc)=index;
    RC::Unreal::FUObjectArray::items[index]={p,100+index};
}
static Pair PairFor(unsigned n,bool attachment=true) {
    auto* p=controllers[n].data();
    Field<std::uintptr_t>(p,0)=image+0x3362590;Field<int>(p,8)=1;
    Field<void*>(p,0xc8)=attachment?objects[n].data():nullptr;
    return {reinterpret_cast<std::uintptr_t>(p)+16,reinterpret_cast<std::uintptr_t>(p)};
}
static void Append(unsigned c,Pair pair) {
    auto& h=Field<Array>(root.data(),0x418+c*16);
    refs[c][h.count++]=pair;
}
static void Membership(unsigned count) {
    for(unsigned n=0;n<count;++n) {
        members[n][0]=reinterpret_cast<std::uintptr_t>(objects[n].data());bits[n/32]|=1u<<(n%32);
    }
    Field<Array>(owner.data(),0x2c0)={reinterpret_cast<std::uintptr_t>(members.data()),int(count),272};
    Field<void*>(owner.data(),0x2e0)=bits.data();Field<int>(owner.data(),0x2e8)=int(count);
    Field<int>(owner.data(),0x2f4)=0;
}
static void Intervene(std::uintptr_t address,SIZE_T bytes,bool after) {
    if(!after || intervened)return;
    if(Case("unstable-ref") && address==reinterpret_cast<std::uintptr_t>(refs[0].data()) && bytes==16) {
        refs[0][0].state+=8;intervened=true;
    }
    if(Case("unstable-header") && address==reinterpret_cast<std::uintptr_t>(root.data())+0x428 && bytes==16) {
        --Field<Array>(root.data(),0x428).capacity;intervened=true;
    }
    if(Case("zero-unstable-vtable") && address==reinterpret_cast<std::uintptr_t>(objects[0].data()) && bytes==8) {
        Field<std::uintptr_t>(objects[0].data(),0)+=8;intervened=true;
    }
    if(Case("zero-unstable-index") && address==reinterpret_cast<std::uintptr_t>(objects[0].data())+0xc && bytes==4) {
        Field<int>(objects[0].data(),0xc)=275;intervened=true;
    }
    if(Case("zero-late-ref") && address==reinterpret_cast<std::uintptr_t>(objects[36].data())+0x188 && bytes==4) {
        refs[0][0]=PairFor(1);intervened=true;
    }
    if(Case("zero-owner-header") && address==reinterpret_cast<std::uintptr_t>(members.data()) && bytes==8) {
        --Field<Array>(owner.data(),0x2c0).capacity;intervened=true;
    }
}
// Exercise the existing owned wrapper, with only its native callee mocked.
// Invalid inputs check diagnostic selection/forwarding, not native tolerance.
static unsigned request_calls{},entry_callbacks{},exit_callbacks{},selected_requests{},request_reads{},exit_request_reads{};
static void* expected_request{};
static unsigned char* request_pages{};
static bool request_returning{};
static void RequestRead(std::uintptr_t address,SIZE_T bytes,bool after) {
    const auto input=reinterpret_cast<std::uintptr_t>(expected_request);
    if(!input || address<input || address-input>=0x30)return;
    const auto offset=address-input;
    Check(bytes==1 && (offset==0 || offset==9 || offset==0x10));
    if(!after){++request_reads;if(request_returning)++exit_request_reads;}
    const bool tear=(Case("request-torn-flag") && offset==9)
        || (Case("request-torn-parts") && offset==0) || (Case("request-torn-kind") && offset==0x10);
    if(after && tear && !intervened) {
        *reinterpret_cast<unsigned char*>(address)^=1;intervened=true;
    }
}
static bool RequestDiagnostic(std::uintptr_t base,void* component,void* input,bool returning) noexcept {
    Check(base==image && component==root.data() && input==expected_request);
    if(returning)++exit_callbacks;else ++entry_callbacks;
    const bool selected=Horse::Deterministic::NativeReplayTraceStartDiagnostic::Observe(base,component,input,returning);
    if(selected)++selected_requests;
    SetLastError(0xc11); // Both observer phases deliberately disturb LastError.
    return selected;
}
static void __fastcall NativeRequestStart(void* component,void* input) {
    Check(component==root.data() && input==expected_request && GetLastError()==0xa11);
    ++request_calls;
    if(Case("request-exit-changed")) {
        auto* bytes=static_cast<unsigned char*>(input);bytes[0]=201;bytes[9]=0;bytes[0x10]=202;
    }
    if(Case("request-exit-unreadable")) {
        DWORD old{};Check(VirtualProtect(request_pages,0x1000,PAGE_NOACCESS,&old)!=FALSE);
    }
    request_returning=true;SetLastError(0xb11);
}
static void CallRequest(void* input) {
    expected_request=input;request_returning=false;
    const auto before=request_calls;
    SetLastError(0xa11);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(entries[26])(root.data(),input);
    Check(GetLastError()==0xb11 && request_calls==before+1);
}
static int RequestRun() {
    using Guard=Horse::Deterministic::NativeReplayMaterialTaskGuard;
    auto& diagnostics=Horse::Deterministic::g_replay_diagnostics;
    MaterialSettlement::Setup(false);Check(Guard::InstallStartup(image,true) && entries[26]);
    Guard::ObserveStarts(&RequestDiagnostic);
    // Exact 0x30-byte request ends at a protected page boundary.
    request_pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(request_pages!=nullptr);DWORD old{};
    Check(VirtualProtect(request_pages+0x1000,0x1000,PAGE_NOACCESS,&old)!=FALSE);
    auto* input=request_pages+0x1000-request.size();
    std::memcpy(input,request.data(),request.size());
    diagnostics.mask=4;diagnostics.byte_limit=65536;
    shape_read_intervention=&RequestRead;
    const bool skip=Case("request-false-only") || Case("request-false-then-true")
        || Case("request-null") || Case("request-unreadable") || Case("request-unreadable-kind")
        || Case("request-torn-flag") || Case("request-torn-parts") || Case("request-torn-kind");
    if(skip) {
        if(Case("request-false-only") || Case("request-false-then-true"))input[9]=0;
        // The last case leaves +0/+9 readable while +10 crosses the guard page.
        if(Case("request-unreadable-kind")){request_pages[0xff0]=7;request_pages[0xff9]=1;}
        CallRequest(Case("request-null")?nullptr:Case("request-unreadable")?request_pages+0x1000:
            Case("request-unreadable-kind")?request_pages+0xff0:input);
        Check(!selected_requests && !exit_callbacks && !diagnostics.Overflowed());
        if(std::strstr(mode,"request-torn-"))Check(intervened);
        if(Case("request-false-only")) {
            CallRequest(input);
            Check(!selected_requests && !exit_callbacks && diagnostics.Admit(4,172,65536)==1);
        } else std::memcpy(input,request.data(),request.size());
    }
    if(!Case("request-false-only")) {
        const auto reads=request_reads;
        CallRequest(input);
        Check(selected_requests==1 && exit_callbacks==1 && request_reads==reads+6);
        Check(VirtualProtect(request_pages,0x1000,PAGE_READWRITE,&old)!=FALSE);
        std::memcpy(input,request.data(),request.size());
        CallRequest(input); // Only the first stable create request is selected.
        Check(selected_requests==1 && exit_callbacks==1 && request_reads==reads+6);
    }
    shape_read_intervention=nullptr;
    Check(!exit_request_reads && entry_callbacks==request_calls && !Guard::HistoricalRestoreSupported());
    Check(VirtualFree(request_pages,0,MEM_RELEASE)!=FALSE);
    std::printf("trace request fixture complete native_calls=%u entry_callbacks=%u exit_callbacks=%u selected=%u exit_request_reads=%u\n",
        request_calls,entry_callbacks,exit_callbacks,selected_requests,exit_request_reads);
    return 0;
}
static int Run() {
    using Horse::Deterministic::NativeReplayTraceStartDiagnostic;
    using Horse::Deterministic::g_replay_diagnostics;
    Object(root.data(),1,0x3360ca8);Object(fighter.data(),2,0x2000);Object(owner.data(),3,0x3361f98);
    Field<void*>(root.data(),0x490)=fighter.data();Field<void*>(fighter.data(),0x458)=owner.data();
    Field<void*>(owner.data(),0x3a8)=root.data();Field<void*>(root.data(),0x190)=owner.data();
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x3361f98),0x70)=image+0x1234;
    Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x3360370),0x370)=image+0x5678;
    for(unsigned n=0;n<objects.size();++n) {
        Object(objects[n].data(),int(n+4),0x3360370);
        Field<void*>(objects[n].data(),0x190)=owner.data();Field<unsigned char>(objects[n].data(),0x18d)=1;
    }
    for(unsigned c=0;c<2;++c)Field<Array>(root.data(),0x418+c*16)={reinterpret_cast<std::uintptr_t>(refs[c].data()),0,260};
    listener_table[1]=image+0x9876;listener[0]=reinterpret_cast<std::uintptr_t>(listener_table.data());
    listener_pointer=reinterpret_cast<std::uintptr_t>(listener.data());
    Field<Array>(reinterpret_cast<void*>(image),0x42a1290)={reinterpret_cast<std::uintptr_t>(&listener_pointer),1,1};
    Field<unsigned>(reinterpret_cast<void*>(image),0x470d0c4)=172;
    const unsigned old=(Case("ref-limit") || Case("zero-ref-limit"))?254:36;
    for(unsigned n=0;n<old;++n)Append(n<old/2?0:1,PairFor(n));
    if(Case("nulls")){Append(1,{});Append(1,PairFor(272,false));}
    Membership(old);
    if(Case("entry-header"))Field<Array>(root.data(),0x418).count=-1;
    if(Case("entry-overflow"))Field<Array>(root.data(),0x418).count=256; // total exceeds 256.
    // Model lazy FUObjectItem serials, including the live tick172 shape. No
    // serial allocation/promotion is supplied to the production observer.
    if(ZeroCase()) {
        RC::Unreal::FUObjectArray::items[1].serial=8104;
        if(!Case("zero-new") && !Case("zero-old"))RC::Unreal::FUObjectArray::items[3].serial=0;
        for(unsigned n=0;n<objects.size();++n)
            if(!Case("zero-manager") && (!Case("zero-new") || n>=old)
                && (!Case("zero-old") || n<old))RC::Unreal::FUObjectArray::items[n+4].serial=0;
        if(Case("zero-component"))RC::Unreal::FUObjectArray::items[1].serial=0;
    }
    g_replay_diagnostics.first_tick=172;g_replay_diagnostics.last_tick=172;
    request[0]=7;request[9]=1;request[0x10]=13;
    if(!std::strncmp(mode+17,"creation-",9))return TraceCreationFixture::Run();
    if(!std::strncmp(mode+17,"request-",8))return RequestRun();
    g_replay_diagnostics.byte_limit=Case("budget")?65535:65536;
    Check(!NativeReplayTraceStartDiagnostic::Observe(image,root.data(),request.data(),false)); // disabled
    g_replay_diagnostics.mask=4;g_replay_diagnostics.first_tick=173;
    Check(!NativeReplayTraceStartDiagnostic::Observe(image,root.data(),request.data(),false)); // outside window
    g_replay_diagnostics.first_tick=172;
    std::array<int,276> entry_serials{};
    for(unsigned i=0;i<entry_serials.size();++i)entry_serials[i]=RC::Unreal::FUObjectArray::items[i].serial;
    const bool selected=NativeReplayTraceStartDiagnostic::Observe(image,root.data(),request.data(),false);
    for(unsigned i=0;i<entry_serials.size();++i)Check(entry_serials[i]==RC::Unreal::FUObjectArray::items[i].serial);
    Check(selected!=Case("budget"));
    Check(!NativeReplayTraceStartDiagnostic::Observe(image,root.data(),request.data(),false)); // nested entry
    if(selected) {
        // This controlled native interval mutates only native storage. The
        // independent test oracle expects one new identity beyond 36 old ones.
        const unsigned added=Case("no-new")?0:(Case("detail-overflow") || Case("zero-detail-overflow"))?17:1;
        for(unsigned n=0;n<added;++n)Append(1,PairFor(old+n));
        if(Case("duplicates")){Append(0,PairFor(0));Append(0,PairFor(old));}
        if(Case("ref-limit") || Case("zero-ref-limit"))Append(0,PairFor(0)); // 256 refs, 255 keys
        Membership(old+added);
        if(Case("exit-header"))Field<Array>(root.data(),0x428).capacity=0;
        if(Case("exit-overflow"))Field<Array>(root.data(),0x428).count=256;
        if(Case("component-generation"))++RC::Unreal::FUObjectArray::items[1].serial;
        if(Case("attachment-generation"))++RC::Unreal::FUObjectArray::items[4].serial;
        if(Case("removed-generation")) {
            refs[0][0]={};++RC::Unreal::FUObjectArray::items[4].serial;
        }
        if(Case("removed") || Case("zero-removed"))refs[0][0]={}; // object still indexed/live
        if(Case("removed-overflow") || Case("zero-removed-overflow"))
            for(unsigned n=0;n<17;++n)refs[0][n]={};
        if(Case("zero-promoted-serial"))++RC::Unreal::FUObjectArray::items[4].serial;
        if(Case("zero-removed-stale")) {
            refs[0][0]={};RC::Unreal::FUObjectArray::items[4].object=nullptr;
        }
        if(Case("zero-reused-index")) {
            refs[0][0]=PairFor(271);
            Field<int>(objects[271].data(),0xc)=4;
            RC::Unreal::FUObjectArray::items[4]={objects[271].data(),0};
            RC::Unreal::FUObjectArray::items[275].object=nullptr;
        }
        if(Case("zero-reused-pointer")) {
            Field<int>(objects[0].data(),0xc)=275;
            RC::Unreal::FUObjectArray::items[275]={objects[0].data(),0};
            RC::Unreal::FUObjectArray::items[4].object=nullptr;
        }
        if(Case("zero-manager-reused-index")) {
            Object(objects[271].data(),3,0x3361f98);
            RC::Unreal::FUObjectArray::items[3].serial=0;
        }
        if(Case("zero-aba-unobserved")) {
            // The same address/index/vtable/zero serial is restored between
            // observations. No read-only snapshot can prove this generation.
            RC::Unreal::FUObjectArray::items[4].object=nullptr;
            RC::Unreal::FUObjectArray::items[4]={objects[0].data(),0};
        }
        if(Case("unreadable-data"))Field<Array>(root.data(),0x428).data=1;
        shape_read_intervention=&Intervene;
        std::array<int,276> serials{};
        for(unsigned i=0;i<serials.size();++i)serials[i]=RC::Unreal::FUObjectArray::items[i].serial;
        Check(!NativeReplayTraceStartDiagnostic::Observe(image,root.data(),request.data(),true));
        shape_read_intervention=nullptr;
        for(unsigned i=0;i<serials.size();++i)Check(serials[i]==RC::Unreal::FUObjectArray::items[i].serial);
    }
    Check(!Horse::Deterministic::NativeReplayMaterialTaskGuard::HistoricalRestoreSupported());
    Check(!NativeReplayTraceStartDiagnostic::Observe(image,root.data(),request.data(),false)); // one event only
    std::puts("trace diagnostic fixture complete");return 0;
}
}

namespace TraceCreationFixture {
using Guard=Horse::Deterministic::NativeReplayMaterialTaskGuard;
using Diagnostic=Horse::Deterministic::NativeReplayTraceStartDiagnostic;
using namespace TraceStartDiagnosticFixture;
static unsigned factories{},constructs{},starts{};
static unsigned scalar_payload_reads{};
static thread_local unsigned factory_depth{},construct_depth{},start_depth{};
static std::uintptr_t factory_thunk{},construct_thunk{};
static bool Case(const char* name){return !std::strcmp(mode+26,name);}
static void ReadScalars(std::uintptr_t address,SIZE_T,bool after) {
    if(!after && ((address>=0x12345 && address<0x12345+4096)
        || (address>=0x6789a && address<0x6789a+4096)))++scalar_payload_reads;
}
static bool Observe(std::uintptr_t base,void* component,void* input,bool returning) noexcept {
    const auto result=Diagnostic::Observe(base,component,input,returning);
    SetLastError(0xccc);return result;
}
static void Identify(std::uintptr_t object,Guard::CreationIdentity& out) noexcept {
    Diagnostic::IdentifyConstruction(object,out);SetLastError(0xddd);
}
static void* __fastcall NativeConstruct(void* outer,void* cls,std::uint64_t name,std::uint32_t flags,
    void* object_template,bool copy,void* graph) {
    Check(outer==root.data() && cls==fighter.data() && name==0xfedcba9876543210ull
        && flags==0x89abcdefu && object_template==owner.data() && copy
        && graph==objects[41].data() && GetLastError()==0xa33);
    ++constructs;++construct_depth;
    if(Case("wrapper-reentry") && construct_depth==1) {
        SetLastError(0xa33);
        Check(reinterpret_cast<decltype(&NativeConstruct)>(construct_thunk)(outer,cls,name,flags,object_template,copy,graph)==objects[40].data());
        Check(GetLastError()==0xb33);
    }
    --construct_depth;SetLastError(0xb33);return objects[40].data();
}
static Pair* __fastcall NativeFactory(void* component,Pair* out,void* parts,int setting,unsigned char kind) {
    Check(component==root.data() && out && parts==fighter.data() && setting==-7 && kind==0xe1 && GetLastError()==0xa22);
    ++factories;++factory_depth;
    if(Case("reentry") && factory_depth==1) {
        Pair nested{};SetLastError(0xa22);
        Check(reinterpret_cast<decltype(&NativeFactory)>(factory_thunk)(component,&nested,parts,setting,kind)==&nested);
        Check(GetLastError()==0xb22);
    }
    if(!Case("no-wrapper")) {
        SetLastError(0xa33);
        const auto callee=Case("wrapper-caller")?entries[33]:construct_thunk;
        Check(reinterpret_cast<decltype(&NativeConstruct)>(callee)(root.data(),fighter.data(),0xfedcba9876543210ull,
            0x89abcdefu,owner.data(),true,objects[41].data())==objects[40].data());
        Check(GetLastError()==0xb33);
    }
    // These deliberately inaccessible scalars must never be dereferenced as
    // state/controller storage by the observer.
    *out=Case("null-pair")?Pair{}:Pair{0x12345,0x6789a};
    --factory_depth;SetLastError(0xb22);return out;
}
static void CallFactory() {
    Pair out{};SetLastError(0xa22);
    const auto callee=Case("factory-caller")?entries[32]:factory_thunk;
    Check(reinterpret_cast<decltype(&NativeFactory)>(callee)(root.data(),&out,fighter.data(),-7,0xe1)==&out);
    Check(GetLastError()==0xb22);
    Check(out.state==(Case("null-pair")?0:0x12345) && out.controller==(Case("null-pair")?0:0x6789a));
}
static void __fastcall NativeStart(void* component,void* input) {
    Check(component==root.data() && input==request.data() && GetLastError()==0xa11);
    ++starts;++start_depth;
    if(Case("start-reentry") && start_depth==1) {
        SetLastError(0xa11);
        reinterpret_cast<decltype(&NativeStart)>(entries[26])(component,input);
        Check(GetLastError()==0xb11);
    }
    if(Case("wrong-thread")) {std::thread worker([]{CallFactory();});worker.join();}
    else if(!Case("no-factory"))for(unsigned i=0;i<(Case("overflow")?5u:1u);++i)CallFactory();
    --start_depth;SetLastError(0xb11);
}
static std::uintptr_t Caller(std::uintptr_t call_rva,std::uintptr_t target_rva,unsigned stack_args) {
    // External native dependencies execute the actual verified E8 call at its
    // real return PC. Preserve all Win64 register and stack arguments.
    auto* call=reinterpret_cast<unsigned char*>(image+call_rva);
    auto* begin=call-4-stack_args*10;auto* p=begin;
    const unsigned char sub[]{0x48,0x83,0xec,0x38};std::memcpy(p,sub,4);p+=4;
    for(unsigned i=0;i<stack_args;++i) {
        const unsigned char move[]{0x48,0x8b,0x44,0x24,static_cast<unsigned char>(0x60+i*8),
            0x48,0x89,0x44,0x24,static_cast<unsigned char>(0x20+i*8)};
        std::memcpy(p,move,10);p+=10;
    }
    Check(p==call);*p++=0xe8;
    const auto displacement=static_cast<std::int32_t>(target_rva-call_rva-5);
    std::memcpy(p,&displacement,4);p+=4;
    const unsigned char end[]{0x48,0x83,0xc4,0x38,0xc3};std::memcpy(p,end,5);
    DWORD old{};Check(VirtualProtect(begin,128,PAGE_EXECUTE_READWRITE,&old)!=FALSE);
    Check(FlushInstructionCache(GetCurrentProcess(),begin,128)!=FALSE);
    return reinterpret_cast<std::uintptr_t>(begin);
}
static int Run() {
    MaterialSettlement::Setup(false);
    // Independently copied from executable SHA256 f8904e4b...e04a553, not
    // production constants. Real-detour mode executes these prefixes too.
    constexpr unsigned char factory[]{0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8d,0xac,0x24,0x30,0xff,0xff,0xff,0x48,0x81,0xec,0xd0,0x01,0x00,0x00};
    constexpr unsigned char construct[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,
        0x57,0x48,0x83,0xec,0x60};
    std::memcpy(reinterpret_cast<void*>(image+0x8d1c00),factory,sizeof(factory));
    std::memcpy(reinterpret_cast<void*>(image+0x8c8f40),construct,sizeof(construct));
    ProbeMaterialCode(image+0x8d1c00,reinterpret_cast<std::uint64_t>(&NativeFactory));
    ProbeMaterialCode(image+0x8c8f40,reinterpret_cast<std::uint64_t>(&NativeConstruct));
    factory_thunk=Caller(0x8d907a,0x8d1c00,1);construct_thunk=Caller(0x8d1f3d,0x8c8f40,3);
    if(Case("signature"))*reinterpret_cast<unsigned char*>(image+0x8c8f40+8)^=1;
    Check(Guard::InstallStartup(image,true) && entries[26]);
    const bool absent=Case("signature") || Case("disabled");
    Check(bool(entries[32])!=absent && bool(entries[33])==(!absent && !Case("install")));
    // In the stub dependency only, route the independently emitted E8 calls
    // through installed production wrappers. Real mode uses actual patches.
#ifndef HORSE_REAL_MATERIAL_DETOUR
    for(const auto slot:{32u,33u})if(entries[slot]) {
        auto* p=reinterpret_cast<unsigned char*>(image+(slot==32?0x8d1c00:0x8c8f40));
        p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&entries[slot],8);p[10]=0xff;p[11]=0xe0;
    }
#endif
    // A rejected startup signature is restored only in this external native
    // dependency so subsequent native calls still exercise ordinary forwarding.
    if(Case("signature"))*reinterpret_cast<unsigned char*>(image+0x8c8f40+8)^=1;
    Check(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(image),0x4900000)!=FALSE);
    Guard::ObserveStarts(&Observe,&Identify);
    auto& diagnostic=Horse::Deterministic::g_replay_diagnostics;
    diagnostic.mask=4;diagnostic.byte_limit=Case("budget")?65536:73728;
    RC::Unreal::FUObjectArray::items[44].serial=0;
    shape_read_intervention=&ReadScalars;
    SetLastError(0xa11);reinterpret_cast<decltype(&NativeStart)>(entries[26])(root.data(),request.data());
    Check(GetLastError()==0xb11 && starts==(Case("start-reentry")?2u:1u));
    shape_read_intervention=nullptr;Check(!scalar_payload_reads);
    Guard::CreationObservation result{};Guard::CopyCreationObservation(result);
    const auto expected_factories=Case("no-factory")?0u:Case("overflow")?5u:(Case("reentry") || Case("start-reentry"))?2u:1u;
    const auto expected_constructs=Case("no-wrapper")?0u:Case("wrapper-reentry")?2u:expected_factories;
    Check(factories==expected_factories && constructs==expected_constructs);
    Check(result.selected!=Case("disabled") && result.ended!=Case("disabled")
        && !result.active && !result.pending && !Guard::HistoricalRestoreSupported());
    Check(result.ready==(!absent && !Case("install")));
    const bool complete=Case("normal") || Case("null-pair") || Case("no-wrapper") || Case("no-factory") || Case("budget");
    Check(result.Complete()==complete);
    if(Case("normal") || Case("null-pair")) {
        Check(result.count==2 && result.calls[0].family==0 && result.calls[1].family==1);
        const auto& f=result.calls[0];const auto& w=result.calls[1];
        Check(f.returned && f.pair_stable && f.result==f.output && f.state==(Case("null-pair")?0:0x12345));
        Check(w.returned && w.parent==1 && w.result==reinterpret_cast<std::uintptr_t>(objects[40].data()));
        Check(w.identity.valid && w.identity.index==44 && w.identity.serial==0);
        Check(f.entry<w.entry && w.end<f.end && f.caller==image+0x8d907f && w.caller==image+0x8d1f42);
    }
    if(Case("wrong-thread"))Check(result.foreign_entries==2 && result.count==0);
    if(Case("no-wrapper"))Check(result.count==1 && constructs==0);
    if(Case("no-factory"))Check(result.count==0 && factories==0);
    if(Case("overflow"))Check(result.count==8 && result.omitted==2 && factories==5 && constructs==5);
    Check(RC::Unreal::FUObjectArray::items[44].serial==0);
    std::printf("trace creation fixture complete factories=%u constructs=%u read_only=true last_error_preserved=true\n",factories,constructs);
    return 0;
}
}

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>1)mode=argv[1];
    trace_start_direct=argc>2 && !std::strcmp(argv[2],"prepare");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4900000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image)return 1;
    if(!std::strncmp(mode,"trace-diagnostic-",17))return TraceStartDiagnosticFixture::Run();
    // Independent bytes from f8904e4b...e04a553, not the observer's own constants.
    constexpr unsigned char registration_signature[]{
        0x48,0x89,0x5c,0x24,0x08,0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x55,
        0x56,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xda,0x48,0x8b,0xf1,0x33,0xd2,0x48,0x8d,
        0x4c,0x24,0x20,0xe8,0x88,0x29,0x6b,0,0x33,0xff,0x48,0x8d,0x4c,0x24,0x20,0x48};
    constexpr unsigned char dispatch_signature[]{
        0x4c,0x8b,0xdc,0x53,0x56,0x41,0x57,0x48,0x81,0xec,0xb0,0,0,0,0x48,0x8b,
        0x05,0x7b,0x1d,0xd1,0x03,0x48,0x33,0xc4,0x48,0x89,0x84,0x24,0x80,0,0,0,
        0x8b,0x59,0x08,0x4c,0x8b,0xfa,0x48,0x8b,0xf1,0x85,0xdb,0x0f,0x8e,0x4c,0x02,0};
    std::memcpy(reinterpret_cast<void*>(image+0x8c9120),registration_signature,sizeof(registration_signature));
    std::memcpy(reinterpret_cast<void*>(image+0x3d74d0),dispatch_signature,sizeof(dispatch_signature));
    constexpr unsigned char prune_signature[]{
        0x40,0x56,0x57,0x48,0x83,0xec,0x28,0x33,0xf6,0x48,0x8b,0xf9,0x39,0x71,0x08,0x0f,
        0x8e,0x91,0,0,0,0x48,0x89,0x5c,0x24,0x48,0x8b,0xde,0x48,0x89,0x6c,0x24,
        0x50,0x8d,0x6e,0x01,0x4c,0x89,0x74,0x24,0x20,0x44,0x8b,0xf6,0x48,0x89,0x74,0x24};
    constexpr unsigned char copy_signature[]{
        0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x3b,0xca,0x74,0x76,0x44,0x8b,
        0x41,0x0c,0x48,0x89,0x5c,0x24,0x30,0x8b,0x5a,0x08,0x48,0x89,0x74,0x24,0x38,0x48,
        0x8b,0x32,0x89,0x59,0x08,0x85,0xdb,0x75,0x1b,0x45,0x85,0xc0,0x75,0x16,0x48,0x8b};
    std::memcpy(reinterpret_cast<void*>(image+0x3ea210),prune_signature,sizeof(prune_signature));
    std::memcpy(reinterpret_cast<void*>(image+0x9481e0),copy_signature,sizeof(copy_signature));
    // Independent read-only native MCP bytes, retained with the witness audit.
    constexpr unsigned char disable_signature[]{72,137,92,36,24,85,65,86,65,87,72,131,236,80,65,15,182,216,76,139,250,72,139,233,232,243,222,255,255,76,139,240,72,133,192,15,132,191,0,0,0,72,137,116,36,112,72,137};
    constexpr unsigned char completion_signature[]{64,83,87,65,87,72,129,236,0,1,0,0,128,61,233,55,34,2,0,72,139,217,116,18,255,21,130,138,43,1,59,5,184,55,34,2,64,15,148,199,235,3,64,183,1,72,139,131};
    std::memcpy(reinterpret_cast<void*>(image+0x3c5250),disable_signature,48);
    std::memcpy(reinterpret_cast<void*>(image+0x1f73990),completion_signature,48);
    constexpr unsigned char task_signature[]{72,137,108,36,24,72,137,124,36,32,65,86,72,131,236,48,128,121,32,0,73,139,232,68,139,242,72,139,249,116,77,128,121,33,0,116,71,72,139,1,72,137,92,36,64,72,137,116};
    constexpr unsigned char hub_signature[]{72,131,236,56,139,2,72,131,193,120,137,68,36,32,139,66,4,137,68,36,36,139,66,8,137,68,36,40,139,66,12,72,141,84,36,32,137,68,36,44,232,67,125,147,1,72,131,196};
    std::memcpy(reinterpret_cast<void*>(image+0x215d250),task_signature,48);
    std::memcpy(reinterpret_cast<void*>(image+0x400590),hub_signature,48);
    constexpr unsigned char hub_add_signature[]{64,85,83,86,87,65,84,65,86,65,87,72,139,236,72,129,236,128,0,0,0,72,139,5,212,86,212,3,72,51,196,72,137,69,240,69,51,228,77,139,241,77,139,248,76,137,101,208};
    constexpr unsigned char hub_remove_signature[]{72,137,92,36,16,72,137,108,36,24,86,87,65,87,72,131,236,32,131,121,100,0,76,139,250,72,139,241,15,142,254,0,0,0,76,139,65,64,64,50,237,76,137,116,36,64,73,139};
    std::memcpy(reinterpret_cast<void*>(image+0x3a3b70),hub_add_signature,48);
    std::memcpy(reinterpret_cast<void*>(image+0x3ca7a0),hub_remove_signature,48);
    const bool bad_registration=!std::strcmp(mode,"registration-signature");
    const bool bad_dispatch=!std::strcmp(mode,"dispatch-signature");
    const bool bad_prune=!std::strcmp(mode,"prune-signature");
    const bool bad_copy=!std::strcmp(mode,"copy-signature");
    const bool bad_disable=!std::strcmp(mode,"disable-signature");
    const bool bad_completion=!std::strcmp(mode,"completion-signature");
    const bool bad_task=!std::strcmp(mode,"task-signature"),bad_hub=!std::strcmp(mode,"hub-signature");
    const bool bad_hub_add=!std::strcmp(mode,"hub-add-signature"),bad_hub_remove=!std::strcmp(mode,"hub-remove-signature");
    if(bad_hub_add || bad_hub_remove)
        *reinterpret_cast<unsigned char*>(image+(bad_hub_add?0x3a3b70:0x3ca7a0)+47)^=1;
    if(bad_task || bad_hub)
        *reinterpret_cast<unsigned char*>(image+(bad_task?0x215d250:0x400590)+47)^=1;
    if(bad_disable || bad_completion)
        *reinterpret_cast<unsigned char*>(image+(bad_disable?0x3c5250:0x1f73990)+47)^=1;
    if(bad_registration || bad_dispatch)
        *reinterpret_cast<unsigned char*>(image+(bad_registration?0x8c9120:0x3d74d0)+47)^=1;
    if(bad_prune || bad_copy)
        *reinterpret_cast<unsigned char*>(image+(bad_prune?0x3ea210:0x9481e0)+47)^=1;
    // Independent MCP/PE-verified writer prefixes, game SHA f8904e4b...e04a553.
    {constexpr unsigned char signature[]{72,137,92,36,8,87,72,131,236,32,72,99,250,72,139,217,65,131,248,1,119,35,72,139,81,64,72,133,210,116,126,76,139,199,73,193,224,6,255,21,252,200,29,1,72,139,75,64};std::memcpy(reinterpret_cast<void*>(image+0x2050570),signature,48);}
    {constexpr unsigned char signature[]{72,137,92,36,8,72,137,116,36,16,87,72,131,236,32,72,99,121,80,139,242,72,139,217,131,255,1,119,7,184,1,0,0,0,235,56,131,123,84,0,185,4,0,0,0,117,5,72};std::memcpy(reinterpret_cast<void*>(image+0x2050940),signature,48);}
    {constexpr unsigned char signature[]{64,85,83,86,87,65,84,65,86,65,87,72,139,236,72,129,236,128,0,0,0,72,139,5,52,192,202,3,72,51,196,72,137,69,240,69,51,228,77,139,241,77,139,248,76,137,101,208};std::memcpy(reinterpret_cast<void*>(image+0x43d210),signature,48);}
    {constexpr unsigned char signature[]{64,83,72,131,236,32,131,121,100,0,72,139,217,15,143,200,0,0,0,132,210,116,15,255,73,96,139,65,96,59,65,80,15,143,181,0,0,0,72,137,108,36,48,139,105,80,72,137};std::memcpy(reinterpret_cast<void*>(image+0x399df0),signature,48);}
    {constexpr unsigned char signature[]{69,133,192,15,132,82,1,0,0,85,86,65,84,65,87,72,131,236,40,72,139,65,64,69,15,182,225,72,137,92,36,80,65,139,232,72,137,124,36,88,72,139,241,76,137,108,36,96};std::memcpy(reinterpret_cast<void*>(image+0x3a1910),signature,48);}
    {constexpr unsigned char signature[]{72,131,236,40,72,139,65,64,72,137,92,36,48,72,247,216,72,137,116,36,64,72,139,241,72,27,219,72,137,124,36,32,139,121,80,72,247,219,72,255,203,72,35,217,72,11,89,64};std::memcpy(reinterpret_cast<void*>(image+0x3ae520),signature,48);}
    {constexpr unsigned char signature[]{72,137,92,36,8,87,72,131,236,32,72,139,249,72,129,193,96,18,0,0,232,199,244,168,255,72,141,143,96,18,0,0,232,187,244,168,255,72,139,143,128,18,0,0,72,133,201,116};std::memcpy(reinterpret_cast<void*>(image+0x912df0),signature,48);}
    {constexpr unsigned char signature[]{72,137,92,36,8,87,72,131,236,32,72,99,218,72,139,249,133,210,116,42,131,251,1,119,7,187,1,0,0,0,235,30,72,139,203,51,210,72,193,225,6,232,66,241,154,0,72,193};std::memcpy(reinterpret_cast<void*>(image+0x3a1c50),signature,48);}
    {constexpr unsigned char signature[]{64,83,72,131,236,32,131,121,48,0,72,139,217,116,84,72,139,65,32,72,247,216,72,27,201,72,247,217,72,255,201,72,35,203,72,11,75,32,116,59,72,139,1,51,210,255,80,72};std::memcpy(reinterpret_cast<void*>(image+0x3a22d0),signature,48);}
    {constexpr unsigned char signature[]{64,83,72,131,236,32,72,139,194,72,139,217,72,139,200,186,40,0,0,0,232,103,149,187,254,72,133,192,116,48,72,141,13,43,184,246,1,72,137,8,139,83,8,137,80,8,139,75};std::memcpy(reinterpret_cast<void*>(image+0x17dfea0),signature,48);}
    {constexpr unsigned char signature[]{72,137,92,36,8,87,72,131,236,32,131,121,48,0,139,250,72,139,217,116,33,72,139,65,32,72,247,216,72,27,201,72,247,217,72,255,201,72,35,203,72,11,75,32,116,8,72,139};std::memcpy(reinterpret_cast<void*>(image+0x399420),signature,48);}
    {constexpr unsigned char signature[]{72,137,92,36,8,87,72,131,236,32,72,99,250,72,139,217,65,131,248,2,119,35,72,139,81,32,72,133,210,116,126,76,139,199,73,193,224,4,255,21,252,179,232,2,72,139,75,32};std::memcpy(reinterpret_cast<void*>(image+0x3a1a70),signature,48);}
    const bool bad_c18_signature=!std::strncmp(mode,"writer-signature-",17);
    if(bad_c18_signature) {
        constexpr std::uintptr_t rvas[]{0x43d210,0x399df0,0x3a1910,0x2050570,0x3ae520,0x912df0,
            0x2050940,0x3a1c50,0x3a22d0,0x17dfea0,0x399420,0x3a1a70};
        const auto site=std::atoi(mode+17);if(site<0 || site>=12)std::abort();
        *reinterpret_cast<unsigned char*>(image+rvas[site])^=1;
    }
    if(!std::strncmp(mode,"material-settlement-",20) || CorrelationMode())MaterialSettlement::Setup();
    ProductionStartup();
    collection=reinterpret_cast<Header*>(manager.data()+0x388);
    *collection={rows.data(),0,2};
    // External indexed UObject service only; no observer-side manager lease.
    *reinterpret_cast<std::uintptr_t*>(manager.data())=image+0x3356f68;
    *reinterpret_cast<int*>(manager.data()+0xc)=1;
    *reinterpret_cast<int*>(receiver.data()+0xc)=7;
    RC::Unreal::FUObjectArray::items[1]={manager.data(),31};
    RC::Unreal::FUObjectArray::items[7]={receiver.data(),11};
    const bool started=!std::strcmp(mode,"collection18-startup-active")
        || !std::strcmp(mode,"material-settlement-observer-disabled") || (ZeroSerialShapeMode()?ProductionDiagnosticStartup():
        Horse::Deterministic::NativeReplayVfxCompletionObservation::Start(L"vfx_completion_observation.json","vfx-observation-fixture"));
    if(!std::strncmp(mode,"material-settlement-",20)) {
        if(!started && std::strncmp(mode+20,"startup-",8))return 84;
        return MaterialSettlement::Run();
    }
    if(!std::strncmp(mode,"collection18-",13)) {
        if(!started || (installations!=10 && installations!=20 && installations!=22 && !(CorrelationMode() && installations>=28 && installations<=31)))return 84;
        return Collection18AppendBoundary();
    }
    if(!std::strcmp(mode,"zero")) {
        if(!started || (installations!=4 && installations!=6 && installations!=8 && installations!=10 && installations!=20 && installations!=22))return 10;
        ExitProcess(0);
    }
    const auto before=manager;
    nested=true;
    const auto target=entries[0]?entries[0]:reinterpret_cast<std::uint64_t>(&NativeRegister);
    SetLastError(0x661);
    reinterpret_cast<void(*)(void*,void*,std::uint64_t,std::uint64_t)>(target)(
        collection,receiver.data(),descriptor,function_name);
    if(GetLastError()!=0x771)return 11;
    nested=false;
    // Same-count receiver replacement before a second dispatch, on another
    // thread. This must be recorded AND forwarded, never vetoed or repaired.
    rows[0].serial=12;
    std::thread worker([]{Dispatch();});worker.join();
    alignas(8) auto expected=before;
    reinterpret_cast<Header*>(expected.data()+0x388)->count=1;
    if(manager!=expected || rows[0]!=Row{7,12,function_name} || rows[1]!=Row{}
        || payload!=0xaabbccdd || registrations!=1 || dispatches!=2)return 2;
    std::puts("native forwarding PASS: registration=1 dispatch=2 changed receiver preserved");
    std::printf("collection=%llu storage=%llu receiver=%llu payload=%llu main_thread=%lu\n",
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(collection)),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(rows.data())),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(receiver.data())),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&payload)),GetCurrentThreadId());
    if(bad_registration || bad_dispatch || bad_prune || bad_copy || bad_disable || bad_completion || bad_task || bad_hub || bad_hub_add || bad_hub_remove || bad_c18_signature) {
        if(installations || started)return 3;
        std::puts("signature mismatch forwarded without hooks");return 0;
    }
    // Writer modes must reach native even against the retained two-hook RED.
    const bool writers=!std::strncmp(mode,"writers",7);
    if((installations!=4 && installations!=6 && installations!=8 && installations!=10 && installations!=20 && installations!=22 && !writers) || !started) {
        std::printf("G1 observation RED: no production entry observation; hooks=%u started=%d\n",installations,started);
        return 126;
    }
    if(!std::strncmp(mode,"producers",9)) {
        if(!std::strcmp(mode,"producers-attachment")) {
            *reinterpret_cast<std::uintptr_t*>(attachment.data())=image+0x3360370;
            *reinterpret_cast<int*>(attachment.data()+0xc)=9;
            RC::Unreal::FUObjectArray::items[9]={attachment.data(),51};
            *reinterpret_cast<Header*>(attachment.data()+0x1b8)={rows.data(),1,2};
            attachment_ready=true;
        }
        *reinterpret_cast<std::uint64_t*>(image+0x4197170)=901;
        *reinterpret_cast<int*>(particle.data()+0xc)=8;
        RC::Unreal::FUObjectArray::items[8]={particle.data(),41};
        *reinterpret_cast<std::uintptr_t*>(particle.data()+0xa70)=0x12340000;
        particle[0xa80]=std::byte{1};
        const auto call=entries[4]?entries[4]:reinterpret_cast<std::uint64_t>(&NativeDisable);
        const unsigned calls=!std::strcmp(mode,"producers-overflow")?65:1;
        if(!std::strncmp(mode,"producers-owners",16)) {
            auto setup=[](auto& task,auto& tick,void* object,std::uintptr_t table) {
                *reinterpret_cast<std::uintptr_t*>(task.data())=image+0x39cd9c0;
                *reinterpret_cast<void**>(task.data()+0x10)=tick.data();
                *reinterpret_cast<void**>(task.data()+0x28)=manager.data();
                *reinterpret_cast<std::uintptr_t*>(task.data()+0x40)=0x99880000;
                *reinterpret_cast<std::uintptr_t*>(tick.data())=image+table;
                *reinterpret_cast<void**>(tick.data()+0x18)=task.data();
                *reinterpret_cast<void**>(tick.data()+0x50)=object;
            };
            setup(owner_task,owner_tick,receiver.data(),0x381c720);
            setup(particle_task,particle_tick,particle.data(),0x3865f98);
            if(!std::strcmp(mode,"producers-owners-unreadable"))
                *reinterpret_cast<void**>(owner_task.data()+0x10)=reinterpret_cast<void*>(1);
            *reinterpret_cast<std::uintptr_t*>(disable_hub.data()+0x28)=image+0x337c4f8;
            *reinterpret_cast<int*>(disable_hub.data()+0xc)=10;
            RC::Unreal::FUObjectArray::items[10]={disable_hub.data(),61};
            *reinterpret_cast<int*>(disable_hub.data()+0x28+0x78+0x50)=1;
            *reinterpret_cast<int*>(disable_hub.data()+0x28+0x78+0x54)=1;
            if(!std::strncmp(mode,"producers-owners-hub",20) || !std::strcmp(mode,"producers-owners-custom-entry") || !std::strcmp(mode,"producers-owners-custom-resume")
                || !std::strncmp(mode,"producers-owners-registration-custom-",36)) {
                auto* hub_collection=disable_hub.data()+0xa0;
                Field<void*>(hub_collection,0x40)=hub_entries.data();
                Field<int>(hub_collection,0x50)=2;Field<int>(hub_collection,0x54)=18;
                Field<int>(hub_receiver_b.data(),0xc)=11;
                RC::Unreal::FUObjectArray::items[11]={hub_receiver_b.data(),71};
                for(unsigned i=0;i<2;++i) {
                    auto* callback=i?hub_callback_b.data():hub_callback_a.data();
                    Field<void*>(hub_entries.data()+i*0x40,0x20)=callback;
                    Field<int>(hub_entries.data()+i*0x40,0x30)=3;
                    Field<std::uintptr_t>(callback,0)=image+0x326bfe8;
                    Field<int>(callback,8)=i?11:7;Field<int>(callback,12)=i?71:11;
                    Field<std::uintptr_t>(callback,16)=image+0x3c5250;
                    Field<unsigned char>(callback,24)=1;
                    Field<std::uint64_t>(callback,32)=i?0x456:0x123;
                }
                if(!std::strncmp(mode,"producers-owners-hub-onearg",27)) {
                    // Native 14043D210 fields; +18/+28 are unknown bytes, not
                    // a bound argument or a receiver adjustment.
                    Field<std::uintptr_t>(hub_callback_b.data(),0)=image+0x374b760;
                    Field<std::uintptr_t>(hub_callback_b.data(),16)=image+0x4704f0;
                    Field<std::uint64_t>(hub_callback_b.data(),24)=0xfedcba9876543210;
                    Field<std::uint64_t>(hub_callback_b.data(),40)=0x0123456789abcdef;
                    constexpr std::uintptr_t slots[]{0x10acd10,0x549650,0x2fc0b0,0x549650,
                        0x301490,0x3f18b0,0x2f08bb0,0x3c3b60,0x3f0670,0x8955b0,
                        0x17dfea0,0x1e33070,0x3bbce0,0x3ef060};
                    for(unsigned j=0;j<14;++j)Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x374b760),j*8)=image+slots[j];
                    constexpr unsigned char invoke[]{72,137,92,36,8,72,137,116,36,16,87,72,131,236,32,72,139,217,72,139,242,72,131,193,8,232,114,51,185,0,72,133,192,116,40,72,141,75,8,232,100,51,185,0,76,139,67,16};
                    std::memcpy(reinterpret_cast<void*>(image+0x3ef060),invoke,48);
                    if(!std::strcmp(mode,"producers-owners-hub-onearg-table"))Field<std::uintptr_t>(reinterpret_cast<void*>(image+0x374b760),0x68)+=1;
                    if(!std::strcmp(mode,"producers-owners-hub-onearg-code"))*reinterpret_cast<unsigned char*>(image+0x3ef060+47)^=1;
                    if(!std::strcmp(mode,"producers-owners-hub-onearg-stale"))RC::Unreal::FUObjectArray::items[11].serial=72;
                    if(!std::strcmp(mode,"producers-owners-hub-onearg-storage"))Field<int>(hub_entries.data()+0x40,0x30)=2;
                    if(!std::strcmp(mode,"producers-owners-hub-onearg-inline")) {
                        Field<void*>(hub_entries.data()+0x40,0x20)=nullptr;
                        Field<std::uintptr_t>(hub_entries.data()+0x40,0)=image+0x374b760;
                    }
                }
                if(!std::strcmp(mode,"producers-owners-hub-unreadable"))Field<void*>(hub_entries.data()+0x40,0x20)=reinterpret_cast<void*>(1);
                if(!std::strcmp(mode,"producers-owners-hub-unknown"))Field<std::uintptr_t>(hub_callback_b.data(),0)=image+0x123;
                if(!std::strcmp(mode,"producers-owners-hub-stale"))RC::Unreal::FUObjectArray::items[11].serial=72;
                if(!std::strcmp(mode,"producers-owners-hub-truncated"))Field<int>(hub_collection,0x50)=3;
                if(!std::strcmp(mode,"producers-owners-hub-unknown-storage"))Field<int>(hub_entries.data()+0x40,0x30)=2;
            }
            RunTask(owner_task.data());
            if(!std::strncmp(mode,"producers-owners-hub",20))HubRemoveCall(disable_hub.data()+0xa0);
        } else
        for(unsigned i=0;i<calls;++i) {
            SetLastError(0x666);
            reinterpret_cast<void(*)(void*,void*,unsigned char)>(call)(receiver.data(),&payload,1);
            if(GetLastError()!=0x776)std::abort();
        }
        if(disables!=calls || completions!=calls+(!std::strcmp(mode,"producers-owners-threaded")?1:0) || tails!=calls)std::abort();
        attachment_ready=false;
        if(!std::strcmp(mode,"producers-attachment") && attachment_callbacks!=2)std::abort();
        Dispatch(); // No producer context may leak beyond nested native returns.
        ProductionCompletion();
        std::printf("producer forwarding PASS: disable=%u completion=%u reflected-tail=%u\n",disables,completions,tails);
        std::fflush(stdout);ExitProcess(0);
    }
    if(writers) {
        source_collection=*collection;copy_result=&payload;
        if(!std::strcmp(mode,"writers-filter")) {
            // Matching vtable alone is insufficient: wrong indexed object,
            // invalid index, zero generation and PendingKill are all omitted.
            alignas(8) auto impostor=manager;
            auto* other=reinterpret_cast<Header*>(impostor.data()+0x388);
            Prune(other);if(CopyCollection(other,&source_collection)!=copy_result)return 20;
            RC::Unreal::FUObjectArray::items[1].serial=0;
            Prune(collection);CopyCollection(collection,&source_collection);
            RC::Unreal::FUObjectArray::items[1].serial=31;
            *reinterpret_cast<unsigned*>(manager.data()+8)=0x10000;
            Prune(collection);CopyCollection(collection,&source_collection);
            *reinterpret_cast<unsigned*>(manager.data()+8)=0;
            *reinterpret_cast<int*>(manager.data()+0xc)=-1;
            Prune(collection);CopyCollection(collection,&source_collection);
            *reinterpret_cast<int*>(manager.data()+0xc)=1;
        } else if(!std::strcmp(mode,"writers-overlap")) {
            worker_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            worker_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            if(!worker_entered || !worker_release)return 21;
            std::thread overlapping([]{overlap_worker=GetCurrentThreadId();Dispatch();});
            if(WaitForSingleObject(worker_entered,5000)!=WAIT_OBJECT_0)std::abort();
            // The worker is parked after all collection reads: no fixture data race.
            Prune(collection);CopyCollection(collection,&source_collection);
            SetEvent(worker_release);overlapping.join();
            CloseHandle(worker_entered);CloseHandle(worker_release);
        } else if(!std::strcmp(mode,"writers-overflow")) {
            for(unsigned i=0;i<130;++i){Prune(collection);CopyCollection(collection,&source_collection);}
        } else if(!std::strcmp(mode,"writers-capacity-full")) {
            // Fixed acceptance workload, independent of production constants:
            // 6 initial records + 84 nested dispatch/prune/copy calls * 6 +
            // one copy pair = 512. Four rows stress serialization as well as
            // slots, with actual production phase closure and native forwarding.
            static std::array<Row,4> dense_rows{};
            for(auto& row:dense_rows)row={7,11,function_name};
            *collection={dense_rows.data(),4,4};source_collection=*collection;
            full_rows=true;helpers_in_dispatch=true;
            for(unsigned i=0;i<84;++i)Dispatch();
            helpers_in_dispatch=false;
            if(CopyCollection(collection,&source_collection)!=copy_result
                || dispatches!=86 || prunes!=84 || copies!=85
                || collection->count!=4 || collection->data!=dense_rows.data())return 24;
            for(const auto& row:dense_rows)if(row!=Row{7,11,function_name})return 25;
        } else {
            close_in_copy=!std::strcmp(mode,"writers-pending");
            expire_in_copy=!std::strcmp(mode,"writers-expired");
            helpers_in_dispatch=true;Dispatch();helpers_in_dispatch=false;
        }
        ProductionCompletion();
        std::uint64_t sealed[6]{},later[6]{};
        Horse::Deterministic::NativeReplayVfxCompletionObservation::ReadStatus(sealed,6);
        const auto prior_prunes=prunes,prior_copies=copies;
        // Closed phase still forwards both helpers, including when invalid.
        Prune(collection);if(CopyCollection(collection,&source_collection)!=copy_result)return 22;
        Horse::Deterministic::NativeReplayVfxCompletionObservation::ReadStatus(later,6);
        if(std::memcmp(sealed,later,sizeof(sealed)) || prunes!=prior_prunes+1 || copies!=prior_copies+1)return 23;
        std::printf("writer forwarding PASS: prunes=%u copies=%u records=%llu dropped=%llu\n",
            prunes,copies,sealed[0],sealed[1]);
        std::fflush(stdout);ExitProcess(0);
    }
    // Fixed workload, independent of the observer's capacity. Live status had
    // 32 records plus 45 dropped CALLS: 16+45 calls require 122 paired records.
    // Exercise that window, the proposed 512-record limit, and three complete
    // calls beyond it. All calls still reach the native boundary.
    unsigned extra_dispatches{};
    if(!std::strcmp(mode,"capacity-window"))extra_dispatches=58; // 6+116=122
    if(!std::strcmp(mode,"phase-close"))extra_dispatches=58;
    if(!std::strcmp(mode,"capacity-limit"))extra_dispatches=253; // 6+506=512
    if(!std::strcmp(mode,"overflow"))extra_dispatches=256;       // 6+512=518
    for(unsigned i=0;i<extra_dispatches;++i)Dispatch();
    if(extra_dispatches && (registrations!=1 || dispatches!=2+extra_dispatches
        || manager!=expected || rows[0]!=Row{7,12,function_name} || rows[1]!=Row{}
        || payload!=0xaabbccdd))return 12;
    if(!std::strcmp(mode,"expired-return")) {expire_on_return=true;Dispatch();}
    if(!std::strcmp(mode,"truncated")) {
        std::array<Row,6> many{};for(auto& row:many)row={7,12,function_name};
        collection->data=many.data();collection->count=collection->capacity=6;
        truncate_rows=true;Dispatch();
    }
    if(!std::strcmp(mode,"writefail")) {
        // Refuse replacement after the initial complete publication. Native
        // dispatch must still execute; status must expose sticky persistence
        // failure and the last complete main file must remain parseable.
        const auto held=CreateFileW(L"vfx_completion_observation.json",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(held==INVALID_HANDLE_VALUE)return 4;
        Dispatch();CloseHandle(held);
    }
    if(!std::strcmp(mode,"phase-close-overlap")) {
        worker_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        worker_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!worker_entered || !worker_release)return 14;
        std::thread overlapping([]{overlap_worker=GetCurrentThreadId();Dispatch();});
        if(WaitForSingleObject(worker_entered,5000)!=WAIT_OBJECT_0)std::abort();
        Dispatch();SetEvent(worker_release);overlapping.join();
        CloseHandle(worker_entered);CloseHandle(worker_release);
    }
    if(!std::strncmp(mode,"phase-close",11)) {
        HANDLE held=INVALID_HANDLE_VALUE;
        if(!std::strcmp(mode,"phase-close-writefail")) {
            held=CreateFileW(L"vfx_completion_observation.json",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            if(held==INVALID_HANDLE_VALUE)return 15;
        }
        ProductionCompletion();
        if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
        // Repeated completion cannot replace an unsuccessful close or change
        // the phase endpoint. Shutdown callbacks all execute after the cutoff.
        ProductionCompletion();
        for(unsigned i=0;i<8;++i)Dispatch();
    }
    std::uint64_t status[6]{};
    if(!Horse::Deterministic::NativeReplayVfxCompletionObservation::ReadStatus(status,6))return 5;
    std::printf("observer-status records=%llu dropped=%llu persistence_failed=%llu pending_pairs=%llu unstable=%llu truncated=%llu\n",
        status[0],status[1],status[2],status[3],status[4],status[5]);
    if(!std::strcmp(mode,"writefail") && (!status[2] || dispatches!=3))return 6;
    if(!std::strcmp(mode,"overflow") && (status[0]!=512 || status[1]!=3 || dispatches!=258))return 7;
    // Reading status must not consume sticky drop/persistence diagnostics.
    std::uint64_t again[6]{};
    if(!Horse::Deterministic::NativeReplayVfxCompletionObservation::ReadStatus(again,6)
        || std::memcmp(status,again,sizeof(status)))return 13;
    if(!std::strcmp(mode,"expired-return") && (!status[4] || dispatches!=3))return 8;
    if(!std::strcmp(mode,"truncated") && (status[5]!=2 || dispatches!=3))return 9;
    // Durable output must already exist. Bypass destructors/normal shutdown.
    std::fflush(stdout);ExitProcess(0);
}
