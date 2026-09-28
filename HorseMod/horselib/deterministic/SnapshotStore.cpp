#include "SnapshotStore.hpp"

#include "CandidateCheckpoint.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace Horse::Deterministic
{
namespace
{
Status PrepareOccupiedSnapshotCopyStorage(
    Snapshot& target, const Snapshot& prototype,
    bool capture_envelope) noexcept
{
    // An occupied slot's Entry coordinate indexes this exact payload. Growing
    // its reusable buffers must not replace that payload with the newest
    // prototype: doing so leaves the sorted index and snapshot identity out
    // of sync. The local-image topology is binding-owned and must remain
    // stable while a checkpoint is retained.
    if (prototype.local_images.size() > target.local_images.size())
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        target.bytes.reserve(capture_envelope
            ? candidate_checkpoint_capture_byte_capacity
            : prototype.bytes.capacity());
        target.local_images.reserve(capture_envelope
            ? maximum_local_reconstruction_images
            : prototype.local_images.capacity());
        for (std::size_t index = 0;
             index < prototype.local_images.size(); ++index)
        {
            target.local_images[index].bytes.reserve(
                prototype.local_images[index].bytes.capacity());
        }
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}
}

SnapshotStore::SnapshotStore(
    std::size_t maximum_bytes,
    std::size_t maximum_entries,
    CapacityPolicy policy, SnapshotIndex index) noexcept
    : maximum_bytes_(maximum_bytes),
      maximum_entries_(maximum_entries),
      slot_capacity_(maximum_entries),
      policy_(policy), index_(index)
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

bool CanCopySnapshotWithoutGrowth(
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

void CopySnapshotWithoutGrowth(
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
    target.canonical_animation = source.canonical_animation;
    target.canonical_stage_emitters = source.canonical_stage_emitters;
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

Status PrepareSnapshotCopyStorage(
    Snapshot& target, const Snapshot& source) noexcept
{
    try
    {
        target.bytes.reserve(source.bytes.capacity());
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    const auto locals = PrepareLocalReconstructionCopyStorage(
        target.local_images, source.local_images);
    if (!locals.ok()) return locals;
    if (!CanCopySnapshotWithoutGrowth(target, source))
        return Status::failure(FailureCode::CapacityExceeded);
    CopySnapshotWithoutGrowth(target, source);
    return Status::success();
}

Status PrepareLocalReconstructionCopyStorage(
    std::vector<LocalReconstructionImage>& target,
    const std::vector<LocalReconstructionImage>& source) noexcept
{
    try
    {
        target.reserve(source.capacity());
        target.resize(source.size());
        for (std::size_t index = 0; index < source.size(); ++index)
            target[index].bytes.reserve(source[index].bytes.capacity());
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

Status PrepareSnapshotCaptureStorage(
    Snapshot& target, const Snapshot& prototype) noexcept
{
    try
    {
        target.bytes.reserve(candidate_checkpoint_capture_byte_capacity);
        // EncodeCaptured exchanges this vector with CandidateCheckpointImage,
        // whose admitted storage always reserves the maximum local-image
        // count.  Matching that capacity here makes the exchange ownership-
        // neutral even when the current prototype only contains two images.
        target.local_images.reserve(maximum_local_reconstruction_images);
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    const auto locals = PrepareLocalReconstructionCopyStorage(
        target.local_images, prototype.local_images);
    if (!locals.ok()) return locals;
    CopySnapshotWithoutGrowth(target, prototype);
    return Status::success();
}

Status SnapshotStore::PrewarmCopySlots(const Snapshot& prototype) noexcept
{
    return prewarm_slots(prototype, false);
}

Status SnapshotStore::PrewarmCaptureSlots(const Snapshot& prototype) noexcept
{
    const auto status = prewarm_slots(prototype, true);
    if (status.ok()) capture_slots_frozen_ = true;
    return status;
}

Status SnapshotStore::prewarm_slots(
    const Snapshot& prototype, bool capture_envelope) noexcept
{
    if (slot_capacity_ == 0)
        return Status::failure(FailureCode::CapacityExceeded);
    // Preparation may reserve several vectors before an allocator/budget
    // failure. Always account their actual retained capacities, including the
    // failing slot. Occupied payloads and coordinate indexes remain intact.
    bool complete = false;
    const auto reconcile = [this, &complete](void*) noexcept {
        if (complete) return;
        bytes_used_ = fixed_bytes_;
        for (std::size_t slot = 0; slot < slot_capacity_; ++slot)
            bytes_used_ += snapshot_dynamic_cost(snapshots_[slot]);
        copy_slots_prewarmed_ = false;
        capture_slots_frozen_ = false;
    };
    std::unique_ptr<void, decltype(reconcile)> preparation_guard(this, reconcile);
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
            const auto prepared = PrepareOccupiedSnapshotCopyStorage(
                target, prototype, capture_envelope);
            if (!prepared.ok()) return prepared;
            allocated += snapshot_dynamic_cost(target);
        }
        if (allocated > maximum_bytes_)
            return Status::failure(FailureCode::CapacityExceeded);
        std::size_t admitted = have_occupied ? highest_occupied + 1 : 0;
        for (std::size_t slot = 0; slot < slot_capacity_; ++slot)
        {
            if (occupied(slot)) continue;
            const auto prepared = capture_envelope
                ? PrepareSnapshotCaptureStorage(snapshots_[slot], prototype)
                : PrepareSnapshotCopyStorage(snapshots_[slot], prototype);
            if (!prepared.ok()) return prepared;
            const auto cost = snapshot_dynamic_cost(snapshots_[slot]);
            if (cost > maximum_bytes_ - allocated)
            {
                snapshots_[slot] = {};
                if (have_occupied && slot < highest_occupied)
                    return Status::failure(FailureCode::CapacityExceeded);
                break;
            }
            allocated += cost;
            admitted = (std::max)(admitted, slot + 1);
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
    complete = true;
    return Status::success();
}

Status SnapshotStore::SaveCopyPrewarmed(const Snapshot& snapshot) noexcept
{
    if (index_ != SnapshotIndex::NativeCoordinate)
        return Status::failure(FailureCode::InvalidConfiguration);
    return save_copy_at_key(snapshot.coordinate, snapshot);
}

Status SnapshotStore::SaveIntervalCopyPrewarmed(
    NativeIntervalId interval, const Snapshot& snapshot) noexcept
{
    if (index_ != SnapshotIndex::NativeInterval || interval.ownership_epoch == 0
        || snapshot.coordinate.generation == 0)
        return Status::failure(FailureCode::InvalidConfiguration);
    // Owned intervals must have their complete storage envelope admitted
    // before the first save. Never allocate as an incidental save side effect.
    if (!capture_slots_frozen_ || !copy_slots_prewarmed_)
        return Status::failure(FailureCode::CapacityExceeded);
    if (FindInterval(interval) != nullptr)
        return Status::failure(FailureCode::IdentityMismatch);
    return save_copy_at_key({interval.ownership_epoch, interval.sequence}, snapshot);
}

Status SnapshotStore::save_copy_at_key(
    FrameCoordinate key, const Snapshot& snapshot) noexcept
{
    if (!copy_slots_prewarmed_)
    {
        if (capture_slots_frozen_)
            return Status::failure(FailureCode::CapacityExceeded);
        const auto prewarmed = PrewarmCopySlots(snapshot);
        if (!prewarmed.ok()) return prewarmed;
    }
    auto existing = std::lower_bound(entries_.begin(), entries_.end(),
        key, [](const Entry& entry, FrameCoordinate value) {
            return entry.key < value;
        });
    if (existing != entries_.end()
        && existing->key == key)
    {
        auto& target = snapshots_[existing->slot];
        if (!CanCopySnapshotWithoutGrowth(target, snapshot))
        {
            if (capture_slots_frozen_)
                return Status::failure(FailureCode::CapacityExceeded);
            const auto rewarmed = PrewarmCopySlots(snapshot);
            if (!rewarmed.ok()
                || !CanCopySnapshotWithoutGrowth(target, snapshot))
                return Status::failure(FailureCode::CapacityExceeded);
        }
        CopySnapshotWithoutGrowth(target, snapshot);
        return Status::success();
    }
    if (free_slot_count_ == 0 || entries_.size() >= maximum_entries_)
        return Status::failure(FailureCode::CapacityExceeded);
    const auto slot = free_slots_[--free_slot_count_];
    if (!CanCopySnapshotWithoutGrowth(snapshots_[slot], snapshot))
    {
        ++free_slot_count_;
        if (capture_slots_frozen_)
            return Status::failure(FailureCode::CapacityExceeded);
        const auto rewarmed = PrewarmCopySlots(snapshot);
        if (!rewarmed.ok() || free_slot_count_ == 0)
            return Status::failure(FailureCode::CapacityExceeded);
        const auto rewarmed_slot = free_slots_[--free_slot_count_];
        if (!CanCopySnapshotWithoutGrowth(snapshots_[rewarmed_slot], snapshot))
        {
            ++free_slot_count_;
            return Status::failure(FailureCode::CapacityExceeded);
        }
        CopySnapshotWithoutGrowth(snapshots_[rewarmed_slot], snapshot);
        entries_.insert(existing, Entry{key, rewarmed_slot});
        return Status::success();
    }
    CopySnapshotWithoutGrowth(snapshots_[slot], snapshot);
    entries_.insert(existing, Entry{key, slot});
    return Status::success();
}

void SnapshotStore::ReleasePrewarmedCopySlots() noexcept
{
    // Failed preparation can also retain partially allocated envelopes.
    entries_.clear();
    for (std::size_t slot = 0; slot < slot_capacity_; ++slot)
        snapshots_[slot] = {};
    maximum_entries_ = slot_capacity_;
    copy_slots_prewarmed_ = false;
    capture_slots_frozen_ = false;
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
    if (index_ != SnapshotIndex::NativeCoordinate)
        return Status::failure(FailureCode::InvalidConfiguration);
    static_assert(std::is_nothrow_move_assignable_v<Snapshot>);
    const std::size_t incoming = snapshot_dynamic_cost(snapshot);
    if (maximum_entries_ == 0 || incoming > maximum_bytes_ - fixed_bytes_)
        return Status::failure(FailureCode::CapacityExceeded);

    auto existing = std::lower_bound(entries_.begin(), entries_.end(),
        snapshot.coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.key < value;
        });
    const bool replacing = existing != entries_.end()
        && existing->key == snapshot.coordinate;
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
            return entry.key < value;
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

const Snapshot* SnapshotStore::FindExact(FrameCoordinate coordinate) const noexcept
{
    return index_ == SnapshotIndex::NativeCoordinate ? find_key(coordinate) : nullptr;
}

const Snapshot* SnapshotStore::FindInterval(NativeIntervalId interval) const noexcept
{
    return index_ == SnapshotIndex::NativeInterval && interval.ownership_epoch != 0
        ? find_key({interval.ownership_epoch, interval.sequence}) : nullptr;
}

const Snapshot* SnapshotStore::find_key(
    FrameCoordinate coordinate) const noexcept
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(),
        coordinate, [](const Entry& entry, FrameCoordinate value) {
            return entry.key < value;
        });
    return found != entries_.end() && found->key == coordinate
        ? &snapshots_[found->slot] : nullptr;
}

const Snapshot* SnapshotStore::FindNearestAtOrBefore(
    FrameCoordinate coordinate) const noexcept
{
    if (index_ != SnapshotIndex::NativeCoordinate) return nullptr;
    auto found = std::upper_bound(entries_.begin(), entries_.end(),
        coordinate, [](FrameCoordinate value, const Entry& entry) {
            return value < entry.key;
        });
    while (found != entries_.begin())
    {
        --found;
        if (found->key.generation == coordinate.generation)
            return &snapshots_[found->slot];
        if (found->key.generation < coordinate.generation) break;
    }
    return nullptr;
}

Status SnapshotStore::ValidateExactReplacement(
    std::span<const Snapshot> replacements,
    std::span<const CanonicalHash> expected_hashes) const noexcept
{
    if (index_ != SnapshotIndex::NativeCoordinate
        || replacements.size() != expected_hashes.size())
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
                return entry.key < value;
            });
        if (found == entries_.end()
            || found->key != replacement.coordinate)
            return Status::failure(FailureCode::MissingSnapshot);
        const auto& current = snapshots_[found->slot];
        if (current.canonical_hash != expected_hashes[index])
            return Status::failure(FailureCode::IdentityMismatch);
        if (copy_slots_prewarmed_
            && !CanCopySnapshotWithoutGrowth(current, replacement))
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
                return entry.key < value;
            });
        const auto slot = found->slot;
        if (copy_slots_prewarmed_)
            CopySnapshotWithoutGrowth(snapshots_[slot], replacement);
        else
        {
            bytes_used_ -= snapshot_dynamic_cost(snapshots_[slot]);
            bytes_used_ += snapshot_dynamic_cost(replacement);
            snapshots_[slot] = std::move(replacement);
        }
    }
}

