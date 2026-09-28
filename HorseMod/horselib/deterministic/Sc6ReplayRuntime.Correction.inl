void Sc6ReplayRuntime::RecordSeekHashMismatch(
    FrameCoordinate target,
    const CanonicalHashEntry& expected,
    const Snapshot& observed) noexcept
{
    timeline_status_.resume_failure_coordinate = target;
    timeline_status_.resume_expected_hash = expected.hash;
    timeline_status_.resume_observed_hash = observed.canonical_hash;
    timeline_status_.resume_component_difference_mask = 0;
    for (std::size_t index = 0; index < expected.components.size(); ++index)
        if (expected.components[index] != observed.canonical_components[index])
            timeline_status_.resume_component_difference_mask
                |= std::uint32_t{1} << index;
    timeline_status_.resume_native_difference_mask = 0;
    timeline_status_.resume_expected_move_dispatch = expected.move_dispatch;
    timeline_status_.resume_observed_move_dispatch =
        observed.canonical_move_dispatch;
    for (std::size_t index = 0; index < expected.native.size(); ++index)
        if (expected.native[index] != observed.canonical_native[index])
            timeline_status_.resume_native_difference_mask
                |= std::uint32_t{1} << index;
    timeline_status_.resume_input_scalar_difference_mask = 0;
    timeline_status_.resume_expected_input_scalars = expected.input.scalars;
    timeline_status_.resume_observed_input_scalars =
        observed.canonical_input.scalars;
    for (std::size_t index = 0; index < expected.input.scalars.size(); ++index)
        if (expected.input.scalars[index]
            != observed.canonical_input.scalars[index])
            timeline_status_.resume_input_scalar_difference_mask
                |= std::uint32_t{1} << index;
    timeline_status_.resume_first_input_cache_chunk = UINT32_MAX;
    for (std::size_t index = 0; index < expected.input.cache_chunks.size(); ++index)
        if (expected.input.cache_chunks[index]
            != observed.canonical_input.cache_chunks[index])
        {
            timeline_status_.resume_first_input_cache_chunk =
                static_cast<std::uint32_t>(index);
            break;
        }
    timeline_status_.resume_first_input_cache_row = UINT32_MAX;
    for (std::size_t index = 0;
         index < expected.input.aligned_block_rows.size(); ++index)
        if (expected.input.aligned_block_rows[index]
            != observed.canonical_input.aligned_block_rows[index])
        {
            timeline_status_.resume_first_input_cache_row =
                static_cast<std::uint32_t>(index);
            timeline_status_.resume_expected_input_cache_row =
                expected.input.aligned_block_rows[index];
            timeline_status_.resume_observed_input_cache_row =
                observed.canonical_input.aligned_block_rows[index];
            break;
        }
    timeline_status_.resume_wind_difference_mask = 0;
    for (std::size_t index = 0; index < expected.wind.size(); ++index)
        if (expected.wind[index] != observed.canonical_wind[index])
            timeline_status_.resume_wind_difference_mask
                |= std::uint32_t{1} << index;
}

void Sc6ReplayRuntime::ArmResumeValidation(
    FrameCoordinate target,
    FrameCoordinate source_end,
    const ReplaySeekPlan& plan,
    std::uint64_t validation_ns) noexcept
{
    timeline_status_.resumed_frames_verified = 0;
    timeline_status_.last_seek_resimulation_coordinates =
        plan.resimulation_coordinates;
    timeline_status_.last_seek_validation_ns = validation_ns;
    timeline_status_.last_coordinate = target;
    timeline_status_.resume_target = target;
    timeline_status_.resume_source_end = source_end;
    timeline_status_.resume_failure_coordinate = {};
    timeline_status_.resume_expected_hash = {};
    timeline_status_.resume_observed_hash = {};
    timeline_status_.resume_component_difference_mask = 0;
    timeline_status_.resume_native_difference_mask = 0;
    timeline_status_.resume_input_scalar_difference_mask = 0;
    timeline_status_.resume_first_input_cache_chunk = UINT32_MAX;
    timeline_status_.resume_first_input_cache_row = UINT32_MAX;
    timeline_status_.resume_first_wind_semantic_word = UINT32_MAX;
    timeline_status_.resume_expected_wind_semantic_word = 0;
    timeline_status_.resume_observed_wind_semantic_word = 0;
    timeline_status_.resume_wind_difference_mask = 0;
    resume_target_ = target;
    resume_source_end_ = source_end;
    resume_catchup_pending_ = false;
    resume_next_batch_ = plan.landing_batch_index + 1;
    resume_end_batch_ = batch_timeline_.batch_count();
    resume_validation_active_ = resume_next_batch_ < resume_end_batch_;
    timeline_status_.resume_validation_active = resume_validation_active_;
}

