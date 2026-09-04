    bool begin_online_service(bool& qualification, bool& production, Horse::Deterministic::OnlineState& state) noexcept
    {
        using namespace Horse::Deterministic;
        if (m_online_preownership_failure_cleanup_delay != 0)
        {
            if (--m_online_preownership_failure_cleanup_delay == 0)
                reset_online_qualification_preownership();
            return false;
        }
        if (m_online_production_reentry_pending.load(
                std::memory_order_acquire))
        {
            m_online_coordinator.Select(OnlineRuntimeKind::Production);
            const auto armed = m_online_lifecycle.ArmPreOwnership();
            if (!armed.ok())
            {
                m_frame_fencepost_failure.store(
                    armed.code, std::memory_order_release);
                // Retain the pending edge.  Scene cleanup and lifecycle
                // completion are asynchronous; consuming it before a
                // successful arm permanently stranded production disabled.
                return false;
            }
            m_online_production_reentry_pending.store(
                false, std::memory_order_release);
            reset_online_session_measurements("production");
            m_online_production_requested.store(true,
                std::memory_order_release);
        }
        qualification = m_online_qualification_requested.load(
            std::memory_order_acquire);
        production = m_online_production_requested.load(
            std::memory_order_acquire);
        if (!qualification && !production)
            return false;
        state = m_online_coordinator.state();
        if (state == OnlineState::Disabled)
        {
            const auto enabled = m_online_coordinator.Enable();
            if (!enabled.ok()) { fail_online_qualification(enabled.code); return false; }
            state = m_online_coordinator.state();
        }
        return true;
    }

    bool service_online_lobby(bool qualification, Horse::Deterministic::OnlineState& state) noexcept
    {
        using namespace Horse::Deterministic;
        if (state == OnlineState::ObservingLobby)
        {
            // SC6 does not guarantee that its eligible player-session state
            // and the final LuxOnlineBattleSync payload remain observable on
            // the same game-thread tick. Capture only value identity after
            // Steam proves the exact two-member lobby, then revalidate that
            // lobby immediately before building the immutable peer contract.
            Sc6OnlineSessionIdentity observed_session{};
            auto status = m_sc6_online_session_observer.ObserveCurrent(
                observed_session);
            const auto observation_stage = static_cast<std::uint32_t>(
                observed_session.observation_stage);
            const auto stage_bit = observation_stage < 32
                ? std::uint32_t{1} << observation_stage : 0;
            const auto state_bit = observed_session.virtual_session_state < 32
                ? std::uint32_t{1}
                    << observed_session.virtual_session_state : 0;
            if (observation_stage != 0
                && (((m_online_session_observation_stage_mask & stage_bit) == 0)
                    || ((m_online_session_observation_state_mask
                        & state_bit) == 0)))
            {
                m_online_session_observation_stage_mask |= stage_bit;
                m_online_session_observation_state_mask |= state_bit;
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] online qualification run_id={} "
                    "session_observation stage={} status={} role={} "
                    "session_state={} lobby_id={}\n"),
                    RC::to_generic_string(m_online_run_id), observation_stage,
                    RC::to_generic_string(std::string(
                        failure_code_name(status.code))),
                    static_cast<int>(observed_session.role),
                    static_cast<unsigned>(
                        observed_session.virtual_session_state),
                    observed_session.lobby_id);
            }
            if (status.ok()
                && IsSc6PreownershipSessionState(
                    observed_session.virtual_session_state))
            {
                SteamLobbyIdentity observed_lobby{};
                const auto lobby_status = m_steam_lobby_observer.Observe(
                    observed_session.lobby_id, observed_lobby);
                if (lobby_status.ok())
                {
                    const bool changed = !m_online_latched_session_valid
                        || m_online_latched_session_identity.lobby_id
                            != observed_session.lobby_id
                        || m_online_latched_session_identity.local_player_slot
                            != observed_session.local_player_slot
                        || m_online_latched_session_identity
                            .virtual_session_state
                            != observed_session.virtual_session_state;
                    // Never extend any SC6 object lifetime across ticks.
                    m_online_latched_session_identity =
                        ValueOnlySc6OnlineSessionIdentity(observed_session);
                    m_online_latched_session_valid = true;
                    if (changed)
                    {
                        Output::send<LogLevel::Default>(STR(
                            "[HorseMod] online qualification run_id={} "
                            "session_identity_latched lobby_id={} "
                            "local_slot={} session_state={}\n"),
                            RC::to_generic_string(m_online_run_id),
                            observed_session.lobby_id,
                            observed_session.local_player_slot,
                            static_cast<unsigned>(
                                observed_session.virtual_session_state));
                    }
                }
                else if (!m_online_lobby_observation_failure_logged)
                {
                    m_online_lobby_observation_failure_logged = true;
                    Output::send<LogLevel::Default>(STR(
                        "[HorseMod] online qualification run_id={} "
                        "session_lobby_pending lobby_id={} status={} "
                        "mask=0x{:x} count={} local={} members={}/{} "
                        "discriminator={}\n"),
                        RC::to_generic_string(m_online_run_id),
                        observed_session.lobby_id,
                        RC::to_generic_string(std::string(
                            failure_code_name(lobby_status.code))),
                        observed_lobby.observation_mask,
                        observed_lobby.observed_member_count,
                        observed_lobby.local_steam_id,
                        observed_lobby.members[0],
                        observed_lobby.members[1],
                        observed_lobby.search_discriminator);
                }
            }
            if (!m_online_latched_session_valid) return false;
            const auto session = m_online_latched_session_identity;
            SteamLobbyIdentity lobby{};
            status = m_steam_lobby_observer.Observe(session.lobby_id, lobby);
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            OnlineContentContract content{};
            status = observe_online_content(content);
            if (!status.ok())
            {
                if (session.virtual_session_state == 3
                    || status.code == FailureCode::ContextUnavailable)
                    return false;
                fail_online_qualification(status.code);
                return false;
            }
            status = prepare_online_file_identities();
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            OnlinePeerContract contract{};
            status = BuildOnlinePeerContract(session, lobby, content,
                m_online_executable_identity, m_online_build_identity,
                m_deterministic_config.input_delay,
                m_deterministic_config.rollback_window, contract);
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            if (qualification)
            {
                m_online_qualification_allowlist.Arm(content);
                if (m_online_qualification_fault
                        == OnlineQualificationFault::
                            PreownershipContractMismatch)
                {
                    // Qualification-only asymmetric contract corruption.  The
                    // runner arms this on one peer so the real authenticated
                    // contract comparison rejects both sides before ownership.
                    contract.build_id[0] ^= std::byte{1};
                    m_online_qualification_fault_triggered = true;
                }
            }
            else
            {
                // Production observation cannot start transport or advance
                // the coordinator until the one-way evidence publication and
                // the exact immutable executable/DLL binding both match.
                if (!m_online_allowlist.IsQualified(content)) return false;
                const auto& binding = m_online_allowlist.binding();
                if (binding.executable_id != m_online_executable_identity
                    || binding.build_id != m_online_build_identity)
                    return false;
            }
            status = m_online_coordinator.ObserveLobby(contract);
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            log_online_event(1u << 0, "session_content_resolved");
            m_online_local_player_slot = session.local_player_slot;
            advance_online_qualification_status(2);
            const auto hash_hex = [](const CanonicalHash& value) {
                constexpr char digits[] = "0123456789abcdef";
                std::string text(value.size() * 2, '0');
                for (std::size_t index = 0; index < value.size(); ++index)
                {
                    const auto byte = std::to_integer<unsigned>(value[index]);
                    text[index * 2] = digits[byte >> 4];
                    text[index * 2 + 1] = digits[byte & 15];
                }
                return text;
            };
            const auto* qualified_stage = FindQualifiedStage(
                content.stage_code.data());
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} handshake map={} display_map={} "
                "fighters={}/{} local_slot={} loaded_map_sha256={} "
                "session_state={}\n"),
                RC::to_generic_string(m_online_run_id),
                RC::to_generic_string(std::string(content.map_name.data())),
                RC::to_generic_string(std::string(qualified_stage == nullptr
                    ? std::string_view{} : qualified_stage->display_name)),
                RC::to_generic_string(std::string(
                    content.fighter_codes[0].data())),
                RC::to_generic_string(std::string(
                    content.fighter_codes[1].data())),
                session.local_player_slot,
                RC::to_generic_string(hash_hex(m_online_loaded_map_identity)),
                static_cast<unsigned>(session.virtual_session_state));
            state = m_online_coordinator.state();
        }
        return true;
    }

    bool service_online_handshake_and_round(bool qualification, Horse::Deterministic::OnlineState& state) noexcept
    {
        using namespace Horse::Deterministic;
        if (state == OnlineState::Handshaking)
        {
            if (qualification && m_online_qualification_fault
                    == OnlineQualificationFault::PreownershipTimeout)
            {
                if (::GetTickCount64()
                        - m_online_qualification_fault_started_ms < 10'000)
                    return false;
                m_online_qualification_fault_triggered = true;
                fail_online_qualification(FailureCode::Timeout);
                return false;
            }
            const auto status = m_online_coordinator.Pump();
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            state = m_online_coordinator.state();
        }
        if (!m_online_authentication_logged
            && online_transport().Authenticated())
        {
            m_online_authentication_logged = true;
            log_online_event(1u << 1, "transport_authenticated");
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} steam_p2p_authenticated "
                "local_steamid64={} peer_steamid64={} "
                "session_key_established=1 transport=steam_legacy_p2p\n"),
                RC::to_generic_string(m_online_run_id),
                online_transport().LocalSteamId(),
                online_transport().PeerSteamId());
        }
        if (state == OnlineState::RoundBarrier)
        {
            const auto status = m_online_coordinator.Pump();
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            state = m_online_coordinator.state();
        }
        if (RequiresRoundTransitionRearm(
                state, m_online_round_transition_pending))
        {
            const auto& timeline =
                m_replay_native_runtime.timeline_status_view();
            if (!m_replay_native_runtime.AtCompletedOuterTickBoundary(
                    timeline.last_coordinate))
                return false;
            m_online_gekko.Stop();
            const auto prediction_cleared = m_replay_native_runtime
                .SetOnlinePredictedRemotePlayer(std::nullopt);
            const auto replacement_generation =
                PlanOwnedRoundReplacementGeneration(
                    m_online_round_completed_coordinate.generation,
                    timeline.last_coordinate.generation);
            auto rebaseline = prediction_cleared.ok()
                    && replacement_generation.has_value()
                ? m_replay_native_runtime.CommitDeferredOnlineRebaseline(
                    true, *replacement_generation)
                : Status::failure(prediction_cleared.ok()
                    ? FailureCode::GenerationMismatch
                    : prediction_cleared.code);
            m_replay_native_runtime.DisablePresentationOwnership();
            if (!rebaseline.ok()
                || m_replay_native_runtime.presentation_ownership_enabled())
            {
                fail_online_qualification(rebaseline.ok()
                    ? FailureCode::RestoreVerificationFailed
                    : rebaseline.code);
                return false;
            }
            m_online_baseline_coordinate = {};
            m_online_prefix_next_frame = 0;
            m_online_next_gekko_frame = 0;
            m_online_next_confirmed_hash_frame = 29;
            m_online_current_advance_pending = false;
            m_online_pending_coordinate = {};
            m_online_owned_storage_prepared = false;
            m_online_round_transition_pending = false;
            // Session/authentication remain one-time facts.  Baseline,
            // catch-up, and first-input ownership must be evidenced for
            // every newly owned native generation.
            m_online_event_mask &= (1u << 0) | (1u << 1);
            m_online_prefix_catchup = true;
            // CommitDeferredOnlineRebaseline preserves the replacement
            // generation/coordinate while retiring all old correction state.
            // The next native frame therefore remains in the generation both
            // peers acknowledged instead of creating a synthetic extra one.
            m_online_last_observed_coordinate = timeline.last_coordinate;
            advance_online_qualification_status(3);
            ++m_online_rounds;
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "event=round_barrier_completed generation={} frame={}\n"),
                RC::to_generic_string(m_online_run_id),
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "round_barrier completed_generation={} next_generation={} "
                "rounds={} corrections={}\n"),
                RC::to_generic_string(m_online_run_id),
                m_online_round_completed_coordinate.generation,
                timeline.last_coordinate.generation, m_online_rounds,
                m_online_corrections);
            m_online_round_completed_coordinate = {};
        }
        return true;
    }

    bool service_online_baseline(Horse::Deterministic::OnlineState& state) noexcept
    {
        using namespace Horse::Deterministic;
        if (state == OnlineState::AwaitingBattle
            && !m_online_gekko.started())
        {
            const auto contract = m_online_coordinator.active_contract();
            if (!contract.has_value())
            { fail_online_qualification(FailureCode::IdentityMismatch); return false; }
            const auto status = m_online_gekko.Start(m_online_coordinator,
                *this, m_online_local_player_slot,
                static_cast<std::uint8_t>(contract->input_delay),
                static_cast<std::uint8_t>(contract->rollback_window),
                sizeof(OnlineStateToken));
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
        }
        state = m_online_coordinator.state();
        if (state == OnlineState::AwaitingBattle
            || state == OnlineState::AwaitingBaselineTarget
            || state == OnlineState::FreezingBaseline)
        {
            const auto polled = m_online_gekko.PollNetwork();
            if (!polled.ok())
            {
                const auto context = m_online_coordinator.failure_context();
                const auto local = m_online_coordinator.local_baseline_ready();
                const auto remote = m_online_coordinator.remote_baseline_ready();
                const auto target = m_online_coordinator.baseline_target();
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] online qualification run_id={} "
                    "coordinator_failure status={} inbound={} kind={} "
                    "state_before={} payload0={} payload1={} "
                    "local={}:{} remote={}:{} target={}:{}\n"),
                    RC::to_generic_string(m_online_run_id),
                    RC::to_generic_string(std::string(
                        failure_code_name(polled.code))),
                    context.has_inbound_message ? 1 : 0,
                    static_cast<unsigned>(context.message_kind),
                    static_cast<unsigned>(context.state_before_message),
                    context.payload_word0, context.payload_word1,
                    local ? local->generation : 0, local ? local->frame : 0,
                    remote ? remote->generation : 0, remote ? remote->frame : 0,
                    target ? target->generation : 0, target ? target->frame : 0);
                fail_online_qualification(polled.code);
                return false;
            }
            state = m_online_coordinator.state();
        }
        if (state == OnlineState::AwaitingBattle
            && m_online_gekko.ReadyForBaseline()
            && !m_online_coordinator.local_baseline_ready().has_value())
        {
            const auto& timeline = m_replay_native_runtime.timeline_status_view();
            if (timeline.last_coordinate.generation == 0) return false;
            CanonicalHashEntry ready_canonical{};
            const auto ready_diagnostic =
                m_replay_native_runtime.GetCanonicalEntry(
                    timeline.last_coordinate, ready_canonical);
            if (!ready_diagnostic.ok())
            {
                fail_online_qualification(ready_diagnostic.code);
                return false;
            }
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_ready_canonical generation={} frame={} "
                "components={:016x}/{:016x}/{:016x}/{:016x}/{:016x} "
                "input_scalars={:016x} input_cache0={:016x} "
                "animation_scheduler_p1={:016x} stage_emitter0={:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                timeline.last_coordinate.generation,
                timeline.last_coordinate.frame,
                ready_canonical.components[0], ready_canonical.components[1],
                ready_canonical.components[2], ready_canonical.components[3],
                ready_canonical.components[4], ready_canonical.native[9],
                ready_canonical.native[10], ready_canonical.animation[4],
                ready_canonical.stage_emitters[1]);
            FrameCoordinate proposal = timeline.last_coordinate;
            if (m_online_coordinator.owns_simulation())
            {
                const auto planned = PlanOnlineRoundBaselineProposal(
                    timeline.last_coordinate);
                if (!planned.has_value())
                {
                    fail_online_qualification(FailureCode::CapacityExceeded);
                    return false;
                }
                proposal = *planned;
                const auto reserved = m_replay_native_runtime
                    .RequireOnlineBaselineCheckpoint(proposal);
                if (!reserved.ok())
                {
                    fail_online_qualification(reserved.code);
                    return false;
                }
            }
            const auto ready = m_online_coordinator.ReadyBaseline(proposal);
            if (!ready.ok()) { fail_online_qualification(ready.code); return false; }
            log_online_event(1u << 2, "local_baseline_ready", proposal);
            state = m_online_coordinator.state();
        }
        if (state == OnlineState::AwaitingBaselineTarget)
        {
            const auto& timeline = m_replay_native_runtime.timeline_status_view();
            const auto target = m_online_coordinator.baseline_target();
            if (!target.has_value())
            { fail_online_qualification(FailureCode::IdentityMismatch); return false; }
            const auto retained =
                m_replay_native_runtime.RequireOnlineBaselineCheckpoint(*target);
            if (!retained.ok())
            { fail_online_qualification(retained.code); return false; }
            log_online_event(1u << 3, "bilateral_baseline_target", *target);
            const auto progress = m_online_coordinator.ObserveBaselineProgress(
                timeline.last_coordinate);
            if (!progress.ok())
            { fail_online_qualification(progress.code); return false; }
            if (timeline.last_coordinate < *target) return false;

            OnlineContentContract loaded{};
            const auto map = observe_online_content(loaded);
            const auto contract = m_online_coordinator.active_contract();
            if (!map.ok() || !contract.has_value() || loaded != contract->content)
            { fail_online_qualification(map.ok()
                ? FailureCode::IdentityMismatch : map.code); return false; }

            CanonicalHashEntry baseline{};
            const auto found = m_replay_native_runtime.GetCanonicalEntry(
                *target, baseline);
            if (!found.ok())
            { fail_online_qualification(found.code); return false; }
            if (!CanFreezeOnlineBaseline(*target, timeline.last_coordinate,
                    true, m_replay_native_runtime
                        .HasOnlineBaselineCheckpoint(*target)))
            {
                // The exact batch-entry image is captured at the beginning of
                // the next native batch. Keep stock authoritative and wait;
                // status 4 must never begin with canonical-only state.
                return false;
            }
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_identity generation={} frame={} "
                "components={:016x}/{:016x}/{:016x}/{:016x}/{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                target->generation, target->frame,
                baseline.components[0], baseline.components[1],
                baseline.components[2], baseline.components[3],
                baseline.components[4]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_native_0_7={:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.native[0], baseline.native[1], baseline.native[2],
                baseline.native[3], baseline.native[4], baseline.native[5],
                baseline.native[6], baseline.native[7]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_native_8_15={:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.native[8], baseline.native[9], baseline.native[10],
                baseline.native[11], baseline.native[12], baseline.native[13],
                baseline.native[14], baseline.native[15]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_input_scalars delay={} local_mask={} player_num={} "
                "local_flags={} game_round={} game_time={} pause_time={} "
                "update_time={} recorder_time={} recorder_stop={} "
                "current0={} current1={}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.input.scalars[0], baseline.input.scalars[1],
                baseline.input.scalars[2], baseline.input.scalars[3],
                baseline.input.scalars[4], baseline.input.scalars[5],
                baseline.input.scalars[6], baseline.input.scalars[7],
                baseline.input.scalars[8], baseline.input.scalars[9],
                baseline.input.scalars[10], baseline.input.scalars[11]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_native_16_23={:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.native[16], baseline.native[17], baseline.native[18],
                baseline.native[19], baseline.native[20], baseline.native[21],
                baseline.native[22], baseline.native[23]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_native_24_31={:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.native[24], baseline.native[25], baseline.native[26],
                baseline.native[27], baseline.native[28], baseline.native[29],
                baseline.native[30], baseline.native[31]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_move={:016x}/{:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}/{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.move_dispatch[0], baseline.move_dispatch[1],
                baseline.move_dispatch[2], baseline.move_dispatch[3],
                baseline.move_dispatch[4], baseline.move_dispatch[5],
                baseline.move_dispatch[6], baseline.move_dispatch[7],
                baseline.move_dispatch[8], baseline.move_dispatch[9]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_animation={:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.animation[0], baseline.animation[1],
                baseline.animation[2], baseline.animation[3],
                baseline.animation[4], baseline.animation[5],
                baseline.animation[6], baseline.animation[7],
                baseline.animation[8], baseline.animation[9],
                baseline.animation[10], baseline.animation[11]);
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} "
                "baseline_stage_emitters={:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}/{:016x}/{:016x}/{:016x}/{:016x}/{:016x}/"
                "{:016x}\n"),
                RC::to_generic_string(m_online_run_id),
                baseline.stage_emitters[0], baseline.stage_emitters[1],
                baseline.stage_emitters[2], baseline.stage_emitters[3],
                baseline.stage_emitters[4], baseline.stage_emitters[5],
                baseline.stage_emitters[6], baseline.stage_emitters[7],
                baseline.stage_emitters[8], baseline.stage_emitters[9],
                baseline.stage_emitters[10], baseline.stage_emitters[11],
                baseline.stage_emitters[12], baseline.stage_emitters[13],
                baseline.stage_emitters[14], baseline.stage_emitters[15],
                baseline.stage_emitters[16]);
            PeerBaselineStateDiagnostic state_diagnostic{};
            const auto diagnostic_status =
                m_replay_native_runtime.GetPeerBaselineStateDiagnostic(
                    *target, state_diagnostic);
            if (diagnostic_status.ok())
            {
                for (std::size_t player = 0; player < 2; ++player)
                {
                    const auto& clip_words =
                        state_diagnostic.animation_clip_words[player];
                    for (std::size_t base = 0;
                         base < clip_words.size(); base += 4)
                    {
                        Output::send<LogLevel::Default>(STR(
                            "[HorseMod] online qualification run_id={} "
                            "baseline_animation_clip_words player={} "
                            "base={} values={:08x}/{:08x}/{:08x}/{:08x}\n"),
                            RC::to_generic_string(m_online_run_id), player,
                            base, clip_words[base],
                            base + 1 < clip_words.size()
                                ? clip_words[base + 1] : 0,
                            base + 2 < clip_words.size()
                                ? clip_words[base + 2] : 0,
                            base + 3 < clip_words.size()
                                ? clip_words[base + 3] : 0);
                    }
                    const auto& words =
                        state_diagnostic.animation_scheduler_words[player];
                    for (std::size_t base = 0; base < words.size(); base += 4)
                    {
                        Output::send<LogLevel::Default>(STR(
                            "[HorseMod] online qualification run_id={} "
                            "baseline_animation_scheduler_words player={} "
                            "base={} values={:08x}/{:08x}/{:08x}/{:08x}\n"),
                            RC::to_generic_string(m_online_run_id), player,
                            base, words[base],
                            base + 1 < words.size() ? words[base + 1] : 0,
                            base + 2 < words.size() ? words[base + 2] : 0,
                            base + 3 < words.size() ? words[base + 3] : 0);
                    }
                }
                const auto& words =
                    state_diagnostic.first_stage_emitter_words;
                for (std::size_t base = 0;
                     state_diagnostic.stage_emitter_count != 0
                        && base < words.size(); base += 4)
                {
                    Output::send<LogLevel::Default>(STR(
                        "[HorseMod] online qualification run_id={} "
                        "baseline_stage_emitter_words emitter=0 base={} "
                        "values={:08x}/{:08x}/{:08x}/{:08x}\n"),
                        RC::to_generic_string(m_online_run_id), base,
                        words[base],
                        base + 1 < words.size() ? words[base + 1] : 0,
                        base + 2 < words.size() ? words[base + 2] : 0,
                        base + 3 < words.size() ? words[base + 3] : 0);
                }
            }
            else
            {
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] online qualification run_id={} "
                    "baseline_state_diagnostic_unavailable status={} "
                    "generation={} frame={}\n"),
                    RC::to_generic_string(m_online_run_id),
                    RC::to_generic_string(std::string(
                        failure_code_name(diagnostic_status.code))),
                    target->generation, target->frame);
            }

            m_online_baseline_coordinate = *target;
            m_online_prefix_next_frame = 0;
            m_online_prefix_catchup = true;
            const auto frozen = m_online_coordinator.FreezeBaseline(
                *target, baseline.hash, m_online_loaded_map_identity);
            if (!frozen.ok())
            { fail_online_qualification(frozen.code); return false; }
            log_online_event(1u << 4, "local_baseline_frozen", *target);
            advance_online_qualification_status(3);
            state = m_online_coordinator.state();
        }
        if (state == OnlineState::FreezingBaseline)
        {
            const auto polled = m_online_gekko.PollNetwork();
            if (!polled.ok()) { fail_online_qualification(polled.code); return false; }
            state = m_online_coordinator.state();
        }
        return true;
    }

    bool service_online_prefix(Horse::Deterministic::OnlineState state) noexcept
    {
        using namespace Horse::Deterministic;
        if (state == OnlineState::Active
            && (!m_online_takeover_ready || m_online_prefix_catchup))
        {
            log_online_event(1u << 5, "bilateral_baseline_acknowledged",
                m_online_baseline_coordinate);
            log_online_event(1u << 6, "prefix_catchup_started",
                m_online_baseline_coordinate);
            m_online_prefix_catchup = true;
            if (!m_online_owned_storage_prepared)
            {
                const auto prepared =
                    m_replay_native_runtime.PrepareOnlineOwnedStorage(
                        m_online_baseline_coordinate);
                if (!prepared.ok())
                { fail_online_qualification(prepared.code); return false; }
                m_online_owned_storage_prepared = true;
            }
            advance_online_qualification_status(4);
            const auto status4_storage =
                m_replay_native_runtime.owned_storage_status();
            m_online_qualification_metrics.BeginStatus4(
                status4_storage.aggregate_bytes);
            const auto status4_capture =
                m_replay_native_runtime.capture_performance();
            Output::send<LogLevel::Default>(STR(
                "[HorseMod] online qualification run_id={} status4_storage "
                "aggregate={} timeline={} forced={} presentation={} scratch={} "
                "runtime={} checkpoint_capture={} correction={} diagnostics={} "
                "corrected_replay={} checkpoint_parts={}/{}/{}/{}/{} "
                "adapter_owners={}/{}/{}/{}/{}/{}/{}\n"),
                RC::to_generic_string(m_online_run_id),
                status4_storage.aggregate_bytes,
                status4_storage.timeline_bytes,
                status4_storage.forced_snapshot_bytes,
                status4_storage.presentation_bytes,
                status4_storage.scratch_metadata_bytes,
                status4_storage.runtime_scratch_bytes,
                status4_storage.checkpoint_capture_scratch_bytes,
                status4_storage.correction_scratch_bytes,
                status4_storage.diagnostic_image_bytes,
                status4_storage.corrected_replay_scratch_bytes,
                status4_storage.checkpoint_fixed_subsystems_bytes,
                status4_storage.checkpoint_adapter_bytes,
                status4_storage.checkpoint_capture_snapshot_bytes,
                status4_storage.checkpoint_callback_topology_bytes,
                status4_storage.checkpoint_auxiliary_decode_bytes,
                status4_capture.scratch_capacity_high_water_by_owner[0],
                status4_capture.scratch_capacity_high_water_by_owner[1],
                status4_capture.scratch_capacity_high_water_by_owner[2],
                status4_capture.scratch_capacity_high_water_by_owner[3],
                status4_capture.scratch_capacity_high_water_by_owner[4],
                status4_capture.scratch_capacity_high_water_by_owner[5],
                status4_capture.scratch_capacity_high_water_by_owner[6]);
            const auto& timeline = m_replay_native_runtime.timeline_status_view();
            if (timeline.last_coordinate.generation
                != m_online_baseline_coordinate.generation)
            { fail_online_qualification(FailureCode::GenerationMismatch); return false; }
            for (std::size_t budget = 0; budget < 8; ++budget)
            {
                const FrameCoordinate coordinate{
                    m_online_baseline_coordinate.generation,
                    m_online_baseline_coordinate.frame
                        + m_online_prefix_next_frame + 1};
                if (coordinate > timeline.last_coordinate) break;
                const auto input = m_replay_native_runtime.input_timeline()
                    .GetExact(coordinate);
                if (!input.has_value()) break;
                std::array<PlayerInput, 2> selected{};
                auto status = m_online_gekko.Advance(
                    input->players[m_online_local_player_slot], selected);
                if (status.ok())
                    status = m_online_gekko.CompleteDeferredSaves();
                if (!status.ok())
                { fail_online_qualification(status.code); return false; }
                ++m_online_prefix_next_frame;
            }
            auto status = m_online_gekko.PollNetwork();
            if (status.ok()) status = m_online_gekko.FlushCorrections();
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            const FrameCoordinate next{
                m_online_baseline_coordinate.generation,
                m_online_baseline_coordinate.frame
                    + m_online_prefix_next_frame + 1};
            if (CanCompleteOnlinePrefixCatchup(
                    m_online_prefix_next_frame,
                    m_online_gekko.confirmed_frame(), next,
                    timeline.last_coordinate))
            {
                if (!m_replay_native_runtime.AtCompletedOuterTickBoundary(
                        timeline.last_coordinate))
                    return false;
                if (m_online_baseline_coordinate.frame == UINT64_MAX)
                {
                    fail_online_qualification(FailureCode::InvalidConfiguration);
                    return false;
                }
                // The entire prefix is peer-confirmed before this branch.
                // It is immutable and cannot become a correction target after
                // takeover.  Preflight the current completed boundary, whose
                // independently retained batch-entry base is the authority for
                // the first future owned correction.  Replaying from the first
                // prefix frame incorrectly rejects an asymmetric but valid
                // catch-up once it grows beyond the 29-frame retention bound.
                status = m_replay_native_runtime.PreflightOwnedCorrection(
                    timeline.last_coordinate);
                if (!status.ok())
                { fail_online_qualification(status.code); return false; }
                status = m_replay_native_runtime.SetOnlinePredictedRemotePlayer(
                    1u - m_online_local_player_slot);
                if (status.ok())
                    status = m_replay_native_runtime.EnablePresentationOwnership();
                if (!status.ok())
                { fail_online_qualification(status.code); return false; }
                m_online_prefix_catchup = false;
                m_online_next_gekko_frame = static_cast<std::int32_t>(
                    m_online_prefix_next_frame);
                m_online_takeover_ready = true;
                log_online_event(1u << 7, "prefix_catchup_completed",
                    timeline.last_coordinate);
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] online qualification armed first owned input "
                    "generation={} after confirmed prefix={}\n"),
                    timeline.last_coordinate.generation,
                    m_online_prefix_next_frame);
            }
        }
        return true;
    }

    void service_online_qualification() noexcept
    {
        bool qualification{};
        bool production{};
        Horse::Deterministic::OnlineState state{};
        if (!begin_online_service(qualification, production, state)) return;
        if (!service_online_lobby(qualification, state)) return;
        if (!service_online_handshake_and_round(qualification, state)) return;
        if (!service_online_baseline(state)) return;
        static_cast<void>(service_online_prefix(state));
    }
