#include "SnapshotStore.hpp"

#include <algorithm>
#include <utility>

namespace Horse::Deterministic
{
SnapshotStore::SnapshotStore(
    std::size_t maximum_bytes,
    std::size_t maximum_entries,
    CapacityPolicy policy) noexcept
    : maximum_bytes_(maximum_bytes),
      maximum_entries_(maximum_entries),
      policy_(policy)
{
    try
    {
        snapshots_.reserve(maximum_entries_);
    }
    catch (...)
    {
        maximum_entries_ = 0;
    }
}

std::size_t SnapshotStore::snapshot_cost(const Snapshot& snapshot) const noexcept
{
    std::size_t cost = sizeof(Snapshot) + snapshot.bytes.size();
    for (const auto& local : snapshot.local_images)
        cost += sizeof(LocalReconstructionImage) + local.bytes.size();
    return cost;
}

void SnapshotStore::erase_oldest() noexcept
{
    if (snapshots_.empty())
    {
        return;
    }
    bytes_used_ -= snapshot_cost(snapshots_.front());
    snapshots_.erase(snapshots_.begin());
}

Status SnapshotStore::Save(Snapshot snapshot) noexcept
{
    const std::size_t incoming = snapshot_cost(snapshot);
    if (incoming > maximum_bytes_ || maximum_entries_ == 0)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }

    auto existing = std::lower_bound(snapshots_.begin(), snapshots_.end(),
        snapshot.coordinate, [](const Snapshot& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    const bool replacing = existing != snapshots_.end()
        && existing->coordinate == snapshot.coordinate;
    const std::size_t replaced = existing == snapshots_.end()
        ? 0
        : (replacing ? snapshot_cost(*existing) : 0);
    const std::size_t effective_count = snapshots_.size()
        - (replacing ? 1 : 0);

    if (policy_ == CapacityPolicy::RejectNew
        && (bytes_used_ - replaced + incoming > maximum_bytes_
            || effective_count >= maximum_entries_))
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }

    if (replacing)
    {
        bytes_used_ -= replaced;
        snapshots_.erase(existing);
        existing = std::lower_bound(snapshots_.begin(), snapshots_.end(),
            snapshot.coordinate,
            [](const Snapshot& entry, FrameCoordinate value) {
                return entry.coordinate < value;
            });
    }

    while (bytes_used_ + incoming > maximum_bytes_
        || snapshots_.size() >= maximum_entries_)
    {
        if (policy_ == CapacityPolicy::RejectNew || snapshots_.empty())
        {
            return Status::failure(FailureCode::CapacityExceeded);
        }
        erase_oldest();
    }

    existing = std::lower_bound(snapshots_.begin(), snapshots_.end(),
        snapshot.coordinate, [](const Snapshot& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    try
    {
        snapshots_.insert(existing, std::move(snapshot));
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    bytes_used_ += incoming;
    return Status::success();
}

std::optional<Snapshot> SnapshotStore::Load(FrameCoordinate coordinate) const
{
    const auto* found = FindExact(coordinate);
    return found == nullptr ? std::nullopt : std::optional<Snapshot>{*found};
}

std::optional<Snapshot> SnapshotStore::NearestAtOrBefore(
    FrameCoordinate coordinate) const
{
    const auto* found = FindNearestAtOrBefore(coordinate);
    return found == nullptr ? std::nullopt : std::optional<Snapshot>{*found};
}

const Snapshot* SnapshotStore::FindExact(
    FrameCoordinate coordinate) const noexcept
{
    const auto found = std::lower_bound(snapshots_.begin(), snapshots_.end(),
        coordinate, [](const Snapshot& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    return found != snapshots_.end() && found->coordinate == coordinate
        ? &*found : nullptr;
}

const Snapshot* SnapshotStore::FindNearestAtOrBefore(
    FrameCoordinate coordinate) const noexcept
{
    auto found = std::upper_bound(snapshots_.begin(), snapshots_.end(),
        coordinate, [](FrameCoordinate value, const Snapshot& entry) {
            return value < entry.coordinate;
        });
    while (found != snapshots_.begin())
    {
        --found;
        if (found->coordinate.generation == coordinate.generation) return &*found;
        if (found->coordinate.generation < coordinate.generation) break;
    }
    return nullptr;
}

void SnapshotStore::InvalidateGeneration(std::uint64_t generation) noexcept
{
    for (auto it = snapshots_.begin(); it != snapshots_.end();)
    {
        if (it->coordinate.generation == generation)
        {
            bytes_used_ -= snapshot_cost(*it);
            it = snapshots_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::size_t SnapshotStore::BytesUsed() const noexcept
{
    return bytes_used_;
}

bool SnapshotStore::TakeOldestIfFull(Snapshot& output) noexcept
{
    if (policy_ != CapacityPolicy::EvictOldest
        || snapshots_.size() < maximum_entries_ || snapshots_.empty())
    {
        return false;
    }
    auto oldest = snapshots_.begin();
    bytes_used_ -= snapshot_cost(*oldest);
    output = std::move(*oldest);
    snapshots_.erase(oldest);
    return true;
}

void SnapshotStore::Clear() noexcept
{
    snapshots_.clear();
    bytes_used_ = 0;
}
}
