#include "../HorseMod/horselib/RollbackOnlineSession.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackOnlineSessionSelfTestReport report =
        Horse::RunRollbackOnlineSessionSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback online-session self-test failed failure=%s "
            "ack=%d predict=%d no_correction=%d correction=%d "
            "reorder=%d duplicate=%d conflict=%d late=%d "
            "reorder_seed=%d no_future_seed=%d "
            "cache_write=%d stock_drain=%d bypass=%d cache_provenance=%d "
            "hash_enforced=%d hash_warn=%d\n",
            report.failure ? report.failure : "?",
            report.local_packet_ack ? 1 : 0,
            report.prediction_created ? 1 : 0,
            report.no_correction_for_matching_prediction ? 1 : 0,
            report.correction_for_delayed_mismatch ? 1 : 0,
            report.reorder_correction ? 1 : 0,
            report.duplicate_rejected ? 1 : 0,
            report.conflict_rejected ? 1 : 0,
            report.over_window_rejected ? 1 : 0,
            report.reorder_preserves_prediction_seed ? 1 : 0,
            report.future_input_not_used_for_earlier_prediction ? 1 : 0,
            report.cache_write_rejected ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.drain_bypass_ok ? 1 : 0,
            report.cache_provenance_ok ? 1 : 0,
            report.hash_enforced_rejected ? 1 : 0,
            report.hash_warn_allows_correction ? 1 : 0);
        return 1;
    }

    std::printf(
        "rollback online-session self-test passed "
        "ack=%d predict=%d no_correction=%d correction=%d "
        "reorder=%d duplicate=%d conflict=%d late=%d "
        "reorder_seed=%d no_future_seed=%d "
        "cache_write=%d stock_drain=%d bypass=%d cache_provenance=%d "
        "hash_enforced=%d hash_warn=%d\n",
        report.local_packet_ack ? 1 : 0,
        report.prediction_created ? 1 : 0,
        report.no_correction_for_matching_prediction ? 1 : 0,
        report.correction_for_delayed_mismatch ? 1 : 0,
        report.reorder_correction ? 1 : 0,
        report.duplicate_rejected ? 1 : 0,
        report.conflict_rejected ? 1 : 0,
        report.over_window_rejected ? 1 : 0,
        report.reorder_preserves_prediction_seed ? 1 : 0,
        report.future_input_not_used_for_earlier_prediction ? 1 : 0,
        report.cache_write_rejected ? 1 : 0,
        report.stock_drain_required ? 1 : 0,
        report.drain_bypass_ok ? 1 : 0,
        report.cache_provenance_ok ? 1 : 0,
        report.hash_enforced_rejected ? 1 : 0,
        report.hash_warn_allows_correction ? 1 : 0);
    return 0;
}
