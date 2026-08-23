#pragma once

#include "Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Horse::Deterministic
{
class INativeMemory
{
public:
    virtual ~INativeMemory() = default;
    virtual bool Read(
        std::uintptr_t address,
        std::span<std::byte> destination) noexcept = 0;
    virtual bool Write(
        std::uintptr_t address,
        std::span<const std::byte> source) noexcept = 0;
};

struct NativeCandidateAddresses
{
    std::uintptr_t image_base{};
    std::uintptr_t move_dispatch{};
    std::uintptr_t pump_state{};
    std::uintptr_t scheduler_base{};
    std::uintptr_t move_command_base{};
    std::uintptr_t slot_param_base{};
    std::uint64_t session_generation{};
    std::uint64_t round_generation{};
};

struct NativePumpImage
{
    std::array<std::byte, 0x1C> lane_a{};
    std::array<std::byte, 0x1C> lane_b{};
    std::array<std::byte, 0x18> controls{};

    friend bool operator==(const NativePumpImage&, const NativePumpImage&) = default;
};

struct NativeSubVmImage
{
    std::uint32_t vtable_rva{};
    std::uint8_t extent{};
    std::array<std::byte, 4> input_command{};
    std::array<std::byte, 0x3C> common{};
    std::array<std::byte, 0x14> derived{};

    friend bool operator==(const NativeSubVmImage&, const NativeSubVmImage&) = default;
};

inline constexpr std::size_t native_move_command_semantic_bytes = 0x2F2C;

struct NativeCandidateImage
{
    std::uint64_t session_generation{};
    std::uint64_t round_generation{};
    std::array<std::uint64_t, 2> move_dispatch_masks{};
    NativePumpImage pump{};
    std::array<NativeSubVmImage, 2> sub_vms{};
    std::array<std::array<std::byte, native_move_command_semantic_bytes>, 2>
        move_commands{};
    std::array<std::array<std::byte, 0x28>, 2> slot_params{};

    friend bool operator==(const NativeCandidateImage&, const NativeCandidateImage&) = default;
};

class NativeCandidateRegions
{
public:
    explicit NativeCandidateRegions(INativeMemory& memory) noexcept;

    Status Bind(const NativeCandidateAddresses& addresses) noexcept;
    void Invalidate() noexcept;
    [[nodiscard]] bool IsBound() const noexcept { return bound_; }

    Status PreflightCapture() noexcept;
    Status Capture(NativeCandidateImage& output) noexcept;
    Status PreflightRestore(const NativeCandidateImage& image) noexcept;
    Status RestoreTransactional(const NativeCandidateImage& image) noexcept;

    [[nodiscard]] static std::vector<std::byte> CanonicalBytes(
        const NativeCandidateImage& image);

private:
    struct SubVmIdentity
    {
        std::uintptr_t scheduler{};
        std::uintptr_t object{};
        std::uintptr_t vtable{};
        std::uintptr_t fighter{};
        std::uintptr_t opponent{};
        std::uintptr_t owner_scheduler{};
        std::uint8_t extent{};
    };

    struct BoundIdentities
    {
        std::uintptr_t event_mask_owner{};
        std::array<std::uintptr_t, 6> pump{};
        std::array<SubVmIdentity, 2> sub_vms{};
        std::array<std::array<std::uintptr_t, 17>, 2> move_commands{};
    };

    bool read_bytes(std::uintptr_t address, std::span<std::byte> out) noexcept;
    bool write_bytes(
        std::uintptr_t address,
        std::span<const std::byte> bytes) noexcept;
    bool capture_identities(BoundIdentities& output) noexcept;
    bool identities_match() noexcept;
    bool image_matches_binding(const NativeCandidateImage& image) const noexcept;
    bool capture_unchecked(NativeCandidateImage& output) noexcept;
    bool write_forward(const NativeCandidateImage& image) noexcept;
    bool write_reverse(const NativeCandidateImage& image) noexcept;

    INativeMemory& memory_;
    NativeCandidateAddresses addresses_{};
    BoundIdentities identities_{};
    bool bound_{};
};
}
