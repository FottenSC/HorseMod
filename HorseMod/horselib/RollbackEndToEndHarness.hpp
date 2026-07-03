// ============================================================================
// Horse::RollbackEndToEndHarness
//
// Local two-peer rollback proof that ties the HRG1/Gekko payload bridge, live
// transport queue, online-session correction metadata, decoded gameplay input,
// and confirmed-input cache replacement into one deterministic convergence gate.
// This still does not touch SC6/Steam live sockets.
// ============================================================================

#pragma once

#include "RollbackGekkoGameplayInputBridge.hpp"

#include <array>
#include <cstdint>

namespace Horse
{
    struct RollbackEndToEndSelfTestReport
    {
        bool ok {false};
        bool decoded_payloads {false};
        bool bridge_roundtrip {false};
        bool prediction_written {false};
        bool prediction_diverged {false};
        bool network_receive_queued_only {false};
        bool stock_drain_required {false};
        bool metadata_accepted {false};
        bool metadata_requires_correction {false};
        bool metadata_not_gameplay_input {false};
        bool confirmed_applied {false};
        bool confirmed_consumed {false};
        bool state_converged {false};
        bool wrong_identity_rejected {false};
        bool network_thread_cache_write_rejected {false};
        uint32_t enqueued_packets {0};
        uint32_t drained_packets {0};
        uint32_t rejected_packets {0};
        uint32_t cache_write_sequence {0};
        uint32_t predicted_checksum_a {0};
        uint32_t predicted_checksum_b {0};
        uint32_t confirmed_checksum_a {0};
        uint32_t confirmed_checksum_b {0};
        const char* failure {"not-run"};
    };

    static inline uint32_t RollbackEndToEndChecksum(
        uint32_t frame,
        uint32_t player0_input,
        uint32_t player1_input) noexcept
    {
        RollbackGekkoDetail::State state {};
        const uint32_t inputs[2] {player0_input, player1_input};
        RollbackGekkoDetail::advance_state(
            state,
            static_cast<int>(frame),
            inputs,
            false);
        return RollbackGekkoDetail::checksum_bytes(
            &state,
            static_cast<uint32_t>(sizeof(state)));
    }

