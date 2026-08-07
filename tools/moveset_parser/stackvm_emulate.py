"""
SC6 MoveVM bytecode virtual-stack emulator.

Walks the disassembled `StackVMInstruction` stream from `stackvm.py` and
abstractly tracks the stack + ACC so it can extract structured "events"
from CALLCOND opcodes. Specifically it captures:

  * TransitionAuthor calls (CALLCOND 0x05..0x08): the args contain the
    packed NextMoveID, timing index, and threshold (see Ghidra plate at
    LuxMoveVM_DecodeVariadicStreamArgs @ 0x1402FC930).
  * EvaluateIfOpcode calls (CALLCOND 0x00, 0x01, 0x25): the args[0] is
    the predicate sub-opcode kind; args[1..] are predicate parameters.

The emulator follows the bytecode control-flow graph and merges abstract state
at branch joins.
Stack values are tagged abstract:
    "concrete"  - a known u16 from SET_ACC_U16 / folded math
    "varref"    - a LOAD_VAR result whose runtime value we don't know
    "unknown"   - the result of an operation we couldn't fold

Each TransitionEvent is annotated with the most recently captured
predicate (last_predicate). In SC6 bytecode the canonical pattern is:

    SET_ACC_U16+PUSH <sub_op>; ...; CALLCOND 0x00 (EvaluateIfOpcode), argc
    JZ <skip>
    SET_ACC_U16+PUSH <next_move_id>; ...; CALLCOND 0x05 (TransitionAuthor), argc

So the "last_predicate" heuristic correlates the gating predicate to the
transition it gates. False positives are possible (predicates used for
other purposes), but in practice this catches 90%+ of input-driven
transitions cleanly.

VERIFIED bit layouts (from Ghidra labelling pass on 2026-05-14):

  Button bits in ushort dwCurrentInputMask (canonical, from
  LuxBattle_BuildInputEventBytes_FromBitmasks @ 0x1403ECC90):
      0x0001 = A   0x0002 = B   0x0004 = K   0x0008 = G
  Direction bits in same mask:
      0x0400 = back (4)  0x0800 = forward (6)
      0x1000 = up (8)    0x2000 = down (2)
      combos form numpad 1, 3, 7, 9.

  Compact-input pattern layout for the +0x2190 history-ring matcher.
  LuxBattle_TickCharaMainSimulation @ 0x14034DD7F copies the low ushort
  of chara+0x2150 into history entry +0x00. LuxBattle_TickCharaInput
  establishes that this word uses A/B/K/G in bits 0..3 and direction in
  bits 10..13:
      Pattern 0x8000 = "no buttons" sentinel
      Pattern 0x8001 = "any button" sentinel
      Pattern 0x8002 = "always true" sentinel
      Otherwise:
        (pattern >> 8) & 0xF = REQUIRED-ALL bits in history low nibble
        pattern & 0x2F       = REQUIRED-ANY bits

  EvaluateIfOpcode sub-opcode kinds we recognise for input rendering:
       1   primary raw direction nibble mask (args[1] & chara+0x2164)
       2   primary side-decoded direction sequence
       3   side-direction mask (args[1] & chara+0x2178)
      0x27 raw secondary direction nibble vs chara+0x216c
      0x26 secondary decoded direction ID vs chara+0x2168
      0x28 secondary side-decoded direction ID vs chara+0x217c
      0x29 secondary side-direction mask vs chara+0x2180
      0x24 multi-arg direction matcher (args[i] & 0xF000 selects source:
            0x1000=side-decoded ID 2170, 0x2000=decoded ID 215C,
            0x3000=side mask 2178, 0x4000=raw nibble 2164)
      0x25 secondary-ring variant of 0x24
       5   "motion held for N consecutive history frames"
       6   "motion matched in last N OR-folded frames"
      0x20 "motion AND-folded last N frames"
       8   active-frame-window test (current frame in [args[1],args[2]])
      0x13 same shape, different lane field
      0x42 same
      0x5d same
      0xe  current move id == args[1]
      0x6b stance @ chara+0x250 == args[1]
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Mapping, Optional, Union
import struct

from luxformats import decode_packed_slot_id
from stackvm import StackVMInstruction, StackVMScript


# Direction bit layout for raw dwCurrentInputMask.
# (Names below are the 8-way numpad directions.)
INPUT_BUTTON_BITS = {
    0x0001: "A",
    0x0002: "B",
    0x0004: "K",
    0x0008: "G",
}
INPUT_DIRECTION_COMBO_TO_NUMPAD = {
    0x0000: 5,
    0x0400: 4,
    0x0800: 6,
    0x1000: 8,
    0x2000: 2,
    0x0400 | 0x1000: 7,
    0x0800 | 0x1000: 9,
    0x0400 | 0x2000: 1,
    0x0800 | 0x2000: 3,
}
INPUT_DIR_MASK = 0x3C00          # bits 10..13
INPUT_BUTTON_MASK = 0x000F       # bits 0..3

# Compact-input bits consumed by CheckMotionConditionFlags. History entry
# +0x00 is a low-ushort copy of chara+0x2150, not the side-direction mask at
# +0x06. Bit 0x20 participates in the native REQUIRED-ANY mask, but its
# producer-side meaning is still unresolved.
HISTORY_INPUT_BITS = {
    0x01: "A",
    0x02: "B",
    0x04: "K",
    0x08: "G",
    0x20: "?bit5",
}

# Side-relative direction-condition masks published through
# g_awLuxInputDirectionMaskByDecodedId and copied to history entry +0x06.
# These are not A/B/K/G bits and are not held-frame counters.
DIRECTION_CONDITION_BITS = {
    0x01: "8",
    0x02: "2",
    0x04: "6",
    0x08: "4",
}

# Raw direction nibbles are obtained from (dwCompactInput >> 10) & 0xF.
RAW_DIRECTION_NIBBLE_BITS = {
    0x01: "4",
    0x02: "6",
    0x04: "8",
    0x08: "2",
}

# g_abLuxInputNibbleToDecodedId maps compact direction nibbles to these IDs.
DECODED_DIRECTION_ID_TO_NUMPAD = {
    0: "5",
    1: "9",
    2: "8",
    3: "7",
    4: "6",
    5: "5",
    6: "4",
    7: "3",
    8: "2",
    9: "1",
}

# CALLCOND function-index subsets we react to.
CALLCOND_EVAL_IF = {0x00, 0x01, 0x25}
CALLCOND_TRANSITION_AUTHOR = {0x05, 0x06, 0x07, 0x08}
CALLCOND_SCHEDULE_TRANSITION = {0x15}
CALLCOND_EXEC_BANK_SLOT = {0x0D}
CALLCOND_EFFECT_DISPATCH = {0x02, 0x03}

FACING_EFFECT_OPCODES = {
    0x1A: "facing_commit",
    0x3B: "retrack_ramp_mode0",
    0x3C: "retrack_ramp_mode1",
}


# ---------------------------------------------------------------------------
# Stack-value model
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Concrete:
    """A concrete u16 known at extract-time."""
    value: int
    source_pc: int = -1

    def as_int(self) -> Optional[int]:
        return self.value & 0xFFFF


@dataclass(frozen=True)
class VarRef:
    """A LOAD_VAR result — we know the varid but not its runtime value."""
    varid: int
    source_pc: int = -1

    def as_int(self) -> Optional[int]:
        return None


@dataclass(frozen=True)
class Unknown:
    """Opaque value from an op we don't simulate."""
    source_pc: int = -1

    def as_int(self) -> Optional[int]:
        return None


