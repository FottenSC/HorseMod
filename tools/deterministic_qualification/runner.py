from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys
from pathlib import Path

from .artifacts import runner_sha256, sha256_file, source_identity
from .process_control import (
    close_game,
    find_game_pid,
    launch_game,
    require_game_process,
    wait_for_game,
)
from .replay_entry import (
    TemporaryReplayMod,
    create_request,
    remove_request_files,
    wait_for_replay_entry,
)
from .report import write_report
from .trace_parser import wait_for_boot_evidence, wait_for_replay_lifecycle_evidence


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64")
DEFAULT_DLL = GAME_ROOT / "ue4ss" / "Mods" / "HorseMod" / "dlls" / "main.dll"
DEFAULT_CONFIG = GAME_ROOT / "ue4ss" / "Mods" / "HorseMod" / "dlls" / "rollback.ini"
DEFAULT_LOG = GAME_ROOT / "ue4ss" / "UE4SS.log"
DEFAULT_SCHEMA = ROOT / "build_cmake_LessEqual421__Shipping__Win64" / "HorseMod" / "generated" / "deterministic_contract.json"
DEFAULT_REPORT = ROOT / "tools" / "deterministic_qualification" / "output" / "boot-report.json"
DEFAULT_REPLAY_MOD = ROOT / "build_cmake_LessEqual421__Shipping__Win64" / "HorseMod" / "ReplayQualificationMod.dll"
DEFAULT_REPLAY_REPORT = ROOT / "tools" / "deterministic_qualification" / "output" / "replay-entry-report.json"


def required_file(path: Path, label: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"{label} not found: {resolved}")
    return resolved


def run_boot(args: argparse.Namespace) -> int:
    dll = required_file(args.dll, "HorseMod DLL")
    config = required_file(args.config, "deterministic config")
    schema = required_file(args.schema, "generated schema")
    executable = required_file(args.game_executable, "SoulcaliburVI executable")
    identity = source_identity(ROOT)
    if identity["dirty"]:
        raise RuntimeError("source tree is dirty; boot evidence would not bind an immutable source state")

    existing_pid = find_game_pid()
    if existing_pid is not None:
        raise RuntimeError("SoulcaliburVI is already running; refusing ambiguous boot evidence")
    launch_game()
    pid = wait_for_game(args.timeout)
    try:
        evidence = wait_for_boot_evidence(
            args.log, args.timeout, lambda: require_game_process(pid)
        )
        if evidence.source_commit != identity["commit"]:
            raise RuntimeError(
                f"deployed DLL source {evidence.source_commit} does not match HEAD {identity['commit']}"
            )
    finally:
        if not args.keep_game and find_game_pid() is not None:
            close_game(pid)

    report_data: dict[str, object] = {
        "report_schema": 1,
        "kind": "boot_probe",
        "certifying": False,
        "result": "pass",
        "reason": "boot provenance and hook installation only; no battle or replay was exercised",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source": identity,
        "artifacts": {
            "horsemod_dll": {"path": str(dll), "sha256": sha256_file(dll)},
            "config": {"path": str(config), "sha256": sha256_file(config)},
            "generated_schema": {"path": str(schema), "sha256": sha256_file(schema)},
            "runner_sha256": runner_sha256(Path(__file__).resolve().parent),
            "game_executable": {"path": str(executable), "sha256": sha256_file(executable)},
        },
        "runtime": {
            "horsemod_version": evidence.version,
            "reported_source_commit": evidence.source_commit,
            "deterministic_lifecycle_hooks_armed": True,
            "clean_exit_requested": not args.keep_game,
        },
    }
    write_report(args.report, report_data)
    print(json.dumps(report_data, indent=2, sort_keys=True))
    print(f"report: {args.report.resolve()}")
    return 0


