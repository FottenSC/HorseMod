#pragma once

#include "NativeCandidateRegions.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

struct StageWindNodeImage
{
    StageWindNodeKind kind{};
    std::vector<std::byte> semantic_state;

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
