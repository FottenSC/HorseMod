#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <intrin.h>
#include "ReplayVfxExecutionScope.hpp"
#include "ReplayVfxFinishDispatch.hpp"
#include "NativeReplayMaterialTaskGuard.hpp"

namespace Horse::Deterministic {
// A bounded first-events observer and the sole finish-dispatch hook owner. These
// entries do NOT cover every writer. Double reads detect some instability;
// even matching reads do not establish a synchronized native snapshot or lease.
class NativeReplayVfxCompletionObservation final {
public:
    static bool InstallStartup(std::uintptr_t base,bool initial_constructor) {
        if(!base || !initial_constructor || hooks_ || !StartupDomain(base))return false;
        std::array<unsigned char,48> bytes{};
        for(unsigned i=0;i<rvas_.size();++i)
            if(!Read(base+rvas_[i],bytes) || bytes!=signatures_[i])return false;
        HMODULE module{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Register),&module))return false;
        // Process pinned, including partial installation. Never unhook a live
        // entry or destroy a trampoline that a worker could still be using.
        base_=base;hooks_=new Hooks{};
        const std::array<std::uintptr_t,22> entries{
            reinterpret_cast<std::uintptr_t>(&Register),reinterpret_cast<std::uintptr_t>(&Dispatch),
            reinterpret_cast<std::uintptr_t>(&Prune),reinterpret_cast<std::uintptr_t>(&CopyCollection),
            reinterpret_cast<std::uintptr_t>(&Disable),reinterpret_cast<std::uintptr_t>(&CompleteParticle),
            reinterpret_cast<std::uintptr_t>(&TickBody),reinterpret_cast<std::uintptr_t>(&DisableHub),
            reinterpret_cast<std::uintptr_t>(&HubRegister),reinterpret_cast<std::uintptr_t>(&HubRemove),
            reinterpret_cast<std::uintptr_t>(&C18Append),
            reinterpret_cast<std::uintptr_t>(&C18Compact),
            reinterpret_cast<std::uintptr_t>(&C18RemoveRange),
            reinterpret_cast<std::uintptr_t>(&C18Backing),
            reinterpret_cast<std::uintptr_t>(&C18Destroy),
            reinterpret_cast<std::uintptr_t>(&C18DestroyHub),
            reinterpret_cast<std::uintptr_t>(&C18Grow),
            reinterpret_cast<std::uintptr_t>(&C18Capacity),
            reinterpret_cast<std::uintptr_t>(&C18ClearRow),
            reinterpret_cast<std::uintptr_t>(&C18CopyRow),
            reinterpret_cast<std::uintptr_t>(&C18ResizeRow),reinterpret_cast<std::uintptr_t>(&C18RowBacking)};
        for(unsigned i=0;i<rvas_.size();++i) {
            hooks_->entries[i]=std::make_unique<PLH::x64Detour>(base+rvas_[i],entries[i],&originals_[i]);
            if(!hooks_->entries[i]->hook())return false;
        }
        installed_=true;ReplayVfxFinishDispatch::Installed(base);return true;
    }
    static bool Start(const wchar_t* path,const char* run) noexcept {
        AcquireSRWLockExclusive(&lock_);
        const bool ok=StartLocked(path,run);
        ReleaseSRWLockExclusive(&lock_);return ok;
    }
    // Start all existing observation before loading; gate only C18 selection.
    static bool StartCollection18Combat(const wchar_t* path,const char* run) noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        const bool ok=StartLocked(path,run,true);
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return ok;
    }
    static std::uint64_t ArmCollection18Combat(const char* run,std::uintptr_t manager,
        std::uintptr_t player_slot,std::uintptr_t player,std::uint32_t frame,std::uint64_t epoch) noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        const auto sequence=C18ArmCombat(run,manager,player_slot,player,frame,epoch);
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return sequence;
    }
    static void InvalidateCollection18Combat(const char* run) noexcept {
        const auto error=GetLastError();AcquireSRWLockExclusive(&lock_);
        if(enabled_.load(std::memory_order_acquire) && C18CombatRun(run)) {
            NativeReplayMaterialTaskGuard::InvalidateC18Correlation();
            C18CombatReject("trajectory_failed");Persist();
        }
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);
    }
    static bool ReadCollection18CombatReturn(const char* run,std::uint64_t* output,std::size_t count) noexcept {
        if(!output || count!=13 || !enabled_.load(std::memory_order_acquire))return false;
        const auto error=GetLastError();AcquireSRWLockShared(&lock_);
        const auto& g=c18_combat_;
        const bool ready=C18CombatRun(run) && g.armed && !g.invalidated
            && c18_.selected && c18_.returned && !c18_.active && !write_failed_;
        if(ready) {
            output[0]=c18_.entry;output[1]=c18_.end;output[2]=c18_.thread;
            output[3]=c18_.collection;output[4]=c18_.descriptor;
            output[5]=g.arm_sequence;output[6]=g.arm_frame;output[7]=g.arm_epoch;
            output[8]=g.manager.address;output[9]=g.player.address;
            output[10]=g.selected_frame;output[11]=g.selected_epoch;output[12]=g.selected_round_frame;
        }
        ReleaseSRWLockShared(&lock_);SetLastError(error);return ready;
    }
    // Seal only the observer's admission interval. The lock protects records,
    // never native storage or receiver lifetime. Entry probes that have not
    // acquired it, and later shutdown calls, are outside this phase. Already
    // admitted calls keep their return slots; a cutoff with pending pairs stays
    // incomplete even after those returns drain. No waiting, unhooking or veto.
    static bool ClosePhase(std::uint64_t end_tick) noexcept {
        const auto error=GetLastError();
        AcquireSRWLockExclusive(&lock_);
        bool ok=false;
        if(enabled_.load(std::memory_order_acquire)) {
            if(!phase_closed_) {
                if(c18_.active)NativeReplayMaterialTaskGuard::InvalidateC18Correlation();
                accepting_.store(false,std::memory_order_release);
                ReplayVfxExecutionScope::accepting_.store(false,std::memory_order_release);
                phase_closed_=true;close_tick_=end_tick;close_thread_=GetCurrentThreadId();
                close_stamp_=GetTickCount64();pending_at_close_=pending_;
                producer_pending_at_close_=producer_pending_;
                hub_.pending_at_close=hub_.active;
                c18_.close_sequence=++c18_sequence_;c18_.untracked_at_close=c18_untracked_active_;
                c18_.pending_writers_at_close=c18_.pending_writers;
                c18_.pending_at_close=c18_.active || c18_.pending_writers || (c18_.selected && c18_untracked_active_!=0);
                for(const auto& w:c18_.writers)if(w.pair && !w.returned)c18_.pending_at_close=true;
                registration_pending_at_close_=RegistrationPending();
                Persist();
            }
            ok=PhaseComplete() && close_tick_==end_tick;
        }
        ReleaseSRWLockExclusive(&lock_);SetLastError(error);return ok;
    }
    // count, dropped (lower bound on disk), persistence failure, unfinished
    // pairs, unstable records, truncated records. None is an admission proof.
    static bool ReadStatus(std::uint64_t* output,std::size_t count) noexcept {
        if(!output || count!=6 || !enabled_.load(std::memory_order_acquire))return false;
        AcquireSRWLockExclusive(&lock_);
        output[0]=count_;output[1]=dropped_;output[2]=write_failed_;
        output[3]=pending_;output[4]=unstable_;output[5]=truncated_;
        ReleaseSRWLockExclusive(&lock_);return true;
    }
    // Owned receipt metadata only, for the ordinary diagnostic's stop marker.
    // Callback return is not material, render, GPU or phase completion. Reading
    // this avoids opening the published sidecar while Persist replaces it.
    static bool ReadCollection18Return(std::uint64_t* output,std::size_t count) noexcept {
        if(!output || count!=5 || !enabled_.load(std::memory_order_acquire))return false;
        const auto error=GetLastError();AcquireSRWLockShared(&lock_);
        const bool ready=c18_.selected && c18_.returned && !c18_.active && !write_failed_;
        if(ready) {
            output[0]=c18_.entry;output[1]=c18_.end;output[2]=c18_.thread;
            output[3]=c18_.collection;output[4]=c18_.descriptor;
        }
        ReleaseSRWLockShared(&lock_);SetLastError(error);return ready;
    }
    // Fixed storage, one serialization/snapshot scratch area under lock, bounded
    // selected-call tokens, producer scopes, and 22 detour/trampoline reserves (64 KiB each).
    // No event-path heap allocation, retained UObject or copied native owner.
    static constexpr std::size_t ProcessOwnedBytes() noexcept {return OwnedReservation;}
    // Execution identity only. Native kind1 is task-body entry (not completion);
    // kinds2/3 identify the custom manager entry/resume which bypass that body.
    // Kind4 is the primary disable hub. Stack-local context is never a lease.
    // Only selected producer entry snapshots are retained, not a task census.
    using ExecutionScope=ReplayVfxExecutionScope;
    // Shared with the sole 141D38300 owner; not an additional executor hook.
    static bool BeforeCollection18Broadcast(void* collection,void* descriptor,void* caller) noexcept {
        const auto error=GetLastError();const auto token=C18Before(collection,descriptor,caller);
        SetLastError(error);return token;
    }
    static void AfterCollection18Broadcast(bool token,bool forwarded) noexcept {
        const auto error=GetLastError();C18After(token,forwarded);SetLastError(error);
    }
