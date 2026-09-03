#pragma once

#include "NativeCandidateRegions.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace Horse::Deterministic
{
struct MoveDispatchActionModeState
{
    std::uint8_t action_mode{};
    std::int32_t frame_counter{};
    std::uint8_t pending_window_gate{};

    friend bool operator==(const MoveDispatchActionModeState&,
        const MoveDispatchActionModeState&) = default;
};

struct MoveDispatchPendingWindow
{
    std::uint32_t owner_slot_tag{};
    std::uint32_t payload_flags{};
    std::uint64_t payload_xy{};
    std::uint64_t payload_tail{};
    std::int32_t start_frame{};
    std::int32_t end_frame{};

    friend bool operator==(const MoveDispatchPendingWindow&,
        const MoveDispatchPendingWindow&) = default;
};

static_assert(sizeof(MoveDispatchPendingWindow) == 0x20);
static_assert(offsetof(MoveDispatchPendingWindow, start_frame) == 0x18);
static_assert(offsetof(MoveDispatchPendingWindow, end_frame) == 0x1C);

struct MoveDispatchPendingState
{
    std::vector<MoveDispatchPendingWindow> windows;

    friend bool operator==(const MoveDispatchPendingState&,
        const MoveDispatchPendingState&) = default;
};

struct MoveDispatchSubElementState
{
    std::int32_t tick_count{};
    std::uint8_t complete{};

    friend bool operator==(const MoveDispatchSubElementState&,
        const MoveDispatchSubElementState&) = default;
};

using MoveDispatchPhaseState =
    std::variant<MoveDispatchActionModeState, MoveDispatchPendingState>;

struct MoveDispatchImage
{
    std::uint64_t generation{};
    std::int32_t frame_slot_index{};
    std::int32_t sub_frame_index{};
    MoveDispatchPhaseState phase{};
    std::uint32_t saved_input_and_gates{};
    std::int32_t completion_delay{};
    std::vector<MoveDispatchSubElementState> sub_elements;
    // Capacity owner for the inactive variant. It prevents action/pending
    // phase transitions from destroying and reallocating the bounded window
    // storage; it has no serialized or canonical meaning.
    std::vector<MoveDispatchPendingWindow> pending_windows_scratch;

    friend bool operator==(const MoveDispatchImage& a,
        const MoveDispatchImage& b) noexcept
    {
        return a.generation == b.generation
            && a.frame_slot_index == b.frame_slot_index
            && a.sub_frame_index == b.sub_frame_index
            && a.phase == b.phase
            && a.saved_input_and_gates == b.saved_input_and_gates
            && a.completion_delay == b.completion_delay
            && a.sub_elements == b.sub_elements;
    }
};

class MoveDispatchState
{
public:
    explicit MoveDispatchState(INativeMemory& memory) noexcept;

    Status Bind(std::uintptr_t object, std::uint64_t generation) noexcept;
    void Invalidate() noexcept;
    void ReleaseScratchStorage() noexcept;
    Status Capture(MoveDispatchImage& output) noexcept;
    Status PreflightRestore(const MoveDispatchImage& image) noexcept;
    Status RestoreTransactional(const MoveDispatchImage& image) noexcept;
    [[nodiscard]] std::size_t ScratchCapacityBytes() const noexcept;

    [[nodiscard]] static std::vector<std::byte> CanonicalBytes(
        const MoveDispatchImage& image);
    static void CanonicalBytes(
        const MoveDispatchImage& image, std::vector<std::byte>& output);
    [[nodiscard]] static Status DecodeCanonicalBytes(
        std::span<const std::byte> bytes, MoveDispatchImage& output) noexcept;
    [[nodiscard]] static Status PrepareImageStorage(
        MoveDispatchImage& output) noexcept;

private:
    struct Identity
    {
        std::uintptr_t frame_slot_table{};
        std::uintptr_t sub_elements{};
        std::int32_t sub_element_count{};
        std::int32_t sub_element_capacity{};
        std::uintptr_t pending_windows{};
        std::int32_t pending_capacity{};
        bool pending_phase{};
    };

    bool capture_identity(Identity& output) noexcept;
    bool identity_matches(const Identity& expected) noexcept;
    bool capture_unchecked(MoveDispatchImage& output) noexcept;
    bool write_image(const MoveDispatchImage& image, bool reverse) noexcept;

    static constexpr std::int32_t maximum_pending_windows = 16;
    static constexpr std::int32_t maximum_sub_elements = 1024;

    INativeMemory& memory_;
    std::uintptr_t object_{};
    std::uint64_t generation_{};
    Identity identity_{};
    MoveDispatchImage restore_undo_scratch_{};
    MoveDispatchImage restore_verification_scratch_{};
    bool bound_{};
};
}
