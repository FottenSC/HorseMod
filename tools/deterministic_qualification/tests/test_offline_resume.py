import json
from pathlib import Path

from tools.deterministic_qualification import artifacts
from tools.deterministic_qualification.configuration import (
    contract_sha256,
    expected_fields,
)
from tools.deterministic_qualification.offline_campaign import (
    _load_reusable_capture,
)
from tools.deterministic_qualification.offline_matrix import build_rows


ROOT = Path(__file__).resolve().parents[3]
MANIFEST = (ROOT / "docs" / "investigations"
            / "deterministic-production-candidate-manifest.json")


def test_capture_and_evaluator_identities_are_independent(tmp_path, monkeypatch):
    (tmp_path / "capture.py").write_text("capture-v1", encoding="utf-8")
    (tmp_path / "evaluate.py").write_text("evaluate-v1", encoding="utf-8")
    monkeypatch.setattr(artifacts, "CAPTURE_HARNESS_PATHS", ("capture.py",))
    monkeypatch.setattr(artifacts, "OFFLINE_EVALUATOR_PATHS", ("evaluate.py",))

    capture = artifacts.capture_harness_sha256(tmp_path)
    evaluator = artifacts.offline_evaluator_sha256(tmp_path)
    (tmp_path / "evaluate.py").write_text("evaluate-v2", encoding="utf-8")

    assert artifacts.capture_harness_sha256(tmp_path) == capture
    assert artifacts.offline_evaluator_sha256(tmp_path) != evaluator


def test_raw_capture_resume_requires_every_producer_hash(tmp_path):
    row = next(row for row in build_rows(MANIFEST)
               if row.required_corrections == 0)
    config = expected_fields(enabled=False, trace=True)
    expected = {
        "source": {"commit": "frozen"},
        "dll": "dll", "schema": "schema", "capture_harness": "capture",
        "replay_mod": "bridge", "executable": "game",
    }
    report = {
        "report_schema": 2, "certifying": True, "result": "pass",
        "case_id": row.case_id, "row_id": row.row_id, "renderer": "normal",
        "display_map_name": row.display_map_name,
        "stage_package_root": row.stage_package_root,
        "source": expected["source"],
        "artifacts": {
            "horsemod_dll_sha256": expected["dll"],
            "schema_sha256": expected["schema"],
            "capture_harness_sha256": expected["capture_harness"],
            # The aggregate evaluator/package hash is deliberately irrelevant
            # to immutable raw capture reuse.
            "runner_sha256": "old-evaluator-package",
            "replay": {"sha256": row.replay_sha256},
            "replay_qualification_mod": {"sha256": expected["replay_mod"]},
            "game_executable": {"sha256": expected["executable"]},
            "config_fields": config,
            "config": {"sha256": contract_sha256(config)},
        },
        "runtime": {
            "replay_metadata": {
                "stage": row.replay_metadata_stage,
                "map": row.replay_metadata_map,
                "left_character": row.replay_metadata_fighters[0],
                "right_character": row.replay_metadata_fighters[1],
            },
            "watch_frames": 600, "seek_percentages": [],
            "clean_exit": True, "process_absent_after_exit": True,
            "temporary_mod_removed": True, "native_replay_import_ready": True,
        },
    }
    path = tmp_path / "capture.json"
    path.write_text(json.dumps(report), encoding="utf-8")

    assert _load_reusable_capture(
        path, row, expected, config, stock=False) == report
    report["artifacts"]["capture_harness_sha256"] = "changed"
    path.write_text(json.dumps(report), encoding="utf-8")
    assert _load_reusable_capture(
        path, row, expected, config, stock=False) is None
