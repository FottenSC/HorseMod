#pragma once

namespace Horse
{
    struct RollbackObserverCaptureTransactionReport
    {
        bool ok {false};
        bool observer_ok {false};
        bool cleanup_ok {false};
    };

    // Snapshot verification invokes native readers that are not guaranteed to
    // be observational. Cleanup must therefore run after both successful and
    // failed observer passes so the live explicit state cannot escape mutated.
    template <typename ObserverFn, typename CleanupFn>
    static inline RollbackObserverCaptureTransactionReport
    RunRollbackObserverCaptureTransaction(
        ObserverFn&& observer,
        CleanupFn&& cleanup) noexcept
    {
        RollbackObserverCaptureTransactionReport report {};
        report.observer_ok = observer();
        report.cleanup_ok = cleanup();
        report.ok = report.observer_ok && report.cleanup_ok;
        return report;
    }
}
