"""Trace nested SC6 MoveVM CALLCOND calls without executing the game.

This is a conservative static tracer for ``FLuxMoveBankSlotView`` bytecode.
It follows bytecode control flow, propagates concrete 16-bit values and the
sixteen CALLCOND local arguments, and recursively resolves CALLCOND 0x0D bank
slot calls.  Unknown values stay unknown; the tool never guesses them.

The tracer is intentionally separate from ``stackvm_emulate``: that module
builds transition graphs, while this one preserves every CALLCOND so gameplay
mechanics implemented through nested effect scripts remain visible.
"""

from __future__ import annotations

import argparse
from collections import Counter, deque
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from luxformats import KhdFile, parse_khd
from stackvm import CALLCOND_NAMES, StackVMInstruction, StackVMScript
from stackvm_emulate import emulate


@dataclass(frozen=True)
class Value:
    value: int | None = None
    source: str = "unknown"

    def signed(self) -> int | None:
        if self.value is None:
            return None
        value = self.value & 0xFFFF
        return value - 0x10000 if value & 0x8000 else value

    def render(self) -> str:
        if self.value is None:
            return f"?<{self.source}>"
        return f"0x{self.value & 0xFFFF:04X}({self.signed()})"


UNKNOWN = Value()


class TraceLimitExceeded(RuntimeError):
    """Raised instead of returning an incomplete call trace."""


@dataclass(frozen=True)
class VMState:
    pc: int
    acc: Value
    stack: tuple[Value, ...]
    locals: tuple[Value, ...]
    stored: tuple[tuple[int, Value], ...] = ()

    def stored_map(self) -> dict[int, Value]:
        return dict(self.stored)


@dataclass(frozen=True)
class CallEvent:
    slot: int
    pc: int
    function_index: int
    args: tuple[Value, ...]

    @property
    def function_name(self) -> str:
        return CALLCOND_NAMES.get(self.function_index, f"OpcodeIf_{self.function_index:02X}")


@dataclass(frozen=True)
class TraceEvent:
    depth: int
    path: tuple[int, ...]
    call: CallEvent


def _binary(op: int, left: Value, right: Value, pc: int) -> Value:
    a, b = left.signed(), right.signed()
    if a is None or b is None:
        return Value(source=f"op@{pc:X}")
    if op == 0x0C:
        out = a + b
    elif op == 0x0D:
        out = a - b
    elif op == 0x0E:
        out = a * b
    elif op == 0x0F:
        if b == 0:
            raise ZeroDivisionError("native MoveVM signed division by zero")
        quotient = abs(a) // abs(b)
        out = -quotient if (a < 0) != (b < 0) else quotient
    elif op == 0x10:
        if b == 0:
            raise ZeroDivisionError("native MoveVM signed remainder by zero")
        quotient = abs(a) // abs(b)
        quotient = -quotient if (a < 0) != (b < 0) else quotient
        out = a - quotient * b
    elif op == 0x14:
        out = (a & 0xFFFF) & (b & 0xFFFF)
    elif op == 0x15:
        out = (a & 0xFFFF) | (b & 0xFFFF)
    elif op == 0x17:
        out = a << (b & 0x1F)
    elif op == 0x18:
        out = a >> (b & 0x1F)
    elif op == 0x1F:
        out = int(a == b)
    elif op == 0x20:
        out = int(a != b)
    elif op == 0x21:
        out = int(a < b)
    elif op == 0x22:
        out = int(a <= b)
    elif op == 0x23:
        out = int(a > b)
    elif op == 0x24:
        out = int(a >= b)
    else:
        return Value(source=f"op@{pc:X}")
    return Value(out & 0xFFFF, f"op@{pc:X}")


def _pop(stack: list[Value]) -> Value:
    return stack.pop() if stack else UNKNOWN


def _successors(
    script: StackVMScript,
    inst: StackVMInstruction,
    branch_value: Value = UNKNOWN,
) -> tuple[int, ...]:
    next_pc = inst.pc + inst.length
    if inst.opcode in (0x03, 0x04, 0x2A):
        return (script.bytecode_offset + (inst.imm_u16 or 0),)
    if inst.opcode in (0x28, 0x29):
        target_pc = script.bytecode_offset + (inst.imm_u16 or 0)
        if branch_value.value is None:
            return (next_pc, target_pc)
        is_nonzero = branch_value.value != 0
        # Native LuxMoveVM_ExecuteBytecode @ 0x1402E5A30 proves that
        # opcode 0x28 branches on zero and 0x29 branches on nonzero.
        branch_taken = not is_nonzero if inst.opcode == 0x28 else is_nonzero
        return (target_pc,) if branch_taken else (next_pc,)
    if inst.opcode in (0x02, 0x05, 0x06, 0x07, 0x08):
        return ()
    return (next_pc,)


