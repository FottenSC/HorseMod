bool Sc6ReplayRuntime::ConsumeResumeValidation() noexcept
{
    if (resume_validation_active_)
    {
        pending_batch_id_ = 0;
        pending_camera_source_frame_ = {};
        pending_batch_coordinates_.clear();
        if (resume_catchup_pending_)
        {
            resume_validation_active_ = false;
            resume_catchup_pending_ = false;
            timeline_status_.resume_validation_active = false;
        }
        return true;
    }
    return false;
}

Status Sc6ReplayRuntime::AccumulateObservedGameplayIdentity(
    const OuterTickObservation& observation) noexcept
{
    if (observation.gameplay_xorshift_unknown_callers != 0)
    {
        timeline_status_.observed_gameplay_xorshift_unknown_callers +=
            observation.gameplay_xorshift_unknown_callers;
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.movevm_transition_07_signature_failures != 0)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.resolved_hit_signature_failures != 0)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    timeline_status_.observed_gameplay_xorshift_draws +=
        observation.gameplay_xorshift_draws;
    timeline_status_.observed_gameplay_xorshift_known_callers |=
        observation.gameplay_xorshift_known_callers;
    timeline_status_.observed_gameplay_xorshift_weighted_draws +=
        observation.gameplay_xorshift_weighted_draws;
    timeline_status_.observed_gameplay_xorshift_if_draws +=
        observation.gameplay_xorshift_if_draws;
    if (observation.gameplay_xorshift_draws != 0)
    {
        AppendFnv64(timeline_status_.observed_gameplay_xorshift_sequence_hash,
            &observation.batch_id, sizeof(observation.batch_id));
        AppendFnv64(timeline_status_.observed_gameplay_xorshift_sequence_hash,
            &observation.gameplay_xorshift_draws,
            sizeof(observation.gameplay_xorshift_draws));
        AppendFnv64(timeline_status_.observed_gameplay_xorshift_sequence_hash,
            &observation.gameplay_xorshift_sequence_hash,
            sizeof(observation.gameplay_xorshift_sequence_hash));
    }
    timeline_status_.observed_movevm_transition_07_calls +=
        observation.movevm_transition_07_calls;
    if (observation.movevm_transition_07_calls != 0)
    {
        AppendFnv64(timeline_status_.observed_movevm_transition_07_sequence_hash,
            &observation.batch_id, sizeof(observation.batch_id));
        AppendFnv64(timeline_status_.observed_movevm_transition_07_sequence_hash,
            &observation.movevm_transition_07_calls,
            sizeof(observation.movevm_transition_07_calls));
        AppendFnv64(timeline_status_.observed_movevm_transition_07_sequence_hash,
            &observation.movevm_transition_07_sequence_hash,
            sizeof(observation.movevm_transition_07_sequence_hash));
    }
    timeline_status_.observed_resolved_hit_calls +=
        observation.resolved_hit_calls;
    if (observation.resolved_hit_calls != 0)
    {
        AppendFnv64(timeline_status_.observed_resolved_hit_sequence_hash,
            &observation.batch_id, sizeof(observation.batch_id));
        AppendFnv64(timeline_status_.observed_resolved_hit_sequence_hash,
            &observation.resolved_hit_calls,
            sizeof(observation.resolved_hit_calls));
        AppendFnv64(timeline_status_.observed_resolved_hit_sequence_hash,
            &observation.resolved_hit_sequence_hash,
            sizeof(observation.resolved_hit_sequence_hash));
    }
    timeline_status_.observed_tira_state19_writer_calls +=
        observation.tira_state19_writer_calls;
    if (observation.tira_state19_writer_calls != 0)
    {
        AppendFnv64(timeline_status_.observed_tira_state19_writer_sequence_hash,
            &observation.batch_id, sizeof(observation.batch_id));
        AppendFnv64(timeline_status_.observed_tira_state19_writer_sequence_hash,
            &observation.tira_state19_writer_calls,
            sizeof(observation.tira_state19_writer_calls));
        AppendFnv64(timeline_status_.observed_tira_state19_writer_sequence_hash,
            &observation.tira_state19_writer_sequence_hash,
            sizeof(observation.tira_state19_writer_sequence_hash));
        timeline_status_.observed_tira_state19_writer_slot_mask |=
            observation.tira_state19_writer_slot_mask;
        timeline_status_.observed_tira_last_state19_writer_move =
            observation.tira_last_state19_writer_move;
    }
    timeline_status_.observed_tira_random_transition_calls +=
        observation.tira_random_transition_calls;
    if (observation.tira_random_transition_calls != 0)
    {
        AppendFnv64(timeline_status_.observed_tira_random_transition_sequence_hash,
            &observation.batch_id, sizeof(observation.batch_id));
        AppendFnv64(timeline_status_.observed_tira_random_transition_sequence_hash,
            &observation.tira_random_transition_calls,
            sizeof(observation.tira_random_transition_calls));
        AppendFnv64(timeline_status_.observed_tira_random_transition_sequence_hash,
            &observation.tira_random_transition_sequence_hash,
            sizeof(observation.tira_random_transition_sequence_hash));
    }
    timeline_status_.observed_tira_character_slot_mask |=
        observation.tira_character_slot_mask;
    for (std::size_t fighter = 0; fighter < 2; ++fighter)
    {
        if ((observation.tira_character_slot_mask & (1u << fighter)) != 0)
            timeline_status_.observed_tira_state19_at_transition[fighter] =
                observation.tira_state19_at_transition[fighter];
    }
    timeline_status_.observed_tira_random_transition_target_mask |=
        observation.tira_random_transition_target_mask;
    if (observation.tira_random_transition_target_mask != 0)
        timeline_status_.observed_tira_last_transition_target =
            observation.tira_last_transition_target;
    const bool tira_helper_probability_transition =
        (observation.gameplay_xorshift_if_source_mask
            & observation.tira_random_transition_source_mask) != 0;
    if (tira_helper_probability_transition)
    {
        ++timeline_status_.observed_tira_probability_transition_batches;
        const auto changed_tira_slots = static_cast<std::uint8_t>(
            pending_movevm_short25_change_mask_
            & observation.tira_character_slot_mask);
        for (std::size_t fighter = 0; fighter < 2; ++fighter)
        {
            if ((changed_tira_slots & (1u << fighter)) == 0) continue;
            const auto before = pending_movevm_short25_before_[fighter];
            const auto after = pending_movevm_short25_after_[fighter];
            if (before <= 1 && after <= 1 && before != after)
                ++timeline_status_.observed_tira_stance_transition_batches;
        }
    }
    const bool probability_draw =
        observation.gameplay_xorshift_weighted_draws != 0
        || observation.gameplay_xorshift_if_draws != 0;
    bool movevm_state_changed{};
    for (std::size_t fighter = 0;
         fighter < pending_movevm_state_short_change_masks_.size(); ++fighter)
    {
        for (std::size_t word = 0;
             word < pending_movevm_state_short_change_masks_[fighter].size();
             ++word)
        {
            const auto changed =
                pending_movevm_state_short_change_masks_[fighter][word];
            movevm_state_changed = movevm_state_changed || changed != 0;
            if (probability_draw)
            {
                timeline_status_.observed_probability_changed_state_short_masks
                    [fighter][word] |= changed;
            }
        }
    }
    if (probability_draw && movevm_state_changed)
        ++timeline_status_.observed_probability_transition_batches;
    return Status::success();
}

