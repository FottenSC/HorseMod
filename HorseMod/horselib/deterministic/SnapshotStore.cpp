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
      slot_capacity_(maximum_entries),
      policy_(policy)
{
    if (maximum_entries_ == 0
        || maximum_entries_ > maximum_bytes_ / sizeof(Snapshot))
    {
        maximum_entries_ = 0;
        slot_capacity_ = 0;
        return;
    }
    snapshots_.reset(new (std::nothrow) Snapshot[maximum_entries_]);
    free_slots_.reset(new (std::nothrow) std::size_t[maximum_entries_]);
    if (!snapshots_ || !free_slots_)
    {
        snapshots_.reset();
        free_slots_.reset();
        maximum_entries_ = 0;
        slot_capacity_ = 0;
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
        slot_capacity_ = 0;
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
        slot_capacity_ = 0;
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

bool SnapshotStore::can_copy_without_growth(
    const Snapshot& target, const Snapshot& source) noexcept
{
    if (source.bytes.size() > target.bytes.capacity()
        || source.local_images.size() > target.local_images.size())
        return false;
    for (std::size_t index = 0; index < source.local_images.size(); ++index)
    {
        if (source.local_images[index].bytes.size()
            > target.local_images[index].bytes.capacity())
            return false;
    }
    return true;
}

void SnapshotStore::copy_without_growth(
    Snapshot& target, const Snapshot& source) noexcept
{
    target.coordinate = source.coordinate;
    target.context_identity = source.context_identity;
    target.canonical_hash = source.canonical_hash;
    target.canonical_components = source.canonical_components;
    target.canonical_native = source.canonical_native;
    target.canonical_input = source.canonical_input;
    target.canonical_wind_semantic = source.canonical_wind_semantic;
    target.canonical_wind = source.canonical_wind;
    target.canonical_wind_node = source.canonical_wind_node;
    target.canonical_wind_schedule = source.canonical_wind_schedule;
    target.canonical_move_dispatch = source.canonical_move_dispatch;
    target.bytes.resize(source.bytes.size());
    std::copy(source.bytes.begin(), source.bytes.end(), target.bytes.begin());
    target.local_images.resize(source.local_images.size());
    for (std::size_t index = 0; index < source.local_images.size(); ++index)
    {
        auto& destination = target.local_images[index];
        const auto& input = source.local_images[index];
        destination.serializer_id = input.serializer_id;
        destination.serializer_version = input.serializer_version;
        destination.context = input.context;
        destination.cursor = input.cursor;
        destination.checksum = input.checksum;
        destination.bytes.resize(input.bytes.size());
        std::copy(input.bytes.begin(), input.bytes.end(),
            destination.bytes.begin());
    }
}

Status SnapshotStore::PrewarmCopySlots(const Snapshot& prototype) noexcept
{
    if (slot_capacity_ == 0)
        return Status::failure(FailureCode::CapacityExceeded);
    const auto occupied = [this](std::size_t slot) noexcept {
        return std::any_of(entries_.begin(), entries_.end(),
            [slot](const Entry& entry) { return entry.slot == slot; });
    };
    std::size_t highest_occupied{};
    bool have_occupied{};
    for (const auto& entry : entries_)
    {
        highest_occupied = (std::max)(highest_occupied, entry.slot);
        have_occupied = true;
    }
    for (std::size_t slot = 0; slot < slot_capacity_; ++slot)
    {
        if (!occupied(slot)) snapshots_[slot] = {};
    }
    std::size_t allocated = fixed_bytes_;
    try
    {
        for (const auto& entry : entries_)
        {
            auto& target = snapshots_[entry.slot];
            target.bytes.reserve(prototype.bytes.size());
            target.local_images.resize((std::max)(
                target.local_images.size(), prototype.local_images.size()));
            for (std::size_t index = 0;
                    index < prototype.local_images.size(); ++index)
                target.local_images[index].bytes.reserve(
                    prototype.local_images[index].bytes.size());
            allocated += snapshot_dynamic_cost(target);
        }
        if (allocated > maximum_bytes_)
            return Status::failure(FailureCode::CapacityExceeded);
        std::size_t admitted = have_occupied ? highest_occupied + 1 : 0;
        for (std::size_t slot = 0; slot < slot_capacity_; ++slot)
        {
            if (occupied(slot)) continue;
            snapshots_[slot] = prototype;
            const auto cost = snapshot_dynamic_cost(snapshots_[slot]);
            if (cost > maximum_bytes_ - allocated)
            {
                snapshots_[slot] = {};
                break;
            }
            allocated += cost;
            admitted = slot + 1;
        }
        if (admitted == 0 || (have_occupied && admitted <= highest_occupied))
            return Status::failure(FailureCode::CapacityExceeded);
        maximum_entries_ = admitted;
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    if (allocated > maximum_bytes_)
        return Status::failure(FailureCode::CapacityExceeded);
    bytes_used_ = allocated;
    copy_slots_prewarmed_ = true;
    free_slot_count_ = 0;
    for (std::size_t slot = maximum_entries_; slot-- > 0;)
    {
        if (!occupied(slot)) free_slots_[free_slot_count_++] = slot;
    }
    return Status::success();
}

Status SnapshotStore::SaveCopyPrewarmed(const Snapshot& snapshot) noexcept
{
    if (!copy_slots_prewarmed_)
    {
        const auto prewarmed = PrewarmCopySlots(snapshot);
        if (!prewarmed.ok()) return prewarmed;
    }
    auto existing = std::lower_bound(entries_.begin(), entries_.end(),
        snapshot.coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.coordinate < value;
        });
    if (existing != entries_.end()
        && existing->coordinate == snapshot.coordinate)
    {
        auto& target = snapshots_[existing->slot];
        if (!can_copy_without_growth(target, snapshot))
        {
            const auto rewarmed = PrewarmCopySlots(snapshot);
            if (!rewarmed.ok()
                || !can_copy_without_growth(target, snapshot))
                return Status::failure(FailureCode::CapacityExceeded);
        }
        copy_without_growth(target, snapshot);
        return Status::success();
    }
    if (free_slot_count_ == 0 || entries_.size() >= maximum_entries_)
        return Status::failure(FailureCode::CapacityExceeded);
    const auto slot = free_slots_[--free_slot_count_];
    if (!can_copy_without_growth(snapshots_[slot], snapshot))
    {
        ++free_slot_count_;
        const auto rewarmed = PrewarmCopySlots(snapshot);
        if (!rewarmed.ok() || free_slot_count_ == 0)
            return Status::failure(FailureCode::CapacityExceeded);
        const auto rewarmed_slot = free_slots_[--free_slot_count_];
        if (!can_copy_without_growth(snapshots_[rewarmed_slot], snapshot))
        {
            ++free_slot_count_;
            return Status::failure(FailureCode::CapacityExceeded);
        }
        copy_without_growth(snapshots_[rewarmed_slot], snapshot);
        entries_.insert(existing, Entry{snapshot.coordinate, rewarmed_slot});
        return Status::success();
    }
    copy_without_growth(snapshots_[slot], snapshot);
    entries_.insert(existing, Entry{snapshot.coordinate, slot});
    return Status::success();
}

void SnapshotStore::ReleasePrewarmedCopySlots() noexcept
{
    if (!copy_slots_prewarmed_) return;
    entries_.clear();
    for (std::size_t slot = 0; slot < slot_capacity_; ++slot)
        snapshots_[slot] = {};
    maximum_entries_ = slot_capacity_;
    copy_slots_prewarmed_ = false;
    bytes_used_ = fixed_bytes_;
    reset_free_slots();
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
    if (copy_slots_prewarmed_)
        snapshots_[slot].coordinate = {};
    else
    {
        bytes_used_ -= snapshot_dynamic_cost(snapshots_[slot]);
        snapshots_[slot] = {};
    }
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
        if (copy_slots_prewarmed_
            && !can_copy_without_growth(current, replacement))
            return Status::failure(FailureCode::CapacityExceeded);
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
    if (!copy_slots_prewarmed_
        && incoming > maximum_bytes_ - (bytes_used_ - removed))
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
        if (copy_slots_prewarmed_)
            copy_without_growth(snapshots_[slot], replacement);
        else
        {
            bytes_used_ -= snapshot_dynamic_cost(snapshots_[slot]);
            bytes_used_ += snapshot_dynamic_cost(replacement);
            snapshots_[slot] = std::move(replacement);
        }
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
    for (const auto& entry : entries_)
    {
        if (copy_slots_prewarmed_)
            snapshots_[entry.slot].coordinate = {};
        else
            snapshots_[entry.slot] = {};
    }
    entries_.clear();
    reset_free_slots();
    if (!copy_slots_prewarmed_) bytes_used_ = fixed_bytes_;
}
}
