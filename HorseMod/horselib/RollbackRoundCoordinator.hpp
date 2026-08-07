// ============================================================================
// Horse::RollbackRoundCoordinator
//
// Pure, allocation-free model for ownership across rounds in one stock match.
// The native simulation-boundary detour is the sole caller of
// observe_per_frame(); service work can inspect the coordinator but cannot
// nominate or freeze a boundary.
// ============================================================================

#pragma once

#include <array>
#include <cstdint>

namespace Horse
{
    enum class RollbackRoundPhase : uint8_t
    {
        Inactive,
        Active,
        ConfirmingRoundEnd,
        TerminalAccepted,
        StockInterRound,
        BaselineFrozen,
        StartingGekko,
        MatchComplete,
        FatalFrozen,
    };

    static constexpr const char* RollbackRoundPhaseName(
        RollbackRoundPhase phase) noexcept
    {
        switch (phase)
        {
        case RollbackRoundPhase::Inactive: return "inactive";
        case RollbackRoundPhase::Active: return "active";
        case RollbackRoundPhase::ConfirmingRoundEnd:
            return "confirming-round-end";
        case RollbackRoundPhase::TerminalAccepted:
            return "terminal-accepted";
        case RollbackRoundPhase::StockInterRound:
            return "stock-inter-round";
        case RollbackRoundPhase::BaselineFrozen:
            return "baseline-frozen";
        case RollbackRoundPhase::StartingGekko:
            return "starting-gekko";
        case RollbackRoundPhase::MatchComplete: return "match-complete";
        case RollbackRoundPhase::FatalFrozen: return "fatal-frozen";
        }
        return "unknown";
    }

    static constexpr bool RollbackStockMatchComplete(
        uint32_t wins0, uint32_t rounds_to_win0,
        uint32_t wins1, uint32_t rounds_to_win1) noexcept
    {
        return (rounds_to_win0 != 0 && wins0 >= rounds_to_win0)
            || (rounds_to_win1 != 0 && wins1 >= rounds_to_win1);
    }

    enum class RollbackPreControlCandidateDisposition : uint8_t
    {
        Candidate,
        NotDue,
        WrongMode,
        QueuedModePresent,
        Unreadable,
    };

    struct RollbackPreControlCandidateObservation
    {
        uintptr_t round_state {0};
        uintptr_t expected_new_round_state {0};
        uintptr_t current_mode {0};
        uintptr_t queued_mode {0};
        uint32_t mode_frame {0};
        uint32_t phase_timer {0};
        bool current_mode_read {false};
        bool queued_mode_read {false};
        bool mode_frame_read {false};
        bool phase_timer_read {false};
    };

    static constexpr RollbackPreControlCandidateDisposition
    ClassifyRollbackPreControlCandidate(
        const RollbackPreControlCandidateObservation& observation) noexcept
    {
        if (observation.round_state == 0
            || observation.expected_new_round_state == 0
            || !observation.current_mode_read
            || !observation.queued_mode_read
            || !observation.mode_frame_read
            || !observation.phase_timer_read)
        {
            return RollbackPreControlCandidateDisposition::Unreadable;
        }
        if (observation.round_state
                != observation.expected_new_round_state
            || observation.current_mode != observation.round_state)
        {
            return RollbackPreControlCandidateDisposition::WrongMode;
        }
        if (observation.queued_mode != 0)
        {
            return RollbackPreControlCandidateDisposition::QueuedModePresent;
        }
        // This observation is made at the enclosing SimulationLoop entry,
        // before PerFrame advances the world-mode pump. NewRound PostTick
        // sees mode_frame + 1, so freeze the iteration that would cross the
        // native threshold. A zero timer is immediately due.
        return observation.phase_timer != 0
                && observation.mode_frame < observation.phase_timer - 1u
            ? RollbackPreControlCandidateDisposition::NotDue
            : RollbackPreControlCandidateDisposition::Candidate;
    }

    static constexpr bool
    RollbackPreControlPassesNewRoundCountdown(
        const RollbackPreControlCandidateObservation& observation,
        bool stock_inter_round, uint64_t round_generation,
        bool round_restart_pending, bool transition_deferred) noexcept
    {
        return ClassifyRollbackPreControlCandidate(observation)
                == RollbackPreControlCandidateDisposition::NotDue
            && observation.current_mode == observation.round_state
            && observation.queued_mode == 0
            && (!stock_inter_round
                || (round_generation != 0
                    && !round_restart_pending
                    && !transition_deferred));
    }

    enum class RollbackNewRoundStockInterRoundAction : uint8_t
    {
        NotApplicable,
        PassCountdown,
        FreezeCandidate,
        Reject,
    };

    static constexpr RollbackNewRoundStockInterRoundAction
    ClassifyRollbackNewRoundStockInterRoundAction(
        const RollbackPreControlCandidateObservation& observation,
        uint8_t battle_main_state, uint8_t battle_status,
        uint64_t round_generation, bool round_restart_pending,
        bool transition_deferred) noexcept
    {
        if (!observation.current_mode_read
            || observation.current_mode != observation.round_state)
        {
            return RollbackNewRoundStockInterRoundAction::NotApplicable;
        }
        if (RollbackPreControlPassesNewRoundCountdown(
                observation, true, round_generation,
                round_restart_pending, transition_deferred))
        {
            if (battle_main_state != 2)
                return RollbackNewRoundStockInterRoundAction::Reject;
            if (battle_status == 3)
                return RollbackNewRoundStockInterRoundAction::PassCountdown;
            // BattleManager's round-sequence status is independent of the
            // NewRound world-mode counter. Live peers expose status 1 or 3
            // during the countdown; both remain bounded by the same native
            // pass-through audit and exact world-mode postconditions.
            if (battle_status == 1)
                return RollbackNewRoundStockInterRoundAction::PassCountdown;
            return RollbackNewRoundStockInterRoundAction::Reject;
        }
        // Live normal-mode evidence freezes NewRound at phase_timer - 1. The
        // next complete native iteration is logical frame 0 and must begin by
        // applying the queued ActiveBattle transition. Do not run a final
        // NewRound Tick here: it mutates fighter state one gameplay step beyond
        // the normal pre-control boundary.
        const bool pre_finalize_boundary =
            observation.phase_timer != 0
            && observation.mode_frame + 1u == observation.phase_timer;
        if (round_generation != 0
            && !round_restart_pending
            && !transition_deferred
            && pre_finalize_boundary
            && battle_main_state == 2
            && (battle_status == 1 || battle_status == 3))
        {
            return RollbackNewRoundStockInterRoundAction::FreezeCandidate;
        }
        return RollbackNewRoundStockInterRoundAction::Reject;
    }

