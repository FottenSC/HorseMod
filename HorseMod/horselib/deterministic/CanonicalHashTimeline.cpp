#include "CanonicalHashTimeline.hpp"

#include <algorithm>

namespace Horse::Deterministic
{
CanonicalHashTimeline::CanonicalHashTimeline(
    std::size_t maximum_entries) noexcept
    : maximum_entries_(maximum_entries)
{
    try
    {
        entries_.reserve(maximum_entries_);
    }
    catch (...)
    {
        maximum_entries_ = 0;
    }
}

Status CanonicalHashTimeline::Append(
    FrameCoordinate coordinate, const CanonicalHash& hash) noexcept
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const CanonicalHashEntry& entry, FrameCoordinate value)
        {
            return entry.coordinate < value;
        });
    if (found != entries_.end() && found->coordinate == coordinate)
    {
        return found->hash == hash
            ? Status::success()
            : Status::failure(FailureCode::StateHashMismatch);
    }
    if (found != entries_.end())
        return Status::failure(FailureCode::IdentityMismatch);
    if (entries_.size() >= maximum_entries_)
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        entries_.push_back({coordinate, hash});
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

std::optional<CanonicalHash> CanonicalHashTimeline::GetExact(
    FrameCoordinate coordinate) const noexcept
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const CanonicalHashEntry& entry, FrameCoordinate value)
        {
            return entry.coordinate < value;
        });
    return found != entries_.end() && found->coordinate == coordinate
        ? std::optional<CanonicalHash>{found->hash} : std::nullopt;
}

void CanonicalHashTimeline::Clear() noexcept
{
    entries_.clear();
}
}
