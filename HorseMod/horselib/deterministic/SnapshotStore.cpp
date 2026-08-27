#include "SnapshotStore.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <type_traits>
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
    if (maximum_entries_ == 0
        || maximum_entries_ > maximum_bytes_ / sizeof(Snapshot))
    {
        maximum_entries_ = 0;
        return;
    }
    snapshots_.reset(new (std::nothrow) Snapshot[maximum_entries_]);
    free_slots_.reset(new (std::nothrow) std::size_t[maximum_entries_]);
    if (!snapshots_ || !free_slots_)
    {
        snapshots_.reset();
        free_slots_.reset();
        maximum_entries_ = 0;
        return;
    }
    try
    {
        entries_.reserve(maximum_entries_);
    }
    catch (...)
    {
        snapshots_.reset();
        free_slots_.reset();
        maximum_entries_ = 0;
        return;
    }
    fixed_bytes_ = maximum_entries_
        * (sizeof(Snapshot) + sizeof(std::size_t))
        + entries_.capacity() * sizeof(Entry);
    if (fixed_bytes_ > maximum_bytes_)
    {
        snapshots_.reset();
        free_slots_.reset();
        std::vector<Entry>{}.swap(entries_);
        maximum_entries_ = 0;
        fixed_bytes_ = 0;
        return;
    }
    bytes_used_ = fixed_bytes_;
    reset_free_slots();
}

std::size_t SnapshotStore::snapshot_dynamic_cost(
    const Snapshot& snapshot) const noexcept
{
    std::size_t cost = snapshot.bytes.capacity()
        + snapshot.local_images.capacity() * sizeof(LocalReconstructionImage);
    for (const auto& local : snapshot.local_images)
        cost += local.bytes.capacity();
    return cost;
}

void SnapshotStore::reset_free_slots() noexcept
{
    free_slot_count_ = maximum_entries_;
    for (std::size_t index = 0; index < maximum_entries_; ++index)
        free_slots_[index] = maximum_entries_ - index - 1;
}

void SnapshotStore::release_entry(
    std::vector<Entry>::iterator entry) noexcept
{
    const auto slot = entry->slot;
    bytes_used_ -= snapshot_dynamic_cost(snapshots_[slot]);
    snapshots_[slot] = {};
    free_slots_[free_slot_count_++] = slot;
    entries_.erase(entry);
}

void SnapshotStore::erase_oldest() noexcept
{
    if (!entries_.empty()) release_entry(entries_.begin());
}

