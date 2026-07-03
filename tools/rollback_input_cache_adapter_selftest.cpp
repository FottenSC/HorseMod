#include "../HorseMod/horselib/RollbackInputCacheAdapter.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackInputCacheAdapterSelfTestReport report =
        Horse::RunRollbackInputCacheAdapterSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback input-cache adapter self-test failed failure=%s "
            "layout=%d pred_after_drain=%d net_reject=%d "
            "not_game_thread=%d drain_required=%d stock_after_pred=%d "
            "confirmed_replace=%d dup_confirmed=%d pred_over_confirmed=%d "
            "conflict=%d consume_source=%d "
            "ring_mismatch=%d\n",
            report.failure ? report.failure : "?",
            report.entry_layout_ok ? 1 : 0,
            report.prediction_after_drain ? 1 : 0,
            report.network_thread_rejected ? 1 : 0,
            report.not_game_thread_rejected ? 1 : 0,
            report.drain_order_required ? 1 : 0,
            report.stock_after_prediction_rejected ? 1 : 0,
            report.confirmed_replaces_prediction ? 1 : 0,
            report.duplicate_confirmed_idempotent ? 1 : 0,
            report.prediction_over_confirmed_rejected ? 1 : 0,
            report.conflict_rejected ? 1 : 0,
            report.consume_source_exact ? 1 : 0,
            report.ring_identity_mismatch_rejected ? 1 : 0);
        return 1;
    }

    std::printf(
        "rollback input-cache adapter self-test passed "
        "layout=%d pred_after_drain=%d net_reject=%d "
        "not_game_thread=%d drain_required=%d stock_after_pred=%d "
        "confirmed_replace=%d dup_confirmed=%d pred_over_confirmed=%d "
        "conflict=%d consume_source=%d "
        "ring_mismatch=%d\n",
        report.entry_layout_ok ? 1 : 0,
        report.prediction_after_drain ? 1 : 0,
        report.network_thread_rejected ? 1 : 0,
        report.not_game_thread_rejected ? 1 : 0,
        report.drain_order_required ? 1 : 0,
        report.stock_after_prediction_rejected ? 1 : 0,
        report.confirmed_replaces_prediction ? 1 : 0,
        report.duplicate_confirmed_idempotent ? 1 : 0,
        report.prediction_over_confirmed_rejected ? 1 : 0,
        report.conflict_rejected ? 1 : 0,
        report.consume_source_exact ? 1 : 0,
        report.ring_identity_mismatch_rejected ? 1 : 0);
    return 0;
}
