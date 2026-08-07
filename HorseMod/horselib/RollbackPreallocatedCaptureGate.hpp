#pragma once

#include <cstring>

namespace Horse
{
    struct RollbackPreallocatedCapturePreflightReport
    {
        bool ok {false};
        const char* failure {"not-run"};
    };

    template <typename State, typename Stage, typename Scratch,
              typename Limits, typename ShapeMatches,
              typename ExplicitReady, typename StateFailure,
              typename ScratchFailure, typename StageReady>
    static inline RollbackPreallocatedCapturePreflightReport
    ValidateRollbackPreallocatedCaptureGate(
        const State& state,
        const Stage* stage,
        const Scratch* scratch,
        const Limits* limits,
        ShapeMatches&& shape_matches,
        ExplicitReady&& explicit_ready,
        StateFailure&& state_failure,
        ScratchFailure&& scratch_failure,
        StageReady&& stage_ready) noexcept
    {
        RollbackPreallocatedCapturePreflightReport report {};
        if (!stage || !scratch || !limits || !limits->valid)
        {
            report.failure = "preallocated-capture-contract-invalid";
            return report;
        }
        if (!shape_matches(state, *limits))
        {
            report.failure = "preallocated-capture-shape-mismatch";
            return report;
        }
        if (!explicit_ready(state))
        {
            report.failure = "explicit-snapshot-capacity-insufficient";
            return report;
        }
        const char* state_reason = state_failure(state);
        if (!state_reason || std::strcmp(state_reason, "ok") != 0)
        {
            report.failure = state_reason ? state_reason
                : "state-capacity-preflight-invalid";
            return report;
        }
        const char* scratch_reason = scratch_failure(*scratch);
        if (!scratch_reason || std::strcmp(scratch_reason, "ok") != 0)
        {
            report.failure = scratch_reason ? scratch_reason
                : "scratch-capacity-preflight-invalid";
            return report;
        }
        if (!stage_ready(*stage, state))
        {
            report.failure = "breakable-stage-capacity-insufficient";
            return report;
        }
        report.ok = true;
        report.failure = "ok";
        return report;
    }
}
