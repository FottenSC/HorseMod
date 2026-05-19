"""
SC6 (Soulcalibur VI / "Project Lux") moveset binary file parsers.

Formats reverse-engineered from SoulcaliburVI.exe v2.31.

This module parses the per-character data files dumped from
`Battle/{hdr,mot,cpu,hit}/` inside the game's PAK:

    .khd  -> moveset bank (KH11 magic). Engine type: FLuxMoveBank.
             Contains 3 sub-arrays: attack cells, non-attack descriptors,
             event records.
    .mot  -> per-character motion / animation table (chr*.mot)
    .dtp  -> CPU AI personality + decision-weight table (cpuai*.dtp)
    .dat  -> hit-volume i16-tagged stream (atkhit / bodyhit / yararehit)
    .vtb  -> visual-tag / frame-event buffer (loaded from chara init params)
    .lpd  -> Lux pose-delta motion blend wrapper (contains inner "lpb" block)

References (Ghidra addresses against SoulcaliburVI.exe v2.31):
    LuxBattleChara_LoadMovesetEntries_AndBoneData  @ 0x140312040
    LuxMoveVM_UpdateMoveDataTable                  @ 0x14038F7D0
    LuxMoveVM_InitStaticMoveDataTable              @ 0x14038F6F0
    LuxMoveVM_LoadVTBFile                          @ 0x14038EFA0
    LuxBattle_BindLPDMotionData                    @ 0x1402F7670
    HgMotion_BindLPBData                           @ 0x14038B740
    Lux_KHitChk_DeserializeLinkedList              @ 0x14030C940
    LuxBattle_InitCpuPersonalityData               @ 0x140364950
    LuxBattle_ResolveAttackVsHurtboxMask22         @ 0x14033C100
    LuxBattle_ComputeHitReactionParams             @ 0x140343EE8
    LuxMoveData_GetEffectPropertyPtr               @ 0x140307550
    LuxMoveVM_ResolveBankSlot                      @ (looked up via xref chain)

Engine struct sizes (from Ghidra):
    FLuxMoveBank                48 bytes (THE .khd header)
    LuxBattleAttackCell        112 bytes (Section A entry)
    LuxBattleNonAttackMoveDescr  6 bytes (Section B entry)
    FLuxMoveBankSlotView        72 bytes (slot view, indexed by bank slot id)
    FLuxKHitNode               160 bytes (in-memory KHit linked-list node)
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Optional


# ----------------------------------------------------------------------
# Generic offset-table header (.mot / .dtp share this)
# ----------------------------------------------------------------------

@dataclass
class OffsetTableFile:
    """count + u32[count+1] offsets (last entry usually = file size)."""
    count: int
    offsets: list[int]
    sizes: list[int]
    raw: bytes = field(repr=False)

    def section(self, i: int) -> bytes:
        return self.raw[self.offsets[i] : self.offsets[i] + self.sizes[i]]


def parse_offset_table(data: bytes, *, expected_max_count: int = 1 << 20) -> OffsetTableFile:
    count = struct.unpack_from("<I", data, 0)[0]
    if count == 0 or count > expected_max_count:
        raise ValueError(f"implausible offset-table count: {count}")
    offs = list(struct.unpack_from(f"<{count+1}I", data, 4))
    sizes = []
    for i in range(count):
        a = offs[i]
        b = offs[i + 1] if i + 1 < len(offs) else len(data)
        sizes.append(b - a)
    return OffsetTableFile(count=count, offsets=offs[:count], sizes=sizes, raw=data)


def parse_mot(data: bytes) -> OffsetTableFile:
    """Parse a chr*.mot motion table."""
    return parse_offset_table(data)


def parse_dtp(data: bytes) -> OffsetTableFile:
    """Parse a cpuai*.dtp AI personality file."""
    return parse_offset_table(data)


# ----------------------------------------------------------------------
# .vtb  — visual-tag / frame-event buffer
# ----------------------------------------------------------------------

@dataclass
class VtbFile:
    version: int
    entry_count: int
    data_offset: int
    header_data: bytes
    entries: list[bytes]


def parse_vtb(data: bytes) -> VtbFile:
    """Parse a .vtb file. See LuxMoveVM_LoadVTBFile @ 0x14038EFA0."""
    if data[:3] != b"vtb":
        raise ValueError(f"bad VTB magic: {data[:4]!r}")
    _, _, version, n_entries, data_off, _ = struct.unpack_from("<6I", data, 0)
    if version != 0x1002:
        raise ValueError(f"unsupported VTB version: 0x{version:X}")
    if data_off < 0x18 or data_off > len(data):
        raise ValueError(f"bad VTB data_offset: 0x{data_off:X}")
    header_data = data[0x18:data_off]
    entries = [
        data[data_off + i * 0x84 : data_off + (i + 1) * 0x84]
        for i in range(n_entries)
    ]
    return VtbFile(version, n_entries, data_off, header_data, entries)


# ----------------------------------------------------------------------
# .lpd / .lpb  — pose-delta motion-blend
# ----------------------------------------------------------------------

@dataclass
class LpbBlock:
    version: int
    total_len: int
    sub_lens: list[int]
    raw: bytes = field(repr=False)


@dataclass
class LpdFile:
    n_sections: int
    off_magic: int
    off_data: int
    off_data_end: int
    inner: Optional[LpbBlock]


def parse_lpd(data: bytes) -> LpdFile:
    """Parse a .lpd container. See LuxBattle_BindLPDMotionData @ 0x1402F7670."""
    n, off_magic, off_data, off_end = struct.unpack_from("<4I", data, 0)
    if n != 3:
        raise ValueError(f"bad LPD section count: {n}")
    if data[off_magic : off_magic + 3] != b"lpb":
        raise ValueError(f"LPD inner magic not 'lpb' at 0x{off_magic:X}")
    inner_raw = data[off_data:off_end]
    inner = parse_lpb(inner_raw) if inner_raw else None
    return LpdFile(n, off_magic, off_data, off_end, inner)


def parse_lpb(data: bytes) -> LpbBlock:
    """Parse an inner LPB block. See HgMotion_BindLPBData @ 0x14038B740."""
    if data[:3] != b"lpb":
        raise ValueError(f"bad LPB magic: {data[:4]!r}")
    version = struct.unpack_from("<I", data, 0x10)[0]
    if version != 0x201:
        raise ValueError(f"unsupported LPB version: 0x{version:X}")
    total_len = struct.unpack_from("<I", data, 0x18)[0]
    sub_lens = list(struct.unpack_from("<7I", data, 0x20))
    return LpbBlock(version, total_len, sub_lens, data)


# ----------------------------------------------------------------------
# .khd  — FLuxMoveBank (engine type) with KH11 magic
# ----------------------------------------------------------------------
#
# The .khd file on disk is a serialised FLuxMoveBank (the engine's
# moveset container struct, 48 bytes). Its fields point at three
# sub-arrays inside the same file:
#
#     dwAttackCellArrayOffset     -> LuxBattleAttackCell[]      (Section A)
#     dwNonAttackDescTableOffset  -> LuxBattleNonAttackMoveDescr[] (Section B)
#     dwEventRecordTableOffset    -> "event records" (Section C — partly opaque)
#
# Verified by `LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100`
# which dereferences pBank->dwAttackCellArrayOffset and indexes into it
# at stride 0x70 to fetch an `LuxBattleAttackCell *`.

# --- Engine enums (verified from Ghidra + SC6ModdingDocs) ---

# ELuxBattleMoveEffectType — Z_Construct_UEnum_ELuxBattleMoveEffectType @ 0x14095BF20
MOVE_EFFECT_TYPE = {
    0:  "Throw",
    1:  "UnblockableAttack",
    2:  "BreakAttack",
    3:  "GuardImpact",
    4:  "SpecialStance",
    5:  "LethalHit",
    6:  "SoulCharge",
    7:  "SoulGaugeFull",
    8:  "SoulGaugeHalf",
    9:  "SoulGaugeQuarter",
    10: "ReversalEdge",
}

# ELuxBattleMoveCategory — Z_Construct_UEnum_LuxBattleMoveCategory @ 0x14095B160
MOVE_CATEGORY = {
    0:  "MainMoves",
    1:  "ReversalEdgeMoves",
    2:  "SoulGaugeMoves",
    3:  "HorizontalAttacks",       # A-button family
    4:  "VerticalAttacks",         # B-button family
    5:  "Kicks",                   # K-button family
    6:  "SimultaneousPressMoves",
    7:  "EightWayRunMoves",
    8:  "Throws",
    9:  "SpecialStance",
    10: "LethalHitMoves",
}

# LuxHitReactionState — defender's reaction-state code at chara+0x252.
# (4-byte enum; consumed by ProcessHitReactionState / UpdateHitVisualLean / AI.)
HIT_REACTION_STATE = {
    0x0: "None",
    0x1: "Hit",
    0x2: "BlockedLow",
    0x3: "BlockedHigh",
    0x4: "MutualHit_Loser",
    0x6: "Tech",
    0x8: "MutualHit_Winner",
    0x9: "AirHit",
    0xA: "MutualHit_Trade",
    0xB: "WallSplat",
    0xC: "Stagger",
}

# EYarareReactionId — per-VM-slot reaction id at vmCtx+0x2B30, range 0x01..0x50.
# Named groups (see SC6ModdingDocs/sc6/reaction-system.md for the full table).
YARARE_REACTION_NAMED = {
    0x03: "CrumpleFall",
    0x0E: "WallStaggerStart",
    0x0F: "WallStagger",
    0x10: "WallStaggerEnd",
    0x1B: "ShortBackStagger",
    0x1C: "StaggerVar_ResetIdle",
    0x1D: "WallStagger_Full",
    0x1E: "Knockdown",
    0x1F: "StandardLaunch",
    0x20: "SideStumble_LaunchVar",
    0x21: "StandardLaunch_Aerial",
    0x26: "SideStumblePoseCopy",
    0x27: "HardKnock",
    0x28: "NullReaction",
    0x29: "HitVsBlockBranch",
    0x2A: "PureStateSwap",
    0x2B: "GenericSideRng",
    0x2C: "LaunchAirCarry_A",
    0x2D: "LaunchAirCarry_B",
    0x32: "PickRingoutReaction",
    0x38: "StandardLaunch_C",
    0x3B: "BackBreaker",
    0x3C: "WallHit",
    0x3D: "WallHitVar",
    0x42: "BackBreakerCrit",
    0x43: "KnockdownLike",
    0x44: "QuickRise_A",
    0x45: "QuickRise_B",
    0x4F: "ParryRecovery",
    0x50: "GetUp_Terminal",
}


def yarare_name(rid: int) -> str:
    """Human-readable name for a yarare reaction id.

    Falls back to group label for ranges defined in the docs."""
    if rid in YARARE_REACTION_NAMED:
        return YARARE_REACTION_NAMED[rid]
    if 0x01 <= rid <= 0x1A:
        return "GenericLightHit"
    if 0x22 <= rid <= 0x27:
        return "MediumReaction"
    if 0x2E <= rid <= 0x31:
        return "RingoutFall"
    if 0x33 <= rid <= 0x37:
        return "HeavyReaction"
    if 0x39 <= rid <= 0x3A:
        return "HeavyReaction"
    if 0x3E <= rid <= 0x41:
        return "HeavyMidReaction"
    if 0x46 <= rid <= 0x4E:
        return "GetUp_ParryBreak"
    return f"YarareId_0x{rid:02X}"


# u64SlotMask -> dwMoveType (LuxMoveVM_TransitionToMove decode).
# Returns string label; called by LuxBattleAttackCell.move_type.
def decode_move_type(u64SlotMask: int) -> str:
    if (u64SlotMask & 0x7FF0003F800000) != 0:
        return "Strike"
    if (u64SlotMask & 0x33F0C0) != 0:
        return "Grab"
    return "Super_or_Other"   # Wire / Super / SC overrides need cell+0x34 / 0x0C bits


# u64SlotMask -> dwAnimKind (high/mid/low classification).
# Returns string label; matches the table in sc6/hitbox-system.md.
def decode_anim_kind(u64SlotMask: int) -> str:
    m = u64SlotMask
    if m & 0x1800000:           return "High"           # bits 23-24
    if m & 0x6000000:           return "Mid"            # bits 25-26
    if m & 0x1000008000000:     return "Low"            # bits 27, 48
    if m & 0x2000010000000:     return "SpecialMid"     # bits 28, 49
    if m & 0x4000020000000:     return "SpecialLow"     # bits 29, 50
    if m & 0x8100000000000:     return "Grab_A"         # bits 40, 51
    if m & 0x10200000000000:    return "Grab_B"         # bits 41, 52
    if m & 0x20400000000000:    return "Grab_C1"
    if m & 0x40800000000000:    return "Grab_C2"
    return "Neutral"


# KHit subclass tag (KHitBase+0x16 / hit-stream tag byte).
KHIT_TAG_NAMES = {
    0: "Sphere",
    1: "Area",
    2: "FixArea",
}

# Special passthrough-tag sentinel values seen in cells / non-attack descrs.
PASSTHROUGH_TAG_NAMES = {
    0xFFFD: "default-reaction",
    0xFFFF: "sentinel/none",
    0x0000: "zero",
}

# Range-byte sentinel: -127 (0x81 as signed byte) = "no constraint"
RANGE_SENTINEL_BYTE = -127


# --- Move-input button mask (used by BTN+TIME opcode 0x1xxxx in MoveVM bytecode) ---
#
# Verified from LuxMoveVM_ExecuteAndDumpOpcode @ 0x140365900 — the dumper
# emits one mnemonic per set bit when disassembling the opcode. Bit 0
# is the low bit; bit 15 is the high bit of the u16 button mask.
#
# Notation: SC6 uses NUMPAD-style direction notation.
#   _1 = down-back   _2 = down   _3 = down-forward
#   _4 = back        _5 = neutral _6 = forward
#   _7 = up-back     _8 = up     _9 = up-forward
#   _A / _B / _K = the three attack buttons (Horizontal / Vertical / Kick)
#   _G          = Guard
#   _C          = Charge / Critical Edge marker
#   _W          = "walking" indicator (held-direction modifier)
#   _NOGUARD    = unblockable-attack marker
BUTTON_MASK_BITS = [
    (0x0001, "_W"),
    (0x0002, "_1"),
    (0x0004, "_2"),
    (0x0008, "_3"),
    (0x0010, "_4"),
    (0x0020, "_5"),
    (0x0040, "_6"),
    (0x0080, "_7"),
    (0x0100, "_8"),
    (0x0200, "_9"),
    (0x0400, "_A"),
    (0x0800, "_B"),
    (0x1000, "_K"),
    (0x2000, "_G"),
    (0x4000, "_NOGUARD"),
    (0x8000, "_C"),
]


def decode_button_mask(mask: int) -> str:
    """Render a BTN+TIME opcode button-mask u16 as a numpad-style notation
    string (matches the LuxMoveVM dumper output).

    Examples:
        decode_button_mask(0x0040 | 0x0800)  # forward + B button
        -> '_6_B'

        decode_button_mask(0x0400 | 0x2000)  # A + Guard (GI input)
        -> '_A_G'
    """
    parts = [name for bit, name in BUTTON_MASK_BITS if mask & bit]
    leftover = mask & ~sum(b for b, _ in BUTTON_MASK_BITS)
    if leftover:
        parts.append(f"0x{leftover:X}")
    return "".join(parts) if parts else "(empty)"

# LuxBattleAttackCell.wU16AttackFlags (cell+0x32) bit definitions.
#
# Authoritative ELuxBattleAttackFlags enum from Ghidra (verified against
# LuxMoveVM_EvaluateMoveTransition @ 0x14033E140 and
# LuxBattle_ResolveAttackVsHurtboxMask22 @ 0x14033C100):
#
#   0x001  HighBlockable          — defender can block this from STANDING
#   0x002  LowBlockable           — defender can block this from CROUCHING
#   0x004  BlockBypass_GuardBreak — routes to result 7 against guard-broken
#                                    defender (the "Guard Break" / counter-
#                                    hit-special path)
#   0x008  LowAttack              — must crouch to block (low strike)
#   0x010  MidAttack              — any stance blocks if guarding
#   0x040  CrouchOnly             — attacker was crouching (move only valid
#                                    from crouch source)
#   0x080  HighAttack             — must stand-block; crouch ducks under
#                                    (sign bit of low byte)
#   0x100  Special_FlagX100       — special framing rule
#   0x200  Unblockable_GIImmune   — cannot be Guard Impacted. If NO block
#                                    bits are set this also means the move
#                                    is genuinely unblockable; if combined
#                                    with block bits it means "Break Attack"
#                                    (blockable but causes block-stagger
#                                    reaction 3 instead of normal 2).
#
# Bits 0x020 and 0x400..0x8000 still unidentified — left as 'bN' so callers
# don't read invented semantics into them.
#
# Standard SC6 community attack class is determined by which of
# {HighBlockable, LowBlockable} are set:
#   HighBlockable + LowBlockable -> Mid         (blockable in either stance)
#   HighBlockable only           -> High        (must stand-block; crouch ducks)
#   LowBlockable  only           -> Low         (must crouch-block)
#   neither                       -> Unblockable (no stance can block)
# `Unblockable_GIImmune` is NOT used to decide the class — its presence
# only adds the "no-GI / stagger-on-block" modifier on top.
ATTACK_FLAG_VERIFIED_BITS: dict[int, str] = {
    0x001: "HighBlockable",
    0x002: "LowBlockable",
    0x004: "BlockBypass_GuardBreak",
    0x008: "LowAttack",
    0x010: "MidAttack",
    0x040: "CrouchOnly",
    0x080: "HighAttack",
    0x100: "Special_FlagX100",
    0x200: "Unblockable_GIImmune",
}


def attack_flags_to_str(flags: int) -> str:
    """Render wU16AttackFlags showing only VERIFIED bit names; unverified
    bits are listed as 'bN' so the caller doesn't read invented semantics
    into them.
    """
    if flags == 0:
        return "0"
    parts: list[str] = []
    remaining = flags
    for bit, name in ATTACK_FLAG_VERIFIED_BITS.items():
        if remaining & bit:
            parts.append(name)
            remaining &= ~bit
    # Anything left over → render each set bit individually as 'bN'.
    for i in range(16):
        if remaining & (1 << i):
            parts.append(f"b{i}")
    return "|".join(parts)


@dataclass
class LuxBattleAttackCell:
    """One 0x70-byte attack cell from KHD Section A.

    Layout matches engine struct `LuxBattleAttackCell` (Ghidra).
    """
    raw: bytes = field(repr=False)
    offset_in_file: int = 0
    # Whole-cell fields
    u64SlotMask: int = 0          # +0x00 — per-attacker bit assignment (bits 31/55 = throw partition)
    wU16AttackFlags: int = 0      # +0x32 — high/low/mid/UB classification (see ATTACK_FLAG_BITS)
    wU16InputCond: int = 0        # +0x34 — input precondition mask
    wI16MasterWindowStart: int = 0  # +0x36 — active-frame start (60ths)
    wI16MasterWindowEnd: int = 0    # +0x38 — active-frame end (60ths)
    wI16BaseDamage: int = 0       # +0x3A — damage applied on hit (i16)
    wI16StunRecoil: int = 0       # +0x3C — block recoil
    wU16ExtraStateFlags: int = 0  # +0x3E — extra state bits (BA / soul-charge / etc.)
    wI16BlockstunFrames: int = 0  # +0x44 — frames of blockstun on block
    wI16HitstunStandingNormal: int = 0  # +0x46 — frames on standing hit
    wI16HitstunStandingAir: int = 0     # +0x48 — frames on airborne hit
    wI16HitstunCrouchNormal: int = 0    # +0x4C — frames on crouching hit
    wI16HitstunCrouchAir: int = 0       # +0x4E — frames on crouching+air hit
    wI16ReactionIdStanding: int = 0     # +0x50 — reaction id (-> chara+0x43DD8, 0x14 stride)
    wI16ReactionIdAir: int = 0          # +0x52 — reaction id for airborne defender
    wI16ThrowEscapeId: int = 0          # +0x54 — throw escape / counter-hit id
    wU16PassthroughTagA: int = 0        # +0x5A — usually 0xFFFD (default tag)
    wU16HitboxGroupBitfield: int = 0    # +0x5E — hitbox group bitmask (high byte = "type tag" 0/FF observed)
    wU16PassthroughTagC: int = 0        # +0x60
    cI8RangeStandMin: int = 0           # +0x62 — min range (standing)
    cI8RangeStandMax: int = 0           # +0x63 — max range (standing)
    cI8RangeCrouchMin: int = 0          # +0x64 — min range (crouching)
    cI8RangeCrouchMax: int = 0          # +0x65 — max range (crouching)
    nI16ReachExtraGate: int = 0         # +0x66 — extra-reach gate
    wU16RuntimePropagateField: int = 0  # +0x6A

    MAX_REASONABLE_MASTER_WINDOW_FRAME = 499

    @property
    def is_cleared_sentinel(self) -> bool:
        """True if the cell is a sentinel/cleared slot (HitboxGroup == 0xFFFF)."""
        return self.wU16HitboxGroupBitfield == 0xFFFF

    @property
    def has_valid_active_window(self) -> bool:
        """True when the master window bounds look like a usable attack window."""
        return (
            0 <= self.wI16MasterWindowStart <= self.MAX_REASONABLE_MASTER_WINDOW_FRAME
            and 0 <= self.wI16MasterWindowEnd <= self.MAX_REASONABLE_MASTER_WINDOW_FRAME
            and self.wI16MasterWindowEnd >= self.wI16MasterWindowStart
        )

    @property
    def cell_role(self) -> str:
        """High-level classification of what this cell is.

        - 'Sentinel' — cleared slot (wU16HitboxGroupBitfield == 0xFFFF).
        - 'Header'   — damage==0 AND wU16AttackFlags has incoherent bits set
                        (both HighAttack and Unblockable, or 6+ of the low
                        9 bits set). These are pre-/post-attack fallback
                        cells the engine uses as "no real hit this slot".
        - 'NonDamaging' — damage==0 with sensible flags; transitions,
                          stances, etc.
        - 'Attack'   — damage > 0; the normal case.
        """
        if self.is_cleared_sentinel:
            return "Sentinel"
        if self.wI16BaseDamage > 0:
            return "Attack"
        # Damage-zero cell. Distinguish "Header" (anomalous flags) from
        # "NonDamaging" (sensible flags, used for stance/transition).
        low9 = self.wU16AttackFlags & 0x1FF
        bit_count = bin(low9).count("1")
        if bit_count >= 6 or (low9 & 0x080 and low9 & 0x200):
            return "Header"
        return "NonDamaging"

    @property
    def attack_class(self) -> str:
        """Human-readable attack class: High/Mid/Low/Throw/Unblockable.

        Logic mirrors LuxMoveVM_EvaluateMoveTransition @ 0x14033E140:
        the block bits (HighBlockable=0x001, LowBlockable=0x002) gate the
        block path against standing/crouching defenders. Cells without
        either block bit can never enter the BLOCK reaction — they are
        genuinely UNBLOCKABLE regardless of whether the GI-immune flag
        (0x200) is set.

          HighBlockable + LowBlockable  -> Mid
          HighBlockable only            -> High
          LowBlockable  only            -> Low
          neither                       -> Unblockable
          throw partition (slotMask)    -> Throw

        Bit 0x200 (Unblockable_GIImmune) is NOT used here — it is only
        a "cannot be Guard Impacted" modifier. On its own it does not
        change the class; combined with block bits it makes the move a
        Break Attack (stagger-on-block) but the move is still blockable.
        See `is_break_attack` / `is_gi_immune` for those properties.
        """
        f = self.wU16AttackFlags
        if (self.u64SlotMask & 0x80000080000000) != 0:
            return "Throw"
        has_high_blk = (f & 0x001) != 0
        has_low_blk = (f & 0x002) != 0
        if has_high_blk and has_low_blk:
            return "Mid"
        if has_high_blk:
            return "High"
        if has_low_blk:
            return "Low"
        # No block bits set. If the cell carries no flags at all it's an
        # "Inactive" placeholder; otherwise it's a true Unblockable. The
        # GIImmune bit is informational only (most UBs set it but it isn't
        # what makes them unblockable).
        if f == 0:
            return "Inactive"
        return "Unblockable"

    @property
    def is_break_attack(self) -> bool:
        """True if this cell is a Break Attack (still blockable, but the
        defender's block reaction is the stagger variant — reaction 3
        with chara+0x88 = 1 — instead of normal block reaction 2).
        Block dispatch in LuxBattle_ResolveAttackVsHurtboxMask22 case 1
        keys on (AttackFlags & 0x200) AND the cell having block bits.
        """
        f = self.wU16AttackFlags
        if (f & 0x200) == 0:
            return False
        # Must be blockable in at least one stance to qualify as BA — a
        # cell with 0x200 set and NO block bits is just a regular UB.
        return (f & 0x003) != 0

    @property
    def is_gi_immune(self) -> bool:
        """True iff the cell has the Unblockable_GIImmune bit (0x200)
        set. The defender's GI roll in EvaluateMoveTransition is short-
        circuited to false when this is set, regardless of class.
        """
        return (self.wU16AttackFlags & 0x200) != 0

    @property
    def is_guard_bypass(self) -> bool:
        """True iff cell has BlockBypass_GuardBreak (0x004). When the
        defender is in the guard-broken substate this bit lets the move
        return result 7 (guard-crush counter-hit) instead of 0.
        """
        return (self.wU16AttackFlags & 0x004) != 0

    @property
    def move_type(self) -> str:
        """Strike / Grab / Super-or-Other — derived from u64SlotMask bits.
        Mirrors `chara+0x1354 dwMoveType` engine output.
        """
        return decode_move_type(self.u64SlotMask)

    @property
    def anim_kind(self) -> str:
        """High / Mid / Low / SpecialMid / SpecialLow / Grab_A..C — derived
        from u64SlotMask bit pattern. Mirrors `chara+0x1358 dwAnimKind`.
        """
        return decode_anim_kind(self.u64SlotMask)

    @property
    def active_frames(self) -> str:
        """Active-frame window as a 'start-end' string."""
        return f"{self.wI16MasterWindowStart}-{self.wI16MasterWindowEnd}"

    @property
    def active_frame_count(self) -> int:
        """Number of active frames (end - start + 1, clamped to >= 0)."""
        n = self.wI16MasterWindowEnd - self.wI16MasterWindowStart + 1
        return max(0, n)

    @staticmethod
    def _fmt_range_byte(b: int) -> str:
        """Render a range byte: -127 (sentinel) shown as '∞'."""
        if b == RANGE_SENTINEL_BYTE:
            return "inf"
        return str(b)

    @property
    def range_stand(self) -> str:
        """Standing-defender range as 'min..max' with sentinel handling."""
        return f"{self._fmt_range_byte(self.cI8RangeStandMin)}..{self._fmt_range_byte(self.cI8RangeStandMax)}"

    @property
    def range_crouch(self) -> str:
        return f"{self._fmt_range_byte(self.cI8RangeCrouchMin)}..{self._fmt_range_byte(self.cI8RangeCrouchMax)}"

    @property
    def passthrough_a_name(self) -> str:
        """Human label for wU16PassthroughTagA value."""
        return PASSTHROUGH_TAG_NAMES.get(
            self.wU16PassthroughTagA, f"0x{self.wU16PassthroughTagA:04X}"
        )

    # NOTE: wI16ReactionIdStanding / Air values index into the chara's
    # per-character reaction-property table at chara+0x43DD8 (0x14-byte
    # stride) — they're NOT EYarareReactionId values directly. Use
    # yarare_name() only on vmCtx+0x2B30 ActiveYarareId / similar runtime ids.

    @property
    def attack_flag_bits_decoded(self) -> str:
        return attack_flags_to_str(self.wU16AttackFlags)

    @property
    def on_block(self) -> int:
        """Plain on-block frame delta (defender disadvantage)."""
        return self.wI16BlockstunFrames

    @property
    def on_hit_standing(self) -> int:
        return self.wI16HitstunStandingNormal

    @property
    def summary(self) -> str:
        """One-line summary suitable for a list row."""
        if self.is_cleared_sentinel:
            return "<cleared sentinel>"
        return (
            f"{self.attack_class} {self.anim_kind} {self.move_type} "
            f"dmg={self.wI16BaseDamage} active={self.active_frames} "
            f"onBlock={self.on_block} onHit={self.on_hit_standing} "
            f"range={self.range_stand}"
        )


def parse_attack_cell(buf: bytes, off: int) -> LuxBattleAttackCell:
    """Parse one 0x70-byte LuxBattleAttackCell at `off` inside the .khd buffer."""
    raw = buf[off : off + 0x70]
    return LuxBattleAttackCell(
        raw=raw,
        offset_in_file=off,
        u64SlotMask=struct.unpack_from("<Q", raw, 0x00)[0],
        wU16AttackFlags=struct.unpack_from("<H", raw, 0x32)[0],
        wU16InputCond=struct.unpack_from("<H", raw, 0x34)[0],
        wI16MasterWindowStart=struct.unpack_from("<h", raw, 0x36)[0],
        wI16MasterWindowEnd=struct.unpack_from("<h", raw, 0x38)[0],
        wI16BaseDamage=struct.unpack_from("<h", raw, 0x3A)[0],
        wI16StunRecoil=struct.unpack_from("<h", raw, 0x3C)[0],
        wU16ExtraStateFlags=struct.unpack_from("<H", raw, 0x3E)[0],
        wI16BlockstunFrames=struct.unpack_from("<h", raw, 0x44)[0],
        wI16HitstunStandingNormal=struct.unpack_from("<h", raw, 0x46)[0],
        wI16HitstunStandingAir=struct.unpack_from("<h", raw, 0x48)[0],
        wI16HitstunCrouchNormal=struct.unpack_from("<h", raw, 0x4C)[0],
        wI16HitstunCrouchAir=struct.unpack_from("<h", raw, 0x4E)[0],
        wI16ReactionIdStanding=struct.unpack_from("<h", raw, 0x50)[0],
        wI16ReactionIdAir=struct.unpack_from("<h", raw, 0x52)[0],
        wI16ThrowEscapeId=struct.unpack_from("<h", raw, 0x54)[0],
        wU16PassthroughTagA=struct.unpack_from("<H", raw, 0x5A)[0],
        wU16HitboxGroupBitfield=struct.unpack_from("<H", raw, 0x5E)[0],
        wU16PassthroughTagC=struct.unpack_from("<H", raw, 0x60)[0],
        cI8RangeStandMin=struct.unpack_from("<b", raw, 0x62)[0],
        cI8RangeStandMax=struct.unpack_from("<b", raw, 0x63)[0],
        cI8RangeCrouchMin=struct.unpack_from("<b", raw, 0x64)[0],
        cI8RangeCrouchMax=struct.unpack_from("<b", raw, 0x65)[0],
        nI16ReachExtraGate=struct.unpack_from("<h", raw, 0x66)[0],
        wU16RuntimePropagateField=struct.unpack_from("<H", raw, 0x6A)[0],
    )


# Backwards-compat alias so legacy code that imported FLuxMoveDataEntry
# keeps working. New code should use LuxBattleAttackCell.
FLuxMoveDataEntry = LuxBattleAttackCell
parse_khd_entry = parse_attack_cell


# MOVE_TYPE_NAMES is preserved for the FLuxMoveDataEntry (in-memory)
# `+0x5F` typeTag dispatch that LuxMoveVM_UpdateMoveDataTable uses.
# (Distinct from the on-disk LuxBattleAttackCell's HitboxGroupBitfield;
# the engine remaps these.)
MOVE_TYPE_NAMES = {
    0xFF: "Sentinel_Cleared",
    0x00: "BootstrapMatch_0",
    0x01: "BootstrapMatch_1",
    0x02: "BootstrapMatch_2",
    0x03: "BootstrapMatch_3",
    0x04: "Move_Generic_Raw",
    0x05: "Move_Raw_05",
    0x06: "Move_Remap_38",
    0x07: "Move_Remap_3E_07",
    0x08: "Move_NewValue_3C_08",
    0x09: "Move_NewValue_3C_09",
    0x0A: "Move_Raw_0A",
    0x0B: "SpecialMove_MultiRef_0B",
    0x0C: "Move_Raw_0C",
    0x0D: "Move_Raw_0D",
    0x0E: "Move_RemapWriter",
    0x0F: "Move_Raw_0F",
    0x10: "Move_OptSkip_3C",
    0x11: "Move_Raw_11",
    0x12: "Move_Raw_12",
    0x13: "Move_Raw_13",
    0x14: "Move_Raw_14",
    0x15: "Move_Raw_15",
    0x16: "Move_Raw_16",
    0x17: "Move_Raw_17",
    0x18: "Move_Raw_18",
    0x19: "Move_Raw_19",
    0x1A: "Move_Raw_1A",
    0x1B: "Move_Remap_3E_1B",
    0x1C: "Move_NewValue_3C_1C",
    0x1D: "Move_NewValue_3C_1D",
    0x1E: "SpecialMove_MultiRef_1E",
}


@dataclass
class LuxBattleNonAttackMoveDescr:
    """One 6-byte non-attack-move descriptor from KHD Section B.

    Engine type: `LuxBattleNonAttackMoveDescr` (Ghidra).
    """
    nSDamageMultiplier: int   # +0x00 — i16 damage multiplier (or scaled)
    nSPassthroughTag: int     # +0x02 — i16, usually 0xFFFD (default-reaction marker)
    nSDuration60ths: int      # +0x04 — i16 duration in 60ths of a second


def parse_non_attack_descriptor(buf: bytes, off: int) -> LuxBattleNonAttackMoveDescr:
    a, b, c = struct.unpack_from("<3h", buf, off)
    return LuxBattleNonAttackMoveDescr(a, b, c)


@dataclass
class KhdSection:
    section_index: int
    offset: int
    size: int
    entry_count: int
    # Section 0 ("A"): LuxBattleAttackCell array (alias: FLuxMoveDataEntry).
    entries: list[LuxBattleAttackCell]
    raw: bytes = field(repr=False)
    detected_stride: Optional[int] = None
    detected_count: int = 0
    # Section 1 ("B"): LuxBattleNonAttackMoveDescr array (6-byte stride).
    non_attack_descriptors: list[LuxBattleNonAttackMoveDescr] = field(default_factory=list)
    # Section 2 ("C") only: typed header-record prefix (see parse_khd_section_c_prefix).
    c_prefix_records: list["KhdCRecord"] = field(default_factory=list)
    # Section 2 ("C") only: byte offset (within section) where the typed prefix ends.
    # Bytes after this are the opaque payload (not yet decoded).
    c_prefix_end: int = 0


# Section C header-record tag values that ARE in the FLuxMoveDataEntry enum
# range. The 0xD6 marker appears at the very start of the prefix as a count
# header; 0xFF is the cleared-slot sentinel (same as in Section A).
KHD_SECTION_C_VALID_TAGS = frozenset(list(range(0, 0x1F)) + [0xD6, 0xFF])


@dataclass
class KhdCRecord:
    """One 0x30-byte typed record from Section C's header-record prefix.

    Layout (inferred from byte patterns, validated across all 24 shipped charas):
        +0x00  u8   type_tag       FLuxMoveDataEntry typeTag enum (0..0x1E)
                                   OR 0xD6 (count marker, only at start)
                                   OR 0xFF (cleared / sentinel)
        +0x01  u8   subtype        0 for D6 markers, 1 for content records
        +0x02..0x03                always 0x0000 (validates the layout)
        +0x04  u32  index          1-based sequential within type, except for
                                   bitmask types where this is `1 << (n-1)` or
                                   a power-of-2 slot indicator
        +0x08..0x0F                mixed; +0x0F seen as a bitmask byte
        +0x10..0x2F                type-specific payload (mostly floats; for
                                   hit-cell records this carries position +
                                   scale + offset vectors)

    The full field map per type tag isn't yet decoded — see Ghidra's
    LuxMoveVM_UpdateMoveDataTable @ 0x14038F7D0 for the type-tag dispatch
    semantics.
    """
    record_index: int    # ordinal within the prefix
    byte_offset: int     # file-relative offset
    type_tag: int        # +0x00
    subtype: int         # +0x01
    index: int           # +0x04 (u32)
    raw: bytes = field(repr=False)

    @property
    def type_name(self) -> str:
        if self.type_tag == 0xD6:
            return "Header_CountMarker"
        return MOVE_TYPE_NAMES.get(self.type_tag, f"Unknown_0x{self.type_tag:02X}")


def parse_khd_section_c_prefix(buf: bytes, sec_off: int, sec_end: int) -> tuple[list[KhdCRecord], int]:
    """Walk the typed header-record prefix of Section C.

    Section C of a .khd file is a mixed-format block: it starts with a
    sequence of 0x30-byte typed records (variable count per chara, 3..597
    in shipped data), then transitions into opaque payload data the engine
    accesses via in-memory pointer fixups.

    This walker stops at the first record whose shape doesn't match the
    typed-record signature:

        byte[+0x00] in {0..0x1E, 0xD6, 0xFF}      (FLuxMoveDataEntry tags)
        byte[+0x01] in {0, 1}                      (subtype: 0 for D6, 1 else)
        byte[+0x02..+0x03] == 0x0000               (separator)

    Returns (records, end_byte_offset_in_buf).
    """
    records: list[KhdCRecord] = []
    o = sec_off
    while o + 0x30 <= sec_end:
        t = buf[o]
        if t not in KHD_SECTION_C_VALID_TAGS:
            break
        st = buf[o + 1]
        if st > 1:
            break
        if buf[o + 2] != 0 or buf[o + 3] != 0:
            break
        records.append(KhdCRecord(
            record_index=len(records),
            byte_offset=o,
            type_tag=t,
            subtype=st,
            index=struct.unpack_from("<I", buf, o + 4)[0],
            raw=buf[o : o + 0x30],
        ))
        o += 0x30
    return records, o


@dataclass
class FLuxMoveBankSlotView:
    """One 0x48-byte slot record from the bank's slot table at bank+0x30.

    Each slot describes one "move state" (idle, attack, transition, etc.).
    The slot table is contiguous; total count = sum of bucket counts at
    bank+0x1C..+0x2B.
    """
    slot_index: int                     # 0-based linear index in the slot table
    bank_offset: int                    # byte offset of slot record within the bank
    wAnimationIndex_00: int = 0         # +0x00 motion-id used by HgMotion
    wMotionPlaybackParam_02: int = 0    # +0x02
    nField_04: int = 0                  # +0x04
    wMotionFlags_06: int = 0            # +0x06
    dwSubTableOffset_10: int = 0        # +0x10
    dwSubTableOffset_14: int = 0        # +0x14
    dwAltBytecodeOffset_1C: int = 0     # +0x1C alt bytecode (typically unused)
    qwInputMask_20: int = 0             # +0x20 input mask (copied to lane+0x448)
    qwInputMask_28: int = 0             # +0x28
    flAnimLength_30: float = 0.0        # +0x30
    nAnimLengthFlag_34: int = 0         # +0x34
    nHitWindowStart_36: int = 0         # +0x36
    dwBytecodeOffset_38: int = 0        # +0x38 STACK VM bytecode (BYTE offset, NOT u32-aligned)
    nCellBoneIndexPerVariant: list[int] = field(default_factory=list)  # +0x3C..+0x46 i16[6]
    raw: bytes = field(repr=False, default=b"")
    # Decoded bytecode (filled by parse_khd when bytecode_off is in-range).
    bytecode: Optional["StackVMScript"] = None  # type: ignore


@dataclass
class KhdFile:
    magic: bytes
    field_0c: int
    section_offsets: list[int]
    trailer_data: bytes
    sections: list[KhdSection]
    raw: bytes = field(repr=False)
    # Slot table: every FLuxMoveBankSlotView, indexed from 0. Walked
    # automatically by parse_khd.
    slots: list[FLuxMoveBankSlotView] = field(default_factory=list)
    # Reverse index: cell_idx -> list of (slot_idx, variant_index).
    # Built by parse_khd from nCellBoneIndexPerVariant scans.
    cell_to_slots: dict[int, list] = field(default_factory=dict)


def _detect_record_stride(section: bytes) -> tuple[Optional[int], int]:
    n = len(section)
    if n < 12:
        return (None, 0)
    candidates = [6, 8, 0xC, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40, 0x70, 0x84]
    best: tuple[Optional[int], int, float] = (None, 0, 0.0)
    for stride in candidates:
        count = n // stride
        if count < 4:
            continue
        leftover = n - count * stride
        if leftover > stride // 2:
            continue
        first = section[:4]
        same_head = sum(
            1 for i in range(count)
            if section[i * stride : i * stride + 4] == first
        )
        head_frac = same_head / count
        marker = section[2:4]
        same_marker = sum(
            1 for i in range(count)
            if section[i * stride + 2 : i * stride + 4] == marker
        )
        marker_frac = same_marker / count
        score = max(head_frac, marker_frac * 0.95)
        if score >= 0.7 and score > best[2]:
            best = (stride, count, score)
    return (best[0], best[1])


def _scan_for_entry_array(buf: bytes, sec_off: int, sec_end: int) -> tuple[int, int]:
    """Scan a .khd section for a plausible 0x70-stride entry array."""
    section_len = sec_end - sec_off
    if section_len < 0x70 * 4:
        return (0, 0)
    # Fast path: section size is multiple of 0x70 and most tags look ok.
    if section_len % 0x70 == 0:
        n_candidates = section_len // 0x70
        sample = min(n_candidates, 64)
        tags = [buf[sec_off + i * 0x70 + 0x5F] for i in range(sample)]
        known = sum(1 for t in tags if t in MOVE_TYPE_NAMES or t == 0xFF)
        if known / sample >= 0.90:
            return (sec_off, n_candidates)
    best = (None, 0)
    for start in range(0, min(section_len, 0x200), 4):
        if (section_len - start) % 0x70 != 0:
            continue
        n_candidates = (section_len - start) // 0x70
        if n_candidates < 4:
            continue
        sample = min(n_candidates, 16)
        tags = [buf[sec_off + start + i * 0x70 + 0x5F] for i in range(sample)]
        if any(t not in MOVE_TYPE_NAMES for t in tags):
            continue
        if n_candidates > best[1]:
            best = (start, n_candidates)
    if best[0] is None:
        return (0, 0)
    return (sec_off + best[0], best[1])


def parse_khd(data: bytes) -> KhdFile:
    """Parse a .khd file."""
    if data[:4] != b"KH11":
        raise ValueError(f"bad KH11 magic: {data[:4]!r}")
    field_0c = struct.unpack_from("<I", data, 0x0C)[0]
    s_offs = list(struct.unpack_from("<3I", data, 0x10))
    for o in s_offs:
        if o >= len(data):
            raise ValueError(f"KH11 section offset 0x{o:X} >= file size 0x{len(data):X}")
    first_off = min(s_offs)
    trailer = data[0x1C:first_off]
    sorted_offs = sorted(s_offs + [len(data)])
    section_size_by_offset: dict[int, int] = {}
    for o, nxt in zip(sorted_offs, sorted_offs[1:]):
        section_size_by_offset[o] = nxt - o

    sections: list[KhdSection] = []
    for i, off in enumerate(s_offs):
        size = section_size_by_offset[off]
        sec_end = off + size
        sec_bytes = data[off:sec_end]
        array_off, n = _scan_for_entry_array(data, off, sec_end)
        entries: list[LuxBattleAttackCell] = []
        if n:
            for j in range(n):
                entries.append(parse_attack_cell(data, array_off + j * 0x70))
        stride, scount = _detect_record_stride(sec_bytes) if n == 0 else (0x70, n)

        # Section index 1 ("Section B") is LuxBattleNonAttackMoveDescr[].
        non_attack_descs: list[LuxBattleNonAttackMoveDescr] = []
        if i == 1 and size % 6 == 0:
            for j in range(size // 6):
                non_attack_descs.append(parse_non_attack_descriptor(data, off + j * 6))

        # Section index 2 ("Section C") gets the tagged-stream walker.
        c_records: list[KhdCRecord] = []
        c_end_abs = 0
        if i == 2:
            c_records, c_end_abs = parse_khd_section_c_prefix(data, off, sec_end)
        sections.append(KhdSection(
            section_index=i,
            offset=off,
            size=size,
            entry_count=n,
            entries=entries,
            raw=sec_bytes,
            detected_stride=stride,
            detected_count=scount,
            non_attack_descriptors=non_attack_descs,
            c_prefix_records=c_records,
            c_prefix_end=(c_end_abs - off) if c_records else 0,
        ))
    # Walk the slot table: bank+0x30, stride 0x48. Total count = sum of
    # bucket counts. Verify the buckets are contiguous (engine relies on
    # this — `slot_table_off + (StartIdx+slotInBucket)*0x48` only works
    # if buckets meet end-to-end).
    slot_table_off = 0x30
    buckets = []
    for i in range(4):
        start, count = struct.unpack_from("<HH", data, 0x1C + i * 4)
        buckets.append((start, count))
    expected = 0
    for i, (start, count) in enumerate(buckets):
        if count > 0 and start != expected:
            # Non-contiguous bucket layout. Engine works (it indexes by
            # bucket) but our linear walker would emit phantom slots in
            # the gap. Skip gaps gracefully.
            pass
        expected = max(expected, start + count)
    total_slots = expected
    slot_records: list[FLuxMoveBankSlotView] = []
    try:
        from stackvm import walk_stackvm
    except ImportError:
        walk_stackvm = None  # type: ignore
    for slot_idx in range(total_slots):
        slot_off = slot_table_off + slot_idx * 0x48
        if slot_off + 0x48 > len(data):
            break
        raw_slot = data[slot_off : slot_off + 0x48]
        anim_idx, motion_param, field_04, motion_flags = struct.unpack_from(
            "<HHhH", raw_slot, 0x00
        )
        sub_a, sub_b = struct.unpack_from("<II", raw_slot, 0x10)
        alt_bc = struct.unpack_from("<I", raw_slot, 0x1C)[0]
        mask_a, mask_b = struct.unpack_from("<QQ", raw_slot, 0x20)
        anim_len = struct.unpack_from("<f", raw_slot, 0x30)[0]
        len_flag, hit_win = struct.unpack_from("<hh", raw_slot, 0x34)
        bc_off = struct.unpack_from("<I", raw_slot, 0x38)[0]
        cells = list(struct.unpack_from("<6h", raw_slot, 0x3C))
        sv = FLuxMoveBankSlotView(
            slot_index=slot_idx,
            bank_offset=slot_off,
            wAnimationIndex_00=anim_idx,
            wMotionPlaybackParam_02=motion_param,
            nField_04=field_04,
            wMotionFlags_06=motion_flags,
            dwSubTableOffset_10=sub_a,
            dwSubTableOffset_14=sub_b,
            dwAltBytecodeOffset_1C=alt_bc,
            qwInputMask_20=mask_a,
            qwInputMask_28=mask_b,
            flAnimLength_30=anim_len,
            nAnimLengthFlag_34=len_flag,
            nHitWindowStart_36=hit_win,
            dwBytecodeOffset_38=bc_off,
            nCellBoneIndexPerVariant=cells,
            raw=raw_slot,
        )
        # Walk the slot's stack-VM bytecode if it points into the file.
        # max_bytes cap = full Section C — we won't overshoot in practice
        # because the walker stops at first terminator.
        if walk_stackvm is not None and 0 < bc_off < len(data):
            try:
                sv.bytecode = walk_stackvm(data, bc_off, max_bytes=len(data) - bc_off)
            except Exception:
                sv.bytecode = None
        slot_records.append(sv)

    khd = KhdFile(
        magic=data[:4],
        field_0c=field_0c,
        section_offsets=s_offs,
        trailer_data=trailer,
        sections=sections,
        raw=data,
        slots=slot_records,
    )
    # Build a reverse index: cell_idx -> list of (slot_idx, variant)
    # that reference that cell via nCellBoneIndexPerVariant.
    cell_to_slots: dict[int, list[tuple[int, int]]] = {}
    for sv in slot_records:
        for variant, cell_idx in enumerate(sv.nCellBoneIndexPerVariant):
            if cell_idx >= 0:
                cell_to_slots.setdefault(cell_idx, []).append((sv.slot_index, variant))
    khd.cell_to_slots = cell_to_slots  # type: ignore[attr-defined]
    return khd


# ----------------------------------------------------------------------
# Hit data (.dat) — i16-tagged stream of KHit nodes
# ----------------------------------------------------------------------
# Walks the same format Lux_KHitChk_DeserializeLinkedList @ 0x14030C940
# consumes at runtime:
#
#   tag <  0   end-of-stream
#   tag == 0   KHitSphere    stride 0x20
#   tag == 1   KHitArea      stride 0x28
#   tag == 2   KHitFixArea   stride 0x30
#
# Common 8-byte prefix:
#   +0x00  i16  tag
#   +0x02  u16  slot     defender bone slot (0..63)
#   +0x04  u32  flags    authored, copied to KHit+0x10
#
# Sphere tail:
#   +0x08  float[4]  (x, y, z, radius)
#   +0x18  u32 id, u32 reserved

@dataclass
class HitRecord:
    """One KHit stream record (variable stride)."""
    offset_in_stream: int
    tag: int                # 0=Sphere, 1=Area, 2=FixArea
    stride: int             # 0x20 / 0x28 / 0x30
    slot: int
    flags: int
    pos_x: float = 0.0
    pos_y: float = 0.0
    pos_z: float = 0.0
    radius: float = 0.0
    id_link: int = 0
    raw: bytes = field(default=b"", repr=False)


@dataclass
class HitFile:
    records: list[HitRecord]
    stream_end: int
    trailer: bytes = field(repr=False)


def parse_hit_dat(data: bytes) -> HitFile:
    """Parse an atkhit / bodyhit / yararehit .dat file as an i16-tagged stream."""
    records: list[HitRecord] = []
    o = 0
    while o + 2 <= len(data):
        tag = struct.unpack_from("<h", data, o)[0]
        if tag < 0:
            break
        if tag == 0:
            stride = 0x20
        elif tag == 1:
            stride = 0x28
        elif tag == 2:
            stride = 0x30
        else:
            break
        if o + stride > len(data):
            break
        slot = struct.unpack_from("<H", data, o + 2)[0]
        flags = struct.unpack_from("<I", data, o + 4)[0]
        rec = HitRecord(
            offset_in_stream=o,
            tag=tag,
            stride=stride,
            slot=slot,
            flags=flags,
            raw=data[o : o + stride],
        )
        if tag == 0:
            x, y, z, r, idlink, _ = struct.unpack_from("<4f2I", data, o + 0x08)
            rec.pos_x, rec.pos_y, rec.pos_z, rec.radius = x, y, z, r
            rec.id_link = idlink
        records.append(rec)
        o += stride
    return HitFile(records=records, stream_end=o, trailer=data[o:])


# ----------------------------------------------------------------------
# Convenience dispatcher
# ----------------------------------------------------------------------

def parse_auto(path: str):
    import os
    ext = os.path.splitext(path)[1].lower()
    with open(path, "rb") as f:
        data = f.read()
    if ext == ".khd":
        return parse_khd(data)
    if ext == ".mot":
        return parse_mot(data)
    if ext == ".dtp":
        return parse_dtp(data)
    if ext == ".vtb":
        return parse_vtb(data)
    if ext == ".lpd":
        return parse_lpd(data)
    if ext == ".lpb":
        return parse_lpb(data)
    if ext == ".dat":
        return parse_hit_dat(data)
    raise ValueError(f"unknown extension: {ext}")
