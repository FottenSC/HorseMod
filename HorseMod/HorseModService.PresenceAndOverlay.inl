    void service_presence_transition_safety(const char* source)
    {
        const uint8_t cur = static_cast<uint8_t>(
            Horse::GameMode::instance().current_presence());
        const uint8_t prev = m_last_seen_presence.exchange(
            cur, std::memory_order_acq_rel);
        if (prev == cur)
            return;

        using GMP = Horse::GamePresence;
        const GMP from = static_cast<GMP>(prev);
        const GMP to   = static_cast<GMP>(cur);
        if (from != GMP::Unknown)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod] presence transition {} -> {} - "
                    "force-clearing Freeze frame + Slow-motion + "
                    "step queue (manual re-enable required, source={})\n"),
                Horse::presence_name(from),
                Horse::presence_name(to),
                RC::to_generic_string(source ? source : "?"));
        }

        clear_time_features_on_transition();

        // Drop the cached battle-level globals.  LuxBattleManager /
        // CockpitBase / PlayerController are torn down across this
        // transition; invalidating forces the next GlobalPtr::get() to
        // re-resolve immediately.
        m_lux.invalidate();
        m_player_controller.invalidate();
        m_backend_hit.invalidate();
        m_backend_hurt.invalidate();
        m_backend_hit_once.invalidate();
        m_backend_hurt_once.invalidate();
        m_backend_stage.invalidate();
        m_stage_boundary.invalidate();
        m_stage_visuals.invalidate();
#if HORSE_ENABLE_GEKKONET || HORSE_ENABLE_OBSERVER_PROBE
        // The initializer can run while entering CasualMatch, before the
        // presence callback is observed. Preserve that newly published
        // pointer on entry; clear it on every transition out of the owning
        // scene. Qualification reset paths also clear it between cycles.
        if (to != GMP::CasualMatch)
            Horse::Deterministic::Sc6BattleSyncOwnerHook::instance().clear();
#endif
#if HORSE_ENABLE_GEKKONET
        const auto online_contract = m_online_coordinator.active_contract();
        m_online_session_hub.invalidate();
        if (from == GMP::CasualMatch && to != GMP::CasualMatch
            && (m_online_qualification_requested.load(
                    std::memory_order_acquire)
                || m_online_production_requested.load(
                    std::memory_order_acquire)))
        {
            const Horse::Deterministic::OnlineSceneExitEvidence evidence{
                online_contract ? online_contract->session_id : 0,
                Horse::Deterministic::OnlineSceneExitBoundary::
                    CasualMatchPresenceExit};
            if (m_online_lifecycle.RequiresOwnedInput())
                reset_online_qualification_after_scene_exit(evidence);
            else
                reset_online_qualification_preownership();
        }
#endif
#if HORSE_ENABLE_OBSERVER_PROBE
        m_online_observer_probe.Disarm();
