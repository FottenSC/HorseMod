#include "RollbackNativePreNewRoundGate.hpp"

#include <cstdio>

namespace
{
    constexpr Horse::RollbackPreNewRoundBarrierMessage MakeMessage(
        uint8_t slot)
    {
        Horse::RollbackPreNewRoundBarrierMessage message {};
        message.stage = Horse::RollbackPreNewRoundBarrierStage::Ready;
        message.local_player_slot = slot;
        message.completed_round_ordinal = 1;
        message.target_round_ordinal = 2;
        message.session_epoch = 0x11;
        message.completed_pair_epoch = 0x22;
        message.terminal_canonical_hash = 0x33;
        message.target_round_generation = 2;
        message.match_identity_digest = 0x44;
        message.entry_digest = 0x55;
        message.native_stage_identity = 0x10009;
        return message;
    }
}

int main()
{
    using Horse::RollbackNativePreNewRoundAction;
    using Horse::RollbackNativePreNewRoundGate;
    using Horse::RollbackPreNewRoundBarrierStage;

    constexpr auto host = MakeMessage(0);
    constexpr auto guest = MakeMessage(1);
    static_assert(sizeof(Horse::RollbackPreNewRoundBarrierMessage) == 68);
    static_assert(Horse::RollbackPreNewRoundBarrierValid(host));
    static_assert(Horse::RollbackPreNewRoundBarrierValid(guest));
    static_assert(Horse::RollbackPreNewRoundBarriersMatch(host, guest));
    const bool host_valid = Horse::RollbackPreNewRoundBarrierValid(host);
    const bool guest_valid = Horse::RollbackPreNewRoundBarrierValid(guest);
    const bool pair_match =
        Horse::RollbackPreNewRoundBarriersMatch(host, guest);
    if (!host_valid || !guest_valid || !pair_match)
    {
        std::printf(
            "pre-NewRound initial contract failed valid=%d/%d match=%d "
            "host=%u:%u:%u:%u:%u:%u:%llu:%llu:%llu:%llu:%llu:%llu:%u:%u "
            "guest=%u:%u:%u:%u:%u:%u:%llu:%llu:%llu:%llu:%llu:%llu:%u:%u\n",
            host_valid ? 1 : 0, guest_valid ? 1 : 0, pair_match ? 1 : 0,
            static_cast<unsigned>(host.version),
            static_cast<unsigned>(host.stage),
            static_cast<unsigned>(host.local_player_slot),
            static_cast<unsigned>(host.reserved0),
            host.completed_round_ordinal, host.target_round_ordinal,
            static_cast<unsigned long long>(host.session_epoch),
            static_cast<unsigned long long>(host.completed_pair_epoch),
            static_cast<unsigned long long>(host.terminal_canonical_hash),
            static_cast<unsigned long long>(host.target_round_generation),
            static_cast<unsigned long long>(host.match_identity_digest),
            static_cast<unsigned long long>(host.entry_digest),
            host.native_stage_identity, host.reserved1,
            static_cast<unsigned>(guest.version),
            static_cast<unsigned>(guest.stage),
            static_cast<unsigned>(guest.local_player_slot),
            static_cast<unsigned>(guest.reserved0),
            guest.completed_round_ordinal, guest.target_round_ordinal,
            static_cast<unsigned long long>(guest.session_epoch),
            static_cast<unsigned long long>(guest.completed_pair_epoch),
            static_cast<unsigned long long>(guest.terminal_canonical_hash),
            static_cast<unsigned long long>(guest.target_round_generation),
            static_cast<unsigned long long>(guest.match_identity_digest),
            static_cast<unsigned long long>(guest.entry_digest),
            guest.native_stage_identity, guest.reserved1);
        return 1;
    }

    RollbackNativePreNewRoundGate fast {};
    if (!fast.arrive(host)
        || fast.action(true) != RollbackNativePreNewRoundAction::Hold)
        return 2;
    for (int skew = 0; skew < 4; ++skew)
    {
        if (!fast.arrive(host)
            || fast.action(true) != RollbackNativePreNewRoundAction::Hold)
            return 3;
    }
    if (!fast.accept_peer(guest)
        || fast.action(true) != RollbackNativePreNewRoundAction::Release
        || !fast.mark_released()
        || fast.local().stage != RollbackPreNewRoundBarrierStage::Accepted
        || fast.report().releases != 1)
        return 4;
    if (fast.action(true) != RollbackNativePreNewRoundAction::Reject
        || fast.mark_released())
        return 5;

    RollbackNativePreNewRoundGate slow {};
    if (!slow.accept_peer(host) || !slow.arrive(guest)
        || slow.action(true) != RollbackNativePreNewRoundAction::Release
        || !slow.mark_released())
        return 6;
    auto host_accepted = host;
    host_accepted.stage = RollbackPreNewRoundBarrierStage::Accepted;
    auto guest_accepted = guest;
    guest_accepted.stage = RollbackPreNewRoundBarrierStage::Accepted;
    if (!slow.accept_peer(host_accepted) || !slow.complete())
        return 7;

    // Packet loss may allow one peer to release and publish Accepted before
    // the held peer receives any matching barrier. The held peer must remain
    // eligible for a same-thread service retry; otherwise stock will not call
    // the one-shot E60 PostTick a second time and the round transition stalls.
    RollbackNativePreNewRoundGate asymmetric {};
    if (!asymmetric.arrive(host)
        || !asymmetric.accept_peer(guest_accepted)
        || asymmetric.complete()
        || asymmetric.action(true)
            != RollbackNativePreNewRoundAction::Release
        || !Horse::RollbackNativePreNewRoundServiceReleaseAllowed(
            asymmetric, true, true, false)
        || Horse::RollbackNativePreNewRoundServiceReleaseAllowed(
            asymmetric, true, false, false)
        || Horse::RollbackNativePreNewRoundServiceReleaseAllowed(
            asymmetric, false, true, false)
        || Horse::RollbackNativePreNewRoundServiceReleaseAllowed(
            asymmetric, true, true, true)
        || !asymmetric.mark_released()
        || !asymmetric.complete()
        || Horse::RollbackNativePreNewRoundServiceReleaseAllowed(
            asymmetric, true, true, false))
        return 70;
    if (Horse::RollbackNativePreNewRoundSuppressSimulationLoop(
            asymmetric, false, false)
        || Horse::RollbackNativePreNewRoundSuppressSimulationLoop(
            asymmetric, true, true)
        || !Horse::RollbackNativeInterRoundServiceTickAllowed(
            true, true, true, 17, 17)
        || Horse::RollbackNativeInterRoundServiceTickAllowed(
            true, true, true, 18, 17)
        || Horse::RollbackNativeInterRoundServiceTickAllowed(
            true, false, true, 17, 17)
        || Horse::RollbackNativeInterRoundServiceTickAllowed(
            false, true, true, 17, 17)
        || Horse::RollbackNativeInterRoundServiceTickAllowed(
            true, true, false, 17, 17))
        return 71;

    const Horse::RollbackPreNewRoundExpectedIdentity expected {
        1, 1, 2, 0x11, 0x22, 0x33, 2, 0x44, 0x10009,
    };
    if (Horse::ClassifyRollbackPreNewRoundPacket(guest, expected)
            != Horse::RollbackPreNewRoundPacketDisposition::Current)
        return 8;
    RollbackNativePreNewRoundGate receiver {};
    const auto received = Horse::ReceiveRollbackPreNewRoundBarrier(
        receiver, guest, expected);
    if (!received.accepted
        || received.disposition
            != Horse::RollbackPreNewRoundPacketDisposition::Current
        || !receiver.peer_valid())
        return 9;

    Horse::RollbackCompletedRoundEpochLatch completed_epoch {};
    RollbackNativePreNewRoundGate early_peer {};
    uint64_t active_epoch = 0x22;
    if (!Horse::RollbackPreNewRoundReceiveWindowOpen(
            false, true, true)
        || Horse::RollbackPreNewRoundReceiveWindowOpen(
            false, true, false)
        || Horse::RollbackPreNewRoundReceiveWindowOpen(
            false, false, true)
        || Horse::ResolveRollbackCompletedRoundEpoch(
            completed_epoch.value(), active_epoch, true, true) != 0x22
        || !Horse::ReceiveRollbackPreNewRoundBarrier(
            early_peer, guest, expected).accepted
        || !early_peer.peer_valid()
        || !completed_epoch.retain(active_epoch))
        return 10;
    active_epoch = 0;
    if (!early_peer.prepare_local_transition_preserving_peer()
        || !early_peer.peer_valid()
        || !Horse::RollbackPreNewRoundReceiveWindowOpen(
            true, false, false)
        || Horse::ResolveRollbackCompletedRoundEpoch(
            completed_epoch.value(), active_epoch, false, false) != 0x22
        || !early_peer.arrive(host)
        || early_peer.action(true)
            != RollbackNativePreNewRoundAction::Release
        || !early_peer.mark_released()
        || completed_epoch.clear_after_new_round_acceptance(false, 0x99)
        || completed_epoch.clear_after_new_round_acceptance(true, 0x22)
        || completed_epoch.value() != 0x22
        || !completed_epoch.clear_after_new_round_acceptance(true, 0x99)
        || completed_epoch.valid())
        return 11;
    RollbackNativePreNewRoundGate changed_early_peer {};
    auto changed_guest = guest;
    ++changed_guest.entry_digest;
    if (!changed_early_peer.accept_peer(guest)
        || changed_early_peer.accept_peer(changed_guest)
        || !changed_early_peer.report().failed)
        return 12;
    auto stale = guest;
    stale.target_round_generation = 1;
    if (Horse::ClassifyRollbackPreNewRoundPacket(stale, expected)
            != Horse::RollbackPreNewRoundPacketDisposition::Invalid)
        return 13;
    stale = guest;
    stale.target_round_generation = 2;
    Horse::RollbackPreNewRoundExpectedIdentity next_expected = expected;
    next_expected.target_round_generation = 3;
    if (Horse::ClassifyRollbackPreNewRoundPacket(stale, next_expected)
            != Horse::RollbackPreNewRoundPacketDisposition::Stale)
        return 14;
    auto future = guest;
    future.target_round_generation = 3;
    future.completed_round_ordinal = 2;
    future.target_round_ordinal = 3;
    if (Horse::ClassifyRollbackPreNewRoundPacket(future, expected)
            != Horse::RollbackPreNewRoundPacketDisposition::Future)
        return 15;
    auto wrong_session = guest;
    wrong_session.session_epoch++;
    if (Horse::ClassifyRollbackPreNewRoundPacket(wrong_session, expected)
            != Horse::RollbackPreNewRoundPacketDisposition::Invalid)
        return 16;

    RollbackNativePreNewRoundGate sequence {};
    uint64_t original_calls = 0;
    bool post_release_pending = false;
    if (Horse::RollbackNativePreNewRoundSuppressOuterCall(
            sequence, post_release_pending)
        || !sequence.arrive(host)
        || sequence.action(true) != RollbackNativePreNewRoundAction::Hold
        || !Horse::RollbackNativePreNewRoundSuppressOuterCall(
            sequence, post_release_pending)
        || !Horse::RollbackNativePreNewRoundTransitionHeld(
            sequence, post_release_pending)
        || Horse::RollbackNativePreNewRoundShutdownAllowed(true, false)
        || !Horse::RollbackNativePreNewRoundShutdownAllowed(true, true))
        return 17;
    if (!sequence.accept_peer(guest)
        || !sequence.release_ready()
        || !Horse::ValidateRollbackNativePreNewRoundHeldReentry({
            true, true, true, sequence.local_valid(), sequence.released(),
            0xE60, 0, 0xE60, 2, 2, 2})
        || !Horse::RollbackNativePreNewRoundReleaseTransitAllowed(
            0xE60, 0xE60, sequence)
        || Horse::RollbackNativePreNewRoundReleaseTransitAllowed(
            0xE20, 0xE60, sequence)
        || Horse::RollbackNativePreNewRoundSuppressOuterCall(
            sequence, post_release_pending)
        || sequence.action(true) != RollbackNativePreNewRoundAction::Release
        || !sequence.mark_released())
        return 18;
    if (Horse::RollbackNativePreNewRoundReleaseTransitAllowed(
            0xE60, 0xE60, sequence)
        || sequence.complete()
        || !Horse::RollbackNativePreNewRoundTransitionHeld(sequence, false))
        return 19;
    if (!Horse::RollbackNativePreNewRoundDeferredEntryAllowed(
            0xE60, 0xE60, true, true, false)
        || Horse::RollbackNativePreNewRoundDeferredEntryAllowed(
            0xE20, 0xE60, true, true, false)
        || Horse::RollbackNativePreNewRoundDeferredEntryAllowed(
            0xE60, 0xE60, false, true, false)
        || Horse::RollbackNativePreNewRoundDeferredEntryAllowed(
            0xE60, 0xE60, true, false, false)
        || Horse::RollbackNativePreNewRoundDeferredEntryAllowed(
            0xE60, 0xE60, true, true, true))
        return 190;
    post_release_pending = true;
    if (!Horse::RollbackNativePreNewRoundSuppressOuterCall(
            sequence, post_release_pending))
        return 20;
    ++original_calls;
    const Horse::RollbackNativePreNewRoundPostReleaseObservation observation {
        true, true, sequence.released(),
        0xE20, 0, 0xE20, 1, 120, 1,
        original_calls, sequence.report().releases, 1, 2,
    };
    if (!Horse::ValidateRollbackNativePreNewRoundPostRelease(observation))
        return 21;
    post_release_pending = false;
    if (!Horse::RollbackNativePreNewRoundSuppressOuterCall(
            sequence, post_release_pending)
        || !Horse::RollbackNativePreNewRoundTransitionHeld(
            sequence, post_release_pending)
        || Horse::RollbackNativePreNewRoundShutdownAllowed(
            Horse::RollbackNativePreNewRoundTransitionHeld(
                sequence, post_release_pending), false))
        return 22;
    if (!sequence.accept_peer(guest_accepted) || !sequence.complete()
        || Horse::RollbackNativePreNewRoundSuppressOuterCall(
            sequence, post_release_pending)
        || Horse::RollbackNativePreNewRoundTransitionHeld(
            sequence, post_release_pending)
        || !Horse::RollbackNativePreNewRoundShutdownAllowed(
            Horse::RollbackNativePreNewRoundTransitionHeld(
                sequence, post_release_pending), false)
        || original_calls != 1)
        return 23;

    auto bad_observation = observation;
    bad_observation.per_frame_calls = 2;
    if (Horse::ValidateRollbackNativePreNewRoundPostRelease(bad_observation))
        return 24;

    auto bad_reentry = Horse::RollbackNativePreNewRoundHeldReentryObservation {
        true, true, true, true, false, 0xE60, 0, 0xE60, 2, 2, 2};
    bad_reentry.queued_mode = 0xE20;
    if (Horse::ValidateRollbackNativePreNewRoundHeldReentry(bad_reentry))
        return 25;

    const Horse::RollbackNativePreNewRoundCinematicReleaseObservation
        cinematic_idle {true, 0, 0, 0, 0, 0, 0};
    if (!Horse::RollbackNativePreNewRoundCinematicReadyForRelease(
            cinematic_idle))
        return 241;
    const Horse::RollbackNativePreNewRoundCinematicReleaseObservation
        cinematic_capture_quiescent {true, 1, 0, 0, 0, 0, 0};
    if (!Horse::RollbackNativePreNewRoundCinematicReadyForRelease(
            cinematic_capture_quiescent))
        return 245;
    // Exact failing v34 phase: the E60 callback arrived while the stock
    // cinematic cleanup still owned a pending active-HgCpu restore. Releasing
    // here queues E20 too early; the restore erases that queue later in the
    // same world-pump call.
    const Horse::RollbackNativePreNewRoundCinematicReleaseObservation
        cinematic_restore_armed {true, 2, 1, 0, 0, 0, 1};
    const uint32_t armed_mask = Horse::
        RollbackNativePreNewRoundCinematicReleaseMismatchMask(
            cinematic_restore_armed);
    if (Horse::RollbackNativePreNewRoundCinematicReadyForRelease(
            cinematic_restore_armed)
        || (armed_mask
            & Horse::RollbackNativePreNewRoundCinematicReleaseMismatchState)
            == 0
        || (armed_mask
            & Horse::RollbackNativePreNewRoundCinematicReleaseMismatchTrigger)
            == 0
        || (armed_mask & Horse::
                RollbackNativePreNewRoundCinematicReleaseMismatchInteractiveBlock)
            == 0)
        return 242;
    auto unreadable_cinematic = cinematic_idle;
    unreadable_cinematic.readable = false;
    if (Horse::RollbackNativePreNewRoundCinematicReadyForRelease(
            unreadable_cinematic))
        return 243;
    auto palette_active_cinematic = cinematic_idle;
    palette_active_cinematic.palette_state_1 = 1;
    if (Horse::RollbackNativePreNewRoundCinematicReadyForRelease(
            palette_active_cinematic))
        return 244;

    // Retain attribution for the old post-release sequence, but production now
    // prevents it by deferring the release while cinematic_restore_armed is
    // true. If this observation occurs after an idle-phase release it is an
    // unexpected duplicate and must fail closed, not be suppressed.
    const Horse::
        RollbackNativePreNewRoundPendingValidationReentryObservation
            pending_validation_reentry {
                true, true, true, true, true, true,
                0xE60, 0, 0xE60, 2, 2, 2,
            };
    if (!Horse::
            ValidateRollbackNativePreNewRoundPendingValidationReentry(
                pending_validation_reentry))
        return 251;
    auto pending_with_second_queue = pending_validation_reentry;
    pending_with_second_queue.queued_mode = 0xE20;
    if (Horse::
            ValidateRollbackNativePreNewRoundPendingValidationReentry(
                pending_with_second_queue))
        return 252;
    auto pending_without_release = pending_validation_reentry;
    pending_without_release.released = false;
    if (Horse::
            ValidateRollbackNativePreNewRoundPendingValidationReentry(
                pending_without_release))
        return 253;
    auto pending_after_validation = pending_validation_reentry;
    pending_after_validation.post_release_validation_pending = false;
    if (Horse::
            ValidateRollbackNativePreNewRoundPendingValidationReentry(
                pending_after_validation))
        return 254;

    RollbackNativePreNewRoundGate accepted_before_reentry {};
    uint64_t accepted_before_reentry_original_calls = 0;
    if (!accepted_before_reentry.arrive(host)
        || accepted_before_reentry.action(true)
            != RollbackNativePreNewRoundAction::Hold
        || !accepted_before_reentry.accept_peer(guest_accepted))
        return 26;
    const Horse::RollbackPreNewRoundExpectedIdentity local_current {
        0, 1, 2, 0x11, 0x22, 0x33, 2, 0x44, 0x10009,
    };
    const bool retained_current =
        Horse::ClassifyRollbackPreNewRoundPacket(
            accepted_before_reentry.local(), local_current)
            == Horse::RollbackPreNewRoundPacketDisposition::Current;
    const Horse::RollbackNativePreNewRoundHeldReentryObservation
        accepted_reentry {
            true, true, retained_current,
            accepted_before_reentry.local_valid(),
            accepted_before_reentry.released(),
            0xE60, 0, 0xE60, 2, 2, 2,
        };
    if (!Horse::ValidateRollbackNativePreNewRoundHeldReentry(
            accepted_reentry)
        || accepted_before_reentry.action(true)
            != RollbackNativePreNewRoundAction::Release
        || !accepted_before_reentry.mark_released())
        return 27;
    ++accepted_before_reentry_original_calls;
    if (accepted_before_reentry_original_calls != 1
        || accepted_before_reentry.report().arrivals != 1
        || accepted_before_reentry.report().releases != 1
        || accepted_before_reentry.local().entry_digest
            != host.entry_digest)
        return 28;

    auto stale_local_current = local_current;
    ++stale_local_current.session_epoch;
    auto stale_reentry = accepted_reentry;
    const uint32_t stale_control_mask =
        Horse::RollbackPreNewRoundCurrentIdentityMismatchMask(
            host, stale_local_current);
    stale_reentry.retained_control_identity_current = stale_control_mask == 0;
    if (stale_control_mask
            != Horse::RollbackPreNewRoundCurrentIdentityMismatchSession
        || Horse::RollbackNativePreNewRoundHeldReentryMismatchMask(
                stale_reentry)
            != Horse::RollbackNativePreNewRoundHeldReentryMismatchControl
        || Horse::ValidateRollbackNativePreNewRoundHeldReentry(stale_reentry))
        return 29;

    RollbackNativePreNewRoundGate inactive {};
    if (inactive.action(false)
        != RollbackNativePreNewRoundAction::PassThrough)
        return 30;

    for (int field = 0; field < 8; ++field)
    {
        auto bad = guest;
        switch (field)
        {
        case 0: bad.session_epoch++; break;
        case 1: bad.completed_pair_epoch++; break;
        case 2: bad.terminal_canonical_hash++; break;
        case 3: bad.target_round_generation++; break;
        case 4: bad.match_identity_digest++; break;
        case 5: bad.entry_digest++; break;
        case 6: bad.native_stage_identity++; break;
        case 7: bad.target_round_ordinal++; break;
        }
        RollbackNativePreNewRoundGate mismatch {};
        if (!mismatch.arrive(host) || mismatch.accept_peer(bad)
            || !mismatch.report().failed)
            return 31 + field;
    }

    std::puts("rollback native pre-NewRound gate selftest passed");
    return 0;
}
