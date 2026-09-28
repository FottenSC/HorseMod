#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>
#include <Unreal/UObjectArray.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "ReplayConsumerFailure.hpp"
#include "ReplayVfxFinishBinding.hpp"

namespace Horse::Deterministic {
// Containment at the existing dispatcher owner, NOT a before-effects lease.
// Native producers may already have mutated state. Nothing here admits G1,
// excludes native writers, retains receivers through ProcessEvent or recovers B.
class ReplayVfxFinishDispatch final {
public:
    using Binding=ReplayVfxFinishBinding;
    using Header=Binding::Header;
    using Row=Binding::Row;
    using ReceiverCheck=bool(*)(const void*,bool,const std::array<std::int32_t,2>&,std::uint64_t);
    enum class Rejection : unsigned { None, Thread, Epoch, Manager, Header, Backing, Row, Receiver, Reentry };
    ReplayVfxFinishDispatch()=default;
    ReplayVfxFinishDispatch(const ReplayVfxFinishDispatch&)=delete;
    ReplayVfxFinishDispatch& operator=(const ReplayVfxFinishDispatch&)=delete;
    ~ReplayVfxFinishDispatch(){if(!Stop())__fastfail(FAST_FAIL_INVALID_ARG);}

    bool Arm(const Binding& binding,const void* context,ReceiverCheck receiver_check) noexcept {
        const auto error=GetLastError();
        if(!installed_base_.load(std::memory_order_acquire)
            || installed_base_.load(std::memory_order_relaxed)!=binding.base || !context || !receiver_check
            || !Shape(binding)) {SetLastError(error);return false;}
        // Check before publishing. This does not close a race with an unowned
        // native writer. Dispatch repeats the check against the same baseline.
        const auto thread=GetCurrentThreadId();
        if(Validate(binding,context,receiver_check,thread)!=Rejection::None) {SetLastError(error);return false;}
        AcquireSRWLockExclusive(&lock_);
        const bool idle=!active_ && !native_calls_;
        if(idle){binding_=binding;context_=context;check_=receiver_check;thread_=thread;active_=this;}
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return idle;
    }
    bool Stop() noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        const bool ours=active_==this;
        const bool idle=!ours || (GetCurrentThreadId()==thread_ && !calls_);
        if(ours && idle){active_=nullptr;context_=nullptr;check_=nullptr;}
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return idle;
    }
private:
    friend class NativeReplayVfxCompletionObservation;
    // Called only after the existing owner's complete startup install succeeds.
    static void Installed(std::uintptr_t base) noexcept {installed_base_.store(base,std::memory_order_release);}
    template<class T> static bool Read(std::uintptr_t p,T& value) noexcept {
        if(!p || p>UINTPTR_MAX-sizeof(T))return false;
        SIZE_T bytes{};
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&value,sizeof(T),&bytes)
            && bytes==sizeof(T);
    }
    static bool Shape(const Binding& b) noexcept {
        const auto& h=b.header;
        if(!b.base || b.base>UINTPTR_MAX-0x4197178 || !b.manager || b.manager>UINTPTR_MAX-0x398
            || b.manager_weak[0]<0 || b.manager_weak[1]<=0 || h.count<0 || h.count>2
            || h.capacity<h.count || h.capacity>4096 || bool(h.data)!=bool(h.capacity)
            || (h.data && ((h.data&7) || h.data>UINTPTR_MAX-std::size_t(h.capacity)*16)))return false;
        for(int i=0;i<h.count;++i) {
            if(b.rows[i].weak[0]<0 || b.rows[i].weak[1]<=0)return false;
            for(int j=0;j<i;++j)if(b.rows[i]==b.rows[j])return false;
        }
        return true;
    }
    static bool Backing(const Header& h) noexcept {
        auto p=h.data;const auto end=p+std::size_t(h.capacity)*16;
        // Validate the entire declared backing range without reading unused
        // native capacity. The bounded VirtualQuery walk is not an allocation lease.
        while(p<end) {
            MEMORY_BASIC_INFORMATION info{};
            if(!VirtualQuery(reinterpret_cast<void*>(p),&info,sizeof(info)) || info.State!=MEM_COMMIT
                || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                || !(info.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY
                    |PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
            const auto region=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            if(info.RegionSize>UINTPTR_MAX-region || region+info.RegionSize<=p)return false;
            p=region+info.RegionSize;
        }
        return true;
    }
    static bool Manager(const Binding& b) noexcept {
        __try {
            auto* item=RC::Unreal::FUObjectArray::IndexToObject(b.manager_weak[0]);
            if(!item || !item->IsValid(false) || item->GetSerialNumber()!=b.manager_weak[1]
                || reinterpret_cast<std::uintptr_t>(item->GetUObject())!=b.manager)return false;
            std::uintptr_t table{};unsigned flags{};std::int32_t index{};
            return Read(b.manager,table) && table==b.base+0x3356f68
                && Read(b.manager+8,flags) && !(flags&0x18000u)
                && Read(b.manager+0xc,index) && index==b.manager_weak[0];
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Receiver(const void* context,ReceiverCheck receiver_check,const Row& row) noexcept {
        __try {return receiver_check(context,false,row.weak,row.name);}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static Rejection Validate(const Binding& b,const void* context,ReceiverCheck receiver_check,DWORD thread) noexcept {
        if(GetCurrentThreadId()!=thread)return Rejection::Thread;
        std::uint64_t epoch{};
        if(!Read(b.base+0x4197170,epoch) || epoch!=b.epoch)return Rejection::Epoch;
        if(!Manager(b))return Rejection::Manager;
        Header h{};
        if(!Read(b.manager+0x388,h) || h!=b.header)return Rejection::Header;
        if(!Backing(h))return Rejection::Backing;
        for(int i=0;i<h.count;++i) {
            Row row{};
            if(!Read(h.data+i*sizeof(Row),row) || row!=b.rows[i])return Rejection::Row;
            if(!Receiver(context,receiver_check,row))return Rejection::Receiver;
        }
        // Recheck after predicate calls, including ordered membership. These
        // reads detect changes only; matching reads still do not exclude ABA,
        // a subsequent writer, or first-listener retirement of the next receiver.
        for(int i=0;i<h.count;++i) {
            Row row{};
            if(!Read(h.data+i*sizeof(Row),row) || row!=b.rows[i])return Rejection::Row;
        }
        if(!Read(b.manager+0x388,h) || h!=b.header)return Rejection::Header;
        if(!Manager(b))return Rejection::Manager;
        if(!Read(b.base+0x4197170,epoch) || epoch!=b.epoch)return Rejection::Epoch;
        return Rejection::None;
    }
    [[noreturn]] void Fail(Rejection why,void* payload) const noexcept {
        ReplayConsumerFailure::Record r{};
        r.site=ReplayConsumerFailure::Site::VfxFinishDispatch;r.reason=static_cast<unsigned>(why);
        r.native_site=0x3d74d0;r.epoch=binding_.epoch;r.owner=binding_.manager;
        r.owner_index=binding_.manager_weak[0];r.owner_generation=binding_.manager_weak[1];
        r.original_thread=thread_;r.task=binding_.manager+0x388;r.function=reinterpret_cast<std::uintptr_t>(payload);
        r.operands[0]=binding_.header.data;r.operands[1]=binding_.header.count;r.operands[2]=binding_.header.capacity;
        for(unsigned i=0;i<binding_.rows.size();++i) {
            r.operands[3+i*3]=static_cast<std::uint32_t>(binding_.rows[i].weak[0]);
            r.operands[4+i*3]=static_cast<std::uint32_t>(binding_.rows[i].weak[1]);
            r.operands[5+i*3]=binding_.rows[i].name;
        }
        ReplayConsumerFailure::Write(r);
        // Returning would suppress callbacks while the producer continues.
        // Do not unwind/release the transaction or B; this is terminal containment.
        __fastfail(FAST_FAIL_INVALID_ARG);
    }
    class Call final {
    public:
        Call(void* collection,void* payload) noexcept {
            const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);++native_calls_;
            if(active_ && reinterpret_cast<std::uintptr_t>(collection)==active_->binding_.manager+0x388) {
                owner_=active_;reentry_=owner_->calls_++!=0;
            }
            ReleaseSRWLockExclusive(&lock_);
            if(owner_) {
                const auto why=reentry_?Rejection::Reentry:Validate(owner_->binding_,owner_->context_,owner_->check_,owner_->thread_);
                if(why!=Rejection::None)owner_->Fail(why,payload);
            }
            SetLastError(error);
        }
        ~Call() {
            const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
            if(owner_)--owner_->calls_;
            --native_calls_;ReleaseSRWLockExclusive(&lock_);SetLastError(error);
        }
        Call(const Call&)=delete;
        Call& operator=(const Call&)=delete;
    private: ReplayVfxFinishDispatch* owner_{};bool reentry_{};
    };
    inline static std::atomic<std::uintptr_t> installed_base_{};
    inline static SRWLOCK lock_=SRWLOCK_INIT;
    inline static ReplayVfxFinishDispatch* active_{};
    inline static unsigned native_calls_{};
    Binding binding_{};const void* context_{};ReceiverCheck check_{};DWORD thread_{};unsigned calls_{};
};
}
