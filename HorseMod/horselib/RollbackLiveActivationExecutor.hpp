// ============================================================================
// Horse::RollbackLiveActivationExecutor
//
// Activation-gated model for the future live HRG1/Gekko path. This does not hook
// Steam or SC6 packet I/O; it proves that the path after activation refuses all
// packet/cache work until the live activation policy is Ready.
// ============================================================================

#pragma once

#include "RollbackLiveActivationGate.hpp"
#include "RollbackLivePeerPipeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    struct RollbackLiveActivationExecutorEnqueueReport
    {
        bool ok {false};
        bool armed {false};
        bool enqueued {false};
        bool queued_only {false};
        bool strict_identity_used {false};
        uint32_t queue_count {0};
        const char* failure {"not-run"};
    };

    struct RollbackLiveActivationExecutorDrainReport
    {
        bool ok {false};
        bool armed {false};
        bool drained {false};
        bool left_queued {false};
        bool stock_drain_required {false};
        bool metadata_accepted {false};
        bool correction_required {false};
        bool cache_untouched {false};
        RollbackLivePeerPipelineDrainReport pipeline {};
        const char* failure {"not-run"};
    };

    struct RollbackLiveActivationExecutorCacheReport
    {
        bool ok {false};
        bool armed {false};
        bool wrote {false};
        bool consumed {false};
        bool duplicate {false};
        bool replaced_prediction {false};
        bool requires_correction {false};
        bool source_prediction {false};
        bool source_confirmed {false};
        bool network_thread_rejected {false};
        bool decoded_route_rejected {false};
        RollbackInputCacheAccessReport access {};
        const char* failure {"not-run"};
    };

    struct RollbackLiveActivationExecutorSelfTestReport
    {
        bool ok {false};
        bool activation_required_rejected {false};
        bool readiness_only_rejected {false};
        bool stock_surface_rejected {false};
        bool provenance_required_rejected {false};
        bool route_identity_rejected {false};
        bool activation_ready {false};
        bool live_enqueue_ok {false};
        bool network_receive_queued_only {false};
        bool stock_drain_required {false};
        bool metadata_drained {false};
        bool metadata_not_gameplay_input {false};
        bool prediction_written {false};
        bool decoded_gameplay_applied {false};
        bool confirmed_consumed {false};
        bool network_thread_cache_rejected {false};
        bool wrong_source_rejected {false};
        bool wrong_destination_rejected {false};
        bool wrong_session_rejected {false};
        bool decoded_route_rejected {false};
        uint32_t enqueued_packets {0};
        uint32_t drained_packets {0};
        uint32_t rejected_packets {0};
        uint32_t cache_write_sequence {0};
        const char* failure {"not-run"};
    };

    template<size_t QueueN, size_t HistoryN>
    class RollbackLiveActivationExecutor
    {
    public:
        RollbackLiveActivationReport arm(
            const RollbackLiveActivationRequest& req,
            uint32_t rollback_window_frames,
            RollbackHashPolicy hash_policy) noexcept
        {
            m_activation = EvaluateRollbackLiveActivation(req);
            m_armed = m_activation.ok && m_activation.activation_ready;
            m_expected_source_peer = req.source_peer;
            m_expected_dest_peer = req.destination_peer;
            m_expected_session_id = req.session_id;
            m_pipeline.reset(rollback_window_frames, hash_policy);
            if (m_armed)
            {
                m_pipeline.require_decoded_input_route(
                    m_expected_source_peer,
                    m_expected_dest_peer,
                    m_expected_session_id);
            }
            return m_activation;
        }

        bool armed() const noexcept
        {
            return m_armed;
        }

        const RollbackLiveActivationReport& activation() const noexcept
        {
            return m_activation;
        }

        RollbackLiveActivationExecutorEnqueueReport enqueue_hrg1_wire(
            const uint8_t* bytes,
            size_t size) noexcept
        {
            RollbackLiveActivationExecutorEnqueueReport out {};
            out.armed = m_armed;
            out.failure = "ok";
            if (!m_armed)
            {
                out.failure = "activation-not-ready";
                return out;
            }

            const RollbackTransportMetrics metrics_before =
                m_pipeline.metrics();
            const uint32_t cache_writes_before = m_pipeline.cache_writes();
            out.enqueued = m_pipeline.enqueue_hrg1_wire(
                bytes,
                size,
                m_expected_source_peer,
                m_expected_dest_peer,
                m_expected_session_id);
            out.queue_count = m_pipeline.queue_count();
            out.strict_identity_used =
                m_expected_source_peer != 0
                && m_expected_dest_peer != 0
                && m_expected_source_peer != m_expected_dest_peer
                && m_expected_session_id != 0;
            out.queued_only =
                m_pipeline.metrics().packets_received
                    == metrics_before.packets_received
                && m_pipeline.metrics().packets_accepted
                    == metrics_before.packets_accepted
                && m_pipeline.cache_writes() == cache_writes_before;
            out.ok = out.enqueued && out.queued_only && out.strict_identity_used;
            if (!out.ok)
                out.failure = out.enqueued
                    ? "enqueue-side-effect-policy-failed"
                    : "hrg1-enqueue-rejected";
            return out;
        }

        RollbackLiveActivationExecutorDrainReport drain_metadata_to_session(
            uint32_t local_sim_frame,
            bool stock_drain_complete,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            RollbackLiveActivationExecutorDrainReport out {};
            out.armed = m_armed;
            out.failure = "ok";
            if (!m_armed)
            {
                out.failure = "activation-not-ready";
                return out;
            }

            out.pipeline = m_pipeline.drain_metadata_to_session(
                local_sim_frame,
                stock_drain_complete,
                cache_mode);
            out.drained = out.pipeline.drain.drained;
            out.left_queued = out.pipeline.drain.left_queued;
            out.stock_drain_required =
                out.left_queued
                && out.pipeline.drain.status
                    == RollbackLiveTransportDrainStatus::CacheOrderingRejected;
            out.metadata_accepted = out.pipeline.metadata_accepted;
            out.correction_required = out.pipeline.metadata_requires_correction;
            out.cache_untouched = out.pipeline.cache_untouched;
            out.ok = out.pipeline.ok;
            out.failure = out.pipeline.failure;
            return out;
        }

        RollbackLiveActivationExecutorCacheReport predict_gameplay_input(
            const RollbackDecodedGameplayInput& input,
            bool stock_drain_complete,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            RollbackLiveActivationExecutorCacheReport out {};
            out.armed = m_armed;
            out.failure = "ok";
            if (!m_armed)
            {
                out.failure = "activation-not-ready";
                return out;
            }
            out.access = m_pipeline.predict_remote_input(
                input,
                stock_drain_complete,
                cache_mode);
            fill_cache_report(out);
            return out;
        }

        RollbackLiveActivationExecutorCacheReport apply_confirmed_gameplay_input(
            const RollbackDecodedGameplayInput& input,
            bool on_game_thread,
            bool stock_drain_complete,
            bool network_thread_wants_live_cache_write,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            RollbackLiveActivationExecutorCacheReport out {};
            out.armed = m_armed;
            out.failure = "ok";
            if (!m_armed)
            {
                out.failure = "activation-not-ready";
                return out;
            }
            out.access = m_pipeline.apply_confirmed_gameplay_input(
                input,
                on_game_thread,
                stock_drain_complete,
                network_thread_wants_live_cache_write,
                cache_mode);
            fill_cache_report(out);
            return out;
        }

        RollbackLiveActivationExecutorCacheReport consume_gameplay_input(
            const RollbackDecodedGameplayInput& input) const noexcept
        {
            RollbackLiveActivationExecutorCacheReport out {};
            out.armed = m_armed;
            out.failure = "ok";
            if (!m_armed)
            {
                out.failure = "activation-not-ready";
                return out;
            }
            out.access = m_pipeline.consume_remote_input(input);
            fill_cache_report(out);
            out.consumed = out.access.ok;
            return out;
        }

        uint32_t enqueued_packets() const noexcept
        {
            return m_pipeline.enqueued_packets();
        }

        uint32_t drained_packets() const noexcept
        {
            return m_pipeline.drained_packets();
        }

        uint32_t rejected_packets() const noexcept
        {
            return m_pipeline.rejected_packets();
        }

        uint32_t cache_writes() const noexcept
        {
            return m_pipeline.cache_writes();
        }

    private:
        static void fill_cache_report(
            RollbackLiveActivationExecutorCacheReport& out) noexcept
        {
            out.ok = out.access.ok;
            out.wrote = out.access.wrote;
            out.duplicate = out.access.duplicate;
            out.replaced_prediction = out.access.replaced_prediction;
            out.requires_correction = out.access.requires_correction;
            out.source_prediction = out.access.source_prediction;
            out.source_confirmed = out.access.source_confirmed;
            out.network_thread_rejected =
                out.access.status
                == RollbackInputCacheAccessStatus::NetworkThreadCacheWrite;
            out.decoded_route_rejected =
                out.access.status
                == RollbackInputCacheAccessStatus::DecodedInputRouteMismatch;
            out.failure = out.access.failure;
        }

        RollbackLivePeerPipeline<QueueN, HistoryN> m_pipeline {};
        RollbackLiveActivationReport m_activation {};
        uint8_t m_expected_source_peer {0};
        uint8_t m_expected_dest_peer {0};
        uint64_t m_expected_session_id {0};
        bool m_armed {false};
    };

    static inline RollbackLiveActivationRequest
    RollbackLiveActivationExecutorGoodRequest() noexcept
    {
        const RollbackLiveOnlineCaptureReport live =
            RollbackLiveActivationMakeCapture(true, false, true);
        const RollbackStockTransportRoute horse =
            RollbackLiveActivationHorseRoute(true);
        return RollbackLiveActivationRequest {
            live, horse, 0xA0u, 0xB0u, 0x4C495645414354ull, true};
    }

    static inline RollbackLiveActivationExecutorSelfTestReport
    RunRollbackLiveActivationExecutorSelfTest() noexcept
    {
        RollbackLiveActivationExecutorSelfTestReport report {};
        report.failure = "ok";
        static constexpr uint32_t kWindow = 12;
        static constexpr uint8_t kRemotePeer = 0xA0;
        static constexpr uint8_t kLocalPeer = 0xB0;
        static constexpr uint64_t kSessionId = 0x4C495645414354ull;

        RollbackLiveActivationExecutor<4, 128> executor {};
        std::array<uint8_t, 4> payload0 {0x10, 0x20, 0x30, 0x40};
        RollbackGekkoBridgeWirePacket wire0 {};
        const bool wire0_ok =
            MakeRollbackLiveTransportTestWire(
                kRemotePeer,
                kLocalPeer,
                kSessionId,
                0,
                kRollbackTransportNoFrame,
                payload0.data(),
                payload0.size(),
                wire0);
        const RollbackLiveActivationExecutorEnqueueReport unarmed =
            executor.enqueue_hrg1_wire(wire0.bytes.data(), wire0.size);
        report.activation_required_rejected =
            wire0_ok && !unarmed.ok && !unarmed.armed;

        RollbackLiveActivationRequest readiness_only =
            RollbackLiveActivationExecutorGoodRequest();
        readiness_only.capture =
            RollbackLiveActivationMakeCapture(false, false, true);
        const RollbackLiveActivationReport readiness =
            executor.arm(
                readiness_only,
                kWindow,
                RollbackHashPolicy::Enforced);
        report.readiness_only_rejected =
            !readiness.ok
            && readiness.status
                == RollbackLiveActivationStatus::LiveTrafficNotProven
            && !executor.armed();

        RollbackLiveActivationRequest stock_req =
            RollbackLiveActivationExecutorGoodRequest();
        stock_req.route = RollbackStockTransportRoute {
            kLuxOnlineTransportSendInputSlot,
            kLuxOnlineChannelInputBinary,
            true,
            true,
            true,
            true,
            true};
        const RollbackLiveActivationReport stock =
            executor.arm(stock_req, kWindow, RollbackHashPolicy::Enforced);
        report.stock_surface_rejected =
            !stock.ok
            && stock.status
                == RollbackLiveActivationStatus::StockSurfaceRejected
            && !executor.armed();

        RollbackLiveActivationRequest missing_provenance =
            RollbackLiveActivationExecutorGoodRequest();
        missing_provenance.route.horse_adapter_cookie = 0;
        const RollbackLiveActivationReport provenance =
            executor.arm(
                missing_provenance,
                kWindow,
                RollbackHashPolicy::Enforced);
        report.provenance_required_rejected =
            !provenance.ok
            && provenance.status
                == RollbackLiveActivationStatus::HorseRouteProvenanceMissing
            && !executor.armed();

        RollbackLiveActivationRequest route_identity =
            RollbackLiveActivationExecutorGoodRequest();
        route_identity.route.source_peer = static_cast<uint8_t>(kRemotePeer + 1);
        const RollbackLiveActivationReport route_id =
            executor.arm(
                route_identity,
                kWindow,
                RollbackHashPolicy::Enforced);
        report.route_identity_rejected =
            !route_id.ok
            && route_id.status
                == RollbackLiveActivationStatus::RouteIdentityMismatch
            && !executor.armed();

        const RollbackLiveActivationRequest good =
            RollbackLiveActivationExecutorGoodRequest();
        const RollbackLiveActivationReport ready =
            executor.arm(good, kWindow, RollbackHashPolicy::Enforced);
        report.activation_ready = ready.ok && executor.armed();

        const RollbackLiveActivationExecutorEnqueueReport enq =
            executor.enqueue_hrg1_wire(wire0.bytes.data(), wire0.size);
        report.live_enqueue_ok = enq.ok && enq.enqueued && enq.queue_count == 1;
        report.network_receive_queued_only =
            report.live_enqueue_ok && enq.queued_only;

        const RollbackLiveActivationExecutorDrainReport blocked =
            executor.drain_metadata_to_session(0, false);
        report.stock_drain_required =
            blocked.stock_drain_required
            && blocked.left_queued
            && !blocked.drained;

        const RollbackLiveActivationExecutorDrainReport drained =
            executor.drain_metadata_to_session(0, true);
        const RollbackDecodedGameplayInput gameplay0 =
            RollbackDecodedGameplayInputWithRoute(
                {0, 1, 0x44},
                kRemotePeer,
                kLocalPeer,
                kSessionId);
        const RollbackLiveActivationExecutorCacheReport before_cache =
            executor.consume_gameplay_input(gameplay0);
        report.metadata_drained =
            drained.ok
            && drained.metadata_accepted
            && drained.drained
            && drained.cache_untouched;
        report.metadata_not_gameplay_input =
            report.metadata_drained
            && drained.pipeline.drain.metadata.local_input
                != gameplay0.input_value
            && !before_cache.ok
            && before_cache.access.status
                == RollbackInputCacheAccessStatus::CacheMiss;

        const RollbackLiveActivationExecutorCacheReport predicted =
            executor.predict_gameplay_input(
                RollbackDecodedGameplayInputWithRoute(
                    {0, 1, 0x00},
                    kRemotePeer,
                    kLocalPeer,
                    kSessionId),
                true);
        report.prediction_written =
            predicted.ok
            && predicted.wrote
            && predicted.source_prediction;

        const RollbackLiveActivationExecutorCacheReport applied =
            executor.apply_confirmed_gameplay_input(
                gameplay0,
                true,
                true,
                false);
        report.decoded_gameplay_applied =
            applied.ok
            && applied.wrote
            && applied.source_confirmed
            && applied.replaced_prediction
            && applied.requires_correction;
        const RollbackLiveActivationExecutorCacheReport consumed =
            executor.consume_gameplay_input(gameplay0);
        report.confirmed_consumed =
            consumed.ok
            && consumed.consumed
            && consumed.source_confirmed
            && consumed.access.dwInputValue == gameplay0.input_value;

        const RollbackLiveActivationExecutorCacheReport network_bad =
            executor.apply_confirmed_gameplay_input(
                RollbackDecodedGameplayInputWithRoute(
                    {1, 1, 0x55},
                    kRemotePeer,
                    kLocalPeer,
                    kSessionId),
                false,
                false,
                true);
        report.network_thread_cache_rejected =
            !network_bad.ok && network_bad.network_thread_rejected;

        RollbackGekkoBridgeWirePacket wrong_source_wire {};
        const bool wrong_source_wire_ok =
            MakeRollbackLiveTransportTestWire(
                static_cast<uint8_t>(kRemotePeer + 1),
                kLocalPeer,
                kSessionId,
                1,
                0,
                payload0.data(),
                payload0.size(),
                wrong_source_wire);
        const RollbackLiveActivationExecutorEnqueueReport wrong_source =
            executor.enqueue_hrg1_wire(
                wrong_source_wire.bytes.data(),
                wrong_source_wire.size);
        report.wrong_source_rejected =
            wrong_source_wire_ok && !wrong_source.ok && !wrong_source.enqueued;

        RollbackGekkoBridgeWirePacket wrong_dest_wire {};
        const bool wrong_dest_wire_ok =
            MakeRollbackLiveTransportTestWire(
                kRemotePeer,
                static_cast<uint8_t>(kLocalPeer + 1),
                kSessionId,
                1,
                0,
                payload0.data(),
                payload0.size(),
                wrong_dest_wire);
        const RollbackLiveActivationExecutorEnqueueReport wrong_dest =
            executor.enqueue_hrg1_wire(
                wrong_dest_wire.bytes.data(),
                wrong_dest_wire.size);
        report.wrong_destination_rejected =
            wrong_dest_wire_ok && !wrong_dest.ok && !wrong_dest.enqueued;

        RollbackGekkoBridgeWirePacket wrong_session_wire {};
        const bool wrong_session_wire_ok =
            MakeRollbackLiveTransportTestWire(
                kRemotePeer,
                kLocalPeer,
                kSessionId ^ 0x100ull,
                1,
                0,
                payload0.data(),
                payload0.size(),
                wrong_session_wire);
        const RollbackLiveActivationExecutorEnqueueReport wrong_session =
            executor.enqueue_hrg1_wire(
                wrong_session_wire.bytes.data(),
                wrong_session_wire.size);
        report.wrong_session_rejected =
            wrong_session_wire_ok
            && !wrong_session.ok
            && !wrong_session.enqueued;

        const RollbackLiveActivationExecutorCacheReport decoded_route_bad =
            executor.apply_confirmed_gameplay_input(
                RollbackDecodedGameplayInputWithRoute(
                    {2, 1, 0x77},
                    static_cast<uint8_t>(kRemotePeer + 1),
                    kLocalPeer,
                    kSessionId),
                true,
                true,
                false);
        report.decoded_route_rejected =
            !decoded_route_bad.ok
            && decoded_route_bad.decoded_route_rejected;

        report.enqueued_packets = executor.enqueued_packets();
        report.drained_packets = executor.drained_packets();
        report.rejected_packets = executor.rejected_packets();
        report.cache_write_sequence = executor.cache_writes();

        report.ok =
            report.activation_required_rejected
            && report.readiness_only_rejected
            && report.stock_surface_rejected
            && report.provenance_required_rejected
            && report.route_identity_rejected
            && report.activation_ready
            && report.live_enqueue_ok
            && report.network_receive_queued_only
            && report.stock_drain_required
            && report.metadata_drained
            && report.metadata_not_gameplay_input
            && report.prediction_written
            && report.decoded_gameplay_applied
            && report.confirmed_consumed
            && report.network_thread_cache_rejected
            && report.wrong_source_rejected
            && report.wrong_destination_rejected
            && report.wrong_session_rejected
            && report.decoded_route_rejected
            && report.enqueued_packets == 1
            && report.drained_packets == 1
            && report.rejected_packets == 3
            && report.cache_write_sequence == 2;
        if (!report.ok)
            report.failure = "live-activation-executor-selftest-failed";
        return report;
    }
}
