#pragma once

#include "Types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace PLH
{
class x64Detour;
}

namespace Horse::Deterministic
{
struct FrameFencepostObservation
{
    std::uintptr_t battle_manager{};
    std::uint32_t frame_counter{};
    std::uint32_t thread_id{};
    std::uint8_t round_state{};
    std::uint8_t repeat_pending{};
};

using FrameFencepostCallback = void (*)(
    void* user,
    const FrameFencepostObservation& observation) noexcept;

struct DeterministicHookCallbacks
{
    void* user{};
    FrameFencepostCallback frame_fencepost{};
};

class DeterministicHookSet final
{
public:
    DeterministicHookSet() noexcept = default;
    ~DeterministicHookSet();

    DeterministicHookSet(const DeterministicHookSet&) = delete;
    DeterministicHookSet& operator=(const DeterministicHookSet&) = delete;

    Status Install(
        std::uintptr_t image_base,
        DeterministicHookCallbacks callbacks);
    void Uninstall() noexcept;

    [[nodiscard]] bool installed() const noexcept;

private:
    using FrameFencepostFn = void (__fastcall*)(void* battle_manager);

    static void __fastcall FrameFencepostDetour(void* battle_manager) noexcept;
    void EmitFrameFencepost(void* battle_manager) noexcept;

    static std::atomic<DeterministicHookSet*> active_;
    static std::atomic<std::uint32_t> callbacks_in_flight_;
    static std::atomic<std::uint64_t> frame_fencepost_trampoline_global_;

    std::unique_ptr<PLH::x64Detour> frame_fencepost_detour_{};
    std::uint64_t frame_fencepost_trampoline_{};
    std::uintptr_t image_base_{};
    DeterministicHookCallbacks callbacks_{};
    std::atomic<bool> installed_{};
};
}