Status Sc6ReplayRuntime::StoreObservedBatch(
    const OuterTickObservation& observation,
    const NativeBatchEnvelope& envelope) noexcept
{
    timeline_status_.observed_stage_wall_calls += observation.stage_wall_calls;
    timeline_status_.observed_stage_barrier_calls +=
        observation.stage_barrier_calls;
    timeline_status_.observed_stage_dispatch_calls +=
        observation.stage_dispatch_calls;
    timeline_status_.observed_battle_audio_dispatches +=
        observation.battle_audio_dispatches;
    timeline_status_.observed_battle_audio_direct_dispatches +=
        observation.battle_audio_direct_dispatches;
    timeline_status_.observed_battle_audio_remap_calls +=
        observation.battle_audio_remap_calls;
    timeline_status_.observed_battle_audio_source_calls +=
        observation.battle_audio_source_calls;
    timeline_status_.observed_battle_audio_stop_all_calls +=
        observation.battle_audio_stop_all_calls;
    timeline_status_.observed_audio_terminal_calls +=
        observation.audio_terminal_calls;
    timeline_status_.observed_battle_audio_blueprint_calls +=
        observation.battle_audio_blueprint_calls;
    timeline_status_.observed_particle_spawn_calls +=
        observation.particle_spawn_calls;
    const Status stored = batch_timeline_.Append(
        envelope, pending_batch_coordinates_);
    pending_batch_id_ = 0;
    pending_camera_source_frame_ = {};
    pending_batch_coordinates_.clear();
    pending_movevm_short25_change_mask_ = 0;
    pending_movevm_short25_before_ = {};
    pending_movevm_short25_after_ = {};
    pending_movevm_state_short_change_masks_ = {};
    if (!stored.ok())
    {
        if (stored.code == FailureCode::CapacityExceeded)
        {
            timeline_status_.partial = true;
            timeline_status_.partial_reason =
                ReplayTimelinePartialReason::NativeBatch;
            timeline_status_.partial_coordinate = envelope.exit_coordinate;
            return Status::success();
        }
        timeline_status_.failure = stored.code;
        return stored;
    }
    return Status::success();
}

Status Sc6ReplayRuntime::FinalizeObservedBatch(
    const OuterTickObservation& observation,
    const NativeBatchEnvelope& envelope,
    std::uint32_t coordinate_count,
    bool input_generation_changed) noexcept
{
    if (generation_rebaseline_pending_)
    {
        // A native round/identity replacement can occur inside this outer
        // batch.  Its entry coordinate belongs to the retired generation and
        // its exit coordinate belongs to the replacement, so the envelope is
        // deliberately not correction-replayable.  Archive session-wide
        // diagnostics and invalidate the retired timeline/presentation
        // generation before any speculative publication is attempted.
        RebaselineAfterIdentityDrift();
        return Status::success();
    }
    if (presentation_ownership_enabled_)
    {
        Status presentation = presentation_controller_.BeginGeneration(
            envelope.entry_coordinate.generation);
        if (presentation.ok())
            presentation = presentation_controller_.RecordSpeculative(envelope);
        if (!presentation.ok())
        {
            timeline_status_.failure = presentation.code;
            return presentation;
        }
    }
    ++timeline_status_.native_batches;
    if (coordinate_count == 0)
        ++timeline_status_.zero_coordinate_batches;
    if (coordinate_count > 1)
        ++timeline_status_.multi_coordinate_batches;
    if (coordinate_count > timeline_status_.maximum_coordinates_per_batch)
        timeline_status_.maximum_coordinates_per_batch = coordinate_count;
    timeline_status_.batch_repeat_coordinates +=
        observation.repeat_pending_coordinates;
    timeline_status_.batch_same_input_time_coordinates +=
        observation.same_input_time_coordinates;

    const bool same_input_generation = !input_generation_changed
        && observation.after.input_game_time >= observation.before.input_game_time;
    if (same_input_generation)
    {
        const auto input_delta = static_cast<std::uint32_t>(
            observation.after.input_game_time
            - observation.before.input_game_time);
        if (input_delta > timeline_status_.maximum_input_delta_per_batch)
            timeline_status_.maximum_input_delta_per_batch = input_delta;
    }
    else
    {
        ++timeline_status_.batch_input_generation_changes;
    }
    if (coordinate_count != observation.observed_coordinates
        || (coordinate_count != 0 && timeline_status_.captured_frames != 0
            && observation.after.frame_counter
                != timeline_status_.last_coordinate.frame))
    {
        ++timeline_status_.batch_frame_accounting_mismatches;
    }
    return Status::success();
}

Status Sc6ReplayRuntime::ObserveOuterTick(
    const OuterTickObservation& observation) noexcept
{
    std::uint32_t coordinate_count{};
    bool input_generation_changed{};
    bool skip_batch{};
    Status status = BeginObservedOuterTick(observation, coordinate_count,
        input_generation_changed, skip_batch);
    if (!status.ok() || skip_batch) return status;

    NativeBatchEnvelope envelope{};
    FillObservedGameplayEnvelope(observation, coordinate_count,
        input_generation_changed, envelope);
    FillObservedPresentationEnvelope(observation, input_generation_changed,
        envelope);
    if (ConsumeResumeValidation()) return Status::success();

    status = AccumulateObservedGameplayIdentity(observation);
    if (!status.ok()) return status;
    status = StoreObservedBatch(observation, envelope);
    if (!status.ok() || timeline_status_.partial) return status;
    return FinalizeObservedBatch(observation, envelope, coordinate_count,
        input_generation_changed);
}

void Sc6ReplayRuntime::ObserveReplayExit() noexcept
{
    // Replay PostTick may immediately replace camera, fighter, stage, and
    // container allocations. Invalidate every dependent local image and input
    // envelope before that native teardown begins; no identity survives re-entry.
    input_timeline_.Clear();
    batch_timeline_.Clear();
    canonical_timeline_.Clear();
    archived_last_canonical_.reset();
    archived_canonical_frames_ = 0;
    archived_presentation_identity_ = {};
    forced_qualification_snapshots_.Clear();
    correction_undo_scratch_ = {};
    correction_verified_scratch_ = {};
    correction_canonical_capture_scratch_ = {};
    timeline_canonical_capture_scratch_ = {};
    checkpoint_capture_.ReleaseHistoryStorage();
    presentation_controller_.EndGeneration();
    diagnostic_snapshot_scratch_ = {};
    if (diagnostic_image_a_ != nullptr) *diagnostic_image_a_ = {};
    if (diagnostic_image_b_ != nullptr) *diagnostic_image_b_ = {};
    for (auto& snapshot : corrected_replay_capture_.replacement_landing)
        snapshot = {};
    for (auto& snapshot : corrected_replay_capture_.replacement_batch_entry)
        snapshot = {};
    corrected_replay_capture_.Clear();
    timeline_status_ = {};
    timeline_manager_ = 0;
    timeline_input_log_ = 0;
    timeline_thread_id_ = 0;
    pending_batch_id_ = 0;
    pending_batch_entry_ = {};
    pending_camera_source_frame_ = {};
    pending_batch_coordinates_.clear();
    last_movevm_short25_ = {};
    last_movevm_state_shorts_ = {};
    pending_movevm_state_short_change_masks_ = {};
    pending_movevm_short25_change_mask_ = 0;
    pending_movevm_short25_before_ = {};
    pending_movevm_short25_after_ = {};
    last_movevm_short25_valid_ = false;
    resume_target_ = {};
    resume_source_end_ = {};
    resume_validation_active_ = false;
    resume_catchup_pending_ = false;
    checkpoint_capture_.ReleaseBinding();
    generation_rebaseline_pending_ = false;
    continuing_session_rebaseline_ = false;
    replay_history_capture_required_ =
        next_replay_history_capture_required_;
}

