#include "RollbackProductionActiveGuard.hpp"

#include <cstdio>
#include <cstring>

namespace
{
    Horse::RollbackProductionActiveGuardInput ready_input()
    {
        Horse::RollbackProductionActiveGuardInput input {};
        input.schema_coverage_ready = true;
        input.schema_unchanged = true;
        input.lifecycle_epoch_ok = true;
        input.lifecycle_generation_unchanged = true;
        input.lifecycle_active = true;
        input.network_running = true;
        input.endpoint_open = true;
        input.endpoint_pinned = true;
        input.peer_ready = true;
        input.network_failure_none = true;
        input.handshake_generation_unchanged = true;
        return input;
    }

    bool failure_is(
        const Horse::RollbackProductionActiveGuardInput& input,
        const char* expected)
    {
        const auto report =
            Horse::EvaluateRollbackProductionActiveGuard(input);
        return !report.ok && std::strcmp(report.failure, expected) == 0;
    }
}

int main()
{
    const auto ready = ready_input();
    if (!Horse::EvaluateRollbackProductionActiveGuard(ready).ok)
        return 1;

    auto presence_change = ready;
    presence_change.lifecycle_active = false;
    auto epoch_change = ready;
    epoch_change.lifecycle_generation_unchanged = false;
    auto udp_generation_change = ready;
    udp_generation_change.handshake_generation_unchanged = false;
    auto peer_loss = ready;
    peer_loss.peer_ready = false;
    auto schema_change = ready;
    schema_change.schema_unchanged = false;

    const bool presence_rejected = failure_is(
        presence_change, "active-lifecycle-epoch-changed");
    const bool epoch_rejected = failure_is(
        epoch_change, "active-lifecycle-epoch-changed");
    const bool generation_rejected = failure_is(
        udp_generation_change, "active-peer-readiness-lost");
    const bool peer_rejected = failure_is(
        peer_loss, "active-peer-readiness-lost");
    const bool schema_rejected = failure_is(
        schema_change, "active-schema-or-coverage-changed");

    std::printf(
        "rollback production active-guard self-test passed "
        "presence=%d epoch=%d udp_generation=%d peer_loss=%d schema=%d\n",
        presence_rejected ? 1 : 0,
        epoch_rejected ? 1 : 0,
        generation_rejected ? 1 : 0,
        peer_rejected ? 1 : 0,
        schema_rejected ? 1 : 0);
    return presence_rejected && epoch_rejected && generation_rejected
            && peer_rejected && schema_rejected
        ? 0 : 2;
}
