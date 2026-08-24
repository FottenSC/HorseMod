#include "ReplaySeekPlanner.hpp"

namespace Horse::Deterministic
{
namespace
{
std::optional<std::size_t> find_batch_starting_at(
    const NativeBatchTimeline& batches,
    FrameCoordinate coordinate,
    std::size_t last_index) noexcept
{
    for (std::size_t index = 0; index <= last_index; ++index)
    {
        const NativeBatchEnvelope* batch = batches.GetBatch(index);
        if (batch != nullptr && batch->entry_coordinate == coordinate)
            return index;
    }
    return std::nullopt;
}
}

Status PlanReplaySeek(
    FrameCoordinate target,
    const NativeBatchTimeline& batches,
    const SnapshotStore& batch_entry_snapshots,
    std::uint64_t maximum_resimulation_distance,
    ReplaySeekPlan& output) noexcept
{
    output = {};
    if (target.generation == 0 || maximum_resimulation_distance == 0)
        return Status::failure(FailureCode::InvalidConfiguration);

    const auto membership = batches.FindCoordinate(target);
    if (!membership.has_value())
        return Status::failure(FailureCode::ContextUnavailable);
    const NativeBatchEnvelope* landing_batch =
        batches.GetBatch(membership->batch_index);
    if (landing_batch == nullptr
        || landing_batch->coordinate_count <= membership->offset_in_batch)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }

    std::optional<Snapshot> base;
    const auto exact = batch_entry_snapshots.Load(target);
    if (exact.has_value() && landing_batch->exit_coordinate == target)
        base = exact;
    else
        base = batch_entry_snapshots.NearestAtOrBefore(
            landing_batch->entry_coordinate);
    if (!base.has_value())
        return Status::failure(FailureCode::MissingSnapshot);
    if (base->coordinate.generation != target.generation
        || base->coordinate.frame > target.frame)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }

    const std::uint64_t distance = target.frame - base->coordinate.frame;
    if (distance > maximum_resimulation_distance)
        return Status::failure(FailureCode::AdapterUnqualified);

    std::size_t first_batch_index = membership->batch_index + 1;
    const bool requires_replay = base->coordinate != target;
    if (requires_replay)
    {
        const auto first = find_batch_starting_at(
            batches, base->coordinate, membership->batch_index);
        if (!first.has_value())
            return Status::failure(FailureCode::MissingSnapshot);
        first_batch_index = *first;
        for (std::size_t index = first_batch_index;
             index <= membership->batch_index; ++index)
        {
            const NativeBatchEnvelope* batch = batches.GetBatch(index);
            if (batch == nullptr
                || batch->entry_coordinate.generation != target.generation
                || batch->exit_coordinate.generation != target.generation)
            {
                return Status::failure(FailureCode::GenerationMismatch);
            }
        }
    }

    output.target = target;
    output.resimulation_base = base->coordinate;
    output.first_batch_index = first_batch_index;
    output.landing_batch_index = membership->batch_index;
    output.landing_offset_in_batch = membership->offset_in_batch;
    output.coordinates_after_landing = landing_batch->coordinate_count
        - membership->offset_in_batch - 1;
    output.resimulation_coordinates = distance;
    output.landing_requires_batch_replay = requires_replay;
    return Status::success();
}
}
