from __future__ import annotations

import copy
import json

import pytest

from frame_meter_static import (
    NativeFrameKey,
    _outcome,
    validate_dataset,
    write_cpp_table,
    write_json,
)


def _dataset(record: dict) -> dict:
    record["evidence"]["status"] = record["status"]
    return {
        "schema": "frame-meter-static-v1",
        "certification": "StaticOnlyRuntimeUnvalidated",
        "clock": {
            "domain": "test", "contactTickZero": "test",
            "advantageFormula": "test", "sharedFullFreeze": "test",
        },
        "sources": {
            "executable": {"path": "test.exe", "sha256": "A" * 64},
            "reactionTable": {"path": "yarare.dat", "sha256": "B" * 64},
            "khd": [{"style": "001", "path": "hdr001.khd", "sha256": "C" * 64}],
        },
        "coverage": {
            "recordCount": 1,
            "candidateCellReferenceCount": 1,
            "emittedCellReferenceCount": 1,
            "excludedCellReferenceCount": 0,
            "exclusionReasonCounts": {},
            "statusCounts": {record["status"]: 1},
            "unresolvedReasonCounts": ({record["reasonCode"]: 1} if record["reasonCode"] else {}),
            "silentFallbackCount": 0,
            "genericUnknownCount": 0,
        },
        "records": [record],
    }


def _record(**overrides: object) -> dict:
    key = {
        "style": "001",
        "bank_kind": 0,
        "packed_move": 0x123,
        "attack_cell_index": 7,
        "contact_mode": "Block",
        "reaction_context": "GuardConditionRuntime",
        "reaction_row_id": -1,
        "contact_coordinate": 10,
    }
    row = {
        "key": key,
        "status": "UnsupportedStatic",
        "reasonCode": "clock-alignment",
        "attackerFirstActionableTick": None,
        "defenderFirstActionableTick": None,
        "advantage": None,
        "activeIntervals": [[10, 12]],
        "authoredContactCoordinates": [10],
        "lastLockedAttackerCoordinate": 15,
        "authoredDefenderCounter": 9,
        "reason": "test reason",
        "hitstop": {
            "classification": "SharedFullFreezeExcluded",
            "sharedFullFreezeTicks": None, "asymmetricTicks": None,
            "reason": "test hitstop reason",
        },
        "evidence": {
            "status": "UnsupportedStatic", "sourceKhdSha256": "C" * 64,
            "runtimeValidated": False,
        },
    }
    row.update(overrides)
    return row


def test_coordinate_and_counter_do_not_fabricate_actionable_ticks() -> None:
    result = _outcome(
        mode="Block",
        authored_counter=9,
        categorical=None,
        attacker_coordinate=15,
        reaction_row_id=-1,
    )
    assert result["status"] == "UnsupportedStatic"
    assert result["reasonCode"] == "clock-alignment"
    assert result["attackerFirstActionableTick"] is None
    assert result["defenderFirstActionableTick"] is None
    assert result["advantage"] is None


def test_hit_route_requires_reaction_exit_proof() -> None:
    result = _outcome(
        mode="Hit", authored_counter=12, categorical=None,
        attacker_coordinate=15, reaction_row_id=4,
    )
    assert result["reasonCode"] == "reaction-endpoint"
    assert result["advantage"] is None


def test_knockdown_is_categorical_and_never_gets_numeric_advantage() -> None:
    result = _outcome(
        mode="CounterHit", authored_counter=None,
        categorical="KND", attacker_coordinate=4, reaction_row_id=12,
    )
    assert result["status"] == "CategoricalKnockdown"
    assert result["defenderFirstActionableTick"] is None
    assert result["advantage"] is None


def test_throw_success_category_satisfies_nonnumeric_schema() -> None:
    row = _record(
        status="CategoricalThrowSuccess",
        reasonCode="reaction-endpoint",
        attackerFirstActionableTick=None,
        defenderFirstActionableTick=None,
        advantage=None,
    )
    validate_dataset(_dataset(row))


def test_missing_attacker_coordinate_fails_closed() -> None:
    result = _outcome(
        mode="Hit", authored_counter=20,
        categorical=None, attacker_coordinate=None, reaction_row_id=12,
    )
    assert result["status"] == "UnsupportedStatic"
    assert result["reasonCode"] == "attacker-endpoint"
    assert result["advantage"] is None


