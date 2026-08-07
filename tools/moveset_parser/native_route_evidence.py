"""Audited, native-only MoveVM routes for official movelist rows.

This is a proof ledger, not a fuzzy matcher.  An entry is usable only for the
exact KHD it was recovered from and only when the official row identity and
the referenced slot/cell structure still match.  The production exporter may
consume these records; comparison-sheet tooling must never author them.

The first record documents Astaroth's Bear Tamer (B.6A).  Static bytecode
analysis establishes this route:

* standing dispatcher slot 2659, B/neutral branch -> slot 308
* slot 308's A follow-up, non-back arm -> lane-1 transition to slot 310
* default attack variants -> cells 67 and 71

The PCs in ``resolutions`` are KHD file offsets.  The dispatcher and
TransitionAuthor semantics were independently established from the native
MoveVM evaluator/transition functions in Ghidra; no game runtime observation
or comparison-sheet value is used here.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
from typing import Any

from native_frame_analysis import (
    NativeFrameAdvantageEvidence,
    analyze_confirmed_slot_frames,
)
from native_reaction_table import LuxHitReactionMoveIdTable


@dataclass(frozen=True)
class NativeRouteEvidence:
    cid: str
    official_order: int
    move_id: int
    name: str
    command: str
    main_definition_id: int
    khd_sha256: str
    slots: tuple[int, ...]
    cells: tuple[int, ...]
    animations: tuple[int, ...]
    resolutions: tuple[str, ...]
    frame_advantage: NativeFrameAdvantageEvidence | None = None


_ROUTES: dict[tuple[str, int], NativeRouteEvidence] = {
    ("012", 75): NativeRouteEvidence(
        cid="012",
        official_order=75,
        move_id=188,
        name="Bear Tamer",
        command="B.6A",
        main_definition_id=634,
        khd_sha256="D9B0056C2708B701BC3ACACF3F572673F67636359B069E704CF95CAA3D3BDE2D",
        slots=(308, 310),
        cells=(67, 71),
        animations=(40, 64),
        resolutions=(
            "khd-standing-input-dispatch:slot2659@0xD526D->slot308",
            "khd-followup-lane1:slot308@0x61EEE->slot310",
            "khd-default-cell-variants:slot308[0]->cell67;slot310[0]->cell71",
        ),
    ),
}


def resolve_native_route(
    cid: str,
    move: dict[str, Any],
    khd: Any | None,
    reaction_table: LuxHitReactionMoveIdTable | None = None,
) -> NativeRouteEvidence | None:
    """Return an audited route only if every version/identity check passes.

    A matching official row with a different KHD is an error rather than a
    reason to reuse stale slot numbers.  Rows without a ledger entry simply
    return ``None`` and remain honestly unresolved/heuristic.
    """
    order = int(move.get("order", -1))
    route = _ROUTES.get((cid.casefold(), order))
    if route is None:
        return None
    if khd is None:
        raise ValueError(f"native route {cid}/{order} requires a parsed KHD")

    command_sets = move.get("commandSets", []) or []
    primary_main = int(command_sets[0].get("mainIndex", 0) or 0) if command_sets else 0
    identity = (
        int(move.get("moveId", 0) or 0),
        str(move.get("name", "")),
        str(move.get("input", "")),
        primary_main,
    )
    expected_identity = (
        route.move_id,
        route.name,
        route.command,
        route.main_definition_id,
    )
    if identity != expected_identity:
        raise ValueError(
            f"native route identity drift for {cid}/{order}: "
            f"expected {expected_identity!r}, got {identity!r}"
        )

    digest = hashlib.sha256(khd.raw).hexdigest().upper()
    if digest != route.khd_sha256:
        raise ValueError(
            f"native route KHD drift for {cid}/{order}: "
            f"expected {route.khd_sha256}, got {digest}"
        )

    if not khd.sections:
        raise ValueError(f"native route {cid}/{order} has no KHD attack-cell section")
    attack_cells = khd.sections[0].entries
    if not (len(route.slots) == len(route.cells) == len(route.animations)):
        raise ValueError(f"native route {cid}/{order} has inconsistent proof lengths")

    for path_index, (slot_idx, cell_idx, animation_idx) in enumerate(
        zip(route.slots, route.cells, route.animations)
    ):
        if not (0 <= slot_idx < len(khd.slots)):
            raise ValueError(f"native route {cid}/{order} slot {slot_idx} is out of range")
        if not (0 <= cell_idx < len(attack_cells)):
            raise ValueError(f"native route {cid}/{order} cell {cell_idx} is out of range")
        slot = khd.slots[slot_idx]
        if slot.wAnimationIndex_00 != animation_idx:
            raise ValueError(
                f"native route {cid}/{order} slot {slot_idx} animation drift: "
                f"expected {animation_idx}, got {slot.wAnimationIndex_00}"
            )
        if not slot.nCellBoneIndexPerVariant or slot.nCellBoneIndexPerVariant[0] != cell_idx:
            raise ValueError(
                f"native route {cid}/{order} path {path_index} no longer selects "
                f"cell {cell_idx} as the default slot variant"
            )
        if attack_cells[cell_idx].cell_role != "Attack":
            raise ValueError(
                f"native route {cid}/{order} cell {cell_idx} is no longer an attack cell"
            )

    frame = analyze_confirmed_slot_frames(
        khd,
        route.slots[-1],
        route.cells[-1],
        reaction_table=reaction_table,
    )
    return replace(route, frame_advantage=frame)
