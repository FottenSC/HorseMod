"""Static recovery classification for movement routes."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from bank_resolver import BankResolutionContext, resolve_route_target
from luxformats import KhdFile
from move_graph import SlotGraph
from route_trust import classify_slot_cells


@dataclass(frozen=True)
class RecoveryEdgeSemantics:
    src_slot: int
    dst_bank: int
    dst_slot: int
    first_frame: int
    predicate_text: str
    dst_source_kind: str
    dst_cell_role_summary: str
    dst_has_offensive_hit: bool
    dst_has_unresolved_cells: bool
    recovery_role: str
    confidence: str
    reason: str


@dataclass(frozen=True)
class RecoveryTrust:
    status: str
    earliest_return_or_guard_frame: int | None
    earliest_attack_cancel_frame: int | None
    earliest_stance_return_frame: int | None
    earliest_unresolved_frame: int | None
    confidence: str
    reason: str
    edges: list[RecoveryEdgeSemantics]


def _min_or_none(values: Iterable[int]) -> int | None:
    vals = list(values)
    return min(vals) if vals else None


def _cell_summary(cells: list) -> str:
    if not cells:
        return "no_cells"
    counts: dict[str, int] = {}
    for cell in cells:
        counts[cell.role] = counts.get(cell.role, 0) + 1
    return " ".join(f"{role}:{count}" for role, count in sorted(counts.items()))


def classify_recovery_from_slot(
    *,
    cid: str,
    character: str,
    movement_type: str,
    slot_index: int,
    bank: KhdFile,
    graph: SlotGraph,
    neutral_sources: set[int],
    stance_sources: set[int],
    bank_ctx: BankResolutionContext,
) -> RecoveryTrust:
    edges: list[RecoveryEdgeSemantics] = []
    saw_frame_edge = False
    for edge in graph.edges_by_src.get(slot_index, []):
        if edge.predicate_kind != "frame" or not edge.predicate_args:
            continue
        first = edge.predicate_args[0]
        if first is None:
            edges.append(
                RecoveryEdgeSemantics(
                    src_slot=slot_index,
                    dst_bank=edge.dst_bank,
                    dst_slot=edge.dst_slot,
                    first_frame=-1,
                    predicate_text=edge.predicate_text,
                    dst_source_kind="unknown",
                    dst_cell_role_summary="unknown",
                    dst_has_offensive_hit=False,
                    dst_has_unresolved_cells=True,
                    recovery_role="unknown_frame_edge",
                    confidence="unresolved_static_data",
                    reason="frame predicate first-frame argument is indirect",
                )
            )
            continue
        saw_frame_edge = True
        resolved = resolve_route_target(
            source_cid=cid,
            source_character=character,
            movement_type=movement_type,
            src_slot=slot_index,
            dst_bank=edge.dst_bank,
            dst_slot=edge.dst_slot,
            raw_move_id=edge.raw_move_id,
            ctx=bank_ctx,
        )
        if resolved.resolution_status.startswith("unresolved"):
            role = "unresolved_destination"
            edges.append(
                RecoveryEdgeSemantics(
                    src_slot=slot_index,
                    dst_bank=edge.dst_bank,
                    dst_slot=edge.dst_slot,
                    first_frame=first,
                    predicate_text=edge.predicate_text,
                    dst_source_kind=resolved.resolution_status,
                    dst_cell_role_summary="unknown",
                    dst_has_offensive_hit=False,
                    dst_has_unresolved_cells=True,
                    recovery_role=role,
                    confidence=resolved.confidence,
                    reason=resolved.reason,
                )
            )
            continue
        dst_slot = bank.slots[edge.dst_slot] if edge.dst_slot < len(bank.slots) else None
        if dst_slot is None:
            edges.append(
                RecoveryEdgeSemantics(
                    src_slot=slot_index,
                    dst_bank=edge.dst_bank,
                    dst_slot=edge.dst_slot,
                    first_frame=first,
                    predicate_text=edge.predicate_text,
                    dst_source_kind="missing_slot",
                    dst_cell_role_summary="missing_slot",
                    dst_has_offensive_hit=False,
                    dst_has_unresolved_cells=True,
                    recovery_role="unresolved_destination",
                    confidence="unresolved_static_data",
                    reason="same-bank recovery target slot does not exist",
                )
            )
            continue

        cells = classify_slot_cells(bank, dst_slot)
        unresolved = any(cell.confidence == "unresolved" for cell in cells)
        offensive = any(cell.has_offensive_hit for cell in cells)
        if unresolved:
            role = "unresolved_cell_semantics"
            confidence = "unresolved_static_data"
            reason = "recovery target has unresolved cell semantics"
        elif offensive:
            role = "attack_followup"
            confidence = "confirmed_static_data"
            reason = "frame edge target has offensive hit data"
        elif edge.dst_slot == slot_index:
            role = "movement_loop"
            confidence = "confirmed_static_data"
            reason = "frame edge loops back to the movement slot"
        elif edge.dst_slot in neutral_sources:
            role = "return_to_neutral"
            confidence = "confirmed_static_data"
            reason = "frame edge reaches a neutral-like source with no offensive hit data"
        elif edge.dst_slot in stance_sources:
            role = "stance_return"
            confidence = "confirmed_static_data"
            reason = "frame edge reaches a stance root with no offensive hit data"
        else:
            role = "unknown_frame_edge"
            confidence = "unresolved_static_data"
            reason = "frame edge target is non-offensive but not proven neutral, guard, or stance"

        edges.append(
            RecoveryEdgeSemantics(
                src_slot=slot_index,
                dst_bank=edge.dst_bank,
                dst_slot=edge.dst_slot,
                first_frame=first,
                predicate_text=edge.predicate_text,
                dst_source_kind=(
                    "neutral" if edge.dst_slot in neutral_sources
                    else "stance" if edge.dst_slot in stance_sources
                    else "same_bank"
                ),
                dst_cell_role_summary=_cell_summary(cells),
                dst_has_offensive_hit=offensive,
                dst_has_unresolved_cells=unresolved,
                recovery_role=role,
                confidence=confidence,
                reason=reason,
            )
        )

    return_frames = [
        edge.first_frame
        for edge in edges
        if edge.first_frame >= 0 and edge.recovery_role in {"return_to_neutral", "guard_or_control_return"}
    ]
    stance_frames = [
        edge.first_frame
        for edge in edges
        if edge.first_frame >= 0 and edge.recovery_role == "stance_return"
    ]
    attack_frames = [
        edge.first_frame
        for edge in edges
        if edge.first_frame >= 0 and edge.recovery_role in {"attack_followup", "special_followup"}
    ]
    unresolved_frames = [
        edge.first_frame
        for edge in edges
        if edge.first_frame >= 0 and edge.recovery_role.startswith("unresolved")
    ]
    earliest_return = _min_or_none(return_frames)
    earliest_stance = _min_or_none(stance_frames)
    earliest_attack = _min_or_none(attack_frames)
    earliest_unresolved = _min_or_none(unresolved_frames)

    if earliest_return is not None:
        status = "confirmed_static_recovery"
        confidence = "confirmed_static_data"
        reason = f"earliest neutral/control return frame is {earliest_return}"
    elif earliest_stance is not None:
        status = "confirmed_static_stance_recovery"
        confidence = "confirmed_static_data"
        reason = f"earliest stance return frame is {earliest_stance}"
    elif earliest_attack is not None and earliest_unresolved is None:
        status = "attack_followup_only"
        confidence = "confirmed_static_data"
        reason = f"only offensive follow-up frame edges found, earliest {earliest_attack}"
    elif earliest_unresolved is not None:
        status = "unresolved_cell_semantics"
        confidence = "unresolved_static_data"
        reason = f"earliest unresolved recovery edge is frame {earliest_unresolved}"
    elif saw_frame_edge:
        status = "unknown_from_static_bytecode"
        confidence = "unresolved_static_data"
        reason = "frame edges exist but none prove recovery or follow-up semantics"
    else:
        status = "unknown_from_static_bytecode"
        confidence = "unknown"
        reason = "no static frame edges found"

    if earliest_return is not None and earliest_attack is not None and earliest_attack <= earliest_return:
        status = "mixed_recovery_and_followup"
        confidence = "confirmed_static_data"
        reason = "offensive follow-up is not after the earliest neutral/control return"

    return RecoveryTrust(
        status=status,
        earliest_return_or_guard_frame=earliest_return,
        earliest_attack_cancel_frame=earliest_attack,
        earliest_stance_return_frame=earliest_stance,
        earliest_unresolved_frame=earliest_unresolved,
        confidence=confidence,
        reason=reason,
        edges=edges,
    )
