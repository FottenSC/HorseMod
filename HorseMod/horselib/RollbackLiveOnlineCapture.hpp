// ============================================================================
// Horse::RollbackLiveOnlineCapture
//
// Combined live-online capture readiness model. It joins the observe-only stock
// transport hook report with the stock-drain/cache-consumer boundary report so
// a future real online match must prove both packet traffic and safe drain
// ordering before any rollback injection work proceeds.
// ============================================================================

#pragma once

#include "RollbackLiveBoundary.hpp"
#include "RollbackStockTransportObserveModel.hpp"

#include <cstdint>

namespace Horse
{
    struct RollbackLiveOnlineCaptureReport
    {
        bool ok {false};
        bool observe_only {true};
        bool capture_ready {false};
        bool live_capture_complete {false};
        bool stock_observe_ok {false};
        bool stock_hooks_installed {false};
        bool stock_trace_active {false};
        bool boundary_hooks_installed {false};
        bool boundary_trace_active {false};
        bool acquire_observed {false};
        bool nonnull_session_observed {false};
        bool stock_input_observed {false};
        bool battle_sync_observed {false};
        bool receive_enqueue_observed {false};
        bool drain_observed {false};
        bool consumer_observed {false};
        bool live_order_proven {false};
        bool boundary_violation {false};
        uint64_t acquire_count {0};
        uint64_t acquire_nonnull_session_count {0};
        uint64_t input_send_count {0};
        uint64_t battle_sync_request_stage_count {0};
        uint64_t receive_enqueue_count {0};
        uint64_t drain_enter_count {0};
        uint64_t drain_exit_count {0};
        uint64_t consumer_count {0};
        uint64_t total_observed_calls {0};
        uintptr_t last_session_ptr {0};
        uintptr_t last_input_log {0};
        uintptr_t last_receive_input_log {0};
        uintptr_t last_receive_packet_wrapper {0};
        uintptr_t last_battle_manager {0};
        const char* failure {"not-run"};
    };

    static inline RollbackLiveOnlineCaptureReport
    EvaluateRollbackLiveOnlineCapture(
        const RollbackStockTransportObserveReport& stock,
        const RollbackLiveBoundaryReport& boundary) noexcept
    {
        RollbackLiveOnlineCaptureReport out {};
        out.observe_only = stock.observe_only;
        out.stock_observe_ok = stock.ok;
        out.stock_hooks_installed = stock.hooks_installed;
        out.stock_trace_active = stock.trace_active;
        out.boundary_hooks_installed = boundary.hooks_installed;
        out.boundary_trace_active = boundary.trace_active;
        out.acquire_observed = stock.acquire_observed;
        out.nonnull_session_observed = stock.nonnull_session_observed;
        out.stock_input_observed = stock.stock_input_observed;
        out.battle_sync_observed = stock.battle_sync_observed;
        out.receive_enqueue_observed = stock.receive_enqueue_observed;
        out.drain_observed =
            boundary.drain_enter_count > 0 || boundary.drain_exit_count > 0;
        out.consumer_observed = boundary.consumer_count > 0;
        out.live_order_proven = boundary.live_order_proven;
        out.boundary_violation =
            boundary.consumer_during_drain || boundary.unbalanced_drain;
        out.acquire_count = stock.acquire_count;
        out.acquire_nonnull_session_count =
            stock.acquire_nonnull_session_count;
        out.input_send_count = stock.opcode0_count + stock.opcode1_count;
        out.battle_sync_request_stage_count =
            stock.battle_sync_request_stage_count;
        out.receive_enqueue_count = stock.receive_enqueue_count;
        out.drain_enter_count = boundary.drain_enter_count;
        out.drain_exit_count = boundary.drain_exit_count;
        out.consumer_count = boundary.consumer_count;
        out.total_observed_calls =
            stock.total_observed_calls + boundary.sequence;
        out.last_session_ptr = stock.last_session_ptr;
        out.last_input_log =
            stock.last_input_log ? stock.last_input_log
                                 : boundary.last_input_log;
        out.last_receive_input_log = stock.last_receive_input_log;
        out.last_receive_packet_wrapper =
            stock.last_receive_packet_wrapper;
        out.last_battle_manager = boundary.last_battle_manager;

        out.capture_ready =
            out.observe_only
            && out.stock_hooks_installed
            && out.stock_trace_active
            && out.boundary_hooks_installed
            && out.boundary_trace_active;
        out.live_capture_complete =
            out.capture_ready
            && out.acquire_nonnull_session_count > 0
            && out.input_send_count > 0
            && out.battle_sync_request_stage_count > 0
            && out.receive_enqueue_count > 0
            && out.consumer_count > 0
            && out.live_order_proven
            && !out.boundary_violation;
        out.ok = out.capture_ready && !out.boundary_violation;

        if (!out.observe_only)
            out.failure = "not-observe-only";
        else if (!out.stock_hooks_installed)
            out.failure = "stock-hooks-not-installed";
        else if (!out.stock_trace_active)
            out.failure = "stock-trace-not-active";
        else if (!out.boundary_hooks_installed)
            out.failure = "boundary-hooks-not-installed";
        else if (!out.boundary_trace_active)
            out.failure = "boundary-trace-not-active";
        else if (out.boundary_violation)
            out.failure = "boundary-order-violation";
        else if (!out.live_capture_complete)
            out.failure = "waiting-for-live-online-traffic";
        else
            out.failure = "ok";
        return out;
    }