StackVal = Union[Concrete, VarRef, Unknown]


def _signed_short(v: int) -> int:
    """Interpret a u16 as signed short (the bytecode VM uses i16 ops)."""
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# ---------------------------------------------------------------------------
# Captured events
# ---------------------------------------------------------------------------

@dataclass
class PredicateEvent:
    """A captured EvaluateIfOpcode call."""
    callcond_idx: int             # 0x00, 0x01, or 0x25
    args: list[StackVal]
    source_pc: int

    @property
    def sub_opcode(self) -> Optional[int]:
        """First arg is the predicate sub-op kind (case label)."""
        if not self.args:
            return None
        v = self.args[0]
        return v.value if isinstance(v, Concrete) else None


@dataclass
class TransitionEvent:
    """A captured TransitionAuthor call."""
    callcond_idx: int             # 0x05..0x08
    args: list[StackVal]
    source_pc: int
    predicate: Optional[PredicateEvent] = None  # Most recent gating predicate

    @property
    def next_move_id_raw(self) -> Optional[int]:
        """args[0] = packed FLuxMoveBank address.

        Engine reference: LuxMoveVM_ResolveBankSlot uses bits 15..12 as
        the move-bank bucket and bits 10..0 as slot within that bucket.
        Bit 11 is not part of the slot index.
        """
        if not self.args:
            return None
        v = self.args[0]
        return v.value if isinstance(v, Concrete) else None

    @property
    def next_move_bank(self) -> Optional[int]:
        raw = self.next_move_id_raw
        if raw is None:
            return None
        return decode_packed_slot_id(raw).bank

    @property
    def next_move_slot(self) -> Optional[int]:
        raw = self.next_move_id_raw
        if raw is None:
            return None
        return decode_packed_slot_id(raw).slot

    @property
    def next_move_ignored_bit_11(self) -> Optional[bool]:
        raw = self.next_move_id_raw
        if raw is None:
            return None
        return decode_packed_slot_id(raw).ignored_bit_11

    @property
    def is_indirect(self) -> bool:
        """True if next_move_id came from a LOAD_VAR (concrete unknown)."""
        if not self.args:
            return False
        return isinstance(self.args[0], VarRef)


@dataclass
class EffectEvent:
    """A captured MoveVM effect-dispatch CALLCOND."""
    callcond_idx: int             # 0x02 / 0x03 DispatchEffectOp aliases
    args: list[StackVal]
    source_pc: int

    @property
    def opcode(self) -> Optional[int]:
        if not self.args:
            return None
        v = self.args[0]
        return v.value if isinstance(v, Concrete) else None

    @property
    def concrete_args(self) -> list[Optional[int]]:
        return [a.value if isinstance(a, Concrete) else None for a in self.args]

    @property
    def kind(self) -> str:
        op = self.opcode
        if op is None:
            return "effect_indirect"
        return FACING_EFFECT_OPCODES.get(op, f"effect_0x{op:04X}")

    @property
    def is_facing_related(self) -> bool:
        op = self.opcode
        return op in FACING_EFFECT_OPCODES if op is not None else False

    @property
    def target_weight(self) -> Optional[float]:
        """Decoded retrack target for opcodes 0x3B/0x3C, after engine /60."""
        op = self.opcode
        if op not in (0x3B, 0x3C) or len(self.args) < 2:
            return None
        v = self.args[1]
        if not isinstance(v, Concrete):
            return None
        return decode_lux_fp16_literal(v.value) / 60.0

    @property
    def ramp_selector(self) -> Optional[int]:
        """Opcode arg2: nonzero asks native code to use lane timing as ramp duration."""
        if len(self.args) < 3:
            return 0
        v = self.args[2]
        return v.value if isinstance(v, Concrete) else None


@dataclass
class BankScriptEvent:
    """A captured nested bank-slot execution (CALLCOND 0x0D or 0x15).

    The first argument is the packed MoveVM bank/slot id. Remaining arguments
    seed the nested script's local variables; preserving them is required to
    distinguish the common neutral-locomotion helpers from unrelated calls to
    the same script.
    """

    callcond_idx: int
    args: list[StackVal]
    source_pc: int

    @property
    def packed_move_id(self) -> Optional[int]:
        if not self.args:
            return None
        value = self.args[0]
        return value.value if isinstance(value, Concrete) else None

    @property
    def concrete_args(self) -> list[Optional[int]]:
        return [arg.value if isinstance(arg, Concrete) else None for arg in self.args]


