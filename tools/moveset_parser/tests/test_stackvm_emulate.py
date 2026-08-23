"""Unit tests for the predicate decoder + virtual stack emulator.

These pin down the load-bearing decoder logic — especially the input
bit layout, which we've broken twice during this project.
"""
from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace

import pytest

from luxformats import parse_khd
from stackvm import StackVMInstruction, StackVMScript
from stackvm_emulate import (
    Concrete, VarRef, Unknown,
    PredicateEvent,
    _decode_lux_fp16_literal,
    _decode_button_mask,
    _decode_direction,
    _decode_input_mask,
    _decode_motion_pattern,
    _decode_direction_condition_mask,
    _c_div,
    _c_mod,
    decode_predicate,
    emulate,
)


def test_must_latched_motion_flags_requires_every_terminal_path():
    unconditional = StackVMScript(
        bytecode_offset=0,
        instructions=[
            StackVMInstruction(0, b"\x8b\x0e\x00", 0x0B, 0x0B, True, "SET_ACC_U16", imm_u16=0x0E),
            StackVMInstruction(3, b"\x25\x09\x01", 0x25, 0x25, False, "CALLCOND", imm_b0=0x09, imm_b1=1),
            StackVMInstruction(6, b"\x05", 0x05, 0x05, False, "RET"),
        ],
    )
    conditional = StackVMScript(
        bytecode_offset=0,
        instructions=[
            StackVMInstruction(0, b"\x8a\x01\x00", 0x0A, 0x0A, True, "LOAD_VAR", imm_u16=1),
            StackVMInstruction(3, b"\x28\x0d\x00", 0x28, 0x28, False, "JZ", imm_u16=13),
            StackVMInstruction(6, b"\x8b\x0e\x00", 0x0B, 0x0B, True, "SET_ACC_U16", imm_u16=0x0E),
            StackVMInstruction(9, b"\x25\x09\x01", 0x25, 0x25, False, "CALLCOND", imm_b0=0x09, imm_b1=1),
            StackVMInstruction(12, b"\x05", 0x05, 0x05, False, "RET"),
            StackVMInstruction(13, b"\x05", 0x05, 0x05, False, "RET"),
        ],
    )

    assert emulate(unconditional, 0).must_latched_motion_flags == frozenset({0x0E})
    assert emulate(unconditional, 0).may_latched_motion_flags == frozenset({0x0E})
    assert emulate(conditional, 0).must_latched_motion_flags == frozenset()
    assert emulate(conditional, 0).may_latched_motion_flags == frozenset({0x0E})


# ---------------------------------------------------------------------------
# Button mask decoding (A=0x01, B=0x02, K=0x04, G=0x08)
# ---------------------------------------------------------------------------

class TestButtonMaskDecode:
    def test_single_buttons(self):
        assert _decode_button_mask(0x01) == "A"
        assert _decode_button_mask(0x02) == "B"
        assert _decode_button_mask(0x04) == "K"
        assert _decode_button_mask(0x08) == "G"

    def test_button_combinations(self):
        assert _decode_button_mask(0x03) == "A+B"
        assert _decode_button_mask(0x05) == "A+K"
        assert _decode_button_mask(0x0F) == "A+B+K+G"

    def test_empty(self):
        assert _decode_button_mask(0x00) == ""
        # Bits outside the button range are ignored
        assert _decode_button_mask(0x10) == ""
        assert _decode_button_mask(0x100) == ""


# ---------------------------------------------------------------------------
# Direction decoding (numpad notation from bits 10-13)
# ---------------------------------------------------------------------------

class TestDirectionDecode:
    @pytest.mark.parametrize("mask,expected", [
        (0x0000, ""),         # neutral (numpad 5) renders as empty
        (0x0400, "4"),        # back
        (0x0800, "6"),        # forward
        (0x1000, "8"),        # up
        (0x2000, "2"),        # down
        (0x1400, "7"),        # up-back
        (0x1800, "9"),        # up-forward
        (0x2400, "1"),        # down-back
        (0x2800, "3"),        # down-forward
    ])
    def test_numpad_combos(self, mask, expected):
        assert _decode_direction(mask) == expected

    def test_button_bits_ignored(self):
        # Direction decoder shouldn't be affected by button bits
        assert _decode_direction(0x0800 | 0x0001) == "6"


