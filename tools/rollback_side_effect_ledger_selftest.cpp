#include "../HorseMod/horselib/RollbackSideEffectLedger.hpp"
#include "../HorseMod/horselib/RollbackCameraPresentation.hpp"
#include "../HorseMod/horselib/RollbackProductionSummary.hpp"

#include <cmath>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    bool commit(const Horse::RollbackSideEffectEvent&, void* context) noexcept
    {
        ++*static_cast<uint32_t*>(context);
        return true;
    }

    struct OrderedCommitContext
    {
        std::array<uint32_t, 8> frames {};
        size_t count {0};
    };

    bool commit_ordered(
        const Horse::RollbackSideEffectEvent& event,
        void* context) noexcept
    {
        auto& ordered = *static_cast<OrderedCommitContext*>(context);
        if (ordered.count < ordered.frames.size())
            ordered.frames[ordered.count++] = event.frame;
        return true;
    }

    struct FailingCommitContext
    {
        uint32_t calls {0};
        uint32_t successes {0};
        uint32_t fail_on_call {0};
    };

    bool commit_with_failure(
        const Horse::RollbackSideEffectEvent&,
        void* context) noexcept
    {
        auto& state = *static_cast<FailingCommitContext*>(context);
        ++state.calls;
        if (state.calls == state.fail_on_call) return false;
        ++state.successes;
        return true;
    }

}

