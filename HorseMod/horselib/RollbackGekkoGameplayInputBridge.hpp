// ============================================================================
// Horse::RollbackGekkoGameplayInputBridge
//
// Explicit decoder between GekkoNet advance-event input bytes and the SC6
// rollback gameplay input cache contract. HRG1 packet metadata remains envelope
// identity; only decoded gameplay inputs may become cache writes.
// ============================================================================

#pragma once

#ifndef HORSE_ENABLE_GEKKONET
#define HORSE_ENABLE_GEKKONET 0
#endif

#include "RollbackGekkoSession.hpp"
#include "RollbackFrameStamp.hpp"
#include "RollbackLivePeerPipeline.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#if HORSE_ENABLE_GEKKONET
#include <gekkonet.h>
#endif

namespace Horse
{
    static constexpr uint32_t kRollbackGekkoGameplayInputMaxPlayers = 2;

    enum class RollbackGekkoGameplayInputDecodeStatus : uint8_t
    {
        Ok,
        NullInputs,
        InvalidFrame,
        UnsupportedPlayerCount,
        InvalidInputLength,
        InvalidPlayerSlot,
    };

    struct RollbackGekkoGameplayInputDecodeReport
    {
        bool ok {false};
        bool decoded {false};
        RollbackGekkoGameplayInputDecodeStatus status {
            RollbackGekkoGameplayInputDecodeStatus::NullInputs};
        uint32_t frame {0};
        uint32_t player_count {0};
        uint32_t input_len {0};
        uint32_t decoded_count {0};
        std::array<
            RollbackDecodedGameplayInput,
            kRollbackGekkoGameplayInputMaxPlayers> inputs {};
        const char* failure {"not-run"};
    };

    struct RollbackGekkoGameplayInputBridgeSelfTestReport
    {
        bool ok {false};
        bool dependency_enabled {false};
        bool raw_decode_ok {false};
        bool raw_decode_player0 {false};
        bool raw_decode_player1 {false};
        bool null_inputs_rejected {false};
        bool bad_frame_rejected {false};
        bool last_safe_frame_accepted {false};
        bool signed_ceiling_rejected {false};
        bool signed_update_guard_ok {false};
        bool bad_size_rejected {false};
        bool bad_player_count_rejected {false};
        bool bad_slot_rejected {false};
        bool pipeline_apply_ok {false};
        bool payload_hash_separate {false};
        bool create_ok {false};
        bool start_ok {false};
        bool actors_ok {false};
        bool baseline_frame_key_ok {false};
        bool actual_gekko_advance_decode {false};
        bool actual_gekko_rollback_decode {false};
        bool no_desync {false};
        bool destroy_ok {false};
        uint32_t decoded_events {0};
        uint32_t decoded_inputs {0};
        uint32_t frames_submitted {0};
        uint32_t advance_events {0};
        uint32_t rollback_advance_events {0};
        uint32_t final_checksum {0};
        bool final_checksum_expected {false};
        const char* failure {"not-run"};
    };

    static inline const char* RollbackGekkoGameplayInputDecodeFailure(
        RollbackGekkoGameplayInputDecodeStatus status) noexcept
    {
        switch (status)
        {
        case RollbackGekkoGameplayInputDecodeStatus::Ok:
            return "ok";
        case RollbackGekkoGameplayInputDecodeStatus::NullInputs:
            return "null-gekkonet-inputs";
        case RollbackGekkoGameplayInputDecodeStatus::InvalidFrame:
            return "invalid-gekkonet-frame";
        case RollbackGekkoGameplayInputDecodeStatus::UnsupportedPlayerCount:
            return "unsupported-gekkonet-player-count";
        case RollbackGekkoGameplayInputDecodeStatus::InvalidInputLength:
            return "invalid-gekkonet-input-length";
        case RollbackGekkoGameplayInputDecodeStatus::InvalidPlayerSlot:
            return "invalid-gekkonet-player-slot";
        default:
            return "unknown-gekkonet-input-decode-status";
        }
    }