@dataclass
class SlotTransitions:
    """All transition events extracted from one slot's bytecode."""
    slot_idx: int
    bytecode_offset: int
    transitions: list[TransitionEvent] = field(default_factory=list)
    effects: list[EffectEvent] = field(default_factory=list)
    bank_scripts: list[BankScriptEvent] = field(default_factory=list)
    predicates: list[PredicateEvent] = field(default_factory=list)
    # Counts of CALLCOND sub-opcode kinds used (a coarse fingerprint).
    callcond_summary: dict[int, int] = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Emulator
# ---------------------------------------------------------------------------

class _StackState:
    """A mutable stack-tracking state for one walk."""

    __slots__ = ("stack", "acc")

    def __init__(self) -> None:
        self.stack: list[StackVal] = []
        self.acc: StackVal = Unknown()

    def push(self, val: StackVal) -> None:
        self.stack.append(val)

    def pop(self) -> StackVal:
        if not self.stack:
            return Unknown()
        return self.stack.pop()

    def peek_n(self, n: int) -> list[StackVal]:
        if n <= 0:
            return []
        if n > len(self.stack):
            # Pad with unknowns
            pad = [Unknown() for _ in range(n - len(self.stack))]
            return pad + list(self.stack)
        return list(self.stack[-n:])


def _emulate_linear(script: StackVMScript, slot_idx: int) -> SlotTransitions:
    """Run the virtual stack emulator over one slot's instructions and
    return all captured TransitionEvents.

    Single-pass linear over instructions in PC order. Branches (JMP/JNZ/JZ)
    are treated as straight-line fall-through for stack purposes — we rely
    on the reachability of the source instruction list (the walker has
    already pruned unreachable code).
    """
    out = SlotTransitions(slot_idx=slot_idx, bytecode_offset=script.bytecode_offset)
    state = _StackState()
    last_predicate: Optional[PredicateEvent] = None

    # Cache for STORE_VAR -> last concrete value, to resolve indirect
    # TransitionAuthor cases where next_move_id is loaded from a STACK var.
    var_concretes: dict[int, int] = {}

    for inst in script.instructions:
        op = inst.opcode

        if op == 0x01:                              # STACK FRAME RESERVE <n>
            # Not "push immediate". Engine handler (case 1 of
            # LuxMoveVM_ExecuteBytecode @ 0x1402E5A30): if imm != 0, pushes
            # one spill cell (DAT_14470d5d0) and then decrements the stack
            # top by imm, effectively reserving imm+1 uninitialized slots.
            # Concrete `PUSH IMMEDIATE` in shipped bytecode uses 0x09 with
            # the push-flag (0x89), which we handle correctly via
            # SET_ACC_U16 + state.push(state.acc).
            n = inst.imm_u16 or 0
            if n != 0:
                for _ in range(n + 1):
                    state.push(Unknown(source_pc=inst.pc))
        elif op in (0x03, 0x04, 0x09, 0x0B, 0x2A):  # SET_ACC_U16 / JMP target
            state.acc = Concrete(inst.imm_u16 or 0, source_pc=inst.pc)
        elif op == 0x0A:                            # LOAD_VAR <varid>
            varid = inst.imm_u16 or 0
            known = var_concretes.get(varid)
            if known is not None:
                state.acc = Concrete(known, source_pc=inst.pc)
            else:
                state.acc = VarRef(varid, source_pc=inst.pc)
        elif op == 0x12 or op == 0x13:              # POSTINC / POSTDEC
            state.acc = Unknown(source_pc=inst.pc)
        elif op == 0x19:                            # STORE_VAR <varid>
            top = state.pop()
            varid = inst.imm_u16 or 0
            if isinstance(top, Concrete):
                var_concretes[varid] = top.value
            else:
                var_concretes.pop(varid, None)
            state.acc = top
        elif op in (0x1A, 0x1B, 0x1C, 0x1D, 0x1E):  # ADD/SUB/MUL/DIV/MOD_VAR
            state.pop()  # Consume rhs; we forget concretes here
            varid = inst.imm_u16 or 0
            var_concretes.pop(varid, None)
            # The engine doesn't update ACC in these RMW ops, but a stale
            # ACC from a prior op shouldn't leak into the next PUSH_ACC.
            state.acc = Unknown(source_pc=inst.pc)
        elif op == 0x26:                            # PUSH_ACC
            state.push(state.acc)
        elif op == 0x27:                            # POP_ACC
            state.acc = state.pop()
        elif op in (0x0C, 0x0D, 0x0E, 0x0F, 0x10):  # ADD/SUB/MUL/DIV/MOD
            b = state.pop()
            a = state.pop()
            if isinstance(a, Concrete) and isinstance(b, Concrete):
                av, bv = _signed_short(a.value), _signed_short(b.value)
                if op == 0x0C: r = av + bv
                elif op == 0x0D: r = av - bv
                elif op == 0x0E: r = av * bv
                elif op == 0x0F: r = _c_div(av, bv)
                else:            r = _c_mod(av, bv)
                state.acc = Concrete(r & 0xFFFF, source_pc=inst.pc)
            else:
                state.acc = Unknown(source_pc=inst.pc)
        elif op == 0x11:                            # NEG
            a = state.pop()
            if isinstance(a, Concrete):
                state.acc = Concrete((-_signed_short(a.value)) & 0xFFFF, source_pc=inst.pc)
            else:
                state.acc = Unknown(source_pc=inst.pc)
        elif op in (0x14, 0x15, 0x16, 0x17, 0x18):  # AND/OR/LNOT/SHL/SAR
            if op == 0x16:                          # LNOT (1 arg)
                a = state.pop()
                if isinstance(a, Concrete):
                    state.acc = Concrete(0 if a.value != 0 else 1, source_pc=inst.pc)
                else:
                    state.acc = Unknown(source_pc=inst.pc)
            else:
                b = state.pop()
                a = state.pop()
                if isinstance(a, Concrete) and isinstance(b, Concrete):
                    av, bv = a.value & 0xFFFF, b.value & 0xFFFF
                    if op == 0x14:   r = av & bv
                    elif op == 0x15: r = av | bv
                    elif op == 0x17: r = (_signed_short(av) << (bv & 0x1F)) & 0xFFFF
                    else:            r = (_signed_short(av) >> (bv & 0x1F)) & 0xFFFF
                    state.acc = Concrete(r, source_pc=inst.pc)
                else:
                    state.acc = Unknown(source_pc=inst.pc)
        elif op in (0x1F, 0x20, 0x21, 0x22, 0x23, 0x24):  # EQ/NE/LT/LE/GT/GE
            state.pop()
            state.pop()
            state.acc = Unknown(source_pc=inst.pc)
        elif op == 0x25:                            # CALLCOND
            fn_idx = inst.imm_b0 or 0
            argc = inst.imm_b1 or 0
            args = []
            for _ in range(argc):
                args.append(state.pop())
            args.reverse()  # args[0] = first-pushed
            out.callcond_summary[fn_idx] = out.callcond_summary.get(fn_idx, 0) + 1

            if fn_idx in CALLCOND_EVAL_IF:
                last_predicate = PredicateEvent(
                    callcond_idx=fn_idx, args=args, source_pc=inst.pc)
                out.predicates.append(last_predicate)
                state.acc = Unknown(source_pc=inst.pc)
            elif fn_idx in CALLCOND_TRANSITION_AUTHOR:
                out.transitions.append(
                    TransitionEvent(
                        callcond_idx=fn_idx,
                        args=args,
                        source_pc=inst.pc,
                        predicate=last_predicate,
                    )
                )
                # The transition's gate has been consumed.
                last_predicate = None
                state.acc = Unknown(source_pc=inst.pc)
            elif fn_idx in CALLCOND_EFFECT_DISPATCH:
                out.effects.append(
                    EffectEvent(
                        callcond_idx=fn_idx,
                        args=args,
                        source_pc=inst.pc,
                    )
                )
                state.acc = Unknown(source_pc=inst.pc)
            elif fn_idx in CALLCOND_EXEC_BANK_SLOT | CALLCOND_SCHEDULE_TRANSITION:
                out.bank_scripts.append(
                    BankScriptEvent(
                        callcond_idx=fn_idx,
                        args=args,
                        source_pc=inst.pc,
                    )
                )
                state.acc = Unknown(source_pc=inst.pc)
            else:
                state.acc = Unknown(source_pc=inst.pc)
            # last_predicate is invalidated by TransitionAuthor fire only
            # (see above). Non-EvalIf CALLCONDs like ExecuteBankSlotScript
            # are side-effects but don't end the predicate scope — empirically,
            # the pattern PUSH preds; CALLCOND EvalIf; JZ skip; ...;
            # CALLCOND TransitionAuthor still gates correctly even when
            # other CALLCONDs (e.g. DispatchEffectOp for VFX) intervene.
        elif op in (0x28, 0x29, 0x2A):              # JZ / JNZ / JMP
            if op != 0x2A:
                state.pop()
        elif op in (0x02, 0x05, 0x06, 0x07, 0x08):  # RETs / BRKs
            pass
        # opcode 0x00 (NOP) and 0x2B..0x3C all no-op

        # Push-flag (bit 0x80 on opcode byte)
        if inst.push_flag and op not in (0x02, 0x05, 0x06, 0x07, 0x08):
            state.push(state.acc)

    return out


