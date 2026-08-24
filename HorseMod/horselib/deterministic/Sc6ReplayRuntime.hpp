#pragma once

#include "DeterministicHookSet.hpp"
#include "InputTimeline.hpp"
#include "Sc6CandidateCheckpointCapture.hpp"
#include "Sc6ReplayNativeBridge.hpp"

#include <optional>

namespace Horse
{
class Lux;

namespace Deterministic
{
struct ReplayTimelineStatus
{
    FailureCode failure{FailureCode::None};
    FrameCoordinate last_coordinate{};
    std::int32_t native_round{};
    std::int32_t native_time{};
    std::uint64_t generations{};
    std::uint64_t sessions{};
    std::uint64_t captured_frames{};
    std::uint64_t captured_checkpoints{};
    std::size_t checkpoint_bytes{};
    FailureCode checkpoint_failure{FailureCode::None};
    bool partial{};
};

class Sc6ReplayRuntime final
{
public:
    explicit Sc6ReplayRuntime(Lux& lux) noexcept;

    Status Initialize(std::uintptr_t image_base) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] IReplayNativeBridge* bridge() noexcept;
    Status ObserveFrame(const FrameFencepostObservation& observation) noexcept;
    void ObserveReplayExit() noexcept;
    [[nodiscard]] ReplayTimelineStatus timeline_status() const noexcept;
    [[nodiscard]] const InputTimeline& input_timeline() const noexcept;

private:
    static void* ResolveReplayPlayer(void* user) noexcept;
    static void* ResolveBattleManager(void* user) noexcept;
    static void* ResolveFighterOne(void* user) noexcept;
    static void* ResolveFighterTwo(void* user) noexcept;
    static void* ResolveStage(void* user) noexcept;

    [[nodiscard]] void* ResolveFighter(std::size_t index) noexcept;

    Lux& lux_;
    std::optional<Sc6ReplayNativeBridge> bridge_{};
    InputTimeline input_timeline_{
        Schema::replay_input_memory_budget / Schema::replay_input_entry_budget};
    Sc6CandidateCheckpointCapture checkpoint_capture_{};
    ReplayTimelineStatus timeline_status_{};
    std::uintptr_t timeline_manager_{};
    std::uintptr_t timeline_input_log_{};
    std::uint32_t timeline_thread_id_{};
    std::uint64_t timeline_session_generation_{};
};
}
}