    static inline RollbackLiveOnlineCaptureReport
    RunRollbackLiveOnlineCaptureSelfTest() noexcept
    {
        RollbackStockTransportObserveTracker stock_ready {};
        stock_ready.reset();
        stock_ready.mark_hooks(true, true, true, true, true);
        stock_ready.mark_trace_active(true);

        RollbackLiveBoundaryTracker boundary_ready {};
        boundary_ready.reset();
        boundary_ready.mark_hooks_installed(true);
        boundary_ready.mark_trace_active(true);

        const RollbackLiveOnlineCaptureReport ready =
            EvaluateRollbackLiveOnlineCapture(
                stock_ready.report(), boundary_ready.report());

        RollbackStockTransportObserveTracker stock_live {};
        stock_live.reset();
        stock_live.mark_hooks(true, true, true, true, true);
        stock_live.mark_trace_active(true);
        stock_live.record_acquire(10, 0x1000u, 0x2000u, 0x3000u, 0x4000u);
        stock_live.record_opcode0(11, 0x5000u, 0x7Fu, 120);
        stock_live.record_battle_sync_request_stage(12);
        stock_live.record_receive_enqueue(13, 0x5000u, 0x2u, 0x6000u);

        RollbackLiveBoundaryTracker boundary_live {};
        boundary_live.reset();
        boundary_live.mark_hooks_installed(true);
        boundary_live.mark_trace_active(true);
        boundary_live.on_drain_enter(13, 0x5000u);
        boundary_live.on_drain_exit(13, 0x5000u);
        boundary_live.on_cache_consumer(
            14, 0x7000u, 0x5000u, 1, 0, 121, 120);

        const RollbackLiveOnlineCaptureReport live =
            EvaluateRollbackLiveOnlineCapture(
                stock_live.report(), boundary_live.report());

        RollbackLiveBoundaryTracker boundary_bad {};
        boundary_bad.reset();
        boundary_bad.mark_hooks_installed(true);
        boundary_bad.mark_trace_active(true);
        boundary_bad.on_drain_enter(13, 0x5000u);
        boundary_bad.on_cache_consumer(
            14, 0x7000u, 0x5000u, 1, 0, 122, 121);
        const RollbackLiveOnlineCaptureReport bad =
            EvaluateRollbackLiveOnlineCapture(
                stock_live.report(), boundary_bad.report());

        RollbackStockTransportObserveTracker stock_missing_receive {};
        stock_missing_receive.reset();
        stock_missing_receive.mark_hooks(true, true, true, true, true);
        stock_missing_receive.mark_trace_active(true);
        stock_missing_receive.record_acquire(
            10, 0x1000u, 0x2000u, 0x3000u, 0x4000u);
        stock_missing_receive.record_opcode0(11, 0x5000u, 0x7Fu, 120);
        stock_missing_receive.record_battle_sync_request_stage(12);
        const RollbackLiveOnlineCaptureReport missing_receive =
            EvaluateRollbackLiveOnlineCapture(
                stock_missing_receive.report(), boundary_live.report());

        RollbackLiveOnlineCaptureReport out = live;
        out.capture_ready = ready.capture_ready;
        out.live_capture_complete = live.live_capture_complete;
        out.boundary_violation = bad.boundary_violation;
        out.receive_enqueue_observed =
            live.receive_enqueue_observed
            && !missing_receive.live_capture_complete;
        out.ok =
            ready.ok
            && ready.capture_ready
            && !ready.live_capture_complete
            && live.ok
            && live.live_capture_complete
            && !bad.ok
            && bad.boundary_violation
            && missing_receive.ok
            && !missing_receive.live_capture_complete;
        out.failure = out.ok ? "ok" : "live-online-capture-selftest-failed";
        return out;
    }
}
