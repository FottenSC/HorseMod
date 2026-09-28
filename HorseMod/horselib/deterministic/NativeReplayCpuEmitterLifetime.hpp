#pragma once
#include "Sc6ReplayCpuEmitterState.hpp"
#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>
#include <array>
#include <atomic>
#include <memory>
#include <cstring>

namespace Horse::Deterministic {
// 141F78870 notifies peers, invokes virtual0(this,1), then clears the slot.
// Sprite141F908C0 and mesh141F90870 use RCX=this, EDX=flags, RAX=this.
// Observe their return, never substitute their cleanup or infer it from null.
class NativeReplayCpuEmitterLifetime final {
    inline static NativeReplayCpuEmitterLifetime* active_{};
    std::array<std::unique_ptr<PLH::x64Detour>,6> hooks_;
    std::array<std::uint64_t,6> originals_{};
    void* lifecycle_context_{};
    bool (*lifecycle_guard_)(void*,void*,bool) noexcept{};
    std::atomic<unsigned> depth_{};
    DWORD thread_{};
    template<std::size_t I> static void* Destroy(void* root,unsigned flags) {
        auto& self=*active_;++self.depth_;
        Sc6ReplayCpuEmitterState::Prepared::NativeDestruction(root,I==1,flags,false);
        auto* result=reinterpret_cast<void*(*)(void*,unsigned)>(self.originals_[I])(root,flags);
        Sc6ReplayCpuEmitterState::Prepared::NativeDestruction(root,I==1,flags,true);
        --self.depth_;return result;
    }
    static void* DestroyGpu(void* root,unsigned flags) {
        auto& self=*active_;++self.depth_;
        Sc6ReplayCpuEmitterState::Prepared::NativeGpuDestruction(root,flags,false);
        auto* result=reinterpret_cast<void*(*)(void*,unsigned)>(self.originals_[5])(root,flags);
        Sc6ReplayCpuEmitterState::Prepared::NativeGpuDestruction(root,flags,true);
        --self.depth_;return result;
    }
    // A retained-B lifetime veto aborts the whole speculative execution. It is
    // never a successful replacement for native completion or destruction.
    static void CompleteComponent(void* component) {
        auto& self=*active_;++self.depth_;
        if(!self.lifecycle_guard_ || !self.lifecycle_guard_(self.lifecycle_context_,component,true))
            reinterpret_cast<void(*)(void*)>(self.originals_[2])(component);
        --self.depth_;
    }
    static void DestroyComponent(void* component,bool promote) {
        auto& self=*active_;++self.depth_;
        if(!self.lifecycle_guard_ || !self.lifecycle_guard_(self.lifecycle_context_,component,false)) {
            Sc6ReplayCpuEmitterState::Prepared::NativeComponentDestruction(component,false);
            reinterpret_cast<void(*)(void*,bool)>(self.originals_[3])(component,promote);
            Sc6ReplayCpuEmitterState::Prepared::NativeComponentDestruction(component,true);
        }
        --self.depth_;
    }
    // GPU vslot D8 is the read-only completion query141F9BC90. Native
    //141F78870 consumes it before peer notifications and virtual destruction.
    // A protected true result aborts C before freeing the in-place B root.
    static bool GpuCompleted(void* emitter) {
        auto& self=*active_;++self.depth_;
        const bool complete=reinterpret_cast<bool(*)(void*)>(self.originals_[4])(emitter);
        bool protected_owner=false;
        if(complete && (*reinterpret_cast<unsigned*>(static_cast<std::byte*>(emitter)+0xe0)&4u)
            && self.lifecycle_guard_) {
            auto* component=*reinterpret_cast<void**>(static_cast<std::byte*>(emitter)+0x18);
            protected_owner=self.lifecycle_guard_(self.lifecycle_context_,component,true);
        }
        --self.depth_;return complete && !protected_owner;
    }
public:
    std::size_t owned_bytes() const {return sizeof(*this)+6*1024*1024;}
    bool Bind(std::uintptr_t base,void* context=nullptr,bool (*guard)(void*,void*,bool) noexcept=nullptr) {
        if(active_ || !base)return false;
        constexpr unsigned char sprite[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x05,0x8f,0x92,0x9b,0x01,0x8b,0xda};
        constexpr unsigned char mesh[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x8b,0xfa};
        constexpr unsigned char completion[]{0x40,0x53,0x48,0x83,0xec,0x20,0xc7,0x44,0x24,0x30,0,0,0,0,0x48,0x8d,0x54,0x24,0x30,0x48};
        constexpr unsigned char component[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb6,0xda,0x48,0x8b,0xf9,0xe8,0x5b,0x57,0};
        constexpr unsigned char gpu_destroy[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0xe8,0x8c,0xf2,0xff,0xff};
        constexpr unsigned char gpu_complete[]{0x8b,0x81,0x90,0x02,0,0,0x85,0xc0,0x74,0x13,0x39,0x81,0x70,0x01,0,0,0x7c,0x0b,0x83,0xb9,0x18,0x01,0,0,0,0x0f,0x94,0xc0,0xc3,0x32,0xc0,0xc3};
        if(*reinterpret_cast<std::uintptr_t*>(base+0x394c1d8)!=base+0x1f9bc90)return false;
        if(std::memcmp(reinterpret_cast<void*>(base+0x1f908c0),sprite,sizeof(sprite))
            || std::memcmp(reinterpret_cast<void*>(base+0x1f90870),mesh,sizeof(mesh))
            || std::memcmp(reinterpret_cast<void*>(base+0x1f6ff20),completion,sizeof(completion))
            || std::memcmp(reinterpret_cast<void*>(base+0x8cecf0),component,sizeof(component))
            || std::memcmp(reinterpret_cast<void*>(base+0x1f9bc90),gpu_complete,sizeof(gpu_complete))
            || std::memcmp(reinterpret_cast<void*>(base+0x1f90680),gpu_destroy,sizeof(gpu_destroy)))return false;
        active_=this;thread_=GetCurrentThreadId();lifecycle_context_=context;lifecycle_guard_=guard;
        hooks_[0]=std::make_unique<PLH::x64Detour>(base+0x1f908c0,reinterpret_cast<std::uint64_t>(&Destroy<0>),&originals_[0]);
        hooks_[1]=std::make_unique<PLH::x64Detour>(base+0x1f90870,reinterpret_cast<std::uint64_t>(&Destroy<1>),&originals_[1]);
        hooks_[2]=std::make_unique<PLH::x64Detour>(base+0x1f6ff20,reinterpret_cast<std::uint64_t>(&CompleteComponent),&originals_[2]);
        hooks_[3]=std::make_unique<PLH::x64Detour>(base+0x8cecf0,reinterpret_cast<std::uint64_t>(&DestroyComponent),&originals_[3]);
        hooks_[4]=std::make_unique<PLH::x64Detour>(base+0x1f9bc90,reinterpret_cast<std::uint64_t>(&GpuCompleted),&originals_[4]);
        hooks_[5]=std::make_unique<PLH::x64Detour>(base+0x1f90680,reinterpret_cast<std::uint64_t>(&DestroyGpu),&originals_[5]);
        if(hooks_[0]->hook() && hooks_[1]->hook() && hooks_[2]->hook() && hooks_[3]->hook() && hooks_[4]->hook() && hooks_[5]->hook())return true;
        if(!Stop())__fastfail(FAST_FAIL_INVALID_ARG);
        return false;
    }
    bool Stop() {
        if(active_!=this)return true;
        if(GetCurrentThreadId()!=thread_ || depth_.load() || Sc6ReplayCpuEmitterState::Prepared::HasExecutionOwners())return false;
        for(auto& hook:hooks_)if(hook && hook->isHooked() && !hook->unHook())return false;
        for(auto& hook:hooks_)hook.reset();
        lifecycle_context_=nullptr;lifecycle_guard_=nullptr;active_=nullptr;return true;
    }
    ~NativeReplayCpuEmitterLifetime() {if(!Stop())__fastfail(FAST_FAIL_INVALID_ARG);}
};
}