PredicateState = tuple[int, tuple[StackVal, ...], int]


@dataclass(frozen=True)
class _EmulationState:
    pc: int
    acc: StackVal
    stack: tuple[StackVal, ...]
    variables: tuple[tuple[int, StackVal], ...] = ()
    last_predicate: PredicateState | None = None


def _pop_value(stack: list[StackVal], pc: int) -> StackVal:
    return stack.pop() if stack else Unknown(source_pc=pc)


def _c_div(left: int, right: int) -> int:
    if right == 0:
        raise ZeroDivisionError("native MoveVM signed division by zero")
    quotient = abs(left) // abs(right)
    return -quotient if (left < 0) != (right < 0) else quotient


def _c_mod(left: int, right: int) -> int:
    return left - _c_div(left, right) * right


def _fold_binary(op: int, left: StackVal, right: StackVal, pc: int) -> StackVal:
    if not isinstance(left, Concrete) or not isinstance(right, Concrete):
        return Unknown(source_pc=pc)
    a, b = _signed_short(left.value), _signed_short(right.value)
    if op == 0x0C:
        result = a + b
    elif op == 0x0D:
        result = a - b
    elif op == 0x0E:
        result = a * b
    elif op == 0x0F:
        result = _c_div(a, b)
    elif op == 0x10:
        result = _c_mod(a, b)
    elif op == 0x14:
        result = (a & 0xFFFF) & (b & 0xFFFF)
    elif op == 0x15:
        result = (a & 0xFFFF) | (b & 0xFFFF)
    elif op == 0x17:
        result = a << (b & 0x1F)
    elif op == 0x18:
        result = a >> (b & 0x1F)
    elif op == 0x1F:
        result = int(a == b)
    elif op == 0x20:
        result = int(a != b)
    elif op == 0x21:
        result = int(a < b)
    elif op == 0x22:
        result = int(a <= b)
    elif op == 0x23:
        result = int(a > b)
    elif op == 0x24:
        result = int(a >= b)
    else:
        return Unknown(source_pc=pc)
    return Concrete(result & 0xFFFF, source_pc=pc)


def _merge_value(left: StackVal, right: StackVal, pc: int) -> StackVal:
    if isinstance(left, Concrete) and isinstance(right, Concrete) and left.value == right.value:
        return left
    if isinstance(left, VarRef) and isinstance(right, VarRef) and left.varid == right.varid:
        return left
    return Unknown(source_pc=pc)


