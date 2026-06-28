"""End-to-end smoke test on the exported JSON.

If `webui/public/data/chars/001.json` doesn't have the expected shape,
the React UI's loaders will crash at runtime. Pin the schema here.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

from export_webui_data import _build_move_groups
import export_webui_data

DATA_DIR = Path(__file__).parent.parent / "webui" / "public" / "data"


def test_build_move_groups_marks_duplicates_and_input_families():
    moves = [
        {"order": 0, "moveId": 10, "condition": "", "input": "214A", "name": "Poseidon Tide"},
        {"order": 1, "moveId": 11, "condition": "", "input": "214A.A", "name": "Poseidon Tide Rush"},
        {"order": 2, "moveId": 10, "condition": "", "input": "214A", "name": "Poseidon Tide"},
        {"order": 3, "moveId": 12, "condition": "During Foo", "input": "214A.A", "name": "Different Stance"},
        {"order": 4, "moveId": 13, "condition": "", "input": "214AB", "name": "Not A Continuation"},
    ]

    groups = _build_move_groups(moves)

    duplicate_groups = [g for g in groups if g["kind"] == "duplicate-move-id"]
    input_groups = [g for g in groups if g["kind"] == "input-family"]
    assert duplicate_groups == [
        {
            "id": "duplicate-move-id-0",
            "kind": "duplicate-move-id",
            "reason": "same DA_MovePlayData MoveListID appears in multiple movelist rows",
            "rootOrder": 0,
            "orders": [0, 2],
            "moveIds": [10],
            "condition": "",
            "baseInput": "214A",
            "displayName": "Poseidon Tide",
        }
    ]
    assert any(
        g["orders"] == [0, 1, 2] and g["rootOrder"] == 0 and g["baseInput"] == "214A"
        for g in input_groups
    )
    assert all(4 not in g["orders"] for g in input_groups)
    assert moves[0]["groupIds"] == ["duplicate-move-id-0", "input-family-1"]
    assert moves[1]["groupIds"] == ["input-family-1"]
    assert moves[3]["groupIds"] == []


def _fake_export_move(
    order: int,
    name: str,
    input_: str,
    *,
    move_id: int | None = None,
    condition: str = "",
    cell_idx: int = -1,
    slot_idx: int = -1,
) -> dict:
    return {
        "moveId": move_id if move_id is not None else order + 1,
        "category": 0,
        "order": order,
        "name": name,
        "condition": condition,
        "input": input_,
        "fullCommand": f"{condition} {input_}".strip(),
        "inputMarkup": "",
        "note": "",
        "isRevengeAttack": False,
        "isMovementOnly": False,
        "hasInputAlternatives": "|" in input_,
        "inputVariants": [],
        "tracking": {"hasFacingCommit": False, "hasRetrackRamp": False, "maxTargetWeight": None, "events": []},
        "communityFrame": None,
        "isThrowInput": False,
        "attributeTag": "",
        "hitClasses": ["High"],
        "effectTags": [],
        "mainTip": "",
        "lethalHitCondition": "",
        "commandSets": [{
            "commandSetIndex": 0,
            "mainIndex": order + 1,
            "introIndex": 0,
            "cellIdx": cell_idx,
            "slotIdx": slot_idx,
            "resolution": "cell-direct",
            "candidateCount": 1,
            "candidateBestRank": 0,
            "candidateScore": 1,
            "tracking": {"hasFacingCommit": False, "hasRetrackRamp": False, "maxTargetWeight": None, "events": []},
        }],
    }


def test_player_move_families_use_community_rows_when_available(monkeypatch):
    moves = [
        _fake_export_move(0, "Barbed Blades", "A", cell_idx=22, slot_idx=260),
        _fake_export_move(1, "Barbed Blades", "A.A", cell_idx=23, slot_idx=261),
        _fake_export_move(2, "Shadow Banishment", "B", cell_idx=30, slot_idx=270),
    ]
    groups = _build_move_groups(moves)
    monkeypatch.setattr(export_webui_data, "_load_community_frame_data", lambda: {
        "chars": {
            "003": {
                "moves": [
                    {
                        "name": "Barbed Blades",
                        "category": "HorizontalAttacks",
                        "stance": "",
                        "command": "A",
                        "startup": 10,
                        "damage": [8],
                        "block": "-8",
                        "hit": "2",
                        "counterHit": "2",
                        "guardBurst": 1,
                        "hitLevels": ["High"],
                        "notes": "",
                    },
                    {
                        "name": "Barbed Blades",
                        "category": "HorizontalAttacks",
                        "stance": "",
                        "command": "AA",
                        "startup": 10,
                        "damage": [8, 8],
                        "block": "-6",
                        "hit": "4",
                        "counterHit": "4",
                        "guardBurst": 2,
                        "hitLevels": ["High", "High"],
                        "notes": "",
                    },
                ],
            }
        }
    })

    families, summary = export_webui_data._build_player_move_families("003", moves, groups, None)

    community_family = next(f for f in families if f["rootName"] == "Barbed Blades")
    assert community_family["confidence"] == "mixed-supported"
    assert community_family["relations"] == ["prefix"]
    assert community_family["rows"][0]["source"] == "mixed"
    assert community_family["rows"][0]["parserMoveOrders"] == [0]
    assert community_family["rows"][0]["metrics"]["startup"] == 10
    assert community_family["rows"][1]["displayCommand"] == "AA"
    assert summary["communityRows"] == 2
    assert summary["communityCoveredParserRows"] == 2
    assert any(f["rootName"] == "Shadow Banishment" for f in families)


def test_player_move_families_fallback_when_community_missing(monkeypatch):
    moves = [
        _fake_export_move(0, "Slash Sequence", "A"),
        _fake_export_move(1, "Slash Sequence", "A.A"),
        _fake_export_move(2, "Virtuous Contract", "A+B"),
    ]
    groups = _build_move_groups(moves)
    monkeypatch.setattr(export_webui_data, "_load_community_frame_data", lambda: {"chars": {}})

    families, summary = export_webui_data._build_player_move_families("060", moves, groups, None)

    assert summary["communityRows"] == 0
    assert summary["rawMoveRows"] == 3
    assert summary["parserFallbackFamilies"] == 1
    slash = next(f for f in families if f["rootName"] == "Slash Sequence")
    assert slash["relations"] == ["prefix"]
    assert [row["source"] for row in slash["rows"]] == ["movelist", "movelist"]
    assert all(row["timelineStatus"] == "native-cell-only" for row in slash["rows"])
    contract = next(f for f in families if f["rootName"] == "Virtuous Contract")
    assert contract["kind"] == "single-row"


def test_move_metrics_native_fallback_uses_current_cell_fields():
    class FakeCell:
        wI16MasterWindowStart = 12
        wI16BaseDamage = 24
        wI16BlockstunFrames = -14
        wI16HitstunStandingNormal = 6
        attack_class = "Mid"

    class FakeSection:
        entries = [FakeCell()]

    class FakeKhd:
        sections = [FakeSection()]

    move = _fake_export_move(0, "Launcher", "3B", cell_idx=0, slot_idx=10)
    move["hitClasses"] = []

    metrics = export_webui_data._move_metrics(move, FakeKhd())

    assert metrics == {
        "startup": 12,
        "damage": [24],
        "block": -14,
        "hit": 6,
        "counterHit": None,
        "hitLevels": ["Mid"],
    }


def test_export_main_out_dir_alias_writes_to_alternate_directory(tmp_path, monkeypatch):
    out_dir = tmp_path / "out"

    def fake_export_char(cid: str, paths: dict[str, str], out_path: str) -> None:
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        Path(out_path).write_text(json.dumps({"cid": cid, "name": "Mitsurugi"}), encoding="utf-8")

    monkeypatch.setattr(export_webui_data, "discover_chars", lambda root: {"001": {"khd": "fake"}})
    monkeypatch.setattr(export_webui_data, "char_summary", lambda cid, paths: {
        "cid": cid,
        "name": "Mitsurugi",
        "kind": "base",
        "files": {"khd": True, "mot": False, "dtp": False, "atkhit": False, "bodyhit": False, "yararehit": False},
    })
    monkeypatch.setattr(export_webui_data, "export_char", fake_export_char)
    monkeypatch.setattr(sys, "argv", [
        "export_webui_data.py",
        "--root",
        str(tmp_path / "battle"),
        "--out-dir",
        str(out_dir),
    ])

    assert export_webui_data.main() == 0
    assert (out_dir / "roster.json").exists()
    assert (out_dir / "chars" / "001.json").exists()


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
    for f in ("cells", "throws", "eventRecords", "slots", "slotEdges", "stanceRoots", "flatMoves"):
        assert f in khd, f"missing field {f!r} in khd payload"
    for f in ("moveCount", "throwCount", "eventRecordTableOffset", "eventRecordCount",
              "parsedEventRecordCount", "eventRecordPrefixBytes", "firstCancelOffset"):
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
    for f in ("idx", "animationIndex", "animLength", "totalFrames",
              "playbackSpeed60ths", "playbackSpeed", "cellVariants",
              "attackCellRefs", "throwCellRefs", "bytecodeOffset"):
        assert f in slot
    if slot.get("bytecode") is not None:
        assert "facingEffects" in slot["bytecode"]

    if khd["eventRecords"]:
        event = khd["eventRecords"][0]
        for f in ("idx", "offset", "packedMoveId", "resolvedSlot", "eventKind",
                  "eventKindName", "field08", "shapeFlags", "offsetX", "offsetY",
                  "offsetZ", "field1C", "field20", "field24", "radiusScale",
                  "field2C", "key", "typeTag", "typeName"):
            assert f in event, f"missing field {f!r} in event record payload"

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
    assert "moveGroups" in ml
    assert "ryuuhaType" in ml
    assert len(ml["moves"]) >= 100, "Mitsurugi should have ≥100 movelist entries"

    # Spot-check: Heaven Cannon exists with proper name + input
    heaven = next((m for m in ml["moves"] if m["name"] == "Heaven Cannon"), None)
    assert heaven is not None
    assert heaven["input"] == "3B"
    if ml["moveGroups"]:
        group = ml["moveGroups"][0]
        for f in ("id", "kind", "reason", "rootOrder", "orders", "moveIds",
                  "condition", "baseInput", "displayName"):
            assert f in group, f"missing moveGroups.{f!r}"

    # Every move's first CommandSet has resolved cell/slot indices
    for m in ml["moves"]:
        cs = m["commandSets"][0]
        for f in ("mainIndex", "introIndex", "cellIdx", "slotIdx", "resolution"):
            assert f in cs, f"missing {f!r} in move {m['name']!r}"
        assert "tracking" in cs, f"missing tracking in move {m['name']!r}"
        for f in ("hasFacingCommit", "hasRetrackRamp", "maxTargetWeight", "events"):
            assert f in cs["tracking"], f"missing tracking.{f!r} in move {m['name']!r}"
        assert "communityFrame" in m, f"missing communityFrame in move {m['name']!r}"
        assert "isRevengeAttack" in m, f"missing isRevengeAttack in move {m['name']!r}"
        assert "groupIds" in m, f"missing groupIds in move {m['name']!r}"
        if m["communityFrame"] is not None:
            for f in ("source", "onBlock", "onHit", "onCounterHit"):
                assert f in m["communityFrame"], f"missing communityFrame.{f!r} in move {m['name']!r}"
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
