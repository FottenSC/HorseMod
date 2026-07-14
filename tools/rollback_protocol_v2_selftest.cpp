#include "../HorseMod/horselib/RollbackProtocolV2.hpp"

#include <cstdio>

int main()
{
    const Horse::RollbackProtocolV2SelfTestReport report =
        Horse::RunRollbackProtocolV2SelfTest();
    std::printf(
        "rollback protocol-v2 self-test %s nonce=%d roundtrip=%d ack=%d "
        "payload_reject=%d tag_reject=%d secret_reject=%d build_reject=%d "
        "schema_reject=%d replay=%d nonce_replay=%d failure=%s\n",
        report.ok ? "passed" : "failed",
        report.nonce_generated ? 1 : 0,
        report.roundtrip ? 1 : 0,
        report.ack_present_roundtrip ? 1 : 0,
        report.corrupted_payload_rejected ? 1 : 0,
        report.corrupted_tag_rejected ? 1 : 0,
        report.wrong_secret_rejected ? 1 : 0,
        report.build_mismatch_rejected ? 1 : 0,
        report.schema_mismatch_rejected ? 1 : 0,
        report.replay_window_ok ? 1 : 0,
        report.nonce_scoped_replay_ok ? 1 : 0,
        report.failure ? report.failure : "?");
    return report.ok ? 0 : 1;
}
