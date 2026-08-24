#include "ReplayPayloadImporter.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

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
using ApplyPlaybackContextFn = void (__fastcall*)(void*);
using InitializeProfileFn = void* (__fastcall*)(void*);
using DestroyProfileFn = void (__fastcall*)(void*);
using CopyProfileFn = void* (__fastcall*)(void*, void*);

struct NativeFunctions
{
    InitializeItemFn initialize{};
    DestroyItemFn destroy{};
    GetSaveManagerFn get_save_manager{};
    DecompressFn decompress{};
    DeserializeFn deserialize{};
    CopyFn copy_battle_data{};
    CopyFn copy_item{};
    FreeFn free_memory{};
    GetContainerClassFn get_container_class{};
    RequestPlayerProfilesFn request_player_profiles{};
    ApplyPlaybackContextFn apply_playback_context{};
    InitializeProfileFn initialize_profile{};
    DestroyProfileFn destroy_profile{};
    CopyProfileFn copy_profile{};
};

NativeFunctions g_functions{};

struct FunctionContract
{
    std::uintptr_t rva;
    std::array<std::byte, 8> signature;
};

constexpr FunctionContract kContracts[]{
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
    {0x538580, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}}},
    {0x57e1b0, {std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
                std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}}},
    {0xd46a00, {std::byte{0x48}, std::byte{0x85}, std::byte{0xc9}, std::byte{0x74},
                std::byte{0x1d}, std::byte{0x4c}, std::byte{0x8b}, std::byte{0x05}}},
    {0xb77900, {std::byte{0x48}, std::byte{0x81}, std::byte{0xec}, std::byte{0x98},
                std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x48}}},
    {0x5e90c0, {std::byte{0x40}, std::byte{0x55}, std::byte{0x53}, std::byte{0x57},
                std::byte{0x41}, std::byte{0x54}, std::byte{0x48}, std::byte{0x8d}}},
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
        reinterpret_cast<CopyFn>(image_base + 0x538580),
        reinterpret_cast<CopyFn>(image_base + 0x57e1b0),
        reinterpret_cast<FreeFn>(image_base + 0xd46a00),
        reinterpret_cast<GetContainerClassFn>(image_base + 0xb77900),
        reinterpret_cast<RequestPlayerProfilesFn>(image_base + 0x5e90c0),
        reinterpret_cast<ApplyPlaybackContextFn>(image_base + 0x5e3010),
        reinterpret_cast<InitializeProfileFn>(image_base + 0x2dc0270),
        reinterpret_cast<DestroyProfileFn>(image_base + 0x4eeed0),
        reinterpret_cast<CopyProfileFn>(image_base + 0x4f1cf0)};
    return true;
}

ImportFailure ReplayPayloadImporter::Import(
    std::span<const std::byte> payload, ReplayMetadata& metadata) noexcept
{
    constexpr std::size_t kMaximumPayload = 64u * 1024u * 1024u;
    constexpr std::size_t kItemSize = 0x1a00;
    constexpr std::size_t kBattleData = 0xa0;
    constexpr std::size_t kContainerCurrentItem = 0x80;
    constexpr std::size_t kCurrentTarget = 0x40;
    constexpr std::size_t kTemporaryItem = 0x19e0;
    constexpr std::size_t kStageIndex = kBattleData + 0x98;
    constexpr std::size_t kLeftCharacter = kBattleData + 0xa0 + 0x28 + 0x08;
    constexpr std::size_t kRightCharacter = kBattleData + 0xcf0 + 0x28 + 0x08;
    metadata = {};
    ReleasePlaybackContext();
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
        std::memcpy(&metadata.stage_index, item.data() + kStageIndex,
                    sizeof(metadata.stage_index));
        std::memcpy(&metadata.left_character, item.data() + kLeftCharacter,
                    sizeof(metadata.left_character));
        std::memcpy(&metadata.right_character, item.data() + kRightCharacter,
                    sizeof(metadata.right_character));
        const std::int32_t map_index = metadata.stage_index > 0xff
            ? metadata.stage_index & 0xff : metadata.stage_index;
        if (metadata.stage_index < 0 || metadata.stage_index > 0xfff
            || map_index < 0 || map_index > 0xff
            || metadata.left_character == 0xff
            || metadata.right_character == 0xff)
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
            else
            {
                auto* bytes = static_cast<std::byte*>(save);
                const bool copied = SafeCall(false, [&]() {
                    g_functions.copy_battle_data(bytes + kCurrentTarget,
                                                 item.data() + kBattleData);
                    g_functions.copy_item(bytes + kTemporaryItem, item.data());
                    g_functions.copy_battle_data(
                        bytes + kTemporaryItem + kBattleData,
                        item.data() + kBattleData);
                    return true;
                });
                if (!copied) result = ImportFailure::CopyFailed;
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
                        g_functions.copy_battle_data(
                            container + kContainerCurrentItem + kBattleData,
                            item.data() + kBattleData);
                        return true;
                    });
                    if (!copied)
                    {
                        result = ImportFailure::PlaybackContextCopyFailed;
                        ReleasePlaybackContext();
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

bool ReplayPayloadImporter::ApplyPlaybackContext() noexcept
{
    if (playback_container_ == nullptr
        || g_functions.apply_playback_context == nullptr)
    {
        return false;
    }
    return SafeCall(false, [&]() {
        g_functions.apply_playback_context(playback_container_);
        return true;
    });
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
