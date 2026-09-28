// Compile actual permanent-owner startup, signatures, Call/Invoke and exports.
// External detour service only routes verified addresses to native probes.
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <tuple>
#include <thread>
#include <string>
#include "ReplayConsumerFailure.hpp"
static bool fail_writes{};
static bool fail_flush{},fail_reads{};
static BOOL ProbeReadProcessMemory(HANDLE process,LPCVOID source,LPVOID destination,SIZE_T bytes,SIZE_T* copied) {
    if(fail_reads){*copied=0;SetLastError(ERROR_PARTIAL_COPY);return FALSE;}
    return ReadProcessMemory(process,source,destination,bytes,copied);
}
static BOOL ProbeWriteFile(HANDLE f,LPCVOID b,DWORD n,LPDWORD written,LPOVERLAPPED overlap) {
    if(fail_writes){*written=0;SetLastError(ERROR_DISK_FULL);return FALSE;}
    return WriteFile(f,b,n,written,overlap);
}
static BOOL ProbeFlushFileBuffers(HANDLE f) {
    if(fail_flush){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    return FlushFileBuffers(f);
}
#if __has_include("ReplayPhysicsCallbackObservation.hpp")
#define ReadProcessMemory ProbeReadProcessMemory
#define WriteFile ProbeWriteFile
#define FlushFileBuffers ProbeFlushFileBuffers
#include "ReplayPhysicsCallbackObservation.hpp"
#undef ReadProcessMemory
#undef WriteFile
#undef FlushFileBuffers
#endif
using namespace Horse::Deterministic;
static std::uintptr_t image;
static std::uint64_t lifecycle_entry{},dispatch_entry{},register_entry{},remove_entry{};
static unsigned installations;
static const char* mode;
static std::atomic<unsigned> lifecycle_calls{},dispatch_calls{};
static void* scene;
static unsigned lifecycle_offset=0x4095fc0,physics_offset=0x80;
static HANDLE entered{},release_worker{};
static void NativeLifecycle(void*,void*,unsigned);
static void NativeDispatch(void*,void*,unsigned,float);
static void Complete();
static unsigned writer_calls{},nesting{};
static thread_local void* expected_writer_collection{};
static std::uint64_t* NativeRegister(void*,std::uint64_t*,void*,void*);
static void NativeRemove(void*,std::uint64_t);
static void Writer(bool remove=false,void* collection=nullptr) {
    if(!collection)collection=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(scene)+0x10);
    auto saved_collection=expected_writer_collection;expected_writer_collection=collection;
    SetLastError(0x663);
    if(remove) {
        auto f=remove_entry?remove_entry:reinterpret_cast<std::uint64_t>(&NativeRemove);
        reinterpret_cast<void(*)(void*,std::uint64_t)>(f)(collection,0xabcdef0123456789ull);
    } else {
        auto f=register_entry?register_entry:reinterpret_cast<std::uint64_t>(&NativeRegister);
        std::uint64_t handle{};
        auto result=reinterpret_cast<std::uint64_t*(*)(void*,std::uint64_t*,void*,void*)>(f)(collection,&handle,scene,reinterpret_cast<void*>(0x123456789));
        if(result!=&handle||handle!=0xabcdef0123456789ull)std::abort();
    }
    if(GetLastError()!=0x773)std::abort();
    expected_writer_collection=saved_collection;
}
static std::uint64_t* NativeRegister(void* collection,std::uint64_t* output,void* receiver,void* method) {
    if(collection!=expected_writer_collection||receiver!=scene||method!=reinterpret_cast<void*>(0x123456789)||GetLastError()!=0x663)std::abort();
    ++writer_calls;
    if(!std::strcmp(mode,"writer-nested") && nesting++==0)Writer(true);
    if(!std::strcmp(mode,"writer-depth") && ++nesting<140)Writer(true);
    if(!std::strcmp(mode,"writer-pending"))Complete();
    if(!std::strcmp(mode,"writer-retired")) {if(!VirtualFree(scene,0,MEM_RELEASE))std::abort();}
    if(!std::strcmp(mode,"writer-readfail"))fail_reads=true;
    *output=0xabcdef0123456789ull;SetLastError(0x773);return output;
}
static void NativeRemove(void* collection,std::uint64_t handle) {
    if(collection!=expected_writer_collection||handle!=0xabcdef0123456789ull||GetLastError()!=0x663)std::abort();
    ++writer_calls;
    if(!std::strcmp(mode,"writer-depth") && ++nesting<140)Writer(true);
    SetLastError(0x773);
}
static void Unrelated(){}
static std::uint64_t ProbeInstall(std::uint64_t target,std::uint64_t entry) {
    ++installations;
    if(target==image+0x29b4530){lifecycle_entry=entry;return reinterpret_cast<std::uint64_t>(&NativeLifecycle);}
    if(target==image+0x20113e0){dispatch_entry=entry;return reinterpret_cast<std::uint64_t>(&NativeDispatch);}
    if(target==image+0x21377c0){register_entry=entry;return reinterpret_cast<std::uint64_t>(&NativeRegister);}
    if(target==image+0x1f605a0){remove_entry=entry;return reinterpret_cast<std::uint64_t>(&NativeRemove);}
    return reinterpret_cast<std::uint64_t>(&Unrelated);
}
#include <polyhook2/Detour/x64Detour.hpp>
namespace Horse::Deterministic {
struct NativeReplayTraceTaskGuard {
#include "physics_install.inl"
#include "physics_startup.inl"
    template<class T>static T Read(std::uintptr_t p,std::size_t offset=0){return *reinterpret_cast<const T*>(p+offset);}
#include "physics_methods.inl"
    inline static std::atomic<NativeReplayTraceTaskGuard*> active_{};
    inline static HookStorage* hooks_{};
    inline static std::array<std::uint64_t,64> originals_{};
    inline static std::atomic<bool> installed_{};
    inline static std::uintptr_t installed_base_{};
    inline static SRWLOCK mutex_=SRWLOCK_INIT;
    inline static unsigned native_calls_{};
    inline static bool raw_service_retired_{};
    unsigned calls_{};bool held_{},executing_{};
    DWORD thread_{};std::uintptr_t named_thread_{},raw_service_{};
    struct Identity {std::uintptr_t object{};int index{},serial{};};
    struct {Identity component,manager,chara,scene,world;} binding_;
    struct {void* function{};} task_;
    struct {void* context{};bool(*before)(void*,unsigned,void*,void*,void*)noexcept{};
        bool(*after)(void*,unsigned,void*,void*,void*)noexcept{};} observer_;
};
}
#define HORSE_MOD_API
#include "physics_exports.inl"
template<class T>static T ResolveHorseModExport(const char* name) {
#include "physics_resolver.inl"
    return nullptr;
}
namespace RC {const std::string& to_generic_string(const std::string& s){return s;}}
enum class LogLevel {Default};
struct Output {template<LogLevel,class...T>static void send(T...) {}};
#define STR(x) x
static void Complete() {
    struct {std::string run_id{"physics-observation-fixture"};} request_;
    struct {unsigned frame{483};} trajectory_last_;
#include "physics_completion.inl"
}
static void Dispatch() {
    SetLastError(0x662);
    auto call=dispatch_entry?dispatch_entry:reinterpret_cast<std::uint64_t>(&NativeDispatch);
    reinterpret_cast<void(*)(void*,void*,unsigned,float)>(call)(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(scene)+physics_offset),scene,2,1.0f/60.0f);
    if(GetLastError()!=0x772)std::abort();
}
static void Lifecycle() {
    SetLastError(0x661);
    auto call=lifecycle_entry?lifecycle_entry:reinterpret_cast<std::uint64_t>(&NativeLifecycle);
    reinterpret_cast<void(*)(void*,void*,unsigned)>(call)(reinterpret_cast<void*>(image+lifecycle_offset),scene,2);
    if(GetLastError()!=0x771)std::abort();
}
static void NativeLifecycle(void* collection,void* p,unsigned type) {
    if(collection!=reinterpret_cast<void*>(image+lifecycle_offset)||p!=scene||type!=2||GetLastError()!=0x661)std::abort();
    ++lifecycle_calls;
    SetLastError(0x771);
}
static void NativeDispatch(void* collection,void* p,unsigned type,float delta) {
    unsigned bits{};std::memcpy(&bits,&delta,4);
    if(collection!=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(scene)+physics_offset)||p!=scene||type!=2
        ||bits!=0x3c888889||GetLastError()!=0x662)std::abort();
    ++dispatch_calls;
    if(!std::strcmp(mode,"nested"))Lifecycle();
    if(!std::strcmp(mode,"worker")){SetEvent(entered);WaitForSingleObject(release_worker,5000);}
    if(!std::strcmp(mode,"pending"))Complete();
    if(!std::strcmp(mode,"retired")){if(!VirtualFree(scene,0,MEM_RELEASE))std::abort();}
    if(!std::strcmp(mode,"crash")){std::puts("native forwarding PASS");std::fflush(stdout);ExitProcess(73);}
    if(!std::strcmp(mode,"writefail")) {
        // Controlled external Win32 failure after an actually flushed entry.
        fail_writes=true;
    }
    SetLastError(0x772);
}
int main(int argc,char** argv) {
    mode=argc>1?argv[1]:"normal";
    image=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    scene=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!image||!scene)return 1;
    struct Site {std::uintptr_t rva;const unsigned char* bytes;std::size_t count;};
