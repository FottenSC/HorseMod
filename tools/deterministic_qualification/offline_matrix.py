from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .configuration import expected_fields, is_exact_contract


LOCATIONS = ("near_round_start", "active_combat", "confirmed_hit", "round_end")
MODES = (("depth_1", 1), ("depth_6", 6), ("depth_11", 11),
         ("continuous_depth_7", 7))


@dataclass(frozen=True)
class OfflineMatrixRow:
    row_id: str
    case_id: str
    replay: str
    replay_sha256: str
    replay_metadata_stage: int
    replay_metadata_map: int
    replay_metadata_fighters: tuple[int, int]
    display_map_name: str
    stage_package_root: str
    location: str | None
    mode: str
    depth: int
    required_corrections: int


def load_candidate_cases(path: Path) -> list[dict[str, Any]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    cases = document.get("cases")
    if document.get("schema_version") != 1 or not isinstance(cases, list) or len(cases) != 3:
        raise RuntimeError("candidate manifest must contain exactly three schema-v1 cases")
    required = {"case_id", "replay", "replay_sha256",
                "replay_metadata_stage", "replay_metadata_map",
                "fighter_order", "stage_package_root", "native_display_name"}
    for case in cases:
        if not required <= set(case) or len(case["fighter_order"]) != 2:
            raise RuntimeError("candidate manifest case is incomplete")
    return cases


def build_rows(candidate_manifest: Path) -> tuple[OfflineMatrixRow, ...]:
    rows: list[OfflineMatrixRow] = []
    for case in load_candidate_cases(candidate_manifest):
        case_id = case["case_id"]
        common = dict(case_id=case_id, replay=case["replay"],
                      replay_sha256=case["replay_sha256"],
                      replay_metadata_stage=case["replay_metadata_stage"],
                      replay_metadata_map=case["replay_metadata_map"],
                      replay_metadata_fighters=tuple(
                          int(value) - 1 for value in case["fighter_order"]),
                      display_map_name=case["native_display_name"],
                      stage_package_root=case["stage_package_root"])
        rows.append(OfflineMatrixRow(
            row_id=f"{case_id}__baseline", location=None,
            mode="same_build_no_correction", depth=0,
            required_corrections=0, **common))
        for location in LOCATIONS:
            for mode, depth in MODES:
                rows.append(OfflineMatrixRow(
                    row_id=f"{case_id}__{location}__{mode}",
                    location=location, mode=mode, depth=depth,
                    required_corrections=600, **common))
    if len(rows) != 51 or len({row.row_id for row in rows}) != 51:
        raise RuntimeError("offline matrix must contain exactly 51 unique rows")
    return tuple(rows)


def _require(condition: bool, reason: str, failures: list[str]) -> None:
    if not condition:
        failures.append(reason)


def evaluate_row(row: OfflineMatrixRow, report: dict[str, Any],
                 dll_sha256: str, schema_sha256: str,
                 runner_sha256: str,
                 expected_artifacts: dict[str, Any] | None = None) -> list[str]:
    failures: list[str] = []
    artifacts = report.get("artifacts", {})
    runtime = report.get("runtime", {})
    presentation = runtime.get("presentation", {})
    performance = runtime.get("performance", {})
    _require(report.get("report_schema") == 2, "report schema is not v2", failures)
    _require(report.get("certifying") is True, "report is non-certifying", failures)
    _require(report.get("result") == "pass", "row result is not pass", failures)
    _require(report.get("case_id") == row.case_id, "case identity mismatch", failures)
    _require(report.get("row_id") == row.row_id, "row identity mismatch", failures)
    _require(report.get("renderer") == "normal", "renderer is not normal", failures)
    _require(report.get("display_map_name") == row.display_map_name,
             "native display map name mismatch", failures)
    _require(report.get("stage_package_root") == row.stage_package_root,
             "authored stage package mismatch", failures)
    _require(artifacts.get("horsemod_dll_sha256") == dll_sha256,
             "DLL hash mismatch", failures)
    _require(artifacts.get("schema_sha256") == schema_sha256,
             "schema hash mismatch", failures)
    _require(artifacts.get("runner_sha256") == runner_sha256,
             "runner hash mismatch", failures)
    _require(artifacts.get("replay", {}).get("sha256") == row.replay_sha256,
             "frozen replay hash mismatch", failures)
    metadata = runtime.get("replay_metadata", {})
    _require(metadata.get("stage") == row.replay_metadata_stage
             and metadata.get("map") == row.replay_metadata_map,
             "native replay map metadata mismatch", failures)
    _require((metadata.get("left_character"), metadata.get("right_character"))
             == row.replay_metadata_fighters,
             "native replay fighter metadata mismatch", failures)
    if expected_artifacts is not None:
        _require(report.get("source") == expected_artifacts["source"],
                 "source identity mismatch", failures)
        _require(artifacts.get("replay_qualification_mod", {}).get("sha256")
                 == expected_artifacts["replay_mod"],
                 "replay bridge hash mismatch", failures)
        _require(artifacts.get("game_executable", {}).get("sha256")
                 == expected_artifacts["executable"],
                 "game executable hash mismatch", failures)
        _require(isinstance(artifacts.get("config", {}).get("sha256"), str)
                 and len(artifacts["config"]["sha256"]) == 64,
                 "config hash binding missing", failures)
    config = artifacts.get("config_fields", {})
    expected_config = expected_fields(
        enabled=False, trace=True,
        forced_depth7=bool(row.required_corrections),
        depth=row.depth if row.required_corrections else 7,
        location=(LOCATIONS.index(row.location) + 1)
            if row.required_corrections else 2)
    _require(is_exact_contract(config, expected_config),
             "qualification config contract mismatch", failures)
    _require(runtime.get("canonical_convergence") == "exact",
             "canonical convergence is not exact", failures)
    _require(runtime.get("capacity_failures") == 0,
             "capacity failure observed", failures)
    _require(runtime.get("capacity_growth_events") == 0,
             "capacity growth observed", failures)
    _require(runtime.get("timeline_accounting_failures") == 0,
             "timeline identity/accounting failure observed", failures)
    _require(runtime.get("presentation_duplicate_failures") == 0
             and runtime.get("presentation_publish_failures") == 0,
             "presentation allocation/publication identity failure observed",
             failures)
    _require(runtime.get("aggregate_owned_bytes", 10**18) <= 576 * 1024**2,
             "aggregate deterministic owned storage exceeded 576 MiB", failures)
    _require(runtime.get("clean_exit") is True and runtime.get("reentry") is True,
             "clean exit/re-entry missing", failures)
    _require(performance.get("normal_render_fps", 0) >= 58.0
             and performance.get("normal_render_tick_rate", 0) >= 58.0,
             "full normal-render frame/tick rate was below 58 Hz", failures)
    _require(performance.get("active_battle_fps", 0) >= 58.0
             and performance.get("active_battle_tick_rate", 0) >= 58.0,
             "active-battle frame/tick rate was below 58 Hz", failures)
    reentry_proof = runtime.get("reentry_proof", {})
    _require(all(reentry_proof.get(key) is True for key in (
        "distinct_run_id", "native_import_ready", "clean_exit",
        "temporary_mod_removed", "process_absent_after_exit")),
        "observed re-entry process proof missing", failures)
    _require(presentation.get("ordered_audio_payload_ids") is True,
             "ordered audio payload-ID identity failed", failures)
    _require(presentation.get("ephemeral_exactly_once") is True,
             "ephemeral presentation was not exactly once", failures)
    _require(presentation.get("persistent_final_exact") is True,
             "persistent presentation did not reconcile exactly", failures)
    _require(presentation.get("leaks") == 0,
             "presentation leak observed", failures)
    if row.required_corrections:
        _require(runtime.get("location") == row.location,
                 "correction location mismatch", failures)
        _require(runtime.get("depth") == row.depth,
                 "correction depth mismatch", failures)
        _require(runtime.get("consecutive_corrections", 0) >= row.required_corrections,
                 "fewer than 600 consecutive corrections", failures)
        _require(presentation.get("required_activity", 0) > 0,
                 "required presentation activity was zero", failures)
        _require(presentation.get("terminal_coverage") == "complete",
                 "presentation terminal coverage incomplete", failures)
        _require(performance.get("capture_p99_us", 10**9) <= 500,
                 "checkpoint capture p99 exceeded 0.5 ms", failures)
        _require(performance.get("capture_max_us", 10**9) <= 1000,
                 "checkpoint capture max exceeded 1 ms", failures)
        if row.depth == 7:
            _require(performance.get("correction_p99_us", 10**9) < 16670,
                     "depth-7 correction p99 exceeded 16.67 ms", failures)
    else:
        _require(runtime.get("corrections") == 0,
                 "baseline unexpectedly corrected", failures)
        _require(runtime.get("repeat_canonical_equal") is True,
                 "same-build baseline canonical equality failed", failures)
        _require(runtime.get("vanilla_outcome_equal") is True,
                 "authored-winner vanilla control mismatch", failures)
    return failures


def evaluate_matrix(candidate_manifest: Path, output_dir: Path,
                    dll_sha256: str, schema_sha256: str,
                    runner_sha256: str,
                    expected_artifacts: dict[str, Any] | None = None) -> dict[str, Any]:
    row_results: list[dict[str, Any]] = []
    for row in build_rows(candidate_manifest):
        path = output_dir / f"{row.row_id}.json"
        if not path.is_file():
            failures = ["report missing"]
        else:
            failures = evaluate_row(row, json.loads(path.read_text(encoding="utf-8")),
                                    dll_sha256, schema_sha256, runner_sha256,
                                    expected_artifacts)
        row_results.append({"row_id": row.row_id,
                            "display_map_name": row.display_map_name,
                            "result": "pass" if not failures else "fail",
                            "failures": failures})
    failures = sum(bool(row["failures"]) for row in row_results)
    return {"report_schema": 2, "kind": "offline_matrix_evaluation",
            "certifying": failures == 0, "result": "pass" if failures == 0 else "fail",
            "expected_rows": 51, "passed_rows": 51 - failures,
            "failed_rows": failures, "rows": row_results}
