    void service_frame_fencepost_diagnostics() noexcept
    {
        const std::uint64_t entries =
            m_frame_fencepost_entries.load(std::memory_order_acquire);
        const std::uint64_t observations =
            m_frame_fencepost_observations.load(std::memory_order_acquire);
        if (entries != 0 && observations == 0
            && m_frame_fencepost_last_read_mask.load(std::memory_order_acquire)
                != Horse::Deterministic::Schema::Sc6FrameLayout::
                    required_observation_read_mask
            && !m_frame_fencepost_incomplete_logged)
        {
            m_frame_fencepost_incomplete_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] frame-fencepost invoked but native reads "
                "were incomplete mask=0x{:x}\n"),
                m_frame_fencepost_last_read_mask.load(std::memory_order_acquire));
        }
        if (observations != 0 && !m_frame_fencepost_first_observation_logged)
        {
            m_frame_fencepost_first_observation_logged = true;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] frame-fencepost first observation frame={} "
                "manager=0x{:x}\n"),
                m_frame_fencepost_last_frame.load(std::memory_order_acquire),
                m_frame_fencepost_manager.load(std::memory_order_acquire));
        }

        const auto failure = m_frame_fencepost_failure.load(
            std::memory_order_acquire);
        if (failure != Horse::Deterministic::FailureCode::None
            && !m_frame_fencepost_failure_logged)
        {
            m_frame_fencepost_failure_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] frame-fencepost observation failed: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(failure))));
        }

        const std::uint64_t replay_exits =
            m_replay_exit_observations.load(std::memory_order_acquire);
        if (replay_exits != 0 && !m_replay_exit_first_observation_logged)
        {
            m_replay_exit_first_observation_logged = true;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] replay-exit invalidated native identity "
                "before destructive teardown state=0x{:x}\n"),
                m_replay_exit_state.load(std::memory_order_acquire));
        }

        const auto replay_failure = m_replay_exit_failure.load(
            std::memory_order_acquire);
        if (replay_failure != Horse::Deterministic::FailureCode::None
            && !m_replay_exit_failure_logged)
        {
            m_replay_exit_failure_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] replay-exit observation failed: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(replay_failure))));
        }
    }
public:
#if HORSE_ENABLE_GEKKONET
    bool PrepareModuleUnload() noexcept
    {
        if (m_online_lifecycle.CanUnloadModule()) return true;
        if (m_online_lifecycle.phase()
            != Horse::Deterministic::OnlineLifecyclePhase::
                FailClosedAwaitingSceneExit)
            fail_online_qualification(
                Horse::Deterministic::FailureCode::PeerDisconnected);
        return false;
    }
#endif
#if HORSE_ENABLE_OBSERVER_PROBE
    bool ArmOnlineObserverProbe(
        const Horse::Deterministic::OnlineObserverProbeRequest& request) noexcept
    {
        if (m_deterministic_config.enabled || !m_deterministic_config.trace
            || m_deterministic_config.correction_probe
            || m_deterministic_config.forced_depth7_qualification)
            return false;
#if HORSE_ENABLE_GEKKONET
        if (m_online_qualification_requested.load(std::memory_order_acquire)
            || m_online_production_requested.load(std::memory_order_acquire)
            || m_online_coordinator.state()
                != Horse::Deterministic::OnlineState::Disabled
            || m_online_gekko.started())
            return false;
#endif
        return m_online_observer_probe.Arm(request, ::GetTickCount64());
    }

    std::uint32_t GetOnlineObserverProbeReport(
        Horse::Deterministic::OnlineObserverProbeReport& output) const noexcept
    {
        static_cast<void>(m_online_observer_probe.CopyReport(output));
        return static_cast<std::uint32_t>(m_online_observer_probe.state());
    }

    void DisarmOnlineObserverProbe() noexcept
    {
        m_online_observer_probe.Disarm();
    }
#endif
#if HORSE_ENABLE_GEKKONET
    bool ArmOnlineQualification(std::string_view run_id = {},
        std::uint32_t fault_value = 0) noexcept
    {
        if (fault_value > static_cast<std::uint32_t>(
                OnlineQualificationFault::PostownershipPeer))
            return false;
        const auto fault = static_cast<OnlineQualificationFault>(fault_value);
        if (run_id.size() > 96
            || std::any_of(run_id.begin(), run_id.end(), [](char value) {
                return !(std::isalnum(static_cast<unsigned char>(value))
                    || value == '-' || value == '_' || value == '.');
            }))
            return false;
        if (!g_horse_mod_unload_guard_ready.load(std::memory_order_acquire)
            || m_deterministic_config.enabled || !m_deterministic_config.trace
            || m_deterministic_config.correction_probe
            || m_deterministic_config.forced_depth7_qualification
            || !m_deterministic_hooks.installed()
#if HORSE_ENABLE_OBSERVER_PROBE
            || m_online_observer_probe.state()
                == Horse::Deterministic::OnlineObserverProbeState::Armed
#endif
            || m_online_coordinator.state()
                != Horse::Deterministic::OnlineState::Disabled
            || m_online_production_requested.load(std::memory_order_acquire)
            || !m_online_lifecycle.IsClearForStock())
            return false;
        m_online_coordinator.Select(OnlineRuntimeKind::Qualification);
        if (!m_online_lifecycle.ArmPreOwnership().ok()) return false;
        reset_online_session_measurements(run_id, fault);
        m_online_qualification_requested.store(true,
            std::memory_order_release);
        return true;
    }

    std::uint32_t GetOnlineQualificationStatus() const noexcept
    {
        return m_online_qualification_status.load(std::memory_order_acquire);
    }