#include "NativeReplayTraceTaskGuard.Signatures.inl"
    for(const auto& site:sites)std::memcpy(reinterpret_cast<void*>(image+site.rva),site.bytes,site.count);
    if(!std::strcmp(mode,"signature")) *reinterpret_cast<unsigned char*>(image+0x29b4530)^=1;
    if(!std::strcmp(mode,"signature-dispatch")) *reinterpret_cast<unsigned char*>(image+0x20113e0)^=1;
    if(!std::strcmp(mode,"signature-register")) *reinterpret_cast<unsigned char*>(image+0x21377c0)^=1;
    if(!std::strcmp(mode,"signature-remove")) *reinterpret_cast<unsigned char*>(image+0x1f605a0)^=1;
    bool installed=NativeReplayTraceTaskGuard::InstallStartup(image,true);
    if(!std::strncmp(mode,"signature",9)) {
        if(installed||installations)return 2;
        Lifecycle();Dispatch();std::puts("native forwarding PASS");return 0;
    }
    if(!installed)return 3;
    if(!std::strcmp(mode,"existing")) {
        auto f=CreateFileW(L"physics_callback_observation.jsonl",GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
        DWORD n;WriteFile(f,"prior evidence",14,&n,nullptr);CloseHandle(f);
    }
    auto start=ResolveHorseModExport<bool(*)(const wchar_t*,const char*)>("horsemod_start_physics_callback_observation");
    bool started=start&&start(L"physics_callback_observation.jsonl","physics-observation-fixture");
    if(!std::strcmp(mode,"existing")&&started)return 4;
    NativeReplayTraceTaskGuard guard;
    if(!std::strcmp(mode,"held-observer") || !std::strcmp(mode,"writer-held")) {
        guard.held_=true;guard.binding_.scene.object=reinterpret_cast<std::uintptr_t>(scene);
        guard.observer_.before=+[](void*,unsigned,void*,void*,void*)noexcept{std::abort();return false;};
        guard.observer_.after=guard.observer_.before;
        NativeReplayTraceTaskGuard::active_.store(&guard);
    }
    if(!std::strcmp(mode,"writer-filter"))Writer(); // Before scene discovery: coverage gap.
    Lifecycle();
    if(!std::strcmp(mode,"worker")) {
        entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_worker=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::thread worker(Dispatch);if(WaitForSingleObject(entered,5000)!=WAIT_OBJECT_0)std::abort();
        Lifecycle();SetEvent(release_worker);worker.join();CloseHandle(entered);CloseHandle(release_worker);
    } else Dispatch();
    if(!std::strcmp(mode,"overflow"))for(unsigned i=0;i<ReplayPhysicsCallbackObservation::MaxEvents/2+1;++i)Lifecycle();
    // 4975 calls reached the failed selected phase (4096 recorded + 879
    // dropped). Exercise 5000 total calls through the actual hook owner.
    if(!std::strcmp(mode,"selected-phase"))for(unsigned i=0;i<4998;++i) {
        physics_offset=(i%2)?0x80:0x10;Dispatch();
    }
    if(!std::strcmp(mode,"routes")) {
        lifecycle_offset=0x4095f50;Lifecycle();lifecycle_offset=0x12345;Lifecycle();
        physics_offset=0x10;Dispatch();physics_offset=0x234;Dispatch();
    }
    if(!std::strncmp(mode,"writer-",7)) {
        if(!std::strcmp(mode,"writer-scenes")) {
            auto saved=scene;
            for(unsigned i=1;i<=32;++i){scene=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(saved)+i*0x1000);Lifecycle();}
            scene=saved;
        }
        // Arbitrary caller, outside lifecycle/dispatch: bypass must be visible.
        writer_calls=0;nesting=0;
        if(!std::strcmp(mode,"writer-filter"))for(unsigned i=0;i<20000;++i)Writer(false,reinterpret_cast<void*>(0x424242));
        if(!std::strcmp(mode,"writer-overflow"))for(unsigned i=0;i<8200;++i)Writer(true);
        else {if(!std::strcmp(mode,"writer-worker")){std::thread writer([]{Writer();});writer.join();}
            else if(!std::strcmp(mode,"writer-step"))Writer(false,reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(scene)+0x80));
            else Writer();if(std::strcmp(mode,"writer-retired") && std::strcmp(mode,"writer-depth"))Writer(true);}
    }
    if(!std::strcmp(mode,"flush-close"))fail_flush=true;
    Complete();
    // Later shutdown still forwards but is outside the sealed phase.
    Lifecycle();
    if(!std::strncmp(mode,"writer-",7) && std::strcmp(mode,"writer-retired"))Writer(true);
    if(guard.calls_ || guard.native_calls_)return 5;
    auto read=ResolveHorseModExport<bool(*)(std::uint64_t*,std::size_t)>("horsemod_read_physics_callback_observation_status");
    std::uint64_t status[4]{};if(read)read(status,4);
    std::printf("native forwarding PASS lifecycle=%u dispatch=%u started=%u records=%llu dropped=%llu persistence_failed=%llu pending=%llu\n",
        lifecycle_calls.load(),dispatch_calls.load(),started,status[0],status[1],status[2],status[3]);
    std::printf("writer forwarding PASS calls=%u\n",writer_calls);
    return 0;
}