    static constexpr bool RollbackNewRoundArmedAfterCallValid(
        const RollbackPreControlCandidateObservation& before,
        const RollbackPreControlCandidateObservation& after,
        bool due_finalize, uint32_t pending_after,
        uint64_t per_frame_calls,
        uint8_t battle_main_state, uint8_t battle_status,
        bool transition_deferred) noexcept
    {
        if (pending_after != 0
            || per_frame_calls != 1
            || !after.current_mode_read
            || !after.queued_mode_read
            || !after.mode_frame_read
            || !after.phase_timer_read
            || after.round_state != before.round_state
            || after.expected_new_round_state
                != before.expected_new_round_state
            || after.current_mode != before.current_mode
            || after.queued_mode != 0
            || after.mode_frame != before.mode_frame + 1u
            || after.phase_timer != before.phase_timer
            || battle_main_state != 2)
        {
            return false;
        }
        return !due_finalize
            && !transition_deferred
            && (battle_status == 1 || battle_status == 3)
            && ClassifyRollbackPreControlCandidate(before)
                == RollbackPreControlCandidateDisposition::NotDue;
    }

    static constexpr bool RollbackNewRoundZeroDeltaEpochSyncAfterCallValid(
        const RollbackPreControlCandidateObservation& before,
        const RollbackPreControlCandidateObservation& after,
        uint32_t pending_after, uint64_t per_frame_calls,
        uint8_t battle_main_state, uint8_t battle_status,
        bool transition_deferred) noexcept
    {
        return pending_after == 0
            && per_frame_calls == 0
            && !transition_deferred
            && after.current_mode_read
            && after.queued_mode_read
            && after.mode_frame_read
            && after.phase_timer_read
            && after.round_state == before.round_state
            && after.expected_new_round_state
                == before.expected_new_round_state
            && after.current_mode == before.current_mode
            && after.queued_mode == before.queued_mode
            && after.mode_frame == before.mode_frame
            && after.phase_timer == before.phase_timer
            && battle_main_state == 2
            && (battle_status == 1 || battle_status == 3)
            && ClassifyRollbackPreControlCandidate(before)
                == RollbackPreControlCandidateDisposition::NotDue;
    }

    enum class RollbackRearmCoordinateResult : uint8_t
    {
        Accepted,
        Inactive,
        WrongGeneration,
        InvalidCoordinate,
        Duplicate,
        SkippedOrReordered,
        Pending,
        NotComplete,
        Sealed,
    };

