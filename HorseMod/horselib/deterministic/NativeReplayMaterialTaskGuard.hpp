#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <intrin.h>
#include "ReplayDiagnosticTrace.hpp"

namespace Horse::Deterministic {
// Necessary CPU veto only. No proxy lease, producer exclusion, downstream
// resource/GPU completion or B reconciliation is established by this receipt.
// Installation belongs to the verified-executable initial constructor. Unlike
// the VFX observer this process-pinned owner never starts/stops with diagnostics.
class NativeReplayMaterialTaskGuard final {
public:
    enum class State : unsigned { Clear, CpuPending, Unsupported, CoverageLost, ProducerUncovered };
    struct Receipt { std::uint64_t baseline{}; bool admitted{}; };
    struct ProducerSnapshot {
        // Process totals, not resource ownership or operation completion.
        // Family 0 = Start, 1 = raw-MID helper. Nesting means same native
        // thread, not a proven relationship between component/actor objects.
        std::array<std::uint64_t,2> entries{},returns{},active{};
        std::uint64_t reentries{},concurrent_entries{},helpers_without_start{};
        unsigned active_threads{},this_thread_depth{},peak_depth{};
    };
    struct Snapshot {
        State state=State::CoverageLost;
        std::uint64_t sequence{},builders{},tasks{};
        ProducerSnapshot producers;
    };
    // A clear counter snapshot is not an admission lease. A producer can
    // already hold a raw proxy before either builder, enter after Inspect
    // unlocks, or reenter on this thread. These counters neither exclude those
    // producers nor pin their proxy/resource lifetime across mutation. The
    // separate Start veto below terminates; it cannot admit corrected execution.
    // Reject historical transactions before acquisition/publication until
    // that native contract is proved; ordinary capture remains independent.
    static constexpr bool HistoricalRestoreSupported() noexcept {return false;}
    // Optional process-pinned, read-only diagnostic. The same callback observes
    // both sides of one forwarded Start; it cannot grant admission or veto it.
    using StartDiagnostic = bool(*)(std::uintptr_t,void*,void*,bool) noexcept;
    struct CreationIdentity {
        std::uintptr_t table{};
        std::int32_t index{-1},serial{-1};
        bool valid{};
    };
    using ConstructionDiagnostic = void(*)(std::uintptr_t,CreationIdentity&) noexcept;
    static void ObserveStarts(StartDiagnostic observer,ConstructionDiagnostic identify=nullptr) noexcept {
        ConstructionDiagnostic no_identity{};
        construction_diagnostic_.compare_exchange_strong(no_identity,identify);
        StartDiagnostic empty{};
        start_diagnostic_.compare_exchange_strong(empty,observer);
    }
    // Negative operation scope, NOT an admission/undo lease. Start has no
    // verified reversible ownership for new children, MIDs or downstream work.
    // An unexpected Start while historical owners exist must terminate before
    // 1408D8C65, never skip the call and resume an incomplete history.
    // Declare first in the transaction so it outlives B and native/render
    // teardown. No public early-release operation is provided.
    class TraceStartVeto final {
    public:
        TraceStartVeto() noexcept = default;
        TraceStartVeto(const TraceStartVeto&)=delete;
        TraceStartVeto& operator=(const TraceStartVeto&)=delete;
        bool Block() noexcept {
            const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
            // The same lock covers ProducerEntry, closing both directions:
            // a preexisting call rejects acquisition; a later call terminates.
            // Other material writers/completion remain independently unproved.
            const bool blocked=!blocking_ && !trace_start_veto_ && installed_ && !lost_
                && !builders_ && !pending_ && !producers_.active[0] && !producers_.active[1];
            if(blocked) {trace_start_veto_=this;blocking_=true;}
            ReleaseSRWLockExclusive(&lock_);SetLastError(error);return blocked;
        }
        ~TraceStartVeto() noexcept {
            if(!blocking_)return;
            const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
            if(trace_start_veto_!=this) {
                ReleaseSRWLockExclusive(&lock_);__fastfail(FAST_FAIL_INVALID_ARG);
            }
            trace_start_veto_=nullptr;
            ReleaseSRWLockExclusive(&lock_);SetLastError(error);
        }
    private:
        bool blocking_{};
    };
    static Receipt BeginOperation() noexcept {
        const auto error=GetLastError();
        AcquireSRWLockExclusive(&lock_);
        const Receipt result{sequence_,installed_ && !lost_ && !builders_ && !pending_
            && !producers_.active[0] && !producers_.active[1]};
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return result;
    }
    static Snapshot Inspect(const Receipt& receipt) noexcept {
        const auto error=GetLastError();
        AcquireSRWLockExclusive(&lock_);
        Snapshot result{State::CoverageLost,sequence_,builders_,pending_};
        result.producers=producers_;
        const auto thread=GetCurrentThreadId();
        for(unsigned i=0;i<ProducerCapacity;++i) {
            const auto& record=producer_records_[i];if(!record.generation)continue;
            if(record.thread==thread && record.depth>result.producers.this_thread_depth)
                result.producers.this_thread_depth=record.depth;
            bool first=true;
            for(unsigned j=0;j<i;++j)
                if(producer_records_[j].generation && producer_records_[j].thread==record.thread)first=false;
            if(first)++result.producers.active_threads;
        }
        if(installed_ && !lost_ && receipt.admitted && receipt.baseline<=sequence_)
            result.state=builders_ || pending_ || producers_.active[0] || producers_.active[1] ? State::CpuPending
                : sequence_!=receipt.baseline ? State::Unsupported : State::Clear;
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return result;
    }
    // Separate destructive-boundary receipt: even an idle CPU baseline cannot
    // authorize C-only teardown. Start/helper entry counts now span the raw
    // GetMaterial-to-builder gap, but do not exclude a new producer or pin its
    // provider/MID/proxies. The two entries are not a census of other writers.
    // No producer permit/exclusion is installed here; reset/reinitialization,
    // BeginDestroy/FinishDestroy and +F8/+100 ownership remain unresolved too.
    //
    // This is deliberately a veto, never a positive permission sampled under
    // the lock and consumed after unlocking. All results reject retirement;
    // new work or same-thread reentry cannot turn the result into permission.
    // These inspections never suppress required callbacks or hold a lock across
    // native calls. The separate Start veto is terminal containment only.
    // A future positive result needs a real operation-scoped exclusion AND
    // resource completion contract, not another counter or callback return.
    static Snapshot InspectCOnlyRetirement(const Receipt& receipt) noexcept {
        auto result=Inspect(receipt);
        if(result.state==State::Clear)result.state=State::ProducerUncovered;
        return result;
    }
    // All six signatures and both table associations must match before any
    // patch. Partial/late/failed startup cannot be retried into empty coverage.
    static bool InstallStartup(std::uintptr_t base,bool initial_constructor) {
        if(attempted_)return false;
        attempted_=true;
        if(!base || !initial_constructor || !StartupDomain(base))return false;
        for(unsigned i=0;i<Rvas.size();++i) {
            std::array<unsigned char,48> bytes{};
            if(!Read(base+Rvas[i],bytes) || bytes!=Signatures[i])return false;
        }
        for(unsigned i=0;i<2;++i) {
            std::uintptr_t callback{};
            if(!Read(base+Tables[i]+8,callback) || callback!=base+Rvas[i+2])return false;
        }
        HMODULE module{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Build<0>),&module))return false;
        base_=base;
        // Deliberately process-owned even on allocation/hook failure. A native
        // entry already published may still forward through its trampoline.
        try {
            hooks_=new Hooks{};
            const std::array<std::uintptr_t,6> wrappers{
                reinterpret_cast<std::uintptr_t>(&Build<0>),reinterpret_cast<std::uintptr_t>(&Build<1>),
                reinterpret_cast<std::uintptr_t>(&Callback<0>),reinterpret_cast<std::uintptr_t>(&Callback<1>),
                reinterpret_cast<std::uintptr_t>(&TraceStart),reinterpret_cast<std::uintptr_t>(&TraceHelper)};
            for(unsigned i=0;i<Rvas.size();++i) {
                hooks_->entries[i]=std::make_unique<PLH::x64Detour>(base+Rvas[i],wrappers[i],&originals_[i]);
                if(!hooks_->entries[i]->hook())return false;
            }
        } catch(...) {return false;}
        AcquireSRWLockExclusive(&lock_);
        installed_=!lost_ && !builders_ && !pending_ && !producers_.active[0] && !producers_.active[1]
            && StartupDomain(base);
        const bool result=installed_;
        ReleaseSRWLockExclusive(&lock_);
        if(result) {InstallMaterialObservation(base);InstallCreationObservation(base);}
        return result;
    }
    // Fixed records + metadata + eleven detour/trampoline reservations. Native
    // tasks/proxies/resources are not owned or accounted as freed by this guard.
    static constexpr std::size_t ProcessOwnedBytes() noexcept {return 1024*1024;}
    // One ordinary-forward C18 broadcast interval. IDs below are process-local
    // call IDs, not UObject generations. No observer callback or lock crosses
    // native forwarding; the VFX owner copies these records for serialization.
    static constexpr unsigned CorrelationCapacity=64;
    static constexpr unsigned MaterialCapacity=128;
    static constexpr unsigned RowDispatchCapacity=64;
    struct RowDispatchCall {
        std::uint64_t id{},setter{},getter{},helper{},start{},parent{},entry{},end{},name{};
        std::uintptr_t mid{},caller{};
        std::array<std::uint32_t,4> bits{};
        DWORD thread{};bool row_read{},row_stable{},matched{},returned{};
        // Deliberately no borrowed row pointer, GUID, MID payload or proxy.
    };
    struct RowDispatchCorrelation {
        std::uint64_t attempted{},omitted{},foreign_entries{},unattributed{};
        unsigned count{},pending{};bool invalidated{},reentry{};
        std::array<RowDispatchCall,RowDispatchCapacity> calls{};
    };
    struct MaterialCall {
        // IDs use the enclosing correlation's event sequence. Return pointers
        // are opaque scalars: never resolve/dereference them after native.
        std::uint64_t id{},helper{},start{},parent{},entry{},end{},getter{},name{};
        std::uintptr_t object{},result{},caller{},wrapper_caller{},color{};
        std::array<std::uint32_t,4> bits{};
        DWORD thread{};int slot{};unsigned family{};
        bool returned{},route{},color_read{},color_stable{},linked{};
    };
    struct MaterialCorrelation {
        std::uint64_t attempted{},omitted{},foreign_entries{};
        unsigned count{},pending{};
        bool ready{},invalidated{},reentry{};
        std::array<MaterialCall,MaterialCapacity> calls{};
        RowDispatchCorrelation dispatch{};
    };
    struct CorrelationCall {
        std::uint64_t id{},parent{},start{},entry{},end{},ordinal{},family_ordinal{},name{};
        std::uintptr_t component{},request{},actor{},provider{},color{},caller{};
        std::array<std::uint32_t,4> bits{};
        DWORD thread{};unsigned family{},depth{};
        bool returned{},selected_thread{},color_read{},color_stable{},provider_read{},provider_link_matches{};
    };
    struct Correlation {
        std::uint64_t c18_entry{},sequence{},attempted{},omitted{},foreign_entries{};
        std::array<std::uint64_t,2> families{};
        DWORD thread{};unsigned count{},pending{};
        bool selected{},active{},ended{},guard_ready{},guard_lost{},preexisting{},invalidated{};
        std::array<CorrelationCall,CorrelationCapacity> calls{};
        MaterialCorrelation material{};
        bool Complete() const noexcept {
            return selected && ended && !active && guard_ready && !guard_lost && !preexisting
                && !invalidated && !omitted && !foreign_entries && !pending;
        }
    };
    static void BeginC18Correlation(std::uint64_t entry,bool interrupted) noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        auto& c=correlation_;
        // Never reset/reuse storage while an admitted native call can return.
        if(c.selected)c.invalidated=true;
        else {
            c.selected=c.active=true;c.c18_entry=entry;c.thread=GetCurrentThreadId();
            c.guard_ready=installed_;c.guard_lost=lost_;
            c.material.ready=material_ready_;
            c.preexisting=producers_.active[0] || producers_.active[1];
            c.invalidated=interrupted || !entry;
            material_observing_.store(true,std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);
    }
    static void InvalidateC18Correlation() noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        if(correlation_.selected)correlation_.invalidated=true;
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);
    }
    static void EndC18Correlation(std::uint64_t entry,bool forwarded,bool interrupted) noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);auto& c=correlation_;
        c.invalidated|=!c.active || c.c18_entry!=entry || c.thread!=GetCurrentThreadId()
            || !forwarded || interrupted || c.pending!=0;
        c.active=false;c.ended=true;c.guard_lost|=lost_;
        material_observing_.store(false,std::memory_order_release);
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);
    }
    static void CopyC18Correlation(Correlation& out) noexcept {
        const auto error=GetLastError();AcquireSRWLockShared(&lock_);out=correlation_;
        ReleaseSRWLockShared(&lock_);SetLastError(error);
    }
