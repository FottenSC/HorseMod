"""Reusable static attacker-recovery and defender-stun analysis.

The analyzer intentionally accepts a *proven* KHD slot/cell pair; it does not
solve official-row routing.  This separation lets every confirmed route use
the same arithmetic while ambiguous routes remain null.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib

from native_reaction_table import LuxHitReactionMoveIdTable
from stackvm_emulate import emulate


ATTACK_SETUP_HELPER = 0x3020
REACTION_TICK_HELPER = 0x305E
REACTION_STATE_HELPER = 0x3115

# The common v2.31 attack-setup helper has character-relative packed ids, so
# its raw bytes differ per bank.  Its opcode/push-flag shape is identical for
# the 25 normal profiles using the audited 447-instruction closure.
COMMON_ATTACK_SETUP_OPCODE_SHA256 = (
    "8B6682FFD423EC0C50373B5F7CB4C9320036F6BB6D83007188401FD846889EC4"
)

# The matching content dump contains three statically audited revisions of
# packed helper 0x3020.  Their surrounding feature branches differ, but all
# three retain the recovery contract used here: LOCAL1 is published to
# GLOBAL0 before the common lane-end transition path.  Do not accept a new
# shape until that dataflow has been audited as well.
AUDITED_ATTACK_SETUP_OPCODE_SHA256 = frozenset({
    COMMON_ATTACK_SETUP_OPCODE_SHA256,
    "542201C0FAF168F5EBE1C2F802757455315F62D82E58BFDDDA9F97059F0B5B96",
    "1161A41148D748772864C548B89E386AE5B36D65ACF6DE01878B8CBDA49F0E56",
})

# Export visits many official rows that share the same native reaction row.
# Cache immutable proof results per parsed KHD object and common-table digest;
# this avoids repeatedly emulating the same eight wrappers.
_KHD_CACHE_IDENTITIES: dict[int, tuple[object, str]] = {}
_REACTION_ROOT_CACHE: dict[tuple[str, int], tuple[int, int] | None] = {}
_REACTION_ROW_CACHE: dict[
    tuple[str, str, int], NativeReactionRouteEvidence | None
] = {}


def _khd_cache_identity(khd: object) -> str:
    object_id = id(khd)
    cached = _KHD_CACHE_IDENTITIES.get(object_id)
    if cached is not None and cached[0] is khd:
        return cached[1]
    identity = hashlib.sha256(khd.raw).hexdigest().upper()
    # Retain the object alongside its digest so CPython cannot recycle the id
    # for a later character bank while cached proof entries still exist.
    _KHD_CACHE_IDENTITIES[object_id] = (khd, identity)
    return identity


@dataclass(frozen=True)
class NativeReactionRouteEvidence:
    reaction_row_id: int
    raw_move_ids: tuple[int, ...]
    packed_move_ids: tuple[int, ...]
    resolved_slots: tuple[int, ...]
    driver_move_ids: tuple[int, ...]


@dataclass(frozen=True)
class NativeFrameAdvantageEvidence:
    attack_slot: int
    attack_cell: int
    total_frames: int
    cell_window_start_coordinate: int
    cell_window_end_coordinate: int
    recovery_lead: int
    recovery_open_coordinate: int
    inclusive_recovery_frames: int
    block_stun_frames: int
    hit_stun_frames: int
    counter_hit_stun_frames: int | None
    block_advantage: int
    hit_advantage: int
    counter_hit_advantage: int | None
    hit_reaction: NativeReactionRouteEvidence
    counter_hit_reaction: NativeReactionRouteEvidence | None
    resolutions: tuple[str, ...]


@dataclass(frozen=True)
class NativeThrowBreakEvidence:
    attack_slot: int
    attack_cell: int
    total_frames: int
    cell_window_start_coordinate: int
    cell_window_end_coordinate: int
    recovery_lead: int
    recovery_open_coordinate: int
    inclusive_recovery_frames: int
    break_stun_frames: int
    break_advantage: int
    resolutions: tuple[str, ...]


def _opcode_shape_sha256(script: object) -> str:
    signature = bytes(
        instruction.opcode | (0x80 if instruction.push_flag else 0)
        for instruction in script.instructions
    )
    return hashlib.sha256(signature).hexdigest().upper()


def _common_recovery_timeline(
    khd: object,
    attack_slot: int,
    attack_cell: int,
    *,
    allowed_cell_roles: frozenset[str],
) -> tuple[object, object, object, str, int, int, int, int] | None:
    """Resolve the shared 0x3020 attacker recovery convention."""

    if not (0 <= attack_slot < len(khd.slots)) or not khd.sections:
        return None
    cells = khd.sections[0].entries
    if not 0 <= attack_cell < len(cells):
        return None
    slot = khd.slots[attack_slot]
    cell = cells[attack_cell]
    if cell.cell_role not in allowed_cell_roles or slot.bytecode is None:
        return None

    helper_slot = khd.resolve_packed_slot(ATTACK_SETUP_HELPER)
    if helper_slot is None or khd.slots[helper_slot].bytecode is None:
        return None
    helper_script = khd.slots[helper_slot].bytecode
    helper_shape = _opcode_shape_sha256(helper_script)
    if helper_shape not in AUDITED_ATTACK_SETUP_OPCODE_SHA256:
        return None

    calls = [
        event
        for event in emulate(slot.bytecode, attack_slot).bank_scripts
        if event.packed_move_id == ATTACK_SETUP_HELPER
    ]
    if len(calls) != 1:
        return None
    args = calls[0].concrete_args
    if len(args) < 3 or args[0] != ATTACK_SETUP_HELPER or args[2] is None:
        return None
    recovery_lead = int(args[2])
    total_frames = int(slot.wTotalFrames)
    cell_window_start = int(cell.wI16MasterWindowStart)
    cell_window_end = int(cell.wI16MasterWindowEnd)
    if (
        total_frames in (0xFFFE, 0xFFFF)
        or recovery_lead < 0
        or recovery_lead >= total_frames
        or cell_window_start < 0
        or cell_window_end < cell_window_start
    ):
        return None

    recovery_open = total_frames - recovery_lead - 1
    inclusive_recovery = recovery_open - cell_window_start + 1
    if inclusive_recovery <= 0:
        return None
    return (
        slot,
        cell,
        calls[0],
        helper_shape,
        recovery_lead,
        total_frames,
        recovery_open,
        inclusive_recovery,
    )


def _concrete_predicate_args(event: object) -> tuple[int | None, ...]:
    return tuple(getattr(arg, "value", None) for arg in event.args)


def _has_predicate(script_result: object, expected: tuple[int, ...]) -> bool:
    return any(
        _concrete_predicate_args(event) == expected
        for event in script_result.predicates
    )


def _confirm_standard_reaction_root(khd: object, packed_move_id: int) -> tuple[int, int] | None:
    """Confirm a shipped ordinary hit-reaction wrapper and counter clock.

    This is deliberately narrow.  It accepts only roots that make one direct
    call to a driver with the audited entry/end/state-helper contract. Special
    launch, wall, ground, and bespoke character routes remain unknown.
    """

    cache_key = (_khd_cache_identity(khd), packed_move_id)
    if cache_key in _REACTION_ROOT_CACHE:
        return _REACTION_ROOT_CACHE[cache_key]

    def finish(value: tuple[int, int] | None) -> tuple[int, int] | None:
        _REACTION_ROOT_CACHE[cache_key] = value
        return value

    resolved_slot = khd.resolve_packed_slot(packed_move_id)
    if resolved_slot is None or not 0 <= resolved_slot < len(khd.slots):
        return finish(None)
    root_script = khd.slots[resolved_slot].bytecode
    if root_script is None:
        return finish(None)
    root_calls = [
        event.packed_move_id
        for event in emulate(root_script, resolved_slot).bank_scripts
        if event.packed_move_id is not None
    ]
    if len(root_calls) != 1:
        return finish(None)
    driver_move_id = int(root_calls[0])

    driver_slot = khd.resolve_packed_slot(driver_move_id)
    state_slot = khd.resolve_packed_slot(REACTION_STATE_HELPER)
    tick_slot = khd.resolve_packed_slot(REACTION_TICK_HELPER)
    if None in (driver_slot, state_slot, tick_slot):
        return finish(None)
    driver_script = khd.slots[driver_slot].bytecode
    state_script = khd.slots[state_slot].bytecode
    tick_script = khd.slots[tick_slot].bytecode
    if None in (driver_script, state_script, tick_script):
        return finish(None)

    driver_result = emulate(driver_script, driver_slot)
    if not _has_predicate(driver_result, (0x10,)):
        return finish(None)
    if not _has_predicate(driver_result, (0x09,)):
        return finish(None)
    if REACTION_STATE_HELPER not in {
        event.packed_move_id for event in driver_result.bank_scripts
    }:
        return finish(None)

    state_result = emulate(state_script, state_slot)
    if REACTION_TICK_HELPER not in {
        event.packed_move_id for event in state_result.bank_scripts
    }:
        return finish(None)

    tick_result = emulate(tick_script, tick_slot)
    if not _has_predicate(tick_result, (0x0B, 0x60)):
        return finish(None)
    if not _has_predicate(tick_result, (0x1A, 0x7FFF, 1)):
        return finish(None)
    if not any(event.concrete_args == [0x12] for event in tick_result.effects):
        return finish(None)
    return finish((resolved_slot, driver_move_id))


def _confirm_reaction_row(
    khd: object,
    reaction_table: LuxHitReactionMoveIdTable,
    reaction_row_id: int,
) -> NativeReactionRouteEvidence | None:
    cache_key = (_khd_cache_identity(khd), reaction_table.sha256, reaction_row_id)
    if cache_key in _REACTION_ROW_CACHE:
        return _REACTION_ROW_CACHE[cache_key]
    row = reaction_table.row(reaction_row_id)
    if row is None:
        _REACTION_ROW_CACHE[cache_key] = None
        return None
    raw_move_ids = row.all_move_ids
    # ApplyHitReactionMove clears this flag before transition.  Preserve the
    # raw value as evidence but resolve the same low-15-bit id the game uses.
    packed_move_ids = tuple(raw & 0x7FFF for raw in raw_move_ids)
    confirmed = [
        _confirm_standard_reaction_root(khd, packed)
        for packed in packed_move_ids
    ]
    if any(item is None for item in confirmed):
        _REACTION_ROW_CACHE[cache_key] = None
        return None
    evidence = NativeReactionRouteEvidence(
        reaction_row_id=reaction_row_id,
        raw_move_ids=raw_move_ids,
        packed_move_ids=packed_move_ids,
        resolved_slots=tuple(item[0] for item in confirmed if item is not None),
        driver_move_ids=tuple(item[1] for item in confirmed if item is not None),
    )
    _REACTION_ROW_CACHE[cache_key] = evidence
    return evidence


def analyze_confirmed_slot_frames(
    khd: object,
    attack_slot: int,
    attack_cell: int,
    reaction_table: LuxHitReactionMoveIdTable | None = None,
) -> NativeFrameAdvantageEvidence | None:
    """Derive frame advantage when the complete common recovery profile fits.

    Returning ``None`` is the normal result for unaudited helper profiles,
    non-attack cells, invalid cells, multiple setup calls, or non-concrete
    timing args. Throw-break recovery uses the narrower dedicated analyzer.
    """

    timeline = _common_recovery_timeline(
        khd,
        attack_slot,
        attack_cell,
        allowed_cell_roles=frozenset({"Attack"}),
    )
    if timeline is None:
        return None
    slot, cell, call, helper_shape, recovery_lead, total_frames, recovery_open, inclusive_recovery = timeline
    cell_window_start = int(cell.wI16MasterWindowStart)
    cell_window_end = int(cell.wI16MasterWindowEnd)
    block_stun = int(cell.wI16BlockstunFrames)
    hit_stun = int(cell.wI16HitstunBaseContact)
    # LuxBattleChara_EvaluateHitContactMode returns native mode 11 for
    # Counter Hit. ProcessHit snapshots that mode at accepted contact, and
    # ComputeHitReactionParams selects the shared +0x48 special-contact stun
    # whenever the snapshot is >= 2.  The column is not CH-exclusive (LH,
    # Punish Hit, rise/GI contexts also use it), but it is the exact native
    # defender-stun endpoint selected for an ordinary mode-11 Counter Hit.
    special_contact_stun = int(cell.wI16HitstunSpecialContact)
    if block_stun < 0 or hit_stun < 0:
        return None
    if reaction_table is None:
        return None
    hit_reaction = _confirm_reaction_row(
        khd, reaction_table, int(cell.wI16ReactionIdBaseContact)
    )
    if hit_reaction is None:
        return None
    # Negative signed values are native sentinels, not frame counts.  Keep
    # ordinary Block/Hit evidence when only the CH-selected column is unset.
    counter_hit_stun = special_contact_stun if special_contact_stun >= 0 else None
    counter_hit_reaction = (
        _confirm_reaction_row(
            khd, reaction_table, int(cell.wI16ReactionIdSpecialContact)
        )
        if counter_hit_stun is not None
        else None
    )
    if counter_hit_stun is not None and counter_hit_reaction is None:
        counter_hit_stun = None
    counter_hit_advantage = (
        counter_hit_stun - inclusive_recovery
        if counter_hit_stun is not None
        else None
    )
    return NativeFrameAdvantageEvidence(
        attack_slot=attack_slot,
        attack_cell=attack_cell,
        total_frames=total_frames,
        cell_window_start_coordinate=cell_window_start,
        cell_window_end_coordinate=cell_window_end,
        recovery_lead=recovery_lead,
        recovery_open_coordinate=recovery_open,
        inclusive_recovery_frames=inclusive_recovery,
        block_stun_frames=block_stun,
        hit_stun_frames=hit_stun,
        counter_hit_stun_frames=counter_hit_stun,
        block_advantage=block_stun - inclusive_recovery,
        hit_advantage=hit_stun - inclusive_recovery,
        counter_hit_advantage=counter_hit_advantage,
        hit_reaction=hit_reaction,
        counter_hit_reaction=counter_hit_reaction,
        resolutions=(
            f"khd-attacker-recovery:slot{attack_slot}"
            f"@0x{call.source_pc:X}->packed0x3020;LOCAL1={recovery_lead};"
            f"profile={helper_shape}",
            f"khd-attacker-actionable:slot{attack_slot};"
            f"0x7600(end{total_frames})-GLOBAL0({recovery_lead})-1="
            f"coordinate{recovery_open};inclusive{cell_window_start}..{recovery_open}="
            f"{inclusive_recovery}",
            f"khd-defender-stun:cell{attack_cell}[block+0x44={block_stun};"
            f"base-hit+0x46={hit_stun};"
            f"special-hit+0x48={special_contact_stun}(mode11-counter-hit);"
            "ApplyHitReactionMove->counter+0x1364;"
            "EffectOp0x12->delta+0x1370;TickMain->clamped-subtract]",
            f"native-hit-reaction:yarare-row{hit_reaction.reaction_row_id};"
            f"raw={','.join(f'0x{x:04X}' for x in hit_reaction.raw_move_ids)};"
            f"slots={','.join(str(x) for x in hit_reaction.resolved_slots)};"
            "standard-driver->0x3115->0x305E;IF0x0B[0x60];Effect0x12;"
            "IF0x1A[0x7FFF,1]",
            f"native-advantage:block={block_stun}-{inclusive_recovery}="
            f"{block_stun - inclusive_recovery};hit={hit_stun}-{inclusive_recovery}="
            f"{hit_stun - inclusive_recovery}",
            (
                f"native-counter-advantage:{counter_hit_stun}-{inclusive_recovery}="
                f"{counter_hit_advantage};"
                "contact-mode11-selects-shared-special-column+0x48;"
                f"yarare-row{counter_hit_reaction.reaction_row_id}"
                if counter_hit_stun is not None and counter_hit_reaction is not None
                else "native-counter-advantage:unknown;"
                "special-column+0x48-is-negative-sentinel"
            ),
        ),
    )


def analyze_throw_break_frames(
    khd: object,
    attack_slot: int,
    attack_cell: int,
) -> NativeThrowBreakEvidence | None:
    """Derive recovery after a block/break of an authored grab attempt.

    The caller must separately prove that the official row is authored with
    the game's TH effect tag.  This function deliberately accepts only a
    non-damaging native cell, preventing strikes that merely lead into a throw
    from being re-labelled as throws.
    """

    timeline = _common_recovery_timeline(
        khd,
        attack_slot,
        attack_cell,
        allowed_cell_roles=frozenset({"NonDamaging"}),
    )
    if timeline is None:
        return None
    slot, cell, call, helper_shape, recovery_lead, total_frames, recovery_open, inclusive_recovery = timeline
    cell_window_start = int(cell.wI16MasterWindowStart)
    cell_window_end = int(cell.wI16MasterWindowEnd)
    break_stun = int(cell.wI16BlockstunFrames)
    if break_stun < 0:
        return None
    break_advantage = break_stun - inclusive_recovery
    return NativeThrowBreakEvidence(
        attack_slot=attack_slot,
        attack_cell=attack_cell,
        total_frames=total_frames,
        cell_window_start_coordinate=cell_window_start,
        cell_window_end_coordinate=cell_window_end,
        recovery_lead=recovery_lead,
        recovery_open_coordinate=recovery_open,
        inclusive_recovery_frames=inclusive_recovery,
        break_stun_frames=break_stun,
        break_advantage=break_advantage,
        resolutions=(
            f"khd-throw-break-attacker-recovery:slot{attack_slot}"
            f"@0x{call.source_pc:X}->packed0x3020;LOCAL1={recovery_lead};"
            f"profile={helper_shape}",
            f"khd-throw-break-attacker-actionable:slot{attack_slot};"
            f"end{total_frames}-lead{recovery_lead}-1=coordinate{recovery_open};"
            f"inclusive{cell_window_start}..{recovery_open}={inclusive_recovery}",
            f"khd-throw-break-defender-stun:cell{attack_cell}[block+0x44={break_stun}]",
            f"native-throw-break-advantage:{break_stun}-{inclusive_recovery}={break_advantage}",
        ),
    )