    // Transactional authority for the native NewRound countdown. Admission is
    // performed before native code runs and commit is performed only after the
    // existing clock/world-mode postconditions pass. A role whose InputLog
    // identity has just rolled over first commits one zero-PerFrame cursor
    // synchronization; a role already synchronized begins at coordinate 1.
    // That optional native bookkeeping call cannot occur after countdown work.
    // The first observable NewRound coordinate is native frame 1. Countdown
    // calls run only while coordinate + 1 < phase_timer. The last admitted
    // call advances to phase_timer - 1, which is the verified normal-mode
    // pre-control boundary. The sealed baseline queues ActiveBattle without
    // another NewRound Tick, and Gekko Advance(0) starts from that queued
    // transition. Neither service ticks nor wall time participate.
    class RollbackRearmCoordinateGate
    {
    public:
        RollbackRearmCoordinateResult arm(uint64_t generation) noexcept
        {
            reset();
            if (generation == 0)
                return RollbackRearmCoordinateResult::WrongGeneration;
            m_generation = generation;
            m_next_coordinate = 1;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult admit_zero_delta(
            uint64_t generation, uint32_t coordinate,
            uint32_t phase_timer) noexcept
        {
            const auto common = validate_open(
                generation, coordinate, phase_timer);
            if (common != RollbackRearmCoordinateResult::Accepted)
                return common;
            if (m_zero_delta_committed || m_step_committed
                || m_pending != Pending::None)
                return RollbackRearmCoordinateResult::Duplicate;
            if (coordinate != m_next_coordinate)
                return RollbackRearmCoordinateResult::SkippedOrReordered;
            m_pending = Pending::ZeroDelta;
            m_pending_coordinate = coordinate;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult commit_zero_delta(
            uint64_t generation, uint32_t before_coordinate,
            uint32_t after_coordinate, uint32_t phase_timer) noexcept
        {
            if (!pending_matches(Pending::ZeroDelta, generation,
                    before_coordinate, phase_timer))
            {
                return RollbackRearmCoordinateResult::Pending;
            }
            if (after_coordinate != before_coordinate)
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            clear_pending();
            m_zero_delta_committed = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult admit_step(
            uint64_t generation, uint32_t coordinate,
            uint32_t phase_timer, bool due_finalize) noexcept
        {
            const auto common = validate_open(
                generation, coordinate, phase_timer);
            if (common != RollbackRearmCoordinateResult::Accepted)
                return common;
            if (m_pending != Pending::None)
                return RollbackRearmCoordinateResult::Pending;
            if (coordinate < m_next_coordinate)
                return RollbackRearmCoordinateResult::Duplicate;
            if (coordinate != m_next_coordinate)
                return RollbackRearmCoordinateResult::SkippedOrReordered;
            if (due_finalize
                || coordinate + 1u >= phase_timer)
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            m_pending = Pending::Countdown;
            m_pending_coordinate = coordinate;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult commit_step(
            uint64_t generation, uint32_t before_coordinate,
            uint32_t after_coordinate, uint32_t phase_timer,
            bool due_finalize) noexcept
        {
            const Pending expected = Pending::Countdown;
            if (!pending_matches(expected, generation,
                    before_coordinate, phase_timer))
            {
                return RollbackRearmCoordinateResult::Pending;
            }
            if (due_finalize
                || after_coordinate != before_coordinate + 1u
                || after_coordinate >= phase_timer)
            {
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            }
            clear_pending();
            m_next_coordinate = after_coordinate;
            m_step_committed = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult seal(
            uint64_t generation, uint32_t coordinate,
            uint32_t phase_timer) noexcept
        {
            if (!active()) return RollbackRearmCoordinateResult::Inactive;
            if (m_sealed) return RollbackRearmCoordinateResult::Sealed;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (phase_timer < 2 || coordinate >= phase_timer
                || (m_phase_timer != 0 && m_phase_timer != phase_timer))
            {
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            }
            if (m_pending != Pending::None)
                return RollbackRearmCoordinateResult::Pending;
            const uint32_t final_countdown_coordinate = phase_timer - 1u;
            if (coordinate != final_countdown_coordinate
                || m_next_coordinate != final_countdown_coordinate)
            {
                return RollbackRearmCoordinateResult::NotComplete;
            }
            m_sealed = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        void reset() noexcept
        {
            m_generation = 0;
            m_phase_timer = 0;
            m_next_coordinate = 0;
            m_pending_coordinate = 0;
            m_pending = Pending::None;
            m_zero_delta_committed = false;
            m_step_committed = false;
            m_sealed = false;
        }

        bool active() const noexcept { return m_generation != 0; }
        bool sealed() const noexcept { return m_sealed; }
        uint64_t generation() const noexcept { return m_generation; }
        uint32_t next_coordinate() const noexcept
        {
            return m_next_coordinate;
        }

    private:
        enum class Pending : uint8_t
        {
            None,
            ZeroDelta,
            Countdown,
        };

        RollbackRearmCoordinateResult validate_open(
            uint64_t generation, uint32_t coordinate,
            uint32_t phase_timer) noexcept
        {
            if (!active()) return RollbackRearmCoordinateResult::Inactive;
            if (m_sealed) return RollbackRearmCoordinateResult::Sealed;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (coordinate == 0 || phase_timer < 2
                || coordinate >= phase_timer)
            {
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            }
            if (m_phase_timer == 0)
                m_phase_timer = phase_timer;
            else if (m_phase_timer != phase_timer)
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            return RollbackRearmCoordinateResult::Accepted;
        }

        bool pending_matches(
            Pending expected, uint64_t generation,
            uint32_t coordinate, uint32_t phase_timer) noexcept
        {
            return validate_open(generation, coordinate, phase_timer)
                    == RollbackRearmCoordinateResult::Accepted
                && m_pending == expected
                && m_pending_coordinate == coordinate;
        }

        void clear_pending() noexcept
        {
            m_pending = Pending::None;
            m_pending_coordinate = 0;
        }

        uint64_t m_generation {0};
        uint32_t m_phase_timer {0};
        uint32_t m_next_coordinate {0};
        uint32_t m_pending_coordinate {0};
        Pending m_pending {Pending::None};
        bool m_zero_delta_committed {false};
        bool m_step_committed {false};
        bool m_sealed {false};
    };

    enum class RollbackInterRoundWindMode : uint8_t
    {
        RoundResult,
        NewRound,
    };

    // RoundResult's mode frame is sampled before the native iteration. The
    // transition coordinate still executes that tick's emitter and wind-root
    // tail; the next observation is already another world mode and must not
    // execute another deterministic RoundResult callback.
    // Keeping the coordinate rule pure makes the boundary independently
    // testable.
    static constexpr bool RollbackRoundResultOwnsWindCoordinate(
        uint32_t mode_frame, uint32_t transition_frame) noexcept
    {
        return mode_frame > 0 && transition_frame > 0
            && mode_frame <= transition_frame;
    }

    // LuxBattle_AdvanceWorldModePump drains the round-result cinematic state
    // machine before calling the current mode Tick and incrementing +0x08.
    // Either native HgCpu restore can therefore rewind +0x08 during an
    // already-admitted pump call; the explicit increment leaves the next
    // observable coordinate at 2. The coordinate before the restore is
    // selected by cinematic timing/ring state and is not a fixed constant.
    // Only a rewind directly observed across that owned native call may arm
    // the next replay pass.
    static constexpr uint32_t
        kRollbackRoundResultReplayRestartCoordinate = 2;
    static constexpr uint32_t
        kRollbackRoundResultMaximumReplayRestarts = 2;

    static constexpr bool RollbackRoundResultNativePostRewindAllowed(
        uint32_t before_coordinate,
        uint32_t after_coordinate,
        uint32_t completed_restarts) noexcept
    {
        return before_coordinate
                > kRollbackRoundResultReplayRestartCoordinate
            && after_coordinate
                == kRollbackRoundResultReplayRestartCoordinate
            && completed_restarts
                < kRollbackRoundResultMaximumReplayRestarts;
    }

    // Stage wind is invoked from several stock inter-round world modes. Only
    // RoundResult and NewRound are deterministic parts of the next-round
    // simulation boundary. ActiveBattle/barrier residence is peer-dependent
    // and is observed only to close the RoundResult sequence. Admit each
    // phase-qualified native coordinate once, require contiguous coordinates
    // within each pass, admit at most two native-transaction-proven replay
    // rewinds, suppress a later RoundResult re-entry, and seal at the frozen
    // baseline.
    class RollbackInterRoundWindCoordinateGate
    {
    public:
        RollbackRearmCoordinateResult arm(uint64_t generation) noexcept
        {
            reset();
            if (generation == 0)
                return RollbackRearmCoordinateResult::WrongGeneration;
            m_generation = generation;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult observe_other(
            uint64_t generation) noexcept
        {
            if (!m_generation)
                return RollbackRearmCoordinateResult::Inactive;
            if (m_sealed)
                return RollbackRearmCoordinateResult::Sealed;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (m_round_result_seen)
                m_round_result_closed = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult admit(
            uint64_t generation, RollbackInterRoundWindMode mode,
            uintptr_t world_mode,
            uint32_t mode_frame, uint32_t phase_timer) noexcept
        {
            if (!m_generation)
                return RollbackRearmCoordinateResult::Inactive;
            if (m_sealed)
                return RollbackRearmCoordinateResult::Sealed;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (!world_mode)
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            const bool round_result =
                mode == RollbackInterRoundWindMode::RoundResult;
            if (round_result && (m_round_result_closed || m_new_round_seen))
                return RollbackRearmCoordinateResult::Duplicate;
            uint32_t& last_coordinate = round_result
                ? m_round_result_last_coordinate
                : m_new_round_last_coordinate;
            const bool seen = round_result
                ? m_round_result_seen : m_new_round_seen;
            bool starts_replay_pass = false;
            if (seen && mode_frame <= last_coordinate)
            {
                if (mode_frame == last_coordinate)
                    return RollbackRearmCoordinateResult::Duplicate;
                starts_replay_pass =
                    round_result
                    && m_pending_round_result_replay_restart
                    && last_coordinate
                        == m_pending_round_result_replay_from
                    && mode_frame
                        == m_pending_round_result_replay_coordinate;
                if (!starts_replay_pass)
                {
                    return
                        RollbackRearmCoordinateResult::SkippedOrReordered;
                }
            }
            // RoundResult ownership begins only after the bilateral terminal
            // handoff, by which time stock may have advanced this absolute
            // mode coordinate. Retain that first value as the origin and
            // require strict contiguity afterward. NewRound always starts at
            // its deterministic coordinate 1.
            const uint32_t expected_coordinate = seen
                ? starts_replay_pass
                    ? mode_frame
                    : last_coordinate + 1u
                : round_result ? mode_frame : 1u;
            if (mode_frame != expected_coordinate)
                return RollbackRearmCoordinateResult::SkippedOrReordered;
            if (!round_result)
            {
                m_round_result_closed = m_round_result_seen;
                m_new_round_seen = true;
            }
            else
            {
                m_round_result_seen = true;
                if (starts_replay_pass)
                {
                    ++m_round_result_replay_restarts;
                    m_pending_round_result_replay_restart = false;
                    m_pending_round_result_replay_from = 0;
                    m_pending_round_result_replay_coordinate = 0;
                }
            }
            m_world_mode = world_mode;
            m_mode_frame = mode_frame;
            m_phase_timer = phase_timer;
            last_coordinate = mode_frame;
            m_have_coordinate = true;
            ++m_admitted;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult observe_native_post_iteration(
            uint64_t generation, RollbackInterRoundWindMode mode,
            uintptr_t world_mode, uint32_t before_coordinate,
            uint32_t after_coordinate, uint32_t phase_timer,
            bool verified_zero_delta_epoch_sync = false) noexcept
        {
            if (!m_generation)
                return RollbackRearmCoordinateResult::Inactive;
            if (m_sealed)
                return RollbackRearmCoordinateResult::Sealed;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (!m_have_coordinate || !world_mode
                || world_mode != m_world_mode
                || before_coordinate != m_mode_frame
                || phase_timer != m_phase_timer)
            {
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            }
            if (after_coordinate == before_coordinate + 1u)
                return RollbackRearmCoordinateResult::Accepted;
            // SC6's first NewRound call after InputLog identity rollover is a
            // verified zero-PerFrame cursor synchronization. The world mode
            // remains at deterministic coordinate 1. Admit that unchanged
            // post-coordinate exactly once and only when the separate native
            // clock transaction has proved the zero-delta epoch sync.
            if (mode == RollbackInterRoundWindMode::NewRound
                && before_coordinate == 1u
                && after_coordinate == before_coordinate
                && verified_zero_delta_epoch_sync
                && !m_new_round_zero_delta_post_seen)
            {
                m_new_round_zero_delta_post_seen = true;
                return RollbackRearmCoordinateResult::Accepted;
            }
            if (mode != RollbackInterRoundWindMode::RoundResult
                || m_pending_round_result_replay_restart
                || !RollbackRoundResultNativePostRewindAllowed(
                    before_coordinate, after_coordinate,
                    m_round_result_replay_restarts))
            {
                return RollbackRearmCoordinateResult::SkippedOrReordered;
            }
            m_pending_round_result_replay_restart = true;
            m_pending_round_result_replay_from = before_coordinate;
            m_pending_round_result_replay_coordinate = after_coordinate;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult seal(uint64_t generation) noexcept
        {
            if (!m_generation)
                return RollbackRearmCoordinateResult::Inactive;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (m_sealed)
                return RollbackRearmCoordinateResult::Sealed;
            if (!m_have_coordinate || !m_round_result_seen
                || !m_round_result_closed || !m_new_round_seen
                || m_pending_round_result_replay_restart)
                return RollbackRearmCoordinateResult::NotComplete;
            m_sealed = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        void reset() noexcept
        {
            m_generation = 0;
            m_world_mode = 0;
            m_mode_frame = 0;
            m_phase_timer = 0;
            m_admitted = 0;
            m_round_result_last_coordinate = 0;
            m_new_round_last_coordinate = 0;
            m_round_result_replay_restarts = 0;
            m_pending_round_result_replay_from = 0;
            m_pending_round_result_replay_coordinate = 0;
            m_have_coordinate = false;
            m_round_result_seen = false;
            m_round_result_closed = false;
            m_new_round_seen = false;
            m_new_round_zero_delta_post_seen = false;
            m_pending_round_result_replay_restart = false;
            m_sealed = false;
        }

        uint64_t admitted() const noexcept { return m_admitted; }
        uint32_t round_result_coordinates() const noexcept
        {
            return m_round_result_last_coordinate;
        }
        uint32_t new_round_coordinates() const noexcept
        {
            return m_new_round_last_coordinate;
        }
        uint32_t round_result_replay_restarts() const noexcept
        {
            return m_round_result_replay_restarts;
        }
        uint64_t generation() const noexcept { return m_generation; }
        uintptr_t world_mode() const noexcept { return m_world_mode; }
        uint32_t mode_frame() const noexcept { return m_mode_frame; }
        uint32_t phase_timer() const noexcept { return m_phase_timer; }
        bool round_result_seen() const noexcept
        {
            return m_round_result_seen;
        }
        bool round_result_closed() const noexcept
        {
            return m_round_result_closed;
        }
        bool new_round_seen() const noexcept { return m_new_round_seen; }
        bool replay_restart_pending() const noexcept
        {
            return m_pending_round_result_replay_restart;
        }
        uint32_t replay_restart_from() const noexcept
        {
            return m_pending_round_result_replay_from;
        }
        uint32_t replay_restart_coordinate() const noexcept
        {
            return m_pending_round_result_replay_coordinate;
        }
        bool sealed() const noexcept { return m_sealed; }

    private:
        uint64_t m_generation {0};
        uintptr_t m_world_mode {0};
        uint32_t m_mode_frame {0};
        uint32_t m_phase_timer {0};
        uint64_t m_admitted {0};
        uint32_t m_round_result_last_coordinate {0};
        uint32_t m_new_round_last_coordinate {0};
        uint32_t m_round_result_replay_restarts {0};
        uint32_t m_pending_round_result_replay_from {0};
        uint32_t m_pending_round_result_replay_coordinate {0};
        bool m_have_coordinate {false};
        bool m_round_result_seen {false};
        bool m_round_result_closed {false};
        bool m_new_round_seen {false};
        bool m_new_round_zero_delta_post_seen {false};
        bool m_pending_round_result_replay_restart {false};
        bool m_sealed {false};
    };

    static constexpr bool RollbackPreNewRoundAwaitsFinalRoundResultCapture(
        bool first_arrival,
        bool carried_state_captured,
        bool outer_wind_iteration_admitted,
        bool outer_mode_is_round_result,
        uint32_t outer_coordinate,
        uint32_t transition_coordinate) noexcept
    {
        return first_arrival
            && !carried_state_captured
            && outer_wind_iteration_admitted
            && outer_mode_is_round_result
            && transition_coordinate != 0
            && outer_coordinate == transition_coordinate;
    }

    static constexpr bool RollbackFinalRoundResultPostCallAllowed(
        bool outer_wind_iteration_admitted,
        bool outer_mode_is_round_result,
        uint32_t outer_coordinate,
        uint32_t transition_coordinate,
        bool carried_state_captured,
        bool current_mode_is_pre_new_round,
        bool queued_mode_clear) noexcept
    {
        return outer_wind_iteration_admitted
            && outer_mode_is_round_result
            && transition_coordinate != 0
            && outer_coordinate == transition_coordinate
            && carried_state_captured
            && current_mode_is_pre_new_round
            && queued_mode_clear;
    }

    static constexpr bool RollbackFinalRoundResultZeroPerFrameAllowed(
        bool final_capture_completed,
        bool transition_callback_deferred,
        uint32_t pending_armed,
        uint32_t pending_after,
        bool match_identity_stable,
        bool control_state_allowed) noexcept
    {
        return final_capture_completed
            && transition_callback_deferred
            && pending_armed == 1
            && pending_after == 0
            && match_identity_stable
            && control_state_allowed;
    }

    // The stock PreNewRound transition is a bilateral hold point, but the
    // surrounding native PerFrame tail continues to advance fighter and wind
    // state while either peer waits. Preserve the last deterministic
    // RoundResult coordinate and restore it exactly once, immediately before
    // releasing PreNewRound into NewRound. The two-phase restore contract
    // prevents a partial native write from being reported as committed.
    class RollbackInterRoundCarriedStateGate
    {
    public:
        RollbackRearmCoordinateResult arm(
            uint64_t generation, uint32_t capture_coordinate) noexcept
        {
            reset();
            if (generation == 0 || capture_coordinate == 0)
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            m_generation = generation;
            m_capture_coordinate = capture_coordinate;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult capture(
            uint64_t generation, uint32_t coordinate) noexcept
        {
            if (!m_generation)
                return RollbackRearmCoordinateResult::Inactive;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (m_restore_committed)
                return RollbackRearmCoordinateResult::Sealed;
            if (coordinate != m_capture_coordinate)
                return RollbackRearmCoordinateResult::InvalidCoordinate;
            if (m_captured)
                return RollbackRearmCoordinateResult::Duplicate;
            m_captured = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult begin_restore(
            uint64_t generation) noexcept
        {
            if (!m_generation)
                return RollbackRearmCoordinateResult::Inactive;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (m_restore_committed)
                return RollbackRearmCoordinateResult::Sealed;
            if (!m_captured)
                return RollbackRearmCoordinateResult::NotComplete;
            if (m_restore_pending)
                return RollbackRearmCoordinateResult::Duplicate;
            m_restore_pending = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        RollbackRearmCoordinateResult commit_restore(
            uint64_t generation) noexcept
        {
            if (!m_generation)
                return RollbackRearmCoordinateResult::Inactive;
            if (generation != m_generation)
                return RollbackRearmCoordinateResult::WrongGeneration;
            if (m_restore_committed)
                return RollbackRearmCoordinateResult::Sealed;
            if (!m_restore_pending || !m_captured)
                return RollbackRearmCoordinateResult::NotComplete;
            m_restore_pending = false;
            m_restore_committed = true;
            return RollbackRearmCoordinateResult::Accepted;
        }

        void reset() noexcept
        {
            m_generation = 0;
            m_capture_coordinate = 0;
            m_captured = false;
            m_restore_pending = false;
            m_restore_committed = false;
        }

        uint64_t generation() const noexcept { return m_generation; }
        uint32_t capture_coordinate() const noexcept
        {
            return m_capture_coordinate;
        }
        bool captured() const noexcept { return m_captured; }
        bool restore_pending() const noexcept { return m_restore_pending; }
        bool restore_committed() const noexcept
        {
            return m_restore_committed;
        }

    private:
        uint64_t m_generation {0};
        uint32_t m_capture_coordinate {0};
        bool m_captured {false};
        bool m_restore_pending {false};
        bool m_restore_committed {false};
    };

    enum class RollbackPreControlIterationPhase : uint8_t
    {
        PassThrough,
        Held,
        BaselineCaptured,
        FrameZeroExecuting,
        OutOfBattleAuthorized,
    };

    class RollbackPreControlIterationLifetime
    {
    public:
        bool freeze() noexcept
        {
            if (m_phase == RollbackPreControlIterationPhase::PassThrough)
                m_phase = RollbackPreControlIterationPhase::Held;
            return m_phase == RollbackPreControlIterationPhase::Held;
        }

        bool capture_baseline() noexcept
        {
            if (m_phase != RollbackPreControlIterationPhase::Held)
                return false;
            m_phase = RollbackPreControlIterationPhase::BaselineCaptured;
            return true;
        }

        bool begin_frame_zero() noexcept
        {
            if (m_phase
                == RollbackPreControlIterationPhase::FrameZeroExecuting)
            {
                return true;
            }
            if (m_phase
                != RollbackPreControlIterationPhase::BaselineCaptured)
            {
                return false;
            }
            m_phase = RollbackPreControlIterationPhase::FrameZeroExecuting;
            return true;
        }

        bool complete_frame_zero() noexcept
        {
            if (m_phase
                != RollbackPreControlIterationPhase::FrameZeroExecuting)
                return false;
            reset();
            return true;
        }

        bool authorize_out_of_battle_shutdown() noexcept
        {
            if (!frozen()) return true;
            m_phase = RollbackPreControlIterationPhase::OutOfBattleAuthorized;
            return true;
        }

        bool frozen() const noexcept
        {
            return m_phase != RollbackPreControlIterationPhase::PassThrough;
        }
        bool shutdown_authorized() const noexcept
        {
            return m_phase
                == RollbackPreControlIterationPhase::OutOfBattleAuthorized;
        }
        bool shutdown_allowed() const noexcept
        {
            return !frozen() || shutdown_authorized();
        }
        RollbackPreControlIterationPhase phase() const noexcept
        {
            return m_phase;
        }
        void reset() noexcept
        {
            m_phase = RollbackPreControlIterationPhase::PassThrough;
        }

    private:
        RollbackPreControlIterationPhase m_phase {
            RollbackPreControlIterationPhase::PassThrough};
    };

    enum class RollbackPreControlShutdownDecision : uint8_t
    {
        Allow,
        DeferForCallback,
        DenyHeldIteration,
    };

    static constexpr RollbackPreControlShutdownDecision
    ClassifyRollbackPreControlShutdown(
        bool current_thread_inside_callback,
        const RollbackPreControlIterationLifetime& lifetime) noexcept
    {
        if (current_thread_inside_callback)
            return RollbackPreControlShutdownDecision::DeferForCallback;
        return lifetime.shutdown_allowed()
            ? RollbackPreControlShutdownDecision::Allow
            : RollbackPreControlShutdownDecision::DenyHeldIteration;
    }

    enum class RollbackPerFrameAction : uint8_t
    {
        RunRollbackAdvance,
        CallStockTrampoline,
        FreezeBeforeTrampoline,
        SkipTrampoline,
    };

    struct RollbackStockMatchIdentity
    {
        uintptr_t battle_manager {0};
        uintptr_t input_log {0};
        std::array<uintptr_t, 2> fighters {};
        std::array<uintptr_t, 2> presentation_actors {};
        uintptr_t stage_manager {0};
        uint64_t actor_pointer_order_hash {0};
        uint64_t stage_identity {0};
        uint8_t local_slot {0xff};
        uint8_t remote_slot {0xff};
        uint64_t session_epoch {0};
        uint64_t udp_handshake_generation {0};
        uint64_t selection_hash {0};
        uint64_t configuration_hash {0};

        bool valid() const noexcept
        {
            return battle_manager != 0 && input_log != 0
                && fighters[0] != 0 && fighters[1] != 0
                && presentation_actors[0] != 0
                && presentation_actors[1] != 0
                && stage_manager != 0 && actor_pointer_order_hash != 0
                && stage_identity != 0 && local_slot < 2
                && remote_slot < 2 && local_slot != remote_slot
                && session_epoch != 0 && udp_handshake_generation != 0
                && selection_hash != 0 && configuration_hash != 0;
        }

        bool same_native_match(
            const RollbackStockMatchIdentity& other) const noexcept
        {
            return battle_manager == other.battle_manager
                && input_log == other.input_log
                && fighters == other.fighters
                && presentation_actors == other.presentation_actors
                && stage_manager == other.stage_manager
                && actor_pointer_order_hash == other.actor_pointer_order_hash
                && stage_identity == other.stage_identity
                && local_slot == other.local_slot
                && remote_slot == other.remote_slot
                && session_epoch == other.session_epoch
                && udp_handshake_generation
                    == other.udp_handshake_generation
                && selection_hash == other.selection_hash
                && configuration_hash == other.configuration_hash;
        }
    };

    struct RollbackRoundIdentity
    {
        uint64_t round_generation {0};
        uint32_t round_ordinal {0};
        uint64_t round_start_digest {0};
        uint8_t initial_battle_status {0};
        uint8_t initial_main_state {0};
        uint32_t replay_round_index {0};
        uint64_t replay_digest {0};
        uint64_t canonical_baseline_hash {0};
        std::array<uint64_t, 4> component_hashes {};
        uint64_t native_boundary_frame {0};
        uint64_t input_log_frame {0};

        bool valid() const noexcept
        {
            if (round_generation == 0 || round_start_digest == 0
                || (initial_battle_status != 1
                    && initial_battle_status != 2)
                || initial_main_state != 2
                || replay_digest == 0 || canonical_baseline_hash == 0)
            {
                return false;
            }
            for (uint64_t hash : component_hashes)
            {
                if (hash == 0) return false;
            }
            return true;
        }
    };

    struct RollbackRoundCandidate
    {
        RollbackStockMatchIdentity match {};
        uint32_t round_ordinal {0};
        uint64_t round_start_digest {0};
        uint8_t battle_status {0};
        uint8_t main_state {0};
        bool player_match_scene_active {false};
        bool pvp_presence {false};
        bool native_slots_valid {false};
        bool input_log_valid {false};
        bool fighters_valid {false};
        bool stage_valid {false};
        bool auto_advance_clear {false};
        uint64_t native_boundary_frame {0};
        uint64_t input_log_frame {0};

        bool is_pre_control_boundary() const noexcept
        {
            return battle_status == 2 && main_state == 2
                && player_match_scene_active && pvp_presence
                && native_slots_valid && input_log_valid && fighters_valid
                && stage_valid && auto_advance_clear
                && round_start_digest != 0;
        }
    };

    struct RollbackRoundBaseline
    {
        uint64_t session_epoch {0};
        uint32_t completed_round_ordinal {0};
        RollbackRoundIdentity round {};
        uint8_t native_slot {0xff};
        uint64_t match_identity_digest {0};
        uint64_t round_identity_digest {0};
        uint64_t stage_identity {0};
    };

    struct RollbackRoundCoordinatorCounters
    {
        uint64_t stock_pass_through_calls {0};
        uint64_t calls_while_frozen {0};
        uint64_t round_candidates_frozen {0};
        uint64_t round_identities_captured {0};
        uint64_t round_baselines_published {0};
        uint64_t round_baselines_accepted {0};
        uint64_t round_gekko_restarts {0};
        uint64_t stock_round_transition_rearms {0};
        uint64_t frame_zero_executions {0};
    };

    class RollbackRoundCoordinator
    {
    public:
        bool initialize(
            const RollbackStockMatchIdentity& match,
            uint32_t active_round_ordinal,
            uint64_t active_round_generation,
            uint64_t match_identity_digest) noexcept
        {
            reset();
            if (!match.valid() || active_round_generation == 0
                || match_identity_digest == 0)
            {
                return fail("invalid-initial-match-identity");
            }
            m_match = match;
            m_completed_round_ordinal = active_round_ordinal;
            m_round_generation = active_round_generation;
            m_match_identity_digest = match_identity_digest;
            m_phase = RollbackRoundPhase::Active;
            m_failure = "ok";
            return true;
        }

        void reset() noexcept
        {
            m_phase = RollbackRoundPhase::FatalFrozen;
            m_match = {};
            m_round = {};
            m_local_baseline = {};
            m_completed_round_ordinal = 0;
            m_expected_round_ordinal = 0;
            m_round_generation = 0;
            m_match_identity_digest = 0;
            m_round_epoch = 0;
            m_local_baseline_published = false;
            m_peer_baseline_accepted = false;
            m_counters = {};
            m_failure = "not-initialized";
        }

        bool begin_terminal_confirmation() noexcept
        {
            if (m_phase != RollbackRoundPhase::Active)
                return fail("terminal-confirmation-out-of-phase");
            m_phase = RollbackRoundPhase::ConfirmingRoundEnd;
            return true;
        }

        bool accept_terminal() noexcept
        {
            if (m_phase != RollbackRoundPhase::ConfirmingRoundEnd)
                return fail("terminal-accept-out-of-phase");
            m_phase = RollbackRoundPhase::TerminalAccepted;
            return true;
        }

        bool release_terminal_to_stock(bool match_complete) noexcept
        {
            if (m_phase != RollbackRoundPhase::TerminalAccepted)
                return fail("terminal-release-out-of-phase");
            if (match_complete)
            {
                m_phase = RollbackRoundPhase::MatchComplete;
                return true;
            }

            if (m_completed_round_ordinal == UINT32_MAX)
                return fail("round-ordinal-overflow");
            m_expected_round_ordinal = m_completed_round_ordinal + 1u;
            m_local_baseline = {};
            m_round = {};
            m_round_epoch = 0;
            m_local_baseline_published = false;
            m_peer_baseline_accepted = false;
            m_phase = RollbackRoundPhase::StockInterRound;
            return true;
        }

        RollbackPerFrameAction observe_per_frame(
            const RollbackRoundCandidate& candidate) noexcept
        {
            if (m_phase == RollbackRoundPhase::Active
                || m_phase == RollbackRoundPhase::ConfirmingRoundEnd)
            {
                return RollbackPerFrameAction::RunRollbackAdvance;
            }

            if (m_phase == RollbackRoundPhase::FatalFrozen
                || m_phase == RollbackRoundPhase::TerminalAccepted
                || m_phase == RollbackRoundPhase::BaselineFrozen
                || m_phase == RollbackRoundPhase::StartingGekko)
            {
                ++m_counters.calls_while_frozen;
                return RollbackPerFrameAction::SkipTrampoline;
            }

            if (m_phase == RollbackRoundPhase::MatchComplete)
                return RollbackPerFrameAction::CallStockTrampoline;

            if (m_phase != RollbackRoundPhase::StockInterRound)
            {
                fail("unknown-round-phase");
                return RollbackPerFrameAction::SkipTrampoline;
            }

            if (!candidate.match.valid()
                || !m_match.same_native_match(candidate.match))
            {
                fail("stock-match-identity-changed");
                return RollbackPerFrameAction::SkipTrampoline;
            }

            if (candidate.round_ordinal != m_completed_round_ordinal
                && candidate.round_ordinal != m_expected_round_ordinal)
            {
                fail("next-round-ordinal-mismatch");
                return RollbackPerFrameAction::SkipTrampoline;
            }

            if (candidate.round_ordinal == m_expected_round_ordinal
                && candidate.is_pre_control_boundary())
            {
                if (m_round_generation == UINT64_MAX)
                {
                    fail("round-generation-overflow");
                    return RollbackPerFrameAction::SkipTrampoline;
                }
                ++m_round_generation;
                m_round.round_generation = m_round_generation;
                m_round.round_ordinal = candidate.round_ordinal;
                m_round.round_start_digest = candidate.round_start_digest;
                m_round.initial_battle_status = candidate.battle_status;
                m_round.initial_main_state = candidate.main_state;
                m_round.native_boundary_frame =
                    candidate.native_boundary_frame;
                m_round.input_log_frame = candidate.input_log_frame;
                ++m_counters.round_candidates_frozen;
                m_phase = RollbackRoundPhase::BaselineFrozen;
                return RollbackPerFrameAction::FreezeBeforeTrampoline;
            }

            ++m_counters.stock_pass_through_calls;
            return RollbackPerFrameAction::CallStockTrampoline;
        }

        bool publish_local_baseline(
            uint32_t replay_round_index,
            uint64_t replay_digest,
            uint64_t canonical_baseline_hash,
            const std::array<uint64_t, 4>& component_hashes,
            uint64_t round_identity_digest) noexcept
        {
            if (m_phase != RollbackRoundPhase::BaselineFrozen)
                return fail("baseline-publish-out-of-phase");
            if (m_local_baseline_published)
                return fail("baseline-already-published");

            m_round.replay_round_index = replay_round_index;
            m_round.replay_digest = replay_digest;
            m_round.canonical_baseline_hash = canonical_baseline_hash;
            m_round.component_hashes = component_hashes;
            if (!m_round.valid() || round_identity_digest == 0)
                return fail("invalid-round-baseline");

            m_local_baseline.session_epoch = m_match.session_epoch;
            m_local_baseline.completed_round_ordinal =
                m_completed_round_ordinal;
            m_local_baseline.round = m_round;
            m_local_baseline.native_slot = m_match.local_slot;
            m_local_baseline.match_identity_digest =
                m_match_identity_digest;
            m_local_baseline.round_identity_digest = round_identity_digest;
            m_local_baseline.stage_identity = m_match.stage_identity;
            m_local_baseline_published = true;
            ++m_counters.round_identities_captured;
            ++m_counters.round_baselines_published;
            return true;
        }

        bool accept_peer_baseline(
            const RollbackRoundBaseline& peer) noexcept
        {
            if (m_phase != RollbackRoundPhase::BaselineFrozen
                || !m_local_baseline_published)
            {
                return fail("baseline-accept-out-of-phase");
            }
            if (m_peer_baseline_accepted)
                return true;

            const RollbackRoundBaseline& local = m_local_baseline;
            if (peer.session_epoch != local.session_epoch
                || peer.completed_round_ordinal
                    != local.completed_round_ordinal
                || peer.native_slot >= 2
                || peer.native_slot == local.native_slot
                || peer.match_identity_digest
                    != local.match_identity_digest
                || peer.round.round_generation
                    != local.round.round_generation
                || peer.round.round_ordinal != local.round.round_ordinal
                || peer.round.round_start_digest
                    != local.round.round_start_digest
                || peer.round.initial_battle_status
                    != local.round.initial_battle_status
                || peer.round.initial_main_state
                    != local.round.initial_main_state
                || peer.round.replay_round_index
                    != local.round.replay_round_index
                || peer.round.replay_digest != local.round.replay_digest
                || peer.round.canonical_baseline_hash
                    != local.round.canonical_baseline_hash
                || peer.round.component_hashes
                    != local.round.component_hashes
                || peer.round_identity_digest
                    != local.round_identity_digest
                || peer.stage_identity != local.stage_identity)
            {
                return fail("round-baseline-mismatch");
            }

            // Native boundary and InputLog frames are deliberately excluded.
            m_round_epoch = derive_round_epoch(local);
            if (m_round_epoch == 0)
                return fail("round-epoch-derivation-failed");
            m_peer_baseline_accepted = true;
            ++m_counters.round_baselines_accepted;
            m_phase = RollbackRoundPhase::StartingGekko;
            return true;
        }

        bool complete_gekko_restart(bool frame_zero_executed) noexcept
        {
            if (m_phase != RollbackRoundPhase::StartingGekko)
                return fail("gekko-restart-out-of-phase");
            if (!m_peer_baseline_accepted || !frame_zero_executed)
                return fail("frame-zero-execution-failed");

            ++m_counters.frame_zero_executions;
            ++m_counters.round_gekko_restarts;
            ++m_counters.stock_round_transition_rearms;
            m_completed_round_ordinal = m_round.round_ordinal;
            m_phase = RollbackRoundPhase::Active;
            return true;
        }

        // Service work is intentionally observation-only. The returned phase
        // lets the control plane pump messages and emit telemetry without
        // acquiring boundary authority.
        RollbackRoundPhase observe_service() const noexcept
        {
            return m_phase;
        }

        RollbackRoundPhase phase() const noexcept { return m_phase; }
        const char* failure() const noexcept { return m_failure; }
        uint64_t round_epoch() const noexcept { return m_round_epoch; }
        uint64_t round_generation() const noexcept
        {
            return m_round_generation;
        }
        uint32_t expected_round_ordinal() const noexcept
        {
            return m_expected_round_ordinal;
        }
        const RollbackStockMatchIdentity& match_identity() const noexcept
        {
            return m_match;
        }
        const RollbackRoundIdentity& round_identity() const noexcept
        {
            return m_round;
        }
        const RollbackRoundBaseline& local_baseline() const noexcept
        {
            return m_local_baseline;
        }
        const RollbackRoundCoordinatorCounters& counters() const noexcept
        {
            return m_counters;
        }

    private:
        bool fail(const char* failure) noexcept
        {
            m_failure = failure;
            m_phase = RollbackRoundPhase::FatalFrozen;
            return false;
        }

        static uint64_t mix(uint64_t hash, uint64_t value) noexcept
        {
            hash ^= value;
            hash *= 1099511628211ull;
            return hash;
        }

        static uint64_t derive_round_epoch(
            const RollbackRoundBaseline& baseline) noexcept
        {
            uint64_t hash = 1469598103934665603ull;
            hash = mix(hash, baseline.session_epoch);
            hash = mix(hash, baseline.round.round_generation);
            hash = mix(hash, baseline.round.round_ordinal);
            hash = mix(hash, baseline.round.replay_round_index);
            hash = mix(hash, baseline.round.replay_digest);
            hash = mix(hash, baseline.round.canonical_baseline_hash);
            hash = mix(hash, baseline.round_identity_digest);
            return hash;
        }

        RollbackRoundPhase m_phase {RollbackRoundPhase::FatalFrozen};
        RollbackStockMatchIdentity m_match {};
        RollbackRoundIdentity m_round {};
        RollbackRoundBaseline m_local_baseline {};
        uint32_t m_completed_round_ordinal {0};
        uint32_t m_expected_round_ordinal {0};
        uint64_t m_round_generation {0};
        uint64_t m_match_identity_digest {0};
        uint64_t m_round_epoch {0};
        bool m_local_baseline_published {false};
        bool m_peer_baseline_accepted {false};
        RollbackRoundCoordinatorCounters m_counters {};
        const char* m_failure {"not-initialized"};
    };
}
