

    static std::size_t companion_replay_storage(void* user) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        // Include retained legacy timelines and scratch while the new core
        // coexists with the maintained strict-seek implementation.
        return self ? self->m_replay_native_runtime.owned_storage_status().aggregate_bytes
            + sizeof(self->m_deterministic_hooks) + sizeof(self->m_native_replay_session_entry) : SIZE_MAX;
    }

    static void on_frame_fencepost(
        void* user,
        const Horse::Deterministic::FrameFencepostObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
        {
            return;
        }
        self->m_frame_fencepost_entries.fetch_add(1, std::memory_order_acq_rel);
        self->m_frame_fencepost_last_read_mask.store(
            observation.read_mask, std::memory_order_release);
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }
        if (observation.read_mask
            != Horse::Deterministic::Schema::Sc6FrameLayout::
                required_observation_read_mask)
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::ContextUnavailable,
                std::memory_order_release);
            return;
        }
        const auto capture = self->m_replay_native_runtime.ObserveFrame(observation);
        if (!capture.ok())
        {
            self->m_frame_fencepost_failure.store(
                capture.code, std::memory_order_release);
            const auto failed = self->m_replay_native_runtime.timeline_status();
            if (capture.code
                    == Horse::Deterministic::FailureCode::StateHashMismatch
                && failed.resume_failure_coordinate.generation != 0)
            {
                if (self->m_resume_divergence_logged.exchange(
                        true, std::memory_order_acq_rel))
                {
                    return;
                }
                std::uint64_t expected_prefix{};
                std::uint64_t observed_prefix{};
                std::memcpy(&expected_prefix,
                    failed.resume_expected_hash.data(), sizeof(expected_prefix));
                std::memcpy(&observed_prefix,
                    failed.resume_observed_hash.data(), sizeof(observed_prefix));
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] resumed canonical frame diverged "
                    "coordinate={} verified_before={} component_mask=0x{:x} "
                    "wind_mask=0x{:x} wind_semantic_word={} "
                    "wind_semantic_expected=0x{:08x} "
                    "wind_semantic_observed=0x{:08x} "
                    "wind_node_expected={}/{:08x}/{}/{}/{}/{:08x}/{} "
                    "wind_node_observed={}/{:08x}/{}/{}/{}/{:08x}/{} "
                    "expected_hash_prefix=0x{:016x} "
                    "observed_hash_prefix=0x{:016x}\n"),
                    failed.resume_failure_coordinate.frame,
                    failed.resumed_frames_verified,
                    failed.resume_component_difference_mask,
                    failed.resume_wind_difference_mask,
                    failed.resume_first_wind_semantic_word,
                    failed.resume_expected_wind_semantic_word,
                    failed.resume_observed_wind_semantic_word,
                    failed.resume_expected_wind_node.kind,
                    failed.resume_expected_wind_node.life_bits,
                    failed.resume_expected_wind_node.oscillator_tick,
                    failed.resume_expected_wind_node.prepared,
                    failed.resume_expected_wind_node.active,
                    failed.resume_expected_wind_node.frame_step_bits,
                    failed.resume_expected_wind_node.repeat_count,
                    failed.resume_observed_wind_node.kind,
                    failed.resume_observed_wind_node.life_bits,
                    failed.resume_observed_wind_node.oscillator_tick,
                    failed.resume_observed_wind_node.prepared,
                    failed.resume_observed_wind_node.active,
                    failed.resume_observed_wind_node.frame_step_bits,
                    failed.resume_observed_wind_node.repeat_count,
                    expected_prefix, observed_prefix);
            }
            else if (capture.code
                    == Horse::Deterministic::FailureCode::CaptureFailed
                && !self->m_resume_divergence_logged.exchange(
                    true, std::memory_order_acq_rel))
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] canonical frame capture failed coordinate={} "
                    "phase={} validation={} validation_index={} "
                    "validation_observed={}/{} validation_expected={}/{} "
                    "camera_status={} camera_stage={} camera_index={} "
                    "camera_observed=0x{:x} camera_expected=0x{:x} "
                    "animation={} observed=0x{:x}\n"),
                    failed.canonical_capture_failure_coordinate.frame,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::candidate_capture_phase_name(
                            failed.canonical_capture_phase))),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::
                            native_candidate_validation_issue_name(
                                failed.canonical_capture_validation.issue))),
                    failed.canonical_capture_validation.index,
                    failed.canonical_capture_validation.observed_a,
                    failed.canonical_capture_validation.observed_b,
                    failed.canonical_capture_validation.expected_a,
                    failed.canonical_capture_validation.expected_b,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(
                            failed.canonical_camera_capture.intended_failure))),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::camera_topology_capture_stage_name(
                            failed.canonical_camera_capture.stage))),
                    failed.canonical_camera_capture.index,
                    failed.canonical_camera_capture.observed,
                    failed.canonical_camera_capture.expected,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::chara_animation_topology_issue_name(
                            failed.canonical_animation_topology_issue))),
                    failed.canonical_animation_topology_observed);
            }
            else if (!self->m_resume_divergence_logged.exchange(
                         true, std::memory_order_acq_rel))
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] canonical frame capture rejected status={} "
                    "coordinate={} phase={} validation={} validation_index={} "
                    "validation_observed={}/{} validation_expected={}/{} "
                    "camera_status={} camera_stage={} camera_index={} "
                    "camera_observed=0x{:x} camera_expected=0x{:x} "
                    "animation={} observed=0x{:x} "
                    "identity_issue={} expected=0x{:x} actual=0x{:x}\n"),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(capture.code))),
                    failed.canonical_capture_failure_coordinate.frame,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::candidate_capture_phase_name(
                            failed.canonical_capture_phase))),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::
                            native_candidate_validation_issue_name(
                                failed.canonical_capture_validation.issue))),
                    failed.canonical_capture_validation.index,
                    failed.canonical_capture_validation.observed_a,
                    failed.canonical_capture_validation.observed_b,
                    failed.canonical_capture_validation.expected_a,
                    failed.canonical_capture_validation.expected_b,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(
                            failed.canonical_camera_capture.intended_failure))),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::camera_topology_capture_stage_name(
                            failed.canonical_camera_capture.stage))),
                    failed.canonical_camera_capture.index,
                    failed.canonical_camera_capture.observed,
                    failed.canonical_camera_capture.expected,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::chara_animation_topology_issue_name(
                            failed.canonical_animation_topology_issue))),
                    failed.canonical_animation_topology_observed,
                    failed.identity_issue, failed.identity_expected,
                    failed.identity_observed);
            }
            return;
        }
        self->m_deterministic_hooks.SetBattleAudioPresentationGeneration(
            self->m_replay_native_runtime.timeline_status_view()
                .last_coordinate.generation);
        const auto& timeline =
            self->m_replay_native_runtime.timeline_status_view();
        if (timeline.observed_resolved_hit_calls != 0)
            self->m_replay_session_resolved_hit_observed = true;
        if (timeline.last_coordinate.generation != 0)
            self->m_replay_identity_active.store(
                true, std::memory_order_release);
        if (!timeline.resume_validation_active)
        {
            self->refresh_stage_break_presentation_identity(
                timeline.last_coordinate.generation);
            self->observe_hgcpu_diagnostic(observation.frame_counter);
            self->observe_stage_break_listener_diagnostic(
                observation.frame_counter);
        }
        const auto logged_checkpoints =
            self->m_candidate_checkpoint_logged_count.load(std::memory_order_acquire);
        const bool checkpoint_log_due = timeline.captured_checkpoints == 1
            && logged_checkpoints == 0;
        if (self->m_deterministic_config.trace && checkpoint_log_due)
        {
            self->m_candidate_checkpoint_logged_count.store(
                timeline.captured_checkpoints, std::memory_order_release);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] candidate checkpoint captured count={} generation={} "
                "frame={} bytes={} wind_nodes={} capture_p99_us={} "
                "capture_max_us={} store_p99_us={} typed_p99_us={} "
                "local_p99_us={} hgcpu_p99_us={} motion_p99_us={} "
                "wind_p99_us={} encode_p99_us={}\n"),
                timeline.captured_checkpoints,
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame,
                timeline.checkpoint_bytes,
                timeline.checkpoint_wind_nodes,
                timeline.checkpoint_capture_p99_ns / 1000,
                timeline.checkpoint_capture_max_ns / 1000,
                timeline.checkpoint_store_p99_ns / 1000,
                timeline.checkpoint_adapter_performance.typed_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.local_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.hgcpu_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.motion_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.wind_capture.p99_ns / 1000,
                timeline.checkpoint_adapter_performance.encode.p99_ns / 1000);
        }
        if (timeline.checkpoint_failure != Horse::Deterministic::FailureCode::None
            && !self->m_candidate_checkpoint_first_failure_logged.exchange(
                true, std::memory_order_acq_rel))
        {
            const auto failure = Horse::Deterministic::failure_code_name(
                timeline.checkpoint_failure);
            const auto& diagnostic = timeline.checkpoint_validation;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] candidate checkpoint capture unavailable: {} "
                "phase={} detail={} animation={} animation_ptr=0x{:x} fighters=0x{:x}/0x{:x} "
                "index={} observed={}/{} expected={}/{}\n"),
                RC::to_generic_string(std::string(failure)),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::candidate_capture_phase_name(
                        timeline.checkpoint_capture_phase))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::native_candidate_validation_issue_name(
                        diagnostic.issue))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::chara_animation_topology_issue_name(
                        timeline.checkpoint_animation_topology_issue))),
                timeline.checkpoint_animation_observed,
                timeline.checkpoint_animation_fighters[0],
                timeline.checkpoint_animation_fighters[1],
                diagnostic.index, diagnostic.observed_a, diagnostic.observed_b,
                diagnostic.expected_a, diagnostic.expected_b);
        }

        const std::uintptr_t prior_manager = self->m_frame_fencepost_manager.exchange(
            observation.battle_manager, std::memory_order_acq_rel);
        const std::uint64_t prior_count = self->m_frame_fencepost_observations.fetch_add(
            1, std::memory_order_acq_rel);
        const std::uint32_t prior_frame = self->m_frame_fencepost_last_frame.exchange(
            observation.frame_counter, std::memory_order_acq_rel);
        if (prior_manager != observation.battle_manager)
        {
            self->m_frame_fencepost_generations.fetch_add(
                1, std::memory_order_relaxed);
        }
        else if (prior_count != 0
                 && observation.frame_counter != prior_frame + 1)
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::AdvanceFailed,
                std::memory_order_release);
        }
        if (observation.repeat_pending != 0)
        {
            self->m_frame_fencepost_repeats.fetch_add(
                1, std::memory_order_relaxed);
        }
    }

    static void on_outer_tick(
        void* user,
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
            return;
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }
        const auto status = self->m_replay_native_runtime.ObserveOuterTick(
            observation);
        if (!status.ok())
        {
            const auto failed = self->m_replay_native_runtime.timeline_status();
            const auto fencepost_failure_before_outer =
                self->m_frame_fencepost_failure.load(std::memory_order_acquire);
            const auto fencepost_read_mask =
                self->m_frame_fencepost_last_read_mask.load(
                    std::memory_order_acquire);
            const auto fencepost_expected_thread =
                self->m_frame_fencepost_expected_thread.load(
                    std::memory_order_acquire);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] outer-tick accounting failed status={} batch={} "
                "frame={}->{} coordinates={} filter={}/{} capture_preserved={} "
                "input_log=0x{:x}->0x{:x} "
                "input_round={}->{} input_time={}->{} "
                "manager_round={}->{} manager_time={}->{} "
                "main_state={}->{} round_state={}->{} "
                "fencepost_status={} read_mask=0x{:x} "
                "expected_thread={} outer_thread={} "
                "identity_issue={} expected={} observed={}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                observation.batch_id,
                observation.before.frame_counter,
                observation.after.frame_counter,
                observation.observed_coordinates,
                observation.input_filter_observed ? 1 : 0,
                observation.input_filter_invocations,
                observation.outer_capture_context_preserved ? 1 : 0,
                observation.before.input_log, observation.after.input_log,
                observation.before.input_game_round,
                observation.after.input_game_round,
                observation.before.input_game_time,
                observation.after.input_game_time,
                observation.before.manager_game_round_cursor,
                observation.after.manager_game_round_cursor,
                observation.before.manager_game_time_cursor,
                observation.after.manager_game_time_cursor,
                static_cast<unsigned>(observation.before.main_state),
                static_cast<unsigned>(observation.after.main_state),
                static_cast<unsigned>(observation.before.round_state),
                static_cast<unsigned>(observation.after.round_state),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        fencepost_failure_before_outer))),
                fencepost_read_mask, fencepost_expected_thread,
                observation.thread_id,
                failed.identity_issue, failed.identity_expected,
                failed.identity_observed);
            if (status.code == Horse::Deterministic::FailureCode::ProtocolMismatch
                || status.code
                    == Horse::Deterministic::FailureCode::UnsupportedContent)
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] presentation journal build failed status={} "
                    "batch={} frame={}->{} order={} "
                    "terminal={}/{} blueprint={}/{} wall={}/{} barrier={}/{} "
                    "dispatch={}/{} particles={}/{} signatures={}/{}/{}/{}\n"),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(status.code))),
                    observation.batch_id, observation.before.frame_counter,
                    observation.after.frame_counter,
                    observation.presentation_order_journal_count,
                    observation.audio_terminal_calls,
                    observation.audio_terminal_journal_count,
                    observation.battle_audio_blueprint_calls,
                    observation.battle_audio_blueprint_journal_count,
                    observation.stage_wall_calls,
                    observation.stage_wall_journal_count,
                    observation.stage_barrier_calls,
                    observation.stage_barrier_journal_count,
                    observation.stage_dispatch_calls,
                    observation.stage_dispatch_journal_count,
                    observation.particle_spawn_calls,
                    observation.particle_spawn_journal_count,
                    observation.stage_signature_failures,
                    observation.battle_audio_signature_failures,
                    observation.particle_signature_failures,
                    observation.presentation_order_failures);
                const auto& batches =
                    self->m_replay_native_runtime.batch_timeline();
                const auto batch_count = batches.batch_count();
                const auto* captured = batch_count != 0
                    ? batches.GetBatch(batch_count - 1) : nullptr;
                if (captured != nullptr
                    && observation.presentation_order_journal_count != 0)
                {
                    Output::send<LogLevel::Warning>(STR(
                        "[HorseMod] presentation journal envelope "
                        "entry={}:{} exit={}:{} count={}\n"),
                        captured->entry_coordinate.generation,
                        captured->entry_coordinate.frame,
                        captured->exit_coordinate.generation,
                        captured->exit_coordinate.frame,
                        captured->presentation_order_journal_count);
                    for (std::size_t index = 0;
                         index < captured->presentation_order_journal_count;
                         ++index)
                    {
                        const auto& entry =
                            captured->presentation_order_journal[index];
                        Output::send<LogLevel::Warning>(STR(
                            "[HorseMod] presentation journal order "
                            "index={} family={} family_index={} offset={}\n"),
                            index, static_cast<unsigned int>(entry.family),
                            static_cast<unsigned int>(entry.family_index),
                            static_cast<unsigned int>(entry.source_offset));
                    }
                }
            }
            if (status.code == Horse::Deterministic::FailureCode::PresentationFailed
                && observation.battle_audio_signature_failures != 0)
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] authoritative battle-audio capture failed "
                    "batch={} frame={}->{} failures={} mask=0x{:x} "
                    "dispatches={} journal={}/{} sources={} source_journal={}/{} "
                    "remaps={} remap_journal={}/{} stop_all={} "
                    "stop_all_journal={}/{} stop_all_owners={} "
                    "owner_selector=unresolved unresolved_owner=0x{:x} "
                    "caller_rva=0x{:x} owner_epoch={} owner_bindings={} "
                    "owner_stage={} graph_provenance=0x{:016x}\n"),
                    observation.batch_id, observation.before.frame_counter,
                    observation.after.frame_counter,
                    observation.battle_audio_signature_failures,
                    observation.battle_audio_signature_failure_mask,
                    observation.battle_audio_dispatches,
                    observation.battle_audio_journal_count,
                    observation.battle_audio_journal.size(),
                    observation.battle_audio_source_calls,
                    observation.battle_audio_source_journal_count,
                    observation.battle_audio_source_journal.size(),
                    observation.battle_audio_remap_calls,
                    observation.battle_audio_remap_journal_count,
                    observation.battle_audio_remap_journal.size(),
                    observation.battle_audio_stop_all_calls,
                    observation.battle_audio_stop_all_journal_count,
                    observation.battle_audio_stop_all_journal.size(),
                    observation.battle_audio_stop_all_owner_identity_count,
                    observation.first_unresolved_audio_owner,
                    observation.first_unresolved_audio_return_rva,
                    observation.audio_owner_graph_epoch,
                    observation.audio_owner_graph_bindings,
                    observation.audio_owner_graph_failure_stage,
                    observation.audio_owner_graph_provenance);
            }
            self->m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
            return;
        }


        const auto& timeline =
            self->m_replay_native_runtime.timeline_status_view();
        if (timeline.observed_resolved_hit_calls != 0)
            self->m_replay_session_resolved_hit_observed = true;
        const std::uint64_t completed_intervals = timeline.captured_frames / 600;
        std::uint64_t logged_intervals =
            self->m_native_batch_evidence_logged_intervals.load(
                std::memory_order_acquire);
        // Emit the full diagnostic once. Repeating this synchronous UE4SS log
        // write inside later 600-frame qualification windows perturbs the very
        // real-time pacing those windows certify.
        if (self->m_deterministic_config.trace && completed_intervals == 1
            && completed_intervals > logged_intervals
            && self->m_native_batch_evidence_logged_intervals.compare_exchange_strong(
                logged_intervals, completed_intervals,
                std::memory_order_acq_rel))
        {
            Horse::Deterministic::UcrtRandBrokerImage ucrt_image{};
            const auto ucrt_status = self->m_ucrt_rand_broker.Capture(
                observation.thread_id, ucrt_image);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] native fencepost evidence frames={} repeats={} "
                "same_time={} cursor_mismatches={} round_transition_barriers={} "
                "round_state_frame={} "
                "input_filter_observations={} input_filter_mutations={} "
                "input_filter_invocations_max={} identity_rebaselines={} unpause={} "
                "pending_move_state={} batches={} zero_batches={} "
                "multi_batches={} batch_repeats={} batch_same_time={} max_batch={} "
                "max_input_delta={} input_generation_changes={} "
                "batch_accounting_mismatches={} entry_uncovered={} "
                "entry_gap_max={} resim_distance_max={} fp_samples={} "
                "fp_control_mismatches={} fp_status_mismatches={} "
                "fp_x87_status_mismatches={} fp_mxcsr_status_mismatches={} "
                "fp_before=0x{:04x}/0x{:04x}/0x{:08x} "
                "fp_after=0x{:04x}/0x{:04x}/0x{:08x} "
                "ucrt_mode={} ucrt_status={} ucrt_owner_thread={} "
                "ucrt_seeded={} ucrt_draws={} "
                "ucrt_state=0x{:08x} ucrt_combat_state=0x{:08x} "
                "ucrt_native_draws={} ucrt_unknown_draws={} ucrt_epoch={}\n"),
                timeline.captured_frames,
                timeline.repeat_requests,
                timeline.same_native_time_coordinates,
                timeline.cursor_mismatches,
                timeline.round_transition_cursor_barriers,
                timeline.round_state_frame,
                timeline.input_filter_observations,
                timeline.input_filter_mutations,
                timeline.maximum_input_filter_invocation_ordinal,
                timeline.identity_rebaselines,
                timeline.unpause_countdown,
                static_cast<unsigned int>(timeline.pending_move_state),
                timeline.native_batches,
                timeline.zero_coordinate_batches,
                timeline.multi_coordinate_batches,
                timeline.batch_repeat_coordinates,
                timeline.batch_same_input_time_coordinates,
                timeline.maximum_coordinates_per_batch,
                timeline.maximum_input_delta_per_batch,
                timeline.batch_input_generation_changes,
                timeline.batch_frame_accounting_mismatches,
                timeline.coordinates_without_batch_entry_checkpoint,
                timeline.maximum_batch_entry_checkpoint_gap,
                timeline.maximum_resim_distance_from_batch_entry,
                timeline.fp_samples,
                timeline.fp_control_mismatches,
                timeline.fp_status_mismatches,
                timeline.fp_x87_status_mismatches,
                timeline.fp_mxcsr_status_mismatches,
                timeline.fp_last_before.x87_control,
                timeline.fp_last_before.x87_status,
                timeline.fp_last_before.mxcsr,
                timeline.fp_last_after.x87_control,
                timeline.fp_last_after.x87_status,
                timeline.fp_last_after.mxcsr,
                static_cast<unsigned int>(self->m_ucrt_rand_broker.mode()),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(ucrt_status.code))),
                self->m_ucrt_rand_broker.owner_thread_id(),
                ucrt_image.seeded,
                ucrt_image.draws,
                ucrt_image.state, ucrt_image.combat_state,
                ucrt_image.native_draws, ucrt_image.unknown_draws, ucrt_image.epoch);
        }
        if (!self->service_owned_seek_request())
        {
            self->service_forced_depth7_qualification();
            if (!self->m_deterministic_config.forced_depth7_qualification
                && !self->m_forced_correction_qualification.runtime_armed)
                self->service_owned_correction_probe();
        }
        if (self->m_replay_native_runtime.presentation_ownership_enabled())
        {
            const auto after = self->m_replay_native_runtime.timeline_status();
            const auto window = static_cast<std::uint64_t>(
                self->m_deterministic_config.rollback_window);
            if (after.last_coordinate.generation != 0
                && after.last_coordinate.frame > window)
            {
                const Horse::Deterministic::FrameCoordinate confirmed{
                    after.last_coordinate.generation,
                    after.last_coordinate.frame - window};
                const auto committed =
                    self->m_replay_native_runtime.CommitPresentationThrough(
                        confirmed, self->m_deterministic_hooks);
                if (!committed.ok())
                    self->m_frame_fencepost_failure.store(
                        committed.code, std::memory_order_release);
            }
        }
    }

    static void on_outer_tick_begin(
        void* user,
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
            return;
        self->log_replay_source_boundary(observation);
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_frame_fencepost_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }
        const auto status = self->m_replay_native_runtime.ObserveOuterTickBegin(
            observation);
        if (!status.ok())
        {
            self->m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
            const auto pending_terminal =
                self->m_qualification_stage_terminal_request.exchange(
                    0, std::memory_order_acq_rel);
            if (pending_terminal != 0)
            {
                const auto failed =
                    self->m_replay_native_runtime.timeline_status();
                self->m_qualification_stage_terminal_status.store(
                    3, std::memory_order_release);
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] qualification stage terminal blocked "
                    "operation={} phase=outer_tick_begin status={} "
                    "identity_issue={} expected=0x{:x} actual=0x{:x}\n"),
                    pending_terminal,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(status.code))),
                    failed.identity_issue, failed.identity_expected,
                    failed.identity_observed);
            }
            return;
        }
        const auto& timeline =
            self->m_replay_native_runtime.timeline_status_view();
        // ObserveOuterTickBegin has admitted the native batch-entry
        // coordinate before the game's outer tick can emit presentation.
        // Publish that generation now so first-frame semantic audio sources
        // bind against the same admitted lifetime instead of generation zero.
        self->m_deterministic_hooks.SetBattleAudioPresentationGeneration(
            timeline.last_coordinate.generation);
        if (timeline.last_coordinate.generation != 0)
            self->m_replay_identity_active.store(
                true, std::memory_order_release);
        const auto logged = self->m_candidate_batch_entry_logged_count.load(
            std::memory_order_acquire);
        const bool batch_entry_log_due =
            timeline.captured_batch_entry_checkpoints == 1 && logged == 0;
        if (self->m_deterministic_config.trace && batch_entry_log_due)
        {
            self->m_candidate_batch_entry_logged_count.store(
                timeline.captured_batch_entry_checkpoints,
                std::memory_order_release);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] candidate batch-entry checkpoint captured count={} "
                "generation={} frame={} bytes={} wind_nodes={} "
                        "capture_p99_us={} capture_max_us={} store_p99_us={} "
                        "typed_p99_us={} local_p99_us={} hgcpu_p99_us={} "
                        "motion_p99_us={} wind_p99_us={} "
                "encode_p99_us={}\n"),
                timeline.captured_batch_entry_checkpoints,
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame,
                timeline.batch_entry_checkpoint_bytes,
                timeline.batch_entry_wind_nodes,
                timeline.batch_entry_capture_p99_ns / 1000,
                timeline.batch_entry_capture_max_ns / 1000,
                timeline.batch_entry_store_p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.typed_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.local_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.hgcpu_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.motion_capture.p99_ns / 1000,
                        timeline.batch_entry_adapter_performance.wind_capture.p99_ns / 1000,
                timeline.batch_entry_adapter_performance.encode.p99_ns / 1000);
        }
        if (timeline.batch_entry_checkpoint_failure
                != Horse::Deterministic::FailureCode::None
            && !self->m_candidate_batch_entry_first_failure_logged.exchange(
                true, std::memory_order_acq_rel))
        {
            const auto failure = Horse::Deterministic::failure_code_name(
                timeline.batch_entry_checkpoint_failure);
            const auto& diagnostic = timeline.batch_entry_checkpoint_validation;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] candidate batch-entry checkpoint unavailable: {} "
                "phase={} detail={} animation={} animation_ptr=0x{:x} fighters=0x{:x}/0x{:x} "
                "index={} observed={}/{} expected={}/{}\n"),
                RC::to_generic_string(std::string(failure)),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::candidate_capture_phase_name(
                        timeline.batch_entry_capture_phase))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::native_candidate_validation_issue_name(
                        diagnostic.issue))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::chara_animation_topology_issue_name(
                        timeline.batch_entry_animation_topology_issue))),
                timeline.batch_entry_animation_observed,
                timeline.batch_entry_animation_fighters[0],
                timeline.batch_entry_animation_fighters[1],
                diagnostic.index, diagnostic.observed_a, diagnostic.observed_b,
                diagnostic.expected_a, diagnostic.expected_b);
        }
    }

    static bool on_outer_tick_prepare(
        void* user,
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr) return false;
        const auto status = self->m_replay_native_runtime.PrepareResumeOuterTick(
            observation.battle_manager, observation.thread_id);
        if (!status.ok())
        {
            self->m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
            // No completed-outer callback will run for a rejected admission.
            // This active request already has a terminal runtime failure;
            // consume its existing failure branch without starting a seek.
            if (self->m_seek_request_active)
                static_cast<void>(self->service_owned_seek_request());
            return false;
        }
        // Arm the one-shot only after all preparation can no longer reject
        // this invocation. Owned correction has its own presentation scope.
        const auto presentation =
            self->m_replay_native_runtime.PreparePresentationOuterTick(
                self->m_deterministic_hooks);
        if (!presentation.ok())
        {
            self->m_frame_fencepost_failure.store(
                presentation.code, std::memory_order_release);
            return false;
        }
        return true;
    }

    static void on_outer_tick_source(
        void* user,
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr) return;
        self->service_qualification_stage_terminal(observation);
    }

    bool qualification_stage_runtime_ready() const noexcept
    {
        const auto timeline = m_replay_native_runtime.timeline_status();
        return timeline.failure == Horse::Deterministic::FailureCode::None
            && timeline.last_coordinate.generation != 0
            && timeline.last_coordinate.generation
                == m_stage_break_identity_generation;
    }

    void log_qualification_stage_terminal(std::uint32_t operation,
        bool invoked, std::uint32_t frame_before, std::uint32_t frame_after,
        std::uint64_t batch_id) const noexcept
    {
        const auto timeline = m_replay_native_runtime.timeline_status();
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] qualification stage terminal operation={} "
            "status={} frame={}->{} batch={} runtime_status={} "
            "identity_issue={} expected=0x{:x} actual=0x{:x}\n"), operation,
            invoked ? STR("executed") : STR("failed"), frame_before,
            frame_after, batch_id,
            RC::to_generic_string(std::string(
                Horse::Deterministic::failure_code_name(timeline.failure))),
            timeline.identity_issue, timeline.identity_expected,
            timeline.identity_observed);
    }

    void log_qualification_stage_actor_unresolved(std::uint32_t operation,
        Horse::Deterministic::FailureCode failure,
        std::uint64_t owner_logical_id) const noexcept
    {
        Output::send<LogLevel::Warning>(STR(
            "[HorseMod] qualification stage actor unresolved "
            "operation={} status={} owner={} generation={}\n"), operation,
            RC::to_generic_string(std::string(
                Horse::Deterministic::failure_code_name(failure))),
            owner_logical_id, m_stage_break_identity_generation);
    }

    void service_qualification_stage_terminal(
        const Horse::Deterministic::OuterTickObservation& observation) noexcept
    {
        const auto operation = m_qualification_stage_terminal_request.load(
            std::memory_order_acquire);
        if (operation == 0) return;
        if (!m_deterministic_config.trace
            || (!m_deterministic_config.forced_depth7_qualification
                && !m_forced_correction_qualification.runtime_armed)
            || (operation != 1 && operation != 2))
        {
            m_qualification_stage_terminal_request.store(
                0, std::memory_order_release);
            m_qualification_stage_terminal_status.store(
                3, std::memory_order_release);
            return;
        }
        constexpr std::uint64_t qualification_wait_timeout_ms = 60'000;
        const auto requested_ms = m_qualification_stage_terminal_requested_ms.load(
            std::memory_order_acquire);
        if (requested_ms != 0
            && ::GetTickCount64() - requested_ms >= qualification_wait_timeout_ms)
        {
            log_qualification_stage_terminal_wait_once("timeout");
            m_qualification_stage_terminal_request.store(
                0, std::memory_order_release);
            m_qualification_stage_terminal_status.store(
                3, std::memory_order_release);
            return;
        }
        if (!qualification_stage_runtime_ready())
        {
            log_qualification_stage_terminal_wait_once("runtime_identity");
            return;
        }

        Horse::Obj battle_manager = m_lux.battleManager();
        Horse::Obj stage_manager = battle_manager
            ? battle_manager.getObj(L"BattleStageActorManager") : Horse::Obj{};
        if (!stage_manager)
        {
            log_qualification_stage_terminal_wait_once("source_stage_manager");
            return;
        }
        const wchar_t* list_name = operation == 1
            ? L"BreakableWallActorList" : L"BarrierActorList";
        const Horse::TArrHdr* list = stage_manager.getPtr<Horse::TArrHdr>(
            list_name);
        void* actor{};
        __try
        {
            if (list == nullptr || list->Data == nullptr || list->Num < 1
                || list->Max < list->Num || list->Num > 64)
            {
                log_qualification_stage_terminal_wait_once("source_actor_list");
                return;
            }
            auto* const* actors = static_cast<void* const*>(list->Data);
            for (std::int32_t index = 0; index < list->Num; ++index)
            {
                if (actors[index] != nullptr)
                {
                    actor = actors[index];
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            actor = nullptr;
        }
        if (actor == nullptr)
        {
            log_qualification_stage_terminal_wait_once("source_actor");
            return;
        }

        std::uint64_t qualification_owner{};
        const auto owner_status =
            m_deterministic_hooks.ResolveQualificationStageActor(
                reinterpret_cast<std::uintptr_t>(actor), qualification_owner);
        if (!owner_status.ok())
        {
            log_qualification_stage_actor_unresolved(
                operation, owner_status.code, qualification_owner);
            m_qualification_stage_terminal_request.store(
                0, std::memory_order_release);
            m_qualification_stage_terminal_status.store(
                3, std::memory_order_release);
            return;
        }

        const auto image_base = Horse::NativeBinding::imageBase();
        bool invoked{};
        std::uint32_t frame_before{};
        __try
        {
            frame_before = *reinterpret_cast<const std::uint32_t*>(
                image_base
                + Horse::Deterministic::Schema::Sc6FrameLayout::
                    frame_counter_rva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return; }
        __try
        {
            if (operation == 1)
            {
                using WallBrokenFn = void (__fastcall*)(void*, bool);
                reinterpret_cast<WallBrokenFn>(image_base + 0x53d4b0)(
                    actor, false);
            }
            else
            {
                using BarrierHitFn = void (__fastcall*)(void*, void*);
                std::array<float, 3> position{};
                reinterpret_cast<BarrierHitFn>(image_base + 0x549f40)(
                    actor, position.data());
            }
            invoked = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        if (invoked && !m_deterministic_hooks.MarkQualificationStageTerminal(
                operation).ok())
        {
            invoked = false;
        }
        std::uint32_t source_frame{};
        __try
        {
            source_frame = *reinterpret_cast<const std::uint32_t*>(
                image_base
                + Horse::Deterministic::Schema::Sc6FrameLayout::
                    frame_counter_rva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        m_qualification_stage_terminal_frame.store(
            source_frame, std::memory_order_release);
        m_qualification_stage_terminal_request.store(
            0, std::memory_order_release);
        m_qualification_stage_terminal_status.store(
            invoked ? 2u : 3u, std::memory_order_release);
        log_qualification_stage_terminal(operation, invoked, frame_before,
            m_qualification_stage_terminal_frame.load(
                std::memory_order_acquire), observation.batch_id);
    }

    static void on_replay_exit(
        void* user,
        const Horse::Deterministic::ReplayExitObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr)
        {
            return;
        }
        if (observation.thread_id
            != self->m_frame_fencepost_expected_thread.load(
                std::memory_order_acquire))
        {
            self->m_replay_exit_failure.store(
                Horse::Deterministic::FailureCode::WrongThread,
                std::memory_order_release);
            return;
        }
        // Replay PostTick and LuxBattleGameMode::TerminateBattle are two
        // legitimate native teardown routes. Preserve any currently readable
        // value-only evidence before deduplicating their destructive cleanup:
        // the later route can be the first one that observes the completed
        // authored result even when an earlier transition consumed the active
        // identity. Snapshot replacement is monotonic within the qualification
        // window, so duplicate signals cannot erase a complete capture.
        const auto& exit_timeline =
            self->m_replay_native_runtime.timeline_status_view();
        const auto exit_generation = exit_timeline.last_coordinate.generation;
        const auto exit_frame = exit_timeline.last_coordinate.frame;
        const auto exit_canonical_frames = exit_timeline.canonical_frames;
        const bool terminal_snapshot_complete =
            self->CaptureReplayQualificationTerminalSnapshot();
        const bool active_identity = self->m_replay_identity_active.exchange(
            false, std::memory_order_acq_rel);
        self->m_replay_session_resolved_hit_observed = false;
        if (self->m_deterministic_config.trace)
        {
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] replay exit evidence generation={} frame={} "
                "canonical_frames={} active_identity={} snapshot_complete={}\n"),
                exit_generation, exit_frame, exit_canonical_frames,
                active_identity ? 1 : 0,
                terminal_snapshot_complete ? 1 : 0);
        }
        if (!active_identity) return;

        // Both callbacks run before their route can mutate or release the
        // camera, fighters, queued world mode, or battle manager. Remove the
        // observed native identity first.
        auto& qualification = self->m_forced_correction_qualification;
        if (self->m_deterministic_config.trace
            && (self->m_deterministic_config.forced_depth7_qualification
                || qualification.runtime_armed)
            && qualification.active && !qualification.reported)
        {
            qualification.failure =
                Horse::Deterministic::FailureCode::NativeLifecycleEnded;
            qualification.reported = true;
            qualification.lifecycle = 4;
            self->m_frame_fencepost_failure.store(
                qualification.failure, std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] forced depth-7 qualification failed "
                "completed={} target={} generations={}-{} transitions={} "
                "frames={}-{} awaiting_generation_history={} "
                "status=native_lifecycle_ended reason=replay_exit\n"),
                qualification.completed, kForcedQualificationCorrections,
                qualification.first_generation, qualification.generation,
                qualification.generation_transitions,
                qualification.first_frame, qualification.last_frame,
                qualification.awaiting_generation_history);
        }
        self->m_frame_fencepost_manager.store(0, std::memory_order_release);
        self->m_frame_fencepost_last_frame.store(0, std::memory_order_release);
        if (self->m_qualification_stage_terminal_request.exchange(
                0, std::memory_order_acq_rel) != 0)
        {
            self->m_qualification_stage_terminal_status.store(
                3, std::memory_order_release);
        }
        self->m_qualification_stage_terminal_requested_ms.store(
            0, std::memory_order_release);
        self->invalidate_stage_break_presentation_identity();
        self->m_deterministic_hooks.InvalidateBattleAudioPresentationIdentity();
        self->m_replay_native_runtime.ObserveReplayExit();
        self->m_owned_correction_probe_index = 0;
        if (qualification.runtime_armed)
        {
            self->m_replay_native_runtime.SetForcedDepth7QualificationEnabled(false);
            self->m_replay_native_runtime.DisablePresentationOwnership();
            qualification.runtime_armed = false;
            qualification.storage_cleanup =
                self->m_replay_native_runtime.owned_storage_status();
            qualification.pending_events_cleanup =
                self->m_replay_native_runtime.pending_presentation_events();
            qualification.pending_payload_cleanup =
                self->m_replay_native_runtime.presentation_payload_bytes();
            qualification.cleanup_verified =
                qualification.pending_events_cleanup == 0
                && qualification.pending_payload_cleanup == 0
                && self->m_deterministic_hooks
                    .QualificationPresentationIdentityClear();
        }
        else
        {
            self->m_forced_correction_qualification = {};
        }
        self->m_seek_request_active = false;
        self->m_replay_source_trace_historical = false;
        self->m_resume_divergence_logged.store(false, std::memory_order_release);
        self->m_seek_completed_target.store(0, std::memory_order_release);
        self->m_seek_completed_source.store(0, std::memory_order_release);
        self->m_seek_completed_verified.store(0, std::memory_order_release);
        self->m_seek_handled_sequence = self->m_seek_request_sequence.load(
            std::memory_order_acquire);
        self->m_seek_pending_sequence = self->m_seek_handled_sequence;
        self->m_seek_defer_count = 0;
        self->m_replay_exit_state.store(
            observation.replay_state, std::memory_order_release);
        self->m_replay_exit_observations.fetch_add(1, std::memory_order_acq_rel);
    }

    static bool on_native_actor_hold(void* user) noexcept
    {
        const auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr) return false;
        // These entries belong to DeterministicHookSet. The sibling actor
        // gate shares its policy without installing overlapping entry patches.
        return self->m_actor_tick_gate.is_enabled()
            && self->m_world_tick_gate.is_enabled()
            && self->m_world_tick_gate.policy_slot_address() != nullptr
            && *self->m_world_tick_gate.policy_slot_address() == 0;
    }

    static void on_input_producer_tick(void* user,
        const Horse::Deterministic::InputProducerObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr
            || !self->m_replay_native_runtime.ObserveInputProducerTick(observation))
            return;
        const auto& before = observation.before;
        const auto& after = observation.after;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] native input producer frame={}/{} actor=0x{:x} valid={} "
            "round={}/{} time={}/{} update={}/{} pause={}/{} recorder={}/{} "
            "local_flags={:x} receivers={}/{} parent_inert={} script_size={} "
            "latent_index={} removals={}\n"),
            observation.native_frame_before, observation.native_frame_after,
            before.owner, observation.valid, before.scalars[4], after.scalars[4],
            before.scalars[5], after.scalars[5], before.scalars[7], after.scalars[7],
            before.scalars[6], after.scalars[6], before.scalars[8], after.scalars[8],
            before.scalars[3], before.receiver_counts[0], before.receiver_counts[1],
            observation.parent.inert, observation.parent.script_size,
            observation.parent.actor_latent_index, observation.parent.pending_removals);
    }

    static void on_tutorial_tick(void* user,
        const Horse::Deterministic::TutorialConsumerObservation& observation) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr || !self->m_replay_native_runtime.ObserveTutorialTick(observation))
            return;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] native consumer frame={} actor=0x{:x} valid={} "
            "mode={}/{} gate={}/{} masks={:x}/{:x}->{:x}/{:x} "
            "provider={}/{}->{}/{} authored_frame={}/{} parent_inert={} "
            "script_size={} function_flags={:x} latent_index={} removals={} destruction={:x}\n"),
            observation.native_frame, observation.before.owner, observation.valid,
            observation.before.mode, observation.after.mode,
            observation.before.gate, observation.after.gate,
            observation.before.masks[0], observation.before.masks[1],
            observation.after.masks[0], observation.after.masks[1],
            observation.before.values[5], observation.before.values[6],
            observation.after.values[5], observation.after.values[6],
            observation.before.values[2], observation.after.values[2],
            observation.parent.inert, observation.parent.script_size,
            observation.parent.function_flags, observation.parent.actor_latent_index,
            observation.parent.pending_removals, observation.parent.destruction_flags);
    }
