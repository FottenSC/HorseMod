#include "../HorseMod/horselib/RollbackLiveActivationExecutor.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackLiveActivationExecutorSelfTestReport report =
        Horse::RunRollbackLiveActivationExecutorSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback live-activation-executor self-test failed failure=%s "
            "activation_required=%d readiness_only=%d stock=%d "
            "provenance=%d route_identity=%d ready=%d enqueue=%d "
            "queued_only=%d stock_drain=%d metadata=%d "
            "metadata_not_gameplay=%d predict=%d apply=%d consume=%d "
            "net_cache_reject=%d wrong_source=%d wrong_dest=%d "
            "wrong_session=%d decoded_route=%d "
            "enqueued=%u drained=%u rejected=%u cache_writes=%u\n",
            report.failure ? report.failure : "?",
            report.activation_required_rejected ? 1 : 0,
            report.readiness_only_rejected ? 1 : 0,
            report.stock_surface_rejected ? 1 : 0,
            report.provenance_required_rejected ? 1 : 0,
            report.route_identity_rejected ? 1 : 0,
            report.activation_ready ? 1 : 0,
            report.live_enqueue_ok ? 1 : 0,
            report.network_receive_queued_only ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.metadata_drained ? 1 : 0,
            report.metadata_not_gameplay_input ? 1 : 0,
            report.prediction_written ? 1 : 0,
            report.decoded_gameplay_applied ? 1 : 0,
            report.confirmed_consumed ? 1 : 0,
            report.network_thread_cache_rejected ? 1 : 0,
            report.wrong_source_rejected ? 1 : 0,
            report.wrong_destination_rejected ? 1 : 0,
            report.wrong_session_rejected ? 1 : 0,
            report.decoded_route_rejected ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.cache_write_sequence);
        return 1;
    }

    std::printf(
        "rollback live-activation-executor self-test passed "
        "activation_required=%d readiness_only=%d stock=%d provenance=%d "
        "route_identity=%d ready=%d enqueue=%d queued_only=%d "
        "stock_drain=%d metadata=%d metadata_not_gameplay=%d predict=%d "
        "apply=%d consume=%d net_cache_reject=%d wrong_source=%d "
        "wrong_dest=%d wrong_session=%d decoded_route=%d "
        "enqueued=%u drained=%u rejected=%u cache_writes=%u\n",
        report.activation_required_rejected ? 1 : 0,
        report.readiness_only_rejected ? 1 : 0,
        report.stock_surface_rejected ? 1 : 0,
        report.provenance_required_rejected ? 1 : 0,
        report.route_identity_rejected ? 1 : 0,
        report.activation_ready ? 1 : 0,
        report.live_enqueue_ok ? 1 : 0,
        report.network_receive_queued_only ? 1 : 0,
        report.stock_drain_required ? 1 : 0,
        report.metadata_drained ? 1 : 0,
        report.metadata_not_gameplay_input ? 1 : 0,
        report.prediction_written ? 1 : 0,
        report.decoded_gameplay_applied ? 1 : 0,
        report.confirmed_consumed ? 1 : 0,
        report.network_thread_cache_rejected ? 1 : 0,
        report.wrong_source_rejected ? 1 : 0,
        report.wrong_destination_rejected ? 1 : 0,
        report.wrong_session_rejected ? 1 : 0,
        report.decoded_route_rejected ? 1 : 0,
        report.enqueued_packets,
        report.drained_packets,
        report.rejected_packets,
        report.cache_write_sequence);
    return 0;
}