def test_schema_rejects_silent_fallback_and_bad_formula() -> None:
    bad_formula = _dataset(_record(
        status="Numeric", reasonCode=None,
        attackerFirstActionableTick=6,
        defenderFirstActionableTick=9,
        advantage=4,
    ))
    with pytest.raises(ValueError, match="fabricated advantage"):
        validate_dataset(bad_formula)

    silent = _record(
        status="UnsupportedStatic",
        reasonCode=None,
        attackerFirstActionableTick=None,
        defenderFirstActionableTick=None,
        advantage=None,
    )
    with pytest.raises(ValueError, match="concrete nonnumeric reason"):
        validate_dataset(_dataset(silent))


def test_json_and_cpp_exports_are_byte_deterministic(tmp_path) -> None:
    dataset = _dataset(_record())
    validate_dataset(dataset)
    json_a, json_b = tmp_path / "a.json", tmp_path / "b.json"
    cpp_a, cpp_b = tmp_path / "a.hpp", tmp_path / "b.hpp"
    write_json(dataset, json_a)
    write_json(copy.deepcopy(dataset), json_b)
    write_cpp_table(dataset, cpp_a)
    write_cpp_table(copy.deepcopy(dataset), cpp_b)
    assert json_a.read_bytes() == json_b.read_bytes()
    assert cpp_a.read_bytes() == cpp_b.read_bytes()
    loaded = json.loads(json_a.read_text())
    assert loaded["schema"] == "frame-meter-static-v1"
    validate_dataset(loaded)


def test_cpp_compaction_rejects_coordinate_dependent_semantics(tmp_path) -> None:
    first = _record()
    second = copy.deepcopy(first)
    second["key"]["contact_coordinate"] = 11
    second["authoredContactCoordinates"] = [11]
    second["authoredDefenderCounter"] = 10
    dataset = _dataset(first)
    dataset["records"].append(second)
    with pytest.raises(ValueError, match="discard conflicting semantics"):
        write_cpp_table(dataset, tmp_path / "conflict.hpp")


def test_cpp_table_uses_numeric_enum_sort_order(tmp_path) -> None:
    hit = _record()
    hit["key"]["contact_mode"] = "Hit"
    hit["key"]["reaction_context"] = "StandardGrounded"
    counter = copy.deepcopy(hit)
    counter["key"]["contact_mode"] = "CounterHit"
    counter["key"]["reaction_context"] = "RuntimeClassifierRoute"
    dataset = _dataset(counter)
    dataset["records"] = [counter, hit]
    output = tmp_path / "ordered.hpp"
    write_cpp_table(dataset, output)
    text = output.read_text(encoding="utf-8")
    assert text.index("StaticLookupMode::Hit") < text.index("StaticLookupMode::CounterHit")


def test_key_shape_is_explicit_and_stable() -> None:
    assert tuple(NativeFrameKey.__dataclass_fields__) == (
        "style", "bank_kind", "packed_move", "attack_cell_index",
        "contact_mode", "reaction_context", "reaction_row_id",
        "contact_coordinate",
    )


def test_schema_file_is_valid_json() -> None:
    from pathlib import Path
    import jsonschema

    schema = Path(__file__).parents[1] / "schemas" / "frame-meter-static-v1.schema.json"
    parsed = json.loads(schema.read_text(encoding="utf-8"))
    assert parsed["$id"] == "frame-meter-static-v1"
    jsonschema.Draft202012Validator.check_schema(parsed)
    jsonschema.validate(_dataset(_record()), parsed)


def test_ignored_packed_move_bit_is_rejected() -> None:
    row = _record()
    row["key"]["packed_move"] |= 0x0800
    with pytest.raises(ValueError, match="ignored packed-move bit"):
        validate_dataset(_dataset(row))


def test_coverage_reconciliation_rejects_omitted_candidates() -> None:
    dataset = _dataset(_record())
    dataset["coverage"] = {
        "recordCount": 1,
        "candidateCellReferenceCount": 2,
        "emittedCellReferenceCount": 1,
        "excludedCellReferenceCount": 0,
        "exclusionReasonCounts": {},
        "statusCounts": {"UnsupportedStatic": 1},
        "unresolvedReasonCounts": {"clock-alignment": 1},
        "silentFallbackCount": 1,
        "genericUnknownCount": 0,
    }
    with pytest.raises(ValueError, match="reconciliation"):
        validate_dataset(dataset)