def trace_slot(script: StackVMScript, slot: int, local_args: Iterable[Value] = (),
               max_states: int = 20000) -> list[CallEvent]:
    """Return deduplicated CALLCOND events reachable in one slot."""
    by_pc = {inst.pc: inst for inst in script.instructions}
    local_values = list(local_args)[:16]
    local_values.extend(Value(source=f"local[{i}]") for i in range(len(local_values), 16))
    initial = VMState(script.bytecode_offset, UNKNOWN, (), tuple(local_values))
    queue = deque([initial])
    seen: set[VMState] = set()
    events: dict[tuple, CallEvent] = {}

    while queue and len(seen) < max_states:
        state = queue.popleft()
        if state in seen:
            continue
        seen.add(state)
        inst = by_pc.get(state.pc)
        if inst is None:
            continue

        acc = state.acc
        stack = list(state.stack)
        locals_ = list(state.locals)
        stored = state.stored_map()
        op = inst.opcode

        if op == 0x01:
            count = inst.imm_u16 or 0
            if count:
                stack.extend(Value(source=f"frame@{inst.pc:X}") for _ in range(count + 1))
        elif op in (0x03, 0x04, 0x09, 0x0B, 0x2A):
            acc = Value(inst.imm_u16 or 0, f"literal@{inst.pc:X}")
        elif op == 0x0A:
            var_id = inst.imm_u16 or 0
            if 0xF0 <= var_id <= 0xFF:
                acc = state.locals[var_id - 0xF0]
            else:
                acc = stored.get(var_id, Value(source=f"var[{var_id:X}]"))
        elif op in (0x12, 0x13):
            var_id = inst.imm_u16 or 0
            if 0xF0 <= var_id <= 0xFF:
                old = locals_[var_id - 0xF0]
            else:
                old = stored.get(var_id, Value(source=f"var[{var_id:X}]"))
            acc = old
            if old.value is not None:
                delta = 1 if op == 0x12 else -1
                updated = Value((old.value + delta) & 0xFFFF, f"post@{inst.pc:X}")
                if 0xF0 <= var_id <= 0xFF:
                    locals_[var_id - 0xF0] = updated
                else:
                    stored[var_id] = updated
        elif op == 0x19:
            var_id = inst.imm_u16 or 0
            acc = _pop(stack)
            if 0xF0 <= var_id <= 0xFF:
                locals_[var_id - 0xF0] = acc
            else:
                stored[var_id] = acc
        elif op in (0x1A, 0x1B, 0x1C, 0x1D, 0x1E):
            var_id = inst.imm_u16 or 0
            rhs = _pop(stack)
            if 0xF0 <= var_id <= 0xFF:
                lhs = locals_[var_id - 0xF0]
                locals_[var_id - 0xF0] = _binary(op - 0x0E, lhs, rhs, inst.pc)
            else:
                lhs = stored.get(var_id, Value(source=f"var[{var_id:X}]"))
                stored[var_id] = _binary(op - 0x0E, lhs, rhs, inst.pc)
            acc = Value(source=f"rmw@{inst.pc:X}")
        elif op == 0x26:
            stack.append(acc)
        elif op == 0x27:
            acc = _pop(stack)
        elif op in (0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x14, 0x15, 0x17, 0x18,
                    0x1F, 0x20, 0x21, 0x22, 0x23, 0x24):
            rhs, lhs = _pop(stack), _pop(stack)
            acc = _binary(op, lhs, rhs, inst.pc)
        elif op == 0x11:
            value = _pop(stack).signed()
            acc = Value((-value) & 0xFFFF, f"neg@{inst.pc:X}") if value is not None else UNKNOWN
        elif op == 0x16:
            value = _pop(stack).value
            acc = Value(int(not value), f"not@{inst.pc:X}") if value is not None else UNKNOWN
        elif op == 0x25:
            argc = inst.imm_b1 or 0
            args = tuple(reversed([_pop(stack) for _ in range(argc)]))
            event = CallEvent(slot, inst.pc, inst.imm_b0 or 0, args)
            key = (event.slot, event.pc, event.function_index, event.args)
            events[key] = event
            acc = Value(source=f"call@{inst.pc:X}")
        branch_value = UNKNOWN
        if op in (0x28, 0x29):
            branch_value = _pop(stack)

        if inst.push_flag and op not in (0x02, 0x05, 0x06, 0x07, 0x08):
            stack.append(acc)

        successor_state = VMState(
            0, acc, tuple(stack), tuple(locals_),
            tuple(sorted(stored.items(), key=lambda item: item[0])),
        )
        for next_pc in _successors(script, inst, branch_value):
            queue.append(VMState(
                next_pc, successor_state.acc, successor_state.stack,
                successor_state.locals, successor_state.stored,
            ))

    if any(state not in seen for state in queue):
        raise TraceLimitExceeded(
            f"slot {slot} reached max_states={max_states}; refusing to return a partial trace"
        )

    return sorted(
        events.values(),
        key=lambda event: (
            event.pc,
            event.function_index,
            tuple((-1 if value.value is None else value.value, value.source) for value in event.args),
        ),
    )


