from pathlib import Path
from types import SimpleNamespace

import pytest

from locomotion_movement import (
    BACKWALK_START_SLOT,
    CHARACTERS,
    FORWARD_RUN_START_SLOT,
    MOVE_TABLE_INDEX_BY_CID,
    _route_horizontal_velocity_call,
)
from lux_movement_vm import (
    LaneTimingState,
    evaluate_timing,
    execute_velocity_helper,
    execute_bank_slot_script,
    execute_secondary_lane_script,
    MovementHelperState,
    required_character_state_slots,
)
from lux_reference_engine import MoveVMContext, MoveVMReference, StaticResolutionError
from lux_lane_lifecycle import MoveVMCharacterLifecycleState
from lux_transition_author import MoveVMLaneSchedulerState
from stackvm import walk_stackvm
from luxformats import parse_khd
from stackvm_emulate import emulate


pytestmark = pytest.mark.needs_dump


def test_timing_point_and_unbounded_window() -> None:
    timing = LaneTimingState(current_frame=3.0)
    assert evaluate_timing(timing, (3,)) == 1
    assert evaluate_timing(timing, (4,)) == 0
    assert evaluate_timing(timing, (0x7FFF, 2)) == 0
    assert evaluate_timing(timing, (0x7FFF, 3)) == 1


@pytest.mark.parametrize("cid", list(CHARACTERS))
@pytest.mark.parametrize(
    ("route_slot", "direction"),
    ((BACKWALK_START_SLOT, 4), (FORWARD_RUN_START_SLOT, 6)),
)
def test_route_selected_helper_executes_without_character_speed_schedule(
    cid: str, route_slot: int, direction: int
) -> None:
    root = Path(__file__).resolve().parents[3] / "dump" / "Battle" / "hdr"
    bank = parse_khd((root / f"hdr{cid}.khd").read_bytes())
    route = emulate(bank.slots[route_slot].bytecode, route_slot)
    call = _route_horizontal_velocity_call(bank, route)
    assert call.packed_move_id is not None
    assert call.concrete_args == [call.packed_move_id, direction]
    outcome = execute_velocity_helper(
        bank,
        call.packed_move_id,
        direction_selector=direction,
        move_table_index=MOVE_TABLE_INDEX_BY_CID[cid],
        chara_state_shorts={
            index: 0
            for index in required_character_state_slots(bank, call.packed_move_id)
        },
    )
    writes = [effect for effect in outcome.effects if effect.opcode == 0x0004]
    if writes:
        assert outcome.effect_angle_word == (180 if direction == 4 else 0)
    else:
        # Absence of a write is a real authored outcome, not permission to
        # substitute a hand-decoded table value.
        assert outcome.effect_speed_word == 0


def test_character_specific_routes_emerge_from_authored_predicates() -> None:
    root = Path(__file__).resolve().parents[3] / "dump" / "Battle" / "hdr"

    voldo = parse_khd((root / "hdr005.khd").read_bytes())
    front = execute_velocity_helper(
        voldo, 0x30C1, direction_selector=6, move_table_index=4,
        opponent_relative_angle_turns=0.0,
    )
    rear = execute_velocity_helper(
        voldo, 0x30C1, direction_selector=6, move_table_index=4,
        opponent_relative_angle_turns=0.5,
    )
    assert front.effect_angle_word == 0
    assert rear.effect_angle_word == 180

    hwang = parse_khd((root / "hdr009.khd").read_bytes())
    ordinary = execute_velocity_helper(
        hwang, 0x30C6, direction_selector=4, move_table_index=0,
    )
    authored_style = execute_velocity_helper(
        hwang, 0x30C6, direction_selector=4, move_table_index=0x22,
    )
    assert authored_style.effect_speed_word - ordinary.effect_speed_word == 70


