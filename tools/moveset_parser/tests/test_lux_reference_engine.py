from __future__ import annotations

import pytest

from lux_reference_engine import (
    CallCondResult,
    MoveVMContext,
    MoveVMReference,
    StaticResolutionError,
    c_div_i16,
    c_mod_i16,
)
from stackvm import walk_stackvm


def execute(raw: bytes, *, handlers=None):
    vm = MoveVMReference()
    context = MoveVMContext(handlers=handlers or {})
    result = vm.execute(walk_stackvm(raw, 0), context)
    return vm, context, result


@pytest.mark.parametrize("opcode", [0x03, 0x04, 0x2A])
def test_native_absolute_jumps_skip_fallthrough(opcode: int) -> None:
    # target PC 6 pushes 7 then POP_RET.  The skipped path would return 99.
    raw = bytes([opcode, 0, 6, 0x89, 0, 99, 0x89, 0, 7, 0x05])
    _, _, result = execute(raw)
    assert result.return_value == 7


def test_signed_arithmetic_and_c_remainder() -> None:
    assert c_div_i16(-7, 3) == -2
    assert c_mod_i16(-7, 3) == -1
    raw = bytes([
        0x89, 0xFF, 0xF9,  # push -7
        0x89, 0x00, 0x03,  # push 3
        0x8F,              # DIV and push
        0x05,              # POP_RET
    ])
    _, _, result = execute(raw)
    assert result.return_value == -2


def test_native_frame_relative_storage_and_restore() -> None:
    vm = MoveVMReference()
    original_top = vm.top
    original_base = vm.frame_base
    raw = bytes([
        0x01, 0x00, 0x02,  # ENTER_FRAME 2
        0x89, 0x12, 0x34,  # push value
        0x19, 0x01, 0x00,  # STACK[0] = value
        0x8A, 0x01, 0x00,  # load STACK[0] and push
        0x05,
    ])
    result = vm.execute(walk_stackvm(raw, 0), MoveVMContext())
    assert result.return_value == 0x1234
    assert vm.top == original_top
    assert vm.frame_base == original_base


def test_callcond_preserves_authored_argument_order() -> None:
    observed = []

    def handler(context, arguments):
        observed.append(arguments)
        return CallCondResult(9)

    raw = bytes([
        0x89, 0, 1,
        0x89, 0, 2,
        0xA5, 0x05, 2,  # CALLCOND+PUSH
        0x05,
    ])
    _, context, result = execute(raw, handlers={0x05: handler})
    assert observed == [(1, 2)]
    assert result.return_value == 9
    assert context.call_log == [(0x05, (1, 2), 9)]


def test_unresolved_callcond_blocks_static_complete() -> None:
    raw = bytes([0x25, 0x26, 0, 0x05])
    context = MoveVMContext()
    with pytest.raises(StaticResolutionError, match="CALLCOND 0x26"):
        MoveVMReference().execute(walk_stackvm(raw, 0), context)
    with pytest.raises(StaticResolutionError, match="static-complete refused"):
        context.coverage.require_complete()


def test_jz_and_jnz_use_signed_short_truth_without_guessing() -> None:
    # push zero; JZ -> PC 9; return 7 (fallthrough return 99 is skipped)
    raw = bytes([
        0x89, 0, 0,
        0x28, 0, 9,
        0x89, 0, 99,
        0x89, 0, 7,
        0x05,
    ])
    _, _, result = execute(raw)
    assert result.return_value == 7


def test_compound_assignment_keeps_native_accumulator() -> None:
    # ACC=4, push 3, GLOBAL[1] += 3.  High-bit on ADD_VAR pushes the
    # unchanged ACC, matching the native interpreter's register flow.
    raw = bytes([
        0x89, 0, 10,
        0x19, 0, 1,
        0x89, 0, 3,
        0x9A, 0, 1,
        0x05,
    ])
    _, context, result = execute(raw)
    assert context.globals[1] == 13
    assert result.return_value == 3
