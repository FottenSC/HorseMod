from __future__ import annotations

from pathlib import Path

import pytest

from luxformats import parse_auto
from native_frame_analysis import (
    _confirm_reaction_row,
    _has_reaction_motion_state_contract,
    _resolve_reaction_motion_state_code,
    analyze_confirmed_slot_frames,
    analyze_throw_break_frames,
)
from native_reaction_table import (
    LuxHitReactionMoveIdRow,
    parse_hit_reaction_move_id_table,
)


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


def test_reaction_path_status_keeps_standing_and_crouched_columns_separate():
    row = LuxHitReactionMoveIdRow(
        metadata_words=(0, 0),
        base_move_ids_by_facing=(1, 2, 3, 4),
        alternate_move_ids_by_facing=(0x8001, 0x8002, 0x8003, 0x8004),
    )

    assert row.base_move_path_status == "ordinary"
    assert row.alternate_move_path_status == "promoted"
    assert row.move_path_status == "mixed"


def test_block_advantage_does_not_require_unrelated_hit_reaction_table():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))

    frame = analyze_confirmed_slot_frames(khd, attack_slot=310, attack_cell=71)
    assert frame is not None
    assert frame.block_advantage == -8
    assert frame.hit_advantage is None
    assert frame.counter_hit_advantage is None


def test_common_frame_analyzer_keeps_bear_tamer_ordinary_hitstun_numeric():
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
    assert frame.counter_hit_stun_frames is None
    assert frame.counter_hit_advantage is None
    assert frame.hit_outcome is None
    assert frame.counter_hit_outcome is None
    assert frame.hit_reaction.reaction_row_id == 272
    assert len(frame.hit_reaction.resolved_slots) == 4
    assert frame.hit_reaction.raw_move_ids == reaction_table.rows[
        frame.hit_reaction.reaction_row_id
    ].base_move_ids_by_facing
    assert frame.hit_reaction.must_latched_motion_flags == (0x0E,)
    assert frame.hit_reaction.may_latched_motion_flags == (0x0E,)
    assert frame.hit_reaction.terminal_motion_flags == (0x0E,)
    assert any(
        "selected-air-cinematic=35(+0x48;not-counter-hit)"
        in resolution
        for resolution in frame.resolutions
    )


def test_counter_knockdown_uses_native_motion_state_classifier_not_flag_name():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))
    reaction_table = parse_hit_reaction_move_id_table(
        path.with_name("yarare.dat").read_bytes()
    )

    frame = analyze_confirmed_slot_frames(
        khd,
        attack_slot=341,
        attack_cell=111,
        reaction_table=reaction_table,
    )

    assert frame is not None
    assert (frame.block_advantage, frame.hit_advantage) == (-8, 6)
    assert frame.counter_hit_advantage is None
    assert frame.hit_outcome is None
    # +0x52 is an air/cinematic row, not a Counter-Hit row. Its KND route must
    # not be published as a Counter-Hit outcome.
    assert frame.counter_hit_outcome is None
    assert frame.hit_reaction.terminal_motion_flags == (0x02, 0x0E)
    assert frame.counter_hit_reaction is None


def test_promoted_reaction_move_id_is_not_normalized_into_numeric_advantage():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))
    reaction_table = parse_hit_reaction_move_id_table(
        path.with_name("yarare.dat").read_bytes()
    )

    # Tornado Spike's held contact uses reaction row 687. Every facing id in
    # that row has bit 15 set, which ComputeHitReactionParams consumes as a
    # promoted-path flag before transitioning to the low-15-bit MoveVM slot.
    assert reaction_table.rows[687].move_path_status == "promoted"
    frame = analyze_confirmed_slot_frames(
        khd,
        attack_slot=319,
        attack_cell=79,
        reaction_table=reaction_table,
    )

    assert frame is not None
    assert frame.block_advantage == 5
    assert frame.hit_advantage is None
    assert frame.counter_hit_advantage is None
    assert frame.hit_outcome == "LNC"
    assert frame.counter_hit_outcome is None
    assert frame.hit_reaction.must_latched_motion_flags == (0x0F,)
    assert frame.hit_reaction.terminal_motion_flags == (0x19, 0x28)
    assert frame.hit_reaction.positive_vertical_effect is True


def test_reaction_consensus_covers_old_and_new_defender_khd_revisions():
    root = Path(__file__).parents[3] / "dump" / "Battle" / "hdr"
    old_path = root / "hdr001.khd"
    new_path = root / "hdr012.khd"
    if not old_path.exists() or not new_path.exists():
        pytest.skip("checked-in old/new KHD reaction revisions are unavailable")
    old_khd = parse_auto(str(old_path))
    astaroth_khd = parse_auto(str(new_path))
    reaction_table = parse_hit_reaction_move_id_table(
        (root / "yarare.dat").read_bytes()
    )

    frame = analyze_confirmed_slot_frames(
        astaroth_khd,
        attack_slot=310,
        attack_cell=71,
        reaction_table=reaction_table,
        reaction_khds=(old_khd, astaroth_khd),
    )

    assert frame is not None
    assert (frame.hit_advantage, frame.counter_hit_advantage) == (2, None)
    assert frame.hit_reaction.defender_profile_count == 2
    assert frame.hit_reaction.defender_profiles_confirmed == 2
    assert frame.hit_reaction.defender_static_profile_count == 2
    # Bank-relative slot numbers differ across revisions and must not be
    # presented as one universal defender slot tuple.
    assert frame.hit_reaction.resolved_slots == ()


