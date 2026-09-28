#pragma once
#include "Types.hpp"
#include "ReplayStreamableDomain.hpp"
#include "ReplayUiTransactionAdmission.hpp"
#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <DynamicOutput/DynamicOutput.hpp>

namespace Horse::Deterministic
{
// Admission only. Native registration, delivery and retirement are forwarded
// unchanged. A stamp is a process-local lease, never rewound simulation state.
class NativeReplayCallbackAdmission final
{
public:
    using Stamp = std::array<std::uint64_t, 3>; // latent, streamable registrations, current resource-domain identity
    bool Bind(std::uintptr_t base, void* world)
    {
        if (active_.load() || !base || !world) return false;
        constexpr unsigned char latent_code[]{0x4c,0x89,0x4c,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x55,0x53,0x41,0x56};
        constexpr unsigned char queue_code[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
        constexpr unsigned char request_code[]{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
        if (std::memcmp(reinterpret_cast<void*>(base+0x1ebcaf0),latent_code,sizeof(latent_code))
            || std::memcmp(reinterpret_cast<void*>(base+0x2141f70),queue_code,sizeof(queue_code))
            || std::memcmp(reinterpret_cast<void*>(base+0x214f930),request_code,sizeof(request_code))
            || !ReplayUiTransactionAdmission::Signature(base)) return false;
        base_=base; world_=world; thread_=GetCurrentThreadId();
        if (!ReadManagers(managers_,instance_)) return false;
        latent_=std::make_unique<PLH::x64Detour>(base+0x1ebcaf0,reinterpret_cast<std::uint64_t>(&AddLatent),&latent_original_);
        queue_=std::make_unique<PLH::x64Detour>(base+0x2141f70,reinterpret_cast<std::uint64_t>(&QueueDelegate),&queue_original_);
        request_=std::make_unique<PLH::x64Detour>(base+0x214f930,reinterpret_cast<std::uint64_t>(&RequestLoad),&request_original_);
        active_.store(this,std::memory_order_release);
        if (latent_->hook() && queue_->hook() && request_->hook()) return true;
        if (!Stop()) __fastfail(FAST_FAIL_INVALID_ARG);
        return false;
    }
    bool Stop()
    {
        if (active_.load()!=this) return true;
        if (GetCurrentThreadId()!=thread_ || calls_.load()) return false;
        if (request_ && request_->isHooked() && !request_->unHook()) return false;
        if (queue_ && queue_->isHooked() && !queue_->unHook()) return false;
        if (latent_ && latent_->isHooked() && !latent_->unHook()) return false;
        if (calls_.load()) return false;
        active_.store(nullptr,std::memory_order_release);
        request_.reset(); queue_.reset(); latent_.reset();
        return true;
    }
    ~NativeReplayCallbackAdmission() { if (!Stop()) __fastfail(FAST_FAIL_INVALID_ARG); }
    Stamp stamp() const noexcept { return {latent_revision_.load(),queue_revision_.load()}; }
    std::size_t owned_bytes() const noexcept {
        // Include conservative trampoline-page reservations, not just C++ objects.
        return sizeof(*this)+3*(sizeof(PLH::x64Detour)+65536);
    }
    using Diagnostic=ReplayStreamableDomain::Diagnostic;
    Diagnostic diagnostic() const noexcept { return diagnostic_; }
    Status Admit(Stamp& output) const noexcept
    {
        diagnostic_={1,0,0};
        if (active_.load()!=this || GetCurrentThreadId()!=thread_ || foreign_.load())
            return Status::failure(FailureCode::WrongThread);
        const auto before=stamp();
        std::array<void*,2> managers{}; void* instance{};
        if (calls_.load() || !ReadManagers(managers,instance) || managers!=managers_ || instance!=instance_)
            return Status::failure(FailureCode::GenerationMismatch);
        for (auto* manager:managers) if (manager)
            for (auto offset:{0u,0x50u,0xa0u}) {
                diagnostic_={2,reinterpret_cast<std::uintptr_t>(manager)+offset,0};
                if (!EmptySparse(static_cast<std::byte*>(manager)+offset))
                    return Status::failure(FailureCode::UnsupportedContent);
            }
        diagnostic_={3,0,0};
        if (!EmptyQueue()) return Status::failure(FailureCode::UnsupportedContent);
        ReplayUiTransactionAdmission::Diagnostic ui{};
        if(!ReplayUiTransactionAdmission::Check(base_,ui)) {
            diagnostic_={ui.check,ui.owner,ui.count};
            return Status::failure(FailureCode::UnsupportedContent);
        }
        std::uint64_t domain{};
        if (!QuiescentStreamableResources(domain)) return Status::failure(FailureCode::UnsupportedContent);
        output=stamp();
        if (before!=output || calls_.load()) return Status::failure(FailureCode::CapturePreflightFailed);
        output[2]=domain;
        diagnostic_={};
        return Status::success();
    }
    Status Validate(const Stamp& expected) const noexcept {
        Stamp current{}; const auto status=Admit(current);
        if(status.ok() && current!=expected)
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] callback resource mismatch expected={:x} current={:x} latent={}/{} registrations={}/{} quiescent=true\n"),expected[2],current[2],expected[0],current[0],expected[1],current[1]);
        return !status.ok()?status:current==expected?Status::success():Status::failure(FailureCode::GenerationMismatch);
    }
private:
    template<class T> static T Read(const void* p,std::size_t offset=0) {
        return *reinterpret_cast<const T*>(static_cast<const std::byte*>(p)+offset);
    }
    bool ReadManagers(std::array<void*,2>& managers,void*& instance) const noexcept {
        __try {
            instance=Read<void*>(world_,0x140);
            managers={static_cast<std::byte*>(world_)+0x438,instance?Read<void*>(instance,0xe0):nullptr};
            return !instance || managers[1];
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool EmptySparse(const void* set) noexcept {
        __try {
            const auto slots=Read<int>(set,8),capacity=Read<int>(set,0xc);
            const auto bits=Read<int>(set,0x28),max_bits=Read<int>(set,0x2c),free=Read<int>(set,0x34);
            const auto* heap=Read<const unsigned*>(set,0x20);
            const auto* words=heap?heap:reinterpret_cast<const unsigned*>(static_cast<const std::byte*>(set)+0x10);
            if(slots<0 || slots>65536 || capacity<slots || bits!=slots || max_bits<bits
                || (!heap&&max_bits>128) || free!=slots || (slots&&!Read<void*>(set))) return false;
            for(int i=0;i<bits;++i) if(words[i/32]&(1u<<(i%32))) return false;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool EmptyQueue() const noexcept {
        __try {
            auto* queue=Read<void*>(reinterpret_cast<void*>(base_+0x43b1ee8));
            if(!queue) return true; // Never call the allocating singleton accessor.
            if(Read<std::uintptr_t>(queue)!=base_+0x39c4fa0) return false;
            auto* lock=reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(queue)+0x18);
            if(!TryEnterCriticalSection(lock)) return false;
            bool empty=false;
            __try {
                empty=Read<int>(queue,0x10)==0 && Read<int>(queue,0x14)>=0
                    && (!Read<int>(queue,0x14)||Read<void*>(queue,8));
            } __finally {LeaveCriticalSection(lock);}
            return empty;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static void Hash(std::uint64_t& hash,std::uint64_t value) noexcept {
        for(unsigned i=0;i<8;++i) {hash^=(value>>(8*i))&255;hash*=1099511628211ull;}
    }
    // Registry lock protects owner membership. Resource maps are admitted only
    // on the replay GT; foreign intercepted registrations invalidate the lease.
    // No reference promotion, allocating lookup, callback delivery or queue edit.
    bool QuiescentStreamableResources(std::uint64_t& hash) const noexcept {
        __try {
            diagnostic_={30,0,0};
            hash=14695981039346656037ull;
            if(Read<int>(reinterpret_cast<void*>(base_+0x43b1f10))!=0) return false;
            auto* registry=Read<void*>(reinterpret_cast<void*>(base_+0x429eac8));
            diagnostic_.owner=reinterpret_cast<std::uintptr_t>(registry);
            if(!registry) return false;
            auto* lock=reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(registry)+0x38);
            if(!TryEnterCriticalSection(lock)) return false;
            bool valid=true;
            __try {
                const auto count=Read<int>(registry,0x30),capacity=Read<int>(registry,0x34);
                auto** owners=Read<void**>(registry,0x28);
                diagnostic_={31,reinterpret_cast<std::uintptr_t>(registry),count};
                valid=count>=0 && count<=16384 && capacity>=count && (!count||owners);
                for(int i=0;valid && i<count;++i) {
                    auto* owner=owners[i];
                    if(!owner || Read<std::uintptr_t>(owner)!=base_+0x39c4f90) continue;
                    std::uint64_t manager_hash=14695981039346656037ull;bool relevant{};
                    valid=ReplayStreamableDomain::Manager(owner,manager_hash,relevant,diagnostic_);
                    if(valid && relevant) {
                        Hash(hash,reinterpret_cast<std::uintptr_t>(owner));Hash(hash,manager_hash);
                    }
                }
            } __finally {LeaveCriticalSection(lock);}
            return valid && Read<int>(reinterpret_cast<void*>(base_+0x43b1f10))==0;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    struct Call {
        NativeReplayCallbackAdmission& self;
        explicit Call(NativeReplayCallbackAdmission& owner, bool relevant):self(owner) {
            self.calls_.fetch_add(1);
            if(relevant && GetCurrentThreadId()!=self.thread_) self.foreign_.store(true);
        }
        ~Call(){self.calls_.fetch_sub(1);}
    };
    static void AddLatent(void* manager,void* object,unsigned uuid,void* action) {
        auto& self=*active_.load(std::memory_order_acquire);
        const bool relevant=manager==self.managers_[0]||manager==self.managers_[1];
        Call call(self,relevant);
        if(relevant) self.latent_revision_.fetch_add(1);
        reinterpret_cast<void(*)(void*,void*,unsigned,void*)>(self.latent_original_)(manager,object,uuid,action);
    }
    static void QueueDelegate(void* queue,void* delegate,void* handle_pair) {
        auto& self=*active_.load(std::memory_order_acquire); Call call(self,true);
        // Conservative global invalidation; unrelated entries are never edited.
        self.queue_revision_.fetch_add(1);
        reinterpret_cast<void(*)(void*,void*,void*)>(self.queue_original_)(queue,delegate,handle_pair);
    }
    static void* RequestLoad(void* manager,void* result,void* paths,void* delegate,
        unsigned priority,unsigned char managed,void* debug_name) {
        auto& self=*active_.load(std::memory_order_acquire); Call call(self,true);
        self.queue_revision_.fetch_add(1);
        return reinterpret_cast<void*(*)(void*,void*,void*,void*,unsigned,unsigned char,void*)>(self.request_original_)(
            manager,result,paths,delegate,priority,managed,debug_name);
    }
    inline static std::atomic<NativeReplayCallbackAdmission*> active_{};
    std::uintptr_t base_{}; void* world_{}; void* instance_{};
    std::array<void*,2> managers_{};
    DWORD thread_{};
    mutable Diagnostic diagnostic_{};
    std::atomic<std::uint64_t> latent_revision_{},queue_revision_{};
    std::atomic<unsigned> calls_{};
    std::atomic<bool> foreign_{};
    std::uint64_t latent_original_{},queue_original_{},request_original_{};
    std::unique_ptr<PLH::x64Detour> latent_,queue_,request_;
};
}
