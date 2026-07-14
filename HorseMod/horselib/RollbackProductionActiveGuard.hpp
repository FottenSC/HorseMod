#pragma once

namespace Horse
{
    struct RollbackProductionActiveGuardInput
    {
        bool schema_coverage_ready {false};
        bool schema_unchanged {false};
        bool lifecycle_epoch_ok {false};
        bool lifecycle_generation_unchanged {false};
        bool lifecycle_active {false};
        bool network_running {false};
        bool endpoint_open {false};
        bool endpoint_pinned {false};
        bool peer_ready {false};
        bool network_failure_none {false};
        bool handshake_generation_unchanged {false};
    };

    struct RollbackProductionActiveGuardReport
    {
        bool ok {false};
        const char* failure {"not-run"};
    };

    static inline RollbackProductionActiveGuardReport
    EvaluateRollbackProductionActiveGuard(
        const RollbackProductionActiveGuardInput& input) noexcept
    {
        if (!input.schema_coverage_ready || !input.schema_unchanged)
            return {false, "active-schema-or-coverage-changed"};
        if (!input.lifecycle_epoch_ok
            || !input.lifecycle_generation_unchanged
            || !input.lifecycle_active)
        {
            return {false, "active-lifecycle-epoch-changed"};
        }
        if (!input.network_running
            || !input.endpoint_open
            || !input.endpoint_pinned
            || !input.peer_ready
            || !input.network_failure_none
            || !input.handshake_generation_unchanged)
        {
            return {false, "active-peer-readiness-lost"};
        }
        return {true, "ok"};
    }
}
