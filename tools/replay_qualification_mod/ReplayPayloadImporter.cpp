#include "ReplayPayloadImporter.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FString.hpp>

#include <array>
#include <climits>
#include <cstring>

namespace Horse::Qualification
{
namespace
{
struct ByteArray
{
    std::byte* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};
static_assert(sizeof(ByteArray) == 0x10);

using InitializeItemFn = void* (__fastcall*)(void*);
using DestroyItemFn = void (__fastcall*)(void*);
using GetSaveManagerFn = void* (__fastcall*)(bool);
using DecompressFn = bool (__fastcall*)(ByteArray*, ByteArray*);
using DeserializeFn = bool (__fastcall*)(ByteArray*, void*);
using CopyFn = void* (__fastcall*)(void*, void*);
using FreeFn = void (__fastcall*)(void*);
using GetContainerClassFn = const RC::Unreal::UClass* (__fastcall*)();
using RequestPlayerProfilesFn = bool (__fastcall*)(void*);
using RequestReadyReplayFn = void (__fastcall*)(void*);
using InitializeProfileFn = void* (__fastcall*)(void*);
using DestroyProfileFn = void (__fastcall*)(void*);
using CopyProfileFn = void* (__fastcall*)(void*, void*);
using SerializeFn = ByteArray* (__fastcall*)(ByteArray*, void*);
using SerializedLengthFn = std::int64_t (__fastcall*)(void*);

struct NativeFunctions
{
    InitializeItemFn initialize{};
    DestroyItemFn destroy{};
    GetSaveManagerFn get_save_manager{};
    DecompressFn decompress{};
    DeserializeFn deserialize{};
    CopyFn copy_item{};
    FreeFn free_memory{};
    GetContainerClassFn get_container_class{};
    RequestPlayerProfilesFn request_player_profiles{};
    RequestReadyReplayFn request_ready_replay{};
    InitializeProfileFn initialize_profile{};
    DestroyProfileFn destroy_profile{};
    CopyProfileFn copy_profile{};
    CopyFn copy_battle{};
    SerializeFn serialize{};
    SerializedLengthFn serialized_length{};
};

NativeFunctions g_functions{};

struct FunctionContract
{
    std::uintptr_t rva;
    std::array<std::byte, 8> signature;
};

constexpr FunctionContract kContracts[]{
    {0x599130, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x10}, std::byte{0x48}, std::byte{0x89}, std::byte{0x74}}},
    {0x538580, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}}},
    {0x5b49b0, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x18}, std::byte{0x48}, std::byte{0x89}, std::byte{0x74}}},
    {0x5799d0, {std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
                std::byte{0xec}, std::byte{0x20}, std::byte{0x48}, std::byte{0x8b}}},
    {0x4eeba0, {std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
                std::byte{0xec}, std::byte{0x20}, std::byte{0x48}, std::byte{0x8b}}},
    {0x50bda0, {std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
                std::byte{0xec}, std::byte{0x20}, std::byte{0x84}, std::byte{0xc9}}},
    {0x2dce6f0, {std::byte{0x40}, std::byte{0x53}, std::byte{0x41}, std::byte{0x56},
                 std::byte{0x41}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}}},
    {0x5b17f0, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x10}, std::byte{0x57}, std::byte{0x48}, std::byte{0x81}}},
    {0x57e1b0, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}}},
    {0xd46a00, {std::byte{0x48}, std::byte{0x85}, std::byte{0xc9}, std::byte{0x74},
                std::byte{0x1d}, std::byte{0x4c}, std::byte{0x8b}, std::byte{0x05}}},
    {0xb77900, {std::byte{0x48}, std::byte{0x81}, std::byte{0xec}, std::byte{0x98},
                std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x48}}},
    {0x5e90c0, {std::byte{0x40}, std::byte{0x55}, std::byte{0x53}, std::byte{0x57},
                std::byte{0x41}, std::byte{0x54}, std::byte{0x48}, std::byte{0x8d}}},
    {0x5ea1c0, {std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
                std::byte{0xec}, std::byte{0x20}, std::byte{0x48}, std::byte{0x8b}}},
    {0x5e3010, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x10}, std::byte{0x57}, std::byte{0x48}, std::byte{0x81}}},
    {0x2dc0270, {std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
                  std::byte{0xec}, std::byte{0x20}, std::byte{0x48}, std::byte{0x8b}}},
    {0x4eeed0, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}}},
    {0x4f1cf0, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x10}, std::byte{0x48}, std::byte{0x89}, std::byte{0x6c}}},
};