Status Sc6ReplayRuntime::ExecuteOwnedStateSeek(
    FrameCoordinate target, DeterministicHookSet& hooks) noexcept
{
    const auto validation_begin = std::chrono::steady_clock::now();
    if (resume_validation_active_ || pending_batch_id_ != 0
        || timeline_status_.partial
        || timeline_status_.failure != FailureCode::None)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    ReplaySeekPlan plan{};
    Status status = PlanSeek(target, plan);
    if (!status.ok())
    {
        timeline_status_.identity_issue = 300;
        timeline_status_.identity_expected = target.frame;
        timeline_status_.identity_observed =
            static_cast<std::uint64_t>(status.code);
        timeline_status_.seek_plan_failure_stage = plan.failure_stage;
        timeline_status_.seek_plan_failure_batch_index =
            plan.failure_batch_index;
        timeline_status_.seek_plan_failure_base = plan.failure_base;
        timeline_status_.seek_plan_failure_entry = plan.failure_entry;
        timeline_status_.seek_plan_failure_exit = plan.failure_exit;
        return status;
    }
    if (!hooks.installed() || timeline_thread_id_ == 0
        || timeline_thread_id_ != ::GetCurrentThreadId()
        || timeline_manager_ == 0
        || timeline_status_.last_coordinate.generation != target.generation)
    {
        return Status::failure(FailureCode::WrongThread);
    }

    const FrameCoordinate source_end = timeline_status_.last_coordinate;
    const auto* target_batch = batch_timeline_.GetBatch(plan.landing_batch_index);
    const auto* source_batch = batch_timeline_.batch_count() == 0 ? nullptr
        : batch_timeline_.GetBatch(batch_timeline_.batch_count() - 1);
    // Requests are serviced after the entire native outer call returns.
    // Interior coordinates need an explicit suspended native continuation;
    // reject them before any write until that phase can be reconstructed.
    if (plan.coordinates_after_landing != 0 || target_batch == nullptr
        || target_batch->exit_coordinate != target
        || !target_batch->completed_identity_valid || source_batch == nullptr
        || source_batch->exit_coordinate != source_end
        || !source_batch->completed_identity_valid)
    {
        timeline_status_.identity_issue = 311;
        timeline_status_.identity_expected = target.frame;
        timeline_status_.identity_observed = plan.coordinates_after_landing;
        return Status::failure(FailureCode::RestorePreflightFailed);
    }
    ReplaySourceState source_undo{};
    // The existing reconstruction path needs an exact input-cache handoff
    // for an empty call. Check that prerequisite before acquiring/restoring
    // state; a sparse frame checkpoint cannot supply a missing native phase.
    for (std::size_t index = plan.first_batch_index;
         index <= plan.landing_batch_index; ++index)
    {
        const auto* batch = batch_timeline_.GetBatch(index);
        if (batch == nullptr || !batch->completed_identity_valid
            || batch->consumers_before_count > batch->consumers_before.size()
            || batch->producers_before_count > batch->producers_before.size()
            || (index != plan.first_batch_index
                && batch->preceding_outer_batch_id
                    != batch_timeline_.GetBatch(index - 1)->batch_id)
            || (batch->coordinate_count == 0 && !batch->input_before_valid
            && checkpoint_capture_.snapshots(CandidateCheckpointRole::BatchEntry)
                .FindExact(batch->entry_coordinate) == nullptr))
            return Status::failure(FailureCode::RestorePreflightFailed);
        if (index != plan.first_batch_index)
            for (std::size_t consumer = 0; consumer < batch->consumers_before_count; ++consumer)
            {
                const auto& observed = batch->consumers_before[consumer];
                if (!observed.valid || !observed.parent.inert
                    || observed.before.mode != 0 || observed.after.mode != 0
                    || observed.before.mode_zero_callback != 0
                    || observed.after.mode_zero_callback != 0)
                {
                    timeline_status_.identity_issue = 323;
                    timeline_status_.identity_expected = index;
                    timeline_status_.identity_observed = observed.before.mode;
                    return Status::failure(FailureCode::RestorePreflightFailed);
                }
            }
        if (index != plan.first_batch_index)
        {
            std::uint8_t preceding_consumers{};
            for (std::size_t producer = 0; producer < batch->producers_before_count; ++producer)
            {
                const auto& observed = batch->producers_before[producer];
                if (!observed.valid || !observed.parent.inert
                    || observed.preceding_consumers < preceding_consumers
                    || observed.preceding_consumers > batch->consumers_before_count
                    || observed.native_frame_before != batch->entry_coordinate.frame
                    || observed.native_frame_after != observed.native_frame_before
                    || observed.before.scalars[2] != 2
                    || observed.sampled_slots != (observed.before.scalars[3] & 3u))
                    return Status::failure(FailureCode::RestorePreflightFailed);
                preceding_consumers = observed.preceding_consumers;
            }
        }
    }
    status = bridge_->CapturePlaybackSource(source_undo);
    if (!status.ok()) return status;
    if (!source_undo.SameRecording(target_batch->replay_source_after)
        || source_undo.round < 0 || source_undo != source_batch->replay_source_after)
        return Status::failure(FailureCode::RestorePreflightFailed);
    status = checkpoint_capture_.EnsureRestoreOwnership(timeline_thread_id_);
    if (!status.ok()) return status;

    Snapshot undo{};
    status = checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, undo);
    if (!status.ok()) return status;
    const auto* base = checkpoint_capture_.snapshots(
        CandidateCheckpointRole::BatchEntry).FindExact(plan.resimulation_base);
    if (base == nullptr)
    {
        timeline_status_.identity_issue = 303;
        timeline_status_.identity_expected = plan.resimulation_base.frame;
        timeline_status_.identity_observed = target.frame;
        return Status::failure(FailureCode::MissingSnapshot);
    }

    const auto restore_undo = [&]() noexcept {
        const bool simulation_restored = checkpoint_capture_.RestoreAndVerify(undo).ok();
        ReplaySourceState current_source{};
        const bool source_read = bridge_->CapturePlaybackSource(current_source).ok();
        const bool source_restored = source_read && (current_source == source_undo
            || bridge_->RestorePlaybackSource(current_source, source_undo).ok());
        return simulation_restored && source_restored;
    };
    status = checkpoint_capture_.RestoreAndVerify(*base);
    if (status.ok())
        status = bridge_->RestorePlaybackSource(source_undo,
            batch_timeline_.GetBatch(plan.first_batch_index)->replay_source_before);
    if (!status.ok())
        return restore_undo()
            ? status : Status::failure(FailureCode::UndoFailed);

    // Reconstruct through the native return, including repeat epilogues,
    // round-over checks and actor tail. Never overwrite this completed state
    // with the earlier fencepost's masks, input cache or snapshot.
    Snapshot landing{};
    for (std::size_t batch_index = plan.first_batch_index;
         status.ok() && batch_index <= plan.landing_batch_index; ++batch_index)
    {
        const auto* replayed_batch = batch_timeline_.GetBatch(batch_index);
        if (replayed_batch == nullptr || !replayed_batch->completed_identity_valid)
        {
            status = Status::failure(FailureCode::RestorePreflightFailed);
            break;
        }
        if (batch_index != plan.first_batch_index)
        {
            std::size_t consumer{};
            const auto replay_consumers_until = [&](std::size_t end) noexcept {
                while (status.ok() && consumer < end)
                {
                    TutorialConsumerObservation observed{};
                    status = checkpoint_capture_.ReplayTutorialConsumer(
                        replayed_batch->consumers_before[consumer++], true, observed);
                }
            };
            for (std::size_t producer = 0;
                 status.ok() && producer < replayed_batch->producers_before_count; ++producer)
            {
                const auto& observed = replayed_batch->producers_before[producer];
                replay_consumers_until(observed.preceding_consumers);
                if (status.ok()) status = hooks.ReplayInputProducer(observed, source_undo.owner);
            }
            replay_consumers_until(replayed_batch->consumers_before_count);
            if (!status.ok())
            {
                timeline_status_.identity_issue = 324;
                timeline_status_.identity_expected = batch_index;
                timeline_status_.identity_observed = replayed_batch->consumers_before_count;
                timeline_status_.resume_failure_coordinate = replayed_batch->entry_coordinate;
                break;
            }
        }
        ReplaySourceState source_before{};
        status = bridge_->CapturePlaybackSource(source_before);
        if (!status.ok() || source_before != replayed_batch->replay_source_before)
        {
            if (status.ok()) status = Status::failure(FailureCode::IdentityMismatch);
            timeline_status_.identity_issue = 326;
            timeline_status_.resume_failure_coordinate = replayed_batch->entry_coordinate;
            break;
        }
        std::size_t failed_batch_index = batch_index;
        status = ReplayOwnedBatchRange(batch_index, batch_index,
            target.generation, hooks, std::nullopt, 0, nullptr, true, true,
            nullptr, nullptr, &failed_batch_index);
        if (!status.ok())
        {
            timeline_status_.identity_issue = 313;
            timeline_status_.identity_expected = failed_batch_index;
            timeline_status_.identity_observed = static_cast<std::uint64_t>(status.code);
            break;
        }
        // Observe every completed call, including zero-coordinate calls.
        // A final-only comparison loses the first divergent native boundary.
        status = checkpoint_capture_.CaptureCanonical(
            replayed_batch->exit_coordinate, landing);
        ReplaySourceState source_after{};
        if (status.ok()) status = bridge_->CapturePlaybackSource(source_after);
        if (status.ok() && source_after != replayed_batch->replay_source_after)
        {
            timeline_status_.identity_issue = 327;
            timeline_status_.resume_failure_coordinate = replayed_batch->exit_coordinate;
            status = Status::failure(FailureCode::IdentityMismatch);
        }
        if (status.ok() && landing.canonical_hash != replayed_batch->completed_hash)
        {
            timeline_status_.identity_issue = 314; // First divergent completed native call.
            timeline_status_.identity_expected = batch_index;
            timeline_status_.identity_observed = plan.first_batch_index;
            timeline_status_.resume_failure_coordinate = replayed_batch->exit_coordinate;
            timeline_status_.resume_expected_hash = replayed_batch->completed_hash;
            timeline_status_.resume_observed_hash = landing.canonical_hash;
            timeline_status_.resume_component_difference_mask = 0;
            for (std::size_t index = 0; index < replayed_batch->completed_components.size(); ++index)
                if (replayed_batch->completed_components[index] != landing.canonical_components[index])
                    timeline_status_.resume_component_difference_mask |= std::uint32_t{1} << index;
            timeline_status_.resume_native_difference_mask = 0;
            for (std::size_t index = 0; index < replayed_batch->completed_native.size(); ++index)
                if (replayed_batch->completed_native[index] != landing.canonical_native[index])
                    timeline_status_.resume_native_difference_mask |= std::uint32_t{1} << index;
            timeline_status_.resume_expected_move_dispatch = replayed_batch->completed_move_dispatch;
            timeline_status_.resume_observed_move_dispatch = landing.canonical_move_dispatch;
            timeline_status_.resume_expected_input_scalars = replayed_batch->completed_input_scalars;
            timeline_status_.resume_observed_input_scalars = landing.canonical_input.scalars;
            timeline_status_.resume_input_scalar_difference_mask = 0;
            for (std::size_t index = 0; index < replayed_batch->completed_input_scalars.size(); ++index)
                if (replayed_batch->completed_input_scalars[index] != landing.canonical_input.scalars[index])
                    timeline_status_.resume_input_scalar_difference_mask |= std::uint32_t{1} << index;
            status = Status::failure(FailureCode::StateHashMismatch);
        }
    }
    if (!status.ok())
        return restore_undo()
            ? status : Status::failure(FailureCode::UndoFailed);

    const auto validation_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - validation_begin).count());
    ArmResumeValidation(target, source_end, plan, validation_ns);
    return Status::success();
}

