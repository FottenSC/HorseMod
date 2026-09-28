// Execute the production host admission methods against controlled owners.
#include "deterministic/ReplaySeekOwnership.hpp"
#include <atomic>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <Windows.h>
#include <array>
#include <cstring>
#include "native_gpu_completion_bytes.inl"
namespace Horse::Deterministic {
struct Sc6ReplayHost {
    inline static Sc6ReplayHost* active_{};
    struct Execution {
        ReplaySeekOwnership ownership{210,220};
        std::atomic<void*> protected_particle{};
        std::atomic<unsigned> protected_particle_routes{};
    };
    struct Restore {
        std::unique_ptr<Execution> execution=std::make_unique<Execution>();
        struct {struct {void* owner{};bool live=true;
            bool RetainsComponent(const void* p)const{return live && p && p==owner;}
        } vfx;} undo;
    };
    std::unique_ptr<Restore> historical_restore_=std::make_unique<Restore>();
    static bool ProtectParticleLifecycle(void*,void*,bool) noexcept;
    bool HistoricalParticleAbortPending()const noexcept;
    Sc6ReplayHost(){active_=this;}
};
#include "deterministic/Sc6ReplayHost.ParticleLifetime.inl"
}
using H=Horse::Deterministic::Sc6ReplayHost;
void check(bool value,int line){if(!value){std::printf("failed line %d\n",line);std::abort();}}
#define REQUIRE(x) check((x),__LINE__)
int main(){
    // Actual installed native query bytes, extracted by the runner. Verify its
    // return contract and absence of writes before relying on an early veto.
    {
        auto* code=VirtualAlloc(nullptr,sizeof(native_gpu_completion),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        REQUIRE(code);std::memcpy(code,native_gpu_completion,sizeof(native_gpu_completion));
        DWORD old{};REQUIRE(VirtualProtect(code,sizeof(native_gpu_completion),PAGE_EXECUTE_READ,&old));
        REQUIRE(FlushInstructionCache(GetCurrentProcess(),code,sizeof(native_gpu_completion)));
        auto query=reinterpret_cast<bool(*)(void*)>(code);
        for(int threshold: {0,1,2})for(int count:{0,1,2,3})for(int active:{0,1}) {
            std::array<std::byte,0x2a0> root{};
            std::memcpy(root.data()+0x290,&threshold,4);std::memcpy(root.data()+0x170,&count,4);std::memcpy(root.data()+0x118,&active,4);
            const auto before=root;
            REQUIRE(query(root.data())==(threshold!=0 && count>=threshold && active==0));
            REQUIRE(root==before);
        }
        REQUIRE(VirtualFree(code,0,MEM_RELEASE));
    }

    int a{},b{};
    H h;auto& tx=*h.historical_restore_;auto& e=*tx.execution;
    tx.undo.vfx.owner=&b;
    REQUIRE(!H::ProtectParticleLifecycle(nullptr,&b,true));
    REQUIRE(!H::ProtectParticleLifecycle(&h,&a,true));
    tx.undo.vfx.live=false;REQUIRE(!H::ProtectParticleLifecycle(&h,&b,true));tx.undo.vfx.live=true;
    REQUIRE(e.ownership.PublishA() && e.ownership.ActivateExecution());
    REQUIRE(H::ProtectParticleLifecycle(&h,&b,true));
    REQUIRE(e.protected_particle.load()==&b && e.protected_particle_routes.load()==1);
    REQUIRE(H::ProtectParticleLifecycle(&h,&b,false));
    REQUIRE(e.protected_particle_routes.load()==3 && h.HistoricalParticleAbortPending());
    REQUIRE(e.ownership.execution_active() && e.ownership.retains_undo());
    e.ownership.Fail();REQUIRE(!h.HistoricalParticleAbortPending());
    REQUIRE(e.ownership.BeginRecovery());REQUIRE(!h.HistoricalParticleAbortPending());
    REQUIRE(H::ProtectParticleLifecycle(&h,&b,true)); // Dispose C without retiring B.
    REQUIRE(e.ownership.CompleteRecoveryTails(218));
    REQUIRE(!h.HistoricalParticleAbortPending());
    REQUIRE(H::ProtectParticleLifecycle(&h,&b,true));
    REQUIRE(e.protected_particle_routes.load()==7); // Unexpected B-reconstruction veto is separately recorded.
    REQUIRE(e.ownership.CompleteRecovery(210));
    REQUIRE(!H::ProtectParticleLifecycle(&h,&b,true));
    tx.execution=std::make_unique<H::Execution>();auto& committed=tx.execution->ownership;
    REQUIRE(committed.PublishA() && committed.ActivateExecution() && committed.SettleC(220));
    REQUIRE(committed.CompleteTargetTails(220) && committed.BeginCommit());
    REQUIRE(!H::ProtectParticleLifecycle(&h,&b,false));
    REQUIRE(committed.CompleteCommit() && !h.HistoricalParticleAbortPending());
    tx.execution.reset();REQUIRE(!H::ProtectParticleLifecycle(&h,&b,true));
    h.historical_restore_.reset();REQUIRE(!h.HistoricalParticleAbortPending());
    std::puts("particle lifecycle selection and recovery ownership passed");
}
