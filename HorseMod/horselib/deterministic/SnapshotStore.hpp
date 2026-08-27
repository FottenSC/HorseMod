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
    void release_entry(std::vector<Entry>::iterator entry) noexcept;
    void erase_oldest() noexcept;
    void reset_free_slots() noexcept;

    std::size_t maximum_bytes_{};
    std::size_t maximum_entries_{};
    CapacityPolicy policy_{CapacityPolicy::RejectNew};
    std::size_t fixed_bytes_{};
    std::size_t bytes_used_{};
    std::unique_ptr<Snapshot[]> snapshots_;
    std::unique_ptr<std::size_t[]> free_slots_;
    std::size_t free_slot_count_{};
    std::vector<Entry> entries_;
};
}
