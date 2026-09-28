#pragma once
#include "Sc6ReplayColdParticleOwner.hpp"

namespace Horse::Deterministic {
// Operation-local native component identity. The enclosing transaction owns
// the application hold and this journal until explicit retirement completes.
// A failure after native entry is dirty, including an exception/partial write.
// This first registration boundary deliberately has no emitter/render graph.
struct Sc6ReplayParticleComponentOwner : Sc6ReplayColdParticleOwner {
    enum class Phase { Empty, Adding, Added, Registering, Registered, Destroying, Destroyed, Retired, Failed };
    struct Registration {
        Phase phase{Phase::Empty};
        std::uintptr_t component{},outer{},world{},particle_template{};
        const char* check{"not_started"};
    };
    enum class RenderPhase { Empty, RemovingInactive, InactiveRemoved, RebuildingRelevance, Creating, Created, Failed };
    struct RenderPreparation {
        RenderPhase phase{RenderPhase::Empty};
        std::array<std::byte,16> emitter_header{};
        std::uintptr_t component{},proxy{};
        unsigned primitive_id{};
        std::size_t charged_bytes{};
        bool array_hidden{};
        const char* check{"not_started"};
    };
    enum class GpuRegistrationPhase { Empty, Entered, Queued, Failed };
    struct GpuRegistration {
        GpuRegistrationPhase phase{GpuRegistrationPhase::Empty};
        std::uintptr_t system{},render{};
    };
    static bool NativeRegisterGpuRender(std::uintptr_t base,std::uintptr_t system,std::uintptr_t render) noexcept {
        __try {
            constexpr unsigned char signature[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x50};
            if(std::memcmp(reinterpret_cast<void*>(base+0x1f91160),signature,sizeof(signature)))return false;
            // This public entry owns allocation/dispatch of its pooled task.
            // The task body must never be invoked with caller-owned storage.
            reinterpret_cast<void(*)(void*,void*)>(base+0x1f91160)(reinterpret_cast<void*>(system),reinterpret_cast<void*>(render));return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool QueueGpuRenderRegistration(std::uintptr_t base,const Sc6ReplayObjectLease& lease,
        const Registration& registration,const RenderPreparation& proxy,std::uintptr_t root,
        std::uintptr_t render,GpuRegistration& journal) {
        if(journal.phase!=GpuRegistrationPhase::Empty || registration.phase!=Phase::Registered
            || proxy.phase!=RenderPhase::Created || proxy.array_hidden
            || !lease.ValidateObject(reinterpret_cast<void*>(registration.component)).ok())return false;
        if(!root || !render || Read<std::uintptr_t>(root)!=base+0x394c100
            || Read<std::uintptr_t>(root,0x18)!=registration.component || Read<std::uintptr_t>(root,0x1e0)!=render
            || Read<std::uintptr_t>(render)!=base+0x394bfc0 || Read<int>(render,0x240)!=-1
            || Read<unsigned char>(render,0x28)!=1 || Read<unsigned char>(render,0x218)!=1
            || Read<unsigned char>(render,0x252) || !Read<std::uintptr_t>(render,0x48))return false;
        const auto system=Read<std::uintptr_t>(root,0x1d0);
        if(!system)return false;
        journal.system=system;journal.render=render;journal.phase=GpuRegistrationPhase::Entered;
        if(!NativeRegisterGpuRender(base,system,render)){journal.phase=GpuRegistrationPhase::Failed;return false;}
        journal.phase=GpuRegistrationPhase::Queued;return true; // completion is the enclosing ordered event
    }
    static bool NativeRemoveRenderState(std::uintptr_t base,std::uintptr_t component) noexcept {
        __try {
            constexpr unsigned char signature[]{0x40,0x53,0x48,0x83,0xec,0x20,0x33,0xd2};
            if(std::memcmp(reinterpret_cast<void*>(base+0x1f71fd0),signature,sizeof(signature)))return false;
            reinterpret_cast<void(*)(void*)>(base+0x1f71fd0)(reinterpret_cast<void*>(component));return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool NativeRebuildRelevance(std::uintptr_t base,std::uintptr_t component,std::uintptr_t particle_template) noexcept {
        __try {
            constexpr unsigned char signature[]{0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x41,0x56};
            if(std::memcmp(reinterpret_cast<void*>(base+0x1f6f2d0),signature,sizeof(signature)))return false;
            reinterpret_cast<void(*)(void*,void*)>(base+0x1f6f2d0)(reinterpret_cast<void*>(component),reinterpret_cast<void*>(particle_template));return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool NativeCreateRenderState(std::uintptr_t base,std::uintptr_t component) noexcept {
        __try {
            constexpr unsigned char signature[]{0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20};
            if(std::memcmp(reinterpret_cast<void*>(base+0x1f711a0),signature,sizeof(signature)))return false;
            reinterpret_cast<void(*)(void*)>(base+0x1f711a0)(reinterpret_cast<void*>(component));return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool SetPrivateEmitterHeader(std::uintptr_t component,const std::array<std::byte,16>& header) noexcept {
        __try {std::memcpy(reinterpret_cast<void*>(component+0xa50),header.data(),header.size());return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool RemoveInactiveRenderState(std::uintptr_t base,const Sc6ReplayObjectLease& lease,
        const Registration& registration,RenderPreparation& render) {
        if(render.phase!=RenderPhase::Empty || registration.phase!=Phase::Registered
            || !InactiveRegistered(base,lease,registration))return false;
        const auto c=registration.component;
        // Template-state bit80 runs Finalize even when the primitive is absent.
        // This is an empty inactive preparation, not logical completion.
        if(Read<unsigned>(c,0x830)&0x80)return false;
        render.component=c;render.primitive_id=Read<unsigned>(c,0x420);
        if(!render.primitive_id)return false;
        render.phase=RenderPhase::RemovingInactive;render.check="inactive_render_removal_entered";
        if(!NativeRemoveRenderState(base,c) || (Read<unsigned>(c,0x188)&2)
            || Read<std::uintptr_t>(c,0x790) || Read<int>(c,0x7a0))return false;
        render.phase=RenderPhase::InactiveRemoved;render.check="inactive_render_removed";return true;
    }
    static bool CreateActiveRenderState(std::uintptr_t base,const Sc6ReplayObjectLease& lease,
        const Registration& registration,RenderPreparation& render,std::size_t budget) {
        if(render.phase!=RenderPhase::InactiveRemoved || registration.phase!=Phase::Registered
            || render.component!=registration.component || !lease.ValidateObject(reinterpret_cast<void*>(render.component)).ok())return false;
        const auto c=render.component,t=registration.particle_template;
        const auto flags=Read<unsigned>(c,0x188);
        const auto count=Read<int>(c,0xa58),capacity=Read<int>(c,0xa5c);
        const auto slots=Read<std::uintptr_t>(c,0xa50);
        if(!t || Read<std::uintptr_t>(c)!=base+0x335db28 || Read<std::uintptr_t>(c,0x808)!=t
            || Read<std::uintptr_t>(c,0x1c8)!=registration.world || Read<std::uintptr_t>(c,0x190)!=registration.outer
            || !(flags&1) || !(flags&0x40000) || (flags&2) || Read<std::uintptr_t>(c,0x790)
            || Read<unsigned>(c,0x420)!=render.primitive_id || Read<std::uintptr_t>(c,0xa70)
            || Read<unsigned char>(c,0xa80) || Read<unsigned char>(c,0xa81) || Read<int>(c,0x908)
            || !Read<unsigned char>(base+0x40956b8) || (Read<unsigned char>(t,0xbc)&2)
            || count<=0 || count>128 || capacity<count || capacity>128 || !slots)return false;
        // The alternate proxy writes template+10C. Admit only its already-set
        // state, without mutating shared configuration during reconstruction.
        const auto mode=Read<unsigned char>(t,0xf0);
        if(mode && (mode==2 || Read<unsigned char>(t,0xb4)) && Read<unsigned char>(t,0x10c)!=1)return false;
        unsigned gpu{},roots{};
        for(int i=0;i<count;++i)if(const auto root=Read<std::uintptr_t>(slots,std::size_t(i)*8)) {
            const auto table=Read<std::uintptr_t>(root);
            const bool is_gpu=table==base+0x394c100,is_mesh=table==base+0x3949d88;
            if((!is_gpu && !is_mesh && table!=base+0x3949b60) || Read<std::uintptr_t>(root,0x18)!=c
                || Read<std::uintptr_t>(table,0x170)!=base+(is_mesh?0x1f982a0:0x1f981c0))return false;
            gpu+=is_gpu;++roots;
        }
        // Sprite/GPU +170 OR packed material flags; mesh +170 aggregates its
        // material list through 141F982A0. The native rebuild walks all roots.
        if(gpu>128 || !roots || budget<1024*1024+sizeof(RenderPreparation))return false;
        render.charged_bytes=1024*1024+sizeof(RenderPreparation);
        std::memcpy(render.emitter_header.data(),reinterpret_cast<void*>(c+0xa50),16);
        render.phase=RenderPhase::RebuildingRelevance;render.check="relevance_entered";
        if(!NativeRebuildRelevance(base,c,t))return false;
        if(Read<int>(c,0x8e8)<=0 || Read<int>(c,0x8e8)>128 || Read<int>(c,0x8ec)<Read<int>(c,0x8e8)
            || Read<int>(c,0x8ec)>128 || !Read<std::uintptr_t>(c,0x8e0))return false;
        // The private native emitter graph stays owned in this journal while
        // CreateSceneProxy builds its empty initial dynamic packet. No emitter
        // virtual+120 runs here; ordinary application work consumes it once.
        render.array_hidden=true;render.phase=RenderPhase::Creating;render.check="proxy_creation_entered";
        const bool hidden=SetPrivateEmitterHeader(c,{});
        const bool created=hidden && NativeCreateRenderState(base,c);
        const bool restored=SetPrivateEmitterHeader(c,render.emitter_header);
        if(restored)render.array_hidden=false;
        if(!created || !restored) {render.phase=RenderPhase::Failed;return false;}
        render.proxy=Read<std::uintptr_t>(c,0x790);
        if(!render.proxy || !(Read<unsigned>(c,0x188)&2) || Read<unsigned>(c,0x420)!=render.primitive_id
            || Read<std::uintptr_t>(c,0x808)!=t)return false;
        const auto type=Read<std::uintptr_t>(render.proxy);
        if(type!=base+0x3956e80 && type!=base+0x3956f98)return false;
        render.phase=RenderPhase::Created;render.check="proxy_created_render_completion_pending";return true;
    }
    static bool NativeAdd(std::uintptr_t base,void* outer,void* component) noexcept {
        __try {reinterpret_cast<void(*)(void*,void*)>(base+0x1c0c8d0)(outer,component);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool NativeRegister(std::uintptr_t base,void* component,void* world) noexcept {
        __try {reinterpret_cast<void(*)(void*,void*)>(base+0x1d58ba0)(component,world);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool EndPlayEmpty(std::uintptr_t base,std::uintptr_t component) noexcept {
        __try {
            const auto function=reinterpret_cast<std::uintptr_t(*)(const void*,std::uint64_t)>(base+0xf6e0e0)
                (reinterpret_cast<void*>(component),Read<std::uint64_t>(base+0x43b5538));
            return function && !(Read<unsigned>(function,0x88)&0x400) && !Read<unsigned>(function,0x50);
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool InactiveRegistered(std::uintptr_t base,const Sc6ReplayObjectLease& lease,
        const Registration& state) noexcept {
        if(!lease.ValidateObject(reinterpret_cast<void*>(state.component)).ok())return false;
        __try {
            const auto c=state.component;
            const auto flags=Read<unsigned>(c,0x188);
            return Read<std::uintptr_t>(c)==base+0x335db28
                && Read<std::uintptr_t>(c,0x20)==state.outer && Read<std::uintptr_t>(c,0x190)==state.outer
                && Read<std::uintptr_t>(c,0x1c8)==state.world && Read<std::uintptr_t>(c,0x808)==state.particle_template
                && (flags&1) && !(flags&(4u|0x60000u|0x200000u|0x10000000u))
                && !Read<std::uintptr_t>(c,0x790) && !Read<int>(c,0x7a0)
                && !Read<std::uintptr_t>(c,0x1d0) && EmptyArray(c,0x1d8)
                && EmptyArray(c,0x6a0) && EmptyArray(c,0xa50)
                && !Read<std::uintptr_t>(c,0xa70) && !Read<unsigned char>(c,0xa80)
                && !Read<unsigned char>(c,0xa81) && EndPlayEmpty(base,c);
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool RegisterInactive(std::uintptr_t base,void* world,Sc6ReplayObjectLease& lease,
        Witness& owner,Registration& state) {
        if(state.phase!=Phase::Empty)return false;
        const auto fail=[&](const char* check){state.check=check;return false;};
        if(!RegistrationPreflight(base,world,lease,owner))return fail("registration_preflight");
        // Child registration and alternate render-state implementations require
        // their own ownership. Never change native mode flags to pass admission.
        const auto table=Read<std::uintptr_t>(owner.copy);
        const auto method=Read<unsigned char>(owner.copy,0x18d);
        if(method==1 || method==2 || !EndPlayEmpty(base,owner.copy)
            || Read<std::uintptr_t>(table,0x298)!=base+0x1f711a0
            || Read<std::uintptr_t>(table,0x640)!=base+0x1fbd100)return fail("registration_native_dispatch");
        Members before{},after{};unsigned count{},current{};
        if(!ReadMembers(owner.outer,before,count) || count==before.size()
            || std::find(before.begin(),before.begin()+count,owner.copy)!=before.begin()+count)
            return fail("registration_membership");
        state.component=owner.copy;state.outer=owner.outer;state.world=reinterpret_cast<std::uintptr_t>(world);
        state.particle_template=Read<std::uintptr_t>(owner.copy,0x808);
        state.phase=Phase::Adding;owner.detached=false;
        if(!NativeAdd(base,reinterpret_cast<void*>(owner.outer),reinterpret_cast<void*>(owner.copy)))
            return fail("registration_add_hold_required");
        if(!ReadMembers(owner.outer,after,current) || current!=count+1
            || std::find(after.begin(),after.begin()+current,owner.copy)==after.begin()+current)
            return fail("registration_add_membership_hold_required");
        for(unsigned i=0;i<count;++i)
            if(std::find(after.begin(),after.begin()+current,before[i])==after.begin()+current)
                return fail("registration_prior_membership_hold_required");
        state.phase=Phase::Added;
        state.phase=Phase::Registering;
        if(!NativeRegister(base,reinterpret_cast<void*>(owner.copy),world)
            || !InactiveRegistered(base,lease,state))return fail("registration_native_hold_required");
        state.phase=Phase::Registered;state.check="inactive_registered";return true;
    }
    static bool RetireInactive(std::uintptr_t base,Sc6ReplayObjectLease& lease,Witness& owner,Registration& state) {
        if(state.phase==Phase::Retired)return true;
        const auto fail=[&](const char* check){state.check=check;return false;};
        if(state.component!=owner.copy || state.outer!=owner.outer)return fail("retirement_identity");
        if(state.phase!=Phase::Destroyed) {
            if(state.phase!=Phase::Registered || !InactiveRegistered(base,lease,state))
                return fail("retirement_preflight_hold_required");
            // Native RemovePrimitive1414BDE80 queues no work for a null proxy;
            // the zero +7A0 count above also excludes prior render references.
            // Native destruction still runs Unregister and required callbacks.
            state.phase=Phase::Destroying;
            if(!NativeDestroy(base,reinterpret_cast<void*>(owner.copy)) || Live(reinterpret_cast<void*>(owner.copy)))
                return fail("retirement_native_hold_required");
            owner.destroyed=true;state.phase=Phase::Destroyed;
        }
        owner.lease_released=lease.Release().ok();
        if(!owner.lease_released)return fail("retirement_lease_pending");
        state.phase=Phase::Retired;state.check="inactive_retired";return true;
    }
};
}