# ---------------------------------------------------------------------------
# Full input-mask render (direction + buttons)
# ---------------------------------------------------------------------------

class TestInputMask:
    def test_a_press(self):
        assert _decode_input_mask(0x0001) == "A"

    def test_forward_a(self):
        assert _decode_input_mask(0x0800 | 0x0001) == "6A"

    def test_down_back_a(self):
        # numpad 1 + A = "1A" (low slash idiom)
        assert _decode_input_mask(0x0400 | 0x2000 | 0x0001) == "1A"

    def test_neutral_returns_5(self):
        # The pure-zero mask is the explicit neutral sentinel
        assert _decode_input_mask(0x0000) == "5"


# ---------------------------------------------------------------------------
# Motion pattern decoder
# ---------------------------------------------------------------------------

class TestMotionPattern:
    def test_neutral_sentinel(self):
        assert _decode_motion_pattern(0x8000) == "buttons:none"

    def test_any_sentinel(self):
        assert _decode_motion_pattern(0x8001) == "buttons:any"

    def test_always_true(self):
        assert _decode_motion_pattern(0x8002) == "input:*"

    def test_low_nibble_uses_abkg_layout(self):
        assert _decode_motion_pattern(0x0001) == "(any:A)"
        assert _decode_motion_pattern(0x0F00) == "(A+B+K+G)"

    def test_unresolved_bit_five_stays_explicit(self):
        assert _decode_motion_pattern(0x0020) == "(any:?bit5)"

    def test_side_direction_mask_has_distinct_layout(self):
        assert _decode_direction_condition_mask(0x01) == "8"
        assert _decode_direction_condition_mask(0x02) == "2"
        assert _decode_direction_condition_mask(0x04) == "6"
        assert _decode_direction_condition_mask(0x08) == "4"
        assert _decode_direction_condition_mask(0x09) == "8|4"


# ---------------------------------------------------------------------------
# Predicate decoder
# ---------------------------------------------------------------------------

def _pred(sub_op: int, *args: int) -> PredicateEvent:
    return PredicateEvent(
        callcond_idx=0x00,
        args=[Concrete(sub_op)] + [Concrete(a) for a in args],
        source_pc=0,
    )


def _pred_indirect(sub_op: int) -> PredicateEvent:
    return PredicateEvent(
        callcond_idx=0x00,
        args=[Concrete(sub_op), VarRef(0x101)],
        source_pc=0,
    )