private:
    // The shortest four-entry run filled 128 records and dropped at least 13
    // calls: >=154 records, not an upper bound on its actual demand. Reserve
    // 512 records (256 paired calls) for a bounded retry with >3x that observed
    // lower bound. This is diagnostic headroom, never a completeness claim:
    // overflow still invalidates the phase and every excess call forwards.
    // Reserve the return at admission, including nested helper calls.
    static constexpr unsigned MaxEvents=512,MaxRows=4;
    // Transfer64 KiB from the existing serialization envelope to the expanded
    // C18 double capture (+47,616 bytes). The aggregate4 MiB assertion below
    // still charges both simultaneously, with all records/tokens/pinned hooks.
    // Serialization keeps a fixed2,068,480-byte buffer and fails closed on
    // exhaustion; no partial receipt is published or native buffer borrowed.
    static constexpr std::size_t SerializedCapacity=4096+MaxEvents*4096+32768-98304,OwnedReservation=4*1024*1024;
    struct Identity {
        std::uintptr_t address{},table{};
        std::int32_t index{-1},serial{};
        bool valid{};
    };
    struct Header {
        std::uintptr_t data{};std::int32_t count{},capacity{};
        bool operator==(const Header&) const = default;
    };
    struct Row {std::int32_t index{},serial{};std::uint64_t name{};};
    static_assert(sizeof(Header)==16 && sizeof(Row)==16);
    struct Record {
        std::uint64_t pair{},parent{},stamp{},producer_id{};
        std::uintptr_t rva{},collection{},receiver{},descriptor{},name{},payload{};
        std::uintptr_t caller{},source_collection{};
        DWORD thread{};unsigned depth{};bool returning{};
        bool attachment{};
        unsigned active_same_thread_same_collection{},active_other_thread_same_collection{};
        unsigned active_dispatch_same_thread_same_collection{},active_dispatch_other_thread_same_collection{};
        Identity manager{},receiver_identity{};
        Header header{},header_after{};
        bool header_read{},header_after_read{},rows_complete{},unstable{},truncated{};
        unsigned row_count{};
        std::array<Row,MaxRows> rows{};
        std::array<Identity,MaxRows> row_identities{};
    };
    struct Active {unsigned entry{};bool used{};};
    struct Hooks {std::array<std::unique_ptr<PLH::x64Detour>,22> entries;};
    struct ExecutionIdentity {
        std::uint64_t id{};
        unsigned kind{},native_thread{};
        std::uintptr_t context{},argument{},caller{},table{},function{},function_table{},scheduled_task{},world{},completion{};
        std::uintptr_t collection{},storage{};
        std::int32_t count{},capacity{},recursion{};
        Identity tick_owner{},hub_owner{};
        bool task_read{},hub_read{};
    };
    // Entry snapshots precede the original producer. Only scopes enclosing a
    // selected manager call are retained/persisted, avoiding an all-particle
    // census. Thus disk entry publication is retrospective, not a crash proof
    // before the producer's first effect. No pointer here is a lifetime lease.
    static constexpr unsigned MaxProducers=128;
    struct Producer {
        std::uint64_t id{},parent{},entry_ms{},return_ms{},epoch{};
        std::uintptr_t rva{},caller{},event{},fence{};
        Identity object{};
        DWORD thread{};
        unsigned depth{};
        unsigned char bound{},update_active{};
        bool active{},selected{},returned{},epoch_read{},particle_read{};
        std::array<ExecutionIdentity,4> owners{};
        unsigned owner_count{},ancestry_count{};
        std::array<void*,16> ancestry{};
        bool owners_truncated{},ancestry_at_capacity{};
    };