int main()
{
    const bool native_correction_bypass =
        Horse::RollbackNativeCorrectionBypassesSideEffectLedger(
            true, Horse::RollbackSideEffectType::ConfirmedTransition)
        && !Horse::RollbackNativeCorrectionBypassesSideEffectLedger(
            true, Horse::RollbackSideEffectType::Audio)
        && !Horse::RollbackNativeCorrectionBypassesSideEffectLedger(
            true, Horse::RollbackSideEffectType::Vfx)
        && !Horse::RollbackNativeCorrectionBypassesSideEffectLedger(
            true, Horse::RollbackSideEffectType::Camera)
        && !Horse::RollbackNativeCorrectionBypassesSideEffectLedger(
            false, Horse::RollbackSideEffectType::ConfirmedTransition);
    const bool round_lifecycle_enable =
        Horse::RollbackSideEffectsEnabledForOwnedRound(true, true, false)
        && !Horse::RollbackSideEffectsEnabledForOwnedRound(
            false, true, false)
        && !Horse::RollbackSideEffectsEnabledForOwnedRound(
            true, false, false)
        && !Horse::RollbackSideEffectsEnabledForOwnedRound(
            true, true, true);
    Horse::RollbackSideEffectLedger<8, 16> ledger {};
    const uint32_t payload = 0x1234;
    Horse::RollbackSideEffectLedger<2, 4> payload_ledger {};
    std::array<uint8_t, 0x54> vfx_payload {};
    std::array<uint8_t, 97> oversized_payload {};
    const bool vfx_payload_boundary = payload_ledger.enqueue(
            1, 1, Horse::RollbackSideEffectType::Vfx, 1,
            vfx_payload.data(), static_cast<uint16_t>(vfx_payload.size()))
        && !payload_ledger.enqueue(
            1, 2, Horse::RollbackSideEffectType::Vfx, 2,
            oversized_payload.data(),
            static_cast<uint16_t>(oversized_payload.size()))
        && !payload_ledger.report().ok
        && std::strcmp(payload_ledger.report().failure,
            "invalid-side-effect") == 0;
    const bool q0 = ledger.enqueue(
        9, 10, Horse::RollbackSideEffectType::Audio, 1,
        &payload, sizeof(payload));
    const bool duplicate = ledger.enqueue(
        9, 10, Horse::RollbackSideEffectType::Audio, 1,
        &payload, sizeof(payload));
    const bool q1 = ledger.enqueue(
        9, 11, Horse::RollbackSideEffectType::Vfx, 2,
        &payload, sizeof(payload));
    const bool camera_q = ledger.enqueue(
        9, 12, Horse::RollbackSideEffectType::Camera, 5,
        &payload, sizeof(payload));
    ledger.rollback_from(9, 11);
    const bool discarded = ledger.pending() == 1;
    const bool camera_discarded = ledger.report().discarded_by_type[
        static_cast<size_t>(Horse::RollbackSideEffectType::Camera)] == 1;
    const bool lane_discarded_from = ledger.report().discarded_by_lane[
        static_cast<size_t>(Horse::RollbackSideEffectType::Camera)][0] == 1
        && ledger.report().discarded_by_lane[
            static_cast<size_t>(Horse::RollbackSideEffectType::Vfx)][0] == 1;

    uint32_t commits = 0;
    const bool confirmed = ledger.confirm_through(9, 10, &commit, &commits);
    const bool once = confirmed && commits == 1 && ledger.pending() == 0;
    const bool replay_duplicate = ledger.enqueue(
        9, 10, Horse::RollbackSideEffectType::Audio, 1,
        &payload, sizeof(payload));
    (void)ledger.confirm_through(9, 10, &commit, &commits);
    const bool still_once = replay_duplicate && commits == 1;
    const size_t audio_type = static_cast<size_t>(
        Horse::RollbackSideEffectType::Audio);
    const uint64_t committed_audio_digest =
        ledger.report().committed_digest_by_type[audio_type];
    Horse::RollbackSideEffectLedger<8, 16> digest_ledger {};
    uint32_t digest_commits = 0;
    const bool digest_q = digest_ledger.enqueue(
        9, 10, Horse::RollbackSideEffectType::Audio, 1,
        &payload, sizeof(payload));
    const bool digest_committed = digest_ledger.confirm_through(
        9, 10, &commit, &digest_commits);
    const bool digest_stable = committed_audio_digest != 0
        && digest_q && digest_committed && digest_commits == 1
        && digest_ledger.report().committed_digest_by_type[audio_type]
            == committed_audio_digest;

    Horse::RollbackSideEffectLedger<4, 4> commit_failure_ledger {};
    FailingCommitContext commit_failure {0, 0, 2};
    const bool commit_failure_accounting =
        commit_failure_ledger.enqueue(
            90, 4, Horse::RollbackSideEffectType::Audio, 401,
            &payload, sizeof(payload))
        && commit_failure_ledger.enqueue(
            90, 4, Horse::RollbackSideEffectType::Audio, 402,
            &payload, sizeof(payload))
        && !commit_failure_ledger.confirm_through(
            90, 4, &commit_with_failure, &commit_failure)
        && commit_failure.calls == 2
        && commit_failure.successes == 1
        && commit_failure_ledger.report().committed == 1
        && commit_failure_ledger.report().committed_by_type[audio_type] == 1
        && !commit_failure_ledger.report().ok
        && std::strcmp(commit_failure_ledger.report().failure,
            "side-effect-commit-failed") == 0;

    Horse::RollbackSideEffectLedger<2, 4> wrap_order_ledger {};
    OrderedCommitContext wrap_order {};
    const bool wrap_order_queued = wrap_order_ledger.enqueue(
            91, 64, Horse::RollbackSideEffectType::Audio, 64,
            &payload, sizeof(payload))
        && wrap_order_ledger.enqueue(
            91, 63, Horse::RollbackSideEffectType::Audio, 63,
            &payload, sizeof(payload))
        && wrap_order_ledger.enqueue(
            91, 65, Horse::RollbackSideEffectType::Audio, 65,
            &payload, sizeof(payload));
    const bool wrap_order_committed = wrap_order_queued
        && wrap_order_ledger.confirm_through(
            91, 65, &commit_ordered, &wrap_order)
        && wrap_order.count == 3
        && wrap_order.frames[0] == 63
        && wrap_order.frames[1] == 64
        && wrap_order.frames[2] == 65;
    const bool exact_confirmation_repeat =
        wrap_order_ledger.confirm_through(
            91, 65, &commit_ordered, &wrap_order)
        && wrap_order.count == 3;
    const bool backward_confirmation_rejected =
        !wrap_order_ledger.confirm_through(
            91, 64, &commit_ordered, &wrap_order)
        && std::strcmp(wrap_order_ledger.report().failure,
            "backward-side-effect-confirmation") == 0;

    Horse::RollbackSideEffectLedger<2, 4> late_event_ledger {};
    uint32_t late_event_commits = 0;
    const bool late_original = late_event_ledger.enqueue(
            92, 7, Horse::RollbackSideEffectType::Audio, 700,
            &payload, sizeof(payload))
        && late_event_ledger.confirm_through(
            92, 7, &commit, &late_event_commits);
    const bool committed_duplicate_allowed = late_original
        && late_event_ledger.enqueue(
            92, 7, Horse::RollbackSideEffectType::Audio, 700,
            &payload, sizeof(payload));
    const bool new_late_event_rejected =
        !late_event_ledger.enqueue(
            92, 7, Horse::RollbackSideEffectType::Audio, 701,
            &payload, sizeof(payload))
        && std::strcmp(late_event_ledger.report().failure,
            "side-effect-after-confirmation") == 0;

    Horse::RollbackSideEffectLedger<2, 4> mixed_epoch_ledger {};
    uint32_t mixed_epoch_commits = 0;
    const bool mixed_epoch_confirmation_rejected =
        mixed_epoch_ledger.enqueue(
            93, 7, Horse::RollbackSideEffectType::Audio, 9307,
            &payload, sizeof(payload))
        && mixed_epoch_ledger.enqueue(
            94, 8, Horse::RollbackSideEffectType::Audio, 9408,
            &payload, sizeof(payload))
        && !mixed_epoch_ledger.confirm_through(
            94, 8, &commit, &mixed_epoch_commits)
        && mixed_epoch_commits == 0
        && std::strcmp(mixed_epoch_ledger.report().failure,
            "side-effect-epoch-transition-with-pending") == 0;

    constexpr uint8_t state_edge_lane = 3;
    constexpr uint8_t positional_lane = 1;
    Horse::RollbackSideEffectOrdinalDomains ordinal_order_a {};
    Horse::RollbackSideEffectOrdinalDomains ordinal_order_b {};
    const uint32_t audio_ordinal_a = ordinal_order_a.next(
        Horse::RollbackSideEffectType::Audio, 0);
    const uint32_t positional_ordinal_a = ordinal_order_a.next(
        Horse::RollbackSideEffectType::Vfx, positional_lane);
    const uint32_t state_edge_ordinal_a = ordinal_order_a.next(
        Horse::RollbackSideEffectType::Vfx, state_edge_lane);
    const uint32_t state_edge_ordinal_b = ordinal_order_b.next(
        Horse::RollbackSideEffectType::Vfx, state_edge_lane);
    const uint32_t positional_ordinal_b = ordinal_order_b.next(
        Horse::RollbackSideEffectType::Vfx, positional_lane);
    const uint32_t audio_ordinal_b = ordinal_order_b.next(
        Horse::RollbackSideEffectType::Audio, 0);
    const bool ordinal_domains_isolated = audio_ordinal_a == 0
        && audio_ordinal_b == 0
        && positional_ordinal_a == 0 && positional_ordinal_b == 0
        && state_edge_ordinal_a == 0 && state_edge_ordinal_b == 0
        && ordinal_order_a.next(
            Horse::RollbackSideEffectType::Vfx, state_edge_lane) == 1;
    ordinal_order_a.reset();
    const bool ordinal_reset = ordinal_order_a.next(
        Horse::RollbackSideEffectType::Vfx, state_edge_lane) == 0;
    const uint32_t state_edge_payload = 0xA1B2C3D4u;
    const uint32_t changed_state_edge_payload = 0xA1B2C3D5u;
    const uint32_t positional_payload = 0x55667788u;
    const uint64_t state_edge_key =
        Horse::ComputeRollbackSideEffectIdentityKey(
            Horse::RollbackSideEffectType::Vfx, state_edge_lane,
            &state_edge_payload, sizeof(state_edge_payload), 0);
    const uint64_t state_edge_key_repeat =
        Horse::ComputeRollbackSideEffectIdentityKey(
            Horse::RollbackSideEffectType::Vfx, state_edge_lane,
            &state_edge_payload, sizeof(state_edge_payload), 0);
    const uint64_t changed_state_edge_key =
        Horse::ComputeRollbackSideEffectIdentityKey(
            Horse::RollbackSideEffectType::Vfx, state_edge_lane,
            &changed_state_edge_payload,
            sizeof(changed_state_edge_payload), 0);
    const uint64_t positional_key =
        Horse::ComputeRollbackSideEffectIdentityKey(
            Horse::RollbackSideEffectType::Vfx, positional_lane,
            &positional_payload, sizeof(positional_payload), 0);
    Horse::RollbackSideEffectLedger<8, 16> lane_order_a {};
    Horse::RollbackSideEffectLedger<8, 16> lane_order_b {};
    Horse::RollbackSideEffectLedger<8, 16> lane_changed {};
    const bool lane_events_queued =
        lane_order_a.enqueue(12, 30, Horse::RollbackSideEffectType::Vfx,
            positional_key, &positional_payload,
            sizeof(positional_payload), positional_lane)
        && lane_order_a.enqueue(12, 30,
            Horse::RollbackSideEffectType::Vfx, state_edge_key,
            &state_edge_payload, sizeof(state_edge_payload), state_edge_lane)
        && lane_order_a.enqueue(12, 31,
            Horse::RollbackSideEffectType::Vfx,
            Horse::ComputeRollbackSideEffectIdentityKey(
                Horse::RollbackSideEffectType::Vfx, state_edge_lane,
                &state_edge_payload, sizeof(state_edge_payload), 1),
            &state_edge_payload, sizeof(state_edge_payload), state_edge_lane)
        && lane_order_b.enqueue(12, 30,
            Horse::RollbackSideEffectType::Vfx, state_edge_key_repeat,
            &state_edge_payload, sizeof(state_edge_payload), state_edge_lane)
        && lane_order_b.enqueue(12, 30,
            Horse::RollbackSideEffectType::Vfx, positional_key,
            &positional_payload, sizeof(positional_payload), positional_lane)
        && lane_changed.enqueue(12, 30,
            Horse::RollbackSideEffectType::Vfx, changed_state_edge_key,
            &changed_state_edge_payload,
            sizeof(changed_state_edge_payload), state_edge_lane);
    Horse::RollbackSideEffectConfirmedCheckpoint lane_projection_a {};
    Horse::RollbackSideEffectConfirmedCheckpoint lane_projection_b {};
    Horse::RollbackSideEffectConfirmedCheckpoint lane_projection_changed {};
    const size_t vfx_lane_type = static_cast<size_t>(
        Horse::RollbackSideEffectType::Vfx);
    const bool lane_identity_isolated = lane_events_queued
        && state_edge_key == state_edge_key_repeat
        && state_edge_key != changed_state_edge_key
        && lane_order_a.project_confirmed_through(
            12, 30, lane_projection_a)
        && lane_order_b.project_confirmed_through(
            12, 30, lane_projection_b)
        && lane_changed.project_confirmed_through(
            12, 30, lane_projection_changed)
        && lane_projection_a.committed_by_lane
            [vfx_lane_type][state_edge_lane] == 1
        && lane_projection_b.committed_by_lane
            [vfx_lane_type][state_edge_lane] == 1
        && lane_projection_a.committed_digest_by_lane
            [vfx_lane_type][state_edge_lane]
            == lane_projection_b.committed_digest_by_lane
                [vfx_lane_type][state_edge_lane]
        && lane_projection_a.committed_digest_by_lane
            [vfx_lane_type][state_edge_lane]
            != lane_projection_changed.committed_digest_by_lane
                [vfx_lane_type][state_edge_lane]
        && lane_projection_a.committed_by_lane
            [vfx_lane_type][positional_lane] == 1;

    Horse::RollbackSideEffectLedger<8, 16> checkpoint_ledger {};
    uint32_t checkpoint_commits = 0;
    const size_t camera_type = static_cast<size_t>(
        Horse::RollbackSideEffectType::Camera);
    const bool checkpoint_enqueued = checkpoint_ledger.enqueue(
            10, 120, Horse::RollbackSideEffectType::Camera, 10,
            &payload, sizeof(payload))
        && checkpoint_ledger.enqueue(
            10, 121, Horse::RollbackSideEffectType::Camera, 11,
            &payload, sizeof(payload));
    const bool checkpoint_prefix_committed =
        checkpoint_ledger.confirm_through(
            10, 120, &commit, &checkpoint_commits);
    Horse::RollbackSideEffectConfirmedCheckpoint projected {};
    const bool checkpoint_projected =
        checkpoint_ledger.project_confirmed_through(10, 121, projected)
        && projected.valid && projected.epoch == 10
        && projected.frame == 121
        && projected.queued_by_type[camera_type] == 2
        && projected.discarded_by_type[camera_type] == 0
        && projected.committed_by_type[camera_type] == 2
        && projected.committed_digest_by_type[camera_type] != 0;
    const bool checkpoint_committed = checkpoint_ledger.confirm_through(
        10, 121, &commit, &checkpoint_commits);
    const bool checkpoint_matches_commit = checkpoint_committed
        && checkpoint_ledger.report().committed_by_type[camera_type]
            == projected.committed_by_type[camera_type]
        && checkpoint_ledger.report().committed_digest_by_type[camera_type]
            == projected.committed_digest_by_type[camera_type];
    (void)checkpoint_ledger.confirm_through(
        10, 122, &commit, &checkpoint_commits);
    Horse::RollbackSideEffectConfirmedCheckpoint stale_projection {};
    const bool checkpoint_rejects_stale =
        !checkpoint_ledger.project_confirmed_through(
            10, 121, stale_projection)
        && !stale_projection.valid;
    const bool checkpoint_projection = checkpoint_enqueued
        && checkpoint_prefix_committed && checkpoint_projected
        && checkpoint_matches_commit && checkpoint_rejects_stale;

    Horse::RollbackSideEffectLedger<8, 16> load_ledger {};
    const bool load_q0 = load_ledger.enqueue(
        10, 20, Horse::RollbackSideEffectType::Audio, 3,
        &payload, sizeof(payload));
    const bool load_q1 = load_ledger.enqueue(
        10, 21, Horse::RollbackSideEffectType::Vfx, 4,
        &payload, sizeof(payload), 2);
    const bool load_q2 = load_ledger.enqueue(
        10, 22, Horse::RollbackSideEffectType::Audio, 5,
        &payload, sizeof(payload));
    uint32_t first_after = 0;
    uint32_t last_after = 0;
    uint32_t count_after = 0;
    const bool range_after = load_ledger.pending_frame_range_after(
        10, Horse::RollbackSideEffectType::Audio, 20,
        first_after, last_after, count_after)
        && first_after == 22 && last_after == 22 && count_after == 1;
    uint32_t witness_count = 0;
    uint64_t witness_digest = 0;
    const bool witness_after = load_ledger.pending_witness_after(
            10, Horse::RollbackSideEffectType::Audio, 20,
            witness_count, witness_digest)
        && witness_count == 1
        && witness_digest == Horse::ComputeRollbackSideEffectEventDigest(
            22, Horse::RollbackSideEffectType::Audio, 0, 5,
            &payload, sizeof(payload));
    load_ledger.rollback_after(10, 20);
    Horse::RollbackSideEffectConfirmedCheckpoint load_projection {};
    const size_t vfx_type = static_cast<size_t>(
        Horse::RollbackSideEffectType::Vfx);
    const bool load_counts_projected =
        load_ledger.project_confirmed_through(10, 20, load_projection)
        && load_projection.queued_by_type[audio_type] == 2
        && load_projection.queued_by_type[vfx_type] == 1
        && load_projection.discarded_by_type[audio_type] == 1
        && load_projection.discarded_by_type[vfx_type] == 1
        && load_projection.discarded_by_lane[audio_type][0] == 1
        && load_projection.discarded_by_lane[vfx_type][2] == 1
        && load_projection.committed_by_type[audio_type] == 1
        && load_projection.committed_by_type[vfx_type] == 0;
    uint32_t load_commits = 0;
    const bool loaded_frame_kept = load_q0 && load_q1 && load_q2
        && load_ledger.pending() == 1
        && load_ledger.confirm_through(
            10, 20, &commit, &load_commits)
        && load_commits == 1;

    Horse::RollbackSideEffectLedger<8, 16> epoch_ledger {};
    const uint32_t epoch_payload_a = 0xAA;
    const uint32_t epoch_payload_b = 0xBB;
    const bool epoch_queued = epoch_ledger.enqueue(
            10, 21, Horse::RollbackSideEffectType::Audio, 21,
            &epoch_payload_a, sizeof(epoch_payload_a))
        && epoch_ledger.enqueue(
            11, 22, Horse::RollbackSideEffectType::Audio, 22,
            &epoch_payload_b, sizeof(epoch_payload_b));
    uint32_t epoch_count = 0;
    uint64_t epoch_digest = 0;
    uint32_t epoch_first = 0;
    uint32_t epoch_last = 0;
    uint32_t epoch_range_count = 0;
    const bool epoch_witness_scoped = epoch_queued
        && epoch_ledger.pending_witness_after(
            10, Horse::RollbackSideEffectType::Audio, 20,
            epoch_count, epoch_digest)
        && epoch_count == 1
        && epoch_digest == Horse::ComputeRollbackSideEffectEventDigest(
            21, Horse::RollbackSideEffectType::Audio, 0, 21,
            &epoch_payload_a, sizeof(epoch_payload_a))
        && epoch_ledger.pending_frame_range_after(
            10, Horse::RollbackSideEffectType::Audio, 20,
            epoch_first, epoch_last, epoch_range_count)
        && epoch_first == 21 && epoch_last == 21
        && epoch_range_count == 1;
    epoch_ledger.rollback_after(10, 20);
    const bool epoch_rollback_scoped = epoch_ledger.pending() == 1
        && epoch_ledger.report().discarded_by_type[audio_type] == 1;

    std::array<uint64_t, Horse::kRollbackSideEffectTypeCount>
        no_discards {};
    std::array<uint64_t, Horse::kRollbackSideEffectTypeCount>
        no_discard_digests {};
    Horse::RollbackFixtureEffectWitness fixture_witness {};
    const bool corrected_only_witness = fixture_witness.begin_load(
            7, 0, no_discards, no_discard_digests)
        && fixture_witness.state().valid
        && fixture_witness.state().load_frame == 0;
    auto later_discards = no_discards;
    auto later_digests = no_discard_digests;
    later_discards[vfx_type] = 1;
    later_digests[vfx_type] = 0x5151;
    const bool continuous_load_preserves_first = fixture_witness.begin_load(
            7, 9, later_discards, later_digests)
        && fixture_witness.state().load_frame == 0
        && fixture_witness.state().discarded_count[vfx_type] == 0
        && !fixture_witness.begin_load(
            8, 9, later_discards, later_digests);
    Horse::RollbackFixtureEffectWitness invalid_witness {};
    auto invalid_digest = no_discard_digests;
    invalid_digest[audio_type] = 1;
    const bool invalid_witness_rejected = !invalid_witness.begin_load(
        7, 1, no_discards, invalid_digest);
    fixture_witness.reset();
    const bool witness_reset = !fixture_witness.state().valid;

    Horse::RollbackSideEffectLedger<16, 64> fixture_ledger {};
    Horse::RollbackSideEffectLedger<16, 64> sender_ledger {};
    bool fixture_enqueued = true;
    for (uint32_t pass = 0; pass < 3; ++pass)
    {
        for (size_t type = 0;
             type < Horse::kRollbackSideEffectTypeCount; ++type)
        {
            fixture_enqueued = fixture_enqueued && fixture_ledger.enqueue(
                17, 120u + static_cast<uint32_t>(type),
                static_cast<Horse::RollbackSideEffectType>(type),
                0x1700u + type, &payload, sizeof(payload), 0, 1);
        }
        if (pass < 2) fixture_ledger.rollback_after(17, 119);
    }
    // An unrelated correction after the evidence interval must not alter the
    // fixture-tagged conservation counters.
    const bool unrelated_correction = fixture_ledger.enqueue(
            17, 141, Horse::RollbackSideEffectType::Audio, 0x1741,
            &payload, sizeof(payload), 0, 0)
        && (fixture_ledger.rollback_after(17, 140), true);
    for (size_t type = 0;
         type < Horse::kRollbackSideEffectTypeCount; ++type)
    {
        fixture_enqueued = fixture_enqueued && sender_ledger.enqueue(
            17, 120u + static_cast<uint32_t>(type),
            static_cast<Horse::RollbackSideEffectType>(type),
            0x1700u + type, &payload, sizeof(payload));
    }
    uint32_t fixture_commits = 0;
    uint32_t sender_commits = 0;
    Horse::RollbackSideEffectConfirmedCheckpoint sender_before_target {};
    const bool exact_target_committed = fixture_enqueued
        && unrelated_correction
        && sender_ledger.confirm_through(
            17, 149, &commit, &sender_commits)
        && sender_ledger.project_confirmed_through(
            17, 149, sender_before_target)
        && sender_before_target.frame == 149
        && fixture_ledger.confirm_through(
            17, 150, &commit, &fixture_commits)
        && sender_ledger.confirm_through(
            17, 150, &commit, &sender_commits)
        && fixture_commits == Horse::kRollbackSideEffectTypeCount
        && sender_commits == Horse::kRollbackSideEffectTypeCount;
    Horse::RollbackSideEffectConfirmedCheckpoint fixture_projection {};
    Horse::RollbackSideEffectConfirmedCheckpoint sender_projection {};
    const bool tagged_conservation = exact_target_committed
        && fixture_ledger.project_confirmed_through(
            17, 150, fixture_projection)
        && sender_ledger.project_confirmed_through(
            17, 150, sender_projection)
        && Horse::RollbackFixtureTaggedEvidenceValid(
            fixture_projection.fixture_queued_by_type,
            fixture_projection.fixture_discarded_by_type,
            fixture_projection.fixture_committed_by_type,
            fixture_projection.fixture_committed_digest_by_type, true)
        && Horse::RollbackFixtureTaggedEvidenceValid(
            sender_projection.fixture_queued_by_type,
            sender_projection.fixture_discarded_by_type,
            sender_projection.fixture_committed_by_type,
            sender_projection.fixture_committed_digest_by_type, false)
        && fixture_projection.committed_by_type
            == sender_projection.committed_by_type
        && fixture_projection.committed_digest_by_type
            == sender_projection.committed_digest_by_type;
    bool repeated_tagged_counts = true;
    for (size_t type = 0;
         type < Horse::kRollbackSideEffectTypeCount; ++type)
    {
        repeated_tagged_counts = repeated_tagged_counts
            && fixture_projection.fixture_queued_by_type[type] == 3
            && fixture_projection.fixture_discarded_by_type[type] == 2
            && fixture_projection.fixture_committed_by_type[type] == 1;
    }
    Horse::RollbackSideEffectLedger<8, 16> missing_type_ledger {};
    for (size_t type = 0;
         type + 1 < Horse::kRollbackSideEffectTypeCount; ++type)
    {
        (void)missing_type_ledger.enqueue(
            18, 120u + static_cast<uint32_t>(type),
            static_cast<Horse::RollbackSideEffectType>(type),
            0x1800u + type, &payload, sizeof(payload), 0, 1);
    }
    uint32_t missing_commits = 0;
    Horse::RollbackSideEffectConfirmedCheckpoint missing_projection {};
    const bool missing_type_rejected = missing_type_ledger.confirm_through(
            18, 150, &commit, &missing_commits)
        && missing_type_ledger.project_confirmed_through(
            18, 150, missing_projection)
        && !Horse::RollbackFixtureTaggedEvidenceValid(
            missing_projection.fixture_queued_by_type,
            missing_projection.fixture_discarded_by_type,
            missing_projection.fixture_committed_by_type,
            missing_projection.fixture_committed_digest_by_type, true);
    const bool missing_type_allowed_for_replay =
        Horse::RollbackFixtureTaggedEvidenceValid(
            missing_projection.fixture_queued_by_type,
            missing_projection.fixture_discarded_by_type,
            missing_projection.fixture_committed_by_type,
            missing_projection.fixture_committed_digest_by_type,
            true, false);
    Horse::RollbackSideEffectLedger<8, 16> skipped_target_ledger {};
    (void)skipped_target_ledger.enqueue(
        19, 120, Horse::RollbackSideEffectType::Audio, 0x1912,
        &payload, sizeof(payload), 0, 1);
    uint32_t skipped_commits = 0;
    Horse::RollbackSideEffectConfirmedCheckpoint skipped_projection {};
    const bool skipped_target_rejected = skipped_target_ledger.confirm_through(
            19, 151, &commit, &skipped_commits)
        && !skipped_target_ledger.project_confirmed_through(
            19, 150, skipped_projection);
    Horse::RollbackSideEffectLedger<8, 16> provenance_mismatch_ledger {};
    const bool provenance_mismatch_rejected =
        provenance_mismatch_ledger.enqueue(
            20, 120, Horse::RollbackSideEffectType::Audio, 0x2012,
            &payload, sizeof(payload), 0, 1)
        && !provenance_mismatch_ledger.enqueue(
            20, 120, Horse::RollbackSideEffectType::Audio, 0x2012,
            &payload, sizeof(payload), 0, 0)
        && std::strcmp(provenance_mismatch_ledger.report().failure,
            "side-effect-fixture-provenance-mismatch") == 0;
    Horse::RollbackSideEffectEvent canonical_a {};
    canonical_a.epoch = 20;
    canonical_a.frame = 120;
    canonical_a.type = Horse::RollbackSideEffectType::Audio;
    canonical_a.idempotency_key = 0x2012;
    canonical_a.payload_bytes = sizeof(payload);
    std::memcpy(canonical_a.payload.data(), &payload, sizeof(payload));
    Horse::RollbackSideEffectEvent canonical_b = canonical_a;
    canonical_b.fixture_generation = 1;
    const bool provenance_excluded_from_digest =
        Horse::ComputeRollbackSideEffectEventDigest(canonical_a)
            == Horse::ComputeRollbackSideEffectEventDigest(canonical_b);

    bool scope_ok = false;
    {
        Horse::RollbackResimScope scope(9, 12);
        const auto context = Horse::CurrentRollbackResimContext();
        scope_ok = context.active && context.epoch == 9 && context.frame == 12;
    }
    scope_ok = scope_ok && !Horse::CurrentRollbackResimContext().active;

    Horse::RollbackCameraOutputValue external {
        1.0f, 2.0f, 3.0f, 1};
    Horse::RollbackCameraPresentationValue published {};
    uint32_t native_camera_calls = 0;
    const auto camera_capture =
        Horse::RollbackCaptureCameraPresentationOutput(
            [&external](Horse::RollbackCameraOutputValue& value)
                noexcept
            {
                value = external;
                return true;
            },
            [&external, &native_camera_calls]() noexcept
            {
                ++native_camera_calls;
                external = {4.0f, 5.0f, 6.0f, 0};
            },
            [&external](
                const Horse::RollbackCameraOutputValue& value)
                noexcept
            {
                external = value;
                return true;
            },
            published);
    const bool camera_output_deferred = camera_capture.ok()
        && native_camera_calls == 1
        && published.x == 0.0f && published.y == 0.0f
        && published.z == 0.0f && published.active == 0
        && published.write_mask == Horse::RollbackCameraWriteActive
        && external.x == 1.0f && external.y == 2.0f
        && external.z == 3.0f && external.active == 1;
    uint32_t inactive_float_writes = 0;
    uint32_t inactive_active_writes = 0;
    const bool inactive_commit_semantics =
        Horse::RollbackCommitCameraPresentation(
            published,
            [&inactive_float_writes](size_t, float) noexcept
            {
                ++inactive_float_writes;
                return true;
            },
            [&inactive_active_writes](uint32_t active) noexcept
            {
                ++inactive_active_writes;
                return active == 0;
            })
        && inactive_float_writes == 0 && inactive_active_writes == 1;
    const Horse::RollbackCameraPresentationValue active_presentation {
        4.0f, 5.0f, 6.0f, 1,
        Horse::RollbackCameraWriteActive | Horse::RollbackCameraWriteXyz};
    uint32_t active_commit_calls = 0;
    const bool active_commit_attempts_all =
        !Horse::RollbackCommitCameraPresentation(
            active_presentation,
            [&active_commit_calls](size_t offset, float) noexcept
            {
                ++active_commit_calls;
                return offset != 4;
            },
            [&active_commit_calls](uint32_t active) noexcept
            {
                ++active_commit_calls;
                return active == 1;
            })
        && active_commit_calls == 4;
    Horse::RollbackCameraOutputValue active_external {
        7.0f, 8.0f, 9.0f, 0};
    Horse::RollbackCameraPresentationValue captured_active {};
    uint32_t active_native_calls = 0;
    const auto active_capture =
        Horse::RollbackCaptureCameraPresentationOutput(
            [&active_external](Horse::RollbackCameraOutputValue& value)
                noexcept
            {
                value = active_external;
                return true;
            },
            [&active_external, &active_native_calls]() noexcept
            {
                ++active_native_calls;
                active_external = {4.0f, 5.0f, 6.0f, 1};
            },
            [&active_external](
                const Horse::RollbackCameraOutputValue& value) noexcept
            {
                active_external = value;
                return true;
            },
            captured_active);
    uint32_t captured_active_float_writes = 0;
    uint32_t captured_active_flag_writes = 0;
    const bool active_capture_and_commit = active_capture.ok()
        && active_native_calls == 1
        && captured_active.x == 4.0f && captured_active.y == 5.0f
        && captured_active.z == 6.0f && captured_active.active == 1
        && captured_active.write_mask
            == (Horse::RollbackCameraWriteActive
                | Horse::RollbackCameraWriteXyz)
        && active_external.x == 7.0f && active_external.y == 8.0f
        && active_external.z == 9.0f && active_external.active == 0
        && Horse::RollbackCommitCameraPresentation(
            captured_active,
            [&captured_active_float_writes](size_t, float) noexcept
            {
                ++captured_active_float_writes;
                return true;
            },
            [&captured_active_flag_writes](uint32_t active) noexcept
            {
                ++captured_active_flag_writes;
                return active == 1;
            })
        && captured_active_float_writes == 3
        && captured_active_flag_writes == 1;
    bool invalid_output_restored = false;
    const auto invalid_camera_capture =
        Horse::RollbackCaptureCameraPresentationOutput(
            [&external](Horse::RollbackCameraOutputValue& value)
                noexcept
            {
                value = external;
                return true;
            },
            [&external]() noexcept
            {
                external.x = std::nanf("");
            },
            [&external, &invalid_output_restored](
                const Horse::RollbackCameraOutputValue& value)
                noexcept
            {
                external = value;
                invalid_output_restored = true;
                return true;
            },
            published);
    const bool invalid_camera_rejected = !invalid_camera_capture.ok()
        && invalid_camera_capture.output_restored
        && invalid_output_restored && external.x == 1.0f;

    uint32_t restore_write_calls = 0;
    const bool partial_restore_rejected =
        !Horse::RollbackRestoreCameraOutput(
            external,
            [&restore_write_calls](size_t offset, float) noexcept
            {
                ++restore_write_calls;
                return offset != 4;
            },
            [&restore_write_calls](uint32_t) noexcept
            {
                ++restore_write_calls;
                return true;
            })
        && restore_write_calls == 4;
    uint32_t failed_read_native_calls = 0;
    bool failed_read_restore_called = false;
    const auto failed_read_capture =
        Horse::RollbackCaptureCameraPresentationOutput(
            [reads = 0u](Horse::RollbackCameraOutputValue& value)
                mutable noexcept
            {
                ++reads;
                value = {1.0f, 2.0f, 3.0f, 1};
                return reads == 1;
            },
            [&failed_read_native_calls]() noexcept
            {
                ++failed_read_native_calls;
            },
            [&failed_read_restore_called](
                const Horse::RollbackCameraOutputValue&) noexcept
            {
                failed_read_restore_called = true;
                return true;
            },
            published);
    const bool failed_post_read_restored = !failed_read_capture.ok()
        && failed_read_capture.native_called
        && failed_read_capture.output_restored
        && failed_read_native_calls == 1 && failed_read_restore_called;
    uint32_t initial_read_native_calls = 0;
    const auto initial_read_capture =
        Horse::RollbackCaptureCameraPresentationOutput(
            [](Horse::RollbackCameraOutputValue&) noexcept
            {
                return false;
            },
            [&initial_read_native_calls]() noexcept
            {
                ++initial_read_native_calls;
            },
            [](const Horse::RollbackCameraOutputValue&) noexcept
            {
                return true;
            },
            published);
    const bool initial_read_blocks_native = !initial_read_capture.ok()
        && !initial_read_capture.native_called
        && initial_read_native_calls == 0;
    const bool camera_routes =
        Horse::SelectRollbackCameraPresentationRoute(false, false, false)
            == Horse::RollbackCameraPresentationRoute::PassThrough
        && Horse::SelectRollbackCameraPresentationRoute(true, false, true)
            == Horse::RollbackCameraPresentationRoute::CaptureOnly
        && Horse::SelectRollbackCameraPresentationRoute(true, true, false)
            == Horse::RollbackCameraPresentationRoute::CaptureOnly
        && Horse::SelectRollbackCameraPresentationRoute(true, true, true)
            == Horse::RollbackCameraPresentationRoute::CaptureAndQueue;
    constexpr size_t production_bucket_count =
        Horse::kRollbackProductionSummaryWindowCapacity * 2u;
    Horse::RollbackSideEffectLedger<1, production_bucket_count>
        deferred_confirmation_ledger {};
    uint32_t deferred_commits = 0;
    bool deferred_confirmation = true;
    // Model the worst accepted production batch: one full summary window
    // remains presentation-pending after consensus retires it, then one new
    // summary window is populated before the batch-end commit.
    for (uint32_t frame = 0;
         frame < production_bucket_count; ++frame)
    {
        deferred_confirmation = deferred_confirmation
            && deferred_confirmation_ledger.enqueue(
                23, frame, Horse::RollbackSideEffectType::Camera,
                static_cast<uint64_t>(frame) + 1u,
                &payload, sizeof(payload));
    }
    deferred_confirmation = deferred_confirmation
        && deferred_confirmation_ledger.pending()
            == production_bucket_count
        && deferred_confirmation_ledger.confirm_through(
            23, static_cast<uint32_t>(production_bucket_count - 1u),
            &commit, &deferred_commits)
        && deferred_commits == production_bucket_count
        && deferred_confirmation_ledger.pending() == 0
        && deferred_confirmation_ledger.report().ok;
    Horse::RollbackSideEffectLedger<1, production_bucket_count>
        deferred_confirmation_overflow_ledger {};
    bool deferred_confirmation_overflow = true;
    for (uint32_t frame = 0;
         frame < production_bucket_count; ++frame)
    {
        deferred_confirmation_overflow =
            deferred_confirmation_overflow
            && deferred_confirmation_overflow_ledger.enqueue(
                24, frame, Horse::RollbackSideEffectType::Camera,
                static_cast<uint64_t>(frame) + 1u,
                &payload, sizeof(payload));
    }
    deferred_confirmation_overflow =
        deferred_confirmation_overflow
        && !deferred_confirmation_overflow_ledger.enqueue(
            24, static_cast<uint32_t>(production_bucket_count),
            Horse::RollbackSideEffectType::Camera,
            static_cast<uint64_t>(production_bucket_count) + 1u,
            &payload, sizeof(payload))
        && deferred_confirmation_overflow_ledger.report().overflow
        && std::strcmp(
            deferred_confirmation_overflow_ledger.report().failure,
            "side-effect-frame-bucket-collision") == 0;

    const bool ok = q0 && duplicate && q1 && camera_q && discarded
        && vfx_payload_boundary
        && camera_discarded && lane_discarded_from && once
        && still_once && digest_stable && commit_failure_accounting
        && wrap_order_committed && exact_confirmation_repeat
        && backward_confirmation_rejected
        && committed_duplicate_allowed && new_late_event_rejected
        && mixed_epoch_confirmation_rejected
        && ordinal_domains_isolated && ordinal_reset
        && lane_identity_isolated
        && loaded_frame_kept && range_after && witness_after
        && load_counts_projected
        && epoch_witness_scoped && epoch_rollback_scoped
        && corrected_only_witness && continuous_load_preserves_first
        && invalid_witness_rejected && witness_reset
        && tagged_conservation && repeated_tagged_counts
        && missing_type_rejected && missing_type_allowed_for_replay
        && skipped_target_rejected
        && provenance_mismatch_rejected
        && provenance_excluded_from_digest
        && scope_ok
        && native_correction_bypass
        && round_lifecycle_enable
        && checkpoint_projection
        && camera_output_deferred && invalid_camera_rejected
        && inactive_commit_semantics && active_commit_attempts_all
        && active_capture_and_commit
        && partial_restore_rejected && failed_post_read_restored
        && initial_read_blocks_native && camera_routes
        && deferred_confirmation && deferred_confirmation_overflow
        && ledger.report().duplicates >= 2;
    std::printf(
        "rollback side-effect ledger self-test %s discarded=%d "
        "lane_discard=%d witness=%d epoch_scope=%d provenance=%d once=%d "
        "loaded_frame=%d scope=%d native_correction_bypass=%d "
        "round_lifecycle_enable=%d checkpoint_projection=%d "
        "ordinal_domains=%d lane_identity=%d "
        "camera_output=%d camera_invalid=%d "
        "camera_inactive=%d camera_commit_all=%d camera_active=%d "
        "camera_partial_restore=%d camera_post_read=%d "
        "camera_initial_read=%d camera_routes=%d deferred_confirmation=%d "
        "deferred_confirmation_overflow=%d "
        "duplicates=%llu\n",
        ok ? "passed" : "failed", discarded ? 1 : 0,
        lane_discarded_from ? 1 : 0, witness_after ? 1 : 0,
        (epoch_witness_scoped && epoch_rollback_scoped) ? 1 : 0,
        (corrected_only_witness && continuous_load_preserves_first
            && invalid_witness_rejected && witness_reset
            && tagged_conservation && repeated_tagged_counts
            && missing_type_rejected && missing_type_allowed_for_replay
            && skipped_target_rejected
            && provenance_mismatch_rejected
            && provenance_excluded_from_digest) ? 1 : 0,
        once ? 1 : 0,
        loaded_frame_kept ? 1 : 0, scope_ok ? 1 : 0,
        native_correction_bypass ? 1 : 0,
        round_lifecycle_enable ? 1 : 0,
        checkpoint_projection ? 1 : 0,
        (ordinal_domains_isolated && ordinal_reset) ? 1 : 0,
        lane_identity_isolated ? 1 : 0,
        camera_output_deferred ? 1 : 0,
        invalid_camera_rejected ? 1 : 0,
        inactive_commit_semantics ? 1 : 0,
        active_commit_attempts_all ? 1 : 0,
        active_capture_and_commit ? 1 : 0,
        partial_restore_rejected ? 1 : 0,
        failed_post_read_restored ? 1 : 0,
        initial_read_blocks_native ? 1 : 0,
        camera_routes ? 1 : 0,
        deferred_confirmation ? 1 : 0,
        deferred_confirmation_overflow ? 1 : 0,
        static_cast<unsigned long long>(ledger.report().duplicates));
    return ok ? 0 : 1;
}
