#include "DeterministicHookSet.hpp"

#include "Schema.hpp"

#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>

#include <cstring>
#include <thread>

namespace Horse::Deterministic
{
std::atomic<DeterministicHookSet*> DeterministicHookSet::active_{};
std::atomic<std::uint32_t> DeterministicHookSet::callbacks_in_flight_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::frame_fencepost_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::outer_tick_trampoline_global_{};
std::atomic<std::uint64_t>
    DeterministicHookSet::replay_post_tick_trampoline_global_{};
thread_local DeterministicHookSet::OuterTickCaptureContext*
    DeterministicHookSet::active_outer_capture_{};

namespace
{
bool SafeEqual(const void* left, const void* right, std::size_t size) noexcept
{
    __try
    {
        return std::memcmp(left, right, size) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <typename T>
bool SafeRead(std::uintptr_t address, T& output) noexcept
{
    __try
    {
        std::memcpy(&output, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}

DeterministicHookSet::~DeterministicHookSet()
{
    Uninstall();
}

Status DeterministicHookSet::Install(
    std::uintptr_t image_base,
    DeterministicHookCallbacks callbacks)
{
    if (installed())
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    if (image_base == 0 || callbacks.frame_fencepost == nullptr
        || callbacks.outer_tick == nullptr || callbacks.replay_exit == nullptr
        || active_.load(std::memory_order_acquire) != nullptr)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }

    const std::uintptr_t frame_target =
        image_base + Schema::Sc6FrameLayout::landing_fencepost_rva;
    if (!SafeEqual(
            reinterpret_cast<const void*>(frame_target),
            Schema::Sc6FrameLayout::landing_fencepost_signature.data(),
            Schema::Sc6FrameLayout::landing_fencepost_signature.size())
        || !SafeEqual(
            reinterpret_cast<const void*>(
                image_base + Schema::Sc6FrameLayout::outer_tick_rva),
            Schema::Sc6FrameLayout::outer_tick_signature.data(),
            Schema::Sc6FrameLayout::outer_tick_signature.size())
        || !SafeEqual(
            reinterpret_cast<const void*>(
                image_base + Schema::Sc6ReplayLayout::post_tick_rva),
            Schema::Sc6ReplayLayout::post_tick_signature.data(),
            Schema::Sc6ReplayLayout::post_tick_signature.size()))
    {
        return Status::failure(FailureCode::AdapterUnqualified);
    }

    image_base_ = image_base;
    callbacks_ = callbacks;
    frame_fencepost_trampoline_ = 0;
    replay_post_tick_trampoline_ = 0;
    outer_tick_trampoline_ = 0;
    frame_fencepost_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(frame_target),
        reinterpret_cast<std::uint64_t>(&FrameFencepostDetour),
        &frame_fencepost_trampoline_);
    active_.store(this, std::memory_order_release);
    if (!frame_fencepost_detour_->hook())
    {
        active_.store(nullptr, std::memory_order_release);
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    frame_fencepost_trampoline_global_.store(
        frame_fencepost_trampoline_, std::memory_order_release);

    replay_post_tick_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(
            image_base + Schema::Sc6ReplayLayout::post_tick_rva),
        reinterpret_cast<std::uint64_t>(&ReplayPostTickDetour),
        &replay_post_tick_trampoline_);
    if (!replay_post_tick_detour_->hook())
    {
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    replay_post_tick_trampoline_global_.store(
        replay_post_tick_trampoline_, std::memory_order_release);

    outer_tick_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(
            image_base + Schema::Sc6FrameLayout::outer_tick_rva),
        reinterpret_cast<std::uint64_t>(&OuterTickDetour),
        &outer_tick_trampoline_);
    if (!outer_tick_detour_->hook())
    {
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    outer_tick_trampoline_global_.store(
        outer_tick_trampoline_, std::memory_order_release);
    installed_.store(true, std::memory_order_release);
    return Status::success();
}

void DeterministicHookSet::Uninstall() noexcept
{
    if (!installed_.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }
    // Hooks are removed in the reverse of their installation order.
    if (outer_tick_detour_)
    {
        outer_tick_detour_->unHook();
    }
    if (replay_post_tick_detour_)
    {
        replay_post_tick_detour_->unHook();
    }
    if (frame_fencepost_detour_)
    {
        frame_fencepost_detour_->unHook();
    }
    active_.store(nullptr, std::memory_order_release);
    while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
    }
    ClearState();
}

void __fastcall DeterministicHookSet::OuterTickDetour(
    void* battle_manager, float delta_seconds) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->outer_tick_trampoline_
        : outer_tick_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<OuterTickFn>(trampoline);
    OuterTickObservation observation{};
    observation.battle_manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    observation.thread_id = ::GetCurrentThreadId();
    observation.delta_seconds = delta_seconds;
    if (hooks != nullptr)
    {
        hooks->CaptureOuterTickState(
            battle_manager, observation.before, observation.read_mask,
            0x1, 0x2, 0x4, 0x8);
    }
    OuterTickCaptureContext capture_context{&observation};
    OuterTickCaptureContext* previous_capture = active_outer_capture_;
    active_outer_capture_ = &capture_context;
    if (original != nullptr)
    {
        original(battle_manager, delta_seconds);
    }
    active_outer_capture_ = previous_capture;
    if (hooks != nullptr)
    {
        hooks->CaptureOuterTickState(
            battle_manager, observation.after, observation.read_mask,
            0x10, 0x20, 0x40, 0x80);
        hooks->callbacks_.outer_tick(hooks->callbacks_.user, observation);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

bool DeterministicHookSet::installed() const noexcept
{
    return installed_.load(std::memory_order_acquire);
}

void __fastcall DeterministicHookSet::FrameFencepostDetour(
    void* battle_manager) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->frame_fencepost_trampoline_
        : frame_fencepost_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<FrameFencepostFn>(trampoline);
    if (original != nullptr)
    {
        original(battle_manager);
        if (hooks != nullptr)
        {
            hooks->EmitFrameFencepost(battle_manager);
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::ReplayPostTickDetour(
    void* replay_state) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->replay_post_tick_trampoline_
        : replay_post_tick_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<ReplayPostTickFn>(trampoline);
    std::uint32_t exit_guard = 1;
    if (hooks != nullptr && replay_state != nullptr
        && SafeRead(
            reinterpret_cast<std::uintptr_t>(replay_state)
                + Schema::Sc6ReplayLayout::exit_guard,
            exit_guard)
        && exit_guard == 0)
    {
        hooks->EmitReplayExit(replay_state);
    }
    if (original != nullptr)
    {
        original(replay_state);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void DeterministicHookSet::EmitFrameFencepost(void* battle_manager) noexcept
{
    FrameFencepostObservation observation{};
    observation.battle_manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    observation.thread_id = ::GetCurrentThreadId();
    if (SafeRead(
            image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
            observation.frame_counter))
    {
        observation.read_mask |= 0x1;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6ReplayLayout::manager_status,
            observation.round_state))
    {
        observation.read_mask |= 0x2;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_repeat_pending,
            observation.repeat_pending))
    {
        observation.read_mask |= 0x4;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_pending_move_state,
            observation.pending_move_state))
    {
        observation.read_mask |= 0x80;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_game_round_cursor,
            observation.manager_game_round_cursor))
    {
        observation.read_mask |= 0x100;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_game_time_cursor,
            observation.manager_game_time_cursor))
    {
        observation.read_mask |= 0x200;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_round_state_frame,
            observation.round_state_frame))
    {
        observation.read_mask |= 0x400;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_unpause_countdown,
            observation.unpause_countdown))
    {
        observation.read_mask |= 0x800;
    }
    std::int32_t player_count{};
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager + Schema::Sc6FrameLayout::manager_input_log,
            observation.input_log)
        && observation.input_log != 0
        && SafeRead(
            observation.input_log + Schema::Sc6FrameLayout::input_log_game_round,
            observation.game_round)
        && SafeRead(
            observation.input_log + Schema::Sc6FrameLayout::input_log_game_time,
            observation.game_time))
    {
        observation.read_mask |= 0x8;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_input_pair_array,
            observation.input_pair_array)
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_active_player_count,
            player_count)
        && observation.input_pair_array != 0 && player_count == 2)
    {
        observation.read_mask |= 0x10;
        if (SafeRead(observation.input_pair_array, observation.inputs[0]))
        {
            observation.read_mask |= 0x20;
        }
        if (SafeRead(
                observation.input_pair_array + sizeof(PlayerInput),
                observation.inputs[1]))
        {
            observation.read_mask |= 0x40;
        }
    }
    OuterTickCaptureContext* batch = active_outer_capture_;
    if (batch != nullptr && batch->observation != nullptr
        && batch->observation->battle_manager == observation.battle_manager)
    {
        if (batch->has_previous_coordinate
            && batch->previous_game_round == observation.game_round
            && batch->previous_game_time == observation.game_time)
        {
            ++batch->observation->same_input_time_coordinates;
        }
        batch->previous_game_round = observation.game_round;
        batch->previous_game_time = observation.game_time;
        batch->has_previous_coordinate = true;
        ++batch->observation->observed_coordinates;
        if (observation.repeat_pending != 0)
            ++batch->observation->repeat_pending_coordinates;
    }
    callbacks_.frame_fencepost(callbacks_.user, observation);
}

