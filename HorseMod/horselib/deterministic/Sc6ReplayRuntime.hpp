#pragma once

#include "DeterministicHookSet.hpp"
#include "InputTimeline.hpp"
#include "NativeBatchTimeline.hpp"
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
    std::uint64_t repeat_requests{};
    std::uint64_t same_native_time_coordinates{};
    std::uint64_t cursor_mismatches{};
    std::uint64_t native_batches{};
    std::uint64_t zero_coordinate_batches{};
    std::uint64_t multi_coordinate_batches{};
    std::uint64_t batch_repeat_coordinates{};
    std::uint64_t batch_same_input_time_coordinates{};
    std::uint64_t batch_input_generation_changes{};
    std::uint64_t batch_frame_accounting_mismatches{};
    std::uint32_t maximum_coordinates_per_batch{};
    std::uint32_t maximum_input_delta_per_batch{};
    std::uint32_t round_state_frame{};
    std::int32_t unpause_countdown{};
    std::uint8_t pending_move_state{};
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
    Status ObserveOuterTick(const OuterTickObservation& observation) noexcept;
    void ObserveReplayExit() noexcept;
    [[nodiscard]] ReplayTimelineStatus timeline_status() const noexcept;
    [[nodiscard]] const InputTimeline& input_timeline() const noexcept;
    [[nodiscard]] const NativeBatchTimeline& batch_timeline() const noexcept;

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
    NativeBatchTimeline batch_timeline_{
        (Schema::replay_native_batch_memory_budget / 2)
            / Schema::replay_native_batch_entry_budget,
        (Schema::replay_native_batch_memory_budget / 2)
            / Schema::replay_native_batch_coordinate_budget};
    Sc6CandidateCheckpointCapture checkpoint_capture_{};
    ReplayTimelineStatus timeline_status_{};
    std::uintptr_t timeline_manager_{};
    std::uintptr_t timeline_input_log_{};
    std::uint32_t timeline_thread_id_{};
    std::uint64_t timeline_session_generation_{};
    std::uint64_t pending_batch_id_{};
    FrameCoordinate pending_batch_entry_{};
    std::vector<FrameCoordinate> pending_batch_coordinates_{};
};
}
}
