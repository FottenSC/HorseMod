"""Unit tests for the predicate decoder + virtual stack emulator.

These pin down the load-bearing decoder logic — especially the input
bit layout, which we've broken twice during this project.
"""
from __future__ import annotations

import pytest

from stackvm import StackVMInstruction, StackVMScript
from stackvm_emulate import (
    Concrete, VarRef, Unknown,
    PredicateEvent,
    _decode_lux_fp16_literal,
    _decode_button_mask,
    _decode_direction,
    _decode_input_mask,
    _decode_motion_pattern,
    decode_predicate,
    emulate,
)


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
        assert _decode_motion_pattern(0x8000) == "stick:neutral"

    def test_any_sentinel(self):
        assert _decode_motion_pattern(0x8001) == "stick:any"

    def test_always_true(self):
        assert _decode_motion_pattern(0x8002) == "stick:*"

    def test_simple_back(self):
        # REQUIRED-ANY: bit 0x01 = "back" by our convention
        assert "back" in _decode_motion_pattern(0x0001)


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

    def test_button_mask_sub_op_1(self):
        # sub-op 1: raw button mask test
        d = decode_predicate(_pred(0x01, 0x0004))  # K button
        assert d.kind == "buttons"
        assert d.text == "K"

    def test_button_indirect(self):
        d = decode_predicate(_pred_indirect(0x01))
        # We can't know the mask, so this should be tagged indirect
        assert d.kind == "indirect"

    def test_multi_arg_input_check_nibble_4(self):
        # sub-op 0x24 with arg nibble 4 (dwCurrentInputMask check)
        d = decode_predicate(_pred(0x24, 0x4001))  # 0x4000 nibble + A
        assert d.kind == "buttons"
        assert "A" in d.text

    def test_multi_arg_stance_nibble_1(self):
        # nibble 1 = stance ring 1 check — purely contextual, no buttons
        d = decode_predicate(_pred(0x24, 0x1008))  # stance1 == 8
        assert d.kind == "stance"
        assert "stance1==8" in d.text

    def test_command_input(self):
        # sub-op 0x2C: command-input system (LuxBattle_EvaluateMoveTransitionConditions)
        d = decode_predicate(_pred(0x2C, 0x0001))
        assert d.kind == "command"
        assert "cmd:0x0001" in d.text

    def test_motion_check(self):
        # sub-op 5: motion sequence (history-ring scan)
        d = decode_predicate(_pred(0x05, 0x8001))  # "any stick"
        assert d.kind == "direction"
        assert "stick:any" in d.text

    def test_frame_window(self):
        # sub-op 8: active-frame-window test
        d = decode_predicate(_pred(0x08, 5, 12))
        assert d.kind == "frame"
        assert "5" in d.text and "12" in d.text

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
        # PUSH sub_op=1; PUSH 0x0001 (A button); CALLCOND 0x00 (EvalIf), 2
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
        # The decoder should classify this as a button press
        d = decode_predicate(t.predicate)
        assert d.kind == "buttons"
        assert d.text == "A"

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

    def test_effect_event_facing_commit(self):
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
        assert e.kind == "facing_commit"
        assert e.is_facing_related
        assert e.concrete_args == [0x1A, 0xBB, 0xFFFF]

    def test_effect_event_retrack_weight(self):
        script = StackVMScript(bytecode_offset=0, instructions=[
            _push_imm(0x003C, pc=0),
            _push_imm(0x5640, pc=3),
            _callcond(0x03, 2, pc=6),
            _ret(pc=9),
        ])
        result = emulate(script, slot_idx=13)
        e = result.effects[0]
        assert e.opcode == 0x3C
        assert e.kind == "retrack_ramp_mode1"
        assert e.ramp_selector == 0
        assert e.target_weight == pytest.approx(100.0 / 60.0)


def test_lux_fp16_literal_decode():
    assert _decode_lux_fp16_literal(0) == 0.0
    assert _decode_lux_fp16_literal(0x5640) == pytest.approx(100.0)
    assert _decode_lux_fp16_literal(0xBE00) == pytest.approx(-3.0)