class TestDecodePredicate:
    def test_unconditional(self):
        d = decode_predicate(None)
        assert d.kind == "always"
        assert d.text == "(unconditional)"

    def test_primary_direction_nibble_sub_op_1(self):
        d = decode_predicate(_pred(0x01, 0x0004))
        assert d.kind == "direction"
        assert d.text == "raw-dir:any(8)"

    def test_button_indirect(self):
        d = decode_predicate(_pred_indirect(0x01))
        # We can't know the mask, so this should be tagged indirect
        assert d.kind == "indirect"

    def test_primary_side_decoded_direction_sequence(self):
        d = decode_predicate(_pred(0x02, 0x0006, 3, 10))
        assert d.kind == "direction"
        assert d.text == "side-dir:4"

    def test_secondary_exact_direction_forms(self):
        assert decode_predicate(_pred(0x26, 0x0008)).text == "alt-raw-dir:2"
        assert decode_predicate(_pred(0x28, 0x0006)).text == "alt-side-dir:4"

    def test_secondary_multi_arg_direction_sources_are_labeled(self):
        d = decode_predicate(_pred(0x25, 0x3008, 0x2004))
        assert d.kind == "direction"
        assert d.text == "alt-dir:any(4)+alt-raw-dir:6"

    def test_multi_arg_raw_direction_nibble_4(self):
        d = decode_predicate(_pred(0x24, 0x4001))
        assert d.kind == "direction"
        assert d.text == "raw-dir:any(4)"

    def test_multi_arg_side_decoded_direction_nibble_1(self):
        d = decode_predicate(_pred(0x24, 0x1008))
        assert d.kind == "direction"
        assert d.text == "side-dir:2"

    def test_command_input(self):
        # sub-op 0x2C: command-input system (LuxBattle_EvaluateMoveTransitionConditions)
        d = decode_predicate(_pred(0x2C, 0x0001))
        assert d.kind == "command"
        assert "cmd:0x0001" in d.text

    def test_callcond_25_prepends_frame_window_subopcode(self):
        pred = PredicateEvent(
            callcond_idx=0x25,
            args=[Concrete(0x002C)],
            source_pc=0x1234,
        )

        assert pred.sub_opcode == 0x002C
        assert pred.effective_sub_opcode == 0x0008
        assert [value.value for value in pred.effective_args] == [0x0008, 0x002C]
        decoded = decode_predicate(pred)
        assert decoded.kind == "frame"
        assert decoded.text == "(frame window)"

    def test_motion_check(self):
        # Sub-op 5 scans compact-input history entry +0x00. Its low nibble
        # is A/B/K/G, not direction.
        d = decode_predicate(_pred(0x05, 0x8001))
        assert d.kind == "buttons"
        assert d.text == "buttons:any"

    def test_side_direction_mask_check(self):
        d = decode_predicate(_pred(0x03, 0x0008))
        assert d.kind == "direction"
        assert d.text == "dir:any(4)"

    def test_multi_arg_direction_sources_use_native_layouts(self):
        d = decode_predicate(_pred(0x24, 0x3004, 0x1006))
        assert d.kind == "direction"
        assert d.text == "dir:any(6)+side-dir:4"

    def test_frame_window(self):
        # sub-op 8: active-frame-window test
        d = decode_predicate(_pred(0x08, 5, 12))
        assert d.kind == "frame"
        assert "5" in d.text and "12" in d.text

    def test_orientation_window(self):
        # Native sub-op 0x13 compares the character's wrapped orientation
        # against two authored signed-degree bounds. It is not a frame gate.
        d = decode_predicate(_pred(0x13, -90 & 0xFFFF, 90))
        assert d.kind == "orientation"
        assert d.text == "orientation [-90\N{DEGREE SIGN}..90\N{DEGREE SIGN}]"

    def test_move_id_check(self):
        # sub-op 0x0E: current move id == arg
        d = decode_predicate(_pred(0x0E, 0x123))
        assert d.kind == "from-move"

    def test_move_id_indirect(self):
        d = decode_predicate(_pred_indirect(0x0E))
        assert d.kind == "from-move"
        assert "indirect" in d.text

    def test_unknown_sub_op_falls_through(self):
        # Any sub-op not in the known set returns "other" kind
        d = decode_predicate(_pred(0x99))
        assert d.kind == "other"


@pytest.mark.needs_dump
def test_voldo_back_ukemi_routes_head_end_to_e3_and_feet_end_to_e5():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr005.khd").read_bytes())
    transitions = emulate(bank.slots[0x76].bytecode, 0x76).transitions

    assert len(transitions) == 2
    head_end, feet_end = transitions
    assert head_end.next_move_slot == 0x81
    assert bank.slots[head_end.next_move_slot].wAnimationIndex_00 == 0x10E3
    assert head_end.predicate is not None
    assert [arg.value for arg in head_end.predicate.args if isinstance(arg, Concrete)] == [
        0x13,
        0xFFA6,
        0x005A,
    ]
    assert feet_end.next_move_slot == 0x7F
    assert bank.slots[feet_end.next_move_slot].wAnimationIndex_00 == 0x10E5
    assert feet_end.predicate is None


# ---------------------------------------------------------------------------
# Stack emulator — synthesised programs
# ---------------------------------------------------------------------------

def _push_imm(value: int, pc: int = 0) -> StackVMInstruction:
    # 0x09 + push-flag = "SET ACC then push" — the canonical "push imm" idiom
    return StackVMInstruction(
        pc=pc, raw=bytes([0x89, value >> 8, value & 0xFF]),
        opcode_byte=0x89, opcode=0x09, push_flag=True,
        mnemonic="SET_ACC_U16", imm_u16=value,
    )


def _callcond(fn_idx: int, argc: int, pc: int = 0) -> StackVMInstruction:
    return StackVMInstruction(
        pc=pc, raw=bytes([0x25, fn_idx, argc]),
        opcode_byte=0x25, opcode=0x25, push_flag=False,
        mnemonic="CALLCOND", imm_b0=fn_idx, imm_b1=argc,
    )


