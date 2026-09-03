#pragma once

#include "OnlineContractTypes.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace Horse::Deterministic
{
enum class Sc6OnlineSessionObservationStage : std::uint8_t
{
    None,
    Acquired,
    ExpectedVtable,
    RoleAndState,
    OnlineSession,
    NamedSession,
    SessionInfo,
    LobbyIdentity,
};

struct Sc6OnlineSessionIdentity
{
    std::uint64_t lobby_id{};
    std::uint64_t session_name{};
    std::uintptr_t session_interface{};
    std::uintptr_t active_connect{};
    std::uintptr_t online_session{};
    std::uintptr_t named_session{};
    std::uintptr_t session_info{};
    std::int8_t role{-1};
    std::uint8_t virtual_session_state{};
    std::uint8_t local_player_slot{};
    Sc6OnlineSessionObservationStage observation_stage{
        Sc6OnlineSessionObservationStage::None};
};

// State 1 waits for the named session connection; opcode 0x0B writes state 3
// only after resolving the peer transport entry and expected session route;
// state 4 begins match transport. These are the fresh pre-ownership windows.
// State 5 is deferred, state 6 can recycle into state 4, and state 9 is failure.
[[nodiscard]] constexpr bool IsSc6PreownershipSessionState(
    std::uint8_t state) noexcept
{
    return state == 1 || state == 3 || state == 4;
}

[[nodiscard]] constexpr Sc6OnlineSessionIdentity
ValueOnlySc6OnlineSessionIdentity(
    const Sc6OnlineSessionIdentity& observed) noexcept
{
    auto value = observed;
    value.session_interface = 0;
    value.active_connect = 0;
    value.online_session = 0;
    value.named_session = 0;
    value.session_info = 0;
    return value;
}

struct Sc6BattleSyncIdentity
{
    enum class ObservationStage : std::uint8_t
    {
        None,
        Acquired,
        CompletionFlags,
        FighterCodes,
        StageFields,
        StageCatalog,
        SelectionIdentity,
    };

    OnlineContentContract content{};
    std::uintptr_t battle_sync_object{};
    std::uint16_t native_stage_code{};
    std::uint8_t native_stage_random{};
    std::array<bool, 2> fighter_code_valid{};
    bool characters_received{};
    bool stage_received{};
    ObservationStage observation_stage{ObservationStage::None};
};

struct SteamLobbyIdentity
{
    std::array<std::uint64_t, 2> members{};
    std::uint64_t local_steam_id{};
    std::int32_t observed_member_count{-1};
    std::uint32_t observation_mask{};
    std::uint32_t search_discriminator{};
    std::uint8_t member_count{};
    bool casual_player_match{};
};

class Sc6BattleSyncObserver
{
public:
    // Reads the verified match-data layout embedded in
    // ULuxOnlineBattleSync. The supplied pointer is the UObject base.
    Status Observe(
        const void* battle_sync_object,
        OnlineContentContract& output) const noexcept;
    Status ObserveDetailed(
        const void* battle_sync_object,
        Sc6BattleSyncIdentity& output) const noexcept;
};

// Copies the completed selection once. A valid latch is value-only and this
// helper deliberately returns without dereferencing a later raw pointer.
Status LatchSc6BattleSyncContent(
    const Sc6BattleSyncObserver& observer,
    const void* battle_sync_object,
    OnlineContentContract& latched_content,
    bool& latched_valid,
    Sc6BattleSyncIdentity& observation) noexcept;

class Sc6OnlineSessionObserver
{
public:
    Status Initialize(std::uintptr_t image_base) noexcept;
    // Reads the current structurally valid player-session identity without
    // imposing the narrower fresh pre-ownership state gate. Native pointers
    // are diagnostic only and must never be retained across ticks.
    Status ObserveCurrent(Sc6OnlineSessionIdentity& output) const noexcept;
    Status Observe(Sc6OnlineSessionIdentity& output) const noexcept;

private:
    std::uintptr_t image_base_{};
};

class SteamLobbyObserver
{
public:
    Status Observe(
        std::uint64_t lobby_id,
        SteamLobbyIdentity& output) noexcept;

private:
    template <typename Function>
    static Function Resolve(HMODULE module, const char* name) noexcept
    {
        return reinterpret_cast<Function>(GetProcAddress(module, name));
    }

    using SteamClientFn = void* (__cdecl*)();
    using GetHandleFn = int (__cdecl*)();
    using GetInterfaceFn = void* (__cdecl*)(void*, int, int, const char*);
    using GetSteamIdFn = std::uint64_t (__cdecl*)(void*);
    using GetNumLobbyMembersFn = int (__cdecl*)(void*, std::uint64_t);
    using GetLobbyMemberByIndexFn = std::uint64_t (__cdecl*)(
        void*, std::uint64_t, int);
    using GetLobbyDataFn = const char* (__cdecl*)(
        void*, std::uint64_t, const char*);

    bool Initialize() noexcept;

    bool symbols_resolved_{};
    void* client_{};
    void* user_interface_{};
    void* matchmaking_{};
    int user_handle_{};
    int pipe_handle_{};
    std::uint64_t local_steam_id_{};
    SteamClientFn steam_client_{};
    GetHandleFn get_user_handle_{};
    GetHandleFn get_pipe_handle_{};
    GetInterfaceFn get_user_interface_{};
    GetInterfaceFn get_matchmaking_{};
    GetSteamIdFn get_steam_id_{};
    GetNumLobbyMembersFn get_member_count_{};
    GetLobbyMemberByIndexFn get_member_{};
    GetLobbyDataFn get_lobby_data_{};
};

Status BuildOnlinePeerContract(
    const Sc6OnlineSessionIdentity& session,
    const SteamLobbyIdentity& lobby,
    const OnlineContentContract& content,
    const CanonicalHash& executable_id,
    const CanonicalHash& build_id,
    std::uint32_t input_delay,
    std::uint32_t rollback_window,
    OnlinePeerContract& output) noexcept;

Status HashFileIdentity(
    const std::filesystem::path& path,
    CanonicalHash& output) noexcept;

// Hashes the exact authored streaming-level package set. Input order is not
// significant; duplicates and unbounded sets fail closed.
Status HashMapPackageIdentity(
    std::span<const std::string_view> package_names,
    CanonicalHash& output) noexcept;

Status HashOnlineSelectionIdentity(
    const OnlineContentContract& content,
    CanonicalHash& output) noexcept;
}
