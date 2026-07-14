#!/usr/bin/env python3
"""Deterministic two-process replay-fork GekkoNet integration runner."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

import rollback_two_client_test_run as two_client
from rollback_report_contract import contract_fields, coverage, sha256_file, utc_now


REPLAY = two_client.DEFAULT_REPLAY_INPUT_FILE
REPLAY_SHA256 = "95E12E394D35C13D5E0DD3DCE692F9E0A4022E2A84205A9EC75F2FA6726D7879"
ANCHOR_SEQUENCE = 2751
ANCHOR_ROUND = 1
ANCHOR_MASTER = 414
DEFAULT_BUILD_ID = 0x1135D62F163558E1
# Live replay manifests include the gameplay ranges; the default-disabled
# startup manifest intentionally has a different, incomplete schema hash.
DEFAULT_SCHEMA_ID = 0x1A830FAD7075422E
STEAM = Path(r"C:\Program Files (x86)\Steam\steam.exe")
SANDBOXIE = Path(r"C:\Program Files\Sandboxie-Plus\Start.exe")
GAME_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\SoulcaliburVI.exe"
)
DEPLOYED_HORSEMOD_DLL = (
    GAME_EXE.parent / "ue4ss" / "Mods" / "HorseMod" / "dlls" / "main.dll"
)
APP_ID = "544750"
REPORT_DIR = two_client.REPO / "reports" / "rollback_replay_fork"
REQUIRED_PHASES = [
    "inventory", "replay-identity", "frozen-fixture", "direct-step-matrix",
    "authenticated-gekko", "rollback-correction", "anchor-restore",
    "replay-resume",
]


def atomic_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(f".{path.name}.{os.getpid()}.{time.time_ns()}.tmp")
    temp.write_text(text, encoding="utf-8", newline="\n")
    os.replace(temp, path)


def atomic_json(path: Path, value: dict[str, Any]) -> None:
    atomic_text(path, json.dumps(value, indent=2) + "\n")


def kill_clients() -> dict[str, Any]:
    proc = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "$p=Get-Process SoulcaliburVI -ErrorAction SilentlyContinue; "
         "if($p){$p|Stop-Process -Force}; exit 0"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        check=False, timeout=30,
    )
    return {"phase": "kill", "ok": proc.returncode == 0,
            "output": proc.stdout}


def clear_stale_requests(args: argparse.Namespace) -> dict[str, Any]:
    roots = [two_client.HOST_SAVED_DIR]
    roots.extend(two_client.sandbox_saved_roots(
        args.sandbox_root, args.sandbox_box))
    removed: list[str] = []
    failures: list[str] = []
    for root in roots:
        for name in ("rollback_lab_request.txt",
                     "replay_file_start_request.json"):
            path = root / name
            try:
                path.unlink()
                removed.append(str(path))
            except FileNotFoundError:
                pass
            except OSError as exc:
                failures.append(f"{path}: {exc}")
    return {"phase": "clear-stale-requests", "ok": not failures,
            "removed": removed, "failures": failures}


def sandbox_leaf(box: str) -> str:
    return Path(box.replace("/", "\\")).name


def launch_clients(args: argparse.Namespace) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    # Launch the installed executable while the two Steam clients are already
    # resident. Repeated `steam.exe -applaunch` requests can block forever on
    # Steam's interactive cloud-conflict prompt after a forcibly terminated
    # fixture, making the supposedly automated inventory phase nondeterministic.
    # Direct launch still registers AppID 544750 with the corresponding Steam
    # process, and Sandboxie supplies the isolated Saved root for peer two.
    host_cmd = [str(args.game_exe)]
    sandbox_cmd = [str(args.sandboxie_start),
                   f"/box:{sandbox_leaf(args.sandbox_box)}",
                   str(args.game_exe),
                   f"-QueryPort={args.sandbox_query_port}"]
    launches = (("sandbox", sandbox_cmd), ("host", host_cmd)) \
        if args.sandbox_first else (("host", host_cmd), ("sandbox", sandbox_cmd))
    for index, (role, command) in enumerate(launches):
        try:
            process = subprocess.Popen(command, cwd=str(args.game_exe.parent))
            results.append({"phase": f"launch-{role}", "ok": True,
                            "launcher_pid": process.pid, "command": command})
        except OSError as exc:
            results.append({"phase": f"launch-{role}", "ok": False,
                            "failure": str(exc), "command": command})
            break
        if index == 0:
            lead = (args.sandbox_launch_lead_seconds if role == "sandbox"
                    else args.host_launch_lead_seconds)
            time.sleep(lead)
    return results


def roots_for_live_clients(args: argparse.Namespace) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    snap = two_client.snapshot("replay-fork-inventory")
    pids = {two_client.int_value(pid, -1) for pid in snap.get("sc6_pids", [])}
    roots = two_client.discover_roots(
        pids, args.sandbox_root, args.sandbox_box,
        processes=snap.get("processes", []),
    )
    selected: list[dict[str, Any]] = []
    for role in ("host", "sandbox"):
        candidates = [root for root in roots if root.get("role") == role]
        live = [root for root in candidates
                if len(root.get("live_trace_pids") or []) == 1]
        if len(live) == 1:
            selected.append(live[0])
    return snap, selected


def wait_inventory(args: argparse.Namespace) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    deadline = time.time() + args.launch_timeout
    latest: tuple[dict[str, Any], list[dict[str, Any]]] = ({}, [])
    while time.time() < deadline:
        latest = roots_for_live_clients(args)
        snap, roots = latest
        pids = {two_client.int_value(pid, -1)
                for pid in snap.get("sc6_pids", []) if two_client.int_value(pid, -1) > 0}
        role_pids = {two_client.int_value((root.get("live_trace_pids") or [-1])[0], -1)
                     for root in roots}
        if len(pids) == 2 and len(roots) == 2 and len(role_pids) == 2:
            return latest
        time.sleep(1)
    return latest


def role_pid(root: dict[str, Any]) -> int:
    return two_client.int_value((root.get("live_trace_pids") or [-1])[0], -1)


def role_trace(root: dict[str, Any]) -> Path | None:
    pid = role_pid(root)
    files = [path for path in two_client.trace_files(Path(root["path"]))
             if f"_pid{pid}.jsonl" in path.name]
    return files[-1] if files else None


def read_events(path: Path | None) -> list[dict[str, Any]]:
    if not path:
        return []
    events: list[dict[str, Any]] = []
    try:
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                events.append(value)
    except OSError:
        pass
    return events


def matching_events(root: dict[str, Any], request_id: str) -> list[dict[str, Any]]:
    return [event for event in read_events(role_trace(root))
            if event.get("request_id") == request_id]


def wait_replay_start(roots: list[dict[str, Any]], start_id: str,
                      timeout: float,
                      lab_request_id: str = "") -> dict[str, Any]:
    deadline = time.time() + timeout
    latest: dict[str, Any] = {}
    while time.time() < deadline:
        latest = {}
        for root in roots:
            role = str(root["role"])
            matches = [event for event in read_events(role_trace(root))
                       if event.get("event") == "replay_file_start_result"
                       and event.get("run_id") == start_id]
            if matches:
                latest[role] = matches[-1]
                continue
            if lab_request_id:
                lab_events = matching_events(root, lab_request_id)
                claimed = [event for event in lab_events
                           if event.get("event") in {
                               "rollback_replay_fork_baseline",
                               "rollback_replay_fork_stability",
                               "rollback_replay_fork_direct_step",
                               "rollback_replay_fork_failed",
                               "rollback_replay_fork_complete"}
                           or (event.get("event") == "rollback_replay_fork_status"
                               and event.get("state") != "waiting-for-anchor")]
                if claimed:
                    latest[role] = {
                        "event": "replay_file_start_claimed_by_fixture",
                        "run_id": start_id,
                        "ok": True,
                        "reason": "replay-fork driver claimed live replay",
                        "lab_event": claimed[-1],
                    }
        if len(latest) == 2:
            break
        time.sleep(1)
    ok = len(latest) == 2 and all(bool(event.get("ok")) for event in latest.values())
    return {"phase": "replay-start", "ok": ok, "roles": latest}


def replay_start(roots: list[dict[str, Any]], replay: Path,
                 start_id: str, timeout: int,
                 generation_mode: str = "lux-no-render") -> None:
    request = {"enabled": True, "run_id": start_id, "path": str(replay),
               "timeout_seconds": timeout, "force_native_launch": True,
               "generate_mode": generation_mode,
               "timeline_generation_mode": generation_mode}
    for root in roots:
        atomic_json(Path(root["path"]) / "replay_file_start_request.json", request)


def lab_request(args: argparse.Namespace, root: dict[str, Any], request_id: str,
                profile: str, nonce: int, run_frames: int) -> str:
    host = root["role"] == "host"
    slot = 0 if host else 1
    local_peer = 1 if host else 2
    remote_peer = 2 if host else 1
    local_port = args.host_port if host else args.sandbox_port
    peer_port = args.sandbox_port if host else args.host_port
    base = two_client.request_text(
        enabled=True, trace=True, case="replay-fork-lab",
        request_id=request_id, request_phase="replay-fork-lab",
        rollback_window=args.rollback_window, seed=f"0x{args.fault_seed:X}",
        mode="replay-fork-lab", client_role=str(root["role"]),
        sandbox_root=str(args.sandbox_root), sandbox_box=args.sandbox_box,
        replay_input_file=str(args.replay), production_enabled=True,
        bind_address="127.0.0.1", bind_port=local_port,
        peer_address="127.0.0.1", peer_port=peer_port,
        local_player_slot=slot, native_input_source_slot=slot,
        production_local_peer=local_peer, production_remote_peer=remote_peer,
        secret=args.secret, input_delay=args.input_delay,
        network_profile=profile, fault_seed=args.fault_seed,
        expected_build_id=args.expected_build_id,
        expected_schema_id=args.expected_schema_id,
    )
    extra = [
        "replay_fork=1", f"replay_sha256={REPLAY_SHA256}",
        f"replay_anchor_sequence={ANCHOR_SEQUENCE}",
        f"replay_anchor_round={ANCHOR_ROUND}",
        f"replay_anchor_master={ANCHOR_MASTER}",
        f"replay_run_nonce_hash=0x{nonce:X}",
        f"replay_fork_stability_ticks={args.stability_ticks}",
        f"replay_fork_run_frames={run_frames}",
        f"replay_fork_require_rollback={1 if profile != 'clean_0ms' else 0}",
    ]
    return base + "\n".join(extra) + "\n"


def integer(event: dict[str, Any], field: str) -> int:
    return two_client.int_value(event.get(field), 0)


def presentation_target_matches(event: dict[str, Any], player: int) -> bool:
    """Reject invalid native presentation targets before readback checks."""
    try:
        actor_x = float(event[f"player{player}_actor_x"])
        actor_y = float(event[f"player{player}_actor_y"])
        actor_z = float(event[f"player{player}_actor_z"])
    except (KeyError, TypeError, ValueError):
        return False
    # The stock virtual getter includes presentation interpolation and is not
    # expected to equal the raw simulation fields at every frame. Its return
    # must still be finite/plausible; exact K2 actor readback and motion across
    # credited publications are checked independently below.
    return all(value == value and abs(value) <= 100000.0
               for value in (actor_x, actor_y, actor_z))


def evaluate_pair(roots: list[dict[str, Any]], request_id: str,
                  profile: str) -> dict[str, Any]:
    by_role: dict[str, list[dict[str, Any]]] = {
        str(root["role"]): matching_events(root, request_id) for root in roots
    }
    failures: list[str] = []
    finals: dict[str, dict[str, Any]] = {}
    directs: dict[str, dict[int, int]] = {}
    baselines: dict[str, int] = {}
    for role, events in by_role.items():
        complete = [event for event in events
                    if event.get("event") == "rollback_replay_fork_complete"]
        if not complete:
            failed = [event for event in events
                      if event.get("event") == "rollback_replay_fork_failed"]
            reason = str(failed[-1].get("failure")) if failed else "missing"
            failures.append(f"{role}:complete-missing:{reason}")
            continue
        final = complete[-1]
        finals[role] = final
        baselines[role] = integer(final, "baseline_hash")
        directs[role] = {integer(event, "window"): integer(event, "post_hash")
                         for event in events
                         if event.get("event") == "rollback_replay_fork_direct_step"}
        direct_events = [event for event in events
                         if event.get("event") == "rollback_replay_fork_direct_step"]
        if any(integer(event, "frame_delta") != integer(event, "window")
               for event in direct_events):
            failures.append(f"{role}:direct-step-frame-delta-mismatch")
        if direct_events and all(integer(event, "post_hash") == baselines[role]
                                 for event in direct_events):
            failures.append(f"{role}:direct-step-state-never-changed")
        if integer(final, "saves") == 0 or integer(final, "advances") == 0:
            failures.append(f"{role}:gekko-save-advance-missing")
        if integer(final, "pair_accepts") == 0 or not final.get("no_desync"):
            failures.append(f"{role}:consensus-missing-or-desync")
        if (integer(final, "evidence_frames") != integer(final, "frames")
                or integer(final, "terminal_evidence_hash") == 0
                or integer(final, "summary_overwrites") != 0):
            failures.append(f"{role}:terminal-evidence-incomplete")
        if integer(final, "peer_final_hash") != integer(final, "final_hash"):
            failures.append(f"{role}:terminal-peer-hash-mismatch")
        if (not final.get("passed") or not final.get("baseline_restored")
                or not final.get("replay_resumed")):
            failures.append(f"{role}:completion-contract-incomplete")
        if profile != "clean_0ms" and (
                integer(final, "loads") == 0
                or integer(final, "rollback_advances") == 0):
            failures.append(f"{role}:correction-not-observed")
        if profile != "clean_0ms" and not final.get("prediction_diverged"):
            failures.append(f"{role}:prediction-divergence-not-observed")
        if integer(final, "snapshot_peak") > 128:
            failures.append(f"{role}:snapshot-retention-unbounded")
        if (integer(final, "presentation_syncs") < 5
                or integer(final, "presentation_failures") != 0
                or not final.get("presentation_gameplay_unchanged")
                or not final.get("presentation_motion_observed")):
            failures.append(f"{role}:presentation-evidence-incomplete")
        presentation_events = [
            event for event in events
            if event.get("event") == "rollback_replay_fork_presentation_sync"
        ]
        presentation_credits = {
            (str(event.get("reason")), integer(event, "presentation_credit"),
             integer(event, "expected_canonical_hash"))
            for event in presentation_events
        }
        if (len(presentation_events) < integer(final, "presentation_syncs")
                or len(presentation_events) != len(presentation_credits)
                or any(not event.get("gameplay_unchanged")
                       for event in presentation_events)):
            failures.append(f"{role}:presentation-credit-contract-failed")
        if any(
                not event.get("actor_readback_matches")
                or not presentation_target_matches(event, 0)
                or not presentation_target_matches(event, 1)
                or not event.get("player0_getter_called")
                or not event.get("player1_getter_called")
                or not event.get("player0_setter_called")
                or not event.get("player1_setter_called")
                or not event.get("player0_setter_result")
                or not event.get("player1_setter_result")
                or integer(event, "player0_actor") == 0
                or integer(event, "player1_actor") == 0
                or integer(event, "player0_root_component") == 0
                or integer(event, "player1_root_component") == 0
                or integer(event, "player0_actor")
                    == integer(event, "player0_simulation_chara")
                or integer(event, "player1_actor")
                    == integer(event, "player1_simulation_chara")
                for event in presentation_events):
            failures.append(f"{role}:presentation-actor-readback-failed")
        names = {str(event.get("event")) for event in events}
        for required in ("rollback_replay_fork_stability",
                         "rollback_replay_fork_cleanup",
                         "rollback_replay_fork_presentation_sync"):
            if required not in names:
                failures.append(f"{role}:{required}-missing")
        cleanup = [event for event in events
                   if event.get("event") == "rollback_replay_fork_cleanup"]
        if (not cleanup or not cleanup[-1].get("anchor_restore_ok")
                or not cleanup[-1].get("hold_released")):
            failures.append(f"{role}:cleanup-not-verified")
        if any(event.get("production_certified") is not False
               or event.get("evidence_scope") != "rollback-core-integration"
               or event.get("proof_non_skip") is not False
               for event in events if str(event.get("event", "")).startswith("rollback_replay_fork")):
            failures.append(f"{role}:evidence-scope-violation")
    if set(finals) == {"host", "sandbox"}:
        if baselines["host"] == 0 or baselines["host"] != baselines["sandbox"]:
            failures.append("pair:baseline-hash-mismatch")
        if directs.get("host") != directs.get("sandbox") or set(directs.get("host", {})) != {1, 2, 8, 15, 60}:
            failures.append("pair:direct-step-matrix-mismatch")
        host, sandbox = finals["host"], finals["sandbox"]
        if integer(host, "final_hash") != integer(sandbox, "final_hash"):
            failures.append("pair:final-hash-mismatch")
        if integer(host, "terminal_evidence_hash") != integer(
                sandbox, "terminal_evidence_hash"):
            failures.append("pair:terminal-evidence-hash-mismatch")
        if integer(host, "local_input_hash") != integer(sandbox, "remote_input_hash"):
            failures.append("pair:host-local-sandbox-remote-input-mismatch")
        if integer(sandbox, "local_input_hash") != integer(host, "remote_input_hash"):
            failures.append("pair:sandbox-local-host-remote-input-mismatch")
        if profile != "clean_0ms" and (
                integer(host, "loads") + integer(sandbox, "loads") == 0
                or integer(host, "rollback_advances")
                    + integer(sandbox, "rollback_advances") == 0
                or not (host.get("prediction_diverged")
                        or sandbox.get("prediction_diverged"))):
            failures.append("pair:forced-correction-evidence-missing")
    return {"profile": profile, "request_id": request_id,
            "ok": not failures, "failures": failures,
            "traces": {str(root["role"]): str(role_trace(root) or "") for root in roots},
            "finals": finals, "direct_matrix": directs,
            "baseline_hashes": baselines}


def submit_profile(args: argparse.Namespace, roots: list[dict[str, Any]],
                   launch_index: int, profile: str) -> str:
    request_id = f"{args.run_id}-launch{launch_index}-{profile}"
    nonce = int.from_bytes(hashlib.sha256(request_id.encode()).digest()[:8], "little") or 1
    run_frames = max(60, round(args.soak_seconds * 60))
    for root in roots:
        text = lab_request(args, root, request_id, profile, nonce, run_frames)
        atomic_text(Path(root["path"]) / "rollback_lab_request.txt", text)
    return request_id


def run_profile(args: argparse.Namespace, roots: list[dict[str, Any]],
                launch_index: int, profile: str,
                request_id: str | None = None) -> dict[str, Any]:
    request_id = request_id or submit_profile(
        args, roots, launch_index, profile)
    deadline = time.time() + args.fixture_timeout + args.soak_seconds
    while time.time() < deadline:
        terminal = 0
        for root in roots:
            events = matching_events(root, request_id)
            if any(event.get("event") in {
                    "rollback_replay_fork_complete", "rollback_replay_fork_failed"}
                   for event in events):
                terminal += 1
        if terminal == 2:
            break
        time.sleep(1)
    result = evaluate_pair(roots, request_id, profile)
    for root in roots:
        try:
            (Path(root["path"]) / "rollback_lab_request.txt").unlink()
        except FileNotFoundError:
            pass
    return result


def parse_profiles(text: str) -> list[str]:
    allowed = {"clean_0ms", "wifi_50ms_jitter"}
    profiles = [item.strip() for item in text.split(",") if item.strip()]
    unknown = [item for item in profiles if item not in allowed]
    if not profiles or unknown:
        raise ValueError("profiles must contain clean_0ms and/or wifi_50ms_jitter")
    return profiles


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["replay-fork-lab", "direct-connect"],
                        default="replay-fork-lab")
    parser.add_argument("--run-id", default="")
    parser.add_argument("--replay", type=Path, default=REPLAY)
    parser.add_argument("--profiles", default="clean_0ms,wifi_50ms_jitter")
    parser.add_argument("--fresh-launches", type=int, default=1)
    parser.add_argument("--attach-only", action="store_true")
    parser.add_argument("--skip-kill", action="store_true")
    parser.add_argument("--steam-exe", type=Path, default=STEAM)
    parser.add_argument("--game-exe", type=Path, default=GAME_EXE)
    parser.add_argument("--sandboxie-start", type=Path, default=SANDBOXIE)
    parser.add_argument("--sandbox-root", type=Path, default=Path(r"C:\Sandbox"))
    parser.add_argument("--sandbox-box", default=two_client.DEFAULT_SANDBOX_BOX)
    parser.add_argument("--sandbox-query-port", type=int, default=27012)
    parser.add_argument("--host-port", type=int, default=two_client.HOST_HORSE_UDP_PORT)
    parser.add_argument("--sandbox-port", type=int, default=two_client.SANDBOX_HORSE_UDP_PORT)
    parser.add_argument("--expected-build-id", type=lambda x: int(x, 0), default=DEFAULT_BUILD_ID)
    parser.add_argument("--expected-schema-id", type=lambda x: int(x, 0), default=DEFAULT_SCHEMA_ID)
    parser.add_argument("--rollback-window", type=int, default=60)
    parser.add_argument("--input-delay", type=int, default=1)
    parser.add_argument("--fault-seed", type=lambda x: int(x, 0), default=0x5C6B0001)
    parser.add_argument("--secret", default="horse-replay-fork-v1")
    parser.add_argument("--stability-ticks", type=int, default=120)
    parser.add_argument("--soak-seconds", type=float, default=120.0)
    parser.add_argument("--launch-timeout", type=float, default=240.0)
    parser.add_argument("--fixture-timeout", type=float, default=300.0)
    parser.add_argument("--replay-start-timeout", type=int, default=240)
    parser.add_argument("--host-launch-lead-seconds", type=float, default=20.0)
    parser.add_argument("--sandbox-launch-lead-seconds", type=float, default=20.0)
    parser.add_argument("--sandbox-first", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="launch the constrained Sandboxie client before the host")
    parser.add_argument("--leave-running", action="store_true")
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    args.run_id = args.run_id or datetime.now().strftime("%Y%m%d-%H%M%S")
    if args.mode == "direct-connect":
        print("warning: direct-connect is deprecated; using replay-fork-lab",
              file=sys.stderr)
        args.mode = "replay-fork-lab"
    try:
        profiles = parse_profiles(args.profiles)
    except ValueError as exc:
        parser.error(str(exc))
    if args.fresh_launches < 1 or args.stability_ticks < 120:
        parser.error("fresh launches must be positive and stability ticks >= 120")
    if args.attach_only and (args.fresh_launches != 1 or len(profiles) != 1):
        parser.error("attach-only supports exactly one launch and one profile")
    actual_sha = sha256_file(args.replay).upper() if args.replay.is_file() else ""
    deployed_dll_sha = (sha256_file(DEPLOYED_HORSEMOD_DLL).upper()
                        if DEPLOYED_HORSEMOD_DLL.is_file() else "")
    steps: list[dict[str, Any]] = []
    runs: list[dict[str, Any]] = []
    observed: list[str] = []
    workflow_ok = actual_sha == REPLAY_SHA256
    if workflow_ok:
        observed.append("replay-identity")
    stop_workflow = False
    for launch_index in range(1, args.fresh_launches + 1):
        for profile in profiles:
            # A replay-fork run consumes and restores a live replay fixture, but
            # it does not return the game to a state where timeline generation
            # can safely be requested again. Give every profile its own fresh
            # process pair so clean and impaired oracle runs are independent.
            if not args.attach_only:
                if not args.skip_kill:
                    steps.append(kill_clients())
                stale = clear_stale_requests(args)
                steps.append(stale)
                workflow_ok = workflow_ok and stale["ok"]
                launched = launch_clients(args)
                steps.extend(launched)
                workflow_ok = workflow_ok and all(
                    step["ok"] for step in launched)
            snap, roots = wait_inventory(args)
            inventory_ok = (len(roots) == 2
                            and len(set(map(role_pid, roots))) == 2)
            steps.append({"phase": "inventory", "ok": inventory_ok,
                          "launch_index": launch_index, "profile": profile,
                          "pids": sorted(role_pid(root) for root in roots),
                          "roots": roots, "snapshot": snap})
            if inventory_ok:
                observed.append("inventory")
            if not inventory_ok:
                workflow_ok = False
                stop_workflow = True
                break
            request_id = submit_profile(
                args, roots, launch_index, profile)
            start_id = (
                f"{args.run_id}-launch{launch_index}-{profile}-replay")
            replay_start(roots, args.replay, start_id,
                         args.replay_start_timeout)
            start_result = wait_replay_start(
                roots, start_id, args.replay_start_timeout,
                lab_request_id=request_id)
            start_result["profile"] = profile
            steps.append(start_result)
            if not start_result["ok"]:
                workflow_ok = False
                for root in roots:
                    try:
                        (Path(root["path"]) /
                         "rollback_lab_request.txt").unlink()
                    except FileNotFoundError:
                        pass
                break
            run = run_profile(args, roots, launch_index, profile,
                              request_id=request_id)
            run["launch_index"] = launch_index
            runs.append(run)
            workflow_ok = workflow_ok and run["ok"]
            if run["ok"]:
                observed.extend(["frozen-fixture", "direct-step-matrix",
                                 "authenticated-gekko", "anchor-restore",
                                 "replay-resume"])
                if profile != "clean_0ms":
                    observed.append("rollback-correction")
            if not args.leave_running and not args.attach_only:
                steps.append(kill_clients())
        if stop_workflow:
            break
    oracle_match = None
    oracle_pairs: list[dict[str, Any]] = []
    if {"clean_0ms", "wifi_50ms_jitter"}.issubset(profiles):
        oracle_match = True
        for launch_index in range(1, args.fresh_launches + 1):
            clean = next((run for run in runs
                          if run["profile"] == "clean_0ms"
                          and run.get("launch_index") == launch_index), None)
            impaired = next((run for run in runs
                             if run["profile"] == "wifi_50ms_jitter"
                             and run.get("launch_index") == launch_index), None)
            pair_failures: list[str] = []
            if not clean or not impaired or not clean.get("ok") or not impaired.get("ok"):
                pair_failures.append("paired-run-missing-or-failed")
            else:
                for role in ("host", "sandbox"):
                    clean_final = clean["finals"][role]
                    impaired_final = impaired["finals"][role]
                    for field in ("final_hash", "terminal_evidence_hash",
                                  "local_input_hash", "remote_input_hash"):
                        if integer(clean_final, field) != integer(impaired_final, field):
                            pair_failures.append(f"{role}:{field}-oracle-mismatch")
            pair_ok = not pair_failures
            oracle_pairs.append({"launch_index": launch_index,
                                 "ok": pair_ok,
                                 "failures": pair_failures})
            oracle_match = oracle_match and pair_ok
            if impaired and pair_failures:
                impaired["failures"].extend(
                    f"oracle:{failure}" for failure in pair_failures)
                impaired["ok"] = False
        workflow_ok = workflow_ok and oracle_match
    required = list(REQUIRED_PHASES)
    if "wifi_50ms_jitter" not in profiles:
        required.remove("rollback-correction")
    cov = coverage(required, observed)
    deployed_dll_sha_after = (sha256_file(DEPLOYED_HORSEMOD_DLL).upper()
                              if DEPLOYED_HORSEMOD_DLL.is_file() else "")
    deployed_dll_unchanged = (bool(deployed_dll_sha)
                              and deployed_dll_sha_after == deployed_dll_sha)
    workflow_ok = workflow_ok and deployed_dll_unchanged
    report = {
        **contract_fields(workflow_kind="replay-fork-lab",
                          workflow_ok=workflow_ok,
                          coverage_result=cov,
                          acceptance_workflow=True),
        "run_id": args.run_id, "created_utc": utc_now(),
        "mode": "replay-fork-lab", "deprecated_alias_used": False,
        "evidence_scope": "rollback-core-integration",
        "production_certified": False, "proof_non_skip": False,
        "gekko_library": "GekkoNet", "replay": str(args.replay),
        "deployed_horsemod_dll": str(DEPLOYED_HORSEMOD_DLL),
        "deployed_horsemod_sha256_start": deployed_dll_sha,
        "deployed_horsemod_sha256_end": deployed_dll_sha_after,
        "deployed_horsemod_unchanged": deployed_dll_unchanged,
        "replay_sha256_expected": REPLAY_SHA256,
        "replay_sha256_actual": actual_sha,
        "anchor": {"sequence": ANCHOR_SEQUENCE, "round": ANCHOR_ROUND,
                   "master": ANCHOR_MASTER},
        "profiles": profiles, "fresh_launches": args.fresh_launches,
        "oracle_match": oracle_match, "oracle_pairs": oracle_pairs,
        "steps": steps, "runs": runs,
        "release_lanes": {
            "replay_fork": "rollback-core-integration",
            "local_vs_attach": "MirroredVersus production evidence",
            "player_match_attach": "StockOnlinePvp production evidence",
            "ui_automation": "smoke/diagnostic only",
        },
    }
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    output = args.output or REPORT_DIR / f"rollback_replay_fork_{args.run_id}.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                      encoding="utf-8")
    print(f"report={output}")
    print(f"verdict={report['verdict']}")
    return 0 if report["verdict"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
