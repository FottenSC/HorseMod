from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from .artifacts import runner_sha256, sha256_file, source_identity
from .configuration import (
    armed_baseline, contract_sha256, disarm_diagnostics, expected_fields,
    is_exact_contract,
)
from .process_control import list_game_processes
from .report import write_report


def _load_cases(path: Path) -> list[dict[str, Any]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    cases = document.get("cases")
    if document.get("schema_version") != 3 or not isinstance(cases, list):
        raise RuntimeError("Tira manifest must be schema v3")
    if len(cases) < 3:
        raise RuntimeError("Tira manifest must contain positive and control replays")
    required = {
        "case_id", "replay", "stage_package_root", "native_display_name",
        "contains_tira", "replay_sha256", "replay_metadata_stage",
        "replay_metadata_map", "role",
    }
    if any(not isinstance(case, dict) or not required <= set(case) for case in cases):
        raise RuntimeError("Tira manifest case is incomplete")
    roles = {case["role"] for case in cases}
    required_roles = {
        "rng_helpers_3250_3251_success", "non_rng_stance_change",
        "non_tira_control",
    }
    if not required_roles <= roles:
        raise RuntimeError(
            "Tira manifest needs a current-build RNG-helper success replay, "
            "a non-RNG stance-change control, and a non-Tira control")
    return cases


def _int_field(coverage: dict[str, Any], name: str) -> int:
    value = coverage.get(name)
    if isinstance(value, str):
        return int(value, 0)
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    raise RuntimeError(f"Tira coverage field is missing or malformed: {name}")


def evaluate_tira_reports(cases: list[dict[str, Any]], reports: list[dict[str, Any]],
                          dll_hash: str, schema_hash: str,
                          runner_hash: str,
                          expected_artifacts: dict[str, Any] | None = None) -> dict[str, Any]:
    failures: list[str] = []
    rows: list[dict[str, Any]] = []
    transition_runs = 0
    expected_count = len(cases) * 2
    if len(reports) != expected_count:
        failures.append(f"expected {expected_count} reports, found {len(reports)}")
    by_case: dict[str, list[dict[str, Any]]] = {}
    for report in reports:
        by_case.setdefault(str(report.get("case_id", "")), []).append(report)
    exact_names = (
        "xorshift_draws", "known_callers", "unknown_callers", "if_draws",
        "xorshift_sequence", "transition07_sequence", "tira_sequence",
        "tira_random_transitions", "tira_rng_stance_changes",
        "tira_probability_batches", "tira_targets",
        "tira_last_target",
        "tira_writer_calls", "tira_stance_changes", "tira_writer_sequence",
        "tira_writer_slot_mask", "tira_last_writer_move",
        "tira_stance_batches", "tira_slot_mask", "state19_sequence_p0",
        "state19_sequence_p1", "state19_initial_p0", "state19_initial_p1",
        "state19_final_p0", "state19_final_p1", "xorshift_landing",
        "state19_at_tira_transition_p0", "state19_at_tira_transition_p1",
        "state19_initial_valid",
        "tira_helper_attempts", "tira_helper_exact_draws",
        "tira_helper_writes", "tira_helper_no_write",
        "tira_helper_no_change", "tira_helper_signature_failures",
        "tira_helper_last_enclosing_move", "tira_helper_last_chance",
        "tira_helper_last_result", "tira_helper_last_rejection_mask",
    )
    for case in cases:
        case_failures: list[str] = []
        pair = by_case.get(case["case_id"], [])
        if len(pair) != 2:
            case_failures.append("two repeat reports are required")
        for report in pair:
            runtime = report.get("runtime", {})
            artifacts = report.get("artifacts", {})
            coverage = runtime.get("gameplay_rng_coverage")
            presentation = runtime.get("presentation", {})
            performance = runtime.get("performance", {})
            if (report.get("result") != "pass" or report.get("certifying") is not True):
                case_failures.append("report is not a certifying pass")
            if report.get("renderer") != "normal":
                case_failures.append("renderer is not normal")
            if (report.get("display_map_name") != case["native_display_name"]
                    or report.get("stage_package_root") != case["stage_package_root"]):
                case_failures.append("authored map identity mismatch")
            if (artifacts.get("horsemod_dll_sha256") != dll_hash
                    or artifacts.get("schema_sha256") != schema_hash
                    or artifacts.get("runner_sha256") != runner_hash):
                case_failures.append("immutable artifact identity mismatch")
            if artifacts.get("replay", {}).get("sha256") != case["replay_sha256"]:
                case_failures.append("frozen Tira replay hash mismatch")
            metadata = runtime.get("replay_metadata", {})
            if (metadata.get("stage") != case["replay_metadata_stage"]
                    or metadata.get("map") != case["replay_metadata_map"]):
                case_failures.append("native Tira replay map metadata mismatch")
            if expected_artifacts is not None:
                if report.get("source") != expected_artifacts["source"]:
                    case_failures.append("Tira source identity mismatch")
                if (artifacts.get("replay_qualification_mod", {}).get("sha256")
                        != expected_artifacts["replay_mod"]):
                    case_failures.append("Tira replay bridge hash mismatch")
                if artifacts.get("game_executable", {}).get("sha256") \
                        != expected_artifacts["executable"]:
                    case_failures.append("Tira game executable hash mismatch")
            if (presentation.get("ordered_audio_payload_ids") is not True
                    or presentation.get("ephemeral_exactly_once") is not True
                    or presentation.get("persistent_final_exact") is not True
                    or presentation.get("identity") is None):
                case_failures.append("Tira presentation identity is incomplete")
            config = artifacts.get("config_fields", {})
            expected_config = expected_fields(enabled=False, trace=True)
            if not is_exact_contract(config, expected_config):
                case_failures.append("Tira qualification config contract mismatch")
            if (artifacts.get("config", {}).get("sha256")
                    != contract_sha256(expected_config)):
                case_failures.append("Tira qualification config byte contract mismatch")
            if runtime.get("stock_round_outcome") is None:
                case_failures.append("authored round/final winner proof missing")
            if runtime.get("final_canonical") is None:
                case_failures.append("final canonical proof missing")
            if (performance.get("independent_clocks") is not True
                    or performance.get("normal_render_fps", 0) <= 59.0
                    or performance.get("normal_render_tick_rate", 0) <= 59.0
                    or performance.get("active_battle_fps", 0) <= 59.0
                    or performance.get("active_battle_tick_rate", 0) <= 59.0):
                case_failures.append(
                    "independent active-battle FPS/TPS budget failed")
            if (runtime.get("capacity_failures") != 0
                    or runtime.get("capacity_growth_events") != 0
                    or runtime.get("timeline_accounting_failures") != 0
                    or runtime.get("presentation_duplicate_failures") != 0
                    or runtime.get("presentation_publish_failures") != 0):
                case_failures.append("Tira runtime allocation/identity health failed")
            if runtime.get("aggregate_owned_bytes", 10**18) > 576 * 1024**2:
                case_failures.append("Tira aggregate owned storage exceeded 576 MiB")
            if not isinstance(coverage, dict):
                case_failures.append("gameplay RNG coverage missing")
                continue
            try:
                if _int_field(coverage, "unknown_callers") != 0:
                    case_failures.append("unknown gameplay RNG caller observed")
                transition = (
                    case["role"] == "rng_helpers_3250_3251_success"
                    and _int_field(coverage, "if_draws") > 0
                    and _int_field(coverage, "xorshift_draws") > 0
                    and _int_field(coverage, "tira_probability_batches") > 0
                    and _int_field(coverage, "tira_random_transitions") > 0
                    and _int_field(coverage, "tira_stance_batches") > 0
                    and _int_field(coverage, "tira_targets") > 0
                    and _int_field(coverage, "tira_slot_mask") > 0
                    and _int_field(coverage, "tira_writer_calls") > 0
                    and _int_field(coverage, "tira_writer_sequence") > 0
                    and _int_field(coverage, "tira_writer_slot_mask") > 0
                    and _int_field(coverage, "tira_helper_attempts") > 0
                    and _int_field(coverage, "tira_helper_exact_draws") > 0
                    and _int_field(coverage, "tira_helper_writes") > 0
                    and _int_field(
                        coverage, "tira_helper_signature_failures") == 0
                )
                if transition:
                    transition_runs += 1
                    slot_mask = _int_field(coverage, "tira_slot_mask")
                    if slot_mask == 0 or _int_field(coverage, "tira_targets") == 0:
                        case_failures.append("Tira target/slot identity missing")
                    if _int_field(coverage, "tira_last_target") == 0:
                        case_failures.append("exact Tira target is missing")
                    if ((_int_field(coverage, "tira_writer_slot_mask")
                            & slot_mask) != slot_mask):
                        case_failures.append(
                            "Tira state19 writer slot identity is incomplete")
                    for slot, field in ((1, "state19_at_tira_transition_p0"),
                                        (2, "state19_at_tira_transition_p1")):
                        if slot_mask & slot and _int_field(coverage, field) not in (0, 1):
                            case_failures.append("Tira state19 did not land in Gloomy/Jolly")
                    if coverage.get("state19_initial_valid") is not True:
                        case_failures.append("initial Tira state19 was not captured")
                    if (_int_field(coverage, "xorshift_sequence") == 0
                            or _int_field(coverage, "tira_sequence") == 0):
                        case_failures.append(
                            "ordered Tira RNG/transition identity is empty")
                elif case["role"] == "rng_helpers_3250_3251_success":
                    case_failures.append(
                        "authored Tira replay did not execute the exact random transition")
                elif case["role"] == "non_rng_stance_change":
                    if (not case["contains_tira"]
                            or _int_field(coverage, "tira_stance_changes") == 0
                            or _int_field(coverage, "tira_random_transitions") != 0):
                        case_failures.append(
                            "non-RNG stance-change control did not remain distinct")
                elif case["role"] == "non_tira_control":
                    if (case["contains_tira"]
                            or _int_field(coverage, "tira_random_transitions") != 0
                            or _int_field(coverage, "tira_helper_attempts") != 0):
                        case_failures.append("non-Tira control observed Tira helper activity")
            except RuntimeError as error:
                case_failures.append(str(error))
        if len(pair) == 2:
            left = pair[0].get("runtime", {})
            right = pair[1].get("runtime", {})
            left_coverage = left.get("gameplay_rng_coverage", {})
            right_coverage = right.get("gameplay_rng_coverage", {})
            if any(left_coverage.get(name) != right_coverage.get(name)
                   for name in exact_names):
                case_failures.append("repeat RNG/transition sequence mismatch")
            if left.get("final_canonical") != right.get("final_canonical"):
                case_failures.append("repeat final canonical mismatch")
            if left.get("stock_round_outcome") != right.get("stock_round_outcome"):
                case_failures.append("repeat authored outcome mismatch")
            if left.get("presentation") != right.get("presentation"):
                case_failures.append("repeat presentation identity mismatch")
        rows.append({
            "case_id": case["case_id"],
            "display_map_name": case["native_display_name"],
            "result": "pass" if not case_failures else "fail",
            "failures": sorted(set(case_failures)),
        })
        failures.extend(f"{case['case_id']}: {reason}" for reason in set(case_failures))
    if transition_runs == 0:
        failures.append(
            "no Tira helper 0x3250/0x3251 RNG/state19 transition was observed")
    return {
        "report_schema": 3,
        "kind": "tira_rng_transition_evaluation",
        "certifying": not failures,
        "result": "pass" if not failures else "fail",
        "transition_runs": transition_runs,
        "rows": rows,
        "failures": failures,
    }


def run_tira_campaign(args: Any, root: Path) -> int:
    cases = _load_cases(args.case_manifest.resolve())
    if list_game_processes():
        raise RuntimeError("SC6 must be closed before Tira qualification")
    if shutil.disk_usage(root).free < 10 * 1024**3:
        raise RuntimeError("less than 10 GiB free; refusing Tira campaign")
    if source_identity(root)["dirty"]:
        raise RuntimeError("Tira certification requires frozen deterministic sources")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    reports: list[dict[str, Any]] = []
    event_restore_reports: list[dict[str, Any]] = []
    try:
        for case in cases:
            replay_path = root / case["replay"]
            if sha256_file(replay_path) != case["replay_sha256"]:
                raise RuntimeError(f"frozen Tira replay hash mismatch: {case['case_id']}")
            control_path = (
                args.output_dir / f"{case['case_id']}-stock-control.json"
            )
            control_command = [
                sys.executable,
                str(root / "tools" / "deterministic_qualification.py"),
                "replay-entry", "--replay", str(replay_path),
                "--dll", str(args.dll), "--replay-mod", str(args.replay_mod),
                "--config", str(args.config), "--schema", str(args.schema),
                "--log", str(args.log),
                "--game-executable", str(args.game_executable),
                "--report", str(control_path), "--timeout", str(args.timeout),
                "--watch-frames", "1", "--certifying",
                "--stock-round-outcome-control",
                "--case-id", case["case_id"],
                "--display-map-name", case["native_display_name"],
                "--stage-package-root", case["stage_package_root"],
            ]
            disarm_diagnostics(args.config)
            subprocess.run(control_command, cwd=root, check=True)
            for repeat in range(2):
                report_path = args.output_dir / f"{case['case_id']}-repeat-{repeat + 1}.json"
                command = [
                    sys.executable, str(root / "tools" / "deterministic_qualification.py"),
                    "replay-entry", "--replay", str(replay_path),
                    "--dll", str(args.dll), "--replay-mod", str(args.replay_mod),
                    "--config", str(args.config), "--schema", str(args.schema),
                    "--log", str(args.log), "--game-executable", str(args.game_executable),
                    "--report", str(report_path), "--timeout", str(args.timeout),
                    "--watch-frames", "600", "--certifying",
                    "--deterministic-baseline", "--require-authored-outcomes",
                    "--outcome-control-report", str(control_path),
                    "--case-id", case["case_id"],
                    "--display-map-name", case["native_display_name"],
                    "--stage-package-root", case["stage_package_root"],
                ]
                if case["role"] == "rng_helpers_3250_3251_success":
                    command.append("--require-tira-probability-transition")
                elif case["role"] == "non_rng_stance_change":
                    command.append("--require-tira-stance-change")
                with armed_baseline(args.config):
                    subprocess.run(command, cwd=root, check=True)
                reports.append(json.loads(report_path.read_text(encoding="utf-8")))
            if case["role"] == "rng_helpers_3250_3251_success":
                for suffix, location, anchors, repeats in (
                    ("event-restores", 5, 1, 15),
                    ("production-cadence", 6, 15, 1),
                ):
                    event_path = args.output_dir / (
                        f"{case['case_id']}-{suffix}.json")
                    event_command = [
                        sys.executable,
                        str(root / "tools" / "deterministic_qualification.py"),
                        "replay-qualification-campaign", "--replay",
                        str(replay_path), "--dll", str(args.dll),
                        "--replay-mod", str(args.replay_mod), "--config",
                        str(args.config), "--schema", str(args.schema), "--log",
                        str(args.log), "--game-executable",
                        str(args.game_executable), "--report", str(event_path),
                        "--timeout", str(args.timeout), "--certifying",
                        "--anchors", str(anchors), "--repeats", str(repeats),
                        "--cycle", "11", str(location), "--cycle", "1",
                        str(location), "--cycle", "6", str(location),
                        "--case-id", case["case_id"], "--display-map-name",
                        case["native_display_name"], "--stage-package-root",
                        case["stage_package_root"], "--min-resume-tick-rate",
                        "59.001",
                    ]
                    disarm_diagnostics(args.config)
                    subprocess.run(event_command, cwd=root, check=True)
                    event_report = json.loads(
                        event_path.read_text(encoding="utf-8"))
                    runtime = event_report.get("runtime", {})
                    cycles = event_report.get("cycles", [])
                    expected = anchors * repeats
                    valid_cycles = (
                        len(cycles) == 3
                        and [cycle.get("depth") for cycle in cycles]
                            == [11, 1, 6]
                        and all(cycle.get("location") == location
                                and cycle.get("completed")
                                    == f"{expected}/{expected}"
                                and cycle.get("failure") == 0
                                for cycle in cycles)
                    )
                    if (event_report.get("certifying") is not True
                            or event_report.get("result") != "pass"
                            or event_report.get("depth_order") != [11, 1, 6]
                            or event_report.get("qualification_anchors") != anchors
                            or event_report.get(
                                "qualification_repeats_per_anchor") != repeats
                            or runtime.get(
                                "independent_performance_clocks") is not True
                            or min(runtime.get("normal_render_fps", 0),
                                   runtime.get("normal_render_tick_rate", 0),
                                   runtime.get("active_battle_fps", 0),
                                   runtime.get("active_battle_tick_rate", 0))
                                <= 59.0
                            or not valid_cycles):
                        raise RuntimeError(
                            f"Tira {suffix} 11/1/6 restore gate failed")
                    event_restore_reports.append(event_report)
    finally:
        disarm_diagnostics(args.config)
        if list_game_processes():
            raise RuntimeError("Tira campaign cleanup left SC6 running")
    evaluation = evaluate_tira_reports(
        cases, reports, sha256_file(args.dll), sha256_file(args.schema),
        runner_sha256(root / "tools" / "deterministic_qualification"),
        {"source": source_identity(root),
         "replay_mod": sha256_file(args.replay_mod),
         "executable": sha256_file(args.game_executable)},
    )
    evaluation["artifacts"] = {
        "horsemod_dll_sha256": sha256_file(args.dll),
        "schema_sha256": sha256_file(args.schema),
        "runner_sha256": runner_sha256(
            root / "tools" / "deterministic_qualification"),
        "tira_manifest_sha256": sha256_file(args.case_manifest),
        "replay_qualification_mod_sha256": sha256_file(args.replay_mod),
        "game_executable_sha256": sha256_file(args.game_executable),
        "evidence_reports": [
            {
                "path": str((args.output_dir / (
                    f"{case['case_id']}-repeat-{repeat + 1}.json")).resolve()),
                "sha256": sha256_file(args.output_dir / (
                    f"{case['case_id']}-repeat-{repeat + 1}.json")),
                "replay_sha256": case["replay_sha256"],
            }
            for case in cases for repeat in range(2)
        ],
        "event_restore_reports": [
            {
                "path": str((args.output_dir / f"{case['case_id']}-{suffix}.json").resolve()),
                "sha256": sha256_file(
                    args.output_dir / f"{case['case_id']}-{suffix}.json"),
                "replay_sha256": case["replay_sha256"],
            }
            for case in cases
            if case["role"] == "rng_helpers_3250_3251_success"
            for suffix in ("event-restores", "production-cadence")
        ],
    }
    evaluation["event_restore_runs"] = len(event_restore_reports)
    if len(event_restore_reports) != 2 * sum(
            case["role"] == "rng_helpers_3250_3251_success"
            for case in cases):
        evaluation["certifying"] = False
        evaluation["result"] = "fail"
        evaluation["failures"].append(
            "exact-event repeated-restore evidence is incomplete")
    evaluation["source"] = source_identity(root)
    write_report(args.report, evaluation)
    if not evaluation["certifying"]:
        raise RuntimeError("Tira RNG transition qualification failed")
    return 0
