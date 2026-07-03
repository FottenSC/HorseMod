// ============================================================================
// Horse::RollbackLiveActivationGate
//
// Final guard before any future live HRG1/Gekko packet path is allowed to leave
// observe-only mode. This is a pure model: it never sends packets and never
// writes the stock InputLog cache.
// ============================================================================

#pragma once

#include "RollbackLiveOnlineCapture.hpp"
#include "RollbackStockTransportSurface.hpp"

#include <cstdint>

namespace Horse
{
    enum class RollbackLiveActivationStatus : uint8_t
    {
        Ready,
        OperatorNotArmed,
        CaptureNotReady,
        BoundaryViolation,
        LiveTrafficNotProven,
        SessionPointerNotBound,
        InputLogNotBound,
        NonHrg1Payload,
        MissingStrictIdentity,
        StockSurfaceRejected,
        HorseRouteProvenanceMissing,
        HorseRouteNotAllowed,
        PeerIdentityInvalid,
        SessionIdInvalid,
        RouteIdentityMismatch,
    };

    struct RollbackLiveActivationRequest
    {
        RollbackLiveOnlineCaptureReport capture {};
        RollbackStockTransportRoute route {};
        uint8_t source_peer {0};
        uint8_t destination_peer {0};
        uint64_t session_id {0};
        bool explicit_operator_enable {false};
    };

    struct RollbackLiveActivationReport
    {
        bool ok {false};
        bool activation_ready {false};
        bool explicit_operator_enable {false};
        bool capture_ready {false};
        bool observe_only {false};
        bool stock_observe_ready {false};
        bool boundary_ready {false};
        bool live_capture_complete {false};
        bool no_boundary_violation {false};
        bool stock_send_observed {false};
        bool receive_observed {false};
        bool drain_consumer_observed {false};
        bool live_order_proven {false};
        bool session_pointer_bound {false};
        bool input_log_bound {false};
        bool hrg1_payload {false};
        bool route_provenance_valid {false};
        bool strict_identity {false};
        bool horse_route_allowed {false};
        bool stock_surface_rejected {false};
        bool peer_identity_bound {false};
        bool session_id_bound {false};
        bool route_identity_matches {false};
        RollbackStockTransportDecision surface_decision {
            RollbackStockTransportDecision::RejectUnknownStockPath};
        RollbackLiveActivationStatus status {
            RollbackLiveActivationStatus::CaptureNotReady};
        const char* failure {"not-run"};
    };

    struct RollbackLiveActivationSelfTestReport
    {
        bool ok {false};
        bool activation_ready {false};
        bool readiness_only_rejected {false};
        bool stock_surface_rejected {false};
        bool route_provenance_rejected {false};
        bool missing_identity_rejected {false};
        bool direct_readiness_rejected {false};
        bool route_identity_rejected {false};
        bool boundary_violation_rejected {false};
        bool missing_session_rejected {false};
        bool missing_input_log_rejected {false};
        bool self_peer_rejected {false};
        bool zero_session_rejected {false};
        bool operator_not_armed_rejected {false};
        bool missing_receive_rejected {false};
        bool non_hrg1_rejected {false};
        const char* failure {"not-run"};
    };

    static inline bool RollbackLiveActivationStockSurfaceRejected(
        RollbackStockTransportDecision decision) noexcept
    {
        return decision
                == RollbackStockTransportDecision::RejectUnknownStockPath
            || decision
                == RollbackStockTransportDecision::RejectStockInputHrg1
            || decision
                == RollbackStockTransportDecision::RejectBattleSyncHrg1;
    }

