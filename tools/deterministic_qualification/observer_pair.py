from __future__ import annotations

import datetime as dt
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .artifacts import sha256_file
from .process_control import (
    GameProcess,
    close_game,
    force_stop_game_for_cleanup,
    list_game_processes,
)
from .sandboxie_pair import (
    SandboxiePairProcesses,
    SandboxiePairSpec,
    classify_game_processes,
    list_sandbox_pids,
    require_isolated_paths,
)


SAFE_CONFIG = """config_version=1
enabled=false
rollback_window=12
input_delay=1
trace=false
correction_probe=false
forced_depth7_qualification=false
"""

OBSERVER_CONFIG = SAFE_CONFIG.replace("trace=false", "trace=true")
REQUEST_NAME = "online_observer_request.txt"
REPORT_NAME = "online_observer_report.json"
ROOM_REQUEST_NAME = "online_room_request.txt"
ROOM_REPORT_NAME = "online_room_report.json"
_RUN_ID = re.compile(r"^[A-Za-z0-9_.-]{1,63}$")
_HASH = re.compile(r"^[0-9a-fA-F]{64}$")


@dataclass(frozen=True)
class ObserverPeerPaths:
    mods_root: Path
    horsemod_dll: Path
    config: Path
    qualification_root: Path
    log: Path

    @property
    def replay_mod_root(self) -> Path:
        return self.mods_root / "ReplayQualificationMod"


@dataclass(frozen=True)
class ObserverPairPaths:
    host: ObserverPeerPaths
    sandbox: ObserverPeerPaths

    def validate(self) -> None:
        require_isolated_paths(
            self.host.qualification_root,
            self.sandbox.qualification_root,
            self.host.log,
            self.sandbox.log,
        )
        if self.host.horsemod_dll.resolve(strict=False) == self.sandbox.horsemod_dll.resolve(strict=False):
            raise RuntimeError("host and Sandboxie HorseMod DLL paths must be distinct")


def _atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="\n")
    os.replace(temporary, path)


def write_safe_config(peer: ObserverPeerPaths, observer: bool = False) -> None:
    _atomic_write(peer.config, OBSERVER_CONFIG if observer else SAFE_CONFIG)


def remove_probe_files(peer: ObserverPeerPaths, run_id: str | None = None) -> None:
    request = peer.qualification_root / REQUEST_NAME
    report = peer.qualification_root / REPORT_NAME
    room_request = peer.qualification_root / ROOM_REQUEST_NAME
    room_report = peer.qualification_root / ROOM_REPORT_NAME
    temporaries = (
        peer.qualification_root / (REPORT_NAME + ".tmp"),
        peer.qualification_root / "online_observer_report.tmp",
        peer.qualification_root / "online_room_report.tmp",
    )
    if run_id is None:
        for path in (request, report, room_request, room_report):
            path.unlink(missing_ok=True)
    else:
        for path in (request, report, room_request, room_report):
            try:
                text = path.read_text(encoding="utf-8")
            except OSError:
                continue
            if f"run_id={run_id}\n" in text or f'"run_id": "{run_id}"' in text:
                path.unlink(missing_ok=True)
    for temporary in temporaries:
        temporary.unlink(missing_ok=True)


def create_probe_request(
    peer: ObserverPeerPaths, run_id: str, timeout_seconds: int
) -> None:
    if not _RUN_ID.fullmatch(run_id):
        raise ValueError("observer run ID is invalid")
    if timeout_seconds < 1 or timeout_seconds > 900:
        raise ValueError("observer timeout must be between 1 and 900 seconds")
    remove_probe_files(peer)
    request = (
        "version=1\n"
        "request_type=observer_only\n"
        f"run_id={run_id}\n"
        f"timeout_seconds={timeout_seconds}\n"
        "arm=true\n"
    )
    _atomic_write(peer.qualification_root / REQUEST_NAME, request)


