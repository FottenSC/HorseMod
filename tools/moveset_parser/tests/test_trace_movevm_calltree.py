import pytest

from stackvm import StackVMInstruction, StackVMScript
from trace_movevm_calltree import (
    TraceLimitExceeded,
    Value,
    _binary,
    _nested_local_frame,
    trace_slot,
)


def test_nested_call_with_only_slot_id_zeroes_all_locals() -> None:
    frame = _nested_local_frame((Value(2537, "slot"),))

    assert len(frame) == 16
    assert [value.value for value in frame] == [0] * 16


def test_nested_call_copies_supplied_locals_then_zero_fills() -> None:
    frame = _nested_local_frame(
        (Value(2537, "slot"), Value(143, "arg0"), Value(7, "arg1"))
    )

    assert [value.value for value in frame[:4]] == [143, 7, 0, 0]
    assert [value.value for value in frame[2:]] == [0] * 14


def test_signed_remainder_matches_native_c_semantics() -> None:
    assert _binary(0x0F, Value(0xFFF9), Value(3), 0).signed() == -2
    assert _binary(0x10, Value(0xFFF9), Value(3), 0).signed() == -1
    with pytest.raises(ZeroDivisionError):
        _binary(0x0F, Value(1), Value(0), 0)


def test_jump_opcode_updates_acc_before_reaching_target() -> None:
    script = StackVMScript(
        bytecode_offset=0,
        instructions=[
            StackVMInstruction(0, b"\x2a\x00\x03", 0x2A, 0x2A, False, "JMP_ABS", imm_u16=3),
            StackVMInstruction(3, b"\x26", 0x26, 0x26, False, "PUSH_ACC"),
            StackVMInstruction(4, b"\x25\x05\x01", 0x25, 0x25, False, "CALLCOND", imm_b0=5, imm_b1=1),
            StackVMInstruction(7, b"\x05", 0x05, 0x05, False, "RET"),
        ],
    )

    assert trace_slot(script, 0)[0].args[0].value == 3


def test_trace_limit_is_reported_instead_of_returning_partial_events() -> None:
    script = StackVMScript(
        bytecode_offset=0,
        instructions=[
            StackVMInstruction(0, b"\x12\x00\xf0", 0x12, 0x12, False, "POSTINC", imm_u16=0xF0),
            StackVMInstruction(3, b"\x2a\x00\x00", 0x2A, 0x2A, False, "JMP_ABS", imm_u16=0),
        ],
    )

    with pytest.raises(TraceLimitExceeded, match="refusing to return a partial trace"):
        trace_slot(script, 0, [Value(0)], max_states=5)
