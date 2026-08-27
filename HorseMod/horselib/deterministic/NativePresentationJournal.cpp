#include "NativePresentationJournal.hpp"

#include "AudioPresentation.hpp"
#include "StagePresentation.hpp"

#include <array>
#include <limits>

namespace Horse::Deterministic
{
Status BuildNativeAudioPresentation(const NativeBatchEnvelope& batch,
    std::span<PresentationEvent> output, std::size_t& output_count) noexcept
{
    output_count = 0;
    const bool generation_transition = batch.exit_coordinate.generation
        != batch.entry_coordinate.generation;
    if (batch.entry_coordinate.generation == 0
        || batch.exit_coordinate.generation == 0
        || (generation_transition
            && batch.exit_coordinate.generation
                != batch.entry_coordinate.generation + 1)
        || batch.exit_coordinate.frame < batch.entry_coordinate.frame
        || batch.audio_terminal_calls != batch.audio_terminal_journal_count
        || batch.audio_terminal_journal_count
            > batch.audio_terminal_journal.size()
        || batch.battle_audio_blueprint_calls
            != batch.battle_audio_blueprint_journal_count
        || batch.battle_audio_blueprint_journal_count
            > batch.battle_audio_blueprint_journal.size()
        || batch.stage_wall_calls != batch.stage_wall_journal_count
        || batch.stage_wall_journal_count > batch.stage_wall_journal.size()
        || batch.stage_barrier_calls != batch.stage_barrier_journal_count
        || batch.stage_barrier_journal_count
            > batch.stage_barrier_journal.size()
        || batch.particle_spawn_calls != batch.particle_spawn_journal_count
        || batch.particle_spawn_journal_count
            > batch.particle_spawn_journal.size()
        || batch.presentation_order_failures != 0
        || batch.presentation_order_journal_count
            > batch.presentation_order_journal.size())
        return Status::failure(FailureCode::ProtocolMismatch);

    const auto required = static_cast<std::size_t>(
        batch.audio_terminal_journal_count)
        + batch.battle_audio_blueprint_journal_count
        + batch.stage_wall_journal_count
        + batch.stage_barrier_journal_count;
    if (output.size() < required)
        return Status::failure(FailureCode::CapacityExceeded);
    std::array<bool, maximum_audio_terminal_journal_events> seen_terminal{};
    std::array<bool, maximum_battle_audio_blueprint_journal_events>
        seen_blueprint{};
    std::array<bool, maximum_stage_presentation_journal_events> seen_wall{};
    std::array<bool, maximum_stage_presentation_journal_events> seen_barrier{};
    std::array<bool, maximum_particle_presentation_journal_events>
        claimed_particle{};
    for (std::size_t order_index = 0;
         order_index < batch.presentation_order_journal_count; ++order_index)
    {
        const auto& order = batch.presentation_order_journal[order_index];
        const bool terminal = order.family
            == PresentationEventFamily::AudioTerminal;
        const bool blueprint = order.family
            == PresentationEventFamily::BattleAudioBlueprint;
        const bool wall = order.family == PresentationEventFamily::StageWall;
        const bool barrier = order.family
            == PresentationEventFamily::StageBarrier;
        if (!terminal && !blueprint && !wall && !barrier) continue;
        if (batch.entry_coordinate.frame
                > (std::numeric_limits<std::uint64_t>::max)()
                    - order.source_offset)
            return Status::failure(FailureCode::ProtocolMismatch);
        const auto source_frame = batch.entry_coordinate.frame
            + order.source_offset;
        if (source_frame > batch.exit_coordinate.frame)
            return Status::failure(FailureCode::ProtocolMismatch);
        // ObserveFrame advances the round generation at the native fencepost.
        // In the one outer batch that straddles it, pre-fencepost events retain
        // the entry generation while events at the exit frame belong to the
        // new generation. Runtime evidence shows the split as offset 0 versus
        // offset 1; no event may name an intermediate future generation.
        const FrameCoordinate source{
            generation_transition && source_frame == batch.exit_coordinate.frame
                ? batch.exit_coordinate.generation
                : batch.entry_coordinate.generation,
            source_frame};
        Status status{};
        if (terminal)
        {
            if (order.family_index >= batch.audio_terminal_journal_count
                || seen_terminal[order.family_index])
                return Status::failure(FailureCode::ProtocolMismatch);
            status = EncodeAudioPresentation(source,
                static_cast<std::uint32_t>(order_index + 1),
                batch.audio_terminal_journal[order.family_index],
                output[output_count]);
            seen_terminal[order.family_index] = true;
        }
        else if (blueprint)
        {
            if (order.family_index
                    >= batch.battle_audio_blueprint_journal_count
                || seen_blueprint[order.family_index])
                return Status::failure(FailureCode::ProtocolMismatch);
            const auto& entry = batch.battle_audio_blueprint_journal[
                order.family_index];
            if (entry.direct > 1)
                return Status::failure(FailureCode::ProtocolMismatch);
            const AudioBlueprintPresentationValue value{
                entry.handler_slot, entry.direct != 0, entry.semantic};
            status = EncodeAudioBlueprintPresentation(source,
                static_cast<std::uint32_t>(order_index + 1), value,
                output[output_count]);
            seen_blueprint[order.family_index] = true;
        }
        else
        {
            const StagePresentationJournalEntry* entry{};
            StagePresentationOperation operation{};
            if (wall)
            {
                if (order.family_index >= batch.stage_wall_journal_count
                    || seen_wall[order.family_index])
                    return Status::failure(FailureCode::ProtocolMismatch);
                seen_wall[order.family_index] = true;
                entry = &batch.stage_wall_journal[order.family_index];
                operation = StagePresentationOperation::WallBroken;
            }
            else
            {
                if (order.family_index >= batch.stage_barrier_journal_count
                    || seen_barrier[order.family_index])
                    return Status::failure(FailureCode::ProtocolMismatch);
                seen_barrier[order.family_index] = true;
                entry = &batch.stage_barrier_journal[order.family_index];
                operation = StagePresentationOperation::BarrierHit;
            }
            if (entry == nullptr || entry->particle_count
                    > Schema::maximum_stage_particles_per_event
                || static_cast<std::size_t>(entry->first_particle)
                        + entry->particle_count
                    > batch.particle_spawn_journal_count)
                return Status::failure(FailureCode::ProtocolMismatch);
            StagePresentationValue value{};
            value.coordinate = source;
            value.source_ordinal = static_cast<std::uint32_t>(order_index + 1);
            value.operation = operation;
            value.owner_logical_id = entry->owner_logical_id;
            value.source_semantic = entry->semantic;
            value.canonical_before = entry->canonical_before;
            value.source_payload_size = entry->payload_size;
            value.canonical_before_size = entry->canonical_before_size;
            value.particle_count = entry->particle_count;
            for (std::size_t particle = 0; particle < entry->particle_count;
                 ++particle)
            {
                const auto particle_index = static_cast<std::size_t>(
                    entry->first_particle) + particle;
                if (claimed_particle[particle_index])
                    return Status::failure(FailureCode::ProtocolMismatch);
                bool ordered{};
                for (std::size_t nested_order = order_index + 1;
                     nested_order < batch.presentation_order_journal_count;
                     ++nested_order)
                {
                    const auto& nested =
                        batch.presentation_order_journal[nested_order];
                    if (nested.family == PresentationEventFamily::StageWall
                        || nested.family
                            == PresentationEventFamily::StageBarrier)
                        break;
                    if (nested.family == PresentationEventFamily::ParticleSpawn
                        && nested.family_index == particle_index)
                    {
                        ordered = true;
                        break;
                    }
                }
                if (!ordered)
                    return Status::failure(FailureCode::ProtocolMismatch);
                claimed_particle[particle_index] = true;
                value.particles[particle].semantic =
                    batch.particle_spawn_journal[particle_index].semantic;
            }
            status = EncodeStagePresentation(value, output[output_count]);
        }
        if (!status.ok()) return status;
        ++output_count;
    }
    if (output_count != required)
        return Status::failure(FailureCode::ProtocolMismatch);
    for (std::size_t index = 0; index < batch.particle_spawn_journal_count;
         ++index)
        if (!claimed_particle[index])
            return Status::failure(FailureCode::UnsupportedContent);
    return Status::success();
}

Status RecordNativeAudioPresentation(
    const NativeBatchEnvelope& batch,
    IPresentationJournal& journal) noexcept
{
    std::array<PresentationEvent, maximum_audio_terminal_journal_events
        + maximum_battle_audio_blueprint_journal_events
        + maximum_stage_presentation_journal_events * 2> events{};
    std::size_t event_count{};
    const Status built = BuildNativeAudioPresentation(batch, events, event_count);
    if (!built.ok()) return built;
    for (std::size_t index = 0; index < event_count; ++index)
    {
        const Status status = journal.Record(events[index]);
        if (!status.ok()) return status;
    }
    return Status::success();
}
}
