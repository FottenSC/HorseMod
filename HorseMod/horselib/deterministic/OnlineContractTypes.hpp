#pragma once

#include "Interfaces.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic
{
struct OnlineContentContract
{
    static constexpr std::size_t maximum_fighter_code_bytes = 16;
    static constexpr std::size_t maximum_stage_code_bytes = 8;
    static constexpr std::size_t maximum_map_name_bytes = 64;

    // Exact authored codes exchanged by LuxOnlineBattleSync. These remain
    // available before ALuxBattleChara and the battle UWorld exist.
    std::array<std::array<char, maximum_fighter_code_bytes>, 2>
        fighter_codes{};
    std::array<char, maximum_stage_code_bytes> stage_code{};
    std::uint32_t stage_rng_seed{};
    bool stage_was_random{};
    CanonicalHash map_identity{};
    // Stable authored UWorld/package name, not a replay override or an
    // implementation-only numeric map index. Fixed storage keeps the game
    // thread and wire contract allocation-free.
    std::array<char, maximum_map_name_bytes> map_name{};

    friend constexpr bool operator==(
        const OnlineContentContract&,
        const OnlineContentContract&) = default;
};

struct OnlinePeerContract
{
    std::uint64_t session_id{};
    std::uint64_t lobby_id{};
    std::array<std::uint64_t, 2> steam_ids{};
    std::uint8_t local_player_slot{};
    std::uint8_t lobby_member_count{};
    bool casual_player_match{};
    CanonicalHash executable_id{};
    CanonicalHash build_id{};
    OnlineContentContract content{};
    std::uint32_t input_delay{};
    std::uint32_t rollback_window{};
};
}
