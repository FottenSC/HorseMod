#include "../HorseMod/horselib/RollbackLivePeerPipeline.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackLivePeerPipelineSelfTestReport report =
        Horse::RunRollbackLivePeerPipelineSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback live-peer-pipeline self-test failed failure=%s "
            "enqueue=%d queued_only=%d stock_drain=%d metadata=%d "
            "payload_not_cache=%d predict_cache=%d confirm_replace=%d "
            "consume_confirmed=%d duplicate=%d pred_over_confirmed=%d "
            "wrong_identity=%d late_no_cache=%d net_cache_reject=%d "
            "bypass=%d enqueued=%u drained=%u rejected=%u cache_writes=%u\n",
            report.failure ? report.failure : "?",
            report.bridge_enqueue_ok ? 1 : 0,
            report.network_receive_queued_only ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.metadata_drains_to_session ? 1 : 0,
            report.bridge_payload_not_cache_input ? 1 : 0,
            report.prediction_cache_write_ok ? 1 : 0,
            report.confirmed_input_replaces_prediction ? 1 : 0,
            report.cache_consume_confirmed ? 1 : 0,
            report.duplicate_confirmed_idempotent ? 1 : 0,
            report.prediction_over_confirmed_rejected ? 1 : 0,
            report.wrong_identity_rejected ? 1 : 0,
            report.over_window_no_cache_write ? 1 : 0,
            report.network_thread_cache_write_rejected ? 1 : 0,
            report.drain_bypass_confirmed_input ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.cache_write_sequence);
        return 1;
    }

    std::printf(
        "rollback live-peer-pipeline self-test passed "
        "enqueue=%d queued_only=%d stock_drain=%d metadata=%d "
        "payload_not_cache=%d predict_cache=%d confirm_replace=%d "
        "consume_confirmed=%d duplicate=%d pred_over_confirmed=%d "
        "wrong_identity=%d late_no_cache=%d net_cache_reject=%d "
        "bypass=%d enqueued=%u drained=%u rejected=%u cache_writes=%u\n",
        report.bridge_enqueue_ok ? 1 : 0,
        report.network_receive_queued_only ? 1 : 0,
        report.stock_drain_required ? 1 : 0,
        report.metadata_drains_to_session ? 1 : 0,
        report.bridge_payload_not_cache_input ? 1 : 0,
        report.prediction_cache_write_ok ? 1 : 0,
        report.confirmed_input_replaces_prediction ? 1 : 0,
        report.cache_consume_confirmed ? 1 : 0,
        report.duplicate_confirmed_idempotent ? 1 : 0,
        report.prediction_over_confirmed_rejected ? 1 : 0,
        report.wrong_identity_rejected ? 1 : 0,
        report.over_window_no_cache_write ? 1 : 0,
        report.network_thread_cache_write_rejected ? 1 : 0,
        report.drain_bypass_confirmed_input ? 1 : 0,
        report.enqueued_packets,
        report.drained_packets,
        report.rejected_packets,
        report.cache_write_sequence);
    return 0;
}
