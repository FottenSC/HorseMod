#include "../HorseMod/horselib/RollbackReplaySelectionAvailabilityPolicy.hpp"

#include <cstdio>

int main()
{
    using namespace Horse;
    RollbackReplaySelectionAvailabilityPolicyInput input {};
    input.requested = true;
    input.stock_online_local_lab = true;
    input.replay_input_enabled = true;
    input.expected_selection_hash = 0x12345678u;
    input.requested_selection_hash = input.expected_selection_hash;
    input.left_character_code = "007";
    input.right_character_code = "023";
    input.stage_code = "STG011_R_V";

    const bool waits_for_bilateral_contract =
        EvaluateRollbackReplaySelectionAvailability(input)
        == RollbackReplaySelectionAvailabilityDecision::
            AwaitingPeerContract;
    input.peer_contract_ready = true;
    const bool exact_replay_selection_accepted =
        EvaluateRollbackReplaySelectionAvailability(input)
        == RollbackReplaySelectionAvailabilityDecision::Active;

    auto production = input;
    production.stock_online_local_lab = false;
    const bool production_rejected =
        EvaluateRollbackReplaySelectionAvailability(production)
        == RollbackReplaySelectionAvailabilityDecision::InvalidScope;

    auto native_input = input;
    native_input.replay_input_enabled = false;
    const bool native_input_rejected =
        EvaluateRollbackReplaySelectionAvailability(native_input)
        == RollbackReplaySelectionAvailabilityDecision::InvalidScope;

    auto observed_selection = input;
    observed_selection.bind_observed_stock_selection = true;
    const bool observed_selection_rejected =
        EvaluateRollbackReplaySelectionAvailability(observed_selection)
        == RollbackReplaySelectionAvailabilityDecision::InvalidScope;

    auto mismatch = input;
    ++mismatch.requested_selection_hash;
    const bool hash_mismatch_rejected =
        EvaluateRollbackReplaySelectionAvailability(mismatch)
        == RollbackReplaySelectionAvailabilityDecision::SelectionMismatch;

    auto malformed_character = input;
    malformed_character.right_character_code = "23";
    const bool malformed_character_rejected =
        EvaluateRollbackReplaySelectionAvailability(malformed_character)
        == RollbackReplaySelectionAvailabilityDecision::InvalidScope;

    auto malformed_stage = input;
    malformed_stage.stage_code = "../STG011_R_V";
    const bool malformed_stage_rejected =
        EvaluateRollbackReplaySelectionAvailability(malformed_stage)
        == RollbackReplaySelectionAvailabilityDecision::InvalidScope;

    auto lowercase_stage = input;
    lowercase_stage.stage_code = "STG011_r_v";
    const bool lowercase_stage_rejected =
        EvaluateRollbackReplaySelectionAvailability(lowercase_stage)
        == RollbackReplaySelectionAvailabilityDecision::InvalidScope;

    auto completed = input;
    completed.setup_complete = true;
    const bool post_setup_disabled =
        EvaluateRollbackReplaySelectionAvailability(completed)
        == RollbackReplaySelectionAvailabilityDecision::SetupComplete;

    auto disabled = input;
    disabled.requested = false;
    const bool ordinary_run_disabled =
        EvaluateRollbackReplaySelectionAvailability(disabled)
        == RollbackReplaySelectionAvailabilityDecision::Disabled;

    if (!waits_for_bilateral_contract
        || !exact_replay_selection_accepted
        || !production_rejected
        || !native_input_rejected
        || !observed_selection_rejected
        || !hash_mismatch_rejected
        || !malformed_character_rejected
        || !malformed_stage_rejected
        || !lowercase_stage_rejected
        || !post_setup_disabled
        || !ordinary_run_disabled)
    {
        std::fprintf(stderr,
            "rollback replay-selection availability policy self-test failed\n");
        return 1;
    }

    std::puts(
        "rollback replay-selection availability policy self-test passed");
    return 0;
}
