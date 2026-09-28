#pragma once
#include <atomic>
#include <cstdint>

namespace Horse::Deterministic {
// A synchronous execution witness, never ownership, exclusion or completion.
// Kinds: 1 native tick body, 2 custom manager entry, 3 manager continuation,
// 4 primary disable hub. Only the VFX observer may persist selected snapshots.
class ReplayVfxExecutionScope final {
public:
    ReplayVfxExecutionScope(unsigned kind,void* context,void* argument,unsigned native_thread,void* caller) noexcept
        :kind_(kind),native_thread_(native_thread),context_(reinterpret_cast<std::uintptr_t>(context)),
         argument_(reinterpret_cast<std::uintptr_t>(argument)),caller_(reinterpret_cast<std::uintptr_t>(caller)) {
        if(accepting_.load(std::memory_order_acquire)) {
            previous_=current_;current_=this;linked_=true;
            id_=next_.fetch_add(1,std::memory_order_relaxed)+1;
            if(observe_)observe_(*this,true);
        }
    }
    ~ReplayVfxExecutionScope(){if(linked_){if(observe_)observe_(*this,false);current_=previous_;}}
    ReplayVfxExecutionScope(const ReplayVfxExecutionScope&)=delete;
    ReplayVfxExecutionScope& operator=(const ReplayVfxExecutionScope&)=delete;
private:
    friend class NativeReplayVfxCompletionObservation;
    inline static std::atomic<bool> accepting_{};
    inline static std::atomic<std::uint64_t> next_{};
    inline static thread_local ReplayVfxExecutionScope* current_{};
    // Installed once before accepting scopes by the existing bounded observer.
    // Private diagnostic callback; never admission, suppression or a task lease.
    inline static void (*observe_)(ReplayVfxExecutionScope&,bool) noexcept{};
    unsigned observation_slot_{~0u};
    ReplayVfxExecutionScope* previous_{};
    unsigned kind_{},native_thread_{};
    std::uintptr_t context_{},argument_{},caller_{};
    std::uint64_t id_{};
    bool linked_{};
};
}
