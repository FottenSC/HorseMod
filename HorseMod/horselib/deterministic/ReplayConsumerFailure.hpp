#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace Horse::Deterministic {
// Failure-only, one bounded record per process. Start runs before execution.
// Record is also called under the permanent native-entry SRW lock: it must
// never allocate, log through UE4SS, invoke native hooks, or acquire the guard
// lock. Concurrent terminal callers wait for the first direct file write so
// none can kill its writer before persistence. That writer takes no user lock.
// The handle stays process-owned; timeout/fast-fail does not run a destructor.
class ReplayConsumerFailure final {
public:
    enum class Site : unsigned { DispatchAdmission=1, ComponentConsumer, PrerequisiteAdmission,
        QueueScope, NativeBefore, NativeAfter, NativeHeld, ObservedCompletion, NativeQueueSnapshot, PhysicsConsumer,
        PhysicsSubstepConsumer, VfxFinishDispatch };
    struct Record {
        Site site{};
        unsigned reason{}, native_site=~0u, protocol_phase{}, native_started{};
        std::uint64_t epoch{}, task{}, function{}, owner{}, owner_index=~0ull, owner_generation{};
        unsigned original_thread{}, native_thread{};
        std::array<std::uint64_t,16> operands{};
    };
    static bool Start(const wchar_t* path,const char* run) noexcept {
        if(file_!=INVALID_HANDLE_VALUE || !path || !run)return false;
        unsigned n=0;
        for(;run[n];++n) {
            if(n>=96 || !((run[n]>='a' && run[n]<='z') || (run[n]>='0' && run[n]<='9') || run[n]=='-'))return false;
        }
        if(!n)return false;
        const auto file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        for(unsigned i=0;i<=n;++i)run_[i]=run[i];
        file_=file;return true;
    }
    static void Context(std::uint64_t tick,std::uint64_t phase) noexcept {
        tick_.store(tick,std::memory_order_relaxed);phase_.store(phase,std::memory_order_relaxed);
    }
    static void Tick(std::uint64_t tick) noexcept {tick_.store(tick,std::memory_order_relaxed);}
    static bool Enabled() noexcept {return file_!=INVALID_HANDLE_VALUE;}
    static void Write(const Record& r) noexcept {
        if(file_==INVALID_HANDLE_VALUE)return;
        unsigned empty=0;
        if(!state_.compare_exchange_strong(empty,1,std::memory_order_acq_rel)) {
            while(state_.load(std::memory_order_acquire)==1)YieldProcessor();
            return;
        }
        // At most 16 operands plus 16 scalar fields, 20 decimal digits each.
        // All strings are fixed keys or validated ASCII run IDs; no CRT/heap.
        char bytes[2048]{};unsigned n=0;
        const auto text=[&](const char* s){while(*s)bytes[n++]=*s++;};
        const auto number=[&](std::uint64_t v){char digits[20];unsigned c=0;do{digits[c++]=char('0'+v%10);v/=10;}while(v);while(c)bytes[n++]=digits[--c];};
        const auto field=[&](const char* key,std::uint64_t v){text(",\"");text(key);text("\":");number(v);};
        text("{\"run_id\":\"");text(run_.data());text("\"");
        field("version",1);field("pid",GetCurrentProcessId());field("thread",GetCurrentThreadId());
        field("site",static_cast<unsigned>(r.site));field("reason",r.reason);field("native_site",r.native_site);
        field("native_started",r.native_started);field("native_tick",tick_.load(std::memory_order_relaxed));
        field("transaction_phase",phase_.load(std::memory_order_relaxed));field("protocol_phase",r.protocol_phase);
        field("epoch",r.epoch);field("task",r.task);field("function",r.function);field("owner",r.owner);
        field("owner_index",r.owner_index);field("owner_generation",r.owner_generation);
        field("original_thread",r.original_thread);field("native_thread",r.native_thread);
        text(",\"operands\":[");for(unsigned i=0;i<r.operands.size();++i){if(i)text(",");number(r.operands[i]);}text("]}\n");
        DWORD written{};
        const bool complete=WriteFile(file_,bytes,n,&written,nullptr) && written==n;
        persisted_.store(complete && FlushFileBuffers(file_),std::memory_order_relaxed);
        state_.store(2,std::memory_order_release);
    }
    static constexpr std::size_t OwnedBytes() noexcept {return 4096;} // fixed storage, stack record and handle allowance
private:
    inline static HANDLE file_=INVALID_HANDLE_VALUE;
    inline static std::array<char,97> run_{};
    inline static std::atomic<unsigned> state_{};
    inline static std::atomic<bool> persisted_{};
    inline static std::atomic<std::uint64_t> tick_{~0ull},phase_{~0ull};
};
}
