Status Sc6ReplayRuntime::Initialize(
    std::uintptr_t image_base, UcrtRandBroker* ucrt_broker) noexcept
{
    Shutdown();
    if (!Sc6ReplayNativeBridge::ValidateMoveStateSetter(image_base))
    {
        return Status::failure(FailureCode::AdapterUnqualified);
    }

    Sc6ReplayResolvers resolvers{};
    resolvers.user = this;
    resolvers.replay_player = ResolveReplayPlayer;
    resolvers.battle_manager = ResolveBattleManager;
    resolvers.fighter_one = ResolveFighterOne;
    resolvers.fighter_two = ResolveFighterTwo;
    resolvers.stage = ResolveStage;
    resolvers.set_move_state = reinterpret_cast<SetBattleManagerMoveStateFn>(
        image_base + Schema::Sc6ReplayLayout::set_move_state_rva);
    resolvers.set_move_state_signature_valid = true;
    bridge_.emplace(resolvers);
    try
    {
        diagnostic_image_a_ = std::make_unique<CandidateCheckpointImage>();
        diagnostic_image_b_ = std::make_unique<CandidateCheckpointImage>();
        pending_batch_coordinates_.reserve(
            Schema::maximum_supported_native_batch_width);
    }
    catch (...)
    {
        Shutdown();
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return checkpoint_capture_.Initialize(image_base, ucrt_broker);
}

void Sc6ReplayRuntime::Shutdown() noexcept
{
    presentation_controller_.EndGeneration();
    presentation_ownership_enabled_ = false;
    bridge_.reset();
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
    diagnostic_snapshot_scratch_ = {};
    diagnostic_image_a_.reset();
    diagnostic_image_b_.reset();
    checkpoint_capture_.Reset();
    timeline_status_ = {};
    timeline_manager_ = 0;
    timeline_input_log_ = 0;
    timeline_thread_id_ = 0;
    timeline_session_generation_ = 0;
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
    generation_rebaseline_pending_ = false;
    continuing_session_rebaseline_ = false;
    replay_history_capture_required_ = true;
    next_replay_history_capture_required_ = true;
}

bool Sc6ReplayRuntime::ready() const noexcept
{
    return bridge_.has_value();
}

IReplayNativeBridge* Sc6ReplayRuntime::bridge() noexcept
{
    return bridge_ ? &*bridge_ : nullptr;
}

bool Sc6ReplayRuntime::ObserveCurrentSimulationPhase(
    std::int32_t& native_round, std::int32_t& native_time,
    std::uint32_t& round_state_frame,
    std::int32_t& unpause_countdown) noexcept
{
    // The outer-tick hook is the authoritative owner of the active native
    // BattleManager lifetime. Scene-singleton discovery can transiently return
    // null during persistent ReplayBattleScene re-entry even after that hook
    // has admitted and observed the new manager. Prefer the admitted identity;
    // retain discovery only for setup before the first admitted outer tick.
    const std::uintptr_t admitted_manager = timeline_manager_;
    const Obj discovered_manager = admitted_manager == 0
        ? lux_.battleManager() : Obj{};
    const auto manager = reinterpret_cast<const std::byte*>(
        admitted_manager != 0 ? admitted_manager :
            reinterpret_cast<std::uintptr_t>(discovered_manager.raw()));
    if (manager == nullptr) return false;
    void* input_log{};
    if (!SafeReadPtr(manager + Schema::Sc6FrameLayout::manager_input_log,
            &input_log)
        || input_log == nullptr
        || !SafeReadInt32(static_cast<const std::byte*>(input_log)
                + Schema::Sc6FrameLayout::input_log_game_round,
            &native_round)
        || !SafeReadInt32(static_cast<const std::byte*>(input_log)
                + Schema::Sc6FrameLayout::input_log_game_time,
            &native_time)
        || !SafeReadUInt32(manager
                + Schema::Sc6FrameLayout::manager_round_state_frame,
            &round_state_frame)
        || !SafeReadInt32(manager
                + Schema::Sc6FrameLayout::manager_unpause_countdown,
            &unpause_countdown))
    {
        return false;
    }
    return true;
}

void Sc6ReplayRuntime::SetForcedDepth7QualificationEnabled(
    bool enabled) noexcept
{
    forced_depth7_qualification_enabled_ = enabled;
    forced_qualification_snapshots_.Clear();
    correction_undo_scratch_ = {};
    correction_verified_scratch_ = {};
    correction_canonical_capture_scratch_ = {};
    timeline_canonical_capture_scratch_ = {};
}

void Sc6ReplayRuntime::SetCorrectedInputQualificationEnabled(
    bool enabled) noexcept
{
    corrected_input_qualification_enabled_ = enabled;
}

Status Sc6ReplayRuntime::SetReplayHistoryCaptureRequired(
    bool required) noexcept
{
    // ReplayQualificationMod can publish the next request while the prior
    // replay is still leaving ReplayBattleScene. Record that next-lifetime
    // policy immediately, but never mutate storage ownership underneath an
    // admitted timeline.
    next_replay_history_capture_required_ = required;
    if (timeline_status_.last_coordinate.generation != 0
        || pending_batch_id_ != 0 || resume_validation_active_)
    {
        return Status::success();
    }
    replay_history_capture_required_ = required;
    if (!required) checkpoint_capture_.ReleaseHistoryStorage();
    return Status::success();
}

Status Sc6ReplayRuntime::SetOnlinePredictedRemotePlayer(
    std::optional<std::size_t> player_index) noexcept
{
    if (player_index.has_value() && *player_index >= 2)
        return Status::failure(FailureCode::InvalidConfiguration);
    if (pending_batch_id_ != 0 || resume_validation_active_)
        return Status::failure(FailureCode::IllegalTransition);
    online_predicted_remote_player_ = player_index;
    return Status::success();
}

Status Sc6ReplayRuntime::PrepareOnlineOwnedStorage(
    FrameCoordinate baseline) noexcept
{
    const Snapshot* prototype = checkpoint_capture_.snapshots(
        CandidateCheckpointRole::BatchEntry).FindNearestAtOrBefore(baseline);
    if (prototype == nullptr)
        prototype = checkpoint_capture_.snapshots(
            CandidateCheckpointRole::Landing).FindNearestAtOrBefore(baseline);
    if (prototype == nullptr)
        return Status::failure(FailureCode::MissingSnapshot);
    try
    {
        correction_undo_scratch_ = *prototype;
        correction_verified_scratch_ = *prototype;
        correction_canonical_capture_scratch_ = *prototype;
        timeline_canonical_capture_scratch_ = *prototype;
        diagnostic_snapshot_scratch_ = *prototype;
        for (auto& snapshot : corrected_replay_capture_.replacement_landing)
            snapshot = *prototype;
        for (auto& snapshot : corrected_replay_capture_.replacement_batch_entry)
            snapshot = *prototype;
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    if (diagnostic_image_a_ == nullptr || diagnostic_image_b_ == nullptr)
        return Status::failure(FailureCode::CapacityExceeded);
    auto status = CandidateCheckpointCodec::Decode(
        *prototype, *diagnostic_image_a_);
    if (status.ok()) status = CandidateCheckpointCodec::Decode(
        *prototype, *diagnostic_image_b_);
    corrected_replay_capture_.Clear();
    return status;
}

std::size_t Sc6ReplayRuntime::forced_qualification_bytes() const noexcept
{
    return forced_qualification_snapshots_.BytesUsed();
}

void Sc6ReplayRuntime::ResetCapturePerformanceWindow() noexcept
{
    checkpoint_capture_.ResetCapturePerformanceWindow();
}

CandidateAdapterPerformanceStatus
Sc6ReplayRuntime::capture_performance() const noexcept
{
    return checkpoint_capture_.adapter_performance();
}

Status Sc6ReplayRuntime::PrepareInitialGeneration(
    const OuterTickObservation& observation) noexcept
{
    if (timeline_manager_ != 0)
        return Status::success();
    if (observation.battle_manager == 0 || observation.before.input_log == 0)
        return Status::failure(FailureCode::ContextUnavailable);

    if (!continuing_session_rebaseline_) ++timeline_session_generation_;
    continuing_session_rebaseline_ = false;
    ++timeline_status_.generations;
    timeline_status_.sessions = timeline_session_generation_;
    timeline_status_.native_round = observation.before.input_game_round;
    timeline_status_.native_time = observation.before.input_game_time;
    timeline_status_.last_coordinate = {
        timeline_status_.generations, observation.before.frame_counter};
    timeline_manager_ = observation.battle_manager;
    timeline_input_log_ = observation.before.input_log;
    timeline_thread_id_ = observation.thread_id;
    return Status::success();
}

Status Sc6ReplayRuntime::BeginObservedFrame(
    const FrameFencepostObservation& observation,
    FrameCoordinate& coordinate, bool& new_generation) noexcept
{
    if (observation.read_mask
        != Schema::Sc6FrameLayout::required_observation_read_mask)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_thread_id_ != 0 && timeline_thread_id_ != observation.thread_id)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_status_.failure != FailureCode::None)
    {
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_status_.partial)
    {
        return Status::success();
    }
    if (observation.outer_batch_id == 0)
    {
        timeline_status_.identity_issue = 1;
        timeline_status_.identity_observed = observation.outer_batch_id;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (pending_batch_id_ != observation.outer_batch_id)
    {
        timeline_status_.identity_issue = 2;
        timeline_status_.identity_expected = pending_batch_id_;
        timeline_status_.identity_observed = observation.outer_batch_id;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (!observation.input_filter_observed
        || observation.input_filter_invocations == 0)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    if (!resume_validation_active_ && !batch_timeline_.CanAppendBatch(
            pending_batch_coordinates_.size() + 1))
    {
        timeline_status_.partial = true;
        pending_batch_id_ = 0;
        pending_camera_source_frame_ = {};
        pending_batch_coordinates_.clear();
        return Status::success();
    }
    timeline_thread_id_ = observation.thread_id;

    const bool new_session = timeline_manager_ == 0;
    new_generation = !resume_validation_active_ && (new_session
        || timeline_manager_ != observation.battle_manager
        || timeline_input_log_ != observation.input_log
        || timeline_status_.native_round != observation.game_round
        || (timeline_status_.captured_frames != 0
            && observation.frame_counter
                <= timeline_status_.last_coordinate.frame));
    if (new_generation)
    {
        if (new_session)
        {
            ++timeline_session_generation_;
            timeline_status_.sessions = timeline_session_generation_;
        }
        ++timeline_status_.generations;
        timeline_manager_ = observation.battle_manager;
        timeline_input_log_ = observation.input_log;
    }
    else if (observation.frame_counter
             != timeline_status_.last_coordinate.frame + 1)
    {
        timeline_status_.failure = FailureCode::AdvanceFailed;
        return Status::failure(timeline_status_.failure);
    }

    coordinate = FrameCoordinate{
        timeline_status_.generations, observation.frame_counter};
    if (resume_validation_active_
        && (coordinate.generation != resume_source_end_.generation
            || coordinate <= timeline_status_.last_coordinate
            || coordinate > resume_source_end_))
    {
        timeline_status_.failure = FailureCode::AdvanceFailed;
        return Status::failure(timeline_status_.failure);
    }
    if (!new_generation && timeline_status_.captured_frames != 0
        && observation.game_time == timeline_status_.native_time)
        ++timeline_status_.same_native_time_coordinates;
    if (observation.repeat_pending != 0)
        ++timeline_status_.repeat_requests;
    ++timeline_status_.input_filter_observations;
    if (observation.pre_filter_inputs[0] != observation.inputs[0]
        || observation.pre_filter_inputs[1] != observation.inputs[1])
    {
        ++timeline_status_.input_filter_mutations;
    }
    if (observation.input_filter_invocations
        > timeline_status_.maximum_input_filter_invocation_ordinal)
    {
        timeline_status_.maximum_input_filter_invocation_ordinal =
            observation.input_filter_invocations;
    }
    const bool cursor_mismatch =
        observation.manager_game_round_cursor != observation.game_round
        || observation.manager_game_time_cursor
            != static_cast<std::uint32_t>(observation.game_time);
    const auto record_cursor_mismatch = [&]() noexcept {
        ++timeline_status_.cursor_mismatches;
        timeline_status_.last_cursor_mismatch_coordinate = coordinate;
        timeline_status_.last_cursor_mismatch_input_round =
            observation.game_round;
        timeline_status_.last_cursor_mismatch_input_time =
            observation.game_time;
        timeline_status_.last_cursor_mismatch_manager_round =
            observation.manager_game_round_cursor;
        timeline_status_.last_cursor_mismatch_manager_time =
            observation.manager_game_time_cursor;
        timeline_status_.last_cursor_mismatch_pending_dispatch =
            observation.pending_dispatch;
        timeline_status_.last_cursor_mismatch_round_image_applied =
            observation.round_image_applied;
        timeline_status_.last_cursor_mismatch_round_state =
            observation.round_state;
    };
    const bool round_reset_publication_barrier = new_generation
        && observation.round_state == 1
        && observation.game_time == 0
        && static_cast<std::int64_t>(observation.game_round)
            == static_cast<std::int64_t>(
                observation.manager_game_round_cursor) + 1
        && observation.pending_dispatch == 0
        && observation.round_image_applied == 0;
    if (observation.round_image_applied == 1)
    {
        // Native move-state 4 deliberately applies the next-round image, sets
        // +0x1464/+0x1465, and forces one simulation traversal with the
        // manager's consumed-time cursor reset to zero. This is the exact
        // round-image barrier, not an accounting failure. Both companion
        // values are native postconditions of that branch and fail closed if
        // they do not agree.
        ++timeline_status_.round_transition_cursor_barriers;
        if (observation.pending_dispatch != 1
            || observation.manager_game_time_cursor != 0)
            record_cursor_mismatch();
    }
    else if (round_reset_publication_barrier)
    {
        // Round-state ID 1 synchronously broadcasts manager collection
        // +0xB80. Its bound FrameInputLog delegate resets the InputLog to the
        // next round at time zero before this post-coordinate fencepost, while
        // the manager cursors still describe the traversal just consumed.
        // The next worker tick consumes the newly published round image.
        ++timeline_status_.round_transition_cursor_barriers;
    }
    else if (observation.round_image_applied != 0 || cursor_mismatch)
    {
        record_cursor_mismatch();
    }
    return Status::success();
}

Status Sc6ReplayRuntime::AppendObservedInput(
    const FrameFencepostObservation& observation,
    FrameCoordinate coordinate) noexcept
{
    InputPair inputs{};
    inputs.players[0] = observation.pre_filter_inputs[0];
    inputs.players[1] = observation.pre_filter_inputs[1];
    inputs.post_filter_players[0] = observation.inputs[0];
    inputs.post_filter_players[1] = observation.inputs[1];
    inputs.source_rows[0] = observation.source_rows[0];
    inputs.source_rows[1] = observation.source_rows[1];
    inputs.input_update_time = observation.input_update_time;
    inputs.remote_confirmed = !online_predicted_remote_player_.has_value();
    inputs.post_filter_observed = true;
    inputs.source_rows_observed = observation.source_rows_observed;
    if (resume_validation_active_)
    {
        const auto expected_input = input_timeline_.GetExact(coordinate);
        if (!expected_input.has_value())
        {
            timeline_status_.failure = FailureCode::MissingInput;
            return Status::failure(timeline_status_.failure);
        }
        std::uint32_t issue{};
        if (expected_input->players[0] != inputs.players[0]
            || expected_input->players[1] != inputs.players[1]) issue |= 1;
        if (expected_input->post_filter_players[0]
                != inputs.post_filter_players[0]
            || expected_input->post_filter_players[1]
                != inputs.post_filter_players[1]) issue |= 2;
        if (expected_input->source_rows[0] != inputs.source_rows[0]
            || expected_input->source_rows[1] != inputs.source_rows[1])
            issue |= 4;
        if (expected_input->input_update_time != inputs.input_update_time)
            issue |= 8;
        if (issue != 0)
        {
            timeline_status_.identity_issue = 200 + issue;
            timeline_status_.identity_expected =
                (static_cast<std::uint64_t>(expected_input->players[0].held) << 32)
                | expected_input->players[1].held;
            timeline_status_.identity_observed =
                (static_cast<std::uint64_t>(inputs.players[0].held) << 32)
                | inputs.players[1].held;
        }
    }
    const auto existing_input = input_timeline_.GetExact(coordinate);
    const Status appended = input_timeline_.AppendAuthoritative(coordinate, inputs);
    if (!appended.ok())
    {
        if (appended.code == FailureCode::CapacityExceeded)
        {
            timeline_status_.partial = true;
            return Status::success();
        }
        if (appended.code == FailureCode::IdentityMismatch
            && existing_input.has_value())
        {
            timeline_status_.identity_issue = 12;
            timeline_status_.identity_expected =
                (static_cast<std::uint64_t>(
                    existing_input->post_filter_players[0].held) << 32)
                | existing_input->post_filter_players[1].held;
            timeline_status_.identity_observed =
                (static_cast<std::uint64_t>(inputs.post_filter_players[0].held)
                    << 32)
                | inputs.post_filter_players[1].held;
        }
        timeline_status_.failure = appended.code;
        return appended;
    }
    try
    {
        pending_batch_coordinates_.push_back(coordinate);
    }
    catch (...)
    {
        timeline_status_.partial = true;
        pending_batch_id_ = 0;
        pending_camera_source_frame_ = {};
        pending_batch_coordinates_.clear();
        return Status::success();
    }
    timeline_status_.last_coordinate = coordinate;
    timeline_status_.native_round = observation.game_round;
    timeline_status_.native_time = observation.game_time;
    timeline_status_.round_state_frame = observation.round_state_frame;
    timeline_status_.unpause_countdown = observation.unpause_countdown;
    timeline_status_.pending_move_state = observation.pending_move_state;
    return Status::success();
}

Status Sc6ReplayRuntime::ValidateResumedFrame(
    FrameCoordinate coordinate) noexcept
{
    if (resume_validation_active_)
    {
        const auto expected = canonical_timeline_.GetExact(coordinate);
        if (!expected.has_value())
        {
            timeline_status_.failure = FailureCode::MissingSnapshot;
            return Status::failure(timeline_status_.failure);
        }
        Snapshot& observed = timeline_canonical_capture_scratch_;
        const Status captured = checkpoint_capture_.CaptureCanonical(
            coordinate, observed);
        if (!captured.ok())
        {
            timeline_status_.identity_issue =
                100 + checkpoint_capture_.transient_identity_issue();
            timeline_status_.identity_expected =
                checkpoint_capture_.transient_identity_expected();
            timeline_status_.identity_observed =
                checkpoint_capture_.transient_identity_observed();
            timeline_status_.canonical_capture_phase =
                checkpoint_capture_.transient_capture_phase();
            timeline_status_.canonical_animation_topology_issue =
                checkpoint_capture_.transient_animation_topology_issue();
            timeline_status_.canonical_animation_topology_observed =
                checkpoint_capture_.transient_animation_topology_observed();
            timeline_status_.canonical_capture_failure_coordinate = coordinate;
            timeline_status_.failure = captured.code;
            return captured;
        }
        if (observed.canonical_hash != expected->hash)
        {
            timeline_status_.resume_failure_coordinate = coordinate;
            timeline_status_.resume_expected_hash = expected->hash;
            timeline_status_.resume_observed_hash = observed.canonical_hash;
            timeline_status_.resume_component_difference_mask = 0;
            for (std::size_t index = 0;
                 index < expected->components.size(); ++index)
            {
                if (expected->components[index]
                    != observed.canonical_components[index])
                {
                    timeline_status_.resume_component_difference_mask
                        |= std::uint32_t{1} << index;
                }
            }
            timeline_status_.resume_wind_difference_mask = 0;
            for (std::size_t index = 0; index < expected->wind.size(); ++index)
            {
                if (expected->wind[index] != observed.canonical_wind[index])
                {
                    timeline_status_.resume_wind_difference_mask
                        |= std::uint32_t{1} << index;
                }
            }
            timeline_status_.resume_expected_wind_node = expected->wind_node;
            timeline_status_.resume_observed_wind_node =
                observed.canonical_wind_node;
            timeline_status_.resume_first_wind_semantic_chunk = UINT32_MAX;
            for (std::size_t index = 0;
                 index < expected->wind_semantic.size(); ++index)
            {
                if (expected->wind_semantic[index]
                    != observed.canonical_wind_semantic[index])
                {
                    timeline_status_.resume_first_wind_semantic_chunk =
                        static_cast<std::uint32_t>(index);
                    break;
                }
            }
            timeline_status_.failure = FailureCode::StateHashMismatch;
            return Status::failure(timeline_status_.failure);
        }
        ++timeline_status_.resumed_frames_verified;
        if (coordinate == resume_source_end_) resume_catchup_pending_ = true;
        return Status::success();
    }
    return Status::success();
}

void Sc6ReplayRuntime::CaptureLandingCheckpoint(
    const FrameFencepostObservation& observation,
    FrameCoordinate coordinate, bool new_generation) noexcept
{
    ++timeline_status_.captured_frames;
    const bool retain_history = replay_history_capture_required_
        || forced_depth7_qualification_enabled_
        || online_predicted_remote_player_.has_value();
    if (!retain_history) return;
    const auto* batch_entry = checkpoint_capture_.snapshots(
        CandidateCheckpointRole::BatchEntry).FindNearestAtOrBefore(coordinate);
    if (batch_entry == nullptr)
    {
        ++timeline_status_.coordinates_without_batch_entry_checkpoint;
    }
    else
    {
        const std::uint64_t distance =
            coordinate.frame - batch_entry->coordinate.frame;
        if (distance
            > timeline_status_.maximum_resim_distance_from_batch_entry)
        {
            timeline_status_.maximum_resim_distance_from_batch_entry = distance;
        }
    }
    if (timeline_status_.captured_frames == 1 || new_generation
        || coordinate.frame % Schema::checkpoint_interval == 0)
    {
        const Status checkpoint = checkpoint_capture_.Capture(
            CandidateCheckpointRole::Landing,
            observation.battle_manager,
            coordinate,
            timeline_session_generation_,
            observation.thread_id);
        const auto checkpoint_status = checkpoint_capture_.status(
            CandidateCheckpointRole::Landing);
        timeline_status_.captured_checkpoints = checkpoint_status.captured;
        timeline_status_.checkpoint_bytes = checkpoint_status.bytes_used;
        timeline_status_.checkpoint_wind_nodes = checkpoint_status.wind_node_count;
        timeline_status_.checkpoint_capture_samples = checkpoint_status.capture_samples;
        timeline_status_.checkpoint_capture_max_ns = checkpoint_status.capture_max_ns;
        timeline_status_.checkpoint_capture_p99_ns = checkpoint_status.capture_p99_ns;
        timeline_status_.checkpoint_store_max_ns = checkpoint_status.store_max_ns;
        timeline_status_.checkpoint_store_p99_ns = checkpoint_status.store_p99_ns;
        timeline_status_.checkpoint_adapter_performance =
            checkpoint_status.adapter_performance;
        timeline_status_.checkpoint_failure = checkpoint.ok()
            ? FailureCode::None : checkpoint.code;
        timeline_status_.checkpoint_validation = checkpoint_status.validation;
        timeline_status_.checkpoint_animation_topology_issue =
            checkpoint_status.animation_topology_issue;
        timeline_status_.checkpoint_capture_phase =
            checkpoint_status.capture_phase;
        timeline_status_.checkpoint_animation_observed =
            checkpoint_status.animation_topology_observed;
        timeline_status_.checkpoint_animation_fighters =
            checkpoint_status.animation_fighters;
        if (checkpoint.code == FailureCode::CapacityExceeded)
            timeline_status_.partial = true;
        else if (checkpoint.code == FailureCode::IdentityMismatch
            || checkpoint.code == FailureCode::GenerationMismatch)
        {
            generation_rebaseline_pending_ = true;
            timeline_status_.checkpoint_failure = FailureCode::None;
        }
    }
}

Status Sc6ReplayRuntime::CaptureCanonicalFrame(
    FrameCoordinate coordinate, bool new_generation) noexcept
{
    if (!generation_rebaseline_pending_)
    {
        Snapshot& canonical = timeline_canonical_capture_scratch_;
        if (forced_depth7_qualification_enabled_)
            static_cast<void>(
                forced_qualification_snapshots_.TakeOldestIfFull(canonical));
        const Status captured = forced_depth7_qualification_enabled_
            ? checkpoint_capture_.CaptureTransient(coordinate, canonical)
            : checkpoint_capture_.CaptureCanonical(coordinate, canonical);
        if (!captured.ok())
        {
            timeline_status_.identity_issue =
                100 + checkpoint_capture_.transient_identity_issue();
            timeline_status_.identity_expected =
                checkpoint_capture_.transient_identity_expected();
            timeline_status_.identity_observed =
                checkpoint_capture_.transient_identity_observed();
            timeline_status_.canonical_capture_phase =
                checkpoint_capture_.transient_capture_phase();
            timeline_status_.canonical_animation_topology_issue =
                checkpoint_capture_.transient_animation_topology_issue();
            timeline_status_.canonical_animation_topology_observed =
                checkpoint_capture_.transient_animation_topology_observed();
            timeline_status_.canonical_capture_failure_coordinate = coordinate;
            if (captured.code == FailureCode::IdentityMismatch
                || captured.code == FailureCode::GenerationMismatch)
            {
                generation_rebaseline_pending_ = true;
                return Status::success();
            }
            timeline_status_.failure = captured.code;
            return captured;
        }
        const Status stored = canonical_timeline_.Append(
            coordinate, canonical.canonical_hash,
            canonical.canonical_components, canonical.canonical_native,
            canonical.canonical_move_dispatch,
            canonical.canonical_input,
            canonical.canonical_wind_semantic,
            canonical.canonical_wind,
            canonical.canonical_wind_node);
        timeline_status_.canonical_frames = archived_canonical_frames_
            + canonical_timeline_.size();
        timeline_status_.canonical_hash_bytes = canonical_timeline_.bytes_used();
        if (stored.code == FailureCode::CapacityExceeded)
        {
            timeline_status_.partial = true;
            return Status::success();
        }
        if (!stored.ok())
        {
            timeline_status_.identity_issue = 13;
            timeline_status_.canonical_capture_failure_coordinate = coordinate;
            timeline_status_.failure = stored.code;
            return stored;
        }
        const auto movevm_short25 =
            checkpoint_capture_.last_captured_movevm_short25();
        const auto movevm_state_shorts =
            checkpoint_capture_.last_captured_movevm_state_shorts();
        const auto rng = checkpoint_capture_.last_captured_rng();
        if (!timeline_status_.movevm_short25_initial_recorded)
        {
            timeline_status_.initial_movevm_short25 = movevm_short25;
            timeline_status_.movevm_short25_initial_recorded = true;
        }
        timeline_status_.final_movevm_short25 = movevm_short25;
        timeline_status_.final_gameplay_xorshift_state = rng.xorshift;
        if (last_movevm_short25_valid_ && !new_generation)
        {
            for (std::size_t fighter = 0;
                 fighter < movevm_short25.size(); ++fighter)
            {
                if (movevm_short25[fighter] == last_movevm_short25_[fighter])
                    continue;
                ++timeline_status_.observed_movevm_short25_changes[fighter];
                pending_movevm_short25_change_mask_ |=
                    static_cast<std::uint8_t>(1u << fighter);
                pending_movevm_short25_before_[fighter] =
                    last_movevm_short25_[fighter];
                pending_movevm_short25_after_[fighter] =
                    movevm_short25[fighter];
                auto& sequence_hash = timeline_status_
                    .observed_movevm_short25_sequence_hash[fighter];
                AppendFnv64(sequence_hash, &coordinate.generation,
                    sizeof(coordinate.generation));
                AppendFnv64(sequence_hash, &coordinate.frame,
                    sizeof(coordinate.frame));
                AppendFnv64(sequence_hash, &fighter, sizeof(fighter));
                AppendFnv64(sequence_hash, &last_movevm_short25_[fighter],
                    sizeof(last_movevm_short25_[fighter]));
                AppendFnv64(sequence_hash, &movevm_short25[fighter],
                    sizeof(movevm_short25[fighter]));
            }
            for (std::size_t fighter = 0;
                 fighter < movevm_state_shorts.fighters.size(); ++fighter)
            {
                for (std::size_t index = 0;
                     index < movevm_state_shorts.fighters[fighter].size();
                     ++index)
                {
                    if (movevm_state_shorts.fighters[fighter][index]
                        == last_movevm_state_shorts_.fighters[fighter][index])
                        continue;
                    ++timeline_status_.observed_movevm_state_changes[fighter];
                    pending_movevm_state_short_change_masks_[fighter]
                        [index / 64] |= std::uint64_t{1} << (index % 64);
                }
            }
        }
        last_movevm_short25_ = movevm_short25;
        last_movevm_state_shorts_ = movevm_state_shorts;
        last_movevm_short25_valid_ = true;
        if (forced_depth7_qualification_enabled_)
        {
            const Status retained =
                forced_qualification_snapshots_.Save(std::move(canonical));
            if (!retained.ok())
            {
                timeline_status_.identity_issue = 14;
                timeline_status_.canonical_capture_failure_coordinate = coordinate;
                timeline_status_.failure = retained.code;
                return retained;
            }
        }
    }
    return Status::success();
}

Status Sc6ReplayRuntime::ObserveFrame(
    const FrameFencepostObservation& observation) noexcept
{
    FrameCoordinate coordinate{};
    bool new_generation{};
    auto status = BeginObservedFrame(observation, coordinate, new_generation);
    if (!status.ok() || timeline_status_.partial) return status;
    status = AppendObservedInput(observation, coordinate);
    if (!status.ok() || timeline_status_.partial) return status;
    if (resume_validation_active_) return ValidateResumedFrame(coordinate);
    CaptureLandingCheckpoint(observation, coordinate, new_generation);
    return CaptureCanonicalFrame(coordinate, new_generation);
}

Status Sc6ReplayRuntime::ObserveOuterTickBegin(
    const OuterTickObservation& observation) noexcept
{
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    if (timeline_status_.partial)
        return Status::success();
    constexpr std::uint16_t required_begin_reads = 0x0f;
    if ((observation.read_mask & required_begin_reads) != required_begin_reads)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.before.main_state != 2)
        return Status::success();
    if (observation.batch_id == 0 || pending_batch_id_ != 0)
    {
        timeline_status_.identity_issue = 3;
        timeline_status_.identity_expected = 0;
        timeline_status_.identity_observed = pending_batch_id_;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_thread_id_ != 0
        && timeline_thread_id_ != observation.thread_id)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_manager_ != 0
        && timeline_manager_ != observation.battle_manager)
    {
        timeline_status_.identity_issue = 4;
        timeline_status_.identity_expected = timeline_manager_;
        timeline_status_.identity_observed = observation.battle_manager;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    const Status initialized = PrepareInitialGeneration(observation);
    if (!initialized.ok())
    {
        timeline_status_.failure = initialized.code;
        return initialized;
    }
    pending_batch_id_ = observation.batch_id;
    pending_batch_entry_ = timeline_status_.last_coordinate;
    pending_camera_source_frame_ = {};
    pending_movevm_short25_change_mask_ = 0;
    pending_movevm_short25_before_ = {};
    pending_movevm_short25_after_ = {};
    pending_movevm_state_short_change_masks_ = {};

    // A resumed future reuses the immutable baseline checkpoints and batch
    // envelopes. Capturing a second batch-entry image here would both waste
    // the bounded store and violate its strictly increasing coordinate order.
    if (resume_validation_active_)
    {
        if (observation.before.frame_counter
            != timeline_status_.last_coordinate.frame)
        {
            timeline_status_.identity_issue = 5;
            timeline_status_.identity_expected =
                timeline_status_.last_coordinate.frame;
            timeline_status_.identity_observed =
                observation.before.frame_counter;
            timeline_status_.failure = FailureCode::IdentityMismatch;
            return Status::failure(timeline_status_.failure);
        }
        return Status::success();
    }

    const FrameCoordinate coordinate = timeline_status_.last_coordinate;
    if (coordinate.generation == 0)
        return Status::success();
    const bool retain_history = replay_history_capture_required_
        || forced_depth7_qualification_enabled_
        || online_predicted_remote_player_.has_value();
    if (!retain_history)
    {
        const Status bound = checkpoint_capture_.BindForCanonicalCapture(
            observation.battle_manager, coordinate,
            timeline_session_generation_, observation.thread_id);
        if (!bound.ok())
        {
            timeline_status_.failure = bound.code;
            return bound;
        }
        return CapturePendingCameraSource();
    }
    const auto previous = checkpoint_capture_.status(
        CandidateCheckpointRole::BatchEntry);
    const std::optional<FrameCoordinate> previous_coordinate = previous.captured == 0
        ? std::nullopt
        : std::optional<FrameCoordinate>{previous.last_coordinate};
    const auto action = PlanResimulationBase(
        previous_coordinate,
        coordinate,
        Schema::maximum_supported_native_batch_width,
        Schema::checkpoint_interval - 1);
    if (action == ResimulationBaseAction::Invalid)
    {
        timeline_status_.identity_issue = 6;
        timeline_status_.identity_expected = previous.captured == 0
            ? 0 : previous.last_coordinate.frame;
        timeline_status_.identity_observed = coordinate.frame;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (action == ResimulationBaseAction::Retain)
        return CapturePendingCameraSource();
    const Status captured = checkpoint_capture_.Capture(
        CandidateCheckpointRole::BatchEntry,
        observation.battle_manager,
        coordinate,
        timeline_session_generation_,
        observation.thread_id);
    const auto status = checkpoint_capture_.status(
        CandidateCheckpointRole::BatchEntry);
    timeline_status_.captured_batch_entry_checkpoints = status.captured;
    timeline_status_.batch_entry_checkpoint_bytes = status.bytes_used;
    timeline_status_.batch_entry_wind_nodes = status.wind_node_count;
    timeline_status_.batch_entry_capture_samples = status.capture_samples;
    timeline_status_.batch_entry_capture_max_ns = status.capture_max_ns;
    timeline_status_.batch_entry_capture_p99_ns = status.capture_p99_ns;
    timeline_status_.batch_entry_store_max_ns = status.store_max_ns;
    timeline_status_.batch_entry_store_p99_ns = status.store_p99_ns;
    timeline_status_.batch_entry_adapter_performance = status.adapter_performance;
    timeline_status_.batch_entry_checkpoint_failure = captured.ok()
        ? FailureCode::None : captured.code;
    timeline_status_.batch_entry_checkpoint_validation = status.validation;
    timeline_status_.batch_entry_animation_topology_issue =
        status.animation_topology_issue;
    timeline_status_.batch_entry_capture_phase = status.capture_phase;
    timeline_status_.batch_entry_animation_observed =
        status.animation_topology_observed;
    timeline_status_.batch_entry_animation_fighters =
        status.animation_fighters;
    if (captured.code == FailureCode::CapacityExceeded)
    {
        timeline_status_.partial = true;
        return Status::success();
    }
    if (!captured.ok())
    {
        if (captured.code == FailureCode::IdentityMismatch
            || captured.code == FailureCode::GenerationMismatch)
        {
            generation_rebaseline_pending_ = true;
            timeline_status_.batch_entry_checkpoint_failure = FailureCode::None;
        }
        return Status::success();
    }
    const Status camera_source = CapturePendingCameraSource();
    if (!camera_source.ok()) return camera_source;
    if (previous.captured != 0
        && previous.last_coordinate.generation == coordinate.generation)
    {
        const std::uint64_t gap =
            coordinate.frame - previous.last_coordinate.frame;
        if (gap > timeline_status_.maximum_batch_entry_checkpoint_gap)
            timeline_status_.maximum_batch_entry_checkpoint_gap = gap;
    }
    return Status::success();
}

Status Sc6ReplayRuntime::CapturePendingCameraSource() noexcept
{
    const Status captured = checkpoint_capture_.CaptureCameraSourceFrame(
        pending_camera_source_frame_);
    if (!captured.ok()) timeline_status_.failure = captured.code;
    return captured;
}

Status Sc6ReplayRuntime::PrepareResumeOuterTick(
    std::uintptr_t battle_manager, std::uint32_t thread_id) noexcept
{
    if (!resume_validation_active_) return Status::success();
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    if (battle_manager != timeline_manager_ || thread_id != timeline_thread_id_)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    const FrameCoordinate next{timeline_status_.last_coordinate.generation,
        timeline_status_.last_coordinate.frame + 1};
    const auto member = batch_timeline_.FindCoordinate(next);
    if (!member.has_value())
    {
        timeline_status_.failure = FailureCode::MissingInput;
        return Status::failure(timeline_status_.failure);
    }
    if (member->offset_in_batch != 0)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    const auto expected = canonical_timeline_.GetExact(next);
    const auto input = input_timeline_.GetExact(next);
    if (!expected.has_value() || !input.has_value())
    {
        timeline_status_.failure = FailureCode::MissingInput;
        return Status::failure(timeline_status_.failure);
    }
    const Status restored = checkpoint_capture_.PrepareInputLogForReplay(
        expected->input, *input);
    if (!restored.ok()) timeline_status_.failure = restored.code;
    return restored;
}

void Sc6ReplayRuntime::RebaselineAfterIdentityDrift() noexcept
{
    const std::uint64_t sessions = timeline_status_.sessions;
    const std::uint64_t generations = timeline_status_.generations;
    const std::uint64_t rebaselines = timeline_status_.identity_rebaselines + 1;
    // Presentation/RNG observations belong to the replay session, not to the
    // replaceable native object generation. Preserve them when a round or
    // identity transition invalidates restorable timeline storage.
    const ReplaySessionCoverage coverage =
        CaptureReplaySessionCoverage(timeline_status_);
    if (const auto range = canonical_timeline_.Range(); range.has_value())
        archived_last_canonical_ = canonical_timeline_.GetExact(range->second);
    archived_canonical_frames_ += canonical_timeline_.size();
    ArchivePresentationIdentity();
    input_timeline_.Clear();
    batch_timeline_.Clear();
    canonical_timeline_.Clear();
    forced_qualification_snapshots_.Clear();
    if (!online_predicted_remote_player_.has_value())
    {
        correction_undo_scratch_ = {};
        correction_verified_scratch_ = {};
        correction_canonical_capture_scratch_ = {};
        timeline_canonical_capture_scratch_ = {};
    }
    checkpoint_capture_.InvalidateHistory();
    presentation_controller_.EndGeneration();
    timeline_status_ = {};
    timeline_status_.sessions = sessions;
    timeline_status_.generations = generations;
    timeline_status_.identity_rebaselines = rebaselines;
    timeline_status_.canonical_frames = archived_canonical_frames_;
    timeline_status_.canonical_hash_bytes = archived_canonical_frames_
        * sizeof(CanonicalHashEntry);
    if (archived_last_canonical_.has_value())
        timeline_status_.last_coordinate =
            archived_last_canonical_->coordinate;
    RestoreReplaySessionCoverage(timeline_status_, coverage);
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
    generation_rebaseline_pending_ = false;
    continuing_session_rebaseline_ = true;
}

void Sc6ReplayRuntime::ArchivePresentationIdentity() noexcept
{
    auto& values = archived_presentation_identity_;
    if (values[2] == 0) values[2] = 1469598103934665603ull;
    if (values[4] == 0) values[4] = 1469598103934665603ull;
    if (values[5] == 0) values[5] = 1469598103934665603ull;
    for (std::size_t index = 0; index < batch_timeline_.batch_count(); ++index)
    {
        const auto* batch = batch_timeline_.GetBatch(index);
        if (batch == nullptr) continue;
        ++values[0];
        values[1] += batch->battle_audio_dispatches
            + batch->audio_terminal_calls;
        values[3] += batch->presentation_order_journal_count;
        AppendFnv64(values[2], &batch->entry_coordinate,
            sizeof(batch->entry_coordinate));
        AppendFnv64(values[2], &batch->battle_audio_dispatches,
            sizeof(batch->battle_audio_dispatches));
        AppendFnv64(values[2], &batch->battle_audio_sequence_hash,
            sizeof(batch->battle_audio_sequence_hash));
        AppendFnv64(values[2], &batch->battle_audio_payload_hash,
            sizeof(batch->battle_audio_payload_hash));
        AppendFnv64(values[2], &batch->audio_terminal_calls,
            sizeof(batch->audio_terminal_calls));
        AppendFnv64(values[2], &batch->audio_terminal_hash,
            sizeof(batch->audio_terminal_hash));
        AppendFnv64(values[4], &batch->entry_coordinate,
            sizeof(batch->entry_coordinate));
        AppendFnv64(values[4], &batch->presentation_order_journal_count,
            sizeof(batch->presentation_order_journal_count));
        AppendFnv64(values[4], &batch->presentation_order_hash,
            sizeof(batch->presentation_order_hash));
        AppendFnv64(values[5], &batch->entry_coordinate,
            sizeof(batch->entry_coordinate));
        AppendFnv64(values[5], &batch->camera_publication_hash,
            sizeof(batch->camera_publication_hash));
        values[8] += batch->camera_publication_hash != 0 ? 1 : 0;
        values[6] += batch->presentation_order_failures
            + batch->camera_signature_failures
            + batch->stage_signature_failures
            + batch->particle_signature_failures;
    }
}

bool Sc6ReplayRuntime::GetPresentationIdentity(
    std::array<std::uint64_t, 9>& values) const noexcept
{
    values = archived_presentation_identity_;
    if (values[2] == 0) values[2] = 1469598103934665603ull;
    if (values[4] == 0) values[4] = 1469598103934665603ull;
    if (values[5] == 0) values[5] = 1469598103934665603ull;
    for (std::size_t index = 0; index < batch_timeline_.batch_count(); ++index)
    {
        const auto* batch = batch_timeline_.GetBatch(index);
        if (batch == nullptr) return false;
        ++values[0];
        values[1] += batch->battle_audio_dispatches
            + batch->audio_terminal_calls;
        values[3] += batch->presentation_order_journal_count;
        AppendFnv64(values[2], &batch->entry_coordinate,
            sizeof(batch->entry_coordinate));
        AppendFnv64(values[2], &batch->battle_audio_dispatches,
            sizeof(batch->battle_audio_dispatches));
        AppendFnv64(values[2], &batch->battle_audio_sequence_hash,
            sizeof(batch->battle_audio_sequence_hash));
        AppendFnv64(values[2], &batch->battle_audio_payload_hash,
            sizeof(batch->battle_audio_payload_hash));
        AppendFnv64(values[2], &batch->audio_terminal_calls,
            sizeof(batch->audio_terminal_calls));
        AppendFnv64(values[2], &batch->audio_terminal_hash,
            sizeof(batch->audio_terminal_hash));
        AppendFnv64(values[4], &batch->entry_coordinate,
            sizeof(batch->entry_coordinate));
        AppendFnv64(values[4], &batch->presentation_order_journal_count,
            sizeof(batch->presentation_order_journal_count));
        AppendFnv64(values[4], &batch->presentation_order_hash,
            sizeof(batch->presentation_order_hash));
        AppendFnv64(values[5], &batch->entry_coordinate,
            sizeof(batch->entry_coordinate));
        AppendFnv64(values[5], &batch->camera_publication_hash,
            sizeof(batch->camera_publication_hash));
        values[8] += batch->camera_publication_hash != 0 ? 1 : 0;
        values[6] += batch->presentation_order_failures
            + batch->camera_signature_failures
            + batch->stage_signature_failures
            + batch->particle_signature_failures;
    }
    const auto statistics = presentation_controller_.statistics();
    values[6] += statistics.duplicates + statistics.capacity_failures
        + statistics.publish_failures;
    values[7] = statistics.committed;
    return values[0] != 0 && values[1] != 0 && values[3] != 0
        && values[8] != 0;
}

Status Sc6ReplayRuntime::BeginObservedOuterTick(
    const OuterTickObservation& observation,
    std::uint32_t& coordinate_count,
    bool& input_generation_changed,
    bool& skip_batch) noexcept
{
    skip_batch = false;
    if (timeline_status_.failure != FailureCode::None)
        return Status::failure(timeline_status_.failure);
    if (timeline_status_.partial)
    {
        pending_batch_id_ = 0;
        pending_camera_source_frame_ = {};
        pending_batch_coordinates_.clear();
        skip_batch = true;
        return Status::success();
    }
    constexpr std::uint16_t state_reads = 0x33;
    if ((observation.read_mask & state_reads) != state_reads)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.before.main_state != 2)
    {
        if (pending_batch_id_ == observation.batch_id)
        {
            timeline_status_.identity_issue = 7;
            timeline_status_.identity_expected = 0;
            timeline_status_.identity_observed = pending_batch_id_;
            timeline_status_.failure = FailureCode::IdentityMismatch;
            return Status::failure(timeline_status_.failure);
        }
        skip_batch = true;
        return Status::success();
    }
    if (observation.read_mask
        != Schema::Sc6FrameLayout::required_outer_tick_read_mask)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    if (timeline_thread_id_ != 0
        && timeline_thread_id_ != observation.thread_id)
    {
        timeline_status_.failure = FailureCode::WrongThread;
        return Status::failure(timeline_status_.failure);
    }
    if (!observation.fp_before_valid || !observation.fp_after_valid)
    {
        timeline_status_.failure = FailureCode::ContextUnavailable;
        return Status::failure(timeline_status_.failure);
    }
    ++timeline_status_.fp_samples;
    timeline_status_.fp_last_before = observation.fp_before;
    timeline_status_.fp_last_after = observation.fp_after;
    if (!FloatingPointControlMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_control_mismatches;
    if (!FloatingPointStatusMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_status_mismatches;
    if (!FloatingPointX87StatusMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_x87_status_mismatches;
    if (!FloatingPointMxcsrStatusMatches(observation.fp_before, observation.fp_after))
        ++timeline_status_.fp_mxcsr_status_mismatches;
    if (observation.after.frame_counter != observation.before.frame_counter
        && timeline_manager_ != 0
        && timeline_manager_ != observation.battle_manager)
    {
        timeline_status_.identity_issue = 8;
        timeline_status_.identity_expected = timeline_manager_;
        timeline_status_.identity_observed = observation.battle_manager;
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.after.frame_counter < observation.before.frame_counter)
    {
        timeline_status_.failure = FailureCode::AdvanceFailed;
        return Status::failure(timeline_status_.failure);
    }

    coordinate_count =
        observation.after.frame_counter - observation.before.frame_counter;
    if (coordinate_count > Schema::maximum_supported_native_batch_width)
    {
        timeline_status_.failure = FailureCode::AdapterUnqualified;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.stage_signature_failures != 0
        || observation.battle_audio_signature_failures != 0
        || observation.particle_signature_failures != 0
        || observation.camera_signature_failures != 0
        || observation.presentation_order_failures != 0)
    {
        timeline_status_.failure = FailureCode::PresentationFailed;
        return Status::failure(timeline_status_.failure);
    }
    if (observation.batch_id == 0
        || coordinate_count != pending_batch_coordinates_.size()
        || pending_batch_id_ != observation.batch_id)
    {
        if (observation.batch_id == 0)
        {
            timeline_status_.identity_issue = 9;
            timeline_status_.identity_expected = 1;
            timeline_status_.identity_observed = 0;
        }
        else if (coordinate_count != pending_batch_coordinates_.size())
        {
            timeline_status_.identity_issue = 10;
            timeline_status_.identity_expected =
                pending_batch_coordinates_.size();
            timeline_status_.identity_observed = coordinate_count;
        }
        else
        {
            timeline_status_.identity_issue = 11;
            timeline_status_.identity_expected = pending_batch_id_;
            timeline_status_.identity_observed = observation.batch_id;
        }
        timeline_status_.failure = FailureCode::IdentityMismatch;
        return Status::failure(timeline_status_.failure);
    }
    input_generation_changed =
        observation.before.input_log != observation.after.input_log
        || observation.before.input_game_round
            != observation.after.input_game_round;
    return Status::success();
}

void Sc6ReplayRuntime::FillObservedGameplayEnvelope(
    const OuterTickObservation& observation,
    std::uint32_t coordinate_count,
    bool input_generation_changed,
    NativeBatchEnvelope& envelope) const noexcept
{
    envelope.batch_id = observation.batch_id;
    envelope.entry_coordinate = coordinate_count == 0
        ? timeline_status_.last_coordinate : pending_batch_entry_;
    envelope.exit_coordinate = timeline_status_.last_coordinate;
    envelope.delta_seconds = observation.delta_seconds;
    envelope.native_frame_before = observation.before.frame_counter;
    envelope.native_frame_after = observation.after.frame_counter;
    envelope.input_round_before = observation.before.input_game_round;
    envelope.input_round_after = observation.after.input_game_round;
    envelope.input_time_before = observation.before.input_game_time;
    envelope.input_time_after = observation.after.input_game_time;
    envelope.manager_round_cursor_before =
        observation.before.manager_game_round_cursor;
    envelope.manager_round_cursor_after =
        observation.after.manager_game_round_cursor;
    envelope.manager_time_cursor_before =
        observation.before.manager_game_time_cursor;
    envelope.manager_time_cursor_after =
        observation.after.manager_game_time_cursor;
    envelope.coordinate_count = coordinate_count;
    envelope.repeat_pending_coordinates =
        observation.repeat_pending_coordinates;
    envelope.same_input_time_coordinates =
        observation.same_input_time_coordinates;
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
}

void Sc6ReplayRuntime::FillObservedPresentationEnvelope(
    const OuterTickObservation& observation,
    bool input_generation_changed,
    NativeBatchEnvelope& envelope) const noexcept
{
    envelope.stage_wall_calls = observation.stage_wall_calls;
    envelope.stage_wall_hash = observation.stage_wall_hash;
    envelope.stage_barrier_calls = observation.stage_barrier_calls;
    envelope.stage_barrier_hash = observation.stage_barrier_hash;
    envelope.stage_dispatch_calls = observation.stage_dispatch_calls;
    envelope.stage_dispatch_hash = observation.stage_dispatch_hash;
    envelope.stage_signature_failures = observation.stage_signature_failures;
    envelope.battle_audio_dispatches = observation.battle_audio_dispatches;
    envelope.battle_audio_sequence_hash =
        observation.battle_audio_sequence_hash;
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
    envelope.battle_audio_stop_all_calls =
        observation.battle_audio_stop_all_calls;
    envelope.battle_audio_stop_all_hash =
        observation.battle_audio_stop_all_hash;
    envelope.audio_terminal_calls = observation.audio_terminal_calls;
    envelope.audio_terminal_hash = observation.audio_terminal_hash;
    envelope.battle_audio_blueprint_calls =
        observation.battle_audio_blueprint_calls;
    envelope.battle_audio_blueprint_hash =
        observation.battle_audio_blueprint_hash;
    envelope.particle_spawn_calls = observation.particle_spawn_calls;
    envelope.particle_spawn_hash = observation.particle_spawn_hash;
    envelope.particle_signature_failures =
        observation.particle_signature_failures;
    envelope.camera_publication_hash = observation.camera_publication_hash;
    envelope.camera_publication = observation.camera_publication;
    envelope.camera_source_frame = pending_camera_source_frame_;
    envelope.camera_signature_failures =
        observation.camera_signature_failures;
    envelope.presentation_order_hash = observation.presentation_order_hash;
    envelope.presentation_order_failures =
        observation.presentation_order_failures;
    envelope.qualification_stage_terminal_mask =
        observation.qualification_stage_terminal_mask;
    envelope.battle_audio_remap_entry_values =
        observation.battle_audio_remap_entry_values;
    envelope.battle_audio_remap_entry_mask =
        observation.battle_audio_remap_entry_mask;
    envelope.battle_audio_journal = observation.battle_audio_journal;
    envelope.battle_audio_source_journal =
        observation.battle_audio_source_journal;
    envelope.battle_audio_remap_journal =
        observation.battle_audio_remap_journal;
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
    envelope.presentation_order_journal =
        observation.presentation_order_journal;
    envelope.battle_audio_journal_count =
        observation.battle_audio_journal_count;
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
    envelope.stage_barrier_journal_count =
        observation.stage_barrier_journal_count;
    envelope.stage_dispatch_journal_count =
        observation.stage_dispatch_journal_count;
    envelope.particle_spawn_journal_count =
        observation.particle_spawn_journal_count;
    envelope.presentation_order_journal_count =
        observation.presentation_order_journal_count;
    envelope.main_state_before = observation.before.main_state;
    envelope.main_state_after = observation.after.main_state;
    envelope.round_state_before = observation.before.round_state;
    envelope.round_state_after = observation.after.round_state;
    envelope.input_generation_changed = input_generation_changed;
}
