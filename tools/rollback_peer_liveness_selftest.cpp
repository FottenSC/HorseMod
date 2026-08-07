#include "../HorseMod/horselib/RollbackPeerLiveness.hpp"

#include <cstdio>

int main()
{
    using Horse::RollbackPeerLiveness;
    using Horse::RollbackPeerLivenessResult;

    RollbackPeerLiveness liveness;
    bool ok = true;
    ok = ok && liveness.observe(true, 10, 1'000'000)
        == RollbackPeerLivenessResult::Healthy;
    ok = ok && liveness.observe(
        true, 10,
        1'000'000
            + RollbackPeerLiveness::kAuthenticatedTrafficTimeoutUs - 1)
        == RollbackPeerLivenessResult::Healthy;
    ok = ok && liveness.observe(
        true, 10,
        1'000'000
            + RollbackPeerLiveness::kAuthenticatedTrafficTimeoutUs)
        == RollbackPeerLivenessResult::AuthenticatedTrafficTimeout;

    ok = ok && liveness.observe(true, 11, 7'000'000)
        == RollbackPeerLivenessResult::Healthy;
    ok = ok && liveness.last_authenticated_packets() == 11;
    ok = ok && liveness.last_progress_us() == 7'000'000;
    ok = ok && liveness.observe(false, 11, 7'000'001)
        == RollbackPeerLivenessResult::TransportUnavailable;

    liveness.reset();
    ok = ok && liveness.observe(true, 20, 100)
        == RollbackPeerLivenessResult::Healthy;
    ok = ok && liveness.observe(true, 20, 50)
        == RollbackPeerLivenessResult::Healthy;
    ok = ok && liveness.last_progress_us() == 50;

    liveness.reset();
    ok = ok && liveness.observe(true, 30, 100)
        == RollbackPeerLivenessResult::Healthy;
    for (uint32_t observation = 1;
         observation < RollbackPeerLiveness::kMaximumStalledObservations;
         ++observation)
    {
        ok = ok && liveness.observe(true, 30, 101)
            == RollbackPeerLivenessResult::Healthy;
    }
    ok = ok && liveness.observe(true, 30, 101)
        == RollbackPeerLivenessResult::AuthenticatedTrafficTimeout;

    std::printf(
        "rollback peer liveness self-test %s\n",
        ok ? "passed" : "failed");
    return ok ? 0 : 1;
}