Status Sc6ReplayRuntime::CaptureCurrentCanonical(Snapshot& output) noexcept
{
    if (!ready() || timeline_manager_ == 0 || timeline_thread_id_ == 0
        || timeline_thread_id_ != ::GetCurrentThreadId()
        || pending_batch_id_ != 0
        || timeline_status_.failure != FailureCode::None
        || timeline_status_.last_coordinate.generation == 0)
    {
        output = {};
        return Status::failure(FailureCode::WrongThread);
    }
    return checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, output);
}

bool Sc6ReplayRuntime::AtCompletedOuterTickBoundary(
    FrameCoordinate coordinate) const noexcept
{
    return coordinate.generation != 0
        && timeline_status_.failure == FailureCode::None
        && timeline_status_.last_coordinate == coordinate
        && active_outer_tick_id_ == 0 && pending_batch_id_ == 0;
}

Status Sc6ReplayRuntime::GetCanonicalHash(
    FrameCoordinate coordinate, CanonicalHash& output) const noexcept
{
    output = {};
    const auto entry = canonical_timeline_.GetExact(coordinate);
    if (entry.has_value()) output = entry->hash;
    else if (archived_last_canonical_.has_value()
        && archived_last_canonical_->coordinate == coordinate)
        output = archived_last_canonical_->hash;
    else return Status::failure(FailureCode::MissingSnapshot);
    return Status::success();
}

Status Sc6ReplayRuntime::GetCanonicalEntry(
    FrameCoordinate coordinate, CanonicalHashEntry& output) const noexcept
{
    output = {};
    const auto entry = canonical_timeline_.GetExact(coordinate);
    if (entry.has_value()) output = *entry;
    else if (archived_last_canonical_.has_value()
        && archived_last_canonical_->coordinate == coordinate)
        output = *archived_last_canonical_;
    else return Status::failure(FailureCode::MissingSnapshot);
    return Status::success();
}

