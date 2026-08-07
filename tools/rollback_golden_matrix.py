#!/usr/bin/env python3
"""Validate every trusted rollback golden against normal-render generation."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from rollback_golden_trace import (
    GoldenTraceError,
    load_manifest,
    sha256_file,
    validate_case,
)


REPO = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPO / "tools" / "rollback_goldens" / "manifest.json"
DEFAULT_CANDIDATE_DLL = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\ue4ss\Mods\HorseMod\dlls\main.dll"
)
TRACE_PATTERN = re.compile(
    r"(?m)^final: PASS .*? trace=(.+?) failed_cases=")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def resolve_manifest_path(manifest: Path, value: object) -> Path:
    raw_path = str(value or "").strip()
    if not raw_path:
        raise GoldenTraceError("golden case has no replay path")
    path = Path(raw_path)
    return path if path.is_absolute() else manifest.parent / path


def write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def run_case(
    *,
    manifest_path: Path,
    case: dict[str, Any],
    candidate_dll_sha256: str,
    report_dir: Path,
) -> dict[str, Any]:
    case_id = str(case["id"])
    replay = resolve_manifest_path(
        manifest_path, case.get("replay_path")).resolve()
    case_report_dir = report_dir / case_id
    command = [
        sys.executable,
        str(REPO / "tools" / "replay_seek_test_run.py"),
        "--kill-game",
        "--launch-game",
        "--allow-unknown-presence",
        "--start-replay",
        str(replay),
        "--timeline-generation-mode",
        "normal",
        "--generation-full-frame-trace",
        "--case-preset",
        "static",
        "--start-timeout",
        "360",
        "--timeout",
        "900",
        "--wait",
        "--analyze",
        "--report-dir",
        str(case_report_dir),
    ]
    completed = subprocess.run(
        command,
        cwd=REPO,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=1200,
        check=False,
    )
    output = completed.stdout or ""
    result: dict[str, Any] = {
        "case_id": case_id,
        "candidate_dll_sha256": candidate_dll_sha256,
        "trusted_source_commit": case.get("source_commit"),
        "trusted_dll_sha256": case.get("dll_sha256"),
        "replay": str(replay),
        "replay_sha256": sha256_file(replay) if replay.is_file() else "",
        "command": command,
        "runner_returncode": completed.returncode,
        "output_tail": output[-12000:],
        "ok": False,
    }
    if completed.returncode != 0:
        result["failure"] = "candidate-generation-failed"
        return result
    matches = TRACE_PATTERN.findall(output)
    if not matches:
        result["failure"] = "candidate-trace-path-missing"
        return result
    trace = Path(matches[-1].strip())
    try:
        validation = validate_case(
            manifest_path, case_id, trace, replay)
    except (OSError, ValueError, GoldenTraceError) as exc:
        result["failure"] = f"golden-validation:{exc}"
        return result
    result["trace"] = str(trace)
    result["trace_sha256"] = sha256_file(trace)
    result["validation"] = validation
    result["ok"] = True
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--candidate-dll", type=Path,
                        default=DEFAULT_CANDIDATE_DLL)
    parser.add_argument(
        "--deployed-dll",
        type=Path,
        default=DEFAULT_CANDIDATE_DLL,
        help="DLL the game will load; its hash must match --candidate-dll",
    )
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument(
        "--diagnostic", action="store_true",
        help="permit a partial --case subset without granting gate authority")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.cases and not args.diagnostic:
        print("partial golden --case selection requires --diagnostic")
        return 2
    if not args.candidate_dll.is_file():
        print(f"candidate DLL is missing: {args.candidate_dll}")
        return 2
    if not args.deployed_dll.is_file():
        print(f"deployed DLL is missing: {args.deployed_dll}")
        return 2
    try:
        manifest = load_manifest(args.manifest)
    except (OSError, GoldenTraceError) as exc:
        print(f"cannot load trusted golden manifest: {exc}")
        return 2
    selected = [
        case for case in manifest["cases"]
        if not args.cases or case["id"] in set(args.cases)
    ]
    if not selected or (args.cases and len(selected) != len(set(args.cases))):
        print("requested golden case inventory is incomplete")
        return 2
    required_case_ids = sorted(str(case["id"]) for case in manifest["cases"])
    selected_case_ids = sorted(str(case["id"]) for case in selected)
    coverage_complete = selected_case_ids == required_case_ids

    candidate_hash = sha256_file(args.candidate_dll)
    deployed_hash = sha256_file(args.deployed_dll)
    if deployed_hash != candidate_hash:
        print(
            "candidate/deployed DLL mismatch: "
            f"candidate={candidate_hash} deployed={deployed_hash}"
        )
        return 2
    report: dict[str, Any] = {
        "schema_version": 1,
        "classification": (
            "trusted-golden-candidate-matrix"
            if coverage_complete
            else "trusted-golden-candidate-diagnostic"
        ),
        "generated_at": utc_now(),
        "manifest": str(args.manifest),
        "manifest_sha256": sha256_file(args.manifest),
        "candidate_dll": str(args.candidate_dll),
        "candidate_dll_sha256": candidate_hash,
        "deployed_dll": str(args.deployed_dll),
        "deployed_dll_sha256": deployed_hash,
        "required_case_ids": required_case_ids,
        "selected_case_ids": selected_case_ids,
        "coverage_complete": coverage_complete,
        "cases": [],
        "ok": False,
        "diagnostic_ok": False,
        "gate_ok": False,
    }
    report_path = args.report_dir / "rollback_golden_matrix.json"
    for case in selected:
        try:
            result = run_case(
                manifest_path=args.manifest,
                case=case,
                candidate_dll_sha256=candidate_hash,
                report_dir=args.report_dir,
            )
        except (OSError, subprocess.TimeoutExpired, GoldenTraceError) as exc:
            result = {
                "case_id": case["id"],
                "ok": False,
                "failure": f"infrastructure:{type(exc).__name__}:{exc}",
            }
        report["cases"].append(result)
        write_json_atomic(report_path, report)
    report["ok"] = all(case["ok"] for case in report["cases"])
    report["diagnostic_ok"] = report["ok"] if args.diagnostic else False
    report["gate_ok"] = report["ok"] and coverage_complete \
        and not args.diagnostic
    write_json_atomic(report_path, report)
    print(
        "rollback trusted golden "
        + ("matrix" if coverage_complete else "diagnostic")
        + (" passed" if report["ok"] else " failed"))
    print(f"report={report_path}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
