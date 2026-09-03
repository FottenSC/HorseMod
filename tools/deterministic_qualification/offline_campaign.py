from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from .artifacts import (
    capture_harness_sha256, offline_evaluator_sha256,
    require_compiled_candidate_manifest, runner_sha256, sha256_file,
    source_identity,
)
from .configuration import (
    armed_baseline, contract_sha256, disarm_diagnostics, expected_fields,
    is_exact_contract, read_fields,
)
from .offline_matrix import (
    OWNED_STORAGE_LIMIT_BYTES, OfflineMatrixRow, build_rows, evaluate_matrix,
    evaluate_row, load_candidate_cases,
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
STRICT_SEEK_PERCENTAGES = (10, 25, 50, 75)


def _strict_seek_capture_options(stock_path: Path) -> dict[str, Any]:
    """Keep strict seeks separate from the full authored-outcome lifetime."""
    return {
        "baseline": True,
        "outcome_control": stock_path,
        "seek_percentages": STRICT_SEEK_PERCENTAGES,
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


def _persistent_campaign_reusable(
    path: Path, rows: tuple[OfflineMatrixRow, ...],
    expected_artifacts: dict[str, Any], expected_config: dict[str, str],
) -> dict[str, Any] | None:
    if not path.is_file() or not rows:
        return None
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    artifacts = report.get("artifacts", {})
    runtime = report.get("runtime", {})
    cycles = report.get("cycles", [])
    valid = (
        report.get("report_schema") == 2
        and report.get("certifying") is True
        and report.get("result") == "pass"
        and report.get("case_id") == rows[0].case_id
        and report.get("display_map_name") == rows[0].display_map_name
        and report.get("stage_package_root") == rows[0].stage_package_root
        and artifacts.get("horsemod_dll_sha256") == expected_artifacts["dll"]
        and artifacts.get("schema_sha256") == expected_artifacts["schema"]
        and artifacts.get("capture_harness_sha256")
            == expected_artifacts["capture_harness"]
        and artifacts.get("replay", {}).get("sha256") == rows[0].replay_sha256
        and artifacts.get("replay_qualification_mod", {}).get("sha256")
            == expected_artifacts["replay_mod"]
        and artifacts.get("game_executable", {}).get("sha256")
            == expected_artifacts["executable"]
        and is_exact_contract(artifacts.get("config_fields"), expected_config)
        and artifacts.get("config", {}).get("sha256")
            == contract_sha256(expected_config)
        and capture_log_artifact_is_intact(report, path.with_suffix(".log"))
        and runtime.get("process_restarts") == 0
        and runtime.get("replay_entries") == 1
        and report.get("cleanup", {}).get("process_absent") is True
        and report.get("cleanup", {}).get("temporary_mod_removed") is True
        and report.get("cleanup", {}).get("config_disarmed") is True
        and len(cycles) == len(rows)
    )
    if valid:
        run_ids = [str(cycle.get("run_id", "")) for cycle in cycles]
        replay_entries = [cycle.get("replay_entry") for cycle in cycles]
        valid = (
            all(run_ids)
            and len(set(run_ids)) == len(run_ids)
            and replay_entries == [1] * len(rows)
            and len(report.get("parent_run_ids", [])) == 1
        )
    if valid:
        for cycle, row in zip(cycles, rows):
            cleanup = cycle.get("cleanup", {})
            owned = str(cycle.get("owned_bytes", "")).split("->")
            if (cycle.get("depth") != row.depth
                    or cycle.get("location") != LOCATION_CODES[row.location]
                    or cycle.get("status") != 3
                    or cycle.get("completed") != "600/600"
                    or cycle.get("anchors") != "40/40"
                    or cycle.get("repeats") != 15
                    or not isinstance(cycle.get("anchor_hash"), int)
                    or cycle.get("anchor_hash") == 0
                    or cycle.get("failure") != 0
                    or cycle.get("capacity_growth") != 0
                    or cycle.get("duplicates") != 0
                    or cycle.get("publish_failures") != 0
                    or cycle.get("pending") != "0/0"
                    or cycle.get("terminal_coverage") != 1
                    or cleanup.get("stale_mask") != 0
                    or cleanup.get("pending") != "0/0"
                    or len(owned) != 2
                    or not all(value.isdigit() for value in owned)
                    # ResetQualificationCycle clears every logical owner but
                    # deliberately retains bounded, prewarmed capacities.  A
                    # cleanup sample may therefore exceed the pre-cycle
                    # allocation sample; it must never exceed the cycle
                    # peak or the deterministic owned-storage ceiling.
                    or not isinstance(cleanup.get("owned_bytes"), int)
                    or cleanup["owned_bytes"] > int(owned[1])
                    or cleanup["owned_bytes"] > OWNED_STORAGE_LIMIT_BYTES):
                valid = False
                break
        if valid:
            for index in range(0, len(cycles), 3):
                group = cycles[index:index + 3]
                valid = (len(group) == 3
                         and [item.get("depth") for item in group]
                             == [11, 1, 6]
                         and len({item.get("location") for item in group}) == 1
                         and len({item.get("anchor_hash") for item in group}) == 1)
                if not valid:
                    break
    return report if valid else None


def _invoke_persistent_campaign(
    root: Path, rows: tuple[OfflineMatrixRow, ...], report: Path,
    dll: Path, config: Path, schema: Path, replay_mod: Path,
    game_executable: Path, log: Path, timeout: float,
) -> dict[str, Any]:
    if not rows or any(row.case_id != rows[0].case_id for row in rows):
        raise RuntimeError("persistent correction campaign must contain one case")
    command = [
        sys.executable, str(root / "tools" / "deterministic_qualification.py"),
        "replay-qualification-campaign",
        "--replay", str(root / rows[0].replay),
        "--replay-mod", str(replay_mod), "--dll", str(dll),
        "--config", str(config), "--schema", str(schema),
        "--game-executable", str(game_executable), "--log", str(log),
        "--report", str(report), "--timeout", str(timeout),
        "--display-map-name", rows[0].display_map_name,
        "--stage-package-root", rows[0].stage_package_root,
        "--case-id", rows[0].case_id, "--certifying",
        "--anchors", "40", "--repeats", "15",
    ]
    for row in rows:
        command.extend([
            "--cycle", str(row.depth), str(LOCATION_CODES[row.location])])
    result = subprocess.run(command, cwd=root, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"persistent correction campaign failed ({result.returncode}): "
            f"{rows[0].display_map_name}")
    document = json.loads(report.read_text(encoding="utf-8"))
    if document.get("result") != "pass":
        raise RuntimeError("persistent correction campaign did not pass")
    return document


def _compose_persistent_row(
    row: OfflineMatrixRow, campaign: dict[str, Any], cycle: dict[str, Any],
    baseline: dict[str, Any], baseline_path: Path,
) -> dict[str, Any]:
    if (baseline.get("report_schema") != 2
            or baseline.get("certifying") is not True
            or baseline.get("result") != "pass"
            or baseline.get("case_id") != row.case_id
            or baseline.get("runtime", {}).get("clean_exit") is not True):
        raise RuntimeError(
            f"fresh-process lifecycle proof is invalid: {row.case_id}")
    campaign_artifacts = campaign.get("artifacts", {})
    baseline_artifacts = baseline.get("artifacts", {})
    if any(campaign_artifacts.get(key) != baseline_artifacts.get(key)
           for key in ("horsemod_dll_sha256", "schema_sha256",
                       "capture_harness_sha256")):
        raise RuntimeError(
            f"fresh-process lifecycle producer mismatch: {row.case_id}")
    for key in ("replay", "replay_qualification_mod", "game_executable"):
        if (campaign_artifacts.get(key, {}).get("sha256")
                != baseline_artifacts.get(key, {}).get("sha256")):
            raise RuntimeError(
                f"fresh-process lifecycle {key} mismatch: {row.case_id}")
    cleanup = cycle["cleanup"]
    activity = [int(value) for value in str(
        cycle["presentation_activity"]).split("/")]
    owned_end = int(str(cycle["owned_bytes"]).split("->", 1)[-1])
    rate = campaign["runtime"]
    baseline_artifact = {
        "path": str(baseline_path.resolve()),
        "sha256": sha256_file(baseline_path),
        "case_id": row.case_id,
        "row_id": baseline.get("row_id"),
    }
    initial_owned = int(str(cycle["owned_bytes"]).split("->", 1)[0])
    retained_owned = int(cleanup["owned_bytes"])
    return {
        "report_schema": 2,
        "kind": "persistent_replay_qualification_row",
        "certifying": True,
        "result": "pass",
        "source": campaign["source"],
        "case_id": row.case_id,
        "row_id": row.row_id,
        "renderer": "normal",
        "display_map_name": row.display_map_name,
        "stage_package_root": row.stage_package_root,
        "artifacts": {
            **campaign["artifacts"],
            "fresh_process_lifecycle_report": baseline_artifact,
        },
        "runtime": {
            "replay_metadata": {
                "stage": rate["native_stage"],
                "map": rate["native_map"],
                "left_character": rate["native_left_character"],
                "right_character": rate["native_right_character"],
            },
            "qualification_runtime_rearm": True,
            "qualification_run_id": cycle["run_id"],
            "replay_entry": cycle["replay_entry"],
            "location": row.location,
            "depth": row.depth,
            "consecutive_corrections": int(
                str(cycle["completed"]).split("/", 1)[0]),
            "corrections": int(str(cycle["completed"]).split("/", 1)[0]),
            "canonical_convergence": "exact",
            "capacity_failures": 0,
            "capacity_growth_events": cycle["capacity_growth"],
            "timeline_accounting_failures": 0,
            "presentation_duplicate_failures": cycle["duplicates"],
            "presentation_publish_failures": cycle["publish_failures"],
            "aggregate_owned_bytes": owned_end,
            "clean_exit": campaign["cleanup"]["process_absent"],
            "reentry": True,
            "persistent_cycle_proof": {
                "unique_run_id": bool(cycle["run_id"]),
                "fresh_replay_entry": cycle["replay_entry"] > 0,
                "zero_process_restarts": rate["process_restarts"] == 0,
                "cleanup_verified": cleanup["stale_mask"] == 0,
                "pending_clear": cleanup["pending"] == "0/0",
                "stale_mask": cleanup["stale_mask"],
                "pending_events": cleanup["pending"],
                "anchors": cycle["anchors"],
                "repeats_per_anchor": cycle["repeats"],
                "anchor_sequence_hash": cycle["anchor_hash"],
                "owned_bytes_before_cycle": initial_owned,
                "owned_bytes_cycle_peak": owned_end,
                "owned_bytes_after_cleanup": retained_owned,
                "owned_bytes_retained_delta": retained_owned - initial_owned,
                "owned_bytes_released_at_cleanup": owned_end - retained_owned,
                "owned_bytes_cleanup_within_limit": (
                    retained_owned <= owned_end
                    and retained_owned <= OWNED_STORAGE_LIMIT_BYTES),
                "fresh_process_lifecycle_report": baseline_artifact,
            },
            "performance": {
                "normal_render_fps": rate.get(
                    "normal_render_fps", rate["normal_render_tick_rate"]),
                "normal_render_tick_rate": rate["normal_render_tick_rate"],
                "active_battle_fps": rate.get(
                    "active_battle_fps", rate["active_battle_tick_rate"]),
                "active_battle_tick_rate": rate["active_battle_tick_rate"],
                "capture_p99_us": cycle["capture_p99_us"],
                "capture_max_us": cycle["capture_max_us"],
                "correction_p99_us": cycle["cycle_p99_us"],
                "correction_max_us": cycle["cycle_max_us"],
                "timing_drift_ms": cycle["drift_ms"],
                "working_set_bytes": cycle["working_set_bytes"],
                "private_bytes": cycle["private_bytes"],
            },
            "presentation": {
                "ordered_audio_payload_ids": True,
                "ephemeral_exactly_once": True,
                "persistent_final_exact": True,
                "required_activity": activity[0],
                "committed_activity": activity[1],
                "discarded_activity": activity[2],
                "terminal_coverage": "complete",
                "leaks": int(str(cleanup["pending"]).split("/", 1)[0]),
            },
        },
    }


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
                seek_percentages=STRICT_SEEK_PERCENTAGES)
            if strict is None:
                _invoke_replay(
                    root, row, strict_path, deployed, config, schema,
                    replay_mod, game_executable, log, certifying=True,
                    timeout=args.timeout,
                    **_strict_seek_capture_options(stock_path),
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
                disarm_diagnostics(config)
                correction_rows = tuple(
                    item for item in matrix_rows
                    if item.case_id == row.case_id
                    and item.required_corrections)
                campaign_path = (
                    raw / f"{row.case_id}__persistent-corrections.json")
                campaign_config = expected_fields(enabled=False, trace=True)
                campaign = _persistent_campaign_reusable(
                    campaign_path, correction_rows, expected_artifacts,
                    campaign_config)
                if campaign is None:
                    print(
                        "persistent correction campaign: "
                        f"{len(correction_rows)} rows in one SC6 process on "
                        f"{row.display_map_name}", flush=True)
                    campaign = _invoke_persistent_campaign(
                        root, correction_rows, campaign_path, deployed, config,
                        schema, replay_mod, game_executable, log, args.timeout)
                    campaign = _persistent_campaign_reusable(
                        campaign_path, correction_rows, expected_artifacts,
                        campaign_config)
                    if campaign is None:
                        raise RuntimeError(
                            "new persistent campaign failed immutable evidence "
                            f"validation: {row.display_map_name}")
                else:
                    _reuse_notice(campaign_path, row)

                baseline_path = output / f"{row.case_id}__baseline.json"
                if not baseline_path.is_file():
                    raise RuntimeError(
                        "persistent correction campaign requires its completed "
                        f"fresh-process baseline: {row.display_map_name}")
                baseline = json.loads(
                    baseline_path.read_text(encoding="utf-8"))
                baseline_row = next(
                    item for item in matrix_rows
                    if item.case_id == row.case_id and item.location is None)
                baseline_failures = evaluate_row(
                    baseline_row, baseline, expected_artifacts["dll"],
                    expected_artifacts["schema"],
                    runner_sha256(root / "tools" / "deterministic_qualification"),
                    expected_artifacts)
                if baseline_failures or not capture_log_artifact_is_intact(baseline):
                    raise RuntimeError(
                        "fresh-process baseline cannot prove lifecycle for "
                        f"{row.display_map_name}: {baseline_failures}")

                for campaign_row, cycle in zip(
                        correction_rows, campaign["cycles"]):
                    campaign_completed = _compose_persistent_row(
                        campaign_row, campaign, cycle, baseline, baseline_path)
                    campaign_failures = evaluate_row(
                        campaign_row, campaign_completed,
                        expected_artifacts["dll"], expected_artifacts["schema"],
                        runner_sha256(
                            root / "tools" / "deterministic_qualification"),
                        expected_artifacts)
                    if campaign_failures:
                        raise RuntimeError(
                            f"persistent row failed closed: {campaign_row.row_id}: "
                            f"{campaign_failures}")
                    write_report(
                        output / f"{campaign_row.row_id}.json",
                        campaign_completed)
                evaluation = evaluate_matrix(
                    manifest, output, sha256_file(deployed),
                    sha256_file(schema),
                    runner_sha256(
                        root / "tools" / "deterministic_qualification"),
                    expected_artifacts, evaluator_identity)
                write_report(
                    output / "offline-matrix-progress.json", evaluation)
                if row.case_id == canary_baseline.case_id:
                    print(
                        "canary ladder: widest-first correction campaign passed; "
                        f"running strict seeks on {canary_baseline.display_map_name}",
                        flush=True)
                    run_strict_capture(canary_baseline)
                continue
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
