#include "NativeBatchTimeline.hpp"

#include "Schema.hpp"

#include <algorithm>
#include <cstring>

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
    std::uint64_t maximum_resimulation_distance,
    std::optional<FrameCoordinate> required_checkpoint) noexcept
{
    if (batch_entry.generation == 0 || maximum_batch_width == 0
        || maximum_batch_width > maximum_resimulation_distance)
    {
        return ResimulationBaseAction::Invalid;
    }
    if (required_checkpoint.has_value())
    {
        if (required_checkpoint->generation != batch_entry.generation
            || required_checkpoint->frame < batch_entry.frame)
            return ResimulationBaseAction::Invalid;
        if (*required_checkpoint == batch_entry)
            return ResimulationBaseAction::Capture;
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

namespace
{
bool ValidateIntervalActors(const NativeBatchEnvelope& envelope) noexcept
{
    if (envelope.consumers_before_count > envelope.consumers_before.size()
        || envelope.producers_before_count > envelope.producers_before.size()) return false;
    std::uint8_t preceding_consumers{};
    for (std::size_t index = 0; index < envelope.producers_before_count; ++index)
    {
        const auto& producer = envelope.producers_before[index];
        if (!producer.valid || producer.preceding_consumers < preceding_consumers
            || producer.preceding_consumers > envelope.consumers_before_count
            || producer.native_frame_before != envelope.native_frame_before
            || producer.native_frame_after != producer.native_frame_before) return false;
        preceding_consumers = producer.preceding_consumers;
    }
    return true;
}

bool ValidateBattleAudioSourceSpans(
    const NativeBatchEnvelope& envelope) noexcept
{
    const auto in_range = [](std::uint8_t value, std::uint8_t first,
                              std::uint8_t count) noexcept {
        return value >= first
            && static_cast<std::size_t>(value)
                < static_cast<std::size_t>(first) + count;
    };
    for (std::size_t index = 0;
         index < envelope.battle_audio_source_journal_count; ++index)
    {
        const auto& source = envelope.battle_audio_source_journal[index];
        const auto begin = static_cast<std::size_t>(
            source.first_presentation_order);
        const auto end = begin + source.presentation_order_count;
        if (source.presentation_order_count == 0
            || source.presentation_order_count
                != 1u + source.dispatch_count + source.remap_count
                    + source.blueprint_count + source.terminal_count
            || static_cast<std::size_t>(source.first_dispatch)
                    + source.dispatch_count
                > envelope.battle_audio_journal_count
            || static_cast<std::size_t>(source.first_remap)
                    + source.remap_count
                > envelope.battle_audio_remap_journal_count
            || static_cast<std::size_t>(source.first_blueprint)
                    + source.blueprint_count
                > envelope.battle_audio_blueprint_journal_count
            || static_cast<std::size_t>(source.first_terminal)
                    + source.terminal_count
                > envelope.audio_terminal_journal_count
            || end > envelope.presentation_order_journal_count
            || envelope.presentation_order_journal[begin].family
                != PresentationEventFamily::BattleAudioSource
            || envelope.presentation_order_journal[begin].family_index != index)
            return false;
        for (std::size_t order = begin + 1; order < end; ++order)
        {
            const auto& entry = envelope.presentation_order_journal[order];
            if (!((entry.family == PresentationEventFamily::BattleAudioDispatch
                        && in_range(entry.family_index, source.first_dispatch,
                            source.dispatch_count))
                    || (entry.family == PresentationEventFamily::BattleAudioRemap
                        && in_range(entry.family_index, source.first_remap,
                            source.remap_count))
                    || (entry.family
                            == PresentationEventFamily::BattleAudioBlueprint
                        && in_range(entry.family_index, source.first_blueprint,
                            source.blueprint_count))
                    || (entry.family == PresentationEventFamily::AudioTerminal
                        && in_range(entry.family_index, source.first_terminal,
                            source.terminal_count))))
                return false;
        }
    }
    return true;
}
}

NativeBatchTimeline::NativeBatchTimeline(
    std::size_t maximum_batches,
    std::size_t maximum_coordinates) noexcept
    : maximum_batches_(maximum_batches),
      maximum_coordinates_(maximum_coordinates)
{
    // Allocate on demand: constructing an inactive legacy coordinator must
    // not consume its 224 MiB retention ceiling beside the resumable core.
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
        const auto reserve = [](auto& storage, std::size_t needed, std::size_t maximum) {
            if (needed <= storage.capacity()) return;
            const auto capacity = storage.capacity();
            const auto grown = capacity ? capacity + (std::min)(capacity, maximum - capacity)
                                       : (std::min)(std::size_t{64}, maximum);
            storage.reserve((std::max)(needed, grown));
        };
        reserve(batches_, batch_index + 1, maximum_batches_);
        reserve(coordinates_, coordinate_size + coordinates.size(), maximum_coordinates_);
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

Status NativeBatchTimeline::ReplaceBatch(
    std::size_t batch_index,
    const NativeBatchEnvelope& expected,
    const NativeBatchEnvelope& replacement) noexcept
{
    if (batch_index >= batches_.size())
        return Status::failure(FailureCode::MissingSnapshot);
    auto& current = batches_[batch_index];
    if (std::memcmp(&current, &expected, sizeof(current)) != 0)
        return Status::failure(FailureCode::IdentityMismatch);
    const bool immutable_match =
        replacement.batch_id == expected.batch_id
        && replacement.preceding_outer_batch_id == expected.preceding_outer_batch_id
        && replacement.entry_coordinate == expected.entry_coordinate
        && replacement.exit_coordinate == expected.exit_coordinate
        && replacement.delta_seconds == expected.delta_seconds
        && replacement.native_frame_before == expected.native_frame_before
        && replacement.native_frame_after == expected.native_frame_after
        && replacement.input_round_before == expected.input_round_before
        && replacement.input_round_after == expected.input_round_after
        && replacement.input_time_before == expected.input_time_before
        && replacement.input_time_after == expected.input_time_after
        && replacement.manager_round_cursor_before
            == expected.manager_round_cursor_before
        && replacement.manager_round_cursor_after
            == expected.manager_round_cursor_after
        && replacement.manager_time_cursor_before
            == expected.manager_time_cursor_before
        && replacement.manager_time_cursor_after
            == expected.manager_time_cursor_after
        && replacement.coordinate_count == expected.coordinate_count
        && replacement.main_state_before == expected.main_state_before
        && replacement.main_state_after == expected.main_state_after
        && replacement.round_state_before == expected.round_state_before
        && replacement.round_state_after == expected.round_state_after
        && replacement.input_generation_changed
            == expected.input_generation_changed
        && replacement.camera_source_frame.session_generation
            == expected.camera_source_frame.session_generation
        && replacement.camera_source_frame.round_generation
            == expected.camera_source_frame.round_generation;
    const auto expected_order =
        static_cast<std::size_t>(replacement.battle_audio_dispatches)
        + replacement.battle_audio_source_calls
        + replacement.battle_audio_remap_calls
        + replacement.battle_audio_blueprint_calls
        + replacement.battle_audio_stop_all_calls
        + replacement.audio_terminal_calls
        + replacement.stage_wall_calls + replacement.stage_barrier_calls
        + replacement.stage_dispatch_calls + replacement.particle_spawn_calls;
    if (!immutable_match || !ValidateIntervalActors(replacement)
        || replacement.stage_signature_failures != 0
        || replacement.particle_signature_failures != 0
        || replacement.camera_signature_failures != 0
        || replacement.presentation_order_failures != 0
        || replacement.battle_audio_journal_count
            != replacement.battle_audio_dispatches
        || replacement.battle_audio_source_journal_count
            != replacement.battle_audio_source_calls
        || replacement.battle_audio_remap_journal_count
            != replacement.battle_audio_remap_calls
        || replacement.battle_audio_blueprint_journal_count
            != replacement.battle_audio_blueprint_calls
        || replacement.battle_audio_stop_all_journal_count
            != replacement.battle_audio_stop_all_calls
        || replacement.audio_terminal_journal_count
            != replacement.audio_terminal_calls
        || replacement.stage_wall_journal_count
            != replacement.stage_wall_calls
        || replacement.stage_barrier_journal_count
            != replacement.stage_barrier_calls
        || replacement.stage_dispatch_journal_count
            != replacement.stage_dispatch_calls
        || replacement.particle_spawn_journal_count
            != replacement.particle_spawn_calls
        || replacement.presentation_order_journal_count != expected_order
        || expected_order > replacement.presentation_order_journal.size()
        || !ValidateBattleAudioSourceSpans(replacement))
        return Status::failure(FailureCode::IdentityMismatch);
    std::array<std::uint8_t,
        static_cast<std::size_t>(PresentationEventFamily::AudioTerminal)>
        next_family_index{};
    for (std::size_t index = 0; index < expected_order; ++index)
    {
        const auto& entry = replacement.presentation_order_journal[index];
        const auto family = static_cast<std::uint8_t>(entry.family);
        if (family == 0 || family > next_family_index.size()
            || entry.source_offset > replacement.coordinate_count
            || entry.family_index != next_family_index[family - 1]++)
            return Status::failure(FailureCode::IdentityMismatch);
    }
    current = replacement;
    return Status::success();
}

Status NativeBatchTimeline::ValidateSuffixReplacement(
    std::span<const NativeBatchEnvelope> expected,
    std::span<const NativeBatchEnvelope> replacements,
    std::span<const FrameCoordinate> coordinates) const noexcept
{
    if (expected.empty() || expected.size() != replacements.size())
        return Status::failure(FailureCode::InvalidConfiguration);
    const auto first = std::lower_bound(batches_.begin(), batches_.end(),
        expected.front().batch_id,
        [](const NativeBatchEnvelope& batch, std::uint64_t id) {
            return batch.batch_id < id;
        });
    if (first == batches_.end() || first->batch_id != expected.front().batch_id)
        return Status::failure(FailureCode::MissingSnapshot);
    if (expected.size() != static_cast<std::size_t>(batches_.end() - first))
        return Status::failure(FailureCode::InvalidConfiguration);
    const auto first_index = static_cast<std::size_t>(first - batches_.begin());
    const auto prefix_end = std::lower_bound(coordinates_.begin(), coordinates_.end(),
        first_index, [](const NativeBatchCoordinate& value, std::size_t index) {
            return value.batch_index < index;
        });
    const auto prefix_count = static_cast<std::size_t>(prefix_end - coordinates_.begin());
    if (coordinates.size() > maximum_coordinates_ - prefix_count)
        return Status::failure(FailureCode::CapacityExceeded);
    const auto generation = expected.front().entry_coordinate.generation;
    if (generation == 0
        || replacements.front().entry_coordinate != expected.front().entry_coordinate)
        return Status::failure(FailureCode::IdentityMismatch);
    const NativeBatchEnvelope* previous = first_index == 0 ? nullptr : &batches_[first_index - 1];
    std::optional<FrameCoordinate> previous_coordinate = prefix_count == 0
        ? std::nullopt : std::optional{coordinates_[prefix_count - 1].coordinate};
    std::size_t offset{};
    for (std::size_t index = 0; index < replacements.size(); ++index)
    {
        const auto& old = expected[index];
        const auto& replacement = replacements[index];
        // A correction cannot cross an allocation/generation barrier. Native
        // geometry inside that ownership epoch is reconstructed, not copied.
        if (std::memcmp(&batches_[first_index + index], &old, sizeof(old)) != 0
            || !old.completed_identity_valid || !replacement.completed_identity_valid
            || replacement.batch_id != old.batch_id
            || replacement.preceding_outer_batch_id != old.preceding_outer_batch_id
            || replacement.delta_seconds != old.delta_seconds
            || old.input_generation_changed || replacement.input_generation_changed
            || old.entry_coordinate.generation != generation
            || old.exit_coordinate.generation != generation
            || replacement.entry_coordinate.generation != generation
            || replacement.exit_coordinate.generation != generation
            || replacement.camera_source_frame.session_generation
                != old.camera_source_frame.session_generation
            || replacement.camera_source_frame.round_generation
                != old.camera_source_frame.round_generation
            || replacement.stage_signature_failures != 0
            || replacement.particle_signature_failures != 0
            || replacement.camera_signature_failures != 0)
            return Status::failure(FailureCode::IdentityMismatch);
        const auto count = static_cast<std::size_t>(replacement.coordinate_count);
        if (count > coordinates.size() - offset
            || replacement.exit_coordinate.frame < replacement.entry_coordinate.frame
            || replacement.exit_coordinate.frame - replacement.entry_coordinate.frame != count
            || !ValidateAfter(replacement, coordinates.subspan(offset, count),
                previous, previous_coordinate))
            return Status::failure(FailureCode::IdentityMismatch);
        for (std::size_t native = 0; native < count; ++native)
        {
            // The subtraction above proves this addition cannot overflow.
            if (coordinates[offset + native] != FrameCoordinate{
                    generation, replacement.entry_coordinate.frame + native + 1})
                return Status::failure(FailureCode::IdentityMismatch);
        }
        if (count != 0) previous_coordinate = coordinates[offset + count - 1];
        offset += count;
        previous = &replacement;
    }
    return offset == coordinates.size() ? Status::success()
        : Status::failure(FailureCode::InvalidConfiguration);
}

void NativeBatchTimeline::CommitValidatedSuffixReplacement(
    std::span<const NativeBatchEnvelope> replacements,
    std::span<const FrameCoordinate> coordinates) noexcept
{
    const auto first = std::lower_bound(batches_.begin(), batches_.end(),
        replacements.front().batch_id,
        [](const NativeBatchEnvelope& batch, std::uint64_t id) {
            return batch.batch_id < id;
        });
    const auto first_index = static_cast<std::size_t>(first - batches_.begin());
    const auto prefix_end = std::lower_bound(coordinates_.begin(), coordinates_.end(),
        first_index, [](const NativeBatchCoordinate& value, std::size_t index) {
            return value.batch_index < index;
        });
    coordinates_.resize(static_cast<std::size_t>(prefix_end - coordinates_.begin()));
    std::size_t offset{};
    for (std::size_t index = 0; index < replacements.size(); ++index)
    {
        batches_[first_index + index] = replacements[index];
        for (std::uint32_t native = 0; native < replacements[index].coordinate_count; ++native)
            coordinates_.push_back({coordinates[offset++], first_index + index, native});
    }
}

bool NativeBatchTimeline::CanAppendBatch(
    std::size_t coordinate_count) const noexcept
{
    return batches_.size() < maximum_batches_
        && coordinate_count <= maximum_coordinates_ - coordinates_.size();
}

void NativeBatchTimeline::DiscardBefore(FrameCoordinate minimum) noexcept
{
    const auto first_batch = std::lower_bound(
        batches_.begin(), batches_.end(), minimum,
        [](const NativeBatchEnvelope& batch, FrameCoordinate value) {
            return batch.exit_coordinate < value;
        });
    const auto removed = static_cast<std::size_t>(
        first_batch - batches_.begin());
    if (removed == 0) return;

    batches_.erase(batches_.begin(), first_batch);
    const auto first_coordinate = std::lower_bound(
        coordinates_.begin(), coordinates_.end(), removed,
        [](const NativeBatchCoordinate& coordinate, std::size_t batch) {
            return coordinate.batch_index < batch;
        });
    coordinates_.erase(coordinates_.begin(), first_coordinate);
    for (auto& coordinate : coordinates_)
        coordinate.batch_index -= removed;
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
    return ValidateAfter(envelope, coordinates,
        batches_.empty() ? nullptr : &batches_.back(),
        coordinates_.empty() ? std::nullopt
            : std::optional{coordinates_.back().coordinate});
}

bool NativeBatchTimeline::ValidateAfter(
    const NativeBatchEnvelope& envelope,
    std::span<const FrameCoordinate> coordinates,
    const NativeBatchEnvelope* previous,
    std::optional<FrameCoordinate> previous_coordinate) noexcept
{
    const auto expected_presentation_order =
        static_cast<std::size_t>(envelope.battle_audio_dispatches)
        + envelope.battle_audio_source_calls
        + envelope.battle_audio_remap_calls
        + envelope.battle_audio_blueprint_calls
        + envelope.battle_audio_stop_all_calls
        + envelope.audio_terminal_calls
        + envelope.stage_wall_calls + envelope.stage_barrier_calls
        + envelope.stage_dispatch_calls + envelope.particle_spawn_calls;
    if (envelope.batch_id == 0
        || !ValidateIntervalActors(envelope)
        || envelope.coordinate_count != coordinates.size()
        || envelope.battle_audio_journal_count
            != envelope.battle_audio_dispatches
        || envelope.battle_audio_journal_count
            > envelope.battle_audio_journal.size()
        || envelope.battle_audio_source_journal_count
            != envelope.battle_audio_source_calls
        || envelope.battle_audio_source_journal_count
            > envelope.battle_audio_source_journal.size()
        || envelope.battle_audio_remap_journal_count
            != envelope.battle_audio_remap_calls
        || envelope.battle_audio_remap_journal_count
            > envelope.battle_audio_remap_journal.size()
        || envelope.battle_audio_blueprint_journal_count
            != envelope.battle_audio_blueprint_calls
        || envelope.battle_audio_blueprint_journal_count
            > envelope.battle_audio_blueprint_journal.size()
        || envelope.battle_audio_stop_all_journal_count
            != envelope.battle_audio_stop_all_calls
        || envelope.battle_audio_stop_all_journal_count
            > envelope.battle_audio_stop_all_journal.size()
        || envelope.audio_terminal_journal_count
            != envelope.audio_terminal_calls
        || envelope.audio_terminal_journal_count
            > envelope.audio_terminal_journal.size()
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
            > envelope.particle_spawn_journal.size()
        || envelope.presentation_order_failures != 0
        || envelope.presentation_order_journal_count
            != expected_presentation_order
        || envelope.presentation_order_journal_count
            > envelope.presentation_order_journal.size())
    {
        return false;
    }
    std::array<std::uint8_t,
        static_cast<std::size_t>(PresentationEventFamily::AudioTerminal)>
        next_family_index{};
    for (std::size_t index = 0;
         index < envelope.presentation_order_journal_count; ++index)
    {
        const auto& entry = envelope.presentation_order_journal[index];
        const auto family = static_cast<std::uint8_t>(entry.family);
        const bool source_in_batch =
            entry.source_offset <= envelope.coordinate_count;
        if (family == 0 || family > next_family_index.size()
            || !source_in_batch
            || entry.family_index != next_family_index[family - 1]++)
            return false;
    }
    if (!ValidateBattleAudioSourceSpans(envelope)) return false;
    for (std::size_t index = 0;
         index < envelope.battle_audio_remap_journal_count; ++index)
        if (envelope.battle_audio_remap_journal[index].handler_slot
            >= maximum_battle_audio_handlers)
            return false;
    for (std::size_t index = 0;
         index < envelope.audio_terminal_journal_count; ++index)
        if (!envelope.audio_terminal_journal[index].valid()) return false;
    const auto valid_stage = [&](const StagePresentationJournalEntry& entry,
                                 std::uint8_t canonical_size) noexcept {
        return entry.owner_logical_id != 0 && entry.payload_size <= 12
            && entry.canonical_before_size == canonical_size
            && static_cast<std::size_t>(entry.first_particle)
                    + entry.particle_count
                <= envelope.particle_spawn_journal_count;
    };
    for (std::size_t index = 0; index < envelope.stage_wall_journal_count;
         ++index)
        if (!valid_stage(envelope.stage_wall_journal[index], 12)) return false;
    for (std::size_t index = 0; index < envelope.stage_barrier_journal_count;
         ++index)
        if (!valid_stage(envelope.stage_barrier_journal[index], 4)) return false;
    if (previous != nullptr
        && (envelope.batch_id <= previous->batch_id
            || (envelope.entry_coordinate != previous->exit_coordinate
                && envelope.entry_coordinate.generation
                    <= previous->exit_coordinate.generation)))
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
    return !previous_coordinate.has_value()
        || coordinates.front() > *previous_coordinate;
}
}
