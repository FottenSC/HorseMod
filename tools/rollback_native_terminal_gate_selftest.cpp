#include "RollbackNativeTerminalGate.hpp"
#include "RollbackNativeSimulationIteration.hpp"

#include <cstdio>
#include <cstring>

int main()
{
    if (Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::Active, false, false, false, false)
            != Horse::RollbackNativeTerminalGekkoPump::OrdinaryUpdate
        || Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::Active, true, false, false, false)
            != Horse::RollbackNativeTerminalGekkoPump::OrdinaryUpdate
        || Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::Active, false, true, true, false)
            != Horse::RollbackNativeTerminalGekkoPump::OrdinaryUpdate
        || Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::Active, true, true, false, false)
            != Horse::RollbackNativeTerminalGekkoPump::CorrectionFlush
        || Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::ConfirmingRoundEnd,
            true, false, true, false)
            != Horse::RollbackNativeTerminalGekkoPump::CorrectionFlush
        || Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::TerminalAccepted,
            true, true, true, false)
            != Horse::RollbackNativeTerminalGekkoPump::OrdinaryUpdate
        || Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::StockInterRound,
            true, true, true, false)
            != Horse::RollbackNativeTerminalGekkoPump::OrdinaryUpdate)
    {
        return 58;
    }
    if (Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::Active,
            true, true, true, true)
            != Horse::RollbackNativeTerminalGekkoPump::DrainControlOnly
        || Horse::DecideRollbackNativeTerminalGekkoPump(
            Horse::RollbackRoundPhase::ConfirmingRoundEnd,
            true, true, true, true)
            != Horse::RollbackNativeTerminalGekkoPump::DrainControlOnly)
    {
        // Once the pair terminal is immutable, no native entry may admit
        // another Gekko correction. Only authenticated control traffic drains.
        return 59;
    }
    using Horse::RollbackNativeTerminalBarrierAction;
    using Horse::RollbackNativeTerminalGate;
    using Horse::RollbackNativeTerminalProducerDisposition;
    using Horse::RollbackNativeTerminalTransitionDisposition;
    using Horse::RollbackRoundPhase;

    Horse::RollbackNativeRoundWinDiagnostics win_state {};
    for (uint32_t slot = 0; slot < 2; ++slot)
    {
        win_state.fighter_valid[slot] = true;
        win_state.wins_read[slot] = true;
        win_state.rounds_to_win_read[slot] = true;
    }
    win_state.rounds_to_win[0] = 2;
    win_state.rounds_to_win[1] = 3;
    win_state.wins[0] = 1;
    win_state.wins[1] = 2;
    if (!Horse::RollbackNativeRoundWinDiagnosticsValid(win_state)
        || Horse::RollbackNativeRoundWinDiagnosticsMatchComplete(win_state))
        return 31;
    auto asymmetric_match = win_state;
    asymmetric_match.wins[1] = 3;
    if (!Horse::RollbackNativeRoundWinDiagnosticsMatchComplete(
            asymmetric_match))
        return 31;
    for (uint32_t slot = 0; slot < 2; ++slot)
    {
        auto missing_fighter = win_state;
        missing_fighter.fighter_valid[slot] = false;
        auto missing_wins = win_state;
        missing_wins.wins_read[slot] = false;
        auto missing_threshold = win_state;
        missing_threshold.rounds_to_win_read[slot] = false;
        auto zero_threshold = win_state;
        zero_threshold.rounds_to_win[slot] = 0;
        if (Horse::RollbackNativeRoundWinDiagnosticsValid(missing_fighter)
            || Horse::RollbackNativeRoundWinDiagnosticsValid(missing_wins)
            || Horse::RollbackNativeRoundWinDiagnosticsValid(
                missing_threshold)
            || Horse::RollbackNativeRoundWinDiagnosticsValid(zero_threshold))
            return 31;
    }

    const Horse::RollbackFrameStamp invalid_frame {};
    const Horse::RollbackFrameStamp frame_10 =
        Horse::RollbackFrameStamp::From(10);
    const Horse::RollbackFrameStamp frame_11 =
        Horse::RollbackFrameStamp::From(11);
    if (Horse::ClassifyRollbackNativeTerminalProducerInvocation(
            false, RollbackRoundPhase::Active, frame_10,
            invalid_frame, false)
            != RollbackNativeTerminalProducerDisposition::PassThrough
        || Horse::ClassifyRollbackNativeTerminalProducerInvocation(
            true, RollbackRoundPhase::Active, frame_10,
            invalid_frame, false)
            != RollbackNativeTerminalProducerDisposition::Defer
        || Horse::ClassifyRollbackNativeTerminalProducerInvocation(
            true, RollbackRoundPhase::ConfirmingRoundEnd, frame_10,
            frame_10, false)
            != RollbackNativeTerminalProducerDisposition::Defer
        || Horse::ClassifyRollbackNativeTerminalProducerInvocation(
            true, RollbackRoundPhase::ConfirmingRoundEnd, frame_11,
            frame_10, false)
            != RollbackNativeTerminalProducerDisposition::Reject
        || Horse::ClassifyRollbackNativeTerminalProducerInvocation(
            true, RollbackRoundPhase::TerminalAccepted, frame_10,
            frame_10, true)
            != RollbackNativeTerminalProducerDisposition::Reject)
        return 25;

    if (Horse::RollbackShouldDiscardDeferredTerminalNotification(
            invalid_frame, 9)
        || !Horse::RollbackShouldDiscardDeferredTerminalNotification(
            frame_10, 9)
        || Horse::RollbackShouldDiscardDeferredTerminalNotification(
            frame_10, 10)
        || Horse::RollbackShouldDiscardDeferredTerminalNotification(
            frame_10, 11)
        || !Horse::RollbackShouldDiscardDeferredTerminalNotification(
            Horse::RollbackFrameStamp::From(0), UINT32_MAX)
        || Horse::RollbackShouldDiscardDeferredTerminalNotification(
            Horse::RollbackFrameStamp::From(UINT32_MAX), 0))
        return 26;

    Horse::RollbackNativeSimulationIterationToken restored_before_terminal {};
    restored_before_terminal.valid = true;
    restored_before_terminal.input_callbacks = {
        0x1000, 1, 0, 0x1111, 0x2222};
    restored_before_terminal.simulation_callbacks = {
        0x2000, 2, 0, 0x3333, 0x4444};
    restored_before_terminal.loop_again = 0;
    restored_before_terminal.pending_dispatch = 0;
    restored_before_terminal.unpause_grace_period = 0;
    auto restored_terminal_frame = restored_before_terminal;
    restored_terminal_frame.unpause_grace_period =
        Horse::kRollbackNativeDeferredTerminalGraceStart;
    const bool pending_after_earlier_load =
        !Horse::RollbackShouldDiscardDeferredTerminalNotification(
            frame_10, 9);
    const bool pending_after_equal_load =
        !Horse::RollbackShouldDiscardDeferredTerminalNotification(
            frame_10, 10);
    if (pending_after_earlier_load
        || !Horse::ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
            restored_before_terminal, pending_after_earlier_load)
        || !pending_after_equal_load
        || !Horse::ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
            restored_terminal_frame, pending_after_equal_load))
        return 30;

    if (Horse::DecideRollbackNativeTerminalBarrierAction(
            RollbackRoundPhase::ConfirmingRoundEnd, false, false)
        != RollbackNativeTerminalBarrierAction::None)
        return 1;
    if (Horse::DecideRollbackNativeTerminalBarrierAction(
            RollbackRoundPhase::ConfirmingRoundEnd, true, false)
        != RollbackNativeTerminalBarrierAction::Service)
        return 2;
    if (Horse::DecideRollbackNativeTerminalBarrierAction(
            RollbackRoundPhase::ConfirmingRoundEnd, true, true)
        != RollbackNativeTerminalBarrierAction::Promote)
        return 3;
    if (Horse::DecideRollbackNativeTerminalBarrierAction(
            RollbackRoundPhase::TerminalAccepted, true, true)
        != RollbackNativeTerminalBarrierAction::Hold)
        return 4;

    uint32_t terminal_reconcile_order = 0;
    if (!Horse::ReconcileRollbackTerminalPresentationAfterLoad(
            false,
            [&]() noexcept {
                terminal_reconcile_order = 1;
                return true;
            },
            [&]() noexcept {
                if (terminal_reconcile_order != 1) return false;
                terminal_reconcile_order = 2;
                return true;
            })
        || terminal_reconcile_order != 2)
        return 34;
    uint32_t blocked_model_calls = 0;
    if (Horse::ReconcileRollbackTerminalPresentationAfterLoad(
            false, []() noexcept { return false; },
            [&]() noexcept {
                ++blocked_model_calls;
                return true;
            })
        || blocked_model_calls != 0)
        return 34;
    uint32_t bypassed_reconcile_calls = 0;
    if (!Horse::ReconcileRollbackTerminalPresentationAfterLoad(
            true,
            [&]() noexcept {
                ++bypassed_reconcile_calls;
                return false;
            },
            [&]() noexcept {
                ++bypassed_reconcile_calls;
                return false;
            })
        || bypassed_reconcile_calls != 0)
        return 34;

    RollbackNativeTerminalGate gate;
    if (!gate.allow_simulation_loop(RollbackRoundPhase::Active)
        || !gate.allow_simulation_loop(
            RollbackRoundPhase::ConfirmingRoundEnd))
        return 5;
    if (!gate.filter_round_over_result(
            RollbackRoundPhase::Active, false))
    {
        // A native false stays false. This branch is intentionally empty.
    }
    else
        return 6;
    if (gate.filter_round_over_result(
            RollbackRoundPhase::ConfirmingRoundEnd, true)
        || gate.report().round_over_true_results_suppressed != 1)
        return 7;
    // After an ordinary round enters StockInterRound, SC6 owns the predicate.
    if (!gate.filter_round_over_result(
            RollbackRoundPhase::StockInterRound, true)
        || gate.filter_round_over_result(
            RollbackRoundPhase::StockInterRound, false)
        || gate.report().round_over_true_results_suppressed != 1)
        return 31;
    // MatchComplete is selected from the pair-confirmed win state before the
    // inter-round phase. Do not suppress SC6's real match-complete decision.
    if (!gate.filter_round_over_result(
            RollbackRoundPhase::MatchComplete, true)
        || gate.report().round_over_true_results_suppressed != 1)
        return 33;

    // The SimulationLoop's track-complete callback writes deterministic
    // BattleManager scalars and notifies the external UI game-flow manager.
    // The external notification is deferred through speculative execution
    // and released once only after bilateral terminal acceptance.
    if (!gate.defer_game_flow_notification(RollbackRoundPhase::Active)
        || !gate.defer_game_flow_notification(
            RollbackRoundPhase::ConfirmingRoundEnd)
        || !gate.game_flow_notification_pending()
        || gate.report().game_flow_notifications_suppressed != 2)
        return 20;
    if (!gate.can_discard_game_flow_notification()
        || !gate.discard_game_flow_notification()
        || gate.game_flow_notification_pending()
        || !gate.defer_game_flow_notification(
            RollbackRoundPhase::ConfirmingRoundEnd))
        return 24;
    uint32_t producer_calls = 0;
    bool producer_ran_before_release_publication = false;
    if (!gate.release_terminal_handoff(
            true, [&]() noexcept {
                producer_ran_before_release_publication =
                    gate.game_flow_notification_pending()
                    && gate.report().game_flow_notifications_released == 0
                    && gate.report().terminal_handoffs_released == 0
                    && !gate.terminal_handoff_released();
                ++producer_calls;
                return true;
            })
        || gate.game_flow_notification_pending()
        || gate.report().game_flow_notifications_released != 1
        || gate.report().terminal_handoffs_released != 1
        || !gate.terminal_handoff_released()
        || producer_calls != 1
        || !producer_ran_before_release_publication)
        return 21;

    // TerminalAccepted freezes the complete SimulationLoop. After the
    // deferred native game-flow notification is released once, an ordinary
    // per-round transition leaves the match-complete predicate false.
    if (gate.allow_simulation_loop(RollbackRoundPhase::TerminalAccepted))
        return 8;
    if (gate.filter_round_over_result(
            RollbackRoundPhase::TerminalAccepted, false))
        return 10;
    if (!gate.waiting_for_native_transition()) return 11;
    if (!gate.observe_native_transition(true)
        || gate.waiting_for_native_transition())
        return 12;
    if (gate.report().terminal_handoffs_released != 1
        || gate.report().round_over_predicate_releases != 0
        || gate.report().native_transitions_observed != 1)
        return 13;

    // Timeout completion is produced by the outer IsRoundOver query and does
    // not enter the track-complete notification function. It must still arm
    // the exactly-once native transition without inventing a producer call.
    RollbackNativeTerminalGate timeout_handoff;
    uint32_t timeout_producer_calls = 0;
    if (!timeout_handoff
            .release_terminal_handoff(
                true, [&]() noexcept {
                    ++timeout_producer_calls;
                    return true;
                })
        || timeout_handoff.report().failed
        || timeout_producer_calls != 0
        || timeout_handoff.report()
                .handoffs_without_game_flow_notification != 1
        || timeout_handoff.report().terminal_handoffs_released != 1
        || !timeout_handoff.waiting_for_native_transition()
        || !timeout_handoff.filter_round_over_result(
            RollbackRoundPhase::TerminalAccepted, true))
        return 22;

    RollbackNativeTerminalGate repeated_notification;
    if (!repeated_notification.defer_game_flow_notification(
            RollbackRoundPhase::Active)
        || !repeated_notification
            .release_terminal_handoff(
                true, []() noexcept { return true; })
        || repeated_notification
            .release_terminal_handoff(
                true, []() noexcept { return true; })
        || repeated_notification.can_discard_game_flow_notification()
        || !repeated_notification.report().failed)
        return 23;

    RollbackNativeTerminalGate unavailable_producer;
    uint32_t unavailable_calls = 0;
    if (!unavailable_producer.defer_game_flow_notification(
            RollbackRoundPhase::Active)
        || unavailable_producer
            .release_terminal_handoff(
                false, [&]() noexcept {
                    ++unavailable_calls;
                    return true;
                })
        || unavailable_calls != 0
        || !unavailable_producer.game_flow_notification_pending()
        || unavailable_producer.report().game_flow_notifications_released != 0
        || unavailable_producer.report().terminal_handoffs_released != 0
        || std::strcmp(unavailable_producer.report().failure,
            "native-terminal-notification-original-missing") != 0)
        return 27;

    RollbackNativeTerminalGate failed_producer;
    uint32_t failed_calls = 0;
    if (!failed_producer.defer_game_flow_notification(
            RollbackRoundPhase::Active)
        || failed_producer
            .release_terminal_handoff(
                true, [&]() noexcept {
                    ++failed_calls;
                    return false;
                })
        || failed_calls != 1
        || !failed_producer.game_flow_notification_pending()
        || failed_producer.report().game_flow_notifications_released != 0
        || failed_producer.report().terminal_handoffs_released != 0
        || std::strcmp(failed_producer.report().failure,
            "native-terminal-notification-original-failed") != 0)
        return 28;

    uint32_t rejected_reentry_calls = 0;
    const auto reentry =
        Horse::ClassifyRollbackNativeTerminalProducerInvocation(
            true, RollbackRoundPhase::TerminalAccepted, frame_10,
            frame_10, true);
    if (reentry == RollbackNativeTerminalProducerDisposition::PassThrough)
        ++rejected_reentry_calls;
    if (reentry != RollbackNativeTerminalProducerDisposition::Reject
        || rejected_reentry_calls != 0)
        return 29;

    // Match completion is the only case where SC6's original predicate is
    // expected to return true after the accepted producer handoff.
    RollbackNativeTerminalGate repeated_predicate;
    if (!repeated_predicate.defer_game_flow_notification(
            RollbackRoundPhase::Active)
        || !repeated_predicate
            .release_terminal_handoff(
                true, []() noexcept { return true; })
        || !repeated_predicate.filter_round_over_result(
            RollbackRoundPhase::TerminalAccepted, true)
        || repeated_predicate.filter_round_over_result(
            RollbackRoundPhase::TerminalAccepted, true)
        || !repeated_predicate.report().failed)
        return 17;

    if (Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 3,
            (1u << 5) | (1u << 8) | (1u << 10), false, 0)
            != RollbackNativeTerminalTransitionDisposition::OrdinaryRound
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 3, 3,
            (1u << 5) | (1u << 8) | (1u << 9) | (1u << 10), true, 1)
            != RollbackNativeTerminalTransitionDisposition::MatchComplete
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 3, 3,
            (1u << 5) | (1u << 8) | (1u << 9) | (1u << 10), false, 0)
            != RollbackNativeTerminalTransitionDisposition::Invalid
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 3,
            (1u << 5) | (1u << 8) | (1u << 10), true, 1)
            != RollbackNativeTerminalTransitionDisposition::Invalid
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 3,
            (1u << 5) | (1u << 8) | (1u << 10), true, 0)
            != RollbackNativeTerminalTransitionDisposition::MatchComplete
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 3, 3,
            (1u << 5) | (1u << 8) | (1u << 9) | (1u << 10), true, 0)
            != RollbackNativeTerminalTransitionDisposition::Invalid
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 1, 2, 1, 0, false, 0)
            != RollbackNativeTerminalTransitionDisposition::Waiting
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 1, 2, 1, 0, false, 1)
            != RollbackNativeTerminalTransitionDisposition::Invalid)
        return 18;

    // Standard PvP in-place rearm: the owned Lux frame commits the next
    // ordinal while BattleManager main/status remain active. This is the
    // observed timeout path for MoveProvider +0x150 == 1.
    if (Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 1, 1u << 8, false, 0)
            != RollbackNativeTerminalTransitionDisposition::OrdinaryRound
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 1, 1u << 8, false, 1)
            != RollbackNativeTerminalTransitionDisposition::Invalid
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 1, 1u << 8, true, 0)
            != RollbackNativeTerminalTransitionDisposition::MatchComplete
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 1, 1u << 8, true, 1)
            != RollbackNativeTerminalTransitionDisposition::Invalid
        || Horse::ClassifyRollbackNativeTerminalTransition(
            1, 2, 1, 2, 2, 2, 1u << 8, false, 0)
            != RollbackNativeTerminalTransitionDisposition::Invalid)
        return 35;

    RollbackNativeTerminalGate reusable_rounds;
    uint32_t reusable_producer_calls = 0;
    for (uint32_t round = 0; round < 2; ++round)
    {
        if (round != 0) reusable_rounds.begin_round();
        if (!reusable_rounds.defer_game_flow_notification(
                RollbackRoundPhase::Active)
            || !reusable_rounds
                .release_terminal_handoff(
                    true, [&]() noexcept {
                        ++reusable_producer_calls;
                        return true;
                    })
            || reusable_rounds.filter_round_over_result(
                RollbackRoundPhase::TerminalAccepted, false)
            || !reusable_rounds.observe_native_transition(true))
            return 19;
    }
    if (reusable_rounds.report().terminal_handoffs_released != 2
        || reusable_rounds.report().game_flow_notifications_released != 2
        || reusable_rounds.report().round_over_predicate_releases != 0
        || reusable_producer_calls != 2)
        return 19;

    RollbackNativeTerminalGate reset_gate;
    if (!reset_gate.defer_game_flow_notification(RollbackRoundPhase::Active)
        || !reset_gate.release_terminal_handoff(
            true, []() noexcept { return true; })
        || !reset_gate.filter_round_over_result(
            RollbackRoundPhase::TerminalAccepted, true)
        || reset_gate.report().round_over_predicate_releases != 1)
        return 32;
    reset_gate.reset();
    if (reset_gate.report().round_over_predicate_releases != 0
        || reset_gate.report().terminal_handoffs_released != 0
        || reset_gate.waiting_for_native_transition()
        || reset_gate.terminal_handoff_released())
        return 32;

    std::puts("rollback native terminal gate self-test passed");
    return 0;
}