Status Sc6ReplayRuntime::ResetQualificationCycle(
    std::uint64_t& stale_state_mask) noexcept
{
    stale_state_mask = 0;
    if (pending_presentation_events() != 0) stale_state_mask |= 1ull << 0;
    if (presentation_payload_bytes() != 0) stale_state_mask |= 1ull << 1;
    if (pending_batch_id_ != 0) stale_state_mask |= 1ull << 2;
    if (resume_validation_active_ || resume_catchup_pending_)
        stale_state_mask |= 1ull << 3;
    if (stale_state_mask != 0)
        return Status::failure(FailureCode::IllegalTransition);

    SetForcedDepth7QualificationEnabled(false);
    DisablePresentationOwnership();
    ObserveReplayExit();

    if (input_timeline_.size() != 0) stale_state_mask |= 1ull << 4;
    if (batch_timeline_.batch_count() != 0) stale_state_mask |= 1ull << 5;
    if (canonical_timeline_.size() != 0) stale_state_mask |= 1ull << 6;
    if (forced_qualification_snapshots_.entry_count() != 0)
        stale_state_mask |= 1ull << 7;
    if (timeline_status_.last_coordinate != FrameCoordinate{}
        || timeline_status_.canonical_frames != 0
        || timeline_status_.failure != FailureCode::None)
        stale_state_mask |= 1ull << 8;
    if (timeline_manager_ != 0 || timeline_input_log_ != 0
        || timeline_thread_id_ != 0 || pending_batch_id_ != 0)
        stale_state_mask |= 1ull << 9;
    if (presentation_ownership_enabled_
        || presentation_controller_.generation() != 0
        || pending_presentation_events() != 0
        || presentation_payload_bytes() != 0)
        stale_state_mask |= 1ull << 10;
    if (resume_validation_active_ || resume_catchup_pending_
        || resume_target_ != FrameCoordinate{}
        || resume_source_end_ != FrameCoordinate{})
        stale_state_mask |= 1ull << 11;
    if (corrected_replay_capture_.batch_count != 0)
        stale_state_mask |= 1ull << 12;
    if (timeline_status_.observed_gameplay_xorshift_draws != 0
        || timeline_status_.observed_gameplay_xorshift_sequence_hash != 0
        || timeline_status_.final_gameplay_xorshift_state
            != std::array<std::uint32_t, 3>{})
        stale_state_mask |= 1ull << 13;
    if (archived_last_canonical_.has_value()
        || archived_canonical_frames_ != 0
        || archived_presentation_identity_ != std::array<std::uint64_t, 9>{})
        stale_state_mask |= 1ull << 14;
    if (last_movevm_short25_valid_
        || last_movevm_short25_ != std::array<std::uint16_t, 2>{}
        || pending_movevm_short25_change_mask_ != 0)
        stale_state_mask |= 1ull << 15;
    const auto snapshot_has_state = [](const Snapshot& snapshot) noexcept {
        return !snapshot.bytes.empty() || !snapshot.local_images.empty()
            || snapshot.coordinate != FrameCoordinate{};
    };
    if (snapshot_has_state(correction_undo_scratch_)
        || snapshot_has_state(correction_verified_scratch_)
        || snapshot_has_state(correction_canonical_capture_scratch_)
        || snapshot_has_state(timeline_canonical_capture_scratch_)
        || snapshot_has_state(diagnostic_snapshot_scratch_))
        stale_state_mask |= 1ull << 16;
    if (stale_state_mask != 0)
        return Status::failure(FailureCode::IllegalTransition);
    return presentation_controller_.ResetStatistics();
}

Status Sc6ReplayRuntime::EnablePresentationOwnership() noexcept
{
    if (presentation_ownership_enabled_)
        return Status::failure(FailureCode::IllegalTransition);
    presentation_controller_.EndGeneration();
    presentation_ownership_enabled_ = true;
    return Status::success();
}

void Sc6ReplayRuntime::DisablePresentationOwnership() noexcept
{
    presentation_controller_.EndGeneration();
    presentation_ownership_enabled_ = false;
}

Status Sc6ReplayRuntime::PreparePresentationOuterTick(
    DeterministicHookSet& hooks) noexcept
{
    if (!presentation_ownership_enabled_)
        return Status::success();
    return hooks.ArmPresentationCaptureForNextOuterTick();
}

Status Sc6ReplayRuntime::CommitPresentationThrough(
    FrameCoordinate confirmed, DeterministicHookSet& hooks) noexcept
{
    if (!presentation_ownership_enabled_)
        return Status::failure(FailureCode::IllegalTransition);
    // Rebaselining ends the retired presentation generation and leaves no
    // pending events.  Until the first complete batch in the replacement
    // generation is observed there is intentionally nothing to commit.
    if (presentation_controller_.generation() == 0
        && pending_presentation_events() == 0
        && presentation_payload_bytes() == 0)
        return Status::success();
    const auto generation = presentation_controller_.BeginGeneration(
        confirmed.generation);
    if (!generation.ok()) return generation;
    Sc6PresentationSink sink{hooks};
    return presentation_controller_.CommitThrough(confirmed, sink);
}

bool Sc6ReplayRuntime::presentation_ownership_enabled() const noexcept
{
    return presentation_ownership_enabled_;
}

bool Sc6ReplayRuntime::IsOnlineClearForStock() const noexcept
{
    return !online_predicted_remote_player_.has_value()
        && !presentation_ownership_enabled_ && pending_batch_id_ == 0
        && !resume_validation_active_ && !resume_catchup_pending_
        && timeline_status_.last_coordinate == FrameCoordinate{}
        && pending_presentation_events() == 0;
}

std::size_t Sc6ReplayRuntime::pending_presentation_events() const noexcept
{
    return presentation_controller_.pending_count();
}

std::size_t Sc6ReplayRuntime::presentation_payload_bytes() const noexcept
{
    return presentation_controller_.payload_bytes();
}

namespace
{
std::size_t SnapshotDynamicCapacity(const Snapshot& snapshot) noexcept
{
    std::size_t bytes = snapshot.bytes.capacity()
        + snapshot.local_images.capacity() * sizeof(LocalReconstructionImage);
    for (const auto& local : snapshot.local_images)
        bytes += local.bytes.capacity();
    return bytes;
}
}

DeterministicOwnedStorageStatus Sc6ReplayRuntime::owned_storage_status()
    const noexcept
{
    DeterministicOwnedStorageStatus result{};
    result.timeline_bytes = input_timeline_.allocated_bytes()
        + batch_timeline_.allocated_bytes()
        + canonical_timeline_.allocated_bytes()
        + checkpoint_capture_.status(CandidateCheckpointRole::Landing).bytes_used
        + checkpoint_capture_.status(CandidateCheckpointRole::BatchEntry).bytes_used;
    result.forced_snapshot_bytes = forced_qualification_snapshots_.BytesUsed();
    result.presentation_bytes = presentation_controller_.allocated_bytes();
    result.scratch_metadata_bytes = sizeof(*this)
        + checkpoint_capture_.owned_scratch_bytes()
        + pending_batch_coordinates_.capacity() * sizeof(FrameCoordinate)
        + SnapshotDynamicCapacity(correction_undo_scratch_)
        + SnapshotDynamicCapacity(correction_verified_scratch_)
        + SnapshotDynamicCapacity(correction_canonical_capture_scratch_)
        + SnapshotDynamicCapacity(timeline_canonical_capture_scratch_)
        + SnapshotDynamicCapacity(diagnostic_snapshot_scratch_)
        + (diagnostic_image_a_ ? sizeof(CandidateCheckpointImage)
            + CandidateCheckpointDynamicCapacity(*diagnostic_image_a_, true) : 0)
        + (diagnostic_image_b_ ? sizeof(CandidateCheckpointImage)
            + CandidateCheckpointDynamicCapacity(*diagnostic_image_b_, true) : 0);
    for (const auto& snapshot : corrected_replay_capture_.replacement_landing)
        result.scratch_metadata_bytes += SnapshotDynamicCapacity(snapshot);
    for (const auto& snapshot :
            corrected_replay_capture_.replacement_batch_entry)
        result.scratch_metadata_bytes += SnapshotDynamicCapacity(snapshot);
    result.aggregate_bytes = result.timeline_bytes
        + result.forced_snapshot_bytes + result.presentation_bytes
        + result.scratch_metadata_bytes;
    return result;
}

