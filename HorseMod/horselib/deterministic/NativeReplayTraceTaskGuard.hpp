#pragma once
#include "Sc6ReplayTaskGroup.hpp"
#include "ReplayConsumerFailure.hpp"
#include "ReplayPhysicsCallbackObservation.hpp"
#include <Windows.h>
#include <intrin.h>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <tuple>

namespace Horse::Deterministic {
// Single-family storage protection, not native cancellation. A conflicting
// retirement/callback terminates before native entry; it never returns success
// instead of a required callback. Strict host/execution-domain exclusion is an
// additional admission condition, not something these sentinels establish.
class NativeReplayTraceTaskGuard final {
public:
    struct Identity {
        std::uintptr_t object{};
        std::int32_t index{}, serial{};
        friend bool operator==(const Identity&,const Identity&)=default;
    };
    struct Binding {
        Identity component, manager, chara, scene, world;
        friend bool operator==(const Binding&,const Binding&)=default;
    };
    // Internal, execution-owned enforcement at these already installed native
    // entries. Call ownership pins the context through the native return.
    struct ExecutionObserver {
        void* context{};
        bool (*before)(void*,unsigned,void*,void*,void*) noexcept{};
        bool (*after)(void*,unsigned,void*,void*,void*) noexcept{};
    };
    NativeReplayTraceTaskGuard() = default;
    NativeReplayTraceTaskGuard(const NativeReplayTraceTaskGuard&) = delete;
    NativeReplayTraceTaskGuard& operator=(const NativeReplayTraceTaskGuard&) = delete;
    // Called only from the verified initial proxy-loader C++ constructor.
    // Code and trampolines remain process-owned; activation never patches code.
    static bool InstallStartup(std::uintptr_t base,bool initial_constructor) {
        if(!initial_constructor || !base || hooks_ || !StartupDomain(base))return false;
        struct Site {std::uintptr_t rva;const unsigned char* bytes;std::size_t count;};
#include "NativeReplayTraceTaskGuard.Signatures.inl"
        for (const auto& site:sites)
            if (std::memcmp(reinterpret_cast<void*>(base+site.rva),site.bytes,site.count)) return false;
        const std::array<std::uint64_t,29> entries{
            Address<&Invoke<0,void,void*>>(),
            Address<&Invoke<1,void*,void*,unsigned>>(),
            Address<&Invoke<2,void,void*>>(),
            Address<&Invoke<3,void,void*,unsigned char,unsigned char,void*>>(),
            Address<&Invoke<4,void,void*>>(),
            Address<&Invoke<5,void,void*,void*>>(),
            Address<&Invoke<6,bool,void*,void*,bool,bool>>(),
            Address<&Invoke<7,void,void*>>(),
            Address<&Invoke<8,void,void*>>(),
            Address<&Invoke<9,void,void*,bool>>(),
            Address<&Invoke<10,void,void*>>(),
            Address<&Invoke<11,void,void*,void*,void*>>(),
            Address<&Invoke<12,void,void*,void*,void*>>(),
            Address<&Invoke<13,void,void*>>(),
            Address<&Invoke<14,void,void*,std::uint64_t>>(),
            Address<&Invoke<15,void,void*>>(),
            Address<&Invoke<16,void,void*>>(),
            Address<&Invoke<17,void,void*>>(),
            Address<&Invoke<18,void,void*>>(),
            Address<&Invoke<19,void,void*,bool>>(),
            Address<&Invoke<20,void,void*>>(),
            Address<&Invoke<21,void,void*,unsigned>>(),
            Address<&Invoke<22,void,void*,void*>>(),
            Address<&Invoke<23,void,void*>>(),
            Address<&Invoke<24,void*,void*>>(),
            Address<&ObservePhysicsLifecycle>(),
            Address<&ObservePhysicsDispatch>(),
            Address<&ObservePhysicsRegister>(),
            Address<&ObservePhysicsRemove>()};
        HMODULE module{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Invoke<0,void,void*>),&module))return false;
        // Pin before publishing any entry. Partial installation also remains
        // process-owned and forwards through its valid native trampoline.
        hooks_=new HookStorage{};
        installed_base_=base;
        for(std::size_t i=0;i<sites.size();++i) {
            hooks_->entries[i]=std::make_unique<PLH::x64Detour>(base+sites[i].rva,entries[i],&originals_[i]);
            if(!hooks_->entries[i]->hook())return false;
        }
        installed_.store(true,std::memory_order_release);
        return true;
    }
    static bool StartPhysicsObservation(const wchar_t* path,const char* run) noexcept {
        return installed_.load(std::memory_order_acquire)
            && ReplayPhysicsCallbackObservation::Start(installed_base_,path,run);
    }
    bool Bind(std::uintptr_t base,std::uintptr_t world) {
        if(!installed_.load(std::memory_order_acquire) || installed_base_!=base || !world)return false;
        base_=base;thread_=GetCurrentThreadId();
        if(!ExecutionDomain(world) || !BindNamedThread())return false;
        AcquireSRWLockExclusive(&mutex_);
        // Count every native entry, even while no probe is active. Publishing
        // the owner and checking older entry completion share the same lock.
        const bool idle=!active_.load() && native_calls_==0;
        if(idle){observer_={};active_.store(this,std::memory_order_release);}
        ReleaseSRWLockExclusive(&mutex_);
        return idle;
    }
    bool BindExecution(std::uintptr_t base,ExecutionObserver observer) {
        if(!installed_.load(std::memory_order_acquire) || installed_base_!=base
            || !observer.context || !observer.before || !observer.after)return false;
        base_=base;thread_=GetCurrentThreadId();
        if(!BindNamedThread())return false;
        AcquireSRWLockExclusive(&mutex_);
        const bool idle=!active_.load() && native_calls_==0;
        if(idle){observer_=observer;active_.store(this,std::memory_order_release);}
        ReleaseSRWLockExclusive(&mutex_);
        return idle;
    }
    std::uintptr_t named_thread() const noexcept {return named_thread_;}
    bool Stop() {
        AcquireSRWLockExclusive(&mutex_);
        const bool ours=active_.load()==this;
        const bool idle=!ours || (GetCurrentThreadId()==thread_ && !held_ && !raw_service_ && calls_==0);
        if(ours && idle){active_.store(nullptr,std::memory_order_release);observer_={};}
        ReleaseSRWLockExclusive(&mutex_);
        return idle;
    }
    ~NativeReplayTraceTaskGuard(){if(!Stop())__fastfail(FAST_FAIL_INVALID_ARG);}
    static std::size_t ProcessOwnedBytes() noexcept {
        return ReplayPhysicsCallbackObservation::OwnedBytes
            +(hooks_?sizeof(HookStorage)+sizeof(originals_)+29*(sizeof(PLH::x64Detour)+65536):0);
    }
    std::size_t owned_bytes() const noexcept {return sizeof(*this);}
    // Values come from retained indexed identities, never from dereferencing a
    // possibly retired raw owner to rediscover its index.
    bool ReadContract(const Binding& binding,std::uint64_t& contract) noexcept {
        AcquireSRWLockExclusive(&mutex_);
        const bool acquired=active_.load()==this && GetCurrentThreadId()==thread_
            && !held_ && calls_==0 && AcquireRawService();
        const bool valid=acquired && InspectContract(binding,contract);
        if(acquired)ReleaseRawService();
        ReleaseSRWLockExclusive(&mutex_);
        return valid;
    }
    unsigned admission_failure() const noexcept {return admission_failure_;}
    unsigned domain_failure() const noexcept {return domain_failure_;}
    bool Acquire(const Binding& binding,std::uint64_t expected,
        Sc6ReplayTaskGroup::ConsumerTask& task) noexcept {
        AcquireSRWLockExclusive(&mutex_);
        admission_failure_=0;
        if(active_.load()!=this || GetCurrentThreadId()!=thread_)admission_failure_=1;
        else if(held_ || calls_)admission_failure_=2;
        else if(!AcquireRawService())admission_failure_=5;
        else if(!InspectTask(binding,task))admission_failure_=3;
        else if(!ExecutionDomain(binding.world.object,true))admission_failure_=4;
        const bool valid=admission_failure_==0;
        if(valid) {
            binding_=binding;expected_contract_=expected;task_=task;
            task_.owner=binding.component.object;
            task_.owner_index=binding.component.index;
            task_.owner_generation=binding.component.serial;
            task_.application_epoch=Read<std::uint64_t>(base_+0x4197170);
            task_.contract=expected;
            std::memcpy(tick_.data(),task_.function,tick_.size());
            held_=true;executing_=false;task=task_;
        }
        else if(!held_)ReleaseRawService();
        ReleaseSRWLockExclusive(&mutex_);
        return valid;
    }
    bool Validate(const Sc6ReplayTaskGroup::ConsumerTask& task) const noexcept {
        AcquireSRWLockExclusive(&mutex_);
        std::uint64_t contract{};
        const bool valid=held_ && raw_service_ && !executing_ && calls_==0 && GetCurrentThreadId()==thread_
            && task.task==task_.task && task.function==task_.function
            && task.owner==task_.owner && task.owner_index==task_.owner_index
            && task.owner_generation==task_.owner_generation
            && task.world==task_.world && task.completion==task_.completion
            && task.operating_thread==thread_ && task.native_thread==task_.native_thread
            && task.application_epoch==task_.application_epoch
            && task.contract==expected_contract_
            && InspectContract(binding_,contract) && contract==expected_contract_
            && InspectRetainedTask(task) && ExecutionDomain(binding_.world.object,true);
        ReleaseSRWLockExclusive(&mutex_);
        return valid;
    }
    // Called exactly once after the dispatcher's final retained-payload checks,
    // immediately before entering the original native task wrapper. Native
    // execution now owns ordinary callback/lifecycle ordering on its original
    // thread. Other threads must not retire its selected roots between this
    // handoff and native completion, including the first owner dereference.
    bool BeginExecution(const Sc6ReplayTaskGroup::ConsumerTask& task) noexcept {
        if(!Validate(task))return false;
        AcquireSRWLockExclusive(&mutex_);
        std::uint64_t contract{};
        const bool valid=held_ && !executing_ && calls_==0
            && GetCurrentThreadId()==thread_ && task.task==task_.task
            && InspectContract(binding_,contract) && contract==expected_contract_
            && InspectRetainedTask(task) && ExecutionDomain(binding_.world.object,true);
        if(valid) {
            executing_=true;
            // Normal native execution may need this service. Release before
            // the original wrapper, never pump or execute under its lease.
            ReleaseRawService();
        }
        ReleaseSRWLockExclusive(&mutex_);
        return valid;
    }
    void Completed(const Sc6ReplayTaskGroup::ConsumerTask& task) noexcept {
        AcquireSRWLockExclusive(&mutex_);
        // The task allocation has already returned to native TLS storage. Only
        // compare the saved identity; never read through it here.
        if(!held_ || !executing_ || GetCurrentThreadId()!=thread_ || task.task!=task_.task)
            __fastfail(FAST_FAIL_INVALID_ARG);
        // Ordinary independent CRI calls may be active now. They only retain
        // this guard's call count, not the completed task's recycled storage.
        held_=false;executing_=false;binding_={};task_={};tick_={};expected_contract_=0;
        ReleaseSRWLockExclusive(&mutex_);
    }
