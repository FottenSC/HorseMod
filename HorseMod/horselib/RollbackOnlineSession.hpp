// ============================================================================
// Horse::RollbackOnlineSession
//
// Testable adapter/session layer between absolute-frame Horse rollback packets
// and the local rollback controller. This deliberately does not hook SC6's
// live transport yet and never writes the stock ALuxBattleFrameInputLog cache.
// ============================================================================

#pragma once

#include "RollbackInputCacheAdapter.hpp"
#include "RollbackTransport.hpp"

#include <cstdint>

namespace Horse
{
    enum class RollbackOnlineAdapterStatus : uint8_t
    {
        AcceptedNoCorrection,
        AcceptedCorrectionRequired,
        Duplicate,
        Conflict,
        OverWindowLate,
        InvalidPacket,
        HashRejected,
        CacheOrderingRejected,
    };

    struct RollbackRemotePredictionReport
    {
        uint32_t frame {0};
        uint64_t input {0};
        bool predicted {false};
        bool already_confirmed {false};
        uint32_t prediction_age_frames {0};
    };

    struct RollbackOnlineReceiveReport
    {
        bool ok {false};
        bool accepted {false};
        bool requires_correction {false};
        bool cache_ordering_ok {false};
        bool state_hash_warning {false};
        bool may_write_live_cache {false};
        RollbackOnlineAdapterStatus status {
            RollbackOnlineAdapterStatus::InvalidPacket};
        RollbackTransportAcceptStatus transport_status {
            RollbackTransportAcceptStatus::InvalidPacket};
        RollbackStateHashStatus hash_status {
            RollbackStateHashStatus::RequiredButMissing};
        uint32_t remote_frame {kRollbackTransportNoFrame};
        uint32_t correction_start_frame {kRollbackTransportNoFrame};
        uint32_t rollback_depth_frames {0};
        uint32_t contiguous_remote_frame {kRollbackTransportNoFrame};
        const char* failure {"not-run"};
    };

    template<size_t N>
    class RollbackOnlineSessionModel
    {
    public:
        void reset(
            uint32_t rollback_window_frames,
            RollbackHashPolicy hash_policy) noexcept
        {
            m_history.clear();
            m_peer.clear();
            m_rollback_window =
                rollback_window_frames == 0 ? 1 : rollback_window_frames;
            if (m_rollback_window > 60) m_rollback_window = 60;
            m_hash_policy = hash_policy;
        }

        RollbackTransportPacket build_local_input_packet(
            uint32_t local_frame,
            uint64_t local_input,
            uint64_t state_hash,
            bool include_state_hash) noexcept
        {
            RollbackInputFrame& frame =
                m_history.write(static_cast<int32_t>(local_frame));
            frame.input.p1 = local_input;
            frame.p1_confirmed = true;
            frame.p1_predicted = false;

            RollbackTransportPacket packet {};
            packet.local_frame = local_frame;
            packet.local_input = local_input;
            packet.last_confirmed_remote_frame =
                m_peer.metrics().contiguous_remote_frame;
            if (include_state_hash)
            {
                packet.flags |= RollbackTransportFlag_StateHashPresent;
                packet.state_hash = state_hash;
            }
            return packet;
        }

        RollbackRemotePredictionReport predict_remote_input(
            uint32_t remote_frame) noexcept
        {
            RollbackRemotePredictionReport report {};
            report.frame = remote_frame;

            const RollbackInputFrame* existing =
                m_history.find(static_cast<int32_t>(remote_frame));
            if (existing && existing->p2_confirmed)
            {
                report.input = existing->input.p2;
                report.already_confirmed = true;
                return report;
            }

            RollbackInputFrame& frame =
                m_history.write(static_cast<int32_t>(remote_frame));
            frame.input.p2 = remote_prediction_seed(remote_frame);
            frame.p2_predicted = true;
            frame.p2_confirmed = false;

            report.input = frame.input.p2;
            report.predicted = true;
            const uint32_t contiguous =
                m_peer.metrics().contiguous_remote_frame;
            report.prediction_age_frames =
                contiguous == kRollbackTransportNoFrame
                ? remote_frame + 1
                : (remote_frame > contiguous
                    ? remote_frame - contiguous
                    : 0);
            return report;
        }

