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


def test_production_exporter_has_no_comparison_import_boundary_leak():
    source = Path(export_webui_data.__file__).read_text(encoding="utf-8")
    for forbidden in (
        "community_framedata",
        "from player_move_families import",
        "communityFrame",
        "--community-json",
        "--community-xlsx",
        "docs.google.com/spreadsheets",
    ):
        assert forbidden not in source


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


def test_category_listings_collapse_to_one_move_identity():
    main = _fake_export_move(1, "Titan Bomb", "236A+G", move_id=123)
    main["category"] = 0
    throw = _fake_export_move(183, "Titan Bomb", "236A+G", move_id=123)
    throw["category"] = 8
    # Category membership is the only difference.
    throw["commandSets"] = main["commandSets"]

    moves = export_webui_data._canonicalize_category_listings([main, throw])

    assert len(moves) == 1
    assert moves[0]["moveId"] == 123


def test_category_listings_do_not_make_throw_status_part_of_move_identity():
    main = _fake_export_move(1, "Titan Bomb", "236A+G", move_id=123)
    main["category"] = 0
    main["isThrowInput"] = False
    throw_tab = _fake_export_move(183, "Titan Bomb", "236A+G", move_id=123)
    throw_tab["category"] = 8
    throw_tab["isThrowInput"] = True
    throw_tab["commandSets"] = main["commandSets"]

    moves = export_webui_data._canonicalize_category_listings([main, throw_tab])

    assert len(moves) == 1
    assert moves[0]["categoryMemberships"] == [0, 8]
    assert moves[0]["order"] == 0
    assert moves[0]["listingOrders"] == [1, 183]
    assert moves[0]["categoryMemberships"] == [0, 8]
    assert len(moves[0]["authoredVariants"]) == 1


