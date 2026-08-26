#pragma once

#include "Types.hpp"
#include "BattleAudioSelectorState.hpp"
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
    NativeInputCacheRowImage source_rows[2]{};
    std::int32_t input_update_time{};
    std::uint8_t round_state{};
    std::uint8_t repeat_pending{};
    std::uint8_t pending_move_state{};
    std::uint16_t read_mask{};
    std::uint32_t input_filter_invocations{};
    bool input_filter_observed{};
    bool source_rows_observed{};
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
    std::uint32_t stage_wall_calls{};
    std::uint64_t stage_wall_hash{};
    std::uint32_t stage_barrier_calls{};
    std::uint64_t stage_barrier_hash{};
    std::uint32_t stage_dispatch_calls{};
    std::uint64_t stage_dispatch_hash{};
    std::uint32_t stage_signature_failures{};
    std::uint32_t battle_audio_dispatches{};
    std::uint64_t battle_audio_sequence_hash{};
    std::uint32_t battle_audio_route_hash{};
    std::uint32_t battle_audio_payload_hash{};
    std::uint32_t battle_audio_position_hash{};
    std::uint32_t battle_audio_direct_dispatches{};
    std::uint32_t battle_audio_direct_route_hash{};
    std::uint32_t battle_audio_direct_payload_hash{};
    std::uint32_t battle_audio_direct_position_hash{};
    std::uint64_t battle_audio_direct_sequence_hash{};
    std::uint32_t battle_audio_remap_calls{};
    std::uint64_t battle_audio_remap_hash{};
    std::uint32_t battle_audio_source_calls{};
    std::uint64_t battle_audio_source_hash{};
    std::uint32_t battle_audio_stop_all_calls{};
    std::uint64_t battle_audio_stop_all_hash{};
    std::uint32_t particle_spawn_calls{};
    std::uint64_t particle_spawn_hash{};
    std::uint32_t particle_signature_failures{};
    std::uint64_t camera_publication_hash{};
    CameraPublicationState camera_publication{};
    std::uint32_t camera_signature_failures{};
    std::array<std::uint8_t, maximum_battle_audio_handlers>
        battle_audio_remap_entry_values{};
    std::uint8_t battle_audio_remap_entry_mask{};
    std::uint32_t battle_audio_signature_failures{};
    std::array<BattleAudioDispatchJournalEntry,
        maximum_battle_audio_journal_dispatches> battle_audio_journal{};
    std::array<BattleAudioSourceJournalEntry,
        maximum_battle_audio_journal_sources> battle_audio_source_journal{};
    std::array<BattleAudioRemapJournalEntry,
        maximum_battle_audio_journal_remaps> battle_audio_remap_journal{};
    std::uint8_t battle_audio_journal_count{};
    std::uint8_t battle_audio_source_journal_count{};
    std::uint8_t battle_audio_remap_journal_count{};
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
    OuterTickCallback outer_tick_prepare{};
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
    std::uint32_t suppressed_stage_wall_calls{};
    std::uint32_t suppressed_stage_barrier_calls{};
    std::uint32_t semantic_stage_dispatch_calls{};
    std::uint64_t stage_wall_hash{};
    std::uint64_t stage_barrier_hash{};
    std::uint64_t stage_dispatch_hash{};
    std::uint32_t stage_signature_failures{};
    std::uint32_t suppressed_audio_calls{};
    std::uint64_t suppressed_audio_sequence_hash{};
    std::uint32_t suppressed_audio_route_hash{};
    std::uint32_t suppressed_audio_payload_hash{};
    std::uint32_t suppressed_audio_position_hash{};
    std::uint32_t suppressed_audio_direct_dispatches{};
    std::uint64_t suppressed_audio_direct_sequence_hash{};
    std::uint32_t suppressed_audio_direct_route_hash{};
    std::uint32_t suppressed_audio_direct_payload_hash{};
    std::uint32_t suppressed_audio_direct_position_hash{};
    std::uint32_t suppressed_audio_remap_calls{};
    std::uint64_t suppressed_audio_remap_hash{};
    std::uint32_t suppressed_audio_source_calls{};
    std::uint64_t suppressed_audio_source_hash{};
    std::uint32_t suppressed_audio_stop_all_calls{};
    std::uint64_t suppressed_audio_stop_all_hash{};
    std::uint32_t suppressed_particle_spawn_calls{};
    std::uint64_t suppressed_particle_spawn_hash{};
    std::uint32_t suppressed_particle_finished_binds{};
    std::uint32_t unknown_particle_routes{};
    std::uint64_t camera_publication_hash{};
    CameraPublicationState camera_publication{};
    std::uint32_t camera_signature_failures{};
    std::uint32_t camera_publication_mismatches{};
    std::uint32_t camera_publication_difference_count{};
    std::uint32_t first_camera_publication_difference{UINT32_MAX};
    std::array<std::uint8_t, maximum_battle_audio_handlers>
        suppressed_audio_remap_entry_values{};
    std::uint8_t suppressed_audio_remap_entry_mask{};
    std::uint32_t audio_sequence_mismatches{};
    std::uint32_t audio_journal_failure_mask{};
    std::uint32_t presentation_failure_mask{};
    std::uint32_t presentation_failures{};
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
    [[nodiscard]] static std::uintptr_t ObservedBattleAudioHandler(
        std::size_t index) noexcept;
    [[nodiscard]] static bool BattleAudioHandlerOverflowed() noexcept;
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
    using BattleAudioRemapFn = std::int32_t (__fastcall*)(
        void* handler, std::int32_t contact_type);
    using BattleAudioContactHandlerFn = void (__fastcall*)(
        void* handler, void* event_record);
    using BattleAudioPhaseChangedFn = void (__fastcall*)(
        void* handler, void* phase_record);
    using BattleAudioTrackingRemoveFn = std::uint64_t (__fastcall*)(
        void* tracking_set, std::uint32_t key);
    using BattleAudioTrackingInsertFn = std::int32_t* (__fastcall*)(
        void* tracking_set, std::int32_t* index, void* pair,
        std::uint8_t* replaced);
    using BattleAudioTrackingRehashFn = void (__fastcall*)(void* tracking_set);
    using BattleAudioBlueprintPublishFn = void (__fastcall*)(
        void* handler, void* event_record);
    using BattleAudioRegisterVoiceFn = std::uint32_t (__fastcall*)(
        void* shared_player, std::uint32_t cue_id, std::int32_t pitch_shift,
        std::uint32_t flags);
    using BattleAudioAppendCommandFn = void (__fastcall*)(
        void* active_voice_owner, void* command_record);
    using BattleAudioStopAllFn = void (__fastcall*)(
        void* active_voice_owner, std::uint8_t immediate);
    using BattleAudioAppendParameterFn = void (__fastcall*)(
        void* shared_player, void* parameter_name, float value);
    using ParticleSpawnFn = void* (__fastcall*)(void* world_context,
        void* particle_system, const void* location, const void* rotation,
        const void* scale, bool auto_activate);
    using ParticleFinishedBindFn = void (__fastcall*)(void* delegate,
        void* owner, void* callback, std::uint64_t callback_name);

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
    static std::int32_t __fastcall BattleAudioRemapDetour(
        void* handler, std::int32_t contact_type) noexcept;
    static void __fastcall BattleAudioContactHandlerDetour(
        void* handler, void* event_record) noexcept;
    static void __fastcall BattleAudioPhaseChangedDetour(
        void* handler, void* phase_record) noexcept;
    static std::uint64_t __fastcall BattleAudioTrackingRemoveDetour(
        void* tracking_set, std::uint32_t key) noexcept;
    static std::int32_t* __fastcall BattleAudioTrackingInsertDetour(
        void* tracking_set, std::int32_t* index, void* pair,
        std::uint8_t* replaced) noexcept;
    static void __fastcall BattleAudioTrackingRehashDetour(
        void* tracking_set) noexcept;
    static void __fastcall BattleAudioBlueprintPublishDetour(
        void* handler, void* event_record) noexcept;
    static std::uint32_t __fastcall BattleAudioRegisterVoiceDetour(
        void* shared_player, std::uint32_t cue_id, std::int32_t pitch_shift,
        std::uint32_t flags) noexcept;
    static void __fastcall BattleAudioAppendCommandDetour(
        void* active_voice_owner, void* command_record) noexcept;
    static void __fastcall BattleAudioStopAllDetour(
        void* active_voice_owner, std::uint8_t immediate) noexcept;
    static void __fastcall BattleAudioAppendParameterDetour(
        void* shared_player, void* parameter_name, float value) noexcept;
    static void* __fastcall ParticleSpawnDetour(void* world_context,
        void* particle_system, const void* location, const void* rotation,
        const void* scale, bool auto_activate) noexcept;
    static void __fastcall ParticleFinishedBindDetour(void* delegate,
        void* owner, void* callback, std::uint64_t callback_name) noexcept;
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
    Status RestoreBattleAudioRemapEntry(
        const NativeBatchEnvelope& envelope,
        OwnedBatchReplayResult& output) noexcept;
    [[nodiscard]] static bool IsObservedBattleAudioTrackingSet(
        const void* tracking_set) noexcept;
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
    static std::atomic<std::uint64_t> battle_audio_remap_trampoline_global_;
    static std::atomic<std::uint64_t>
        battle_audio_contact_handler_trampoline_global_;
    static std::atomic<std::uint64_t>
        battle_audio_phase_changed_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_tracking_remove_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_tracking_insert_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_tracking_rehash_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_blueprint_publish_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_register_voice_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_append_command_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_stop_all_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_append_parameter_trampoline_global_;
    static std::atomic<std::uint64_t> particle_spawn_trampoline_global_;
    static std::atomic<std::uint64_t> particle_finished_bind_trampoline_global_;
    static std::array<std::atomic<std::uintptr_t>,
        maximum_battle_audio_handlers> observed_battle_audio_handlers_;
    static std::atomic<bool> battle_audio_handler_overflow_;
    static thread_local OuterTickCaptureContext* active_outer_capture_;

    std::unique_ptr<PLH::x64Detour> frame_fencepost_detour_{};
    std::unique_ptr<PLH::x64Detour> replay_post_tick_detour_{};
    std::unique_ptr<PLH::x64Detour> outer_tick_detour_{};
    std::unique_ptr<PLH::x64Detour> callback_executor_detour_{};
    std::unique_ptr<PLH::x64Detour> stage_break_wall_detour_{};
    std::unique_ptr<PLH::x64Detour> stage_break_barrier_detour_{};
    std::unique_ptr<PLH::x64Detour> stage_break_dispatch_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_dispatch_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_remap_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_contact_handler_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_phase_changed_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_tracking_remove_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_tracking_insert_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_tracking_rehash_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_blueprint_publish_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_register_voice_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_append_command_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_stop_all_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_append_parameter_detour_{};
    std::unique_ptr<PLH::x64Detour> particle_spawn_detour_{};
    std::unique_ptr<PLH::x64Detour> particle_finished_bind_detour_{};
    std::uint64_t frame_fencepost_trampoline_{};
    std::uint64_t replay_post_tick_trampoline_{};
    std::uint64_t outer_tick_trampoline_{};
    std::uint64_t callback_executor_trampoline_{};
    std::uint64_t stage_break_wall_trampoline_{};
    std::uint64_t stage_break_barrier_trampoline_{};
    std::uint64_t stage_break_dispatch_trampoline_{};
    std::uint64_t battle_audio_dispatch_trampoline_{};
    std::uint64_t battle_audio_remap_trampoline_{};
    std::uint64_t battle_audio_contact_handler_trampoline_{};
    std::uint64_t battle_audio_phase_changed_trampoline_{};
    std::uint64_t battle_audio_tracking_remove_trampoline_{};
    std::uint64_t battle_audio_tracking_insert_trampoline_{};
    std::uint64_t battle_audio_tracking_rehash_trampoline_{};
    std::uint64_t battle_audio_blueprint_publish_trampoline_{};
    std::uint64_t battle_audio_register_voice_trampoline_{};
    std::uint64_t battle_audio_append_command_trampoline_{};
    std::uint64_t battle_audio_stop_all_trampoline_{};
    std::uint64_t battle_audio_append_parameter_trampoline_{};
    std::uint64_t particle_spawn_trampoline_{};
    std::uint64_t particle_finished_bind_trampoline_{};
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
