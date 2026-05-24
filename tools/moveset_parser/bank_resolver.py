"""Static resolution for packed MoveVM bank/slot route targets.

Ghidra reference: LuxMoveVM_ResolveBankSlot treats bits 15..12 as one
of four buckets inside the current FLuxMoveBank and bits 10..0 as the
slot within that bucket.  These are not external KHD files.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

from luxformats import KhdFile, MotionBankFile


@dataclass(frozen=True)
class BankTarget:
    source_cid: str
    dst_bank: int
    dst_slot: int
    target_cid: str | None
    target_bank_kind: str
    target_slot_exists: bool
    confidence: str
    reason: str


@dataclass(frozen=True)
class ResolvedRouteTarget:
    source_cid: str
    source_character: str
    movement_type: str
    src_slot: int
    dst_bank: int
    dst_slot: int
    target_cid: str | None
    target_character: str | None
    target_slot_index: int | None
    target_animation_index: int | None
    target_khd_path: str | None
    target_mot_path: str | None
    resolution_status: str
    confidence: str
    reason: str


@dataclass(frozen=True)
class BankResolutionContext:
    khd_by_cid: Mapping[str, KhdFile]
    mot_by_cid: Mapping[str, MotionBankFile]
    character_names: Mapping[str, str]
    khd_paths_by_cid: Mapping[str, Path]
    mot_paths_by_cid: Mapping[str, Path]
    confirmed_bank_map: Mapping[int, str]


def resolve_bank_target(
    *,
    source_cid: str,
    dst_bank: int,
    dst_slot: int,
    ctx: BankResolutionContext,
) -> BankTarget:
    if 0 <= dst_bank < 4:
        bank = ctx.khd_by_cid.get(source_cid)
        exists = bank is not None and 0 <= dst_slot < len(bank.slots)
        return BankTarget(
            source_cid=source_cid,
            dst_bank=dst_bank,
            dst_slot=dst_slot,
            target_cid=source_cid,
            target_bank_kind="local" if dst_bank == 0 else f"move_bucket_{dst_bank}",
            target_slot_exists=exists,
            confidence="confirmed_static_data",
            reason=(
                "bank 0 targets the first current-character FLuxMoveBank bucket"
                if dst_bank == 0
                else f"bank {dst_bank} targets current-character FLuxMoveBank bucket {dst_bank}"
            ),
        )

    target_cid = ctx.confirmed_bank_map.get(dst_bank)
    if target_cid is None:
        return BankTarget(
            source_cid=source_cid,
            dst_bank=dst_bank,
            dst_slot=dst_slot,
            target_cid=None,
            target_bank_kind="unknown",
            target_slot_exists=False,
            confidence="unresolved_static_code",
            reason=(
                f"bank {dst_bank} is not mapped by confirmed static code; "
                "route is audited but not selected"
            ),
        )

    bank = ctx.khd_by_cid.get(target_cid)
    exists = bank is not None and 0 <= dst_slot < len(bank.slots)
    kind = "common" if target_cid.lower() == "0ff" else "loaded"
    return BankTarget(
        source_cid=source_cid,
        dst_bank=dst_bank,
        dst_slot=dst_slot,
        target_cid=target_cid,
        target_bank_kind=kind,
        target_slot_exists=exists,
        confidence="confirmed_static_code",
        reason=f"bank {dst_bank} maps to {target_cid} by confirmed static code",
    )


def resolve_route_target(
    *,
    source_cid: str,
    source_character: str,
    movement_type: str,
    src_slot: int,
    dst_bank: int,
    dst_slot: int,
    raw_move_id: int,
    ctx: BankResolutionContext,
) -> ResolvedRouteTarget:
    target = resolve_bank_target(
        source_cid=source_cid,
        dst_bank=dst_bank,
        dst_slot=dst_slot,
        ctx=ctx,
    )
    target_cid = target.target_cid
    target_bank = ctx.khd_by_cid.get(target_cid or "")
    target_slot_index: int | None = None
    target_anim: int | None = None
    if target.target_slot_exists and target_bank is not None:
        target_slot_index = dst_slot
        target_anim = target_bank.slots[dst_slot].wAnimationIndex_00

    if dst_bank == 0:
        status = "resolved_local" if target.target_slot_exists else "unresolved_missing_target_slot"
    elif 0 < dst_bank < 4 and target_cid == source_cid:
        status = "resolved_move_bucket" if target.target_slot_exists else "unresolved_missing_target_slot"
    elif target_cid is None:
        status = "unresolved_unknown_bank"
    elif not target.target_slot_exists:
        status = "unresolved_missing_target_slot"
    elif target_cid not in ctx.mot_by_cid:
        status = "unresolved_missing_motion"
    elif target.target_bank_kind == "common":
        status = "resolved_common_bank"
    else:
        status = "resolved_loaded_bank"

    return ResolvedRouteTarget(
        source_cid=source_cid,
        source_character=source_character,
        movement_type=movement_type,
        src_slot=src_slot,
        dst_bank=dst_bank,
        dst_slot=dst_slot,
        target_cid=target_cid,
        target_character=ctx.character_names.get(target_cid or "") if target_cid else None,
        target_slot_index=target_slot_index,
        target_animation_index=target_anim,
        target_khd_path=str(ctx.khd_paths_by_cid[target_cid]) if target_cid in ctx.khd_paths_by_cid else None,
        target_mot_path=str(ctx.mot_paths_by_cid[target_cid]) if target_cid in ctx.mot_paths_by_cid else None,
        resolution_status=status,
        confidence=target.confidence,
        reason=f"0x{raw_move_id:04X}: {target.reason}",
    )
