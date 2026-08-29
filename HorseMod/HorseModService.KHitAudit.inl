    void push_hud_text_event(const char* msg)
    {
        if (!msg) return;
        auto& slot   = m_hud_text_events[m_hud_text_event_head];
        const auto n = std::min<size_t>(
            std::strlen(msg), sizeof(slot.text) - 1);
        std::memcpy(slot.text, msg, n);
        slot.text[n] = '\0';
        slot.text_len = static_cast<int>(n);
        slot.time     = ImGui::GetTime();
        m_hud_text_event_head =
            (m_hud_text_event_head + 1) % kHudTextEventCount;
    }

    // Look up the current show-flag for (player, list).  Inlined in the
    // hot path; pi outside [0,1] falls through to P1's settings.
    bool shouldShow(int pi, Horse::KHitList list) const
    {
        const bool is_p2 = (pi == 1);
        switch (list)
        {
            case Horse::KHitList::Hurtbox:
                return (is_p2 ? m_show_p2_hurt : m_show_p1_hurt).load();
            case Horse::KHitList::Attack:
                return (is_p2 ? m_show_p2_atk  : m_show_p1_atk ).load();
            case Horse::KHitList::Body:
                return (is_p2 ? m_show_p2_body : m_show_p1_body).load();
        }
        return false;
    }

    static bool canMatterThisFrame(const Horse::KHitDraw& d)
    {
        switch (d.list)
        {
            case Horse::KHitList::Attack:
                return d.is_per_frame_active && d.attacker_can_strike_engine;
            case Horse::KHitList::Hurtbox:
                return d.classifier_addressable &&
                       d.overlap_active &&
                       d.defender_can_react_engine;
            case Horse::KHitList::Body:
                return false;
        }
        return false;
    }

    static bool canRenderAttackShapeThisFrame(const Horse::KHitDraw& d)
    {
        if (d.list != Horse::KHitList::Attack ||
            !d.geom_active ||
            !d.attacker_can_strike_engine ||
            d.attack_mask_stale)
        {
            return false;
        }

        const bool primary_mask_selected =
            d.active_move_valid &&
            ((d.slot_bit_mask & d.primary_attack_mask) != 0);
        const bool primary_window_open =
            d.classify_enabled &&
            (d.attack_in_master_window ||
             d.engine_phase == Horse::KHitAttackPhase::Active);
        const bool primary_visual_ready =
            primary_mask_selected && primary_window_open;
        const bool alt_visual_ready =
            d.alt_classify_open &&
            ((d.slot_bit_mask & d.alt_attack_mask) != 0);

        // Deliberately ignore +0x16EB/+0x16FE here. Those bytes mean
        // "this active hitbox already connected and cannot deal damage
        // again yet"; for display, users expect the same active shape
        // they would have seen on whiff.
        return primary_visual_ready || alt_visual_ready;
    }

    static bool read_lux_battle_game_frame(uint32_t& out_frame) noexcept
    {
        constexpr uintptr_t kFrameCounterRVA = 0x470D0C4;
        const uintptr_t base = Horse::NativeBinding::imageBase();
        return base != 0 && Horse::SafeReadUInt32(
            reinterpret_cast<const void*>(base + kFrameCounterRVA),
            &out_frame);
    }

    static bool mask_has_slot(uint64_t mask, int slot) noexcept
    {
        return slot >= 0 && slot < 64 &&
               (((mask >> static_cast<unsigned>(slot)) & 1ull) != 0);
    }

    static bool khit_audit_move_matches(int wanted,
                                        bool has_move,
                                        int packed_move,
                                        int move_id_low11) noexcept
    {
        return wanted < 0 ||
               (has_move &&
                (packed_move == wanted || move_id_low11 == wanted));
    }

    bool khit_audit_matches_move_filter(
        const Horse::KHitDraw& d,
        const Horse::KHitWalker::LaneSnapshot* attacker_lane) const
    {
        if (m_khit_sphere_audit_filter_move.load(std::memory_order_relaxed))
        {
            const int wanted =
                m_khit_sphere_audit_move.load(std::memory_order_relaxed);
            if (d.list == Horse::KHitList::Hurtbox && attacker_lane)
            {
                const int packed =
                    static_cast<int>(attacker_lane->packed_move);
                const int low11 = packed & 0x7ff;
                return khit_audit_move_matches(
                    wanted, attacker_lane->has_move, packed, low11);
            }

            return khit_audit_move_matches(
                wanted,
                d.has_move_identity,
                static_cast<int>(d.active_packed_move),
                static_cast<int>(d.active_move_id_low11));
        }

        return true;
    }

    bool khit_audit_matches_slot_filter(const Horse::KHitDraw& d) const
    {
        if (m_khit_sphere_audit_filter_slots.load(std::memory_order_relaxed))
        {
            const int slot_a =
                m_khit_sphere_audit_slot_a.load(std::memory_order_relaxed);
            const int slot_b =
                m_khit_sphere_audit_slot_b.load(std::memory_order_relaxed);
            auto matches_slot = [&](int slot) {
                return slot >= 0 && slot < 64 &&
                       (static_cast<int>(d.bone_id_internal) == slot ||
                        mask_has_slot(d.slot_bit_mask, slot) ||
                        (d.defender_hurtbox_mask_valid &&
                         mask_has_slot(d.defender_hurtbox_attack_mask, slot)));
            };
            if (!matches_slot(slot_a) && !matches_slot(slot_b))
                return false;
        }

        return true;
    }

    bool khit_audit_matches_filter(
        const Horse::KHitDraw& d,
        const Horse::KHitWalker::LaneSnapshot* attacker_lane) const
    {
        return khit_audit_matches_move_filter(d, attacker_lane) &&
               khit_audit_matches_slot_filter(d);
    }

    static bool can_expose_khit_attack_for_audit(const Horse::KHitDraw& d)
    {
        return d.list == Horse::KHitList::Attack &&
               d.geom_active &&
               (d.attack_mask_selected ||
                d.attack_mask_stale ||
                d.accepted_overlap_this_frame ||
                d.accepted_exact_overlap_this_frame) &&
               d.attacker_can_strike_engine;
    }

    enum class KHitAuditLogBucket : uint8_t
    {
        Attack,
        HurtResult,
        OverlapPair,
        Calibration,
        AttackCluster,
    };

    bool consume_khit_audit_log_slot(KHitAuditLogBucket bucket)
    {
        switch (bucket)
        {
            case KHitAuditLogBucket::Attack:
                if (m_khit_audit_attack_logs_this_frame >=
                    kMaxKHitAuditAttackLogsPerFrame)
                    return false;
                ++m_khit_audit_attack_logs_this_frame;
                return true;
            case KHitAuditLogBucket::HurtResult:
                if (m_khit_audit_hurt_logs_this_frame >=
                    kMaxKHitAuditHurtLogsPerFrame)
                    return false;
                ++m_khit_audit_hurt_logs_this_frame;
                return true;
            case KHitAuditLogBucket::OverlapPair:
                if (m_khit_audit_pair_logs_this_frame >=
                    kMaxKHitAuditPairLogsPerFrame)
                    return false;
                ++m_khit_audit_pair_logs_this_frame;
                return true;
            case KHitAuditLogBucket::Calibration:
                if (m_khit_audit_calib_logs_this_frame >=
                    kMaxKHitAuditCalibLogsPerFrame)
                    return false;
                ++m_khit_audit_calib_logs_this_frame;
                return true;
            case KHitAuditLogBucket::AttackCluster:
                if (m_khit_audit_cluster_logs_this_frame >=
                    kMaxKHitAuditClusterLogsPerFrame)
                    return false;
                ++m_khit_audit_cluster_logs_this_frame;
                return true;
        }
        return false;
    }

    static Horse::FVec3 midpoint(const Horse::FVec3& a,
                                 const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{
            (a.X + b.X) * 0.5f,
            (a.Y + b.Y) * 0.5f,
            (a.Z + b.Z) * 0.5f,
        };
    }

    static Horse::FVec3 centroid3(const Horse::FVec3& a,
                                  const Horse::FVec3& b,
                                  const Horse::FVec3& c) noexcept
    {
        return Horse::FVec3{
            (a.X + b.X + c.X) / 3.0f,
            (a.Y + b.Y + c.Y) / 3.0f,
            (a.Z + b.Z + c.Z) / 3.0f,
        };
    }

    static float distance3(const Horse::FVec3& a,
                           const Horse::FVec3& b) noexcept
    {
        const float dx = a.X - b.X;
        const float dy = a.Y - b.Y;
        const float dz = a.Z - b.Z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    static Horse::FVec3 add3(const Horse::FVec3& a,
                             const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{a.X + b.X, a.Y + b.Y, a.Z + b.Z};
    }

    static Horse::FVec3 sub3(const Horse::FVec3& a,
                             const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{a.X - b.X, a.Y - b.Y, a.Z - b.Z};
    }

    static Horse::FVec3 scale3(const Horse::FVec3& v,
                               float scale) noexcept
    {
        return Horse::FVec3{v.X * scale, v.Y * scale, v.Z * scale};
    }

    static bool normalize3(const Horse::FVec3& v,
                           Horse::FVec3& out) noexcept
    {
        const float len = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        if (len <= 0.001f)
            return false;
        out = scale3(v, 1.0f / len);
        return true;
    }

    static float length3(const Horse::FVec3& v) noexcept
    {
        return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    }

    static bool is_sane_vec3(const Horse::FVec3& v) noexcept
    {
        constexpr float kMaxReasonableCoord = 10000000.0f;
        return std::isfinite(v.X) && std::isfinite(v.Y) &&
               std::isfinite(v.Z) &&
               std::fabsf(v.X) < kMaxReasonableCoord &&
               std::fabsf(v.Y) < kMaxReasonableCoord &&
               std::fabsf(v.Z) < kMaxReasonableCoord;
    }

    static bool is_meaningful_offset(const Horse::FVec3& offset) noexcept
    {
        return length3(offset) > 0.001f;
    }

    static void apply_render_offset_to_khit_draw(
        Horse::KHitDraw& d,
        const Horse::FVec3& offset) noexcept
    {
        if (!is_meaningful_offset(offset))
            return;

        switch (d.kind)
        {
            case Horse::KHitKind::Sphere:
                d.centre = add3(d.centre, offset);
                break;

            case Horse::KHitKind::AreaSpine:
                d.spine_p1_world = add3(d.spine_p1_world, offset);
                d.spine_p2_world = add3(d.spine_p2_world, offset);
                d.prev_p1_world = add3(d.prev_p1_world, offset);
                d.prev_p2_world = add3(d.prev_p2_world, offset);
                break;

            case Horse::KHitKind::FixAreaTri:
                for (Horse::FVec3& corner : d.corners)
                    corner = add3(corner, offset);
                break;
        }
    }

    static void apply_render_offset_to_khit_draws(
        std::vector<Horse::KHitDraw> (&draws)[2],
        const Horse::FVec3& offset) noexcept
    {
        if (!is_meaningful_offset(offset))
            return;

        for (auto& player_draws : draws)
        {
            for (Horse::KHitDraw& d : player_draws)
                apply_render_offset_to_khit_draw(d, offset);
        }
    }

    static float max_float(float a, float b) noexcept
    {
        return (a > b) ? a : b;
    }

    struct KHitAuditShapeMetrics
    {
        Horse::FVec3 native_center{};
        Horse::FVec3 ue_center{};
        float native_radius = 0.0f;
        float ue_radius = 0.0f;
    };

    struct KHitAuditCharaPose
    {
        bool ok = false;
        Horse::FVec3 native_pos{};
        Horse::FVec3 ue_pos{};
        uint8_t slot_byte = 0xff;
        bool distance_ok = false;
        float opponent_distance = 0.0f;
    };

    static Horse::FVec3 audit_battle_to_ue_render_world(
        const Horse::FVec3& battleWorld) noexcept
    {
        // Keep audit coordinates identical to KHitWalker render coordinates:
        // native KHit world buffers already use battle Y as vertical.
        return Horse::FVec3{
            battleWorld.X * Horse::kBattleToUE,
            battleWorld.Z * Horse::kBattleToUE,
            battleWorld.Y * Horse::kBattleToUE
        };
    }

    static float dot3(const Horse::FVec3& a,
                      const Horse::FVec3& b) noexcept
    {
        return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    }

    static Horse::FVec3 cross3(const Horse::FVec3& a,
                               const Horse::FVec3& b) noexcept
    {
        return Horse::FVec3{
            a.Y * b.Z - a.Z * b.Y,
            a.Z * b.X - a.X * b.Z,
            a.X * b.Y - a.Y * b.X
        };
    }

    static KHitAuditCharaPose read_khit_audit_chara_pose(
        void* chara) noexcept
    {
        KHitAuditCharaPose pose{};
        if (!chara)
            return pose;

        auto* base = reinterpret_cast<uint8_t*>(chara);
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint8_t slot_byte = 0xff;
        const bool ok =
            Horse::SafeReadFloat(base + 0x0C0, &x) &&
            Horse::SafeReadFloat(base + 0x0C4, &y) &&
            Horse::SafeReadFloat(base + 0x0C8, &z) &&
            Horse::SafeReadUInt8(base + 0x23C, &slot_byte);
        if (!ok)
            return pose;

        pose.ok = true;
        pose.native_pos = Horse::FVec3{x, y, z};
        pose.ue_pos = audit_battle_to_ue_render_world(pose.native_pos);
        pose.slot_byte = slot_byte;
        pose.distance_ok =
            Horse::KHitWalker::readOpponentDistance(
                chara, pose.opponent_distance);
        return pose;
    }

    KHitRenderCalibrationPoint read_khit_render_calibration_point(
        void* chara,
        int player_index)
    {
        KHitRenderCalibrationPoint point{};
        const KHitAuditCharaPose pose = read_khit_audit_chara_pose(chara);
        if (pose.ok && is_sane_vec3(pose.native_pos) &&
            is_sane_vec3(pose.ue_pos))
        {
            point.native_ok = true;
            point.native_root = pose.native_pos;
            point.converted_root = pose.ue_pos;
        }

        if (chara)
        {
            auto* obj = reinterpret_cast<UObject*>(chara);
            if (obj && UObject::IsReal(obj))
            {
                Horse::Obj actor{obj};
                const int fn_index =
                    (player_index >= 0 && player_index < 2)
                        ? player_index
                        : 0;
                const Horse::FVec3 actor_root =
                    actor.callVec3Any(m_fn_khit_actor_location[fn_index],
                                      L"GetActorLocation",
                                      L"K2_GetActorLocation");
                if (is_sane_vec3(actor_root))
                {
                    point.actor_ok = true;
                    point.actor_root = actor_root;
                }
            }
        }

        if (point.native_ok && point.actor_ok)
        {
            point.delta = sub3(point.actor_root, point.converted_root);
            point.delta_ok = is_sane_vec3(point.delta);
        }

        return point;
    }

    KHitRenderCalibrationFrame read_khit_render_calibration_frame(
        void* const (&slot_charas)[2])
    {
        KHitRenderCalibrationFrame frame{};
        frame.point[0] =
            read_khit_render_calibration_point(slot_charas[0], 0);
        frame.point[1] =
            read_khit_render_calibration_point(slot_charas[1], 1);
        return frame;
    }

    void update_khit_render_calibration(
        KHitRenderCalibrationFrame& frame) noexcept
    {
        constexpr float kMaxPlayerDeltaDisagreementCm = 25.0f;
        constexpr float kMaxRollingOffsetDriftCm = 35.0f;
        constexpr int kMinStableSamples = 10;
        constexpr int kMaxStableSamples = 60;

        frame.samples = m_khit_render_calibration.samples;

        if (!frame.point[0].delta_ok || !frame.point[1].delta_ok)
        {
            m_khit_render_calibration.valid = false;
            m_khit_render_calibration.samples = 0;
            frame.status = L"missing";
            frame.samples = 0;
            return;
        }

        frame.has_common_delta = true;
        frame.delta_distance =
            distance3(frame.point[0].delta, frame.point[1].delta);
        frame.common_delta =
            midpoint(frame.point[0].delta, frame.point[1].delta);

        if (frame.delta_distance > kMaxPlayerDeltaDisagreementCm)
        {
            m_khit_render_calibration.valid = false;
            m_khit_render_calibration.samples = 0;
            frame.consistent = false;
            frame.status = L"inconsistent";
            frame.samples = 0;
            return;
        }

        frame.consistent = true;
        KHitRenderCalibrationState& state = m_khit_render_calibration;
        const bool has_prior = state.samples > 0;
        const float drift = has_prior
            ? distance3(frame.common_delta, state.offset)
            : 0.0f;
        if (has_prior && drift > kMaxRollingOffsetDriftCm)
        {
            state.valid = false;
            state.samples = 1;
            state.offset = frame.common_delta;
            frame.status = L"warming";
            frame.samples = state.samples;
            return;
        }

        if (!has_prior)
        {
            state.offset = frame.common_delta;
            state.samples = 1;
        }
        else
        {
            const int next_samples =
                (std::min)(state.samples + 1, kMaxStableSamples);
            const float weight_old =
                static_cast<float>(next_samples - 1) /
                static_cast<float>(next_samples);
            const float weight_new = 1.0f / static_cast<float>(next_samples);
            state.offset = add3(scale3(state.offset, weight_old),
                                scale3(frame.common_delta, weight_new));
            state.samples = next_samples;
        }

        state.valid = state.samples >= kMinStableSamples;
        frame.samples = state.samples;
        frame.applied = state.valid;
        frame.active_offset = state.valid ? state.offset : Horse::FVec3{};
        frame.status = state.valid ? L"applied" : L"warming";
    }

    static KHitAuditShapeMetrics audit_shape_metrics(
        const Horse::KHitDraw& d) noexcept
    {
        KHitAuditShapeMetrics m{};
        switch (d.kind)
        {
            case Horse::KHitKind::Sphere:
                m.native_center = d.native_centre;
                m.ue_center = d.centre;
                m.native_radius = d.native_radius;
                m.ue_radius = d.radius;
                break;

            case Horse::KHitKind::AreaSpine:
                m.native_center =
                    midpoint(d.native_spine_p1_world,
                             d.native_spine_p2_world);
                m.ue_center = midpoint(d.spine_p1_world,
                                       d.spine_p2_world);
                m.native_radius = max_float(
                    distance3(m.native_center, d.native_spine_p1_world),
                    distance3(m.native_center, d.native_spine_p2_world));
                m.ue_radius = max_float(
                    distance3(m.ue_center, d.spine_p1_world),
                    distance3(m.ue_center, d.spine_p2_world));
                if (d.has_prev_spine)
                {
                    const Horse::FVec3 native_prev_center =
                        midpoint(d.native_prev_p1_world,
                                 d.native_prev_p2_world);
                    const Horse::FVec3 ue_prev_center =
                        midpoint(d.prev_p1_world, d.prev_p2_world);
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, native_prev_center));
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, d.native_prev_p1_world));
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, d.native_prev_p2_world));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, ue_prev_center));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, d.prev_p1_world));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, d.prev_p2_world));
                }
                break;

            case Horse::KHitKind::FixAreaTri:
                m.native_center = centroid3(d.native_corners[0],
                                            d.native_corners[1],
                                            d.native_corners[2]);
                m.ue_center = centroid3(d.corners[0],
                                        d.corners[1],
                                        d.corners[2]);
                for (int i = 0; i < 3; ++i)
                {
                    m.native_radius = max_float(
                        m.native_radius,
                        distance3(m.native_center, d.native_corners[i]));
                    m.ue_radius = max_float(
                        m.ue_radius,
                        distance3(m.ue_center, d.corners[i]));
                }
                break;
        }
        return m;
    }

    static bool khit_sphere_pair_overlaps_native(
        const Horse::KHitDraw& attack,
        const Horse::KHitDraw& hurt,
        float* out_native_margin = nullptr) noexcept
    {
        if (attack.kind != Horse::KHitKind::Sphere ||
            hurt.kind != Horse::KHitKind::Sphere)
        {
            if (out_native_margin)
                *out_native_margin = 0.0f;
            return false;
        }

        const float native_dist =
            distance3(attack.native_centre, hurt.native_centre);
        const float native_rsum =
            attack.native_radius + hurt.native_radius;
        const float native_margin = native_rsum - native_dist;
        if (out_native_margin)
            *out_native_margin = native_margin;

        // The defender's accepted mask is keyed by attacker slot, not by
        // individual KHit node.  Require exact local sphere/sphere contact
        // before crediting a same-slot sphere as the accepted visual pair.
        constexpr float kNativeOverlapEpsilon = 0.005f;
        return native_margin >= -kNativeOverlapEpsilon;
    }

    static bool khit_pair_has_exact_geometry(
        const Horse::KHitDraw& attack,
        const Horse::KHitDraw& hurt) noexcept
    {
        if (attack.kind == Horse::KHitKind::Sphere &&
            hurt.kind == Horse::KHitKind::Sphere)
        {
            return khit_sphere_pair_overlaps_native(attack, hurt);
        }

        return false;
    }

    static bool khit_pair_geometry_plausible(
        const Horse::KHitDraw& attack,
        const Horse::KHitDraw& hurt) noexcept
    {
        if (attack.kind == Horse::KHitKind::Sphere &&
            hurt.kind == Horse::KHitKind::Sphere)
        {
            return khit_pair_has_exact_geometry(attack, hurt);
        }

        // Area/fix-area native incoming masks prove that an attacker slot
        // touched this hurtbox slot, but they do not identify a unique node
        // when same-slot non-sphere candidates exist. Treat these as broad
        // attribution only; mark_khit_accepted_overlap_candidates decides
        // whether a pair can be promoted to reaction-exact.
        return true;
    }

    void service_khit_sphere_audit_frame(bool have_game_frame,
                                         uint32_t game_frame)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
        {
            m_have_sphere_audit_frame = false;
            m_khit_audit_attack_logs_this_frame = 0;
            m_khit_audit_hurt_logs_this_frame = 0;
            m_khit_audit_pair_logs_this_frame = 0;
            m_khit_audit_calib_logs_this_frame = 0;
            m_khit_audit_cluster_logs_this_frame = 0;
            return;
        }

        const uint32_t audit_frame = have_game_frame
            ? game_frame
            : static_cast<uint32_t>(m_update_calls);
        if (!m_have_sphere_audit_frame ||
            audit_frame != m_last_sphere_audit_frame)
        {
            m_have_sphere_audit_frame = true;
            m_last_sphere_audit_frame = audit_frame;
            m_khit_audit_attack_logs_this_frame = 0;
            m_khit_audit_hurt_logs_this_frame = 0;
            m_khit_audit_pair_logs_this_frame = 0;
            m_khit_audit_calib_logs_this_frame = 0;
            m_khit_audit_cluster_logs_this_frame = 0;
        }
    }

    void maybe_log_khit_audit(
        const Horse::KHitDraw& d,
        int player,
        bool matters_this_frame,
        bool have_game_frame,
        uint32_t game_frame,
        Horse::LineBatcherSlot renderer_slot,
        const Horse::KHitWalker::LaneSnapshot* attacker_lane)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
            return;

        const bool is_attack_audit =
            d.list == Horse::KHitList::Attack &&
            can_expose_khit_attack_for_audit(d);
        const bool has_raw_or_final_reaction =
            d.raw_reaction_state != 0 ||
            d.final_hit_result_code != 0;
        const bool slot_filter_enabled =
            m_khit_sphere_audit_filter_slots.load(std::memory_order_relaxed);
        const bool slot_filter_match =
            !slot_filter_enabled || khit_audit_matches_slot_filter(d);
        const bool accepted_watch_summary =
            slot_filter_enabled &&
            slot_filter_match &&
            d.accepted_overlap_this_frame;
        const bool is_hit_result_audit =
            d.list == Horse::KHitList::Hurtbox &&
            (has_raw_or_final_reaction ||
             d.reaction_overlap_this_frame ||
             d.reaction_hot ||
             accepted_watch_summary);
        if (!is_attack_audit && !is_hit_result_audit)
            return;

        const bool move_filter_match =
            khit_audit_matches_move_filter(d, attacker_lane);
        const bool has_nonzero_incoming_mask =
            d.defender_hurtbox_mask_valid &&
            d.defender_hurtbox_attack_mask != 0;
        if (!move_filter_match &&
            !(is_hit_result_audit &&
              (has_nonzero_incoming_mask || has_raw_or_final_reaction)))
            return;

        if (is_hit_result_audit)
        {
            if (!has_raw_or_final_reaction &&
                !d.reaction_overlap_this_frame &&
                !accepted_watch_summary)
            {
                return;
            }

            if (!consume_khit_audit_log_slot(
                    KHitAuditLogBucket::HurtResult))
                return;

            const int attacker_player = (player == 0) ? 2 : 1;
            const bool attacker_has_move =
                attacker_lane && attacker_lane->has_move;
            const int attacker_packed = attacker_has_move
                ? static_cast<int>(attacker_lane->packed_move)
                : -1;
            const int attacker_low11 = attacker_has_move
                ? (attacker_packed & 0x7ff)
                : -1;
            const wchar_t* summary_kind =
                d.raw_reaction_state != 0
                    ? L"raw"
                    : (d.final_hit_result_code != 0
                        ? L"final"
                        : (d.reaction_overlap_this_frame
                            ? L"reaction_candidate"
                            : L"accepted"));
            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.HurtResult] frame_ok={} frame={} "
                    "summary={} def_p={} atk_p={} atk_move=0x{:04x}/{} "
                    "hurt_node=0x{:x} "
                    "hurt_slot={} raw_react={} sticky_react={} final={} "
                    "mask_valid={} incoming=0x{:016x} filter_slots=({}, {}) "
                    "move_filter_match={} phase={} defender_can_react={} "
                    "overlap={} addressable={} accepted={} accepted_bits=0x{:016x} "
                    "accepted_pairs={} accepted_ambiguous={} "
                    "accepted_exact={} accepted_exact_bits=0x{:016x} "
                    "accepted_exact_pairs={} "
                    "reaction={} reaction_bits=0x{:016x} "
                    "reaction_pairs={} reaction_unique={} reaction_ambiguous={} "
                    "reaction_unresolved={}\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                summary_kind,
                player + 1,
                attacker_player,
                attacker_has_move
                    ? static_cast<unsigned>(attacker_packed & 0xffff)
                    : 0xffffu,
                attacker_low11,
                d.source_node,
                d.hurtbox_slot,
                d.raw_reaction_state,
                d.reaction_state,
                d.final_hit_result_code,
                d.defender_hurtbox_mask_valid,
                d.defender_hurtbox_attack_mask,
                m_khit_sphere_audit_slot_a.load(std::memory_order_relaxed),
                m_khit_sphere_audit_slot_b.load(std::memory_order_relaxed),
                move_filter_match,
                static_cast<int>(d.engine_phase),
                d.defender_can_react_engine,
                d.overlap_active,
                d.classifier_addressable,
                d.accepted_overlap_this_frame,
                d.accepted_overlap_matched_bits,
                d.accepted_overlap_pair_count,
                d.accepted_overlap_ambiguous,
                d.accepted_exact_overlap_this_frame,
                d.accepted_exact_overlap_matched_bits,
                d.accepted_exact_overlap_pair_count,
                d.reaction_overlap_this_frame,
                d.reaction_overlap_matched_bits,
                d.reaction_overlap_pair_count,
                d.raw_reaction_state != 0 &&
                    d.reaction_overlap_pair_count == 1,
                d.reaction_overlap_ambiguous ||
                    (d.raw_reaction_state != 0 &&
                     d.reaction_overlap_pair_count > 1),
                d.raw_reaction_state != 0 &&
                    d.reaction_overlap_pair_count == 0 &&
                    !d.reaction_overlap_ambiguous);
            return;
        }

        if (!move_filter_match)
            return;
        if (!khit_audit_matches_slot_filter(d))
            return;
        if (!consume_khit_audit_log_slot(KHitAuditLogBucket::Attack))
            return;

        if (d.kind == Horse::KHitKind::Sphere)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.Attack] frame_ok={} frame={} p={} "
                    "move=0x{:04x}/{} sub=0x{:04x} node=0x{:x} kind=sphere "
                    "slot={} renderer={} matters={} geom={} mask_selected={} "
                    "active_move_valid={} mask_stale={} "
                    "phase={} classifier_ready={} gates=({},{},{},{}) "
                    "primary=0x{:016x} alt_open={} alt=0x{:016x} "
                    "slot_bit=0x{:016x} accepted={} "
                    "accepted_bits=0x{:016x} accepted_pairs={} "
                    "accepted_exact={} accepted_exact_bits=0x{:016x} "
                    "accepted_exact_pairs={} "
                    "reaction={} reaction_bits=0x{:016x} reaction_pairs={} "
                    "native=({:.3f},{:.3f},{:.3f}) "
                    "ue=({:.1f},{:.1f},{:.1f}) local=({:.3f},{:.3f},{:.3f}) "
                    "auth_local=({:.3f},{:.3f},{:.3f}) "
                    "local_delta=({:.3f},{:.3f},{:.3f}) "
                    "radius_native={:.3f} "
                    "radius_auth={:.3f} radius_scale={:.3f} "
                    "anim_modified={} radius_ue={:.1f}\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                player + 1,
                d.has_move_identity ? d.active_packed_move : 0xFFFFu,
                d.has_move_identity
                    ? static_cast<int>(d.active_move_id_low11)
                    : -1,
                d.move_subframe_id,
                d.source_node,
                static_cast<int>(d.bone_id_internal),
                static_cast<int>(renderer_slot),
                matters_this_frame,
                d.geom_active,
                d.attack_mask_selected,
                d.active_move_valid,
                d.attack_mask_stale,
                static_cast<int>(d.engine_phase),
                d.attack_classifier_ready,
                d.classify_enabled,
                d.attack_in_master_window,
                d.attack_lockout_a,
                d.attack_lockout_b,
                d.primary_attack_mask,
                d.alt_classify_open,
                d.alt_attack_mask,
                d.slot_bit_mask,
                d.accepted_overlap_this_frame,
                d.accepted_overlap_matched_bits,
                d.accepted_overlap_pair_count,
                d.accepted_exact_overlap_this_frame,
                d.accepted_exact_overlap_matched_bits,
                d.accepted_exact_overlap_pair_count,
                d.reaction_overlap_this_frame,
                d.reaction_overlap_matched_bits,
                d.reaction_overlap_pair_count,
                d.native_centre.X,
                d.native_centre.Y,
                d.native_centre.Z,
                d.centre.X,
                d.centre.Y,
                d.centre.Z,
                d.native_live_local_centre.X,
                d.native_live_local_centre.Y,
                d.native_live_local_centre.Z,
                d.native_authored_local_centre.X,
                d.native_authored_local_centre.Y,
                d.native_authored_local_centre.Z,
                d.sphere_live_local_delta.X,
                d.sphere_live_local_delta.Y,
                d.sphere_live_local_delta.Z,
                d.native_radius,
                d.native_authored_radius,
                d.sphere_live_radius_scale,
                d.sphere_anim_modified,
                d.radius);
            return;
        }

        if (d.kind == Horse::KHitKind::AreaSpine)
        {
            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.Attack] frame_ok={} frame={} p={} "
                    "move=0x{:04x}/{} sub=0x{:04x} node=0x{:x} kind=area "
                    "slot={} renderer={} matters={} geom={} mask_selected={} "
                    "active_move_valid={} mask_stale={} "
                    "phase={} classifier_ready={} gates=({},{},{},{}) "
                    "primary=0x{:016x} alt_open={} alt=0x{:016x} "
                    "slot_bit=0x{:016x} accepted={} "
                    "accepted_bits=0x{:016x} accepted_pairs={} "
                    "accepted_exact={} accepted_exact_bits=0x{:016x} "
                    "accepted_exact_pairs={} "
                    "reaction={} reaction_bits=0x{:016x} reaction_pairs={} "
                    "native_p1=({:.3f},{:.3f},{:.3f}) "
                    "native_p2=({:.3f},{:.3f},{:.3f}) "
                    "native_prev_p1=({:.3f},{:.3f},{:.3f}) "
                    "native_prev_p2=({:.3f},{:.3f},{:.3f}) has_prev={} "
                    "ue_p1=({:.1f},{:.1f},{:.1f}) ue_p2=({:.1f},{:.1f},{:.1f})\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                player + 1,
                d.has_move_identity ? d.active_packed_move : 0xFFFFu,
                d.has_move_identity
                    ? static_cast<int>(d.active_move_id_low11)
                    : -1,
                d.move_subframe_id,
                d.source_node,
                static_cast<int>(d.bone_id_internal),
                static_cast<int>(renderer_slot),
                matters_this_frame,
                d.geom_active,
                d.attack_mask_selected,
                d.active_move_valid,
                d.attack_mask_stale,
                static_cast<int>(d.engine_phase),
                d.attack_classifier_ready,
                d.classify_enabled,
                d.attack_in_master_window,
                d.attack_lockout_a,
                d.attack_lockout_b,
                d.primary_attack_mask,
                d.alt_classify_open,
                d.alt_attack_mask,
                d.slot_bit_mask,
                d.accepted_overlap_this_frame,
                d.accepted_overlap_matched_bits,
                d.accepted_overlap_pair_count,
                d.accepted_exact_overlap_this_frame,
                d.accepted_exact_overlap_matched_bits,
                d.accepted_exact_overlap_pair_count,
                d.reaction_overlap_this_frame,
                d.reaction_overlap_matched_bits,
                d.reaction_overlap_pair_count,
                d.native_spine_p1_world.X,
                d.native_spine_p1_world.Y,
                d.native_spine_p1_world.Z,
                d.native_spine_p2_world.X,
                d.native_spine_p2_world.Y,
                d.native_spine_p2_world.Z,
                d.native_prev_p1_world.X,
                d.native_prev_p1_world.Y,
                d.native_prev_p1_world.Z,
                d.native_prev_p2_world.X,
                d.native_prev_p2_world.Y,
                d.native_prev_p2_world.Z,
                d.has_prev_spine,
                d.spine_p1_world.X,
                d.spine_p1_world.Y,
                d.spine_p1_world.Z,
                d.spine_p2_world.X,
                d.spine_p2_world.Y,
                d.spine_p2_world.Z);
            return;
        }

        Output::send<LogLevel::Default>(
            STR("[HorseMod.KHitAudit.Attack] frame_ok={} frame={} p={} "
                "move=0x{:04x}/{} sub=0x{:04x} node=0x{:x} kind=fixarea "
                "slot={} renderer={} matters={} geom={} mask_selected={} "
                "active_move_valid={} mask_stale={} "
                "phase={} classifier_ready={} gates=({},{},{},{}) "
                "primary=0x{:016x} alt_open={} alt=0x{:016x} "
                "slot_bit=0x{:016x} accepted={} "
                "accepted_bits=0x{:016x} accepted_pairs={} "
                "accepted_exact={} accepted_exact_bits=0x{:016x} "
                "accepted_exact_pairs={} "
                "reaction={} reaction_bits=0x{:016x} reaction_pairs={} "
                "native_p1=({:.3f},{:.3f},{:.3f}) "
                "native_p2=({:.3f},{:.3f},{:.3f}) "
                "native_p3=({:.3f},{:.3f},{:.3f}) "
                "ue_p1=({:.1f},{:.1f},{:.1f}) ue_p2=({:.1f},{:.1f},{:.1f}) "
                "ue_p3=({:.1f},{:.1f},{:.1f})\n"),
            have_game_frame,
            have_game_frame ? game_frame : 0,
            player + 1,
            d.has_move_identity ? d.active_packed_move : 0xFFFFu,
            d.has_move_identity
                ? static_cast<int>(d.active_move_id_low11)
                : -1,
            d.move_subframe_id,
            d.source_node,
            static_cast<int>(d.bone_id_internal),
            static_cast<int>(renderer_slot),
            matters_this_frame,
            d.geom_active,
            d.attack_mask_selected,
            d.active_move_valid,
            d.attack_mask_stale,
            static_cast<int>(d.engine_phase),
            d.attack_classifier_ready,
            d.classify_enabled,
            d.attack_in_master_window,
            d.attack_lockout_a,
            d.attack_lockout_b,
            d.primary_attack_mask,
            d.alt_classify_open,
            d.alt_attack_mask,
            d.slot_bit_mask,
            d.accepted_overlap_this_frame,
            d.accepted_overlap_matched_bits,
            d.accepted_overlap_pair_count,
            d.accepted_exact_overlap_this_frame,
            d.accepted_exact_overlap_matched_bits,
            d.accepted_exact_overlap_pair_count,
            d.reaction_overlap_this_frame,
            d.reaction_overlap_matched_bits,
            d.reaction_overlap_pair_count,
            d.native_corners[0].X,
            d.native_corners[0].Y,
            d.native_corners[0].Z,
            d.native_corners[1].X,
            d.native_corners[1].Y,
            d.native_corners[1].Z,
            d.native_corners[2].X,
            d.native_corners[2].Y,
            d.native_corners[2].Z,
            d.corners[0].X,
            d.corners[0].Y,
            d.corners[0].Z,
            d.corners[1].X,
            d.corners[1].Y,
            d.corners[1].Z,
            d.corners[2].X,
            d.corners[2].Y,
                            d.corners[2].Z);
    }

    static const char* khit_kind_short(Horse::KHitKind kind) noexcept
    {
        switch (kind)
        {
            case Horse::KHitKind::Sphere:     return "S";
            case Horse::KHitKind::AreaSpine:  return "A";
            case Horse::KHitKind::FixAreaTri: return "F";
        }
        return "?";
    }

    void maybe_log_khit_attack_clusters(
        const std::vector<Horse::KHitDraw> (&draws)[2],
        bool have_game_frame,
        uint32_t game_frame,
        Horse::LineBatcherSlot hit_renderer_slot)
    {
        if (!m_khit_sphere_audit.load(std::memory_order_relaxed))
            return;

        for (int player = 0; player < 2; ++player)
        {
            std::vector<const Horse::KHitDraw*> nodes;
            nodes.reserve(draws[player].size());
            int selected_count = 0;
            int ready_count = 0;
            int stale_count = 0;
            int modified_sphere_count = 0;

            for (const Horse::KHitDraw& d : draws[player])
            {
                if (d.list != Horse::KHitList::Attack ||
                    !d.geom_active ||
                    !khit_audit_matches_move_filter(d, nullptr) ||
                    !khit_audit_matches_slot_filter(d))
                {
                    continue;
                }

                const bool selected_for_audit =
                    d.attack_mask_selected ||
                    d.attack_mask_stale ||
                    d.accepted_overlap_this_frame ||
                    d.accepted_exact_overlap_this_frame;
                if (!selected_for_audit)
                    continue;

                nodes.push_back(&d);
                if (d.attack_mask_selected)
                    ++selected_count;
                if (d.attack_classifier_ready)
                    ++ready_count;
                if (d.attack_mask_stale)
                    ++stale_count;
                if (d.kind == Horse::KHitKind::Sphere &&
                    d.sphere_anim_modified)
                {
                    ++modified_sphere_count;
                }
            }

            if (nodes.size() < 2)
                continue;
            if (!consume_khit_audit_log_slot(
                    KHitAuditLogBucket::AttackCluster))
                return;

            const Horse::KHitDraw& first = *nodes.front();
            std::string slot_summary;
            slot_summary.reserve(1024);
            size_t emitted = 0;
            for (const Horse::KHitDraw* node : nodes)
            {
                if (!node || slot_summary.size() >= 1400)
                    break;

                char item[192]{};
                if (node->kind == Horse::KHitKind::Sphere)
                {
                    std::snprintf(
                        item, sizeof(item),
                        "%s%d:r=%.3f,scale=%.3f,d=(%.3f,%.3f,%.3f)%s%s",
                        emitted ? ";" : "",
                        static_cast<int>(node->bone_id_internal),
                        node->native_radius,
                        node->sphere_live_radius_scale,
                        node->sphere_live_local_delta.X,
                        node->sphere_live_local_delta.Y,
                        node->sphere_live_local_delta.Z,
                        node->sphere_anim_modified ? ",mod" : "",
                        node->attack_mask_stale ? ",stale" : "");
                }
                else
                {
                    std::snprintf(
                        item, sizeof(item),
                        "%s%d:%s%s",
                        emitted ? ";" : "",
                        static_cast<int>(node->bone_id_internal),
                        khit_kind_short(node->kind),
                        node->attack_mask_stale ? ",stale" : "");
                }
                slot_summary += item;
                ++emitted;
            }
            if (emitted < nodes.size())
                slot_summary += ";...";

            Output::send<LogLevel::Default>(
                STR("[HorseMod.KHitAudit.AttackCluster] frame_ok={} "
                    "frame={} p={} move=0x{:04x}/{} sub=0x{:04x} "
                    "renderer={} active_move_valid={} primary=0x{:016x} "
                    "node_count={} selected_count={} ready_count={} "
                    "stale_count={} modified_spheres={} slots={}\n"),
                have_game_frame,
                have_game_frame ? game_frame : 0,
                player + 1,
                first.has_move_identity ? first.active_packed_move : 0xFFFFu,
                first.has_move_identity
                    ? static_cast<int>(first.active_move_id_low11)
                    : -1,
                first.move_subframe_id,
                static_cast<int>(hit_renderer_slot),
                first.active_move_valid,
                first.primary_attack_mask,
                static_cast<int>(nodes.size()),
                selected_count,
                ready_count,
                stale_count,
                modified_sphere_count,
                RC::to_generic_string(slot_summary.c_str()));
        }
    }

    static void mark_khit_accepted_overlap_candidates(
        std::vector<Horse::KHitDraw> (&draws)[2])
    {
        struct KHitOverlapCandidate
        {
            Horse::KHitDraw* attack = nullptr;
            uint64_t matched_bits = 0;
            bool exact_geometry = false;
            bool same_slot_ambiguous = false;
        };

        for (auto& player_draws : draws)
        {
            for (Horse::KHitDraw& d : player_draws)
            {
                d.accepted_overlap_this_frame = false;
                d.accepted_overlap_matched_bits = 0;
                d.accepted_overlap_pair_count = 0;
                d.accepted_overlap_ambiguous = false;
                d.accepted_exact_overlap_this_frame = false;
                d.accepted_exact_overlap_matched_bits = 0;
                d.accepted_exact_overlap_pair_count = 0;
                d.reaction_overlap_this_frame = false;
                d.reaction_overlap_matched_bits = 0;
                d.reaction_overlap_pair_count = 0;
                d.reaction_overlap_ambiguous = false;
            }
        }

        for (int defender = 0; defender < 2; ++defender)
        {
            const int attacker = (defender == 0) ? 1 : 0;
            for (Horse::KHitDraw& hurt : draws[defender])
            {
                if (hurt.list != Horse::KHitList::Hurtbox ||
                    !hurt.defender_hurtbox_mask_valid ||
                    hurt.defender_hurtbox_attack_mask == 0)
                {
                    continue;
                }

                std::vector<KHitOverlapCandidate> candidates;
                candidates.reserve(draws[attacker].size());
                for (Horse::KHitDraw& attack : draws[attacker])
                {
                    if (attack.list != Horse::KHitList::Attack ||
                        !attack.geom_active)
                    {
                        continue;
                    }

                    const uint64_t matched_bits =
                        hurt.defender_hurtbox_attack_mask &
                        attack.slot_bit_mask;
                    if (matched_bits == 0)
                        continue;
                    if (!khit_pair_geometry_plausible(attack, hurt))
                        continue;

                    candidates.push_back(KHitOverlapCandidate{
                        &attack,
                        matched_bits,
                        khit_pair_has_exact_geometry(attack, hurt),
                        false
                    });
                }

                for (size_t i = 0; i < candidates.size(); ++i)
                {
                    for (size_t j = i + 1; j < candidates.size(); ++j)
                    {
                        if ((candidates[i].matched_bits &
                             candidates[j].matched_bits) == 0)
                        {
                            continue;
                        }
                        candidates[i].same_slot_ambiguous = true;
                        candidates[j].same_slot_ambiguous = true;
                    }
                }

                for (KHitOverlapCandidate& c : candidates)
                {
                    Horse::KHitDraw& attack = *c.attack;

                    attack.accepted_overlap_this_frame = true;
                    attack.accepted_overlap_matched_bits |= c.matched_bits;
                    ++attack.accepted_overlap_pair_count;
                    if (c.same_slot_ambiguous)
                        attack.accepted_overlap_ambiguous = true;
                    if (c.exact_geometry)
                    {
                        attack.accepted_exact_overlap_this_frame = true;
                        attack.accepted_exact_overlap_matched_bits |=
                            c.matched_bits;
                        ++attack.accepted_exact_overlap_pair_count;
                    }

                    hurt.accepted_overlap_this_frame = true;
                    hurt.accepted_overlap_matched_bits |= c.matched_bits;
                    ++hurt.accepted_overlap_pair_count;
                    if (c.same_slot_ambiguous)
                        hurt.accepted_overlap_ambiguous = true;
                    if (c.exact_geometry)
                    {
                        hurt.accepted_exact_overlap_this_frame = true;
                        hurt.accepted_exact_overlap_matched_bits |=
                            c.matched_bits;
                        ++hurt.accepted_exact_overlap_pair_count;
                    }

                    if (hurt.raw_reaction_state != 0 &&
                        canMatterThisFrame(attack))
                    {
                        const bool can_promote_to_reaction =
                            c.exact_geometry || !c.same_slot_ambiguous;
                        if (!can_promote_to_reaction)
                        {
                            attack.reaction_overlap_ambiguous = true;
                            hurt.reaction_overlap_ambiguous = true;
                            continue;
                        }

                        attack.reaction_overlap_this_frame = true;
                        attack.reaction_overlap_matched_bits |= c.matched_bits;
                        ++attack.reaction_overlap_pair_count;

                        hurt.reaction_overlap_this_frame = true;
                        hurt.reaction_overlap_matched_bits |= c.matched_bits;
                        ++hurt.reaction_overlap_pair_count;
                    }
                }
            }
        }
    }

