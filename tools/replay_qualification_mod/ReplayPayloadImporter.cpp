#include "ReplayPayloadImporter.hpp"

#include <Windows.h>

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

bool ReplayPayloadImporter::Bind(std::uintptr_t image_base) noexcept
{
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
        reinterpret_cast<FreeFn>(image_base + 0xd46a00)};
    return true;
}

ImportFailure ReplayPayloadImporter::Import(
    std::span<const std::byte> payload) noexcept
{
    constexpr std::size_t kMaximumPayload = 64u * 1024u * 1024u;
    constexpr std::size_t kItemSize = 0x1a00;
    constexpr std::size_t kBattleData = 0xa0;
    constexpr std::size_t kCurrentTarget = 0x40;
    constexpr std::size_t kTemporaryItem = 0x19e0;
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
                g_functions.copy_battle_data(bytes + kTemporaryItem + kBattleData,
                                             item.data() + kBattleData);
                return true;
            });
            if (!copied) result = ImportFailure::CopyFailed;
        }
    }

    const bool destroyed = SafeCall(false, [&]() {
        g_functions.destroy(item.data());
        return true;
    });
    Release(decoded);
    return !destroyed && result == ImportFailure::None
        ? ImportFailure::DestroyFailed : result;
}
}
