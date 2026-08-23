#pragma once

#include "Interfaces.hpp"

#include <thread>

namespace Horse::Deterministic
{
class SimulationSession
{
public:
    SimulationSession(
        IGameStateAdapter& adapter,
        IInputTimeline& inputs,
        ISnapshotStore& snapshots,
        IPresentationJournal& presentation) noexcept;

    Status BindAndCaptureBaseline(
        const NativeContext& context,
        FrameCoordinate baseline) noexcept;
    Status CaptureCheckpoint(FrameCoordinate coordinate) noexcept;
    Status Advance(FrameCoordinate coordinate, const InputPair& inputs) noexcept;
    Status RestoreAndResimulate(
        FrameCoordinate target,
        FrameCoordinate resume_at) noexcept;
    Status Quiesce() noexcept;
    void Reset() noexcept;

    [[nodiscard]] SimulationState state() const noexcept { return state_; }
    [[nodiscard]] FailureCode terminal_failure() const noexcept { return failure_; }

private:
    [[nodiscard]] Status require_owner_thread() const noexcept;
    Status transactional_restore(const Snapshot& target) noexcept;
    Status undo_restore(const Snapshot& undo) noexcept;
    Status fail(FailureCode code) noexcept;

    IGameStateAdapter& adapter_;
    IInputTimeline& inputs_;
    ISnapshotStore& snapshots_;
    IPresentationJournal& presentation_;
    std::thread::id owner_thread_{};
    NativeContext context_{};
    SimulationState state_{SimulationState::Idle};
    FailureCode failure_{FailureCode::None};
};
}
