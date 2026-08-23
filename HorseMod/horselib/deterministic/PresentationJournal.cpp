#include "PresentationJournal.hpp"

#include <utility>
#include <iterator>

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
    if (committed_.contains(key) || pending_.contains(key))
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
            committed_.insert(it->first);
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
    for (auto it = committed_.begin(); it != committed_.end();)
    {
        it = it->coordinate.generation == generation
            ? committed_.erase(it)
            : std::next(it);
    }
}

std::size_t PresentationJournal::pending_count() const noexcept
{
    return pending_.size();
}
}
