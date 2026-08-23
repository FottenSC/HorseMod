#pragma once

#include "SimulationSession.hpp"
#include "Schema.hpp"

namespace Horse::Deterministic
{
class ReplayCoordinator
{
public:
    static constexpr std::uint64_t checkpoint_interval = Schema::checkpoint_interval;
    static constexpr std::size_t default_memory_limit = 512ull * 1024ull * 1024ull;

    explicit ReplayCoordinator(SimulationSession& simulation) noexcept;

    Status Begin(const NativeContext& context, FrameCoordinate baseline) noexcept;
    Status RecordAndAdvance(FrameCoordinate coordinate, const InputPair& inputs) noexcept;
    Status MarkGenerationBoundary(FrameCoordinate coordinate) noexcept;
    Status FinishCapture() noexcept;
    Status Seek(FrameCoordinate target) noexcept;
    Status Resume() noexcept;
    Status Fail(FailureCode code) noexcept;
    void Reset() noexcept;

    [[nodiscard]] ReplayState state() const noexcept { return state_; }
    [[nodiscard]] FailureCode terminal_failure() const noexcept { return failure_; }
    [[nodiscard]] bool partial_timeline() const noexcept { return partial_timeline_; }
    [[nodiscard]] FrameCoordinate playhead() const noexcept { return playhead_; }
    [[nodiscard]] FrameCoordinate captured_end() const noexcept { return captured_end_; }

private:
    SimulationSession& simulation_;
    ReplayState state_{ReplayState::Idle};
    FailureCode failure_{FailureCode::None};
    FrameCoordinate playhead_{};
    FrameCoordinate captured_end_{};
    bool partial_timeline_{};
};
}
