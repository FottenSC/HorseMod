#pragma once

#include "SimulationSession.hpp"
#include "Schema.hpp"

#include <map>
#include <optional>

namespace Horse::Deterministic
{
class ReplayCoordinator
{
public:
    static constexpr std::uint64_t checkpoint_interval = Schema::checkpoint_interval;
    static constexpr std::size_t default_memory_limit = 512ull * 1024ull * 1024ull;

    explicit ReplayCoordinator(
        SimulationSession& simulation,
        IReplayGenerationMaterializer* materializer = nullptr) noexcept;

    Status Begin(
        const NativeContext& context,
        FrameCoordinate baseline,
        std::uint32_t native_round_index,
        std::uint64_t round_image_identity) noexcept;
    Status RecordAndAdvance(FrameCoordinate coordinate, const InputPair& inputs) noexcept;
    Status BeginGeneration(
        const NativeContext& context,
        FrameCoordinate baseline,
        std::uint32_t native_round_index,
        std::uint64_t round_image_identity) noexcept;
    Status FinishCapture() noexcept;
    Status Seek(FrameCoordinate target) noexcept;
    Status PollSeek() noexcept;
    Status Resume() noexcept;
    Status Fail(FailureCode code) noexcept;
    void Reset() noexcept;

    [[nodiscard]] ReplayState state() const noexcept { return state_; }
    [[nodiscard]] FailureCode terminal_failure() const noexcept { return failure_; }
    [[nodiscard]] bool partial_timeline() const noexcept { return partial_timeline_; }
    [[nodiscard]] FrameCoordinate playhead() const noexcept { return playhead_; }
    [[nodiscard]] FrameCoordinate captured_end() const noexcept { return captured_end_; }

private:
    struct GenerationRecord
    {
        ReplayGenerationTarget target{};
        FrameCoordinate captured_end{};
    };

    Status finish_seek(FrameCoordinate target) noexcept;

    SimulationSession& simulation_;
    IReplayGenerationMaterializer* materializer_{};
    ReplayState state_{ReplayState::Idle};
    FailureCode failure_{FailureCode::None};
    FrameCoordinate playhead_{};
    FrameCoordinate captured_end_{};
    bool partial_timeline_{};
    std::map<std::uint64_t, GenerationRecord> generations_;
    std::optional<FrameCoordinate> pending_seek_;
};
}
