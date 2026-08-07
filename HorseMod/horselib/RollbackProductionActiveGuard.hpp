#pragma once

#include <cstdint>

namespace Horse
{
    enum class RollbackDiagnosticReleaseDecision : uint8_t
    {
        NotRequested = 0,
        Release = 1,
        Reject = 2,
    };

    static constexpr RollbackDiagnosticReleaseDecision
    EvaluateRollbackDiagnosticRelease(
        bool release_requested,
        bool current_is_production,
        bool current_is_native_correction,
        bool requested_is_production,
        bool requested_is_native_correction,
        bool requested_production_enabled) noexcept
    {
        if (!release_requested)
            return RollbackDiagnosticReleaseDecision::NotRequested;
        return current_is_production
                && current_is_native_correction
                && requested_is_production
                && requested_is_native_correction
                && !requested_production_enabled
            ? RollbackDiagnosticReleaseDecision::Release
            : RollbackDiagnosticReleaseDecision::Reject;
    }

    static constexpr bool ShouldServiceStockBattleControlPlane(
        bool battle_ready,
        uint64_t service_tick) noexcept
    {
        return !battle_ready || service_tick == 1
            || (service_tick % 6u) == 0u;
    }

    static constexpr bool ShouldServiceRollbackProduction(
        bool selection_ready,
        bool production_active,
        bool native_commit_observation_pending = false) noexcept
    {
        // Native tick ownership begins before the bilateral launch barrier and
        // Gekko handshake finish. Keep servicing those activation states; only
        // the fully active PerFrame coordinator owns round simulation and
        // terminal handoff decisions.
        return selection_ready
            && (!production_active || native_commit_observation_pending);
    }

    static constexpr bool ShouldServiceActiveOwnedSimulationLiveness(
        bool production_active,
        bool rollback_phase_active,
        bool terminal_pending,
        bool terminal_quiesced) noexcept
    {
        // This path only observes organic SimulationLoop entries and invokes
        // the owned-loop fallback after its grace window. Terminal consensus
        // and native inter-round transitions retain their existing service
        // paths and must never be entered from this liveness heartbeat.
        return production_active && rollback_phase_active
            && !terminal_pending && !terminal_quiesced;
    }

    static constexpr uint32_t
        kRollbackActiveOwnedSimulationProbeGraceTicks = 3;

    static constexpr bool ShouldProbeActiveOwnedSimulationLiveness(
        bool production_active,
        uint64_t organic_entries,
        uint64_t previously_observed_entries,
        uint32_t consecutive_misses) noexcept
    {
        return production_active
            && organic_entries == previously_observed_entries
            && consecutive_misses
                >= kRollbackActiveOwnedSimulationProbeGraceTicks;
    }

    static constexpr bool ShouldUseAcceptedRollbackManifestForService(
        bool stock_online_pvp,
        bool hook_owns_tick,
        bool accepted_manifest_valid) noexcept
    {
        // Once frame 0 is frozen, the accepted immutable manifest owns
        // activation. The controller's live manifest is intentionally
        // transient and may report an inactive epoch during the freeze.
        return stock_online_pvp && hook_owns_tick && accepted_manifest_valid;
    }

    static constexpr bool ShouldArmRollbackRoundTerminal(
        uint64_t commits_before_confirmation,
        uint64_t commits_after_confirmation,
        uint64_t commits_at_session_start) noexcept
    {
        return commits_after_confirmation > commits_before_confirmation
            && commits_after_confirmation > commits_at_session_start;
    }

    static constexpr bool ShouldCaptureRollbackTerminalCheckpoint(
        bool committed_terminal_state,
        bool journaled_round_transition_edge) noexcept
    {
        return committed_terminal_state
            && journaled_round_transition_edge;
    }

    static constexpr bool ShouldSuppressStockPerFrameForRoundTransition(
        bool rearm_pending, bool boundary_frozen) noexcept
    {
        // Inter-round ticks on the retained online BattleManager remain
        // stock-owned. Suppress only after the observer freezes the accepted
        // pre-control boundary for the next round.
        return rearm_pending && boundary_frozen;
    }

    static constexpr bool IsRollbackCommittedRoundResultBoundary(
        uint8_t expected_battle_status,
        uint8_t post_advance_battle_status,
        uint16_t committed_round_result_type) noexcept
    {
        return (expected_battle_status == 1
                || expected_battle_status == 2)
            && (post_advance_battle_status == 3
                || committed_round_result_type != 0);
    }

    static constexpr bool IsRollbackTerminalControlPairCompatible(
        uint8_t local_terminal,
        uint16_t local_result_type,
        uint8_t remote_terminal,
        uint16_t remote_result_type) noexcept
    {
        if (local_terminal == 0 && remote_terminal == 0)
            return false;
        return local_terminal == 0 || remote_terminal == 0
            || local_result_type == remote_result_type;
    }

    static constexpr bool ShouldQuiesceRollbackRoundTerminal(
        uint8_t local_terminal, uint8_t remote_terminal) noexcept
    {
        // The peers can observe the native result edge one logical frame
        // apart. Only a frame whose two authenticated summaries are already
        // terminal is safe for the shared restore-and-native-commit handoff.
        return local_terminal != 0 && remote_terminal != 0;
    }

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