Status SnapshotStore::ValidateIntervalReplacement(
    std::span<const NativeIntervalId> intervals,
    std::span<const Snapshot> replacements,
    std::span<const CanonicalHash> expected_hashes) const noexcept
{
    if (index_ != SnapshotIndex::NativeInterval
        || intervals.size() != replacements.size()
        || intervals.size() != expected_hashes.size())
        return Status::failure(FailureCode::InvalidConfiguration);
    if (!capture_slots_frozen_ || !copy_slots_prewarmed_)
        return Status::failure(FailureCode::CapacityExceeded);
    for (std::size_t index = 0; index < intervals.size(); ++index)
    {
        if (intervals[index].ownership_epoch == 0
            || (index != 0 && (intervals[index].ownership_epoch
                    != intervals[0].ownership_epoch
                || !(intervals[index - 1] < intervals[index]))))
            return Status::failure(FailureCode::InvalidConfiguration);
        const auto* current = FindInterval(intervals[index]);
        if (current == nullptr) return Status::failure(FailureCode::MissingSnapshot);
        const auto& replacement = replacements[index];
        if (current->canonical_hash != expected_hashes[index]
            || current->context_identity != replacement.context_identity
            || current->coordinate.generation != replacement.coordinate.generation)
            return Status::failure(FailureCode::IdentityMismatch);
        if (!CanCopySnapshotWithoutGrowth(*current, replacement))
            return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

void SnapshotStore::CommitValidatedIntervalReplacement(
    std::span<const NativeIntervalId> intervals,
    std::span<Snapshot> replacements) noexcept
{
    // Like coordinate replacements, admission and commit share the owner
    // thread. No index, slot, capacity, or allocation changes during commit.
    for (std::size_t index = 0; index < intervals.size(); ++index)
    {
        const FrameCoordinate key{intervals[index].ownership_epoch, intervals[index].sequence};
        const auto found = std::lower_bound(entries_.begin(), entries_.end(), key,
            [](const Entry& entry, FrameCoordinate value) { return entry.key < value; });
        CopySnapshotWithoutGrowth(snapshots_[found->slot], replacements[index]);
    }
}

void SnapshotStore::InvalidateGeneration(std::uint64_t generation) noexcept
{
    for (auto entry = entries_.begin(); entry != entries_.end();)
    {
        if (snapshots_[entry->slot].coordinate.generation == generation)
        {
            const auto index = static_cast<std::size_t>(entry - entries_.begin());
            release_entry(entry);
            entry = entries_.begin() + (std::min)(index, entries_.size());
        }
        else ++entry;
    }
}

void SnapshotStore::DiscardBeforeRetainingNearest(FrameCoordinate minimum) noexcept
{
    if (index_ == SnapshotIndex::NativeCoordinate)
        discard_before_key_retaining_nearest(minimum);
}

void SnapshotStore::DiscardIntervalsBeforeRetainingNearest(NativeIntervalId minimum) noexcept
{
    if (index_ == SnapshotIndex::NativeInterval && minimum.ownership_epoch != 0)
        discard_before_key_retaining_nearest({minimum.ownership_epoch, minimum.sequence});
}

void SnapshotStore::discard_before_key_retaining_nearest(
    FrameCoordinate minimum) noexcept
{
    auto first = std::lower_bound(entries_.begin(), entries_.end(),
        minimum, [](const Entry& entry, FrameCoordinate value) {
            return entry.key < value;
        });
    std::size_t discard = static_cast<std::size_t>(first - entries_.begin());
    if (discard != 0
        && (first == entries_.end() || first->key != minimum)
        && (index_ != SnapshotIndex::NativeInterval
            || entries_[discard - 1].key.generation == minimum.generation))
        --discard;
    while (discard-- != 0) release_entry(entries_.begin());
}

std::size_t SnapshotStore::BytesUsed() const noexcept
{
    return bytes_used_;
}

bool SnapshotStore::TakeOldestIfFull(Snapshot& output) noexcept
{
    if (index_ != SnapshotIndex::NativeCoordinate
        || policy_ != CapacityPolicy::EvictOldest
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
