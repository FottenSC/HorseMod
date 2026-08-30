#pragma once

#include "Types.hpp"
#include "AuthoritativeInputGate.hpp"
#include "BattleAudioSelectorState.hpp"
#include "FloatingPointEnvironment.hpp"
#include "NativeBatchTimeline.hpp"
#include "StageBreakPresentationIdentity.hpp"
#include "StagePresentation.hpp"
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
    bool authoritative_input_requested{};
    bool authoritative_input_applied{};
    bool authoritative_input_round_barrier{};
    bool authoritative_input_failed_closed{};
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
    std::uint32_t gameplay_xorshift_draws{};
    std::uint64_t gameplay_xorshift_sequence_hash{};
    std::uint64_t gameplay_xorshift_known_callers{};
    std::uint32_t gameplay_xorshift_unknown_callers{};
    std::uint32_t gameplay_xorshift_weighted_draws{};
    std::uint32_t gameplay_xorshift_if_draws{};
    std::uint16_t gameplay_xorshift_weighted_source_mask{};
    std::uint16_t gameplay_xorshift_if_source_mask{};
    std::uint32_t movevm_transition_07_calls{};
    std::uint64_t movevm_transition_07_sequence_hash{};
    std::uint32_t movevm_transition_07_signature_failures{};
    std::uint32_t resolved_hit_calls{};
    std::uint64_t resolved_hit_sequence_hash{};
    std::uint32_t resolved_hit_signature_failures{};
    std::uint32_t tira_random_transition_calls{};
    std::uint64_t tira_random_transition_sequence_hash{};
    std::uint16_t tira_random_transition_source_mask{};
    std::uint8_t tira_random_transition_target_mask{};
    std::uint16_t tira_last_transition_target{};
    std::uint8_t tira_character_slot_mask{};
    std::array<std::uint16_t, 2> tira_state19_at_transition{};
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
    std::uint32_t audio_terminal_calls{};
    std::uint64_t audio_terminal_hash{};
    std::uint32_t battle_audio_blueprint_calls{};
    std::uint64_t battle_audio_blueprint_hash{};
    std::uint32_t particle_spawn_calls{};
    std::uint64_t particle_spawn_hash{};
    std::uint32_t particle_signature_failures{};
    std::uint64_t camera_publication_hash{};
    CameraPublicationState camera_publication{};
    std::uint32_t camera_signature_failures{};
    std::uint64_t presentation_order_hash{};
    std::uint32_t presentation_order_failures{};
    std::array<std::uint8_t, maximum_battle_audio_handlers>
        battle_audio_remap_entry_values{};
    std::uint8_t battle_audio_remap_entry_mask{};
    std::uint32_t battle_audio_signature_failures{};
    std::uint32_t battle_audio_signature_failure_mask{};
    std::uintptr_t first_unresolved_audio_owner{};
    std::uintptr_t first_unresolved_audio_return_rva{};
    std::uint64_t audio_owner_graph_epoch{};
    std::uint32_t audio_owner_graph_bindings{};
    std::uint32_t audio_owner_graph_failure_stage{};
    std::array<BattleAudioDispatchJournalEntry,
        maximum_battle_audio_journal_dispatches> battle_audio_journal{};
    std::array<BattleAudioSourceJournalEntry,
        maximum_battle_audio_journal_sources> battle_audio_source_journal{};
    std::array<BattleAudioRemapJournalEntry,
        maximum_battle_audio_journal_remaps> battle_audio_remap_journal{};
    std::array<BattleAudioBlueprintJournalEntry,
        maximum_battle_audio_blueprint_journal_events>
        battle_audio_blueprint_journal{};
    std::array<BattleAudioStopAllJournalEntry,
        maximum_battle_audio_stop_all_journal_events>
        battle_audio_stop_all_journal{};
    std::array<AudioTerminalEvent, maximum_audio_terminal_journal_events>
        audio_terminal_journal{};
    // Diagnostic-only native caller RVAs. These identify the authoritative
    // source route without becoming part of the ordered terminal identity.
    std::array<std::uint32_t, maximum_audio_terminal_journal_events>
        audio_terminal_return_rvas{};
    // Diagnostic-only process-local CRI table slots. Ordered canonical
    // identity stays in audio_terminal_journal as an authored cue family.
    std::array<std::uint32_t, maximum_audio_terminal_journal_events>
        audio_terminal_raw_cue_sheet_ids{};
    std::array<StagePresentationJournalEntry,
        maximum_stage_presentation_journal_events> stage_wall_journal{};
    std::array<StagePresentationJournalEntry,
        maximum_stage_presentation_journal_events> stage_barrier_journal{};
    std::array<StagePresentationJournalEntry,
        maximum_stage_presentation_journal_events> stage_dispatch_journal{};
    std::array<ParticleSpawnJournalEntry,
        maximum_particle_presentation_journal_events> particle_spawn_journal{};
    std::array<PresentationOrderEntry, maximum_presentation_order_events>
        presentation_order_journal{};
    std::uint8_t battle_audio_journal_count{};
    std::uint8_t battle_audio_source_journal_count{};
    std::uint8_t battle_audio_remap_journal_count{};
    std::uint8_t battle_audio_blueprint_journal_count{};
    std::uint8_t battle_audio_stop_all_journal_count{};
    std::uint8_t audio_terminal_journal_count{};
    std::array<std::uintptr_t, maximum_battle_audio_stop_all_journal_events>
        battle_audio_stop_all_owner_identities{};
    std::uint8_t battle_audio_stop_all_owner_identity_count{};
    std::uint8_t stage_wall_journal_count{};
    std::uint8_t stage_barrier_journal_count{};
    std::uint8_t stage_dispatch_journal_count{};
    std::uint8_t particle_spawn_journal_count{};
    std::uint8_t presentation_order_journal_count{};
    // Qualification-only typed source event marker. Bit 0 replays the first
    // wall terminal; bit 1 replays the first barrier terminal during owned
    // execution. Ordinary gameplay envelopes always leave this zero.
    std::uint8_t qualification_stage_terminal_mask{};
    std::uint16_t read_mask{};
    bool authoritative_input_requested{};
    bool authoritative_input_applied{};
    bool authoritative_input_round_barrier{};
    bool authoritative_input_failed_closed{};
    // Set only when the live outer tick was unwound before any native input
    // consumer could run. This is distinct from a post-frame failure.
    bool authoritative_input_aborted_before_consume{};
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

