#pragma once

#include "NativeCandidateRegions.hpp"

#include <array>

namespace Horse::Deterministic
{
inline constexpr std::ptrdiff_t secondary_event_stack_fighter_offset = 0x95788;
inline constexpr std::size_t secondary_event_slot_count = 0x18;
inline constexpr std::size_t secondary_event_max_headers = 0x100;

struct SecondaryEventSlotImage
{
    std::array<std::byte, 8> prefix{};
    std::array<std::byte, 8> suffix{};
    friend bool operator==(const SecondaryEventSlotImage&,
        const SecondaryEventSlotImage&) = default;
};

struct SecondaryEventStateImage
{
    std::uint64_t round_generation{};
    std::array<std::array<SecondaryEventSlotImage,
        secondary_event_slot_count>, 2> slots{};
    std::array<std::array<std::byte, 8>, 2> scalars{};
    std::array<std::uint32_t, 2> header_counts{};
    std::array<std::array<std::uint16_t,
        secondary_event_max_headers>, 2> header_cursors{};
    friend bool operator==(const SecondaryEventStateImage&,
        const SecondaryEventStateImage&) = default;
};

class SecondaryEventState final
{
public:
    explicit SecondaryEventState(INativeMemory& memory) noexcept;
    Status Bind(const std::array<std::uintptr_t, 2>& fighters,
        std::uint64_t round_generation) noexcept;
    void Invalidate() noexcept;
    Status Capture(SecondaryEventStateImage& output) noexcept;
    Status RestoreTransactional(const SecondaryEventStateImage& image) noexcept;

    static std::vector<std::byte> CanonicalBytes(
        const SecondaryEventStateImage& image);
    static Status DecodeCanonicalBytes(std::span<const std::byte> bytes,
        SecondaryEventStateImage& output) noexcept;
    static bool Validate(const SecondaryEventStateImage& image) noexcept;

private:
    struct Topology
    {
        std::uintptr_t table_header{};
        std::uintptr_t event_headers{};
        std::uintptr_t event_payloads{};
        std::uint32_t header_count{};
    };

    bool topology_matches() noexcept;
    Status capture_unchecked(SecondaryEventStateImage& output) noexcept;
    bool write_unchecked(const SecondaryEventStateImage& image) noexcept;

    INativeMemory& memory_;
    std::array<std::uintptr_t, 2> fighters_{};
    std::array<Topology, 2> topology_{};
    std::uint64_t round_generation_{};
    bool bound_{};
};
}
