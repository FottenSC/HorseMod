#pragma once
#include "NativeReplayTraceTaskGuard.hpp"
#include "ReplayPrerequisiteConsumer.hpp"
#include "ReplayConsumerFailure.hpp"
#include <algorithm>
#include <span>

namespace Horse::Deterministic {
// One already linked GT edge removal. This owner never cancels a task or
// owns native prerequisite backing; scheduler BeginExecution must precede Arm.
class NativeReplayPrerequisiteGuard final {
public:
    using Protocol=ReplayPrerequisiteConsumer;
    using Contract=Protocol::Contract;
    using Identity=Protocol::Identity;
    bool Configure(std::uintptr_t base,const Sc6ReplayTaskGroup& tasks,std::span<const Contract> contracts,
        std::span<const NativeReplayTraceTaskGuard::Binding> roots) noexcept {
        if(armed_ || !base || contracts.empty() || contracts.size()>contracts_.size() || roots.size()!=roots_.size())return false;
        base_=base;thread_=GetCurrentThreadId();tasks_=&tasks;
        count_=contracts.size();std::copy(contracts.begin(),contracts.end(),contracts_.begin());
        std::copy(roots.begin(),roots.end(),roots_.begin());
        for(std::size_t i=0;i<count_;++i) {
            if(!contracts_[i].valid())return false;
            for(std::size_t j=0;j<i;++j)if(contracts_[j].tick==contracts_[i].tick)return false;
        }
        return true;
    }
    bool Arm() {
        if(armed_ || !tasks_ || !count_)return false;
        armed_=guard_.BindExecution(base_,{this,
            +[](void* p,unsigned s,void* a,void* b,void* c)noexcept{
                auto& self=*static_cast<NativeReplayPrerequisiteGuard*>(p);const bool ok=self.Before(s,a,b,c);
                if(!ok)self.Failure(ReplayConsumerFailure::Site::NativeBefore,s,a,b,c);return ok;},
            +[](void* p,unsigned s,void* a,void* b,void* c)noexcept{
                auto& self=*static_cast<NativeReplayPrerequisiteGuard*>(p);const bool ok=self.After(s,a);
                if(!ok)self.Failure(ReplayConsumerFailure::Site::NativeAfter,s,a,b,c);return ok;}});
        if(!armed_)return false;
        for(const auto& root:roots_)if(!RootStable(root)) {
            if(!Stop())__fastfail(FAST_FAIL_INVALID_ARG);
            return false;
        }
        for(std::size_t i=0;i<count_;++i) {
            Contract observed{};
            if(!Observe(contracts_[i],observed) || observed!=contracts_[i]) {
                if(!Stop())__fastfail(FAST_FAIL_INVALID_ARG);
                return false;
            }
        }
        return true;
    }
    bool Stop() {
        if(pop_.load() || (tasks_ && tasks_->executing_consumer_task()) || !guard_.Stop())return false;
        armed_=false;return true;
    }
    Sc6ReplayTaskGroup::ConsumerAdmissionHooks Hooks() noexcept {
        Sc6ReplayTaskGroup::ConsumerAdmissionHooks hooks{};hooks.context=this;
        hooks.acquire=+[](void* p,Sc6ReplayTaskGroup::ConsumerTask& task)noexcept {
            return static_cast<NativeReplayPrerequisiteGuard*>(p)->Admit(task);
        };
        // This disposition never authorizes an untouched hold/resume.
        hooks.validate=+[](void*,const Sc6ReplayTaskGroup::ConsumerTask&)noexcept{return false;};
        hooks.begin_execution=hooks.validate;
        hooks.completed=+[](void* p,const Sc6ReplayTaskGroup::ConsumerTask& task)noexcept {
            auto& self=*static_cast<NativeReplayPrerequisiteGuard*>(p);
            if(!self.protocol_.Completed(reinterpret_cast<std::uintptr_t>(task.task),task.application_epoch)) {
                self.Failure(ReplayConsumerFailure::Site::ObservedCompletion,0,task.task,task.function,nullptr);
                __fastfail(FAST_FAIL_INVALID_ARG);
            }
        };
        hooks.pop_scope=+[](void* p,bool enter)noexcept {
            auto& self=*static_cast<NativeReplayPrerequisiteGuard*>(p);
            const auto thread=GetCurrentThreadId();
            const auto* active=thread==self.thread_?self.tasks_->executing_consumer_task():nullptr;
            const auto depth=thread==self.thread_ && !active?Read<int>(self.guard_.named_thread(),0x3e0):-1;
            // Preserve the original short-circuit exchange semantics.
            const bool prior=thread==self.thread_ && !active && depth==1?self.pop_.exchange(enter):self.pop_.load();
            if(thread!=self.thread_ || active || depth!=1 || prior==enter) {
                self.Failure(ReplayConsumerFailure::Site::QueueScope,
                    thread!=self.thread_?1:active?2:depth!=1?3:4,
                    reinterpret_cast<void*>(static_cast<std::uintptr_t>(depth)),
                    reinterpret_cast<void*>(static_cast<std::uintptr_t>(enter)),
                    reinterpret_cast<void*>(static_cast<std::uintptr_t>(prior)));
                __fastfail(FAST_FAIL_INVALID_ARG);
            }
        };
        return hooks;
    }
    bool changed() const noexcept {return protocol_.changed();}
    bool completed() const noexcept {return protocol_.phase()==Protocol::Phase::Completed;}
    const Protocol& witness() const noexcept {return protocol_;}
    // Read-only diagnostic selection. The real mutation entry rechecks every
    // witness; this query neither admits nor changes the production protocol.
    bool MutationCandidate(void* source_task,void*& mesh_owner,void*& source_owner)const noexcept {
        mesh_owner=source_owner=nullptr;
        if(!armed_ || GetCurrentThreadId()!=thread_ || protocol_.phase()!=Protocol::Phase::Empty)return false;
        const auto* active=tasks_->executing_consumer_task();
        if(!active || active->task!=source_task)return false;
        for(const auto& root:roots_)if(!RootStable(root))return false;
        for(std::size_t i=0;i<count_;++i) {
            const auto& expected=contracts_[i];Contract observed{};Protocol::Task mesh{},source{};
            Protocol candidate;
            if(Observe(expected,observed) && ReadTask(expected.owner,expected.world,mesh)
                && ReadTask(expected.source,expected.world,source)
                && candidate.BeforeRemove(expected,observed,mesh,source,reinterpret_cast<std::uintptr_t>(source_task),base_+0x39cd9c0,thread_)) {
                mesh_owner=reinterpret_cast<void*>(expected.owner.object);source_owner=reinterpret_cast<void*>(expected.source.object);
                return true;
            }
        }
        return false;
    }
private:
    static std::uint64_t AtomicQueueWord(std::uintptr_t address)noexcept {
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(address),0,0));
    }
    std::uintptr_t QueueNode(std::uint64_t token)const noexcept {
        // Native140D2B9F0 resolves a 26-bit node ID through 16384-node pages;
        // each pool node is24 bytes. Copy node words only, never task storage.
        const auto id=token&0x3ffffffull;if(!id)return 0;
        const auto page=Read<std::uintptr_t>(base_+0x415dae8,(id>>14)*8);
        const auto offset=(id&0x3fff)*0x18;
        return page && page<=UINTPTR_MAX-offset?page+offset:0;
    }
    void QueueFailure(std::uintptr_t queue)const noexcept {
        if(!ReplayConsumerFailure::Enabled())return;
        ReplayConsumerFailure::Record r{};r.site=ReplayConsumerFailure::Site::NativeQueueSnapshot;
        r.native_site=24;r.original_thread=thread_;r.protocol_phase=static_cast<unsigned>(protocol_.phase());
        r.owner=queue;r.function=guard_.named_thread();
        if(GetCurrentThreadId()!=thread_)r.reason=1;
        else __try {
            r.epoch=Read<std::uint64_t>(base_+0x4197170);
            r.native_thread=Read<unsigned>(guard_.named_thread(),0x10);
            r.operands[8]=Read<int>(guard_.named_thread(),0x3e0);
            r.operands[15]=pop_.load();
            const auto head=r.operands[0]=AtomicQueueWord(queue+0x80);
            r.operands[1]=AtomicQueueWord(queue+0x108);
            const auto node=QueueNode(head);r.operands[6]=node;
            if(node) {
                const auto next=r.operands[2]=AtomicQueueWord(node);
                if(const auto successor=QueueNode(next))r.task=AtomicQueueWord(successor+8);
                r.operands[3]=AtomicQueueWord(queue+0x80);
                r.operands[4]=AtomicQueueWord(queue+0x108);
                r.operands[5]=AtomicQueueWord(node);
                r.operands[7]=(head==r.operands[3]?1:0) | (r.operands[1]==r.operands[4]?2:0)
                    | (next==r.operands[5]?4:0) | (!(next&0x3ffffffull)?8:0);
            } else r.reason=3;
            //142167D10 waits on sequencer+980 before1415E3830 releases these
            // retained event references. Only the original GT mutates this
            // array; workers may complete its events. Sample at most8 heads.
            // This census and queue rereads are comparison data, never admission.
            const auto sequencer=r.operands[9]=tasks_?reinterpret_cast<std::uintptr_t>(tasks_->sequencer_):0;
            if(sequencer) {
                const auto count=Read<int>(sequencer,0x9a8),capacity=Read<int>(sequencer,0x9ac);
                r.operands[10]=count;r.operands[11]=capacity;
                const auto heap=Read<std::uintptr_t>(sequencer,0x9a0);
                if(count>=0 && count<=4096 && capacity>=count && (heap || count<=4)) {
                    const auto data=heap?heap:sequencer+0x980;
                    r.operands[14]=static_cast<unsigned>((std::min)(count,8));
                    for(unsigned i=0;i<r.operands[14];++i) {
                        const auto event=Read<std::uintptr_t>(data,i*8);
                        if(!event)continue;
                        r.operands[13]|=1ull<<i;
                        if(AtomicQueueWord(event+8)&(1ull<<26))r.operands[12]|=1ull<<i;
                    }
                } else r.reason=4;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){r.reason=2;}
        ReplayConsumerFailure::Write(r);
    }
    void Failure(ReplayConsumerFailure::Site site,unsigned reason,void* a,void* b,void* c,
        const Contract* expected=nullptr,const Protocol::Task* task=nullptr,const Contract* observed=nullptr)const noexcept {
        ReplayConsumerFailure::Record r{};r.site=site;r.reason=reason;r.original_thread=thread_;
        r.protocol_phase=static_cast<unsigned>(protocol_.phase());
        r.native_started=site==ReplayConsumerFailure::Site::NativeAfter || site==ReplayConsumerFailure::Site::ObservedCompletion;
        if(site==ReplayConsumerFailure::Site::NativeBefore || site==ReplayConsumerFailure::Site::NativeAfter)r.native_site=reason;
        r.task=reinterpret_cast<std::uintptr_t>(a);r.function=reinterpret_cast<std::uintptr_t>(b);
        r.operands[0]=reinterpret_cast<std::uintptr_t>(c);r.operands[1]=armed_;r.operands[2]=pop_.load();
        // Native epoch is process-global storage, never a borrowed UObject.
        __try {if(base_)r.epoch=Read<std::uint64_t>(base_+0x4197170);}
        __except(EXCEPTION_EXECUTE_HANDLER){}
        if(expected){r.owner=expected->owner.object;r.owner_index=expected->owner.index;r.owner_generation=expected->owner.serial;
            r.operands[3]=expected->count;r.operands[4]=observed?observed->count:~0ull;
            r.operands[5]=observed && *expected==*observed;}
        if(task){r.task=task->task;r.function=task->tick;r.epoch=task->epoch;r.native_thread=task->native_thread;
            r.operands[6]=task->admitted;r.operands[7]=task->queued;r.operands[8]=task->dependencies;
            r.operands[9]=task->event_head;r.operands[10]=task->flags;r.operands[11]=task->enabled;
            r.operands[12]=task->constructed;r.operands[13]=task->desired_thread;r.operands[14]=task->payload_thread;
            r.operands[15]=expected && task->queued_on_gt(expected->tick,expected->world.object,base_+0x39cd9c0,thread_);}
        ReplayConsumerFailure::Write(r);
    }
    template<class T>static T Read(std::uintptr_t p,std::size_t offset=0)noexcept {
        return *reinterpret_cast<const T*>(p+offset);
    }
    static bool Live(const Identity& id)noexcept {
        if(!id.valid())return false;
        auto* item=RC::Unreal::FUObjectArray::IndexToObject(id.index);
        return item && reinterpret_cast<std::uintptr_t>(item->GetUObject())==id.object
            && item->IsValid(false) && item->GetSerialNumber()==id.serial
            && !(Read<unsigned>(id.object,8)&0x18000u);
    }
    struct RootFailure {unsigned check{};std::uint64_t expected{},actual{};};
    bool RootStable(const NativeReplayTraceTaskGuard::Binding& b,RootFailure* failure=nullptr)const noexcept {
        __try {
            const auto live=[](const auto& id){return Live({id.object,id.index,id.serial});};
            const auto matches_value=[&](unsigned site,std::uint64_t expected,std::uint64_t actual){
                if(expected==actual)return true;if(failure)*failure={site,expected,actual};return false;};
            // All indexed identities precede every UObject dereference.
            if(!matches_value(1,1,live(b.component)) || !matches_value(2,1,live(b.manager)) || !matches_value(3,1,live(b.chara))
                || !matches_value(4,1,live(b.scene)) || !matches_value(5,1,live(b.world)))return false;
            return matches_value(6,base_+0x3360ca8,Read<std::uintptr_t>(b.component.object))
                && matches_value(7,base_+0x3361f98,Read<std::uintptr_t>(b.manager.object))
                && matches_value(8,base_+0x3268078,Read<std::uintptr_t>(b.chara.object))
                && matches_value(9,base_+0x3360370,Read<std::uintptr_t>(b.scene.object))
                && matches_value(10,b.manager.object,Read<std::uintptr_t>(b.component.object,0x190))
                && matches_value(11,b.world.object,Read<std::uintptr_t>(b.component.object,0x1c8))
                && matches_value(12,b.chara.object,Read<std::uintptr_t>(b.component.object,0x490))
                && matches_value(13,b.component.object,Read<std::uintptr_t>(b.manager.object,0x3a8))
                && matches_value(14,b.manager.object,Read<std::uintptr_t>(b.chara.object,0x458))
                && matches_value(15,b.scene.object,Read<std::uintptr_t>(b.chara.object,0x168))
                && matches_value(16,b.chara.object,Read<std::uintptr_t>(b.scene.object,0x190))
                && matches_value(17,0,Read<std::uintptr_t>(b.component.object,0x460));
        } __except(EXCEPTION_EXECUTE_HANDLER){if(failure)failure->check=18;return false;}
    }
    bool Observe(const Contract& expected,Contract& current)const noexcept {
        __try {
            if(!expected.valid() || !Live(expected.owner) || !Live(expected.world)
                || (expected.source.valid() && !Live(expected.source)))return false;
            current=expected;current.edges={};
            current.tick_table=Read<std::uintptr_t>(expected.tick);
            current.owner_table=Read<std::uintptr_t>(expected.owner.object);
            if(current.tick_table!=base_+0x3865f98 || current.owner_table!=base_+0x38829c0
                || Read<std::uintptr_t>(expected.tick,0x50)!=expected.owner.object)return false;
            current.consumer=Read<std::uintptr_t>(current.owner_table,0x300);
            current.level=Read<std::uintptr_t>(expected.tick,0x48);
            const auto count=Read<int>(expected.tick,0x28),capacity=Read<int>(expected.tick,0x2c);
            const auto data=Read<std::uintptr_t>(expected.tick,0x20);
            if(count<0 || count>8 || capacity<count || capacity>1024 || (count && !data))return false;
            current.count=static_cast<unsigned>(count);
            for(int i=0;i<count;++i)current.edges[i]=Read<Protocol::Edge>(data,i*16);
            return current.valid();
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool ReadTask(const Identity& owner,const Identity& world,Protocol::Task& task)const noexcept {
        __try {
            if(GetCurrentThreadId()!=thread_ || !Live(owner) || !Live(world))return false;
            task={};task.tick=owner.object+0x110;task.task=Read<std::uintptr_t>(task.tick,0x18);
            if(!task.task || Read<std::uintptr_t>(task.task,0x10)!=task.tick)return false;
            task.table=Read<std::uintptr_t>(task.task);if(task.table!=base_+0x39cd9c0)return false;
            task.event=Read<std::uintptr_t>(task.task,0x40);if(!task.event)return false;
            task.world=Read<std::uintptr_t>(task.task,0x28);
            task.epoch=Read<std::uint64_t>(base_+0x4197170);
            task.admitted=Read<int>(task.tick,0x10);task.queued=Read<int>(task.tick,0x14);
            task.dependencies=static_cast<int>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(task.task+0xc),0,0));
            task.event_head=static_cast<std::uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(task.event+8),0,0));
            task.os_thread=GetCurrentThreadId();task.native_thread=Read<unsigned>(guard_.named_thread(),0x10);
            task.desired_thread=Read<unsigned>(task.task,8);task.payload_thread=Read<unsigned>(task.task,0x24);
            task.flags=Read<unsigned char>(task.tick,0xc);task.enabled=Read<unsigned char>(task.tick,0xd);
            task.constructed=Read<unsigned char>(task.task,0x38);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    const Contract* Find(std::uintptr_t tick)const noexcept {
        for(std::size_t i=0;i<count_;++i)if(contracts_[i].tick==tick)return &contracts_[i];
        return nullptr;
    }
    bool Selected(std::uintptr_t object)const noexcept {
        if(!object)return false;
        for(std::size_t i=0;i<count_;++i)
            if(object==contracts_[i].owner.object || object==contracts_[i].world.object)return true;
        for(const auto& root:roots_)
            if(object==root.component.object || object==root.manager.object || object==root.chara.object
                || object==root.scene.object || object==root.world.object)return true;
        return false;
    }
    bool Before(unsigned site,void* a,void* b,void* c)noexcept {
        const auto p=reinterpret_cast<std::uintptr_t>(a),q=reinterpret_cast<std::uintptr_t>(b);
        if(site==24) {
            const auto named=guard_.named_thread();
            if((p!=named+0x38 && p!=named+0x1c8) || (GetCurrentThreadId()==thread_ && pop_.load()))return true;
            QueueFailure(p);
            return false;
        }
        if(site==11 || site==12 || site==15) {
            const auto* expected=Find(p);
            if(!expected)return !Selected(p>=0x110?p-0x110:0);
            if(site!=12 || GetCurrentThreadId()!=thread_ || q!=expected->source.object
                || reinterpret_cast<std::uintptr_t>(c)!=q+0x110)return false;
            for(const auto& root:roots_)if(!RootStable(root))return false;
            const auto* active=tasks_->executing_consumer_task();
            Contract current{};Protocol::Task mesh{},source{};
            return active && Observe(*expected,current) && ReadTask(expected->owner,expected->world,mesh)
                && ReadTask(expected->source,expected->world,source)
                && protocol_.BeforeRemove(*expected,current,mesh,source,reinterpret_cast<std::uintptr_t>(active->task),base_+0x39cd9c0,thread_);
        }
        if(site==6)return !Selected(p) && !Selected(q);
        if(site==13 || site==14 || site==16 || site==22)return true;
        return !Selected(p);
    }
    bool After(unsigned site,void* a)noexcept {
        const auto* expected=site==12?Find(reinterpret_cast<std::uintptr_t>(a)):nullptr;
        if(!expected)return true;
        Contract current{};Protocol::Task mesh{},source{};
        return Observe(*expected,current) && ReadTask(expected->owner,expected->world,mesh)
            && ReadTask(expected->source,expected->world,source) && protocol_.AfterRemove(current,mesh,source);
    }
    Sc6ReplayTaskGroup::ConsumerAdmission Admit(Sc6ReplayTaskGroup::ConsumerTask& task)noexcept {
        using Admission=Sc6ReplayTaskGroup::ConsumerAdmission;
        for(const auto& root:roots_)if(reinterpret_cast<std::uintptr_t>(task.function)==root.component.object+0x110) {
            RootFailure detail{};
            const unsigned reason=!armed_?1:GetCurrentThreadId()!=thread_?2:!RootStable(root,&detail)?3:0;
            if(!reason)return Admission::Forward;
            task.owner=root.component.object;task.owner_index=root.component.index;task.owner_generation=root.component.serial;
            ReplayConsumerFailure::Record r{};r.site=ReplayConsumerFailure::Site::PrerequisiteAdmission;r.reason=reason;
            r.task=reinterpret_cast<std::uintptr_t>(task.task);r.function=reinterpret_cast<std::uintptr_t>(task.function);
            r.owner=task.owner;r.owner_index=task.owner_index;r.owner_generation=task.owner_generation;
            r.original_thread=thread_;r.native_thread=task.native_thread;r.protocol_phase=static_cast<unsigned>(protocol_.phase());
            r.operands[0]=detail.check;r.operands[1]=detail.expected;r.operands[2]=detail.actual;
            ReplayConsumerFailure::Write(r);
            return Admission::Reject;
        }
        const auto* expected=Find(reinterpret_cast<std::uintptr_t>(task.function));
        if(!expected)return Admission::Forward;
        Contract current{};Protocol::Task native{};
        const unsigned reason=!armed_?4:!Observe(*expected,current)?5:!ReadTask(expected->owner,expected->world,native)?6:
            native.task!=reinterpret_cast<std::uintptr_t>(task.task)?7:0;
        if(reason){Failure(ReplayConsumerFailure::Site::PrerequisiteAdmission,reason,task.task,task.function,nullptr,expected,&native,&current);return Admission::Reject;}
        const auto admitted=protocol_.BeforeDispatch(*expected,current,native,base_+0x39cd9c0,thread_);
        if(admitted==Protocol::Admission::Stable)return Admission::Forward;
        if(admitted!=Protocol::Admission::DrainCurrentApplication){
            Failure(ReplayConsumerFailure::Site::PrerequisiteAdmission,8,task.task,task.function,nullptr,expected,&native,&current);return Admission::Reject;}
        task.owner=expected->owner.object;task.owner_index=expected->owner.index;task.owner_generation=expected->owner.serial;
        task.application_epoch=native.epoch;task.contract=expected->policy.digest;
        return Admission::ForwardObserved;
    }
    NativeReplayTraceTaskGuard guard_;
    std::array<Contract,128> contracts_{};
    std::array<NativeReplayTraceTaskGuard::Binding,2> roots_{};
    std::size_t count_{};
    std::uintptr_t base_{};DWORD thread_{};
    const Sc6ReplayTaskGroup* tasks_{};
    std::atomic<bool> pop_{};
    Protocol protocol_{};
    bool armed_{};
};
}