Status Sc6ReplayRuntime::GetLastCanonicalEntryInGeneration(
    std::uint64_t generation, CanonicalHashEntry& output) const noexcept
{
    output = {};
    const auto live = canonical_timeline_.GetLastInGeneration(generation);
    const bool archived_matches = archived_last_canonical_.has_value()
        && archived_last_canonical_->coordinate.generation == generation;
    if (live.has_value()
        && (!archived_matches
            || live->coordinate > archived_last_canonical_->coordinate))
    {
        output = *live;
        return Status::success();
    }
    if (archived_matches)
    {
        output = *archived_last_canonical_;
        return Status::success();
    }
    return Status::failure(FailureCode::MissingSnapshot);
}

bool Sc6ReplayRuntime::GetSeekableRange(
    FrameCoordinate& first, FrameCoordinate& last) const noexcept
{
    first = {};
    last = {};
    const auto range = canonical_timeline_.LatestGenerationRange();
    if (!range.has_value() || range->first.generation == 0)
        return false;
    first = range->first;
    last = range->second;
    return true;
}

Status Sc6ReplayRuntime::PreflightOwnedCorrection(
    FrameCoordinate earliest_changed) const noexcept
{
    if (timeline_manager_ == 0 || pending_batch_id_ != 0
        || timeline_status_.failure != FailureCode::None)
    {
        return Status::failure(FailureCode::IllegalTransition);
    }
    ReplayCorrectionPlan plan{};
    const SnapshotStore& correction_snapshots =
        forced_depth7_qualification_enabled_
        ? forced_qualification_snapshots_
        : checkpoint_capture_.snapshots(CandidateCheckpointRole::BatchEntry);
    return PlanReplayCorrection(earliest_changed,
        timeline_status_.last_coordinate, batch_timeline_,
        correction_snapshots, Schema::checkpoint_interval - 1, plan);
}

Status Sc6ReplayRuntime::ExecuteOwnedCorrection(
    FrameCoordinate earliest_changed,
    const CanonicalHash& expected_final_hash,
    DeterministicHookSet& hooks,
    OwnedCorrectionResult& output) noexcept
{
    return ExecuteOwnedCorrectionInternal(earliest_changed,
        &expected_final_hash, hooks, output, nullptr);
}

void Sc6ReplayRuntime::RecordCorrectionLocalCameraDiagnostics(
    const CandidateCheckpointImage& expected_image,
    const CandidateCheckpointImage& verified_image,
    const Snapshot& undo,
    const Snapshot& verified,
    OwnedCorrectionResult& output) noexcept
{
        output.expected_move_dispatch = undo.canonical_move_dispatch;
        output.observed_move_dispatch = verified.canonical_move_dispatch;
        output.undo_comparison_mask = CandidateDifferenceMask(
            expected_image, verified_image);
        if (expected_image.local_images.size() == 2
            && verified_image.local_images.size() == 2)
        {
            for (std::size_t image_index = 0; image_index < 2; ++image_index)
            {
                const auto& a = expected_image.local_images[image_index].bytes;
                const auto& b = verified_image.local_images[image_index].bytes;
                const auto size = a.size() < b.size()
                    ? a.size() : b.size();
                for (std::size_t index = 0; index < size; ++index)
                {
                    if (a[index] != b[index])
                    {
                        if (output.first_final_local_difference[image_index]
                            == UINT32_MAX)
                            output.first_final_local_difference[image_index] =
                                static_cast<std::uint32_t>(index);
                        ++output.final_local_difference_count[image_index];
                    }
                }
            }
        }
        for (std::size_t slot = 0;
             slot < expected_image.native.camera_components.size(); ++slot)
        {
            const auto& a = expected_image.native.camera_components[slot];
            const auto& b = verified_image.native.camera_components[slot];
            std::uint32_t logical_offset{};
            const auto compare = [&](const void* expected, const void* observed,
                                     std::size_t size) noexcept {
                const auto* expected_bytes = static_cast<const std::byte*>(expected);
                const auto* observed_bytes = static_cast<const std::byte*>(observed);
                for (std::size_t index = 0; index < size; ++index)
                {
                    if (expected_bytes[index] == observed_bytes[index]) continue;
                    if (output.first_camera_component_slot == UINT32_MAX)
                    {
                        output.first_camera_component_slot =
                            static_cast<std::uint32_t>(slot);
                        output.first_camera_component_difference =
                            logical_offset + static_cast<std::uint32_t>(index);
                        output.expected_camera_component_byte =
                            std::to_integer<std::uint8_t>(expected_bytes[index]);
                        output.observed_camera_component_byte =
                            std::to_integer<std::uint8_t>(observed_bytes[index]);
                        output.camera_component_vtable_rva = a.vtable_rva;
                        output.camera_component_writer_rva = a.writer_rva;
                    }
                    ++output.camera_component_difference_count;
                }
                logical_offset += static_cast<std::uint32_t>(size);
            };
            compare(a.common.data(), b.common.data(), a.common.size());
            compare(a.derived.data(), b.derived.data(), a.derived_size);
        }
}

