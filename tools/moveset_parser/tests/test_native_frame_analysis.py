from __future__ import annotations

from pathlib import Path

import pytest

from luxformats import parse_auto
from native_frame_analysis import analyze_confirmed_slot_frames, analyze_throw_break_frames
from native_reaction_table import parse_hit_reaction_move_id_table


def test_native_reaction_table_parses_counted_facing_rows():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "yarare.dat"
    if not path.exists():
        pytest.skip("checked-in native yarare table is unavailable")
    table = parse_hit_reaction_move_id_table(path.read_bytes())

    assert len(table.rows) == 1239
    assert table.rows[13].base_move_ids_by_facing == (
        0x2059,
        0x205A,
        0x205B,
        0x205C,
    )
    assert table.rows[814].base_move_ids_by_facing == (
        0xA245,
        0xA246,
        0xA247,
        0xA248,
    )


def test_frame_advantage_requires_native_reaction_route_table():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))

    assert analyze_confirmed_slot_frames(khd, attack_slot=310, attack_cell=71) is None


def test_common_frame_analyzer_reproduces_audited_bear_tamer_result():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))
    reaction_table = parse_hit_reaction_move_id_table(
        path.with_name("yarare.dat").read_bytes()
    )

    frame = analyze_confirmed_slot_frames(
        khd,
        attack_slot=310,
        attack_cell=71,
        reaction_table=reaction_table,
    )

    assert frame is not None
    assert frame.total_frames == 69
    assert frame.cell_window_start_coordinate == 18
    assert frame.recovery_lead == 18
    assert frame.recovery_open_coordinate == 50
    assert frame.inclusive_recovery_frames == 33
    assert (frame.block_advantage, frame.hit_advantage) == (-8, 2)
    assert frame.counter_hit_stun_frames == 35
    assert frame.counter_hit_advantage == 2
    assert frame.hit_reaction.reaction_row_id == 272
    assert len(frame.hit_reaction.resolved_slots) == 8
    assert any(
        "contact-mode11-selects-shared-special-column+0x48" in resolution
        for resolution in frame.resolutions
    )


def test_throw_break_analyzer_uses_native_attempt_cell_and_recovery():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))

    frame = analyze_throw_break_frames(khd, attack_slot=482, attack_cell=292)

    assert frame is not None
    assert frame.total_frames == 54
    assert frame.cell_window_start_coordinate == 17
    assert frame.recovery_lead == 0
    assert frame.recovery_open_coordinate == 53
    assert frame.inclusive_recovery_frames == 37
    assert frame.break_stun_frames == 30
    assert frame.break_advantage == -7
