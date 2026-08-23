"""Native MoveVM lane-scalar and frame-clock operations.

This module models the deterministic subset recovered from
``LuxMoveVM_SetLaneScalarTween @ 0x1403001F0`` and
``LuxMoveVM_AdvanceLaneFrameStep @ 0x1402FFEB0``.  It also records the
authored held-Canyon Creation tail in slot 341 as a scenario fixture.  The
fixture is control-flow data, not a fitted timing table: global 0x25 permits
only the first crossed speed checkpoint, advances 1 -> 2 in that same script
invocation, and resets the lane scalar when phase 2 executes on the next
invocation.
"""

from __future__ import annotations

from dataclasses import dataclass

from lux_numeric import (
    add_f32,
    cvttss2si,
    div_f32,
    float32,
    mul_f32,
    sub_f32,
)
from lux_transition_author import MoveVMLaneSchedulerState
from stackvm_emulate import decode_lux_fp16_literal


@dataclass(frozen=True)
class HeldCanyonSpeedCheckpoint:
    coordinate_word: int
    required_b_samples: int
    success_scalar_word: int
    failure_scalar_word: int

    @property
    def coordinate(self) -> float:
        return float32(decode_lux_fp16_literal(self.coordinate_word))

    @property
    def success_scalar(self) -> float:
        return float32(
            decode_lux_fp16_literal(self.success_scalar_word) / 100.0
        )

    @property
    def failure_scalar(self) -> float:
        return float32(
            decode_lux_fp16_literal(self.failure_scalar_word) / 100.0
        )


HELD_CANYON_SPEED_CHECKPOINTS = (
    HeldCanyonSpeedCheckpoint(0x4600, 6, 0x5240, 0x5640),
    HeldCanyonSpeedCheckpoint(0x4700, 8, 0x4E40, 0x5640),
    HeldCanyonSpeedCheckpoint(0x4800, 12, 0x4900, 0x5640),
    HeldCanyonSpeedCheckpoint(0x4840, 17, 0x4900, 0x5240),
    HeldCanyonSpeedCheckpoint(0x4880, 22, 0x4900, 0x5640),
    HeldCanyonSpeedCheckpoint(0x48C0, 27, 0x4900, 0x5240),
)


@dataclass(frozen=True)
class HeldCanyonLaneTick:
    global_tick: int
    local_tick: int
    previous_coordinate: float
    script_coordinate: float
    speed_before_script: float
    crossed_checkpoint: float | None
    required_b_samples: int | None
    history_passed: bool | None
    speed_after_script: float
    speed_phase_before: int
    speed_phase_after: int
    transition_authored: bool
    coordinate_after_advance: float


@dataclass(frozen=True)
class HeldCanyonTimeline:
    entry_tick: int
    b_hold_start_tick: int
    source_slot: int
    target_slot: int
    target_start_coordinate: float
    transition_tick: int | None
    first_active_tick: int | None
    final_active_tick: int | None
    minimum_b_hold_start_tick: int | None
    ticks: tuple[HeldCanyonLaneTick, ...]

    def source_coordinate_at(self, global_tick: int) -> float:
        for tick in self.ticks:
            if tick.global_tick == global_tick:
                return tick.script_coordinate
        raise KeyError(global_tick)


def set_lane_scalar_tween(
    lane: MoveVMLaneSchedulerState,
    target_scalar: float,
    frames_to_reach: int,
) -> None:
    """Mirror ``LuxMoveVM_SetLaneScalarTween`` for one populated lane."""

    if lane.current_move_id == 0xFFFF:
        return
    target = float32(target_scalar)
    lane.playback_speed_target = target
    lane.playback_speed_exponential_rate = float32(0.0)
    if frames_to_reach < 1:
        lane.playback_speed_current = target
        lane.playback_speed_countdown = float32(0.0)
        # Native snaps clear +0x3C, not the stale linear delta at +0x38.
        return
    duration = float32(frames_to_reach)
    lane.playback_speed_countdown = duration
    lane.playback_speed_delta = div_f32(
        sub_f32(target, lane.playback_speed_current), duration
    )


def advance_lane_frame_step(
    lane: MoveVMLaneSchedulerState,
    *,
    animation_length: float,
    time_dilation: float = 1.0,
    pending_hit_freeze: bool = False,
) -> bool:
    """Advance the proven one-step native lane path and return end status."""

    zero = float32(0.0)
    one = float32(1.0)
    scale = float32(time_dilation)
    lane.animation_frame_previous = lane.animation_frame_current
    previous_integer = cvttss2si(lane.animation_frame_current)

    if lane.playback_speed_countdown > zero:
        remaining = sub_f32(lane.playback_speed_countdown, one)
        lane.playback_speed_current = add_f32(
            lane.playback_speed_current,
            mul_f32(scale, lane.playback_speed_delta),
        )
        lane.playback_speed_countdown = remaining
        if remaining <= zero:
            lane.playback_speed_current = lane.playback_speed_target
    elif lane.playback_speed_exponential_rate > zero:
        distance = sub_f32(
            lane.playback_speed_target, lane.playback_speed_current
        )
        lane.playback_speed_current = add_f32(
            lane.playback_speed_current,
            mul_f32(mul_f32(distance, lane.playback_speed_exponential_rate), scale),
        )

    increment = mul_f32(scale, lane.playback_speed_current)
    if pending_hit_freeze:
        increment = zero
    next_frame = add_f32(lane.animation_frame_current, increment)
    length = float32(animation_length)
    lane.total_tick_counter = (lane.total_tick_counter + 1) & 0xFFFFFFFF
    lane.animation_frame_delta_this_tick = (
        cvttss2si(next_frame) - previous_integer
    )
    if next_frame > length:
        lane.animation_frame_current = length
        lane.animation_end_status = 1
        return True
    lane.animation_frame_current = next_frame
    lane.animation_end_status = int(cvttss2si(next_frame) == previous_integer)
    return False


