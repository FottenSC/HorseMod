#pragma once

#include <cstddef>
#include <cstdint>
#include "Sc6ReplayTaskGroup.hpp"

namespace Horse::Deterministic
{
// Explicit orchestration of 141F02230. Advance returns after one phase or
// task with the ambient memory arena restored. The engine owner decides when
// to call the next step; ambient task admission is still unresolved.
class Sc6ReplayWorld final
{
public:
    Sc6ReplayWorld() = default;
    Sc6ReplayWorld(const Sc6ReplayWorld&) = delete;
    Sc6ReplayWorld& operator=(const Sc6ReplayWorld&) = delete;
    void Begin(std::uintptr_t base, void* world, int tick_type, float delta);
    void Advance();
    void SyncFrame(std::uintptr_t base, void* state, bool allow_one_frame_lag);
    bool idle() const noexcept { return phase_ == Phase::Idle; }
    std::uint64_t arena_scopes() const noexcept { return arena_scopes_; }
    std::uint64_t max_retained_arena_bytes() const noexcept { return max_retained_arena_bytes_; }
    std::uint64_t arena_probe_checks() const noexcept { return arena_probe_checks_; }
    bool arena_empty() const noexcept { return mark_.closed && !arena_attached_ && !owned_arena_.chunk; }
    const Sc6ReplayTaskGroup& task_groups() const noexcept { return task_groups_; }
    void BindConsumerAdmission(Sc6ReplayTaskGroup::ConsumerAdmissionHooks hooks) {
        task_groups_.BindConsumerAdmission(hooks);
    }
    bool consumer_boundary(bool attached) const noexcept {
        return phase_==Phase::Group5 && arena_attached_==attached
            && !mark_.closed && task_groups_.at_consumer_boundary();
    }
    bool RequestConsumerResume() noexcept {return task_groups_.RequestConsumerResume();}
    void BindSimulation(void* actor, Sc6ReplayExecutor* simulation, bool yield_each_tick)
    {
        task_groups_.BindSimulation(actor, simulation, yield_each_tick);
        arena_scopes_ = max_retained_arena_bytes_ = 0;
        arena_probe_pending_ = yield_each_tick;
        arena_probe_checks_ = 0;
    }

private:
    enum class Phase : std::uint8_t
    {
        Idle, Entry, Prepare, Collection, StartTasks, PriorCleanup, Group0, AfterGroup0,
        Group1, Group2, Group3, Group4, CollectionConsumers, Group5, Group6,
        FinishTasks, FinishCollection, WorldTail, CloseMemory, CollectGarbage,
        NotifyEnd, Render, Finish
    } phase_{Phase::Idle};

    struct Array { void* data{}; std::int32_t count{}, capacity{}; } levels_;
    struct MemoryArena
    {
        void* top{};
        void* end{};
        void* chunk{};
        void* mark{};
        std::int32_t marks{};
        std::uint32_t unknown{};
    };
    struct MemoryMark
    {
        MemoryArena* arena{};
        void* top{};
        void* chunk{};
        bool closed{true};
        std::byte padding[7]{};
        void* previous{};
    } mark_;
    MemoryArena owned_arena_{}, ambient_arena_{};
    bool arena_attached_{};
    std::uint64_t arena_scopes_{}, max_retained_arena_bytes_{};
    // One bounded allocation-lifetime experiment in per-tick diagnostic mode.
    bool arena_probe_pending_{};
    std::byte* arena_probe_{};
    std::uint64_t arena_probe_checks_{};
    static_assert(sizeof(MemoryArena) == 0x28);
    static_assert(sizeof(MemoryMark) == 0x28);
    static_assert(offsetof(MemoryMark, previous) == 0x20);
    static_assert(sizeof(Array) == 0x10);

    // Process-local leases. None of these addresses is a serialized snapshot.
    std::uintptr_t base_{};
    std::byte* world_{};
    void* settings_{};
    void* latent_{};
    std::byte* collection_{};
    std::byte* collection_end_{};
    void* collection_context_[2]{};
    std::uint64_t token_{};
    int tick_type_{};
    float original_delta_{}, delta_{};
    bool paused_{}, run_tasks_{};
    Sc6ReplayTaskGroup task_groups_;

    void Enter();
    void AttachArena();
    void DetachArena();
    void Prepare();
    void OpenCollection();
    bool AdvanceGroup(int group, bool wait);
    void TickCollectionConsumers();
    void TickTail();
    void CloseMemory();
    void CollectGarbage();
    void NotifyListener(std::size_t slot);
    void DispatchRender();
};
}
