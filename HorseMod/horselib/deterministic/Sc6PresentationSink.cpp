#include "Sc6PresentationSink.hpp"

#include "AudioPresentation.hpp"
#include "DeterministicHookSet.hpp"

namespace Horse::Deterministic
{
Status Sc6PresentationSink::Publish(const PresentationEvent& event) noexcept
{
    if (event.kind == Schema::audio_presentation_event_kind)
    {
        AudioTerminalEvent terminal{};
        const Status decoded = DecodeAudioPresentation(event, terminal);
        if (!decoded.ok()) return decoded;
        return hooks_.CommitAudioTerminal(terminal);
    }
    if (event.kind == Schema::audio_blueprint_presentation_event_kind)
    {
        AudioBlueprintPresentationValue value{};
        const Status decoded = DecodeAudioBlueprintPresentation(event, value);
        if (!decoded.ok()) return decoded;
        return hooks_.CommitAudioBlueprint(value);
    }
    return Status::failure(FailureCode::UnsupportedContent);
}
}