bool SafeEqual(const void* left, const void* right, std::size_t size) noexcept
{
    __try { return std::memcmp(left, right, size) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ValidateContracts(std::uintptr_t image_base) noexcept
{
    for (const FunctionContract& contract : kContracts)
    {
        if (!SafeEqual(reinterpret_cast<void*>(image_base + contract.rva),
                       contract.signature.data(), contract.signature.size()))
        {
            return false;
        }
    }
    return true;
}

template <typename Result, typename Callable>
Result SafeCall(Result failure, Callable&& callable) noexcept
{
    __try { return callable(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return failure; }
}

void Release(ByteArray& bytes) noexcept
{
    if (bytes.data != nullptr && g_functions.free_memory != nullptr)
    {
        SafeCall(false, [&]() {
            g_functions.free_memory(bytes.data);
            return true;
        });
    }
    bytes = {};
}

using Digest = std::array<std::uint8_t, 32>;

bool BattleIdentity(void* battle, std::uint32_t version, Digest& output) noexcept
{
    // Serialize an owned deep copy through the game's writer. Hash only its
    // battle-data suffix, independently of list-row names or summary fields.
    alignas(16) std::array<std::byte, 0x1A00> item{};
    ByteArray encoded{};
    if (!SafeCall(false, [&]() { g_functions.initialize(item.data()); return true; })) return false;
    const bool copied = SafeCall(false, [&]() {
        std::memcpy(item.data() + 0x18, &version, sizeof(version));
        g_functions.copy_battle(item.data() + 0xA0, battle);
        // Native writer only computes this field when version == -1.
        // Preserve the source version and explicitly measure this owned copy.
        const auto length = g_functions.serialized_length(item.data());
        if (length <= 0 || length > 64 * 1024 * 1024) return false;
        std::memcpy(item.data() + 0x20, &length, sizeof(length));
        g_functions.serialize(&encoded, item.data());
        return true;
    });
    const bool hashed = copied && SafeCall(false, [&]() {
        if (encoded.data == nullptr || encoded.count < 0x1C || encoded.count > encoded.capacity
            || encoded.count > 64 * 1024 * 1024)
            return false;
        std::uint64_t length{};
        std::memcpy(&length, encoded.data + 0x14, sizeof(length));
        if (length == 0 || length > static_cast<std::uint64_t>(encoded.count - 0x1C)) return false;
        return BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
            reinterpret_cast<PUCHAR>(encoded.data + encoded.count - length),
            static_cast<ULONG>(length), output.data(), static_cast<ULONG>(output.size())));
    });
    const bool destroyed = SafeCall(false, [&]() { g_functions.destroy(item.data()); return true; });
    Release(encoded);
    return hashed && destroyed;
}

bool RecordingIdentity(const std::byte* reset, const std::byte* recording, Digest& output) noexcept
{
    BCRYPT_HASH_HANDLE hash{};
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0)))
        return false;
    const bool hashed = SafeCall(false, [&]() {
        std::size_t total{};
        const auto append = [&](const void* data, std::size_t size) {
            if (size > 64 * 1024 * 1024 - total || (size != 0 && data == nullptr)) return false;
            total += size;
            return BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                static_cast<ULONG>(size), 0));
        };
        const auto reset_count = *reinterpret_cast<const std::int32_t*>(reset + 8);
        const auto round_count = *reinterpret_cast<const std::int32_t*>(recording + 8);
        if (reset_count <= 0 || reset_count > 16 || round_count <= 0 || round_count > 16
            || reset_count > *reinterpret_cast<const std::int32_t*>(reset + 12)
            || round_count > *reinterpret_cast<const std::int32_t*>(recording + 12)
            || !append(&reset_count, 4) || !append(*reinterpret_cast<void* const*>(reset), reset_count * 0xC0)
            || !append(&round_count, 4)) return false;
        const auto* rounds = *reinterpret_cast<const std::byte* const*>(recording);
        if (rounds == nullptr) return false;
        for (std::int32_t round = 0; round < round_count; ++round) {
            const auto* row = rounds + round * 0x10;
            const auto count = *reinterpret_cast<const std::int32_t*>(row + 8);
            const auto* entries = *reinterpret_cast<const std::byte* const*>(row);
            if (entries == nullptr || count <= 0 || count > 8
                || count > *reinterpret_cast<const std::int32_t*>(row + 12)
                || !append(&count, 4)) return false;
            for (std::int32_t recorder = 0; recorder < count; ++recorder) {
                const auto* object = *reinterpret_cast<const std::byte* const*>(entries + recorder * 0x18 + 0x10);
                if (object == nullptr) return false;
                const auto bytes = *reinterpret_cast<const std::int32_t*>(object + 0x10);
                if (bytes < 0 || bytes > *reinterpret_cast<const std::int32_t*>(object + 0x14)
                    || !append(&bytes, 4)
                    || !append(*reinterpret_cast<void* const*>(object + 8), static_cast<std::size_t>(bytes))) return false;
            }
        }
        return BCRYPT_SUCCESS(BCryptFinishHash(hash, output.data(), static_cast<ULONG>(output.size()), 0));
    });
    BCryptDestroyHash(hash);
    return hashed;
}
}

