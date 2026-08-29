#pragma once

#include "NativePresentationJournal.hpp"
#include "PresentationJournal.hpp"

#include <memory>

namespace Horse::Deterministic
{
class NativeAudioPresentationController
{
public:
    NativeAudioPresentationController(std::size_t maximum_events,
        std::size_t maximum_payload_bytes,
        std::size_t maximum_correction_events) noexcept;

    Status BeginGeneration(std::uint64_t generation) noexcept;
    Status RecordSpeculative(const NativeBatchEnvelope& batch) noexcept;
    Status ReplaceCorrected(FrameCoordinate earliest_changed,
        std::span<const NativeBatchEnvelope> corrected_batches) noexcept;
    Status CommitThrough(FrameCoordinate confirmed,
        IPresentationSink& sink) noexcept;
    void EndGeneration() noexcept;

    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::size_t pending_count() const noexcept;
    [[nodiscard]] std::size_t payload_bytes() const noexcept;
    [[nodiscard]] std::size_t allocated_bytes() const noexcept;
    [[nodiscard]] PresentationJournal::Statistics statistics() const noexcept;

private:
    PresentationJournal journal_;
    std::unique_ptr<PresentationEvent[]> correction_events_;
    std::size_t maximum_correction_events_{};
    std::uint64_t generation_{};
};
}