void Sc6ReplayRuntime::RecordCorrectionInputRngDiagnostics(
    const CandidateCheckpointImage& expected_image,
    const CandidateCheckpointImage& verified_image,
    OwnedCorrectionResult& output) noexcept
{
        for (std::size_t index = 0;
             index < expected_image.native.input_log.scalars.size(); ++index)
        {
            if (expected_image.native.input_log.scalars[index]
                != verified_image.native.input_log.scalars[index])
            {
                if (output.first_input_scalar_difference == UINT32_MAX)
                {
                    output.first_input_scalar_difference =
                        static_cast<std::uint32_t>(index);
                    const auto word = index & ~std::size_t{3};
                    if (word + sizeof(std::uint32_t)
                        <= expected_image.native.input_log.scalars.size())
                    {
                        std::memcpy(&output.expected_input_scalar_word,
                            expected_image.native.input_log.scalars.data() + word,
                            sizeof(output.expected_input_scalar_word));
                        std::memcpy(&output.observed_input_scalar_word,
                            verified_image.native.input_log.scalars.data() + word,
                            sizeof(output.observed_input_scalar_word));
                    }
                }
                ++output.input_scalar_difference_count;
            }
        }
        for (std::size_t index = 0;
             index < expected_image.native.input_log.cache_rows.size(); ++index)
        {
            if (expected_image.native.input_log.cache_rows[index]
                != verified_image.native.input_log.cache_rows[index])
            {
                if (output.first_input_cache_difference == UINT32_MAX)
                {
                    output.first_input_cache_difference =
                        static_cast<std::uint32_t>(index);
                    output.expected_input_cache_row =
                        expected_image.native.input_log.cache_rows[index];
                    output.observed_input_cache_row =
                        verified_image.native.input_log.cache_rows[index];
                }
                ++output.input_cache_difference_count;
            }
        }
        const auto& expected_rng = expected_image.native.rng;
        const auto& observed_rng = verified_image.native.rng;
        output.expected_rng = expected_rng;
        output.observed_rng = observed_rng;
        if (expected_rng.lcg != observed_rng.lcg) output.rng_difference_mask |= 1;
        if (expected_rng.lfsr != observed_rng.lfsr)
        {
            output.rng_difference_mask |= 2;
            for (std::size_t index = 0; index < expected_rng.lfsr.size(); ++index)
                if (expected_rng.lfsr[index] != observed_rng.lfsr[index])
                {
                    output.first_lfsr_difference =
                        static_cast<std::uint32_t>(index);
                    break;
                }
        }
        if (expected_rng.lfsr_index != observed_rng.lfsr_index)
            output.rng_difference_mask |= 4;
        if (expected_rng.xorshift != observed_rng.xorshift)
            output.rng_difference_mask |= 8;
        if (expected_rng.wind != observed_rng.wind)
            output.rng_difference_mask |= 16;
}

void Sc6ReplayRuntime::RecordCorrectionWindDiagnostics(
    const CandidateCheckpointImage& expected_image,
    const CandidateCheckpointImage& verified_image,
    OwnedCorrectionResult& output) noexcept
{
        const auto& expected_wind = expected_image.wind;
        const auto& observed_wind = verified_image.wind;
        output.final_expected_wind = WindScheduleDiagnostic(expected_wind);
        output.final_observed_wind = WindScheduleDiagnostic(observed_wind);
        if (expected_wind.root_clock != observed_wind.root_clock)
            output.wind_difference_mask |= 1;
        if (expected_wind.pending_callback_rvas
            != observed_wind.pending_callback_rvas)
            output.wind_difference_mask |= 2;
        if (expected_wind.schedule_state != observed_wind.schedule_state)
            output.wind_difference_mask |= 4;
        if (expected_wind.schedule_params != observed_wind.schedule_params)
            output.wind_difference_mask |= 8;
        if (expected_wind.output_force != observed_wind.output_force)
        {
            output.wind_difference_mask |= 16;
            for (std::size_t index = 0; index < expected_wind.output_force.size(); ++index)
                if (expected_wind.output_force[index]
                    != observed_wind.output_force[index])
                {
                    output.first_wind_output_difference =
                        static_cast<std::uint32_t>(index);
                    break;
                }
        }
        if (expected_wind.nodes != observed_wind.nodes)
        {
            output.wind_difference_mask |= 32;
            output.expected_wind_node_count =
                static_cast<std::uint32_t>(expected_wind.nodes.size());
            output.observed_wind_node_count =
                static_cast<std::uint32_t>(observed_wind.nodes.size());
            const auto common = (std::min)(
                expected_wind.nodes.size(), observed_wind.nodes.size());
            for (std::size_t index = 0; index < common; ++index)
            {
                const auto& a = expected_wind.nodes[index];
                const auto& b = observed_wind.nodes[index];
                if (a == b) continue;
                output.first_wind_node_difference =
                    static_cast<std::uint32_t>(index);
                output.expected_wind_node_kind =
                    static_cast<std::uint8_t>(a.kind);
                output.observed_wind_node_kind =
                    static_cast<std::uint8_t>(b.kind);
                const auto semantic_common = (std::min)(
                    a.semantic_state.size(), b.semantic_state.size());
                for (std::size_t byte = 0; byte < semantic_common; ++byte)
                    if (a.semantic_state[byte] != b.semantic_state[byte])
                    {
                        output.first_wind_semantic_difference =
                            static_cast<std::uint32_t>(byte);
                        output.expected_wind_difference_byte =
                            std::to_integer<std::uint8_t>(a.semantic_state[byte]);
                        output.observed_wind_difference_byte =
                            std::to_integer<std::uint8_t>(b.semantic_state[byte]);
                        break;
                    }
                const auto derived_common = (std::min)(
                    a.derived_state.size(), b.derived_state.size());
                for (std::size_t byte = 0; byte < derived_common; ++byte)
                    if (a.derived_state[byte] != b.derived_state[byte])
                    {
                        output.first_wind_derived_difference =
                            static_cast<std::uint32_t>(byte);
                        break;
                    }
                break;
            }
            if (output.first_wind_node_difference == UINT32_MAX
                && expected_wind.nodes.size() != observed_wind.nodes.size())
                output.first_wind_node_difference =
                    static_cast<std::uint32_t>(common);
        }
}

void Sc6ReplayRuntime::RecordCorrectionMismatchDiagnostics(
    const Snapshot& expected,
    const Snapshot& observed,
    OwnedCorrectionResult& output) noexcept
{
    if (diagnostic_image_a_ == nullptr || diagnostic_image_b_ == nullptr
        || !CandidateCheckpointCodec::Decode(
            expected, *diagnostic_image_a_).ok()
        || !CandidateCheckpointCodec::Decode(
            observed, *diagnostic_image_b_).ok())
        return;
    const auto& expected_image = *diagnostic_image_a_;
    const auto& verified_image = *diagnostic_image_b_;
    RecordCorrectionLocalCameraDiagnostics(expected_image, verified_image,
        expected, observed, output);
    RecordCorrectionInputRngDiagnostics(
        expected_image, verified_image, output);
    RecordCorrectionWindDiagnostics(expected_image, verified_image, output);
}

