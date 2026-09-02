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
            Sc6OnlineSessionIdentity session{};
            auto status = m_sc6_online_session_observer.Observe(session);
            if (!status.ok()) return false;
            SteamLobbyIdentity lobby{};
            status = m_steam_lobby_observer.Observe(session.lobby_id, lobby);
            if (!status.ok()) { fail_online_qualification(status.code); return false; }
            Horse::Obj sync{m_online_battle_sync.get(L"LuxOnlineBattleSync")};
            OnlineContentContract content{};
            status = m_battle_sync_observer.Observe(sync.raw(), content);
            if (!status.ok()) return false;
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
            if (state == OnlineState::AwaitingBattle
                && m_online_round_transition_pending)
            {
                const auto& timeline =
                    m_replay_native_runtime.timeline_status_view();
                m_online_gekko.Stop();
                m_online_baseline_coordinate = {};
                m_online_prefix_next_frame = 0;
                m_online_next_gekko_frame = 0;
                m_online_next_confirmed_hash_frame = 29;
                m_online_current_advance_pending = false;
                m_online_pending_coordinate = {};
                m_online_round_transition_pending = false;
                // Session/authentication remain one-time facts.  Baseline,
                // catch-up, and first-input ownership must be evidenced for
                // every newly owned native generation.
                m_online_event_mask &= (1u << 0) | (1u << 1);
                m_online_prefix_catchup = true;
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
            if (!polled.ok()) { fail_online_qualification(polled.code); return false; }
            state = m_online_coordinator.state();
        }
        if (state == OnlineState::AwaitingBattle
            && m_online_gekko.ReadyForBaseline())
        {
            const auto& timeline = m_replay_native_runtime.timeline_status_view();
            if (timeline.last_coordinate.generation == 0) return false;
            const auto ready = m_online_coordinator.ReadyBaseline(
                timeline.last_coordinate);
            if (!ready.ok()) { fail_online_qualification(ready.code); return false; }
            log_online_event(1u << 2, "local_baseline_ready",
                timeline.last_coordinate);
            state = m_online_coordinator.state();
        }
        if (state == OnlineState::AwaitingBaselineTarget)
        {
            const auto& timeline = m_replay_native_runtime.timeline_status_view();
            const auto target = m_online_coordinator.baseline_target();
            if (!target.has_value())
            { fail_online_qualification(FailureCode::IdentityMismatch); return false; }
            log_online_event(1u << 3, "bilateral_baseline_target", *target);
            const auto progress = m_online_coordinator.ObserveBaselineProgress(
                timeline.last_coordinate);
            if (!progress.ok())
            { fail_online_qualification(progress.code); return false; }
            if (timeline.last_coordinate != *target) return false;
            OnlineContentContract loaded{};
            const auto map = observe_online_content(loaded);
            if (!map.ok()) return false;
            const auto contract = m_online_coordinator.active_contract();
            if (!contract.has_value() || loaded != contract->content)
            { fail_online_qualification(FailureCode::IdentityMismatch); return false; }
            CanonicalHash hash{};
            const auto found = m_replay_native_runtime.GetCanonicalHash(
                *target, hash);
            if (!found.ok()) return false;
            m_online_baseline_coordinate = *target;
            const auto frozen = m_online_coordinator.FreezeBaseline(
                *target, hash, m_online_loaded_map_identity);
            if (!frozen.ok()) { fail_online_qualification(frozen.code); return false; }
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
            const auto processed = m_online_prefix_next_frame == 0
                ? -1 : static_cast<std::int32_t>(m_online_prefix_next_frame - 1);
            const FrameCoordinate next{
                m_online_baseline_coordinate.generation,
                m_online_baseline_coordinate.frame
                    + m_online_prefix_next_frame + 1};
            if (m_online_gekko.confirmed_frame() >= processed
                && next > timeline.last_coordinate)
            {
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