#endif

    bool RequestQualificationStageTerminal(std::uint32_t operation) noexcept
    {
        if (!m_deterministic_config.trace
            || !m_deterministic_config.forced_depth7_qualification
            || !m_deterministic_hooks.installed()
            || (operation != 1 && operation != 2))
        {
            return false;
        }
        std::uint32_t expected{};
        if (!m_qualification_stage_terminal_request.compare_exchange_strong(
                expected, operation, std::memory_order_acq_rel))
        {
            return false;
        }
        m_qualification_stage_terminal_frame.store(0,
            std::memory_order_release);
        m_qualification_stage_terminal_requested_ms.store(
            ::GetTickCount64(), std::memory_order_release);
        m_qualification_stage_terminal_wait_logged.store(
            false, std::memory_order_release);
        m_qualification_stage_terminal_status.store(1,
            std::memory_order_release);
        const auto timeline = m_replay_native_runtime.timeline_status();
        refresh_stage_break_presentation_identity(
            timeline.last_coordinate.generation);
        return true;
    }

    std::uint32_t GetQualificationStageTerminalStatus(
        std::uint32_t& frame) const noexcept
    {
        frame = m_qualification_stage_terminal_frame.load(
            std::memory_order_acquire);
        return m_qualification_stage_terminal_status.load(
            std::memory_order_acquire);
    }

    std::uint32_t GetForcedQualificationStatus() const noexcept
    {
        const auto& qualification = m_forced_correction_qualification;
        if (qualification.reported)
            return qualification.failure == Horse::Deterministic::FailureCode::None
                ? 2u : 3u;
        return qualification.active && !qualification.warmup_pending ? 1u : 0u;
    }

    bool SetReplayHistoryCaptureRequired(bool required) noexcept
    {
        return m_replay_native_runtime.SetReplayHistoryCaptureRequired(
            required).ok();
    }

    bool CaptureReplayQualificationTerminalEvidence() noexcept
    {
        return CaptureReplayQualificationTerminalSnapshot();
    }

    bool RequestReplaySeek(std::uint64_t frame) noexcept
    {
        if (frame == UINT64_MAX) return false;
        m_seek_request_frame.store(frame, std::memory_order_release);
        m_seek_completed_target.store(0, std::memory_order_release);
        m_seek_request_sequence.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool GetReplaySeekableRange(
        std::uint64_t& generation, std::uint64_t& first_frame,
        std::uint64_t& last_frame) const noexcept
    {
        Horse::Deterministic::FrameCoordinate first{}, last{};
        if (!m_replay_native_runtime.GetSeekableRange(first, last)) return false;
        generation = first.generation;
        first_frame = first.frame;
        last_frame = last.frame;
        return true;
    }

    bool GetReplaySimulationPhase(
        std::int32_t& native_round, std::int32_t& native_time,
        std::uint32_t& round_state_frame,
        std::int32_t& unpause_countdown) noexcept
    {
        return m_replay_native_runtime.ObserveCurrentSimulationPhase(
            native_round, native_time, round_state_frame, unpause_countdown);
    }

    bool GetReplaySeekMetrics(
        std::uint64_t& validation_ns,
        std::uint64_t& resimulation_coordinates) const noexcept
    {
        const auto timeline = m_replay_native_runtime.timeline_status();
        if (timeline.resume_target.generation == 0
            || timeline.last_seek_validation_ns == 0)
        {
            return false;
        }
        validation_ns = timeline.last_seek_validation_ns;
        resimulation_coordinates =
            timeline.last_seek_resimulation_coordinates;
        return true;
    }

    bool GetReplayCanonicalState(std::uint64_t& generation,
        std::uint64_t& frame, std::byte* hash, std::size_t hash_size) const noexcept
    {
        if (hash == nullptr || hash_size != Horse::Deterministic::CanonicalHash{}.size())
            return false;
        const auto timeline = m_replay_native_runtime.timeline_status();
        if (timeline.last_coordinate.generation == 0
            && m_replay_qualification_terminal_snapshot.canonical_valid)
        {
            generation = m_replay_qualification_terminal_snapshot
                .canonical_generation;
            frame = m_replay_qualification_terminal_snapshot.canonical_frame;
            std::copy(m_replay_qualification_terminal_snapshot
                .canonical_hash.begin(),
                m_replay_qualification_terminal_snapshot
                    .canonical_hash.end(), hash);
            return true;
        }
        Horse::Deterministic::CanonicalHash value{};
        if (timeline.last_coordinate.generation == 0
            || !m_replay_native_runtime.GetCanonicalHash(
                timeline.last_coordinate, value).ok())
            return false;
        generation = timeline.last_coordinate.generation;
        frame = timeline.last_coordinate.frame;
        std::copy(value.begin(), value.end(), hash);
        return true;
    }

    bool GetReplayCanonicalComponents(
        std::uint64_t* components, std::size_t count) noexcept
    {
        if (components == nullptr || count < 6) return false;
        Horse::Deterministic::Snapshot snapshot{};
        if (!m_replay_native_runtime.CaptureCurrentCanonical(snapshot).ok())
            return false;
        components[0] = snapshot.context_identity;
        std::copy(snapshot.canonical_components.begin(),
            snapshot.canonical_components.end(), components + 1);
        if (count >= 66)
        {
            std::copy(snapshot.canonical_wind.begin(),
                snapshot.canonical_wind.end(), components + 6);
            std::copy(snapshot.canonical_wind_semantic.begin(),
                snapshot.canonical_wind_semantic.end(), components + 26);
            components[58] = snapshot.canonical_wind_node.life_bits;
            components[59] = static_cast<std::uint32_t>(
                snapshot.canonical_wind_node.oscillator_tick);
            components[60] = snapshot.canonical_wind_node.prepared;
            components[61] = snapshot.canonical_wind_node.active;
            components[62] = snapshot.canonical_wind_node.frame_step_bits;
            components[63] = static_cast<std::uint32_t>(
                snapshot.canonical_wind_node.repeat_count);
            components[64] = snapshot.canonical_wind_node.kind;
            components[65] = snapshot.canonical_wind_node.present;
        }
        if (count >= 72)
            std::copy(snapshot.canonical_wind_schedule.begin(),
                snapshot.canonical_wind_schedule.end(), components + 66);
        return true;
    }

    bool GetReplayPresentationCoverage(
        std::uint64_t* counts, std::size_t count) const noexcept
    {
        if (counts == nullptr || count < 10) return false;
        const auto timeline = m_replay_native_runtime.timeline_status();
        if (timeline.canonical_frames == 0
            && m_replay_qualification_terminal_snapshot
                .presentation_coverage_valid)
        {
            std::copy(m_replay_qualification_terminal_snapshot
                .presentation_coverage.begin(),
                m_replay_qualification_terminal_snapshot
                    .presentation_coverage.end(), counts);
            return true;
        }
        counts[0] = timeline.observed_stage_wall_calls;
        counts[1] = timeline.observed_stage_barrier_calls;
        counts[2] = timeline.observed_stage_dispatch_calls;
        counts[3] = timeline.observed_battle_audio_dispatches;
        counts[4] = timeline.observed_battle_audio_direct_dispatches;
        counts[5] = timeline.observed_battle_audio_remap_calls;
        counts[6] = timeline.observed_battle_audio_source_calls;
        counts[7] = timeline.observed_battle_audio_stop_all_calls;
        counts[8] = timeline.observed_battle_audio_blueprint_calls;
        counts[9] = timeline.observed_particle_spawn_calls;
        return timeline.canonical_frames != 0;
    }

    bool GetReplayPresentationIdentity(
        std::uint64_t* values, std::size_t count) const noexcept
    {
        if (values == nullptr || count < 9) return false;
        if (m_replay_native_runtime.timeline_status().canonical_frames == 0
            && m_replay_qualification_terminal_snapshot
                .presentation_identity_valid)
        {
            std::copy(m_replay_qualification_terminal_snapshot
                .presentation_identity.begin(),
                m_replay_qualification_terminal_snapshot
                    .presentation_identity.end(), values);
            return true;
        }
        std::array<std::uint64_t, 9> identity{};
        if (!m_replay_native_runtime.GetPresentationIdentity(identity))
            return false;
        std::copy(identity.begin(), identity.end(), values);
        return true;
    }

    bool GetReplayAudioBatchIdentity(std::size_t batch_index,
        std::uint64_t* values, std::size_t count) const noexcept
    {
        if (values == nullptr || count < 8) return false;
        const auto& batches = m_replay_native_runtime.batch_timeline();
        const auto* batch = batches.GetBatch(batch_index);
        if (batch == nullptr) return false;
        values[0] = batch->entry_coordinate.generation;
        values[1] = batch->entry_coordinate.frame;
        values[2] = batch->battle_audio_dispatches;
        values[3] = batch->battle_audio_sequence_hash;
        values[4] = batch->battle_audio_route_hash;
        values[5] = batch->battle_audio_payload_hash;
        values[6] = batch->battle_audio_position_hash;
        values[7] = batch->battle_audio_journal_count;
        if (count >= 10)
        {
            values[8] = batch->audio_terminal_calls;
            values[9] = batch->audio_terminal_hash;
        }
        return true;
    }

    bool GetReplayAudioTerminalIdentity(std::size_t batch_index,
        std::size_t terminal_index, std::uint64_t* values,
        std::size_t count) const noexcept
    {
        if (values == nullptr || count < 8) return false;
        const auto& batches = m_replay_native_runtime.batch_timeline();
        const auto* batch = batches.GetBatch(batch_index);
        if (batch == nullptr
            || terminal_index >= batch->audio_terminal_journal_count)
            return false;
        const auto& terminal = batch->audio_terminal_journal[terminal_index];
        values[0] = static_cast<std::uint8_t>(terminal.operation);
        values[1] = static_cast<std::uint8_t>(terminal.owner.domain);
        values[2] = terminal.owner.index;
        values[3] = terminal.owner.scope_id;
        values[4] = terminal.logical_playback_id;
        values[5] = terminal.cue_sheet_id;
        values[6] = static_cast<std::uint32_t>(terminal.cue_id);
        values[7] = terminal.value;
        if (count >= 9)
            values[8] = batch->audio_terminal_return_rvas[terminal_index];
        if (count >= 10)
            values[9] =
                batch->audio_terminal_raw_cue_sheet_ids[terminal_index];
        return true;
    }

    bool GetReplayAudioDispatchIdentity(std::size_t batch_index,
        std::size_t dispatch_index, std::uint64_t* values,
        std::size_t count) const noexcept
    {
        if (values == nullptr || count < 7) return false;
        const auto& batches = m_replay_native_runtime.batch_timeline();
        const auto* batch = batches.GetBatch(batch_index);
        if (batch == nullptr
            || dispatch_index >= batch->battle_audio_journal_count)
            return false;
        const auto& dispatch = batch->battle_audio_journal[dispatch_index];
        std::uint32_t payload_ext{};
        std::uint64_t payload{};
        std::uint32_t reserved{};
        std::memcpy(&payload_ext, dispatch.semantic.data() + 1,
            sizeof(payload_ext));
        std::memcpy(&payload, dispatch.semantic.data() + 5,
            sizeof(payload));
        std::memcpy(&reserved, dispatch.semantic.data() + 13,
            sizeof(reserved));
        values[0] = std::to_integer<std::uint8_t>(dispatch.semantic[0]);
        values[1] = payload_ext;
        values[2] = payload;
        values[3] = reserved;
        values[4] = std::to_integer<std::uint8_t>(dispatch.semantic[17]);
        values[5] = dispatch.direct;
        values[6] = dispatch.succeeded;
        return true;
    }

    bool GetReplayQualificationHealth(
        std::uint64_t* values, std::size_t count) const noexcept
    {
        if (values == nullptr || count < 7) return false;
        if (m_replay_native_runtime.timeline_status().canonical_frames == 0
            && m_replay_qualification_terminal_snapshot.health_valid)
        {
            const auto copied = (std::min)(count,
                m_replay_qualification_terminal_snapshot.health.size());
            std::copy_n(m_replay_qualification_terminal_snapshot.health.begin(),
                copied, values);
            return true;
        }
        const auto presentation =
            m_replay_native_runtime.presentation_statistics();
        const auto capture = m_replay_native_runtime.capture_performance();
        const auto timeline = m_replay_native_runtime.timeline_status();
        const auto storage = m_replay_native_runtime.owned_storage_status();
        const auto observer_delta = [](std::uint64_t value,
                                        std::uint64_t baseline) noexcept {
            return value >= baseline ? value - baseline : value;
        };
        const std::uint64_t cursor_mismatches = observer_delta(
            timeline.cursor_mismatches,
            m_replay_qualification_cursor_mismatch_baseline);
        const std::uint64_t batch_accounting_mismatches = observer_delta(
            timeline.batch_frame_accounting_mismatches,
            m_replay_qualification_batch_accounting_mismatch_baseline);
        const std::uint64_t round_transition_barriers = observer_delta(
            timeline.round_transition_cursor_barriers,
            m_replay_qualification_round_transition_barrier_baseline);
        values[0] = presentation.capacity_failures;
        values[1] = capture.scratch_capacity_growth_events;
        values[2] = cursor_mismatches + batch_accounting_mismatches;
        values[3] = storage.aggregate_bytes;
        values[4] = storage.presentation_bytes;
        values[5] = presentation.duplicates;
        values[6] = presentation.publish_failures;
        if (count >= 21)
            for (std::size_t index = 0;
                 index < capture.scratch_owner_count; ++index)
            {
                values[7 + index] =
                    capture.scratch_capacity_baseline_by_owner[index];
                values[14 + index] =
                    capture.scratch_capacity_high_water_by_owner[index];
            }
        // Append-only observer diagnostics. Preserve the original 7/21-value
        // contracts for older harnesses.
        if (count >= 36)
        {
            values[21] = static_cast<std::uint64_t>(timeline.failure);
            values[22] = timeline.last_coordinate.generation;
            values[23] = timeline.last_coordinate.frame;
            values[24] =
                timeline.canonical_capture_failure_coordinate.generation;
            values[25] = timeline.canonical_capture_failure_coordinate.frame;
            values[26] = timeline.identity_issue;
            values[27] = static_cast<std::uint64_t>(
                presentation.first_publish_failure);
            values[28] = presentation.first_failed_event.kind;
            values[29] = presentation.first_failed_event.identity;
            const std::uint64_t fp_mismatches = timeline.fp_control_mismatches
                + timeline.fp_status_mismatches
                + timeline.fp_x87_status_mismatches
                + timeline.fp_mxcsr_status_mismatches;
            // The qualification module resets this observer window only after
            // the authored replay is active. Setup batches remain available in
            // the full timeline diagnostics, but must not masquerade as an
            // active-replay terminal failure.
            values[30] = fp_mismatches >=
                    m_replay_qualification_fp_mismatch_baseline
                ? fp_mismatches - m_replay_qualification_fp_mismatch_baseline
                : fp_mismatches;
            values[31] = timeline.observed_gameplay_xorshift_unknown_callers;
            values[32] = timeline.identity_expected;
            values[33] = timeline.identity_observed;
            values[34] = timeline.observed_battle_audio_source_calls;
            values[35] = timeline.observed_audio_terminal_calls;
        }
        if (count >= 39)
        {
            values[36] = cursor_mismatches;
            values[37] = batch_accounting_mismatches;
            values[38] = round_transition_barriers;
        }
        if (count >= 48)
        {
            values[39] =
                timeline.last_cursor_mismatch_coordinate.generation;
            values[40] = timeline.last_cursor_mismatch_coordinate.frame;
            values[41] = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    timeline.last_cursor_mismatch_input_round));
            values[42] = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    timeline.last_cursor_mismatch_input_time));
            values[43] = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    timeline.last_cursor_mismatch_manager_round));
            values[44] = timeline.last_cursor_mismatch_manager_time;
            values[45] = timeline.last_cursor_mismatch_pending_dispatch;
            values[46] = timeline.last_cursor_mismatch_round_image_applied;
            values[47] = timeline.last_cursor_mismatch_round_state;
        }
        if (count >= 49)
            values[48] = timeline.partial ? 1 : 0;
        if (count >= 54)
        {
            values[49] = static_cast<std::uint64_t>(timeline.partial_reason);
            values[50] = timeline.partial_coordinate.generation;
            values[51] = timeline.partial_coordinate.frame;
            values[52] = static_cast<std::uint64_t>(
                timeline.checkpoint_failure);
            values[53] = static_cast<std::uint64_t>(
                timeline.batch_entry_checkpoint_failure);
        }
        return timeline.canonical_frames != 0
            || timeline.failure
                != Horse::Deterministic::FailureCode::None
            || timeline.partial
            || presentation.capacity_failures != 0
            || presentation.duplicates != 0
            || presentation.publish_failures != 0;
    }

    bool ResetReplayQualificationHealth() noexcept
    {
        m_replay_qualification_terminal_snapshot = {};
        const auto timeline = m_replay_native_runtime.timeline_status();
        m_replay_qualification_fp_mismatch_baseline =
            timeline.fp_control_mismatches
            + timeline.fp_status_mismatches
            + timeline.fp_x87_status_mismatches
            + timeline.fp_mxcsr_status_mismatches;
        // Preserve the native lifetime counters and any latched FailureCode.
        // Qualification health is an observer-only window beginning at the
        // first active authored replay frame, so pre-active setup accounting
        // cannot masquerade as an active replay failure.
        m_replay_qualification_cursor_mismatch_baseline =
            timeline.cursor_mismatches;
        m_replay_qualification_batch_accounting_mismatch_baseline =
            timeline.batch_frame_accounting_mismatches;
        m_replay_qualification_round_transition_barrier_baseline =
            timeline.round_transition_cursor_barriers;
        m_replay_native_runtime.ResetCapturePerformanceWindow();
        return true;
    }

    bool GetReplayGameplayRngCoverage(
        std::uint64_t* counts, std::size_t count) const noexcept
    {
        if (counts == nullptr || count < 42) return false;
        const auto timeline = m_replay_native_runtime.timeline_status();
        if (timeline.canonical_frames == 0
            && m_replay_qualification_terminal_snapshot
                .gameplay_rng_coverage_valid)
        {
            std::copy(m_replay_qualification_terminal_snapshot
                .gameplay_rng_coverage.begin(),
                m_replay_qualification_terminal_snapshot
                    .gameplay_rng_coverage.end(), counts);
            return true;
        }
        counts[0] = timeline.observed_gameplay_xorshift_draws;
        counts[1] = timeline.observed_gameplay_xorshift_known_callers;
        counts[2] = timeline.observed_gameplay_xorshift_unknown_callers;
        counts[3] = timeline.observed_gameplay_xorshift_weighted_draws;
        counts[4] = timeline.observed_gameplay_xorshift_if_draws;
        counts[5] = timeline.observed_movevm_short25_changes[0];
        counts[6] = timeline.observed_movevm_short25_changes[1];
        counts[7] = timeline.observed_probability_transition_batches;
        counts[8] = timeline.observed_movevm_state_changes[0];
        counts[9] = timeline.observed_movevm_state_changes[1];
        for (std::size_t word = 0; word < 4; ++word)
        {
            counts[10 + word] = timeline
                .observed_probability_changed_state_short_masks[0][word];
            counts[14 + word] = timeline
                .observed_probability_changed_state_short_masks[1][word];
        }
        counts[18] = timeline.observed_movevm_transition_07_calls;
        counts[19] = timeline.observed_tira_random_transition_calls;
        counts[20] = timeline.observed_tira_probability_transition_batches;
        counts[21] = timeline.observed_tira_random_transition_target_mask;
        counts[22] = timeline.observed_gameplay_xorshift_sequence_hash;
        counts[23] = timeline.observed_movevm_transition_07_sequence_hash;
        counts[24] = timeline.observed_tira_random_transition_sequence_hash;
        counts[25] = timeline.observed_tira_stance_transition_batches;
        counts[26] = timeline.observed_tira_character_slot_mask;
        counts[27] = timeline.observed_movevm_short25_sequence_hash[0];
        counts[28] = timeline.observed_movevm_short25_sequence_hash[1];
        counts[29] = timeline.initial_movevm_short25[0];
        counts[30] = timeline.initial_movevm_short25[1];
        counts[31] = timeline.final_movevm_short25[0];
        counts[32] = timeline.final_movevm_short25[1];
        counts[33] = timeline.final_gameplay_xorshift_state[0];
        counts[34] = timeline.final_gameplay_xorshift_state[1];
        counts[35] = timeline.final_gameplay_xorshift_state[2];
        counts[36] = timeline.observed_tira_state19_at_transition[0];
        counts[37] = timeline.observed_tira_state19_at_transition[1];
        counts[38] = timeline.movevm_short25_initial_recorded ? 1 : 0;
        counts[39] = timeline.observed_tira_last_transition_target;
        counts[40] = timeline.observed_resolved_hit_calls;
        counts[41] = timeline.observed_resolved_hit_sequence_hash;
        return timeline.canonical_frames != 0;
    }

    bool CaptureReplayQualificationTerminalSnapshot() noexcept
    {
        ReplayQualificationTerminalSnapshot captured{};
        captured.presentation_coverage_valid = GetReplayPresentationCoverage(
            captured.presentation_coverage.data(),
            captured.presentation_coverage.size());
        captured.presentation_identity_valid = GetReplayPresentationIdentity(
            captured.presentation_identity.data(),
            captured.presentation_identity.size());
        captured.health_valid = GetReplayQualificationHealth(
            captured.health.data(), captured.health.size());
        captured.gameplay_rng_coverage_valid = GetReplayGameplayRngCoverage(
            captured.gameplay_rng_coverage.data(),
            captured.gameplay_rng_coverage.size());
        captured.canonical_valid = GetReplayCanonicalState(
            captured.canonical_generation, captured.canonical_frame,
            captured.canonical_hash.data(), captured.canonical_hash.size());

        // PostTick is a guarded world-mode boundary, not a promise that this
        // particular transition completed a canonical frame. A later
        // zero-frame transition must not erase the last complete value-only
        // evidence captured for the current qualification request. The reset
        // API is the sole boundary that starts a new observer window.
        auto& terminal = m_replay_qualification_terminal_snapshot;
        if (captured.presentation_coverage_valid)
        {
            terminal.presentation_coverage_valid = true;
            terminal.presentation_coverage = captured.presentation_coverage;
        }
        if (captured.presentation_identity_valid)
        {
            terminal.presentation_identity_valid = true;
            terminal.presentation_identity = captured.presentation_identity;
        }
        if (captured.health_valid)
        {
            terminal.health_valid = true;
            terminal.health = captured.health;
        }
        if (captured.gameplay_rng_coverage_valid)
        {
            terminal.gameplay_rng_coverage_valid = true;
            terminal.gameplay_rng_coverage = captured.gameplay_rng_coverage;
        }
        if (captured.canonical_valid)
        {
            terminal.canonical_valid = true;
            terminal.canonical_generation = captured.canonical_generation;
            terminal.canonical_frame = captured.canonical_frame;
            terminal.canonical_hash = captured.canonical_hash;
        }
        const bool complete = terminal.presentation_coverage_valid
            && terminal.presentation_identity_valid && terminal.health_valid
            && terminal.gameplay_rng_coverage_valid && terminal.canonical_valid;
        if (!complete && m_deterministic_config.trace)
        {
            const auto timeline = m_replay_native_runtime.timeline_status();
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] terminal replay evidence incomplete "
                "coverage={} identity={} health={} rng={} canonical={} "
                "coordinate={}:{} canonical_frames={} batches={} "
                "rebaselines={} partial={} failure={}\n"),
                terminal.presentation_coverage_valid ? 1 : 0,
                terminal.presentation_identity_valid ? 1 : 0,
                terminal.health_valid ? 1 : 0,
                terminal.gameplay_rng_coverage_valid ? 1 : 0,
                terminal.canonical_valid ? 1 : 0,
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame, timeline.canonical_frames,
                m_replay_native_runtime.batch_timeline().batch_count(),
                timeline.identity_rebaselines, timeline.partial ? 1 : 0,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        timeline.failure))));
        }
        return complete;
    }

    std::uint32_t GetReplaySeekStatus(
        std::uint64_t& target_frame,
        std::uint64_t& source_end_frame,
        std::uint64_t& verified_frames,
        std::uint16_t& failure) const noexcept
    {
        const auto& timeline = m_replay_native_runtime.timeline_status_view();
        const auto completed_target = m_seek_completed_target.load(
            std::memory_order_acquire);
        if (completed_target != 0)
        {
            target_frame = completed_target;
            source_end_frame = m_seek_completed_source.load(
                std::memory_order_relaxed);
            verified_frames = m_seek_completed_verified.load(
                std::memory_order_relaxed);
            failure = 0;
            return 1;
        }
        target_frame = timeline.resume_target.frame;
        source_end_frame = timeline.resume_source_end.frame;
        verified_frames = timeline.resumed_frames_verified;
        const auto hook_failure = m_frame_fencepost_failure.load(
            std::memory_order_acquire);
        const auto effective_failure = timeline.failure
            != Horse::Deterministic::FailureCode::None
            ? timeline.failure : hook_failure;
        failure = static_cast<std::uint16_t>(effective_failure);
        if (effective_failure != Horse::Deterministic::FailureCode::None)
            return 3;
        if (timeline.resume_validation_active) return 2;
        if (timeline.resume_target.generation != 0) return 1;
        return 0;
    }

    HorseMod() : CppUserModBase()
    {
        ModName        = STR("HorseMod");
        ModVersion     = STR("0.10.0");
        ModDescription = STR("SC6 KHit hitbox / hurtbox / body visualiser.");
        ModAuthors     = STR("horse");

        horsemod_report_unsupported_legacy_options_once();

        const std::filesystem::path deterministic_config_path =
            std::filesystem::path(horsemod_current_module_path()).parent_path()
            / L"rollback.ini";
        m_deterministic_config_present =
            std::filesystem::exists(deterministic_config_path);
        auto deterministic_load =
            Horse::Deterministic::LoadConfig(deterministic_config_path);
        m_deterministic_config = deterministic_load.config;
        m_replay_native_runtime.SetForcedDepth7QualificationEnabled(
            m_deterministic_config.forced_depth7_qualification);
        m_replay_native_runtime.SetCorrectedInputQualificationEnabled(
            m_deterministic_config.correction_probe);
        if (deterministic_load.status.ok() && m_deterministic_config.trace)
        {
            const auto report_path = deterministic_config_path.parent_path()
                / L"reports" / L"deterministic"
                / L"hgcpu_coverage_runtime.md";
            m_hgcpu_runtime_diagnostics = std::make_unique<
                Horse::Deterministic::HgCpuRuntimeDiagnostics>(report_path);
            m_stage_break_listener_diagnostics = std::make_unique<
                Horse::Deterministic::StageBreakListenerRuntimeDiagnostics>(
                    report_path.parent_path() / L"stage_break_listener_topology.md");
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] deterministic runtime diagnostics armed; "
                "stock simulation remains active\n"));
        }
        if (!deterministic_load.status.ok())
        {
            m_deterministic_config.enabled = false;
            m_deterministic_failure = deterministic_load.status.code;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] rollback.ini is invalid; deterministic simulation "
                "remains disabled\n"));
        }
        else if (m_deterministic_config.enabled
                 && Horse::Deterministic::Schema::production_regions.empty())
        {
            m_deterministic_failure =
                Horse::Deterministic::FailureCode::AdapterUnqualified;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] rollback.ini requested activation, but no native "
                "state regions are qualified; stock behavior is unchanged\n"));
        }
        if (deterministic_load.status.ok()
            && !deterministic_load.diagnostics.empty()
            && m_deterministic_config_present)
        {
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] rollback.ini contained unsupported or invalid "
                "configuration; see the deterministic status panel\n"));
        }

        // The overlay Present hook still waits for Steam's first frames, but
        // SC6 may decide whether to poll XInput during title/menu bootstrap.
        // Install only the overlay's XInput gate here; rollback automation
        // uses native request-file orchestration, not OS/controller scripts.
        (void)Horse::GameImGui::XInputHook::instance().install();

        // Load persisted settings BEFORE any render path can observe
        // an atomic.  If settings.cfg is missing (first-run) each
        // get_* call returns its default argument, matching the
        // compiled-in defaults - functionally identical to the
        // pre-persistence behaviour on a clean install.
        load_persisted_settings();

        // Materialize a first-run settings.cfg immediately instead of
        // relying on the later on_update save tick.  This keeps fresh
        // Thunderstore profiles inspectable even if the user exits from
        // the title screen before the periodic save runs.
        save_persisted_settings();

        // Populate reset-hook candidate list.  Registration is attempted
        // (and retried) from on_update once each slot's containing class
        // is loaded.  See the ResetHookSlot doc-comment for why we hook
        // multiple paths instead of just one.
        //
        // Class-path verification (cross-checked against UHTHeaderDump):
        //   LuxBattleManager : TrainingModePositionReset, RestartBattle,
        //                      RestartBattleImmediately
        //   LuxBattleGameMode: RequestTrainingModeBattleReset(side)
        // Earlier builds put RequestTrainingModeBattleReset on
        // LuxBattleFunctionLibrary - that's wrong and crashed startup
        // because UE4SS's RegisterHook(StringType) dereferences the
        // result of StaticFindObject<UFunction*> without a null check
        // when the path doesn't resolve.
        m_reset_slots = {
            { STR("/Script/LuxorGame.LuxBattleManager"),
              STR("/Script/LuxorGame.LuxBattleManager:TrainingModePositionReset") },
            { STR("/Script/LuxorGame.LuxBattleManager"),
              STR("/Script/LuxorGame.LuxBattleManager:RestartBattle") },
            { STR("/Script/LuxorGame.LuxBattleManager"),
              STR("/Script/LuxorGame.LuxBattleManager:RestartBattleImmediately") },
            { STR("/Script/LuxorGame.LuxBattleGameMode"),
              STR("/Script/LuxorGame.LuxBattleGameMode:RequestTrainingModeBattleReset") },
        };

        Input::ModifierKeyArray no_mods{};
        no_mods.fill(Input::ModifierKey::MOD_KEY_START_OF_ENUM);

        register_keydown_event(Input::Key::F5, no_mods, [this]() {
            bool s = !m_enabled.load();
            m_enabled.store(s);
            Output::send<LogLevel::Verbose>(STR("[HorseMod] overlay {}\n"),
                s ? STR("ON") : STR("OFF"));
            if (!s)
            {
                // Hide on all KHit backends so neither persistent trails
                // nor one-frame fallback lines survive overlay-off.
                hide_khit_overlay_lines();
            }
        });

        // F6 - single-frame step.  Lazily turns on Freeze-frame on first
        // press so the user doesn't need to touch the ImGui tab.  Holding
        // F6 yields ~30 fps slow-motion via UE4SS's keyboard auto-repeat:
        // each press queues one frame; the cockpit-hook state machine
        // drains them one per two cockpit ticks (see frame_step_apply).
        //
        // F7 / F6 both honour the General-tab "Auto disable online"
        // gate - if we're in a Ranked / Casual match with the gate on,
        // pressing the hotkey is a no-op (with a one-line log so the
        // user knows their press was ignored, not lost).  This matches
        // the UI checkbox behaviour: the ImGui control is greyed out
        // and clicking does nothing; the hotkey shouldn't be a
        // back-door around that.
        register_keydown_event(Input::Key::F7, no_mods, [this]() {
            if (Horse::GameMode::instance().should_force_disable_features())
            {
                Output::send<LogLevel::Default>(
                    STR("[HorseMod] F7 ignored - Free-fly camera is "
                        "auto-disabled in online matches.\n"));
                return;
            }
            bool s = !m_free_camera_enabled.load();
            m_free_camera_enabled.store(s);
            Output::send<LogLevel::Verbose>(STR("[HorseMod] free-camera {}\n"),
                s ? STR("ON") : STR("OFF"));
        });

        register_keydown_event(Input::Key::F6, no_mods, [this]() {
            // First press while running: latch Freeze ON so the user
            // sees an immediate freeze and the step actually advances
            // a SINGLE frame instead of letting the engine free-run.
            //
            // Note: the HUD "Step 1 (F6)" button (see render_tab_impl)
            // does NOT latch freeze - it only adds to step_pending and
            // is greyed out unless Freeze is already on.  That's by
            // design: the button path expects the user to have opened
            // the HUD and turned on Freeze deliberately, whereas the
            // hotkey path is the "just press F6 and it works"
            // convenience entry-point.
            if (Horse::GameMode::instance().should_force_disable_features())
            {
                Output::send<LogLevel::Default>(
                    STR("[HorseMod] F6 ignored - Freeze frame is "
                        "auto-disabled in online matches.\n"));
                return;
            }
            if (!m_freeze_frame.load())
            {
                m_freeze_frame.store(true);
            }
            m_step_pending.fetch_add(1);
        });

        // NOTE: the old UE4SS register_tab(...) call lived here.  We no
        // longer register our tab with UE4SS's external GUI - the tab
        // is now hosted in-game via Horse::GameImGui (see on_unreal_init
        // below).  Removing the UE4SS registration means the HorseMod
        // tab no longer appears in the separate "UE4SS Debugging Tools"
        // window; it draws directly into the SC6 window instead.

        // F2 toggles the in-game ImGui overlay visibility.
        //
        // Why UE4SS's register_keydown_event (and NOT a WndProc hook
        // inside GameImGui): SC6 registers RawInput with the
        // RIDEV_NOLEGACY flag, which suppresses WM_KEYDOWN on the
        // game HWND at the OS level.  UE4SS's keydown_event uses a
        // WH_KEYBOARD_LL low-level hook underneath, which is the
        // only reliable way to catch keys past NOLEGACY - this is
        // the same trick Horse::LowLevelKeyInput uses for F5/F6/F7.
        //
        // Back/Select on the gamepad also toggles the overlay; that
        // half is wired inside horselib/GameImGui/GamepadInput.hpp's
        // BACK-button edge detector.
        register_keydown_event(Input::Key::F2, no_mods, [this]() {
            if (m_gameimgui_toggle_key_down.exchange(
                    true, std::memory_order_acq_rel))
            {
                return;
            }

            bool v = !Horse::GameImGui::visible();
            Horse::GameImGui::set_visible(v);
            Output::send<LogLevel::Default>(
                STR("[HorseMod] F2 pressed - overlay {}\n"),
                v ? STR("SHOWN") : STR("HIDDEN"));
        });

        s_instance.store(this);
        Output::send<LogLevel::Default>(
            STR("[HorseMod] ctor v{} source={}\n"),
            RC::to_generic_string(HORSEMOD_VERSION),
            RC::to_generic_string(HORSEMOD_SOURCE_COMMIT));
    }

    ~HorseMod() override
    {
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor ENTER\n"));

        if (m_hgcpu_runtime_diagnostics)
            m_hgcpu_runtime_diagnostics->Finish();
        if (m_stage_break_listener_diagnostics)
            m_stage_break_listener_diagnostics->Finish();

#if HORSE_ENABLE_GEKKONET
        m_online_production_requested.store(false, std::memory_order_release);
        m_online_production_reentry_pending.store(
            false, std::memory_order_release);
        if (m_online_lifecycle.phase()
                == Horse::Deterministic::OnlineLifecyclePhase::PreOwnership
            || m_online_lifecycle.phase()
                == Horse::Deterministic::OnlineLifecyclePhase::SceneExitCleanup)
            reset_online_qualification_preownership();
        else if (m_online_lifecycle.IsClearForStock())
        {
            m_online_gekko.Stop();
            m_online_coordinator.Disable();
        }
        else
        {
            const auto state = m_online_coordinator.state();
            if (state == Horse::Deterministic::OnlineState::Active
                || state == Horse::Deterministic::OnlineState::RoundBarrier)
                static_cast<void>(m_online_coordinator.ReturnToLobby());
            m_online_gekko.Stop();
            online_transport().Stop();
        }
#endif
#if HORSE_ENABLE_OBSERVER_PROBE
        m_online_observer_probe.Disarm();
#endif
        m_deterministic_hooks.Uninstall();
        m_ucrt_rand_broker.Stop();
        if (m_deterministic_config.trace)
        {
            const auto failure = m_frame_fencepost_failure.load(
                std::memory_order_acquire);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] frame-fencepost summary observed={} repeats={} "
                "generations={} replay_exits={} failure={}\n"),
                m_frame_fencepost_observations.load(std::memory_order_acquire),
                m_frame_fencepost_repeats.load(std::memory_order_acquire),
                m_frame_fencepost_generations.load(std::memory_order_acquire),
                m_replay_exit_observations.load(std::memory_order_acquire),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(failure))));
        }
        m_replay_native_runtime.Shutdown();

        // Final settings save - catches anything the periodic
        // on_update save would have missed in the last sub-2s window
        // before shutdown.  Crashes lose at most the most-recent
        // ~2-second window of changes; graceful exits lose nothing.
        save_persisted_settings();

        // Restore visual-only stage hiding before the UObject hook is
        // removed so a graceful unload cannot leave stage actors hidden.
        m_stage_visuals.restoreNow();

        // Zero instance pointer early so any in-flight hook sees null.
        s_instance.store(nullptr);

        if (m_engine_tick_callback_id != RC::Unreal::Hook::ERROR_ID)
        {
            (void)RC::Unreal::Hook::UnregisterCallback(
                m_engine_tick_callback_id);
            Output::send<LogLevel::Verbose>(STR(
                "[HorseMod] dtor unregistered engine tick callback id={}\n"),
                m_engine_tick_callback_id);
            m_engine_tick_callback_id = RC::Unreal::Hook::ERROR_ID;
        }

        // Tear down the in-game ImGui overlay BEFORE unregistering the
        // cockpit hook.  Order matters only loosely here, but calling
        // shutdown() synchronises: it unHooks the DXGI vtable (Present
        // calls immediately revert to whatever was installed before
        // us - usually Steam's hook directly), restores the game's
        // WndProc, and releases our D3D11 RTV.  After this returns no
        // further render_tab_impl calls can happen from our hook.
        if (m_gameimgui_tab_token)
        {
            Horse::GameImGui::unregister_tab(m_gameimgui_tab_token);
            m_gameimgui_tab_token = 0;
        }
        if (m_gameimgui_toast_token)
        {
            Horse::GameImGui::unregister_passive_draw_callback(
                m_gameimgui_toast_token);
            m_gameimgui_toast_token = 0;
        }
        Horse::GameImGui::shutdown();

        if (m_hook_registered && !m_hook_path.empty())
        {
            UObjectGlobals::UnregisterHook(m_hook_path, m_hook_ids);
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor unregistered cockpit hook pre={} post={}\n"),
                m_hook_ids.first, m_hook_ids.second);
        }
        for (auto& slot : m_reset_slots)
        {
            if (!slot.registered) continue;
            UObjectGlobals::UnregisterHook(slot.func_path, slot.ids);
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor unregistered reset hook {} pre={} post={}\n"),
                slot.func_path, slot.ids.first, slot.ids.second);
            slot.registered = false;
        }
        if (m_battle_terminate_hook_registered)
        {
            UObjectGlobals::UnregisterHook(
                m_battle_terminate_hook_path, m_battle_terminate_hook_ids);
            m_battle_terminate_hook_registered = false;
        }

        // Tear down the C++-level SetStartPosition detour cleanly so the
        // reloaded mod (e.g. dev iteration) doesn't double-hook on its
        // next install.  Idempotent if install never succeeded.
        Horse::SetStartPositionHook::instance().uninstall();

        // Tear down all online-rules UFunction hooks (SlipOut + any
        // future implemented rules).  Idempotent.
        Horse::OnlineRules::instance().uninstall_hooks();

        // Tear down the launcher-Start PolyHook detour cleanly so a
        // hot-reload of the mod doesn't double-hook on its next install.
        // Idempotent if install never succeeded.
        Horse::LuxBattleLauncherStartHook::instance().uninstall();

        // Tear down the SlipOut runtime-gate PolyHook detour.
        // Idempotent if install never succeeded.
        Horse::HasSubProviderEntryHook::instance().uninstall();

        // Tear down the SetPresence post-hook so the lambda doesn't
        // fire on a freed cached path-string after dllmain unload.
        // Idempotent.
        Horse::GameMode::instance().uninstall_hook();

        // m_cam_lock will restore any active patches via its own dtor
        // when our member destruction runs after this body returns.
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor EXIT\n"));
    }

    // No on_ui_init() override - UE4SS_ENABLE_IMGUI() set up the shared
    // ImGui context + allocator for the UE4SS external window.  We host
    // our own ImGui context inside Horse::GameImGui (see on_unreal_init),
    // so we skip UE4SS's wiring entirely.  The allocator remains the
    // default (malloc/free via ImGui), which is fine for an isolated
    // context.

    auto on_unreal_init() -> void override
    {
        // -----------------------------------------------------------
        // In-game ImGui overlay bring-up.
        //
        // Register callbacks now, but delay the DXGI vtable swap for a
        // few game-thread updates.  Fresh Steam launches can still be
        // finishing gameoverlayrenderer64.dll's first Present path when
        // on_unreal_init runs; installing our vtable hook immediately can
        // let Steam re-enter Present through our slot and recurse until
        // stack overflow.  The delayed install gives Steam's code-patched
        // DXGI hook a few normal frames before HorseMod starts rendering.
        // -----------------------------------------------------------
        m_gameimgui_init_pending = true;
        m_gameimgui_init_attempted = false;
        m_gameimgui_init_delay_ticks_remaining =
            kGameImGuiDeferredInstallTicks;
        Output::send<LogLevel::Default>(
            STR("[HorseMod] scheduled GameImGui install after {} "
                "game-thread ticks\n"),
            kGameImGuiDeferredInstallTicks);
        m_gameimgui_tab_token = Horse::GameImGui::register_tab(
            L"HorseMod", [this] { this->render_tab_impl(); });
        m_gameimgui_toast_token =
            Horse::GameImGui::register_passive_draw_callback([] {
                return Horse::GameImGui::ToastManager::instance().draw();
            });

        RC::Unreal::Hook::FCallbackOptions engine_tick_opts{};
        engine_tick_opts.bReadonly = true;
        engine_tick_opts.OwnerModName = STR("HorseMod");
        engine_tick_opts.HookName = STR("OverlayService");
        m_engine_tick_callback_id =
            RC::Unreal::Hook::RegisterEngineTickPostCallback(
                [](RC::Unreal::Hook::TCallbackIterationData<void>&,
                   RC::Unreal::UEngine*, float, bool) {
                    HorseMod* self = s_instance.load(std::memory_order_acquire);
                    if (!self) return;
                    if (self->m_frame_fencepost_expected_thread.load(
                            std::memory_order_acquire) == 0)
                    {
                        self->m_frame_fencepost_expected_thread.store(
                            ::GetCurrentThreadId(), std::memory_order_release);
                    }
                    self->service_gameimgui_toggle_key_release();
                    self->service_gameimgui_deferred_install();
#if HORSE_ENABLE_GEKKONET
                    self->service_online_qualification();
#endif
#if HORSE_ENABLE_OBSERVER_PROBE
                    self->service_online_observer_probe();
#endif
                    self->draw_line_overlays_after_battle_tick();
                }, engine_tick_opts);
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] engine tick overlay service registered id={}\n"),
            m_engine_tick_callback_id);

        // Resolve SC6 native RVAs now that the game image is loaded.  KHit
        // rendering reads native world buffers directly; the remaining
        // pointers cover reset/start-position, online rules, presence
        // tracking, line-batcher refresh, and throw-height prediction.
        Horse::NativeBinding::resolve();
