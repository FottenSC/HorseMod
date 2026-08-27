#pragma once

#include "Interfaces.hpp"
#include "NativeBatchTimeline.hpp"

namespace Horse::Deterministic
{
// Records the terminal audio subset of one native batch using the batch's
// exact authored cross-family order. Other presentation families remain in the
// same order stream and therefore still contribute to source_ordinal.
[[nodiscard]] Status RecordNativeAudioPresentation(
    const NativeBatchEnvelope& batch,
    IPresentationJournal& journal) noexcept;
}