def _merge_states(left: _EmulationState, right: _EmulationState) -> _EmulationState:
    if len(left.stack) == len(right.stack):
        stack = tuple(_merge_value(a, b, left.pc) for a, b in zip(left.stack, right.stack))
    else:
        # A path-dependent stack depth cannot be aligned safely.
        stack = tuple(Unknown(source_pc=left.pc) for _ in range(min(len(left.stack), len(right.stack))))
    left_vars = dict(left.variables)
    right_vars = dict(right.variables)
    variables = tuple(
        (varid, _merge_value(left_vars[varid], right_vars[varid], left.pc))
        for varid in sorted(left_vars.keys() & right_vars.keys())
    )
    return _EmulationState(
        pc=left.pc,
        acc=_merge_value(left.acc, right.acc, left.pc),
        stack=stack,
        variables=variables,
        last_predicate=(
            left.last_predicate if left.last_predicate == right.last_predicate else None
        ),
    )


def _event_value_key(value: StackVal) -> tuple[str, int | None]:
    if isinstance(value, Concrete):
        return "concrete", value.value & 0xFFFF
    if isinstance(value, VarRef):
        return "var", value.varid
    return "unknown", None


def emulate(
    script: StackVMScript,
    slot_idx: int,
    local_args: tuple[StackVal, ...] | None = None,
    *,
    initial_variables: Mapping[int, int | StackVal] | None = None,
    callcond_evaluator: Callable[[int, tuple[StackVal, ...]], int | None] | None = None,
) -> SlotTransitions:
    """Extract reachable events with branch-aware abstract interpretation.

    ``CALLCOND 0x0D`` creates a fresh sixteen-word local frame for the called
    bank-slot script.  Passing ``local_args`` seeds that frame at variables
    ``0xF0..0xFF`` and zero-fills omitted words, matching
    ``LuxMoveVM_ExecuteBankSlotScript``.  A root script leaves the frame
    unknown by passing ``None`` (the compatibility/default behavior).
    """
    out = SlotTransitions(slot_idx=slot_idx, bytecode_offset=script.bytecode_offset)
    by_pc = {inst.pc: inst for inst in script.instructions}
    seeded_variables: dict[int, StackVal] = {}
    for varid, value in (initial_variables or {}).items():
        seeded_variables[int(varid)] = (
            value if isinstance(value, (Concrete, VarRef, Unknown))
            else Concrete(int(value) & 0xFFFF, source_pc=script.bytecode_offset)
        )
    if local_args is not None:
        values = list(local_args[:16])
        values.extend(
            Concrete(0, source_pc=script.bytecode_offset)
            for _ in range(16 - len(values))
        )
        seeded_variables.update((0xF0 + i, value) for i, value in enumerate(values))
    initial = _EmulationState(
        script.bytecode_offset,
        Unknown(),
        (),
        variables=tuple(sorted(seeded_variables.items())),
    )
    states: dict[int, _EmulationState] = {initial.pc: initial}
    queue = deque([initial.pc])
    predicate_keys: set[tuple] = set()
    transition_keys: set[tuple] = set()
    effect_keys: set[tuple] = set()
    bank_script_keys: set[tuple] = set()
    call_sites: set[tuple[int, int]] = set()

    def enqueue(incoming: _EmulationState) -> None:
        current = states.get(incoming.pc)
        merged = incoming if current is None else _merge_states(current, incoming)
        if current != merged:
            states[incoming.pc] = merged
            queue.append(incoming.pc)

    while queue:
        state = states[queue.popleft()]
        inst = by_pc.get(state.pc)
        if inst is None:
            continue
        op = inst.opcode
        acc = state.acc
        stack = list(state.stack)
        variables = dict(state.variables)
        last_predicate = state.last_predicate
        branch_value: StackVal = Unknown(source_pc=inst.pc)

        if op == 0x01:
            count = inst.imm_u16 or 0
            if count:
                stack.extend(Unknown(source_pc=inst.pc) for _ in range(count + 1))
        elif op in (0x03, 0x04, 0x09, 0x0B, 0x2A):
            acc = Concrete(inst.imm_u16 or 0, source_pc=inst.pc)
        elif op == 0x0A:
            varid = inst.imm_u16 or 0
            acc = variables.get(varid, VarRef(varid, source_pc=inst.pc))
        elif op in (0x12, 0x13):
            varid = inst.imm_u16 or 0
            old = variables.get(varid, VarRef(varid, source_pc=inst.pc))
            acc = old
            if isinstance(old, Concrete):
                delta = 1 if op == 0x12 else -1
                variables[varid] = Concrete((old.value + delta) & 0xFFFF, inst.pc)
            else:
                variables.pop(varid, None)
        elif op == 0x19:
            acc = _pop_value(stack, inst.pc)
            variables[inst.imm_u16 or 0] = acc
        elif op in (0x1A, 0x1B, 0x1C, 0x1D, 0x1E):
            varid = inst.imm_u16 or 0
            rhs = _pop_value(stack, inst.pc)
            lhs = variables.get(varid, VarRef(varid, source_pc=inst.pc))
            variables[varid] = _fold_binary(op - 0x0E, lhs, rhs, inst.pc)
            acc = Unknown(source_pc=inst.pc)
        elif op == 0x26:
            stack.append(acc)
        elif op == 0x27:
            acc = _pop_value(stack, inst.pc)
        elif op in (0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x14, 0x15, 0x17, 0x18,
                    0x1F, 0x20, 0x21, 0x22, 0x23, 0x24):
            right, left = _pop_value(stack, inst.pc), _pop_value(stack, inst.pc)
            acc = _fold_binary(op, left, right, inst.pc)
        elif op == 0x11:
            value = _pop_value(stack, inst.pc)
            acc = Concrete((-_signed_short(value.value)) & 0xFFFF, inst.pc) if isinstance(value, Concrete) else Unknown(inst.pc)
        elif op == 0x16:
            value = _pop_value(stack, inst.pc)
            acc = Concrete(int(value.value == 0), inst.pc) if isinstance(value, Concrete) else Unknown(inst.pc)
        elif op == 0x25:
            fn_idx = inst.imm_b0 or 0
            argc = inst.imm_b1 or 0
            args = list(reversed([_pop_value(stack, inst.pc) for _ in range(argc)]))
            args_key = tuple(_event_value_key(value) for value in args)
            call_sites.add((inst.pc, fn_idx))
            # State at a PC only becomes less specific as branches join. Replace
            # an earlier event from this call site so a transient path value
            # cannot survive after the fixed-point state becomes unknown.
            out.predicates[:] = [event for event in out.predicates if event.source_pc != inst.pc]
            out.transitions[:] = [event for event in out.transitions if event.source_pc != inst.pc]
            out.effects[:] = [event for event in out.effects if event.source_pc != inst.pc]
            out.bank_scripts[:] = [event for event in out.bank_scripts if event.source_pc != inst.pc]
            predicate_keys = {key for key in predicate_keys if key[0] != inst.pc}
            transition_keys = {key for key in transition_keys if key[0] != inst.pc}
            effect_keys = {key for key in effect_keys if key[0] != inst.pc}
            bank_script_keys = {key for key in bank_script_keys if key[0] != inst.pc}
            if fn_idx in CALLCOND_EVAL_IF:
                last_predicate = (fn_idx, tuple(args), inst.pc)
                key = (inst.pc, fn_idx, args_key)
                if key not in predicate_keys:
                    predicate_keys.add(key)
                    out.predicates.append(PredicateEvent(fn_idx, args, inst.pc))
            elif fn_idx in CALLCOND_TRANSITION_AUTHOR:
                predicate = None
                predicate_key = None
                if last_predicate is not None:
                    pred_fn, pred_args, pred_pc = last_predicate
                    predicate = PredicateEvent(pred_fn, list(pred_args), pred_pc)
                    predicate_key = (pred_pc, pred_fn, tuple(_event_value_key(v) for v in pred_args))
                key = (inst.pc, fn_idx, args_key, predicate_key)
                if key not in transition_keys:
                    transition_keys.add(key)
                    out.transitions.append(TransitionEvent(fn_idx, args, inst.pc, predicate))
                last_predicate = None
            elif fn_idx in CALLCOND_EFFECT_DISPATCH:
                key = (inst.pc, fn_idx, args_key)
                if key not in effect_keys:
                    effect_keys.add(key)
                    out.effects.append(EffectEvent(fn_idx, args, inst.pc))
            elif fn_idx in CALLCOND_EXEC_BANK_SLOT | CALLCOND_SCHEDULE_TRANSITION:
                key = (inst.pc, args_key)
                if key not in bank_script_keys:
                    bank_script_keys.add(key)
                    out.bank_scripts.append(BankScriptEvent(fn_idx, args, inst.pc))
            resolved_result = (
                callcond_evaluator(fn_idx, tuple(args))
                if callcond_evaluator is not None else None
            )
            acc = (
                Concrete(resolved_result & 0xFFFF, source_pc=inst.pc)
                if resolved_result is not None else Unknown(source_pc=inst.pc)
            )
        elif op in (0x28, 0x29):
            branch_value = _pop_value(stack, inst.pc)

        if inst.push_flag and op not in (0x02, 0x05, 0x06, 0x07, 0x08):
            stack.append(acc)

        variables_tuple = tuple(sorted(variables.items()))
        fallthrough = inst.pc + inst.length
        if op in (0x03, 0x04, 0x2A):
            successors = (script.bytecode_offset + (inst.imm_u16 or 0),)
        elif op in (0x28, 0x29):
            target = script.bytecode_offset + (inst.imm_u16 or 0)
            if isinstance(branch_value, Concrete):
                nonzero = (branch_value.value & 0xFFFF) != 0
                taken = (not nonzero) if op == 0x28 else nonzero
                successors = (target,) if taken else (fallthrough,)
            else:
                successors = (fallthrough, target)
        elif op in (0x02, 0x05, 0x06, 0x07, 0x08):
            successors = ()
        else:
            successors = (fallthrough,)
        for successor in successors:
            successor_predicate = last_predicate
            if last_predicate is not None and op in (0x28, 0x29):
                target = script.bytecode_offset + (inst.imm_u16 or 0)
                predicate_true = successor != target if op == 0x28 else successor == target
                if not predicate_true:
                    successor_predicate = None
            enqueue(_EmulationState(
                successor,
                acc,
                tuple(stack),
                variables_tuple,
                successor_predicate,
            ))

    for _, fn_idx in call_sites:
        out.callcond_summary[fn_idx] = out.callcond_summary.get(fn_idx, 0) + 1
    # Predicate attribution remains the established source-order heuristic.
    # It is deliberately separate from CFG stack evaluation so a conservative
    # state join cannot erase labels used by graph/UI consumers.
    legacy_predicates = {
        event.source_pc: event.predicate
        for event in _emulate_linear(script, slot_idx).transitions
        if event.predicate is not None
    }
    for event in out.transitions:
        if event.predicate is None:
            event.predicate = legacy_predicates.get(event.source_pc)
    return out