def _nested_local_frame(call_args: tuple[Value, ...]) -> tuple[Value, ...]:
    """Model the 16-short local frame created by CALLCOND 0x0D.

    ``LuxMoveVM_ExecuteBankSlotScript @ 0x1402FCC30`` passes arguments after
    the packed slot ID to ``LuxMoveVM_RunBytecodeScript @ 0x1402E67B0``.
    Both native paths zero all sixteen locals before copying the supplied
    arguments, so omitted locals are concrete zeroes rather than inherited or
    unknown values.
    """
    supplied = list(call_args[1:17])
    supplied.extend(
        Value(0, f"zeroed nested local[{index}]")
        for index in range(len(supplied), 16)
    )
    return tuple(supplied)


def trace_call_tree(
    bank: KhdFile,
    root_slot: int,
    max_depth: int = 16,
    max_states: int = 20000,
) -> list[TraceEvent]:
    """Trace a slot and recursively expand concrete CALLCOND 0x0D calls."""
    output: list[TraceEvent] = []
    work = deque([(root_slot, tuple(), (root_slot,), 0)])
    visited: set[tuple[int, tuple[Value, ...]]] = set()
    while work:
        slot, locals_, path, depth = work.popleft()
        key = (slot, locals_)
        if key in visited or depth > max_depth or not (0 <= slot < len(bank.slots)):
            continue
        visited.add(key)
        script = bank.slots[slot].bytecode
        if script is None:
            continue
        for call in trace_slot(script, slot, locals_, max_states=max_states):
            output.append(TraceEvent(depth, path, call))
            if call.function_index != 0x0D or not call.args or call.args[0].value is None:
                continue
            child = bank.resolve_packed_slot(call.args[0].value)
            if child is not None and child not in path:
                work.append((child, _nested_local_frame(call.args), path + (child,), depth + 1))
    return output