    static inline RollbackLiveActivationReport
    EvaluateRollbackLiveActivation(
        const RollbackLiveActivationRequest& req) noexcept
    {
        RollbackLiveActivationReport out {};
        out.explicit_operator_enable = req.explicit_operator_enable;
        out.observe_only = req.capture.observe_only;
        out.stock_observe_ready =
            req.capture.stock_observe_ok
            && req.capture.stock_hooks_installed
            && req.capture.stock_trace_active;
        out.boundary_ready =
            req.capture.boundary_hooks_installed
            && req.capture.boundary_trace_active;
        out.capture_ready =
            req.capture.capture_ready
            && out.observe_only
            && out.stock_observe_ready
            && out.boundary_ready;
        out.live_capture_complete = req.capture.live_capture_complete;
        out.no_boundary_violation = !req.capture.boundary_violation;
        out.stock_send_observed =
            req.capture.input_send_count > 0
            && req.capture.battle_sync_request_stage_count > 0;
        out.receive_observed = req.capture.receive_enqueue_count > 0;
        out.drain_consumer_observed =
            req.capture.drain_enter_count > 0
            && req.capture.drain_exit_count > 0
            && req.capture.consumer_count > 0;
        out.live_order_proven = req.capture.live_order_proven;
        out.session_pointer_bound = req.capture.last_session_ptr != 0;
        out.input_log_bound =
            req.capture.last_input_log != 0
            && req.capture.last_receive_input_log != 0;
        out.hrg1_payload = req.route.payload_is_hrg1;
        out.route_provenance_valid =
            RollbackStockTransportHasHorseAdapterProvenance(req.route);
        out.strict_identity =
            RollbackStockTransportHasStrictIdentity(req.route);
        out.surface_decision =
            DecideRollbackStockTransportSurface(req.route);
        out.horse_route_allowed =
            out.surface_decision
            == RollbackStockTransportDecision::AllowHorseOwnedHrg1;
        out.stock_surface_rejected =
            RollbackLiveActivationStockSurfaceRejected(out.surface_decision);
        out.peer_identity_bound =
            req.source_peer != 0
            && req.destination_peer != 0
            && req.source_peer != req.destination_peer;
        out.session_id_bound = req.session_id != 0;
        out.route_identity_matches =
            out.strict_identity
            && out.peer_identity_bound
            && out.session_id_bound
            && req.route.source_peer == req.source_peer
            && req.route.destination_peer == req.destination_peer
            && req.route.session_id == req.session_id;

        if (!out.explicit_operator_enable)
        {
            out.status = RollbackLiveActivationStatus::OperatorNotArmed;
            out.failure = "operator-not-armed";
        }
        else if (!out.capture_ready)
        {
            out.status = RollbackLiveActivationStatus::CaptureNotReady;
            out.failure = "capture-not-ready";
        }
        else if (!out.no_boundary_violation)
        {
            out.status = RollbackLiveActivationStatus::BoundaryViolation;
            out.failure = "boundary-violation";
        }
        else if (!out.live_capture_complete
                 || !out.stock_send_observed
                 || !out.receive_observed
                 || !out.drain_consumer_observed
                 || !out.live_order_proven)
        {
            out.status = RollbackLiveActivationStatus::LiveTrafficNotProven;
            out.failure = "live-traffic-not-proven";
        }
        else if (!out.session_pointer_bound)
        {
            out.status = RollbackLiveActivationStatus::SessionPointerNotBound;
            out.failure = "session-pointer-not-bound";
        }
        else if (!out.input_log_bound)
        {
            out.status = RollbackLiveActivationStatus::InputLogNotBound;
            out.failure = "input-log-not-bound";
        }
        else if (!out.hrg1_payload)
        {
            out.status = RollbackLiveActivationStatus::NonHrg1Payload;
            out.failure = "non-hrg1-payload";
        }
        else if (out.stock_surface_rejected)
        {
            out.status = RollbackLiveActivationStatus::StockSurfaceRejected;
            out.failure = "stock-surface-rejected";
        }
        else if (out.surface_decision
                 == RollbackStockTransportDecision::
                     RejectMissingHorseAdapterProvenance)
        {
            out.status =
                RollbackLiveActivationStatus::HorseRouteProvenanceMissing;
            out.failure = "horse-route-provenance-missing";
        }
        else if (!out.strict_identity
                 || out.surface_decision
                    == RollbackStockTransportDecision::
                        RejectMissingStrictIdentity)
        {
            out.status = RollbackLiveActivationStatus::MissingStrictIdentity;
            out.failure = "missing-strict-identity";
        }
        else if (!out.horse_route_allowed)
        {
            out.status = RollbackLiveActivationStatus::HorseRouteNotAllowed;
            out.failure = "horse-route-not-allowed";
        }
        else if (!out.peer_identity_bound)
        {
            out.status = RollbackLiveActivationStatus::PeerIdentityInvalid;
            out.failure = "peer-identity-invalid";
        }
        else if (!out.session_id_bound)
        {
            out.status = RollbackLiveActivationStatus::SessionIdInvalid;
            out.failure = "session-id-invalid";
        }
        else if (!out.route_identity_matches)
        {
            out.status = RollbackLiveActivationStatus::RouteIdentityMismatch;
            out.failure = "route-identity-mismatch";
        }
        else
        {
            out.status = RollbackLiveActivationStatus::Ready;
            out.activation_ready = true;
            out.ok = true;
            out.failure = "ok";
        }
        return out;
    }