private:
    static constexpr unsigned Capacity=1024;
    static constexpr unsigned ProducerCapacity=128;
    enum class Phase : unsigned { Empty, Building, Pending, Executing };
    struct Record {std::uint64_t generation{};std::uintptr_t task{};unsigned family{};Phase phase{};};
    struct Token {unsigned slot=Capacity;std::uint64_t generation{};unsigned family{};bool builder_counted{};};
    struct ProducerRecord {std::uint64_t generation{},parent{};DWORD thread{};unsigned family{},depth{};};
    struct ProducerToken {unsigned slot=ProducerCapacity,family{},correlation=CorrelationCapacity;std::uint64_t generation{};bool counted{};};
    struct Hooks {std::array<std::unique_ptr<PLH::x64Detour>,11> entries;};
    static_assert(sizeof(Record)*Capacity+sizeof(ProducerRecord)*ProducerCapacity+sizeof(ProducerSnapshot)
        +sizeof(Correlation)+sizeof(Hooks)+11*(sizeof(PLH::x64Detour)+65536)+32768+8192
        <1024*1024);
    template<class T> static bool Read(std::uintptr_t address,T& value) noexcept {
        SIZE_T copied{};
        return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&copied)
            && copied==sizeof(value);
    }
    static bool StartupDomain(std::uintptr_t base) noexcept {
        for(const auto rva:{0x4166720u,0x4168038u,0x4168040u,0x4148b90u,0x44379e0u}) {
            std::uintptr_t value{};if(!Read(base+rva,value) || value)return false;
        }
        return true;
    }
    static bool Increment(std::uint64_t& value) noexcept {
        if(value==UINT64_MAX){lost_=true;return false;}
        ++value;return true;
    }
    static std::uint64_t CorrelationNext(std::uint64_t& value) noexcept {
        if(value==UINT64_MAX){correlation_.invalidated=true;return 0;}
        return ++value;
    }
    template<class T> static bool CorrelationRead(std::uintptr_t p,std::size_t offset,T& value) noexcept {
        return p && offset<=UINTPTR_MAX-p && Read(p+offset,value);
    }
    static void CorrelationEntry(ProducerToken& token,void* object,void* request,
        std::uint64_t name,float* color,void* caller) noexcept {
        auto& c=correlation_;if(!c.active)return;
        const auto ordinal=CorrelationNext(c.attempted);
        const auto family_ordinal=CorrelationNext(c.families[token.family]);
        c.guard_lost|=lost_;
        if(GetCurrentThreadId()!=c.thread)CorrelationNext(c.foreign_entries);
        if(c.count==CorrelationCapacity){CorrelationNext(c.omitted);return;}
        token.correlation=c.count++;auto& r=c.calls[token.correlation];
        r.id=token.generation;r.family=token.family;r.ordinal=ordinal;r.family_ordinal=family_ordinal;
        r.thread=GetCurrentThreadId();r.selected_thread=r.thread==c.thread;
        r.entry=CorrelationNext(c.sequence);r.caller=reinterpret_cast<std::uintptr_t>(caller);
        if(token.slot<ProducerCapacity) {
            r.parent=producer_records_[token.slot].parent;r.depth=producer_records_[token.slot].depth;
        } else c.invalidated=true;
        for(const auto& p:producer_records_)
            if(p.generation && p.thread==r.thread && p.family==0 && p.generation>r.start)r.start=p.generation;
        ++c.pending;
        if(!token.family) {
            r.component=reinterpret_cast<std::uintptr_t>(object);r.request=reinterpret_cast<std::uintptr_t>(request);
        } else {
            r.actor=reinterpret_cast<std::uintptr_t>(object);r.name=name;r.color=reinterpret_cast<std::uintptr_t>(color);
            // Preserve all bits (including signed zero/NaN); never normalize,
            // resolve an FName, call a getter or inspect proxy/resource payloads.
            std::array<std::uint32_t,4> again{};
            r.color_read=Read(r.color,r.bits);r.color_stable=r.color_read && Read(r.color,again) && again==r.bits;
            std::uintptr_t provider_again{},root{},owner{},actor_table{},provider_table{};
            r.provider_read=CorrelationRead(r.actor,0x398,r.provider);
            // Verified by helper1408D585A and the existing C18ProviderAt
            // native layout. Matching raw links are not ownership/generation.
            r.provider_link_matches=r.provider_read && Read(r.actor,actor_table) && actor_table==base_+0x3361660
                && Read(r.provider,provider_table) && provider_table==base_+0x38829c0
                && CorrelationRead(r.actor,0x168,root) && root==r.provider
                && CorrelationRead(r.provider,0x190,owner) && owner==r.actor
                && CorrelationRead(r.actor,0x398,provider_again) && provider_again==r.provider;
            if(!r.color_stable)c.invalidated=true;
            if(!r.start)c.invalidated=true;
        }
    }
    static ProducerToken ProducerEntry(unsigned family,void* object,void* request,
        std::uint64_t name,float* color,void* caller) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if(family==0 && trace_start_veto_) {
            // Do not read the borrowed component/request, invoke an observer,
            // or forward any native instruction. No lock crosses native work
            // or termination; process death is containment, never B recovery.
            ReleaseSRWLockExclusive(&lock_);__fastfail(FAST_FAIL_INVALID_ARG);
        }
        ProducerToken token;token.family=family;
        if(!installed_)lost_=true;
        const auto thread=GetCurrentThreadId();
        // Any additional Start inside the selected native interval destroys
        // attribution, including a Start on another thread. It still forwards.
        if(!family && creation_.active)creation_.invalidated=creation_.reentry=true;
        std::uint64_t parent{};unsigned depth{};bool same_family{},other_thread{},start{};
        for(const auto& record:producer_records_)if(record.generation) {
            if(record.thread!=thread){other_thread=true;continue;}
            if(record.generation>parent){parent=record.generation;depth=record.depth;}
            same_family|=record.family==family;start|=record.family==0;
        }
        if(same_family)Increment(producers_.reentries);
        if(other_thread)Increment(producers_.concurrent_entries);
        if(correlation_.active && (same_family || other_thread))correlation_.material.invalidated=true;
        if(family==1 && !start) {
            Increment(producers_.helpers_without_start);
            // Unknown ancestry still forwards. Never turn it into selected
            // producer coverage merely because its own helper was observed.
            lost_=true;
        }
        Increment(producers_.entries[family]);
        token.counted=Increment(producers_.active[family]);
        if(Increment(sequence_)) {
            token.generation=sequence_;
            for(unsigned i=0;i<ProducerCapacity;++i)if(!producer_records_[i].generation) {
                producer_records_[i]={sequence_,parent,thread,family,depth+1};token.slot=i;break;
            }
            if(token.slot==ProducerCapacity)lost_=true;
            else if(depth+1>producers_.peak_depth)producers_.peak_depth=depth+1;
        }
        CorrelationEntry(token,object,request,name,color,caller);
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void ProducerReturn(ProducerToken token) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if(token.correlation<CorrelationCapacity) {
            auto& c=correlation_;auto& r=c.calls[token.correlation];
            if(r.id!=token.generation || r.returned || r.thread!=GetCurrentThreadId() || !c.pending)c.invalidated=true;
            else {r.returned=true;r.end=CorrelationNext(c.sequence);--c.pending;}
            if(!c.active)c.invalidated=true;
        }
        if(token.slot<ProducerCapacity) {
            auto& record=producer_records_[token.slot];
            if(record.generation!=token.generation || record.family!=token.family
                || record.thread!=GetCurrentThreadId())lost_=true;
            else {
                for(const auto& child:producer_records_)
                    if(child.generation && child.parent==record.generation)lost_=true;
                record={};
            }
        } else lost_=true;
        if(token.counted) {
            if(producers_.active[token.family])--producers_.active[token.family];else lost_=true;
        }
        Increment(producers_.returns[token.family]);
        ReleaseSRWLockExclusive(&lock_);
    }
    // Verified Win64 void(RCX component, RDX request). Enter before the first
    // native write at 1408D8C65. No speculative class layout is read here.
    __declspec(noinline) static void __fastcall TraceStart(void* component,void* request) {
        const auto incoming=GetLastError();const auto token=ProducerEntry(0,component,request,0,nullptr,_ReturnAddress());
        // ProducerEntry's historical veto precedes even diagnostic reads.
        // No guard lock or observer storage is borrowed by the native callee.
        const auto observer=start_diagnostic_.load(std::memory_order_acquire);
        const bool observed=observer && observer(base_,component,request,false);
        if(observed)BeginCreationObservation(token,component);
        SetLastError(incoming);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(originals_[4])(component,request);
        const auto native_error=GetLastError();
        if(observed)EndCreationObservation(token);
        if(observed)observer(base_,component,request,true);
        ProducerReturn(token);SetLastError(native_error);
    }
    // Verified Win64 void(RCX actor, RDX packed FName, R8 float[4]). Enter
    // before provider read 1408D585A and GetMaterial 1408D588C. Forward all args
    // once, including indirect/orphan/reentrant paths. Exit reads no payload.
    __declspec(noinline) static void __fastcall TraceHelper(void* actor,std::uint64_t name,float* color) {
        const auto incoming=GetLastError();const auto token=ProducerEntry(1,actor,nullptr,name,color,_ReturnAddress());
        SetLastError(incoming);
        reinterpret_cast<void(__fastcall*)(void*,std::uint64_t,float*)>(originals_[5])(actor,name,color);
        const auto native_error=GetLastError();ProducerReturn(token);SetLastError(native_error);
    }
    // Optional observation coverage is independent of the six mandatory veto
    // hooks. A partial install remains pinned/forwarding, but never publishes
    // a complete material receipt and cannot retry into empty coverage.
    class MaterialDetour final : public PLH::x64Detour {
    public:
        using PLH::x64Detour::x64Detour;
        bool InstallExact(std::uintptr_t address,unsigned bytes) {
            setDetourScheme(PLH::x64Detour::VALLOC2);
            // This x64Detour uses the 8-byte destination-holder size as its
            // minimum, rounding to 10 getter bytes (TEST, JS, CMP), or 11
            // setter bytes (through SUB RSP,28h). The JS is relocated;
            // JGE remains at its original address. Never silently certify a
            // redirected target, fallback patch or expanded instruction range.
            // Reject an expanded range before publishing any patch. Use the
            // same disassembler/branch-map algorithm as this hook owner.
            auto instructions=m_disasm.disassemble(address,address,address+100,*this);
            std::uint64_t minimum=8,rounded=8;
            auto prologue=calcNearestSz(instructions,minimum,rounded);
            if(!prologue || !expandProlSelfJmps(*prologue,instructions,minimum,rounded) || rounded!=bytes)return false;
            return hook() && m_fnAddress==address && m_hookSize==bytes
                && m_chosen_scheme==PLH::x64Detour::VALLOC2;
        }
    };
