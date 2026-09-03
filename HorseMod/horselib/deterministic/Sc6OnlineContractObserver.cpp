#include "Sc6OnlineContractObserver.hpp"
#include "Sc6StageCatalog.hpp"

#include <algorithm>
#include <atomic>
#include <bcrypt.h>
#include <charconv>
#include <cstring>
#include <cstdio>
#include <string>

namespace Horse::Deterministic
{
namespace
{
constexpr std::uintptr_t get_local_online_session_rva = 0x003f07a0;
constexpr std::uintptr_t get_online_session_interface_rva = 0x02ea0470;
constexpr std::uintptr_t message_router_vtable_rva = 0x03d27940;
constexpr std::size_t message_router_subobject_offset = 0x18;
constexpr std::size_t session_name_offset = 0x30;
constexpr std::size_t named_session_info_offset = 0xa8;
constexpr std::size_t steam_lobby_id_offset = 0x48;
constexpr std::uint32_t tournament_discriminator_bit = 0x40000;
constexpr std::size_t sync_character_data_offset = 0x2e0;
constexpr std::size_t sync_character_data_stride = 0xc70;
constexpr std::size_t sync_character_code_offset = 0xc58;
constexpr std::size_t sync_stage_code_offset = 0x1bc0;
constexpr std::size_t sync_stage_random_offset = 0x1bc4;
constexpr std::size_t sync_stage_seed_offset = 0x1bc8;
constexpr std::size_t sync_characters_received_offset = 0x1bce;
constexpr std::size_t sync_stage_received_offset = 0x1bcf;

struct UnrealWideString
{
    const wchar_t* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

static_assert(sizeof(UnrealWideString) == 0x10);

struct SharedSession
{
    void* object{};
    void* controller{};
};

using GetLocalOnlineSessionFn = SharedSession* (*)(SharedSession*);
using GetOnlineSessionInterfaceFn = SharedSession* (*)(SharedSession*, void*);
using GetRoleFn = std::int8_t (*)(void*);
using GetStateFn = std::uint8_t (*)(void*);
using GetNamedSessionFn = void* (*)(void*, std::uint64_t);

void ReleaseSharedSession(SharedSession& value) noexcept
{
    auto* controller = static_cast<std::byte*>(value.controller);
    value.object = nullptr;
    value.controller = nullptr;
    if (controller == nullptr) return;
    auto* strong = reinterpret_cast<volatile long*>(controller + 8);
    if (InterlockedDecrement(strong) != 0) return;
    auto** vtable = *reinterpret_cast<void***>(controller);
    reinterpret_cast<void (*)(void*)>(vtable[0])(controller);
    auto* weak = reinterpret_cast<volatile long*>(controller + 12);
    if (InterlockedDecrement(weak) == 0)
        reinterpret_cast<void (*)(void*, int)>(vtable[1])(controller, 1);
}

bool HasIdentity(const CanonicalHash& value) noexcept
{
    return std::any_of(value.begin(), value.end(),
        [](std::byte item) { return item != std::byte{}; });
}

bool HasBoundedMapName(const OnlineContentContract& content) noexcept
{
    return content.map_name[0] != '\0'
        && std::find(content.map_name.begin(), content.map_name.end(), '\0')
            != content.map_name.end();
}

template <std::size_t Size>
bool HasBoundedText(const std::array<char, Size>& value) noexcept
{
    return value[0] != '\0'
        && std::find(value.begin(), value.end(), '\0') != value.end();
}

template <std::size_t Size>
bool CopyUnrealString(
    const UnrealWideString& source, std::array<char, Size>& output) noexcept
{
    if (source.data == nullptr || source.count <= 1
        || source.capacity < source.count || source.count > 64
        || source.data[source.count - 1] != L'\0')
        return false;
    const int characters = source.count - 1;
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        source.data, characters, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0 || static_cast<std::size_t>(bytes) >= Size)
        return false;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data,
            characters, output.data(), bytes, nullptr, nullptr) != bytes)
        return false;
    output[static_cast<std::size_t>(bytes)] = '\0';
    return true;
}
}

Status Sc6BattleSyncObserver::Observe(
    const void* battle_sync_object, OnlineContentContract& output) const noexcept
{
    Sc6BattleSyncIdentity detailed{};
    const auto status = ObserveDetailed(battle_sync_object, detailed);
    output = status.ok() ? detailed.content : OnlineContentContract{};
    return status;
}

