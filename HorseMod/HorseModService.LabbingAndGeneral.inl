    static bool read_labbing_live_distance(float& out) noexcept
    {
        for (uint32_t pi = 0; pi < 2; ++pi)
        {
            void* chara = Horse::KHitWalker::charaSlotFromGlobal(pi);
            float distance = 0.0f;
            if (Horse::KHitWalker::readOpponentDistance(chara, distance))
            {
                out = distance;
                return true;
            }
        }
        return false;
    }

    static void draw_labbing_attack_frame_row(const char* label, int pi)
    {
        void* chara = Horse::KHitWalker::charaSlotFromGlobal(
            static_cast<uint32_t>(pi));
        const auto s = Horse::KHitWalker::readLaneSnapshot(chara);

        ImGui::TextUnformatted(label);
        ImGui::SameLine(48.0f);
        if (!s.has_move)
        {
            ImGui::TextDisabled("idle");
            return;
        }

        const int curI = static_cast<int>(s.current_frame);
        const int totI = static_cast<int>(s.length_frames);
        const bool has_window =
            s.phase != Horse::KHitAttackPhase::None ||
            s.master_window_start != 0 ||
            s.master_window_end != 0;

        if (has_window)
        {
            ImGui::Text(
                "%3d / %3d  move=0x%04X  phase=%s  active=%d-%d%s",
                curI, totI,
                static_cast<uint16_t>(s.packed_move),
                Horse::KHitAttackPhaseName(s.phase),
                static_cast<int>(s.master_window_start),
                static_cast<int>(s.master_window_end),
                s.in_master_window ? "  [live]" : "");
        }
        else
        {
            ImGui::Text(
                "%3d / %3d  move=0x%04X  phase=%s  active=--",
                curI, totI,
                static_cast<uint16_t>(s.packed_move),
                Horse::KHitAttackPhaseName(s.phase));
        }

        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Current move animation frame from the active MoveVM lane.\n"
            "phase is the engine's startup / active / recovery tag.\n"
            "[live] means the strict in-master-window flag is set, so\n"
            "the active frame gate is open after sub-window inhibitors.");
    }

    // ==================================================================
    // Labbing tab - training-mode utilities for practising specific
    // setups: capture a custom reset pose and have the in-game training
    // position-reset bind warp both players back to it.
    // ==================================================================
    void render_labbing_tab()
    {
            // --- Attack animation frame ------------------------------
            // Same engine-truth source as the Time tab, with the
            // attack-window phase included so a raw animation frame
            // is not mistaken for an active hit frame.
            ImGui::TextUnformatted("Attack animation frame");
            draw_labbing_attack_frame_row("P1:", 0);
            draw_labbing_attack_frame_row("P2:", 1);
            ImGui::Separator();

            // --- Reset position override -----------------------------
            // When enabled and the user has captured a pose, our post-
            // hook on TrainingModePositionReset replays the captured
            // (X, Y, Z) for both players after the engine's
            // own reset has run.  Press the in-game training-reset
            // bind (default Select on a pad) to trigger.
            ImGui::TextUnformatted("Reset position override");
            {
                auto& ro = Horse::ResetOverride::instance();
                bool ro_on = ro.enabled();
                if (ImGui::Checkbox("Override reset position", &ro_on))
                {
                    ro.set_enabled(ro_on);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Send both players to the captured pose on the next "
                    "training-mode reset. Capture one below first.");

                if (ImGui::Button("Capture current position"))
                {
                    const bool ok = ro.capture_both();
                    if (ok)
                    {
                        Output::send<LogLevel::Default>(
                            STR("[HorseMod] reset-override pose captured\n"));
                    }
                    else
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[HorseMod] reset-override capture failed "
                                "(no active match?)\n"));
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Snapshot both characters' positions. "
                    "Persistent across restarts.");

                ImGui::SameLine();
                if (ImGui::Button("Clear captured pose"))
                {
                    ro.clear_captured();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Forget the captured pose.");

                // ---- Copy / Paste captured-pose JSON --------------------
                // Compact one-line JSON of the captured pose so users can
                // share setups (Discord, notes) without re-capturing.
                // Format documented in ResetOverride::poses_to_json /
                // poses_from_json.  Only "expected numbers in expected
                // places" validation - does NOT verify the position is
                // legal on the current stage.
                if (ImGui::Button("Copy position"))
                {
                    const std::string js =
                        Horse::ResetOverride::poses_to_json();
                    ImGui::SetClipboardText(js.c_str());
                    m_reset_pose_io_status = "copied to clipboard";
                    m_reset_pose_io_ok     = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Copy both captured poses to the clipboard as JSON.");

                ImGui::SameLine();
                if (ImGui::Button("Paste position"))
                {
                    const char* clip = ImGui::GetClipboardText();
                    if (!clip || !*clip)
                    {
                        m_reset_pose_io_status = "clipboard empty";
                        m_reset_pose_io_ok     = false;
                    }
                    else
                    {
                        std::string err;
                        const bool ok =
                            Horse::ResetOverride::poses_from_json(
                                std::string_view{clip}, err);
                        if (ok)
                        {
                            m_reset_pose_io_status = "pasted OK";
                            m_reset_pose_io_ok     = true;
                            Output::send<LogLevel::Default>(
                                STR("[HorseMod] reset-override pose "
                                    "pasted from clipboard\n"));
                        }
                        else
                        {
                            m_reset_pose_io_status = "paste failed: " + err;
                            m_reset_pose_io_ok     = false;
                            Output::send<LogLevel::Warning>(
                                STR("[HorseMod] reset-override paste "
                                    "rejected: {}\n"),
                                RC::to_generic_string(err));
                        }
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Replace captured poses from JSON in the clipboard. "
                    "P1 / P2 are independent - pasting a P1-only payload "
                    "leaves the existing P2 capture untouched.");

                if (!m_reset_pose_io_status.empty())
                {
                    const ImVec4 colour = m_reset_pose_io_ok
                        ? ImVec4{0.55f, 0.85f, 0.55f, 1.0f}
                        : ImVec4{0.95f, 0.55f, 0.35f, 1.0f};
                    ImGui::TextColored(colour, "%s",
                                       m_reset_pose_io_status.c_str());
                }

                // Per-player readout of what's currently captured.
                for (int pi = 0; pi < 2; ++pi)
                {
                    const auto p = ro.get_pose(pi);
                    if (p.has)
                    {
                        ImGui::TextDisabled(
                            "P%d  pos=(%.1f, %.1f, %.1f)",
                            pi + 1, p.pos_x, p.pos_y, p.pos_z);
                    }
                    else
                    {
                        ImGui::TextDisabled("P%d  not captured yet", pi + 1);
                    }
                }

                float live_distance = 0.0f;
                if (read_labbing_live_distance(live_distance))
                {
                    ImGui::TextDisabled("Live distance  %.2f", live_distance);
                }
                else
                {
                    ImGui::TextDisabled("Live distance  --");
                }

                const bool any_reset_registered = std::any_of(
                    m_reset_slots.begin(), m_reset_slots.end(),
                    [](const ResetHookSlot& s) { return s.registered; });
                if (!any_reset_registered)
                {
                    ImGui::TextDisabled(
                        "(waiting for training-reset hook - start a match)");
                }
            }

    }

    void render_general_tab()
    {
            // ---- Online safety gate (TOP of General - primary control) ----
            // The single master toggle for HorseMod's online auto-disable
            // behaviour.  Placed at the top of the General tab because:
            //   1. It governs whether four features in OTHER tabs (Camera,
            //      Time) get force-disabled, so the user needs to find it
            //      WITHOUT first hunting through unrelated controls.
            //   2. The colour-coded status indicator in the title bar
            //      (next to the window name) reflects this toggle's
            //      effect; placing the toggle near the top gives a clear
            //      visual link from the indicator to the control.
            //
            // When ON and the game enters Ranked / Casual matchmaking,
            // these four are force-disabled and their UI struck-through:
            //   - Lock camera position    (Camera tab)
            //   - Free-fly camera         (Camera tab, F7)
            //   - Freeze frame            (Time tab, F6)
            //   - Slow motion             (Time tab)
            //
            // Other features (hitbox overlay, character / weapon
            // visibility, VFX suppression, online rule overrides, reset-
            // position override) are unaffected.  See horselib/GameMode.hpp
            // for the full rationale behind this gated subset.
            {
                auto& gm = Horse::GameMode::instance();
                bool gating = gm.auto_disable_online();
                if (ImGui::Checkbox(
                        "Auto disable online",
                        &gating))
                {
                    gm.set_auto_disable_online(gating);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Force-disable Lock camera, Free-fly, Freeze "
                    "frame, and Slow motion in Ranked/Casual matches. "
                    "Indicator next to the window title shows the "
                    "current state.");

                // Friendly status row that mirrors the title-bar
                // indicator's state in plain text - same colour, same
                // tooltip body - so users who prefer reading text over
                // squinting at a 12-pixel square can see exactly what
                // the gate is doing right now.
                const OnlineStatusUI s = compute_online_status_ui();
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, s.colour);
                ImGui::TextUnformatted(s.short_label);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", s.tooltip_body);

                // Hook-installed warning surfaces here at the top of
                // the General tab so it's impossible to miss.  If the
                // SetPresence hook never installed (very rare), the
                // gate has no signal and stays inactive regardless of
                // the user's selection above.
                if (!gm.hook_installed())
                {
                    ImGui::TextColored(
                        ImVec4{1.0f, 0.45f, 0.20f, 1.0f},
                        "Warning: presence hook not yet installed.");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Presence hook hasn't installed yet. Resolves "
                        "a few seconds into engine init.");
                }
            }
            ImGui::Separator();

            // --- Stage boundary -----------------------------------------
            // Draws the live LuxBattle frame-bounds geometry used by terrain,
            // edge, and wall logic. This is intentionally independent of the
            // F5 hitbox overlay toggle.
            {
                bool sb = m_show_stage_boundary.load();
                if (ImGui::Checkbox("Stage boundary", &sb))
                    m_show_stage_boundary.store(sb);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Draw exact LuxBattle terrain triangles plus current "
                    "breakable-stage presentation bounds. Orange: ring/wall "
                    "clearance; blue: floor/ceiling; cyan: edge/ring-out; "
                    "purple: point-sampled special terrain; grey: excluded "
                    "scan entries. Unaffected by F5.");
            }

            {
                bool hsv = m_hide_stage_visuals.load();
                if (ImGui::Checkbox("Hide stage visuals", &hsv))
                    m_hide_stage_visuals.store(hsv);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Hide rendered stage meshes so hitboxes and stage "
                    "wireframes are easier to see. Gameplay collision is "
                    "unchanged.");
            }

            ImGui::Separator();

            // --- Hide weapons -------------------------------------------
            // Force hide both charas' weapons so they stop occluding the
            // hitbox overlay.  Calls SetWeaponVisibility(false) every frame
            // while on (so the game's own show-triggers don't sneak weapons
            // back in); calls SetWeaponVisibility(true) once on OFF to
            // restore.  Applies only while the overlay is enabled - if F5
            // turns the mod off, weapons stay in whatever state the engine
            // last set (typically visible).
            //
            // When "Hide characters" is also on, this control is greyed
            // out: CharaInvis already hides both chara mesh AND weapons
            // via a bytepatch that's incompatible with our per-frame
            // SetWeaponVisibility writes (the patch inverts the meaning
            // of the +0x534 weapon-flag, so writing 0 produces "visible"
            // - opposite of what we want).  See the apply-loop comment
            // in render_tab_impl for the full breakdown.
            const bool hide_chara_active = m_hide_chara.load();
            bool hw = m_hide_weapons.load();
            ImGui::BeginDisabled(hide_chara_active);
            if (ImGui::Checkbox("Hide weapons", &hw))
                m_hide_weapons.store(hw);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                if (hide_chara_active)
                {
                    ImGui::SetTooltip(
                        "Already covered by \"Hide characters\".");
                }
                else
                {
                    ImGui::SetTooltip(
                        "Hide both characters' weapons. Only applies "
                        "while the F5 overlay is enabled.");
                }
            }

            // --- Hide characters (bytepatch, no flicker) ---------------
            // Inverts the engine's own visibility-compare instructions
            // inside ALuxBattleChara_SyncMoveStateVisibility - the
            // chara stays hidden through every move state including
            // critical edges and transformations that previously caused
            // 1-frame flickers.  See horselib/CharaInvis.hpp.
            bool hc = m_hide_chara.load();
            if (ImGui::Checkbox("Hide characters", &hc))
            {
                m_hide_chara.store(hc);
                m_chara_invis.set(hc);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Hide both characters' models. Hitboxes and gameplay "
                "still work normally.");

            if (!m_chara_invis.is_resolved() && hc)
            {
                ImGui::TextDisabled(
                    "(couldn't hook character visibility - see UE4SS.log)");
            }

            // --- Suppress VFX ------------------------------------------
            // Bytepatch port of somberness's CE "VFX off" cheat.
            // Patches the engine's per-slot VFX-state writer to plant a
            // sentinel constant the renderer culls - effects never
            // become visible.  See horselib/VFXOff.hpp.
            bool sv = m_suppress_vfx.load();
            if (ImGui::Checkbox("Suppress VFX", &sv))
            {
                m_suppress_vfx.store(sv);
                m_vfx_off.set(sv);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Suppress hit sparks and particle VFX for a cleaner "
                "view.");

            if (!m_vfx_off.is_resolved() && sv)
            {
                ImGui::TextDisabled(
                    "(couldn't hook the VFX system - see UE4SS.log)");
            }

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Developer##general_developer"))
            {
                ImGui::TextUnformatted("Deterministic simulation");
                ImGui::TextDisabled("Lifecycle: Disabled");
                ImGui::TextDisabled(
                    "Native manifest: %zu qualified regions",
                    Horse::Deterministic::Schema::production_regions.size());
                if (m_replay_native_runtime.ready())
                {
                    ImGui::TextDisabled(
                        "Replay native bridge: signature verified (inactive)");
                }
                else
                {
                    const auto bridge_failure =
                        Horse::Deterministic::failure_code_name(
                            m_replay_native_runtime_status.code);
                    ImGui::TextDisabled(
                        "Replay native bridge: unavailable (%.*s)",
                        static_cast<int>(bridge_failure.size()),
                        bridge_failure.data());
                }
                if (m_deterministic_config.trace)
                {
                    const auto timeline = m_replay_native_runtime.timeline_status();
                    ImGui::TextDisabled(
                        "Frame fencepost: %s (observed=%llu, repeats=%llu, "
                        "generations=%llu, replay exits=%llu)",
                        m_deterministic_hooks.installed() ? "armed" : "unavailable",
                        static_cast<unsigned long long>(
                            m_frame_fencepost_observations.load()),
                        static_cast<unsigned long long>(
                            m_frame_fencepost_repeats.load()),
                        static_cast<unsigned long long>(
                            m_frame_fencepost_generations.load()),
                        static_cast<unsigned long long>(
                            m_replay_exit_observations.load()));
                    ImGui::TextDisabled(
                        "Replay timeline: frames=%llu sessions=%llu generations=%llu "
                        "landing=%llu landing_mib=%.2f entries=%llu entry_mib=%.2f "
                        "round=%d native_time=%d%s",
                        static_cast<unsigned long long>(timeline.captured_frames),
                        static_cast<unsigned long long>(timeline.sessions),
                        static_cast<unsigned long long>(timeline.generations),
                        static_cast<unsigned long long>(timeline.captured_checkpoints),
                        static_cast<double>(timeline.checkpoint_bytes) / (1024.0 * 1024.0),
                        static_cast<unsigned long long>(
                            timeline.captured_batch_entry_checkpoints),
                        static_cast<double>(timeline.batch_entry_checkpoint_bytes)
                            / (1024.0 * 1024.0),
                        timeline.native_round,
                        timeline.native_time,
                        timeline.partial ? " (memory limit reached)" : "");
                    ImGui::TextDisabled(
                        "Native fencepost: repeats=%llu same_time=%llu "
                        "cursor_mismatch=%llu round_state_frame=%u unpause=%d "
                        "pending_move=%u",
                        static_cast<unsigned long long>(timeline.repeat_requests),
                        static_cast<unsigned long long>(
                            timeline.same_native_time_coordinates),
                        static_cast<unsigned long long>(timeline.cursor_mismatches),
                        timeline.round_state_frame,
                        timeline.unpause_countdown,
                        static_cast<unsigned int>(timeline.pending_move_state));
                    ImGui::TextDisabled(
                        "Native batches: total=%llu zero=%llu multi=%llu "
                        "repeat_coords=%llu same_time_coords=%llu max=%u input_delta_max=%u "
                        "accounting_mismatch=%llu",
                        static_cast<unsigned long long>(timeline.native_batches),
                        static_cast<unsigned long long>(
                            timeline.zero_coordinate_batches),
                        static_cast<unsigned long long>(
                            timeline.multi_coordinate_batches),
                        static_cast<unsigned long long>(
                            timeline.batch_repeat_coordinates),
                        static_cast<unsigned long long>(
                            timeline.batch_same_input_time_coordinates),
                        timeline.maximum_coordinates_per_batch,
                        timeline.maximum_input_delta_per_batch,
                        static_cast<unsigned long long>(
                            timeline.batch_frame_accounting_mismatches));
                    ImGui::TextDisabled(
                        "Resim bases: uncovered=%llu entry_gap_max=%llu "
                        "distance_max=%llu",
                        static_cast<unsigned long long>(
                            timeline.coordinates_without_batch_entry_checkpoint),
                        static_cast<unsigned long long>(
                            timeline.maximum_batch_entry_checkpoint_gap),
                        static_cast<unsigned long long>(
                            timeline.maximum_resim_distance_from_batch_entry));
                    if (timeline.checkpoint_failure
                        != Horse::Deterministic::FailureCode::None)
                    {
                        const auto failure = Horse::Deterministic::failure_code_name(
                            timeline.checkpoint_failure);
                        ImGui::TextDisabled(
                            "Candidate checkpoint unavailable: %.*s",
                            static_cast<int>(failure.size()), failure.data());
                    }
                    if (timeline.batch_entry_checkpoint_failure
                        != Horse::Deterministic::FailureCode::None)
                    {
                        const auto failure = Horse::Deterministic::failure_code_name(
                            timeline.batch_entry_checkpoint_failure);
                        ImGui::TextDisabled(
                            "Batch-entry checkpoint unavailable: %.*s",
                            static_cast<int>(failure.size()), failure.data());
                    }
                    const auto probe_failure = m_frame_fencepost_failure.load(
                        std::memory_order_acquire);
                    if (probe_failure != Horse::Deterministic::FailureCode::None)
                    {
                        const auto failure = Horse::Deterministic::failure_code_name(
                            probe_failure);
                        ImGui::TextDisabled(
                            "Frame fencepost failure: %.*s",
                            static_cast<int>(failure.size()), failure.data());
                    }
                }
                ImGui::TextDisabled(
                    "Config: %s (window=%u, delay=%u, trace=%s)",
                    m_deterministic_config_present ? "loaded" : "missing",
                    m_deterministic_config.rollback_window,
                    m_deterministic_config.input_delay,
                    m_deterministic_config.trace ? "true" : "false");
                if (m_deterministic_failure
                    != Horse::Deterministic::FailureCode::None)
                {
                    const auto failure = Horse::Deterministic::failure_code_name(
                        m_deterministic_failure);
                    ImGui::TextDisabled(
                        "Terminal gate: %.*s",
                        static_cast<int>(failure.size()), failure.data());
                }
                ImGui::TextDisabled(
                    "Replay seek is exposed only through the qualification "
                    "fencepost API; online ownership remains fail-closed.");
                render_khit_audit_log_options();
            }

    }