#endif
    }

    // ------------------------------------------------------------------
    // Line overlays that must sample completed battle state.
    //
    // CockpitBase_C::Update can run before LuxBattle_PerFrameTick on a UE
    // frame.  KHit node buffers are refreshed inside that battle tick, so
    // drawing from the cockpit pre-hook can show previous native KHit
    // positions while the later collision pass already uses the new ones.
    // This post-engine-tick path samples after the battle tick has had its
    // chance to flip Area buffers, update Sphere/FixArea world points, apply
    // Sphere anim-cell modifiers, and resolve hits.
    // ------------------------------------------------------------------
    static bool can_draw_battle_overlays_for_presence(
        Horse::GamePresence presence) noexcept
    {
        using GMP = Horse::GamePresence;
        switch (presence)
        {
            case GMP::ShinEdgeMaster:
            case GMP::Chronicle:
            case GMP::Arcade:
            case GMP::Versus:
            case GMP::Training:
            case GMP::RankMatch:
            case GMP::CasualMatch:
            case GMP::Replay:
            case GMP::Tournament:
                return true;
            case GMP::MainMenu:
            case GMP::Creation:
            case GMP::Ranking:
            case GMP::Museum:
            case GMP::Options:
            case GMP::Unknown:
                return false;
        }
        return false;
    }

    void draw_line_overlays_after_battle_tick()
    {
        if (!can_draw_battle_overlays_for_presence(
                Horse::GameMode::instance().current_presence()))
        {
            m_have_sphere_audit_frame = false;
            m_khit_render_calibration = {};
            return;
        }

        const bool wants_line_overlay =
            m_show_stage_boundary.load() || m_enabled.load();
        if (!wants_line_overlay)
        {
            m_have_sphere_audit_frame = false;
            m_khit_render_calibration = {};
            return;
        }

        Horse::Obj pivot = m_lux.cockpit();
        if (!pivot) return;

        const bool native_ready = Horse::NativeBinding::isReady();
        if (!native_ready)
        {
            if ((m_enabled.load() || m_show_stage_boundary.load()) &&
                !m_logged_native_missing)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] NativeBinding not ready - overlay draw disabled\n"));
                m_logged_native_missing = true;
            }
            return;
        }

        // Render cadence and gameplay cadence are deliberately separate.
        // Freeze-frame halts g_LuxBattle_FrameCounter, but the overlay still
        // has to redraw every engine-post callback so foreground/current
        // hitboxes remain visible.  The game-frame counter is used only below
        // for persistent trail sampling and lifetime aging.

        if (m_show_stage_boundary.load())
        {
            if (m_backend_stage.slot() != Horse::LineBatcherSlot::Foreground)
                m_backend_stage.setSlot(Horse::LineBatcherSlot::Foreground);
            m_backend_stage.setLifetime(Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_stage.primeFrom(pivot);
            if (m_backend_stage.isReady())
            {
                Horse::Obj bm = m_lux.battleManager();
                Horse::Obj stageManager =
                    bm ? bm.getObj(L"BattleStageActorManager") : Horse::Obj{};
                m_backend_stage.beginFrame();
                (void)m_stage_boundary.draw(m_backend_stage, stageManager);
                m_backend_stage.endFrame();
            }
        }

        if (!m_enabled.load()) return;

        const Horse::LineBatcherSlot desired_hit_slot  = m_slot_hit.load();
        const Horse::LineBatcherSlot desired_hurt_slot = m_slot_hurt.load();
        const bool only_active_this_frame = m_only_show_active.load();

        bool trail_filter_changed = false;
        if (m_have_trail_filter_state)
        {
            trail_filter_changed =
                only_active_this_frame != m_last_trail_only_active;
        }
        m_last_trail_only_active = only_active_this_frame;
        m_have_trail_filter_state = true;

        // Sync each configured backend's slot with the per-feature ImGui
        // toggles and prime all KHit backends this frame.  *_once backends
        // are fixed Foreground fallbacks for inactive boxes when the
        // configured backend is Persistent.
        bool trail_slot_changed = false;
        bool clear_hit_trail_after_prime = false;
        bool clear_hurt_trail_after_prime = false;
        if (m_backend_hit.slot() != desired_hit_slot)
        {
            if (m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hit.clearLines();
            m_backend_hit.setSlot(desired_hit_slot);
            clear_hit_trail_after_prime =
                desired_hit_slot == Horse::LineBatcherSlot::Persistent;
            trail_slot_changed = true;
        }
        if (m_backend_hurt.slot() != desired_hurt_slot)
        {
            if (m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hurt.clearLines();
            m_backend_hurt.setSlot(desired_hurt_slot);
            clear_hurt_trail_after_prime =
                desired_hurt_slot == Horse::LineBatcherSlot::Persistent;
            trail_slot_changed = true;
        }
        if (m_backend_hit_once.slot() != Horse::LineBatcherSlot::Foreground)
            m_backend_hit_once.setSlot(Horse::LineBatcherSlot::Foreground);
        if (m_backend_hurt_once.slot() != Horse::LineBatcherSlot::Foreground)
            m_backend_hurt_once.setSlot(Horse::LineBatcherSlot::Foreground);
        if (trail_slot_changed)
            m_have_trail_game_frame = false;

        // Push the per-line lifetime: Persistent backends use the user-
        // configured trail length (m_trail_frames game frames at 60Hz),
        // Normal backends stick to the engine-debug default (~6 frames).
        // Re-pushed every tick so a slider drag is immediately reflected
        // on the next appended line.  setLifetime() is a single float
        // store; cheap to call unconditionally.
        {
            const float trail_seconds =
                static_cast<float>(m_trail_frames.load()) / 60.0f;
            m_backend_hit.setLifetime(
                desired_hit_slot == Horse::LineBatcherSlot::Persistent
                    ? trail_seconds
                    : Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_hurt.setLifetime(
                desired_hurt_slot == Horse::LineBatcherSlot::Persistent
                    ? trail_seconds
                    : Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_hit_once.setLifetime(
                Horse::LineBatcherBackend::kDefaultLifetime);
            m_backend_hurt_once.setLifetime(
                Horse::LineBatcherBackend::kDefaultLifetime);
        }

        m_backend_hit.primeFrom(pivot);
        m_backend_hurt.primeFrom(pivot);
        m_backend_hit_once.primeFrom(pivot);
        m_backend_hurt_once.primeFrom(pivot);

        // All KHit backends must be ready to proceed - partial readiness
        // could split active trails from one-frame inactive boxes and
        // visually misrepresent the current move state.
        if (!m_backend_hit.isReady() || !m_backend_hurt.isReady() ||
            !m_backend_hit_once.isReady() || !m_backend_hurt_once.isReady())
            return;

        if (clear_hit_trail_after_prime || clear_hurt_trail_after_prime ||
            trail_filter_changed)
        {
            if ((clear_hit_trail_after_prime || trail_filter_changed) &&
                m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hit.clearLines();
            if ((clear_hurt_trail_after_prime || trail_filter_changed) &&
                m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hurt.clearLines();
            m_have_trail_game_frame = false;
        }

        if (Horse::ResetOverride::instance().consume_trail_clear_request())
        {
            // Clears persistent lines and restarts cadence so the first
            // post-teleport engine-post tick appends a fresh trail entry.
            clear_persistent_khit_trails();
        }

        uint32_t trail_game_frame = 0;
        const bool have_trail_game_frame =
            read_lux_battle_game_frame(trail_game_frame);

        uint32_t trail_frames_elapsed = 0;
        bool append_persistent_this_tick = true;
        if (have_trail_game_frame)
        {
            if (m_have_trail_game_frame)
            {
                trail_frames_elapsed =
                    trail_game_frame - m_last_trail_game_frame;
                append_persistent_this_tick = trail_frames_elapsed != 0;
            }
            m_last_trail_game_frame = trail_game_frame;
            m_have_trail_game_frame = true;
        }
        else
        {
            m_have_trail_game_frame = false;
        }
        service_khit_sphere_audit_frame(have_trail_game_frame,
                                        trail_game_frame);

        if (trail_frames_elapsed > 0)
        {
            const float game_seconds =
                static_cast<float>(trail_frames_elapsed) / 60.0f;
            m_backend_hit.advanceLifetime(game_seconds);
            m_backend_hurt.advanceLifetime(game_seconds);
        }
        auto trim_persistent_trails = [&](int target_lines) {
            if (target_lines < 0)
                target_lines = 0;
            if (m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hit.trimOldestLines(target_lines);
            if (m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
                (void)m_backend_hurt.trimOldestLines(target_lines);
        };
        if (append_persistent_this_tick)
        {
            trim_persistent_trails(
                kKHitPersistentTrailLineBudget -
                kKHitPersistentTrailLineHeadroom);
        }

        m_backend_hit.beginFrame();
        m_backend_hurt.beginFrame();
        m_backend_hit_once.beginFrame();
        m_backend_hurt_once.beginFrame();

        const float T = m_thickness.load();
        void* slot_charas[2] = {
            Horse::KHitWalker::charaSlotFromGlobal(0),
            Horse::KHitWalker::charaSlotFromGlobal(1),
        };
        KHitRenderCalibrationFrame khit_render_calib =
            read_khit_render_calibration_frame(slot_charas);
        update_khit_render_calibration(khit_render_calib);

        Horse::KHitWalker::LaneSnapshot lane_snapshots[2] = {
            Horse::KHitWalker::readLaneSnapshot(slot_charas[0]),
            Horse::KHitWalker::readLaneSnapshot(slot_charas[1]),
        };
        std::vector<Horse::KHitDraw> khit_draws[2];
        khit_draws[0].reserve(96);
        khit_draws[1].reserve(96);

        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* slot_chara = slot_charas[pi];
            if (!slot_chara) continue;

            Horse::KHitWalker::forEachKHit(
                slot_chara,
                pi,
                [&](const Horse::KHitDraw& d) {
                    khit_draws[pi].push_back(d);
                });
        }

        mark_khit_accepted_overlap_candidates(khit_draws);
        if (khit_render_calib.applied)
            apply_render_offset_to_khit_draws(khit_draws,
                                              khit_render_calib.active_offset);

        maybe_log_khit_overlap_pairs(
            khit_draws, lane_snapshots, khit_render_calib,
            have_trail_game_frame, trail_game_frame,
            desired_hit_slot, desired_hurt_slot);
        maybe_log_khit_attack_clusters(
            khit_draws, have_trail_game_frame, trail_game_frame,
            desired_hit_slot);

        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            const int player = static_cast<int>(pi);
            const auto* audit_attacker_lane =
                &lane_snapshots[(pi == 0u) ? 1u : 0u];
            const bool show_hurt =
                shouldShow(player, Horse::KHitList::Hurtbox);
            const bool show_atk =
                shouldShow(player, Horse::KHitList::Attack);
            const bool show_body =
                shouldShow(player, Horse::KHitList::Body);

            // Snapshot the master visibility filter (see
            // m_only_show_active block).  Damage/audit truth still uses
            // canMatterThisFrame(); attack rendering uses a visual-active
            // predicate so hitboxes do not disappear just because they
            // already connected and native re-hit lockout is set.
            const bool only_active = only_active_this_frame;
            if (!show_hurt && !show_atk && !show_body) continue;

            for (const Horse::KHitDraw& d : khit_draws[pi])
            {
                const bool matters_this_frame = canMatterThisFrame(d);
                Horse::LineBatcherSlot renderer_slot =
                    Horse::LineBatcherSlot::Foreground;
                switch (d.list)
                {
                    case Horse::KHitList::Attack:
                        renderer_slot = m_backend_hit.slot();
                        break;
                    case Horse::KHitList::Hurtbox:
                    case Horse::KHitList::Body:
                        renderer_slot = m_backend_hurt.slot();
                        break;
                }
                maybe_log_khit_audit(
                    d, player, matters_this_frame,
                    have_trail_game_frame, trail_game_frame,
                    renderer_slot, audit_attacker_lane);

                // Audit is observability only. Accepted-only overlap stays
                // out of the visual damage highlight; bright red is reserved
                // for a current native hurtbox reaction/damage pulse.
                const bool hurt_damage_highlight =
                    d.list == Horse::KHitList::Hurtbox &&
                    (d.reaction_overlap_this_frame ||
                     d.raw_reaction_hot);
                const bool attack_active_display =
                    canRenderAttackShapeThisFrame(d);
                const bool visible_when_filtered =
                    matters_this_frame ||
                    attack_active_display ||
                    hurt_damage_highlight;
                switch (d.list)
                {
                    case Horse::KHitList::Hurtbox:
                        if (!show_hurt) continue;
                        if (only_active && !visible_when_filtered)
                            continue;
                        break;
                    case Horse::KHitList::Attack:
                        if (!show_atk) continue;
                        if (only_active && !visible_when_filtered)
                            continue;
                        break;
                    case Horse::KHitList::Body:
                        if (!show_body) continue;
                        break;
                }

                const Horse::FLinColor col = colourFor(d, player);
                const bool trail_sample_eligible =
                    matters_this_frame ||
                    attack_active_display ||
                    hurt_damage_highlight;
                Horse::LineBatcherBackend* trail_backend = nullptr;
                Horse::LineBatcherBackend* current_backend = nullptr;
                switch (d.list)
                {
                    case Horse::KHitList::Attack:
                        renderer_slot = m_backend_hit.slot();
                        if (renderer_slot ==
                            Horse::LineBatcherSlot::Persistent)
                        {
                            if (trail_sample_eligible &&
                                append_persistent_this_tick)
                            {
                                trail_backend = &m_backend_hit;
                            }
                            current_backend = &m_backend_hit_once;
                        }
                        else
                        {
                            current_backend = &m_backend_hit;
                        }
                        break;
                    case Horse::KHitList::Hurtbox:
                        renderer_slot = m_backend_hurt.slot();
                        if (renderer_slot ==
                            Horse::LineBatcherSlot::Persistent)
                        {
                            if (trail_sample_eligible &&
                                append_persistent_this_tick)
                            {
                                trail_backend = &m_backend_hurt;
                            }
                            current_backend = &m_backend_hurt_once;
                        }
                        else
                        {
                            current_backend = &m_backend_hurt;
                        }
                        break;
                    case Horse::KHitList::Body:
                        renderer_slot = m_backend_hurt.slot();
                        current_backend =
                            (renderer_slot ==
                                 Horse::LineBatcherSlot::Persistent)
                                ? &m_backend_hurt_once
                                : &m_backend_hurt;
                        break;
                }
                if (trail_backend)
                    Horse::DrawKHitDrawTrailSample(*trail_backend, d, col, T);
                if (current_backend)
                {
                    if (d.list == Horse::KHitList::Hurtbox)
                        Horse::DrawKHitDrawCompact(
                            *current_backend, d, col, T);
                    else
                        Horse::DrawKHitDraw(*current_backend, d, col, T);
                }
            }
        }

        trim_persistent_trails(kKHitPersistentTrailLineBudget);
        m_backend_hit.endFrame();
        m_backend_hurt.endFrame();
        m_backend_hit_once.endFrame();
        m_backend_hurt_once.endFrame();
    }

    // ------------------------------------------------------------------
    // CockpitBase_C::Update pre-hook.  Game thread, one call per frame.
    // ------------------------------------------------------------------
    void on_cockpit_update_pre(UObject* raw_cockpit)
    {
        ++m_update_calls;

        service_presence_transition_safety("cockpit");

        // Drain the ResetOverride deferred-apply queue.  Cheap no-op
        // when no reset is pending.  Must run BEFORE any other tick
        // logic so the user's captured pose is visible to camera /
        // rendering this frame.  See ResetOverride.hpp's "Deferred-
        // apply" plate for why we don't write directly from the
        // reset post-hook.
        Horse::ResetOverride::instance().tick();

        // ----------------------------------------------------------------
        // ONLINE-MATCH FEATURE GATE
        // ----------------------------------------------------------------
        // If the user's "Auto disable online" toggle is on AND we're in
        // a Ranked or Casual online match, force-disable a specific
        // subset of features that would give the user an unfair
        // perceptual / simulation-rate advantage:
        //
        //   - Lock camera position
        //   - Free-fly camera
        //   - Freeze frame
        //   - Slow motion
        //
        // We force-disable these every tick (idempotent calls - if the
        // feature is already off, the disable is a no-op) so even if
        // the user finds a way to flip the underlying atomic via some
        // other code path, the next cockpit tick clamps it back off.
        // The UI side is gated separately (see render_camera_tab and
        // render_time_tab) - both checkboxes go BeginDisabled() while
        // this predicate is true.
        //
        // NOT gated by this:
        //   - Hitbox overlay (single-player visualization, no
        //     gameplay effect)
        //   - Weapon / chara visibility, VFX suppression (local
        //     visual state, no opponent impact)
        //   - Ansel always-allowed (local photography, no opponent
        //     impact)
        //   - Reset position override (only fires on training-mode
        //     reset events the engine doesn't dispatch in matches)
        //   - Online rule overrides (the intended use case for
        //     online play - both peers opt in)
        if (Horse::GameMode::instance().should_force_disable_features())
        {
            apply_online_forced_disable();
        }

        // Apply Ansel override first - independent of the overlay F5
        // gate and the NativeBinding-ready gate below, because the user
        // asked for it to be "always" on while the toggle is held.
        apply_ansel_override_if_needed();

        // Camera lock has NO per-frame helper here - it's implemented as
        // a runtime bytepatch (Horse::CamLock) that's flipped on/off
        // from the ImGui toggle.  The patch is a property of the
        // process, not the cockpit tick.

        // VFX suppression: same bytepatch story as camera lock - the
        // toggle is a process-state property, not a per-frame action.
        // No call needed here.

        // Frame-step + freeze-frame driver.  Computes the desired
        // speedval from the (Freeze, Slow-mo, step-counter) tuple and
        // pushes it into Horse::SpeedControl.  Must run here (not from
        // the ImGui callback) because cockpit::Update ticks even while
        // SpeedControl is at 0 (UMG widget tick is independent of
        // world tick), while the ImGui tab callback only runs when the
        // user has the menu open.
        frame_step_apply();

        // Free-camera driver.  Resolves ALuxBattleCamera* from the current
        // LuxBattleManager.BattleCamera property (null outside battle)
        // and feeds it to m_free_camera.tick() which polls keyboard and
        // writes the pose fields directly on the camera actor.  Running
        // this unconditionally (not gated by m_enabled) matches the other
        // "always on while toggled" features above.
        free_camera_apply();

        if (!raw_cockpit) return;

        m_stage_visuals.tick(m_hide_stage_visuals.load());

        // KHit/stage line-overlay drawing runs from the engine tick post
        // callback, after the native battle tick has refreshed KHit world
        // buffers.  Cockpit pre-hook remains responsible for controls,
        // weapon visibility, and retrack-event HUD state.

        int charas_seen = 0;

        // ---- Weapon visibility snapshot ---------------------------------
        // Compute once per frame.  `apply_weapons` is true when we need to
        // actively push SetWeaponVisibility into the game this frame:
        //   * when the EFFECTIVE toggle is ON - re-apply every frame to
        //     overwrite any game-driven re-show (the engine can flip
        //     visibility as part of cinematic cues; we fight it back).
        //   * on the EFFECTIVE ON -> OFF transition - call once with
        //     true to restore visibility, then stop touching it.
        //
        // CONFLICT WITH "Hide characters"
        // -------------------------------
        // CharaInvis (m_hide_chara) bytepatches the engine's read of
        // chara+0x534 inside SyncMoveStateVisibility from `cmp [..],0`
        // to `cmp [..],1`.  That inverts the boolean: with the patch
        // active, flag=1 (engine "visible") reads as invisible, and
        // flag=0 (engine "invisible") reads as visible.
        //
        // SetWeaponVisibility(false) writes 0 to +0x534.  When both
        // toggles are on, the patched compare reads `0 == 1 -> visible`
        // and the weapons stay VISIBLE - opposite of what the user
        // asked for.
        //
        // Fix: when hide_chara is on, the patch ALREADY hides weapons
        // (CharaInvis patches both +0x533 chara-mesh and +0x534
        // weapon-mesh comparators).  So we suppress our own writes
        // entirely - let the engine's per-move-state writes settle the
        // flag back to 1 (its normal "visible" default) and let the
        // patch invert that to "invisible" the way it's designed to.
        //
        // The transition tracking (last_applied) still runs against
        // the EFFECTIVE state so that toggling hide_chara ON while
        // hide_weapons was previously hiding gets correctly accounted
        // for - we write `true` once on that edge to flip +0x534 back
        // to 1, which the patch then reads as invisible.  Without that
        // restore step, +0x534 would stay at 0 (our last write) and
        // the patch's "0 -> visible" inversion would briefly show the
        // weapon for the few frames before the engine's own state
        // machine writes 1 again.
        const bool hide_weapons_raw = m_hide_weapons.load();
        const bool hide_chara_now   = m_hide_chara.load();
        const bool hide_weapons_now = hide_weapons_raw && !hide_chara_now;
        const bool was_hiding       = m_last_applied_hide_weapons.load();
        const bool apply_weapons    = hide_weapons_now || was_hiding;
        // Cache the UFunction resolution once.  ALuxBattleChara's
        // SetWeaponVisibility is declared BlueprintCallable, so it's a
        // regular reflection-reachable UFunction shared across all
        // instances of the class.
        static Horse::Fn s_fn_set_weapon_vis;

        // ---- Character-mesh visibility ----------------------------------
        // Now handled by Horse::CharaInvis bytepatches (see ImGui block
        // for the toggle).  No per-frame UFunction call here - the
        // patch lives inside the engine's own visibility-getter so it
        // works invariantly across all move states without flicker.

        m_lux.forEachChara([&](int i, Horse::Obj chara) {
            if (i >= 2) return;  // only P1 / P2; ignore spectators
            ++charas_seen;
            int32_t pi = chara.getValueOr<int32_t>(L"PlayerIndex", i);
            if (pi < 0 || pi > 1) pi = i;

            // Push the weapon-visibility state for this chara.  Done
            // first so it runs even if all list toggles are off below.
            if (apply_weapons)
            {
                struct { bool bVisible; } p{ !hide_weapons_now };
                chara.callRaw(s_fn_set_weapon_vis,
                              L"SetWeaponVisibility", &p);
            }

            // ---- Retrack-event edge detection ---------------------------
            // Read chara+0x94 (facing yaw, in [0,1) normalised, 1.0=360-)
            // and chara+0x16E6 (a motion-input flag that's set during
            // most moves) every cockpit tick.  Compute per-tick yaw
            // delta against last tick's snapshot, then fire on the
            // rising edge "in-move + |delta| > threshold".
            //
            // SafeRead* wraps the dereference in __try/__except so a
            // destroyed chara during a mode-transition tick can't AV
            // the cockpit hook.  Failures default the values to 0,
            // which collapses to "no event" (safe state).
            //
            // See the field doc on m_show_retrack_events for why we're
            // measuring yaw-delta directly instead of watching gate
            // flags - short version: the original flag-pair check fired
            // on hit-fall reactions, not on what the user calls
            // "retrack events".
            {
                auto* base = reinterpret_cast<const uint8_t*>(chara.raw());

                // Wrap-aware delta on a [0,1) circular axis.  Engine-
                // produced retracks never wrap by more than a tiny
                // amount per frame, so we bring the raw delta into
                // (-0.5, +0.5] and take its magnitude.
                float yaw_now = 0.0f;
                Horse::SafeReadFloat(base + 0x94, &yaw_now);

                uint8_t in_move = 0;
                Horse::SafeReadUInt8(base + 0x16E6, &in_move);

                bool retracking_now = false;
                if (m_have_prev_yaw[pi])
                {
                    float d = yaw_now - m_prev_yaw[pi];
                    if (d >  0.5f) d -= 1.0f;
                    if (d < -0.5f) d += 1.0f;
                    if (d < 0.0f)  d  = -d;
                    retracking_now =
                        (in_move != 0) && (d > kRetrackYawThresholdNorm);
                }
                m_prev_yaw[pi]      = yaw_now;
                m_have_prev_yaw[pi] = true;

                const bool was = m_was_retracking[pi];

                // Rising edge: not-retracking ? retracking.  Only push
                // a banner if the user has the overlay enabled - keeps
                // the buffer empty (and no stale times) for users who
                // never enable it.
                if (retracking_now && !was &&
                    m_show_retrack_events.load(std::memory_order_relaxed))
                {
                    char msg[40];
                    std::snprintf(msg, sizeof(msg),
                                  "Player %d retrack event", pi + 1);
                    push_hud_text_event(msg);
                }
                m_was_retracking[pi] = retracking_now;
            }

        });

        // Commit the state we actually pushed to the game this frame.
        // Only update last-applied when we had at least one chara to push
        // to; otherwise we'd "lose" the pending transition (e.g. toggle
        // flips OFF between rounds while no chara exists - we'd never
        // get a chance to call SetWeaponVisibility(true) and weapons
        // would stay hidden).
        //
        // On next frame:
        //   * If hide_weapons_now is still true we keep re-hiding.
        //   * If it transitions true -> false we'll detect (was_hiding
        //     was true, now false) and apply once to restore.
        //   * If false -> false we skip entirely.
        if (charas_seen > 0)
        {
            m_last_applied_hide_weapons.store(hide_weapons_now);
        }
        (void)raw_cockpit;
    }

    // ------------------------------------------------------------------
    // Colour scheme (engine-role driven, not size-heuristic)
    //   Hurtboxes - green (receive volumes).  Bright red only for a
    //               current raw-frame reaction/damage candidate.  Sticky
    //               recent-hit memory is muted so accepted-only overlap
    //               cannot masquerade as damage.
    //   Attacks   - amber (strike) / magenta (throw/grab).  Hot (the
    //               currently-active cell) overrides to bright yellow for
    //               strikes or bright pink for throws so you can still see
    //               which one is live.
    //   Body/push - dim blue.  These are not involved in damage.
    // A subtle per-player hue nudge keeps P1 / P2 distinguishable when
    // they overlap visually.
    // ------------------------------------------------------------------
    static Horse::FLinColor colourFor(const Horse::KHitDraw& d, int pi)
    {
        const float player_tint = (pi == 1) ? 0.80f : 1.0f;

        switch (d.list)
        {
            case Horse::KHitList::Hurtbox:
            {
                if (d.reaction_overlap_this_frame ||
                    d.raw_reaction_hot)
                {
                    return Horse::FLinColor{ 1.0f, 0.15f, 0.15f, 1.0f };
                }
                if (d.reaction_hot)
                {
                    return Horse::FLinColor{ 0.70f * player_tint,
                                             0.20f,
                                             0.18f * player_tint, 0.45f };
                }

                // Chara-wide engine-frozen state.  Battle not
                // running, chara incapacitated / dead, or chara in
                // no-react state 6.  Resolver early-returns BEFORE
                // touching this hurtbox's slot, so it cannot fire
                // a reaction regardless of geometry / +0x14 / slot
                // index.  Show as DIM GREY ("authored, but engine
                // is frozen on this chara right now") so the
                // distinction from cyan "classifier ignores this slot"
                // cases is visible.
                //
                // Reached only when the master engine-live
                // filter is OFF - the narrow filter hides these
                // boxes by default.
                if (!d.defender_can_react_engine)
                {
                    return Horse::FLinColor{ 0.45f * player_tint,
                                             0.45f,
                                             0.45f * player_tint, 0.5f };
                }

                // Classifier-ignored hurtbox.  This box may be real
                // geometry, but its slot index is outside the current
                // classifier iteration range, so the damage resolver
                // will not read it this frame.  Many of these are
                // move-script extended-reach / meta hurtboxes, but the
                // user-facing truth is simpler: the classifier ignores
                // this slot right now.
                //
                // Colour them in CYAN tones so the user can see
                // them flip on/off across frames:
                //   bright cyan  = ignored slot AND currently on
                //                  (overlap_active == true)
                //   dim cyan     = ignored slot AND currently off
                //                  (overlap_active == false; only
                //                  visible when the engine-live filter
                //                  is OFF, since the narrow filter
                //                  would otherwise skip them)
                //
                // Detection: classifier_addressable captures `slot < cap`,
                // so `!classifier_addressable` is exactly "resolver will
                // not read this slot".
                if (!d.classifier_addressable)
                {
                    return d.overlap_active
                        ? Horse::FLinColor{ 0.30f * player_tint,
                                            0.95f,
                                            1.0f, 1.0f }   // bright cyan = live geometry, slot OOB
                        : Horse::FLinColor{ 0.20f * player_tint,
                                            0.45f,
                                            0.55f, 0.6f }; // dim cyan = +0x14 off + slot OOB
                }

                // Classifier-addressable but +0x14 == 0 - the slot
                // IS in range but the engine's overlap loop will skip
                // this node.  Full-body i-frames get a distinct purple
                // tint; otherwise render dim green for a per-slot
                // disable / armor-style window.
                if (!d.overlap_active)
                {
                    if (d.full_body_invul)
                    {
                        return Horse::FLinColor{ 0.70f * player_tint,
                                                 0.45f,
                                                 0.95f, 0.75f };
                    }
                    return Horse::FLinColor{ 0.20f * player_tint,
                                             0.50f,
                                             0.20f * player_tint, 0.5f };
                }

                // Unified green for normal classifier-addressable
                // hurtbox entries - the engine doesn't sub-
                // categorise these from the defender side.
                return Horse::FLinColor{ 0.25f * player_tint,
                                         0.95f,
                                         0.35f * player_tint, 1.0f };
            }

            case Horse::KHitList::Attack:
            {
                const bool is_throw =
                    (d.attack_role == Horse::KHitAttackRole::Throw);
                const bool visually_active =
                    canRenderAttackShapeThisFrame(d);

                if (d.reaction_overlap_this_frame)
                    return Horse::FLinColor{ 1.0f, 1.0f, 1.0f, 1.0f };

                // Throws keep the pink/magenta scheme - tier doesn't
                // apply to grabs.  Hot vs cold variants only.  When the
                // engine's throw-height gate would reject this throw
                // against the current defender (defender too tall and
                // throw's yarareId not in the unconditional allow-set
                // - see KHitDraw::throw_height_gate_ok), desaturate to
                // grey-ish to signal "boxes overlap but throw dispatch
                // will reject this defender height."  Other stance /
                // transition gates are already accounted for before a
                // throw becomes engine-live; this colour is specifically
                // the late height dispatch rule.
                if (is_throw)
                {
                    const bool gate_fail = !d.throw_height_gate_ok;
                    if (gate_fail)
                    {
                        return visually_active
                            ? Horse::FLinColor{ 0.65f, 0.50f, 0.60f, 0.85f }
                            : Horse::FLinColor{ 0.45f * player_tint,
                                                0.35f,
                                                0.40f * player_tint, 0.45f };
                    }
                    return visually_active
                        ? Horse::FLinColor{ 1.0f, 0.30f, 0.85f, 1.0f }  // hot throw = pink
                        : Horse::FLinColor{ 0.85f * player_tint,
                                            0.15f,
                                            0.70f * player_tint, 0.6f }; // cold throw = magenta
                }

                // Strikes - colour by AttackFlags tier (engine-truth
                // classification of high/mid/low/unblockable from
                // cell+0x32 read in EvaluateMoveTransition + ProcessHit).
                // The data has been on every KHitDraw since the
                // 2026-05-15 audit; this routes it into rendering.
                //
                //   High        - red-orange  (must block standing)
                //   Mid         - amber       (blockable any stance)
                //   Low         - sky-blue    (must block crouching)
                //   Unblockable - magenta-red (must dodge - GI-immune
                //                              when bit 0x200 is set)
                //   Special     - light cyan  (special framing rule)
                //   Unknown     - amber       (fallback to legacy)
                //
                // Hot (live this frame) variants are full saturation;
                // cold variants are tinted by player_tint with 0.6 alpha.
                const Horse::KHitAttackTier tier = d.attack_tier;
                if (visually_active)
                {
                    // Hot strike - pop out from other strikes.
                    switch (tier)
                    {
                        case Horse::KHitAttackTier::High:
                            return Horse::FLinColor{ 1.0f, 0.40f, 0.15f, 1.0f };
                        case Horse::KHitAttackTier::Low:
                            return Horse::FLinColor{ 0.35f, 0.75f, 1.0f, 1.0f };
                        case Horse::KHitAttackTier::Unblockable:
                            return Horse::FLinColor{ 1.0f, 0.10f, 0.55f, 1.0f };
                        case Horse::KHitAttackTier::Special:
                            return Horse::FLinColor{ 0.55f, 1.0f, 0.95f, 1.0f };
                        case Horse::KHitAttackTier::Mid:
                        case Horse::KHitAttackTier::Unknown:
                        default:
                            return Horse::FLinColor{ 1.0f, 1.0f, 0.25f, 1.0f };
                    }
                }
                // Cold strike.
                switch (tier)
                {
                    case Horse::KHitAttackTier::High:
                        return Horse::FLinColor{ 0.95f * player_tint,
                                                 0.30f,
                                                 0.10f, 0.6f };
                    case Horse::KHitAttackTier::Low:
                        return Horse::FLinColor{ 0.20f * player_tint,
                                                 0.55f * player_tint,
                                                 0.85f, 0.6f };
                    case Horse::KHitAttackTier::Unblockable:
                        return Horse::FLinColor{ 0.85f * player_tint,
                                                 0.10f,
                                                 0.45f * player_tint, 0.6f };
                    case Horse::KHitAttackTier::Special:
                        return Horse::FLinColor{ 0.40f * player_tint,
                                                 0.80f * player_tint,
                                                 0.80f, 0.6f };
                    case Horse::KHitAttackTier::Mid:
                    case Horse::KHitAttackTier::Unknown:
                    default:
                        return Horse::FLinColor{ 1.0f * player_tint,
                                                 0.55f * player_tint,
                                                 0.10f, 0.6f };  // legacy amber
                }
            }

            case Horse::KHitList::Body:
            default:
                return Horse::FLinColor{ 0.25f,
                                         0.45f * player_tint,
                                         1.0f * player_tint, 0.5f };
        }
    }

    // ------------------------------------------------------------------
    // Online-gate UI helpers
    // ------------------------------------------------------------------
    // Centralised look-up of the colour + tooltip text used by both the
    // title-bar status indicator and the "Auto disable online" status
    // line at the top of the General tab.  Returning by value keeps the
    // call sites free of the four-way state switch they used to inline.
    //
    // Bundled into one helper so colour and tooltip can never drift out
    // of sync (the previous header banner had label/colour twinned in
    // separate switch branches and we'd hit the same drift if we left
    // each call site to compute its own state).
    struct OnlineStatusUI
    {
        ImVec4      colour;
        const char* short_label;   // 1-line, used by general-tab status row
        const char* tooltip_body;  // multi-line, used by both indicator + status row
    };

    static OnlineStatusUI compute_online_status_ui()
    {
        using GMP = Horse::GamePresence;
        auto& gm = Horse::GameMode::instance();
        const GMP  p          = gm.current_presence();
        const bool gating_on  = gm.auto_disable_online();
        const bool forced     = gm.should_force_disable_features();

        OnlineStatusUI s;
        if (!gating_on)
        {
            s.colour       = ImVec4{0.65f, 0.65f, 0.65f, 1.0f};
            s.short_label  = "Auto-disable OFF";
            s.tooltip_body =
                "Auto disable online: OFF. All features available.";
        }
        else if (p == GMP::Unknown)
        {
            s.colour       = ImVec4{0.95f, 0.85f, 0.20f, 1.0f};
            s.short_label  = "Presence unknown";
            s.tooltip_body =
                "Auto disable online: ON. Scene presence not yet "
                "observed; gate inactive.";
        }
        else if (forced)
        {
            s.colour       = ImVec4{1.00f, 0.30f, 0.30f, 1.0f};
            s.short_label  = "Online match - features locked";
            s.tooltip_body =
                "Auto disable online: ON. In a Ranked/Casual match - "
                "Lock-cam, Free-fly, Freeze, Slow-mo are locked off.";
        }
        else
        {
            s.colour       = ImVec4{0.30f, 0.90f, 0.40f, 1.0f};
            s.short_label  = "All features available";
            s.tooltip_body =
                "Auto disable online: ON. Scene safe - all features "
                "available.";
        }
        return s;
    }

    // Draws a small colored square in the active window's title bar,
    // positioned just to the right of the title text.  Hover over the
    // square shows the current online-gate status as a tooltip.
    //
    // Why ForegroundDrawList: the title bar is rendered by ImGui after
    // user content for this window, so a normal window-draw-list
    // submission can be overdrawn by the title bar.  Using the
    // foreground draw list guarantees our square sits ON TOP of the
    // title bar at all times.
    //
    // Tooltip uses IsMouseHoveringRect because raw ImDrawList primitives
    // bypass ImGui's input-claim path; the normal IsItemHovered() flow
    // doesn't apply to ad-hoc draw calls.
    static void draw_title_bar_status_indicator()
    {
        const OnlineStatusUI s = compute_online_status_ui();

        const ImVec2 wpos      = ImGui::GetWindowPos();
        const float  frame_h   = ImGui::GetFrameHeight();
        // Square sized relative to the title bar height so it scales
        // nicely on different DPI / font configurations.
        const float  sq_size   = frame_h * 0.55f;
        const float  pad_x     = 6.0f;
        // Rough left padding before the title text starts: collapse-
        // arrow (~frame_h) + a bit of breathing room.  Then we add the
        // title-text width to land the square just past the title.
        const float  title_w   = ImGui::CalcTextSize(horsemod_window_title()).x;
        const float  square_x  = wpos.x + frame_h + 4.0f + title_w + pad_x;
        const float  square_y  = wpos.y + (frame_h - sq_size) * 0.5f;

        const ImVec2 sq_min{square_x, square_y};
        const ImVec2 sq_max{square_x + sq_size, square_y + sq_size};

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(sq_min, sq_max, ImGui::GetColorU32(s.colour), 2.0f);
        // Thin black outline so the square is visible against any
        // title-bar background colour theme.
        dl->AddRect(sq_min, sq_max, IM_COL32(0, 0, 0, 200), 2.0f, 0, 1.0f);

        // Tooltip on hover - manual hit-test since the square isn't an
        // ImGui item.  Slight padding around the rect so the user
        // doesn't have to be pixel-precise.
        const ImVec2 hover_min{sq_min.x - 2.0f, sq_min.y - 2.0f};
        const ImVec2 hover_max{sq_max.x + 2.0f, sq_max.y + 2.0f};
        if (ImGui::IsMouseHoveringRect(hover_min, hover_max, /*clip=*/false))
        {
            ImGui::SetTooltip("%s", s.tooltip_body);
        }
    }

    // After rendering a force-disabled checkbox / slider, draw a
    // horizontal line across its label area so the user has a strong
    // visual cue ("crossed out") in addition to ImGui's normal
    // BeginDisabled greying.  Call AFTER EndDisabled and BEFORE the
    // next item submission so GetItemRectMin/Max still references the
    // checkbox we just drew.
    //
    // The line skips past the leading frame-height square (the
    // checkbox's tickbox) so the strikethrough visually crosses only
    // the label text, leaving the box itself unobscured.
    static void draw_disabled_strikethrough()
    {
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 rmax = ImGui::GetItemRectMax();
        const float y     = (rmin.y + rmax.y) * 0.5f;
        const float x0    = rmin.x + ImGui::GetFrameHeight() + 4.0f;
        const float x1    = rmax.x;
        // Use the disabled-text colour so the line tracks ImGui's theme
        // (light themes get a darker line, dark themes a lighter one).
        const ImU32 col   = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(x0, y), ImVec2(x1, y), col, 1.5f);
    }

    // Walk m_hud_text_events[] and draw every entry that's still within
    // its lifetime onto the foreground draw list, fading alpha linearly
    // from 100% at fire-time to 0% at lifetime expiry.  Stacks the most
    // recent event at the top and grows downward - newer entries hide
    // older ones if more fired in a short burst, which is the right
    // visual cue (the latest matters more).
    //
    // Drawing is FOREGROUND so the lines appear above both the game
    // and any ImGui windows.  Costs one std::array sweep + at most
    // kHudTextEventCount AddText calls per frame regardless of what's
    // happening on screen - cheap.
    //
    // The buffer is the shared overlay queue for arbitrary on-screen
    // text events (retrack-event detector, "Hello World" test button,
    // future C++-side diagnostic banners).  We don't gate on the
    // retrack toggle here because the queue is generic - gating
    // happens at the push site (only retrack pushes are gated by
    // m_show_retrack_events).
