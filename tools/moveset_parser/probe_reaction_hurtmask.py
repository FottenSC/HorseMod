"""Fail-explicit static probe for reaction-script hurt-sphere masks.

This is an RE utility, not the combo classifier.  It executes shipped MoveVM
bytecode with the exact integer VM and reports every effect 0x13AC write.
Runtime-dependent IF families must be supplied explicitly on the command line;
unrelated effect operations are logged because they cannot alter VM control
flow synchronously, but are not promoted to complete static coverage.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path

from lux_effect_dispatch_subset import (
    LuxEffectDispatch02State,
    dispatch_effect_02,
    dispatch_effect_03_hurt_mask,
)
from lux_input_history import (
    InputHistoryEntry,
    check_history_condition,
    check_motion_condition_flags,
)
from lux_movement_vm import (
    LaneTimingState,
    MovementHelperState,
    evaluate_if,
    evaluate_timing,
    execute_bank_slot_script,
    map_timing_index,
)
from lux_numeric import cvttss2si, signed_low_i16
from lux_reference_engine import CallCondResult, MoveVMContext, MoveVMReference, StaticResolutionError
from lux_transition_author import (
    MoveVMLaneSchedulerState,
    MoveVMTransitionAuthorState,
    author_lane0_transition_06,
    author_lane1_transition_07,
)
from luxformats import parse_khd


@dataclass
class ReactionProbeState:
    """Persistent state shared by nested and repeated reaction scripts."""

    motion_state_latches: list[int]
    previous_motion_state_latches: list[int]
    opponent_motion_state_latches: list[int]
    chara_state_shorts: dict[int, int]
    opponent_chara_state_shorts: dict[int, int]
    opponent_previous_chara_state_shorts: dict[int, int] | None = None
    lane_move_ids: tuple[int, int, int] = (0xFFFF, 0xFFFF, 0xFFFF)
    active_lane_index: int = 1
    opponent_active_attack_flags: int = 0
    opponent_copied_cell_word_3a: int = 0
    state_260: int = 0
    mirrored_damage_pool_group: int = 0xFFFF
    transition_entry_matched: int = 0
    player_slot: int = 1
    round_result_winner_slot_low_byte: int = 0xFF
    result_vital_gate_mode: int = 0
    world_master_mode: int = 0
    battle_simulation_mode: int = 0
    terrain_tags_by_selector: dict[int, int] | None = None
    transition_condition_mode0: dict[int, int] | None = None
    pending_round_result: int = 0
    round_result_delay_gate: int = 0
    last_round_result: int = 0
    candidate_vital_scaled: float = 240.0
    adjusted_opponent_distance_xz: float = 0.75
    opponent_facing_delta_turns: float = 0.0
    ko_vital_threshold: float = 0.0
    meter_slots: dict[int, int] | None = None
    meter_slot_10_override_600: int = 0
    meter_slot_10_override_zero: int = 0
    vm_pump_enabled: int = 1
    state_248: int = 0
    state_324: int = 0
    deferred_transition_target_move_id: int = 0xFFFF
    deferred_transition_commit_flag: int = 0
    body_box_active_mask: int = 0x7FFFFF
    effect_commands: list[tuple[int, tuple[int, ...]]] = field(default_factory=list)
    callcond_commands: list[tuple[int, tuple[int, ...]]] = field(default_factory=list)
    transition_author: MoveVMTransitionAuthorState | None = None
    opponent_history_b: list[int] = field(default_factory=lambda: [0] * 0x72)
    current_history_b: list[int] = field(default_factory=lambda: [0] * 0x72)
    input_primary_word_2150: int = 0
    input_secondary_word_2158: int = 0
    side_decoded_input_id_2170: int = 0
    side_direction_mask_2178: int = 0
    decoded_high_input_id_215c: int = 0
    high_input_nibble_2164: int = 0
    side_decoded_secondary_input_id_217c: int = 0
    side_secondary_direction_mask_2180: int = 0
    decoded_secondary_high_input_id_2168: int = 0
    secondary_high_input_nibble_216c: int = 0
    state_1e60: int = 0
    state_1e68: int = 0
    state_404: int = 0
    primary_script_running: int = 0
    secondary_script_running: int = 0
    animation_ended: int = 0

    @classmethod
    def reset(cls) -> "ReactionProbeState":
        state = cls(
            [0] * 0x100,
            [0] * 0x100,
            [0] * 0x100,
            {index: 0 for index in range(0x80)},
            {index: 0 for index in range(0x80)},
        )
        state.meter_slots = {index: 0 for index in range(0x40)}
        state.opponent_previous_chara_state_shorts = {
            index: 0 for index in range(0x4A)
        }
        state.terrain_tags_by_selector = {}
        state.transition_condition_mode0 = {}
        state.transition_author = MoveVMTransitionAuthorState(
            lanes=tuple(MoveVMLaneSchedulerState(lane_index=index) for index in range(3)),
            active_lane_index=1,
        )
        return state


def execute_probe(
    bank,
    packed_helper: int,
    *,
    locals_: tuple[int, ...],
    chara_id: int,
    move_table_index: int,
    frame: float,
    overrides: dict[int, int],
    globals_: list[int] | None = None,
    effect_state: LuxEffectDispatch02State | None = None,
    reaction_state: ReactionProbeState | None = None,
) -> tuple[list[int], list[int], list[int]]:
    slot = bank.resolve_packed_slot(packed_helper)
    if slot is None or bank.slots[slot].bytecode is None:
        raise StaticResolutionError(f"packed helper 0x{packed_helper:04X} does not resolve")
    vm = MoveVMReference()
    reaction_state = reaction_state or ReactionProbeState.reset()
    movement = MovementHelperState(
        move_table_index=move_table_index,
        stat_fields={0x3F: 0.0, 0x40: 0.0},
        chara_state_shorts=reaction_state.chara_state_shorts,
        timing=LaneTimingState(
            current_frame=frame,
            timing_frame_10=0.0,
            timing_frame_14=0.0,
            motion_start_frame=0.0,
            motion_end_frame=60.0,
        ),
    )
    transition = reaction_state.transition_author
    if transition is None:
        raise StaticResolutionError("reaction probe requires transition-author state")
    transition.active_lane_index = reaction_state.active_lane_index
    for index, move_id in enumerate(reaction_state.lane_move_ids):
        lane = transition.lane(index)
        lane.current_move_id = move_id & 0xFFFF
        if index == reaction_state.active_lane_index:
            lane.animation_frame_current = frame
            lane.animation_end_status = int(reaction_state.animation_ended)
    masks: list[int] = []
    ignored_effects: list[int] = []
    queried_predicates: list[int] = []
    effect_state = effect_state or LuxEffectDispatch02State()

    def if_handler(_context, arguments):
        if not arguments:
            raise StaticResolutionError("empty IF stream")
        opcode = arguments[0] & 0xFFFF
        queried_predicates.append(opcode)
        if opcode == 0x0011:
            if len(arguments) < 2:
                raise StaticResolutionError("IF 0x0011 requires character ID")
            return CallCondResult(int(chara_id == signed_low_i16(arguments[1])))
        if opcode == 0x0005 and len(arguments) == 2:
            return CallCondResult(
                int(
                    check_motion_condition_flags(
                        arguments[1], reaction_state.input_primary_word_2150
                    )
                )
            )
        if opcode == 0x0006 and len(arguments) == 2:
            return CallCondResult(
                int(
                    check_motion_condition_flags(
                        arguments[1], reaction_state.input_secondary_word_2158
                    )
                )
            )
        if opcode == 0x0015:
            if len(arguments) < 2:
                raise StaticResolutionError("IF 0x0015 requires a battle-simulation mode")
            return CallCondResult(
                int(reaction_state.battle_simulation_mode == signed_low_i16(arguments[1]))
            )
        if opcode == 0x0012:
            other_lane_index = 1 if reaction_state.active_lane_index == 0 else 0
            return CallCondResult(
                int(reaction_state.lane_move_ids[other_lane_index] != 0xFFFF)
            )
        if opcode == 0x0014 and len(arguments) >= 3:
            distance = reaction_state.adjusted_opponent_distance_xz
            lower = arguments[1] & 0xFFFF
            upper = arguments[2] & 0xFFFF
            if lower != 0x7FFF and distance < signed_low_i16(lower) / 1000.0:
                return CallCondResult(0)
            if upper != 0x7FFF and signed_low_i16(upper) / 1000.0 < distance:
                return CallCondResult(0)
            return CallCondResult(1)
        if opcode == 0x0042 and len(arguments) >= 3:
            facing_movement = MovementHelperState(
                move_table_index=move_table_index,
                opponent_relative_angle_turns=reaction_state.opponent_facing_delta_turns,
            )
            return CallCondResult(
                evaluate_if(facing_movement, (0x0013, arguments[1], arguments[2]))
            )
        if opcode == 0x0016:
            winner = reaction_state.round_result_winner_slot_low_byte & 0xFF
            winner = winner - 0x100 if winner & 0x80 else winner
            return CallCondResult(int(winner == (reaction_state.player_slot & 0xFF)))
        if opcode == 0x0019:
            if reaction_state.result_vital_gate_mode == 1:
                if reaction_state.world_master_mode in (6, 7):
                    return CallCondResult(0)
                return CallCondResult(
                    int(
                        reaction_state.candidate_vital_scaled < 0.0
                        or reaction_state.pending_round_result != 0
                    )
                )
            return CallCondResult(
                int(
                    reaction_state.pending_round_result != 0
                    or reaction_state.last_round_result == 8
                )
            )
        if opcode == 0x0009:
            return CallCondResult(int(reaction_state.animation_ended))
        if opcode == 0x0010:
            return CallCondResult(int(reaction_state.primary_script_running))
        if opcode == 0x001F:
            return CallCondResult(reaction_state.transition_entry_matched & 0xFFFF)
        if opcode == 0x0054:
            return CallCondResult(int(reaction_state.secondary_script_running))
        if opcode == 0x005F and len(arguments) >= 2:
            index = signed_low_i16(arguments[1])
            if not 0 <= index < len(reaction_state.previous_motion_state_latches):
                raise StaticResolutionError(
                    f"IF 0x005F previous-latch index {index} is outside the native probe bank"
                )
            return CallCondResult(reaction_state.previous_motion_state_latches[index] & 0xFF)
        if opcode == 0x0023 and len(arguments) >= 3:
            index = signed_low_i16(arguments[1])
            if index not in reaction_state.opponent_chara_state_shorts:
                raise StaticResolutionError(
                    f"IF 0x0023 requires unresolved opponent character-state slot {index}"
                )
            actual = reaction_state.opponent_chara_state_shorts[index] & 0xFFFF
            return CallCondResult(int(actual == signed_low_i16(arguments[2])))
        if opcode == 0x0098 and len(arguments) >= 3:
            index = signed_low_i16(arguments[1])
            previous = reaction_state.opponent_previous_chara_state_shorts or {}
            if index not in previous:
                raise StaticResolutionError(
                    f"IF 0x0098 requires unresolved opponent previous-state slot {index}"
                )
            actual = previous[index] & 0xFFFF
            return CallCondResult(int(actual == signed_low_i16(arguments[2])))
        if opcode == 0x0033 and len(arguments) >= 2:
            return CallCondResult(
                reaction_state.opponent_active_attack_flags & (arguments[1] & 0xFFFF)
            )
        if opcode == 0x003E:
            return CallCondResult(
                int(reaction_state.state_248 == 2 or reaction_state.state_324 == 2)
            )
        if opcode == 0x000F and len(arguments) >= 2:
            return CallCondResult(
                int(reaction_state.active_lane_index == ((arguments[1] & 0xFFFF) >> 1))
            )
        if opcode == 0x0041:
            lane = transition.active_lane
            return CallCondResult(
                int(
                    lane.deferred_transition_target_move_id != 0xFFFF
                    or lane.queued_target_move_id != 0xFFFF
                )
            )
        if opcode == 0x0068 and len(arguments) >= 2:
            index = signed_low_i16(arguments[1])
            if not 0 <= index < len(reaction_state.opponent_history_b):
                raise StaticResolutionError(
                    f"IF 0x0068 opponent-history index {index} is outside the bank"
                )
            return CallCondResult(int(reaction_state.opponent_history_b[index] == 1))
        if opcode == 0x0067 and len(arguments) >= 2:
            index = signed_low_i16(arguments[1])
            if not 0 <= index < len(reaction_state.current_history_b):
                raise StaticResolutionError(
                    f"IF 0x0067 current-history index {index} is outside the bank"
                )
            return CallCondResult(int(reaction_state.current_history_b[index] == 0xFF))
        if opcode == 0x008F:
            if len(arguments) == 1:
                lane_mask = 1 << reaction_state.active_lane_index
                target = 0xFFFF
            elif len(arguments) == 2:
                lane_mask = signed_low_i16(arguments[1]) & 0xFFFFFFFF
                target = 0xFFFF
            else:
                lane_mask = signed_low_i16(arguments[1]) & 0xFFFFFFFF
                target = arguments[2] & 0xFFFF
            for lane in transition.lanes:
                if not (lane_mask & (1 << lane.lane_index)):
                    continue
                queued = lane.queued_target_move_id
                if (
                    lane.current_move_id != 0xFFFF
                    and queued != 0xFFFF
                    and (target == 0xFFFF or queued == target)
                ):
                    return CallCondResult(1)
            return CallCondResult(0)
        if opcode == 0x002D and len(arguments) >= 2:
            selector = arguments[1] & 0xFFFF
            if selector == 0:
                value = int(bool(reaction_state.input_primary_word_2150 & 0x8))
                reaction_state.motion_state_latches[1] = value
                return CallCondResult(value)
            if selector < 0x20:
                return CallCondResult(0)
            return CallCondResult(0)
        if opcode in (0x0024, 0x0025):
            secondary = opcode == 0x0025
            entry = InputHistoryEntry(
                current_compact_word=(
                    reaction_state.input_secondary_word_2158
                    if secondary
                    else reaction_state.input_primary_word_2150
                )
                & 0xFFFF,
                secondary_compact_word=0,
                side_decoded_input_id=(
                    reaction_state.side_decoded_secondary_input_id_217c
                    if secondary
                    else reaction_state.side_decoded_input_id_2170
                )
                & 0xFFFF,
                side_direction_mask=(
                    reaction_state.side_secondary_direction_mask_2180
                    if secondary
                    else reaction_state.side_direction_mask_2178
                )
                & 0xFFFF,
                decoded_high_nibble_input_id=(
                    reaction_state.decoded_secondary_high_input_id_2168
                    if secondary
                    else reaction_state.decoded_high_input_id_215c
                )
                & 0xFFFF,
                high_input_nibble=(
                    reaction_state.secondary_high_input_nibble_216c
                    if secondary
                    else reaction_state.high_input_nibble_2164
                )
                & 0xFFFF,
            )
            return CallCondResult(
                int(all(check_history_condition(entry, clause) for clause in arguments[1:]))
            )
        if opcode == 0x0040:
            return CallCondResult(
                int(not reaction_state.candidate_vital_scaled < reaction_state.ko_vital_threshold)
            )
        if opcode == 0x0BBD and len(arguments) == 1:
            return CallCondResult(reaction_state.opponent_copied_cell_word_3a & 0xFFFF)
        if opcode == 0x000B and len(arguments) >= 2:
            index = signed_low_i16(arguments[1])
            if not 0 <= index < len(reaction_state.motion_state_latches):
                raise StaticResolutionError(
                    f"IF 0x000B latch index {index} is outside the native probe bank"
                )
            return CallCondResult(reaction_state.motion_state_latches[index] & 0xFF)
        if opcode == 0x000C and len(arguments) >= 2:
            index = signed_low_i16(arguments[1])
            if not 0 <= index < len(reaction_state.motion_state_latches):
                raise StaticResolutionError(
                    f"IF 0x000C latch index {index} is outside the native probe bank"
                )
            return CallCondResult(int(reaction_state.motion_state_latches[index] == 0))
        if opcode in (0x001C, 0x001D) and len(arguments) >= 2:
            index = signed_low_i16(arguments[1])
            if not 0 <= index < len(reaction_state.opponent_motion_state_latches):
                raise StaticResolutionError(
                    f"IF 0x{opcode:04X} opponent-latch index {index} is outside the bank"
                )
            value = reaction_state.opponent_motion_state_latches[index] & 0xFF
            return CallCondResult(value if opcode == 0x001C else int(value == 0))
        if opcode == 0x000E and len(arguments) >= 2:
            active_move_id = reaction_state.lane_move_ids[reaction_state.active_lane_index]
            return CallCondResult(int(active_move_id == (arguments[1] & 0xFFFF)))
        if opcode == 0x139B and len(arguments) == 2:
            return CallCondResult(
                cvttss2si(map_timing_index(movement.timing, arguments[1]))
            )
        if opcode == 0x139A and len(arguments) == 1:
            return CallCondResult(cvttss2si(movement.timing.current_frame))
        if opcode == 0x13B3 and len(arguments) == 2:
            selector = signed_low_i16(arguments[1])
            if not 0 <= selector < len(reaction_state.lane_move_ids):
                return CallCondResult(0)
            return CallCondResult(reaction_state.lane_move_ids[selector] & 0xFFFF)
        if opcode == 0x13AB and len(arguments) == 1:
            return CallCondResult(
                int(reaction_state.vm_pump_enabled or reaction_state.state_324 == 0x69)
            )
        if opcode == 0x13C7 and len(arguments) >= 2:
            selector = arguments[1] & 0xFFFF
            result = reaction_state.pending_round_result
            if selector == 1:
                return CallCondResult(int(result in (1, 2, 8)))
            if selector == 3:
                return CallCondResult(int(result in (3, 9)))
            if selector == 4:
                return CallCondResult(int(result in (4, 10)))
            return CallCondResult(int(result == selector))
        if opcode == 0x13CC and len(arguments) >= 2:
            selector = arguments[1] & 0xFFFF
            values = {
                0: reaction_state.state_1e68,
                1: reaction_state.state_1e60,
                2: reaction_state.state_404,
            }
            return CallCondResult(values.get(selector, 0))
        if opcode == 0x13DC and len(arguments) == 1:
            return CallCondResult(int(reaction_state.round_result_delay_gate != 0))
        if opcode == 0x0075 and len(arguments) >= 2:
            return CallCondResult(
                int(reaction_state.state_260 == signed_low_i16(arguments[1]))
            )
        if opcode == 0x07D2 and len(arguments) >= 2:
            selector = arguments[1] & 0xFFFF
            if not 1 <= selector <= 0xA6:
                selector = 1
            terrain_tags = reaction_state.terrain_tags_by_selector or {}
            return CallCondResult(int(terrain_tags.get(selector, 0) != 0x3A))
        if opcode == 0x002C and len(arguments) >= 2:
            condition_index = arguments[1] & 0xFFFF
            conditions = reaction_state.transition_condition_mode0 or {}
            return CallCondResult(int(bool(conditions.get(condition_index, 0))))
        if opcode == 0x138C and len(arguments) == 1:
            raw_group = reaction_state.mirrored_damage_pool_group & 0xFFFF
            if raw_group == 0xFFFF:
                return CallCondResult(0xFFFFFFFF)
            group = raw_group & 0x7FF
            return CallCondResult((group & 0xF) if group < 0x40 else 0xFFFFFFFF)
        if opcode == 0x138A and len(arguments) >= 4:
            selector = signed_low_i16(arguments[1])
            if reaction_state.meter_slots is None or selector not in reaction_state.meter_slots:
                raise StaticResolutionError(
                    f"IF 0x138A requires unresolved meter slot {selector}"
                )
            value = signed_low_i16(reaction_state.meter_slots[selector])
            if selector == 10 and value > 0:
                if reaction_state.meter_slot_10_override_zero:
                    value = 0
                elif reaction_state.meter_slot_10_override_600:
                    value = 600
            return CallCondResult(
                int(
                    signed_low_i16(arguments[2])
                    <= value
                    <= signed_low_i16(arguments[3])
                )
            )
        if opcode in overrides:
            return CallCondResult(int(overrides[opcode]))
        return CallCondResult(evaluate_if(movement, arguments))

    def timing_handler(_context, arguments):
        return CallCondResult(evaluate_timing(movement.timing, arguments))

    def effect_handler(context, arguments):
        opcode = (arguments[0] & 0xFFFF) if arguments else -1
        reaction_state.effect_commands.append((opcode, tuple(arguments)))
        if opcode == 0x13AC:
            dispatch_effect_03_hurt_mask(effect_state, context, arguments)
            masks.append(effect_state.hurt_sphere_disable_mask)
        elif opcode in (0x0004, 0x0006, 0x000E):
            dispatch_effect_02(effect_state, context, arguments)
        elif opcode == 0x0027:
            if len(arguments) == 1:
                reaction_state.body_box_active_mask = 0
            elif len(arguments) == 2:
                reaction_state.body_box_active_mask = signed_low_i16(arguments[1])
            else:
                reaction_state.body_box_active_mask = (
                    (signed_low_i16(arguments[2]) << 15)
                    | (arguments[1] & 0x7FFF)
                )
        else:
            ignored_effects.append(opcode)
        return CallCondResult(0)

    def bounded_effect_handler(context, arguments):
        opcode = (arguments[0] & 0xFFFF) if arguments else -1
        reaction_state.effect_commands.append((opcode, tuple(arguments)))
        return dispatch_effect_02(effect_state, context, arguments)

    def nested_handler(context, arguments):
        return execute_bank_slot_script(vm, bank, context, movement, arguments)

    def set_motion_latch_handler(_context, arguments):
        if not arguments:
            raise StaticResolutionError("CALLCOND 0x09 requires a latch index")
        index = signed_low_i16(arguments[0])
        if not 0 <= index < len(reaction_state.motion_state_latches):
            raise StaticResolutionError(f"motion latch index {index} is outside the bank")
        reaction_state.motion_state_latches[index] = 1
        return CallCondResult(0)

    def clear_motion_latch_handler(_context, arguments):
        if not arguments:
            raise StaticResolutionError("CALLCOND 0x0A requires a latch index")
        index = signed_low_i16(arguments[0])
        if not 0 <= index < len(reaction_state.motion_state_latches):
            raise StaticResolutionError(f"motion latch index {index} is outside the bank")
        reaction_state.motion_state_latches[index] = 0
        return CallCondResult(0)

    def write_chara_state_handler(_context, arguments):
        if len(arguments) != 2:
            raise StaticResolutionError("CALLCOND 0x14 requires state index and value")
        index = signed_low_i16(arguments[0])
        if not 0 <= index < 0x80:
            raise StaticResolutionError(f"character-state index {index} is outside the bank")
        reaction_state.chara_state_shorts[index] = signed_low_i16(arguments[1])
        return CallCondResult(0)

    def state_side_effect_handler(_context, _arguments):
        return CallCondResult(0)

    def transition_lane0_handler(_context, arguments):
        reaction_state.callcond_commands.append((0x06, tuple(arguments)))
        return CallCondResult(author_lane0_transition_06(transition, arguments))

    def transition_lane1_handler(_context, arguments):
        reaction_state.callcond_commands.append((0x07, tuple(arguments)))
        return CallCondResult(author_lane1_transition_07(transition, arguments))

    def clear_pending_transition_handler(_context, _arguments):
        reaction_state.deferred_transition_target_move_id = 0xFFFF
        reaction_state.deferred_transition_commit_flag = 0
        return CallCondResult(1)

    handlers = {
        0x01: if_handler,
        0x02: bounded_effect_handler,
        0x03: effect_handler,
        0x05: state_side_effect_handler,
        0x06: transition_lane0_handler,
        0x07: transition_lane1_handler,
        0x09: set_motion_latch_handler,
        0x0A: clear_motion_latch_handler,
        0x0C: state_side_effect_handler,
        0x0D: nested_handler,
        0x14: write_chara_state_handler,
        0x15: state_side_effect_handler,
        0x1A: clear_pending_transition_handler,
        0x25: timing_handler,
    }
    context = MoveVMContext(
        globals=globals_ if globals_ is not None else [0] * 0xF0,
        locals=list(locals_) + [0] * (16 - len(locals_)),
        handlers=handlers,
    )
    vm.execute(bank.slots[slot].bytecode, context)
    return masks, ignored_effects, queried_predicates


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("khd", type=Path)
    parser.add_argument("helper", type=lambda value: int(value, 0))
    parser.add_argument("--local", action="append", default=[], type=lambda value: int(value, 0))
    parser.add_argument("--chara-id", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--move-table-index", default=0, type=lambda value: int(value, 0))
    parser.add_argument("--frame", default=0.0, type=float)
    parser.add_argument("--primary-script-running", default=0, type=int)
    parser.add_argument("--secondary-script-running", default=0, type=int)
    parser.add_argument("--animation-ended", default=0, type=int)
    parser.add_argument("--lane-move-id", action="append", default=[], type=lambda value: int(value, 0))
    parser.add_argument("--active-lane-index", default=1, type=int)
    parser.add_argument("--opponent-attack-flags", default=0, type=lambda value: int(value, 0))
    parser.add_argument("--opponent-cell-word-3a", default=0, type=lambda value: int(value, 0))
    parser.add_argument("--input-primary-word", default=0, type=lambda value: int(value, 0))
    parser.add_argument("--input-secondary-word", default=0, type=lambda value: int(value, 0))
    parser.add_argument("--side-decoded-input-id", default=0, type=lambda value: int(value, 0))
    parser.add_argument("--side-direction-mask", default=0, type=lambda value: int(value, 0))
    parser.add_argument("--timeline-end", type=int)
    parser.add_argument("--animation-length", type=int)
    parser.add_argument("--if", dest="predicates", action="append", default=[])
    args = parser.parse_args()
    overrides = {
        int(item.split("=", 1)[0], 0): int(item.split("=", 1)[1], 0)
        for item in args.predicates
    }
    bank = parse_khd(args.khd.read_bytes())
    reaction_state = ReactionProbeState.reset()
    reaction_state.primary_script_running = int(bool(args.primary_script_running))
    reaction_state.secondary_script_running = int(bool(args.secondary_script_running))
    reaction_state.animation_ended = int(bool(args.animation_ended))
    reaction_state.active_lane_index = args.active_lane_index
    if args.lane_move_id:
        if len(args.lane_move_id) != 3:
            parser.error("--lane-move-id must be supplied exactly three times")
        reaction_state.lane_move_ids = tuple(value & 0xFFFF for value in args.lane_move_id)
    reaction_state.opponent_active_attack_flags = args.opponent_attack_flags & 0xFFFF
    reaction_state.opponent_copied_cell_word_3a = args.opponent_cell_word_3a & 0xFFFF
    reaction_state.input_primary_word_2150 = args.input_primary_word & 0xFFFF
    reaction_state.input_secondary_word_2158 = args.input_secondary_word & 0xFFFF
    reaction_state.side_decoded_input_id_2170 = args.side_decoded_input_id & 0xFFFF
    reaction_state.side_direction_mask_2178 = args.side_direction_mask & 0xFFFF
    effect_state = LuxEffectDispatch02State()
    def run_at(frame: float) -> tuple[list[int], list[int], list[int], list[tuple[int, tuple[int, ...]]]]:
        effect_start = len(reaction_state.effect_commands)
        result = execute_probe(
            bank,
            args.helper,
            locals_=tuple(args.local),
            chara_id=args.chara_id,
            move_table_index=args.move_table_index,
            frame=frame,
            overrides=overrides,
            effect_state=effect_state,
            reaction_state=reaction_state,
        )
        return (*result, reaction_state.effect_commands[effect_start:])

    if args.timeline_end is not None:
        if args.animation_length is None:
            parser.error("--timeline-end requires --animation-length")
        reaction_state.primary_script_running = 1
        masks, ignored, predicates, commands = run_at(0.0)
        print(
            "tick entry:",
            f"masks={','.join(f'0x{mask:06X}' for mask in masks) or '-'}",
            f"body=0x{reaction_state.body_box_active_mask & 0x7FFFFF:06X}",
            f"velocity=({effect_state.effect_velocity_x},{effect_state.effect_velocity_y},{effect_state.effect_velocity_z})",
            f"effects={','.join(f'0x{opcode:04X}' for opcode, _words in commands) or '-'}",
        )
        reaction_state.primary_script_running = 0
        for tick in range(1, args.timeline_end + 1):
            reaction_state.animation_ended = int(tick > args.animation_length)
            masks, ignored, predicates, commands = run_at(float(tick))
            if masks or commands:
                queued = [
                    f"L{lane.lane_index}=0x{lane.queued_target_move_id:04X}"
                    for lane in reaction_state.transition_author.lanes
                    if lane.queued_target_move_id != 0xFFFF
                ]
                print(
                    f"tick {tick}:",
                    f"masks={','.join(f'0x{mask:06X}' for mask in masks) or '-'}",
                    f"body=0x{reaction_state.body_box_active_mask & 0x7FFFFF:06X}",
                    f"velocity=({effect_state.effect_velocity_x},{effect_state.effect_velocity_y},{effect_state.effect_velocity_z})",
                    f"effects={','.join(f'0x{opcode:04X}' for opcode, _words in commands) or '-'}",
                    f"queued={','.join(queued) or '-'}",
                )
            reaction_state.previous_motion_state_latches[:] = (
                reaction_state.motion_state_latches
            )
        return 0

    masks, ignored, predicates, _commands = run_at(args.frame)
    print("mask writes:", " ".join(f"0x{mask:06X}" for mask in masks) or "none")
    print("final mask:", f"0x{masks[-1]:06X}" if masks else "unchanged")
    print("predicates:", " ".join(f"0x{opcode:04X}" for opcode in predicates))
    print("ignored effects:", " ".join(f"0x{opcode:04X}" for opcode in sorted(set(ignored))))
    print("body mask:", f"0x{reaction_state.body_box_active_mask & 0x7FFFFF:06X}")
    print(
        "effect velocity:",
        effect_state.effect_velocity_x,
        effect_state.effect_velocity_y,
        effect_state.effect_velocity_z,
    )
    print(
        "effect commands:",
        " ".join(
            "[" + ",".join(f"0x{word & 0xFFFF:04X}" for word in words) + "]"
            for _opcode, words in reaction_state.effect_commands
        ),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