Status Sc6BattleSyncObserver::ObserveDetailed(
    const void* battle_sync_object, Sc6BattleSyncIdentity& output) const noexcept
{
    output = {};
    if (battle_sync_object == nullptr)
        return Status::failure(FailureCode::ContextUnavailable);
    output.battle_sync_object = reinterpret_cast<std::uintptr_t>(
        battle_sync_object);
    output.observation_stage = Sc6BattleSyncIdentity::ObservationStage::Acquired;
    Status status = Status::failure(FailureCode::ContextUnavailable);
#if defined(_MSC_VER)
    __try
    {
#endif
        const auto* object = static_cast<const std::byte*>(battle_sync_object);
        const auto characters_received = *reinterpret_cast<const std::uint8_t*>(
            object + sync_characters_received_offset);
        const auto stage_received = *reinterpret_cast<const std::uint8_t*>(
            object + sync_stage_received_offset);
        // Stock IsCompleted checks both bytes for nonzero, not equality to 1.
        // Preserve that native contract so a truthy flag representation does
        // not strand exact-content qualification after the payload is valid.
        output.characters_received = characters_received != 0;
        output.stage_received = stage_received != 0;
        output.observation_stage =
            Sc6BattleSyncIdentity::ObservationStage::CompletionFlags;
        if (characters_received != 0 && stage_received != 0)
        {
            auto& content = output.content;
            bool valid = true;
            for (std::size_t player = 0; player < 2; ++player)
            {
                const auto* text = reinterpret_cast<const UnrealWideString*>(
                    object + sync_character_data_offset
                    + player * sync_character_data_stride
                    + sync_character_code_offset);
                output.fighter_code_valid[player] = CopyUnrealString(
                    *text, content.fighter_codes[player]);
                valid = valid && output.fighter_code_valid[player];
            }
            output.observation_stage =
                Sc6BattleSyncIdentity::ObservationStage::FighterCodes;
            const auto stage = *reinterpret_cast<const std::uint16_t*>(
                object + sync_stage_code_offset);
            const auto random = *reinterpret_cast<const std::uint8_t*>(
                object + sync_stage_random_offset);
            output.native_stage_code = stage;
            output.native_stage_random = random;
            content.stage_rng_seed = *reinterpret_cast<const std::uint32_t*>(
                object + sync_stage_seed_offset);
            valid = valid && stage != 0 && stage <= 999 && random <= 1;
            output.observation_stage =
                Sc6BattleSyncIdentity::ObservationStage::StageFields;
            if (valid)
            {
                const int stage_length = std::snprintf(content.stage_code.data(),
                    content.stage_code.size(), "%03u", unsigned{stage});
                const auto* catalog = stage_length == 3
                    ? FindQualifiedStage(content.stage_code.data()) : nullptr;
                valid = catalog != nullptr
                    && catalog->package_root.size() < content.map_name.size();
                if (valid)
                {
                    std::memcpy(content.map_name.data(),
                        catalog->package_root.data(), catalog->package_root.size());
                    content.map_name[catalog->package_root.size()] = '\0';
                }
                output.observation_stage =
                    Sc6BattleSyncIdentity::ObservationStage::StageCatalog;
            }
            content.stage_was_random = random != 0;
            if (valid)
            {
                status = HashOnlineSelectionIdentity(
                    content, content.map_identity);
                output.observation_stage =
                    Sc6BattleSyncIdentity::ObservationStage::SelectionIdentity;
            }
            else
            {
                status = Status::failure(FailureCode::IdentityMismatch);
            }
        }
#if defined(_MSC_VER)
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = Status::failure(FailureCode::ContextUnavailable);
    }
#endif
    // Keep bounded raw diagnostics on failure. Observe() still exposes an
    // empty content contract unless the complete validation succeeds.
    return status;
}

Status LatchSc6BattleSyncContent(
    const Sc6BattleSyncObserver& observer,
    const void* battle_sync_object,
    OnlineContentContract& latched_content,
    bool& latched_valid,
    Sc6BattleSyncIdentity& observation) noexcept
{
    observation = {};
    if (latched_valid) return Status::success();
    const auto status = observer.ObserveDetailed(
        battle_sync_object, observation);
    if (status.ok())
    {
        latched_content = observation.content;
        latched_valid = true;
    }
    return status;
}

