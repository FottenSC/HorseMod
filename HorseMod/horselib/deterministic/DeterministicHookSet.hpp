#pragma once

#include "Types.hpp"
#include "FloatingPointEnvironment.hpp"
#include "NativeBatchTimeline.hpp"
#include "UcrtRandBroker.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

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
    std::uint64_t outer_batch_id{};
    std::uint32_t frame_counter{};
    std::uint32_t thread_id{};
    std::int32_t game_round{};
    std::int32_t game_time{};
    std::int32_t manager_game_round_cursor{};
    std::uint32_t manager_game_time_cursor{};
    std::uint32_t round_state_frame{};
    std::int32_t unpause_countdown{};
    PlayerInput inputs[2]{};
    PlayerInput pre_filter_inputs[2]{};
    std::uint8_t round_state{};
    std::uint8_t repeat_pending{};
    std::uint8_t pending_move_state{};
    std::uint16_t read_mask{};
    std::uint32_t input_filter_invocations{};
    bool input_filter_observed{};
};

struct ReplayExitObservation
{
    std::uintptr_t replay_state{};
    std::uint32_t thread_id{};
};

struct OuterTickState
{
    std::uintptr_t input_log{};
    std::uint32_t frame_counter{};
    std::int32_t input_game_round{};
    std::int32_t input_game_time{};
    std::int32_t manager_game_round_cursor{};
    std::uint32_t manager_game_time_cursor{};
    std::uint8_t main_state{};
    std::uint8_t round_state{};
};

struct OuterTickObservation
{
    std::uintptr_t battle_manager{};
    std::uint64_t batch_id{};
    std::uint32_t thread_id{};
    float delta_seconds{};
    OuterTickState before{};
    OuterTickState after{};
    FloatingPointEnvironment fp_before{};
    FloatingPointEnvironment fp_after{};
    std::uint32_t observed_coordinates{};
    std::uint32_t repeat_pending_coordinates{};
    std::uint32_t same_input_time_coordinates{};
    std::uint16_t read_mask{};
    bool fp_before_valid{};
    bool fp_after_valid{};
};

using FrameFencepostCallback = void (*)(
    void* user,
    const FrameFencepostObservation& observation) noexcept;
using ReplayExitCallback = void (*)(
    void* user,
    const ReplayExitObservation& observation) noexcept;
using OuterTickCallback = void (*)(
    void* user,
    const OuterTickObservation& observation) noexcept;

struct DeterministicHookCallbacks
{
    void* user{};
    FrameFencepostCallback frame_fencepost{};
    OuterTickCallback outer_tick_begin{};
    OuterTickCallback outer_tick{};
    ReplayExitCallback replay_exit{};
};

using OwnedBatchLandingCaptureFn = Status (*)(
    void* user, FrameCoordinate coordinate) noexcept;

struct OwnedBatchReplayRequest
{
    std::uintptr_t battle_manager{};
    std::uint32_t owner_thread_id{};
    const NativeBatchEnvelope* envelope{};
    std::span<const FrameCoordinate> coordinates{};
    std::span<const InputPair> inputs{};
    std::uint32_t landing_offset{UINT32_MAX};
    void* landing_user{};
    OwnedBatchLandingCaptureFn capture_landing{};
    bool suppress_ephemeral_presentation{};
};

struct OwnedBatchReplayResult
{
    FailureCode failure{FailureCode::None};
    OuterTickState before{};
    OuterTickState after{};
    std::uint32_t observed_coordinates{};
    std::uint32_t filter_invocations{};
    bool landing_captured{};
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
        DeterministicHookCallbacks callbacks,
        UcrtRandBroker* ucrt_broker = nullptr);
    void Uninstall() noexcept;

    [[nodiscard]] bool installed() const noexcept;
    Status ExecuteOwnedBatch(
        const OwnedBatchReplayRequest& request,
        OwnedBatchReplayResult& output) noexcept;

