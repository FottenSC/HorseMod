// Native SC6 round-result handoff gate.
//
// Rollback never reconstructs the native round-state sequence. The complete
// SimulationLoop runs as rollback state. When its track-complete branch emits
// an external game-flow notification, that notification is held until the pair
// accepts the terminal state and then released exactly once. Timeout completion
// is detected later by the outer IsRoundOver query and has no track-complete
// notification to defer. Both paths arm the later native IsRoundOver result
// only after acceptance. Once the pair-confirmed ordinary round has entered
// StockInterRound, SC6 owns that native predicate again.
#pragma once

#include "RollbackFrameStamp.hpp"
#include "RollbackRoundCoordinator.hpp"

#include <cstdint>

namespace Horse
{
    enum class RollbackNativeRoundStateQueueDisposition : uint8_t
    {
        NoTransition,
        TerminalTransition,
        UnexpectedTransition,
        OtherTransition,
        Invalid,
    };

    enum class RollbackNativeTerminalBarrierAction : uint8_t
    {
        None,
        Service,
        Promote,
        Hold,
    };

    enum class RollbackNativeTerminalProducerDisposition : uint8_t
    {
        PassThrough,
        Defer,
        Reject,
    };

    enum class RollbackNativeTerminalTransitionDisposition : uint8_t
    {
        Waiting,
        OrdinaryRound,
        MatchComplete,
        Invalid,
    };

    struct RollbackNativeRoundWinDiagnostics
    {
        uint32_t wins[2] {};
        uint32_t rounds_to_win[2] {};
        bool fighter_valid[2] {};
        bool wins_read[2] {};
        bool rounds_to_win_read[2] {};
    };

    inline bool RollbackNativeRoundWinDiagnosticsValid(
        const RollbackNativeRoundWinDiagnostics& value) noexcept
    {
        for (uint32_t slot = 0; slot < 2; ++slot)
        {
            if (!value.fighter_valid[slot]
                || !value.wins_read[slot]
                || !value.rounds_to_win_read[slot]
                || value.rounds_to_win[slot] == 0)
            {
                return false;
            }
        }
        return true;
    }

    inline bool RollbackNativeRoundWinDiagnosticsMatchComplete(
        const RollbackNativeRoundWinDiagnostics& value) noexcept
    {
        // SC6 stores a threshold on each fighter. They normally match, but
        // classification uses each native value independently rather than
        // inventing an equality requirement.
        return RollbackNativeRoundWinDiagnosticsValid(value)
            && ((value.wins[0] >= value.rounds_to_win[0])
                || (value.wins[1] >= value.rounds_to_win[1]));
    }

    inline RollbackNativeTerminalProducerDisposition
    ClassifyRollbackNativeTerminalProducerInvocation(
        bool production_gate_active,
        RollbackRoundPhase phase,
        const RollbackFrameStamp& invocation_frame,
        const RollbackFrameStamp& pending_frame,
        bool already_released) noexcept
    {
        if (!production_gate_active)
            return RollbackNativeTerminalProducerDisposition::PassThrough;
        if (phase == RollbackRoundPhase::TerminalAccepted
            || phase == RollbackRoundPhase::FatalFrozen)
        {
            return RollbackNativeTerminalProducerDisposition::Reject;
        }
        if (phase != RollbackRoundPhase::Active
            && phase != RollbackRoundPhase::ConfirmingRoundEnd)
        {
            return RollbackNativeTerminalProducerDisposition::PassThrough;
        }
        if (!invocation_frame.valid || already_released)
            return RollbackNativeTerminalProducerDisposition::Reject;
        if (pending_frame.valid
            && pending_frame.value != invocation_frame.value)
        {
            return RollbackNativeTerminalProducerDisposition::Reject;
        }
        return RollbackNativeTerminalProducerDisposition::Defer;
    }

    inline bool RollbackShouldDiscardDeferredTerminalNotification(
        const RollbackFrameStamp& producer_frame,
        uint32_t loaded_frame) noexcept
    {
        // Save F is captured after Advance F. A Load of F therefore retains
        // an effect authored by F; only effects strictly after F are removed.
        return producer_frame.valid
            && RollbackFrameIsAfter(producer_frame.value, loaded_frame);
    }

    inline RollbackNativeTerminalBarrierAction
    DecideRollbackNativeTerminalBarrierAction(
        RollbackRoundPhase phase,
        bool terminal_quiesced,
        bool barrier_complete) noexcept
    {
        if (!terminal_quiesced)
            return RollbackNativeTerminalBarrierAction::None;
        if (!barrier_complete)
            return RollbackNativeTerminalBarrierAction::Service;
        if (phase == RollbackRoundPhase::Active
            || phase == RollbackRoundPhase::ConfirmingRoundEnd)
        {
            return RollbackNativeTerminalBarrierAction::Promote;
        }
        return RollbackNativeTerminalBarrierAction::Hold;
    }

