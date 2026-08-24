#pragma once

#include "StageWindTopology.hpp"

namespace Horse::Deterministic
{
class IStageWindAllocator
{
public:
    virtual ~IStageWindAllocator() = default;
    virtual std::uintptr_t Allocate(std::size_t size) noexcept = 0;
    virtual void Free(std::uintptr_t address) noexcept = 0;
};

// Structural transaction used to prove allocator-safe wind graph replacement.
// It is intentionally not wired to SC6 until all non-canonical derived fields
// have a verified rebuild/reconciliation contract.
class StageWindGraphTransaction
{
public:
    StageWindGraphTransaction(
        INativeMemory& memory, IStageWindAllocator& allocator) noexcept;

    Status Restore(
        const StageWindTopologyAddresses& addresses,
        const StageWindTopologyImage& target) noexcept;

private:
    INativeMemory& memory_;
    IStageWindAllocator& allocator_;
};
}
