from __future__ import annotations

import json
import shutil
from pathlib import Path
from typing import Any

from .artifacts import (
    capture_harness_sha256, offline_evaluator_sha256,
    require_compiled_candidate_manifest, runner_sha256, sha256_file,
    source_identity,
)
from .configuration import (
    armed_baseline, armed_correction, disarm_diagnostics, expected_fields,
    read_fields,
)
from .offline_matrix import (
    OfflineMatrixRow, build_rows, evaluate_matrix, evaluate_row,
    load_candidate_cases,
)
from .offline_capture import (
    capture_log_artifact_is_intact,
    invoke_replay as _invoke_replay,
    load_reusable_capture as _load_reusable_capture,
)
from .process_control import list_game_processes
from .report import write_report


LOCATION_CODES = {
    "near_round_start": 1,
    "active_combat": 2,
    "confirmed_hit": 3,
    "round_end": 4,
}


def _reuse_notice(path: Path, row: OfflineMatrixRow) -> None:
    print(f"hash-safe reuse: {row.row_id} on {row.display_map_name} ({path.name})",
          flush=True)


def _finish_row(row: OfflineMatrixRow, primary: dict[str, Any],
                reentry: dict[str, Any]) -> dict[str, Any]:
    if (primary.get("report_schema") != 2
            or primary.get("certifying") is not True
            or primary.get("result") != "pass"):
        raise RuntimeError(f"primary row is not certifying/pass: {row.row_id}")
    if (reentry.get("report_schema") != 2
            or reentry.get("certifying") is not True
            or reentry.get("result") != "pass"
            or reentry.get("runtime", {}).get("native_replay_import_ready") is not True
            or reentry.get("runtime", {}).get("clean_exit") is not True):
        raise RuntimeError(f"certifying re-entry failed: {row.row_id}")
    primary_runtime = primary.get("runtime", {})
    reentry_runtime = reentry.get("runtime", {})
    if (primary_runtime.get("native_replay_import_ready") is not True
            or primary_runtime.get("clean_exit") is not True
            or not primary_runtime.get("run_id")
            or not reentry_runtime.get("run_id")
            or primary_runtime["run_id"] == reentry_runtime["run_id"]):
        raise RuntimeError(f"distinct clean process lifecycle missing: {row.row_id}")
    primary_artifacts = primary.get("artifacts", {})
    reentry_artifacts = reentry.get("artifacts", {})
    for key in ("horsemod_dll_sha256", "schema_sha256", "runner_sha256"):
        if primary_artifacts.get(key) != reentry_artifacts.get(key):
            raise RuntimeError(f"re-entry {key} mismatch: {row.row_id}")
    for key in ("replay", "replay_qualification_mod", "game_executable"):
        if (primary_artifacts.get(key, {}).get("sha256")
                != reentry_artifacts.get(key, {}).get("sha256")):
            raise RuntimeError(f"re-entry {key} mismatch: {row.row_id}")
    if primary.get("source") != reentry.get("source"):
        raise RuntimeError(f"re-entry source identity mismatch: {row.row_id}")
    report = dict(primary)
    runtime = dict(primary["runtime"])
    runtime["reentry"] = True
    runtime["reentry_run_id"] = reentry["runtime"]["run_id"]
    runtime["reentry_proof"] = {
        "distinct_run_id": True,
        "native_import_ready": True,
        "clean_exit": True,
        "temporary_mod_removed":
            reentry_runtime.get("temporary_mod_removed") is True,
        "process_absent_after_exit":
            reentry_runtime.get("process_absent_after_exit") is True,
    }
    report["runtime"] = runtime
    return report


