#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "ReplaySeekOwnership.hpp"

namespace Horse::Deterministic {
// Observation only. No native calls, logger, allocation or guard lock while
// serializing. One entry/return pair per entry and scope, twenty-four records max.
class ReplayNiagaraObservation final {
public:
    struct Record {std::array<std::uint64_t,26> values{};};
    struct Token {bool selected{};Record entry{};};
    static bool Start(const wchar_t* path,const char* run) noexcept {
        if(enabled_.load() || file_!=INVALID_HANDLE_VALUE || !path || !run)return false;
        unsigned n{};for(;run[n];++n)if(n>=96 || !((run[n]>='a'&&run[n]<='z') || (run[n]>='0'&&run[n]<='9') || run[n]=='-'))return false;
        if(!n)return false;
        file_=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file_==INVALID_HANDLE_VALUE)return false;
        for(unsigned i=0;i<=n;++i)run_[i]=run[i];
        // An empty event array is a valid run-owned zero-hit observation.
        const bool ok=Persist();enabled_.store(ok,std::memory_order_release);return ok;
    }
    static bool Enabled() noexcept{return enabled_.load(std::memory_order_acquire);}
    static void Tick(std::uint64_t tick) noexcept{tick_.store(tick,std::memory_order_relaxed);}
    static void World(std::uintptr_t world) noexcept{world_.store(world,std::memory_order_relaxed);}
    static void Transaction(ReplaySeekOwnership::Phase phase) noexcept {
        const unsigned scope=phase==ReplaySeekOwnership::Phase::ExecutionActive?1u:
            phase==ReplaySeekOwnership::Phase::Recovered?3u:2u;
        phase_scope_.store((static_cast<std::uint64_t>(phase)<<32)|scope,std::memory_order_release);
    }
    static Token Select(unsigned helper,std::uintptr_t rva,std::uintptr_t caller,std::uintptr_t base,std::uint64_t epoch) noexcept {
        Token token{};if(!Enabled() || helper>=3)return token;
        const auto phase_scope=phase_scope_.load(std::memory_order_acquire);
        const auto scope=static_cast<unsigned>(phase_scope);
        if(selected_[helper*4+scope].exchange(true,std::memory_order_acq_rel))return token;
        token.selected=true;auto& v=token.entry.values;
        v[0]=rva;v[1]=scope;v[2]=0;v[3]=tick_.load();v[4]=phase_scope>>32;v[5]=epoch;
        v[6]=GetCurrentThreadId();v[7]=caller;v[8]=caller>=base&&caller-base<0x5000000?caller-base:~0ull;
        v[9]=world_.load();v[10]=helper*4+scope;
        return token;
    }
    static void Append(const Record& record) noexcept {
        if(!Enabled())return;
        AcquireSRWLockExclusive(&write_lock_);
        if(count_<records_.size()){records_[count_++]=record;if(!Persist())write_failed_=true;}
        ReleaseSRWLockExclusive(&write_lock_);
    }
    static bool ReadStatus(std::uint64_t* output,std::size_t count) noexcept {
        if(!Enabled() || !output || count!=2)return false;
        AcquireSRWLockExclusive(&write_lock_);output[0]=count_;output[1]=write_failed_;
        ReleaseSRWLockExclusive(&write_lock_);return true;
    }
    static constexpr std::size_t OwnedBytes() noexcept{return 65536;}
private:
    static bool Persist() noexcept {
        char bytes[32768]{};unsigned n{};
        const auto text=[&](const char* s){while(*s)bytes[n++]=*s++;};
        const auto number=[&](std::uint64_t v){char digits[20];unsigned c{};do{digits[c++]=char('0'+v%10);v/=10;}while(v);while(c)bytes[n++]=digits[--c];};
        text("{\"run_id\":\"");text(run_.data());text("\",\"version\":2,\"registration_snapshot\":\"pre_original_only\",\"pid\":");number(GetCurrentProcessId());
        text(",\"fields\":[\"helper_rva\",\"scope\",\"boundary\",\"native_tick\",\"transaction_phase\",\"epoch\",\"thread\",\"caller\",\"caller_rva\",\"host_world\",\"pair\",\"input\",\"input_index\",\"input_serial\",\"input_valid\",\"input_table\",\"component\",\"component_index\",\"component_serial\",\"component_valid\",\"component_table\",\"world\",\"world_index\",\"world_serial\",\"world_valid\",\"world_table\"],\"events\":[");
        for(unsigned i=0;i<count_;++i){if(i)text(",");text("[");for(unsigned j=0;j<records_[i].values.size();++j){if(j)text(",");number(records_[i].values[j]);}text("]");}
        text("]}\n");LARGE_INTEGER start{};DWORD written{};
        return SetFilePointerEx(file_,start,nullptr,FILE_BEGIN) && WriteFile(file_,bytes,n,&written,nullptr) && written==n && FlushFileBuffers(file_);
    }
    inline static HANDLE file_=INVALID_HANDLE_VALUE;
    inline static std::atomic<bool> enabled_{};
    inline static std::array<char,97> run_{};
    inline static std::array<std::atomic<bool>,12> selected_{};
    inline static std::array<Record,24> records_{};
    inline static unsigned count_{};
    inline static bool write_failed_{};
    inline static SRWLOCK write_lock_=SRWLOCK_INIT;
    inline static std::atomic<std::uint64_t> phase_scope_{0xffffffff00000000ull};
    inline static std::atomic<std::uint64_t> tick_{~0ull},world_{};
    static_assert(sizeof(records_)+sizeof(selected_)+32768+2*sizeof(Token)+4096<65536);
};
}
