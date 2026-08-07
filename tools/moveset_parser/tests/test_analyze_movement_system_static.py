from __future__ import annotations

from pathlib import Path

import pytest

from analyze_movement_system_static import BackstepQuality, _assign_backstep_scores, analyze


pytestmark = pytest.mark.needs_dump


def test_static_movement_analyzer_finds_core_candidates():
    battle_root = Path("E:/myMods/dump/Battle")
    result = analyze(
        battle_root,
        Path("C:/Users/prest/Documents/SoulcaliburModding/SCVI Sound Tools/dump/Battle"),
        copy_missing=False,
    )

    summary = result["summary"]
    assert summary["static_totals"]["character_count_with_khd"] >= 6
    assert summary["static_totals"]["direction_transition_count"] > 0
    assert summary["static_totals"]["candidate_count"] > 0
    assert summary["static_totals"]["cross_bank_direction_edge_count"] > 0
    assert len(result["cross_bank_direction_edges"]) == len(result["cross_bank_route_resolution"])
    assert len(result["recovery_trust_audit"]) >= len(result["basic_movement_routes"])
    assert not summary["errors"]
    assert "backstep_quality_summary" in summary

    mitsurugi = next(c for c in summary["characters"] if c["cid"] == "001")
    best = mitsurugi["best_direction_candidates"]
    assert best["backstep_candidate"][0]["confidence"].startswith("inferred_static")
    assert best["sidestep_down_candidate"][0]["confidence"].startswith("inferred_static")
    assert best["forward_step_candidate"][0]["confidence"].startswith("inferred_static")

    unknowns = result["unknowns"]["do_not_claim"]
    assert any("MOT section size" in line for line in unknowns)

    backsteps = result["backstep_quality"]["characters"]
    assert len(backsteps) == summary["static_totals"]["character_count_with_khd"]
    assert all(row["quality_score"] is None or row["root_decode_confidence"] == "high" for row in backsteps)
    assert all(row["quality_grade"] != "" for row in backsteps)
    assert result["backstep_quality"]["ranked_count"] == summary["backstep_quality_summary"]["ranked_count"]

    basic_routes = result["basic_movement_routes"]
    route_evidence = result["route_trust_evidence"]
    assert basic_routes
    assert route_evidence

    movement_types = {row["movement_type"] for row in basic_routes}
    assert movement_types == {
        "back_diagonal",
        "backstep_candidate",
        "eight_way_or_ambiguous",
        "forward_diagonal",
        "forward_step_candidate",
        "sidestep_down_candidate",
        "sidestep_up_candidate",
    }
    assert len(basic_routes) == summary["static_totals"]["character_count_with_khd"] * len(movement_types)
    assert any(row["trust_status"] == "trusted_basic" for row in basic_routes)
    assert any(row["trust_status"] == "unresolved" for row in basic_routes)

    hilde_backstep = next(
        row
        for row in basic_routes
        if row["character"] == "Hilde" and row["movement_type"] == "backstep_candidate"
    )
    assert hilde_backstep["trust_status"] == "trusted_basic"
    assert "direct movement predicate" in hilde_backstep["trust_reason"]
    assert hilde_backstep["src_slot"] == 2670
    assert hilde_backstep["dst_slot"] == 473

    quality_by_character = {
        row["character"]: row
        for row in result["backstep_quality"]["characters"]
    }
    for route in basic_routes:
        if route["movement_type"] != "backstep_candidate":
            continue
        quality = quality_by_character[route["character"]]
        assert quality["route_kind"] == route["trust_status"]
        assert quality["src_slot"] == route["src_slot"]
        assert quality["dst_slot"] == route["dst_slot"]
        assert quality["animation_hex"] == route["animation_hex"]
        assert quality["quality_score"] is None or route["trust_status"] in {
            "trusted_basic",
            "trusted_stance_basic",
        }

    audit_by_key = {
        (row["character"], row["movement_type"]): row
        for row in result["movement_route_audit"]
    }
    for route in basic_routes:
        key = (route["character"], route["movement_type"])
        if route["trust_status"] == "unresolved" and route["src_slot"] is None:
            audit = audit_by_key[key]
            assert audit["route_kind"] == route["trust_status"]
            assert audit["src_slot"] is None
            assert audit["dst_slot"] is None
            continue
        audit = audit_by_key[key]
        assert audit["route_kind"] == route["trust_status"]
        assert audit["src_slot"] == route["src_slot"]
        assert audit["dst_slot"] == route["dst_slot"]
        assert audit["animation_hex"] == route["animation_hex"]

    assert len(result["movement_route_audit"]) == len(basic_routes)
    assert len(result["movement_route_audit"]) == len(result["movement_quality"]["characters"])
    assert all(
        row["quality_score"] is None or row["route_kind"] in {"trusted_basic", "trusted_stance_basic"}
        for row in result["movement_quality"]["characters"]
    )
    assert all(
        row["quality_score"] is None or row["root_decode_confidence"] == "high"
        for row in result["movement_quality"]["characters"]
    )
    assert all(
        row["quality_score"] is None
        for row in result["movement_quality"]["characters"]
        if row["route_kind"] == "trusted_basic_with_late_followup"
    )
    assert all(
        row["route_resolution_status"] or row["trust_status"] == "unresolved"
        for row in basic_routes
    )


def test_backstep_scoring_prefers_early_distance_and_fast_recovery():
    def row(name: str, early: float, total: float, recovery: int) -> BackstepQuality:
        return BackstepQuality(
            cid=name,
            character=name,
            route_kind="canonical_basic",
            quality_status="rankable_static_distance",
            quality_score=None,
            quality_grade="Unranked",
            total_back_distance=total,
            back_distance_f4=early,
            back_distance_f8=early,
            back_distance_f12=early,
            back_distance_f16=early,
            first_frame_8_units=recovery,
            first_frame_16_units=recovery + 2,
            first_frame_30_units=recovery + 4,
            recovery_estimate_status="estimated_from_static_bytecode",
            earliest_guard_or_neutral_frame=recovery,
            earliest_attack_cancel_frame=None,
            plain_movement=True,
            has_attack_cell=False,
            root_decode_confidence="high",
            src_slot=1,
            dst_slot=2,
            animation_hex="0001",
            selection_reason="test",
            reason="test",
        )

    slow_short = row("slow_short", early=2.0, total=10.0, recovery=30)
    fast_long = row("fast_long", early=8.0, total=20.0, recovery=12)
    rows = [slow_short, fast_long]

    _assign_backstep_scores(rows)

    assert fast_long.quality_score is not None
    assert slow_short.quality_score is not None
    assert fast_long.quality_score > slow_short.quality_score
