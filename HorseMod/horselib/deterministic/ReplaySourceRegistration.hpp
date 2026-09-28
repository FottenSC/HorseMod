#pragma once
#include "Types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse::Deterministic {
// One independently verified replay receiver. Additional recipients, external
// delegates and foreign owners reject; this is not a generic delegate snapshot.
class ReplaySourceRegistration {
public:
    struct Header {
        std::uintptr_t data{};
        std::int32_t count{}, capacity{};
        friend bool operator==(const Header&,const Header&)=default;
    };
    struct Image {
        std::uintptr_t input{}, source{};
        Header storage{};
        std::uint64_t handle{};
        friend bool operator==(const Image&,const Image&)=default;
    };
    struct Ops {
        void* user{};
        std::uintptr_t base{};
        bool (*read)(void*,std::uintptr_t,void*,std::size_t){};
        bool (*write)(void*,std::uintptr_t,const void*,std::size_t){};
        bool (*bindings)(void*){};
        void* (*allocate)(void*,std::size_t){};
        void (*free)(void*,void*){};
        bool (*copy_wrapper)(void*,std::uintptr_t,std::uint64_t,void*){};
        void (*destroy_wrapper)(void*,void*){};
    };
    static constexpr std::size_t maximum_capacity=64;
    static constexpr std::size_t stride=0x40;
    template<class T> static bool Read(const Ops& ops,std::uintptr_t p,T& out) noexcept {
        return ops.read && ops.read(ops.user,p,&out,sizeof(out));
    }
    static bool StorageValid(const Ops& ops,const Image& image) noexcept {
        const auto& h=image.storage;
        if(!image.input || !image.source || h.count<0 || h.count>1 || h.capacity<h.count
            || h.capacity>maximum_capacity || (h.capacity!=0)!=(h.data!=0))return false;
        if(!h.count)return image.handle==0;
        std::uintptr_t vtable{},owner{},external{};std::uint64_t token{};std::int32_t units{};
        return image.handle && Read(ops,h.data,vtable) && vtable==ops.base+0x3298810
            && Read(ops,h.data+8,owner) && owner==image.source
            && Read(ops,h.data+0x18,token) && token==image.handle
            && Read(ops,h.data+0x20,external) && !external
            && Read(ops,h.data+0x30,units) && units==2;
    }
    static bool Capture(const Ops& ops,std::uintptr_t input,std::uintptr_t source,Image& out) noexcept {
        out={input,source};
        return ops.bindings && ops.bindings(ops.user)
            && Read(ops,input+0x43d0,out.storage) && Read(ops,source+0x3c8,out.handle)
            && StorageValid(ops,out);
    }
    static bool Matches(const Ops& ops,const Image& image) noexcept {
        Image current;
        return Capture(ops,image.input,image.source,current) && current==image;
    }

