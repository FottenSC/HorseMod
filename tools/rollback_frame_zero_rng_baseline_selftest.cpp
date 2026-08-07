#include "RollbackFrameZeroRngBaseline.hpp"

#include <array>
#include <cstdio>

namespace
{
    Horse::RollbackRngTuple make_rng(uint32_t index)
    {
        std::array<uint8_t, 100> lfsr {};
        for (size_t i = 0; i < lfsr.size(); ++i)
            lfsr[i] = static_cast<uint8_t>((i * 13u + 7u) & 0xFFu);
        Horse::RollbackGameplayCrtState gameplay_crt {};
        gameplay_crt.internal_state = 0x2468ACE0u + index;
        gameplay_crt.full_round_seed = 0x12345000u;
        gameplay_crt.gameplay_draw_ordinal = index;
        gameplay_crt.warmup_draws = 0;
        gameplay_crt.owner_thread_id = 77;
        gameplay_crt.phase = Horse::RollbackGameplayCrtPhase::Ready;
        gameplay_crt.native_seed_observed = true;
        return Horse::RollbackRngTuple::capture(
            0x13579BDFu, lfsr, index, &gameplay_crt);
    }

    Horse::RollbackFrameZeroBaselineSnapshot make_snapshot(
        uint32_t round,
        uint64_t epoch,
        const Horse::RollbackRngTuple& rng)
    {
        Horse::RollbackFrameZeroBaselineSnapshot snapshot {};
        snapshot.round = round;
        snapshot.epoch = epoch;
        snapshot.logical_frame = -1;
        snapshot.canonical_hash = 0x1000000000000000ull + epoch;
        snapshot.hgcpu_hash = 0x2000000000000000ull + epoch;
        snapshot.explicit_hash = 0x3000000000000000ull + epoch;
        snapshot.stage_hash = 0x4000000000000000ull + epoch;
        snapshot.wind_hash = 0x5000000000000000ull + epoch;
        snapshot.rng = rng;
        return snapshot;
    }

    Horse::RollbackFrameZeroBaselineIdentity identity_of(
        const Horse::RollbackFrameZeroBaselineSnapshot& snapshot)
    {
        return {
            snapshot.round,
            snapshot.epoch,
            snapshot.canonical_hash,
        };
    }
}

