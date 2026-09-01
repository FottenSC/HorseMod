    void maybe_log_khit_overlap_pairs(
        const std::vector<Horse::KHitDraw> (&draws)[2],
        const Horse::KHitWalker::LaneSnapshot (&lane_snapshots)[2],
        const KHitRenderCalibrationFrame& render_calib,
        bool have_game_frame,
        uint32_t game_frame,
        Horse::LineBatcherSlot hit_renderer_slot,
        Horse::LineBatcherSlot hurt_renderer_slot)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
            return;

        for (int defender = 0; defender < 2; ++defender)
        {
            const int attacker = (defender == 0) ? 1 : 0;
            const auto* attacker_lane = &lane_snapshots[attacker];
            const bool attacker_has_move =
                attacker_lane && attacker_lane->has_move;
            const int attacker_packed = attacker_has_move
                ? static_cast<int>(attacker_lane->packed_move)
                : -1;
            const int attacker_low11 = attacker_has_move
                ? (attacker_packed & 0x7ff)
                : -1;

            for (const Horse::KHitDraw& hurt : draws[defender])
            {
                if (hurt.list != Horse::KHitList::Hurtbox ||
                    !hurt.defender_hurtbox_mask_valid ||
                    hurt.defender_hurtbox_attack_mask == 0)
                {
                    continue;
                }
                const bool move_filter_match =
                    khit_audit_matches_move_filter(hurt, attacker_lane);

                for (const Horse::KHitDraw& attack : draws[attacker])
                {
                    if (attack.list != Horse::KHitList::Attack)
                        continue;

                    const uint64_t matched_bits =
                        hurt.defender_hurtbox_attack_mask &
                        attack.slot_bit_mask;
                    if (matched_bits == 0)
                        continue;
                    if (!attack.geom_active)
                        continue;
                    if (!khit_pair_geometry_plausible(attack, hurt))
                        continue;

                    if (m_khit_sphere_audit_filter_slots.load(
                            std::memory_order_relaxed) &&
                        !khit_audit_matches_slot_filter(attack) &&
                        !khit_audit_matches_slot_filter(hurt))
                    {
                        continue;
                    }
                    if (!consume_khit_audit_log_slot(
                            KHitAuditLogBucket::OverlapPair))
                        return;

                    const bool atk_matters = canMatterThisFrame(attack);
                    const bool hurt_matters =
                        canMatterThisFrame(hurt) ||
                        hurt.raw_reaction_state != 0;
                    const bool reaction_candidate =
                        (hurt.reaction_overlap_matched_bits &
                         attack.slot_bit_mask) != 0;
                    const bool exact_geometry =
                        khit_pair_has_exact_geometry(attack, hurt);
                    const bool accepted_exact_pair =
                        (hurt.accepted_exact_overlap_matched_bits &
                         attack.slot_bit_mask) != 0;
                    const bool accepted_only =
                        accepted_exact_pair && !reaction_candidate;

                    const KHitAuditShapeMetrics atk_m =
                        audit_shape_metrics(attack);
                    const KHitAuditShapeMetrics hurt_m =
                        audit_shape_metrics(hurt);
                    const float native_dist =
                        distance3(atk_m.native_center,
                                  hurt_m.native_center);
                    const float ue_dist =
                        distance3(atk_m.ue_center,
                                  hurt_m.ue_center);
                    const float native_rsum =
                        atk_m.native_radius + hurt_m.native_radius;
                    const float ue_rsum =
                        atk_m.ue_radius + hurt_m.ue_radius;
                    const KHitAuditCharaPose atk_pose =
                        read_khit_audit_chara_pose(
                            Horse::KHitWalker::charaSlotFromGlobal(
                                static_cast<uint32_t>(attacker)));
                    const KHitAuditCharaPose def_pose =
                        read_khit_audit_chara_pose(
                            Horse::KHitWalker::charaSlotFromGlobal(
                                static_cast<uint32_t>(defender)));

                    Horse::FVec3 native_contact = midpoint(
                        atk_m.native_center, hurt_m.native_center);
                    bool native_contact_valid = false;
                    Horse::FVec3 native_contact_dir{};
                    if (normalize3(sub3(hurt_m.native_center,
                                        atk_m.native_center),
                                   native_contact_dir))
                    {
                        const Horse::FVec3 attack_shell =
                            add3(atk_m.native_center,
                                 scale3(native_contact_dir,
                                        atk_m.native_radius));
                        const Horse::FVec3 hurt_shell =
                            sub3(hurt_m.native_center,
                                 scale3(native_contact_dir,
                                        hurt_m.native_radius));
                        native_contact = midpoint(attack_shell, hurt_shell);
                        native_contact_valid = true;
                    }
                    const Horse::FVec3 ue_contact =
                        audit_battle_to_ue_render_world(native_contact);

                    Horse::FVec3 attacker_to_defender_axis{};
                    bool attacker_to_defender_axis_valid = false;
                    if (atk_pose.ok && def_pose.ok)
                    {
                        Horse::FVec3 flat_delta =
                            sub3(def_pose.native_pos, atk_pose.native_pos);
                        flat_delta.Y = 0.0f;
                        attacker_to_defender_axis_valid =
                            normalize3(flat_delta,
                                       attacker_to_defender_axis);
                    }
                    const Horse::FVec3 attacker_right_axis{
                        attacker_to_defender_axis.Z,
                        0.0f,
                        -attacker_to_defender_axis.X
                    };
                    auto signed_forward = [&](const Horse::FVec3& p) {
                        return attacker_to_defender_axis_valid
                            ? dot3(sub3(p, atk_pose.native_pos),
                                   attacker_to_defender_axis)
                            : 0.0f;
                    };
                    auto signed_right = [&](const Horse::FVec3& p) {
                        return attacker_to_defender_axis_valid
                            ? dot3(sub3(p, atk_pose.native_pos),
                                   attacker_right_axis)
                            : 0.0f;
                    };
                    auto signed_up = [&](const Horse::FVec3& p) {
                        return atk_pose.ok ? (p.Y - atk_pose.native_pos.Y)
                                           : 0.0f;
                    };

                    Output::send<LogLevel::Default>(
                        STR("[HorseMod.KHitAudit.OverlapPair] frame_ok={} "
                            "frame={} def_p={} atk_p={} "
                            "atk_move=0x{:04x}/{} "
                            "hurt_node=0x{:x} hurt_kind={} hurt_slot={} "
                            "atk_node=0x{:x} atk_kind={} atk_slot={} "
                            "atk_slot_bit=0x{:016x} matched=0x{:016x} "
                            "incoming=0x{:016x} move_filter_match={} "
                            "raw_react={} sticky_react={} final={} "
                            "atk_phase={} atk_geom={} atk_mask_selected={} "
                            "atk_active_move_valid={} atk_mask_stale={} "
                            "atk_matters={} hurt_matters={} "
                            "reaction_candidate={} reaction_pair_count={} "
                            "exact_geometry={} accepted_exact={} "
                            "accepted_only={} accepted_ambiguous={} "
                            "reaction_ambiguous={} "
                            "renderer_hit={} renderer_hurt={} "
                            "atk_local=({:.3f},{:.3f},{:.3f}) "
                            "native_atk=({:.3f},{:.3f},{:.3f}) "
                            "native_hurt=({:.3f},{:.3f},{:.3f}) "
                            "native_dist={:.3f} native_rsum={:.3f} "
                            "native_margin={:.3f} "
                            "ue_atk=({:.1f},{:.1f},{:.1f}) "
                            "ue_hurt=({:.1f},{:.1f},{:.1f}) "
                            "ue_dist={:.1f} ue_rsum={:.1f} "
                            "ue_margin={:.1f} "
                            "atk_radius_native={:.3f} "
                            "hurt_radius_native={:.3f} "
                            "atk_radius_ue={:.1f} hurt_radius_ue={:.1f}\n"),
                        have_game_frame,
                        have_game_frame ? game_frame : 0,
                        defender + 1,
                        attacker + 1,
                        attacker_has_move
                            ? static_cast<unsigned>(attacker_packed & 0xffff)
                            : 0xffffu,
                        attacker_low11,
                        hurt.source_node,
                        static_cast<int>(hurt.kind),
                        hurt.hurtbox_slot,
                        attack.source_node,
                        static_cast<int>(attack.kind),
                        static_cast<int>(attack.bone_id_internal),
                        attack.slot_bit_mask,
                        matched_bits,
                        hurt.defender_hurtbox_attack_mask,
                        move_filter_match,
                        hurt.raw_reaction_state,
                        hurt.reaction_state,
                        hurt.final_hit_result_code,
                        static_cast<int>(attack.engine_phase),
                        attack.geom_active,
                        attack.attack_mask_selected,
                        attack.active_move_valid,
                        attack.attack_mask_stale,
                        atk_matters,
                        hurt_matters,
                        reaction_candidate,
                        hurt.reaction_overlap_pair_count,
                        exact_geometry,
                        accepted_exact_pair,
                        accepted_only,
                        attack.accepted_overlap_ambiguous ||
                            hurt.accepted_overlap_ambiguous,
                        attack.reaction_overlap_ambiguous ||
                            hurt.reaction_overlap_ambiguous,
                        static_cast<int>(hit_renderer_slot),
                        static_cast<int>(hurt_renderer_slot),
                        attack.native_live_local_centre.X,
                        attack.native_live_local_centre.Y,
                        attack.native_live_local_centre.Z,
                        atk_m.native_center.X,
                        atk_m.native_center.Y,
                        atk_m.native_center.Z,
                        hurt_m.native_center.X,
                        hurt_m.native_center.Y,
                        hurt_m.native_center.Z,
                        native_dist,
                        native_rsum,
                        native_rsum - native_dist,
                        atk_m.ue_center.X,
                        atk_m.ue_center.Y,
                        atk_m.ue_center.Z,
                        hurt_m.ue_center.X,
                        hurt_m.ue_center.Y,
                        hurt_m.ue_center.Z,
                        ue_dist,
                        ue_rsum,
                        ue_rsum - ue_dist,
                        atk_m.native_radius,
                        hurt_m.native_radius,
                        atk_m.ue_radius,
                        hurt_m.ue_radius);

                    Output::send<LogLevel::Default>(
                        STR("[HorseMod.KHitAudit.OverlapSpace] frame_ok={} "
                            "frame={} def_p={} atk_p={} "
                            "atk_node=0x{:x} hurt_node=0x{:x} "
                            "atk_pose_ok={} def_pose_ok={} "
                            "atk_pos=({:.3f},{:.3f},{:.3f}) "
                            "def_pos=({:.3f},{:.3f},{:.3f}) "
                            "atk_slot_byte={} def_slot_byte={} "
                            "atk_opp_dist_ok={} atk_opp_dist={:.3f} "
                            "axis_ok={} atk_to_def_axis=({:.3f},{:.3f},{:.3f}) "
                            "contact_ok={} native_contact=({:.3f},{:.3f},{:.3f}) "
                            "ue_contact=({:.1f},{:.1f},{:.1f}) "
                            "atk_center_rel=({:.3f},{:.3f},{:.3f}) "
                            "contact_rel=({:.3f},{:.3f},{:.3f}) "
                            "hurt_center_rel=({:.3f},{:.3f},{:.3f}) "
                            "atk_per_frame={} atk_can_strike={} atk_matters={} "
                            "hurt_addressable={} hurt_overlap={} "
                            "hurt_can_react={} hurt_matters={}\n"),
                        have_game_frame,
                        have_game_frame ? game_frame : 0,
                        defender + 1,
                        attacker + 1,
                        attack.source_node,
                        hurt.source_node,
                        atk_pose.ok,
                        def_pose.ok,
                        atk_pose.native_pos.X,
                        atk_pose.native_pos.Y,
                        atk_pose.native_pos.Z,
                        def_pose.native_pos.X,
                        def_pose.native_pos.Y,
                        def_pose.native_pos.Z,
                        static_cast<unsigned>(atk_pose.slot_byte),
                        static_cast<unsigned>(def_pose.slot_byte),
                        atk_pose.distance_ok,
                        atk_pose.opponent_distance,
                        attacker_to_defender_axis_valid,
                        attacker_to_defender_axis.X,
                        attacker_to_defender_axis.Y,
                        attacker_to_defender_axis.Z,
                        native_contact_valid,
                        native_contact.X,
                        native_contact.Y,
                        native_contact.Z,
                        ue_contact.X,
                        ue_contact.Y,
                        ue_contact.Z,
                        signed_forward(atk_m.native_center),
                        signed_right(atk_m.native_center),
                        signed_up(atk_m.native_center),
                        signed_forward(native_contact),
                        signed_right(native_contact),
                        signed_up(native_contact),
                        signed_forward(hurt_m.native_center),
                        signed_right(hurt_m.native_center),
                        signed_up(hurt_m.native_center),
                        attack.is_per_frame_active,
                        attack.attacker_can_strike_engine,
                        canMatterThisFrame(attack),
                        hurt.classifier_addressable,
                        hurt.overlap_active,
                        hurt.defender_can_react_engine,
                        canMatterThisFrame(hurt));

                    if ((hurt.raw_reaction_state != 0 ||
                         reaction_candidate) &&
                        consume_khit_audit_log_slot(
                            KHitAuditLogBucket::Calibration))
                    {
                        const KHitRenderCalibrationPoint& atk_cal =
                            render_calib.point[attacker];
                        const KHitRenderCalibrationPoint& def_cal =
                            render_calib.point[defender];
                        const Horse::FVec3 candidate_ue =
                            midpoint(atk_m.ue_center, hurt_m.ue_center);
                        const Horse::FVec3 draw_contact_ue =
                            add3(ue_contact, render_calib.active_offset);

                        Output::send<LogLevel::Default>(
                            STR("[HorseMod.KHitRenderCalib] frame_ok={} "
                                "frame={} status={} applied={} samples={} "
                                "consistent={} delta_dist={:.1f} "
                                "offset=({:.1f},{:.1f},{:.1f}) "
                                "def_p={} atk_p={} atk_node=0x{:x} "
                                "hurt_node=0x{:x} reaction_candidate={} "
                                "raw_react={} final={} "
                                "atk_native_root=({:.3f},{:.3f},{:.3f}) "
                                "atk_root_ue=({:.1f},{:.1f},{:.1f}) "
                                "atk_actor_ue=({:.1f},{:.1f},{:.1f}) "
                                "atk_delta=({:.1f},{:.1f},{:.1f}) "
                                "def_native_root=({:.3f},{:.3f},{:.3f}) "
                                "def_root_ue=({:.1f},{:.1f},{:.1f}) "
                                "def_actor_ue=({:.1f},{:.1f},{:.1f}) "
                                "def_delta=({:.1f},{:.1f},{:.1f}) "
                                "candidate_ue=({:.1f},{:.1f},{:.1f}) "
                                "native_contact=({:.3f},{:.3f},{:.3f}) "
                                "converted_contact=({:.1f},{:.1f},{:.1f}) "
                                "draw_contact=({:.1f},{:.1f},{:.1f})\n"),
                            have_game_frame,
                            have_game_frame ? game_frame : 0,
                            render_calib.status,
                            render_calib.applied,
                            render_calib.samples,
                            render_calib.consistent,
                            render_calib.delta_distance,
                            render_calib.active_offset.X,
                            render_calib.active_offset.Y,
                            render_calib.active_offset.Z,
                            defender + 1,
                            attacker + 1,
                            attack.source_node,
                            hurt.source_node,
                            reaction_candidate,
                            hurt.raw_reaction_state,
                            hurt.final_hit_result_code,
                            atk_cal.native_root.X,
                            atk_cal.native_root.Y,
                            atk_cal.native_root.Z,
                            atk_cal.converted_root.X,
                            atk_cal.converted_root.Y,
                            atk_cal.converted_root.Z,
                            atk_cal.actor_root.X,
                            atk_cal.actor_root.Y,
                            atk_cal.actor_root.Z,
                            atk_cal.delta.X,
                            atk_cal.delta.Y,
                            atk_cal.delta.Z,
                            def_cal.native_root.X,
                            def_cal.native_root.Y,
                            def_cal.native_root.Z,
                            def_cal.converted_root.X,
                            def_cal.converted_root.Y,
                            def_cal.converted_root.Z,
                            def_cal.actor_root.X,
                            def_cal.actor_root.Y,
                            def_cal.actor_root.Z,
                            def_cal.delta.X,
                            def_cal.delta.Y,
                            def_cal.delta.Z,
                            candidate_ue.X,
                            candidate_ue.Y,
                            candidate_ue.Z,
                            native_contact.X,
                            native_contact.Y,
                            native_contact.Z,
                            ue_contact.X,
                            ue_contact.Y,
                            ue_contact.Z,
                            draw_contact_ue.X,
                            draw_contact_ue.Y,
                            draw_contact_ue.Z);
                    }
                }
            }
        }
    }

    void clear_persistent_khit_trails()
    {
        if (m_backend_hit.slot() == Horse::LineBatcherSlot::Persistent)
            (void)m_backend_hit.clearLines();
        if (m_backend_hurt.slot() == Horse::LineBatcherSlot::Persistent)
            (void)m_backend_hurt.clearLines();
        m_have_trail_game_frame = false;
    }

    void hide_khit_overlay_lines()
    {
        m_backend_hit.hideAll();
        m_backend_hurt.hideAll();
        m_backend_hit_once.hideAll();
        m_backend_hurt_once.hideAll();
        m_khit_render_calibration = {};
        m_have_trail_game_frame = false;
    }

    // (Secondary attack-role filter / shouldShowAttackRole was removed
    // 2026-04 along with the UI for it.  Strike vs Throw partitioning
    // turned out to be more noise than signal for practical hitbox
    // inspection - users just want "show all attack volumes" or
    // "show none," which the master Attacks per-player checkbox
    // already covers.  If you want them back, the engine split is
    // documented at KHitAttackRole + the classifier at
    // LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100.)

    // ==================================================================
    // Settings persistence - file-backed via Horse::ModSettings.
    // ==================================================================
    //
    // Load: call once from the ctor BEFORE any render path reads an
    // atomic.  Reads <mod_folder>/settings.cfg, populates each atomic
    // from its persisted value, falls back to the compiled-in default
    // argument when the key is missing (fresh install, or we added a
    // new setting after the file was written).
    //
    // Save: sync every persisted atomic back into the ModSettings
    // map, then ModSettings::save_if_dirty() does the actual disk
    // write (only if something changed since the last save).  Called
    // periodically from on_update (every ~120 frames - 2s at 60 FPS)
    // so slider drags don't spam the disk, and once more from the
    // dtor so the final state lands on disk on graceful shutdown.
    //
    // What we DON'T persist: runtime state (m_update_calls, hook
    // bookkeeping), transient toggles (m_freeze_frame, m_step_pending,
    // overlay visibility - user wants overlay hidden on launch
    // regardless), diagnostic-only flags.
    //
    // Key-naming convention: snake_case, descriptive over short, no
    // prefix.  Old/renamed settings are safe to leave in the file -
    // ModSettings preserves unknown keys across saves.
    void load_persisted_settings()
    {
        auto& S = Horse::ModSettings::instance();
        S.load();

        // --- Hitboxes tab -----------------------------------------
        m_enabled                .store(S.get_bool ("master_overlay",        false));
        m_show_p1_hurt           .store(S.get_bool ("show_p1_hurt",          false));
        m_show_p1_atk            .store(S.get_bool ("show_p1_hitboxes",      true ));
        m_show_p1_body           .store(S.get_bool ("show_p1_body",          false));
        m_show_p2_hurt           .store(S.get_bool ("show_p2_hurt",          true ));
        m_show_p2_atk            .store(S.get_bool ("show_p2_hitboxes",      true ));
        m_show_p2_body           .store(S.get_bool ("show_p2_body",          false));
        // Box-visibility filter triple (see m_only_show_active block).
        // Default: master narrow ON, both per-list overrides OFF - gives
        // the engine-truth "what's hitting RIGHT NOW" view on first
        // launch.  The legacy keys (`damage_active_only`,
        // `show_unused_hurtboxes`) are silently dropped; users who had
        // them set to non-default values will land on the new defaults.
        m_only_show_active       .store(S.get_bool ("only_show_active",     true ));
        m_flash_frames           .store(S.get_int  ("hit_flash_frames",      15   ));
        m_thickness              .store(S.get_float("thickness",             1.5f ));
        // Per-feature line-batcher slot.  Hitboxes default to Foreground
        // (always-on-top, the only sensible choice - Persistent would
        // pile up unreadable trails).  Hurtboxes also default to
        // Foreground but the user can flip them to Persistent to trace
        // a chara's hurtbox path through a move.  The legacy single
        // key "line_batcher_slot" from before the split is silently
        // ignored - old enum values aren't valid in the new 2-entry
        // enum and the user has to pick again from the new combos.
        m_slot_hit .store(static_cast<Horse::LineBatcherSlot>(
            S.get_int("line_batcher_slot_hit",
                      static_cast<int>(Horse::LineBatcherSlot::Foreground))));
        m_slot_hurt.store(static_cast<Horse::LineBatcherSlot>(
            S.get_int("line_batcher_slot_hurt",
                      static_cast<int>(Horse::LineBatcherSlot::Foreground))));
        m_trail_frames           .store(S.get_int  ("persistent_trail_frames", 30   ));

        // --- Camera tab -------------------------------------------
        m_ansel_always_allowed   .store(S.get_bool ("ansel_always_allowed",  true ));
        m_lock_camera            .store(S.get_bool ("lock_camera",           false));
        m_free_camera.move_speed() = S.get_float("free_camera_move_speed", 20.0f);
        m_free_camera.look_speed() = S.get_float("free_camera_look_speed",  1.5f);
        m_free_camera.fov_deg()    = S.get_float("free_camera_fov",        70.0f);

        // --- Time tab ---------------------------------------------
        m_speed_enabled          .store(S.get_bool ("slow_motion_enabled",   false));
        m_speed_value            .store(S.get_float("slow_motion_value",     1.0f ));

        // --- General tab ------------------------------------------
        m_hide_weapons           .store(S.get_bool ("hide_weapons",          false));
        m_hide_chara             .store(S.get_bool ("hide_characters",       false));
        m_suppress_vfx           .store(S.get_bool ("suppress_vfx",          false));
        m_show_stage_boundary    .store(S.get_bool ("show_stage_boundary",   false));
        m_hide_stage_visuals     .store(S.get_bool ("hide_stage_visuals",    false));
        m_show_retrack_events    .store(S.get_bool ("show_retrack_events",   false));

        // --- Reset override -----------------------------------------
        // Captured pose persists across reboots so the user can resume
        // training from the same custom starting position.  The toggle
        // itself does NOT persist - it's deliberately reset to OFF on
        // every game start so a stale capture from a previous session
        // can't surprise the user with an unexpected teleport on the
        // first reset bind they press.  The user has to consciously
        // re-enable it to opt in.
        {
            auto& ro = Horse::ResetOverride::instance();
            ro.set_enabled(false);
            for (int pi = 0; pi < 2; ++pi)
            {
                Horse::ResetOverride::FCharaPose p{};
                std::string base = "reset_override_p";
                base += static_cast<char>('1' + pi);
                p.has = S.get_bool((base + "_has").c_str(), false);
                if (!p.has) continue;
                p.pos_x     = S.get_float((base + "_x").c_str(),    0.0f);
                p.pos_y     = S.get_float((base + "_y").c_str(),    0.0f);
                p.pos_z     = S.get_float((base + "_z").c_str(),    0.0f);
                ro.set_pose(pi, p);
            }
        }

        // Persisted HorseMod online policy.  Defaults to Vanilla so a
        // first-launch user with the mod installed gets vanilla
        // multiplayer behaviour; they have to consciously pick a
        // policy from the Online section in the General tab.
        Horse::OnlineRules::instance().set_policy(
            static_cast<Horse::HorsePolicy>(
                S.get_int("online_policy",
                    static_cast<int>(Horse::HorsePolicy::Vanilla))));

        // GameMode "Auto disable online" toggle.  Default ON.
        // Persists so a user who deliberately turns it off doesn't
        // have to re-disable on every launch.  See
        // horselib/GameMode.hpp for the full rationale.
        Horse::GameMode::instance().set_auto_disable_online(
            S.get_bool("gamemode_auto_disable_online", true));
    }

    // Mirror every persisted atomic into the ModSettings map, then ask
    // ModSettings to write the file if anything changed since the
    // last save.  Set() calls diff internally, so idempotent calls on
    // unchanged values are O(map-lookup) and don't touch the dirty
    // flag - cheap to call every on_update tick.
    void save_persisted_settings()
    {
        auto& S = Horse::ModSettings::instance();

        // Hitboxes tab
        S.set("master_overlay",        m_enabled.load());
        S.set("show_p1_hurt",          m_show_p1_hurt.load());
        S.set("show_p1_hitboxes",      m_show_p1_atk.load());
        S.set("show_p1_body",          m_show_p1_body.load());
        S.set("show_p2_hurt",          m_show_p2_hurt.load());
        S.set("show_p2_hitboxes",      m_show_p2_atk.load());
        S.set("show_p2_body",          m_show_p2_body.load());
        S.set("only_show_active",      m_only_show_active.load());
        S.set("hit_flash_frames",      m_flash_frames.load());
        S.set("thickness",             m_thickness.load());
        S.set("line_batcher_slot_hit",  static_cast<int>(m_slot_hit.load()));
        S.set("line_batcher_slot_hurt", static_cast<int>(m_slot_hurt.load()));
        S.set("persistent_trail_frames", m_trail_frames.load());

        // Camera tab
        S.set("ansel_always_allowed",  m_ansel_always_allowed.load());
        S.set("lock_camera",           m_lock_camera.load());
        S.set("free_camera_move_speed", m_free_camera.move_speed());
        S.set("free_camera_look_speed", m_free_camera.look_speed());
        S.set("free_camera_fov",       m_free_camera.fov_deg());

        // Time tab
        S.set("slow_motion_enabled",   m_speed_enabled.load());
        S.set("slow_motion_value",     m_speed_value.load());

        // General tab
        S.set("hide_weapons",          m_hide_weapons.load());
        S.set("hide_characters",       m_hide_chara.load());
        S.set("suppress_vfx",          m_suppress_vfx.load());
        S.set("show_stage_boundary",   m_show_stage_boundary.load());
        S.set("hide_stage_visuals",    m_hide_stage_visuals.load());
        S.set("show_retrack_events",   m_show_retrack_events.load());

        // --- Reset override ----------------------------------------
        // The toggle is deliberately NOT persisted - see the matching
        // load_persisted_settings block for the rationale (start each
        // session with the override OFF; user must opt in).  We still
        // persist the captured pose so a previously-set custom spawn
        // is one click away.
        {
            auto& ro = Horse::ResetOverride::instance();
            for (int pi = 0; pi < 2; ++pi)
            {
                const auto p = ro.get_pose(pi);
                std::string base = "reset_override_p";
                base += static_cast<char>('1' + pi);
                S.set((base + "_has").c_str(),  p.has);
                S.set((base + "_x").c_str(),    p.pos_x);
                S.set((base + "_y").c_str(),    p.pos_y);
                S.set((base + "_z").c_str(),    p.pos_z);
            }
        }

        // HorseMod online policy persists across reboots so the user's
        // chosen modded-lobby ruleset survives a restart.  Unlike the
        // reset-override toggle, this one IS persistent - it's a
        // long-lived "what kind of online matches do I want" pref,
        // not a session-scoped behaviour.
        S.set("online_policy",
              static_cast<int>(Horse::OnlineRules::instance().current_policy()));
        // GameMode "Auto disable online" - see load path for the
        // default rationale.
        S.set("gamemode_auto_disable_online",
              Horse::GameMode::instance().auto_disable_online());

        S.save_if_dirty();
    }

    // ---- Hook / backend ----
    bool                         m_hook_registered = false;
    std::pair<int32_t, int32_t>  m_hook_ids{};
    StringType                   m_hook_path;
    int                          m_poll_counter = 0;
    int                          m_update_calls = 0;
    int                          m_engine_fallback_last_cockpit_calls = 0;
    int                          m_engine_fallback_missed_ticks = 0;
    bool                         m_engine_fallback_logged = false;

    // Reset-override UFunction hook bookkeeping.
    //
    // We don't actually know which UFunction the user's reset bind invokes -
    // SC6 has at least four candidate paths that all eventually run the
    // training-mode position-reset chain:
    //
    //   /Script/LuxorGame.LuxBattleManager:TrainingModePositionReset
    //   /Script/LuxorGame.LuxBattleManager:RestartBattle
    //   /Script/LuxorGame.LuxBattleManager:RestartBattleImmediately
    //   /Script/LuxorGame.LuxBattleFunctionLibrary:RequestTrainingModeBattleReset
    //
    // The previous attempt hooked only TrainingModePositionReset and the
    // post-hook never fired - the user's bind takes a different path.
    // Rather than guess, we register hooks on ALL of them and let the one
    // that fires identify itself in the log via the custom_data ptr.
    // Multiple firings are harmless: apply_to_charas() is idempotent
    // (writes the same captured pose to the same chara struct).
    //
    // Each slot is registered independently as soon as its containing class
    // is loaded; on_update polls until all slots are registered.  Failed
    // class lookups (class not yet loaded into UObject array) just retry
    // next tick, same way try_register_cockpit_hook works.
    struct ResetHookSlot
    {
        StringType class_path;          // gate: StaticFindObject of this UClass must succeed
        StringType func_path;           // RegisterHook key + custom_data tag + UnregisterHook key
        bool       registered = false;
        std::pair<int32_t, int32_t> ids{};
    };
    std::vector<ResetHookSlot>   m_reset_slots;
    bool m_battle_terminate_hook_registered{};
    std::pair<int32_t, int32_t> m_battle_terminate_hook_ids{};
    StringType m_battle_terminate_hook_path{
        STR("/Script/LuxorGame.LuxBattleGameMode:TerminateBattle")};

    // Last status for the Labbing tab's Copy/Paste pose JSON buttons.
    // ImGui-only state; UI thread reads + writes the same fields, no
    // atomics needed.  Empty string means "no message yet".
    std::string                  m_reset_pose_io_status;
    bool                         m_reset_pose_io_ok = false;

    // Tick counter for throttled settings persistence.  on_update
    // bumps this every frame and calls save_persisted_settings()
    // every kSaveEveryNFrames - batching slider-drag updates into
    // one disk write per ~2 seconds.  See ctor for constant value.
    int                          m_save_tick    = 0;
    static constexpr int         kSaveEveryNFrames = 120;

    Horse::Lux                 m_lux;
    Horse::Deterministic::Sc6ReplayRuntime m_replay_native_runtime{m_lux};
    struct ReplayQualificationTerminalSnapshot
    {
        bool presentation_coverage_valid{};
        bool presentation_identity_valid{};
        bool health_valid{};
        bool gameplay_rng_coverage_valid{};
        bool canonical_valid{};
        std::array<std::uint64_t, 10> presentation_coverage{};
        std::array<std::uint64_t, 9> presentation_identity{};
        std::array<std::uint64_t, 54> health{};
        std::array<std::uint64_t, 46> gameplay_rng_coverage{};
        std::uint64_t canonical_generation{};
        std::uint64_t canonical_frame{};
        Horse::Deterministic::CanonicalHash canonical_hash{};
    };
    // The native replay-exit hooks must invalidate all live object-backed
    // history before SC6 tears those objects down. Preserve only this bounded,
    // value-only observer snapshot so a later qualification EngineTick can
    // read terminal evidence without extending native lifetime.
    ReplayQualificationTerminalSnapshot
        m_replay_qualification_terminal_snapshot{};
    std::uint64_t m_replay_qualification_fp_mismatch_baseline{};
    std::uint64_t m_replay_qualification_cursor_mismatch_baseline{};
    std::uint64_t m_replay_qualification_batch_accounting_mismatch_baseline{};
    std::uint64_t m_replay_qualification_round_transition_barrier_baseline{};
    Horse::Deterministic::DeterministicHookSet m_deterministic_hooks{};