def test_native_timing_variant_groups_fast_input_without_category_identity():
    def native_link(definition_id: int, durations: list[int]) -> dict:
        masks = [0x0004, 0x0008, 0x0040, 0x2440, 0x0001]
        return {
            "status": "heuristic",
            "resolutions": [],
            "definitions": [{
                "lane": "primary-fighter",
                "mainDefinitionId": definition_id,
                "fallbackDefinitionId": 0,
                "mainDefinition": {
                    "controlFlow": "native-linear",
                    "buttonSteps": [
                        {"mask": mask, "durationFrames": duration}
                        for mask, duration in zip(masks, durations)
                    ],
                },
            }],
            "slots": [],
            "cells": [],
        }

    normal = _fake_export_move(0, "Titan Bomb", "236A+G", move_id=123)
    fast = _fake_export_move(1, "Titan Bomb", "236A+G (fast)", move_id=124)
    normal["nativeLink"] = native_link(402, [1, 1, 5, 5, 1])
    fast["nativeLink"] = native_link(403, [2, 2, 1, 3, 1])
    for move in (normal, fast):
        move["listingOrders"] = [move["order"]]
        move["categoryMemberships"] = [8]
        move["authoredVariants"] = [{"nativeLink": move["nativeLink"]}]

    groups = _build_move_groups([normal, fast])
    timing = next(group for group in groups if group["kind"] == "native-timing-variant")
    assert timing["orders"] == [0, 1]
    families, _ = export_webui_data._build_player_move_families(
        "012", [normal, fast], groups, None
    )
    assert len(families) == 1
    assert families[0]["relations"] == ["timing-variant"]
    assert [row["displayCommand"] for row in families[0]["rows"]] == [
        "236A+G", "236A+G (fast)"
    ]


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
        "isThrowInput": False,
        "attributeTag": "",
        "hitClasses": ["High"],
        "effectTags": [],
        "mainTip": "",
        "lethalHitCondition": "",
        "commandSets": [{
            "commandSetIndex": 0,
            "lane": "primary-fighter",
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


def test_player_move_families_preserve_only_official_rows():
    moves = [
        _fake_export_move(0, "Barbed Blades", "A", cell_idx=22, slot_idx=260),
        _fake_export_move(1, "Barbed Blades", "A.A", cell_idx=23, slot_idx=261),
        _fake_export_move(2, "Shadow Banishment", "B", cell_idx=30, slot_idx=270),
    ]
    for move, status in zip(moves, ("heuristic", "ambiguous", "unresolved"), strict=True):
        move["nativeLink"] = {
            "status": status,
            "resolutions": [],
            "definitions": [],
            "slots": [],
            "cells": [],
        }
    groups = _build_move_groups(moves)
    families, summary = export_webui_data._build_player_move_families("003", moves, groups, None)

    family = next(f for f in families if f["rootName"] == "Barbed Blades")
    assert family["confidence"] == "native-inferred"
    assert family["relations"] == ["prefix"]
    assert [row["displayCommand"] for row in family["rows"]] == ["A", "A.A"]
    assert all(row["source"] == "game-movelist-table" for row in family["rows"])
    assert summary["officialRows"] == 3
    assert summary["playerRows"] == 3
    assert summary["nativeLinkedRows"] == 1
    assert summary["nativeUnlinkedRows"] == 2
    assert "authoredCategoryListings" not in summary
    assert "categoryDuplicateListings" not in summary
    assert all("category" not in row and "categoryMemberships" not in row
               for item in families for row in item["rows"])
    assert any(f["rootName"] == "Shadow Banishment" for f in families)


def test_player_move_families_do_not_synthesize_prefix_rows():
    moves = [
        _fake_export_move(0, "Prime Moon Shadow Rush", "A.A.A"),
        _fake_export_move(1, "Virtuous Contract", "A+B"),
    ]
    groups = _build_move_groups(moves)

    families, summary = export_webui_data._build_player_move_families("060", moves, groups, None)

    commands = [row["displayCommand"] for family in families for row in family["rows"]]
    assert commands == ["A.A.A", "A+B"]
    assert "A" not in commands and "A.A" not in commands
    assert summary["officialRows"] == summary["playerRows"] == 2


def test_native_route_alternatives_group_same_named_commands_only():
    moves = [
        _fake_export_move(0, "Bear Tamer", "B.A"),
        _fake_export_move(1, "Bear Tamer", "B.6A"),
        _fake_export_move(2, "Great Divide", "B.B"),
    ]
    for move in moves:
        move["nativeLink"] = {
            "status": "heuristic",
            "resolutions": [],
            "definitions": [],
            "slots": [308, 310],
            "cells": [67, 71],
            "attackSlots": [308, 310],
            "attackCells": [67, 71],
        }

    groups = _build_move_groups(moves)
    route_group = next(group for group in groups if group["kind"] == "native-route-alternative")
    assert route_group["orders"] == [0, 1]

    families, summary = export_webui_data._build_player_move_families(
        "012", moves, groups, None
    )
    bear_tamer = next(family for family in families if family["rootName"] == "Bear Tamer")
    assert bear_tamer["id"] == "player-family-012-official-00000"
    assert bear_tamer["relations"] == ["native-route-alternative"]
    assert [row["displayCommand"] for row in bear_tamer["rows"]] == ["B.A", "B.6A"]
    assert any(family["id"] == "player-family-012-official-00002"
               and family["rootName"] == "Great Divide" and len(family["rows"]) == 1
               for family in families)
    assert summary["officialRows"] == summary["playerRows"] == 3


def test_native_route_grouping_uses_effective_attacks_not_navigation():
    same_attack_a = _fake_export_move(0, "Same Move", "A")
    same_attack_b = _fake_export_move(1, "Same Move", "6A")
    different_attack = _fake_export_move(2, "Same Move", "4A")
    no_attack = _fake_export_move(3, "Same Move", "2A")
    for move, slots, cells, attack_slots, attack_cells in (
        (same_attack_a, [100, 101], [10, 11], [101], [11]),
        (same_attack_b, [200, 101], [20, 11], [101], [11]),
        (different_attack, [100, 101], [10, 11], [102], [12]),
        (no_attack, [100, 101], [10, 11], [], []),
    ):
        move["nativeLink"] = {
            "status": "heuristic",
            "resolutions": [],
            "definitions": [],
            "slots": slots,
            "cells": cells,
            "attackSlots": attack_slots,
            "attackCells": attack_cells,
        }

    groups = _build_move_groups([
        same_attack_a, same_attack_b, different_attack, no_attack
    ])
    route_groups = [
        group for group in groups if group["kind"] == "native-route-alternative"
    ]

    assert [group["orders"] for group in route_groups] == [[0, 1]]


def test_move_metrics_do_not_treat_definition_id_as_attack_cell():
    class FakeCell:
        cell_role = "Attack"
        wI16MasterWindowStart = 12
        wI16BaseDamage = 24
        wI16BlockstunFrames = -14
        wI16HitstunBaseContact = 6
        attack_class = "Mid"

    class FakeSection:
        entries = [FakeCell()]

    class FakeKhd:
        sections = [FakeSection()]

    move = _fake_export_move(0, "Launcher", "3B", cell_idx=0, slot_idx=10)
    move["hitClasses"] = []

    metrics, evidence = export_webui_data._move_metrics(move, FakeKhd())

    assert metrics == {
        "startup": None,
        "damage": [],
        "block": None,
        "hit": None,
        "counterHit": None,
        "guardBurst": None,
        "hitLevels": [],
    }
    assert evidence["startup"] == {"source": "unknown", "status": "unknown"}
    assert evidence["block"] == {"source": "unknown", "status": "unknown"}


def test_unproven_movement_metrics_remain_unknown():
    move = _fake_export_move(0, "Native row", "A+G", cell_idx=0, slot_idx=10)
    move["isMovementOnly"] = True
    metrics, evidence = export_webui_data._move_metrics(move, None)
    assert metrics["startup"] is None
    assert metrics["damage"] == []
    assert metrics["block"] is None
    assert evidence["startup"]["status"] == "unknown"


def test_legacy_payload_fallback_does_not_publish_raw_cell_coordinate_as_startup():
    move = _fake_export_move(0, "Native row", "3B")
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["legacy-route"],
        "definitions": [],
        "slots": [10],
        "cells": [0],
    }
    payload = {"khd": {"cells": [{
        "role": "Attack",
        "damage": 20,
        "class": "Mid",
        "activeStartCoordinate": 13,
        "activeEndCoordinate": 15,
    }]}}

    metrics, evidence = export_webui_data._move_metrics_from_payload(move, payload)

    assert metrics["startup"] is None
    assert evidence["startup"] == {"source": "unknown", "status": "unknown"}


def test_legacy_payload_fallback_uses_exported_player_startup_proof_and_effective_cell():
    move = _fake_export_move(0, "Native row", "4B")
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["native-timed-cell-variant+zero-based-window"],
        "definitions": [],
        "slots": [341],
        "cells": [0],
        "startupProof": {"effectiveCell": 1, "playerImpactFrame": 16},
    }
    payload = {"khd": {"cells": [
        {"role": "Attack", "damage": 20, "class": "Mid", "activeStartCoordinate": 13},
        {"role": "Attack", "damage": 22, "class": "Mid", "activeStartCoordinate": 15},
    ]}}

    metrics, evidence = export_webui_data._move_metrics_from_payload(move, payload)

    assert metrics["startup"] == 16
    assert metrics["damage"] == [22]
    assert evidence["startup"] == {
        "source": "khd-static-timeline",
        "status": "native-inferred",
    }