def decode_lux_fp16_literal(value: int) -> float:
    """Decode LuxMoveVM's packed half-float literal into Python float."""
    value &= 0xFFFF
    signed_value = value - 0x10000 if value & 0x8000 else value
    if signed_value == 0:
        return 0.0
    sign_bit = 0x80000000 if signed_value < 0 else 0
    mag = abs(signed_value)
    bits = ((mag & 0x7C00) * 0x2000 + 0x38000000) | ((mag & 0x03FF) << 13) | sign_bit
    return struct.unpack("<f", struct.pack("<I", bits))[0]


# Kept for existing downstream imports while new analysis code uses the public
# name above.  This is an API compatibility alias, not a second decoder.
_decode_lux_fp16_literal = decode_lux_fp16_literal


# ---------------------------------------------------------------------------
# Input-condition decoder (predicate -> human-readable input string)
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class DecodedInput:
    """A human-readable rendering of an EvaluateIfOpcode predicate."""
    text: str
    kind: str              # "buttons", "direction", "frame", "stance", "complex", "indirect", ...
    raw_args: tuple = ()   # For UI to show on hover

    def __str__(self) -> str:
        return self.text


def _decode_button_mask(mask: int) -> str:
    """Render a button-mask bitfield as e.g. 'A+B' or 'K'."""
    buttons = []
    if mask & 0x0001: buttons.append("A")
    if mask & 0x0002: buttons.append("B")
    if mask & 0x0004: buttons.append("K")
    if mask & 0x0008: buttons.append("G")
    if not buttons:
        return ""
    return "+".join(buttons)


