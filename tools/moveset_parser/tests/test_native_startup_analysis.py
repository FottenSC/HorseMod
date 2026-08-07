from pathlib import Path

import pytest

from luxformats import parse_khd
from native_startup_analysis import analyze_player_startup


HDR_ROOT = Path(__file__).parents[3] / "dump" / "Battle" / "hdr"


def _bank(cid: str):
    path = HDR_ROOT / f"hdr{cid}.khd"
    if not path.exists():
        pytest.skip(f"checked-in {path.name} is unavailable")
    return parse_khd(path.read_bytes())


def test_astaroth_bear_fang_uses_timed_contact_variant_and_one_based_impact():
    evidence = analyze_player_startup(_bank("012"), 341, 110)

    assert evidence is not None
    assert evidence.route_cell == 110
    assert evidence.effective_cell == 111
    assert evidence.effective_variant == 1
    assert evidence.master_window_start == 15
    assert evidence.selection_coordinate == 15
    assert evidence.impact_coordinate == 15
    assert evidence.player_impact_frame == 16


def test_direct_contact_cell_converts_zero_based_coordinate_to_impact_frame():
    evidence = analyze_player_startup(_bank("012"), 347, 122)

    assert evidence is not None
    assert evidence.effective_cell == 122
    assert evidence.master_window_start == 13
    assert evidence.player_impact_frame == 14


def test_dynamic_variant_without_local_timing_proof_fails_closed():
    # hdr011 slot 386 has a special conditional variant whose nearby timing
    # operand is the 0x7FFF sentinel.  It must not become an ordinary i2 move.
    assert analyze_player_startup(_bank("011"), 386, 137) is None


def test_variant_selection_later_than_cell_window_controls_impact():
    evidence = analyze_player_startup(_bank("022"), 400, 248)

    assert evidence is not None
    assert evidence.master_window_start == 17
    assert evidence.selection_coordinate == 18
    assert evidence.player_impact_frame == 19
