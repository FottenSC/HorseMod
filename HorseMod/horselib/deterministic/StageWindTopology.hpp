#pragma once

#include "NativeCandidateRegions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
enum class StageWindNodeKind : std::uint8_t
{
    Parallel,
    RingOut,
    RingIn,
    ShockWave,
};

struct StageWindStateRange
{
    std::size_t offset{};
    std::size_t size{};
};

struct StageWindNodeLayout
{
    StageWindNodeKind kind{};
    std::uint32_t vtable_rva{};
    std::size_t allocation_size{};
    std::span<const StageWindStateRange> class_ranges{};
    std::span<const StageWindStateRange> derived_ranges{};
};

[[nodiscard]] std::span<const StageWindStateRange> StageWindCommonRanges() noexcept;
[[nodiscard]] const StageWindNodeLayout* FindStageWindNodeLayout(
    StageWindNodeKind kind) noexcept;
[[nodiscard]] const StageWindNodeLayout* FindStageWindNodeLayoutByVtable(
    std::uint32_t vtable_rva) noexcept;
[[nodiscard]] std::size_t StageWindSemanticStateSize(
    const StageWindNodeLayout& layout) noexcept;
[[nodiscard]] std::size_t StageWindDerivedStateSize(
    const StageWindNodeLayout& layout) noexcept;

struct StageWindNodeImage
{
    StageWindNodeKind kind{};
    std::vector<std::byte> semantic_state;
    std::vector<std::byte> derived_state;

    friend bool operator==(const StageWindNodeImage&, const StageWindNodeImage&) = default;
};

struct StageWindTopologyImage
{
    std::uint64_t generation{};
    std::array<std::byte, 12> root_clock{};
    std::array<std::uint32_t, 16> pending_callback_rvas{};
    std::array<std::byte, 16> schedule_state{};
    std::array<std::byte, 16> schedule_params{};
    std::array<std::byte, 48> output_force{};
    std::vector<StageWindNodeImage> nodes;

    friend bool operator==(const StageWindTopologyImage&, const StageWindTopologyImage&) = default;
};

[[nodiscard]] bool ValidateStageWindTopologyImage(
    const StageWindTopologyImage& image) noexcept;

struct StageWindTopologyAddresses
{
    std::uintptr_t image_base{};
    std::size_t image_size{};
    std::uintptr_t root_pointer{};
    std::uint64_t generation{};
};

// Read-only admission probe. It deliberately serializes no native pointer and
// does not claim that wind allocations can be reconstructed or restored.
class StageWindTopologyProbe
{
public:
    explicit StageWindTopologyProbe(INativeMemory& memory) noexcept;

    Status Bind(const StageWindTopologyAddresses& addresses) noexcept;
    void Invalidate() noexcept;
    Status Capture(StageWindTopologyImage& output) noexcept;

    [[nodiscard]] static std::vector<std::byte> CanonicalBytes(
        const StageWindTopologyImage& image);

private:
    INativeMemory& memory_;
    StageWindTopologyAddresses addresses_{};
    std::uintptr_t root_{};
    bool bound_{};
};
}
