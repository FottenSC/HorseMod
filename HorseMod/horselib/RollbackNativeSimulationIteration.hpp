// Minimal adapter for executing SC6's complete SimulationLoop trampoline once.
// Owned rollback iterations restore their scoped scheduling cursors after the
// call. A frozen pre-gameplay peer-wait control delta may retain successful
// native cursor progress until the already-captured baseline is restored.
// The InputLog cache remains owned by Steam/SC6.
#pragma once

#include "RollbackStateHash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace Horse
{
    static constexpr uint32_t
        kRollbackOwnedSimulationServiceGraceTicks = 3;

    // The stock BattleManager normally supplies SimulationLoop once per game
    // thread tick. It can stop doing so after Horse has owned several calls
    // without advancing SC6's source clock. Admit a service-side replacement
    // only after multiple consecutive game-thread observations with no
    // organic entry; once active, keep one replacement per service tick until
    // an organic entry resumes.
    static constexpr bool RollbackOwnedSimulationServiceTickAllowed(
        bool rollback_phase_active,
        bool hook_owns_tick,
        bool on_simulation_thread,
        bool manager_identity_valid,
        bool organic_entry_observed,
        bool fallback_active,
        uint32_t missed_service_ticks) noexcept
    {
        return rollback_phase_active
            && hook_owns_tick
            && on_simulation_thread
            && manager_identity_valid
            && !organic_entry_observed
            && (fallback_active
                || missed_service_ticks
                    >= kRollbackOwnedSimulationServiceGraceTicks);
    }

    enum class RollbackNativeCallbackSelectionStatus : uint8_t
    {
        Waiting,
        Match,
        StageMismatch,
        CharacterMismatch,
    };

    static constexpr RollbackNativeCallbackSelectionStatus
    ClassifyRollbackNativeCallbackSelection(
        uint32_t observed_stage_identity,
        uint32_t expected_stage_identity,
        uint64_t observed_selection_hash,
        uint64_t expected_selection_hash) noexcept
    {
        if (expected_stage_identity == 0 || expected_selection_hash == 0
            || observed_stage_identity == 0
            || observed_selection_hash == 0)
        {
            return RollbackNativeCallbackSelectionStatus::Waiting;
        }
        if (observed_stage_identity != expected_stage_identity)
            return RollbackNativeCallbackSelectionStatus::StageMismatch;
        if (observed_selection_hash != expected_selection_hash)
            return RollbackNativeCallbackSelectionStatus::CharacterMismatch;
        return RollbackNativeCallbackSelectionStatus::Match;
    }

    static constexpr bool RollbackNativeCallbackSelectionMatches(
        uint32_t observed_stage_identity,
        uint32_t expected_stage_identity,
        uint64_t observed_selection_hash,
        uint64_t expected_selection_hash) noexcept
    {
        return ClassifyRollbackNativeCallbackSelection(
            observed_stage_identity, expected_stage_identity,
            observed_selection_hash, expected_selection_hash)
            == RollbackNativeCallbackSelectionStatus::Match;
    }

    enum class RollbackStockSelectionBindingResult : uint8_t
    {
        Waiting,
        Bound,
        Match,
        Mismatch,
    };

    struct RollbackStockSelectionBinding
    {
        uint32_t native_stage_identity {0};
        uint64_t selection_hash {0};

        void reset() noexcept
        {
            native_stage_identity = 0;
            selection_hash = 0;
        }

        bool valid() const noexcept
        {
            return native_stage_identity != 0 && selection_hash != 0;
        }

        RollbackStockSelectionBindingResult observe(
            bool bind_observed,
            uint32_t configured_stage_identity,
            uint64_t configured_selection_hash,
            uint32_t observed_stage_identity,
            uint64_t observed_selection_hash) noexcept
        {
            const uint32_t stage = bind_observed
                ? observed_stage_identity : configured_stage_identity;
            const uint64_t selection = bind_observed
                ? observed_selection_hash : configured_selection_hash;
            if (stage == 0 || selection == 0)
                return RollbackStockSelectionBindingResult::Waiting;
            if (!valid())
            {
                native_stage_identity = stage;
                selection_hash = selection;
                return RollbackStockSelectionBindingResult::Bound;
            }
            return native_stage_identity == stage
                    && selection_hash == selection
                ? RollbackStockSelectionBindingResult::Match
                : RollbackStockSelectionBindingResult::Mismatch;
        }
    };

    struct RollbackNativeSimulationClock
    {
        int32_t input_log_last_frame {0};
        uint32_t input_log_master_clock {0};
        int32_t battle_last_frame {0};
        uint32_t battle_last_applied {0};
    };

    static inline bool PrepareRollbackNativeSingleIteration(
        const RollbackNativeSimulationClock& before,
        RollbackNativeSimulationClock& scoped) noexcept
    {
        if (before.input_log_master_clock == 0)
            return false;
        scoped = before;
        scoped.battle_last_frame = before.input_log_last_frame;
        scoped.battle_last_applied = before.input_log_master_clock - 1u;
        return true;
    }

    // Terminal consensus can hold the native SimulationLoop while the stock
    // InputLog master clock keeps advancing. Preserve exactly the newest
    // already-produced control delta and discard the older wall-time backlog.
    // SC6 needs that one native iteration to drain the queued result mode and
    // run its complete round-state sequence; retaining more would catch up
    // through unauthored gameplay, while retaining none stalls the stock pump.
    static inline bool PrepareRollbackNativeNoCatchUpHandoff(
        const RollbackNativeSimulationClock& before,
        RollbackNativeSimulationClock& aligned,
        uint32_t& discarded_frames) noexcept
    {
        if (before.battle_last_frame == before.input_log_last_frame
            && before.battle_last_applied > before.input_log_master_clock)
        {
            return false;
        }
        const uint32_t pending_frames = before.battle_last_frame
                == before.input_log_last_frame
            ? before.input_log_master_clock - before.battle_last_applied
            : before.input_log_master_clock;
        if (pending_frames == 0) return false;
        discarded_frames = pending_frames - 1u;
        aligned = before;
        aligned.battle_last_frame = before.input_log_last_frame;
        aligned.battle_last_applied = before.input_log_master_clock - 1u;
        return true;
    }

    static inline bool RollbackNativePendingDelta(
        const RollbackNativeSimulationClock& clock,
        uint32_t& pending_delta) noexcept
    {
        if (clock.battle_last_frame != clock.input_log_last_frame)
        {
            pending_delta = clock.input_log_master_clock;
            return true;
        }
        if (clock.battle_last_applied > clock.input_log_master_clock)
            return false;
        pending_delta =
            clock.input_log_master_clock - clock.battle_last_applied;
        return true;
    }

    // A peer can reach the same deterministic NewRound coordinate after a
    // longer wall-time stall than its partner. Bound that scheduling backlog
    // to the newest single coordinate before the audited native pass. This
    // prevents native catch-up from skipping countdown steps while retaining
    // the same coordinate and post-call validation used by the ordinary path.
    static inline bool PrepareRollbackNativeBoundedPassThrough(
        const RollbackNativeSimulationClock& before,
        uint32_t reported_pending,
        RollbackNativeSimulationClock& bounded,
        uint32_t& discarded_frames) noexcept
    {
        uint32_t observed_pending = 0;
        if (!RollbackNativePendingDelta(before, observed_pending)
            || observed_pending != reported_pending)
        {
            return false;
        }
        if (observed_pending <= 1u)
        {
            bounded = before;
            discarded_frames = 0;
            return true;
        }
        if (!PrepareRollbackNativeSingleIteration(before, bounded))
            return false;
        discarded_frames = observed_pending - 1u;
        return true;
    }

    // SC6 starts a new InputLog epoch by publishing a new frame identity with
    // master clock zero. The complete native SimulationLoop consumes that
    // identity change, resets the BattleManager cursor, and intentionally
    // executes no PerFrame call. This is native cursor synchronization, not a
    // simulated gameplay frame.
    static constexpr bool RollbackNativeNeedsZeroDeltaEpochSync(
        const RollbackNativeSimulationClock& clock) noexcept
    {
        return clock.input_log_last_frame != clock.battle_last_frame
            && clock.input_log_master_clock == 0;
    }

    static constexpr bool ValidateRollbackNativeZeroDeltaEpochSync(
        const RollbackNativeSimulationClock& before,
        const RollbackNativeSimulationClock& after,
        uint32_t pending_before, uint32_t pending_after,
        uint64_t native_calls, uint64_t per_frame_calls) noexcept
    {
        return RollbackNativeNeedsZeroDeltaEpochSync(before)
            && pending_before == 0
            && after.input_log_last_frame == before.input_log_last_frame
            && after.input_log_master_clock == 0
            && after.battle_last_frame == before.input_log_last_frame
            && after.battle_last_applied == 0
            && pending_after == 0
            && native_calls == 1
            && per_frame_calls == 0;
    }

    struct RollbackNativeZeroDeltaTransitionEvidence
    {
        bool terminal_secondary_captured {false};
        bool terminal_rng_captured {false};
        bool committed_secondary_captured {false};
        bool committed_rng_captured {false};
    };

    // The stock zero-delta call rolls the InputLog/BattleManager cursor and may
    // consume RNG, but normal-mode traces prove that the carried secondary
    // event history remains equal to the pair-confirmed RoundResult terminal
    // state. Require both sides to be readable. Runtime restores a mutated
    // secondary history transactionally; RNG remains under its independent
    // launch-baseline contract.
    static constexpr bool ValidateRollbackNativeZeroDeltaTransition(
        const RollbackNativeZeroDeltaTransitionEvidence& evidence) noexcept
    {
        return evidence.terminal_secondary_captured
            && evidence.terminal_rng_captured
            && evidence.committed_secondary_captured
            && evidence.committed_rng_captured;
    }

    enum class RollbackNativeZeroDeltaSecondaryAction : uint8_t
    {
        Preserve,
        RestoreTerminal,
        Reject,
    };

    static constexpr RollbackNativeZeroDeltaSecondaryAction
    ClassifyRollbackNativeZeroDeltaSecondaryAction(
        uint64_t target_generation,
        uint64_t carried_generation,
        bool terminal_valid,
        uint64_t terminal_hash,
        bool committed_valid,
        uint64_t committed_hash) noexcept
    {
        if (target_generation <= 1
            || carried_generation != target_generation
            || !terminal_valid || terminal_hash == 0
            || !committed_valid || committed_hash == 0)
        {
            return RollbackNativeZeroDeltaSecondaryAction::Reject;
        }
        return terminal_hash == committed_hash
            ? RollbackNativeZeroDeltaSecondaryAction::Preserve
            : RollbackNativeZeroDeltaSecondaryAction::RestoreTerminal;
    }

    enum class RollbackNativeInterRoundClockAction : uint8_t
    {
        Reject,
        ArmSingleIteration,
        RunZeroDeltaEpochSync,
    };

    enum class RollbackNativeInterRoundSchedulingAction : uint8_t
    {
        Reject,
        AlignTerminalBacklog,
        AwaitNativeClock,
        PassThroughNative,
        ControlSingleIteration,
    };

    static constexpr uint32_t kRollbackNativeInterRoundClockWaitLimit = 120;

    static constexpr RollbackNativeInterRoundSchedulingAction
    ClassifyRollbackNativeInterRoundSchedulingAction(
        bool stock_inter_round, bool terminal_backlog_aligned,
        bool new_round_control_call, uint32_t pending_delta) noexcept
    {
        if (!stock_inter_round)
            return RollbackNativeInterRoundSchedulingAction::Reject;
        if (!terminal_backlog_aligned)
            return RollbackNativeInterRoundSchedulingAction::
                AlignTerminalBacklog;
        // A zero master clock cannot be armed as one iteration without
        // underflowing BattleManager+0x148C. It is the native epoch boundary:
        // suppress this early SimulationLoop entry and let the independently
        // owned FrameInputLog actor tick publish the first delta. Do not write
        // +0x3A4 or invoke the actor pipeline a second time from this hook.
        if (pending_delta == 0)
            return RollbackNativeInterRoundSchedulingAction::
                AwaitNativeClock;
        // Preserve a naturally produced NewRound delta so the stock loop owns
        // the countdown step. Other inter-round modes are bounded through the
        // existing one-iteration clock adapter.
        if (!new_round_control_call)
        {
            return RollbackNativeInterRoundSchedulingAction::
                ControlSingleIteration;
        }
        return RollbackNativeInterRoundSchedulingAction::PassThroughNative;
    }

    static constexpr bool RollbackNativeZeroDeltaEpochSyncAdmissionAllowed(
        uint64_t current_round_generation,
        uint64_t admitted_sync_generation) noexcept
    {
        if (current_round_generation == 0
            || current_round_generation == UINT64_MAX)
        {
            return false;
        }
        // The rollover is role-local: a peer may skip any number of round
        // epochs before it next observes a native identity reset.  A claim at
        // or behind the active generation is therefore valid.  The claimed
        // target (current + 1) and any future value reject a duplicate before
        // native code runs.
        return admitted_sync_generation <= current_round_generation;
    }

    // One peer can observe the stock loop immediately after SC6 has already
    // consumed the zero-delta InputLog rollover, while the other peer observes
    // the identity change and consumes it through Horse's controlled call.
    // The former still needs the same transaction admission/commit bookkeeping
    // before its first real NewRound coordinate. Its already-committed gameplay
    // and RNG state must be observed, not rewritten. An equal InputLog/Battle
    // identity proves the rollover is complete. Depending on which native
    // callback published that identity, the first Horse observation can have
    // either zero pending frames (before coordinate 1 is scheduled) or one
    // pending frame (coordinate 1 has just become due). Both observations must
    // commit the rollover before any countdown coordinate is admitted.
    static constexpr bool
    RollbackNativeNeedsConsumedZeroDeltaCommitObservation(
        bool stock_inter_round,
        bool new_round_control_call,
        const RollbackNativeSimulationClock& clock,
        uint32_t pending_delta,
        uint32_t countdown_coordinate,
        uint64_t current_round_generation,
        uint64_t admitted_sync_generation) noexcept
    {
        return stock_inter_round
            && new_round_control_call
            // An already-consumed rollover is only observable before the
            // first deterministic countdown step. If identity equality first
            // appears at coordinate 2, coordinate 1 performed the rollover as
            // part of its admitted native iteration; trying to retroactively
            // admit a zero-delta transaction would duplicate completed work.
            && countdown_coordinate == 1
            && current_round_generation != 0
            && current_round_generation != UINT64_MAX
            && admitted_sync_generation <= current_round_generation
            && !RollbackNativeNeedsZeroDeltaEpochSync(clock)
            && clock.input_log_last_frame == clock.battle_last_frame
            && pending_delta <= 1;
    }

    static constexpr RollbackNativeInterRoundClockAction
    ClassifyRollbackNativeInterRoundClockAction(
        bool stock_inter_round, bool new_round_control_call,
        const RollbackNativeSimulationClock& clock) noexcept
    {
        if (!stock_inter_round)
            return RollbackNativeInterRoundClockAction::Reject;
        if (new_round_control_call
            && RollbackNativeNeedsZeroDeltaEpochSync(clock))
        {
            return RollbackNativeInterRoundClockAction::
                RunZeroDeltaEpochSync;
        }
        return RollbackNativeInterRoundClockAction::ArmSingleIteration;
    }

    static constexpr bool RollbackNativeZeroDeltaEpochSyncCommitAllowed(
        RollbackNativeInterRoundClockAction action,
        bool native_clock_valid, bool new_round_state_valid) noexcept
    {
        return action
                == RollbackNativeInterRoundClockAction::RunZeroDeltaEpochSync
            && native_clock_valid
            && new_round_state_valid;
    }

    static constexpr bool ValidateRollbackNativeNoCatchUpHandoffObservation(
        uint32_t pending_before, uint32_t discarded_frames,
        uint32_t pending_armed, uint64_t native_calls,
        uint64_t per_frame_calls, uint32_t pending_after) noexcept
    {
        return pending_before > 0
            && discarded_frames == pending_before - 1u
            && pending_armed == 1u
            && native_calls == 1u
            && per_frame_calls == 1u
            && pending_after == 0u;
    }

    static constexpr bool ValidateRollbackNativeInterRoundControlTick(
        uint32_t pending_armed, uint64_t native_calls,
        uint64_t per_frame_calls, uint32_t pending_after) noexcept
    {
        return pending_armed == 1u
            && native_calls == 1u
            && per_frame_calls == 1u
            && pending_after == 0u;
    }

    // Once NewRound is current, retain SC6's native scheduling. The hook audits
    // one enclosing call without rewriting BattleManager cursors. A naturally
    // pending frame is bounded to one so the call cannot run through the
    // pre-control boundary.
    static constexpr bool ValidateRollbackNativeInterRoundPassThrough(
        uint32_t pending_before, uint32_t pending_after,
        uint64_t native_calls, uint64_t per_frame_calls,
        bool match_identity_stable, bool active_gameplay_observed) noexcept
    {
        return pending_before <= 1u
            && pending_after <= 1u
            && native_calls == 1u
            && per_frame_calls <= 1u
            && match_identity_stable
            && !active_gameplay_observed;
    }

    enum class RollbackNativeInterRoundControlState : uint8_t
    {
        Allowed,
        Unreadable,
        UnexpectedWorldMode,
        ActiveGameplay,
        UnexpectedBattleState,
    };

    // The world-mode pump can chain ResultScreen directly to ActiveBattle in
    // one native call. Battle status 1 is the observed cinematic completion
    // bridge; status 3 is the previously characterized NewRound bridge.
    // Status 2 is active gameplay and is deliberately excluded.
    static constexpr bool RollbackNativeCurrentActivePreGameplayBridgeAllowed(
        bool current_active_mode, bool queued_mode_clear,
        uint8_t battle_main_state, uint8_t battle_status) noexcept
    {
        return current_active_mode
            && queued_mode_clear
            && battle_main_state == 2
            && (battle_status == 1 || battle_status == 3);
    }

    static constexpr RollbackNativeInterRoundControlState
    ClassifyRollbackNativeInterRoundControlState(
        bool world_modes_readable, bool battle_state_readable,
        bool world_modes_allowed, bool active_gameplay_mode,
        uint8_t battle_main_state, uint8_t battle_status) noexcept
    {
        if (!world_modes_readable || !battle_state_readable)
            return RollbackNativeInterRoundControlState::Unreadable;
        // ActiveBattle is also the native bridge out of RoundResult. Live
        // traces show it paired with status 3 for several stock ticks before
        // NewRound becomes current. Status 2, not the mode pointer alone, is
        // the evidence that gameplay simulation has resumed.
        if (battle_status == 2
            && (active_gameplay_mode || battle_main_state == 2))
        {
            return RollbackNativeInterRoundControlState::ActiveGameplay;
        }
        if (!world_modes_allowed)
            return RollbackNativeInterRoundControlState::UnexpectedWorldMode;
        const bool main_state_allowed = battle_main_state == 2
            || battle_main_state == 4;
        const bool status_allowed = battle_status == 1
            || battle_status == 3 || battle_status == 5
            || battle_status == 9;
        return main_state_allowed && status_allowed
            ? RollbackNativeInterRoundControlState::Allowed
            : RollbackNativeInterRoundControlState::UnexpectedBattleState;
    }

    enum class RollbackNativeInterRoundOuterAction : uint8_t
    {
        FreezeCandidate,
        ArmControlTick,
        FailClosed,
    };

    static constexpr RollbackNativeInterRoundOuterAction
    ClassifyRollbackNativeInterRoundOuterAction(
        bool precontrol_candidate,
        RollbackNativeInterRoundControlState control_state) noexcept
    {
        if (precontrol_candidate)
            return RollbackNativeInterRoundOuterAction::FreezeCandidate;
        return control_state == RollbackNativeInterRoundControlState::Allowed
            ? RollbackNativeInterRoundOuterAction::ArmControlTick
            : RollbackNativeInterRoundOuterAction::FailClosed;
    }

    static constexpr bool ValidateRollbackNativeInterRoundControlledPostCall(
        uint32_t pending_armed, uint64_t native_calls,
        uint64_t per_frame_calls, uint32_t pending_after,
        bool match_identity_stable,
        RollbackNativeInterRoundControlState control_state) noexcept
    {
        return ValidateRollbackNativeInterRoundControlTick(
                   pending_armed, native_calls, per_frame_calls,
                   pending_after)
            && match_identity_stable
            && control_state == RollbackNativeInterRoundControlState::Allowed;
    }

    enum class RollbackNativeNewRoundFinalizeAction : uint8_t
    {
        PassThrough,
        Release,
        Reject,
    };

    static constexpr RollbackNativeNewRoundFinalizeAction
    ClassifyRollbackNativeNewRoundFinalizeAction(
        bool production_gate_active,
        bool stock_inter_round,
        bool starting_gekko,
        uint64_t round_generation,
        bool round_restart_pending,
        bool transition_deferred,
        bool identity_readable,
        bool exact_new_round_state,
        bool current_new_round_mode,
        bool queued_mode_clear,
        uint32_t mode_frame,
        uint32_t phase_timer) noexcept
    {
        if (!production_gate_active
            || (!stock_inter_round && !starting_gekko))
        {
            return RollbackNativeNewRoundFinalizeAction::PassThrough;
        }
        if (!identity_readable
            || !exact_new_round_state
            || !current_new_round_mode
            || !queued_mode_clear)
        {
            return RollbackNativeNewRoundFinalizeAction::Reject;
        }
        // Native calls reach this hook after NewRound Tick increments the
        // frame. Rearm's controlled release is the sole exception: it invokes
        // the retained finalizer directly from the verified pre-finalize
        // boundary and temporarily presents phase_timer to the stock predicate.
        const bool transition_due = mode_frame >= phase_timer;
        if (stock_inter_round)
        {
            if (round_generation == 0
                || round_restart_pending
                || transition_deferred)
                return RollbackNativeNewRoundFinalizeAction::Reject;
            return transition_due
                ? RollbackNativeNewRoundFinalizeAction::Reject
                : RollbackNativeNewRoundFinalizeAction::PassThrough;
        }

        // Initial startup freezes before the due NewRound Tick; Advance(0)
        // executes that complete stock edge. Rearm freezes one coordinate
        // earlier and queues ActiveBattle through a controlled direct finalizer
        // call without executing an extra NewRound Tick.
        if (round_generation == 1)
        {
            if (round_restart_pending || transition_deferred)
                return RollbackNativeNewRoundFinalizeAction::Reject;
            return transition_due
                ? RollbackNativeNewRoundFinalizeAction::PassThrough
                : RollbackNativeNewRoundFinalizeAction::Reject;
        }
        const bool pre_finalize_boundary = phase_timer != 0
            && mode_frame == phase_timer - 1u;
        if (round_generation <= 1
            || !round_restart_pending
            || transition_deferred
            || !pre_finalize_boundary)
            return RollbackNativeNewRoundFinalizeAction::Reject;
        return RollbackNativeNewRoundFinalizeAction::Release;
    }

    static constexpr bool
    RollbackNativeNewRoundDeferredShutdownAllowed(
        bool transition_deferred,
        bool out_of_battle_authorized) noexcept
    {
        return !transition_deferred || out_of_battle_authorized;
    }

    struct RollbackNativeInitialNewRoundBaselineEvidence
    {
        uint64_t round_generation {0};
        uintptr_t live_current_mode {0};
        uintptr_t live_queued_mode {0};
        uint32_t live_transition {0};
        uintptr_t saved_current_mode {0};
        uintptr_t saved_queued_mode {0};
        uint32_t saved_transition {0};
        bool frame_zero_held {false};
        bool round_restart_pending {false};
        bool transition_deferred {false};
        bool rearm_evidence_clear {false};
        bool baseline_already_verified {false};
    };

    static constexpr bool
    ValidateRollbackNativeInitialNewRoundBaselineEvidence(
        const RollbackNativeInitialNewRoundBaselineEvidence& evidence,
        uintptr_t expected_new_round_mode) noexcept
    {
        return evidence.round_generation == 1
            && evidence.frame_zero_held
            && !evidence.round_restart_pending
            && !evidence.transition_deferred
            && evidence.rearm_evidence_clear
            && !evidence.baseline_already_verified
            && evidence.live_current_mode == expected_new_round_mode
            && evidence.live_queued_mode == 0
            && evidence.saved_current_mode == evidence.live_current_mode
            && evidence.saved_queued_mode == 0
            && evidence.saved_transition == evidence.live_transition;
    }

    struct RollbackNativeNewRoundBaselineEvidence
    {
        uint64_t round_generation {0};
        uint64_t release_generation {0};
        uint64_t save_generation {0};
        uint64_t release_serial {0};
        uint64_t save_serial {0};
        uintptr_t live_current_mode {0};
        uintptr_t live_queued_mode {0};
        uint32_t live_transition {0};
        uintptr_t saved_current_mode {0};
        uintptr_t saved_queued_mode {0};
        uint32_t saved_transition {0};
        bool transition_deferred {false};
        bool baseline_already_verified {false};
        bool frame_zero_held {false};
    };

    static constexpr bool ValidateRollbackNativeNewRoundBaselineEvidence(
        const RollbackNativeNewRoundBaselineEvidence& evidence,
        uintptr_t expected_new_round_mode,
        uintptr_t expected_active_battle_mode) noexcept
    {
        return evidence.round_generation > 1
            && evidence.release_generation == evidence.round_generation
            && evidence.save_generation == evidence.round_generation
            && evidence.release_serial != 0
            && evidence.save_serial != 0
            && evidence.release_serial < evidence.save_serial
            && !evidence.transition_deferred
            && !evidence.baseline_already_verified
            && evidence.frame_zero_held
            && evidence.live_current_mode == expected_new_round_mode
            && evidence.live_queued_mode == expected_active_battle_mode
            && evidence.saved_current_mode == evidence.live_current_mode
            && evidence.saved_queued_mode == evidence.live_queued_mode
            && evidence.saved_transition == evidence.live_transition;
    }

    enum class RollbackNativeNewRoundBaselineSaveAction : uint8_t
    {
        Reject,
        Commit,
    };

    struct RollbackNativeNewRoundBaselineSaveDecision
    {
        RollbackNativeNewRoundBaselineSaveAction action {
            RollbackNativeNewRoundBaselineSaveAction::Reject};
        bool keep_frame_zero_held {true};
    };

    static constexpr RollbackNativeNewRoundBaselineSaveDecision
    DecideRollbackNativeNewRoundBaselineSave(
        const RollbackNativeNewRoundBaselineEvidence& evidence,
        uintptr_t expected_new_round_mode,
        uintptr_t expected_active_battle_mode) noexcept
    {
        return {
            ValidateRollbackNativeNewRoundBaselineEvidence(
                evidence, expected_new_round_mode,
                expected_active_battle_mode)
                ? RollbackNativeNewRoundBaselineSaveAction::Commit
                : RollbackNativeNewRoundBaselineSaveAction::Reject,
            true,
        };
    }

    static constexpr bool ValidateRollbackInitialBoundaryTail(
        uint32_t pre_delta,
        uint32_t post_delta,
        bool gameplay_attempted,
        uint32_t per_frame_before,
        uint32_t per_frame_after,
        uint32_t input_pair_before,
        uint32_t input_pair_after) noexcept
    {
        return pre_delta <= 1 && post_delta == pre_delta
            && !gameplay_attempted
            && per_frame_before == per_frame_after
            && input_pair_before == input_pair_after;
    }

    static constexpr bool ValidateRollbackInitialPeerWaitControlDelta(
        uint32_t pre_delta,
        uint32_t post_delta,
        bool gameplay_attempted,
        bool input_pair_suppressed,
        uint32_t per_frame_before,
        uint32_t per_frame_after,
        uint32_t input_pair_before,
        uint32_t input_pair_after) noexcept
    {
        return pre_delta > 0 && post_delta == 0
            && gameplay_attempted
            && input_pair_suppressed
            && per_frame_before == per_frame_after
            && input_pair_before == input_pair_after;
    }

    enum class RollbackInitialBoundaryClockAction : uint8_t
    {
        Invalid,
        AuditZeroDeltaTail,
        AuditOnePendingDeltaTail,
    };

    static constexpr RollbackInitialBoundaryClockAction
    ClassifyRollbackInitialBoundaryClock(
        const RollbackNativeSimulationClock& clock,
        uint32_t& pending_delta) noexcept
    {
        pending_delta = 0;
        if (clock.battle_last_frame != clock.input_log_last_frame
            || clock.battle_last_applied > clock.input_log_master_clock)
        {
            return RollbackInitialBoundaryClockAction::Invalid;
        }
        pending_delta =
            clock.input_log_master_clock - clock.battle_last_applied;
        if (pending_delta == 0)
            return RollbackInitialBoundaryClockAction::AuditZeroDeltaTail;
        if (pending_delta == 1)
            return RollbackInitialBoundaryClockAction::
                AuditOnePendingDeltaTail;
        return RollbackInitialBoundaryClockAction::Invalid;
    }

    template <typename RestoreFn, typename VerifyFn>
    class RollbackInitialBoundaryCursorRestoreGuard
    {
    public:
        RollbackInitialBoundaryCursorRestoreGuard(
            uint32_t& attempts,
            uint32_t& verified_restores,
            uint32_t& fallbacks,
            uint32_t& failures,
            RestoreFn restore,
            VerifyFn verify) noexcept
            : m_attempts(attempts),
              m_verified_restores(verified_restores),
              m_fallbacks(fallbacks),
              m_failures(failures),
              m_restore(std::move(restore)),
              m_verify(std::move(verify))
        {
            static_assert(noexcept(std::declval<RestoreFn&>()()));
            static_assert(noexcept(std::declval<VerifyFn&>()()));
        }

        RollbackInitialBoundaryCursorRestoreGuard(
            const RollbackInitialBoundaryCursorRestoreGuard&) = delete;
        RollbackInitialBoundaryCursorRestoreGuard& operator=(
            const RollbackInitialBoundaryCursorRestoreGuard&) = delete;

        void mark_armed() noexcept
        {
            m_armed = true;
        }

        bool restore_and_verify() noexcept
        {
            if (!m_armed) return false;
            ++m_attempts;
            if (!m_restore() || !m_verify())
            {
                ++m_failures;
                return false;
            }
            m_armed = false;
            ++m_verified_restores;
            return true;
        }

        bool armed() const noexcept
        {
            return m_armed;
        }

        void disarm_without_restore() noexcept
        {
            m_armed = false;
        }

        ~RollbackInitialBoundaryCursorRestoreGuard() noexcept
        {
            if (!m_armed) return;
            ++m_fallbacks;
            if (!m_restore()) ++m_failures;
        }

    private:
        uint32_t& m_attempts;
        uint32_t& m_verified_restores;
        uint32_t& m_fallbacks;
        uint32_t& m_failures;
        RestoreFn m_restore;
        VerifyFn m_verify;
        bool m_armed {false};
    };

    static inline bool ValidateRollbackNativeSingleIterationResult(
        const RollbackNativeSimulationClock& scoped,
        int32_t observed_battle_last_frame,
        uint32_t observed_battle_last_applied,
        int32_t observed_input_log_last_frame,
        uint32_t observed_input_log_master_clock) noexcept
    {
        return observed_input_log_last_frame
                == scoped.input_log_last_frame
            && observed_input_log_master_clock
                == scoped.input_log_master_clock
            && observed_battle_last_frame == scoped.input_log_last_frame
            && observed_battle_last_applied
                == scoped.input_log_master_clock;
    }

    static inline bool ValidateRollbackNativeSimulationClockArm(
        const RollbackNativeSimulationClock& scoped,
        const RollbackNativeSimulationClock& observed) noexcept
    {
        return observed.battle_last_frame == scoped.battle_last_frame
            && observed.battle_last_applied
                == scoped.battle_last_applied;
    }

    enum class RollbackNativeInputPairPublishResult : uint8_t
    {
        Ok,
        InvalidBoundary,
        ReadFailed,
        PairWriteFailed,
        PreviousWriteFailedRecovered,
        RecoveryFailed,
    };

    enum class RollbackNativeInputPairHookDisposition : uint8_t
    {
        PassThroughUnrelated,
        PublishExpected,
        InvalidExpectedBoundary,
        RepeatedExpected,
    };

    static constexpr RollbackNativeInputPairHookDisposition
    ClassifyRollbackNativeInputPairHookInvocation(
        uintptr_t battle_manager,
        uintptr_t callback_collection,
        uintptr_t input_pair_header,
        uint32_t successful_injections) noexcept
    {
        if (battle_manager == 0)
            return RollbackNativeInputPairHookDisposition::
                InvalidExpectedBoundary;
        if (callback_collection != battle_manager + 0x1210)
            return RollbackNativeInputPairHookDisposition::
                PassThroughUnrelated;
        if (input_pair_header != battle_manager + 0x14A8)
            return RollbackNativeInputPairHookDisposition::
                InvalidExpectedBoundary;
        return successful_injections == 0
            ? RollbackNativeInputPairHookDisposition::PublishExpected
            : RollbackNativeInputPairHookDisposition::RepeatedExpected;
    }

    struct RollbackNativeInputPairPublishReport
    {
        bool pair_written {false};
        bool previous_written {false};
        bool pair_recovered {false};
        bool previous_recovered {false};
    };

    template <typename ReadPointerFn, typename ReadFn, typename WriteFn>
    static inline RollbackNativeInputPairPublishResult
    PublishRollbackNativeInputPairs(
        uintptr_t battle_manager,
        uintptr_t callback_collection,
        uintptr_t input_pair_header,
        const std::array<uint64_t, 2>& packed_inputs,
        ReadPointerFn&& read_pointer,
        ReadFn&& read,
        WriteFn&& write,
        RollbackNativeInputPairPublishReport& report) noexcept
    {
        report = {};
        if (battle_manager == 0
            || callback_collection != battle_manager + 0x1210
            || input_pair_header != battle_manager + 0x14A8)
            return RollbackNativeInputPairPublishResult::InvalidBoundary;
        uintptr_t pair_array = 0;
        uintptr_t previous_input_array = 0;
        if (!read_pointer(input_pair_header, pair_array)
            || !read_pointer(
                battle_manager + 0x1498, previous_input_array)
            || pair_array == 0 || previous_input_array == 0)
            return RollbackNativeInputPairPublishResult::ReadFailed;
        const std::array<uint32_t, 2> current_inputs {
            static_cast<uint32_t>(packed_inputs[0]),
            static_cast<uint32_t>(packed_inputs[1]),
        };
        std::array<uint64_t, 2> original_pairs {};
        std::array<uint32_t, 2> original_previous {};
        if (!read(pair_array, original_pairs.data(), sizeof(original_pairs))
            || !read(previous_input_array, original_previous.data(),
                sizeof(original_previous)))
            return RollbackNativeInputPairPublishResult::ReadFailed;

        report.pair_written = write(
            pair_array, packed_inputs.data(), sizeof(packed_inputs));
        if (!report.pair_written)
            return RollbackNativeInputPairPublishResult::PairWriteFailed;
        report.previous_written = write(
            previous_input_array, current_inputs.data(),
            sizeof(current_inputs));
        if (report.previous_written)
            return RollbackNativeInputPairPublishResult::Ok;

        // A failed second write cannot leave a mixed native input state.
        // Attempt both restorations even if the first recovery fails.
        report.pair_recovered = write(
            pair_array, original_pairs.data(), sizeof(original_pairs));
        report.previous_recovered = write(
            previous_input_array, original_previous.data(),
            sizeof(original_previous));
        return report.pair_recovered && report.previous_recovered
            ? RollbackNativeInputPairPublishResult::
                PreviousWriteFailedRecovered
            : RollbackNativeInputPairPublishResult::RecoveryFailed;
    }

    enum class RollbackNativeSimulationClockWriteResult : uint8_t
    {
        Ok,
        ArmFailedRecovered,
        ArmRecoveryFailed,
        RestoreFailed,
    };

    struct RollbackNativeSimulationClockWriteReport
    {
        bool frame_write {false};
        bool applied_write {false};
        bool frame_recovery {false};
        bool applied_recovery {false};
    };

    enum class RollbackNativeSimulationClockArmVerifyResult : uint8_t
    {
        Ok,
        ReadFailedRecovered,
        MismatchRecovered,
        RecoveryFailed,
    };

    struct RollbackNativeSimulationClockArmVerifyReport
    {
        bool frame_read {false};
        bool applied_read {false};
        RollbackNativeSimulationClock observed {};
        RollbackNativeSimulationClockWriteReport restore {};
    };

    struct RollbackNativeInputLogClockWriteReport
    {
        bool last_frame_restore {false};
        bool master_clock_restore {false};
    };

    struct RollbackNativeInputLogClockReadReport
    {
        bool last_frame_read {false};
        bool master_clock_read {false};
    };

    template <typename ReadFn>
    static inline bool CaptureRollbackNativeInputLogClock(
        uintptr_t input_log,
        ReadFn&& read,
        RollbackNativeSimulationClock& clock,
        RollbackNativeInputLogClockReadReport& report) noexcept
    {
        report = {};
        if (input_log == 0) return false;
        report.last_frame_read = read(
            input_log + 0x3A0,
            &clock.input_log_last_frame,
            sizeof(clock.input_log_last_frame));
        report.master_clock_read = read(
            input_log + 0x3A4,
            &clock.input_log_master_clock,
            sizeof(clock.input_log_master_clock));
        return report.last_frame_read && report.master_clock_read;
    }

    template <typename WriteFn>
    static inline bool RestoreRollbackNativeInputLogClock(
        uintptr_t input_log,
        const RollbackNativeSimulationClock& before,
        WriteFn&& write,
        RollbackNativeInputLogClockWriteReport& report) noexcept
    {
        report = {};
        if (input_log == 0) return false;
        // Attempt both writes even when the first fails. The two scheduling
        // cursors are one scoped unit and may never be left half-restored.
        report.last_frame_restore = write(
            input_log + 0x3A0,
            &before.input_log_last_frame,
            sizeof(before.input_log_last_frame));
        report.master_clock_restore = write(
            input_log + 0x3A4,
            &before.input_log_master_clock,
            sizeof(before.input_log_master_clock));
        return report.last_frame_restore && report.master_clock_restore;
    }

    struct RollbackNativeSimulationScopeContext
    {
        const void* owner {nullptr};
        uintptr_t battle_manager {0};
        uintptr_t input_log {0};
        const void* camera_input {nullptr};
        RollbackNativeSimulationClock prevalidated_clock {};
        RollbackNativeSimulationClock armed_clock {};
        uint32_t logical_frame {UINT32_MAX};
        bool rolling_back {false};
        bool active {false};
    };

    // Caller attribution is meaningful only when every execution attempt of
    // a logical frame is measured. Prediction can make the first forward
    // attempt differ between peers; their final comparable attempt may be a
    // rollback replay on one side and a forward advance on the other.
    static inline constexpr bool ShouldTraceRollbackNativeRngCallers(
        uint32_t logical_frame,
        uint32_t prefix_frames,
        bool rolling_back,
        bool trace_enabled,
        bool hook_installed) noexcept
    {
        (void)rolling_back;
        return logical_frame < prefix_frames
            && trace_enabled && hook_installed;
    }

    inline thread_local RollbackNativeSimulationScopeContext
        g_rollback_native_simulation_scope {};

    struct RollbackInitialBoundaryScopeContext
    {
        const void* owner {nullptr};
        uintptr_t battle_manager {0};
        bool gameplay_attempted {false};
        bool input_pair_suppressed {false};
        bool active {false};
    };

    inline thread_local RollbackInitialBoundaryScopeContext
        g_rollback_initial_boundary_scope {};

    class RollbackInitialBoundaryScope
    {
    public:
        RollbackInitialBoundaryScope(
            const void* owner, uintptr_t battle_manager) noexcept
        {
            if (!owner || battle_manager == 0
                || g_rollback_initial_boundary_scope.active)
                return;
            g_rollback_initial_boundary_scope = {
                owner, battle_manager, false, false, true};
            m_armed = true;
        }

        ~RollbackInitialBoundaryScope() noexcept
        {
            if (m_armed) g_rollback_initial_boundary_scope = {};
        }

        RollbackInitialBoundaryScope(
            const RollbackInitialBoundaryScope&) = delete;
        RollbackInitialBoundaryScope& operator=(
            const RollbackInitialBoundaryScope&) = delete;

        explicit operator bool() const noexcept { return m_armed; }

    private:
        bool m_armed {false};
    };

    static inline bool MarkRollbackInitialBoundaryGameplayAttempt(
        const void* owner) noexcept
    {
        if (!owner || !g_rollback_initial_boundary_scope.active
            || g_rollback_initial_boundary_scope.owner != owner)
            return false;
        g_rollback_initial_boundary_scope.gameplay_attempted = true;
        return true;
    }

    static inline bool RollbackInitialBoundaryGameplayAttempted(
        const void* owner, uintptr_t battle_manager) noexcept
    {
        return owner && battle_manager != 0
            && g_rollback_initial_boundary_scope.active
            && g_rollback_initial_boundary_scope.owner == owner
            && g_rollback_initial_boundary_scope.battle_manager
                == battle_manager
            && g_rollback_initial_boundary_scope.gameplay_attempted;
    }

    static inline bool SuppressRollbackInitialBoundaryInputPair(
        const void* owner, uintptr_t collection,
        uintptr_t input_pair_header) noexcept
    {
        if (!owner || !g_rollback_initial_boundary_scope.active
            || g_rollback_initial_boundary_scope.owner != owner)
            return false;
        const uintptr_t manager =
            g_rollback_initial_boundary_scope.battle_manager;
        if (collection != manager + 0x1210
            || input_pair_header != manager + 0x14A8)
            return false;
        g_rollback_initial_boundary_scope.input_pair_suppressed = true;
        return true;
    }

    static inline bool RollbackInitialBoundaryInputPairSuppressed(
        const void* owner, uintptr_t battle_manager) noexcept
    {
        return owner && battle_manager != 0
            && g_rollback_initial_boundary_scope.active
            && g_rollback_initial_boundary_scope.owner == owner
            && g_rollback_initial_boundary_scope.battle_manager
                == battle_manager
            && g_rollback_initial_boundary_scope.input_pair_suppressed;
    }

    static inline bool ShouldRunRollbackInitialPeerWaitTail(
        bool baseline_frozen, uint64_t round_generation,
        bool local_baseline_parsed, bool peer_baseline_parsed) noexcept
    {
        return baseline_frozen && round_generation == 1
            && local_baseline_parsed && !peer_baseline_parsed;
    }

    class RollbackInitialBaselinePrestartGate
    {
    public:
        template <typename CaptureAndCompare, typename RestoreAndVerify>
        bool verify_once(
            CaptureAndCompare&& capture_and_compare,
            RestoreAndVerify&& restore_and_verify) noexcept
        {
            if (m_verified) return true;
            if (!capture_and_compare()) return false;
            if (!restore_and_verify()) return false;
            m_verified = true;
            return true;
        }

        bool verified() const noexcept { return m_verified; }

    private:
        bool m_verified {false};
    };

    class RollbackNativeSimulationScope
    {
    public:
        RollbackNativeSimulationScope(
            const void* owner,
            uintptr_t battle_manager,
            uintptr_t input_log,
            const void* camera_input,
            const RollbackNativeSimulationClock& prevalidated_clock,
            const RollbackNativeSimulationClock& armed_clock,
            bool rolling_back = false,
            uint32_t logical_frame = UINT32_MAX) noexcept
        {
            if (!owner || battle_manager == 0 || input_log == 0
                || !camera_input
                || g_rollback_native_simulation_scope.active)
                return;
            g_rollback_native_simulation_scope = {
                owner,
                battle_manager,
                input_log,
                camera_input,
                prevalidated_clock,
                armed_clock,
                logical_frame,
                rolling_back,
                true,
            };
            m_armed = true;
        }

        ~RollbackNativeSimulationScope() noexcept
        {
            if (m_armed)
                g_rollback_native_simulation_scope = {};
        }

        RollbackNativeSimulationScope(
            const RollbackNativeSimulationScope&) = delete;
        RollbackNativeSimulationScope& operator=(
            const RollbackNativeSimulationScope&) = delete;

        explicit operator bool() const noexcept { return m_armed; }

    private:
        bool m_armed {false};
    };

    static inline const RollbackNativeSimulationScopeContext*
    CurrentRollbackNativeSimulationScope() noexcept
    {
        const auto& context = g_rollback_native_simulation_scope;
        return context.active ? &context : nullptr;
    }

    static inline const RollbackNativeSimulationScopeContext*
    CurrentRollbackNativeSimulationScope(
        const void* owner,
        uintptr_t battle_manager = 0,
        uintptr_t input_log = 0) noexcept
    {
        const auto& context = g_rollback_native_simulation_scope;
        if (!context.active || context.owner != owner
            || (battle_manager != 0
                && context.battle_manager != battle_manager)
            || (input_log != 0 && context.input_log != input_log))
            return nullptr;
        return &context;
    }

    template <typename WriteFn>
    static inline RollbackNativeSimulationClockWriteResult
    ArmRollbackNativeSimulationClock(
        uintptr_t battle_manager,
        const RollbackNativeSimulationClock& before,
        const RollbackNativeSimulationClock& scoped,
        WriteFn&& write,
        RollbackNativeSimulationClockWriteReport& report) noexcept
    {
        report = {};
        report.frame_write = write(
            battle_manager + 0x1488,
            &scoped.battle_last_frame, sizeof(scoped.battle_last_frame));
        // Never short-circuit the second write. A failed adapter must have a
        // complete recovery attempt regardless of which write failed.
        report.applied_write = write(
            battle_manager + 0x148C,
            &scoped.battle_last_applied, sizeof(scoped.battle_last_applied));
        if (report.frame_write && report.applied_write)
            return RollbackNativeSimulationClockWriteResult::Ok;

        report.frame_recovery = write(
            battle_manager + 0x1488,
            &before.battle_last_frame, sizeof(before.battle_last_frame));
        report.applied_recovery = write(
            battle_manager + 0x148C,
            &before.battle_last_applied, sizeof(before.battle_last_applied));
        return report.frame_recovery && report.applied_recovery
            ? RollbackNativeSimulationClockWriteResult::ArmFailedRecovered
            : RollbackNativeSimulationClockWriteResult::ArmRecoveryFailed;
    }

    template <typename WriteFn>
    static inline RollbackNativeSimulationClockWriteResult
    RestoreRollbackNativeSimulationClock(
        uintptr_t battle_manager,
        const RollbackNativeSimulationClock& before,
        WriteFn&& write,
        RollbackNativeSimulationClockWriteReport& report) noexcept
    {
        report = {};
        report.frame_recovery = write(
            battle_manager + 0x1488,
            &before.battle_last_frame, sizeof(before.battle_last_frame));
        report.applied_recovery = write(
            battle_manager + 0x148C,
            &before.battle_last_applied, sizeof(before.battle_last_applied));
        return report.frame_recovery && report.applied_recovery
            ? RollbackNativeSimulationClockWriteResult::Ok
            : RollbackNativeSimulationClockWriteResult::RestoreFailed;
    }

    template <typename ReadFn, typename WriteFn>
    static inline RollbackNativeSimulationClockArmVerifyResult
    VerifyRollbackNativeSimulationClockArmOrRestore(
        uintptr_t battle_manager,
        const RollbackNativeSimulationClock& before,
        const RollbackNativeSimulationClock& scoped,
        ReadFn&& read,
        WriteFn&& write,
        RollbackNativeSimulationClockArmVerifyReport& report) noexcept
    {
        report = {};
        report.observed = before;
        report.frame_read = read(
            battle_manager + 0x1488,
            &report.observed.battle_last_frame,
            sizeof(report.observed.battle_last_frame));
        report.applied_read = read(
            battle_manager + 0x148C,
            &report.observed.battle_last_applied,
            sizeof(report.observed.battle_last_applied));
        const bool read_ok = report.frame_read && report.applied_read;
        if (read_ok && ValidateRollbackNativeSimulationClockArm(
                scoped, report.observed))
            return RollbackNativeSimulationClockArmVerifyResult::Ok;

        const auto restored = RestoreRollbackNativeSimulationClock(
            battle_manager, before, std::forward<WriteFn>(write),
            report.restore);
        if (restored != RollbackNativeSimulationClockWriteResult::Ok)
            return RollbackNativeSimulationClockArmVerifyResult::
                RecoveryFailed;
        return read_ok
            ? RollbackNativeSimulationClockArmVerifyResult::
                MismatchRecovered
            : RollbackNativeSimulationClockArmVerifyResult::
                ReadFailedRecovered;
    }

    class RollbackFrozenBoundaryClockRestoreGate
    {
    public:
        bool arm(uintptr_t battle_manager,
            const RollbackNativeSimulationClock& clock) noexcept
        {
            if (battle_manager == 0 || m_state != State::Empty)
                return false;
            m_battle_manager = battle_manager;
            m_clock = clock;
            m_state = State::Pending;
            return true;
        }

        template <typename ReadFn, typename WriteFn>
        bool restore_once(ReadFn&& read, WriteFn&& write) noexcept
        {
            if (m_state == State::Empty || m_state == State::Restored)
                return true;
            if (m_state == State::Failed) return false;

            ++m_attempts;
            RollbackNativeSimulationClockWriteReport write_report {};
            const auto write_result = RestoreRollbackNativeSimulationClock(
                m_battle_manager, m_clock, std::forward<WriteFn>(write),
                write_report);
            int32_t observed_frame = 0;
            uint32_t observed_applied = 0;
            const bool frame_read = read(
                m_battle_manager + 0x1488, &observed_frame,
                sizeof(observed_frame));
            const bool applied_read = read(
                m_battle_manager + 0x148C, &observed_applied,
                sizeof(observed_applied));
            if (write_result != RollbackNativeSimulationClockWriteResult::Ok
                || !frame_read || !applied_read
                || observed_frame != m_clock.battle_last_frame
                || observed_applied != m_clock.battle_last_applied)
            {
                ++m_failures;
                m_state = State::Failed;
                return false;
            }
            ++m_verified_restores;
            m_state = State::Restored;
            return true;
        }

        bool empty() const noexcept { return m_state == State::Empty; }
        bool pending() const noexcept { return m_state == State::Pending; }
        bool restored() const noexcept { return m_state == State::Restored; }
        bool failed() const noexcept { return m_state == State::Failed; }
        uint32_t attempts() const noexcept { return m_attempts; }
        uint32_t verified_restores() const noexcept
        {
            return m_verified_restores;
        }
        uint32_t failures() const noexcept { return m_failures; }

        void clear() noexcept
        {
            m_battle_manager = 0;
            m_clock = {};
            m_state = State::Empty;
            m_attempts = 0;
            m_verified_restores = 0;
            m_failures = 0;
        }

    private:
        enum class State : uint8_t
        {
            Empty,
            Pending,
            Restored,
            Failed,
        };

        uintptr_t m_battle_manager {0};
        RollbackNativeSimulationClock m_clock {};
        State m_state {State::Empty};
        uint32_t m_attempts {0};
        uint32_t m_verified_restores {0};
        uint32_t m_failures {0};
    };

    class RollbackFrozenBoundaryReleaseCoordinator
    {
    public:
        bool arm(uintptr_t battle_manager,
            const RollbackNativeSimulationClock& clock) noexcept
        {
            if (!m_clock.arm(battle_manager, clock)) return false;
            m_fatal_frozen = false;
            m_shutdown_deferred = false;
            m_pass_through_allowed = false;
            return true;
        }

        void note_fail_closed() noexcept
        {
            m_fatal_frozen = true;
            m_pass_through_allowed = false;
        }

        void defer_shutdown() noexcept
        {
            m_fatal_frozen = true;
            m_shutdown_deferred = true;
            m_pass_through_allowed = false;
        }

        template <typename ReadFn, typename WriteFn>
        bool restore_before_frame_zero(
            ReadFn&& read, WriteFn&& write) noexcept
        {
            m_pass_through_allowed = false;
            return m_clock.restore_once(
                std::forward<ReadFn>(read), std::forward<WriteFn>(write));
        }

        template <typename ReadFn, typename WriteFn>
        bool service_fatal_restore(
            ReadFn&& read, WriteFn&& write) noexcept
        {
            m_fatal_frozen = true;
            m_pass_through_allowed = false;
            return m_clock.restore_once(
                std::forward<ReadFn>(read), std::forward<WriteFn>(write));
        }

        template <typename ReadFn, typename WriteFn>
        bool prepare_pass_through_release(
            ReadFn&& read, WriteFn&& write) noexcept
        {
            m_pass_through_allowed = false;
            if (!m_clock.restore_once(
                    std::forward<ReadFn>(read),
                    std::forward<WriteFn>(write)))
            {
                m_fatal_frozen = true;
                return false;
            }
            if (m_clock.pending() || m_clock.failed())
            {
                m_fatal_frozen = true;
                return false;
            }
            m_shutdown_deferred = false;
            m_pass_through_allowed = true;
            return true;
        }

        bool needs_fatal_restore() const noexcept
        {
            return m_fatal_frozen && m_clock.pending();
        }
        bool pass_through_allowed() const noexcept
        {
            return m_pass_through_allowed
                && !m_clock.pending() && !m_clock.failed();
        }
        bool fatal_frozen() const noexcept { return m_fatal_frozen; }
        bool shutdown_deferred() const noexcept
        {
            return m_shutdown_deferred;
        }
        bool empty() const noexcept { return m_clock.empty(); }
        bool pending() const noexcept { return m_clock.pending(); }
        bool restored() const noexcept { return m_clock.restored(); }
        bool failed() const noexcept { return m_clock.failed(); }
        uint32_t attempts() const noexcept { return m_clock.attempts(); }
        uint32_t verified_restores() const noexcept
        {
            return m_clock.verified_restores();
        }
        uint32_t failures() const noexcept { return m_clock.failures(); }

        void clear() noexcept
        {
            m_clock.clear();
            m_fatal_frozen = false;
            m_shutdown_deferred = false;
            m_pass_through_allowed = false;
        }

    private:
        RollbackFrozenBoundaryClockRestoreGate m_clock {};
        bool m_fatal_frozen {false};
        bool m_shutdown_deferred {false};
        bool m_pass_through_allowed {false};
    };

    struct RollbackNativeCallbackCollectionToken
    {
        uintptr_t heap_entries {0};
        int32_t count {-1};
        int32_t recursion_depth {-1};
        uint64_t entry_digest {0};
        uint64_t target_digest {0};

        bool valid() const noexcept
        {
            return count >= 0 && count <= 32 && recursion_depth == 0
                && (count == 0
                    || (entry_digest != 0 && target_digest != 0));
        }

        bool unchanged_from(
            const RollbackNativeCallbackCollectionToken& before) const noexcept
        {
            return valid() && before.valid()
                && heap_entries == before.heap_entries
                && count == before.count
                && entry_digest == before.entry_digest
                && target_digest == before.target_digest;
        }
    };

    static inline uint64_t HashRollbackNativeCallbackEntryTopology(
        uintptr_t entry, uintptr_t external_target,
        uint32_t entry_state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(entry);
        hash.add_scalar(external_target);
        hash.add_scalar(entry_state);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackNativeCallbackTargetTopology(
        uintptr_t target,
        const std::array<uint64_t, 6>& target_storage,
        uintptr_t resolved_object,
        uintptr_t resolved_this, uintptr_t wrapper_dispatch,
        uintptr_t resolved_vtable,
        uintptr_t resolved_virtual_5f8) noexcept
    {
        int32_t this_adjustment = 0;
        std::memcpy(&this_adjustment, &target_storage[3],
            sizeof(this_adjustment));
        RollbackHash hash {};
        hash.add_scalar(target);
        hash.add_scalar(target_storage[0]);
        hash.add_scalar(target_storage[1]);
        hash.add_scalar(target_storage[2]);
        hash.add_scalar(this_adjustment);
        // Registration stores GetNextNonZeroSequenceId() at +0x28. It is
        // the stable callback handle, not mutable callback payload.
        hash.add_scalar(target_storage[5]);
        hash.add_scalar(resolved_object);
        hash.add_scalar(resolved_this);
        hash.add_scalar(wrapper_dispatch);
        hash.add_scalar(resolved_vtable);
        hash.add_scalar(resolved_virtual_5f8);
        return hash.value ? hash.value : 1;
    }

    static constexpr size_t kRollbackNativeCallbackCapacity = 32;

    enum class RollbackNativeCallbackCollectionKind : uint8_t
    {
        Input,
        Simulation,
        Presentation,
    };

    // Full callback discovery is a pre-activation operation.  The active
    // rollback path deliberately keeps using the cheap collection token
    // above; collection topology alone is not evidence that callback-owned
    // state is covered by snapshots.
    struct RollbackNativeCallbackTargetDescriptor
    {
        uintptr_t entry {0};
        uintptr_t target {0};
        uintptr_t vtable {0};
        uintptr_t dispatch {0};
        uintptr_t bound_function {0};
        int32_t this_adjustment {0};
        uintptr_t resolved_object {0};
        uintptr_t resolved_this {0};
        uintptr_t resolved_vtable {0};
        uintptr_t resolved_virtual_5f8 {0};
        uintptr_t effective_target {0};
        uint64_t entry_digest {0};
        uint64_t target_storage_digest {0};
        uint64_t resolved_storage_digest {0};
        // The registered weak-object callback is exactly 3 * 0x10 bytes.
        // Reading eight qwords would overrun it into adjacent heap storage.
        std::array<uint64_t, 6> target_storage {};
        std::array<uint64_t, 8> resolved_storage {};
        uint32_t entry_state {0};
        bool external_target {false};
        bool virtual_5f8_applies {false};

        bool valid() const noexcept
        {
            return entry != 0 && target != 0 && vtable != 0
                && dispatch != 0 && bound_function != 0
                && resolved_object != 0 && resolved_this != 0
                && effective_target != 0
                && (!virtual_5f8_applies
                    || (resolved_vtable != 0
                        && resolved_virtual_5f8 != 0))
                && entry_digest != 0 && target_storage_digest != 0
                && resolved_storage_digest != 0;
        }
    };

    struct RollbackNativeCallbackCollectionInventory
    {
        RollbackNativeCallbackCollectionKind kind {
            RollbackNativeCallbackCollectionKind::Input};
        uintptr_t collection {0};
        int32_t count {-1};
        uint64_t identity_digest {0};
        std::array<RollbackNativeCallbackTargetDescriptor,
            kRollbackNativeCallbackCapacity> targets {};

        bool valid() const noexcept
        {
            if (collection == 0 || count < 0
                || count > static_cast<int32_t>(targets.size()))
                return false;
            for (int32_t i = 0; i < count; ++i)
            {
                if (!targets[static_cast<size_t>(i)].valid())
                    return false;
            }
            return count == 0 || identity_digest != 0;
        }
    };

    struct RollbackNativeSimulationIterationToken
    {
        RollbackNativeCallbackCollectionToken input_callbacks {};
        RollbackNativeCallbackCollectionToken simulation_callbacks {};
        uint8_t loop_again {0xFF};
        uint8_t pending_dispatch {0xFF};
        int32_t unpause_grace_period {-1};
        bool valid {false};
    };

    // The stock online PvP callback topology observed on the supported SC6
    // executable.  RVAs are used instead of process addresses so the policy
    // remains ASLR-independent.  A different target or ordering is not a
    // near-match: ownership stays disabled until that topology is audited.
    static constexpr uintptr_t kRollbackNativeCallbackRvaInputDispatch =
        0x3EF0C0;
    static constexpr uintptr_t kRollbackNativeCallbackRvaSimulationDispatch =
        0x3EF130;
    static constexpr uintptr_t kRollbackNativeCallbackRvaAttackStateInput =
        0x427940;
    static constexpr uintptr_t kRollbackNativeCallbackRvaPausedTickThunk =
        0x8954A4;
    static constexpr uintptr_t kRollbackNativeCallbackRvaPausedTickTarget =
        0x428270;
    static constexpr uintptr_t kRollbackNativeCallbackRvaRefreshMoveCaches =
        0x3C7080;
    static constexpr uintptr_t kRollbackNativeCallbackVtableRvaPausedTick =
        0x3296278;
    static constexpr uintptr_t kRollbackNativeCallbackVtableRvaInput =
        0x328F8D8;
    static constexpr uintptr_t kRollbackNativeCallbackVtableRvaMoveCache =
        0x32698A8;
    static constexpr uintptr_t kRollbackNativeCallbackWrapperVtableRvaInput =
        0x3285198;
    static constexpr uintptr_t
        kRollbackNativeCallbackWrapperVtableRvaSimulation = 0x37AF460;

    struct RollbackNativeInputCallbackState
    {
        uintptr_t slot_table {0};
        int32_t table_index {-1};
        int32_t slot_index {-1};
        uint8_t action_mode {0xFF};
        uint64_t digest {0};

        bool valid_for_stock_pvp() const noexcept
        {
            // -1 is SC6's normal "no slot entry" sentinel. Indices and
            // action mode are mutable per-frame state; snapshot coverage owns
            // them. Only the table is immutable callback identity.
            return slot_table != 0
                && table_index >= -1 && slot_index >= -1
                && action_mode <= 9 && digest != 0;
        }

        bool same_identity_as(
            const RollbackNativeInputCallbackState& accepted) const noexcept
        {
            return valid_for_stock_pvp()
                && accepted.valid_for_stock_pvp()
                && slot_table == accepted.slot_table;
        }
    };

    struct RollbackNativeCallbackCoverage
    {
        RollbackNativeCallbackCollectionToken input_token {};
        RollbackNativeCallbackCollectionToken simulation_token {};
        RollbackNativeInputCallbackState input_state {};
        uintptr_t input_state_object {0};
        uintptr_t derived_cache_object {0};
        uintptr_t derived_cache_refresh {0};
        uint64_t input_inventory_digest {0};
        uint64_t simulation_inventory_digest {0};
        bool valid {false};

        bool accepts(
            const RollbackNativeSimulationIterationToken& token,
            const RollbackNativeInputCallbackState& current_input) const
            noexcept
        {
            return accepts_topology(token, current_input);
        }

        bool accepts_topology(
            const RollbackNativeSimulationIterationToken& token,
            const RollbackNativeInputCallbackState& current_input) const
            noexcept
        {
            return valid && input_state_object != 0
                && current_input.valid_for_stock_pvp()
                && current_input.same_identity_as(input_state)
                && token.input_callbacks.unchanged_from(input_token)
                && token.simulation_callbacks.unchanged_from(
                    simulation_token);
        }
    };

    enum RollbackNativeCallbackCoverageMismatch : uint32_t
    {
        RollbackNativeCallbackCoverageMismatchNone = 0,
        RollbackNativeCallbackCoverageMismatchBoundary = 1u << 0,
        RollbackNativeCallbackCoverageMismatchInputCollection = 1u << 1,
        RollbackNativeCallbackCoverageMismatchSimulationCollection = 1u << 2,
        RollbackNativeCallbackCoverageMismatchInputState = 1u << 3,
    };

    static inline uint32_t DiagnoseRollbackNativeCallbackCoverage(
        const RollbackNativeSimulationIterationToken& token,
        const RollbackNativeCallbackCoverage& coverage,
        const RollbackNativeInputCallbackState& input_state) noexcept
    {
        uint32_t mismatch = RollbackNativeCallbackCoverageMismatchNone;
        if (!token.valid || !token.input_callbacks.valid()
            || !token.simulation_callbacks.valid()
            || token.loop_again != 0 || token.pending_dispatch != 0
            || token.unpause_grace_period != 0)
            mismatch |= RollbackNativeCallbackCoverageMismatchBoundary;
        if (!token.input_callbacks.unchanged_from(coverage.input_token))
            mismatch |= RollbackNativeCallbackCoverageMismatchInputCollection;
        if (!token.simulation_callbacks.unchanged_from(
                coverage.simulation_token))
        {
            mismatch |=
                RollbackNativeCallbackCoverageMismatchSimulationCollection;
        }
        if (!input_state.same_identity_as(coverage.input_state))
            mismatch |= RollbackNativeCallbackCoverageMismatchInputState;
        return mismatch;
    }

    struct RollbackNativeCallbackCoverageMismatchEvidence
    {
        bool valid {false};
        uint32_t mismatch_mask {0};
        RollbackNativeSimulationIterationToken token {};
        RollbackNativeInputCallbackState input_state {};

        bool latch(
            uint32_t mismatch,
            const RollbackNativeSimulationIterationToken& observed_token,
            const RollbackNativeInputCallbackState& observed_input_state)
            noexcept
        {
            if (valid || mismatch == 0) return false;
            mismatch_mask = mismatch;
            token = observed_token;
            input_state = observed_input_state;
            valid = true;
            return true;
        }
    };

    enum class RollbackNativeOwnedIterationFailureKind : uint8_t
    {
        None,
        Boundary,
        Topology,
        Completion,
    };

    struct RollbackNativeOwnedIterationFailureEvidence
    {
        bool valid {false};
        RollbackNativeOwnedIterationFailureKind kind {
            RollbackNativeOwnedIterationFailureKind::None};
        uint32_t logical_frame {0};
        bool rolling_back {false};
        uint32_t raw_coverage_mismatch {0};
        uint32_t effective_coverage_mismatch {0};
        RollbackNativeSimulationIterationToken before {};
        RollbackNativeSimulationIterationToken after {};
        bool terminal_pending_before {false};
        bool terminal_pending_after {false};
        bool producer_frame_before_valid {false};
        uint32_t producer_frame_before {0};
        bool producer_frame_after_valid {false};
        uint32_t producer_frame_after {0};
        uint64_t notification_suppressions_before {0};
        uint64_t notification_suppressions_after {0};
        bool completion_valid {false};

        bool latch(
            uint32_t frame,
            bool is_rolling_back,
            uint32_t raw_mismatch,
            uint32_t effective_mismatch,
            const RollbackNativeSimulationIterationToken& token_before,
            const RollbackNativeSimulationIterationToken& token_after,
            bool pending_before,
            bool pending_after,
            bool producer_before_valid,
            uint32_t producer_before,
            bool producer_after_valid,
            uint32_t producer_after,
            uint64_t suppressions_before,
            uint64_t suppressions_after,
            bool completed) noexcept
        {
            if (valid || (effective_mismatch == 0 && completed))
                return false;
            valid = true;
            logical_frame = frame;
            rolling_back = is_rolling_back;
            raw_coverage_mismatch = raw_mismatch;
            effective_coverage_mismatch = effective_mismatch;
            before = token_before;
            after = token_after;
            terminal_pending_before = pending_before;
            terminal_pending_after = pending_after;
            producer_frame_before_valid = producer_before_valid;
            producer_frame_before = producer_before;
            producer_frame_after_valid = producer_after_valid;
            producer_frame_after = producer_after;
            notification_suppressions_before = suppressions_before;
            notification_suppressions_after = suppressions_after;
            completion_valid = completed;
            constexpr uint32_t topology_mask =
                RollbackNativeCallbackCoverageMismatchInputCollection
                | RollbackNativeCallbackCoverageMismatchSimulationCollection
                | RollbackNativeCallbackCoverageMismatchInputState;
            kind = (effective_mismatch & topology_mask) != 0
                ? RollbackNativeOwnedIterationFailureKind::Topology
                : effective_mismatch != 0
                ? RollbackNativeOwnedIterationFailureKind::Boundary
                : RollbackNativeOwnedIterationFailureKind::Completion;
            return true;
        }
    };

    template <typename ReadFn>
    static inline bool CaptureRollbackNativeInputCallbackState(
        uintptr_t object, ReadFn&& read,
        RollbackNativeInputCallbackState& out) noexcept
    {
        out = {};
        if (object == 0
            || !read(object + 0x470, &out.slot_table,
                sizeof(out.slot_table))
            || !read(object + 0x478, &out.table_index,
                sizeof(out.table_index))
            || !read(object + 0x47C, &out.slot_index,
                sizeof(out.slot_index))
            || !read(object + 0x480, &out.action_mode,
                sizeof(out.action_mode)))
        {
            return false;
        }
        RollbackHash hash {};
        hash.add_scalar(out.slot_table);
        hash.add_scalar(out.table_index);
        hash.add_scalar(out.slot_index);
        hash.add_scalar(out.action_mode);
        out.digest = hash.value ? hash.value : 1;
        return out.valid_for_stock_pvp();
    }

    template <typename ReadFn>
    static inline bool BuildRollbackNativeCallbackCoverage(
        uintptr_t image_base,
        const RollbackNativeSimulationIterationToken& token,
        const RollbackNativeCallbackCollectionInventory& input,
        const RollbackNativeCallbackCollectionInventory& simulation,
        ReadFn&& read,
        RollbackNativeCallbackCoverage& out) noexcept
    {
        out = {};
        if (image_base == 0 || !input.valid() || !simulation.valid()
            || input.kind != RollbackNativeCallbackCollectionKind::Input
            || simulation.kind
                != RollbackNativeCallbackCollectionKind::Simulation
            || input.count != 1 || simulation.count != 2
            || token.input_callbacks.count != input.count
            || token.simulation_callbacks.count != simulation.count)
        {
            return false;
        }

        const auto rva_is = [image_base](
            uintptr_t address, uintptr_t rva) noexcept {
            return address >= image_base && address - image_base == rva;
        };
        const auto& input_target = input.targets[0];
        const auto& paused_target = simulation.targets[0];
        const auto& refresh_target = simulation.targets[1];
        if (input_target.entry_state != 3
            || input_target.virtual_5f8_applies
            || !rva_is(input_target.vtable,
                kRollbackNativeCallbackWrapperVtableRvaInput)
            || !rva_is(input_target.dispatch,
                kRollbackNativeCallbackRvaInputDispatch)
            || !rva_is(input_target.bound_function,
                kRollbackNativeCallbackRvaAttackStateInput)
            || !rva_is(input_target.effective_target,
                kRollbackNativeCallbackRvaAttackStateInput)
            || !rva_is(static_cast<uintptr_t>(
                    input_target.resolved_storage[0]),
                kRollbackNativeCallbackVtableRvaInput)
            || paused_target.entry_state != 3
            || !paused_target.virtual_5f8_applies
            || !rva_is(paused_target.vtable,
                kRollbackNativeCallbackWrapperVtableRvaSimulation)
            || !rva_is(paused_target.dispatch,
                kRollbackNativeCallbackRvaSimulationDispatch)
            || !rva_is(paused_target.bound_function,
                kRollbackNativeCallbackRvaPausedTickThunk)
            || !rva_is(paused_target.effective_target,
                kRollbackNativeCallbackRvaPausedTickTarget)
            || !rva_is(paused_target.resolved_vtable,
                kRollbackNativeCallbackVtableRvaPausedTick)
            || !rva_is(static_cast<uintptr_t>(
                    paused_target.resolved_storage[0]),
                kRollbackNativeCallbackVtableRvaPausedTick)
            || refresh_target.entry_state != 3
            || refresh_target.virtual_5f8_applies
            || !rva_is(refresh_target.vtable,
                kRollbackNativeCallbackWrapperVtableRvaSimulation)
            || !rva_is(refresh_target.dispatch,
                kRollbackNativeCallbackRvaSimulationDispatch)
            || !rva_is(refresh_target.bound_function,
                kRollbackNativeCallbackRvaRefreshMoveCaches)
            || !rva_is(refresh_target.effective_target,
                kRollbackNativeCallbackRvaRefreshMoveCaches)
            || !rva_is(static_cast<uintptr_t>(
                    refresh_target.resolved_storage[0]),
                kRollbackNativeCallbackVtableRvaMoveCache)
            || refresh_target.resolved_this == 0)
        {
            return false;
        }

        RollbackNativeInputCallbackState input_state {};
        if (!CaptureRollbackNativeInputCallbackState(
                input_target.resolved_this, read, input_state))
        {
            return false;
        }
        out.input_token = token.input_callbacks;
        out.simulation_token = token.simulation_callbacks;
        out.input_state = input_state;
        out.input_state_object = input_target.resolved_this;
        out.derived_cache_object = refresh_target.resolved_this;
        out.derived_cache_refresh = refresh_target.effective_target;
        out.input_inventory_digest = input.identity_digest;
        out.simulation_inventory_digest = simulation.identity_digest;
        out.valid = true;
        return true;
    }

    template <typename ReadPointerFn, typename ReadFn,
              typename ResolveWeakObjectFn, typename IsExecutableFn>
    static inline bool CaptureRollbackNativeCallbackCollectionInventory(
        uintptr_t collection,
        RollbackNativeCallbackCollectionKind kind,
        ReadPointerFn&& read_pointer,
        ReadFn&& read,
        ResolveWeakObjectFn&& resolve_weak_object,
        IsExecutableFn&& is_executable,
        uintptr_t virtual_5f8_thunk,
        RollbackNativeCallbackCollectionInventory& out) noexcept
    {
        out = {};
        out.kind = kind;
        out.collection = collection;
        uintptr_t heap_entries = 0;
        int32_t recursion_depth = -1;
        if (collection == 0
            || !read_pointer(collection + 0x40, heap_entries)
            || !read(collection + 0x50, &out.count, sizeof(out.count))
            || !read(collection + 0x64, &recursion_depth,
                sizeof(recursion_depth))
            || recursion_depth != 0 || out.count < 0
            || out.count > static_cast<int32_t>(out.targets.size())
            || (out.count > 1 && heap_entries == 0))
        {
            return false;
        }

        RollbackHash identity {};
        identity.add_scalar(static_cast<uint8_t>(kind));
        identity.add_scalar(out.count);
        const uintptr_t entry_base = heap_entries ? heap_entries : collection;
        for (int32_t i = 0; i < out.count; ++i)
        {
            auto& descriptor = out.targets[static_cast<size_t>(i)];
            descriptor.entry = entry_base
                + static_cast<uintptr_t>(i) * 0x40u;
            std::array<uint8_t, 0x40> entry_bytes {};
            uintptr_t external_target = 0;
            if (!read(descriptor.entry, entry_bytes.data(),
                    entry_bytes.size())
                || !read_pointer(descriptor.entry + 0x20,
                    external_target))
            {
                return false;
            }
            std::memcpy(&descriptor.entry_state,
                entry_bytes.data() + 0x30,
                sizeof(descriptor.entry_state));
            descriptor.external_target = external_target != 0;
            descriptor.target = external_target
                ? external_target : descriptor.entry;
            if (!read_pointer(descriptor.target, descriptor.vtable)
                || descriptor.vtable == 0
                || !read_pointer(descriptor.vtable + 0x68,
                    descriptor.dispatch)
                || descriptor.dispatch == 0
                 || !read(descriptor.target,
                     descriptor.target_storage.data(),
                     sizeof(descriptor.target_storage))
                 || !resolve_weak_object(
                     descriptor.target + 0x08,
                     descriptor.resolved_object))
            {
                return false;
            }
            descriptor.bound_function = static_cast<uintptr_t>(
                descriptor.target_storage[2]);
            std::memcpy(&descriptor.this_adjustment,
                &descriptor.target_storage[3],
                sizeof(descriptor.this_adjustment));
            if (descriptor.bound_function == 0
                || descriptor.this_adjustment < -0x100000
                || descriptor.this_adjustment > 0x100000)
            {
                return false;
            }
            descriptor.resolved_this = descriptor.this_adjustment >= 0
                ? descriptor.resolved_object
                    + static_cast<uintptr_t>(descriptor.this_adjustment)
                : descriptor.resolved_object
                    - static_cast<uintptr_t>(-descriptor.this_adjustment);
            if (descriptor.resolved_this == 0
                || !read(descriptor.resolved_this,
                    descriptor.resolved_storage.data(),
                    sizeof(descriptor.resolved_storage))
                || !is_executable(descriptor.dispatch)
                || !is_executable(descriptor.bound_function))
            {
                return false;
            }
            descriptor.virtual_5f8_applies =
                virtual_5f8_thunk != 0
                && descriptor.bound_function == virtual_5f8_thunk;
            descriptor.effective_target = descriptor.bound_function;
            if (descriptor.virtual_5f8_applies)
            {
                if (!read_pointer(
                        descriptor.resolved_this,
                        descriptor.resolved_vtable)
                    || descriptor.resolved_vtable == 0
                    || !read_pointer(
                        descriptor.resolved_vtable + 0x5F8,
                        descriptor.resolved_virtual_5f8)
                    || descriptor.resolved_virtual_5f8 == 0
                    || !is_executable(descriptor.resolved_virtual_5f8))
                {
                    return false;
                }
                descriptor.effective_target =
                    descriptor.resolved_virtual_5f8;
            }
            RollbackHash entry_hash {};
            entry_hash.add_bytes(entry_bytes.data(), entry_bytes.size());
            descriptor.entry_digest = entry_hash.value
                ? entry_hash.value : 1;
            RollbackHash target_hash {};
            target_hash.add_bytes(
                descriptor.target_storage.data(),
                sizeof(descriptor.target_storage));
            descriptor.target_storage_digest = target_hash.value
                ? target_hash.value : 1;
            RollbackHash resolved_hash {};
            resolved_hash.add_bytes(
                descriptor.resolved_storage.data(),
                sizeof(descriptor.resolved_storage));
            descriptor.resolved_storage_digest = resolved_hash.value
                ? resolved_hash.value : 1;

            identity.add_scalar(descriptor.entry_state);
            identity.add_scalar(descriptor.external_target);
            identity.add_scalar(descriptor.vtable);
            identity.add_scalar(descriptor.dispatch);
            identity.add_scalar(descriptor.bound_function);
            identity.add_scalar(descriptor.this_adjustment);
            identity.add_scalar(descriptor.virtual_5f8_applies);
            identity.add_scalar(descriptor.resolved_vtable);
            identity.add_scalar(descriptor.resolved_virtual_5f8);
            identity.add_scalar(descriptor.effective_target);
            identity.add_scalar(descriptor.entry_digest);
            identity.add_scalar(descriptor.target_storage_digest);
            identity.add_scalar(descriptor.resolved_storage_digest);
        }
        out.identity_digest = identity.value ? identity.value : 1;
        return out.valid();
    }

    static inline bool ValidateRollbackNativeSimulationIterationBoundary(
        const RollbackNativeSimulationIterationToken& token) noexcept
    {
        return token.valid
            && token.input_callbacks.valid()
            && token.simulation_callbacks.valid()
            && token.loop_again == 0
            && token.pending_dispatch == 0
            // Avoid the separate unpause callback collection. It is a stock
            // setup boundary and must be exhausted before rollback ownership.
            && token.unpause_grace_period == 0;
    }

    static constexpr int32_t kRollbackNativeDeferredTerminalGraceStart = 59;

    static inline bool
    ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
        const RollbackNativeSimulationIterationToken& token,
        bool terminal_notification_pending) noexcept
    {
        return token.valid
            && token.input_callbacks.valid()
            && token.simulation_callbacks.valid()
            && token.loop_again == 0
            && token.pending_dispatch == 0
            && (terminal_notification_pending
                ? token.unpause_grace_period >= 2
                    && token.unpause_grace_period
                        <= kRollbackNativeDeferredTerminalGraceStart
                : token.unpause_grace_period == 0);
    }

    static inline bool
    ValidateRollbackNativeSimulationIterationCompletionForDeferredTerminal(
        const RollbackNativeSimulationIterationToken& before,
        const RollbackNativeSimulationIterationToken& after,
        bool terminal_notification_pending_before,
        bool terminal_notification_pending_after) noexcept
    {
        if (!before.input_callbacks.unchanged_from(after.input_callbacks)
            || !before.simulation_callbacks.unchanged_from(
                after.simulation_callbacks))
        {
            return false;
        }
        if (!terminal_notification_pending_before
            && !terminal_notification_pending_after)
        {
            return ValidateRollbackNativeSimulationIterationBoundary(before)
                && ValidateRollbackNativeSimulationIterationBoundary(after);
        }
        if (!terminal_notification_pending_before
            && terminal_notification_pending_after)
        {
            return ValidateRollbackNativeSimulationIterationBoundary(before)
                && ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
                    after, true)
                && after.unpause_grace_period
                    == kRollbackNativeDeferredTerminalGraceStart;
        }
        return terminal_notification_pending_before
            && terminal_notification_pending_after
            && ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
                before, true)
            && ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
                after, true)
            && after.unpause_grace_period
                == before.unpause_grace_period - 1;
    }

    static inline uint32_t
    FilterRollbackNativeCallbackCoverageMismatchForDeferredTerminal(
        uint32_t mismatch,
        const RollbackNativeSimulationIterationToken& token,
        bool terminal_notification_pending) noexcept
    {
        if ((mismatch & RollbackNativeCallbackCoverageMismatchBoundary) != 0
            && ValidateRollbackNativeSimulationIterationBoundaryForDeferredTerminal(
                token, terminal_notification_pending))
        {
            mismatch &= ~RollbackNativeCallbackCoverageMismatchBoundary;
        }
        return mismatch;
    }

    static inline bool
    RollbackNativeCallbackPreflightAllowsHookInstallation(
        const RollbackNativeSimulationIterationToken& token,
        const RollbackNativeCallbackCoverage& coverage,
        const RollbackNativeInputCallbackState& input_state,
        bool inventory_only) noexcept
    {
        return !inventory_only
            && ValidateRollbackNativeSimulationIterationBoundary(token)
            && coverage.accepts(token, input_state);
    }

    template <typename InvokeFn>
    static inline bool RepairRollbackNativeDerivedCallbackState(
        uintptr_t image_base,
        const RollbackNativeCallbackCoverage& coverage,
        InvokeFn&& invoke) noexcept
    {
        if (!coverage.valid || image_base == 0
            || coverage.derived_cache_object == 0
            || coverage.derived_cache_refresh
                != image_base
                    + kRollbackNativeCallbackRvaRefreshMoveCaches)
        {
            return false;
        }
        return invoke(
            coverage.derived_cache_refresh,
            coverage.derived_cache_object);
    }

    enum class RollbackNativeLoadRepairResult : uint8_t
    {
        RestoreRejected,
        VerificationRejected,
        RepairFailed,
        Repaired,
    };

    template <typename RepairFn>
    static inline RollbackNativeLoadRepairResult
    CompleteRollbackNativeLoadRepair(
        bool restore_ok, bool verification_ok,
        RepairFn&& repair) noexcept
    {
        if (!restore_ok)
            return RollbackNativeLoadRepairResult::RestoreRejected;
        if (!verification_ok)
            return RollbackNativeLoadRepairResult::VerificationRejected;
        return repair()
            ? RollbackNativeLoadRepairResult::Repaired
            : RollbackNativeLoadRepairResult::RepairFailed;
    }

    static inline bool ValidateRollbackNativeSimulationIterationCompletion(
        const RollbackNativeSimulationIterationToken& before,
        const RollbackNativeSimulationIterationToken& after) noexcept
    {
        return ValidateRollbackNativeSimulationIterationBoundary(before)
            && ValidateRollbackNativeSimulationIterationBoundary(after)
            && after.input_callbacks.unchanged_from(
                before.input_callbacks)
            && after.simulation_callbacks.unchanged_from(
                before.simulation_callbacks);
    }

    // Wind is part of SC6's deterministic shared-RNG schedule. During the
    // bootstrap Advance(0), rollback ownership is established but the public
    // lifecycle is still WaitingForGekko. Admit only that explicitly owned
    // frame-zero invocation. During later-round handoff, admit stock wind only
    // while the deterministic NewRound countdown is current. RoundResult and
    // menu/transition residence time differs between processes and must never
    // become simulation evidence.
    static inline bool ShouldRunRollbackOwnedWindCallback(
        bool rollback_active,
        bool stock_new_round_countdown,
        bool waiting_for_gekko,
        bool owned_native_simulation,
        bool effect_frame_valid,
        uint32_t effect_frame) noexcept
    {
        if (stock_new_round_countdown)
            return true;
        if (!owned_native_simulation)
            return false;
        return rollback_active
            || (waiting_for_gekko
                && effect_frame_valid
                && effect_frame == 0);
    }
}