Status Sc6OnlineSessionObserver::Initialize(std::uintptr_t image_base) noexcept
{
    if (image_base == 0)
        return Status::failure(FailureCode::InvalidConfiguration);
    image_base_ = image_base;
    return Status::success();
}

Status Sc6OnlineSessionObserver::Observe(
    Sc6OnlineSessionIdentity& output) const noexcept
{
    const auto status = ObserveCurrent(output);
    if (!status.ok()) return status;
    if (!IsSc6PreownershipSessionState(output.virtual_session_state))
        return Status::failure(FailureCode::ContextUnavailable);
    return status;
}

Status Sc6OnlineSessionObserver::ObserveCurrent(
    Sc6OnlineSessionIdentity& output) const noexcept
{
    output = {};
    if (image_base_ == 0)
        return Status::failure(FailureCode::ContextUnavailable);

    SharedSession retained{};
    SharedSession online_interface{};
    Status status = Status::failure(FailureCode::ContextUnavailable);
#if defined(_MSC_VER)
    __try
    {
#endif
        const auto acquire = reinterpret_cast<GetLocalOnlineSessionFn>(
            image_base_ + get_local_online_session_rva);
        acquire(&retained);
        if (retained.object != nullptr && retained.controller != nullptr)
        {
            auto* session = static_cast<std::byte*>(retained.object);
            output.session_interface =
                reinterpret_cast<std::uintptr_t>(session);
            output.observation_stage =
                Sc6OnlineSessionObservationStage::Acquired;
            auto** session_vtable = *reinterpret_cast<void***>(session);
            const auto expected_vtable = reinterpret_cast<void*>(
                image_base_ + message_router_vtable_rva);
            if (session_vtable == expected_vtable)
            {
                output.observation_stage =
                    Sc6OnlineSessionObservationStage::ExpectedVtable;
                const auto role = reinterpret_cast<GetRoleFn>(
                    session_vtable[0])(session);
                // The retained pointer is the message-router interface at
                // LuxorActiveConnect +0x18.  Native vtable slot 1 reads the
                // authoritative session state from interface +0x26 (outer
                // +0x3e).  Outer +0x3d is a distinct connect-system byte.
                const auto state = reinterpret_cast<GetStateFn>(
                    session_vtable[1])(session);
                auto* active = session - message_router_subobject_offset;
                output.active_connect =
                    reinterpret_cast<std::uintptr_t>(active);
                output.role = role;
                output.virtual_session_state = state;
                if (role == 0 || role == 1)
                    output.local_player_slot = static_cast<std::uint8_t>(role);
                output.observation_stage =
                    Sc6OnlineSessionObservationStage::RoleAndState;
                const auto session_name = *reinterpret_cast<std::uint64_t*>(
                    active + session_name_offset);
                output.session_name = session_name;
                if ((role == 0 || role == 1) && session_name != 0)
                {
                    const auto acquire_online = reinterpret_cast<
                        GetOnlineSessionInterfaceFn>(
                            image_base_ + get_online_session_interface_rva);
                    acquire_online(&online_interface, nullptr);
                    auto* online_session = online_interface.object;
                    if (online_session != nullptr
                        && online_interface.controller != nullptr)
                    {
                        output.observation_stage =
                            Sc6OnlineSessionObservationStage::OnlineSession;
                        output.online_session =
                            reinterpret_cast<std::uintptr_t>(online_session);
                        auto** online_vtable = *reinterpret_cast<void***>(
                            online_session);
                        // Native create/join completion handlers acquire this
                        // same shared interface and call vtable +0x18 with the
                        // packed FName by value.
                        const auto get_named =
                            reinterpret_cast<GetNamedSessionFn>(
                                online_vtable[3]);
                        auto* named_session = static_cast<std::byte*>(
                            get_named(online_session, session_name));
                        if (named_session != nullptr)
                        {
                            output.observation_stage =
                                Sc6OnlineSessionObservationStage::NamedSession;
                            output.named_session =
                                reinterpret_cast<std::uintptr_t>(named_session);
                            auto* session_info =
                                *reinterpret_cast<std::byte**>(
                                    named_session
                                    + named_session_info_offset);
                            if (session_info != nullptr)
                            {
                                output.observation_stage =
                                    Sc6OnlineSessionObservationStage::SessionInfo;
                                output.session_info =
                                    reinterpret_cast<std::uintptr_t>(
                                        session_info);
                                output.lobby_id =
                                    *reinterpret_cast<std::uint64_t*>(
                                        session_info
                                        + steam_lobby_id_offset);
                                if (output.lobby_id != 0)
                                    output.observation_stage =
                                        Sc6OnlineSessionObservationStage::
                                            LobbyIdentity;
                                status = output.lobby_id != 0
                                    ? Status::success()
                                    : Status::failure(
                                        FailureCode::IdentityMismatch);
                            }
                        }
                    }
                }
            }
        }
#if defined(_MSC_VER)
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = Status::failure(FailureCode::ContextUnavailable);
    }
#endif
    ReleaseSharedSession(online_interface);
    ReleaseSharedSession(retained);
    return status;
}

