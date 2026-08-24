#include "SnapshotStore.hpp"

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
    bytes_used_ -= snapshot_cost(snapshots_.begin()->second);
    snapshots_.erase(snapshots_.begin());
}

Status SnapshotStore::Save(Snapshot snapshot) noexcept
{
    const std::size_t incoming = snapshot_cost(snapshot);
    if (incoming > maximum_bytes_ || maximum_entries_ == 0)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }

    const auto existing = snapshots_.find(snapshot.coordinate);
    const std::size_t replaced = existing == snapshots_.end()
        ? 0
        : snapshot_cost(existing->second);
    const std::size_t effective_count = snapshots_.size()
        - (existing == snapshots_.end() ? 0 : 1);

    if (policy_ == CapacityPolicy::RejectNew
        && (bytes_used_ - replaced + incoming > maximum_bytes_
            || effective_count >= maximum_entries_))
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }

    if (existing != snapshots_.end())
    {
        bytes_used_ -= replaced;
        snapshots_.erase(existing);
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

    bytes_used_ += incoming;
    snapshots_.emplace(snapshot.coordinate, std::move(snapshot));
    return Status::success();
}

std::optional<Snapshot> SnapshotStore::Load(FrameCoordinate coordinate) const
{
    const auto found = snapshots_.find(coordinate);
    return found == snapshots_.end()
        ? std::nullopt
        : std::optional<Snapshot>{found->second};
}

std::optional<Snapshot> SnapshotStore::NearestAtOrBefore(
    FrameCoordinate coordinate) const
{
    auto found = snapshots_.upper_bound(coordinate);
    while (found != snapshots_.begin())
    {
        --found;
        if (found->first.generation == coordinate.generation)
        {
            return found->second;
        }
        if (found->first.generation < coordinate.generation)
        {
            break;
        }
    }
    return std::nullopt;
}

void SnapshotStore::InvalidateGeneration(std::uint64_t generation) noexcept
{
    for (auto it = snapshots_.begin(); it != snapshots_.end();)
    {
        if (it->first.generation == generation)
        {
            bytes_used_ -= snapshot_cost(it->second);
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
    bytes_used_ -= snapshot_cost(oldest->second);
    output = std::move(oldest->second);
    snapshots_.erase(oldest);
    return true;
}

void SnapshotStore::Clear() noexcept
{
    snapshots_.clear();
    bytes_used_ = 0;
}
}
