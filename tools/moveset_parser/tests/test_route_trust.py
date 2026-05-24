from __future__ import annotations

from pathlib import Path

import pytest

from luxformats import parse_khd
from route_trust import classify_slot_cells


pytestmark = pytest.mark.needs_dump


def test_empty_cell_movement_slot_has_no_offensive_cells():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr001.khd").read_bytes())
    slot = bank.slots[263]

    assert classify_slot_cells(bank, slot) == []


def test_damage_cell_with_valid_window_is_offensive():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr002.khd").read_bytes())
    slot = bank.slots[259]
    cells = classify_slot_cells(bank, slot)

    assert cells
    assert any(c.has_offensive_hit and c.active_start == 35 for c in cells)
    assert all(c.confidence == "confirmed_static_data" for c in cells)


def test_hilde_invalid_attack_window_is_unresolved_not_trusted():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr028.khd").read_bytes())
    slot = bank.slots[293]
    cells = classify_slot_cells(bank, slot)

    assert cells
    assert any(c.confidence == "unresolved" for c in cells)
    assert any("invalid active window" in c.reason for c in cells)
