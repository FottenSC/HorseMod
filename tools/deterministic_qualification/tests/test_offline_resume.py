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
from tools.deterministic_qualification.offline_capture import (
    _read_current_run_tail, capture_log_artifact_is_intact,
)
from tools.deterministic_qualification.trace_parser import capture_log_offset
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


def test_offline_capture_protocol_is_hashed_as_producer_not_policy():
    capture_path = "tools/deterministic_qualification/offline_capture.py"
    specification_path = "tools/deterministic_qualification/offline_spec.py"
    campaign_path = "tools/deterministic_qualification/offline_campaign.py"
    assert capture_path in artifacts.CAPTURE_HARNESS_PATHS
    assert capture_path not in artifacts.OFFLINE_EVALUATOR_PATHS
    assert specification_path in artifacts.CAPTURE_HARNESS_PATHS
    assert specification_path not in artifacts.OFFLINE_EVALUATOR_PATHS
    assert campaign_path not in artifacts.CAPTURE_HARNESS_PATHS
    assert campaign_path in artifacts.OFFLINE_EVALUATOR_PATHS


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
    bounded_log = path.with_suffix(".log")
    bounded_log.write_text("bounded current run\n", encoding="utf-8")
    report["artifacts"]["bounded_log"] = {
        "sha256": artifacts.sha256_file(bounded_log),
        "size": bounded_log.stat().st_size,
    }
    path.write_text(json.dumps(report), encoding="utf-8")

    assert _load_reusable_capture(
        path, row, expected, config, stock=False) == report
    report["artifacts"]["capture_harness_sha256"] = "changed"
    path.write_text(json.dumps(report), encoding="utf-8")
    assert _load_reusable_capture(
        path, row, expected, config, stock=False) is None


def test_raw_capture_resume_rejects_changed_bounded_log(tmp_path):
    row = next(row for row in build_rows(MANIFEST)
               if row.required_corrections == 0)
    config = expected_fields(enabled=False, trace=True)
    expected = {
        "dll": "dll", "schema": "schema", "capture_harness": "capture",
        "replay_mod": "bridge", "executable": "game",
    }
    path = tmp_path / "capture.json"
    bounded_log = path.with_suffix(".log")
    bounded_log.write_text("original\n", encoding="utf-8")
    report = {
        "report_schema": 2, "certifying": True, "result": "pass",
        "case_id": row.case_id, "row_id": row.row_id, "renderer": "normal",
        "display_map_name": row.display_map_name,
        "stage_package_root": row.stage_package_root,
        "artifacts": {
            "horsemod_dll_sha256": "dll", "schema_sha256": "schema",
            "capture_harness_sha256": "capture",
            "replay": {"sha256": row.replay_sha256},
            "replay_qualification_mod": {"sha256": "bridge"},
            "game_executable": {"sha256": "game"},
            "config_fields": config,
            "config": {"sha256": contract_sha256(config)},
            "bounded_log": {
                "sha256": artifacts.sha256_file(bounded_log),
                "size": bounded_log.stat().st_size,
            },
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
    path.write_text(json.dumps(report), encoding="utf-8")
    bounded_log.write_text("modified\n", encoding="utf-8")

    assert _load_reusable_capture(
        path, row, expected, config, stock=False) is None


def test_capture_log_keeps_only_current_bounded_tail(tmp_path):
    log = tmp_path / "UE4SS.log"
    log.write_text("old process\n", encoding="utf-8")
    cursor = capture_log_offset(log)
    with log.open("a", encoding="utf-8") as stream:
        for index in range(700):
            stream.write(f"current {index}\n")

    lines = _read_current_run_tail(log, cursor, maximum_lines=256)

    assert len(lines) == 256
    assert lines[0] == "current 444"
    assert lines[-1] == "current 699"
    assert "old process" not in lines


def test_composed_capture_reuse_requires_stored_log_artifact(tmp_path):
    bounded_log = tmp_path / "raw" / "row-primary.log"
    bounded_log.parent.mkdir()
    bounded_log.write_text("evidence\n", encoding="utf-8")
    report = {"artifacts": {"bounded_log": {
        "path": str(bounded_log),
        "sha256": artifacts.sha256_file(bounded_log),
        "size": bounded_log.stat().st_size,
    }}}

    assert capture_log_artifact_is_intact(report)
    bounded_log.write_text("changed\n", encoding="utf-8")
    assert not capture_log_artifact_is_intact(report)
