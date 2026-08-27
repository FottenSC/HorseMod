#include "Sc6PresentationSink.hpp"

#include "AudioPresentation.hpp"
#include "DeterministicHookSet.hpp"

namespace Horse::Deterministic
{
Status Sc6PresentationSink::Publish(const PresentationEvent& event) noexcept
{
    if (event.kind != Schema::audio_presentation_event_kind)
        return Status::failure(FailureCode::UnsupportedContent);
    AudioTerminalEvent terminal{};
    const Status decoded = DecodeAudioPresentation(event, terminal);
    if (!decoded.ok()) return decoded;
    return hooks_.CommitAudioTerminal(terminal);
}
}
