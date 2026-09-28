#pragma once
#include "deterministic/Sc6ReplayParticleComponentOwner.hpp"
#include "deterministic/Sc6ReplayVfxState.hpp"

namespace ReplayQualification {
// Bounded diagnostic wrapper; native construction is shared with production.
struct ParticleOwnerCopy : Horse::Deterministic::Sc6ReplayParticleComponentOwner {
    struct Witness : Horse::Deterministic::Sc6ReplayColdParticleOwner::Witness { bool registration_performed{}; };
    using SourceValidator=bool(*)(std::uintptr_t,void*,const Horse::Deterministic::Sc6ReplayVfxState*);
    static bool Run(std::uintptr_t base,void* battle,const Horse::Deterministic::Sc6ReplayVfxState& source_image,
        Horse::Deterministic::Sc6ReplayObjectLease& lease,Witness& w,SourceValidator validate_source,bool register_inactive=false,
        Horse::Deterministic::Sc6ReplayCpuEmitterState::FreshGpuOwner* gpu_owner=nullptr) {
        using namespace RC::Unreal;
        const auto fail=[&](const char* check){w.check=check;return false;};
        if(!validate_source || !validate_source(base,battle,&source_image))return fail("source_capture");
        const auto manager=Read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(battle),0x508);
        const auto rows=Read<std::uintptr_t>(manager,0x3e8);const auto n=Read<int>(manager,0x3f0);
        if(n<0 || n>1024 || (n && !rows))return fail("manager_rows");
        for(int i=0;i<n && !w.source;++i) {
            const auto component=Read<std::uintptr_t>(rows,std::size_t(i)*0xc0);
            if(!component || !source_image.RetainsComponent(reinterpret_cast<void*>(component)))continue;
            if(Read<std::uintptr_t>(component)!=base+0x335db28)continue;
            if(SingleGpuSource(base,component))w.source=component;
        }
        if(!w.source)return fail("one_gpu_source_required");
        const auto outer=Read<std::uintptr_t>(w.source,0x190);
        Members before{},after{};unsigned count{},current{};
        if(!ReadMembers(outer,before,count))return fail("source_membership");
        const bool constructed=ConstructDetached(base,reinterpret_cast<void*>(w.source),lease,w);
        if(!constructed && !w.destroyed)return false;
        const auto* construction_check=w.check;
        if(constructed && register_inactive) {
            Registration registration;
            if(!RegisterInactive(base,reinterpret_cast<UObject*>(w.source)->GetWorld(),lease,w,registration)) {
                w.check=registration.check;return false;
            }
            w.registration_performed=true;
            if(gpu_owner) {
                Horse::Deterministic::Sc6ReplayCpuEmitterState::ComponentReplacement binding;
                if(!source_image.ConstructFreshParticleOwner(w.source,w.copy,1024*1024,binding,*gpu_owner).ok())
                    return fail("fresh_gpu_constructor_hold_required");
                if(!source_image.QueueFreshGpuRetirement(binding,*gpu_owner).ok())
                    return fail("fresh_gpu_retirement_hold_required");
            }
            if(!RetireInactive(base,lease,w,registration)){w.check=registration.check;return false;}
        } else if(constructed && !Retire(base,lease,w))return false;
        w.source_unchanged=validate_source(base,battle,&source_image)
            && ReadMembers(outer,after,current) && current==count && before==after;
        if(!w.source_unchanged)return fail("copy_retirement_recovery_failed");
        if(!constructed)return fail(construction_check);
        w.check=register_inactive?"inactive_copy_retired":"cold_copy_retired";return true;
    }
};
}