def _decode_direction(mask: int) -> str:
    """Render dwCurrentInputMask direction bits as numpad notation."""
    dir_bits = mask & INPUT_DIR_MASK
    np = INPUT_DIRECTION_COMBO_TO_NUMPAD.get(dir_bits)
    if np is None:
        return ""
    if np == 5:
        return ""
    return str(np)


def _decode_input_mask(mask: int) -> str:
    """Render the full ushort mask as 'direction + buttons'."""
    parts = []
    d = _decode_direction(mask)
    b = _decode_button_mask(mask)
    if d: parts.append(d)
    if b: parts.append(b)
    return "".join(parts) if parts else "5"  # 5 = neutral


def _decode_motion_pattern(pattern: int) -> str:
    """Render a CheckMotionConditionFlags pattern."""
    if pattern == 0x8000:
        return "buttons:none"
    if pattern == 0x8001:
        return "buttons:any"
    if pattern == 0x8002:
        return "input:*"
    parts = []
    req_all = (pattern >> 8) & 0xF
    # Mask is 0x2F per CheckMotionConditionFlags @ 0x140312E30 (bits
    # 0,1,2,3,5). Bit 4 is intentionally not part of REQUIRED-ANY.
    req_any = pattern & 0x2F
    if req_all:
        names = [n for b, n in HISTORY_INPUT_BITS.items() if (req_all & b) and b < 0x10]
        if names:
            parts.append("+".join(names))
        else:
            parts.append(f"all:0x{req_all:02X}")
    if req_any:
        names = [n for b, n in HISTORY_INPUT_BITS.items() if req_any & b]
        if names:
            parts.append("any:" + "|".join(names))
        else:
            parts.append(f"any:0x{req_any:02X}")
    return "(" + ";".join(parts) + ")" if parts else f"motion:0x{pattern:04X}"


def _decode_direction_condition_mask(mask: int) -> str:
    """Render the side-relative direction mask used at +0x2178/+0x2180."""
    known = mask & 0x0F
    names = [name for bit, name in DIRECTION_CONDITION_BITS.items() if known & bit]
    unknown = mask & ~0x0F
    if unknown:
        names.append(f"mask:0x{unknown:X}")
    return "|".join(names) if names else "neutral"


def _decode_raw_direction_nibble_mask(mask: int) -> str:
    """Render a bit-test against the compact word's direction nibble."""
    known = mask & 0x0F
    names = [name for bit, name in RAW_DIRECTION_NIBBLE_BITS.items() if known & bit]
    unknown = mask & ~0x0F
    if unknown:
        names.append(f"mask:0x{unknown:X}")
    return "|".join(names) if names else "neutral"


def _decode_direction_id(value: int) -> str:
    """Render the decoded-direction ID comparisons used by selectors 1/2."""
    if value == 5:
        return "*"
    if value == 10:
        return "any"
    return DECODED_DIRECTION_ID_TO_NUMPAD.get(value, f"id:0x{value:X}")