ReplayPayloadImporter::~ReplayPayloadImporter()
{
    ReleasePlaybackContext();
}

bool ReplayPayloadImporter::Bind(std::uintptr_t image_base) noexcept
{
    ReleasePlaybackContext();
    g_functions = {};
    if (image_base == 0 || !ValidateContracts(image_base)) return false;
    g_functions = {
        reinterpret_cast<InitializeItemFn>(image_base + 0x5799d0),
        reinterpret_cast<DestroyItemFn>(image_base + 0x4eeba0),
        reinterpret_cast<GetSaveManagerFn>(image_base + 0x50bda0),
        reinterpret_cast<DecompressFn>(image_base + 0x2dce6f0),
        reinterpret_cast<DeserializeFn>(image_base + 0x5b17f0),
        reinterpret_cast<CopyFn>(image_base + 0x57e1b0),
        reinterpret_cast<FreeFn>(image_base + 0xd46a00),
        reinterpret_cast<GetContainerClassFn>(image_base + 0xb77900),
        reinterpret_cast<RequestPlayerProfilesFn>(image_base + 0x5e90c0),
        reinterpret_cast<RequestReadyReplayFn>(image_base + 0x5ea1c0),
        reinterpret_cast<InitializeProfileFn>(image_base + 0x2dc0270),
        reinterpret_cast<DestroyProfileFn>(image_base + 0x4eeed0),
        reinterpret_cast<CopyProfileFn>(image_base + 0x4f1cf0),
        reinterpret_cast<CopyFn>(image_base + 0x538580),
        reinterpret_cast<SerializeFn>(image_base + 0x5b49b0),
        reinterpret_cast<SerializedLengthFn>(image_base + 0x599130)};
    return true;
}