def create_host_room_request(peer: ObserverPeerPaths, run_id: str) -> None:
    if not _RUN_ID.fullmatch(run_id):
        raise ValueError("room-creation run ID is invalid")
    request = (
        "version=2\n"
        "request_type=host_room_create\n"
        f"run_id={run_id}\n"
        "arm=true\n"
    )
    _atomic_write(peer.qualification_root / ROOM_REQUEST_NAME, request)


def create_host_room_suppression(peer: ObserverPeerPaths, run_id: str) -> None:
    """Shadow the host request inside Sandboxie's merged filesystem.

    The request parser requires arm=true. A sandbox-local arm=false record is
    therefore structurally incapable of entering room automation while still
    hiding the host filesystem's request from the sandbox process.
    """
    if not _RUN_ID.fullmatch(run_id):
        raise ValueError("room-suppression run ID is invalid")
    request = (
        "version=2\n"
        "request_type=host_room_create\n"
        f"run_id={run_id}\n"
        "arm=false\n"
    )
    _atomic_write(peer.qualification_root / ROOM_REQUEST_NAME, request)


def validate_host_room_suppression(peer: ObserverPeerPaths, run_id: str) -> None:
    expected = (
        "version=2\n"
        "request_type=host_room_create\n"
        f"run_id={run_id}\n"
        "arm=false\n"
    )
    request = peer.qualification_root / ROOM_REQUEST_NAME
    if request.read_text(encoding="utf-8") != expected:
        raise RuntimeError("sandbox-local host-room suppression changed")
    if (peer.qualification_root / ROOM_REPORT_NAME).exists():
        raise RuntimeError("sandbox process executed forbidden host-room automation")


def create_match_setup_request(
    peer: ObserverPeerPaths,
    run_id: str,
    role: str,
    lobby_id: int,
    local_steam_id: int,
    peer_steam_id: int,
    fighter_codes: list[str],
    stage_code: str,
    authored_stage_code: str,
    ui_stage_code: str,
    display_map_name: str,
) -> None:
    if not _RUN_ID.fullmatch(run_id):
        raise ValueError("match-setup run ID is invalid")
    if role not in ("host", "sandbox"):
        raise ValueError("match-setup role is invalid")
    if min(lobby_id, local_steam_id, peer_steam_id) <= 0:
        raise ValueError("match-setup Steam identities must be nonzero")
    if len(fighter_codes) != 2 or not all(
        isinstance(code, str) and 0 < len(code) <= 8
        and not set(code) & set("\r\n=") for code in fighter_codes
    ):
        raise ValueError("match-setup fighter codes are invalid")
    if not stage_code or len(stage_code) > 8 or set(stage_code) & set("\r\n="):
        raise ValueError("match-setup stage code is invalid")
    if (not authored_stage_code or len(authored_stage_code) > 8
            or set(authored_stage_code) & set("\r\n=")):
        raise ValueError("match-setup authored stage code is invalid")
    if (not ui_stage_code or len(ui_stage_code) > 32
            or set(ui_stage_code) & set("\r\n=")):
        raise ValueError("match-setup UI stage code is invalid")
    if (not display_map_name or len(display_map_name) > 96
            or set(display_map_name) & set("\r\n=")):
        raise ValueError("match-setup display map name is invalid")
    request = (
        "version=2\n"
        "request_type=match_setup\n"
        f"run_id={run_id}\n"
        "arm=true\n"
        f"role={role}\n"
        f"lobby_id={lobby_id}\n"
        f"local_steam_id={local_steam_id}\n"
        f"peer_steam_id={peer_steam_id}\n"
        f"fighter_left={fighter_codes[0]}\n"
        f"fighter_right={fighter_codes[1]}\n"
        f"stage_code={stage_code}\n"
        f"authored_stage_code={authored_stage_code}\n"
        f"ui_stage_code={ui_stage_code}\n"
        f"display_map_name={display_map_name}\n"
    )
    _atomic_write(peer.qualification_root / ROOM_REQUEST_NAME, request)


