#include "PresentationJournal.hpp"

#include <algorithm>
#include <utility>

namespace Horse::Deterministic
{
PresentationJournal::PresentationJournal(
    std::size_t maximum_events,
    std::size_t maximum_payload_bytes) noexcept
    : maximum_events_(maximum_events),
      maximum_payload_bytes_(maximum_payload_bytes)
{
}

Status PresentationJournal::Record(PresentationEvent event) noexcept
{
    const EventKey key{event.coordinate, event.kind, event.identity};
    const auto watermark = committed_through_.find(event.coordinate.generation);
    if ((watermark != committed_through_.end()
            && event.coordinate.frame <= watermark->second)
        || pending_.contains(key))
    {
        return Status::success();
    }
    if (pending_.size() >= maximum_events_
        || payload_bytes_ + event.payload.size() > maximum_payload_bytes_)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    payload_bytes_ += event.payload.size();
    pending_.emplace(key, std::move(event));
    return Status::success();
}

void PresentationJournal::DiscardFrom(FrameCoordinate coordinate) noexcept
{
    for (auto it = pending_.lower_bound(EventKey{coordinate, 0, 0});
         it != pending_.end();)
    {
        if (it->first.coordinate.generation != coordinate.generation)
        {
            break;
        }
        payload_bytes_ -= it->second.payload.size();
        it = pending_.erase(it);
    }
}

Status PresentationJournal::CommitThrough(
    FrameCoordinate confirmed,
    IPresentationSink& sink) noexcept
{
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        if (it->first.coordinate.generation < confirmed.generation
            || (it->first.coordinate.generation == confirmed.generation
                && it->first.coordinate.frame <= confirmed.frame))
        {
            const Status published = sink.Publish(it->second);
            if (!published.ok())
            {
                return published;
            }
            payload_bytes_ -= it->second.payload.size();
            auto& watermark = committed_through_[it->first.coordinate.generation];
            watermark = std::max(watermark, it->first.coordinate.frame);
            it = pending_.erase(it);
            continue;
        }
        ++it;
    }
    return Status::success();
}

void PresentationJournal::InvalidateGeneration(std::uint64_t generation) noexcept
{
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        if (it->first.coordinate.generation == generation)
        {
            payload_bytes_ -= it->second.payload.size();
            it = pending_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    committed_through_.erase(generation);
}

std::size_t PresentationJournal::pending_count() const noexcept
{
    return pending_.size();
}
}