bool SteamLobbyObserver::Initialize() noexcept
{
    const HMODULE module = GetModuleHandleW(L"steam_api64.dll");
    if (!module) return false;
    if (!symbols_resolved_)
    {
        steam_client_ = Resolve<SteamClientFn>(module, "SteamClient");
        get_user_handle_ = Resolve<GetHandleFn>(
            module, "SteamAPI_GetHSteamUser");
        get_pipe_handle_ = Resolve<GetHandleFn>(
            module, "SteamAPI_GetHSteamPipe");
        get_user_interface_ = Resolve<GetInterfaceFn>(
            module, "SteamAPI_ISteamClient_GetISteamUser");
        get_matchmaking_ = Resolve<GetInterfaceFn>(
            module, "SteamAPI_ISteamClient_GetISteamMatchmaking");
        get_steam_id_ = Resolve<GetSteamIdFn>(
            module, "SteamAPI_ISteamUser_GetSteamID");
        get_member_count_ = Resolve<GetNumLobbyMembersFn>(
            module, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
        get_member_ = Resolve<GetLobbyMemberByIndexFn>(
            module, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
        get_lobby_data_ = Resolve<GetLobbyDataFn>(
            module, "SteamAPI_ISteamMatchmaking_GetLobbyData");
        symbols_resolved_ = steam_client_ && get_user_handle_
            && get_pipe_handle_ && get_user_interface_ && get_matchmaking_
            && get_steam_id_ && get_member_count_ && get_member_
            && get_lobby_data_;
    }
    if (!symbols_resolved_) return false;
    void* client = steam_client_();
    const int user = get_user_handle_();
    const int pipe = get_pipe_handle_();
    if (!client || user == 0 || pipe == 0) return false;
    if (client != client_ || user != user_handle_ || pipe != pipe_handle_
        || user_interface_ == nullptr || matchmaking_ == nullptr)
    {
        client_ = client;
        user_handle_ = user;
        pipe_handle_ = pipe;
        user_interface_ = get_user_interface_(
            client, user, pipe, "SteamUser019");
        matchmaking_ = get_matchmaking_(
            client, user, pipe, "SteamMatchMaking009");
    }
    if (!user_interface_ || !matchmaking_) return false;
    local_steam_id_ = get_steam_id_(user_interface_);
    return local_steam_id_ != 0;
}

Status SteamLobbyObserver::Observe(
    std::uint64_t lobby_id, SteamLobbyIdentity& output) noexcept
{
    output = {};
    output.observed_member_count = -1;
    if (lobby_id == 0 || !Initialize())
        return Status::failure(FailureCode::ContextUnavailable);
    output.observation_mask |= 1u << 0;
    output.local_steam_id = local_steam_id_;
    const int count = get_member_count_(matchmaking_, lobby_id);
    output.observed_member_count = count;
    if (count != 2)
        return Status::failure(FailureCode::IdentityMismatch);
    output.observation_mask |= 1u << 1;
    output.member_count = 2;
    for (int index = 0; index < count; ++index)
        output.members[static_cast<std::size_t>(index)] =
            get_member_(matchmaking_, lobby_id, index);
    if (output.members[0] == 0 || output.members[1] == 0
        || output.members[0] == output.members[1]
        || (output.members[0] != local_steam_id_
            && output.members[1] != local_steam_id_))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    output.observation_mask |= 1u << 2;

    // Steam OSS serializes advertised settings with type suffixes. Decompiled
    // FindLuxorPlayerSession requires SEARCHKEYWORDS=PlayerMatch; optional
    // EXCUSTOMSEARCHINT5 may be absent when its setting is zero. Historical
    // raw Steam capture confirms SEARCHKEYWORDS_s and EXCUSTOMSEARCHINT4_i.
    // Accept unsuffixed aliases for native-version compatibility, reject every
    // ranked-key representation, and copy each value immediately because
    // Steam owns the returned buffer.
    const auto read_lobby_data = [&](std::span<const char* const> keys) {
        for (const char* key : keys)
        {
            const char* value = get_lobby_data_(matchmaking_, lobby_id, key);
            if (value != nullptr && value[0] != '\0')
                return std::string(value);
        }
        return std::string{};
    };
    constexpr std::array player_keys{
        "EXCUSTOMSEARCHINT5_s", "EXCUSTOMSEARCHINT5"};
    constexpr std::array keyword_keys{
        "SEARCHKEYWORDS_s", "SEARCHKEYWORDS"};
    constexpr std::array ranked_keys{
        "RANKMATCH_NEAR_CLASS_i", "RANKMATCH_NEAR_CLASS_s",
        "RANKMATCH_NEAR_CLASS"};
    constexpr std::array discriminator_keys{
        "EXCUSTOMSEARCHINT4_i", "EXCUSTOMSEARCHINT4"};
    const auto player_key = read_lobby_data(player_keys);
    const auto search_keyword = read_lobby_data(keyword_keys);
    const auto rank_key = read_lobby_data(ranked_keys);
    const auto discriminator_text = read_lobby_data(discriminator_keys);
    const bool player_match_metadata = !player_key.empty()
        || search_keyword == "PlayerMatch";
    if (player_match_metadata) output.observation_mask |= 1u << 3;
    if (rank_key.empty()) output.observation_mask |= 1u << 4;
    std::uint32_t discriminator{};
    const char* discriminator_begin = discriminator_text.data();
    const char* discriminator_end = discriminator_begin
        + discriminator_text.size();
    const auto parsed = std::from_chars(
        discriminator_begin, discriminator_end, discriminator);
    const bool discriminator_valid = !discriminator_text.empty()
        && parsed.ec == std::errc{} && parsed.ptr == discriminator_end;
    if (discriminator_valid)
    {
        output.observation_mask |= 1u << 5;
        output.search_discriminator = discriminator;
        if ((discriminator & tournament_discriminator_bit) == 0)
            output.observation_mask |= 1u << 6;
    }
    output.casual_player_match = player_match_metadata && rank_key.empty()
        && discriminator_valid
        && (discriminator & tournament_discriminator_bit) == 0;
    if (!output.casual_player_match)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    return Status::success();
}

Status BuildOnlinePeerContract(
    const Sc6OnlineSessionIdentity& session,
    const SteamLobbyIdentity& lobby,
    const OnlineContentContract& content,
    const CanonicalHash& executable_id,
    const CanonicalHash& build_id,
    std::uint32_t input_delay,
    std::uint32_t rollback_window,
    OnlinePeerContract& output) noexcept
{
    output = {};
    if (session.lobby_id == 0 || lobby.local_steam_id == 0
        || session.local_player_slot > 1 || lobby.member_count != 2
        || lobby.members[0] == 0 || lobby.members[1] == 0
        || lobby.members[0] == lobby.members[1]
        || (lobby.members[0] != lobby.local_steam_id
            && lobby.members[1] != lobby.local_steam_id)
        || rollback_window == 0 || rollback_window > 30 || input_delay > 8
        || !lobby.casual_player_match
        || !HasIdentity(executable_id) || !HasIdentity(build_id)
        || !HasBoundedText(content.fighter_codes[0])
        || !HasBoundedText(content.fighter_codes[1])
        || !HasBoundedText(content.stage_code)
        || !HasIdentity(content.map_identity)
        || !HasBoundedMapName(content))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    const auto remote = lobby.members[0] == lobby.local_steam_id
        ? lobby.members[1] : lobby.members[0];
    output.session_id = session.lobby_id;
    output.lobby_id = session.lobby_id;
    output.local_player_slot = session.local_player_slot;
    output.lobby_member_count = lobby.member_count;
    output.casual_player_match = lobby.casual_player_match;
    output.steam_ids[session.local_player_slot] = lobby.local_steam_id;
    output.steam_ids[1u - session.local_player_slot] = remote;
    output.executable_id = executable_id;
    output.build_id = build_id;
    output.content = content;
    output.input_delay = input_delay;
    output.rollback_window = rollback_window;
    return Status::success();
}

Status HashFileIdentity(
    const std::filesystem::path& path, CanonicalHash& output) noexcept
{
    output = {};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL
            | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return Status::failure(FailureCode::ContextUnavailable);
    BCRYPT_HASH_HANDLE hash{};
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,
            &hash, nullptr, 0, nullptr, 0, 0)))
    {
        CloseHandle(file);
        return Status::failure(FailureCode::CaptureFailed);
    }
    std::array<std::byte, 64 * 1024> buffer{};
    bool ok = true;
    for (;;)
    {
        DWORD read{};
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                &read, nullptr))
        {
            ok = false;
            break;
        }
        if (read == 0) break;
        if (!BCRYPT_SUCCESS(BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(buffer.data()), read, 0)))
        {
            ok = false;
            break;
        }
    }
    if (ok)
        ok = BCRYPT_SUCCESS(BCryptFinishHash(hash,
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size()), 0));
    BCryptDestroyHash(hash);
    CloseHandle(file);
    if (!ok)
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    return Status::success();
}