def _remove_replay_mod(root: Path) -> None:
    if not root.exists():
        return
    allowed_files = {
        root / "enabled.txt",
        root / "dlls" / "main.dll",
    }
    actual_files = {path for path in root.rglob("*") if path.is_file()}
    if not actual_files.issubset(allowed_files):
        unexpected = sorted(str(path) for path in actual_files - allowed_files)
        raise RuntimeError(
            "refusing to remove unexpected qualification-mod files: "
            + ", ".join(unexpected)
        )
    for path in allowed_files:
        path.unlink(missing_ok=True)
    dlls = root / "dlls"
    if dlls.exists():
        dlls.rmdir()
    root.rmdir()


def deploy_observer_pair(
    paths: ObserverPairPaths,
    horsemod_source: Path,
    replay_mod_source: Path,
) -> dict[str, str]:
    paths.validate()
    if list_game_processes():
        raise RuntimeError("SC6 must be closed before observer deployment")
    for source, label in (
        (horsemod_source, "HorseMod observer DLL"),
        (replay_mod_source, "observer bridge DLL"),
    ):
        if not source.is_file():
            raise FileNotFoundError(f"{label} not found: {source}")
    source_hash = sha256_file(horsemod_source)
    bridge_hash = sha256_file(replay_mod_source)
    for peer in (paths.host, paths.sandbox):
        peer.horsemod_dll.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(horsemod_source, peer.horsemod_dll)
        if sha256_file(peer.horsemod_dll) != source_hash:
            raise RuntimeError(f"deployed HorseMod hash mismatch: {peer.horsemod_dll}")
        _remove_replay_mod(peer.replay_mod_root)
        bridge_dll = peer.replay_mod_root / "dlls" / "main.dll"
        bridge_dll.parent.mkdir(parents=True)
        shutil.copy2(replay_mod_source, bridge_dll)
        (peer.replay_mod_root / "enabled.txt").write_text(
            "# Qualification-only bridge; presence of this file enables the mod.\n",
            encoding="utf-8",
            newline="\n",
        )
        if sha256_file(bridge_dll) != bridge_hash:
            raise RuntimeError(f"deployed observer bridge hash mismatch: {bridge_dll}")
        write_safe_config(peer, observer=True)
        remove_probe_files(peer)
    return {"horsemod": source_hash, "observer_bridge": bridge_hash}


def cleanup_observer_pair(paths: ObserverPairPaths, run_id: str | None) -> None:
    errors: list[str] = []
    for peer in (paths.host, paths.sandbox):
        try:
            remove_probe_files(peer, run_id)
            write_safe_config(peer, observer=False)
            _remove_replay_mod(peer.replay_mod_root)
        except OSError as error:
            errors.append(f"{peer.mods_root}: {error}")
    if errors:
        raise RuntimeError("observer cleanup failed: " + "; ".join(errors))


def wait_for_pair_processes(
    spec: SandboxiePairSpec, timeout_seconds: float
) -> tuple[SandboxiePairProcesses, dict[int, GameProcess]]:
    deadline = time.monotonic() + timeout_seconds
    last_count = 0
    while time.monotonic() < deadline:
        processes = list_game_processes()
        last_count = len(processes)
        if len(processes) > 2:
            raise RuntimeError("more than two SC6 processes started")
        if len(processes) == 2:
            sandbox_pids = set(list_sandbox_pids(spec))
            pair = classify_game_processes(
                {process.pid for process in processes}, sandbox_pids
            )
            by_pid = {process.pid: process for process in processes}
            query = f"-queryport={spec.sandbox_query_port}".casefold()
            if query not in by_pid[pair.sandbox_pid].command_line.casefold():
                raise RuntimeError("sandbox SC6 lacks the required alternate query port")
            if "-queryport=" in by_pid[pair.host_pid].command_line.casefold():
                raise RuntimeError("normal Steam SC6 unexpectedly owns a query-port override")
            return pair, by_pid
        time.sleep(0.5)
    raise TimeoutError(f"expected two SC6 processes; observed {last_count}")


