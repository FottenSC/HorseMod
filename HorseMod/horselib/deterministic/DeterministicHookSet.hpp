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
    std::uintptr_t input_log{};
    std::uintptr_t input_pair_array{};
    std::uint32_t frame_counter{};
    std::uint32_t thread_id{};
    std::int32_t game_round{};
    std::int32_t game_time{};
    PlayerInput inputs[2]{};
    std::uint8_t round_state{};
    std::uint8_t repeat_pending{};
    std::uint16_t read_mask{};
};

struct ReplayExitObservation
{
    std::uintptr_t replay_state{};
    std::uint32_t thread_id{};
};

using FrameFencepostCallback = void (*)(
    void* user,
    const FrameFencepostObservation& observation) noexcept;
using ReplayExitCallback = void (*)(
    void* user,
    const ReplayExitObservation& observation) noexcept;

struct DeterministicHookCallbacks
{
    void* user{};
    FrameFencepostCallback frame_fencepost{};
    ReplayExitCallback replay_exit{};
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
    using ReplayPostTickFn = void (__fastcall*)(void* replay_state);

    static void __fastcall FrameFencepostDetour(void* battle_manager) noexcept;
    static void __fastcall ReplayPostTickDetour(void* replay_state) noexcept;
    void EmitFrameFencepost(void* battle_manager) noexcept;
    void EmitReplayExit(void* replay_state) noexcept;
    void ClearState() noexcept;

    static std::atomic<DeterministicHookSet*> active_;
    static std::atomic<std::uint32_t> callbacks_in_flight_;
    static std::atomic<std::uint64_t> frame_fencepost_trampoline_global_;
    static std::atomic<std::uint64_t> replay_post_tick_trampoline_global_;

    std::unique_ptr<PLH::x64Detour> frame_fencepost_detour_{};
    std::unique_ptr<PLH::x64Detour> replay_post_tick_detour_{};
    std::uint64_t frame_fencepost_trampoline_{};
    std::uint64_t replay_post_tick_trampoline_{};
    std::uintptr_t image_base_{};
    DeterministicHookCallbacks callbacks_{};
    std::atomic<bool> installed_{};
};
}