    static inline RollbackEndToEndSelfTestReport
    RunRollbackEndToEndSelfTest() noexcept
    {
        RollbackEndToEndSelfTestReport report {};
        report.failure = "ok";

        static constexpr uint8_t kPeerA = 0x41;
        static constexpr uint8_t kPeerB = 0x42;
        static constexpr uint64_t kSessionId = 0x484F525345454532ull;
        static constexpr uint32_t kFrame = 6;
        static constexpr uint32_t kLocalAInput = 0x10203040u;
        static constexpr uint32_t kLocalBInput = 0x55667788u;
        static constexpr uint32_t kPredictedAInput = 0x01020304u;
        static constexpr uint32_t kPredictedBInput = 0xA0B0C0D0u;

        const uint32_t raw_inputs[2] {kLocalAInput, kLocalBInput};
        const RollbackGekkoGameplayInputDecodeReport decoded =
            DecodeRollbackGekkoGameplayInputs(
                static_cast<int32_t>(kFrame),
                raw_inputs,
                static_cast<uint32_t>(sizeof(raw_inputs)),
                2);
        RollbackDecodedGameplayInput decoded_a {};
        RollbackDecodedGameplayInput decoded_b {};
        report.decoded_payloads =
            decoded.ok
            && GetRollbackGekkoDecodedGameplayInput(decoded, 0, decoded_a)
            && GetRollbackGekkoDecodedGameplayInput(decoded, 1, decoded_b)
            && decoded_a.input_value == kLocalAInput
            && decoded_b.input_value == kLocalBInput;

        RollbackLivePeerPipeline<4, 128> peer_a {};
        RollbackLivePeerPipeline<4, 128> peer_b {};
        peer_a.reset(12, RollbackHashPolicy::Enforced);
        peer_b.reset(12, RollbackHashPolicy::Enforced);

        const RollbackRemotePredictionReport meta_pred_a =
            peer_a.predict_remote_metadata_input(kFrame);
        const RollbackRemotePredictionReport meta_pred_b =
            peer_b.predict_remote_metadata_input(kFrame);

        const RollbackDecodedGameplayInput pred_for_a {
            kFrame,
            1,
            kPredictedBInput,
        };
        const RollbackDecodedGameplayInput pred_for_b {
            kFrame,
            0,
            kPredictedAInput,
        };
        const RollbackInputCacheAccessReport pred_a =
            peer_a.predict_remote_input(pred_for_a, true);
        const RollbackInputCacheAccessReport pred_b =
            peer_b.predict_remote_input(pred_for_b, true);
        const RollbackInputCacheAccessReport pred_a_seen =
            peer_a.consume_remote_input(pred_for_a);
        const RollbackInputCacheAccessReport pred_b_seen =
            peer_b.consume_remote_input(pred_for_b);
        report.prediction_written =
            meta_pred_a.predicted
            && meta_pred_b.predicted
            && pred_a.ok
            && pred_a.wrote
            && pred_a.source_prediction
            && pred_b.ok
            && pred_b.wrote
            && pred_b.source_prediction
            && pred_a_seen.ok
            && pred_a_seen.source_prediction
            && pred_a_seen.dwInputValue == kPredictedBInput
            && pred_b_seen.ok
            && pred_b_seen.source_prediction
            && pred_b_seen.dwInputValue == kPredictedAInput;

        RollbackGekkoBridgeWirePacket wire_b_to_a {};
        RollbackGekkoBridgeWirePacket wire_a_to_b {};
        const bool encode_b_to_a =
            MakeRollbackLiveTransportTestWire(
                kPeerB,
                kPeerA,
                kSessionId,
                kFrame,
                kRollbackTransportNoFrame,
                raw_inputs,
                sizeof(raw_inputs),
                wire_b_to_a);
        const bool encode_a_to_b =
            MakeRollbackLiveTransportTestWire(
                kPeerA,
                kPeerB,
                kSessionId,
                kFrame,
                kRollbackTransportNoFrame,
                raw_inputs,
                sizeof(raw_inputs),
                wire_a_to_b);

        const bool enqueue_b_to_a =
            encode_b_to_a
            && peer_a.enqueue_hrg1_wire(
                wire_b_to_a.bytes.data(),
                wire_b_to_a.size,
                kPeerB,
                kPeerA,
                kSessionId);
        const bool enqueue_a_to_b =
            encode_a_to_b
            && peer_b.enqueue_hrg1_wire(
                wire_a_to_b.bytes.data(),
                wire_a_to_b.size,
                kPeerA,
                kPeerB,
                kSessionId);
        report.bridge_roundtrip =
            enqueue_b_to_a
            && enqueue_a_to_b
            && peer_a.queue_count() == 1
            && peer_b.queue_count() == 1;
        report.network_receive_queued_only =
            report.bridge_roundtrip
            && peer_a.metrics().packets_received == 0
            && peer_b.metrics().packets_received == 0
            && peer_a.cache_writes() == 1
            && peer_b.cache_writes() == 1;

        const RollbackLivePeerPipelineDrainReport blocked_a =
            peer_a.drain_metadata_to_session(kFrame, false);
        const RollbackLivePeerPipelineDrainReport blocked_b =
            peer_b.drain_metadata_to_session(kFrame, false);
        const RollbackInputCacheAccessReport pred_a_still_seen =
            peer_a.consume_remote_input(pred_for_a);
        const RollbackInputCacheAccessReport pred_b_still_seen =
            peer_b.consume_remote_input(pred_for_b);
        report.stock_drain_required =
            blocked_a.drain.left_queued
            && blocked_b.drain.left_queued
            && blocked_a.drain.status
                == RollbackLiveTransportDrainStatus::CacheOrderingRejected
            && blocked_b.drain.status
                == RollbackLiveTransportDrainStatus::CacheOrderingRejected
            && peer_a.queue_count() == 1
            && peer_b.queue_count() == 1
            && pred_a_still_seen.ok
            && pred_a_still_seen.source_prediction
            && pred_b_still_seen.ok
            && pred_b_still_seen.source_prediction;

        const RollbackLivePeerPipelineDrainReport drained_a =
            peer_a.drain_metadata_to_session(kFrame, true);
        const RollbackLivePeerPipelineDrainReport drained_b =
            peer_b.drain_metadata_to_session(kFrame, true);
        report.metadata_accepted =
            drained_a.ok
            && drained_b.ok
            && drained_a.metadata_accepted
            && drained_b.metadata_accepted
            && drained_a.cache_untouched
            && drained_b.cache_untouched
            && peer_a.queue_count() == 0
            && peer_b.queue_count() == 0;
        report.metadata_requires_correction =
            drained_a.metadata_requires_correction
            && drained_b.metadata_requires_correction
            && drained_a.drain.receive.correction_start_frame == kFrame
            && drained_b.drain.receive.correction_start_frame == kFrame;
        report.metadata_not_gameplay_input =
            drained_a.drain.metadata.local_input == drained_a.drain.payload_hash
            && drained_b.drain.metadata.local_input
                == drained_b.drain.payload_hash
            && drained_a.drain.metadata.local_input
                != static_cast<uint64_t>(decoded_b.input_value)
            && drained_b.drain.metadata.local_input
                != static_cast<uint64_t>(decoded_a.input_value);

        const RollbackInputCacheAccessReport confirm_a =
            peer_a.apply_confirmed_gameplay_input(
                decoded_b,
                true,
                true,
                false);
        const RollbackInputCacheAccessReport confirm_b =
            peer_b.apply_confirmed_gameplay_input(
                decoded_a,
                true,
                true,
                false);
        const RollbackInputCacheAccessReport consume_a =
            peer_a.consume_remote_input(decoded_b);
        const RollbackInputCacheAccessReport consume_b =
            peer_b.consume_remote_input(decoded_a);
        report.confirmed_applied =
            confirm_a.ok
            && confirm_a.wrote
            && confirm_a.replaced_prediction
            && confirm_a.requires_correction
            && confirm_b.ok
            && confirm_b.wrote
            && confirm_b.replaced_prediction
            && confirm_b.requires_correction;
        report.confirmed_consumed =
            consume_a.ok
            && consume_a.source_confirmed
            && consume_a.exactly_one_source
            && consume_a.dwPlayerSlot == decoded_b.player_slot
            && consume_a.dwInputValue == decoded_b.input_value
            && consume_b.ok
            && consume_b.source_confirmed
            && consume_b.exactly_one_source
            && consume_b.dwPlayerSlot == decoded_a.player_slot
            && consume_b.dwInputValue == decoded_a.input_value;

        report.predicted_checksum_a =
            RollbackEndToEndChecksum(kFrame, kLocalAInput, kPredictedBInput);
        report.predicted_checksum_b =
            RollbackEndToEndChecksum(kFrame, kPredictedAInput, kLocalBInput);
        report.confirmed_checksum_a =
            RollbackEndToEndChecksum(kFrame, kLocalAInput, kLocalBInput);
        report.confirmed_checksum_b =
            RollbackEndToEndChecksum(kFrame, kLocalAInput, kLocalBInput);
        report.prediction_diverged =
            report.predicted_checksum_a != report.confirmed_checksum_a
            && report.predicted_checksum_b != report.confirmed_checksum_b;
        report.state_converged =
            report.confirmed_checksum_a == report.confirmed_checksum_b
            && report.confirmed_checksum_a != 0;

        report.wrong_identity_rejected =
            encode_b_to_a
            && !peer_a.enqueue_hrg1_wire(
                wire_b_to_a.bytes.data(),
                wire_b_to_a.size,
                static_cast<uint8_t>(kPeerB + 1),
                kPeerA,
                kSessionId)
            && !peer_a.enqueue_hrg1_wire(
                wire_b_to_a.bytes.data(),
                wire_b_to_a.size,
                kPeerB,
                static_cast<uint8_t>(kPeerA + 1),
                kSessionId)
            && !peer_a.enqueue_hrg1_wire(
                wire_b_to_a.bytes.data(),
                wire_b_to_a.size,
                kPeerB,
                kPeerA,
                kSessionId ^ 0x10ull);

        const RollbackInputCacheAccessReport network_bad =
            peer_a.apply_confirmed_gameplay_input(
                {static_cast<uint32_t>(kFrame + 1), 1, 0xDEADu},
                false,
                false,
                true);
        report.network_thread_cache_write_rejected =
            !network_bad.ok
            && network_bad.status
                == RollbackInputCacheAccessStatus::NetworkThreadCacheWrite;

        report.enqueued_packets =
            peer_a.enqueued_packets() + peer_b.enqueued_packets();
        report.drained_packets =
            peer_a.drained_packets() + peer_b.drained_packets();
        report.rejected_packets =
            peer_a.rejected_packets() + peer_b.rejected_packets();
        report.cache_write_sequence =
            peer_a.cache_writes() + peer_b.cache_writes();

        report.ok =
            report.decoded_payloads
            && report.bridge_roundtrip
            && report.prediction_written
            && report.prediction_diverged
            && report.network_receive_queued_only
            && report.stock_drain_required
            && report.metadata_accepted
            && report.metadata_requires_correction
            && report.metadata_not_gameplay_input
            && report.confirmed_applied
            && report.confirmed_consumed
            && report.state_converged
            && report.wrong_identity_rejected
            && report.network_thread_cache_write_rejected
            && report.enqueued_packets == 2
            && report.drained_packets == 2
            && report.rejected_packets >= 3
            && report.cache_write_sequence == 4;
        if (!report.ok)
            report.failure = "rollback-end-to-end-selftest-failed";
        return report;
    }
}