    static inline RollbackGekkoGameplayInputDecodeReport
    DecodeRollbackGekkoGameplayInputs(
        int32_t frame,
        const void* inputs,
        uint32_t input_len,
        uint32_t player_count) noexcept
    {
        RollbackGekkoGameplayInputDecodeReport out {};
        out.frame = frame < 0 ? 0u : static_cast<uint32_t>(frame);
        out.player_count = player_count;
        out.input_len = input_len;

        auto fail = [&](RollbackGekkoGameplayInputDecodeStatus status) {
            out.status = status;
            out.failure = RollbackGekkoGameplayInputDecodeFailure(status);
            return out;
        };

        if (!RollbackGekkoFrameIsProductionSafe(frame))
            return fail(RollbackGekkoGameplayInputDecodeStatus::InvalidFrame);
        if (!inputs)
            return fail(RollbackGekkoGameplayInputDecodeStatus::NullInputs);
        if (player_count == 0
            || player_count > kRollbackGekkoGameplayInputMaxPlayers)
        {
            return fail(
                RollbackGekkoGameplayInputDecodeStatus::
                    UnsupportedPlayerCount);
        }
        if (input_len != player_count * sizeof(uint32_t))
        {
            return fail(
                RollbackGekkoGameplayInputDecodeStatus::InvalidInputLength);
        }

        const auto* bytes = static_cast<const uint8_t*>(inputs);
        for (uint32_t i = 0; i < player_count; ++i)
        {
            uint32_t value = 0;
            std::memcpy(
                &value,
                bytes + static_cast<size_t>(i) * sizeof(uint32_t),
                sizeof(value));
            out.inputs[i].frame = static_cast<uint32_t>(frame);
            out.inputs[i].player_slot = i;
            out.inputs[i].input_value = value;
        }

        out.ok = true;
        out.decoded = true;
        out.status = RollbackGekkoGameplayInputDecodeStatus::Ok;
        out.decoded_count = player_count;
        out.failure = "ok";
        return out;
    }

    static inline bool GetRollbackGekkoDecodedGameplayInput(
        const RollbackGekkoGameplayInputDecodeReport& report,
        uint32_t player_slot,
        RollbackDecodedGameplayInput& out) noexcept
    {
        if (!report.ok || player_slot >= report.decoded_count)
            return false;
        out = report.inputs[player_slot];
        return true;
    }

