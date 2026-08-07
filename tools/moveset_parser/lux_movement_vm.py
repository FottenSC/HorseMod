"""Concrete static execution for the native movement-helper subset.

This is the first production consumer of ``MoveVMReference``.  It replaces
hand-copied speed tables by executing each character's authored 0x30C1-family
script with the verified IF 0x006B/0x13D7 and timing semantics.  Unknown
predicates or effects fail closed.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_lane_lifecycle import MoveVMCharacterLifecycleState
from lux_numeric import add_f32, cvttss2si, div_f32, float32, signed_low_i16, sub_f32
from lux_reference_engine import (
    CallCondResult,
    MoveVMContext,
    MoveVMReference,
    StaticResolutionError,
)
from lux_transition_author import MoveVMLaneSchedulerState
from luxformats import KhdFile
from stackvm_emulate import Concrete, emulate


@dataclass(frozen=True)
class LaneTimingState:
    current_frame: float = 0.0
    frame_delta: int = 1
    timing_frame_10: float = 0.0
    timing_frame_14: float = 0.0
    motion_start_frame: float = 0.0
    motion_end_frame: float = 0.0
    playback_speed: float = 1.0
    chara_timing_scalar: float = 0.0


@dataclass(frozen=True)
class EffectCommand:
    opcode: int
    arguments: tuple[int, ...]


@dataclass
class MovementHelperState:
    move_table_index: int
    stat_fields: dict[int, float] = field(default_factory=dict)
    chara_state_shorts: dict[int, int] = field(default_factory=dict)
    opponent_relative_angle_turns: float = 0.0
    opponent_previous_health: float = 0.0
    timing: LaneTimingState = field(default_factory=LaneTimingState)
    effects: list[EffectCommand] = field(default_factory=list)
    effect_angle_word: int = 0
    effect_speed_word: int = 0
    active_move_id: int = 0


def map_timing_index(state: LaneTimingState, authored: int) -> float:
    index = signed_low_i16(authored)
    if index < 0x6000:
        return float32(index)
    # Paired-lane selectors 0x6000..0x6C00 add 0x1000 before bucket
    # selection.  The compact helper model has one supplied lane, so only the
    # numeric remap is relevant here.
    if index <= 0x6C00:
        index += 0x1000
    bucket = (index + 0x100 + (((index + 0x100) >> 31) & 0x1FF)) >> 9
    if bucket == 0x3A:
        return float32((index - 0x7400) + state.timing_frame_14)
    if bucket == 0x3B:
        return float32((index - 0x7600) + state.timing_frame_10)
    if bucket == 0x3C:
        return float32((index - 0x7800) + state.motion_start_frame)
    if bucket == 0x3D:
        return float32(
            (state.motion_end_frame - state.motion_start_frame) + (index - 0x7A00)
        )
    if bucket == 0x3E:
        return float32(state.playback_speed + state.current_frame + (index - 0x7C00))
    if bucket == 0x3F:
        return float32((index - 0x7E00) + state.chara_timing_scalar)
    return 0.0


def _frame_samples(timing: LaneTimingState) -> tuple[float, ...]:
    delta = signed_low_i16(timing.frame_delta)
    span = abs(delta)
    current = float32(timing.current_frame)
    if span < 2:
        return (current,)
    if delta < 1:
        first = float32(current + float32(delta + 1))
        step = -1.0
    else:
        first = float32(current - float32(delta - 1))
        step = 1.0
    return tuple(float32(first + index * step) for index in range(span))


def evaluate_timing(state: LaneTimingState, authored_args: tuple[int, ...]) -> int:
    # CALLCOND 0x25 inserts IF opcode 0x0008, so one authored operand is the
    # point form and two are the inclusive-window form.
    if len(authored_args) not in (1, 2):
        return 0
    lower_raw = authored_args[0] & 0xFFFF
    upper_raw = (authored_args[1] if len(authored_args) > 1 else 0) & 0xFFFF
    lower = signed_low_i16(cvttss2si(map_timing_index(state, lower_raw)))
    upper = signed_low_i16(cvttss2si(map_timing_index(state, upper_raw)))
    for sample in _frame_samples(state):
        frame = signed_low_i16(cvttss2si(sample))
        if len(authored_args) == 1:
            if lower_raw == 0x7FFF or frame == lower:
                return 1
        elif (
            (lower_raw == 0x7FFF or lower <= frame)
            and (upper_raw == 0x7FFF or frame <= upper)
        ):
            return 1
    return 0


def wrap_turns_signed_half(turns: float) -> float:
    turns = float32(turns)
    remainder = sub_f32(turns, float32(cvttss2si(turns)))
    if turns <= 0.0:
        return add_f32(remainder, 1.0) if remainder < -0.5 else remainder
    return add_f32(remainder, -1.0) if remainder > 0.5 else remainder


def evaluate_if(state: MovementHelperState, arguments: tuple[int, ...]) -> int:
    if not arguments:
        raise StaticResolutionError("EvaluateIfOpcode missing subopcode")
    opcode = arguments[0] & 0xFFFF
    if opcode == 0x006B and len(arguments) >= 2:
        return int(signed_low_i16(arguments[1]) == state.move_table_index)
    if opcode == 0x0013 and len(arguments) >= 3:
        lower = wrap_turns_signed_half(
            div_f32(float32(signed_low_i16(arguments[1])), 360.0)
        )
        upper = wrap_turns_signed_half(
            div_f32(float32(signed_low_i16(arguments[2])), 360.0)
        )
        subject = wrap_turns_signed_half(state.opponent_relative_angle_turns)
        if upper < lower:
            upper = add_f32(upper, 1.0)
        if subject < lower:
            subject = add_f32(subject, 1.0)
        return int(lower <= subject <= upper)
    if opcode == 0x0022 and len(arguments) >= 3:
        index = signed_low_i16(arguments[1])
        if index not in state.chara_state_shorts:
            raise StaticResolutionError(
                f"IF 0x0022 requires unresolved character-state slot {index}"
            )
        actual = state.chara_state_shorts[index] & 0xFFFF
        return int(actual == signed_low_i16(arguments[2]))
    if opcode == 0x13D7 and len(arguments) == 3:
        selector = signed_low_i16(arguments[1])
        key = 0x3F if selector == 0 else 0x40 if selector == 1 else None
        queried = state.stat_fields.get(key, -1.0) if key is not None else 0.0
        return int(cvttss2si(float32(queried)) == signed_low_i16(arguments[2]))
    if opcode == 0x13C4 and len(arguments) == 1:
        return cvttss2si(state.opponent_previous_health)
    raise StaticResolutionError(
        f"movement helper reached unresolved IF 0x{opcode:04X} args={arguments!r}"
    )


def required_character_state_slots(bank: KhdFile, packed_helper: int) -> tuple[int, ...]:
    """Return concrete IF 0x0022 state-array indices read by one helper."""
    slot_index = bank.resolve_packed_slot(packed_helper)
    if slot_index is None or bank.slots[slot_index].bytecode is None:
        raise StaticResolutionError(
            f"cannot resolve movement helper 0x{packed_helper:04X}"
        )
    extracted = emulate(bank.slots[slot_index].bytecode, slot_index)
    indices: set[int] = set()
    for predicate in extracted.predicates:
        if len(predicate.args) < 3:
            continue
        values = [arg.value if isinstance(arg, Concrete) else None for arg in predicate.args]
        if values[0] == 0x0022 and values[1] is not None:
            indices.add(signed_low_i16(values[1]))
    return tuple(sorted(indices))


def execute_bank_slot_script(
    vm: MoveVMReference,
    bank: KhdFile,
    parent_context: MoveVMContext,
    state: MovementHelperState,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """Exact CALLCOND 0x0D local-frame and active-move transaction."""
    if not arguments:
        raise StaticResolutionError("ExecuteBankSlotScript missing packed move ID")
    if len(arguments) - 1 > 16:
        raise StaticResolutionError(
            f"nested script supplied {len(arguments) - 1} locals; native frame has 16"
        )
    packed_move_id = arguments[0] & 0xFFFF
    slot_index = bank.resolve_packed_slot(packed_move_id)
    if slot_index is None:
        raise StaticResolutionError(
            f"nested packed move 0x{packed_move_id:04X} does not resolve"
        )
    script = bank.slots[slot_index].bytecode
    saved_active_move = state.active_move_id
    state.active_move_id = packed_move_id
    try:
        if script is None:
            return CallCondResult(0)
        locals_frame = list(arguments[1:]) + [0] * (16 - (len(arguments) - 1))
        nested_context = MoveVMContext(
            globals=parent_context.globals,
            locals=locals_frame,
            handlers=parent_context.handlers,
            coverage=parent_context.coverage,
        )
        result = vm.execute(script, nested_context)
        parent_context.call_log.extend(nested_context.call_log)
        parent_context.coverage.resolved_functions.add(
            "LuxMoveVM_ExecuteBankSlotScript@0x1402FCC30"
        )
        return CallCondResult(result.return_value, result.broke)
    finally:
        state.active_move_id = saved_active_move


def execute_secondary_lane_script(
    vm: MoveVMReference,
    bank: KhdFile,
    parent_context: MoveVMContext,
    lifecycle: MoveVMCharacterLifecycleState,
    lane: MoveVMLaneSchedulerState,
) -> None:
    """Run the current lane's resolved +0x38 entry script with zero locals."""

    del lifecycle
    move_id = signed_low_i16(lane.current_move_id)
    if move_id < 0 or move_id >= len(bank.slots):
        raise StaticResolutionError(
            f"secondary lane move ID {move_id} does not resolve in the active bank"
        )
    script = bank.slots[move_id].bytecode
    if script is None:
        return
    nested_context = MoveVMContext(
        globals=parent_context.globals,
        locals=[0] * 16,
        handlers=parent_context.handlers,
        coverage=parent_context.coverage,
    )
    vm.execute(script, nested_context)
    parent_context.call_log.extend(nested_context.call_log)
    parent_context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_RunSecondaryLaneScript@0x1402FE1C0",
            "LuxMoveVM_ResolveBankSlot@0x1402FC400",
            "LuxMoveVM_RunBytecodeScript@0x1402E67B0",
        }
    )


