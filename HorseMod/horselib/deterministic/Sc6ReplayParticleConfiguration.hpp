#pragma once
#include "Sc6ReplayColdParticleOwner.hpp"
#include <atomic>
#include <memory>

namespace Horse::Deterministic {
// A detached native configuration is never a logical simulation participant.
// Its last handle queues retirement; only the completed-application owner may
// destroy it. Partial construction stays queued on failure, with its lease.
class Sc6ReplayParticleConfiguration final {
public:
    using Handle=std::shared_ptr<const Sc6ReplayParticleConfiguration>;
    static constexpr std::size_t allocation_allowance=1024*1024;
    static Status Capture(std::uintptr_t base,void* source,std::size_t budget,Handle& output) noexcept {
        if(output || !base || !source)return Status::failure(FailureCode::IllegalTransition);
        if(budget<sizeof(Sc6ReplayParticleConfiguration)+allocation_allowance+4096)
            return Status::failure(FailureCode::CapacityExceeded);
        try {
            auto owner=std::shared_ptr<Sc6ReplayParticleConfiguration>(new Sc6ReplayParticleConfiguration(base),
                [](Sc6ReplayParticleConfiguration* p){p->Queue();});
            // Publish the ownership journal before any native construction.
            output=owner;
            const auto captured=Sc6ReplayColdParticleOwner::CaptureMaterials(base,source,budget,
                owner->material_lease_,owner->materials_,owner->witness_);
            if(!captured.ok())return captured;
            if(owner->owned_bytes()>budget)return Status::failure(FailureCode::CapacityExceeded);
            if(!Sc6ReplayColdParticleOwner::ConstructConfiguration(base,source,owner->lease_,owner->witness_,owner->materials_))
                return Status::failure(FailureCode::CapturePreflightFailed);
            return Status::success();
        } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
    }
    Status Clone(Sc6ReplayObjectLease& lease,Sc6ReplayColdParticleOwner::Witness& witness) const noexcept {
        try {
            if(!Validate().ok() || !Sc6ReplayColdParticleOwner::CloneDetached(base_,witness_,lease_,lease,witness))
                return Status::failure(FailureCode::RestorePreflightFailed);
            return Sc6ReplayColdParticleOwner::RealizeMaterials(base_,materials_,lease,witness)
                ?Status::success():Status::failure(FailureCode::RestorePreflightFailed);
        } catch(...) {return Status::failure(FailureCode::RestorePreflightFailed);}
    }
    Status Validate() const noexcept {
        if(material_lease_.registered()) {const auto status=material_lease_.Validate();if(!status.ok())return status;}
        return lease_.ValidateObject(reinterpret_cast<void*>(witness_.copy));
    }
    bool EquivalentTo(const Sc6ReplayParticleConfiguration& other) const noexcept {
        if(base_!=other.base_ || !materials_.ValuesEqual(other.materials_))return false;
        try {return Sc6ReplayColdParticleOwner::EquivalentDetached(base_,witness_,lease_,other.witness_,other.lease_);}
        catch(...) {return false;}
    }
    std::uintptr_t source() const noexcept {return witness_.source;}
    std::size_t owned_bytes() const noexcept {return sizeof(*this)+allocation_allowance+lease_.owned_bytes()
        +material_lease_.owned_bytes()+materials_.owned_bytes()-sizeof(materials_);}
    std::size_t reconstruction_allowance() const noexcept {
        return allocation_allowance*(1u+materials_.materials);
    }
    const char* failed_check() const noexcept {return witness_.check;}
    const Sc6ReplayColdParticleOwner::Witness& construction_witness() const noexcept {return witness_;}
    static bool retirement_pending() noexcept {return pending_.load()!=nullptr;}
    static std::size_t pending_bytes() noexcept {return pending_bytes_.load();}
    // Caller owns the completed application boundary, including prior render
    // completion. A cold object has no proxy/emitter/native async work.
    static Status RetirePending(std::uintptr_t base) noexcept {
        auto* list=pending_.exchange(nullptr);
        auto result=Status::success();
        while(list) {
            auto* owner=list;list=list->next_;owner->next_=nullptr;
            const auto charged=owner->queued_bytes_;
            bool retired=false;
            try {
                if(owner->base_==base) {
                    if(!owner->witness_.native_construction_entered && !owner->witness_.created && !owner->witness_.copy)
                        retired=owner->lease_.Release().ok();
                    else if(owner->witness_.created && owner->witness_.copy)
                        retired=Sc6ReplayColdParticleOwner::Retire(base,owner->lease_,owner->witness_);
                    // Entered construction without an identified result is
                    // uncertain ownership, never permission to call destroy.
                    if(retired && owner->material_lease_.registered())retired=owner->material_lease_.Release().ok();
                }
            } catch(...) {}
            if(retired) {pending_bytes_.fetch_sub(charged);delete owner;}
            else {owner->Link();result=Status::failure(FailureCode::GenerationMismatch);}
        }
        return result;
    }
private:
    explicit Sc6ReplayParticleConfiguration(std::uintptr_t base):base_(base){}
    std::uintptr_t base_{};
    Sc6ReplayObjectLease lease_,material_lease_;
    ReplayParticleMaterialGraph materials_;
    Sc6ReplayColdParticleOwner::Witness witness_;
    Sc6ReplayParticleConfiguration* next_{};
    std::size_t queued_bytes_{};
    inline static std::atomic<Sc6ReplayParticleConfiguration*> pending_{};
    inline static std::atomic<std::size_t> pending_bytes_{};
    void Link() noexcept {
        auto* head=pending_.load();
        do {next_=head;}while(!pending_.compare_exchange_weak(head,this));
    }
    void Queue() noexcept {
        queued_bytes_=owned_bytes();pending_bytes_.fetch_add(queued_bytes_);Link();
    }
};
}