#include "NativeReplayVfxCompletionObservation.Collection18.inl"
#include "NativeReplayVfxCompletionObservation.Hub.inl"
#include "NativeReplayVfxCompletionObservation.Registration.inl"
    static void ObserveExecution(Producer& p) noexcept {
        auto* scope=ExecutionScope::current_;
        for(;scope && p.owner_count<p.owners.size();scope=scope->previous_) {
            auto& e=p.owners[p.owner_count++];e.id=scope->id_;e.kind=scope->kind_;
            e.context=scope->context_;e.argument=scope->argument_;e.caller=scope->caller_;
            e.native_thread=scope->native_thread_;
            // All reads precede the selected producer's native effects. No
            // scheduled binding, task/event, hub or receiver is read on return.
            if(e.context && e.context<=~std::uintptr_t{}-0xe8 && Read(e.context,e.table)) {
                if(e.kind==4) {
                    e.collection=e.context+0x78;
                    if(e.table==base_+0x337c4f8 && e.context>=0x28)
                        e.hub_owner=ObserveIdentity(e.context-0x28);
                    e.hub_read=Read(e.collection+0x40,e.storage) && Read(e.collection+0x50,e.count)
                        && Read(e.collection+0x54,e.capacity) && Read(e.collection+0x64,e.recursion);
                } else if(e.table==base_+0x39cd9c0) {
                    e.task_read=Read(e.context+0x10,e.function) && Read(e.context+0x28,e.world)
                        && Read(e.context+0x40,e.completion) && e.function
                        && e.function<=~std::uintptr_t{}-0x58 && Read(e.function,e.function_table)
                        && Read(e.function+0x18,e.scheduled_task);
                    if(e.task_read && (e.function_table==base_+0x381c720 || e.function_table==base_+0x3865f98)) {
                        std::uintptr_t owner{};
                        if(Read(e.function+0x50,owner))e.tick_owner=ObserveIdentity(owner);
                    }
                }
            }
        }
        p.owners_truncated=scope!=nullptr;
        p.ancestry_count=CaptureStackBackTrace(0,static_cast<DWORD>(p.ancestry.size()),p.ancestry.data(),nullptr);
        p.ancestry_at_capacity=p.ancestry_count==p.ancestry.size();
        // A saturated stack is explicitly a prefix, never full ancestry. Scope
        // overflow invalidates observation; native always forwards unchanged.
        if(p.owners_truncated && dropped_!=~0ull)++dropped_;
    }
    static unsigned ProducerBefore(unsigned site,void* object,void* event,unsigned char bound,void* caller) noexcept {
        if(!accepting_.load(std::memory_order_acquire))return MaxProducers;
        AcquireSRWLockExclusive(&lock_);
        unsigned token=MaxProducers;
        if(!phase_closed_) {
            for(unsigned i=0;i<MaxProducers;++i)
                if(!producers_[i].active && !producers_[i].selected){token=i;break;}
            if(token==MaxProducers) {
                if(dropped_!=~0ull)++dropped_;
                if(dropped_==1)Persist();
            } else {
                auto& p=producers_[token];p={};p.id=++next_producer_;
                p.rva=rvas_[site];p.thread=GetCurrentThreadId();p.depth=1;
                for(const auto& parent:producers_)
                    if(parent.active && parent.thread==p.thread && parent.depth>=p.depth) {
                        p.parent=parent.id;p.depth=parent.depth+1;
                    }
                p.caller=reinterpret_cast<std::uintptr_t>(caller);
                p.event=reinterpret_cast<std::uintptr_t>(event);p.bound=bound;
                p.object=ObserveIdentity(reinterpret_cast<std::uintptr_t>(object));
                p.epoch_read=Read(base_+0x4197170,p.epoch);
                ObserveExecution(p);
                HubCorrelate(p);
                // A70 is a retained async fence, NOT the enclosing task. Read
                // it only at entry; completion releases it before callbacks.
                if(site==5 && p.object.valid && p.object.serial>0
                    && p.object.address<=~std::uintptr_t{}-0xa81)
                    p.particle_read=Read(p.object.address+0xa70,p.fence)
                        && Read(p.object.address+0xa80,p.update_active);
                p.entry_ms=GetTickCount64();p.active=true;
            }
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    // Called under lock by an admitted manager entry. Select the complete
    // active ancestor chain on this thread, never a same-time other thread.
    static std::uint64_t SelectProducer(DWORD thread) noexcept {
        Producer* inner=nullptr;
        for(auto& p:producers_)if(p.active && p.thread==thread && (!inner || p.depth>inner->depth))inner=&p;
        const auto id=inner?inner->id:0;
        while(inner) {
            if(hub_.active && inner->rva==rvas_[4] && inner->thread==hub_.thread)
                for(unsigned i=0;i<inner->owner_count;++i)
                    if(inner->owners[i].id==hub_.id && inner->owners[i].kind==4)hub_.selected=true;
            if(!inner->selected){inner->selected=true;++producer_pending_;}
            const auto parent=inner->parent;inner=nullptr;
            for(auto& p:producers_)if(p.active && p.id==parent){inner=&p;break;}
        }
        return id;
    }
    static void ProducerAfter(unsigned token) noexcept {
        if(token==MaxProducers)return;
        AcquireSRWLockExclusive(&lock_);
        auto& p=producers_[token];p.active=false;p.returned=true;p.return_ms=GetTickCount64();
        // Never reacquire/dereference the producer, event or released fence on
        // return. The selected receipt remains incomplete if native never returns.
        if(p.selected){--producer_pending_;Persist();}
        ReleaseSRWLockExclusive(&lock_);
    }
    static constexpr unsigned NoToken=MaxEvents;
    static bool Copy(std::uintptr_t address,void* out,std::size_t bytes) noexcept {
        if(!address || bytes>4096 || address>~std::uintptr_t{}-bytes)return false;
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,bytes,&copied)
            && copied==bytes;
    }
    template<class T> static bool Read(std::uintptr_t address,T& out) noexcept {
        return Copy(address,&out,sizeof(out));
    }
    static bool StartupDomain(std::uintptr_t base) noexcept {
        for(const auto rva:{0x4166720u,0x4168038u,0x4168040u,0x4148b90u,0x44379e0u}) {
            std::uintptr_t value{};if(!Read(base+rva,value) || value)return false;
        }
        return true;
    }
    // Indexed access only; no IsReal scan and no AllocateSerialNumber. Guard
    // the external index service too: an identity observation grants no lease.
    static bool Indexed(std::int32_t index,std::uintptr_t& object,std::int32_t& serial) noexcept {
        if(index<0)return false;
        __try {
            auto* item=RC::Unreal::FUObjectArray::IndexToObject(index);
            if(!item || !item->IsValid(false))return false;
            object=reinterpret_cast<std::uintptr_t>(item->GetUObject());serial=item->GetSerialNumber();
            return object && serial>=0 && item->IsValid(false)
                && reinterpret_cast<std::uintptr_t>(item->GetUObject())==object
                && item->GetSerialNumber()==serial;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static Identity ObserveIdentity(std::uintptr_t address) noexcept {
        Identity result{};result.address=address;
        unsigned flags{};std::uintptr_t indexed{};std::int32_t serial{};
        if(!address || address>~std::uintptr_t{}-16 || !Read(address+0xc,result.index)
            || !Indexed(result.index,indexed,result.serial) || indexed!=address
            || !Read(address,result.table) || !Read(address+8,flags) || (flags&0x18000u)
            || !Indexed(result.index,indexed,serial) || indexed!=address || serial!=result.serial)return result;
        result.valid=true;return result;
    }
    static bool SameIdentity(const Identity& a,const Identity& b) noexcept {
        return a.valid && b.valid && a.serial>0 && a.address==b.address && a.index==b.index
            && a.serial==b.serial && a.table==b.table;
    }
    static bool SameHeader(const Header& a,const Header& b) noexcept {
        return a.data==b.data && a.count==b.count && a.capacity==b.capacity;
    }
    static void Snapshot(Record& record,const Identity* prior) noexcept {
        if(prior) {
            std::uintptr_t address{};std::int32_t serial{};
            if(!prior->valid || prior->serial<=0 || !Indexed(prior->index,address,serial)
                || address!=prior->address || serial!=prior->serial) {
                record.manager=*prior;record.manager.valid=false;record.unstable=true;return;
            }
        }
        // The legacy JSON key "manager" identifies the observed container;
        // collection_kind distinguishes this exact attachment from the manager.
        record.manager=ObserveIdentity(record.collection-(record.attachment?0x1b8:0x388));
        // A return must reacquire the indexed generation observed at entry.
        // If it is gone, do not read either its old collection or row storage.
        if(!record.manager.valid || record.manager.table!=base_+(record.attachment?0x3360370:0x3356f68)
            || (prior && !SameIdentity(*prior,record.manager))) {record.unstable=true;return;}
        if(record.receiver)record.receiver_identity=ObserveIdentity(record.receiver);
        auto& h=record.header;
        record.header_read=Read(record.collection,h);
        if(!record.header_read || h.count<0 || h.capacity<h.count || (h.count&&!h.data)) {
            record.unstable=true;return;
        }
        record.truncated=h.count>static_cast<int>(MaxRows);
        const unsigned wanted=record.truncated?MaxRows:static_cast<unsigned>(h.count);
        for(unsigned i=0;i<wanted;++i) {
            if(h.data>~std::uintptr_t{}-i*sizeof(Row) || !Read(h.data+i*sizeof(Row),record.rows[i])) {
                record.unstable=true;break;
            }
            ++record.row_count;
            const auto& row=record.rows[i];auto& identity=record.row_identities[i];
            std::uintptr_t address{};std::int32_t serial{};
            if(Indexed(row.index,address,serial)) {
                identity=ObserveIdentity(address);
                identity.valid=identity.valid && identity.index==row.index && identity.serial==row.serial;
            }
        }
        record.header_after_read=Read(record.collection,record.header_after);
        if(!record.header_after_read || !SameHeader(h,record.header_after))record.unstable=true;
        // Re-read bytes using guarded copies, never a retained raw dereference.
        // ABA and unobserved writers remain possible even if these reads match.
        for(unsigned i=0;i<record.row_count;++i) {
            Row again{};
            if(!Read(h.data+i*sizeof(Row),again) || std::memcmp(&again,&record.rows[i],sizeof(Row)))record.unstable=true;
        }
        if(!SameIdentity(record.manager,ObserveIdentity(record.manager.address)))record.unstable=true;
        record.rows_complete=!record.unstable && !record.truncated && record.row_count==wanted;
    }
    static unsigned Before(unsigned site,void* collection,void* receiver,std::uint64_t descriptor,
        std::uint64_t name,void* payload,void* caller,void* source=nullptr) noexcept {
        if(!accepting_.load(std::memory_order_acquire))return NoToken;
        const auto address=reinterpret_cast<std::uintptr_t>(collection);
        std::uintptr_t table{};
        bool attachment=false;
        // These entries also serve unrelated collections. Recognize the VFX
        // manager container by its verified vtable; never cache the candidate.
        if(address<0x388 || !Read(address-0x388,table) || table!=base_+0x3356f68) {
            // 141D41870 uses this same dispatcher for exact trace attachment
            // +1B8. Tick disable/active-bit clear ALREADY happened; this is a
            // nested broadcast witness, not pre-deactivation ownership.
            if(site!=1 || address<0x1b8 || !Read(address-0x1b8,table) || table!=base_+0x3360370)return NoToken;
            attachment=true;
        }
        AcquireSRWLockExclusive(&lock_);
        // Recheck under the same lock as ClosePhase: a racing entry cannot
        // append to a sealed phase after passing the inexpensive enabled test.
        if(phase_closed_) {ReleaseSRWLockExclusive(&lock_);return NoToken;}
        // A vtable-shaped container is not enough for these shared helpers.
        // Validate the indexed address/generation before reading its header.
        // This is only a filter, not a lease or synchronization of native data.
        const auto manager=ObserveIdentity(address-(attachment?0x1b8:0x388));
        if(!manager.valid || manager.serial<=0 || manager.table!=base_+(attachment?0x3360370:0x3356f68)) {
            ReleaseSRWLockExclusive(&lock_);return NoToken;
        }
        unsigned token=NoToken;
        if(count_+pending_+2>MaxEvents) {
            // Persist the first overflow. Later drops are visible in status;
            // disk explicitly calls this a lower bound, avoiding endless I/O.
            if(dropped_!=~0ull)++dropped_;
            if(dropped_==1)Persist();
        } else {
            for(unsigned i=0;i<active_.size();++i)if(!active_[i].used){token=i;break;}
            if(token!=NoToken) {
                scratch_={};auto& r=scratch_;
                r.pair=++next_pair_;r.rva=rvas_[site];r.collection=address;
                r.attachment=attachment;
                r.caller=reinterpret_cast<std::uintptr_t>(caller);
                r.source_collection=reinterpret_cast<std::uintptr_t>(source);
                r.receiver=reinterpret_cast<std::uintptr_t>(receiver);r.descriptor=descriptor;r.name=name;
                r.payload=reinterpret_cast<std::uintptr_t>(payload);r.thread=GetCurrentThreadId();
                r.producer_id=SelectProducer(r.thread);
                r.depth=1;r.stamp=GetTickCount64();
                for(const auto& a:active_)if(a.used) {
                    const auto& parent=records_[a.entry];
                    if(parent.collection==r.collection) {
                        if(parent.thread==r.thread)++r.active_same_thread_same_collection;
                        else ++r.active_other_thread_same_collection;
                        if(parent.rva==rvas_[1]) {
                            if(parent.thread==r.thread)++r.active_dispatch_same_thread_same_collection;
                            else ++r.active_dispatch_other_thread_same_collection;
                        }
                    }
                    if(parent.thread==r.thread && parent.depth>=r.depth) {r.depth=parent.depth+1;r.parent=parent.pair;}
                }
                Snapshot(r,&manager);RegistrationBefore(r);active_[token]={count_,true};++pending_;Append();
            }
        }
        ReleaseSRWLockExclusive(&lock_);return token;
    }
    static void After(unsigned token) noexcept {
        if(token==NoToken)return;
        AcquireSRWLockExclusive(&lock_);
        const auto& entry=records_[active_[token].entry];
        scratch_={};auto& r=scratch_;
        r.pair=entry.pair;r.parent=entry.parent;r.depth=entry.depth;r.rva=entry.rva;
        r.producer_id=entry.producer_id;
        r.attachment=entry.attachment;
        // These counts describe overlap at admission, not synchronized native
        // reads. Keep them on the matching return for pair-local inspection.
        r.active_same_thread_same_collection=entry.active_same_thread_same_collection;
        r.active_other_thread_same_collection=entry.active_other_thread_same_collection;
        r.active_dispatch_same_thread_same_collection=entry.active_dispatch_same_thread_same_collection;
        r.active_dispatch_other_thread_same_collection=entry.active_dispatch_other_thread_same_collection;
        r.caller=entry.caller;r.source_collection=entry.source_collection;
        r.collection=entry.collection;r.receiver=entry.receiver;r.descriptor=entry.descriptor;r.name=entry.name;
        r.payload=entry.payload;r.thread=GetCurrentThreadId();r.returning=true;r.stamp=GetTickCount64();
        Snapshot(r,&entry.manager);RegistrationAfter(r);active_[token].used=false;--pending_;Append();
        ReleaseSRWLockExclusive(&lock_);
    }
    static void Append() noexcept {
        if(scratch_.unstable)++unstable_;
        if(scratch_.truncated)++truncated_;
        records_[count_++]=scratch_;Persist();
    }
    // Native MCP disassembly: RCX handler, RDX event, R8B bound byte; void.
    // The original tail-jumps to 1409A98B0/ProcessEvent. Its return therefore
    // encloses that reflected tail as well as the destructive slot traversal.
    __declspec(noinline) static void Disable(void* handler,void* event,unsigned char bound) {
        const auto error=GetLastError();const auto token=ProducerBefore(4,handler,event,bound,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*,void*,unsigned char)>(originals_[4])(handler,event,bound);
        const auto returned_error=GetLastError();ProducerAfter(token);SetLastError(returned_error);
    }
    // Native MCP disassembly: only RCX component; void. Original executes once
    // and runs all event receivers, emitter retirement and completion tails.
    __declspec(noinline) static void CompleteParticle(void* component) {
        const auto error=GetLastError();const auto token=ProducerBefore(5,component,nullptr,0,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*)>(originals_[5])(component);
        const auto returned_error=GetLastError();ProducerAfter(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void TickBody(void* payload,unsigned thread,void* completion_reference) {
        // Native14215ED20 supplies task+10 and task+40. Its existing observer
        // hook remains sole owner; this distinct body entry precedes interval
        // writes and payload execution. It returns BEFORE task completion/free.
        const auto p=reinterpret_cast<std::uintptr_t>(payload);
        ExecutionScope scope(1,reinterpret_cast<void*>(p>=0x10?p-0x10:0),completion_reference,thread,_ReturnAddress());
        reinterpret_cast<void(*)(void*,unsigned,void*)>(originals_[6])(payload,thread,completion_reference);
    }
    __declspec(noinline) static void DisableHub(void* dispatcher,void* request) {
        const auto error=GetLastError();
        ExecutionScope scope(4,dispatcher,request,0,_ReturnAddress());
        const bool admitted=HubBefore(dispatcher);SetLastError(error);
        reinterpret_cast<void(*)(void*,void*)>(originals_[7])(dispatcher,request);
        const auto returned_error=GetLastError();HubAfter(admitted);SetLastError(returned_error);
    }
    // Exact Win64 ABIs: RCX collection, RDX receiver, R8 descriptor, R9 FName;
    // and RCX collection, RDX ProcessEvent parameter buffer. Both return void.
    __declspec(noinline) static void Register(void* collection,void* receiver,std::uint64_t descriptor,std::uint64_t name) {
        const auto error=GetLastError();const auto token=Before(0,collection,receiver,descriptor,name,nullptr,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*,void*,std::uint64_t,std::uint64_t)>(originals_[0])(collection,receiver,descriptor,name);
        const auto returned_error=GetLastError();After(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void Dispatch(void* collection,void* payload) {
        // Independent of optional Before diagnostics and their lock/capacity.
        // The call pin spans native return; neither metadata lock spans native.
        ReplayVfxFinishDispatch::Call protection(collection,payload);
        const auto error=GetLastError();const auto token=Before(1,collection,nullptr,0,0,payload,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*,void*)>(originals_[1])(collection,payload);
        const auto returned_error=GetLastError();After(token);SetLastError(returned_error);
    }
    // Verified Win64 shared helper ABIs: prune consumes only RCX, void;
    // copy consumes RCX destination/RDX source, returns original native RAX.
    // Kismet add/remove reach prune AFTER their direct write: its entry is not
    // a before-write witness for them. Clear's FFrame/tailcall route is excluded.
    // No observer lock spans either original. Native calls always execute once,
    // even if filtering, capacity, persistence or phase closure omits a record.
    __declspec(noinline) static void Prune(void* collection) {
        const auto error=GetLastError();const auto token=Before(2,collection,nullptr,0,0,nullptr,_ReturnAddress());
        SetLastError(error);
        reinterpret_cast<void(*)(void*)>(originals_[2])(collection);
        const auto returned_error=GetLastError();After(token);SetLastError(returned_error);
    }
    __declspec(noinline) static void* CopyCollection(void* collection,void* source) {
        const auto error=GetLastError();const auto token=Before(3,collection,nullptr,0,0,nullptr,_ReturnAddress(),source);
        SetLastError(error);
        auto* result=reinterpret_cast<void*(*)(void*,void*)>(originals_[3])(collection,source);
        const auto returned_error=GetLastError();After(token);SetLastError(returned_error);return result;
    }
    static const char* Route(std::uintptr_t rva) noexcept {
        if(rva==rvas_[0])return "native_registration";
        if(rva==rvas_[1])return "dispatch";
        if(rva==rvas_[2])return "shared_prune";
        return "shared_copy";
    }
    static const char* CallerRoute(const Record& r) noexcept {
        // Exact post-CALL addresses from the audited binary. Unknown callers
        // remain raw process-local addresses, not guessed Kismet attribution.
        if(r.rva==rvas_[2] && r.caller==base_+0xf74897)return "kismet_add_after_direct_write";
        if(r.rva==rvas_[2] && r.caller==base_+0xf77639)return "kismet_remove_after_direct_write";
        if(r.rva==rvas_[3] && r.caller==base_+0xf767d3)return "kismet_let_copy";
        return "other";
    }
    static bool StartLocked(const wchar_t* path,const char* run,bool combat=false) noexcept {
        if(!installed_ || started_ || !path || !run)return false;
        unsigned n{};
        for(;run[n];++n)if(n>=96 || !((run[n]>='a'&&run[n]<='z') || (run[n]>='0'&&run[n]<='9') || run[n]=='-'))return false;
        if(!n)return false;
        unsigned length{};for(;path[length];++length)if(length>=path_.size()-5)return false;
        if(!length)return false;
        std::memcpy(run_.data(),run,n+1);
        std::memcpy(path_.data(),path,(length+1)*sizeof(wchar_t));
        std::memcpy(temporary_.data(),path,length*sizeof(wchar_t));
        std::memcpy(temporary_.data()+length,L".tmp",5*sizeof(wchar_t));
        // Never overwrite an unowned target or staging file. The runner parks
        // the sidecar and journals the absent .tmp before starting this process.
        if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES
            || GetFileAttributesW(temporary_.data())!=INVALID_FILE_ATTRIBUTES)return false;
        c18_combat_.required=combat;
        started_=true;started_stamp_=GetTickCount64();c18_start_sequence_=++c18_sequence_;
        const bool ok=Persist(true);enabled_.store(ok,std::memory_order_release);
        accepting_.store(ok,std::memory_order_release);
        ExecutionScope::observe_=&RegistrationScopeBoundary;
        ExecutionScope::accepting_.store(ok,std::memory_order_release);return ok;
    }
    static void Text(const char* s) noexcept {
        while(*s) {if(length_>=bytes_.size()){serialization_failed_=true;return;}bytes_[length_++]=*s++;}
    }
    static void Number(std::uint64_t value) noexcept {
        char digits[20];unsigned n{};
        do {digits[n++]=char('0'+value%10);value/=10;}while(value);
        while(n) {char one[]{digits[--n],0};Text(one);}
    }
    static void Signed(std::int32_t value) noexcept {
        if(value<0){Text("-");Number(static_cast<std::uint64_t>(-static_cast<std::int64_t>(value)));}
        else Number(value);
    }
    static void Flag(bool value) noexcept {Text(value?"true":"false");}
    static void IdentityJson(const Identity& identity) noexcept {
        Text("{\"address\":");Number(identity.address);Text(",\"index\":");Signed(identity.index);
        Text(",\"serial\":");Signed(identity.serial);Text(",\"table\":");Number(identity.table);
        Text(",\"valid\":");Flag(identity.valid);Text("}");
    }
    static bool PhaseComplete() noexcept {
        return phase_closed_ && !c18_.selected && !pending_at_close_ && !pending_ && !dropped_
            && !producer_pending_ && !producer_pending_at_close_
            && !registration_overflow_ && !registration_pending_at_close_ && !RegistrationPending()
            && (!hub_.selected || (hub_.returned && hub_.snapshot_complete && !hub_.overlap && !hub_.writer_overflow && !hub_.pending_at_close))
            && !write_failed_ && !unstable_ && !truncated_;
    }
    static bool Persist(bool first=false) noexcept {
        length_=0;serialization_failed_=false;
        Text("{\"version\":1,\"run_id\":\"");Text(run_.data());Text("\",\"pid\":");Number(GetCurrentProcessId());
        Text(",\"shared_writer_observation_version\":1,\"clear_observed\":false,\"prune_observes_prior_direct_write\":false");
        Text(",\"producer_observation_version\":1,\"task_attribution_proven\":false,\"producer_entry_persisted_before_native\":false");
        Text(",\"attachment_dispatch_observation_version\":1,\"attachment_pre_deactivation_observed\":false");
        Text(",\"execution_owner_observation_version\":1,\"execution_owner_lease_proven\":false");
        HubJson();
        C18Json();
        RegistrationJson();
        Text(",\"producer_pending\":");Number(producer_pending_);
        Text(",\"producer_pending_at_close\":");Number(producer_pending_at_close_);
        Text(",\"producers\":[");bool comma=false;
        for(const auto& p:producers_)if(p.selected) {
            if(comma)Text(",");comma=true;
            Text("{\"id\":");Number(p.id);Text(",\"parent\":");Number(p.parent);
            Text(",\"entry_rva\":");Number(p.rva);Text(",\"caller\":");Number(p.caller);
            Text(",\"thread\":");Number(p.thread);Text(",\"depth\":");Number(p.depth);
            Text(",\"entry_ms\":");Number(p.entry_ms);Text(",\"return_ms\":");Number(p.return_ms);
            Text(",\"returned\":");Flag(p.returned);Text(",\"object\":");IdentityJson(p.object);
            Text(",\"event\":");Number(p.event);Text(",\"bound_byte\":");Number(p.bound);
            Text(",\"observed_application_epoch\":");Number(p.epoch);Text(",\"epoch_read\":");Flag(p.epoch_read);
            Text(",\"async_fence\":");Number(p.fence);Text(",\"update_active\":");Number(p.update_active);
            Text(",\"particle_state_read\":");Flag(p.particle_read);
            Text(",\"owner_snapshot_boundary\":\"producer_entry\",\"owners_truncated\":");Flag(p.owners_truncated);
            Text(",\"ancestry_at_capacity\":");Flag(p.ancestry_at_capacity);Text(",\"ancestry\":[");
            for(unsigned j=0;j<p.ancestry_count;++j){if(j)Text(",");Number(reinterpret_cast<std::uintptr_t>(p.ancestry[j]));}
            Text("],\"execution_owners\":[");
            for(unsigned j=0;j<p.owner_count;++j) {
                const auto& e=p.owners[j];if(j)Text(",");
                Text("{\"id\":");Number(e.id);Text(",\"kind\":");Number(e.kind);
                Text(",\"context\":");Number(e.context);Text(",\"argument\":");Number(e.argument);
                Text(",\"caller\":");Number(e.caller);Text(",\"native_thread\":");Number(e.native_thread);
                Text(",\"table\":");Number(e.table);Text(",\"function\":");Number(e.function);
                Text(",\"function_table\":");Number(e.function_table);Text(",\"scheduled_task\":");Number(e.scheduled_task);
                Text(",\"world\":");Number(e.world);Text(",\"completion\":");Number(e.completion);
                Text(",\"collection\":");Number(e.collection);Text(",\"storage\":");Number(e.storage);
                Text(",\"count\":");Signed(e.count);Text(",\"capacity\":");Signed(e.capacity);
                Text(",\"recursion\":");Signed(e.recursion);Text(",\"task_read\":");Flag(e.task_read);
                Text(",\"hub_read\":");Flag(e.hub_read);Text(",\"tick_owner\":");IdentityJson(e.tick_owner);
                Text(",\"hub_owner\":");IdentityJson(e.hub_owner);Text("}");
            }
            Text("]}");
        }
        Text("]");
        Text(",\"observation_only\":true,\"writer_coverage_proven\":false,\"synchronized_snapshot\":false,\"unrecognized_collections_omitted\":true,\"capture_start_ms\":");Number(started_stamp_);
        Text(",\"phase\":{\"scope\":\"startup_to_trajectory_completion\",\"boundary\":\"CompleteTrajectory.vfx_status\",\"state\":\"");
        Text(phase_closed_?"closed":"open");Text("\",\"complete\":");Flag(PhaseComplete());
        Text(",\"whole_process_coverage\":false,\"native_quiescence_proven\":false,\"end_tick\":");Number(close_tick_);
        Text(",\"close_thread\":");Number(close_thread_);Text(",\"close_ms\":");Number(close_stamp_);
        Text(",\"pending_at_close\":");Number(pending_at_close_);Text("}");
        Text(",\"max_events\":");Number(MaxEvents);Text(",\"max_rows\":");Number(MaxRows);
        Text(",\"owned_bytes\":");Number(ProcessOwnedBytes());Text(",\"dropped\":");Number(dropped_);
        Text(",\"dropped_is_lower_bound\":true,\"persistence_failed\":");Flag(write_failed_);
        Text(",\"pending_pairs\":");Number(pending_);Text(",\"unstable_records\":");Number(unstable_);
        Text(",\"truncated_records\":");Number(truncated_);Text(",\"events\":[");
        for(unsigned i=0;i<count_;++i) {
            const auto& r=records_[i];if(i)Text(",");
            Text("{\"entry_rva\":");Number(r.rva);Text(",\"boundary\":\"");Text(r.returning?"return":"entry");
            Text("\",\"route\":\"");Text(Route(r.rva));Text("\",\"caller_route\":\"");Text(CallerRoute(r));
            Text("\",\"caller\":");Number(r.caller);Text(",\"source_collection\":");Number(r.source_collection);
            Text(",\"pair\":");Number(r.pair);Text(",\"parent_pair\":");Number(r.parent);
            Text(",\"producer_id\":");Number(r.producer_id);
            Text(",\"depth\":");Number(r.depth);Text(",\"thread\":");Number(r.thread);Text(",\"time_ms\":");Number(r.stamp);
            Text(",\"active_same_thread_same_collection\":");Number(r.active_same_thread_same_collection);
            Text(",\"active_other_thread_same_collection\":");Number(r.active_other_thread_same_collection);
            Text(",\"active_dispatch_same_thread_same_collection\":");Number(r.active_dispatch_same_thread_same_collection);
            Text(",\"active_dispatch_other_thread_same_collection\":");Number(r.active_dispatch_other_thread_same_collection);
            Text(",\"collection\":");Number(r.collection);Text(",\"receiver\":");Number(r.receiver);
            Text(",\"collection_kind\":\"");Text(r.attachment?"trace_attachment_deactivated":"vfx_manager_finished");Text("\"");
            Text(",\"descriptor\":");Number(r.descriptor);Text(",\"function_name\":");Number(r.name);
            Text(",\"payload\":");Number(r.payload);Text(",\"manager\":");IdentityJson(r.manager);
            Text(",\"receiver_identity\":");IdentityJson(r.receiver_identity);
            Text(",\"storage\":");Number(r.header.data);Text(",\"count\":");Signed(r.header.count);
            Text(",\"capacity\":");Signed(r.header.capacity);Text(",\"header_read\":");Flag(r.header_read);
            Text(",\"header_after_read\":");Flag(r.header_after_read);Text(",\"header_after\":[");
            Number(r.header_after.data);Text(",");Signed(r.header_after.count);Text(",");Signed(r.header_after.capacity);Text("]");
            Text(",\"unstable\":");Flag(r.unstable);Text(",\"truncated\":");Flag(r.truncated);
            Text(",\"rows_complete\":");Flag(r.rows_complete);Text(",\"rows\":[");
            for(unsigned j=0;j<r.row_count;++j) {
                if(j)Text(",");Text("[");Signed(r.rows[j].index);Text(",");Signed(r.rows[j].serial);
                Text(",");Number(r.rows[j].name);Text("]");
            }
            Text("],\"row_identities\":[");
            for(unsigned j=0;j<r.row_count;++j){if(j)Text(",");IdentityJson(r.row_identities[j]);}
            Text("]}");
        }
        Text("]}\n");
        if(serialization_failed_) {write_failed_=true;return false;}
        HANDLE file=CreateFileW(temporary_.data(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file==INVALID_HANDLE_VALUE){write_failed_=true;return false;}
        DWORD written{};
        const bool flushed=WriteFile(file,bytes_.data(),length_,&written,nullptr) && written==length_ && FlushFileBuffers(file);
        const bool closed=CloseHandle(file)!=FALSE;
        // Preserve the last complete JSON if writing fails or the process dies.
        // A partial .tmp is separately journal-owned; failure stays sticky in
        // status (and in any later successful publication), never a native veto.
        if(!flushed || !closed || !MoveFileExW(temporary_.data(),path_.data(),
            MOVEFILE_WRITE_THROUGH|(first?0:MOVEFILE_REPLACE_EXISTING))) {write_failed_=true;return false;}
        return true;
    }
    inline static Hooks* hooks_{};
    inline static std::uintptr_t base_{};
    inline static bool installed_{},started_{},write_failed_{},serialization_failed_{};
    inline static std::atomic<bool> enabled_{},accepting_{};
    inline static SRWLOCK lock_=SRWLOCK_INIT;
    inline static std::array<Record,MaxEvents> records_{};
    inline static Record scratch_{};
    inline static std::array<Active,MaxEvents/2> active_{};
    inline static std::array<char,SerializedCapacity> bytes_{};
    inline static std::array<char,97> run_{};
    inline static std::array<wchar_t,1024> path_{},temporary_{};
    inline static unsigned count_{},pending_{},unstable_{},truncated_{},length_{};
    inline static std::uint64_t next_pair_{},dropped_{},started_stamp_{};
    inline static bool phase_closed_{};
    inline static DWORD close_thread_{};
    inline static unsigned pending_at_close_{};
    inline static std::uint64_t close_tick_{},close_stamp_{};
    inline static std::array<Producer,MaxProducers> producers_{};
    inline static std::uint64_t next_producer_{};
    inline static unsigned producer_pending_{},producer_pending_at_close_{};
    inline static std::array<std::uint64_t,22> originals_{};
    inline static constexpr std::array<std::uintptr_t,22> rvas_{0x8c9120,0x3d74d0,0x3ea210,0x9481e0,0x3c5250,0x1f73990,0x215d250,0x400590,0x3a3b70,0x3ca7a0,0x43d210,0x399df0,0x3a1910,0x2050570,0x3ae520,0x912df0,0x2050940,0x3a1c50,0x3a22d0,0x17dfea0,0x399420,0x3a1a70};
    inline static constexpr std::array<std::array<unsigned char,48>,22> signatures_{{
        {0x48,0x89,0x5c,0x24,0x08,0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x55,
         0x56,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xda,0x48,0x8b,0xf1,0x33,0xd2,0x48,0x8d,
         0x4c,0x24,0x20,0xe8,0x88,0x29,0x6b,0,0x33,0xff,0x48,0x8d,0x4c,0x24,0x20,0x48},
        {0x4c,0x8b,0xdc,0x53,0x56,0x41,0x57,0x48,0x81,0xec,0xb0,0,0,0,0x48,0x8b,
         0x05,0x7b,0x1d,0xd1,0x03,0x48,0x33,0xc4,0x48,0x89,0x84,0x24,0x80,0,0,0,
         0x8b,0x59,0x08,0x4c,0x8b,0xfa,0x48,0x8b,0xf1,0x85,0xdb,0x0f,0x8e,0x4c,0x02,0},
        {0x40,0x56,0x57,0x48,0x83,0xec,0x28,0x33,0xf6,0x48,0x8b,0xf9,0x39,0x71,0x08,0x0f,
         0x8e,0x91,0,0,0,0x48,0x89,0x5c,0x24,0x48,0x8b,0xde,0x48,0x89,0x6c,0x24,
         0x50,0x8d,0x6e,0x01,0x4c,0x89,0x74,0x24,0x20,0x44,0x8b,0xf6,0x48,0x89,0x74,0x24},
        {0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x3b,0xca,0x74,0x76,0x44,0x8b,
         0x41,0x0c,0x48,0x89,0x5c,0x24,0x30,0x8b,0x5a,0x08,0x48,0x89,0x74,0x24,0x38,0x48,
         0x8b,0x32,0x89,0x59,0x08,0x85,0xdb,0x75,0x1b,0x45,0x85,0xc0,0x75,0x16,0x48,0x8b},
        {72,137,92,36,24,85,65,86,65,87,72,131,236,80,65,15,182,216,76,139,250,72,139,233,232,243,222,255,255,76,139,240,72,133,192,15,132,191,0,0,0,72,137,116,36,112,72,137},
        {64,83,87,65,87,72,129,236,0,1,0,0,128,61,233,55,34,2,0,72,139,217,116,18,255,21,130,138,43,1,59,5,184,55,34,2,64,15,148,199,235,3,64,183,1,72,139,131},
        {72,137,108,36,24,72,137,124,36,32,65,86,72,131,236,48,128,121,32,0,73,139,232,68,139,242,72,139,249,116,77,128,121,33,0,116,71,72,139,1,72,137,92,36,64,72,137,116},
        {72,131,236,56,139,2,72,131,193,120,137,68,36,32,139,66,4,137,68,36,36,139,66,8,137,68,36,40,139,66,12,72,141,84,36,32,137,68,36,44,232,67,125,147,1,72,131,196},
        {64,85,83,86,87,65,84,65,86,65,87,72,139,236,72,129,236,128,0,0,0,72,139,5,212,86,212,3,72,51,196,72,137,69,240,69,51,228,77,139,241,77,139,248,76,137,101,208},
        {72,137,92,36,16,72,137,108,36,24,86,87,65,87,72,131,236,32,131,121,100,0,76,139,250,72,139,241,15,142,254,0,0,0,76,139,65,64,64,50,237,76,137,116,36,64,73,139},
        {64,85,83,86,87,65,84,65,86,65,87,72,139,236,72,129,236,128,0,0,0,72,139,5,52,192,202,3,72,51,196,72,137,69,240,69,51,228,77,139,241,77,139,248,76,137,101,208},
        {64,83,72,131,236,32,131,121,100,0,72,139,217,15,143,200,0,0,0,132,210,116,15,255,73,96,139,65,96,59,65,80,15,143,181,0,0,0,72,137,108,36,48,139,105,80,72,137},
        {69,133,192,15,132,82,1,0,0,85,86,65,84,65,87,72,131,236,40,72,139,65,64,69,15,182,225,72,137,92,36,80,65,139,232,72,137,124,36,88,72,139,241,76,137,108,36,96},
        {72,137,92,36,8,87,72,131,236,32,72,99,250,72,139,217,65,131,248,1,119,35,72,139,81,64,72,133,210,116,126,76,139,199,73,193,224,6,255,21,252,200,29,1,72,139,75,64},
        {72,131,236,40,72,139,65,64,72,137,92,36,48,72,247,216,72,137,116,36,64,72,139,241,72,27,219,72,137,124,36,32,139,121,80,72,247,219,72,255,203,72,35,217,72,11,89,64},
        {72,137,92,36,8,87,72,131,236,32,72,139,249,72,129,193,96,18,0,0,232,199,244,168,255,72,141,143,96,18,0,0,232,187,244,168,255,72,139,143,128,18,0,0,72,133,201,116},
        {72,137,92,36,8,72,137,116,36,16,87,72,131,236,32,72,99,121,80,139,242,72,139,217,131,255,1,119,7,184,1,0,0,0,235,56,131,123,84,0,185,4,0,0,0,117,5,72},
        {72,137,92,36,8,87,72,131,236,32,72,99,218,72,139,249,133,210,116,42,131,251,1,119,7,187,1,0,0,0,235,30,72,139,203,51,210,72,193,225,6,232,66,241,154,0,72,193},
        {64,83,72,131,236,32,131,121,48,0,72,139,217,116,84,72,139,65,32,72,247,216,72,27,201,72,247,217,72,255,201,72,35,203,72,11,75,32,116,59,72,139,1,51,210,255,80,72},
        {64,83,72,131,236,32,72,139,194,72,139,217,72,139,200,186,40,0,0,0,232,103,149,187,254,72,133,192,116,48,72,141,13,43,184,246,1,72,137,8,139,83,8,137,80,8,139,75},
        {72,137,92,36,8,87,72,131,236,32,131,121,48,0,139,250,72,139,217,116,33,72,139,65,32,72,247,216,72,27,201,72,247,217,72,255,201,72,35,203,72,11,75,32,116,8,72,139},
        {72,137,92,36,8,87,72,131,236,32,72,99,250,72,139,217,65,131,248,2,119,35,72,139,81,32,72,133,210,116,126,76,139,199,73,193,224,4,255,21,252,179,232,2,72,139,75,32}}};
    // Charge simultaneous fixed records, serialization/snapshot scratch, pair
    // tokens, paths and process-pinned detours, even before observation starts.
    // The 8 KiB allowance covers alignment, bounded observer frames and small
    // allocation overhead; each detour retains its existing 64 KiB reserve.
    static_assert(sizeof(records_)+sizeof(scratch_)+sizeof(active_)+sizeof(bytes_)+sizeof(run_)
        +sizeof(path_)+sizeof(temporary_)+sizeof(hooks_)+sizeof(base_)
        +sizeof(installed_)+sizeof(started_)+sizeof(write_failed_)+sizeof(serialization_failed_)
        +sizeof(enabled_)+sizeof(accepting_)+sizeof(lock_)+sizeof(count_)+sizeof(pending_)+sizeof(unstable_)
        +sizeof(truncated_)+sizeof(length_)+sizeof(next_pair_)+sizeof(dropped_)+sizeof(started_stamp_)
        +sizeof(phase_closed_)+sizeof(close_thread_)+sizeof(pending_at_close_)+sizeof(close_tick_)+sizeof(close_stamp_)
        +sizeof(originals_)+sizeof(rvas_)+sizeof(signatures_)+sizeof(Hooks)
        +sizeof(producers_)+sizeof(next_producer_)+sizeof(producer_pending_)+sizeof(producer_pending_at_close_)
        +sizeof(ExecutionScope::current_)+sizeof(ExecutionScope::next_)+sizeof(ExecutionScope::accepting_)
        +sizeof(ExecutionScope::observe_)+sizeof(registration_scopes_)+sizeof(registrations_)
        +sizeof(registration_count_)+sizeof(registration_sequence_)+sizeof(registration_overflow_)+sizeof(registration_pending_at_close_)
        +sizeof(hub_)+sizeof(c18_)+sizeof(c18_active_)+sizeof(c18_sequence_)+sizeof(c18_tracking_overflow_)
        +sizeof(c18_untracked_active_)+sizeof(c18_start_sequence_)+sizeof(c18_correlation_scratch_)
        +C18MaterialOwnedBytes
        +sizeof(ReplayVfxFinishDispatch::installed_base_)+sizeof(ReplayVfxFinishDispatch::lock_)
        +sizeof(ReplayVfxFinishDispatch::active_)+sizeof(ReplayVfxFinishDispatch::native_calls_)
        +22*(sizeof(PLH::x64Detour)+65536)+8192<=OwnedReservation);
};
}
