#include "DeterministicHookSet.hpp"

#include "Schema.hpp"

#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>

#include <algorithm>
#include <cstring>
#include <intrin.h>
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
std::atomic<std::uint64_t>
    DeterministicHookSet::callback_executor_trampoline_global_{};
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

bool CaptureInputPairArray(void* argument, PlayerInput (&output)[2]) noexcept
{
    std::uintptr_t data{};
    std::int32_t count{};
    std::int32_t capacity{};
    const auto header = reinterpret_cast<std::uintptr_t>(argument);
    return header != 0 && SafeRead(header, data) && data != 0
        && SafeRead(header + 8, count) && count == 2
        && SafeRead(header + 12, capacity) && capacity >= count
        && SafeRead(data, output[0])
        && SafeRead(data + sizeof(PlayerInput), output[1]);
}
}

DeterministicHookSet::~DeterministicHookSet()
{
    Uninstall();
}

Status DeterministicHookSet::Install(
    std::uintptr_t image_base,
    DeterministicHookCallbacks callbacks,
    UcrtRandBroker* ucrt_broker)
{
    if (installed())
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    if (image_base == 0 || callbacks.frame_fencepost == nullptr
        || callbacks.outer_tick_begin == nullptr
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
            Schema::Sc6ReplayLayout::post_tick_signature.size())
        || !SafeEqual(
            reinterpret_cast<const void*>(
                image_base + Schema::Sc6FrameLayout::callback_executor_rva),
            Schema::Sc6FrameLayout::callback_executor_signature.data(),
            Schema::Sc6FrameLayout::callback_executor_signature.size()))
    {
        return Status::failure(FailureCode::AdapterUnqualified);
    }

    image_base_ = image_base;
    callbacks_ = callbacks;
    ucrt_broker_ = ucrt_broker;
    frame_fencepost_trampoline_ = 0;
    replay_post_tick_trampoline_ = 0;
    outer_tick_trampoline_ = 0;
    callback_executor_trampoline_ = 0;
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
    callback_executor_detour_ = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(
            image_base + Schema::Sc6FrameLayout::callback_executor_rva),
        reinterpret_cast<std::uint64_t>(&CallbackExecutorDetour),
        &callback_executor_trampoline_);
    if (!callback_executor_detour_->hook())
    {
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    callback_executor_trampoline_global_.store(
        callback_executor_trampoline_, std::memory_order_release);
    if (ucrt_broker_ != nullptr && !InstallUcrtIatHooks())
    {
        callback_executor_detour_->unHook();
        outer_tick_detour_->unHook();
        replay_post_tick_detour_->unHook();
        frame_fencepost_detour_->unHook();
        active_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        ClearState();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
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
    UninstallUcrtIatHooks();
    if (callback_executor_detour_)
    {
        callback_executor_detour_->unHook();
    }
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
    observation.batch_id = hooks != nullptr ? ++hooks->next_outer_batch_id_ : 0;
    observation.thread_id = ::GetCurrentThreadId();
    observation.delta_seconds = delta_seconds;
    observation.fp_before = CaptureFloatingPointEnvironment();
    observation.fp_before_valid = true;
    if (hooks != nullptr)
    {
        hooks->CaptureOuterTickState(
            battle_manager, observation.before, observation.read_mask,
            0x1, 0x2, 0x4, 0x8);
        hooks->callbacks_.outer_tick_begin(
            hooks->callbacks_.user, observation);
    }
    OuterTickCaptureContext capture_context{&observation};
    OuterTickCaptureContext* previous_capture = active_outer_capture_;
    active_outer_capture_ = &capture_context;
    if (original != nullptr)
    {
        original(battle_manager, delta_seconds);
    }
    active_outer_capture_ = previous_capture;
    observation.fp_after = CaptureFloatingPointEnvironment();
    observation.fp_after_valid = true;
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

void __fastcall DeterministicHookSet::CallbackExecutorDetour(
    void* collection, void* callback_argument) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->callback_executor_trampoline_
        : callback_executor_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<CallbackExecutorFn>(trampoline);
    auto* batch = active_outer_capture_;
    const bool is_input_filter = hooks != nullptr && batch != nullptr
        && batch->observation != nullptr
        && reinterpret_cast<std::uintptr_t>(collection)
            == batch->observation->battle_manager
                + Schema::Sc6FrameLayout::manager_input_filter_callbacks;
    PlayerInput before[2]{};
    const bool before_valid = is_input_filter
        && CaptureInputPairArray(callback_argument, before);
    if (original != nullptr) original(collection, callback_argument);
    PlayerInput after[2]{};
    const bool after_valid = before_valid
        && CaptureInputPairArray(callback_argument, after);
    if (after_valid)
    {
        std::copy(std::begin(before), std::end(before), batch->pre_filter_inputs);
        std::copy(std::begin(after), std::end(after), batch->post_filter_inputs);
        ++batch->input_filter_invocations;
        batch->input_filter_observed = true;
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

int __cdecl DeterministicHookSet::UcrtRandDetour() noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto original = hooks != nullptr ? hooks->original_rand_ : nullptr;
    int result{};
    if (hooks != nullptr && hooks->ucrt_broker_ != nullptr)
    {
        const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto return_rva = return_address >= hooks->image_base_
            ? return_address - hooks->image_base_ : 0;
        result = hooks->ucrt_broker_->HandleRand(
            ::GetCurrentThreadId(), return_rva, original);
    }
    else if (original != nullptr)
    {
        result = original();
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __cdecl DeterministicHookSet::UcrtSrandDetour(unsigned int seed) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto original = hooks != nullptr ? hooks->original_srand_ : nullptr;
    if (hooks != nullptr && hooks->ucrt_broker_ != nullptr)
    {
        const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto return_rva = return_address >= hooks->image_base_
            ? return_address - hooks->image_base_ : 0;
        hooks->ucrt_broker_->HandleSrand(
            ::GetCurrentThreadId(), return_rva, seed, original);
    }
    else if (original != nullptr)
    {
        original(seed);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

bool DeterministicHookSet::InstallUcrtIatHooks() noexcept
{
    rand_iat_slot_ = image_base_ + Schema::Sc6UcrtLayout::rand_iat_rva;
    srand_iat_slot_ = image_base_ + Schema::Sc6UcrtLayout::srand_iat_rva;
    if (!SafeRead(rand_iat_slot_, original_rand_)
        || !SafeRead(srand_iat_slot_, original_srand_)
        || original_rand_ == nullptr || original_srand_ == nullptr)
    {
        return false;
    }
    DWORD old_protect{};
    if (!::VirtualProtect(reinterpret_cast<void*>(rand_iat_slot_), sizeof(void*),
            PAGE_READWRITE, &old_protect))
        return false;
    auto* rand_slot = reinterpret_cast<void* volatile*>(rand_iat_slot_);
    const auto prior_rand = ::InterlockedCompareExchangePointer(
        rand_slot, reinterpret_cast<void*>(&UcrtRandDetour),
        reinterpret_cast<void*>(original_rand_));
    DWORD ignored{};
    ::VirtualProtect(reinterpret_cast<void*>(rand_iat_slot_), sizeof(void*),
        old_protect, &ignored);
    if (prior_rand != reinterpret_cast<void*>(original_rand_)) return false;

    if (!::VirtualProtect(reinterpret_cast<void*>(srand_iat_slot_), sizeof(void*),
            PAGE_READWRITE, &old_protect))
    {
        UninstallUcrtIatHooks();
        return false;
    }
    auto* srand_slot = reinterpret_cast<void* volatile*>(srand_iat_slot_);
    const auto prior_srand = ::InterlockedCompareExchangePointer(
        srand_slot, reinterpret_cast<void*>(&UcrtSrandDetour),
        reinterpret_cast<void*>(original_srand_));
    ::VirtualProtect(reinterpret_cast<void*>(srand_iat_slot_), sizeof(void*),
        old_protect, &ignored);
    if (prior_srand != reinterpret_cast<void*>(original_srand_))
    {
        UninstallUcrtIatHooks();
        return false;
    }
    return true;
}

void DeterministicHookSet::UninstallUcrtIatHooks() noexcept
{
    const auto restore = [](std::uintptr_t slot, void* hook, void* original) {
        if (slot == 0 || original == nullptr) return;
        DWORD old_protect{};
        if (!::VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*),
                PAGE_READWRITE, &old_protect)) return;
        ::InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(slot), original, hook);
        DWORD ignored{};
        ::VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*),
            old_protect, &ignored);
    };
    restore(srand_iat_slot_, reinterpret_cast<void*>(&UcrtSrandDetour),
        reinterpret_cast<void*>(original_srand_));
    restore(rand_iat_slot_, reinterpret_cast<void*>(&UcrtRandDetour),
        reinterpret_cast<void*>(original_rand_));
    srand_iat_slot_ = 0;
    rand_iat_slot_ = 0;
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
        observation.outer_batch_id = batch->observation->batch_id;
        observation.input_filter_observed = batch->input_filter_observed;
        observation.input_filter_invocations = batch->input_filter_invocations;
        std::copy(std::begin(batch->pre_filter_inputs),
            std::end(batch->pre_filter_inputs), observation.pre_filter_inputs);
        if (batch->input_filter_observed
            && (batch->post_filter_inputs[0] != observation.inputs[0]
                || batch->post_filter_inputs[1] != observation.inputs[1]))
        {
            observation.input_filter_observed = false;
        }
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
    callback_executor_detour_.reset();
    outer_tick_detour_.reset();
    replay_post_tick_detour_.reset();
    frame_fencepost_detour_.reset();
    replay_post_tick_trampoline_ = 0;
    frame_fencepost_trampoline_ = 0;
    outer_tick_trampoline_ = 0;
    callback_executor_trampoline_ = 0;
    next_outer_batch_id_ = 0;
    replay_post_tick_trampoline_global_.store(0, std::memory_order_release);
    frame_fencepost_trampoline_global_.store(0, std::memory_order_release);
    outer_tick_trampoline_global_.store(0, std::memory_order_release);
    callback_executor_trampoline_global_.store(0, std::memory_order_release);
    rand_iat_slot_ = 0;
    srand_iat_slot_ = 0;
    image_base_ = 0;
    original_rand_ = nullptr;
    original_srand_ = nullptr;
    ucrt_broker_ = nullptr;
    callbacks_ = {};
}
}
