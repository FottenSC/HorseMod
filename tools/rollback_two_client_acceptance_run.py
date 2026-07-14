#!/usr/bin/env python3
"""Single-command rollback two-client acceptance ladder.

The default lane launches a normal client and a Sandboxie client into the same
real replay and runs the deterministic replay-fork GekkoNet fixture. Production
Local VS and Player Match remain explicit attach/certification lanes.

This is the operator-facing rollback test command. The lower-level
rollback_two_client_test_run.py script is kept as the internal per-phase worker
so each phase can still produce a focused report and fail-closed cleanup.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

from rollback_report_contract import contract_fields, coverage, utc_now

import rollback_two_client_test_run as two_client
from sc6_launch_catalog import (
    LaunchSelectionError,
    resolve_character,
    resolve_stage,
)


REPORT_DIR = two_client.REPO / "reports" / "rollback_two_client_acceptance"

DIRECT_CONNECT_PHASES = [
    "inventory",
    "role-manifest",
    "sidecar",
    "sidecar-fault-closed",
    "menu-ready",
    "player-match-nav",
    "player-match-lobby",
    "player-match-battle",
    "rollback-proof",
]

STEAM_ONLINE_PHASES = [
    "inventory",
    "role-manifest",
    "sidecar",
    "sidecar-fault-closed",
    "online-stage",
    "stock-online",
    "activation",
    "live-replay-input",
    "live-correction",
    "soak",
]

MIRRORED_VERSUS_PHASES = [
    "inventory",
    "role-manifest",
    "horse-udp-ready",
    "mirrored-versus-battle",
    "rollback-proof",
    "soak",
]

DEFAULT_PHASES = []

PREFLIGHT_PHASES = [
    "inventory",
    "role-manifest",
    "sidecar",
    "sidecar-fault-closed",
]

INVENTORY_PHASES = ["inventory"]

ONLINE_STAGE_GATE_PHASES = [
    "menu-ready",
    "player-match-nav",
    "player-match-lobby",
    "player-match-battle",
]

ONLINE_STAGE_PHASES = ["online-stage"]

OBSERVE_GAMEFLOW_PHASES = ["inventory", "observe-gameflow"]

SIDECAR_OWNING_PHASES = {
    "sidecar",
    "sidecar-fault-closed",
    "direct-stage",
    "direct-connect",
    "direct-replay-input",
    "direct-correction",
    "menu-ready",
    "player-match-nav",
    "player-match-lobby",
    "player-match-battle",
    "online-stage",
    "stock-online",
    "activation",
    "live-replay-input",
    "live-correction",
    "soak",
    "horse-udp-ready",
    "mirrored-versus-setup",
    "mirrored-versus-battle",
}


def now_id() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S-%f")


def default_stage_name(prefix: str, run_id: str) -> str:
    # SC6 can mask digit-heavy room strings in Steam metadata, which makes the
    # native join target impossible to identify. Keep generated names short and
    # alphabetic so they survive the stock room-name path.
    value = two_client.fnv1a64(run_id) & 0xFFFFFFFF
    letters = []
    for _ in range(8):
        letters.append(chr(ord("A") + (value % 26)))
        value //= 26
    return f"{prefix}{''.join(letters)}"


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
                "$p=Get-Process SoulcaliburVI -ErrorAction SilentlyContinue; "
                "if ($p) { $p | Stop-Process -Force }; exit 0"
            ),
        ],
        timeout,
    )


def cleanup_request_files(sandbox_root: Path, sandbox_box: str) -> dict[str, Any]:
    snap = two_client.snapshot("cleanup")
    roots = two_client.discover_roots(
        set(snap["sc6_pids"]),
        sandbox_root,
        sandbox_box,
    )
    removed: list[str] = []
    for root in roots:
        root_path = Path(root["path"])
        request_path = root_path / "rollback_lab_request.txt"
        if request_path.exists():
            removed.append(str(request_path))
        two_client.remove_request_file(root_path)
    return {
        "name": "cleanup-request-files",
        "ok": True,
        "removed": removed,
        "roots": roots,
        "snapshot": snap,
    }


def wait_for_two_sc6(seconds: float) -> dict[str, Any]:
    deadline = time.time() + max(0.0, seconds)
    latest = two_client.snapshot("wait-for-two-sc6")
    while time.time() < deadline:
        latest = two_client.snapshot("wait-for-two-sc6")
        if len(latest["sc6_pids"]) == 2:
            return {
                "name": "wait-for-two-sc6",
                "ok": True,
                "snapshot": latest,
            }
        time.sleep(1.0)
    return {
        "name": "wait-for-two-sc6",
        "ok": len(latest["sc6_pids"]) == 2,
        "snapshot": latest,
    }


def role_pids_from_snapshot(
    snap: dict[str, Any],
    sandbox_root: Path,
    sandbox_box: str,
) -> dict[str, int]:
    current_pids = {
        two_client.int_value(pid, -1)
        for pid in snap.get("sc6_pids", [])
        if two_client.int_value(pid, -1) >= 0
    }
    roots = two_client.discover_roots(
        current_pids,
        sandbox_root,
        sandbox_box,
        processes=snap.get("processes", []),
    )
    role_pids: dict[str, int] = {}
    for root in roots:
        live_pids = [
            two_client.int_value(pid, -1)
            for pid in root.get("live_trace_pids", [])
            if two_client.int_value(pid, -1) >= 0
        ]
        if len(live_pids) == 1:
            role_pids[str(root.get("role", ""))] = live_pids[0]

    launch_rows = [
        launch for launch in snap.get("sc6_launch_args", [])
        if isinstance(launch, dict)
    ]
    if "sandbox" not in role_pids:
        sandbox_candidates = [
            two_client.int_value(launch.get("pid"), -1)
            for launch in launch_rows
            if two_client.int_value(launch.get("query_port"), 0)
            == two_client.DEFAULT_SANDBOX_QUERY_PORT
        ]
        sandbox_candidates = [pid for pid in sandbox_candidates if pid >= 0]
        if len(sandbox_candidates) == 1:
            role_pids["sandbox"] = sandbox_candidates[0]
    if "host" not in role_pids and "sandbox" in role_pids:
        host_candidates = [
            two_client.int_value(launch.get("pid"), -1)
            for launch in launch_rows
            if (
                two_client.int_value(launch.get("pid"), -1) >= 0
                and two_client.int_value(launch.get("pid"), -1)
                != role_pids["sandbox"]
            )
        ]
        if len(host_candidates) == 1:
            role_pids["host"] = host_candidates[0]
    return role_pids


def confirmed_crash_indicators(probe_pids: set[int]) -> list[dict[str, Any]]:
    indicators = two_client.enumerate_crash_indicators(probe_pids)
    non_hung = [
        indicator
        for indicator in indicators
        if str(indicator.get("source") or "") != "hung-window"
    ]
    hung = [
        indicator
        for indicator in indicators
        if str(indicator.get("source") or "") == "hung-window"
    ]
    if non_hung or not hung:
        return indicators

    time.sleep(3.0)
    second = two_client.enumerate_crash_indicators(probe_pids)
    confirmed_hung_pids = {
        two_client.int_value(indicator.get("pid"), -1)
        for indicator in second
        if str(indicator.get("source") or "") == "hung-window"
    }
    return [
        indicator
        for indicator in hung
        if two_client.int_value(indicator.get("pid"), -1)
        in confirmed_hung_pids
    ]


def acceptance_health_snapshot(
    label: str,
    args: argparse.Namespace,
    *,
    expected_pids: set[int] | None = None,
    expected_role_pids: dict[str, int] | None = None,
) -> dict[str, Any]:
    snap = two_client.snapshot(label)
    current_pids = {
        two_client.int_value(pid, -1)
        for pid in snap.get("sc6_pids", [])
        if two_client.int_value(pid, -1) >= 0
    }
    expected_pids = set(expected_pids or [])
    expected_role_pids = dict(expected_role_pids or {})
    probe_pids = expected_pids or current_pids
    crash_indicators = confirmed_crash_indicators(probe_pids)
    current_role_pids = role_pids_from_snapshot(
        snap,
        args.sandbox_root,
        args.sandbox_box,
    )
    failures: list[str] = []

    if expected_pids:
        missing = sorted(expected_pids - current_pids)
        extra = sorted(current_pids - expected_pids)
        if missing:
            failures.append(
                "expected_sc6_pids_missing="
                + ",".join(str(pid) for pid in missing)
            )
        if extra:
            failures.append(
                "unexpected_sc6_pids="
                + ",".join(str(pid) for pid in extra)
            )
    if expected_role_pids:
        for role, pid in sorted(expected_role_pids.items()):
            if pid not in current_pids:
                failures.append(
                    f"sc6_process_exited role={role} pid={pid} "
                    f"current={sorted(current_pids)}"
                )
            elif current_role_pids.get(role) not in {None, pid}:
                failures.append(
                    f"sc6_role_pid_changed role={role} expected={pid} "
                    f"current={current_role_pids.get(role)}"
                )

    for indicator in crash_indicators:
        pid = two_client.int_value(indicator.get("pid"), -1)
        related_pid = two_client.int_value(indicator.get("related_pid"), -1)
        title = str(indicator.get("title") or "").replace("\n", " ")[:120]
        process_name = str(indicator.get("process_name") or "")
        source = str(indicator.get("source") or "unknown")
        prefix = (
            "sc6_hung_window"
            if source == "hung-window" else "sc6_crash_dialog"
        )
        failures.append(
            f"{prefix} pid={pid} related={related_pid} "
            f"source={source} process={process_name or '-'} "
            f"title={title or '-'}"
        )

    return {
        "label": label,
        "ok": not failures,
        "failures": failures,
        "current_sc6_pids": sorted(current_pids),
        "expected_sc6_pids": sorted(expected_pids),
        "current_role_pids": current_role_pids,
        "expected_role_pids": expected_role_pids,
        "crash_indicators": crash_indicators,
    }


def attach_post_step_health(
    step: dict[str, Any],
    args: argparse.Namespace,
    *,
    expected_pids: set[int],
    expected_role_pids: dict[str, int],
) -> None:
    if not expected_pids:
        return
    health = acceptance_health_snapshot(
        f"post-{step.get('name', 'step')}",
        args,
        expected_pids=expected_pids,
        expected_role_pids=expected_role_pids,
    )
    step["post_step_health"] = health
    if not health["ok"]:
        step["ok"] = False
        step["health_failures"] = health["failures"]


def parse_phases(text: str) -> list[str]:
    phases = [part.strip() for part in text.split(",") if part.strip()]
    known = set(
        DIRECT_CONNECT_PHASES
        + STEAM_ONLINE_PHASES
        + MIRRORED_VERSUS_PHASES
        + OBSERVE_GAMEFLOW_PHASES
        + ["identity", "horse-udp"]
    )
    unknown = [phase for phase in phases if phase not in known]
    if unknown:
        raise ValueError("unknown phase(s): " + ", ".join(unknown))
    return phases


def selected_phases(args: argparse.Namespace) -> list[str]:
    presets = [
        bool(args.phases),
        args.inventory_only,
        args.preflight_only,
        args.online_stage_only,
        args.diagnostic_online_stage,
        args.observe_gameflow,
        args.cleanup_only,
    ]
    if sum(1 for selected in presets if selected) > 1:
        raise ValueError(
            "--phases, --inventory-only, --preflight-only, "
            "--online-stage-only, --diagnostic-online-stage, and "
            "--observe-gameflow, and --cleanup-only are mutually exclusive"
        )
    if args.cleanup_only:
        return []
    if args.phases:
        return parse_phases(args.phases)
    if args.inventory_only:
        return list(INVENTORY_PHASES)
    if args.preflight_only:
        return list(PREFLIGHT_PHASES)
    if args.online_stage_only or args.diagnostic_online_stage:
        return list(ONLINE_STAGE_PHASES)
    if args.observe_gameflow:
        return list(OBSERVE_GAMEFLOW_PHASES)
    if args.mode == "steam-online":
        return list(STEAM_ONLINE_PHASES)
    if args.mode == "mirrored-versus":
        return list(MIRRORED_VERSUS_PHASES)
    return list(DIRECT_CONNECT_PHASES)


def watch_seconds_for_phase(args: argparse.Namespace, phase: str) -> float:
    if phase == "inventory":
        return 0.0
    if phase in {"identity", "role-manifest"}:
        return args.role_manifest_watch_seconds
    if phase in {"sidecar", "horse-udp", "horse-udp-ready"}:
        return args.sidecar_watch_seconds
    if phase == "mirrored-versus-setup":
        return args.online_stage_watch_seconds
    if phase == "mirrored-versus-battle":
        return args.direct_release_watch_seconds
    if phase == "sidecar-fault-closed":
        return args.fault_watch_seconds
    if phase == "online-stage":
        return args.online_stage_watch_seconds
    if phase in {"menu-ready", "player-match-nav"}:
        return args.online_stage_watch_seconds
    if phase in {"player-match-lobby", "player-match-battle"}:
        return args.direct_release_watch_seconds
    if phase == "observe-gameflow":
        return args.observe_gameflow_watch_seconds
    if phase == "direct-stage":
        return args.direct_stage_watch_seconds
    if phase == "direct-connect":
        return args.direct_connect_watch_seconds
    if phase == "direct-replay-input":
        return args.direct_replay_input_watch_seconds
    if phase == "direct-correction":
        return args.direct_correction_watch_seconds
    if phase == "direct-release":
        return args.direct_release_watch_seconds
    if phase == "rollback-proof":
        return args.direct_release_watch_seconds
    if phase == "stock-online":
        return args.stock_online_watch_seconds
    if phase == "activation":
        return args.activation_watch_seconds
    if phase == "live-replay-input":
        return args.live_replay_input_watch_seconds
    if phase == "live-correction":
        return args.live_correction_watch_seconds
    if phase == "soak":
        return args.soak_watch_seconds
    return args.role_manifest_watch_seconds


def phase_timeout(phase: str, watch_seconds: float) -> int:
    if phase == "inventory":
        return 180
    if phase in {
        "direct-release",
        "rollback-proof",
        "mirrored-versus-battle",
    }:
        return max(180, int(watch_seconds) + 150)
    if phase in ONLINE_STAGE_GATE_PHASES:
        return max(120, int(watch_seconds) + 90)
    if phase == "observe-gameflow":
        return max(90, int(watch_seconds) + 45)
    return max(90, int(watch_seconds) + 45)


def online_stage_goal_for_phase(phase: str) -> str:
    return {
        "menu-ready": "main-menu",
        "player-match-nav": "player-match-nav",
        "player-match-lobby": "player-match-lobby",
        "player-match-battle": "player-match-battle",
        "rollback-proof": "proof-only",
    }.get(phase, "player-match-battle")


def append_main_user_override_args(
    cmd: list[str],
    args: argparse.Namespace,
) -> None:
    if args.online_stage_main_user_id_override is not None:
        cmd.extend([
            "--online-stage-main-user-id-override",
            str(args.online_stage_main_user_id_override),
        ])
    if args.host_online_stage_main_user_id_override is not None:
        cmd.extend([
            "--host-online-stage-main-user-id-override",
            str(args.host_online_stage_main_user_id_override),
        ])
    if args.sandbox_online_stage_main_user_id_override is not None:
        cmd.extend([
            "--sandbox-online-stage-main-user-id-override",
            str(args.sandbox_online_stage_main_user_id_override),
        ])


def run_phase(
    *,
    args: argparse.Namespace,
    phase: str,
    watch_seconds: float,
    run_id: str,
    activation_token: str,
) -> dict[str, Any]:
    if phase == "observe-gameflow":
        report_path = (
            two_client.OBSERVE_REPORT_DIR
            / f"rollback_gameflow_observe_{run_id}_{phase}.json"
        )
    else:
        report_path = (
            REPORT_DIR / f"rollback_two_client_acceptance_{run_id}_{phase}.json"
        )
    cmd = [
        sys.executable,
        str(two_client.REPO / "tools" / "rollback_two_client_test_run.py"),
        "--internal-phase-worker",
        "--phase",
        phase,
        "--mode",
        args.mode,
        "--stock-join-route",
        args.stock_join_route,
        "--require-two-sc6",
        "--require-udp-safety",
        "--sandbox-root",
        str(args.sandbox_root),
        "--sandbox-box",
        args.sandbox_box,
        "--rollback-window",
        str(args.rollback_window),
        "--seed",
        args.seed,
        "--native-input-source-slot",
        str(args.native_input_source_slot),
        "--input-delay",
        str(args.input_delay),
        "--fault-profile",
        args.fault_profile,
        "--fault-seed",
        f"0x{args.fault_seed:X}",
        "--expected-build-id",
        f"0x{args.expected_build_id:X}",
        "--expected-schema-id",
        f"0x{args.expected_schema_id:X}",
        "--host-sidecar-port",
        str(args.host_sidecar_port),
        "--sandbox-sidecar-port",
        str(args.sandbox_sidecar_port),
        "--activation-token",
        activation_token,
        "--online-stage-native-session-name",
        args.online_stage_native_session_name,
        "--online-stage-session-name",
        args.online_stage_session_name,
        "--online-stage-room-name",
        args.online_stage_room_name,
        "--online-stage-target-owner-id",
        str(args.online_stage_target_owner_id),
        "--replay-input-file",
        str(args.replay_input_file),
        "--main-menu-player-match-route",
        args.main_menu_player_match_route,
        "--replay-divergence-frame",
        str(args.replay_divergence_frame),
        "--replay-divergence-window",
        str(args.replay_divergence_window),
        "--run-id",
        (
            f"{run_id}-mirrored-versus"
            if (
                args.mode == "mirrored-versus"
                and phase in MIRRORED_VERSUS_PHASES[2:]
            )
            else f"{run_id}-{phase}"
        ),
        "--report-output",
        str(report_path),
    ]
    if args.left_character is not None:
        cmd.extend(["--left-character", args.left_character])
    if args.right_character is not None:
        cmd.extend(["--right-character", args.right_character])
    if args.stage is not None:
        cmd.extend(["--stage", args.stage])
    append_main_user_override_args(cmd, args)
    if args.strict:
        cmd.append("--strict")
    uses_online_stage = phase in {
        "online-stage",
        "direct-release",
        *ONLINE_STAGE_GATE_PHASES,
    }
    if uses_online_stage:
        cmd.extend(["--online-stage-goal", online_stage_goal_for_phase(phase)])
    if args.online_stage_diagnostic_reflection and uses_online_stage:
        cmd.append("--online-stage-diagnostic-reflection")
    if args.debug_steam_probe and uses_online_stage:
        cmd.append("--debug-steam-probe")
    if args.online_stage_fail_fast_empty_finds and uses_online_stage:
        cmd.append("--online-stage-fail-fast-empty-finds")
        cmd.extend([
            "--online-stage-fail-fast-empty-find-attempts",
            str(args.online_stage_fail_fast_empty_find_attempts),
        ])
    if args.debug_steam_filter_probe and uses_online_stage:
        cmd.append("--debug-steam-filter-probe")
    if args.debug_direct_stage_begin_play and phase == "direct-stage":
        cmd.append("--debug-direct-stage-begin-play")
    if args.online_stage_host_room_ready_gate and uses_online_stage:
        cmd.append("--online-stage-host-room-ready-gate")
    if args.online_stage_network_check_compat and uses_online_stage:
        cmd.append("--online-stage-network-check-compat")
    if args.online_stage_join_complete_compat and uses_online_stage:
        cmd.append("--online-stage-join-complete-compat")
    if args.online_stage_transport_ready_compat and uses_online_stage:
        cmd.append("--online-stage-transport-ready-compat")
    if args.online_stage_ready_open_compat and uses_online_stage:
        cmd.append("--online-stage-ready-open-compat")
    if args.online_stage_peer_route_tag_fix and uses_online_stage:
        cmd.append("--online-stage-peer-route-tag-fix")
    if args.online_stage_in_room_transition_compat and uses_online_stage:
        cmd.append("--online-stage-in-room-transition-compat")
    if args.online_stage_direct_native_join_diagnostic and uses_online_stage:
        cmd.append("--online-stage-direct-native-join-diagnostic")
    if args.online_stage_no_presence_find and uses_online_stage:
        cmd.append("--online-stage-no-presence-find")
    if args.skip_online_stage_drive and phase == "direct-release":
        cmd.append("--skip-online-stage-drive")
    if args.capture_gameflow and phase == "observe-gameflow":
        cmd.append("--capture-gameflow")
    if phase == "observe-gameflow":
        if args.observe_gameflow_process_events:
            cmd.append("--observe-gameflow-process-events")
        else:
            cmd.append("--no-observe-gameflow-process-events")
    if phase != "inventory":
        cmd.extend([
            "--watch-seconds",
            str(watch_seconds),
        ])
        if args.mode != "mirrored-versus" or phase == "soak":
            cmd.append("--cleanup-request-after")

    result = run_command(
        f"phase-{phase}",
        cmd,
        phase_timeout(phase, watch_seconds),
    )
    result["phase"] = phase
    result["phase_report"] = str(report_path)
    if report_path.exists():
        try:
            result["report_json"] = json.loads(report_path.read_text("utf-8"))
        except json.JSONDecodeError as exc:
            result["report_json_error"] = str(exc)
    return result


def run_online_stage_cleanup(
    *,
    args: argparse.Namespace,
    run_id: str,
    activation_token: str,
) -> dict[str, Any]:
    report_path = (
        REPORT_DIR
        / f"rollback_two_client_acceptance_{run_id}_cleanup-online-stage.json"
    )
    watch_seconds = max(20.0, args.role_manifest_watch_seconds)
    cmd = [
        sys.executable,
        str(two_client.REPO / "tools" / "rollback_two_client_test_run.py"),
        "--internal-phase-worker",
        "--phase",
        "online-stage",
        "--mode",
        "steam-online",
        "--stock-join-route",
        args.stock_join_route,
        "--require-two-sc6",
        "--require-udp-safety",
        "--sandbox-root",
        str(args.sandbox_root),
        "--sandbox-box",
        args.sandbox_box,
        "--rollback-window",
        str(args.rollback_window),
        "--seed",
        args.seed,
        "--host-sidecar-port",
        str(args.host_sidecar_port),
        "--sandbox-sidecar-port",
        str(args.sandbox_sidecar_port),
        "--activation-token",
        activation_token,
        "--online-stage-native-session-name",
        args.online_stage_native_session_name,
        "--online-stage-session-name",
        args.online_stage_session_name,
        "--online-stage-room-name",
        args.online_stage_room_name,
        "--online-stage-target-owner-id",
        str(args.online_stage_target_owner_id),
        "--online-stage-cleanup-only",
        "--run-id",
        f"{run_id}-cleanup-online-stage",
        "--report-output",
        str(report_path),
        "--watch-seconds",
        str(watch_seconds),
        "--cleanup-request-after",
    ]
    append_main_user_override_args(cmd, args)
    if args.strict:
        cmd.append("--strict")

    result = run_command(
        "cleanup-online-stage",
        cmd,
        phase_timeout("online-stage", watch_seconds),
    )
    result["cleanup_phase"] = "online-stage"
    result["phase"] = "online-stage-cleanup"
    result["phase_report"] = str(report_path)
    if report_path.exists():
        try:
            result["report_json"] = json.loads(report_path.read_text("utf-8"))
        except json.JSONDecodeError as exc:
            result["report_json_error"] = str(exc)
    return result


def sidecar_port_check(args: argparse.Namespace) -> dict[str, Any]:
    snap = two_client.snapshot("sidecar-port-check")
    ports = {args.host_sidecar_port, args.sandbox_sidecar_port}
    rows = [
        row for row in snap.get("udp_endpoints", [])
        if int(row.get("local_port", -1)) in ports
    ]
    return {
        "name": "verify-sidecar-ports-clear",
        "ok": not rows,
        "rows": rows,
        "snapshot": snap,
    }


def run_disabled_sidecar_cleanup(
    *,
    args: argparse.Namespace,
    run_id: str,
    activation_token: str,
) -> dict[str, Any]:
    attempts: list[dict[str, Any]] = []
    watch_seconds = max(15.0, min(args.role_manifest_watch_seconds, 20.0))
    timeout_seconds = max(45, int(watch_seconds) + 20)

    for attempt in range(1, 3):
        report_path = (
            REPORT_DIR
            / (
                f"rollback_two_client_acceptance_{run_id}"
                f"_cleanup-disable-sidecar-attempt{attempt}.json"
            )
        )
        cmd = [
            sys.executable,
            str(two_client.REPO / "tools" / "rollback_two_client_test_run.py"),
            "--internal-phase-worker",
            "--phase",
            "role-manifest",
            "--mode",
            args.mode,
            "--stock-join-route",
            args.stock_join_route,
            "--require-two-sc6",
            "--require-udp-safety",
            "--sandbox-root",
            str(args.sandbox_root),
            "--sandbox-box",
            args.sandbox_box,
            "--rollback-window",
            str(args.rollback_window),
            "--seed",
            args.seed,
            "--host-sidecar-port",
            str(args.host_sidecar_port),
            "--sandbox-sidecar-port",
            str(args.sandbox_sidecar_port),
            "--activation-token",
            activation_token,
            "--run-id",
            f"{run_id}-cleanup-disable-sidecar-attempt{attempt}",
            "--report-output",
            str(report_path),
            "--watch-seconds",
            str(watch_seconds),
            "--cleanup-request-after",
        ]
        if args.strict:
            cmd.append("--strict")

        attempt_result = run_command(
            f"cleanup-disable-sidecar-attempt{attempt}",
            cmd,
            timeout_seconds,
        )
        attempt_result["cleanup_phase"] = "role-manifest"
        attempt_result["phase_report"] = str(report_path)
        if report_path.exists():
            try:
                attempt_result["report_json"] = json.loads(
                    report_path.read_text("utf-8"))
            except json.JSONDecodeError as exc:
                attempt_result["report_json_error"] = str(exc)
        attempt_result["sidecar_port_check"] = sidecar_port_check(args)
        attempts.append(attempt_result)
        if (attempt_result.get("ok")
                and attempt_result["sidecar_port_check"].get("ok")):
            break
        time.sleep(2.0)

    final_attempt = attempts[-1] if attempts else {}
    ok = bool(
        final_attempt.get("ok")
        and final_attempt.get("sidecar_port_check", {}).get("ok")
    )
    return {
        "name": "cleanup-disable-sidecar",
        "ok": ok,
        "cleanup_phase": "role-manifest",
        "attempts": attempts,
        "phase_report": final_attempt.get("phase_report", ""),
        "sidecar_port_check": final_attempt.get("sidecar_port_check", {}),
        "failure": "" if ok else "sidecar-disable-or-port-clear-failed",
    }


def run_public_cleanup(
    *,
    args: argparse.Namespace,
    run_id: str,
    activation_token: str,
) -> tuple[list[dict[str, Any]], bool, bool]:
    steps: list[dict[str, Any]] = []
    cleanup_online_stage_attempted = bool(args.cleanup_online_stage)
    if cleanup_online_stage_attempted:
        steps.append(run_online_stage_cleanup(
            args=args,
            run_id=run_id,
            activation_token=activation_token,
        ))

    cleanup_sidecar_attempted = True
    steps.append(run_disabled_sidecar_cleanup(
        args=args,
        run_id=run_id,
        activation_token=activation_token,
    ))
    return steps, cleanup_online_stage_attempted, cleanup_sidecar_attempted


def should_cleanup_sidecar(steps: list[dict[str, Any]]) -> bool:
    for step in steps:
        if step.get("phase") in SIDECAR_OWNING_PHASES:
            return True
        if "stale Horse sidecar port" in str(step.get("output", "")):
            return True
    return False


def correction_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    results = [
        result for result in report.get("results", [])
        if isinstance(result, dict)
    ]
    pair_hashes: dict[str, Any] = {}
    for result in results:
        root = result.get("root", {})
        convergence = result.get("live_convergence") or {}
        pair_hashes[str(root.get("role", ""))] = (
            convergence.get("local_corrected_hash")
        )
    out: list[dict[str, Any]] = []
    for result in results:
        root = result.get("root", {})
        correction = result.get("live_correction") or {}
        convergence = result.get("live_convergence") or {}
        cache_write = result.get("live_cache_write") or {}
        sidecar = result.get("sidecar_handshake") or {}
        role = str(root.get("role", ""))
        peer_role = "sandbox" if role == "host" else "host"
        out.append(
            {
                "role": role,
                "request_id": result.get("request_id"),
                "trace_file": (
                    correction.get("_trace_file")
                    or convergence.get("_trace_file")
                    or cache_write.get("_trace_file")
                ),
                "sidecar_ok": bool(sidecar.get("ok")),
                "prediction_diverged": bool(
                    cache_write.get("prediction_diverged")
                    or correction.get("predicted_differs_from_baseline")
                ),
                "correction_depth": correction.get("correction_depth"),
                "snapshot_restore": bool(correction.get("snapshot_restore")),
                "hidden_resim": bool(correction.get("hidden_resim")),
                "corrected_hash": correction.get("corrected_hash"),
                "peer_hash": pair_hashes.get(peer_role),
                "converged": bool(convergence.get("converged")),
            }
        )
    return out


def direct_stage_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    out: list[dict[str, Any]] = []
    for result in report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root", {})
        stage = result.get("direct_stage") or {}
        out.append(
            {
                "role": root.get("role", ""),
                "request_id": result.get("request_id"),
                "trace_file": stage.get("_trace_file"),
                "latest_callback_function": stage.get(
                    "latest_callback_function", ""
                ),
                "latest_callback_result": bool(
                    stage.get("latest_callback_result")
                ),
                "ok": bool(stage.get("ok")),
                "battle_context_ready": bool(
                    stage.get("battle_context_ready")
                ),
                "battle_scene_requested": bool(
                    stage.get("battle_scene_requested")
                ),
                "battle_scene_request_ok": bool(
                    stage.get("battle_scene_request_ok")
                ),
                "battle_scene_ready": bool(stage.get("battle_scene_ready")),
                "battle_scene_reason": stage.get("battle_scene_reason", ""),
                "battle_scene_transition_tag": stage.get(
                    "battle_scene_transition_tag", ""
                ),
                "battle_scene_transition_owner": stage.get(
                    "battle_scene_transition_owner", ""
                ),
                "manager_ready_flag": bool(stage.get("manager_ready_flag")),
                "manager_start_latch_flag": bool(
                    stage.get("manager_start_latch_flag")
                ),
                "manager_enable_change_flag": bool(
                    stage.get("manager_enable_change_flag")
                ),
                "battle_manager_spawn_requested": bool(
                    stage.get("battle_manager_spawn_requested")
                ),
                "battle_manager_spawn_ok": bool(
                    stage.get("battle_manager_spawn_ok")
                ),
                "debug_direct_stage_begin_play": bool(
                    stage.get("debug_direct_stage_begin_play")
                ),
                "battle_manager_spawn_class": stage.get(
                    "battle_manager_spawn_class"
                ),
                "battle_manager_begin_play_requested": bool(
                    stage.get("battle_manager_begin_play_requested")
                ),
                "battle_manager_begin_play_ok": bool(
                    stage.get("battle_manager_begin_play_ok")
                ),
                "battle_manager_begin_play_failure": stage.get(
                    "battle_manager_begin_play_failure", ""
                ),
                "active_battle_manager": stage.get("active_battle_manager"),
                "battle_rule_read_ok": bool(stage.get("battle_rule_read_ok")),
                "battle_rule_finite": bool(stage.get("battle_rule_finite")),
                "battle_rule_source": stage.get("battle_rule_source", ""),
                "battle_rule_type": stage.get("battle_rule_type"),
                "battle_rule_time": stage.get("battle_rule_time"),
                "chara_p1": stage.get("chara_p1"),
                "chara_p2": stage.get("chara_p2"),
                "chara_p1_read_ok": bool(stage.get("chara_p1_read_ok")),
                "chara_p2_read_ok": bool(stage.get("chara_p2_read_ok")),
                "chara_p1_object_real": bool(
                    stage.get("chara_p1_object_real")
                ),
                "chara_p2_object_real": bool(
                    stage.get("chara_p2_object_real")
                ),
                "chara_p1_native_live": bool(
                    stage.get("chara_p1_native_live")
                ),
                "chara_p2_native_live": bool(
                    stage.get("chara_p2_native_live")
                ),
                "chara_p1_static_live": bool(
                    stage.get("chara_p1_static_live")
                ),
                "chara_p2_static_live": bool(
                    stage.get("chara_p2_static_live")
                ),
                "chara_p1_context_live": bool(
                    stage.get("chara_p1_context_live")
                    or stage.get("chara_p1_object_real")
                    or stage.get("chara_p1_native_live")
                    or stage.get("chara_p1_static_live")
                ),
                "chara_p2_context_live": bool(
                    stage.get("chara_p2_context_live")
                    or stage.get("chara_p2_object_real")
                    or stage.get("chara_p2_native_live")
                    or stage.get("chara_p2_static_live")
                ),
                "battle_manager_object_real": bool(
                    stage.get("battle_manager_object_real")
                ),
                "battle_context_failure": stage.get(
                    "battle_context_failure", ""
                ),
                "failure": stage.get("failure", ""),
            }
        )
    return out


def direct_connect_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    out: list[dict[str, Any]] = []
    for result in report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root", {})
        direct = result.get("direct_connect") or {}
        out.append(
            {
                "role": root.get("role", ""),
                "request_id": result.get("request_id"),
                "trace_file": direct.get("_trace_file"),
                "ok": bool(direct.get("ok")),
                "validated_direct_input": bool(
                    direct.get("validated_direct_input")
                ),
                "udp_connreset_disabled": bool(
                    direct.get("udp_connreset_disabled")
                ),
                "local_input_hash": direct.get("local_input_hash"),
                "remote_input_hash": direct.get("remote_input_hash"),
                "remote_frame_count": direct.get("remote_frame_count"),
                "direct_packets_sent": direct.get("direct_packets_sent"),
                "direct_packets_received": direct.get(
                    "direct_packets_received"
                ),
                "recvfrom_error": direct.get("recvfrom_error"),
                "sendto_error": direct.get("sendto_error"),
                "udp_connreset_error": direct.get("udp_connreset_error"),
                "failure": direct.get("failure", ""),
            }
        )
    return out


def direct_correction_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    results = [
        result for result in report.get("results", [])
        if isinstance(result, dict)
    ]
    out: list[dict[str, Any]] = []
    for result in results:
        root = result.get("root", {})
        correction = result.get("direct_correction") or {}
        role = str(root.get("role", ""))
        out.append(
            {
                "role": role,
                "request_id": result.get("request_id"),
                "trace_file": correction.get("_trace_file"),
                "ok": bool(correction.get("ok")),
                "direct_input_ready": bool(
                    correction.get("direct_input_ready")
                ),
                "prediction_diverged": bool(
                    correction.get("predicted_differs_from_baseline")
                ),
                "correction_depth": correction.get("correction_depth"),
                "snapshot_restore": bool(correction.get("snapshot_restore")),
                "hidden_resim": bool(correction.get("hidden_resim")),
                "corrected_hash": correction.get("corrected_hash"),
                "baseline_explicit_hash": correction.get(
                    "baseline_explicit_hash"
                ),
                "corrected_explicit_hash": correction.get(
                    "corrected_explicit_hash"
                ),
                "explicit_match": bool(correction.get("explicit_match")),
                "hgcpu_policy_match": bool(
                    correction.get("hgcpu_policy_match")
                ),
                "frame_counter_match": bool(
                    correction.get("frame_counter_match")
                ),
                "frame_counter_delta_ok": bool(
                    correction.get("frame_counter_delta_ok")
                ),
                "all_steps_ok": bool(correction.get("all_steps_ok")),
                "local_input_hash": correction.get("local_input_hash"),
                "remote_input_hash": correction.get("remote_input_hash"),
                "post_baseline_restore_ok": bool(
                    correction.get("post_baseline_restore_ok")
                ),
                "post_baseline_restore_failure": correction.get(
                    "post_baseline_restore_failure", ""
                ),
                "post_baseline_restore_hgcpu_faulted": bool(
                    correction.get("post_baseline_restore_hgcpu_faulted")
                ),
                "post_baseline_restore_hgcpu_exception_code": correction.get(
                    "post_baseline_restore_hgcpu_exception_code"
                ),
                "post_baseline_restore_hgcpu_exception_rip": correction.get(
                    "post_baseline_restore_hgcpu_exception_rip"
                ),
                "failure": correction.get("failure", ""),
            }
        )
    return out


def online_stage_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    out: list[dict[str, Any]] = []
    for result in report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root", {})
        stage = result.get("online_stage") or {}
        out.append(
            {
                "role": root.get("role", ""),
                "request_id": result.get("request_id"),
                "trace_file": stage.get("_trace_file"),
                "ok": bool(stage.get("ok")),
                "debug_steam_probe": bool(stage.get("debug_steam_probe")),
                "debug_steam_filter_probe": bool(
                    stage.get("debug_steam_filter_probe")
                ),
                "online_stage_no_presence_find": bool(
                    stage.get("online_stage_no_presence_find")
                ),
                "session_name": stage.get("session_name", ""),
                "room_name": stage.get("room_name", ""),
                "target_owner_id": stage.get("target_owner_id", 0),
                "current_scene_name": stage.get("current_scene_name", ""),
                "current_scene_class": stage.get("current_scene_class", ""),
                "next_scene_name": stage.get("next_scene_name", ""),
                "next_scene_class": stage.get("next_scene_class", ""),
                "online_nav_attempts": stage.get("online_nav_attempts"),
                "online_nav_reason": stage.get("online_nav_reason", ""),
                "online_nav_transition": stage.get("online_nav_transition", ""),
                "gameflow_input_attempts": stage.get("gameflow_input_attempts"),
                "main_menu_input_attempts": stage.get(
                    "main_menu_input_attempts"
                ),
                "main_menu_input_sequence_step": stage.get(
                    "main_menu_input_sequence_step"
                ),
                "main_menu_input_sequence_complete": bool(
                    stage.get("main_menu_input_sequence_complete")
                ),
                "main_menu_input_last_ok": bool(
                    stage.get("main_menu_input_last_ok")
                ),
                "main_menu_input_last_key": stage.get(
                    "main_menu_input_last_key"
                ),
                "main_menu_input_last_reason": stage.get(
                    "main_menu_input_last_reason", ""
                ),
                "main_menu_nav_last_action": stage.get(
                    "main_menu_nav_last_action", ""
                ),
                "main_menu_nav_last_action_accepted": bool(
                    stage.get("main_menu_nav_last_action_accepted")
                ),
                "main_menu_nav_last_action_transitioned": bool(
                    stage.get("main_menu_nav_last_action_transitioned")
                ),
                "main_menu_nav_last_action_attempt": stage.get(
                    "main_menu_nav_last_action_attempt"
                ),
                "main_menu_nav_cooldown_remaining": stage.get(
                    "main_menu_nav_cooldown_remaining"
                ),
                "main_menu_player_match_route": stage.get(
                    "main_menu_player_match_route", ""
                ),
                "player_match_scene_requested": bool(
                    stage.get("player_match_scene_requested")
                ),
                "player_match_scene_request_attempts": stage.get(
                    "player_match_scene_request_attempts"
                ),
                "player_match_scene_last_request_nav_attempt": stage.get(
                    "player_match_scene_last_request_nav_attempt"
                ),
                "player_match_scene_last_request_ok": bool(
                    stage.get("player_match_scene_last_request_ok")
                ),
                "player_match_scene_ready": bool(
                    stage.get("player_match_scene_ready")
                ),
                "player_match_in_room_requested": bool(
                    stage.get("player_match_in_room_requested")
                ),
                "player_match_in_room_ok": bool(
                    stage.get("player_match_in_room_ok")
                ),
                "player_match_in_room_poll_ok": bool(
                    stage.get("player_match_in_room_poll_ok")
                ),
                "player_match_in_room_poll_count": stage.get(
                    "player_match_in_room_poll_count"
                ),
                "player_match_in_room_state": stage.get(
                    "player_match_in_room_state"
                ),
                "player_match_in_room_state_class": stage.get(
                    "player_match_in_room_state_class", ""
                ),
                "player_match_in_room_enable_ready": bool(
                    stage.get("player_match_in_room_enable_ready")
                ),
                "player_match_in_room_session_connecting": bool(
                    stage.get("player_match_in_room_session_connecting")
                ),
                "player_match_in_room_failure": stage.get(
                    "player_match_in_room_failure", ""
                ),
                "online_stage_wait_host_room_ready_marker": bool(
                    stage.get("online_stage_wait_host_room_ready_marker")
                ),
                "online_stage_host_room_ready_marker": stage.get(
                    "online_stage_host_room_ready_marker", ""
                ),
                "host_room_ready_marker_wait_requested": bool(
                    stage.get("host_room_ready_marker_wait_requested")
                ),
                "host_room_ready_marker_observed": bool(
                    stage.get("host_room_ready_marker_observed")
                ),
                "host_room_ready_marker_failure": stage.get(
                    "host_room_ready_marker_failure", ""
                ),
                "join_scene_pipeline_step": stage.get(
                    "join_scene_pipeline_step"
                ),
                "join_scene_failure": stage.get("join_scene_failure", ""),
                "ping_search_event_trace_count": stage.get(
                    "ping_search_event_trace_count"
                ),
                "session_result_helper_trace_count": stage.get(
                    "session_result_helper_trace_count"
                ),
                "create_session_event_trace_count": stage.get(
                    "create_session_event_trace_count"
                ),
                "join_session_event_trace_count": stage.get(
                    "join_session_event_trace_count"
                ),
                "session_connect_event_trace_count": stage.get(
                    "session_connect_event_trace_count"
                ),
                "session_disconnect_event_trace_count": stage.get(
                    "session_disconnect_event_trace_count"
                ),
                "host_create_request_ok": bool(
                    stage.get("host_create_request_ok")
                ),
                "create_callback_seen": bool(
                    stage.get("create_callback_seen")
                ),
                "create_callback_result": bool(
                    stage.get("create_callback_result")
                ),
                "create_callback_true_count": stage.get(
                    "create_callback_true_count"
                ),
                "create_callback_false_count": stage.get(
                    "create_callback_false_count"
                ),
                "destroy_start_requested": bool(
                    stage.get("destroy_start_requested")
                ),
                "destroy_start_call_ok": bool(
                    stage.get("destroy_start_call_ok")
                ),
                "destroy_start_wait_complete": bool(
                    stage.get("destroy_start_wait_complete")
                ),
                "destroy_callback_seen": bool(
                    stage.get("destroy_callback_seen")
                ),
                "destroy_callback_result": bool(
                    stage.get("destroy_callback_result")
                ),
                "destroy_callback_true_count": stage.get(
                    "destroy_callback_true_count"
                ),
                "destroy_callback_false_count": stage.get(
                    "destroy_callback_false_count"
                ),
                "destroy_start_failure": stage.get(
                    "destroy_start_failure", ""
                ),
                "client_find_request_ok": bool(
                    stage.get("client_find_request_ok")
                ),
                "client_find_attempts": stage.get(
                    "client_find_attempts"
                ),
                "client_find_empty_callbacks": stage.get(
                    "client_find_empty_callbacks"
                ),
                "client_find_retry_reason": stage.get(
                    "client_find_retry_reason", ""
                ),
                "find_callback_seen": bool(
                    stage.get("find_callback_seen")
                ),
                "find_callback_result": bool(
                    stage.get("find_callback_result")
                ),
                "find_result_count": stage.get("find_result_count"),
                "find_result_element_size": stage.get(
                    "find_result_element_size"
                ),
                "find_result_target_found": bool(
                    stage.get("find_result_target_found")
                ),
                "native_no_presence_find_requested": bool(
                    stage.get("native_no_presence_find_requested")
                ),
                "native_no_presence_find_enabled": bool(
                    stage.get("native_no_presence_find_enabled")
                ),
                "native_no_presence_find_call_ok": bool(
                    stage.get("native_no_presence_find_call_ok")
                ),
                "native_no_presence_find_polled": bool(
                    stage.get("native_no_presence_find_polled")
                ),
                "native_no_presence_find_presence_disabled": bool(
                    stage.get("native_no_presence_find_presence_disabled")
                ),
                "native_no_presence_find_join_requested": bool(
                    stage.get("native_no_presence_find_join_requested")
                ),
                "native_no_presence_find_join_ok": bool(
                    stage.get("native_no_presence_find_join_ok")
                ),
                "native_no_presence_find_result_count": stage.get(
                    "native_no_presence_find_result_count"
                ),
                "native_no_presence_find_target_found": bool(
                    stage.get("native_no_presence_find_target_found")
                ),
                "native_no_presence_find_failure": stage.get(
                    "native_no_presence_find_failure", ""
                ),
                "client_join_request_ok": bool(
                    stage.get("client_join_request_ok")
                ),
                "join_scene_selected_result_bytes": stage.get(
                    "join_scene_selected_result_bytes"
                ),
                "join_complete_seen": bool(
                    stage.get("join_complete_seen")
                ),
                "join_complete_result": bool(
                    stage.get("join_complete_result")
                ),
                "join_complete_result_type": stage.get(
                    "join_complete_result_type"
                ),
                "join_callback_true_count": stage.get(
                    "join_callback_true_count"
                ),
                "join_callback_false_count": stage.get(
                    "join_callback_false_count"
                ),
                "session_connect_complete_seen": bool(
                    stage.get("session_connect_complete_seen")
                ),
                "session_connect_complete_result": bool(
                    stage.get("session_connect_complete_result")
                ),
                "session_connect_complete_full_member_error": bool(
                    stage.get(
                        "session_connect_complete_full_member_error"
                    )
                ),
                "session_connect_callback_true_count": stage.get(
                    "session_connect_callback_true_count"
                ),
                "session_connect_callback_false_count": stage.get(
                    "session_connect_callback_false_count"
                ),
                "session_member_join_seen": bool(
                    stage.get("session_member_join_seen")
                ),
                "session_member_join_count": stage.get(
                    "session_member_join_count"
                ),
                "session_member_join_attempted_count": stage.get(
                    "session_member_join_attempted_count"
                ),
                "session_member_join_first_tick": stage.get(
                    "session_member_join_first_tick"
                ),
                "native_named_session_sampled": bool(
                    stage.get("native_named_session_sampled")
                ),
                "session_hub_initialize_requested": bool(
                    stage.get("session_hub_initialize_requested")
                ),
                "session_hub_initialize_ok": bool(
                    stage.get("session_hub_initialize_ok")
                ),
                "process_event_followup_count": stage.get(
                    "process_event_followup_count"
                ),
                "process_event_followup_last_tick": stage.get(
                    "process_event_followup_last_tick"
                ),
                "process_event_followup_last_kind": stage.get(
                    "process_event_followup_last_kind", ""
                ),
                "native_named_session_ok": bool(
                    stage.get("native_named_session_ok")
                ),
                "native_named_session_failure": stage.get(
                    "native_named_session_failure", ""
                ),
                "native_named_session_ptr": stage.get(
                    "native_named_session_ptr"
                ),
                "native_named_session_info": stage.get(
                    "native_named_session_info"
                ),
                "native_named_session_info_ref_controller": stage.get(
                    "native_named_session_info_ref_controller"
                ),
                "native_named_session_lobby_id": stage.get(
                    "native_named_session_lobby_id"
                ),
                "native_named_session_public_connections": stage.get(
                    "native_named_session_public_connections"
                ),
                "native_named_session_luxor_connections": stage.get(
                    "native_named_session_luxor_connections"
                ),
                "native_named_session_hosting_player_num": stage.get(
                    "native_named_session_hosting_player_num"
                ),
                "native_named_session_first_sample_tick": stage.get(
                    "native_named_session_first_sample_tick"
                ),
                "native_named_session_first_state": stage.get(
                    "native_named_session_first_state"
                ),
                "native_named_session_first_state_byte": stage.get(
                    "native_named_session_first_state_byte"
                ),
                "native_named_session_state7_first_tick": stage.get(
                    "native_named_session_state7_first_tick"
                ),
                "native_named_session_state_byte_ready_first_tick": stage.get(
                    "native_named_session_state_byte_ready_first_tick"
                ),
                "native_named_session_state_transition_count": stage.get(
                    "native_named_session_state_transition_count"
                ),
                "native_named_session_state": stage.get(
                    "native_named_session_state"
                ),
                "native_named_session_state_byte": stage.get(
                    "native_named_session_state_byte"
                ),
                "native_named_session_state_byte_ready": bool(
                    stage.get("native_named_session_state_byte_ready")
                ),
                "deferred_session_connect_attempts": stage.get(
                    "deferred_session_connect_attempts"
                ),
                "deferred_session_connect_last_tick": stage.get(
                    "deferred_session_connect_last_tick"
                ),
                "deferred_session_connect_call_ok": bool(
                    stage.get("deferred_session_connect_call_ok")
                ),
                "deferred_session_connect_failure": stage.get(
                    "deferred_session_connect_failure", ""
                ),
                "connect_manager_sampled": bool(
                    stage.get("connect_manager_sampled")
                ),
                "connect_manager_ok": bool(stage.get("connect_manager_ok")),
                "connect_manager_failure": stage.get(
                    "connect_manager_failure", ""
                ),
                "luxor_network_check_compat_enabled": bool(
                    stage.get("luxor_network_check_compat_enabled")
                ),
                "luxor_network_check_compat_hook_attempted": bool(
                    stage.get("luxor_network_check_compat_hook_attempted")
                ),
                "luxor_network_check_compat_hook_installed": bool(
                    stage.get("luxor_network_check_compat_hook_installed")
                ),
                "luxor_network_check_compat_calls": stage.get(
                    "luxor_network_check_compat_calls"
                ),
                "luxor_network_check_compat_original_true": stage.get(
                    "luxor_network_check_compat_original_true"
                ),
                "luxor_network_check_compat_original_false": stage.get(
                    "luxor_network_check_compat_original_false"
                ),
                "luxor_network_check_compat_forced_true": stage.get(
                    "luxor_network_check_compat_forced_true"
                ),
                "luxor_network_check_compat_last_original": stage.get(
                    "luxor_network_check_compat_last_original"
                ),
                "luxor_network_check_compat_last_returned": stage.get(
                    "luxor_network_check_compat_last_returned"
                ),
                "connection_state_update_task_hook_attempted": bool(
                    stage.get("connection_state_update_task_hook_attempted")
                ),
                "connection_state_update_task_hook_installed": bool(
                    stage.get("connection_state_update_task_hook_installed")
                ),
                "connection_state_update_task_calls": stage.get(
                    "connection_state_update_task_calls"
                ),
                "connection_state_update_task_transitions_to_state5": (
                    stage.get(
                        "connection_state_update_task_transitions_to_state5"
                    )
                ),
                "connection_state_update_task_last_caller_rva": stage.get(
                    "connection_state_update_task_last_caller_rva"
                ),
                "connection_state_update_task_last_active": stage.get(
                    "connection_state_update_task_last_active"
                ),
                "connection_state_update_task_last_network_calls_before": (
                    stage.get(
                        "connection_state_update_task_last_network_calls_before"
                    )
                ),
                "connection_state_update_task_last_network_calls_after": (
                    stage.get(
                        "connection_state_update_task_last_network_calls_after"
                    )
                ),
                "connection_state_update_task_last_delta_millis": stage.get(
                    "connection_state_update_task_last_delta_millis"
                ),
                "connection_state_update_task_last_state_before": stage.get(
                    "connection_state_update_task_last_state_before"
                ),
                "connection_state_update_task_last_state_after": stage.get(
                    "connection_state_update_task_last_state_after"
                ),
                "connection_state_update_task_last_sub_state_before": (
                    stage.get(
                        "connection_state_update_task_last_sub_state_before"
                    )
                ),
                "connection_state_update_task_last_sub_state_after": (
                    stage.get(
                        "connection_state_update_task_last_sub_state_after"
                    )
                ),
                "connection_state_update_task_last_ready_before": stage.get(
                    "connection_state_update_task_last_ready_before"
                ),
                "connection_state_update_task_last_ready_after": stage.get(
                    "connection_state_update_task_last_ready_after"
                ),
                "active_failed_substate9_hook_attempted": bool(
                    stage.get("active_failed_substate9_hook_attempted")
                ),
                "active_failed_substate9_hook_installed": bool(
                    stage.get("active_failed_substate9_hook_installed")
                ),
                "active_failed_substate9_calls": stage.get(
                    "active_failed_substate9_calls"
                ),
                "active_failed_substate9_last_caller_rva": stage.get(
                    "active_failed_substate9_last_caller_rva"
                ),
                "active_failed_substate9_last_pointer": stage.get(
                    "active_failed_substate9_last_pointer"
                ),
                "active_failed_substate9_last_active_before": stage.get(
                    "active_failed_substate9_last_active_before"
                ),
                "active_failed_substate9_last_active_after": stage.get(
                    "active_failed_substate9_last_active_after"
                ),
                "active_failed_substate9_last_result": stage.get(
                    "active_failed_substate9_last_result"
                ),
                "active_failed_substate9_last_local_user_before": stage.get(
                    "active_failed_substate9_last_local_user_before"
                ),
                "active_failed_substate9_last_local_user_after": stage.get(
                    "active_failed_substate9_last_local_user_after"
                ),
                "active_failed_substate9_last_state_before": stage.get(
                    "active_failed_substate9_last_state_before"
                ),
                "active_failed_substate9_last_state_after": stage.get(
                    "active_failed_substate9_last_state_after"
                ),
                "active_failed_substate9_last_sub_state_before": stage.get(
                    "active_failed_substate9_last_sub_state_before"
                ),
                "active_failed_substate9_last_sub_state_after": stage.get(
                    "active_failed_substate9_last_sub_state_after"
                ),
                "active_failed_substate9_last_ready_before": stage.get(
                    "active_failed_substate9_last_ready_before"
                ),
                "active_failed_substate9_last_ready_after": stage.get(
                    "active_failed_substate9_last_ready_after"
                ),
                "active_state5_wide_hook_attempt_mask": stage.get(
                    "active_state5_wide_hook_attempt_mask"
                ),
                "active_state5_wide_hook_install_mask": stage.get(
                    "active_state5_wide_hook_install_mask"
                ),
                "active_state5_wide_hook_all_bits": stage.get(
                    "active_state5_wide_hook_all_bits"
                ),
                "active_state5_wide_calls": stage.get(
                    "active_state5_wide_calls"
                ),
                "active_state5_wide_transitions_to_state5": stage.get(
                    "active_state5_wide_transitions_to_state5"
                ),
                "active_state5_wide_last_hook_bit": stage.get(
                    "active_state5_wide_last_hook_bit"
                ),
                "active_state5_wide_last_function_rva": stage.get(
                    "active_state5_wide_last_function_rva"
                ),
                "active_state5_wide_last_caller_rva": stage.get(
                    "active_state5_wide_last_caller_rva"
                ),
                "active_state5_wide_last_active": stage.get(
                    "active_state5_wide_last_active"
                ),
                "active_state5_wide_last_context": stage.get(
                    "active_state5_wide_last_context"
                ),
                "active_state5_wide_last_sender_code": stage.get(
                    "active_state5_wide_last_sender_code"
                ),
                "active_state5_wide_last_result": stage.get(
                    "active_state5_wide_last_result"
                ),
                "active_state5_wide_last_delta_millis": stage.get(
                    "active_state5_wide_last_delta_millis"
                ),
                "active_state5_wide_last_local_user_before": stage.get(
                    "active_state5_wide_last_local_user_before"
                ),
                "active_state5_wide_last_local_user_after": stage.get(
                    "active_state5_wide_last_local_user_after"
                ),
                "active_state5_wide_last_state_before": stage.get(
                    "active_state5_wide_last_state_before"
                ),
                "active_state5_wide_last_state_after": stage.get(
                    "active_state5_wide_last_state_after"
                ),
                "active_state5_wide_last_sub_state_before": stage.get(
                    "active_state5_wide_last_sub_state_before"
                ),
                "active_state5_wide_last_sub_state_after": stage.get(
                    "active_state5_wide_last_sub_state_after"
                ),
                "active_state5_wide_last_ready_before": stage.get(
                    "active_state5_wide_last_ready_before"
                ),
                "active_state5_wide_last_ready_after": stage.get(
                    "active_state5_wide_last_ready_after"
                ),
                "active_request_queue_hook_attempted": stage.get(
                    "active_request_queue_hook_attempted"
                ),
                "active_request_queue_hook_installed": stage.get(
                    "active_request_queue_hook_installed"
                ),
                "active_request_queue_calls": stage.get(
                    "active_request_queue_calls"
                ),
                "active_request_queue_last_caller_rva": stage.get(
                    "active_request_queue_last_caller_rva"
                ),
                "active_request_queue_last_context": stage.get(
                    "active_request_queue_last_context"
                ),
                "active_request_queue_last_event_opcode": stage.get(
                    "active_request_queue_last_event_opcode"
                ),
                "active_request_queue_last_timeout_millis": stage.get(
                    "active_request_queue_last_timeout_millis"
                ),
                "luxor_connect_manager": stage.get("luxor_connect_manager"),
                "luxor_connect_delegate_handle_array": stage.get(
                    "luxor_connect_delegate_handle_array"
                ),
                "luxor_connect_delegate_handle_array_ref": stage.get(
                    "luxor_connect_delegate_handle_array_ref"
                ),
                "luxor_delegate_create_session_complete_handle": stage.get(
                    "luxor_delegate_create_session_complete_handle"
                ),
                "luxor_delegate_slot_08_handle": stage.get(
                    "luxor_delegate_slot_08_handle"
                ),
                "luxor_delegate_start_session_complete_handle": stage.get(
                    "luxor_delegate_start_session_complete_handle"
                ),
                "luxor_delegate_destroy_session_complete_handle": stage.get(
                    "luxor_delegate_destroy_session_complete_handle"
                ),
                "luxor_delegate_slot_20_handle": stage.get(
                    "luxor_delegate_slot_20_handle"
                ),
                "luxor_delegate_join_session_complete_handle": stage.get(
                    "luxor_delegate_join_session_complete_handle"
                ),
                "luxor_delegate_deferred_session_connection_handle": (
                    stage.get(
                        "luxor_delegate_deferred_session_connection_handle"
                    )
                ),
                "luxor_delegate_external_ui_handle": stage.get(
                    "luxor_delegate_external_ui_handle"
                ),
                "luxor_delegate_sender_handle": stage.get(
                    "luxor_delegate_sender_handle"
                ),
                "luxor_delegate_slot_48_handle": stage.get(
                    "luxor_delegate_slot_48_handle"
                ),
                "luxor_connect_binding_handle_a": stage.get(
                    "luxor_connect_binding_handle_a"
                ),
                "luxor_connect_binding_handle_b": stage.get(
                    "luxor_connect_binding_handle_b"
                ),
                "luxor_connect_sender_message_binding": stage.get(
                    "luxor_connect_sender_message_binding"
                ),
                "luxor_connect_message_binding": stage.get(
                    "luxor_connect_message_binding"
                ),
                "luxor_connect_online_session_object": stage.get(
                    "luxor_connect_online_session_object"
                ),
                "luxor_connect_online_session_ref": stage.get(
                    "luxor_connect_online_session_ref"
                ),
                "luxor_active_connect_object": stage.get(
                    "luxor_active_connect_object"
                ),
                "luxor_active_connect_ref": stage.get(
                    "luxor_active_connect_ref"
                ),
                "luxor_active_connect_system_slot": stage.get(
                    "luxor_active_connect_system_slot"
                ),
                "luxor_active_connect_system_ref": stage.get(
                    "luxor_active_connect_system_ref"
                ),
                "luxor_active_connect_system_offset": stage.get(
                    "luxor_active_connect_system_offset"
                ),
                "luxor_active_connect_system_known_interface": bool(
                    stage.get("luxor_active_connect_system_known_interface")
                ),
                "luxor_active_connect_state": stage.get(
                    "luxor_active_connect_state"
                ),
                "luxor_active_connect_local_user_byte": stage.get(
                    "luxor_active_connect_local_user_byte"
                ),
                "luxor_active_connect_sub_state": stage.get(
                    "luxor_active_connect_sub_state"
                ),
                "luxor_active_state_flags": stage.get(
                    "luxor_active_state_flags"
                ),
                "luxor_active_session_name_raw": stage.get(
                    "luxor_active_session_name_raw"
                ),
                "luxor_active_session_state_update_task": stage.get(
                    "luxor_active_session_state_update_task"
                ),
                "luxor_active_session_state_update_task_ref": stage.get(
                    "luxor_active_session_state_update_task_ref"
                ),
                "luxor_active_session_notify_task": stage.get(
                    "luxor_active_session_notify_task"
                ),
                "luxor_active_session_notify_task_ref": stage.get(
                    "luxor_active_session_notify_task_ref"
                ),
                "luxor_active_session_event_handle": stage.get(
                    "luxor_active_session_event_handle"
                ),
                "luxor_session_connection_object": stage.get(
                    "luxor_session_connection_object"
                ),
                "luxor_session_connection_ref": stage.get(
                    "luxor_session_connection_ref"
                ),
                "luxor_session_async_queue_head": stage.get(
                    "luxor_session_async_queue_head"
                ),
                "luxor_session_async_queue_next": stage.get(
                    "luxor_session_async_queue_next"
                ),
                "luxor_session_async_queue_prev": stage.get(
                    "luxor_session_async_queue_prev"
                ),
                "luxor_session_async_queue_count": stage.get(
                    "luxor_session_async_queue_count"
                ),
                "luxor_session_async_queue_first_callback": stage.get(
                    "luxor_session_async_queue_first_callback"
                ),
                "luxor_session_async_queue_first_callback_rva": stage.get(
                    "luxor_session_async_queue_first_callback_rva"
                ),
                "luxor_session_async_queue_first_payload_count": stage.get(
                    "luxor_session_async_queue_first_payload_count"
                ),
                "luxor_session_async_queue_tail_callback": stage.get(
                    "luxor_session_async_queue_tail_callback"
                ),
                "luxor_session_async_queue_tail_callback_rva": stage.get(
                    "luxor_session_async_queue_tail_callback_rva"
                ),
                "luxor_session_async_queue_tail_payload_count": stage.get(
                    "luxor_session_async_queue_tail_payload_count"
                ),
                "luxor_connect_main_user_sentinel": stage.get(
                    "luxor_connect_main_user_sentinel"
                ),
                "luxor_connect_scratch_object": stage.get(
                    "luxor_connect_scratch_object"
                ),
                "luxor_connect_scratch_ref": stage.get(
                    "luxor_connect_scratch_ref"
                ),
                "luxor_active_transport": stage.get(
                    "luxor_active_transport"
                ),
                "luxor_active_transport_tick": stage.get(
                    "luxor_active_transport_tick"
                ),
                "luxor_active_transport_status_code": stage.get(
                    "luxor_active_transport_status_code"
                ),
                "luxor_active_transport_ready_state": stage.get(
                    "luxor_active_transport_ready_state"
                ),
                "luxor_active_transport_is_host": stage.get(
                    "luxor_active_transport_is_host"
                ),
                "luxor_active_transport_channel_count": stage.get(
                    "luxor_active_transport_channel_count"
                ),
                "luxor_active_transport_channel_capacity": stage.get(
                    "luxor_active_transport_channel_capacity"
                ),
                "luxor_active_transport_ready_sampled": bool(
                    stage.get("luxor_active_transport_ready_sampled")
                ),
                "luxor_active_transport_ready": bool(
                    stage.get("luxor_active_transport_ready")
                ),
                "online_stage_join_complete_compat": bool(
                    stage.get("online_stage_join_complete_compat")
                ),
                "online_stage_in_room_transition_compat": bool(
                    stage.get("online_stage_in_room_transition_compat")
                ),
                "online_stage_direct_native_join_diagnostic": bool(
                    stage.get("online_stage_direct_native_join_diagnostic")
                ),
                "join_complete_compat_attempted": bool(
                    stage.get("join_complete_compat_attempted")
                ),
                "join_complete_compat_method": stage.get(
                    "join_complete_compat_method", ""
                ),
                "join_complete_compat_trigger_reason": stage.get(
                    "join_complete_compat_trigger_reason", ""
                ),
                "join_complete_compat_failure": stage.get(
                    "join_complete_compat_failure", ""
                ),
                "join_complete_compat_call_ok": bool(
                    stage.get("join_complete_compat_call_ok")
                ),
                "join_complete_compat_count": stage.get(
                    "join_complete_compat_count"
                ),
                "join_complete_compat_last_tick": stage.get(
                    "join_complete_compat_last_tick"
                ),
                "join_complete_compat_active_state_before": stage.get(
                    "join_complete_compat_active_state_before"
                ),
                "join_complete_compat_active_state_after": stage.get(
                    "join_complete_compat_active_state_after"
                ),
                "join_complete_compat_session_connection_before": stage.get(
                    "join_complete_compat_session_connection_before"
                ),
                "join_complete_compat_session_connection_after": stage.get(
                    "join_complete_compat_session_connection_after"
                ),
                "online_stage_transport_ready_compat": bool(
                    stage.get("online_stage_transport_ready_compat")
                ),
                "online_stage_ready_open_compat": bool(
                    stage.get("online_stage_ready_open_compat")
                ),
                "online_stage_peer_route_tag_fix": bool(
                    stage.get("online_stage_peer_route_tag_fix")
                ),
                "transport_ready_compat_attempted": bool(
                    stage.get("transport_ready_compat_attempted")
                ),
                "transport_ready_compat_method": stage.get(
                    "transport_ready_compat_method", ""
                ),
                "transport_ready_compat_trigger_reason": stage.get(
                    "transport_ready_compat_trigger_reason", ""
                ),
                "transport_ready_compat_failure": stage.get(
                    "transport_ready_compat_failure", ""
                ),
                "transport_ready_compat_call_ok": bool(
                    stage.get("transport_ready_compat_call_ok")
                ),
                "transport_ready_compat_count": stage.get(
                    "transport_ready_compat_count"
                ),
                "transport_ready_compat_last_tick": stage.get(
                    "transport_ready_compat_last_tick"
                ),
                "transport_ready_compat_before_ready_state": stage.get(
                    "transport_ready_compat_before_ready_state"
                ),
                "transport_ready_compat_after_ready_state": stage.get(
                    "transport_ready_compat_after_ready_state"
                ),
                "transport_ready_compat_before_ready_query": bool(
                    stage.get("transport_ready_compat_before_ready_query")
                ),
                "transport_ready_compat_after_ready_query": bool(
                    stage.get("transport_ready_compat_after_ready_query")
                ),
                "transport_ready_compat_active_state_before": stage.get(
                    "transport_ready_compat_active_state_before"
                ),
                "transport_ready_compat_transport_status_before": stage.get(
                    "transport_ready_compat_transport_status_before"
                ),
                "transport_ready_compat_transport_is_host_before": stage.get(
                    "transport_ready_compat_transport_is_host_before"
                ),
                "transport_ready_compat_session_connection_before": stage.get(
                    "transport_ready_compat_session_connection_before"
                ),
                "transport_ready_compat_session_connection_after": stage.get(
                    "transport_ready_compat_session_connection_after"
                ),
                "transport_ready_compat_deferred_called_after_force": bool(
                    stage.get(
                        "transport_ready_compat_deferred_called_after_force"
                    )
                ),
                "transport_ready_compat_session_connection_after_deferred": (
                    stage.get(
                        "transport_ready_compat_session_connection_after_deferred"
                    )
                ),
                "ready_channel_open_hook_attempted": bool(
                    stage.get("ready_channel_open_hook_attempted")
                ),
                "ready_channel_open_hook_installed": bool(
                    stage.get("ready_channel_open_hook_installed")
                ),
                "ready_channel_open_calls": stage.get(
                    "ready_channel_open_calls"
                ),
                "ready_channel_open_last_caller_rva": stage.get(
                    "ready_channel_open_last_caller_rva"
                ),
                "ready_channel_open_last_session_connection": stage.get(
                    "ready_channel_open_last_session_connection"
                ),
                "ready_channel_open_last_transport": stage.get(
                    "ready_channel_open_last_transport"
                ),
                "ready_channel_open_last_can_send_before": stage.get(
                    "ready_channel_open_last_can_send_before"
                ),
                "ready_channel_open_last_can_send_after": stage.get(
                    "ready_channel_open_last_can_send_after"
                ),
                "transport_ready_mark_hook_attempted": bool(
                    stage.get("transport_ready_mark_hook_attempted")
                ),
                "transport_ready_mark_hook_installed": bool(
                    stage.get("transport_ready_mark_hook_installed")
                ),
                "transport_ready_mark_calls": stage.get(
                    "transport_ready_mark_calls"
                ),
                "transport_ready_mark_last_caller_rva": stage.get(
                    "transport_ready_mark_last_caller_rva"
                ),
                "transport_ready_mark_last_transport": stage.get(
                    "transport_ready_mark_last_transport"
                ),
                "transport_ready_mark_last_ready_before": stage.get(
                    "transport_ready_mark_last_ready_before"
                ),
                "transport_ready_mark_last_ready_after": stage.get(
                    "transport_ready_mark_last_ready_after"
                ),
                "ready_registry_step80_hook_attempted": bool(
                    stage.get("ready_registry_step80_hook_attempted")
                ),
                "ready_registry_step80_hook_installed": bool(
                    stage.get("ready_registry_step80_hook_installed")
                ),
                "ready_registry_step80_calls": stage.get(
                    "ready_registry_step80_calls"
                ),
                "ready_registry_stepd0_hook_attempted": bool(
                    stage.get("ready_registry_stepd0_hook_attempted")
                ),
                "ready_registry_stepd0_hook_installed": bool(
                    stage.get("ready_registry_stepd0_hook_installed")
                ),
                "ready_registry_stepd0_calls": stage.get(
                    "ready_registry_stepd0_calls"
                ),
                "ready_registry_stepd8_hook_attempted": bool(
                    stage.get("ready_registry_stepd8_hook_attempted")
                ),
                "ready_registry_stepd8_hook_installed": bool(
                    stage.get("ready_registry_stepd8_hook_installed")
                ),
                "ready_registry_stepd8_calls": stage.get(
                    "ready_registry_stepd8_calls"
                ),
                "queued_opcode_send_hook_attempted": bool(
                    stage.get("queued_opcode_send_hook_attempted")
                ),
                "queued_opcode_send_hook_installed": bool(
                    stage.get("queued_opcode_send_hook_installed")
                ),
                "queued_opcode_send_calls": stage.get(
                    "queued_opcode_send_calls"
                ),
                "queued_opcode_send_last_caller_rva": stage.get(
                    "queued_opcode_send_last_caller_rva"
                ),
                "queued_opcode_send_last_session_connection": stage.get(
                    "queued_opcode_send_last_session_connection"
                ),
                "queued_opcode_send_last_source_packet": stage.get(
                    "queued_opcode_send_last_source_packet"
                ),
                "queued_opcode_send_last_source_route_key_vtable": stage.get(
                    "queued_opcode_send_last_source_route_key_vtable"
                ),
                "queued_opcode_send_last_source_routing_tag": stage.get(
                    "queued_opcode_send_last_source_routing_tag"
                ),
                "queued_opcode_send_last_outer_opcode": stage.get(
                    "queued_opcode_send_last_outer_opcode"
                ),
                "queued_opcode_send_last_inner_opcode": stage.get(
                    "queued_opcode_send_last_inner_opcode"
                ),
                "queued_opcode_send_last_opcode": stage.get(
                    "queued_opcode_send_last_opcode"
                ),
                "queued_opcode_send_last_channel_id": stage.get(
                    "queued_opcode_send_last_channel_id"
                ),
                "queued_opcode_send_last_result": stage.get(
                    "queued_opcode_send_last_result"
                ),
                "queued_opcode_send_last_queue_before": stage.get(
                    "queued_opcode_send_last_queue_before"
                ),
                "queued_opcode_send_last_queue_after": stage.get(
                    "queued_opcode_send_last_queue_after"
                ),
                "packet_routing_tag_copy_last_timeline_opcode": stage.get(
                    "packet_routing_tag_copy_last_timeline_opcode"
                ),
                "packet_routing_tag_copy_last_source_tag": stage.get(
                    "packet_routing_tag_copy_last_source_tag"
                ),
                "packet_routing_tag_copy_last_dest_tag_after": stage.get(
                    "packet_routing_tag_copy_last_dest_tag_after"
                ),
                "peer_route_tag_fix_enabled": bool(
                    stage.get("peer_route_tag_fix_enabled")
                ),
                "peer_route_tag_fix_last_peer_tag": stage.get(
                    "peer_route_tag_fix_last_peer_tag"
                ),
                "peer_route_tag_fix_last_peer_registry_index": stage.get(
                    "peer_route_tag_fix_last_peer_registry_index"
                ),
                "peer_route_tag_fix_last_peer_source": stage.get(
                    "peer_route_tag_fix_last_peer_source"
                ),
                "peer_route_tag_fix_attempts": stage.get(
                    "peer_route_tag_fix_attempts"
                ),
                "peer_route_tag_fix_applied": stage.get(
                    "peer_route_tag_fix_applied"
                ),
                "peer_route_tag_fix_last_original_tag": stage.get(
                    "peer_route_tag_fix_last_original_tag"
                ),
                "peer_route_tag_fix_last_replacement_tag": stage.get(
                    "peer_route_tag_fix_last_replacement_tag"
                ),
                "peer_route_tag_fix_last_result": stage.get(
                    "peer_route_tag_fix_last_result"
                ),
                "peer_route_tag_fix_last_write_verified": stage.get(
                    "peer_route_tag_fix_last_write_verified"
                ),
                "peer_route_tag_fix_last_verified_tag": stage.get(
                    "peer_route_tag_fix_last_verified_tag"
                ),
                "queued_work_item_clone_hook_attempted": bool(
                    stage.get("queued_work_item_clone_hook_attempted")
                ),
                "queued_work_item_clone_hook_installed": bool(
                    stage.get("queued_work_item_clone_hook_installed")
                ),
                "queued_work_item_clone_calls": stage.get(
                    "queued_work_item_clone_calls"
                ),
                "queued_work_item_clone_last_caller_rva": stage.get(
                    "queued_work_item_clone_last_caller_rva"
                ),
                "queued_work_item_clone_last_source_work_item": stage.get(
                    "queued_work_item_clone_last_source_work_item"
                ),
                "queued_work_item_clone_last_cloned_work_item": stage.get(
                    "queued_work_item_clone_last_cloned_work_item"
                ),
                "queued_work_item_clone_last_source_session_connection": stage.get(
                    "queued_work_item_clone_last_source_session_connection"
                ),
                "queued_work_item_clone_last_source_route_key_vtable": stage.get(
                    "queued_work_item_clone_last_source_route_key_vtable"
                ),
                "queued_work_item_clone_last_source_routing_tag": stage.get(
                    "queued_work_item_clone_last_source_routing_tag"
                ),
                "queued_work_item_clone_last_source_inner_opcode": stage.get(
                    "queued_work_item_clone_last_source_inner_opcode"
                ),
                "queued_work_item_clone_last_source_channel_id": stage.get(
                    "queued_work_item_clone_last_source_channel_id"
                ),
                "queued_work_item_clone_last_cloned_session_connection": stage.get(
                    "queued_work_item_clone_last_cloned_session_connection"
                ),
                "queued_work_item_clone_last_cloned_route_key_vtable": stage.get(
                    "queued_work_item_clone_last_cloned_route_key_vtable"
                ),
                "queued_work_item_clone_last_cloned_routing_tag": stage.get(
                    "queued_work_item_clone_last_cloned_routing_tag"
                ),
                "queued_work_item_clone_last_cloned_inner_opcode": stage.get(
                    "queued_work_item_clone_last_cloned_inner_opcode"
                ),
                "queued_work_item_clone_last_cloned_channel_id": stage.get(
                    "queued_work_item_clone_last_cloned_channel_id"
                ),
                "active_opcode6_send_hook_attempted": bool(
                    stage.get("active_opcode6_send_hook_attempted")
                ),
                "active_opcode6_send_hook_installed": bool(
                    stage.get("active_opcode6_send_hook_installed")
                ),
                "active_opcode6_send_calls": stage.get(
                    "active_opcode6_send_calls"
                ),
                "active_opcode6_send_last_caller_rva": stage.get(
                    "active_opcode6_send_last_caller_rva"
                ),
                "active_opcode6_send_last_active": stage.get(
                    "active_opcode6_send_last_active"
                ),
                "active_opcode6_send_last_sender": stage.get(
                    "active_opcode6_send_last_sender"
                ),
                "active_opcode6_send_last_sender_vtable": stage.get(
                    "active_opcode6_send_last_sender_vtable"
                ),
                "active_opcode6_send_last_state_payload": stage.get(
                    "active_opcode6_send_last_state_payload"
                ),
                "active_opcode6_send_last_result": stage.get(
                    "active_opcode6_send_last_result"
                ),
                "active_opcode6_send_last_active_state": stage.get(
                    "active_opcode6_send_last_active_state"
                ),
                "active_opcode6_send_last_active_sub_state": stage.get(
                    "active_opcode6_send_last_active_sub_state"
                ),
                "active_opcode6_send_last_transport_tick": stage.get(
                    "active_opcode6_send_last_transport_tick"
                ),
                "active_opcode6_send_last_transport_status": stage.get(
                    "active_opcode6_send_last_transport_status"
                ),
                "active_opcode6_send_last_transport_ready": stage.get(
                    "active_opcode6_send_last_transport_ready"
                ),
                "active_opcode6_send_last_transport_is_host": stage.get(
                    "active_opcode6_send_last_transport_is_host"
                ),
                "active_opcode6_send_last_transport_channel_count": stage.get(
                    "active_opcode6_send_last_transport_channel_count"
                ),
                "active_opcode6_send_last_transport_channel_capacity": (
                    stage.get(
                        "active_opcode6_send_last_transport_channel_capacity"
                    )
                ),
                "active_sender_endpoint_get_hook_attempted": bool(
                    stage.get("active_sender_endpoint_get_hook_attempted")
                ),
                "active_sender_endpoint_get_hook_installed": bool(
                    stage.get("active_sender_endpoint_get_hook_installed")
                ),
                "active_sender_endpoint_get_calls": stage.get(
                    "active_sender_endpoint_get_calls"
                ),
                "active_sender_endpoint_get_last_caller_rva": stage.get(
                    "active_sender_endpoint_get_last_caller_rva"
                ),
                "active_sender_endpoint_get_last_sender_interface": stage.get(
                    "active_sender_endpoint_get_last_sender_interface"
                ),
                "active_sender_endpoint_get_last_sender_interface_vtable": (
                    stage.get(
                        "active_sender_endpoint_get_last_sender_interface_vtable"
                    )
                ),
                "active_sender_endpoint_get_last_local_user": stage.get(
                    "active_sender_endpoint_get_last_local_user"
                ),
                "active_sender_endpoint_get_last_endpoint": stage.get(
                    "active_sender_endpoint_get_last_endpoint"
                ),
                "active_sender_endpoint_get_last_endpoint_vtable": stage.get(
                    "active_sender_endpoint_get_last_endpoint_vtable"
                ),
                "active_sender_endpoint_get_last_endpoint_local_user_slot": (
                    stage.get(
                        "active_sender_endpoint_get_last_endpoint_local_user_slot"
                    )
                ),
                "active_sender_endpoint_get_last_endpoint_send_target": (
                    stage.get(
                        "active_sender_endpoint_get_last_endpoint_send_target"
                    )
                ),
                "active_endpoint_send_hook_attempted": bool(
                    stage.get("active_endpoint_send_hook_attempted")
                ),
                "active_endpoint_send_hook_installed": bool(
                    stage.get("active_endpoint_send_hook_installed")
                ),
                "active_endpoint_send_hook_target": stage.get(
                    "active_endpoint_send_hook_target"
                ),
                "active_endpoint_send_calls": stage.get(
                    "active_endpoint_send_calls"
                ),
                "active_endpoint_send_data_opcode0_calls": stage.get(
                    "active_endpoint_send_data_opcode0_calls"
                ),
                "active_endpoint_send_data_opcode4_calls": stage.get(
                    "active_endpoint_send_data_opcode4_calls"
                ),
                "active_endpoint_send_data_opcode5_calls": stage.get(
                    "active_endpoint_send_data_opcode5_calls"
                ),
                "active_endpoint_send_data_opcode6_calls": stage.get(
                    "active_endpoint_send_data_opcode6_calls"
                ),
                "active_endpoint_send_data_opcode10_calls": stage.get(
                    "active_endpoint_send_data_opcode10_calls"
                ),
                "active_endpoint_send_data_opcode15_calls": stage.get(
                    "active_endpoint_send_data_opcode15_calls"
                ),
                "active_endpoint_send_data_opcode20_calls": stage.get(
                    "active_endpoint_send_data_opcode20_calls"
                ),
                "active_endpoint_send_data_opcode21_calls": stage.get(
                    "active_endpoint_send_data_opcode21_calls"
                ),
                "active_endpoint_send_last_caller_rva": stage.get(
                    "active_endpoint_send_last_caller_rva"
                ),
                "active_endpoint_send_last_endpoint": stage.get(
                    "active_endpoint_send_last_endpoint"
                ),
                "active_endpoint_send_last_endpoint_vtable": stage.get(
                    "active_endpoint_send_last_endpoint_vtable"
                ),
                "active_endpoint_send_last_endpoint_local_user_slot": (
                    stage.get(
                        "active_endpoint_send_last_endpoint_local_user_slot"
                    )
                ),
                "active_endpoint_send_last_packet": stage.get(
                    "active_endpoint_send_last_packet"
                ),
                "active_endpoint_send_last_packet_cursor": stage.get(
                    "active_endpoint_send_last_packet_cursor"
                ),
                "active_endpoint_send_last_packet_data": stage.get(
                    "active_endpoint_send_last_packet_data"
                ),
                "active_endpoint_send_last_packet_mode": stage.get(
                    "active_endpoint_send_last_packet_mode"
                ),
                "active_endpoint_send_last_packet_size": stage.get(
                    "active_endpoint_send_last_packet_size"
                ),
                "active_endpoint_send_last_packet_capacity": stage.get(
                    "active_endpoint_send_last_packet_capacity"
                ),
                "active_endpoint_send_last_packet_byte0": stage.get(
                    "active_endpoint_send_last_packet_byte0"
                ),
                "active_endpoint_send_last_packet_byte1": stage.get(
                    "active_endpoint_send_last_packet_byte1"
                ),
                "active_endpoint_send_last_packet_byte2": stage.get(
                    "active_endpoint_send_last_packet_byte2"
                ),
                "active_endpoint_send_last_packet_byte3": stage.get(
                    "active_endpoint_send_last_packet_byte3"
                ),
                "active_endpoint_send_last_packet_data_byte0": stage.get(
                    "active_endpoint_send_last_packet_data_byte0"
                ),
                "active_endpoint_send_last_packet_data_byte1": stage.get(
                    "active_endpoint_send_last_packet_data_byte1"
                ),
                "active_endpoint_send_last_packet_data_byte2": stage.get(
                    "active_endpoint_send_last_packet_data_byte2"
                ),
                "active_endpoint_send_last_packet_data_byte3": stage.get(
                    "active_endpoint_send_last_packet_data_byte3"
                ),
                "active_endpoint_send_last_arg2": stage.get(
                    "active_endpoint_send_last_arg2"
                ),
                "active_endpoint_send_last_result": stage.get(
                    "active_endpoint_send_last_result"
                ),
                "active_endpoint_send_opcode21_last_caller_rva": stage.get(
                    "active_endpoint_send_opcode21_last_caller_rva"
                ),
                "active_endpoint_send_opcode21_last_result": stage.get(
                    "active_endpoint_send_opcode21_last_result"
                ),
                "active_endpoint_send_opcode21_last_size": stage.get(
                    "active_endpoint_send_opcode21_last_size"
                ),
                "active_endpoint_send_opcode21_last_byte1": stage.get(
                    "active_endpoint_send_opcode21_last_byte1"
                ),
                "active_endpoint_send_opcode21_last_byte2": stage.get(
                    "active_endpoint_send_opcode21_last_byte2"
                ),
                "active_endpoint_send_opcode21_last_byte3": stage.get(
                    "active_endpoint_send_opcode21_last_byte3"
                ),
                "active_endpoint_send_last_active": stage.get(
                    "active_endpoint_send_last_active"
                ),
                "active_endpoint_send_last_active_state": stage.get(
                    "active_endpoint_send_last_active_state"
                ),
                "active_endpoint_send_last_active_sub_state": stage.get(
                    "active_endpoint_send_last_active_sub_state"
                ),
                "active_endpoint_send_last_transport_tick": stage.get(
                    "active_endpoint_send_last_transport_tick"
                ),
                "active_endpoint_send_last_transport_status": stage.get(
                    "active_endpoint_send_last_transport_status"
                ),
                "active_endpoint_send_last_transport_ready": stage.get(
                    "active_endpoint_send_last_transport_ready"
                ),
                "active_endpoint_send_last_transport_is_host": stage.get(
                    "active_endpoint_send_last_transport_is_host"
                ),
                "active_endpoint_send_last_transport_channel_count": (
                    stage.get(
                        "active_endpoint_send_last_transport_channel_count"
                    )
                ),
                "active_endpoint_send_last_transport_channel_capacity": (
                    stage.get(
                        "active_endpoint_send_last_transport_channel_capacity"
                    )
                ),
                "route_writer_resolve_hook_attempted": bool(
                    stage.get("route_writer_resolve_hook_attempted")
                ),
                "route_writer_resolve_hook_installed": bool(
                    stage.get("route_writer_resolve_hook_installed")
                ),
                "route_writer_resolve_calls": stage.get(
                    "route_writer_resolve_calls"
                ),
                "route_writer_resolve_last_caller_rva": stage.get(
                    "route_writer_resolve_last_caller_rva"
                ),
                "route_writer_resolve_last_root": stage.get(
                    "route_writer_resolve_last_root"
                ),
                "route_writer_resolve_last_route_key": stage.get(
                    "route_writer_resolve_last_route_key"
                ),
                "route_writer_resolve_last_route_tag": stage.get(
                    "route_writer_resolve_last_route_tag"
                ),
                "route_writer_resolve_last_writer": stage.get(
                    "route_writer_resolve_last_writer"
                ),
                "route_writer_resolve_last_writer_ref": stage.get(
                    "route_writer_resolve_last_writer_ref"
                ),
                "route_writer_resolve_last_writer_vtable": stage.get(
                    "route_writer_resolve_last_writer_vtable"
                ),
                "route_writer_resolve_last_writer_send_target": stage.get(
                    "route_writer_resolve_last_writer_send_target"
                ),
                "route_writer_registry_last_registry": stage.get(
                    "route_writer_registry_last_registry"
                ),
                "route_writer_registry_last_entries_begin": stage.get(
                    "route_writer_registry_last_entries_begin"
                ),
                "route_writer_registry_last_entries_end": stage.get(
                    "route_writer_registry_last_entries_end"
                ),
                "route_writer_registry_last_entries_capacity_end": stage.get(
                    "route_writer_registry_last_entries_capacity_end"
                ),
                "route_writer_registry_last_enabled": stage.get(
                    "route_writer_registry_last_enabled"
                ),
                "route_writer_registry_last_entry_count": stage.get(
                    "route_writer_registry_last_entry_count"
                ),
                "route_writer_registry_last_sample_count": stage.get(
                    "route_writer_registry_last_sample_count"
                ),
                "route_writer_registry_last_selected_index": stage.get(
                    "route_writer_registry_last_selected_index"
                ),
                "route_writer_registry_last_selected_owner": stage.get(
                    "route_writer_registry_last_selected_owner"
                ),
                "route_writer_registry_last_selected_owner_ref": stage.get(
                    "route_writer_registry_last_selected_owner_ref"
                ),
                "route_writer_registry_last_selected_owner_vtable": stage.get(
                    "route_writer_registry_last_selected_owner_vtable"
                ),
                "route_writer_registry_last_selected_writer": stage.get(
                    "route_writer_registry_last_selected_writer"
                ),
                "route_writer_registry_last_selected_writer_vtable": stage.get(
                    "route_writer_registry_last_selected_writer_vtable"
                ),
                "route_writer_registry_last_selected_writer_send_target": stage.get(
                    "route_writer_registry_last_selected_writer_send_target"
                ),
                "route_writer_registry_last_entry0_owner": stage.get(
                    "route_writer_registry_last_entry0_owner"
                ),
                "route_writer_registry_last_entry0_owner_ref": stage.get(
                    "route_writer_registry_last_entry0_owner_ref"
                ),
                "route_writer_registry_last_entry0_owner_vtable": stage.get(
                    "route_writer_registry_last_entry0_owner_vtable"
                ),
                "route_writer_registry_last_entry0_writer": stage.get(
                    "route_writer_registry_last_entry0_writer"
                ),
                "route_writer_registry_last_entry0_writer_vtable": stage.get(
                    "route_writer_registry_last_entry0_writer_vtable"
                ),
                "route_writer_registry_last_entry0_writer_send_target": stage.get(
                    "route_writer_registry_last_entry0_writer_send_target"
                ),
                "luxor_route_key_enum_hook_attempted": bool(
                    stage.get("luxor_route_key_enum_hook_attempted")
                ),
                "luxor_route_key_enum_hook_installed": bool(
                    stage.get("luxor_route_key_enum_hook_installed")
                ),
                "luxor_route_key_enum_hook_target": stage.get(
                    "luxor_route_key_enum_hook_target"
                ),
                "luxor_route_key_enum_calls": stage.get(
                    "luxor_route_key_enum_calls"
                ),
                "luxor_route_key_enum_last_caller_rva": stage.get(
                    "luxor_route_key_enum_last_caller_rva"
                ),
                "luxor_route_key_enum_last_route_service": stage.get(
                    "luxor_route_key_enum_last_route_service"
                ),
                "luxor_route_key_enum_last_output_array": stage.get(
                    "luxor_route_key_enum_last_output_array"
                ),
                "luxor_route_key_enum_last_result_array": stage.get(
                    "luxor_route_key_enum_last_result_array"
                ),
                "luxor_route_key_enum_last_entry_count": stage.get(
                    "luxor_route_key_enum_last_entry_count"
                ),
                "luxor_route_key_enum_last_sample_count": stage.get(
                    "luxor_route_key_enum_last_sample_count"
                ),
                "luxor_route_key_enum_last_service_shared_ref_count": stage.get(
                    "luxor_route_key_enum_last_service_shared_ref_count"
                ),
                "luxor_route_key_enum_last_replacement_count": stage.get(
                    "luxor_route_key_enum_last_replacement_count"
                ),
                "luxor_route_key_enum_last_nonreplacement_count": stage.get(
                    "luxor_route_key_enum_last_nonreplacement_count"
                ),
                "luxor_route_key_enum_last_default_count": stage.get(
                    "luxor_route_key_enum_last_default_count"
                ),
                "luxor_route_key_enum_last_expected_count": stage.get(
                    "luxor_route_key_enum_last_expected_count"
                ),
                "luxor_route_key_enum_last_entry0_owner": stage.get(
                    "luxor_route_key_enum_last_entry0_owner"
                ),
                "luxor_route_key_enum_last_entry0_route_tag": stage.get(
                    "luxor_route_key_enum_last_entry0_route_tag"
                ),
                "luxor_route_key_enum_last_entry1_owner": stage.get(
                    "luxor_route_key_enum_last_entry1_owner"
                ),
                "luxor_route_key_enum_last_entry1_route_tag": stage.get(
                    "luxor_route_key_enum_last_entry1_route_tag"
                ),
                "luxor_route_key_enum_last_entry2_owner": stage.get(
                    "luxor_route_key_enum_last_entry2_owner"
                ),
                "luxor_route_key_enum_last_entry2_route_tag": stage.get(
                    "luxor_route_key_enum_last_entry2_route_tag"
                ),
                "luxor_route_key_enum_last_entry3_owner": stage.get(
                    "luxor_route_key_enum_last_entry3_owner"
                ),
                "luxor_route_key_enum_last_entry3_route_tag": stage.get(
                    "luxor_route_key_enum_last_entry3_route_tag"
                ),
                "luxor_route_key_list_build_hook_attempted": bool(
                    stage.get("luxor_route_key_list_build_hook_attempted")
                ),
                "luxor_route_key_list_build_hook_installed": bool(
                    stage.get("luxor_route_key_list_build_hook_installed")
                ),
                "luxor_route_key_list_build_hook_target": stage.get(
                    "luxor_route_key_list_build_hook_target"
                ),
                "luxor_route_key_list_build_calls": stage.get(
                    "luxor_route_key_list_build_calls"
                ),
                "luxor_route_key_list_build_last_caller_rva": stage.get(
                    "luxor_route_key_list_build_last_caller_rva"
                ),
                "luxor_route_key_list_build_last_active": stage.get(
                    "luxor_route_key_list_build_last_active"
                ),
                "luxor_route_key_list_build_last_mode": stage.get(
                    "luxor_route_key_list_build_last_mode"
                ),
                "luxor_route_key_list_build_last_entry_count": stage.get(
                    "luxor_route_key_list_build_last_entry_count"
                ),
                "luxor_route_key_list_build_last_replacement_count": stage.get(
                    "luxor_route_key_list_build_last_replacement_count"
                ),
                "luxor_route_key_list_build_last_nonreplacement_count": stage.get(
                    "luxor_route_key_list_build_last_nonreplacement_count"
                ),
                "luxor_route_key_list_build_last_default_count": stage.get(
                    "luxor_route_key_list_build_last_default_count"
                ),
                "luxor_route_key_list_build_last_entry0_route_tag": stage.get(
                    "luxor_route_key_list_build_last_entry0_route_tag"
                ),
                "luxor_route_key_list_build_last_entry1_route_tag": stage.get(
                    "luxor_route_key_list_build_last_entry1_route_tag"
                ),
                "route_writer_source_acquire_hook_attempted": bool(
                    stage.get("route_writer_source_acquire_hook_attempted")
                ),
                "route_writer_source_acquire_hook_installed": bool(
                    stage.get("route_writer_source_acquire_hook_installed")
                ),
                "route_writer_source_acquire_hook_target": stage.get(
                    "route_writer_source_acquire_hook_target"
                ),
                "route_writer_source_acquire_calls": stage.get(
                    "route_writer_source_acquire_calls"
                ),
                "route_writer_source_acquire_last_caller_rva": stage.get(
                    "route_writer_source_acquire_last_caller_rva"
                ),
                "route_writer_source_acquire_last_route_service": stage.get(
                    "route_writer_source_acquire_last_route_service"
                ),
                "route_writer_source_acquire_last_out_writer_owner": stage.get(
                    "route_writer_source_acquire_last_out_writer_owner"
                ),
                "route_writer_source_acquire_last_result_pair": stage.get(
                    "route_writer_source_acquire_last_result_pair"
                ),
                "route_writer_source_acquire_last_native_route_source": stage.get(
                    "route_writer_source_acquire_last_native_route_source"
                ),
                "route_writer_source_acquire_last_native_route_source_vtable": stage.get(
                    "route_writer_source_acquire_last_native_route_source_vtable"
                ),
                "route_writer_source_acquire_last_use_routing_tag_object": stage.get(
                    "route_writer_source_acquire_last_use_routing_tag_object"
                ),
                "route_writer_source_acquire_last_out_owner": stage.get(
                    "route_writer_source_acquire_last_out_owner"
                ),
                "route_writer_source_acquire_last_out_owner_ref": stage.get(
                    "route_writer_source_acquire_last_out_owner_ref"
                ),
                "route_writer_source_acquire_last_registry_entry_count": stage.get(
                    "route_writer_source_acquire_last_registry_entry_count"
                ),
                "route_writer_source_acquire_last_selected_index": stage.get(
                    "route_writer_source_acquire_last_selected_index"
                ),
                "route_writer_acquire_hook_attempted": bool(
                    stage.get("route_writer_acquire_hook_attempted")
                ),
                "route_writer_acquire_hook_installed": bool(
                    stage.get("route_writer_acquire_hook_installed")
                ),
                "route_writer_acquire_hook_target": stage.get(
                    "route_writer_acquire_hook_target"
                ),
                "route_writer_acquire_calls": stage.get(
                    "route_writer_acquire_calls"
                ),
                "route_writer_acquire_last_caller_rva": stage.get(
                    "route_writer_acquire_last_caller_rva"
                ),
                "route_writer_acquire_last_route_service": stage.get(
                    "route_writer_acquire_last_route_service"
                ),
                "route_writer_acquire_last_route_key": stage.get(
                    "route_writer_acquire_last_route_key"
                ),
                "route_writer_acquire_last_route_tag": stage.get(
                    "route_writer_acquire_last_route_tag"
                ),
                "route_writer_acquire_last_out_owner": stage.get(
                    "route_writer_acquire_last_out_owner"
                ),
                "route_writer_acquire_last_out_owner_ref": stage.get(
                    "route_writer_acquire_last_out_owner_ref"
                ),
                "route_writer_acquire_last_registry_entry_count": stage.get(
                    "route_writer_acquire_last_registry_entry_count"
                ),
                "route_writer_acquire_last_selected_index": stage.get(
                    "route_writer_acquire_last_selected_index"
                ),
                "route_writer_assign_hook_attempted": bool(
                    stage.get("route_writer_assign_hook_attempted")
                ),
                "route_writer_assign_hook_installed": bool(
                    stage.get("route_writer_assign_hook_installed")
                ),
                "route_writer_assign_hook_target": stage.get(
                    "route_writer_assign_hook_target"
                ),
                "route_writer_assign_calls": stage.get(
                    "route_writer_assign_calls"
                ),
                "route_writer_assign_last_caller_rva": stage.get(
                    "route_writer_assign_last_caller_rva"
                ),
                "route_writer_assign_last_registry": stage.get(
                    "route_writer_assign_last_registry"
                ),
                "route_writer_assign_last_route_key": stage.get(
                    "route_writer_assign_last_route_key"
                ),
                "route_writer_assign_last_route_tag": stage.get(
                    "route_writer_assign_last_route_tag"
                ),
                "route_writer_assign_last_writer_mode": stage.get(
                    "route_writer_assign_last_writer_mode"
                ),
                "route_writer_assign_last_out_owner": stage.get(
                    "route_writer_assign_last_out_owner"
                ),
                "route_writer_assign_last_out_owner_ref": stage.get(
                    "route_writer_assign_last_out_owner_ref"
                ),
                "route_writer_assign_last_registry_entry_count": stage.get(
                    "route_writer_assign_last_registry_entry_count"
                ),
                "route_writer_assign_last_selected_index": stage.get(
                    "route_writer_assign_last_selected_index"
                ),
                "route_writer_send_hook_attempted": bool(
                    stage.get("route_writer_send_hook_attempted")
                ),
                "route_writer_send_hook_installed": bool(
                    stage.get("route_writer_send_hook_installed")
                ),
                "route_writer_send_hook_target": stage.get(
                    "route_writer_send_hook_target"
                ),
                "route_writer_send_calls": stage.get(
                    "route_writer_send_calls"
                ),
                "route_writer_send_data_opcode21_calls": stage.get(
                    "route_writer_send_data_opcode21_calls"
                ),
                "route_writer_send_last_caller_rva": stage.get(
                    "route_writer_send_last_caller_rva"
                ),
                "route_writer_send_last_writer": stage.get(
                    "route_writer_send_last_writer"
                ),
                "route_writer_send_last_writer_vtable": stage.get(
                    "route_writer_send_last_writer_vtable"
                ),
                "route_writer_send_last_primary_peer_object": stage.get(
                    "route_writer_send_last_primary_peer_object"
                ),
                "route_writer_send_last_primary_peer_ref": stage.get(
                    "route_writer_send_last_primary_peer_ref"
                ),
                "route_writer_send_last_route_map_vtable": stage.get(
                    "route_writer_send_last_route_map_vtable"
                ),
                "route_writer_send_last_small_route_vtable": stage.get(
                    "route_writer_send_last_small_route_vtable"
                ),
                "route_writer_send_last_large_packet_sink_vtable": stage.get(
                    "route_writer_send_last_large_packet_sink_vtable"
                ),
                "route_writer_send_last_small_route_ready_target": stage.get(
                    "route_writer_send_last_small_route_ready_target"
                ),
                "route_writer_send_last_small_route_send_target": stage.get(
                    "route_writer_send_last_small_route_send_target"
                ),
                "route_writer_send_last_small_route_state": stage.get(
                    "route_writer_send_last_small_route_state"
                ),
                "route_writer_send_last_small_route_entries": stage.get(
                    "route_writer_send_last_small_route_entries"
                ),
                "route_writer_send_last_small_route_buckets": stage.get(
                    "route_writer_send_last_small_route_buckets"
                ),
                "route_writer_send_last_small_route_sample_ok": stage.get(
                    "route_writer_send_last_small_route_sample_ok"
                ),
                "route_writer_send_last_small_route_count": stage.get(
                    "route_writer_send_last_small_route_count"
                ),
                "route_writer_send_last_small_route_limit": stage.get(
                    "route_writer_send_last_small_route_limit"
                ),
                "route_writer_send_last_small_route_bucket_mask_plus_one": stage.get(
                    "route_writer_send_last_small_route_bucket_mask_plus_one"
                ),
                "route_writer_send_last_small_route_sequence_counter": stage.get(
                    "route_writer_send_last_small_route_sequence_counter"
                ),
                "route_writer_send_last_small_route_next_sequence": stage.get(
                    "route_writer_send_last_small_route_next_sequence"
                ),
                "route_writer_send_last_small_route_next_available": stage.get(
                    "route_writer_send_last_small_route_next_available"
                ),
                "route_writer_send_last_small_route_next_slot_present": stage.get(
                    "route_writer_send_last_small_route_next_slot_present"
                ),
                "route_writer_send_last_small_route_bucket_index": stage.get(
                    "route_writer_send_last_small_route_bucket_index"
                ),
                "route_writer_send_last_small_route_collision_entry_index": stage.get(
                    "route_writer_send_last_small_route_collision_entry_index"
                ),
                "route_writer_send_last_small_route_walk_steps": stage.get(
                    "route_writer_send_last_small_route_walk_steps"
                ),
                "route_writer_send_last_route_map_capacity_target": stage.get(
                    "route_writer_send_last_route_map_capacity_target"
                ),
                "route_writer_send_last_route_map_send_target": stage.get(
                    "route_writer_send_last_route_map_send_target"
                ),
                "route_writer_send_last_route_map_bind_target": stage.get(
                    "route_writer_send_last_route_map_bind_target"
                ),
                "route_writer_send_last_route_map_flush_target": stage.get(
                    "route_writer_send_last_route_map_flush_target"
                ),
                "route_writer_send_last_large_packet_sink_target": stage.get(
                    "route_writer_send_last_large_packet_sink_target"
                ),
                "route_writer_send_last_deferred_count": stage.get(
                    "route_writer_send_last_deferred_count"
                ),
                "route_writer_send_last_deferred_capacity": stage.get(
                    "route_writer_send_last_deferred_capacity"
                ),
                "route_writer_send_last_backend_available": stage.get(
                    "route_writer_send_last_backend_available"
                ),
                "route_writer_send_last_secondary_backend_available": stage.get(
                    "route_writer_send_last_secondary_backend_available"
                ),
                "route_writer_send_last_packet": stage.get(
                    "route_writer_send_last_packet"
                ),
                "route_writer_send_last_route_tag": stage.get(
                    "route_writer_send_last_route_tag"
                ),
                "route_writer_send_last_local_user": stage.get(
                    "route_writer_send_last_local_user"
                ),
                "route_writer_send_last_send_flag": stage.get(
                    "route_writer_send_last_send_flag"
                ),
                "route_writer_send_last_local_user_is8": stage.get(
                    "route_writer_send_last_local_user_is8"
                ),
                "route_writer_send_last_packet_data_byte0": stage.get(
                    "route_writer_send_last_packet_data_byte0"
                ),
                "route_writer_send_last_packet_data_byte1": stage.get(
                    "route_writer_send_last_packet_data_byte1"
                ),
                "route_writer_send_last_packet_mode": stage.get(
                    "route_writer_send_last_packet_mode"
                ),
                "route_writer_send_last_packet_size": stage.get(
                    "route_writer_send_last_packet_size"
                ),
                "route_writer_send_last_result": stage.get(
                    "route_writer_send_last_result"
                ),
                "route_writer_send_last_inferred_branch": stage.get(
                    "route_writer_send_last_inferred_branch"
                ),
                "route_writer_send_opcode21_last_result": stage.get(
                    "route_writer_send_opcode21_last_result"
                ),
                "route_writer_send_opcode21_small_route_pre_available": stage.get(
                    "route_writer_send_opcode21_small_route_pre_available"
                ),
                "route_writer_send_opcode21_small_route_post_available": stage.get(
                    "route_writer_send_opcode21_small_route_post_available"
                ),
                "route_writer_send_opcode21_small_route_pre_slot_present": stage.get(
                    "route_writer_send_opcode21_small_route_pre_slot_present"
                ),
                "route_writer_send_opcode21_small_route_post_slot_present": stage.get(
                    "route_writer_send_opcode21_small_route_post_slot_present"
                ),
                "route_writer_send_opcode21_small_route_pre_sequence": stage.get(
                    "route_writer_send_opcode21_small_route_pre_sequence"
                ),
                "route_writer_send_opcode21_small_route_post_sequence": stage.get(
                    "route_writer_send_opcode21_small_route_post_sequence"
                ),
                "route_writer_send_opcode21_small_route_pre_count": stage.get(
                    "route_writer_send_opcode21_small_route_pre_count"
                ),
                "route_writer_send_opcode21_small_route_post_count": stage.get(
                    "route_writer_send_opcode21_small_route_post_count"
                ),
                "route_writer_send_opcode21_small_route_pre_collision_index": stage.get(
                    "route_writer_send_opcode21_small_route_pre_collision_index"
                ),
                "route_writer_send_opcode21_small_route_post_collision_index": stage.get(
                    "route_writer_send_opcode21_small_route_post_collision_index"
                ),
                "route_writer_send_last_parent": stage.get(
                    "route_writer_send_last_parent"
                ),
                "route_writer_send_last_parent_vtable": stage.get(
                    "route_writer_send_last_parent_vtable"
                ),
                "route_writer_send_last_parent_ready_target": stage.get(
                    "route_writer_send_last_parent_ready_target"
                ),
                "route_writer_send_last_parent_state_target": stage.get(
                    "route_writer_send_last_parent_state_target"
                ),
                "route_writer_send_last_parent_identity_target": stage.get(
                    "route_writer_send_last_parent_identity_target"
                ),
                "route_writer_send_last_parent_backend_get_target": stage.get(
                    "route_writer_send_last_parent_backend_get_target"
                ),
                "route_writer_send_last_parent_state": stage.get(
                    "route_writer_send_last_parent_state"
                ),
                "route_writer_send_last_parent_ready_flags": stage.get(
                    "route_writer_send_last_parent_ready_flags"
                ),
                "route_writer_send_last_parent_ready_flag_set": stage.get(
                    "route_writer_send_last_parent_ready_flag_set"
                ),
                "route_writer_send_last_parent_ready_state_ok": stage.get(
                    "route_writer_send_last_parent_ready_state_ok"
                ),
                "route_writer_send_last_parent_ready": stage.get(
                    "route_writer_send_last_parent_ready"
                ),
                "route_writer_send_opcode21_last_parent": stage.get(
                    "route_writer_send_opcode21_last_parent"
                ),
                "route_writer_send_opcode21_last_parent_vtable": stage.get(
                    "route_writer_send_opcode21_last_parent_vtable"
                ),
                "route_writer_send_opcode21_last_parent_backend_get_target": stage.get(
                    "route_writer_send_opcode21_last_parent_backend_get_target"
                ),
                "route_writer_send_opcode21_parent_state": stage.get(
                    "route_writer_send_opcode21_parent_state"
                ),
                "route_writer_send_opcode21_parent_ready_flags": stage.get(
                    "route_writer_send_opcode21_parent_ready_flags"
                ),
                "route_writer_send_opcode21_parent_ready": stage.get(
                    "route_writer_send_opcode21_parent_ready"
                ),
                "route_writer_backend_get_hook_attempted": bool(
                    stage.get("route_writer_backend_get_hook_attempted")
                ),
                "route_writer_backend_get_hook_installed": bool(
                    stage.get("route_writer_backend_get_hook_installed")
                ),
                "route_writer_backend_get_hook_target": stage.get(
                    "route_writer_backend_get_hook_target"
                ),
                "route_writer_backend_get_calls": stage.get(
                    "route_writer_backend_get_calls"
                ),
                "route_writer_backend_get_last_caller_rva": stage.get(
                    "route_writer_backend_get_last_caller_rva"
                ),
                "route_writer_backend_get_last_parent": stage.get(
                    "route_writer_backend_get_last_parent"
                ),
                "route_writer_backend_get_last_parent_vtable": stage.get(
                    "route_writer_backend_get_last_parent_vtable"
                ),
                "route_writer_backend_get_last_backend": stage.get(
                    "route_writer_backend_get_last_backend"
                ),
                "route_writer_backend_get_last_backend_vtable": stage.get(
                    "route_writer_backend_get_last_backend_vtable"
                ),
                "route_writer_backend_get_last_backend_send_target": stage.get(
                    "route_writer_backend_get_last_backend_send_target"
                ),
                "route_writer_backend_get_last_backend_route_channel_target": stage.get(
                    "route_writer_backend_get_last_backend_route_channel_target"
                ),
                "route_writer_backend_send_hook_attempted": bool(
                    stage.get("route_writer_backend_send_hook_attempted")
                ),
                "route_writer_backend_send_hook_installed": bool(
                    stage.get("route_writer_backend_send_hook_installed")
                ),
                "route_writer_backend_send_hook_target": stage.get(
                    "route_writer_backend_send_hook_target"
                ),
                "route_writer_backend_send_calls": stage.get(
                    "route_writer_backend_send_calls"
                ),
                "route_writer_backend_send_magic_calls": stage.get(
                    "route_writer_backend_send_magic_calls"
                ),
                "route_writer_backend_send_last_caller_rva": stage.get(
                    "route_writer_backend_send_last_caller_rva"
                ),
                "route_writer_backend_send_last_backend": stage.get(
                    "route_writer_backend_send_last_backend"
                ),
                "route_writer_backend_send_last_backend_vtable": stage.get(
                    "route_writer_backend_send_last_backend_vtable"
                ),
                "route_writer_backend_send_last_destination": stage.get(
                    "route_writer_backend_send_last_destination"
                ),
                "route_writer_backend_send_last_packet_data": stage.get(
                    "route_writer_backend_send_last_packet_data"
                ),
                "route_writer_backend_send_last_packet_size": stage.get(
                    "route_writer_backend_send_last_packet_size"
                ),
                "route_writer_backend_send_last_magic": stage.get(
                    "route_writer_backend_send_last_magic"
                ),
                "route_writer_backend_send_last_local_user": stage.get(
                    "route_writer_backend_send_last_local_user"
                ),
                "route_writer_backend_send_last_marker": stage.get(
                    "route_writer_backend_send_last_marker"
                ),
                "route_writer_backend_send_last_payload_opcode": stage.get(
                    "route_writer_backend_send_last_payload_opcode"
                ),
                "route_writer_backend_send_last_payload_byte1": stage.get(
                    "route_writer_backend_send_last_payload_byte1"
                ),
                "route_writer_backend_send_last_payload_byte2": stage.get(
                    "route_writer_backend_send_last_payload_byte2"
                ),
                "route_writer_backend_send_last_backend_parent": stage.get(
                    "route_writer_backend_send_last_backend_parent"
                ),
                "route_writer_backend_send_last_backend_parent_vtable": stage.get(
                    "route_writer_backend_send_last_backend_parent_vtable"
                ),
                "route_writer_backend_send_last_connection_lookup_target": stage.get(
                    "route_writer_backend_send_last_connection_lookup_target"
                ),
                "route_writer_backend_send_last_destination_key": stage.get(
                    "route_writer_backend_send_last_destination_key"
                ),
                "route_writer_backend_send_last_destination_ref": stage.get(
                    "route_writer_backend_send_last_destination_ref"
                ),
                "luxor_backend_route_channel_forward_hook_attempted": bool(
                    stage.get("luxor_backend_route_channel_forward_hook_attempted")
                ),
                "luxor_backend_route_channel_forward_hook_installed": bool(
                    stage.get("luxor_backend_route_channel_forward_hook_installed")
                ),
                "luxor_backend_route_channel_forward_hook_target": stage.get(
                    "luxor_backend_route_channel_forward_hook_target"
                ),
                "luxor_backend_route_channel_forward_calls": stage.get(
                    "luxor_backend_route_channel_forward_calls"
                ),
                "luxor_backend_route_channel_forward_last_caller_rva": stage.get(
                    "luxor_backend_route_channel_forward_last_caller_rva"
                ),
                "luxor_backend_route_channel_forward_last_backend": stage.get(
                    "luxor_backend_route_channel_forward_last_backend"
                ),
                "luxor_backend_route_channel_forward_last_backend_vtable": stage.get(
                    "luxor_backend_route_channel_forward_last_backend_vtable"
                ),
                "luxor_backend_route_channel_forward_last_connection": stage.get(
                    "luxor_backend_route_channel_forward_last_connection"
                ),
                "luxor_backend_route_channel_forward_last_connection_vtable": stage.get(
                    "luxor_backend_route_channel_forward_last_connection_vtable"
                ),
                "luxor_backend_route_channel_forward_last_output_slots_target": stage.get(
                    "luxor_backend_route_channel_forward_last_output_slots_target"
                ),
                "luxor_backend_route_channel_forward_last_route_channel": stage.get(
                    "luxor_backend_route_channel_forward_last_route_channel"
                ),
                "luxor_backend_route_channel_forward_last_route_channel_vtable": stage.get(
                    "luxor_backend_route_channel_forward_last_route_channel_vtable"
                ),
                "luxor_backend_route_channel_forward_last_used_before": stage.get(
                    "luxor_backend_route_channel_forward_last_used_before"
                ),
                "luxor_backend_route_channel_forward_last_used_after": stage.get(
                    "luxor_backend_route_channel_forward_last_used_after"
                ),
                "luxor_route_channel_output_slots_hook_attempted": bool(
                    stage.get("luxor_route_channel_output_slots_hook_attempted")
                ),
                "luxor_route_channel_output_slots_hook_installed": bool(
                    stage.get("luxor_route_channel_output_slots_hook_installed")
                ),
                "luxor_route_channel_output_slots_hook_target": stage.get(
                    "luxor_route_channel_output_slots_hook_target"
                ),
                "luxor_route_channel_output_slots_calls": stage.get(
                    "luxor_route_channel_output_slots_calls"
                ),
                "luxor_route_channel_output_slots_last_caller_rva": stage.get(
                    "luxor_route_channel_output_slots_last_caller_rva"
                ),
                "luxor_route_channel_output_slots_last_route_sink": stage.get(
                    "luxor_route_channel_output_slots_last_route_sink"
                ),
                "luxor_route_channel_output_slots_last_route_sink_vtable": stage.get(
                    "luxor_route_channel_output_slots_last_route_sink_vtable"
                ),
                "luxor_route_channel_output_slots_last_slots_begin": stage.get(
                    "luxor_route_channel_output_slots_last_slots_begin"
                ),
                "luxor_route_channel_output_slots_last_slots_end": stage.get(
                    "luxor_route_channel_output_slots_last_slots_end"
                ),
                "luxor_route_channel_output_slots_last_slot_count": stage.get(
                    "luxor_route_channel_output_slots_last_slot_count"
                ),
                "luxor_route_channel_output_slots_last_identity_object": stage.get(
                    "luxor_route_channel_output_slots_last_identity_object"
                ),
                "luxor_route_channel_output_slots_last_identity_ref": stage.get(
                    "luxor_route_channel_output_slots_last_identity_ref"
                ),
                "luxor_route_channel_output_slots_last_route_channel": stage.get(
                    "luxor_route_channel_output_slots_last_route_channel"
                ),
                "luxor_route_channel_output_slots_last_route_channel_vtable": stage.get(
                    "luxor_route_channel_output_slots_last_route_channel_vtable"
                ),
                "luxor_route_channel_output_slots_last_used_before": stage.get(
                    "luxor_route_channel_output_slots_last_used_before"
                ),
                "luxor_route_channel_output_slots_last_used_after": stage.get(
                    "luxor_route_channel_output_slots_last_used_after"
                ),
                "luxor_route_frame_output_slot_hook_attempted": bool(
                    stage.get("luxor_route_frame_output_slot_hook_attempted")
                ),
                "luxor_route_frame_output_slot_hook_installed": bool(
                    stage.get("luxor_route_frame_output_slot_hook_installed")
                ),
                "luxor_route_frame_output_slot_hook_target": stage.get(
                    "luxor_route_frame_output_slot_hook_target"
                ),
                "luxor_route_frame_output_slot_calls": stage.get(
                    "luxor_route_frame_output_slot_calls"
                ),
                "luxor_route_frame_output_slot_reject_slot_ff_calls": stage.get(
                    "luxor_route_frame_output_slot_reject_slot_ff_calls"
                ),
                "luxor_route_frame_output_slot_success_calls": stage.get(
                    "luxor_route_frame_output_slot_success_calls"
                ),
                "luxor_route_frame_output_slot_last_caller_rva": stage.get(
                    "luxor_route_frame_output_slot_last_caller_rva"
                ),
                "luxor_route_frame_output_slot_last_output_slot": stage.get(
                    "luxor_route_frame_output_slot_last_output_slot"
                ),
                "luxor_route_frame_output_slot_last_output_slot_index": stage.get(
                    "luxor_route_frame_output_slot_last_output_slot_index"
                ),
                "luxor_route_frame_output_slot_last_identity_object": stage.get(
                    "luxor_route_frame_output_slot_last_identity_object"
                ),
                "luxor_route_frame_output_slot_last_identity_ref": stage.get(
                    "luxor_route_frame_output_slot_last_identity_ref"
                ),
                "luxor_route_frame_output_slot_last_route_channel": stage.get(
                    "luxor_route_frame_output_slot_last_route_channel"
                ),
                "luxor_route_frame_output_slot_last_route_channel_vtable": stage.get(
                    "luxor_route_frame_output_slot_last_route_channel_vtable"
                ),
                "luxor_route_frame_output_slot_last_used_before": stage.get(
                    "luxor_route_frame_output_slot_last_used_before"
                ),
                "luxor_route_frame_output_slot_last_used_after": stage.get(
                    "luxor_route_frame_output_slot_last_used_after"
                ),
                "luxor_route_frame_output_slot_last_result": stage.get(
                    "luxor_route_frame_output_slot_last_result"
                ),
                "luxor_route_output_task_queue_hook_attempted": bool(
                    stage.get("luxor_route_output_task_queue_hook_attempted")
                ),
                "luxor_route_output_task_queue_hook_installed": bool(
                    stage.get("luxor_route_output_task_queue_hook_installed")
                ),
                "luxor_route_output_task_queue_hook_target": stage.get(
                    "luxor_route_output_task_queue_hook_target"
                ),
                "luxor_route_output_task_queue_calls": stage.get(
                    "luxor_route_output_task_queue_calls"
                ),
                "luxor_route_output_task_queue_last_caller_rva": stage.get(
                    "luxor_route_output_task_queue_last_caller_rva"
                ),
                "luxor_route_output_task_queue_last_queue": stage.get(
                    "luxor_route_output_task_queue_last_queue"
                ),
                "luxor_route_output_task_queue_last_heap_entries": stage.get(
                    "luxor_route_output_task_queue_last_heap_entries"
                ),
                "luxor_route_output_task_queue_last_entry_count": stage.get(
                    "luxor_route_output_task_queue_last_entry_count"
                ),
                "luxor_route_output_task_queue_last_valid_entry_count": stage.get(
                    "luxor_route_output_task_queue_last_valid_entry_count"
                ),
                "luxor_route_output_task_queue_last_slot_index": stage.get(
                    "luxor_route_output_task_queue_last_slot_index"
                ),
                "luxor_route_output_task_queue_last_dispatch_depth_before": stage.get(
                    "luxor_route_output_task_queue_last_dispatch_depth_before"
                ),
                "luxor_route_output_task_queue_last_dispatch_depth_after": stage.get(
                    "luxor_route_output_task_queue_last_dispatch_depth_after"
                ),
                "luxor_route_output_task_queue_last_frame_archive": stage.get(
                    "luxor_route_output_task_queue_last_frame_archive"
                ),
                "luxor_route_output_task_queue_last_first_consumer": stage.get(
                    "luxor_route_output_task_queue_last_first_consumer"
                ),
                "luxor_route_output_task_queue_last_first_consumer_vtable": stage.get(
                    "luxor_route_output_task_queue_last_first_consumer_vtable"
                ),
                "luxor_route_output_task_queue_last_first_consumer_accept_target": stage.get(
                    "luxor_route_output_task_queue_last_first_consumer_accept_target"
                ),
                "luxor_route_output_task_queue_last_last_consumer": stage.get(
                    "luxor_route_output_task_queue_last_last_consumer"
                ),
                "luxor_route_output_task_queue_last_last_consumer_vtable": stage.get(
                    "luxor_route_output_task_queue_last_last_consumer_vtable"
                ),
                "luxor_route_output_task_queue_last_last_consumer_accept_target": stage.get(
                    "luxor_route_output_task_queue_last_last_consumer_accept_target"
                ),
                "luxor_route_output_task_queue_last_callback": stage.get(
                    "luxor_route_output_task_queue_last_callback"
                ),
                "luxor_route_output_task_queue_last_receiver_base": stage.get(
                    "luxor_route_output_task_queue_last_receiver_base"
                ),
                "luxor_route_output_task_queue_last_receiver_ref": stage.get(
                    "luxor_route_output_task_queue_last_receiver_ref"
                ),
                "luxor_route_output_task_queue_last_receiver_adjustment": stage.get(
                    "luxor_route_output_task_queue_last_receiver_adjustment"
                ),
                "luxor_route_output_task_queue_last_adjusted_receiver": stage.get(
                    "luxor_route_output_task_queue_last_adjusted_receiver"
                ),
                "luxor_route_output_task_queue_last_consumer_layout": stage.get(
                    "luxor_route_output_task_queue_last_consumer_layout"
                ),
                "luxor_route_output_task_queue_last_frame_magic": stage.get(
                    "luxor_route_output_task_queue_last_frame_magic"
                ),
                "luxor_route_output_task_queue_last_frame_payload_opcode": stage.get(
                    "luxor_route_output_task_queue_last_frame_payload_opcode"
                ),
                "luxor_route_output_task_consumer_hook_attempted": bool(
                    stage.get("luxor_route_output_task_consumer_hook_attempted")
                ),
                "luxor_route_output_task_consumer_hook_installed": bool(
                    stage.get("luxor_route_output_task_consumer_hook_installed")
                ),
                "luxor_route_output_task_consumer_hook_target": stage.get(
                    "luxor_route_output_task_consumer_hook_target"
                ),
                "luxor_route_output_task_consumer_calls": stage.get(
                    "luxor_route_output_task_consumer_calls"
                ),
                "luxor_route_output_task_consumer_last_caller_rva": stage.get(
                    "luxor_route_output_task_consumer_last_caller_rva"
                ),
                "luxor_route_output_task_consumer_last_consumer": stage.get(
                    "luxor_route_output_task_consumer_last_consumer"
                ),
                "luxor_route_output_task_consumer_last_consumer_vtable": stage.get(
                    "luxor_route_output_task_consumer_last_consumer_vtable"
                ),
                "luxor_route_output_task_consumer_last_slot_index": stage.get(
                    "luxor_route_output_task_consumer_last_slot_index"
                ),
                "luxor_route_output_task_consumer_last_frame_archive": stage.get(
                    "luxor_route_output_task_consumer_last_frame_archive"
                ),
                "luxor_route_output_task_consumer_last_callback": stage.get(
                    "luxor_route_output_task_consumer_last_callback"
                ),
                "luxor_route_output_task_consumer_last_receiver_base": stage.get(
                    "luxor_route_output_task_consumer_last_receiver_base"
                ),
                "luxor_route_output_task_consumer_last_receiver_adjustment": stage.get(
                    "luxor_route_output_task_consumer_last_receiver_adjustment"
                ),
                "luxor_route_output_task_consumer_last_adjusted_receiver": stage.get(
                    "luxor_route_output_task_consumer_last_adjusted_receiver"
                ),
                "luxor_route_output_task_consumer_last_result": stage.get(
                    "luxor_route_output_task_consumer_last_result"
                ),
                "luxor_route_output_task_consumer_last_frame_magic": stage.get(
                    "luxor_route_output_task_consumer_last_frame_magic"
                ),
                "luxor_route_output_task_consumer_last_frame_payload_opcode": stage.get(
                    "luxor_route_output_task_consumer_last_frame_payload_opcode"
                ),
                "luxor_forwarding_route_output_task_consumer_hook_attempted": bool(
                    stage.get(
                        "luxor_forwarding_route_output_task_consumer_hook_attempted"
                    )
                ),
                "luxor_forwarding_route_output_task_consumer_hook_installed": bool(
                    stage.get(
                        "luxor_forwarding_route_output_task_consumer_hook_installed"
                    )
                ),
                "luxor_forwarding_route_output_task_consumer_hook_target": stage.get(
                    "luxor_forwarding_route_output_task_consumer_hook_target"
                ),
                "luxor_forwarding_route_output_task_consumer_calls": stage.get(
                    "luxor_forwarding_route_output_task_consumer_calls"
                ),
                "luxor_forwarding_route_output_task_consumer_last_caller_rva": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_caller_rva"
                ),
                "luxor_forwarding_route_output_task_consumer_last_consumer": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_consumer"
                ),
                "luxor_forwarding_route_output_task_consumer_last_consumer_vtable": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_consumer_vtable"
                ),
                "luxor_forwarding_route_output_task_consumer_last_slot_index": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_slot_index"
                ),
                "luxor_forwarding_route_output_task_consumer_last_frame_archive": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_frame_archive"
                ),
                "luxor_forwarding_route_output_task_consumer_last_receiver_base": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_receiver_base"
                ),
                "luxor_forwarding_route_output_task_consumer_last_receiver_ref": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_receiver_ref"
                ),
                "luxor_forwarding_route_output_task_consumer_last_callback": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_callback"
                ),
                "luxor_forwarding_route_output_task_consumer_last_receiver_adjustment": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_receiver_adjustment"
                ),
                "luxor_forwarding_route_output_task_consumer_last_adjusted_receiver": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_adjusted_receiver"
                ),
                "luxor_forwarding_route_output_task_consumer_last_result": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_result"
                ),
                "luxor_forwarding_route_output_task_consumer_last_frame_magic": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_frame_magic"
                ),
                "luxor_forwarding_route_output_task_consumer_last_frame_payload_opcode": stage.get(
                    "luxor_forwarding_route_output_task_consumer_last_frame_payload_opcode"
                ),
                "luxor_forwarded_route_opcode_dispatch_hook_attempted": bool(
                    stage.get("luxor_forwarded_route_opcode_dispatch_hook_attempted")
                ),
                "luxor_forwarded_route_opcode_dispatch_hook_installed": bool(
                    stage.get("luxor_forwarded_route_opcode_dispatch_hook_installed")
                ),
                "luxor_forwarded_route_opcode_dispatch_hook_target": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_hook_target"
                ),
                "luxor_forwarded_route_opcode_dispatch_calls": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_calls"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_caller_rva": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_caller_rva"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_dispatcher": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_dispatcher"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_opcode_tree": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_opcode_tree"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_opcode": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_opcode"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_handler_node": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_handler_node"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_handler_key": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_handler_key"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_handler_storage": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_handler_storage"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_handler_found": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_handler_found"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_frame_magic": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_frame_magic"
                ),
                "luxor_forwarded_route_opcode_dispatch_last_frame_payload_opcode": stage.get(
                    "luxor_forwarded_route_opcode_dispatch_last_frame_payload_opcode"
                ),
                "luxor_backend_connection_lookup_hook_attempted": bool(
                    stage.get("luxor_backend_connection_lookup_hook_attempted")
                ),
                "luxor_backend_connection_lookup_hook_installed": bool(
                    stage.get("luxor_backend_connection_lookup_hook_installed")
                ),
                "luxor_backend_connection_lookup_hook_target": stage.get(
                    "luxor_backend_connection_lookup_hook_target"
                ),
                "luxor_backend_connection_lookup_calls": stage.get(
                    "luxor_backend_connection_lookup_calls"
                ),
                "luxor_backend_connection_lookup_last_caller_rva": stage.get(
                    "luxor_backend_connection_lookup_last_caller_rva"
                ),
                "luxor_backend_connection_lookup_last_map": stage.get(
                    "luxor_backend_connection_lookup_last_map"
                ),
                "luxor_backend_connection_lookup_last_destination_key": stage.get(
                    "luxor_backend_connection_lookup_last_destination_key"
                ),
                "luxor_backend_connection_lookup_last_connection": stage.get(
                    "luxor_backend_connection_lookup_last_connection"
                ),
                "luxor_backend_connection_lookup_last_connection_ref": stage.get(
                    "luxor_backend_connection_lookup_last_connection_ref"
                ),
                "luxor_backend_connection_lookup_last_connection_vtable": stage.get(
                    "luxor_backend_connection_lookup_last_connection_vtable"
                ),
                "luxor_backend_connection_lookup_last_raw_send_target": stage.get(
                    "luxor_backend_connection_lookup_last_raw_send_target"
                ),
                "luxor_backend_connection_destination_match_hook_attempted": bool(
                    stage.get(
                        "luxor_backend_connection_destination_match_hook_attempted"
                    )
                ),
                "luxor_backend_connection_destination_match_hook_installed": bool(
                    stage.get(
                        "luxor_backend_connection_destination_match_hook_installed"
                    )
                ),
                "luxor_backend_connection_destination_match_hook_target": stage.get(
                    "luxor_backend_connection_destination_match_hook_target"
                ),
                "luxor_backend_connection_destination_match_calls": stage.get(
                    "luxor_backend_connection_destination_match_calls"
                ),
                "luxor_backend_connection_destination_match_true_calls": stage.get(
                    "luxor_backend_connection_destination_match_true_calls"
                ),
                "luxor_backend_connection_destination_match_false_calls": stage.get(
                    "luxor_backend_connection_destination_match_false_calls"
                ),
                "luxor_backend_connection_destination_match_last_caller_rva": stage.get(
                    "luxor_backend_connection_destination_match_last_caller_rva"
                ),
                "luxor_backend_connection_destination_match_last_destination_pair": stage.get(
                    "luxor_backend_connection_destination_match_last_destination_pair"
                ),
                "luxor_backend_connection_destination_match_last_destination_key": stage.get(
                    "luxor_backend_connection_destination_match_last_destination_key"
                ),
                "luxor_backend_connection_destination_match_last_destination_ref": stage.get(
                    "luxor_backend_connection_destination_match_last_destination_ref"
                ),
                "luxor_backend_connection_destination_match_last_candidate_pair": stage.get(
                    "luxor_backend_connection_destination_match_last_candidate_pair"
                ),
                "luxor_backend_connection_destination_match_last_candidate_connection": stage.get(
                    "luxor_backend_connection_destination_match_last_candidate_connection"
                ),
                "luxor_backend_connection_destination_match_last_candidate_ref": stage.get(
                    "luxor_backend_connection_destination_match_last_candidate_ref"
                ),
                "luxor_backend_connection_destination_match_last_candidate_vtable": stage.get(
                    "luxor_backend_connection_destination_match_last_candidate_vtable"
                ),
                "luxor_backend_connection_destination_match_last_candidate_raw_send_target": stage.get(
                    "luxor_backend_connection_destination_match_last_candidate_raw_send_target"
                ),
                "luxor_backend_connection_destination_match_last_timeline_opcode": stage.get(
                    "luxor_backend_connection_destination_match_last_timeline_opcode"
                ),
                "luxor_backend_connection_destination_match_last_result": stage.get(
                    "luxor_backend_connection_destination_match_last_result"
                ),
                "luxor_backend_connection_table_last_owner": stage.get(
                    "luxor_backend_connection_table_last_owner"
                ),
                "luxor_backend_connection_table_last_table": stage.get(
                    "luxor_backend_connection_table_last_table"
                ),
                "luxor_backend_connection_table_last_entries_begin": stage.get(
                    "luxor_backend_connection_table_last_entries_begin"
                ),
                "luxor_backend_connection_table_last_entries_end": stage.get(
                    "luxor_backend_connection_table_last_entries_end"
                ),
                "luxor_backend_connection_table_last_entries_capacity_end": stage.get(
                    "luxor_backend_connection_table_last_entries_capacity_end"
                ),
                "luxor_backend_connection_table_last_entry_count": stage.get(
                    "luxor_backend_connection_table_last_entry_count"
                ),
                "luxor_backend_connection_table_last_sample_count": stage.get(
                    "luxor_backend_connection_table_last_sample_count"
                ),
                "luxor_backend_connection_table_last_selected_index": stage.get(
                    "luxor_backend_connection_table_last_selected_index"
                ),
                "luxor_backend_connection_table_last_selected_connection": stage.get(
                    "luxor_backend_connection_table_last_selected_connection"
                ),
                "luxor_backend_connection_table_last_selected_raw_send_target": stage.get(
                    "luxor_backend_connection_table_last_selected_raw_send_target"
                ),
                "luxor_backend_connection_table_last_entry0_connection": stage.get(
                    "luxor_backend_connection_table_last_entry0_connection"
                ),
                "luxor_backend_connection_table_last_entry0_raw_send_target": stage.get(
                    "luxor_backend_connection_table_last_entry0_raw_send_target"
                ),
                "luxor_backend_connection_table_last_entry1_connection": stage.get(
                    "luxor_backend_connection_table_last_entry1_connection"
                ),
                "luxor_backend_connection_table_last_entry1_raw_send_target": stage.get(
                    "luxor_backend_connection_table_last_entry1_raw_send_target"
                ),
                "luxor_backend_connection_raw_send_hook_attempted": bool(
                    stage.get("luxor_backend_connection_raw_send_hook_attempted")
                ),
                "luxor_backend_connection_raw_send_hook_installed": bool(
                    stage.get("luxor_backend_connection_raw_send_hook_installed")
                ),
                "luxor_backend_connection_raw_send_hook_target": stage.get(
                    "luxor_backend_connection_raw_send_hook_target"
                ),
                "luxor_backend_connection_raw_send_calls": stage.get(
                    "luxor_backend_connection_raw_send_calls"
                ),
                "luxor_backend_connection_raw_send_magic_calls": stage.get(
                    "luxor_backend_connection_raw_send_magic_calls"
                ),
                "luxor_backend_connection_raw_send_last_caller_rva": stage.get(
                    "luxor_backend_connection_raw_send_last_caller_rva"
                ),
                "luxor_backend_connection_raw_send_last_connection": stage.get(
                    "luxor_backend_connection_raw_send_last_connection"
                ),
                "luxor_backend_connection_raw_send_last_connection_vtable": stage.get(
                    "luxor_backend_connection_raw_send_last_connection_vtable"
                ),
                "luxor_backend_connection_raw_send_last_packet_data": stage.get(
                    "luxor_backend_connection_raw_send_last_packet_data"
                ),
                "luxor_backend_connection_raw_send_last_packet_size": stage.get(
                    "luxor_backend_connection_raw_send_last_packet_size"
                ),
                "luxor_backend_connection_raw_send_last_magic": stage.get(
                    "luxor_backend_connection_raw_send_last_magic"
                ),
                "luxor_backend_connection_raw_send_last_payload_opcode": stage.get(
                    "luxor_backend_connection_raw_send_last_payload_opcode"
                ),
                "luxor_backend_connection_raw_send_last_payload_byte1": stage.get(
                    "luxor_backend_connection_raw_send_last_payload_byte1"
                ),
                "luxor_backend_connection_raw_send_last_payload_byte2": stage.get(
                    "luxor_backend_connection_raw_send_last_payload_byte2"
                ),
                "luxor_backend_connection_raw_send_last_payload_size": stage.get(
                    "luxor_backend_connection_raw_send_last_payload_size"
                ),
                "luxor_backend_connection_raw_send_last_marker": stage.get(
                    "luxor_backend_connection_raw_send_last_marker"
                ),
                "luxor_backend_connection_raw_send_last_route_selector": stage.get(
                    "luxor_backend_connection_raw_send_last_route_selector"
                ),
                "luxor_backend_connection_raw_send_last_route_channel": stage.get(
                    "luxor_backend_connection_raw_send_last_route_channel"
                ),
                "luxor_backend_connection_raw_send_last_route_channel_vtable": stage.get(
                    "luxor_backend_connection_raw_send_last_route_channel_vtable"
                ),
                "luxor_backend_connection_raw_send_last_route_channel_target": stage.get(
                    "luxor_backend_connection_raw_send_last_route_channel_target"
                ),
                "luxor_backend_connection_raw_send_last_lower_transport": stage.get(
                    "luxor_backend_connection_raw_send_last_lower_transport"
                ),
                "luxor_backend_connection_raw_send_last_lower_transport_vtable": stage.get(
                    "luxor_backend_connection_raw_send_last_lower_transport_vtable"
                ),
                "luxor_backend_connection_raw_send_last_lower_ready_target": stage.get(
                    "luxor_backend_connection_raw_send_last_lower_ready_target"
                ),
                "luxor_backend_connection_raw_send_last_lower_sender": stage.get(
                    "luxor_backend_connection_raw_send_last_lower_sender"
                ),
                "luxor_backend_connection_raw_send_last_lower_sender_vtable": stage.get(
                    "luxor_backend_connection_raw_send_last_lower_sender_vtable"
                ),
                "luxor_backend_connection_raw_send_last_lower_sender_send_target": stage.get(
                    "luxor_backend_connection_raw_send_last_lower_sender_send_target"
                ),
                "luxor_backend_packet_stream_receive_calls": stage.get(
                    "luxor_backend_packet_stream_receive_calls"
                ),
                "luxor_backend_packet_stream_receive_last_caller_rva": stage.get(
                    "luxor_backend_packet_stream_receive_last_caller_rva"
                ),
                "luxor_backend_packet_stream_receive_last_connection": stage.get(
                    "luxor_backend_packet_stream_receive_last_connection"
                ),
                "luxor_backend_packet_stream_receive_last_packet_size": stage.get(
                    "luxor_backend_packet_stream_receive_last_packet_size"
                ),
                "luxor_backend_packet_stream_receive_last_magic": stage.get(
                    "luxor_backend_packet_stream_receive_last_magic"
                ),
                "luxor_backend_packet_stream_receive_last_payload_opcode": stage.get(
                    "luxor_backend_packet_stream_receive_last_payload_opcode"
                ),
                "luxor_backend_packet_stream_receive_last_route_selector": stage.get(
                    "luxor_backend_packet_stream_receive_last_route_selector"
                ),
                "luxor_backend_packet_stream_receive_last_route_channel": stage.get(
                    "luxor_backend_packet_stream_receive_last_route_channel"
                ),
                "luxor_lower_transport_send_if_ready_hook_attempted": bool(
                    stage.get("luxor_lower_transport_send_if_ready_hook_attempted")
                ),
                "luxor_lower_transport_send_if_ready_hook_installed": bool(
                    stage.get("luxor_lower_transport_send_if_ready_hook_installed")
                ),
                "luxor_lower_transport_send_if_ready_hook_target": stage.get(
                    "luxor_lower_transport_send_if_ready_hook_target"
                ),
                "luxor_lower_transport_send_if_ready_calls": stage.get(
                    "luxor_lower_transport_send_if_ready_calls"
                ),
                "luxor_lower_transport_send_if_ready_last_caller_rva": stage.get(
                    "luxor_lower_transport_send_if_ready_last_caller_rva"
                ),
                "luxor_lower_transport_send_if_ready_last_transport": stage.get(
                    "luxor_lower_transport_send_if_ready_last_transport"
                ),
                "luxor_lower_transport_send_if_ready_last_transport_vtable": stage.get(
                    "luxor_lower_transport_send_if_ready_last_transport_vtable"
                ),
                "luxor_lower_transport_send_if_ready_last_ready_target": stage.get(
                    "luxor_lower_transport_send_if_ready_last_ready_target"
                ),
                "luxor_lower_transport_send_if_ready_last_sender": stage.get(
                    "luxor_lower_transport_send_if_ready_last_sender"
                ),
                "luxor_lower_transport_send_if_ready_last_sender_vtable": stage.get(
                    "luxor_lower_transport_send_if_ready_last_sender_vtable"
                ),
                "luxor_lower_transport_send_if_ready_last_sender_send_target": stage.get(
                    "luxor_lower_transport_send_if_ready_last_sender_send_target"
                ),
                "luxor_lower_transport_send_if_ready_last_result": stage.get(
                    "luxor_lower_transport_send_if_ready_last_result"
                ),
                "luxor_lower_transport_send_if_ready_last_magic": stage.get(
                    "luxor_lower_transport_send_if_ready_last_magic"
                ),
                "luxor_lower_transport_send_if_ready_last_payload_opcode": stage.get(
                    "luxor_lower_transport_send_if_ready_last_payload_opcode"
                ),
                "luxor_lower_sender_send_hook_attempted": bool(
                    stage.get("luxor_lower_sender_send_hook_attempted")
                ),
                "luxor_lower_sender_send_hook_installed": bool(
                    stage.get("luxor_lower_sender_send_hook_installed")
                ),
                "luxor_lower_sender_send_hook_target": stage.get(
                    "luxor_lower_sender_send_hook_target"
                ),
                "luxor_lower_sender_send_calls": stage.get(
                    "luxor_lower_sender_send_calls"
                ),
                "luxor_lower_sender_send_magic_calls": stage.get(
                    "luxor_lower_sender_send_magic_calls"
                ),
                "luxor_lower_sender_send_last_caller_rva": stage.get(
                    "luxor_lower_sender_send_last_caller_rva"
                ),
                "luxor_lower_sender_send_last_sender": stage.get(
                    "luxor_lower_sender_send_last_sender"
                ),
                "luxor_lower_sender_send_last_sender_vtable": stage.get(
                    "luxor_lower_sender_send_last_sender_vtable"
                ),
                "luxor_lower_sender_send_last_packet_data": stage.get(
                    "luxor_lower_sender_send_last_packet_data"
                ),
                "luxor_lower_sender_send_last_packet_size": stage.get(
                    "luxor_lower_sender_send_last_packet_size"
                ),
                "luxor_lower_sender_send_last_result_out": stage.get(
                    "luxor_lower_sender_send_last_result_out"
                ),
                "luxor_lower_sender_send_last_route_identity": stage.get(
                    "luxor_lower_sender_send_last_route_identity"
                ),
                "luxor_lower_sender_send_last_result": stage.get(
                    "luxor_lower_sender_send_last_result"
                ),
                "luxor_lower_sender_send_last_magic": stage.get(
                    "luxor_lower_sender_send_last_magic"
                ),
                "luxor_lower_sender_send_last_payload_opcode": stage.get(
                    "luxor_lower_sender_send_last_payload_opcode"
                ),
                "luxor_lower_sender_send_last_payload_byte1": stage.get(
                    "luxor_lower_sender_send_last_payload_byte1"
                ),
                "luxor_lower_sender_send_last_payload_byte2": stage.get(
                    "luxor_lower_sender_send_last_payload_byte2"
                ),
                "luxor_route_channel_append_hook_attempted": bool(
                    stage.get("luxor_route_channel_append_hook_attempted")
                ),
                "luxor_route_channel_append_hook_installed": bool(
                    stage.get("luxor_route_channel_append_hook_installed")
                ),
                "luxor_route_channel_append_hook_target": stage.get(
                    "luxor_route_channel_append_hook_target"
                ),
                "luxor_route_channel_append_calls": stage.get(
                    "luxor_route_channel_append_calls"
                ),
                "luxor_route_channel_append_opcode21_calls": stage.get(
                    "luxor_route_channel_append_opcode21_calls"
                ),
                "luxor_route_channel_append_last_caller_rva": stage.get(
                    "luxor_route_channel_append_last_caller_rva"
                ),
                "luxor_route_channel_append_last_channel": stage.get(
                    "luxor_route_channel_append_last_channel"
                ),
                "luxor_route_channel_append_last_channel_vtable": stage.get(
                    "luxor_route_channel_append_last_channel_vtable"
                ),
                "luxor_route_channel_append_last_packet_data": stage.get(
                    "luxor_route_channel_append_last_packet_data"
                ),
                "luxor_route_channel_append_last_packet_size": stage.get(
                    "luxor_route_channel_append_last_packet_size"
                ),
                "luxor_route_channel_append_last_magic": stage.get(
                    "luxor_route_channel_append_last_magic"
                ),
                "luxor_route_channel_append_last_payload_opcode": stage.get(
                    "luxor_route_channel_append_last_payload_opcode"
                ),
                "luxor_route_channel_append_last_payload_size": stage.get(
                    "luxor_route_channel_append_last_payload_size"
                ),
                "luxor_route_channel_append_last_capacity": stage.get(
                    "luxor_route_channel_append_last_capacity"
                ),
                "luxor_route_channel_append_last_used_before": stage.get(
                    "luxor_route_channel_append_last_used_before"
                ),
                "luxor_route_channel_append_last_used_after": stage.get(
                    "luxor_route_channel_append_last_used_after"
                ),
                "luxor_route_channel_append_last_identity_object": stage.get(
                    "luxor_route_channel_append_last_identity_object"
                ),
                "luxor_route_channel_append_last_identity_ref": stage.get(
                    "luxor_route_channel_append_last_identity_ref"
                ),
                "luxor_route_channel_append_last_result": stage.get(
                    "luxor_route_channel_append_last_result"
                ),
                "luxor_route_dispatch_drain_hook_attempted": bool(
                    stage.get("luxor_route_dispatch_drain_hook_attempted")
                ),
                "luxor_route_dispatch_drain_hook_installed": bool(
                    stage.get("luxor_route_dispatch_drain_hook_installed")
                ),
                "luxor_route_dispatch_drain_hook_target": stage.get(
                    "luxor_route_dispatch_drain_hook_target"
                ),
                "luxor_route_dispatch_drain_calls": stage.get(
                    "luxor_route_dispatch_drain_calls"
                ),
                "luxor_route_dispatch_drain_last_caller_rva": stage.get(
                    "luxor_route_dispatch_drain_last_caller_rva"
                ),
                "luxor_route_dispatch_drain_last_connection": stage.get(
                    "luxor_route_dispatch_drain_last_connection"
                ),
                "luxor_route_dispatch_drain_last_selector_before": stage.get(
                    "luxor_route_dispatch_drain_last_selector_before"
                ),
                "luxor_route_dispatch_drain_last_selector_after": stage.get(
                    "luxor_route_dispatch_drain_last_selector_after"
                ),
                "luxor_route_dispatch_drain_last_pending_before": stage.get(
                    "luxor_route_dispatch_drain_last_pending_before"
                ),
                "luxor_route_dispatch_drain_last_pending_after": stage.get(
                    "luxor_route_dispatch_drain_last_pending_after"
                ),
                "luxor_route_dispatch_drain_last_channel": stage.get(
                    "luxor_route_dispatch_drain_last_channel"
                ),
                "luxor_route_dispatch_drain_last_channel_used_before": stage.get(
                    "luxor_route_dispatch_drain_last_channel_used_before"
                ),
                "luxor_route_dispatch_drain_last_channel_used_after": stage.get(
                    "luxor_route_dispatch_drain_last_channel_used_after"
                ),
                "connect_sender_send_hook_attempted": bool(
                    stage.get("connect_sender_send_hook_attempted")
                ),
                "connect_sender_send_hook_installed": bool(
                    stage.get("connect_sender_send_hook_installed")
                ),
                "connect_sender_send_calls": stage.get(
                    "connect_sender_send_calls"
                ),
                "connect_sender_send_last_caller_rva": stage.get(
                    "connect_sender_send_last_caller_rva"
                ),
                "connect_sender_send_last_sender": stage.get(
                    "connect_sender_send_last_sender"
                ),
                "connect_sender_send_last_sender_vtable": stage.get(
                    "connect_sender_send_last_sender_vtable"
                ),
                "connect_sender_send_last_packet": stage.get(
                    "connect_sender_send_last_packet"
                ),
                "connect_sender_send_last_packet_cursor": stage.get(
                    "connect_sender_send_last_packet_cursor"
                ),
                "connect_sender_send_last_packet_mode": stage.get(
                    "connect_sender_send_last_packet_mode"
                ),
                "connect_sender_send_last_packet_byte0": stage.get(
                    "connect_sender_send_last_packet_byte0"
                ),
                "connect_sender_send_last_packet_byte1": stage.get(
                    "connect_sender_send_last_packet_byte1"
                ),
                "connect_sender_send_last_packet_byte2": stage.get(
                    "connect_sender_send_last_packet_byte2"
                ),
                "connect_sender_send_last_packet_byte3": stage.get(
                    "connect_sender_send_last_packet_byte3"
                ),
                "connect_sender_send_last_arg2": stage.get(
                    "connect_sender_send_last_arg2"
                ),
                "connect_sender_send_last_arg3": stage.get(
                    "connect_sender_send_last_arg3"
                ),
                "connect_sender_send_last_result": stage.get(
                    "connect_sender_send_last_result"
                ),
                "connect_sender_send_last_active": stage.get(
                    "connect_sender_send_last_active"
                ),
                "connect_sender_send_last_active_state": stage.get(
                    "connect_sender_send_last_active_state"
                ),
                "connect_sender_send_last_active_sub_state": stage.get(
                    "connect_sender_send_last_active_sub_state"
                ),
                "connect_sender_send_last_transport_tick": stage.get(
                    "connect_sender_send_last_transport_tick"
                ),
                "connect_sender_send_last_transport_status": stage.get(
                    "connect_sender_send_last_transport_status"
                ),
                "connect_sender_send_last_transport_ready": stage.get(
                    "connect_sender_send_last_transport_ready"
                ),
                "connect_sender_send_last_transport_is_host": stage.get(
                    "connect_sender_send_last_transport_is_host"
                ),
                "connect_sender_send_last_transport_channel_count": stage.get(
                    "connect_sender_send_last_transport_channel_count"
                ),
                "connect_sender_send_last_transport_channel_capacity": (
                    stage.get(
                        "connect_sender_send_last_transport_channel_capacity"
                    )
                ),
                "active_packet_dispatch_hook_attempted": bool(
                    stage.get("active_packet_dispatch_hook_attempted")
                ),
                "active_packet_dispatch_hook_installed": bool(
                    stage.get("active_packet_dispatch_hook_installed")
                ),
                "active_packet_dispatch_calls": stage.get(
                    "active_packet_dispatch_calls"
                ),
                "active_packet_dispatch_opcode0_calls": stage.get(
                    "active_packet_dispatch_opcode0_calls"
                ),
                "active_packet_dispatch_opcode4_calls": stage.get(
                    "active_packet_dispatch_opcode4_calls"
                ),
                "active_packet_dispatch_opcode5_calls": stage.get(
                    "active_packet_dispatch_opcode5_calls"
                ),
                "active_packet_dispatch_opcode6_calls": stage.get(
                    "active_packet_dispatch_opcode6_calls"
                ),
                "active_packet_dispatch_opcode9_calls": stage.get(
                    "active_packet_dispatch_opcode9_calls"
                ),
                "active_packet_dispatch_opcode10_calls": stage.get(
                    "active_packet_dispatch_opcode10_calls"
                ),
                "active_packet_dispatch_opcode11_calls": stage.get(
                    "active_packet_dispatch_opcode11_calls"
                ),
                "active_packet_dispatch_opcode15_calls": stage.get(
                    "active_packet_dispatch_opcode15_calls"
                ),
                "active_packet_dispatch_opcode20_calls": stage.get(
                    "active_packet_dispatch_opcode20_calls"
                ),
                "active_packet_dispatch_opcode21_calls": stage.get(
                    "active_packet_dispatch_opcode21_calls"
                ),
                "active_packet_dispatch_last_active": stage.get(
                    "active_packet_dispatch_last_active"
                ),
                "active_packet_dispatch_last_packet": stage.get(
                    "active_packet_dispatch_last_packet"
                ),
                "active_packet_dispatch_last_context": stage.get(
                    "active_packet_dispatch_last_context"
                ),
                "active_packet_dispatch_last_opcode": stage.get(
                    "active_packet_dispatch_last_opcode"
                ),
                "active_packet_dispatch_last_state_before": stage.get(
                    "active_packet_dispatch_last_state_before"
                ),
                "active_packet_dispatch_last_state_after": stage.get(
                    "active_packet_dispatch_last_state_after"
                ),
                "active_packet_dispatch_last_ready_before": stage.get(
                    "active_packet_dispatch_last_ready_before"
                ),
                "active_packet_dispatch_last_ready_after": stage.get(
                    "active_packet_dispatch_last_ready_after"
                ),
                "transport_open_message_hook_attempted": bool(
                    stage.get("transport_open_message_hook_attempted")
                ),
                "transport_open_message_hook_installed": bool(
                    stage.get("transport_open_message_hook_installed")
                ),
                "transport_open_message_calls": stage.get(
                    "transport_open_message_calls"
                ),
                "transport_open_message_last_active": stage.get(
                    "transport_open_message_last_active"
                ),
                "transport_open_message_last_packet": stage.get(
                    "transport_open_message_last_packet"
                ),
                "transport_open_message_last_state_before": stage.get(
                    "transport_open_message_last_state_before"
                ),
                "transport_open_message_last_state_after": stage.get(
                    "transport_open_message_last_state_after"
                ),
                "transport_open_message_last_ready_before": stage.get(
                    "transport_open_message_last_ready_before"
                ),
                "transport_open_message_last_ready_after": stage.get(
                    "transport_open_message_last_ready_after"
                ),
                "transport_open_response_hook_attempted": bool(
                    stage.get("transport_open_response_hook_attempted")
                ),
                "transport_open_response_hook_installed": bool(
                    stage.get("transport_open_response_hook_installed")
                ),
                "transport_open_response_calls": stage.get(
                    "transport_open_response_calls"
                ),
                "transport_open_response_last_active": stage.get(
                    "transport_open_response_last_active"
                ),
                "transport_open_response_last_packet": stage.get(
                    "transport_open_response_last_packet"
                ),
                "transport_open_response_last_state_before": stage.get(
                    "transport_open_response_last_state_before"
                ),
                "transport_open_response_last_state_after": stage.get(
                    "transport_open_response_last_state_after"
                ),
                "transport_open_response_last_ready_before": stage.get(
                    "transport_open_response_last_ready_before"
                ),
                "transport_open_response_last_ready_after": stage.get(
                    "transport_open_response_last_ready_after"
                ),
                "ready_open_compat_attempted": bool(
                    stage.get("ready_open_compat_attempted")
                ),
                "ready_open_compat_call_ok": bool(
                    stage.get("ready_open_compat_call_ok")
                ),
                "ready_open_compat_count": stage.get(
                    "ready_open_compat_count"
                ),
                "ready_open_compat_last_tick": stage.get(
                    "ready_open_compat_last_tick"
                ),
                "ready_open_compat_open_calls_before": stage.get(
                    "ready_open_compat_open_calls_before"
                ),
                "ready_open_compat_open_calls_after": stage.get(
                    "ready_open_compat_open_calls_after"
                ),
                "ready_open_compat_can_send_before": stage.get(
                    "ready_open_compat_can_send_before"
                ),
                "ready_open_compat_can_send_after": stage.get(
                    "ready_open_compat_can_send_after"
                ),
                "ready_open_compat_peer_writer": stage.get(
                    "ready_open_compat_peer_writer"
                ),
                "ready_open_compat_peer_route_tag": stage.get(
                    "ready_open_compat_peer_route_tag"
                ),
                "ready_open_compat_peer_registry_index": stage.get(
                    "ready_open_compat_peer_registry_index"
                ),
                "ready_open_compat_parent_sample_ok": stage.get(
                    "ready_open_compat_parent_sample_ok"
                ),
                "ready_open_compat_parent_state": stage.get(
                    "ready_open_compat_parent_state"
                ),
                "ready_open_compat_parent_ready_flags": stage.get(
                    "ready_open_compat_parent_ready_flags"
                ),
                "ready_open_compat_parent_ready_flag_set": stage.get(
                    "ready_open_compat_parent_ready_flag_set"
                ),
                "ready_open_compat_parent_ready_state_ok": stage.get(
                    "ready_open_compat_parent_ready_state_ok"
                ),
                "ready_open_compat_parent_ready": stage.get(
                    "ready_open_compat_parent_ready"
                ),
                "ready_open_compat_parent_state_target": stage.get(
                    "ready_open_compat_parent_state_target"
                ),
                "ready_open_compat_small_route_sample_ok": stage.get(
                    "ready_open_compat_small_route_sample_ok"
                ),
                "ready_open_compat_small_route_next_available": stage.get(
                    "ready_open_compat_small_route_next_available"
                ),
                "ready_open_compat_small_route_next_slot_present": stage.get(
                    "ready_open_compat_small_route_next_slot_present"
                ),
                "ready_open_compat_small_route_count": stage.get(
                    "ready_open_compat_small_route_count"
                ),
                "ready_open_compat_small_route_limit": stage.get(
                    "ready_open_compat_small_route_limit"
                ),
                "ready_open_compat_small_route_sequence_counter": stage.get(
                    "ready_open_compat_small_route_sequence_counter"
                ),
                "ready_open_compat_small_route_next_sequence": stage.get(
                    "ready_open_compat_small_route_next_sequence"
                ),
                "ready_open_compat_small_route_collision_index": stage.get(
                    "ready_open_compat_small_route_collision_index"
                ),
                "ready_open_compat_mark_ready_attempted": bool(
                    stage.get("ready_open_compat_mark_ready_attempted")
                ),
                "ready_open_compat_mark_ready_call_ok": bool(
                    stage.get("ready_open_compat_mark_ready_call_ok")
                ),
                "ready_open_compat_mark_ready_before": stage.get(
                    "ready_open_compat_mark_ready_before"
                ),
                "ready_open_compat_mark_ready_after": stage.get(
                    "ready_open_compat_mark_ready_after"
                ),
                "ready_open_compat_mark_ready_failure": stage.get(
                    "ready_open_compat_mark_ready_failure", ""
                ),
                "ready_open_compat_failure": stage.get(
                    "ready_open_compat_failure", ""
                ),
                "online_session_slot_kind": stage.get(
                    "online_session_find_sessions_slot_kind", ""
                ),
                "online_session_slot_is_steam": bool(
                    stage.get("online_session_find_sessions_slot_is_steam")
                ),
                "explicit_steam_online_session_slot_kind": stage.get(
                    "explicit_steam_online_session_find_sessions_slot_kind",
                    "",
                ),
                "explicit_steam_online_session_slot_is_steam": bool(
                    stage.get(
                        "explicit_steam_online_session_find_sessions_slot_is_steam"
                    )
                ),
                "explicit_steam_online_session_probe_ok": bool(
                    stage.get("explicit_steam_online_session_probe_ok")
                ),
                "steam_lobby_probe_target_visible": bool(
                    stage.get("steam_lobby_probe_target_visible")
                ),
                "steam_lobby_matrix_full_count": stage.get(
                    "steam_lobby_matrix_full_count"
                ),
                "steam_lobby_target_request_data_called": bool(
                    stage.get("steam_lobby_target_request_data_called")
                ),
                "steam_lobby_target_request_data_ok": bool(
                    stage.get("steam_lobby_target_request_data_ok")
                ),
                "steam_lobby_target_required_metadata_count": stage.get(
                    "steam_lobby_target_required_metadata_count"
                ),
                "steam_lobby_target_build_id_matches_local": bool(
                    stage.get("steam_lobby_target_build_id_matches_local")
                ),
                "steam_lobby_conversion_calls": stage.get(
                    "steam_lobby_conversion_calls"
                ),
                "steam_lobby_conversion_accepts": stage.get(
                    "steam_lobby_conversion_accepts"
                ),
                "steam_lobby_data_callback_calls": stage.get(
                    "steam_lobby_data_callback_calls"
                ),
                "steam_lobby_data_callback_matching_pair_calls": stage.get(
                    "steam_lobby_data_callback_matching_pair_calls"
                ),
                "steam_presence_search_calls": stage.get(
                    "steam_presence_search_calls"
                ),
                "steam_presence_search_state2_calls": stage.get(
                    "steam_presence_search_state2_calls"
                ),
                "steam_presence_search_state3_calls": stage.get(
                    "steam_presence_search_state3_calls"
                ),
                "steam_presence_search_state2_to3": stage.get(
                    "steam_presence_search_state2_to3"
                ),
                "steam_presence_search_state2_to4": stage.get(
                    "steam_presence_search_state2_to4"
                ),
                "steam_presence_search_state3_to4": stage.get(
                    "steam_presence_search_state3_to4"
                ),
                "steam_presence_search_state3_timeout_fail": stage.get(
                    "steam_presence_search_state3_timeout_fail"
                ),
                "steam_presence_search_last_api_lobby_count": stage.get(
                    "steam_presence_search_last_api_lobby_count"
                ),
                "steam_presence_search_max_api_lobby_count": stage.get(
                    "steam_presence_search_max_api_lobby_count"
                ),
                "steam_presence_search_max_collected_lobby_count": stage.get(
                    "steam_presence_search_max_collected_lobby_count"
                ),
                "steam_presence_search_last_lobby_count": stage.get(
                    "steam_presence_search_last_lobby_count"
                ),
                "steam_filter_hook_installed": bool(
                    stage.get("steam_filter_hook_installed")
                ),
                "steam_filter_request_lobby_list_calls": stage.get(
                    "steam_filter_request_lobby_list_calls"
                ),
                "steam_filter_string_calls": stage.get(
                    "steam_filter_string_calls"
                ),
                "steam_filter_numerical_calls": stage.get(
                    "steam_filter_numerical_calls"
                ),
                "steam_filter_distance_calls": stage.get(
                    "steam_filter_distance_calls"
                ),
                "steam_filter_result_count_calls": stage.get(
                    "steam_filter_result_count_calls"
                ),
                "steam_filter_sample": stage.get(
                    "steam_filter_sample", ""
                ),
                "play_side_request_ok": bool(
                    stage.get("play_side_request_ok")
                ),
                "battle_sync_ready_to_connect_requested": bool(
                    stage.get("battle_sync_ready_to_connect_requested")
                ),
                "battle_sync_ready_to_connect_ok": bool(
                    stage.get("battle_sync_ready_to_connect_ok")
                ),
                "battle_sync_ready_to_connect_failure": stage.get(
                    "battle_sync_ready_to_connect_failure", ""
                ),
                "match_setting_sync": stage.get("match_setting_sync", ""),
                "match_setting_sync_name": stage.get(
                    "match_setting_sync_name", ""
                ),
                "match_setting_sync_class": stage.get(
                    "match_setting_sync_class", ""
                ),
                "match_setting_sync_requested": bool(
                    stage.get("match_setting_sync_requested")
                ),
                "match_setting_sync_initialize_ok": bool(
                    stage.get("match_setting_sync_initialize_ok")
                ),
                "match_setting_sync_ready_to_connect_ok": bool(
                    stage.get("match_setting_sync_ready_to_connect_ok")
                ),
                "match_setting_sync_failure": stage.get(
                    "match_setting_sync_failure", ""
                ),
                "match_setting_sync_is_connected_query_ok": bool(
                    stage.get("match_setting_sync_is_connected_query_ok")
                ),
                "match_setting_sync_is_connected": bool(
                    stage.get("match_setting_sync_is_connected")
                ),
                "match_setting_sync_is_completed_query_ok": bool(
                    stage.get("match_setting_sync_is_completed_query_ok")
                ),
                "match_setting_sync_is_completed": bool(
                    stage.get("match_setting_sync_is_completed")
                ),
                "match_setting_sync_raw_sampled": bool(
                    stage.get("match_setting_sync_raw_sampled")
                ),
                "match_setting_sync_state_read_ok": bool(
                    stage.get("match_setting_sync_state_read_ok")
                ),
                "match_setting_sync_state": stage.get(
                    "match_setting_sync_state"
                ),
                "match_setting_sync_connected_raw": bool(
                    stage.get("match_setting_sync_connected_raw")
                ),
                "match_setting_sync_character_complete_read_ok": bool(
                    stage.get(
                        "match_setting_sync_character_complete_read_ok"
                    )
                ),
                "match_setting_sync_character_complete": bool(
                    stage.get("match_setting_sync_character_complete")
                ),
                "match_setting_sync_stage_complete_read_ok": bool(
                    stage.get("match_setting_sync_stage_complete_read_ok")
                ),
                "match_setting_sync_stage_complete": bool(
                    stage.get("match_setting_sync_stage_complete")
                ),
                "match_setting_sync_completed_raw": bool(
                    stage.get("match_setting_sync_completed_raw")
                ),
                "ready_request_ok": bool(stage.get("ready_request_ok")),
                "ready_request_type": stage.get("ready_request_type"),
                "ready_request_skipped_by_role": bool(
                    stage.get("ready_request_skipped_by_role")
                ),
                "ready_request_failure": stage.get(
                    "ready_request_failure", ""
                ),
                "lux_match_lobby_request_ready_requested": bool(
                    stage.get("lux_match_lobby_request_ready_requested")
                ),
                "lux_match_lobby_request_ready_ok": bool(
                    stage.get("lux_match_lobby_request_ready_ok")
                ),
                "lux_match_lobby_request_ready_failure": stage.get(
                    "lux_match_lobby_request_ready_failure", ""
                ),
                "start_latch_ok": bool(stage.get("start_latch_ok")),
                "start_latch_method": stage.get("start_latch_method", ""),
                "start_latch_failure": stage.get(
                    "start_latch_failure", ""
                ),
                "manual_launch_checked": bool(
                    stage.get("manual_launch_checked")
                ),
                "manual_launch_requested": bool(
                    stage.get("manual_launch_requested")
                ),
                "manual_launch_request_ok": bool(
                    stage.get("manual_launch_request_ok")
                ),
                "manual_launch_failure": stage.get(
                    "manual_launch_failure", ""
                ),
                "can_launch_ready": bool(stage.get("can_launch_ready")),
                "has_battle_request_pending": bool(
                    stage.get("has_battle_request_pending")
                ),
                "ready_preconditions_ok": bool(
                    stage.get("ready_preconditions_ok")
                ),
                "ready_precondition_failure": stage.get(
                    "ready_precondition_failure", ""
                ),
                "ready_connect_system": stage.get("ready_connect_system"),
                "ready_connect_vtable": stage.get("ready_connect_vtable"),
                "ready_connect_get_channel_fn": stage.get(
                    "ready_connect_get_channel_fn"
                ),
                "ready_connect_state_fn": stage.get(
                    "ready_connect_state_fn"
                ),
                "ready_connect_channel": stage.get("ready_connect_channel"),
                "ready_channel_vtable": stage.get("ready_channel_vtable"),
                "ready_channel_can_send_fn": stage.get(
                    "ready_channel_can_send_fn"
                ),
                "ready_channel_state_fn": stage.get(
                    "ready_channel_state_fn"
                ),
                "ready_connect_sender": stage.get("ready_connect_sender"),
                "ready_sender_vtable": stage.get("ready_sender_vtable"),
                "ready_session_connection_registry": stage.get(
                    "ready_session_connection_registry"
                ),
                "ready_session_connection_registry_vtable": stage.get(
                    "ready_session_connection_registry_vtable"
                ),
                "ready_session_connection_registry_attach_fn": stage.get(
                    "ready_session_connection_registry_attach_fn"
                ),
                "ready_session_connection_registry_lookup_fn": stage.get(
                    "ready_session_connection_registry_lookup_fn"
                ),
                "ready_session_connection_registry_step80_fn": stage.get(
                    "ready_session_connection_registry_step80_fn"
                ),
                "ready_session_connection_registry_stepd0_fn": stage.get(
                    "ready_session_connection_registry_stepd0_fn"
                ),
                "ready_session_connection_registry_stepd8_fn": stage.get(
                    "ready_session_connection_registry_stepd8_fn"
                ),
                "ready_channel_qword_08": stage.get(
                    "ready_channel_qword_08"
                ),
                "ready_channel_field_20": stage.get(
                    "ready_channel_field_20"
                ),
                "ready_channel_raw_48": stage.get("ready_channel_raw_48"),
                "ready_channel_byte_49": stage.get(
                    "ready_channel_byte_49"
                ),
                "ready_channel_byte_4a": stage.get(
                    "ready_channel_byte_4a"
                ),
                "ready_channel_byte_4b": stage.get(
                    "ready_channel_byte_4b"
                ),
                "ready_channel_can_send_raw_4c": stage.get(
                    "ready_channel_can_send_raw_4c"
                ),
                "ready_channel_byte_4d": stage.get(
                    "ready_channel_byte_4d"
                ),
                "ready_channel_byte_4e": stage.get(
                    "ready_channel_byte_4e"
                ),
                "ready_channel_byte_4f": stage.get(
                    "ready_channel_byte_4f"
                ),
                "ready_channel_qword_58": stage.get(
                    "ready_channel_qword_58"
                ),
                "ready_channel_qword_70": stage.get(
                    "ready_channel_qword_70"
                ),
                "ready_channel_qword_88": stage.get(
                    "ready_channel_qword_88"
                ),
                "ready_channel_qword_90": stage.get(
                    "ready_channel_qword_90"
                ),
                "ready_connect_state": stage.get("ready_connect_state"),
                "ready_channel_state": stage.get("ready_channel_state"),
                "ready_channel_can_send": bool(
                    stage.get("ready_channel_can_send")
                ),
                "main_user_id_override": stage.get("main_user_id_override"),
                "session_main_user_id": stage.get("session_main_user_id"),
                "session_main_user_id_overridden": bool(
                    stage.get("session_main_user_id_overridden")
                ),
                "failure": stage.get("failure", ""),
                "missing": result.get("missing", []),
            }
        )
    return out


def replay_input_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    out: list[dict[str, Any]] = []
    for result in report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root", {})
        replay = result.get("replay_input_script") or {}
        out.append(
            {
                "role": root.get("role", ""),
                "request_id": result.get("request_id"),
                "trace_file": replay.get("_trace_file"),
                "ok": bool(replay.get("ok")),
                "replay_input_file": replay.get("replay_input_file", ""),
                "replay_file_hash": replay.get("replay_file_hash", 0),
                "local_replay_player": replay.get("local_replay_player"),
                "remote_replay_player": replay.get("remote_replay_player"),
                "input_frames_p0": replay.get("input_frames_p0", 0),
                "input_frames_p1": replay.get("input_frames_p1", 0),
                "input_latched": bool(replay.get("input_latched")),
                "input_injected": bool(replay.get("input_injected")),
                "input_complete": bool(replay.get("input_complete")),
                "divergence_frame": replay.get("divergence_frame"),
                "divergence_window": replay.get("divergence_window"),
                "failure": replay.get("failure", ""),
                "missing": result.get("missing", []),
            }
        )
    return out


def direct_replay_input_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    out: list[dict[str, Any]] = []
    for result in report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root", {})
        replay = result.get("direct_replay_input") or {}
        out.append(
            {
                "role": root.get("role", ""),
                "request_id": result.get("request_id"),
                "trace_file": replay.get("_trace_file"),
                "ok": bool(replay.get("ok")),
                "replay_file_hash": replay.get("replay_file_hash"),
                "local_replay_player": replay.get("local_replay_player"),
                "remote_replay_player": replay.get("remote_replay_player"),
                "local_input_hash": replay.get("local_input_hash"),
                "expected_remote_input_hash": replay.get(
                    "expected_remote_input_hash"
                ),
                "frame_count": replay.get("frame_count"),
                "input_latched": bool(replay.get("input_latched")),
                "input_injected": bool(replay.get("input_injected")),
                "input_complete": bool(replay.get("input_complete")),
                "direct_cache_write_complete": bool(
                    replay.get("direct_cache_write_complete")
                ),
                "direct_cache_writes_local": replay.get(
                    "direct_cache_writes_local"
                ),
                "direct_cache_writes_remote": replay.get(
                    "direct_cache_writes_remote"
                ),
                "direct_input_log": replay.get("direct_input_log"),
                "failure": replay.get("failure", ""),
            }
        )
    return out


def soak_summary(step: dict[str, Any]) -> list[dict[str, Any]]:
    report = step.get("report_json")
    if not isinstance(report, dict):
        return []
    out: list[dict[str, Any]] = []
    for result in report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root", {})
        summary = result.get("soak_summary") or {}
        sidecar = result.get("sidecar_handshake") or {}
        stock = result.get("stock_online") or {}
        out.append(
            {
                "role": root.get("role", ""),
                "request_id": result.get("request_id"),
                "trace_file": (
                    (stock or {}).get("_trace_file")
                    or (sidecar or {}).get("_trace_file")
                ),
                "ok": bool(result.get("ok")),
                "live_online_events": summary.get("live_online_events", 0),
                "sidecar_handshake_events": summary.get(
                    "sidecar_handshake_events", 0),
                "session_pointers": summary.get("session_pointers", []),
                "unexpected_correction_events": summary.get(
                    "unexpected_correction_events", 0),
                "disarm_events": summary.get("disarm_events", 0),
                "missing": result.get("missing", []),
            }
        )
    return out


def write_report(report: dict[str, Any]) -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    path = REPORT_DIR / f"rollback_two_client_acceptance_{report['run_id']}.json"
    path.write_text(
        json.dumps(report, indent=2, sort_keys=True),
        encoding="utf-8",
        newline="\n",
    )
    return path


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run rollback two-client tests. This is the single "
            "operator-facing rollback test command; the phase runner is "
            "internal."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--run-id", default="")
    parser.add_argument(
        "--mode",
        choices=["replay-fork-lab", "direct-connect", "mirrored-versus", "steam-online"],
        default="replay-fork-lab",
        help=(
            "replay-fork-lab is the automated rollback-core gate; "
            "direct-connect is its deprecated alias for one transition cycle; "
            "mirrored-versus is the attach-only Local VS production path; "
            "steam-online keeps the older SC6 matchmaking diagnostic ladder."
        ),
    )
    parser.add_argument("--sandbox-root", type=Path, default=Path(r"C:\Sandbox"))
    parser.add_argument("--sandbox-box", default=two_client.DEFAULT_SANDBOX_BOX)
    parser.add_argument("--rollback-window", type=int, default=60)
    parser.add_argument("--seed", default="0x5C6B0001")
    parser.add_argument("--left-character", default=None)
    parser.add_argument("--right-character", default=None)
    parser.add_argument("--stage", default=None)
    parser.add_argument(
        "--native-input-source-slot",
        type=int,
        choices=[0, 1],
        default=0,
    )
    parser.add_argument("--input-delay", type=int, default=1)
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
        "--expected-build-id", type=lambda value: int(value, 0), default=0
    )
    parser.add_argument(
        "--expected-schema-id", type=lambda value: int(value, 0), default=0
    )
    parser.add_argument(
        "--host-sidecar-port",
        type=int,
        default=two_client.HOST_HORSE_UDP_PORT,
    )
    parser.add_argument(
        "--sandbox-sidecar-port",
        type=int,
        default=two_client.SANDBOX_HORSE_UDP_PORT,
    )
    parser.add_argument("--activation-token", default="")
    parser.add_argument("--role-manifest-watch-seconds", type=float, default=20.0)
    parser.add_argument("--sidecar-watch-seconds", type=float, default=40.0)
    parser.add_argument("--fault-watch-seconds", type=float, default=20.0)
    parser.add_argument("--direct-stage-watch-seconds", type=float, default=60.0)
    parser.add_argument("--direct-connect-watch-seconds", type=float, default=60.0)
    parser.add_argument("--direct-replay-input-watch-seconds", type=float, default=120.0)
    parser.add_argument("--direct-correction-watch-seconds", type=float, default=180.0)
    parser.add_argument("--direct-release-watch-seconds", type=float, default=180.0)
    parser.add_argument("--online-stage-watch-seconds", type=float, default=180.0)
    parser.add_argument("--observe-gameflow-watch-seconds", type=float, default=180.0)
    parser.add_argument("--stock-online-watch-seconds", type=float, default=120.0)
    parser.add_argument("--activation-watch-seconds", type=float, default=120.0)
    parser.add_argument("--live-replay-input-watch-seconds", type=float, default=120.0)
    parser.add_argument("--live-correction-watch-seconds", type=float, default=180.0)
    parser.add_argument("--soak-watch-seconds", type=float, default=120.0)
    parser.add_argument("--replay-fork-attach-only", action="store_true")
    parser.add_argument("--replay-fork-fresh-launches", type=int, default=1)
    parser.add_argument("--online-stage-session-name", default="")
    parser.add_argument("--online-stage-room-name", default="")
    parser.add_argument("--online-stage-native-session-name", default="")
    parser.add_argument(
        "--online-stage-target-owner-id",
        type=lambda value: int(value, 0),
        default=0,
        help=(
            "Steam owner ID to target in SC6/Steam search results without "
            "forcing a named native room."
        ),
    )
    parser.add_argument(
        "--online-stage-main-user-id-override",
        type=int,
        default=None,
        help=(
            "Diagnostic override applied to both clients; omitted keeps "
            "SC6's reported main user id."
        ),
    )
    parser.add_argument(
        "--host-online-stage-main-user-id-override",
        type=int,
        default=None,
        help="Diagnostic main-user-id override for the host client only.",
    )
    parser.add_argument(
        "--sandbox-online-stage-main-user-id-override",
        type=int,
        default=None,
        help="Diagnostic main-user-id override for the Sandboxie client only.",
    )
    parser.add_argument("--debug-steam-probe", action="store_true")
    parser.add_argument(
        "--online-stage-no-presence-find",
        action="store_true",
        help=(
            "After standard SC6 find fails/empties, run the native "
            "FindSessions path with PRESENCESEARCH disabled. This does not "
            "install the Steam filter-hook probe."
        ),
    )
    parser.add_argument(
        "--stock-join-route",
        choices=["browser", "invite-fallback"],
        default="browser",
        help=(
            "Keep browser discovery preferred and enable the authenticated "
            "Steam invite bridge only after discovery is exhausted."
        ),
    )
    parser.add_argument(
        "--online-stage-diagnostic-reflection",
        action="store_true",
        help=(
            "Diagnostic-only: enable fragile reflected menu/scene commands "
            "inside the C++ online-stage harness. Release gates leave this off."
        ),
    )
    parser.add_argument(
        "--online-stage-host-room-ready-gate",
        action="store_true",
        help=(
            "Legacy diagnostic: hold sandbox search behind the Python "
            "host-room-ready marker gate. Disabled by default for the native "
            "local-two-client flow."
        ),
    )
    parser.add_argument(
        "--online-stage-fail-fast-empty-finds",
        action="store_true",
        help=(
            "Diagnostic: stop online-stage/direct-release early after "
            "confirmed host create and repeated empty sandbox find results."
        ),
    )
    parser.add_argument(
        "--online-stage-fail-fast-empty-find-attempts",
        type=int,
        default=3,
    )
    parser.add_argument(
        "--debug-steam-filter-probe",
        action="store_true",
        help=(
            "Experimental: capture Steam matchmaking vtable filter calls. "
            "Kept separate from --debug-steam-probe because this probe is "
            "diagnostic-only and not part of release acceptance."
        ),
    )
    parser.add_argument(
        "--debug-direct-stage-begin-play",
        action="store_true",
        help=(
            "Experimental direct-stage diagnostic: forwards the debug-only "
            "native ALuxBattleManager BeginPlay spawn-pipeline probe. "
            "Disabled by default for release acceptance."
        ),
    )
    parser.add_argument(
        "--online-stage-network-check-compat",
        action="store_true",
        help=(
            "Focused local-two-client compatibility: let the Sandboxie "
            "client pass SC6's native Windows network-adapter check while "
            "recording the original result."
        ),
    )
    parser.add_argument(
        "--online-stage-join-complete-compat",
        action="store_true",
        help=(
            "Focused local-two-client compatibility: after sandbox "
            "OnJoinSession success with named-session evidence, call SC6's "
            "Luxor join-complete handler. Reports using this flag are "
            "compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--online-stage-transport-ready-compat",
        action="store_true",
        help=(
            "Focused local-two-client compatibility: after sandbox join "
            "success and native session-connect false, call SC6's native "
            "Luxor transport mark-ready helper. Reports using this flag are "
            "compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--online-stage-ready-open-compat",
        action="store_true",
        help=(
            "Focused local-two-client compatibility: after host native "
            "OnSessionConnectComplete and ready-channel evidence, call SC6's "
            "native ready-channel open helper once. Reports using this flag "
            "are compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--online-stage-peer-route-tag-fix",
        action="store_true",
        help=(
            "Focused active-routing fix: when queued opcode 21 copies the "
            "replacement route tag but the native peer writer registry already "
            "has a nonreplacement peer tag, send the packet with that peer tag."
        ),
    )
    parser.add_argument(
        "--online-stage-in-room-transition-compat",
        action="store_true",
        help=(
            "Focused diagnostic: after native create/join evidence, request "
            "PlayerMatchLobbyScene InRoomInit. Reports using this flag are "
            "compat-assisted, not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--online-stage-direct-native-join-diagnostic",
        action="store_true",
        help=(
            "Focused diagnostic: bypass the PlayerMatch UI decision and call "
            "ULuxorSessionHub.JoinSession directly. Reports using this flag "
            "are diagnostic, not native manual-flow MVP evidence."
        ),
    )
    parser.add_argument(
        "--skip-online-stage-drive",
        action="store_true",
        help=(
            "Manual-attach diagnostic: during direct-release, do not drive "
            "online menu/create/join; validate the already-running match."
        ),
    )
    parser.add_argument(
        "--no-online-stage-cleanup",
        dest="cleanup_online_stage",
        action="store_false",
        default=True,
    )
    parser.add_argument(
        "--replay-input-file",
        type=Path,
        default=two_client.DEFAULT_REPLAY_INPUT_FILE,
    )
    parser.add_argument(
        "--main-menu-player-match-route",
        default=two_client.DEFAULT_MAIN_MENU_PLAYER_MATCH_ROUTE,
        help=(
            "Comma-separated UIGameFlowAutomation input route from "
            "MainMenuScene_C to Player Match."
        ),
    )
    parser.add_argument("--replay-divergence-frame", type=int, default=120)
    parser.add_argument("--replay-divergence-window", type=int, default=12)
    parser.add_argument("--phases", default="")
    parser.add_argument("--inventory-only", action="store_true")
    parser.add_argument("--preflight-only", action="store_true")
    parser.add_argument("--online-stage-only", action="store_true")
    parser.add_argument(
        "--observe-gameflow",
        action="store_true",
        help=(
            "Passive manual-flow recorder. Arms trace only, waits for both "
            "clients to reach the real online battle scene, and writes a native "
            "gameflow timeline."
        ),
    )
    parser.add_argument(
        "--capture-gameflow",
        action="store_true",
        help=(
            "With --observe-gameflow, record the native/manual scene timeline "
            "without requiring the online battle scene. Diagnostic capture "
            "only; full rollback acceptance is unchanged."
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
        "--diagnostic-online-stage",
        action="store_true",
        help=(
            "Fast bundled online-stage diagnostic: runs only online-stage, "
            "enables the stable Steam/SC6 query matrix, fail-fasts empty "
            "finds, and keeps cleanup enabled."
        ),
    )
    parser.add_argument(
        "--cleanup-only",
        action="store_true",
        help=(
            "Run the public cleanup path only: destroy staged online "
            "sessions, disable sidecars, and verify Horse UDP ports are clear."
        ),
    )
    parser.add_argument("--continue-on-fail", action="store_true")
    parser.add_argument("--clean-start", action="store_true")
    parser.add_argument("--wait-for-two-sc6-seconds", type=float, default=0.0)
    parser.add_argument("--strict", dest="strict", action="store_true", default=True)
    parser.add_argument("--no-strict", dest="strict", action="store_false")
    args = parser.parse_args()
    if args.mode in {"replay-fork-lab", "direct-connect"}:
        alias = args.mode == "direct-connect"
        command = [
            sys.executable,
            str(two_client.REPO / "tools" / "rollback_replay_fork_test_run.py"),
            "--mode", args.mode,
            "--run-id", args.run_id or now_id(),
            "--sandbox-root", str(args.sandbox_root),
            "--sandbox-box", args.sandbox_box,
            "--replay", str(args.replay_input_file),
            "--fresh-launches", str(args.replay_fork_fresh_launches),
            "--soak-seconds", str(args.soak_watch_seconds),
            "--rollback-window", str(max(args.rollback_window, 1)),
            "--input-delay", str(args.input_delay),
            "--fault-seed", hex(args.fault_seed),
            "--host-port", str(args.host_sidecar_port),
            "--sandbox-port", str(args.sandbox_sidecar_port),
        ]
        if args.expected_build_id:
            command.extend(["--expected-build-id", hex(args.expected_build_id)])
        if args.expected_schema_id:
            command.extend(["--expected-schema-id", hex(args.expected_schema_id)])
        if args.replay_fork_attach_only:
            command.append("--attach-only")
        if alias:
            print("warning: --mode direct-connect is deprecated; forwarding "
                  "to replay-fork-lab", file=sys.stderr)
        return subprocess.call(command, cwd=str(two_client.REPO))
    if args.fault_profile != "clean_0ms" and args.mode != "mirrored-versus":
        parser.error(
            "non-clean --fault-profile requires --mode mirrored-versus; "
            "the direct-connect and steam-online lanes do not use the "
            "production UDP impairment scheduler"
        )

    if args.mode == "steam-online" and any(
        value is not None
        for value in (args.left_character, args.right_character, args.stage)
    ):
        parser.error(
            "character/stage selection is unsupported for --mode steam-online"
        )
    try:
        args.resolved_left_character = (
            resolve_character(args.left_character)
            if args.left_character is not None else None
        )
        args.resolved_right_character = (
            resolve_character(args.right_character)
            if args.right_character is not None else None
        )
        args.resolved_stage = (
            resolve_stage(args.stage) if args.stage is not None else None
        )
    except LaunchSelectionError as exc:
        parser.error(str(exc))

    if (args.online_stage_join_complete_compat
            or args.online_stage_transport_ready_compat):
        parser.error(
            "manual join-complete and transport-ready compatibility options "
            "are unsupported unsafe experiments")
    if args.mode == "mirrored-versus" and (
        args.expected_build_id == 0 or args.expected_schema_id == 0
    ):
        parser.error(
            "--mode mirrored-versus requires nonzero --expected-build-id "
            "and --expected-schema-id"
        )
    if args.stock_join_route == "invite-fallback":
        if args.mode != "steam-online":
            parser.error(
                "--stock-join-route invite-fallback requires --mode steam-online"
            )
        if args.expected_build_id == 0 or args.expected_schema_id == 0:
            parser.error(
                "invite-fallback requires nonzero --expected-build-id and "
                "--expected-schema-id"
            )
        args.online_stage_no_presence_find = True

    if args.online_stage_only or args.diagnostic_online_stage:
        args.mode = "steam-online"
    if args.diagnostic_online_stage:
        args.debug_steam_probe = True
        args.online_stage_fail_fast_empty_finds = True
        if args.online_stage_watch_seconds == parser.get_default(
            "online_stage_watch_seconds"
        ):
            args.online_stage_watch_seconds = 75.0
    if args.debug_steam_filter_probe:
        args.debug_steam_probe = True

    try:
        phases = selected_phases(args)
    except ValueError as exc:
        print(f"error={exc}", file=sys.stderr)
        return 2

    run_id = args.run_id or now_id()
    activation_token = args.activation_token or f"rollback-two-client-{run_id}"
    steps: list[dict[str, Any]] = []
    failed_step = ""

    if args.clean_start:
        for step in (
            kill_sc6(timeout=30),
            cleanup_request_files(args.sandbox_root, args.sandbox_box),
        ):
            steps.append(step)
            if not step.get("ok") and not failed_step:
                failed_step = str(step.get("name") or "clean-start")
        if not failed_step and args.wait_for_two_sc6_seconds > 0:
            step = wait_for_two_sc6(args.wait_for_two_sc6_seconds)
            steps.append(step)
            if not step.get("ok"):
                failed_step = "wait-for-two-sc6"

    health_baseline = acceptance_health_snapshot("baseline", args)
    expected_sc6_pids = {
        two_client.int_value(pid, -1)
        for pid in health_baseline.get("current_sc6_pids", [])
        if two_client.int_value(pid, -1) >= 0
    }
    if len(expected_sc6_pids) != 2:
        expected_sc6_pids = set()
    expected_role_pids = {
        str(role): two_client.int_value(pid, -1)
        for role, pid in (
            health_baseline.get("current_role_pids") or {}
        ).items()
        if two_client.int_value(pid, -1) >= 0
    }
    if set(expected_role_pids) != {"host", "sandbox"}:
        expected_role_pids = {}

    if (not args.cleanup_only) and (not failed_step or args.continue_on_fail):
        for phase in phases:
            watch_seconds = watch_seconds_for_phase(args, phase)
            step = run_phase(
                args=args,
                phase=phase,
                watch_seconds=watch_seconds,
                run_id=run_id,
                activation_token=activation_token,
            )
            attach_post_step_health(
                step,
                args,
                expected_pids=expected_sc6_pids,
                expected_role_pids=expected_role_pids,
            )
            steps.append(step)
            if not step.get("ok") and not failed_step:
                failed_step = f"phase-{phase}"
                if not args.continue_on_fail:
                    break

    cleanup_sidecar_attempted = False
    cleanup_online_stage_attempted = False
    if args.cleanup_only and (not failed_step or args.continue_on_fail):
        cleanup_steps, cleanup_online_stage_attempted, cleanup_sidecar_attempted = (
            run_public_cleanup(
                args=args,
                run_id=run_id,
                activation_token=activation_token,
            )
        )
        for cleanup_step in cleanup_steps:
            attach_post_step_health(
                cleanup_step,
                args,
                expected_pids=expected_sc6_pids,
                expected_role_pids=expected_role_pids,
            )
            steps.append(cleanup_step)
            if not cleanup_step.get("ok") and not failed_step:
                failed_step = str(cleanup_step.get("name") or "cleanup")
    elif not args.cleanup_only:
        cleanup_allowed = not failed_step or args.continue_on_fail
        cleanup_sidecar_attempted = should_cleanup_sidecar(steps)
        cleanup_online_stage_attempted = (
            cleanup_allowed and args.cleanup_online_stage
            and any(
                step.get("phase") in {"online-stage", *ONLINE_STAGE_GATE_PHASES}
                for step in steps
            )
        )
        cleanup_sidecar_attempted = (
            cleanup_allowed and cleanup_sidecar_attempted
        )
    if (not args.cleanup_only) and cleanup_online_stage_attempted:
        cleanup_step = run_online_stage_cleanup(
            args=args,
            run_id=run_id,
            activation_token=activation_token,
        )
        attach_post_step_health(
            cleanup_step,
            args,
            expected_pids=expected_sc6_pids,
            expected_role_pids=expected_role_pids,
        )
        steps.append(cleanup_step)
        if not cleanup_step.get("ok") and not failed_step:
            failed_step = "cleanup-online-stage"

    if (not args.cleanup_only) and cleanup_sidecar_attempted:
        cleanup_step = run_disabled_sidecar_cleanup(
            args=args,
            run_id=run_id,
            activation_token=activation_token,
        )
        attach_post_step_health(
            cleanup_step,
            args,
            expected_pids=expected_sc6_pids,
            expected_role_pids=expected_role_pids,
        )
        steps.append(cleanup_step)
        if not cleanup_step.get("ok") and not failed_step:
            failed_step = "cleanup-disable-sidecar"

    live_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "live-correction"
        ),
        {},
    )
    direct_stage_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "direct-stage"
        ),
        {},
    )
    direct_connect_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "direct-connect"
        ),
        {},
    )
    direct_replay_input_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "direct-replay-input"
        ),
        {},
    )
    direct_correction_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "direct-correction"
        ),
        {},
    )
    direct_release_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") in {"direct-release", "rollback-proof"}
        ),
        {},
    )
    if not direct_stage_step:
        direct_stage_step = direct_release_step
    if not direct_connect_step:
        direct_connect_step = direct_release_step
    if not direct_replay_input_step:
        direct_replay_input_step = direct_release_step
    if not direct_correction_step:
        direct_correction_step = direct_release_step
    online_stage_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "online-stage"
        ),
        {},
    )
    replay_input_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "live-replay-input"
        ),
        {},
    )
    soak_step = next(
        (
            step for step in reversed(steps)
            if step.get("phase") == "soak"
        ),
        {},
    )
    health_final = acceptance_health_snapshot(
        "final",
        args,
        expected_pids=expected_sc6_pids,
        expected_role_pids=expected_role_pids,
    )
    if not health_final["ok"] and not failed_step:
        failed_step = "post-run-health"
    ok = not failed_step
    direct_release_report = direct_release_step.get("report_json", {})
    if not isinstance(direct_release_report, dict):
        direct_release_report = {}
    packet_timeline = direct_release_report.get("packet_timeline") or (
        two_client.packet_timeline_from_phase_report(direct_release_report)
        if direct_release_report else {}
    )
    packet_timeline_summary = (
        direct_release_report.get("packet_timeline_summary")
        or two_client.summarize_packet_timeline_results(packet_timeline)
    )
    gate_phase_labels = {
        "inventory": "launch",
        "horse-udp-ready": "horse-udp-ready",
        "mirrored-versus-setup": "mirrored-setup",
        "mirrored-versus-battle": "battle",
        "menu-ready": "menu-ready",
        "player-match-nav": "nav",
        "player-match-lobby": "lobby",
        "player-match-battle": "battle",
        "rollback-proof": "proof",
        "direct-release": "proof",
    }
    gate_timeline_parts: list[str] = []
    seen_gate_labels: set[str] = set()
    for step in steps:
        phase = str(step.get("phase", ""))
        label = gate_phase_labels.get(phase)
        if not label or label in seen_gate_labels:
            continue
        seen_gate_labels.add(label)
        gate_timeline_parts.append(
            f"{label}:{'PASS' if step.get('ok') else 'FAIL'}"
        )
    cleanup_ok = True
    cleanup_seen = False
    for step in steps:
        phase = str(step.get("phase", ""))
        name = str(step.get("name", ""))
        if phase.endswith("-cleanup") or name.startswith("cleanup"):
            cleanup_seen = True
            cleanup_ok = cleanup_ok and bool(step.get("ok"))
    expected_gate_labels = (
        (
            "launch",
            "horse-udp-ready",
            "mirrored-setup",
            "battle",
            "proof",
        )
        if args.mode == "mirrored-versus"
        else ("launch", "menu-ready", "nav", "lobby", "battle", "proof")
    )
    for label in expected_gate_labels:
        if label not in seen_gate_labels:
            gate_timeline_parts.append(f"{label}:SKIP")
            seen_gate_labels.add(label)
    gate_timeline_parts.append(
        f"cleanup:{'PASS' if cleanup_ok else 'FAIL'}" if cleanup_seen
        else "cleanup:SKIP"
    )
    gate_timeline = " ".join(gate_timeline_parts)

    observed_phases = [
        str(step.get("phase", "")) for step in steps if step.get("phase")
    ]
    required_phases = (
        list(STEAM_ONLINE_PHASES)
        if args.mode == "steam-online" else
        list(MIRRORED_VERSUS_PHASES)
        if args.mode == "mirrored-versus" else
        list(DIRECT_CONNECT_PHASES)
    )
    coverage_result = coverage(required_phases, observed_phases)
    contract = contract_fields(
        workflow_kind="two-client-acceptance",
        workflow_ok=ok,
        coverage_result=coverage_result,
        acceptance_workflow=True,
    )
    report = {
        **contract,
        "run_id": run_id,
        "mode": args.mode,
        "stock_join_route": args.stock_join_route,
        "generated_at": utc_now(),
        "strict": args.strict,
        "cleanup_only": args.cleanup_only,
        "phases": phases,
        "failed_step": failed_step,
        "repo": str(two_client.REPO),
        "game_exe": str(two_client.GAME_EXE),
        "operator_runner": str(Path(__file__).resolve()),
        "internal_phase_runner": str(
            two_client.REPO / "tools" / "rollback_two_client_test_run.py"
        ),
        "sandbox_root": str(args.sandbox_root),
        "sandbox_box": args.sandbox_box,
        "steam_udp_port": two_client.STEAM_UDP_PORT,
        "host_sidecar_port": args.host_sidecar_port,
        "sandbox_sidecar_port": args.sandbox_sidecar_port,
        "lifecycle_mode": (
            "mirrored-versus"
            if args.mode == "mirrored-versus" else "stock-online-pvp"
        ),
        "native_input_source_slot": args.native_input_source_slot,
        "launch_selection": {
            "left": (
                vars(args.resolved_left_character)
                if args.resolved_left_character else None
            ),
            "right": (
                vars(args.resolved_right_character)
                if args.resolved_right_character else None
            ),
            "stage": (
                vars(args.resolved_stage) if args.resolved_stage else None
            ),
        },
        "input_delay": args.input_delay,
        "fault_profile": args.fault_profile,
        "fault_seed": f"0x{args.fault_seed:X}",
        "expected_build_id": f"0x{args.expected_build_id:X}",
        "expected_schema_id": f"0x{args.expected_schema_id:X}",
        "activation_token_hash": two_client.fnv1a64(activation_token),
        "diagnostic_online_stage": args.diagnostic_online_stage,
        "debug_steam_probe": args.debug_steam_probe,
        "debug_steam_filter_probe": args.debug_steam_filter_probe,
        "observe_gameflow": args.observe_gameflow,
        "capture_gameflow": args.capture_gameflow,
        "observe_gameflow_process_events": args.observe_gameflow_process_events,
        "online_stage_network_check_compat": (
            args.online_stage_network_check_compat
        ),
        "online_stage_join_complete_compat": (
            args.online_stage_join_complete_compat
        ),
        "online_stage_transport_ready_compat": (
            args.online_stage_transport_ready_compat
        ),
        "online_stage_ready_open_compat": args.online_stage_ready_open_compat,
        "online_stage_peer_route_tag_fix": (
            args.online_stage_peer_route_tag_fix
        ),
        "online_stage_in_room_transition_compat": (
            args.online_stage_in_room_transition_compat
        ),
        "online_stage_direct_native_join_diagnostic": (
            args.online_stage_direct_native_join_diagnostic
        ),
        "online_stage_no_presence_find": args.online_stage_no_presence_find,
        "online_stage_diagnostic_reflection": (
            args.online_stage_diagnostic_reflection
        ),
        "skip_online_stage_drive": args.skip_online_stage_drive,
        "online_stage_host_room_ready_gate": (
            args.online_stage_host_room_ready_gate
        ),
        "online_stage_fail_fast_empty_finds": (
            args.online_stage_fail_fast_empty_finds
        ),
        "online_stage_fail_fast_empty_find_attempts": (
            args.online_stage_fail_fast_empty_find_attempts
        ),
        "online_stage_native_session_name": args.online_stage_native_session_name,
        "online_stage_main_user_id_override": (
            args.online_stage_main_user_id_override
        ),
        "host_online_stage_main_user_id_override": (
            args.host_online_stage_main_user_id_override
        ),
        "sandbox_online_stage_main_user_id_override": (
            args.sandbox_online_stage_main_user_id_override
        ),
        "online_stage_session_name": args.online_stage_session_name,
        "online_stage_room_name": args.online_stage_room_name,
        "online_stage_target_owner_id": args.online_stage_target_owner_id,
        "replay_input_file": str(args.replay_input_file),
        "replay_input_file_hash": two_client.file_fnv1a64(args.replay_input_file),
        "main_menu_player_match_route": args.main_menu_player_match_route,
        "replay_divergence_frame": args.replay_divergence_frame,
        "replay_divergence_window": args.replay_divergence_window,
        "cleanup_sidecar_attempted": cleanup_sidecar_attempted,
        "cleanup_online_stage_attempted": cleanup_online_stage_attempted,
        "health_baseline": health_baseline,
        "health_final": health_final,
        "direct_stage_summary": direct_stage_summary(direct_stage_step),
        "direct_connect_summary": direct_connect_summary(direct_connect_step),
        "direct_replay_input_summary": direct_replay_input_summary(
            direct_replay_input_step
        ),
        "direct_correction_summary": direct_correction_summary(
            direct_correction_step
        ),
        "direct_release_summary": direct_release_report.get("results", []),
        "packet_timeline": packet_timeline,
        "packet_timeline_summary": packet_timeline_summary,
        "gate_timeline": gate_timeline,
        "online_stage_summary": online_stage_summary(online_stage_step),
        "live_replay_input_summary": replay_input_summary(replay_input_step),
        "live_correction_replay_input_summary": replay_input_summary(live_step),
        "live_correction_summary": correction_summary(live_step),
        "soak_summary": soak_summary(soak_step),
        "steps": steps,
    }
    report_path = write_report(report)

    print(f"rollback two-client acceptance {contract['verdict'].upper()}")
    print(f"report={report_path}")
    print(f"mode={args.mode}")
    print(f"steam_udp_port={two_client.STEAM_UDP_PORT}")
    print(f"host_sidecar_port={args.host_sidecar_port}")
    print(f"sandbox_sidecar_port={args.sandbox_sidecar_port}")
    print(
        "phases="
        + ",".join(f"{step.get('phase')}:{'PASS' if step.get('ok') else 'FAIL'}"
                   for step in steps if step.get("phase"))
    )
    print(f"gate-timeline {gate_timeline}")
    if failed_step:
        print(f"failed_step={failed_step}", file=sys.stderr)
    packet_summary = report.get("packet_timeline_summary") or {}
    if packet_summary:
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
        for failure in step.get("health_failures", []):
            print(
                f"health_failure step={step.get('name')} {failure}",
                file=sys.stderr,
            )
    for failure in report.get("health_final", {}).get("failures", []):
        print(f"health_final_failure {failure}", file=sys.stderr)
    if report["direct_stage_summary"]:
        for item in report["direct_stage_summary"]:
            print(
                "direct-stage "
                f"role={item.get('role')} "
                f"ok={1 if item.get('ok') else 0} "
                f"battle={1 if item.get('battle_context_ready') else 0} "
                f"spawn={1 if item.get('battle_manager_spawn_ok') else 0} "
                f"bm_begin={1 if item.get('battle_manager_begin_play_ok') else 0} "
                f"p1live={1 if item.get('chara_p1_context_live') else 0} "
                f"p2live={1 if item.get('chara_p2_context_live') else 0} "
                f"bmreal={1 if item.get('battle_manager_object_real') else 0} "
                f"rule={1 if item.get('battle_rule_finite') else 0}:"
                f"{item.get('battle_rule_type')}/"
                f"{item.get('battle_rule_time')}:"
                f"{item.get('battle_rule_source')} "
                f"context={item.get('battle_context_failure')} "
                f"failure={item.get('failure')}"
            )
    if report["direct_connect_summary"]:
        for item in report["direct_connect_summary"]:
            print(
                "direct-connect "
                f"role={item.get('role')} "
                f"ok={1 if item.get('ok') else 0} "
                f"validated={1 if item.get('validated_direct_input') else 0} "
                f"frames={item.get('remote_frame_count')} "
                f"failure={item.get('failure')}"
            )
    if report["direct_replay_input_summary"]:
        for item in report["direct_replay_input_summary"]:
            print(
                "direct-replay-input "
                f"role={item.get('role')} "
                f"ok={1 if item.get('ok') else 0} "
                f"complete={1 if item.get('input_complete') else 0} "
                f"frames={item.get('frame_count')} "
                f"failure={item.get('failure')}"
            )
    if report["direct_correction_summary"]:
        for item in report["direct_correction_summary"]:
            print(
                "direct-correction "
                f"role={item.get('role')} "
                f"ok={1 if item.get('ok') else 0} "
                f"depth={item.get('correction_depth')} "
                f"hash={item.get('corrected_hash')} "
                f"explicit={1 if item.get('explicit_match') else 0} "
                f"hgcpu={1 if item.get('hgcpu_policy_match') else 0} "
                f"frame={1 if item.get('frame_counter_match') else 0} "
                f"restore={1 if item.get('post_baseline_restore_ok') else 0} "
                f"restore_fault={1 if item.get('post_baseline_restore_hgcpu_faulted') else 0} "
                f"restore_rip={item.get('post_baseline_restore_hgcpu_exception_rip')} "
                f"failure={item.get('failure')}"
            )
    if report["direct_release_summary"]:
        for item in report["direct_release_summary"]:
            online = item.get("online_stage") or {}
            stage = item.get("direct_stage") or {}
            replay = item.get("direct_replay_input") or {}
            correction = item.get("direct_correction") or {}
            summary = item.get("direct_release_summary") or {}
            print(
                "direct-release "
                f"role={item.get('root', {}).get('role')} "
                f"ok={1 if item.get('ok') else 0} "
                f"in_room={1 if online.get('player_match_in_room_ok') else 0}:"
                f"{online.get('player_match_in_room_state')} "
                f"in_room_conn={1 if online.get('player_match_in_room_session_connecting') else 0} "
                f"host_marker={1 if online.get('host_room_ready_marker_observed') else 0}:"
                f"{online.get('host_room_ready_marker_failure')} "
                f"scene={stage.get('current_scene_class')} "
                f"pm_req={online.get('player_match_scene_request_attempts')}:"
                f"{online.get('player_match_scene_last_request_nav_attempt')}:"
                f"{1 if online.get('player_match_scene_last_request_ok') else 0} "
                f"menu_input={online.get('main_menu_input_attempts')}:"
                f"{online.get('main_menu_input_sequence_step')}:"
                f"{1 if online.get('main_menu_input_last_ok') else 0}:"
                f"{online.get('main_menu_input_last_key')} "
                f"menu_nav={online.get('main_menu_nav_last_action')}:"
                f"{1 if online.get('main_menu_nav_last_action_accepted') else 0}:"
                f"{1 if online.get('main_menu_nav_last_action_transitioned') else 0}:"
                f"{online.get('main_menu_nav_cooldown_remaining')} "
                f"rule_type={stage.get('battle_rule_type')} "
                f"time={stage.get('battle_rule_time')} "
                f"endless={1 if stage.get('battle_setup_endless') else 0} "
                f"refreshes={replay.get('direct_cache_refreshes')} "
                f"correction={1 if correction.get('ok') else 0} "
                f"native_launches={summary.get('native_launch_events')} "
                f"asset_requests={summary.get('battle_asset_request_events')} "
                f"active_state={online.get('luxor_active_connect_state')} "
                f"active_sub={online.get('luxor_active_connect_sub_state')} "
                f"active_system_off={online.get('luxor_active_connect_system_offset')} "
                f"state_task={online.get('luxor_active_session_state_update_task')} "
                f"notify_task={online.get('luxor_active_session_notify_task')} "
                f"session_event={online.get('luxor_active_session_event_handle')} "
                f"delegate_array={online.get('luxor_connect_delegate_handle_array')} "
                f"join_delegate={online.get('luxor_delegate_join_session_complete_handle')} "
                f"deferred_delegate={online.get('luxor_delegate_deferred_session_connection_handle')} "
                f"pe_followups={online.get('process_event_followup_count')} "
                f"pe_kind={online.get('process_event_followup_last_kind')} "
                f"member_join={online.get('session_member_join_count')} "
                f"member_attempt={online.get('session_member_join_attempted_count')} "
                f"member_first={online.get('session_member_join_first_tick')} "
                f"transport_tick={online.get('luxor_active_transport_tick')} "
                f"transport_status={online.get('luxor_active_transport_status_code')} "
                f"transport_ready_state={online.get('luxor_active_transport_ready_state')} "
                f"transport_is_host={online.get('luxor_active_transport_is_host')} "
                f"transport_channels={online.get('luxor_active_transport_channel_count')}/"
                f"{online.get('luxor_active_transport_channel_capacity')} "
                f"transport_ready={1 if online.get('luxor_active_transport_ready') else 0} "
                f"sessq={online.get('luxor_session_async_queue_count')}:"
                f"{online.get('luxor_session_async_queue_first_callback_rva')}/"
                f"{online.get('luxor_session_async_queue_tail_callback_rva')} "
                f"join_force={online.get('join_complete_compat_count')} "
                f"join_call={1 if online.get('join_complete_compat_call_ok') else 0} "
                f"join_before={online.get('join_complete_compat_active_state_before')} "
                f"join_after={online.get('join_complete_compat_active_state_after')} "
                f"join_conn_after={online.get('join_complete_compat_session_connection_after')} "
                f"join_failure={online.get('join_complete_compat_failure')} "
                f"trans_force={online.get('transport_ready_compat_count')} "
                f"trans_trigger={online.get('transport_ready_compat_trigger_reason')} "
                f"trans_call={1 if online.get('transport_ready_compat_call_ok') else 0} "
                f"trans_before={online.get('transport_ready_compat_before_ready_state')} "
                f"trans_after={online.get('transport_ready_compat_after_ready_state')} "
                f"trans_deferred={1 if online.get('transport_ready_compat_deferred_called_after_force') else 0} "
                f"trans_conn_after={online.get('transport_ready_compat_session_connection_after_deferred')} "
                f"trans_failure={online.get('transport_ready_compat_failure')} "
                f"open_calls={online.get('ready_channel_open_calls')} "
                f"open_caller={online.get('ready_channel_open_last_caller_rva')} "
                f"open_can={online.get('ready_channel_open_last_can_send_before')}->{online.get('ready_channel_open_last_can_send_after')} "
                f"ready_open={online.get('ready_open_compat_count')} "
                f"ready_open_ok={1 if online.get('ready_open_compat_call_ok') else 0} "
                f"ready_open_can={online.get('ready_open_compat_can_send_before')}->{online.get('ready_open_compat_can_send_after')} "
                f"ready_open_parent={online.get('ready_open_compat_parent_sample_ok')}:"
                f"{online.get('ready_open_compat_parent_state')}/"
                f"{online.get('ready_open_compat_parent_ready_flags')}:"
                f"{online.get('ready_open_compat_parent_ready_state_ok')}/"
                f"{online.get('ready_open_compat_parent_ready_flag_set')}:"
                f"ready={online.get('ready_open_compat_parent_ready')} "
                f"ready_open_sr={online.get('ready_open_compat_small_route_sample_ok')}:"
                f"{online.get('ready_open_compat_small_route_next_available')}/"
                f"{online.get('ready_open_compat_small_route_next_slot_present')}:"
                f"seq={online.get('ready_open_compat_small_route_sequence_counter')}->"
                f"{online.get('ready_open_compat_small_route_next_sequence')}:"
                f"n={online.get('ready_open_compat_small_route_count')}/"
                f"{online.get('ready_open_compat_small_route_limit')}:"
                f"hit={online.get('ready_open_compat_small_route_collision_index')} "
                f"ready_open_mark={online.get('ready_open_compat_mark_ready_before')}->{online.get('ready_open_compat_mark_ready_after')} "
                f"ready_open_mark_ok={1 if online.get('ready_open_compat_mark_ready_call_ok') else 0} "
                f"ready_open_failure={online.get('ready_open_compat_failure')} "
                f"mark_calls={online.get('transport_ready_mark_calls')} "
                f"mark_ready={online.get('transport_ready_mark_last_ready_before')}->{online.get('transport_ready_mark_last_ready_after')} "
                f"reg_steps={online.get('ready_registry_step80_calls')}/{online.get('ready_registry_stepd0_calls')}/{online.get('ready_registry_stepd8_calls')} "
                f"qsend={online.get('queued_opcode_send_calls')}:"
                f"{online.get('queued_opcode_send_last_outer_opcode')}/"
                f"{online.get('queued_opcode_send_last_inner_opcode')}:"
                f"ch={online.get('queued_opcode_send_last_channel_id')}:"
                f"{online.get('queued_opcode_send_last_result')}:"
                f"tag={online.get('queued_opcode_send_last_source_routing_tag')} "
                f"rtfix={1 if online.get('peer_route_tag_fix_enabled') else 0}:"
                f"{online.get('peer_route_tag_fix_attempts')}/"
                f"{online.get('peer_route_tag_fix_applied')}:"
                f"orig={online.get('peer_route_tag_fix_last_original_tag')}:"
                f"peer={online.get('peer_route_tag_fix_last_replacement_tag')}:"
                f"src={online.get('peer_route_tag_fix_last_peer_source')}:"
                f"verify={online.get('peer_route_tag_fix_last_write_verified')}/"
                f"{online.get('peer_route_tag_fix_last_verified_tag')}:"
                f"res={online.get('peer_route_tag_fix_last_result')} "
                f"qclone={online.get('queued_work_item_clone_calls')}:"
                f"{online.get('queued_work_item_clone_last_source_inner_opcode')}:"
                f"ch={online.get('queued_work_item_clone_last_source_channel_id')}:"
                f"tag={online.get('queued_work_item_clone_last_source_routing_tag')} "
                f"op6send={online.get('active_opcode6_send_calls')}:"
                f"{online.get('active_opcode6_send_last_state_payload')}:"
                f"{online.get('active_opcode6_send_last_result')} "
                f"eget={online.get('active_sender_endpoint_get_calls')}:"
                f"{online.get('active_sender_endpoint_get_last_local_user')}:"
                f"{online.get('active_sender_endpoint_get_last_endpoint_local_user_slot')}:"
                f"{online.get('active_sender_endpoint_get_last_endpoint_send_target')} "
                f"epsend={online.get('active_endpoint_send_calls')}:"
                f"{online.get('active_endpoint_send_last_packet_data_byte0')}/"
                f"{online.get('active_endpoint_send_last_packet_byte0')}:"
                f"{online.get('active_endpoint_send_last_result')}:"
                f"sz={online.get('active_endpoint_send_last_packet_size')} "
                f"epsend_op=0:{online.get('active_endpoint_send_data_opcode0_calls')}/"
                f"4:{online.get('active_endpoint_send_data_opcode4_calls')}/"
                f"5:{online.get('active_endpoint_send_data_opcode5_calls')}/"
                f"6:{online.get('active_endpoint_send_data_opcode6_calls')}/"
                f"10:{online.get('active_endpoint_send_data_opcode10_calls')}/"
                f"15:{online.get('active_endpoint_send_data_opcode15_calls')}/"
                f"20:{online.get('active_endpoint_send_data_opcode20_calls')}/"
                f"21:{online.get('active_endpoint_send_data_opcode21_calls')}:"
                f"op21res={online.get('active_endpoint_send_opcode21_last_result')}:"
                f"op21sz={online.get('active_endpoint_send_opcode21_last_size')} "
                f"rresolve={online.get('route_writer_resolve_calls')}:"
                f"{online.get('route_writer_resolve_last_route_tag')}:"
                f"{online.get('route_writer_resolve_last_writer')} "
                f"rreg={online.get('route_writer_registry_last_entry_count')}:"
                f"{online.get('route_writer_registry_last_selected_index')}:"
                f"e0={online.get('route_writer_registry_last_entry0_writer')}:"
                f"sel={online.get('route_writer_registry_last_selected_writer')} "
                f"rkeys={online.get('luxor_route_key_enum_last_entry_count')}:"
                f"{online.get('luxor_route_key_enum_last_replacement_count')}/"
                f"{online.get('luxor_route_key_enum_last_nonreplacement_count')}:"
                f"shared={online.get('luxor_route_key_enum_last_service_shared_ref_count')}:"
                f"e0={online.get('luxor_route_key_enum_last_entry0_route_tag')}:"
                f"e1={online.get('luxor_route_key_enum_last_entry1_route_tag')} "
                f"rklist={online.get('luxor_route_key_list_build_last_mode')}:"
                f"{online.get('luxor_route_key_list_build_last_entry_count')}:"
                f"{online.get('luxor_route_key_list_build_last_replacement_count')}/"
                f"{online.get('luxor_route_key_list_build_last_nonreplacement_count')}:"
                f"e0={online.get('luxor_route_key_list_build_last_entry0_route_tag')} "
                f"rsrcq={online.get('route_writer_source_acquire_calls')}:"
                f"src={online.get('route_writer_source_acquire_last_native_route_source')}:"
                f"out={online.get('route_writer_source_acquire_last_out_owner')}:"
                f"sel={online.get('route_writer_source_acquire_last_selected_index')} "
                f"racq={online.get('route_writer_acquire_calls')}:"
                f"tag={online.get('route_writer_acquire_last_route_tag')}:"
                f"out={online.get('route_writer_acquire_last_out_owner')}:"
                f"sel={online.get('route_writer_acquire_last_selected_index')}:"
                f"n={online.get('route_writer_acquire_last_registry_entry_count')} "
                f"rassign={online.get('route_writer_assign_calls')}:"
                f"tag={online.get('route_writer_assign_last_route_tag')}:"
                f"mode={online.get('route_writer_assign_last_writer_mode')}:"
                f"out={online.get('route_writer_assign_last_out_owner')}:"
                f"sel={online.get('route_writer_assign_last_selected_index')} "
                f"rwrite={online.get('route_writer_send_calls')}:"
                f"{online.get('route_writer_send_last_packet_data_byte0')}:"
                f"op21={online.get('route_writer_send_data_opcode21_calls')}:"
                f"{online.get('route_writer_send_opcode21_last_result')}:"
                f"{online.get('route_writer_send_last_result')} "
                f"rwpeer={online.get('route_writer_send_last_primary_peer_object')}:"
                f"{online.get('route_writer_send_last_primary_peer_ref')} "
                f"rwdef={online.get('route_writer_send_last_deferred_count')}:"
                f"{online.get('route_writer_send_last_deferred_capacity')} "
                f"rwback={online.get('route_writer_send_last_backend_available')}:"
                f"{online.get('route_writer_send_last_secondary_backend_available')} "
                f"rwbranch={online.get('route_writer_send_last_inferred_branch')}:"
                f"mode={online.get('route_writer_send_last_packet_mode')} "
                f"rwsmall={online.get('route_writer_send_last_small_route_ready_target')}:"
                f"{online.get('route_writer_send_last_small_route_send_target')} "
                f"rwop21sr={online.get('route_writer_send_opcode21_small_route_pre_available')}/"
                f"{online.get('route_writer_send_opcode21_small_route_pre_slot_present')}:"
                f"{online.get('route_writer_send_opcode21_small_route_pre_sequence')}->"
                f"{online.get('route_writer_send_opcode21_small_route_post_sequence')}:"
                f"post={online.get('route_writer_send_opcode21_small_route_post_available')}/"
                f"{online.get('route_writer_send_opcode21_small_route_post_slot_present')} "
                f"rwmap={online.get('route_writer_send_last_route_map_capacity_target')}:"
                f"{online.get('route_writer_send_last_route_map_send_target')}:"
                f"{online.get('route_writer_send_last_route_map_bind_target')}:"
                f"{online.get('route_writer_send_last_route_map_flush_target')} "
                f"rparent={online.get('route_writer_send_last_parent_vtable')}:"
                f"{online.get('route_writer_send_last_parent_backend_get_target')} "
                f"rwparent={online.get('route_writer_send_last_parent_state')}/"
                f"{online.get('route_writer_send_last_parent_ready_flags')}:"
                f"{online.get('route_writer_send_last_parent_ready')} "
                f"rwop21parent={online.get('route_writer_send_opcode21_parent_state')}/"
                f"{online.get('route_writer_send_opcode21_parent_ready_flags')}:"
                f"{online.get('route_writer_send_opcode21_parent_ready')} "
                f"bget={online.get('route_writer_backend_get_calls')}:"
                f"{online.get('route_writer_backend_get_last_backend')}:"
                f"{online.get('route_writer_backend_get_last_backend_send_target')} "
                f"bsend={online.get('route_writer_backend_send_calls')}:"
                f"magic={online.get('route_writer_backend_send_magic_calls')}:"
                f"{online.get('route_writer_backend_send_last_payload_opcode')}:"
                f"{online.get('route_writer_backend_send_last_destination')} "
                f"lookup={online.get('luxor_backend_connection_lookup_calls')}:"
                f"{online.get('luxor_backend_connection_lookup_last_destination_key')}:"
                f"{online.get('luxor_backend_connection_lookup_last_connection')} "
                f"bmatch={online.get('luxor_backend_connection_destination_match_calls')}:"
                f"t={online.get('luxor_backend_connection_destination_match_true_calls')}:"
                f"f={online.get('luxor_backend_connection_destination_match_false_calls')}:"
                f"op={online.get('luxor_backend_connection_destination_match_last_timeline_opcode')}:"
                f"res={online.get('luxor_backend_connection_destination_match_last_result')}:"
                f"cand={online.get('luxor_backend_connection_destination_match_last_candidate_connection')}->"
                f"{online.get('luxor_backend_connection_destination_match_last_candidate_raw_send_target')} "
                f"btable={online.get('luxor_backend_connection_table_last_entry_count')}:"
                f"{online.get('luxor_backend_connection_table_last_selected_index')}:"
                f"e0={online.get('luxor_backend_connection_table_last_entry0_connection')}->"
                f"{online.get('luxor_backend_connection_table_last_entry0_raw_send_target')}:"
                f"e1={online.get('luxor_backend_connection_table_last_entry1_connection')}->"
                f"{online.get('luxor_backend_connection_table_last_entry1_raw_send_target')} "
                f"raw={online.get('luxor_backend_connection_raw_send_calls')}:"
                f"magic={online.get('luxor_backend_connection_raw_send_magic_calls')}:"
                f"{online.get('luxor_backend_connection_raw_send_last_payload_opcode')}:"
                f"sel={online.get('luxor_backend_connection_raw_send_last_route_selector')}:"
                f"chan={online.get('luxor_backend_connection_raw_send_last_route_channel_target')}:"
                f"lower={online.get('luxor_backend_connection_raw_send_last_lower_transport')}:"
                f"{online.get('luxor_backend_connection_raw_send_last_lower_sender_send_target')} "
                f"lsend={online.get('luxor_lower_sender_send_calls')}:"
                f"magic={online.get('luxor_lower_sender_send_magic_calls')}:"
                f"{online.get('luxor_lower_sender_send_last_payload_opcode')}:"
                f"res={online.get('luxor_lower_sender_send_last_result')} "
                f"rappend={online.get('luxor_route_channel_append_calls')}:"
                f"op21={online.get('luxor_route_channel_append_opcode21_calls')}:"
                f"{online.get('luxor_route_channel_append_last_payload_opcode')}:"
                f"{online.get('luxor_route_channel_append_last_used_before')}->"
                f"{online.get('luxor_route_channel_append_last_used_after')}:"
                f"res={online.get('luxor_route_channel_append_last_result')} "
                f"rdrain={online.get('luxor_route_dispatch_drain_calls')}:"
                f"sel={online.get('luxor_route_dispatch_drain_last_selector_before')}->"
                f"{online.get('luxor_route_dispatch_drain_last_selector_after')}:"
                f"pending={online.get('luxor_route_dispatch_drain_last_pending_before')}->"
                f"{online.get('luxor_route_dispatch_drain_last_pending_after')}:"
                f"used={online.get('luxor_route_dispatch_drain_last_channel_used_before')}->"
                f"{online.get('luxor_route_dispatch_drain_last_channel_used_after')} "
                f"bfwd={online.get('luxor_backend_route_channel_forward_calls')}:"
                f"{online.get('luxor_backend_route_channel_forward_last_used_before')}->"
                f"{online.get('luxor_backend_route_channel_forward_last_used_after')}:"
                f"{online.get('luxor_backend_route_channel_forward_last_output_slots_target')} "
                f"slots={online.get('luxor_route_channel_output_slots_calls')}:"
                f"{online.get('luxor_route_channel_output_slots_last_slot_count')}:"
                f"{online.get('luxor_route_channel_output_slots_last_used_before')}->"
                f"{online.get('luxor_route_channel_output_slots_last_used_after')} "
                f"oslot={online.get('luxor_route_frame_output_slot_calls')}:"
                f"ff={online.get('luxor_route_frame_output_slot_reject_slot_ff_calls')}:"
                f"ok={online.get('luxor_route_frame_output_slot_success_calls')}:"
                f"idx={online.get('luxor_route_frame_output_slot_last_output_slot_index')}:"
                f"res={online.get('luxor_route_frame_output_slot_last_result')} "
                f"oq={online.get('luxor_route_output_task_queue_calls')}:"
                f"n={online.get('luxor_route_output_task_queue_last_entry_count')}:"
                f"valid={online.get('luxor_route_output_task_queue_last_valid_entry_count')}:"
                f"slot={online.get('luxor_route_output_task_queue_last_slot_index')}:"
                f"target={online.get('luxor_route_output_task_queue_last_first_consumer_accept_target')}:"
                f"last={online.get('luxor_route_output_task_queue_last_last_consumer_accept_target')}:"
                f"layout={online.get('luxor_route_output_task_queue_last_consumer_layout')}:"
                f"cb={online.get('luxor_route_output_task_queue_last_callback')}:"
                f"ref={online.get('luxor_route_output_task_queue_last_receiver_ref')}:"
                f"op={online.get('luxor_route_output_task_queue_last_frame_payload_opcode')} "
                f"oqcb={online.get('luxor_route_output_task_consumer_calls')}:"
                f"res={online.get('luxor_route_output_task_consumer_last_result')}:"
                f"cb={online.get('luxor_route_output_task_consumer_last_callback')}:"
                f"op={online.get('luxor_route_output_task_consumer_last_frame_payload_opcode')} "
                f"fw={online.get('luxor_forwarding_route_output_task_consumer_calls')}:"
                f"res={online.get('luxor_forwarding_route_output_task_consumer_last_result')}:"
                f"cb={online.get('luxor_forwarding_route_output_task_consumer_last_callback')}:"
                f"op={online.get('luxor_forwarding_route_output_task_consumer_last_frame_payload_opcode')} "
                f"fwd={online.get('luxor_forwarded_route_opcode_dispatch_calls')}:"
                f"op={online.get('luxor_forwarded_route_opcode_dispatch_last_opcode')}:"
                f"hit={online.get('luxor_forwarded_route_opcode_dispatch_last_handler_found')}:"
                f"key={online.get('luxor_forwarded_route_opcode_dispatch_last_handler_key')} "
                f"recv={online.get('luxor_backend_packet_stream_receive_calls')}:"
                f"{online.get('luxor_backend_packet_stream_receive_last_payload_opcode')}:"
                f"sel={online.get('luxor_backend_packet_stream_receive_last_route_selector')} "
                f"lsif={online.get('luxor_lower_transport_send_if_ready_calls')}:"
                f"{online.get('luxor_lower_transport_send_if_ready_last_payload_opcode')}:"
                f"res={online.get('luxor_lower_transport_send_if_ready_last_result')} "
                f"sender={online.get('connect_sender_send_calls')}:"
                f"{online.get('connect_sender_send_last_packet_byte0')}:"
                f"{online.get('connect_sender_send_last_result')} "
                f"dispatch={online.get('active_packet_dispatch_calls')}:"
                f"op0={online.get('active_packet_dispatch_opcode0_calls')}/"
                f"op4={online.get('active_packet_dispatch_opcode4_calls')}/"
                f"op5={online.get('active_packet_dispatch_opcode5_calls')}/"
                f"op6={online.get('active_packet_dispatch_opcode6_calls')}/"
                f"op9={online.get('active_packet_dispatch_opcode9_calls')}/"
                f"op10={online.get('active_packet_dispatch_opcode10_calls')}/"
                f"op11={online.get('active_packet_dispatch_opcode11_calls')}/"
                f"op15={online.get('active_packet_dispatch_opcode15_calls')}/"
                f"op20={online.get('active_packet_dispatch_opcode20_calls')}/"
                f"op21={online.get('active_packet_dispatch_opcode21_calls')}:"
                f"last={online.get('active_packet_dispatch_last_opcode')}:"
                f"{online.get('active_packet_dispatch_last_state_before')}->"
                f"{online.get('active_packet_dispatch_last_state_after')} "
                f"open_msg={online.get('transport_open_message_calls')}:"
                f"{online.get('transport_open_message_last_state_before')}->"
                f"{online.get('transport_open_message_last_state_after')}:"
                f"{online.get('transport_open_message_last_ready_before')}->"
                f"{online.get('transport_open_message_last_ready_after')} "
                f"open_rsp={online.get('transport_open_response_calls')}:"
                f"{online.get('transport_open_response_last_state_before')}->"
                f"{online.get('transport_open_response_last_state_after')}:"
                f"{online.get('transport_open_response_last_ready_before')}->"
                f"{online.get('transport_open_response_last_ready_after')} "
                f"ready_conn={online.get('ready_connect_state')} "
                f"ready_chan={online.get('ready_channel_state')} "
                f"ready_can={1 if online.get('ready_channel_can_send') else 0} "
                f"ready_can_raw={online.get('ready_channel_can_send_raw_4c')} "
                f"ready_raw48={online.get('ready_channel_raw_48')} "
                f"ready_field20={online.get('ready_channel_field_20')} "
                f"ready_registry={online.get('ready_session_connection_registry')} "
                f"ready_lookup={online.get('ready_session_connection_registry_lookup_fn')} "
                f"net_enabled={1 if online.get('luxor_network_check_compat_enabled') else 0} "
                f"net_hook={1 if online.get('luxor_network_check_compat_hook_installed') else 0} "
                f"net_orig_false={online.get('luxor_network_check_compat_original_false')} "
                f"net_forced={online.get('luxor_network_check_compat_forced_true')} "
                f"state_task={online.get('connection_state_update_task_calls')}:"
                f"{online.get('connection_state_update_task_last_state_before')}->"
                f"{online.get('connection_state_update_task_last_state_after')}/"
                f"{online.get('connection_state_update_task_last_sub_state_before')}->"
                f"{online.get('connection_state_update_task_last_sub_state_after')}:"
                f"to5={online.get('connection_state_update_task_transitions_to_state5')} "
                f"fail9={online.get('active_failed_substate9_calls')}:"
                f"{online.get('active_failed_substate9_last_state_before')}->"
                f"{online.get('active_failed_substate9_last_state_after')}/"
                f"{online.get('active_failed_substate9_last_sub_state_before')}->"
                f"{online.get('active_failed_substate9_last_sub_state_after')}:"
                f"caller={online.get('active_failed_substate9_last_caller_rva')} "
                f"wide5={online.get('active_state5_wide_calls')}:"
                f"{online.get('active_state5_wide_last_state_before')}->"
                f"{online.get('active_state5_wide_last_state_after')}/"
                f"{online.get('active_state5_wide_last_sub_state_before')}->"
                f"{online.get('active_state5_wide_last_sub_state_after')}:"
                f"fn={online.get('active_state5_wide_last_function_rva')}:"
                f"hook={online.get('active_state5_wide_last_hook_bit')}:"
                f"mask={online.get('active_state5_wide_hook_install_mask')}/"
                f"{online.get('active_state5_wide_hook_all_bits')} "
                f"reqq={online.get('active_request_queue_calls')}:"
                f"op={online.get('active_request_queue_last_event_opcode')}:"
                f"caller={online.get('active_request_queue_last_caller_rva')}:"
                f"hook={1 if online.get('active_request_queue_hook_installed') else 0} "
                f"missing={','.join(item.get('missing') or [])}"
            )
    if report["online_stage_summary"]:
        for item in report["online_stage_summary"]:
            print(
                "online-stage "
                f"role={item.get('role')} "
                f"ok={1 if item.get('ok') else 0} "
                f"scene={item.get('current_scene_class')} "
                f"nav={item.get('online_nav_attempts')} "
                f"pm_req={item.get('player_match_scene_request_attempts')}:"
                f"{item.get('player_match_scene_last_request_nav_attempt')}:"
                f"{1 if item.get('player_match_scene_last_request_ok') else 0} "
                f"menu_input={item.get('main_menu_input_attempts')}:"
                f"{item.get('main_menu_input_sequence_step')}:"
                f"{1 if item.get('main_menu_input_last_ok') else 0}:"
                f"{item.get('main_menu_input_last_key')} "
                f"menu_nav={item.get('main_menu_nav_last_action')}:"
                f"{1 if item.get('main_menu_nav_last_action_accepted') else 0}:"
                f"{1 if item.get('main_menu_nav_last_action_transitioned') else 0}:"
                f"{item.get('main_menu_nav_cooldown_remaining')} "
                f"in_room={1 if item.get('player_match_in_room_ok') else 0}:"
                f"{item.get('player_match_in_room_state')} "
                f"in_room_conn={1 if item.get('player_match_in_room_session_connecting') else 0} "
                f"host_marker={1 if item.get('host_room_ready_marker_observed') else 0}:"
                f"{item.get('host_room_ready_marker_failure')} "
                f"find_attempts={item.get('client_find_attempts')} "
                f"find_count={item.get('find_result_count')} "
                f"find_bytes={item.get('find_result_element_size')} "
                f"join_step={item.get('join_scene_pipeline_step')} "
                f"join_bytes={item.get('join_scene_selected_result_bytes')} "
                f"ping_events={item.get('ping_search_event_trace_count')} "
                f"steam_visible={1 if item.get('steam_lobby_probe_target_visible') else 0} "
                f"named_state={item.get('native_named_session_state_byte')} "
                f"session_enum={item.get('native_named_session_state')} "
                f"lobby={item.get('native_named_session_lobby_id')} "
                f"deferred={item.get('deferred_session_connect_attempts')} "
                f"conn_obj={item.get('luxor_session_connection_object')} "
                f"active_state={item.get('luxor_active_connect_state')} "
                f"active_sub={item.get('luxor_active_connect_sub_state')} "
                f"active_system_off={item.get('luxor_active_connect_system_offset')} "
                f"state_task={item.get('luxor_active_session_state_update_task')} "
                f"notify_task={item.get('luxor_active_session_notify_task')} "
                f"session_event={item.get('luxor_active_session_event_handle')} "
                f"delegate_array={item.get('luxor_connect_delegate_handle_array')} "
                f"join_delegate={item.get('luxor_delegate_join_session_complete_handle')} "
                f"deferred_delegate={item.get('luxor_delegate_deferred_session_connection_handle')} "
                f"pe_followups={item.get('process_event_followup_count')} "
                f"pe_kind={item.get('process_event_followup_last_kind')} "
                f"member_join={item.get('session_member_join_count')} "
                f"member_attempt={item.get('session_member_join_attempted_count')} "
                f"member_first={item.get('session_member_join_first_tick')} "
                f"transport_tick={item.get('luxor_active_transport_tick')} "
                f"transport_status={item.get('luxor_active_transport_status_code')} "
                f"transport_ready_state={item.get('luxor_active_transport_ready_state')} "
                f"transport_is_host={item.get('luxor_active_transport_is_host')} "
                f"transport_channels={item.get('luxor_active_transport_channel_count')}/"
                f"{item.get('luxor_active_transport_channel_capacity')} "
                f"transport_ready={1 if item.get('luxor_active_transport_ready') else 0} "
                f"sessq={item.get('luxor_session_async_queue_count')}:"
                f"{item.get('luxor_session_async_queue_first_callback_rva')}/"
                f"{item.get('luxor_session_async_queue_tail_callback_rva')} "
                f"join_force={item.get('join_complete_compat_count')} "
                f"join_call={1 if item.get('join_complete_compat_call_ok') else 0} "
                f"join_before={item.get('join_complete_compat_active_state_before')} "
                f"join_after={item.get('join_complete_compat_active_state_after')} "
                f"join_conn_after={item.get('join_complete_compat_session_connection_after')} "
                f"join_failure={item.get('join_complete_compat_failure')} "
                f"trans_force={item.get('transport_ready_compat_count')} "
                f"trans_trigger={item.get('transport_ready_compat_trigger_reason')} "
                f"trans_call={1 if item.get('transport_ready_compat_call_ok') else 0} "
                f"trans_before={item.get('transport_ready_compat_before_ready_state')} "
                f"trans_after={item.get('transport_ready_compat_after_ready_state')} "
                f"trans_deferred={1 if item.get('transport_ready_compat_deferred_called_after_force') else 0} "
                f"trans_conn_after={item.get('transport_ready_compat_session_connection_after_deferred')} "
                f"trans_failure={item.get('transport_ready_compat_failure')} "
                f"open_calls={item.get('ready_channel_open_calls')} "
                f"open_caller={item.get('ready_channel_open_last_caller_rva')} "
                f"open_can={item.get('ready_channel_open_last_can_send_before')}->{item.get('ready_channel_open_last_can_send_after')} "
                f"ready_open={item.get('ready_open_compat_count')} "
                f"ready_open_ok={1 if item.get('ready_open_compat_call_ok') else 0} "
                f"ready_open_can={item.get('ready_open_compat_can_send_before')}->{item.get('ready_open_compat_can_send_after')} "
                f"ready_open_parent={item.get('ready_open_compat_parent_sample_ok')}:"
                f"{item.get('ready_open_compat_parent_state')}/"
                f"{item.get('ready_open_compat_parent_ready_flags')}:"
                f"{item.get('ready_open_compat_parent_ready_state_ok')}/"
                f"{item.get('ready_open_compat_parent_ready_flag_set')}:"
                f"ready={item.get('ready_open_compat_parent_ready')} "
                f"ready_open_sr={item.get('ready_open_compat_small_route_sample_ok')}:"
                f"{item.get('ready_open_compat_small_route_next_available')}/"
                f"{item.get('ready_open_compat_small_route_next_slot_present')}:"
                f"seq={item.get('ready_open_compat_small_route_sequence_counter')}->"
                f"{item.get('ready_open_compat_small_route_next_sequence')}:"
                f"n={item.get('ready_open_compat_small_route_count')}/"
                f"{item.get('ready_open_compat_small_route_limit')}:"
                f"hit={item.get('ready_open_compat_small_route_collision_index')} "
                f"ready_open_mark={item.get('ready_open_compat_mark_ready_before')}->{item.get('ready_open_compat_mark_ready_after')} "
                f"ready_open_mark_ok={1 if item.get('ready_open_compat_mark_ready_call_ok') else 0} "
                f"ready_open_failure={item.get('ready_open_compat_failure')} "
                f"mark_calls={item.get('transport_ready_mark_calls')} "
                f"mark_ready={item.get('transport_ready_mark_last_ready_before')}->{item.get('transport_ready_mark_last_ready_after')} "
                f"reg_steps={item.get('ready_registry_step80_calls')}/{item.get('ready_registry_stepd0_calls')}/{item.get('ready_registry_stepd8_calls')} "
                f"qsend={item.get('queued_opcode_send_calls')}:"
                f"{item.get('queued_opcode_send_last_outer_opcode')}/"
                f"{item.get('queued_opcode_send_last_inner_opcode')}:"
                f"ch={item.get('queued_opcode_send_last_channel_id')}:"
                f"{item.get('queued_opcode_send_last_result')}:"
                f"tag={item.get('queued_opcode_send_last_source_routing_tag')} "
                f"rtfix={1 if item.get('peer_route_tag_fix_enabled') else 0}:"
                f"{item.get('peer_route_tag_fix_attempts')}/"
                f"{item.get('peer_route_tag_fix_applied')}:"
                f"orig={item.get('peer_route_tag_fix_last_original_tag')}:"
                f"peer={item.get('peer_route_tag_fix_last_replacement_tag')}:"
                f"src={item.get('peer_route_tag_fix_last_peer_source')}:"
                f"verify={item.get('peer_route_tag_fix_last_write_verified')}/"
                f"{item.get('peer_route_tag_fix_last_verified_tag')}:"
                f"res={item.get('peer_route_tag_fix_last_result')} "
                f"qclone={item.get('queued_work_item_clone_calls')}:"
                f"{item.get('queued_work_item_clone_last_source_inner_opcode')}:"
                f"ch={item.get('queued_work_item_clone_last_source_channel_id')}:"
                f"tag={item.get('queued_work_item_clone_last_source_routing_tag')} "
                f"op6send={item.get('active_opcode6_send_calls')}:"
                f"{item.get('active_opcode6_send_last_state_payload')}:"
                f"{item.get('active_opcode6_send_last_result')} "
                f"eget={item.get('active_sender_endpoint_get_calls')}:"
                f"{item.get('active_sender_endpoint_get_last_local_user')}:"
                f"{item.get('active_sender_endpoint_get_last_endpoint_local_user_slot')}:"
                f"{item.get('active_sender_endpoint_get_last_endpoint_send_target')} "
                f"epsend={item.get('active_endpoint_send_calls')}:"
                f"{item.get('active_endpoint_send_last_packet_data_byte0')}/"
                f"{item.get('active_endpoint_send_last_packet_byte0')}:"
                f"{item.get('active_endpoint_send_last_result')}:"
                f"sz={item.get('active_endpoint_send_last_packet_size')} "
                f"epsend_op=0:{item.get('active_endpoint_send_data_opcode0_calls')}/"
                f"4:{item.get('active_endpoint_send_data_opcode4_calls')}/"
                f"5:{item.get('active_endpoint_send_data_opcode5_calls')}/"
                f"6:{item.get('active_endpoint_send_data_opcode6_calls')}/"
                f"10:{item.get('active_endpoint_send_data_opcode10_calls')}/"
                f"15:{item.get('active_endpoint_send_data_opcode15_calls')}/"
                f"20:{item.get('active_endpoint_send_data_opcode20_calls')}/"
                f"21:{item.get('active_endpoint_send_data_opcode21_calls')}:"
                f"op21res={item.get('active_endpoint_send_opcode21_last_result')}:"
                f"op21sz={item.get('active_endpoint_send_opcode21_last_size')} "
                f"rresolve={item.get('route_writer_resolve_calls')}:"
                f"{item.get('route_writer_resolve_last_route_tag')}:"
                f"{item.get('route_writer_resolve_last_writer')} "
                f"rreg={item.get('route_writer_registry_last_entry_count')}:"
                f"{item.get('route_writer_registry_last_selected_index')}:"
                f"e0={item.get('route_writer_registry_last_entry0_writer')}:"
                f"sel={item.get('route_writer_registry_last_selected_writer')} "
                f"rkeys={item.get('luxor_route_key_enum_last_entry_count')}:"
                f"{item.get('luxor_route_key_enum_last_replacement_count')}/"
                f"{item.get('luxor_route_key_enum_last_nonreplacement_count')}:"
                f"shared={item.get('luxor_route_key_enum_last_service_shared_ref_count')}:"
                f"e0={item.get('luxor_route_key_enum_last_entry0_route_tag')}:"
                f"e1={item.get('luxor_route_key_enum_last_entry1_route_tag')} "
                f"rklist={item.get('luxor_route_key_list_build_last_mode')}:"
                f"{item.get('luxor_route_key_list_build_last_entry_count')}:"
                f"{item.get('luxor_route_key_list_build_last_replacement_count')}/"
                f"{item.get('luxor_route_key_list_build_last_nonreplacement_count')}:"
                f"e0={item.get('luxor_route_key_list_build_last_entry0_route_tag')} "
                f"rsrcq={item.get('route_writer_source_acquire_calls')}:"
                f"src={item.get('route_writer_source_acquire_last_native_route_source')}:"
                f"out={item.get('route_writer_source_acquire_last_out_owner')}:"
                f"sel={item.get('route_writer_source_acquire_last_selected_index')} "
                f"racq={item.get('route_writer_acquire_calls')}:"
                f"tag={item.get('route_writer_acquire_last_route_tag')}:"
                f"out={item.get('route_writer_acquire_last_out_owner')}:"
                f"sel={item.get('route_writer_acquire_last_selected_index')}:"
                f"n={item.get('route_writer_acquire_last_registry_entry_count')} "
                f"rassign={item.get('route_writer_assign_calls')}:"
                f"tag={item.get('route_writer_assign_last_route_tag')}:"
                f"mode={item.get('route_writer_assign_last_writer_mode')}:"
                f"out={item.get('route_writer_assign_last_out_owner')}:"
                f"sel={item.get('route_writer_assign_last_selected_index')} "
                f"rwrite={item.get('route_writer_send_calls')}:"
                f"{item.get('route_writer_send_last_packet_data_byte0')}:"
                f"op21={item.get('route_writer_send_data_opcode21_calls')}:"
                f"{item.get('route_writer_send_opcode21_last_result')}:"
                f"{item.get('route_writer_send_last_result')} "
                f"rwpeer={item.get('route_writer_send_last_primary_peer_object')}:"
                f"{item.get('route_writer_send_last_primary_peer_ref')} "
                f"rwdef={item.get('route_writer_send_last_deferred_count')}:"
                f"{item.get('route_writer_send_last_deferred_capacity')} "
                f"rwback={item.get('route_writer_send_last_backend_available')}:"
                f"{item.get('route_writer_send_last_secondary_backend_available')} "
                f"rwbranch={item.get('route_writer_send_last_inferred_branch')}:"
                f"mode={item.get('route_writer_send_last_packet_mode')} "
                f"rwsmall={item.get('route_writer_send_last_small_route_ready_target')}:"
                f"{item.get('route_writer_send_last_small_route_send_target')} "
                f"rwop21sr={item.get('route_writer_send_opcode21_small_route_pre_available')}/"
                f"{item.get('route_writer_send_opcode21_small_route_pre_slot_present')}:"
                f"{item.get('route_writer_send_opcode21_small_route_pre_sequence')}->"
                f"{item.get('route_writer_send_opcode21_small_route_post_sequence')}:"
                f"post={item.get('route_writer_send_opcode21_small_route_post_available')}/"
                f"{item.get('route_writer_send_opcode21_small_route_post_slot_present')} "
                f"rwmap={item.get('route_writer_send_last_route_map_capacity_target')}:"
                f"{item.get('route_writer_send_last_route_map_send_target')}:"
                f"{item.get('route_writer_send_last_route_map_bind_target')}:"
                f"{item.get('route_writer_send_last_route_map_flush_target')} "
                f"rparent={item.get('route_writer_send_last_parent_vtable')}:"
                f"{item.get('route_writer_send_last_parent_backend_get_target')} "
                f"rwparent={item.get('route_writer_send_last_parent_state')}/"
                f"{item.get('route_writer_send_last_parent_ready_flags')}:"
                f"{item.get('route_writer_send_last_parent_ready')} "
                f"rwop21parent={item.get('route_writer_send_opcode21_parent_state')}/"
                f"{item.get('route_writer_send_opcode21_parent_ready_flags')}:"
                f"{item.get('route_writer_send_opcode21_parent_ready')} "
                f"bget={item.get('route_writer_backend_get_calls')}:"
                f"{item.get('route_writer_backend_get_last_backend')}:"
                f"{item.get('route_writer_backend_get_last_backend_send_target')} "
                f"bsend={item.get('route_writer_backend_send_calls')}:"
                f"magic={item.get('route_writer_backend_send_magic_calls')}:"
                f"{item.get('route_writer_backend_send_last_payload_opcode')}:"
                f"{item.get('route_writer_backend_send_last_destination')} "
                f"lookup={item.get('luxor_backend_connection_lookup_calls')}:"
                f"{item.get('luxor_backend_connection_lookup_last_destination_key')}:"
                f"{item.get('luxor_backend_connection_lookup_last_connection')} "
                f"bmatch={item.get('luxor_backend_connection_destination_match_calls')}:"
                f"t={item.get('luxor_backend_connection_destination_match_true_calls')}:"
                f"f={item.get('luxor_backend_connection_destination_match_false_calls')}:"
                f"op={item.get('luxor_backend_connection_destination_match_last_timeline_opcode')}:"
                f"res={item.get('luxor_backend_connection_destination_match_last_result')}:"
                f"cand={item.get('luxor_backend_connection_destination_match_last_candidate_connection')}->"
                f"{item.get('luxor_backend_connection_destination_match_last_candidate_raw_send_target')} "
                f"btable={item.get('luxor_backend_connection_table_last_entry_count')}:"
                f"{item.get('luxor_backend_connection_table_last_selected_index')}:"
                f"e0={item.get('luxor_backend_connection_table_last_entry0_connection')}->"
                f"{item.get('luxor_backend_connection_table_last_entry0_raw_send_target')}:"
                f"e1={item.get('luxor_backend_connection_table_last_entry1_connection')}->"
                f"{item.get('luxor_backend_connection_table_last_entry1_raw_send_target')} "
                f"raw={item.get('luxor_backend_connection_raw_send_calls')}:"
                f"magic={item.get('luxor_backend_connection_raw_send_magic_calls')}:"
                f"{item.get('luxor_backend_connection_raw_send_last_payload_opcode')} "
                f"bfwd={item.get('luxor_backend_route_channel_forward_calls')}:"
                f"{item.get('luxor_backend_route_channel_forward_last_used_before')}->"
                f"{item.get('luxor_backend_route_channel_forward_last_used_after')}:"
                f"{item.get('luxor_backend_route_channel_forward_last_output_slots_target')} "
                f"slots={item.get('luxor_route_channel_output_slots_calls')}:"
                f"{item.get('luxor_route_channel_output_slots_last_slot_count')}:"
                f"{item.get('luxor_route_channel_output_slots_last_used_before')}->"
                f"{item.get('luxor_route_channel_output_slots_last_used_after')} "
                f"oslot={item.get('luxor_route_frame_output_slot_calls')}:"
                f"ff={item.get('luxor_route_frame_output_slot_reject_slot_ff_calls')}:"
                f"ok={item.get('luxor_route_frame_output_slot_success_calls')}:"
                f"idx={item.get('luxor_route_frame_output_slot_last_output_slot_index')}:"
                f"res={item.get('luxor_route_frame_output_slot_last_result')} "
                f"oq={item.get('luxor_route_output_task_queue_calls')}:"
                f"n={item.get('luxor_route_output_task_queue_last_entry_count')}:"
                f"valid={item.get('luxor_route_output_task_queue_last_valid_entry_count')}:"
                f"slot={item.get('luxor_route_output_task_queue_last_slot_index')}:"
                f"target={item.get('luxor_route_output_task_queue_last_first_consumer_accept_target')}:"
                f"last={item.get('luxor_route_output_task_queue_last_last_consumer_accept_target')}:"
                f"layout={item.get('luxor_route_output_task_queue_last_consumer_layout')}:"
                f"cb={item.get('luxor_route_output_task_queue_last_callback')}:"
                f"ref={item.get('luxor_route_output_task_queue_last_receiver_ref')}:"
                f"op={item.get('luxor_route_output_task_queue_last_frame_payload_opcode')} "
                f"oqcb={item.get('luxor_route_output_task_consumer_calls')}:"
                f"res={item.get('luxor_route_output_task_consumer_last_result')}:"
                f"cb={item.get('luxor_route_output_task_consumer_last_callback')}:"
                f"op={item.get('luxor_route_output_task_consumer_last_frame_payload_opcode')} "
                f"fw={item.get('luxor_forwarding_route_output_task_consumer_calls')}:"
                f"res={item.get('luxor_forwarding_route_output_task_consumer_last_result')}:"
                f"cb={item.get('luxor_forwarding_route_output_task_consumer_last_callback')}:"
                f"op={item.get('luxor_forwarding_route_output_task_consumer_last_frame_payload_opcode')} "
                f"fwd={item.get('luxor_forwarded_route_opcode_dispatch_calls')}:"
                f"op={item.get('luxor_forwarded_route_opcode_dispatch_last_opcode')}:"
                f"hit={item.get('luxor_forwarded_route_opcode_dispatch_last_handler_found')}:"
                f"key={item.get('luxor_forwarded_route_opcode_dispatch_last_handler_key')} "
                f"recv={item.get('luxor_backend_packet_stream_receive_calls')}:"
                f"{item.get('luxor_backend_packet_stream_receive_last_payload_opcode')}:"
                f"sel={item.get('luxor_backend_packet_stream_receive_last_route_selector')} "
                f"lsif={item.get('luxor_lower_transport_send_if_ready_calls')}:"
                f"{item.get('luxor_lower_transport_send_if_ready_last_payload_opcode')}:"
                f"res={item.get('luxor_lower_transport_send_if_ready_last_result')} "
                f"sender={item.get('connect_sender_send_calls')}:"
                f"{item.get('connect_sender_send_last_packet_byte0')}:"
                f"{item.get('connect_sender_send_last_result')} "
                f"dispatch={item.get('active_packet_dispatch_calls')}:"
                f"op0={item.get('active_packet_dispatch_opcode0_calls')}/"
                f"op4={item.get('active_packet_dispatch_opcode4_calls')}/"
                f"op5={item.get('active_packet_dispatch_opcode5_calls')}/"
                f"op6={item.get('active_packet_dispatch_opcode6_calls')}/"
                f"op9={item.get('active_packet_dispatch_opcode9_calls')}/"
                f"op10={item.get('active_packet_dispatch_opcode10_calls')}/"
                f"op11={item.get('active_packet_dispatch_opcode11_calls')}/"
                f"op15={item.get('active_packet_dispatch_opcode15_calls')}/"
                f"op20={item.get('active_packet_dispatch_opcode20_calls')}/"
                f"op21={item.get('active_packet_dispatch_opcode21_calls')}:"
                f"last={item.get('active_packet_dispatch_last_opcode')}:"
                f"{item.get('active_packet_dispatch_last_state_before')}->"
                f"{item.get('active_packet_dispatch_last_state_after')} "
                f"open_msg={item.get('transport_open_message_calls')}:"
                f"{item.get('transport_open_message_last_state_before')}->"
                f"{item.get('transport_open_message_last_state_after')}:"
                f"{item.get('transport_open_message_last_ready_before')}->"
                f"{item.get('transport_open_message_last_ready_after')} "
                f"open_rsp={item.get('transport_open_response_calls')}:"
                f"{item.get('transport_open_response_last_state_before')}->"
                f"{item.get('transport_open_response_last_state_after')}:"
                f"{item.get('transport_open_response_last_ready_before')}->"
                f"{item.get('transport_open_response_last_ready_after')} "
                f"ready_conn={item.get('ready_connect_state')} "
                f"ready_chan={item.get('ready_channel_state')} "
                f"ready_can={1 if item.get('ready_channel_can_send') else 0} "
                f"ready_can_raw={item.get('ready_channel_can_send_raw_4c')} "
                f"ready_raw48={item.get('ready_channel_raw_48')} "
                f"ready_field20={item.get('ready_channel_field_20')} "
                f"ready_registry={item.get('ready_session_connection_registry')} "
                f"ready_lookup={item.get('ready_session_connection_registry_lookup_fn')} "
                f"net_enabled={1 if item.get('luxor_network_check_compat_enabled') else 0} "
                f"net_hook={1 if item.get('luxor_network_check_compat_hook_installed') else 0} "
                f"net_orig_false={item.get('luxor_network_check_compat_original_false')} "
                f"net_forced={item.get('luxor_network_check_compat_forced_true')} "
                f"state_task={item.get('connection_state_update_task_calls')}:"
                f"{item.get('connection_state_update_task_last_state_before')}->"
                f"{item.get('connection_state_update_task_last_state_after')}/"
                f"{item.get('connection_state_update_task_last_sub_state_before')}->"
                f"{item.get('connection_state_update_task_last_sub_state_after')}:"
                f"to5={item.get('connection_state_update_task_transitions_to_state5')} "
                f"fail9={item.get('active_failed_substate9_calls')}:"
                f"{item.get('active_failed_substate9_last_state_before')}->"
                f"{item.get('active_failed_substate9_last_state_after')}/"
                f"{item.get('active_failed_substate9_last_sub_state_before')}->"
                f"{item.get('active_failed_substate9_last_sub_state_after')}:"
                f"caller={item.get('active_failed_substate9_last_caller_rva')} "
                f"wide5={item.get('active_state5_wide_calls')}:"
                f"{item.get('active_state5_wide_last_state_before')}->"
                f"{item.get('active_state5_wide_last_state_after')}/"
                f"{item.get('active_state5_wide_last_sub_state_before')}->"
                f"{item.get('active_state5_wide_last_sub_state_after')}:"
                f"fn={item.get('active_state5_wide_last_function_rva')}:"
                f"hook={item.get('active_state5_wide_last_hook_bit')}:"
                f"mask={item.get('active_state5_wide_hook_install_mask')}/"
                f"{item.get('active_state5_wide_hook_all_bits')} "
                f"reqq={item.get('active_request_queue_calls')}:"
                f"op={item.get('active_request_queue_last_event_opcode')}:"
                f"caller={item.get('active_request_queue_last_caller_rva')}:"
                f"hook={1 if item.get('active_request_queue_hook_installed') else 0} "
                f"sc6_raw={item.get('steam_presence_search_max_api_lobby_count')} "
                f"sc6_collected={item.get('steam_presence_search_max_collected_lobby_count')} "
                f"failure={item.get('failure')}"
            )
    for item in report["live_correction_summary"]:
        print(
            "correction "
            f"role={item.get('role')} depth={item.get('correction_depth')} "
            f"restore={1 if item.get('snapshot_restore') else 0} "
            f"resim={1 if item.get('hidden_resim') else 0} "
            f"converged={1 if item.get('converged') else 0}"
        )
    for item in report["soak_summary"]:
        print(
            "soak "
            f"role={item.get('role')} ok={1 if item.get('ok') else 0} "
            f"live_events={item.get('live_online_events')} "
            f"handshake_events={item.get('sidecar_handshake_events')} "
            f"disarms={item.get('disarm_events')} "
            f"corrections={item.get('unexpected_correction_events')}"
        )
    return 0 if contract["verdict"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