def crossed_quantized_coordinate(
    previous_coordinate: float,
    current_coordinate: float,
    target_coordinate: float,
) -> bool:
    """Mirror IF 0x13C9's 1/128-frame crossing comparison."""

    previous_q = cvttss2si(mul_f32(previous_coordinate, 128.0))
    current_q = cvttss2si(mul_f32(current_coordinate, 128.0))
    target_q = cvttss2si(mul_f32(target_coordinate, 128.0))
    return previous_q < target_q <= current_q


def held_primary_all_at_tick(
    current_tick: int,
    hold_start_tick: int,
    count: int,
) -> bool:
    """Evaluate IF 0x20 for a continuous hold without constructing a ring.

    Native checks the current snapshot, skips cursor-1, then checks ring ages
    2..count.  A continuous hold therefore passes exactly when the current
    tick and ``current_tick-count`` onward are held; age one is irrelevant.
    """

    return count > 1 and hold_start_tick <= current_tick - count


def simulate_held_canyon_timeline(
    *,
    entry_tick: int,
    b_hold_start_tick: int,
    source_slot: int = 341,
    target_slot: int = 342,
    source_animation_length: float = 62.0,
    target_start_coordinate: float = 10.0,
    active_coordinates: tuple[int, ...] = (13, 14, 15, 16),
    max_ticks: int = 96,
) -> HeldCanyonTimeline:
    """Execute slot 341's recovered speed phase and held transition tail."""

    lane = MoveVMLaneSchedulerState(
        lane_index=0,
        current_move_id=source_slot,
        animation_frame_current=0.0,
        animation_frame_previous=-1.0,
        animation_frame_delta_this_tick=1,
        playback_speed_current=1.0,
        playback_speed_target=1.0,
    )
    speed_phase = 0  # slot-341 GLOBAL[0x25]
    transition_tick: int | None = None
    trace: list[HeldCanyonLaneTick] = []

    for local_tick in range(max_ticks + 1):
        global_tick = entry_tick + local_tick
        previous = lane.animation_frame_previous
        coordinate = lane.animation_frame_current
        speed_before = lane.playback_speed_current
        phase_before = speed_phase
        crossed: HeldCanyonSpeedCheckpoint | None = None
        history_passed: bool | None = None

        if speed_phase == 0 and 6 <= cvttss2si(coordinate) <= 10:
            for checkpoint in HELD_CANYON_SPEED_CHECKPOINTS:
                if crossed_quantized_coordinate(
                    previous, coordinate, checkpoint.coordinate
                ):
                    crossed = checkpoint
                    history_passed = held_primary_all_at_tick(
                        global_tick,
                        b_hold_start_tick,
                        checkpoint.required_b_samples,
                    )
                    set_lane_scalar_tween(
                        lane,
                        checkpoint.success_scalar
                        if history_passed else checkpoint.failure_scalar,
                        0,
                    )
                    speed_phase = 1
                    break

        # The checkpoint's jump lands at the phase state machine. Phase 1 is
        # promoted to 2 in the same invocation; phase 2 resets speed on the
        # following invocation. This source-order detail is what prevents the
        # later checkpoint branches from executing after the first crossing.
        if speed_phase == 1:
            speed_phase = 2
        elif speed_phase == 2 and phase_before == 2:
            speed_phase = 3
            set_lane_scalar_tween(lane, 1.0, 0)

        transition_authored = False
        if transition_tick is None and cvttss2si(coordinate) == 10 and held_primary_all_at_tick(
            global_tick, b_hold_start_tick, 32
        ):
            transition_authored = True
            transition_tick = global_tick

        advance_lane_frame_step(
            lane,
            animation_length=source_animation_length,
        )
        trace.append(HeldCanyonLaneTick(
            global_tick=global_tick,
            local_tick=local_tick,
            previous_coordinate=previous,
            script_coordinate=coordinate,
            speed_before_script=speed_before,
            crossed_checkpoint=(crossed.coordinate if crossed else None),
            required_b_samples=(crossed.required_b_samples if crossed else None),
            history_passed=history_passed,
            speed_after_script=lane.playback_speed_current,
            speed_phase_before=phase_before,
            speed_phase_after=speed_phase,
            transition_authored=transition_authored,
            coordinate_after_advance=lane.animation_frame_current,
        ))
        if (
            transition_tick is not None
            and global_tick >= transition_tick
            + max(active_coordinates)
            - int(target_start_coordinate)
        ):
            break

    first_active = (
        transition_tick + min(active_coordinates) - int(target_start_coordinate)
        if transition_tick is not None else None
    )
    final_active = (
        transition_tick + max(active_coordinates) - int(target_start_coordinate)
        if transition_tick is not None else None
    )
    minimum_hold_start = (
        transition_tick - 32 if transition_tick is not None else None
    )
    return HeldCanyonTimeline(
        entry_tick=entry_tick,
        b_hold_start_tick=b_hold_start_tick,
        source_slot=source_slot,
        target_slot=target_slot,
        target_start_coordinate=float32(target_start_coordinate),
        transition_tick=transition_tick,
        first_active_tick=first_active,
        final_active_tick=final_active,
        minimum_b_hold_start_tick=minimum_hold_start,
        ticks=tuple(trace),
    )
