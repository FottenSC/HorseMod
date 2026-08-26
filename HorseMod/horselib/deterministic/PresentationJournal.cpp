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
    if (!slots_ || !watermarks_)
    {
        slots_.reset();
        watermarks_.reset();
        maximum_events_ = 0;
        maximum_payload_bytes_ = 0;
    }
}

Status PresentationJournal::Record(PresentationEvent event) noexcept
{
    ++statistics_.attempted;
    if (event.coordinate.generation == 0 || event.source_ordinal == 0
        || event.kind == 0
        || event.identity == 0
        || event.payload_size > Schema::maximum_presentation_payload)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    const EventKey key{event.coordinate, event.source_ordinal,
        event.kind, event.identity};
    const auto* watermark = FindWatermark(event.coordinate.generation);
    if (watermark != nullptr
        && (event.coordinate.frame < watermark->frame
            || (event.coordinate.frame == watermark->frame
                && event.source_ordinal <= watermark->source_ordinal)))
    {
        ++statistics_.duplicates;
        return Status::success();
    }
    Slot* free_slot{};
    for (std::size_t index = 0; index < maximum_events_; ++index)
    {
        auto& slot = slots_[index];
        if (!slot.occupied)
        {
            if (free_slot == nullptr) free_slot = &slot;
            continue;
        }
        const EventKey existing{slot.event.coordinate,
            slot.event.source_ordinal, slot.event.kind, slot.event.identity};
        if (existing == key)
        {
            ++statistics_.duplicates;
            return Status::success();
        }
    }
    if (free_slot == nullptr
        || payload_bytes_ + event.payload_size > maximum_payload_bytes_)
    {
        ++statistics_.capacity_failures;
        return Status::failure(FailureCode::CapacityExceeded);
    }
    payload_bytes_ += event.payload_size;
    free_slot->event = std::move(event);
    free_slot->occupied = true;
    ++pending_count_;
    ++statistics_.recorded;
    return Status::success();
}

void PresentationJournal::DiscardFrom(FrameCoordinate coordinate) noexcept
{
    for (std::size_t index = 0; index < maximum_events_; ++index)
    {
        auto& slot = slots_[index];
        if (slot.occupied
            && slot.event.coordinate.generation == coordinate.generation
            && slot.event.coordinate.frame >= coordinate.frame)
        {
            ClearSlot(slot);
            ++statistics_.discarded;
        }
    }
}

Status PresentationJournal::CommitThrough(
    FrameCoordinate confirmed,
    IPresentationSink& sink) noexcept
{
    for (;;)
    {
        Slot* next{};
        EventKey next_key{};
        for (std::size_t index = 0; index < maximum_events_; ++index)
        {
            auto& slot = slots_[index];
            if (!slot.occupied) continue;
            const auto coordinate = slot.event.coordinate;
            if (coordinate.generation > confirmed.generation
                || (coordinate.generation == confirmed.generation
                    && coordinate.frame > confirmed.frame))
            {
                continue;
            }
            const EventKey key{coordinate, slot.event.source_ordinal,
                slot.event.kind, slot.event.identity};
            if (next == nullptr || key < next_key)
            {
                next = &slot;
                next_key = key;
            }
        }
        if (next == nullptr) return Status::success();

        auto* watermark = EnsureWatermark(next->event.coordinate.generation);
        if (watermark == nullptr)
        {
            ++statistics_.capacity_failures;
            return Status::failure(FailureCode::CapacityExceeded);
        }
        const Status published = sink.Publish(next->event);
        if (!published.ok())
        {
            ++statistics_.publish_failures;
            return published;
        }
        if (next->event.coordinate.frame > watermark->frame)
        {
            watermark->frame = next->event.coordinate.frame;
            watermark->source_ordinal = next->event.source_ordinal;
        }
        else
        {
            watermark->source_ordinal = (std::max)(
                watermark->source_ordinal, next->event.source_ordinal);
        }
        ClearSlot(*next);
        ++statistics_.committed;
    }
}

void PresentationJournal::InvalidateGeneration(std::uint64_t generation) noexcept
{
    for (std::size_t index = 0; index < maximum_events_; ++index)
    {
        auto& slot = slots_[index];
        if (slot.occupied && slot.event.coordinate.generation == generation)
        {
            ClearSlot(slot);
            ++statistics_.discarded;
        }
    }
    if (auto* watermark = FindWatermark(generation); watermark != nullptr)
        *watermark = {};
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

PresentationJournal::Watermark* PresentationJournal::FindWatermark(
    std::uint64_t generation) noexcept
{
    for (std::size_t index = 0; index < maximum_events_; ++index)
        if (watermarks_[index].occupied
            && watermarks_[index].generation == generation)
            return &watermarks_[index];
    return nullptr;
}

const PresentationJournal::Watermark* PresentationJournal::FindWatermark(
    std::uint64_t generation) const noexcept
{
    for (std::size_t index = 0; index < maximum_events_; ++index)
        if (watermarks_[index].occupied
            && watermarks_[index].generation == generation)
            return &watermarks_[index];
    return nullptr;
}

PresentationJournal::Watermark* PresentationJournal::EnsureWatermark(
    std::uint64_t generation) noexcept
{
    if (auto* existing = FindWatermark(generation); existing != nullptr)
        return existing;
    for (std::size_t index = 0; index < maximum_events_; ++index)
    {
        if (!watermarks_[index].occupied)
        {
            watermarks_[index] = {true, generation, 0, 0};
            return &watermarks_[index];
        }
    }
    return nullptr;
}

void PresentationJournal::ClearSlot(Slot& slot) noexcept
{
    payload_bytes_ -= slot.event.payload_size;
    slot = {};
    --pending_count_;
}
}