void DeterministicHookSet::CaptureOuterTickState(
    void* battle_manager,
    OuterTickState& state,
    std::uint16_t& read_mask,
    std::uint16_t frame_bit,
    std::uint16_t state_bit,
    std::uint16_t input_bit,
    std::uint16_t cursor_bit) noexcept
{
    const auto manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    if (SafeRead(
            image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
            state.frame_counter))
    {
        read_mask |= frame_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_main_state,
            state.main_state)
        && SafeRead(
            manager + Schema::Sc6ReplayLayout::manager_status,
            state.round_state))
    {
        read_mask |= state_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_input_log,
            state.input_log)
        && state.input_log != 0
        && SafeRead(
            state.input_log + Schema::Sc6FrameLayout::input_log_game_round,
            state.input_game_round)
        && SafeRead(
            state.input_log + Schema::Sc6FrameLayout::input_log_game_time,
            state.input_game_time))
    {
        read_mask |= input_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_game_round_cursor,
            state.manager_game_round_cursor)
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_game_time_cursor,
            state.manager_game_time_cursor))
    {
        read_mask |= cursor_bit;
    }
}

void DeterministicHookSet::EmitReplayExit(void* replay_state) noexcept
{
    const ReplayExitObservation observation{
        reinterpret_cast<std::uintptr_t>(replay_state),
        ::GetCurrentThreadId()};
    callbacks_.replay_exit(callbacks_.user, observation);
}

void DeterministicHookSet::ClearState() noexcept
{
    outer_tick_detour_.reset();
    replay_post_tick_detour_.reset();
    frame_fencepost_detour_.reset();
    replay_post_tick_trampoline_ = 0;
    frame_fencepost_trampoline_ = 0;
    outer_tick_trampoline_ = 0;
    replay_post_tick_trampoline_global_.store(0, std::memory_order_release);
    frame_fencepost_trampoline_global_.store(0, std::memory_order_release);
    outer_tick_trampoline_global_.store(0, std::memory_order_release);
    image_base_ = 0;
    callbacks_ = {};
}
}