def test_missing_character_state_is_refused_and_explicit_tira_states_diverge() -> None:
    root = Path(__file__).resolve().parents[3] / "dump" / "Battle" / "hdr"
    tira = parse_khd((root / "hdr023.khd").read_bytes())
    assert required_character_state_slots(tira, 0x30C1) == (25,)
    with pytest.raises(StaticResolutionError, match="state slot 25"):
        execute_velocity_helper(
            tira, 0x30C1, direction_selector=4, move_table_index=0x10,
        )
    state_zero = execute_velocity_helper(
        tira, 0x30C1, direction_selector=2, move_table_index=0x10,
        chara_state_shorts={25: 0},
    )
    state_one = execute_velocity_helper(
        tira, 0x30C1, direction_selector=2, move_table_index=0x10,
        chara_state_shorts={25: 1},
    )
    assert state_zero.effect_speed_word != state_one.effect_speed_word


def test_nested_bank_script_seeds_local_zero_and_restores_active_move() -> None:
    root = Path(__file__).resolve().parents[3] / "dump" / "Battle" / "hdr"
    bank = parse_khd((root / "hdr001.khd").read_bytes())
    state = MovementHelperState(
        move_table_index=0,
        active_move_id=0x1234,
        timing=LaneTimingState(current_frame=3.0),
    )
    vm = MoveVMReference()

    def if_handler(context, arguments):
        from lux_reference_engine import CallCondResult
        from lux_movement_vm import evaluate_if
        return CallCondResult(evaluate_if(state, arguments))

    def timing_handler(context, arguments):
        from lux_reference_engine import CallCondResult
        return CallCondResult(evaluate_timing(state.timing, arguments))

    def effect_handler(context, arguments):
        from lux_reference_engine import CallCondResult
        opcode = arguments[0] & 0xFFFF
        if opcode == 4:
            state.effect_angle_word = arguments[1]
            state.effect_speed_word = arguments[2]
        elif opcode == 6:
            state.effect_angle_word = state.effect_speed_word = 0
        else:
            raise StaticResolutionError(f"unexpected effect {opcode}")
        return CallCondResult(0)

    handlers = {}
    context = MoveVMContext(handlers=handlers)
    handlers.update({
        0x01: if_handler,
        0x03: effect_handler,
        0x25: timing_handler,
        0x0D: lambda ctx, args: execute_bank_slot_script(vm, bank, ctx, state, args),
    })
    # Parent script invokes packed helper 0x30C1 with nested local[0] = 6.
    parent = walk_stackvm(bytes([
        0x89, 0x30, 0xC1,
        0x89, 0x00, 0x06,
        0x25, 0x0D, 0x02,
        0x02,
    ]), 0)
    vm.execute(parent, context)
    assert state.effect_angle_word == 0
    assert state.effect_speed_word == 12
    assert state.active_move_id == 0x1234


def test_secondary_lane_script_provider_resolves_current_slot_and_zeroes_locals() -> None:
    script = walk_stackvm(bytes((0x8A, 0x00, 0xF0, 0x05)), 0)
    bank = SimpleNamespace(slots=[SimpleNamespace(bytecode=script)])
    lane = MoveVMLaneSchedulerState(lane_index=1, current_move_id=0)
    lifecycle = MoveVMCharacterLifecycleState(
        lanes=(lane,), active_vm_lane_index=1
    )
    context = MoveVMContext(locals=[77] * 16)

    execute_secondary_lane_script(
        MoveVMReference(), bank, context, lifecycle, lane
    )

    assert context.locals == [77] * 16
    assert {
        "LuxMoveVM_RunSecondaryLaneScript@0x1402FE1C0",
        "LuxMoveVM_ResolveBankSlot@0x1402FC400",
        "LuxMoveVM_RunBytecodeScript@0x1402E67B0",
    } <= context.coverage.resolved_functions


def test_secondary_lane_script_provider_rejects_unresolved_move_id() -> None:
    lane = MoveVMLaneSchedulerState(lane_index=1, current_move_id=4)
    lifecycle = MoveVMCharacterLifecycleState(
        lanes=(lane,), active_vm_lane_index=1
    )
    with pytest.raises(StaticResolutionError, match="does not resolve"):
        execute_secondary_lane_script(
            MoveVMReference(), SimpleNamespace(slots=[]), MoveVMContext(), lifecycle, lane
        )
