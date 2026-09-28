#pragma once

#include "Types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic
{
// Native orchestration from 1403FBF30, 1403FE520 and 1403F7D70. A yield
// retains logical work here; no native stack or return address is retained.
class Sc6ReplayExecutor final
{
public:
    enum class Phase : std::uint8_t
    {
        Idle, OuterEntry, WorkerEntry, MoveEntry, MoveTick, MoveTail,
        WorkerCounters, PublishInput, SimulationTick, RepeatDecision,
        InputTail, RoundDecision, OuterTail, Failed,
        RestartSetup, RestartTick, RestartTail
    };

    struct Continuation
    {
        std::uint64_t tick{};
        std::uint64_t interval{};
        std::uint64_t publications{};
        std::uint32_t remaining_inputs{};
        std::int32_t cache_offset{};
        float delta_seconds{};
        Phase phase{Phase::Idle};
        bool entry_press_timer_positive{};
        bool entry_down_timer_positive{};

        friend bool operator==(const Continuation&, const Continuation&) = default;
    };

    struct Statistics
    {
        std::uint64_t completed_intervals{};
        std::uint64_t zero_tick_intervals{};
        std::uint64_t multi_tick_intervals{};
        std::uint64_t repeat_requests{};
        std::uint64_t move_state_ticks{};
        std::uint64_t yielded_boundaries{};
        std::uint64_t restart_traversals{};
    };

    // Start only after the previous interval's outer tail has completed.
    Status Begin(std::uintptr_t image_base, void* manager, float delta_seconds) noexcept;
    enum class AdvanceOutcome : std::uint8_t { ReachedTarget, IntervalComplete, Failed, PendingTraversal };
    struct AdvanceResult {
        AdvanceOutcome outcome{AdvanceOutcome::Failed};
        Status status{Status::failure(FailureCode::IllegalTransition)};
    };
    // Interval completion is an explicit outcome, never proof of target
    // readiness. The application owner drives subsequent scheduling intervals.
    AdvanceResult AdvanceToTick(std::uint64_t target) noexcept;
    Status AdvanceOneTick() noexcept;
    // Temporary application-owner admission. Exact completion may execute
    // owed epilogues, but must return before publishing another input or
    // entering a traversal/reset. The continuation remains fully owned.
    void SetTailOnly(bool enabled) noexcept { tail_only_=enabled; tail_blocked_=false; }
    bool tail_blocked() const noexcept { return tail_blocked_; }
    static constexpr bool IsPendingBoundary(Phase phase) noexcept {
        return phase==Phase::MoveTail || phase==Phase::RepeatDecision || phase==Phase::RestartTail
            || phase==Phase::PublishInput || phase==Phase::SimulationTick || phase==Phase::RestartSetup
            || phase==Phase::RestartTick || phase==Phase::MoveTick || phase==Phase::OuterEntry;
    }
    Status DrainInterval() noexcept { return AdvanceToTick(UINT64_MAX).status; }
    Status Stop() noexcept;
    // Same live input publication/interval only. The enclosing owner restores
    // gameplay state transactionally before publishing this continuation.
    Status RestoreContinuation(const Continuation& expected_current, const Continuation& target) noexcept;
    // Completed native outer tails only. The enclosing owner must install the
    // matching source/game/world state before releasing application ownership.
    Status RestoreIdleContinuation(const Continuation& expected_current, const Continuation& target) noexcept;
    [[nodiscard]] const Continuation& continuation() const noexcept { return state_; }
    [[nodiscard]] bool interval_complete() const noexcept { return state_.phase == Phase::Idle; }
    [[nodiscard]] const Statistics& statistics() const noexcept { return statistics_; }
    // Value-only observation after traversal callbacks/round updates, and after
    // each outer tail (including empty intervals). Not part of a checkpoint.
    using BoundaryObserver = void (*)(void*, const Continuation&, bool interval_complete) noexcept;
    bool SetBoundaryObserver(void* owner, BoundaryObserver observer) noexcept;

private:
    friend struct ReplayExecutorTestAccess;
    bool tail_only_{}, tail_blocked_{};
    struct SharedHandler { void* object{}; void* controller{}; };
    // These are process-local bindings, never serialized as deterministic data.
    struct Bindings
    {
        std::uintptr_t image_base{};
        std::byte* manager{};
        std::uint32_t thread{};
        std::array<void*, 3> frame_args{};
        SharedHandler handler{};
    } bindings_{};
    Continuation state_{};
    Statistics statistics_{};
    std::uint64_t interval_entry_tick_{};
    void* observer_owner_{};
    BoundaryObserver observer_{};

    Status AdvanceUnchecked(std::uint64_t target);
    void MoveEntry();
    void MoveTail();
    void WorkerCounters();
    void PublishInput();
    void RoundDecision();
    void RestartSetup();
    void OuterTail();
    void ReleaseHandler(SharedHandler& handler);
    void CountTraversal(std::uint32_t before);
    Status Fail(FailureCode code) noexcept;
};
}
