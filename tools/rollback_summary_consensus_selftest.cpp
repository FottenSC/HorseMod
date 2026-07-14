#include "../HorseMod/horselib/RollbackSummaryConsensus.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackSummaryConsensusSelfTestReport report =
        Horse::RunRollbackSummaryConsensusSelfTest();
    std::printf(
        "rollback summary-consensus self-test %s peer_first=%d "
        "ack_first=%d no_skip=%d bilateral=%d reorder=%d duplicate=%d stale=%d "
        "collision=%d too_far=%d wrap=%d failure=%s\n",
        report.ok ? "passed" : "failed",
        report.peer_summary_first ? 1 : 0,
        report.ack_first ? 1 : 0,
        report.later_frame_cannot_skip ? 1 : 0,
        report.bilateral_ack_required ? 1 : 0,
        report.reordered_drains_contiguously ? 1 : 0,
        report.duplicates_idempotent ? 1 : 0,
        report.stale_after_reuse_safe ? 1 : 0,
        report.collision_rejected ? 1 : 0,
        report.too_far_rejected ? 1 : 0,
        report.wrap_aware ? 1 : 0,
        report.failure ? report.failure : "?");
    return report.ok ? 0 : 1;
}
