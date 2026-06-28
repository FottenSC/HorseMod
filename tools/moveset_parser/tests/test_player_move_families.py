from __future__ import annotations

from player_move_families import (
    build_community_families,
    summarize_community_families,
    tokenize_command,
)


def _move(
    command: str,
    name: str,
    *,
    stance: str = "",
    damage: list[int] | None = None,
    hit_levels: list[str] | None = None,
    category: str = "Horizontal Attacks",
) -> dict:
    return {
        "command": command,
        "name": name,
        "stance": stance,
        "category": category,
        "damage": damage or [],
        "hitLevels": hit_levels or [],
    }


def _family_containing(families: list[dict], command: str) -> dict:
    for family in families:
        if any(row["command"] == command for row in family["rows"]):
            return family
    raise AssertionError(f"no family contains command {command!r}")


def _commands(family: dict) -> set[str]:
    return {row["command"] for row in family["rows"]}


def _edge_kinds(family: dict) -> set[str]:
    return {edge["relation"] for edge in family["edges"]}


def test_tokenizer_keeps_chords_and_hold_skeletons_distinct():
    assert [t.text for t in tokenize_command("236A+B+K")] == ["236", "A+B+K"]
    assert [t.skeleton for t in tokenize_command("4(A)AAA")] == ["4", "A", "A", "A", "A"]
    assert [t.text for t in tokenize_command("(3)(6)(9)K")] == ["(3)", "(6)", "(9)", "K"]
    assert [t.skeleton for t in tokenize_command("(B)+(G)")] == ["B+G"]
    assert [t.skeleton for t in tokenize_command("2(B)+(K)")] == ["2", "B+K"]


def test_taki_a_string_groups_prefix_branches_but_not_chord():
    families = build_community_families("003", [
        _move("A", "Barbed Blades", damage=[8], hit_levels=["High"]),
        _move("AA", "Barbed Blades", damage=[8, 8], hit_levels=["High", "High"]),
        _move("AAA", "Barbed Blades", damage=[8, 8, 10], hit_levels=["High", "High", "Mid"]),
        _move("AAB", "Barbed Blades", damage=[8, 8, 16], hit_levels=["High", "High", "Mid"]),
        _move("AAK", "Barbed Blades", damage=[8, 8, 14], hit_levels=["High", "High", "High"]),
        _move("A+B", "Stalker Drop", damage=[20], hit_levels=["Mid"]),
    ])

    family = _family_containing(families, "A")
    assert {"A", "AA", "AAA", "AAB", "AAK"}.issubset(_commands(family))
    assert "A+B" not in _commands(family)
    assert "prefix" in _edge_kinds(family)


def test_sophitia_hold_variants_link_without_crossing_soul_charge_context():
    families = build_community_families("006", [
        _move("4A", "Twin Step Grace", damage=[12], hit_levels=["High"]),
        _move("4(A)", "Twin Step Grace", damage=[12], hit_levels=["High"]),
        _move("4AA", "Twin Step Grace", damage=[12, 18], hit_levels=["High", "Mid"]),
        _move("4(A)B", "Twin Step Grace", damage=[12, 20], hit_levels=["High", "Mid"]),
        _move("4A", "Twin Step Grace", stance="SC", damage=[16], hit_levels=["High"]),
    ])

    neutral = _family_containing(
        [family for family in families if family["context"] == "Neutral"],
        "4A",
    )
    assert {"4A", "4(A)", "4AA", "4(A)B"}.issubset(_commands(neutral))
    assert {"prefix", "hold-variant"}.issubset(_edge_kinds(neutral))

    soul_charge = _family_containing(
        [family for family in families if family["context"] == "SC"],
        "4A",
    )
    assert _commands(soul_charge) == {"4A"}


def test_siegfried_hold_and_branch_rows_share_one_neutral_string():
    families = build_community_families("007", [
        _move("A", "Progressive Step", damage=[16], hit_levels=["High"]),
        _move("(A)", "Progressive Step", damage=[16], hit_levels=["High"]),
        _move("AA", "Progressive Step", damage=[16, 14], hit_levels=["High", "High"]),
        _move("AAA", "Progressive Step", damage=[16, 14, 30], hit_levels=["High", "High", "Mid"]),
        _move("AAB", "Progressive Step", damage=[16, 14, 20], hit_levels=["High", "High", "Mid"]),
        _move("AA(B)", "Progressive Step", damage=[16, 14, 30], hit_levels=["High", "High", "Mid"]),
        _move("AA4", "Progressive Step", damage=[16, 14], hit_levels=["High", "High"]),
        _move("A", "Chief Hold Swing", stance="SCH", damage=[22], hit_levels=["Mid"]),
    ])

    neutral = _family_containing(
        [family for family in families if family["context"] == "Neutral"],
        "A",
    )
    assert {"A", "(A)", "AA", "AAA", "AAB", "AA(B)", "AA4"}.issubset(_commands(neutral))
    assert "SCH" not in {neutral["context"]}
    assert {"prefix", "hold-variant"}.issubset(_edge_kinds(neutral))

    chief_hold = _family_containing(
        [family for family in families if family["context"] == "SCH"],
        "A",
    )
    assert _commands(chief_hold) == {"A"}


