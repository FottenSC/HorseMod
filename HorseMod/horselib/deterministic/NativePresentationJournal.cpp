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
        || batch.presentation_order_failures != 0
        || batch.presentation_order_journal_count
            > batch.presentation_order_journal.size())
        return Status::failure(FailureCode::ProtocolMismatch);

    if (output.size() < batch.audio_terminal_journal_count)
        return Status::failure(FailureCode::CapacityExceeded);
    std::array<bool, maximum_audio_terminal_journal_events> seen{};
    for (std::size_t order_index = 0;
         order_index < batch.presentation_order_journal_count; ++order_index)
    {
        const auto& order = batch.presentation_order_journal[order_index];
        if (order.family != PresentationEventFamily::AudioTerminal) continue;
        if (order.family_index >= batch.audio_terminal_journal_count
            || seen[order.family_index]
            || batch.entry_coordinate.frame
                > (std::numeric_limits<std::uint64_t>::max)()
                    - order.source_offset)
            return Status::failure(FailureCode::ProtocolMismatch);
        const FrameCoordinate source{batch.entry_coordinate.generation,
            batch.entry_coordinate.frame + order.source_offset};
        if (source > batch.exit_coordinate)
            return Status::failure(FailureCode::ProtocolMismatch);
        Status status = EncodeAudioPresentation(source,
            static_cast<std::uint32_t>(order_index + 1),
            batch.audio_terminal_journal[order.family_index],
            output[output_count]);
        if (!status.ok()) return status;
        seen[order.family_index] = true;
        ++output_count;
    }
    if (output_count != batch.audio_terminal_journal_count)
        return Status::failure(FailureCode::ProtocolMismatch);
    return Status::success();
}

Status RecordNativeAudioPresentation(
    const NativeBatchEnvelope& batch,
    IPresentationJournal& journal) noexcept
{
    std::array<PresentationEvent, maximum_audio_terminal_journal_events> events{};
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
