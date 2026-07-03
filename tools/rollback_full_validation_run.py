#!/usr/bin/env python3
"""Run the rollback validation bundle.

This is the pinned review-bundle command for the rollback branch. By default it
runs the local self-tests, in-game request-file lab gates, and the strict replay
seek regression. Live online traffic capture remains opt-in because it requires
an actual SC6 online peer/match.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
BUILD_DIR = REPO / "build_cmake_LessEqual421__Shipping__Win64"
HORSE_BUILD_DIR = BUILD_DIR / "HorseMod"
REPORT_DIR = REPO / "reports" / "rollback_validation"
REPLAY_FILE = REPO / "ReplayExample" / "REPLAY_12744704008398858106.bin"
DEFAULT_ACTIVATION_SOURCE_PEER = 0xA0
DEFAULT_ACTIVATION_DESTINATION_PEER = 0xB0
DEFAULT_ACTIVATION_SESSION_ID = 0x4C495645414354

FAULT_PROFILES = [
    "all",
    "clean_0ms",
    "wifi_50ms_jitter",
    "bad_wifi_120ms_5pct_loss",
    "overseas_180ms_2pct_loss",
    "spike_every_10s",
    "burst_loss_500ms",
    "corrupt_probe",
]


SELFTESTS = [
    HORSE_BUILD_DIR / "RollbackGekkoGameplayInputSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackGekkoSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackGekkoUdpSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackInputCacheAdapterSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackTransportSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackFaultInjectSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackOnlineSessionSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackLiveTransportSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackLivePeerPipelineSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackEndToEndSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackLiveActivationSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackLiveActivationExecutorSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackLiveBoundarySelfTest.exe",
    HORSE_BUILD_DIR / "RollbackStockTransportSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackStockTransportObserveSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackLiveOnlineCaptureSelfTest.exe",
    HORSE_BUILD_DIR / "RollbackSnapshotSelfTest.exe",
]

SELFTEST_TARGETS = [
    "RollbackGekkoGameplayInputSelfTest",
    "RollbackGekkoSelfTest",
    "RollbackGekkoUdpSelfTest",
    "RollbackInputCacheAdapterSelfTest",
    "RollbackTransportSelfTest",
    "RollbackFaultInjectSelfTest",
    "RollbackOnlineSessionSelfTest",
    "RollbackLiveTransportSelfTest",
    "RollbackLivePeerPipelineSelfTest",
    "RollbackEndToEndSelfTest",
    "RollbackLiveActivationSelfTest",
    "RollbackLiveActivationExecutorSelfTest",
    "RollbackLiveBoundarySelfTest",
    "RollbackStockTransportSelfTest",
    "RollbackStockTransportObserveSelfTest",
    "RollbackLiveOnlineCaptureSelfTest",
    "RollbackSnapshotSelfTest",
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
    proc = subprocess.run(
        cmd,
        cwd=str(REPO),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    return {
        "name": name,
        "cmd": cmd,
        "returncode": proc.returncode,
        "elapsed_seconds": round(time.time() - started, 3),
        "ok": proc.returncode == 0,
        "output": proc.stdout,
    }


def selftest_command(exe: Path, fault_profile: str) -> list[str]:
    cmd = [str(exe)]
    if exe.name == "RollbackFaultInjectSelfTest.exe" and fault_profile != "all":
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
    targets = " ".join(SELFTEST_TARGETS)
    return [
        "cmd",
        "/c",
        "call E:\\ProgramFiles\\vsStudioCommunity\\VC\\Auxiliary\\Build\\"
        f"vcvars64.bat >nul && cmake --build {BUILD_DIR} "
        f"--target {targets} --parallel",
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
        "lux-no-render",
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
        result["live_online_summary"] = json.loads(
            live_summary_path.read_text(encoding="utf-8"))
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
        trace_result["live_online_trace_summary"] = json.loads(
            trace_summary_path.read_text(encoding="utf-8"))
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
    for attempt_index in range(retries + 1):
        warm_process = attempt_index > 0
        result = run_command(
            "strict-replay",
            replay_command(warm_process=warm_process),
            900)
        result["warm_process_retry"] = warm_process
        attempts.append(result)
        if result["ok"] or not strict_replay_retryable(result):
            break
    final = dict(attempts[-1])
    final["name"] = "strict-replay"
    final["attempt_count"] = len(attempts)
    final["retry_count"] = len(attempts) - 1
    final["attempts"] = attempts
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
    p.add_argument("--include-live-online", action="store_true")
    p.add_argument("--live-online-only", action="store_true")
    p.add_argument("--require-live-activation-candidate", action="store_true")
    p.add_argument("--live-online-watch-seconds", type=float)
    p.add_argument("--profile", choices=FAULT_PROFILES, default="all",
                   help="Fault-injected same-machine network profile to run")
    p.add_argument("--strict-replay-retries", type=int, default=2)
    p.add_argument("--watch-seconds", type=float, default=25.0)
    p.add_argument("--output-dir", type=Path, default=REPORT_DIR)
    args = p.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    run_id = now_id()
    report_path = args.output_dir / f"rollback_validation_{run_id}.json"
    results: list[dict[str, object]] = []
    live_online_watch_seconds = (
        args.live_online_watch_seconds
        if args.live_online_watch_seconds is not None else
        180.0 if args.live_online_only else
        args.watch_seconds
    )

    if not args.live_online_only and not args.skip_build:
        results.append(run_command("preflight-close-game", close_game_command(),
                                   60))
        results.append(run_command("build-and-deploy", build_command(), 600))
        results.append(
            run_command("build-selftests", build_selftests_command(), 300))

    if not args.live_online_only and not args.skip_selftests:
        for exe in SELFTESTS:
            if not exe.exists():
                results.append({
                    "name": exe.name,
                    "cmd": [str(exe)],
                    "returncode": 127,
                    "elapsed_seconds": 0,
                    "ok": False,
                    "output": f"missing executable: {exe}",
                })
                continue
            results.append(
                run_command(
                    exe.name,
                    selftest_command(exe, args.profile),
                    180 if exe.name == "RollbackFaultInjectSelfTest.exe"
                    else 120,
                ))

    if not args.live_online_only and not args.skip_game_labs:
        for case, require_flag in LAB_CASES:
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
                    result["live_online_summary"] = json.loads(
                        live_summary_path.read_text(encoding="utf-8"))
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
                        trace_result["live_online_trace_summary"] = json.loads(
                            trace_summary_path.read_text(encoding="utf-8"))
                    pending_trace_result = trace_result
            results.append(result)
            if pending_trace_result is not None:
                results.append(pending_trace_result)

    if not args.live_online_only and not args.skip_replay:
        results.append(
            run_strict_replay_with_retries(
                max(0, args.strict_replay_retries)))

    if args.include_live_online or args.live_online_only:
        append_live_online_capture(
            results,
            run_id=run_id,
            output_dir=args.output_dir,
            watch_seconds=live_online_watch_seconds,
            require_activation_candidate=(
                args.require_live_activation_candidate),
        )

    ok = all(bool(r["ok"]) for r in results)
    summary = {
        "ok": ok,
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "repo": str(REPO),
        "run_id": run_id,
        "request_id": run_id,
        "mode": "live-online-only" if args.live_online_only else "full",
        "fault_profile": args.profile,
        "results": results,
    }
    report_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"rollback validation {'PASS' if ok else 'FAIL'}")
    print(f"report={report_path}")
    for r in results:
        status = "PASS" if r["ok"] else "FAIL"
        print(f"{status} {r['name']} ({r['elapsed_seconds']}s)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