def test_payload_fallback_uses_effective_attack_route_and_preserves_multihit_order():
    move = _fake_export_move(0, "Native string", "A.A")
    move["hitClasses"] = []
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["input-timed-replacement"],
        "definitions": [],
        "slots": [100, 101, 102],
        "cells": [0, 1, 2],
        "attackSlots": [101, 102],
        "attackCells": [1, 2],
    }
    payload = {"khd": {"cells": [
        {"role": "Attack", "damage": 99, "class": "Low"},
        {"role": "Attack", "damage": 12, "class": "Mid"},
        {"role": "Attack", "damage": 8, "class": "High"},
    ]}}

    metrics, evidence = export_webui_data._move_metrics_from_payload(move, payload)

    assert metrics["damage"] == [12, 8]
    assert metrics["hitLevels"] == ["Mid", "High"]
    assert evidence["damage"] == {
        "source": "khd-attack-cell", "status": "native-inferred"
    }


def test_effective_attack_refs_preserve_repeated_contacts():
    slots, cells = export_webui_data._native_attack_refs({
        "attackSlots": [100, 100, 101],
        "attackCells": [7, 7, 8],
    })

    assert slots == [100, 100, 101]
    assert cells == [7, 7, 8]


def test_hit_count_mismatch_keeps_startup_but_suppresses_completed_metrics():
    class FakeCell:
        cell_role = "Attack"
        attack_class = "Mid"
        wI16BaseDamage = 20
        wI16MasterWindowStart = 11

    class FakeSection:
        entries = [FakeCell()]

    class FakeKhd:
        sections = [FakeSection()]

    move = _fake_export_move(0, "Incomplete string", "A.A")
    move["hitClasses"] = ["High", "High"]
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["static-partial-route"],
        "definitions": [],
        "slots": [100],
        "cells": [0],
        "attackSlots": [100],
        "attackCells": [0],
        "startupProof": {
            "attackSlot": 100,
            "effectiveCell": 0,
            "playerImpactFrame": 12,
        },
        "frameProof": {
            "status": "heuristic",
            "advantages": {"block": -4, "hit": 6, "counterHit": 8},
        },
    }

    export_webui_data._annotate_native_hit_sequence(move)
    metrics, evidence = export_webui_data._move_metrics(move, FakeKhd())

    assert move["nativeLink"]["hitSequenceStatus"] == "unresolved"
    assert move["nativeLink"]["frameEndpointStatus"] == "unresolved"
    assert "frameProof" not in move["nativeLink"]
    assert (
        "native-hit-sequence-count-mismatch:game-authored=2;native-cells=1"
        in move["nativeLink"]["resolutions"]
    )
    assert metrics["startup"] == 12
    assert metrics["damage"] == []
    assert metrics["hitLevels"] == ["High", "High"]
    for metric in ("block", "hit", "counterHit"):
        assert metrics[metric] is None
        assert evidence[metric] == {"source": "unknown", "status": "unknown"}