Status HashMapPackageIdentity(
    std::span<const std::string_view> package_names,
    CanonicalHash& output) noexcept
{
    output = {};
    constexpr std::size_t maximum_packages = 64;
    if (package_names.empty() || package_names.size() > maximum_packages)
        return Status::failure(FailureCode::InvalidConfiguration);
    std::array<std::string_view, maximum_packages> sorted{};
    std::copy(package_names.begin(), package_names.end(), sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + package_names.size());
    for (std::size_t index = 0; index < package_names.size(); ++index)
    {
        if (sorted[index].empty() || sorted[index].size() > UINT32_MAX
            || (index != 0 && sorted[index] == sorted[index - 1]))
        {
            return Status::failure(FailureCode::IdentityMismatch);
        }
    }

    BCRYPT_HASH_HANDLE hash{};
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,
            &hash, nullptr, 0, nullptr, 0, 0)))
        return Status::failure(FailureCode::CaptureFailed);
    bool ok = true;
    const auto feed_u32 = [&](std::uint32_t value) noexcept {
        std::array<UCHAR, 4> bytes{
            static_cast<UCHAR>(value), static_cast<UCHAR>(value >> 8u),
            static_cast<UCHAR>(value >> 16u),
            static_cast<UCHAR>(value >> 24u)};
        return BCRYPT_SUCCESS(BCryptHashData(
            hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0));
    };
    ok = feed_u32(static_cast<std::uint32_t>(package_names.size()));
    for (std::size_t index = 0; ok && index < package_names.size(); ++index)
    {
        const auto name = sorted[index];
        ok = feed_u32(static_cast<std::uint32_t>(name.size()))
            && BCRYPT_SUCCESS(BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(const_cast<char*>(name.data())),
                static_cast<ULONG>(name.size()), 0));
    }
    if (ok)
        ok = BCRYPT_SUCCESS(BCryptFinishHash(hash,
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size()), 0));
    BCryptDestroyHash(hash);
    if (!ok)
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    return Status::success();
}