Status SnapshotStore::Save(Snapshot snapshot) noexcept
{
    static_assert(std::is_nothrow_move_assignable_v<Snapshot>);
    const std::size_t incoming = snapshot_dynamic_cost(snapshot);
    if (maximum_entries_ == 0 || incoming > maximum_bytes_ - fixed_bytes_)
        return Status::failure(FailureCode::CapacityExceeded);

    auto existing = std::lower_bound(entries_.begin(), entries_.end(),
        snapshot.coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    const bool replacing = existing != entries_.end()
        && existing->coordinate == snapshot.coordinate;
    const std::size_t replaced = replacing
        ? snapshot_dynamic_cost(snapshots_[existing->slot]) : 0;
    const std::size_t effective_count = entries_.size() - (replacing ? 1 : 0);
    if (policy_ == CapacityPolicy::RejectNew
        && (bytes_used_ - replaced + incoming > maximum_bytes_
            || effective_count >= maximum_entries_))
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }

    if (replacing) release_entry(existing);
    while (bytes_used_ + incoming > maximum_bytes_
        || entries_.size() >= maximum_entries_)
    {
        if (policy_ == CapacityPolicy::RejectNew || entries_.empty())
            return Status::failure(FailureCode::CapacityExceeded);
        erase_oldest();
    }
    if (free_slot_count_ == 0)
        return Status::failure(FailureCode::CapacityExceeded);

    const auto slot = free_slots_[--free_slot_count_];
    snapshots_[slot] = std::move(snapshot);
    const auto insertion = std::lower_bound(entries_.begin(), entries_.end(),
        snapshots_[slot].coordinate,
        [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    try
    {
        entries_.insert(insertion, Entry{snapshots_[slot].coordinate, slot});
    }
    catch (...)
    {
        snapshots_[slot] = {};
        free_slots_[free_slot_count_++] = slot;
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
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    return found != entries_.end() && found->coordinate == coordinate
        ? &snapshots_[found->slot] : nullptr;
}

const Snapshot* SnapshotStore::FindNearestAtOrBefore(
    FrameCoordinate coordinate) const noexcept
{
    auto found = std::upper_bound(entries_.begin(), entries_.end(),
        coordinate, [](FrameCoordinate value, const Entry& entry) {
            return value < entry.coordinate;
        });
    while (found != entries_.begin())
    {
        --found;
        if (found->coordinate.generation == coordinate.generation)
            return &snapshots_[found->slot];
        if (found->coordinate.generation < coordinate.generation) break;
    }
    return nullptr;
}

Status SnapshotStore::ValidateExactReplacement(
    std::span<const Snapshot> replacements,
    std::span<const CanonicalHash> expected_hashes) const noexcept
{
    if (replacements.size() != expected_hashes.size())
        return Status::failure(FailureCode::InvalidConfiguration);
    std::size_t removed{};
    std::size_t incoming{};
    FrameCoordinate previous{};
    bool have_previous{};
    for (std::size_t index = 0; index < replacements.size(); ++index)
    {
        const auto& replacement = replacements[index];
        if (replacement.coordinate.generation == 0
            || (have_previous && !(previous < replacement.coordinate)))
            return Status::failure(FailureCode::InvalidConfiguration);
        const auto found = std::lower_bound(entries_.begin(), entries_.end(),
            replacement.coordinate,
            [](const Entry& entry, FrameCoordinate value) {
                return entry.coordinate < value;
            });
        if (found == entries_.end()
            || found->coordinate != replacement.coordinate)
            return Status::failure(FailureCode::MissingSnapshot);
        const auto& current = snapshots_[found->slot];
        if (current.canonical_hash != expected_hashes[index])
            return Status::failure(FailureCode::IdentityMismatch);
        const auto old_cost = snapshot_dynamic_cost(current);
        const auto new_cost = snapshot_dynamic_cost(replacement);
        if (removed > (std::numeric_limits<std::size_t>::max)() - old_cost
            || incoming > (std::numeric_limits<std::size_t>::max)() - new_cost)
            return Status::failure(FailureCode::CapacityExceeded);
        removed += old_cost;
        incoming += new_cost;
        previous = replacement.coordinate;
        have_previous = true;
    }
    if (incoming > maximum_bytes_ - (bytes_used_ - removed))
        return Status::failure(FailureCode::CapacityExceeded);
    return Status::success();
}

void SnapshotStore::CommitValidatedExactReplacement(
    std::span<Snapshot> replacements) noexcept
{
    for (auto& replacement : replacements)
    {
        const auto found = std::lower_bound(entries_.begin(), entries_.end(),
            replacement.coordinate,
            [](const Entry& entry, FrameCoordinate value) {
                return entry.coordinate < value;
            });
        const auto slot = found->slot;
        bytes_used_ -= snapshot_dynamic_cost(snapshots_[slot]);
        bytes_used_ += snapshot_dynamic_cost(replacement);
        snapshots_[slot] = std::move(replacement);
    }
}

void SnapshotStore::InvalidateGeneration(std::uint64_t generation) noexcept
{
    for (auto entry = entries_.begin(); entry != entries_.end();)
    {
        if (entry->coordinate.generation == generation)
        {
            const auto index = static_cast<std::size_t>(entry - entries_.begin());
            release_entry(entry);
            entry = entries_.begin() + (std::min)(index, entries_.size());
        }
        else ++entry;
    }
}

std::size_t SnapshotStore::BytesUsed() const noexcept
{
    return bytes_used_;
}

bool SnapshotStore::TakeOldestIfFull(Snapshot& output) noexcept
{
    if (policy_ != CapacityPolicy::EvictOldest
        || entries_.size() < maximum_entries_ || entries_.empty())
        return false;
    const auto slot = entries_.front().slot;
    bytes_used_ -= snapshot_dynamic_cost(snapshots_[slot]);
    output = std::move(snapshots_[slot]);
    snapshots_[slot] = {};
    free_slots_[free_slot_count_++] = slot;
    entries_.erase(entries_.begin());
    return true;
}

void SnapshotStore::Clear() noexcept
{
    for (const auto& entry : entries_) snapshots_[entry.slot] = {};
    entries_.clear();
    reset_free_slots();
    bytes_used_ = fixed_bytes_;
}
}