def test_explicit_empty_attack_refs_never_fall_back_to_navigation():
    move = _fake_export_move(0, "Navigation only", "A")
    move["hitClasses"] = []
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["navigation-only"],
        "definitions": [],
        "slots": [100],
        "cells": [0],
        "attackSlots": [],
        "attackCells": [],
        "startupProof": {"effectiveCell": 0, "playerImpactFrame": 10},
    }
    payload = {"khd": {"cells": [
        {"role": "Attack", "damage": 99, "class": "Mid"},
    ]}}

    metrics, evidence = export_webui_data._move_metrics_from_payload(move, payload)

    assert metrics["startup"] is None
    assert metrics["damage"] == []
    assert metrics["hitLevels"] == []
    assert evidence["damage"] == {"source": "unknown", "status": "unknown"}


def test_unresolved_startup_status_suppresses_stale_payload_proof():
    move = _fake_export_move(0, "Held move", "[A]")
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["input-timed-replacement"],
        "definitions": [],
        "slots": [100, 101],
        "cells": [0, 1],
        "attackSlots": [101],
        "attackCells": [1],
        "startupTimingStatus": "unresolved",
        "startupProof": {"effectiveCell": 1, "playerImpactFrame": 16},
    }
    payload = {"khd": {"cells": [
        {"role": "Attack", "damage": 10, "class": "High"},
        {"role": "Attack", "damage": 24, "class": "Mid"},
    ]}}

    metrics, evidence = export_webui_data._move_metrics_from_payload(move, payload)

    assert metrics["startup"] is None
    assert metrics["damage"] == [24]
    assert evidence["startup"] == {"source": "unknown", "status": "unknown"}


def test_comparator_prefers_effective_attack_refs_over_navigation():
    import compare_community_vs_parsed

    records, *_ = compare_community_vs_parsed._build_movelist_index({
        "rows": [
            {
                "name": "Held move",
                "input": "[A]",
                "nativeLink": {
                    "status": "heuristic",
                    "slots": [100, 101],
                    "cells": [0, 1],
                    "attackSlots": [101],
                    "attackCells": [1],
                },
            },
            {
                "name": "Navigation only",
                "input": "[B]",
                "nativeLink": {
                    "status": "heuristic",
                    "slots": [100],
                    "cells": [0],
                    "attackSlots": [],
                    "attackCells": [],
                },
            },
        ],
        "khd": {"cells": [
            {"role": "Attack", "damage": 10},
            {"role": "Attack", "damage": 24},
        ]},
    })

    assert records[0]["slotIdx"] == 101
    assert records[0]["cellIdx"] == 1
    assert records[0]["cell"]["damage"] == 24
    assert records[1]["slotIdx"] == -1
    assert records[1]["cellIdx"] == -1
    assert records[1]["cell"] is None


def test_comparison_legacy_cell_fallback_keeps_raw_coordinate_out_of_startup():
    import compare_community_vs_parsed

    parsed = compare_community_vs_parsed._parsed_metrics({
        "input": "3B",
        "cell": {
            "role": "Attack",
            "damage": 20,
            "activeStartCoordinate": 13,
            "activeEndCoordinate": 15,
            "activeFrames": 3,
            "blockStunFrames": 24,
            "baseHitStunFrames": 38,
        },
    })

    assert parsed["startup"] is None
    assert parsed["activeFrames"] == 3


def test_throw_break_advantage_is_exported_as_block_only():
    move = _fake_export_move(0, "Titan Bomb", "236A+G", cell_idx=292, slot_idx=482)
    move["isThrowInput"] = True
    move["effectTags"] = [{"code": "TH", "label": "Throw"}]
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["native-throw-break-endpoints:confirmed;route-link=inferred"],
        "definitions": [],
        "slots": [482],
        "cells": [292],
        "throwBreakProof": {
            "status": "heuristic",
            "advantage": -7,
            "defenderBreakStunFrames": 30,
        },
    }

    metrics, evidence = export_webui_data._move_metrics(move, None)

    assert metrics["startup"] is None
    assert metrics["damage"] == []
    assert metrics["block"] == -7
    assert metrics["hit"] is None
    assert metrics["counterHit"] is None
    assert evidence["block"] == {
        "source": "khd-static-timeline",
        "status": "native-inferred",
    }


