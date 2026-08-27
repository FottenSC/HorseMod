#pragma once

#include "CanonicalHashTimeline.hpp"
#include "DeterministicHookSet.hpp"
#include "InputTimeline.hpp"
#include "NativeBatchTimeline.hpp"
#include "ReplaySeekPlanner.hpp"
#include "Sc6CandidateCheckpointCapture.hpp"
#include "Sc6ReplayNativeBridge.hpp"

#include <optional>

namespace Horse
{
class Lux;

namespace Deterministic
{
struct ReplayTimelineStatus
{
    FailureCode failure{FailureCode::None};
    FrameCoordinate last_coordinate{};
    std::int32_t native_round{};
    std::int32_t native_time{};
    std::uint64_t generations{};
    std::uint64_t sessions{};
    std::uint64_t captured_frames{};
    std::uint64_t captured_checkpoints{};
    std::uint64_t captured_batch_entry_checkpoints{};
    std::uint64_t canonical_frames{};
    std::size_t canonical_hash_bytes{};
    std::uint64_t resumed_frames_verified{};
    std::uint64_t last_seek_validation_ns{};
    std::uint64_t last_seek_resimulation_coordinates{};
    std::size_t checkpoint_bytes{};
    std::size_t batch_entry_checkpoint_bytes{};
    std::size_t checkpoint_wind_nodes{};
    std::size_t batch_entry_wind_nodes{};
    std::uint64_t checkpoint_capture_samples{};
    std::uint64_t checkpoint_capture_max_ns{};
    std::uint64_t checkpoint_capture_p99_ns{};
    std::uint64_t checkpoint_store_max_ns{};
    std::uint64_t checkpoint_store_p99_ns{};
    std::uint64_t batch_entry_capture_samples{};
    std::uint64_t batch_entry_capture_max_ns{};
    std::uint64_t batch_entry_capture_p99_ns{};
    std::uint64_t batch_entry_store_max_ns{};
    std::uint64_t batch_entry_store_p99_ns{};
    CandidateAdapterPerformanceStatus checkpoint_adapter_performance{};
    CandidateAdapterPerformanceStatus batch_entry_adapter_performance{};
    std::uint64_t repeat_requests{};
    std::uint64_t same_native_time_coordinates{};
    std::uint64_t cursor_mismatches{};
    std::uint64_t input_filter_observations{};
    std::uint64_t input_filter_mutations{};
    std::uint64_t identity_rebaselines{};
    std::uint64_t native_batches{};
    std::uint64_t zero_coordinate_batches{};
    std::uint64_t multi_coordinate_batches{};
    std::uint64_t batch_repeat_coordinates{};
    std::uint64_t batch_same_input_time_coordinates{};
    std::uint64_t batch_input_generation_changes{};
    std::uint64_t batch_frame_accounting_mismatches{};
    std::uint64_t observed_stage_wall_calls{};
    std::uint64_t observed_stage_barrier_calls{};
    std::uint64_t observed_stage_dispatch_calls{};
    std::uint64_t observed_battle_audio_dispatches{};
    std::uint64_t observed_battle_audio_direct_dispatches{};
    std::uint64_t observed_battle_audio_remap_calls{};
    std::uint64_t observed_battle_audio_source_calls{};
    std::uint64_t observed_battle_audio_stop_all_calls{};
    std::uint64_t observed_audio_terminal_calls{};
    std::uint64_t observed_battle_audio_blueprint_calls{};
    std::uint64_t observed_particle_spawn_calls{};
    std::uint64_t fp_samples{};
    std::uint64_t fp_control_mismatches{};
    std::uint64_t fp_status_mismatches{};
    std::uint64_t fp_x87_status_mismatches{};
    std::uint64_t fp_mxcsr_status_mismatches{};
    std::uint64_t coordinates_without_batch_entry_checkpoint{};
    std::uint64_t maximum_batch_entry_checkpoint_gap{};
    std::uint64_t maximum_resim_distance_from_batch_entry{};
    std::uint32_t maximum_coordinates_per_batch{};
    std::uint32_t maximum_input_delta_per_batch{};
    std::uint32_t maximum_input_filter_invocation_ordinal{};
    std::uint32_t round_state_frame{};
    std::int32_t unpause_countdown{};
    std::uint8_t pending_move_state{};
    FloatingPointEnvironment fp_last_before{};
    FloatingPointEnvironment fp_last_after{};
    FailureCode checkpoint_failure{FailureCode::None};
    FailureCode batch_entry_checkpoint_failure{FailureCode::None};
    NativeCandidateValidationDiagnostic checkpoint_validation{};
    NativeCandidateValidationDiagnostic batch_entry_checkpoint_validation{};
    CharaAnimationTopologyIssue checkpoint_animation_topology_issue{};
    CharaAnimationTopologyIssue batch_entry_animation_topology_issue{};
    CandidateCapturePhase checkpoint_capture_phase{};
    CandidateCapturePhase batch_entry_capture_phase{};
    std::uintptr_t checkpoint_animation_observed{};
    std::uintptr_t batch_entry_animation_observed{};
    std::array<std::uintptr_t, 2> checkpoint_animation_fighters{};
    std::array<std::uintptr_t, 2> batch_entry_animation_fighters{};
    bool partial{};
    bool resume_validation_active{};
    FrameCoordinate resume_target{};
    FrameCoordinate resume_source_end{};
    FrameCoordinate resume_failure_coordinate{};
    CanonicalHash resume_expected_hash{};
    CanonicalHash resume_observed_hash{};
    std::uint32_t resume_component_difference_mask{};
    std::uint32_t resume_native_difference_mask{};
    CanonicalMoveDispatchDiagnostic resume_expected_move_dispatch{};
    CanonicalMoveDispatchDiagnostic resume_observed_move_dispatch{};
    std::uint32_t resume_input_scalar_difference_mask{};
    std::uint32_t resume_first_input_cache_chunk{UINT32_MAX};
    std::uint32_t resume_first_input_cache_row{UINT32_MAX};
    NativeInputCacheRowImage resume_expected_input_cache_row{};
    NativeInputCacheRowImage resume_observed_input_cache_row{};
    std::uint32_t resume_first_wind_semantic_chunk{UINT32_MAX};
    std::array<std::uint32_t, 12> resume_expected_input_scalars{};
    std::array<std::uint32_t, 12> resume_observed_input_scalars{};
    std::uint32_t resume_wind_difference_mask{};
    CanonicalWindNodeDiagnostic resume_expected_wind_node{};
    CanonicalWindNodeDiagnostic resume_observed_wind_node{};
    CandidateCapturePhase canonical_capture_phase{};
    FrameCoordinate canonical_capture_failure_coordinate{};
    CharaAnimationTopologyIssue canonical_animation_topology_issue{};
    std::uintptr_t canonical_animation_topology_observed{};
    std::uint32_t identity_issue{};
    std::uint64_t identity_expected{};
    std::uint64_t identity_observed{};
};

struct OwnedCorrectionResult
{
    struct WindNodeScheduleDiagnostic
    {
        std::uint32_t life_bits{};
        std::int32_t oscillator_tick{};
        std::uint32_t prepared{};
        std::uint32_t active{};
        std::uint32_t frame_step_bits{};
        std::int32_t repeat_count{};
        std::uint8_t kind{UINT8_MAX};
        bool present{};
    };
    struct WindGraphScheduleDiagnostic
    {
        std::array<WindNodeScheduleDiagnostic, 8> nodes{};
        std::uint64_t callback_hash{};
        std::uint32_t node_count{};
        std::uint32_t callback_count{};
        std::uint32_t active_bank{};
        std::int32_t pending_count{};
    };
    FailureCode failure{FailureCode::None};
    FailureCode primary_failure{FailureCode::None};
    FailureCode undo_failure{FailureCode::None};
    FrameCoordinate earliest_changed{};
    FrameCoordinate resimulation_base{};
    FrameCoordinate final_coordinate{};
    CanonicalHash final_hash{};
    CanonicalMoveDispatchDiagnostic expected_move_dispatch{};
    CanonicalMoveDispatchDiagnostic observed_move_dispatch{};
    BattleAudioSelectorImage base_audio_selector{};
    BattleAudioSelectorImage undo_audio_selector{};
    NativeCandidateValidationDiagnostic primary_validation{};
    NativeCandidateValidationDiagnostic undo_validation{};
    CandidateAdapterPerformanceStatus primary_performance{};
    NativeBatchEnvelope failed_envelope{};
    OwnedBatchReplayResult failed_batch_result{};
    std::size_t failed_batch_index{SIZE_MAX};
    std::uint64_t replayed_coordinates{};
    std::uint32_t replayed_batches{};
    std::uint64_t suppressed_stage_wall_calls{};
    std::uint64_t suppressed_stage_barrier_calls{};
    std::uint64_t semantic_stage_dispatch_calls{};
    std::uint64_t suppressed_audio_calls{};
    std::uint64_t discarded_audio_calls{};
    std::uint64_t suppressed_audio_stop_all_calls{};
    std::uint64_t suppressed_audio_terminal_calls{};
    std::uint64_t suppressed_audio_blueprint_calls{};
    std::uint64_t suppressed_particle_spawn_calls{};
    std::uint64_t suppressed_particle_finished_binds{};
    std::uint64_t unknown_particle_routes{};
    std::uint64_t verified_audio_batches{};
    std::uint64_t verified_camera_batches{};
    std::uint64_t camera_publication_mismatches{};
    std::uint64_t audio_sequence_mismatches{};
    std::uint64_t presentation_failures{};
    std::uint64_t undo_capture_ns{};
    std::uint64_t restore_ns{};
    std::uint64_t resimulation_ns{};
    std::uint64_t verification_ns{};
    std::uint64_t total_ns{};
    std::uint64_t undo_comparison_mask{};
    std::uint32_t input_scalar_difference_count{};
    std::uint32_t input_cache_difference_count{};
    std::uint32_t first_input_cache_difference{UINT32_MAX};
    std::uint32_t first_input_scalar_difference{UINT32_MAX};
    std::uint32_t expected_input_scalar_word{};
    std::uint32_t observed_input_scalar_word{};
    NativeInputCacheRowImage expected_input_cache_row{};
    NativeInputCacheRowImage observed_input_cache_row{};
    std::uint32_t rng_difference_mask{};
    std::uint32_t wind_difference_mask{};
    std::uint64_t first_interbatch_difference_mask{};
    std::size_t first_interbatch_difference_batch{SIZE_MAX};
    std::uint32_t first_interbatch_frame_difference_mask{};
    std::uint32_t first_interbatch_local_difference{UINT32_MAX};
    std::uint32_t interbatch_local_difference_count{};
    std::uint32_t first_interbatch_motion_difference{UINT32_MAX};
    std::uint32_t interbatch_motion_difference_count{};
    std::array<std::uint32_t, 2> first_final_local_difference{
        UINT32_MAX, UINT32_MAX};
    std::array<std::uint32_t, 2> final_local_difference_count{};
    HgCpuWriteSpan first_final_local_source{};
    std::uint32_t first_camera_component_slot{UINT32_MAX};
    std::uint32_t first_camera_component_difference{UINT32_MAX};
    std::uint32_t camera_component_difference_count{};
    std::uint8_t expected_camera_component_byte{};
    std::uint8_t observed_camera_component_byte{};
    std::uint32_t camera_component_vtable_rva{};
    std::uint32_t camera_component_writer_rva{};
    HgCpuWriteSpan first_interbatch_local_source{};
    std::array<std::uintptr_t, 2> diagnostic_fighter_roots{};
    std::uintptr_t diagnostic_image_base{};
    NativeRngImage expected_rng{};
    NativeRngImage observed_rng{};
    std::uint32_t first_lfsr_difference{UINT32_MAX};
    std::uint32_t expected_wind_node_count{};
    std::uint32_t observed_wind_node_count{};
    std::uint32_t first_wind_node_difference{UINT32_MAX};
    std::uint32_t first_wind_semantic_difference{UINT32_MAX};
    std::uint32_t first_wind_derived_difference{UINT32_MAX};
    std::uint32_t first_wind_output_difference{UINT32_MAX};
    std::uint8_t expected_wind_node_kind{UINT8_MAX};
    std::uint8_t observed_wind_node_kind{UINT8_MAX};
    std::uint8_t expected_wind_difference_byte{};
    std::uint8_t observed_wind_difference_byte{};
    NativeRngImage first_interbatch_expected_rng{};
    NativeRngImage first_interbatch_observed_rng{};
    WindNodeScheduleDiagnostic first_interbatch_expected_wind{};
    WindNodeScheduleDiagnostic first_interbatch_observed_wind{};
    WindNodeScheduleDiagnostic final_expected_wind{};
    WindNodeScheduleDiagnostic final_observed_wind{};
    WindGraphScheduleDiagnostic base_wind_graph{};
    WindGraphScheduleDiagnostic first_interbatch_expected_wind_graph{};
    WindGraphScheduleDiagnostic first_interbatch_observed_wind_graph{};
    bool converged{};
    bool undo_restored{};
};

class Sc6ReplayRuntime final
{
public:
    explicit Sc6ReplayRuntime(Lux& lux) noexcept;