PresentationJournal::Statistics
Sc6ReplayRuntime::presentation_statistics() const noexcept
{
    return presentation_controller_.statistics();
}

ReplayTimelineStatus Sc6ReplayRuntime::timeline_status() const noexcept
{
    return timeline_status_;
}

const ReplayTimelineStatus& Sc6ReplayRuntime::timeline_status_view()
    const noexcept
{
    return timeline_status_;
}

const InputTimeline& Sc6ReplayRuntime::input_timeline() const noexcept
{
    return input_timeline_;
}

const NativeBatchTimeline& Sc6ReplayRuntime::batch_timeline() const noexcept
{
    return batch_timeline_;
}

Status Sc6ReplayRuntime::PlanSeek(
    FrameCoordinate target, ReplaySeekPlan& output) const noexcept
{
    output = {};
    if (!ready())
        return Status::failure(FailureCode::ContextUnavailable);
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    return PlanReplaySeek(
        target,
        batch_timeline_,
        checkpoint_capture_.snapshots(CandidateCheckpointRole::BatchEntry),
        Schema::checkpoint_interval - 1,
        output);
}

Status Sc6ReplayRuntime::CaptureOwnedLanding(
    void* user, FrameCoordinate coordinate) noexcept
{
    auto* capture = static_cast<OwnedLandingCapture*>(user);
    if (capture == nullptr || capture->checkpoints == nullptr
        || capture->output == nullptr)
        return Status::failure(FailureCode::InvalidConfiguration);
    return capture->checkpoints->CaptureTransient(
        coordinate, *capture->output);
}

Status Sc6ReplayRuntime::CaptureCorrectedCoordinate(
    void* user, FrameCoordinate coordinate, std::uint32_t index) noexcept
{
    auto* capture = static_cast<CorrectedCoordinateCapture*>(user);
    if (capture == nullptr || capture->runtime == nullptr
        || index >= capture->inputs.size())
        return Status::failure(FailureCode::InvalidConfiguration);
    auto& runtime = *capture->runtime;
    auto& corrected = runtime.corrected_replay_capture_;
    if (corrected.coordinate_count >= corrected.coordinates.size())
        return Status::failure(FailureCode::CapacityExceeded);
    const auto expected_canonical = runtime.canonical_timeline_.GetExact(
        coordinate);
    const auto expected_input = runtime.input_timeline_.GetExact(coordinate);
    if (!expected_canonical.has_value() || !expected_input.has_value())
        return Status::failure(FailureCode::MissingSnapshot);
    const auto* retained_landing = runtime.checkpoint_capture_.snapshots(
        CandidateCheckpointRole::Landing).FindExact(coordinate);
    if (retained_landing != nullptr
        && corrected.landing_count >= corrected.replacement_landing.size())
        return Status::failure(FailureCode::CapacityExceeded);
    Snapshot& snapshot = retained_landing == nullptr
        ? runtime.correction_canonical_capture_scratch_
        : corrected.replacement_landing[corrected.landing_count];
    const Status captured = retained_landing == nullptr
        ? runtime.checkpoint_capture_.CaptureCanonical(coordinate, snapshot)
        : runtime.checkpoint_capture_.CaptureTransient(coordinate, snapshot);
    if (!captured.ok()) return captured;
    const auto target = corrected.coordinate_count++;
    corrected.coordinates[target] = coordinate;
    corrected.expected_inputs[target] = *expected_input;
    corrected.replacement_inputs[target] = capture->inputs[index];
    corrected.expected_canonical[target] = *expected_canonical;
    corrected.replacement_canonical[target] = {
        coordinate, snapshot.canonical_hash, snapshot.canonical_components,
        snapshot.canonical_native, snapshot.canonical_move_dispatch,
        snapshot.canonical_input, snapshot.canonical_wind_semantic,
        snapshot.canonical_wind, snapshot.canonical_wind_node};
    if (retained_landing != nullptr)
    {
        corrected.expected_landing_hashes[corrected.landing_count] =
            retained_landing->canonical_hash;
        ++corrected.landing_count;
    }
    return Status::success();
}

