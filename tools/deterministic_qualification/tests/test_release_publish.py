import json

import pytest

from tools.deterministic_qualification.release_publish import (
    _certificate, _common_report, _paired_gate,
)


CASE = {
    "case_id": "case-a", "fighter_order": ["012", "015"],
    "stage_selection_code": "273", "authored_stage_code": "111",
    "stage_package_root": "/Game/DLC/07/Stage/STG011_R",
    "map_path": "/Game/DLC/07/Stage/STG011_R/Maps/STG011_R",
    "native_display_name": "Silver Wolves' Haven",
}


def test_release_certificate_is_canonical_compact_json():
    hashes = {
        "executable": "11" * 32, "dll": "22" * 32,
        "source": "33" * 32, "schema": "44" * 32,
        "candidate": "55" * 32, "regions": "66" * 32,
        "runner": "77" * 32, "capture_harness": "88" * 32,
        "offline_evaluator": "99" * 32, "replay_mod": "aa" * 32,
    }
    encoded = _certificate(CASE, True, hashes, "77" * 32)
    assert b'": ' not in encoded and b", " not in encoded
    value = json.loads(encoded)
    assert value["kind"] == "paired_online_release_case"
    assert value["fighter_order"] == ["012", "015"]
    assert value["qualification_complete"] is True
    assert value["capture_harness_sha256"] == "88" * 32
    assert value["offline_evaluator_sha256"] == "99" * 32
    assert encoded.endswith(b"}") and not encoded.endswith(b"\n")


def test_paired_release_gate_refuses_a_superficial_pass():
    with pytest.raises(RuntimeError, match="paired impairment profiles missing"):
        _paired_gate([], CASE, "aa" * 32, "bb" * 32)


def test_replay_release_evidence_uses_capture_identity_not_policy_hash():
    report = {
        "report_schema": 2, "certifying": True, "result": "pass",
        "case_id": CASE["case_id"],
        "display_map_name": CASE["native_display_name"],
        "stage_package_root": CASE["stage_package_root"],
        "source": {"commit": "capture-time-source"},
        "artifacts": {
            "horsemod_dll_sha256": "dll",
            "schema_sha256": "schema",
            "capture_harness_sha256": "capture",
            "runner_sha256": "old-policy-package",
            "game_executable": {"sha256": "game"},
            "replay_qualification_mod": {"sha256": "bridge"},
            "config": {"sha256": "11" * 32},
        },
    }
    expected = {
        "source": {"commit": "current-policy-source"},
        "runner": "current-policy-package",
        "capture_harness": "capture",
        "executable": "game",
        "replay_mod": "bridge",
    }

    _common_report(report, CASE, "dll", "schema", expected)
