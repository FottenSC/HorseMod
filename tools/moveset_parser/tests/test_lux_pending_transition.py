import pytest

from lux_lane_lifecycle import (
    MoveVMCharacterLifecycleState,
    drain_pending_transition,
)
from lux_reference_engine import StaticResolutionError
from lux_transition_author import MoveVMLaneSchedulerState


def make_state(*, destination: int = 1) -> tuple[
    MoveVMCharacterLifecycleState,
    MoveVMLaneSchedulerState,
    MoveVMLaneSchedulerState,
]:
    source = MoveVMLaneSchedulerState(
        lane_index=0,
        current_move_id=0x20,
        deferred_transition_target_move_id=0x31,
        deferred_transition_destination_lane_index=destination,
        deferred_transition_commit_flag=1,
        deferred_transition_start_frame=7.25,
    )
    target = MoveVMLaneSchedulerState(lane_index=1, current_move_id=0x40)
    state = MoveVMCharacterLifecycleState(
        lanes=(source, target), active_vm_lane_index=0
    )
    return state, source, target


def test_absent_pending_transition_is_a_noop_without_executor() -> None:
    state, source, target = make_state()
    source.deferred_transition_target_move_id = 0xFFFF

    assert drain_pending_transition(state, ()) == 0
    assert source.deferred_transition_commit_flag == 1
    assert target.transition_fired_marker == 0


def test_round_result_cancels_pending_package_before_executor_lookup() -> None:
    state, source, target = make_state()
    state.round_result_state_3d0 = 3

    assert drain_pending_transition(state, (2,)) == 0
    assert source.deferred_transition_target_move_id == 0xFFFF
    assert source.deferred_transition_commit_flag == 0
    assert target.transition_fired_marker == 0


def test_reachable_transition_fails_closed_without_complete_executor() -> None:
    state, _source, _target = make_state()
    with pytest.raises(
        StaticResolutionError,
        match="complete LuxMoveVM_TransitionToMove executor",
    ):
        drain_pending_transition(state, ())


def test_ordinary_drain_transitions_then_clears_source_and_marks_destination() -> None:
    state, source, target = make_state()
    calls: list[tuple[int, int, float]] = []

    def transition(
        _state: MoveVMCharacterLifecycleState,
        lane_index: int,
        move_id: int,
        start_frame: float,
    ) -> int:
        calls.append((lane_index, move_id, start_frame))
        return 19

    state.transition_to_move_executor = transition

    assert drain_pending_transition(state, ()) == 1
    assert calls == [(1, 0x31, 7.25)]
    assert source.current_move_id == 0x20
    assert source.deferred_transition_target_move_id == 0xFFFF
    assert source.deferred_transition_commit_flag == 0
    assert target.transition_fired_marker == 1


def test_operand_two_cross_lane_commits_source_after_transition() -> None:
    state, source, target = make_state()
    order: list[str] = []

    def transition(
        _state: MoveVMCharacterLifecycleState,
        lane_index: int,
        move_id: int,
        start_frame: float,
    ) -> int:
        assert (lane_index, move_id, start_frame) == (1, 0x31, 7.25)
        order.append("transition")
        return 0

    def secondary(
        _state: MoveVMCharacterLifecycleState,
        lane: MoveVMLaneSchedulerState,
    ) -> None:
        assert lane is source
        order.append("source-entry")

    state.transition_to_move_executor = transition
    state.secondary_script_executor = secondary

    assert drain_pending_transition(state, (2,)) == 1
    assert order == ["transition", "source-entry"]
    assert source.current_move_id == 0xFFFF
    assert source.deferred_transition_target_move_id == 0xFFFF
    assert target.transition_fired_marker == 1


def test_operand_two_same_lane_uses_ordinary_clear_path() -> None:
    lane = MoveVMLaneSchedulerState(
        lane_index=0,
        current_move_id=0x20,
        deferred_transition_target_move_id=0x31,
        deferred_transition_destination_lane_index=0,
        deferred_transition_commit_flag=1,
        deferred_transition_start_frame=2.0,
    )
    state = MoveVMCharacterLifecycleState(
        lanes=(lane,),
        active_vm_lane_index=0,
        transition_to_move_executor=lambda *_args: 0,
    )

    assert drain_pending_transition(state, (2,)) == 1
    assert lane.current_move_id == 0x20
    assert lane.deferred_transition_target_move_id == 0xFFFF
    assert lane.deferred_transition_commit_flag == 0
    assert lane.transition_fired_marker == 1


def test_destination_lane_is_validated_before_native_executor_call() -> None:
    state, _source, _target = make_state(destination=7)
    state.transition_to_move_executor = lambda *_args: 0
    with pytest.raises(StaticResolutionError, match="lane 7 is unavailable"):
        drain_pending_transition(state, ())
