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

    const Snapshot* base{};
    const auto* exact = batch_entry_snapshots.FindExact(target);
    if (exact != nullptr && landing_batch->exit_coordinate == target)
        base = exact;
    else
        base = batch_entry_snapshots.FindNearestAtOrBefore(
            landing_batch->entry_coordinate);
    if (base == nullptr)
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

Status PlanReplayCorrection(
    FrameCoordinate earliest_changed,
    FrameCoordinate current,
    const NativeBatchTimeline& batches,
    const SnapshotStore& batch_entry_snapshots,
    std::uint64_t maximum_resimulation_distance,
    ReplayCorrectionPlan& output) noexcept
{
    output = {};
    if (earliest_changed.generation == 0
        || earliest_changed.generation != current.generation
        || earliest_changed.frame > current.frame
        || maximum_resimulation_distance == 0)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }

    const auto changed_membership = batches.FindCoordinate(earliest_changed);
    if (!changed_membership.has_value())
        return Status::failure(FailureCode::ContextUnavailable);
    const NativeBatchEnvelope* changed_batch =
        batches.GetBatch(changed_membership->batch_index);
    if (changed_batch == nullptr)
        return Status::failure(FailureCode::IdentityMismatch);
    const auto* base = batch_entry_snapshots.FindNearestAtOrBefore(
        changed_batch->entry_coordinate);
    if (base == nullptr)
        return Status::failure(FailureCode::MissingSnapshot);
    if (base->coordinate.generation != current.generation
        || base->coordinate.frame > changed_batch->entry_coordinate.frame)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    const auto first_batch_index = find_batch_starting_at(
        batches, base->coordinate, changed_membership->batch_index);
    if (!first_batch_index.has_value())
        return Status::failure(FailureCode::MissingSnapshot);

    const auto current_membership = batches.FindCoordinate(current);
    if (!current_membership.has_value())
        return Status::failure(FailureCode::ContextUnavailable);
    const NativeBatchEnvelope* final_batch =
        batches.GetBatch(current_membership->batch_index);
    if (final_batch == nullptr || final_batch->exit_coordinate != current
        || current_membership->offset_in_batch + 1
            != final_batch->coordinate_count
        || *first_batch_index > current_membership->batch_index)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    const std::uint64_t total_distance =
        current.frame - base->coordinate.frame;
    if (total_distance > maximum_resimulation_distance)
        return Status::failure(FailureCode::AdapterUnqualified);
    for (std::size_t index = *first_batch_index;
         index <= current_membership->batch_index; ++index)
    {
        const NativeBatchEnvelope* batch = batches.GetBatch(index);
        if (batch == nullptr
            || batch->entry_coordinate.generation != current.generation
            || batch->exit_coordinate.generation != current.generation)
        {
            return Status::failure(FailureCode::GenerationMismatch);
        }
    }

    output.earliest_changed = earliest_changed;
    output.current = current;
    output.resimulation_base = base->coordinate;
    output.first_batch_index = *first_batch_index;
    output.final_batch_index = current_membership->batch_index;
    output.resimulation_coordinates = total_distance;
    return Status::success();
}
}
