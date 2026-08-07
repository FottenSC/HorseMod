from __future__ import annotations

import pytest

from lux_lane_lifecycle import (
    LuxAttackCellLifecycleView,
    MoveVMCharacterLifecycleState,
    commit_move_end,
    deactivate_lane,
    reset_lane_for_transition_target_resolution,
    stage_transition_entry_arguments,
)
from lux_reference_engine import StaticResolutionError
from lux_transition_author import MoveVMLaneSchedulerState


def test_deactivate_lane_resets_exact_native_fields() -> None:
    lane = MoveVMLaneSchedulerState(
        current_move_id=23,
        animation_end_status=1,
        primary_script_running=0x101,
        secondary_script_running=0x202,
        transition_fired_marker=9,
        queued_target_move_id=0x1234,
        override_target_on_flag_16fe=1,
        override_target_on_flag_16eb=2,
        override_target_on_opponent_move_class=3,
        cancel_sentinel_c4=3,
        cancel_word_c8=4,
        cancel_sentinel_d4=5,
        deferred_transition_target_move_id=7,
        deferred_transition_commit_flag=8,
        transition_threshold_frame=1.0,
        total_tick_counter=99,
        queued_transition_argument_count=3,
        queued_transition_arguments=(1, 2, 3, 4),
        entry_argument_count=2,
        entry_arguments=[5, 6] + [0] * 15,
        motion_playback_active_cue_a=11,
        motion_playback_active_cue_b=12,
    )

    deactivate_lane(lane)

    assert lane.current_move_id == 0xFFFF
    assert lane.animation_end_status == 1
    assert lane.primary_script_running == 0
    assert lane.secondary_script_running == 0
    assert lane.transition_fired_marker == 0
    assert lane.cancel_sentinel_c4 == 0xFFFF
    assert lane.cancel_word_c8 == 0
    assert lane.cancel_sentinel_d4 == 0xFFFF
    assert lane.queued_target_move_id == 0xFFFF
    assert lane.override_target_on_flag_16fe == 0xFFFF
    assert lane.override_target_on_flag_16eb == 0xFFFF
    assert lane.override_target_on_opponent_move_class == 0xFFFF
    assert lane.deferred_transition_target_move_id == 0xFFFF
    assert lane.deferred_transition_commit_flag == 0
    assert lane.transition_threshold_frame == 1.0
    assert lane.total_tick_counter == 0
    assert lane.queued_transition_argument_count == 3
    assert lane.queued_transition_arguments == (1, 2, 3, 4)
    assert lane.entry_argument_count == 2
    assert lane.entry_arguments[:2] == [5, 6]
    assert lane.motion_playback_active_cue_a == -1
    assert lane.motion_playback_active_cue_b == -1


def test_deactivate_lane_preserves_absent_playback_identities() -> None:
    lane = MoveVMLaneSchedulerState(
        motion_playback_active_cue_a=None,
        motion_playback_active_cue_b=None,
    )
    deactivate_lane(lane)
    assert lane.motion_playback_active_cue_a is None
    assert lane.motion_playback_active_cue_b is None


def test_entry_argument_staging_has_independent_fixed_native_storage() -> None:
    lane = MoveVMLaneSchedulerState(
        entry_argument_count=4,
        entry_arguments=[0xAAAA] * 17,
        queued_transition_argument_count=2,
        queued_transition_arguments=(0x1111, 0x2222),
    )

    stage_transition_entry_arguments(lane, (1, 0xFFFF, 3))

    assert lane.entry_argument_count == 3
    assert lane.entry_arguments[:4] == [1, 0xFFFF, 3, 0xAAAA]
    assert lane.queued_transition_argument_count == 2
    assert lane.queued_transition_arguments == (0x1111, 0x2222)

    with pytest.raises(StaticResolutionError, match="17-word storage"):
        stage_transition_entry_arguments(lane, tuple(range(18)))


