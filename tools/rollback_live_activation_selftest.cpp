#include "../HorseMod/horselib/RollbackLiveActivationGate.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackLiveActivationSelfTestReport report =
        Horse::RunRollbackLiveActivationSelfTest();
    if (!report.ok)
    {
        std::printf(
            "rollback live-activation self-test failed failure=%s "
            "ready=%d readiness_only=%d stock=%d identity=%d boundary=%d "
            "session=%d input_log=%d self_peer=%d zero_session=%d "
            "operator=%d receive=%d non_hrg1=%d route_provenance=%d "
            "direct_ready=%d route_identity=%d\n",
            report.failure ? report.failure : "?",
            report.activation_ready ? 1 : 0,
            report.readiness_only_rejected ? 1 : 0,
            report.stock_surface_rejected ? 1 : 0,
            report.missing_identity_rejected ? 1 : 0,
            report.boundary_violation_rejected ? 1 : 0,
            report.missing_session_rejected ? 1 : 0,
            report.missing_input_log_rejected ? 1 : 0,
            report.self_peer_rejected ? 1 : 0,
            report.zero_session_rejected ? 1 : 0,
            report.operator_not_armed_rejected ? 1 : 0,
            report.missing_receive_rejected ? 1 : 0,
            report.non_hrg1_rejected ? 1 : 0,
            report.route_provenance_rejected ? 1 : 0,
            report.direct_readiness_rejected ? 1 : 0,
            report.route_identity_rejected ? 1 : 0);
        return 1;
    }

    std::printf(
        "rollback live-activation self-test passed ready=%d "
        "readiness_only=%d stock=%d identity=%d boundary=%d "
        "session=%d input_log=%d self_peer=%d zero_session=%d "
        "operator=%d receive=%d non_hrg1=%d route_provenance=%d "
        "direct_ready=%d route_identity=%d\n",
        report.activation_ready ? 1 : 0,
        report.readiness_only_rejected ? 1 : 0,
        report.stock_surface_rejected ? 1 : 0,
        report.missing_identity_rejected ? 1 : 0,
        report.boundary_violation_rejected ? 1 : 0,
        report.missing_session_rejected ? 1 : 0,
        report.missing_input_log_rejected ? 1 : 0,
        report.self_peer_rejected ? 1 : 0,
        report.zero_session_rejected ? 1 : 0,
        report.operator_not_armed_rejected ? 1 : 0,
        report.missing_receive_rejected ? 1 : 0,
        report.non_hrg1_rejected ? 1 : 0,
        report.route_provenance_rejected ? 1 : 0,
        report.direct_readiness_rejected ? 1 : 0,
        report.route_identity_rejected ? 1 : 0);
    return 0;
}