def test_reaction_population_rejects_partial_launcher_consensus():
    root = Path(__file__).parents[3] / "dump" / "Battle" / "hdr"
    old_path = root / "hdr001.khd"
    new_path = root / "hdr012.khd"
    if not old_path.exists() or not new_path.exists():
        pytest.skip("checked-in old/new KHD reaction revisions are unavailable")
    old_khd = parse_auto(str(old_path))
    astaroth_khd = parse_auto(str(new_path))
    reaction_table = parse_hit_reaction_move_id_table(
        (root / "yarare.dat").read_bytes()
    )

    frame = analyze_confirmed_slot_frames(
        astaroth_khd,
        attack_slot=319,
        attack_cell=79,
        reaction_table=reaction_table,
        reaction_khds=(old_khd, astaroth_khd),
    )

    assert frame is not None
    assert frame.hit_outcome is None
    assert frame.counter_hit_outcome is None
    assert frame.hit_advantage is None
    assert frame.counter_hit_advantage is None


def test_reaction_entry_state_persists_into_tick_and_allows_auxiliary_root_helper():
    root = Path(__file__).parents[3] / "dump" / "Battle" / "hdr"
    old_path = root / "hdr001.khd"
    if not old_path.exists():
        pytest.skip("checked-in old KHD reaction revision is unavailable")
    old_khd = parse_auto(str(old_path))
    reaction_table = parse_hit_reaction_move_id_table(
        (root / "yarare.dat").read_bytes()
    )

    # Row 814's fourth facing wrapper calls the common 0x305C auxiliary after
    # the driver. During entry the driver writes GLOBAL[25]=14; later tick
    # executions use it to author concrete 0x305D phase boundaries.
    proof = _confirm_reaction_row(old_khd, reaction_table, 814)

    assert proof is not None
    assert proof.must_latched_motion_flags == (0x0F,)
    assert proof.terminal_motion_flags == (0x03, 0x10)
    assert proof.outcome == "KND"
    assert set(proof.motion_state_codes) == {6, 7}
    assert proof.numeric_endpoint is False


def test_reaction_root_accepts_only_hashed_noop_auxiliary_after_driver():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))
    reaction_table = parse_hit_reaction_move_id_table(
        path.with_name("yarare.dat").read_bytes()
    )

    # Row 305's four wrappers call their normal driver and then packed 0x3242,
    # whose audited body is exactly FRAME 0; NOP; RET2 in every playable bank.
    proof = _confirm_reaction_row(khd, reaction_table, 305)

    assert proof is not None
    assert proof.numeric_endpoint is True
    assert proof.outcome is None


def test_reaction_root_intersects_both_finite_posture_predicate_branches():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "hdr" / "hdr012.khd"
    if not path.exists():
        pytest.skip("checked-in Astaroth KHD asset is unavailable")
    khd = parse_auto(str(path))
    reaction_table = parse_hit_reaction_move_id_table(
        path.with_name("yarare.dat").read_bytes()
    )

    # Two facings in row 521 select motion 208 or 209 through one IF 0x0BBE
    # branch. Both paths must independently prove native state00 6/7.
    proof = _confirm_reaction_row(khd, reaction_table, 521)

    assert proof is not None
    assert proof.outcome == "KND"
    assert proof.motion_state_codes == (6, 7)
    assert proof.numeric_endpoint is False


def test_state11_intermediate_reaction_chains_to_knockdown_endpoint():
    root = Path(__file__).parents[3] / "dump" / "Battle" / "hdr"
    old_path = root / "hdr001.khd"
    if not old_path.exists():
        pytest.skip("checked-in facing-specific reaction revision is unavailable")
    khd = parse_auto(str(old_path))
    reaction_table = parse_hit_reaction_move_id_table(
        (root / "yarare.dat").read_bytes()
    )

    # Row 983 facing 0 enters classifier state 11, then its driver authors a
    # terminal transition to the state-6 reaction used by facing 1.  Other
    # facings enter the proven state-6/7 KND family directly.
    proof = _confirm_reaction_row(khd, reaction_table, 983)

    assert proof is not None
    assert proof.outcome == "KND"
    assert proof.numeric_endpoint is False
    assert set(proof.motion_state_codes) == {6, 7, 11}


@pytest.mark.parametrize(
    ("filename", "packed_helper"),
    (("hdr001.khd", 0x3104), ("hdr012.khd", 0x3114)),
)
def test_motion_state_classifier_contract_covers_both_shipped_revisions(
    filename: str,
    packed_helper: int,
):
    root = Path(__file__).parents[3] / "dump" / "Battle" / "hdr"
    path = root / filename
    if not path.exists():
        pytest.skip("checked-in KHD reaction revision is unavailable")
    khd = parse_auto(str(path))
    slot = khd.resolve_packed_slot(packed_helper)
    assert slot is not None
    script = khd.slots[slot].bytecode
    assert script is not None

    assert _has_reaction_motion_state_contract(script)
    assert _resolve_reaction_motion_state_code(khd, script, 200) == 1
    assert _resolve_reaction_motion_state_code(khd, script, 208) == 6
    assert _resolve_reaction_motion_state_code(khd, script, 209) == 7


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