void Sc6ReplayRuntime::ApplyCorrectedPresentationObservation(
    NativeBatchEnvelope& envelope,
    const OuterTickObservation& observation) noexcept
{
    envelope.repeat_pending_coordinates = observation.repeat_pending_coordinates;
    envelope.same_input_time_coordinates = observation.same_input_time_coordinates;
    envelope.gameplay_xorshift_draws = observation.gameplay_xorshift_draws;
    envelope.gameplay_xorshift_sequence_hash =
        observation.gameplay_xorshift_sequence_hash;
    envelope.gameplay_xorshift_known_callers =
        observation.gameplay_xorshift_known_callers;
    envelope.gameplay_xorshift_unknown_callers =
        observation.gameplay_xorshift_unknown_callers;
    envelope.gameplay_xorshift_weighted_draws =
        observation.gameplay_xorshift_weighted_draws;
    envelope.gameplay_xorshift_if_draws =
        observation.gameplay_xorshift_if_draws;
    envelope.gameplay_xorshift_weighted_source_mask =
        observation.gameplay_xorshift_weighted_source_mask;
    envelope.gameplay_xorshift_if_source_mask =
        observation.gameplay_xorshift_if_source_mask;
    envelope.movevm_transition_07_calls = observation.movevm_transition_07_calls;
    envelope.movevm_transition_07_sequence_hash =
        observation.movevm_transition_07_sequence_hash;
    envelope.movevm_transition_07_signature_failures =
        observation.movevm_transition_07_signature_failures;
    envelope.resolved_hit_calls = observation.resolved_hit_calls;
    envelope.resolved_hit_sequence_hash = observation.resolved_hit_sequence_hash;
    envelope.resolved_hit_signature_failures =
        observation.resolved_hit_signature_failures;
    envelope.tira_state19_writer_calls = observation.tira_state19_writer_calls;
    envelope.tira_state19_writer_sequence_hash =
        observation.tira_state19_writer_sequence_hash;
    envelope.tira_state19_writer_slot_mask =
        observation.tira_state19_writer_slot_mask;
    envelope.tira_last_state19_writer_move =
        observation.tira_last_state19_writer_move;
    envelope.tira_random_transition_calls =
        observation.tira_random_transition_calls;
    envelope.tira_random_transition_sequence_hash =
        observation.tira_random_transition_sequence_hash;
    envelope.tira_random_transition_source_mask =
        observation.tira_random_transition_source_mask;
    envelope.tira_random_transition_target_mask =
        observation.tira_random_transition_target_mask;
    envelope.tira_last_transition_target = observation.tira_last_transition_target;
    envelope.tira_character_slot_mask = observation.tira_character_slot_mask;
    envelope.tira_state19_at_transition =
        observation.tira_state19_at_transition;
    envelope.stage_wall_calls = observation.stage_wall_calls;
    envelope.stage_wall_hash = observation.stage_wall_hash;
    envelope.stage_barrier_calls = observation.stage_barrier_calls;
    envelope.stage_barrier_hash = observation.stage_barrier_hash;
    envelope.stage_dispatch_calls = observation.stage_dispatch_calls;
    envelope.stage_dispatch_hash = observation.stage_dispatch_hash;
    envelope.stage_signature_failures = observation.stage_signature_failures;
    envelope.battle_audio_dispatches = observation.battle_audio_dispatches;
    envelope.battle_audio_sequence_hash = observation.battle_audio_sequence_hash;
    envelope.battle_audio_route_hash = observation.battle_audio_route_hash;
    envelope.battle_audio_payload_hash = observation.battle_audio_payload_hash;
    envelope.battle_audio_position_hash = observation.battle_audio_position_hash;
    envelope.battle_audio_direct_dispatches =
        observation.battle_audio_direct_dispatches;
    envelope.battle_audio_direct_route_hash =
        observation.battle_audio_direct_route_hash;
    envelope.battle_audio_direct_payload_hash =
        observation.battle_audio_direct_payload_hash;
    envelope.battle_audio_direct_position_hash =
        observation.battle_audio_direct_position_hash;
    envelope.battle_audio_direct_sequence_hash =
        observation.battle_audio_direct_sequence_hash;
    envelope.battle_audio_remap_calls = observation.battle_audio_remap_calls;
    envelope.battle_audio_remap_hash = observation.battle_audio_remap_hash;
    envelope.battle_audio_source_calls = observation.battle_audio_source_calls;
    envelope.battle_audio_source_hash = observation.battle_audio_source_hash;
    envelope.battle_audio_stop_all_calls = observation.battle_audio_stop_all_calls;
    envelope.battle_audio_stop_all_hash = observation.battle_audio_stop_all_hash;
    envelope.audio_terminal_calls = observation.audio_terminal_calls;
    envelope.audio_terminal_hash = observation.audio_terminal_hash;
    envelope.battle_audio_blueprint_calls = observation.battle_audio_blueprint_calls;
    envelope.battle_audio_blueprint_hash = observation.battle_audio_blueprint_hash;
    envelope.particle_spawn_calls = observation.particle_spawn_calls;
    envelope.particle_spawn_hash = observation.particle_spawn_hash;
    envelope.particle_signature_failures = observation.particle_signature_failures;
    envelope.camera_publication_hash = observation.camera_publication_hash;
    envelope.camera_publication = observation.camera_publication;
    envelope.camera_signature_failures = observation.camera_signature_failures;
    envelope.presentation_order_hash = observation.presentation_order_hash;
    envelope.presentation_order_failures = observation.presentation_order_failures;
    envelope.qualification_stage_terminal_mask =
        observation.qualification_stage_terminal_mask;
    envelope.battle_audio_remap_entry_values =
        observation.battle_audio_remap_entry_values;
    envelope.battle_audio_remap_entry_mask = observation.battle_audio_remap_entry_mask;
    envelope.battle_audio_journal = observation.battle_audio_journal;
    envelope.battle_audio_source_journal = observation.battle_audio_source_journal;
    envelope.battle_audio_remap_journal = observation.battle_audio_remap_journal;
    envelope.battle_audio_blueprint_journal =
        observation.battle_audio_blueprint_journal;
    envelope.battle_audio_stop_all_journal =
        observation.battle_audio_stop_all_journal;
    envelope.audio_terminal_journal = observation.audio_terminal_journal;
    envelope.audio_terminal_return_rvas =
        observation.audio_terminal_return_rvas;
    envelope.audio_terminal_raw_cue_sheet_ids =
        observation.audio_terminal_raw_cue_sheet_ids;
    envelope.stage_wall_journal = observation.stage_wall_journal;
    envelope.stage_barrier_journal = observation.stage_barrier_journal;
    envelope.stage_dispatch_journal = observation.stage_dispatch_journal;
    envelope.particle_spawn_journal = observation.particle_spawn_journal;
    envelope.presentation_order_journal = observation.presentation_order_journal;
    envelope.battle_audio_journal_count = observation.battle_audio_journal_count;
    envelope.battle_audio_source_journal_count =
        observation.battle_audio_source_journal_count;
    envelope.battle_audio_remap_journal_count =
        observation.battle_audio_remap_journal_count;
    envelope.battle_audio_blueprint_journal_count =
        observation.battle_audio_blueprint_journal_count;
    envelope.battle_audio_stop_all_journal_count =
        observation.battle_audio_stop_all_journal_count;
    envelope.audio_terminal_journal_count =
        observation.audio_terminal_journal_count;
    envelope.stage_wall_journal_count = observation.stage_wall_journal_count;
    envelope.stage_barrier_journal_count = observation.stage_barrier_journal_count;
    envelope.stage_dispatch_journal_count = observation.stage_dispatch_journal_count;
    envelope.particle_spawn_journal_count = observation.particle_spawn_journal_count;
    envelope.presentation_order_journal_count =
        observation.presentation_order_journal_count;
}

