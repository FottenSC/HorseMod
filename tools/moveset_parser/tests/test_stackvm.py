"""Bytecode disassembler tests.

These pin the opcode encoding — especially the tricky 0x01 case
(reservation, NOT push-immediate) and the CALLCOND arg ordering.
"""
from __future__ import annotations

import pytest

from stackvm import (
    TERMINATOR_OPCODES,
    walk_stackvm,
)


# ---------------------------------------------------------------------------
# Basic opcode parsing
# ---------------------------------------------------------------------------

def test_walk_empty_terminator():
    # RET (0x05) alone — should walk one instruction and stop.
    buf = bytes([0x05])
    s = walk_stackvm(buf, 0)
    assert len(s.instructions) == 1
    assert s.instructions[0].opcode == 0x05
    assert not s.truncated


def test_walk_push_imm_then_callcond():
    # 0x89 + BE16(0x123) = "SET_ACC 0x123, push" — canonical "push imm"
    # 0x25 + 0x05 + 0x01 = CALLCOND fn=0x05, argc=1 (TransitionAuthor)
    # 0x05 = RET
    buf = bytes([0x89, 0x01, 0x23, 0x25, 0x05, 0x01, 0x05])
    s = walk_stackvm(buf, 0)
    ops = [(i.opcode, i.imm_u16, i.imm_b0, i.imm_b1) for i in s.instructions]
    assert ops == [
        (0x09, 0x0123, None, None),  # SET_ACC + push (push-flag stripped to opcode)
        (0x25, None, 0x05, 0x01),    # CALLCOND fn=5, argc=1
        (0x05, None, None, None),    # RET
    ]
    assert s.instructions[0].push_flag is True


def test_jmp_target_followed():
    # Layout:
    #  0x00: JMP +3 (to 0x03)  - opcode 0x2A, imm 0x0003
    #  0x03: RET
    buf = bytes([0x2A, 0x00, 0x03, 0x05])
    s = walk_stackvm(buf, 0)
    # Both the JMP and the RET should be decoded
    opcodes = [i.opcode for i in s.instructions]
    assert 0x2A in opcodes
    assert 0x05 in opcodes


def test_jz_both_branches():
    # JZ followed by RET on fall-through, with a real op at the jump target
    #  0x00: JZ +5 (to 0x05)
    #  0x03: RET   <-- fall-through end
    #  0x04: pad
    #  0x05: 0x05 RET (target)
    buf = bytes([0x29, 0x00, 0x05, 0x05, 0x00, 0x05])
    s = walk_stackvm(buf, 0)
    pcs = {i.pc for i in s.instructions}
    # Both branches walked
    assert 0x00 in pcs   # the JZ itself
    assert 0x03 in pcs   # fall-through RET
    assert 0x05 in pcs   # target RET


def test_truncated_immediate():
    # FRAME (0x01) needs 3 bytes; with only 2 we must mark truncated
    buf = bytes([0x01, 0x12])
    s = walk_stackvm(buf, 0)
    assert s.truncated


def test_opcode_01_is_frame_not_push_immediate():
    buf = bytes([0x01, 0x00, 0x02, 0x05])
    s = walk_stackvm(buf, 0)
    assert s.instructions[0].opcode == 0x01
    assert s.instructions[0].mnemonic == "FRAME"
    assert "FRAME" in s.instructions[0].render()


def test_callcond_summary():
    # Two CALLCONDs at different fn-indices should produce a count summary
    buf = bytes([
        0x25, 0x00, 0x00,   # CALLCOND fn=0
        0x25, 0x05, 0x00,   # CALLCOND fn=5
        0x25, 0x05, 0x00,   # CALLCOND fn=5 again
        0x05,               # RET
    ])
    s = walk_stackvm(buf, 0)
    summary = s.callcond_summary
    assert summary == {0x00: 1, 0x05: 2}


def test_terminator_opcodes_constant():
    # The walker stops at: RET2, RET, RET-alt, RETBRK, BRK
    assert 0x02 in TERMINATOR_OPCODES
    assert 0x05 in TERMINATOR_OPCODES
    assert 0x06 in TERMINATOR_OPCODES
    assert 0x07 in TERMINATOR_OPCODES
    assert 0x08 in TERMINATOR_OPCODES


# ---------------------------------------------------------------------------
# Real-world: a Mitsurugi slot's bytecode should walk cleanly
# ---------------------------------------------------------------------------

@pytest.mark.needs_dump
def test_real_slot_bytecode_walks(mitsurugi_bytes, mitsurugi_bank):
    # Find any slot with a non-zero bytecode offset and walk it.
    slot = next(
        s for s in mitsurugi_bank.slots
        if s.dwBytecodeOffset_38 > 0 and s.wAnimationIndex_00 != 0xFFFF
    )
    s = walk_stackvm(mitsurugi_bytes, slot.dwBytecodeOffset_38)
    assert len(s.instructions) > 0
    # Should end with a terminator
    last = s.instructions[-1]
    assert last.opcode in TERMINATOR_OPCODES or s.truncated
