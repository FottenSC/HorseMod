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
    // Populated on failure so qualification can distinguish an indexed
    // checkpoint whose payload identity drifted from a cross-generation
    // native batch.  These fields are diagnostic only and never authorize a
    // restore.
    std::uint32_t failure_stage{};
    std::size_t failure_batch_index{};
    FrameCoordinate failure_base{};
    FrameCoordinate failure_entry{};
    FrameCoordinate failure_exit{};
};

struct ReplayCorrectionPlan
{
    FrameCoordinate earliest_changed{};
    FrameCoordinate current{};
    FrameCoordinate resimulation_base{};
    std::size_t first_batch_index{};
    std::size_t final_batch_index{};
    std::uint64_t resimulation_coordinates{};
};

[[nodiscard]] Status PlanReplaySeek(
    FrameCoordinate target,
    const NativeBatchTimeline& batches,
    const SnapshotStore& batch_entry_snapshots,
    std::uint64_t maximum_resimulation_distance,
    ReplaySeekPlan& output) noexcept;

[[nodiscard]] Status PlanReplayCorrection(
    FrameCoordinate earliest_changed,
    FrameCoordinate current,
    const NativeBatchTimeline& batches,
    const SnapshotStore& batch_entry_snapshots,
    std::uint64_t maximum_resimulation_distance,
    ReplayCorrectionPlan& output) noexcept;
}
