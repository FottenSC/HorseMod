// ============================================================================
// Horse::RollbackLiveBoundary
//
// Pure ordering model for the rollback live input-boundary proof. Runtime
// detours feed this tracker; standalone self-tests can use it without PolyHook
// or UE4SS dependencies.
// ============================================================================

#pragma once

#include <cstdint>

namespace Horse
{
    struct RollbackLiveBoundaryReport
    {
        bool ok {false};
        bool hooks_installed {false};
        bool trace_active {false};
        bool stock_drain_inert {false};
        bool live_order_proven {false};
        bool offline_boundary_observed {false};
        bool consumer_after_completed_drain {false};
        bool consumer_during_drain {false};
        bool unbalanced_drain {false};
        uint64_t sequence {0};
        uint64_t drain_enter_count {0};
        uint64_t drain_exit_count {0};
        uint64_t consumer_count {0};
        uint64_t first_drain_enter_sequence {0};
        uint64_t last_drain_exit_sequence {0};
        uint64_t first_consumer_sequence {0};
        uint64_t first_consumer_after_drain_sequence {0};
        uint32_t last_drain_thread_id {0};
        uint32_t last_consumer_thread_id {0};
        uintptr_t last_input_log {0};
        uintptr_t last_battle_manager {0};
        uint32_t last_player_index {0};
        int32_t last_cache_frame {0};
        uint32_t last_master_clock {0};
        int32_t last_frames_back {0};
        const char* failure {"not-run"};
    };

    class RollbackLiveBoundaryTracker
    {
    public:
        void reset() noexcept
        {
            m_report = {};
            m_report.failure = "waiting";
            m_drain_depth = 0;
        }

        void mark_hooks_installed(bool installed) noexcept
        {
            m_report.hooks_installed = installed;
        }

        void mark_trace_active(bool active) noexcept
        {
            m_report.trace_active = active;
        }

        void on_drain_enter(
            uint32_t dwThreadId,
            uintptr_t pInputLog) noexcept
        {
            const uint64_t seq = ++m_report.sequence;
            ++m_report.drain_enter_count;
            ++m_drain_depth;
            if (m_report.first_drain_enter_sequence == 0)
                m_report.first_drain_enter_sequence = seq;
            m_report.last_drain_thread_id = dwThreadId;
            m_report.last_input_log = pInputLog;
        }

        void on_drain_exit(
            uint32_t dwThreadId,
            uintptr_t pInputLog) noexcept
        {
            const uint64_t seq = ++m_report.sequence;
            ++m_report.drain_exit_count;
            if (m_drain_depth > 0)
                --m_drain_depth;
            else
                m_report.unbalanced_drain = true;
            m_report.last_drain_exit_sequence = seq;
            m_report.last_drain_thread_id = dwThreadId;
            m_report.last_input_log = pInputLog;
        }

        void on_cache_consumer(
            uint32_t dwThreadId,
            uintptr_t pBattleManager,
            uintptr_t pInputLog,
            uint32_t dwPlayerIndex,
            int32_t nFramesBack,
            uint32_t dwMasterClock,
            int32_t nCacheFrame) noexcept
        {
            const uint64_t seq = ++m_report.sequence;
            ++m_report.consumer_count;
            if (m_report.first_consumer_sequence == 0)
                m_report.first_consumer_sequence = seq;
            if (m_drain_depth > 0)
                m_report.consumer_during_drain = true;
            if (m_report.last_drain_exit_sequence != 0
                && seq > m_report.last_drain_exit_sequence)
            {
                m_report.consumer_after_completed_drain = true;
                if (m_report.first_consumer_after_drain_sequence == 0)
                    m_report.first_consumer_after_drain_sequence = seq;
            }
            m_report.last_consumer_thread_id = dwThreadId;
            m_report.last_battle_manager = pBattleManager;
            m_report.last_input_log = pInputLog ? pInputLog
                                                : m_report.last_input_log;
            m_report.last_player_index = dwPlayerIndex;
            m_report.last_frames_back = nFramesBack;
            m_report.last_master_clock = dwMasterClock;
            m_report.last_cache_frame = nCacheFrame;
        }

