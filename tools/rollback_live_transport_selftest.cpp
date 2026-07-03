#include "../HorseMod/horselib/RollbackLiveTransportQueue.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackLiveTransportQueueSelfTestReport report =
        Horse::RunRollbackLiveTransportQueueSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback live-transport self-test failed failure=%s "
            "enqueue=%d bad=%d wrong_source=%d wrong_dest=%d "
            "wrong_session=%d queued_only=%d stock_drain=%d drain=%d "
            "correction=%d duplicate=%d late=%d bypass=%d capacity=%d "
            "enqueued=%u drained=%u rejected=%u queued=%u\n",
            report.failure ? report.failure : "?",
            report.bridge_enqueue_ok ? 1 : 0,
            report.bad_bridge_rejected ? 1 : 0,
            report.wrong_source_rejected ? 1 : 0,
            report.wrong_destination_rejected ? 1 : 0,
            report.wrong_session_rejected ? 1 : 0,
            report.network_receive_queued_only ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.game_thread_drain_accepts ? 1 : 0,
            report.correction_required ? 1 : 0,
            report.duplicate_drained ? 1 : 0,
            report.over_window_rejected ? 1 : 0,
            report.drain_bypass_ok ? 1 : 0,
            report.capacity_guard ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.queue_count);
        return 1;
    }

    std::printf(
        "rollback live-transport self-test passed "
        "enqueue=%d bad=%d wrong_source=%d wrong_dest=%d "
        "wrong_session=%d queued_only=%d stock_drain=%d drain=%d "
        "correction=%d duplicate=%d late=%d bypass=%d capacity=%d "
        "enqueued=%u drained=%u rejected=%u queued=%u\n",
        report.bridge_enqueue_ok ? 1 : 0,
        report.bad_bridge_rejected ? 1 : 0,
        report.wrong_source_rejected ? 1 : 0,
        report.wrong_destination_rejected ? 1 : 0,
        report.wrong_session_rejected ? 1 : 0,
        report.network_receive_queued_only ? 1 : 0,
        report.stock_drain_required ? 1 : 0,
        report.game_thread_drain_accepts ? 1 : 0,
        report.correction_required ? 1 : 0,
        report.duplicate_drained ? 1 : 0,
        report.over_window_rejected ? 1 : 0,
        report.drain_bypass_ok ? 1 : 0,
        report.capacity_guard ? 1 : 0,
        report.enqueued_packets,
        report.drained_packets,
        report.rejected_packets,
        report.queue_count);
    return 0;
}
