#pragma once
#include <Windows.h>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include <memory>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include "ReplayHudWidgetAdmission.hpp"

namespace Horse::Deterministic {
// Widget ticking admits animations AND latent/Blueprint callbacks. This is
// replay scheduling, not a classification of those consumers as cosmetic.
// SWidget::Paint141042460 forwards its application delta to SObjectWidget's
// virtual+8 (1418264D0), which forwards XMM3 to UUserWidget virtual+2C8 in XMM2.
// Admit one fixed application interval at that boundary, preserving the native
// widget's own skip/accumulate/zero-delta rules (e.g.1403F2D10). Neither engine
// nor Slate timestamps/epochs are rewound. Native controls use the same policy.
class NativeReplayWidgetClock final {
    inline static NativeReplayWidgetClock* active_{};
    std::unique_ptr<PLH::x64Detour> hook_;
    std::uint64_t original_{},calls_{};
    std::uintptr_t base_{};void* world_{};void* engine_{};DWORD thread_{};
    unsigned depth_{};bool failed_{};
    template<class T> static T& At(void* p,std::size_t offset) {return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
    static void Tick(void* slate_widget,void* geometry,double current_time,float delta) {
        auto& self=*active_;
        const auto original=reinterpret_cast<void(*)(void*,void*,double,float)>(self.original_);
        if(GetCurrentThreadId()!=self.thread_) {original(slate_widget,geometry,current_time,delta);return;}
        ++self.depth_;
        auto* widget=At<RC::Unreal::UObject*>(slate_widget,0x4e8);
        // Private B is still attached for reversible viewport ownership. Skip
        // the whole native widget update, including wrapper clock accumulation,
        // animation admission and Blueprint/latent dispatch, not just its delta.
        if(replay_hud_widget_admission.Excludes(widget)) {--self.depth_;return;}
        if(widget) {
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(widget->GetInternalIndex());
            if(item && item->GetUObject()==widget && item->IsValid(false)) {
                const auto get_world=reinterpret_cast<void*(*)(void*)>(At<std::uintptr_t>(At<void*>(widget,0),0x138));
                if(get_world(widget)==self.world_) {
                    if(*reinterpret_cast<void**>(self.base_+0x43b3068)!=self.engine_
                        || !(At<unsigned char>(self.engine_,0x648)&0x40) || At<float>(self.engine_,0x64c)!=60.0f) self.failed_=true;
                    else {delta=1.0f/60.0f;++self.calls_;}
                }
            }
        }
        original(slate_widget,geometry,current_time,delta);
        --self.depth_;
    }
public:
    static bool Owns(void* world,DWORD thread) {
        return active_ && active_->world_==world && active_->thread_==thread && !active_->depth_ && !active_->failed_;
    }
    // Conservative fixed reservation for the sole detour/trampoline and its
    // bounded decoder scratch, in addition to this object's storage.
    std::size_t owned_bytes() const {return sizeof(*this)+1024*1024;}
    bool Bind(std::uintptr_t base,void* world) {
        if(active_ || !base || !world) return false;
        constexpr unsigned char signature[]{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x40};
        if(std::memcmp(reinterpret_cast<void*>(base+0x18264d0),signature,sizeof(signature))) return false;
        base_=base;world_=world;thread_=GetCurrentThreadId();engine_=*reinterpret_cast<void**>(base+0x43b3068);
        if(!engine_ || !(At<unsigned char>(engine_,0x648)&0x40) || At<float>(engine_,0x64c)!=60.0f) return false;
        calls_=0;failed_=false;active_=this;
        hook_=std::make_unique<PLH::x64Detour>(base+0x18264d0,reinterpret_cast<std::uint64_t>(&Tick),&original_);
        if(!hook_->hook()) {hook_.reset();active_=nullptr;return false;}
        return true;
    }
    bool Stop() {
        if(active_!=this) return true;
        if(GetCurrentThreadId()!=thread_ || depth_ || !replay_hud_widget_admission.empty()) return false;
        if(hook_ && hook_->isHooked() && !hook_->unHook()) return false;
        hook_.reset();active_=nullptr;world_=nullptr;return true;
    }
    ~NativeReplayWidgetClock() {if(!Stop()) __fastfail(FAST_FAIL_INVALID_ARG);}
    bool failed() const {return failed_;}
    std::uint64_t calls() const {return calls_;}
};
}