        RollbackLiveBoundaryReport report() const noexcept
        {
            RollbackLiveBoundaryReport out = m_report;
            out.stock_drain_inert =
                out.drain_enter_count == 0 && out.drain_exit_count == 0;
            out.unbalanced_drain =
                out.unbalanced_drain
                || out.drain_enter_count != out.drain_exit_count;
            out.live_order_proven =
                out.consumer_after_completed_drain
                && !out.consumer_during_drain
                && !out.unbalanced_drain;
            out.offline_boundary_observed =
                out.stock_drain_inert && out.consumer_count > 0;
            out.ok =
                out.hooks_installed
                && out.consumer_count > 0
                && !out.consumer_during_drain
                && !out.unbalanced_drain
                && (out.live_order_proven || out.offline_boundary_observed);
            if (!out.hooks_installed)
                out.failure = "hooks-not-installed";
            else if (out.consumer_count == 0)
                out.failure = "cache-consumer-not-observed";
            else if (out.consumer_during_drain)
                out.failure = "consumer-during-stock-drain";
            else if (out.unbalanced_drain)
                out.failure = "unbalanced-stock-drain";
            else if (!out.live_order_proven && !out.offline_boundary_observed)
                out.failure = "stock-drain-order-not-proven";
            else
                out.failure = "ok";
            return out;
        }

    private:
        RollbackLiveBoundaryReport m_report {};
        uint32_t m_drain_depth {0};
    };

    static inline RollbackLiveBoundaryReport
    RunRollbackLiveBoundaryModelSelfTest() noexcept
    {
        RollbackLiveBoundaryTracker offline {};
        offline.reset();
        offline.mark_hooks_installed(true);
        offline.mark_trace_active(true);
        offline.on_cache_consumer(1, 0x1000, 0x2000, 1, 0, 120, 119);
        const RollbackLiveBoundaryReport offline_report = offline.report();

        RollbackLiveBoundaryTracker live {};
        live.reset();
        live.mark_hooks_installed(true);
        live.mark_trace_active(true);
        live.on_drain_enter(1, 0x2000);
        live.on_drain_exit(1, 0x2000);
        live.on_cache_consumer(1, 0x1000, 0x2000, 1, 0, 121, 120);
        const RollbackLiveBoundaryReport live_report = live.report();

        RollbackLiveBoundaryTracker bad_unbalanced {};
        bad_unbalanced.reset();
        bad_unbalanced.mark_hooks_installed(true);
        bad_unbalanced.mark_trace_active(true);
        bad_unbalanced.on_drain_enter(1, 0x2000);
        bad_unbalanced.on_cache_consumer(1, 0x1000, 0x2000, 1, 0, 122, 121);
        const RollbackLiveBoundaryReport bad_unbalanced_report =
            bad_unbalanced.report();

        RollbackLiveBoundaryTracker bad_order {};
        bad_order.reset();
        bad_order.mark_hooks_installed(true);
        bad_order.mark_trace_active(true);
        bad_order.on_cache_consumer(1, 0x1000, 0x2000, 1, 0, 123, 122);
        bad_order.on_drain_enter(1, 0x2000);
        bad_order.on_drain_exit(1, 0x2000);
        const RollbackLiveBoundaryReport bad_order_report =
            bad_order.report();

        RollbackLiveBoundaryReport out = live_report;
        out.offline_boundary_observed = offline_report.offline_boundary_observed;
        out.live_order_proven = live_report.live_order_proven;
        out.consumer_during_drain =
            bad_unbalanced_report.consumer_during_drain;
        out.unbalanced_drain = bad_unbalanced_report.unbalanced_drain;
        out.ok =
            offline_report.ok
            && live_report.ok
            && live_report.live_order_proven
            && offline_report.offline_boundary_observed
            && !bad_unbalanced_report.ok
            && bad_unbalanced_report.consumer_during_drain
            && bad_unbalanced_report.unbalanced_drain
            && !bad_order_report.ok;
        out.failure = out.ok ? "ok" : "live-boundary-selftest-failed";
        return out;
    }
}
