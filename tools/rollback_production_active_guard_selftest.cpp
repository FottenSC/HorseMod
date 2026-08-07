#include "RollbackProductionActiveGuard.hpp"
#include "RollbackNativeInputOverride.hpp"

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
    using ReleaseDecision = Horse::RollbackDiagnosticReleaseDecision;
    const bool diagnostic_release_gate =
        Horse::EvaluateRollbackDiagnosticRelease(
            false, true, true, true, true, false)
                == ReleaseDecision::NotRequested
        && Horse::EvaluateRollbackDiagnosticRelease(
            true, true, true, true, true, false)
                == ReleaseDecision::Release
        && Horse::EvaluateRollbackDiagnosticRelease(
            true, false, true, true, true, false)
                == ReleaseDecision::Reject
        && Horse::EvaluateRollbackDiagnosticRelease(
            true, true, false, true, true, false)
                == ReleaseDecision::Reject
        && Horse::EvaluateRollbackDiagnosticRelease(
            true, true, true, true, false, false)
                == ReleaseDecision::Reject
        && Horse::EvaluateRollbackDiagnosticRelease(
            true, true, true, true, true, true)
                == ReleaseDecision::Reject;
    const bool activation_service_gate =
        !Horse::ShouldServiceRollbackProduction(false, false)
        && Horse::ShouldServiceRollbackProduction(true, false)
        && !Horse::ShouldServiceRollbackProduction(true, true)
        && Horse::ShouldServiceRollbackProduction(true, true, true)
        && !Horse::ShouldServiceRollbackProduction(false, true, true)
        && Horse::ShouldServiceActiveOwnedSimulationLiveness(
            true, true, false, false)
        && !Horse::ShouldServiceActiveOwnedSimulationLiveness(
            false, true, false, false)
        && !Horse::ShouldServiceActiveOwnedSimulationLiveness(
            true, false, false, false)
        && !Horse::ShouldServiceActiveOwnedSimulationLiveness(
            true, true, true, false)
        && !Horse::ShouldServiceActiveOwnedSimulationLiveness(
            true, true, false, true)
        && !Horse::ShouldProbeActiveOwnedSimulationLiveness(
            true, 12, 12, 2)
        && Horse::ShouldProbeActiveOwnedSimulationLiveness(
            true, 12, 12, 3)
        && !Horse::ShouldProbeActiveOwnedSimulationLiveness(
            true, 13, 12, 3)
        && !Horse::ShouldProbeActiveOwnedSimulationLiveness(
            false, 12, 12, 3);
    const bool accepted_manifest_service =
        Horse::ShouldUseAcceptedRollbackManifestForService(
            true, true, true)
        && !Horse::ShouldUseAcceptedRollbackManifestForService(
            false, true, true)
        && !Horse::ShouldUseAcceptedRollbackManifestForService(
            true, false, true)
        && !Horse::ShouldUseAcceptedRollbackManifestForService(
            true, true, false);
    const bool confirmed_round_tail =
        !Horse::ShouldArmRollbackRoundTerminal(4, 4, 4)
        && !Horse::ShouldArmRollbackRoundTerminal(3, 4, 4)
        && Horse::ShouldArmRollbackRoundTerminal(4, 5, 4)
        && !Horse::ShouldCaptureRollbackTerminalCheckpoint(false, true)
        && !Horse::ShouldCaptureRollbackTerminalCheckpoint(true, false)
        && Horse::ShouldCaptureRollbackTerminalCheckpoint(true, true);
    const bool stock_control_cadence =
        Horse::ShouldServiceStockBattleControlPlane(false, 2)
        && Horse::ShouldServiceStockBattleControlPlane(true, 1)
        && !Horse::ShouldServiceStockBattleControlPlane(true, 5)
        && Horse::ShouldServiceStockBattleControlPlane(true, 6)
        && !Horse::ShouldServiceStockBattleControlPlane(true, 7);
    const bool stock_round_transition_ownership =
        Horse::ShouldSuppressStockPerFrameForRoundTransition(true, true)
        && !Horse::ShouldSuppressStockPerFrameForRoundTransition(true, false)
        && !Horse::ShouldSuppressStockPerFrameForRoundTransition(false, true);
    const bool committed_result_boundary =
        Horse::IsRollbackCommittedRoundResultBoundary(1, 3, 1)
        && !Horse::IsRollbackCommittedRoundResultBoundary(2, 2, 0)
        && Horse::IsRollbackCommittedRoundResultBoundary(2, 3, 0)
        && Horse::IsRollbackCommittedRoundResultBoundary(2, 2, 1);
    const bool terminal_control_pair =
        Horse::IsRollbackTerminalControlPairCompatible(1, 2, 0, 0)
        && Horse::IsRollbackTerminalControlPairCompatible(0, 0, 1, 2)
        && Horse::IsRollbackTerminalControlPairCompatible(1, 2, 1, 2)
        && Horse::IsRollbackTerminalControlPairCompatible(1, 0, 0, 0)
        && !Horse::IsRollbackTerminalControlPairCompatible(0, 0, 0, 0)
        && !Horse::IsRollbackTerminalControlPairCompatible(1, 2, 1, 3);
    const bool bilateral_terminal_quiesce =
        !Horse::ShouldQuiesceRollbackRoundTerminal(0, 0)
        && !Horse::ShouldQuiesceRollbackRoundTerminal(1, 0)
        && !Horse::ShouldQuiesceRollbackRoundTerminal(0, 1)
        && Horse::ShouldQuiesceRollbackRoundTerminal(1, 1);
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

    constexpr uint32_t kVerifiedActionMask = 0x0003;
    const std::array<uint64_t, 2> expected_inputs {
        Horse::PackRollbackNativeEngineInput(0x40, 0x00),
        Horse::PackRollbackNativeEngineInput(kVerifiedActionMask, 0x00),
    };
    const std::array<uint32_t, 2> consumed {
        static_cast<uint32_t>(expected_inputs[0]),
        static_cast<uint32_t>(expected_inputs[1]),
    };
    const std::array<uint32_t, 2> consumed_rising {
        static_cast<uint32_t>(expected_inputs[0] >> 32),
        static_cast<uint32_t>(expected_inputs[1] >> 32),
    };

    const auto slot_one_action =
        Horse::EvaluateRollbackNativeInputActionEvidence(
            expected_inputs, consumed,
            consumed_rising, kVerifiedActionMask);
    const Horse::RollbackNativeInputOwnershipEvidence ownership_evidence {
        true, true, true,
        slot_one_action.action_edge_submitted,
        slot_one_action.action_edge_preserved, 0x3,
    };
    const bool ownership_verified =
        Horse::IsRollbackNativeInputOwnershipVerified(ownership_evidence);
    auto missing_pair = ownership_evidence;
    missing_pair.exact_input_pair_published = false;
    auto missing_native_tick = ownership_evidence;
    missing_native_tick.native_per_frame_completed = false;
    auto wrong_engine_source = ownership_evidence;
    wrong_engine_source.engine_source_exact = false;
    auto missing_consumer_action = ownership_evidence;
    missing_consumer_action.consumer_action_edge_submitted = false;
    auto transformed_action_lost = ownership_evidence;
    transformed_action_lost.consumer_action_edge_preserved = false;
    auto missing_consumer = ownership_evidence;
    missing_consumer.consumer_read_mask = 0x1;
    const bool ownership_refusals =
        !Horse::IsRollbackNativeInputOwnershipVerified(missing_pair)
        && !Horse::IsRollbackNativeInputOwnershipVerified(missing_native_tick)
        && !Horse::IsRollbackNativeInputOwnershipVerified(wrong_engine_source)
        && !Horse::IsRollbackNativeInputOwnershipVerified(
            missing_consumer_action)
        && !Horse::IsRollbackNativeInputOwnershipVerified(
            transformed_action_lost)
        && !Horse::IsRollbackNativeInputOwnershipVerified(missing_consumer);

    const std::array<uint32_t, 2> no_consumed_input {};
    const std::array<uint64_t, 2> held_action {
        Horse::PackRollbackNativeEngineInput(kVerifiedActionMask,
            kVerifiedActionMask),
        0,
    };
    const std::array<uint64_t, 2> disjoint_current_and_edge {
        static_cast<uint64_t>(0x0001)
            | (static_cast<uint64_t>(0x0002) << 32),
        0,
    };
    const std::array<uint64_t, 2> partial_edge {
        Horse::PackRollbackNativeEngineInput(kVerifiedActionMask, 0x0002),
        0,
    };
    const std::array<uint64_t, 2> single_action_slot_zero {
        Horse::PackRollbackNativeEngineInput(0x0002, 0),
        0,
    };
    const std::array<uint32_t, 2> complete_consumed {
        kVerifiedActionMask, 0,
    };
    const std::array<uint32_t, 2> complete_edge_consumed {
        kVerifiedActionMask, 0,
    };
    const std::array<uint32_t, 2> partial_edge_consumed {0x0001, 0};
    const auto held_action_evidence =
        Horse::EvaluateRollbackNativeInputActionEvidence(
            held_action, complete_consumed, no_consumed_input,
            kVerifiedActionMask);
    const auto disjoint_action_evidence =
        Horse::EvaluateRollbackNativeInputActionEvidence(
            disjoint_current_and_edge, complete_consumed,
            complete_edge_consumed,
            kVerifiedActionMask);
    const auto partial_edge_evidence =
        Horse::EvaluateRollbackNativeInputActionEvidence(
            partial_edge, complete_consumed, complete_edge_consumed,
            kVerifiedActionMask);
    const auto missing_consumer_edge_evidence =
        Horse::EvaluateRollbackNativeInputActionEvidence(
            single_action_slot_zero, complete_consumed,
            partial_edge_consumed, kVerifiedActionMask);
    const auto slot_zero_action =
        Horse::EvaluateRollbackNativeInputActionEvidence(
            single_action_slot_zero, complete_consumed,
            complete_edge_consumed, kVerifiedActionMask);
    const bool exact_action_evidence =
        !held_action_evidence.action_edge_submitted
        && !held_action_evidence.action_edge_preserved
        && !disjoint_action_evidence.action_edge_submitted
        && partial_edge_evidence.action_edge_submitted
        && partial_edge_evidence.action_edge_preserved
        && missing_consumer_edge_evidence.action_edge_submitted
        && !missing_consumer_edge_evidence.action_edge_preserved
        && slot_zero_action.action_edge_submitted
        && slot_zero_action.action_edge_preserved;

    std::printf(
        "rollback production active-guard self-test passed "
        "activation_service=%d accepted_manifest=%d confirmed_round_tail=%d stock_cadence=%d round_transition=%d committed_result=%d terminal_control=%d presence=%d epoch=%d udp_generation=%d "
        "peer_loss=%d schema=%d "
        "bilateral_terminal=%d diagnostic_release=%d ownership=%d ownership_refusals=%d exact_action=%d\n",
        activation_service_gate ? 1 : 0,
        accepted_manifest_service ? 1 : 0,
        confirmed_round_tail ? 1 : 0,
        stock_control_cadence ? 1 : 0,
        stock_round_transition_ownership ? 1 : 0,
        committed_result_boundary ? 1 : 0,
        terminal_control_pair ? 1 : 0,
        presence_rejected ? 1 : 0,
        epoch_rejected ? 1 : 0,
        generation_rejected ? 1 : 0,
        peer_rejected ? 1 : 0,
        schema_rejected ? 1 : 0,
        bilateral_terminal_quiesce ? 1 : 0,
        diagnostic_release_gate ? 1 : 0,
        ownership_verified ? 1 : 0,
        ownership_refusals ? 1 : 0,
        exact_action_evidence ? 1 : 0);
    return activation_service_gate
            && accepted_manifest_service
            && confirmed_round_tail
            && stock_control_cadence
            && stock_round_transition_ownership
            && committed_result_boundary
            && terminal_control_pair
            && bilateral_terminal_quiesce
            && diagnostic_release_gate
            && presence_rejected
            && epoch_rejected && generation_rejected
            && peer_rejected && schema_rejected
            && ownership_verified && ownership_refusals
            && exact_action_evidence
        ? 0 : 2;
}
