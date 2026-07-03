// ============================================================================
// Horse::RollbackStockTransportObserveModel
//
// Pure reporting/tracker model for observe-only native stock transport probes.
// Kept free of UE4SS/PolyHook dependencies so standalone tests can exercise it.
// ============================================================================

#pragma once

#include "RollbackStockTransportSurface.hpp"

#include <cstdint>
#include <cstring>

namespace Horse
{
    struct RollbackStockTransportObserveReport
    {
        bool ok {false};
        bool observe_only {true};
        bool hooks_installed {false};
        bool trace_active {false};
        bool acquire_hook_installed {false};
        bool opcode0_hook_installed {false};
        bool opcode1_hook_installed {false};
        bool battle_sync_hook_installed {false};
        bool receive_enqueue_hook_installed {false};
        bool acquire_observed {false};
        bool nonnull_session_observed {false};
        bool stock_input_observed {false};
        bool battle_sync_observed {false};
        bool receive_enqueue_observed {false};
        uint64_t acquire_count {0};
        uint64_t acquire_nonnull_session_count {0};
        uint64_t opcode0_count {0};
        uint64_t opcode1_count {0};
        uint64_t battle_sync_request_stage_count {0};
        uint64_t receive_enqueue_count {0};
        uint64_t total_observed_calls {0};
        uint32_t last_thread_id {0};
        uint32_t last_receive_thread_id {0};
        uintptr_t last_out_session_ptr {0};
        uintptr_t last_session_ptr {0};
        uintptr_t last_ref_controller_ptr {0};
        uintptr_t last_session_vtable {0};
        uintptr_t last_input_log {0};
        uintptr_t last_receive_input_log {0};
        uintptr_t last_receive_packet_wrapper {0};
        uint32_t last_channel {0};
        uint32_t last_msg_type {0};
        uint32_t last_receive_flag {0};
        uint32_t last_opcode0_input {0};
        int32_t last_opcode0_frame {0};
        uint32_t last_opcode1_slot_mask {0};
        int32_t last_opcode1_frame {0};
        int32_t last_opcode1_current_frame {0};
        int32_t last_opcode1_window_frames {0};
        uint32_t last_opcode1_resend_counter {0};
        const char* failure {"not-run"};
    };

    struct RollbackStockTransportObserveSelfTestReport
    {
        bool ok {false};
        bool hooks_gate {false};
        bool trace_gate {false};
        bool acquire_recorded {false};
        bool opcode0_recorded {false};
        bool opcode1_recorded {false};
        bool battle_sync_recorded {false};
        bool receive_enqueue_recorded {false};
        bool totals_ok {false};
        RollbackStockTransportObserveReport observed {};
        const char* failure {"not-run"};
    };

    class RollbackStockTransportObserveTracker
    {
    public:
        void reset() noexcept
        {
            m_report = {};
            m_report.observe_only = true;
            m_report.failure = "waiting";
        }

        void mark_hooks(
            bool acquire_hook,
            bool opcode0_hook,
            bool opcode1_hook,
            bool battle_sync_hook,
            bool receive_enqueue_hook) noexcept
        {
            m_report.acquire_hook_installed = acquire_hook;
            m_report.opcode0_hook_installed = opcode0_hook;
            m_report.opcode1_hook_installed = opcode1_hook;
            m_report.battle_sync_hook_installed = battle_sync_hook;
            m_report.receive_enqueue_hook_installed = receive_enqueue_hook;
        }

        void mark_trace_active(bool active) noexcept
        {
            m_report.trace_active = active;
            if (active && cstr_equal(m_report.failure, "waiting"))
                m_report.failure = "ok";
        }

        void record_acquire(
            uint32_t thread_id,
            uintptr_t out_session_ptr,
            uintptr_t session_ptr,
            uintptr_t ref_controller_ptr,
            uintptr_t session_vtable) noexcept
        {
            m_report.acquire_count += 1;
            m_report.acquire_observed = true;
            if (session_ptr != 0)
            {
                m_report.acquire_nonnull_session_count += 1;
                m_report.nonnull_session_observed = true;
            }
            m_report.last_thread_id = thread_id;
            m_report.last_out_session_ptr = out_session_ptr;
            m_report.last_session_ptr = session_ptr;
            m_report.last_ref_controller_ptr = ref_controller_ptr;
            m_report.last_session_vtable = session_vtable;
        }

        void record_opcode0(
            uint32_t thread_id,
            uintptr_t input_log,
            uint8_t input_byte,
            int32_t frame_id) noexcept
        {
            m_report.opcode0_count += 1;
            m_report.stock_input_observed = true;
            m_report.last_thread_id = thread_id;
            m_report.last_input_log = input_log;
            m_report.last_channel = kLuxOnlineChannelInputBinary;
            m_report.last_msg_type = 0;
            m_report.last_opcode0_input = input_byte;
            m_report.last_opcode0_frame = frame_id;
        }

