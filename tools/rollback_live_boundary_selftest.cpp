#include "../HorseMod/horselib/RollbackLiveBoundary.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackLiveBoundaryReport report =
        Horse::RunRollbackLiveBoundaryModelSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback live-boundary self-test failed failure=%s "
            "offline=%d live_order=%d consumer_during_drain=%d "
            "unbalanced=%d\n",
            report.failure ? report.failure : "?",
            report.offline_boundary_observed ? 1 : 0,
            report.live_order_proven ? 1 : 0,
            report.consumer_during_drain ? 1 : 0,
            report.unbalanced_drain ? 1 : 0);
        return 1;
    }

    std::printf(
        "rollback live-boundary self-test passed offline=%d live_order=%d "
        "bad_consumer_during_drain=%d bad_unbalanced=%d\n",
        report.offline_boundary_observed ? 1 : 0,
        report.live_order_proven ? 1 : 0,
        report.consumer_during_drain ? 1 : 0,
        report.unbalanced_drain ? 1 : 0);
    return 0;
}