    static inline void RollbackGekkoGameplayBridgeSelfTestRaw(
        RollbackGekkoGameplayInputBridgeSelfTestReport& report) noexcept
    {
        uint32_t raw[2] {0x12345678u, 0xAA55CC33u};
        const RollbackGekkoGameplayInputDecodeReport decoded =
            DecodeRollbackGekkoGameplayInputs(
                3,
                raw,
                static_cast<uint32_t>(sizeof(raw)),
                2);
        RollbackDecodedGameplayInput player0 {};
        RollbackDecodedGameplayInput player1 {};
        report.raw_decode_player0 =
            GetRollbackGekkoDecodedGameplayInput(decoded, 0, player0)
            && player0.frame == 3
            && player0.player_slot == 0
            && player0.input_value == raw[0];
        report.raw_decode_player1 =
            GetRollbackGekkoDecodedGameplayInput(decoded, 1, player1)
            && player1.frame == 3
            && player1.player_slot == 1
            && player1.input_value == raw[1];
        report.raw_decode_ok =
            decoded.ok
            && decoded.decoded_count == 2
            && report.raw_decode_player0
            && report.raw_decode_player1;

        const RollbackGekkoGameplayInputDecodeReport null_decode =
            DecodeRollbackGekkoGameplayInputs(
                3,
                nullptr,
                static_cast<uint32_t>(sizeof(raw)),
                2);
        report.null_inputs_rejected =
            !null_decode.ok
            && null_decode.status
                == RollbackGekkoGameplayInputDecodeStatus::NullInputs;

        const RollbackGekkoGameplayInputDecodeReport bad_frame =
            DecodeRollbackGekkoGameplayInputs(
                -1,
                raw,
                static_cast<uint32_t>(sizeof(raw)),
                2);
        report.bad_frame_rejected =
            !bad_frame.ok
            && bad_frame.status
                == RollbackGekkoGameplayInputDecodeStatus::InvalidFrame;

        const int32_t last_safe_frame = static_cast<int32_t>(
            kRollbackGekkoSignedFrameExclusiveCeiling - 1u);
        const RollbackGekkoGameplayInputDecodeReport last_safe =
            DecodeRollbackGekkoGameplayInputs(
                last_safe_frame,
                raw,
                static_cast<uint32_t>(sizeof(raw)),
                2);
        report.last_safe_frame_accepted = last_safe.ok
            && last_safe.frame == static_cast<uint32_t>(last_safe_frame);

        const RollbackGekkoGameplayInputDecodeReport at_ceiling =
            DecodeRollbackGekkoGameplayInputs(
                static_cast<int32_t>(
                    kRollbackGekkoSignedFrameExclusiveCeiling),
                raw,
                static_cast<uint32_t>(sizeof(raw)),
                2);
        report.signed_ceiling_rejected = !at_ceiling.ok
            && at_ceiling.status
                == RollbackGekkoGameplayInputDecodeStatus::InvalidFrame;

        RollbackFrameStamp high_water {};
        const bool initial_update = RollbackGekkoMayUpdateSession(
            high_water, 0);
        RollbackObserveGekkoFrame(
            high_water,
            kRollbackGekkoSignedFrameExclusiveCeiling - 2u);
        const bool penultimate_update = RollbackGekkoMayUpdateSession(
            high_water,
            kRollbackGekkoSignedFrameExclusiveCeiling - 1u);
        RollbackObserveGekkoFrame(
            high_water,
            kRollbackGekkoSignedFrameExclusiveCeiling - 1u);
        const bool high_water_stops = !RollbackGekkoMayUpdateSession(
            high_water, 1);
        RollbackFrameStamp low_high_water = RollbackFrameStamp::From(10);
        const bool update_count_stops = !RollbackGekkoMayUpdateSession(
            low_high_water,
            kRollbackGekkoSignedFrameExclusiveCeiling);
        RollbackObserveGekkoFrame(low_high_water, 4);
        const bool high_water_does_not_regress = low_high_water.valid
            && low_high_water.value == 10;
        report.signed_update_guard_ok = initial_update
            && penultimate_update
            && high_water_stops
            && update_count_stops
            && high_water_does_not_regress;
        uint32_t baseline_key = 0;
        report.baseline_frame_key_ok = RollbackGekkoStateFrameToKey(
            kRollbackGekkoBaselineFrame, baseline_key)
            && baseline_key == kRollbackGekkoBaselineFrameKey;

        const RollbackGekkoGameplayInputDecodeReport bad_size =
            DecodeRollbackGekkoGameplayInputs(
                3,
                raw,
                static_cast<uint32_t>(sizeof(uint32_t)),
                2);
        report.bad_size_rejected =
            !bad_size.ok
            && bad_size.status
                == RollbackGekkoGameplayInputDecodeStatus::InvalidInputLength;

        const RollbackGekkoGameplayInputDecodeReport bad_players =
            DecodeRollbackGekkoGameplayInputs(
                3,
                raw,
                static_cast<uint32_t>(sizeof(raw)),
                3);
        report.bad_player_count_rejected =
            !bad_players.ok
            && bad_players.status
                == RollbackGekkoGameplayInputDecodeStatus::
                    UnsupportedPlayerCount;

        RollbackDecodedGameplayInput bad_slot {};
        report.bad_slot_rejected =
            !GetRollbackGekkoDecodedGameplayInput(decoded, 2, bad_slot);

        RollbackLivePeerPipeline<4, 128> pipeline {};
        pipeline.reset(12, RollbackHashPolicy::Enforced);
        std::array<uint8_t, 5> payload {0x47, 0x45, 0x4B, 0x4B, 0x4F};
        RollbackGekkoBridgeWirePacket wire {};
        const bool enqueued =
            MakeRollbackLiveTransportTestWire(
                0xA0,
                0xB0,
                0x47454B4B494E5055ull,
                player1.frame,
                kRollbackTransportNoFrame,
                payload.data(),
                payload.size(),
                wire)
            && pipeline.enqueue_hrg1_wire(
                wire.bytes.data(),
                wire.size,
                0xA0,
                0xB0,
                0x47454B4B494E5055ull);
        const RollbackLivePeerPipelineDrainReport drained =
            pipeline.drain_metadata_to_session(player1.frame, true);
        const RollbackInputCacheAccessReport before_cache =
            pipeline.consume_remote_input(
                player1, static_cast<int32_t>(player1.frame));
        const RollbackInputCacheAccessReport applied =
            pipeline.apply_confirmed_gameplay_input(
                player1,
                static_cast<int32_t>(player1.frame),
                true,
                true,
                false);
        const RollbackInputCacheAccessReport consumed =
            pipeline.consume_remote_input(
                player1, static_cast<int32_t>(player1.frame));
        report.payload_hash_separate =
            enqueued
            && drained.metadata_accepted
            && drained.drain.metadata.local_input
                == drained.drain.payload_hash
            && drained.drain.metadata.local_input
                != static_cast<uint64_t>(player1.input_value)
            && before_cache.status == RollbackInputCacheAccessStatus::CacheMiss;
        report.pipeline_apply_ok =
            report.payload_hash_separate
            && applied.ok
            && applied.wrote
            && consumed.ok
            && consumed.source_confirmed
            && consumed.dwInputValue == player1.input_value;
    }