Status HashOnlineSelectionIdentity(
    const OnlineContentContract& content, CanonicalHash& output) noexcept
{
    output = {};
    if (!HasBoundedText(content.fighter_codes[0])
        || !HasBoundedText(content.fighter_codes[1])
        || !HasBoundedText(content.stage_code)
        || !HasBoundedMapName(content))
        return Status::failure(FailureCode::InvalidConfiguration);

    BCRYPT_HASH_HANDLE hash{};
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,
            &hash, nullptr, 0, nullptr, 0, 0)))
        return Status::failure(FailureCode::CaptureFailed);
    const auto feed = [&](const void* data, std::size_t size) noexcept {
        return size <= ULONG_MAX && BCRYPT_SUCCESS(BCryptHashData(hash,
            reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
            static_cast<ULONG>(size), 0));
    };
    const std::uint8_t random = content.stage_was_random ? 1u : 0u;
    bool ok = feed(content.fighter_codes.data(),
            sizeof(content.fighter_codes))
        && feed(content.stage_code.data(), content.stage_code.size())
        && feed(&content.stage_rng_seed, sizeof(content.stage_rng_seed))
        && feed(&random, sizeof(random))
        && feed(content.map_name.data(), content.map_name.size());
    if (ok)
        ok = BCRYPT_SUCCESS(BCryptFinishHash(hash,
            reinterpret_cast<PUCHAR>(output.data()),
            static_cast<ULONG>(output.size()), 0));
    BCryptDestroyHash(hash);
    if (!ok)
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    return Status::success();
}
}