def test_throw_marker_requires_native_non_damaging_attempt_cell():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    from luxformats import parse_auto

    khd = parse_auto(str(path))
    throw = _fake_export_move(0, "Titan Bomb", "236A+G", cell_idx=292, slot_idx=482)
    throw["effectTags"] = [{"code": "TH", "label": "Throw"}]
    throw["nativeLink"] = {"status": "heuristic", "slots": [482], "cells": [292]}
    strike = _fake_export_move(1, "Hades Control", "6A", cell_idx=71, slot_idx=310)
    strike["effectTags"] = [{"code": "TH", "label": "Throw"}]
    strike["nativeLink"] = {"status": "heuristic", "slots": [310], "cells": [71]}

    assert export_webui_data._is_native_throw_attempt(throw, khd) is True
    assert export_webui_data._is_native_throw_attempt(strike, khd) is False


def test_main_index_link_is_navigation_evidence_not_confirmed_combat_link():
    move = _fake_export_move(0, "Native row", "3B", cell_idx=22, slot_idx=260)
    move["commandSets"][0]["resolution"] = "movevm-main-definition-navigation"
    assert export_webui_data._native_link(move) == {
        "status": "heuristic",
        "resolutions": ["movevm-main-definition-navigation"],
        "definitions": [{
            "lane": "primary-fighter",
            "mainDefinitionId": 1,
            "fallbackDefinitionId": 0,
        }],
        "slots": [],
        "cells": [],
    }


def test_confirmed_native_route_exports_audited_frame_advantage():
    class FakeCell:
        def __init__(self, attack_class, damage, active_start):
            self.cell_role = "Attack"
            self.attack_class = attack_class
            self.wI16BaseDamage = damage
            self.wI16MasterWindowStart = active_start

    class FakeSection:
        entries = [FakeCell("Mid", 20, 19), FakeCell("High", 14, 18)]

    class FakeKhd:
        sections = [FakeSection()]

    move = _fake_export_move(75, "Bear Tamer", "B.6A")
    move["hitClasses"] = ["Mid", "High"]
    move["nativeLink"] = {
        "status": "confirmed",
        "resolutions": ["khd-static-route"],
        "definitions": [],
        "slots": [308, 310],
        "cells": [0, 1],
        "startupProof": {
            "attackSlot": 308,
            "effectiveCell": 0,
            "playerImpactFrame": 20,
        },
        "frameProof": {
            "status": "confirmed",
            "advantages": {"block": -8, "hit": 2, "counterHit": 2},
        },
    }

    metrics, evidence = export_webui_data._move_metrics(move, FakeKhd())

    assert metrics["startup"] == 20
    assert metrics["damage"] == [20, 14]
    assert metrics["block"] == -8
    assert metrics["hit"] == 2
    assert metrics["counterHit"] == 2
    assert evidence["startup"] == {
        "source": "khd-static-timeline",
        "status": "native-confirmed",
    }
    assert evidence["damage"] == {
        "source": "khd-attack-cell",
        "status": "native-confirmed",
    }
    assert evidence["block"] == {
        "source": "khd-static-timeline",
        "status": "native-confirmed",
    }
    assert evidence["hit"] == evidence["block"]
    assert evidence["counterHit"] == evidence["block"]


def test_heuristic_native_route_exports_inferred_advantage_with_proven_endpoints():
    class FakeCell:
        cell_role = "Attack"
        attack_class = "Mid"
        wI16BaseDamage = 20
        wI16MasterWindowStart = 19

    class FakeSection:
        entries = [FakeCell()]

    class FakeKhd:
        sections = [FakeSection()]

    move = _fake_export_move(74, "Bear Tamer", "B.A")
    move["nativeLink"] = {
        "status": "heuristic",
        "resolutions": ["khd-selector:packed0x304E;tick=31"],
        "definitions": [],
        "slots": [308],
        "cells": [0],
        "startupProof": {
            "attackSlot": 308,
            "effectiveCell": 0,
            "playerImpactFrame": 20,
        },
        "frameProof": {
            "status": "heuristic",
            "advantages": {"block": -8, "hit": 2, "counterHit": None},
        },
    }

    metrics, evidence = export_webui_data._move_metrics(move, FakeKhd())

    assert metrics["startup"] == 20
    assert metrics["damage"] == [20]
    assert metrics["block"] == -8
    assert metrics["hit"] == 2
    assert metrics["counterHit"] is None
    assert evidence["startup"]["status"] == "native-inferred"
    assert evidence["block"] == {
        "source": "khd-static-timeline",
        "status": "native-inferred",
    }
    assert evidence["counterHit"] == {"source": "unknown", "status": "unknown"}


