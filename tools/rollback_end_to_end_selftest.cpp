#include "../HorseMod/horselib/RollbackEndToEndHarness.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackEndToEndSelfTestReport report =
        Horse::RunRollbackEndToEndSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback end-to-end self-test failed failure=%s "
            "decode=%d bridge=%d predict=%d predicted_diff=%d "
            "queued_only=%d stock_drain=%d metadata=%d correction=%d "
            "metadata_not_gameplay=%d confirm_apply=%d confirm_consume=%d "
            "state=%d wrong_identity=%d net_cache_reject=%d "
            "enqueued=%u drained=%u rejected=%u cache_writes=%u "
            "pred_a=0x%08X pred_b=0x%08X confirm_a=0x%08X "
            "confirm_b=0x%08X\n",
            report.failure ? report.failure : "?",
            report.decoded_payloads ? 1 : 0,
            report.bridge_roundtrip ? 1 : 0,
            report.prediction_written ? 1 : 0,
            report.prediction_diverged ? 1 : 0,
            report.network_receive_queued_only ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.metadata_accepted ? 1 : 0,
            report.metadata_requires_correction ? 1 : 0,
            report.metadata_not_gameplay_input ? 1 : 0,
            report.confirmed_applied ? 1 : 0,
            report.confirmed_consumed ? 1 : 0,
            report.state_converged ? 1 : 0,
            report.wrong_identity_rejected ? 1 : 0,
            report.network_thread_cache_write_rejected ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.cache_write_sequence,
            report.predicted_checksum_a,
            report.predicted_checksum_b,
            report.confirmed_checksum_a,
            report.confirmed_checksum_b);
        return 1;
    }

    std::printf(
        "rollback end-to-end self-test passed "
        "decode=%d bridge=%d predict=%d predicted_diff=%d "
        "queued_only=%d stock_drain=%d metadata=%d correction=%d "
        "metadata_not_gameplay=%d confirm_apply=%d confirm_consume=%d "
        "state=%d wrong_identity=%d net_cache_reject=%d "
        "enqueued=%u drained=%u rejected=%u cache_writes=%u "
        "pred_a=0x%08X pred_b=0x%08X confirm_a=0x%08X "
        "confirm_b=0x%08X\n",
        report.decoded_payloads ? 1 : 0,
        report.bridge_roundtrip ? 1 : 0,
        report.prediction_written ? 1 : 0,
        report.prediction_diverged ? 1 : 0,
        report.network_receive_queued_only ? 1 : 0,
        report.stock_drain_required ? 1 : 0,
        report.metadata_accepted ? 1 : 0,
        report.metadata_requires_correction ? 1 : 0,
        report.metadata_not_gameplay_input ? 1 : 0,
        report.confirmed_applied ? 1 : 0,
        report.confirmed_consumed ? 1 : 0,
        report.state_converged ? 1 : 0,
        report.wrong_identity_rejected ? 1 : 0,
        report.network_thread_cache_write_rejected ? 1 : 0,
        report.enqueued_packets,
        report.drained_packets,
        report.rejected_packets,
        report.cache_write_sequence,
        report.predicted_checksum_a,
        report.predicted_checksum_b,
        report.confirmed_checksum_a,
        report.confirmed_checksum_b);
    return 0;
}
