#include "NativePresentationJournal.hpp"

#include "AudioPresentation.hpp"

#include <array>
#include <limits>

namespace Horse::Deterministic
{
Status BuildNativeAudioPresentation(const NativeBatchEnvelope& batch,
    std::span<PresentationEvent> output, std::size_t& output_count) noexcept
{
    output_count = 0;
    if (batch.entry_coordinate.generation == 0
        || batch.exit_coordinate.generation
            != batch.entry_coordinate.generation
        || batch.exit_coordinate < batch.entry_coordinate
        || batch.audio_terminal_calls != batch.audio_terminal_journal_count
        || batch.audio_terminal_journal_count
            > batch.audio_terminal_journal.size()
        || batch.battle_audio_blueprint_calls
            != batch.battle_audio_blueprint_journal_count
        || batch.battle_audio_blueprint_journal_count
            > batch.battle_audio_blueprint_journal.size()
        || batch.presentation_order_failures != 0
        || batch.presentation_order_journal_count
            > batch.presentation_order_journal.size())
        return Status::failure(FailureCode::ProtocolMismatch);

    const auto required = static_cast<std::size_t>(
        batch.audio_terminal_journal_count)
        + batch.battle_audio_blueprint_journal_count;
    if (output.size() < required)
        return Status::failure(FailureCode::CapacityExceeded);
    std::array<bool, maximum_audio_terminal_journal_events> seen_terminal{};
    std::array<bool, maximum_battle_audio_blueprint_journal_events>
        seen_blueprint{};
    for (std::size_t order_index = 0;
         order_index < batch.presentation_order_journal_count; ++order_index)
    {
        const auto& order = batch.presentation_order_journal[order_index];
        const bool terminal = order.family
            == PresentationEventFamily::AudioTerminal;
        const bool blueprint = order.family
            == PresentationEventFamily::BattleAudioBlueprint;
        if (!terminal && !blueprint) continue;
        if (batch.entry_coordinate.frame
                > (std::numeric_limits<std::uint64_t>::max)()
                    - order.source_offset)
            return Status::failure(FailureCode::ProtocolMismatch);
        const FrameCoordinate source{batch.entry_coordinate.generation,
            batch.entry_coordinate.frame + order.source_offset};
        if (source > batch.exit_coordinate)
            return Status::failure(FailureCode::ProtocolMismatch);
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
        else
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
        if (!status.ok()) return status;
        ++output_count;
    }
    if (output_count != required)
        return Status::failure(FailureCode::ProtocolMismatch);
    return Status::success();
}

Status RecordNativeAudioPresentation(
    const NativeBatchEnvelope& batch,
    IPresentationJournal& journal) noexcept
{
    std::array<PresentationEvent, maximum_audio_terminal_journal_events
        + maximum_battle_audio_blueprint_journal_events> events{};
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
