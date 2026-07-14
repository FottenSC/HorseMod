// ============================================================================
// Horse::RollbackLivePeerPipeline
//
// Integrated Phase-8 model for the live rollback peer path:
// network-side HRG1 decode queues metadata, game-thread drain accepts metadata,
// and only an explicitly decoded gameplay input may touch the cache shadow.
// ============================================================================

#pragma once

#include "RollbackInputCacheAdapter.hpp"
#include "RollbackLiveTransportQueue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    struct RollbackLivePeerPipelineDrainReport
    {
        bool ok {false};
        bool metadata_accepted {false};
        bool metadata_requires_correction {false};
        bool cache_untouched {false};
        RollbackLiveTransportDrainReport drain {};
        const char* failure {"not-run"};
    };

    struct RollbackDecodedGameplayInput
    {
        uint32_t frame {0};
        uint32_t player_slot {1};
        uint32_t input_value {0};
        uint8_t source_peer {0};
        uint8_t destination_peer {0};
        uint64_t session_id {0};
    };

    static inline RollbackDecodedGameplayInput
    RollbackDecodedGameplayInputWithRoute(
        RollbackDecodedGameplayInput input,
        uint8_t source_peer,
        uint8_t destination_peer,
        uint64_t session_id) noexcept
    {
        input.source_peer = source_peer;
        input.destination_peer = destination_peer;
        input.session_id = session_id;
        return input;
    }

    static inline bool RollbackDecodedGameplayInputRouteMatches(
        const RollbackDecodedGameplayInput& input,
        uint8_t source_peer,
        uint8_t destination_peer,
        uint64_t session_id) noexcept
    {
        return input.source_peer == source_peer
            && input.destination_peer == destination_peer
            && input.session_id == session_id;
    }

    struct RollbackLivePeerPipelineSelfTestReport
    {
        bool ok {false};
        bool bridge_enqueue_ok {false};
        bool network_receive_queued_only {false};
        bool stock_drain_required {false};
        bool metadata_drains_to_session {false};
        bool bridge_payload_not_cache_input {false};
        bool prediction_cache_write_ok {false};
        bool confirmed_input_replaces_prediction {false};
        bool cache_consume_confirmed {false};
        bool duplicate_confirmed_idempotent {false};
        bool prediction_over_confirmed_rejected {false};
        bool wrong_identity_rejected {false};
        bool over_window_no_cache_write {false};
        bool network_thread_cache_write_rejected {false};
        bool drain_bypass_confirmed_input {false};
        uint32_t enqueued_packets {0};
        uint32_t drained_packets {0};
        uint32_t rejected_packets {0};
        uint32_t cache_write_sequence {0};
        const char* failure {"not-run"};
    };

    template<size_t QueueN, size_t HistoryN>
    class RollbackLivePeerPipeline
    {
    public:
        void reset(
            uint32_t rollback_window_frames,
            RollbackHashPolicy hash_policy) noexcept
        {
            m_queue.clear();
            m_session.reset(rollback_window_frames, hash_policy);
            m_cache.clear();
            m_cache_writes = 0;
            m_require_decoded_route = false;
            m_expected_source_peer = 0;
            m_expected_dest_peer = 0;
            m_expected_session_id = 0;
        }

        void require_decoded_input_route(
            uint8_t source_peer,
            uint8_t dest_peer,
            uint64_t session_id) noexcept
        {
            m_require_decoded_route = true;
            m_expected_source_peer = source_peer;
            m_expected_dest_peer = dest_peer;
            m_expected_session_id = session_id;
        }

        bool enqueue_hrg1_wire(
            const uint8_t* bytes,
            size_t size,
            uint8_t expected_source_peer,
            uint8_t expected_dest_peer,
            uint64_t expected_session_id) noexcept
        {
            return m_queue.enqueue_bridge_wire(
                bytes,
                size,
                expected_source_peer,
                expected_dest_peer,
                expected_session_id);
        }

        RollbackLivePeerPipelineDrainReport drain_metadata_to_session(
            uint32_t local_sim_frame,
            bool stock_drain_complete,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            RollbackLivePeerPipelineDrainReport out {};
            out.failure = "ok";

            uint64_t expected_hash = 0;
            if (!m_queue.peek_state_hash(expected_hash))
            {
                out.ok = true;
                out.cache_untouched = true;
                out.failure = "no-packet";
                return out;
            }

            const uint32_t cache_writes_before = m_cache_writes;
            out.drain = m_queue.drain_one_to_session(
                m_session,
                local_sim_frame,
                expected_hash,
                stock_drain_complete,
                cache_mode);
            out.metadata_accepted =
                out.drain.receive.accepted
                && (out.drain.status
                    == RollbackLiveTransportDrainStatus::AcceptedNoCorrection
                    || out.drain.status
                        == RollbackLiveTransportDrainStatus::
                            AcceptedCorrectionRequired);
            out.metadata_requires_correction =
                out.drain.receive.requires_correction;
            out.cache_untouched = m_cache_writes == cache_writes_before;
            out.ok = out.drain.ok && out.cache_untouched;
            out.failure = out.drain.failure;
            return out;
        }

        RollbackInputCacheAccessReport predict_remote_input(
            const RollbackDecodedGameplayInput& input,
            int32_t nLastFrameId,
            bool stock_drain_complete,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            if (!decoded_route_ok(input))
                return decoded_route_rejected(input);
            RollbackInputCacheWriteRequest req =
                RollbackInputCacheRequest(
                    RollbackInputCacheSource::Prediction,
                    input.player_slot,
                    input.frame,
                    nLastFrameId,
                    input.input_value);
            req.bStockDrainComplete = stock_drain_complete;
            req.bDrainBypass =
                cache_mode == RollbackCacheOrderingMode::DrainBypass;
            RollbackInputCacheAccessReport out = m_cache.write(req);
            if (out.ok && out.wrote)
                ++m_cache_writes;
            return out;
        }

        RollbackRemotePredictionReport predict_remote_metadata_input(
            uint32_t remote_frame) noexcept
        {
            return m_session.predict_remote_input(remote_frame);
        }

        RollbackInputCacheAccessReport apply_confirmed_gameplay_input(
            const RollbackDecodedGameplayInput& input,
            int32_t nLastFrameId,
            bool on_game_thread,
            bool stock_drain_complete,
            bool network_thread_wants_live_cache_write,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            if (!decoded_route_ok(input))
                return decoded_route_rejected(input);
            RollbackInputCacheWriteRequest req =
                RollbackInputCacheRequest(
                    RollbackInputCacheSource::ConfirmedRemote,
                    input.player_slot,
                    input.frame,
                    nLastFrameId,
                    input.input_value);
            req.bOnGameThread = on_game_thread;
            req.bNetworkThread = network_thread_wants_live_cache_write;
            req.bStockDrainComplete = stock_drain_complete;
            req.bDrainBypass =
                cache_mode == RollbackCacheOrderingMode::DrainBypass;
            RollbackInputCacheAccessReport out = m_cache.write(req);
            if (out.ok && out.wrote)
                ++m_cache_writes;
            return out;
        }

        RollbackInputCacheAccessReport consume_remote_input(
            const RollbackDecodedGameplayInput& input,
            int32_t nLastFrameId) const noexcept
        {
            if (!decoded_route_ok(input))
                return decoded_route_rejected(input);
            return m_cache.consume(
                input.player_slot,
                input.frame,
                nLastFrameId);
        }

        uint32_t queue_count() const noexcept
        {
            return m_queue.count();
        }

        uint32_t enqueued_packets() const noexcept
        {
            return m_queue.enqueued_packets();
        }

        uint32_t drained_packets() const noexcept
        {
            return m_queue.drained_packets();
        }

        uint32_t rejected_packets() const noexcept
        {
            return m_queue.rejected_packets();
        }

        uint32_t cache_writes() const noexcept
        {
            return m_cache_writes;
        }

        const RollbackTransportMetrics& metrics() const noexcept
        {
            return m_session.metrics();
        }

    private:
        bool decoded_route_ok(
            const RollbackDecodedGameplayInput& input) const noexcept
        {
            return !m_require_decoded_route
                || RollbackDecodedGameplayInputRouteMatches(
                    input,
                    m_expected_source_peer,
                    m_expected_dest_peer,
                    m_expected_session_id);
        }

        static RollbackInputCacheAccessReport decoded_route_rejected(
            const RollbackDecodedGameplayInput& input) noexcept
        {
            RollbackInputCacheAccessReport out {};
            out.status =
                RollbackInputCacheAccessStatus::DecodedInputRouteMismatch;
            out.failure = "decoded-gameplay-route-mismatch";
            out.dwPlayerSlot = input.player_slot;
            out.dwFrameIndex = input.frame;
            out.nLastFrameId = 0;
            out.dwInputValue = input.input_value;
            return out;
        }

        RollbackLiveTransportQueue<QueueN> m_queue {};
        RollbackOnlineSessionModel<HistoryN> m_session {};
        RollbackInputCacheShadow<2, 512> m_cache {};
        uint32_t m_cache_writes {0};
        uint8_t m_expected_source_peer {0};
        uint8_t m_expected_dest_peer {0};
        uint64_t m_expected_session_id {0};
        bool m_require_decoded_route {false};
    };

    static inline RollbackLivePeerPipelineSelfTestReport
    RunRollbackLivePeerPipelineSelfTest() noexcept
    {
        RollbackLivePeerPipelineSelfTestReport report {};
        report.failure = "ok";

        static constexpr uint8_t kRemotePeer = 0xA0;
        static constexpr uint8_t kLocalPeer = 0xB0;
        static constexpr uint64_t kSessionId = 0x504950454C495645ull;
        RollbackLivePeerPipeline<4, 128> pipeline {};
        pipeline.reset(12, RollbackHashPolicy::Enforced);

        std::array<uint8_t, 4> payload0 {0x10, 0x20, 0x30, 0x40};
        RollbackGekkoBridgeWirePacket wire0 {};
        report.bridge_enqueue_ok =
            MakeRollbackLiveTransportTestWire(
                kRemotePeer,
                kLocalPeer,
                kSessionId,
                0,
                kRollbackTransportNoFrame,
                payload0.data(),
                payload0.size(),
                wire0)
            && pipeline.enqueue_hrg1_wire(
                wire0.bytes.data(),
                wire0.size,
                kRemotePeer,
                kLocalPeer,
                kSessionId)
            && pipeline.queue_count() == 1;
        report.network_receive_queued_only =
            report.bridge_enqueue_ok
            && pipeline.metrics().packets_received == 0
            && pipeline.cache_writes() == 0;

        const RollbackLivePeerPipelineDrainReport blocked =
            pipeline.drain_metadata_to_session(0, false);
        const RollbackDecodedGameplayInput gameplay0 {0, 1, 0x44};
        const RollbackInputCacheAccessReport before_cache =
            pipeline.consume_remote_input(gameplay0, 0);
        report.stock_drain_required =
            blocked.drain.left_queued
            && blocked.drain.status
                == RollbackLiveTransportDrainStatus::CacheOrderingRejected
            && pipeline.queue_count() == 1
            && before_cache.status == RollbackInputCacheAccessStatus::CacheMiss
            && pipeline.cache_writes() == 0;

        const RollbackLivePeerPipelineDrainReport drained =
            pipeline.drain_metadata_to_session(0, true);
        const RollbackInputCacheAccessReport post_metadata_cache =
            pipeline.consume_remote_input(gameplay0, 0);
        report.metadata_drains_to_session =
            drained.ok
            && drained.metadata_accepted
            && drained.cache_untouched
            && pipeline.metrics().packets_accepted == 1
            && pipeline.queue_count() == 0;
        report.bridge_payload_not_cache_input =
            report.metadata_drains_to_session
            && drained.drain.metadata.local_input != gameplay0.input_value
            && post_metadata_cache.status
                == RollbackInputCacheAccessStatus::CacheMiss;

        const RollbackDecodedGameplayInput predicted0 {0, 1, 0x00};
        const RollbackInputCacheAccessReport pred0 =
            pipeline.predict_remote_input(predicted0, 0, true);
        report.prediction_cache_write_ok =
            pred0.ok && pred0.wrote && pred0.source_prediction;

        const RollbackInputCacheAccessReport confirm0 =
            pipeline.apply_confirmed_gameplay_input(
                gameplay0,
                0,
                true,
                true,
                false);
        const RollbackInputCacheAccessReport consume0 =
            pipeline.consume_remote_input(gameplay0, 0);
        report.confirmed_input_replaces_prediction =
            confirm0.ok
            && confirm0.wrote
            && confirm0.replaced_prediction
            && confirm0.requires_correction;
        report.cache_consume_confirmed =
            consume0.ok
            && consume0.source_confirmed
            && consume0.exactly_one_source
            && consume0.dwInputValue == gameplay0.input_value;

        const RollbackInputCacheAccessReport duplicate =
            pipeline.apply_confirmed_gameplay_input(
                gameplay0,
                0,
                true,
                true,
                false);
        report.duplicate_confirmed_idempotent =
            duplicate.ok
            && duplicate.duplicate
            && duplicate.source_confirmed;

        const RollbackInputCacheAccessReport pred_over_confirmed =
            pipeline.predict_remote_input({0, 1, 0x45}, 0, true);
        report.prediction_over_confirmed_rejected =
            !pred_over_confirmed.ok
            && pred_over_confirmed.status
                == RollbackInputCacheAccessStatus::PredictionOverConfirmed;

        report.wrong_identity_rejected =
            !pipeline.enqueue_hrg1_wire(
                wire0.bytes.data(),
                wire0.size,
                static_cast<uint8_t>(kRemotePeer + 1),
                kLocalPeer,
                kSessionId)
            && !pipeline.enqueue_hrg1_wire(
                wire0.bytes.data(),
                wire0.size,
                kRemotePeer,
                static_cast<uint8_t>(kLocalPeer + 1),
                kSessionId)
            && !pipeline.enqueue_hrg1_wire(
                wire0.bytes.data(),
                wire0.size,
                kRemotePeer,
                kLocalPeer,
                kSessionId ^ 0x10ull);

        RollbackLivePeerPipeline<2, 128> late_pipeline {};
        late_pipeline.reset(2, RollbackHashPolicy::Enforced);
        const bool late_enqueued =
            late_pipeline.enqueue_hrg1_wire(
                wire0.bytes.data(),
                wire0.size,
                kRemotePeer,
                kLocalPeer,
                kSessionId);
        const RollbackLivePeerPipelineDrainReport late =
            late_pipeline.drain_metadata_to_session(10, true);
        const RollbackInputCacheAccessReport late_cache =
            late_pipeline.consume_remote_input(gameplay0, 0);
        report.over_window_no_cache_write =
            late_enqueued
            && late.drain.drained
            && late.drain.status
                == RollbackLiveTransportDrainStatus::OverWindowLate
            && late.cache_untouched
            && late_cache.status == RollbackInputCacheAccessStatus::CacheMiss;

        const RollbackInputCacheAccessReport network_bad =
            pipeline.apply_confirmed_gameplay_input(
                {1, 1, 0x55},
                1,
                false,
                false,
                true);
        report.network_thread_cache_write_rejected =
            !network_bad.ok
            && network_bad.status
                == RollbackInputCacheAccessStatus::NetworkThreadCacheWrite;

        RollbackLivePeerPipeline<2, 128> bypass_pipeline {};
        bypass_pipeline.reset(12, RollbackHashPolicy::Enforced);
        const RollbackDecodedGameplayInput bypass_input {2, 1, 0x66};
        const RollbackInputCacheAccessReport bypass =
            bypass_pipeline.apply_confirmed_gameplay_input(
                bypass_input,
                2,
                true,
                false,
                false,
                RollbackCacheOrderingMode::DrainBypass);
        const RollbackInputCacheAccessReport bypass_consume =
            bypass_pipeline.consume_remote_input(bypass_input, 2);
        report.drain_bypass_confirmed_input =
            bypass.ok
            && bypass.wrote
            && bypass_consume.ok
            && bypass_consume.source_confirmed
            && bypass_consume.dwInputValue == bypass_input.input_value;

        report.enqueued_packets =
            pipeline.enqueued_packets()
            + late_pipeline.enqueued_packets()
            + bypass_pipeline.enqueued_packets();
        report.drained_packets =
            pipeline.drained_packets()
            + late_pipeline.drained_packets()
            + bypass_pipeline.drained_packets();
        report.rejected_packets =
            pipeline.rejected_packets()
            + late_pipeline.rejected_packets()
            + bypass_pipeline.rejected_packets();
        report.cache_write_sequence =
            pipeline.cache_writes()
            + late_pipeline.cache_writes()
            + bypass_pipeline.cache_writes();

        report.ok =
            report.bridge_enqueue_ok
            && report.network_receive_queued_only
            && report.stock_drain_required
            && report.metadata_drains_to_session
            && report.bridge_payload_not_cache_input
            && report.prediction_cache_write_ok
            && report.confirmed_input_replaces_prediction
            && report.cache_consume_confirmed
            && report.duplicate_confirmed_idempotent
            && report.prediction_over_confirmed_rejected
            && report.wrong_identity_rejected
            && report.over_window_no_cache_write
            && report.network_thread_cache_write_rejected
            && report.drain_bypass_confirmed_input;
        if (!report.ok)
            report.failure = "live-peer-pipeline-selftest-failed";
        return report;
    }
}