Status Sc6ReplayRuntime::ValidateReplayedPresentationEnvelope(
    const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult& result) noexcept
{
    Status status = Status::success();
        if (status.ok()
            && (envelope.stage_signature_failures != 0
                || result.stage_signature_failures != 0
                || result.suppressed_stage_wall_calls
                    != envelope.stage_wall_calls
                || result.stage_wall_hash != envelope.stage_wall_hash
                || result.suppressed_stage_barrier_calls
                    != envelope.stage_barrier_calls
                || result.stage_barrier_hash != envelope.stage_barrier_hash
                || result.semantic_stage_dispatch_calls
                    != envelope.stage_dispatch_calls
                || result.stage_dispatch_hash != envelope.stage_dispatch_hash))
        {
            ++result.presentation_failures;
            result.failure = FailureCode::PresentationFailed;
            status = Status::failure(result.failure);
        }
        if (status.ok()
            && (result.suppressed_audio_source_calls
                    != envelope.battle_audio_source_calls
                || result.suppressed_audio_source_hash
                    != envelope.battle_audio_source_hash))
        {
            ++result.audio_sequence_mismatches;
            ++result.presentation_failures;
            result.failure = FailureCode::PresentationFailed;
            status = Status::failure(result.failure);
        }
        if (status.ok()
            && (envelope.camera_signature_failures != 0
                || result.camera_signature_failures != 0
                || result.camera_publication_hash
                    != envelope.camera_publication_hash))
        {
            const auto* expected_camera = reinterpret_cast<const std::byte*>(
                &envelope.camera_publication);
            const auto* observed_camera = reinterpret_cast<const std::byte*>(
                &result.camera_publication);
            for (std::uint32_t index = 0;
                 index < sizeof(CameraPublicationState); ++index)
            {
                if (expected_camera[index] == observed_camera[index]) continue;
                if (result.first_camera_publication_difference == UINT32_MAX)
                {
                    result.first_camera_publication_difference = index;
                    result.expected_camera_publication_byte =
                        std::to_integer<std::uint8_t>(expected_camera[index]);
                    result.observed_camera_publication_byte =
                        std::to_integer<std::uint8_t>(observed_camera[index]);
                }
                ++result.camera_publication_difference_count;
            }
            ++result.camera_publication_mismatches;
            ++result.presentation_failures;
            result.failure = FailureCode::PresentationFailed;
            status = Status::failure(result.failure);
        }
        if (status.ok()
            && (result.suppressed_audio_calls
                    != envelope.battle_audio_dispatches
                || result.suppressed_audio_route_hash
                    != envelope.battle_audio_route_hash
                || result.suppressed_audio_payload_hash
                    != envelope.battle_audio_payload_hash
                || result.suppressed_audio_position_hash
                    != envelope.battle_audio_position_hash
                || result.suppressed_audio_direct_dispatches
                    != envelope.battle_audio_direct_dispatches
                || result.suppressed_audio_direct_sequence_hash
                    != envelope.battle_audio_direct_sequence_hash
                || result.suppressed_audio_direct_route_hash
                    != envelope.battle_audio_direct_route_hash
                || result.suppressed_audio_direct_payload_hash
                    != envelope.battle_audio_direct_payload_hash
                || result.suppressed_audio_direct_position_hash
                    != envelope.battle_audio_direct_position_hash
                || result.suppressed_audio_remap_calls
                    != envelope.battle_audio_remap_calls
                || result.suppressed_audio_remap_hash
                    != envelope.battle_audio_remap_hash
                || result.suppressed_audio_stop_all_calls
                    != envelope.battle_audio_stop_all_calls
                || result.suppressed_audio_stop_all_hash
                    != envelope.battle_audio_stop_all_hash
                || result.suppressed_audio_terminal_calls
                    != envelope.audio_terminal_calls
                || result.suppressed_audio_terminal_hash
                    != envelope.audio_terminal_hash
                || result.suppressed_audio_blueprint_calls
                    != envelope.battle_audio_blueprint_calls
                || result.suppressed_audio_blueprint_hash
                    != envelope.battle_audio_blueprint_hash
                || result.suppressed_particle_spawn_calls
                    != envelope.particle_spawn_calls
                || result.suppressed_particle_spawn_hash
                    != envelope.particle_spawn_hash
                || result.suppressed_presentation_order_events
                    != envelope.presentation_order_journal_count
                || result.suppressed_presentation_order_hash
                    != envelope.presentation_order_hash
                || result.unknown_particle_routes != 0
                || result.suppressed_audio_remap_entry_mask
                    != envelope.battle_audio_remap_entry_mask
                || result.suppressed_audio_remap_entry_values
                    != envelope.battle_audio_remap_entry_values))
        {
            ++result.audio_sequence_mismatches;
            ++result.presentation_failures;
            result.failure = FailureCode::PresentationFailed;
            status = Status::failure(result.failure);
        }
    return status;
}

void Sc6ReplayRuntime::AccumulateReplayPresentationDiagnostics(
    const OwnedBatchReplayResult& result,
    OwnedCorrectionResult* diagnostics) noexcept
{
        if (diagnostics != nullptr)
        {
            diagnostics->suppressed_stage_wall_calls +=
                result.suppressed_stage_wall_calls;
            diagnostics->suppressed_stage_barrier_calls +=
                result.suppressed_stage_barrier_calls;
            diagnostics->semantic_stage_dispatch_calls +=
                result.semantic_stage_dispatch_calls;
            diagnostics->suppressed_audio_calls +=
                result.suppressed_audio_calls;
            diagnostics->discarded_audio_calls +=
                result.discarded_audio_calls;
            diagnostics->suppressed_audio_stop_all_calls +=
                result.suppressed_audio_stop_all_calls;
            diagnostics->suppressed_audio_terminal_calls +=
                result.suppressed_audio_terminal_calls;
            diagnostics->suppressed_audio_blueprint_calls +=
                result.suppressed_audio_blueprint_calls;
            diagnostics->suppressed_particle_spawn_calls +=
                result.suppressed_particle_spawn_calls;
            diagnostics->suppressed_particle_finished_binds +=
                result.suppressed_particle_finished_binds;
            diagnostics->unknown_particle_routes +=
                result.unknown_particle_routes;
            diagnostics->verified_audio_batches +=
                result.audio_sequence_mismatches == 0 ? 1 : 0;
            diagnostics->verified_camera_batches +=
                result.camera_publication_mismatches == 0
                    && result.camera_signature_failures == 0 ? 1 : 0;
            diagnostics->camera_publication_mismatches +=
                result.camera_publication_mismatches;
            diagnostics->audio_sequence_mismatches +=
                result.audio_sequence_mismatches;
            diagnostics->presentation_failures +=
                result.presentation_failures;
        }
}

Status Sc6ReplayRuntime::CaptureCorrectedReplayBatch(
    std::size_t batch_index,
    const NativeBatchEnvelope& envelope,
    const NativeCameraSourceFrameImage& camera_source,
    const OuterTickObservation& corrected_observation,
    NativeCameraSourceFrameImage& next_camera_source,
    bool& next_camera_source_valid,
    CorrectedReplayCapture* corrected) noexcept
{
        if (corrected != nullptr)
        {
            const auto target = corrected->batch_count++;
            corrected->batch_indices[target] = batch_index;
            corrected->expected_batches[target] = envelope;
            corrected->replacement_batches[target] = envelope;
            corrected->replacement_batches[target].camera_source_frame =
                camera_source;
            ApplyCorrectedPresentationObservation(
                corrected->replacement_batches[target],
                corrected_observation);
            const Status captured_camera =
                checkpoint_capture_.CaptureCameraSourceFrame(
                    next_camera_source);
            if (!captured_camera.ok()) return captured_camera;
            next_camera_source_valid = true;

            if (corrected->coordinate_count == 0
                || corrected->coordinates[corrected->coordinate_count - 1]
                    != envelope.exit_coordinate)
                return Status::failure(FailureCode::MissingSnapshot);
            const auto corrected_exit = corrected->coordinate_count - 1;
            const Status normalized_input =
                checkpoint_capture_.PrepareInputLogForReplay(
                    corrected->replacement_canonical[corrected_exit].input,
                    corrected->replacement_inputs[corrected_exit]);
            if (!normalized_input.ok()) return normalized_input;
            const auto* retained_entry = checkpoint_capture_.snapshots(
                CandidateCheckpointRole::BatchEntry).FindExact(
                    envelope.exit_coordinate);
            if (retained_entry != nullptr)
            {
                if (corrected->batch_entry_count
                    >= corrected->replacement_batch_entry.size())
                    return Status::failure(FailureCode::CapacityExceeded);
                const auto checkpoint_index = corrected->batch_entry_count;
                Status captured_entry = checkpoint_capture_.CaptureTransient(
                    envelope.exit_coordinate,
                    corrected->replacement_batch_entry[checkpoint_index]);
                if (!captured_entry.ok()) return captured_entry;
                corrected->expected_batch_entry_hashes[checkpoint_index] =
                    retained_entry->canonical_hash;
                ++corrected->batch_entry_count;
            }
        }
    return Status::success();
}