    // This classifier is diagnostic only. The queue is captured/restored as
    // ordinary rollback state; it is never held or manually consumed.
    inline RollbackNativeRoundStateQueueDisposition
    ClassifyRollbackNativeRoundStateQueue(
        uint8_t current_state,
        const uint8_t* entries,
        int32_t count,
        int32_t capacity) noexcept
    {
        constexpr int32_t kMaxRoundStateEntries = 32;
        if (count < 0 || capacity < 0 || count > capacity
            || count > kMaxRoundStateEntries
            || (count != 0 && entries == nullptr))
        {
            return RollbackNativeRoundStateQueueDisposition::Invalid;
        }
        bool transitioned = false;
        uint32_t terminal_transitions = 0;
        bool other_transition = false;
        bool unexpected_active_exit = false;
        uint8_t simulated_state = current_state;
        for (int32_t index = 0; index < count; ++index)
        {
            const uint8_t next_state = entries[index];
            if (next_state == simulated_state) continue;
            transitioned = true;
            if (simulated_state == 2 && next_state == 3)
                ++terminal_transitions;
            else
            {
                other_transition = true;
                if (simulated_state == 2)
                    unexpected_active_exit = true;
            }
            simulated_state = next_state;
        }
        if (unexpected_active_exit || terminal_transitions > 1
            || (terminal_transitions != 0 && other_transition))
            return RollbackNativeRoundStateQueueDisposition::
                UnexpectedTransition;
        if (terminal_transitions == 1)
            return RollbackNativeRoundStateQueueDisposition::TerminalTransition;
        return transitioned
            ? RollbackNativeRoundStateQueueDisposition::OtherTransition
            : RollbackNativeRoundStateQueueDisposition::NoTransition;
    }

    inline bool RollbackDiscardedJournaledTerminalEdge(
        bool edge_seen,
        bool edge_frame_valid,
        uint32_t edge_frame,
        uint32_t loaded_frame) noexcept
    {
        return edge_seen && edge_frame_valid
            && RollbackFrameIsAfter(edge_frame, loaded_frame);
    }

    template <typename ReconcileStageFn, typename ReconcileModelFn>
    inline bool ReconcileRollbackTerminalPresentationAfterLoad(
        bool native_correction_only,
        ReconcileStageFn&& reconcile_stage,
        ReconcileModelFn&& reconcile_model) noexcept
    {
        if (native_correction_only) return true;
        return reconcile_stage() && reconcile_model();
    }

