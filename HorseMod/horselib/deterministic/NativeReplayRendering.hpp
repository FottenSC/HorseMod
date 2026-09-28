#pragma once
#include "ReplayRenderState.hpp"
#include "Sc6ReplayObjectVisit.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <Windows.h>
#include <DynamicOutput/DynamicOutput.hpp>
#include <polyhook2/Detour/x64Detour.hpp>

namespace Horse::Deterministic {
// Replay presentation admission. GT constructor -> owned native renderer ->
// RT execution, with no lookup of live GT clocks on the render thread.
// Native algorithms and rand() calls remain intact. Render delta and shader
// time/noise use this logical stream, independently initialized in the control.
// Global engine epochs are untouched. The historical RHI transaction separately
// owns the admitted view's query history and local maintenance epoch.
// Bounded admission: one persistent FXAA view, normal named rendering thread.
class NativeReplayRendering final {
public:
    bool Bind(std::uintptr_t base) {
        if(active_) return false;
        constexpr unsigned char signatures[4][12]={
            {0x48,0x89,0x5c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x48,0x89},
            {0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x08,0x49,0x89,0x6b,0x18,0x56},
            {0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x55},
            {0x40,0x55,0x53,0x41,0x54,0x41,0x57,0x48,0x8d,0xac,0x24,0x68}};
        constexpr std::uintptr_t entries[]{0x14dba90,0x14f8500,0x208c930,0x14f38b0};
        for(unsigned i=0;i<4;++i)
            if(std::memcmp(reinterpret_cast<void*>(base+entries[i]),signatures[i],12)) return false;
        base_=base;thread_=GetCurrentThreadId();
        engine_=*reinterpret_cast<void**>(base+0x43b3068);
        if(!engine_ || !(Field<unsigned char>(engine_,0x648)&0x40) || Field<float>(engine_,0x64c)!=60.0f
            || Field<bool>(reinterpret_cast<void*>(base),0x416a943) || Field<bool>(reinterpret_cast<void*>(base),0x416a944)) return false;
        slot_=reinterpret_cast<void**>(base+0x3657810+0x78);
        if(*slot_!=reinterpret_cast<void*>(base+0x14b3fc0)) return false;
        Fence();active_=this;
        const std::uint64_t callbacks[]{reinterpret_cast<std::uint64_t>(&Construct),reinterpret_cast<std::uint64_t>(&Render),reinterpret_cast<std::uint64_t>(&Uniforms),reinterpret_cast<std::uint64_t>(&InitializeViews)};
        for(unsigned i=0;i<4;++i) {
            hooks_[i]=std::make_unique<PLH::x64Detour>(base+entries[i],callbacks[i],&originals_[i]);
            if(!hooks_[i]->hook()) {Stop();return false;}
        }
        if(!SwapSlot(reinterpret_cast<void*>(base+0x14b3fc0),reinterpret_cast<void*>(&SamplingIndex))) {Stop();return false;}
        slot_owned_=true;return true;
    }
    bool Stop() {
        if(active_!=this) return true;
        if(GetCurrentThreadId()!=thread_) return false;
        Fence();
        {std::lock_guard lock(mutex_);if(!Empty()) return false;}
        if((slot_owned_ || *slot_==reinterpret_cast<void*>(&SamplingIndex)) && !SwapSlot(reinterpret_cast<void*>(&SamplingIndex),reinterpret_cast<void*>(base_+0x14b3fc0))) return false;
        slot_owned_=false;
        for(auto& h:hooks_) if(h && h->isHooked() && !h->unHook()) return false;
        for(auto& h:hooks_) h.reset();active_=nullptr;return true;
    }
    ~NativeReplayRendering() {if(!Stop()) __fastfail(FAST_FAIL_INVALID_ARG);}
    // Called only on the admitted render/RHI owner while the application is
    // held. The enclosing host validates world/viewport lifetime and drains
    // render entries before the GPU transaction requests this binding.
    void* HeldViewState() {
        std::lock_guard lock(mutex_);
        return !failed() && Empty() ? last_view_state_ : nullptr;
    }
    // Attachment can follow the first native view construction. Discover its
    // existing owner before tick-zero capture, without producing another view
    // or advancing the logical render stream. Later Construct still requires
    // exact identity with this binding.
    bool BindInitialViewState(void* world) {
        if(GetCurrentThreadId()!=thread_) return false;
        std::lock_guard lock(mutex_);
        if(failed() || !Empty() || constructed_ || state_.publications) return false;
        auto* view=InitialViewState(base_,world);
        if(!view || (last_view_state_ && last_view_state_!=view)) return false;
        last_view_state_=view;
        RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] initial replay view binding existing_owner=true publications=0 state={:x}\n"),
            reinterpret_cast<std::uintptr_t>(view));
        return true;
    }
    bool failed() const {return failed_.load();}
    std::uint64_t completed() const {return completed_.load();}
    std::uint64_t constructed() const {return constructed_;}
    bool Capture(ReplayRenderState& out) {
        std::lock_guard lock(mutex_);
        if(GetCurrentThreadId()!=thread_ || failed() || !Empty()) return false;
        out=state_;return true;
    }
    bool Replace(const ReplayRenderState& expected,const ReplayRenderState& target) {
        std::lock_guard lock(mutex_);
        if(GetCurrentThreadId()!=thread_ || failed() || !Empty() || state_!=expected
            || !std::isfinite(target.elapsed) || target.elapsed<0 || target.publications>constructed_) return false;
        state_=target;reconstruct_history_=target.history_valid;return true;
    }
    // IDs come from retained, lifetime-validated B primitive owners. The
    // caller owns producer exclusion and must keep them alive until clearing.
    // This edits only each newly constructed private view's native hidden set.
    bool SetQuarantinedPrimitives(std::span<const unsigned> ids,std::size_t budget) {
        std::lock_guard lock(mutex_);
        if(GetCurrentThreadId()!=thread_ || failed() || !Empty() || ids.size()>quarantined_.size()) return false;
        if(!ids.empty()) {
            constexpr unsigned char code[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
            if(std::memcmp(reinterpret_cast<void*>(base_+0x1487ee0),code,sizeof(code)) || budget<quarantine_reservation) return false;
        }
        for(std::size_t i=0;i<ids.size();++i) for(std::size_t j=0;j<i;++j) if(ids[i]==ids[j]) return false;
        std::copy(ids.begin(),ids.end(),quarantined_.begin());quarantined_count_=ids.size();
        return true;
    }
    bool ReadQuarantinedPrimitives(std::span<unsigned> ids,std::size_t& count) {
        std::lock_guard lock(mutex_);
        if(GetCurrentThreadId()!=thread_ || failed() || !Empty() || ids.size()<quarantined_count_)return false;
        std::copy_n(quarantined_.begin(),quarantined_count_,ids.begin());count=quarantined_count_;return true;
    }
    std::size_t owned_bytes() const noexcept {return sizeof(*this)+(quarantined_count_?quarantine_reservation:0);}
private:
    static void* InitialViewState(std::uintptr_t base,void* world) noexcept {
        __try {
            // 1421BB640 returns a live controller's LocalPlayer; 141EEA220
            // selects +98 for normal/left views. 14205C920 copies it to
            // FSceneView+8, and 1414EFD10 copies that to FViewInfo+1220.
            constexpr unsigned char getter[]{0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x30,0xc7,0x44};
            constexpr unsigned char consumer[]{0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xec};
            if(!world || std::memcmp(reinterpret_cast<void*>(base+0x21bb640),getter,sizeof(getter))
                || std::memcmp(reinterpret_cast<void*>(base+0x1eea220),consumer,sizeof(consumer))
                // The getter calls ULocalPlayer::StaticClass. Its native lazy
                // registration must already be complete: capture only borrows
                // an existing owner and must not create a class as a side effect.
                || !*reinterpret_cast<void**>(base+0x43c2b00)
                || Field<int>(world,0x190)<1 || Field<int>(world,0x190)>16 || !Field<void*>(world,0x188)) return nullptr;
            auto* player=reinterpret_cast<RC::Unreal::UObject*(*)(void*)>(base+0x21bb640)(world);
            if(!player) return nullptr;
            const auto* item=RC::Unreal::FUObjectArray::IndexToObject(player->GetInternalIndex());
            if(!item || item->GetUObject()!=player || !item->IsValid(false)
                || Field<std::uintptr_t>(player,0x90)!=base+0x33feb00) return nullptr;
            auto* controller=Field<void*>(player,0x30);
            auto* view=Field<void*>(player,0x98);
            if(!controller || Field<void*>(controller,0x3f0)!=player || !view
                || Field<std::uintptr_t>(view,0)!=base+0x3657810) return nullptr;
            return view;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
    }
    struct Entry {void* renderer{};void* view{};void* state{};std::uint64_t epoch{};unsigned seed{},uniforms{};float shader_time{};};
    inline static NativeReplayRendering* active_{};
    inline static thread_local Entry* executing_{};
    std::array<Entry,16> entries_{};
    std::mutex mutex_;
    ReplayRenderState state_{};
    std::array<unsigned,64> quarantined_{};
    std::size_t quarantined_count_{};
    // Conservative allowance for native growth plus the old copied backing
    // while up to sixteen admitted view objects await native destruction.
    static constexpr std::size_t quarantine_reservation=16*512*1024;
    bool ApplyQuarantine(void* view) noexcept {
        if(!quarantined_count_) return true;
        __try {
            auto* set=static_cast<std::byte*>(view)+0x8a8;
            // This is TSet<FPrimitiveComponentId>, stride 12, not a slot ID
            // filter in the VFX manager. C may reuse B's manager slot numbers.
            const auto valid=[&]() {
                const int count=Field<int>(set,8),capacity=Field<int>(set,12),bits=Field<int>(set,0x28);
                const int maxbits=Field<int>(set,0x2c),free=Field<int>(set,0x34),hashes=Field<int>(set,0x48);
                return count>=0 && count<=8192 && capacity>=count && capacity<=8192 && bits==count
                    && maxbits>=bits && maxbits<=8192 && free>=0 && free<=count
                    && (!capacity || Field<void*>(set,0)) && (Field<void*>(set,0x20) || maxbits<=128)
                    // Native 1411C5C10 allocates the first hash bucket only
                    // after insertion. An empty copied view may have zero.
                    && hashes>=0 && hashes<=16384 && !(hashes&(hashes-1))
                    && (hashes || (count==free && !Field<void*>(set,0x40)))
                    && (Field<void*>(set,0x40) || hashes<=1);
            };
            if(!valid() || Field<int>(set,8)>4096) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] quarantine view rejected count={} capacity={} bits={} maxbits={} free={} hashes={}\n"),
                    Field<int>(set,8),Field<int>(set,12),Field<int>(set,0x28),Field<int>(set,0x2c),Field<int>(set,0x34),Field<int>(set,0x48));
                return false;
            }
            for(std::size_t i=0;i<quarantined_count_;++i) {
                int index=-1;unsigned char present{};auto id=quarantined_[i];
                auto* result=reinterpret_cast<int*(*)(void*,int*,unsigned*,unsigned char*)>(base_+0x1487ee0)(set,&index,&id,&present);
                if(result!=&index || !valid() || index<0 || index>=Field<int>(set,8)) return false;
                const auto* flags=Field<unsigned*>(set,0x20);
                if(!flags) flags=reinterpret_cast<unsigned*>(set+0x10);
                if(!(flags[index/32]&(1u<<(index%32)))
                    || Field<unsigned>(Field<void*>(set,0),std::size_t(index)*12)!=id) return false;
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    std::array<std::unique_ptr<PLH::x64Detour>,4> hooks_;
    std::array<std::uint64_t,4> originals_{};
    std::uintptr_t base_{};DWORD thread_{};void** slot_{};void* scene_{};void* engine_{};void* last_view_state_{};
    void Fail(unsigned code) {
        if(!failed_.exchange(true)) RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] replay rendering failure check={} tick={}\n"),
            code,*reinterpret_cast<unsigned*>(base_+0x470d0c4));
    }
    std::atomic<bool> failed_{};std::atomic<std::uint64_t> completed_{};
    std::uint64_t constructed_{};bool slot_owned_{};bool reconstruct_history_{};
    template<class T> static T& Field(void* p,std::size_t offset) {return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
    bool Empty() const {for(const auto& e:entries_) if(e.renderer) return false;return true;}
    void Fence() {
        void* event{};
        reinterpret_cast<void (*)(void**,bool)>(base_+0x15e9510)(&event,false);
        reinterpret_cast<void (*)(void**,bool)>(base_+0x15efec0)(&event,false);
    }
    bool SwapSlot(void* expected,void* desired) {
        DWORD protection{};if(!VirtualProtect(slot_,sizeof(void*),PAGE_READWRITE,&protection)) return false;
        const auto prior=InterlockedCompareExchangePointer(slot_,desired,expected);
        DWORD unused{};const bool restored=VirtualProtect(slot_,sizeof(void*),protection,&unused)!=0;
        if(!restored && prior==expected) {
            InterlockedCompareExchangePointer(slot_,expected,desired);
            VirtualProtect(slot_,sizeof(void*),protection,&unused);
        }
        return prior==expected && restored;
    }
    static void* Construct(void* renderer,void* family,void* canvas) {
        auto& s=*active_;
        auto* result=reinterpret_cast<void* (*)(void*,void*,void*)>(s.originals_[0])(renderer,family,canvas);
        if(GetCurrentThreadId()!=s.thread_ || result!=renderer || !renderer) {s.Fail(1);return result;}
        auto* view=Field<void*>(renderer,0xb8);
        auto* scene=Field<void*>(renderer,8);
        if(Field<int>(renderer,0xc0)!=1 || !view || !scene || Field<unsigned char>(view,0x11d0)!=1
            || Field<void*>(view,0)!=static_cast<std::byte*>(renderer)+0x10 || !Field<void*>(view,0x1220)) {s.Fail(2);return result;}
        const auto delta=Field<float>(renderer,0x70);
        if(s.constructed_<10) RC::Output::send<RC::LogLevel::Default>(STR("[HorseMod] replay view admission ordinal={} tick={} delta={:.9f}\n"),
            s.constructed_,*reinterpret_cast<unsigned*>(s.base_+0x470d0c4),delta); // copied Family+60 WorldDeltaTime
        if(!std::isfinite(delta) || delta<0 || delta>1 || (s.scene_ && scene!=s.scene_)
            || (s.last_view_state_ && Field<void*>(view,0x1220)!=s.last_view_state_)
            || *reinterpret_cast<void**>(s.base_+0x43b3068)!=s.engine_
            || !(Field<unsigned char>(s.engine_,0x648)&0x40) || Field<float>(s.engine_,0x64c)!=60.0f) {s.Fail(3);return result;}
        std::lock_guard lock(s.mutex_);
        if(!s.ApplyQuarantine(view)) {s.Fail(9);return result;}
        Entry* free{};for(auto& e:s.entries_) {if(e.renderer==renderer) {s.Fail(4);return result;}if(!e.renderer) free=&e;}
        if(!free) {s.Fail(5);return result;}
        s.scene_=scene;s.last_view_state_=Field<void*>(view,0x1220);
        // Bounded fixed-rate replay presentation clock. Native FEngineLoop's
        // wall pacing remains untouched. The first two observed view deltas
        // included hook/fence installation time; they are not replay inputs.
        // View publications are separate from Lux ticks (repeats/no-tick
        // intervals do not change this coordinate's meaning).
        ++s.state_.publications;
        s.state_.elapsed=static_cast<double>(s.state_.publications-1)/static_cast<double>(Field<float>(s.engine_,0x64c));
        s.state_.random_state=s.state_.random_state*214013u+2531011u;
        *free={renderer,view,Field<void*>(view,0x1220),s.state_.publications,(s.state_.random_state>>16)&32767u,0,static_cast<float>(s.state_.elapsed)};
        // The owned renderer copied Family+60 to +70. Native1412F5E60 feeds
        // this delta to point-lighting interpolation14130ABE0;14137C950 feeds
        // it to exposure. Wall-time stalls during capture must not alter those
        // retained histories. Derive the delta from the admitted fixed-rate
        // publication clock, including after rewind or changed later inputs.
        // Never replay a recorded future delta or overwrite expected outputs.
        // This is a view-publication interval, not a Lux tick/Gekko frame.
        Field<float>(renderer,0x70)=1.0f/Field<float>(s.engine_,0x64c);
        // Family absolute time/frame still feed native visibility/admission.
        // Shader absolute time/noise remain scoped in Uniforms below.
        ++s.constructed_;return result;
    }
    static void Render(void* commands,void* renderer) {
        auto& s=*active_;Entry* entry{};
        {std::lock_guard lock(s.mutex_);for(auto& e:s.entries_) if(e.renderer==renderer) {entry=&e;break;}}
        if(!entry || executing_) s.Fail(6);
        auto* prior=executing_;executing_=entry;
        // 1414F8500 owns execution and renderer cleanup; never touch its
        // renderer/view/state pointers after this call returns.
        reinterpret_cast<void (*)(void*,void*)>(s.originals_[1])(commands,renderer);
        executing_=prior;
        if(entry) {std::lock_guard lock(s.mutex_);if(entry->uniforms!=1) s.Fail(7);*entry={};++s.completed_;}
    }
    static void InitializeViews(void* renderer,void* commands) {
        auto& s=*active_;
        reinterpret_cast<void (*)(void*,void*)>(s.originals_[3])(renderer,commands);
        auto* e=executing_;
        if(!e || e->renderer!=renderer) return;
        std::lock_guard lock(s.mutex_);
        if(!s.reconstruct_history_) return;
        if(!s.state_.history_valid || Field<void*>(e->view,0x1220)!=e->state) {s.Fail(8);return;}
        // 1414F38B0 resets previous transforms on a backward real-time jump.
        // The logical previous camera is retained at the checkpoint. Keep
        // native ignore-occlusion/fade flags (28) for physical stale queries.
        // An authored camera cut still owns its native matrix reset.
        if(!Field<unsigned char>(e->view,0xad4)) {
            std::memcpy(static_cast<std::byte*>(e->view)+0x1fe0,s.state_.last_view_matrices.data(),s.state_.last_view_matrices.size());
            Field<unsigned>(e->view,0x1fcc)&=~4u;
        }
        s.reconstruct_history_=false;
    }
    static void Uniforms(void* view,void* output,void* size,unsigned samples,void* rect,void* matrices,void* previous) {
        auto& s=*active_;
        reinterpret_cast<void (*)(void*,void*,void*,unsigned,void*,void*,void*)>(s.originals_[2])(view,output,size,samples,rect,matrices,previous);
        auto* e=executing_;
        if(!e) return;
        // Shadow setup 1414FC630 clones the admitted view through 1414E7BA0:
        // Family (+0) and view-state (+1220) remain shared; clone flag +2518
        // is set, and 1414F1FE0 publishes its separate shadow uniform. It is
        // part of this same logical view family, not another scheduling epoch.
        const bool primary=view==e->view;
        const bool shadow=!primary && Field<void*>(view,0)==Field<void*>(e->view,0)
            && Field<void*>(view,0x1220)==e->state && Field<unsigned char>(view,0x2518)==1;
        if(primary || shadow) {
            // 14208C930 also emits previous game/real time at +7D8/+7DC.
            // Keep the complete shader clock pair in this logical fixed-60
            // domain; native Family clocks still own visibility/admission.
            const auto previous_shader_time=e->shader_time-1.0f/60.0f;
            Field<float>(output,0x7d8)=previous_shader_time;
            Field<float>(output,0x7dc)=previous_shader_time;
            Field<float>(output,0x808)=e->shader_time;
            Field<float>(output,0x80c)=e->shader_time;
            Field<unsigned>(output,0x810)=e->seed;
            Field<unsigned>(output,0x814)=static_cast<unsigned>(e->epoch);
            // Only the primary view owns the retained camera-history image.
            // Native rand() was still called above for each shadow/primary;
            // only shader-visible inputs share the checkpointed family stream.
            if(!primary) return;
            ++e->uniforms;
            std::lock_guard lock(s.mutex_);
            std::memcpy(s.state_.last_view_matrices.data(),matrices,s.state_.last_view_matrices.size());
            s.state_.history_valid=true;
        }
    }
    static unsigned SamplingIndex(void* state) {
        if(auto* e=executing_;e && state==e->state) return static_cast<unsigned>(e->epoch&7);
        return reinterpret_cast<unsigned (*)(void*)>(active_->base_+0x14b3fc0)(state);
    }
};
}