int main()
{
    using namespace Horse;
    using Phase = RollbackFrameZeroBaselinePhase;
    using Result = RollbackFrameZeroBaselineResult;
    using Restore = RollbackFrameZeroRestoreDecision;

    bool ok = !RollbackReplayRngBaselineRequired(false, false)
        && !RollbackReplayRngBaselineRequired(false, true)
        && !RollbackReplayRngBaselineRequired(true, false)
        && RollbackReplayRngBaselineRequired(true, true);

    const RollbackRngTuple rng_index_5 = make_rng(5);
    const RollbackRngTuple rng_index_8 = make_rng(8);
    auto rng_lcg_changed = rng_index_5;
    ++rng_lcg_changed.lcg_state;
    auto changed_lfsr = rng_index_5.lfsr_state;
    changed_lfsr[37] ^= 0x80u;
    const RollbackRngTuple rng_lfsr_changed = RollbackRngTuple::capture(
        rng_index_5.lcg_state, changed_lfsr, rng_index_5.lfsr_index,
        &rng_index_5.gameplay_crt);
    const auto baseline = make_snapshot(2, 0xABCDEFu, rng_index_5);

    RollbackFrameZeroBaselineGate gate {};
    ok = ok
        && rng_index_5.valid()
        && rng_index_8.valid()
        && rng_lcg_changed.valid()
        && rng_lfsr_changed.valid()
        && rng_index_5 != rng_index_8
        && rng_index_5 != rng_lcg_changed
        && rng_index_5 != rng_lfsr_changed
        && gate.capture(baseline) == Result::Accepted
        && gate.phase() == Phase::Captured;

    // Bootstrap updates may occur both before and after barrier acceptance.
    // A drift from the sidecar's index 5 to index 8 must be detected and the
    // complete tuple (not just the index) must be verified after restoration.
    ok = ok
        && gate.before_starting_gekko_update(rng_index_8)
            == Restore::RestoreRequired
        && gate.before_starting_gekko_update(rng_lcg_changed)
            == Restore::RestoreRequired
        && gate.before_starting_gekko_update(rng_lfsr_changed)
            == Restore::RestoreRequired
        && gate.verify_restored(rng_index_5) == Result::Accepted
        && gate.restore_verifications() == 1
        && gate.accept_barrier(identity_of(baseline)) == Result::Accepted
        && gate.phase() == Phase::BarrierAccepted
        && gate.before_starting_gekko_update(rng_index_8)
            == Restore::RestoreRequired
        && gate.verify_restored(rng_index_8) == Result::RngMismatch
        && gate.verify_restored(rng_index_5) == Result::Accepted
        && gate.mark_gekko_ready() == Result::Accepted
        && gate.phase() == Phase::GekkoReady;

    // Save(-1) must reject both RNG-only and non-RNG snapshot differences and
    // remain retryable with the exact immutable launch snapshot.
    auto wrong_rng_save = baseline;
    wrong_rng_save.rng = rng_index_8;
    auto wrong_component_save = baseline;
    ++wrong_component_save.explicit_hash;
    ok = ok
        && gate.record_baseline_save(wrong_rng_save) == Result::RngMismatch
        && gate.phase() == Phase::GekkoReady
        && gate.record_baseline_save(wrong_component_save)
            == Result::SnapshotMismatch
        && gate.record_baseline_save(baseline) == Result::Accepted
        && gate.phase() == Phase::BaselineSaved
        && gate.record_baseline_save(baseline) == Result::InvalidTransition;

    // Save without Advance remains a live pre-frame-zero transaction. Drift
    // after Save is repaired before the owned native iteration.
    ok = ok
        && gate.before_starting_gekko_update(rng_index_8)
            == Restore::RestoreRequired
        && gate.verify_restored(rng_index_5) == Result::Accepted
        && gate.phase() == Phase::BaselineSaved;
    auto drifted_pre_advance = baseline;
    drifted_pre_advance.rng = rng_index_8;
    ok = ok
        && gate.commit_frame_zero_advance(drifted_pre_advance)
            == Result::RngMismatch
        && gate.phase() == Phase::BaselineSaved
        && gate.commit_frame_zero_advance(baseline) == Result::Accepted
        && gate.phase() == Phase::FrameZeroAdvanced;

    // Once Advance(0) commits, no restore or accidental same-round restage is
    // possible. Only an explicit next-round rearm creates a new transaction.
    ok = ok
        && gate.before_starting_gekko_update(rng_index_5)
            == Restore::FrameZeroAlreadyAdvanced
        && gate.verify_restored(rng_index_5) == Result::InvalidTransition
        && gate.capture(baseline) == Result::InvalidTransition
        && gate.rearm_for_next_round() == Result::Accepted
        && gate.phase() == Phase::Unarmed;

    const auto next_baseline = make_snapshot(3, 0xABCDF0u, make_rng(4));
    auto stale_identity = identity_of(next_baseline);
    ++stale_identity.epoch;
    ok = ok
        && gate.capture(next_baseline) == Result::Accepted
        && gate.accept_barrier(stale_identity) == Result::IdentityMismatch
        && gate.phase() == Phase::Captured
        && gate.accept_barrier(identity_of(next_baseline)) == Result::Accepted
        && gate.mark_gekko_ready() == Result::Accepted
        && gate.commit_frame_zero_advance(next_baseline)
            == Result::InvalidTransition
        && gate.record_baseline_save(next_baseline) == Result::Accepted
        && gate.commit_frame_zero_advance(next_baseline) == Result::Accepted;

    // Invalid tuple and invalid launch identity are rejected before the state
    // machine can publish them.
    RollbackFrameZeroBaselineGate invalid_gate {};
    auto invalid_snapshot = baseline;
    ++invalid_snapshot.rng.lfsr_hash;
    ok = ok
        && invalid_gate.capture(invalid_snapshot) == Result::InvalidSnapshot
        && invalid_gate.phase() == Phase::Unarmed
        && invalid_gate.before_starting_gekko_update(rng_index_5)
            == Restore::NotArmed;

    std::printf(
        "rollback frame-zero RNG baseline self-test %s restores=%u\n",
        ok ? "passed" : "failed",
        gate.restore_verifications());
    return ok ? 0 : 1;
}
