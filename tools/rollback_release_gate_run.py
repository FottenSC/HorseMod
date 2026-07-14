#!/usr/bin/env python3
"""Build, launch two SC6 clients, and run the direct-connect release gate."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import rollback_two_client_test_run as two_client
from rollback_report_contract import (
    artifact,
    contract_fields,
    coverage,
    sha256_file,
    utc_now,
    validate_v2,
)


REPORT_DIR = two_client.REPO / "reports" / "rollback_release_gate"
BUILD_BAT = two_client.REPO / "build_and_deploy.bat"
ACCEPTANCE = two_client.REPO / "tools" / "rollback_two_client_acceptance_run.py"
FULL_VALIDATION = two_client.REPO / "tools" / "rollback_full_validation_run.py"
BUILT_DLL = (
    two_client.REPO / "build_cmake_LessEqual421__Shipping__Win64"
    / "HorseMod" / "HorseMod.dll"
)
DEPLOYED_DLL = (
    two_client.GAME_EXE.parent / "ue4ss" / "Mods" / "HorseMod"
    / "dlls" / "main.dll"
)
STRICT_REPLAY = (
    two_client.REPO / "ReplayExample" / "REPLAY_12744704008398858106.bin"
)
DEFAULT_STEAM_EXE = Path(r"C:\Program Files (x86)\Steam\steam.exe")
DEFAULT_SANDBOXIE_START = Path(r"C:\Program Files\Sandboxie-Plus\Start.exe")
DEFAULT_STEAM_APP_ID = "544750"
READY_TRACE_EVENT = "native_replay_trace_hooks_installed"
EARLY_TRACE_EVENTS = (
    READY_TRACE_EVENT,
    "rollback_two_client_role_manifest",
    "rollback_lab_configured",
    "session_start",
)
DEFAULT_SANDBOX_QUERY_PORT = 27012


def argv_has_option(*names: str) -> bool:
    return any(arg in names for arg in sys.argv[1:])


def now_id() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S-%f")


def default_mvp_room_name(run_id: str) -> str:
    digest = hashlib.sha1(run_id.encode("utf-8", errors="replace")).digest()
    suffix = "".join(chr(ord("A") + (byte % 26)) for byte in digest[:8])
    return f"HRoom{suffix}"


def parse_optional_u64(value: str) -> int:
    value = str(value or "").strip()
    if not value:
        return 0
    return int(value, 0)


def parse_loginusers_vdf(path: Path) -> list[dict[str, Any]]:
    users: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return users

    user_re = re.compile(r'^\s*"(\d{16,20})"\s*$')
    kv_re = re.compile(r'^\s*"([^"]+)"\s+"([^"]*)"\s*$')
    for line in lines:
        user_match = user_re.match(line)
        if user_match:
            current = {
                "steam_id": int(user_match.group(1)),
                "path": str(path),
            }
            users.append(current)
            continue
        if current is None:
            continue
        kv_match = kv_re.match(line)
        if not kv_match:
            continue
        key, value = kv_match.groups()
        current[key] = value
    return users


def choose_loginusers_account(path: Path) -> dict[str, Any]:
    users = parse_loginusers_vdf(path)
    if not users:
        return {"ok": False, "path": str(path), "failure": "loginusers-empty"}

    def int_field(row: dict[str, Any], key: str) -> int:
        try:
            return int(str(row.get(key, "") or "0"), 0)
        except ValueError:
            return 0

    chosen = next((row for row in users if int_field(row, "MostRecent") == 1), None)
    source = "MostRecent"
    if chosen is None:
        chosen = next((row for row in users if int_field(row, "AllowAutoLogin") == 1), None)
        source = "AllowAutoLogin"
    if chosen is None:
        chosen = max(users, key=lambda row: int_field(row, "Timestamp"))
        source = "Timestamp"
    return {
        "ok": True,
        "source": source,
        "steam_id": int(chosen["steam_id"]),
        "account_name": str(chosen.get("AccountName", "")),
        "persona_name": str(chosen.get("PersonaName", "")),
        "path": str(path),
    }


def sandbox_box_for_path(path: Path) -> str:
    parts = path.parts
    lowered = [part.lower() for part in parts]
    if "sandbox" not in lowered:
        return ""
    index = lowered.index("sandbox")
    if len(parts) > index + 2:
        return f"{parts[index + 1]}\\{parts[index + 2]}"
    return ""


def sandbox_loginusers_candidates(args: argparse.Namespace) -> list[Path]:
    steam_drive = args.steam_exe.drive.rstrip(":") or "C"
    pattern = (
        f"*/*/drive/{steam_drive}/"
        "Program Files (x86)/Steam/config/loginusers.vdf"
    )
    candidates = [
        path for path in args.sandbox_root.glob(pattern)
        if path.exists()
    ]
    expected_box = two_client.normalize_sandbox_box(args.sandbox_box)
    if not expected_box:
        return candidates
    return [
        path for path in candidates
        if two_client.normalize_sandbox_box(sandbox_box_for_path(path))
        == expected_box
    ]


def resolve_host_owner_from_steam_loginusers(
    args: argparse.Namespace,
) -> dict[str, Any]:
    host_path = args.steam_exe.parent / "config" / "loginusers.vdf"
    host = choose_loginusers_account(host_path)
    sandbox_candidates = sandbox_loginusers_candidates(args)
    sandbox_accounts = [
        choose_loginusers_account(path)
        for path in sandbox_candidates
    ]
    sandbox_accounts = [
        account for account in sandbox_accounts
        if account.get("ok") and account.get("steam_id")
    ]

    result: dict[str, Any] = {
        "ok": False,
        "source": "steam-loginusers",
        "host": host,
        "sandbox": sandbox_accounts,
        "failure": "",
    }
    if not host.get("ok") or not host.get("steam_id"):
        result["failure"] = "host-loginusers-unresolved"
        return result
    host_id = int(host["steam_id"])
    sandbox_ids = {int(account["steam_id"]) for account in sandbox_accounts}
    if not sandbox_ids:
        result["failure"] = "sandbox-loginusers-unresolved"
        return result
    if host_id in sandbox_ids:
        result["failure"] = "host-and-sandbox-steam-user-match"
        return result
    result["ok"] = True
    result["target_owner_id"] = host_id
    return result


def resolve_online_stage_targeting(
    args: argparse.Namespace,
    run_id: str,
) -> dict[str, Any]:
    target_owner_id = parse_optional_u64(args.online_stage_target_owner_id)
    room_name = args.online_stage_room_name
    resolution = {
        "ok": True,
        "source": "explicit-or-empty",
        "room_name": room_name,
        "target_owner_id": target_owner_id,
        "failure": "",
    }
    if not args.mvp_online_match:
        return resolution
    if room_name:
        resolution["source"] = "explicit-room-name"
        return resolution
    if target_owner_id:
        resolution["source"] = "explicit-target-owner-id"
        return resolution
    owner = resolve_host_owner_from_steam_loginusers(args)
    resolution["host_owner_resolution"] = owner
    if owner.get("ok") and owner.get("target_owner_id"):
        resolution["source"] = "host-steam-loginusers"
        resolution["room_name"] = ""
        resolution["target_owner_id"] = int(owner["target_owner_id"])
        return resolution

    resolution["ok"] = False
    resolution["source"] = "host-steam-loginusers"
    resolution["room_name"] = ""
    resolution["failure"] = str(owner.get("failure") or "")
    return resolution


def run_command(name: str, cmd: list[str], timeout: int) -> dict[str, Any]:
    started = time.time()
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(two_client.REPO),
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
    except subprocess.TimeoutExpired as exc:
        return {
            "name": name,
            "cmd": cmd,
            "returncode": -1,
            "elapsed_seconds": round(time.time() - started, 3),
            "ok": False,
            "output": exc.stdout or "",
            "failure": "timeout",
        }


def load_report_from_command_output(output: str) -> tuple[Path | None, dict[str, Any]]:
    report_path: Path | None = None
    for line in output.splitlines():
        if line.startswith("report="):
            value = line.split("=", 1)[1].strip()
            if value:
                report_path = Path(value)
    if report_path is None:
        return None, {}
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return report_path, {}
    if not isinstance(report, dict):
        return report_path, {}
    return report_path, report


def kill_sc6(timeout: int) -> dict[str, Any]:
    return run_command(
        "kill-sc6",
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            (
                "$p=Get-Process SoulcaliburVI,CrashReportClient,crashhelper "
                "-ErrorAction SilentlyContinue; "
                "if ($p) { $p | Stop-Process -Force }; exit 0"
            ),
        ],
        timeout,
    )


def sandbox_box_leaf(sandbox_box: str) -> str:
    return Path(sandbox_box.replace("/", "\\")).name or sandbox_box


def launch_creationflags(args: argparse.Namespace) -> int:
    if args.high_priority:
        return getattr(subprocess, "HIGH_PRIORITY_CLASS", 0)
    return 0


def launch_host(args: argparse.Namespace) -> dict[str, Any]:
    cmd = [
        str(args.steam_exe),
        "-applaunch",
        str(args.steam_app_id),
    ]
    started = time.time()
    try:
        proc = subprocess.Popen(
            cmd,
            cwd=str(args.steam_exe.parent),
            creationflags=launch_creationflags(args),
        )
    except OSError as exc:
        return {
            "name": "launch-host-sc6",
            "ok": False,
            "cmd": cmd,
            "failure": str(exc),
        }
    return {
        "name": "launch-host-sc6",
        "ok": True,
        "pid": proc.pid,
        "cmd": cmd,
        "elapsed_seconds": round(time.time() - started, 3),
    }


def launch_sandbox(args: argparse.Namespace) -> dict[str, Any]:
    cmd = [
        str(args.sandboxie_start),
        f"/box:{sandbox_box_leaf(args.sandbox_box)}",
        str(args.steam_exe),
        "-applaunch",
        str(args.steam_app_id),
    ]
    if args.inject_sandbox_query_port_arg:
        cmd.append(f"-QueryPort={args.sandbox_query_port}")
    started = time.time()
    try:
        proc = subprocess.Popen(
            cmd,
            cwd=str(args.steam_exe.parent),
            creationflags=launch_creationflags(args),
        )
    except OSError as exc:
        return {
            "name": "launch-sandbox-sc6",
            "ok": False,
            "cmd": cmd,
            "failure": str(exc),
        }
    return {
        "name": "launch-sandbox-sc6",
        "ok": True,
        "pid": proc.pid,
        "cmd": cmd,
        "elapsed_seconds": round(time.time() - started, 3),
    }


def wait_for_role_ready(
    role: str,
    args: argparse.Namespace,
    seconds: float,
) -> dict[str, Any]:
    deadline = time.time() + max(0.0, seconds)
    latest: dict[str, Any] = {}
    latest_root: dict[str, Any] | None = None
    latest_trace: Path | None = None
    latest_pid = -1
    while time.time() < deadline:
        latest = two_client.snapshot(f"release-gate-wait-{role}-ready")
        current_pids = {
            two_client.int_value(value, -1)
            for value in latest.get("sc6_pids", [])
            if two_client.int_value(value, -1) >= 0
        }
        if len(current_pids) > 2:
            return {
                "name": f"wait-for-{role}-ready",
                "ok": False,
                "role": role,
                "snapshot": latest,
                "failure": f"too many SC6 clients: {sorted(current_pids)}",
            }
        roots = two_client.discover_roots(
            current_pids,
            args.sandbox_root,
            args.sandbox_box,
            processes=latest.get("processes", []),
        )
        latest_root = next(
            (root for root in roots if root.get("role") == role),
            None,
        )
        live_pids = [
            two_client.int_value(pid, -1)
            for pid in (latest_root or {}).get("live_trace_pids", [])
            if two_client.int_value(pid, -1) >= 0
        ]
        if len(live_pids) == 1:
            latest_pid = live_pids[0]
            latest_trace = trace_for_pid(Path(latest_root["path"]), latest_pid)
            ready_event = trace_first_event(latest_trace, EARLY_TRACE_EVENTS)
            if latest_trace and ready_event:
                return {
                    "name": f"wait-for-{role}-ready",
                    "ok": True,
                    "role": role,
                    "pid": latest_pid,
                    "root": latest_root,
                    "ready_event": ready_event,
                    "trace": str(latest_trace),
                    "snapshot": latest,
                }
        time.sleep(1.0)
    return {
        "name": f"wait-for-{role}-ready",
        "ok": False,
        "role": role,
        "pid": latest_pid,
        "root": latest_root,
        "trace": str(latest_trace) if latest_trace else "",
        "trace_tail": trace_tail(latest_trace),
        "snapshot": latest,
        "latest_event": trace_first_event(latest_trace, EARLY_TRACE_EVENTS),
        "failure": "timed out waiting for HorseMod trace readiness",
    }


def trace_for_pid(root: Path, pid: int) -> Path | None:
    matches = [
        path for path in two_client.trace_files(root)
        if f"_pid{pid}.jsonl" in path.name
    ]
    return matches[-1] if matches else None


def trace_has_event(path: Path, event_name: str) -> bool:
    return bool(trace_first_event(path, (event_name,)))


def trace_first_event(path: Path | None, event_names: tuple[str, ...]) -> str:
    if path is None:
        return ""
    wanted = set(event_names)
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if not any(name in line for name in wanted):
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                name = event.get("event")
                if name in wanted:
                    return str(name)
    except OSError:
        return ""
    return ""


def trace_tail(path: Path | None, lines: int = 12) -> list[str]:
    if path is None:
        return []
    try:
        return path.read_text(
            encoding="utf-8",
            errors="replace",
        ).splitlines()[-lines:]
    except OSError:
        return []


def wait_for_client_ready(
    role: str,
    pid: int,
    args: argparse.Namespace,
    seconds: float,
) -> dict[str, Any]:
    deadline = time.time() + max(0.0, seconds)
    latest: dict[str, Any] = {}
    latest_root: dict[str, Any] | None = None
    latest_trace: Path | None = None
    while time.time() < deadline:
        latest = two_client.snapshot(f"release-gate-wait-{role}-ready")
        current_pids = {
            two_client.int_value(value, -1)
            for value in latest.get("sc6_pids", [])
            if two_client.int_value(value, -1) >= 0
        }
        if pid not in current_pids:
            return {
                "name": f"wait-for-{role}-ready",
                "ok": False,
                "role": role,
                "pid": pid,
                "snapshot": latest,
                "failure": "SC6 process exited before HorseMod readiness",
            }
        roots = two_client.discover_roots(
            current_pids,
            args.sandbox_root,
            args.sandbox_box,
            processes=latest.get("processes", []),
        )
        latest_root = next(
            (root for root in roots if root.get("role") == role),
            None,
        )
        if latest_root:
            latest_trace = trace_for_pid(Path(latest_root["path"]), pid)
            ready_event = trace_first_event(latest_trace, EARLY_TRACE_EVENTS)
            if latest_trace and ready_event:
                return {
                    "name": f"wait-for-{role}-ready",
                    "ok": True,
                    "role": role,
                    "pid": pid,
                    "root": latest_root,
                    "ready_event": ready_event,
                    "trace": str(latest_trace),
                    "snapshot": latest,
                }
        time.sleep(1.0)
    return {
        "name": f"wait-for-{role}-ready",
        "ok": False,
        "role": role,
        "pid": pid,
        "root": latest_root,
        "trace": str(latest_trace) if latest_trace else "",
        "trace_tail": trace_tail(latest_trace),
        "snapshot": latest,
        "latest_event": trace_first_event(latest_trace, EARLY_TRACE_EVENTS),
        "failure": "timed out waiting for HorseMod trace readiness",
    }


def settle_after_launch(seconds: float) -> dict[str, Any]:
    if seconds > 0:
        time.sleep(seconds)
    return {
        "name": "post-launch-settle",
        "ok": True,
        "elapsed_seconds": round(max(0.0, seconds), 3),
        "snapshot": two_client.snapshot("release-gate-post-launch-settle"),
    }


def disable_rollback_lab(args: argparse.Namespace, run_id: str) -> dict[str, Any]:
    snap = two_client.snapshot("release-gate-disable-rollback-lab")
    sc6_pids = {
        two_client.int_value(pid, -1)
        for pid in snap.get("sc6_pids", [])
        if two_client.int_value(pid, -1) >= 0
    }
    roots = two_client.discover_roots(
        sc6_pids,
        args.sandbox_root,
        args.sandbox_box,
        processes=snap.get("processes", []),
    )
    text = (
        "enabled=0\n"
        "trace=0\n"
        "case=baseline-oracle\n"
        f"request_id=disable-rollback-lab-{run_id}\n"
    )
    records: list[dict[str, Any]] = []
    for root in roots:
        root_path = Path(root["path"])
        request_path = two_client.write_request_file(root_path, text)
        records.append({
            "role": root.get("role", ""),
            "root": str(root_path),
            "request_path": request_path,
        })
    if records:
        time.sleep(2.0)
    for root in roots:
        two_client.remove_request_file(Path(root["path"]))
    return {
        "name": "disable-rollback-lab",
        "ok": bool(records),
        "request_id": f"disable-rollback-lab-{run_id}",
        "records": records,
        "snapshot": snap,
    }


def wait_for_two_sc6(seconds: float) -> dict[str, Any]:
    deadline = time.time() + max(0.0, seconds)
    latest = two_client.snapshot("release-gate-wait")
    while time.time() < deadline:
        latest = two_client.snapshot("release-gate-wait")
        if len(latest.get("sc6_pids", [])) == 2:
            return {"name": "wait-for-two-sc6", "ok": True, "snapshot": latest}
        time.sleep(1.0)
    return {
        "name": "wait-for-two-sc6",
        "ok": len(latest.get("sc6_pids", [])) == 2,
        "snapshot": latest,
    }


def run_acceptance(
    args: argparse.Namespace,
    run_id: str,
    *,
    phases_override: str = "",
    skip_online_stage_drive_override: bool | None = None,
) -> dict[str, Any]:
    phases = phases_override or (
        "inventory,role-manifest,sidecar,direct-release"
        if args.mvp_online_match
        else "inventory,role-manifest,sidecar,sidecar-fault-closed,direct-release"
    )
    online_stage_target_owner_id = int(
        getattr(
            args,
            "resolved_online_stage_target_owner_id",
            parse_optional_u64(args.online_stage_target_owner_id),
        )
    )
    online_stage_room_name = str(
        getattr(args, "resolved_online_stage_room_name", args.online_stage_room_name)
        or ""
    )
    cmd = [
        sys.executable,
        str(ACCEPTANCE),
        "--mode",
        "direct-connect",
        "--run-id",
        run_id,
        "--phases",
        phases,
        "--sandbox-root",
        str(args.sandbox_root),
        "--sandbox-box",
        args.sandbox_box,
        "--role-manifest-watch-seconds",
        str(args.role_manifest_watch_seconds),
        "--direct-release-watch-seconds",
        str(args.direct_release_watch_seconds),
        "--online-stage-native-session-name",
        "PlayerMatch",
        "--online-stage-room-name",
        online_stage_room_name,
        "--online-stage-target-owner-id",
        str(online_stage_target_owner_id),
        "--main-menu-player-match-route",
        args.main_menu_player_match_route,
        "--wait-for-two-sc6-seconds",
        "0",
        "--strict",
        "--fault-profile",
        args.fault_profile,
        "--fault-seed",
        f"0x{args.fault_seed:X}",
    ]
    if not args.mvp_online_match:
        cmd.append("--debug-steam-probe")
    else:
        cmd.append("--debug-steam-probe")
        if args.mvp_online_match_network_check_compat:
            cmd.append("--online-stage-network-check-compat")
        if args.mvp_online_match_join_complete_compat:
            cmd.append("--online-stage-join-complete-compat")
        if args.mvp_online_match_transport_ready_compat:
            cmd.append("--online-stage-transport-ready-compat")
        if args.mvp_online_match_ready_open_compat:
            cmd.append("--online-stage-ready-open-compat")
        if args.mvp_online_match_peer_route_tag_fix:
            cmd.append("--online-stage-peer-route-tag-fix")
        if args.mvp_online_match_in_room_transition_compat:
            cmd.append("--online-stage-in-room-transition-compat")
        if args.mvp_online_match_direct_native_join_diagnostic:
            cmd.append("--online-stage-direct-native-join-diagnostic")
    skip_online_stage_drive = (
        skip_online_stage_drive_override
        if skip_online_stage_drive_override is not None
        else should_skip_online_stage_drive(args)
    )
    if not skip_online_stage_drive:
        cmd.append("--online-stage-fail-fast-empty-finds")
    if skip_online_stage_drive:
        cmd.append("--skip-online-stage-drive")
    return run_command("direct-release-acceptance", cmd, args.acceptance_timeout)


def should_skip_online_stage_drive(args: argparse.Namespace) -> bool:
    if args.skip_online_stage_drive or args.wait_for_player_match_then_test:
        return True
    return bool(args.manual_attach and not args.auto_online_stage)


def run_gameflow_observe(args: argparse.Namespace, run_id: str) -> dict[str, Any]:
    cmd = [
        sys.executable,
        str(ACCEPTANCE),
        "--run-id",
        run_id,
        "--observe-gameflow",
        "--observe-gameflow-watch-seconds",
        str(args.observe_gameflow_watch_seconds),
        "--sandbox-root",
        str(args.sandbox_root),
        "--sandbox-box",
        args.sandbox_box,
        "--main-menu-player-match-route",
        args.main_menu_player_match_route,
        "--wait-for-two-sc6-seconds",
        "0",
        "--strict",
    ]
    if args.capture_gameflow:
        cmd.append("--capture-gameflow")
    if args.observe_gameflow_process_events:
        cmd.append("--observe-gameflow-process-events")
    else:
        cmd.append("--no-observe-gameflow-process-events")
    return run_command(
        "gameflow-observe",
        cmd,
        max(120, int(args.observe_gameflow_watch_seconds) + 90),
    )


def attach_report_from_output(step: dict[str, Any]) -> tuple[Path | None, dict[str, Any]]:
    report_path, report = load_report_from_command_output(str(step.get("output", "")))
    if report_path is not None:
        step["report_path"] = str(report_path)
    if report:
        step["report_json"] = report
    return report_path, report


def print_wait_player_match_summary(step: dict[str, Any]) -> None:
    report = step.get("report_json")
    if not isinstance(report, dict):
        print(step.get("output", ""))
        return
    print(
        "wait-player-match "
        f"{'PASS' if step.get('ok') else 'FAIL'} "
        f"report={step.get('report_path') or report.get('report', '')}"
    )
    observe_step = next(
        (
            item for item in report.get("steps", [])
            if isinstance(item, dict) and item.get("phase") == "observe-gameflow"
        ),
        {},
    )
    observe_report = observe_step.get("report_json")
    if not isinstance(observe_report, dict):
        return
    for result in observe_report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root") or {}
        latest = result.get("gameflow_observe") or {}
        missing = ",".join(str(item) for item in result.get("missing", []))
        print(
            "wait-player-match "
            f"role={root.get('role', '-')} "
            f"ok={1 if result.get('ok') else 0} "
            f"scene={latest.get('current_scene_class', '')}/"
            f"{latest.get('current_scene_name', '')} "
            f"player_match={1 if latest.get('player_match_scene') else 0} "
            f"lobby={1 if latest.get('player_match_lobby_scene') else 0} "
            f"process_events={result.get('gameflow_process_event_count', 0)} "
            f"missing={missing or '-'}"
        )


def write_report(report: dict[str, Any]) -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    path = REPORT_DIR / f"rollback_release_gate_{report['run_id']}.json"
    path.write_text(
        json.dumps(report, indent=2, sort_keys=True),
        encoding="utf-8",
        newline="\n",
    )
    return path


def validate_validation_bundle(path: Path, max_age_hours: float) -> dict[str, Any]:
    failures: list[str] = []
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return {
            "name": "validate-local-regression-bundle", "ok": False,
            "path": str(path), "failures": [str(exc)],
        }
    failures.extend(validate_v2(report, workflow_kind="local-regression"))
    if report.get("verdict") != "pass":
        failures.append("local regression verdict is not pass")
    if not report.get("coverage_complete"):
        failures.append("local regression coverage is incomplete")
    try:
        generated = datetime.fromisoformat(str(report.get("generated_at", "")))
        if generated.tzinfo is None:
            generated = generated.replace(tzinfo=timezone.utc)
        age_hours = (
            datetime.now(timezone.utc) - generated.astimezone(timezone.utc)
        ).total_seconds() / 3600.0
        if age_hours < -0.05 or age_hours > max_age_hours:
            failures.append(
                f"local regression bundle age {age_hours:.2f}h exceeds "
                f"{max_age_hours:.2f}h"
            )
    except ValueError:
        age_hours = None
        failures.append("generated_at is missing or invalid")
    report_artifacts = report.get("artifacts") or {}
    for label, current_path in (
        ("built_dll", BUILT_DLL),
        ("deployed_dll", DEPLOYED_DLL),
        ("replay_input", STRICT_REPLAY),
    ):
        expected = (report_artifacts.get(label) or {}).get("sha256", "")
        actual = sha256_file(current_path) if current_path.is_file() else ""
        if not expected or expected != actual:
            failures.append(f"{label} artifact identity mismatch")
    strict_artifact = report_artifacts.get("strict_replay_report") or {}
    strict_artifact_path = Path(str(strict_artifact.get("path", "")))
    strict_artifact_hash = str(strict_artifact.get("sha256", ""))
    if (not strict_artifact_path.is_file()
            or not strict_artifact_hash
            or sha256_file(strict_artifact_path) != strict_artifact_hash):
        failures.append("strict replay report artifact identity mismatch")
    strict = next(
        (item for item in report.get("results", [])
         if item.get("name") == "strict-replay"), None
    )
    if not isinstance(strict, dict) or not strict.get("ok"):
        failures.append("strict replay result is absent or failed")
    elif "--strict" not in strict.get("cmd", []):
        failures.append("strict replay did not run with --strict")
    if isinstance(strict, dict):
        attempts = strict.get("attempts", [])
        final_output = str(
            attempts[-1].get("output", "")
            if attempts else strict.get("output", "")
        )
        match = re.search(r"^report json:\s*(.+)$", final_output, re.MULTILINE)
        expected_strict_path = (
            Path(match.group(1).strip()) if match else None
        )
        if expected_strict_path != strict_artifact_path:
            failures.append("strict replay report does not match final attempt")
    try:
        strict_report_json = json.loads(
            strict_artifact_path.read_text(encoding="utf-8")
        )
        if not strict_report_json.get("final_passed"):
            failures.append("strict replay report final_passed is false")
    except (OSError, json.JSONDecodeError):
        failures.append("strict replay report JSON is unreadable")
    return {
        "name": "validate-local-regression-bundle",
        "ok": not failures,
        "path": str(path),
        "age_hours": age_hours,
        "failures": failures,
        "report": report,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Post-change rollback release gate: build, two clients, direct-release test.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--run-id", default="")
    parser.add_argument(
        "--full-player-match-test",
        action="store_true",
        help=(
            "Operator one-command flow: kill existing SC6 clients, build and "
            "deploy, launch host normally, launch Sandboxie through Steam "
            "(using that sandboxed Steam account's saved -QueryPort=27012 "
            "launch option), wait for both clients to reach the real Player "
            "Match fight, then run full rollback acceptance."
        ),
    )
    parser.add_argument(
        "--setup-only",
        action="store_true",
        help=(
            "Run the setup half only: optional kill, build/deploy, launch host "
            "and sandbox, wait for HorseMod traces, then leave both clients "
            "running for manual menu navigation."
        ),
    )
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-launch", action="store_true")
    parser.add_argument(
        "--kill-existing",
        dest="kill_existing",
        action="store_true",
        default=None,
        help=(
            "Kill existing SoulcaliburVI/CrashReportClient processes before "
            "build or launch. Defaults on when this script launches clients."
        ),
    )
    parser.add_argument(
        "--no-kill-existing",
        dest="kill_existing",
        action="store_false",
        help="Do not kill existing SC6 clients before build or launch.",
    )
    parser.set_defaults(kill_existing=None)
    parser.add_argument(
        "--manual-attach",
        action="store_true",
        help=(
            "Validate already-running host + Sandboxie clients without "
            "killing, building, or launching. When used with "
            "--full-player-match-test, this expects the clients to already "
            "be in the real Player Match fight unless "
            "--wait-for-player-match-then-test is also supplied."
        ),
    )
    parser.add_argument("--leave-running", action="store_true")
    parser.add_argument(
        "--mvp-online-match",
        action="store_true",
        help=(
            "Run the focused two-client MVP gate: role manifest, sidecar, "
            "real Player Match connect, online battle scene, and direct input."
        ),
    )
    parser.add_argument(
        "--observe-gameflow",
        action="store_true",
        help=(
            "Passive manual-attach recorder: arm both clients, do not drive "
            "menus, and pass only if both reach the real online battle scene."
        ),
    )
    parser.add_argument(
        "--wait-for-player-match-then-test",
        action="store_true",
        help=(
            "One-command manual-flow gate: wait until both clients reach the "
            "real Player Match fight, then run the full direct-release "
            "rollback acceptance with online-stage driving disabled."
        ),
    )
    parser.add_argument(
        "--auto-online-stage",
        "--drive-online-stage",
        dest="auto_online_stage",
        action="store_true",
        help=(
            "Use the native online-stage automation during direct-release "
            "instead of waiting for manual Player Match navigation. With "
            "--full-player-match-test this keeps the one-command "
            "kill/build/deploy/launch/test flow while driving Player Match "
            "create/join/start through the harness."
        ),
    )
    parser.add_argument(
        "--capture-gameflow",
        action="store_true",
        help=(
            "With --observe-gameflow, collect the scene/ProcessEvent timeline "
            "without requiring the fight scene. This is diagnostic only and "
            "does not count as rollback acceptance."
        ),
    )
    parser.add_argument(
        "--observe-gameflow-process-events",
        dest="observe_gameflow_process_events",
        action="store_true",
        default=False,
        help=(
            "Opt-in diagnostic: with --observe-gameflow, include the filtered "
            "ProcessEvent timeline."
        ),
    )
    parser.add_argument(
        "--no-observe-gameflow-process-events",
        dest="observe_gameflow_process_events",
        action="store_false",
        help=(
            "With --observe-gameflow, use scene samples only and do not "
            "install/log the ProcessEvent observer."
        ),
    )
    parser.add_argument(
        "--mvp-online-match-join-complete-compat",
        action="store_true",
        help=(
            "For --mvp-online-match, explicitly enable the sandbox Luxor "
            "join-complete handler compatibility experiment. Reports using "
            "this are compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--mvp-online-match-transport-ready-compat",
        action="store_true",
        help=(
            "For --mvp-online-match, explicitly enable the sandbox Luxor "
            "transport mark-ready compatibility experiment. Reports using "
            "this are compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--mvp-online-match-ready-open-compat",
        action="store_true",
        help=(
            "For --mvp-online-match, explicitly enable the host Luxor "
            "ready-channel open compatibility experiment. Reports using this "
            "are compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--mvp-online-match-peer-route-tag-fix",
        action="store_true",
        help=(
            "For --mvp-online-match, explicitly enable the active-routing "
            "peer route tag correction for queued opcode 21 packets."
        ),
    )
    parser.add_argument(
        "--mvp-online-match-network-check-compat",
        action="store_true",
        help=(
            "For --mvp-online-match, explicitly enable the Sandboxie native "
            "network-adapter check compatibility experiment. Reports using "
            "this are compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--mvp-online-match-in-room-transition-compat",
        action="store_true",
        help=(
            "For --mvp-online-match, explicitly enable the old "
            "PlayerMatchLobbyScene InRoomInit transition experiment. Reports "
            "using this are compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--mvp-online-match-direct-native-join-diagnostic",
        action="store_true",
        help=(
            "Diagnostic shortcut: bypass the UI-style select/search-connect "
            "join pipeline and call ULuxorSessionHub.JoinSession directly. "
            "Reports using this are diagnostic, not native manual-flow MVP "
            "evidence."
        ),
    )
    parser.add_argument("--sandbox-root", type=Path, default=Path(r"C:\Sandbox"))
    parser.add_argument("--sandbox-box", default=two_client.DEFAULT_SANDBOX_BOX)
    parser.add_argument("--steam-exe", type=Path, default=DEFAULT_STEAM_EXE)
    parser.add_argument("--steam-app-id", default=DEFAULT_STEAM_APP_ID)
    parser.add_argument("--sandboxie-start", type=Path, default=DEFAULT_SANDBOXIE_START)
    parser.add_argument(
        "--sandbox-query-port",
        type=int,
        default=DEFAULT_SANDBOX_QUERY_PORT,
        help=(
            "SC6 query port passed only to the Sandboxie client. Host launch "
            "receives no SC6 networking argument."
        ),
    )
    parser.add_argument(
        "--inject-sandbox-query-port-arg",
        action="store_true",
        default=False,
        help=(
            "Pass -QueryPort through Sandboxie/Steam -applaunch. Use this on "
            "machines where the sandboxed Steam account does not already have "
            "the SC6 launch option configured."
        ),
    )
    parser.add_argument(
        "--no-inject-sandbox-query-port-arg",
        dest="inject_sandbox_query_port_arg",
        action="store_false",
        help=(
            "Rely on the sandboxed Steam account SC6 launch option. This is "
            "the default because it matches the proven manual setup."
        ),
    )
    parser.set_defaults(inject_sandbox_query_port_arg=False)
    parser.add_argument("--host-game-port", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--host-query-port", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--sandbox-game-port", type=int, help=argparse.SUPPRESS)
    parser.add_argument(
        "--online-stage-room-name",
        default="",
        help=(
            "Diagnostic room-name target. Setting this forces named-room "
            "matching and can bypass the normal host UI creation flow."
        ),
    )
    parser.add_argument(
        "--online-stage-target-owner-id",
        default="",
        help=(
            "Steam owner ID to target in SC6/Steam search results. When set, "
            "the MVP path leaves room/session names empty so host creation "
            "can follow the normal UI flow. When omitted for --mvp-online-match, "
            "the gate infers the host SteamID from loginusers.vdf."
        ),
    )
    parser.add_argument(
        "--infer-online-stage-target-owner",
        action="store_true",
        help=(
            "Kept for older command lines; --mvp-online-match now infers the "
            "host SteamID by default."
        ),
    )
    parser.add_argument(
        "--skip-online-stage-drive",
        action="store_true",
        help=(
            "Do not automate SC6 online menu/create/join during direct-release. "
            "Use this with launched clients or --manual-attach when the match "
            "is created through the normal UI/manual flow."
        ),
    )
    parser.add_argument("--launch-wait-seconds", type=float, default=120.0)
    parser.add_argument("--client-ready-seconds", type=float, default=120.0)
    parser.add_argument("--role-manifest-watch-seconds", type=float, default=90.0)
    parser.add_argument("--post-launch-settle-seconds", type=float, default=0.0)
    parser.add_argument("--direct-release-watch-seconds", type=float, default=180.0)
    parser.add_argument("--observe-gameflow-watch-seconds", type=float, default=180.0)
    parser.add_argument("--acceptance-timeout", type=int, default=900)
    parser.add_argument(
        "--main-menu-player-match-route",
        default=two_client.DEFAULT_MAIN_MENU_PLAYER_MATCH_ROUTE,
        help=(
            "Comma-separated UIGameFlowAutomation input route from "
            "MainMenuScene_C to Player Match."
        ),
    )
    parser.add_argument("--high-priority", action="store_true")
    parser.add_argument(
        "--fault-profile",
        choices=[
            "clean_0ms", "wifi_50ms_jitter",
            "bad_wifi_120ms_5pct_loss", "overseas_180ms_2pct_loss",
            "spike_every_10s", "burst_loss_500ms", "corrupt_probe",
        ],
        default="clean_0ms",
    )
    parser.add_argument(
        "--fault-seed", type=lambda value: int(value, 0),
        default=0x5C6B0001,
    )
    parser.add_argument(
        "--validation-bundle", type=Path,
        help=(
            "Schema-v2 complete local-regression report to reuse. Without "
            "this, a normal launch gate runs the full validation bundle first."
        ),
    )
    parser.add_argument("--max-validation-age-hours", type=float, default=24.0)
    args = parser.parse_args()
    if args.fault_profile != "clean_0ms":
        parser.error(
            "non-clean --fault-profile is unsupported by the direct-connect "
            "release gate; run mirrored-versus acceptance explicitly"
        )
    if (args.mvp_online_match_join_complete_compat
            or args.mvp_online_match_transport_ready_compat):
        parser.error(
            "manual join-complete and transport-ready compatibility options "
            "are unsupported unsafe experiments")
    wait_for_player_match_explicit = argv_has_option(
        "--wait-for-player-match-then-test"
    )
    auto_online_stage_explicit = argv_has_option(
        "--auto-online-stage", "--drive-online-stage"
    )
    if args.auto_online_stage and args.wait_for_player_match_then_test:
        print(
            "error=--auto-online-stage cannot be combined with "
            "--wait-for-player-match-then-test",
            file=sys.stderr,
        )
        return 2
    if args.auto_online_stage and args.skip_online_stage_drive:
        print(
            "error=--auto-online-stage cannot be combined with "
            "--skip-online-stage-drive",
            file=sys.stderr,
        )
        return 2
    if args.full_player_match_test:
        args.mvp_online_match = True
        if wait_for_player_match_explicit:
            args.wait_for_player_match_then_test = True
        if (
            not argv_has_option("--observe-gameflow-watch-seconds")
            and args.observe_gameflow_watch_seconds
                == parser.get_default("observe_gameflow_watch_seconds")
        ):
            args.observe_gameflow_watch_seconds = 900.0
        if args.kill_existing is None and not args.manual_attach:
            args.kill_existing = True
    if args.kill_existing is None:
        args.kill_existing = not (args.skip_launch or args.manual_attach)
    if args.wait_for_player_match_then_test and args.capture_gameflow:
        print(
            "error=--wait-for-player-match-then-test cannot be combined with "
            "--capture-gameflow",
            file=sys.stderr,
        )
        return 2
    if args.manual_attach:
        args.skip_build = True
        args.skip_launch = True
        if not argv_has_option("--kill-existing", "--no-kill-existing"):
            args.kill_existing = False
        print(
            "manual-attach: using already-running SC6 clients; not killing, "
            "building, or launching.",
            flush=True,
        )
        if args.full_player_match_test and not args.wait_for_player_match_then_test:
            print(
                "manual-attach: running direct rollback acceptance now; "
                "pass --wait-for-player-match-then-test to wait/record first.",
                flush=True,
            )
        if args.auto_online_stage:
            print(
                "manual-attach: native online-stage automation is enabled for "
                "the already-running clients.",
                flush=True,
            )
    if args.auto_online_stage:
        print(
            "auto-online-stage: native Player Match automation enabled.",
            flush=True,
        )
    elif (
        args.full_player_match_test
        and args.wait_for_player_match_then_test
        and not wait_for_player_match_explicit
    ):
        print(
            "full-player-match-test: defaulting to manual wait mode; pass "
            "--auto-online-stage to drive Player Match automatically.",
            flush=True,
        )
    if args.wait_for_player_match_then_test:
        args.leave_running = True
    if (args.observe_gameflow or args.wait_for_player_match_then_test) and args.manual_attach:
        args.leave_running = True
    if args.setup_only:
        args.leave_running = True

    run_id = args.run_id or f"release-{now_id()}"
    steps: list[dict[str, Any]] = []
    failed = ""

    validation_bundle = args.validation_bundle
    if not args.setup_only and validation_bundle is None:
        if args.manual_attach:
            parser.error(
                "--manual-attach requires --validation-bundle; automatic "
                "validation would terminate the attached game clients"
            )
        validation_step = run_command(
            "local-regression-bundle",
            [sys.executable, str(FULL_VALIDATION)],
            3600,
        )
        bundle_path, _ = attach_report_from_output(validation_step)
        steps.append(validation_step)
        if not validation_step.get("ok") or bundle_path is None:
            failed = "local-regression-bundle"
        else:
            validation_bundle = bundle_path
            args.skip_build = True
    if not args.setup_only and not failed and validation_bundle is not None:
        validation_step = validate_validation_bundle(
            validation_bundle, args.max_validation_age_hours
        )
        steps.append(validation_step)
        if not validation_step.get("ok"):
            failed = "validate-local-regression-bundle"

    targeting = resolve_online_stage_targeting(args, run_id)
    args.resolved_online_stage_room_name = targeting["room_name"]
    args.resolved_online_stage_target_owner_id = targeting["target_owner_id"]
    if args.mvp_online_match:
        steps.append({
            "name": "mvp-online-targeting",
            "ok": bool(targeting.get("ok")),
            **targeting,
        })
        if not targeting.get("ok"):
            failed = "mvp-online-targeting"

    if args.kill_existing:
        step = kill_sc6(timeout=30)
        steps.append(step)
        if not step.get("ok") and not failed:
            failed = "kill-existing"

    if not failed and not args.skip_build:
        step = run_command("build-and-deploy", ["cmd", "/c", str(BUILD_BAT)], 900)
        steps.append(step)
        if not step.get("ok"):
            failed = "build-and-deploy"

    if not failed and not args.skip_launch:
        if not args.steam_exe.exists():
            steps.append({
                "name": "steam-exe-check",
                "ok": False,
                "path": str(args.steam_exe),
                "failure": "Steam executable not found",
            })
            failed = "steam-exe-check"
        elif not args.sandboxie_start.exists():
            steps.append({
                "name": "sandboxie-start-check",
                "ok": False,
                "path": str(args.sandboxie_start),
                "failure": "Sandboxie Start.exe not found",
            })
            failed = "sandboxie-start-check"
        else:
            step = launch_host(args)
            steps.append(step)
            if not step.get("ok"):
                failed = "launch-host-sc6"
            else:
                host_ready_step = wait_for_role_ready(
                    "host",
                    args,
                    args.client_ready_seconds,
                )
                steps.append(host_ready_step)
                if not host_ready_step.get("ok"):
                    failed = "wait-for-host-ready"

            if not failed:
                step = launch_sandbox(args)
                steps.append(step)
                if not step.get("ok"):
                    failed = "launch-sandbox-sc6"

            if not failed:
                wait_step = wait_for_two_sc6(args.launch_wait_seconds)
                steps.append(wait_step)
                if not wait_step.get("ok"):
                    failed = "wait-for-two-sc6"

            if not failed:
                sandbox_ready_step = wait_for_role_ready(
                    "sandbox",
                    args,
                    args.client_ready_seconds,
                )
                steps.append(sandbox_ready_step)
                if not sandbox_ready_step.get("ok"):
                    failed = "wait-for-sandbox-ready"

            if not failed:
                steps.append(settle_after_launch(args.post_launch_settle_seconds))

    if not failed and args.setup_only:
        steps.append({
            "name": "operator-handoff",
            "ok": True,
            "message": (
                "Both clients are launched. Press Start / enter the main menu "
                "on both clients, create/join Player Match manually, then run "
                "the manual-attach full proof."
            ),
        })

    if not failed and args.manual_attach:
        wait_step = wait_for_two_sc6(args.launch_wait_seconds)
        steps.append(wait_step)
        if not wait_step.get("ok"):
            failed = "wait-for-two-sc6"
        if not failed:
            host_ready_step = wait_for_role_ready(
                "host",
                args,
                args.client_ready_seconds,
            )
            steps.append(host_ready_step)
            if not host_ready_step.get("ok"):
                failed = "wait-for-host-ready"
        if not failed:
            sandbox_ready_step = wait_for_role_ready(
                "sandbox",
                args,
                args.client_ready_seconds,
            )
            steps.append(sandbox_ready_step)
            if not sandbox_ready_step.get("ok"):
                failed = "wait-for-sandbox-ready"

    if not failed and not args.setup_only:
        if args.wait_for_player_match_then_test:
            print(
                "operator-handoff: press Start / enter main menu on both "
                "clients, create/join Player Match, and keep this command "
                "running; rollback proof starts automatically once both "
                "clients reach PlayerMatchScene_C.",
                flush=True,
            )
            observe_process_events = args.observe_gameflow_process_events
            args.observe_gameflow_process_events = False
            observe_step = run_gameflow_observe(args, f"{run_id}-wait-player-match")
            args.observe_gameflow_process_events = observe_process_events
            observe_step["name"] = "wait-player-match"
            observe_step["wait_for_player_match_then_test"] = True
            observe_step["observe_gameflow_process_events"] = False
            attach_report_from_output(observe_step)
            steps.append(observe_step)
            if not observe_step.get("ok"):
                failed = "wait-player-match"
            if not failed:
                direct_step = run_acceptance(args, f"{run_id}-direct-release")
                direct_step["name"] = "direct-release-after-player-match"
                direct_step["wait_for_player_match_then_test"] = True
                attach_report_from_output(direct_step)
                steps.append(direct_step)
                if not direct_step.get("ok"):
                    failed = "direct-release-acceptance"
        else:
            if args.observe_gameflow:
                step = run_gameflow_observe(args, run_id)
            elif args.full_player_match_test:
                layered_phases = (
                    "inventory,role-manifest,sidecar,"
                    "menu-ready,player-match-nav,player-match-lobby,"
                    "player-match-battle,rollback-proof"
                )
                step = run_acceptance(
                    args,
                    run_id,
                    phases_override=layered_phases,
                    skip_online_stage_drive_override=False,
                )
                step["name"] = "layered-player-match-acceptance"
            else:
                step = run_acceptance(args, run_id)
            attach_report_from_output(step)
            steps.append(step)
            if not step.get("ok"):
                failed = "gameflow-observe" if args.observe_gameflow else (
                    "layered-player-match-acceptance"
                    if args.full_player_match_test
                    else "direct-release-acceptance"
                )

    if args.leave_running:
        steps.append(disable_rollback_lab(args, run_id))

    if not args.leave_running:
        steps.append(kill_sc6(timeout=30))

    ok = not failed
    direct_acceptance_json = next(
        (
            step.get("report_json")
            for step in reversed(steps)
            if step.get("name") in {
                "direct-release-acceptance",
                "direct-release-after-player-match",
                "layered-player-match-acceptance",
            }
            and isinstance(step.get("report_json"), dict)
        ),
        {},
    )
    packet_timeline = direct_acceptance_json.get("packet_timeline") or (
        two_client.packet_timeline_from_results(
            direct_acceptance_json.get("direct_release_summary", [])
        )
        if direct_acceptance_json else {}
    )
    packet_timeline_summary = (
        direct_acceptance_json.get("packet_timeline_summary")
        or two_client.summarize_packet_timeline_results(packet_timeline)
    )
    gate_timeline = str(direct_acceptance_json.get("gate_timeline", ""))
    online_stage_target_owner_id = int(
        getattr(args, "resolved_online_stage_target_owner_id", 0)
    )
    online_stage_room_name = str(
        getattr(args, "resolved_online_stage_room_name", "") or ""
    )
    acceptance_step = next(
        (step for step in reversed(steps)
         if step.get("name") in {
             "direct-release-acceptance",
             "direct-release-after-player-match",
             "layered-player-match-acceptance",
         }),
        None,
    )
    required = [] if args.setup_only else ["validate-local-regression-bundle", "two-client-acceptance"]
    observed = [
        "validate-local-regression-bundle"
        for step in steps if step.get("name") == "validate-local-regression-bundle"
    ]
    if acceptance_step is not None:
        nested = acceptance_step.get("report_json") or {}
        if (nested.get("schema_version") == 2
                and nested.get("verdict") == "pass"
                and nested.get("acceptance_executed") is True):
            observed.append("two-client-acceptance")
        elif not failed:
            failed = "two-client-acceptance-contract"
            ok = False
    coverage_result = coverage(required, observed)
    contract = contract_fields(
        workflow_kind="release-gate",
        workflow_ok=ok,
        coverage_result=coverage_result,
        setup_only=args.setup_only,
        acceptance_workflow=not args.setup_only,
    )
    report = {
        **contract,
        "generated_at": utc_now(),
        "run_id": run_id,
        "repo": str(two_client.REPO),
        "game_exe": str(two_client.GAME_EXE),
        "steam_exe": str(args.steam_exe),
        "steam_app_id": str(args.steam_app_id),
        "launch_style": (
            f"steam-applaunch-sandbox-queryport{args.sandbox_query_port}"
            + ("-injected" if args.inject_sandbox_query_port_arg else "-steamopt")
        ),
        "full_player_match_test": args.full_player_match_test,
        "setup_only": args.setup_only,
        "fault_profile": args.fault_profile,
        "fault_seed": f"0x{args.fault_seed:X}",
        "validation_bundle": str(validation_bundle or ""),
        "artifacts": {
            "built_dll": artifact(BUILT_DLL),
            "deployed_dll": artifact(DEPLOYED_DLL),
            "replay_input": artifact(STRICT_REPLAY),
        },
        "kill_existing": args.kill_existing,
        "manual_attach": args.manual_attach,
        "auto_online_stage": args.auto_online_stage,
        "auto_online_stage_explicit": auto_online_stage_explicit,
        "skip_online_stage_drive": should_skip_online_stage_drive(args),
        "sandbox_root": str(args.sandbox_root),
        "sandbox_box": args.sandbox_box,
        "sandboxie_start": str(args.sandboxie_start),
        "sandbox_query_port": args.sandbox_query_port,
        "inject_sandbox_query_port_arg": args.inject_sandbox_query_port_arg,
        "direct_release_watch_seconds": args.direct_release_watch_seconds,
        "observe_gameflow": args.observe_gameflow,
        "wait_for_player_match_then_test": args.wait_for_player_match_then_test,
        "capture_gameflow": args.capture_gameflow,
        "observe_gameflow_process_events": args.observe_gameflow_process_events,
        "main_menu_player_match_route": args.main_menu_player_match_route,
        "wait_player_match_process_events": (
            False if args.wait_for_player_match_then_test else None
        ),
        "observe_gameflow_watch_seconds": args.observe_gameflow_watch_seconds,
        "mvp_online_match": args.mvp_online_match,
        "online_stage_room_name": online_stage_room_name,
        "online_stage_target_owner_id": online_stage_target_owner_id,
        "online_stage_targeting": targeting,
        "mvp_online_match_join_complete_compat": (
            args.mvp_online_match_join_complete_compat
        ),
        "mvp_online_match_transport_ready_compat": (
            args.mvp_online_match_transport_ready_compat
        ),
        "mvp_online_match_ready_open_compat": (
            args.mvp_online_match_ready_open_compat
        ),
        "mvp_online_match_peer_route_tag_fix": (
            args.mvp_online_match_peer_route_tag_fix
        ),
        "mvp_online_match_network_check_compat": (
            args.mvp_online_match_network_check_compat
        ),
        "mvp_online_match_in_room_transition_compat": (
            args.mvp_online_match_in_room_transition_compat
        ),
        "mvp_online_match_direct_native_join_diagnostic": (
            args.mvp_online_match_direct_native_join_diagnostic
        ),
        "packet_timeline": packet_timeline,
        "packet_timeline_summary": packet_timeline_summary,
        "gate_timeline": gate_timeline,
        "failed_step": failed,
        "steps": steps,
    }
    report_path = write_report(report)
    label = "rollback gameflow observe" if args.observe_gameflow else (
        "rollback release gate"
    )
    print(f"{label} {contract['verdict'].upper()}")
    print(f"report={report_path}")
    if gate_timeline:
        print(f"gate-timeline {gate_timeline}")
    if failed:
        print(f"failed_step={failed}", file=sys.stderr)
    packet_summary = report.get("packet_timeline_summary") or {}
    if packet_summary and not args.observe_gameflow and direct_acceptance_json:
        roles = packet_summary.get("roles") or {}
        host_stop = (roles.get("host") or {}).get("stop_boundary", "")
        sandbox_stop = (roles.get("sandbox") or {}).get("stop_boundary", "")
        print(
            "packet-timeline "
            f"opcode={packet_summary.get('opcode')} "
            f"diagnosis={packet_summary.get('diagnosis')} "
            f"host_stop={host_stop} "
            f"sandbox_stop={sandbox_stop}"
        )
    for step in steps:
        if step.get("name") == "operator-handoff":
            print(step.get("message", ""))
        if step.get("name") in {
            "direct-release-acceptance",
            "gameflow-observe",
            "wait-player-match",
            "direct-release-after-player-match",
            "layered-player-match-acceptance",
        }:
            if step.get("name") == "wait-player-match":
                print_wait_player_match_summary(step)
            else:
                print(step.get("output", ""))
    return 0 if contract["verdict"] in {"pass", "setup-ready"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
