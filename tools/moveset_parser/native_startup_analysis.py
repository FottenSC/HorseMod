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

    ``LuxBattle_ClassifyAttackContact`` first allows special reactions.  When
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


def _timed_variant_guard_resolution(
    instructions: list[object], variant_call_index: int
) -> str | None:
    """Validate the local control block between timing and cell selection.

    Shipped ordinary-contact selectors are either direct, or compile ANDs of
    IF 0x0C/0x1D motion-state-byte-is-zero predicates.  Those predicates are
    the explicit ordinary current/opponent state baseline.  Reject stored
    variables, comparisons, and every other CALLCOND rather than treating a
    nearby selection write as unconditional.
    """

    timing_index = None
    for index in range(variant_call_index - 1, -1, -1):
        instruction = instructions[index]
        if (
            getattr(instruction, "mnemonic", None) == "CALLCOND"
            and getattr(instruction, "imm_b0", None) == CALLCOND_EVALUATE_TIMING
        ):
            timing_index = index
            break
        if (
            getattr(instruction, "mnemonic", None) == "CALLCOND"
            and getattr(instruction, "imm_b0", None) == CALLCOND_SET_ACTIVE_CELL_VARIANT
        ):
            return None
    if timing_index is None:
        return None

    allowed_mnemonics = {"JZ", "JNZ", "JMP_ABS", "SET_ACC_U16", "CALLCOND"}
    baseline_guarded = False
    for index in range(timing_index + 1, variant_call_index):
        instruction = instructions[index]
        mnemonic = getattr(instruction, "mnemonic", None)
        if mnemonic not in allowed_mnemonics:
            return None
        if mnemonic != "CALLCOND":
            continue
        if getattr(instruction, "imm_b0", None) != 0x01:
            return None
        argc = int(getattr(instruction, "imm_b1", 0) or 0)
        if argc != 2 or index < 2:
            return None
        arg_instructions = instructions[index - 2:index]
        if any(
            not bool(getattr(arg, "push_flag", False))
            or getattr(arg, "imm_u16", None) is None
            or int(getattr(arg, "opcode", -1)) not in (0x09, 0x0B)
            for arg in arg_instructions
        ):
            return None
        args = tuple(int(arg.imm_u16) & 0xFFFF for arg in arg_instructions)
        if args[0] not in (0x000C, 0x001D):
            return None
        baseline_guarded = True

    return (
        "native-timed-cell-variant+ordinary-motion-state-zero-baseline"
        "+zero-based-window"
        if baseline_guarded
        else "native-timed-cell-variant+zero-based-window"
    )


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
    route_evidence = (
        NativeStartupEvidence(
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
        if _is_ordinary_standing_contact_cell(route)
        else None
    )

    script = getattr(slot, "bytecode", None)
    if script is None:
        return route_evidence
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
        resolution = _timed_variant_guard_resolution(instructions, index)
        if (
            variant is None
            or selection is None
            or resolution is None
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
        if impact_coordinate > int(cell.wI16MasterWindowEnd):
            continue
        candidates.append(NativeStartupEvidence(
            attack_slot=attack_slot,
            route_cell=route_cell,
            effective_cell=cell_index,
            effective_variant=variant,
            master_window_start=master_start,
            selection_coordinate=selection,
            impact_coordinate=impact_coordinate,
            player_impact_frame=impact_coordinate + 1,
            resolution=resolution,
        ))

    if route_evidence is not None:
        # A timed write after the supplied cell's first active coordinate
        # cannot change first-contact startup.  A write at or before that
        # coordinate replaces the active cell before collision evaluation and
        # therefore must be considered even when the supplied cell is already
        # contact-capable.  Multiple distinct pre-impact writes remain
        # unresolved: choosing one would invent branch/state ordering.
        preimpact = {
            candidate
            for candidate in candidates
            if candidate.selection_coordinate <= route_start
        }
        if not preimpact:
            return route_evidence
        if len(preimpact) != 1:
            return None
        return next(iter(preimpact))

    # Multiple statically reachable writes can be controlled by VM state that
    # the official input row does not establish.  Picking the earliest write
    # would silently choose a branch and can also substitute the wrong damage
    # cell.  Exact duplicate evidence is harmless; any distinct candidate is
    # unresolved until its predicate is proven.
    unique_candidates = set(candidates)
    if len(unique_candidates) != 1:
        return None
    return next(iter(unique_candidates))


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
