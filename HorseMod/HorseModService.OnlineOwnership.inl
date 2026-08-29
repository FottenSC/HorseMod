    void observe_hgcpu_diagnostic(std::uint32_t frame) noexcept
    {
        if (!m_hgcpu_runtime_diagnostics
            || m_hgcpu_runtime_diagnostics->complete())
        {
            return;
        }
        const auto status = m_hgcpu_runtime_diagnostics->Observe(
            Horse::NativeBinding::imageBase(), frame);
        if (!status.ok()
            && status.code != Horse::Deterministic::FailureCode::ContextUnavailable
            && !m_hgcpu_diagnostic_failure_logged)
        {
            m_hgcpu_diagnostic_failure_logged = true;
            const auto failure = Horse::Deterministic::failure_code_name(status.code);
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] HgCpu runtime coverage diagnostic failed: {}\n"),
                RC::to_generic_string(std::string(failure)));
        }
    }

    static bool append_stage_break_actor_list(
        const Horse::TArrHdr* list,
        Horse::Deterministic::StageBreakActorKind kind,
        std::array<Horse::Deterministic::StageBreakActorRef, 64>& output,
        std::size_t& count) noexcept
    {
        __try
        {
            if (list == nullptr || list->Num == 0) return true;
            if (list->Data == nullptr || list->Num < 0 || list->Max < list->Num
                || list->Num > 64
                || count + static_cast<std::size_t>(list->Num) > output.size())
            {
                return false;
            }
            auto* const* entries = static_cast<RC::Unreal::UObject* const*>(
                list->Data);
            for (std::int32_t index = 0; index < list->Num; ++index)
            {
                output[count++] = {
                    kind, reinterpret_cast<std::uintptr_t>(entries[index])};
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool loaded_image_size(
        std::uintptr_t image_base, std::size_t& image_size) noexcept
    {
        image_size = 0;
        if (image_base == 0) return false;
        __try
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                image_base + static_cast<std::uintptr_t>(dos->e_lfanew));
            if (dos->e_magic != IMAGE_DOS_SIGNATURE
                || nt->Signature != IMAGE_NT_SIGNATURE
                || nt->OptionalHeader.SizeOfImage == 0)
                return false;
            image_size = nt->OptionalHeader.SizeOfImage;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void invalidate_stage_break_presentation_identity() noexcept
    {
        m_deterministic_hooks.InvalidateStageBreakPresentationIdentity();
        m_deterministic_hooks.InvalidateBattleAudioPresentationIdentity();
        m_stage_break_topology = {};
        m_stage_break_identity_actors.fill({});
        m_stage_break_identity_assets.fill({});
        m_stage_break_identity_actor_count = 0;
        m_stage_break_identity_asset_count = 0;
        m_stage_break_identity_generation = 0;
    }

    void log_qualification_stage_terminal_wait_once(const char* phase,
        Horse::Deterministic::FailureCode status =
            Horse::Deterministic::FailureCode::ContextUnavailable) noexcept
    {
        if (m_qualification_stage_terminal_request.load(
                std::memory_order_acquire) == 0
            || m_qualification_stage_terminal_wait_logged.exchange(
                true, std::memory_order_acq_rel))
        {
            return;
        }
        const auto timeline = m_replay_native_runtime.timeline_status();
        Output::send<LogLevel::Warning>(STR(
            "[HorseMod] qualification stage terminal waiting phase={} "
            "status={} timeline_status={} timeline_generation={} "
            "identity_generation={} actors={} assets={}\n"),
            RC::to_generic_string(std::string(phase)),
            RC::to_generic_string(std::string(
                Horse::Deterministic::failure_code_name(status))),
            RC::to_generic_string(std::string(
                Horse::Deterministic::failure_code_name(timeline.failure))),
            timeline.last_coordinate.generation,
            m_stage_break_identity_generation,
            m_stage_break_identity_actor_count,
            m_stage_break_identity_asset_count);
    }

    void refresh_stage_break_presentation_identity(
        std::uint64_t generation) noexcept
    {
        if (!m_deterministic_hooks.installed() || generation == 0) return;
        Horse::Obj battle_manager = m_lux.battleManager();
        Horse::Obj stage_manager = battle_manager
            ? battle_manager.getObj(L"BattleStageActorManager") : Horse::Obj{};
        if (!stage_manager)
        {
            log_qualification_stage_terminal_wait_once("stage_manager");
            return;
        }

        std::array<Horse::Deterministic::StageBreakActorRef,
            Horse::Deterministic::StageBreakPresentationIdentityMap::maximum_actors>
            actors{};
        std::size_t actor_count{};
        const bool valid_lists = append_stage_break_actor_list(
            stage_manager.getPtr<Horse::TArrHdr>(L"BreakableWallActorList"),
            Horse::Deterministic::StageBreakActorKind::Wall, actors, actor_count)
            && append_stage_break_actor_list(
                stage_manager.getPtr<Horse::TArrHdr>(L"BarrierActorList"),
                Horse::Deterministic::StageBreakActorKind::Barrier,
                actors, actor_count);
        if (!valid_lists || actor_count == 0)
        {
            log_qualification_stage_terminal_wait_once(
                valid_lists ? "empty_actor_lists" : "invalid_actor_lists",
                valid_lists
                    ? Horse::Deterministic::FailureCode::ContextUnavailable
                    : Horse::Deterministic::FailureCode::InvalidConfiguration);
            return;
        }

        const auto actors_match = [&]() noexcept {
            if (generation != m_stage_break_identity_generation
                || actor_count != m_stage_break_identity_actor_count)
                return false;
            for (std::size_t index = 0; index < actor_count; ++index)
                if (actors[index].kind != m_stage_break_identity_actors[index].kind
                    || actors[index].address
                        != m_stage_break_identity_actors[index].address)
                    return false;
            return true;
        };
        // The authored particle templates are reflected stage-instance configuration:
        // native break handlers only read them, while their separately typed live
        // component fields own the mutable effect lifecycle.  Actor membership and
        // address are still checked every frame; a stable generation plus identical
        // actor identities therefore retains the exact bound asset/topology contract
        // without repeating UObject identity and listener-topology capture each tick.
        if (actors_match()) return;
        if (!actors_match() && m_stage_break_identity_generation != 0)
            invalidate_stage_break_presentation_identity();

        std::array<Horse::Deterministic::StageBreakParticleAssetRef,
            Horse::Deterministic::StageBreakPresentationIdentityMap::maximum_assets>
            assets{};
        std::size_t asset_count{};
        auto status = Horse::Deterministic::CaptureStageBreakParticleAssets(
            m_stage_break_process_memory,
            std::span{actors.data(), actor_count}, assets, asset_count);
        if (!status.ok())
        {
            log_qualification_stage_terminal_wait_once(
                "particle_assets", status.code);
            invalidate_stage_break_presentation_identity();
            return;
        }
        const auto assets_match = [&]() noexcept {
            if (!actors_match() || asset_count != m_stage_break_identity_asset_count)
                return false;
            for (std::size_t index = 0; index < asset_count; ++index)
            {
                const auto& left = assets[index];
                const auto& right = m_stage_break_identity_assets[index];
                if (left.actor_address != right.actor_address
                    || left.route != right.route
                    || left.asset_ordinal != right.asset_ordinal
                    || left.asset_address != right.asset_address)
                    return false;
            }
            return true;
        };
        if (assets_match()) return;
        if (m_stage_break_identity_generation != 0)
            invalidate_stage_break_presentation_identity();

        std::size_t image_size{};
        const auto image_base = Horse::NativeBinding::imageBase();
        if (!loaded_image_size(image_base, image_size))
        {
            log_qualification_stage_terminal_wait_once("loaded_image");
            return;
        }
        Horse::Deterministic::StageBreakListenerTopology topology{};
        status = m_stage_break_topology_probe.Capture(image_base, image_size,
            std::span{actors.data(), actor_count}, topology);
        if (status.ok())
        {
            status = m_deterministic_hooks.BindStageBreakPresentationIdentity(
                generation, std::span{actors.data(), actor_count}, topology,
                std::span{assets.data(), asset_count});
        }
        if (!status.ok())
        {
            log_qualification_stage_terminal_wait_once(
                "listener_topology_or_bind", status.code);
            if (status.code
                    != Horse::Deterministic::FailureCode::ContextUnavailable
                && !m_stage_break_identity_failure_logged)
            {
                m_stage_break_identity_failure_logged = true;
                Output::send<LogLevel::Warning>(STR(
                    "[HorseMod] stage-break presentation identity failed: {}\n"),
                    RC::to_generic_string(std::string(
                        Horse::Deterministic::failure_code_name(status.code))));
            }
            return;
        }
        m_stage_break_topology = topology;
        m_stage_break_identity_actors = actors;
        m_stage_break_identity_assets = assets;
        m_stage_break_identity_actor_count = actor_count;
        m_stage_break_identity_asset_count = asset_count;
        m_stage_break_identity_generation = generation;
        m_stage_break_identity_failure_logged = false;
    }

    void observe_stage_break_listener_diagnostic(std::uint32_t frame) noexcept
    {
        if (!m_stage_break_listener_diagnostics
            || m_stage_break_listener_diagnostics->complete())
        {
            return;
        }
        Horse::Deterministic::Status status = Horse::Deterministic::Status::failure(
            Horse::Deterministic::FailureCode::ContextUnavailable);
        Horse::Obj battle_manager = m_lux.battleManager();
        Horse::Obj stage_manager = battle_manager
            ? battle_manager.getObj(L"BattleStageActorManager") : Horse::Obj{};
        if (!stage_manager) return;
        std::array<Horse::Deterministic::StageBreakActorRef, 64> actors{};
        std::size_t actor_count{};
        const bool valid_lists = append_stage_break_actor_list(
            stage_manager.getPtr<Horse::TArrHdr>(L"BreakableWallActorList"),
            Horse::Deterministic::StageBreakActorKind::Wall, actors, actor_count)
            && append_stage_break_actor_list(
                stage_manager.getPtr<Horse::TArrHdr>(L"BarrierActorList"),
                Horse::Deterministic::StageBreakActorKind::Barrier,
                actors, actor_count);
        if (!valid_lists)
        {
            status = Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::InvalidConfiguration);
        }
        else if (actor_count != 0)
        {
            status = m_stage_break_listener_diagnostics->Observe(
                Horse::NativeBinding::imageBase(), frame,
                std::span{actors.data(), actor_count});
        }
        if (!status.ok()
            && status.code != Horse::Deterministic::FailureCode::ContextUnavailable
            && !m_stage_break_listener_failure_logged)
        {
            m_stage_break_listener_failure_logged = true;
            Output::send<LogLevel::Warning>(STR(
                "[HorseMod] stage-break listener diagnostic failed: {}\n"),
                RC::to_generic_string(std::string(
                    Horse::Deterministic::failure_code_name(status.code))));
        }
    }

#if HORSE_ENABLE_GEKKONET
    void advance_online_qualification_status(std::uint32_t status) noexcept
    {
        auto current = m_online_qualification_status.load(
            std::memory_order_acquire);
        while (current < status && current < 6
            && !m_online_qualification_status.compare_exchange_weak(
                current, status, std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
        }
    }

    void log_online_event(std::uint32_t bit, std::string_view name,
        Horse::Deterministic::FrameCoordinate coordinate = {}) noexcept
    {
        if ((m_online_event_mask & bit) != 0) return;
        m_online_event_mask |= bit;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] online qualification run_id={} event={} "
            "generation={} frame={}\n"),
            RC::to_generic_string(m_online_run_id),
            RC::to_generic_string(std::string(name)),
            coordinate.generation, coordinate.frame);
    }

    Horse::Deterministic::SteamP2PTransport& online_transport() noexcept
    {
        return m_online_coordinator.kind() == OnlineRuntimeKind::Production
            ? m_online_production_transport
            : m_online_qualification_transport;
    }

    void fail_online_qualification(
        Horse::Deterministic::FailureCode code) noexcept
    {
        if (code == Horse::Deterministic::FailureCode::CapacityExceeded)
            m_online_qualification_metrics.RecordCapacityFailure();
        if (m_online_coordinator.state()
            != Horse::Deterministic::OnlineState::Failed)
            static_cast<void>(m_online_coordinator.Abort(code));
        const bool post_ownership = m_online_coordinator.failure_disposition()
            == Horse::Deterministic::OnlineFailureDisposition::
                TerminateMatchToLobby;
        m_online_lifecycle.BeginFailure(post_ownership);
        if (post_ownership && m_online_lifecycle.TakeLobbyRequest())
        {
            Horse::Obj hub{m_online_session_hub.get(L"LuxorSessionHub")};
            struct RequestParameters { std::int32_t result{}; } parameters{};
            hub.callRaw(m_online_request_battle_end_to_lobby,
                L"RequestBattleEndToLobby", &parameters);
            if (!hub
                || m_online_request_battle_end_to_lobby.raw() == nullptr)
                code = Horse::Deterministic::FailureCode::ContextUnavailable;
        }
        m_frame_fencepost_failure.store(code, std::memory_order_release);
        m_online_prefix_catchup = false;
        m_online_current_advance_pending = false;
        m_online_gekko.Stop();
        if (!post_ownership) m_online_takeover_ready = false;
        m_online_qualification_status.store(6, std::memory_order_release);
        Output::send<LogLevel::Warning>(STR(
            "[HorseMod] online qualification run_id={} failed status={}\n"),
            RC::to_generic_string(m_online_run_id),
            RC::to_generic_string(std::string(
                Horse::Deterministic::failure_code_name(code))));
        if (!post_ownership)
        {
            // Keep terminal status 6 observable for two game-thread service
            // turns before clearing resources and publishing status 7.  The
            // old same-call reset made the external bridge able to see 5/7
            // while missing the actual failure state entirely.
            m_online_preownership_failure_cleanup_delay = 2;
        }
    }

    Horse::Deterministic::Status prepare_online_file_identities() noexcept
    {
        if (m_online_identities_ready)
            return Horse::Deterministic::Status::success();
        std::array<wchar_t, 32768> executable_path{};
        const DWORD count = GetModuleFileNameW(nullptr, executable_path.data(),
            static_cast<DWORD>(executable_path.size()));
        const std::wstring build_path = horsemod_current_module_path();
        if (count == 0 || count >= executable_path.size()
            || build_path.empty())
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::ContextUnavailable);
        auto status = Horse::Deterministic::HashFileIdentity(
            std::filesystem::path(executable_path.data(),
                executable_path.data() + count),
            m_online_executable_identity);
        if (status.ok())
            status = Horse::Deterministic::HashFileIdentity(
                build_path, m_online_build_identity);
        m_online_identities_ready = status.ok();
        if (status.ok()
            && m_online_coordinator.kind() == OnlineRuntimeKind::Production
            && !m_online_allowlist.IsPublished())
        {
            const auto allowlist_path = std::filesystem::path(
                horsemod_current_module_path()).parent_path()
                / L"production-allowlist.ini";
            const auto release = m_online_allowlist.LoadAndPublish(allowlist_path,
                m_online_executable_identity, m_online_build_identity,
                HORSEMOD_SOURCE_COMMIT);
            if (!release.ok() && release.code
                != Horse::Deterministic::FailureCode::ContextUnavailable)
            {
                status = release;
                if (!m_online_release_manifest_failure_logged)
                {
                    m_online_release_manifest_failure_logged = true;
                    Output::send<LogLevel::Warning>(STR(
                        "[HorseMod] production release allowlist rejected: {}\n"),
                        RC::to_generic_string(std::string(
                            Horse::Deterministic::failure_code_name(
                                release.code))));
                }
            }
            if (!release.ok()
                && release.code
                    != Horse::Deterministic::FailureCode::ContextUnavailable)
                status = release;
            m_online_identities_ready = status.ok();
        }
        return status;
    }

    void reset_online_session_measurements(std::string_view run_id,
        OnlineQualificationFault fault = OnlineQualificationFault::None) noexcept
    {
        m_online_qualification_allowlist.Clear();
        m_online_executable_identity = {};
        m_online_build_identity = {};
        m_online_loaded_map_identity = {};
        m_online_baseline_coordinate = {};
        m_online_pending_coordinate = {};
        m_online_prefix_next_frame = 0;
        m_online_next_gekko_frame = 0;
        m_online_current_advance_pending = false;
        m_online_next_confirmed_hash_frame = 29;
        m_online_last_observed_coordinate = {};
        m_online_round_completed_coordinate = {};
        m_online_round_transition_pending = false;
        m_online_last_owned_inputs = {};
        m_online_last_owned_inputs_valid = false;
        m_online_round_hold_inputs = {};
        m_online_round_hold_inputs_valid = false;
        m_online_corrections = 0;
        m_online_max_correction_depth = 0;
        m_online_rounds = 1;
        m_online_first_owned_generation = 0;
        m_online_confirmed_hashes = 0;
        m_online_verified_audio_batches = 0;
        m_online_verified_camera_batches = 0;
        m_online_audio_sequence_mismatches = 0;
        m_online_camera_publication_mismatches = 0;
        m_online_presentation_failures = 0;
        m_online_qualification_fault = fault;
        m_online_qualification_fault_triggered = false;
        m_online_qualification_fault_started_ms = ::GetTickCount64();
        m_online_event_mask = 0;
        m_online_qualification_metrics.Reset();
        m_online_qualification_metrics.SetPreMatchOwnedBytes(
            m_replay_native_runtime.owned_storage_status().aggregate_bytes);
        m_online_prefix_catchup = false;
        m_online_takeover_ready = false;
        m_online_identities_ready = false;
        m_online_authentication_logged = false;
        m_online_owned_storage_prepared = false;
        m_online_preownership_failure_cleanup_delay = 0;
        m_online_run_id.assign(run_id.begin(), run_id.end());
        m_online_qualification_status.store(1, std::memory_order_release);
    }

    // Per-match transport, baseline, ownership, and round-barrier service.
    #include "OnlineQualificationService.inl"

    void reset_online_qualification_preownership() noexcept
    {
        using namespace Horse::Deterministic;
        log_online_event(1u << 9, "cleanup_started");
        const bool reenter_production =
            m_online_coordinator.kind() == OnlineRuntimeKind::Production
            && m_deterministic_config.enabled;
        m_online_production_requested.store(false,
            std::memory_order_release);
        static_cast<void>(m_online_lifecycle.BeginSceneExitCleanup());
        m_online_takeover_ready = false;
        m_online_prefix_catchup = false;
        m_online_current_advance_pending = false;
        m_online_round_transition_pending = false;
        m_online_last_owned_inputs = {};
        m_online_last_owned_inputs_valid = false;
        m_online_round_hold_inputs = {};
        m_online_round_hold_inputs_valid = false;
        m_online_last_observed_coordinate = {};
        m_online_gekko.Stop();
        m_online_coordinator.Disable();
        m_online_qualification_allowlist.Clear();
        m_online_qualification_requested.store(false,
            std::memory_order_release);
        m_online_battle_sync.invalidate();
        m_online_session_hub.invalidate();
        m_online_executable_identity = {};
        m_online_build_identity = {};
        m_online_loaded_map_identity = {};
        m_online_identities_ready = false;
        m_online_authentication_logged = false;
        m_online_owned_storage_prepared = false;
        m_online_preownership_failure_cleanup_delay = 0;
        OnlineStockClearance clearance{};
        clearance.resources.fill(true);
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Coordinator)] =
            m_online_coordinator.IsClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Transport)] =
            online_transport().IsClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Gekko)] = m_online_gekko.IsClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Allowlist)] =
            m_online_qualification_allowlist.IsClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Request)] =
            !m_online_qualification_requested.load(std::memory_order_acquire)
            && !m_online_production_requested.load(std::memory_order_acquire);
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::InputOwner)] =
            !m_online_takeover_ready && !m_online_prefix_catchup
            && !m_online_current_advance_pending
            && !m_online_round_transition_pending
            && !m_online_last_owned_inputs_valid
            && !m_online_round_hold_inputs_valid;
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::SessionIdentity)] =
            !m_online_identities_ready
            && m_online_executable_identity == CanonicalHash{}
            && m_online_build_identity == CanonicalHash{};
        const auto cleared =
            m_online_lifecycle.CompleteSceneExitCleanup(clearance);
        const auto cleanup_storage =
            m_replay_native_runtime.owned_storage_status();
        const auto qualification_metrics =
            m_online_qualification_metrics.status();
        const bool storage_returned = cleanup_storage.aggregate_bytes
            == qualification_metrics.pre_match_owned_bytes;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] online qualification run_id={} cleanup_storage "
            "pre_match_owned_bytes={} ending_owned_bytes={} returned={}\n"),
            RC::to_generic_string(m_online_run_id),
            qualification_metrics.pre_match_owned_bytes,
            cleanup_storage.aggregate_bytes, storage_returned ? 1 : 0);
        const bool cleanup_ok = cleared.ok() && storage_returned;
        if (cleanup_ok) log_online_event(1u << 10, "cleanup_completed");
        m_online_qualification_status.store(cleanup_ok ? 7u : 6u,
            std::memory_order_release);
        if (cleanup_ok && reenter_production)
            m_online_production_reentry_pending.store(true,
                std::memory_order_release);
        if (!cleanup_ok)
            m_frame_fencepost_failure.store(cleared.ok()
                    ? FailureCode::CapacityExceeded : cleared.code,
                std::memory_order_release);
    }

    void reset_online_qualification_after_scene_exit(
        const Horse::Deterministic::OnlineSceneExitEvidence& evidence) noexcept
    {
        using namespace Horse::Deterministic;
        log_online_event(1u << 9, "cleanup_started");
        const bool reenter_production =
            m_online_coordinator.kind() == OnlineRuntimeKind::Production
            && m_deterministic_config.enabled;
        m_online_production_requested.store(false,
            std::memory_order_release);
        auto coordinator_status = Status::success();
        const auto state = m_online_coordinator.state();
        if (state == OnlineState::Active || state == OnlineState::RoundBarrier)
            coordinator_status = m_online_coordinator.ReturnToLobby();
        if (coordinator_status.ok()
            && (m_online_coordinator.state() == OnlineState::ReturningToLobby
                || m_online_coordinator.state() == OnlineState::Failed))
            coordinator_status = m_online_coordinator.NotifyReturnedToLobby(
                evidence);
        if (!coordinator_status.ok())
        {
            m_online_qualification_status.store(6, std::memory_order_release);
            m_frame_fencepost_failure.store(coordinator_status.code,
                std::memory_order_release);
            return;
        }
        static_cast<void>(m_online_lifecycle.BeginSceneExitCleanup());
        m_online_takeover_ready = false;
        m_online_prefix_catchup = false;
        m_online_current_advance_pending = false;
        m_online_round_transition_pending = false;
        m_online_last_owned_inputs = {};
        m_online_last_owned_inputs_valid = false;
        m_online_round_hold_inputs = {};
        m_online_round_hold_inputs_valid = false;
        m_online_last_observed_coordinate = {};
        m_online_gekko.Stop();
        m_online_coordinator.Disable();
        invalidate_stage_break_presentation_identity();
        m_replay_native_runtime.ObserveReplayExit();
        const auto predicted =
            m_replay_native_runtime.SetOnlinePredictedRemotePlayer(std::nullopt);
        m_replay_native_runtime.DisablePresentationOwnership();
        m_online_qualification_allowlist.Clear();
        m_online_qualification_requested.store(false,
            std::memory_order_release);
        m_online_battle_sync.invalidate();
        m_online_session_hub.invalidate();
        m_online_executable_identity = {};
        m_online_build_identity = {};
        m_online_loaded_map_identity = {};
        m_online_identities_ready = false;
        m_online_authentication_logged = false;
        m_online_owned_storage_prepared = false;
        m_online_preownership_failure_cleanup_delay = 0;
        OnlineStockClearance clearance{};
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Coordinator)] =
            m_online_coordinator.IsClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Transport)] =
            online_transport().IsClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Gekko)] = m_online_gekko.IsClearForStock();
        const bool runtime_clear = predicted.ok()
            && m_replay_native_runtime.IsOnlineClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::PredictedPlayer)] = runtime_clear;
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Presentation)] = runtime_clear;
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Timeline)] = runtime_clear;
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Allowlist)] =
            m_online_qualification_allowlist.IsClearForStock();
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::Request)] =
            !m_online_qualification_requested.load(std::memory_order_acquire)
            && !m_online_production_requested.load(std::memory_order_acquire);
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::InputOwner)] =
            !m_online_takeover_ready && !m_online_prefix_catchup
            && !m_online_current_advance_pending
            && !m_online_round_transition_pending
            && !m_online_last_owned_inputs_valid
            && !m_online_round_hold_inputs_valid;
        clearance.resources[static_cast<std::size_t>(
            OnlineCleanupResource::SessionIdentity)] =
            !m_online_identities_ready
            && m_online_executable_identity == CanonicalHash{}
            && m_online_build_identity == CanonicalHash{};
        const auto cleared =
            m_online_lifecycle.CompleteSceneExitCleanup(clearance);
        const auto cleanup_storage =
            m_replay_native_runtime.owned_storage_status();
        const auto qualification_metrics =
            m_online_qualification_metrics.status();
        const bool storage_returned = cleanup_storage.aggregate_bytes
            == qualification_metrics.pre_match_owned_bytes;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] online qualification run_id={} cleanup_storage "
            "pre_match_owned_bytes={} ending_owned_bytes={} returned={}\n"),
            RC::to_generic_string(m_online_run_id),
            qualification_metrics.pre_match_owned_bytes,
            cleanup_storage.aggregate_bytes, storage_returned ? 1 : 0);
        const bool cleanup_ok = cleared.ok() && storage_returned;
        if (cleanup_ok) log_online_event(1u << 10, "cleanup_completed");
        m_online_qualification_status.store(cleanup_ok ? 7u : 6u,
            std::memory_order_release);
        if (cleanup_ok && reenter_production)
            m_online_production_reentry_pending.store(true,
                std::memory_order_release);
        if (!cleanup_ok)
            m_frame_fencepost_failure.store(cleared.ok()
                    ? FailureCode::CapacityExceeded : cleared.code,
                std::memory_order_release);
    }

    Horse::Deterministic::Status observe_online_content(
        Horse::Deterministic::OnlineContentContract& output) noexcept
    {
        Horse::Obj battle_sync{
            m_online_battle_sync.get(L"LuxOnlineBattleSync")};
        const auto selection = m_battle_sync_observer.Observe(
            battle_sync.raw(), output);
        if (!selection.ok()) return selection;

        Horse::Obj battle_manager = m_lux.battleManager();
        auto* world = battle_manager ? battle_manager.raw()->GetWorld() : nullptr;
        if (world == nullptr)
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::ContextUnavailable);
        Horse::Obj world_object{world};
        const auto* streaming = world_object.getPtr<Horse::TArrHdr>(
            L"StreamingLevels");
        constexpr std::size_t maximum_packages = 64;
        if (streaming == nullptr || streaming->Data == nullptr
            || streaming->Num <= 0
            || streaming->Num > static_cast<std::int32_t>(maximum_packages))
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::ContextUnavailable);
        std::array<std::string, maximum_packages> package_storage{};
        std::array<std::string_view, maximum_packages> package_views{};
        std::size_t package_count{};
        auto** levels = static_cast<RC::Unreal::UObject**>(streaming->Data);
        std::string stage_root{};
        for (std::int32_t index = 0; index < streaming->Num; ++index)
        {
            auto* level = levels[index];
            if (level == nullptr) continue;
            auto* package_name = level
                ->GetValuePtrByPropertyNameInChain<RC::Unreal::FName>(
                    L"PackageNameToLoad");
            if (package_name == nullptr) continue;
            const auto wide_name = package_name->ToString();
            if (wide_name.empty() || wide_name.size() > INT32_MAX) continue;
            const int utf8_size = WideCharToMultiByte(CP_UTF8,
                WC_ERR_INVALID_CHARS, wide_name.data(),
                static_cast<int>(wide_name.size()), nullptr, 0, nullptr, nullptr);
            if (utf8_size <= 0) continue;
            std::string name(static_cast<std::size_t>(utf8_size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                    wide_name.data(), static_cast<int>(wide_name.size()),
                    name.data(), utf8_size, nullptr, nullptr) != utf8_size)
                continue;
            const auto duplicate = std::find_if(package_storage.begin(),
                package_storage.begin() + package_count,
                [&](const std::string& existing) { return existing == name; });
            if (duplicate != package_storage.begin() + package_count) continue;
            package_storage[package_count] = std::move(name);
            package_views[package_count] = package_storage[package_count];

            const auto stage_marker = package_storage[package_count].find(
                "/Stage/");
            if (stage_marker != std::string::npos)
            {
                const auto component_start = stage_marker + 7;
                const auto component_end = package_storage[package_count].find(
                    '/', component_start);
                const auto root = package_storage[package_count].substr(0,
                    component_end == std::string::npos
                        ? package_storage[package_count].size() : component_end);
                if (stage_root.empty()) stage_root = root;
                else if (stage_root != root)
                    return Horse::Deterministic::Status::failure(
                        Horse::Deterministic::FailureCode::IdentityMismatch);
            }
            ++package_count;
        }
        if (package_count == 0 || stage_root.empty()
            || stage_root != output.map_name.data())
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::IdentityMismatch);
        const auto* stage = Horse::Deterministic::FindQualifiedStage(
            output.stage_code.data());
        const bool exact_main_map_loaded = stage != nullptr
            && std::find(package_views.begin(),
                package_views.begin() + package_count, stage->map_path)
                != package_views.begin() + package_count;
        if (!exact_main_map_loaded)
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::IdentityMismatch);
        Horse::Deterministic::CanonicalHash loaded_map_identity{};
        const auto map_hash = Horse::Deterministic::HashMapPackageIdentity(
            std::span<const std::string_view>{package_views.data(), package_count},
            loaded_map_identity);
        if (!map_hash.ok()) return map_hash;
        if (m_online_coordinator.kind() == OnlineRuntimeKind::Production)
        {
            const auto* expected = m_online_allowlist
                .ExpectedLoadedMapIdentity(output);
            if (expected == nullptr || *expected != loaded_map_identity)
                return Horse::Deterministic::Status::failure(
                    Horse::Deterministic::FailureCode::IdentityMismatch);
        }
        m_online_loaded_map_identity = loaded_map_identity;
        return Horse::Deterministic::Status::success();
    }

    bool online_coordinate_for_frame(std::int32_t frame,
        Horse::Deterministic::FrameCoordinate& coordinate) const noexcept
    {
        if (m_online_baseline_coordinate.generation == 0 || frame < -1)
            return false;
        coordinate = m_online_baseline_coordinate;
        if (frame >= 0)
        {
            const auto offset = static_cast<std::uint64_t>(frame) + 1;
            if (coordinate.frame > UINT64_MAX - offset) return false;
            coordinate.frame += offset;
        }
        return true;
    }

    Horse::Deterministic::Status Save(std::int32_t frame, GekkoSaveKind,
        std::span<std::byte> destination, std::uint32_t& written,
        std::uint32_t& checksum) noexcept override
    {
        written = 0;
        checksum = 0;
        Horse::Deterministic::FrameCoordinate coordinate{};
        if (destination.size() < sizeof(OnlineStateToken)
            || !online_coordinate_for_frame(frame, coordinate))
        {
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::InvalidConfiguration);
        }
        OnlineStateToken token{};
        token.gekko_frame = frame;
        token.coordinate = coordinate;
        const auto hash = m_replay_native_runtime.GetCanonicalHash(
            coordinate, token.hash);
        if (!hash.ok()) return hash;
        std::memcpy(destination.data(), &token, sizeof(token));
        written = sizeof(token);
        for (std::size_t index = 0; index < token.hash.size(); ++index)
        {
            checksum = (checksum * 16777619u)
                ^ std::to_integer<std::uint8_t>(token.hash[index]);
        }
        return Horse::Deterministic::Status::success();
    }

    Horse::Deterministic::Status Load(std::int32_t frame,
        std::span<const std::byte> state) noexcept override
    {
        if (state.size() != sizeof(OnlineStateToken))
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::ProtocolMismatch);
        OnlineStateToken token{};
        std::memcpy(&token, state.data(), sizeof(token));
        Horse::Deterministic::FrameCoordinate expected{};
        if (token.magic != 0x484f5253u || token.version != 1
            || token.reserved != 0 || token.gekko_frame != frame
            || !online_coordinate_for_frame(frame, expected)
            || token.coordinate != expected)
        {
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::IdentityMismatch);
        }
        Horse::Deterministic::CanonicalHash current{};
        const auto found = m_replay_native_runtime.GetCanonicalHash(
            token.coordinate, current);
        if (!found.ok()) return found;
        if (current != token.hash)
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::StateHashMismatch);
        return Horse::Deterministic::Status::success();
    }

    Horse::Deterministic::Status Advance(
        const Horse::Deterministic::GekkoAdvanceValue& value) noexcept override
    {
        Horse::Deterministic::FrameCoordinate coordinate{};
        if (!online_coordinate_for_frame(value.frame, coordinate)
            || value.running_ahead)
        {
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::InvalidConfiguration);
        }
        if (m_online_prefix_catchup)
        {
            const auto existing = m_replay_native_runtime.input_timeline()
                .GetExact(coordinate);
            if (!existing.has_value()
                || existing->players[m_online_local_player_slot]
                    != value.inputs[m_online_local_player_slot])
            {
                return Horse::Deterministic::Status::failure(
                    Horse::Deterministic::FailureCode::IdentityMismatch);
            }
            if (value.rolling_back
                && (existing->players[0] != value.inputs[0]
                    || existing->players[1] != value.inputs[1]))
            {
                // A peer-confirmed prefix differing from the stock match is
                // a pre-ownership rejection. No native state has been
                // mutated and stock remains authoritative.
                return Horse::Deterministic::Status::failure(
                    Horse::Deterministic::FailureCode::StateHashMismatch);
            }
            if (!value.rolling_back
                && value.frame != static_cast<std::int32_t>(
                    m_online_prefix_next_frame))
            {
                return Horse::Deterministic::Status::failure(
                    Horse::Deterministic::FailureCode::AdvanceFailed);
            }
            return Horse::Deterministic::Status::success();
        }
        if (!value.rolling_back)
        {
            if (m_online_current_advance_pending
                || value.frame != m_online_next_gekko_frame)
            {
                return Horse::Deterministic::Status::failure(
                    Horse::Deterministic::FailureCode::AdvanceFailed);
            }
            m_online_pending_coordinate = coordinate;
            m_online_current_advance_pending = true;
            ++m_online_next_gekko_frame;
            return Horse::Deterministic::Status::success();
        }

        const auto existing = m_replay_native_runtime.input_timeline().GetExact(
            coordinate);
        if (!existing.has_value()
            || existing->players[m_online_local_player_slot]
                != value.inputs[m_online_local_player_slot])
        {
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::IdentityMismatch);
        }
        Horse::Deterministic::OwnedCorrectionResult correction{};
        const bool changed = existing->players[1u - m_online_local_player_slot]
            != value.inputs[1u - m_online_local_player_slot];
        if (changed && !m_online_qualification_fault_triggered
            && m_online_qualification_fault
                == OnlineQualificationFault::PostownershipRestore)
        {
            m_online_qualification_fault_triggered = true;
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::RestoreWriteFailed);
        }
        const auto corrected = m_replay_native_runtime.ApplyConfirmedRemoteInput(
            coordinate, 1u - m_online_local_player_slot,
            value.inputs[1u - m_online_local_player_slot],
            m_deterministic_hooks, correction);
        if (!corrected.ok()) return corrected;
        if (!correction.converged)
            return Horse::Deterministic::Status::failure(
                Horse::Deterministic::FailureCode::StateHashMismatch);
        if (changed)
        {
            ++m_online_corrections;
            m_online_verified_audio_batches += correction.verified_audio_batches;
            m_online_verified_camera_batches += correction.verified_camera_batches;
            m_online_audio_sequence_mismatches +=
                correction.audio_sequence_mismatches;
            m_online_camera_publication_mismatches +=
                correction.camera_publication_mismatches;
            m_online_presentation_failures += correction.presentation_failures;
            m_online_qualification_metrics.RecordCorrection(correction.total_ns);
            const auto depth = m_online_next_gekko_frame > value.frame
                ? static_cast<std::uint32_t>(
                    m_online_next_gekko_frame - value.frame) : 0u;
            m_online_max_correction_depth = (std::max)(
                m_online_max_correction_depth, depth);
        }
        return Horse::Deterministic::Status::success();
    }

    static Horse::Deterministic::AuthoritativeInputDisposition
        on_authoritative_input(void* user,
            const Horse::Deterministic::OuterTickObservation& observation,
            bool stock_valid,
            const Horse::Deterministic::PlayerInput (&stock)[2],
            Horse::Deterministic::PlayerInput (&authoritative)[2]) noexcept
    {
        auto* self = static_cast<HorseMod*>(user);
        if (self == nullptr || !self->m_online_takeover_ready)
            return Horse::Deterministic::AuthoritativeInputDisposition::Stock;
        const auto lifecycle = self->m_online_lifecycle.phase();
        if (lifecycle == Horse::Deterministic::
                OnlineLifecyclePhase::FailClosedAwaitingSceneExit)
        {
            if (self->m_online_round_hold_inputs_valid)
            {
                authoritative[0] = self->m_online_round_hold_inputs[0];
                authoritative[1] = self->m_online_round_hold_inputs[1];
            }
            return Horse::Deterministic::AuthoritativeInputDisposition::FailClosed;
        }
        if (lifecycle == Horse::Deterministic::OnlineLifecyclePhase::PreOwnership)
        {
            if (!stock_valid)
            {
                self->fail_online_qualification(
                    Horse::Deterministic::FailureCode::ContextUnavailable);
                return Horse::Deterministic::
                    AuthoritativeInputDisposition::Stock;
            }
            if (self->m_online_coordinator.state()
                    != Horse::Deterministic::OnlineState::Active
                || self->m_online_prefix_catchup)
            {
                self->fail_online_qualification(
                    Horse::Deterministic::FailureCode::IllegalTransition);
                return Horse::Deterministic::
                    AuthoritativeInputDisposition::Stock;
            }
            std::array<Horse::Deterministic::PlayerInput, 2> selected{};
            const auto selected_status = self->m_online_gekko.Advance(
                stock[self->m_online_local_player_slot], selected);
            if (!selected_status.ok())
            {
                self->fail_online_qualification(selected_status.code);
                return Horse::Deterministic::
                    AuthoritativeInputDisposition::Stock;
            }
            self->m_online_last_owned_inputs = selected;
            self->m_online_last_owned_inputs_valid = true;
            self->m_online_round_hold_inputs = {{
                {selected[0].held, 0}, {selected[1].held, 0}}};
            self->m_online_round_hold_inputs_valid = true;
            authoritative[0] = selected[0];
            authoritative[1] = selected[1];
            return Horse::Deterministic::AuthoritativeInputDisposition::
                PreparedTakeover;
        }
        if (!self->m_online_coordinator.owns_simulation())
            return Horse::Deterministic::AuthoritativeInputDisposition::Stock;
        if (!stock_valid)
        {
            self->fail_online_qualification(
                Horse::Deterministic::FailureCode::ContextUnavailable);
            if (self->m_online_round_hold_inputs_valid)
            {
                authoritative[0] = self->m_online_round_hold_inputs[0];
                authoritative[1] = self->m_online_round_hold_inputs[1];
            }
            return Horse::Deterministic::AuthoritativeInputDisposition::FailClosed;
        }
        if (self->m_online_coordinator.state()
                != Horse::Deterministic::OnlineState::Active
            || self->m_online_prefix_catchup)
        {
            if (!self->m_online_round_hold_inputs_valid)
            {
                self->fail_online_qualification(
                    Horse::Deterministic::FailureCode::MissingInput);
                return Horse::Deterministic::
                    AuthoritativeInputDisposition::FailClosed;
            }
            authoritative[0] = self->m_online_round_hold_inputs[0];
            authoritative[1] = self->m_online_round_hold_inputs[1];
            return Horse::Deterministic::AuthoritativeInputDisposition::
                OwnedRoundBarrier;
        }
        const auto& timeline = self->m_replay_native_runtime
            .timeline_status_view();
        const bool entering_owned_round_barrier =
            timeline.last_coordinate.generation != 0
            && observation.before.input_game_round != timeline.native_round;
        if (entering_owned_round_barrier)
        {
            self->m_online_round_transition_pending = true;
            const bool previous_generation_confirmed =
                self->m_online_gekko.confirmed_frame()
                    >= self->m_online_next_gekko_frame - 1;
            const auto confirmed_pair = self->m_replay_native_runtime
                .input_timeline().GetExact(timeline.last_coordinate);
            if (!previous_generation_confirmed || !confirmed_pair.has_value())
            {
                self->fail_online_qualification(
                    Horse::Deterministic::FailureCode::MissingInput);
                return Horse::Deterministic::
                    AuthoritativeInputDisposition::FailClosed;
            }
            self->m_online_round_hold_inputs = {{
                {confirmed_pair->players[0].held, 0},
                {confirmed_pair->players[1].held, 0}}};
            self->m_online_round_hold_inputs_valid = true;
            authoritative[0] = self->m_online_round_hold_inputs[0];
            authoritative[1] = self->m_online_round_hold_inputs[1];
            return Horse::Deterministic::AuthoritativeInputDisposition::
                OwnedRoundBarrier;
        }
        std::array<Horse::Deterministic::PlayerInput, 2> selected{};
        const auto status = self->m_online_gekko.Advance(
            stock[self->m_online_local_player_slot], selected);
        if (!status.ok())
        {
            self->fail_online_qualification(status.code);
            return Horse::Deterministic::
                AuthoritativeInputDisposition::FailClosed;
        }
        authoritative[0] = selected[0];
        authoritative[1] = selected[1];
        self->m_online_last_owned_inputs = selected;
        self->m_online_last_owned_inputs_valid = true;
        self->m_online_round_hold_inputs = {{
            {selected[0].held, 0}, {selected[1].held, 0}}};
        self->m_online_round_hold_inputs_valid = true;
        return Horse::Deterministic::AuthoritativeInputDisposition::Replace;
    }

#endif