def transition_paths(bank: KhdFile, root_slot: int, max_depth: int) -> dict[int, tuple[int, ...]]:
    """Return one shortest authored-transition path to each reachable slot.

    Utility-script expansion and authored move transitions are distinct edges in
    the native VM.  Keeping the paths separate prevents a shared CALLCOND 0x0D
    helper from being mistaken for an active move-state transition.
    """
    paths = {root_slot: (root_slot,)}
    queue = deque([(root_slot, 0)])
    while queue:
        slot, depth = queue.popleft()
        if depth >= max_depth:
            continue
        if not (0 <= slot < len(bank.slots)):
            continue
        script = bank.slots[slot].bytecode
        if script is None:
            continue
        try:
            transitions = emulate(script, slot).transitions
        except Exception:
            continue
        for transition in transitions:
            if transition.next_move_id_raw is None:
                continue
            destination = bank.resolve_packed_slot(transition.next_move_id_raw)
            if destination is None or destination in paths:
                continue
            paths[destination] = paths[slot] + (destination,)
            queue.append((destination, depth + 1))
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("khd", type=Path)
    parser.add_argument("slots", nargs="*", type=lambda text: int(text, 0))
    parser.add_argument(
        "--all-slots",
        action="store_true",
        help="Trace every slot as an independent root (normally used with --max-depth 0).",
    )
    parser.add_argument("--effects-only", action="store_true")
    parser.add_argument(
        "--effects-summary",
        action="store_true",
        help=(
            "Print compact per-root DispatchEffectOp opcode counts instead of "
            "individual calls. Direct and recursively inherited calls are counted "
            "separately so shared setup does not masquerade as root behavior."
        ),
    )
    parser.add_argument(
        "--called-slots-summary",
        action="store_true",
        help=(
            "Print concrete ExecuteBankSlotScript targets per root, separated "
            "into direct and recursively inherited targets."
        ),
    )
    parser.add_argument(
        "--function-index",
        type=lambda text: int(text, 0),
        help="Only print CALLCOND events for this function index.",
    )
    parser.add_argument(
        "--state-index",
        type=lambda text: int(text, 0),
        help=(
            "Only print WriteCharaStateShort (CALLCOND 0x14) events whose "
            "first argument is this concrete state index, or is unresolved."
        ),
    )
    parser.add_argument(
        "--calls-to-slot",
        type=lambda text: int(text, 0),
        help="Only print concrete ExecuteBankSlotScript calls resolving to this slot.",
    )
    parser.add_argument(
        "--transition-depth",
        type=int,
        default=0,
        help=(
            "Also trace slots reachable through up to this many authored "
            "move-state transitions. Utility CALLCOND 0x0D depth remains "
            "controlled independently by --max-depth."
        ),
    )
    parser.add_argument("--max-depth", type=int, default=16)
    parser.add_argument(
        "--max-states",
        type=int,
        default=20000,
        help="Maximum VM states per slot; exceeding it is reported as an error.",
    )
    args = parser.parse_args()

    file_bytes = args.khd.read_bytes()
    bank = parse_khd(file_bytes)
    roots = range(len(bank.slots)) if args.all_slots else args.slots
    if not args.all_slots and not args.slots:
        parser.error("provide at least one slot or use --all-slots")
    for root in roots:
        authored_paths = (
            transition_paths(bank, root, args.transition_depth)
            if args.transition_depth else {root: (root,)}
        )
        traced = [
            (authored_path, event)
            for slot, authored_path in sorted(authored_paths.items())
            for event in trace_call_tree(bank, slot, args.max_depth, args.max_states)
        ]
        if args.called_slots_summary:
            direct_slots: set[int] = set()
            inherited_slots: set[int] = set()
            for _authored_path, event in traced:
                call = event.call
                if call.function_index != 0x0D or not call.args or call.args[0].value is None:
                    continue
                target = bank.resolve_packed_slot(call.args[0].value)
                if target is None:
                    continue
                (direct_slots if event.depth == 0 else inherited_slots).add(target)
            render_slots = lambda values: " ".join(str(value) for value in sorted(values)) or "none"
            print(f"ROOT slot={root}")
            print(f"  direct: {render_slots(direct_slots)}")
            print(f"  inherited: {render_slots(inherited_slots)}")
            continue
        if args.effects_summary:
            direct: Counter[int | None] = Counter()
            inherited: Counter[int | None] = Counter()
            for _authored_path, event in traced:
                call = event.call
                if call.function_index != 0x03:
                    continue
                opcode = call.args[0].value if call.args else None
                (direct if event.depth == 0 else inherited)[opcode] += 1

            def render_counts(counts: Counter[int | None]) -> str:
                def sort_key(item: tuple[int | None, int]) -> tuple[int, int]:
                    opcode, _ = item
                    return (opcode is None, -1 if opcode is None else opcode)

                return " ".join(
                    f"{'unknown' if opcode is None else f'0x{opcode:04X}'}:{count}"
                    for opcode, count in sorted(counts.items(), key=sort_key)
                ) or "none"

            print(f"ROOT slot={root}")
            print(f"  direct: {render_counts(direct)}")
            print(f"  inherited: {render_counts(inherited)}")
            continue
        rendered: list[str] = []
        for authored_path, event in traced:
            call = event.call
            if args.effects_only and call.function_index not in (0x02, 0x03):
                continue
            if args.function_index is not None and call.function_index != args.function_index:
                continue
            if args.state_index is not None:
                if call.function_index != 0x14 or not call.args:
                    continue
                state_index = call.args[0].signed()
                if state_index is not None and state_index != args.state_index:
                    continue
            if args.calls_to_slot is not None:
                if call.function_index != 0x0D or not call.args or call.args[0].value is None:
                    continue
                if bank.resolve_packed_slot(call.args[0].value) != args.calls_to_slot:
                    continue
            indent = "  " * event.depth
            rendered_args = ", ".join(value.render() for value in call.args)
            utility_path = ">".join(str(slot) for slot in event.path)
            authored_path_text = ">".join(str(slot) for slot in authored_path)
            rendered.append(
                f"{indent}transition={authored_path_text} utility={utility_path} "
                f"pc=0x{call.pc:X} CALLCOND 0x{call.function_index:02X} "
                f"{call.function_name}({rendered_args})"
            )
        if rendered:
            print(f"ROOT slot={root}")
            print("\n".join(rendered))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
