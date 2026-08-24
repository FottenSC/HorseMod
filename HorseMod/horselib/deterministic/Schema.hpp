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
inline constexpr std::size_t replay_round_image_size = 0xc0;
inline constexpr std::uint32_t maximum_replay_round_images = 64;

namespace Sc6ReplayLayout
{
inline constexpr std::uintptr_t replay_enabled = 0x398;
inline constexpr std::uintptr_t round_images = 0x3a8;
inline constexpr std::uintptr_t round_count = 0x3b0;
inline constexpr std::uintptr_t round_capacity = 0x3b4;
inline constexpr std::uintptr_t manager_round_image = 0x1360;
inline constexpr std::uintptr_t manager_move_state = 0x1463;
inline constexpr std::uintptr_t manager_pending_dispatch = 0x1464;
inline constexpr std::uintptr_t manager_round_image_applied = 0x1465;
inline constexpr std::uintptr_t manager_status = 0x1480;
inline constexpr std::uintptr_t set_move_state_rva = 0x3f8370;
inline constexpr std::array<std::byte, 7> set_move_state_signature{
    std::byte{0x88}, std::byte{0x91}, std::byte{0x63}, std::byte{0x14},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xc3}};
}

namespace Sc6FrameLayout
{
inline constexpr std::uintptr_t frame_counter_rva = 0x470d0c4;
inline constexpr std::uintptr_t landing_fencepost_rva = 0x3fca60;
inline constexpr std::ptrdiff_t manager_repeat_pending = 0x1462;
inline constexpr std::array<std::byte, 16> landing_fencepost_signature{
    std::byte{0x41}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xec}, std::byte{0x50}, std::byte{0x48}, std::byte{0x83},
    std::byte{0xb9}, std::byte{0x28}, std::byte{0x05}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x4c}, std::byte{0x8b}};
}

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
