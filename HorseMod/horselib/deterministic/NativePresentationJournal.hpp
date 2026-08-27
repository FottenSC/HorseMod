#pragma once

#include "Interfaces.hpp"
#include "NativeBatchTimeline.hpp"

namespace Horse::Deterministic
{
[[nodiscard]] Status BuildNativeAudioPresentation(
    const NativeBatchEnvelope& batch,
    std::span<PresentationEvent> output,
    std::size_t& output_count) noexcept;

// Records the closed audio and composite static stage subsets of one native
// batch using the exact authored cross-family order. Static stage values own
// their nested particle creates; unowned/dynamic particle routes fail closed.
[[nodiscard]] Status RecordNativeAudioPresentation(
    const NativeBatchEnvelope& batch,
    IPresentationJournal& journal) noexcept;
}
