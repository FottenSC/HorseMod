#include "RollbackMotionDecodeTraceWindow.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    using namespace Horse;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }
}

int main()
{
    using namespace Horse;

    auto decision = EvaluateMotionDecodeTraceWindow(
        {864, 0, false, -1, false});
    require(decision.enabled, "source lower boundary must be included");
    require(
        decision.source
            == MotionDecodeTraceCoordinateSource::ReplaySourceSequence,
        "source window must report replay coordinates");

    require(
        EvaluateMotionDecodeTraceWindow({878, 0, false, -1}).enabled,
        "source upper boundary must be included");
    require(
        !EvaluateMotionDecodeTraceWindow({863, 0, false, -1}).enabled,
        "source frame below the window must be excluded");
    require(
        !EvaluateMotionDecodeTraceWindow({879, 0, false, -1}).enabled,
        "source frame above the window must be excluded");
    require(
        !EvaluateMotionDecodeTraceWindow({871, 1, false, -1}).enabled,
        "the same source frame in another round must be excluded");

    require(
        EvaluateMotionDecodeTraceWindow(
            {-1, -1, true, 748, false}).enabled,
        "bounded owned evidence must not require deep diagnostics");

    decision = EvaluateMotionDecodeTraceWindow({-1, -1, true, 748, true});
    require(decision.enabled, "native lower boundary must be included");
    require(
        decision.source
            == MotionDecodeTraceCoordinateSource::RollbackLogicalFrame,
        "owned window must report rollback logical frames");
    require(
        EvaluateMotionDecodeTraceWindow({-1, -1, true, 762, true}).enabled,
        "native upper boundary must be included");
    require(
        !EvaluateMotionDecodeTraceWindow({-1, -1, true, 747, true}).enabled,
        "native frame below the window must be excluded");
    require(
        !EvaluateMotionDecodeTraceWindow({-1, -1, true, 763, true}).enabled,
        "native frame above the window must be excluded");
    require(
        !EvaluateMotionDecodeTraceWindow({-1, -1, false, 755}).enabled,
        "native coordinates require an owned native scope");

    for (int32_t coordinate = 0; coordinate <= 3; ++coordinate)
    {
        require(
            EvaluateMotionDecodeTraceWindow(
                {-1, -1, true, coordinate, true}).enabled,
            "bootstrap coordinates 0..3 must be included");
    }
    require(
        !EvaluateMotionDecodeTraceWindow({-1, -1, true, 4, true}).enabled,
        "post-bootstrap frame 4 must be excluded");

    decision = EvaluateMotionDecodeTraceWindow({871, 0, true, 755, true});
    require(
        decision.source
            == MotionDecodeTraceCoordinateSource::RollbackLogicalFrame,
        "owned logical frames must take precedence when both match");

    require(
        std::string(MotionDecodeTraceCoordinateSourceName(decision.source))
            == "rollback-logical-frame",
        "coordinate source name must remain stable for trace analyzers");

    require(
        !EvaluateMotionDecodeTraceWindow({871, 0, true, 100, true}).enabled,
        "a parked replay source cursor must not unbound rollback tracing");
    require(
        !EvaluateMotionDecodeTraceWindow(
            {871, 0, false, -1, true}).enabled,
        "a rollback lease must suppress parked source fallback between owned ticks");

    require(
        ShouldEmitRollbackDeepDiagnostic(true, true, true),
        "deep diagnostics require all three authorities");
    require(
        !ShouldEmitRollbackDeepDiagnostic(false, true, true)
            && !ShouldEmitRollbackDeepDiagnostic(true, false, true)
            && !ShouldEmitRollbackDeepDiagnostic(true, true, false),
        "acceptance traces must not accidentally enable deep diagnostics");

    constexpr uint32_t rollback_owner = 1u << 3;
    constexpr uint32_t timeline_owner = 1u << 1;
    constexpr uint32_t frame_input_owner = 1u << 4;
    constexpr uint32_t full_owner_mask =
        (1u << 0) | timeline_owner | (1u << 2);
    require(
        !ShouldEmitDetailedReplayLifecycleTrace(
            rollback_owner, full_owner_mask, rollback_owner, false),
        "rollback diagnostics must be quiet outside the focused window");
    require(
        ShouldEmitDetailedReplayLifecycleTrace(
            rollback_owner, full_owner_mask, rollback_owner, true),
        "rollback diagnostics must retain focused evidence");
    require(
        ShouldEmitDetailedReplayLifecycleTrace(
            timeline_owner, full_owner_mask, rollback_owner, false),
        "full timeline owners must retain their existing evidence");
    require(
        ShouldEmitDetailedReplayLifecycleTrace(
            timeline_owner | rollback_owner, full_owner_mask,
            rollback_owner, false),
        "a full owner must dominate a simultaneous rollback lease");
    require(
        !ShouldEmitDetailedReplayLifecycleTrace(
            0, full_owner_mask, rollback_owner, true),
        "an inactive lifecycle trace must remain quiet");
    require(
        !ShouldEmitDetailedReplayLifecycleTrace(
            frame_input_owner, full_owner_mask, rollback_owner, true),
        "frame-input-only ownership must not enable full lifecycle probes");

    require(
        !EvaluateDetailedReplayLifecycleTrace(
            rollback_owner, full_owner_mask, rollback_owner, false, 755u),
        "rollback-only tracing must stay disabled before native ownership");
    require(
        !EvaluateDetailedReplayLifecycleTrace(
            rollback_owner, full_owner_mask, rollback_owner, true, 100u),
        "rollback-only tracing must reject out-of-window owned coordinates");
    require(
        EvaluateDetailedReplayLifecycleTrace(
            rollback_owner, full_owner_mask, rollback_owner, true, 755u),
        "rollback-only tracing must admit a focused owned coordinate");
    require(
        EvaluateDetailedReplayLifecycleTrace(
            timeline_owner, full_owner_mask, rollback_owner, false, 0u),
        "full timeline tracing must not require rollback ownership");
    require(
        !EvaluateDetailedReplayLifecycleTrace(
            frame_input_owner, full_owner_mask, rollback_owner, true, 755u),
        "frame-input-only owner must stay isolated at focused coordinates");

    require(
        ShouldEmitRollbackAnimationCheckpoint(true, true, 0, false),
        "animation bootstrap frame zero must be retained");
    require(
        ShouldEmitRollbackAnimationCheckpoint(true, true, 31, false),
        "animation contract frame 31 must be retained");
    require(
        !ShouldEmitRollbackAnimationCheckpoint(true, true, 32, false),
        "animation frames outside evidence windows must be quiet");
    require(
        ShouldEmitRollbackAnimationCheckpoint(true, true, 755, false),
        "focused animation mismatch coordinate must be retained");
    require(
        !ShouldEmitRollbackAnimationCheckpoint(true, false, -1, false),
        "ordinary unowned animation ticks must not create full snapshots");
    require(
        ShouldEmitRollbackAnimationCheckpoint(true, false, -1, true),
        "an actual unowned mutation must retain full before/after evidence");
    require(
        !ShouldEmitRollbackAnimationCheckpoint(false, true, 0, false),
        "inactive rollback sessions must not emit animation checkpoints");

    std::cout << "Rollback motion-decode trace-window self-test passed\n";
    return 0;
}