#if HORSE_ENABLE_GEKKONET || HORSE_ENABLE_OBSERVER_PROBE
    Horse::GlobalPtr m_online_battle_sync{};
#endif
#if HORSE_ENABLE_OBSERVER_PROBE
    Horse::Deterministic::Sc6OnlineObserverReadOnlyAccess
        m_online_observer_access{};
    Horse::Deterministic::Sc6OnlineObserverProbe m_online_observer_probe{
        m_online_observer_access};
#endif
#if HORSE_ENABLE_GEKKONET
    Horse::Deterministic::SteamP2PTransport
        m_online_qualification_transport{};
    Horse::Deterministic::SteamP2PTransport m_online_production_transport{};
    Horse::Fn m_online_get_stage_id{};
    Horse::GlobalPtr m_online_session_hub{};
    Horse::Fn m_online_request_battle_end_to_lobby{};
    Horse::Deterministic::Sc6OnlineSessionObserver
        m_sc6_online_session_observer{};
    Horse::Deterministic::SteamLobbyObserver m_steam_lobby_observer{};
    Horse::Deterministic::Sc6BattleSyncObserver m_battle_sync_observer{};
    Horse::Deterministic::ProductionOnlineAllowlist m_online_allowlist{};
    QualificationOnlineAllowlist m_online_qualification_allowlist{};
    QualificationOnlineCoordinator m_online_qualification_coordinator{
        m_online_qualification_transport, m_online_qualification_allowlist};
    ProductionOnlineCoordinator m_online_production_coordinator{
        m_online_production_transport, m_online_allowlist};
    OnlineCoordinatorRouter m_online_coordinator{
        m_online_qualification_coordinator, m_online_production_coordinator};
    Horse::Deterministic::OnlineLifecycle m_online_lifecycle{};
    Horse::Deterministic::GekkoRollbackSession m_online_gekko{};
    Horse::Deterministic::FrameCoordinate m_online_baseline_coordinate{};
    Horse::Deterministic::FrameCoordinate m_online_pending_coordinate{};
    std::int32_t m_online_next_gekko_frame{};
    std::uint8_t m_online_local_player_slot{};
    bool m_online_current_advance_pending{};
    std::atomic<bool> m_online_qualification_requested{};
    std::atomic<bool> m_online_production_requested{};
    std::atomic<bool> m_online_production_reentry_pending{};
    std::atomic<std::uint32_t> m_online_qualification_status{};
    Horse::Deterministic::CanonicalHash m_online_executable_identity{};
    Horse::Deterministic::CanonicalHash m_online_build_identity{};
    Horse::Deterministic::CanonicalHash m_online_loaded_map_identity{};
    std::uint64_t m_online_prefix_next_frame{};
    bool m_online_prefix_catchup{};
    bool m_online_takeover_ready{};
    bool m_online_identities_ready{};
    bool m_online_authentication_logged{};
    bool m_online_owned_storage_prepared{};
    std::string m_online_run_id{};
    bool m_online_release_manifest_failure_logged{};
    std::int32_t m_online_next_confirmed_hash_frame{29};
    Horse::Deterministic::FrameCoordinate m_online_last_observed_coordinate{};
    Horse::Deterministic::FrameCoordinate m_online_round_completed_coordinate{};
    bool m_online_round_transition_pending{};
    std::array<Horse::Deterministic::PlayerInput, 2>
        m_online_last_owned_inputs{};
    bool m_online_last_owned_inputs_valid{};
    std::array<Horse::Deterministic::PlayerInput, 2>
        m_online_round_hold_inputs{};
    bool m_online_round_hold_inputs_valid{};
    std::uint64_t m_online_corrections{};
    std::uint32_t m_online_max_correction_depth{};
    std::uint32_t m_online_rounds{};
    std::uint64_t m_online_first_owned_generation{};
    std::uint64_t m_online_confirmed_hashes{};
    std::uint64_t m_online_verified_audio_batches{};
    std::uint64_t m_online_verified_camera_batches{};
    std::uint64_t m_online_audio_sequence_mismatches{};
    std::uint64_t m_online_camera_publication_mismatches{};
    std::uint64_t m_online_presentation_failures{};
    OnlineQualificationFault m_online_qualification_fault{
        OnlineQualificationFault::None};
    bool m_online_qualification_fault_triggered{};
    std::uint64_t m_online_qualification_fault_started_ms{};
    std::uint32_t m_online_event_mask{};
    std::uint32_t m_online_preownership_failure_cleanup_delay{};
    Horse::Deterministic::OnlineQualificationMetrics
        m_online_qualification_metrics{};