def _fake_full_payload(cid: str, name: str) -> dict:
    move = _fake_export_move(0, "Fast Slice", "AA", cell_idx=0, slot_idx=10)
    family_row = {
        "id": f"{cid}-row-aa",
        "displayCommand": "AA",
        "displayName": "Fast Slice",
        "context": "Neutral",
        "source": "game-movelist-table",
        "confidence": "native-inferred",
        "parserMoveOrders": [0],
        "nativeLink": {
            "status": "confirmed",
            "resolutions": ["movevm-main-definition-confirmed"],
            "definitions": [{"lane": "primary-fighter", "mainDefinitionId": 1, "fallbackDefinitionId": 0}],
            "slots": [],
            "cells": [],
        },
        "metrics": {
            "startup": 10,
            "damage": [8, 8],
            "block": None,
            "hit": None,
            "counterHit": None,
            "guardBurst": None,
            "hitLevels": ["High", "High"],
        },
        "evidence": {
            "startup": {"source": "khd-attack-cell", "status": "native-inferred"},
            "damage": {"source": "khd-attack-cell", "status": "native-inferred"},
            "block": {"source": "unknown", "status": "unknown"},
            "hit": {"source": "unknown", "status": "unknown"},
            "counterHit": {"source": "unknown", "status": "unknown"},
            "guardBurst": {"source": "unknown", "status": "unknown"},
            "hitLevels": {"source": "game-movelist-table", "status": "game-authored"},
        },
        "notes": "quick poke",
    }
    move["nativeLink"] = family_row["nativeLink"]
    move["metrics"] = family_row["metrics"]
    move["evidence"] = family_row["evidence"]
    return {
        "cid": cid,
        "name": name,
        "kind": "base",
        "files": {"khd": True, "mot": False, "dtp": False, "atkhit": False, "bodyhit": False, "yararehit": False},
        "khd": {
            "moveCount": 300,
            "attackCount": 120,
            "slotCount": 500,
            "totalCells": 200,
            "cells": [{
                "idx": 0,
                "role": "Attack",
                "class": "High",
                "damage": 16,
                "activeStartCoordinate": 10,
                "activeEndCoordinate": 12,
                "blockStunFrames": 24,
                "baseHitStunFrames": 34,
            }],
            "slots": [{"idx": 10}],
            "slotEdges": [{"src": 1, "dst": 2}],
            "eventRecords": [{"idx": 0}],
        },
        "movelist": {
            "ryuuhaType": 0,
            "categories": [{"index": 0, "name": "Horizontal", "itemOrders": [0]}],
            "moves": [move],
            "moveGroups": [],
            "playerMoveFamilies": [{
                "id": f"player-family-{cid}-aa",
                "cid": cid,
                "kind": "single-row",
                "rootCommand": "AA",
                "rootName": "Fast Slice",
                "context": "Neutral",
                "confidence": "native-inferred",
                "relations": [],
                "rows": [family_row],
                "edges": [],
            }],
            "playerMoveSummary": {
                "officialRows": 1,
                "playerFamilies": 1,
                "playerRows": 1,
                "nativeLinkedRows": 1,
                "nativeUnlinkedRows": 0,
                "linkStatusCounts": {"heuristic": 1},
                "groupingConfidenceCounts": {"native-inferred": 1},
                "metricCoverage": {"startup": 1, "damage": 1, "hitLevels": 1},
            },
        },
    }


def test_v2_player_payload_keeps_families_but_drops_heavy_khd_arrays():
    player = export_webui_data.build_v2_player_payload(_fake_full_payload("003", "Taki"))

    assert player["cid"] == "003"
    assert player["playerMoveFamilies"][0]["rootCommand"] == "AA"
    assert player["nativeSummary"]["attackCount"] == 120
    assert "khd" not in player
    encoded = json.dumps(player)
    for heavy_key in ('"slotEdges"', '"eventRecords"'):
        assert heavy_key not in encoded


def test_v2_raw_movelist_payload_contains_compact_rows_with_native_metrics():
    raw = export_webui_data.build_v2_raw_movelist_payload(_fake_full_payload("003", "Taki"))

    assert raw["cid"] == "003"
    assert raw["schemaVersion"] == 2
    assert raw["categories"][0]["name"] == "Horizontal"
    assert raw["rows"][0]["nativeLink"]["definitions"][0]["mainDefinitionId"] == 1
    assert raw["rows"][0]["nativeLink"]["slots"] == []
    assert raw["rows"][0]["nativeLink"]["cells"] == []
    assert raw["rows"][0]["metrics"]["startup"] == 10
    assert raw["rows"][0]["metrics"]["block"] is None
    encoded = json.dumps(raw)
    assert '"commandSets"' not in encoded
    assert '"slotEdges"' not in encoded