inline void DispatchCompletedOuterTick(
    void* user, OuterTickCallback callback,
    const OuterTickObservation& observation) noexcept
{
    if (callback != nullptr) callback(user, observation);
}

using AuthoritativeInputCallback = AuthoritativeInputDisposition (*)(
    void* user,
    const OuterTickObservation& observation,
    bool stock_valid,
    const PlayerInput (&stock)[2],
    PlayerInput (&authoritative)[2]) noexcept;
using AuthoritativeInputCommitCallback = bool (*)(void* user) noexcept;

struct DeterministicHookCallbacks
{
    void* user{};
    FrameFencepostCallback frame_fencepost{};
    OuterTickCallback outer_tick_prepare{};
    OuterTickCallback outer_tick_begin{};
    // Runs after active_outer_capture_ is published and before the stock
    // battle tick. Qualification-only source events must enter here so their
    // native presentation terminals are journaled on an authoritative frame.
    OuterTickCallback outer_tick_source{};
    OuterTickCallback outer_tick{};
    ReplayExitCallback replay_exit{};
    AuthoritativeInputCallback authoritative_input{};
    AuthoritativeInputCommitCallback authoritative_input_commit{};
};

using OwnedBatchLandingCaptureFn = Status (*)(
    void* user, FrameCoordinate coordinate) noexcept;
using OwnedBatchCoordinateCaptureFn = Status (*)(
    void* user, FrameCoordinate coordinate, std::uint32_t index) noexcept;

enum class OwnedBatchPresentationMode : std::uint8_t
{
    VerifyRecorded,
    CaptureCorrected,
};