def test_mitsurugi_direction_prefix_does_not_merge_unrelated_root_directions():
    families = build_community_families("001", [
        _move("4A", "Drawn Breath", damage=[20], hit_levels=["High"]),
        _move("6A", "Double Binder", damage=[16], hit_levels=["High"]),
        _move("6AA", "Double Binder", damage=[16, 18], hit_levels=["High", "Mid"]),
        _move("6AB", "Double Binder", damage=[16, 22], hit_levels=["High", "Mid"]),
        _move("6A+B", "Relic Break", damage=[30], hit_levels=["Mid"]),
    ])

    drawn_breath = _family_containing(families, "4A")
    double_binder = _family_containing(families, "6A")
    assert _commands(drawn_breath) == {"4A"}
    assert {"6A", "6AA", "6AB"}.issubset(_commands(double_binder))
    assert "4A" not in _commands(double_binder)
    assert "6A+B" not in _commands(double_binder)


def test_maxi_stance_transition_suffix_is_an_explicit_edge():
    families = build_community_families("004", [
        _move("236A+B+K", "Seven Stars Rebirth", damage=[30], hit_levels=["Mid"]),
        _move("236A+B+K", "Seven Stars Rebirth ~ Right Cross", damage=[30], hit_levels=["Mid"]),
        _move("236A+B+K", "Seven Stars Rebirth ~ Left Outer", damage=[30], hit_levels=["Mid"]),
    ])

    family = _family_containing(families, "236A+B+K")
    assert _commands(family) == {"236A+B+K"}
    assert len(family["rows"]) == 3
    assert "stance-transition" in _edge_kinds(family)


def test_2b_slash_sequence_prefix_tree_covers_direction_branches():
    families = build_community_families("060", [
        _move("A", "Slash Sequence", damage=[8], hit_levels=["High"]),
        _move("AA", "Slash Sequence", damage=[8, 8], hit_levels=["High", "High"]),
        _move("AAA", "Slash Sequence", damage=[8, 8, 10], hit_levels=["High", "High", "Mid"]),
        _move("AAA6", "Slash Sequence", damage=[8, 8, 10], hit_levels=["High", "High", "Mid"]),
        _move("AAA4", "Slash Sequence", damage=[8, 8, 10], hit_levels=["High", "High", "Mid"]),
        _move("AAA8", "Slash Sequence", damage=[8, 8, 10], hit_levels=["High", "High", "Mid"]),
        _move("AAAA", "Slash Sequence", damage=[8, 8, 10, 12], hit_levels=["High", "High", "Mid", "Mid"]),
        _move("AAAB", "Slash Sequence", damage=[8, 8, 10, 14], hit_levels=["High", "High", "Mid", "Mid"]),
        _move("AAB", "Slash Sequence", damage=[8, 8, 16], hit_levels=["High", "High", "Mid"]),
        _move("7A", "Virtuous Leap", damage=[18], hit_levels=["Mid"]),
        _move("8A", "Other Jump", damage=[18], hit_levels=["Mid"]),
    ])

    family = _family_containing(families, "A")
    assert {
        "A", "AA", "AAA", "AAA6", "AAA4", "AAA8", "AAAA", "AAAB", "AAB",
    }.issubset(_commands(family))
    assert "7A" not in _commands(family)
    assert "8A" not in _commands(family)
    assert {"prefix", "direction-alternative"}.issubset(_edge_kinds(family))


def test_family_schema_has_stable_edges_with_reasons_and_confidence():
    families = build_community_families("003", [
        _move("7A", "Wind Roll Slash", damage=[18], hit_levels=["Mid"]),
        _move("8A", "Wind Roll Slash", damage=[18], hit_levels=["Mid"]),
        _move("9A", "Wind Roll Slash", damage=[18], hit_levels=["Mid"]),
        _move("7B", "Wind Roll Slash", damage=[20], hit_levels=["Mid"]),
    ])

    family = _family_containing(families, "7A")
    assert {"7A", "8A", "9A"}.issubset(_commands(family))
    assert "7B" not in _commands(family)
    assert "direction-alternative" in _edge_kinds(family)
    for edge in family["edges"]:
        assert edge["id"].startswith("edge-community-003-")
        assert edge["parentRowId"].startswith("community-003-")
        assert edge["childRowId"].startswith("community-003-")
        assert edge["confidence"] in {"medium", "strong"}
        assert edge["reasons"]


def test_summary_report_includes_reproducible_per_character_counts():
    community = {
        "chars": {
            "003": {
                "communityName": "taki",
                "moves": [
                    _move("A", "Barbed Blades", damage=[8], hit_levels=["High"]),
                    _move("AA", "Barbed Blades", damage=[8, 8], hit_levels=["High", "High"]),
                    _move("A+B", "Stalker Drop", damage=[20], hit_levels=["Mid"]),
                ],
            }
        }
    }

    report = summarize_community_families(community, parsed_data_dir=None)

    assert report["totals"]["communityRows"] == 3
    assert report["totals"]["multiRowFamilies"] == 1
    assert report["perCharacter"]["003"]["communityName"] == "taki"
    assert report["perCharacter"]["003"]["nonDirectRowsAttachableThroughAnchoredFamily"] == 0