    static inline RollbackGekkoGameplayInputBridgeSelfTestReport
    RunRollbackGekkoGameplayInputBridgeSelfTest() noexcept
    {
        RollbackGekkoGameplayInputBridgeSelfTestReport report {};
        report.failure = "ok";
        report.dependency_enabled = HORSE_ENABLE_GEKKONET != 0;
        RollbackGekkoGameplayBridgeSelfTestRaw(report);

#if !HORSE_ENABLE_GEKKONET
        report.failure = "gekkonet-disabled";
        return report;
#else
        GekkoSession* session = nullptr;
        report.create_ok = gekko_create(&session, GekkoStressSession)
            && session;
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
                && report.raw_decode_ok
                && report.null_inputs_rejected
                && report.bad_frame_rejected
                && report.last_safe_frame_accepted
                && report.signed_ceiling_rejected
                && report.signed_update_guard_ok
                && report.bad_size_rejected
                && report.bad_player_count_rejected
                && report.bad_slot_rejected
                && report.pipeline_apply_ok
                && report.payload_hash_separate
                && report.create_ok
                && report.start_ok
                && report.actors_ok
                && report.baseline_frame_key_ok
                && report.actual_gekko_advance_decode
                && report.actual_gekko_rollback_decode
                && report.no_desync
                && report.final_checksum_expected
                && report.destroy_ok
                && report.decoded_events > 0
                && report.decoded_inputs > 0;
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
            uint32_t p0_input = 0x3100u + step;
            uint32_t p1_input = 0x4200u ^ (step * 7u);
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
                    *ev->data.save.state_len =
                        static_cast<unsigned int>(sizeof(state));
                    *ev->data.save.checksum =
                        RollbackGekkoDetail::checksum_bytes(
                            &state,
                            static_cast<uint32_t>(sizeof(state)));
                    std::memcpy(ev->data.save.state, &state, sizeof(state));
                    break;
                case GekkoLoadEvent:
                    if (ev->data.load.state_len != sizeof(state))
                        return finish("load-size-mismatch");
                    std::memcpy(&state, ev->data.load.state, sizeof(state));
                    break;
                case GekkoAdvanceEvent:
                {
                    ++report.advance_events;
                    const RollbackGekkoGameplayInputDecodeReport decoded =
                        DecodeRollbackGekkoGameplayInputs(
                            ev->data.adv.frame,
                            ev->data.adv.inputs,
                            static_cast<uint32_t>(ev->data.adv.input_len),
                            config.num_players);
                    if (!decoded.ok)
                        return finish(decoded.failure);
                    ++report.decoded_events;
                    report.decoded_inputs += decoded.decoded_count;
                    report.actual_gekko_advance_decode = true;
                    if (ev->data.adv.rolling_back)
                    {
                        report.actual_gekko_rollback_decode = true;
                        ++report.rollback_advance_events;
                    }

                    RollbackDecodedGameplayInput player0 {};
                    RollbackDecodedGameplayInput player1 {};
                    if (!GetRollbackGekkoDecodedGameplayInput(
                            decoded, 0, player0)
                        || !GetRollbackGekkoDecodedGameplayInput(
                            decoded, 1, player1))
                    {
                        return finish("decoded-player-missing");
                    }
                    const uint32_t inputs[2] {
                        player0.input_value,
                        player1.input_value,
                    };
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
            report.final_checksum == 0x79B1B776u;
        return finish("ok");
#endif
    }
}