def test_v2_lookup_index_contains_searchable_family_summaries_for_key_chars():
    payloads = [
        export_webui_data.build_v2_player_payload(_fake_full_payload("003", "Taki")),
        export_webui_data.build_v2_player_payload(_fake_full_payload("060", "2B")),
        export_webui_data.build_v2_player_payload(_fake_full_payload("016", "Talim")),
        export_webui_data.build_v2_player_payload(_fake_full_payload("009", "Hwang")),
    ]

    index = export_webui_data.build_v2_lookup_index(payloads)

    assert index["schemaVersion"] == 2
    names = {char["name"] for char in index["chars"]}
    assert {"Taki", "2B", "Talim", "Hwang"} <= names
    assert len(index["families"]) == 4
    taki_family = next(item for item in index["families"] if item["charName"] == "Taki")
    assert taki_family["commandKeys"] == ["AA"]
    assert "taki" in taki_family["searchText"]
    assert "quick poke" in taki_family["searchText"]


def test_schema_v2_payloads_contain_no_external_sheet_values():
    full = _fake_full_payload("003", "Taki")
    outputs = [
        export_webui_data.build_v2_player_payload(full),
        export_webui_data.build_v2_raw_movelist_payload(full),
        export_webui_data.build_v2_lookup_index([export_webui_data.build_v2_player_payload(full)]),
    ]
    encoded = json.dumps(outputs)
    for forbidden in ("communityFrame", '"community"', "docs.google.com/spreadsheets"):
        assert forbidden not in encoded


def test_export_main_out_dir_alias_writes_to_alternate_directory(tmp_path, monkeypatch):
    out_dir = tmp_path / "out"

    def fake_export_char(cid: str, paths: dict[str, str], out_path: str) -> None:
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        Path(out_path).write_text(json.dumps({"cid": cid, "name": "Mitsurugi"}), encoding="utf-8")

    monkeypatch.setattr(export_webui_data, "discover_chars", lambda root: {"001": {"khd": "fake"}})
    # This test covers only the --out-dir alias.  Root-coherence has its own
    # contract tests and cannot be meaningfully evaluated against a synthetic
    # directory with mocked discovery/export.
    monkeypatch.setattr(export_webui_data, "_require_coherent_content_roots", lambda root: None)
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
    if data.get("schemaVersion") != 2:
        pytest.skip("generated data predates schema v2; run the exporter")
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
    for f in ("idx", "role", "class", "damage", "activeStartCoordinate", "baseHitStunFrames",
              "blockStunFrames", "rangeStandMin", "rangeStandMax", "inputCond"):
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
    if data.get("schemaVersion") != 2:
        pytest.skip("generated data predates schema v2; run the exporter")
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

    # Every move preserves the authored lane order and exposes definition IDs
    # without reinterpreting them as KHD cells or slots.
    for m in ml["moves"]:
        cs = m["commandSets"][0]
        for f in ("mainIndex", "introIndex", "cellIdx", "slotIdx", "resolution"):
            assert f in cs, f"missing {f!r} in move {m['name']!r}"
        assert "tracking" in cs, f"missing tracking in move {m['name']!r}"
        for f in ("hasFacingCommit", "hasRetrackRamp", "maxTargetWeight", "events"):
            assert f in cs["tracking"], f"missing tracking.{f!r} in move {m['name']!r}"
        assert "nativeLink" in m, f"missing nativeLink in move {m['name']!r}"
        assert "metrics" in m and "evidence" in m
        for metric in ("block", "hit", "counterHit"):
            value = m["metrics"][metric]
            status = m["evidence"][metric]["status"]
            if value is None:
                assert status == "unknown"
            else:
                assert status in {"native-confirmed", "native-inferred"}
                assert isinstance(value, int) and not isinstance(value, bool)
        assert "isRevengeAttack" in m, f"missing isRevengeAttack in move {m['name']!r}"
        assert "groupIds" in m, f"missing groupIds in move {m['name']!r}"
        assert cs["lane"] == "primary-fighter"
        assert cs["cellIdx"] == -1
        assert cs["slotIdx"] == -1
        assert m["nativeLink"]["definitions"][0]["lane"] == "primary-fighter"
        assert cs["resolution"] != "none", f"unresolved CommandSet in {m['name']!r}"

    # A MoveVM definition ID is not itself evidence for an attack cell.
    with_cell = sum(1 for m in ml["moves"] if m["commandSets"][0]["cellIdx"] >= 0)
    assert with_cell == 0


