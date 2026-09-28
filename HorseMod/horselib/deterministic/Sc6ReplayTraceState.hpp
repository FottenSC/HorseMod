#pragma once
#include "Sc6ReplayObjectLease.hpp"
#include "ReplayTraceWeakReference.hpp"
#include "ReplayGpuCompletion.hpp"
#include "Sc6ReplayObjectVisit.hpp"
#include "Sc6ReplayTraceChildFactory.hpp"
#include "Sc6ReplayVfxState.hpp"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UFunction.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <vector>
#include <array>
#include <algorithm>
#include <cstring>
#include <source_location>
#include <span>

namespace Horse::Deterministic {
// Retained native trace source/animation rows at a completed application.
// Historical strong pool ownership remains fixed. Native C-only child lifetimes
// have explicit disposal; weak/child arrays and manager maps use private native
// storage with complete displaced-B ownership until commit.
// It never acquires a native strong reference, resurrects a dead controller,
// overrides native lifecycle functions, or treats trace state as purely cosmetic.
class Sc6ReplayTraceState final {
    using Object=RC::Unreal::UObject;
    struct Id {Object* object{};int index{},serial{};friend bool operator==(const Id&,const Id&)=default;};
    struct Array {std::byte* data{};int count{},capacity{};};
    struct Ref {std::byte* state{};std::byte* controller{};};
    struct Image {std::byte* address{};std::vector<std::byte> bytes;std::uint32_t mutable_bits{};bool projected_child{};};
    struct DynamicImage {
        enum class Kind : std::uint8_t { Map, WeakReferences, ChildStrongReferences };
        std::byte* address{};std::array<std::byte,0x50> header{};
        std::vector<std::byte> bytes;Kind kind{};
        bool weak() const {return kind==Kind::WeakReferences;}
        bool array() const {return kind!=Kind::Map;}
        std::size_t header_size() const {return array()?16:0x50;}
        std::size_t stride() const {return array()?16:0x44;}
    };
    struct Root {Id component,chara,manager,scene;};
    struct State {Ref ref;Id actor,attachment,mesh,animation;};
    std::vector<Root> roots_;
    std::vector<State> states_;
    std::vector<Ref> expired_;
    std::size_t expired_bytes_{};
    // Exact vector capacities, updated only when Add publishes a new image.
    // Audit once after capture instead of rescanning every payload per insert.
    std::size_t payload_bytes_{};
    bool OwnsExpired(const Ref& ref) const noexcept {
        return std::any_of(expired_.begin(),expired_.end(),[&](const auto& row) {
            return row.state==ref.state && row.controller==ref.controller;
        }) && reinterpret_cast<const ReplayTraceWeakController*>(ref.controller)->Expired(base_+0x3362590);
    }
    static void DeleteExpiredController(void* controller) noexcept {
        const auto table=*reinterpret_cast<std::uintptr_t*>(controller);
        reinterpret_cast<void(*)(void*,unsigned)>(*reinterpret_cast<std::uintptr_t*>(table+8))(controller,1);
    }
    bool ReleaseExpired() noexcept {
        if(!expired_.empty() && thread_!=GetCurrentThreadId()) return false;
        for(const auto& ref:expired_) if(!OwnsExpired(ref)) return false;
        while(!expired_.empty()) {
            auto* controller=reinterpret_cast<ReplayTraceWeakController*>(expired_.back().controller);
            if(!controller->ReleaseExpired(base_+0x3362590,&DeleteExpiredController)) return false;
            expired_.pop_back();
        }
        expired_bytes_=0;return true;
    }
    bool RetainExpired(const Ref& ref) {
        if(OwnsExpired(ref)) return true;
        if(expired_.size()>=1536) return false;
        // Native1408D1C00 allocates controller16 + stateF8. The weak owner
        // retains that allocation, not the destroyed state's nested resources.
        const auto bytes=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0)(0x108,0);
        if(bytes<0x108 || owned_bytes()>budget_ || bytes+sizeof(Ref)>budget_-owned_bytes()) return false;
        expired_.reserve(expired_.size()+1);
        if(owned_bytes()>budget_ || bytes>budget_-owned_bytes()) return false;
        expired_.push_back(ref);
        if(!reinterpret_cast<ReplayTraceWeakController*>(ref.controller)->RetainExpired(base_+0x3362590)) {
            expired_.pop_back();return false;
        }
        expired_bytes_+=bytes;return true;
    }
    std::vector<Image> values_,bindings_;
    std::array<DynamicImage,10> dynamic_{};
    std::size_t dynamic_count_{};
    Sc6ReplayObjectLease lease_;
    std::uintptr_t base_{};void* world_{};DWORD thread_{};
    std::size_t budget_{};
    bool captured_{};
    bool validation_only_{}; // Common-owner view; never a complete publishable A.
    const Sc6ReplayTraceState* survivor_source_{};
    template<class T> static T& At(void* p,std::size_t offset) {return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
    static Status Fail(const char* check,std::source_location where=std::source_location::current()) {
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace transaction rejected check={} line={}\n"),RC::to_generic_string(check),where.line());
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    Id Identify(Object* p) const {
        if(!p) return {};
        auto* item=RC::Unreal::FUObjectArray::IndexToObject(p->GetInternalIndex());
        if(!item || item->GetUObject()!=p || !item->IsValid(false)) return {};
        std::array<int,2> weak{};
        reinterpret_cast<void(*)(void*,const void*)>(base_+0xf7bad0)(weak.data(),p);
        return {p,weak[0],weak[1]};
    }
    static bool Live(const Id& id) {
        if(!id.object) return true;
        auto* item=RC::Unreal::FUObjectArray::IndexToObject(id.index);
        return item && item->GetUObject()==id.object && item->IsValid(false) && item->GetSerialNumber()==id.serial;
    }
    static bool Equal(const Image& image) noexcept {
        __try {
            if(image.mutable_bits) {
                std::uint32_t live{},saved{};
                std::memcpy(&live,image.address,image.bytes.size());
                std::memcpy(&saved,image.bytes.data(),image.bytes.size());
                return ((live^saved)&~image.mutable_bits)==0;
            }
            return std::memcmp(image.address,image.bytes.data(),image.bytes.size())==0;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool Writable(const Image& image) noexcept {
        auto address=reinterpret_cast<std::uintptr_t>(image.address);
        if(image.bytes.size()>UINTPTR_MAX-address) return false;
        const auto end=address+image.bytes.size();
        while(address<end) {
            MEMORY_BASIC_INFORMATION region{};
            if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region)) || region.State!=MEM_COMMIT
                || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                || !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
            const auto next=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
            if(next<=address) return false;address=next;
        }
        return true;
    }
    static bool Write(const Image& image) noexcept {
        __try {std::memcpy(image.address,image.bytes.data(),image.bytes.size());return true;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool Add(std::vector<Image>& to,void* p,std::size_t size) {
        if(!size) return true;
        if(!p) return false;
        // Shared animation destinations can appear through several states.
        for(const auto& old:to) if(old.address==p) return old.bytes.size()==size;
        if(size>budget_ || owned_bytes()>budget_-size) return false;
        if(to.size()==to.capacity()) {
            const auto next=(std::max)(std::size_t{1},to.capacity()*2);
            // Existing metadata and new metadata coexist during reallocation.
            if(next> (budget_-owned_bytes()-size)/sizeof(Image)) return false;
            to.reserve(next);
        }
        Image image;image.address=static_cast<std::byte*>(p);
        image.bytes.assign(image.address,image.address+size);
        const auto charge=image.bytes.capacity();
        if(charge>budget_ || owned_bytes()>budget_-charge) return false;
        to.push_back(std::move(image));payload_bytes_+=charge;
        return owned_bytes()<=budget_;
    }
    bool ArrayImage(void* header,int limit,std::size_t stride) {
        const auto a=At<Array>(header,0);
        if(a.count<0 || a.count>a.capacity || a.capacity>limit || a.capacity<0 || (a.capacity && !a.data)) return false;
        // Refuse allocation changes instead of writing through stale storage.
        // Count and full pointer-free backing are semantic state; capacity and
        // data address are process-local bindings, never historical writes.
        return Add(bindings_,header,8) && Add(bindings_,static_cast<std::byte*>(header)+12,4)
            && Add(values_,static_cast<std::byte*>(header)+8,4)
            && Add(values_,a.data,std::size_t(a.capacity)*stride);
    }
    bool SceneTransform(Object* p,bool trace_attachment=true) {
        if(!p) return true;
        if(trace_attachment) {
            //1408D3F20 invokes attachment+238 after a particle OR ground
            // manager completion. Empty delegates do not own an override's
            // writes. Admit the verified native attachment and its complete
            // Deactivate -> ShouldActivate/SetComponentTickEnabled chain.
            const auto table=At<std::uintptr_t>(p,0);
            if(table!=base_+0x3360370 || !Add(bindings_,p,sizeof(table)))return false;
            for(const auto [slot,target]:{std::pair{0x238u,0x1d41870u},
                {0x278u,0x1d7fbb0u},{0x308u,0x1d5dbe0u}}) {
                auto* entry=reinterpret_cast<std::byte*>(table)+slot;
                if(At<std::uintptr_t>(entry,0)!=base_+target || !Add(bindings_,entry,8))return false;
            }
        }
        // Same native scene-transform overlay used by the particle participant.
        // Bounds/transform/relative values persist into dependent VFX queries.
        for(const auto s:{std::pair{0x24cu,28u},{0x270u,48u},{0x2a0u,28u},
            {0x2c0u,24u},{0x2e0u,28u},{0x300u,12u}})
            if(!Add(values_,reinterpret_cast<std::byte*>(p)+s.first,s.second)) return false;
        // Native Activate/Deactivate owns bit18; RegisterComponentTickFunctions
        // owns bit29. All other component flags and attachment bindings stay
        // immutable. No lifecycle delegate may run during direct installation.
        if(At<int>(p,0x1b0) || At<int>(p,0x1c0)) return false;
        return FlagImage(reinterpret_cast<std::byte*>(p)+0x188,4,0x20040000)
            // 141DB0600 sets bit0 when ComponentToWorld becomes current and
            // consumes the same bit before parent recursion. Retain it with
            // the transform; absolute-channel bits1..3 remain binding guards.
            && FlagImage(reinterpret_cast<std::byte*>(p)+0x240,4,0x11)
            && Add(bindings_,reinterpret_cast<std::byte*>(p)+0x1a8,32)
            && Add(bindings_,reinterpret_cast<std::byte*>(p)+0x1d0,16);
    }
    bool FlagImage(std::byte* p,std::size_t bytes,std::uint32_t mutable_bits) {
        if(!Add(bindings_,p,bytes) || !Add(values_,p,bytes)) return false;
        for(auto& b:bindings_) if(b.address==p) {b.mutable_bits=mutable_bits;return true;}
        return false;
    }
    bool CaptureDynamic(std::byte* address,DynamicImage::Kind kind) {
        if(dynamic_count_==dynamic_.size()) return false;
        auto& image=dynamic_[dynamic_count_++];image.address=address;image.kind=kind;
        std::memcpy(image.header.data(),address,image.header_size());
        const auto array=At<Array>(image.header.data(),0);
        if(array.count<0 || array.capacity<array.count || array.capacity>(image.array()?256:128)
            || (array.capacity && !array.data) || (!array.capacity && array.data)) return false;
        
        if(!image.array() && (At<void*>(address,0x20) || At<void*>(address,0x40))) return false;
        const auto bytes=std::size_t(array.capacity)*image.stride();
        if(bytes>budget_ || owned_bytes()>budget_-bytes) return false;
        if(bytes) image.bytes.assign(array.data,array.data+bytes);
        return owned_bytes()<=budget_;
    }
    static bool DynamicEqual(const DynamicImage& image) noexcept {
        __try {
            // Only the native backing address is relocatable. Counts,
            // capacity, sparse flags/chains and actual entries remain exact.
            if(std::memcmp(image.address+8,image.header.data()+8,image.header_size()-8)) return false;
            const auto live=At<Array>(image.address,0);
            if(bool(live.data)!=(live.capacity!=0)) return false;
            
            const auto bytes=image.array()?std::size_t(live.count)*16:image.bytes.size();
            return !bytes || std::memcmp(live.data,image.bytes.data(),bytes)==0;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
public:
    struct RootConsumer {
        struct Identity {std::uintptr_t object{};std::int32_t index{},serial{};};
        Identity component,manager,chara,scene;
    };
    // Copy retained indexed identities only. The consumer guard validates
    // them under its native exclusion before reading any live owner fields.
    bool ReadRootConsumers(std::array<RootConsumer,2>& output) const noexcept {
        if(!captured_ || thread_!=GetCurrentThreadId() || roots_.size()!=output.size())return false;
        const auto identity=[](const Id& id) {
            return RootConsumer::Identity{reinterpret_cast<std::uintptr_t>(id.object),id.index,id.serial};
        };
        for(std::size_t i=0;i<output.size();++i) {
            const auto& root=roots_[i];
            output[i]={identity(root.component),identity(root.manager),identity(root.chara),identity(root.scene)};
        }
        return true;
    }
    bool OwnsCompletionReceiver(Object* object,const std::array<std::int32_t,2>& weak) const {
        if(!captured_ || thread_!=GetCurrentThreadId() || !object) return false;
        return std::any_of(roots_.begin(),roots_.end(),[&](const auto& root) {
            return root.component.object==object && root.component.index==weak[0]
                && root.component.serial==weak[1] && Live(root.component)
                // Indexed liveness alone does not prove retained ownership.
                // Require this capture's registered GC owner and exact member;
                // this does not exclude explicit destruction or native writers.
                && lease_.ValidateObject(object).ok();
        });
    }
    class Prepared;
    struct FreshChild;
    Status PrepareStorage(const Sc6ReplayTraceState& current,std::size_t budget,Prepared& output,
        const Sc6ReplayVfxState::ParticleBirthSet* private_child_owners=nullptr,
        const Sc6ReplayTraceState* survivors=nullptr,std::span<FreshChild* const> fresh={}) const;
    Status VisitPrimaryTicks(void* context,bool(*visitor)(void*,std::uintptr_t)) const {
        return visit_primary_ticks_(*this,context,visitor);
    }
private:
    // The core is linked separately into runtime and observer DLLs. Lease
    // callback identity belongs to the creating module; validate there rather
    // than comparing against the observer's separate static callback table.
    // The captured native lease retains that module and sizeof(*this) charges
    // this immutable dispatch pointer. No ownership predicate is relaxed.
    static Status VisitPrimaryTicksInOwnerModule(const Sc6ReplayTraceState& self,
        void* context,bool(*visitor)(void*,std::uintptr_t)) {
        const auto status=self.ValidateBindings();if(!status.ok()) return status;
        if(!visitor)return Fail("primary_tick_visitor");
        for(std::size_t i=0;i<self.states_.size();++i) {
            const auto mesh=self.states_[i].mesh.object;
            bool seen{};for(std::size_t j=0;j<i;++j) seen|=self.states_[j].mesh.object==mesh;
            if(!seen && !visitor(context,reinterpret_cast<std::uintptr_t>(mesh))) return Fail("primary_tick_owner");
        }
        return Status::success();
    }
    using PrimaryTickVisit=Status(*)(const Sc6ReplayTraceState&,void*,bool(*)(void*,std::uintptr_t));
    const PrimaryTickVisit visit_primary_ticks_=&VisitPrimaryTicksInOwnerModule;
public:
    // Capture-time GC ownership proof for render-only companion routing. The
    // trace image owns reconstruction of this exact mesh identity; a later
    // explicit death still invalidates its original binding normally.
    bool RetainsCapturedMesh(const void* object) const {
        if(!captured_ || thread_!=GetCurrentThreadId() || !object) return false;
        for(const auto& state:states_)
            if(state.mesh.object==object)
                return Live(state.mesh) && lease_.ValidateObject(object).ok();
        return false;
    }
    // Failure-only inventory of existing A/B images. No additional capture,
    // native reads, or expected-state publication is performed here.
    void DescribeOwnershipDelta(const Sc6ReplayTraceState& current) const {
        // Failure-only partition from retained bytes. Historical native array
        // pointers are lookup keys only; never dereference their old backing.
        const auto partition=[&](const Sc6ReplayTraceState& image,const Sc6ReplayTraceState& other,bool current_side) {
            std::size_t added{},retained{},printed{};
            for(const auto& state:image.states_) {
                const bool present=std::any_of(other.states_.begin(),other.states_.end(),[&](const auto& row) {
                    return row.ref.state==state.ref.state && row.ref.controller==state.ref.controller;
                });
                if(present){++retained;continue;}++added;
                if(printed++<16)RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace owner partition current={} state={:x} controller={:x} actor={:x} mesh={:x} attachment={:x} animation={:x}\n"),
                    current_side,reinterpret_cast<std::uintptr_t>(state.ref.state),reinterpret_cast<std::uintptr_t>(state.ref.controller),
                    reinterpret_cast<std::uintptr_t>(state.actor.object),reinterpret_cast<std::uintptr_t>(state.mesh.object),
                    reinterpret_cast<std::uintptr_t>(state.attachment.object),reinterpret_cast<std::uintptr_t>(state.animation.object));
            }
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace owner partition totals current={} unique={} retained={} expired_leases={}\n"),
                current_side,added,retained,image.expired_.size());
            constexpr std::size_t offsets[]{0x418,0x428,0x3e0,0x3f0,0x400};
            printed=0;
            for(std::size_t root=0;root<image.roots_.size();++root)for(unsigned collection=0;collection<5;++collection) {
                const auto* address=reinterpret_cast<std::byte*>(image.roots_[root].component.object)+offsets[collection];
                Array header{};const std::byte* bytes{};std::size_t size{};bool found{};
                for(std::size_t i=0;i<image.dynamic_count_;++i)if(image.dynamic_[i].address==address && image.dynamic_[i].array()) {
                    std::memcpy(&header,image.dynamic_[i].header.data(),sizeof(header));
                    bytes=image.dynamic_[i].bytes.data();size=image.dynamic_[i].bytes.size();found=true;break;
                }
                if(!found)for(const auto& row:image.bindings_)if(row.address==address && row.bytes.size()==sizeof(header)) {
                    std::memcpy(&header,row.bytes.data(),sizeof(header));found=true;
                    for(const auto& backing:image.bindings_)if(backing.address==header.data) {
                        bytes=backing.bytes.data();size=backing.bytes.size();break;
                    }
                    break;
                }
                const bool complete=found && header.count>=0 && header.count<=256 && size>=std::size_t(header.count)*sizeof(Ref);
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace collection partition current={} root={} collection={} count={} copied_complete={}\n"),
                    current_side,root,collection,header.count,complete);
                if(!complete)continue;
                for(int i=0;i<header.count;++i) {
                    Ref ref{};std::memcpy(&ref,bytes+std::size_t(i)*sizeof(ref),sizeof(ref));
                    if(std::any_of(other.states_.begin(),other.states_.end(),[&](const auto& row) {
                        return row.ref.state==ref.state && row.ref.controller==ref.controller;
                    }))continue;
                    if(printed++<16)RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace collection unmatched current={} root={} collection={} index={} state={:x} controller={:x}\n"),
                        current_side,root,collection,i,reinterpret_cast<std::uintptr_t>(ref.state),reinterpret_cast<std::uintptr_t>(ref.controller));
                }
            }
        };
        partition(*this,current,false);partition(current,*this,true);
        std::size_t changed{}, missing{}, shown{}, value_storage{}, mutable_changes{};
        const auto describe=[&](std::byte* address) {
            std::pair<const char*,std::size_t> result{"backing",0};
            const auto within=[&](void* root,std::size_t size,const char* name) {
                const auto a=reinterpret_cast<std::uintptr_t>(address),b=reinterpret_cast<std::uintptr_t>(root);
                if(root && a>=b && a-b<size) result={name,a-b};
            };
            for(const auto& root:roots_) within(root.component.object,0x4b0,"component");
            for(const auto& state:states_) {
                within(state.ref.state,0xf8,"state"); within(state.actor.object,0x410,"actor");
                within(state.mesh.object,0xff0,"mesh"); within(state.attachment.object,0x480,"attachment");
                within(state.animation.object,0x400,"animation");
            }
            return result;
        };
        for(const auto& a:bindings_) {
            const auto b=std::find_if(current.bindings_.begin(),current.bindings_.end(),
                [&](const auto& row){return row.address==a.address && row.bytes.size()==a.bytes.size();});
            if(b==current.bindings_.end()) {++missing;continue;}
            if(a.bytes==b->bytes) continue;
            if(a.mutable_bits && a.mutable_bits==b->mutable_bits && a.bytes.size()<=sizeof(std::uint32_t)) {
                std::uint32_t av{},bv{};
                std::memcpy(&av,a.bytes.data(),a.bytes.size());
                std::memcpy(&bv,b->bytes.data(),b->bytes.size());
                if(!((av^bv)&~a.mutable_bits)) {++mutable_changes;continue;}
            }
            ++changed;
            if(shown++>=16) continue;
            std::size_t offset{};while(offset<a.bytes.size() && a.bytes[offset]==b->bytes[offset]) ++offset;
            std::uint64_t av{},bv{};const auto bytes=(std::min)(std::size_t{8},a.bytes.size()-offset);
            std::memcpy(&av,a.bytes.data()+offset,bytes);std::memcpy(&bv,b->bytes.data()+offset,bytes);
            const auto [kind,relative]=describe(a.address);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace ownership delta kind={} offset={:x} address={:x} bytes={} first={} A={:x} B={:x}\n"),
                RC::to_generic_string(kind),relative,reinterpret_cast<std::uintptr_t>(a.address),a.bytes.size(),offset,av,bv);
        }
        for(const auto& a:values_) if(std::none_of(current.values_.begin(),current.values_.end(),
            [&](const auto& row){return row.address==a.address && row.bytes.size()==a.bytes.size();})) ++value_storage;
        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace ownership inventory changed_bindings={} missing_bindings={} changed_value_storage={} A_states={} B_states={} admitted_mutable_changes={}\n"),
            changed,missing,value_storage,states_.size(),current.states_.size(),mutable_changes);
    }
    // Current-C, failure-only observations for the native actor-destruction
    // contract. These are not checkpoint contents or retirement admission.
    // 141C05C20 walks actor+2C0's sparse owned-component set; 141C12290
    // dispatches ReceiveDestroyed and the actor+2A0 multicast. No event runs.
#include "Sc6ReplayTraceRetirement.inl"
#include "Sc6ReplayTraceReconstruction.inl"
#include "Sc6ReplayTraceFreshChild.inl"
#include "Sc6ReplayTraceProjection.inl"
    Sc6ReplayTraceState()=default;
    ~Sc6ReplayTraceState() { if(!ReleaseExpired()) Fail("expired_weak_retirement"); }
    Sc6ReplayTraceState(const Sc6ReplayTraceState&)=delete;
    Sc6ReplayTraceState& operator=(const Sc6ReplayTraceState&)=delete;
    // Completed application only, after every prepared consumer relinquishes
    // this C image. Failure retains the lease and all copied bytes for retry.
    Status ReleaseCapture() {
        if(thread_ && thread_!=GetCurrentThreadId())return Fail("release_thread");
        auto status=lease_.Release();if(!status.ok())return status;
        if(!ReleaseExpired()) return Fail("expired_weak_retirement");
        roots_.clear();states_.clear();values_.clear();bindings_.clear();payload_bytes_=0;
        for(auto& image:dynamic_)image={};
        dynamic_count_=0;captured_=validation_only_=false;base_=0;world_=nullptr;thread_=0;budget_=0;
        survivor_source_=nullptr;
        return Status::success();
    }
    std::size_t owned_bytes() const {
        std::size_t n=sizeof(*this)+roots_.capacity()*sizeof(Root)+states_.capacity()*sizeof(State)
            +(values_.capacity()+bindings_.capacity())*sizeof(Image)+lease_.owned_bytes()
            +expired_.capacity()*sizeof(Ref)+expired_bytes_;
        n+=payload_bytes_;
        for(const auto& x:dynamic_) n+=x.bytes.capacity();
        return n;
    }
    bool ValidatePayloadAccounting() const noexcept {
        std::size_t actual{};
        for(const auto* images:{&values_,&bindings_})for(const auto& image:*images) {
            if(image.bytes.capacity()>SIZE_MAX-actual)return false;
            actual+=image.bytes.capacity();
        }
        return actual==payload_bytes_;
    }
    Status Capture(std::uintptr_t base,void* world,std::size_t budget) {
        if(captured_ || !roots_.empty()) return Fail("capture_reuse");
        base_=base;world_=world;thread_=GetCurrentThreadId();budget_=budget;
        if(budget<ReplayObjectVisitScratchBytes || owned_bytes()>budget-ReplayObjectVisitScratchBytes)
            return Fail("metadata_budget");
        std::array<Object*,2> objects{};std::size_t object_count{};
        if(!VisitReplayObjectsOfClass(L"LuxTraceComponent",[&](Object* object) {
            if(!Identify(object).object || At<void*>(object,0x1c8)!=world || At<unsigned>(object,0x498)>1) return true;
            if(object_count==objects.size()) return false;
            objects[object_count++]=object;return true;
        }) || object_count!=objects.size()) return Fail("root_coverage");
        // Two roots, five native reference arrays/root, at most256 references
        // per array (checked below). Duplicate states only reduce these bounds.
        constexpr std::size_t maximum_states=2*5*256;
        constexpr std::size_t maximum_owners=8+4*maximum_states;
        constexpr auto owner_scratch=maximum_owners*sizeof(void*)+ReplayObjectVisitScratchBytes;
        const auto initial=owner_scratch+maximum_states*sizeof(State)+2*sizeof(Root);
        if(initial>budget || owned_bytes()>budget-initial) return Fail("metadata_budget");
        std::vector<void*> owners;owners.reserve(maximum_owners);
        states_.reserve(maximum_states);roots_.reserve(2);
        budget_=budget-owner_scratch;
        for(auto* component:objects) {
            const auto component_id=Identify(component);
            if(!component_id.object || At<void*>(component,0x1c8)!=world || At<unsigned>(component,0x498)>1) continue;
            const auto chara=Identify(At<Object*>(component,0x490));
            if(!chara.object) return Fail("chara_owner");
            const auto manager=Identify(At<Object*>(chara.object,0x458));
            if(!manager.object || At<Object*>(manager.object,0x3a8)!=component
                || At<std::uintptr_t>(component,0)!=base+0x3360ca8 || roots_.size()==2) return Fail("root_binding");
            // Native1408D8C40 seeds a new trace origin from the character
            // root's ComponentToWorld translation (+280), before pose refresh.
            // Retain its exact completed-application transform, including B.
            const auto scene=Identify(At<Object*>(chara.object,0x168));
            if(!scene.object || At<Object*>(scene.object,0x190)!=chara.object
                || !Add(bindings_,reinterpret_cast<std::byte*>(chara.object)+0x168,8)
                || !Add(bindings_,reinterpret_cast<std::byte*>(scene.object)+0x190,8)
                || !SceneTransform(scene.object,false)) return Fail("chara_root_transform");
            roots_.push_back({component_id,chara,manager,scene});
            owners.insert(owners.end(),{component,chara.object,manager.object,scene.object});
            // One-shot completion is not invoked during installation. Null
            // callback and unchanged context/setup are admission.
            if(At<void*>(component,0x460)) return Fail("completion_callback");
            for(const auto s:{std::pair{0x410u,8u},{0x438u,1u},{0x43cu,16u},
                {0x458u,3u},{0x45cu,4u},{0x470u,4u},{0x478u,4u}})
                if(!Add(values_,reinterpret_cast<std::byte*>(component)+s.first,s.second)) return Fail("component_budget");
            if(!Add(bindings_,reinterpret_cast<std::byte*>(component)+0x44c,12)
                || !Add(bindings_,reinterpret_cast<std::byte*>(component)+0x474,4)
                || !Add(bindings_,reinterpret_cast<std::byte*>(component)+0x460,16)
                || !Add(bindings_,reinterpret_cast<std::byte*>(component)+0x480,0x30)) return Fail("component_setup");
            // Native 1408C8D60: 0x44 sparse records, including hash links.
            // Pointer-free sparse entries use private native backing on restore.
            auto* map=reinterpret_cast<std::byte*>(manager.object)+0x3b0;
            const auto a=At<Array>(map,0);
            if(a.count<0 || a.count>a.capacity || a.capacity>128 || a.capacity<0 || (a.capacity&&!a.data)
                || At<void*>(map,0x20) || At<void*>(map,0x40)
                || !CaptureDynamic(map,DynamicImage::Kind::Map)) return Fail("manager_map");
            constexpr std::size_t offsets[]{0x418,0x428,0x3e0,0x3f0,0x400};
            for(unsigned collection=0;collection<5;++collection) {
                auto* header=reinterpret_cast<std::byte*>(component)+offsets[collection];
                const auto refs=At<Array>(header,0);
                if(refs.count<0 || refs.count>256 || refs.capacity<refs.count || refs.capacity>256 || (refs.capacity&&!refs.data))
                    return Fail("reference_membership");
                // 1408CE180 appends strong children; 1408D8130/1408D9C20
                // remove them without necessarily releasing array capacity.
                // Empty backing owns no controllers, but its allocation still
                // needs private A storage and complete displaced B undo.
                if(collection==1) {
                    if(!CaptureDynamic(header,DynamicImage::Kind::ChildStrongReferences)) return Fail("child_storage");
                } else if(collection<2) {
                    if(!Add(bindings_,header,16) || !Add(bindings_,refs.data,std::size_t(refs.count)*16)) return Fail("reference_membership");
                } else if(!CaptureDynamic(header,DynamicImage::Kind::WeakReferences)) return Fail("reference_membership");
                for(int i=0;i<refs.count;++i) {
                    const auto ref=At<Ref>(refs.data,std::size_t(i)*16);
                    if(collection>=2 && ref.controller && ref.state==ref.controller+16
                        && reinterpret_cast<ReplayTraceWeakController*>(ref.controller)->Expired(base+0x3362590)) {
                        if(!RetainExpired(ref)) return Fail("expired_weak_ownership");
                        continue; // Only the weak controller survives; never access state.
                    }
                    if(!ref.controller || ref.state!=ref.controller+16 || At<std::uintptr_t>(ref.controller,0)!=base+0x3362590
                        || At<int>(ref.controller,8)<=0) {
                        RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] trace reference rejected collection={} index={} count={} state={:x} controller={:x} type={:x} strong={} weak={}\n"),
                            collection,i,refs.count,reinterpret_cast<std::uintptr_t>(ref.state),reinterpret_cast<std::uintptr_t>(ref.controller),
                            ref.controller?At<std::uintptr_t>(ref.controller,0)-base:0,
                            ref.controller?At<int>(ref.controller,8):0,ref.controller?At<int>(ref.controller,12):0);
                        return Fail("reference_lifetime");
                    }
                    if(std::any_of(states_.begin(),states_.end(),[&](const State& s){return s.ref.state==ref.state;})) continue;
                    State state;state.ref=ref;
                    state.actor=Identify(At<Object*>(ref.state,8));state.attachment=Identify(At<Object*>(ref.state,0xb8));
                    if(!state.actor.object || (At<void*>(ref.state,0xb8)&&!state.attachment.object)) return Fail("trace_objects");
                    state.mesh=Identify(At<Object*>(state.actor.object,0x398));
                    if(!state.mesh.object) return Fail("trace_mesh");
                    // Exact base trace actor/provider topology from141CC8120.
                    if(At<std::uintptr_t>(state.actor.object,0)!=base+0x3361660
                        || At<std::uintptr_t>(state.mesh.object,0)!=base+0x38829c0
                        || At<Object*>(state.actor.object,0x168)!=state.mesh.object
                        || At<Object*>(state.mesh.object,0x190)!=state.actor.object
                        || !Add(bindings_,reinterpret_cast<std::byte*>(state.actor.object)+0x168,8)
                        || !Add(bindings_,reinterpret_cast<std::byte*>(state.actor.object)+0x398,8)
                        || !FlagImage(reinterpret_cast<std::byte*>(state.actor.object)+0x85,1,0x80)) return Fail("actor_registration");
                    // Registered secondary ticks are already captured by the
                    // scheduler. Dormant secondary domains remain immutable
                    // admission guards until their activation is supported.
                    for(const auto offset:{0x7b0u,0xc58u,0xe08u})
                        if(At<void*>(state.mesh.object,offset+0x18)
                            || (!(At<unsigned char>(state.mesh.object,offset+0xc)&0x40)
                                && !Add(bindings_,reinterpret_cast<std::byte*>(state.mesh.object)+offset,0x58)))
                            return Fail("secondary_mesh_tick");
                    state.animation=Identify(reinterpret_cast<Object*(*)(void*)>(base+0x1d9d460)(state.mesh.object));
                    if(!state.animation.object) return Fail("trace_animation");
                    if(!Add(bindings_,reinterpret_cast<std::byte*>(state.animation.object)+0x10,8))
                        return Fail("trace_animation_class");
                    // Native render reconstruction must consume the retained
                    // mesh/animation assets, never a substituted object.
                    for(const auto range:{std::pair{0x910u,8u},{0xab0u,8u},{0x420u,4u}})
                        if(!Add(bindings_,reinterpret_cast<std::byte*>(state.mesh.object)+range.first,range.second))
                            return Fail("trace_render_binding");
                    states_.push_back(state);
                    for(auto o:{state.actor.object,state.attachment.object,state.mesh.object,state.animation.object}) if(o) owners.push_back(o);
                    for(const auto s:{std::pair{0u,2u},{0x20u,0x35u},{0x80u,8u},{0x98u,4u},
                        {0xb0u,4u},{0xc0u,9u},{0xccu,0x24u},{0xf0u,1u},{0xf4u,4u}})
                        if(!Add(values_,ref.state+s.first,s.second)) return Fail("state_budget");
                    for(const auto s:{std::pair{8u,8u},{0x58u,24u},{0xa0u,16u},{0xb8u,8u}})
                        if(!Add(bindings_,ref.state+s.first,s.second)) return Fail("state_binding");
                    if(!ArrayImage(ref.state+0x10,1024,0x70) || !ArrayImage(ref.state+0x70,1024,0x2c)
                        || !ArrayImage(ref.state+0x88,1024,0x2c)) return Fail("source_arrays");
                    if(!SceneTransform(state.mesh.object,false) || !SceneTransform(state.attachment.object)) return Fail("scene_transform");
                    // 1408D5450 destinations: primary row array and every
                    // reflected matching node. Capture their native contents
                    // for complete undo; no guessed proxy offsets or GPU rows.
                    auto* instance=state.animation.object;
                    auto* proxy=At<void*>(instance,0x350);
                    if(!proxy || !Add(bindings_,reinterpret_cast<std::byte*>(instance)+0x350,8)
                        || !Add(bindings_,static_cast<std::byte*>(proxy)+0xa0,16)
                        || !ArrayImage(reinterpret_cast<std::byte*>(instance)+0x3f0,1024,0x70)) return Fail("animation_proxy");
                    auto* metadata=At<void*>(proxy,0xa8);
                    if(metadata) {
                        auto* properties=reinterpret_cast<void*(*)(void*)>(At<std::uintptr_t>(At<void*>(metadata,0),0x20))(metadata);
                        const auto props=At<Array>(properties,0);
                        if(props.count<0 || props.count>256 || props.capacity<props.count
                            || !Add(bindings_,properties,16) || !Add(bindings_,props.data,std::size_t(props.count)*8)) return Fail("node_properties");
                        auto* type=reinterpret_cast<void*(*)()>(base+0x8e4340)();
                        for(int node=0;node<props.count;++node) {
                            auto* address=reinterpret_cast<void*(*)(void*,int,void*)>(base+0x1c93e00)(proxy,node,type);
                            if(address && !ArrayImage(static_cast<std::byte*>(address)+0x78,1024,0x70)) return Fail("node_array");
                        }
                    }
                }
            }
        }
        if(!ValidatePayloadAccounting())return Fail("payload_accounting");
        if(roots_.size()!=2 || states_.empty() || owned_bytes()>budget_) return Fail("root_coverage");
        const auto status=lease_.Acquire(base,owners,budget_-owned_bytes());
        captured_=status.ok();
        if(!expired_.empty()) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace expired weak owners count={} retained_bytes={} strong_acquired=false payload_read=false\n"),expired_.size(),expired_bytes_);
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace capture roots={} states={} values={} bindings={} bytes={} success={}\n"),
            roots_.size(),states_.size(),values_.size(),bindings_.size(),owned_bytes(),captured_);
        return status;
    }
    // Called only by the enclosing ordered render command while the host
    // retains immutable B and prevents GT/GC advancement. Do not call the
    // thread-affine capture/lease mutation APIs from the render owner.
    bool RetainedRenderBinding(std::uintptr_t component,const std::array<int,2>& weak,
        unsigned primitive_id,std::uintptr_t mesh,bool dormant) const noexcept {
        __try {
            if(!captured_)return false;
            const auto state=std::find_if(states_.begin(),states_.end(),[&](const auto& s) {
                return reinterpret_cast<std::uintptr_t>(s.mesh.object)==component
                    && s.mesh.index==weak[0] && s.mesh.serial==weak[1];
            });
            if(state==states_.end() || !Live(state->actor) || !Live(state->mesh) || !Live(state->animation)
                || !Live(state->attachment) || !state->ref.controller
                || At<std::uintptr_t>(state->ref.controller,0)!=base_+0x3362590
                || At<int>(state->ref.controller,8)<=0)return false;
            for(const auto& root:roots_)if(!Live(root.component) || !Live(root.chara) || !Live(root.manager) || !Live(root.scene))return false;
            // Includes fixed strong-pool backing, exact actor/mesh ownership,
            // asset/animation IDs and secondary scheduling prerequisites.
            for(const auto& b:bindings_)if(!Equal(b))return false;
            auto* p=state->mesh.object;
            if(At<std::uintptr_t>(p,0)!=base_+0x38829c0 || At<unsigned>(p,0x420)!=primitive_id
                || At<std::uintptr_t>(p,0x910)!=mesh)return false;
            return !dormant || (!(At<unsigned>(p,0x240)&0x10)
                && !(At<unsigned>(p,0x188)&0xc00000e0u) && !At<void*>(p,0x790) && !At<void*>(p,0xa18));
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool RetainedDormantRenderBinding(std::uintptr_t component) const noexcept {
        __try {
            const auto state=std::find_if(states_.begin(),states_.end(),[&](const auto& s) {
                return reinterpret_cast<std::uintptr_t>(s.mesh.object)==component;
            });
            if(state==states_.end())return false;
            unsigned id{};std::uintptr_t mesh{};bool have_id{},have_mesh{};
            // Capture retains these exact typed bindings even when no primitive
            // exists. Read the retained bytes, never an old proxy allocation.
            for(const auto& b:bindings_) {
                if(b.address==reinterpret_cast<std::byte*>(component)+0x420 && b.bytes.size()==sizeof(id)) {
                    if(have_id)return false;std::memcpy(&id,b.bytes.data(),sizeof(id));have_id=true;
                }
                if(b.address==reinterpret_cast<std::byte*>(component)+0x910 && b.bytes.size()==sizeof(mesh)) {
                    if(have_mesh)return false;std::memcpy(&mesh,b.bytes.data(),sizeof(mesh));have_mesh=true;
                }
            }
            if(!have_id || !have_mesh || !RetainedRenderBinding(component,{state->mesh.index,state->mesh.serial},id,mesh,false))return false;
            auto* p=state->mesh.object;
            // Render-state bookkeeping may exist without a scene primitive or
            // skeletal render object. Preserve it; neither resource can be
            // quarantined by an invented primitive ID. The caller separately
            // requires absent captured scene membership and completed GPU work.
            return !(At<unsigned>(p,0x188)&0xc00000e0u) && !At<void*>(p,0x790) && !At<void*>(p,0xa18);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    // Recovery only: remove C rendering for a retained trace that is hidden
    // in B. No trace is destroyed, no expected data enters animation, and the
    // caller must join ordered native render retirement before installing B.
    // Operation-owned native-call receipt. A failed call may already have
    // submitted render work; neither retry nor timeout grants cancellation.
    struct RenderRecovery {
        enum class Step { Empty, Running, Submitted, Failed };
        Step step{};
        unsigned completed{};
    };
    Status RetireHiddenRenderingForUndo(const Sc6ReplayTraceState& current,const Sc6ReplayTraceState& target,RenderRecovery& journal,const Retirement* private_b=nullptr) const {
        if(journal.step==RenderRecovery::Step::Submitted) return Status::success();
        if(journal.step!=RenderRecovery::Step::Empty) return Fail("trace_render_poisoned");
        auto status=PrepareExecution(current,target,private_b);if(!status.ok())return status;
        constexpr unsigned char create[]{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x83,0xb9,0x10,9,0};
        constexpr unsigned char destroy[]{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xd2,0x80,0xfd,0xff,0x48,0x8d};
        if(std::memcmp(reinterpret_cast<void*>(base_+0x1dc0b20),create,sizeof(create))
            || std::memcmp(reinterpret_cast<void*>(base_+0x1dc1b70),destroy,sizeof(destroy)))return Fail("trace_render_native_identity");
        const auto needs_retirement=[&](const State& state) {
            auto* p=reinterpret_cast<std::byte*>(state.mesh.object);
            const auto saved=std::find_if(values_.begin(),values_.end(),[&](const auto& v){return v.address==p+0x240;});
            return saved!=values_.end() && saved->bytes.size()==4
                && !(At<unsigned>(const_cast<std::byte*>(saved->bytes.data()),0)&0x10)
                && At<void*>(p,0xa18);
        };
        // Complete preflight before the first native retirement. Source rows,
        // controllers, strong pool membership and all object generations were
        // checked by Prepare; mutable array/morph domains remain unsupported.
        for(const auto& state:states_) if(needs_retirement(state)) {
            auto* p=state.mesh.object;const auto table=At<std::uintptr_t>(p,0);
            const auto flags=At<unsigned>(p,0x188);
            if(table!=base_+0x38829c0 || At<std::uintptr_t>(reinterpret_cast<void*>(table),0x298)!=base_+0x1dc0b20
                || At<std::uintptr_t>(reinterpret_cast<void*>(table),0x2b0)!=base_+0x1dc1b70
                || (flags&3)!=3 || (flags&0xc00000e0u) || (At<unsigned>(p,0x3fc)&2)
                || At<void*>(p,0x1c8)!=world_ || !At<void*>(world_,0x168)
                || !At<void*>(p,0x910) || At<int>(p,0x990) || At<int>(p,0x9a0)
                || At<int>(p,0x9d0)!=At<int>(At<void*>(p,0x910),0xd0)
                || (At<float>(p,0x3f0)==0 && At<float>(p,0x3ec)>0))return Fail("trace_render_reconstruction_domain");
        }
        journal.step=RenderRecovery::Step::Running;
        for(const auto& state:states_) if(needs_retirement(state)) {
            if(!RetireHiddenMesh(base_,state.mesh.object)) {
                journal.step=RenderRecovery::Step::Failed;return Fail("trace_render_native_retirement");
            }
            ++journal.completed;
        }
        // Exact current trace state, not a hash over an assumed subset. This
        // catches native bounds/animation feedback before any B publication.
        status=current.ValidateValues();
        if(status.ok())status=ValidateBindings();
        if(!status.ok()) {journal.step=RenderRecovery::Step::Failed;return status;}
        journal.step=RenderRecovery::Step::Submitted;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace render retirement count={} native=true source_animation_unchanged=true B_retained=true render_drain_required=true\n"),journal.completed);
        return Status::success();
    }
    Status ReconstructVisibleRenderingForUndo(RenderRecovery& journal,bool recovery=true) const {
        if(journal.step==RenderRecovery::Step::Submitted)return ValidateValues();
        if(journal.step!=RenderRecovery::Step::Empty)return Fail("trace_visible_render_poisoned");
        auto status=ValidateValues();if(!status.ok())return status;
        const auto needs_reconstruction=[&](const State& state) {
            return (At<unsigned>(state.mesh.object,0x240)&0x10) && !At<void*>(state.mesh.object,0xa18);
        };
        for(const auto& state:states_)if(needs_reconstruction(state)) {
            auto* p=state.mesh.object;const auto table=At<std::uintptr_t>(p,0);
            // Native Create recalculates bounds and can resize LOD/morph state.
            // Admit only the same retained exact base mesh, quiescent storage
            // and cached-bounds path; exact B values are checked after calls.
            if(table!=base_+0x38829c0 || At<std::uintptr_t>(reinterpret_cast<void*>(table),0x298)!=base_+0x1dc0b20
                || At<std::uintptr_t>(reinterpret_cast<void*>(table),0x2b0)!=base_+0x1dc1b70
                || At<std::uintptr_t>(reinterpret_cast<void*>(table),0x840)!=base_+0x2d2bc0
                || At<void*>(p,0x790) || (At<unsigned>(p,0x188)&3)!=3 || (At<unsigned>(p,0x188)&0xc00000e0u)
                || (At<unsigned>(p,0x3fc)&2) || At<void*>(p,0x1c8)!=world_ || !At<void*>(world_,0x168)
                || !At<void*>(p,0x910) || At<int>(p,0x990) || At<int>(p,0x9a0)
                || At<int>(p,0x9d0)!=At<int>(At<void*>(p,0x910),0xd0)
                || !At<unsigned char>(p,0xa40) || At<void*>(p,0xec8) || At<void*>(p,0xd40)
                || (At<float>(p,0x3f0)==0 && At<float>(p,0x3ec)>0))return Fail("trace_visible_render_domain");
        }
        journal.step=RenderRecovery::Step::Running;
        for(const auto& state:states_)if(needs_reconstruction(state)) {
            if(!RecreateVisibleMesh(base_,state.mesh.object)) {
                journal.step=RenderRecovery::Step::Failed;return Fail("trace_visible_render_native");
            }
            ++journal.completed;
        }
        status=ValidateValues();
        if(!status.ok()) {journal.step=RenderRecovery::Step::Failed;return status;}
        journal.step=RenderRecovery::Step::Submitted;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] trace visible {} reconstruction count={} source_animation_unchanged=true B_retained=true render_drain_required=true\n"),recovery?STR("B"):STR("A"),journal.completed);
        return Status::success();
    }
private:
    static bool RecreateVisibleMesh(std::uintptr_t base,void* component) noexcept {
        __try {
            constexpr unsigned char create[]{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x83,0xb9,0x10,9,0};
            constexpr unsigned char destroy[]{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xd2,0x80,0xfd,0xff,0x48,0x8d};
            if(std::memcmp(reinterpret_cast<void*>(base+0x1dc0b20),create,sizeof(create))
                || std::memcmp(reinterpret_cast<void*>(base+0x1dc1b70),destroy,sizeof(destroy)))return false;
            reinterpret_cast<void(*)(void*)>(base+0x1dc1b70)(component);
            reinterpret_cast<void(*)(void*)>(base+0x1dc0b20)(component);
            return At<void*>(component,0xa18) && At<void*>(component,0x790);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static bool RetireHiddenMesh(std::uintptr_t base,void* component) noexcept {
        __try {
            const auto visibility=At<unsigned>(component,0x240);
            __try {
                // Temporary render-construction admission only. Restore C's
                // exact visibility even on failure; no application/gameplay
                // traversal can run inside this admitted native call pair.
                At<unsigned>(component,0x240)=visibility&~0x10u;
                reinterpret_cast<void(*)(void*)>(base+0x1dc1b70)(component);
                reinterpret_cast<void(*)(void*)>(base+0x1dc0b20)(component);
            } __finally {At<unsigned>(component,0x240)=visibility;}
            return !At<void*>(component,0xa18) && !At<void*>(component,0x790);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
public:
    Status ValidateBindings() const {
        if(!captured_ || thread_!=GetCurrentThreadId() || !lease_.Validate().ok()) return Fail("lease");
        // GC lease rejects explicitly destroyed UObject generations. Check
        // root arrays before any state backing; no stale snapshot dereference.
        for(const auto& r:roots_) if(!Live(r.component)||!Live(r.chara)||!Live(r.manager)||!Live(r.scene)
            || At<void*>(r.component.object,0x1c8)!=world_ || At<void*>(r.chara.object,0x458)!=r.manager.object
            || At<void*>(r.manager.object,0x3a8)!=r.component.object) return Fail("root_generation");
        for(const auto& s:states_) if(!Live(s.actor)||!Live(s.attachment)||!Live(s.mesh)||!Live(s.animation)) return Fail("object_generation");
        for(const auto& b:bindings_) if(!Equal(b)) return Fail("binding_changed");
        for(const auto& ref:expired_) if(!OwnsExpired(ref)) return Fail("expired_weak_lifetime");
        for(const auto& s:states_) if(At<int>(s.ref.controller,8)<=0 || At<std::uintptr_t>(s.ref.controller,0)!=base_+0x3362590) return Fail("controller_retired");
        return Status::success();
    }
    static bool MaterialRangeReadable(const void* pointer,std::size_t bytes) noexcept {
        auto address=reinterpret_cast<std::uintptr_t>(pointer);
        if(!address || bytes>UINTPTR_MAX-address)return false;
        const auto end=address+bytes;
        while(address<end) {
            MEMORY_BASIC_INFORMATION region{};
            if(!VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof(region))
                || region.State!=MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                || !(region.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY
                    |PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
            const auto begin=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if(region.RegionSize>UINTPTR_MAX-begin)return false;
            const auto next=begin+region.RegionSize;
            if(next<=address)return false;address=next;
        }
        return true;
    }
    static bool MaterialArrayReadable(const Array& array,std::size_t stride) noexcept {
        // Admission bound, not a claim about the engine's maximum slot count.
        return array.count>=0 && array.capacity>=array.count && array.capacity<=256
            && bool(array.data)==(array.capacity!=0)
            && (!array.capacity || MaterialRangeReadable(array.data,std::size_t(array.capacity)*stride));
    }
    static bool MaterialFreeMesh(std::uintptr_t base,Object* mesh) noexcept {
        __try {
            if(!MaterialRangeReadable(mesh,0x918) || At<std::uintptr_t>(mesh,0)!=base+0x38829c0)return false;
            const auto overrides=At<Array>(mesh,0x808);
            auto* asset=At<void*>(mesh,0x910);
            if(!MaterialArrayReadable(overrides,sizeof(void*)))return false;
            Array slots{};
            if(asset) {
                if(!MaterialRangeReadable(asset,0xb0))return false;
                slots=At<Array>(asset,0xa0);
                if(!MaterialArrayReadable(slots,0x30))return false;
            }
            // 141DC8A90 supplies the asset slot count. 141DC8220 prefers a
            // nonnull override, then falls back to the 0x30-byte asset slot.
            // 1408D5840 only writes MIDs, but no material class/proxy ownership
            // contract is admitted here: conservatively reject every nonnull
            // effective provider without dereferencing the material itself.
            for(int i=0;i<slots.count;++i) {
                void* material{};
                if(i<overrides.count)material=At<void*>(overrides.data,std::size_t(i)*sizeof(void*));
                if(!material)material=At<void*>(slots.data,std::size_t(i)*0x30);
                if(material)return false;
            }
            const auto same=[](const Array& a,const Array& b) {
                return a.data==b.data && a.count==b.count && a.capacity==b.capacity;
            };
            return At<std::uintptr_t>(mesh,0)==base+0x38829c0 && At<void*>(mesh,0x910)==asset
                && same(overrides,At<Array>(mesh,0x808)) && (!asset || same(slots,At<Array>(asset,0xa0)));
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    Status ValidateMaterialPublication(const Sc6ReplayTraceState& current) const {
        // Called after ordinary identity/binding preflight, while application
        // advancement is held. Check both sides, including private B children
        // and freshly projected A children, before any publication writes.
        // This is entry-visible admission only. Start can create providers and
        // 1408D5840 rereads slot count: this scan grants no future writer lease,
        // material undo, or vector/refresh-task completion proof.
        for(const auto* image:{this,&current}) {
            if(image->states_.size()>2*5*256)return Fail("trace_material_state_bound");
            for(const auto& state:image->states_)
                if(!Live(state.mesh) || !MaterialFreeMesh(image->base_,state.mesh.object))
                    return Fail("trace_material_provider_unsupported");
        }
        return Status::success();
    }
    Status Prepare(const Sc6ReplayTraceState& current) const {
        auto status=ValidateBindings();if(!status.ok()) return status;
        status=current.ValidateBindings();if(!status.ok()) return status;
        status=ValidateMaterialPublication(current);if(!status.ok())return status;
        if(base_!=current.base_ || world_!=current.world_ || values_.size()!=current.values_.size()) return Fail("image_shape");
        for(std::size_t i=0;i<values_.size();++i)
            if(values_[i].address!=current.values_[i].address || values_[i].bytes.size()!=current.values_[i].bytes.size()) return Fail("storage_changed");
        for(const auto& v:values_) if(!Writable(v)) return Fail("storage_not_writable");
        if(dynamic_count_!=current.dynamic_count_) return Fail("dynamic_shape");
        for(std::size_t i=0;i<dynamic_count_;++i)
            if(dynamic_[i].address!=current.dynamic_[i].address || dynamic_[i].kind!=current.dynamic_[i].kind)
                return Fail("dynamic_owner");
        return current.ValidateValues();
    }
    Status ValidateValues() const {
        const auto status=ValidateBindings();if(!status.ok()) return status;
        for(const auto& v:values_) if(!Equal(v)) return Fail("value_mismatch");
        for(std::size_t i=0;i<dynamic_count_;++i) if(!DynamicEqual(dynamic_[i])) return Fail("dynamic_value_mismatch");
        return Status::success();
    }
    Status Install() const {
        if(validation_only_)return Fail("validation_view_install");
        const auto status=ValidateBindings();if(!status.ok()) return status;
        for(const auto& v:values_) if(!Writable(v)) return Fail("storage_not_writable");
        for(const auto& v:values_) if(!Write(v)) return Fail("write_fault");
        return ValidateValues();
    }
};
}

#include "Sc6ReplayTraceStorage.inl"
