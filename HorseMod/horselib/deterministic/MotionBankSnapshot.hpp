#pragma once

#include "HgCpuStream.hpp"
#include "NativeCandidateRegions.hpp"

#include <array>

namespace Horse::Deterministic
{
inline constexpr std::uint32_t motion_bank_serializer_version = 7;
inline constexpr std::size_t motion_bank_primary_bytes = 0xC000;
inline constexpr std::size_t motion_bank_secondary_bytes = 0x800;
inline constexpr std::ptrdiff_t motion_tail_fighter_offset = 0x96490;
inline constexpr std::size_t motion_tail_bytes = 0x1000;
inline constexpr std::size_t motion_bank_base_image_bytes =
    18 + 2 * (3 * (motion_bank_primary_bytes + motion_bank_secondary_bytes)
        + motion_tail_bytes);

inline constexpr std::size_t motion_bank_image_bytes = motion_bank_base_image_bytes + 0xA0000;

class CharaAnimationState;
class MotionBankSnapshot final
{
public:
    explicit MotionBankSnapshot(INativeMemory& memory) noexcept;

    Status Bind(const std::array<std::uintptr_t, 2>& fighters,
        const LocalReconstructionGenerationContext& context,
        CharaAnimationState* animation_owner = nullptr) noexcept;
    void Invalidate() noexcept;
    void ReleaseScratchStorage() noexcept;
    [[nodiscard]] static std::size_t BindAllocationEnvelopeBytes() noexcept;
    Status Capture(LocalReconstructionImage& output) noexcept;
    Status RestoreTransactional(const LocalReconstructionImage& image) noexcept;
    [[nodiscard]] std::size_t ScratchCapacityBytes() const noexcept
    {
        return undo_scratch_.bytes.capacity()
            + observed_scratch_.bytes.capacity()
            + regions_.capacity()*sizeof(ValueRegion) + guards_.capacity()*sizeof(BindingGuard)
            + colliders_.capacity()*sizeof(ColliderBinding) + collision_nodes_.capacity()*sizeof(std::uintptr_t);
    }

    struct BindingDiagnostic { unsigned line{}; std::uintptr_t address{}; std::uint64_t observed{}, expected{}, fighter_offset{}; const char* kind{}; };
    BindingDiagnostic binding_diagnostic() const noexcept { return binding_diagnostic_; }
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
    struct ValueRegion { std::uintptr_t address{}; std::size_t bytes{}; };
    struct BindingGuard { std::uintptr_t address{}; std::uint64_t value{}; std::size_t bytes{}; const char* kind{}; };
    std::vector<ValueRegion> regions_;
    std::vector<BindingGuard> guards_;
    // 140336F80 can propagate an existing collider to another spring node.
    // The dictionary retains initial owners; mutable links serialize indices,
    // never unchecked pointers. Unknown new owners still reject admission.
    struct ColliderBinding { std::uintptr_t address{}, vtable{}; std::uint32_t runtime_index{}; };
    std::vector<ColliderBinding> colliders_;
    std::vector<std::uintptr_t> collision_nodes_;
    std::size_t image_bytes_{motion_bank_base_image_bytes};
    std::uint64_t binding_fingerprint_{};
    BindingDiagnostic binding_diagnostic_{};
    bool bind_skeleton_values() noexcept;
    bool identify_secondary_source(std::uintptr_t pointer, std::uint8_t& slot) const noexcept;
    std::uintptr_t resolve_secondary_source(std::uint8_t slot) const noexcept;
    bool topology_matches(bool check_collision_values = true) noexcept;
    bool collision_values_valid(const LocalReconstructionImage* image = nullptr) noexcept;
    Status capture_unchecked(LocalReconstructionImage& output) noexcept;
    bool write_unchecked(const LocalReconstructionImage& image, bool recovering = false) noexcept;

    INativeMemory& memory_;
    CharaAnimationState* animation_owner_{};
    std::uint64_t animation_binding_serial_{};
    std::array<std::uintptr_t, 2> fighters_{};
    std::array<std::int32_t, 2> matrix_counts_{};
    std::array<std::array<BankTopology, 2>, 2> topology_{};
    LocalReconstructionGenerationContext context_{};
    LocalReconstructionImage undo_scratch_{};
    LocalReconstructionImage observed_scratch_{};
    bool bound_{};
};
}
