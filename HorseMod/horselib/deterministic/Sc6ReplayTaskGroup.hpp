#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace Horse::Deterministic
{
class Sc6ReplayExecutor;
// Tick-manager/sequencer orchestration from 142165D30 / 142164510 and the
// game-thread event wait from 140D37EB0. Each Advance returns after at most
// one task dispatch. No native wait/pump stack is retained between calls.
class Sc6ReplayTaskGroup final
{
public:
    Sc6ReplayTaskGroup() = default;
    Sc6ReplayTaskGroup(const Sc6ReplayTaskGroup&) = delete;
    Sc6ReplayTaskGroup& operator=(const Sc6ReplayTaskGroup&) = delete;
    void Begin(std::uintptr_t base, void* manager, int group, bool wait);
    // Preserve the prior frame's cleanup-event wait before native StartTasks,
    // using the same admitted task pump as the ordinary group waits.
    bool BeginPriorCleanup(std::uintptr_t base, void* manager, void* world, float delta, int tick_type);
    bool AdvancePriorCleanup();
    // Non-yielding application tail, with admitted GT drains and native fence
    // submission/completion/timeout semantics (1421876D0 / 1415ED580).
    void SyncFrame(std::uintptr_t base, void* state, bool allow_one_frame_lag);
    void BindSimulation(void* actor, Sc6ReplayExecutor* simulation, bool yield_each_tick);
    bool OwnsManagerEntry(void* actor) const noexcept { return entry_active_ && actor == actor_; }
    void Advance();
    bool idle() const noexcept { return phase_ == Phase::Idle; }
    std::uint64_t completed_groups() const noexcept { return completed_groups_; }
    std::uint64_t dispatched_tasks() const noexcept { return dispatched_tasks_; }
    std::uint64_t manager_tasks() const noexcept { return manager_tasks_; }
    std::uint64_t manager_yields() const noexcept { return manager_yields_; }
    enum class DispatchOutcome : std::uint8_t { Completed, ManagerPending, ConsumerHeld, TerminalFailure };
    DispatchOutcome dispatch_outcome() const noexcept { return dispatch_outcome_; }
    // The host supplies concrete native lifetime admission. These hooks do not
    // cancel a task or manufacture a completion event. Hold is legal only when
    // acquire has obtained that family's separately verified protection.
    struct ConsumerTask {
        void* task{};
        void* function{};
        void* world{};
        void* completion{};
        std::uintptr_t owner{};
        std::int32_t owner_index{}, owner_generation{};
        std::uint64_t application_epoch{}, contract{};
        std::uint32_t native_thread{}, operating_thread{};
    };
    enum class ConsumerAdmission : std::uint8_t { Forward, Hold, Reject, ForwardObserved };
    struct ConsumerAdmissionHooks {
        void* context{};
        ConsumerAdmission (*acquire)(void*, ConsumerTask&) noexcept{};
        bool (*validate)(void*, const ConsumerTask&) noexcept{};
        bool (*begin_execution)(void*, const ConsumerTask&) noexcept{};
        void (*completed)(void*, const ConsumerTask&) noexcept{};
        void (*pop_scope)(void*,bool) noexcept{};
    };
    bool at_consumer_boundary() const noexcept {
        return group_==5 && cleanup_group_==5 && requested_wait_
            && phase_==Phase::WaitGroup && pumping_ && !pending_task_;
    }
    void BindConsumerAdmission(ConsumerAdmissionHooks hooks);
    const ConsumerTask* held_consumer_task() const noexcept {
        return consumer_task_.task ? &consumer_task_ : nullptr;
    }
    const ConsumerTask* executing_consumer_task() const noexcept {return executing_consumer_task_;}
    // A failed validation leaves the untouched task and protection owned.
    // Neither polling nor timeout invokes this operation implicitly.
    bool RequestConsumerResume() noexcept;
    // Process-local lease for independent suspended-event observation.
    const void* pending_manager_task() const noexcept { return pending_task_; }

private:
    friend struct ReplayTaskGroupTestAccess;
    friend class NativeReplayPrerequisiteGuard; // Read-only rejection diagnostic of retained cleanup events.
    struct EventArray
    {
        void* inline_events[4]{};
        void** heap{};
        std::int32_t count{}, capacity{};
        void** data() noexcept { return heap ? heap : inline_events; }
    } single_event_;
    static_assert(sizeof(EventArray) == 0x30);
    static_assert(offsetof(EventArray, count) == 0x28);

    enum class Phase : std::uint8_t
    {
        Idle, PriorCleanup, FrameSync, Release, WaitRelease, SelectWait, PumpIdle, WaitGroup,
        CleanupGroup, NextGroup, FinishSequence, SpawnLevel, SpawnDecision,
        DropSpawned, Finish
    } phase_{Phase::Idle};

    std::uintptr_t base_{};
    std::byte* manager_{};
    std::byte* sequencer_{};
    std::byte* named_thread_{};
    void* release_event_{};
    void* return_task_{};
    EventArray* waiting_events_{};
    int group_{}, cleanup_group_{}, level_{}, spawn_passes_{};
    std::uint32_t spawned_{};
    bool requested_wait_{}, spawn_sequence_{}, pumping_{}, allow_stall_{};
    std::uint8_t pump_return_requested_{};
    std::uint64_t completed_groups_{}, dispatched_tasks_{};
    Sc6ReplayExecutor* simulation_{};
    void* actor_{};
    std::byte* pending_task_{};
    bool entry_active_{}, yield_each_tick_{};
    std::uint64_t manager_tasks_{}, manager_yields_{};
    DispatchOutcome dispatch_outcome_{DispatchOutcome::Completed};
    // Failure-only process-local diagnostic, never a resumable task lease.
    void* terminal_task_{};
    ConsumerAdmissionHooks consumer_hooks_{};
    ConsumerTask consumer_task_{};
    const ConsumerTask* executing_consumer_task_{};
    bool consumer_resume_requested_{}, consumer_executing_{};
    std::array<std::uint8_t,0x19> prior_context_{};
    std::array<std::uint8_t,3> prior_flags_{};
    EventArray prior_array_{};
    std::array<void*,8> prior_events_{};

    bool ParallelTasks() const;
    std::array<std::uint8_t,3> PriorCleanupFlags() const;
    bool PriorCleanupStable() const;
    void DrainFrameTasks();
    void WaitFrameFence(void** slot, bool process_tasks);
    double FrameWaitClock() const;
    void ReportFrameWaitTimeout(double start);
    void ReleaseGroup();
    void BeginWait(EventArray* events);
    void BeginPump();
    void PumpOne(bool until_idle);
    void CleanupGroup();
    void ReleaseEvent(void* event);
    void FinishSequence();
    DispatchOutcome DispatchTask(void* task);
    DispatchOutcome ResumeConsumerTask();
    void ResumeManagerTask();
    void ValidatePendingTask();
    void FinishManagerTask();
};
}