ImportFailure ReplayPayloadImporter::Import(
    std::span<const std::byte> payload, ReplayMetadata& metadata) noexcept
{
    constexpr std::size_t kMaximumPayload = 64u * 1024u * 1024u;
    constexpr std::size_t kItemSize = 0x1a00;
    constexpr std::size_t kBattleData = 0xa0;
    constexpr std::size_t kContainerCurrentItem = 0x80;
    constexpr std::size_t kStageIndex = kBattleData + 0x98;
    constexpr std::size_t kLeftCharacter = kBattleData + 0xa0 + 0x28 + 0x08;
    constexpr std::size_t kRightCharacter = kBattleData + 0xcf0 + 0x28 + 0x08;
    constexpr std::size_t kStateResetData = kBattleData + 0x1940;
    metadata = {};
    ReleasePlaybackContext();
    identity_valid_ = false;
    identity_phase_ = "none";
    if (g_functions.initialize == nullptr || payload.size() < 8
        || payload.size() > kMaximumPayload || payload.size() > INT32_MAX
        || std::memcmp(payload.data(), "ULX1", 4) != 0)
    {
        return g_functions.initialize == nullptr
            ? ImportFailure::UnsupportedExecutable : ImportFailure::InvalidPayload;
    }

    ByteArray input{const_cast<std::byte*>(payload.data()),
                    static_cast<std::int32_t>(payload.size()),
                    static_cast<std::int32_t>(payload.size())};
    ByteArray decoded{};
    alignas(16) std::array<std::byte, kItemSize> item{};
    if (!SafeCall(false, [&]() { g_functions.initialize(item.data()); return true; }))
        return ImportFailure::InitializeFailed;

    ImportFailure result = ImportFailure::None;
    if (!SafeCall(false, [&]() { return g_functions.decompress(&decoded, &input); })
        || decoded.data == nullptr || decoded.count <= 0)
    {
        result = ImportFailure::DecompressFailed;
    }
    else if (!SafeCall(false, [&]() { return g_functions.deserialize(&decoded, item.data()); }))
    {
        result = ImportFailure::DeserializeFailed;
    }
    else
    {
        std::memcpy(&replay_version_, item.data() + 0x18, sizeof(replay_version_));
        // 1405A9C40 serializes summary +8 at wire +24 for version 0x2A.
        if (replay_version_ == 0x2A && decoded.count >= 0x28 && decoded.count <= decoded.capacity)
            std::memcpy(&metadata.recorded_match_winner, decoded.data + 0x24, sizeof(std::int32_t));
        identity_phase_ = "payload_serialize";
        identity_valid_ = BattleIdentity(item.data() + kBattleData, replay_version_, battle_identity_);
        if (identity_valid_) {
            identity_phase_ = "payload_recordings";
            identity_valid_ = RecordingIdentity(item.data() + kStateResetData,
                item.data() + kBattleData + 0x1950, recording_identity_);
        }
        if (!identity_valid_) result = ImportFailure::IdentityMismatch;
        std::memcpy(&metadata.stage_index, item.data() + kStageIndex,
                    sizeof(metadata.stage_index));
        std::memcpy(&metadata.left_character, item.data() + kLeftCharacter,
                    sizeof(metadata.left_character));
        std::memcpy(&metadata.right_character, item.data() + kRightCharacter,
                    sizeof(metadata.right_character));
        ByteArray state_reset_data{};
        std::memcpy(&state_reset_data, item.data() + kStateResetData,
                    sizeof(state_reset_data));
        if (state_reset_data.data != nullptr && state_reset_data.count > 0
            && state_reset_data.count <= static_cast<std::int32_t>(
                ReplayMetadata::kMaximumStateResetRecords))
            metadata.state_reset_record_count =
                static_cast<std::uint32_t>(state_reset_data.count);
        const std::int32_t map_index = metadata.stage_index > 0xff
            ? metadata.stage_index & 0xff : metadata.stage_index;
        if (metadata.stage_index < 0 || metadata.stage_index > 0xfff
            || map_index < 0 || map_index > 0xff
            || metadata.left_character == 0xff
            || metadata.right_character == 0xff
            || metadata.state_reset_record_count == 0)
        {
            result = ImportFailure::InvalidMetadata;
        }
        if (result == ImportFailure::None)
        {
            void* save = SafeCall<void*>(nullptr, [&]() {
                return g_functions.get_save_manager(false);
            });
            if (save == nullptr)
            {
                result = ImportFailure::SaveManagerUnavailable;
            }
            if (result == ImportFailure::None)
            {
                playback_container_ = SafeCall<void*>(nullptr, [&]() {
                    const RC::Unreal::UClass* klass =
                        g_functions.get_container_class();
                    if (klass == nullptr) return static_cast<void*>(nullptr);
                    RC::Unreal::UObject* container =
                        RC::Unreal::UObjectGlobals::NewObject<
                            RC::Unreal::UObject>(nullptr, klass);
                    if (container != nullptr) container->SetRootSet();
                    return static_cast<void*>(container);
                });
                if (playback_container_ == nullptr)
                {
                    result = ImportFailure::ContainerCreateFailed;
                }
                else
                {
                    auto* container =
                        static_cast<std::byte*>(playback_container_);
                    const bool copied = SafeCall(false, [&]() {
                        g_functions.copy_item(
                            container + kContainerCurrentItem, item.data());
                        return true;
                    });
                    if (!copied)
                    {
                        result = ImportFailure::PlaybackContextCopyFailed;
                        ReleasePlaybackContext();
                    }
                    else {
                        identity_phase_ = "container_copy";
                        Digest staged{};
                        if (!BattleIdentity(container + kContainerCurrentItem + kBattleData,
                                replay_version_, staged) || staged != battle_identity_)
                            result = ImportFailure::IdentityMismatch;
                    }
                }
            }
        }
    }

    const bool destroyed = SafeCall(false, [&]() {
        g_functions.destroy(item.data());
        return true;
    });
    Release(decoded);
    if (!destroyed && result == ImportFailure::None)
        result = ImportFailure::DestroyFailed;
    if (result != ImportFailure::None) ReleasePlaybackContext();
    return result;
}