#if HORSE_ENABLE_GEKKONET
        const auto online_observer = m_sc6_online_session_observer.Initialize(
            Horse::NativeBinding::imageBase());
        if (!online_observer.ok())
        {
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] online session observer unavailable: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        online_observer.code))));
        }
#endif
#if HORSE_ENABLE_OBSERVER_PROBE
        const auto observer_probe = m_online_observer_access.Initialize(
            Horse::NativeBinding::imageBase());
        if (!observer_probe.ok())
        {
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] observer-only online accessor unavailable: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        observer_probe.code))));
        }
#endif
        m_replay_native_runtime_status = m_replay_native_runtime.Initialize(
            Horse::NativeBinding::imageBase(), &m_ucrt_rand_broker);
        if (!m_replay_native_runtime_status.ok())
        {
            const auto failure = Horse::Deterministic::failure_code_name(
                m_replay_native_runtime_status.code);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] native replay bridge unavailable: {}; "
                "deterministic simulation remains disabled\n"),
                RC::to_generic_string(std::string(failure)));
        }
        if (m_deterministic_config.trace || m_deterministic_config.enabled)
        {
            const auto ucrt_started = m_ucrt_rand_broker.Start();
            if (!ucrt_started.ok())
            {
                m_frame_fencepost_hook_status = ucrt_started;
                return;
            }
            m_frame_fencepost_expected_thread.store(
                0, std::memory_order_release);
            m_frame_fencepost_hook_status = m_deterministic_hooks.Install(
                Horse::NativeBinding::imageBase(),
                {this, &HorseMod::on_frame_fencepost,
                    &HorseMod::on_outer_tick_prepare,
                    &HorseMod::on_outer_tick_begin,
                    &HorseMod::on_outer_tick_source,
                    &HorseMod::on_outer_tick,
                    &HorseMod::on_replay_exit,
#if HORSE_ENABLE_GEKKONET
                    &HorseMod::on_authoritative_input,
                    &HorseMod::on_authoritative_input_commit
#else
                    nullptr,
                    nullptr
#endif
                },
                &m_ucrt_rand_broker);
            if (!m_frame_fencepost_hook_status.ok())
            {
                m_ucrt_rand_broker.Stop();
                const auto failure = Horse::Deterministic::failure_code_name(
                    m_frame_fencepost_hook_status.code);
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] frame-fencepost runtime proof unavailable: {}\n"),
                    RC::to_generic_string(std::string(failure)));
            }
            else
            {
                if (m_deterministic_config.forced_depth7_qualification
                    || m_deterministic_config.correction_probe)
                {
                    const auto presentation =
                        m_replay_native_runtime.EnablePresentationOwnership();
                    if (!presentation.ok())
                    {
                        m_frame_fencepost_hook_status = presentation;
                        m_deterministic_hooks.Uninstall();
                        m_ucrt_rand_broker.Stop();
                        return;
                    }
                }
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] deterministic lifecycle hooks armed; "
                    "stock simulation remains authoritative\n"));
