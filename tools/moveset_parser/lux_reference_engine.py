"""Instruction-faithful static reference primitives for SC6 Lux battle logic.

This module is deliberately strict.  It never substitutes a guessed value for
an unresolved native handler, branch target, or VM instruction.  Callers may
use it to build partial evidence, but ``StaticCoverage.require_complete`` is
the only path that can support a ``static-complete`` result.

The MoveVM interpreter mirrors ``LuxMoveVM_ExecuteBytecode @ 0x1402E5A30``:
the native downward-growing ring stack, frame-base save slot, signed 16-bit
variable tiers, absolute jumps, CALLCOND argument order, and C integer
arithmetic are represented explicitly.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Iterable, Mapping

from stackvm import StackVMInstruction, StackVMScript


def u16(value: int) -> int:
    return value & 0xFFFF


def i16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def c_div_i16(left: int, right: int) -> int:
    """C/C++ signed division with truncation toward zero, narrowed to i16."""
    if right == 0:
        raise ZeroDivisionError("native MoveVM signed division by zero")
    return i16(int(i16(left) / i16(right)))


def c_mod_i16(left: int, right: int) -> int:
    """C/C++ signed remainder (same sign as dividend), narrowed to i16."""
    if right == 0:
        raise ZeroDivisionError("native MoveVM signed remainder by zero")
    quotient = int(i16(left) / i16(right))
    return i16(i16(left) - quotient * i16(right))


@dataclass
class StaticCoverage:
    """Resolution ledger shared by the lifted static model.

    A non-empty unresolved set is a hard blocker for ``static-complete``.  The
    strings intentionally include the evidence coordinate so a corpus pass can
    lead directly back to the native function or authored bytecode.
    """

    opcodes: set[int] = field(default_factory=set)
    callconds: set[int] = field(default_factory=set)
    resolved_functions: set[str] = field(default_factory=set)
    resolved_assets: set[str] = field(default_factory=set)
    unresolved: set[str] = field(default_factory=set)

    def block(self, message: str) -> None:
        self.unresolved.add(message)

    @property
    def complete(self) -> bool:
        return not self.unresolved

    def require_complete(self) -> None:
        if self.unresolved:
            details = "\n".join(f"- {item}" for item in sorted(self.unresolved))
            raise StaticResolutionError(
                "static-complete refused; unresolved evidence remains:\n" + details
            )


class StaticResolutionError(RuntimeError):
    pass


@dataclass(frozen=True)
class CallCondResult:
    value: int = 0
    break_execution: bool = False


CallCondHandler = Callable[["MoveVMContext", tuple[int, ...]], CallCondResult]


@dataclass
class MoveVMContext:
    """Mutable state supplied to one native-equivalent bytecode invocation."""

    globals: list[int] = field(default_factory=lambda: [0] * 0xF0)
    locals: list[int] = field(default_factory=lambda: [0] * 16)
    handlers: Mapping[int, CallCondHandler] = field(default_factory=dict)
    coverage: StaticCoverage = field(default_factory=StaticCoverage)
    call_log: list[tuple[int, tuple[int, ...], int]] = field(default_factory=list)

    def __post_init__(self) -> None:
        if len(self.globals) != 0xF0:
            raise ValueError("MoveVM global bank must contain exactly 240 i16 values")
        if len(self.locals) != 16:
            raise ValueError("MoveVM local frame must contain exactly 16 i16 values")
        self.globals[:] = [i16(value) for value in self.globals]
        self.locals[:] = [i16(value) for value in self.locals]


@dataclass(frozen=True)
class MoveVMExecutionResult:
    return_value: int
    steps: int
    final_pc: int
    broke: bool


class MoveVMReference:
    """Exact integer/ring-stack execution core for Lux MoveVM bytecode."""

    def __init__(self, ring_words: int = 0x100) -> None:
        if ring_words <= 0 or ring_words & (ring_words - 1):
            raise ValueError("MoveVM ring size must be a positive power of two")
        self.ring = [0] * ring_words
        self.mask = ring_words - 1
        self.top = 0
        self.frame_base = 0

    def push(self, value: int) -> None:
        self.top = (self.top - 1) & self.mask
        self.ring[self.top] = i16(value)

    def pop(self) -> int:
        value = i16(self.ring[self.top])
        self.top = (self.top + 1) & self.mask
        return value

    def _var_index(self, var_id: int) -> int:
        return ((var_id - 0x100) + self.frame_base) & self.mask

    def read_var(self, context: MoveVMContext, var_id: int) -> int:
        if var_id < 0xF0:
            return i16(context.globals[var_id])
        if var_id < 0x100:
            return i16(context.locals[var_id - 0xF0])
        return i16(self.ring[self._var_index(var_id)])

    def write_var(self, context: MoveVMContext, var_id: int, value: int) -> None:
        value = i16(value)
        if var_id < 0xF0:
            context.globals[var_id] = value
        elif var_id < 0x100:
            context.locals[var_id - 0xF0] = value
        else:
            self.ring[self._var_index(var_id)] = value

    def _leave_frame(self, frame_words: int) -> None:
        if frame_words:
            saved_slot = (self.top + frame_words) & self.mask
            self.top = (saved_slot + 1) & self.mask
            self.frame_base = u16(self.ring[saved_slot]) & self.mask

    @staticmethod
    def _binary(opcode: int, left: int, right: int) -> int:
        left, right = i16(left), i16(right)
        if opcode == 0x0C:
            return i16(left + right)
        if opcode == 0x0D:
            return i16(left - right)
        if opcode == 0x0E:
            return i16(left * right)
        if opcode == 0x0F:
            return c_div_i16(left, right)
        if opcode == 0x10:
            return c_mod_i16(left, right)
        if opcode == 0x14:
            return i16(u16(left) & u16(right))
        if opcode == 0x15:
            return i16(u16(left) | u16(right))
        if opcode == 0x17:
            return i16(left << (u16(right) & 0x1F))
        if opcode == 0x18:
            return i16(left >> (u16(right) & 0x1F))
        if opcode == 0x1F:
            return int(left == right)
        if opcode == 0x20:
            return int(left != right)
        if opcode == 0x21:
            return int(left < right)
        if opcode == 0x22:
            return int(left <= right)
        if opcode == 0x23:
            return int(left > right)
        if opcode == 0x24:
            return int(left >= right)
        raise AssertionError(f"unhandled binary opcode 0x{opcode:02X}")

    def execute(
        self,
        script: StackVMScript,
        context: MoveVMContext,
        *,
        max_steps: int = 1_000_000,
    ) -> MoveVMExecutionResult:
        by_pc: dict[int, StackVMInstruction] = {
            instruction.pc: instruction for instruction in script.instructions
        }
        pc = script.bytecode_offset
        acc = 0
        frame_words = 0
        steps = 0

        def finish(value: int, broke: bool) -> MoveVMExecutionResult:
            self._leave_frame(frame_words)
            return MoveVMExecutionResult(i16(value), steps, pc, broke)

        while steps < max_steps:
            instruction = by_pc.get(pc)
            if instruction is None:
                context.coverage.block(
                    f"missing MoveVM instruction at bytecode PC 0x{pc:X}"
                )
                raise StaticResolutionError(
                    f"cannot execute unresolved MoveVM PC 0x{pc:X}"
                )
            steps += 1
            opcode = instruction.opcode
            context.coverage.opcodes.add(opcode)
            next_pc = pc + instruction.length

            if opcode == 0x00 or 0x2B <= opcode <= 0x3C:
                pass
            elif opcode == 0x01:
                frame_words = instruction.imm_u16 or 0
                if frame_words:
                    saved_slot = (self.top - 1) & self.mask
                    self.ring[saved_slot] = i16(self.frame_base)
                    self.top = (saved_slot - frame_words) & self.mask
                    self.frame_base = self.top
            elif opcode in (0x02, 0x06):
                return finish(0, False)
            elif opcode in (0x03, 0x04, 0x2A):
                acc = i16(instruction.imm_u16 or 0)
                next_pc = script.bytecode_offset + (instruction.imm_u16 or 0)
            elif opcode == 0x05:
                return finish(self.pop(), False)
            elif opcode == 0x07:
                return finish(self.pop(), True)
            elif opcode == 0x08:
                return finish(0, True)
            elif opcode in (0x09, 0x0B):
                acc = i16(instruction.imm_u16 or 0)
            elif opcode == 0x0A:
                acc = self.read_var(context, instruction.imm_u16 or 0)
            elif opcode in (
                0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                0x14, 0x15, 0x17, 0x18,
                0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
            ):
                right = self.pop()
                left = self.pop()
                acc = self._binary(opcode, left, right)
            elif opcode == 0x11:
                acc = i16(-self.pop())
            elif opcode in (0x12, 0x13):
                var_id = instruction.imm_u16 or 0
                acc = self.read_var(context, var_id)
                self.write_var(context, var_id, acc + (1 if opcode == 0x12 else -1))
            elif opcode == 0x16:
                acc = int(self.pop() == 0)
            elif opcode == 0x19:
                acc = self.pop()
                self.write_var(context, instruction.imm_u16 or 0, acc)
            elif opcode in (0x1A, 0x1B, 0x1C, 0x1D, 0x1E):
                var_id = instruction.imm_u16 or 0
                operand = self.pop()
                current = self.read_var(context, var_id)
                base_opcode = {
                    0x1A: 0x0C,
                    0x1B: 0x0D,
                    0x1C: 0x0E,
                    0x1D: 0x0F,
                    0x1E: 0x10,
                }[opcode]
                self.write_var(context, var_id, self._binary(base_opcode, current, operand))
                # Native compound assignments do not replace the accumulator.
            elif opcode == 0x25:
                function_index = instruction.imm_b0 or 0
                argument_count = instruction.imm_b1 or 0
                context.coverage.callconds.add(function_index)
                popped = [self.pop() for _ in range(argument_count)]
                arguments = tuple(reversed(popped))
                handler = context.handlers.get(function_index)
                if handler is None:
                    context.coverage.block(
                        f"CALLCOND 0x{function_index:02X} at bytecode PC 0x{pc:X}"
                    )
                    raise StaticResolutionError(
                        f"no verified handler for CALLCOND 0x{function_index:02X}"
                    )
                result = handler(context, arguments)
                acc = i16(result.value)
                context.call_log.append((function_index, arguments, acc))
                if result.break_execution:
                    return finish(acc, True)
            elif opcode == 0x26:
                self.push(acc)
            elif opcode == 0x27:
                acc = self.pop()
            elif opcode in (0x28, 0x29):
                condition = self.pop()
                acc = i16(instruction.imm_u16 or 0)
                take = condition == 0 if opcode == 0x28 else condition != 0
                if take:
                    next_pc = script.bytecode_offset + (instruction.imm_u16 or 0)
            else:
                context.coverage.block(
                    f"MoveVM opcode 0x{opcode:02X} at bytecode PC 0x{pc:X}"
                )
                raise StaticResolutionError(
                    f"unresolved MoveVM opcode 0x{opcode:02X}"
                )

            if instruction.push_flag:
                self.push(acc)
            pc = next_pc

        context.coverage.block(
            f"MoveVM step limit {max_steps} reached at bytecode PC 0x{pc:X}"
        )
        raise StaticResolutionError(
            f"MoveVM step limit {max_steps} reached at PC 0x{pc:X}"
        )


@dataclass(frozen=True)
class ScenarioState:
    fighter_id: str
    style_id: str
    authored_state: tuple[tuple[str, int], ...]
    player_side: int
    input_history: tuple[int, ...]
    initial_x: float
    initial_z: float
    facing_turns: float
    opponent_fighter_id: str
    opponent_move_id: int
    opponent_state: tuple[tuple[str, int], ...]
    timing_offset: int


@dataclass(frozen=True)
class MovementTick:
    tick: int
    lane: int
    move_id: int
    animation_frame_bits: int
    requested_input: int
    admitted_input: int
    velocity_x_bits: int
    velocity_z_bits: int
    root_dx_bits: int
    root_dz_bits: int
    position_x_bits: int
    position_z_bits: int
    facing_turns_bits: int
    control_flags: int


@dataclass(frozen=True)
class MovementTrace:
    scenario: ScenarioState
    ticks: tuple[MovementTick, ...]
    outcome_label: str
    evidence: tuple[str, ...]


@dataclass(frozen=True)
class EvasionResult:
    hit: bool
    first_collision_tick: int | None
    minimum_clearance_bits: int
    first_guardable_tick: int | None
    first_actionable_tick: int | None
    movement_reentry_tick: int | None
    attack_primitive: str | None
    body_primitive: str | None
    evidence: tuple[str, ...]