Status Sc6ReplayRuntime::PrepareReplayBatchHandoffs(
    std::size_t batch_index,
    std::size_t first_batch_index,
    bool preserve_first_entry_input_log,
    const NativeBatchEnvelope& envelope,
    std::span<const InputPair> inputs,
    CorrectedReplayCapture* corrected,
    const NativeCameraSourceFrameImage& camera_source,
    bool capture_landing,
    std::uint32_t landing_offset) noexcept
{
    const auto* batch_entry = checkpoint_capture_.snapshots(
        CandidateCheckpointRole::BatchEntry).FindExact(
            envelope.entry_coordinate);
    Status status{};
    if (corrected != nullptr && batch_index != first_batch_index)
    {
        if (corrected->coordinate_count == 0
            || corrected->coordinates[corrected->coordinate_count - 1]
                != envelope.entry_coordinate)
            return Status::failure(FailureCode::MissingSnapshot);
        const auto entry = corrected->coordinate_count - 1;
        status = checkpoint_capture_.PrepareInputLogForReplay(
            corrected->replacement_canonical[entry].input,
            corrected->replacement_inputs[entry]);
    }
    else if (preserve_first_entry_input_log
        && batch_index == first_batch_index)
        status = Status::success();
    else if (batch_entry != nullptr)
        status = checkpoint_capture_.RestoreInputLogForReplay(*batch_entry);
    else
    {
        const auto expected = canonical_timeline_.GetExact(
            envelope.entry_coordinate);
        if (!expected.has_value() || envelope.coordinate_count == 0)
            return Status::failure(FailureCode::MissingSnapshot);
        status = checkpoint_capture_.PrepareInputLogForReplay(
            expected->input, inputs[0]);
    }
    if (!status.ok()) return status;
    status = checkpoint_capture_.RestoreCameraSourceFrameForReplay(
        camera_source);
    if (!status.ok()) return status;
    if (capture_landing
        && !canonical_timeline_.GetExact(
            inputs.empty() ? FrameCoordinate{}
                           : FrameCoordinate{envelope.entry_coordinate.generation,
                               envelope.entry_coordinate.frame
                                   + landing_offset + 1}).has_value())
        return Status::failure(FailureCode::MissingSnapshot);
    return Status::success();
}

void Sc6ReplayRuntime::CaptureInterbatchDiagnostics(
    std::size_t batch_index,
    std::size_t final_batch_index,
    const NativeBatchEnvelope& envelope,
    const InterbatchDiagnosticTargets& targets) noexcept
{
    auto* first_interbatch_difference_mask = targets.difference_mask;
    auto* first_interbatch_difference_batch = targets.difference_batch;
    auto* first_interbatch_frame_difference_mask = targets.frame_difference_mask;
    auto* first_interbatch_local_difference = targets.local_difference;
    auto* interbatch_local_difference_count = targets.local_difference_count;
    auto* first_interbatch_motion_difference = targets.motion_difference;
    auto* interbatch_motion_difference_count = targets.motion_difference_count;
    auto* first_interbatch_expected_rng = targets.expected_rng;
    auto* first_interbatch_observed_rng = targets.observed_rng;
    auto* first_interbatch_expected_wind = targets.expected_wind;
    auto* first_interbatch_observed_wind = targets.observed_wind;
    auto* first_interbatch_expected_wind_graph = targets.expected_wind_graph;
    auto* first_interbatch_observed_wind_graph = targets.observed_wind_graph;
        if (batch_index < final_batch_index
            && first_interbatch_difference_mask != nullptr
            && *first_interbatch_difference_mask == 0)
        {
            const NativeBatchEnvelope* next =
                batch_timeline_.GetBatch(batch_index + 1);
            const auto* expected = next == nullptr
                ? nullptr
                : checkpoint_capture_.snapshots(
                    CandidateCheckpointRole::BatchEntry).FindExact(
                        next->entry_coordinate);
            Snapshot& observed = diagnostic_snapshot_scratch_;
            if (expected != nullptr
                && diagnostic_image_a_ != nullptr
                && diagnostic_image_b_ != nullptr
                && checkpoint_capture_.CaptureTransient(
                    envelope.exit_coordinate, observed).ok())
            {
                auto& expected_image = *diagnostic_image_a_;
                auto& observed_image = *diagnostic_image_b_;
                if (CandidateCheckpointCodec::Decode(
                        *expected, expected_image).ok()
                    && CandidateCheckpointCodec::Decode(
                        observed, observed_image).ok())
                {
                    const auto difference =
                        CandidateDifferenceMask(expected_image, observed_image);
                    constexpr std::uint64_t expected_intertick_differences =
                        (std::uint64_t{1} << 1)
                        | (std::uint64_t{1} << 3)
                        | (std::uint64_t{1} << 15);
                    const auto material_difference =
                        difference & ~expected_intertick_differences;
                    if (material_difference != 0)
                    {
                        *first_interbatch_difference_mask = difference;
                        if (first_interbatch_expected_rng != nullptr)
                            *first_interbatch_expected_rng = expected_image.native.rng;
                        if (first_interbatch_observed_rng != nullptr)
                            *first_interbatch_observed_rng = observed_image.native.rng;
                        if (first_interbatch_expected_wind != nullptr)
                            *first_interbatch_expected_wind =
                                WindScheduleDiagnostic(expected_image.wind);
                        if (first_interbatch_observed_wind != nullptr)
                            *first_interbatch_observed_wind =
                                WindScheduleDiagnostic(observed_image.wind);
                        if (first_interbatch_expected_wind_graph != nullptr)
                            *first_interbatch_expected_wind_graph =
                                WindGraphDiagnostic(expected_image.wind);
                        if (first_interbatch_observed_wind_graph != nullptr)
                            *first_interbatch_observed_wind_graph =
                                WindGraphDiagnostic(observed_image.wind);
                    }
                    if (material_difference != 0
                        && first_interbatch_difference_batch != nullptr)
                        *first_interbatch_difference_batch = batch_index;
                    if (material_difference != 0
                        && first_interbatch_frame_difference_mask != nullptr)
                    {
                        const auto& a = expected_image.native.frame;
                        const auto& b = observed_image.native.frame;
                        if (a.frame_counter != b.frame_counter) *first_interbatch_frame_difference_mask |= 1;
                        if (a.input_game_round != b.input_game_round || a.input_game_time != b.input_game_time) *first_interbatch_frame_difference_mask |= 2;
                        if (a.manager_game_round_cursor != b.manager_game_round_cursor || a.manager_game_time_cursor != b.manager_game_time_cursor) *first_interbatch_frame_difference_mask |= 4;
                        if (a.round_state_frame != b.round_state_frame || a.unpause_countdown != b.unpause_countdown) *first_interbatch_frame_difference_mask |= 8;
                        if (a.previous_inputs != b.previous_inputs) *first_interbatch_frame_difference_mask |= 0x10;
                        if (a.input_pairs != b.input_pairs) *first_interbatch_frame_difference_mask |= 0x20;
                        if (a.prior_input_pairs != b.prior_input_pairs) *first_interbatch_frame_difference_mask |= 0x40;
                        if (a.repeat_pending != b.repeat_pending
                            || a.pending_move_state != b.pending_move_state
                            || a.pending_dispatch != b.pending_dispatch
                            || a.round_image_applied != b.round_image_applied)
                            *first_interbatch_frame_difference_mask |= 0x80;
                    }
                    if (material_difference != 0
                        && first_interbatch_local_difference != nullptr
                        && interbatch_local_difference_count != nullptr
                        && expected_image.local_images.size() == 2
                        && observed_image.local_images.size() == 2)
                    {
                        for (std::size_t image_index = 0;
                             image_index < 2; ++image_index)
                        {
                            auto* first = image_index == 0
                                ? first_interbatch_local_difference
                                : first_interbatch_motion_difference;
                            auto* count = image_index == 0
                                ? interbatch_local_difference_count
                                : interbatch_motion_difference_count;
                            if (first == nullptr || count == nullptr) continue;
                            const auto& a = expected_image.local_images[
                                image_index].bytes;
                            const auto& b = observed_image.local_images[
                                image_index].bytes;
                            const auto size = a.size() < b.size()
                                ? a.size() : b.size();
                            for (std::size_t index = 0; index < size; ++index)
                            {
                                if (a[index] != b[index])
                                {
                                    if (*first == UINT32_MAX)
                                        *first = static_cast<std::uint32_t>(index);
                                    ++*count;
                                }
                            }
                        }
                    }
                }
            }
        }
}

