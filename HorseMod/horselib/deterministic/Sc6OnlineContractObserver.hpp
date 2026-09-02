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
};

// Native Lux online-session state consumers treat 1, 4, and 6 as live.
// Pre-ownership may bind while the pair is connected (1) or has entered the
// match transport state (4). State 6 can transition back into state 4 and is
// therefore not a fresh-match boundary; admitting it could bind stale battle
// sync content between matches.
[[nodiscard]] constexpr bool IsSc6PreownershipSessionState(
    std::uint8_t state) noexcept
{
    return state == 1 || state == 4;
}

struct Sc6BattleSyncIdentity
{
    OnlineContentContract content{};
    std::uintptr_t battle_sync_object{};
    bool characters_received{};
    bool stage_received{};
};

struct SteamLobbyIdentity
{
    std::array<std::uint64_t, 2> members{};
    std::uint64_t local_steam_id{};
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

class Sc6OnlineSessionObserver
{
public:
    Status Initialize(std::uintptr_t image_base) noexcept;
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