def test_transition_prevalidation_reset_is_deliberately_non_atomic() -> None:
    lane = MoveVMLaneSchedulerState(
        current_move_id=0x20,
        queued_target_move_id=0x31,
        override_target_on_flag_16fe=0x40,
        override_target_on_flag_16eb=0x41,
        override_target_on_opponent_move_class=0x42,
        script_rerun_sentinel=9,
        sub_frame_trigger_accumulator=3.25,
        deferred_transition_target_move_id=0x55,
        deferred_transition_commit_flag=0,
    )
    lane.scheduled_effects.register((3, 0x44))

    reset_lane_for_transition_target_resolution(lane, 0x12345)

    assert lane.current_move_id == 0x2345
    assert lane.queued_target_move_id == 0xFFFF
    assert lane.override_target_on_flag_16fe == 0xFFFF
    assert lane.override_target_on_flag_16eb == 0xFFFF
    assert lane.override_target_on_opponent_move_class == 0xFFFF
    assert lane.script_rerun_sentinel == 0
    assert lane.sub_frame_trigger_accumulator == 0.0
    assert lane.deferred_transition_target_move_id == 0xFFFF
    assert all(entry.trigger_frame == -1 for entry in lane.scheduled_effects.entries)


def test_commit_move_end_preserves_attack_state_runs_script_and_applies_mask() -> None:
    lane0 = MoveVMLaneSchedulerState(lane_index=0)
    lane1 = MoveVMLaneSchedulerState(
        lane_index=1,
        current_move_id=12,
        animation_frame_previous=8.0,
        scratch_value_mask=(1 << 2),
        scratch_write_mask=(1 << 2) | (1 << 3),
    )
    lane1.scheduled_effects.register((3, 0x44))
    observations: list[tuple[int, int]] = []

    def execute_secondary(state, lane) -> None:
        observations.append((state.active_vm_lane_index, lane.secondary_script_running))
        state.motion_state_flags_16d0[7] = 1

    state = MoveVMCharacterLifecycleState(
        lanes=(lane0, lane1),
        active_vm_lane_index=1,
        active_lane_state_cursor_index=1,
        own_active_attack_cell=LuxAttackCellLifecycleView(6),
        live_attack_flag_16eb=0,
        live_attack_flag_16ec=0,
        live_attack_flag_16fe=0,
        snapshot_source_1354=17,
        secondary_script_executor=execute_secondary,
    )

    commit_move_end(state, lane1)

    assert observations == [(1, 1)]
    assert state.saved_attack_flag_1720 == 1
    assert state.snapshot_copy_2134 == 17
    assert state.snapshot_attack_cell_identity is state.own_active_attack_cell
    assert state.move_end_snapshot_pending_2130 == 1
    assert state.motion_state_flags_16d0[2:4] == [1, 0]
    assert state.motion_state_flags_16d0[7] == 1
    assert all(entry.trigger_frame == -1 for entry in lane1.scheduled_effects.entries)
    assert lane1.current_move_id == 0xFFFF
    assert state.vm_break_flag == -1


def test_commit_move_end_requires_reachable_secondary_script_executor() -> None:
    lane = MoveVMLaneSchedulerState(lane_index=0, current_move_id=2)
    state = MoveVMCharacterLifecycleState(lanes=(lane,), active_vm_lane_index=0)
    with pytest.raises(StaticResolutionError, match="secondary lane script"):
        commit_move_end(state, lane)


def test_commit_move_end_lane2_variant_branch_fails_closed_without_resolver() -> None:
    lane = MoveVMLaneSchedulerState(lane_index=2, current_move_id=2)
    state = MoveVMCharacterLifecycleState(
        lanes=(lane,),
        active_vm_lane_index=0,
        move_bank_available=True,
    )
    with pytest.raises(StaticResolutionError, match="variant resolver"):
        commit_move_end(state, lane)


@pytest.mark.parametrize(("reference", "clears"), [(-1, False), (0x1002, False), (2, True)])
def test_commit_move_end_lane2_variant_reference_gate(reference: int, clears: bool) -> None:
    lane = MoveVMLaneSchedulerState(lane_index=2, current_move_id=2, variant_index=4)
    state = MoveVMCharacterLifecycleState(
        lanes=(lane,),
        active_vm_lane_index=0,
        move_bank_available=True,
        lane2_state_1725_1728=b"ABCD",
        lane2_state_1729=7,
        variant_reference_resolver=lambda move, variant: reference,
        secondary_script_executor=lambda state, lane: None,
    )
    commit_move_end(state, lane)
    assert (state.lane2_state_1725_1728 == b"\0\0\0\0") is clears
    assert (state.lane2_state_1729 == 0) is clears
    assert (lane.variant_index == 0) is clears
