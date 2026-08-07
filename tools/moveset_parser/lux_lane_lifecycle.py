"""Verified leaf operations in the native MoveVM lane lifecycle."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

from lux_numeric import float32, signed_low_i16
from lux_reference_engine import StaticResolutionError
from lux_transition_author import MoveVMLaneSchedulerState


@dataclass(frozen=True)
class LuxAttackCellLifecycleView:
    master_window_start: int
    master_window_end: int = 0
    range_stand_min: int = -1
    range_stand_max: int = -1
    slot_mask: int = 0
    hitbox_group_bitfield: int = 0
    passthrough_tag_a: int = 0
    passthrough_tag_c: int = 0

    def __post_init__(self) -> None:
        object.__setattr__(
            self, "master_window_start", signed_low_i16(self.master_window_start)
        )
        object.__setattr__(
            self, "master_window_end", signed_low_i16(self.master_window_end)
        )
        object.__setattr__(
            self,
            "range_stand_min",
            (self.range_stand_min & 0xFF) - (0x100 if self.range_stand_min & 0x80 else 0),
        )
        object.__setattr__(
            self,
            "range_stand_max",
            (self.range_stand_max & 0xFF) - (0x100 if self.range_stand_max & 0x80 else 0),
        )
        object.__setattr__(self, "slot_mask", self.slot_mask & 0xFFFFFFFFFFFFFFFF)
        object.__setattr__(
            self, "hitbox_group_bitfield", self.hitbox_group_bitfield & 0xFFFF
        )
        object.__setattr__(self, "passthrough_tag_a", self.passthrough_tag_a & 0xFFFF)
        object.__setattr__(self, "passthrough_tag_c", self.passthrough_tag_c & 0xFFFF)


SecondaryLaneScriptExecutor = Callable[
    ["MoveVMCharacterLifecycleState", MoveVMLaneSchedulerState], None
]
LaneVariantReferenceResolver = Callable[[int, int], int | None]
TransitionToMoveExecutor = Callable[
    ["MoveVMCharacterLifecycleState", int, int, float], int
]


@dataclass
class MoveVMCharacterLifecycleState:
    lanes: tuple[MoveVMLaneSchedulerState, ...]
    active_vm_lane_index: int
    active_lane_state_cursor_index: int | None = None
    own_active_attack_cell: LuxAttackCellLifecycleView | None = None
    counter_descriptor_identity: object | None = None
    live_attack_flag_16eb: int = 0
    live_attack_flag_16ec: int = 0
    live_attack_flag_16fe: int = 0
    saved_attack_flag_171e: int = 0
    saved_attack_flag_171f: int = 0
    saved_attack_flag_1720: int = 0
    snapshot_source_1354: int = 0
    snapshot_copy_2134: int = 0
    snapshot_attack_cell_identity: LuxAttackCellLifecycleView | None = None
    snapshot_counter_descriptor_identity: object | None = None
    move_end_snapshot_pending_2130: int = 0
    move_bank_available: bool = False
    lane2_state_1725_1728: bytes = b"\0\0\0\0"
    lane2_state_1729: int = 0
    motion_state_flags_16d0: list[int] = field(default_factory=lambda: [0] * 64)
    secondary_script_executor: SecondaryLaneScriptExecutor | None = None
    variant_reference_resolver: LaneVariantReferenceResolver | None = None
    transition_to_move_executor: TransitionToMoveExecutor | None = None
    round_result_state_3d0: int = 0
    vm_break_flag: int = 0

    def __post_init__(self) -> None:
        if len(self.motion_state_flags_16d0) != 64:
            raise ValueError("MoveVM current-state flag bank must contain 64 bytes")
        self.motion_state_flags_16d0[:] = [value & 0xFF for value in self.motion_state_flags_16d0]
        if len(self.lane2_state_1725_1728) != 4:
            raise ValueError("lane-2 state at +0x1725 must contain four bytes")
        self.round_result_state_3d0 &= 0xFFFFFFFF

    def lane(self, index: int) -> MoveVMLaneSchedulerState:
        for lane in self.lanes:
            if lane.lane_index == index:
                return lane
        raise StaticResolutionError(f"MoveVM lifecycle lane {index} is unavailable")


def deactivate_lane(lane: MoveVMLaneSchedulerState) -> None:
    """Mirror ``LuxMoveVM_DeactivateLane @ 0x1402FDD00``.

    This deliberately preserves animation-end status and every playback field
    except each linked slot's active cue.
    """

    lane.primary_script_running = 0
    lane.secondary_script_running = 0
    lane.cancel_sentinel_d4 = 0xFFFF
    lane.cancel_sentinel_c4 = 0xFFFF
    lane.queued_target_move_id = 0xFFFF
    lane.override_target_on_flag_16fe = 0xFFFF
    lane.override_target_on_flag_16eb = 0xFFFF
    lane.override_target_on_opponent_move_class = 0xFFFF
    lane.deferred_transition_target_move_id = 0xFFFF
    lane.current_move_id = 0xFFFF
    lane.transition_fired_marker = 0
    lane.cancel_word_c8 = 0
    lane.deferred_transition_commit_flag = 0
    lane.total_tick_counter = 0
    if lane.motion_playback_active_cue_a is not None:
        lane.motion_playback_active_cue_a = -1
    if lane.motion_playback_active_cue_b is not None:
        lane.motion_playback_active_cue_b = -1


def stage_transition_entry_arguments(
    lane: MoveVMLaneSchedulerState,
    arguments: tuple[int, ...],
) -> None:
    """Mirror the +0x6C/+0x6E copy at ``0x1402FE3AF``.

    The native storage contains 17 signed-short words.  The reviewed authored
    corpus never exceeds that capacity; refusing a larger offline request is
    preferable to emulating native adjacent-field memory corruption.
    """

    if len(arguments) > 17:
        raise StaticResolutionError(
            "MoveVM transition entry argument count exceeds native 17-word storage"
        )
    lane.entry_argument_count = len(arguments)
    for index, value in enumerate(arguments):
        lane.entry_arguments[index] = value & 0xFFFF


def reset_lane_for_transition_target_resolution(
    lane: MoveVMLaneSchedulerState,
    packed_move_id: int,
) -> None:
    """Mirror TransitionToMove's deliberate pre-validation lane mutations.

    This begins after same-move/outgoing-script and facing handling.  It does
    not claim the target-resolution, motion, attack-cell, or entry-bytecode
    closure that follows in the native function.
    """

    lane.queued_target_move_id = 0xFFFF
    lane.override_target_on_flag_16fe = 0xFFFF
    lane.override_target_on_flag_16eb = 0xFFFF
    lane.override_target_on_opponent_move_class = 0xFFFF
    lane.script_rerun_sentinel = 0
    lane.sub_frame_trigger_accumulator = float32(0.0)
    lane.current_move_id = packed_move_id & 0xFFFF
    if lane.deferred_transition_commit_flag == 0:
        lane.deferred_transition_target_move_id = 0xFFFF
    for entry in lane.scheduled_effects.entries:
        entry.trigger_frame = -1


def run_secondary_lane_script(
    state: MoveVMCharacterLifecycleState,
    lane: MoveVMLaneSchedulerState,
) -> None:
    """Mirror the state-owning wrapper at ``0x1402FE1C0``."""

    if lane.current_move_id == 0xFFFF or lane.primary_script_running != 0:
        return
    executor = state.secondary_script_executor
    if executor is None:
        raise StaticResolutionError(
            "secondary lane script is reachable but no exact bank-script executor is bound"
        )

    saved_active_lane = state.active_vm_lane_index
    state.active_vm_lane_index = lane.lane_index
    lane.script_rerun_sentinel = 0
    lane.secondary_script_running = 1
    try:
        executor(state, lane)
    finally:
        lane.secondary_script_running = 0
        state.active_vm_lane_index = saved_active_lane


def commit_move_end(
    state: MoveVMCharacterLifecycleState,
    lane: MoveVMLaneSchedulerState,
) -> None:
    """Mirror the complete deterministic transaction at ``0x1402FCFB0``."""

    owns_attack_state = (
        state.own_active_attack_cell is not None
        or state.counter_descriptor_identity is not None
    ) and state.active_lane_state_cursor_index == lane.lane_index
    if owns_attack_state:
        cell = state.own_active_attack_cell
        if cell is not None:
            saved_16ec = state.live_attack_flag_16ec & 0xFF
            state.saved_attack_flag_171e = state.live_attack_flag_16eb & 0xFF
            state.saved_attack_flag_171f = state.live_attack_flag_16fe & 0xFF
            state.saved_attack_flag_1720 = saved_16ec
            if (
                float32(cell.master_window_start) <= lane.animation_frame_previous
                and state.live_attack_flag_16eb == 0
            ):
                if state.live_attack_flag_16fe == 0:
                    saved_16ec = 1
                state.saved_attack_flag_1720 = saved_16ec
        state.live_attack_flag_16eb = 0
        state.live_attack_flag_16ec = 0
        state.live_attack_flag_16fe = 0
        if state.snapshot_source_1354 != 0:
            state.snapshot_copy_2134 = state.snapshot_source_1354
            state.snapshot_attack_cell_identity = state.own_active_attack_cell
            state.snapshot_counter_descriptor_identity = (
                state.counter_descriptor_identity
            )
        state.move_end_snapshot_pending_2130 = 1

    if (
        lane.lane_index == 2
        and lane.current_move_id != 0xFFFF
        and state.move_bank_available
    ):
        resolver = state.variant_reference_resolver
        if resolver is None:
            raise StaticResolutionError(
                "lane-2 move-end commit requires the exact bank variant resolver"
            )
        variant_reference = resolver(lane.current_move_id, lane.variant_index)
        if variant_reference is not None:
            signed_reference = signed_low_i16(variant_reference)
            if signed_reference != -1 and ((variant_reference & 0xFFFF) >> 12) & 1 == 0:
                state.lane2_state_1725_1728 = b"\0\0\0\0"
                state.lane2_state_1729 = 0
                state.lane(2).variant_index = 0

    for entry in lane.scheduled_effects.entries:
        entry.trigger_frame = -1

    run_secondary_lane_script(state, lane)

    if lane.current_move_id != 0xFFFF:
        for index in range(64):
            if (lane.scratch_write_mask >> index) & 1:
                state.motion_state_flags_16d0[index] = (
                    lane.scratch_value_mask >> index
                ) & 1

    ended_active_vm_lane = state.active_vm_lane_index == lane.lane_index
    deactivate_lane(lane)
    if ended_active_vm_lane:
        state.vm_break_flag = -1


def drain_pending_transition(
    state: MoveVMCharacterLifecycleState,
    arguments: tuple[int, ...],
) -> int:
    """Mirror CALLCOND 0x16's drain transaction at ``0x1402FCDE0``.

    The caller must bind the complete native-equivalent TransitionToMove
    executor.  Keeping that dependency explicit prevents the small drain
    wrapper from being mistaken for a complete transition implementation.
    """

    source = state.lane(state.active_vm_lane_index)
    target_move_id = source.deferred_transition_target_move_id
    if target_move_id == 0xFFFF:
        return 0

    if state.round_result_state_3d0 != 0:
        source.deferred_transition_target_move_id = 0xFFFF
        source.deferred_transition_commit_flag = 0
        return 0

    destination_index = signed_low_i16(
        source.deferred_transition_destination_lane_index
    )
    destination = state.lane(destination_index)
    executor = state.transition_to_move_executor
    if executor is None:
        raise StaticResolutionError(
            "CALLCOND 0x16 requires the complete LuxMoveVM_TransitionToMove executor"
        )

    start_frame = float32(source.deferred_transition_start_frame)
    executor(
        state,
        destination_index,
        signed_low_i16(target_move_id),
        start_frame,
    )

    commit_distinct_source = (
        bool(arguments)
        and signed_low_i16(arguments[0]) == 2
        and source.lane_index
        != source.deferred_transition_destination_lane_index
    )
    if commit_distinct_source:
        commit_move_end(state, source)
    else:
        source.deferred_transition_commit_flag = 0
        source.deferred_transition_target_move_id = 0xFFFF

    destination.transition_fired_marker = 1
    return 1
