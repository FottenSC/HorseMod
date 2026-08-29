    void draw_hud_text_overlay()
    {
        const double now = ImGui::GetTime();

        struct Live { const char* text; double age_s; };
        Live live[kHudTextEventCount];
        size_t n_live = 0;
        for (const auto& ev : m_hud_text_events)
        {
            if (ev.text_len < 0) continue;
            const double age = now - ev.time;
            if (age < 0.0 || age > kHudTextEventLifetime) continue;
            live[n_live++] = Live{ev.text, age};
        }
        if (n_live == 0) return;

        // Newest first.
        std::sort(live, live + n_live,
                  [](const Live& a, const Live& b) { return a.age_s < b.age_s; });

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        ImVec2 origin{24.0f, 24.0f};

        for (size_t i = 0; i < n_live; ++i)
        {
            const float t = static_cast<float>(
                std::clamp(live[i].age_s / kHudTextEventLifetime, 0.0, 1.0));
            const uint32_t alpha = static_cast<uint32_t>(255.0f * (1.0f - t));
            const ImU32 colour   = IM_COL32(255, 220, 0, alpha);

            const ImU32 shadow = IM_COL32(0, 0, 0, alpha / 2);
            dl->AddText(ImVec2(origin.x + 1, origin.y + 1), shadow, live[i].text);
            dl->AddText(origin, colour, live[i].text);

            origin.y += line_h;
        }
    }

    // ------------------------------------------------------------------
    // ImGui panel - single window split into four topical tabs.
    //
    //   Hitboxes  master F5, live move-frame, KHit lists, attack-role
    //             / damage filters, hit-flash slider, render options
    //   Camera    pose lock (pos + rot), Free-fly (F7), Ansel
    //   Time      freeze frame, frame-step, slow-motion
    //   General   catch-all: visibility overrides (weapons / chara
    //             / VFX) and anything else not specific to the other
    //             three tabs
    //
    // render_tab_impl() is just the dispatch shell; each tab's widgets
    // live in its own render_*_tab() method so the monolithic 900-line
    // panel is now four ~200-line focused ones.
    // ------------------------------------------------------------------
    void render_tab_impl()
    {
        // ----------------------------------------------------------------
        // Always-on overlays first - these draw to GetForegroundDrawList
        // unconditionally so they show up regardless of whether the
        // HorseMod window is open / collapsed / hidden via F2.  Anything
        // that needs to appear on top of the game without the user
        // having to interact with our panel goes here.
        // ----------------------------------------------------------------
        draw_hud_text_overlay();

        // -----------------------------------------------------------------
        // Gamepad-first friendliness - ONE-SHOT focus claim on show
        // -----------------------------------------------------------------
        // When the overlay flips hidden ? shown (F2, Back-button, etc.)
        // we claim window focus + set m_nav_bootstrap_pending so the
        // currently-visible tab can run a single ImGui::SetKeyboardFocus-
        // Here() against its primary widget.  That's it.  No per-frame
        // re-claim, no focus-loss watchdog.
        //
        // History (so this comment doesn't get re-broken):
        // ------------------------------------------------
        // Earlier versions of this code ALSO ran a per-frame
        // `if (!IsWindowFocused) { SetWindowFocus(); m_nav_bootstrap_-
        // pending = true; }` block right after `Begin()`.  The intent was
        // "if focus drifted away for any reason, get it back".  In
        // practice that block caused two user-visible bugs:
        //
        //   1. CLICK EATING - `IsWindowFocused(_RootAndChildWindows)`
        //      can transiently return false during the same frame ImGui
        //      is processing a click on one of our widgets (popups,
        //      child regions, even regular checkbox state transitions
        //      can trigger a one-frame "focus is moving" window).
        //      Calling SetWindowFocus() in that window competes with
        //      the in-flight click and causes the click to be lost ~10%
        //      of the time.  Reported as "sometimes when opening the
        //      mod menu it lags quite a bit for letting me click on
        //      things."
        //
        //   2. STUCK BOOTSTRAP - m_nav_bootstrap_pending was set true
        //      every frame the focus check failed.  If the user was on
        //      a non-Hitboxes tab when the bootstrap fired, the flag
        //      was never consumed (only render_hitboxes_tab clears it).
        //      Then the moment the user navigated to Hitboxes,
        //      SetKeyboardFocusHere() snapped focus onto the F5
        //      checkbox - eating any in-flight click on a different
        //      widget.
        //
        // The fix below addresses both: bootstrap is one-shot, fires only
        // on the show edge, and is unconditionally cleared at the end of
        // each render_tab_impl regardless of which tab was visible.
        const bool just_shown = Horse::GameImGui::g_overlay_just_shown.exchange(
                false, std::memory_order_relaxed);
        if (just_shown)
        {
            ImGui::SetNextWindowFocus();
            m_nav_bootstrap_pending = true;
        }

        // Window title carries the package version so users can tell which
        // build is loaded when triaging bug reports.
        if (!ImGui::Begin(horsemod_window_title()))
        {
            ImGui::End();
            return;
        }

        // ---------------------------------------------------------------
        // Title-bar online-match status indicator
        // ---------------------------------------------------------------
        // Replaces the previous full-width banner with a small colored
        // square drawn IN the title bar, just to the right of the
        // window title text.  Hover for a tooltip explaining the
        // current state and the gate's effect.
        //
        // Four colour states (same semantics as the old banner):
        //   GREY     gating toggle off              - all features available
        //   GREEN    gating on, scene safe          - all features available
        //   RED      gating on, in Ranked / Casual  - 4 features force-disabled
        //   YELLOW   presence not yet resolved      - gate inactive
        //
        // The square is drawn into the WINDOW draw list (clipped to the
        // title bar rect) so it composites correctly with ImGui's own
        // title-bar rendering.  Tooltip uses IsMouseHoveringRect since
        // ImDrawList lines / rects don't go through the normal
        // input-claim path.
        draw_title_bar_status_indicator();

        // 2. L1 / R1 (shoulder) cycle tabs.  Two pieces of state:
        //    - m_current_tab mirrors whichever tab is ACTUALLY showing
        //      (updated by whichever BeginTabItem returns true this
        //      frame).
        //    - requested_tab is a one-frame switch target, consumed
        //      from m_requested_tab so shoulder presses can select the
        //      target tab without racing the renderer.
        //
        //    This separation avoids a bug where the currently-visible
        //    BeginTabItem's sync-back would clobber our requested-tab
        //    value during the per-tab iteration, causing the
        //    SetSelected flag to never be applied to the target tab.
        //    Symptom was: R1 from any tab > 0 would "bounce back" to
        //    the first tab on every press, because the target tab
        //    never received the focus hand-off.
        //
        //    L1/R1 are suppressed while a widget is actively being
        //    edited (dragging a slider) so they keep their stock
        //    ImGui "tweak slower / faster" role in that context.
        int requested_tab = m_requested_tab.exchange(
            -1, std::memory_order_relaxed);
        if (!ImGui::IsAnyItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, /*repeat=*/false))
            {
                requested_tab =
                    (m_current_tab + kHorseModTabCount - 1)
                    % kHorseModTabCount;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, /*repeat=*/false))
            {
                requested_tab =
                    (m_current_tab + 1) % kHorseModTabCount;
            }
        }

        if (ImGui::BeginTabBar("##horsemod_tabs"))
        {
            auto tab_item = [&](const char* label, int idx, auto&& body) {
                ImGuiTabItemFlags flags = 0;
                if (requested_tab == idx)
                {
                    flags |= ImGuiTabItemFlags_SetSelected;
                }
                if (ImGui::BeginTabItem(label, nullptr, flags))
                {
                    // Sync "what's actually visible" back to
                    // m_current_tab.  Does NOT touch requested_tab,
                    // so the SetSelected flag still gets applied to
                    // the target tab later in the iteration.
                    m_current_tab = idx;
                    body();
                    ImGui::EndTabItem();
                }
            };

            tab_item("Hitboxes", 0, [this] { render_hitboxes_tab(); });
            tab_item("Camera",   1, [this] { render_camera_tab(); });
            tab_item("Time",     2, [this] { render_time_tab(); });
            tab_item("Labbing",  3, [this] { render_labbing_tab(); });
            tab_item("General",  4, [this] { render_general_tab(); });

            ImGui::EndTabBar();
        }

        // Unconditionally clear m_nav_bootstrap_pending at the end of
        // every frame - even if the visible tab wasn't render_hitboxes_-
        // tab and didn't consume it.  Without this clear the flag would
        // be sticky across multiple frames in the "Camera/Time/General
        // tab is visible when the user shows the overlay" case, and
        // would then steal focus the moment the user navigated to the
        // Hitboxes tab (eating any in-flight click).  Clearing here
        // means: bootstrap is best-effort - if you happen to be on the
        // Hitboxes tab when the overlay shows, focus snaps to F5; on
        // any other tab the bootstrap is harmlessly dropped.
        m_nav_bootstrap_pending = false;

        ImGui::End();
    }

    void reset_khit_audit_cadence() noexcept
    {
        m_have_sphere_audit_frame = false;
        m_khit_audit_attack_logs_this_frame = 0;
        m_khit_audit_hurt_logs_this_frame = 0;
        m_khit_audit_pair_logs_this_frame = 0;
        m_khit_audit_calib_logs_this_frame = 0;
        m_khit_audit_cluster_logs_this_frame = 0;
    }

    void render_khit_audit_log_options()
    {
        bool audit = m_khit_sphere_audit.load();
        if (ImGui::Checkbox("KHit audit log", &audit))
        {
            m_khit_sphere_audit.store(audit);
            reset_khit_audit_cadence();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Logs native KHit attack shapes, defender hit-result masks, "
            "and paired attacker/hurtbox geometry to UE4SS.log once per "
            "game frame. Use OverlapPair lines to compare native vs UE "
            "centers for the exact incoming bit the engine accepted. "
            "This does not make extra boxes visible.");

        if (!audit)
            return;

        ImGui::Indent();

        bool filter_move = m_khit_sphere_audit_filter_move.load();
        if (ImGui::Checkbox("Move filter##sphere_audit_move_on",
                            &filter_move))
        {
            m_khit_sphere_audit_filter_move.store(filter_move);
            reset_khit_audit_cadence();
        }
        ImGui::SameLine();
        int move = m_khit_sphere_audit_move.load();
        ImGui::PushItemWidth(100.0f);
        if (ImGui::InputInt("Move id##sphere_audit_move", &move, 0, 0))
        {
            if (move < -1) move = -1;
            if (move > 0xFFFF) move = 0xFFFF;
            m_khit_sphere_audit_move.store(move);
            reset_khit_audit_cadence();
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Matches either the low 11-bit move id or the packed "
            "move value. Use 328 for hdr030_TEST.khd move 328.");

        bool filter_slots = m_khit_sphere_audit_filter_slots.load();
        if (ImGui::Checkbox("Slot filter##sphere_audit_slot_on",
                            &filter_slots))
        {
            m_khit_sphere_audit_filter_slots.store(filter_slots);
            reset_khit_audit_cadence();
        }
        ImGui::SameLine();
        int slot_a = m_khit_sphere_audit_slot_a.load();
        int slot_b = m_khit_sphere_audit_slot_b.load();
        ImGui::PushItemWidth(70.0f);
        if (ImGui::InputInt("A##sphere_audit_slot_a", &slot_a, 0, 0))
        {
            slot_a = std::clamp(slot_a, -1, 63);
            m_khit_sphere_audit_slot_a.store(slot_a);
            reset_khit_audit_cadence();
        }
        ImGui::SameLine();
        if (ImGui::InputInt("B##sphere_audit_slot_b", &slot_b, 0, 0))
        {
            slot_b = std::clamp(slot_b, -1, 63);
            m_khit_sphere_audit_slot_b.store(slot_b);
            reset_khit_audit_cadence();
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "For attack nodes, matches node slot or node+0x08 slot "
            "bit. For hurt-result lines, the full incoming mask is "
            "logged whenever native wrote a nonzero mask, even if "
            "the move filter misses.");

        if (ImGui::Button("Use 328 / all active slots"))
        {
            m_khit_sphere_audit_filter_move.store(true);
            m_khit_sphere_audit_move.store(328);
            m_khit_sphere_audit_filter_slots.store(false);
            m_khit_sphere_audit_slot_a.store(56);
            m_khit_sphere_audit_slot_b.store(57);
            reset_khit_audit_cadence();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Preset for hdr030_TEST.khd move 328. Slot filtering is "
            "disabled so Area/FixArea or slot 21 contributors are "
            "not hidden while debugging the huge edited hitbox.");

        ImGui::Unindent();
    }

    // ==================================================================
    // Hitboxes tab - the core feature.  Master F5 toggle with live
    // status line, per-player move-frame display, KHit list checkboxes
    // (hurt / attack / body for P1 + P2), attack-role filters
    // (strike / throw) and the three engine-derived damage filters,
    // hit-flash duration slider, LineBatcher render options.
    // ==================================================================
    void render_hitboxes_tab()
    {
        // Nav bootstrap - see m_nav_bootstrap_pending doc comment.
        // Called BEFORE the checkbox so ImGui applies focus to it.
        // Cleared immediately so subsequent frames don't keep
        // stealing focus from wherever the user has navigated to.
        if (m_nav_bootstrap_pending)
        {
            ImGui::SetKeyboardFocusHere();
            m_nav_bootstrap_pending = false;
        }
        bool enabled = m_enabled.load();
        if (ImGui::Checkbox("Overlay enabled (F5)", &enabled))
        {
            m_enabled.store(enabled);
            if (!enabled)
            {
                hide_khit_overlay_lines();
            }
        }
        // Belt-and-suspenders: SetItemDefaultFocus registers the F5
        // checkbox as the fallback nav target when the tab bar
        // switches between tabs (ImGui picks this widget when there
        // are no previous-nav hints in the new tab).
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        // Friendly readiness summary.  If anything's still
        // initialising, say which thing and (almost always) the user
        // just needs to start a match for the rest to come online.
        if (!Horse::NativeBinding::isReady())
        {
            ImGui::TextDisabled("(setting up - check UE4SS.log if this persists)");
        }
        else if (!m_hook_registered)
        {
            ImGui::TextDisabled("(waiting for a match to start)");
        }
        else if (!m_backend_hit.isReady() || !m_backend_hurt.isReady() ||
                 !m_backend_hit_once.isReady() ||
                 !m_backend_hurt_once.isReady())
        {
            ImGui::TextDisabled("(waiting for the battle scene)");
        }
        else
        {
            ImGui::TextDisabled("(ready)");
        }

        ImGui::Separator();

        auto per_player_row = [](const char* label,
                                 std::atomic<bool>& hurt,
                                 std::atomic<bool>& atk,
                                 std::atomic<bool>& body,
                                 const char* id_suffix)
        {
            ImGui::PushID(id_suffix);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(80.0f);
            {
                bool h = hurt.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Hurtboxes##%s", id_suffix);
                if (ImGui::Checkbox(tag, &h)) hurt.store(h);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Show this player's hurtboxes (volumes that take "
                    "damage). Green; flash red on hit.");
            }
            ImGui::SameLine();
            {
                bool a = atk.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Hitboxes##%s", id_suffix);
                if (ImGui::Checkbox(tag, &a)) atk.store(a);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Show this player's hitboxes (volumes that deal "
                    "damage). Strikes amber/yellow, throws magenta/pink.");
            }
            ImGui::SameLine();
            {
                bool b = body.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Body##%s", id_suffix);
                if (ImGui::Checkbox(tag, &b)) body.store(b);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Show this player's pushbox (used for spacing, "
                    "not damage). Dim blue.");
            }
            ImGui::PopID();
        };

        per_player_row("P1",
                       m_show_p1_hurt, m_show_p1_atk, m_show_p1_body, "p1");
        per_player_row("P2",
                       m_show_p2_hurt, m_show_p2_atk, m_show_p2_body, "p2");

        ImGui::Spacing();

        // --- Box-visibility filter ---------------------------------------
        // Single master toggle.  See the m_only_show_active block at the
        // top of this class for the engine-truth predicates.
        {
            bool only_active = m_only_show_active.load();
            if (ImGui::Checkbox("Only boxes that can matter this frame",
                                &only_active))
            {
                m_only_show_active.store(only_active);
                clear_persistent_khit_trails();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Default analysis view.\n\n"
                "Hitboxes: shown only on engine damage frames.\n"
                "Hurtboxes: shown only when the classifier can read the "
                "slot, the overlap gate is on, and the defender can react.\n\n"
                "Turn this off to inspect authored boxes, ignored slots, "
                "partial armor, and full-body i-frame states.");
            ImGui::TextDisabled(
                "Colors: hurt green/red, ignored slot cyan, per-slot off "
                "dim green, full-body i-frames purple, no-react grey; "
                "throws grey when height dispatch rejects.");
        }

        // --- Hit-flash duration -----------------------------------------
        // The raw PerHurtboxReactionState signal is a ~1-frame pulse
        // (~16ms at 60fps) - too short to see.  This slider extends the
        // visible red flash by holding the "hot" state for N GAME FRAMES
        // before fading.  0 = disable the sticky entirely (raw 1-frame
        // pulse only).
        //
        // The drain is keyed on g_LuxBattle_FrameCounter (incremented
        // at the end of LuxBattle_PerFrameTick), so it tracks the same
        // tick the rest of the simulation does:
        //   * Freeze frame ON  ? counter halts ? flash held indefinitely.
        //   * F6 step          ? counter +1   ? flash drains by 1.
        //   * Slow-mo at S-    ? counter advances at S- wall rate, so
        //                        the flash visibly persists 1/S- longer
        //                        in real time (matching the slowed anim).
        //   * Native play      ? counter advances at 60Hz regardless of
        //                        render rate, so 15 frames = 250ms on
        //                        any monitor (60/120/144Hz).
        //
        // 15 frames - 250ms at 60fps; 60 frames - 1 second; the slider
        // caps at 60.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Hit-flash duration");
        {
            int frames = m_flash_frames.load();
            if (ImGui::SliderInt("frames##flashdur", &frames, 0, 60, "%d frames"))
            {
                if (frames < 0)  frames = 0;
                if (frames > 60) frames = 60;
                m_flash_frames.store(frames);
                Horse::KHitWalker::setStickyFrames(frames);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "How long the red hit-flash stays visible, in game "
                "frames (60/sec). Held during freeze; drains 1 per "
                "F6 step. 0 disables.");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Render");
        {
            float t = m_thickness.load();
            if (ImGui::SliderFloat("Thickness", &t, 0.5f, 8.0f, "%.1f"))
                m_thickness.store(t);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Line thickness for the wireframes.");

            // Per-feature renderer combos.  Two entries each: Persistent
            // (depth-tested trail plus a current-frame foreground copy) and
            // Normal (always-on-top, lines clear each frame - clean read
            // of the current state).  The third historical entry "Default"
            // (UWorld+0x40, depth-tested per-frame) was removed because
            // its lines disappeared behind characters, which defeats
            // the purpose of an overlay.
            //
            // Enum order matches LineBatcherSlot: Persistent=0, Normal=1.
            const char* slot_names[2] = {
                "Persistent (trail)",
                "Normal",
            };

            int hit_idx = static_cast<int>(m_slot_hit.load());
            if (ImGui::Combo("Hitbox renderer", &hit_idx, slot_names, 2))
                m_slot_hit.store(static_cast<Horse::LineBatcherSlot>(hit_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Normal: current frame, always on top. Persistent: "
                    "active hitboxes trail while the current hitbox also "
                    "draws on top each frame.");

            int hurt_idx = static_cast<int>(m_slot_hurt.load());
            if (ImGui::Combo("Hurtbox renderer", &hurt_idx, slot_names, 2))
                m_slot_hurt.store(static_cast<Horse::LineBatcherSlot>(hurt_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Normal: current frame, always on top. Persistent: "
                    "active hurtboxes trail while the current hurtbox also "
                    "draws on top each frame.");

            // Trail length - only meaningful when at least one renderer
            // is set to Persistent.  Hidden otherwise to keep the UI
            // free of inert controls.  Persistent batchers accumulate only
            // engine-live hit/hurt trail samples. Current active boxes,
            // inactive broad-view boxes, and body boxes are routed to
            // one-frame Foreground fallbacks. Dense moves are line-capped
            // and old trail samples are trimmed before the renderer stalls.
            const bool any_persistent =
                m_slot_hit.load()  == Horse::LineBatcherSlot::Persistent ||
                m_slot_hurt.load() == Horse::LineBatcherSlot::Persistent;
            if (any_persistent)
            {
                int trail = m_trail_frames.load();
                if (ImGui::SliderInt("Trail frames##trail", &trail,
                                     1, 300, "%d frames"))
                {
                    if (trail < 1)   trail = 1;
                    if (trail > 300) trail = 300;
                    m_trail_frames.store(trail);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "How long Persistent-slot lines stay visible, in "
                    "game frames (60/sec). Lifetime decrements only "
                    "when SC6's game-frame counter advances, so freeze "
                    "holds the trail and F6 step drains one frame. "
                    "Only active hit/hurt boxes enter the trail; current "
                    "attack boxes still redraw detailed once per render "
                    "frame, while hurtboxes and trail samples use compact "
                    "rings. Dense moves auto-trim old trail samples to "
                    "protect FPS.");
            }

        }
    }

    // ==================================================================
    // Camera tab - pose lock (position + rotation group), Free-fly
    // camera (F7) with its sub-controls (move/look/FOV sliders, live
    // pose readout, memory-verify line, input diagnostics), and Ansel
    // always-allowed.  All independent of the F5 hitbox overlay.
    // ==================================================================
    void render_camera_tab()
    {
        // --- Always allow Ansel camera -----------------------------------
        // Runs independent of the F5 hitbox overlay.  Kept at the top of
        // the Camera tab (rather than buried under Free-fly's sub-controls)
        // because it's a single checkbox with no state to inspect - the
        // user either wants Ansel always available or not.
        bool aa = m_ansel_always_allowed.load();
        if (ImGui::Checkbox("Always allow Ansel camera", &aa))
            m_ansel_always_allowed.store(aa);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Force NVIDIA Ansel (the built-in free-camera photo\n"
            "mode) to be available at all times.\n\n"
            "Normally SC6 only allows Ansel in specific situations\n"
            "(menus, cinematics, ring-out).  With this on you can\n"
            "trigger the Ansel hotkey any time, even mid-match.\n\n"
            "Independent of the F5 overlay - you can use Ansel with\n"
            "or without the hitbox overlay enabled.");

        ImGui::Separator();

        // --- Lock camera position -----------------------------------
            // Bytepatch-based: NOPs the engine's per-frame stores into
            // the camera struct, so whatever pose the camera is in at
            // toggle-ON time stays put until OFF.  See
            // horselib/CamLock.hpp for the disassembly walk and the
            // history of why the previous CameraCache.POV-write
            // approach didn't work.
            //
            // UI binding: read directly from the live CamLock state
            // rather than from m_lock_camera.  Free-fly camera toggles
            // CamLock on/off behind the scenes, so if we bound to the
            // separate `m_lock_camera` atomic the checkbox could drift
            // out of sync with reality ("checkbox off but camera is
            // locked because free-fly turned it on").  Additionally we
            // grey-out the checkbox while free-fly is active because
            // its underlying CamLock is being driven by the free-fly
            // state machine - letting the user poke the checkbox then
            // would cause a fight between the two owners.
            const bool fc_on = m_free_camera_enabled.load();
            const bool online_locked =
                Horse::GameMode::instance().should_force_disable_features();
            bool lc = m_cam_lock.is_enabled();
            const bool any_disabled = fc_on || online_locked;
            if (any_disabled) ImGui::BeginDisabled(true);
            if (ImGui::Checkbox("Lock camera position", &lc))
            {
                m_lock_camera.store(lc);
                m_cam_lock.set(lc);
            }
            if (any_disabled) ImGui::EndDisabled();
            // Strike through the label when force-disabled BY THE
            // ONLINE GATE specifically - a strong visual cue that the
            // gate (not a normal "no value" path) is blocking the
            // toggle.  Strikethrough is reserved for the online-gate
            // case so the existing "free-fly owns the lock" disabled
            // state still looks like a regular grey-out.
            if (online_locked) draw_disabled_strikethrough();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                online_locked
                    ? "Disabled - you're in a Ranked or Casual online\n"
                      "match and the General tab's \"Auto disable online\"\n"
                      "toggle is on.\n\n"
                      "Camera locking will be available again when the\n"
                      "match ends, or turn the \"Auto disable online\"\n"
                      "toggle off in the General tab to override (not\n"
                      "recommended for online play)."
                : fc_on
                    ? "Disabled while Free-fly camera is on - free-fly\n"
                      "takes over the camera lock while it's active.\n"
                      "Turn free-fly off first to toggle this manually."
                    : "Freeze the camera at its current position, angle,\n"
                      "and zoom level.  The game's own camera system\n"
                      "stops moving it until you turn this off.\n\n"
                      "Useful for framing a specific moment: turn this\n"
                      "OFF, let the game move the camera where you\n"
                      "want it, then turn ON to hold that shot.\n\n"
                      "Independent of the F5 overlay.");

            // Lock camera rotation has been removed from the UI.
            // It's still useful internally - Free-fly camera enables
            // it automatically while it's active so arrow-key look
            // works - but exposing it as a separate user toggle was
            // confusing.  Free-fly now owns the rotation lock entirely.

            // Status line - friendly summary of whether the camera
            // is currently locked.  Free-fly turning on the lock
            // counts as "active" here so the user sees feedback when
            // free-cam mode is engaged.
            if (!m_cam_lock.is_resolved() && lc)
            {
                ImGui::TextDisabled(
                    "(camera lock couldn't find its hook points - "
                    "see UE4SS.log for details)");
            }
            else if (m_cam_lock.is_enabled() &&
                     m_cam_lock.is_rotation_enabled())
            {
                ImGui::TextDisabled(
                    "(camera fully locked - position + rotation)");
            }
            else if (m_cam_lock.is_enabled())
            {
                ImGui::TextDisabled("(camera position locked)");
            }
            else if (m_cam_lock.is_rotation_enabled())
            {
                ImGui::TextDisabled("(camera rotation locked)");
            }

            // --- Free-fly camera (Ansel replacement) --------------------
            // Built-in WASD + arrow-key fly camera.  Uses CamLock to
            // freeze the engine's camera stores then writes the pose
            // ourselves each cockpit tick.  Works WITHOUT invoking
            // Nvidia Ansel - the hitbox overlay stays visible because
            // SC6's `r.Photography.InSession` CVar never gets set.
            ImGui::Spacing();
            {
                const bool ff_online_locked =
                    Horse::GameMode::instance().should_force_disable_features();
                bool fc = m_free_camera_enabled.load();
                if (ff_online_locked) ImGui::BeginDisabled(true);
                if (ImGui::Checkbox("Free-fly camera (F7)", &fc))
                    m_free_camera_enabled.store(fc);
                if (ff_online_locked) ImGui::EndDisabled();
                if (ff_online_locked) draw_disabled_strikethrough();
                if (ImGui::IsItemHovered() && ff_online_locked)
                {
                    ImGui::SetTooltip(
                        "Disabled - you're in a Ranked or Casual online\n"
                        "match and the General tab's \"Auto disable online\"\n"
                        "toggle is on.\n\n"
                        "Free-fly camera will be available again when the\n"
                        "match ends, or turn the \"Auto disable online\"\n"
                        "toggle off in the General tab to override (not\n"
                        "recommended for online play).");
                }
                else if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Take manual control of the camera and fly it\n"
                    "around freely.  Unlike Ansel this keeps the\n"
                    "hitbox overlay visible and the match running.\n\n"
                    "Keyboard (game window must be focused):\n"
                    "  W / S       move forward / back\n"
                    "  A / D       strafe left / right\n"
                    "  E / Q       move up / down\n"
                    "  Arrows or IJKL   look around\n"
                    "  Shift       5- faster  |  Ctrl  0.2- slower\n"
                    "(If the arrow keys don't respond, use IJKL\n"
                    " instead - the game grabs arrows on some\n"
                    " screens.)\n\n"
                    "Controller (player 1):\n"
                    "  Left stick    move\n"
                    "  Right stick   look\n"
                    "  LT / RT       move down / up\n"
                    "  LB / RB       0.2- / 5- speed\n\n"
                    "Turning this on also locks the camera\n"
                    "automatically; turning it off releases the\n"
                    "lock.  To re-frame a shot, toggle OFF, let\n"
                    "the game move the camera where you want,\n"
                    "then toggle back ON.");

                // Sub-controls, only visible when free-cam is on to
                // avoid cluttering the Camera tab.
                if (fc)
                {
                    float mv = m_free_camera.move_speed();
                    if (ImGui::SliderFloat("Move speed", &mv,
                                           2.0f, 100.0f, "%.1f"))
                        m_free_camera.move_speed() = mv;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "How fast WASD / left-stick moves the camera.\n"
                        "Hold Shift (or RB) for 5- this speed, Ctrl\n"
                        "(or LB) for 0.2-.");

                    float lk = m_free_camera.look_speed();
                    if (ImGui::SliderFloat("Look speed", &lk,
                                           0.2f, 6.0f, "%.2f"))
                        m_free_camera.look_speed() = lk;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "How fast the arrow keys / IJKL / right-stick\n"
                        "rotate the camera view.  Same Shift / Ctrl\n"
                        "multipliers as Move speed.");

                    float fv = m_free_camera.fov_deg();
                    if (ImGui::SliderFloat("Field of view", &fv,
                                           20.0f, 120.0f, "%.0f"))
                        m_free_camera.fov_deg() = fv;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Camera field of view in degrees.  Lower =\n"
                        "zoomed-in / telephoto look, higher = wide-\n"
                        "angle / fisheye look.  70 is the game's\n"
                        "default.");

                    // Live pose readout - handy for reproducing shots.
                    ImGui::TextDisabled(
                        "position (%.1f, %.1f, %.1f)  rotation (%.1f, %.1f, %.1f)",
                        m_free_camera.loc_x(),
                        m_free_camera.loc_y(),
                        m_free_camera.loc_z(),
                        m_free_camera.pitch(),
                        m_free_camera.yaw(),
                        m_free_camera.roll());

                    // On-screen memory persistence check - read the
                    // camera-manager memory live and compare to our
                    // expected pose.  Makes "is our write actually
                    // reaching memory?" debuggable without reading log
                    // files.
                    // Connection status: keeping these (they're genuinely
                    // useful when input suddenly stops working) but
                    // rewriting the labels in plain English.
                    ImGui::TextDisabled(
                        "Controller: %s",
                        Horse::FreeCamera::controllerConnected()
                            ? "connected"
                            : "not detected");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Whether a controller is currently reporting\n"
                        "input to the game.  If 'not detected' but\n"
                        "you ARE pressing buttons, Steam Input or the\n"
                        "controller driver may not be passing the\n"
                        "input to SC6.");

                    ImGui::TextDisabled(
                        "Keyboard: %s",
                        (Horse::LowLevelKeyInput::instance().hook_installed() ||
                         Horse::RawInputSource::instance().ready())
                            ? "responding"
                            : "not responding");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Whether the keyboard-input paths the free-\n"
                        "camera uses are currently alive.  If 'not\n"
                        "responding', movement / look keys won't\n"
                        "register.");

                    // Developer-mode "Log key input" and
                    // "Log camera pose" checkboxes have been removed
                    // from the UI.  The underlying atomics still
                    // exist on Horse::FreeCamera, so the diagnostics
                    // can still be flipped from C++ if anyone is
                    // chasing a bug, but the panel stays clean of
                    // debug-only checkboxes for typical users.
                }

                // Friendly status when free-cam is on but we don't
                // have a camera to drive (menus, idle, loading).
                if (fc && !m_cached_player_camera_manager)
                {
                    ImGui::TextDisabled(
                        "(waiting for a match - free-cam needs an "
                        "active camera)");
                }
            }
    }

    // ==================================================================
    // Time tab - Freeze frame (WorldTickGate hard stop), Step 1 / Step N
    // buttons for deterministic frame-stepping under freeze, and the
    // gate-driven Slow-motion slider + preset buttons (0.001x..1.0x).
    // ==================================================================
    void render_time_tab()
    {
        // --- Live move-frame display -------------------------------------
        // Deref chara+0x44068 ActiveLaneStateCursorPtr and show
        // CurrentAnimFrame / AnimLengthFrames for each player.  Costs
        // ~4 safe reads per frame per player - negligible.
        //
        // Lives on the Time tab because it's the frame-data readout you
        // watch while driving Freeze frame / Slow-mo: "I paused at frame
        // 7 of 30 of move 0x1234, lane 2, playback 0.5x."
        {
            ImGui::TextUnformatted("Move frame");
            auto row = [](const char* label, int pi) {
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
                ImGui::Text("%3d / %3d  move=0x%04X  lane=%d  speed=%.2fx%s%s",
                            curI, totI,
                            static_cast<uint16_t>(s.packed_move),
                            static_cast<int>(s.lane_index),
                            s.playback_speed,
                            s.primary_entry_script_bracket ? "  [entry]" : "",
                            s.finished      ? "  [done]" :
                            s.at_end        ? "  [end]"  : "");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Live readout of which move this player is in:\n"
                    "  current-frame / total-frames\n"
                    "  move ID (internal hex value)\n"
                    "  lane (which attack slot is active)\n"
                    "  speed (playback multiplier - 1.00x = normal)\n"
                    "  [entry] = primary move-entry setup/script bracket\n"
                    "  [done] / [end] = move has finished playing\n"
                    "\n"
                    "Useful alongside Freeze frame and Slow-motion:\n"
                    "pause the world, read the frame number off this\n"
                    "line, then step to inspect exactly what's active\n"
                    "on that frame.");
            };
            row("P1:", 0);
            row("P2:", 1);
        }

        ImGui::Separator();

            // --- Freeze frame (WorldTickGate-driven) ------------------
            // frame_step_apply() resolves/enables WorldTickGate and sibling
            // gates on the next cockpit tick. Frame-step adds gate credits;
            // it no longer writes SpeedControl speedval or resolves legacy
            // SpeedControl replay AOBs.
            const bool time_online_locked =
                Horse::GameMode::instance().should_force_disable_features();
            bool ff = m_freeze_frame.load();
            if (time_online_locked) ImGui::BeginDisabled(true);
            if (ImGui::Checkbox("Freeze frame", &ff))
            {
                m_freeze_frame.store(ff);
                // No explicit resolve/enable here - frame_step_apply
                // does the lazy enable on the next cockpit tick when
                // it sees freeze=true.
            }
            if (time_online_locked) ImGui::EndDisabled();
            if (time_online_locked) draw_disabled_strikethrough();
            if (ImGui::IsItemHovered() && time_online_locked)
            {
                ImGui::SetTooltip(
                    "Disabled - you're in a Ranked or Casual online\n"
                    "match and the General tab's \"Auto disable online\"\n"
                    "toggle is on.\n\n"
                    "Freeze frame will be available again when the\n"
                    "match ends, or turn the \"Auto disable online\"\n"
                    "toggle off in the General tab to override (not\n"
                    "recommended for online play).");
            }
            else if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "The game will be pause while this is checked. This "
                "option will also be enabled if you step x frames "
                "forward");

            // Step-frame controls.  m_step_pending++ queues frames so
            // mashing the button (or holding F6) is lossless.  No
            // engine-state gating needed - the SpeedControl patches
            // are independent of battle context and work as soon as
            // they resolve, which the frame_step_apply driver does
            // lazily on first non-1.0 target.
            ImGui::BeginDisabled(!ff);
            if (ImGui::Button("Step 1 (F6)"))
            {
                m_step_pending.fetch_add(1);
            }
            ImGui::SameLine();
            static int s_step_n = 10;
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::InputInt("##stepn", &s_step_n, 0))
            {
                if (s_step_n < 1)   s_step_n = 1;
                if (s_step_n > 600) s_step_n = 600;
            }
            ImGui::SameLine();
            if (ImGui::Button("Step N"))
            {
                if (s_step_n > 0) m_step_pending.fetch_add(s_step_n);
            }
            ImGui::EndDisabled();

            if (!ff && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Turn on Freeze frame first - stepping only\n"
                    "makes sense when the world is paused.");
            }

            // Status line - "paused" / "stepping" / arming.
            if (const int q = m_step_pending.load(); q > 0)
            {
                ImGui::TextDisabled("(advancing %d more frame%s)",
                                    q, q == 1 ? "" : "s");
            }
            else if (ff)
            {
                ImGui::TextDisabled(
                    m_world_tick_gate.is_resolved()
                        ? "(paused - press F6 to advance one frame)"
                        : "(arming WorldTickGate on next cockpit tick)");
            }

            // --- Speed control (slow-motion / freeze) ------------------
            // Replaces the engine's master delta-time reads with a load
            // from a single user-controlled float.  Independent of the
            // Freeze-frame toggle above - Freeze gates the chara state
            // machine, this gates ALL dt-driven subsystems (animation,
            // hit timing, particles within the MoveVM scope).
            //
            // Slow-motion is implemented as a WorldTickGate cadence.  The
            // checkbox flips desired state; frame_step_apply resolves/enables
            // the actual gates on the next cockpit tick.
            {
                // The whole slow-motion block (checkbox + slider +
                // preset buttons) is locked while the online gate is
                // engaged - disabling just the checkbox would leave
                // the slider/presets clickable, and clicking a preset
                // would still mutate m_speed_value (harmless while locked,
                // but confusing UI).  Wrapping the whole block keeps the
                // visual state honest.
                const bool sm_online_locked =
                    Horse::GameMode::instance().should_force_disable_features();
                if (sm_online_locked) ImGui::BeginDisabled(true);

                bool sc_on = m_speed_enabled.load();
                if (ImGui::Checkbox("Slow-motion", &sc_on))
                {
                    m_speed_enabled.store(sc_on);
                    if (!sc_on && m_speed_control.is_enabled())
                    {
                        m_speed_control.disable();
                    }
                }
                // Strike through the Slow-motion checkbox label when
                // the online gate is the reason for disable.  Drawn
                // BEFORE EndDisabled would normally pop styling, but
                // here we strike before the tooltip / extra widgets
                // so the visual cue lines up with the checkbox row
                // and not with anything below it.
                if (sm_online_locked) draw_disabled_strikethrough();
                // Tooltip - picks the gate-locked text or the normal
                // explanation depending on state.  Only shown while the
                // checkbox is hovered (Slider/Presets get their own
                // tooltips below).
                if (ImGui::IsItemHovered() && sm_online_locked)
                {
                    ImGui::SetTooltip(
                        "Disabled - you're in a Ranked or Casual online\n"
                        "match and the General tab's \"Auto disable online\"\n"
                        "toggle is on.\n\n"
                        "Slow-motion will be available again when the\n"
                        "match ends, or turn the \"Auto disable online\"\n"
                        "toggle off in the General tab to override (not\n"
                        "recommended for online play).");
                }
                else if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Run the game in slow motion");

                // ---- Live cadence dot --------------------------
                // Small colored square on the SAME ROW as the
                // checkbox + slider that flickers between green
                // (this tick is a "go" tick - full game frame) and
                // red (this tick is a "stop" tick - frozen).  Lets
                // the user visually confirm the slider is producing
                // the cadence they expect, especially important at
                // very low speeds (e.g., 0.01x = one go tick every
                // 100 cockpit ticks - 1.6 sec - without this dot
                // the user has no feedback the system is alive).
                //
                // Drawn as a custom-rendered dummy item so its
                // colour reads from m_last_tick_kind (an atomic
                // updated by frame_step_apply on the cockpit thread)
                // rather than via PushStyleColor + ImGui::TextUnformatted
                // which would only convey two of the three states.
                ImGui::SameLine();
                {
                    const auto kind = static_cast<TickKind>(
                        m_last_tick_kind.load(std::memory_order_acquire));
                    ImVec4 col;
                    const char* hover_text = nullptr;
                    switch (kind)
                    {
                        case TickKind::Go:
                            col = ImVec4{0.30f, 0.90f, 0.40f, 1.0f};
                            hover_text =
                                "GO tick - this cockpit tick is\n"
                                "advancing the game by one full\n"
                                "native-dt frame.";
                            break;
                        case TickKind::Stop:
                            col = ImVec4{0.95f, 0.30f, 0.30f, 1.0f};
                            hover_text =
                                "STOP tick - this cockpit tick is\n"
                                "fully frozen.  The next 'go' tick\n"
                                "fires once the accumulator crosses\n"
                                "1.0 (slider value adds per tick).";
                            break;
                        case TickKind::Inactive:
                        default:
                            col = ImVec4{0.50f, 0.50f, 0.50f, 0.6f};
                            hover_text =
                                "Slow-motion not active or running\n"
                                "at native speed (slider >= 1.0).";
                            break;
                    }
                    const float dot = ImGui::GetTextLineHeight() * 0.6f;
                    const ImVec2 cur = ImGui::GetCursorScreenPos();
                    const float y_off = (ImGui::GetFrameHeight() - dot) * 0.5f;
                    ImVec2 dmin{cur.x + 2.0f, cur.y + y_off};
                    ImVec2 dmax{dmin.x + dot, dmin.y + dot};
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        dmin, dmax, ImGui::GetColorU32(col), 2.0f);
                    ImGui::GetWindowDrawList()->AddRect(
                        dmin, dmax, IM_COL32(0, 0, 0, 200), 2.0f, 0, 1.0f);
                    ImGui::Dummy(ImVec2(dot + 4.0f, ImGui::GetFrameHeight()));
                    if (hover_text && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", hover_text);
                }

                // Slider controls the WorldTickGate cadence.  We still allow
                // drag while off so the user can pre-set their target value
                // before flipping on.
                //
                // Range capped at 1.0 because the frame-stepped
                // implementation can't tick the world MORE than once
                // per cockpit tick - values >1.0 silently behave as
                // 1.0 (see frame_step_apply's slow-mo branch).
                //
                // Logarithmic scale gives finer resolution at the
                // low end where users spend most of their time
                // (analysis ranges 0.05x..0.25x).  Linear from
                // 0.5x..1.0x where small differences matter less.
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140.0f);
                float sv = m_speed_value.load();
                if (ImGui::SliderFloat("##speedval", &sv, 0.0f, 1.0f,
                                       "%.3fx",
                                       ImGuiSliderFlags_Logarithmic))
                {
                    if (sv < 0.0f) sv = 0.0f;
                    if (sv > 1.0f) sv = 1.0f;
                    m_speed_value.store(sv);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "Effective game-frame rate as a fraction of\n"
                    "native (60 fps).  Logarithmic so the analysis\n"
                    "range 0.05x..0.25x has finer drag resolution\n"
                    "than the casual range 0.5x..1.0x.\n\n"
                    "0.5x  = every 2nd tick is a game frame  (~30 fps)\n"
                    "0.25x = every 4th tick is a game frame  (~15 fps)\n"
                    "0.1x  = every 10th tick is a game frame (~6 fps)\n"
                    "0.05x = every 20th tick is a game frame (~3 fps)");

                // Effective rate readout - shown below the slider so
                // the user sees the cadence they're picking in
                // human-readable units without having to do mental
                // arithmetic.
                {
                    const float S_ui = m_speed_value.load();
                    if (S_ui >= 1.0f)
                    {
                        ImGui::TextDisabled(
                            "Effective: native speed (~60 fps).");
                    }
                    else if (S_ui <= 0.0f)
                    {
                        ImGui::TextDisabled(
                            "Effective: frozen (slider at 0.0x).");
                    }
                    else
                    {
                        const float fps_eff   = 60.0f * S_ui;
                        const float every_n   = 1.0f / S_ui;
                        // Round display: show "every N ticks" only
                        // for clean integer ratios; otherwise show
                        // the float ratio at one decimal.
                        if (std::abs(every_n - std::round(every_n)) < 0.05f)
                        {
                            ImGui::TextDisabled(
                                "Effective: 1 frame every %d ticks  (~%.1f fps).",
                                static_cast<int>(std::round(every_n)),
                                fps_eff);
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "Effective: 1 frame every %.2f ticks  (~%.1f fps).",
                                every_n, fps_eff);
                        }
                    }
                }

                // Preset buttons for common hitbox-analysis speeds.
                // 0.25x and 0.125x added for finer analysis without
                // having to drag the log slider; the very-slow ones
                // (0.001x / 0.01x) kept for "essentially paused but
                // still creeping" moments.
                struct Preset { const char* label; float value; };
                static const Preset kPresets[] = {
                    {"Freeze##sp",   0.0f   },
                    {"0.01x##sp",    0.01f  },
                    {"0.1x##sp",     0.1f   },
                    {"0.125x##sp",   0.125f },
                    {"0.25x##sp",    0.25f  },
                    {"0.5x##sp",     0.5f   },
                    {"1x##sp",       1.0f   },
                };
                for (const auto& p : kPresets)
                {
                    if (ImGui::SmallButton(p.label))
                    {
                        m_speed_value.store(p.value);
                    }
                    ImGui::SameLine();
                }
                ImGui::NewLine();

                if (sm_online_locked) ImGui::EndDisabled();

                if (sc_on)
                {
                    ImGui::TextDisabled(
                        "(gate-driven cadence at %.3fx; SpeedControl idle)",
                        m_speed_value.load());
                }
            }

    }

