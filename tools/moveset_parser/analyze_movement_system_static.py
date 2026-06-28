#!/usr/bin/env python3
"""Static SC6 movement-system analyzer.

This program joins the KHD move-bank parser, MoveVM transition graph, and
MOT offset-table parser into one reproducible movement evidence pass.  It is
intentionally static-only: it never reads live game memory and it never claims
world-unit travel distances unless the HgMotion root-translation format has
been decoded.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import shutil
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable

from bank_resolver import BankResolutionContext, ResolvedRouteTarget, resolve_route_target
from luxformats import FLuxMoveBankSlotView, KhdFile, MotionBankFile, parse_khd, parse_mot
from motion_decode import RootMotionCurve, decode_root_motion_curve
from move_graph import SlotEdge, SlotGraph, build_slot_graph, identify_stance_roots
from recovery_trust import RecoveryTrust, classify_recovery_from_slot
from route_trust import (
    TRUSTED_STATUSES,
    BasicMovementRoute,
    RouteTrustEvidence,
    SelectedRouteTrust,
    build_route_trust_evidence,
    cell_semantics_rows,
    evidence_to_flat_row,
    route_sort_key,
)


RANKABLE_TRUST_STATUSES = {"trusted_basic", "trusted_stance_basic"}


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DUMP_BATTLE = REPO_ROOT / "dump" / "Battle"
DEFAULT_FULL_DUMP_BATTLE = (
    Path("C:/Users/prest/Documents/SoulcaliburModding/SCVI Sound Tools/dump")
    / "Battle"
)
DEFAULT_OUT_DIR = REPO_ROOT / "docs" / "investigations" / "generated"

CORE_MOVEMENT_SLOTS = (0, 1, 2, 3, 6, 7, 9, 10, 21)

CHARA_NAMES: dict[str, str] = {
    "001": "Mitsurugi",
    "002": "Seong Mi-na",
    "003": "Taki",
    "004": "Maxi",
    "005": "Voldo",
    "006": "Sophitia",
    "007": "Siegfried",
    "009": "Hwang",
    "00b": "Ivy",
    "00c": "Kilik",
    "00d": "Xianghua",
    "00f": "Yoshimitsu",
    "011": "Nightmare",
    "012": "Astaroth",
    "014": "Cervantes",
    "015": "Raphael",
    "016": "Talim",
    "017": "Cassandra",
    "022": "Setsuka",
    "023": "Tira",
    "024": "Zasalamel",
    "028": "Hilde",
    "030": "Amy",
    "060": "2B",
    "061": "Haohmaru",
    "062": "Groh",
    "064": "Azwel",
    "065": "Geralt",
    "066": "Unknown (cid 066)",
    "0ff": "Common motion bank",
}


@dataclass
class MotionSectionInfo:
    status: str
    motion_index: int
    offset: int | None = None
    size: int = 0
    sha1: str = ""
    first16: str = ""
    entropy: float | None = None
    shared_hash_key: str = ""
    shared_character_count: int = 0
    shared_characters: list[str] | None = None
    common_bank_size: int | None = None
    common_bank_sha1: str = ""


@dataclass
class SlotInfo:
    slot: int
    animation_index: int
    animation_hex: str
    animation_length: float
    motion_flags: int
    bytecode_offset: int
    has_attack_cell: bool
    cells: list[int]
    mot: MotionSectionInfo


@dataclass
class MovementCandidate:
    cid: str
    character: str
    movement_type: str
    score: int
    confidence: str
    distance_tier: str
    reason: str
    source_kind: str
    src_slot: int | None
    dst_slot: int
    dst_bank: int
    predicate_text: str
    predicate_kind: str
    predicate_args: list[int | None]
    callcond_idx: int | None
    source_pc: int | None
    slot: SlotInfo
    target_cid: str | None = None
    target_character: str | None = None
    target_bank_kind: str = "local"
    target_resolution_status: str = "resolved_local"


@dataclass
class TransitionRow:
    cid: str
    character: str
    src_slot: int
    dst_bank: int
    dst_slot: int
    movement_type: str
    predicate_kind: str
    predicate_text: str
    predicate_args: str
    raw_move_id: str
    source_pc: str
    callcond_idx: str
    dst_animation_hex: str
    dst_animation_length: float
    dst_has_attack_cell: bool
    dst_mot_status: str
    dst_mot_size: int
    dst_mot_sha1: str
    distance_tier: str
    confidence: str
    score: int
    reason: str


@dataclass
class BackstepRoute:
    cid: str
    character: str
    route_kind: str
    src_slot: int
    dst_slot: int
    animation_index: int
    predicate_text: str
    has_attack_cell: bool
    source_rank: int
    selection_score: int
    selection_reason: str
    candidate: MovementCandidate
    curve: RootMotionCurve
    recovery_trust: RecoveryTrust | None = None


@dataclass
class BackstepQuality:
    cid: str
    character: str
    route_kind: str
    quality_status: str
    quality_score: float | None
    quality_grade: str
    total_back_distance: float | None
    back_distance_f4: float | None
    back_distance_f8: float | None
    back_distance_f12: float | None
    back_distance_f16: float | None
    first_frame_8_units: int | None
    first_frame_16_units: int | None
    first_frame_30_units: int | None
    recovery_estimate_status: str
    earliest_guard_or_neutral_frame: int | None
    earliest_attack_cancel_frame: int | None
    plain_movement: bool
    has_attack_cell: bool
    root_decode_confidence: str
    src_slot: int
    dst_slot: int
    animation_hex: str
    selection_reason: str
    reason: str


def _cid_from_path(path: Path, prefix: str, suffix: str) -> str:
    name = path.name.lower()
    return name.removeprefix(prefix).removesuffix(suffix)


def _copy_missing(local: Path, full: Path) -> bool:
    if local.exists() or not full.exists():
        return False
    local.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(full, local)
    return True


def _file_sha1(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def _entropy(data: bytes) -> float:
    if not data:
        return 0.0
    hist = [0] * 256
    for b in data:
        hist[b] += 1
    total = len(data)
    return -sum((n / total) * math.log2(n / total) for n in hist if n)


def _valid_cells(slot: FLuxMoveBankSlotView) -> list[int]:
    return [c for c in slot.nCellBoneIndexPerVariant if c >= 0]


def _slot_has_attack_cell(slot: FLuxMoveBankSlotView) -> bool:
    return bool(_valid_cells(slot))


def _motion_section(
    mot: MotionBankFile | None,
    common_mot: MotionBankFile | None,
    anim_idx: int,
) -> MotionSectionInfo:
    if anim_idx == 0xFFFF:
        return MotionSectionInfo(status="sentinel_animation", motion_index=anim_idx)
    if mot is None:
        return MotionSectionInfo(status="missing_character_mot", motion_index=anim_idx)
    if anim_idx >= mot.count:
        return MotionSectionInfo(status="motion_index_out_of_range", motion_index=anim_idx)

    off = mot.offsets[anim_idx]
    size = mot.sizes[anim_idx]
    raw = mot.raw[off : off + size] if size > 0 else b""
    common_size: int | None = None
    common_sha1 = ""
    if common_mot is not None and anim_idx < common_mot.count:
        coff = common_mot.offsets[anim_idx]
        common_size = common_mot.sizes[anim_idx]
        craw = common_mot.raw[coff : coff + common_size] if common_size > 0 else b""
        common_sha1 = _file_sha1(craw) if craw else ""

    if size == 0:
        return MotionSectionInfo(
            status="empty_local_section_possible_common_or_loader_fallback",
            motion_index=anim_idx,
            offset=off,
            size=0,
            common_bank_size=common_size,
            common_bank_sha1=common_sha1,
        )

    sha1 = _file_sha1(raw)
    return MotionSectionInfo(
        status="character_motion_section",
        motion_index=anim_idx,
        offset=off,
        size=size,
        sha1=sha1,
        first16=raw[:16].hex(),
        entropy=round(_entropy(raw), 4),
        shared_hash_key=f"{size}:{sha1}",
        common_bank_size=common_size,
        common_bank_sha1=common_sha1,
    )


def _slot_info(
    slot: FLuxMoveBankSlotView,
    mot: MotionBankFile | None,
    common_mot: MotionBankFile | None,
) -> SlotInfo:
    return SlotInfo(
        slot=slot.slot_index,
        animation_index=slot.wAnimationIndex_00,
        animation_hex=f"{slot.wAnimationIndex_00:04X}",
        animation_length=float(slot.total_frames),
        motion_flags=slot.wMotionFlags_06,
        bytecode_offset=slot.dwBytecodeOffset_38,
        has_attack_cell=_slot_has_attack_cell(slot),
        cells=list(slot.nCellBoneIndexPerVariant),
        mot=_motion_section(mot, common_mot, slot.wAnimationIndex_00),
    )


def _movement_type_from_predicate(text: str) -> str:
    t = text.lower()
    has_back = "back" in t or re.search(r"(^|[^0-9])[147]($|[^0-9])", t) is not None
    has_forward = "forward" in t or re.search(r"(^|[^0-9])[369]($|[^0-9])", t) is not None
    has_up = "up" in t or re.search(r"(^|[^0-9])[789]($|[^0-9])", t) is not None
    has_down = "down" in t or re.search(r"(^|[^0-9])[123]($|[^0-9])", t) is not None

    side_count = int(has_up) + int(has_down)
    axial_count = int(has_back) + int(has_forward) + side_count
    if axial_count == 0:
        return "unknown_directional"
    if axial_count >= 3 or ("stick:any" in t) or ("back|forward" in t):
        return "eight_way_or_ambiguous"
    if has_back and side_count:
        return "back_diagonal"
    if has_forward and side_count:
        return "forward_diagonal"
    if has_back and not has_forward:
        return "backstep_candidate"
    if has_forward and not has_back:
        return "forward_step_candidate"
    if has_up and not has_down:
        return "sidestep_up_candidate"
    if has_down and not has_up:
        return "sidestep_down_candidate"
    if has_up or has_down:
        return "sidestep_ambiguous_candidate"
    return "eight_way_or_ambiguous"


def _distance_tier(mot: MotionSectionInfo) -> str:
    if mot.status == "character_motion_section":
        if mot.shared_character_count > 1:
            return "shared_movement_asset_no_exact_distance"
        return "relative_authored_difference_no_exact_distance"
    if mot.status == "empty_local_section_possible_common_or_loader_fallback":
        return "unknown_empty_local_section_possible_fallback"
    return "unknown_no_motion_payload"


def _candidate_score(movement_type: str, edge: SlotEdge | None, slot: FLuxMoveBankSlotView, mot: MotionSectionInfo) -> tuple[int, str]:
    score = 0
    reasons: list[str] = []
    if edge is None:
        score += 20
        reasons.append("early core movement-state slot sampled by index")
    else:
        score += 35
        reasons.append("MoveVM transition is gated by decoded direction predicate")
        if edge.dst_bank < 4:
            score += 10
            reasons.append("packed move id resolves through this character's FLuxMoveBank buckets")
        if edge.predicate_kind == "direction":
            score += 15
        if movement_type in {"backstep_candidate", "forward_step_candidate", "sidestep_up_candidate", "sidestep_down_candidate"}:
            score += 15
            reasons.append("predicate maps to one primary direction")
        elif "diagonal" in movement_type:
            score += 8
            reasons.append("predicate maps to diagonal movement")
        else:
            score -= 8
            reasons.append("predicate is directional but ambiguous")
    reasons.append("cell references are classified later by route trust")
    if slot.wAnimationIndex_00 != 0xFFFF:
        score += 8
        reasons.append("destination has a concrete motion id")
    if mot.status == "character_motion_section":
        score += 12
        reasons.append("local MOT section is non-empty")
    elif mot.status.startswith("empty_local"):
        score -= 4
        reasons.append("local MOT section is empty; common/fallback path not fully decoded")
    else:
        score -= 10
        reasons.append(mot.status)
    return score, "; ".join(reasons)


def _confidence(score: int, edge: SlotEdge | None, movement_type: str) -> str:
    if edge is None:
        return "confirmed_static_data"
    if score >= 75 and "ambiguous" not in movement_type:
        return "inferred_static"
    if score >= 55:
        return "inferred_static_low"
    return "unknown_without_motion_decode"


def _candidate_from_slot(
    cid: str,
    character: str,
    movement_type: str,
    source_kind: str,
    slot: FLuxMoveBankSlotView,
    mot: MotionBankFile | None,
    common_mot: MotionBankFile | None,
    edge: SlotEdge | None = None,
) -> MovementCandidate:
    sinfo = _slot_info(slot, mot, common_mot)
    score, reason = _candidate_score(movement_type, edge, slot, sinfo.mot)
    target_bank_kind = "local"
    target_resolution_status = "resolved_local"
    if edge is not None and edge.dst_bank != 0:
        target_bank_kind = f"move_bucket_{edge.dst_bank}"
        target_resolution_status = "resolved_move_bucket"
    return MovementCandidate(
        cid=cid,
        character=character,
        movement_type=movement_type,
        score=score,
        confidence=_confidence(score, edge, movement_type),
        distance_tier=_distance_tier(sinfo.mot),
        reason=reason,
        source_kind=source_kind,
        src_slot=edge.src_slot if edge else None,
        dst_slot=slot.slot_index,
        dst_bank=edge.dst_bank if edge else 0,
        predicate_text=edge.predicate_text if edge else "(core slot sample)",
        predicate_kind=edge.predicate_kind if edge else "core_slot",
        predicate_args=list(edge.predicate_args) if edge else [],
        callcond_idx=edge.callcond_idx if edge else None,
        source_pc=edge.source_pc if edge else None,
        slot=sinfo,
        target_cid=cid,
        target_character=character,
        target_bank_kind=target_bank_kind,
        target_resolution_status=target_resolution_status,
    )


def _flatten_candidate(c: MovementCandidate) -> dict[str, Any]:
    mot = c.slot.mot
    return {
        "cid": c.cid,
        "character": c.character,
        "movement_type": c.movement_type,
        "score": c.score,
        "confidence": c.confidence,
        "distance_tier": c.distance_tier,
        "source_kind": c.source_kind,
        "src_slot": "" if c.src_slot is None else c.src_slot,
        "dst_slot": c.dst_slot,
        "dst_bank": c.dst_bank,
        "target_cid": c.target_cid,
        "target_character": c.target_character,
        "target_bank_kind": c.target_bank_kind,
        "target_resolution_status": c.target_resolution_status,
        "predicate_text": c.predicate_text,
        "predicate_kind": c.predicate_kind,
        "predicate_args": " ".join("" if v is None else f"0x{v:04X}" for v in c.predicate_args),
        "animation_hex": c.slot.animation_hex,
        "animation_length": c.slot.animation_length,
        "has_attack_cell": c.slot.has_attack_cell,
        "cells": " ".join(str(v) for v in c.slot.cells),
        "mot_status": mot.status,
        "mot_size": mot.size,
        "mot_sha1": mot.sha1,
        "mot_entropy": "" if mot.entropy is None else mot.entropy,
        "shared_character_count": mot.shared_character_count,
        "shared_characters": " ".join(mot.shared_characters or []),
        "common_bank_size": "" if mot.common_bank_size is None else mot.common_bank_size,
        "common_bank_sha1": mot.common_bank_sha1,
        "reason": c.reason,
    }


def _direction_edges(bank: KhdFile, raw: bytes) -> list[SlotEdge]:
    graph = build_slot_graph(bank, raw)
    return _direction_edges_from_graph(graph)


def _direction_edges_from_graph(graph: SlotGraph) -> list[SlotEdge]:
    out: list[SlotEdge] = []
    for edges in graph.edges_by_src.values():
        for edge in edges:
            if edge.predicate_kind == "direction":
                out.append(edge)
    return out


def _is_primary_back(c: MovementCandidate) -> bool:
    return c.movement_type == "backstep_candidate" and c.predicate_kind == "direction"


def _motion_raw(mot: MotionBankFile | None, anim_idx: int) -> bytes:
    if mot is None or anim_idx == 0xFFFF or anim_idx >= mot.count:
        return b""
    return mot.section(anim_idx)


def _resolution_row(r: ResolvedRouteTarget, raw_move_id: int) -> dict[str, Any]:
    return {
        "cid": r.source_cid,
        "character": r.source_character,
        "movement_type": r.movement_type,
        "src_slot": r.src_slot,
        "dst_bank": r.dst_bank,
        "dst_slot": r.dst_slot,
        "raw_move_id": f"0x{raw_move_id:04X}",
        "target_cid": r.target_cid,
        "target_character": r.target_character,
        "target_slot_exists": r.target_slot_index is not None,
        "target_animation_hex": (
            f"{r.target_animation_index:04X}" if r.target_animation_index is not None else ""
        ),
        "resolution_status": r.resolution_status,
        "confidence": r.confidence,
        "reason": r.reason,
    }


def _recovery_rows(
    *,
    cid: str,
    character: str,
    movement_type: str,
    src_slot: int,
    dst_slot: int,
    trust: RecoveryTrust,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for edge in trust.edges:
        rows.append(
            {
                "cid": cid,
                "character": character,
                "movement_type": movement_type,
                "src_slot": src_slot,
                "dst_slot": dst_slot,
                "frame": edge.first_frame if edge.first_frame >= 0 else None,
                "edge_dst_bank": edge.dst_bank,
                "edge_dst_slot": edge.dst_slot,
                "predicate_text": edge.predicate_text,
                "recovery_role": edge.recovery_role,
                "dst_cell_role_summary": edge.dst_cell_role_summary,
                "confidence": edge.confidence,
                "reason": edge.reason,
            }
        )
    if not rows:
        rows.append(
            {
                "cid": cid,
                "character": character,
                "movement_type": movement_type,
                "src_slot": src_slot,
                "dst_slot": dst_slot,
                "frame": None,
                "edge_dst_bank": None,
                "edge_dst_slot": None,
                "predicate_text": "",
                "recovery_role": trust.status,
                "dst_cell_role_summary": "",
                "confidence": trust.confidence,
                "reason": trust.reason,
            }
        )
    return rows


def _neutral_like_sources(bank: KhdFile, graph: SlotGraph) -> set[int]:
    roots = identify_stance_roots(bank, graph)
    neutral = set(CORE_MOVEMENT_SLOTS)
    neutral.update(r.slot_idx for r in roots)
    for src, edges in graph.edges_by_src.items():
        types = {
            _movement_type_from_predicate(e.predicate_text)
            for e in edges
            if e.predicate_kind == "direction"
        }
        if {
            "backstep_candidate",
            "forward_step_candidate",
            "sidestep_up_candidate",
            "sidestep_down_candidate",
        }.issubset(types):
            neutral.add(src)
    return neutral


def _estimate_recovery(
    route: BackstepRoute,
    bank: KhdFile,
    graph: SlotGraph,
    neutral_sources: set[int],
) -> tuple[str, int | None, int | None]:
    trust = getattr(route, "recovery_trust", None)
    if trust is not None:
        return (
            trust.status,
            trust.earliest_return_or_guard_frame or trust.earliest_stance_return_frame,
            trust.earliest_attack_cancel_frame,
        )
    return "unknown_from_static_bytecode", None, None


def _estimate_recovery_from_slot(
    slot_index: int,
    bank: KhdFile,
    graph: SlotGraph,
    neutral_sources: set[int],
    *,
    cid: str,
    character: str,
    movement_type: str,
    stance_sources: set[int],
    bank_ctx: BankResolutionContext,
) -> RecoveryTrust:
    return classify_recovery_from_slot(
        cid=cid,
        character=character,
        movement_type=movement_type,
        slot_index=slot_index,
        bank=bank,
        graph=graph,
        neutral_sources=neutral_sources,
        stance_sources=stance_sources,
        bank_ctx=bank_ctx,
    )


def _curve_back_distance_at(curve: RootMotionCurve, frame_no: int) -> float | None:
    if curve.confidence != "high" or not curve.frames:
        return None
    idx = min(frame_no, len(curve.frames) - 1)
    return abs(curve.frames[idx].cumulative_z)


def _curve_axis_distance_at(curve: RootMotionCurve, movement_type: str, frame_no: int) -> float | None:
    if curve.confidence != "high" or not curve.frames:
        return None
    idx = min(frame_no, len(curve.frames) - 1)
    frame = curve.frames[idx]
    if movement_type in {"sidestep_up_candidate", "sidestep_down_candidate"}:
        return abs(frame.cumulative_x)
    if movement_type in {"back_diagonal", "forward_diagonal", "eight_way_or_ambiguous"}:
        return math.hypot(frame.cumulative_x, frame.cumulative_z)
    return abs(frame.cumulative_z)


def _curve_peak_axis_distance(curve: RootMotionCurve, movement_type: str) -> float | None:
    if curve.confidence != "high" or not curve.frames:
        return None
    if movement_type in {"sidestep_up_candidate", "sidestep_down_candidate"}:
        return max(abs(f.cumulative_x) for f in curve.frames)
    if movement_type in {"back_diagonal", "forward_diagonal", "eight_way_or_ambiguous"}:
        return max(math.hypot(f.cumulative_x, f.cumulative_z) for f in curve.frames)
    return max(abs(f.cumulative_z) for f in curve.frames)


def _first_frame_at_distance(curve: RootMotionCurve, movement_type: str, threshold: float) -> int | None:
    if curve.confidence != "high":
        return None
    for frame in curve.frames:
        if movement_type in {"sidestep_up_candidate", "sidestep_down_candidate"}:
            value = abs(frame.cumulative_x)
        elif movement_type in {"back_diagonal", "forward_diagonal", "eight_way_or_ambiguous"}:
            value = math.hypot(frame.cumulative_x, frame.cumulative_z)
        else:
            value = abs(frame.cumulative_z)
        if value >= threshold:
            return frame.frame
    return None


def _is_direct_basic_movement_candidate(c: MovementCandidate) -> bool:
    return (
        c.predicate_kind == "direction"
        and c.movement_type
        in {
            "backstep_candidate",
            "sidestep_up_candidate",
            "sidestep_down_candidate",
            "forward_step_candidate",
            "back_diagonal",
            "forward_diagonal",
            "eight_way_or_ambiguous",
        }
    )


def _select_basic_movement_routes(
    cid: str,
    character: str,
    direction_candidates: list[MovementCandidate],
    bank: KhdFile,
    graph: SlotGraph,
    mot: MotionBankFile | None,
    movement_type: str,
    neutral_sources: set[int],
    stance_sources: set[int],
    bank_ctx: BankResolutionContext,
) -> list[SelectedRouteTrust]:
    selected: list[SelectedRouteTrust] = []
    candidates = [
        c for c in direction_candidates
        if c.movement_type == movement_type
    ]
    for cand in candidates:
        curve = decode_root_motion_curve(_motion_raw(mot, cand.slot.animation_index))
        recovery_trust = _estimate_recovery_from_slot(
            cand.dst_slot,
            bank,
            graph,
            neutral_sources,
            cid=cid,
            character=character,
            movement_type=movement_type,
            stance_sources=stance_sources,
            bank_ctx=bank_ctx,
        )
        guard_frame = recovery_trust.earliest_return_or_guard_frame or recovery_trust.earliest_stance_return_frame
        attack_frame = recovery_trust.earliest_attack_cancel_frame
        evidence = build_route_trust_evidence(
            cid=cid,
            character=character,
            movement_type=movement_type,
            candidate=cand,
            bank=bank,
            source_is_neutral=cand.src_slot in neutral_sources if cand.src_slot is not None else False,
            source_is_stance_root=cand.src_slot in stance_sources if cand.src_slot is not None else False,
            motion_decoded=curve.confidence == "high",
            root_curve_status=curve.status,
            earliest_return_or_guard_frame=guard_frame,
            earliest_attack_cancel_frame=attack_frame,
            recovery_trust=recovery_trust,
            direct_movement=_is_direct_basic_movement_candidate(cand),
        )
        selected.append(
            SelectedRouteTrust(
                evidence=evidence,
                candidate=cand,
                curve=curve,
                recovery_trust=recovery_trust,
            )
        )
    selected.sort(key=route_sort_key)
    return selected


def _basic_route_row(selected: SelectedRouteTrust | None, cid: str, character: str, movement_type: str) -> dict[str, Any]:
    if selected is None:
        return {
            "cid": cid,
            "character": character,
            "movement_type": movement_type,
            "trust_status": "unresolved",
            "src_slot": None,
            "dst_slot": None,
            "animation_hex": "",
            "distance_f4": None,
            "distance_f8": None,
            "distance_f12": None,
            "distance_f16": None,
            "total_distance": None,
            "earliest_return_or_guard_frame": None,
            "earliest_attack_cancel_frame": None,
            "earliest_stance_return_frame": None,
            "earliest_unresolved_recovery_frame": None,
            "recovery_trust_status": "unknown_no_static_route",
            "recovery_trust_confidence": "unknown",
            "target_character": None,
            "target_bank_kind": "unknown",
            "route_resolution_status": "unresolved_no_route",
            "trust_score": 0,
            "trust_reason": "no direction-gated route found for this movement type",
            "recovery_status": "unknown_no_static_route",
            "caveat": "no direction-gated route found for this movement type",
        }

    e = selected.evidence
    curve = selected.curve
    trusted = e.trust_status in TRUSTED_STATUSES
    return {
        "cid": e.cid,
        "character": e.character,
        "movement_type": e.movement_type,
        "trust_status": e.trust_status,
        "src_slot": e.src_slot,
        "dst_slot": e.dst_slot,
        "animation_hex": f"{e.animation_index:04X}",
        "distance_f4": _curve_axis_distance_at(curve, e.movement_type, 4),
        "distance_f8": _curve_axis_distance_at(curve, e.movement_type, 8),
        "distance_f12": _curve_axis_distance_at(curve, e.movement_type, 12),
        "distance_f16": _curve_axis_distance_at(curve, e.movement_type, 16),
        "total_distance": _curve_peak_axis_distance(curve, e.movement_type) if trusted else None,
        "earliest_return_or_guard_frame": e.earliest_return_or_guard_frame,
        "earliest_attack_cancel_frame": e.earliest_attack_cancel_frame,
        "earliest_stance_return_frame": e.earliest_stance_return_frame,
        "earliest_unresolved_recovery_frame": e.earliest_unresolved_recovery_frame,
        "recovery_trust_status": e.recovery_trust_status,
        "recovery_trust_confidence": e.recovery_trust_confidence,
        "target_character": e.target_character,
        "target_bank_kind": e.target_bank_kind,
        "route_resolution_status": e.target_resolution_status,
        "trust_score": e.trust_score,
        "trust_reason": e.trust_reason,
        "recovery_status": (
            "estimated_from_static_frame_edges"
            if e.earliest_return_or_guard_frame is not None or e.earliest_attack_cancel_frame is not None
            else "unknown_from_static_bytecode"
        ),
        "caveat": "" if trusted else e.trust_reason,
    }


def _movement_quality_row(
    cand: MovementCandidate,
    curve: RootMotionCurve,
    bank: KhdFile,
    graph: SlotGraph,
    neutral_sources: set[int],
    trust_evidence: RouteTrustEvidence | None = None,
) -> dict[str, Any]:
    guard_frame: int | None = None
    attack_frame: int | None = None
    recovery_status = "unknown_from_static_bytecode"
    recovery_confidence = "unknown"
    if trust_evidence is not None:
        route_kind = trust_evidence.trust_status
        plain_movement = trust_evidence.trust_status in RANKABLE_TRUST_STATUSES
        guard_frame = trust_evidence.earliest_return_or_guard_frame
        attack_frame = trust_evidence.earliest_attack_cancel_frame
        recovery_status = trust_evidence.recovery_trust_status
        recovery_confidence = trust_evidence.recovery_trust_confidence
    else:
        is_neutral = cand.src_slot in neutral_sources if cand.src_slot is not None else False
        plain_movement = (
            is_neutral
            and not cand.slot.has_attack_cell
            and cand.predicate_kind == "direction"
            and cand.slot.mot.status == "character_motion_section"
        )
        route_kind = "trusted_basic" if plain_movement else "unresolved"
    high = curve.confidence == "high" and plain_movement
    reason_parts = [cand.reason, curve.reason]
    if trust_evidence is not None:
        reason_parts.append(trust_evidence.trust_reason)
    if not high:
        reason_parts.append("unranked unless route is trusted basic movement and root motion is decoded with high confidence")
    total_distance = _curve_peak_axis_distance(curve, cand.movement_type)
    return {
        "cid": cand.cid,
        "character": cand.character,
        "movement_type": cand.movement_type,
        "route_kind": route_kind,
        "quality_status": "rankable_static_distance" if high else "unranked",
        "quality_score": None,
        "quality_grade": "Unranked",
        "distance_f4": _curve_axis_distance_at(curve, cand.movement_type, 4),
        "distance_f8": _curve_axis_distance_at(curve, cand.movement_type, 8),
        "distance_f12": _curve_axis_distance_at(curve, cand.movement_type, 12),
        "distance_f16": _curve_axis_distance_at(curve, cand.movement_type, 16),
        "total_distance": total_distance if high else None,
        "peak_distance": total_distance if high else None,
        "first_movement_frame": _first_frame_at_distance(curve, cand.movement_type, 0.01),
        "first_frame_8_units": _first_frame_at_distance(curve, cand.movement_type, 8.0),
        "first_frame_16_units": _first_frame_at_distance(curve, cand.movement_type, 16.0),
        "first_frame_30_units": _first_frame_at_distance(curve, cand.movement_type, 30.0),
        "earliest_guard_or_neutral_frame": guard_frame,
        "earliest_attack_cancel_frame": attack_frame,
        "recovery_trust_status": recovery_status,
        "recovery_trust_confidence": recovery_confidence,
        "plain_movement": plain_movement,
        "has_attack_cell": cand.slot.has_attack_cell,
        "root_decode_confidence": curve.confidence,
        "selection_reason": cand.reason,
        "reason": "; ".join(p for p in reason_parts if p),
    }


def _assign_movement_scores(rows: list[dict[str, Any]]) -> None:
    def pct(value: float, values: list[float], *, higher_is_better: bool = True) -> float:
        ordered = sorted(values)
        if len(ordered) == 1:
            return 1.0
        matches = [i for i, candidate in enumerate(ordered) if candidate == value]
        pos = sum(matches) / len(matches) if matches else ordered.index(value)
        percentile = pos / (len(ordered) - 1)
        return percentile if higher_is_better else 1.0 - percentile

    movement_types = sorted({row["movement_type"] for row in rows})
    for movement_type in movement_types:
        rankable = [
            row for row in rows
            if row["movement_type"] == movement_type
            and row["quality_status"] == "rankable_static_distance"
            and row["total_distance"] is not None
            and row["distance_f4"] is not None
            and row["distance_f8"] is not None
            and row["distance_f12"] is not None
        ]
        if not rankable:
            continue
        f4 = [float(row["distance_f4"] or 0.0) for row in rankable]
        f8 = [float(row["distance_f8"] or 0.0) for row in rankable]
        f12 = [float(row["distance_f12"] or 0.0) for row in rankable]
        totals = [float(row["total_distance"] or 0.0) for row in rankable]
        guard_values = [
            float(row["earliest_guard_or_neutral_frame"])
            for row in rankable
            if row["earliest_guard_or_neutral_frame"] is not None
        ]
        for row in rankable:
            early = (
                pct(float(row["distance_f4"] or 0.0), f4)
                + pct(float(row["distance_f8"] or 0.0), f8)
                + pct(float(row["distance_f12"] or 0.0), f12)
            ) / 3.0
            total = pct(float(row["total_distance"] or 0.0), totals)
            recovery = (
                pct(float(row["earliest_guard_or_neutral_frame"]), guard_values, higher_is_better=False)
                if row["earliest_guard_or_neutral_frame"] is not None and guard_values
                else 0.4
            )
            startup = (
                pct(float(row["first_movement_frame"]), [float(r["first_movement_frame"]) for r in rankable if r["first_movement_frame"] is not None], higher_is_better=False)
                if row["first_movement_frame"] is not None
                else 0.0
            )
            row["quality_score"] = round(early * 40 + total * 25 + recovery * 20 + startup * 10 + 5, 3)

        ranked = sorted(rankable, key=lambda row: float(row["quality_score"] or 0.0), reverse=True)
        n = len(ranked)
        for i, row in enumerate(ranked):
            q = (i + 1) / n
            if q <= 0.10:
                row["quality_grade"] = "S"
            elif q <= 0.30:
                row["quality_grade"] = "A"
            elif q <= 0.70:
                row["quality_grade"] = "B"
            elif q <= 0.90:
                row["quality_grade"] = "C"
            else:
                row["quality_grade"] = "D"


def _build_backstep_routes(
    cid: str,
    character: str,
    direction_candidates: list[MovementCandidate],
    bank: KhdFile,
    graph: SlotGraph,
    mot: MotionBankFile | None,
    bank_ctx: BankResolutionContext,
    selected_routes: list[SelectedRouteTrust] | None = None,
) -> list[BackstepRoute]:
    neutral_sources = _neutral_like_sources(bank, graph)
    routes: list[BackstepRoute] = []
    if selected_routes is None:
        stance_sources = {r.slot_idx for r in identify_stance_roots(bank, graph)}
        selected_routes = _select_basic_movement_routes(
            cid,
            character,
            direction_candidates,
            bank,
            graph,
            mot,
            "backstep_candidate",
            neutral_sources,
            stance_sources,
            bank_ctx,
        )
    for selected in selected_routes:
        c = selected.candidate
        evidence = selected.evidence
        routes.append(
            BackstepRoute(
                cid=cid,
                character=character,
                route_kind=evidence.trust_status,
                src_slot=-1 if c.src_slot is None else c.src_slot,
                dst_slot=c.dst_slot,
                animation_index=c.slot.animation_index,
                predicate_text=c.predicate_text,
                has_attack_cell=c.slot.has_attack_cell,
                source_rank=0 if evidence.source_is_neutral else 1,
                selection_score=evidence.trust_score,
                selection_reason=evidence.trust_reason,
                candidate=c,
                curve=selected.curve,
                recovery_trust=selected.recovery_trust,
            )
        )
    # Preserve the exact order from _select_basic_movement_routes. Backstep
    # quality, movement quality, and basic route audit must all consume the
    # same selected route.
    return routes[:5]


def _quality_from_route(route: BackstepRoute, bank: KhdFile, graph: SlotGraph, neutral_sources: set[int]) -> BackstepQuality:
    curve = route.curve
    plain_movement = (
        route.route_kind in RANKABLE_TRUST_STATUSES
        and route.predicate_text == "(any:back)"
        and curve.confidence in {"high", "experimental"}
    )
    rec_status, guard_frame, attack_frame = _estimate_recovery(route, bank, graph, neutral_sources)
    high = route.route_kind in RANKABLE_TRUST_STATUSES and curve.confidence == "high" and plain_movement
    reason_parts = [route.selection_reason, curve.reason]
    if not high:
        reason_parts.append("not ranked because route trust or root motion confidence is insufficient")
    return BackstepQuality(
        cid=route.cid,
        character=route.character,
        route_kind=route.route_kind,
        quality_status="rankable_static_distance" if high else "unranked_static_distance_unavailable",
        quality_score=None,
        quality_grade="Unranked",
        total_back_distance=curve.max_backward if high else None,
        back_distance_f4=_curve_back_distance_at(curve, 4),
        back_distance_f8=_curve_back_distance_at(curve, 8),
        back_distance_f12=_curve_back_distance_at(curve, 12),
        back_distance_f16=_curve_back_distance_at(curve, 16),
        first_frame_8_units=curve.first_backward_8 if high else None,
        first_frame_16_units=curve.first_backward_16 if high else None,
        first_frame_30_units=curve.first_backward_30 if high else None,
        recovery_estimate_status=rec_status,
        earliest_guard_or_neutral_frame=guard_frame,
        earliest_attack_cancel_frame=attack_frame,
        plain_movement=plain_movement,
        has_attack_cell=route.has_attack_cell,
        root_decode_confidence=curve.confidence,
        src_slot=route.src_slot,
        dst_slot=route.dst_slot,
        animation_hex=f"{route.animation_index:04X}",
        selection_reason=route.selection_reason,
        reason="; ".join(p for p in reason_parts if p),
    )


def _assign_backstep_scores(rows: list[BackstepQuality]) -> None:
    rankable = [
        r for r in rows
        if r.quality_status == "rankable_static_distance"
        and r.total_back_distance is not None
        and r.back_distance_f4 is not None
        and r.back_distance_f8 is not None
        and r.back_distance_f12 is not None
    ]
    if not rankable:
        return

    def pct(value: float, values: list[float], *, higher_is_better: bool = True) -> float:
        ordered = sorted(values)
        if len(ordered) == 1:
            return 1.0
        # Average duplicate ranks so tied values receive the same percentile.
        matches = [i for i, candidate in enumerate(ordered) if candidate == value]
        pos = sum(matches) / len(matches) if matches else ordered.index(value)
        percentile = pos / (len(ordered) - 1)
        return percentile if higher_is_better else 1.0 - percentile

    f4 = [r.back_distance_f4 or 0.0 for r in rankable]
    f8 = [r.back_distance_f8 or 0.0 for r in rankable]
    f12 = [r.back_distance_f12 or 0.0 for r in rankable]
    totals = [r.total_back_distance or 0.0 for r in rankable]
    guard_values = [r.earliest_guard_or_neutral_frame for r in rankable if r.earliest_guard_or_neutral_frame is not None]
    first_8_values = [r.first_frame_8_units for r in rankable if r.first_frame_8_units is not None]
    first_16_values = [r.first_frame_16_units for r in rankable if r.first_frame_16_units is not None]
    first_30_values = [r.first_frame_30_units for r in rankable if r.first_frame_30_units is not None]

    for r in rankable:
        early = (
            pct(r.back_distance_f4 or 0.0, f4)
            + pct(r.back_distance_f8 or 0.0, f8)
            + pct(r.back_distance_f12 or 0.0, f12)
        ) / 3.0
        total = pct(r.total_back_distance or 0.0, totals)
        if r.earliest_guard_or_neutral_frame is None or not guard_values:
            recovery = 0.4
        else:
            recovery = pct(
                float(r.earliest_guard_or_neutral_frame),
                [float(v) for v in guard_values],
                higher_is_better=False,
            )
        bucket = 0.0
        bucket += (
            pct(float(r.first_frame_8_units), [float(v) for v in first_8_values], higher_is_better=False) / 3
            if r.first_frame_8_units is not None and first_8_values
            else 0.0
        )
        bucket += (
            pct(float(r.first_frame_16_units), [float(v) for v in first_16_values], higher_is_better=False) / 3
            if r.first_frame_16_units is not None and first_16_values
            else 0.0
        )
        bucket += (
            pct(float(r.first_frame_30_units), [float(v) for v in first_30_values], higher_is_better=False) / 3
            if r.first_frame_30_units is not None and first_30_values
            else 0.0
        )
        consistency = 1.0 if r.plain_movement else 0.0
        r.quality_score = round(early * 35 + total * 20 + recovery * 20 + bucket * 15 + consistency * 10, 3)

    ranked = sorted(rankable, key=lambda r: r.quality_score or 0.0, reverse=True)
    n = len(ranked)
    for i, r in enumerate(ranked):
        q = (i + 1) / n
        if q <= 0.10:
            r.quality_grade = "S"
        elif q <= 0.30:
            r.quality_grade = "A"
        elif q <= 0.70:
            r.quality_grade = "B"
        elif q <= 0.90:
            r.quality_grade = "C"
        else:
            r.quality_grade = "D"


def _quality_row(q: BackstepQuality) -> dict[str, Any]:
    return asdict(q)


def _route_row(route: BackstepRoute) -> dict[str, Any]:
    return {
        "cid": route.cid,
        "character": route.character,
        "route_kind": route.route_kind,
        "src_slot": route.src_slot,
        "dst_slot": route.dst_slot,
        "animation_hex": f"{route.animation_index:04X}",
        "predicate_text": route.predicate_text,
        "has_attack_cell": route.has_attack_cell,
        "source_rank": route.source_rank,
        "selection_score": route.selection_score,
        "selection_reason": route.selection_reason,
        "root_decode_confidence": route.curve.confidence,
        "root_decode_status": route.curve.status,
        "root_decode_reason": route.curve.reason,
    }


def _all_motion_hashes(
    char_mots: dict[str, MotionBankFile],
) -> dict[str, list[str]]:
    groups: dict[str, list[str]] = {}
    for cid, mot in char_mots.items():
        if cid == "0ff":
            continue
        for idx in range(mot.count):
            size = mot.sizes[idx]
            if size <= 0:
                continue
            off = mot.offsets[idx]
            raw = mot.raw[off : off + size]
            key = f"{size}:{_file_sha1(raw)}"
            groups.setdefault(key, []).append(f"{cid}:{idx:04X}")
    return groups


def _apply_shared_groups(candidates: Iterable[MovementCandidate], groups: dict[str, list[str]]) -> None:
    for c in candidates:
        mot = c.slot.mot
        if not mot.shared_hash_key:
            continue
        members = groups.get(mot.shared_hash_key, [])
        chars = sorted({m.split(":")[0] for m in members})
        mot.shared_character_count = len(chars)
        mot.shared_characters = chars[:20]
        c.distance_tier = _distance_tier(mot)


def _write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows and fieldnames is None:
        path.write_text("", encoding="utf-8")
        return
    if fieldnames is None:
        fieldnames = list(rows[0])
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def _write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, sort_keys=True), encoding="utf-8")


def _load_inputs(dump_battle: Path, full_dump_battle: Path, copy_missing: bool) -> tuple[dict[str, bytes], dict[str, bytes]]:
    khd_dir = dump_battle / "hdr"
    mot_dir = dump_battle / "mot"
    if copy_missing and full_dump_battle.exists():
        # Stay conservative: local KHD files define the analysis scope.
        # Copy only the matching MOT files needed to analyze those banks,
        # plus the common motion bank used for fallback evidence.
        local_cids = {
            _cid_from_path(path, "hdr", ".khd")
            for path in khd_dir.glob("hdr*.khd")
        }
        for cid in sorted(local_cids | {"0ff"}):
            _copy_missing(
                mot_dir / f"chr{cid}.mot",
                full_dump_battle / "mot" / f"chr{cid}.mot",
            )

    khds = {
        _cid_from_path(path, "hdr", ".khd"): path.read_bytes()
        for path in sorted(khd_dir.glob("hdr*.khd"))
    }
    mots = {
        _cid_from_path(path, "chr", ".mot"): path.read_bytes()
        for path in sorted(mot_dir.glob("chr*.mot"))
    }
    return khds, mots


def result_counts_by_type(rows: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        movement_type = str(row.get("movement_type", "unknown"))
        counts[movement_type] = counts.get(movement_type, 0) + 1
    return dict(sorted(counts.items()))


def result_counts_by_reason(rows: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        if row.get("quality_score") is not None:
            continue
        reason = str(row.get("root_decode_confidence") or row.get("quality_status") or "unknown")
        counts[reason] = counts.get(reason, 0) + 1
    return dict(sorted(counts.items()))


def result_counts_by_confidence(rows: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        confidence = str(row.get("root_decode_confidence", "unknown"))
        counts[confidence] = counts.get(confidence, 0) + 1
    return dict(sorted(counts.items()))


def analyze(dump_battle: Path, full_dump_battle: Path, copy_missing: bool) -> dict[str, Any]:
    khd_raw, mot_raw = _load_inputs(dump_battle, full_dump_battle, copy_missing)
    parsed_khds = {cid: parse_khd(raw) for cid, raw in khd_raw.items()}
    parsed_mots = {cid: parse_mot(raw) for cid, raw in mot_raw.items()}
    common_mot = parsed_mots.get("0ff") or parsed_mots.get("000")
    motion_groups = _all_motion_hashes(parsed_mots)
    khd_paths_by_cid = {
        cid: dump_battle / "hdr" / f"hdr{cid}.khd"
        for cid in khd_raw
    }
    mot_paths_by_cid = {
        cid: dump_battle / "mot" / f"chr{cid}.mot"
        for cid in parsed_mots
    }
    bank_ctx = BankResolutionContext(
        khd_by_cid=parsed_khds,
        mot_by_cid=parsed_mots,
        character_names=CHARA_NAMES,
        khd_paths_by_cid=khd_paths_by_cid,
        mot_paths_by_cid=mot_paths_by_cid,
        confirmed_bank_map={},
    )

    all_candidates: list[MovementCandidate] = []
    all_edges: list[TransitionRow] = []
    all_backstep_routes: list[BackstepRoute] = []
    all_backstep_quality: list[BackstepQuality] = []
    all_backstep_curve_rows: list[dict[str, Any]] = []
    all_movement_quality: list[dict[str, Any]] = []
    all_movement_curve_rows: list[dict[str, Any]] = []
    all_movement_decode_failures: list[dict[str, Any]] = []
    all_movement_route_audit: list[dict[str, Any]] = []
    all_basic_movement_routes: list[dict[str, Any]] = []
    all_route_trust_evidence: list[dict[str, Any]] = []
    all_cell_semantics_rows: list[dict[str, Any]] = []
    all_unresolved_basic_routes: list[dict[str, Any]] = []
    all_cross_bank_direction_edges: list[dict[str, Any]] = []
    all_cross_bank_route_resolutions: list[dict[str, Any]] = []
    all_recovery_trust_audit: list[dict[str, Any]] = []
    char_summaries: list[dict[str, Any]] = []
    errors: list[dict[str, str]] = []

    for cid, raw in sorted(khd_raw.items()):
        character = CHARA_NAMES.get(cid, f"chara_{cid}")
        try:
            bank = parsed_khds[cid]
            graph = build_slot_graph(bank, raw)
            neutral_sources = _neutral_like_sources(bank, graph)
            mot = parsed_mots.get(cid)
            if mot is None:
                errors.append({"cid": cid, "error": "missing matching chr*.mot"})

            sections = bank.sections
            non_attack_count = len(sections[1].throw_cells) if len(sections) > 1 else 0
            attack_count = len(sections[0].entries) if sections else 0
            mot_nonempty = sum(1 for s in mot.sizes if s > 0) if mot else 0
            mot_empty = sum(1 for s in mot.sizes if s == 0) if mot else 0
            core_samples: list[MovementCandidate] = []
            for slot_idx in CORE_MOVEMENT_SLOTS:
                if slot_idx < len(bank.slots):
                    core_samples.append(
                        _candidate_from_slot(
                            cid,
                            character,
                            f"early_core_slot_{slot_idx}",
                            "early_core_slot_sample",
                            bank.slots[slot_idx],
                            mot,
                            common_mot,
                        )
                    )

            direction_candidates: list[MovementCandidate] = []
            for edge in _direction_edges_from_graph(graph):
                movement_type = _movement_type_from_predicate(edge.predicate_text)
                if edge.dst_bank != 0:
                    resolved = resolve_route_target(
                        source_cid=cid,
                        source_character=character,
                        movement_type=movement_type,
                        src_slot=edge.src_slot,
                        dst_bank=edge.dst_bank,
                        dst_slot=edge.dst_slot,
                        raw_move_id=edge.raw_move_id,
                        ctx=bank_ctx,
                    )
                    all_cross_bank_direction_edges.append(
                        {
                            "cid": cid,
                            "character": character,
                            "movement_type": movement_type,
                            "src_slot": edge.src_slot,
                            "dst_bank": edge.dst_bank,
                            "dst_slot": edge.dst_slot,
                            "raw_move_id": edge.raw_move_id,
                            "predicate_text": edge.predicate_text,
                            "predicate_kind": edge.predicate_kind,
                            "source_pc": edge.source_pc,
                            "reason": "cross-bank direction target is audited but not selected until target-bank resolution is implemented",
                        }
                    )
                    all_cross_bank_route_resolutions.append(_resolution_row(resolved, edge.raw_move_id))
                if edge.dst_slot >= len(bank.slots):
                    continue
                dst = bank.slots[edge.dst_slot]
                cand = _candidate_from_slot(
                    cid,
                    character,
                    movement_type,
                    "direction_transition",
                    dst,
                    mot,
                    common_mot,
                    edge,
                )
                direction_candidates.append(cand)

            _apply_shared_groups(core_samples, motion_groups)
            _apply_shared_groups(direction_candidates, motion_groups)
            all_candidates.extend(core_samples)
            all_candidates.extend(direction_candidates)

            movement_selection_types = {
                "backstep_candidate",
                "sidestep_up_candidate",
                "sidestep_down_candidate",
                "forward_step_candidate",
                "back_diagonal",
                "forward_diagonal",
                "eight_way_or_ambiguous",
            }
            stance_sources = {r.slot_idx for r in identify_stance_roots(bank, graph)}
            selected_by_type: dict[str, list[SelectedRouteTrust]] = {}
            for movement_type in sorted(movement_selection_types):
                selected_routes = _select_basic_movement_routes(
                    cid,
                    character,
                    direction_candidates,
                    bank,
                    graph,
                    mot,
                    movement_type,
                    neutral_sources,
                    stance_sources,
                    bank_ctx,
                )
                selected_by_type[movement_type] = selected_routes
                selected = selected_routes[0] if selected_routes else None
                basic_row = _basic_route_row(selected, cid, character, movement_type)
                all_basic_movement_routes.append(basic_row)
                if basic_row["trust_status"] == "unresolved":
                    all_unresolved_basic_routes.append(basic_row)
                for selected_route in selected_routes:
                    flat = evidence_to_flat_row(selected_route.evidence)
                    all_route_trust_evidence.append(flat)
                    all_cell_semantics_rows.extend(cell_semantics_rows(selected_route.evidence))
                    all_recovery_trust_audit.extend(
                        _recovery_rows(
                            cid=cid,
                            character=character,
                            movement_type=movement_type,
                            src_slot=selected_route.evidence.src_slot,
                            dst_slot=selected_route.evidence.dst_slot,
                            trust=selected_route.recovery_trust,
                        )
                    )

            backstep_routes = _build_backstep_routes(
                cid,
                character,
                direction_candidates,
                bank,
                graph,
                mot,
                bank_ctx,
                selected_by_type.get("backstep_candidate"),
            )
            all_backstep_routes.extend(backstep_routes)
            if backstep_routes:
                quality = _quality_from_route(backstep_routes[0], bank, graph, neutral_sources)
            else:
                dummy_slot = bank.slots[0] if bank.slots else FLuxMoveBankSlotView(slot_index=-1, bank_offset=0)
                dummy_candidate = _candidate_from_slot(
                    cid,
                    character,
                    "backstep_candidate",
                    "missing_backstep_route",
                    dummy_slot,
                    mot,
                    common_mot,
                )
                dummy_route = BackstepRoute(
                    cid=cid,
                    character=character,
                    route_kind="unresolved",
                    src_slot=-1,
                    dst_slot=-1,
                    animation_index=0xFFFF,
                    predicate_text="",
                    has_attack_cell=False,
                    source_rank=99,
                    selection_score=0,
                    selection_reason="no static backstep route found",
                    candidate=dummy_candidate,
                    curve=decode_root_motion_curve(b""),
                )
                quality = _quality_from_route(dummy_route, bank, graph, neutral_sources)
            all_backstep_quality.append(quality)
            for frame in backstep_routes[0].curve.frames if backstep_routes else []:
                all_backstep_curve_rows.append(
                    {
                        "cid": cid,
                        "character": character,
                        "route_kind": backstep_routes[0].route_kind,
                        "frame": frame.frame,
                        "x": frame.x,
                        "y": frame.y,
                        "z": frame.z,
                        "cumulative_x": frame.cumulative_x,
                        "cumulative_y": frame.cumulative_y,
                        "cumulative_z": frame.cumulative_z,
                        "back_distance": abs(frame.cumulative_z),
                        "source_channel": frame.source_channel,
                        "root_decode_confidence": backstep_routes[0].curve.confidence,
                    }
                )

            for movement_type in sorted(movement_selection_types):
                selected_routes = selected_by_type.get(movement_type, [])
                if not selected_routes:
                    no_route_reason = "no direction-gated route found for this movement type"
                    all_movement_quality.append(
                        {
                            "cid": cid,
                            "character": character,
                            "movement_type": movement_type,
                            "route_kind": "unresolved",
                            "quality_status": "unknown_no_static_route",
                            "quality_score": None,
                            "quality_grade": "Unranked",
                            "distance_f4": None,
                            "distance_f8": None,
                            "distance_f12": None,
                            "distance_f16": None,
                            "total_distance": None,
                            "peak_distance": None,
                            "first_movement_frame": None,
                            "first_frame_8_units": None,
                            "first_frame_16_units": None,
                            "first_frame_30_units": None,
                            "earliest_guard_or_neutral_frame": None,
                            "earliest_attack_cancel_frame": None,
                            "recovery_trust_status": "unknown_from_static_bytecode",
                            "recovery_trust_confidence": "unknown",
                            "plain_movement": False,
                            "has_attack_cell": False,
                            "root_decode_confidence": "unknown",
                            "selection_reason": no_route_reason,
                            "reason": no_route_reason,
                        }
                    )
                    all_movement_route_audit.append(
                        {
                            "cid": cid,
                            "character": character,
                            "movement_type": movement_type,
                            "src_slot": None,
                            "dst_slot": None,
                            "animation_hex": "",
                            "predicate_text": "",
                            "has_attack_cell": False,
                            "route_kind": "unresolved",
                            "root_decode_confidence": "unknown",
                            "recovery_trust_status": "unknown_no_static_route",
                            "recovery_trust_confidence": "unknown",
                            "selection_score": 0,
                            "selection_reason": no_route_reason,
                        }
                    )
                    continue

                selected_route = selected_routes[0]
                cand = selected_route.candidate
                curve = selected_route.curve
                all_movement_quality.append(
                    _movement_quality_row(
                        cand,
                        curve,
                        bank,
                        graph,
                        neutral_sources,
                        selected_route.evidence,
                    )
                )
                all_movement_route_audit.append(
                    {
                        "cid": cid,
                        "character": character,
                        "movement_type": movement_type,
                        "src_slot": cand.src_slot if cand.src_slot is not None else -1,
                        "dst_slot": cand.dst_slot,
                        "animation_hex": cand.slot.animation_hex,
                        "predicate_text": cand.predicate_text,
                        "has_attack_cell": cand.slot.has_attack_cell,
                        "route_kind": selected_route.evidence.trust_status,
                        "root_decode_confidence": curve.confidence,
                        "recovery_trust_status": selected_route.evidence.recovery_trust_status,
                        "recovery_trust_confidence": selected_route.evidence.recovery_trust_confidence,
                        "selection_score": selected_route.evidence.trust_score,
                        "selection_reason": selected_route.evidence.trust_reason,
                    }
                )
                if curve.confidence != "high":
                    all_movement_decode_failures.append(
                        {
                            "cid": cid,
                            "character": character,
                            "animation_hex": cand.slot.animation_hex,
                            "movement_type": movement_type,
                            "failure_stage": curve.reason.split(":", 1)[0] if ":" in curve.reason else curve.status,
                            "reason": curve.reason,
                            "clip_offset": cand.slot.mot.offset,
                            "frame": "",
                        }
                    )
                for frame in curve.frames:
                    primary = _curve_axis_distance_at(curve, movement_type, frame.frame)
                    secondary = (
                        abs(frame.cumulative_z)
                        if movement_type in {"sidestep_up_candidate", "sidestep_down_candidate"}
                        else abs(frame.cumulative_x)
                    )
                    all_movement_curve_rows.append(
                        {
                            "cid": cid,
                            "character": character,
                            "movement_type": movement_type,
                            "route_kind": all_movement_quality[-1]["route_kind"],
                            "frame": frame.frame,
                            "local_x": frame.x,
                            "local_y": frame.y,
                            "local_z": frame.z,
                            "delta_x": frame.x - (curve.frames[frame.frame - 1].x if frame.frame > 0 else 0.0),
                            "delta_y": frame.y - (curve.frames[frame.frame - 1].y if frame.frame > 0 else 0.0),
                            "delta_z": frame.z - (curve.frames[frame.frame - 1].z if frame.frame > 0 else 0.0),
                            "cumulative_x": frame.cumulative_x,
                            "cumulative_y": frame.cumulative_y,
                            "cumulative_z": frame.cumulative_z,
                            "primary_distance": primary,
                            "secondary_distance": secondary,
                            "source_channel": frame.source_channel,
                            "root_decode_confidence": curve.confidence,
                        }
                    )

            for cand in direction_candidates:
                all_edges.append(
                    TransitionRow(
                        cid=cid,
                        character=character,
                        src_slot=cand.src_slot if cand.src_slot is not None else -1,
                        dst_bank=cand.dst_bank,
                        dst_slot=cand.dst_slot,
                        movement_type=cand.movement_type,
                        predicate_kind=cand.predicate_kind,
                        predicate_text=cand.predicate_text,
                        predicate_args=" ".join(
                            "" if v is None else f"0x{v:04X}" for v in cand.predicate_args
                        ),
                        raw_move_id=f"0x{((cand.dst_bank << 12) | cand.dst_slot):04X}",
                        source_pc="" if cand.source_pc is None else f"0x{cand.source_pc:X}",
                        callcond_idx="" if cand.callcond_idx is None else f"0x{cand.callcond_idx:02X}",
                        dst_animation_hex=cand.slot.animation_hex,
                        dst_animation_length=cand.slot.animation_length,
                        dst_has_attack_cell=cand.slot.has_attack_cell,
                        dst_mot_status=cand.slot.mot.status,
                        dst_mot_size=cand.slot.mot.size,
                        dst_mot_sha1=cand.slot.mot.sha1,
                        distance_tier=cand.distance_tier,
                        confidence=cand.confidence,
                        score=cand.score,
                        reason=cand.reason,
                    )
                )

            best_by_type: dict[str, list[dict[str, Any]]] = {}
            for movement_type in sorted({c.movement_type for c in direction_candidates}):
                ranked = sorted(
                    [c for c in direction_candidates if c.movement_type == movement_type],
                    key=lambda c: (-c.score, c.slot.has_attack_cell, c.dst_slot),
                )
                best_by_type[movement_type] = [_flatten_candidate(c) for c in ranked[:5]]

            char_summaries.append(
                {
                    "cid": cid,
                    "character": character,
                    "khd_bytes": len(raw),
                    "slot_count": len(bank.slots),
                    "attack_cell_count": attack_count,
                    "non_attack_descriptor_count": non_attack_count,
                    "mot_count": mot.count if mot else 0,
                    "mot_nonempty": mot_nonempty,
                    "mot_empty": mot_empty,
                    "core_samples": [_flatten_candidate(c) for c in core_samples],
                    "best_direction_candidates": best_by_type,
                    "backstep_routes": [_route_row(r) for r in backstep_routes],
                }
            )
        except Exception as exc:
            errors.append({"cid": cid, "error": f"{type(exc).__name__}: {exc}"})

    mot_only = []
    for cid, mot in sorted(parsed_mots.items()):
        if cid in khd_raw:
            continue
        mot_only.append(
            {
                "cid": cid,
                "character": CHARA_NAMES.get(cid, f"chara_{cid}"),
                "mot_count": mot.count,
                "mot_nonempty": sum(1 for s in mot.sizes if s > 0),
                "mot_empty": sum(1 for s in mot.sizes if s == 0),
                "note": "MOT exists without a matching local KHD; useful as common/fallback evidence only.",
            }
        )

    flat_candidates = [_flatten_candidate(c) for c in sorted(
        all_candidates,
        key=lambda c: (c.cid, c.source_kind, c.movement_type, -c.score, c.dst_slot),
    )]

    movement_types: dict[str, int] = {}
    distance_tiers: dict[str, int] = {}
    confidence_counts: dict[str, int] = {}
    for c in all_candidates:
        movement_types[c.movement_type] = movement_types.get(c.movement_type, 0) + 1
        distance_tiers[c.distance_tier] = distance_tiers.get(c.distance_tier, 0) + 1
        confidence_counts[c.confidence] = confidence_counts.get(c.confidence, 0) + 1

    _assign_backstep_scores(all_backstep_quality)
    _assign_movement_scores(all_movement_quality)
    backstep_quality_rows = [_quality_row(q) for q in all_backstep_quality]
    ranked_backsteps = [q for q in all_backstep_quality if q.quality_score is not None]
    ranked_movements = [row for row in all_movement_quality if row.get("quality_score") is not None]
    cross_bank_resolved_count = sum(
        1 for row in all_cross_bank_route_resolutions
        if str(row["resolution_status"]).startswith("resolved")
    )
    recovery_confirmed_count = sum(
        1 for row in all_basic_movement_routes
        if row.get("recovery_trust_status") in {
            "confirmed_static_recovery",
            "confirmed_static_stance_recovery",
        }
    )
    late_followup_unranked_count = sum(
        1 for row in all_movement_quality
        if row.get("route_kind") == "trusted_basic_with_late_followup"
        and row.get("quality_score") is None
    )
    root_high = sum(1 for q in all_backstep_quality if q.root_decode_confidence == "high")
    root_failed = sum(1 for q in all_backstep_quality if q.root_decode_confidence == "failed")
    unranked_reasons: dict[str, int] = {}
    for q in all_backstep_quality:
        if q.quality_score is None:
            key = q.root_decode_confidence or q.quality_status
            unranked_reasons[key] = unranked_reasons.get(key, 0) + 1

    slot_counts = [c["slot_count"] for c in char_summaries]
    attack_counts = [c["attack_cell_count"] for c in char_summaries]
    non_attack_counts = [c["non_attack_descriptor_count"] for c in char_summaries]
    summary = {
        "inputs": {
            "dump_battle": str(dump_battle),
            "full_dump_battle": str(full_dump_battle),
            "copied_missing_from_full_dump": copy_missing,
            "khd_count": len(khd_raw),
            "mot_count": len(mot_raw),
        },
        "engine_model_confirmed_by_ghidra": {
            "authored_sources": [
                "KHD move-bank slot selects animation id and MoveVM bytecode",
                "MOT offset table holds raw HgMotion payload per animation id",
                "MoveVM offset path can write explicit movement velocity/offsets",
            ],
            "movement_pipeline": [
                "LuxMoveVM_ApplyMoveOffsetToChara writes movement offset/velocity fields",
                "LuxBattleChara_UpdateVelocityFromBoneMotion consumes bone/root motion",
                "LuxBattleChara_IntegratePhysics_PerTick integrates move, ground, hit-pushback, one-shot offsets, blend weight, and time dilation",
                "LuxBattle_TickCharaCollisionPhysics and step clearance can clip or redirect movement",
            ],
            "hit_hurt_links": [
                "LuxBattle_CheckYarareGate_StepRange checks step state and active attack range bucket",
                "LuxBattle_CheckYarareGate_BackStepRange checks backstep/attack range conditions",
                "Hit detection is therefore not just raw box overlap during movement",
            ],
        },
        "static_totals": {
            "character_count_with_khd": len(char_summaries),
            "slot_count_min": min(slot_counts) if slot_counts else 0,
            "slot_count_max": max(slot_counts) if slot_counts else 0,
            "attack_cell_count_min": min(attack_counts) if attack_counts else 0,
            "attack_cell_count_max": max(attack_counts) if attack_counts else 0,
            "non_attack_descriptor_count_min": min(non_attack_counts) if non_attack_counts else 0,
            "non_attack_descriptor_count_max": max(non_attack_counts) if non_attack_counts else 0,
            "candidate_count": len(all_candidates),
            "direction_transition_count": len(all_edges),
            "cross_bank_direction_edge_count": len(all_cross_bank_direction_edges),
            "cross_bank_resolved_count": cross_bank_resolved_count,
            "cross_bank_unresolved_count": len(all_cross_bank_route_resolutions) - cross_bank_resolved_count,
            "recovery_confirmed_count": recovery_confirmed_count,
            "recovery_unknown_count": len(all_basic_movement_routes) - recovery_confirmed_count,
            "late_followup_unranked_count": late_followup_unranked_count,
            "unique_nonempty_motion_hashes": len(motion_groups),
        },
        "movement_type_counts": dict(sorted(movement_types.items())),
        "distance_tier_counts": dict(sorted(distance_tiers.items())),
        "confidence_counts": dict(sorted(confidence_counts.items())),
        "motion_hash_groups_reused_by_multiple_characters": {
            k: v for k, v in sorted(motion_groups.items()) if len({m.split(":")[0] for m in v}) > 1
        },
        "backstep_quality_summary": {
            "ranked_count": len(ranked_backsteps),
            "unranked_count": len(all_backstep_quality) - len(ranked_backsteps),
            "root_decode_high_confidence_count": root_high,
            "root_decode_failed_count": root_failed,
            "best_backstep_by_score": [
                _quality_row(q)
                for q in sorted(ranked_backsteps, key=lambda row: row.quality_score or 0.0, reverse=True)[:10]
            ],
            "fastest_early_retreat_f8": [
                _quality_row(q)
                for q in sorted(
                    [q for q in ranked_backsteps if q.back_distance_f8 is not None],
                    key=lambda row: row.back_distance_f8 or 0.0,
                    reverse=True,
                )[:10]
            ],
            "longest_total_retreat": [
                _quality_row(q)
                for q in sorted(
                    [q for q in ranked_backsteps if q.total_back_distance is not None],
                    key=lambda row: row.total_back_distance or 0.0,
                    reverse=True,
                )[:10]
            ],
            "earliest_16_unit_escape": [
                _quality_row(q)
                for q in sorted(
                    [q for q in ranked_backsteps if q.first_frame_16_units is not None],
                    key=lambda row: row.first_frame_16_units or 999999,
                )[:10]
            ],
        },
        "movement_quality_summary": {
            "ranked_count": len(ranked_movements),
            "unranked_count": len(all_movement_quality) - len(ranked_movements),
            "trusted_basic_route_count": sum(
                1 for row in all_basic_movement_routes if row["trust_status"] in TRUSTED_STATUSES
            ),
            "unresolved_basic_route_count": len(all_unresolved_basic_routes),
            "trust_status_counts": {
                status: sum(1 for row in all_basic_movement_routes if row["trust_status"] == status)
                for status in sorted({row["trust_status"] for row in all_basic_movement_routes})
            },
            "ranked_counts_by_movement_type": {
                movement_type: sum(
                    1 for row in ranked_movements if row["movement_type"] == movement_type
                )
                for movement_type in sorted({row["movement_type"] for row in all_movement_quality})
            },
            "decode_confidence_counts": {
                confidence: sum(
                    1 for row in all_movement_quality if row["root_decode_confidence"] == confidence
                )
                for confidence in sorted({row["root_decode_confidence"] for row in all_movement_quality})
            },
            "best_by_movement_type": {
                movement_type: sorted(
                    [
                        row for row in ranked_movements
                        if row["movement_type"] == movement_type
                    ],
                    key=lambda row: row["quality_score"] or 0.0,
                    reverse=True,
                )[:10]
                for movement_type in sorted({row["movement_type"] for row in all_movement_quality})
            },
        },
        "characters": char_summaries,
        "mot_only": mot_only,
        "errors": errors,
    }
    unknowns = {
        "known_exact_authored_root_motion_when_high_confidence": [
            "Backstep/sidestep/forward-step authored root distance for selected routes with root_decode_confidence=high",
            "Per-frame root-translation curve for selected routes with root_decode_confidence=high",
            "First frame of meaningful authored displacement for selected routes with root_decode_confidence=high",
        ],
        "unknown_without_additional_static_modeling": [
            "Exact recovery/cancel frame unless command/movelist metadata proves it",
            "Whether each empty local MOT section resolves through common chr0ff or an asset-loader remap",
            "Cross-bank direction routes until target-bank resolution is implemented",
            "Hurtbox pose over time during movement",
            "Runtime-adjusted position after walls, ring edge, terrain, and opponent body collision",
        ],
        "known_static_but_not_distance": [
            "Per-character KHD slot tables",
            "Decoded direction-gated MoveVM transition candidates",
            "MOT section sizes and hashes for candidate animation ids",
            "Whether a candidate slot carries attack cells",
            "Whether candidate MOT payloads are shared or unique by raw hash",
            "Canonical backstep route selection and unranked reason per character",
            "Motion clip headers from the engine-correct +0x08 MOT layout",
        ],
        "do_not_claim": [
            "Do not rank movement distance from animation id alone",
            "Do not rank movement distance from MOT section size alone",
            "Do not treat one candidate transition as the complete movement option without checking stances and follow-up routing",
        ],
    }
    return {
        "summary": summary,
        "unknowns": unknowns,
        "flat_candidates": flat_candidates,
        "transition_edges": [asdict(row) for row in all_edges],
        "backstep_quality": {
            "metadata": {
                "static_only": True,
                "uses_runtime_instrumentation": False,
                "distance_source": "decoded MOT root motion when high confidence",
                "yarare_thresholds": {
                    "close": 16.0,
                    "backstep_medium": 30.0,
                },
            },
            "characters": backstep_quality_rows,
            "routes": [_route_row(r) for r in all_backstep_routes],
            "unranked_reasons": dict(sorted(unranked_reasons.items())),
            "ranked_count": len(ranked_backsteps),
        },
        "backstep_motion_curves": all_backstep_curve_rows,
        "movement_quality": {
            "metadata": {
                "static_only": True,
                "uses_runtime_instrumentation": False,
                "distance_source": "decoded MOT root motion",
                "decoder_reference_functions": [
                    "LuxMoveVM_InitMotionPlayback @ 0x140300400",
                    "LuxMotion_SampleKeyframeTransforms @ 0x1402E7780",
                    "LuxMotion_DecodeHuffmanKeyframeData @ 0x1402E71E0",
                    "LuxMotion_BitStreamReadBits @ 0x1402E6F90",
                    "LuxMotion_BuildHuffmanTable @ 0x1402E7050",
                    "LuxMotion_BlendKeyframeTransforms @ 0x1402E79C0",
                ],
            },
            "characters": all_movement_quality,
            "ranked_counts_by_movement_type": result_counts_by_type(ranked_movements),
            "unranked_reasons": result_counts_by_reason(all_movement_quality),
            "decode_confidence_counts": result_counts_by_confidence(all_movement_quality),
        },
        "movement_motion_curves": all_movement_curve_rows,
        "movement_decode_failures": all_movement_decode_failures,
        "movement_route_audit": all_movement_route_audit,
        "cross_bank_direction_edges": all_cross_bank_direction_edges,
        "cross_bank_route_resolution": all_cross_bank_route_resolutions,
        "basic_movement_routes": all_basic_movement_routes,
        "route_trust_evidence": all_route_trust_evidence,
        "cell_semantics_audit": all_cell_semantics_rows,
        "unresolved_basic_routes": all_unresolved_basic_routes,
        "recovery_trust_audit": all_recovery_trust_audit,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump-battle", type=Path, default=DEFAULT_DUMP_BATTLE)
    parser.add_argument("--full-dump-battle", type=Path, default=DEFAULT_FULL_DUMP_BATTLE)
    parser.add_argument("--copy-missing", action="store_true")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT_DIR)
    args = parser.parse_args()

    result = analyze(args.dump_battle, args.full_dump_battle, args.copy_missing)
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    _write_json(out / "movement_static_summary.json", result["summary"])
    _write_json(out / "movement_unknowns.json", result["unknowns"])
    _write_json(out / "movement_slot_candidates.json", result["flat_candidates"])
    _write_csv(out / "movement_slot_candidates.csv", result["flat_candidates"])
    _write_csv(out / "movement_transition_edges.csv", result["transition_edges"])
    _write_json(out / "backstep_quality_static.json", result["backstep_quality"])
    _write_csv(out / "backstep_quality_static.csv", result["backstep_quality"]["characters"])
    _write_json(out / "movement_quality_static.json", result["movement_quality"])
    _write_csv(out / "movement_quality_static.csv", result["movement_quality"]["characters"])
    _write_json(out / "movement_decode_failures.json", result["movement_decode_failures"])
    _write_csv(out / "movement_route_audit.csv", result["movement_route_audit"])
    _write_csv(out / "cross_bank_direction_edges.csv", result["cross_bank_direction_edges"])
    _write_csv(out / "cross_bank_route_resolution.csv", result["cross_bank_route_resolution"])
    _write_json(out / "basic_movement_routes.json", result["basic_movement_routes"])
    _write_csv(out / "basic_movement_routes.csv", result["basic_movement_routes"])
    _write_json(out / "route_trust_evidence.json", result["route_trust_evidence"])
    _write_csv(out / "route_trust_evidence.csv", result["route_trust_evidence"])
    _write_csv(out / "cell_semantics_audit.csv", result["cell_semantics_audit"])
    _write_csv(out / "unresolved_basic_routes.csv", result["unresolved_basic_routes"])
    _write_csv(out / "recovery_trust_audit.csv", result["recovery_trust_audit"])
    _write_csv(
        out / "movement_motion_curves.csv",
        result["movement_motion_curves"],
        fieldnames=[
            "cid",
            "character",
            "movement_type",
            "route_kind",
            "frame",
            "local_x",
            "local_y",
            "local_z",
            "delta_x",
            "delta_y",
            "delta_z",
            "cumulative_x",
            "cumulative_y",
            "cumulative_z",
            "primary_distance",
            "secondary_distance",
            "source_channel",
            "root_decode_confidence",
        ],
    )
    _write_csv(
        out / "backstep_motion_curves.csv",
        result["backstep_motion_curves"],
        fieldnames=[
            "cid",
            "character",
            "route_kind",
            "frame",
            "x",
            "y",
            "z",
            "cumulative_x",
            "cumulative_y",
            "cumulative_z",
            "back_distance",
            "source_channel",
            "root_decode_confidence",
        ],
    )

    totals = result["summary"]["static_totals"]
    print(
        "Static movement analysis complete: "
        f"{totals['character_count_with_khd']} KHD banks, "
        f"{totals['direction_transition_count']} direction transitions, "
        f"{totals['candidate_count']} candidates."
    )
    print(f"Wrote outputs to {out}")
    if result["summary"]["errors"]:
        print(f"WARN: {len(result['summary']['errors'])} input errors recorded in summary JSON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
