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

private:
    INativeMemory& memory_;
    IStageWindAllocator& allocator_;
};
}
