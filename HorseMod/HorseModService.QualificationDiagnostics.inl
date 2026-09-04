    bool service_owned_seek_request() noexcept
    {
        const auto& timeline = m_replay_native_runtime.timeline_status_view();
        if (m_seek_request_active)
        {
            if (timeline.failure != Horse::Deterministic::FailureCode::None)
            {
                m_seek_request_active = false;
                m_seek_completed_target.store(0, std::memory_order_release);
                const auto hash_prefix = [](const Horse::Deterministic::CanonicalHash& hash)
                {
                    std::uint64_t value{};
                    std::memcpy(&value, hash.data(), sizeof(value));
                    return value;
                };
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] owned replay seek resume failed status={} "
                    "coordinate={} component_mask=0x{:x} wind_mask=0x{:x} "
                    "expected_hash_prefix=0x{:016x} "
                    "observed_hash_prefix=0x{:016x} verified_frames={}\n"),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(
                            timeline.failure))),
                    timeline.resume_failure_coordinate.frame,
                    timeline.resume_component_difference_mask,
                    timeline.resume_wind_difference_mask,
                    hash_prefix(timeline.resume_expected_hash),
                    hash_prefix(timeline.resume_observed_hash),
                    timeline.resumed_frames_verified);
                return true;
            }
            if (!timeline.resume_validation_active)
            {
                m_seek_request_active = false;
                m_seek_completed_source.store(
                    timeline.resume_source_end.frame, std::memory_order_relaxed);
                m_seek_completed_verified.store(
                    timeline.resumed_frames_verified, std::memory_order_relaxed);
                m_seek_completed_target.store(
                    timeline.resume_target.frame, std::memory_order_release);
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] owned replay seek resume verified "
                    "target={} source_end={} verified_frames={} final={}\n"),
                    timeline.resume_target.frame,
                    timeline.resume_source_end.frame,
                    timeline.resumed_frames_verified,
                    timeline.last_coordinate.frame);
            }
            return true;
        }

        const auto sequence = m_seek_request_sequence.load(
            std::memory_order_acquire);
        if (sequence == m_seek_handled_sequence) return false;
        if (sequence != m_seek_pending_sequence)
        {
            m_seek_pending_sequence = sequence;
            m_seek_defer_count = 0;
        }
        const auto requested_frame = m_seek_request_frame.load(
            std::memory_order_acquire);
        if (timeline.partial
            || timeline.failure != Horse::Deterministic::FailureCode::None
            || timeline.last_coordinate.generation == 0
            || requested_frame >= timeline.last_coordinate.frame)
        {
            m_seek_handled_sequence = sequence;
            const auto failure = Horse::Deterministic::FailureCode::IllegalTransition;
            m_frame_fencepost_failure.store(failure, std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] owned replay seek request rejected target={} "
                "current={} partial={} failure={}\n"),
                requested_frame, timeline.last_coordinate.frame,
                timeline.partial,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(timeline.failure))));
            return true;
        }

        const Horse::Deterministic::FrameCoordinate target{
            timeline.last_coordinate.generation, requested_frame};
        const auto status = m_replay_native_runtime.ExecuteOwnedStateSeek(
            target, m_deterministic_hooks);
        if (!status.ok())
        {
            if (status.code
                    == Horse::Deterministic::FailureCode::ContextUnavailable
                && m_seek_defer_count < 120)
            {
                ++m_seek_defer_count;
                if (m_seek_defer_count == 1)
                {
                    Output::send<LogLevel::Default>(STR(
                        "[HorseMod] owned replay seek deferred target={} "
                        "source_end={} transient_context=true\n"),
                        target.frame, timeline.last_coordinate.frame);
                }
                return true;
            }
            m_seek_handled_sequence = sequence;
            m_frame_fencepost_failure.store(status.code, std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] owned replay seek request failed target={} "
                "source_end={} status={} component_mask=0x{:x} "
                "native_mask=0x{:x} input_scalar_mask=0x{:x} "
                "input_cache_chunk={} game_time={}/{} update_time={}/{} "
                "recorder_time={}/{} cache_row={} "
                "cache_expected={}/{}/{}/{} cache_observed={}/{}/{}/{} "
                "move_dispatch={:016x}/{:016x}->{:016x}/{:016x} "
                "vfx_edges_p1={}/{}/{}/{}->{}/{}/{}/{} "
                "vfx_edges_p2={}/{}/{}/{}->{}/{}/{}/{} "
                "wind_mask=0x{:x} identity_issue={} identity={}/{} "
                "plan_stage={} plan_batch={} plan_base={}:{} "
                "plan_entry={}:{} plan_exit={}:{}\n"),
                target.frame, timeline.last_coordinate.frame,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                m_replay_native_runtime.timeline_status().
                    resume_component_difference_mask,
                m_replay_native_runtime.timeline_status().
                    resume_native_difference_mask,
                m_replay_native_runtime.timeline_status().
                    resume_input_scalar_difference_mask,
                m_replay_native_runtime.timeline_status().
                    resume_first_input_cache_chunk,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_scalars[5],
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_scalars[5],
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_scalars[7],
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_scalars[7],
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_scalars[8],
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_scalars[8],
                m_replay_native_runtime.timeline_status().
                    resume_first_input_cache_row,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.game_round,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.frame_index,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.input_value,
                m_replay_native_runtime.timeline_status().
                    resume_expected_input_cache_row.filled,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.game_round,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.frame_index,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.input_value,
                m_replay_native_runtime.timeline_status().
                    resume_observed_input_cache_row.filled,
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[0],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[1],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[0],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[1],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[2],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[3],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[4],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[5],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[2],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[3],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[4],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[5],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[6],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[7],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[8],
                m_replay_native_runtime.timeline_status().
                    resume_expected_move_dispatch[9],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[6],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[7],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[8],
                m_replay_native_runtime.timeline_status().
                    resume_observed_move_dispatch[9],
                m_replay_native_runtime.timeline_status().
                    resume_wind_difference_mask,
                m_replay_native_runtime.timeline_status().identity_issue,
                m_replay_native_runtime.timeline_status().identity_expected,
                m_replay_native_runtime.timeline_status().identity_observed,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_stage,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_batch_index,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_base.generation,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_base.frame,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_entry.generation,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_entry.frame,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_exit.generation,
                m_replay_native_runtime.timeline_status().
                    seek_plan_failure_exit.frame);
            return true;
        }
        m_seek_handled_sequence = sequence;
        m_seek_defer_count = 0;
        m_seek_request_active = target != timeline.last_coordinate;
        // The independent hook-health cursor observes the same rewritten
        // native counter. Rebase it atomically so the first resumed frame is
        // still required to be exactly target+1 rather than misreported as a
        // spontaneous generation reset.
        m_frame_fencepost_last_frame.store(
            static_cast<std::uint32_t>(target.frame),
            std::memory_order_release);
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] owned replay seek restored target={} source_end={} "
            "resume_validation={}\n"),
            target.frame, timeline.last_coordinate.frame,
            m_seek_request_active);
        return true;
    }

    void service_owned_correction_probe() noexcept
    {
        static constexpr std::array<std::uint64_t, 4> depths{1, 6, 11, 7};
        static constexpr std::array<std::uint64_t, 4> trigger_frames{
            180, 270, 330, 390};
        if (!m_deterministic_config.trace
            || !m_deterministic_config.correction_probe
            || m_owned_correction_probe_index >= depths.size())
        {
            return;
        }
        const auto timeline = m_replay_native_runtime.timeline_status();
        const auto index = m_owned_correction_probe_index;
        if (timeline.partial || timeline.failure
                != Horse::Deterministic::FailureCode::None
            || timeline.captured_frames < trigger_frames[index]
            || timeline.last_coordinate.frame + 1 < depths[index])
        {
            return;
        }

        Horse::Deterministic::Status status{};
        Horse::Deterministic::OwnedCorrectionResult result{};
        const Horse::Deterministic::FrameCoordinate earliest{
            timeline.last_coordinate.generation,
            timeline.last_coordinate.frame - depths[index] + 1};
        if (index + 1 == depths.size())
        {
            const auto input = m_replay_native_runtime.input_timeline().GetExact(
                earliest);
            if (!input.has_value())
            {
                status = Horse::Deterministic::Status::failure(
                    Horse::Deterministic::FailureCode::MissingInput);
            }
            else
            {
                auto corrected = input->players[1];
                corrected.held ^= 1u;
                corrected.rising ^= 1u;
                status = m_replay_native_runtime.ApplyConfirmedRemoteInput(
                    earliest, 1, corrected, m_deterministic_hooks, result);
            }
        }
        else
        {
            Horse::Deterministic::Snapshot expected{};
            status = m_replay_native_runtime.CaptureCurrentCanonical(expected);
            if (status.ok())
            {
                status = m_replay_native_runtime.ExecuteOwnedCorrection(
                    earliest, expected.canonical_hash,
                    m_deterministic_hooks, result);
            }
        }
        if (!status.ok())
        {
            m_owned_correction_probe_index = depths.size();
            m_frame_fencepost_failure.store(
                status.code, std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] owned correction probe failed depth={} status={} "
                "primary={} undo={} detail={} index={} base={} final={} "
                "batches={} coordinates={} total_us={} undo_restored={} "
                "restore_samples={}/{}/{}/{} diff_mask=0x{:x} "
                "input_diff={}@{}/{}@{} rng_mask=0x{:x} wind_mask=0x{:x}\n"),
                depths[index],
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.primary_failure))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.undo_failure))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::native_candidate_validation_issue_name(
                        result.primary_validation.issue))),
                result.primary_validation.index,
                result.resimulation_base.frame,
                result.final_coordinate.frame,
                result.replayed_batches,
                result.replayed_coordinates,
                result.total_ns / 1000,
                result.undo_restored,
                result.primary_performance.local_restore.samples,
                result.primary_performance.typed_restore.samples,
                result.primary_performance.wind_restore.samples,
                result.primary_performance.ucrt_restore.samples,
                result.undo_comparison_mask,
                result.input_scalar_difference_count,
                result.first_input_scalar_difference,
                result.input_cache_difference_count,
                result.first_input_cache_difference,
                result.rng_difference_mask,
                result.wind_difference_mask);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] correction interbatch diff batch={} mask=0x{:x} "
                "frame_mask=0x{:x} hgcpu={}@{} motion={}@{} "
                "final_hgcpu={}@{} final_motion={}@{} "
                "source=0x{:x}+{} stream={}/{}\n"),
                result.first_interbatch_difference_batch,
                result.first_interbatch_difference_mask,
                result.first_interbatch_frame_difference_mask,
                result.interbatch_local_difference_count,
                result.first_interbatch_local_difference,
                result.interbatch_motion_difference_count,
                result.first_interbatch_motion_difference,
                result.final_local_difference_count[0],
                result.first_final_local_difference[0],
                result.final_local_difference_count[1],
                result.first_final_local_difference[1],
                result.first_interbatch_local_source.source_address,
                result.first_interbatch_local_difference
                    - result.first_interbatch_local_source.stream_offset,
                result.first_interbatch_local_source.stream_offset,
                result.first_interbatch_local_source.size);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] correction local source fighters=0x{:x}/0x{:x} "
                "offsets={}/{} source_rva=0x{:x}\n"),
                result.diagnostic_fighter_roots[0],
                result.diagnostic_fighter_roots[1],
                static_cast<std::int64_t>(
                    result.first_interbatch_local_source.source_address
                    + (result.first_interbatch_local_difference
                        - result.first_interbatch_local_source.stream_offset)
                    - result.diagnostic_fighter_roots[0]),
                static_cast<std::int64_t>(
                    result.first_interbatch_local_source.source_address
                    + (result.first_interbatch_local_difference
                        - result.first_interbatch_local_source.stream_offset)
                    - result.diagnostic_fighter_roots[1]),
                result.first_interbatch_local_source.source_address
                    + (result.first_interbatch_local_difference
                        - result.first_interbatch_local_source.stream_offset)
                    - result.diagnostic_image_base);
            if (result.first_input_cache_difference != UINT32_MAX)
            {
                const auto& expected_row = result.expected_input_cache_row;
                const auto& observed_row = result.observed_input_cache_row;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction input/RNG diff row={} "
                    "expected={}/{}/0x{:x}/{} observed={}/{}/0x{:x}/{} "
                    "lcg=0x{:x}/0x{:x} lfsr_index={}/{} wind0=0x{:x}/0x{:x}\n"),
                    result.first_input_cache_difference,
                    expected_row.game_round, expected_row.frame_index,
                    expected_row.input_value, expected_row.filled,
                    observed_row.game_round, observed_row.frame_index,
                    observed_row.input_value, observed_row.filled,
                    result.expected_rng.lcg, result.observed_rng.lcg,
                    result.expected_rng.lfsr_index,
                    result.observed_rng.lfsr_index,
                    result.expected_rng.wind[0], result.observed_rng.wind[0]);
            }
            if (result.rng_difference_mask != 0
                || result.wind_difference_mask != 0)
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction RNG state "
                    "lcg=0x{:x}/0x{:x} lfsr_index={}/{} "
                    "xorshift=0x{:x},0x{:x},0x{:x}/0x{:x},0x{:x},0x{:x} "
                    "wind=0x{:x},0x{:x},0x{:x},0x{:x},0x{:x},0x{:x}/"
                    "0x{:x},0x{:x},0x{:x},0x{:x},0x{:x},0x{:x}\n"),
                    result.expected_rng.lcg, result.observed_rng.lcg,
                    result.expected_rng.lfsr_index,
                    result.observed_rng.lfsr_index,
                    result.expected_rng.xorshift[0],
                    result.expected_rng.xorshift[1],
                    result.expected_rng.xorshift[2],
                    result.observed_rng.xorshift[0],
                    result.observed_rng.xorshift[1],
                    result.observed_rng.xorshift[2],
                    result.expected_rng.wind[0], result.expected_rng.wind[1],
                    result.expected_rng.wind[2], result.expected_rng.wind[3],
                    result.expected_rng.wind[4], result.expected_rng.wind[5],
                    result.observed_rng.wind[0], result.observed_rng.wind[1],
                    result.observed_rng.wind[2], result.observed_rng.wind[3],
                    result.observed_rng.wind[4], result.observed_rng.wind[5]);
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction wind detail lfsr_word={} "
                    "nodes={}/{} first_node={} kind={}/{} semantic={} "
                    "byte=0x{:x}/0x{:x} derived={} output={}\n"),
                    result.first_lfsr_difference,
                    result.expected_wind_node_count,
                    result.observed_wind_node_count,
                    result.first_wind_node_difference,
                    result.expected_wind_node_kind,
                    result.observed_wind_node_kind,
                    result.first_wind_semantic_difference,
                    result.expected_wind_difference_byte,
                    result.observed_wind_difference_byte,
                    result.first_wind_derived_difference,
                    result.first_wind_output_difference);
                const auto& ie = result.first_interbatch_expected_wind;
                const auto& io = result.first_interbatch_observed_wind;
                const auto& fe = result.final_expected_wind;
                const auto& fo = result.final_observed_wind;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction wind schedule interbatch "
                    "kind={}/{} present={}/{} life=0x{:x}/0x{:x} "
                    "tick={}/{} prepared={}/{} active={}/{} "
                    "step=0x{:x}/0x{:x} repeat={}/{} "
                    "lfsr_index={}/{} final_life=0x{:x}/0x{:x} "
                    "final_tick={}/{} final_active={}/{}\n"),
                    ie.kind, io.kind, ie.present, io.present,
                    ie.life_bits, io.life_bits,
                    ie.oscillator_tick, io.oscillator_tick,
                    ie.prepared, io.prepared, ie.active, io.active,
                    ie.frame_step_bits, io.frame_step_bits,
                    ie.repeat_count, io.repeat_count,
                    result.first_interbatch_expected_rng.lfsr_index,
                    result.first_interbatch_observed_rng.lfsr_index,
                    fe.life_bits, fo.life_bits,
                    fe.oscillator_tick, fo.oscillator_tick,
                    fe.active, fo.active);
                const auto& bg = result.base_wind_graph;
                const auto& eg = result.first_interbatch_expected_wind_graph;
                const auto& og = result.first_interbatch_observed_wind_graph;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction wind graph "
                    "root(base={}/{}/{} expected={}/{}/{} observed={}/{}/{}) "
                    "nodes={}/{}/{} callback_hash=0x{:x}/0x{:x}/0x{:x} "
                    "life0-3=0x{:x},0x{:x},0x{:x},0x{:x}/"
                    "0x{:x},0x{:x},0x{:x},0x{:x}/"
                    "0x{:x},0x{:x},0x{:x},0x{:x} "
                    "tick0-3={},{},{},{}/{},{},{},{}/{},{},{},{}\n"),
                    bg.active_bank, bg.pending_count, bg.callback_count,
                    eg.active_bank, eg.pending_count, eg.callback_count,
                    og.active_bank, og.pending_count, og.callback_count,
                    bg.node_count, eg.node_count, og.node_count,
                    bg.callback_hash, eg.callback_hash, og.callback_hash,
                    bg.nodes[0].life_bits, bg.nodes[1].life_bits,
                    bg.nodes[2].life_bits, bg.nodes[3].life_bits,
                    eg.nodes[0].life_bits, eg.nodes[1].life_bits,
                    eg.nodes[2].life_bits, eg.nodes[3].life_bits,
                    og.nodes[0].life_bits, og.nodes[1].life_bits,
                    og.nodes[2].life_bits, og.nodes[3].life_bits,
                    bg.nodes[0].oscillator_tick, bg.nodes[1].oscillator_tick,
                    bg.nodes[2].oscillator_tick, bg.nodes[3].oscillator_tick,
                    eg.nodes[0].oscillator_tick, eg.nodes[1].oscillator_tick,
                    eg.nodes[2].oscillator_tick, eg.nodes[3].oscillator_tick,
                    og.nodes[0].oscillator_tick, og.nodes[1].oscillator_tick,
                    og.nodes[2].oscillator_tick, og.nodes[3].oscillator_tick);
            }
            if (result.failed_batch_index != SIZE_MAX)
            {
                const auto& envelope = result.failed_envelope;
                const auto& before = result.failed_batch_result.before;
                const auto& after = result.failed_batch_result.after;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] correction batch mismatch batch={} failure={} "
                    "observed_coordinates={} before(frame={}/{} input_round={}/{} "
                    "input_time={}/{} cursor_round={}/{} cursor_time={}/{} "
                    "main={}/{} round={}/{}) after(frame={}/{} input_round={}/{} "
                    "input_time={}/{} cursor_round={}/{} cursor_time={}/{} "
                    "main={}/{} round={}/{})\n"),
                    result.failed_batch_index,
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(
                            result.failed_batch_result.failure))),
                    result.failed_batch_result.observed_coordinates,
                    before.frame_counter, envelope.native_frame_before,
                    before.input_game_round, envelope.input_round_before,
                    before.input_game_time, envelope.input_time_before,
                    before.manager_game_round_cursor,
                    envelope.manager_round_cursor_before,
                    before.manager_game_time_cursor,
                    envelope.manager_time_cursor_before, before.main_state,
                    envelope.main_state_before, before.round_state,
                    envelope.round_state_before,
                    after.frame_counter, envelope.native_frame_after,
                    after.input_game_round, envelope.input_round_after,
                    after.input_game_time, envelope.input_time_after,
                    after.manager_game_round_cursor,
                    envelope.manager_round_cursor_after,
                    after.manager_game_time_cursor,
                    envelope.manager_time_cursor_after, after.main_state,
                    envelope.main_state_after, after.round_state,
                    envelope.round_state_after);
            }
            return;
        }
        ++m_owned_correction_probe_index;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] owned correction probe passed depth={} base={} "
            "final={} batches={} coordinates={} undo_capture_us={} "
            "restore_us={} resim_us={} verify_us={} total_us={} "
            "restore_phase_us(local={}/{} typed={}/{} wind={}/{} "
            "ucrt={}/{} total={}/{})\n"),
            depths[index], result.resimulation_base.frame,
            result.final_coordinate.frame, result.replayed_batches,
            result.replayed_coordinates, result.undo_capture_ns / 1000,
            result.restore_ns / 1000, result.resimulation_ns / 1000,
            result.verification_ns / 1000, result.total_ns / 1000,
            result.primary_performance.local_restore.p99_ns / 1000,
            result.primary_performance.local_restore.maximum_ns / 1000,
            result.primary_performance.typed_restore.p99_ns / 1000,
            result.primary_performance.typed_restore.maximum_ns / 1000,
            result.primary_performance.wind_restore.p99_ns / 1000,
            result.primary_performance.wind_restore.maximum_ns / 1000,
            result.primary_performance.ucrt_restore.p99_ns / 1000,
            result.primary_performance.ucrt_restore.maximum_ns / 1000,
            result.primary_performance.total_restore.p99_ns / 1000,
            result.primary_performance.total_restore.maximum_ns / 1000);
    }

    void service_grouped_qualification() noexcept
    {
        auto& q = m_forced_correction_qualification;
        if (!q.grouped || !q.runtime_armed || q.reported) return;
        const auto timeline = m_replay_native_runtime.timeline_status();
        const bool location_ready = [&]() noexcept {
            switch (q.location)
            {
            case 1: return timeline.round_state_frame > 16;
            case 2: return timeline.round_state_frame > 120;
            case 3:
                // ResetQualificationCycle clears all rollback-owned counters.
                // A resolved hit is instead an authored replay-session start
                // barrier, so a hit observed before a depth/location re-arm
                // remains sufficient until the authoritative replay exit.
                return m_replay_session_resolved_hit_observed
                    || timeline.observed_resolved_hit_calls != 0;
            case 4:
                if (timeline.round_state_frame <= 16
                    || timeline.unpause_countdown != 0) return false;
                if (!q.round_terminal_baseline_ready)
                {
                    q.round_terminal_audio_stop_all_baseline =
                        timeline.observed_battle_audio_stop_all_calls;
                    q.round_terminal_baseline_ready = true;
                    return false;
                }
                q.round_terminal_source_stop_all =
                    timeline.observed_battle_audio_stop_all_calls
                        > q.round_terminal_audio_stop_all_baseline;
                return q.round_terminal_source_stop_all;
            case 5:
                // Exact natural Tira helper-0x3250/0x3251 event boundary. The
                // baseline is captured when the request is armed, so an old
                // transition from replay setup cannot satisfy this location.
                return timeline.observed_tira_random_transition_calls
                    > q.tira_transition_baseline;
            case 6:
                // Same natural event trigger as location 5, but corrections
                // are subsequently metered at one depth per authored tick.
                return timeline.observed_tira_random_transition_calls
                    > q.tira_transition_baseline;
            default: return false;
            }
        }();
        if (timeline.partial
            || timeline.failure != Horse::Deterministic::FailureCode::None
            || timeline.round_state_frame <= 16
            || timeline.unpause_countdown != 0
            || timeline.last_coordinate.generation == 0
            || timeline.last_coordinate.frame
                < kGroupedQualificationDepths.front()
            || !location_ready) return;

        const auto fail = [&](Horse::Deterministic::Status status,
                              std::uint32_t depth,
                              std::uint32_t repetition) noexcept {
            q.failure = status.code;
            q.grouped_failure_depth = depth;
            q.grouped_failure_anchor = q.grouped_anchors_completed;
            q.grouped_failure_repeat = repetition;
            q.reported = true;
            q.lifecycle = 4;
            q.storage_end = m_replay_native_runtime.owned_storage_status();
            q.presentation_end =
                m_replay_native_runtime.presentation_statistics();
            q.capture_end = m_replay_native_runtime.capture_performance();
            q.pending_events_end =
                m_replay_native_runtime.pending_presentation_events();
            q.pending_payload_end =
                m_replay_native_runtime.presentation_payload_bytes();
            q.elapsed_ms = q.started_ms == 0 ? 0
                : ::GetTickCount64() - q.started_ms;
            m_frame_fencepost_failure.store(status.code,
                std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] grouped qualification failed location={} "
                "depth={} anchor={}/{} repetition={}/{} frame={}:{} "
                "status={} pending={}/{}\n"), q.location, depth,
                q.grouped_anchors_completed, q.grouped_anchor_target,
                repetition, q.grouped_repeats_per_anchor,
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                q.pending_events_end, q.pending_payload_end);
        };

        if (!q.active)
        {
            q.active = true;
            q.lifecycle = 2;
            q.started_ms = ::GetTickCount64();
            q.warmup_pending = true;
            q.first_generation = timeline.last_coordinate.generation;
            q.generation = timeline.last_coordinate.generation;
            q.first_frame = timeline.last_coordinate.frame;
            q.awaiting_generation_history = true;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] grouped qualification started location={} "
                "generation={} frame={} anchors={} repeats={} "
                "depths=11,1,6 normal_render=true\n"), q.location,
                q.generation, q.first_frame, q.grouped_anchor_target,
                q.grouped_repeats_per_anchor);
        }
        if (timeline.last_coordinate.generation != q.generation)
        {
            ++q.generation_transitions;
            q.generation = timeline.last_coordinate.generation;
            q.awaiting_generation_history = true;
            return;
        }

        for (const auto depth : kGroupedQualificationDepths)
        {
            const Horse::Deterministic::FrameCoordinate earliest{
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame - depth + 1};
            const auto preflight =
                m_replay_native_runtime.PreflightOwnedCorrection(earliest);
            if (!preflight.ok()) return;
        }
        q.awaiting_generation_history = false;

        // Repeated restores share an exact authoritative anchor, while
        // successive anchors retain the former 600-tick temporal exposure.
        // This keeps route/lifecycle coverage without repeating process or
        // replay entry setup for every depth.
        if (q.grouped_anchors_completed != 0
            && q.location != 6
            && q.grouped_last_anchor_generation
                == timeline.last_coordinate.generation
            && timeline.last_coordinate.frame
                < q.grouped_last_anchor_frame
                    + kGroupedQualificationAnchorSpacing)
            return;

        auto& expected = q.expected_scratch;
        auto status = m_replay_native_runtime.CaptureCurrentCanonical(expected);
        if (!status.ok())
        {
            fail(status, 0, 0);
            return;
        }
        const auto execute = [&](std::size_t depth_index,
                                 std::uint32_t repetition,
                                 bool measured) noexcept -> bool {
            const auto depth = kGroupedQualificationDepths[depth_index];
            const Horse::Deterministic::FrameCoordinate earliest{
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame - depth + 1};
            Horse::Deterministic::OwnedCorrectionResult result{};
            auto correction = m_replay_native_runtime.ExecuteOwnedCorrection(
                earliest, expected.canonical_hash, m_deterministic_hooks,
                result);
            const bool exact = correction.ok()
                && result.replayed_coordinates == depth
                && result.final_coordinate == timeline.last_coordinate
                && result.final_hash == expected.canonical_hash;
            if (!exact && correction.ok())
                correction = Horse::Deterministic::Status::failure(
                    Horse::Deterministic::FailureCode::IllegalTransition);
            if (!correction.ok())
            {
                fail(correction, depth, repetition);
                return false;
            }
            if (!measured) return true;
            q.RecordGrouped(depth_index, result.total_ns);
            q.suppressed_stage_wall_calls +=
                result.suppressed_stage_wall_calls;
            q.suppressed_stage_barrier_calls +=
                result.suppressed_stage_barrier_calls;
            q.semantic_stage_dispatch_calls +=
                result.semantic_stage_dispatch_calls;
            q.suppressed_audio_calls += result.suppressed_audio_calls;
            q.discarded_audio_calls += result.discarded_audio_calls;
            q.suppressed_audio_stop_all_calls +=
                result.suppressed_audio_stop_all_calls;
            q.suppressed_audio_terminal_calls +=
                result.suppressed_audio_terminal_calls;
            q.suppressed_audio_blueprint_calls +=
                result.suppressed_audio_blueprint_calls;
            q.suppressed_particle_spawn_calls +=
                result.suppressed_particle_spawn_calls;
            q.suppressed_particle_finished_binds +=
                result.suppressed_particle_finished_binds;
            q.unknown_particle_routes += result.unknown_particle_routes;
            q.verified_audio_batches += result.verified_audio_batches;
            q.verified_camera_batches += result.verified_camera_batches;
            q.camera_publication_mismatches +=
                result.camera_publication_mismatches;
            q.audio_sequence_mismatches +=
                result.audio_sequence_mismatches;
            q.presentation_failures += result.presentation_failures;
            return true;
        };

        if (q.warmup_pending)
        {
            for (std::size_t depth_index = 0;
                 depth_index < kGroupedQualificationDepths.size();
                 ++depth_index)
                if (!execute(depth_index, 0, false)) return;
            q.warmup_pending = false;
            q.first_frame = timeline.last_coordinate.frame;
            m_replay_native_runtime.ResetCapturePerformanceWindow();
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] grouped qualification warmup complete "
                "location={} frame={}:{}\n"), q.location,
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame);
            return;
        }

        const auto mix = [](std::uint64_t hash,
                            std::uint64_t value) noexcept {
            for (std::size_t byte = 0; byte < sizeof(value); ++byte)
            {
                hash ^= (value >> (byte * 8)) & 0xffu;
                hash *= 1099511628211ull;
            }
            return hash;
        };
        const auto mix_anchor = [&](std::size_t depth_index) noexcept {
            auto& hash = q.grouped_anchor_sequence_hash[depth_index];
            hash = mix(hash, timeline.last_coordinate.generation);
            hash = mix(hash, timeline.last_coordinate.frame);
            for (const auto value : expected.canonical_hash)
            {
                hash ^= std::to_integer<std::uint8_t>(value);
                hash *= 1099511628211ull;
            }
        };
        if (q.location == 6)
        {
            // Production cadence: exactly one owned correction per authored
            // outer tick, cycling 11 -> 1 -> 6. This measures real scheduling
            // pressure separately from location 5's same-anchor stress.
            const auto depth_index = static_cast<std::size_t>(
                q.grouped_cadence_depth_index);
            if (!execute(depth_index, q.grouped_anchors_completed, true))
                return;
            mix_anchor(depth_index);
            q.grouped_cadence_depth_index = static_cast<std::uint32_t>(
                (depth_index + 1) % kGroupedQualificationDepths.size());
            if (q.grouped_cadence_depth_index == 0)
                ++q.grouped_anchors_completed;
        }
        else
        {
            for (std::uint32_t repetition = 0;
                 repetition < q.grouped_repeats_per_anchor; ++repetition)
                for (std::size_t depth_index = 0;
                     depth_index < kGroupedQualificationDepths.size();
                     ++depth_index)
                    if (!execute(depth_index, repetition, true)) return;
            for (std::size_t depth_index = 0;
                 depth_index < kGroupedQualificationDepths.size();
                 ++depth_index)
                mix_anchor(depth_index);
            ++q.grouped_anchors_completed;
        }
        q.grouped_last_anchor_generation =
            timeline.last_coordinate.generation;
        q.grouped_last_anchor_frame = timeline.last_coordinate.frame;
        q.completed = q.grouped_completed.front();
        q.last_frame = timeline.last_coordinate.frame;
        if (q.grouped_anchors_completed < q.grouped_anchor_target) return;

        const auto presentation_commit =
            m_replay_native_runtime.CommitPresentationThrough(
                timeline.last_coordinate, m_deterministic_hooks);
        q.presentation_end =
            m_replay_native_runtime.presentation_statistics();
        q.capture_end = m_replay_native_runtime.capture_performance();
        q.storage_end = m_replay_native_runtime.owned_storage_status();
        q.pending_events_end =
            m_replay_native_runtime.pending_presentation_events();
        q.pending_payload_end =
            m_replay_native_runtime.presentation_payload_bytes();
        const bool presentation_journal_complete = presentation_commit.ok()
            && q.pending_events_end == 0 && q.pending_payload_end == 0
            && q.presentation_end.capacity_failures == 0;
        // The grouped correction window is terminal at this point. Do not
        // keep suppressing/capturing speculative presentation while the
        // harness measures the rest of its active-battle rate window. The
        // explicit cleanup request still resets and audits every other owned
        // subsystem, and observes this already-closed ownership boundary.
        m_replay_native_runtime.DisablePresentationOwnership();
        const bool full_qualification = q.grouped_anchor_target == 40
            && q.grouped_repeats_per_anchor == 15;
        q.presentation_terminal_coverage = presentation_journal_complete
            && (!full_qualification
                || (q.suppressed_audio_terminal_calls != 0
                    && q.suppressed_audio_blueprint_calls != 0
                    && q.verified_audio_batches != 0
                    && q.verified_camera_batches != 0
                    && (q.location != 4
                        || q.round_terminal_source_stop_all)));
        bool performance_ok = true;
        for (std::size_t index = 0;
             index < kGroupedQualificationDepths.size(); ++index)
            performance_ok = performance_ok
                && q.GroupedP99(index) < 16'670'000
                && q.grouped_maximum_ns[index] < 33'340'000;
        const bool capture_ok = q.capture_end.total_capture.p99_ns <= 500'000
            && q.capture_end.total_capture.maximum_ns <= 1'000'000
            && q.capture_end.scratch_capacity_growth_events == 0;
        if (!q.presentation_terminal_coverage)
            q.failure = Horse::Deterministic::FailureCode::PresentationFailed;
        else if (!performance_ok || !capture_ok)
            q.failure =
                Horse::Deterministic::FailureCode::PerformanceBudgetExceeded;
        q.elapsed_ms = ::GetTickCount64() - q.started_ms;
        if (m_forced_qualification_first_elapsed_ms == 0)
            m_forced_qualification_first_elapsed_ms = q.elapsed_ms;
        q.timing_drift_ms = q.elapsed_ms
                >= m_forced_qualification_first_elapsed_ms
            ? q.elapsed_ms - m_forced_qualification_first_elapsed_ms
            : m_forced_qualification_first_elapsed_ms - q.elapsed_ms;
        q.reported = true;
        q.lifecycle = q.failure == Horse::Deterministic::FailureCode::None
            ? 3u : 4u;
        if (q.failure != Horse::Deterministic::FailureCode::None)
            m_frame_fencepost_failure.store(q.failure,
                std::memory_order_release);
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] grouped qualification {} location={} anchors={}/{} "
            "repeats={} completed={}/{}/{} anchor_hash={:016x} "
            "p99_us={}/{}/{} max_us={}/{}/{} pending={}/{} "
            "capacity_growth={} terminal_coverage={} "
            "audio_terminals={} audio_blueprint={} audio_batches={} "
            "camera_batches={} round_terminal={} drift_ms={}\n"),
            q.failure == Horse::Deterministic::FailureCode::None
                ? STR("passed") : STR("failed"), q.location,
            q.grouped_anchors_completed, q.grouped_anchor_target,
            q.grouped_repeats_per_anchor, q.grouped_completed[0],
            q.grouped_completed[1], q.grouped_completed[2],
            q.grouped_anchor_sequence_hash[0], q.GroupedP99(0) / 1000,
            q.GroupedP99(1) / 1000, q.GroupedP99(2) / 1000,
            q.grouped_maximum_ns[0] / 1000,
            q.grouped_maximum_ns[1] / 1000,
            q.grouped_maximum_ns[2] / 1000, q.pending_events_end,
            q.pending_payload_end,
            q.capture_end.scratch_capacity_growth_events,
            q.presentation_terminal_coverage ? 1 : 0,
            q.suppressed_audio_terminal_calls,
            q.suppressed_audio_blueprint_calls, q.verified_audio_batches,
            q.verified_camera_batches,
            q.round_terminal_source_stop_all ? 1 : 0, q.timing_drift_ms);
    }

    void service_forced_depth7_qualification() noexcept
    {
        auto& qualification = m_forced_correction_qualification;
        if (qualification.grouped)
        {
            service_grouped_qualification();
            return;
        }
        if (!m_deterministic_config.trace
            || (!m_deterministic_config.forced_depth7_qualification
                && !qualification.runtime_armed)
            || qualification.reported)
        {
            return;
        }
        const auto timeline = m_replay_native_runtime.timeline_status();
        const std::uint64_t qualification_depth = qualification.runtime_armed
            ? qualification.depth : m_deterministic_config.qualification_depth;
        const std::uint32_t qualification_location = qualification.runtime_armed
            ? qualification.location
            : m_deterministic_config.qualification_location;
        const bool location_ready = [&]() noexcept {
            switch (qualification_location)
            {
            // Locations are qualification start barriers, not sampling
            // windows. Once the first stable post-unpause round frame is
            // reached, all 600 consecutive corrections must continue; an
            // upper bound silently capped ordinary matches below the required
            // workload and made this row structurally impossible to pass.
            case 1: return timeline.round_state_frame > 16;
            case 2: return timeline.round_state_frame > 120;
            // Native resolved-hit consumption is the canonical boundary.
            // Audio and particles are optional presentation and cannot define
            // a hit across authored characters/maps.
            case 3:
                return m_replay_session_resolved_hit_observed
                    || timeline.observed_resolved_hit_calls != 0;
            case 4:
                // These are session-lifetime observation counters.  Initial
                // battle setup can stop audio before the first stable replay
                // frame, so raw nonzero values are not a round-end barrier.
                // Establish a post-start baseline once and require a later
                // terminal-event delta authored by this replay.
                if (timeline.round_state_frame <= 16
                    || timeline.unpause_countdown != 0)
                {
                    return false;
                }
                if (!qualification.round_terminal_baseline_ready)
                {
                    qualification.round_terminal_audio_stop_all_baseline =
                        timeline.observed_battle_audio_stop_all_calls;
                    qualification.round_terminal_baseline_ready = true;
                    return false;
                }
                qualification.round_terminal_source_stop_all =
                    timeline.observed_battle_audio_stop_all_calls
                        > qualification.round_terminal_audio_stop_all_baseline;
                return qualification.round_terminal_source_stop_all;
            case 5:
                return timeline.observed_tira_random_transition_calls
                    > qualification.tira_transition_baseline;
            case 6:
                return timeline.observed_tira_random_transition_calls
                    > qualification.tira_transition_baseline;
            default: return false;
            }
        }();
        if (timeline.partial
            || timeline.failure != Horse::Deterministic::FailureCode::None
            || timeline.round_state_frame <= 16
            || timeline.unpause_countdown != 0
            || timeline.last_coordinate.generation == 0
            || timeline.last_coordinate.frame < qualification_depth
            || !location_ready)
        {
            return;
        }
        if (!qualification.active)
        {
            qualification.active = true;
            qualification.lifecycle = 2;
            qualification.started_ms = ::GetTickCount64();
            qualification.warmup_pending = true;
            qualification.first_generation = timeline.last_coordinate.generation;
            qualification.generation = timeline.last_coordinate.generation;
            qualification.first_frame = timeline.last_coordinate.frame;
            qualification.checkpoint_bytes_begin = timeline.checkpoint_bytes;
            qualification.batch_entry_bytes_begin =
                timeline.batch_entry_checkpoint_bytes;
            qualification.forced_history_bytes_begin =
                m_replay_native_runtime.forced_qualification_bytes();
            // A terminal event can publish the next native generation before
            // that generation has a restorable checkpoint.  Never attempt a
            // cross-generation restore; retain the source terminal latch and
            // wait for the first same-generation correction preflight.
            qualification.awaiting_generation_history = true;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] forced depth-7 qualification started "
                "generation={} frame={} warmup=1 target={} normal_render=true\n"),
                qualification.generation, qualification.first_frame,
                kForcedQualificationCorrections);
        }
        if (timeline.last_coordinate.generation != qualification.generation)
        {
            ++qualification.generation_transitions;
            qualification.generation = timeline.last_coordinate.generation;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] forced depth-7 qualification continuing "
                "after generation transition generation={} frame={} "
                "completed={}\n"),
                qualification.generation, timeline.last_coordinate.frame,
                qualification.completed);
            qualification.awaiting_generation_history = true;
            return;
        }
        const Horse::Deterministic::FrameCoordinate earliest{
            timeline.last_coordinate.generation,
            timeline.last_coordinate.frame - qualification_depth + 1};
        const auto correction_preflight =
            m_replay_native_runtime.PreflightOwnedCorrection(earliest);
        if (qualification.awaiting_generation_history
            && !correction_preflight.ok())
        {
            return;
        }
        qualification.awaiting_generation_history = false;
        Horse::Deterministic::OwnedCorrectionResult result{};
        auto status = qualification.failure
                == Horse::Deterministic::FailureCode::None
                && correction_preflight.ok()
            ? m_replay_native_runtime.CaptureCurrentCanonical(
                qualification.expected_scratch)
            : Horse::Deterministic::Status::failure(
                qualification.failure
                    != Horse::Deterministic::FailureCode::None
                ? qualification.failure : correction_preflight.code);
        if (status.ok())
        {
            status = m_replay_native_runtime.ExecuteOwnedCorrection(
                earliest, qualification.expected_scratch.canonical_hash,
                m_deterministic_hooks, result);
        }
        const bool exact_depth = status.ok()
            && result.replayed_coordinates == qualification_depth
            && result.final_coordinate.frame
                == result.resimulation_base.frame + qualification_depth;
        if (!exact_depth && status.ok())
            status = Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::IllegalTransition);
        if (!status.ok())
        {
            qualification.failure = status.code;
            qualification.reported = true;
            qualification.lifecycle = 4;
            qualification.storage_end =
                m_replay_native_runtime.owned_storage_status();
            qualification.presentation_end =
                m_replay_native_runtime.presentation_statistics();
            qualification.capture_end =
                m_replay_native_runtime.capture_performance();
            qualification.pending_events_end =
                m_replay_native_runtime.pending_presentation_events();
            qualification.pending_payload_end =
                m_replay_native_runtime.presentation_payload_bytes();
            qualification.elapsed_ms = qualification.started_ms == 0 ? 0
                : ::GetTickCount64() - qualification.started_ms;
            m_frame_fencepost_failure.store(status.code,
                std::memory_order_release);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] forced depth-7 qualification failed "
                "completed={} frame={} status={} primary={} undo={} "
                "restore_lane_mask=0x{:x} restore_operation_mask=0x{:x} "
                "restore_phase={} "
                "batch_validation_mask=0x{:x} batch_index={} "
                "batch_coordinates={}/{} "
                "coordinates={} base={} final={} total_us={} "
                "diff_mask=0x{:x} local_diff={} local_count={} "
                "motion_diff={} motion_count={} input_scalars={}@{}={}->{} "
                "camera_component={}:{} count={} byte={}->{} "
                "class=0x{:08x}/0x{:08x} "
                "input_cache={} rng_mask=0x{:x} wind_mask=0x{:x} "
                "move_dispatch={:016x}/{:016x}->{:016x}/{:016x} "
                "wind_node={} kind={}->{} semantic={} byte={}->{} "
                "interbatch_mask=0x{:x} interbatch_batch={} "
                "stage_wall={}/{} 0x{:016x}->0x{:016x} "
                "stage_barrier={}/{} 0x{:016x}->0x{:016x} "
                "stage_dispatch={}/{} 0x{:016x}->0x{:016x} "
                "stage_signature_failures={}/{} stage_journal_mask=0x{:x} "
                "camera=0x{:016x}->0x{:016x} "
                "camera_signature_failures={}/{} camera_mismatches={} "
                "camera_diff={}@{} byte={}->{} yaw=0x{:08x}->0x{:08x} "
                "mode=0x{:08x}->0x{:08x} "
                "camera_input=[{:08x},{:08x},{:08x},{:08x},{:08x},{:08x}]->"
                "[{:08x},{:08x},{:08x},{:08x},{:08x},{:08x}] "
                "audio_expected={} audio_observed={} "
                "audio_sequence=0x{:016x}->0x{:016x} "
                "audio_route=0x{:08x}->0x{:08x} "
                "audio_payload=0x{:08x}->0x{:08x} "
                "audio_position=0x{:08x}->0x{:08x} "
                "audio_remap={}/{} 0x{:016x}->0x{:016x} "
                "audio_source={}/{} 0x{:016x}->0x{:016x} "
                "audio_direct={}/{} 0x{:016x}->0x{:016x} "
                "audio_stop_all={}/{} 0x{:016x}->0x{:016x} "
                "audio_terminal={}/{} 0x{:016x}->0x{:016x} "
                "audio_blueprint={}/{} 0x{:016x}->0x{:016x} "
                "presentation_order={}/{} 0x{:016x}->0x{:016x} "
                "particle_spawn={}/{} 0x{:016x}->0x{:016x} "
                "particle_unknown={} journal_failure_mask=0x{:x} "
                "presentation_failure_mask=0x{:x} "
                "remap_entry=0x{:02x}:{}/{}->0x{:02x}:{}/{} "
                "selector_base={}/{}/{}/{} selector_undo={}/{}/{}/{} "
                "audio_sequence_mismatches={} presentation_failures={}\n"),
                qualification.completed, timeline.last_coordinate.frame,
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.primary_failure))),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(
                        result.undo_failure))),
                result.primary_restore_difference_mask,
                result.primary_restore_operation_failure_mask,
                static_cast<unsigned>(result.primary_restore_failure_phase),
                result.failed_batch_result.validation_difference_mask,
                result.failed_batch_index,
                result.failed_batch_result.observed_coordinates,
                result.failed_envelope.coordinate_count,
                result.replayed_coordinates, result.resimulation_base.frame,
                result.final_coordinate.frame, result.total_ns / 1000,
                result.undo_comparison_mask,
                result.first_final_local_difference[0],
                result.final_local_difference_count[0],
                result.first_final_local_difference[1],
                result.final_local_difference_count[1],
                result.input_scalar_difference_count,
                result.first_input_scalar_difference,
                result.expected_input_scalar_word,
                result.observed_input_scalar_word,
                result.first_camera_component_slot,
                result.first_camera_component_difference,
                result.camera_component_difference_count,
                result.expected_camera_component_byte,
                result.observed_camera_component_byte,
                result.camera_component_vtable_rva,
                result.camera_component_writer_rva,
                result.input_cache_difference_count,
                result.rng_difference_mask, result.wind_difference_mask,
                result.expected_move_dispatch[0],
                result.expected_move_dispatch[1],
                result.observed_move_dispatch[0],
                result.observed_move_dispatch[1],
                result.first_wind_node_difference,
                result.expected_wind_node_kind,
                result.observed_wind_node_kind,
                result.first_wind_semantic_difference,
                result.expected_wind_difference_byte,
                result.observed_wind_difference_byte,
                result.first_interbatch_difference_mask,
                result.first_interbatch_difference_batch,
                result.failed_envelope.stage_wall_calls,
                result.failed_batch_result.suppressed_stage_wall_calls,
                result.failed_envelope.stage_wall_hash,
                result.failed_batch_result.stage_wall_hash,
                result.failed_envelope.stage_barrier_calls,
                result.failed_batch_result.suppressed_stage_barrier_calls,
                result.failed_envelope.stage_barrier_hash,
                result.failed_batch_result.stage_barrier_hash,
                result.failed_envelope.stage_dispatch_calls,
                result.failed_batch_result.semantic_stage_dispatch_calls,
                result.failed_envelope.stage_dispatch_hash,
                result.failed_batch_result.stage_dispatch_hash,
                result.failed_envelope.stage_signature_failures,
                result.failed_batch_result.stage_signature_failures,
                result.failed_batch_result.stage_journal_failure_mask,
                result.failed_envelope.camera_publication_hash,
                result.failed_batch_result.camera_publication_hash,
                result.failed_envelope.camera_signature_failures,
                result.failed_batch_result.camera_signature_failures,
                result.failed_batch_result.camera_publication_mismatches,
                result.failed_batch_result.camera_publication_difference_count,
                result.failed_batch_result.first_camera_publication_difference,
                result.failed_batch_result.expected_camera_publication_byte,
                result.failed_batch_result.observed_camera_publication_byte,
                result.failed_envelope.camera_publication.yaw_bits,
                result.failed_batch_result.camera_publication.yaw_bits,
                result.failed_envelope.camera_publication.mode,
                result.failed_batch_result.camera_publication.mode,
                result.failed_envelope.camera_publication.input_words[0],
                result.failed_envelope.camera_publication.input_words[1],
                result.failed_envelope.camera_publication.input_words[2],
                result.failed_envelope.camera_publication.input_words[3],
                result.failed_envelope.camera_publication.input_words[4],
                result.failed_envelope.camera_publication.input_words[5],
                result.failed_batch_result.camera_publication.input_words[0],
                result.failed_batch_result.camera_publication.input_words[1],
                result.failed_batch_result.camera_publication.input_words[2],
                result.failed_batch_result.camera_publication.input_words[3],
                result.failed_batch_result.camera_publication.input_words[4],
                result.failed_batch_result.camera_publication.input_words[5],
                result.failed_envelope.battle_audio_dispatches,
                result.failed_batch_result.suppressed_audio_calls,
                result.failed_envelope.battle_audio_sequence_hash,
                result.failed_batch_result.suppressed_audio_sequence_hash,
                result.failed_envelope.battle_audio_route_hash,
                result.failed_batch_result.suppressed_audio_route_hash,
                result.failed_envelope.battle_audio_payload_hash,
                result.failed_batch_result.suppressed_audio_payload_hash,
                result.failed_envelope.battle_audio_position_hash,
                result.failed_batch_result.suppressed_audio_position_hash,
                result.failed_envelope.battle_audio_remap_calls,
                result.failed_batch_result.suppressed_audio_remap_calls,
                result.failed_envelope.battle_audio_remap_hash,
                result.failed_batch_result.suppressed_audio_remap_hash,
                result.failed_envelope.battle_audio_source_calls,
                result.failed_batch_result.suppressed_audio_source_calls,
                result.failed_envelope.battle_audio_source_hash,
                result.failed_batch_result.suppressed_audio_source_hash,
                result.failed_envelope.battle_audio_direct_dispatches,
                result.failed_batch_result.suppressed_audio_direct_dispatches,
                result.failed_envelope.battle_audio_direct_sequence_hash,
                result.failed_batch_result.suppressed_audio_direct_sequence_hash,
                result.failed_envelope.battle_audio_stop_all_calls,
                result.failed_batch_result.suppressed_audio_stop_all_calls,
                result.failed_envelope.battle_audio_stop_all_hash,
                result.failed_batch_result.suppressed_audio_stop_all_hash,
                result.failed_envelope.audio_terminal_calls,
                result.failed_batch_result.suppressed_audio_terminal_calls,
                result.failed_envelope.audio_terminal_hash,
                result.failed_batch_result.suppressed_audio_terminal_hash,
                result.failed_envelope.battle_audio_blueprint_calls,
                result.failed_batch_result.suppressed_audio_blueprint_calls,
                result.failed_envelope.battle_audio_blueprint_hash,
                result.failed_batch_result.suppressed_audio_blueprint_hash,
                result.failed_envelope.presentation_order_journal_count,
                result.failed_batch_result.suppressed_presentation_order_events,
                result.failed_envelope.presentation_order_hash,
                result.failed_batch_result.suppressed_presentation_order_hash,
                result.failed_envelope.particle_spawn_calls,
                result.failed_batch_result.suppressed_particle_spawn_calls,
                result.failed_envelope.particle_spawn_hash,
                result.failed_batch_result.suppressed_particle_spawn_hash,
                result.failed_batch_result.unknown_particle_routes,
                result.failed_batch_result.audio_journal_failure_mask,
                result.failed_batch_result.presentation_failure_mask,
                result.failed_envelope.battle_audio_remap_entry_mask,
                result.failed_envelope.battle_audio_remap_entry_values[0],
                result.failed_envelope.battle_audio_remap_entry_values[1],
                result.failed_batch_result.suppressed_audio_remap_entry_mask,
                result.failed_batch_result.suppressed_audio_remap_entry_values[0],
                result.failed_batch_result.suppressed_audio_remap_entry_values[1],
                result.base_audio_selector.observed_count,
                result.base_audio_selector.alternations[0],
                result.base_audio_selector.alternations[1],
                result.base_audio_selector.round_generation,
                result.undo_audio_selector.observed_count,
                result.undo_audio_selector.alternations[0],
                result.undo_audio_selector.alternations[1],
                result.undo_audio_selector.round_generation,
                result.failed_batch_result.audio_sequence_mismatches,
                result.failed_batch_result.presentation_failures);
            const auto& camera_source =
                result.failed_envelope.camera_source_frame;
            const auto read_camera_u32 = [&camera_source](std::size_t offset)
            {
                std::uint32_t value{};
                std::memcpy(&value,
                    camera_source.director_state.data() + offset,
                    sizeof(value));
                return value;
            };
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] failed camera synthesis selectors "
                "look_at_gate={} velocity_gate={} "
                "forward=0x{:08x} height=0x{:08x} side=0x{:08x} "
                "velocity_scale=0x{:08x}\n"),
                read_camera_u32(0x350), read_camera_u32(0x354),
                read_camera_u32(0x340), read_camera_u32(0x344),
                read_camera_u32(0x348), read_camera_u32(0x358));
            const auto camera_image_base = Horse::NativeBinding::imageBase();
            for (std::size_t slot = 0;
                 slot < camera_source.components.size(); ++slot)
            {
                const auto& component = camera_source.components[slot];
                if (component.present == 0) continue;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] failed camera component slot={} "
                    "vtable_rva=0x{:x} writer_rva=0x{:x} "
                    "serialization={} derived_size={}\n"),
                    slot, component.vtable_rva, component.writer_rva,
                    static_cast<std::uint32_t>(component.serialization),
                    component.derived_size);
            }
            const auto& action_backing = camera_source.action_backing;
            for (std::size_t slot = 0; slot < 17; ++slot)
            {
                constexpr std::size_t action_stride = 0x3E0;
                std::uintptr_t vtable{};
                std::int32_t countdown{};
                std::uint32_t active{};
                std::memcpy(&vtable,
                    action_backing.data() + slot * action_stride,
                    sizeof(vtable));
                std::memcpy(&countdown,
                    action_backing.data() + slot * action_stride + 0x2C,
                    sizeof(countdown));
                std::memcpy(&active,
                    action_backing.data() + slot * action_stride + 0x30,
                    sizeof(active));
                if (active == 0 && countdown == 0) continue;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] failed camera action slot={} "
                    "vtable_rva=0x{:x} countdown={} active={}\n"),
                    slot, vtable - camera_image_base, countdown, active);
            }
            if (result.first_final_local_source.size != 0)
            {
                const auto source =
                    result.first_final_local_source.source_address
                    + (result.first_final_local_difference[0]
                        - result.first_final_local_source.stream_offset);
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] forced qualification final HgCpu source "
                    "address=0x{:x} span_stream={}/{} source_offset={} "
                    "fighter_offsets={}/{} source_rva=0x{:x}\n"),
                    source,
                    result.first_final_local_source.stream_offset,
                    result.first_final_local_source.size,
                    result.first_final_local_difference[0]
                        - result.first_final_local_source.stream_offset,
                    static_cast<std::int64_t>(source
                        - result.diagnostic_fighter_roots[0]),
                    static_cast<std::int64_t>(source
                        - result.diagnostic_fighter_roots[1]),
                    source - result.diagnostic_image_base);
            }
            return;
        }
        if (qualification.warmup_pending)
        {
            qualification.warmup_pending = false;
            qualification.first_frame = timeline.last_coordinate.frame;
            qualification.checkpoint_bytes_begin = timeline.checkpoint_bytes;
            qualification.batch_entry_bytes_begin =
                timeline.batch_entry_checkpoint_bytes;
            qualification.forced_history_bytes_begin =
                m_replay_native_runtime.forced_qualification_bytes();
            m_replay_native_runtime.ResetCapturePerformanceWindow();
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] forced depth-7 qualification warmup complete "
                "generation={} frame={} target={}\n"),
                qualification.generation, timeline.last_coordinate.frame,
                kForcedQualificationCorrections);
            return;
        }
        qualification.Record(result.total_ns);
        qualification.suppressed_stage_wall_calls +=
            result.suppressed_stage_wall_calls;
        qualification.suppressed_stage_barrier_calls +=
            result.suppressed_stage_barrier_calls;
        qualification.semantic_stage_dispatch_calls +=
            result.semantic_stage_dispatch_calls;
        qualification.suppressed_audio_calls += result.suppressed_audio_calls;
        qualification.discarded_audio_calls += result.discarded_audio_calls;
        qualification.suppressed_audio_stop_all_calls +=
            result.suppressed_audio_stop_all_calls;
        qualification.suppressed_audio_terminal_calls +=
            result.suppressed_audio_terminal_calls;
        qualification.suppressed_audio_blueprint_calls +=
            result.suppressed_audio_blueprint_calls;
        qualification.suppressed_particle_spawn_calls +=
            result.suppressed_particle_spawn_calls;
        qualification.suppressed_particle_finished_binds +=
            result.suppressed_particle_finished_binds;
        qualification.unknown_particle_routes +=
            result.unknown_particle_routes;
        qualification.verified_audio_batches += result.verified_audio_batches;
        qualification.verified_camera_batches += result.verified_camera_batches;
        qualification.camera_publication_mismatches +=
            result.camera_publication_mismatches;
        qualification.audio_sequence_mismatches +=
            result.audio_sequence_mismatches;
        qualification.presentation_failures += result.presentation_failures;
        ++qualification.completed;
        qualification.last_frame = timeline.last_coordinate.frame;
        if (qualification.completed < kForcedQualificationCorrections) return;
        const auto final_timeline = m_replay_native_runtime.timeline_status();
        const auto presentation_commit =
            m_replay_native_runtime.CommitPresentationThrough(
                final_timeline.last_coordinate, m_deterministic_hooks);
        const auto presentation_statistics =
            m_replay_native_runtime.presentation_statistics();
        const bool presentation_journal_complete = presentation_commit.ok()
            && m_replay_native_runtime.pending_presentation_events() == 0
            && m_replay_native_runtime.presentation_payload_bytes() == 0
            && presentation_statistics.capacity_failures == 0;
        const bool presentation_terminal_coverage =
            presentation_journal_complete
            && qualification.suppressed_audio_terminal_calls != 0
            && qualification.suppressed_audio_blueprint_calls != 0
            && qualification.verified_audio_batches != 0
            && qualification.verified_camera_batches != 0
            // A resolved hit is certified by the native pending-hit consumer.
            // Particle activity is still identity-checked when authored, but
            // it is optional across characters, moves, and maps.
            // Round-end is a new-generation barrier.  Its source stop-all must
            // be observed after the post-start baseline, but it cannot be
            // replayed by restoring across generations.  Corrections begin at
            // the first safe same-generation checkpoint after the latched
            // terminal.  Stage events remain exact whenever authored.
            && (qualification_location != 4
                || qualification.round_terminal_source_stop_all);
        qualification.presentation_terminal_coverage =
            presentation_terminal_coverage;
        const auto capture_performance =
            m_replay_native_runtime.capture_performance();
        Horse::Deterministic::AudioTerminalEvent first_failed_audio{};
        Horse::Deterministic::AudioTerminalEvent last_failed_audio{};
        const bool first_failed_audio_decoded =
            Horse::Deterministic::DecodeAudioPresentation(
                presentation_statistics.first_failed_event,
                first_failed_audio).ok();
        const bool last_failed_audio_decoded =
            Horse::Deterministic::DecodeAudioPresentation(
                presentation_statistics.last_failed_event,
                last_failed_audio).ok();
        const auto p99 = qualification.P99();
        const bool performance_ok = p99 < 16'670'000;
        const bool capture_ok = capture_performance.total_capture.p99_ns
                <= 500'000
            && capture_performance.total_capture.maximum_ns <= 1'000'000
            && capture_performance.scratch_capacity_growth_events == 0
            && capture_performance.scratch_capacity_baseline_bytes
                == capture_performance.scratch_capacity_high_water_bytes;
        qualification.reported = true;
        if (!presentation_journal_complete || !presentation_terminal_coverage)
        {
            qualification.failure = presentation_commit.ok()
                ? Horse::Deterministic::FailureCode::PresentationFailed
                : presentation_commit.code;
            m_frame_fencepost_failure.store(qualification.failure,
                std::memory_order_release);
        }
        else if (!performance_ok || !capture_ok)
        {
            qualification.failure =
                Horse::Deterministic::FailureCode::PerformanceBudgetExceeded;
            m_frame_fencepost_failure.store(qualification.failure,
                std::memory_order_release);
        }
        qualification.lifecycle = qualification.failure
                == Horse::Deterministic::FailureCode::None
            ? 3u : 4u;
        qualification.storage_end =
            m_replay_native_runtime.owned_storage_status();
        qualification.presentation_end = presentation_statistics;
        qualification.capture_end = capture_performance;
        qualification.pending_events_end =
            m_replay_native_runtime.pending_presentation_events();
        qualification.pending_payload_end =
            m_replay_native_runtime.presentation_payload_bytes();
        qualification.elapsed_ms = qualification.started_ms == 0 ? 0
            : ::GetTickCount64() - qualification.started_ms;
        if (m_forced_qualification_first_elapsed_ms == 0)
            m_forced_qualification_first_elapsed_ms = qualification.elapsed_ms;
        qualification.timing_drift_ms = qualification.elapsed_ms
                >= m_forced_qualification_first_elapsed_ms
            ? qualification.elapsed_ms - m_forced_qualification_first_elapsed_ms
            : m_forced_qualification_first_elapsed_ms - qualification.elapsed_ms;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] forced correction qualification {} depth={} location={} completed={} "
            "generations={}-{} transitions={} frames={}-{} "
            "cycle_p99_us={} cycle_max_us={} "
            "capture_samples={} capture_p99_us={} capture_max_us={} "
            "scratch_capacity_bytes={}->{} scratch_growth_events={} "
            "scratch_owner_capture={}->{} scratch_owner_canonical={}->{} "
            "scratch_owner_target={}->{} scratch_owner_transaction={}->{} "
            "scratch_owner_regions={}->{} scratch_owner_motion={}->{} "
            "scratch_owner_dispatch={}->{} "
            "checkpoint_bytes={}->{} batch_entry_bytes={}->{} "
            "forced_history_bytes={}->{} "
            "stage_wall_suppressed={} stage_barrier_suppressed={} "
            "stage_semantic_dispatches={} stage_coverage={} "
            "round_terminal_source_stop_all={} "
            "audio_suppressed={} audio_discarded={} "
            "audio_stop_all_suppressed={} "
            "audio_terminals_suppressed={} "
            "audio_blueprint_suppressed={} "
            "particle_spawn_suppressed={} particle_bind_suppressed={} "
            "particle_unknown_routes={} "
            "audio_batches_verified={} audio_sequence_mismatches={} "
            "camera_batches_verified={} camera_publication_mismatches={} "
            "presentation_failures={} journal_attempted={} journal_recorded={} "
            "journal_discarded={} journal_committed={} journal_duplicates={} "
            "journal_capacity_failures={} journal_publish_failures={} "
            "journal_first_publish_failure={} journal_first_failed_event={}:{}:{}:{}:0x{:x} "
            "journal_first_failed_audio={}:{}:{}:{}:{}:{} "
            "journal_last_publish_failure={} journal_last_failed_event={}:{}:{}:{}:0x{:x} "
            "journal_last_failed_audio={}:{}:{}:{}:{}:{} "
            "journal_pending={} journal_payload_bytes={} "
            "canonical_convergence=exact presentation_terminal_coverage={}\n"),
            qualification.failure == Horse::Deterministic::FailureCode::None
                ? STR("passed") : STR("failed"),
            qualification_depth,
            qualification_location,
            qualification.completed, qualification.first_generation,
            qualification.generation, qualification.generation_transitions,
            qualification.first_frame, qualification.last_frame,
            p99 / 1000, qualification.maximum_ns / 1000,
            capture_performance.total_capture.samples,
            capture_performance.total_capture.p99_ns / 1000,
            capture_performance.total_capture.maximum_ns / 1000,
            capture_performance.scratch_capacity_baseline_bytes,
            capture_performance.scratch_capacity_high_water_bytes,
            capture_performance.scratch_capacity_growth_events,
            capture_performance.scratch_capacity_baseline_by_owner[0],
            capture_performance.scratch_capacity_high_water_by_owner[0],
            capture_performance.scratch_capacity_baseline_by_owner[1],
            capture_performance.scratch_capacity_high_water_by_owner[1],
            capture_performance.scratch_capacity_baseline_by_owner[2],
            capture_performance.scratch_capacity_high_water_by_owner[2],
            capture_performance.scratch_capacity_baseline_by_owner[3],
            capture_performance.scratch_capacity_high_water_by_owner[3],
            capture_performance.scratch_capacity_baseline_by_owner[4],
            capture_performance.scratch_capacity_high_water_by_owner[4],
            capture_performance.scratch_capacity_baseline_by_owner[5],
            capture_performance.scratch_capacity_high_water_by_owner[5],
            capture_performance.scratch_capacity_baseline_by_owner[6],
            capture_performance.scratch_capacity_high_water_by_owner[6],
            qualification.checkpoint_bytes_begin,
            final_timeline.checkpoint_bytes,
            qualification.batch_entry_bytes_begin,
            final_timeline.batch_entry_checkpoint_bytes,
            qualification.forced_history_bytes_begin,
            m_replay_native_runtime.forced_qualification_bytes(),
            qualification.suppressed_stage_wall_calls,
            qualification.suppressed_stage_barrier_calls,
            qualification.semantic_stage_dispatch_calls,
            qualification.suppressed_stage_wall_calls != 0
                    || qualification.suppressed_stage_barrier_calls != 0
                ? STR("observed") : STR("missing"),
            qualification.round_terminal_source_stop_all ? 1u : 0u,
            qualification.suppressed_audio_calls,
            qualification.discarded_audio_calls,
            qualification.suppressed_audio_stop_all_calls,
            qualification.suppressed_audio_terminal_calls,
            qualification.suppressed_audio_blueprint_calls,
            qualification.suppressed_particle_spawn_calls,
            qualification.suppressed_particle_finished_binds,
            qualification.unknown_particle_routes,
            qualification.verified_audio_batches,
            qualification.audio_sequence_mismatches,
            qualification.verified_camera_batches,
            qualification.camera_publication_mismatches,
            qualification.presentation_failures,
            presentation_statistics.attempted,
            presentation_statistics.recorded,
            presentation_statistics.discarded,
            presentation_statistics.committed,
            presentation_statistics.duplicates,
            presentation_statistics.capacity_failures,
            presentation_statistics.publish_failures,
            RC::to_generic_string(std::string(
                Horse::Deterministic::failure_code_name(
                    presentation_statistics.first_publish_failure))),
            presentation_statistics.first_failed_event.coordinate.generation,
            presentation_statistics.first_failed_event.coordinate.frame,
            presentation_statistics.first_failed_event.source_ordinal,
            presentation_statistics.first_failed_event.kind,
            presentation_statistics.first_failed_event.identity,
            first_failed_audio_decoded
                ? static_cast<unsigned int>(first_failed_audio.operation) : 0,
            first_failed_audio.logical_playback_id,
            first_failed_audio.cue_sheet_id,
            first_failed_audio.cue_id,
            first_failed_audio.value,
            first_failed_audio_decoded,
            RC::to_generic_string(std::string(
                Horse::Deterministic::failure_code_name(
                    presentation_statistics.last_publish_failure))),
            presentation_statistics.last_failed_event.coordinate.generation,
            presentation_statistics.last_failed_event.coordinate.frame,
            presentation_statistics.last_failed_event.source_ordinal,
            presentation_statistics.last_failed_event.kind,
            presentation_statistics.last_failed_event.identity,
            last_failed_audio_decoded
                ? static_cast<unsigned int>(last_failed_audio.operation) : 0,
            last_failed_audio.logical_playback_id,
            last_failed_audio.cue_sheet_id,
            last_failed_audio.cue_id,
            last_failed_audio.value,
            last_failed_audio_decoded,
            m_replay_native_runtime.pending_presentation_events(),
            m_replay_native_runtime.presentation_payload_bytes(),
            presentation_terminal_coverage
                ? STR("complete") : STR("incomplete"));
    }
