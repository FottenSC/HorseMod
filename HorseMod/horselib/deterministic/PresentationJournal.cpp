#include "PresentationJournal.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace Horse::Deterministic
{
PresentationJournal::PresentationJournal(
    std::size_t maximum_events,
    std::size_t maximum_payload_bytes) noexcept
    : maximum_events_(maximum_events),
      maximum_payload_bytes_(maximum_payload_bytes)
{
    if (maximum_events_ == 0
        || maximum_payload_bytes_ > maximum_events_
            * Schema::maximum_presentation_payload)
    {
        maximum_events_ = 0;
        maximum_payload_bytes_ = 0;
        return;
    }
    slots_.reset(new (std::nothrow) Slot[maximum_events_]);
    watermarks_.reset(new (std::nothrow) Watermark[maximum_events_]);
    replacement_presented_.reset(new (std::nothrow) bool[maximum_events_]);
    if (!slots_ || !watermarks_ || !replacement_presented_)
    {
        slots_.reset();
        watermarks_.reset();
        replacement_presented_.reset();
        maximum_events_ = 0;
        maximum_payload_bytes_ = 0;
    }
}

Status PresentationJournal::Record(PresentationEvent event) noexcept
{
    return RecordInternal(std::move(event), false);
}

Status PresentationJournal::RecordPresented(PresentationEvent event) noexcept
{
    return RecordInternal(std::move(event), true);
}

Status PresentationJournal::RecordInternal(
    PresentationEvent event, bool presented) noexcept
{
    ++statistics_.attempted;
    if (!Valid(event))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    const EventKey key = Key(event);
    if (IsCommitted(event))
    {
        ++statistics_.duplicates;
        return Status::success();
    }
    for (std::size_t index = 0; index < pending_count_; ++index)
    {
        auto& slot = slots_[index];
        const EventKey existing = Key(slot.event);
        if (existing == key)
        {
            if (presented && !slot.presented)
            {
                slot.presented = true;
                ++statistics_.speculative_presented;
            }
            ++statistics_.duplicates;
            return Status::success();
        }
    }
    if (pending_count_ == maximum_events_
        || payload_bytes_ + event.payload_size > maximum_payload_bytes_)
    {
        ++statistics_.capacity_failures;
        return Status::failure(FailureCode::CapacityExceeded);
    }
    auto& free_slot = slots_[pending_count_];
    payload_bytes_ += event.payload_size;
    free_slot.event = std::move(event);
    free_slot.occupied = true;
    free_slot.presented = presented;
    ++pending_count_;
    ++statistics_.recorded;
    if (presented) ++statistics_.speculative_presented;
    return Status::success();
}

Status PresentationJournal::ReplaceFrom(FrameCoordinate coordinate,
    std::span<const PresentationEvent> replacement) noexcept
{
    if (coordinate.generation == 0 || replacement.size() > maximum_events_)
        return Status::failure(FailureCode::InvalidConfiguration);

    std::size_t retained_count{};
    std::size_t retained_payload{};
    for (std::size_t index = 0; index < pending_count_; ++index)
    {
        const auto& slot = slots_[index];
        if (!slot.occupied) continue;
        if (slot.event.coordinate.generation != coordinate.generation
            || slot.event.coordinate.frame < coordinate.frame)
        {
            ++retained_count;
            retained_payload += slot.event.payload_size;
        }
    }

    std::size_t added_count{};
    std::size_t added_payload{};
    for (std::size_t index = 0; index < replacement.size(); ++index)
    {
        const auto& event = replacement[index];
        if (!Valid(event)
            || event.coordinate.generation != coordinate.generation
            || event.coordinate.frame < coordinate.frame)
            return Status::failure(FailureCode::InvalidConfiguration);
        if (IsCommitted(event)) continue;
        const EventKey key = Key(event);
        bool duplicate{};
        for (std::size_t slot_index = 0;
             slot_index < pending_count_; ++slot_index)
        {
            const auto& slot = slots_[slot_index];
            if (!slot.occupied
                || (slot.event.coordinate.generation == coordinate.generation
                    && slot.event.coordinate.frame >= coordinate.frame))
                continue;
            if (Key(slot.event) == key)
            {
                duplicate = true;
                break;
            }
        }
        for (std::size_t prior = 0; !duplicate && prior < index; ++prior)
            duplicate = !IsCommitted(replacement[prior])
                && Key(replacement[prior]) == key;
        if (duplicate) continue;
        ++added_count;
        added_payload += event.payload_size;
    }
    if (retained_count + added_count > maximum_events_
        || retained_payload + added_payload > maximum_payload_bytes_)
    {
        ++statistics_.capacity_failures;
        return Status::failure(FailureCode::CapacityExceeded);
    }

    for (std::size_t index = 0; index < replacement.size(); ++index)
    {
        replacement_presented_[index] = false;
        const auto key = Key(replacement[index]);
        for (std::size_t slot_index = 0;
             slot_index < pending_count_; ++slot_index)
        {
            const auto& slot = slots_[slot_index];
            if (slot.occupied && slot.presented && Key(slot.event) == key)
            {
                replacement_presented_[index] = true;
                break;
            }
        }
    }
    DiscardFrom(coordinate);
    for (std::size_t index = 0; index < replacement.size(); ++index)
    {
        const Status status = RecordInternal(replacement[index],
            replacement_presented_[index]);
        if (!status.ok()) return Status::failure(FailureCode::UndoFailed);
        if (replacement_presented_[index])
            ++statistics_.speculative_reused;
    }
    return Status::success();
}

void PresentationJournal::DiscardFrom(FrameCoordinate coordinate) noexcept
{
    std::size_t index{};
    while (index < pending_count_)
    {
        auto& slot = slots_[index];
        if (slot.event.coordinate.generation == coordinate.generation
            && slot.event.coordinate.frame >= coordinate.frame)
        {
            ClearSlot(index);
            ++statistics_.discarded;
            continue;
        }
        ++index;
    }
}

Status PresentationJournal::CommitThrough(
    FrameCoordinate confirmed,
    IPresentationSink& sink) noexcept
{
    for (;;)
    {
        std::size_t next_index = pending_count_;
        EventKey next_key{};
        for (std::size_t index = 0; index < pending_count_; ++index)
        {
            auto& slot = slots_[index];
            const auto coordinate = slot.event.coordinate;
            if (coordinate.generation > confirmed.generation
                || (coordinate.generation == confirmed.generation
                    && coordinate.frame > confirmed.frame))
            {
                continue;
            }
            const EventKey key{coordinate, slot.event.source_ordinal,
                slot.event.kind, slot.event.identity};
            if (next_index == pending_count_ || key < next_key)
            {
                next_index = index;
                next_key = key;
            }
        }
        if (next_index == pending_count_) return Status::success();
        auto& next = slots_[next_index];

        auto* watermark = EnsureWatermark(next.event.coordinate.generation);
        if (watermark == nullptr)
        {
            ++statistics_.capacity_failures;
            return Status::failure(FailureCode::CapacityExceeded);
        }
        const Status published = next.presented
            ? Status::success() : sink.Publish(next.event);
        if (!published.ok())
        {
            ++statistics_.publish_failures;
            if (statistics_.first_publish_failure == FailureCode::None)
            {
                statistics_.first_publish_failure = published.code;
                statistics_.first_failed_event = next.event;
            }
            statistics_.last_publish_failure = published.code;
            statistics_.last_failed_event = next.event;
            return published;
        }
        if (next.event.coordinate.frame > watermark->frame)
        {
            watermark->frame = next.event.coordinate.frame;
            watermark->source_ordinal = next.event.source_ordinal;
        }
        else
        {
            watermark->source_ordinal = (std::max)(
                watermark->source_ordinal, next.event.source_ordinal);
        }
        ClearSlot(next_index);
        ++statistics_.committed;
    }
}

void PresentationJournal::InvalidateGeneration(std::uint64_t generation) noexcept
{
    std::size_t index{};
    while (index < pending_count_)
    {
        auto& slot = slots_[index];
        if (slot.event.coordinate.generation == generation)
        {
            ClearSlot(index);
            ++statistics_.discarded;
            continue;
        }
        ++index;
    }
    for (std::size_t watermark_index = 0;
         watermark_index < watermark_count_; ++watermark_index)
    {
        if (watermarks_[watermark_index].generation != generation) continue;
        --watermark_count_;
        watermarks_[watermark_index] = watermarks_[watermark_count_];
        watermarks_[watermark_count_] = {};
        break;
    }
}

std::size_t PresentationJournal::pending_count() const noexcept
{
    return pending_count_;
}

std::size_t PresentationJournal::payload_bytes() const noexcept
{
    return payload_bytes_;
}

std::size_t PresentationJournal::capacity() const noexcept
{
    return maximum_events_;
}

PresentationJournal::Statistics PresentationJournal::statistics() const noexcept
{
    return statistics_;
}

Status PresentationJournal::ResetStatistics() noexcept
{
    if (pending_count_ != 0 || payload_bytes_ != 0)
        return Status::failure(FailureCode::IllegalTransition);
    statistics_ = {};
    return Status::success();
}

PresentationJournal::Watermark* PresentationJournal::FindWatermark(
    std::uint64_t generation) noexcept
{
    for (std::size_t index = 0; index < watermark_count_; ++index)
        if (watermarks_[index].generation == generation)
            return &watermarks_[index];
    return nullptr;
}

const PresentationJournal::Watermark* PresentationJournal::FindWatermark(
    std::uint64_t generation) const noexcept
{
    for (std::size_t index = 0; index < watermark_count_; ++index)
        if (watermarks_[index].generation == generation)
            return &watermarks_[index];
    return nullptr;
}

PresentationJournal::Watermark* PresentationJournal::EnsureWatermark(
    std::uint64_t generation) noexcept
{
    if (auto* existing = FindWatermark(generation); existing != nullptr)
        return existing;
    if (watermark_count_ == maximum_events_) return nullptr;
    auto& watermark = watermarks_[watermark_count_++];
    watermark = {true, generation, 0, 0};
    return &watermark;
}

bool PresentationJournal::IsCommitted(const PresentationEvent& event) const noexcept
{
    const auto* watermark = FindWatermark(event.coordinate.generation);
    return watermark != nullptr
        && (event.coordinate.frame < watermark->frame
            || (event.coordinate.frame == watermark->frame
                && event.source_ordinal <= watermark->source_ordinal));
}

PresentationJournal::EventKey PresentationJournal::Key(
    const PresentationEvent& event) noexcept
{
    return {event.coordinate, event.source_ordinal, event.kind, event.identity};
}

bool PresentationJournal::Valid(const PresentationEvent& event) noexcept
{
    return event.coordinate.generation != 0 && event.source_ordinal != 0
        && event.kind != 0 && event.identity != 0
        && event.payload_size <= Schema::maximum_presentation_payload;
}

void PresentationJournal::ClearSlot(std::size_t index) noexcept
{
    auto& slot = slots_[index];
    payload_bytes_ -= slot.event.payload_size;
    --pending_count_;
    if (index != pending_count_)
        slot = std::move(slots_[pending_count_]);
    slots_[pending_count_] = {};
}
}
