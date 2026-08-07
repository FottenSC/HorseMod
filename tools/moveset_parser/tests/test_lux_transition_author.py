from __future__ import annotations

import pytest

from lux_numeric import float32_bits
from lux_reference_engine import StaticResolutionError
from lux_transition_author import (
    MoveVMLaneSchedulerState,
    MoveVMTransitionAuthorState,
    author_lane0_transition_06,
    author_lane1_transition_07,
    evaluate_active_lane_timing,
    map_bank_slot_timing_index,
)


def _state(**kwargs) -> MoveVMTransitionAuthorState:
    lane0 = MoveVMLaneSchedulerState(
        lane_index=0,
        animation_frame_current=5.75,
        timing_frame_10=10.25,
        timing_frame_14=14.5,
        playback_speed_current=0.5,
        motion_playback_frame_20=20.25,
        motion_playback_frame_24=24.75,
    )
    lane1 = MoveVMLaneSchedulerState(
        lane_index=1,
        animation_frame_current=7.25,
        timing_frame_10=110.25,
        timing_frame_14=114.5,
        playback_speed_current=1.5,
        motion_playback_frame_20=120.25,
        motion_playback_frame_24=124.75,
    )
    return MoveVMTransitionAuthorState(
        lanes=(lane0, lane1), active_lane_index=0, **kwargs
    )


@pytest.mark.parametrize(
    ("selector", "expected"),
    [
        (-1, -1.0),
        (0x1234, 4660.0),
        (0x7401, 15.5),
        (0x7602, 12.25),
        (0x7803, 23.25),
        (0x7A04, 8.5),
        (0x7C05, 11.25),
        (0x7E06, 6.0),
        (0x7FFF, 0.0),
    ],
)
def test_timing_mapper_covers_every_native_bucket(
    selector: int, expected: float
) -> None:
    state = _state(chara_timing_scalar_1364=0.0)
    actual = map_bank_slot_timing_index(state, state.lane(0), selector)
    assert float32_bits(actual) == float32_bits(expected)


def test_timing_mapper_opposite_lane_window_uses_lane_identity() -> None:
    state = _state()
    # 0x6400 remaps to effective 0x7400. Supplied lane 0 therefore reads
    # paired lane 1's +0x14 value.
    assert map_bank_slot_timing_index(state, state.lane(0), 0x6400) == 114.5
    assert map_bank_slot_timing_index(state, state.lane(1), 0x6400) == 14.5


def test_timing_mapper_sign_extends_native_short_operand() -> None:
    state = _state()
    assert map_bank_slot_timing_index(state, state.lane(0), 0xFFFE) == -2.0


@pytest.mark.parametrize(
    ("delta", "point", "expected"),
    [
        (1, 8, 0),
        (1, 7, 1),
        (3, 5, 1),
        (3, 8, 0),
        (-3, 5, 1),
        (-3, 8, 0),
    ],
)
def test_active_lane_timing_samples_every_crossed_frame(
    delta: int, point: int, expected: int
) -> None:
    state = _state()
    state.active_lane.animation_frame_current = 7.0
    state.active_lane.animation_frame_delta_this_tick = delta
    assert evaluate_active_lane_timing(state, (point,)) == expected


def test_active_lane_timing_inclusive_and_unbounded_windows() -> None:
    state = _state()
    state.active_lane.animation_frame_current = 7.0
    state.active_lane.animation_frame_delta_this_tick = 3
    assert evaluate_active_lane_timing(state, (6, 6)) == 1
    assert evaluate_active_lane_timing(state, (0x7FFF, 4)) == 0
    assert evaluate_active_lane_timing(state, (0x7FFF, 5)) == 1
    assert evaluate_active_lane_timing(state, (7, 0x7FFF)) == 1
    assert evaluate_active_lane_timing(state, ()) == 0
    assert evaluate_active_lane_timing(state, (1, 2, 3)) == 0


def test_motion_playback_timing_bucket_fails_closed_without_identity() -> None:
    state = _state()
    state.lane(0).motion_playback_frame_20 = None
    with pytest.raises(StaticResolutionError, match="motion-playback state"):
        map_bank_slot_timing_index(state, state.lane(0), 0x7800)


def test_lane1_author_publishes_target_timing_and_queued_transition_arguments() -> None:
    state = _state()
    assert author_lane1_transition_07(
        state, (0x1234, 2, 9, 0xAAAA, 0xBBBB, 0xCCCC)
    ) == 0
    lane1 = state.lane(1)
    assert lane1.queued_target_move_id == 0x1234
    assert lane1.transition_source_lane_index == 0
    assert lane1.transition_start_frame == 2.0
    assert lane1.transition_threshold_frame == 9.0
    assert lane1.queued_transition_arguments == (0xAAAA, 0xBBBB, 0xCCCC)
    assert lane1.queued_transition_argument_count == 3
    assert state.transition_threshold_now_flag == 0


def test_elapsed_threshold_is_clamped_to_zero_and_sets_same_tick_flag() -> None:
    state = _state()
    author_lane1_transition_07(state, (0x55, 2, 5))
    assert state.lane(1).transition_threshold_frame == 0.0
    assert state.transition_threshold_now_flag == 1


def test_negative_deferred_schedule_publishes_only_active_lane_package() -> None:
    state = _state(
        deferred_schedule_flag=1,
        deferred_schedule_frame=-3.5,
        deferred_commit_flag=7,
    )
    lane1 = state.lane(1)
    original_target = lane1.queued_target_move_id
    author_lane1_transition_07(state, (0x2345, 4, 20, 0x9999))
    active = state.active_lane
    assert active.deferred_transition_target_move_id == 0x2345
    assert active.deferred_transition_destination_lane_index == 1
    assert active.deferred_transition_start_frame == 4.0
    assert active.deferred_transition_schedule_frame == -3.5
    assert active.deferred_transition_commit_flag == 7
    assert lane1.queued_target_move_id == original_target


@pytest.mark.parametrize("guarded_move", (0x7E, 0x82, 0x86))
def test_lane0_guarded_family_clears_exact_four_word_cluster(
    guarded_move: int,
) -> None:
    state = _state()
    lane0 = state.lane(0)
    lane0.current_move_id = guarded_move
    lane0.guarded_queue_words_c2_c8[:] = [0x58, 2, 3, 4]
    author_lane0_transition_06(state, (0x14,))
    assert lane0.guarded_queue_words_c2_c8 == [0xFFFF] * 4


@pytest.mark.parametrize("excluded_move", (0x7F, 0x83, 0x87, 0x8A))
def test_lane0_mask_does_not_admit_neighbouring_or_explicitly_excluded_moves(
    excluded_move: int,
) -> None:
    state = _state()
    lane0 = state.lane(0)
    lane0.current_move_id = excluded_move
    lane0.guarded_queue_words_c2_c8[:] = [0x58, 2, 3, 4]
    author_lane0_transition_06(state, (0x14,))
    assert lane0.guarded_queue_words_c2_c8 == [0x58, 2, 3, 4]


def test_zero_argument_native_defaults_and_guarded_dereference() -> None:
    state = _state()
    author_lane1_transition_07(state, ())
    assert state.lane(1).queued_target_move_id == 0
    assert state.transition_threshold_now_flag == 1

    lane0 = state.lane(0)
    lane0.current_move_id = 0x7E
    lane0.guarded_queue_words_c2_c8[0] = 0x58
    with pytest.raises(StaticResolutionError, match="dereferences argument word zero"):
        author_lane0_transition_06(state, ())