#endif

    // Configured backends can target Persistent for active hit/hurt trails.
    // The *_once backends stay Foreground so inactive boxes in broad view
    // draw for the current frame only instead of entering the trail.
    Horse::LineBatcherBackend  m_backend_hit;
    Horse::LineBatcherBackend  m_backend_hurt;
    Horse::LineBatcherBackend  m_backend_hit_once;
    Horse::LineBatcherBackend  m_backend_hurt_once;
    Horse::LineBatcherBackend  m_backend_stage;

    // In-game ImGui overlay token (see on_unreal_init / dtor).  Non-zero
    // after Horse::GameImGui::register_tab returns; passed to
    // unregister_tab on teardown.
    uint64_t m_gameimgui_tab_token = 0;
    uint64_t m_gameimgui_toast_token = 0;
    bool m_gameimgui_init_pending = false;
    bool m_gameimgui_init_attempted = false;
    int m_gameimgui_init_delay_ticks_remaining = 0;
    static constexpr int kGameImGuiDeferredInstallTicks = 180;
    std::atomic<bool> m_gameimgui_toggle_key_down{false};

    RC::Unreal::Hook::GlobalCallbackId m_engine_tick_callback_id{
        RC::Unreal::Hook::ERROR_ID};

    // Nav-bootstrap flag: set to true when the overlay transitions from
    // hidden?shown, consumed by render_hitboxes_tab which then calls
    // ImGui::SetKeyboardFocusHere() on the master F5 toggle.  Forces
    // ImGui to assign a NavId and activate the nav highlight without
    // the user having to press Square/X first.  Without this, ImGui
    // shows the window focused but has no NavId to highlight, so the
    // D-pad appears to do nothing until a "menu" key press kicks nav
    // into gear by side effect.
    bool m_nav_bootstrap_pending = false;
    static constexpr int kHorseModTabCount = 5;
    int m_current_tab = 0;
    std::atomic<int> m_requested_tab{-1};

    // One-shot log flags so UE4SS.log doesn't fill with repeats.
    bool m_logged_native_missing = false;
    // Free-camera diagnostic one-shots: first time we successfully resolve
    // the PlayerCameraManager (so the user can confirm we're targeting
    // a real object), and first time we fall back to the direct +0x420
    // offset read (means reflection couldn't find the property name -
    // unlikely but survivable).
    bool m_logged_pcm_resolve  = false;
    bool m_logged_pcm_fallback = false;
    Horse::Deterministic::Config m_deterministic_config{};
    Horse::Deterministic::FailureCode m_deterministic_failure{
        Horse::Deterministic::FailureCode::None};
    std::unique_ptr<Horse::Deterministic::HgCpuRuntimeDiagnostics>
        m_hgcpu_runtime_diagnostics;
    std::unique_ptr<Horse::Deterministic::StageBreakListenerRuntimeDiagnostics>
        m_stage_break_listener_diagnostics;
    class StageBreakProcessMemory final
        : public Horse::Deterministic::INativeMemory
    {
    public:
        bool Read(std::uintptr_t address,
            std::span<std::byte> destination) noexcept override
        {
            if (address == 0 || destination.empty()) return false;
            __try
            {
                std::memcpy(destination.data(),
                    reinterpret_cast<const void*>(address), destination.size());
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool Write(std::uintptr_t,
            std::span<const std::byte>) noexcept override
        {
            return false;
        }
    };
    StageBreakProcessMemory m_stage_break_process_memory{};
    Horse::Deterministic::StageBreakListenerTopologyProbe
        m_stage_break_topology_probe{m_stage_break_process_memory};
    Horse::Deterministic::StageBreakListenerTopology m_stage_break_topology{};
    std::array<Horse::Deterministic::StageBreakActorRef,
        Horse::Deterministic::StageBreakPresentationIdentityMap::maximum_actors>
        m_stage_break_identity_actors{};
    std::array<Horse::Deterministic::StageBreakParticleAssetRef,
        Horse::Deterministic::StageBreakPresentationIdentityMap::maximum_assets>
        m_stage_break_identity_assets{};
    std::size_t m_stage_break_identity_actor_count{};
    std::size_t m_stage_break_identity_asset_count{};
    std::uint64_t m_stage_break_identity_generation{};
    bool m_stage_break_identity_failure_logged{};
    // Test-only request mailbox. Values: 1=wall Broken, 2=barrier Hit.
    // The request is consumed only from on_outer_tick_source while the
    // authoritative outer-capture context is active.
    std::atomic<std::uint32_t> m_qualification_stage_terminal_request{};
    std::atomic<std::uint32_t> m_qualification_stage_terminal_status{};
    std::atomic<std::uint32_t> m_qualification_stage_terminal_frame{};
    std::atomic<std::uint64_t> m_qualification_stage_terminal_requested_ms{};
    std::atomic<bool> m_qualification_stage_terminal_wait_logged{};
    bool m_hgcpu_diagnostic_failure_logged = false;
    bool m_stage_break_listener_failure_logged = false;
    bool m_deterministic_config_present = false;
    Horse::Deterministic::Status m_replay_native_runtime_status{
        Horse::Deterministic::FailureCode::ContextUnavailable};
    Horse::Deterministic::Status m_frame_fencepost_hook_status{
        Horse::Deterministic::FailureCode::ContextUnavailable};
    Horse::Deterministic::UcrtRandBroker m_ucrt_rand_broker{};
    std::atomic<Horse::Deterministic::FailureCode> m_frame_fencepost_failure{
        Horse::Deterministic::FailureCode::None};
    std::atomic<std::uintptr_t> m_frame_fencepost_manager{};
    std::atomic<std::uint32_t> m_frame_fencepost_last_frame{};
    std::atomic<std::uint64_t> m_frame_fencepost_observations{};
    std::atomic<std::uint64_t> m_frame_fencepost_entries{};
    std::atomic<std::uint64_t> m_frame_fencepost_repeats{};
    std::atomic<std::uint64_t> m_frame_fencepost_generations{};
    std::atomic<std::uint16_t> m_frame_fencepost_last_read_mask{};
    std::atomic<Horse::Deterministic::FailureCode> m_replay_exit_failure{
        Horse::Deterministic::FailureCode::None};
    std::atomic<std::uintptr_t> m_replay_exit_state{};
    std::atomic<std::uint64_t> m_replay_exit_observations{};
    std::atomic<bool> m_replay_identity_active{};
    std::atomic<std::uint32_t> m_frame_fencepost_expected_thread{};
    bool m_frame_fencepost_first_observation_logged{};
    bool m_frame_fencepost_incomplete_logged{};
    bool m_frame_fencepost_failure_logged{};
    bool m_replay_exit_first_observation_logged{};
    bool m_replay_exit_failure_logged{};
    std::atomic<std::uint64_t> m_candidate_checkpoint_logged_count{};
    std::atomic<std::uint64_t> m_candidate_batch_entry_logged_count{};
    std::atomic<std::uint64_t> m_native_batch_evidence_logged_intervals{};
    std::atomic_bool m_candidate_checkpoint_first_failure_logged{};
    std::atomic_bool m_candidate_batch_entry_first_failure_logged{};
    std::size_t m_owned_correction_probe_index{};
    static constexpr std::uint32_t kForcedQualificationCorrections = 600;
    static constexpr std::array<std::uint32_t, 3> kGroupedQualificationDepths{
        11, 1, 6};
    static constexpr std::uint64_t kGroupedQualificationAnchorSpacing = 15;
    struct ForcedCorrectionQualification
    {
        static constexpr std::uint64_t bucket_width_ns = 50'000;
        static constexpr std::size_t bucket_count = 336;
        std::array<std::uint32_t, bucket_count> buckets{};
        std::array<char, 97> run_id{};
        std::uint32_t depth{7};
        std::uint32_t location{1};
        std::uint32_t cycle_ordinal{};
        // 0=idle, 1=armed, 2=active, 3=passed, 4=failed, 5=cleaned.
        std::uint32_t lifecycle{};
        std::uint32_t completed{};
        std::uint32_t generation_transitions{};
        std::uint64_t first_generation{};
        std::uint64_t generation{};
        std::uint64_t first_frame{};
        std::uint64_t last_frame{};
        std::uint64_t maximum_ns{};
        std::size_t checkpoint_bytes_begin{};
        std::size_t batch_entry_bytes_begin{};
        std::size_t forced_history_bytes_begin{};
        Horse::Deterministic::DeterministicOwnedStorageStatus storage_begin{};
        Horse::Deterministic::DeterministicOwnedStorageStatus storage_end{};
        Horse::Deterministic::DeterministicOwnedStorageStatus storage_cleanup{};
        Horse::Deterministic::PresentationJournal::Statistics presentation_end{};
        Horse::Deterministic::CandidateAdapterPerformanceStatus capture_end{};
        std::uint64_t elapsed_ms{};
        std::uint64_t started_ms{};
        std::uint64_t timing_drift_ms{};
        std::uint64_t cleanup_stale_state_mask{};
        std::size_t pending_events_end{};
        std::size_t pending_payload_end{};
        std::size_t pending_events_cleanup{};
        std::size_t pending_payload_cleanup{};
        std::uint64_t round_terminal_audio_stop_all_baseline{};
        std::uint64_t suppressed_stage_wall_calls{};
        std::uint64_t suppressed_stage_barrier_calls{};
        std::uint64_t semantic_stage_dispatch_calls{};
        std::uint64_t suppressed_audio_calls{};
        std::uint64_t discarded_audio_calls{};
        std::uint64_t suppressed_audio_stop_all_calls{};
        std::uint64_t suppressed_audio_terminal_calls{};
        std::uint64_t suppressed_audio_blueprint_calls{};
        std::uint64_t suppressed_particle_spawn_calls{};
        std::uint64_t suppressed_particle_finished_binds{};
        std::uint64_t unknown_particle_routes{};
        std::uint64_t verified_audio_batches{};
        std::uint64_t verified_camera_batches{};
        std::uint64_t camera_publication_mismatches{};
        std::uint64_t audio_sequence_mismatches{};
        std::uint64_t presentation_failures{};
        Horse::Deterministic::Snapshot expected_scratch{};
        Horse::Deterministic::FailureCode failure{
            Horse::Deterministic::FailureCode::None};
        bool active{};
        bool warmup_pending{};
        bool awaiting_generation_history{};
        bool round_terminal_baseline_ready{};
        bool round_terminal_source_stop_all{};
        bool presentation_terminal_coverage{};
        bool reported{};
        bool runtime_armed{};
        bool cleanup_verified{};
        bool grouped{};
        std::uint32_t grouped_anchor_target{};
        std::uint32_t grouped_repeats_per_anchor{};
        std::uint32_t grouped_anchors_completed{};
        std::uint32_t grouped_failure_depth{};
        std::uint32_t grouped_failure_anchor{};
        std::uint32_t grouped_failure_repeat{};
        std::uint64_t grouped_last_anchor_generation{};
        std::uint64_t grouped_last_anchor_frame{};
        std::array<std::uint32_t, 3> grouped_completed{};
        std::array<std::uint64_t, 3> grouped_maximum_ns{};
        std::array<std::uint64_t, 3> grouped_anchor_sequence_hash{};
        std::array<std::array<std::uint32_t, bucket_count>, 3>
            grouped_buckets{};

        void Record(std::uint64_t value) noexcept
        {
            const auto bucket = static_cast<std::size_t>((std::min)(
                value / bucket_width_ns,
                static_cast<std::uint64_t>(bucket_count - 1)));
            ++buckets[bucket];
            maximum_ns = (std::max)(maximum_ns, value);
        }
        [[nodiscard]] std::uint64_t P99() const noexcept
        {
            if (completed == 0) return 0;
            const auto target = (completed * 99u + 99u) / 100u;
            std::uint32_t cumulative{};
            for (std::size_t index = 0; index < buckets.size(); ++index)
            {
                cumulative += buckets[index];
                if (cumulative >= target)
                    return (index + 1) * bucket_width_ns;
            }
            return bucket_count * bucket_width_ns;
        }

        void RecordGrouped(std::size_t depth_index,
            std::uint64_t value) noexcept
        {
            const auto bucket = static_cast<std::size_t>((std::min)(
                value / bucket_width_ns,
                static_cast<std::uint64_t>(bucket_count - 1)));
            ++grouped_buckets[depth_index][bucket];
            grouped_maximum_ns[depth_index] = (std::max)(
                grouped_maximum_ns[depth_index], value);
            ++grouped_completed[depth_index];
        }

        [[nodiscard]] std::uint64_t GroupedP99(
            std::size_t depth_index) const noexcept
        {
            const auto completed_count = grouped_completed[depth_index];
            if (completed_count == 0) return 0;
            const auto target = (completed_count * 99u + 99u) / 100u;
            std::uint32_t cumulative{};
            for (std::size_t index = 0;
                 index < grouped_buckets[depth_index].size(); ++index)
            {
                cumulative += grouped_buckets[depth_index][index];
                if (cumulative >= target)
                    return (index + 1) * bucket_width_ns;
            }
            return bucket_count * bucket_width_ns;
        }
    } m_forced_correction_qualification{};
    std::array<std::array<char, 97>, 128> m_forced_qualification_run_ids{};
    std::size_t m_forced_qualification_run_id_count{};
    std::uint32_t m_forced_qualification_cycle_ordinal{};
    std::uint64_t m_forced_qualification_first_elapsed_ms{};
    std::atomic<std::uint64_t> m_seek_request_frame{UINT64_MAX};
    std::atomic<std::uint64_t> m_seek_request_sequence{};
    std::uint64_t m_seek_handled_sequence{};
    std::uint64_t m_seek_pending_sequence{};
    std::uint32_t m_seek_defer_count{};
    bool m_seek_request_active{};
    std::atomic_bool m_resume_divergence_logged{};
    std::atomic<std::uint64_t> m_seek_completed_target{};
    std::atomic<std::uint64_t> m_seek_completed_source{};
    std::atomic<std::uint64_t> m_seek_completed_verified{};
