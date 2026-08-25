#pragma once

#include "NativeCandidateRegions.hpp"

#include <array>

namespace Horse::Deterministic
{
inline constexpr std::size_t maximum_battle_audio_handlers = 4;

struct BattleAudioSelectorImage
{
    std::uint64_t session_generation{};
    std::uint64_t round_generation{};
    std::array<std::int32_t, maximum_battle_audio_handlers> alternations{};
    std::uint8_t observed_count{};

    friend bool operator==(
        const BattleAudioSelectorImage&,
        const BattleAudioSelectorImage&) = default;
};

using ResolveBattleAudioHandlerFn = std::uintptr_t (*)(
    void* user, std::size_t index) noexcept;
using BattleAudioHandlerOverflowFn = bool (*)(void* user) noexcept;

struct BattleAudioSelectorBinding
{
    std::uintptr_t image_base{};
    std::size_t image_size{};
    LocalReconstructionGenerationContext context{};
    ResolveBattleAudioHandlerFn resolve_handler{};
    BattleAudioHandlerOverflowFn handler_overflowed{};
    void* resolve_user{};
};

// Local-only typed supplement for ALuxBattleSoundEventHandler +0x3E0. The
// handler pointer is retained only as a same-process validation identity and
// never enters the image, canonical hashes, or peer messages.
class BattleAudioSelectorState final
{
public:
    explicit BattleAudioSelectorState(INativeMemory& memory) noexcept;

    Status Bind(const BattleAudioSelectorBinding& binding) noexcept;
    void Reset() noexcept;
    Status Capture(BattleAudioSelectorImage& output) noexcept;
    Status PreflightRestore(const BattleAudioSelectorImage& image) noexcept;
    Status RestoreTransactional(const BattleAudioSelectorImage& image) noexcept;

private:
    Status resolve_and_validate(std::size_t index, std::uintptr_t& handler,
        std::int32_t& alternation, bool allow_unobserved) noexcept;
    static constexpr std::uintptr_t handler_vtable_rva = 0x326A6C8;
    static constexpr std::uintptr_t alternation_offset = 0x3E0;

    INativeMemory& memory_;
    BattleAudioSelectorBinding binding_{};
    std::array<std::uintptr_t, maximum_battle_audio_handlers> bound_handlers_{};
    bool bound_{};
};
}
