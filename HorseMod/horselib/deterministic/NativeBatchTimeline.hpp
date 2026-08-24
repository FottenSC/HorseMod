#pragma once

#include "Types.hpp"

#include <optional>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
struct NativeBatchEnvelope
{
    std::uint64_t batch_id{};
    FrameCoordinate entry_coordinate{};
    FrameCoordinate exit_coordinate{};
    float delta_seconds{};
    std::uint32_t native_frame_before{};
    std::uint32_t native_frame_after{};
    std::int32_t input_round_before{};
    std::int32_t input_round_after{};
    std::int32_t input_time_before{};
    std::int32_t input_time_after{};
    std::int32_t manager_round_cursor_before{};
    std::int32_t manager_round_cursor_after{};
    std::uint32_t manager_time_cursor_before{};
    std::uint32_t manager_time_cursor_after{};
    std::uint32_t coordinate_count{};
    std::uint32_t repeat_pending_coordinates{};
    std::uint32_t same_input_time_coordinates{};
    std::uint8_t main_state_before{};
    std::uint8_t main_state_after{};
    std::uint8_t round_state_before{};
    std::uint8_t round_state_after{};
    bool input_generation_changed{};
};

struct NativeBatchCoordinate
{
    FrameCoordinate coordinate{};
    std::size_t batch_index{};
    std::uint32_t offset_in_batch{};
};

enum class ResimulationBaseAction : std::uint8_t
{
    Retain,
    Capture,
    Invalid,
};

[[nodiscard]] ResimulationBaseAction PlanResimulationBase(
    std::optional<FrameCoordinate> previous,
    FrameCoordinate batch_entry,
    std::uint32_t maximum_batch_width,
    std::uint64_t maximum_resimulation_distance) noexcept;

class NativeBatchTimeline final
{
public:
    NativeBatchTimeline(
        std::size_t maximum_batches,
        std::size_t maximum_coordinates) noexcept;

    Status Append(
        const NativeBatchEnvelope& envelope,
        std::span<const FrameCoordinate> coordinates) noexcept;
    [[nodiscard]] std::optional<NativeBatchCoordinate> FindCoordinate(
        FrameCoordinate coordinate) const noexcept;
    [[nodiscard]] const NativeBatchEnvelope* GetBatch(
        std::size_t batch_index) const noexcept;
    [[nodiscard]] const NativeBatchCoordinate* GetBatchCoordinate(
        std::size_t batch_index, std::uint32_t offset_in_batch) const noexcept;
    [[nodiscard]] bool CanAppendBatch(
        std::size_t coordinate_count) const noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::size_t batch_count() const noexcept;
    [[nodiscard]] std::size_t coordinate_count() const noexcept;

private:
    [[nodiscard]] bool Validate(
        const NativeBatchEnvelope& envelope,
        std::span<const FrameCoordinate> coordinates) const noexcept;

    std::size_t maximum_batches_{};
    std::size_t maximum_coordinates_{};
    std::vector<NativeBatchEnvelope> batches_;
    std::vector<NativeBatchCoordinate> coordinates_;
};
}