    Status Initialize(
        std::uintptr_t image_base, UcrtRandBroker* ucrt_broker) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    void SetForcedDepth7QualificationEnabled(bool enabled) noexcept;
    void SetCorrectedInputQualificationEnabled(bool enabled) noexcept;
    [[nodiscard]] std::size_t forced_qualification_bytes() const noexcept;
    void ResetCapturePerformanceWindow() noexcept;
    [[nodiscard]] CandidateAdapterPerformanceStatus capture_performance()
        const noexcept;
    [[nodiscard]] IReplayNativeBridge* bridge() noexcept;
    Status ObserveFrame(const FrameFencepostObservation& observation) noexcept;
    Status ObserveOuterTickBegin(
        const OuterTickObservation& observation) noexcept;
    Status PrepareResumeOuterTick(
        std::uintptr_t battle_manager, std::uint32_t thread_id) noexcept;
    Status ObserveOuterTick(const OuterTickObservation& observation) noexcept;
    void ObserveReplayExit() noexcept;
    [[nodiscard]] ReplayTimelineStatus timeline_status() const noexcept;
    [[nodiscard]] const InputTimeline& input_timeline() const noexcept;
    [[nodiscard]] const NativeBatchTimeline& batch_timeline() const noexcept;
    [[nodiscard]] Status PlanSeek(
        FrameCoordinate target, ReplaySeekPlan& output) const noexcept;
    // Transactional native-state reconstruction primitive. It intentionally
    // has no production caller until presentation suppression/reconciliation
    // owns the same window.
    Status ExecuteOwnedStateSeek(
        FrameCoordinate target, DeterministicHookSet& hooks) noexcept;
    Status CaptureCurrentCanonical(Snapshot& output) noexcept;
    [[nodiscard]] bool GetSeekableRange(
        FrameCoordinate& first, FrameCoordinate& last) const noexcept;
    Status ExecuteOwnedCorrection(
        FrameCoordinate earliest_changed,
        const CanonicalHash& expected_final_hash,
        DeterministicHookSet& hooks,
        OwnedCorrectionResult& output) noexcept;
    Status ApplyConfirmedRemoteInput(
        FrameCoordinate coordinate,
        std::size_t player_index,
        const PlayerInput& confirmed_remote,
        DeterministicHookSet& hooks,
        OwnedCorrectionResult& output) noexcept;
    [[nodiscard]] Status PreflightOwnedCorrection(
        FrameCoordinate earliest_changed) const noexcept;

private:
    static constexpr std::size_t maximum_owned_correction_coordinates = 64;
    static constexpr std::size_t maximum_owned_correction_batches = 64;
    static constexpr std::size_t maximum_owned_correction_checkpoints =
        maximum_owned_correction_coordinates / (Schema::checkpoint_interval - 1)
        + 2;

