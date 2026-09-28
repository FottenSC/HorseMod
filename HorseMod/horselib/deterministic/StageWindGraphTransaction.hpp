#pragma once

#include "StageWindTopology.hpp"
#include <limits>

namespace Horse::Deterministic
{
class IStageWindAllocator
{
public:
    virtual ~IStageWindAllocator() = default;
    virtual std::uintptr_t Allocate(std::size_t size) noexcept = 0;
    virtual void Free(std::uintptr_t address) noexcept = 0;
    virtual std::size_t AllocationBytes(std::size_t requested) noexcept { return requested; }
};

// Structural transaction for allocator-safe wind graph replacement. Restore
// uses fixed stack scratch only; native node allocation remains delegated to
// the game's verified allocator and no C++ heap allocation occurs per rewind.
class StageWindGraphTransaction
{
public:
    StageWindGraphTransaction(
        INativeMemory& memory, IStageWindAllocator& allocator) noexcept;

    Status Restore(
        const StageWindTopologyAddresses& addresses,
        const StageWindTopologyImage& target) noexcept;
    Status Prepare(const StageWindTopologyAddresses& addresses,
        const StageWindTopologyImage& target, bool enclosing = false,
        std::size_t budget = (std::numeric_limits<std::size_t>::max)()) noexcept;
    [[nodiscard]] std::size_t AllocationEnvelopeBytes() noexcept;
    Status Publish() noexcept;
    Status Undo() noexcept;
    Status ValidateCommit() const noexcept;
    Status Commit() noexcept;
    Status BeginExecution(std::size_t retirement_budget) noexcept;
    Status SettleExecution() noexcept;
    Status ReopenExecutionForUndo() noexcept;
    bool pending() const noexcept { return prepared_; }
    bool published() const noexcept { return published_; }
    bool enclosing() const noexcept { return enclosing_; }
    std::size_t owned_bytes() const noexcept { return owned_bytes_; }

private:
    Status ValidateGraph(bool target, bool root) const noexcept;
    void DiscardPrepared() noexcept;
    INativeMemory& memory_;
    IStageWindAllocator& allocator_;
    StageWindTopologyAddresses addresses_{};
    std::uintptr_t root_{};
    std::array<std::byte, 0xf0> original_root_{}, target_root_{};
    struct Node {
        std::uintptr_t address{};
        std::size_t size{};
        std::array<std::byte, 0x1e0> bytes{};
    };
    std::array<Node, 64> original_nodes_{}, target_nodes_{};
    std::size_t original_count_{}, target_count_{}, owned_bytes_{};
    // Both pointer-free images are prepared before publication; incoming
    // enclosing A/B restores must match one of these exact images.
    StageWindTopologyImage original_image_, target_image_;
    bool prepared_{}, published_{}, write_complete_{}, undo_started_{}, enclosing_{}, recovered_{};
    bool executing_{}, execution_settled_{};
};
}
