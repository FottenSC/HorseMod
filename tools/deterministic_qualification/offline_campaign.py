from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from .artifacts import (
    require_compiled_candidate_manifest, runner_sha256, sha256_file,
    source_identity,
)
from .configuration import (
    armed_baseline, armed_correction, disarm_diagnostics, read_fields,
)
from .offline_matrix import OfflineMatrixRow, build_rows, evaluate_matrix, load_candidate_cases
from .process_control import list_game_processes
from .report import write_report


LOCATION_CODES = {
    "near_round_start": 1,
    "active_combat": 2,
    "confirmed_hit": 3,
    "round_end": 4,
}


def _invoke_replay(
    root: Path,
    row: OfflineMatrixRow,
    report: Path,
    dll: Path,
    config: Path,
    schema: Path,
    replay_mod: Path,
    game_executable: Path,
    log: Path,
    *,
    certifying: bool,
    stock: bool = False,
    baseline: bool = False,
    stage_terminal: str | None = None,
    outcome_control: Path | None = None,
    require_authored_outcomes: bool = False,
    seek_percentages: tuple[int, ...] = (),
    timeout: float,
) -> dict[str, Any]:
    replay_path = root / row.replay
    if sha256_file(replay_path) != row.replay_sha256:
        raise RuntimeError(f"frozen replay hash mismatch: {row.row_id}")
    command = [
        sys.executable, str(root / "tools" / "deterministic_qualification.py"),
        "replay-entry", "--replay", str(replay_path),
        "--replay-mod", str(replay_mod), "--dll", str(dll),
        "--config", str(config), "--schema", str(schema),
        "--game-executable", str(game_executable), "--log", str(log),
        "--report", str(report), "--timeout", str(timeout),
        "--watch-frames", "600" if certifying else "1",
        "--case-id", row.case_id, "--row-id", row.row_id,
        "--display-map-name", row.display_map_name,
        "--stage-package-root", row.stage_package_root,
    ]
    if certifying:
        command.extend([
            "--certifying",
            # Qualify performance over the same full 600-frame workload as
            # the row, after the authored replay has actually begun. The
            # takeover plan requires a 600-frame timing gate, so the shorter
            # 120-frame sample was not certifying evidence.
            "--min-resume-tick-rate", "58",
            "--resume-tick-window", "600",
        ])
    else:
        command.append("--allow-dirty")
    if stock:
        command.append("--stock-round-outcome-control")
    if baseline:
        command.append("--deterministic-baseline")
    if stage_terminal is not None:
        command.extend(["--stage-terminal", stage_terminal])
    if outcome_control is not None:
        command.extend(["--outcome-control-report", str(outcome_control)])
    if require_authored_outcomes:
        command.append("--require-authored-outcomes")
    if seek_percentages:
        command.extend(["--seek-percentages", *map(str, seek_percentages)])
    if row.required_corrections:
        command.append("--require-presentation-coverage")
    if row.location is not None:
        command.extend(["--correction-location", row.location])
    result = subprocess.run(command, cwd=root, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"offline row subprocess failed ({result.returncode}): {row.row_id}")
    document = json.loads(report.read_text(encoding="utf-8"))
    if document.get("result") != "pass":
        raise RuntimeError(f"offline row report failed: {row.row_id}")
    return document


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

    rows = build_rows(manifest)
    expected_artifacts = {
        "source": source_identity(root),
        "replay_mod": sha256_file(replay_mod),
        "executable": sha256_file(game_executable),
    }
    raw = output / "raw"
    raw.mkdir(exist_ok=True)
    current_row: OfflineMatrixRow | None = None
    try:
        for row in rows:
            current_row = row
            print(f"offline qualification: {row.row_id} on {row.display_map_name}", flush=True)
            if row.required_corrections == 0:
                disarm_diagnostics(config)
                stock_path = raw / f"{row.row_id}-vanilla.json"
                stock = _invoke_replay(root, row, stock_path,
                    deployed, config, schema, replay_mod, game_executable, log,
                    certifying=True, stock=True, timeout=args.timeout)
                with armed_baseline(config):
                    first = _invoke_replay(root, row, raw / f"{row.row_id}-first.json",
                        deployed, config, schema, replay_mod, game_executable, log,
                        certifying=True, baseline=True, outcome_control=stock_path,
                        require_authored_outcomes=True, timeout=args.timeout)
                    second = _invoke_replay(root, row, raw / f"{row.row_id}-repeat.json",
                        deployed, config, schema, replay_mod, game_executable, log,
                        certifying=True, baseline=True, outcome_control=stock_path,
                        require_authored_outcomes=True, timeout=args.timeout)
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
                if not stock_path.is_file():
                    raise RuntimeError(
                        f"same-replay vanilla oracle missing for {row.display_map_name}")
                with armed_correction(config, row.depth, LOCATION_CODES[row.location]):
                    primary = _invoke_replay(root, row, raw / f"{row.row_id}-primary.json",
                        deployed, config, schema, replay_mod, game_executable, log,
                        certifying=True, stage_terminal="both",
                        outcome_control=stock_path, require_authored_outcomes=True,
                        timeout=args.timeout)
                with armed_baseline(config):
                    reentry = _invoke_replay(root, row, raw / f"{row.row_id}-reentry.json",
                        deployed, config, schema, replay_mod, game_executable, log,
                        certifying=True, baseline=True, outcome_control=stock_path,
                        require_authored_outcomes=True, timeout=args.timeout)
                completed = _finish_row(row, primary, reentry)
            write_report(output / f"{row.row_id}.json", completed)
            evaluation = evaluate_matrix(manifest, output,
                sha256_file(deployed), sha256_file(schema),
                runner_sha256(root / "tools" / "deterministic_qualification"),
                expected_artifacts)
            write_report(output / "offline-matrix-progress.json", evaluation)
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
            stock_path = raw / f"{row.row_id}-vanilla.json"
            strict_path = output / f"{case['case_id']}__strict-seeks.json"
            with armed_baseline(config):
                _invoke_replay(
                    root, row, strict_path, deployed, config, schema, replay_mod,
                    game_executable, log, certifying=True, baseline=True,
                    outcome_control=stock_path, require_authored_outcomes=True,
                    seek_percentages=(10, 25, 50, 75), timeout=args.timeout,
                )
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
        expected_artifacts)
    evaluation["strict_replay_gates_complete"] = len(strict_results) == 3
    evaluation["strict_replay_gates"] = strict_results
    write_report(args.report, evaluation)
    if (not evaluation["certifying"]
            or not evaluation["strict_replay_gates_complete"]):
        raise RuntimeError("one or more offline matrix rows failed strict evaluation")
    return 0