    class Prepared {
    public:
        enum class Phase { Empty, Prepared, Publishing, Published, Executing, Settled,
            Recovering, Recovered, Committed };
    private:
        Ops ops_{};
        Image a_{}, b_{}, current_{};
        Phase phase_{Phase::Empty};
        std::size_t reserved_{};
        static Status Reject(){return Status::failure(FailureCode::RestorePreflightFailed);}
        bool OriginalValid() const noexcept {return StorageValid(ops_,b_);}
        bool Write(const Image& image) noexcept {
            // Both roots are prevalidated by bindings. Attempt both writes on
            // undo, including recovery from interrupted header publication.
            const bool header=ops_.write(ops_.user,image.input+0x43d0,&image.storage,sizeof(image.storage));
            const bool handle=ops_.write(ops_.user,image.source+0x3c8,&image.handle,sizeof(image.handle));
            return header && handle;
        }
        void Retire(Image& image) noexcept {
            if(image.storage.count)ops_.destroy_wrapper(ops_.user,reinterpret_cast<void*>(image.storage.data));
            if(image.storage.data)ops_.free(ops_.user,reinterpret_cast<void*>(image.storage.data));
            image.storage={};image.handle=0;
        }
    public:
        Prepared()=default;
        Prepared(const Prepared&)=delete;
        Prepared& operator=(const Prepared&)=delete;
        ~Prepared(){if(phase_==Phase::Prepared)Retire(a_);}
        Phase phase() const noexcept{return phase_;}
        std::size_t owned_bytes() const noexcept{return sizeof(*this)+reserved_;}
        Status Prepare(const Ops& ops,const Image& target,const Image& original,std::size_t budget) noexcept {
            if(phase_!=Phase::Empty || target.input!=original.input || target.source!=original.source
                || !ops.read || !ops.write || !ops.allocate || !ops.free || !ops.copy_wrapper || !ops.destroy_wrapper
                || !ops.bindings || !ops.bindings(ops.user) || !Matches(ops,original)
                || target.storage.count<0 || target.storage.count>1
                || (target.storage.count==1)!=(target.handle!=0))return Reject();
            const auto capacity=target.storage.count?std::size_t{4}:0;
            const auto bytes=3*maximum_capacity*stride+128;
            if(budget<sizeof(*this) || bytes>budget-sizeof(*this))return Status::failure(FailureCode::CapacityExceeded);
            ops_=ops;b_=original;a_={target.input,target.source,{},target.handle};reserved_=bytes;
            if(capacity){
                auto* allocation=ops_.allocate(ops_.user,capacity*stride);
                if(!allocation)return Status::failure(FailureCode::CapacityExceeded);
                a_.storage={reinterpret_cast<std::uintptr_t>(allocation),0,static_cast<std::int32_t>(capacity)};
                std::memset(allocation,0,capacity*stride);
                if(!ops_.copy_wrapper(ops_.user,a_.source,a_.handle,allocation)){
                    ops_.free(ops_.user,allocation);a_.storage={};return Reject();
                }
                a_.storage.count=1;
            }
            phase_=Phase::Prepared;
            if(!StorageValid(ops_,a_) || (a_.storage.data && a_.storage.data==b_.storage.data))return Reject();
            return Status::success();
        }
        Status Publish() noexcept {
            if(phase_!=Phase::Prepared || !ops_.bindings(ops_.user) || !Matches(ops_,b_) || !StorageValid(ops_,a_))return Reject();
            phase_=Phase::Publishing;
            if(!Write(a_) || !Matches(ops_,a_))return Status::failure(FailureCode::RestoreVerificationFailed);
            current_=a_;phase_=Phase::Published;return Status::success();
        }
        Status BeginExecution(std::size_t budget) noexcept {
            if(budget<owned_bytes() || phase_!=Phase::Published || !Matches(ops_,current_) || !OriginalValid())return Reject();
            phase_=Phase::Executing;return Status::success();
        }
        Status SettleExecution() noexcept {
            if(phase_!=Phase::Executing || !OriginalValid())return Reject();
            Image observed;
            if(!Capture(ops_,a_.input,a_.source,observed)
                || (observed.storage.data && observed.storage.data==b_.storage.data))return Reject();
            // Native code may have removed or reallocated A. Only C's current
            // validated allocation is now owned; never free A's stale address.
            current_=observed;phase_=Phase::Settled;return Status::success();
        }
        Status ReopenExecution() noexcept {
            if((phase_!=Phase::Executing && phase_!=Phase::Settled)
                || !ops_.bindings(ops_.user) || !OriginalValid())return Reject();
            // While executing, native removal/reallocation can make current_
            // stale. Validate the live domain without adopting it; Undo still
            // requires a subsequent successful C settlement.
            if(phase_==Phase::Executing) {
                Image observed;
                if(!Capture(ops_,a_.input,a_.source,observed)
                    || (observed.storage.data && observed.storage.data==b_.storage.data))return Reject();
                return Status::success();
            }
            if(!Matches(ops_,current_))return Reject();
            phase_=Phase::Executing;return Status::success();
        }
        Status Undo() noexcept {
            if(phase_==Phase::Empty || phase_==Phase::Recovered)return Status::success();
            if(phase_==Phase::Prepared){Retire(a_);phase_=Phase::Recovered;return Status::success();}
            if(phase_!=Phase::Publishing && phase_!=Phase::Published && phase_!=Phase::Settled && phase_!=Phase::Recovering)return Reject();
            if(!ops_.bindings(ops_.user) || !OriginalValid())return Reject();
            if(phase_==Phase::Publishing)current_=a_;
            else if(phase_!=Phase::Recovering && !Matches(ops_,current_))return Reject();
            phase_=Phase::Recovering;
            if(!Write(b_) || !Matches(ops_,b_))return Status::failure(FailureCode::UndoFailed);
            Retire(current_);a_.storage={};phase_=Phase::Recovered;return Status::success();
        }
        Status ValidatePublished() const noexcept {
            return (phase_==Phase::Published || phase_==Phase::Settled) && Matches(ops_,current_) && OriginalValid()
                ? Status::success():Reject();
        }
        Status Commit() noexcept {
            if(phase_==Phase::Committed)return Status::success();
            if((phase_!=Phase::Published && phase_!=Phase::Settled) || !Matches(ops_,current_) || !OriginalValid())return Reject();
            Retire(b_);a_.storage={};phase_=Phase::Committed;return Status::success();
        }
    };
};
}