    inline RollbackNativeTerminalTransitionDisposition
    ClassifyRollbackNativeTerminalTransition(
        uint32_t expected_round_ordinal,
        uint8_t expected_main_state,
        uint8_t expected_battle_status,
        uint32_t live_round_ordinal,
        uint8_t live_main_state,
        uint8_t live_battle_status,
        uint32_t lifecycle_mismatch_mask,
        bool match_complete,
        uint64_t predicate_releases) noexcept
    {
        constexpr uint32_t kRoundStartMismatch = 1u << 5;
        constexpr uint32_t kRoundOrdinalMismatch = 1u << 8;
        constexpr uint32_t kMainStateMismatch = 1u << 9;
        constexpr uint32_t kBattleStatusMismatch = 1u << 10;
        constexpr uint32_t kAllowedMismatchMask =
            kRoundStartMismatch | kRoundOrdinalMismatch
            | kMainStateMismatch | kBattleStatusMismatch;
        const bool unchanged = live_round_ordinal == expected_round_ordinal
            && live_main_state == expected_main_state
            && live_battle_status == expected_battle_status
            && lifecycle_mismatch_mask == 0;
        if (unchanged)
        {
            return predicate_releases == 0
                ? RollbackNativeTerminalTransitionDisposition::Waiting
                : RollbackNativeTerminalTransitionDisposition::Invalid;
        }
        // Standard PvP can commit an ordinary round inside the owned Lux
        // simulation before the outer BattleManager IsRoundOver query. The
        // MoveProvider +0x150 == 1 branch rearms in place: only the round
        // ordinal advances while main state and battle status remain active.
        // No native round-over predicate has been released on this path.
        const bool in_place_round_rearm =
            expected_main_state == 2
            && (expected_battle_status == 1
                || expected_battle_status == 2)
            && live_main_state == expected_main_state
            && live_battle_status == expected_battle_status
            && (live_round_ordinal & 0xFFFFu)
                == ((expected_round_ordinal + 1u) & 0xFFFFu)
            && lifecycle_mismatch_mask == kRoundOrdinalMismatch;
        if (in_place_round_rearm)
        {
            if (predicate_releases != 0)
                return RollbackNativeTerminalTransitionDisposition::Invalid;
            // The same in-place native shape is used for the winning round.
            // Pair-confirmed win diagnostics, not a guessed BattleManager
            // state change, distinguish match completion from ordinary rearm.
            return match_complete
                ? RollbackNativeTerminalTransitionDisposition::MatchComplete
                : RollbackNativeTerminalTransitionDisposition::OrdinaryRound;
        }
        const bool transition_shape = expected_main_state == 2
            && (expected_battle_status == 1
                || expected_battle_status == 2)
            && live_battle_status == 3
            && (live_round_ordinal & 0xFFFFu)
                == ((expected_round_ordinal + 1u) & 0xFFFFu)
            && (lifecycle_mismatch_mask & kRoundOrdinalMismatch) != 0
            && (lifecycle_mismatch_mask & ~kAllowedMismatchMask) == 0;
        if (!transition_shape)
            return RollbackNativeTerminalTransitionDisposition::Invalid;
        if (match_complete)
        {
            // SC6's match-complete producer commits status 3 and the next
            // ordinal while leaving BattleMainState at 2. Unlike an ordinary
            // round, the completed-match path does not call the guarded
            // round-over predicate again. The win diagnostics plus this
            // lifecycle shape are therefore the final native commit.
            if (live_main_state == expected_main_state
                && (lifecycle_mismatch_mask & kMainStateMismatch) == 0
                && predicate_releases == 0)
            {
                return RollbackNativeTerminalTransitionDisposition::
                    MatchComplete;
            }
            // Retain the previously characterized alternate shape for a
            // native path which does advance BattleMainState through the
            // exactly-once predicate release.
            return live_main_state == 3
                    && (lifecycle_mismatch_mask & kMainStateMismatch) != 0
                    && predicate_releases == 1
                ? RollbackNativeTerminalTransitionDisposition::MatchComplete
                : RollbackNativeTerminalTransitionDisposition::Invalid;
        }
        return live_main_state == expected_main_state
                && (lifecycle_mismatch_mask & kMainStateMismatch) == 0
                && predicate_releases == 0
            ? RollbackNativeTerminalTransitionDisposition::OrdinaryRound
            : RollbackNativeTerminalTransitionDisposition::Invalid;
    }

    struct RollbackNativeTerminalGateReport
    {
        uint64_t simulation_calls_allowed {0};
        uint64_t simulation_calls_suppressed {0};
        uint64_t terminal_handoffs_released {0};
        uint64_t native_transitions_observed {0};
        uint64_t round_over_true_results_suppressed {0};
        uint64_t round_over_predicate_releases {0};
        uint64_t game_flow_notifications_suppressed {0};
        uint64_t game_flow_notifications_released {0};
        uint64_t handoffs_without_game_flow_notification {0};
        bool release_pending_native_transition {false};
        bool failed {false};
        const char* failure {"ok"};
    };

    enum class RollbackNativeTerminalGekkoPump : uint8_t
    {
        OrdinaryUpdate,
        CorrectionFlush,
        DrainControlOnly,
    };

    static inline RollbackNativeTerminalGekkoPump
    DecideRollbackNativeTerminalGekkoPump(
        RollbackRoundPhase phase,
        bool terminal_edge_seen,
        bool terminal_overlap_complete,
        bool terminal_proposal_active,
        bool terminal_agreed) noexcept
    {
        const bool rollback_owned =
            phase == RollbackRoundPhase::Active
            || phase == RollbackRoundPhase::ConfirmingRoundEnd;
        if (rollback_owned && terminal_agreed)
            return RollbackNativeTerminalGekkoPump::DrainControlOnly;
        return rollback_owned && terminal_edge_seen
                && (terminal_overlap_complete || terminal_proposal_active)
            ? RollbackNativeTerminalGekkoPump::CorrectionFlush
            : RollbackNativeTerminalGekkoPump::OrdinaryUpdate;
    }

    class RollbackNativeTerminalGate
    {
    public:
        void reset() noexcept
        {
            m_report = {};
            begin_round();
        }

        void begin_round() noexcept
        {
            m_round_handoff_released = false;
            m_round_transition_pending = false;
            m_round_over_predicate_released = false;
            m_game_flow_notification_pending = false;
            m_game_flow_notification_released = false;
            m_report.release_pending_native_transition = false;
        }