Status Sc6ReplayRuntime::VerifyCorrectionFinalState(
    const CanonicalHash* expected_final_hash,
    CorrectedReplayCapture* corrected,
    const Snapshot& undo,
    OwnedCorrectionResult& output) noexcept
{
    // Verification observes reconstructed native state. Historical input or
    // target event-mask writes here concealed omitted producer/consumer ticks.
    // A phase mismatch must fail until that actor interval is reconstructed.
    Snapshot& verified = correction_verified_scratch_;
    const auto begin = std::chrono::steady_clock::now();
    const Status status = checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, verified);
    output.verification_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - begin).count());
    output.final_hash = verified.canonical_hash;
    const CanonicalHash* required = expected_final_hash;
    if (corrected != nullptr && corrected->coordinate_count != 0)
        required = &corrected->replacement_canonical[
            corrected->coordinate_count - 1].hash;
    const bool mismatch = status.ok()
        && (required == nullptr
            || verified.coordinate != timeline_status_.last_coordinate
            || verified.canonical_hash != *required);
    if (mismatch) RecordCorrectionMismatchDiagnostics(undo, verified, output);
    if (output.first_final_local_difference[0] != UINT32_MAX)
    {
        checkpoint_capture_.TraceLocalStreamOffset(
            output.first_final_local_difference[0],
            output.first_final_local_source,
            output.diagnostic_fighter_roots,
            output.diagnostic_image_base);
    }
    return mismatch ? Status::failure(FailureCode::StateHashMismatch) : status;
}

Status Sc6ReplayRuntime::ExecuteOwnedCorrectionInternal(
    FrameCoordinate earliest_changed, const CanonicalHash* expected_final_hash,
    DeterministicHookSet& hooks, OwnedCorrectionResult& output,
    CorrectedReplayCapture* corrected) noexcept
{
    using Clock = std::chrono::steady_clock;
    output = {};
    if (corrected != nullptr) corrected->Clear();
    output.earliest_changed = earliest_changed;
    output.final_coordinate = timeline_status_.last_coordinate;
    const auto total_begin = Clock::now();
    const auto finish = [&](Status status) noexcept {
        output.failure = status.code;
        output.total_ns = ElapsedNanoseconds(total_begin);
        return status;
    };
    if (!hooks.installed() || timeline_thread_id_ == 0
        || timeline_thread_id_ != ::GetCurrentThreadId()
        || timeline_manager_ == 0 || pending_batch_id_ != 0
        || timeline_status_.failure != FailureCode::None)
    {
        return finish(Status::failure(FailureCode::WrongThread));
    }

    ReplayCorrectionPlan plan{};
    const SnapshotStore& correction_snapshots =
        forced_depth7_qualification_enabled_
        ? forced_qualification_snapshots_
        : checkpoint_capture_.snapshots(CandidateCheckpointRole::BatchEntry);
    output.planning_snapshot_count = correction_snapshots.entry_count();
    output.planning_batch_count = batch_timeline_.batch_count();
    const auto capture_status = checkpoint_capture_.status(
        CandidateCheckpointRole::BatchEntry);
    output.planning_last_snapshot = capture_status.last_coordinate;
    const auto changed_membership = batch_timeline_.FindCoordinate(
        earliest_changed);
    if (!changed_membership.has_value())
    {
        output.planning_stage = 1;
    }
    else if (const auto* changed_batch = batch_timeline_.GetBatch(
                 changed_membership->batch_index))
    {
        output.planning_changed_batch_entry = changed_batch->entry_coordinate;
        const auto* nearest = correction_snapshots.FindNearestAtOrBefore(
            changed_batch->entry_coordinate);
        if (nearest == nullptr)
        {
            output.planning_stage = 2;
        }
        else
        {
            output.planning_nearest_snapshot = nearest->coordinate;
            output.planning_distance = timeline_status_.last_coordinate.frame
                    >= nearest->coordinate.frame
                ? timeline_status_.last_coordinate.frame
                    - nearest->coordinate.frame
                : 0;
            for (std::size_t index = 0;
                 index <= changed_membership->batch_index; ++index)
            {
                const auto* batch = batch_timeline_.GetBatch(index);
                if (batch != nullptr
                    && batch->entry_coordinate == nearest->coordinate)
                {
                    output.planning_matching_batch_index = index;
                    break;
                }
            }
            output.planning_stage =
                output.planning_matching_batch_index == SIZE_MAX ? 3 : 4;
        }
    }
    else
    {
        output.planning_stage = 1;
    }
    Status status = PlanReplayCorrection(earliest_changed,
        timeline_status_.last_coordinate, batch_timeline_,
        correction_snapshots,
        Schema::checkpoint_interval - 1, plan);
    if (!status.ok()) return finish(status);
    output.planning_stage = 5;
    output.resimulation_base = plan.resimulation_base;

    // Capture is valid while the UCRT broker observes the stock stream, but
    // restore is deliberately restricted to its one-way owned mode.
    // Correction is the authoritative transition point; later corrections
    // remain owned by this same simulation thread.
    status = checkpoint_capture_.EnsureRestoreOwnership(timeline_thread_id_);
    if (!status.ok()) return finish(status);

    Snapshot& undo = correction_undo_scratch_;
    auto phase_begin = Clock::now();
    status = checkpoint_capture_.CaptureTransient(
        timeline_status_.last_coordinate, undo);
    output.undo_capture_ns = ElapsedNanoseconds(phase_begin);
    if (!status.ok()) return finish(status);
    if (diagnostic_image_a_ != nullptr
        && CandidateCheckpointCodec::Decode(
            undo, *diagnostic_image_a_).ok())
        output.undo_audio_selector =
            diagnostic_image_a_->battle_audio_selector;

    const auto* base = correction_snapshots.FindExact(plan.resimulation_base);
    if (base == nullptr)
        return finish(Status::failure(FailureCode::MissingSnapshot));
    if (diagnostic_image_b_ != nullptr
        && CandidateCheckpointCodec::Decode(
            *base, *diagnostic_image_b_).ok())
    {
        output.base_wind_graph = WindGraphDiagnostic(
            diagnostic_image_b_->wind);
        output.base_audio_selector =
            diagnostic_image_b_->battle_audio_selector;
    }

    bool native_state_was_written = false;
    const auto record_primary_failure = [&](Status failure) noexcept {
        output.primary_failure = failure.code;
        output.primary_validation = checkpoint_capture_.restore_validation();
        output.primary_restore_difference_mask =
            checkpoint_capture_.restore_difference_mask();
        output.primary_restore_operation_failure_mask =
            checkpoint_capture_.restore_operation_failure_mask();
        output.primary_restore_failure_phase =
            checkpoint_capture_.restore_failure_phase();
        output.primary_performance = checkpoint_capture_.adapter_performance();
    };
    const auto restore_undo = [&]() noexcept {
        if (!native_state_was_written) return true;
        Status undone = checkpoint_capture_.RestoreAndVerify(undo);
        const Status audio_undone =
            checkpoint_capture_.RestoreBattleAudioSelectorForPresentation(undo);
        if (undone.ok() && !audio_undone.ok()) undone = audio_undone;
        output.undo_failure = undone.code;
        output.undo_validation = checkpoint_capture_.restore_validation();
        output.undo_restored = undone.ok();
        return output.undo_restored;
    };

    phase_begin = Clock::now();
    native_state_was_written = true;
    status = checkpoint_capture_.RestoreAndVerify(*base);
    output.restore_ns = ElapsedNanoseconds(phase_begin);
    if (!status.ok())
    {
        record_primary_failure(status);
        // A restore verifier can fail before replay begins. Capture the
        // just-restored image while it is still at the checkpoint boundary so
        // the existing canonical diagnostics identify the authoritative lane
        // and byte instead of reporting an empty final-replay comparison.
        Snapshot& restored = correction_verified_scratch_;
        if (checkpoint_capture_.CaptureTransient(
                base->coordinate, restored).ok())
        {
            RecordCorrectionMismatchDiagnostics(*base, restored, output);
        }
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    phase_begin = Clock::now();
    status = ReplayOwnedBatchRange(plan.first_batch_index,
        plan.final_batch_index, earliest_changed.generation, hooks,
        std::nullopt, UINT32_MAX, nullptr,
        forced_depth7_qualification_enabled_, false, &output.replayed_coordinates,
        &output.replayed_batches, &output.failed_batch_index,
        &output.failed_envelope, &output.failed_batch_result,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &output,
        corrected);

    output.resimulation_ns = ElapsedNanoseconds(phase_begin);
    if (output.first_interbatch_local_difference != UINT32_MAX)
    {
        checkpoint_capture_.TraceLocalStreamOffset(
            output.first_interbatch_local_difference,
            output.first_interbatch_local_source,
            output.diagnostic_fighter_roots,
            output.diagnostic_image_base);
    }
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    status = VerifyCorrectionFinalState(
        expected_final_hash, corrected, undo, output);
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    // Owned replay must not publish its historical presentation selector into
    // the next authoritative tick. Reconcile it to the pre-correction image
    // only after canonical convergence has been established.
    status = corrected == nullptr
        ? checkpoint_capture_.RestoreBattleAudioSelectorForPresentation(undo)
        : Status::success();
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }

    // FrameInputLog is a separately ticked producer. By this prepare boundary
    // its complete actor transaction (source/update work, cache publication,
    // replay drain, and GameTime advance) already precedes the pending live
    // BattleManager tick. Historical replay necessarily rewinds that producer
    // while rebuilding simulation state, so restore its exact pre-correction
    // image only after canonical convergence has been verified.
    output.input_producer_restore_phase = 1;
    status = checkpoint_capture_.RestoreInputLogForReplay(undo);
    if (!status.ok())
    {
        record_primary_failure(status);
        if (!restore_undo()) status = Status::failure(FailureCode::UndoFailed);
        return finish(status);
    }
    output.input_producer_restore_phase = 2;

    output.converged = true;
    output.primary_performance = checkpoint_capture_.adapter_performance();
    return finish(Status::success());
}

