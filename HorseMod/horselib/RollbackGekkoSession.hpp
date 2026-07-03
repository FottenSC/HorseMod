// ============================================================================
// Horse::RollbackGekkoSession
//
// Thin lab adapter for the GekkoNet rollback session core. This does not touch
// SC6 online traffic; it proves that Gekko's save/load/advance event loop can
// drive a deterministic fixed-size Horse state adapter.
// ============================================================================

#pragma once

#ifndef HORSE_ENABLE_GEKKONET
#define HORSE_ENABLE_GEKKONET 0
#endif

#include <cstdint>
#include <cstring>

#if HORSE_ENABLE_GEKKONET
#include <gekkonet.h>
#endif

namespace Horse
{
    struct RollbackGekkoSessionSelfTestReport
    {
        bool ok {false};
        bool dependency_enabled {false};
        bool create_ok {false};
        bool start_ok {false};
        bool actors_ok {false};
        bool saw_save {false};
        bool saw_load {false};
        bool saw_advance {false};
        bool saw_rollback_advance {false};
        bool no_desync {false};
        bool final_checksum_expected {false};
        bool destroy_ok {false};
        uint32_t frames_submitted {0};
        uint32_t save_events {0};
        uint32_t load_events {0};
        uint32_t advance_events {0};
        uint32_t rollback_advance_events {0};
        uint32_t final_checksum {0};
        const char* failure {"not-run"};
    };

    namespace RollbackGekkoDetail
    {
        struct State
        {
            uint32_t frame {0};
            uint32_t player_accum[2] {0, 0};
            uint32_t salt {0x47454B4Bu};
        };

        static inline uint32_t checksum_bytes(
            const void* data,
            uint32_t size) noexcept
        {
            const auto* p = static_cast<const uint8_t*>(data);
            uint32_t h = 2166136261u;
            for (uint32_t i = 0; i < size; ++i)
            {
                h ^= p[i];
                h *= 16777619u;
            }
            return h;
        }

        static inline void advance_state(
            State& state,
            int frame,
            const uint32_t* inputs,
            bool /*rolling_back*/) noexcept
        {
            state.frame = static_cast<uint32_t>(frame + 1);
            state.player_accum[0] =
                state.player_accum[0] * 33u + inputs[0]
                + static_cast<uint32_t>(frame);
            state.player_accum[1] =
                state.player_accum[1] * 33u + inputs[1]
                + static_cast<uint32_t>(frame * 3);
        }
    }

    static inline RollbackGekkoSessionSelfTestReport
    RunRollbackGekkoSessionSelfTest() noexcept
    {
        RollbackGekkoSessionSelfTestReport report {};
        report.dependency_enabled = HORSE_ENABLE_GEKKONET != 0;

#if !HORSE_ENABLE_GEKKONET
        report.failure = "gekkonet-disabled";
        return report;
#else
        GekkoSession* session = nullptr;
        report.create_ok = gekko_create(&session, GekkoStressSession) && session;
        if (!report.create_ok)
        {
            report.failure = "create-failed";
            return report;
        }

        auto finish = [&](const char* failure) noexcept {
            report.destroy_ok = gekko_destroy(&session);
            report.failure = failure;
            report.ok =
                report.dependency_enabled
                && report.create_ok
                && report.start_ok
                && report.actors_ok
                && report.saw_save
                && report.saw_load
                && report.saw_advance
                && report.saw_rollback_advance
                && report.no_desync
                && report.final_checksum_expected
                && report.destroy_ok;
            if (report.ok)
                report.failure = "ok";
            return report;
        };

        GekkoConfig config {};
        config.num_players = 2;
        config.max_spectators = 0;
        config.input_prediction_window = 4;
        config.input_size = sizeof(uint32_t);
        config.state_size =
            static_cast<unsigned int>(sizeof(RollbackGekkoDetail::State));
        config.limited_saving = false;
        config.desync_detection = true;
        config.check_distance = 4;

        gekko_start(session, &config);
        report.start_ok = true;

        const int p0 = gekko_add_actor(session, GekkoLocalPlayer, nullptr);
        const int p1 = gekko_add_actor(session, GekkoLocalPlayer, nullptr);
        report.actors_ok = (p0 == 0 && p1 == 1);
        if (!report.actors_ok)
            return finish("actor-add-failed");
        gekko_set_local_delay(session, 0, 1);
        gekko_set_local_delay(session, 1, 1);

        RollbackGekkoDetail::State state {};
        report.no_desync = true;

        for (uint32_t step = 0; step < 16; ++step)
        {
            uint32_t p0_input = 0x10u + step;
            uint32_t p1_input = 0x80u ^ (step * 3u);
            gekko_add_local_input(session, 0, &p0_input);
            gekko_add_local_input(session, 1, &p1_input);
            ++report.frames_submitted;

            int event_count = 0;
            GekkoGameEvent** events =
                gekko_update_session(session, &event_count);
            for (int i = 0; i < event_count; ++i)
            {
                GekkoGameEvent* ev = events[i];
                if (!ev) continue;
                switch (ev->type)
                {
                case GekkoSaveEvent:
                    report.saw_save = true;
                    ++report.save_events;
                    *ev->data.save.state_len =
                        static_cast<unsigned int>(sizeof(state));
                    *ev->data.save.checksum =
                        RollbackGekkoDetail::checksum_bytes(
                            &state,
                            static_cast<uint32_t>(sizeof(state)));
                    std::memcpy(ev->data.save.state, &state, sizeof(state));
                    break;
                case GekkoLoadEvent:
                    report.saw_load = true;
                    ++report.load_events;
                    if (ev->data.load.state_len != sizeof(state))
                        return finish("load-size-mismatch");
                    std::memcpy(&state, ev->data.load.state, sizeof(state));
                    break;
                case GekkoAdvanceEvent:
                {
                    report.saw_advance = true;
                    ++report.advance_events;
                    if (ev->data.adv.rolling_back)
                    {
                        report.saw_rollback_advance = true;
                        ++report.rollback_advance_events;
                    }
                    if (ev->data.adv.input_len
                        != sizeof(uint32_t) * config.num_players)
                    {
                        return finish("advance-input-size-mismatch");
                    }
                    const auto* inputs =
                        reinterpret_cast<const uint32_t*>(
                            ev->data.adv.inputs);
                    RollbackGekkoDetail::advance_state(
                        state,
                        ev->data.adv.frame,
                        inputs,
                        ev->data.adv.rolling_back);
                    break;
                }
                default:
                    break;
                }
            }

            int session_event_count = 0;
            GekkoSessionEvent** session_events =
                gekko_session_events(session, &session_event_count);
            for (int i = 0; i < session_event_count; ++i)
            {
                GekkoSessionEvent* ev = session_events[i];
                if (ev && ev->type == GekkoDesyncDetected)
                    report.no_desync = false;
            }
            if (!report.no_desync)
                return finish("desync-detected");
        }

        report.final_checksum =
            RollbackGekkoDetail::checksum_bytes(
                &state,
                static_cast<uint32_t>(sizeof(state)));
        report.final_checksum_expected =
            report.final_checksum == 0x1004AFC6u;
        return finish("ok");
#endif
    }
}
