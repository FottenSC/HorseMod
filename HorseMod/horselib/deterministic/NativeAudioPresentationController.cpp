#include "NativeAudioPresentationController.hpp"

#include <new>

namespace Horse::Deterministic
{
NativeAudioPresentationController::NativeAudioPresentationController(
    std::size_t maximum_events, std::size_t maximum_payload_bytes,
    std::size_t maximum_correction_events) noexcept
    : journal_(maximum_events, maximum_payload_bytes),
      maximum_correction_events_(maximum_correction_events)
{
    if (maximum_correction_events_ != 0)
        correction_events_.reset(
            new (std::nothrow) PresentationEvent[maximum_correction_events_]);
    if (!correction_events_) maximum_correction_events_ = 0;
}

Status NativeAudioPresentationController::BeginGeneration(
    std::uint64_t generation) noexcept
{
    if (generation == 0)
        return Status::failure(FailureCode::InvalidConfiguration);
    if (generation_ == generation) return Status::success();
    if (generation_ != 0) journal_.InvalidateGeneration(generation_);
    generation_ = generation;
    return Status::success();
}

Status NativeAudioPresentationController::RecordSpeculative(
    const NativeBatchEnvelope& batch) noexcept
{
    if (generation_ == 0
        || batch.entry_coordinate.generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    return RecordNativeAudioPresentation(batch, journal_);
}

Status NativeAudioPresentationController::ReplaceCorrected(
    FrameCoordinate earliest_changed,
    std::span<const NativeBatchEnvelope> corrected_batches) noexcept
{
    if (generation_ == 0 || earliest_changed.generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (!corrected_batches.empty() && maximum_correction_events_ == 0)
        return Status::failure(FailureCode::CapacityExceeded);
    std::size_t replacement_count{};
    FrameCoordinate previous_exit{};
    bool have_previous{};
    for (const auto& batch : corrected_batches)
    {
        if (batch.entry_coordinate.generation != generation_
            || (have_previous && batch.entry_coordinate < previous_exit))
            return Status::failure(FailureCode::IdentityMismatch);
        std::size_t built_count{};
        const auto remaining = std::span{correction_events_.get()
                + replacement_count,
            maximum_correction_events_ - replacement_count};
        const Status built = BuildNativeAudioPresentation(
            batch, remaining, built_count);
        if (!built.ok()) return built;
        const auto end = replacement_count + built_count;
        for (std::size_t index = replacement_count; index < end; ++index)
        {
            if (correction_events_[index].coordinate < earliest_changed)
                continue;
            if (index != replacement_count)
                correction_events_[replacement_count] = correction_events_[index];
            ++replacement_count;
        }
        previous_exit = batch.exit_coordinate;
        have_previous = true;
    }
    return journal_.ReplaceFrom(earliest_changed,
        std::span{correction_events_.get(), replacement_count});
}

Status NativeAudioPresentationController::CommitThrough(
    FrameCoordinate confirmed, IPresentationSink& sink) noexcept
{
    if (generation_ == 0 || confirmed.generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    return journal_.CommitThrough(confirmed, sink);
}

void NativeAudioPresentationController::EndGeneration() noexcept
{
    if (generation_ != 0) journal_.InvalidateGeneration(generation_);
    generation_ = 0;
}

std::uint64_t NativeAudioPresentationController::generation() const noexcept
{
    return generation_;
}

std::size_t NativeAudioPresentationController::pending_count() const noexcept
{
    return journal_.pending_count();
}

std::size_t NativeAudioPresentationController::payload_bytes() const noexcept
{
    return journal_.payload_bytes();
}

PresentationJournal::Statistics
NativeAudioPresentationController::statistics() const noexcept
{
    return journal_.statistics();
}
}
