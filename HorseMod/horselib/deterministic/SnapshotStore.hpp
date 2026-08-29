#pragma once

#include "Interfaces.hpp"

#include <memory>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
enum class CapacityPolicy : std::uint8_t { RejectNew, EvictOldest };

class SnapshotStore final : public ISnapshotStore
{
public:
    SnapshotStore(
        std::size_t maximum_bytes,
        std::size_t maximum_entries,
        CapacityPolicy policy) noexcept;

    Status Save(Snapshot snapshot) noexcept override;
    // Qualification history uses a fixed allocator shape learned from the
    // first native checkpoint. Every admitted slot is allocated before
    // online status 4; later captures copy into those buffers without growth.
    Status PrewarmCopySlots(const Snapshot& prototype) noexcept;
    Status SaveCopyPrewarmed(const Snapshot& snapshot) noexcept;
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
    [[nodiscard]] std::size_t BytesUsed() const noexcept override;
    // Qualification ring helper: once the fixed entry capacity is warm,
    // transfer the oldest buffers to the next capture instead of returning
    // them to the allocator. Only valid for EvictOldest stores.
    [[nodiscard]] bool TakeOldestIfFull(Snapshot& output) noexcept;
    void Clear() noexcept;

private:
    struct Entry
    {
        FrameCoordinate coordinate{};
        std::size_t slot{};
    };

    [[nodiscard]] std::size_t snapshot_dynamic_cost(
        const Snapshot& snapshot) const noexcept;
    [[nodiscard]] static bool can_copy_without_growth(
        const Snapshot& target, const Snapshot& source) noexcept;
    static void copy_without_growth(
        Snapshot& target, const Snapshot& source) noexcept;
    void release_entry(std::vector<Entry>::iterator entry) noexcept;
    void erase_oldest() noexcept;
    void reset_free_slots() noexcept;

    std::size_t maximum_bytes_{};
    std::size_t maximum_entries_{};
    std::size_t slot_capacity_{};
    CapacityPolicy policy_{CapacityPolicy::RejectNew};
    std::size_t fixed_bytes_{};
    std::size_t bytes_used_{};
    std::unique_ptr<Snapshot[]> snapshots_;
    std::unique_ptr<std::size_t[]> free_slots_;
    std::size_t free_slot_count_{};
    std::vector<Entry> entries_;
    bool copy_slots_prewarmed_{};
};
}