        bool defer_game_flow_notification(
            RollbackRoundPhase phase) noexcept
        {
            if (phase != RollbackRoundPhase::Active
                && phase != RollbackRoundPhase::ConfirmingRoundEnd)
                return fail("native-terminal-notification-out-of-phase");
            if (m_game_flow_notification_released)
                return fail("native-terminal-notification-after-release");
            m_game_flow_notification_pending = true;
            ++m_report.game_flow_notifications_suppressed;
            return true;
        }

        bool game_flow_notification_pending() const noexcept
        {
            return m_game_flow_notification_pending;
        }

        bool can_discard_game_flow_notification() const noexcept
        {
            return !m_game_flow_notification_released;
        }

        bool discard_game_flow_notification() noexcept
        {
            if (m_game_flow_notification_released)
                return fail("native-terminal-notification-discard-after-release");
            m_game_flow_notification_pending = false;
            return true;
        }

        template <typename InvokeFn>
        bool release_terminal_handoff(
            bool producer_available,
            InvokeFn&& invoke_original) noexcept
        {
            if (m_round_handoff_released)
                return fail("native-terminal-predicate-arm-repeated");
            if (m_game_flow_notification_pending)
            {
                if (!producer_available)
                    return fail(
                        "native-terminal-notification-original-missing");
                if (m_game_flow_notification_released)
                    return fail(
                        "native-terminal-notification-release-repeated");
                // The native producer is void in SC6. The injectable bool
                // return exists only so the hermetic route can prove that a
                // failed call cannot publish release counters or arm the
                // predicate.
                if (!invoke_original())
                    return fail(
                        "native-terminal-notification-original-failed");
                m_game_flow_notification_pending = false;
                m_game_flow_notification_released = true;
                ++m_report.game_flow_notifications_released;
            }
            else
            {
                // A stock timeout reaches IsRoundOver without entering the
                // track-complete notification producer. This is an explicit
                // native path, not evidence that a detour was missed.
                ++m_report.handoffs_without_game_flow_notification;
            }
            m_round_handoff_released = true;
            m_round_transition_pending = true;
            m_report.release_pending_native_transition = true;
            ++m_report.terminal_handoffs_released;
            return true;
        }

        bool allow_simulation_loop(RollbackRoundPhase phase) noexcept
        {
            const bool allow = phase != RollbackRoundPhase::TerminalAccepted
                && phase != RollbackRoundPhase::FatalFrozen;
            if (allow)
                ++m_report.simulation_calls_allowed;
            else
                ++m_report.simulation_calls_suppressed;
            return allow;
        }

        bool terminal_handoff_released() const noexcept
        {
            return m_round_handoff_released;
        }

        bool filter_round_over_result(
            RollbackRoundPhase phase,
            bool original_result) noexcept
        {
            const bool terminal_phase =
                phase == RollbackRoundPhase::Active
                || phase == RollbackRoundPhase::ConfirmingRoundEnd
                || phase == RollbackRoundPhase::TerminalAccepted;
            if (!terminal_phase) return original_result;

            const bool release_ready =
                phase == RollbackRoundPhase::TerminalAccepted
                && m_round_handoff_released;
            if (!original_result) return false;
            if (!release_ready)
            {
                ++m_report.round_over_true_results_suppressed;
                return false;
            }
            if (m_round_over_predicate_released)
            {
                fail("native-terminal-round-over-release-repeated");
                return false;
            }

            m_round_over_predicate_released = true;
            ++m_report.round_over_predicate_releases;
            return true;
        }

        bool observe_native_transition(bool transition_observed) noexcept
        {
            if (!m_round_handoff_released || !m_round_transition_pending)
                return transition_observed
                    ? fail("native-terminal-transition-without-predicate-release")
                    : true;
            if (!transition_observed) return true;
            m_round_transition_pending = false;
            m_report.release_pending_native_transition = false;
            ++m_report.native_transitions_observed;
            return true;
        }

        bool waiting_for_native_transition() const noexcept
        {
            return m_report.release_pending_native_transition;
        }

        const RollbackNativeTerminalGateReport& report() const noexcept
        {
            return m_report;
        }

    private:
        bool fail(const char* reason) noexcept
        {
            m_report.failed = true;
            m_report.failure = reason;
            return false;
        }

        RollbackNativeTerminalGateReport m_report {};
        bool m_round_handoff_released {false};
        bool m_round_transition_pending {false};
        bool m_round_over_predicate_released {false};
        bool m_game_flow_notification_pending {false};
        bool m_game_flow_notification_released {false};
    };
}
