"""
SC6 MoveVM stack-bytecode disassembler.

Each move slot in a `.khd` (FLuxMoveBank) has a byte-coded "stack VM"
bytecode pointed at by `FLuxMoveBankSlotView.dwBytecodeOffset_38`.
This script walks that byte stream into a printable opcode listing.

The bytecode is consumed by `LuxMoveVM_ExecuteBytecode @ 0x1402E5A30`
(stack VM) — NOT to be confused with `LuxMoveVM_ExecuteAndDumpOpcode @
0x140365900` (u32-cell VM, used by AI command players).

Opcode encoding (verified against the dispatch switch):
    Each opcode byte has a 7-bit OPCODE in the low 7 bits and a "push
    ACC after" flag in bit 0x80. Most opcodes are 1 byte; a few take a
    BIG-ENDIAN u16 immediate (3 bytes total).

    0x00            NOP / pad                 (1 byte)
    0x01 BE16       FRAME     <local_count>   (3 bytes)
    0x02            RET2                      (1 byte)
    0x03 BE16       JMP_ABS                   (3 bytes)
    0x04 BE16       JMP_ABS                   (3 bytes)
    0x05            RET / POP_RET             (1 byte)
    0x06            RET (alt)                 (1 byte)
    0x07            RETBRK                    (1 byte)
    0x08            BRK                       (1 byte)
    0x09 BE16       SET_ACC_U16               (3 bytes; +0x80 = push)
    0x0A BE16       LOAD_VAR <varid>          (3 bytes)
    0x0B BE16       SET_ACC_U16               (3 bytes; +0x80 = push)
    0x0C            ADD  (a + b)              (1 byte)
    0x0D            SUB  (a - b)              (1 byte)
    0x0E            MUL                       (1 byte)
    0x0F            DIV                       (1 byte)
    0x10            MOD                       (1 byte)
    0x11            NEG                       (1 byte)
    0x12 BE16       POSTINC <varid>           (3 bytes)
    0x13 BE16       POSTDEC <varid>           (3 bytes)
    0x14            AND                       (1 byte)
    0x15            OR                        (1 byte)
    0x16            LNOT                      (1 byte)
    0x17            SHL                       (1 byte)
    0x18            SAR                       (1 byte)
    0x19 BE16       STORE_VAR <varid>         (3 bytes)
    0x1A BE16       ADD_VAR  <varid>          (3 bytes)
    0x1B BE16       SUB_VAR  <varid>          (3 bytes)
    0x1C BE16       MUL_VAR  <varid>          (3 bytes)
    0x1D BE16       DIV_VAR  <varid>          (3 bytes)
    0x1E BE16       MOD_VAR  <varid>          (3 bytes)
    0x1F            EQ                        (1 byte)
    0x20            NE                        (1 byte)
    0x21            LT                        (1 byte)
    0x22            LE                        (1 byte)
    0x23            GT                        (1 byte)
    0x24            GE                        (1 byte)
    0x25 u8,u8      CALLCOND <fn>,<argc>      (3 bytes)
    0x26            PUSH_ACC                  (1 byte)
    0x27            POP_ACC                   (1 byte)
    0x28 BE16       JZ  <dest>                (3 bytes)
    0x29 BE16       JNZ <dest>                (3 bytes)
    0x2A BE16       JMP <dest>                (3 bytes)
    0x2B..0x3C      NOP / unused              (1 byte)

Var-id addressing (LOAD/STORE/POSTINC/POSTDEC):
    varid < 0xF0   -> GLOBAL[varid]   (per-character 240-entry i16 array)
    0xF0..0xFF     -> LOCAL[varid-F0] (16 i16 args copied from caller)
    0x100+         -> STACK[varid-100] (stack ring; persist across CALLCONDs)

CALLCOND function table is at `g_LuxMoveVM_OpcodeIfDispatchTable @
0x143E83A90`. Known indices:
    0x05/06/07/08  TransitionAuthor  (writes lane[+0x5A] = next move ID)
    0x09           LatchCharaStateFlag
    0x0D           ExecuteBankSlotScript  (nested sub-slot bytecode)
    0x15           ScheduleTransitionScript  (writes lane[+0xB4])
    0x16           DrainScheduledTransition
    0x17           SaveStagedTransition
    0x18           RestoreStagedTransition
    0x1A           ClearScheduledTransition
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Optional


# Opcode metadata. (mnemonic, immediate-format).
# 'BE16' = big-endian u16 immediate; 'BB' = two u8s (CALLCOND);
# None / "" = no immediate.
_OPCODE_TABLE: dict[int, tuple[str, str]] = {
    0x00: ("NOP",         ""),
    0x01: ("FRAME",       "BE16"),
    0x02: ("RET2",        ""),
    # Native LuxMoveVM_ExecuteBytecode groups 0x03, 0x04, and 0x2A in
    # the same absolute-jump case.  Earlier tooling treated 0x03/0x04 as
    # literals, which created impossible fall-through paths.
    0x03: ("JMP_ABS",     "BE16"),
    0x04: ("JMP_ABS",     "BE16"),
    0x05: ("RET",         ""),
    0x06: ("RET",         ""),
    0x07: ("RETBRK",      ""),
    0x08: ("BRK",         ""),
    0x09: ("SET_ACC_U16", "BE16"),
    0x0A: ("LOAD_VAR",    "BE16"),
    0x0B: ("SET_ACC_U16", "BE16"),
    0x0C: ("ADD",         ""),
    0x0D: ("SUB",         ""),
    0x0E: ("MUL",         ""),
    0x0F: ("DIV",         ""),
    0x10: ("MOD",         ""),
    0x11: ("NEG",         ""),
    0x12: ("POSTINC",     "BE16"),
    0x13: ("POSTDEC",     "BE16"),
    0x14: ("AND",         ""),
    0x15: ("OR",          ""),
    0x16: ("LNOT",        ""),
    0x17: ("SHL",         ""),
    0x18: ("SAR",         ""),
    0x19: ("STORE_VAR",   "BE16"),
    0x1A: ("ADD_VAR",     "BE16"),
    0x1B: ("SUB_VAR",     "BE16"),
    0x1C: ("MUL_VAR",     "BE16"),
    0x1D: ("DIV_VAR",     "BE16"),
    0x1E: ("MOD_VAR",     "BE16"),
    0x1F: ("EQ",          ""),
    0x20: ("NE",          ""),
    0x21: ("LT",          ""),
    0x22: ("LE",          ""),
    0x23: ("GT",          ""),
    0x24: ("GE",          ""),
    0x25: ("CALLCOND",    "BB"),
    0x26: ("PUSH_ACC",    ""),
    0x27: ("POP_ACC",     ""),
    0x28: ("JZ",          "BE16"),
    0x29: ("JNZ",         "BE16"),
    0x2A: ("JMP_ABS",     "BE16"),
}

# Opcodes that fall into the "NOP / pad" no-op default in the dispatcher
# (cases 0, 0x2B..0x3C in the switch statement). Treated as 1-byte no-ops.
for _op in [0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
            0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C]:
    _OPCODE_TABLE[_op] = ("NOP",  "")

# Opcodes that terminate execution. The walker stops after one of these
# AND the high bit (0x80 push-flag) is clear.
TERMINATOR_OPCODES = {0x02, 0x05, 0x06, 0x07, 0x08}


# CALLCOND function-index labels (canonical, parsed from the dispatch
# table at `g_LuxMoveVM_OpcodeIfDispatchTable @ 0x143E83A90`). 38 entries.
#
# Several indices share the same function pointer (engine designers used
# the same handler with different "operator semantic" tags).
CALLCOND_NAMES: dict[int, str] = {
    0x00: "EvaluateIfOpcode",          # 0x1403732F0 — IF-predicate evaluator
    0x01: "EvaluateIfOpcode",          # 0x1403732F0 (alias)
    0x02: "DispatchEffectOp",          # 0x140376B20 — effect-system opcode dispatcher
    0x03: "DispatchEffectOp",          # 0x140376B20 (alias)
    0x04: "RegisterEffectOpDedup",     # 0x1402FD4A0
    0x05: "TransitionAuthor_05",       # 0x1402FCB80 — writes lane[+0x5A] = MoveID
    0x06: "TransitionAuthor_06",       # 0x1402FCB90
    0x07: "TransitionAuthor_07",       # 0x1402FCC10
    0x08: "TransitionAuthor_08",       # 0x1402FCC20
    0x09: "LatchCharaStateFlag",       # 0x1402FD720
    0x0A: "OpcodeIf_0A",                # 0x1402FD7D0
    0x0B: "NullStub",                   # 0x1402D9BF0 — no-op
    0x0C: "OpcodeIf_0C",                # 0x1402FDA50
    0x0D: "ExecuteBankSlotScript",     # 0x1402FCC30 — NESTED call to another slot bytecode
    0x0E: "OpcodeIf_0E",                # 0x1402FD3A0
    0x0F: "OpcodeIf_0F",                # 0x1402FD3C0
    0x10: "OpcodeIf_10",                # 0x1402FD3E0
    0x11: "OpcodeIf_11",                # 0x1402FD400
    0x12: "NullStub",                   # 0x1402D9BF0 — no-op alias
    0x13: "NullStub",                   # 0x1402D9BF0 — no-op alias
    0x14: "WriteCharaStateShort",      # 0x1402FDA30 — *(short*)(chara+0x197C + args[0]*2) = args[1]
    0x15: "ScheduleTransitionScript",  # 0x1402FCD30 — writes lane[+0xB4] (DEFERRED multi-hit)
    0x16: "DrainPendingTransition",    # 0x1402FCDE0 — drain lane[+0xB4], call TransitionToMove
    0x17: "OpcodeIf_17",                # 0x1402FCF10
    0x18: "OpcodeIf_18",                # 0x1402FCF60
    0x19: "RegisterEffectOpDedup",     # 0x1402FD4A0 (alias)
    0x1A: "ClearPendingTransition",    # 0x1402FCDC0 — clear lane[+0xB4] sentinel
    0x1B: "OpcodeIf_1B",                # 0x1402FD420
    0x1C: "OpcodeIf_1C",                # 0x1402FD440
    0x1D: "OpcodeIf_1D",                # 0x1402FD460
    0x1E: "OpcodeIf_1E",                # 0x1402FD480
    0x1F: "OpcodeIf_1B",                # alias
    0x20: "OpcodeIf_1C",                # alias
    0x21: "OpcodeIf_1D",                # alias
    0x22: "OpcodeIf_1E",                # alias
    0x23: "GetRandWeightedIndex",      # 0x1402E58B0
    0x24: "NullStub",                   # 0x1402D9BF0 alias
    0x25: "EvaluateIfOpcodeWithHeader", # 0x1402E5830
    # 0x26 discovered 2026-05-16 by reading the CALLCOND dispatch table
    # past where the earlier plate said it ended. Wraps
    # LuxMoveVM_SetActiveMoveSlot @ 0x140300C70 — takes one short arg
    # (variant index 0..5) and switches the LANE's primary slot's
    # active attack cell to that variant. This is THE mechanism for
    # multi-hit moves: a multi-variant slot lets the bytecode call
    # CALLCOND 0x26 between hits to swap which cell is "active" for
    # damage detection, while the animation keeps playing. Slots that
    # use this are typically anim=0xFFFF trampolines (sentinel
    # animation) that dispatch back to the primary slot after the
    # switch.
    0x26: "SetActiveMoveSlot",         # 0x140300DF0 (wrapper) -> 0x140300C70
}


def _format_var(varid: int) -> str:
    """Human-readable variable address for LOAD/STORE/etc."""
    if varid < 0xF0:
        return f"GLOBAL[0x{varid:02X}]"
    if varid < 0x100:
        return f"LOCAL[{varid - 0xF0}]"
    return f"STACK[{varid - 0x100}]"


@dataclass
class StackVMInstruction:
    """One decoded stack-VM instruction."""
    pc: int                  # Offset within the bytecode (cursor)
    raw: bytes               # Raw bytes (1 or 3 bytes typically)
    opcode_byte: int         # Full byte, including 0x80 push-flag
    opcode: int              # opcode_byte & 0x7F
    push_flag: bool          # opcode_byte & 0x80
    mnemonic: str
    imm_u16: Optional[int] = None     # BE u16 immediate (LOAD/STORE/FRAME/JMP/etc.)
    imm_b0: Optional[int] = None      # First byte of BB (CALLCOND fnIdx)
    imm_b1: Optional[int] = None      # Second byte of BB (CALLCOND argc)

    @property
    def length(self) -> int:
        return len(self.raw)

    def render(self) -> str:
        suffix = " ; +PUSH" if self.push_flag else ""
        if self.opcode == 0x25:  # CALLCOND
            fn_name = CALLCOND_NAMES.get(self.imm_b0 or 0, f"fn{self.imm_b0:02X}")
            return f"CALLCOND  0x{self.imm_b0:02X} ({fn_name}), argc={self.imm_b1}{suffix}"
        if self.opcode in (0x0A, 0x19, 0x12, 0x13, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E):
            return f"{self.mnemonic:<10} {_format_var(self.imm_u16 or 0)}{suffix}"
        if self.opcode in (0x01, 0x03, 0x04, 0x09, 0x0B, 0x28, 0x29, 0x2A):
            return f"{self.mnemonic:<10} 0x{self.imm_u16:04X} ({self.imm_u16}){suffix}"
        return f"{self.mnemonic}{suffix}"


@dataclass
class StackVMScript:
    """A walked bytecode script for one slot."""
    bytecode_offset: int                       # bank-relative byte offset
    instructions: list[StackVMInstruction] = field(default_factory=list)
    truncated: bool = False                    # walker stopped before clean RET

    @property
    def length_bytes(self) -> int:
        if not self.instructions:
            return 0
        last = self.instructions[-1]
        return last.pc + last.length - self.instructions[0].pc

    @property
    def callcond_summary(self) -> dict[int, int]:
        """fn_index -> count, for quick fingerprinting."""
        out: dict[int, int] = {}
        for i in self.instructions:
            if i.opcode == 0x25 and i.imm_b0 is not None:
                out[i.imm_b0] = out.get(i.imm_b0, 0) + 1
        return out


def walk_stackvm(buf: bytes, start_off: int, max_bytes: int = 0x10000,
                  *, follow_jumps: bool = True) -> StackVMScript:
    """Walk one stack-VM bytecode script starting at byte offset `start_off`
    in `buf`.

    If `follow_jumps` is True (default), walks all REACHABLE bytes via
    JMP/JNZ/JZ targets — not just the linear prefix. Targets in JMP/JNZ/JZ
    are *absolute byte offsets relative to start_off* (the engine sets
    `dwPC = imm`). This matches what the engine actually executes.

    The walker stops following a path at the first terminator
    (RET/RET2/RETBRK/BRK). Unreachable bytes between explored paths
    are NOT decoded.
    """
    script = StackVMScript(bytecode_offset=start_off)
    end_cap = min(len(buf), start_off + max_bytes)

    visited_pc: set[int] = set()
    work: list[int] = [start_off]
    out_by_pc: dict[int, StackVMInstruction] = {}

    while work:
        pc = work.pop()
        # Walk forward from pc until terminator / already-visited
        while pc < end_cap:
            if pc in visited_pc:
                break
            visited_pc.add(pc)
            op_byte = buf[pc]
            opcode = op_byte & 0x7F
            push_flag = (op_byte & 0x80) != 0
            meta = _OPCODE_TABLE.get(opcode)
            if meta is None:
                script.truncated = True
                break
            mnemonic, imm_fmt = meta
            if imm_fmt == "":
                inst = StackVMInstruction(
                    pc=pc, raw=buf[pc : pc + 1],
                    opcode_byte=op_byte, opcode=opcode, push_flag=push_flag,
                    mnemonic=mnemonic,
                )
                next_pc = pc + 1
            elif imm_fmt == "BE16":
                if pc + 3 > end_cap:
                    script.truncated = True
                    break
                imm = (buf[pc + 1] << 8) | buf[pc + 2]
                inst = StackVMInstruction(
                    pc=pc, raw=buf[pc : pc + 3],
                    opcode_byte=op_byte, opcode=opcode, push_flag=push_flag,
                    mnemonic=mnemonic, imm_u16=imm,
                )
                next_pc = pc + 3
            elif imm_fmt == "BB":
                if pc + 3 > end_cap:
                    script.truncated = True
                    break
                inst = StackVMInstruction(
                    pc=pc, raw=buf[pc : pc + 3],
                    opcode_byte=op_byte, opcode=opcode, push_flag=push_flag,
                    mnemonic=mnemonic,
                    imm_b0=buf[pc + 1], imm_b1=buf[pc + 2],
                )
                next_pc = pc + 3
            else:
                raise AssertionError(f"unknown imm_fmt {imm_fmt!r}")
            out_by_pc[pc] = inst

            if opcode in TERMINATOR_OPCODES:
                break

            # Control-flow:
            # JMP (0x03/0x04/0x2A): unconditional, next_pc = start_off + imm
            # JNZ/JZ (0x28/0x29): conditional, can also fall through
            if follow_jumps and opcode in (0x03, 0x04, 0x2A):
                pc = start_off + inst.imm_u16  # absolute byte offset
                if pc >= end_cap or pc < start_off:
                    break
            elif follow_jumps and opcode in (0x28, 0x29):
                target = start_off + inst.imm_u16
                if target < end_cap and target >= start_off and target not in visited_pc:
                    work.append(target)
                pc = next_pc  # fall through path
            else:
                pc = next_pc

    # Emit instructions in PC order
    script.instructions = [out_by_pc[k] for k in sorted(out_by_pc)]
    return script