def _load_report(path: Path, run_id: str) -> dict[str, object] | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return data if data.get("run_id") == run_id else None


def wait_for_observer_reports(
    paths: ObserverPairPaths,
    run_id: str,
    timeout_seconds: float,
    guard: Callable[[], None],
) -> tuple[dict[str, object], dict[str, object]]:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        guard()
        host = _load_report(paths.host.qualification_root / REPORT_NAME, run_id)
        sandbox = _load_report(paths.sandbox.qualification_root / REPORT_NAME, run_id)
        if host is not None and sandbox is not None:
            return host, sandbox
        time.sleep(0.25)
    raise TimeoutError("both observer-only reports did not arrive before timeout")


def wait_for_host_room(
    peer: ObserverPeerPaths,
    run_id: str,
    timeout_seconds: float,
    guard: Callable[[], None],
) -> dict[str, object]:
    deadline = time.monotonic() + timeout_seconds
    report_path = peer.qualification_root / ROOM_REPORT_NAME
    while time.monotonic() < deadline:
        guard()
        report = _load_report(report_path, run_id)
        if report is not None:
            if report.get("schema_version") != 2 \
                    or report.get("kind") != "host_room_create":
                raise RuntimeError("host room report schema is invalid")
            if report.get("state") != "complete" \
                    or report.get("detail") != "host_room_created_in_room":
                raise RuntimeError(
                    f"automatic host room creation failed: {report.get('detail')}"
                )
            if not isinstance(report.get("lobby_id"), int) \
                    or report["lobby_id"] <= 0:
                raise RuntimeError("automatic host room report lacks lobby identity")
            if not isinstance(report.get("local_steam_id"), int) \
                    or report["local_steam_id"] <= 0:
                raise RuntimeError("automatic host room report lacks Steam identity")
            return report
        time.sleep(0.25)
    raise TimeoutError("automatic host Player Match room creation timed out")


def raise_for_match_setup_failures(
    paths: ObserverPairPaths, automation_run_ids: dict[str, str]
) -> None:
    for label, peer in (("host", paths.host), ("sandbox", paths.sandbox)):
        report = _load_report(
            peer.qualification_root / ROOM_REPORT_NAME,
            automation_run_ids[label],
        )
        if report is None:
            continue
        if report.get("schema_version") != 2 \
                or report.get("kind") != "online_match_setup":
            raise RuntimeError(f"{label} match-setup report schema is invalid")
        if report.get("state") == "failed":
            raise RuntimeError(
                f"{label} automatic match setup failed: {report.get('detail')}"
            )


def _require_int(record: dict[str, object], field: str, nonzero: bool = False) -> int:
    value = record.get(field)
    if not isinstance(value, int) or isinstance(value, bool) or (nonzero and value == 0):
        raise RuntimeError(f"observer report has invalid {field}")
    return value


