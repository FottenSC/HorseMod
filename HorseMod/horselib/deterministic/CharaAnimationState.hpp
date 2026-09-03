#pragma once

#include "NativeCandidateRegions.hpp"

#include <array>

namespace Horse::Deterministic
{
inline constexpr std::ptrdiff_t chara_anim_slot_controller_offset = 0x95EC0;
inline constexpr std::ptrdiff_t chara_anim_clip_player_offset = 0x95ED0;
inline constexpr std::ptrdiff_t chara_anim_runtime_offset = 0x2B270;
inline constexpr std::ptrdiff_t pose_event_cue_owner_offset = 0x95720;
inline constexpr std::size_t chara_anim_maximum_triggers = 64;
inline constexpr std::size_t chara_anim_maximum_packed_sections = 4096;

enum class CharaAnimationTopologyIssue : std::uint8_t
{
    None,
    PackedData,
    CueOwner,
    Scheduler,
    TriggerList,
    TriggerNode,
    ClipSection,
    RuntimeSection,
    ClipScalars,
    RuntimeScalars,
    CueScalars,
    SchedulerScalars,
    TriggerScalars,
};

constexpr std::string_view chara_animation_topology_issue_name(
    CharaAnimationTopologyIssue issue) noexcept
{
    switch (issue)
    {
    case CharaAnimationTopologyIssue::None: return "none";
    case CharaAnimationTopologyIssue::PackedData: return "packed_data";
    case CharaAnimationTopologyIssue::CueOwner: return "cue_owner";
    case CharaAnimationTopologyIssue::Scheduler: return "scheduler";
    case CharaAnimationTopologyIssue::TriggerList: return "trigger_list";
    case CharaAnimationTopologyIssue::TriggerNode: return "trigger_node";
    case CharaAnimationTopologyIssue::ClipSection: return "clip_section";
    case CharaAnimationTopologyIssue::RuntimeSection: return "runtime_section";
    case CharaAnimationTopologyIssue::ClipScalars: return "clip_scalars";
    case CharaAnimationTopologyIssue::RuntimeScalars: return "runtime_scalars";
    case CharaAnimationTopologyIssue::CueScalars: return "cue_scalars";
    case CharaAnimationTopologyIssue::SchedulerScalars: return "scheduler_scalars";
    case CharaAnimationTopologyIssue::TriggerScalars: return "trigger_scalars";
    }
    return "unknown";
}

struct PackedSectionIdentity
{
    std::uint32_t index{};
    bool present{};
    friend bool operator==(const PackedSectionIdentity&,
        const PackedSectionIdentity&) = default;
};

struct CharaAnimationPlayerImage
{
    bool clip_owner_bound{};
    PackedSectionIdentity clip_section{};
    std::array<std::byte, 0x20> clip_scalars{};
    PackedSectionIdentity runtime_section{};
    std::array<std::byte, 8> runtime_scalars{};
    std::array<std::byte, 0x20> cue_owner_scalars{};
    bool scheduler_chara_bound{};
    std::array<std::byte, 0x5C> scheduler_scalars{};
    std::uint32_t trigger_count{};
    std::array<std::array<std::byte, 0x18>,
        chara_anim_maximum_triggers> trigger_scalars{};
    friend bool operator==(const CharaAnimationPlayerImage&,
        const CharaAnimationPlayerImage&) = default;
};

struct CharaAnimationStateImage
{
    std::uint64_t round_generation{};
    std::array<CharaAnimationPlayerImage, 2> players{};
    friend bool operator==(const CharaAnimationStateImage&,
        const CharaAnimationStateImage&) = default;
};

class CharaAnimationState final
{
public:
    explicit CharaAnimationState(INativeMemory& memory) noexcept;
    Status Bind(const std::array<std::uintptr_t, 2>& fighters,
        std::uint64_t round_generation) noexcept;
    void Invalidate() noexcept;
    Status Capture(CharaAnimationStateImage& output) noexcept;
    Status RestoreTransactional(const CharaAnimationStateImage& image) noexcept;
    [[nodiscard]] CharaAnimationTopologyIssue topology_issue() const noexcept
    {
        return topology_issue_;
    }
    [[nodiscard]] std::uintptr_t topology_observed() const noexcept
    {
        return topology_observed_;
    }
    [[nodiscard]] const std::array<std::uintptr_t, 2>& fighters() const noexcept
    {
        return fighters_;
    }

    static std::vector<std::byte> CanonicalBytes(
        const CharaAnimationStateImage& image);
    static void CanonicalBytes(const CharaAnimationStateImage& image,
        std::vector<std::byte>& output);
    // Peer identity keeps the complete active scheduler transaction. An
    // inactive scheduler's payload, trigger list, and pChara binding are
    // stale residue: the native tick returns on nActive == 0, the intro
    // controller reads only nActive, and configuration overwrites/clears the
    // state before rearming it. Local checkpoint bytes remain unchanged.
    static void PeerCanonicalBytes(const CharaAnimationStateImage& image,
        std::vector<std::byte>& output);
    static Status DecodeCanonicalBytes(std::span<const std::byte> bytes,
        CharaAnimationStateImage& output) noexcept;
    static bool Validate(const CharaAnimationStateImage& image) noexcept;

private:
    struct TriggerTopology
    {
        std::uintptr_t node{};
        std::uintptr_t next{};
        std::uintptr_t previous{};
        std::uintptr_t object{};
        std::uintptr_t control{};
        std::uintptr_t object_vtable{};
    };

    struct PlayerTopology
    {
        std::uintptr_t packed_data{};
        std::uintptr_t cue_owner_vtable{};
        std::uintptr_t enst_data{};
        std::uintptr_t scheduler{};
        std::uintptr_t scheduler_vtable{};
        std::uintptr_t scheduler_chara{};
        std::uintptr_t list_head{};
        std::uintptr_t list_head_next{};
        std::uintptr_t list_head_previous{};
        std::uint32_t trigger_count{};
        std::array<TriggerTopology, chara_anim_maximum_triggers> triggers{};
    };

    bool capture_topology(std::size_t player, PlayerTopology& output) noexcept;
    bool topology_matches() noexcept;
    bool identify_section(std::size_t player, std::uintptr_t pointer,
        PackedSectionIdentity& output) noexcept;
    bool resolve_section(std::size_t player,
        PackedSectionIdentity identity, std::uintptr_t& output) noexcept;
    Status capture_unchecked(CharaAnimationStateImage& output) noexcept;
    bool write_unchecked(const CharaAnimationStateImage& image) noexcept;

    INativeMemory& memory_;
    std::array<std::uintptr_t, 2> fighters_{};
    std::array<PlayerTopology, 2> topology_{};
    std::uint64_t round_generation_{};
    bool bound_{};
    CharaAnimationTopologyIssue topology_issue_{};
    std::uintptr_t topology_observed_{};
};
}