def run_offline_campaign(args: Any, root: Path) -> int:
    candidate = args.dll.resolve()
    deployed = args.deployed_dll.resolve()
    config = args.config.resolve()
    schema = args.schema.resolve()
    replay_mod = args.replay_mod.resolve()
    game_executable = args.game_executable.resolve()
    log = args.log.resolve()
    manifest = args.case_manifest.resolve()
    output = args.output_dir.resolve()
    for path, label in ((candidate, "candidate DLL"), (config, "config"),
                        (schema, "schema"), (replay_mod, "replay bridge"),
                        (game_executable, "game executable"), (manifest, "case manifest")):
        if not path.is_file():
            raise FileNotFoundError(f"{label} not found: {path}")
    if list_game_processes():
        raise RuntimeError("SC6 must be closed before offline campaign deployment")
    if shutil.disk_usage(root).free < 10 * 1024**3:
        raise RuntimeError("less than 10 GiB free; refusing artifact-heavy offline campaign")
    if source_identity(root)["dirty"]:
        raise RuntimeError("offline certification requires the frozen source identity")
    require_compiled_candidate_manifest(schema, manifest)
    output.mkdir(parents=True, exist_ok=True)
    if (deployed.parent / "production-allowlist.ini").exists():
        raise RuntimeError("production allowlist must remain absent during offline qualification")
    deployed.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(candidate, deployed)
    if sha256_file(candidate) != sha256_file(deployed):
        raise RuntimeError("deployed DLL hash differs from immutable candidate")

    matrix_rows = build_rows(manifest)
    canary_baseline = matrix_rows[0]
    canary_correction = next(
        row for row in matrix_rows
        if row.case_id == canary_baseline.case_id
        and row.location == "active_combat")
    # Front-load the smallest representative ladder. The remaining matrix is
    # still identical, but a bad candidate now fails before dozens of launches.
    rows = (canary_baseline, canary_correction, *(
        row for row in matrix_rows
        if row not in (canary_baseline, canary_correction)))
    expected_artifacts = {
        "source": source_identity(root),
        "dll": sha256_file(deployed),
        "schema": sha256_file(schema),
        "capture_harness": capture_harness_sha256(root),
        "replay_mod": sha256_file(replay_mod),
        "executable": sha256_file(game_executable),
    }
    evaluator_identity = offline_evaluator_sha256(root)
    raw = output / "raw"
    raw.mkdir(exist_ok=True)

    def run_strict_capture(row: OfflineMatrixRow) -> Path:
        stock_path = raw / f"{row.row_id}-vanilla.json"
        strict_path = output / f"{row.case_id}__strict-seeks.json"
        with armed_baseline(config):
            strict = _load_reusable_capture(
                strict_path, row, expected_artifacts,
                expected_fields(enabled=False, trace=True), stock=False,
                outcome_control=stock_path,
                seek_percentages=(10, 25, 50, 75))
            if strict is None:
                _invoke_replay(
                    root, row, strict_path, deployed, config, schema,
                    replay_mod, game_executable, log, certifying=True,
                    baseline=True, outcome_control=stock_path,
                    require_authored_outcomes=True,
                    seek_percentages=(10, 25, 50, 75),
                    timeout=args.timeout,
                )
            else:
                _reuse_notice(strict_path, row)
        return strict_path

    current_row: OfflineMatrixRow | None = None
    try:
        for row in rows:
            current_row = row
            completed_path = output / f"{row.row_id}.json"
            if completed_path.is_file():
                try:
                    completed_report = json.loads(
                        completed_path.read_text(encoding="utf-8"))
                except (OSError, UnicodeError, json.JSONDecodeError):
                    completed_report = {}
                if (capture_log_artifact_is_intact(completed_report)
                        and not evaluate_row(
                        row, completed_report, expected_artifacts["dll"],
                        expected_artifacts["schema"],
                        runner_sha256(root / "tools" / "deterministic_qualification"),
                        expected_artifacts)):
                    _reuse_notice(completed_path, row)
                    if row == canary_correction:
                        print("canary ladder: reused correction; validating "
                              f"strict seeks on {canary_baseline.display_map_name}",
                              flush=True)
                        run_strict_capture(canary_baseline)
                    continue
            print(f"offline qualification: {row.row_id} on {row.display_map_name}", flush=True)
            if row.required_corrections == 0:
                disarm_diagnostics(config)
                stock_path = raw / f"{row.row_id}-vanilla.json"
                stock_config = expected_fields(enabled=False, trace=False)
                stock = _load_reusable_capture(
                    stock_path, row, expected_artifacts, stock_config, stock=True)
                if stock is None:
                    stock = _invoke_replay(root, row, stock_path,
                        deployed, config, schema, replay_mod, game_executable, log,
                        certifying=True, stock=True, timeout=args.timeout)
                else:
                    _reuse_notice(stock_path, row)
                # replay-entry deliberately disarms diagnostics after every
                # process, including a successful one. Give each independent
                # baseline its own ownership scope so the second process
                # cannot inherit the first process's safe, trace=false exit
                # state after its automatic smoke restores the caller config.
                with armed_baseline(config):
                    first_path = raw / f"{row.row_id}-first.json"
                    baseline_config = expected_fields(enabled=False, trace=True)
                    first = _load_reusable_capture(
                        first_path, row, expected_artifacts, baseline_config,
                        stock=False, outcome_control=stock_path)
                    if first is None:
                        first = _invoke_replay(root, row, first_path,
                            deployed, config, schema, replay_mod,
                            game_executable, log, certifying=True, baseline=True,
                            outcome_control=stock_path,
                            require_authored_outcomes=True, timeout=args.timeout)
                    else:
                        _reuse_notice(first_path, row)
                with armed_baseline(config):
                    second_path = raw / f"{row.row_id}-repeat.json"
                    second = _load_reusable_capture(
                        second_path, row, expected_artifacts, baseline_config,
                        stock=False, outcome_control=stock_path)
                    if second is None:
                        second = _invoke_replay(root, row, second_path,
                            deployed, config, schema, replay_mod,
                            game_executable, log, certifying=True, baseline=True,
                            outcome_control=stock_path,
                            require_authored_outcomes=True, timeout=args.timeout)
                    else:
                        _reuse_notice(second_path, row)
                first_hash = first["runtime"]["final_canonical"]["sha256"]
                second_hash = second["runtime"]["final_canonical"]["sha256"]
                first["runtime"]["repeat_canonical_equal"] = first_hash == second_hash
                first_presentation = first["runtime"]["presentation"]
                second_presentation = second["runtime"]["presentation"]
                presentation_equal = (
                    first_presentation.get("identity") is not None
                    and first_presentation.get("identity")
                        == second_presentation.get("identity")
                )
                first_presentation["ordered_audio_payload_ids"] = (
                    first_presentation.get("ordered_audio_payload_ids") is True
                    and second_presentation.get("ordered_audio_payload_ids") is True
                    and presentation_equal
                )
                first_presentation["ephemeral_exactly_once"] = (
                    first_presentation.get("ephemeral_exactly_once") is True
                    and second_presentation.get("ephemeral_exactly_once") is True
                    and presentation_equal
                )
                first_presentation["persistent_final_exact"] = (
                    first_presentation.get("persistent_final_exact") is True
                    and second_presentation.get("persistent_final_exact") is True
                    and presentation_equal
                )
                first["runtime"]["vanilla_outcome_equal"] = (
                    first["runtime"]["stock_round_outcome"]
                    == stock["runtime"]["stock_round_outcome"]
                )
                completed = _finish_row(row, first, second)
            else:
                stock_path = raw / f"{row.case_id}__baseline-vanilla.json"
                baseline_row = next(item for item in rows
                    if item.case_id == row.case_id and item.location is None)
                disarm_diagnostics(config)
                stock = _load_reusable_capture(
                    stock_path, baseline_row, expected_artifacts,
                    expected_fields(enabled=False, trace=False), stock=True)
                if stock is None:
                    stock = _invoke_replay(
                        root, baseline_row, stock_path, deployed, config, schema,
                        replay_mod, game_executable, log, certifying=True,
                        stock=True, timeout=args.timeout)
                else:
                    _reuse_notice(stock_path, baseline_row)
                with armed_correction(config, row.depth, LOCATION_CODES[row.location]):
                    primary_path = raw / f"{row.row_id}-primary.json"
                    correction_config = expected_fields(
                        enabled=False, trace=True, forced_depth7=True,
                        depth=row.depth, location=LOCATION_CODES[row.location])
                    primary = _load_reusable_capture(
                        primary_path, row, expected_artifacts,
                        correction_config, stock=False,
                        outcome_control=stock_path)
                    if primary is None:
                        primary = _invoke_replay(root, row, primary_path,
                            deployed, config, schema, replay_mod,
                            game_executable, log,
                            # Matrix rows certify presentation authored by this
                            # exact replay on its native map.
                            certifying=True, outcome_control=stock_path,
                            require_authored_outcomes=True,
                            timeout=args.timeout)
                    else:
                        _reuse_notice(primary_path, row)
                with armed_baseline(config):
                    reentry_path = raw / f"{row.row_id}-reentry.json"
                    reentry = _load_reusable_capture(
                        reentry_path, row, expected_artifacts,
                        expected_fields(enabled=False, trace=True), stock=False,
                        outcome_control=stock_path)
                    if reentry is None:
                        reentry = _invoke_replay(root, row, reentry_path,
                            deployed, config, schema, replay_mod,
                            game_executable, log, certifying=True, baseline=True,
                            outcome_control=stock_path,
                            require_authored_outcomes=True, timeout=args.timeout)
                    else:
                        _reuse_notice(reentry_path, row)
                completed = _finish_row(row, primary, reentry)
            write_report(completed_path, completed)
            evaluation = evaluate_matrix(manifest, output,
                sha256_file(deployed), sha256_file(schema),
                runner_sha256(root / "tools" / "deterministic_qualification"),
                expected_artifacts, evaluator_identity)
            write_report(output / "offline-matrix-progress.json", evaluation)
            if row == canary_correction:
                print("canary ladder: correction passed; running strict seeks "
                      f"on {canary_baseline.display_map_name}", flush=True)
                run_strict_capture(canary_baseline)
    except Exception as error:
        try:
            log_lines = log.read_text(
                encoding="utf-8", errors="replace").splitlines()[-200:]
        except OSError:
            log_lines = []
        failure = {
            "report_schema": 1,
            "kind": "offline_matrix_failure",
            "result": "failed",
            "reason": str(error),
            "row": None if current_row is None else {
                "row_id": current_row.row_id,
                "case_id": current_row.case_id,
                "display_map_name": current_row.display_map_name,
                "stage_package_root": current_row.stage_package_root,
                "location": current_row.location,
                "mode": current_row.mode,
                "depth": current_row.depth,
            },
            "artifacts": {
                "candidate_dll_sha256": sha256_file(candidate),
                "deployed_dll_sha256": sha256_file(deployed),
                "schema_sha256": sha256_file(schema),
                "replay_mod_sha256": sha256_file(replay_mod),
                "runner_sha256": runner_sha256(
                    root / "tools" / "deterministic_qualification"),
                "source": source_identity(root),
                "config_fields": read_fields(config),
            },
            "log_tail": log_lines,
        }
        write_report(output / "offline-matrix-failure.json", failure)
        raise
    finally:
        disarm_diagnostics(config)
        if list_game_processes():
            raise RuntimeError("offline campaign cleanup left SC6 running")
    strict_results: list[dict[str, Any]] = []
    try:
        for case in load_candidate_cases(manifest):
            row = next(item for item in rows
                       if item.case_id == case["case_id"] and item.location is None)
            strict_path = run_strict_capture(row)
            strict_results.append({
                "case_id": case["case_id"],
                "display_map_name": case["native_display_name"],
                "report": str(strict_path.resolve()),
                "result": "pass",
            })
    finally:
        disarm_diagnostics(config)
        if list_game_processes():
            raise RuntimeError("strict replay cleanup left SC6 running")
    evaluation = evaluate_matrix(manifest, output,
        sha256_file(deployed), sha256_file(schema),
        runner_sha256(root / "tools" / "deterministic_qualification"),
        expected_artifacts, evaluator_identity)
    evaluation["strict_replay_gates_complete"] = len(strict_results) == 3
    evaluation["strict_replay_gates"] = strict_results
    write_report(args.report, evaluation)
    if (not evaluation["certifying"]
            or not evaluation["strict_replay_gates_complete"]):
        raise RuntimeError("one or more offline matrix rows failed strict evaluation")
    return 0