#include "NativeReplayMaterialTaskGuard.TraceCreation.inl"
    static void InstallMaterialObservation(std::uintptr_t base) noexcept {
        const auto error=GetLastError();
        std::array<unsigned char,19> getter{};std::array<unsigned char,16> setter{};
        std::array<unsigned char,23> wrapper{};std::uintptr_t target{};
        std::array<unsigned char,18> dispatch{};
        if(!Read(base+0x1dc8220,getter) || getter!=MaterialGetterSignature
            || !Read(base+0x1f1e420,setter) || setter!=MaterialSetterSignature
            || !Read(base+0x1f45940,wrapper) || wrapper!=MaterialWrapperSignature
            || !Read(base+0x1f05150,dispatch) || dispatch!=MaterialDispatchSignature
            || !Read(base+0x38829c0+0x4e8,target) || target!=base+0x1dc8220) {
            SetLastError(error);return;
        }
        try {
            const std::array<std::uintptr_t,3> targets{base+0x1dc8220,base+0x1f1e420,base+0x1f05150};
            const std::array<std::uintptr_t,3> wrappers{
                reinterpret_cast<std::uintptr_t>(&MaterialGet),reinterpret_cast<std::uintptr_t>(&MaterialSet),
                reinterpret_cast<std::uintptr_t>(&MaterialDispatch)};
            for(unsigned i=0;i<3;++i) {
                auto& hook=hooks_->entries[i+6];
                auto* exact=new MaterialDetour(targets[i],wrappers[i],&originals_[i+6]);
                hook.reset(exact);
                // Six-byte non-register-spoiling jump only. Do not fall back to
                // code caves or longer patches outside the verified prologues.
                // Dispatch's exact first 11 bytes are two PUSHes and SUB RSP:
                // no relative instruction. Its RIP-relative CMP starts at +11
                // and must remain outside the accepted copied span.
                if(!exact->InstallExact(targets[i],i?11:10) || !originals_[i+6]){SetLastError(error);return;}
            }
            AcquireSRWLockExclusive(&lock_);
            material_ready_=!correlation_.selected && StartupDomain(base);
            ReleaseSRWLockExclusive(&lock_);
        } catch(...) {} // Already published trampolines stay process owned.
        SetLastError(error);
    }
    static unsigned MaterialEntry(unsigned family,void* object,int slot,std::uint64_t name,float* color,
        std::uintptr_t caller,std::uintptr_t return_cell) noexcept {
        // Global entries are hot. Outside this one selected interval they
        // forward without taking the observer lock or borrowing native data.
        if(!material_observing_.load(std::memory_order_acquire))return MaterialCapacity;
        AcquireSRWLockExclusive(&lock_);auto& c=correlation_;auto& m=c.material;
        unsigned result=MaterialCapacity;
        if(!c.active){ReleaseSRWLockExclusive(&lock_);return result;}
        const auto thread=GetCurrentThreadId();
        if(thread!=c.thread) {
            // Other hooked getters/setters overlapping this interval make the
            // selected association inconclusive. Do not borrow their payloads.
            CorrelationNext(m.foreign_entries);m.invalidated=true;
            ReleaseSRWLockExclusive(&lock_);return result;
        }
        CorrelationCall* helper=nullptr;unsigned active_helpers{};
        for(unsigned i=0;i<c.count;++i) {
            auto& r=c.calls[i];
            if(r.family==1 && !r.returned && r.thread==thread){helper=&r;++active_helpers;}
        }
        if(!helper){ReleaseSRWLockExclusive(&lock_);return result;}
        CorrelationNext(m.attempted);
        if(m.count==MaterialCapacity) {
            CorrelationNext(m.omitted);m.invalidated=true;
            ReleaseSRWLockExclusive(&lock_);return result;
        }
        result=m.count++;auto& r=m.calls[result];
        r.id=r.entry=CorrelationNext(c.sequence);r.helper=helper->id;r.start=helper->start;
        r.thread=thread;r.family=family;r.object=reinterpret_cast<std::uintptr_t>(object);
        r.slot=slot;r.name=name;r.caller=caller;r.color=reinterpret_cast<std::uintptr_t>(color);
        for(unsigned i=0;i<result;++i)if(!m.calls[i].returned)r.parent=m.calls[i].id;
        if(r.parent || active_helpers!=1){m.reentry=true;m.invalidated=true;}
        ++m.pending;
        bool start=false;
        for(unsigned i=0;i<c.count;++i) {
            const auto& s=c.calls[i];
            if(s.id==r.start && !s.family && !s.returned && s.thread==thread
                && helper->parent==s.id)start=true;
        }
        r.route=start && active_helpers==1 && !r.parent && helper->provider_link_matches;
        if(!family) {
            r.route=r.route && caller==base_+0x8d5892 && r.object==helper->provider && slot>=0;
            int expected_slot=0;
            for(unsigned i=0;i<result;++i)if(m.calls[i].helper==r.helper && !m.calls[i].family)++expected_slot;
            r.route=r.route && slot==expected_slot;
        } else {
            std::array<std::uint32_t,4> again{};
            r.color_read=Read(r.color,r.bits);
            r.color_stable=r.color_read && Read(r.color,again) && again==r.bits;
            // Wrapper SUB RSP,38h then CALL: its caller return cell is +40h
            // from the lower setter's entry return cell. Read only on exact
            // wrapper return PC; its entire 23-byte prefix was checked above.
            const bool wrapper=caller==base_+0x1f45957
                && CorrelationRead(return_cell,0x40,r.wrapper_caller)
                && r.wrapper_caller==base_+0x8d58dd;
            r.route=r.route && wrapper && r.color_stable && helper->color_stable
                && r.name==helper->name && r.bits==helper->bits;
            MaterialCall* last=nullptr;unsigned matches{};bool consumed=false;
            for(unsigned i=0;i<result;++i) {
                auto& g=m.calls[i];if(g.helper!=r.helper)continue;
                if(!g.family) {last=&g;if(g.returned && g.result==r.object && r.object)++matches;}
                else if(last && g.getter==last->id)consumed=true;
                if(!g.family)consumed=false;
            }
            r.linked=r.route && m.ready && !m.invalidated && !c.invalidated && !c.foreign_entries && !c.omitted
                && last && last->route && last->returned && last->end<r.entry
                && last->result==r.object && r.object && matches==1 && !consumed;
            if(r.linked){r.getter=last->id;r.slot=last->slot;}
            else m.invalidated=true;
        }
        if(!r.route)m.invalidated=true;
        ReleaseSRWLockExclusive(&lock_);return result;
    }
    static void MaterialReturn(unsigned slot,std::uintptr_t result) noexcept {
        if(slot==MaterialCapacity)return;
        AcquireSRWLockExclusive(&lock_);auto& c=correlation_;auto& m=c.material;auto& r=m.calls[slot];
        if(!c.active || r.returned || r.thread!=GetCurrentThreadId() || !m.pending)m.invalidated=true;
        else {
            r.result=result;r.returned=true;r.end=CorrelationNext(c.sequence);--m.pending;
            if(!r.family && result)for(unsigned i=0;i<slot;++i) {
                const auto& g=m.calls[i];
                if(!g.family && g.helper==r.helper && g.returned && g.result==result)m.invalidated=true;
            }
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    __declspec(noinline) static void* __fastcall MaterialGet(void* provider,int slot) {
        const auto incoming=GetLastError();
        const auto token=MaterialEntry(0,provider,slot,0,nullptr,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),0);
        SetLastError(incoming);
        auto* result=reinterpret_cast<void*(__fastcall*)(void*,int)>(originals_[6])(provider,slot);
        const auto error=GetLastError();MaterialReturn(token,reinterpret_cast<std::uintptr_t>(result));
        SetLastError(error);return result;
    }
    __declspec(noinline) static void __fastcall MaterialSet(void* mid,std::uint64_t name,float* color) {
        const auto incoming=GetLastError();
        const auto token=MaterialEntry(1,mid,-1,name,color,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
            reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
        SetLastError(incoming);
        reinterpret_cast<void(__fastcall*)(void*,std::uint64_t,float*)>(originals_[7])(mid,name,color);
        const auto error=GetLastError();MaterialReturn(token,0);SetLastError(error);
    }
    static unsigned RowDispatchEntry(void* mid,void* row,std::uintptr_t caller) noexcept {
        if(!material_observing_.load(std::memory_order_acquire))return RowDispatchCapacity;
        AcquireSRWLockExclusive(&lock_);auto& c=correlation_;auto& m=c.material;auto& d=m.dispatch;
        unsigned token=RowDispatchCapacity;
        if(!c.active){ReleaseSRWLockExclusive(&lock_);return token;}
        const auto thread=GetCurrentThreadId();
        if(thread!=c.thread) {
            CorrelationNext(d.foreign_entries);d.invalidated=true;
            ReleaseSRWLockExclusive(&lock_);return token;
        }
        CorrelationNext(d.attempted);
        const MaterialCall* setter=nullptr;unsigned active{};
        for(unsigned i=0;i<m.count;++i) {
            const auto& r=m.calls[i];if(r.returned || r.thread!=thread)continue;
            ++active;if(r.family==1)setter=&r;
        }
        if(!setter) {
            CorrelationNext(d.unattributed);d.invalidated=true;
            ReleaseSRWLockExclusive(&lock_);return token;
        }
        if(d.count==RowDispatchCapacity) {
            CorrelationNext(d.omitted);d.invalidated=true;
            ReleaseSRWLockExclusive(&lock_);return token;
        }
        token=d.count++;auto& r=d.calls[token];
        r.id=r.entry=CorrelationNext(c.sequence);r.setter=setter->id;r.getter=setter->getter;
        r.helper=setter->helper;r.start=setter->start;r.thread=thread;
        r.mid=reinterpret_cast<std::uintptr_t>(mid);r.caller=caller;
        for(unsigned i=0;i<token;++i)if(!d.calls[i].returned)r.parent=d.calls[i].id;
        if(r.parent || active!=1){d.reentry=true;d.invalidated=true;}
        ++d.pending;
        // Reflected FVectorParameterValue: FName +0, four float bits +8.
        // Borrow only during entry and copy the 24 relevant bytes twice. No
        // pointer to the row is retained or read by any return/serializer path.
        struct Payload {std::uint64_t name;std::array<std::uint32_t,4> bits;};
        static_assert(sizeof(Payload)==24 && offsetof(Payload,bits)==8);
        Payload value{},again{};
        r.row_read=Read(reinterpret_cast<std::uintptr_t>(row),value);
        r.name=value.name;r.bits=value.bits;
        r.row_stable=r.row_read && Read(reinterpret_cast<std::uintptr_t>(row),again)
            && value.name==again.name && value.bits==again.bits;
        r.matched=m.ready && !m.invalidated && !m.omitted && !m.foreign_entries
            && !d.invalidated && !c.invalidated && !c.omitted && !c.foreign_entries
            && setter->linked && setter->route && !setter->parent
            && caller==base_+0x1f1e528 && r.mid==setter->object
            && r.row_stable && r.name==setter->name && r.bits==setter->bits;
        // In particular, republisher return +1F1A5AA cannot become a write
        // receipt just because it runs on this thread beneath a selected call.
        if(!r.matched)d.invalidated=true;
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void RowDispatchReturn(unsigned token) noexcept {
        if(token==RowDispatchCapacity)return;
        AcquireSRWLockExclusive(&lock_);auto& c=correlation_;auto& d=c.material.dispatch;auto& r=d.calls[token];
        if(!c.active || r.returned || r.thread!=GetCurrentThreadId() || !d.pending)d.invalidated=true;
        else {r.returned=true;r.end=CorrelationNext(c.sequence);--d.pending;}
        ReleaseSRWLockExclusive(&lock_);
    }
    __declspec(noinline) static void __fastcall MaterialDispatch(void* mid,void* row) {
        const auto incoming=GetLastError();
        const auto token=RowDispatchEntry(mid,row,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        SetLastError(incoming);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(originals_[8])(mid,row);
        const auto error=GetLastError();RowDispatchReturn(token);SetLastError(error);
    }
    static Token BuilderEntry(unsigned family,void* event,unsigned thread) noexcept {
        AcquireSRWLockExclusive(&lock_);
        Token token;token.family=family;
        if(!installed_ || event || thread!=0xff)lost_=true;
        if(builders_==UINT64_MAX || sequence_==UINT64_MAX)lost_=true;
        else {
            ++builders_;++sequence_;
            token.builder_counted=true;
            for(unsigned i=0;i<Capacity;++i)if(records_[i].phase==Phase::Empty) {
                records_[i]={sequence_,0,family,Phase::Building};token={i,sequence_,family,true};break;
            }
            if(token.slot==Capacity)lost_=true;
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void BuilderReturn(Token token,void* output) noexcept {
        // Read only the builder's still-live out cell and newly built task's
        // table, before caller payload writes/submission. Never census payload.
        std::uintptr_t task{},table{};
        const bool valid=Read(reinterpret_cast<std::uintptr_t>(output),task)
            && Read(task,table) && token.family<2 && table==base_+Tables[token.family];
        AcquireSRWLockExclusive(&lock_);
        if(token.slot<Capacity) {
            auto& record=records_[token.slot];
            if(record.phase!=Phase::Building || record.generation!=token.generation || !valid)lost_=true;
            else {
                for(const auto& other:records_)
                    if(other.task==task && other.phase==Phase::Pending)lost_=true;
                // An executing older generation can have recycled this address
                // inside native before its wrapper returns. Keep both slots.
                record.task=task;record.phase=Phase::Pending;++pending_;
            }
        } else lost_=true;
        if(token.builder_counted) {if(builders_)--builders_;else lost_=true;}
        ReleaseSRWLockExclusive(&lock_);
    }
    static Token CallbackEntry(unsigned family,void* task) noexcept {
        AcquireSRWLockExclusive(&lock_);
        Token token;
        for(unsigned i=0;i<Capacity;++i) {
            auto& record=records_[i];
            if(record.phase==Phase::Pending && record.task==reinterpret_cast<std::uintptr_t>(task)) {
                if(token.slot!=Capacity || record.family!=family) {lost_=true;continue;}
                token={i,record.generation,family};
            }
        }
        if(!installed_ || token.slot==Capacity)lost_=true;
        if(token.slot<Capacity)records_[token.slot].phase=Phase::Executing;
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void CallbackReturn(Token token) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if(token.slot<Capacity) {
            auto& record=records_[token.slot];
            if(record.phase!=Phase::Executing || record.generation!=token.generation
                || record.family!=token.family || !pending_)lost_=true;
            else {record={};--pending_;}
        } else lost_=true;
        ReleaseSRWLockExclusive(&lock_);
    }
    // Win64 RCX=out, RDX=optional event, R8d=requested thread, RAX=out.
    // Native epilogues 141F158BE/141F3065E copy RSI (saved RCX) to RAX.
    // Null-event constructors 141F05D20/141F24790 do not submit: registration
    // completes before the caller can fill payload and call its scheduler.
    template<unsigned Family> static void* __fastcall Build(void* out,void* event,unsigned thread) {
        const auto incoming=GetLastError();const auto token=BuilderEntry(Family,event,thread);
        SetLastError(incoming);
        auto* result=reinterpret_cast<void*(__fastcall*)(void*,void*,unsigned)>(originals_[Family])(out,event,thread);
        const auto native_error=GetLastError();BuilderReturn(token,out);SetLastError(native_error);return result;
    }
    template<unsigned Family> static void __fastcall Callback(void* task) {
        const auto incoming=GetLastError();const auto token=CallbackEntry(Family,task);
        SetLastError(incoming);
        reinterpret_cast<void(__fastcall*)(void*)>(originals_[Family+2])(task);
        const auto native_error=GetLastError();CallbackReturn(token);SetLastError(native_error);
    }
    inline static constexpr std::array<std::uintptr_t,6> Rvas{0x1f15740,0x1f304e0,0x1f15fc0,0x1f32d90,0x8d8c40,0x8d5840};
    inline static constexpr std::array<std::uintptr_t,2> Tables{0x39150e8,0x3920258};
    inline static constexpr std::array<std::array<unsigned char,48>,6> Signatures{{
        {0x40,0x56,0x57,0x48,0x83,0xec,0x48,0x48,0x89,0x5c,0x24,0x40,0x48,0x8b,0xf1,0x48,0x89,0x6c,0x24,0x38,0x33,0xdb,0x4c,0x89,0x64,0x24,0x30,0x45,0x8b,0xe0,0x4c,0x89,0x74,0x24,0x28,0x48,0x8b,0xea,0x4c,0x89,0x7c,0x24,0x20,0x89,0x5c,0x24,0x70,0x48},
        {0x40,0x56,0x57,0x48,0x83,0xec,0x48,0x48,0x89,0x5c,0x24,0x40,0x48,0x8b,0xf1,0x48,0x89,0x6c,0x24,0x38,0x33,0xdb,0x4c,0x89,0x64,0x24,0x30,0x45,0x8b,0xe0,0x4c,0x89,0x74,0x24,0x28,0x48,0x8b,0xea,0x4c,0x89,0x7c,0x24,0x20,0x89,0x5c,0x24,0x70,0x48},
        {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0xe8,0x14,0x7b,0x6d,0xff,0x48,0x8b,0x57,0x28,0x4c,0x8d,0x47,0x30,0x48,0x8b,0x4f,0x10,0xe8,0x73,0xf4,0xfe,0xff,0x48,0x8b,0x4f},
        {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0xe8,0x44,0xad,0x6b,0xff,0x48,0x8b,0x4f,0x10,0xe8,0xcb,0xbe,0xff,0xff,0x48,0x8d,0x05,0x9c,0xd4,0x9e,0x01,0xc6,0x47,0x18,0x00}
        ,{0x48,0x89,0x4c,0x24,0x08,0x55,0x53,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd1,0x48,0x81,0xec,0xd8,0x00,0x00,0x00,0x45,0x33,0xed,0x0f,0x29,0xbc,0x24,0xa0,0x00,0x00,0x00,0x4c,0x89,0xa9,0x3c,0x04,0x00,0x00,0x4c,0x8b,0xfa,0x44}
        ,{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x56,0x48,0x83,0xec,0x30,0x48,0x8b,0xf1,0x49,0x8b,0xe8,0x48,0x8b,0x89,0x98,0x03,0x00,0x00,0x33,0xdb,0x48,0x8b,0x01,0xff,0x90,0x28,0x06,0x00,0x00,0x85,0xc0,0x0f,0x8e}
    }};
    inline static SRWLOCK lock_=SRWLOCK_INIT;
    inline static const TraceStartVeto* trace_start_veto_{};
    inline static std::array<Record,Capacity> records_{};
    inline static constexpr std::array<unsigned char,19> MaterialGetterSignature{
        0x85,0xd2,0x78,0x1b,0x3b,0x91,0x10,0x08,0x00,0x00,0x7d,0x13,0x48,0x8b,0x81,0x08,0x08,0x00,0x00};
    inline static constexpr std::array<unsigned char,16> MaterialSetterSignature{
        0x48,0x89,0x54,0x24,0x10,0x53,0x56,0x48,0x83,0xec,0x28,0x48,0x89,0x7c,0x24,0x50};
    inline static constexpr std::array<unsigned char,23> MaterialWrapperSignature{
        0x48,0x83,0xec,0x38,0x41,0x0f,0x10,0x00,0x4c,0x8d,0x44,0x24,0x20,0x0f,0x29,0x44,0x24,0x20,
        0xe8,0xc9,0x8a,0xfd,0xff};
    inline static constexpr std::array<unsigned char,18> MaterialDispatchSignature{
        0x40,0x53,0x41,0x56,0x48,0x81,0xec,0x88,0x00,0x00,0x00,0x80,0x3d,0xde,0xc6,0x44,0x02,0x00};
    inline static std::array<std::uint64_t,11> originals_{};
    inline static std::array<ProducerRecord,ProducerCapacity> producer_records_{};
    inline static ProducerSnapshot producers_{};
    inline static Correlation correlation_{};
    inline static Hooks* hooks_{};
    inline static std::uintptr_t base_{};
    inline static std::uint64_t sequence_{},builders_{},pending_{};
    inline static bool attempted_{},installed_{},lost_{},material_ready_{};
    inline static std::atomic<bool> material_observing_{false};
    inline static std::atomic<StartDiagnostic> start_diagnostic_{};
    inline static std::atomic<ConstructionDiagnostic> construction_diagnostic_{};
};
}