private:
    struct OwnedBatchExecution
    {
        const OwnedBatchReplayRequest* request{};
        OwnedBatchReplayResult* result{};
        std::uint32_t invocations_for_coordinate{};
    };

    struct OuterTickCaptureContext
    {
        OuterTickObservation* observation{};
        std::int32_t previous_game_round{};
        std::int32_t previous_game_time{};
        bool has_previous_coordinate{};
        PlayerInput pre_filter_inputs[2]{};
        PlayerInput post_filter_inputs[2]{};
        std::uint32_t input_filter_invocations{};
        bool input_filter_observed{};
        OwnedBatchExecution* owned{};
    };

    using FrameFencepostFn = void (__fastcall*)(void* battle_manager);
    using OuterTickFn = void (__fastcall*)(void* battle_manager, float delta_seconds);
    using ReplayPostTickFn = void (__fastcall*)(void* replay_state);
    using CallbackExecutorFn = void (__fastcall*)(
        void* collection, void* callback_argument);
    using StageBreakWallFn = void (__fastcall*)(void* actor, bool immediately);
    using StageBreakBarrierFn = void (__fastcall*)(void* actor, void* direction);
    using StageBreakDispatchFn = void (__fastcall*)(
        void* emitter, std::int32_t actor_id, void* location);
    using BattleAudioDispatchFn = std::int32_t (__fastcall*)(
        void* battle_manager, void* event_record, bool alternate_route);

    static void __fastcall FrameFencepostDetour(void* battle_manager) noexcept;
    static void __fastcall OuterTickDetour(
        void* battle_manager, float delta_seconds) noexcept;
    static void __fastcall ReplayPostTickDetour(void* replay_state) noexcept;
    static void __fastcall CallbackExecutorDetour(
        void* collection, void* callback_argument) noexcept;
    static void __fastcall StageBreakWallDetour(
        void* actor, bool immediately) noexcept;
    static void __fastcall StageBreakBarrierDetour(
        void* actor, void* direction) noexcept;
    static void __fastcall StageBreakDispatchDetour(
        void* emitter, std::int32_t actor_id, void* location) noexcept;
    static std::int32_t __fastcall BattleAudioDispatchDetour(
        void* battle_manager, void* event_record,
        bool alternate_route) noexcept;
    static int __cdecl UcrtRandDetour() noexcept;
    static void __cdecl UcrtSrandDetour(unsigned int seed) noexcept;
    void EmitFrameFencepost(void* battle_manager) noexcept;
    void CaptureOuterTickState(
        void* battle_manager,
        OuterTickState& state,
        std::uint16_t& read_mask,
        std::uint16_t frame_bit,
        std::uint16_t state_bit,
        std::uint16_t input_bit,
        std::uint16_t cursor_bit) noexcept;
    void EmitReplayExit(void* replay_state) noexcept;
    [[nodiscard]] bool OuterStateMatchesEnvelope(
        const OuterTickState& state,
        const NativeBatchEnvelope& envelope,
        bool before) const noexcept;
    bool InstallUcrtIatHooks() noexcept;
    void UninstallUcrtIatHooks() noexcept;
    void ClearState() noexcept;

    static std::atomic<DeterministicHookSet*> active_;
    static std::atomic<std::uint32_t> callbacks_in_flight_;
    static std::atomic<std::uint64_t> frame_fencepost_trampoline_global_;
    static std::atomic<std::uint64_t> outer_tick_trampoline_global_;
    static std::atomic<std::uint64_t> replay_post_tick_trampoline_global_;
    static std::atomic<std::uint64_t> callback_executor_trampoline_global_;
    static std::atomic<std::uint64_t> stage_break_wall_trampoline_global_;
    static std::atomic<std::uint64_t> stage_break_barrier_trampoline_global_;
    static std::atomic<std::uint64_t> stage_break_dispatch_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_dispatch_trampoline_global_;
    static thread_local OuterTickCaptureContext* active_outer_capture_;

    std::unique_ptr<PLH::x64Detour> frame_fencepost_detour_{};
    std::unique_ptr<PLH::x64Detour> replay_post_tick_detour_{};
    std::unique_ptr<PLH::x64Detour> outer_tick_detour_{};
    std::unique_ptr<PLH::x64Detour> callback_executor_detour_{};
    std::unique_ptr<PLH::x64Detour> stage_break_wall_detour_{};
    std::unique_ptr<PLH::x64Detour> stage_break_barrier_detour_{};
    std::unique_ptr<PLH::x64Detour> stage_break_dispatch_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_dispatch_detour_{};
    std::uint64_t frame_fencepost_trampoline_{};
    std::uint64_t replay_post_tick_trampoline_{};
    std::uint64_t outer_tick_trampoline_{};
    std::uint64_t callback_executor_trampoline_{};
    std::uint64_t stage_break_wall_trampoline_{};
    std::uint64_t stage_break_barrier_trampoline_{};
    std::uint64_t stage_break_dispatch_trampoline_{};
    std::uint64_t battle_audio_dispatch_trampoline_{};
    std::uint64_t next_outer_batch_id_{};
    std::uintptr_t image_base_{};
    std::uintptr_t rand_iat_slot_{};
    std::uintptr_t srand_iat_slot_{};
    UcrtRandFn original_rand_{};
    UcrtSrandFn original_srand_{};
    UcrtRandBroker* ucrt_broker_{};
    DeterministicHookCallbacks callbacks_{};
    std::atomic<bool> installed_{};
};
}