def execute_velocity_helper(
    bank: KhdFile,
    packed_helper: int,
    *,
    direction_selector: int,
    move_table_index: int,
    stat_fields: dict[int, float] | None = None,
    frame: float = 3.0,
    chara_state_shorts: dict[int, int] | None = None,
    opponent_relative_angle_turns: float = 0.0,
    opponent_previous_health: float = 0.0,
) -> MovementHelperState:
    slot_index = bank.resolve_packed_slot(packed_helper)
    if slot_index is None or bank.slots[slot_index].bytecode is None:
        raise StaticResolutionError(
            f"cannot resolve movement helper 0x{packed_helper:04X}"
        )
    state = MovementHelperState(
        move_table_index=move_table_index,
        stat_fields=dict(stat_fields or {}),
        chara_state_shorts=dict(chara_state_shorts or {}),
        opponent_relative_angle_turns=opponent_relative_angle_turns,
        opponent_previous_health=opponent_previous_health,
        timing=LaneTimingState(current_frame=frame),
    )

    vm = MoveVMReference()

    def if_handler(context: MoveVMContext, arguments: tuple[int, ...]) -> CallCondResult:
        return CallCondResult(evaluate_if(state, arguments))

    def timing_handler(context: MoveVMContext, arguments: tuple[int, ...]) -> CallCondResult:
        return CallCondResult(evaluate_timing(state.timing, arguments))

    def effect_handler(context: MoveVMContext, arguments: tuple[int, ...]) -> CallCondResult:
        if not arguments:
            raise StaticResolutionError("DispatchEffectOp missing opcode")
        opcode = arguments[0] & 0xFFFF
        state.effects.append(EffectCommand(opcode, arguments))
        if opcode == 0x0004 and len(arguments) == 3:
            state.effect_angle_word = signed_low_i16(arguments[1])
            state.effect_speed_word = signed_low_i16(arguments[2])
        elif opcode == 0x0006 and len(arguments) == 1:
            state.effect_angle_word = 0
            state.effect_speed_word = 0
        else:
            raise StaticResolutionError(
                f"movement helper reached unresolved effect 0x{opcode:04X} args={arguments!r}"
            )
        return CallCondResult(0)

    def nested_handler(context: MoveVMContext, arguments: tuple[int, ...]) -> CallCondResult:
        return execute_bank_slot_script(vm, bank, context, state, arguments)

    handlers = {
        0x01: if_handler,
        0x03: effect_handler,
        0x0D: nested_handler,
        0x25: timing_handler,
    }
    context = MoveVMContext(
        locals=[direction_selector] + [0] * 15,
        handlers=handlers,
    )
    vm.execute(bank.slots[slot_index].bytecode, context)
    return state