Status Sc6ReplayRuntime::ReplayOwnedBatchRange(
    std::size_t first_batch_index, std::size_t final_batch_index,
    std::uint64_t generation, DeterministicHookSet& hooks,
    std::optional<std::size_t> landing_batch_index,
    std::uint32_t landing_offset, Snapshot* landing,
    bool preserve_first_entry_input_log,
    std::uint64_t* replayed_coordinates, std::uint32_t* replayed_batches,
    std::size_t* failed_batch_index, NativeBatchEnvelope* failed_envelope,
    OwnedBatchReplayResult* failed_result,
    std::uint64_t* first_interbatch_difference_mask, std::size_t* first_interbatch_difference_batch,
    std::uint32_t* first_interbatch_frame_difference_mask, std::uint32_t* first_interbatch_local_difference,
    std::uint32_t* interbatch_local_difference_count, std::uint32_t* first_interbatch_motion_difference,
    std::uint32_t* interbatch_motion_difference_count, NativeRngImage* first_interbatch_expected_rng,
    NativeRngImage* first_interbatch_observed_rng,
    OwnedCorrectionResult::WindNodeScheduleDiagnostic* first_interbatch_expected_wind,
    OwnedCorrectionResult::WindNodeScheduleDiagnostic* first_interbatch_observed_wind,
    OwnedCorrectionResult::WindGraphScheduleDiagnostic* first_interbatch_expected_wind_graph,
    OwnedCorrectionResult::WindGraphScheduleDiagnostic* first_interbatch_observed_wind_graph,
    OwnedCorrectionResult* presentation_diagnostics,
    CorrectedReplayCapture* corrected) noexcept
{
    if (first_batch_index > final_batch_index || generation == 0
        || (landing_batch_index.has_value() && landing == nullptr)
        || (corrected != nullptr && corrected != &corrected_replay_capture_))
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    if (replayed_coordinates != nullptr) *replayed_coordinates = 0;
    if (replayed_batches != nullptr) *replayed_batches = 0;
    const InterbatchDiagnosticTargets interbatch_targets{
        first_interbatch_difference_mask,
        first_interbatch_difference_batch,
        first_interbatch_frame_difference_mask,
        first_interbatch_local_difference,
        interbatch_local_difference_count,
        first_interbatch_motion_difference,
        interbatch_motion_difference_count,
        first_interbatch_expected_rng,
        first_interbatch_observed_rng,
        first_interbatch_expected_wind,
        first_interbatch_observed_wind,
        first_interbatch_expected_wind_graph,
        first_interbatch_observed_wind_graph};
    NativeCameraSourceFrameImage corrected_camera_source{};
    bool corrected_camera_source_valid{};

    for (std::size_t batch_index = first_batch_index;
         batch_index <= final_batch_index; ++batch_index)
    {
        const NativeBatchEnvelope* envelope =
            batch_timeline_.GetBatch(batch_index);
        if (envelope == nullptr
            || envelope->entry_coordinate.generation != generation
            || envelope->exit_coordinate.generation != generation
            || envelope->coordinate_count
                > Schema::maximum_supported_native_batch_width)
        {
            return Status::failure(FailureCode::GenerationMismatch);
        }
        std::array<FrameCoordinate,
            Schema::maximum_supported_native_batch_width> coordinates{};
        std::array<InputPair,
            Schema::maximum_supported_native_batch_width> inputs{};
        for (std::uint32_t offset = 0;
             offset < envelope->coordinate_count; ++offset)
        {
            const NativeBatchCoordinate* member =
                batch_timeline_.GetBatchCoordinate(batch_index, offset);
            if (member == nullptr)
                return Status::failure(FailureCode::MissingInput);
            const auto input = input_timeline_.GetExact(member->coordinate);
            if (!input.has_value())
                return Status::failure(FailureCode::MissingInput);
            coordinates[offset] = member->coordinate;
            inputs[offset] = *input;
        }

        const bool capture_landing = landing_batch_index.has_value()
            && batch_index == *landing_batch_index;
        const auto& camera_source = corrected != nullptr
                && corrected_camera_source_valid
            ? corrected_camera_source : envelope->camera_source_frame;
        Status status = PrepareReplayBatchHandoffs(batch_index,
            first_batch_index, preserve_first_entry_input_log, *envelope,
            std::span{inputs.data(),
                static_cast<std::size_t>(envelope->coordinate_count)},
            corrected, camera_source, capture_landing, landing_offset);
        if (!status.ok()) return status;
        OwnedLandingCapture landing_capture{&checkpoint_capture_, landing};
        OuterTickObservation corrected_observation{};
        std::array<InputPair,
            Schema::maximum_supported_native_batch_width> corrected_inputs{};
        CorrectedCoordinateCapture corrected_capture{
            this, std::span{corrected_inputs.data(),
                static_cast<std::size_t>(envelope->coordinate_count)}};
        OwnedBatchReplayRequest request{};
        request.battle_manager = timeline_manager_;
        request.owner_thread_id = timeline_thread_id_;
        request.envelope = envelope;
        request.coordinates = std::span{coordinates.data(),
            static_cast<std::size_t>(envelope->coordinate_count)};
        request.inputs = std::span{inputs.data(),
            static_cast<std::size_t>(envelope->coordinate_count)};
        request.suppress_ephemeral_presentation = true;
        if (corrected != nullptr)
        {
            if (corrected->batch_count >= corrected->batch_indices.size())
                return Status::failure(FailureCode::CapacityExceeded);
            request.presentation_mode =
                OwnedBatchPresentationMode::CaptureCorrected;
            request.corrected_inputs = corrected_capture.inputs;
            request.corrected_observation = &corrected_observation;
            request.coordinate_capture_user = &corrected_capture;
            request.capture_coordinate = CaptureCorrectedCoordinate;
        }
        if (capture_landing)
        {
            request.landing_offset = landing_offset;
            request.landing_user = &landing_capture;
            request.capture_landing = CaptureOwnedLanding;
        }
        OwnedBatchReplayResult result{};
        status = hooks.ExecuteOwnedBatch(request, result);
        if (corrected == nullptr && status.ok())
            status = ValidateReplayedPresentationEnvelope(*envelope, result);
        AccumulateReplayPresentationDiagnostics(result,
            presentation_diagnostics);
        if (!status.ok() || (capture_landing && !result.landing_captured))
        {
            if (failed_batch_index != nullptr) *failed_batch_index = batch_index;
            if (failed_envelope != nullptr) *failed_envelope = *envelope;
            if (failed_result != nullptr) *failed_result = result;
            return status.ok()
                ? Status::failure(FailureCode::CaptureFailed) : status;
        }
        if (corrected != nullptr)
        {
            status = CaptureCorrectedReplayBatch(batch_index, *envelope,
                camera_source, corrected_observation, corrected_camera_source,
                corrected_camera_source_valid, corrected);
            if (!status.ok()) return status;
        }
        if (replayed_coordinates != nullptr)
            *replayed_coordinates += envelope->coordinate_count;
        if (replayed_batches != nullptr) ++*replayed_batches;
        CaptureInterbatchDiagnostics(
            batch_index, final_batch_index, *envelope, interbatch_targets);
    }
    return Status::success();
}
