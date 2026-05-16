"""End-to-end smoke test on the exported JSON.

If `webui/public/data/chars/001.json` doesn't have the expected shape,
the React UI's loaders will crash at runtime. Pin the schema here.
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest

DATA_DIR = Path(__file__).parent.parent / "webui" / "public" / "data"


def test_roster_json_shape():
    roster_path = DATA_DIR / "roster.json"
    if not roster_path.exists():
        pytest.skip("roster.json not generated yet")
    data = json.loads(roster_path.read_text(encoding="utf-8"))
    assert "chars" in data
    assert isinstance(data["chars"], list)
    assert len(data["chars"]) > 0
    for c in data["chars"]:
        assert "cid" in c
        assert "name" in c
        assert "kind" in c
        assert "files" in c


def test_mitsurugi_json_shape():
    path = DATA_DIR / "chars" / "001.json"
    if not path.exists():
        pytest.skip("chars/001.json not generated yet")
    data = json.loads(path.read_text(encoding="utf-8"))
    assert "cid" in data and data["cid"] == "001"
    assert "name" in data and data["name"] == "Mitsurugi"
    assert "khd" in data
    khd = data["khd"]
    for f in ("cells", "slots", "slotEdges", "stanceRoots", "flatMoves"):
        assert f in khd, f"missing field {f!r} in khd payload"
    # No dead/leftover fields
    assert "moveTrees" not in khd, "dead moveTrees field still being exported"

    # Cells have the expected shape
    cell = khd["cells"][0]
    for f in ("idx", "role", "class", "damage", "activeStart", "onHitStanding",
              "onBlock", "rangeStandMin", "rangeStandMax", "inputCond"):
        assert f in cell

    # Slots have the expected shape
    slot = khd["slots"][0]
    for f in ("idx", "animationIndex", "animLength", "cellVariants",
              "bytecodeOffset"):
        assert f in slot

    # SlotEdges have the expected shape
    if khd["slotEdges"]:
        edge = khd["slotEdges"][0]
        for f in ("src", "dst", "bank", "rawId", "input", "kind", "subOp",
                  "args", "indirect", "callcond", "pc"):
            assert f in edge

    # FlatMoves have the expected shape
    if khd["flatMoves"]:
        m = khd["flatMoves"][0]
        for f in ("slot", "anim", "cell", "inputs", "kinds", "slots",
                  "rootSlot", "rootAnim"):
            assert f in m


def test_mitsurugi_has_meaningful_moves():
    path = DATA_DIR / "chars" / "001.json"
    if not path.exists():
        pytest.skip("chars/001.json not generated yet")
    khd = json.loads(path.read_text(encoding="utf-8"))["khd"]
    moves = khd["flatMoves"]
    # ~165 moves expected (current baseline)
    assert len(moves) >= 130

    # Slot 401 specifically — the canary from the user's bug report
    slot_401 = next((m for m in moves if m["slot"] == 401), None)
    assert slot_401 is not None
    assert slot_401["inputs"], "slot 401 must have a non-empty input chain"


def test_mitsurugi_movelist_payload_shape():
    """Pin the canonical-movelist JSON shape. If `_build_movelist_payload`
    drops a field downstream consumers (the UI) silently break."""
    path = DATA_DIR / "chars" / "001.json"
    if not path.exists():
        pytest.skip("chars/001.json not generated yet")
    data = json.loads(path.read_text(encoding="utf-8"))
    ml = data.get("movelist")
    if ml is None:
        pytest.skip("movelist not available (UE4 dump missing for this run)")
    assert "categories" in ml
    assert "moves" in ml
    assert "ryuuhaType" in ml
    assert len(ml["moves"]) >= 100, "Mitsurugi should have ≥100 movelist entries"

    # Spot-check: Heaven Cannon exists with proper name + input
    heaven = next((m for m in ml["moves"] if m["name"] == "Heaven Cannon"), None)
    assert heaven is not None
    assert heaven["input"] == "3B"

    # Every move's first CommandSet has resolved cell/slot indices
    for m in ml["moves"]:
        cs = m["commandSets"][0]
        for f in ("mainIndex", "introIndex", "cellIdx", "slotIdx", "resolution"):
            assert f in cs, f"missing {f!r} in move {m['name']!r}"
        # No "none" should survive — we drop them at export time
        assert cs["resolution"] != "none", f"unresolved CommandSet in {m['name']!r}"

    # Coverage check: ≥75% of moves should have a resolved cell
    with_cell = sum(1 for m in ml["moves"] if m["commandSets"][0]["cellIdx"] >= 0)
    assert with_cell / len(ml["moves"]) >= 0.75, (
        f"only {with_cell}/{len(ml['moves'])} moves have stats — resolver regression?"
    )


def test_no_question_mark_inputs():
    """The synthetic '?A' / '?B' / '?X' / '?·' fallback labels should
    never appear in exported data."""
    path = DATA_DIR / "chars" / "001.json"
    if not path.exists():
        pytest.skip("chars/001.json not generated yet")
    khd = json.loads(path.read_text(encoding="utf-8"))["khd"]
    for m in khd["flatMoves"]:
        for inp in m["inputs"]:
            assert not inp.startswith("?"), (
                f"slot {m['slot']} has fake fallback input {inp!r}"
            )