struct OwnedBatchReplayRequest
{
    std::uintptr_t battle_manager{};
    std::uint32_t owner_thread_id{};
    const NativeBatchEnvelope* envelope{};
    std::span<const FrameCoordinate> coordinates{};
    std::span<const InputPair> inputs{};
    std::span<InputPair> corrected_inputs{};
    OuterTickObservation* corrected_observation{};
    std::uint32_t landing_offset{UINT32_MAX};
    void* landing_user{};
    OwnedBatchLandingCaptureFn capture_landing{};
    void* coordinate_capture_user{};
    OwnedBatchCoordinateCaptureFn capture_coordinate{};
    bool suppress_ephemeral_presentation{};
    OwnedBatchPresentationMode presentation_mode{
        OwnedBatchPresentationMode::VerifyRecorded};
};

struct OwnedBatchReplayResult
{
    FailureCode failure{FailureCode::None};
    OuterTickState before{};
    OuterTickState after{};
    std::uint32_t observed_coordinates{};
    std::uint32_t filter_invocations{};
    std::uint32_t validation_difference_mask{};
    std::uint32_t gameplay_xorshift_draws{};
    std::uint64_t gameplay_xorshift_sequence_hash{};
    std::uint64_t gameplay_xorshift_known_callers{};
    std::uint32_t gameplay_xorshift_unknown_callers{};
    std::uint32_t gameplay_xorshift_weighted_draws{};
    std::uint32_t gameplay_xorshift_if_draws{};
    std::uint16_t gameplay_xorshift_weighted_source_mask{};
    std::uint16_t gameplay_xorshift_if_source_mask{};
    std::uint32_t movevm_transition_07_calls{};
    std::uint64_t movevm_transition_07_sequence_hash{};
    std::uint32_t movevm_transition_07_signature_failures{};
    std::uint32_t resolved_hit_calls{};
    std::uint64_t resolved_hit_sequence_hash{};
    std::uint32_t resolved_hit_signature_failures{};
    std::uint32_t tira_random_transition_calls{};
    std::uint64_t tira_random_transition_sequence_hash{};
    std::uint16_t tira_random_transition_source_mask{};
    std::uint8_t tira_random_transition_target_mask{};
    std::uint16_t tira_last_transition_target{};
    std::uint8_t tira_character_slot_mask{};
    std::array<std::uint16_t, 2> tira_state19_at_transition{};
    std::uint32_t suppressed_stage_wall_calls{};
    std::uint32_t suppressed_stage_barrier_calls{};
    std::uint32_t semantic_stage_dispatch_calls{};
    std::uint64_t stage_wall_hash{};
    std::uint64_t stage_barrier_hash{};
    std::uint64_t stage_dispatch_hash{};
    std::uint32_t stage_signature_failures{};
    std::uint32_t stage_journal_failure_mask{};
    std::uint32_t suppressed_audio_calls{};
    std::uint32_t discarded_audio_calls{};
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
    std::uint32_t suppressed_audio_terminal_calls{};
    std::uint64_t suppressed_audio_terminal_hash{};
    std::uint32_t suppressed_audio_blueprint_calls{};
    std::uint64_t suppressed_audio_blueprint_hash{};
    std::array<std::uintptr_t, maximum_battle_audio_stop_all_journal_events>
        suppressed_audio_stop_all_owner_identities{};
    std::uint8_t suppressed_audio_stop_all_owner_identity_count{};
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
    std::uint8_t expected_camera_publication_byte{};
    std::uint8_t observed_camera_publication_byte{};
    std::array<std::uint8_t, maximum_battle_audio_handlers>
        suppressed_audio_remap_entry_values{};
    std::uint8_t suppressed_audio_remap_entry_mask{};
    std::uint32_t audio_sequence_mismatches{};
    std::uint32_t audio_journal_failure_mask{};
    std::uint32_t presentation_failure_mask{};
    std::uint32_t presentation_failures{};
    std::uint64_t suppressed_presentation_order_hash{};
    std::uint32_t suppressed_presentation_order_events{};
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
    Status BindStageBreakPresentationIdentity(
        std::uint64_t generation,
        std::span<const StageBreakActorRef> actors,
        const StageBreakListenerTopology& topology,
        std::span<const StageBreakParticleAssetRef> assets) noexcept;
    void InvalidateStageBreakPresentationIdentity() noexcept;
    void InvalidateBattleAudioPresentationIdentity() noexcept;
    void SetBattleAudioPresentationGeneration(
        std::uint64_t generation) noexcept;
    Status MarkQualificationStageTerminal(std::uint32_t operation) noexcept;
    Status ResolveQualificationStageActor(
        std::uintptr_t actor, std::uint64_t& owner_logical_id) const noexcept;
    Status CommitAudioTerminal(
        const AudioTerminalEvent& event) noexcept;
    Status CommitAudioBlueprint(
        const AudioBlueprintPresentationValue& value) noexcept;
    Status CommitStagePresentation(
        const StagePresentationValue& value) noexcept;
    Status ArmPresentationCaptureForNextOuterTick() noexcept;

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
        std::uintptr_t frame_counter_address{};
        std::int32_t previous_game_round{};
        std::int32_t previous_game_time{};
        bool has_previous_coordinate{};
        PlayerInput pre_filter_inputs[2]{};
        PlayerInput post_filter_inputs[2]{};
        std::uint32_t input_filter_invocations{};
        bool input_filter_observed{};
        bool suppress_speculative_presentation{};
        OwnedBatchExecution* owned{};
    };

    Status ValidateOwnedBatchRequest(const OwnedBatchReplayRequest& request,
        OwnedBatchReplayResult& output,
        bool& capture_corrected) const noexcept;
    Status PrepareOwnedBatchState(const OwnedBatchReplayRequest& request,
        OwnedBatchReplayResult& output) noexcept;
    Status ExecuteQualificationStageTerminalIfRequested(
        const OwnedBatchReplayRequest& request,
        OwnedBatchReplayResult& output) noexcept;
    static void CopyObservedGameplayIdentity(
        const OuterTickObservation& observation,
        OwnedBatchReplayResult& output) noexcept;
    static bool OwnedGameplayIdentityMatches(
        const OwnedBatchReplayRequest& request,
        const OwnedBatchReplayResult& output) noexcept;
    Status ExecuteOwnedNativeTick(const OwnedBatchReplayRequest& request,
        OwnedBatchReplayResult& output,
        OuterTickObservation& observation,
        bool capture_corrected) noexcept;
    Status ValidateOwnedBatchResult(const OwnedBatchReplayRequest& request,
        OwnedBatchReplayResult& output,
        OuterTickObservation& observation,
        bool capture_corrected) noexcept;

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
        void* active_voice_owner, std::uint32_t cue_sheet_id,
        std::int32_t cue_id, std::uint32_t playback_flags);
    using BattleAudioResolveCharaCueFn = std::uint32_t (__fastcall*)(
        void* battle_audio_manager, const void* event,
        std::uint8_t cue_family);
    using BattleAudioAppendCommandFn = void (__fastcall*)(
        void* active_voice_owner, void* command_record);
    using BattleAudioStopAllFn = void (__fastcall*)(
        void* active_voice_owner, std::uint8_t immediate);
    using BattleAudioAppendParameterFn = void (__fastcall*)(
        void* shared_player, void* parameter_name, float value);
    using BattleAudioAppendOwnerParameterFn = void (__fastcall*)(
        void* active_voice_owner, void* parameter_name, float value);
    using ParticleSpawnFn = void* (__fastcall*)(void* world_context,
        void* particle_system, const void* location, const void* rotation,
        const void* scale, bool auto_activate);
    using ParticleFinishedBindFn = void (__fastcall*)(void* delegate,
        void* owner, void* callback, std::uint64_t callback_name);
    using GameplayXorshift96Fn = std::uint32_t (__fastcall*)();
    using MoveVmEvaluateIfFn = std::uint64_t (__fastcall*)(
        void* chara, std::int32_t argument_count,
        std::uint16_t* arguments);
    using MoveVmTransitionAuthor07Fn = void (__fastcall*)(
        void* chara, std::int32_t argument_count, std::uint16_t* arguments);
    using ResolvedHitConsumerFn = void (__fastcall*)();

    static void __fastcall FrameFencepostDetour(void* battle_manager) noexcept;
    static void __fastcall OuterTickDetour(
        void* battle_manager, float delta_seconds) noexcept;
    static bool InvokeOuterTickWithAbortGuard(
        OuterTickFn original, void* battle_manager,
        float delta_seconds) noexcept;
    [[noreturn]] static void AbortActiveOuterTick() noexcept;
    static void __fastcall ReplayPostTickDetour(void* replay_state) noexcept;
    static void __fastcall CallbackExecutorDetour(
        void* collection, void* callback_argument) noexcept;
    static void __fastcall StageBreakWallDetour(
        void* actor, bool immediately) noexcept;
    static void __fastcall StageBreakBarrierDetour(
        void* actor, void* direction) noexcept;
    static void SuppressStageWall(void* actor, bool immediately,
        StageBreakWallFn original, OuterTickCaptureContext* batch,
        const StagePresentationJournalEntry& semantic,
        bool semantic_ok) noexcept;
    static void SuppressStageBarrier(void* actor, void* direction,
        StageBreakBarrierFn original, OuterTickCaptureContext* batch,
        const StagePresentationJournalEntry& semantic,
        bool semantic_ok) noexcept;
    static void __fastcall StageBreakDispatchDetour(
        void* emitter, std::int32_t actor_id, void* location) noexcept;
    static std::int32_t __fastcall BattleAudioDispatchDetour(
        void* battle_manager, void* event_record,
        bool alternate_route) noexcept;
    static std::size_t ObserveBattleAudioDispatch(
        OuterTickCaptureContext* batch, void* event_record,
        bool alternate_route) noexcept;
    static std::size_t FindOrRegisterBattleAudioHandler(void* handler) noexcept;
    static std::int32_t __fastcall BattleAudioRemapDetour(
        void* handler, std::int32_t contact_type) noexcept;
    static void __fastcall BattleAudioContactHandlerDetour(
        void* handler, void* event_record) noexcept;
    static void ReplayRecordedBattleAudioSource(
        OuterTickCaptureContext* batch, void* handler,
        void* event_record) noexcept;
    static void ConsumeRecordedBattleAudioSourceSpan(
        OuterTickCaptureContext* batch, void* handler,
        const std::array<std::byte, 18>& semantic,
        const BattleAudioSourceJournalEntry& source) noexcept;
    static bool ConsumeRecordedBattleAudioRemaps(
        OuterTickCaptureContext* batch, std::uintptr_t handler_identity,
        std::size_t handler_slot, std::int32_t source_contact_type,
        const BattleAudioSourceJournalEntry& source) noexcept;
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
        void* active_voice_owner, std::uint32_t cue_sheet_id,
        std::int32_t cue_id, std::uint32_t playback_flags) noexcept;
    static std::uint32_t __fastcall BattleAudioResolveCharaCueDetour(
        void* battle_audio_manager, const void* event,
        std::uint8_t cue_family) noexcept;
    static void __fastcall BattleAudioAppendCommandDetour(
        void* active_voice_owner, void* command_record) noexcept;
    static void __fastcall BattleAudioStopAllDetour(
        void* active_voice_owner, std::uint8_t control) noexcept;
    static void __fastcall BattleAudioAppendParameterDetour(
        void* shared_player, void* parameter_name, float value) noexcept;
    static void* __fastcall ParticleSpawnDetour(void* world_context,
        void* particle_system, const void* location, const void* rotation,
        const void* scale, bool auto_activate) noexcept;
    static void __fastcall ParticleFinishedBindDetour(void* delegate,
        void* owner, void* callback, std::uint64_t callback_name) noexcept;
    static std::uint32_t __fastcall GameplayXorshift96Detour() noexcept;
    static std::uint64_t __fastcall MoveVmEvaluateIfDetour(
        void* chara, std::int32_t argument_count,
        std::uint16_t* arguments) noexcept;
    static void __fastcall MoveVmTransitionAuthor07Detour(
        void* chara, std::int32_t argument_count,
        std::uint16_t* arguments) noexcept;
    static void __fastcall ResolvedHitConsumerDetour() noexcept;
    static int __cdecl UcrtRandDetour() noexcept;
    static void __cdecl UcrtSrandDetour(unsigned int seed) noexcept;
    void EmitFrameFencepost(void* battle_manager) noexcept;
    void CaptureFencepostManagerState(
        void* battle_manager, FrameFencepostObservation& observation) noexcept;
    static void CaptureFencepostInputState(
        FrameFencepostObservation& observation) noexcept;
    void FinalizeFrameFencepost(
        FrameFencepostObservation& observation) noexcept;
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
    [[nodiscard]] bool ValidateInstallationSignatures(
        std::uintptr_t image_base) const noexcept;
    bool InstallDetour(std::unique_ptr<PLH::x64Detour>& storage,
        std::uintptr_t target, std::uintptr_t replacement,
        std::uint64_t& trampoline,
        std::atomic<std::uint64_t>& published_trampoline) noexcept;
    bool InstallFrameHooks() noexcept;
    bool InstallStageHooks() noexcept;
    bool InstallAudioHooks() noexcept;
    bool InstallParticleHooks() noexcept;
    bool InstallRandomHooks() noexcept;
    Status AbortInstallation() noexcept;
    Status RestoreBattleAudioRemapEntry(
        const NativeBatchEnvelope& envelope,
        OwnedBatchReplayResult& output) noexcept;
    Status ExecuteQualificationStageTerminal(
        const StagePresentationJournalEntry& event,
        bool wall, std::uintptr_t actor) noexcept;
    bool CompleteBattleAudioJournal(
        const NativeBatchEnvelope& envelope,
        OwnedBatchReplayResult& output) noexcept;
    bool ConsumeBattleAudioSource(
        const NativeBatchEnvelope& envelope,
        const BattleAudioSourceJournalEntry& source,
        OwnedBatchReplayResult& output) noexcept;
    static bool ConsumeDirectAudioBlueprintsUntil(
        const NativeBatchEnvelope& envelope,
        std::size_t target,
        OwnedBatchReplayResult& output) noexcept;
    static bool ConsumeAudioTerminalsUntil(
        const NativeBatchEnvelope& envelope,
        std::size_t target,
        OwnedBatchReplayResult& output) noexcept;
    static void ObserveMoveVmTransition(void* chara,
        std::int32_t argument_count, std::uint16_t* arguments,
        OuterTickCaptureContext& batch) noexcept;
    static void ObserveTiraRandomTransition(void* chara,
        std::uint16_t target, OuterTickCaptureContext& batch,
        OuterTickObservation& observation) noexcept;
    [[nodiscard]] bool PrepareAudioOwnerGraph(
        std::uintptr_t battle_manager,
        std::uintptr_t battle_audio_manager_override = 0) noexcept;
    void ClearAudioOwnerGraph() noexcept;
    [[nodiscard]] bool ResolveAudioOwner(
        std::uintptr_t owner, AudioOwnerSelector& selector) noexcept;
    [[nodiscard]] bool ResolveBattleCharaCueFamilyIdentity(
        std::uint32_t cue_sheet_slot, std::uint32_t& identity) noexcept;
    [[nodiscard]] bool ResolveBattleCharaCueSheetSlot(
        std::uint32_t identity, std::uint32_t& cue_sheet_slot) noexcept;
    static bool RecordAudioTerminal(
        OuterTickCaptureContext* batch,
        const AudioTerminalEvent& event,
        std::uint32_t return_rva = 0,
        std::uint32_t raw_cue_sheet_id = 0) noexcept;
    [[nodiscard]] static bool IsOwnedPresentationSuppressed(
        const OuterTickCaptureContext* batch) noexcept;
    [[nodiscard]] static bool IsPresentationSuppressed(
        const OuterTickCaptureContext* batch) noexcept;
    [[nodiscard]] static bool IsOwnedPresentationVerification(
        const OuterTickCaptureContext* batch) noexcept;
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
    static std::atomic<std::uint64_t>
        battle_audio_resolve_chara_cue_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_append_command_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_stop_all_trampoline_global_;
    static std::atomic<std::uint64_t> battle_audio_append_parameter_trampoline_global_;
    static std::atomic<std::uint64_t> particle_spawn_trampoline_global_;
    static std::atomic<std::uint64_t> particle_finished_bind_trampoline_global_;
    static std::atomic<std::uint64_t> gameplay_xorshift96_trampoline_global_;
    static std::atomic<std::uint64_t> movevm_evaluate_if_trampoline_global_;
    static std::atomic<std::uint64_t>
        movevm_transition_author_07_trampoline_global_;
    static std::atomic<std::uint64_t> resolved_hit_consumer_trampoline_global_;
    static std::array<std::atomic<std::uintptr_t>,
        maximum_battle_audio_handlers> observed_battle_audio_handlers_;
    static std::atomic<bool> battle_audio_handler_overflow_;
    static thread_local OuterTickCaptureContext* active_outer_capture_;

    struct BattleCharaCueSourceContext
    {
        AudioOwnerSelector selector{};
        std::uintptr_t owner{};
        std::uintptr_t battle_audio_manager{};
        std::uintptr_t chara_pairs{};
        std::int32_t chara_count{};
        std::uint64_t generation{};
        std::uint8_t cue_family{};
        bool valid{};
    };
    static thread_local BattleCharaCueSourceContext
        active_battle_chara_cue_source_;

    struct BattleDispatchSourceContext
    {
        AudioOwnerSelector selector{};
        std::uintptr_t owner{};
        std::uintptr_t battle_audio_manager{};
        std::uintptr_t owner_pairs{};
        std::int32_t owner_count{};
        std::uint64_t generation{};
        bool valid{};
    };
    static thread_local BattleDispatchSourceContext
        active_battle_dispatch_source_;

    struct AudioOwnerGraphProvenance
    {
        std::uint64_t generation{};
        std::uintptr_t battle_manager{};
        std::uintptr_t cri_manager{};
        std::uintptr_t bgm_state{};
        std::uintptr_t active_context{};
        std::uintptr_t bgm_pairs{};
        std::int32_t bgm_count{};
        std::uintptr_t battle_audio_manager{};
        std::uintptr_t class_pairs{};
        std::int32_t class_count{};
        std::uintptr_t chara_pairs{};
        std::int32_t chara_count{};
        std::uintptr_t cue_family_pairs{};
        std::int32_t cue_family_count{};
        std::uintptr_t battle_shared_player{};

        friend constexpr bool operator==(
            const AudioOwnerGraphProvenance&,
            const AudioOwnerGraphProvenance&) = default;
    };

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
    std::unique_ptr<PLH::x64Detour> battle_audio_resolve_chara_cue_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_append_command_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_stop_all_detour_{};
    std::unique_ptr<PLH::x64Detour> battle_audio_append_parameter_detour_{};
    std::unique_ptr<PLH::x64Detour> particle_spawn_detour_{};
    std::unique_ptr<PLH::x64Detour> particle_finished_bind_detour_{};
    std::unique_ptr<PLH::x64Detour> gameplay_xorshift96_detour_{};
    std::unique_ptr<PLH::x64Detour> movevm_evaluate_if_detour_{};
    std::unique_ptr<PLH::x64Detour> movevm_transition_author_07_detour_{};
    std::unique_ptr<PLH::x64Detour> resolved_hit_consumer_detour_{};
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
    std::uint64_t battle_audio_resolve_chara_cue_trampoline_{};
    std::uint64_t battle_audio_append_command_trampoline_{};
    std::uint64_t battle_audio_stop_all_trampoline_{};
    std::uint64_t battle_audio_append_parameter_trampoline_{};
    std::uint64_t particle_spawn_trampoline_{};
    std::uint64_t particle_finished_bind_trampoline_{};
    std::uint64_t gameplay_xorshift96_trampoline_{};
    std::uint64_t movevm_evaluate_if_trampoline_{};
    std::uint64_t movevm_transition_author_07_trampoline_{};
    std::uint64_t resolved_hit_consumer_trampoline_{};
    std::uint64_t next_outer_batch_id_{};
    std::uintptr_t image_base_{};
    std::uintptr_t rand_iat_slot_{};
    std::uintptr_t srand_iat_slot_{};
    UcrtRandFn original_rand_{};
    UcrtSrandFn original_srand_{};
    UcrtRandBroker* ucrt_broker_{};
    DeterministicHookCallbacks callbacks_{};
    StageBreakPresentationIdentityMap stage_break_presentation_identity_{};
    AudioOwnerResolver audio_owner_resolver_{};
    AudioPlaybackMap audio_playback_map_{};
    AudioOwnerGraphProvenance audio_graph_provenance_{};
    std::uint64_t audio_graph_generation_{};
    std::uintptr_t audio_graph_battle_manager_{};
    std::uint64_t audio_graph_epoch_counter_{};
    std::uint32_t audio_graph_failure_stage_{};
    std::atomic<bool> suppress_presentation_next_outer_tick_{};
    std::atomic<bool> installed_{};
};
}