    static inline RollbackLiveOnlineCaptureReport
    RollbackLiveActivationMakeCapture(
        bool live_complete,
        bool boundary_violation,
        bool include_receive) noexcept
    {
        RollbackStockTransportObserveTracker stock {};
        stock.reset();
        stock.mark_hooks(true, true, true, true, true);
        stock.mark_trace_active(true);
        if (live_complete)
        {
            stock.record_acquire(10, 0x1000u, 0x2000u, 0x3000u, 0x4000u);
            stock.record_opcode0(11, 0x5000u, 0x7Fu, 120);
            stock.record_battle_sync_request_stage(12);
            if (include_receive)
                stock.record_receive_enqueue(13, 0x5000u, 0x2u, 0x6000u);
        }

        RollbackLiveBoundaryTracker boundary {};
        boundary.reset();
        boundary.mark_hooks_installed(true);
        boundary.mark_trace_active(true);
        if (live_complete)
        {
            boundary.on_drain_enter(13, 0x5000u);
            if (boundary_violation)
            {
                boundary.on_cache_consumer(
                    14, 0x7000u, 0x5000u, 1, 0, 121, 120);
            }
            else
            {
                boundary.on_drain_exit(13, 0x5000u);
                boundary.on_cache_consumer(
                    14, 0x7000u, 0x5000u, 1, 0, 121, 120);
            }
        }
        return EvaluateRollbackLiveOnlineCapture(stock.report(),
                                                 boundary.report());
    }

    static inline RollbackStockTransportRoute
    RollbackLiveActivationHorseRoute(
        bool strict_identity = true,
        uint8_t source_peer = 0xA0u,
        uint8_t destination_peer = 0xB0u,
        uint64_t session_id = 0x4C495645414354ull) noexcept
    {
        return RollbackStockTransportRoute {
            0u,
            0u,
            true,
            true,
            strict_identity,
            strict_identity,
            strict_identity,
            strict_identity ? source_peer : uint8_t {0},
            strict_identity ? destination_peer : uint8_t {0},
            strict_identity ? session_id : uint64_t {0},
            kRollbackHorseAdapterRouteCookie};
    }