#if HORSE_ENABLE_GEKKONET
                if (m_deterministic_config.enabled)
                {
                    reset_online_session_measurements("production");
                    m_online_coordinator.Select(OnlineRuntimeKind::Production);
                    const auto armed = m_online_lifecycle.ArmPreOwnership();
                    if (!armed.ok())
                    {
                        m_frame_fencepost_hook_status = armed;
                        return;
                    }
                    m_online_production_requested.store(true,
                        std::memory_order_release);
                    Output::send<LogLevel::Default>(STR(
                        "[HorseMod] production rollback observation armed; "
                        "ownership requires the complete immutable release "
                        "allowlist\n"));
                }
#endif
            }
        }

        // Install the C++-level chara-teleport hook.  This is the
        // workhorse for the "Override reset position" feature: every
        // engine reset path (round intro, training-mode reset bind,
        // RestartBattle, ResetBothCharaPositionsAndFacing, ...) funnels
        // through LuxBattleChara_SetStartPosition, so a single PolyHook
        // x64Detour there catches every trigger including the user's
        // raw-input training-reset bind that bypasses the BlueprintCallable
        // UFunction layer.  See horselib/SetStartPositionHook.hpp for
        // the full design rationale.
        Horse::SetStartPositionHook::instance().install();

        // Install the C++-level launcher-Start hook.  This is the
        // chokepoint for ALL 5 BattleRule overrides - the launcher's
        // Start method reads the data-table cache and applies the
        // per-match rules; we hook it to write our desired
        // BattleRule.<X> values into that cache right before the
        // original runs.  Caveat: this only fires on the HOST in
        // online lobbies (the joiner's match init bypasses the
        // launcher.Start path).  Sufficient for offline / training but
        // for online SlipOut we also install HasSubProviderEntryHook
        // below which IS host/joiner symmetric.
        Horse::LuxBattleLauncherStartHook::instance().install();

        // Install the C++-level "is slip-out suppressed?" runtime-gate
        // hook.  This is the deeper, host-joiner-symmetric override
        // for the SlipOut policy specifically - every client runs the
        // chara-init function that calls
        // LuxBattleChara_HasSubProviderEntryOfType0x3e, so PolyHooking
        // it gives both peers the same answer regardless of which side
        // initiated the match.  See the file-header doc for the full
        // rationale and the link to the previous failed-test
        // investigation.
        Horse::HasSubProviderEntryHook::instance().install();

        // Push the default hit-flash duration into the walker so it's
        // correct on frame 0 without the user having to touch the
        // slider.  Now in cockpit ticks (was ms / 60Hz before).
        Horse::KHitWalker::setStickyFrames(m_flash_frames.load());

        // Install the WH_KEYBOARD_LL hook and start the RawInput worker
        // thread eagerly so all our input polling works from the first
        // cockpit tick.  Lazy init would also work but would skip any
        // keys pressed before the first free-cam enable.  Both sources
        // run for the life of the process; no teardown needed here.
        (void)Horse::LowLevelKeyInput::instance();
        (void)Horse::RawInputSource::instance();
        Output::send<LogLevel::Verbose>(
            STR("[HorseMod] input sources: LL-hook={} RawInput={}\n"),
            Horse::LowLevelKeyInput::instance().hook_installed()
                ? STR("installed") : STR("FAILED"),
            Horse::RawInputSource::instance().ready()
                ? STR("ready") : STR("initialising..."));
    }
