#pragma once
#include "Sc6ReplayObjectLease.hpp"
#include "ReplayVfxHandlerStorage.hpp"
#include "Sc6ReplayObjectVisit.hpp"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>
#include <vector>
#include <cstring>
#include <algorithm>
#include <memory>

namespace Horse::Deterministic {
// Native semantic VFX handler ownership, not render history. 1403D4D00
// appends slot IDs to +400's per-effect arrays and destroys the oldest at the
// native limit. Rewinding the manager without these arrays aliases old IDs.
class Sc6ReplayVfxHandlerState final {
    using Object=RC::Unreal::UObject;
    template<class T> static T Read(const void* p,std::size_t offset=0) {
        T v{};std::memcpy(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));return v;
    }
    static bool Copy(void* to,const void* from,std::size_t n) noexcept {
        __try {if(n) std::memcpy(to,from,n);return true;} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Equal(const void* a,const void* b,std::size_t n) noexcept {
        __try {return !n || !std::memcmp(a,b,n);} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Writable(void* p,std::size_t n) noexcept {
        auto a=reinterpret_cast<std::uintptr_t>(p);
        if(!a || n>UINTPTR_MAX-a) return false;const auto end=a+n;
        while(a<end) {
            MEMORY_BASIC_INFORMATION r{};
            if(!VirtualQuery(reinterpret_cast<void*>(a),&r,sizeof(r)) || r.State!=MEM_COMMIT
                || (r.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                || !(r.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
            const auto next=reinterpret_cast<std::uintptr_t>(r.BaseAddress)+r.RegionSize;
            if(next<=a) return false;a=next;
        }
        return true;
    }
    static Status Fail(const char* check) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] VFX handler restore rejected check={}\n"),RC::to_generic_string(check));
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    using Graph=ReplayVfxHandlerStorage::Graph;
    Graph values_,bindings_;
    Object* owner_{};void* battle_{};std::uintptr_t base_{};DWORD thread_{};
    Sc6ReplayObjectLease lease_;
    bool captured_{};
public:
    class Prepared;
    std::size_t owned_bytes() const {return sizeof(*this)-sizeof(values_)-sizeof(bindings_)+values_.owned_bytes()+bindings_.owned_bytes()+lease_.owned_bytes();}
    Status ValidateBindings() const {
        if(!captured_ || thread_!=GetCurrentThreadId() || !lease_.Validate().ok() || !bindings_.Matches())return Fail("owner_or_authored_binding");
        return Status::success();
    }
    Status ValidateValues() const {auto s=ValidateBindings();return s.ok() && !values_.Matches()?Fail("values"):s;}
    Status Capture(std::uintptr_t base,void* battle,std::size_t budget) {
        if(captured_ || owner_ || sizeof(*this)>budget)return Fail("capture_admission");
        base_=base;battle_=battle;thread_=GetCurrentThreadId();
        std::size_t found=0;
        if(!VisitReplayObjectsOfClass(L"LuxBattleVFxEventHandler",[&](Object* object) {
            if(Read<void*>(object,0x98)!=battle)return true;
            if(++found!=1)return false;owner_=object;return true;
        }) || found!=1 || Read<std::uintptr_t>(owner_)!=base+0x326b8d8)return Fail("handler_identity");
        std::array<void*,5> owners{owner_,battle};std::size_t count=2;
        for(auto offset:{0x388u,0x390u,0x398u})if(auto* p=Read<void*>(owner_,offset))owners[count++]=p;
        constexpr auto discovery_scratch=sizeof(owners)+32768;
        const auto reserve=sizeof(*this)+discovery_scratch;if(reserve>budget)return Fail("budget");
        bindings_.budget=budget-reserve+sizeof(bindings_);
        if(bindings_.Add(owner_,0x28)<0 || bindings_.Add(reinterpret_cast<std::byte*>(owner_)+0x98,8)<0
            || bindings_.Add(reinterpret_cast<std::byte*>(owner_)+0x388,0x68)<0
            || !bindings_.Map(2,0x18,false,4096)) {
            const auto* h=reinterpret_cast<const std::byte*>(owner_)+0x3a0;
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] VFX handler map admission code={} nodes={} count={} capacity={} bits={} max_bits={} free={} hash={} bit_heap={} hash_heap={}\n"),
                bindings_.rejection,bindings_.count,Read<int>(h,8),Read<int>(h,12),Read<int>(h,0x28),Read<int>(h,0x2c),Read<int>(h,0x34),Read<int>(h,0x48),Read<void*>(h,0x20)!=nullptr,Read<void*>(h,0x40)!=nullptr);
            return Fail("authored_map");
        }
        // 1403A5420 copies the 12-byte key and nine flag bytes at entry+C.
        // The insertion argument points to authored data; the retained entry
        // does NOT contain that pointer. Exact map bytes are sufficient here.
        if(bindings_.Add(reinterpret_cast<std::byte*>(owner_)+0x460,8)<0
            || bindings_.Add(reinterpret_cast<void*>(base+0x406e1f4),8)<0)return Fail("configuration_binding");
        if(owned_bytes()>budget)return Fail("budget");
        auto s=lease_.Acquire(base,{owners.data(),count},budget-owned_bytes());if(!s.ok())return s;
        if(owned_bytes()>budget || discovery_scratch>budget-owned_bytes())return Fail("budget");values_.budget=budget-owned_bytes()-discovery_scratch+sizeof(values_);
        if(!values_.Runtime(reinterpret_cast<std::byte*>(owner_)+0x3f0))return Fail("runtime_containers");
        if(owned_bytes()>budget)return Fail("budget");captured_=true;
        return ValidateValues();
    }
    Status Prepare(const Sc6ReplayVfxHandlerState& b,std::size_t budget,Prepared& out) const;
};

class Sc6ReplayVfxHandlerState::Prepared final {
    friend class Sc6ReplayVfxHandlerState;
    const Sc6ReplayVfxHandlerState* a_{};const Sc6ReplayVfxHandlerState* b_{};
    std::array<void*,Graph::limit> installed_{};
    std::array<std::byte,0x88> root_{};
    std::size_t bytes_{};bool published_{},ready_{};
    // During execution the native handler owns A's evolving allocations.
    // installed_ is deliberately discarded; it may contain freed addresses
    // after the first append. B's displaced allocations remain private.
    std::unique_ptr<Graph> executed_;
    std::size_t execution_budget_{};
    bool executing_{},settled_{},execution_undo_started_{};
    void ReleasePrivate() {
        if(!a_ || published_)return;
        if(executing_) {
            // Only the graph read at the quiescent execution boundary may be
            // freed. An unsettled or failed read retains all native ownership.
            if(!settled_)return;
            for(std::size_t i=executed_->count;i-->1;)
                reinterpret_cast<void(*)(void*)>(a_->base_+0xd46a00)(executed_->nodes[i].address);
            executed_.reset();execution_budget_=0;executing_=settled_=false;
        } else for(std::size_t i=1;i<a_->values_.count;++i)if(installed_[i]) {
                reinterpret_cast<void(*)(void*)>(a_->base_+0xd46a00)(installed_[i]);installed_[i]=nullptr;
            }
        ready_=false;
    }
public:
    Prepared()=default;Prepared(const Prepared&)=delete;
    ~Prepared(){ReleasePrivate();} // Published ownership is retained on failure.
    std::size_t owned_bytes() const {return sizeof(*this)+bytes_+execution_budget_;}
    Status ValidatePublished() const {
        if(!ready_ || !published_ || !a_->ValidateBindings().ok() || !b_->ValidateBindings().ok())return Fail("published_owner");
        if(executing_) {
            if(execution_undo_started_ || !settled_ || !executed_->Matches() || !executed_->AllocationsDisjoint(b_->values_))return Fail("execution_image");
        } else {
            auto addresses=installed_;addresses[0]=a_->values_.nodes[0].address;
            if(!a_->values_.Matches(&addresses))return Fail("published_image");
        }
        if(!b_->values_.AllocatedValuesMatch())return Fail("private_B");
        return Status::success();
    }
    // Enclosing host must own all other participants and native completion
    // admission before calling this. This method alone does not permit Resume.
    Status BeginExecution(std::size_t scratch_budget) {
        if(executing_)return Fail("execution_transition");
        auto s=ValidatePublished();if(!s.ok())return s;
        // Reserve copied graph bytes and the evolving native allocation graph.
        // Fixed graph metadata also covers native allocator rounding.
        if(scratch_budget<2*sizeof(Graph))return Status::failure(FailureCode::CapacityExceeded);
        auto scratch=std::make_unique<Graph>();scratch->budget=scratch_budget/2;
        executed_=std::move(scratch);execution_budget_=scratch_budget;
        installed_.fill(nullptr);executing_=true;return Status::success();
    }
    // Called only after application/render producers are quiescent. Observe
    // current C, not expected A, so native growth and changed inputs survive.
    // A rejected read leaves B retained and does not authorize Undo or Commit.
    Status SettleExecution() {
        if(!executing_ || settled_ || !published_ || !ready_ || !a_->ValidateBindings().ok()
            || !b_->ValidateBindings().ok() || !b_->values_.AllocatedValuesMatch())return Fail("execution_settle_binding");
        if(executed_->count || !executed_->Runtime(a_->values_.nodes[0].address)
            || !executed_->Matches() || !executed_->AllocationsDisjoint(b_->values_))return Fail("execution_settle_graph");
        settled_=true;return Status::success();
    }
    Status ReopenExecutionForUndo() {
        if(!executing_ || !published_ || execution_undo_started_)return Fail("execution_reopen_phase");
        if(settled_) {auto s=ValidatePublished();if(!s.ok())return s;}
        else if(!a_->ValidateBindings().ok() || !b_->ValidateBindings().ok()
            || !b_->values_.AllocatedValuesMatch())return Fail("execution_reopen_private_B");
        // These are copied bytes, not native allocations. Keep B's graph and
        // its lifetime leases; also discard scratch from a partial failed read.
        for(auto& node:executed_->nodes)node={};
        executed_->count=0;executed_->rejection=0;settled_=false;
        return Status::success();
    }
    Status Publish() {
        if(!ready_ || published_ || !a_->ValidateBindings().ok() || !b_->ValidateValues().ok()
            || !Writable(a_->values_.nodes[0].address,root_.size()))return Fail("publication_preflight");
        published_=true;
        if(!Copy(a_->values_.nodes[0].address,root_.data(),root_.size()))return Fail("publication_write");
        return ValidatePublished();
    }
    Status Undo() {
        if(!published_){ReleasePrivate();return Status::success();}
        if(!a_->ValidateBindings().ok() || !b_->ValidateBindings().ok())return Fail("undo_binding");
        if(executing_ && !execution_undo_started_ && !ValidatePublished().ok())return Fail("undo_execution_unsettled");
        if(!b_->values_.AllocatedValuesMatch())return Fail("undo_private_B");
        const auto& root=b_->values_.nodes[0];
        if(!Writable(root.address,root.bytes.size()))return Fail("undo_write");
        if(executing_)execution_undo_started_=true;
        if(!Copy(root.address,root.bytes.data(),root.bytes.size()) || !b_->ValidateValues().ok())return Fail("undo_write");
        published_=false;ReleasePrivate();return Status::success();
    }
    Status Commit() {
        auto s=ValidatePublished();if(!s.ok())return s;
        // Every active nested allocation is independent; publication transfers
        // all A allocations together, while complete B survives until this point.
        for(std::size_t i=b_->values_.count;i-->1;)
            reinterpret_cast<void(*)(void*)>(a_->base_+0xd46a00)(b_->values_.nodes[i].address);
        installed_.fill(nullptr);published_=ready_=false;
        // Commit relinquishes current C to native ownership; only scratch is
        // released here. Undo instead retires C after verified B publication.
        executed_.reset();execution_budget_=0;executing_=settled_=false;
        return Status::success();
    }
};
inline Status Sc6ReplayVfxHandlerState::Prepare(const Sc6ReplayVfxHandlerState& b,std::size_t budget,Prepared& out) const {
    if(out.a_ || owner_!=b.owner_ || battle_!=b.battle_ || base_!=b.base_ || !ValidateBindings().ok() || !b.ValidateValues().ok())return Fail("prepare_binding");
    if(sizeof(out)>budget)return Status::failure(FailureCode::CapacityExceeded);
    std::size_t bytes=0;
    for(std::size_t i=1;i<values_.count;++i)bytes+=values_.nodes[i].bytes.size();
    for(std::size_t i=1;i<b.values_.count;++i)bytes+=b.values_.nodes[i].bytes.size();
    // Native allocator rounding, root scratch, and metadata are conservatively charged.
    bytes+=(values_.count+b.values_.count)*32;
    if(bytes>budget-sizeof(out))return Status::failure(FailureCode::CapacityExceeded);
    out.a_=this;out.b_=&b;out.bytes_=bytes;out.installed_[0]=out.root_.data();
    std::memcpy(out.root_.data(),values_.nodes[0].bytes.data(),out.root_.size());
    for(std::size_t i=1;i<values_.count;++i) {
        const auto& node=values_.nodes[i];
        auto* p=reinterpret_cast<void*(*)(std::size_t)>(base_+0x4a61c0)(node.bytes.size());
        if(!p){out.ReleasePrivate();return Status::failure(FailureCode::CapacityExceeded);}
        out.installed_[i]=p;std::memcpy(p,node.bytes.data(),node.bytes.size());
    }
    for(std::size_t i=1;i<values_.count;++i) {
        const auto& node=values_.nodes[i];
        if(node.parent<0 || std::size_t(node.parent)>=values_.count){out.ReleasePrivate();return Fail("relocation");}
        std::memcpy(static_cast<std::byte*>(out.installed_[node.parent])+node.offset,&out.installed_[i],8);
    }
    if(!values_.Matches(&out.installed_)){out.ReleasePrivate();return Fail("private_A");}
    out.ready_=true;return Status::success();
}
}
