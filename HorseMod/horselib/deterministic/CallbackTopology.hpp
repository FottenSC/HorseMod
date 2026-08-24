#pragma once

#include "NativeCandidateRegions.hpp"

#include <span>
#include <vector>

namespace Horse::Deterministic
{
enum class CallbackCollectionRole : std::uint8_t
{
    InputFilter = 1,
    MoveState = 2,
    Simulation = 3,
    Round = 4,
    Unpause = 5,
};

struct CallbackCollectionRef
{
    CallbackCollectionRole role{};
    std::uintptr_t address{};
};

struct CallbackTopologyRecord
{
    CallbackCollectionRole role{};
    std::uint16_t collection_order{};
    std::uint16_t dispatch_order{};
    std::int32_t owner_object_index{};
    std::int32_t owner_serial_number{};
    std::uint64_t owner_class_token{};
    std::uint32_t wrapper_vtable_rva{};
    std::uint32_t callback_rva{};

    friend bool operator==(
        const CallbackTopologyRecord&,
        const CallbackTopologyRecord&) = default;
};

struct CallbackTopology
{
    std::uint64_t signature{};
    std::vector<CallbackTopologyRecord> records;

    friend bool operator==(
        const CallbackTopology&,
        const CallbackTopology&) = default;
};

using ResolveCallbackOwnerFn = Status (*)(
    void* user,
    std::int32_t object_index,
    std::int32_t serial_number,
    std::uint64_t& class_token) noexcept;

class CallbackTopologyProbe
{
public:
    explicit CallbackTopologyProbe(INativeMemory& memory) noexcept;

    Status Capture(
        std::uintptr_t image_base,
        std::size_t image_size,
        std::span<const CallbackCollectionRef> collections,
        ResolveCallbackOwnerFn resolve_owner,
        void* resolve_user,
        CallbackTopology& output) noexcept;

private:
    INativeMemory& memory_;
};
}