def _ret(pc: int = 0) -> StackVMInstruction:
    return StackVMInstruction(
        pc=pc, raw=bytes([0x05]),
        opcode_byte=0x05, opcode=0x05, push_flag=False, mnemonic="RET",
    )


class TestEmulator:
    def test_must_final_variables_retains_only_path_invariant_values(self):
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(14, pc=0),
            StackVMInstruction(
                3, b"\x19\x00\x19", 0x19, 0x19, False,
                "STORE_VAR", imm_u16=25,
            ),
            _ret(pc=6),
        ])

        result = emulate(script, slot_idx=7)

        assert result.must_final_variables == {25: 14}

    def test_chara_state_write_is_captured_as_state_mutation(self):
        # Native CALLCOND 0x14 writes args[1] to the defender character-state
        # short bank at args[0].  It is not a boolean predicate.
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(73, pc=0),
            _push_imm(0xFFFF, pc=3),
            _callcond(0x14, 2, pc=6),
            _ret(pc=9),
        ])

        result = emulate(script, slot_idx=7)

        assert result.predicates == []
        assert len(result.chara_state_writes) == 1
        assert result.chara_state_writes[0].source_pc == 6
        assert result.chara_state_writes[0].concrete_args == [73, 0xFFFF]

    def test_simple_transition(self):
        # Build a tiny bytecode: PUSH 0x123; PUSH 5; PUSH 12; CALLCOND 0x05 (TransitionAuthor), 3
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(0x123, pc=0),
            _push_imm(5, pc=3),
            _push_imm(12, pc=6),
            _callcond(0x05, 3, pc=9),
            _ret(pc=12),
        ])
        result = emulate(script, slot_idx=42)
        assert len(result.transitions) == 1
        t = result.transitions[0]
        assert t.next_move_id_raw == 0x123
        assert t.next_move_slot == 0x123 & 0x7FF
        assert t.next_move_bank == 0

    def test_transition_packed_slot_masks_bit_11(self):
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(0x1ABC, pc=0),
            _callcond(0x05, 1, pc=3),
            _ret(pc=6),
        ])
        result = emulate(script, slot_idx=7)
        assert len(result.transitions) == 1
        t = result.transitions[0]
        assert t.next_move_id_raw == 0x1ABC
        assert t.next_move_bank == 1
        assert t.next_move_slot == 0x2BC
        assert t.next_move_ignored_bit_11 is True

    def test_predicate_gates_transition(self):
        # PUSH sub_op=1; PUSH raw-direction bit 0x0001 (back);
        # CALLCOND 0x00 (EvalIf), 2
        # JZ skip
        # PUSH 0x456; CALLCOND 0x05 (TransitionAuthor), 1
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(1, pc=0),
            _push_imm(0x0001, pc=3),
            _callcond(0x00, 2, pc=6),
            _push_imm(0x456, pc=9),
            _callcond(0x05, 1, pc=12),
            _ret(pc=15),
        ])
        result = emulate(script, slot_idx=10)
        assert len(result.transitions) == 1
        t = result.transitions[0]
        assert t.next_move_id_raw == 0x456
        assert t.predicate is not None
        assert t.predicate.sub_opcode == 1
        # IF0001 tests the primary raw direction nibble at +0x2164.
        d = decode_predicate(t.predicate)
        assert d.kind == "direction"
        assert d.text == "raw-dir:any(4)"

    def test_transition_author_resets_last_predicate(self):
        # A second TransitionAuthor without a fresh EvalIf should NOT inherit
        # the previous predicate.
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(1, pc=0),
            _push_imm(0x0001, pc=3),
            _callcond(0x00, 2, pc=6),   # EvalIf
            _push_imm(0x100, pc=9),
            _callcond(0x05, 1, pc=12),  # T_Author #1 — gated
            _push_imm(0x200, pc=15),
            _callcond(0x05, 1, pc=18),  # T_Author #2 — should NOT be gated
            _ret(pc=21),
        ])
        result = emulate(script, slot_idx=11)
        assert len(result.transitions) == 2
        assert result.transitions[0].predicate is not None
        assert result.transitions[1].predicate is None

    def test_effect_event_hit_transition_facing_snap(self):
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(0x001A, pc=0),
            _push_imm(0x00BB, pc=3),
            _push_imm(0xFFFF, pc=6),
            _callcond(0x03, 3, pc=9),
            _ret(pc=12),
        ])
        result = emulate(script, slot_idx=12)
        assert len(result.effects) == 1
        e = result.effects[0]
        assert e.opcode == 0x1A
        assert e.kind == "hit_transition_facing_snap"
        assert e.is_facing_related
        assert e.concrete_args == [0x1A, 0xBB, 0xFFFF]

    @pytest.mark.parametrize(
        ("opcode", "expected_kind"),
        [
            (0x000B, "retrack_ramp_mode0"),
            (0x003C, "retrack_ramp_mode1"),
        ],
    )
    def test_effect_event_retrack_weight(self, opcode, expected_kind):
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(opcode, pc=0),
            _push_imm(0x5640, pc=3),
            _callcond(0x03, 2, pc=6),
            _ret(pc=9),
        ])
        result = emulate(script, slot_idx=13)
        e = result.effects[0]
        assert e.opcode == opcode
        assert e.kind == expected_kind
        assert e.ramp_selector == 0
        assert e.target_weight == pytest.approx(1.0)

    def test_effect_003b_is_not_a_facing_retrack_opcode(self):
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(0x003B, pc=0),
            _push_imm(0x5640, pc=3),
            _callcond(0x03, 2, pc=6),
            _ret(pc=9),
        ])

        event = emulate(script, slot_idx=13).effects[0]

        assert event.kind == "effect_0x003B"
        assert event.is_facing_related is False
        assert event.target_weight is None

    def test_effect_001a_is_a_hit_transition_facing_snap(self):
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(0x001A, pc=0),
            _push_imm(0x00BB, pc=3),
            _push_imm(0xFFFF, pc=6),
            _callcond(0x03, 3, pc=9),
            _ret(pc=12),
        ])

        event = emulate(script, slot_idx=13).effects[0]

        assert event.kind == "hit_transition_facing_snap"
        assert event.is_facing_related is True
        assert event.target_weight is None


