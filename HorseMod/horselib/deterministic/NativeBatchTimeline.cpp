#include "NativeBatchTimeline.hpp"

#include "Schema.hpp"

#include <algorithm>

namespace Horse::Deterministic
{
static_assert(
    sizeof(NativeBatchEnvelope) <= Schema::replay_native_batch_entry_budget);
static_assert(
    sizeof(NativeBatchCoordinate)
    <= Schema::replay_native_batch_coordinate_budget);

ResimulationBaseAction PlanResimulationBase(
    std::optional<FrameCoordinate> previous,
    FrameCoordinate batch_entry,
    std::uint32_t maximum_batch_width,
    std::uint64_t maximum_resimulation_distance) noexcept
{
    if (batch_entry.generation == 0 || maximum_batch_width == 0
        || maximum_batch_width > maximum_resimulation_distance)
    {
        return ResimulationBaseAction::Invalid;
    }
    if (!previous.has_value()
        || previous->generation != batch_entry.generation)
    {
        return ResimulationBaseAction::Capture;
    }
    if (batch_entry.frame < previous->frame)
        return ResimulationBaseAction::Invalid;
    const std::uint64_t distance = batch_entry.frame - previous->frame;
    return distance
            > maximum_resimulation_distance - maximum_batch_width
        ? ResimulationBaseAction::Capture
        : ResimulationBaseAction::Retain;
}

NativeBatchTimeline::NativeBatchTimeline(
    std::size_t maximum_batches,
    std::size_t maximum_coordinates) noexcept
    : maximum_batches_(maximum_batches),
      maximum_coordinates_(maximum_coordinates)
{
    try
    {
        batches_.reserve(maximum_batches_);
        coordinates_.reserve(maximum_coordinates_);
    }
    catch (...)
    {
        maximum_batches_ = 0;
        maximum_coordinates_ = 0;
        batches_.clear();
        coordinates_.clear();
    }
}

Status NativeBatchTimeline::Append(
    const NativeBatchEnvelope& envelope,
    std::span<const FrameCoordinate> coordinates) noexcept
{
    if (!Validate(envelope, coordinates))
        return Status::failure(FailureCode::IdentityMismatch);
    if (batches_.size() >= maximum_batches_
        || coordinates.size() > maximum_coordinates_ - coordinates_.size())
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }

    const std::size_t batch_index = batches_.size();
    const std::size_t coordinate_size = coordinates_.size();
    try
    {
        for (std::size_t offset = 0; offset < coordinates.size(); ++offset)
        {
            coordinates_.push_back(
                {coordinates[offset], batch_index,
                    static_cast<std::uint32_t>(offset)});
        }
        batches_.push_back(envelope);
    }
    catch (...)
    {
        coordinates_.resize(coordinate_size);
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

std::optional<NativeBatchCoordinate> NativeBatchTimeline::FindCoordinate(
    FrameCoordinate coordinate) const noexcept
{
    const auto found = std::lower_bound(
        coordinates_.begin(), coordinates_.end(), coordinate,
        [](const NativeBatchCoordinate& entry, FrameCoordinate value)
        {
            return entry.coordinate < value;
        });
    if (found == coordinates_.end() || found->coordinate != coordinate)
        return std::nullopt;
    return *found;
}

const NativeBatchEnvelope* NativeBatchTimeline::GetBatch(
    std::size_t batch_index) const noexcept
{
    return batch_index < batches_.size() ? &batches_[batch_index] : nullptr;
}

const NativeBatchCoordinate* NativeBatchTimeline::GetBatchCoordinate(
    std::size_t batch_index, std::uint32_t offset_in_batch) const noexcept
{
    const auto found = std::lower_bound(
        coordinates_.begin(), coordinates_.end(),
        std::pair{batch_index, offset_in_batch},
        [](const NativeBatchCoordinate& entry,
            const std::pair<std::size_t, std::uint32_t>& value)
        {
            return entry.batch_index < value.first
                || (entry.batch_index == value.first
                    && entry.offset_in_batch < value.second);
        });
    return found != coordinates_.end() && found->batch_index == batch_index
            && found->offset_in_batch == offset_in_batch
        ? &*found : nullptr;
}

bool NativeBatchTimeline::CanAppendBatch(
    std::size_t coordinate_count) const noexcept
{
    return batches_.size() < maximum_batches_
        && coordinate_count <= maximum_coordinates_ - coordinates_.size();
}

void NativeBatchTimeline::Clear() noexcept
{
    batches_.clear();
    coordinates_.clear();
}

std::size_t NativeBatchTimeline::batch_count() const noexcept
{
    return batches_.size();
}

std::size_t NativeBatchTimeline::coordinate_count() const noexcept
{
    return coordinates_.size();
}

bool NativeBatchTimeline::Validate(
    const NativeBatchEnvelope& envelope,
    std::span<const FrameCoordinate> coordinates) const noexcept
{
    if (envelope.batch_id == 0
        || envelope.coordinate_count != coordinates.size()
        || envelope.battle_audio_blueprint_journal_count
            != envelope.battle_audio_blueprint_calls
        || envelope.battle_audio_blueprint_journal_count
            > envelope.battle_audio_blueprint_journal.size()
        || envelope.battle_audio_stop_all_journal_count
            != envelope.battle_audio_stop_all_calls
        || envelope.battle_audio_stop_all_journal_count
            > envelope.battle_audio_stop_all_journal.size()
        || envelope.stage_wall_journal_count != envelope.stage_wall_calls
        || envelope.stage_wall_journal_count
            > envelope.stage_wall_journal.size()
        || envelope.stage_barrier_journal_count
            != envelope.stage_barrier_calls
        || envelope.stage_barrier_journal_count
            > envelope.stage_barrier_journal.size()
        || envelope.stage_dispatch_journal_count
            != envelope.stage_dispatch_calls
        || envelope.stage_dispatch_journal_count
            > envelope.stage_dispatch_journal.size()
        || envelope.particle_spawn_journal_count
            != envelope.particle_spawn_calls
        || envelope.particle_spawn_journal_count
            > envelope.particle_spawn_journal.size())
    {
        return false;
    }
    if (!batches_.empty()
        && (envelope.batch_id <= batches_.back().batch_id
            || (envelope.entry_coordinate != batches_.back().exit_coordinate
                && envelope.entry_coordinate.generation
                    <= batches_.back().exit_coordinate.generation)))
    {
        return false;
    }
    if (coordinates.empty())
        return envelope.entry_coordinate == envelope.exit_coordinate;
    if (coordinates.front() <= envelope.entry_coordinate
        || coordinates.back() != envelope.exit_coordinate)
    {
        return false;
    }
    for (std::size_t index = 1; index < coordinates.size(); ++index)
    {
        if (coordinates[index] <= coordinates[index - 1])
            return false;
    }
    return coordinates_.empty()
        || coordinates.front() > coordinates_.back().coordinate;
}
}
