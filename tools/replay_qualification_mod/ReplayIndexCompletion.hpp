#pragma once
#include <cstdint>
#include <array>
#include <span>

namespace ReplayQualification {
enum class IndexSeek { Idle, Arming, Seeking, Executing, Holding, Tails, Releasing, Resuming, Done, EndHold, AwaitingUiRequest, ArmingNext };

// Bounded qualification sequences. These are requests, never expected state.
inline constexpr std::array<std::uint32_t,5> early_index_targets{208,2510,2511,2512,208};
inline constexpr std::array<std::uint32_t,4> late_index_targets{5574,6417,11000,2517};
constexpr std::span<const std::uint32_t> IndexSequence(unsigned sequence) noexcept {
    if(sequence==1)return early_index_targets;
    if(sequence==2)return late_index_targets;
    return {};
}
inline constexpr std::array<std::uint32_t,8> index_observation_targets{208,2510,2511,2512,5574,6417,11000,2517};
constexpr bool ObserveIndexSequenceTick(std::uint32_t tick) noexcept {
    for(auto target:index_observation_targets)
        if(tick>=target-16 && tick<=target+121)return true;
    return false;
}

// Early rolling campaigns need uninterrupted observation from A210 through
// final T plus120, in both candidate and independent native control.
constexpr bool ObserveEarlyCombatTarget(std::uint32_t tick,std::uint32_t target) noexcept {
    return target>=210 && target<=816 && tick>=210 && tick<=target+120;
}

// Arming precedes the target's native round transition. Combat admission is
// checked at the held target, not against its possibly pre-combat predecessor.
constexpr bool CanArmIndexCheckpoint(std::uint64_t frame,std::uint64_t target,bool source_active) noexcept
{
    return target>0 && frame==target-1 && source_active;
}
constexpr bool IsCombatIndexCheckpoint(std::uint64_t frame,std::uint64_t target,
    bool source_active,unsigned round_state,unsigned world_mode) noexcept
{
    return frame==target && source_active && round_state==2 && world_mode==2;
}

// Native endpoint completion and seek execution have different deadlines.
// The runner's process deadline still bounds every phase of the experiment.
constexpr bool AwaitingIndexEndpoint(IndexSeek phase) noexcept
{
    return phase==IndexSeek::Idle || phase==IndexSeek::Arming || phase==IndexSeek::Done || phase==IndexSeek::EndHold;
}

// The late11000 undo experiment covers a native round-ending boundary, not
// the execution gate's active-combat pause category. It must never admit an
// intro, inactive source, changed move path, or an unqualified request.
constexpr bool AllowsLateRecoveryHold(bool qualified_request,bool source_active,
    unsigned manager,unsigned move,unsigned round_state,unsigned world,unsigned cursor) noexcept
{
    return qualified_request && source_active && manager==2 && move==0
        && round_state==3 && world==3 && cursor>0;
}
}
