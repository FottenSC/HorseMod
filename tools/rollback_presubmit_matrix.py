#!/usr/bin/env python3
"""Run rollback C++ self-tests from fresh Gekko ON/OFF build directories."""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from rollback_ctest import RollbackCTestError, discover_rollback_selftests


REPO = Path(__file__).resolve().parents[1]
DEFAULT_VCVARS = Path(
    r"E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat"
)
DEFAULT_CONFIGURATION = "LessEqual421__Shipping__Win64"


def run(command: list[str], timeout: int) -> dict[str, Any]:
    started = time.time()
    try:
        proc = subprocess.run(
            command,
            cwd=REPO,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
        return {
            "command": command,
            "returncode": proc.returncode,
            "seconds": round(time.time() - started, 3),
            "output_tail": (proc.stdout or "")[-16000:],
            "ok": proc.returncode == 0,
        }
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {
            "command": command,
            "seconds": round(time.time() - started, 3),
            "error": f"{type(exc).__name__}:{exc}",
            "ok": False,
        }


def dev_command(vcvars: Path, command: list[str]) -> list[str]:
    escaped = subprocess.list2cmdline(command)
    return [
        "cmd", "/d", "/c",
        f'call {subprocess.list2cmdline([str(vcvars)])} >nul && {escaped}',
    ]


def run_case(
    *,
    enabled: bool,
    build_dir: Path,
    vcvars: Path,
    configuration: str,
    timeout: int,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "gekko_enabled": enabled,
        "build_dir": str(build_dir),
        "stages": [],
        "ok": False,
    }
    if build_dir.exists():
        result["failure"] = "fresh-build-directory-already-exists"
        return result

    configure = run(dev_command(vcvars, [
        "cmake",
        "-S", str(REPO),
        "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={configuration}",
        f"-DHORSEMOD_ENABLE_GEKKONET={'ON' if enabled else 'OFF'}",
        "-DMYMODS_FAST_DEV=OFF",
    ]), timeout)
    result["stages"].append({"name": "configure", **configure})
    if not configure["ok"]:
        return result

    build = run(dev_command(vcvars, [
        "cmake", "--build", str(build_dir),
        "--target", "HorseModRollbackSelfTests",
        "--parallel",
    ]), timeout)
    result["stages"].append({"name": "build", **build})
    if not build["ok"]:
        return result

    try:
        tests = discover_rollback_selftests(build_dir)
    except RollbackCTestError as exc:
        result["failure"] = f"ctest-discovery:{exc}"
        return result
    gekko_tests = sorted(
        test.name for test in tests if "requires-gekko" in test.labels
    )
    result["test_count"] = len(tests)
    result["requires_gekko_tests"] = gekko_tests
    if enabled and not gekko_tests:
        result["failure"] = "enabled-matrix-has-no-gekko-tests"
        return result
    if not enabled and gekko_tests:
        result["failure"] = "disabled-matrix-registered-gekko-tests"
        return result

    ctest = run([
        "ctest",
        "--test-dir", str(build_dir),
        "--output-on-failure",
        "-L", "rollback",
    ], timeout)
    result["stages"].append({"name": "ctest", **ctest})
    result["ok"] = bool(ctest["ok"])
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-root",
        type=Path,
        default=REPO / "build_rollback_presubmit" /
                datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"),
    )
    parser.add_argument("--report", type=Path)
    parser.add_argument("--vcvars", type=Path, default=DEFAULT_VCVARS)
    parser.add_argument("--configuration", default=DEFAULT_CONFIGURATION)
    parser.add_argument("--timeout", type=int, default=1800)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.vcvars.is_file():
        print(f"vcvars does not exist: {args.vcvars}")
        return 2
    if args.build_root.exists():
        print(f"fresh build root already exists: {args.build_root}")
        return 2

    cases = [
        run_case(
            enabled=enabled,
            build_dir=args.build_root / ("gekko-on" if enabled else "gekko-off"),
            vcvars=args.vcvars,
            configuration=args.configuration,
            timeout=args.timeout,
        )
        for enabled in (True, False)
    ]
    report = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "source_dir": str(REPO),
        "build_root": str(args.build_root),
        "configuration": args.configuration,
        "cases": cases,
        "ok": all(case["ok"] for case in cases),
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, indent=2) + "\n",
            encoding="utf-8",
        )
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
