#pragma once

#include "Interfaces.hpp"

#include <memory>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
enum class CapacityPolicy : std::uint8_t { RejectNew, EvictOldest };

// Native frame coordinates are observations. A scheduling interval can emit
// zero or several of them; its retained state needs an independent identity.
struct NativeIntervalId
{
    std::uint64_t ownership_epoch{};
    std::uint64_t sequence{};
    friend constexpr bool operator==(NativeIntervalId, NativeIntervalId) = default;
    friend constexpr auto operator<=>(NativeIntervalId, NativeIntervalId) = default;
};

enum class SnapshotIndex : std::uint8_t { NativeCoordinate, NativeInterval };

[[nodiscard]] bool CanCopySnapshotWithoutGrowth(
    const Snapshot& target, const Snapshot& source) noexcept;
void CopySnapshotWithoutGrowth(
    Snapshot& target, const Snapshot& source) noexcept;
[[nodiscard]] Status PrepareLocalReconstructionCopyStorage(
    std::vector<LocalReconstructionImage>& target,
    const std::vector<LocalReconstructionImage>& source) noexcept;
[[nodiscard]] Status PrepareSnapshotCopyStorage(
    Snapshot& target, const Snapshot& source) noexcept;
[[nodiscard]] Status PrepareSnapshotCaptureStorage(
    Snapshot& target, const Snapshot& prototype) noexcept;

class SnapshotStore final : public ISnapshotStore
{
public:
    SnapshotStore(
        std::size_t maximum_bytes,
        std::size_t maximum_entries,
        CapacityPolicy policy,
        SnapshotIndex index = SnapshotIndex::NativeCoordinate) noexcept;

    Status Save(Snapshot snapshot) noexcept override;
    // Qualification history uses a fixed allocator shape learned from the
    // first native checkpoint. Every admitted slot is allocated before
    // online status 4; later captures copy into those buffers without growth.
    Status PrewarmCopySlots(const Snapshot& prototype) noexcept;
    // Online history receives future CaptureTransient outputs whose encoded
    // length can vary by content. Admit fewer slots if necessary so every
    // retained/free slot owns the full bounded capture envelope at status 4.
    Status PrewarmCaptureSlots(const Snapshot& prototype) noexcept;
    Status SaveCopyPrewarmed(const Snapshot& snapshot) noexcept;
    // Insert-only. A saved interval can change only through validated replacement.
    Status SaveIntervalCopyPrewarmed(
        NativeIntervalId interval, const Snapshot& snapshot) noexcept;
    // Borrowed until the next store mutation; tokens retain IDs/hashes, never pointers.
    [[nodiscard]] const Snapshot* FindInterval(NativeIntervalId interval) const noexcept;
    void DiscardIntervalsBeforeRetainingNearest(NativeIntervalId minimum) noexcept;
    [[nodiscard]] Status ValidateIntervalReplacement(
        std::span<const NativeIntervalId> intervals,
        std::span<const Snapshot> replacements,
        std::span<const CanonicalHash> expected_hashes) const noexcept;
    // Requires successful validation with unchanged inputs and no intervening
    // store mutation, on the same owner thread.
    void CommitValidatedIntervalReplacement(
        std::span<const NativeIntervalId> intervals,
        std::span<Snapshot> replacements) noexcept;
    void ReleasePrewarmedCopySlots() noexcept;
    [[nodiscard]] std::optional<Snapshot> Load(
        FrameCoordinate coordinate) const override;
    [[nodiscard]] std::optional<Snapshot> NearestAtOrBefore(
        FrameCoordinate coordinate) const override;
    [[nodiscard]] const Snapshot* FindExact(
        FrameCoordinate coordinate) const noexcept;
    [[nodiscard]] const Snapshot* FindNearestAtOrBefore(
        FrameCoordinate coordinate) const noexcept;
    [[nodiscard]] Status ValidateExactReplacement(
        std::span<const Snapshot> replacements,
        std::span<const CanonicalHash> expected_hashes) const noexcept;
    // Requires a successful ValidateExactReplacement on this same thread.
    // Coordinates and entry slots are immutable, so committing only performs
    // noexcept moves into the already-owned slots.
    void CommitValidatedExactReplacement(
        std::span<Snapshot> replacements) noexcept;
    void InvalidateGeneration(std::uint64_t generation) noexcept override;
    // Retire a confirmed prefix while retaining the closest prior checkpoint
    // as a resimulation anchor. No allocation or capacity change is allowed.
    void DiscardBeforeRetainingNearest(FrameCoordinate minimum) noexcept;
    [[nodiscard]] std::size_t BytesUsed() const noexcept override;
    [[nodiscard]] std::size_t entry_count() const noexcept
    {
        return entries_.size();
    }
    // Qualification ring helper: once the fixed entry capacity is warm,
    // transfer the oldest buffers to the next capture instead of returning
    // them to the allocator. Only valid for EvictOldest stores.
    [[nodiscard]] bool TakeOldestIfFull(Snapshot& output) noexcept;
    void Clear() noexcept;

private:
    struct Entry
    {
        FrameCoordinate key{};
        std::size_t slot{};
    };

    [[nodiscard]] std::size_t snapshot_dynamic_cost(
        const Snapshot& snapshot) const noexcept;
    Status prewarm_slots(
        const Snapshot& prototype, bool capture_envelope) noexcept;
    Status save_copy_at_key(FrameCoordinate key, const Snapshot& snapshot) noexcept;
    [[nodiscard]] const Snapshot* find_key(FrameCoordinate key) const noexcept;
    void discard_before_key_retaining_nearest(FrameCoordinate minimum) noexcept;
    void release_entry(std::vector<Entry>::iterator entry) noexcept;
    void erase_oldest() noexcept;
    void reset_free_slots() noexcept;

    std::size_t maximum_bytes_{};
    std::size_t maximum_entries_{};
    std::size_t slot_capacity_{};
    CapacityPolicy policy_{CapacityPolicy::RejectNew};
    SnapshotIndex index_{SnapshotIndex::NativeCoordinate};
    std::size_t fixed_bytes_{};
    std::size_t bytes_used_{};
    std::unique_ptr<Snapshot[]> snapshots_;
    std::unique_ptr<std::size_t[]> free_slots_;
    std::size_t free_slot_count_{};
    std::vector<Entry> entries_;
    bool copy_slots_prewarmed_{};
    // Full-envelope admission is the ownership boundary. A save may never
    // repair an insufficient envelope by allocating after that boundary.
    bool capture_slots_frozen_{};
};
}