    static inline RollbackLiveActivationSelfTestReport
    RunRollbackLiveActivationSelfTest() noexcept
    {
        RollbackLiveActivationSelfTestReport report {};

        const RollbackLiveOnlineCaptureReport live =
            RollbackLiveActivationMakeCapture(true, false, true);
        const RollbackStockTransportRoute horse =
            RollbackLiveActivationHorseRoute(true);
        const RollbackLiveActivationRequest good {
            live, horse, 0xA0u, 0xB0u, 0x4C495645414354ull, true};
        const RollbackLiveActivationReport ready =
            EvaluateRollbackLiveActivation(good);
        report.activation_ready = ready.ok && ready.activation_ready;

        const RollbackLiveActivationRequest readiness_only {
            RollbackLiveActivationMakeCapture(false, false, true),
            horse,
            0xA0u,
            0xB0u,
            0x4C495645414354ull,
            true};
        const RollbackLiveActivationReport readiness =
            EvaluateRollbackLiveActivation(readiness_only);
        report.readiness_only_rejected =
            !readiness.ok
            && readiness.status
                == RollbackLiveActivationStatus::LiveTrafficNotProven;

        RollbackLiveActivationRequest direct_not_ready = good;
        direct_not_ready.capture.stock_observe_ok = false;
        const RollbackLiveActivationReport direct_ready =
            EvaluateRollbackLiveActivation(direct_not_ready);
        report.direct_readiness_rejected =
            !direct_ready.ok
            && direct_ready.status
                == RollbackLiveActivationStatus::CaptureNotReady;

        const RollbackStockTransportRoute stock_input {
            kLuxOnlineTransportSendInputSlot,
            kLuxOnlineChannelInputBinary,
            true,
            true,
            true,
            true,
            true};
        RollbackLiveActivationRequest stock_req = good;
        stock_req.route = stock_input;
        const RollbackLiveActivationReport stock =
            EvaluateRollbackLiveActivation(stock_req);
        report.stock_surface_rejected =
            !stock.ok
            && stock.status
                == RollbackLiveActivationStatus::StockSurfaceRejected
            && stock.stock_surface_rejected;

        RollbackLiveActivationRequest missing_provenance = good;
        missing_provenance.route.horse_adapter_cookie = 0;
        const RollbackLiveActivationReport provenance =
            EvaluateRollbackLiveActivation(missing_provenance);
        report.route_provenance_rejected =
            !provenance.ok
            && provenance.status
                == RollbackLiveActivationStatus::
                    HorseRouteProvenanceMissing;

        RollbackLiveActivationRequest missing_identity = good;
        missing_identity.route = RollbackLiveActivationHorseRoute(false);
        const RollbackLiveActivationReport missing_id =
            EvaluateRollbackLiveActivation(missing_identity);
        report.missing_identity_rejected =
            !missing_id.ok
            && missing_id.status
                == RollbackLiveActivationStatus::MissingStrictIdentity;

        RollbackLiveActivationRequest route_identity = good;
        route_identity.route.source_peer = 0xA1u;
        const RollbackLiveActivationReport route_id =
            EvaluateRollbackLiveActivation(route_identity);
        report.route_identity_rejected =
            !route_id.ok
            && route_id.status
                == RollbackLiveActivationStatus::RouteIdentityMismatch;

        RollbackLiveActivationRequest bad_boundary = good;
        bad_boundary.capture =
            RollbackLiveActivationMakeCapture(true, true, true);
        const RollbackLiveActivationReport boundary =
            EvaluateRollbackLiveActivation(bad_boundary);
        report.boundary_violation_rejected =
            !boundary.ok
            && boundary.status
                == RollbackLiveActivationStatus::BoundaryViolation;

        RollbackLiveActivationRequest no_session = good;
        no_session.capture.last_session_ptr = 0;
        const RollbackLiveActivationReport no_session_report =
            EvaluateRollbackLiveActivation(no_session);
        report.missing_session_rejected =
            !no_session_report.ok
            && no_session_report.status
                == RollbackLiveActivationStatus::SessionPointerNotBound;

        RollbackLiveActivationRequest no_input_log = good;
        no_input_log.capture.last_receive_input_log = 0;
        const RollbackLiveActivationReport no_input_log_report =
            EvaluateRollbackLiveActivation(no_input_log);
        report.missing_input_log_rejected =
            !no_input_log_report.ok
            && no_input_log_report.status
                == RollbackLiveActivationStatus::InputLogNotBound;

        RollbackLiveActivationRequest self_peer = good;
        self_peer.destination_peer = self_peer.source_peer;
        const RollbackLiveActivationReport self_peer_report =
            EvaluateRollbackLiveActivation(self_peer);
        report.self_peer_rejected =
            !self_peer_report.ok
            && self_peer_report.status
                == RollbackLiveActivationStatus::PeerIdentityInvalid;

        RollbackLiveActivationRequest zero_session = good;
        zero_session.session_id = 0;
        const RollbackLiveActivationReport zero_session_report =
            EvaluateRollbackLiveActivation(zero_session);
        report.zero_session_rejected =
            !zero_session_report.ok
            && zero_session_report.status
                == RollbackLiveActivationStatus::SessionIdInvalid;

        RollbackLiveActivationRequest operator_not_armed = good;
        operator_not_armed.explicit_operator_enable = false;
        const RollbackLiveActivationReport operator_report =
            EvaluateRollbackLiveActivation(operator_not_armed);
        report.operator_not_armed_rejected =
            !operator_report.ok
            && operator_report.status
                == RollbackLiveActivationStatus::OperatorNotArmed;

        RollbackLiveActivationRequest missing_receive = good;
        missing_receive.capture =
            RollbackLiveActivationMakeCapture(true, false, false);
        const RollbackLiveActivationReport missing_receive_report =
            EvaluateRollbackLiveActivation(missing_receive);
        report.missing_receive_rejected =
            !missing_receive_report.ok
            && missing_receive_report.status
                == RollbackLiveActivationStatus::LiveTrafficNotProven;

        RollbackLiveActivationRequest non_hrg1 = good;
        non_hrg1.route.payload_is_hrg1 = false;
        const RollbackLiveActivationReport non_hrg1_report =
            EvaluateRollbackLiveActivation(non_hrg1);
        report.non_hrg1_rejected =
            !non_hrg1_report.ok
            && non_hrg1_report.status
                == RollbackLiveActivationStatus::NonHrg1Payload;

        report.ok =
            report.activation_ready
            && report.readiness_only_rejected
            && report.stock_surface_rejected
            && report.route_provenance_rejected
            && report.missing_identity_rejected
            && report.direct_readiness_rejected
            && report.route_identity_rejected
            && report.boundary_violation_rejected
            && report.missing_session_rejected
            && report.missing_input_log_rejected
            && report.self_peer_rejected
            && report.zero_session_rejected
            && report.operator_not_armed_rejected
            && report.missing_receive_rejected
            && report.non_hrg1_rejected;
        report.failure = report.ok
            ? "ok"
            : "live-activation-selftest-failed";
        return report;
    }
}
