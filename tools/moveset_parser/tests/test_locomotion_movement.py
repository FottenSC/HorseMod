from __future__ import annotations

from functools import lru_cache
from pathlib import Path

from locomotion_movement import (
    BACKWALK_START_SLOT,
    _base_backwalk_effect_speed_word,
    _combine_root_and_delayed_effect_velocity,
    analyze_all,
)
from luxformats import parse_khd
from stackvm_emulate import emulate


REPO_ROOT = Path(__file__).resolve().parents[3]
HDR_DIR = REPO_ROOT / "dump" / "Battle" / "hdr"


def _backwalk_script(cid: str):
    bank = parse_khd((HDR_DIR / f"hdr{cid}.khd").read_bytes())
    return bank, emulate(bank.slots[BACKWALK_START_SLOT].bytecode, BACKWALK_START_SLOT)


@lru_cache(maxsize=1)
def _all_rows():
    return analyze_all(REPO_ROOT / "dump" / "Battle")


def test_delayed_effect_velocity_starts_after_inclusive_frame_two_clear():
    assert _combine_root_and_delayed_effect_velocity((0.0,) * 6, 50, 3) == (
        0.0,
        0.0,
        0.0,
        0.05,
        0.1,
        0.15,
    )


def test_haohmaru_backwalk_uses_base_30c6_velocity():
    bank, script = _backwalk_script("061")
    assert _base_backwalk_effect_speed_word("061", bank, script) == 4


def test_hwang_30c6_move_table_special_adds_seventy():
    bank, script = _backwalk_script("009")
    assert _base_backwalk_effect_speed_word("009", bank, script) == 50


def test_standard_30c1_backwalk_uses_authored_direction_four_velocity():
    bank, script = _backwalk_script("001")
    assert _base_backwalk_effect_speed_word("001", bank, script) == 4


def test_voldo_backwalk_does_not_invent_an_unwritten_velocity():
    bank, script = _backwalk_script("005")
    assert _base_backwalk_effect_speed_word("005", bank, script) == 0


def test_all_held_curves_cover_page_horizon_and_include_native_velocity():
    rows = _all_rows()
    assert len(rows) == 28
    assert all(len(row.backwalk_held_curve_metres) == 181 for row in rows)
    assert all(len(row.forward_run_curve_metres) == 181 for row in rows)
    assert all(row.forward_run_root_curve_complete for row in rows)


def test_backwalk_velocity_persists_after_start_route_handoff():
    rows = {row.cid: row for row in _all_rows()}
    mitsurugi = rows["001"]
    # The standard +0.004/tick channel is still present sixty frames after
    # the start/continue route has handed off to the loop.
    root_only_speed = (
        mitsurugi.backwalk_loop.route_distance_metres
        / mitsurugi.backwalk_loop.motion_route_length_frames
        * 60.0
    )
    assert abs(mitsurugi.backwalk_metres_per_second - (root_only_speed + 0.24)) < 0.01


def test_grounded_conditional_velocity_is_exposed_not_discarded():
    rows = {row.cid: row for row in _all_rows()}
    route = rows["001"].ukemi["forward"][0]
    assert route.conditional_effect_curve_metres is not None
    assert route.conditional_effect_angle_word == 180
    assert route.conditional_effect_curve_metres != route.effective_curve_metres
    assert route.root_curve_complete


def test_known_backwalk_ordering_uses_combined_native_channels():
    rows = {row.cid: row for row in _all_rows()}
    assert rows["009"].backwalk_metres_per_second > 5.5  # Hwang +70 style-table branch
    assert rows["011"].backwalk_metres_per_second > rows["007"].backwalk_metres_per_second + 0.4
    assert rows["061"].backwalk_metres_per_second > 2.4


def test_character_direct_run_overrides_change_complete_distance():
    rows = {row.cid: row for row in _all_rows()}
    xianghua = rows["00d"]
    haohmaru = rows["061"]
    mitsurugi = rows["001"]
    assert xianghua.forward_run_metres_per_second > 13.0
    assert haohmaru.forward_run_metres_per_second > 9.0
    assert mitsurugi.forward_run_metres_per_second < 8.0
    assert xianghua.forward_run_curve_metres[120] > mitsurugi.forward_run_curve_metres[120] + 8.0
