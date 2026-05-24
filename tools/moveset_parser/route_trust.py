"""Static trust classification for SC6 basic movement routes.

This module deliberately separates "a route has a cell reference" from
"the route is an attack".  Movement slots can reference cells for several
engine reasons; only a confirmed offensive cell active during the movement
window should block a route from being treated as basic movement.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterable

from luxformats import FLuxMoveBankSlotView, KhdFile


TRUSTED_STATUSES = {
    "trusted_basic",
    "trusted_basic_with_late_followup",
    "trusted_stance_basic",
}


@dataclass
class CellSemantics:
    cell_index: int
    role: str
    active_start: int | None
    active_end: int | None
    has_offensive_hit: bool
    has_throw_or_special: bool
    has_non_offensive_metadata_or_body: bool
    confidence: str
    reason: str


@dataclass
class RouteTrustEvidence:
    cid: str
    character: str
    movement_type: str
    src_slot: int
    dst_slot: int
    animation_index: int
    predicate_kind: str
    predicate_text: str
    source_kind: str
    source_is_neutral: bool
    source_is_stance_root: bool
    same_bank: bool
    motion_decoded: bool
    root_curve_status: str
    cell_semantics: list[CellSemantics]
    earliest_offensive_cell_frame: int | None
    earliest_return_or_guard_frame: int | None
    earliest_attack_cancel_frame: int | None
    earliest_stance_return_frame: int | None = None
    earliest_unresolved_recovery_frame: int | None = None
    recovery_trust_status: str = "unknown_from_static_bytecode"
    recovery_trust_confidence: str = "unknown"
    target_cid: str | None = None
    target_character: str | None = None
    target_bank_kind: str = "local"
    target_resolution_status: str = "resolved_local"
    trust_status: str = "unresolved"
    trust_score: int = 0
    trust_reason: str = ""


@dataclass
class BasicMovementRoute:
    cid: str
    character: str
    movement_type: str
    trust_status: str
    src_slot: int | None
    dst_slot: int | None
    animation_index: int | None
    distance_f4: float | None
    distance_f8: float | None
    distance_f12: float | None
    distance_f16: float | None
    total_distance: float | None
    recovery_status: str
    caveat: str


@dataclass
class SelectedRouteTrust:
    evidence: RouteTrustEvidence
    candidate: Any = field(repr=False)
    curve: Any = field(repr=False)
    recovery_trust: Any = field(default=None, repr=False)


def classify_slot_cells(bank: KhdFile, slot: FLuxMoveBankSlotView) -> list[CellSemantics]:
    cells: list[CellSemantics] = []
    local_count = len(bank.sections[0].entries) if bank.sections else 0
    for cell_idx in slot.nCellBoneIndexPerVariant:
        if cell_idx < 0:
            continue
        if cell_idx >= local_count:
            cells.append(
                CellSemantics(
                    cell_index=cell_idx,
                    role="cross_bank_or_unknown",
                    active_start=None,
                    active_end=None,
                    has_offensive_hit=False,
                    has_throw_or_special=False,
                    has_non_offensive_metadata_or_body=False,
                    confidence="unresolved",
                    reason="cell index is outside this KHD bank; shared/common cell semantics are not resolved",
                )
            )
            continue

        cell = bank.sections[0].entries[cell_idx]
        role = cell.cell_role
        if role == "Attack" and cell.wI16BaseDamage > 0:
            if not cell.has_valid_active_window:
                cells.append(
                    CellSemantics(
                        cell_index=cell_idx,
                        role="offensive_cell_window_unknown",
                        active_start=None,
                        active_end=None,
                        has_offensive_hit=True,
                        has_throw_or_special=cell.attack_class == "Throw",
                        has_non_offensive_metadata_or_body=False,
                        confidence="unresolved",
                        reason=(
                            f"offensive cell: damage-bearing {cell.attack_class} cell has invalid active window "
                            f"{cell.active_frames}"
                        ),
                    )
                )
            else:
                cells.append(
                    CellSemantics(
                        cell_index=cell_idx,
                        role="offensive_attack",
                        active_start=cell.wI16MasterWindowStart,
                        active_end=cell.wI16MasterWindowEnd,
                        has_offensive_hit=True,
                        has_throw_or_special=cell.attack_class == "Throw",
                        has_non_offensive_metadata_or_body=False,
                        confidence="confirmed_static_data",
                        reason=(
                            f"{cell.attack_class} {cell.move_type} cell, damage={cell.wI16BaseDamage}, "
                            f"active={cell.active_frames}"
                        ),
                    )
                )
            continue

        cells.append(
            CellSemantics(
                cell_index=cell_idx,
                role=role.lower(),
                active_start=(
                    cell.wI16MasterWindowStart if cell.has_valid_active_window else None
                ),
                active_end=cell.wI16MasterWindowEnd if cell.has_valid_active_window else None,
                has_offensive_hit=False,
                has_throw_or_special=False,
                has_non_offensive_metadata_or_body=role in {"Header", "NonDamaging", "Sentinel"},
                confidence="confirmed_static_data",
                reason=f"{role} cell is non-damage metadata/body data until a narrower role is proven",
            )
        )
    return cells


def _first_offensive_frame(cells: Iterable[CellSemantics]) -> int | None:
    starts = [
        c.active_start
        for c in cells
        if c.has_offensive_hit and c.active_start is not None
    ]
    return min(starts) if starts else None


def _route_trust_status(
    *,
    direct_movement: bool,
    source_is_neutral: bool,
    source_is_stance_root: bool,
    same_bank: bool,
    motion_decoded: bool,
    cell_semantics: list[CellSemantics],
    recovery_trust: Any | None,
) -> tuple[str, int, str]:
    score = 0
    reasons: list[str] = []
    if direct_movement:
        score += 30
        reasons.append("direct movement predicate")
    else:
        return "measured_but_not_basic", score, "route is not a direct movement predicate"

    if same_bank:
        score += 10
        reasons.append("same-bank route")
    else:
        return "unresolved", score, "cross-bank destination cannot be proven as this character's basic route"

    if motion_decoded:
        score += 20
        reasons.append("root motion decoded")
    else:
        return "unresolved", score, "root motion did not decode with high confidence"

    unresolved_cells = [c for c in cell_semantics if c.confidence == "unresolved"]
    if unresolved_cells:
        return (
            "unresolved",
            score,
            "; ".join(c.reason for c in unresolved_cells[:3]),
        )

    offensive = [c for c in cell_semantics if c.has_offensive_hit]
    if not offensive:
        if source_is_neutral:
            score += 25
            reasons.append("source is neutral-like")
        elif source_is_stance_root:
            score += 12
            reasons.append("source is a stance root")
        else:
            return (
                "measured_but_not_basic",
                score,
                "source is not proven neutral or stance-root basic movement",
            )
        score += 25
        reasons.append("no active offensive cells on destination slot")
        return (
            "trusted_basic" if source_is_neutral else "trusted_stance_basic",
            score,
            "; ".join(reasons),
        )

    recovery_frame = (
        recovery_trust.earliest_return_or_guard_frame
        if recovery_trust is not None
        else None
    )
    if recovery_frame is None and recovery_trust is not None:
        recovery_frame = recovery_trust.earliest_stance_return_frame
    if recovery_frame is None:
        return (
            "unresolved",
            score,
            "offensive cells exist and no static return/recovery frame is known",
        )

    if recovery_trust is None or recovery_trust.status not in {
        "confirmed_static_recovery",
        "confirmed_static_stance_recovery",
    }:
        return (
            "unresolved",
            score,
            "offensive cells exist but recovery is not confirmed strongly enough for late-followup trust",
        )

    early = [
        c for c in offensive
        if c.active_start is not None and c.active_start <= recovery_frame
    ]
    if early:
        return (
            "attack_or_special",
            score,
            f"offensive cell active by frame {min(c.active_start for c in early if c.active_start is not None)} before recovery frame {recovery_frame}",
        )

    earliest_offense = _first_offensive_frame(offensive)
    if (
        recovery_trust.earliest_unresolved_frame is not None
        and earliest_offense is not None
        and recovery_trust.earliest_unresolved_frame < earliest_offense
    ):
        return (
            "unresolved",
            score,
            "unresolved recovery edge appears before late offensive follow-up",
        )

    if not source_is_neutral and not source_is_stance_root:
        return (
            "measured_but_not_basic",
            score,
            "offensive cell appears after recovery but source is not proven neutral or stance-root",
        )

    if source_is_neutral:
        score += 25
        reasons.append("source is neutral-like")
    else:
        score += 12
        reasons.append("source is a stance root")
    score += 15
    return (
        "trusted_basic_with_late_followup" if source_is_neutral else "trusted_stance_basic",
        score,
        f"offensive cells start after static recovery frame {recovery_frame}",
    )


def build_route_trust_evidence(
    *,
    cid: str,
    character: str,
    movement_type: str,
    candidate: Any,
    bank: KhdFile,
    source_is_neutral: bool,
    source_is_stance_root: bool,
    motion_decoded: bool,
    root_curve_status: str,
    earliest_return_or_guard_frame: int | None,
    earliest_attack_cancel_frame: int | None,
    recovery_trust: Any | None = None,
    direct_movement: bool = True,
) -> RouteTrustEvidence:
    slot = bank.slots[candidate.dst_slot]
    cell_semantics = classify_slot_cells(bank, slot)
    status, score, reason = _route_trust_status(
        direct_movement=direct_movement,
        source_is_neutral=source_is_neutral,
        source_is_stance_root=source_is_stance_root,
        same_bank=getattr(candidate, "target_cid", cid) == cid
        and str(getattr(candidate, "target_resolution_status", "resolved_local")).startswith("resolved"),
        motion_decoded=motion_decoded,
        cell_semantics=cell_semantics,
        recovery_trust=recovery_trust,
    )
    target_cid = getattr(candidate, "target_cid", cid)
    target_character = getattr(candidate, "target_character", character)
    target_bank_kind = getattr(candidate, "target_bank_kind", "local")
    target_resolution_status = getattr(candidate, "target_resolution_status", "resolved_local")
    return RouteTrustEvidence(
        cid=cid,
        character=character,
        movement_type=movement_type,
        src_slot=-1 if candidate.src_slot is None else candidate.src_slot,
        dst_slot=candidate.dst_slot,
        animation_index=candidate.slot.animation_index,
        predicate_kind=candidate.predicate_kind,
        predicate_text=candidate.predicate_text,
        source_kind=candidate.source_kind,
        source_is_neutral=source_is_neutral,
        source_is_stance_root=source_is_stance_root,
        same_bank=target_cid == cid and str(target_resolution_status).startswith("resolved"),
        motion_decoded=motion_decoded,
        root_curve_status=root_curve_status,
        cell_semantics=cell_semantics,
        earliest_offensive_cell_frame=_first_offensive_frame(cell_semantics),
        earliest_return_or_guard_frame=earliest_return_or_guard_frame,
        earliest_attack_cancel_frame=earliest_attack_cancel_frame,
        earliest_stance_return_frame=(
            recovery_trust.earliest_stance_return_frame if recovery_trust is not None else None
        ),
        earliest_unresolved_recovery_frame=(
            recovery_trust.earliest_unresolved_frame if recovery_trust is not None else None
        ),
        recovery_trust_status=(
            recovery_trust.status if recovery_trust is not None else "unknown_from_static_bytecode"
        ),
        recovery_trust_confidence=(
            recovery_trust.confidence if recovery_trust is not None else "unknown"
        ),
        target_cid=target_cid,
        target_character=target_character,
        target_bank_kind=target_bank_kind,
        target_resolution_status=target_resolution_status,
        trust_status=status,
        trust_score=score + candidate.score,
        trust_reason=reason,
    )


def route_sort_key(selected: SelectedRouteTrust) -> tuple[int, int, int, int, int]:
    unresolved_basic_source = (
        selected.evidence.trust_status == "unresolved"
        and (selected.evidence.source_is_neutral or selected.evidence.source_is_stance_root)
    )
    priority = {
        "trusted_basic": 0,
        "trusted_basic_with_late_followup": 1,
        "trusted_stance_basic": 2,
        "measured_but_not_basic": 4,
        "unresolved": 3 if unresolved_basic_source else 5,
        "attack_or_special": 5,
    }.get(selected.evidence.trust_status, 9)
    return (
        priority,
        -selected.evidence.trust_score,
        selected.evidence.src_slot,
        selected.evidence.dst_slot,
        selected.evidence.animation_index,
    )


def evidence_to_flat_row(e: RouteTrustEvidence) -> dict[str, Any]:
    offensive = [c for c in e.cell_semantics if c.has_offensive_hit]
    return {
        "cid": e.cid,
        "character": e.character,
        "movement_type": e.movement_type,
        "src_slot": e.src_slot,
        "dst_slot": e.dst_slot,
        "animation_hex": f"{e.animation_index:04X}",
        "predicate_text": e.predicate_text,
        "source_is_neutral": e.source_is_neutral,
        "source_is_stance_root": e.source_is_stance_root,
        "same_bank": e.same_bank,
        "motion_decoded": e.motion_decoded,
        "root_curve_status": e.root_curve_status,
        "cell_count": len(e.cell_semantics),
        "offensive_cell_count": len(offensive),
        "earliest_offensive_cell_frame": e.earliest_offensive_cell_frame,
        "earliest_return_or_guard_frame": e.earliest_return_or_guard_frame,
        "earliest_stance_return_frame": e.earliest_stance_return_frame,
        "earliest_unresolved_recovery_frame": e.earliest_unresolved_recovery_frame,
        "recovery_trust_status": e.recovery_trust_status,
        "recovery_trust_confidence": e.recovery_trust_confidence,
        "target_cid": e.target_cid,
        "target_character": e.target_character,
        "target_bank_kind": e.target_bank_kind,
        "target_resolution_status": e.target_resolution_status,
        "trust_status": e.trust_status,
        "trust_score": e.trust_score,
        "trust_reason": e.trust_reason,
    }


def cell_semantics_rows(evidence: RouteTrustEvidence) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for cell in evidence.cell_semantics:
        rows.append(
            {
                "cid": evidence.cid,
                "character": evidence.character,
                "slot": evidence.dst_slot,
                "animation_hex": f"{evidence.animation_index:04X}",
                "cell_index": cell.cell_index,
                "role": cell.role,
                "active_start": cell.active_start,
                "active_end": cell.active_end,
                "has_offensive_hit": cell.has_offensive_hit,
                "has_throw_or_special": cell.has_throw_or_special,
                "has_non_offensive_metadata_or_body": cell.has_non_offensive_metadata_or_body,
                "confidence": cell.confidence,
                "reason": cell.reason,
            }
        )
    return rows
