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
