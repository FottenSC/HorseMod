"""Static, player-facing startup extraction from KHD move slots.

KHD master-window values are zero-based animation coordinates.  They are not
directly the familiar ``iN`` value: a fresh MoveVM lane starts at coordinate
zero, so coordinate N is the (N + 1)th player-counted frame.

Some slots also begin on a setup cell which cannot produce the ordinary
grounded hit fallback, then select a contact-capable cell through CALLCOND
0x26 at an authored timing point.  This module handles that narrow, proven
pattern without pretending to solve arbitrary MoveVM state.
"""

from __future__ import annotations

from dataclasses import dataclass


CALLCOND_EVALUATE_TIMING = 0x25
CALLCOND_SET_ACTIVE_CELL_VARIANT = 0x26
HIGH_BLOCKABLE_FLAG = 0x0001
MAX_ORDINARY_STARTUP_COORDINATE = 999


@dataclass(frozen=True)
class NativeStartupEvidence:
    attack_slot: int
    route_cell: int
    effective_cell: int
    effective_variant: int
    master_window_start: int
    selection_coordinate: int
    impact_coordinate: int
    player_impact_frame: int
    resolution: str


def _is_ordinary_standing_contact_cell(cell: object) -> bool:
    """Whether native normal-grounded fallback can accept this cell.

    ``LuxMoveVM_EvaluateMoveTransition`` first allows special reactions.  When
    none applies, an ordinary standing defender requires attack-flag bit zero
    (Ghidra ``HighBlockable``) to return the normal hit type.  Damage and an
    active window alone are insufficient.
    """

    return (
        getattr(cell, "cell_role", None) == "Attack"
        and int(getattr(cell, "wI16BaseDamage", 0)) > 0
        and bool(getattr(cell, "has_valid_active_window", False))
        and (int(getattr(cell, "wU16AttackFlags", 0)) & HIGH_BLOCKABLE_FLAG) != 0
    )


def _literal_push_before(instructions: list[object], index: int) -> int | None:
    if index <= 0:
        return None
    previous = instructions[index - 1]
    if (
        not bool(getattr(previous, "push_flag", False))
        or getattr(previous, "imm_u16", None) is None
        or int(getattr(previous, "opcode", -1)) not in (0x09, 0x0B)
    ):
        return None
    return int(previous.imm_u16) & 0xFFFF


def _nearest_timing_point(instructions: list[object], variant_call_index: int) -> int | None:
    """Find the source-order timing predicate governing a variant write.

    Shipped CALLCOND 0x26 sites use a one-word literal and sit in the same
    compact conditional block as their nearest preceding timing predicate.
    Reject sentinels and implausible coordinates rather than interpreting a
    more distant or dynamic predicate.
    """

    for index in range(variant_call_index - 1, -1, -1):
        instruction = instructions[index]
        if (
            getattr(instruction, "mnemonic", None) == "CALLCOND"
            and getattr(instruction, "imm_b0", None) == CALLCOND_EVALUATE_TIMING
        ):
            point = _literal_push_before(instructions, index)
            if point is None or point > MAX_ORDINARY_STARTUP_COORDINATE:
                return None
            return point
        if (
            getattr(instruction, "mnemonic", None) == "CALLCOND"
            and getattr(instruction, "imm_b0", None) == CALLCOND_SET_ACTIVE_CELL_VARIANT
        ):
            return None
    return None


def analyze_player_startup(
    khd: object,
    attack_slot: int,
    route_cell: int,
) -> NativeStartupEvidence | None:
    """Resolve one player-facing startup value for a proven slot/cell route.

    The route cell remains the authoritative starting point.  A different
    variant is accepted only when the starting cell cannot produce ordinary
    grounded contact and the slot contains a literal, timing-gated CALLCOND
    0x26 selecting a contact-capable cell.
    """

    slots = getattr(khd, "slots", ())
    sections = getattr(khd, "sections", ())
    if not (0 <= attack_slot < len(slots)) or not sections:
        return None
    cells = sections[0].entries
    if not 0 <= route_cell < len(cells):
        return None
    slot = slots[attack_slot]
    route = cells[route_cell]
    if (
        getattr(route, "cell_role", None) != "Attack"
        or int(getattr(route, "wI16BaseDamage", 0)) <= 0
        or not bool(getattr(route, "has_valid_active_window", False))
    ):
        return None

    route_start = int(route.wI16MasterWindowStart)
    if _is_ordinary_standing_contact_cell(route):
        return NativeStartupEvidence(
            attack_slot=attack_slot,
            route_cell=route_cell,
            effective_cell=route_cell,
            # Zero denotes that startup uses the supplied route cell without
            # a timing-triggered variant change.  It is not an assertion that
            # the route cell occupies slot reference zero.
            effective_variant=0,
            master_window_start=route_start,
            selection_coordinate=0,
            impact_coordinate=route_start,
            player_impact_frame=route_start + 1,
            resolution="native-master-window-zero-based",
        )

    script = getattr(slot, "bytecode", None)
    if script is None:
        return None
    references = list(getattr(slot, "nCellBoneIndexPerVariant", ()))
    candidates: list[NativeStartupEvidence] = []
    instructions = list(getattr(script, "instructions", ()))
    for index, instruction in enumerate(instructions):
        if not (
            getattr(instruction, "mnemonic", None) == "CALLCOND"
            and getattr(instruction, "imm_b0", None) == CALLCOND_SET_ACTIVE_CELL_VARIANT
            and getattr(instruction, "imm_b1", None) == 1
        ):
            continue
        variant = _literal_push_before(instructions, index)
        selection = _nearest_timing_point(instructions, index)
        if (
            variant is None
            or selection is None
            or not 0 <= variant < len(references)
        ):
            continue
        cell_index = int(references[variant])
        if not 0 <= cell_index < len(cells):
            continue
        cell = cells[cell_index]
        if not _is_ordinary_standing_contact_cell(cell):
            continue
        master_start = int(cell.wI16MasterWindowStart)
        impact_coordinate = max(selection, master_start)
        candidates.append(NativeStartupEvidence(
            attack_slot=attack_slot,
            route_cell=route_cell,
            effective_cell=cell_index,
            effective_variant=variant,
            master_window_start=master_start,
            selection_coordinate=selection,
            impact_coordinate=impact_coordinate,
            player_impact_frame=impact_coordinate + 1,
            resolution="native-timed-cell-variant+zero-based-window",
        ))

    if not candidates:
        return None
    return min(
        candidates,
        key=lambda evidence: (
            evidence.impact_coordinate,
            evidence.effective_variant,
            evidence.effective_cell,
        ),
    )


def serialize_startup_evidence(evidence: NativeStartupEvidence) -> dict[str, int | str]:
    return {
        "attackSlot": evidence.attack_slot,
        "routeCell": evidence.route_cell,
        "effectiveCell": evidence.effective_cell,
        "effectiveVariant": evidence.effective_variant,
        "masterWindowStartCoordinate": evidence.master_window_start,
        "selectionCoordinate": evidence.selection_coordinate,
        "impactCoordinate": evidence.impact_coordinate,
        "playerImpactFrame": evidence.player_impact_frame,
        "resolution": evidence.resolution,
    }
