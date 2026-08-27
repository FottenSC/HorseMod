#pragma once

#include "HgCpuStream.hpp"
#include "NativeCandidateRegions.hpp"

#include <array>

namespace Horse::Deterministic
{
inline constexpr std::uint32_t motion_bank_serializer_version = 4;
inline constexpr std::size_t motion_bank_primary_bytes = 0xC000;
inline constexpr std::size_t motion_bank_secondary_bytes = 0x800;
inline constexpr std::ptrdiff_t motion_tail_fighter_offset = 0x96490;
inline constexpr std::size_t motion_tail_bytes = 0x1000;
inline constexpr std::size_t motion_bank_image_bytes =
    8 + 2 * (3 * (motion_bank_primary_bytes + motion_bank_secondary_bytes)
        + motion_tail_bytes);

class MotionBankSnapshot final
{
public:
    explicit MotionBankSnapshot(INativeMemory& memory) noexcept;

    Status Bind(const std::array<std::uintptr_t, 2>& fighters,
        const LocalReconstructionGenerationContext& context) noexcept;
    void Invalidate() noexcept;
    Status Capture(LocalReconstructionImage& output) noexcept;
    Status RestoreTransactional(const LocalReconstructionImage& image) noexcept;
    [[nodiscard]] std::size_t ScratchCapacityBytes() const noexcept
    {
        return undo_scratch_.bytes.capacity()
            + observed_scratch_.bytes.capacity();
    }

    [[nodiscard]] static bool ValidateLocalImage(
        const LocalReconstructionImage& image) noexcept;
    [[nodiscard]] static bool ValidateLocalImageMetadata(
        const LocalReconstructionImage& image) noexcept;

private:
    struct BankTopology
    {
        std::uintptr_t bank{};
        std::uintptr_t vtable{};
        std::array<std::uintptr_t, 3> buffers{};
        std::size_t bytes{};
    };

    static std::uint64_t Checksum(
        const LocalReconstructionImage& image) noexcept;
    bool topology_matches() noexcept;
    Status capture_unchecked(LocalReconstructionImage& output) noexcept;
    bool write_unchecked(const LocalReconstructionImage& image) noexcept;

    INativeMemory& memory_;
    std::array<std::uintptr_t, 2> fighters_{};
    std::array<std::int32_t, 2> matrix_counts_{};
    std::array<std::array<BankTopology, 2>, 2> topology_{};
    LocalReconstructionGenerationContext context_{};
    LocalReconstructionImage undo_scratch_{};
    LocalReconstructionImage observed_scratch_{};
    bool bound_{};
};
}