def test_lux_fp16_literal_decode():
    assert _decode_lux_fp16_literal(0) == 0.0
    assert _decode_lux_fp16_literal(0x5640) == pytest.approx(100.0)
    assert _decode_lux_fp16_literal(0xBE00) == pytest.approx(-3.0)


def test_signed_division_and_remainder_match_native_c_semantics():
    assert _c_div(-7, 3) == -2
    assert _c_mod(-7, 3) == -1
    assert _c_div(7, -3) == -2
    assert _c_mod(7, -3) == 1
    with pytest.raises(ZeroDivisionError):
        _c_div(1, 0)


@pytest.mark.needs_dump
def test_cfg_emulator_does_not_mix_incompatible_branch_stacks():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr001.khd").read_bytes())
    transitions = emulate(bank.slots[281].bytecode, 281).transitions
    event = next(transition for transition in transitions if transition.source_pc == 0x4C738)

    assert len(event.args) == 5
    assert isinstance(event.args[3], (VarRef, Unknown))
    assert not (isinstance(event.args[3], Concrete) and event.args[3].value == 2)


def test_nested_brk_stops_parent_before_following_effect():
    parent = StackVMScript(
        bytecode_offset=0,
        instructions=[
            _push_imm(1, 0),
            StackVMInstruction(3, b"\x25\x0d\x01", 0x25, 0x25, False, "CALLCOND", imm_b0=0x0D, imm_b1=1),
            _push_imm(0x0E, 6),
            _push_imm(8, 9),
            StackVMInstruction(12, b"\x25\x03\x02", 0x25, 0x25, False, "CALLCOND", imm_b0=0x03, imm_b1=2),
            StackVMInstruction(15, b"\x05", 0x05, 0x05, False, "RET"),
        ],
    )
    child = StackVMScript(
        bytecode_offset=100,
        instructions=[
            StackVMInstruction(100, b"\x08", 0x08, 0x08, False, "BRK"),
        ],
    )

    class Bank:
        slots = [SimpleNamespace(bytecode=parent), SimpleNamespace(bytecode=child)]

        @staticmethod
        def resolve_packed_slot(value):
            return value if 0 <= value < 2 else None

    assert emulate(parent, 0).effects
    result = emulate(parent, 0, bank=Bank())

    assert result.effects == []
    assert result.must_break_execution is True