def validate_observer_reports(
    host: dict[str, object],
    sandbox: dict[str, object],
    host_steam_id: int,
    sandbox_steam_id: int,
    expected_stage_package: str,
    expected_stage_display_name: str,
) -> dict[str, object]:
    expected_members = {host_steam_id, sandbox_steam_id}
    summaries: list[dict[str, object]] = []
    for label, report, expected_local in (
        ("host", host, host_steam_id),
        ("sandbox", sandbox, sandbox_steam_id),
    ):
        if report.get("schema_version") != 1 or report.get("kind") != "online_observer_only":
            raise RuntimeError(f"{label} observer report schema is invalid")
        if report.get("state") != 2 or report.get("failure") != "none":
            raise RuntimeError(f"{label} observer probe did not complete successfully")
        session = report.get("session")
        lobby = report.get("lobby")
        content = report.get("content")
        if not isinstance(session, dict) or not isinstance(lobby, dict) or not isinstance(content, dict):
            raise RuntimeError(f"{label} observer report sections are invalid")
        role = _require_int(session, "role")
        local_slot = _require_int(session, "local_slot")
        if role not in (0, 1) or local_slot != role or _require_int(session, "virtual_state") != 4:
            raise RuntimeError(f"{label} online role/state/slot contract is invalid")
        for pointer in (
            "session_name", "session_interface", "active_connect", "online_session",
            "named_session", "session_info",
        ):
            _require_int(session, pointer, nonzero=True)
        lobby_id = _require_int(session, "lobby_id", nonzero=True)
        if _require_int(lobby, "local_steam_id") != expected_local:
            raise RuntimeError(f"{label} local Steam identity is wrong")
        members = lobby.get("members")
        if not isinstance(members, list) or len(members) != 2 or set(members) != expected_members:
            raise RuntimeError(f"{label} lobby membership is not the authenticated pair")
        if lobby.get("member_count") != 2 or lobby.get("casual_player_match") is not True:
            raise RuntimeError(f"{label} lobby is not a two-member casual Player Match")
        fighters = content.get("fighters")
        if (
            not isinstance(fighters, list) or len(fighters) != 2
            or not all(isinstance(code, str) and code for code in fighters)
        ):
            raise RuntimeError(f"{label} fighter contract is invalid")
        if content.get("stage_package") != expected_stage_package:
            raise RuntimeError(f"{label} authored stage package is wrong")
        if content.get("stage_display_name") != expected_stage_display_name:
            raise RuntimeError(f"{label} native display map name is wrong")
        if not isinstance(content.get("stage_code"), str) or not content["stage_code"]:
            raise RuntimeError(f"{label} stage code is missing")
        identity = content.get("loaded_package_identity")
        if not isinstance(identity, str) or not _HASH.fullmatch(identity) or int(identity, 16) == 0:
            raise RuntimeError(f"{label} loaded package identity is invalid")
        _require_int(content, "battle_sync_object", nonzero=True)
        if content.get("characters_received") is not True or content.get("stage_received") is not True:
            raise RuntimeError(f"{label} received-content flags are incomplete")
        summaries.append({
            "label": label,
            "role": role,
            "local_slot": local_slot,
            "lobby_id": lobby_id,
            "fighters": fighters,
            "stage_code": content["stage_code"],
            "stage_package": content["stage_package"],
            "stage_display_name": content["stage_display_name"],
            "loaded_package_identity": identity.lower(),
        })
    if {summary["role"] for summary in summaries} != {0, 1}:
        raise RuntimeError("observer peers did not report complementary roles")
    comparable = ("lobby_id", "fighters", "stage_code", "stage_package", "stage_display_name", "loaded_package_identity")
    for field in comparable:
        if summaries[0][field] != summaries[1][field]:
            raise RuntimeError(f"observer peers disagree on {field}")
    return {
        "validated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "map": {
            "package": expected_stage_package,
            "display_name": expected_stage_display_name,
        },
        "peers": summaries,
    }


def launch_observer_pair(spec: SandboxiePairSpec) -> str:
    run_id = "observer-" + uuid.uuid4().hex
    subprocess.Popen(spec.host_command(), close_fds=True)
    subprocess.Popen(spec.sandbox_command(), close_fds=True)
    return run_id


def stop_observer_processes(
    processes: tuple[GameProcess, ...], require_graceful: bool = True
) -> bool:
    failures: list[str] = []
    # Both authenticated peers must receive their graceful close together.
    # Waiting for one online process to exit while its counterpart remains in
    # the match can strand the first process until the teardown timeout.
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=max(1, len(processes))) as executor:
        closing = {
            executor.submit(close_game, process.pid): process
            for process in processes
        }
        for future, process in closing.items():
            try:
                future.result()
            except (RuntimeError, TimeoutError) as error:
                failures.append(f"PID {process.pid}: {error}")
                force_stop_game_for_cleanup(process.pid)
    survivors = list_game_processes()
    if survivors:
        for process in survivors:
            force_stop_game_for_cleanup(process.pid)
        failures.append("SC6 survived graceful paired teardown")
    if failures:
        if require_graceful:
            raise RuntimeError("; ".join(failures))
        return False
    return True
