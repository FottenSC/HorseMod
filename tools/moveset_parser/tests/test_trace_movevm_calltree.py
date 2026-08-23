import pytest
from types import SimpleNamespace

from stackvm import StackVMInstruction, StackVMScript
from trace_movevm_calltree import (
    TraceLimitExceeded,
    Value,
    _binary,
    _nested_local_frame,
    trace_call_tree,
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


def test_counter_loop_reaches_conservative_fixed_point() -> None:
    script = StackVMScript(
        bytecode_offset=0,
        instructions=[
            StackVMInstruction(0, b"\x12\x00\xf0", 0x12, 0x12, False, "POSTINC", imm_u16=0xF0),
            StackVMInstruction(3, b"\x2a\x00\x00", 0x2A, 0x2A, False, "JMP_ABS", imm_u16=0),
        ],
    )

    assert trace_slot(script, 0, [Value(0)], max_states=10) == []


def test_trace_limit_is_reported_instead_of_returning_partial_events() -> None:
    instructions = [
        StackVMInstruction(
            pc,
            b"\x03" + (pc + 3).to_bytes(2, "little"),
            0x03,
            0x03,
            False,
            "JMP_ABS",
            imm_u16=pc + 3,
        )
        for pc in range(0, 30, 3)
    ]
    script = StackVMScript(bytecode_offset=0, instructions=instructions)

    with pytest.raises(TraceLimitExceeded, match="refusing to return a partial trace"):
        trace_slot(script, 0, max_states=5)


def test_call_tree_expands_scheduled_script_and_honors_nested_break() -> None:
    root = StackVMScript(
        bytecode_offset=0,
        instructions=[
            StackVMInstruction(0, b"\x89\x00\x01", 0x89, 0x09, True, "SET_ACC_U16", imm_u16=1),
            StackVMInstruction(3, b"\x25\x15\x01", 0x25, 0x25, False, "CALLCOND", imm_b0=0x15, imm_b1=1),
            StackVMInstruction(6, b"\x05", 0x05, 0x05, False, "RET"),
        ],
    )
    scheduled = StackVMScript(
        bytecode_offset=100,
        instructions=[
            StackVMInstruction(100, b"\x89\x00\x02", 0x89, 0x09, True, "SET_ACC_U16", imm_u16=2),
            StackVMInstruction(103, b"\x25\x0d\x01", 0x25, 0x25, False, "CALLCOND", imm_b0=0x0D, imm_b1=1),
            StackVMInstruction(106, b"\x89\x00\x0e", 0x89, 0x09, True, "SET_ACC_U16", imm_u16=0x0E),
            StackVMInstruction(109, b"\x25\x03\x01", 0x25, 0x25, False, "CALLCOND", imm_b0=0x03, imm_b1=1),
            StackVMInstruction(112, b"\x05", 0x05, 0x05, False, "RET"),
        ],
    )
    breaker = StackVMScript(
        bytecode_offset=200,
        instructions=[StackVMInstruction(200, b"\x08", 0x08, 0x08, False, "BRK")],
    )

    class Bank:
        slots = [
            SimpleNamespace(bytecode=root),
            SimpleNamespace(bytecode=scheduled),
            SimpleNamespace(bytecode=breaker),
        ]

        @staticmethod
        def resolve_packed_slot(value):
            return value if 0 <= value < 3 else None

    events = trace_call_tree(Bank(), 0)

    assert any(event.path == (0, 1) for event in events)
    assert any(event.path == (0, 1) and event.call.pc == 103 for event in events)
    assert all(event.call.pc != 109 for event in events)
