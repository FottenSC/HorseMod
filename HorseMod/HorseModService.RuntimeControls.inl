    auto on_update() -> void override
    {
        // Throttled settings persistence.  Runs every frame so we
        // catch changes regardless of hook-registration state (the
        // early-return below would otherwise skip it after the
        // cockpit hook is registered).  save_persisted_settings
        // fills the ModSettings map and asks it to save_if_dirty;
        // unchanged values are an O(map-lookup) no-op inside set(),
        // so the only actual disk I/O happens when a user toggled
        // something since the last save.
        if (++m_save_tick >= kSaveEveryNFrames)
        {
            m_save_tick = 0;
            save_persisted_settings();
        }

        service_presence_transition_safety("update");
        service_frame_fencepost_diagnostics();
        service_gameimgui_toggle_key_release();
        // IsInGameThread() throws until UE4SS records the game-thread id;
        // on_update can run before that during startup. Use only the old
        // API here so Thunderstore's UE4SS shimloader does not need the
        // newer IsInGameThreadRaw() export.
        const bool in_game_thread = []() noexcept {
            try { return RC::Unreal::IsInGameThread(); }
            catch (...) { return false; }
        }();
        if (in_game_thread
            && m_engine_tick_callback_id == RC::Unreal::Hook::ERROR_ID)
        {
            service_gameimgui_deferred_install();
        }

        const bool all_reset_registered = std::all_of(
            m_reset_slots.begin(), m_reset_slots.end(),
            [](const ResetHookSlot& s) { return s.registered; });
        const bool online_rules_installed =
            Horse::OnlineRules::instance().hooks_installed();
        const bool game_mode_installed =
            Horse::GameMode::instance().hook_installed();
        const bool replay_exit_hook_required =
            m_deterministic_config.trace || m_deterministic_config.enabled;
        const bool replay_exit_hook_ready = !replay_exit_hook_required
            || m_battle_terminate_hook_registered;
        if (m_hook_registered && all_reset_registered
            && online_rules_installed && game_mode_installed
            && replay_exit_hook_ready)
            return;
        if (++m_poll_counter < 60) return;
        m_poll_counter = 0;
        if (!m_hook_registered)        try_register_cockpit_hook();
        if (!all_reset_registered)     try_register_reset_hooks();
        if (replay_exit_hook_required
            && !m_battle_terminate_hook_registered)
            try_register_battle_terminate_hook();
        if (!online_rules_installed)
            Horse::OnlineRules::instance().try_install_hooks();
        // GameMode: hook SetPresence so we know which scene the user
        // is in (Training / Replay / online match / etc).  Idempotent
        // and silent on retry - the LuxUIGamePresenceUtil class is a
        // BlueprintFunctionLibrary loaded very early, so this usually
        // succeeds on the first poll attempt.
        if (!game_mode_installed)
            (void)Horse::GameMode::instance().try_install_hook();
    }

