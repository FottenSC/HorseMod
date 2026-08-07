#!/usr/bin/env python3
"""Run the non-release rollback developer validation bundle.

This is the pinned review-bundle command for the rollback branch. By default it
runs the local self-tests, in-game request-file lab gates, and the strict replay
seek regression. It cannot certify beta: the only release authority is
rollback_two_client_acceptance_run.py --beta-release-gate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from rollback_ctest import (
    RollbackCTest,
    RollbackCTestError,
    discover_rollback_selftests,
)
from rollback_report_contract import artifact, contract_fields, coverage, utc_now


REPO = Path(__file__).resolve().parents[1]
BUILD_DIR = REPO / "build_cmake_LessEqual421__Shipping__Win64"
HORSE_BUILD_DIR = BUILD_DIR / "HorseMod"
REPORT_DIR = REPO / "reports" / "rollback_validation"
REPLAY_FILE = REPO / "ReplayExample" / "REPLAY_12744704008398858106.bin"
DEPLOYED_DLL = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\ue4ss\Mods\HorseMod\dlls\main.dll"
)
DEFAULT_ACTIVATION_SOURCE_PEER = 0xA0
DEFAULT_ACTIVATION_DESTINATION_PEER = 0xB0
DEFAULT_ACTIVATION_SESSION_ID = 0x4C495645414354

FAULT_PROFILES = [
    "all",
    "clean_0ms",
    "wifi_50ms_jitter",
    "bad_wifi_120ms_5pct_loss",
    "overseas_180ms_2pct_loss",
    "wired_intercontinental_200ms_rtt",
    "spike_every_10s",
    "burst_loss_500ms",
    "corrupt_probe",
]


PYTHON_SELFTESTS = [
    REPO / "tools" / "sc6_launch_catalog_selftest.py",
    REPO / "tools" / "replay_input_script_selftest.py",
    REPO / "tools" / "rollback_golden_trace.py",
    REPO / "tools" / "rollback_physical_case_run.py",
    REPO / "tools" / "rollback_two_machine_qualification.py",
    REPO / "tools" / "rollback_two_client_acceptance_run.py",
]


LAB_CASES = [
    ("gekko-gameplay-input", "--require-gekko-gameplay-input"),
    ("gekko-adapter", "--require-gekko-adapter"),
    ("gekko-udp", "--require-gekko-udp"),
    ("live-transport", "--require-live-transport"),
    ("live-peer-pipeline", "--require-live-peer-pipeline"),
    ("end-to-end", "--require-end-to-end"),
    ("live-activation", "--require-live-activation"),
    ("live-activation-executor", "--require-live-activation-executor"),
    ("stock-transport", "--require-stock-transport"),
    ("stock-observe", "--require-stock-observe"),
    ("live-online-capture", "--require-live-online-capture"),
]


def now_id() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S-%f")


def run_command(name: str, cmd: list[str], timeout: int) -> dict[str, object]:
    started = time.time()
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(REPO),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        return {
            "name": name,
            "cmd": cmd,
            "returncode": 124,
            "elapsed_seconds": round(time.time() - started, 3),
            "ok": False,
            "output": output + f"\ncommand timed out after {timeout}s",
            "failure_classification": "infrastructure-timeout",
        }
    return {
        "name": name,
        "cmd": cmd,
        "returncode": proc.returncode,
        "elapsed_seconds": round(time.time() - started, 3),
        "ok": proc.returncode == 0,
        "output": proc.stdout,
    }


def selftest_command(
    test: RollbackCTest, fault_profile: str
) -> list[str]:
    cmd = list(test.command)
    if test.name == "RollbackFaultInjectSelfTest" and fault_profile != "all":
        cmd.extend(["--profile", fault_profile])
    return cmd


def failed_result(name: str, output: str) -> dict[str, object]:
    return {
        "name": name,
        "cmd": [],
        "returncode": 1,
        "elapsed_seconds": 0,
        "ok": False,
        "output": output,
    }


def load_json_result(path: Path, owner: dict[str, object], key: str) -> None:
    """Attach JSON or turn malformed/missing evidence into a durable failure."""
    try:
        owner[key] = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        owner["ok"] = False
        owner["failure_classification"] = "invalid-json-artifact"
        owner["output"] = str(owner.get("output", "")) + \
            f"\n{key} invalid: {type(exc).__name__}:{exc}"


def output_field(output: str, field: str) -> str | None:
    match = re.search(rf"^{re.escape(field)}=(.+)$", output, re.MULTILINE)
    if not match:
        return None
    return match.group(1).strip()


def output_label(output: str, label: str) -> str | None:
    match = re.search(rf"^{re.escape(label)}:\s*(.+)$", output, re.MULTILINE)
    if not match:
        return None
    return match.group(1).strip()


def build_command() -> list[str]:
    return ["cmd", "/c", str(REPO / "build_and_deploy.bat")]


def close_game_command() -> list[str]:
    return [
        sys.executable,
        str(REPO / "tools" / "replay_seek_test_run.py"),
        "--kill-game",
        "--force-kill-game",
        "--kill-timeout",
        "10",
    ]


def build_selftests_command() -> list[str]:
    return [
        "cmd",
        "/d", "/s", "/c",
        str(REPO / "build_horse_mod.bat"),
        "HorseModRollbackSelfTests",
    ]


def lab_command(
    case: str,
    require_flag: str,
    watch_seconds: float,
    *,
    request_id: str,
    live_summary_path: Path | None = None,
) -> list[str]:
    cmd = [
        sys.executable,
        str(REPO / "tools" / "rollback_lab_test_run.py"),
        "--kill-game",
        "--launch-game",
        "--case",
        case,
        "--request-id",
        request_id,
        "--trace",
        "--watch-seconds",
        str(watch_seconds),
        "--strict",
        require_flag,
        "--kill-after",
        "--cleanup-request-after",
    ]
    if live_summary_path is not None:
        cmd.extend(["--live-online-summary-output", str(live_summary_path)])
    return cmd


def replay_command(*, warm_process: bool = False) -> list[str]:
    cmd = [
        sys.executable,
        str(REPO / "tools" / "replay_seek_test_run.py"),
        "--allow-unknown-presence",
        "--start-replay",
        str(REPLAY_FILE),
        "--timeline-generation-mode",
        "normal",
        "--case-preset",
        "watch",
        "--watch-frames",
        "600",
        "--wait",
        "--analyze",
        "--strict",
        "--min-resume-tick-rate",
        "58",
        "--resume-tick-window",
        "120",
        "--max-seek-validation-seconds",
        "0.5",
    ]
    if not warm_process:
        cmd[2:2] = ["--kill-game", "--launch-game"]
    return cmd


def trusted_golden_matrix_command(report_dir: Path) -> list[str]:
    return [
        sys.executable,
        str(REPO / "tools" / "rollback_golden_matrix.py"),
        "--candidate-dll",
        str(HORSE_BUILD_DIR / "HorseMod.dll"),
        "--deployed-dll",
        str(DEPLOYED_DLL),
        "--report-dir",
        str(report_dir),
    ]


def live_online_command(
    watch_seconds: float,
    *,
    request_id: str,
    live_summary_path: Path | None = None,
    require_activation_candidate: bool = False,
    activation_source_peer: int = DEFAULT_ACTIVATION_SOURCE_PEER,
    activation_destination_peer: int = DEFAULT_ACTIVATION_DESTINATION_PEER,
    activation_session_id: int = DEFAULT_ACTIVATION_SESSION_ID,
) -> list[str]:
    cmd = [
        sys.executable,
        str(REPO / "tools" / "rollback_lab_test_run.py"),
        "--case",
        "live-online-capture",
        "--request-id",
        request_id,
        "--trace",
        "--watch-seconds",
        str(watch_seconds),
        "--strict",
        "--require-live-online-capture",
        "--require-live-online-traffic",
        "--cleanup-request-after",
    ]
    if require_activation_candidate:
        cmd.extend([
            "--arm-live-activation",
            "--activation-source-peer",
            f"0x{activation_source_peer:X}",
            "--activation-destination-peer",
            f"0x{activation_destination_peer:X}",
            "--activation-session-id",
            f"0x{activation_session_id:X}",
            "--require-live-activation-candidate",
        ])
    if live_summary_path is not None:
        cmd.extend(["--live-online-summary-output", str(live_summary_path)])
    return cmd


def append_live_online_capture(
    results: list[dict[str, object]],
    *,
    run_id: str,
    output_dir: Path,
    watch_seconds: float,
    require_activation_candidate: bool,
) -> None:
    live_summary_path = output_dir / f"live_online_capture_live_{run_id}.json"
    result = run_command(
        "live-online-capture",
        live_online_command(
            watch_seconds,
            request_id=run_id,
            live_summary_path=live_summary_path,
            require_activation_candidate=require_activation_candidate,
        ),
        int(watch_seconds) + 30,
    )
    result["live_online_summary_path"] = str(live_summary_path)
    if live_summary_path.exists():
        load_json_result(live_summary_path, result, "live_online_summary")
    results.append(result)

    trace_text = output_field(str(result.get("output", "")), "trace_file")
    if not trace_text:
        results.append(
            failed_result(
                "analyze-live-online-capture-trace-live",
                "missing trace_file=... from live-online-capture",
            ))
        return

    trace_path = Path(trace_text)
    trace_summary_path = (
        output_dir / f"live_online_capture_trace_live_{run_id}.json"
    )
    trace_result = run_command(
        "analyze-live-online-capture-trace-live",
        trace_analyze_command(
            trace_path,
            trace_summary_path,
            require_live_traffic=True,
            require_activation_candidate=require_activation_candidate,
            request_id=run_id,
        ),
        60,
    )
    trace_result["live_online_trace_path"] = str(trace_path)
    trace_result["live_online_trace_summary_path"] = str(trace_summary_path)
    if not trace_summary_path.exists():
        trace_result["ok"] = False
        trace_result["output"] += "\nmissing live online trace summary JSON"
    else:
        load_json_result(
            trace_summary_path, trace_result, "live_online_trace_summary")
    results.append(trace_result)


def trace_analyze_command(
    trace_path: Path,
    output_path: Path,
    *,
    require_live_traffic: bool,
    require_activation_candidate: bool = False,
    request_id: str | None = None,
    activation_source_peer: int = DEFAULT_ACTIVATION_SOURCE_PEER,
    activation_destination_peer: int = DEFAULT_ACTIVATION_DESTINATION_PEER,
    activation_session_id: int = DEFAULT_ACTIVATION_SESSION_ID,
) -> list[str]:
    cmd = [
        sys.executable,
        str(REPO / "tools" / "rollback_live_online_capture_analyze.py"),
        str(trace_path),
        "--require-case",
        "live-online-capture",
        "--output",
        str(output_path),
    ]
    if require_live_traffic:
        cmd.append("--require-live-traffic")
    else:
        cmd.append("--require-readiness")
    if require_activation_candidate:
        cmd.extend([
            "--require-live-activation-candidate",
            "--activation-source-peer",
            f"0x{activation_source_peer:X}",
            "--activation-destination-peer",
            f"0x{activation_destination_peer:X}",
            "--activation-session-id",
            f"0x{activation_session_id:X}",
        ])
    if request_id:
        cmd.extend(["--require-request-id", request_id])
    return cmd


def strict_replay_retryable_from_text(output: str) -> bool:
    strict_timing_only = (
        "tick gap spike" in output
        or "native replay tick rate too slow" in output
        or "seek landing too slow" in output
    )
    return (
        "strict failure:" in output
        and strict_timing_only
        and "state_mismatches=0" in output
        and "failed_cases=[]" in output
    )


def strict_replay_retryable_from_report(report_path: Path) -> bool:
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False

    analyzer_stdout = str(report.get("analyzer_stdout", ""))
    summary = report.get("summary", {})
    failed_cases = (
        summary.get("failed_cases") if isinstance(summary, dict) else None)
    summary_passed = bool(report.get("summary_passed")) or (
        isinstance(summary, dict) and bool(summary.get("passed")))
    failed_case_labels = report.get("failed_case_labels")
    failure_groups = report.get("failure_groups")
    state_mismatches_zero = re.search(
        r"\bstate_mismatches=0\b", analyzer_stdout) is not None
    strict_timing_only = (
        "tick gap spike" in analyzer_stdout
        or "native replay tick rate too slow" in analyzer_stdout
        or "seek landing too slow" in analyzer_stdout
    )
    return (
        summary_passed
        and not bool(report.get("final_passed"))
        and "strict failure:" in analyzer_stdout
        and strict_timing_only
        and state_mismatches_zero
        and failed_cases == 0
        and failed_case_labels == []
        and failure_groups == {}
    )


def strict_replay_retryable(result: dict[str, object]) -> bool:
    if bool(result.get("ok")):
        return False
    output = str(result.get("output", ""))
    report_text = output_label(output, "report json")
    if report_text:
        return strict_replay_retryable_from_report(Path(report_text))
    return strict_replay_retryable_from_text(output)


def run_strict_replay_with_retries(retries: int) -> dict[str, object]:
    attempts: list[dict[str, object]] = []
    retry_authorized = False
    for attempt_index in range(retries + 1):
        # A failed timing run can leave replay startup state in-flight.  Every
        # retry must reproduce the pinned cold-launch gate; warm retries both
        # changed the test and could time out while reusing stale state.
        warm_process = False
        result = run_command(
            "strict-replay",
            replay_command(warm_process=warm_process),
            900)
        result["warm_process_retry"] = warm_process
        attempts.append(result)
        if result["ok"]:
            break
        if attempt_index == 0:
            retry_authorized = strict_replay_retryable(result)
        if not retry_authorized:
            break
    final = dict(attempts[-1])
    final["name"] = "strict-replay"
    final["attempt_count"] = len(attempts)
    final["retry_count"] = len(attempts) - 1
    final["retry_authorized_by_timing_only_first_attempt"] = retry_authorized
    final["failed_attempt_count"] = sum(
        1 for attempt in attempts if not attempt["ok"]
    )
    final["attempts"] = attempts
    if not bool(attempts[0]["ok"]):
        recovered = bool(attempts[-1]["ok"])
        final["ok"] = False
        final["failure_classification"] = (
            "flaky-recovered-on-retry" if recovered else "failed"
        )
    if len(attempts) > 1:
        final["output"] = "\n\n".join(
            f"=== strict replay attempt {idx + 1} ===\n{attempt['output']}"
            for idx, attempt in enumerate(attempts)
        )
    return final


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--skip-build", action="store_true")
    p.add_argument("--skip-selftests", action="store_true")
    p.add_argument("--skip-game-labs", action="store_true")
    p.add_argument("--skip-replay", action="store_true")
    p.add_argument(
        "--diagnostic", action="store_true",
        help="permit an explicitly partial validation; never grants gate or "
             "release authority")
    p.add_argument("--include-live-online", action="store_true")
    p.add_argument("--live-online-only", action="store_true")
    p.add_argument("--require-live-activation-candidate", action="store_true")
    p.add_argument("--live-online-watch-seconds", type=float)
    p.add_argument("--simulation-profile", choices=FAULT_PROFILES, default="all",
                   help="In-process simulation fault profile to run")
    p.add_argument("--strict-replay-retries", type=int, default=2)
    p.add_argument("--watch-seconds", type=float, default=25.0)
    p.add_argument("--output-dir", type=Path, default=REPORT_DIR)
    args = p.parse_args()

    requested_skip = any((args.skip_build, args.skip_selftests,
                          args.skip_game_labs, args.skip_replay))
    if requested_skip and not args.diagnostic:
        print("full-validation skip flags require explicit --diagnostic",
              file=sys.stderr)
        return 2

    args.output_dir.mkdir(parents=True, exist_ok=True)
    run_id = now_id()
    report_path = args.output_dir / f"rollback_validation_{run_id}.json"
    results: list[dict[str, object]] = []
    registered_selftests: list[RollbackCTest] = []
    live_online_watch_seconds = (
        args.live_online_watch_seconds
        if args.live_online_watch_seconds is not None else
        180.0 if args.live_online_only else
        args.watch_seconds
    )

    try:
        from rollback_golden_trace import load_manifest
        load_manifest(REPO / "tools" / "rollback_goldens" / "manifest.json")
        results.append({
            "name": "golden-manifest-preflight", "cmd": [],
            "returncode": 0, "elapsed_seconds": 0, "ok": True,
            "output": "trusted golden manifest parsed and validated",
        })
        preflight_failed = False
    except (ImportError, OSError, UnicodeError, ValueError, RuntimeError) as exc:
        results.append(failed_result(
            "golden-manifest-preflight", f"{type(exc).__name__}:{exc}"))
        preflight_failed = True

    if not preflight_failed and not args.live_online_only and not args.skip_build:
        results.append(run_command("preflight-close-game", close_game_command(),
                                   60))
        results.append(run_command("build-and-deploy", build_command(), 600))
        results.append(
            run_command("build-selftests", build_selftests_command(), 300))

    if not preflight_failed and not args.live_online_only:
        try:
            registered_selftests = discover_rollback_selftests(BUILD_DIR)
        except RollbackCTestError as exc:
            results.append(failed_result("discover-rollback-selftests", str(exc)))

    if not preflight_failed and not args.live_online_only and not args.skip_selftests:
        for test in registered_selftests:
            results.append(
                run_command(
                    test.name,
                    selftest_command(test, args.simulation_profile),
                    180 if test.name == "RollbackFaultInjectSelfTest"
                    else 120,
                ))
        for script in PYTHON_SELFTESTS:
            if not script.exists():
                results.append(failed_result(
                    script.name, f"missing script: {script}"))
                continue
            command = [sys.executable, str(script)]
            if script.name in {
                "rollback_golden_trace.py",
                "rollback_physical_case_run.py",
                "rollback_two_machine_qualification.py",
                "rollback_two_client_acceptance_run.py",
            }:
                command.append("--selftest")
            results.append(run_command(script.name, command, 120))
        policy_lint = REPO / "tools" / \
            "rollback_two_client_acceptance_run.py"
        results.append(run_command(
            "rollback-policy-lint",
            [sys.executable, str(policy_lint), "--policy-lint"],
            120,
        ))

    if not preflight_failed and not args.live_online_only and not args.skip_game_labs:
        for case, require_flag in LAB_CASES:
            if case == "live-online-capture" and not args.include_live_online:
                continue
            case_watch_seconds = args.watch_seconds
            if case == "live-online-capture":
                case_watch_seconds = max(case_watch_seconds, 90.0)
            live_summary_path = None
            pending_trace_result: dict[str, object] | None = None
            if case == "live-online-capture":
                live_summary_path = (
                    args.output_dir
                    / f"live_online_capture_readiness_{run_id}.json"
                )
            result = run_command(
                f"lab-{case}",
                lab_command(
                    case,
                    require_flag,
                    case_watch_seconds,
                    request_id=run_id,
                    live_summary_path=live_summary_path,
                ),
                int(case_watch_seconds) + 90,
            )
            if live_summary_path is not None:
                result["live_online_summary_path"] = str(live_summary_path)
                if live_summary_path.exists():
                    load_json_result(
                        live_summary_path, result, "live_online_summary")
                trace_text = output_field(str(result.get("output", "")),
                                          "trace_file")
                if not trace_text:
                    pending_trace_result = failed_result(
                        "analyze-live-online-capture-trace",
                        "missing trace_file=... from lab-live-online-capture",
                    )
                else:
                    trace_path = Path(trace_text)
                    trace_summary_path = (
                        args.output_dir
                        / f"live_online_capture_trace_readiness_{run_id}.json"
                    )
                    trace_result = run_command(
                        "analyze-live-online-capture-trace",
                        trace_analyze_command(
                            trace_path,
                            trace_summary_path,
                            require_live_traffic=False,
                            request_id=run_id,
                        ),
                        60,
                    )
                    trace_result["live_online_trace_path"] = str(trace_path)
                    trace_result["live_online_trace_summary_path"] = (
                        str(trace_summary_path))
                    if not trace_summary_path.exists():
                        trace_result["ok"] = False
                        trace_result["output"] += (
                            "\nmissing live online trace summary JSON")
                    else:
                        load_json_result(
                            trace_summary_path, trace_result,
                            "live_online_trace_summary")
                    pending_trace_result = trace_result
            results.append(result)
            if pending_trace_result is not None:
                results.append(pending_trace_result)

    if not preflight_failed and not args.live_online_only and not args.skip_replay:
        results.append(run_command(
            "trusted-golden-matrix",
            trusted_golden_matrix_command(
                args.output_dir / f"trusted_golden_matrix_{run_id}"
            ),
            3600,
        ))
        results.append(
            run_strict_replay_with_retries(
                max(0, args.strict_replay_retries)))

    if not preflight_failed and (args.include_live_online or args.live_online_only):
        append_live_online_capture(
            results,
            run_id=run_id,
            output_dir=args.output_dir,
            watch_seconds=live_online_watch_seconds,
            require_activation_candidate=(
                args.require_live_activation_candidate),
        )

    workflow_ok = bool(results) and all(bool(r["ok"]) for r in results)
    if args.live_online_only:
        required = [
            "golden-manifest-preflight",
            "live-online-capture",
            "analyze-live-online-capture-trace-live",
        ]
        workflow_kind = "live-online-capture"
    else:
        required = [
            "golden-manifest-preflight",
            "preflight-close-game", "build-and-deploy", "build-selftests"
        ]
        required.extend(test.name for test in registered_selftests)
        required.extend(script.name for script in PYTHON_SELFTESTS)
        required.append("rollback-policy-lint")
        required.extend(
            f"lab-{case}" for case, _ in LAB_CASES
            if case != "live-online-capture"
        )
        required.append("trusted-golden-matrix")
        required.append("strict-replay")
        workflow_kind = "local-regression"
        if args.include_live_online:
            required.extend([
                "lab-live-online-capture",
                "live-online-capture",
                "analyze-live-online-capture-trace-live",
            ])
    observed = [str(result.get("name", "")) for result in results]
    strict_result = next(
        (item for item in results if item.get("name") == "strict-replay"),
        {},
    )
    strict_attempts = strict_result.get("attempts", [])
    strict_output = str(
        strict_attempts[-1].get("output", "")
        if strict_attempts else strict_result.get("output", "")
    )
    strict_report_path = Path(
        output_label(strict_output, "report json") or ""
    )
    coverage_result = coverage(required, observed)
    if args.diagnostic and requested_skip:
        coverage_result = coverage(observed, observed)
    contract = contract_fields(
        workflow_kind=workflow_kind,
        workflow_ok=workflow_ok,
        coverage_result=coverage_result,
    )
    contract["release_authority"] = False
    contract["release_qualification"] = "not-evaluated"
    contract["reason"] = (
        "developer-validation-only; pass does not grant release authority"
    )
    diagnostic_ok = bool(results) and all(bool(item.get("ok"))
                                          for item in results)
    if args.diagnostic:
        contract["diagnostic_ok"] = diagnostic_ok
        contract["gate_ok"] = False
        contract["verdict"] = (
            "diagnostic-pass" if diagnostic_ok else "diagnostic-fail")
    summary = {
        **contract,
        "generated_at": utc_now(),
        "repo": str(REPO),
        "run_id": run_id,
        "request_id": run_id,
        "mode": "live-online-only" if args.live_online_only else "full",
        "simulation_profile": args.simulation_profile,
        "requested_skips": {
            "build": args.skip_build,
            "selftests": args.skip_selftests,
            "game_labs": args.skip_game_labs,
            "replay": args.skip_replay,
        },
        "artifacts": {
            "built_dll": artifact(HORSE_BUILD_DIR / "HorseMod.dll"),
            "deployed_dll": artifact(DEPLOYED_DLL),
            "replay_input": artifact(REPLAY_FILE),
            "strict_replay_report": artifact(strict_report_path),
        },
        "results": results,
    }
    report_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    label = (
        "rollback live online capture"
        if args.live_online_only else "rollback local regression"
    )
    print(f"{label} {contract['verdict'].upper()}")
    print(f"report={report_path}")
    for r in results:
        status = "PASS" if r["ok"] else "FAIL"
        print(f"{status} {r['name']} ({r['elapsed_seconds']}s)")
    return 0 if (diagnostic_ok if args.diagnostic
                 else contract["verdict"] == "pass") else 1


if __name__ == "__main__":
    raise SystemExit(main())