Status Sc6ReplayRuntime::CommitCorrectedReplay(
    FrameCoordinate coordinate,
    const InputPair& proposed,
    const InputPair& previous,
    OwnedCorrectionResult& output) noexcept
{
    Status status = Status::success();
    auto& corrected = corrected_replay_capture_;
    std::size_t replaced_batches{};
    bool canonical_replaced{};
    bool input_range_replaced{};
    bool presentation_replaced{};
    status = checkpoint_capture_.ValidateCorrectionSnapshots(
        std::span{corrected.replacement_landing.data(), corrected.landing_count},
        std::span{corrected.expected_landing_hashes.data(),
            corrected.landing_count},
        std::span{corrected.replacement_batch_entry.data(),
            corrected.batch_entry_count},
        std::span{corrected.expected_batch_entry_hashes.data(),
            corrected.batch_entry_count});
    if (status.ok() && presentation_ownership_enabled_)
    {
        status = presentation_controller_.ReplaceCorrected(coordinate,
            std::span{corrected.replacement_batches.data(),
                corrected.batch_count});
        presentation_replaced = status.ok();
    }
    for (; replaced_batches < corrected.batch_count; ++replaced_batches)
    {
        if (!status.ok()) break;
        status = batch_timeline_.ReplaceBatch(
            corrected.batch_indices[replaced_batches],
            corrected.expected_batches[replaced_batches],
            corrected.replacement_batches[replaced_batches]);
        if (!status.ok()) break;
    }
    if (status.ok())
    {
        status = canonical_timeline_.ReplaceExactRange(
            std::span{corrected.expected_canonical.data(),
                corrected.coordinate_count},
            std::span{corrected.replacement_canonical.data(),
                corrected.coordinate_count});
        canonical_replaced = status.ok();
    }
    if (status.ok())
    {
        status = input_timeline_.CompareExchangeRange(
            std::span{corrected.coordinates.data(), corrected.coordinate_count},
            std::span{corrected.expected_inputs.data(), corrected.coordinate_count},
            std::span{corrected.replacement_inputs.data(),
                corrected.coordinate_count});
        input_range_replaced = status.ok();
    }
    if (status.ok())
    {
        status = checkpoint_capture_.ReplaceCorrectionSnapshots(
            std::span{corrected.replacement_landing.data(),
                corrected.landing_count},
            std::span{corrected.expected_landing_hashes.data(),
                corrected.landing_count},
            std::span{corrected.replacement_batch_entry.data(),
                corrected.batch_entry_count},
            std::span{corrected.expected_batch_entry_hashes.data(),
                corrected.batch_entry_count});
    }
    if (status.ok()) return status;

    if (input_range_replaced)
    {
        static_cast<void>(input_timeline_.CompareExchangeRange(
            std::span{corrected.coordinates.data(), corrected.coordinate_count},
            std::span{corrected.replacement_inputs.data(),
                corrected.coordinate_count},
            std::span{corrected.expected_inputs.data(),
                corrected.coordinate_count}));
    }
    if (canonical_replaced)
    {
        static_cast<void>(canonical_timeline_.ReplaceExactRange(
            std::span{corrected.replacement_canonical.data(),
                corrected.coordinate_count},
            std::span{corrected.expected_canonical.data(),
                corrected.coordinate_count}));
    }
    while (replaced_batches != 0)
    {
        --replaced_batches;
        static_cast<void>(batch_timeline_.ReplaceBatch(
            corrected.batch_indices[replaced_batches],
            corrected.replacement_batches[replaced_batches],
            corrected.expected_batches[replaced_batches]));
    }
    bool presentation_restored = true;
    if (presentation_replaced)
    {
        presentation_restored = presentation_controller_.ReplaceCorrected(
            coordinate, std::span{corrected.expected_batches.data(),
                corrected.batch_count}).ok();
    }
    const Status input_restored = input_timeline_.CompareExchange(
        coordinate, proposed, previous);
    Status native_restored = checkpoint_capture_.RestoreAndVerify(
        correction_undo_scratch_);
    const Status selector_restored =
        checkpoint_capture_.RestoreBattleAudioSelectorForPresentation(
            correction_undo_scratch_);
    if (native_restored.ok() && !selector_restored.ok())
        native_restored = selector_restored;
    output.converged = false;
    output.failure = status.code;
    output.undo_restored = native_restored.ok();
    return input_restored.ok() && native_restored.ok()
            && presentation_restored
        ? status : Status::failure(FailureCode::UndoFailed);
}

