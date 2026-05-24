from __future__ import annotations

from pathlib import Path

import pytest

from bank_resolver import BankResolutionContext, resolve_route_target
from luxformats import parse_khd, parse_mot


pytestmark = pytest.mark.needs_dump


def _ctx() -> BankResolutionContext:
    khd_path = Path("E:/myMods/dump/Battle/hdr/hdr001.khd")
    mot_path = Path("E:/myMods/dump/Battle/mot/chr001.mot")
    return BankResolutionContext(
        khd_by_cid={"001": parse_khd(khd_path.read_bytes())},
        mot_by_cid={"001": parse_mot(mot_path.read_bytes())},
        character_names={"001": "Mitsurugi"},
        khd_paths_by_cid={"001": khd_path},
        mot_paths_by_cid={"001": mot_path},
        confirmed_bank_map={2: "001"},
    )


def test_same_bank_target_resolves_local():
    resolved = resolve_route_target(
        source_cid="001",
        source_character="Mitsurugi",
        movement_type="backstep_candidate",
        src_slot=1,
        dst_bank=0,
        dst_slot=263,
        raw_move_id=263,
        ctx=_ctx(),
    )

    assert resolved.resolution_status == "resolved_local"
    assert resolved.target_cid == "001"
    assert resolved.target_slot_index == 263
    assert resolved.target_animation_index is not None


def test_internal_move_bucket_resolves_without_external_map():
    resolved = resolve_route_target(
        source_cid="001",
        source_character="Mitsurugi",
        movement_type="sidestep_up_candidate",
        src_slot=1,
        dst_bank=2,
        dst_slot=263,
        raw_move_id=(2 << 12) | 263,
        ctx=_ctx(),
    )

    assert resolved.resolution_status == "resolved_move_bucket"
    assert resolved.confidence == "confirmed_static_data"
    assert resolved.target_character == "Mitsurugi"


def test_unknown_cross_bank_stays_unresolved():
    resolved = resolve_route_target(
        source_cid="001",
        source_character="Mitsurugi",
        movement_type="sidestep_up_candidate",
        src_slot=1,
        dst_bank=4,
        dst_slot=263,
        raw_move_id=(4 << 12) | 263,
        ctx=_ctx(),
    )

    assert resolved.resolution_status == "unresolved_unknown_bank"
    assert resolved.target_cid is None


def test_missing_target_slot_is_explicit():
    resolved = resolve_route_target(
        source_cid="001",
        source_character="Mitsurugi",
        movement_type="sidestep_up_candidate",
        src_slot=1,
        dst_bank=0,
        dst_slot=999999,
        raw_move_id=999999,
        ctx=_ctx(),
    )

    assert resolved.resolution_status == "unresolved_missing_target_slot"