    struct CorrectedReplayCapture
    {
        std::array<FrameCoordinate,
            maximum_owned_correction_coordinates> coordinates{};
        std::array<InputPair,
            maximum_owned_correction_coordinates> expected_inputs{};
        std::array<InputPair,
            maximum_owned_correction_coordinates> replacement_inputs{};
        std::array<CanonicalHashEntry,
            maximum_owned_correction_coordinates> expected_canonical{};
        std::array<CanonicalHashEntry,
            maximum_owned_correction_coordinates> replacement_canonical{};
        std::array<std::size_t,
            maximum_owned_correction_batches> batch_indices{};
        std::array<NativeBatchEnvelope,
            maximum_owned_correction_batches> expected_batches{};
        std::array<NativeBatchEnvelope,
            maximum_owned_correction_batches> replacement_batches{};
        std::array<Snapshot,
            maximum_owned_correction_checkpoints> replacement_landing{};
        std::array<CanonicalHash,
            maximum_owned_correction_checkpoints> expected_landing_hashes{};
        std::array<Snapshot,
            maximum_owned_correction_checkpoints> replacement_batch_entry{};
        std::array<CanonicalHash,
            maximum_owned_correction_checkpoints> expected_batch_entry_hashes{};
        std::size_t coordinate_count{};
        std::size_t batch_count{};
        std::size_t landing_count{};
        std::size_t batch_entry_count{};

