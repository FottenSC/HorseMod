from __future__ import annotations

from lux_lane_playback import (
    advance_lane_frame_step,
    crossed_quantized_coordinate,
    held_primary_all_at_tick,
    set_lane_scalar_tween,
    simulate_held_canyon_timeline,
)
from lux_transition_author import MoveVMLaneSchedulerState


def test_scalar_snap_preserves_stale_linear_delta_like_native():
    lane = MoveVMLaneSchedulerState(
        current_move_id=341,
        playback_speed_current=1.0,
        playback_speed_target=1.0,
        playback_speed_delta=0.125,
        playback_speed_exponential_rate=0.25,
        playback_speed_countdown=4.0,
    )
    set_lane_scalar_tween(lane, 0.5, 0)
    assert lane.playback_speed_current == 0.5
    assert lane.playback_speed_target == 0.5
    assert lane.playback_speed_delta == 0.125
    assert lane.playback_speed_exponential_rate == 0.0
    assert lane.playback_speed_countdown == 0.0


def test_lane_advance_uses_snapped_scalar_and_strict_end_test():
    lane = MoveVMLaneSchedulerState(
        current_move_id=341,
        animation_frame_current=6.0,
        playback_speed_current=0.5,
        playback_speed_target=0.5,
    )
    assert advance_lane_frame_step(lane, animation_length=6.5) is False
    assert lane.animation_frame_previous == 6.0
    assert lane.animation_frame_current == 6.5
    assert lane.animation_frame_delta_this_tick == 0
    assert advance_lane_frame_step(lane, animation_length=6.5) is True
    assert lane.animation_frame_current == 6.5


def test_if_13c9_uses_strict_previous_and_inclusive_current_quantization():
    assert crossed_quantized_coordinate(5.999, 6.0, 6.0) is True
    assert crossed_quantized_coordinate(6.0, 6.5, 6.0) is False
    assert crossed_quantized_coordinate(8.49, 8.5, 8.5) is True


def test_if20_continuous_history_skips_age_one():
    assert held_primary_all_at_tick(50, 18, 32) is True
    assert held_primary_all_at_tick(50, 19, 32) is False


def test_held_canyon_phase_uses_only_first_checkpoint_and_commits_same_tick():
    timeline = simulate_held_canyon_timeline(
        entry_tick=40,
        b_hold_start_tick=0,
    )
    crossings = [tick for tick in timeline.ticks if tick.crossed_checkpoint is not None]
    assert [(tick.local_tick, tick.crossed_checkpoint) for tick in crossings] == [(6, 6.0)]
    assert crossings[0].history_passed is True
    assert crossings[0].speed_phase_after == 2
    assert timeline.ticks[7].speed_phase_before == 2
    assert timeline.ticks[7].speed_after_script == 1.0
    assert timeline.transition_tick == 51
    assert timeline.first_active_tick == 54
    assert timeline.final_active_tick == 57
    assert timeline.minimum_b_hold_start_tick == 19


def test_held_canyon_transition_fails_without_32_sample_history():
    timeline = simulate_held_canyon_timeline(
        entry_tick=40,
        b_hold_start_tick=20,
    )
    assert timeline.transition_tick is None
    assert timeline.first_active_tick is None
