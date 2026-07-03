#include "../HorseMod/horselib/RollbackLiveOnlineCapture.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackLiveOnlineCaptureReport report =
        Horse::RunRollbackLiveOnlineCaptureSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback live-online-capture self-test failed failure=%s "
            "ready=%d live=%d recv=%d violation=%d total=%llu\n",
            report.failure ? report.failure : "?",
            report.capture_ready ? 1 : 0,
            report.live_capture_complete ? 1 : 0,
            report.receive_enqueue_observed ? 1 : 0,
            report.boundary_violation ? 1 : 0,
            static_cast<unsigned long long>(report.total_observed_calls));
        return 1;
    }

    std::printf(
        "rollback live-online-capture self-test passed ready=%d live=%d "
        "recv=%d violation_case=%d acquire=%llu input=%llu battle=%llu "
        "receive=%llu drain=%llu/%llu consumer=%llu total=%llu\n",
        report.capture_ready ? 1 : 0,
        report.live_capture_complete ? 1 : 0,
        report.receive_enqueue_observed ? 1 : 0,
        report.boundary_violation ? 1 : 0,
        static_cast<unsigned long long>(report.acquire_count),
        static_cast<unsigned long long>(report.input_send_count),
        static_cast<unsigned long long>(
            report.battle_sync_request_stage_count),
        static_cast<unsigned long long>(report.receive_enqueue_count),
        static_cast<unsigned long long>(report.drain_enter_count),
        static_cast<unsigned long long>(report.drain_exit_count),
        static_cast<unsigned long long>(report.consumer_count),
        static_cast<unsigned long long>(report.total_observed_calls));
    return 0;
}