bool ReplayPayloadImporter::RequestPlayerProfiles() noexcept
{
    if (playback_container_ == nullptr
        || g_functions.request_player_profiles == nullptr)
    {
        return false;
    }
    return SafeCall(false, [&]() {
        return g_functions.request_player_profiles(playback_container_);
    });
}

bool ReplayPayloadImporter::PopulateFallbackProfiles() noexcept
{
    constexpr std::size_t kProfileBytes = 0x140;
    constexpr std::size_t kLeftProfile = 0x1a80;
    constexpr std::size_t kRightProfile = 0x1bc0;
    constexpr std::size_t kRegion = 0x18;
    constexpr std::size_t kLanguage = 0x1a;
    constexpr std::size_t kDisplayName = 0x30;
    constexpr std::size_t kValid = 0xf9;
    if (playback_container_ == nullptr || g_functions.initialize_profile == nullptr
        || g_functions.destroy_profile == nullptr
        || g_functions.copy_profile == nullptr)
    {
        return false;
    }

    const auto populate = [&](std::size_t destination_offset,
                              const wchar_t* display_name) {
        alignas(16) std::array<std::byte, kProfileBytes> profile{};
        if (!SafeCall(false, [&]() {
                g_functions.initialize_profile(profile.data());
                return true;
            }))
        {
            return false;
        }

        bool copied = false;
        try
        {
            profile[kRegion] = std::byte{7};
            profile[kLanguage] = std::byte{2};
            profile[kValid] = std::byte{1};
            auto* name = reinterpret_cast<RC::Unreal::FString*>(
                profile.data() + kDisplayName);
            *name = RC::Unreal::FString(display_name);
            copied = SafeCall(false, [&]() {
                auto* destination =
                    static_cast<std::byte*>(playback_container_)
                    + destination_offset;
                g_functions.copy_profile(destination, profile.data());
                return true;
            });
        }
        catch (...)
        {
            copied = false;
        }
        SafeCall(false, [&]() {
            g_functions.destroy_profile(profile.data());
            return true;
        });
        return copied;
    };

    return populate(kLeftProfile, L"P1")
        && populate(kRightProfile, L"P2");
}

bool ReplayPayloadImporter::RequestReadyPlayback() noexcept
{
    if (playback_container_ == nullptr
        || g_functions.request_ready_replay == nullptr)
    {
        return false;
    }
    return SafeCall(false, [&]() {
        // This is ULuxReplayListContainer::RequestReadyReplay, the exact
        // stock Play path. It invokes the virtual OnRequestPlay ownership
        // transfer and then broadcasts OnReadyReplayCompleted. Do not call
        // OnRequestPlay or ApplyReplayToBattleSetup directly: ReplaySetupScene
        // installs temporary creation profiles in OnStartVersusInfo and
        // applies the replay battle setup later in OnRequestToStop.
        g_functions.request_ready_replay(playback_container_);
        identity_phase_ = "save_staging";
        auto* save = static_cast<std::byte*>(g_functions.get_save_manager(false));
        Digest staged{};
        return identity_valid_ && save != nullptr
            && BattleIdentity(save + 0x40, replay_version_, staged) && staged == battle_identity_;
    });
}

bool ReplayPayloadImporter::VerifyPlaybackRecording(void* replay_player, Digest& actual) noexcept
{
    if (!identity_valid_ || replay_player == nullptr) return false;
    const auto* player = static_cast<const std::byte*>(replay_player);
    actual = {};
    return RecordingIdentity(player + 0x3A8, player + 0x3B8, actual)
        && actual == recording_identity_;
}

void ReplayPayloadImporter::ReleasePlaybackContext() noexcept
{
    if (playback_container_ == nullptr) return;
    SafeCall(false, [&]() {
        auto* container =
            static_cast<RC::Unreal::UObject*>(playback_container_);
        if (container->IsRootSet()) container->ClearRootSet();
        return true;
    });
    playback_container_ = nullptr;
}

}
