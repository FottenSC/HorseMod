import json

import pytest

from native_combat_oracle import (
    canonical_bytes,
    first_divergence,
    validate_oracle_boundary,
)


def _root(**updates: object) -> dict[str, object]:
    record: dict[str, object] = {
        "event": "native_battle_tick_root_transaction",
        "ts_qpc": 1,
        "pid": 2,
        "thread_id": 3,
        "image_base": "0x140000000",
        "oracle_executable_sha256": "exe",
        "oracle_p1_asset_sha256": "p1",
        "oracle_p2_asset_sha256": "p2",
        "oracle_scope_admitted": True,
        "after_p1_velocity_x_bits": "0x3f800000",
    }
    record.update(updates)
    return record


def test_process_local_metadata_does_not_break_repeat_identity() -> None:
    left = [_root(ts_qpc=10, pid=20, thread_id=30, image_base="0x1000")]
    right = [_root(ts_qpc=11, pid=21, thread_id=31, image_base="0x2000")]
    assert canonical_bytes(left) == canonical_bytes(right)
    assert first_divergence(left, right) is None


def test_first_divergence_reports_subsystem_partition() -> None:
    left = [_root(after_p1_velocity_x_bits="0x3f800000")]
    right = [_root(after_p1_velocity_x_bits="0x40000000")]
    difference = first_divergence(left, right)
    assert difference is not None
    assert difference.partition == "kinematics"
    assert difference.field == "after_p1_velocity_x_bits"


def test_oracle_fails_closed_without_artifact_or_scope_binding() -> None:
    validate_oracle_boundary([_root()])
    with pytest.raises(ValueError, match="artifact binding"):
        validate_oracle_boundary([_root(oracle_p2_asset_sha256="")])
    with pytest.raises(ValueError, match="outside the frozen"):
        validate_oracle_boundary([_root(oracle_scope_admitted=False)])
