#pragma once

#include "NativeBinding.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace Horse::Deterministic
{
// Observes (but never owns) the scene-owned LuxOnlineBattleSync object at its
// native initialization boundary. The object is not reliably discoverable by
// UObject name: the channel-6 receiver is registered against this exact
// pointer through FWeakObjectPtr during scene construction.
class Sc6BattleSyncOwnerHook
{
public:
    // Ghidra: InitializeLuxOnlineBattleSyncReceivers @ 0x14050DD60.
    static constexpr std::uintptr_t initializer_rva = 0x0050dd60;
    static constexpr std::array<std::byte, 16> expected_prologue{{
        std::byte{0x4c}, std::byte{0x8b}, std::byte{0xdc}, std::byte{0x55},
        std::byte{0x53}, std::byte{0x57}, std::byte{0x49}, std::byte{0x8d},
        std::byte{0x6b}, std::byte{0xa1}, std::byte{0x48}, std::byte{0x81},
        std::byte{0xec}, std::byte{0xf0}, std::byte{0x00}, std::byte{0x00}}};

    static Sc6BattleSyncOwnerHook& instance() noexcept
    {
        static Sc6BattleSyncOwnerHook value;
        return value;
    }

    bool install() noexcept
    {
        if (installed_.load(std::memory_order_acquire)) return true;
        const auto image_base = Horse::NativeBinding::imageBase();
        if (image_base == 0) return false;
        const auto target = image_base + initializer_rva;
        if (!matches_expected_prologue(target))
        {
            RC::Output::send<RC::LogLevel::Error>(STR(
                "[HorseMod] BattleSync owner hook signature mismatch "
                "at 0x{:X}; online observation remains fail-closed\n"),
                target);
            return false;
        }

        trampoline_ = 0;
        detour_ = std::make_unique<PLH::x64Detour>(
            static_cast<std::uint64_t>(target),
            reinterpret_cast<std::uint64_t>(&detour), &trampoline_);
        if (!detour_->hook())
        {
            detour_.reset();
            trampoline_ = 0;
            RC::Output::send<RC::LogLevel::Error>(STR(
                "[HorseMod] BattleSync owner hook installation failed; "
                "online observation remains fail-closed\n"));
            return false;
        }
        installed_.store(true, std::memory_order_release);
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[HorseMod] BattleSync owner hook installed target=0x{:X}\n"),
            target);
        return true;
    }

    void uninstall() noexcept
    {
        clear();
        if (!installed_.exchange(false, std::memory_order_acq_rel)) return;
        if (detour_)
        {
            detour_->unHook();
            detour_.reset();
        }
        trampoline_ = 0;
    }

    void clear() noexcept
    {
        current_.store(nullptr, std::memory_order_release);
        lifecycle_epoch_.fetch_add(1, std::memory_order_acq_rel);
    }

    [[nodiscard]] void* current() const noexcept
    {
        return current_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t lifecycle_epoch() const noexcept
    {
        return lifecycle_epoch_.load(std::memory_order_acquire);
    }

private:
    Sc6BattleSyncOwnerHook() = default;
    ~Sc6BattleSyncOwnerHook() { uninstall(); }
    Sc6BattleSyncOwnerHook(const Sc6BattleSyncOwnerHook&) = delete;
    Sc6BattleSyncOwnerHook& operator=(const Sc6BattleSyncOwnerHook&) = delete;

    // Keep SEH in a scalar-only leaf: MSVC rejects __try in install(), which
    // owns a unique_ptr and therefore requires C++ unwinding.
    static bool matches_expected_prologue(std::uintptr_t target) noexcept
    {
#if defined(_MSC_VER)
        __try
        {
#endif
            return std::memcmp(reinterpret_cast<const void*>(target),
                expected_prologue.data(), expected_prologue.size()) == 0;
#if defined(_MSC_VER)
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#endif
    }

    static void __fastcall detour(void* battle_sync) noexcept
    {
        auto& self = instance();
        using Fn = void(__fastcall*)(void*);
        const auto original = reinterpret_cast<Fn>(self.trampoline_);
        if (original != nullptr) original(battle_sync);
        if (battle_sync == nullptr) return;
        self.current_.store(battle_sync, std::memory_order_release);
        const auto epoch = self.lifecycle_epoch_.load(
            std::memory_order_acquire);
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[HorseMod] BattleSync owner observed object=0x{:X} epoch={}\n"),
            reinterpret_cast<std::uintptr_t>(battle_sync), epoch);
    }

    std::unique_ptr<PLH::x64Detour> detour_{};
    std::uint64_t trampoline_{};
    std::atomic<void*> current_{};
    std::atomic<std::uint64_t> lifecycle_epoch_{1};
    std::atomic<bool> installed_{};
};
}
