#pragma once

#include "NativeBatchTimeline.hpp"
#include "SnapshotStore.hpp"

namespace Horse::Deterministic
{
struct ReplaySeekPlan
{
    FrameCoordinate target{};
    FrameCoordinate resimulation_base{};
    std::size_t first_batch_index{};
    std::size_t landing_batch_index{};
    std::uint32_t landing_offset_in_batch{};
    std::uint32_t coordinates_after_landing{};
    std::uint64_t resimulation_coordinates{};
    bool landing_requires_batch_replay{};
};

[[nodiscard]] Status PlanReplaySeek(
    FrameCoordinate target,
    const NativeBatchTimeline& batches,
    const SnapshotStore& batch_entry_snapshots,
    std::uint64_t maximum_resimulation_distance,
    ReplaySeekPlan& output) noexcept;
}
