#include "../HorseMod/horselib/RollbackRoundCoordinator.hpp"
#include "../HorseMod/horselib/RollbackNativeSimulationIteration.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace Horse;

    RollbackStockMatchIdentity make_match(uint8_t local_slot) noexcept
    {
        RollbackStockMatchIdentity out {};
        // Each peer has a different address space. These local values must
        // remain stable within that peer, but never enter the shared digest.
        const uintptr_t process_offset =
            static_cast<uintptr_t>(local_slot) * 0x100000;
        out.battle_manager = 0x1000 + process_offset;
        out.input_log = 0x2000 + process_offset;
        out.fighters = {0x3000 + process_offset, 0x4000 + process_offset};
        out.presentation_actors = {
            0x5000 + process_offset, 0x6000 + process_offset};
        out.stage_manager = 0x7000 + process_offset;
        out.actor_pointer_order_hash = 0x8000 + process_offset;
        out.stage_identity = 0x9000;
        out.local_slot = local_slot;
        out.remote_slot = static_cast<uint8_t>(1u - local_slot);
        out.session_epoch = 0xa000;
        out.udp_handshake_generation = 7;
        out.selection_hash = 0xb000;
        out.configuration_hash = 0xc000;
        return out;
    }

    RollbackRoundCandidate make_candidate(
        const RollbackStockMatchIdentity& match,
        uint32_t ordinal,
        uint8_t status,
        uint64_t native_frame) noexcept
    {
        RollbackRoundCandidate out {};
        out.match = match;
        out.round_ordinal = ordinal;
        out.round_start_digest = 0xd000 + ordinal;
        out.battle_status = status;
        out.main_state = status == 1 || status == 2 ? 2 : 4;
        out.player_match_scene_active = true;
        out.pvp_presence = true;
        out.native_slots_valid = true;
        out.input_log_valid = true;
        out.fighters_valid = true;
        out.stage_valid = true;
        out.auto_advance_clear = true;
        out.native_boundary_frame = native_frame;
        out.input_log_frame = native_frame + 20;
        return out;
    }

    bool arm_inter_round(RollbackRoundCoordinator& coordinator) noexcept
    {
        return coordinator.begin_terminal_confirmation()
            && coordinator.accept_terminal()
            && coordinator.release_terminal_to_stock(false)
            && coordinator.phase() == RollbackRoundPhase::StockInterRound;
    }

    bool publish_pair(
        RollbackRoundCoordinator& left,
        RollbackRoundCoordinator& right,
        uint32_t replay_round) noexcept
    {
        const std::array<uint64_t, 4> hashes {
            0x1100 + replay_round,
            0x2200 + replay_round,
            0x3300 + replay_round,
            0x4400 + replay_round,
        };
        return left.publish_local_baseline(
                replay_round, 0x5500 + replay_round,
                0x6600 + replay_round, hashes, 0x7700 + replay_round)
            && right.publish_local_baseline(
                replay_round, 0x5500 + replay_round,
                0x6600 + replay_round, hashes, 0x7700 + replay_round)
            && left.accept_peer_baseline(right.local_baseline())
            && right.accept_peer_baseline(left.local_baseline());
    }

    bool exercise_asymmetric_round(
        RollbackRoundCoordinator& left,
        RollbackRoundCoordinator& right,
        const RollbackStockMatchIdentity& left_match,
        const RollbackStockMatchIdentity& right_match,
        uint32_t next_ordinal,
        uint32_t replay_round,
        unsigned left_delay,
        unsigned right_delay) noexcept
    {
        if (!arm_inter_round(left) || !arm_inter_round(right)) return false;

        const RollbackRoundPhase service_phase = left.observe_service();
        if (service_phase != RollbackRoundPhase::StockInterRound
            || left.phase() != RollbackRoundPhase::StockInterRound)
        {
            return false;
        }

        const uint8_t stock_statuses[] = {3, 5, 9, 2};
        uint64_t left_native = 1000 + next_ordinal * 100;
        uint64_t right_native = left_native + 37;
        bool left_frozen = false;
        bool right_frozen = false;
        for (unsigned tick = 0; tick <= 3; ++tick)
        {
            const uint8_t stock_status = stock_statuses[tick];
            if (!left_frozen)
            {
                const bool candidate_now = tick >= left_delay;
                const auto candidate = make_candidate(
                    left_match,
                    candidate_now ? next_ordinal : next_ordinal - 1,
                    candidate_now ? 2 : stock_status,
                    left_native + tick);
                const auto action = left.observe_per_frame(candidate);
                left_frozen = candidate_now;
                if (action != (candidate_now
                        ? RollbackPerFrameAction::FreezeBeforeTrampoline
                        : RollbackPerFrameAction::CallStockTrampoline))
                {
                    return false;
                }
            }
            else
            {
                const auto action = left.observe_per_frame(make_candidate(
                    left_match, next_ordinal, 2, left_native + tick));
                if (action != RollbackPerFrameAction::SkipTrampoline)
                    return false;
            }

            if (!right_frozen)
            {
                const bool candidate_now = tick >= right_delay;
                const auto candidate = make_candidate(
                    right_match,
                    candidate_now ? next_ordinal : next_ordinal - 1,
                    candidate_now ? 2 : stock_status,
                    right_native + tick);
                const auto action = right.observe_per_frame(candidate);
                right_frozen = candidate_now;
                if (action != (candidate_now
                        ? RollbackPerFrameAction::FreezeBeforeTrampoline
                        : RollbackPerFrameAction::CallStockTrampoline))
                {
                    return false;
                }
            }
            else
            {
                const auto action = right.observe_per_frame(make_candidate(
                    right_match, next_ordinal, 2, right_native + tick));
                if (action != RollbackPerFrameAction::SkipTrampoline)
                    return false;
            }
        }

        if (!left_frozen || !right_frozen
            || left.phase() != RollbackRoundPhase::BaselineFrozen
            || right.phase() != RollbackRoundPhase::BaselineFrozen)
        {
            return false;
        }
        if (!publish_pair(left, right, replay_round)) return false;
        if (left.phase() != RollbackRoundPhase::StartingGekko
            || right.phase() != RollbackRoundPhase::StartingGekko
            || left.round_epoch() == 0
            || left.round_epoch() != right.round_epoch())
        {
            return false;
        }
        return left.complete_gekko_restart(true)
            && right.complete_gekko_restart(true)
            && left.phase() == RollbackRoundPhase::Active
            && right.phase() == RollbackRoundPhase::Active;
    }

    bool rearm_coordinate_gate_selftest() noexcept
    {
        constexpr uint64_t generation = 2;
        constexpr uint32_t timer = 4;

        RollbackRearmCoordinateGate valid {};
        bool ok = valid.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && valid.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && valid.commit_zero_delta(generation, 1, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && valid.admit_step(generation, 1, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && valid.commit_step(generation, 1, 2, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && valid.admit_step(generation, 2, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && valid.commit_step(generation, 2, 3, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && valid.admit_step(generation, 3, timer, true)
                == RollbackRearmCoordinateResult::InvalidCoordinate
            && valid.seal(generation, 3, timer)
                == RollbackRearmCoordinateResult::Accepted
            && valid.sealed()
            && valid.admit_step(generation, 3, timer, false)
                == RollbackRearmCoordinateResult::Sealed;

        RollbackRearmCoordinateGate duplicate {};
        ok = ok
            && duplicate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && duplicate.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && duplicate.commit_zero_delta(generation, 1, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && duplicate.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Duplicate
            && duplicate.admit_step(generation, 1, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && duplicate.commit_step(generation, 1, 2, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && duplicate.admit_step(generation, 1, timer, false)
                == RollbackRearmCoordinateResult::Duplicate;

        RollbackRearmCoordinateGate missing {};
        ok = ok
            && missing.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && missing.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && missing.commit_zero_delta(generation, 1, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && missing.admit_step(generation, 2, timer, false)
                == RollbackRearmCoordinateResult::SkippedOrReordered;

        RollbackRearmCoordinateGate reordered {};
        ok = ok
            && reordered.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && reordered.admit_step(generation, 1, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && reordered.commit_step(generation, 1, 2, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && reordered.admit_zero_delta(generation, 2, timer)
                == RollbackRearmCoordinateResult::Duplicate
            && reordered.admit_step(generation, 3, timer, true)
                == RollbackRearmCoordinateResult::SkippedOrReordered;

        RollbackRearmCoordinateGate no_zero_delta {};
        ok = ok
            && no_zero_delta.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && no_zero_delta.admit_step(generation, 1, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && no_zero_delta.commit_step(generation, 1, 2, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && no_zero_delta.admit_step(generation, 2, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && no_zero_delta.commit_step(generation, 2, 3, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && no_zero_delta.seal(generation, 3, timer)
                == RollbackRearmCoordinateResult::Accepted;

        RollbackRearmCoordinateGate callback_duplicate {};
        ok = ok
            && callback_duplicate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && callback_duplicate.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && callback_duplicate.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Duplicate;

        RollbackRearmCoordinateGate drift {};
        ok = ok
            && drift.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && drift.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && drift.commit_zero_delta(generation, 1, 2, timer)
                == RollbackRearmCoordinateResult::InvalidCoordinate;

        RollbackRearmCoordinateGate prebaseline {};
        ok = ok
            && prebaseline.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && prebaseline.admit_zero_delta(generation, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && prebaseline.commit_zero_delta(generation, 1, 1, timer)
                == RollbackRearmCoordinateResult::Accepted
            && prebaseline.admit_step(generation, 1, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && prebaseline.commit_step(generation, 1, 2, timer, false)
                == RollbackRearmCoordinateResult::Accepted
            && prebaseline.seal(generation, 2, timer)
                == RollbackRearmCoordinateResult::NotComplete
            && prebaseline.admit_step(generation + 1, 2, timer, false)
                == RollbackRearmCoordinateResult::WrongGeneration;
        return ok;
    }

    bool inter_round_wind_coordinate_gate_selftest() noexcept
    {
        constexpr uint64_t generation = 2;
        constexpr uintptr_t round_result = 0x1000;
        constexpr uintptr_t pre_new_round = 0x2000;
        constexpr uintptr_t new_round = 0x3000;
        RollbackInterRoundWindCoordinateGate inactive {};
        RollbackInterRoundWindCoordinateGate gate {};
        RollbackInterRoundWindCoordinateGate transition_gate {};
        RollbackInterRoundWindCoordinateGate cinematic_replay_gate {};
        RollbackInterRoundWindCoordinateGate unproven_rewind_gate {};
        RollbackInterRoundWindCoordinateGate forged_post_gate {};
        RollbackInterRoundWindCoordinateGate late_round_result_gate {};
        RollbackInterRoundWindCoordinateGate zero_delta_new_round_gate {};
        const bool zero_delta_new_round_sequence =
            zero_delta_new_round_gate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && zero_delta_new_round_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 4, 240)
                == RollbackRearmCoordinateResult::Accepted
            && zero_delta_new_round_gate.observe_other(generation)
                == RollbackRearmCoordinateResult::Accepted
            && zero_delta_new_round_gate.admit(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 1, 120)
                == RollbackRearmCoordinateResult::Accepted
            && zero_delta_new_round_gate.observe_native_post_iteration(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 1, 1, 120, true)
                == RollbackRearmCoordinateResult::Accepted
            && zero_delta_new_round_gate.observe_native_post_iteration(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 1, 1, 120, true)
                == RollbackRearmCoordinateResult::SkippedOrReordered
            && zero_delta_new_round_gate.seal(generation)
                == RollbackRearmCoordinateResult::Accepted;
        const bool late_round_result_sequence =
            late_round_result_gate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && late_round_result_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 4, 240)
                == RollbackRearmCoordinateResult::Accepted
            && late_round_result_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 5, 240)
                == RollbackRearmCoordinateResult::Accepted
            && late_round_result_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 7, 240)
                == RollbackRearmCoordinateResult::SkippedOrReordered;
        bool transition_sequence =
            transition_gate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted;
        constexpr uint32_t transition_coordinate = 240;
        for (uint32_t coordinate = 1;
             coordinate <= transition_coordinate; ++coordinate)
        {
            transition_sequence = transition_sequence
                && RollbackRoundResultOwnsWindCoordinate(
                    coordinate, transition_coordinate)
                && transition_gate.admit(
                        generation, RollbackInterRoundWindMode::RoundResult,
                        round_result, coordinate, 1)
                    == RollbackRearmCoordinateResult::Accepted;
        }
        transition_sequence = transition_sequence
            && transition_gate.round_result_coordinates()
                == transition_coordinate
            && !RollbackRoundResultOwnsWindCoordinate(
                transition_coordinate + 1u, transition_coordinate)
            && transition_gate.observe_other(generation)
                == RollbackRearmCoordinateResult::Accepted
            && transition_gate.admit(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 1, 120)
                == RollbackRearmCoordinateResult::Accepted
            && transition_gate.seal(generation)
                == RollbackRearmCoordinateResult::Accepted;

        bool cinematic_replay_sequence =
            cinematic_replay_gate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted;
        constexpr uint32_t cinematic_phase_timer = 240;
        for (uint32_t coordinate = 1; coordinate <= 30; ++coordinate)
        {
            cinematic_replay_sequence = cinematic_replay_sequence
                && cinematic_replay_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, coordinate, cinematic_phase_timer)
                    == RollbackRearmCoordinateResult::Accepted
                && cinematic_replay_gate.observe_native_post_iteration(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, coordinate,
                    coordinate == 30 ? 2 : coordinate + 1u,
                    cinematic_phase_timer)
                    == RollbackRearmCoordinateResult::Accepted;
        }
        for (uint32_t coordinate = 2; coordinate <= 45; ++coordinate)
        {
            cinematic_replay_sequence = cinematic_replay_sequence
                && cinematic_replay_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, coordinate, cinematic_phase_timer)
                    == RollbackRearmCoordinateResult::Accepted
                && cinematic_replay_gate.observe_native_post_iteration(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, coordinate,
                    coordinate == 45 ? 2 : coordinate + 1u,
                    cinematic_phase_timer)
                    == RollbackRearmCoordinateResult::Accepted;
        }
        for (uint32_t coordinate = 2;
             coordinate <= transition_coordinate; ++coordinate)
        {
            cinematic_replay_sequence = cinematic_replay_sequence
                && cinematic_replay_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, coordinate, cinematic_phase_timer)
                    == RollbackRearmCoordinateResult::Accepted
                && cinematic_replay_gate.observe_native_post_iteration(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, coordinate, coordinate + 1u,
                    cinematic_phase_timer)
                    == RollbackRearmCoordinateResult::Accepted;
        }
        cinematic_replay_sequence = cinematic_replay_sequence
            && cinematic_replay_gate.round_result_replay_restarts() == 2
            && !cinematic_replay_gate.replay_restart_pending()
            && cinematic_replay_gate.observe_native_post_iteration(
                generation, RollbackInterRoundWindMode::RoundResult,
                round_result, transition_coordinate, 2,
                cinematic_phase_timer)
                == RollbackRearmCoordinateResult::SkippedOrReordered
            && cinematic_replay_gate.round_result_coordinates()
                == transition_coordinate
            && cinematic_replay_gate.admitted() == 313
            && cinematic_replay_gate.observe_other(generation)
                == RollbackRearmCoordinateResult::Accepted
            && cinematic_replay_gate.admit(
                generation, RollbackInterRoundWindMode::NewRound,
                new_round, 1, 120)
                == RollbackRearmCoordinateResult::Accepted
            && cinematic_replay_gate.seal(generation)
                == RollbackRearmCoordinateResult::Accepted;

        bool unproven_rewind_sequence =
            unproven_rewind_gate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted;
        for (uint32_t coordinate = 1; coordinate <= 30; ++coordinate)
        {
            unproven_rewind_sequence = unproven_rewind_sequence
                && unproven_rewind_gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, coordinate, cinematic_phase_timer)
                    == RollbackRearmCoordinateResult::Accepted;
        }
        unproven_rewind_sequence = unproven_rewind_sequence
            && unproven_rewind_gate.admit(
                generation, RollbackInterRoundWindMode::RoundResult,
                round_result, 2, cinematic_phase_timer)
                == RollbackRearmCoordinateResult::SkippedOrReordered;

        const bool forged_post_sequence =
            forged_post_gate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && forged_post_gate.admit(
                generation, RollbackInterRoundWindMode::RoundResult,
                round_result, 1, cinematic_phase_timer)
                == RollbackRearmCoordinateResult::Accepted
            && forged_post_gate.observe_native_post_iteration(
                generation, RollbackInterRoundWindMode::RoundResult,
                round_result, 30, 2, cinematic_phase_timer)
                == RollbackRearmCoordinateResult::InvalidCoordinate
            && forged_post_gate.observe_native_post_iteration(
                generation, RollbackInterRoundWindMode::NewRound,
                round_result, 1, 2, cinematic_phase_timer)
                == RollbackRearmCoordinateResult::Accepted
            && forged_post_gate.observe_native_post_iteration(
                generation, RollbackInterRoundWindMode::NewRound,
                round_result, 1, 3, cinematic_phase_timer)
                == RollbackRearmCoordinateResult::SkippedOrReordered;

        return transition_sequence
            && cinematic_replay_sequence
            && unproven_rewind_sequence
            && forged_post_sequence
            && zero_delta_new_round_sequence
            && late_round_result_sequence
            && RollbackRoundResultNativePostRewindAllowed(30, 2, 0)
            && RollbackRoundResultNativePostRewindAllowed(45, 2, 1)
            && !RollbackRoundResultNativePostRewindAllowed(45, 2, 2)
            && !RollbackRoundResultNativePostRewindAllowed(2, 2, 0)
            && !RollbackRoundResultNativePostRewindAllowed(30, 3, 0)
            && RollbackRoundResultOwnsWindCoordinate(
                1, transition_coordinate)
            && RollbackRoundResultOwnsWindCoordinate(
                transition_coordinate, transition_coordinate)
            && !RollbackRoundResultOwnsWindCoordinate(
                transition_coordinate + 1u, transition_coordinate)
            && !RollbackRoundResultOwnsWindCoordinate(
                0, transition_coordinate)
            && !RollbackRoundResultOwnsWindCoordinate(240, 239)
            && !RollbackRoundResultOwnsWindCoordinate(239, 0)
            && inactive.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 1, 30)
                == RollbackRearmCoordinateResult::Inactive
            && gate.arm(generation)
                == RollbackRearmCoordinateResult::Accepted
            && gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 1, 30)
                == RollbackRearmCoordinateResult::Accepted
            && gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 1, 30)
                == RollbackRearmCoordinateResult::Duplicate
            && gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 2, 30)
                == RollbackRearmCoordinateResult::Accepted
            && gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 4, 30)
                == RollbackRearmCoordinateResult::SkippedOrReordered
            && gate.observe_other(generation)
                == RollbackRearmCoordinateResult::Accepted
            && gate.admit(
                    generation, RollbackInterRoundWindMode::RoundResult,
                    round_result, 1, 30)
                == RollbackRearmCoordinateResult::Duplicate
            && gate.admit(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 1, 120)
                == RollbackRearmCoordinateResult::Accepted
            && gate.admit(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 3, 120)
                == RollbackRearmCoordinateResult::SkippedOrReordered
            && gate.admit(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 2, 120)
                == RollbackRearmCoordinateResult::Accepted
            && gate.admitted() == 4
            && gate.round_result_coordinates() == 2
            && gate.new_round_coordinates() == 2
            && gate.seal(generation)
                == RollbackRearmCoordinateResult::Accepted
            && gate.sealed()
            && gate.admit(
                    generation, RollbackInterRoundWindMode::NewRound,
                    new_round, 2, 120)
                == RollbackRearmCoordinateResult::Sealed
            && gate.seal(generation)
                == RollbackRearmCoordinateResult::Sealed
            && RollbackPreNewRoundAwaitsFinalRoundResultCapture(
                true, false, true, true, 240, 240)
            && !RollbackPreNewRoundAwaitsFinalRoundResultCapture(
                true, true, true, true, 240, 240)
            && !RollbackPreNewRoundAwaitsFinalRoundResultCapture(
                false, false, true, true, 240, 240)
            && !RollbackPreNewRoundAwaitsFinalRoundResultCapture(
                true, false, false, true, 240, 240)
            && !RollbackPreNewRoundAwaitsFinalRoundResultCapture(
                true, false, true, false, 240, 240)
            && !RollbackPreNewRoundAwaitsFinalRoundResultCapture(
                true, false, true, true, 239, 240)
            && RollbackFinalRoundResultPostCallAllowed(
                true, true, 240, 240, true, true, true)
            && !RollbackFinalRoundResultPostCallAllowed(
                true, true, 239, 240, true, true, true)
            && !RollbackFinalRoundResultPostCallAllowed(
                true, true, 240, 240, false, true, true)
            && !RollbackFinalRoundResultPostCallAllowed(
                true, true, 240, 240, true, false, true)
            && !RollbackFinalRoundResultPostCallAllowed(
                true, true, 240, 240, true, true, false)
            && RollbackFinalRoundResultZeroPerFrameAllowed(
                true, true, 1, 0, true, true)
            && !RollbackFinalRoundResultZeroPerFrameAllowed(
                true, false, 1, 0, true, true)
            && !RollbackFinalRoundResultZeroPerFrameAllowed(
                true, true, 1, 1, true, true)
            && !RollbackFinalRoundResultZeroPerFrameAllowed(
                true, true, 1, 0, true, false);
    }

    bool inter_round_carried_state_gate_selftest() noexcept
    {
        constexpr uint64_t generation = 2;
        constexpr uint32_t terminal_coordinate = 240;
        RollbackInterRoundCarriedStateGate inactive {};
        RollbackInterRoundCarriedStateGate gate {};
        RollbackInterRoundCarriedStateGate missing_capture {};

        return inactive.begin_restore(generation)
                == RollbackRearmCoordinateResult::Inactive
            && gate.arm(generation, terminal_coordinate)
                == RollbackRearmCoordinateResult::Accepted
            && gate.capture(generation + 1, terminal_coordinate)
                == RollbackRearmCoordinateResult::WrongGeneration
            && gate.capture(generation, terminal_coordinate - 1)
                == RollbackRearmCoordinateResult::InvalidCoordinate
            && gate.begin_restore(generation)
                == RollbackRearmCoordinateResult::NotComplete
            && gate.capture(generation, terminal_coordinate)
                == RollbackRearmCoordinateResult::Accepted
            && gate.captured()
            && gate.capture(generation, terminal_coordinate)
                == RollbackRearmCoordinateResult::Duplicate
            && gate.commit_restore(generation)
                == RollbackRearmCoordinateResult::NotComplete
            && gate.begin_restore(generation)
                == RollbackRearmCoordinateResult::Accepted
            && gate.restore_pending()
            && gate.begin_restore(generation)
                == RollbackRearmCoordinateResult::Duplicate
            && gate.commit_restore(generation + 1)
                == RollbackRearmCoordinateResult::WrongGeneration
            && gate.commit_restore(generation)
                == RollbackRearmCoordinateResult::Accepted
            && gate.restore_committed()
            && gate.begin_restore(generation)
                == RollbackRearmCoordinateResult::Sealed
            && gate.capture(generation, terminal_coordinate)
                == RollbackRearmCoordinateResult::Sealed
            && missing_capture.arm(generation, terminal_coordinate)
                == RollbackRearmCoordinateResult::Accepted
            && missing_capture.begin_restore(generation)
                == RollbackRearmCoordinateResult::NotComplete;
    }
}

int main()
{
    using namespace Horse;

    RollbackPreControlIterationLifetime precontrol_lifetime {};
    const bool rearm_gate_ok = rearm_coordinate_gate_selftest();
    const bool inter_round_wind_gate_ok =
        inter_round_wind_coordinate_gate_selftest();
    const bool inter_round_carried_state_gate_ok =
        inter_round_carried_state_gate_selftest();
    bool precontrol_lifetime_ok =
        rearm_gate_ok
        && inter_round_wind_gate_ok
        && inter_round_carried_state_gate_ok
        && !precontrol_lifetime.frozen()
        && precontrol_lifetime.shutdown_allowed()
        && ClassifyRollbackPreControlShutdown(false, precontrol_lifetime)
            == RollbackPreControlShutdownDecision::Allow
        && !precontrol_lifetime.complete_frame_zero();
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && precontrol_lifetime.freeze();
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && precontrol_lifetime.frozen()
        && !precontrol_lifetime.shutdown_allowed()
        && ClassifyRollbackPreControlShutdown(false, precontrol_lifetime)
            == RollbackPreControlShutdownDecision::DenyHeldIteration
        && ClassifyRollbackPreControlShutdown(true, precontrol_lifetime)
            == RollbackPreControlShutdownDecision::DeferForCallback;
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && precontrol_lifetime.capture_baseline()
        && precontrol_lifetime.phase()
            == RollbackPreControlIterationPhase::BaselineCaptured
        && !precontrol_lifetime.shutdown_allowed()
        && precontrol_lifetime.begin_frame_zero()
        && precontrol_lifetime.phase()
            == RollbackPreControlIterationPhase::FrameZeroExecuting
        && !precontrol_lifetime.shutdown_allowed();
    // A failed native attempt does not call complete_frame_zero; the state
    // remains denied until a later successful logical frame 0.
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && precontrol_lifetime.phase()
            == RollbackPreControlIterationPhase::FrameZeroExecuting
        && !precontrol_lifetime.shutdown_allowed();
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && precontrol_lifetime.complete_frame_zero()
        && !precontrol_lifetime.frozen()
        && precontrol_lifetime.shutdown_allowed();
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && precontrol_lifetime.freeze();
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && precontrol_lifetime.authorize_out_of_battle_shutdown()
        && precontrol_lifetime.shutdown_authorized()
        && precontrol_lifetime.shutdown_allowed()
        && !precontrol_lifetime.complete_frame_zero();
    precontrol_lifetime.reset();
    precontrol_lifetime_ok = precontrol_lifetime_ok
        && !precontrol_lifetime.frozen()
        && !precontrol_lifetime.shutdown_authorized();

    RollbackPreControlCandidateObservation precontrol {};
    precontrol.round_state = 0x4100e20;
    precontrol.expected_new_round_state = precontrol.round_state;
    precontrol.current_mode = precontrol.round_state;
    precontrol.current_mode_read = true;
    precontrol.queued_mode_read = true;
    precontrol.mode_frame_read = true;
    precontrol.phase_timer_read = true;
    precontrol.mode_frame = 0;
    precontrol.phase_timer = 0;
    bool precontrol_classifier_ok = precontrol_lifetime_ok &&
        ClassifyRollbackPreControlCandidate(precontrol)
            == RollbackPreControlCandidateDisposition::Candidate;
    precontrol.phase_timer = 120;
    precontrol.mode_frame = 118;
    precontrol_classifier_ok = precontrol_classifier_ok
        && ClassifyRollbackPreControlCandidate(precontrol)
            == RollbackPreControlCandidateDisposition::NotDue
        && RollbackPreControlPassesNewRoundCountdown(
            precontrol, true, 1, false, false)
        && !RollbackPreControlPassesNewRoundCountdown(
            precontrol, true, 0, false, false)
        && !RollbackPreControlPassesNewRoundCountdown(
            precontrol, true, 1, true, false)
        && !RollbackPreControlPassesNewRoundCountdown(
            precontrol, true, 1, false, true)
        && RollbackPreControlPassesNewRoundCountdown(
            precontrol, false, 0, true, true);
    precontrol.mode_frame = 119;
    precontrol_classifier_ok = precontrol_classifier_ok
        && ClassifyRollbackPreControlCandidate(precontrol)
            == RollbackPreControlCandidateDisposition::Candidate
        && !RollbackPreControlPassesNewRoundCountdown(
            precontrol, true, 1, false, false)
        && ClassifyRollbackNewRoundStockInterRoundAction(
            precontrol, 2, 3, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::FreezeCandidate
        && ClassifyRollbackNewRoundStockInterRoundAction(
            precontrol, 2, 1, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::FreezeCandidate
        && ClassifyRollbackNewRoundStockInterRoundAction(
            precontrol, 2, 1, 1, false, true)
                == RollbackNewRoundStockInterRoundAction::Reject;
    const bool classifier_due_boundary_ok = precontrol_classifier_ok;

    RollbackPreControlCandidateObservation precontrol_after = precontrol;
    precontrol.mode_frame = 118;
    precontrol_after.mode_frame = 119;
    precontrol_classifier_ok = precontrol_classifier_ok
        && RollbackNewRoundArmedAfterCallValid(
            precontrol, precontrol_after, false, 0, 1, 2, 3, false);
    const bool classifier_pre_due_step_ok = precontrol_classifier_ok;

    // NewRound OnEnter executes inside the preceding controlled PerFrame call.
    // BattleManager status 1 and 3 are both observed during the independent
    // NewRound countdown; both variants use the same one-delta clock arm.
    auto first_countdown = precontrol;
    first_countdown.mode_frame = 1;
    auto first_countdown_after = first_countdown;
    first_countdown_after.mode_frame = 2;
    const bool first_countdown_armed_ok =
        ClassifyRollbackNewRoundStockInterRoundAction(
            first_countdown, 2, 3, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::PassCountdown
        && ClassifyRollbackNewRoundStockInterRoundAction(
            first_countdown, 2, 1, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::PassCountdown
        && RollbackNewRoundArmedAfterCallValid(
            first_countdown, first_countdown_after, false,
            0, 1, 2, 3, false)
        && !RollbackNewRoundArmedAfterCallValid(
            first_countdown, first_countdown_after, false,
            1, 1, 2, 3, false)
        && !RollbackNewRoundArmedAfterCallValid(
            first_countdown, first_countdown_after, false,
            0, 2, 2, 3, false);
    auto epoch_sync_after = first_countdown;
    const bool zero_delta_epoch_sync_ok =
        RollbackNewRoundZeroDeltaEpochSyncAfterCallValid(
            first_countdown, epoch_sync_after,
            0, 0, 2, 3, false)
        && RollbackNewRoundZeroDeltaEpochSyncAfterCallValid(
            first_countdown, epoch_sync_after,
            0, 0, 2, 1, false)
        && !RollbackNewRoundZeroDeltaEpochSyncAfterCallValid(
            first_countdown, epoch_sync_after,
            1, 0, 2, 3, false)
        && !RollbackNewRoundZeroDeltaEpochSyncAfterCallValid(
            first_countdown, epoch_sync_after,
            0, 1, 2, 3, false);
    ++epoch_sync_after.mode_frame;
    const bool zero_delta_epoch_sync_run_ahead_rejected =
        !RollbackNewRoundZeroDeltaEpochSyncAfterCallValid(
            first_countdown, epoch_sync_after,
            0, 0, 2, 3, false);
    first_countdown.mode_frame = 2;
    first_countdown_after.mode_frame = 3;
    const bool later_status1_armed =
        ClassifyRollbackNewRoundStockInterRoundAction(
            first_countdown, 2, 1, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::PassCountdown
        && RollbackNewRoundArmedAfterCallValid(
            first_countdown, first_countdown_after, false,
            0, 1, 2, 1, false);
    auto dispatch = precontrol;
    dispatch.mode_frame = 118;
    auto dispatch_after = dispatch;
    dispatch_after.mode_frame = 119;
    const bool stock_inter_round_sequence_ok =
        ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 3, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::PassCountdown
        && ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 4, 3, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::Reject
        && ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 1, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::PassCountdown
        && ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 5, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::Reject
        && ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 9, 1, false, false)
                == RollbackNewRoundStockInterRoundAction::Reject
        && RollbackNewRoundArmedAfterCallValid(
            dispatch, dispatch_after, false, 0, 1, 2, 3, false);
    dispatch = dispatch_after;
    uint32_t outer_calls = 0;
    uint32_t per_frame_calls = 0;
    uint32_t finalize_calls = 0;
    uint32_t deferrals = 0;
    bool transition_deferred = false;
    const auto due_dispatch =
        ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 3, 1, false, transition_deferred);
    const auto due_dispatch_status1 =
        ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 1, 1, false, transition_deferred);
    precontrol_classifier_ok = precontrol_classifier_ok
        && first_countdown_armed_ok
        && zero_delta_epoch_sync_ok
        && zero_delta_epoch_sync_run_ahead_rejected
        && later_status1_armed
        && stock_inter_round_sequence_ok
        && due_dispatch
            == RollbackNewRoundStockInterRoundAction::FreezeCandidate
        && due_dispatch_status1
            == RollbackNewRoundStockInterRoundAction::FreezeCandidate
        && outer_calls == 0
        && per_frame_calls == 0
        && finalize_calls == 0
        && deferrals == 0
        && !transition_deferred
        && ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 1, 1, false, true)
                == RollbackNewRoundStockInterRoundAction::Reject
        && ClassifyRollbackNewRoundStockInterRoundAction(
            dispatch, 2, 3, 1, false, true)
                == RollbackNewRoundStockInterRoundAction::Reject
        && ClassifyRollbackNativeNewRoundFinalizeAction(
            true, false, true, 2, true, false,
            true, true, true, true,
            dispatch.mode_frame, dispatch.phase_timer)
                == RollbackNativeNewRoundFinalizeAction::Release;
    precontrol_after.mode_frame = 120;
    precontrol_classifier_ok = precontrol_classifier_ok
        && !RollbackNewRoundArmedAfterCallValid(
            precontrol, precontrol_after, false, 0, 1, 2, 3, false);
    precontrol.mode_frame = 119;
    precontrol_after.mode_frame = 121;
    precontrol_classifier_ok = precontrol_classifier_ok
        && !RollbackNewRoundArmedAfterCallValid(
            precontrol, precontrol_after, true, 0, 1, 2, 1, true);
    precontrol.mode_frame = 120;
    precontrol_classifier_ok = precontrol_classifier_ok
        && ClassifyRollbackPreControlCandidate(precontrol)
            == RollbackPreControlCandidateDisposition::Candidate;
    precontrol.expected_new_round_state += 0x28;
    precontrol_classifier_ok = precontrol_classifier_ok
        && ClassifyRollbackPreControlCandidate(precontrol)
            == RollbackPreControlCandidateDisposition::WrongMode;
    precontrol.expected_new_round_state = precontrol.round_state;
    precontrol.queued_mode = 0x4100b38;
    precontrol_classifier_ok = precontrol_classifier_ok
        && ClassifyRollbackPreControlCandidate(precontrol)
            == RollbackPreControlCandidateDisposition::QueuedModePresent
        && !RollbackPreControlPassesNewRoundCountdown(
            precontrol, true, 1, false, false);
    precontrol.queued_mode = 0;
    precontrol.phase_timer_read = false;
    precontrol_classifier_ok = precontrol_classifier_ok
        && ClassifyRollbackPreControlCandidate(precontrol)
            == RollbackPreControlCandidateDisposition::Unreadable;

    const auto left_match = make_match(0);
    const auto right_match = make_match(1);
    RollbackRoundCoordinator left {};
    RollbackRoundCoordinator right {};
    bool ok = precontrol_classifier_ok
        && left.initialize(left_match, 0, 1, 0x12345678)
        && right.initialize(right_match, 0, 1, 0x12345678);
    ok = ok
        && left.observe_per_frame(make_candidate(left_match, 0, 1, 900))
            == RollbackPerFrameAction::RunRollbackAdvance;

    // Exercise both early-peer directions and the maximum three-tick skew.
    ok = ok && exercise_asymmetric_round(
        left, right, left_match, right_match, 1, 1, 0, 3);
    ok = ok && exercise_asymmetric_round(
        left, right, left_match, right_match, 2, 2, 2, 0);
    ok = ok && left.match_identity().battle_manager
            == left_match.battle_manager
        && right.match_identity().battle_manager
            == right_match.battle_manager
        && left.counters().round_candidates_frozen == 2
        && right.counters().round_candidates_frozen == 2
        && left.counters().round_identities_captured == 2
        && right.counters().round_identities_captured == 2
        && left.counters().round_baselines_accepted == 2
        && right.counters().round_baselines_accepted == 2
        && left.counters().round_gekko_restarts == 2
        && right.counters().round_gekko_restarts == 2
        && left.counters().stock_round_transition_rearms == 2
        && right.counters().stock_round_transition_rearms == 2
        && left.counters().frame_zero_executions == 2
        && right.counters().frame_zero_executions == 2;

    // Every possible 0-3 tick arrival skew is accepted. Native diagnostic
    // frames intentionally differ between the two peers.
    for (unsigned left_delay = 0; left_delay <= 3 && ok; ++left_delay)
    {
        for (unsigned right_delay = 0; right_delay <= 3 && ok; ++right_delay)
        {
            RollbackRoundCoordinator skew_left {};
            RollbackRoundCoordinator skew_right {};
            ok = skew_left.initialize(left_match, 0, 1, 0x12345678)
                && skew_right.initialize(right_match, 0, 1, 0x12345678)
                && exercise_asymmetric_round(
                    skew_left, skew_right, left_match, right_match,
                    1, 1, left_delay, right_delay);
        }
    }

    // The known stock inter-round states each pass through once before the
    // first eligible pre-control entry freezes.
    RollbackRoundCoordinator stock_states {};
    ok = ok && stock_states.initialize(left_match, 0, 1, 0x12345678)
        && arm_inter_round(stock_states);
    for (uint8_t status : {uint8_t{3}, uint8_t{5}, uint8_t{9}, uint8_t{2}})
    {
        ok = ok
            && stock_states.observe_per_frame(
                make_candidate(left_match, 0, status, 1800 + status))
                == RollbackPerFrameAction::CallStockTrampoline;
    }
    ok = ok && stock_states.counters().stock_pass_through_calls == 4
        && stock_states.observe_per_frame(
            make_candidate(left_match, 1, 2, 1900))
            == RollbackPerFrameAction::FreezeBeforeTrampoline;

    // A changed persistent object freezes instead of calling stock.
    RollbackRoundCoordinator pointer_failure {};
    ok = ok && pointer_failure.initialize(left_match, 0, 1, 0x12345678)
        && arm_inter_round(pointer_failure);
    auto replaced_manager = make_candidate(left_match, 1, 2, 2000);
    replaced_manager.match.battle_manager = 0xdead;
    ok = ok
        && pointer_failure.observe_per_frame(replaced_manager)
            == RollbackPerFrameAction::SkipTrampoline
        && pointer_failure.phase() == RollbackRoundPhase::FatalFrozen
        && std::strcmp(
            pointer_failure.failure(), "stock-match-identity-changed") == 0;

    // Skipping the immediately next ordinal is also fail-closed.
    RollbackRoundCoordinator ordinal_failure {};
    ok = ok && ordinal_failure.initialize(left_match, 0, 1, 0x12345678)
        && arm_inter_round(ordinal_failure);
    ok = ok
        && ordinal_failure.observe_per_frame(
            make_candidate(left_match, 2, 2, 2100))
            == RollbackPerFrameAction::SkipTrampoline
        && ordinal_failure.phase() == RollbackRoundPhase::FatalFrozen
        && std::strcmp(
            ordinal_failure.failure(), "next-round-ordinal-mismatch") == 0;

    // A final match gives stock permanent ownership; no rearm is attempted.
    RollbackRoundCoordinator final_match {};
    ok = ok && final_match.initialize(left_match, 2, 3, 0x12345678)
        && RollbackStockMatchComplete(3, 3, 1, 3)
        && RollbackStockMatchComplete(0, 3, 3, 3)
        && !RollbackStockMatchComplete(2, 3, 2, 3)
        && !RollbackStockMatchComplete(3, 0, 3, 0)
        && final_match.begin_terminal_confirmation()
        && final_match.accept_terminal()
        && final_match.release_terminal_to_stock(true)
        && final_match.phase() == RollbackRoundPhase::MatchComplete
        && final_match.observe_per_frame(
            make_candidate(left_match, 3, 1, 2200))
            == RollbackPerFrameAction::CallStockTrampoline
        && final_match.counters().round_candidates_frozen == 0
        && final_match.counters().round_gekko_restarts == 0;

    // Frame-zero failure cannot increment acceptance/rearm evidence.
    RollbackRoundCoordinator frame_zero_failure {};
    RollbackRoundCoordinator frame_zero_peer {};
    ok = ok
        && frame_zero_failure.initialize(left_match, 0, 1, 0x12345678)
        && frame_zero_peer.initialize(right_match, 0, 1, 0x12345678)
        && arm_inter_round(frame_zero_failure)
        && arm_inter_round(frame_zero_peer)
        && frame_zero_failure.observe_per_frame(
            make_candidate(left_match, 1, 2, 2300))
            == RollbackPerFrameAction::FreezeBeforeTrampoline
        && frame_zero_peer.observe_per_frame(
            make_candidate(right_match, 1, 2, 2333))
            == RollbackPerFrameAction::FreezeBeforeTrampoline
        && publish_pair(frame_zero_failure, frame_zero_peer, 1)
        && !frame_zero_failure.complete_gekko_restart(false)
        && frame_zero_failure.phase() == RollbackRoundPhase::FatalFrozen
        && frame_zero_failure.counters().round_gekko_restarts == 0
        && frame_zero_failure.counters().stock_round_transition_rearms == 0;

    if (!ok)
    {
        std::printf(
            "rollback_round_coordinator_selftest: FAILED "
            "rearm_gate=%u wind_gate=%u lifetime=%u classifier=%u "
            "due_boundary=%u pre_due_step=%u first_countdown=%u "
            "zero_delta=%u later_status1=%u stock_sequence=%u "
            "left_phase=%s left_failure=%s right_phase=%s right_failure=%s\n",
            rearm_gate_ok ? 1u : 0u,
            inter_round_wind_gate_ok ? 1u : 0u,
            precontrol_lifetime_ok ? 1u : 0u,
            precontrol_classifier_ok ? 1u : 0u,
            classifier_due_boundary_ok ? 1u : 0u,
            classifier_pre_due_step_ok ? 1u : 0u,
            first_countdown_armed_ok ? 1u : 0u,
            zero_delta_epoch_sync_ok ? 1u : 0u,
            later_status1_armed ? 1u : 0u,
            stock_inter_round_sequence_ok ? 1u : 0u,
            RollbackRoundPhaseName(left.phase()), left.failure(),
            RollbackRoundPhaseName(right.phase()), right.failure());
        return 1;
    }

    std::printf(
        "rollback_round_coordinator_selftest: PASS rounds=%llu "
        "left_pass=%llu right_pass=%llu left_frozen_calls=%llu "
        "right_frozen_calls=%llu epoch=%llu\n",
        static_cast<unsigned long long>(
            left.counters().stock_round_transition_rearms),
        static_cast<unsigned long long>(
            left.counters().stock_pass_through_calls),
        static_cast<unsigned long long>(
            right.counters().stock_pass_through_calls),
        static_cast<unsigned long long>(left.counters().calls_while_frozen),
        static_cast<unsigned long long>(right.counters().calls_while_frozen),
        static_cast<unsigned long long>(left.round_epoch()));
    return 0;
}