        RollbackOnlineReceiveReport receive_remote_packet(
            const RollbackTransportPacket& packet,
            uint32_t local_sim_frame,
            uint64_t expected_state_hash,
            bool on_game_thread,
            bool stock_drain_complete,
            bool network_thread_wants_live_cache_write,
            RollbackCacheOrderingMode cache_mode =
                RollbackCacheOrderingMode::StockDrainBeforePrediction) noexcept
        {
            RollbackOnlineReceiveReport out {};
            out.failure = "ok";
            out.remote_frame = packet.local_frame;

            const RollbackCacheOrderingDecision cache_order =
                ValidateRollbackCacheOrdering(
                    cache_mode,
                    on_game_thread,
                    stock_drain_complete,
                    network_thread_wants_live_cache_write);
            out.cache_ordering_ok = cache_order.ok;
            out.may_write_live_cache = cache_order.may_write_live_cache;
            if (!cache_order.ok)
            {
                out.status = RollbackOnlineAdapterStatus::CacheOrderingRejected;
                out.failure = cache_order.reason;
                return out;
            }

            const RollbackStateHashDecision hash_decision =
                CheckRollbackStateHash(packet, expected_state_hash, m_hash_policy);
            out.hash_status = hash_decision.status;
            out.state_hash_warning = hash_decision.warning;
            if (!hash_decision.ok)
            {
                out.status = RollbackOnlineAdapterStatus::HashRejected;
                out.failure = hash_decision.reason;
                return out;
            }

            const RollbackInputFrame* predicted =
                m_history.find(static_cast<int32_t>(packet.local_frame));
            const bool had_prediction =
                predicted && predicted->p2_predicted && !predicted->p2_confirmed;
            const uint64_t predicted_input =
                had_prediction ? predicted->input.p2 : 0;

            const RollbackTransportAcceptReport accept =
                m_peer.accept_remote_input(
                    packet, local_sim_frame, m_rollback_window);
            out.transport_status = accept.status;
            out.contiguous_remote_frame = accept.contiguous_remote_frame;
            if (!accept.accepted)
            {
                out.failure = accept.failure;
                switch (accept.status)
                {
                case RollbackTransportAcceptStatus::Duplicate:
                    out.status = RollbackOnlineAdapterStatus::Duplicate;
                    out.ok = true;
                    return out;
                case RollbackTransportAcceptStatus::Conflict:
                    out.status = RollbackOnlineAdapterStatus::Conflict;
                    return out;
                case RollbackTransportAcceptStatus::OverWindowLate:
                    out.status = RollbackOnlineAdapterStatus::OverWindowLate;
                    return out;
                case RollbackTransportAcceptStatus::InvalidPacket:
                default:
                    out.status = RollbackOnlineAdapterStatus::InvalidPacket;
                    return out;
                }
            }

            RollbackInputFrame& frame =
                m_history.write(static_cast<int32_t>(packet.local_frame));
            frame.input.p2 = packet.local_input;
            frame.p2_confirmed = true;
            frame.p2_predicted = false;

            out.accepted = true;
            out.ok = true;
            out.requires_correction =
                had_prediction && predicted_input != packet.local_input;
            if (out.requires_correction)
            {
                out.status =
                    RollbackOnlineAdapterStatus::AcceptedCorrectionRequired;
                out.correction_start_frame = packet.local_frame;
                out.rollback_depth_frames =
                    local_sim_frame >= packet.local_frame
                    ? local_sim_frame - packet.local_frame
                    : 0;
            }
            else
            {
                out.status = RollbackOnlineAdapterStatus::AcceptedNoCorrection;
                out.correction_start_frame = kRollbackTransportNoFrame;
            }
            out.failure = "ok";
            return out;
        }

        const RollbackTransportMetrics& metrics() const noexcept
        {
            return m_peer.metrics();
        }

    private:
        uint64_t remote_prediction_seed(uint32_t remote_frame) const noexcept
        {
            bool found = false;
            uint32_t best_frame = 0;
            uint64_t best_input = 0;
            m_history.for_each(
                [&](const RollbackInputFrame& frame) noexcept {
                    if (!frame.p2_confirmed || frame.frame < 0)
                        return;
                    const uint32_t confirmed_frame =
                        static_cast<uint32_t>(frame.frame);
                    if (confirmed_frame >= remote_frame)
                        return;
                    if (!found || confirmed_frame > best_frame)
                    {
                        found = true;
                        best_frame = confirmed_frame;
                        best_input = frame.input.p2;
                    }
                });
            return found ? best_input : 0;
        }

        RollbackInputHistory<N> m_history {};
        RollbackTransportPeerModel<N> m_peer {};
        uint32_t m_rollback_window {60};
        RollbackHashPolicy m_hash_policy {RollbackHashPolicy::WarnOnly};
    };

    struct RollbackOnlineSessionSelfTestReport
    {
        bool ok {false};
        bool local_packet_ack {false};
        bool prediction_created {false};
        bool no_correction_for_matching_prediction {false};
        bool correction_for_delayed_mismatch {false};
        bool reorder_correction {false};
        bool duplicate_rejected {false};
        bool conflict_rejected {false};
        bool over_window_rejected {false};
        bool reorder_preserves_prediction_seed {false};
        bool future_input_not_used_for_earlier_prediction {false};
        bool cache_write_rejected {false};
        bool stock_drain_required {false};
        bool drain_bypass_ok {false};
        bool cache_provenance_ok {false};
        bool hash_enforced_rejected {false};
        bool hash_warn_allows_correction {false};
        const char* failure {"not-run"};
    };

