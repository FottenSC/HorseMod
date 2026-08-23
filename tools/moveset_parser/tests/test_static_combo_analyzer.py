from __future__ import annotations

from dataclasses import asdict
from pathlib import Path
from types import SimpleNamespace

import pytest

from static_combo_analyzer import (
    ASTAROTH_LEFT_UKEMI,
    ComboScenario,
    WorldSphere,
    build_pose_solver_state,
    build_reaction_timeline,
    decode_reaction_move_velocity,
    render_markdown_report,
    prove_training_route_equivalence,
    sphere_event_modifier,
    summarize_reaction_partition,
    strict_sphere_contact,
    _resolve_attack_recovery_entry,
    _resolve_opener_continuation,
)
from luxformats import parse_khd


def test_strict_overlap_rejects_exact_tangent():
    attack = WorldSphere(0, 5, (0.0, 0.0, 0.0), 0.25)
    tangent = WorldSphere(1, 3, (0.5, 0.0, 0.0), 0.25)
    inside = WorldSphere(1, 3, (0.499, 0.0, 0.0), 0.25)
    assert strict_sphere_contact(attack, tangent).overlaps is False
    assert strict_sphere_contact(attack, inside).overlaps is True


@pytest.mark.parametrize(
    "lane_length",
    (60, 70),
)
def test_row_1037_timeline_uses_native_post_lane_counter_order(
    lane_length,
):
    timeline = build_reaction_timeline(lane_length)
    assert timeline.reaction_terminal_coordinate == lane_length
    # Equality remains an active lane sample; the same-invocation cache reset
    # after that sample remains an explicit unresolved boundary.
    assert timeline.reaction_terminal_tick is None
    assert timeline.first_input_recognition_tick == 53
    assert timeline.grounded_dispatch_tick == 53
    assert timeline.ukemi_queue_tick == 53
    assert timeline.ukemi_commit_tick == 53
    assert timeline.ukemi_initial_coordinate == 0.0
    assert timeline.ukemi_blend_duration == 0.0


def test_pose_state_keeps_fifth_physical_record_out_of_main_blend_lanes():
    slot = SimpleNamespace(
        wAnimationIndex_00=0x149A,
        wMotionBId_10=0xFFFF,
        bMotionATrack_06=0,
        flMotionAWeightHundredths_08=100.0,
        bMotionBTrack_16=0,
        flMotionBWeightHundredths_18=0.0,
        bMotionAFlags_07=0,
    )
    state = build_pose_solver_state(
        tick=53,
        ukemi_slot=slot,
        ukemi_coordinate=0.0,
        reaction_packed_motion=0x149A,
        reaction_coordinate=53.0,
        reaction_lane_last_tick=60,
    )
    assert tuple(lane.solver_slot for lane in state.lanes) == (0, 1, 2, 3)
    assert tuple(record.solver_slot for record in state.auxiliary_records) == (4,)


def test_sphere_event_modifier_uses_first_matching_slot_mask():
    events = [
        SimpleNamespace(
            dwPackedMoveId=342, dwEventKind=1, dwField08=0,
            dwShapeFlags=1 << (40 - 32), flRadiusScale=1.5,
            flOffsetX=1.0, flOffsetY=2.0, flOffsetZ=3.0,
        ),
        SimpleNamespace(
            dwPackedMoveId=342, dwEventKind=1, dwField08=0,
            dwShapeFlags=1 << (40 - 32), flRadiusScale=9.0,
            flOffsetX=9.0, flOffsetY=9.0, flOffsetZ=9.0,
        ),
    ]
    assert sphere_event_modifier(events, 342, 40, interpolation=0.25) == (
        1.5, (1.0, 2.0, 3.0), 0.25
    )
    assert sphere_event_modifier(events, 342, 3) == (1.0, (0.0, 0.0, 0.0), 0.0)


@pytest.mark.needs_dump
def test_astaroth_lethal_cell_reaction_offset_slot_zero_decodes_forward_velocity():
    khd = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr012.khd").read_bytes())
    cell = khd.sections[0].entries[145]
    x, y, z, evidence = decode_reaction_move_velocity(
        cell, reaction_phase=0, opponent_facing_turns=0.0
    )
    assert (x, y, z) == pytest.approx((0.0, 0.0, 0.01), abs=1.0e-9)
    assert evidence["rangeRaw"] == 10


def test_scenario_mapping_round_trip_and_validation():
    encoded = asdict(ASTAROTH_LEFT_UKEMI)
    assert ComboScenario.from_mapping(encoded) == ASTAROTH_LEFT_UKEMI
    encoded["spacing"]["step"] = 0
    with pytest.raises(ValueError, match="spacing sweep"):
        ComboScenario.from_mapping(encoded)


def test_markdown_output_is_deterministic():
    result = {
        "character": "001",
        "reactionMotion": "0x13DB",
        "firstActiveTick": 74,
        "nearestHurtSphere": {"clearance": -0.125},
        "contactSpacingIntervals": ((0.25, 0.75),),
        "authoredGeometryOverlap": True,
        "observedOutcome": "hit",
        "predictedOutcome": None,
    }
    payload = {
        "scenario": asdict(ASTAROTH_LEFT_UKEMI),
        "results": [result],
        "complete": False,
        "unresolved": ["native admission"],
    }
    first = render_markdown_report(payload)
    assert first == render_markdown_report(payload)
    assert "| `001` | `0x13DB` | 74 | -0.125000 | (0.2500, 0.7500) | hit | unresolved |" in first
    assert "native admission" in first


def test_reaction_partition_validates_labels_without_becoming_classifier():
    partition = summarize_reaction_partition((
        {"reactionMotion": "0x13DB", "observedOutcome": "hit"},
        {"reactionMotion": "0x1478", "observedOutcome": "escape"},
    ))
    assert partition["allObservedCatchesInCatchMotions"] is True
    assert partition["allObservedEscapesInEscapeMotions"] is True
    assert partition["acceptedAsFinalClassifier"] is False


@pytest.mark.needs_dump
def test_training_lethal_route_has_equivalent_authored_cell_and_events():
    evidence = prove_training_route_equivalence(
        Path("E:/myMods/dump/Battle"), ASTAROTH_LEFT_UKEMI
    )
    assert evidence["ordinaryCell"] == 145
    assert evidence["trainingCell"] == 147
    assert evidence["authoredEquivalent"] is True
    assert evidence["trainingRouteUsedAsInput"] is False


@pytest.mark.needs_dump
def test_astaroth_opener_recovery_and_pose_continuation_are_asset_derived():
    root = Path("E:/myMods/dump/Battle")
    khd = parse_khd((root / "hdr" / "hdr012.khd").read_bytes())
    assert _resolve_attack_recovery_entry(khd, 372, 17) == (4, 57, 40)
    continuation = _resolve_opener_continuation(khd, 372, 17)
    assert continuation.target_slot == 373
    assert continuation.author_tick == 3
    assert continuation.commit_tick == 5
    assert continuation.target_start_coordinate == 22
    assert continuation.final_sample_coordinate == 65
    assert continuation.final_sample_tick == 48