Status Sc6ReplayRuntime::ApplyConfirmedRemoteInput(
    FrameCoordinate coordinate,
    std::size_t player_index,
    const PlayerInput& confirmed_remote,
    DeterministicHookSet& hooks,
    OwnedCorrectionResult& output) noexcept
{
    output = {};
    if (player_index >= 2 || coordinate.generation == 0
        || coordinate.generation != timeline_status_.last_coordinate.generation
        || coordinate > timeline_status_.last_coordinate
        || (online_predicted_remote_player_.has_value()
            && player_index != *online_predicted_remote_player_))
        return Status::failure(FailureCode::InvalidConfiguration);
    const auto previous = input_timeline_.GetExact(coordinate);
    if (!previous.has_value())
        return Status::failure(FailureCode::MissingInput);
    if (!CanReviseObservedRemoteInput(previous->remote_confirmed,
            corrected_input_qualification_enabled_,
            online_predicted_remote_player_, player_index))
    {
        if (previous->players[player_index] != confirmed_remote)
            return Status::failure(FailureCode::IdentityMismatch);
        output.earliest_changed = coordinate;
        output.final_coordinate = timeline_status_.last_coordinate;
        const auto final = canonical_timeline_.GetExact(
            timeline_status_.last_coordinate);
        if (!final.has_value())
            return Status::failure(FailureCode::MissingSnapshot);
        output.final_hash = final->hash;
        output.converged = true;
        return Status::success();
    }

    InputPair proposed = *previous;
    proposed.players[player_index] = confirmed_remote;
    proposed.source_rows[player_index].input_value = confirmed_remote.held;
    proposed.remote_confirmed = true;
    proposed.post_filter_observed = false;
    Status status = input_timeline_.CompareExchange(
        coordinate, *previous, proposed);
    if (!status.ok()) return status;

    if (previous->players[player_index] == confirmed_remote)
    {
        output.earliest_changed = coordinate;
        output.final_coordinate = timeline_status_.last_coordinate;
        const auto final = canonical_timeline_.GetExact(
            timeline_status_.last_coordinate);
        if (!final.has_value())
        {
            const auto restored = input_timeline_.CompareExchange(
                coordinate, proposed, *previous);
            return restored.ok()
                ? Status::failure(FailureCode::MissingSnapshot)
                : Status::failure(FailureCode::UndoFailed);
        }
        output.final_hash = final->hash;
        output.converged = true;
        return Status::success();
    }

    status = ExecuteOwnedCorrectionInternal(coordinate, nullptr, hooks,
        output, &corrected_replay_capture_);
    if (!status.ok())
    {
        const Status restored = input_timeline_.CompareExchange(
            coordinate, proposed, *previous);
        return restored.ok() ? status
            : Status::failure(FailureCode::UndoFailed);
    }

    return CommitCorrectedReplay(coordinate, proposed, *previous, output);
}

void* Sc6ReplayRuntime::ResolveReplayPlayer(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->lux_.battleManager().getObj(L"BattleReplayPlayer").raw()
        : nullptr;
}

void* Sc6ReplayRuntime::ResolveBattleManager(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->lux_.battleManager().raw() : nullptr;
}

void* Sc6ReplayRuntime::ResolveFighterOne(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->ResolveFighter(0) : nullptr;
}

void* Sc6ReplayRuntime::ResolveFighterTwo(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    return runtime ? runtime->ResolveFighter(1) : nullptr;
}

void* Sc6ReplayRuntime::ResolveStage(void* user) noexcept
{
    auto* runtime = static_cast<Sc6ReplayRuntime*>(user);
    if (runtime == nullptr)
    {
        return nullptr;
    }
    Obj manager = runtime->lux_.battleManager();
    return manager ? manager.getObj(L"BattleStageActorManager").raw() : nullptr;
}

void* Sc6ReplayRuntime::ResolveFighter(std::size_t index) noexcept
{
    const TArrHdr* fighters = lux_.battleCharaArray();
    if (fighters == nullptr || fighters->Data == nullptr || fighters->Num != 2
        || index >= static_cast<std::size_t>(fighters->Num))
    {
        return nullptr;
    }
    auto** objects = static_cast<RC::Unreal::UObject**>(fighters->Data);
    return objects[index];
}