private:
    // Called with mutex_ held. The service callback takes its own lock before
    // arbitrary delegates, so use TryEnter only: waiting here could invert
    // callback -> sentinel ordering. Destruction shares mutex_ and is vetoed
    // from lock acquisition through release, even before held_ is published.
    bool AcquireRawService() noexcept {
        if(raw_service_ || raw_service_retired_ || native_calls_)return false;
        __try {
            const auto initialized=Read<int>(base_+0x43fd2fc);
            if(initialized==0 || initialized==-1)return false;
            const auto service=Read<std::uintptr_t>(base_+0x40e3a28);
            if(!service || !TryEnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(service+0x60)))return false;
            raw_service_=service;
            const auto count=Read<int>(service,0x58),capacity=Read<int>(service,0x5c);
            if(count==0 && capacity>=0 && capacity<=4096
                && (!capacity || Read<std::uintptr_t>(service,0x50)))return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        ReleaseRawService();return false;
    }
    void ReleaseRawService() noexcept {
        if(!raw_service_)return;
        LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(raw_service_+0x60));
        raw_service_=0;
    }
    struct HookStorage {std::array<std::unique_ptr<PLH::x64Detour>,29> entries;};
    static bool StartupDomain(std::uintptr_t base) noexcept {
        __try {
            // Native graph/pools/CRI publish these pointers before creating
            // their workers. Zero is meaningful only in the constructor phase.
            for(const auto rva:{0x4166720u,0x4168038u,0x4168040u,0x4148b90u,0x44379e0u})
                if(Read<std::uintptr_t>(base+rva))return false;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    template<auto Function> static std::uint64_t Address(){return reinterpret_cast<std::uint64_t>(Function);}
    template<class T> static T Read(std::uintptr_t p,std::size_t n=0) {
        return *reinterpret_cast<const T*>(p+n);
    }
    static bool Live(const Identity& id) {
        if(!id.object || id.index<0 || id.serial<=0)return false;
        auto* item=RC::Unreal::FUObjectArray::IndexToObject(id.index);
        return item && reinterpret_cast<std::uintptr_t>(item->GetUObject())==id.object
            && item->IsValid(false) && item->GetSerialNumber()==id.serial
            && !(Read<unsigned>(id.object,8)&0x18000u);
    }
    bool InspectContract(const Binding& b,std::uint64_t& hash) const noexcept {
        __try {
            if(!Live(b.component)||!Live(b.manager)||!Live(b.chara)||!Live(b.scene)||!Live(b.world))return false;
            const auto owner=b.component.object;
            if(Read<std::uintptr_t>(owner)!=base_+0x3360ca8
                || Read<std::uintptr_t>(b.manager.object)!=base_+0x3361f98
                // Exact live families from the retained native task census;
                // derived classes may retire contents before these sentinels.
                || Read<std::uintptr_t>(b.chara.object)!=base_+0x3268078
                || Read<std::uintptr_t>(b.scene.object)!=base_+0x3360370
                || Read<std::uintptr_t>(owner,0x190)!=b.manager.object
                || Read<std::uintptr_t>(owner,0x1c8)!=b.world.object
                || Read<std::uintptr_t>(owner,0x490)!=b.chara.object
                || Read<std::uintptr_t>(b.manager.object,0x3a8)!=owner
                || Read<std::uintptr_t>(b.chara.object,0x458)!=b.manager.object
                || Read<std::uintptr_t>(b.chara.object,0x168)!=b.scene.object
                || Read<std::uintptr_t>(b.scene.object,0x190)!=b.chara.object
                || Read<std::uintptr_t>(owner,0x460)!=0)return false;
            hash=14695981039346656037ull;
            // Process-local contract only: these addresses never enter portable
            // logical identities, simulation hashes or observer wire records.
            for(const auto& id:{b.component,b.manager,b.chara,b.scene,b.world}) {
                hash^=id.object;hash*=1099511628211ull;
                hash^=static_cast<unsigned>(id.index);hash*=1099511628211ull;
                hash^=static_cast<unsigned>(id.serial);hash*=1099511628211ull;
            }
            for(const auto value:{Read<std::uintptr_t>(owner,0x110),
                Read<std::uintptr_t>(owner,0x160),Read<std::uintptr_t>(b.world.object,0x1c8),
                Read<std::uintptr_t>(b.world.object,0x778)}) {
                hash^=value;hash*=1099511628211ull;
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool InspectTask(const Binding& b,const Sc6ReplayTaskGroup::ConsumerTask& task) const noexcept {
        __try {
            if(!Live(b.component)||!Live(b.world)||!Live(b.manager))return false;
            const auto tick=b.component.object+0x110;
            return Read<std::uintptr_t>(b.component.object)==base_+0x3360ca8
                && Read<std::uintptr_t>(b.manager.object)==base_+0x3361f98
                && task.task && reinterpret_cast<std::uintptr_t>(task.function)==tick
                && reinterpret_cast<std::uintptr_t>(task.world)==b.world.object
                && task.native_thread==2 && task.operating_thread==thread_
                && Read<std::uintptr_t>(tick)==base_+0x3865f98
                && Read<std::uintptr_t>(tick,0x50)==b.component.object
                && Read<void*>(tick,0x18)==task.task
                && Read<unsigned char>(tick,0xa)==5
                && !(Read<unsigned char>(tick,0xc)&0x20)
                && Read<int>(tick,0x28)==0 && task.completion
                && Read<unsigned char>(reinterpret_cast<std::uintptr_t>(task.task),0x38)!=0
                && !(InterlockedCompareExchange64(
                    reinterpret_cast<volatile LONG64*>(reinterpret_cast<std::uintptr_t>(task.completion)+8),
                    0,0)&(1ll<<26));
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool InspectRetainedTask(const Sc6ReplayTaskGroup::ConsumerTask& task) const noexcept {
        __try {
            return Read<std::uint64_t>(base_+0x4197170)==task.application_epoch
                && InspectTask(binding_,task)
                && std::memcmp(task.function,tick_.data(),tick_.size())==0;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool ExecutionDomain(std::uintptr_t world,bool component_boundary=false) const noexcept {
        domain_failure_=0;
        __try {
            // Read initialized caches; never initialize native singletons or
            // turn a command-line request into an assumed runtime guarantee.
            for(const auto offset:{0x4166dd8u,0x418acb8u,0x429f400u,0x429f250u,0x43b3020u,0x43b2fc0u}) {
                const auto epoch=Read<int>(base_+offset);
                if(epoch==0 || epoch==-1)return DomainFailure(1);
            }
            if(Read<unsigned char>(base_+0x418acb4)!=1
                || Read<unsigned char>(base_+0x429f3fc)
                || Read<unsigned char>(base_+0x429eac0)
                || Read<std::uintptr_t>(base_+0x429ef78))return DomainFailure(2);
            for(const auto offset:{0x4351840u,0x4351841u,0x434459cu,0x434459du,
                0x434459eu,0x434459fu,0x43445a0u})
                if(Read<unsigned char>(base_+offset))return DomainFailure(3);
            for(const auto offset:{0x4167818u,0x4351848u,0x4168028u})
                if(Read<std::uintptr_t>(base_+offset))return DomainFailure(4);
            if(Read<unsigned>(base_+0x4168024))return DomainFailure(5);
            // ONETHREAD serializes standard tick dispatch while preserving
            // the raw-loading pool. Read initialized static objects directly;
            // invoking the lazy getter here would manufacture the witness.
            const auto manager=base_+0x43b2fd0,sequencer=base_+0x43b2608;
            if(Read<std::uintptr_t>(manager)!=base_+0x39cdb10
                || Read<std::uintptr_t>(manager,8)!=sequencer)return DomainFailure(6);
            if(component_boundary && (Read<int>(manager,0x28)!=5
                || Read<unsigned>(manager,0x2c)!=2 || Read<std::uintptr_t>(manager,0x30)!=world
                || Read<unsigned char>(manager,0x38)!=1
                || Read<unsigned char>(sequencer,0x9b4)))return DomainFailure(7);
            // Group5 is reached only after the owned native group3/4 waits,
            // including EndPhysics' chained publication child. Null aggregate
            // alone is insufficient: finish clears it before writing transforms.
            const auto physics=Read<std::uintptr_t>(world,0x1c8);
            if(!physics || Read<std::uintptr_t>(physics,0x160)
                || Read<unsigned char>(physics,0xfe) || Read<std::uintptr_t>(physics,0x130)
                || Read<unsigned char>(physics,0x198))return DomainFailure(8);
            const auto async=Read<unsigned char>(physics);
            if(async>1 || Read<int>(physics,4)!=(async?3:2))return DomainFailure(9);
            // Disabled async scene storage is not initialized by native code.
            if(async && (Read<unsigned char>(physics,0x100)
                || Read<std::uintptr_t>(physics,0x140) || Read<std::uintptr_t>(physics,0x158)
                || Read<unsigned char>(physics,0x228)))return DomainFailure(10);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return DomainFailure(11);}
    }
    bool DomainFailure(unsigned code) const noexcept {domain_failure_=code;return false;}

    bool BindNamedThread() noexcept {
        __try {
            if(!Read<unsigned char>(base_+0x419718c) || Read<unsigned>(base_+0x419716c)!=thread_)return false;
            const auto graph=Read<std::uintptr_t>(base_+0x4166720);
            if(!graph)return false;
            const auto named=Read<std::uintptr_t>(graph,8+2*0x18);
            if(!named || Read<unsigned>(named,0x10)!=2 || Read<int>(named,0x3e0))return false;
            named_thread_=named;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }

    bool Matches(unsigned site,void* primary,void* secondary) const noexcept {
        if(site==22)return raw_service_ && reinterpret_cast<std::uintptr_t>(secondary)==raw_service_;
        if(!held_ || (executing_ && GetCurrentThreadId()==thread_))return false;
        // Global callback/pump veto is only for the untouched hold. During
        // execution, unrelated worker activity forwards normally; selected
        // root retirement and task mutation retain their before-entry veto.
        if(site==13 || site==14 || site==16)return !executing_;
        const auto p=reinterpret_cast<std::uintptr_t>(primary);
        const auto q=reinterpret_cast<std::uintptr_t>(secondary);
        if(site==24)return named_thread_ && (p==named_thread_+0x38 || p==named_thread_+0x38+0x190);
        if(site==6)return p==binding_.world.object || q==binding_.manager.object || q==binding_.chara.object;
        if(site==11 || site==12 || site==15)return primary==task_.function;
        return p==binding_.component.object || p==binding_.manager.object
            || p==binding_.chara.object || p==binding_.scene.object || p==binding_.world.object;
    }
    struct Call {
        NativeReplayTraceTaskGuard* owner{};
        CRITICAL_SECTION* cri_table_lock{};
        unsigned site_{};void* first_{};void* second_{};void* third_{};
        void Failure(ReplayConsumerFailure::Site site)const noexcept {
            ReplayConsumerFailure::Record r{};r.site=site;r.native_site=site_;
            r.native_started=site==ReplayConsumerFailure::Site::NativeAfter;
            r.task=reinterpret_cast<std::uintptr_t>(first_);r.function=reinterpret_cast<std::uintptr_t>(second_);
            r.operands[0]=reinterpret_cast<std::uintptr_t>(third_);r.operands[1]=native_calls_;
            if(owner){r.original_thread=owner->thread_;r.owner=owner->binding_.component.object;
                r.owner_index=owner->binding_.component.index;r.owner_generation=owner->binding_.component.serial;
                r.operands[2]=owner->held_;r.operands[3]=owner->executing_;r.operands[4]=owner->calls_;}
            ReplayConsumerFailure::Write(r);
        }
        static bool EmptyCriTable(void* manager) noexcept {
            __try {
                const auto p=reinterpret_cast<std::uintptr_t>(manager);
                const auto slots=Read<int>(p,0x108),free=Read<int>(p,0x134);
                // Exact native140544470 early-return condition, with bounded
                // valid counts. No voice lookup or delegate runs on this path.
                return slots>=0 && slots<=128 && free>=0 && free==slots;
            } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
        }
        Call(unsigned site,void* first,void* second,void* third=nullptr)
            : site_(site),first_(first),second_(second),third_(third) {
            AcquireSRWLockExclusive(&mutex_);
            ++native_calls_;
            // Native destruction leaves the published pointer dangling. Record
            // retirement even when no probe is active, before DeleteCriticalSection.
            if(site==22 && second)raw_service_retired_=true;
            owner=active_.load(std::memory_order_acquire);
            if(owner)++owner->calls_;
            if(owner && site==14 && owner->held_ && !owner->executing_) {
                // Owner remains referenced by calls_ while the native recursive
                // table lock is acquired outside the guard's mutex.
                ReleaseSRWLockExclusive(&mutex_);
                if(!first)__fastfail(FAST_FAIL_INVALID_ARG);
                cri_table_lock=reinterpret_cast<CRITICAL_SECTION*>(
                    reinterpret_cast<std::uintptr_t>(first)+0xd0);
                EnterCriticalSection(cri_table_lock);
                AcquireSRWLockExclusive(&mutex_);
            }
            if(owner && owner->Matches(site,first,second)
                && !(site==14 && cri_table_lock && EmptyCriTable(first))) {
                Failure(ReplayConsumerFailure::Site::NativeHeld);
                __fastfail(FAST_FAIL_INVALID_ARG);
            }
            if(owner && owner->observer_.before
                && !owner->observer_.before(owner->observer_.context,site,first,second,third)) {
                Failure(ReplayConsumerFailure::Site::NativeBefore);
                __fastfail(FAST_FAIL_INVALID_ARG);
            }
            ReleaseSRWLockExclusive(&mutex_);
        }
        ~Call(){
            AcquireSRWLockExclusive(&mutex_);
            if(owner && owner->observer_.after
                && !owner->observer_.after(owner->observer_.context,site_,first_,second_,third_)) {
                Failure(ReplayConsumerFailure::Site::NativeAfter);
                __fastfail(FAST_FAIL_INVALID_ARG);
            }
            if(owner)--owner->calls_;
            --native_calls_;
            ReleaseSRWLockExclusive(&mutex_);
            if(cri_table_lock)LeaveCriticalSection(cri_table_lock);
        }
    };
    template<std::size_t I,class R,class... A> static R Invoke(A... args) {
        auto tuple=std::tuple<A...>(args...);
        void* secondary{};void* third{};
        if constexpr(I==6 || I==11 || I==12 || I==22)secondary=std::get<1>(tuple);
        if constexpr(I==11 || I==12)third=std::get<2>(tuple);
        Call call(static_cast<unsigned>(I),std::get<0>(tuple),secondary,third);
        return reinterpret_cast<R(*)(A...)>(originals_[I])(args...);
    }
    // Same permanent hook/trampoline owner, deliberately outside Call: these
    // new entries are observation only and must not expand existing admission,
    // held-owner vetoes, raw-service exclusion or retirement semantics.
    // Win64 RCX collection/RDX scene/R8D type; dispatch additionally XMM3 delta.
    __declspec(noinline) static void ObservePhysicsLifecycle(void* collection,void* scene,unsigned type) {
        const auto error=GetLastError();
        const auto token=ReplayPhysicsCallbackObservation::Before(0,collection,scene,type,0,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*,void*,unsigned)>(originals_[25])(collection,scene,type);
        const auto returned_error=GetLastError();ReplayPhysicsCallbackObservation::After(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void ObservePhysicsDispatch(void* collection,void* scene,unsigned type,float delta) {
        const auto error=GetLastError();unsigned bits{};std::memcpy(&bits,&delta,sizeof(bits));
        const auto token=ReplayPhysicsCallbackObservation::Before(1,collection,scene,type,bits,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*,void*,unsigned,float)>(originals_[26])(collection,scene,type,delta);
        const auto returned_error=GetLastError();ReplayPhysicsCallbackObservation::After(token);SetLastError(returned_error);
    }
    // Verified Win64 writer ABIs. No Call/admission or receiver lookup.
    __declspec(noinline) static std::uint64_t* ObservePhysicsRegister(void* collection,std::uint64_t* output,void* receiver,void* method) {
        const auto error=GetLastError();
        const auto token=ReplayPhysicsCallbackObservation::BeforeWriter(2,collection,
            reinterpret_cast<std::uintptr_t>(output),receiver,method,_ReturnAddress());
        SetLastError(error);
        auto result=reinterpret_cast<std::uint64_t*(*)(void*,std::uint64_t*,void*,void*)>(originals_[27])(collection,output,receiver,method);
        const auto returned_error=GetLastError();ReplayPhysicsCallbackObservation::After(token,result);SetLastError(returned_error);
        return result;
    }
    __declspec(noinline) static void ObservePhysicsRemove(void* collection,std::uint64_t handle) {
        const auto error=GetLastError();
        const auto token=ReplayPhysicsCallbackObservation::BeforeWriter(3,collection,handle,nullptr,nullptr,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*,std::uint64_t)>(originals_[28])(collection,handle);
        const auto returned_error=GetLastError();ReplayPhysicsCallbackObservation::After(token);SetLastError(returned_error);
    }
    inline static std::atomic<NativeReplayTraceTaskGuard*> active_{};
    inline static HookStorage* hooks_{}; // Pinned until process exit; never unhook under workers.
    inline static std::array<std::uint64_t,29> originals_{};
    inline static std::atomic<bool> installed_{};
    inline static std::uintptr_t installed_base_{};
    inline static unsigned native_calls_{};
    inline static bool raw_service_retired_{};
    inline static SRWLOCK mutex_=SRWLOCK_INIT;
    std::uintptr_t named_thread_{};
    ExecutionObserver observer_{};
    std::uintptr_t raw_service_{};
    std::uintptr_t base_{};
    DWORD thread_{};
    unsigned calls_{};
    unsigned admission_failure_{};
    mutable unsigned domain_failure_{};
    bool held_{};
    bool executing_{};
    Binding binding_{};
    Sc6ReplayTaskGroup::ConsumerTask task_{};
    std::array<std::byte,0x58> tick_{};
    std::uint64_t expected_contract_{};
};
}
