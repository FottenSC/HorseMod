from pathlib import Path

import pytest

from static_model_coverage import (
    IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS,
    IMPLEMENTED_CALLCOND_EFFECT_OPCODES,
    IMPLEMENTED_CALLCOND_HANDLERS,
    IMPLEMENTED_SUBSYSTEMS,
    REVIEWED_CALLCOND_16_ARGUMENTS,
    REVIEWED_CALLCOND_26_ARGUMENT_COUNTS,
    REQUIRED_SUBSYSTEMS,
    canonical_json,
    extract_contiguous_literal_index,
    require_single_root,
)
from stackvm import walk_stackvm


def test_single_asset_root_rejects_mixed_extraction(tmp_path: Path) -> None:
    root = tmp_path / "root"
    root.mkdir()
    inside = root / "inside.khd"
    outside = tmp_path / "outside.khd"
    inside.write_bytes(b"inside")
    outside.write_bytes(b"outside")

    assert require_single_root([inside], root) == (inside.resolve(),)
    with pytest.raises(ValueError, match="mixed extraction roots refused"):
        require_single_root([inside, outside], root)


def test_manifest_json_is_byte_stable() -> None:
    manifest = {"z": [3, 2, 1], "a": {"value": 4}}
    first = canonical_json(manifest).encode("utf-8")
    second = canonical_json(manifest).encode("utf-8")
    assert first == second
    assert first.startswith(b'{\n  "a"')


def test_static_complete_gate_includes_non_bytecode_subsystems() -> None:
    assert IMPLEMENTED_SUBSYSTEMS < REQUIRED_SUBSYSTEMS
    assert "raw_input_to_current_snapshot" in IMPLEMENTED_SUBSYSTEMS
    assert "current_input_snapshot_to_history_commit" in IMPLEMENTED_SUBSYSTEMS
    assert "per_player_training_input_record_and_playback" in IMPLEMENTED_SUBSYSTEMS
    assert "shared_dummy_dual_training_input_record_and_playback" in IMPLEMENTED_SUBSYSTEMS
    assert "training_input_stop_event_dispatch" in IMPLEMENTED_SUBSYSTEMS
    assert "shared_dummy_dual_training_input_record_and_playback" in REQUIRED_SUBSYSTEMS
    assert "pose_skeleton_and_blending" in REQUIRED_SUBSYSTEMS
    assert "khit_intersection" in REQUIRED_SUBSYSTEMS
    assert "camera_relative_input_side_source" in IMPLEMENTED_SUBSYSTEMS
    assert "all_concrete_input_transform_providers" in IMPLEMENTED_SUBSYSTEMS


def test_callcond_1a_registry_is_bound_to_reviewed_zero_argument_shape() -> None:
    assert 0x1A in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x1A] == frozenset({0})


def test_callcond_23_registry_is_bound_to_reviewed_one_argument_shape() -> None:
    assert 0x23 in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x23] == frozenset({1})


def test_callcond_02_registry_is_bounded_to_complete_authored_effect_subset() -> None:
    assert 0x02 in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x02] == frozenset({1, 2, 3})
    assert IMPLEMENTED_CALLCOND_EFFECT_OPCODES[0x02] == frozenset(
        {0x0004, 0x0006, 0x000E}
    )


def test_callcond_25_registry_covers_point_and_window_shapes() -> None:
    assert 0x25 in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x25] == frozenset({1, 2})


def test_callcond_19_registry_covers_every_authored_payload_shape() -> None:
    assert 0x19 in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x19] == frozenset(
        {2, 3, 4, 5, 11, 15}
    )


def test_callcond_10_registry_is_bound_to_authored_immediate_commit_shape() -> None:
    assert 0x10 in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x10] == frozenset({0})


def test_motion_flag_callconds_are_bound_to_reviewed_one_argument_shape() -> None:
    assert {0x09, 0x0A} <= IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x09] == frozenset({1})
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x0A] == frozenset({1})


def test_transition_author_callconds_cover_complete_authored_variadic_shapes() -> None:
    assert {0x06, 0x07} <= IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x06] == frozenset({1, 2, 3})
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x07] == frozenset(
        {1, 2, 3, 4, 5, 6}
    )


def test_chara_state_short_writer_is_bound_to_two_argument_shape() -> None:
    assert 0x14 in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x14] == frozenset({2})


def test_transition_script_scope_is_bound_to_authored_one_argument_shape() -> None:
    assert 0x15 in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x15] == frozenset({1})


def test_pending_transition_drain_has_a_bounded_authored_operand_domain() -> None:
    assert 0x16 not in IMPLEMENTED_CALLCOND_HANDLERS
    assert REVIEWED_CALLCOND_16_ARGUMENTS == frozenset({(), (2,)})


def test_active_cell_variant_has_one_dynamic_authored_word() -> None:
    assert 0x26 not in IMPLEMENTED_CALLCOND_HANDLERS
    assert REVIEWED_CALLCOND_26_ARGUMENT_COUNTS == frozenset({1})


def test_indexed_float_writer_covers_both_authored_argument_shapes() -> None:
    assert 0x0C in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x0C] == frozenset({2, 3})


def test_nested_bank_slot_executor_covers_every_authored_local_frame_shape() -> None:
    assert 0x0D in IMPLEMENTED_CALLCOND_HANDLERS
    assert IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS[0x0D] == frozenset(
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
    )


def test_literal_index_contract_rejects_dynamic_or_out_of_range_producers() -> None:
    valid = walk_stackvm(
        bytes((0x89, 0x00, 0x0D, 0x89, 0x3C, 0x00, 0xA5, 0x0C, 0x02, 0x02)),
        0,
    ).instructions
    assert extract_contiguous_literal_index(valid, 2, 2, 14) == 13

    dynamic = walk_stackvm(
        bytes((0x8A, 0x00, 0x01, 0x89, 0x3C, 0x00, 0xA5, 0x0C, 0x02, 0x02)),
        0,
    ).instructions
    with pytest.raises(ValueError, match="not a literal"):
        extract_contiguous_literal_index(dynamic, 2, 2, 14)

    out_of_range = walk_stackvm(
        bytes((0x89, 0x00, 0x0E, 0x89, 0x3C, 0x00, 0xA5, 0x0C, 0x02, 0x02)),
        0,
    ).instructions
    with pytest.raises(ValueError, match="outside 0..13"):
        extract_contiguous_literal_index(out_of_range, 2, 2, 14)
