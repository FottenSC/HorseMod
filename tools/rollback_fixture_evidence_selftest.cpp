#include "../HorseMod/horselib/RollbackFixtureEvidence.hpp"

#include <array>
#include <cstdio>

int main()
{
    if (!Horse::RollbackFixtureRequiresFirstLoadEffectWitness(true, false)
        || Horse::RollbackFixtureRequiresFirstLoadEffectWitness(false, false)
        || Horse::RollbackFixtureRequiresFirstLoadEffectWitness(true, true)
        || Horse::RollbackFixtureRequiresFirstLoadEffectWitness(false, true))
    {
        std::fprintf(
            stderr,
            "rollback fixture evidence self-test failed witness policy\n");
        return 1;
    }

    Horse::RollbackDeterministicInputConfig config {};
    config.enabled = true;
    std::array<uint64_t, Horse::kRollbackSideEffectTypeCount> counts {};
    std::array<uint64_t, Horse::kRollbackSideEffectTypeCount> digests {};
    std::array<uint32_t, Horse::kRollbackSideEffectTypeCount> first {};
    counts[0] = 1;
    digests[0] = 0xA0;
    first[0] = 121;
    const std::array<uint32_t, 2> predicted {0, 0};
    const std::array<uint32_t, 2> authored {
        Horse::kRollbackFixtureBasicActionInput, 0};

    Horse::RollbackFixtureCorrectionGate correction {};
    // Normal ordering: held frames 120..125 complete, packets release, then
    // Gekko emits Load(119) while the speculative high-water is still 125.
    const bool release_required = !correction.arm(
        config, 1, 7, 119, 124, predicted, first, counts, digests);
    const bool provisional = correction.arm(
            config, 1, 7, 119, 125, predicted, first, counts, digests)
        && correction.candidate().valid
        && correction.candidate().affected_frame == 120;
    const bool fixture_promoted = provisional
        && correction.observe_advance(
            7, 120, true, authored, authored, 0)
            == Horse::RollbackFixtureCorrectionAction::Promoted
        && correction.candidate().load_frame == 119
        && correction.candidate().discarded_count[0] == 1;
    correction.reset();
    const bool unrelated_rejected = correction.arm(
            config, 1, 7, 119, 125, predicted, first, counts, digests)
        && correction.observe_advance(
            7, 120, true, predicted, authored, 0)
            == Horse::RollbackFixtureCorrectionAction::RejectedUnrelated
        && !correction.candidate().valid;
    const bool later_high_water_valid = correction.arm(
            config, 1, 7, 119, 126, predicted, first, counts, digests)
        && correction.observe_advance(
            7, 120, true, authored, authored, 0)
            == Horse::RollbackFixtureCorrectionAction::Promoted;
    correction.reset();
    const std::array<uint32_t, 2> replay_authored {8, 4};
    const bool replay_authored_promoted = correction.arm(
            config, 1, 8, 120, 126, predicted, first, counts, digests)
        && correction.candidate().affected_frame == 121
        && correction.observe_advance(
            8, 121, true, replay_authored, replay_authored, 0)
            == Horse::RollbackFixtureCorrectionAction::Promoted;

    Horse::RollbackFixtureCheckpointGate fast_peer {};
    Horse::RollbackFixtureCheckpointGate slow_peer {};
    const bool configured = fast_peer.configure(9, 150)
        && slow_peer.configure(9, 150)
        && fast_peer.observe_frontier(9, 149)
            == Horse::RollbackFixtureCheckpointAction::PassThrough
        && slow_peer.observe_frontier(9, 148)
            == Horse::RollbackFixtureCheckpointAction::PassThrough;
    const bool peer_skew_stops_exactly = configured
        && fast_peer.observe_frontier(9, 150)
            == Horse::RollbackFixtureCheckpointAction::StopAtTarget
        && fast_peer.awaiting_confirmation()
        && slow_peer.observe_frontier(9, 149)
            == Horse::RollbackFixtureCheckpointAction::PassThrough
        && slow_peer.observe_frontier(9, 150)
            == Horse::RollbackFixtureCheckpointAction::StopAtTarget
        && slow_peer.awaiting_confirmation();
    const bool ordered_capture = peer_skew_stops_exactly
        && fast_peer.begin_confirmation(9, 150)
        && !fast_peer.checkpoint_captured(9, 150)
        && fast_peer.commit_validated(9, 150)
        && fast_peer.checkpoint_captured(9, 150)
        && fast_peer.captured()
        && !fast_peer.awaiting_confirmation()
        && fast_peer.observe_frontier(9, 151)
            == Horse::RollbackFixtureCheckpointAction::PassThrough;
    const bool slow_peer_ordered = slow_peer.begin_confirmation(9, 150)
        && slow_peer.commit_validated(9, 150)
        && slow_peer.checkpoint_captured(9, 150)
        && !slow_peer.awaiting_confirmation()
        && slow_peer.observe_frontier(9, 151)
            == Horse::RollbackFixtureCheckpointAction::PassThrough;
    Horse::RollbackFixtureCheckpointGate skipped {};
    const bool skipped_rejected = skipped.configure(10, 150)
        && skipped.observe_frontier(10, 149)
            == Horse::RollbackFixtureCheckpointAction::PassThrough
        && skipped.observe_frontier(10, 151)
            == Horse::RollbackFixtureCheckpointAction::Reject;

    const bool presentation_checkpoint_policy =
        Horse::RollbackFixturePresentationCheckpointRequired(
            true, true, false)
        && !Horse::RollbackFixturePresentationCheckpointRequired(
            true, true, true)
        && !Horse::RollbackFixturePresentationCheckpointRequired(
            false, true, false)
        && !Horse::RollbackFixturePresentationCheckpointRequired(
            true, false, false);

    const bool ok = release_required && provisional && unrelated_rejected
        && fixture_promoted && replay_authored_promoted
        && later_high_water_valid
        && ordered_capture && slow_peer_ordered && skipped_rejected
        && presentation_checkpoint_policy;
    std::printf(
        "rollback fixture-evidence self-test %s causality=%d exact=%d "
        "peer_skew=%d resume=%d\n",
        ok ? "passed" : "failed",
        (release_required && unrelated_rejected && fixture_promoted
            && later_high_water_valid
            && replay_authored_promoted) ? 1 : 0,
        ordered_capture ? 1 : 0,
        peer_skew_stops_exactly ? 1 : 0,
        (slow_peer_ordered && skipped_rejected) ? 1 : 0);
    return ok ? 0 : 1;
}
