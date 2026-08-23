"""Native-equivalent MoveVM transition authoring and timing selection.

The implementation covers the shared decoder at ``0x1402FC930``, timing
mapper at ``0x1403002B0``, and lane state touched by reachable CALLCONDs 0x06
and 0x07.  Every scalar operation preserves the native binary32 boundary.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_numeric import add_f32, cvttss2si, float32, signed_low_i16, sub_f32
from lux_reference_engine import StaticResolutionError, u16
from lux_scheduled_effects import LuxScheduledEffectTable


@dataclass
class MoveVMLaneSchedulerState:
    """Typed transition/timing subset of the native 0x468-byte lane."""

    lane_index: int = 0
    current_move_id: int = 0
    animation_frame_current: float = 0.0
    animation_frame_delta_this_tick: int = 1
    animation_frame_previous: float = 0.0
    animation_end_status: int = 0
    timing_frame_10: float = 0.0
    timing_frame_14: float = 0.0
    playback_speed_current: float = 0.0
    playback_speed_target: float = 0.0
    playback_speed_delta: float = 0.0
    playback_speed_exponential_rate: float = 0.0
    playback_speed_countdown: float = 0.0
    motion_playback_frame_20: float | None = None
    motion_playback_frame_24: float | None = None

    transition_source_lane_index: int = 0
    queued_target_move_id: int = 0xFFFF
    override_target_on_flag_16fe: int = 0xFFFF
    override_target_on_flag_16eb: int = 0xFFFF
    override_target_on_opponent_move_class: int = 0xFFFF
    transition_start_frame: float = 0.0
    transition_threshold_frame: float = 0.0
    # +0x90/+0x94: arguments staged with a queued transition.  These are
    # copied into the independent +0x6C/+0x6E entry block by
    # LuxMoveVM_TransitionToMove.
    queued_transition_arguments: tuple[int, ...] = ()
    queued_transition_argument_count: int = 0
    # +0x6C/+0x6E: arguments consumed by the synchronous entry-bytecode call.
    entry_argument_count: int = 0
    entry_arguments: list[int] = field(default_factory=lambda: [0] * 17)
    primary_script_running: int = 0
    secondary_script_running: int = 0
    transition_fired_marker: int = 0
    cancel_sentinel_c4: int = 0xFFFF
    cancel_word_c8: int = 0
    cancel_sentinel_d4: int = 0xFFFF
    script_rerun_sentinel: int = 0

    deferred_transition_target_move_id: int = 0xFFFF
    deferred_transition_destination_lane_index: int = 0
    deferred_transition_commit_flag: int = 0
    deferred_transition_start_frame: float = 0.0
    deferred_transition_schedule_frame: float = 0.0
    total_tick_counter: int = 0
    sub_frame_trigger_accumulator: float = 0.0
    variant_index: int = 0
    scratch_value_mask: int = 0
    scratch_write_mask: int = 0
    motion_playback_active_cue_a: int | None = None
    motion_playback_active_cue_b: int | None = None
    scheduled_effects: LuxScheduledEffectTable = field(
        default_factory=LuxScheduledEffectTable
    )

    guarded_queue_words_c2_c8: list[int] = field(
        default_factory=lambda: [0xFFFF] * 4
    )

    def __post_init__(self) -> None:
        self.lane_index = u16(self.lane_index)
        self.current_move_id = u16(self.current_move_id)
        self.animation_frame_current = float32(self.animation_frame_current)
        self.animation_frame_delta_this_tick = signed_low_i16(
            self.animation_frame_delta_this_tick
        )
        self.animation_frame_previous = float32(self.animation_frame_previous)
        self.animation_end_status = u16(self.animation_end_status)
        self.timing_frame_10 = float32(self.timing_frame_10)
        self.timing_frame_14 = float32(self.timing_frame_14)
        self.playback_speed_current = float32(self.playback_speed_current)
        self.playback_speed_target = float32(self.playback_speed_target)
        self.playback_speed_delta = float32(self.playback_speed_delta)
        self.playback_speed_exponential_rate = float32(
            self.playback_speed_exponential_rate
        )
        self.playback_speed_countdown = float32(self.playback_speed_countdown)
        if self.motion_playback_frame_20 is not None:
            self.motion_playback_frame_20 = float32(self.motion_playback_frame_20)
        if self.motion_playback_frame_24 is not None:
            self.motion_playback_frame_24 = float32(self.motion_playback_frame_24)
        self.transition_source_lane_index = u16(self.transition_source_lane_index)
        self.queued_target_move_id = u16(self.queued_target_move_id)
        self.override_target_on_flag_16fe = u16(
            self.override_target_on_flag_16fe
        )
        self.override_target_on_flag_16eb = u16(
            self.override_target_on_flag_16eb
        )
        self.override_target_on_opponent_move_class = u16(
            self.override_target_on_opponent_move_class
        )
        self.transition_start_frame = float32(self.transition_start_frame)
        self.transition_threshold_frame = float32(
            self.transition_threshold_frame
        )
        self.queued_transition_arguments = tuple(
            u16(value) for value in self.queued_transition_arguments
        )
        self.queued_transition_argument_count &= 0xFFFFFFFF
        self.entry_argument_count = u16(self.entry_argument_count)
        if len(self.entry_arguments) != 17:
            raise ValueError("MoveVM entry-argument block must contain 17 shorts")
        self.entry_arguments[:] = [u16(value) for value in self.entry_arguments]
        self.primary_script_running = u16(self.primary_script_running)
        self.secondary_script_running = u16(self.secondary_script_running)
        self.transition_fired_marker = int(self.transition_fired_marker)
        self.cancel_sentinel_c4 = u16(self.cancel_sentinel_c4)
        self.cancel_word_c8 = u16(self.cancel_word_c8)
        self.cancel_sentinel_d4 = u16(self.cancel_sentinel_d4)
        self.script_rerun_sentinel = u16(self.script_rerun_sentinel)
        self.deferred_transition_target_move_id = u16(
            self.deferred_transition_target_move_id
        )
        self.deferred_transition_destination_lane_index = u16(
            self.deferred_transition_destination_lane_index
        )
        self.deferred_transition_commit_flag = u16(
            self.deferred_transition_commit_flag
        )
        self.deferred_transition_start_frame = float32(
            self.deferred_transition_start_frame
        )
        self.deferred_transition_schedule_frame = float32(
            self.deferred_transition_schedule_frame
        )
        self.total_tick_counter &= 0xFFFFFFFF
        self.sub_frame_trigger_accumulator = float32(
            self.sub_frame_trigger_accumulator
        )
        self.variant_index &= 0xFFFFFFFF
        self.scratch_value_mask &= 0xFFFFFFFFFFFFFFFF
        self.scratch_write_mask &= 0xFFFFFFFFFFFFFFFF
        if self.motion_playback_active_cue_a is not None:
            self.motion_playback_active_cue_a = signed_low_i16(
                self.motion_playback_active_cue_a
            )
        if self.motion_playback_active_cue_b is not None:
            self.motion_playback_active_cue_b = signed_low_i16(
                self.motion_playback_active_cue_b
            )
        if len(self.guarded_queue_words_c2_c8) != 4:
            raise ValueError("lane guarded queue must contain four ushort words")
        self.guarded_queue_words_c2_c8[:] = [
            u16(value) for value in self.guarded_queue_words_c2_c8
        ]


@dataclass
class MoveVMTransitionAuthorState:
    lanes: tuple[MoveVMLaneSchedulerState, ...]
    active_lane_index: int
    chara_timing_scalar_1364: float = 0.0
    deferred_schedule_flag: int = 0
    deferred_schedule_frame: float = 0.0
    deferred_commit_flag: int = 0
    transition_threshold_now_flag: int = 0

    def __post_init__(self) -> None:
        self.chara_timing_scalar_1364 = float32(self.chara_timing_scalar_1364)
        self.deferred_schedule_frame = float32(self.deferred_schedule_frame)
        self.deferred_commit_flag = u16(self.deferred_commit_flag)

    def lane(self, index: int) -> MoveVMLaneSchedulerState:
        for lane in self.lanes:
            if lane.lane_index == index:
                return lane
        raise StaticResolutionError(f"MoveVM lane {index} is unavailable")

    @property
    def active_lane(self) -> MoveVMLaneSchedulerState:
        return self.lane(self.active_lane_index)


def map_bank_slot_timing_index(
    state: MoveVMTransitionAuthorState,
    supplied_lane: MoveVMLaneSchedulerState,
    timing_index: int,
) -> float:
    """Mirror ``LuxMoveVM_MapBankSlotTimingIndex @ 0x1403002B0``."""

    # Native callers sign-extend the authored short before entering this
    # helper. Normalizing here keeps direct offline callers equivalent.
    timing_index = signed_low_i16(timing_index)
    if timing_index < 0x6000:
        return float32(timing_index)

    effective = timing_index
    lane = supplied_lane
    if timing_index <= 0x6C00:
        lane = state.lane(1 if supplied_lane.lane_index == 0 else 0)
        effective += 0x1000

    # All values reaching this switch from a signed i16 operand are positive.
    bucket = (effective + 0x100) // 0x200
    if bucket == 0x3A:
        return add_f32(float32(effective - 0x7400), lane.timing_frame_14)
    if bucket == 0x3B:
        return add_f32(float32(effective - 0x7600), lane.timing_frame_10)
    if bucket in (0x3C, 0x3D):
        if (
            lane.motion_playback_frame_20 is None
            or lane.motion_playback_frame_24 is None
        ):
            raise StaticResolutionError(
                f"timing bucket 0x{bucket:02X} requires lane "
                f"{lane.lane_index} motion-playback state"
            )
        if bucket == 0x3C:
            return add_f32(
                float32(effective - 0x7800), lane.motion_playback_frame_20
            )
        playback_span = sub_f32(
            lane.motion_playback_frame_24, lane.motion_playback_frame_20
        )
        return add_f32(playback_span, float32(effective - 0x7A00))
    if bucket == 0x3E:
        value = add_f32(
            lane.playback_speed_current, lane.animation_frame_current
        )
        return add_f32(value, float32(effective - 0x7C00))
    if bucket == 0x3F:
        return add_f32(
            float32(effective - 0x7E00), state.chara_timing_scalar_1364
        )
    return float32(0.0)


def evaluate_active_lane_timing(
    state: MoveVMTransitionAuthorState,
    authored_args: tuple[int, ...],
) -> int:
    """Mirror CALLCOND 0x25 and IF 0x0008 frame sampling exactly."""

    if len(authored_args) not in (1, 2):
        return 0

    lane = state.active_lane
    delta = signed_low_i16(lane.animation_frame_delta_this_tick)
    sample_count = abs(delta)
    current = float32(lane.animation_frame_current)
    if sample_count < 2:
        samples = (current,)
    else:
        if delta > 0:
            first = sub_f32(current, float32(delta - 1))
            step = float32(1.0)
        else:
            first = add_f32(current, float32(delta + 1))
            step = float32(-1.0)
        generated: list[float] = []
        sample = first
        for _ in range(sample_count):
            generated.append(sample)
            sample = add_f32(sample, step)
        samples = tuple(generated)

    lower_raw = authored_args[0] & 0xFFFF
    lower = signed_low_i16(
        cvttss2si(map_bank_slot_timing_index(state, lane, lower_raw))
    )
    upper_raw = authored_args[1] & 0xFFFF if len(authored_args) == 2 else 0
    upper = (
        signed_low_i16(
            cvttss2si(map_bank_slot_timing_index(state, lane, upper_raw))
        )
        if len(authored_args) == 2
        else 0
    )

    matched = 0
    for sample in samples:
        frame = signed_low_i16(cvttss2si(sample))
        if len(authored_args) == 1:
            matched |= int(lower_raw == 0x7FFF or frame == lower)
        else:
            lower_ok = lower_raw == 0x7FFF or lower <= frame
            upper_ok = upper_raw == 0x7FFF or frame <= upper
            matched |= int(lower_ok and upper_ok)
    return matched


def decode_variadic_transition_arguments(
    state: MoveVMTransitionAuthorState,
    destination_lane_index: int,
    arguments: tuple[int, ...],
) -> int:
    """Publish the immediate/deferred package from ``0x1402FC930``."""

    destination = state.lane(destination_lane_index)
    active = state.active_lane
    target_move_id = 0
    start_frame = float32(0.0)
    threshold_frame = float32(0.0)

    if arguments:
        target_move_id = u16(arguments[0])
        if len(arguments) >= 2:
            if len(arguments) >= 3:
                threshold_frame = map_bank_slot_timing_index(
                    state, destination, arguments[2]
                )
                if cvttss2si(threshold_frame) <= cvttss2si(
                    active.animation_frame_current
                ):
                    threshold_frame = float32(0.0)
            start_frame = map_bank_slot_timing_index(
                state, destination, arguments[1]
            )

    schedule_frame = state.deferred_schedule_frame
    if state.deferred_schedule_flag:
        if schedule_frame >= 0.0:
            threshold_frame = schedule_frame
            if cvttss2si(schedule_frame) <= cvttss2si(
                active.animation_frame_current
            ):
                threshold_frame = float32(0.0)
        if schedule_frame < 0.0:
            active.deferred_transition_target_move_id = target_move_id
            active.deferred_transition_destination_lane_index = u16(
                destination_lane_index
            )
            active.deferred_transition_start_frame = start_frame
            active.deferred_transition_schedule_frame = schedule_frame
            active.deferred_transition_commit_flag = state.deferred_commit_flag
            return 0

    destination.queued_transition_arguments = tuple(
        u16(value) for value in arguments[3:]
    )
    destination.queued_transition_argument_count = len(
        destination.queued_transition_arguments
    )
    if threshold_frame == 0.0:
        state.transition_threshold_now_flag = 1
    destination.transition_threshold_frame = threshold_frame
    destination.transition_start_frame = start_frame
    destination.queued_target_move_id = target_move_id
    destination.transition_source_lane_index = active.lane_index
    return 0


_LANE0_GUARDED_MOVES = frozenset({0x7E, 0x82, 0x86})
_LANE0_GUARDED_TARGETS = frozenset(
    {0x14, 0x15, 0x16, 0x18, 0x21, 0x27, 0x2D, 0x33, 0x39, 0x3F, 0x45}
)


def author_lane0_transition_06(
    state: MoveVMTransitionAuthorState, arguments: tuple[int, ...]
) -> int:
    lane0 = state.lane(0)
    guarded = (
        lane0.guarded_queue_words_c2_c8[0] == 0x58
        and lane0.current_move_id in _LANE0_GUARDED_MOVES
    )
    if guarded:
        if not arguments:
            raise StaticResolutionError(
                "CALLCOND 0x06 guarded lane-0 path dereferences argument word zero"
            )
        if u16(arguments[0]) in _LANE0_GUARDED_TARGETS:
            lane0.guarded_queue_words_c2_c8[:] = [0xFFFF] * 4
    return decode_variadic_transition_arguments(state, 0, arguments)


def author_lane1_transition_07(
    state: MoveVMTransitionAuthorState, arguments: tuple[int, ...]
) -> int:
    return decode_variadic_transition_arguments(state, 1, arguments)