    static inline RollbackOnlineSessionSelfTestReport
    RunRollbackOnlineSessionSelfTest() noexcept
    {
        RollbackOnlineSessionSelfTestReport report {};
        report.failure = "ok";

        RollbackOnlineSessionModel<128> session {};
        session.reset(60, RollbackHashPolicy::Enforced);

        const uint64_t expected_hash = 0xAA5512347788CCDDull;
        RollbackTransportPacket local0 =
            session.build_local_input_packet(0, 0x100, expected_hash, true);
        report.local_packet_ack =
            local0.local_frame == 0
            && local0.local_input == 0x100
            && local0.last_confirmed_remote_frame
                == kRollbackTransportNoFrame
            && (local0.flags & RollbackTransportFlag_StateHashPresent) != 0;

        RollbackRemotePredictionReport pred0 =
            session.predict_remote_input(0);
        report.prediction_created =
            pred0.predicted && pred0.input == 0
            && pred0.prediction_age_frames == 1;

        RollbackTransportPacket remote0 {};
        remote0.flags = RollbackTransportFlag_InputPresent
            | RollbackTransportFlag_StateHashPresent;
        remote0.local_frame = 0;
        remote0.local_input = 0;
        remote0.state_hash = expected_hash;
        remote0.last_confirmed_remote_frame = 0;
        const RollbackOnlineReceiveReport recv0 =
            session.receive_remote_packet(
                remote0, 0, expected_hash, true, true, false);
        report.no_correction_for_matching_prediction =
            recv0.ok && recv0.accepted && !recv0.requires_correction
            && recv0.status == RollbackOnlineAdapterStatus::AcceptedNoCorrection
            && !recv0.may_write_live_cache;

        RollbackTransportPacket local1 =
            session.build_local_input_packet(1, 0x101, expected_hash, true);
        report.local_packet_ack = report.local_packet_ack
            && local1.last_confirmed_remote_frame == 0;

        (void)session.predict_remote_input(1);
        (void)session.predict_remote_input(2);
        (void)session.predict_remote_input(3);

        RollbackTransportPacket remote2 = remote0;
        remote2.local_frame = 2;
        remote2.local_input = 0x22;
        remote2.last_confirmed_remote_frame = 1;
        const RollbackOnlineReceiveReport recv2 =
            session.receive_remote_packet(
                remote2, 4, expected_hash, true, true, false);
        report.correction_for_delayed_mismatch =
            recv2.ok && recv2.accepted && recv2.requires_correction
            && recv2.correction_start_frame == 2
            && recv2.rollback_depth_frames == 2
            && recv2.status
                == RollbackOnlineAdapterStatus::AcceptedCorrectionRequired
            && !recv2.may_write_live_cache;

        RollbackTransportPacket remote1 = remote0;
        remote1.local_frame = 1;
        remote1.local_input = 0x11;
        remote1.last_confirmed_remote_frame = 1;
        const RollbackOnlineReceiveReport recv1 =
            session.receive_remote_packet(
                remote1, 4, expected_hash, true, true, false);
        report.reorder_correction =
            recv1.ok && recv1.accepted && recv1.requires_correction
            && recv1.correction_start_frame == 1
            && recv1.rollback_depth_frames == 3
            && session.metrics().contiguous_remote_frame == 2
            && session.metrics().reordered == 1;

        const RollbackRemotePredictionReport pred3_after_reorder =
            session.predict_remote_input(3);
        report.reorder_preserves_prediction_seed =
            pred3_after_reorder.predicted
            && pred3_after_reorder.input == 0x22;

        RollbackOnlineSessionModel<128> earlier_prediction_session {};
        earlier_prediction_session.reset(60, RollbackHashPolicy::Enforced);
        RollbackTransportPacket early0 = remote0;
        early0.local_frame = 0;
        early0.local_input = 0x10;
        const RollbackOnlineReceiveReport early_recv0 =
            earlier_prediction_session.receive_remote_packet(
                early0, 0, expected_hash, true, true, false);
        RollbackTransportPacket early2 = remote0;
        early2.local_frame = 2;
        early2.local_input = 0x22;
        const RollbackOnlineReceiveReport early_recv2 =
            earlier_prediction_session.receive_remote_packet(
                early2, 2, expected_hash, true, true, false);
        const RollbackRemotePredictionReport early_pred1 =
            earlier_prediction_session.predict_remote_input(1);
        report.future_input_not_used_for_earlier_prediction =
            early_recv0.ok && early_recv0.accepted
            && early_recv2.ok && early_recv2.accepted
            && early_pred1.predicted
            && early_pred1.input == 0x10;

        const RollbackOnlineReceiveReport dup1 =
            session.receive_remote_packet(
                remote1, 4, expected_hash, true, true, false);
        report.duplicate_rejected =
            dup1.ok && !dup1.accepted
            && dup1.status == RollbackOnlineAdapterStatus::Duplicate;

        RollbackTransportPacket conflict1 = remote1;
        conflict1.local_input = 0x12;
        const RollbackOnlineReceiveReport bad1 =
            session.receive_remote_packet(
                conflict1, 4, expected_hash, true, true, false);
        report.conflict_rejected =
            !bad1.ok && !bad1.accepted
            && bad1.status == RollbackOnlineAdapterStatus::Conflict;

        RollbackTransportPacket late = remote0;
        late.local_frame = 30;
        late.local_input = 0x30;
        const RollbackOnlineReceiveReport late_report =
            session.receive_remote_packet(
                late, 100, expected_hash, true, true, false);
        report.over_window_rejected =
            !late_report.ok && !late_report.accepted
            && late_report.status == RollbackOnlineAdapterStatus::OverWindowLate;

        RollbackTransportPacket network_thread = remote0;
        network_thread.local_frame = 5;
        const RollbackOnlineReceiveReport cache_bad =
            session.receive_remote_packet(
                network_thread,
                5,
                expected_hash,
                false,
                false,
                true);
        report.cache_write_rejected =
            !cache_bad.ok
            && cache_bad.status
                == RollbackOnlineAdapterStatus::CacheOrderingRejected
            && !cache_bad.may_write_live_cache;

        RollbackTransportPacket drain_not_done = remote0;
        drain_not_done.local_frame = 6;
        const RollbackOnlineReceiveReport drain_bad =
            session.receive_remote_packet(
                drain_not_done,
                6,
                expected_hash,
                true,
                false,
                false);
        report.stock_drain_required =
            !drain_bad.ok
            && drain_bad.status
                == RollbackOnlineAdapterStatus::CacheOrderingRejected;

        RollbackTransportPacket bypass_packet = remote0;
        bypass_packet.local_frame = 7;
        bypass_packet.local_input = 0x70;
        const RollbackOnlineReceiveReport bypass =
            session.receive_remote_packet(
                bypass_packet,
                7,
                expected_hash,
                true,
                false,
                false,
                RollbackCacheOrderingMode::DrainBypass);
        report.drain_bypass_ok =
            bypass.ok && bypass.accepted && !bypass.may_write_live_cache;

        const RollbackInputCacheAdapterSelfTestReport cache_report =
            RunRollbackInputCacheAdapterSelfTest();
        report.cache_provenance_ok = cache_report.ok;

        RollbackTransportPacket hash_bad = remote0;
        hash_bad.local_frame = 8;
        hash_bad.local_input = 0x80;
        hash_bad.state_hash = expected_hash ^ 1ull;
        const RollbackOnlineReceiveReport hash_reject =
            session.receive_remote_packet(
                hash_bad, 8, expected_hash, true, true, false);
        report.hash_enforced_rejected =
            !hash_reject.ok
            && hash_reject.status == RollbackOnlineAdapterStatus::HashRejected
            && hash_reject.hash_status
                == RollbackStateHashStatus::EnforcedMismatch;

        RollbackOnlineSessionModel<128> warn_session {};
        warn_session.reset(60, RollbackHashPolicy::WarnOnly);
        (void)warn_session.predict_remote_input(0);
        RollbackTransportPacket warn_packet = remote0;
        warn_packet.local_input = 0x44;
        warn_packet.state_hash = expected_hash ^ 1ull;
        const RollbackOnlineReceiveReport warn_report =
            warn_session.receive_remote_packet(
                warn_packet, 2, expected_hash, true, true, false);
        report.hash_warn_allows_correction =
            warn_report.ok && warn_report.accepted
            && warn_report.state_hash_warning
            && warn_report.requires_correction
            && warn_report.status
                == RollbackOnlineAdapterStatus::AcceptedCorrectionRequired;

        report.ok =
            report.local_packet_ack
            && report.prediction_created
            && report.no_correction_for_matching_prediction
            && report.correction_for_delayed_mismatch
            && report.reorder_correction
            && report.duplicate_rejected
            && report.conflict_rejected
            && report.over_window_rejected
            && report.reorder_preserves_prediction_seed
            && report.future_input_not_used_for_earlier_prediction
            && report.cache_write_rejected
            && report.stock_drain_required
            && report.drain_bypass_ok
            && report.cache_provenance_ok
            && report.hash_enforced_rejected
            && report.hash_warn_allows_correction;
        if (!report.ok)
        {
            report.failure = report.cache_provenance_ok
                ? "online-session-selftest-failed"
                : cache_report.failure;
        }
        return report;
    }
}
