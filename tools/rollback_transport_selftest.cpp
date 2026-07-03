#include "../HorseMod/horselib/RollbackTransport.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackTransportSelfTestReport report =
        Horse::RunRollbackTransportModelSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback transport self-test failed failure=%s "
            "abs=%d dup=%d reorder=%d conflict=%d late=%d "
            "ack=%d invalid=%d handshake=%d handshake_reject=%d "
            "handshake_invalid=%d hash=%d loopback=%d "
            "net_write_reject=%d stock_order=%d\n",
            report.failure ? report.failure : "?",
            report.absolute_frame_roundtrip ? 1 : 0,
            report.duplicate_detected ? 1 : 0,
            report.reorder_detected ? 1 : 0,
            report.conflict_detected ? 1 : 0,
            report.over_window_rejected ? 1 : 0,
            report.ack_monotonic ? 1 : 0,
            report.invalid_packets_rejected ? 1 : 0,
            report.handshake_accepts_match ? 1 : 0,
            report.handshake_rejects_mismatch ? 1 : 0,
            report.handshake_rejects_invalid_policy ? 1 : 0,
            report.state_hash_policy_ok ? 1 : 0,
            report.loopback_delay_reorder_ok ? 1 : 0,
            report.network_thread_cache_write_rejected ? 1 : 0,
            report.stock_drain_ordering_ok ? 1 : 0);
        return 1;
    }

    std::printf(
        "rollback transport self-test passed contiguous=%u accepted=%u "
        "duplicates=%u reordered=%u conflicts=%u over_window=%u "
        "max_prediction_age=%u max_rollback_depth=%u "
        "ack_monotonic=%d invalid_packets=%d handshake=%d "
        "handshake_reject=%d handshake_invalid=%d "
        "hash_policy=%d loopback=%d\n",
        report.metrics.contiguous_remote_frame,
        report.metrics.packets_accepted,
        report.metrics.duplicates,
        report.metrics.reordered,
        report.metrics.conflicts,
        report.metrics.over_window_late,
        report.metrics.max_prediction_age,
        report.metrics.max_rollback_depth,
        report.ack_monotonic ? 1 : 0,
        report.invalid_packets_rejected ? 1 : 0,
        report.handshake_accepts_match ? 1 : 0,
        report.handshake_rejects_mismatch ? 1 : 0,
        report.handshake_rejects_invalid_policy ? 1 : 0,
        report.state_hash_policy_ok ? 1 : 0,
        report.loopback_delay_reorder_ok ? 1 : 0);
    return 0;
}
