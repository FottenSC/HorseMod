#pragma once

#include <cstdint>

namespace Horse
{
    enum class MotionDecodeTraceCoordinateSource : uint8_t
    {
        None = 0,
        ReplaySourceSequence,
        RollbackLogicalFrame,
    };

    struct MotionDecodeTraceWindowInput
    {
        int32_t replay_sequence {-1};
        int32_t replay_round {-1};
        bool rollback_native_scope_active {false};
        int32_t rollback_logical_frame {-1};
        bool rollback_diagnostic_owner_active {false};
    };

    struct MotionDecodeTraceWindowDecision
    {
        bool enabled {false};
        MotionDecodeTraceCoordinateSource source {
            MotionDecodeTraceCoordinateSource::None};
    };

    // Evidence window for the first known primitive normal/rollback mismatch:
    // replay round 0, source frame 871, rollback logical frame 755. Keeping
    // this policy pure makes the diagnostic volume limit independently
    // testable; it must never become an unbounded per-sample trace again.
    constexpr MotionDecodeTraceWindowDecision
    EvaluateMotionDecodeTraceWindow(
        const MotionDecodeTraceWindowInput& input) noexcept
    {
        constexpr int32_t kSourceWindowFirst = 864;
        constexpr int32_t kSourceWindowLast = 878;
        constexpr int32_t kLogicalWindowFirst = 748;
        constexpr int32_t kLogicalWindowLast = 762;

        // During rollback, the replay driver's native source cursor can remain
        // parked while Gekko advances and resimulates many logical frames.
        // It is therefore not a usable volume bound.  Once an owned native
        // scope exists, its logical coordinate is the sole authority.
        if (input.rollback_native_scope_active
            && ((input.rollback_logical_frame >= kLogicalWindowFirst
                    && input.rollback_logical_frame <= kLogicalWindowLast)
                || (input.rollback_logical_frame >= 0
                    && input.rollback_logical_frame <= 3)))
        {
            return {
                true,
                MotionDecodeTraceCoordinateSource::RollbackLogicalFrame};
        }

        // A rollback diagnostic lease remains active between owned native
        // transactions while ReplayScrub's source cursor can stay parked on
        // the focused frame. Never fall back to that cursor for the lifetime
        // of the rollback diagnostic session.
        if (!input.rollback_diagnostic_owner_active
            && !input.rollback_native_scope_active
            && input.replay_round == 0
            && input.replay_sequence >= kSourceWindowFirst
            && input.replay_sequence <= kSourceWindowLast)
        {
            return {
                true,
                MotionDecodeTraceCoordinateSource::ReplaySourceSequence};
        }

        return {};
    }

    // Deep differential evidence is intentionally distinct from the compact
    // evidence required by the live beta gate. Capturing every primitive
    // checkpoint perturbs frame pacing and can turn the validator itself into
    // the measured bottleneck.
    constexpr bool ShouldEmitRollbackDeepDiagnostic(
        bool deep_trace_diagnostics,
        bool replay_input_enabled,
        bool trace_enabled) noexcept
    {
        return deep_trace_diagnostics
            && replay_input_enabled
            && trace_enabled;
    }

    // Full replay/timeline owners retain their historical unbounded lifecycle
    // evidence.  A rollback-diagnostics-only lease is intentionally reduced
    // to the focused motion window so hot native hooks cannot perturb the
    // simulation or create multi-gigabyte traces.
    constexpr bool ShouldEmitDetailedReplayLifecycleTrace(
        uint32_t owner_mask,
        uint32_t full_trace_owner_mask,
        uint32_t rollback_diagnostics_owner_bit,
        bool focused_window_enabled) noexcept
    {
        if (owner_mask == 0 || rollback_diagnostics_owner_bit == 0)
            return false;
        const uint32_t full_trace_owners = owner_mask & full_trace_owner_mask;
        return full_trace_owners != 0
            || ((owner_mask & rollback_diagnostics_owner_bit) != 0
                && focused_window_enabled);
    }

    // Rollback-only lifecycle probes are admitted solely by the owned native
    // logical coordinate. Stock setup must not reach into ReplayScrub merely
    // to decide whether a diagnostic event should exist.
    constexpr bool EvaluateDetailedReplayLifecycleTrace(
        uint32_t owner_mask,
        uint32_t full_trace_owner_mask,
        uint32_t rollback_diagnostics_owner_bit,
        bool rollback_native_scope_active,
        uint32_t rollback_logical_frame) noexcept
    {
        const uint32_t full_trace_owners = owner_mask & full_trace_owner_mask;
        if (full_trace_owners != 0)
            return true;
        if ((owner_mask & rollback_diagnostics_owner_bit) == 0
            || !rollback_native_scope_active)
            return false;
        const MotionDecodeTraceWindowDecision decision =
            EvaluateMotionDecodeTraceWindow({
                -1,
                -1,
                true,
                static_cast<int32_t>(rollback_logical_frame),
                true});
        return ShouldEmitDetailedReplayLifecycleTrace(
            owner_mask, full_trace_owner_mask,
            rollback_diagnostics_owner_bit,
            decision.enabled);
    }

    // Production animation checkpoints are large. Keep complete evidence for
    // the contract window used by the validator, the focused mismatch window,
    // and an actual unowned mutation. Ordinary pass-through ticks are covered
    // by compact counters and must not create an unbounded trace.
    constexpr bool ShouldEmitRollbackAnimationCheckpoint(
        bool rollback_session_active,
        bool owned_simulation,
        int32_t logical_frame,
        bool unowned_mutation) noexcept
    {
        if (!rollback_session_active)
            return false;
        if (!owned_simulation)
            return unowned_mutation;
        return (logical_frame >= 0 && logical_frame <= 31)
            || (logical_frame >= 748 && logical_frame <= 762);
    }

    constexpr const char* MotionDecodeTraceCoordinateSourceName(
        MotionDecodeTraceCoordinateSource source) noexcept
    {
        switch (source)
        {
        case MotionDecodeTraceCoordinateSource::ReplaySourceSequence:
            return "replay-source-sequence";
        case MotionDecodeTraceCoordinateSource::RollbackLogicalFrame:
            return "rollback-logical-frame";
        default:
            return "none";
        }
    }
}
