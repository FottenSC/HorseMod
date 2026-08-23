#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Horse::Deterministic::Schema
{
inline constexpr std::uint32_t protocol_version = 1;
inline constexpr std::uint32_t snapshot_schema_version = 1;
inline constexpr std::size_t maximum_transport_payload = 1200;
inline constexpr std::uint64_t checkpoint_interval = 30;

enum class RegionClass : std::uint8_t
{
    CanonicalGameplay,
    Derived,
    ClientLocalDiagnostic,
    PersistentPresentation,
    EphemeralPresentation,
};

struct NativeRegionDescriptor
{
    std::string_view name;
    std::uintptr_t address;
    std::size_t size;
    RegionClass classification;
};

// Admission is deliberately fail-closed. Regions are added only after their complete
// native read/write and lifetime surface is recorded in the Ghidra-backed contract.
inline constexpr std::array<NativeRegionDescriptor, 0> production_regions{};

constexpr std::string_view region_class_name(RegionClass value) noexcept
{
    switch (value)
    {
    case RegionClass::CanonicalGameplay: return "canonical_gameplay";
    case RegionClass::Derived: return "derived";
    case RegionClass::ClientLocalDiagnostic: return "client_local_diagnostic";
    case RegionClass::PersistentPresentation: return "persistent_presentation";
    case RegionClass::EphemeralPresentation: return "ephemeral_presentation";
    }
    return "unknown";
}
}
