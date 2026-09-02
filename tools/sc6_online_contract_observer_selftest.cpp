#include "deterministic/Sc6OnlineContractObserver.hpp"

#include <cassert>
#include <cstring>

using namespace Horse::Deterministic;

namespace
{
CanonicalHash Identity(std::byte value)
{
    CanonicalHash result{};
    result[0] = value;
    return result;
}
}

int main()
{
    struct WideString { const wchar_t* data; std::int32_t count; std::int32_t capacity; };
    std::array<std::byte, 0x1bd0> sync{};
    constexpr wchar_t fighter0[] = L"2b";
    constexpr wchar_t fighter1[] = L"tir";
    const WideString strings[2]{
        {fighter0, static_cast<std::int32_t>(std::size(fighter0)),
            static_cast<std::int32_t>(std::size(fighter0))},
        {fighter1, static_cast<std::int32_t>(std::size(fighter1)),
            static_cast<std::int32_t>(std::size(fighter1))}};
    for (std::size_t player = 0; player < 2; ++player)
        std::memcpy(sync.data() + 0x2e0 + player * 0xc70 + 0xc58,
            &strings[player], sizeof(WideString));
    const std::uint16_t stage = 23;
    const std::uint32_t seed = 0x12345678u;
    std::memcpy(sync.data() + 0x1bc0, &stage, sizeof(stage));
    sync[0x1bc4] = std::byte{1};
    std::memcpy(sync.data() + 0x1bc8, &seed, sizeof(seed));
    sync[0x1bce] = std::byte{1};
    sync[0x1bcf] = std::byte{1};
    OnlineContentContract observed{};
    assert(Sc6BattleSyncObserver{}.Observe(sync.data(), observed).ok());
    assert(std::string_view(observed.fighter_codes[0].data()) == "2b");
    assert(std::string_view(observed.fighter_codes[1].data()) == "tir");
    assert(std::string_view(observed.stage_code.data()) == "023");
    assert(std::string_view(observed.map_name.data())
        == "/Game/DLC/11/Stage/STG017");
    assert(observed.stage_was_random);
    assert(observed.stage_rng_seed == seed);
    sync[0x1bce] = std::byte{0xff};
    sync[0x1bcf] = std::byte{0x7f};
    assert(Sc6BattleSyncObserver{}.Observe(sync.data(), observed).ok());

    CanonicalHash first_map{};
    CanonicalHash reordered_map{};
    CanonicalHash different_map{};
    constexpr std::array<std::string_view, 2> packages{
        "/Game/Battle/Stage/Map07/Main", "/Game/Battle/Stage/Map07/Lighting"};
    constexpr std::array<std::string_view, 2> reordered{
        packages[1], packages[0]};
    constexpr std::array<std::string_view, 1> different{
        "/Game/Battle/Stage/Map17/Main"};
    assert(HashMapPackageIdentity(packages, first_map).ok());
    assert(HashMapPackageIdentity(reordered, reordered_map).ok());
    assert(HashMapPackageIdentity(different, different_map).ok());
    assert(first_map == reordered_map);
    assert(first_map != different_map);

    Sc6OnlineSessionIdentity session{};
    session.lobby_id = 0x12345678u;
    session.role = 1;
    session.local_player_slot = 1;
    SteamLobbyIdentity lobby{};
    lobby.members = {111u, 222u};
    lobby.local_steam_id = 222u;
    lobby.member_count = 2;
    lobby.casual_player_match = true;
    OnlineContentContract content{};
    std::memcpy(content.fighter_codes[0].data(), "2b", 3);
    std::memcpy(content.fighter_codes[1].data(), "tir", 4);
    std::memcpy(content.stage_code.data(), "017", 4);
    content.map_identity = first_map;
    constexpr char map_name[] = "Astral Chaos: Tide of the Damned";
    std::memcpy(content.map_name.data(), map_name, sizeof(map_name));
    OnlinePeerContract contract{};
    auto status = BuildOnlinePeerContract(session, lobby, content,
        Identity(std::byte{1}), Identity(std::byte{2}), 1, 12, contract);
    assert(status.ok());
    assert(contract.session_id == session.lobby_id);
    assert(contract.lobby_id == session.lobby_id);
    assert(contract.local_player_slot == 1);
    assert(contract.steam_ids[0] == 111u);
    assert(contract.steam_ids[1] == 222u);
    assert(contract.content == content);
    assert(contract.casual_player_match);

    lobby.member_count = 3;
    status = BuildOnlinePeerContract(session, lobby, content,
        Identity(std::byte{1}), Identity(std::byte{2}), 1, 12, contract);
    assert(!status.ok());
    lobby.member_count = 2;
    lobby.local_steam_id = 333u;
    status = BuildOnlinePeerContract(session, lobby, content,
        Identity(std::byte{1}), Identity(std::byte{2}), 1, 12, contract);
    assert(!status.ok());
    lobby.local_steam_id = 222u;
    status = BuildOnlinePeerContract(session, lobby, content,
        {}, Identity(std::byte{2}), 1, 12, contract);
    assert(!status.ok());
    lobby.casual_player_match = false;
    status = BuildOnlinePeerContract(session, lobby, content,
        Identity(std::byte{1}), Identity(std::byte{2}), 1, 12, contract);
    assert(!status.ok());
    return 0;
}
