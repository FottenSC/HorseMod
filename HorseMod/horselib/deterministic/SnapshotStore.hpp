#pragma once

#include "Interfaces.hpp"

#include <map>

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
    void InvalidateGeneration(std::uint64_t generation) noexcept override;
    [[nodiscard]] std::size_t BytesUsed() const noexcept override;
    // Qualification ring helper: once the fixed entry capacity is warm,
    // transfer the oldest buffers to the next capture instead of returning
    // them to the allocator. Only valid for EvictOldest stores.
    [[nodiscard]] bool TakeOldestIfFull(Snapshot& output) noexcept;
    void Clear() noexcept;

private:
    [[nodiscard]] std::size_t snapshot_cost(const Snapshot& snapshot) const noexcept;
    void erase_oldest() noexcept;

    std::size_t maximum_bytes_{};
    std::size_t maximum_entries_{};
    CapacityPolicy policy_{CapacityPolicy::RejectNew};
    std::size_t bytes_used_{};
    std::map<FrameCoordinate, Snapshot> snapshots_;
};
}