        void Clear() noexcept
        {
            coordinate_count = 0;
            batch_count = 0;
            landing_count = 0;
            batch_entry_count = 0;
        }
    };

    struct CorrectedCoordinateCapture
    {
        Sc6ReplayRuntime* runtime{};
        std::span<InputPair> inputs{};
    };

    Status PrepareInitialGeneration(
        const OuterTickObservation& observation) noexcept;
    Status CapturePendingCameraSource() noexcept;
    void RebaselineAfterIdentityDrift() noexcept;
    static void* ResolveReplayPlayer(void* user) noexcept;
    static void* ResolveBattleManager(void* user) noexcept;
    static void* ResolveFighterOne(void* user) noexcept;
    static void* ResolveFighterTwo(void* user) noexcept;
    static void* ResolveStage(void* user) noexcept;
    static Status CaptureOwnedLanding(
        void* user, FrameCoordinate coordinate) noexcept;
    static Status CaptureCorrectedCoordinate(
        void* user, FrameCoordinate coordinate, std::uint32_t index) noexcept;
    static void ApplyCorrectedPresentationObservation(
        NativeBatchEnvelope& envelope,
        const OuterTickObservation& observation) noexcept;
    Status ExecuteOwnedCorrectionInternal(
        FrameCoordinate earliest_changed,
        const CanonicalHash* expected_final_hash,
        DeterministicHookSet& hooks,
        OwnedCorrectionResult& output,
        CorrectedReplayCapture* corrected) noexcept;
    Status ReplayOwnedBatchRange(
        std::size_t first_batch_index,
        std::size_t final_batch_index,
        std::uint64_t generation,
        DeterministicHookSet& hooks,
        std::optional<std::size_t> landing_batch_index,
        std::uint32_t landing_offset,
        Snapshot* landing,
        bool preserve_first_entry_input_log = false,
        std::uint64_t* replayed_coordinates = nullptr,
        std::uint32_t* replayed_batches = nullptr,
        std::size_t* failed_batch_index = nullptr,
        NativeBatchEnvelope* failed_envelope = nullptr,
        OwnedBatchReplayResult* failed_result = nullptr,
        std::uint64_t* first_interbatch_difference_mask = nullptr,
        std::size_t* first_interbatch_difference_batch = nullptr,
        std::uint32_t* first_interbatch_frame_difference_mask = nullptr,
        std::uint32_t* first_interbatch_local_difference = nullptr,
        std::uint32_t* interbatch_local_difference_count = nullptr,
        std::uint32_t* first_interbatch_motion_difference = nullptr,
        std::uint32_t* interbatch_motion_difference_count = nullptr,
        NativeRngImage* first_interbatch_expected_rng = nullptr,
        NativeRngImage* first_interbatch_observed_rng = nullptr,
        OwnedCorrectionResult::WindNodeScheduleDiagnostic*
            first_interbatch_expected_wind = nullptr,
        OwnedCorrectionResult::WindNodeScheduleDiagnostic*
            first_interbatch_observed_wind = nullptr,
        OwnedCorrectionResult::WindGraphScheduleDiagnostic*
            first_interbatch_expected_wind_graph = nullptr,
        OwnedCorrectionResult::WindGraphScheduleDiagnostic*
            first_interbatch_observed_wind_graph = nullptr,
        OwnedCorrectionResult* presentation_diagnostics = nullptr,
        CorrectedReplayCapture* corrected = nullptr) noexcept;

