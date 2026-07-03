#include "../HorseMod/horselib/RollbackStockTransportObserveModel.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackStockTransportObserveSelfTestReport report =
        Horse::RunRollbackStockTransportObserveSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback stock-transport observe self-test failed failure=%s "
            "hooks=%d trace=%d acquire=%d opcode0=%d opcode1=%d "
            "battle=%d recv=%d totals=%d observed=%llu\n",
            report.failure ? report.failure : "?",
            report.hooks_gate ? 1 : 0,
            report.trace_gate ? 1 : 0,
            report.acquire_recorded ? 1 : 0,
            report.opcode0_recorded ? 1 : 0,
            report.opcode1_recorded ? 1 : 0,
            report.battle_sync_recorded ? 1 : 0,
            report.receive_enqueue_recorded ? 1 : 0,
            report.totals_ok ? 1 : 0,
            static_cast<unsigned long long>(
                report.observed.total_observed_calls));
        return 1;
    }

    std::printf(
        "rollback stock-transport observe self-test passed "
        "hooks=%d trace=%d acquire=%d opcode0=%d opcode1=%d "
        "battle=%d recv=%d totals=%d observed=%llu\n",
        report.hooks_gate ? 1 : 0,
        report.trace_gate ? 1 : 0,
        report.acquire_recorded ? 1 : 0,
        report.opcode0_recorded ? 1 : 0,
        report.opcode1_recorded ? 1 : 0,
        report.battle_sync_recorded ? 1 : 0,
        report.receive_enqueue_recorded ? 1 : 0,
        report.totals_ok ? 1 : 0,
        static_cast<unsigned long long>(
            report.observed.total_observed_calls));
    return 0;
}