def test_astaroth_bear_tamer_v2_export_has_audited_native_route():
    """Regression for the B.6A family that exposed the MainIndex mix-up."""
    path = DATA_DIR / "v2" / "chars" / "012" / "player.json"
    if not path.exists():
        pytest.skip("Astaroth schema-v2 player data not generated yet")
    player = json.loads(path.read_text(encoding="utf-8"))
    family = next(
        family for family in player["playerMoveFamilies"]
        if any(
            row["displayName"] == "Bear Tamer" and row["displayCommand"] == "B.6A"
            for row in family["rows"]
        )
    )
    row = next(
        row for row in family["rows"]
        if row["displayName"] == "Bear Tamer" and row["displayCommand"] == "B.6A"
    )

    assert row["displayCommand"] == "B.6A"
    assert row["displayName"] == "Bear Tamer"
    assert row["nativeLink"]["status"] == "confirmed"
    assert row["nativeLink"]["slots"] == [308, 310]
    assert row["nativeLink"]["cells"] == [67, 71]
    assert row["metrics"]["startup"] == 20
    assert row["metrics"]["damage"] == [20, 14]
    assert row["metrics"]["hitLevels"] == ["Mid", "High"]
    assert row["nativeLink"]["frameProof"] == {
        "status": "confirmed",
        "attackSlot": 310,
        "attackCell": 71,
        "totalFrames": 69,
        "cellWindowStartCoordinate": 18,
        "cellWindowEndCoordinate": 20,
        "recoveryLead": 18,
        "recoveryOpenCoordinate": 50,
        "inclusiveRecoveryFrames": 33,
        "defenderStunFrames": {"block": 25, "hit": 35, "counterHit": 35},
        "reactionRoutes": {
            "hit": {
                "reactionRowId": 272,
                "rawMoveIds": [8539, 8537, 8540, 8538, 8539, 8537, 8540, 8538],
                "packedMoveIds": [8539, 8537, 8540, 8538, 8539, 8537, 8540, 8538],
                "resolvedSlots": [987, 985, 988, 986, 987, 985, 988, 986],
                "driverMoveIds": [12603] * 8,
            },
            "counterHit": {
                "reactionRowId": 272,
                "rawMoveIds": [8539, 8537, 8540, 8538, 8539, 8537, 8540, 8538],
                "packedMoveIds": [8539, 8537, 8540, 8538, 8539, 8537, 8540, 8538],
                "resolvedSlots": [987, 985, 988, 986, 987, 985, 988, 986],
                "driverMoveIds": [12603] * 8,
            },
        },
        "advantages": {"block": -8, "hit": 2, "counterHit": 2},
    }
    assert row["metrics"]["block"] == -8
    assert row["metrics"]["hit"] == 2
    assert row["metrics"]["counterHit"] == 2
    assert row["evidence"]["startup"] == {
        "source": "khd-static-timeline",
        "status": "native-confirmed",
    }
    assert row["evidence"]["damage"] == {
        "source": "khd-attack-cell",
        "status": "native-confirmed",
    }
    assert row["evidence"]["block"] == {
        "source": "khd-static-timeline",
        "status": "native-confirmed",
    }
    assert row["evidence"]["hit"] == row["evidence"]["block"]
    assert row["evidence"]["counterHit"] == row["evidence"]["block"]


def test_astaroth_held_breath_of_hades_uses_replacement_cell_and_unknown_recovery():
    path = DATA_DIR / "v2" / "chars" / "012" / "player.json"
    if not path.exists():
        pytest.skip("Astaroth schema-v2 player data not generated yet")
    player = json.loads(path.read_text(encoding="utf-8"))
    family = next(
        family for family in player["playerMoveFamilies"]
        if family["id"] == "player-family-012-official-00120"
    )
    pressed = next(row for row in family["rows"] if row["displayCommand"] == "4A+B")
    held = next(row for row in family["rows"] if row["displayCommand"] == "4[A]+[B]")

    assert pressed["nativeLink"]["attackSlots"] == [377]
    assert pressed["nativeLink"]["attackCells"] == [153]
    assert pressed["metrics"]["hit"] == -15
    assert pressed["metrics"]["counterHit"] == -15

    assert held["nativeLink"]["slots"] == [377, 378]
    assert held["nativeLink"]["cells"] == [153, 154]
    assert held["nativeLink"]["attackSlots"] == [378]
    assert held["nativeLink"]["attackCells"] == [154]
    assert held["nativeLink"]["startupTimingStatus"] == "unresolved"
    assert held["nativeLink"]["frameEndpointStatus"] == "unresolved"
    assert "startupProof" not in held["nativeLink"]
    assert "frameProof" not in held["nativeLink"]
    assert held["metrics"]["damage"] == [26]
    for metric in ("startup", "block", "hit", "counterHit"):
        assert held["metrics"][metric] is None
        assert held["evidence"][metric] == {
            "source": "unknown",
            "status": "unknown",
        }


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