    struct OwnedLandingCapture
    {
        Sc6CandidateCheckpointCapture* checkpoints{};
        Snapshot* output{};
    };

    [[nodiscard]] void* ResolveFighter(std::size_t index) noexcept;

    Lux& lux_;
    std::optional<Sc6ReplayNativeBridge> bridge_{};
    InputTimeline input_timeline_{
        Schema::replay_input_memory_budget / Schema::replay_input_entry_budget};
    NativeBatchTimeline batch_timeline_{
        (Schema::replay_native_batch_memory_budget / 2)
            / Schema::replay_native_batch_entry_budget,
        (Schema::replay_native_batch_memory_budget / 2)
            / Schema::replay_native_batch_coordinate_budget};
    CanonicalHashTimeline canonical_timeline_{
        Schema::replay_canonical_hash_memory_budget
            / sizeof(CanonicalHashEntry)};
    Sc6CandidateCheckpointCapture checkpoint_capture_{};
    SnapshotStore forced_qualification_snapshots_{
        16u * 1024u * 1024u, 16, CapacityPolicy::EvictOldest};
    Snapshot correction_undo_scratch_{};
    Snapshot correction_verified_scratch_{};
    Snapshot correction_canonical_capture_scratch_{};
    Snapshot timeline_canonical_capture_scratch_{};
    CorrectedReplayCapture corrected_replay_capture_{};
    bool forced_depth7_qualification_enabled_{};
    bool corrected_input_qualification_enabled_{};
    ReplayTimelineStatus timeline_status_{};
    std::uintptr_t timeline_manager_{};
    std::uintptr_t timeline_input_log_{};
    std::uint32_t timeline_thread_id_{};
    std::uint64_t timeline_session_generation_{};
    std::uint64_t pending_batch_id_{};
    FrameCoordinate pending_batch_entry_{};
    NativeCameraSourceFrameImage pending_camera_source_frame_{};
    std::vector<FrameCoordinate> pending_batch_coordinates_{};
    FrameCoordinate resume_target_{};
    FrameCoordinate resume_source_end_{};
    bool resume_validation_active_{};
    bool resume_catchup_pending_{};
    bool generation_rebaseline_pending_{};
    bool continuing_session_rebaseline_{};
};
}
}