private:
    void service_gameimgui_toggle_key_release() noexcept
    {
        if (!m_gameimgui_toggle_key_down.load(std::memory_order_acquire))
            return;

        const bool async_down = (::GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        const bool ll_down = Horse::LowLevelKeyInput::instance().is_down(VK_F2);
        const bool raw_down = Horse::RawInputSource::instance().is_down(VK_F2);
        if (!async_down && !ll_down && !raw_down)
        {
            m_gameimgui_toggle_key_down.store(false,
                                              std::memory_order_release);
        }
    }

    void service_gameimgui_deferred_install()
    {
        if (!m_gameimgui_init_pending || m_gameimgui_init_attempted) return;
        if (m_gameimgui_init_delay_ticks_remaining > 0)
        {
            --m_gameimgui_init_delay_ticks_remaining;
            return;
        }

        m_gameimgui_init_pending = false;
        m_gameimgui_init_attempted = true;
        if (!Horse::GameImGui::initialize())
        {
            Output::send<LogLevel::Error>(
                STR("[HorseMod] Horse::GameImGui::initialize() failed; "
                    "the in-game ImGui overlay will not appear.\n"));
            return;
        }

        Output::send<LogLevel::Default>(
            STR("[HorseMod] deferred GameImGui install complete\n"));
    }

    void try_register_cockpit_hook()
    {
        Horse::Obj cockpit = m_lux.cockpit();
        if (!cockpit) return;

        UClass* klass = cockpit.raw()->GetClassPrivate();
        if (!klass) return;

        m_hook_path = klass->GetPathName() + STR(":Update");

        // CRITICAL: pre-validate the UFunction exists before calling
        // RegisterHook(path).  UE4SS's path overload (UObjectGlobals.cpp
        // line 859) calls StaticFindObject<UFunction*> with no null check
        // and then dereferences the result inside the UFunction* overload
        // at line 810 (Function->GetFunc()) - null deref crashes the
        // game.  Worse, even with a valid UFunction the inner overload
        // can THROW std::runtime_error if the function isn't FUNC_Native
        // and isn't ProcessInternal-routed (line 855).  An uncaught
        // exception across the DLL boundary tears down the whole mod
        // (and on some MSVC configs, the host process).
        //
        // Pre-checking here means a not-yet-loaded class just retries
        // next poll tick.  The try/catch around RegisterHook below
        // catches the FUNC_Native mismatch case and downgrades it to a
        // log line so we don't crash the game on an unexpected mod /
        // engine version mismatch.
        UFunction* fn = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, m_hook_path);
        if (!fn)
        {
            // Class was found but its :Update UFunction isn't loaded yet.
            // Will retry next poll tick.  Log only at Verbose so we don't
            // spam the log during the seconds-long window before the
            // cockpit blueprint finishes registering.
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] Cockpit UFunction '{}' not yet loaded; "
                    "will retry on next poll tick.\n"), m_hook_path);
            return;
        }

        Output::send<LogLevel::Verbose>(STR("[HorseMod] Registering hook: {}\n"), m_hook_path);

        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext& ctx, void*) {
                if (auto* self = s_instance.load(std::memory_order_acquire))
                    self->on_cockpit_update_pre(ctx.Context);
            };
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {};

        // Wrap RegisterHook in try/catch - the underlying UE4SS code
        // throws std::runtime_error if the UFunction isn't a hookable
        // shape (see UObjectGlobals.cpp:855).  We don't want that
        // exception escaping into UE4SS's mod loop.
        try
        {
            m_hook_ids = UObjectGlobals::RegisterHook(m_hook_path, pre_cb, post_cb, nullptr);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(
                STR("[HorseMod] RegisterHook threw on '{}': {}\n"),
                m_hook_path, RC::to_generic_string(e.what()));
            // Don't set m_hook_registered - poll loop will skip this
            // path on retry once the underlying issue is fixed.  In
            // practice an exception here means the engine version is
            // wrong and we won't recover, but we'd rather log forever
            // than crash forever.
            return;
        }

        // RegisterHook returns {0, 0} for the global-script-hook path
        // (UObjectGlobals.cpp:842 increments before assigning, so the
        // smallest legitimate ID is 1).  An all-zero pair is therefore
        // a sentinel for "registration silently no-op'd" - defensively
        // refuse to mark registered so we keep retrying.
        if (m_hook_ids.first == 0 && m_hook_ids.second == 0)
        {
            Output::send<LogLevel::Warning>(
                STR("[HorseMod] RegisterHook returned (0,0) for '{}' - "
                    "treating as failure.\n"), m_hook_path);
            return;
        }

        m_hook_registered = true;
        Output::send<LogLevel::Verbose>(STR("[HorseMod] hook pre={} post={}\n"),
            m_hook_ids.first, m_hook_ids.second);
    }

    // Register a post-hook on each reset-related UFunction in m_reset_slots.
    //
    // Each post-hook fires AFTER the engine has run the round-intro position
    // chain (PositionCharasByRoundConfig -> PositionCharasSymmetrically ->
    // LuxBattleChara_SetStartPosition) for that path - the right spot to
    // overwrite the chara pose with the user's captured override.
    //
    // Multi-path rationale: the user's reset bind goes through a UFunction
    // we can't determine statically (they may have rebound it; SC6's
    // training-mode UI may dispatch via a different entry point depending
    // on context).  We register on every plausible candidate and the one
    // that fires logs its identity via the custom_data tag - both for our
    // diagnosis here and for the user to see in UE4SS.log.
    //
    // Each slot's class lookup gates that slot independently - failed
    // lookups (class not yet loaded) just retry next poll tick, same way
    // try_register_cockpit_hook does.
    void try_register_reset_hooks()
    {
        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {};
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void* custom_data) {
                // Identify which path fired via the tag we passed at
                // registration time (the slot's func_path c_str()).
                const wchar_t* path = static_cast<const wchar_t*>(custom_data);
                const bool reset_override_enabled =
                    Horse::ResetOverride::instance().enabled();
                if (reset_override_enabled)
                {
                    Output::send<LogLevel::Default>(
                        STR("[HorseMod] reset hook fired: {}\n"),
                        path ? path : STR("(unknown path)"));
                }
                else
                {
                    Output::send<LogLevel::Verbose>(
                        STR("[HorseMod] reset hook ignored while disabled: {}\n"),
                        path ? path : STR("(unknown path)"));
                }

                // Apply the captured pose.  Idempotent if multiple hooks
                // fire on the same reset (engine may chain through more
                // than one of these UFunctions for a single user press).
                Horse::ResetOverride::instance().apply_to_charas();
            };

        for (auto& slot : m_reset_slots)
        {
            if (slot.registered) continue;

            UClass* klass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, slot.class_path);
            if (!klass)
            {
                // Class not yet registered - try again next poll tick.
                continue;
            }

            // CRITICAL: also verify the UFunction exists before calling
            // RegisterHook(path).  UE4SS's path-overload of RegisterHook
            // (UObjectGlobals.cpp:859) calls StaticFindObject<UFunction*>
            // and then immediately dereferences the result via
            // Function->GetFunc() - so a null-result (function-not-found)
            // crashes the game with a null deref.
            //
            // Pre-checking here means a wrong/typo'd path just logs a
            // warning and skips that slot; the rest of the mod loads
            // unscathed.  We only log once (slot stays unregistered but
            // we mark it so we don't retry a known-bad path forever).
            UFunction* fn = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, slot.func_path);
            if (!fn)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] Reset-override hook SKIPPED: UFunction "
                        "'{}' not found on class '{}' - typo or wrong class? "
                        "Will retry on next poll tick.\n"),
                    slot.func_path, slot.class_path);
                continue;
            }

            // c_str() is stable for the lifetime of the wstring, which
            // outlives the hook (m_reset_slots vector is never reassigned
            // after ctor population).  Pass it as custom_data so the
            // post-hook can identify which path triggered it.
            void* tag = const_cast<wchar_t*>(slot.func_path.c_str());

            // RegisterHook can throw std::runtime_error if the resolved
            // UFunction isn't a hookable shape (see UObjectGlobals.cpp:855).
            // The pre-check above covers the common "not loaded yet" case
            // but not e.g. a Blueprint-only UFunction that doesn't qualify
            // as ProcessInternal-routed.  Keep the exception inside this
            // function rather than letting it climb out into UE4SS.
            try
            {
                slot.ids = UObjectGlobals::RegisterHook(
                    slot.func_path, pre_cb, post_cb, tag);
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(
                    STR("[HorseMod] Reset-hook RegisterHook threw on '{}': {}\n"),
                    slot.func_path, RC::to_generic_string(e.what()));
                // Mark this slot registered=false so we retry next
                // poll tick if the underlying issue is transient.
                continue;
            }
            if (slot.ids.first == 0 && slot.ids.second == 0)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] Reset-hook RegisterHook returned (0,0) "
                        "for '{}' - treating as failure.\n"),
                    slot.func_path);
                continue;
            }
            slot.registered = true;
            Output::send<LogLevel::Default>(
                STR("[HorseMod] Reset-override hook registered: {} (pre={} post={})\n"),
                slot.func_path, slot.ids.first, slot.ids.second);
        }
    }

    void try_register_battle_terminate_hook()
    {
        UFunction* function = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, m_battle_terminate_hook_path);
        if (function == nullptr) return;
        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {
                auto* self = s_instance.load(std::memory_order_acquire);
                if (self == nullptr) return;
                const auto contract =
                    self->m_online_coordinator.active_contract();
                const bool online_requested =
                    self->m_online_qualification_requested.load(
                        std::memory_order_acquire)
                    || self->m_online_production_requested.load(
                        std::memory_order_acquire);
                const bool cleanup_armed = contract
                    && self->m_online_scene_exit_gate
                        .ArmBeforeBattleTermination(
                            contract->session_id, online_requested,
                            self->m_online_lifecycle.phase());
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] LuxBattleGameMode termination requested; "
                    "invalidating native replay identity before BattleManager "
                    "teardown online_cleanup_armed={}\n"),
                    cleanup_armed ? 1 : 0);
                Horse::Deterministic::ReplayExitObservation observation{
                    0, ::GetCurrentThreadId()};
                HorseMod::on_replay_exit(self, observation);
            };
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {
                auto* self = s_instance.load(std::memory_order_acquire);
                if (self == nullptr) return;
                const auto evidence = self->m_online_scene_exit_gate
                    .CompleteAfterBattleTermination();
                if (!evidence) return;
                Output::send<LogLevel::Default>(STR(
                    "[HorseMod] LuxBattleGameMode termination completed; "
                    "running deferred online scene-exit cleanup session={}\n"),
                    evidence->session_id);
                self->reset_online_qualification_after_scene_exit(*evidence);
            };
        try
        {
            m_battle_terminate_hook_ids = UObjectGlobals::RegisterHook(
                m_battle_terminate_hook_path, pre_cb, post_cb, nullptr);
        }
        catch (const std::exception& error)
        {
            Output::send<LogLevel::Error>(STR(
                "[HorseMod] LuxBattleGameMode termination-hook registration "
                "failed: {}\n"), RC::to_generic_string(error.what()));
            return;
        }
        if (m_battle_terminate_hook_ids.first == 0
            && m_battle_terminate_hook_ids.second == 0)
            return;
        m_battle_terminate_hook_registered = true;
        Output::send<LogLevel::Default>(STR(
            "[HorseMod] LuxBattleGameMode termination hook registered "
            "pre={} post={}\n"), m_battle_terminate_hook_ids.first,
            m_battle_terminate_hook_ids.second);
    }

    // ---- Ansel "always allow photography" apply-per-frame helper -------
    // Pushes UAnselFunctionLibrary::SetIsPhotographyAllowed(bVisible)
    // via ProcessEvent when either the toggle is ON or we're on the
    // ON -> OFF edge (one-shot restore).  Called from the top of the
    // cockpit pre-hook so it runs every frame independent of the F5
    // overlay state.
    //
    // Safe to call before NativeBinding is resolved - this path is
    // pure UE4 reflection and does not touch SC6 RVAs.  Safe when no
    // ----------------------------------------------------------------
    // Online-match feature gate - force-disable the four "competitive"
    // features (lock camera, free-fly, freeze frame, slow motion) when
    // the user is in a Ranked / Casual online match AND has "Auto
    // disable online" enabled in the General tab.  Idempotent - calling
    // disable() on an already-disabled feature is a no-op.  Called from
    // on_cockpit_update_pre BEFORE the normal apply_* / frame_step_apply
    // / free_camera_apply chain so the rest of those helpers see the
    // already-cleared atomics and produce no work.
    //
    // Each branch logs once on the OFF transition (was-on + now-forced-
    // off) at Default level so the user can confirm the gate engaged.
    // After that, while the match continues, repeated calls are silent
    // because the underlying atomic is already false.
    // ----------------------------------------------------------------
    // Subset of apply_online_forced_disable() that ONLY clears the
    // time-related features (freeze frame, slow-motion, step queue).
    // Called from the presence-transition watcher in
    // on_cockpit_update_pre as a safety net against black-screen /
    // broken-camera-init bugs that happen when SpeedControl patches
    // stay applied while SC6 tears down + rebuilds BattleManager and
    // chara actors during a mode change.
    //
    // Why we don't clear camera-lock + free-fly here (unlike the
    // full apply_online_forced_disable):
    //   - Camera lock is a static bytepatch (CamLock).  It doesn't
    //     interfere with actor lifecycle - the engine's camera
    //     stores still update the underlying memory, our patch just
    //     no-ops the writer.  Carrying it across a transition is
    //     harmless.
    //   - Free-fly camera owns the camera-lock state machine; it
    //     too is harmless across transitions because the cockpit
    //     hook has its own resolve-on-first-use logic for the new
    //     mode's camera manager.
    //
    // Freeze + slow-mo are different because they install into
    // PerFrameTick / replay tick / cursor advance - exactly the
    // paths that get re-entered from the new mode's chara
    // initialization.  Suppressing those during init breaks setup.
    void clear_time_features_on_transition()
    {
        if (m_freeze_frame.load() || m_step_pending.load() > 0)
        {
            m_freeze_frame.store(false);
            m_step_pending.store(0);
            m_step_expecting.store(false);
            m_step_witness.valid = false;
            m_step_dwell = 0;
        }
        if (m_speed_enabled.load() || m_speed_control.is_enabled())
        {
            m_speed_enabled.store(false);
            m_speed_control.disable();
        }
        // World-tick gate: same hazard as the SpeedControl patches -
        // a presence transition rebuilds BattleManager + chara actors,
        // and a stale gate "frozen" state would block PerFrameTick on
        // the new mode's first tick (= black screen).  Disable so the
        // engine runs at native rate during the transition; user re-
        // engages freeze/step manually after the new mode loads.
        // Disable the sibling gates first (they READ the WorldTickGate
        // policy slot, so leaving them enabled past the gate's disable
        // would be harmless but pointless).
        if (m_actor_tick_gate.is_enabled())
            m_actor_tick_gate.disable();
        if (m_time_dilation_gate.is_enabled())
            m_time_dilation_gate.disable();
        if (m_wind_rng_gate.is_enabled())
            m_wind_rng_gate.disable();
        if (m_world_tick_gate.is_enabled())
            m_world_tick_gate.disable();
    }

    void apply_online_forced_disable()
    {
        // ---- Lock camera position --------------------------------
        // Two pieces of state to keep coherent:
        //   - m_lock_camera (the user's "preferred" toggle state)
        //   - m_cam_lock    (the actual BytePatch enable/disable)
        // We force m_cam_lock off and clear m_lock_camera so the UI
        // checkbox (which reads m_cam_lock.is_enabled()) shows OFF
        // and the persisted setting reflects the gate-induced state.
        if (m_cam_lock.is_enabled() || m_lock_camera.load())
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Lock camera position\n"));
            m_lock_camera.store(false);
            m_cam_lock.set(false);
        }

        // ---- Free-fly camera -------------------------------------
        // Toggling m_free_camera_enabled OFF here causes free_camera_apply()
        // to take its "want_off" branch on the next call, which releases
        // the underlying CamLock + restores the engine camera path.
        if (m_free_camera_enabled.load() || m_free_camera.is_enabled())
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Free-fly camera\n"));
            m_free_camera_enabled.store(false);
            // Don't call m_free_camera.set(false, ...) directly here -
            // it needs the live PCM pointer which free_camera_apply
            // already resolves.  Letting that helper do the actual
            // state-machine work keeps the two ownership rules
            // coherent (free_camera_apply is the ONLY place that calls
            // m_free_camera.set).
        }

        // ---- Freeze frame ----------------------------------------
        // Clear the freeze atomic and any pending step queue.  The
        // frame_step_apply driver picks this up on the next tick and
        // restores speedval to the slow-mo base (or 1.0 if slow-mo
        // is off - and we're about to force that off too).
        if (m_freeze_frame.load() || m_step_pending.load() > 0)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Freeze frame\n"));
            m_freeze_frame.store(false);
            m_step_pending.store(0);
            m_step_expecting.store(false);
            m_step_witness.valid = false;
            m_step_dwell = 0;
        }
        if (m_actor_tick_gate.is_enabled())
            m_actor_tick_gate.disable();
        if (m_time_dilation_gate.is_enabled())
            m_time_dilation_gate.disable();
        if (m_wind_rng_gate.is_enabled())
            m_wind_rng_gate.disable();
        if (m_world_tick_gate.is_enabled())
        {
            // Don't double-log if the freeze-frame branch above already
            // covered the user-visible "force-disabling" message; the gate
            // disable is implementation detail of the same feature.
            m_world_tick_gate.disable();
        }

        // ---- Slow motion -----------------------------------------
        // Match the UI checkbox callback's behaviour for "slow motion
        // turned off": clear m_speed_enabled, then disable the
        // SpeedControl patches (which resets the shared speedval
        // back to 1.0 - see SpeedControl::disable()).
        if (m_speed_enabled.load() || m_speed_control.is_enabled())
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] online gate: force-disabling Slow motion\n"));
            m_speed_enabled.store(false);
            m_speed_control.disable();
        }
    }

    // battle chara exists (menu / loading) because the CDO always
    // exists once the Ansel module is loaded.
    void apply_ansel_override_if_needed()
    {
        using namespace RC;
        using namespace RC::Unreal;

        const bool now  = m_ansel_always_allowed.load();
        const bool last = m_last_applied_ansel_allowed.load();
        // Nothing to do while both the toggle and last-applied are off -
        // let the engine manage the flag itself.
        if (!now && !last) return;

        // Resolve / re-resolve CDO.  UObject::IsReal catches the case
        // where UObjectArray was rebuilt (rare, but survivable).
        if (!m_ansel_cdo || !UObject::IsReal(m_ansel_cdo))
        {
            m_ansel_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                STR("/Script/Ansel.Default__AnselFunctionLibrary"));
            if (!m_ansel_cdo)
            {
                // Ansel plugin not loaded in this build / run; silently
                // skip.  (One-shot log would be nice but is low value
                // - the toggle is visibly inert in that case.)
                return;
            }
        }

        UFunction* f = m_fn_set_photo_allowed.on(
            m_ansel_cdo, STR("SetIsPhotographyAllowed"));
        if (!f) return;

        // `now` = desired visibility state; `last` captures whether
        // we're on the restore edge (last=true, now=false ? push false
        // once).
        struct { bool bIsPhotographyAllowed; } p{ now };
        m_ansel_cdo->ProcessEvent(f, &p);

        m_last_applied_ansel_allowed.store(now);
    }

    // ---- Frame-step + freeze-frame driver -------------------------------
    // Called every cockpit tick. Computes gate policy from the user's Freeze
    // and Slow-mo toggles, plus pending step-frame requests, and publishes it
    // to WorldTickGate plus replay/actor/time sibling gates.
    //
    // ==========================================================================
    // STEP PROTOCOL (current state, 2026-05)
    // ==========================================================================
    // The cockpit pre-hook fires on the UMG widget tick. Current stepping no
    // longer depends on cockpit timing or speedval writes: m_step_pending is
    // drained into WorldTickGate credits, and PerFrameTick consumes exactly
    // one credit per native game frame.
    //
    // KNOWN OPEN ISSUES (2026-05):
    //   * Multi-hit moves only register the FIRST hit when frame-stepped
    //     in training mode.  Sites 19/20/21/22 (replay-pipeline gates +
    //     chara TickActor entry-RET) are enabled but did not fix this.
    //     A speculative BattleAdvanceFlag override (force flOutBlendW0=1,
    //     nOutModeTag=0 at step Tick A to defeat the AND-of-three gate
    //     at PerFrameTick step 3) was tried and reverted - empirically
    //     didn't fix it, AND had side effects during normal gameplay.
    //     Root cause is deeper in the hit-classifier or per-cell hit-mask
    //     advance path; needs targeted investigation.
    //   * Held inputs may not refresh correctly during step.  Likely
    //     related to the multi-hit miss above (shared upstream cause).
    //   * GetTimeDilationScalar Path A (chara+0x3510 < 0 = super-freeze /
    //     soul-charge cinematic / KO replay) is engine-controlled.
    //   * AdvanceLaneFrameStep advances by dt - pLane[+0x30] (PlaybackSpeed).
    //     Moves with non-unity playback speed advance by != 1.0 anim
    //     frames per step.  Matches native gameplay; by-engine design.
    //
    // State machine:
    //   click(F6)  m_step_pending++
    //   cockpit    add m_step_pending to WorldTickGate credits
    //   PerFrameTick consumes one credit and runs once at native dt

    // Snapshot the step-mode world-tick witness (per-lane tick counters
    // at lane+0x04 for both charas).  Returns true if at least one
    // counter was successfully read.  Marks `out.valid` accordingly so
    // a later compare can short-circuit on "no usable snapshot".
    bool capture_step_world_tick_witness(StepWorldTickWitness& out) noexcept
    {
        out = StepWorldTickWitness{};
        bool any = false;
        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* chara = Horse::KHitWalker::charaSlotFromGlobal(pi);
            if (!chara) continue;
            auto* b = reinterpret_cast<const uint8_t*>(chara);
            int32_t l0 = 0, l1 = 0;
            const bool ok0 = Horse::SafeReadInt32(b + 0x444F0 + 0x04, &l0);
            const bool ok1 = Horse::SafeReadInt32(b + 0x44958 + 0x04, &l1);
            if (!ok0 || !ok1) continue;
            if (pi == 0) { out.p0_lane0_tickctr = l0; out.p0_lane1_tickctr = l1; }
            else         { out.p1_lane0_tickctr = l0; out.p1_lane1_tickctr = l1; }
            any = true;
        }
        out.valid = any;
        return any;
    }

    // Compare current witness against `prev`.  Returns true when the
    // world has ticked since `prev` was captured (any lane counter
    // changed), OR when we cannot measure (no prior snapshot or
    // current snapshot fails) - the conservative "assume ticked"
    // fallback avoids permanent state-machine lockup if the chara
    // struct disappears mid-step.
    bool world_ticked_since(const StepWorldTickWitness& prev) noexcept
    {
        if (!prev.valid) return true;
        StepWorldTickWitness cur{};
        if (!capture_step_world_tick_witness(cur)) return true;
        return prev.p0_lane0_tickctr != cur.p0_lane0_tickctr
            || prev.p0_lane1_tickctr != cur.p0_lane1_tickctr
            || prev.p1_lane0_tickctr != cur.p1_lane0_tickctr
            || prev.p1_lane1_tickctr != cur.p1_lane1_tickctr;
    }

    void frame_step_apply()
    {
        const bool freeze = m_freeze_frame.load();
        const bool slow_mo = m_speed_enabled.load();
        const int pending = m_step_pending.exchange(0);

        if (freeze || pending > 0)
        {
            if (!m_world_tick_gate.is_resolved())
                m_world_tick_gate.resolve();
            if (m_world_tick_gate.is_resolved()
                && !m_world_tick_gate.is_enabled())
                m_world_tick_gate.enable();
            if (!m_actor_tick_gate.is_resolved())
                m_actor_tick_gate.resolve(
                    m_world_tick_gate.policy_slot_address());
            if (m_actor_tick_gate.is_resolved()
                && !m_actor_tick_gate.is_enabled())
                m_actor_tick_gate.enable();
            if (!m_time_dilation_gate.is_resolved())
                m_time_dilation_gate.resolve(
                    m_world_tick_gate.policy_slot_address());
            if (m_time_dilation_gate.is_resolved()
                && !m_time_dilation_gate.is_enabled())
                m_time_dilation_gate.enable();
            if (pending > 0)
                m_world_tick_gate.add_step(pending);
            if (m_speed_control.is_enabled())
                m_speed_control.disable();
            m_last_tick_kind.store(
                static_cast<uint8_t>(pending > 0
                    ? TickKind::Go
                    : TickKind::Stop),
                std::memory_order_release);
            return;
        }

        if (m_world_tick_gate.is_enabled())
            m_world_tick_gate.disable();
        if (m_actor_tick_gate.is_enabled())
            m_actor_tick_gate.disable();
        if (m_time_dilation_gate.is_enabled())
            m_time_dilation_gate.disable();

        if (slow_mo)
        {
            if (!m_speed_control.is_resolved())
                m_speed_control.resolve();
            if (!m_speed_control.is_enabled())
                m_speed_control.enable();
            m_speed_control.set_value(m_speed_value.load());
        }
        else if (m_speed_control.is_enabled())
        {
            m_speed_control.disable();
        }
        m_last_tick_kind.store(
            static_cast<uint8_t>(slow_mo
                ? TickKind::Go
                : TickKind::Inactive),
            std::memory_order_release);
    }

    // SEH-wrapped single-byte write to g_LuxBattle_VMFreezeRecord.bVMFreezeByte.
    // Lifted to a static helper because __try/__except can't share a
    // function body with C++ destructors (frame_step_apply has plenty).
    // Returns true on successful write; false if the access faulted.
    static bool try_write_vm_freeze_byte(volatile uint8_t* p, uint8_t value) noexcept
    {
        __try
        {
            *p = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // try_force_battle_advance_flag: REMOVED 2026-05-02 - see comment
    // block in frame_step_apply for the rationale (didn't fix multi-hit
    // miss, had side effects during normal gameplay).

    // ------------------------------------------------------------------
    // Multi-hit lockout-clear workaround (2026-05).
    // ------------------------------------------------------------------
    // The classifier (LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100)
    // gates hits on the ATTACKER chara's flag tuple:
    //
    //   attacker[+0x16E5] != 0   // attack-active: in an attack
    //   attacker[+0x16EA] != 0   // ready-to-hit:  in an ACTIVE PHASE
    //   attacker[+0x16EB] == 0   // lockout:       no hit yet this phase
    //   attacker[+0x16FE] == 0   // lockout 2
    //   *attacker[+0x44058] != 0 // own-cell pointer
    //
    // When a hit lands the engine sets +0x16EA=1, +0x16EB=1 atomically.
    // In NATIVE PLAY between hits of a multi-hit move, hit-stop fires
    // (chara+0x3500 -> 0, chara+0x3508 > 0); hit-stop ending re-triggers
    // sub-handlers that clear +0x16EB back to 0 so the NEXT active phase
    // can register a hit.
    //
    // The user's frame-step diagnostic log (Siegfried move 0x015A,
    // lane=1, 1.00x) shows that during stepping HIT-STOP NEVER ENGAGES:
    // chara+0x3500 stays at 1.0 and chara+0x3508 stays at -1 throughout.
    // Hit-stop is the natural clearer; without it +0x16EB latches to 1
    // forever after the first hit and the multi-hit lockout gate blocks
    // all subsequent hit-classifier passes.  Symptom: every step past
    // the first hit's frame fails the gate -> "the move only hits once".
    //
    // FIX: at every step Tick A (pre-hook BEFORE the next world tick),
    // for each chara, if (16E5==1 && 16EA==0 && 16EB==1) ? clear 16EB to
    // 0.  This emulates the engine's between-active-phases cleanup that
    // would normally come out of hit-stop end.  Conditions:
    //
    //   16E5==1   = the chara IS attacking.  Don't clear lockout if not
    //                (e.g. defender just past a hit they took - irrelevant).
    //   16EA==0   = no active phase right now.  Clearing during an active
    //                phase would let a single hitbox register repeatedly
    //                within the SAME phase (double-hit bug).
    //   16EB==1   = lockout is currently latched.  Skip if already clear.
    //
    // Also clears +0x16FE (the secondary lockout flag in the same gate
    // tuple) under the same condition.  Sites 1402fd04b and 1402fd054
    // in the binary write both +0x16EB and +0x16FE in the same
    // CommitMoveEnd code path, confirming they're a paired-clear.
    //
    // SEH-wrapped because the chara pointer can be null mid-load.
    //
    // ----- Multi-hit lockout clearer (gated, cadence-tracked) ------------
    // BACKGROUND
    // ----------
    // SC6 has at least three native multi-hit mechanisms:
    //
    //   (A) HIT-STOP PACED.  The bytecode dispatches a hit-stop opcode
    //       (LuxMoveVM_DispatchEffectOp branch at 0x1403794a0) which
    //       queues +0x3504/+0x350c, committed by
    //       LuxBattle_TickHitStopSchedulerAndInputMirror to
    //       +0x3500/+0x3508.  The 1-tick decrement of +0x3508 paces
    //       hits; on its way to 0 a sub-handler clears +0x16EB so the
    //       next active phase can register a hit.
    //
    //   (B) TRANSITION-MOVE PACED.  The move bytecode authors a
    //       16EB-conditional transition target at lane+0x5E, so when
    //       +0x16EB latches, LuxMoveVM_CheckMoveTransitionTiming
    //       overrides the default target with that one and (when the
    //       lane+0x68 threshold meets the other lane's anim cursor)
    //       calls TransitionToMove, whose snapshot section
    //       unconditionally clears +0x16EB.
    //
    //   (C) DEFAULT-TRANSITION PACED.  The move authors lane+0x5A but
    //       NOT lane+0x5E (e.g. Siegfried's 4A+B with default target
    //       0x150 and threshold 46 frames).  CheckMoveTransitionTiming
    //       still fires TransitionToMove when the threshold lands and
    //       16EB clears as part of that.
    //
    // In step mode the speedval=1.0 tick runs the full simulation, so
    // (B) and (C) work just like native - IF the threshold is reached.
    // (A) is the one that consistently breaks during step: hit-stop
    // queues, but the scheduler that consumes the queue gates on
    // VMFreezeByte and on a tight tick cadence that the step rhythm
    // disrupts; the diagnostic log on Siegfried 4A+B shows
    // chara+0x3500 stays at 1.0 and +0x3508 stays at -1 throughout the
    // master window - hit-stop never engages - so 16EB latches at the
    // first hit and stays latched forever.
    //
    // STRATEGY
    // --------
    // This helper is a SAFETY NET, not a reimplementation.  It only
    // fires when ALL of the following hold:
    //   * Chara is attacking            (16E5=1)
    //   * Lockout is latched            (16EB=1)
    //   * Hit-stop is NOT running       (3508 <= 0)
    //                                    - hit-stop would naturally pace
    //                                      and clear 16EB on its own
    //   * No 16EB-conditional override  (lane[+0x5E] == -1 on lane 0/1)
    //                                    - engine path (B) handles this
    //
    // When all gate, we apply the cadence-counted clear: count step
    // ticks where 16EA=0 (engine forced it off due to 16EB), and after
    // kEBLockoutDelay ticks, clear 16EB so the next tick's classifier
    // can re-arm 16EA and the resolver can fire another hit.  This
    // emulates the timing that hit-stop would have produced.
    //
    // The cadence is DATA-DRIVEN per cell, derived from
    // cell+0x46 (HitstunStandingNormal) - read every step, divided by 4.
    // RATIONALE: SC6 doesn't expose an explicit "frames between hits"
    // field anywhere I could locate via static analysis; what IS
    // authored on every cell is hitstun (the defender's stun frames).
    // In fighting-game design, hit-stop (the attacker's stop frames
    // between hits) is typically ~1/4 of hitstun, so we use that as
    // our derived cadence.  For Siegfried 4A+B (cell+0x46 = 30):
    //   K = 30/4 = 7  ? 8-frame cycle ? hits at anim 18/26/34
    //                    in the [17..39] master window = 3 hits.
    // For shorter-hitstun moves the cadence shrinks proportionally;
    // for single-hit moves the cell's master window is short enough
    // that the second cycle never lands in the active phase, so they
    // still fire only once.  No per-move table required.
    //
    // CAVEAT: this is a HEURISTIC (the /4 ratio).  The engine's exact
    // multi-hit pacing for moves like 4A+B uses a mechanism I could
    // not isolate via static byte search of all common store
    // encodings - the 16EB latch isn't written via direct disp32, it
    // appears to come from indirect addressing or a struct-stamp path
    // (probably inside the hit-application chain in
    // LuxBattleChara_*).  If a move under-/over-fires, the formula
    // is the lever - change /4 to /3 (faster) or /5 (slower).
    static constexpr int kEBLockoutDivisor = 4;
    static constexpr int kEBLockoutFallback = 7;   // when cell read fails
    static constexpr int kEBLockoutMin = 2;        // never below this
    static constexpr int kEBLockoutMax = 30;       // never above this
    static inline int s_eb_lockout_delay[2] = {0, 0};

    static void try_clear_multi_hit_lockout_for_step() noexcept
    {
        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* chara = Horse::KHitWalker::charaSlotFromGlobal(pi);
            if (!chara)
            {
                s_eb_lockout_delay[pi] = 0;
                continue;
            }
            __try
            {
                auto* b = reinterpret_cast<volatile uint8_t*>(chara);
                const uint8_t v_16e5 = b[0x16E5];
                const uint8_t v_16ea = b[0x16EA];
                const uint8_t v_16eb = b[0x16EB];

                // Not attacking, or not locked out: reset cadence.
                if (v_16e5 == 0 || v_16eb == 0)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: hit-stop engaged.  When chara+0x3508 > 0 the
                // engine is in hit-stop and will naturally clear 16EB
                // when the timer expires.  Don't interfere - even
                // clearing 16EB during hit-stop would let the next
                // tick fire another hit through hit-stop, breaking
                // engine semantics.
                int32_t v_3508 = -1;
                std::memcpy(&v_3508,
                            const_cast<const uint8_t*>(b) + 0x3508,
                            sizeof(v_3508));
                if (v_3508 > 0)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: engine has 16EB-conditional transition override
                // authored on the active lane.  CheckMoveTransitionTiming
                // will swap target to lane[+0x5E] when 16EB is latched,
                // and once the threshold is reached TransitionToMove
                // clears 16EB itself.  We must not race that path.
                //
                // Check both lanes - the active attack could be on
                // either.  Lane 0 = chara+0x444F0, lane 1 = +0x44958.
                int16_t lane0_2F = -1, lane1_2F = -1;
                std::memcpy(&lane0_2F,
                            const_cast<const uint8_t*>(b) + 0x444F0 + 0x5E,
                            sizeof(lane0_2F));
                std::memcpy(&lane1_2F,
                            const_cast<const uint8_t*>(b) + 0x44958 + 0x5E,
                            sizeof(lane1_2F));
                if (lane0_2F != -1 || lane1_2F != -1)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: engine has authored a default transition target
                // on the active lane (lane[+0x5A]).  When the bytecode
                // emits CALLCOND 0x07 via LuxMoveVM_DecodeVariadicStreamArgs
                // (instruction at 0x1402FCAF6: MOV [RBX+0x5a], R9W) and
                // the lane[+0x68] threshold is reached, TransitionToMove
                // fires and clears chara+0x16EB itself.  We must not race
                // that path - clearing 16EB heuristically here would let
                // the resolver fire on the current (wrong) cell instead
                // of the cell the engine is about to switch to.
                int16_t lane0_5A = -1, lane1_5A = -1;
                std::memcpy(&lane0_5A,
                            const_cast<const uint8_t*>(b) + 0x444F0 + 0x5A,
                            sizeof(lane0_5A));
                std::memcpy(&lane1_5A,
                            const_cast<const uint8_t*>(b) + 0x44958 + 0x5A,
                            sizeof(lane1_5A));
                if (lane0_5A != -1 || lane1_5A != -1)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // GATE: deferred transition target authored via the
                // CALLCOND 0x15 wrapper path (lane[+0xB4]).  Same race
                // concern as 0x5A - let the engine's transition path
                // own the 16EB clear when it has work queued.
                int16_t lane0_B4 = -1, lane1_B4 = -1;
                std::memcpy(&lane0_B4,
                            const_cast<const uint8_t*>(b) + 0x444F0 + 0xB4,
                            sizeof(lane0_B4));
                std::memcpy(&lane1_B4,
                            const_cast<const uint8_t*>(b) + 0x44958 + 0xB4,
                            sizeof(lane1_B4));
                if (lane0_B4 != -1 || lane1_B4 != -1)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // 16EA simultaneously set means the hit JUST fired this
                // step; reset the delay counter so we start counting
                // from this fresh latch.
                if (v_16ea != 0)
                {
                    s_eb_lockout_delay[pi] = 0;
                    continue;
                }

                // 16E5=1, 16EB=1, 16EA=0 - engine forced 16EA off
                // because of the lockout; classic between-hits state.
                // Compute the per-move cadence threshold from the
                // active cell's HitstunStandingNormal (cell+0x46).
                int delay_threshold = kEBLockoutFallback;
                void* cell_ptr = nullptr;
                std::memcpy(&cell_ptr,
                            const_cast<const uint8_t*>(b) + 0x44058,
                            sizeof(cell_ptr));
                if (cell_ptr)
                {
                    int16_t hitstun = 0;
                    auto* cb = reinterpret_cast<const volatile uint8_t*>(cell_ptr);
                    __try
                    {
                        std::memcpy(&hitstun,
                                    const_cast<const uint8_t*>(cb) + 0x46,
                                    sizeof(hitstun));
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        hitstun = 0;
                    }
                    if (hitstun > 0)
                    {
                        int k = hitstun / kEBLockoutDivisor;
                        if (k < kEBLockoutMin) k = kEBLockoutMin;
                        if (k > kEBLockoutMax) k = kEBLockoutMax;
                        delay_threshold = k;
                    }
                }

                s_eb_lockout_delay[pi]++;
                if (s_eb_lockout_delay[pi] >= delay_threshold)
                {
                    b[0x16EB] = 0;
                    b[0x16FE] = 0;
                    s_eb_lockout_delay[pi] = 0;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Faulted - chara pointer not mapped this frame.  Skip.
                s_eb_lockout_delay[pi] = 0;
            }
        }
    }

    // ------------------------------------------------------------------
    // Free-fly camera driver - wired into the cockpit pre-hook.
    //
    // Per-tick responsibilities:
    //   * Resolve the ALuxBattleCamera* from LuxBattleManager.BattleCamera
    //     (UObject property, stable within a battle, null between).
    //   * Handle UI-toggle edge transitions (ON ? snapshot current pose +
    //     enable CamLock; OFF ? release CamLock, drop our pose state).
    //   * If enabled, drive the camera-manager's POV cache from
    //     keyboard / gamepad input via FreeCamera::tick(), which
    //     writes directly to PCM+0x410..+0x428.  That block IS the
    //     renderer-facing POV - see the long comment on
    //     m_cached_player_camera_manager above for the Ghidra
    //     evidence and the iterations that led us to it.
    //
    // We DON'T early-return when the overlay F5 is off - the user may
    // want to fly the camera around to take screenshots without the
    // overlay, matching the Ansel-replacement use case.
    // ------------------------------------------------------------------
    void free_camera_apply()
    {
        // ---------------- Performance gate (perf audit, 2026-04) ----------
        // Skip the per-tick PlayerCameraManager reflection chain when
        // free-fly is neither user-requested nor currently engaged.
        // The chain (FindFirstOf<APlayerController> + PlayerCameraManager
        // FName-indexed property-chain walk on each tick) is amortised
        // cheap once cached, but it's still wasted work for the common
        // case of a user that never presses F7.  We drop straight to
        // ~0 ns on those frames and only revive the resolution chain
        // once the user actually engages free-fly.
        //
        // Correctness: when both gates are false, m_free_camera.tick()
        // would early-return anyway (FreeCamera.hpp:537), and there's
        // no UI consumer of m_cached_player_camera_manager that needs
        // a value here (the HUD memory-verify panel is only meaningful
        // while free-fly is on, and in the OFF state we want it to
        // read "no PCM resolved" rather than a stale pointer).
        const bool want_on = m_free_camera_enabled.load();
        if (!want_on && !m_free_camera.is_enabled())
        {
            m_cached_player_camera_manager = nullptr;
            return;
        }

        // Resolve the APlayerCameraManager every tick - this is the
        // write target for Free-Fly pose data (see the long comment
        // on m_cached_player_camera_manager for why it is NOT the
        // ALuxBattleCamera from LuxBattleManager.BattleCamera).
        //
        // Primary path (reflection):
        //   find-first-of APlayerController ? read its
        //   "PlayerCameraManager" UObject* property.
        // Fallback (direct offset):
        //   PC+0x420 is the native PlayerCameraManager field on
        //   APlayerController - this is the EXACT offset that
        //   UWorld::Tick @ 0x141f02230 reads when it invokes
        //   APlayerCameraManager_CommitPOV_NoInterp(pc[0x84]).
        //   If UE4SS reflection ever fails to find the property
        //   (e.g. a build where the name string is stripped) the
        //   direct-offset path still works.
        //
        // Both lookups are hashed FName-indexed / trivial pointer reads
        // and cached across frames via GlobalPtr::get / Obj::getObj.
        // GlobalPtr::get throttles its O(N) revalidation scan (see
        // HorseLib.hpp); the presence-transition block above
        // invalidate()s m_player_controller so a torn-down PC is
        // re-resolved promptly instead of at the next throttle tick.
        void* pcm = nullptr;
        UObject* pc_raw = m_player_controller.get(L"PlayerController");
        if (pc_raw)
        {
            Horse::Obj pc_obj{pc_raw};
            Horse::Obj pcm_obj = pc_obj.getObj(L"PlayerCameraManager");
            if (pcm_obj)
            {
                pcm = pcm_obj.raw();
            }
            else
            {
                // Direct-offset fallback - matches the UWorld::Tick
                // read that feeds the engine's own commit path.
                auto* pc_bytes = reinterpret_cast<uint8_t*>(pc_raw);
                void* raw_pcm = *reinterpret_cast<void**>(pc_bytes + 0x420);
                if (raw_pcm) pcm = raw_pcm;
                if (!m_logged_pcm_fallback)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[HorseMod.FreeCamera] reflection didn't find "
                            "PlayerCameraManager property - using direct "
                            "offset fallback PC+0x420 -> 0x{:x}\n"),
                        reinterpret_cast<uintptr_t>(pcm));
                    m_logged_pcm_fallback = true;
                }
            }
        }
        // One-shot first-resolve log - captures BOTH the PC address
        // and the PCM address so the user (or Ghidra) can sanity-check
        // both pointers against whatever the engine reports at runtime.
        if (!m_logged_pcm_resolve && pcm)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod.FreeCamera] resolved PC=0x{:x} PCM=0x{:x} "
                    "(Ghidra-verified write target for +0x410..+0x428)\n"),
                reinterpret_cast<uintptr_t>(pc_raw),
                reinterpret_cast<uintptr_t>(pcm));
            m_logged_pcm_resolve = true;
        }
        m_cached_player_camera_manager = pcm;

        // Handle UI-toggle edge transitions.  `want_on` was captured at
        // the top of the function for the perf gate; reuse it here so
        // we observe a single consistent snapshot of the toggle on this
        // tick (avoids any chance of a TOCTOU split between the gate
        // check and the edge handler).
        if (want_on != m_free_camera.is_enabled())
        {
            m_free_camera.set(want_on, m_cam_lock, pcm);
        }

        // Per-tick pose update (no-op if not enabled or pcm null).
        // This is the ONLY commit path - direct memcpy into the PCM's
        // FCameraCacheEntry.POV at +0x410..+0x428.  CamLock's 5 NOP
        // sites (all of which also target PCM+0x410..+0x428) stop the
        // engine from stomping our writes each tick.
        m_free_camera.tick(pcm);
    }