        void record_opcode1(
            uint32_t thread_id,
            uintptr_t input_log,
            uint32_t slot_mask,
            int32_t frame_id,
            int32_t current_frame,
            int32_t window_frames,
            uint32_t resend_counter) noexcept
        {
            m_report.opcode1_count += 1;
            m_report.stock_input_observed = true;
            m_report.last_thread_id = thread_id;
            m_report.last_input_log = input_log;
            m_report.last_channel = kLuxOnlineChannelInputBinary;
            m_report.last_msg_type = 1;
            m_report.last_opcode1_slot_mask = slot_mask;
            m_report.last_opcode1_frame = frame_id;
            m_report.last_opcode1_current_frame = current_frame;
            m_report.last_opcode1_window_frames = window_frames;
            m_report.last_opcode1_resend_counter = resend_counter;
        }

        void record_battle_sync_request_stage(uint32_t thread_id) noexcept
        {
            m_report.battle_sync_request_stage_count += 1;
            m_report.battle_sync_observed = true;
            m_report.last_thread_id = thread_id;
            m_report.last_channel = kLuxOnlineChannelBattleSync;
            m_report.last_msg_type = 2;
        }

        void record_receive_enqueue(
            uint32_t thread_id,
            uintptr_t input_log,
            uint32_t unused_flag,
            uintptr_t packet_wrapper) noexcept
        {
            m_report.receive_enqueue_count += 1;
            m_report.receive_enqueue_observed = true;
            m_report.last_thread_id = thread_id;
            m_report.last_receive_thread_id = thread_id;
            m_report.last_receive_input_log = input_log;
            m_report.last_receive_packet_wrapper = packet_wrapper;
            m_report.last_receive_flag = unused_flag;
        }

        RollbackStockTransportObserveReport report() const noexcept
        {
            RollbackStockTransportObserveReport out = m_report;
            out.hooks_installed =
                out.acquire_hook_installed
                && out.opcode0_hook_installed
                && out.opcode1_hook_installed
                && out.battle_sync_hook_installed
                && out.receive_enqueue_hook_installed;
            out.total_observed_calls =
                out.acquire_count
                + out.opcode0_count
                + out.opcode1_count
                + out.battle_sync_request_stage_count
                + out.receive_enqueue_count;
            out.ok = out.observe_only && out.hooks_installed && out.trace_active;
            if (!out.hooks_installed)
                out.failure = "hooks-not-installed";
            else if (!out.trace_active)
                out.failure = "trace-not-active";
            else if (cstr_equal(out.failure, "waiting"))
                out.failure = "ok";
            return out;
        }

    private:
        static bool cstr_equal(const char* a, const char* b) noexcept
        {
            if (!a || !b) return a == b;
            return std::strcmp(a, b) == 0;
        }

        RollbackStockTransportObserveReport m_report {};
    };

    static inline RollbackStockTransportObserveSelfTestReport
    RunRollbackStockTransportObserveSelfTest() noexcept
    {
        RollbackStockTransportObserveSelfTestReport report {};
        RollbackStockTransportObserveTracker tracker {};
        tracker.reset();
        tracker.mark_hooks(true, true, true, true, true);
        tracker.mark_trace_active(true);
        RollbackStockTransportObserveReport initial = tracker.report();
        report.hooks_gate = initial.hooks_installed && initial.ok;
        report.trace_gate = initial.trace_active;

        tracker.record_acquire(11, 0x1000u, 0x2000u, 0x3000u, 0x4000u);
        tracker.record_opcode0(12, 0x5000u, 0x7Fu, 123);
        tracker.record_opcode1(13, 0x5000u, 0x3u, 124, 130, 8, 9);
        tracker.record_battle_sync_request_stage(14);
        tracker.record_receive_enqueue(15, 0x6000u, 0x2u, 0x7000u);
        report.observed = tracker.report();
        report.acquire_recorded =
            report.observed.acquire_observed
            && report.observed.nonnull_session_observed
            && report.observed.acquire_count == 1
            && report.observed.acquire_nonnull_session_count == 1
            && report.observed.last_session_vtable == 0x4000u;
        report.opcode0_recorded =
            report.observed.opcode0_count == 1
            && report.observed.stock_input_observed
            && report.observed.last_opcode0_input == 0x7Fu
            && report.observed.last_opcode0_frame == 123;
        report.opcode1_recorded =
            report.observed.opcode1_count == 1
            && report.observed.last_opcode1_slot_mask == 0x3u
            && report.observed.last_opcode1_resend_counter == 9u;
        report.battle_sync_recorded =
            report.observed.battle_sync_request_stage_count == 1
            && report.observed.battle_sync_observed
            && report.observed.last_channel == kLuxOnlineChannelBattleSync
            && report.observed.last_msg_type == 2u;
        report.receive_enqueue_recorded =
            report.observed.receive_enqueue_count == 1
            && report.observed.receive_enqueue_observed
            && report.observed.last_receive_thread_id == 15
            && report.observed.last_receive_input_log == 0x6000u
            && report.observed.last_receive_packet_wrapper == 0x7000u
            && report.observed.last_receive_flag == 0x2u;
        report.totals_ok = report.observed.total_observed_calls == 5;
        report.ok =
            report.hooks_gate
            && report.trace_gate
            && report.acquire_recorded
            && report.opcode0_recorded
            && report.opcode1_recorded
            && report.battle_sync_recorded
            && report.receive_enqueue_recorded
            && report.totals_ok;
        report.failure = report.ok
            ? "ok"
            : "stock-transport-observe-selftest-failed";
        return report;
    }
}