def run_replay_entry(args: argparse.Namespace) -> int:
    dll = required_file(args.dll, "HorseMod DLL")
    replay_mod = required_file(args.replay_mod, "replay qualification mod")
    replay = required_file(args.replay, "replay payload")
    config = required_file(args.config, "deterministic config")
    schema = required_file(args.schema, "generated schema")
    executable = required_file(args.game_executable, "SoulcaliburVI executable")
    identity = source_identity(ROOT)
    if identity["dirty"]:
        raise RuntimeError(
            "source tree is dirty; replay-entry evidence would not bind an immutable source state"
        )
    if find_game_pid() is not None:
        raise RuntimeError("SoulcaliburVI is already running; refusing ambiguous replay evidence")

    run_id = ""
    pid: int | None = None
    mods_root = GAME_ROOT / "ue4ss" / "Mods"
    with TemporaryReplayMod(replay_mod, mods_root):
        try:
            run_id = create_request(replay, args.watch_frames)
            launch_game()
            pid = wait_for_game(args.timeout)
            guard = lambda: require_game_process(pid)
            boot = wait_for_boot_evidence(args.log, args.timeout, guard)
            entry = wait_for_replay_entry(run_id, args.timeout, guard)
            lifecycle = wait_for_replay_lifecycle_evidence(
                args.log, args.timeout, guard
            )
            if boot.source_commit != identity["commit"]:
                raise RuntimeError(
                    f"deployed HorseMod source {boot.source_commit} does not match HEAD {identity['commit']}"
                )
            if lifecycle.source_commit != identity["commit"]:
                raise RuntimeError(
                    "replay qualification mod source does not match the current source commit"
                )
            if not lifecycle.native_import_ready:
                raise RuntimeError("replay qualification native import contract was blocked")
        finally:
            if pid is not None and find_game_pid() is not None:
                close_game(pid)
            if run_id:
                remove_request_files(run_id)

    report_data: dict[str, object] = {
        "report_schema": 1,
        "kind": "replay_entry_probe",
        "certifying": False,
        "result": "pass",
        "reason": (
            "native replay import, stock launch request, and bounded normal-play "
            "frame observation"
        ),
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source": identity,
        "artifacts": {
            "horsemod_dll": {"path": str(dll), "sha256": sha256_file(dll)},
            "replay_qualification_mod": {
                "path": str(replay_mod), "sha256": sha256_file(replay_mod)
            },
            "replay": {"path": str(replay), "sha256": sha256_file(replay)},
            "config": {"path": str(config), "sha256": sha256_file(config)},
            "generated_schema": {"path": str(schema), "sha256": sha256_file(schema)},
            "runner_sha256": runner_sha256(Path(__file__).resolve().parent),
            "game_executable": {"path": str(executable), "sha256": sha256_file(executable)},
        },
        "runtime": {
            "run_id": entry.run_id,
            "horsemod_version": boot.version,
            "reported_source_commit": boot.source_commit,
            "native_replay_import_ready": lifecycle.native_import_ready,
            "launch_requested": True,
            "watch_frames": args.watch_frames,
            "frame_fencepost_observed": True,
            "temporary_mod_removed": True,
            "clean_exit_requested": True,
        },
    }
    write_report(args.report, report_data)
    print(json.dumps(report_data, indent=2, sort_keys=True))
    print(f"report: {args.report.resolve()}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Fail-closed HorseMod deterministic qualification runner"
    )
    subcommands = parser.add_subparsers(dest="command", required=True)
    boot = subcommands.add_parser(
        "boot", help="collect non-certifying DLL provenance and hook-install evidence"
    )
    boot.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    boot.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    boot.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    boot.add_argument("--log", type=Path, default=DEFAULT_LOG)
    boot.add_argument("--game-executable", type=Path, default=GAME_ROOT / "SoulcaliburVI.exe")
    boot.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    boot.add_argument("--timeout", type=float, default=60.0)
    boot.add_argument("--keep-game", action="store_true")
    boot.set_defaults(handler=run_boot)
    replay = subcommands.add_parser(
        "replay-entry",
        help="collect non-certifying native replay-import and first-frame evidence",
    )
    replay.add_argument("--replay", type=Path, required=True)
    replay.add_argument("--replay-mod", type=Path, default=DEFAULT_REPLAY_MOD)
    replay.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    replay.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    replay.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    replay.add_argument("--log", type=Path, default=DEFAULT_LOG)
    replay.add_argument("--game-executable", type=Path, default=GAME_ROOT / "SoulcaliburVI.exe")
    replay.add_argument("--report", type=Path, default=DEFAULT_REPLAY_REPORT)
    replay.add_argument("--timeout", type=float, default=120.0)
    replay.add_argument("--watch-frames", type=int, default=1)
    replay.set_defaults(handler=run_replay_entry)
    return parser


def main() -> int:
    try:
        args = build_parser().parse_args()
        return int(args.handler(args))
    except (FileNotFoundError, RuntimeError, TimeoutError, subprocess.SubprocessError) as error:
        print(f"qualification failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