def decode_predicate(pred: Optional[PredicateEvent]) -> DecodedInput:
    """Try to render an EvaluateIfOpcode predicate as a human-readable
    input string. Falls back to a raw description on anything unknown.
    """
    if pred is None:
        return DecodedInput("(unconditional)", "always")

    sub = pred.sub_opcode
    if sub is None:
        return DecodedInput("(predicate is indirect)", "indirect")

    args = pred.args
    # All args after the sub-op kind:
    rest = [a.value if isinstance(a, Concrete) else None for a in args[1:]]

    if sub == 0x01:                                # primary raw direction nibble
        if rest and rest[0] is not None:
            d = _decode_raw_direction_nibble_mask(rest[0])
            return DecodedInput(f"raw-dir:any({d})", "direction", tuple(rest))
        return DecodedInput("(indirect raw direction nibble)", "indirect")

    if sub == 0x02:                                # primary side-decoded direction sequence
        if rest and rest[0] is not None:
            return DecodedInput(f"side-dir:{_decode_direction_id(rest[0] & 0xFFF)}",
                                "direction", tuple(rest))
        return DecodedInput("(indirect side-direction sequence)", "indirect")

    if sub == 0x03:                                # side-direction mask vs +0x2178
        if rest and rest[0] is not None:
            d = _decode_direction_condition_mask(rest[0])
            return DecodedInput(f"dir:any({d})", "direction", tuple(rest))
        return DecodedInput("(indirect direction mask)", "indirect")

    if sub == 0x27:                                # raw secondary direction nibble vs +0x216c
        if rest and rest[0] is not None:
            d = _decode_raw_direction_nibble_mask(rest[0])
            return DecodedInput(f"alt-dir:any({d})", "direction", tuple(rest))
        return DecodedInput("(indirect alternate direction nibble)", "indirect")

    if sub == 0x29:                                # secondary side-direction mask vs +0x2180
        if rest and rest[0] is not None:
            d = _decode_direction_condition_mask(rest[0])
            return DecodedInput(f"alt-dir:any({d})", "direction", tuple(rest))
        return DecodedInput("(indirect alternate direction mask)", "indirect")

    if sub in (0x24, 0x25, 0x13AE, 0x13AF):        # multi-arg nibble matcher
        # 0x24 / 0x25 are in the small-id table (0x00..0x9D); 0x13AE /
        # 0x13AF are side-aware variants in the secondary table
        # (0x138A..0x13E0). Both share the nibble-encoded multi-arg
        # shape — first reviewer pass incorrectly flagged 0x13AE/0x13AF
        # as dead code; they're real and common.
        # Each arg has top nibble = source and bottom 12 bits = test value:
        # 1 = side-decoded direction ID (+0x2170)
        # 2 = decoded direction ID (+0x215C)
        # 3 = side-direction bitmask (+0x2178)
        # 4 = raw direction nibble (+0x2164)
        # Multi-arg = AND of all sub-tests.
        inputs = []
        secondary = sub in (0x25, 0x13AF)
        prefix = "alt-" if secondary else ""
        for v in rest:
            if v is None:
                return DecodedInput("(indirect direction condition)", "indirect")
            nibble = (v >> 12) & 0xF
            val = v & 0xFFF
            if nibble == 4:
                inputs.append(f"{prefix}raw-dir:any({_decode_raw_direction_nibble_mask(val)})")
            elif nibble == 3:
                # +0x2178 is the side-relative direction-condition mask
                # produced by g_awLuxInputDirectionMaskByDecodedId.
                inputs.append(f"{prefix}dir:any({_decode_direction_condition_mask(val)})")
            elif nibble == 1:
                inputs.append(f"{prefix}side-dir:{_decode_direction_id(val)}")
            elif nibble == 2:
                inputs.append(f"{prefix}raw-dir:{_decode_direction_id(val)}")
            else:
                inputs.append(f"?0x{v:04X}")
        return DecodedInput("+".join(inputs) if inputs else "(empty)",
                            "direction", tuple(rest))

    if sub == 0x26:                                # secondary decoded direction ID
        if rest and rest[0] is not None:
            return DecodedInput(f"alt-raw-dir:{_decode_direction_id(rest[0] & 0xFFF)}",
                                "direction", tuple(rest))
        return DecodedInput("(indirect alternate decoded direction)", "indirect")

    if sub == 0x28:                                # secondary side-decoded direction ID
        if rest and rest[0] is not None:
            return DecodedInput(f"alt-side-dir:{_decode_direction_id(rest[0] & 0xFFF)}",
                                "direction", tuple(rest))
        return DecodedInput("(indirect alternate side direction)", "indirect")

    if sub in (0x05, 0x06, 0x20, 0x92, 0x93):      # compact-input history check
        if rest and rest[0] is not None:
            return DecodedInput(_decode_motion_pattern(rest[0]),
                                "buttons", tuple(rest))
        return DecodedInput("(history input ?)", "indirect")

    if sub == 0x13:
        # Native EvaluateIfOpcode case 0x13 converts both authored operands
        # from signed degrees to turns, wraps them to [-0.5, +0.5], and
        # compares them with the horizontal opponent-relative bearing at
        # +0x15A4. It does not inspect the separate pose pitch/roll fields.
        # Voldo's Ukemi dispatcher uses [-90, +90] to distinguish opponent-at-
        # head-end from opponent-at-feet-end grounded orientations.
        if len(rest) >= 2 and rest[0] is not None and rest[1] is not None:
            return DecodedInput(
                f"orientation [{_signed_short(rest[0])}\N{DEGREE SIGN}.."
                f"{_signed_short(rest[1])}\N{DEGREE SIGN}]",
                "orientation", tuple(rest))
        return DecodedInput("(orientation window)", "orientation")

    if sub in (0x08, 0x42, 0x5D, 0x94, 0x13BB, 0x13BC, 0x13BF):
        # frame-window predicates
        if len(rest) >= 2 and rest[0] is not None and rest[1] is not None:
            return DecodedInput(
                f"frame [{_signed_short(rest[0])}..{_signed_short(rest[1])}]",
                "frame", tuple(rest))
        return DecodedInput("(frame window)", "frame")

    if sub == 0x0E:                                # current move id == arg
        if rest and rest[0] is not None:
            return DecodedInput(f"from move 0x{rest[0]:04X}",
                                "from-move", tuple(rest))
        return DecodedInput("(indirect from-move)", "from-move")

    if sub == 0x6B:                                # stance @ +0x250
        if rest and rest[0] is not None:
            return DecodedInput(f"stance:0x{rest[0]:04X}",
                                "stance", tuple(rest))
        return DecodedInput("(indirect stance)", "stance")

    if sub in (0x2B, 0x2C, 0x84):                  # command-input system
        # Calls LuxBattle_EvaluateMoveTransitionConditions(chara, args[1], mode)
        # where args[1] indexes into per-chara command table at chara+0x971D8.
        # The command table is loaded at chara-init from one of the per-chara
        # data files (probably .dat) — we don't yet parse it. Render as a
        # raw command ID and let UI map to canonical notation when we know.
        if rest and rest[0] is not None:
            return DecodedInput(f"cmd:0x{rest[0]:04X}", "command",
                                tuple(v for v in rest if v is not None))
        return DecodedInput("(command)", "command")

    if sub == 0x14:                                # X-range band test
        if len(rest) >= 2 and rest[0] is not None and rest[1] is not None:
            return DecodedInput(
                f"range [{_signed_short(rest[0])}..{_signed_short(rest[1])}]",
                "range", tuple(rest))
        return DecodedInput("(indirect range)", "range")

    # State-read predicates: read a lane/chara state field and JZ/JNZ on it.
    # The agent report at lines around case 9/0x10/0x11/0x54 confirmed these
    # are active-move state fields (chara->field_0x455a0 + N). They're auto-
    # transition triggers, not user input — show as "auto:" so the UI can
    # distinguish them from input-driven transitions.
    if sub in (0x09, 0x10, 0x11, 0x12, 0x54):
        return DecodedInput(f"auto:state(+0x{0x24 + (sub - 9) * 2:02X})",
                            "auto", tuple(v for v in rest if v is not None))

    return DecodedInput(f"if[0x{sub:04X}]", "other",
                        tuple(v for v in rest if v is not None))
