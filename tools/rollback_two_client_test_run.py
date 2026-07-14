#!/usr/bin/env python3
"""Internal same-machine two-client rollback/netcode phase runner.

This validates already-running SC6 instances. It does not launch Steam, alter
Steam behavior, or patch SC6 networking. The runner separates the unsandboxed
HorseMod Saved root from Sandboxie mirrored Saved roots and writes one rollback
lab request per root.

Use rollback_two_client_acceptance_run.py for normal rollback test runs. This
script is intentionally kept as the acceptance runner's per-phase worker so
each phase can produce a focused report.
"""

from __future__ import annotations

import argparse
import atexit
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

from sc6_launch_catalog import (
    LaunchSelectionError,
    resolve_character,
    resolve_stage,
)


def configure_stdio() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is None:
            continue
        try:
            reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


configure_stdio()


REPO = Path(__file__).resolve().parents[1]
GAME_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
)
SAVED_RELATIVE = Path(
    r"SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved"
)
HOST_SAVED_DIR = GAME_EXE.parent / "ue4ss" / "Mods" / "HorseMod" / "Saved"
REPORT_DIR = REPO / "reports" / "rollback_two_client"
OBSERVE_REPORT_DIR = REPO / "reports" / "rollback_gameflow_observe"
TRACE_RE = re.compile(r"replay_trace_\d{8}_\d{6}_pid(\d+)\.jsonl$", re.I)
CMD_PORT_RE = re.compile(r'(?i)(?:^|\s)"?-Port=(\d+)"?')
CMD_QUERY_PORT_RE = re.compile(r'(?i)(?:^|\s)"?-QueryPort=(\d+)"?')
CRASH_TITLE_RE = re.compile(
    r"(?i)(crash|crashed|fatal error|assertion failed|"
    r"unreal.*reporter|stopped working)"
)
SC6_TEXT_RE = re.compile(r"(?i)(soulcalibur|soulcaliburvi|ue4)")
CRASH_PROCESS_NAMES = {
    "crashreportclient.exe",
    "crashreportclient-win64-shipping.exe",
    "werfault.exe",
}
DEFAULT_SANDBOX_BOX = r"prest\sc67"
DEFAULT_SANDBOX_QUERY_PORT = 27012
STEAM_UDP_PORT = 27036
HORSE_UDP_BASE_PORT = 47160
HOST_HORSE_UDP_PORT = HORSE_UDP_BASE_PORT
SANDBOX_HORSE_UDP_PORT = HORSE_UDP_BASE_PORT + 1
DEFAULT_ACTIVATION_SOURCE_PEER = 0xA0
DEFAULT_ACTIVATION_DESTINATION_PEER = 0xB0
DEFAULT_ACTIVATION_SESSION_ID = 0x4C495645414354
DEFAULT_REPLAY_INPUT_FILE = (
    REPO / "ReplayExample" / "REPLAY_12744704008398858106.bin"
)
# Cooked DB_MainMenuList proves Network is root index 4 and Player Match is
# Network submenu index 1.  Preserve the two hierarchy commits; a flat run of
# Downs never enters the Network submenu.
DEFAULT_MAIN_MENU_PLAYER_MATCH_ROUTE = (
    "Down,Down,Down,Down,Decide,Down,Decide"
)
WINDOWS_FILETIME_UNIX_EPOCH_100NS = 116444736000000000
PROCESS_MARKER_TOLERANCE_100NS = 10_000_000
RUNNER_PHASE_POLL_SECONDS = 1.0
# Preserve the existing thirty-second hung-window confirmation period after
# reducing expensive trace/process polling from two checks per second to one.
HUNG_WINDOW_FAIL_POLLS = 30
DIRECT_RELEASE_HOST_ADVERTISE_TIMEOUT_SECONDS = 180.0
DIRECT_RELEASE_HOST_ADVERTISE_SETTLE_SECONDS = 0.0
RUNNER_REQUEST_PROTOCOL_VERSION = 2
ACTIVE_SCENE_STABLE_OBSERVATIONS = 2
PROCESS_MISSING_FAIL_POLLS = 3

MIRRORED_VERSUS_PHASES = (
    "horse-udp-ready",
    "mirrored-versus-setup",
    "mirrored-versus-battle",
    "rollback-proof",
    "soak",
)
MIRRORED_VERSUS_RUNTIME_PHASE = "mirrored-versus"
ROLLBACK_PRODUCTION_ACTIVE_STATE = 5

HORSE_UDP_REQUIRED_TRUE_GATES = [
    "dependency_enabled",
    "wsa_started",
    "sockets_open",
    "bound_loopback",
    "nonblocking",
    "manual_udp_roundtrip",
    "wrong_endpoint_rejected",
    "wrong_source_rejected",
    "wrong_destination_rejected",
    "wrong_session_rejected",
    "create_ok",
    "adapter_set",
    "start_ok",
    "actors_ok",
    "saw_player_connected",
    "saw_session_started",
    "saw_save",
    "saw_load",
    "saw_advance",
    "saw_rollback_advance",
    "no_desync",
    "callbacks_sent",
    "callbacks_received",
    "callbacks_freed",
    "bidirectional_payloads",
    "bridge_roundtrip",
    "bridge_metadata_accepted",
    "gameplay_inputs_decoded",
    "gameplay_slots_present",
    "gameplay_inputs_drive_state",
    "final_checksums_match",
    "destroy_ok",
]

HORSE_UDP_REQUIRED_POSITIVE_COUNTERS = [
    "frames_submitted",
    "packets_sent",
    "packets_received",
    "free_calls",
    "bridge_packets_encoded",
    "bridge_packets_decoded",
    "gameplay_decoded_events",
    "gameplay_decoded_inputs",
]

HORSE_UDP_REQUIRED_ZERO_ERRORS = [
    "wsa_startup_error",
    "socket_error",
    "bind_error",
    "getsockname_error",
    "ioctlsocket_error",
    "sendto_error",
    "recvfrom_error",
]


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def run_powershell_json(script: str) -> list[dict[str, Any]]:
    proc = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            script,
        ],
        cwd=str(REPO),
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        return [
            {
                "error": "powershell-failed",
                "returncode": proc.returncode,
                "stderr": proc.stderr.strip(),
            }
        ]
    text = proc.stdout.strip()
    if not text:
        return []
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        return [{"error": "json-decode-failed", "message": str(exc), "raw": text}]
    if isinstance(data, list):
        return [x for x in data if isinstance(x, dict)]
    if isinstance(data, dict):
        return [data]
    return []


def int_value(value: Any, fallback: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return fallback
    return fallback


def choose_role_override(common: int | None, specific: int | None) -> int:
    if specific is not None:
        return specific
    if common is not None:
        return common
    return -1


def fnv1a64(text: str) -> int:
    h = 1469598103934665603
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h or 1


def fnv1a64_bytes(data: bytes) -> int:
    h = 1469598103934665603
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h or 1


def file_fnv1a64(path: Path) -> int:
    try:
        return fnv1a64_bytes(path.read_bytes())
    except OSError:
        return 0


def normalized_path_text(value: Any) -> str:
    text = str(value or "")
    if not text:
        return ""
    return os.path.normcase(os.path.abspath(text))


def enumerate_processes() -> list[dict[str, Any]]:
    try:
        import psutil  # type: ignore

        rows: list[dict[str, Any]] = []
        wanted = {
            "soulcaliburvi.exe",
            "steam.exe",
            *CRASH_PROCESS_NAMES,
        }
        for proc in psutil.process_iter([
            "pid",
            "ppid",
            "name",
            "exe",
            "cmdline",
            "create_time",
        ]):
            try:
                info = proc.info
                name = str(info.get("name") or "")
                if name.lower() not in wanted:
                    continue
                cmdline = info.get("cmdline") or []
                if isinstance(cmdline, (list, tuple)):
                    command_line = subprocess.list2cmdline(
                        [str(part) for part in cmdline]
                    )
                else:
                    command_line = str(cmdline or "")
                create_time = float(info.get("create_time") or 0.0)
                unix_ms = int(create_time * 1000.0) if create_time > 0 else 0
                rows.append({
                    "pid": int(info.get("pid") or proc.pid),
                    "parent_pid": int(info.get("ppid") or 0),
                    "name": name,
                    "path": str(info.get("exe") or ""),
                    "command_line": command_line,
                    "creation_date": f"/Date({unix_ms})/" if unix_ms else "",
                })
            except (psutil.Error, OSError, ValueError):
                continue
        return rows
    except ImportError:
        pass

    script = r"""
$items = Get-CimInstance Win32_Process |
  Where-Object {
    $_.Name -eq 'SoulcaliburVI.exe' -or
    $_.Name -eq 'steam.exe' -or
    $_.Name -eq 'CrashReportClient.exe' -or
    $_.Name -eq 'CrashReportClient-Win64-Shipping.exe' -or
    $_.Name -eq 'WerFault.exe'
  } |
  Select-Object `
    @{Name='pid';Expression={$_.ProcessId}},
    @{Name='parent_pid';Expression={$_.ParentProcessId}},
    @{Name='name';Expression={$_.Name}},
    @{Name='path';Expression={$_.ExecutablePath}},
    @{Name='command_line';Expression={$_.CommandLine}},
    @{Name='creation_date';Expression={$_.CreationDate}}
$items | ConvertTo-Json -Depth 4
"""
    return run_powershell_json(script)


def enumerate_udp_endpoints() -> list[dict[str, Any]]:
    try:
        proc = subprocess.run(
            ["netstat", "-ano", "-p", "udp"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=20,
            check=False,
        )
        if proc.returncode == 0:
            rows: list[dict[str, Any]] = []
            for line in proc.stdout.splitlines():
                parts = line.split()
                if len(parts) < 4 or parts[0].upper() != "UDP":
                    continue
                local = parts[1]
                pid = int_value(parts[-1], -1)
                if pid < 0:
                    continue
                if local.startswith("["):
                    end = local.rfind("]:")
                    if end < 0:
                        continue
                    address = local[1:end]
                    port = int_value(local[end + 2:], -1)
                else:
                    if ":" not in local:
                        continue
                    address, port_text = local.rsplit(":", 1)
                    port = int_value(port_text, -1)
                if port < 0:
                    continue
                rows.append({
                    "local_address": address,
                    "local_port": port,
                    "owning_process": pid,
                })
            return rows
    except (OSError, subprocess.SubprocessError):
        pass

    script = r"""
$items = Get-NetUDPEndpoint |
  Select-Object `
    @{Name='local_address';Expression={$_.LocalAddress}},
    @{Name='local_port';Expression={$_.LocalPort}},
    @{Name='owning_process';Expression={$_.OwningProcess}}
$items | ConvertTo-Json -Depth 3
"""
    return run_powershell_json(script)


def endpoint_rows_for_pid(
    endpoints: list[dict[str, Any]],
    pid: int,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for row in endpoints:
        try:
            owner = int(row.get("owning_process", -1))
        except (TypeError, ValueError):
            continue
        if owner == pid:
            rows.append(row)
    return sorted(rows, key=lambda r: int(r.get("local_port") or 0))


def endpoint_rows_for_port(
    rows: list[dict[str, Any]],
    port: int,
) -> list[dict[str, Any]]:
    return [
        row for row in rows
        if int_value(row.get("local_port"), -1) == port
    ]


def endpoint_rows_except_port(
    rows: list[dict[str, Any]],
    port: int,
) -> list[dict[str, Any]]:
    return [
        row for row in rows
        if int_value(row.get("local_port"), -1) != port
    ]


def command_line_port_value(command_line: Any, pattern: re.Pattern[str]) -> int:
    match = pattern.search(str(command_line or ""))
    if not match:
        return 0
    return int_value(match.group(1), 0)


def sc6_launch_args(
    processes: list[dict[str, Any]],
    sc6_pids: list[int],
) -> list[dict[str, Any]]:
    by_pid = {
        int_value(proc.get("pid"), -1): proc
        for proc in processes
    }
    rows: list[dict[str, Any]] = []
    for pid in sc6_pids:
        proc = by_pid.get(pid, {})
        command_line = str(proc.get("command_line") or "")
        rows.append(
            {
                "pid": pid,
                "command_line": command_line,
                "port": command_line_port_value(command_line, CMD_PORT_RE),
                "query_port": command_line_port_value(
                    command_line, CMD_QUERY_PORT_RE
                ),
            }
        )
    return rows


def snapshot(label: str) -> dict[str, Any]:
    processes = enumerate_processes()
    endpoints = enumerate_udp_endpoints()
    sc6_pids = sorted(
        int(p["pid"])
        for p in processes
        if str(p.get("name", "")).lower() == "soulcaliburvi.exe"
        and str(p.get("pid", "")).isdigit()
    )
    steam_pids = sorted(
        int(p["pid"])
        for p in processes
        if str(p.get("name", "")).lower() == "steam.exe"
        and str(p.get("pid", "")).isdigit()
    )
    sc6_udp_by_pid = {
        str(pid): endpoint_rows_for_pid(endpoints, pid) for pid in sc6_pids
    }
    steam_udp_by_pid = {
        str(pid): endpoint_rows_for_pid(endpoints, pid) for pid in steam_pids
    }
    return {
        "label": label,
        "captured_at": now_iso(),
        "processes": processes,
        "udp_endpoints": endpoints,
        "sc6_pids": sc6_pids,
        "steam_pids": steam_pids,
        "sc6_launch_args": sc6_launch_args(processes, sc6_pids),
        "sc6_udp_by_pid": sc6_udp_by_pid,
        "steam_udp_by_pid": steam_udp_by_pid,
        "steam_udp_27036_by_pid": {
            pid: endpoint_rows_for_port(rows, STEAM_UDP_PORT)
            for pid, rows in steam_udp_by_pid.items()
        },
        "steam_udp_other_by_pid": {
            pid: endpoint_rows_except_port(rows, STEAM_UDP_PORT)
            for pid, rows in steam_udp_by_pid.items()
        },
    }


def current_sc6_pids() -> set[int]:
    return set(query_current_sc6_processes()["pids"])


def direct_process_query(pid: int) -> dict[str, Any]:
    """Check one PID without relying on a full process enumeration.

    `alive=None` means the operating system query itself was inconclusive.  It
    must not be treated as evidence that the game exited.
    """
    result: dict[str, Any] = {
        "pid": pid,
        "alive": None,
        "name": "",
        "name_match": None,
        "source": "unavailable",
        "error": "",
    }
    if pid <= 0:
        result.update(alive=False, source="invalid-pid", error="invalid-pid")
        return result

    if os.name == "nt":
        try:
            import ctypes
            from ctypes import wintypes

            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            open_process = kernel32.OpenProcess
            open_process.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            open_process.restype = wintypes.HANDLE
            get_exit_code = kernel32.GetExitCodeProcess
            get_exit_code.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
            get_exit_code.restype = wintypes.BOOL
            query_image = kernel32.QueryFullProcessImageNameW
            query_image.argtypes = [
                wintypes.HANDLE,
                wintypes.DWORD,
                wintypes.LPWSTR,
                ctypes.POINTER(wintypes.DWORD),
            ]
            query_image.restype = wintypes.BOOL
            close_handle = kernel32.CloseHandle
            close_handle.argtypes = [wintypes.HANDLE]
            close_handle.restype = wintypes.BOOL

            # PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE
            handle = open_process(0x1000 | 0x00100000, False, pid)
            if not handle:
                error = ctypes.get_last_error()
                result.update(
                    alive=False if error == 87 else None,
                    source="win32-open-process",
                    error=f"OpenProcess:{error}",
                )
                return result
            try:
                exit_code = wintypes.DWORD(0)
                if not get_exit_code(handle, ctypes.byref(exit_code)):
                    error = ctypes.get_last_error()
                    result.update(
                        source="win32-exit-code",
                        error=f"GetExitCodeProcess:{error}",
                    )
                    return result
                alive = int(exit_code.value) == 259  # STILL_ACTIVE
                name = ""
                if alive:
                    capacity = wintypes.DWORD(32768)
                    buffer = ctypes.create_unicode_buffer(capacity.value)
                    if query_image(handle, 0, buffer, ctypes.byref(capacity)):
                        name = Path(buffer.value).name
                result.update(
                    alive=alive,
                    name=name,
                    name_match=(
                        name.lower() == "soulcaliburvi.exe" if name else None
                    ),
                    source="win32-direct",
                )
                return result
            finally:
                close_handle(handle)
        except (AttributeError, ImportError, OSError, ValueError) as exc:
            result.update(source="win32-direct", error=repr(exc))
            return result

    try:
        import psutil  # type: ignore

        proc = psutil.Process(pid)
        alive = bool(proc.is_running()) and proc.status() != psutil.STATUS_ZOMBIE
        name = str(proc.name() or "") if alive else ""
        result.update(
            alive=alive,
            name=name,
            name_match=(name.lower() == "soulcaliburvi.exe" if name else None),
            source="psutil-direct",
        )
    except ImportError:
        try:
            os.kill(pid, 0)
            result.update(alive=True, source="os-kill-direct")
        except ProcessLookupError:
            result.update(alive=False, source="os-kill-direct")
        except (OSError, PermissionError) as exc:
            result.update(source="os-kill-direct", error=repr(exc))
    except Exception as exc:  # psutil has platform-specific exception classes.
        no_such = exc.__class__.__name__ in {"NoSuchProcess", "ZombieProcess"}
        result.update(
            alive=False if no_such else None,
            source="psutil-direct",
            error=repr(exc),
        )
    return result


def query_current_sc6_processes(
    expected_pids: set[int] | None = None,
) -> dict[str, Any]:
    """Return process presence together with whether the query is trustworthy."""
    rows = enumerate_processes()
    enumeration_errors = [
        dict(row) for row in rows if row.get("error")
    ]
    enumeration_valid = not enumeration_errors
    enumerated = {
        int_value(proc.get("pid"), -1)
        for proc in rows
        if str(proc.get("name", "")).lower() == "soulcaliburvi.exe"
        and int_value(proc.get("pid"), -1) > 0
    }
    expected = {pid for pid in (expected_pids or set()) if pid > 0}
    direct_checks = {
        str(pid): direct_process_query(pid) for pid in sorted(expected)
    }
    pids = set(enumerated)
    for pid in expected:
        direct = direct_checks[str(pid)]
        if direct.get("alive") is True and direct.get("name_match") is not False:
            pids.add(pid)
        elif direct.get("alive") is False or direct.get("name_match") is False:
            pids.discard(pid)
    direct_conclusive = bool(expected) and all(
        direct_checks[str(pid)].get("alive") is not None for pid in expected
    )
    valid = enumeration_valid or direct_conclusive
    return {
        "valid": valid,
        "pids": sorted(pids),
        "enumeration_valid": enumeration_valid,
        "enumeration_errors": enumeration_errors,
        "direct_checks": direct_checks,
        "source": (
            "enumeration+direct" if enumeration_valid and expected
            else "enumeration" if enumeration_valid
            else "direct" if direct_conclusive
            else "unavailable"
        ),
    }


def update_process_presence(
    query: dict[str, Any],
    expected_pids: set[int],
    consecutive_misses: dict[int, int],
    *,
    fail_after: int = PROCESS_MISSING_FAIL_POLLS,
) -> set[int]:
    """Debounce disappearance while never turning a query error into an exit."""
    observed = {int_value(pid, -1) for pid in query.get("pids", [])}
    query_valid = bool(query.get("valid"))
    for pid in expected_pids:
        if pid in observed:
            consecutive_misses[pid] = 0
        elif query_valid:
            consecutive_misses[pid] = consecutive_misses.get(pid, 0) + 1
    effective = set(observed)
    for pid in expected_pids:
        if not query_valid or consecutive_misses.get(pid, 0) < fail_after:
            effective.add(pid)
    query["consecutive_misses"] = {
        str(pid): consecutive_misses.get(pid, 0) for pid in sorted(expected_pids)
    }
    query["missing_fail_polls"] = fail_after
    query["effective_pids"] = sorted(effective)
    return effective


def enumerate_top_level_windows() -> list[dict[str, Any]]:
    if os.name != "nt":
        return []
    try:
        import ctypes
        from ctypes import wintypes
    except (ImportError, OSError):
        return []

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    enum_windows = user32.EnumWindows
    enum_windows.argtypes = [
        ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM),
        wintypes.LPARAM,
    ]
    enum_windows.restype = wintypes.BOOL
    get_window_text = user32.GetWindowTextW
    get_window_text.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    get_window_text.restype = ctypes.c_int
    get_window_text_length = user32.GetWindowTextLengthW
    get_window_text_length.argtypes = [wintypes.HWND]
    get_window_text_length.restype = ctypes.c_int
    get_class_name = user32.GetClassNameW
    get_class_name.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    get_class_name.restype = ctypes.c_int
    get_window_thread_process_id = user32.GetWindowThreadProcessId
    get_window_thread_process_id.argtypes = [
        wintypes.HWND,
        ctypes.POINTER(wintypes.DWORD),
    ]
    get_window_thread_process_id.restype = wintypes.DWORD
    is_window_visible = user32.IsWindowVisible
    is_window_visible.argtypes = [wintypes.HWND]
    is_window_visible.restype = wintypes.BOOL
    is_hung_app_window = getattr(user32, "IsHungAppWindow", None)
    if is_hung_app_window is not None:
        is_hung_app_window.argtypes = [wintypes.HWND]
        is_hung_app_window.restype = wintypes.BOOL

    rows: list[dict[str, Any]] = []
    callback_type = ctypes.WINFUNCTYPE(
        wintypes.BOOL, wintypes.HWND, wintypes.LPARAM
    )

    def read_window_text(hwnd: Any) -> str:
        length = max(0, int(get_window_text_length(hwnd)))
        if length <= 0:
            return ""
        buf = ctypes.create_unicode_buffer(length + 1)
        n = int(get_window_text(hwnd, buf, length + 1))
        return buf.value[:n]

    def read_class_name(hwnd: Any) -> str:
        buf = ctypes.create_unicode_buffer(256)
        n = int(get_class_name(hwnd, buf, len(buf)))
        return buf.value[:n]

    @callback_type
    def callback(hwnd: Any, _: Any) -> bool:
        if not bool(is_window_visible(hwnd)):
            return True
        pid = wintypes.DWORD(0)
        get_window_thread_process_id(hwnd, ctypes.byref(pid))
        title = read_window_text(hwnd)
        cls = read_class_name(hwnd)
        if not title and not cls:
            return True
        rows.append(
            {
                "pid": int(pid.value),
                "title": title,
                "class_name": cls,
                "hung": bool(is_hung_app_window(hwnd))
                if is_hung_app_window is not None else False,
            }
        )
        return True

    enum_windows(callback, 0)
    return rows


def enumerate_crash_indicators(expected_pids: set[int]) -> list[dict[str, Any]]:
    processes = enumerate_processes()
    proc_by_pid = {
        int_value(proc.get("pid"), -1): proc for proc in processes
    }
    out: list[dict[str, Any]] = []
    seen: set[tuple[int, str, str]] = set()

    def add_indicator(
        *,
        pid: int,
        related_pid: int,
        source: str,
        title: str = "",
        class_name: str = "",
        process_name: str = "",
        command_line: str = "",
    ) -> None:
        key = (pid, source, title[:120])
        if key in seen:
            return
        seen.add(key)
        out.append(
            {
                "pid": pid,
                "related_pid": related_pid,
                "source": source,
                "title": title,
                "class_name": class_name,
                "process_name": process_name,
                "command_line": command_line[:500],
            }
        )

    for win in enumerate_top_level_windows():
        pid = int_value(win.get("pid"), -1)
        title = str(win.get("title") or "")
        class_name = str(win.get("class_name") or "")
        proc = proc_by_pid.get(pid, {})
        process_name = str(proc.get("name") or "")
        process_lower = process_name.lower()
        if pid in expected_pids and bool(win.get("hung")):
            add_indicator(
                pid=pid,
                related_pid=pid,
                source="hung-window",
                title=title,
                class_name=class_name,
                process_name=process_name,
                command_line=str(proc.get("command_line") or ""),
            )
            continue
        if not CRASH_TITLE_RE.search(title):
            continue
        if pid in expected_pids:
            add_indicator(
                pid=pid,
                related_pid=pid,
                source="window",
                title=title,
                class_name=class_name,
                process_name=process_name,
                command_line=str(proc.get("command_line") or ""),
            )
            continue
        if process_lower in CRASH_PROCESS_NAMES and (
            SC6_TEXT_RE.search(title)
            or SC6_TEXT_RE.search(str(proc.get("command_line") or ""))
            or int_value(proc.get("parent_pid"), -1) in expected_pids
        ):
            related = int_value(proc.get("parent_pid"), -1)
            add_indicator(
                pid=pid,
                related_pid=related if related in expected_pids else pid,
                source="window",
                title=title,
                class_name=class_name,
                process_name=process_name,
                command_line=str(proc.get("command_line") or ""),
            )

    for proc in processes:
        pid = int_value(proc.get("pid"), -1)
        process_name = str(proc.get("name") or "")
        process_lower = process_name.lower()
        if process_lower not in CRASH_PROCESS_NAMES:
            continue
        command_line = str(proc.get("command_line") or "")
        path = str(proc.get("path") or "")
        parent_pid = int_value(proc.get("parent_pid"), -1)
        if (
            parent_pid in expected_pids
            or SC6_TEXT_RE.search(command_line)
            or SC6_TEXT_RE.search(path)
        ):
            add_indicator(
                pid=pid,
                related_pid=parent_pid if parent_pid in expected_pids else pid,
                source="process",
                process_name=process_name,
                command_line=command_line,
            )
    return out


def process_by_pid(snapshot_data: dict[str, Any], pid: int) -> dict[str, Any] | None:
    for proc in snapshot_data.get("processes", []):
        if int_value(proc.get("pid"), -1) == pid:
            return proc
    return None


def endpoint_rows_for_global_port(
    endpoints: list[dict[str, Any]],
    port: int,
) -> list[dict[str, Any]]:
    return [
        row for row in endpoints
        if int_value(row.get("local_port"), -1) == port
    ]


def normalize_sandbox_box(value: str) -> str:
    return value.strip().replace("/", "\\").strip("\\").lower()


def sandbox_saved_roots(sandbox_root: Path, sandbox_box: str) -> list[Path]:
    if not sandbox_root.exists():
        return []
    drive = GAME_EXE.drive.rstrip(":")
    pattern = (
        f"*/*/drive/{drive}/"
        "SteamLibrary/steamapps/common/SoulcaliburVI/SoulcaliburVI/"
        "Binaries/Win64/ue4ss/Mods/HorseMod/Saved"
    )
    roots = [p for p in sandbox_root.glob(pattern) if p.exists()]
    box = normalize_sandbox_box(sandbox_box)
    if not box:
        return roots
    return [
        p for p in roots
        if normalize_sandbox_box(sandbox_box_for_root(p)) == box
    ]


def role_for_root(root: Path) -> str:
    text = str(root)
    if text.lower().startswith(r"c:\sandbox"):
        return "sandbox"
    return "host"


def sandbox_box_for_root(root: Path) -> str:
    parts = root.parts
    lowered = [p.lower() for p in parts]
    if "sandbox" not in lowered:
        return ""
    idx = lowered.index("sandbox")
    if len(parts) > idx + 2:
        return f"{parts[idx + 1]}\\{parts[idx + 2]}"
    return ""


def trace_files(root: Path) -> list[Path]:
    trace_dir = root / "ReplayTrace"
    if not trace_dir.exists():
        return []
    return sorted(
        trace_dir.glob("replay_trace_*.jsonl"),
        key=lambda p: p.stat().st_mtime if p.exists() else 0,
    )


def trace_file_offsets(root: Path) -> dict[str, int]:
    offsets: dict[str, int] = {}
    for path in trace_files(root):
        try:
            offsets[str(path)] = path.stat().st_size
        except OSError:
            continue
    return offsets


def process_start_markers(processes: list[dict[str, Any]]) -> dict[int, int]:
    markers: dict[int, int] = {}
    for proc in processes:
        if str(proc.get("name", "")).lower() != "soulcaliburvi.exe":
            continue
        pid = int_value(proc.get("pid"), -1)
        if pid < 0:
            continue
        creation = proc.get("creation_date")
        raw = ""
        if isinstance(creation, dict):
            raw = str(creation.get("value") or creation.get("DateTime") or "")
        else:
            raw = str(creation or "")
        match = re.search(r"/Date\((\d+)\)/", raw)
        if not match:
            continue
        unix_ms = int(match.group(1))
        markers[pid] = unix_ms * 10_000 + WINDOWS_FILETIME_UNIX_EPOCH_100NS
    return markers


def trace_start_marker(path: Path) -> int:
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for index, line in enumerate(f):
                if index > 16:
                    break
                if "process_start_marker" not in line:
                    continue
                event = json.loads(line)
                return int_value(event.get("process_start_marker"), 0)
    except (OSError, json.JSONDecodeError, ValueError):
        return 0
    return 0


def marker_matches_trace(expected: int, observed: int) -> bool:
    if expected <= 0 or observed <= 0:
        return True
    return abs(expected - observed) <= PROCESS_MARKER_TOLERANCE_100NS


def trace_pids(
    root: Path,
    live_pids: set[int],
    pid_markers: dict[int, int] | None = None,
) -> list[int]:
    found: set[int] = set()
    pid_markers = pid_markers or {}
    for path in trace_files(root):
        match = TRACE_RE.match(path.name)
        if not match:
            continue
        pid = int(match.group(1))
        if pid in live_pids:
            expected_marker = pid_markers.get(pid, 0)
            observed_marker = trace_start_marker(path)
            if not marker_matches_trace(expected_marker, observed_marker):
                continue
            found.add(pid)
    return sorted(found)


def discover_roots(
    sc6_pids: set[int],
    sandbox_root: Path,
    sandbox_box: str = DEFAULT_SANDBOX_BOX,
    processes: list[dict[str, Any]] | None = None,
) -> list[dict[str, Any]]:
    candidates: list[Path] = []
    if HOST_SAVED_DIR.exists():
        candidates.append(HOST_SAVED_DIR)
    candidates.extend(sandbox_saved_roots(sandbox_root, sandbox_box))

    unique: dict[str, Path] = {}
    for path in candidates:
        unique[str(path).lower()] = path

    roots: list[dict[str, Any]] = []
    pid_markers = process_start_markers(processes or [])
    for path in unique.values():
        files = trace_files(path)
        latest = files[-1] if files else None
        roots.append(
            {
                "path": str(path),
                "role": role_for_root(path),
                "sandbox_box": sandbox_box_for_root(path),
                "request_file_exists": (
                    path / "rollback_lab_request.txt"
                ).exists(),
                "live_trace_pids": trace_pids(path, sc6_pids, pid_markers),
                "trace_file_count": len(files),
                "latest_trace": str(latest) if latest else "",
                "latest_trace_mtime": (
                    latest.stat().st_mtime if latest and latest.exists() else 0
                ),
            }
        )

    sandbox_pids = {
        int_value(pid, -1)
        for root in roots
        if root.get("role") == "sandbox"
        for pid in root.get("live_trace_pids", [])
        if int_value(pid, -1) >= 0
    }
    if sandbox_pids:
        for root in roots:
            if root.get("role") != "host":
                continue
            root["live_trace_pids"] = [
                pid for pid in root.get("live_trace_pids", [])
                if int_value(pid, -1) not in sandbox_pids
            ]

    return sorted(
        roots,
        key=lambda r: (
            0 if r["role"] == "host" else 1,
            str(r["path"]).lower(),
        ),
    )


def root_label(root: dict[str, Any]) -> str:
    box = root.get("sandbox_box") or "-"
    return f"{root.get('role', '?')} box={box} path={root.get('path', '')}"


def validate_inventory(
    roots: list[dict[str, Any]],
    sc6_pids: set[int],
    sandbox_box: str,
    *,
    require_trace_pids: bool = False,
) -> list[str]:
    failures: list[str] = []
    host_roots = [r for r in roots if r.get("role") == "host"]
    sandbox_roots = [r for r in roots if r.get("role") == "sandbox"]
    expected_box = normalize_sandbox_box(sandbox_box)

    if len(host_roots) != 1:
        failures.append(f"expected exactly 1 host Saved root, got {len(host_roots)}")
    if expected_box and len(sandbox_roots) != 1:
        failures.append(
            "expected exactly 1 sandbox Saved root "
            f"for box {sandbox_box!r}, got {len(sandbox_roots)}"
        )
    elif not expected_box and not sandbox_roots:
        failures.append("expected at least 1 sandbox Saved root, got 0")

    mapped_pids: list[int] = []
    for root in roots:
        if root.get("request_file_exists"):
            failures.append(
                "stale rollback_lab_request.txt present for "
                f"{root_label(root)}"
            )
        live_pids = [
            int_value(pid, -1)
            for pid in root.get("live_trace_pids", [])
            if int_value(pid, -1) >= 0
        ]
        if require_trace_pids and len(live_pids) != 1:
            failures.append(
                "expected exactly 1 live trace PID for "
                f"{root_label(root)}, got {live_pids or '-'}"
            )
        elif not require_trace_pids and len(live_pids) > 1:
            failures.append(
                "ambiguous live trace PIDs for "
                f"{root_label(root)}, got {live_pids}"
            )
        mapped_pids.extend(live_pids)

    if mapped_pids and len(set(mapped_pids)) != len(mapped_pids):
        failures.append(f"duplicate live trace PID mapping: {mapped_pids}")
    if require_trace_pids and sorted(mapped_pids) != sorted(sc6_pids):
        failures.append(
            "live trace PID mapping does not match SC6 PIDs: "
            f"mapped={sorted(mapped_pids)} sc6={sorted(sc6_pids)}"
        )

    return failures


def role_pids_from_roots(roots: list[dict[str, Any]]) -> dict[str, int]:
    role_pids: dict[str, int] = {}
    for root in roots:
        role = str(root.get("role", ""))
        live_pids = [
            int_value(pid, -1)
            for pid in root.get("live_trace_pids", [])
            if int_value(pid, -1) >= 0
        ]
        if len(live_pids) == 1:
            role_pids[role] = live_pids[0]
    return role_pids


def validate_sc6_launch_configuration(
    snap: dict[str, Any],
    roots: list[dict[str, Any]],
    *,
    sandbox_query_port: int = DEFAULT_SANDBOX_QUERY_PORT,
) -> list[str]:
    failures: list[str] = []
    rows = [
        row for row in snap.get("sc6_launch_args", [])
        if isinstance(row, dict)
    ]
    if len(rows) != 2:
        return failures

    by_pid = {
        int_value(row.get("pid"), -1): row
        for row in rows
        if int_value(row.get("pid"), -1) >= 0
    }
    role_pids = role_pids_from_roots(roots)

    if "sandbox" not in role_pids:
        sandbox_candidates = [
            int_value(row.get("pid"), -1)
            for row in rows
            if int_value(row.get("query_port"), 0) == sandbox_query_port
        ]
        if len(sandbox_candidates) == 1:
            role_pids["sandbox"] = sandbox_candidates[0]
    if "host" not in role_pids and "sandbox" in role_pids:
        host_candidates = [
            int_value(row.get("pid"), -1)
            for row in rows
            if int_value(row.get("pid"), -1) != role_pids["sandbox"]
        ]
        if len(host_candidates) == 1:
            role_pids["host"] = host_candidates[0]

    for role in ("host", "sandbox"):
        pid = role_pids.get(role, -1)
        if pid < 0:
            failures.append(f"SC6 launch role {role} has no unique live trace PID")
            continue
        row = by_pid.get(pid)
        if row is None:
            failures.append(f"SC6 launch role {role} PID {pid} missing command line")
            continue
        port = int_value(row.get("port"), 0)
        query_port = int_value(row.get("query_port"), 0)
        if port > 0:
            failures.append(
                f"SC6 role {role} PID {pid} has unexpected -Port={port}"
            )
        if role == "host" and query_port > 0:
            failures.append(
                f"SC6 role host PID {pid} has unexpected -QueryPort={query_port}"
            )
        if role == "sandbox" and query_port != sandbox_query_port:
            failures.append(
                "SC6 role sandbox PID "
                f"{pid} expected -QueryPort={sandbox_query_port}, "
                f"got {query_port or '-'}"
            )

    query_ports: dict[int, list[int]] = {}
    for row in rows:
        pid = int_value(row.get("pid"), -1)
        query_port = int_value(row.get("query_port"), 0)
        if query_port > 0:
            query_ports.setdefault(query_port, []).append(pid)

    for port, pids in query_ports.items():
        if len(pids) > 1:
            failures.append(f"SC6 -QueryPort collision {port} pids={pids}")
    return failures


def validate_udp_safety(
    snap: dict[str, Any],
    *,
    host_sidecar_port: int,
    sandbox_sidecar_port: int,
    allow_sc6_sidecar_owners: bool = False,
) -> list[str]:
    failures: list[str] = []
    endpoints = snap.get("udp_endpoints", [])
    sc6_pids = {int_value(pid, -1) for pid in snap.get("sc6_pids", [])}
    steam_pids = {int_value(pid, -1) for pid in snap.get("steam_pids", [])}

    if host_sidecar_port == STEAM_UDP_PORT or sandbox_sidecar_port == STEAM_UDP_PORT:
        failures.append("Horse sidecar port must not be Steam UDP 27036")
    if host_sidecar_port == sandbox_sidecar_port:
        failures.append(
            f"host/sandbox Horse sidecar ports collide: {host_sidecar_port}"
        )

    steam_27036_owners: set[int] = set()
    for row in endpoint_rows_for_global_port(endpoints, STEAM_UDP_PORT):
        owner = int_value(row.get("owning_process"), -1)
        if owner in sc6_pids:
            failures.append(f"SC6 PID {owner} owns reserved Steam UDP 27036")
        elif owner in steam_pids:
            steam_27036_owners.add(owner)
        else:
            failures.append(
                f"non-Steam process PID {owner} owns reserved Steam UDP 27036"
            )
    if len(steam_27036_owners) > 1:
        failures.append(
            "multiple visible Steam processes own UDP 27036: "
            + ",".join(str(pid) for pid in sorted(steam_27036_owners))
        )

    for port in (host_sidecar_port, sandbox_sidecar_port):
        for row in endpoint_rows_for_global_port(endpoints, port):
            owner = int_value(row.get("owning_process"), -1)
            if allow_sc6_sidecar_owners and owner in sc6_pids:
                continue
            failures.append(
                f"stale Horse sidecar port {port} already owned by PID {owner}"
            )

    return failures


def request_text(
    *,
    enabled: bool,
    trace: bool,
    case: str,
    request_id: str,
    request_protocol_version: int = RUNNER_REQUEST_PROTOCOL_VERSION,
    request_generation: int = 0,
    request_phase: str = "",
    rollback_window: int,
    seed: str,
    mode: str = "",
    client_role: str = "",
    sandbox_root: str = "",
    sandbox_box: str = "",
    local_peer_id: int = 0,
    remote_peer_id: int = 0,
    sidecar_local_port: int = 0,
    sidecar_remote_port: int = 0,
    sidecar_remote_addr: str = "127.0.0.1",
    activation_arm: bool = False,
    activation_source_peer: int = DEFAULT_ACTIVATION_SOURCE_PEER,
    activation_destination_peer: int = DEFAULT_ACTIVATION_DESTINATION_PEER,
    activation_session_id: int = DEFAULT_ACTIVATION_SESSION_ID,
    activation_token: str = "",
    force_live_prediction_divergence: bool = False,
    debug_steam_probe: bool = False,
    debug_steam_filter_probe: bool = False,
    debug_direct_stage_begin_play: bool = False,
    observe_gameflow: bool = False,
    observe_gameflow_process_events: bool = False,
    online_stage_network_check_compat: bool = False,
    online_stage_join_complete_compat: bool = False,
    online_stage_transport_ready_compat: bool = False,
    online_stage_ready_open_compat: bool = False,
    online_stage_peer_route_tag_fix: bool = False,
    online_stage_in_room_transition_compat: bool = False,
    online_stage_direct_native_join_diagnostic: bool = False,
    online_stage: bool = False,
    direct_stage: bool = False,
    direct_stage_observe_only: bool = False,
    direct_connect: bool = False,
    direct_replay_input: bool = False,
    direct_correction: bool = False,
    online_stage_no_presence_find: bool = False,
    online_stage_cleanup_only: bool = False,
    online_stage_find_only: bool = False,
    online_stage_wait_host_room_ready_marker: bool = False,
    online_stage_host_room_ready_marker: str = "",
    online_stage_goal: str = "player-match-battle",
    online_stage_diagnostic_reflection: bool = False,
    online_stage_main_user_id_override: int = -1,
    online_stage_native_session_name: str = "",
    online_stage_session_name: str = "",
    online_stage_room_name: str = "",
    online_stage_target_owner_id: int = 0,
    online_stage_invite_target_id: int = 0,
    online_stage_join_lobby_id: int = 0,
    stock_join_route: str = "browser",
    skip_online_stage_drive: bool = False,
    live_replay_input: bool = False,
    replay_input_file: str = "",
    main_menu_player_match_route: str = DEFAULT_MAIN_MENU_PLAYER_MATCH_ROUTE,
    local_replay_player: int = 0,
    remote_replay_player: int = 1,
    replay_divergence_frame: int = 120,
    replay_divergence_window: int = 12,
    production_enabled: bool = False,
    bind_address: str = "0.0.0.0",
    bind_port: int = 0,
    peer_address: str = "127.0.0.1",
    peer_port: int = 0,
    local_player_slot: int = 0,
    native_input_source_slot: int = 0,
    lifecycle_mode: str = "stock-online-pvp",
    production_local_peer: int = 0,
    production_remote_peer: int = 1,
    secret: str = "",
    input_delay: int = 0,
    network_profile: str = "clean_0ms",
    fault_seed: int = 0x5C6B0001,
    expected_build_id: int = 0,
    expected_schema_id: int = 0,
    launch_left_character: int = -1,
    launch_right_character: int = -1,
    launch_stage: int = -1,
) -> str:
    lines = [
        f"enabled={1 if enabled else 0}",
        f"trace={1 if trace else 0}",
        f"case={case}",
        f"mode={mode}",
        f"window={rollback_window}",
        f"seed={seed}",
        f"request_id={request_id}",
        f"request_protocol_version={request_protocol_version}",
        f"request_generation=0x{request_generation:X}",
        f"request_phase={request_phase}",
        f"client_role={client_role}",
        f"sandbox_root={sandbox_root}",
        f"sandbox_box={sandbox_box}",
        f"local_peer_id=0x{local_peer_id:X}",
        f"remote_peer_id=0x{remote_peer_id:X}",
        f"sidecar_local_port={sidecar_local_port}",
        f"sidecar_remote_port={sidecar_remote_port}",
        f"sidecar_remote_addr={sidecar_remote_addr}",
        f"activation_arm={1 if activation_arm else 0}",
        f"activation_source_peer=0x{activation_source_peer:X}",
        f"activation_destination_peer=0x{activation_destination_peer:X}",
        f"activation_session_id=0x{activation_session_id:X}",
        f"activation_token={activation_token}",
        (
            "force_live_prediction_divergence="
            f"{1 if force_live_prediction_divergence else 0}"
        ),
        f"debug_steam_probe={1 if debug_steam_probe else 0}",
        f"debug_steam_filter_probe={1 if debug_steam_filter_probe else 0}",
        (
            "debug_direct_stage_begin_play="
            f"{1 if debug_direct_stage_begin_play else 0}"
        ),
        f"observe_gameflow={1 if observe_gameflow else 0}",
        (
            "observe_gameflow_process_events="
            f"{1 if observe_gameflow_process_events else 0}"
        ),
        (
            "online_stage_network_check_compat="
            f"{1 if online_stage_network_check_compat else 0}"
        ),
        (
            "online_stage_join_complete_compat="
            f"{1 if online_stage_join_complete_compat else 0}"
        ),
        (
            "online_stage_transport_ready_compat="
            f"{1 if online_stage_transport_ready_compat else 0}"
        ),
        (
            "online_stage_ready_open_compat="
            f"{1 if online_stage_ready_open_compat else 0}"
        ),
        (
            "online_stage_peer_route_tag_fix="
            f"{1 if online_stage_peer_route_tag_fix else 0}"
        ),
        (
            "online_stage_in_room_transition_compat="
            f"{1 if online_stage_in_room_transition_compat else 0}"
        ),
        (
            "online_stage_direct_native_join_diagnostic="
            f"{1 if online_stage_direct_native_join_diagnostic else 0}"
        ),
        f"online_stage={1 if online_stage else 0}",
        f"direct_stage={1 if direct_stage else 0}",
        f"direct_stage_observe_only={1 if direct_stage_observe_only else 0}",
        f"direct_connect={1 if direct_connect else 0}",
        f"direct_replay_input={1 if direct_replay_input else 0}",
        f"direct_correction={1 if direct_correction else 0}",
        f"online_stage_no_presence_find={1 if online_stage_no_presence_find else 0}",
        f"online_stage_cleanup_only={1 if online_stage_cleanup_only else 0}",
        f"online_stage_find_only={1 if online_stage_find_only else 0}",
        (
            "online_stage_wait_host_room_ready_marker="
            f"{1 if online_stage_wait_host_room_ready_marker else 0}"
        ),
        (
            "online_stage_host_room_ready_marker="
            f"{online_stage_host_room_ready_marker}"
        ),
        f"online_stage_goal={online_stage_goal}",
        (
            "online_stage_diagnostic_reflection="
            f"{1 if online_stage_diagnostic_reflection else 0}"
        ),
        f"online_stage_main_user_id_override={online_stage_main_user_id_override}",
        f"online_stage_native_session_name={online_stage_native_session_name}",
        f"online_stage_session_name={online_stage_session_name}",
        f"online_stage_room_name={online_stage_room_name}",
        f"online_stage_target_owner_id=0x{online_stage_target_owner_id:X}",
        f"online_stage_invite_target_id=0x{online_stage_invite_target_id:X}",
        f"online_stage_join_lobby_id=0x{online_stage_join_lobby_id:X}",
        f"stock_join_route={stock_join_route}",
        f"live_replay_input={1 if live_replay_input else 0}",
        f"replay_input_file={replay_input_file}",
        f"main_menu_player_match_route={main_menu_player_match_route}",
        f"local_replay_player={local_replay_player}",
        f"remote_replay_player={remote_replay_player}",
        f"replay_divergence_frame={replay_divergence_frame}",
        f"replay_divergence_window={replay_divergence_window}",
        f"production_enabled={1 if production_enabled else 0}",
        f"bind_address={bind_address}",
        f"bind_port={bind_port}",
        f"peer_address={peer_address}",
        f"peer_port={peer_port}",
        f"local_player_slot={local_player_slot}",
        f"native_input_source_slot={native_input_source_slot}",
        f"lifecycle_mode={lifecycle_mode}",
        f"production_local_peer={production_local_peer}",
        f"production_remote_peer={production_remote_peer}",
        f"secret={secret}",
        f"input_delay={input_delay}",
        f"network_profile={network_profile}",
        f"fault_seed=0x{fault_seed:X}",
        f"expected_build_id=0x{expected_build_id:X}",
        f"expected_schema_id=0x{expected_schema_id:X}",
        f"launch_left_character={launch_left_character}",
        f"launch_right_character={launch_right_character}",
        f"launch_stage={launch_stage}",
    ]
    return "\n".join(lines) + "\n"


def write_request_file(root: Path, text: str) -> str:
    root.mkdir(parents=True, exist_ok=True)
    target = root / "rollback_lab_request.txt"
    tmp = target.with_name(
        f".{target.name}.{os.getpid()}.{time.time_ns()}.tmp"
    )
    try:
        tmp.write_text(text, encoding="utf-8", newline="\n")
        os.replace(tmp, target)
    finally:
        try:
            tmp.unlink()
        except FileNotFoundError:
            pass
    return str(target)


def remove_request_file(root: Path) -> None:
    try:
        (root / "rollback_lab_request.txt").unlink()
    except FileNotFoundError:
        pass


def safe_marker_name(value: str) -> str:
    cleaned = "".join(
        ch if ch.isalnum() or ch in "._-" else "_"
        for ch in value
    ).strip("._-")
    return cleaned or "default"


def host_room_ready_marker_name(run_id: str) -> str:
    return f"rollback_host_room_ready_{safe_marker_name(run_id)}.ready"


def host_room_ready_marker_path(root: Path, marker_name: str) -> Path:
    return root / marker_name


def write_host_room_ready_marker(root: Path, marker_name: str) -> str:
    marker = host_room_ready_marker_path(root, marker_name)
    tmp = marker.with_name(
        f".{marker.name}.{os.getpid()}.{time.time_ns()}.tmp"
    )
    try:
        root.mkdir(parents=True, exist_ok=True)
        tmp.write_text("ready\n", encoding="utf-8", newline="\n")
        os.replace(tmp, marker)
    finally:
        try:
            tmp.unlink()
        except FileNotFoundError:
            pass
    return str(marker)


def remove_host_room_ready_marker(root: Path, marker_name: str) -> None:
    try:
        host_room_ready_marker_path(root, marker_name).unlink()
    except FileNotFoundError:
        pass


def write_host_room_ready_markers(
    roots: list[Path],
    marker_name: str,
) -> list[str]:
    written: list[str] = []
    seen: set[str] = set()
    for root in roots:
        key = str(root).casefold()
        if key in seen:
            continue
        seen.add(key)
        written.append(write_host_room_ready_marker(root, marker_name))
    return written


def read_jsonl_events(
    root: Path,
    request_id: str,
    *,
    min_mtime: float = 0.0,
    allowed_pids: set[int] | None = None,
    file_offsets: dict[str, int] | None = None,
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    request_id_bytes = request_id.encode("utf-8")
    for path in trace_files(root):
        offset = int_value((file_offsets or {}).get(str(path)), 0)
        try:
            if min_mtime and path.stat().st_mtime < min_mtime - 2.0:
                continue
        except OSError:
            continue
        try:
            f = path.open("rb")
        except OSError:
            continue
        with f:
            try:
                if offset > 0:
                    f.seek(offset)
            except OSError:
                pass
            for raw_line in f:
                if request_id_bytes not in raw_line:
                    continue
                line = raw_line.decode("utf-8", errors="replace")
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("request_id") != request_id:
                    continue
                pid = int_value(event.get("pid"), -1)
                if allowed_pids is not None and pid not in allowed_pids:
                    continue
                event["_trace_file"] = str(path)
                events.append(event)
    return events


def latest_event(
    events: list[dict[str, Any]],
    name: str,
) -> dict[str, Any] | None:
    matches = [e for e in events if e.get("event") == name]
    if not matches:
        return None
    return matches[-1]


def perform_phase_cleanup(
    roots: list[dict[str, Any]],
    *,
    run_id: str,
    phase: str,
    generation: int,
    marker_name: str,
    allowed_pids: set[int],
    rollback_window: int,
    seed: str,
    wait_seconds: float = 10.0,
) -> dict[str, Any]:
    """Explicitly disable the lab configuration and remove phase artifacts."""
    started = time.time()
    records: list[dict[str, Any]] = []
    pending: dict[str, dict[str, Any]] = {}
    for index, root in enumerate(roots):
        root_path = Path(root["path"])
        remove_host_room_ready_marker(root_path, marker_name)
        remove_request_file(root_path)
        cleanup_generation = (generation + index + 1) & 0xFFFFFFFFFFFFFFFF
        cleanup_id = (
            f"two-client-v{RUNNER_REQUEST_PROTOCOL_VERSION}-"
            f"g{cleanup_generation:016x}-{run_id}-cleanup-"
            f"{safe_marker_name(str(root.get('role') or index))}"
        )
        offsets = trace_file_offsets(root_path)
        request_path = write_request_file(
            root_path,
            request_text(
                enabled=False,
                trace=False,
                case="baseline-oracle",
                request_id=cleanup_id,
                request_protocol_version=RUNNER_REQUEST_PROTOCOL_VERSION,
                request_generation=cleanup_generation,
                request_phase=f"{phase}:cleanup",
                rollback_window=rollback_window,
                seed=seed,
                client_role=str(root.get("role") or ""),
            ),
        )
        record = {
            "root": str(root_path),
            "role": str(root.get("role") or ""),
            "request_id": cleanup_id,
            "request_generation": cleanup_generation,
            "request_path": request_path,
            "request_consumed": False,
            "configured_event_acknowledged": False,
            "acknowledgement": "pending",
            "residual_request_removed": False,
            "marker_removed": not host_room_ready_marker_path(
                root_path, marker_name
            ).exists(),
        }
        records.append(record)
        pending[str(root_path)] = {
            "root": root_path,
            "record": record,
            "offsets": offsets,
        }

    deadline = time.time() + max(0.0, wait_seconds)
    while pending and time.time() <= deadline:
        completed: list[str] = []
        for key, state in pending.items():
            root_path = state["root"]
            record = state["record"]
            target = root_path / "rollback_lab_request.txt"
            consumed = not target.exists()
            events = read_jsonl_events(
                root_path,
                str(record["request_id"]),
                min_mtime=started,
                allowed_pids=allowed_pids or None,
                file_offsets=state["offsets"],
            )
            configured = latest_event(events, "rollback_lab_configured")
            record["request_consumed"] = consumed
            record["configured_event_acknowledged"] = configured is not None
            if configured is not None:
                record["acknowledgement"] = "configured-event"
                record["configured"] = configured
                completed.append(key)
            elif consumed:
                record["acknowledgement"] = "request-consumed"
                completed.append(key)
        for key in completed:
            pending.pop(key, None)
        if pending and time.time() < deadline:
            time.sleep(0.05)

    for record in records:
        root_path = Path(record["root"])
        target = root_path / "rollback_lab_request.txt"
        if target.exists():
            remove_request_file(root_path)
            record["residual_request_removed"] = True
        remove_host_room_ready_marker(root_path, marker_name)
        record["request_artifact_absent"] = not target.exists()
        record["marker_removed"] = not host_room_ready_marker_path(
            root_path, marker_name
        ).exists()
        record["command_acknowledged"] = bool(
            record["request_consumed"]
            or record["configured_event_acknowledged"]
        )
        record["ok"] = bool(
            record["request_artifact_absent"]
            and record["marker_removed"]
            and record["command_acknowledged"]
        )
    return {
        "attempted": True,
        "enforced": True,
        "protocol_version": RUNNER_REQUEST_PROTOCOL_VERSION,
        "phase": phase,
        "elapsed_seconds": round(time.time() - started, 3),
        "records": records,
        "artifacts_removed": all(
            record["request_artifact_absent"] and record["marker_removed"]
            for record in records
        ),
        "all_commands_acknowledged": all(
            record["command_acknowledged"] for record in records
        ),
        "ok": all(record["ok"] for record in records),
    }


def merge_online_stage_callbacks(
    online_stage: dict[str, Any] | None,
    callbacks: list[dict[str, Any]],
) -> dict[str, Any] | None:
    if online_stage is None or not callbacks:
        return online_stage
    merged = dict(online_stage)
    for callback in callbacks:
        function = str(callback.get("function", ""))
        result = bool(callback.get("result"))
        true_count = int_value(callback.get("true_count"), 0)
        false_count = int_value(callback.get("false_count"), 0)
        merged["latest_callback_function"] = function
        merged["latest_callback_result"] = result
        merged["latest_callback_trace_file"] = callback.get("_trace_file", "")
        if function == "OnCreateSession":
            merged["create_callback_seen"] = True
            merged["create_callback_result"] = result
            merged["create_callback_true_count"] = true_count
            merged["create_callback_false_count"] = false_count
        elif function == "OnDestroySession":
            merged["destroy_callback_seen"] = True
            merged["destroy_callback_result"] = result
            merged["destroy_callback_true_count"] = true_count
            merged["destroy_callback_false_count"] = false_count
        elif function == "OnJoinSession":
            merged["join_complete_seen"] = True
            merged["join_complete_result"] = result
            merged["join_complete_result_type"] = int_value(
                callback.get("result_type"), -1
            )
            merged["join_callback_true_count"] = true_count
            merged["join_callback_false_count"] = false_count
        elif function == "OnSessionConnectComplete":
            merged["session_connect_complete_seen"] = True
            merged["session_connect_complete_result"] = result
            merged["session_connect_complete_full_member_error"] = bool(
                callback.get("full_member_error")
            )
            merged["session_connect_callback_true_count"] = true_count
            merged["session_connect_callback_false_count"] = false_count
        elif function == "OnSessionMemberJoin":
            merged["session_member_join_seen"] = True
            merged["session_member_join_count"] = int_value(
                callback.get("count"), true_count + false_count
            )
    return merged


def direct_release_host_advertise_summary(
    stage: dict[str, Any] | None,
) -> dict[str, Any]:
    if not stage:
        return {}
    keys = (
        "_trace_file",
        "latest_callback_function",
        "latest_callback_result",
        "ok",
        "client_role",
        "native_session_name",
        "session_name",
        "room_name",
        "target_owner_id",
        "current_scene_class",
        "current_scene_name",
        "next_scene_class",
        "next_scene_name",
        "failure",
        "player_match_scene_requested",
        "player_match_scene_request_attempts",
        "player_match_scene_last_request_nav_attempt",
        "player_match_scene_last_request_ok",
        "player_match_scene_ready",
        "player_match_state_requested",
        "player_match_state_request_ok",
        "player_match_state_request_tick",
        "player_match_poll_requested",
        "player_match_poll_ok",
        "player_match_poll_count",
        "player_match_in_room_requested",
        "player_match_in_room_ok",
        "player_match_in_room_poll_ok",
        "player_match_in_room_poll_count",
        "player_match_in_room_state",
        "player_match_in_room_state_name",
        "player_match_in_room_state_class",
        "player_match_in_room_enable_ready_query_ok",
        "player_match_in_room_enable_ready",
        "player_match_in_room_session_connecting_query_ok",
        "player_match_in_room_session_connecting",
        "player_match_in_room_failure",
        "online_nav_reason",
        "online_nav_transition",
        "gameflow_input_attempts",
        "main_menu_input_attempts",
        "title_navigation_generation",
        "title_navigation_dispatch_id",
        "title_navigation_pulse_generation",
        "title_navigation_lease_generation",
        "title_navigation_state",
        "title_navigation_failure",
        "title_navigation_step_attempts",
        "title_navigation_total_dispatches",
        "title_navigation_action_observed",
        "title_scene_stable_ticks",
        "title_scene_required_stable_ticks",
        "title_scene_stable",
        "title_navigation_semantic_acknowledged",
        "title_navigation_target_scene_queued",
        "title_navigation_native_method",
        "title_navigation_native_dispatches",
        "title_navigation_last_native_dispatch_ok",
        "title_xinput_lease_generation",
        "title_xinput_lease_state_reads",
        "title_xinput_lease_forced_state_reads",
        "title_xinput_lease_active",
        "online_nav_root_cache_hits",
        "online_nav_root_validations",
        "online_nav_root_searches",
        "online_nav_automation_cache_hits",
        "online_nav_automation_searches",
        "online_nav_identity_cache_hits",
        "online_nav_identity_cache_misses",
        "main_menu_navigation_generation",
        "main_menu_navigation_dispatch_id",
        "main_menu_navigation_pulse_generation",
        "main_menu_navigation_lease_generation",
        "main_menu_navigation_state",
        "main_menu_navigation_failure",
        "main_menu_navigation_step_attempts",
        "main_menu_navigation_total_dispatches",
        "main_menu_xinput_phase",
        "main_menu_xinput_failure",
        "main_menu_xinput_state_reads",
        "main_menu_xinput_force_rescan_writes",
        "main_menu_xinput_lease_generation",
        "main_menu_xinput_lease_state_reads",
        "main_menu_xinput_lease_forced_state_reads",
        "main_menu_xinput_lease_active",
        "main_menu_xinput_native_poller_verified",
        "main_menu_xinput_native_poller_hooked",
        "main_menu_input_sequence_step",
        "main_menu_input_sequence_complete",
        "main_menu_input_last_ok",
        "main_menu_input_last_key",
        "main_menu_input_last_reason",
        "main_menu_nav_last_action",
        "main_menu_nav_last_action_accepted",
        "main_menu_nav_last_action_transitioned",
        "main_menu_nav_last_action_attempt",
        "main_menu_nav_cooldown_remaining",
        "main_menu_player_match_route",
        "online_stage_wait_host_room_ready_marker",
        "online_stage_host_room_ready_marker",
        "host_room_ready_marker_wait_requested",
        "host_room_ready_marker_observed",
        "host_room_ready_marker_first_wait_tick",
        "host_room_ready_marker_observed_tick",
        "host_room_ready_marker_path",
        "host_room_ready_marker_failure",
        "match_data",
        "match_data_present",
        "host_create_request_ok",
        "create_callback_seen",
        "create_callback_result",
        "create_callback_true_count",
        "create_callback_false_count",
        "session_connect_complete_seen",
        "session_connect_complete_result",
        "session_connect_callback_true_count",
        "session_connect_callback_false_count",
        "session_member_join_seen",
        "session_member_join_count",
        "session_member_join_attempted_count",
        "session_member_join_first_tick",
        "session_hub_initialize_requested",
        "session_hub_initialize_ok",
        "process_event_followup_count",
        "process_event_followup_last_tick",
        "process_event_followup_last_kind",
        "native_named_session_sampled",
        "native_named_session_ok",
        "native_named_session_failure",
        "native_named_session_ptr",
        "native_named_session_info",
        "native_named_session_info_ref_controller",
        "native_named_session_lobby_id",
        "native_named_session_public_connections",
        "native_named_session_luxor_connections",
        "native_named_session_hosting_player_num",
        "native_named_session_first_sample_tick",
        "native_named_session_first_state",
        "native_named_session_first_state_byte",
        "native_named_session_state7_first_tick",
        "native_named_session_state_byte_ready_first_tick",
        "native_named_session_state_transition_count",
        "native_named_session_state",
        "native_named_session_state_byte",
        "native_named_session_state_byte_ready",
        "deferred_session_connect_attempts",
        "deferred_session_connect_last_tick",
        "deferred_session_connect_call_ok",
        "deferred_session_connect_failure",
        "connect_manager_sampled",
        "connect_manager_ok",
        "connect_manager_failure",
        "luxor_connect_manager",
        "luxor_connect_delegate_handle_array",
        "luxor_connect_delegate_handle_array_ref",
        "luxor_delegate_create_session_complete_handle",
        "luxor_delegate_slot_08_handle",
        "luxor_delegate_start_session_complete_handle",
        "luxor_delegate_destroy_session_complete_handle",
        "luxor_delegate_slot_20_handle",
        "luxor_delegate_join_session_complete_handle",
        "luxor_delegate_deferred_session_connection_handle",
        "luxor_delegate_external_ui_handle",
        "luxor_delegate_sender_handle",
        "luxor_delegate_slot_48_handle",
        "luxor_connect_binding_handle_a",
        "luxor_connect_binding_handle_b",
        "luxor_connect_sender_message_binding",
        "luxor_connect_message_binding",
        "luxor_connect_online_session_object",
        "luxor_connect_online_session_ref",
        "luxor_active_connect_object",
        "luxor_active_connect_ref",
        "luxor_active_connect_system_slot",
        "luxor_active_connect_system_ref",
        "luxor_active_connect_system_offset",
        "luxor_active_connect_system_known_interface",
        "luxor_active_connect_state",
        "luxor_active_connect_local_user_byte",
        "luxor_active_connect_sub_state",
        "luxor_active_state_flags",
        "luxor_active_session_name_raw",
        "luxor_active_session_state_update_task",
        "luxor_active_session_state_update_task_ref",
        "luxor_active_session_notify_task",
        "luxor_active_session_notify_task_ref",
        "luxor_active_session_event_handle",
        "connection_state_update_task_hook_attempted",
        "connection_state_update_task_hook_installed",
        "connection_state_update_task_calls",
        "connection_state_update_task_transitions_to_state5",
        "connection_state_update_task_last_caller_rva",
        "connection_state_update_task_last_active",
        "connection_state_update_task_last_network_calls_before",
        "connection_state_update_task_last_network_calls_after",
        "connection_state_update_task_last_delta_millis",
        "connection_state_update_task_last_state_before",
        "connection_state_update_task_last_state_after",
        "connection_state_update_task_last_sub_state_before",
        "connection_state_update_task_last_sub_state_after",
        "connection_state_update_task_last_ready_before",
        "connection_state_update_task_last_ready_after",
        "active_failed_substate9_hook_attempted",
        "active_failed_substate9_hook_installed",
        "active_failed_substate9_calls",
        "active_failed_substate9_last_caller_rva",
        "active_failed_substate9_last_pointer",
        "active_failed_substate9_last_active_before",
        "active_failed_substate9_last_active_after",
        "active_failed_substate9_last_result",
        "active_failed_substate9_last_local_user_before",
        "active_failed_substate9_last_local_user_after",
        "active_failed_substate9_last_state_before",
        "active_failed_substate9_last_state_after",
        "active_failed_substate9_last_sub_state_before",
        "active_failed_substate9_last_sub_state_after",
        "active_failed_substate9_last_ready_before",
        "active_failed_substate9_last_ready_after",
        "active_state5_wide_hook_attempt_mask",
        "active_state5_wide_hook_install_mask",
        "active_state5_wide_hook_all_bits",
        "active_state5_wide_calls",
        "active_state5_wide_transitions_to_state5",
        "active_state5_wide_last_hook_bit",
        "active_state5_wide_last_function_rva",
        "active_state5_wide_last_caller_rva",
        "active_state5_wide_last_active",
        "active_state5_wide_last_context",
        "active_state5_wide_last_sender_code",
        "active_state5_wide_last_result",
        "active_state5_wide_last_delta_millis",
        "active_state5_wide_last_local_user_before",
        "active_state5_wide_last_local_user_after",
        "active_state5_wide_last_state_before",
        "active_state5_wide_last_state_after",
        "active_state5_wide_last_sub_state_before",
        "active_state5_wide_last_sub_state_after",
        "active_state5_wide_last_ready_before",
        "active_state5_wide_last_ready_after",
        "luxor_session_connection_object",
        "luxor_session_connection_ref",
        "luxor_session_async_queue_head",
        "luxor_session_async_queue_next",
        "luxor_session_async_queue_prev",
        "luxor_session_async_queue_count",
        "luxor_session_async_queue_first_callback",
        "luxor_session_async_queue_first_callback_rva",
        "luxor_session_async_queue_first_payload_count",
        "luxor_session_async_queue_tail_callback",
        "luxor_session_async_queue_tail_callback_rva",
        "luxor_session_async_queue_tail_payload_count",
        "luxor_connect_main_user_sentinel",
        "luxor_connect_scratch_object",
        "luxor_connect_scratch_ref",
        "luxor_active_transport",
        "luxor_active_transport_tick",
        "luxor_active_transport_status_code",
        "luxor_active_transport_ready_state",
        "luxor_active_transport_is_host",
        "luxor_active_transport_channel_count",
        "luxor_active_transport_channel_capacity",
        "luxor_active_transport_ready_sampled",
        "luxor_active_transport_ready",
        "online_stage_join_complete_compat",
        "join_complete_compat_attempted",
        "join_complete_compat_method",
        "join_complete_compat_trigger_reason",
        "join_complete_compat_failure",
        "join_complete_compat_call_ok",
        "join_complete_compat_count",
        "join_complete_compat_last_tick",
        "join_complete_compat_active_state_before",
        "join_complete_compat_active_state_after",
        "join_complete_compat_session_connection_before",
        "join_complete_compat_session_connection_after",
        "online_stage_transport_ready_compat",
        "online_stage_ready_open_compat",
        "online_stage_peer_route_tag_fix",
        "online_stage_in_room_transition_compat",
        "online_stage_direct_native_join_diagnostic",
        "transport_ready_compat_attempted",
        "transport_ready_compat_method",
        "transport_ready_compat_trigger_reason",
        "transport_ready_compat_failure",
        "transport_ready_compat_call_ok",
        "transport_ready_compat_count",
        "transport_ready_compat_last_tick",
        "transport_ready_compat_before_ready_state",
        "transport_ready_compat_after_ready_state",
        "transport_ready_compat_before_ready_query",
        "transport_ready_compat_after_ready_query",
        "transport_ready_compat_active_state_before",
        "transport_ready_compat_transport_status_before",
        "transport_ready_compat_transport_is_host_before",
        "transport_ready_compat_session_connection_before",
        "transport_ready_compat_session_connection_after",
        "transport_ready_compat_deferred_called_after_force",
        "transport_ready_compat_session_connection_after_deferred",
        "ready_channel_open_hook_attempted",
        "ready_channel_open_hook_installed",
        "ready_channel_open_calls",
        "ready_channel_open_last_caller_rva",
        "ready_channel_open_last_session_connection",
        "ready_channel_open_last_transport",
        "ready_channel_open_last_can_send_before",
        "ready_channel_open_last_can_send_after",
        "transport_ready_mark_hook_attempted",
        "transport_ready_mark_hook_installed",
        "transport_ready_mark_calls",
        "transport_ready_mark_last_caller_rva",
        "transport_ready_mark_last_transport",
        "transport_ready_mark_last_ready_before",
        "transport_ready_mark_last_ready_after",
        "ready_registry_step80_hook_attempted",
        "ready_registry_step80_hook_installed",
        "ready_registry_step80_calls",
        "ready_registry_stepd0_hook_attempted",
        "ready_registry_stepd0_hook_installed",
        "ready_registry_stepd0_calls",
        "ready_registry_stepd8_hook_attempted",
        "ready_registry_stepd8_hook_installed",
        "ready_registry_stepd8_calls",
        "queued_opcode_send_hook_attempted",
        "queued_opcode_send_hook_installed",
        "queued_opcode_send_calls",
        "queued_opcode_send_last_caller_rva",
        "queued_opcode_send_last_session_connection",
        "queued_opcode_send_last_source_packet",
        "queued_opcode_send_last_source_route_key_vtable",
        "queued_opcode_send_last_source_routing_tag",
        "queued_opcode_send_last_outer_opcode",
        "queued_opcode_send_last_inner_opcode",
        "queued_opcode_send_last_opcode",
        "queued_opcode_send_last_channel_id",
        "queued_opcode_send_last_result",
        "queued_opcode_send_last_queue_before",
        "queued_opcode_send_last_queue_after",
        "queued_work_item_clone_hook_attempted",
        "queued_work_item_clone_hook_installed",
        "queued_work_item_clone_calls",
        "queued_work_item_clone_last_caller_rva",
        "queued_work_item_clone_last_source_work_item",
        "queued_work_item_clone_last_cloned_work_item",
        "queued_work_item_clone_last_source_session_connection",
        "queued_work_item_clone_last_source_route_key_vtable",
        "queued_work_item_clone_last_source_routing_tag",
        "queued_work_item_clone_last_source_inner_opcode",
        "queued_work_item_clone_last_source_channel_id",
        "queued_work_item_clone_last_cloned_session_connection",
        "queued_work_item_clone_last_cloned_route_key_vtable",
        "queued_work_item_clone_last_cloned_routing_tag",
        "queued_work_item_clone_last_cloned_inner_opcode",
        "queued_work_item_clone_last_cloned_channel_id",
        "host_packet_archive_init_hook_attempted",
        "host_packet_archive_init_hook_installed",
        "host_packet_archive_init_calls",
        "host_packet_archive_init_last_caller_rva",
        "host_packet_archive_init_last_packet",
        "host_packet_archive_init_last_mode",
        "host_packet_archive_init_last_state",
        "host_packet_archive_init_last_archive_mode",
        "host_packet_archive_init_last_routing_tag",
        "host_packet_archive_init_last_default_tag",
        "host_packet_archive_init_last_sentinel_tag",
        "host_packet_archive_init_last_replacement_tag",
        "host_packet_archive_init_last_byte_array",
        "host_packet_archive_init_last_byte_array_ref",
        "host_packet_archive_init_last_cursor",
        "host_packet_archive_init_last_data",
        "host_packet_archive_init_last_size",
        "host_packet_archive_init_last_capacity",
        "host_packet_archive_init_last_timeline_opcode",
        "packet_routing_tag_copy_hook_attempted",
        "packet_routing_tag_copy_hook_installed",
        "packet_routing_tag_copy_calls",
        "packet_routing_tag_copy_last_caller_rva",
        "packet_routing_tag_copy_last_dest_packet",
        "packet_routing_tag_copy_last_source_packet",
        "packet_routing_tag_copy_last_dest_tag_before",
        "packet_routing_tag_copy_last_source_tag",
        "packet_routing_tag_copy_last_dest_tag_after",
        "packet_routing_tag_copy_last_timeline_opcode",
        "peer_route_tag_fix_enabled",
        "peer_route_tag_fix_last_peer_tag",
        "peer_route_tag_fix_last_peer_registry_index",
        "peer_route_tag_fix_last_peer_owner",
        "peer_route_tag_fix_last_peer_writer",
        "peer_route_tag_fix_last_peer_source",
        "peer_route_tag_fix_attempts",
        "peer_route_tag_fix_applied",
        "peer_route_tag_fix_last_caller_rva",
        "peer_route_tag_fix_last_dest_packet",
        "peer_route_tag_fix_last_source_packet",
        "peer_route_tag_fix_last_original_tag",
        "peer_route_tag_fix_last_replacement_tag",
        "peer_route_tag_fix_last_result",
        "peer_route_tag_fix_last_write_verified",
        "peer_route_tag_fix_last_verified_tag",
        "active_opcode6_send_hook_attempted",
        "active_opcode6_send_hook_installed",
        "active_opcode6_send_calls",
        "active_opcode6_send_last_caller_rva",
        "active_opcode6_send_last_active",
        "active_opcode6_send_last_sender",
        "active_opcode6_send_last_sender_vtable",
        "active_opcode6_send_last_state_payload",
        "active_opcode6_send_last_result",
        "active_opcode6_send_last_active_state",
        "active_opcode6_send_last_active_sub_state",
        "active_opcode6_send_last_transport_tick",
        "active_opcode6_send_last_transport_status",
        "active_opcode6_send_last_transport_ready",
        "active_opcode6_send_last_transport_is_host",
        "active_opcode6_send_last_transport_channel_count",
        "active_opcode6_send_last_transport_channel_capacity",
        "active_sender_endpoint_get_hook_attempted",
        "active_sender_endpoint_get_hook_installed",
        "active_sender_endpoint_get_calls",
        "active_sender_endpoint_get_last_caller_rva",
        "active_sender_endpoint_get_last_sender_interface",
        "active_sender_endpoint_get_last_sender_interface_vtable",
        "active_sender_endpoint_get_last_local_user",
        "active_sender_endpoint_get_last_endpoint",
        "active_sender_endpoint_get_last_endpoint_vtable",
        "active_sender_endpoint_get_last_endpoint_local_user_slot",
        "active_sender_endpoint_get_last_endpoint_send_target",
        "active_endpoint_send_hook_attempted",
        "active_endpoint_send_hook_installed",
        "active_endpoint_send_hook_target",
        "active_endpoint_send_calls",
        "active_endpoint_send_data_opcode0_calls",
        "active_endpoint_send_data_opcode4_calls",
        "active_endpoint_send_data_opcode5_calls",
        "active_endpoint_send_data_opcode6_calls",
        "active_endpoint_send_data_opcode10_calls",
        "active_endpoint_send_data_opcode15_calls",
        "active_endpoint_send_data_opcode20_calls",
        "active_endpoint_send_data_opcode21_calls",
        "active_endpoint_send_last_caller_rva",
        "active_endpoint_send_last_endpoint",
        "active_endpoint_send_last_endpoint_vtable",
        "active_endpoint_send_last_endpoint_local_user_slot",
        "active_endpoint_send_last_packet",
        "active_endpoint_send_last_packet_cursor",
        "active_endpoint_send_last_packet_data",
        "active_endpoint_send_last_packet_mode",
        "active_endpoint_send_last_packet_size",
        "active_endpoint_send_last_packet_capacity",
        "active_endpoint_send_last_packet_byte0",
        "active_endpoint_send_last_packet_byte1",
        "active_endpoint_send_last_packet_byte2",
        "active_endpoint_send_last_packet_byte3",
        "active_endpoint_send_last_packet_data_byte0",
        "active_endpoint_send_last_packet_data_byte1",
        "active_endpoint_send_last_packet_data_byte2",
        "active_endpoint_send_last_packet_data_byte3",
        "active_endpoint_send_last_arg2",
        "active_endpoint_send_last_result",
        "active_endpoint_send_opcode21_last_caller_rva",
        "active_endpoint_send_opcode21_last_result",
        "active_endpoint_send_opcode21_last_size",
        "active_endpoint_send_opcode21_last_byte1",
        "active_endpoint_send_opcode21_last_byte2",
        "active_endpoint_send_opcode21_last_byte3",
        "active_endpoint_send_last_active",
        "active_endpoint_send_last_active_state",
        "active_endpoint_send_last_active_sub_state",
        "active_endpoint_send_last_transport_tick",
        "active_endpoint_send_last_transport_status",
        "active_endpoint_send_last_transport_ready",
        "active_endpoint_send_last_transport_is_host",
        "active_endpoint_send_last_transport_channel_count",
        "active_endpoint_send_last_transport_channel_capacity",
        "route_writer_resolve_hook_attempted",
        "route_writer_resolve_hook_installed",
        "route_writer_resolve_calls",
        "route_writer_resolve_last_caller_rva",
        "route_writer_resolve_last_root",
        "route_writer_resolve_last_route_key",
        "route_writer_resolve_last_route_tag",
        "route_writer_resolve_last_writer",
        "route_writer_resolve_last_writer_ref",
        "route_writer_resolve_last_writer_vtable",
        "route_writer_resolve_last_writer_send_target",
        "route_writer_owner_tag_match_hook_attempted",
        "route_writer_owner_tag_match_hook_installed",
        "route_writer_owner_tag_match_calls",
        "route_writer_owner_tag_match_last_caller_rva",
        "route_writer_owner_tag_match_last_requested_route_key",
        "route_writer_owner_tag_match_last_requested_routing_tag",
        "route_writer_owner_tag_match_last_candidate_owner",
        "route_writer_owner_tag_match_last_candidate_owner_vtable",
        "route_writer_owner_tag_match_last_candidate_accessor_rva",
        "route_writer_owner_tag_match_last_candidate_route_key",
        "route_writer_owner_tag_match_last_candidate_routing_tag",
        "route_writer_owner_tag_match_last_result",
        "route_writer_registry_last_registry",
        "route_writer_registry_last_entries_begin",
        "route_writer_registry_last_entries_end",
        "route_writer_registry_last_entries_capacity_end",
        "route_writer_registry_last_enabled",
        "route_writer_registry_last_entry_count",
        "route_writer_registry_last_sample_count",
        "route_writer_registry_last_selected_index",
        "route_writer_registry_last_selected_owner",
        "route_writer_registry_last_selected_owner_ref",
        "route_writer_registry_last_selected_owner_vtable",
        "route_writer_registry_last_selected_writer",
        "route_writer_registry_last_selected_writer_vtable",
        "route_writer_registry_last_selected_writer_send_target",
        "route_writer_registry_last_entry0_owner",
        "route_writer_registry_last_entry0_owner_ref",
        "route_writer_registry_last_entry0_owner_vtable",
        "route_writer_registry_last_entry0_writer",
        "route_writer_registry_last_entry0_writer_vtable",
        "route_writer_registry_last_entry0_writer_send_target",
        "luxor_route_key_enum_hook_attempted",
        "luxor_route_key_enum_hook_installed",
        "luxor_route_key_enum_hook_target",
        "luxor_route_key_enum_calls",
        "luxor_route_key_enum_last_caller_rva",
        "luxor_route_key_enum_last_route_service",
        "luxor_route_key_enum_last_output_array",
        "luxor_route_key_enum_last_result_array",
        "luxor_route_key_enum_last_entries_begin",
        "luxor_route_key_enum_last_entries_end",
        "luxor_route_key_enum_last_entries_capacity_end",
        "luxor_route_key_enum_last_entry_count",
        "luxor_route_key_enum_last_sample_count",
        "luxor_route_key_enum_last_service_shared_refs_begin",
        "luxor_route_key_enum_last_service_shared_refs_end",
        "luxor_route_key_enum_last_service_shared_refs_capacity_end",
        "luxor_route_key_enum_last_service_shared_ref_count",
        "luxor_route_key_enum_last_replacement_count",
        "luxor_route_key_enum_last_nonreplacement_count",
        "luxor_route_key_enum_last_default_count",
        "luxor_route_key_enum_last_expected_count",
        "luxor_route_key_enum_last_entry0_owner",
        "luxor_route_key_enum_last_entry0_route_tag",
        "luxor_route_key_enum_last_entry1_owner",
        "luxor_route_key_enum_last_entry1_route_tag",
        "luxor_route_key_enum_last_entry2_owner",
        "luxor_route_key_enum_last_entry2_route_tag",
        "luxor_route_key_enum_last_entry3_owner",
        "luxor_route_key_enum_last_entry3_route_tag",
        "luxor_route_key_list_build_hook_attempted",
        "luxor_route_key_list_build_hook_installed",
        "luxor_route_key_list_build_hook_target",
        "luxor_route_key_list_build_calls",
        "luxor_route_key_list_build_last_caller_rva",
        "luxor_route_key_list_build_last_active",
        "luxor_route_key_list_build_last_mode",
        "luxor_route_key_list_build_last_entry_count",
        "luxor_route_key_list_build_last_replacement_count",
        "luxor_route_key_list_build_last_nonreplacement_count",
        "luxor_route_key_list_build_last_default_count",
        "luxor_route_key_list_build_last_entry0_route_tag",
        "luxor_route_key_list_build_last_entry1_route_tag",
        "route_writer_source_acquire_hook_attempted",
        "route_writer_source_acquire_hook_installed",
        "route_writer_source_acquire_hook_target",
        "route_writer_source_acquire_calls",
        "route_writer_source_acquire_last_caller_rva",
        "route_writer_source_acquire_last_route_service",
        "route_writer_source_acquire_last_out_writer_owner",
        "route_writer_source_acquire_last_result_pair",
        "route_writer_source_acquire_last_native_route_source",
        "route_writer_source_acquire_last_native_route_source_vtable",
        "route_writer_source_acquire_last_use_routing_tag_object",
        "route_writer_source_acquire_last_out_owner",
        "route_writer_source_acquire_last_out_owner_ref",
        "route_writer_source_acquire_last_registry_entry_count",
        "route_writer_source_acquire_last_selected_index",
        "route_writer_acquire_hook_attempted",
        "route_writer_acquire_hook_installed",
        "route_writer_acquire_hook_target",
        "route_writer_acquire_calls",
        "route_writer_acquire_last_caller_rva",
        "route_writer_acquire_last_route_service",
        "route_writer_acquire_last_route_key",
        "route_writer_acquire_last_route_tag",
        "route_writer_acquire_last_out_owner",
        "route_writer_acquire_last_out_owner_ref",
        "route_writer_acquire_last_registry_entry_count",
        "route_writer_acquire_last_selected_index",
        "route_writer_assign_hook_attempted",
        "route_writer_assign_hook_installed",
        "route_writer_assign_hook_target",
        "route_writer_assign_calls",
        "route_writer_assign_last_caller_rva",
        "route_writer_assign_last_registry",
        "route_writer_assign_last_route_key",
        "route_writer_assign_last_route_tag",
        "route_writer_assign_last_writer_mode",
        "route_writer_assign_last_out_owner",
        "route_writer_assign_last_out_owner_ref",
        "route_writer_assign_last_registry_entry_count",
        "route_writer_assign_last_selected_index",
        "route_writer_send_hook_attempted",
        "route_writer_send_hook_installed",
        "route_writer_send_hook_target",
        "route_writer_send_calls",
        "route_writer_send_data_opcode21_calls",
        "route_writer_send_last_caller_rva",
        "route_writer_send_last_writer",
        "route_writer_send_last_writer_vtable",
        "route_writer_send_last_primary_peer_object",
        "route_writer_send_last_primary_peer_ref",
        "route_writer_send_last_route_map_vtable",
        "route_writer_send_last_small_route_vtable",
        "route_writer_send_last_large_packet_sink_vtable",
        "route_writer_send_last_small_route_ready_target",
        "route_writer_send_last_small_route_send_target",
        "route_writer_send_last_small_route_state",
        "route_writer_send_last_small_route_entries",
        "route_writer_send_last_small_route_buckets",
        "route_writer_send_last_small_route_sample_ok",
        "route_writer_send_last_small_route_count",
        "route_writer_send_last_small_route_limit",
        "route_writer_send_last_small_route_bucket_mask_plus_one",
        "route_writer_send_last_small_route_sequence_counter",
        "route_writer_send_last_small_route_next_sequence",
        "route_writer_send_last_small_route_next_available",
        "route_writer_send_last_small_route_next_slot_present",
        "route_writer_send_last_small_route_bucket_index",
        "route_writer_send_last_small_route_collision_entry_index",
        "route_writer_send_last_small_route_walk_steps",
        "route_writer_send_last_route_map_capacity_target",
        "route_writer_send_last_route_map_send_target",
        "route_writer_send_last_route_map_bind_target",
        "route_writer_send_last_route_map_flush_target",
        "route_writer_send_last_large_packet_sink_target",
        "route_writer_send_last_deferred_count",
        "route_writer_send_last_deferred_capacity",
        "route_writer_send_last_backend_available",
        "route_writer_send_last_secondary_backend_available",
        "route_writer_send_last_packet",
        "route_writer_send_last_route_tag",
        "route_writer_send_last_local_user",
        "route_writer_send_last_send_flag",
        "route_writer_send_last_local_user_is8",
        "route_writer_send_last_packet_data_byte0",
        "route_writer_send_last_packet_data_byte1",
        "route_writer_send_last_packet_mode",
        "route_writer_send_last_packet_size",
        "route_writer_send_last_result",
        "route_writer_send_last_inferred_branch",
        "route_writer_send_opcode21_last_result",
        "route_writer_send_opcode21_small_route_pre_available",
        "route_writer_send_opcode21_small_route_post_available",
        "route_writer_send_opcode21_small_route_pre_slot_present",
        "route_writer_send_opcode21_small_route_post_slot_present",
        "route_writer_send_opcode21_small_route_pre_sequence",
        "route_writer_send_opcode21_small_route_post_sequence",
        "route_writer_send_opcode21_small_route_pre_count",
        "route_writer_send_opcode21_small_route_post_count",
        "route_writer_send_opcode21_small_route_pre_collision_index",
        "route_writer_send_opcode21_small_route_post_collision_index",
        "route_writer_send_last_parent",
        "route_writer_send_last_parent_vtable",
        "route_writer_send_last_parent_ready_target",
        "route_writer_send_last_parent_state_target",
        "route_writer_send_last_parent_identity_target",
        "route_writer_send_last_parent_backend_get_target",
        "route_writer_send_last_parent_state",
        "route_writer_send_last_parent_ready_flags",
        "route_writer_send_last_parent_ready_flag_set",
        "route_writer_send_last_parent_ready_state_ok",
        "route_writer_send_last_parent_ready",
        "route_writer_send_opcode21_last_parent",
        "route_writer_send_opcode21_last_parent_vtable",
        "route_writer_send_opcode21_last_parent_backend_get_target",
        "route_writer_send_opcode21_parent_state",
        "route_writer_send_opcode21_parent_ready_flags",
        "route_writer_send_opcode21_parent_ready",
        "route_writer_backend_get_hook_attempted",
        "route_writer_backend_get_hook_installed",
        "route_writer_backend_get_hook_target",
        "route_writer_backend_get_calls",
        "route_writer_backend_get_last_caller_rva",
        "route_writer_backend_get_last_parent",
        "route_writer_backend_get_last_parent_vtable",
        "route_writer_backend_get_last_backend",
        "route_writer_backend_get_last_backend_vtable",
        "route_writer_backend_get_last_backend_send_target",
        "route_writer_backend_get_last_backend_route_channel_target",
        "route_writer_backend_send_hook_attempted",
        "route_writer_backend_send_hook_installed",
        "route_writer_backend_send_hook_target",
        "route_writer_backend_send_calls",
        "route_writer_backend_send_magic_calls",
        "route_writer_backend_send_last_caller_rva",
        "route_writer_backend_send_last_backend",
        "route_writer_backend_send_last_backend_vtable",
        "route_writer_backend_send_last_destination",
        "route_writer_backend_send_last_packet_data",
        "route_writer_backend_send_last_packet_size",
        "route_writer_backend_send_last_magic",
        "route_writer_backend_send_last_local_user",
        "route_writer_backend_send_last_marker",
        "route_writer_backend_send_last_payload_opcode",
        "route_writer_backend_send_last_payload_byte1",
        "route_writer_backend_send_last_payload_byte2",
        "route_writer_backend_send_last_backend_parent",
        "route_writer_backend_send_last_backend_parent_vtable",
        "route_writer_backend_send_last_connection_lookup_target",
        "route_writer_backend_send_last_destination_key",
        "route_writer_backend_send_last_destination_ref",
        "luxor_backend_route_channel_forward_hook_attempted",
        "luxor_backend_route_channel_forward_hook_installed",
        "luxor_backend_route_channel_forward_hook_target",
        "luxor_backend_route_channel_forward_calls",
        "luxor_backend_route_channel_forward_last_caller_rva",
        "luxor_backend_route_channel_forward_last_backend",
        "luxor_backend_route_channel_forward_last_backend_vtable",
        "luxor_backend_route_channel_forward_last_connection",
        "luxor_backend_route_channel_forward_last_connection_vtable",
        "luxor_backend_route_channel_forward_last_output_slots_target",
        "luxor_backend_route_channel_forward_last_route_channel",
        "luxor_backend_route_channel_forward_last_route_channel_vtable",
        "luxor_backend_route_channel_forward_last_used_before",
        "luxor_backend_route_channel_forward_last_used_after",
        "luxor_route_channel_output_slots_hook_attempted",
        "luxor_route_channel_output_slots_hook_installed",
        "luxor_route_channel_output_slots_hook_target",
        "luxor_route_channel_output_slots_calls",
        "luxor_route_channel_output_slots_last_caller_rva",
        "luxor_route_channel_output_slots_last_route_sink",
        "luxor_route_channel_output_slots_last_route_sink_vtable",
        "luxor_route_channel_output_slots_last_slots_begin",
        "luxor_route_channel_output_slots_last_slots_end",
        "luxor_route_channel_output_slots_last_slot_count",
        "luxor_route_channel_output_slots_last_identity_object",
        "luxor_route_channel_output_slots_last_identity_ref",
        "luxor_route_channel_output_slots_last_route_channel",
        "luxor_route_channel_output_slots_last_route_channel_vtable",
        "luxor_route_channel_output_slots_last_used_before",
        "luxor_route_channel_output_slots_last_used_after",
        "luxor_route_frame_output_slot_hook_attempted",
        "luxor_route_frame_output_slot_hook_installed",
        "luxor_route_frame_output_slot_hook_target",
        "luxor_route_frame_output_slot_calls",
        "luxor_route_frame_output_slot_reject_slot_ff_calls",
        "luxor_route_frame_output_slot_success_calls",
        "luxor_route_frame_output_slot_last_caller_rva",
        "luxor_route_frame_output_slot_last_output_slot",
        "luxor_route_frame_output_slot_last_output_slot_index",
        "luxor_route_frame_output_slot_last_identity_object",
        "luxor_route_frame_output_slot_last_identity_ref",
        "luxor_route_frame_output_slot_last_route_channel",
        "luxor_route_frame_output_slot_last_route_channel_vtable",
        "luxor_route_frame_output_slot_last_used_before",
        "luxor_route_frame_output_slot_last_used_after",
        "luxor_route_frame_output_slot_last_result",
        "luxor_route_output_task_queue_hook_attempted",
        "luxor_route_output_task_queue_hook_installed",
        "luxor_route_output_task_queue_hook_target",
        "luxor_route_output_task_queue_calls",
        "luxor_route_output_task_queue_last_caller_rva",
        "luxor_route_output_task_queue_last_queue",
        "luxor_route_output_task_queue_last_heap_entries",
        "luxor_route_output_task_queue_last_entry_count",
        "luxor_route_output_task_queue_last_valid_entry_count",
        "luxor_route_output_task_queue_last_slot_index",
        "luxor_route_output_task_queue_last_dispatch_depth_before",
        "luxor_route_output_task_queue_last_dispatch_depth_after",
        "luxor_route_output_task_queue_last_frame_archive",
        "luxor_route_output_task_queue_last_first_consumer",
        "luxor_route_output_task_queue_last_first_consumer_vtable",
        "luxor_route_output_task_queue_last_first_consumer_accept_target",
        "luxor_route_output_task_queue_last_last_consumer",
        "luxor_route_output_task_queue_last_last_consumer_vtable",
        "luxor_route_output_task_queue_last_last_consumer_accept_target",
        "luxor_route_output_task_queue_last_callback",
        "luxor_route_output_task_queue_last_receiver_base",
        "luxor_route_output_task_queue_last_receiver_ref",
        "luxor_route_output_task_queue_last_receiver_adjustment",
        "luxor_route_output_task_queue_last_adjusted_receiver",
        "luxor_route_output_task_queue_last_consumer_layout",
        "luxor_route_output_task_queue_last_frame_magic",
        "luxor_route_output_task_queue_last_frame_payload_opcode",
        "luxor_route_output_task_consumer_hook_attempted",
        "luxor_route_output_task_consumer_hook_installed",
        "luxor_route_output_task_consumer_hook_target",
        "luxor_route_output_task_consumer_calls",
        "luxor_route_output_task_consumer_last_caller_rva",
        "luxor_route_output_task_consumer_last_consumer",
        "luxor_route_output_task_consumer_last_consumer_vtable",
        "luxor_route_output_task_consumer_last_slot_index",
        "luxor_route_output_task_consumer_last_frame_archive",
        "luxor_route_output_task_consumer_last_callback",
        "luxor_route_output_task_consumer_last_receiver_base",
        "luxor_route_output_task_consumer_last_receiver_adjustment",
        "luxor_route_output_task_consumer_last_adjusted_receiver",
        "luxor_route_output_task_consumer_last_result",
        "luxor_route_output_task_consumer_last_frame_magic",
        "luxor_route_output_task_consumer_last_frame_payload_opcode",
        "luxor_forwarding_route_output_task_consumer_hook_attempted",
        "luxor_forwarding_route_output_task_consumer_hook_installed",
        "luxor_forwarding_route_output_task_consumer_hook_target",
        "luxor_forwarding_route_output_task_consumer_calls",
        "luxor_forwarding_route_output_task_consumer_last_caller_rva",
        "luxor_forwarding_route_output_task_consumer_last_consumer",
        "luxor_forwarding_route_output_task_consumer_last_consumer_vtable",
        "luxor_forwarding_route_output_task_consumer_last_slot_index",
        "luxor_forwarding_route_output_task_consumer_last_frame_archive",
        "luxor_forwarding_route_output_task_consumer_last_receiver_base",
        "luxor_forwarding_route_output_task_consumer_last_receiver_ref",
        "luxor_forwarding_route_output_task_consumer_last_callback",
        "luxor_forwarding_route_output_task_consumer_last_receiver_adjustment",
        "luxor_forwarding_route_output_task_consumer_last_adjusted_receiver",
        "luxor_forwarding_route_output_task_consumer_last_result",
        "luxor_forwarding_route_output_task_consumer_last_frame_magic",
        "luxor_forwarding_route_output_task_consumer_last_frame_payload_opcode",
        "luxor_forwarded_route_opcode_dispatch_hook_attempted",
        "luxor_forwarded_route_opcode_dispatch_hook_installed",
        "luxor_forwarded_route_opcode_dispatch_hook_target",
        "luxor_forwarded_route_opcode_dispatch_calls",
        "luxor_forwarded_route_opcode_dispatch_last_caller_rva",
        "luxor_forwarded_route_opcode_dispatch_last_dispatcher",
        "luxor_forwarded_route_opcode_dispatch_last_opcode_tree",
        "luxor_forwarded_route_opcode_dispatch_last_opcode",
        "luxor_forwarded_route_opcode_dispatch_last_handler_node",
        "luxor_forwarded_route_opcode_dispatch_last_handler_key",
        "luxor_forwarded_route_opcode_dispatch_last_handler_storage",
        "luxor_forwarded_route_opcode_dispatch_last_handler_found",
        "luxor_forwarded_route_opcode_dispatch_last_frame_magic",
        "luxor_forwarded_route_opcode_dispatch_last_frame_payload_opcode",
        "luxor_backend_connection_lookup_hook_attempted",
        "luxor_backend_connection_lookup_hook_installed",
        "luxor_backend_connection_lookup_hook_target",
        "luxor_backend_connection_lookup_calls",
        "luxor_backend_connection_lookup_last_caller_rva",
        "luxor_backend_connection_lookup_last_map",
        "luxor_backend_connection_lookup_last_destination_key",
        "luxor_backend_connection_lookup_last_connection",
        "luxor_backend_connection_lookup_last_connection_ref",
        "luxor_backend_connection_lookup_last_connection_vtable",
        "luxor_backend_connection_lookup_last_raw_send_target",
        "luxor_backend_connection_destination_match_hook_attempted",
        "luxor_backend_connection_destination_match_hook_installed",
        "luxor_backend_connection_destination_match_hook_target",
        "luxor_backend_connection_destination_match_calls",
        "luxor_backend_connection_destination_match_true_calls",
        "luxor_backend_connection_destination_match_false_calls",
        "luxor_backend_connection_destination_match_last_caller_rva",
        "luxor_backend_connection_destination_match_last_destination_pair",
        "luxor_backend_connection_destination_match_last_destination_key",
        "luxor_backend_connection_destination_match_last_destination_ref",
        "luxor_backend_connection_destination_match_last_candidate_pair",
        "luxor_backend_connection_destination_match_last_candidate_connection",
        "luxor_backend_connection_destination_match_last_candidate_ref",
        "luxor_backend_connection_destination_match_last_candidate_vtable",
        "luxor_backend_connection_destination_match_last_candidate_raw_send_target",
        "luxor_backend_connection_destination_match_last_timeline_opcode",
        "luxor_backend_connection_destination_match_last_result",
        "luxor_backend_connection_table_last_owner",
        "luxor_backend_connection_table_last_table",
        "luxor_backend_connection_table_last_entries_begin",
        "luxor_backend_connection_table_last_entries_end",
        "luxor_backend_connection_table_last_entries_capacity_end",
        "luxor_backend_connection_table_last_entry_count",
        "luxor_backend_connection_table_last_sample_count",
        "luxor_backend_connection_table_last_selected_index",
        "luxor_backend_connection_table_last_selected_connection",
        "luxor_backend_connection_table_last_selected_raw_send_target",
        "luxor_backend_connection_table_last_entry0_connection",
        "luxor_backend_connection_table_last_entry0_raw_send_target",
        "luxor_backend_connection_table_last_entry1_connection",
        "luxor_backend_connection_table_last_entry1_raw_send_target",
        "luxor_backend_connection_raw_send_hook_attempted",
        "luxor_backend_connection_raw_send_hook_installed",
        "luxor_backend_connection_raw_send_hook_target",
        "luxor_backend_connection_raw_send_calls",
        "luxor_backend_connection_raw_send_magic_calls",
        "luxor_backend_connection_raw_send_last_caller_rva",
        "luxor_backend_connection_raw_send_last_connection",
        "luxor_backend_connection_raw_send_last_connection_vtable",
        "luxor_backend_connection_raw_send_last_packet_data",
        "luxor_backend_connection_raw_send_last_packet_size",
        "luxor_backend_connection_raw_send_last_magic",
        "luxor_backend_connection_raw_send_last_payload_opcode",
        "luxor_backend_connection_raw_send_last_payload_byte1",
        "luxor_backend_connection_raw_send_last_payload_byte2",
        "luxor_backend_connection_raw_send_last_payload_size",
        "luxor_backend_connection_raw_send_last_marker",
        "luxor_backend_connection_raw_send_last_route_selector",
        "luxor_backend_connection_raw_send_last_route_channel",
        "luxor_backend_connection_raw_send_last_route_channel_vtable",
        "luxor_backend_connection_raw_send_last_route_channel_target",
        "luxor_backend_connection_raw_send_last_lower_transport",
        "luxor_backend_connection_raw_send_last_lower_transport_vtable",
        "luxor_backend_connection_raw_send_last_lower_ready_target",
        "luxor_backend_connection_raw_send_last_lower_sender",
        "luxor_backend_connection_raw_send_last_lower_sender_vtable",
        "luxor_backend_connection_raw_send_last_lower_sender_send_target",
        "luxor_backend_packet_stream_receive_calls",
        "luxor_backend_packet_stream_receive_last_caller_rva",
        "luxor_backend_packet_stream_receive_last_connection",
        "luxor_backend_packet_stream_receive_last_packet_size",
        "luxor_backend_packet_stream_receive_last_magic",
        "luxor_backend_packet_stream_receive_last_payload_opcode",
        "luxor_backend_packet_stream_receive_last_route_selector",
        "luxor_backend_packet_stream_receive_last_route_channel",
        "luxor_lower_transport_send_if_ready_hook_attempted",
        "luxor_lower_transport_send_if_ready_hook_installed",
        "luxor_lower_transport_send_if_ready_hook_target",
        "luxor_lower_transport_send_if_ready_calls",
        "luxor_lower_transport_send_if_ready_last_caller_rva",
        "luxor_lower_transport_send_if_ready_last_transport",
        "luxor_lower_transport_send_if_ready_last_transport_vtable",
        "luxor_lower_transport_send_if_ready_last_ready_target",
        "luxor_lower_transport_send_if_ready_last_sender",
        "luxor_lower_transport_send_if_ready_last_sender_vtable",
        "luxor_lower_transport_send_if_ready_last_sender_send_target",
        "luxor_lower_transport_send_if_ready_last_result",
        "luxor_lower_transport_send_if_ready_last_magic",
        "luxor_lower_transport_send_if_ready_last_payload_opcode",
        "luxor_lower_sender_send_hook_attempted",
        "luxor_lower_sender_send_hook_installed",
        "luxor_lower_sender_send_hook_target",
        "luxor_lower_sender_send_calls",
        "luxor_lower_sender_send_magic_calls",
        "luxor_lower_sender_send_last_caller_rva",
        "luxor_lower_sender_send_last_sender",
        "luxor_lower_sender_send_last_sender_vtable",
        "luxor_lower_sender_send_last_packet_data",
        "luxor_lower_sender_send_last_packet_size",
        "luxor_lower_sender_send_last_result_out",
        "luxor_lower_sender_send_last_route_identity",
        "luxor_lower_sender_send_last_result",
        "luxor_lower_sender_send_last_magic",
        "luxor_lower_sender_send_last_payload_opcode",
        "luxor_lower_sender_send_last_payload_byte1",
        "luxor_lower_sender_send_last_payload_byte2",
        "luxor_route_channel_append_hook_attempted",
        "luxor_route_channel_append_hook_installed",
        "luxor_route_channel_append_hook_target",
        "luxor_route_channel_append_calls",
        "luxor_route_channel_append_opcode21_calls",
        "luxor_route_channel_append_last_caller_rva",
        "luxor_route_channel_append_last_channel",
        "luxor_route_channel_append_last_channel_vtable",
        "luxor_route_channel_append_last_packet_data",
        "luxor_route_channel_append_last_packet_size",
        "luxor_route_channel_append_last_magic",
        "luxor_route_channel_append_last_payload_opcode",
        "luxor_route_channel_append_last_payload_size",
        "luxor_route_channel_append_last_capacity",
        "luxor_route_channel_append_last_used_before",
        "luxor_route_channel_append_last_used_after",
        "luxor_route_channel_append_last_identity_object",
        "luxor_route_channel_append_last_identity_ref",
        "luxor_route_channel_append_last_result",
        "luxor_route_dispatch_drain_hook_attempted",
        "luxor_route_dispatch_drain_hook_installed",
        "luxor_route_dispatch_drain_hook_target",
        "luxor_route_dispatch_drain_calls",
        "luxor_route_dispatch_drain_last_caller_rva",
        "luxor_route_dispatch_drain_last_connection",
        "luxor_route_dispatch_drain_last_selector_before",
        "luxor_route_dispatch_drain_last_selector_after",
        "luxor_route_dispatch_drain_last_pending_before",
        "luxor_route_dispatch_drain_last_pending_after",
        "luxor_route_dispatch_drain_last_channel",
        "luxor_route_dispatch_drain_last_channel_used_before",
        "luxor_route_dispatch_drain_last_channel_used_after",
        "connect_sender_send_hook_attempted",
        "connect_sender_send_hook_installed",
        "connect_sender_send_calls",
        "connect_sender_send_last_caller_rva",
        "connect_sender_send_last_sender",
        "connect_sender_send_last_sender_vtable",
        "connect_sender_send_last_packet",
        "connect_sender_send_last_packet_cursor",
        "connect_sender_send_last_packet_mode",
        "connect_sender_send_last_packet_byte0",
        "connect_sender_send_last_packet_byte1",
        "connect_sender_send_last_packet_byte2",
        "connect_sender_send_last_packet_byte3",
        "connect_sender_send_last_arg2",
        "connect_sender_send_last_arg3",
        "connect_sender_send_last_result",
        "connect_sender_send_last_active",
        "connect_sender_send_last_active_state",
        "connect_sender_send_last_active_sub_state",
        "connect_sender_send_last_transport_tick",
        "connect_sender_send_last_transport_status",
        "connect_sender_send_last_transport_ready",
        "connect_sender_send_last_transport_is_host",
        "connect_sender_send_last_transport_channel_count",
        "connect_sender_send_last_transport_channel_capacity",
        "active_packet_dispatch_hook_attempted",
        "active_packet_dispatch_hook_installed",
        "active_packet_dispatch_calls",
        "active_packet_dispatch_opcode0_calls",
        "active_packet_dispatch_opcode4_calls",
        "active_packet_dispatch_opcode5_calls",
        "active_packet_dispatch_opcode6_calls",
        "active_packet_dispatch_opcode9_calls",
        "active_packet_dispatch_opcode10_calls",
        "active_packet_dispatch_opcode11_calls",
        "active_packet_dispatch_opcode15_calls",
        "active_packet_dispatch_opcode20_calls",
        "active_packet_dispatch_opcode21_calls",
        "active_packet_dispatch_last_active",
        "active_packet_dispatch_last_packet",
        "active_packet_dispatch_last_context",
        "active_packet_dispatch_last_opcode",
        "active_packet_dispatch_last_state_before",
        "active_packet_dispatch_last_state_after",
        "active_packet_dispatch_last_ready_before",
        "active_packet_dispatch_last_ready_after",
        "transport_open_message_hook_attempted",
        "transport_open_message_hook_installed",
        "transport_open_message_calls",
        "transport_open_message_last_active",
        "transport_open_message_last_packet",
        "transport_open_message_last_state_before",
        "transport_open_message_last_state_after",
        "transport_open_message_last_ready_before",
        "transport_open_message_last_ready_after",
        "transport_open_response_hook_attempted",
        "transport_open_response_hook_installed",
        "transport_open_response_calls",
        "transport_open_response_last_active",
        "transport_open_response_last_packet",
        "transport_open_response_last_state_before",
        "transport_open_response_last_state_after",
        "transport_open_response_last_ready_before",
        "transport_open_response_last_ready_after",
        "active_opcode15_message_hook_attempted",
        "active_opcode15_message_hook_installed",
        "active_opcode15_message_calls",
        "ready_keyed_dispatch_hook_attempted",
        "ready_keyed_dispatch_hook_installed",
        "ready_keyed_dispatch_calls",
        "ready_keyed_dispatch_opcode15_calls",
        "ready_keyed_dispatch_last_pool",
        "ready_keyed_dispatch_last_route",
        "ready_keyed_dispatch_last_group",
        "ready_keyed_dispatch_last_code",
        "ready_keyed_dispatch_last_listener_count",
        "ready_open_compat_attempted",
        "ready_open_compat_call_ok",
        "ready_open_compat_count",
        "ready_open_compat_last_tick",
        "ready_open_compat_open_calls_before",
        "ready_open_compat_open_calls_after",
        "ready_open_compat_can_send_before",
        "ready_open_compat_can_send_after",
        "ready_open_compat_peer_writer",
        "ready_open_compat_peer_route_tag",
        "ready_open_compat_peer_registry_index",
        "ready_open_compat_parent_sample_ok",
        "ready_open_compat_parent_state",
        "ready_open_compat_parent_ready_flags",
        "ready_open_compat_parent_ready_flag_set",
        "ready_open_compat_parent_ready_state_ok",
        "ready_open_compat_parent_ready",
        "ready_open_compat_parent_state_target",
        "ready_open_compat_small_route_sample_ok",
        "ready_open_compat_small_route_next_available",
        "ready_open_compat_small_route_next_slot_present",
        "ready_open_compat_small_route_count",
        "ready_open_compat_small_route_limit",
        "ready_open_compat_small_route_sequence_counter",
        "ready_open_compat_small_route_next_sequence",
        "ready_open_compat_small_route_collision_index",
        "ready_open_compat_mark_ready_attempted",
        "ready_open_compat_mark_ready_call_ok",
        "ready_open_compat_mark_ready_before",
        "ready_open_compat_mark_ready_after",
        "ready_open_compat_mark_ready_failure",
        "ready_open_compat_failure",
        "luxor_network_check_compat_enabled",
        "luxor_network_check_compat_hook_attempted",
        "luxor_network_check_compat_hook_installed",
        "luxor_network_check_compat_calls",
        "luxor_network_check_compat_original_true",
        "luxor_network_check_compat_original_false",
        "luxor_network_check_compat_forced_true",
        "luxor_network_check_compat_last_original",
        "luxor_network_check_compat_last_returned",
        "connection_state_update_task_hook_attempted",
        "connection_state_update_task_hook_installed",
        "connection_state_update_task_calls",
        "connection_state_update_task_transitions_to_state5",
        "connection_state_update_task_last_caller_rva",
        "connection_state_update_task_last_active",
        "connection_state_update_task_last_network_calls_before",
        "connection_state_update_task_last_network_calls_after",
        "connection_state_update_task_last_delta_millis",
        "connection_state_update_task_last_state_before",
        "connection_state_update_task_last_state_after",
        "connection_state_update_task_last_sub_state_before",
        "connection_state_update_task_last_sub_state_after",
        "connection_state_update_task_last_ready_before",
        "connection_state_update_task_last_ready_after",
        "active_failed_substate9_hook_attempted",
        "active_failed_substate9_hook_installed",
        "active_failed_substate9_calls",
        "active_failed_substate9_last_caller_rva",
        "active_failed_substate9_last_pointer",
        "active_failed_substate9_last_active_before",
        "active_failed_substate9_last_active_after",
        "active_failed_substate9_last_result",
        "active_failed_substate9_last_local_user_before",
        "active_failed_substate9_last_local_user_after",
        "active_failed_substate9_last_state_before",
        "active_failed_substate9_last_state_after",
        "active_failed_substate9_last_sub_state_before",
        "active_failed_substate9_last_sub_state_after",
        "active_failed_substate9_last_ready_before",
        "active_failed_substate9_last_ready_after",
        "active_state5_wide_hook_attempt_mask",
        "active_state5_wide_hook_install_mask",
        "active_state5_wide_hook_all_bits",
        "active_state5_wide_calls",
        "active_state5_wide_transitions_to_state5",
        "active_state5_wide_last_hook_bit",
        "active_state5_wide_last_function_rva",
        "active_state5_wide_last_caller_rva",
        "active_state5_wide_last_active",
        "active_state5_wide_last_context",
        "active_state5_wide_last_sender_code",
        "active_state5_wide_last_result",
        "active_state5_wide_last_delta_millis",
        "active_state5_wide_last_local_user_before",
        "active_state5_wide_last_local_user_after",
        "active_state5_wide_last_state_before",
        "active_state5_wide_last_state_after",
        "active_state5_wide_last_sub_state_before",
        "active_state5_wide_last_sub_state_after",
        "active_state5_wide_last_ready_before",
        "active_state5_wide_last_ready_after",
        "ready_precondition_failure",
        "ready_connect_system",
        "ready_connect_vtable",
        "ready_connect_get_channel_fn",
        "ready_connect_state_fn",
        "ready_connect_channel",
        "ready_channel_vtable",
        "ready_channel_can_send_fn",
        "ready_channel_state_fn",
        "ready_connect_sender",
        "ready_sender_vtable",
        "ready_session_connection_registry",
        "ready_session_connection_registry_vtable",
        "ready_session_connection_registry_attach_fn",
        "ready_session_connection_registry_lookup_fn",
        "ready_session_connection_registry_step80_fn",
        "ready_session_connection_registry_stepd0_fn",
        "ready_session_connection_registry_stepd8_fn",
        "ready_channel_qword_08",
        "ready_channel_field_20",
        "ready_channel_raw_48",
        "ready_channel_byte_49",
        "ready_channel_byte_4a",
        "ready_channel_byte_4b",
        "ready_channel_can_send_raw_4c",
        "ready_channel_byte_4d",
        "ready_channel_byte_4e",
        "ready_channel_byte_4f",
        "ready_channel_qword_58",
        "ready_channel_qword_70",
        "ready_channel_qword_88",
        "ready_channel_qword_90",
        "ready_connect_state",
        "ready_channel_state",
        "ready_channel_can_send",
        "match_setting_sync_requested",
        "match_setting_sync_state_read_ok",
        "match_setting_sync_state",
        "match_setting_sync_connected_raw",
        "match_setting_sync_character_complete",
        "match_setting_sync_stage_complete",
        "match_setting_sync_completed_raw",
        "steam_lobby_probe_completed",
        "steam_lobby_probe_ok",
        "steam_lobby_probe_target_visible",
        "steam_lobby_probe_filters_applied",
        "steam_lobby_probe_failure",
        "steam_lobby_matrix_full_count",
        "target_owner_id",
        "steam_lobby_target_room",
        "steam_lobby_target_id",
        "steam_lobby_target_owner_id",
    )
    return {key: stage.get(key) for key in keys if key in stage}


def direct_release_host_ready_checks(
    stage: dict[str, Any] | None,
    expected: dict[str, Any],
    *,
    require_steam_visible: bool,
) -> dict[str, Any]:
    if not stage:
        return {"ok": False, "failure": "no-online-stage-event"}

    current_scene_text = " ".join(
        str(stage.get(key, ""))
        for key in (
            "current_scene_class",
            "current_scene_name",
        )
    )
    in_room_state = int_value(stage.get("player_match_in_room_state"), 0)
    in_room_state_text = " ".join(
        str(stage.get(key, ""))
        for key in (
            "player_match_in_room_state_class",
            "player_match_in_room_state_name",
        )
    )

    checks = {
        "room_name": str(stage.get("room_name", "")) == str(
            expected.get("online_stage_room_name", "")
        ),
        "session_name": str(stage.get("session_name", "")) == str(
            expected.get("online_stage_session_name", "")
        ),
        "native_session_name": str(stage.get("native_session_name", ""))
        == str(expected.get("online_stage_native_session_name", "")),
        "target_owner_id": int_value(stage.get("target_owner_id"), 0)
        == int_value(expected.get("online_stage_target_owner_id"), 0),
        "client_role_host": str(stage.get("client_role", "")) == "host",
        "player_match_scene": "PlayerMatchLobbyScene" in current_scene_text,
        "player_match_in_room_ok": bool(
            stage.get("player_match_in_room_ok")
        ),
        "player_match_in_room_state": in_room_state != 0,
        "player_match_in_room_state_class": (
            "PlayerMatchInRoomState" in in_room_state_text
        ),
        "player_match_in_room_not_session_connecting": (
            bool(stage.get("player_match_in_room_session_connecting_query_ok"))
            and not bool(stage.get("player_match_in_room_session_connecting"))
        ),
        "match_data_present": bool(stage.get("match_data_present")),
        "host_create_request_ok": bool(stage.get("host_create_request_ok")),
        "create_callback_ok": bool(stage.get("create_callback_seen"))
        and bool(stage.get("create_callback_result")),
        "steam_lobby_visible": (
            bool(stage.get("steam_lobby_probe_completed"))
            and bool(stage.get("steam_lobby_probe_ok"))
            and bool(stage.get("steam_lobby_probe_target_visible"))
            and bool(stage.get("steam_lobby_probe_filters_applied"))
            and str(stage.get("steam_lobby_probe_failure", "")) == "ok"
        )
        if require_steam_visible
        else True,
    }
    missing = [key for key, ok in checks.items() if not ok]
    return {
        "ok": not missing,
        "missing": missing,
        "checks": checks,
        "required_steam_visible": require_steam_visible,
        "active_scene_only": True,
        "native_named_session_state": int_value(
            stage.get("native_named_session_state"), -1
        ),
        "native_named_session_state_byte": int_value(
            stage.get("native_named_session_state_byte"), 0xFF
        ),
        "native_named_session_lobby_id": (
            f"0x{int_value(stage.get('native_named_session_lobby_id'), 0):X}"
            if int_value(stage.get("native_named_session_lobby_id"), 0)
            else "0x0"
        ),
        "player_match_in_room_state": (
            f"0x{in_room_state:X}" if in_room_state else "0x0"
        ),
    }


def wait_for_direct_release_host_advertisement(
    *,
    root: dict[str, Any],
    request_id: str,
    expected: dict[str, Any],
    min_mtime: float,
    allowed_pids: set[int],
    file_offsets: dict[str, int] | None,
    require_steam_visible: bool,
    timeout_seconds: float,
    settle_seconds: float,
) -> dict[str, Any]:
    deadline = time.time() + max(0.1, timeout_seconds)
    root_path = Path(root["path"])
    latest_stage: dict[str, Any] | None = None
    polls = 0
    process_missing_counts = {pid: 0 for pid in allowed_pids}
    process_query_unavailable_count = 0
    stable_ready_observations = 0
    last_ready_observation = 0
    while time.time() < deadline:
        polls += 1
        process_query = query_current_sc6_processes(allowed_pids)
        if process_query.get("valid"):
            process_query_unavailable_count = 0
        else:
            process_query_unavailable_count += 1
        process_query["unavailable_consecutive"] = (
            process_query_unavailable_count
        )
        effective_pids = update_process_presence(
            process_query,
            allowed_pids,
            process_missing_counts,
        )
        if process_query_unavailable_count >= PROCESS_MISSING_FAIL_POLLS:
            return {
                "ok": False,
                "polls": polls,
                "failure": "process query unavailable during host advertisement",
                "process_query": process_query,
            }
        if allowed_pids - effective_pids:
            return {
                "ok": False,
                "polls": polls,
                "failure": "SC6 process exited during host advertisement",
                "process_query": process_query,
                "missing_pids": sorted(allowed_pids - effective_pids),
            }
        events = read_jsonl_events(
            root_path,
            request_id,
            min_mtime=min_mtime,
            allowed_pids=allowed_pids,
            file_offsets=file_offsets,
        )
        latest_stage = latest_event(events, "rollback_online_stage")
        latest_stage = merge_online_stage_callbacks(
            latest_stage,
            [
                e for e in events
                if e.get("event") == "rollback_online_stage_callback"
            ],
        )
        if latest_stage:
            ready = direct_release_host_ready_checks(
                latest_stage,
                expected,
                require_steam_visible=require_steam_visible,
            )
            if ready["ok"]:
                observation = int_value(latest_stage.get("ts_qpc"), 0)
                if not observation:
                    observation = fnv1a64(
                        json.dumps(latest_stage, sort_keys=True, default=str)
                    )
                if observation != last_ready_observation:
                    stable_ready_observations += 1
                    last_ready_observation = observation
                if stable_ready_observations >= ACTIVE_SCENE_STABLE_OBSERVATIONS:
                    if settle_seconds > 0:
                        time.sleep(settle_seconds)
                    return {
                        "ok": True,
                        "polls": polls,
                        "elapsed_seconds": round(
                            max(0.0, timeout_seconds - (deadline - time.time())),
                            3,
                        ),
                        "settle_seconds": round(max(0.0, settle_seconds), 3),
                        "required_steam_visible": require_steam_visible,
                        "stable_active_scene_observations": (
                            stable_ready_observations
                        ),
                        "stable_active_scene_required": (
                            ACTIVE_SCENE_STABLE_OBSERVATIONS
                        ),
                        "process_query": process_query,
                        "ready_checks": ready,
                        "latest_online_stage": (
                            direct_release_host_advertise_summary(latest_stage)
                        ),
                    }
            else:
                stable_ready_observations = 0
                last_ready_observation = 0
        time.sleep(RUNNER_PHASE_POLL_SECONDS)
    return {
        "ok": False,
        "polls": polls,
        "elapsed_seconds": round(max(0.0, timeout_seconds), 3),
        "settle_seconds": 0.0,
        "required_steam_visible": require_steam_visible,
        "failure": "timed out waiting for host room-ready advertisement",
        "stable_active_scene_observations": stable_ready_observations,
        "stable_active_scene_required": ACTIVE_SCENE_STABLE_OBSERVATIONS,
        "ready_checks": direct_release_host_ready_checks(
            latest_stage,
            expected,
            require_steam_visible=require_steam_visible,
        ),
        "latest_online_stage": direct_release_host_advertise_summary(
            latest_stage
        ),
    }


STOCK_REQUIRED_GATES = [
    "stock_hooks_installed",
    "nonnull_session_observed",
    "stock_input_observed",
    "battle_sync_observed",
    "receive_enqueue_observed",
]


def best_stock_online_event(events: list[dict[str, Any]]) -> dict[str, Any] | None:
    matches = [e for e in events if e.get("event") == "rollback_live_online_capture"]
    if not matches:
        return None
    return max(
        matches,
        key=lambda e: (
            sum(1 for gate in STOCK_REQUIRED_GATES if bool(e.get(gate))),
            1 if bool(e.get("ok")) else 0,
            int(e.get("ts_qpc") or 0),
        ),
    )


def event_pid_mapping_failures(root: dict[str, Any], pids: list[int]) -> list[str]:
    failures: list[str] = []
    live_pids = [
        int_value(pid, -1)
        for pid in root.get("live_trace_pids", [])
        if int_value(pid, -1) >= 0
    ]
    if len(pids) != 1:
        failures.append(f"expected_1_event_pid got={pids or '-'}")
    if len(live_pids) > 1:
        failures.append(f"ambiguous_live_trace_pid got={live_pids}")
    if len(pids) == 1 and len(live_pids) == 1 and pids[0] != live_pids[0]:
        failures.append(f"event_pid_mismatch event={pids[0]} root={live_pids[0]}")
    return failures


def current_event_pid_failures(
    pids: list[int],
    allowed_pids: set[int],
    *,
    root: dict[str, Any] | None = None,
    current_pids: set[int] | None = None,
) -> list[str]:
    failures: list[str] = []
    if len(pids) != 1:
        failures.append(f"expected_1_current_event_pid got={pids or '-'}")
    elif pids[0] not in allowed_pids:
        failures.append(
            f"event_pid_not_current event={pids[0]} current={sorted(allowed_pids)}"
        )
    elif current_pids is not None and pids[0] not in current_pids:
        failures.append(
            f"event_pid_exited event={pids[0]} current={sorted(current_pids)}"
        )

    if root is not None and current_pids is not None:
        root_pids = [
            int_value(pid, -1)
            for pid in root.get("live_trace_pids", [])
            if int_value(pid, -1) >= 0
        ]
        role = str(root.get("role", "?"))
        if len(root_pids) == 1 and root_pids[0] in allowed_pids:
            expected_pid = root_pids[0]
            if expected_pid not in current_pids:
                failures.append(
                    f"sc6_process_exited role={role} pid={expected_pid} "
                    f"current={sorted(current_pids)}"
                )
    return failures


def crash_dialog_failures(
    root: dict[str, Any],
    indicators: list[dict[str, Any]],
) -> list[str]:
    if not indicators:
        return []
    role = str(root.get("role", "?"))
    root_pids = {
        int_value(pid, -1)
        for pid in root.get("live_trace_pids", [])
        if int_value(pid, -1) >= 0
    }
    failures: list[str] = []
    for indicator in indicators:
        pid = int_value(indicator.get("pid"), -1)
        related_pid = int_value(indicator.get("related_pid"), -1)
        applies_to_root = (
            (pid in root_pids)
            or (related_pid in root_pids)
            or not root_pids
        )
        if not applies_to_root:
            continue
        title = str(indicator.get("title") or "").replace("\n", " ")[:120]
        process_name = str(indicator.get("process_name") or "")
        source = str(indicator.get("source") or "unknown")
        prefix = (
            "sc6_hung_window"
            if source == "hung-window" else "sc6_crash_dialog"
        )
        failures.append(
            f"{prefix} role={role} pid={pid} related={related_pid} "
            f"source={source} process={process_name or '-'} "
            f"title={title or '-'}"
        )
    return failures


def configured_event_failures(
    configured: dict[str, Any] | None,
    expected_case: str,
) -> list[str]:
    if configured is None:
        return ["rollback_lab_configured"]
    failures: list[str] = []
    if configured.get("source") != "request-file":
        failures.append(f"configured_source={configured.get('source', 'missing')}")
    if configured.get("case") != expected_case:
        failures.append(f"configured_case={configured.get('case', 'missing')}")
    if not bool(configured.get("trace_enabled")):
        failures.append("configured_trace_enabled")
    return failures


def configured_request_acknowledgement(
    configured: dict[str, Any] | None,
    *,
    request_id: str,
    protocol_version: int,
    generation: int,
    phase: str,
) -> dict[str, Any]:
    """Describe what the current trace protocol can actually acknowledge.

    Shipping builds echo the full request id but do not yet echo the added
    protocol fields.  The version and generation are therefore bound into the
    request-id envelope and reported as such, never presented as native echoes.
    """
    observed_id = str((configured or {}).get("request_id") or "")
    version_echo_present = bool(
        configured and "request_protocol_version" in configured
    )
    generation_echo_present = bool(
        configured and "request_generation" in configured
    )
    phase_echo_present = bool(configured and "request_phase" in configured)
    failures: list[str] = []
    expected_envelope = f"two-client-v{protocol_version}-g{generation:016x}-"
    envelope_valid = request_id.startswith(expected_envelope)
    if not envelope_valid:
        failures.append("config_ack_request_id_envelope_invalid")
    if configured is None:
        failures.append("config_ack_missing")
    elif observed_id != request_id:
        failures.append(
            f"config_ack_request_id={observed_id or 'missing'}"
        )
    if version_echo_present and int_value(
        configured.get("request_protocol_version"), -1
    ) != protocol_version:
        failures.append("config_ack_protocol_version_mismatch")
    if generation_echo_present and int_value(
        configured.get("request_generation"), -1
    ) != generation:
        failures.append("config_ack_generation_mismatch")
    if phase_echo_present and str(configured.get("request_phase") or "") != phase:
        failures.append("config_ack_phase_mismatch")
    return {
        "ok": not failures,
        "request_id_expected": request_id,
        "request_id_observed": observed_id,
        "request_id_acknowledged": bool(configured) and observed_id == request_id,
        "request_id_envelope_valid": envelope_valid,
        "protocol_version_expected": protocol_version,
        "generation_expected": generation,
        "phase_expected": phase,
        "native_protocol_echo_supported": (
            version_echo_present and generation_echo_present and phase_echo_present
        ),
        "protocol_version_observed": (
            configured.get("request_protocol_version")
            if version_echo_present else None
        ),
        "generation_observed": (
            configured.get("request_generation")
            if generation_echo_present else None
        ),
        "phase_observed": (
            configured.get("request_phase") if phase_echo_present else None
        ),
        "identity_binding": "versioned-request-id-envelope",
        "failures": failures,
    }


def horse_udp_failures(gekko_udp: dict[str, Any] | None) -> list[str]:
    if gekko_udp is None:
        return ["rollback_gekko_udp_selftest"]

    failures: list[str] = []
    if not bool(gekko_udp.get("ok")):
        failures.append(
            f"gekko_udp_ok failure={gekko_udp.get('failure', 'missing')}"
        )
    for gate in HORSE_UDP_REQUIRED_TRUE_GATES:
        if not bool(gekko_udp.get(gate)):
            failures.append(f"gekko_udp_gate:{gate}")
    for counter in HORSE_UDP_REQUIRED_POSITIVE_COUNTERS:
        if int_value(gekko_udp.get(counter), 0) <= 0:
            failures.append(f"gekko_udp_counter:{counter}")
    for field in HORSE_UDP_REQUIRED_ZERO_ERRORS:
        value = int_value(gekko_udp.get(field), 0)
        if value != 0:
            failures.append(f"gekko_udp_error:{field}={value}")

    port_a = int_value(gekko_udp.get("port_a"), 0)
    port_b = int_value(gekko_udp.get("port_b"), 0)
    if port_a <= 0:
        failures.append("gekko_udp_port:port_a")
    if port_b <= 0:
        failures.append("gekko_udp_port:port_b")
    if port_a > 0 and port_b > 0 and port_a == port_b:
        failures.append(f"gekko_udp_ports_not_distinct:{port_a}")

    return failures


def expected_role_config(
    root: dict[str, Any],
    *,
    sandbox_root: Path,
    sandbox_box: str,
    host_sidecar_port: int,
    sandbox_sidecar_port: int,
    activation_token: str,
    host_online_stage_main_user_id_override: int,
    sandbox_online_stage_main_user_id_override: int,
    online_stage_native_session_name: str,
    online_stage_session_name: str,
    online_stage_room_name: str,
    online_stage_target_owner_id: int,
    online_stage_goal: str,
    skip_online_stage_drive: bool,
    replay_input_file: Path,
    replay_divergence_frame: int,
    replay_divergence_window: int,
    mode: str,
    native_input_source_slot: int,
    input_delay: int,
    network_profile: str,
    fault_seed: int,
    expected_build_id: int,
    expected_schema_id: int,
    launch_left_character: int = -1,
    launch_right_character: int = -1,
    launch_stage: int = -1,
) -> dict[str, Any]:
    role = str(root.get("role", ""))
    if role == "host":
        local_peer = DEFAULT_ACTIVATION_SOURCE_PEER
        remote_peer = DEFAULT_ACTIVATION_DESTINATION_PEER
        local_port = host_sidecar_port
        remote_port = sandbox_sidecar_port
        local_replay_player = 0
        remote_replay_player = 1
        local_player_slot = 0
        main_user_id_override = host_online_stage_main_user_id_override
    else:
        local_peer = DEFAULT_ACTIVATION_DESTINATION_PEER
        remote_peer = DEFAULT_ACTIVATION_SOURCE_PEER
        local_port = sandbox_sidecar_port
        remote_port = host_sidecar_port
        local_replay_player = 1
        remote_replay_player = 0
        local_player_slot = 1
        main_user_id_override = sandbox_online_stage_main_user_id_override
    return {
        "client_role": role,
        "sandbox_root": str(sandbox_root),
        "sandbox_box": sandbox_box,
        "local_peer_id": local_peer,
        "remote_peer_id": remote_peer,
        "sidecar_local_port": local_port,
        "sidecar_remote_port": remote_port,
        "sidecar_remote_addr": "127.0.0.1",
        "activation_source_peer": local_peer,
        "activation_destination_peer": remote_peer,
        "activation_session_id": DEFAULT_ACTIVATION_SESSION_ID,
        "activation_token": activation_token,
        "activation_token_hash": fnv1a64(activation_token),
        "online_stage_main_user_id_override": main_user_id_override,
        "online_stage_native_session_name": online_stage_native_session_name,
        "online_stage_session_name": online_stage_session_name,
        "online_stage_room_name": online_stage_room_name,
        "online_stage_target_owner_id": online_stage_target_owner_id,
        "online_stage_goal": online_stage_goal,
        "skip_online_stage_drive": skip_online_stage_drive,
        "direct_stage_observe_only": skip_online_stage_drive,
        "replay_input_file": str(replay_input_file),
        "local_replay_player": local_replay_player,
        "remote_replay_player": remote_replay_player,
        "replay_divergence_frame": replay_divergence_frame,
        "replay_divergence_window": replay_divergence_window,
        "mode": mode,
        "production_enabled": mode == "mirrored-versus",
        "bind_address": "0.0.0.0",
        "bind_port": local_port,
        "peer_address": "127.0.0.1",
        "peer_port": remote_port,
        "local_player_slot": local_player_slot,
        "native_input_source_slot": native_input_source_slot,
        "lifecycle_mode": (
            "mirrored-versus"
            if mode == "mirrored-versus" else "stock-online-pvp"
        ),
        "production_local_peer": local_peer,
        "production_remote_peer": remote_peer,
        "secret": activation_token,
        "input_delay": input_delay,
        "network_profile": network_profile,
        "fault_seed": fault_seed,
        "expected_build_id": expected_build_id,
        "expected_schema_id": expected_schema_id,
        "launch_left_character": launch_left_character,
        "launch_right_character": launch_right_character,
        "launch_stage": launch_stage,
    }


def role_manifest_failures(
    role_manifest: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if role_manifest is None:
        return ["rollback_two_client_role_manifest"]
    failures: list[str] = []
    for key in (
        "client_role",
        "sandbox_box",
        "sidecar_remote_addr",
        "network_profile",
    ):
        if str(role_manifest.get(key, "")) != str(expected.get(key, "")):
            failures.append(f"role_manifest_{key}")
    for key in (
        "local_peer_id",
        "remote_peer_id",
        "sidecar_local_port",
        "sidecar_remote_port",
        "online_stage_main_user_id_override",
        "fault_seed",
    ):
        if int_value(role_manifest.get(key), -1) != int_value(expected.get(key), -2):
            failures.append(f"role_manifest_{key}")
    if int_value(role_manifest.get("activation_token_hash"), 0) != int_value(
        expected.get("activation_token_hash"), 0
    ):
        failures.append("role_manifest_activation_token_hash")
    if not bool(role_manifest.get("sidecar_requested")):
        failures.append("role_manifest_sidecar_requested")
    return failures


def sidecar_failures(
    bind: dict[str, Any] | None,
    handshake: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    failures: list[str] = []
    if bind is None:
        failures.append("rollback_sidecar_bind")
    else:
        for gate in (
            "ok",
            "wsa_started",
            "socket_open",
            "bound_loopback",
            "nonblocking",
            "udp_connreset_disabled",
        ):
            if not bool(bind.get(gate)):
                failures.append(f"sidecar_bind_{gate}")
        if bool(bind.get("reserved_steam_port_rejected")):
            failures.append("sidecar_bind_reserved_steam_port")
        for key in ("sidecar_local_port", "sidecar_remote_port"):
            if int_value(bind.get(key), -1) != int_value(expected.get(key), -2):
                failures.append(f"sidecar_bind_{key}")
    if handshake is None:
        failures.append("rollback_sidecar_handshake")
    else:
        for gate in ("ok", "sent_hello", "received_hello", "validated_peer"):
            if not bool(handshake.get(gate)):
                failures.append(f"sidecar_handshake_{gate}")
        for key in ("local_peer_id", "remote_peer_id"):
            if int_value(handshake.get(key), -1) != int_value(expected.get(key), -2):
                failures.append(f"sidecar_handshake_{key}")
    return failures


def live_traffic_failures(stock_online: dict[str, Any] | None) -> list[str]:
    if stock_online is None:
        return ["rollback_live_online_capture"]
    failures: list[str] = []
    for gate in (
        "capture_ready",
        "live_capture_complete",
        "stock_hooks_installed",
        "stock_trace_active",
        "boundary_hooks_installed",
        "boundary_trace_active",
        "nonnull_session_observed",
        "stock_input_observed",
        "battle_sync_observed",
        "receive_enqueue_observed",
        "drain_observed",
        "consumer_observed",
        "live_order_proven",
    ):
        if not bool(stock_online.get(gate)):
            failures.append(f"live_online_{gate}")
    if bool(stock_online.get("boundary_violation")):
        failures.append("live_online_no_boundary_violation")
    return failures


def activation_failures(candidate: dict[str, Any] | None) -> list[str]:
    if candidate is None:
        return ["rollback_live_activation_candidate"]
    failures: list[str] = []
    for gate in (
        "ok",
        "activation_ready",
        "explicit_operator_enable",
        "capture_ready",
        "live_capture_complete",
        "stock_send_observed",
        "receive_observed",
        "drain_consumer_observed",
        "live_order_proven",
        "session_pointer_bound",
        "input_log_bound",
        "route_provenance_valid",
        "strict_identity",
        "horse_route_allowed",
        "route_identity_matches",
    ):
        if not bool(candidate.get(gate)):
            failures.append(f"activation_{gate}")
    return failures


def exact_host_target_configured(expected: dict[str, Any]) -> bool:
    return bool(
        str(expected.get("online_stage_room_name", "")).strip()
        or str(expected.get("online_stage_session_name", "")).strip()
        or int_value(expected.get("online_stage_target_owner_id"), 0)
    )


def exact_host_result_selected(online_stage: dict[str, Any] | None) -> bool:
    stage = online_stage or {}
    standard = bool(stage.get("find_result_target_found")) and int_value(
        stage.get("find_result_selected_index"), -1
    ) >= 0
    no_presence = bool(
        stage.get("native_no_presence_find_target_found")
    ) and int_value(stage.get("native_no_presence_find_selected_index"), -1) >= 0
    invite_bridge = (
        str(stage.get("stock_join_route", "")) == "invite-fallback"
        and bool(stage.get("stock_native_bridge_complete"))
        and bool(stage.get("steam_join_lobby_ok"))
        and int_value(stage.get("stock_offer_lobby_id"), 0) != 0
        and int_value(stage.get("stock_offer_lobby_id"), 0)
        == int_value(stage.get("native_named_session_lobby_id"), 0)
    )
    return standard or no_presence or invite_bridge


def online_stage_failures(
    online_stage: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if online_stage is None:
        return ["rollback_online_stage"]
    failures: list[str] = []
    role = str(expected.get("client_role", ""))

    if not bool(online_stage.get("ok")):
        failures.append("online_stage_ok")
    if str(online_stage.get("client_role", "")) != role:
        failures.append("online_stage_client_role")
    expected_native = str(expected.get("online_stage_native_session_name", ""))
    if expected_native and str(
        online_stage.get("native_session_name", "")
    ) != expected_native:
        failures.append("online_stage_native_session_name")
    if str(online_stage.get("session_name", "")) != str(
        expected.get("online_stage_session_name", "")
    ):
        failures.append("online_stage_session_name")
    if str(online_stage.get("room_name", "")) != str(
        expected.get("online_stage_room_name", "")
    ):
        failures.append("online_stage_room_name")
    expected_target_owner_id = int_value(
        expected.get("online_stage_target_owner_id"), 0
    )
    if expected_target_owner_id and int_value(
        online_stage.get("target_owner_id"), 0
    ) != expected_target_owner_id:
        failures.append("online_stage_target_owner_id")
    expected_main_user_id_override = int_value(
        expected.get("online_stage_main_user_id_override"), -1
    )
    if int_value(online_stage.get("main_user_id_override"), -2) != (
        expected_main_user_id_override
    ):
        failures.append("online_stage_main_user_id_override")
    if expected_main_user_id_override >= 0:
        if int_value(online_stage.get("session_main_user_id"), -1) != (
            expected_main_user_id_override
        ):
            failures.append("online_stage_session_main_user_id")
        if not bool(online_stage.get("session_main_user_id_overridden")):
            failures.append("online_stage_session_main_user_id_overridden")
    if bool(online_stage.get("online_session_probe_attempted")):
        slot_kind = str(
            online_stage.get("online_session_find_sessions_slot_kind", "")
        )
        if slot_kind and slot_kind != "steam":
            failures.append(f"online_stage_online_session_slot={slot_kind}")

    if bool(online_stage.get("online_stage_cleanup_only")):
        for gate in (
            "online_stage_requested",
            "game_thread",
            "session_hub_present",
        ):
            if not bool(online_stage.get(gate)):
                failures.append(f"online_stage_{gate}")
        if role == "host":
            for gate in (
                "destroy_start_requested",
                "destroy_start_wait_complete",
            ):
                if not bool(online_stage.get(gate)):
                    failures.append(f"online_stage_{gate}")
            if bool(online_stage.get("destroy_start_call_ok")):
                for gate in (
                    "destroy_callback_seen",
                    "destroy_callback_result",
                ):
                    if not bool(online_stage.get(gate)):
                        failures.append(f"online_stage_{gate}")
        failure_reason = str(online_stage.get("failure", ""))
        if failure_reason and failure_reason != "ok":
            failures.append(f"online_stage_failure={failure_reason}")
        return failures

    for gate in (
        "background_idle_override_value_read",
        "background_idle_override_command_ok",
    ):
        if not bool(online_stage.get(gate)):
            failures.append(f"online_stage_{gate}")

    if bool(online_stage.get("online_stage_find_only")):
        for gate in (
            "online_stage_requested",
            "game_thread",
            "flow_manager_present",
            "session_hub_present",
            "client_find_requested",
            "client_find_request_ok",
            "find_result_target_found",
            "play_side_requested",
            "play_side_request_ok",
        ):
            if role == "host" and gate.startswith("client_find"):
                continue
            if role == "host" and gate == "find_result_target_found":
                continue
            if not bool(online_stage.get(gate)):
                failures.append(f"online_stage_{gate}")
        failure_reason = str(online_stage.get("failure", ""))
        if failure_reason and failure_reason != "ok":
            failures.append(f"online_stage_failure={failure_reason}")
        return failures

    for gate in (
        "online_stage_requested",
        "game_thread",
        "flow_manager_present",
        "session_hub_present",
        "play_side_requested",
        "play_side_request_ok",
    ):
        if not bool(online_stage.get(gate)):
            failures.append(f"online_stage_{gate}")

    if role == "host":
        for gate in (
            "host_create_requested",
            "host_create_request_ok",
            "match_data_present",
            "start_latch_requested",
            "start_latch_ok",
        ):
            if not bool(online_stage.get(gate)):
                failures.append(f"online_stage_{gate}")
    elif role == "sandbox":
        for gate in (
            "client_find_requested",
            "client_find_request_ok",
            "find_callback_seen",
            "find_callback_result",
            "client_join_request_ok",
            "match_data_present",
        ):
            if not bool(online_stage.get(gate)):
                failures.append(f"online_stage_{gate}")
        if not exact_host_target_configured(expected):
            failures.append("online_stage_exact_host_target_not_configured")
        if not exact_host_result_selected(online_stage):
            failures.append("online_stage_exact_host_result_not_selected")
    else:
        failures.append(f"online_stage_unknown_role={role or 'missing'}")

    if bool(online_stage.get("start_latch_requested")):
        for gate in (
            "ready_preconditions_sampled",
            "ready_preconditions_ok",
            "ready_channel_can_send",
        ):
            if not bool(online_stage.get(gate)):
                failures.append(f"online_stage_{gate}")
        if int_value(online_stage.get("ready_connect_state"), -1) != 3:
            failures.append(
                "online_stage_ready_connect_state="
                f"{online_stage.get('ready_connect_state')}"
            )
        if int_value(online_stage.get("ready_channel_state"), -1) != 1:
            failures.append(
                "online_stage_ready_channel_state="
                f"{online_stage.get('ready_channel_state')}"
            )

    if bool(online_stage.get("match_setting_sync_requested")):
        if not bool(online_stage.get("match_setting_sync_raw_sampled")):
            failures.append("online_stage_match_setting_sync_raw_sampled")
        if not bool(online_stage.get("match_setting_sync_state_read_ok")):
            failures.append("online_stage_match_setting_sync_state_read_ok")
        if int_value(online_stage.get("match_setting_sync_state"), -1) < 1:
            failures.append(
                "online_stage_match_setting_sync_state="
                f"{online_stage.get('match_setting_sync_state')}"
            )

    failure_reason = str(online_stage.get("failure", ""))
    if failure_reason and failure_reason != "ok":
        failures.append(f"online_stage_failure={failure_reason}")
    return failures


PLAYER_MATCH_NAV_SCENES = (
    "PlayerMatchLobbyScene",
    "PlayerMatchSetupScene",
    "PlayerMatchScene",
)


def online_stage_scene_text(online_stage: dict[str, Any] | None) -> str:
    if online_stage is None:
        return ""
    return " ".join(
        str(online_stage.get(key, ""))
        for key in (
            "current_scene_class",
            "current_scene_name",
            "next_scene_class",
            "next_scene_name",
            "next_scene_direct_class",
            "next_scene_direct_name",
        )
    )


def online_stage_current_scene_text(online_stage: dict[str, Any] | None) -> str:
    if online_stage is None:
        return ""
    return " ".join(
        str(online_stage.get(key, ""))
        for key in ("current_scene_class", "current_scene_name")
    )


def online_stage_gate_expected_scene(phase: str) -> str:
    return {
        "menu-ready": "MainMenuScene_C",
        "player-match-nav": (
            "PlayerMatchLobbyScene_C|PlayerMatchSetupScene_C|"
            "PlayerMatchScene_C"
        ),
        "player-match-lobby": (
            "PlayerMatchLobbyScene_C|PlayerMatchSetupScene_C|"
            "PlayerMatchScene_C"
        ),
        "player-match-battle": "PlayerMatchScene_C",
    }.get(phase, "")


def online_stage_gate_ok(
    online_stage: dict[str, Any] | None,
    phase: str,
) -> bool:
    current_text = online_stage_current_scene_text(online_stage)
    if phase == "menu-ready":
        return "MainMenuScene" in current_text
    if phase == "player-match-nav":
        return any(scene in current_text for scene in PLAYER_MATCH_NAV_SCENES)
    if phase == "player-match-lobby":
        return any(
            scene in current_text
            for scene in (
                "PlayerMatchLobbyScene",
                "PlayerMatchSetupScene",
                "PlayerMatchScene",
            )
        )
    if phase == "player-match-battle":
        return is_online_player_match_battle_scene_text(current_text)
    return False


def active_scene_context(
    online_stage: dict[str, Any] | None,
    phase: str,
) -> str:
    current = online_stage_current_scene_text(online_stage)
    if phase == "menu-ready" and "MainMenuScene" in current:
        return "main-menu"
    if phase == "player-match-nav" and any(
        scene in current for scene in PLAYER_MATCH_NAV_SCENES
    ):
        return "player-match-navigation"
    if phase == "player-match-lobby" and any(
        scene in current
        for scene in (
            "PlayerMatchLobbyScene",
            "PlayerMatchSetupScene",
            "PlayerMatchScene",
        )
    ):
        return "player-match-lobby-or-later"
    if phase == "player-match-battle" and is_online_player_match_battle_scene_text(
        current
    ):
        return "player-match-battle"
    return ""


def online_stage_gate_summary(
    online_stage: dict[str, Any] | None,
    phase: str,
) -> dict[str, Any]:
    gate_ok = online_stage_gate_ok(online_stage, phase)
    return {
        "gate": phase,
        "gate_ok": gate_ok,
        "active_scene_only": True,
        "active_scene_context": active_scene_context(online_stage, phase),
        "observation_ts_qpc": int_value(
            (online_stage or {}).get("ts_qpc"), 0
        ),
        "expected_scene": online_stage_gate_expected_scene(phase),
        "current_scene": (
            " ".join(
                str((online_stage or {}).get(key, ""))
                for key in ("current_scene_class", "current_scene_name")
            ).strip()
        ),
        "next_scene": (
            " ".join(
                str((online_stage or {}).get(key, ""))
                for key in ("next_scene_class", "next_scene_name")
            ).strip()
        ),
        "next_scene_is_diagnostic_only": True,
        "last_action": (
            (online_stage or {}).get("main_menu_nav_last_action", "")
        ),
        "action_accepted": bool(
            (online_stage or {}).get("main_menu_nav_last_action_accepted")
        ),
        "scene_transitioned": bool(
            (online_stage or {}).get("main_menu_nav_last_action_transitioned")
        ),
        "action_cooldown": int_value(
            (online_stage or {}).get("main_menu_nav_cooldown_remaining"), 0
        ),
        "diagnostic_reflection": bool(
            (online_stage or {}).get("online_stage_diagnostic_reflection")
        ),
        "automation_source": str(
            (online_stage or {}).get("main_menu_nav_automation_source")
            or (online_stage or {}).get("ui_gameflow_automation_source")
            or ""
        ),
    }


def semantic_ui_navigation_supported(
    ui_input_event: dict[str, Any] | None,
) -> bool:
    """Return whether the event came from the semantic Blueprint navigator.

    Field presence is intentional here.  A failed semantic dispatch can leave
    every value false, but it still proves that this build exposes the new
    FocusItem/OnDecide evidence schema.  XInput hook state is unrelated to
    whether that dispatcher is available.
    """

    event = ui_input_event or {}
    return any(
        key in event
        for key in (
            "semantic_action_started",
            "target_scene_queued",
            "focus_before_live_dispatchable",
            "focus_after_live_dispatchable",
        )
    )


def effective_navigation_state(
    online_stage: dict[str, Any] | None,
    ui_input_event: dict[str, Any] | None,
) -> dict[str, Any]:
    """Overlay newer per-action navigation state onto the stage snapshot.

    The stage event carries authoritative current-scene evidence, while the
    per-action event is emitted at the exact state-machine transition.  The
    latter can therefore be newer by one runner poll.  Preserve scene fields
    from the stage and overlay only navigation transaction fields.
    """

    stage = dict(online_stage or {})
    event = ui_input_event or {}
    stage_ts = int_value(stage.get("ts_qpc"), 0)
    event_ts = int_value(event.get("ts_qpc"), 0)
    source = "rollback_online_stage" if stage else "none"
    source_ts = stage_ts

    stage_request = str(stage.get("request_id") or "")
    event_request = str(event.get("request_id") or "")
    request_matches = not (
        stage_request and event_request and stage_request != event_request
    )
    event_is_newer = bool(event) and request_matches and (
        not stage
        or (event_ts > 0 and (stage_ts == 0 or event_ts >= stage_ts))
    )
    if event_is_newer:
        mappings = {
            "main_menu_navigation_state": "navigation_state_name",
            "main_menu_navigation_failure": "navigation_failure",
            "main_menu_navigation_generation": "navigation_generation",
            "main_menu_navigation_dispatch_id": "navigation_dispatch_id",
            "main_menu_navigation_step_attempts": "navigation_step_attempts",
            "main_menu_navigation_total_dispatches": (
                "navigation_total_dispatches"
            ),
            "main_menu_input_sequence_complete": "sequence_complete",
            "main_menu_input_last_reason": "reason",
            "main_menu_input_last_ok": "ok",
            "main_menu_input_last_key": "key_name",
        }
        for stage_key, event_key in mappings.items():
            if event_key in event:
                stage[stage_key] = event[event_key]
        if "navigation_total_dispatches" in event:
            stage["main_menu_input_attempts"] = event[
                "navigation_total_dispatches"
            ]
        elif "attempts" in event:
            stage["main_menu_input_attempts"] = event["attempts"]
        source = "rollback_main_menu_input_navigation"
        source_ts = event_ts

    stage["_navigation_state_source"] = source
    stage["_navigation_state_ts_qpc"] = source_ts
    return stage


def ui_navigation_evidence(
    ui_input_event: dict[str, Any] | None,
) -> dict[str, bool | str]:
    """Normalize semantic action, focus, queue, and scene evidence."""

    event = ui_input_event or {}
    semantic_supported = semantic_ui_navigation_supported(event)
    semantic_action_started = bool(event.get("semantic_action_started"))
    target_scene_queued = bool(event.get("target_scene_queued"))
    selection_changed = bool(event.get("selection_changed"))
    scene_transitioned = bool(event.get("scene_transitioned"))

    # Older transition code briefly reused action_acknowledged for action
    # start/queue evidence.  Keep those concepts separate even when reading
    # such a trace.  A genuine focus acknowledgement or active-scene change
    # remains sufficient.
    explicit_action_ack = bool(event.get("action_acknowledged")) and not (
        semantic_action_started or target_scene_queued
    )
    action_acknowledged = bool(
        explicit_action_ack or selection_changed or scene_transitioned
    )
    xinput_supported = bool(event.get("xinput_native_poller_hook_installed"))
    dispatcher = (
        "semantic-blueprint"
        if semantic_supported
        else "xinput"
        if xinput_supported
        else "unknown"
    )
    return {
        "semantic_blueprint_supported": semantic_supported,
        "xinput_supported": xinput_supported,
        "semantic_action_started": semantic_action_started,
        "target_scene_queued": target_scene_queued,
        "selection_changed": selection_changed,
        "scene_transitioned": scene_transitioned,
        "action_acknowledged": action_acknowledged,
        "dispatcher": dispatcher,
    }


def terminal_navigation_failure(
    online_stage: dict[str, Any] | None,
    ui_input_event: dict[str, Any] | None = None,
) -> dict[str, Any] | None:
    """Classify deterministic navigation failure using active-scene evidence."""
    stage = effective_navigation_state(online_stage, ui_input_event)
    if not stage:
        return None
    current = online_stage_current_scene_text(stage)
    if any(scene in current for scene in PLAYER_MATCH_NAV_SCENES):
        return None
    if "MainMenuScene" not in current:
        return None
    route_complete = bool(stage.get("main_menu_input_sequence_complete"))
    route_attempts = int_value(stage.get("main_menu_input_attempts"), 0)
    navigation_state = str(
        stage.get("main_menu_navigation_state") or ""
    ).strip().lower()
    navigation_failure = str(
        stage.get("main_menu_navigation_failure") or ""
    ).strip().lower()
    player_attempts = int_value(
        stage.get("player_match_scene_request_attempts"), 0
    )
    nav_attempts = int_value(stage.get("online_nav_attempts"), 0)
    reason = str(
        stage.get("main_menu_input_last_reason")
        or stage.get("navigation_reason")
        or stage.get("failure")
        or ""
    )
    unavailable = any(
        token in reason.lower()
        for token in (
            "unavailable",
            "diagnostic-only",
            "class-unavailable",
            "sequence already complete",
        )
    )
    code = ""
    if navigation_state == "failed" and navigation_failure not in {
        "", "none"
    }:
        code = {
            "attempt-limit-exceeded": "ui-input-attempt-limit-exceeded",
            "deadline-exceeded": "ui-input-deadline-exceeded",
            "invalid-route": "ui-input-route-invalid",
            "invalid-configuration": "ui-input-config-invalid",
        }.get(navigation_failure, "ui-input-navigation-failed")
    elif route_complete and (route_attempts > 0 or player_attempts > 0):
        code = "input-route-exhausted-without-active-scene"
    elif unavailable and (route_attempts > 0 or player_attempts > 0):
        code = "ui-input-dispatch-unavailable"
    elif (
        navigation_state in {"", "idle"}
        and nav_attempts >= 150
        and player_attempts >= 5
    ):
        code = "navigation-retry-exhausted"
    if not code:
        return None
    return {
        "code": code,
        "reason": reason,
        "current_scene": current.strip(),
        "active_scene_proven": False,
        "route_complete": route_complete,
        "route_attempts": route_attempts,
        "navigation_state": navigation_state,
        "navigation_failure": navigation_failure,
        "player_match_scene_request_attempts": player_attempts,
        "online_nav_attempts": nav_attempts,
        "last_dispatch_ok": bool(stage.get("main_menu_input_last_ok")),
        "navigation_state_source": stage.get("_navigation_state_source"),
        "navigation_state_ts_qpc": int_value(
            stage.get("_navigation_state_ts_qpc"), 0
        ),
    }


def navigation_diagnostics(
    online_stage: dict[str, Any] | None,
    ui_input_event: dict[str, Any] | None,
) -> dict[str, Any]:
    raw_stage = online_stage or {}
    stage = effective_navigation_state(raw_stage, ui_input_event)
    evidence = ui_navigation_evidence(ui_input_event)
    return {
        "active_scene": online_stage_current_scene_text(raw_stage).strip(),
        "queued_scene": " ".join(
            str(raw_stage.get(key, ""))
            for key in (
                "next_scene_class",
                "next_scene_name",
                "next_scene_direct_class",
                "next_scene_direct_name",
            )
        ).strip(),
        "queued_scene_counts_as_success": False,
        "automation_source": str(
            stage.get("main_menu_nav_automation_source")
            or stage.get("ui_gameflow_automation_source")
            or ""
        ),
        "input_sequence": str(stage.get("main_menu_input_sequence") or ""),
        "input_sequence_complete": bool(
            stage.get("main_menu_input_sequence_complete")
        ),
        "input_attempts": int_value(stage.get("main_menu_input_attempts"), 0),
        "navigation_state": str(
            stage.get("main_menu_navigation_state") or ""
        ),
        "navigation_failure": str(
            stage.get("main_menu_navigation_failure") or ""
        ),
        "navigation_state_source": str(
            stage.get("_navigation_state_source") or ""
        ),
        "navigation_state_ts_qpc": int_value(
            stage.get("_navigation_state_ts_qpc"), 0
        ),
        "navigation_generation": int_value(
            stage.get("main_menu_navigation_generation"), 0
        ),
        "navigation_dispatch_id": int_value(
            stage.get("main_menu_navigation_dispatch_id"), 0
        ),
        "navigation_pulse_generation": int_value(
            stage.get("main_menu_navigation_pulse_generation"), 0
        ),
        "navigation_lease_generation": int_value(
            stage.get("main_menu_navigation_lease_generation"), 0
        ),
        "xinput_phase": int_value(stage.get("main_menu_xinput_phase"), 0),
        "xinput_failure": int_value(
            stage.get("main_menu_xinput_failure"), 0
        ),
        "xinput_state_reads": int_value(
            stage.get("main_menu_xinput_state_reads"), 0
        ),
        "xinput_force_rescan_writes": int_value(
            stage.get("main_menu_xinput_force_rescan_writes"), 0
        ),
        "xinput_lease_generation": int_value(
            stage.get("main_menu_xinput_lease_generation"), 0
        ),
        "xinput_lease_state_reads": int_value(
            stage.get("main_menu_xinput_lease_state_reads"), 0
        ),
        "xinput_lease_forced_state_reads": int_value(
            stage.get("main_menu_xinput_lease_forced_state_reads"), 0
        ),
        "xinput_lease_active": bool(
            stage.get("main_menu_xinput_lease_active")
        ),
        "xinput_native_poller_verified": bool(
            stage.get("main_menu_xinput_native_poller_verified")
        ),
        "xinput_native_poller_hooked": bool(
            stage.get("main_menu_xinput_native_poller_hooked")
        ),
        "dispatcher": evidence["dispatcher"],
        "semantic_blueprint_supported": evidence[
            "semantic_blueprint_supported"
        ],
        "semantic_action_started": evidence["semantic_action_started"],
        "target_scene_queued": evidence["target_scene_queued"],
        "selection_changed": evidence["selection_changed"],
        "action_acknowledged": evidence["action_acknowledged"],
        "scene_transitioned": evidence["scene_transitioned"],
        "last_input_key": str(stage.get("main_menu_input_last_key") or ""),
        "last_input_ok": bool(stage.get("main_menu_input_last_ok")),
        "last_input_reason": str(
            stage.get("main_menu_input_last_reason") or ""
        ),
        "last_action": str(stage.get("main_menu_nav_last_action") or ""),
        "last_action_call_ok": bool(
            stage.get("main_menu_nav_last_action_call_ok")
            or stage.get("main_menu_input_last_ok")
        ),
        "last_action_accepted": bool(
            stage.get("main_menu_nav_last_action_accepted")
        ),
        "last_action_transitioned": bool(
            stage.get("main_menu_nav_last_action_transitioned")
        ),
        "input_handler_class": str(
            stage.get("main_menu_input_handler_class")
            or (ui_input_event or {}).get("handler_class")
            or ""
        ),
        "player_match_scene_request_attempts": int_value(
            stage.get("player_match_scene_request_attempts"), 0
        ),
        "online_nav_attempts": int_value(stage.get("online_nav_attempts"), 0),
        "ui_input_probe_event": ui_input_event,
        "terminal_failure": terminal_navigation_failure(
            raw_stage, ui_input_event
        ),
    }


def navigation_milestones(
    role: str,
    online_stage: dict[str, Any] | None,
    ui_input_event: dict[str, Any] | None,
    phase: str,
) -> dict[str, dict[str, Any]]:
    raw_stage = online_stage or {}
    stage = effective_navigation_state(raw_stage, ui_input_event)
    evidence = ui_navigation_evidence(ui_input_event)
    current = online_stage_current_scene_text(raw_stage)
    # A menu-ready transaction intentionally stops at MainMenuScene.  Treat
    # that phase's actual gate as the navigation milestone instead of
    # hard-coding the later Player Match scene set; otherwise a successful
    # menu-ready client is misleadingly reported as nav.reached=false.
    active_nav = (
        "MainMenuScene" in current
        if phase == "menu-ready"
        else any(scene in current for scene in PLAYER_MATCH_NAV_SCENES)
    )
    stage_input_attempts = int_value(stage.get("main_menu_input_attempts"), 0)
    input_probe_observed = bool(ui_input_event)
    event_dispatched = bool(
        ui_input_event
        and (
            ui_input_event.get("dispatch_accepted")
            or ui_input_event.get("call_success")
            or ui_input_event.get("native_input_consumed")
        )
    )
    input_dispatched = event_dispatched or stage_input_attempts > 0
    input_action_ack = bool(evidence["action_acknowledged"])
    semantic_action_started = bool(evidence["semantic_action_started"])
    target_scene_queued = bool(evidence["target_scene_queued"])
    host_room = bool(
        role == "host"
        and stage.get("host_create_request_ok")
        and stage.get("create_callback_seen")
        and stage.get("create_callback_result")
    )
    join = bool(
        role != "host"
        and (stage.get("client_join_request_ok") or stage.get("join_request_ok"))
        and stage.get("join_complete_seen")
        and stage.get("join_complete_result")
    )
    return {
        "ui-input-probe": {
            "applicable": True,
            "supported": bool(
                evidence["semantic_blueprint_supported"]
                or evidence["xinput_supported"]
                or stage.get("main_menu_xinput_native_poller_hooked")
            ),
            "dispatcher": evidence["dispatcher"],
            "semantic_blueprint_supported": evidence[
                "semantic_blueprint_supported"
            ],
            "xinput_supported": bool(
                evidence["xinput_supported"]
                or stage.get("main_menu_xinput_native_poller_hooked")
            ),
            "reached": input_dispatched,
            "probe_observed": input_probe_observed,
            "dispatch_observed": input_dispatched,
            "dispatch_call_ok": bool(
                (ui_input_event or {}).get("call_success")
                or stage.get("main_menu_input_last_ok")
            ),
            "evidence_source": (
                "rollback_main_menu_input_navigation"
                if ui_input_event else "rollback_online_stage"
                if stage_input_attempts > 0 else "unsupported"
            ),
            "attempts": stage_input_attempts,
            "action_acknowledged": input_action_ack,
            "semantic_action_started": semantic_action_started,
            "target_scene_queued": target_scene_queued,
            "selection_changed": evidence["selection_changed"],
            "scene_transitioned": evidence["scene_transitioned"],
            "status": (
                "scene-transitioned"
                if evidence["scene_transitioned"]
                else "target-scene-queued"
                if target_scene_queued
                else "action-acknowledged"
                if input_action_ack
                else "semantic-action-started"
                if semantic_action_started
                else "dispatch-observed-no-selection-ack" if input_dispatched
                else "not-observed"
            ),
        },
        "host-nav": {
            "applicable": role == "host",
            "reached": role == "host" and active_nav,
            "active_scene_only": True,
            "scene": current.strip(),
        },
        "host-room": {
            "applicable": role == "host",
            "reached": host_room,
            "create_request_ok": bool(stage.get("host_create_request_ok")),
            "create_callback_seen": bool(stage.get("create_callback_seen")),
            "create_callback_result": bool(stage.get("create_callback_result")),
        },
        "sandbox-nav": {
            "applicable": role != "host",
            "reached": role != "host" and active_nav,
            "active_scene_only": True,
            "scene": current.strip(),
        },
        "join": {
            "applicable": role != "host",
            "reached": join,
            "join_request_ok": bool(
                stage.get("client_join_request_ok")
                or stage.get("join_request_ok")
            ),
            "join_complete_seen": bool(stage.get("join_complete_seen")),
            "join_complete_result": bool(stage.get("join_complete_result")),
            "session_member_join_seen": bool(
                stage.get("session_member_join_seen")
            ),
        },
    }


def aggregate_navigation_milestones(
    results: list[dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    names = (
        "ui-input-probe",
        "host-nav",
        "host-room",
        "sandbox-nav",
        "join",
    )
    summary: dict[str, dict[str, Any]] = {}
    for name in names:
        by_role: dict[str, Any] = {}
        applicable: list[dict[str, Any]] = []
        for result in results:
            role = str(result.get("root", {}).get("role") or "unknown")
            milestone = dict((result.get("milestones") or {}).get(name) or {})
            by_role[role] = milestone
            if milestone.get("applicable"):
                applicable.append(milestone)
        summary[name] = {
            "applicable": bool(applicable),
            "reached": bool(applicable) and all(
                item.get("reached") for item in applicable
            ),
            "stable": bool(applicable) and all(
                item.get("stable", item.get("reached")) for item in applicable
            ),
            "by_role": by_role,
        }
    return summary


def apply_active_scene_stability(
    results: list[dict[str, Any]],
    phase: str,
    trackers: dict[str, dict[str, Any]],
    *,
    required: int = ACTIVE_SCENE_STABLE_OBSERVATIONS,
) -> None:
    if phase not in {
        "menu-ready",
        "player-match-nav",
        "player-match-lobby",
        "player-match-battle",
    }:
        return
    for result in results:
        key = str(result.get("root", {}).get("path") or result.get("request_id"))
        tracker = trackers.setdefault(
            key, {"context": "", "last_observation": 0, "count": 0}
        )
        stage = result.get("online_stage") or {}
        context = active_scene_context(stage, phase)
        gate_ok = bool(context) and online_stage_gate_ok(stage, phase)
        observation = int_value(stage.get("ts_qpc"), 0)
        if not observation and stage:
            observation = fnv1a64(json.dumps(stage, sort_keys=True, default=str))
        if not gate_ok:
            tracker.update(context="", last_observation=0, count=0)
        elif tracker.get("context") != context:
            tracker.update(
                context=context,
                last_observation=observation,
                count=1 if observation else 0,
            )
        elif observation and observation != tracker.get("last_observation"):
            tracker["last_observation"] = observation
            tracker["count"] = int_value(tracker.get("count"), 0) + 1
        count = int_value(tracker.get("count"), 0)
        stable = gate_ok and count >= required
        result["gate_stable_observations"] = count
        result["gate_stable_required"] = required
        result["gate_stable"] = stable
        for name in ("host-nav", "sandbox-nav"):
            milestone = (result.get("milestones") or {}).get(name)
            if milestone is not None and milestone.get("applicable"):
                milestone["stable"] = stable and bool(milestone.get("reached"))
                milestone["stable_observations"] = count
                milestone["stable_required"] = required
        if gate_ok and not stable:
            marker = f"stable_active_scene_observations={count}/{required}"
            if marker not in result["missing"]:
                result["missing"].append(marker)
            result["ok"] = False


def online_stage_gate_failures(
    online_stage: dict[str, Any] | None,
    expected: dict[str, Any],
    phase: str,
) -> list[str]:
    if online_stage is None:
        return ["rollback_online_stage"]
    failures: list[str] = []
    if str(online_stage.get("client_role", "")) != str(
        expected.get("client_role", "")
    ):
        failures.append("online_stage_client_role")
    if not bool(online_stage.get("online_stage_requested")):
        failures.append("online_stage_requested")
    if not bool(online_stage.get("game_thread")):
        failures.append("online_stage_game_thread")
    for gate in (
        "background_idle_override_value_read",
        "background_idle_override_command_ok",
    ):
        if not bool(online_stage.get(gate)):
            failures.append(f"online_stage_{gate}")
    expected_goal = str(expected.get("online_stage_goal", ""))
    if expected_goal and str(online_stage.get("online_stage_goal", "")) != expected_goal:
        failures.append("online_stage_goal")
    if not online_stage_gate_ok(online_stage, phase):
        failures.append(
            f"{phase}_active_scene="
            f"{online_stage_current_scene_text(online_stage) or 'missing'}"
        )
    if phase == "player-match-lobby":
        role = str(expected.get("client_role", ""))
        stock_join_route = str(expected.get("stock_join_route", "browser"))
        if str(online_stage.get("stock_join_route", "browser")) != stock_join_route:
            failures.append("online_stage_stock_join_route")
        if role == "host":
            if not (
                bool(online_stage.get("create_callback_result"))
                or bool(online_stage.get("host_adopted_existing_session"))
            ):
                failures.append("online_stage_host_membership_not_created")
            if not bool(online_stage.get("host_adopted_existing_session")):
                for gate in (
                    "host_room_create_make_room_ok",
                    "host_room_create_private_room_enabled",
                    "host_room_create_private_room_readback_ok",
                    "host_room_create_decide_ok",
                    "host_room_create_make_connecting_poll_ok",
                ):
                    if not bool(online_stage.get(gate)):
                        failures.append(f"online_stage_{gate}")
                for gate, expected_count in (
                    ("host_room_create_private_room_down_count", 9),
                    ("host_room_create_private_room_right_count", 1),
                    ("host_room_create_private_room_up_count", 9),
                ):
                    if int(online_stage.get(gate, -1)) != expected_count:
                        failures.append(f"online_stage_{gate}")
                if int(online_stage.get(
                    "host_room_create_private_room_readback_value", -1
                )) != 1:
                    failures.append(
                        "online_stage_host_room_create_private_room_readback_value"
                    )
            if stock_join_route == "invite-fallback" and not bool(
                online_stage.get("steam_invite_ok")
            ):
                failures.append("online_stage_authenticated_invite_not_sent")
            if stock_join_route == "invite-fallback" and not bool(
                online_stage.get("steam_private_lobby_ok")
            ):
                failures.append("online_stage_private_lobby_not_proven")
        elif role == "sandbox":
            if not bool(online_stage.get("membership_ready")):
                failures.append("online_stage_membership_ready")
            if not exact_host_result_selected(online_stage):
                failures.append("online_stage_exact_host_result_not_selected")
            if stock_join_route == "invite-fallback":
                for gate in (
                    "stock_offer_valid",
                    "steam_join_lobby_ok",
                    "stock_metadata_request_ok",
                    "stock_native_event_dispatched",
                    "stock_native_bridge_complete",
                ):
                    if not bool(online_stage.get(gate)):
                        failures.append(f"online_stage_{gate}")
                if int_value(online_stage.get("stock_offer_lobby_id"), 0) != int_value(
                    online_stage.get("native_named_session_lobby_id"), 0
                ):
                    failures.append("online_stage_invite_named_lobby_mismatch")
        else:
            failures.append(f"online_stage_unknown_role={role or 'missing'}")
    return failures


def direct_release_online_stage_failures(
    online_stage: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if online_stage is None:
        return ["rollback_online_stage"]
    failures: list[str] = []
    role = str(expected.get("client_role", ""))

    if not bool(online_stage.get("ok")):
        failures.append("online_stage_ok")

    if str(online_stage.get("client_role", "")) != role:
        failures.append("online_stage_client_role")
    expected_native = str(expected.get("online_stage_native_session_name", ""))
    if expected_native and str(
        online_stage.get("native_session_name", "")
    ) != expected_native:
        failures.append("online_stage_native_session_name")
    if str(online_stage.get("session_name", "")) != str(
        expected.get("online_stage_session_name", "")
    ):
        failures.append("online_stage_session_name")
    if str(online_stage.get("room_name", "")) != str(
        expected.get("online_stage_room_name", "")
    ):
        failures.append("online_stage_room_name")
    expected_target_owner_id = int_value(
        expected.get("online_stage_target_owner_id"), 0
    )
    if expected_target_owner_id and int_value(
        online_stage.get("target_owner_id"), 0
    ) != expected_target_owner_id:
        failures.append("online_stage_target_owner_id")
    expected_main_user_id_override = int_value(
        expected.get("online_stage_main_user_id_override"), -1
    )
    if int_value(online_stage.get("main_user_id_override"), -2) != (
        expected_main_user_id_override
    ):
        failures.append("online_stage_main_user_id_override")

    for gate in ("online_stage_requested", "game_thread"):
        if not bool(online_stage.get(gate)):
            failures.append(f"online_stage_{gate}")
    if bool(online_stage.get("online_stage_cleanup_only")):
        failures.append("online_stage_unexpected_cleanup_only")
    if bool(online_stage.get("online_stage_find_only")):
        failures.append("online_stage_unexpected_find_only")
    for gate in (
        "background_idle_override_value_read",
        "background_idle_override_command_ok",
    ):
        if not bool(online_stage.get(gate)):
            failures.append(f"online_stage_{gate}")
    if role not in {"host", "sandbox"}:
        failures.append(f"online_stage_unknown_role={role or 'missing'}")
    elif role == "host":
        for gate in ("host_create_requested", "host_create_request_ok"):
            if not bool(online_stage.get(gate)):
                failures.append(f"online_stage_{gate}")
    else:
        for gate in (
            "client_find_requested",
            "client_find_request_ok",
            "find_callback_seen",
            "find_callback_result",
            "client_join_request_ok",
        ):
            if not bool(online_stage.get(gate)):
                failures.append(f"online_stage_{gate}")
        if not exact_host_target_configured(expected):
            failures.append("online_stage_exact_host_target_not_configured")
        if not exact_host_result_selected(online_stage):
            failures.append("online_stage_exact_host_result_not_selected")
    return failures


def direct_stage_failures(
    direct_stage: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if direct_stage is None:
        return ["rollback_direct_stage"]
    failures: list[str] = []
    if not bool(direct_stage.get("ok")):
        failures.append("direct_stage_ok")
    if str(direct_stage.get("client_role", "")) != str(
        expected.get("client_role", "")
    ):
        failures.append("direct_stage_client_role")
    for gate in (
        "direct_stage_requested",
        "steam_stage_bypassed",
        "manual_setup_launch",
        "game_thread",
        "image_base_ready",
        "replay_file_exists",
        "replay_input_decoded",
        "setup_metadata_valid",
        "navigation_requested",
        "battle_rule_patch_attempted",
        "battle_rule_patch_ok",
        "battle_scene_requested",
        "battle_context_ready",
    ):
        if not bool(direct_stage.get(gate)):
            failures.append(f"direct_stage_{gate}")
    if int_value(direct_stage.get("battle_rule_ptr"), 0) == 0:
        failures.append("direct_stage_battle_rule_ptr")
    if int_value(direct_stage.get("battle_rule_type"), -1) != 0:
        failures.append(
            f"direct_stage_battle_rule_type={direct_stage.get('battle_rule_type')}"
        )
    battle_time = int_value(direct_stage.get("battle_rule_time"), -1)
    if battle_time <= 0 or battle_time == 0xFFFFFFFF:
        failures.append(
            f"direct_stage_battle_rule_time={direct_stage.get('battle_rule_time')}"
        )
    if not bool(direct_stage.get("battle_setup_endless_read_ok")):
        failures.append("direct_stage_battle_setup_endless_read_ok")
    if bool(direct_stage.get("battle_setup_endless")):
        failures.append("direct_stage_battle_setup_endless")
    if not bool(direct_stage.get("battle_setup_autostart_read_ok")):
        failures.append("direct_stage_battle_setup_autostart_read_ok")
    if not bool(direct_stage.get("battle_setup_autostart")):
        failures.append("direct_stage_battle_setup_autostart")
    if bool(direct_stage.get("quick_battle_requested")):
        failures.append("direct_stage_unexpected_quick_battle_request")
    manual_launch_ok = all(
        bool(direct_stage.get(gate))
        for gate in (
            "battle_setup_query_ok",
            "battle_setup_patched",
            "battle_asset_requested",
            "battle_asset_request_ok",
            "has_battle_request_query_ok",
            "can_launch_query_ok",
            "manual_launch_requested",
            "manual_launch_request_ok",
        )
    )
    launcher_launch_ok = bool(
        direct_stage.get("battle_launcher_start_requested")
    ) and bool(direct_stage.get("battle_launcher_start_ok"))
    spawn_launch_ok = bool(
        direct_stage.get("battle_manager_spawn_requested")
    ) and bool(direct_stage.get("battle_manager_spawn_ok"))
    gameflow_launch_ok = all(
        bool(direct_stage.get(gate))
        for gate in (
            "battle_setup_query_ok",
            "battle_setup_patched",
            "battle_scene_requested",
            "battle_scene_request_ok",
            "battle_scene_ready",
        )
    )
    launch_path_ok = gameflow_launch_ok or launcher_launch_ok or spawn_launch_ok
    navigation_ready = bool(direct_stage.get("navigation_ready"))
    battle_scene_ready = bool(direct_stage.get("battle_scene_ready"))
    if not (navigation_ready or launcher_launch_ok or spawn_launch_ok):
        failures.append("direct_stage_navigation_or_launcher_ready")
        if not navigation_ready:
            failures.append("direct_stage_navigation_ready")
        if bool(direct_stage.get("battle_launcher_start_requested")) and not bool(
            direct_stage.get("battle_launcher_start_ok")
        ):
                failures.append("direct_stage_battle_launcher_start_ok")
    if not launch_path_ok:
        failures.append("direct_stage_launch_path")
        for gate in (
            "battle_setup_query_ok",
            "battle_setup_patched",
            "battle_scene_requested",
            "battle_scene_request_ok",
            "battle_scene_ready",
        ):
            if not bool(direct_stage.get(gate)):
                failures.append(f"direct_stage_{gate}")
        if not battle_scene_ready:
            failures.append("direct_stage_battle_scene_ready")
    for key in ("game_instance",):
        if int_value(direct_stage.get(key), 0) == 0:
            failures.append(f"direct_stage_{key}")
    if (manual_launch_ok or gameflow_launch_ok) and int_value(
        direct_stage.get("battle_setup"), 0
    ) == 0:
        failures.append("direct_stage_battle_setup")
    for key in ("setup_stage", "setup_stage_map", "setup_left_chara", "setup_right_chara"):
        if int_value(direct_stage.get(key), -1) < 0:
            failures.append(f"direct_stage_{key}")
    for expected_key, observed_key in (
        ("launch_left_character", "setup_left_chara"),
        ("launch_right_character", "setup_right_chara"),
        ("launch_stage", "setup_stage"),
    ):
        expected_value = int_value(expected.get(expected_key), -1)
        if expected_value >= 0 and int_value(
            direct_stage.get(observed_key), -2
        ) != expected_value:
            failures.append(f"direct_stage_{expected_key}_override")
    for key in ("chara_p1", "chara_p2"):
        if int_value(direct_stage.get(key), 0) == 0:
            failures.append(f"direct_stage_{key}")
    if bool(direct_stage.get("battle_manager_spawn_ok")) and bool(
        direct_stage.get("debug_direct_stage_begin_play")
    ):
        for key in (
            "battle_manager_begin_play_requested",
            "battle_manager_begin_play_ok",
        ):
            if key in direct_stage and not bool(direct_stage.get(key)):
                failures.append(f"direct_stage_{key}")
    for key in (
        "chara_p1_read_ok",
        "chara_p2_read_ok",
        "battle_manager_object_real",
    ):
        if key in direct_stage and not bool(direct_stage.get(key)):
            failures.append(f"direct_stage_{key}")
    for player in (1, 2):
        context_live = (
            bool(direct_stage.get(f"chara_p{player}_context_live"))
            or bool(direct_stage.get(f"chara_p{player}_object_real"))
            or bool(direct_stage.get(f"chara_p{player}_native_live"))
            or bool(direct_stage.get(f"chara_p{player}_static_live"))
        )
        if not context_live:
            failures.append(f"direct_stage_chara_p{player}_context_live")
    context_failure = str(direct_stage.get("battle_context_failure", ""))
    if context_failure and context_failure != "ok":
        failures.append(f"direct_stage_context={context_failure}")
    if bool(direct_stage.get("has_battle_request_pending")) and not bool(
        direct_stage.get("battle_asset_requested")
    ):
        failures.append("direct_stage_has_battle_request_pending")
    if bool(direct_stage.get("stage_map_requested")) and not bool(
        direct_stage.get("stage_map_request_ok")
    ):
        failures.append("direct_stage_stage_map_request_ok")
    failure_reason = str(direct_stage.get("failure", ""))
    if failure_reason and failure_reason != "ok":
        failures.append(f"direct_stage_failure={failure_reason}")
    return failures


def direct_release_stage_failures(
    direct_stage: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if direct_stage is None:
        return ["rollback_direct_stage"]
    failures: list[str] = []
    if not bool(direct_stage.get("ok")):
        failures.append("direct_stage_ok")
    if str(direct_stage.get("client_role", "")) != str(
        expected.get("client_role", "")
    ):
        failures.append("direct_stage_client_role")
    observe_only = bool(expected.get("direct_stage_observe_only"))
    required_gates = [
        "direct_stage_requested",
        "game_thread",
        "image_base_ready",
        "replay_file_exists",
        "replay_input_decoded",
        "setup_metadata_valid",
        "navigation_requested",
        "navigation_ready",
        "battle_scene_requested",
        "battle_scene_request_ok",
        "battle_scene_ready",
        "battle_context_ready",
    ]
    if observe_only:
        required_gates.append("direct_stage_observe_only")
    else:
        required_gates.append("online_stage_requested")
    for gate in required_gates:
        if not bool(direct_stage.get(gate)):
            failures.append(f"direct_stage_{gate}")

    if bool(direct_stage.get("steam_stage_bypassed")):
        failures.append("direct_release_unexpected_steam_stage_bypassed")
    if bool(direct_stage.get("manual_setup_launch")):
        failures.append("direct_release_unexpected_manual_setup_launch")

    scene_text = " ".join(
        str(direct_stage.get(key, ""))
        for key in ("current_scene_class", "current_scene_name")
    )
    if not is_online_player_match_battle_scene_text(scene_text):
        failures.append(f"direct_release_current_scene={scene_text or 'missing'}")
    for bad in (
        "PlayerMatchLobbyScene",
        "MainMenu",
        "Training",
        "Replay",
        "BattleSetup",
    ):
        if bad in scene_text:
            failures.append(f"direct_release_unexpected_scene={scene_text}")

    if int_value(direct_stage.get("battle_rule_ptr"), 0) == 0:
        failures.append("direct_release_battle_rule_ptr")
    if not bool(direct_stage.get("battle_rule_read_ok")):
        failures.append("direct_release_battle_rule_read_ok")
    if not bool(direct_stage.get("battle_rule_finite")):
        failures.append("direct_release_battle_rule_finite")
    battle_rule_type = int_value(direct_stage.get("battle_rule_type"), -1)
    if (battle_rule_type < 0) or (
        not observe_only and battle_rule_type != 0
    ):
        failures.append(
            f"direct_release_battle_rule_type={direct_stage.get('battle_rule_type')}"
        )
    battle_time = int_value(direct_stage.get("battle_rule_time"), -1)
    if battle_time <= 0 or battle_time == 0xFFFFFFFF:
        failures.append(
            f"direct_release_battle_rule_time={direct_stage.get('battle_rule_time')}"
        )
    if not observe_only:
        if not bool(direct_stage.get("battle_setup_endless_read_ok")):
            failures.append("direct_release_battle_setup_endless_read_ok")
        if bool(direct_stage.get("battle_setup_endless")):
            failures.append("direct_release_battle_setup_endless")
        if not bool(direct_stage.get("battle_setup_autostart_read_ok")):
            failures.append("direct_release_battle_setup_autostart_read_ok")
        if not bool(direct_stage.get("battle_setup_autostart")):
            failures.append("direct_release_battle_setup_autostart")

    for key in ("game_instance", "active_battle_manager", "chara_p1", "chara_p2"):
        if int_value(direct_stage.get(key), 0) == 0:
            failures.append(f"direct_stage_{key}")
    for key in (
        "chara_p1_read_ok",
        "chara_p2_read_ok",
        "battle_manager_object_real",
    ):
        if key in direct_stage and not bool(direct_stage.get(key)):
            failures.append(f"direct_stage_{key}")
    for player in (1, 2):
        context_live = (
            bool(direct_stage.get(f"chara_p{player}_context_live"))
            or bool(direct_stage.get(f"chara_p{player}_object_real"))
            or bool(direct_stage.get(f"chara_p{player}_native_live"))
            or bool(direct_stage.get(f"chara_p{player}_static_live"))
        )
        if not context_live:
            failures.append(f"direct_stage_chara_p{player}_context_live")

    for key in ("setup_stage", "setup_stage_map", "setup_left_chara", "setup_right_chara"):
        if int_value(direct_stage.get(key), -1) < 0:
            failures.append(f"direct_stage_{key}")
    context_failure = str(direct_stage.get("battle_context_failure", ""))
    if context_failure and context_failure != "ok":
        failures.append(f"direct_stage_context={context_failure}")
    failure_reason = str(direct_stage.get("failure", ""))
    if failure_reason and failure_reason != "ok":
        failures.append(f"direct_stage_failure={failure_reason}")
    return failures


def direct_stage_mode_failures(
    direct_stage: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if bool(expected.get("direct_stage_observe_only")):
        return direct_release_stage_failures(direct_stage, expected)
    return direct_stage_failures(direct_stage, expected)


def direct_connect_failures(
    direct_connect: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if direct_connect is None:
        return ["rollback_direct_connect"]
    failures: list[str] = []
    if not bool(direct_connect.get("ok")):
        failures.append("direct_connect_ok")
    if str(direct_connect.get("client_role", "")) != str(
        expected.get("client_role", "")
    ):
        failures.append("direct_connect_client_role")
    for gate in (
        "steam_stage_bypassed",
        "sidecar_ready",
        "udp_connreset_disabled",
        "direct_input_enabled",
        "sent_direct_input",
        "received_direct_input",
        "validated_direct_input",
        "direct_payload_hash_valid",
    ):
        if not bool(direct_connect.get(gate)):
            failures.append(f"direct_connect_{gate}")
    for key in ("local_peer_id", "remote_peer_id"):
        if int_value(direct_connect.get(key), -1) != int_value(
            expected.get(key), -2
        ):
            failures.append(f"direct_connect_{key}")
    for key in ("sidecar_local_port", "sidecar_remote_port"):
        if int_value(direct_connect.get(key), -1) != int_value(
            expected.get(key), -2
        ):
            failures.append(f"direct_connect_{key}")
    for key in ("local_replay_player", "remote_replay_player"):
        if int_value(direct_connect.get(key), -1) != int_value(
            expected.get(key), -2
        ):
            failures.append(f"direct_connect_{key}")
    if int_value(direct_connect.get("remote_frame_count"), 0) <= 0:
        failures.append("direct_connect_remote_frame_count")
    if int_value(direct_connect.get("remote_input_hash"), 0) == 0:
        failures.append("direct_connect_remote_input_hash")
    if int_value(direct_connect.get("remote_input_hash"), 0) != int_value(
        direct_connect.get("expected_remote_input_hash"), -1
    ):
        failures.append("direct_connect_expected_remote_input_hash")
    for key in (
        "wrong_endpoint_rejected",
        "wrong_route_rejected",
        "wrong_token_rejected",
        "wrong_packet_type_rejected",
        "wrong_direct_sequence_rejected",
        "wrong_direct_payload_rejected",
    ):
        if bool(direct_connect.get(key)):
            failures.append(f"direct_connect_{key}")
    for key in (
        "sendto_error",
        "recvfrom_error",
        "udp_connreset_error",
        "direct_packets_rejected",
    ):
        if int_value(direct_connect.get(key), 0) != 0:
            failures.append(f"direct_connect_{key}")
    failure_reason = str(direct_connect.get("failure", ""))
    if failure_reason and failure_reason != "ok":
        failures.append(f"direct_connect_failure={failure_reason}")
    return failures


def direct_replay_input_failures(
    direct_replay_input: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if direct_replay_input is None:
        return ["rollback_direct_replay_input"]
    failures: list[str] = []
    expected_file = Path(str(expected.get("replay_input_file", "")))
    if not bool(direct_replay_input.get("ok")):
        failures.append("direct_replay_input_ok")
    if str(direct_replay_input.get("client_role", "")) != str(
        expected.get("client_role", "")
    ):
        failures.append("direct_replay_input_client_role")
    if normalized_path_text(direct_replay_input.get("replay_input_file")) != (
        normalized_path_text(expected_file)
    ):
        failures.append("direct_replay_input_file")
    expected_hash = file_fnv1a64(expected_file)
    if expected_hash == 0:
        failures.append("direct_replay_input_expected_file_hash")
    elif int_value(direct_replay_input.get("replay_file_hash"), 0) != expected_hash:
        failures.append("direct_replay_input_file_hash")
    for key in ("local_replay_player", "remote_replay_player"):
        if int_value(direct_replay_input.get(key), -1) != int_value(
            expected.get(key), -2
        ):
            failures.append(f"direct_replay_input_{key}")
    for gate in (
        "steam_stage_bypassed",
        "input_latched",
        "input_injected",
        "input_complete",
    ):
        if not bool(direct_replay_input.get(gate)):
            failures.append(f"direct_replay_input_{gate}")
    for key in (
        "local_input_hash",
        "expected_remote_input_hash",
        "frame_count",
    ):
        if int_value(direct_replay_input.get(key), 0) <= 0:
            failures.append(f"direct_replay_input_{key}")
    if str(expected.get("mode", "")) == "direct-connect":
        if not bool(direct_replay_input.get("direct_cache_write_attempted")):
            failures.append("direct_replay_input_direct_cache_write_attempted")
        if not bool(direct_replay_input.get("direct_cache_write_complete")):
            failures.append("direct_replay_input_direct_cache_write_complete")
        for key in (
            "direct_cache_writes_local",
            "direct_cache_writes_remote",
            "direct_battle_manager",
            "direct_input_log",
            "direct_cache_refreshes",
        ):
            if int_value(direct_replay_input.get(key), 0) <= 0:
                failures.append(f"direct_replay_input_{key}")
    else:
        for key in ("send_cache_writes", "consumer_writes"):
            if int_value(direct_replay_input.get(key), 0) <= 0:
                failures.append(f"direct_replay_input_{key}")
    failure_reason = str(direct_replay_input.get("failure", ""))
    if failure_reason and failure_reason != "ok":
        failures.append(f"direct_replay_input_failure={failure_reason}")
    return failures


def replay_input_failures(
    replay_input: dict[str, Any] | None,
    expected: dict[str, Any],
) -> list[str]:
    if replay_input is None:
        return ["rollback_replay_input_script"]
    failures: list[str] = []
    expected_file = Path(str(expected.get("replay_input_file", "")))

    if not bool(replay_input.get("ok")):
        failures.append("replay_input_ok")
    if str(replay_input.get("client_role", "")) != str(
        expected.get("client_role", "")
    ):
        failures.append("replay_input_client_role")
    if not bool(replay_input.get("live_replay_input_requested")):
        failures.append("replay_input_requested")
    if normalized_path_text(replay_input.get("replay_input_file")) != (
        normalized_path_text(expected_file)
    ):
        failures.append("replay_input_file")

    expected_hash = file_fnv1a64(expected_file)
    if expected_hash == 0:
        failures.append("replay_input_expected_file_hash")
    elif int_value(replay_input.get("replay_file_hash"), 0) != expected_hash:
        failures.append("replay_input_file_hash")

    for gate in (
        "replay_file_exists",
        "player_mapping_valid",
        "inputs_decoded",
        "input_latched",
        "input_injected",
        "input_complete",
    ):
        if not bool(replay_input.get(gate)):
            failures.append(f"replay_input_{gate}")
    for key in ("replay_file_bytes", "input_frames_p0", "input_frames_p1"):
        if int_value(replay_input.get(key), 0) <= 0:
            failures.append(f"replay_input_{key}")
    for key in ("local_replay_player", "remote_replay_player"):
        if int_value(replay_input.get(key), -1) != int_value(
            expected.get(key), -2
        ):
            failures.append(f"replay_input_{key}")
    for key in ("replay_divergence_frame", "replay_divergence_window"):
        event_key = key.replace("replay_", "")
        if int_value(replay_input.get(event_key), -1) != int_value(
            expected.get(key), -2
        ):
            failures.append(f"replay_input_{key}")

    failure_reason = str(replay_input.get("failure", ""))
    if failure_reason and failure_reason != "ok":
        failures.append(f"replay_input_failure={failure_reason}")
    return failures


def live_correction_failures(
    cache_write: dict[str, Any] | None,
    correction: dict[str, Any] | None,
    convergence: dict[str, Any] | None,
    disarm: dict[str, Any] | None,
) -> list[str]:
    failures: list[str] = []
    if disarm is not None:
        failures.append(f"live_disarmed:{disarm.get('reason', 'unknown')}")
    if cache_write is None:
        failures.append("rollback_live_cache_write")
    else:
        for gate in (
            "ok",
            "game_thread",
            "stock_drain_before_write",
            "consumer_after_write",
            "prediction_written",
            "prediction_diverged",
        ):
            if not bool(cache_write.get(gate)):
                failures.append(f"live_cache_write_{gate}")
        if bool(cache_write.get("network_thread")):
            failures.append("live_cache_write_not_network_thread")
    if correction is None:
        failures.append("rollback_live_correction")
    else:
        for gate in (
            "ok",
            "confirmed_input_applied",
            "correction_scheduled",
            "nonzero_correction_depth",
            "snapshot_restore",
            "hidden_resim",
            "corrected_matches_baseline",
            "predicted_differs_from_baseline",
        ):
            if not bool(correction.get(gate)):
                failures.append(f"live_correction_{gate}")
    if convergence is None:
        failures.append("rollback_live_convergence")
    else:
        for gate in ("ok", "converged"):
            if not bool(convergence.get(gate)):
                failures.append(f"live_convergence_{gate}")
        if int_value(convergence.get("local_corrected_hash"), 0) == 0:
            failures.append("live_convergence_local_corrected_hash")
    return failures


def direct_correction_failures(
    correction: dict[str, Any] | None,
    disarm: dict[str, Any] | None,
) -> list[str]:
    failures: list[str] = []
    correction_ready = bool(
        correction
        and correction.get("ok")
        and correction.get("sidecar_ready")
        and correction.get("direct_input_ready")
    )
    if disarm is not None and not correction_ready:
        failures.append(f"direct_disarmed:{disarm.get('reason', 'unknown')}")
    if correction is None:
        return failures + ["rollback_direct_correction"]
    if not bool(correction.get("ok")):
        failures.append("direct_correction_ok")
    for gate in (
        "steam_stage_bypassed",
        "game_thread",
        "sidecar_ready",
        "direct_input_ready",
        "confirmed_input_applied",
        "correction_scheduled",
        "nonzero_correction_depth",
        "snapshot_restore",
        "hidden_resim",
        "corrected_matches_baseline",
        "predicted_differs_from_baseline",
        "explicit_match",
        "hgcpu_policy_match",
        "frame_counter_match",
        "frame_counter_delta_ok",
        "all_steps_ok",
        "post_baseline_restore_explicit_ok",
        "post_baseline_restore_explicit_match",
        "post_predicted_restore_explicit_ok",
        "post_predicted_restore_explicit_match",
    ):
        if not bool(correction.get(gate)):
            failures.append(f"direct_correction_{gate}")
    for key in ("correction_depth", "steps_ok", "steps_attempted"):
        if int_value(correction.get(key), 0) <= 0:
            failures.append(f"direct_correction_{key}")
    for key in (
        "baseline_hash",
        "predicted_hash",
        "corrected_hash",
        "baseline_explicit_hash",
        "predicted_explicit_hash",
        "corrected_explicit_hash",
        "post_baseline_restore_explicit_hash",
        "post_predicted_restore_explicit_hash",
    ):
        if int_value(correction.get(key), 0) == 0:
            failures.append(f"direct_correction_{key}")
    for key in (
        "start_frame",
        "baseline_frame",
        "predicted_frame",
        "corrected_frame",
    ):
        if int_value(correction.get(key), -1) < 0:
            failures.append(f"direct_correction_{key}")
    if int_value(correction.get("baseline_frame"), -1) != int_value(
        correction.get("corrected_frame"), -2
    ):
        failures.append("direct_correction_baseline_corrected_frame")
    if int_value(correction.get("baseline_explicit_hash"), 0) != int_value(
        correction.get("corrected_explicit_hash"), -1
    ):
        failures.append("direct_correction_baseline_corrected_explicit_hash")
    if int_value(correction.get("hgcpu_unignored_mismatch_count"), 0) != 0:
        failures.append("direct_correction_hgcpu_unignored_mismatch_count")
    failure_reason = str(correction.get("failure", ""))
    if failure_reason and failure_reason != "ok":
        failures.append(f"direct_correction_failure={failure_reason}")
    return failures


def live_correction_pair_failures(results: list[dict[str, Any]]) -> list[str]:
    failures: list[str] = []
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host = by_role.get("host")
    sandbox = by_role.get("sandbox")
    if host is None or sandbox is None:
        return ["pair_convergence_expected_host_and_sandbox"]

    hashes: dict[str, int] = {}
    for role, result in (("host", host), ("sandbox", sandbox)):
        convergence = result.get("live_convergence") or {}
        local_hash = int_value(convergence.get("local_corrected_hash"), 0)
        if local_hash == 0:
            failures.append(f"pair_convergence_{role}_missing_hash")
        hashes[role] = local_hash
    if hashes.get("host", 0) and hashes.get("sandbox", 0):
        if hashes["host"] != hashes["sandbox"]:
            failures.append(
                "pair_convergence_hash_mismatch "
                f"host=0x{hashes['host']:X} sandbox=0x{hashes['sandbox']:X}"
            )
    return failures


def direct_correction_pair_failures(results: list[dict[str, Any]]) -> list[str]:
    failures: list[str] = []
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host = by_role.get("host")
    sandbox = by_role.get("sandbox")
    if host is None or sandbox is None:
        return ["pair_direct_correction_expected_host_and_sandbox"]

    local_hashes: dict[str, int] = {}
    remote_hashes: dict[str, int] = {}
    for role, result in (("host", host), ("sandbox", sandbox)):
        correction = result.get("direct_correction") or {}
        if not bool(correction.get("ok")):
            failures.append(f"pair_direct_correction_{role}_ok")
        for gate in (
            "corrected_matches_baseline",
            "predicted_differs_from_baseline",
            "explicit_match",
            "hgcpu_policy_match",
            "frame_counter_match",
            "frame_counter_delta_ok",
            "all_steps_ok",
        ):
            if not bool(correction.get(gate)):
                failures.append(f"pair_direct_correction_{role}_{gate}")
        local_hashes[role] = int_value(correction.get("local_input_hash"), 0)
        remote_hashes[role] = int_value(correction.get("remote_input_hash"), 0)
        if local_hashes[role] == 0:
            failures.append(f"pair_direct_correction_{role}_local_input_hash")
        if remote_hashes[role] == 0:
            failures.append(f"pair_direct_correction_{role}_remote_input_hash")
    if all(local_hashes.get(role, 0) for role in ("host", "sandbox")) and all(
        remote_hashes.get(role, 0) for role in ("host", "sandbox")
    ):
        if local_hashes["host"] != remote_hashes["sandbox"]:
            failures.append(
                "pair_direct_correction_host_local_not_sandbox_remote "
                f"host=0x{local_hashes['host']:X} "
                f"sandbox=0x{remote_hashes['sandbox']:X}"
            )
        if local_hashes["sandbox"] != remote_hashes["host"]:
            failures.append(
                "pair_direct_correction_sandbox_local_not_host_remote "
                f"sandbox=0x{local_hashes['sandbox']:X} "
                f"host=0x{remote_hashes['host']:X}"
            )
    return failures


def online_stage_membership_pair_failures(
    results: list[dict[str, Any]],
) -> list[str]:
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host = (by_role.get("host") or {}).get("online_stage") or {}
    sandbox = (by_role.get("sandbox") or {}).get("online_stage") or {}
    if not host or not sandbox:
        return ["pair_online_stage_membership_roles_missing"]
    host_lobby = int_value(host.get("native_named_session_lobby_id"), 0)
    sandbox_lobby = int_value(
        sandbox.get("native_named_session_lobby_id"), 0
    )
    failures: list[str] = []
    if host_lobby == 0:
        failures.append("pair_online_stage_host_lobby_missing")
    if sandbox_lobby == 0:
        failures.append("pair_online_stage_sandbox_lobby_missing")
    if host_lobby and sandbox_lobby and host_lobby != sandbox_lobby:
        failures.append(
            "pair_online_stage_lobby_mismatch="
            f"0x{host_lobby:X}/0x{sandbox_lobby:X}"
        )
    return failures


def mirrored_versus_failures(
    status: dict[str, Any] | None,
    launch: dict[str, Any] | None,
    expected: dict[str, Any],
    phase: str,
) -> list[str]:
    if status is None:
        return ["rollback_production_status"]
    failures: list[str] = []
    role = str(expected.get("client_role", "unknown"))
    if str(status.get("lifecycle_mode", "")) != "mirrored-versus":
        failures.append("production_lifecycle_mode")
    if str(status.get("network_profile", "")) != str(
        expected.get("network_profile", "")
    ):
        failures.append("production_network_profile")
    if int_value(status.get("fault_seed"), 0) != int_value(
        expected.get("fault_seed"), -1
    ):
        failures.append("production_fault_seed")
    if int_value(status.get("gekko_slot"), -1) != int_value(
        expected.get("local_player_slot"), -2
    ):
        failures.append("production_gekko_slot")
    if int_value(status.get("native_input_source_slot"), -1) != int_value(
        expected.get("native_input_source_slot"), -2
    ):
        failures.append("production_native_input_source_slot")
    for gate in ("executable_match", "schema_match", "peer_ready"):
        if not bool(status.get(gate)):
            failures.append(f"production_{gate}")
    if int_value(status.get("fault_submitted"), 0) <= 0:
        failures.append("production_fault_submitted")
    if int_value(status.get("fault_delivered"), 0) <= 0:
        failures.append("production_fault_delivered")
    if str(expected.get("network_profile", "clean_0ms")) != "clean_0ms":
        if int_value(status.get("fault_queued"), 0) <= 0:
            failures.append("production_fault_queued")
    if int_value(status.get("state"), -1) == 6:
        failures.append(
            "production_fatal=" + str(status.get("failure", "unknown"))
        )
    if phase == "horse-udp-ready":
        return failures

    if launch is None:
        failures.append("rollback_mirrored_versus_launch")
        return failures
    if str(launch.get("state", "")) == "failed":
        failures.append(
            f"mirrored_launch_failed={launch.get('failure', 'unknown')}"
        )
    for expected_key, launch_key in (
        ("launch_left_character", "requested_left_character"),
        ("launch_right_character", "requested_right_character"),
        ("launch_stage", "requested_stage"),
    ):
        expected_value = int_value(expected.get(expected_key), -1)
        if expected_value >= 0 and int_value(
            launch.get(launch_key), -2
        ) != expected_value:
            failures.append(f"mirrored_{expected_key}_override")
    desired = int_value(status.get("desired_descriptor_hash"), 0)
    observed = int_value(status.get("observed_descriptor_hash"), 0)
    peer = int_value(status.get("peer_descriptor_hash"), 0)
    if desired == 0:
        failures.append("production_desired_descriptor_hash")
    if observed == 0 or observed != desired:
        failures.append("production_observed_descriptor_hash")
    if peer == 0 or peer != desired:
        failures.append("production_peer_descriptor_hash")
    for gate in ("setup_barrier_local", "setup_barrier_peer"):
        if not bool(status.get(gate)):
            failures.append(f"production_{gate}")
    if phase == "mirrored-versus-setup":
        return failures

    for gate in (
        "baseline_barrier_local",
        "baseline_barrier_peer",
        "tick_hook_installed",
        "presentation_hooks_installed",
    ):
        if not bool(status.get(gate)):
            failures.append(f"production_{gate}")
    baseline_frame = int_value(status.get("baseline_frame"), -1)
    baseline_epoch = int_value(status.get("baseline_epoch"), 0)
    peer_epoch = int_value(status.get("peer_baseline_epoch"), 0)
    baseline_hash = int_value(status.get("baseline_hash"), 0)
    peer_hash = int_value(status.get("peer_baseline_hash"), 0)
    if baseline_frame < 0:
        failures.append("production_baseline_frame")
    if baseline_epoch == 0 or baseline_epoch != peer_epoch:
        failures.append("production_baseline_epoch")
    if baseline_hash == 0 or baseline_hash != peer_hash:
        failures.append("production_baseline_hash")
    if phase == "mirrored-versus-battle":
        return failures

    if int_value(status.get("state"), -1) != ROLLBACK_PRODUCTION_ACTIVE_STATE:
        failures.append(
            "production_not_active=" + str(status.get("failure", "unknown"))
        )
    for gate in (
        "manifest_ready",
        "lifecycle_ready",
        "baseline_restore_verified",
        "prediction_restore_verified",
        "final_restore_verified",
        "presentation_exactly_once",
    ):
        if not bool(status.get(gate)):
            failures.append(f"production_{gate}")
    for counter in (
        "saves",
        "loads",
        "advances",
        "rollback_advances",
        "pair_accepts",
        "local_input_count",
        "remote_input_count",
    ):
        if int_value(status.get(counter), 0) <= 0:
            failures.append(f"production_{counter}")
    for hash_field in (
        "local_input_hash",
        "remote_input_hash",
        "confirmed_canonical_hash",
        "last_restore_expected_hash",
        "last_restore_observed_hash",
    ):
        if int_value(status.get(hash_field), 0) == 0:
            failures.append(f"production_{hash_field}")
    if int_value(status.get("last_restore_expected_hash"), 0) != int_value(
        status.get("last_restore_observed_hash"), -1
    ):
        failures.append("production_final_restore_hash_mismatch")
    corrected = int_value(status.get("corrected_frame"), -1)
    confirmed = int_value(status.get("confirmed_frame"), -1)
    if corrected < 0 or confirmed < 0 or corrected != confirmed:
        failures.append(
            f"production_corrected_confirmed_frame:{role}:"
            f"{corrected}:{confirmed}"
        )
    profile = str(expected.get("network_profile", "clean_0ms"))
    profile_counters = {
        "wifi_50ms_jitter": ("fault_reordered",),
        "bad_wifi_120ms_5pct_loss": ("fault_dropped", "fault_reordered"),
        "overseas_180ms_2pct_loss": ("fault_dropped", "fault_reordered"),
        "spike_every_10s": ("fault_spiked",),
        "burst_loss_500ms": ("fault_burst_dropped",),
        "corrupt_probe": ("fault_corrupted",),
    }
    for counter in profile_counters.get(profile, ()):
        if int_value(status.get(counter), 0) <= 0:
            failures.append(f"production_profile_effect:{profile}:{counter}")
    return failures


def mirrored_versus_pair_failures(
    results: list[dict[str, Any]], phase: str
) -> list[str]:
    failures: list[str] = []
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host = by_role.get("host")
    sandbox = by_role.get("sandbox")
    if host is None or sandbox is None:
        return ["pair_mirrored_expected_host_and_sandbox"]
    hs = host.get("production_status") or {}
    ss = sandbox.get("production_status") or {}
    if int_value(hs.get("gekko_slot"), -1) != 0:
        failures.append("pair_mirrored_host_gekko_slot")
    if int_value(ss.get("gekko_slot"), -1) != 1:
        failures.append("pair_mirrored_sandbox_gekko_slot")
    if int_value(hs.get("native_input_source_slot"), -1) != int_value(
        ss.get("native_input_source_slot"), -2
    ):
        failures.append("pair_mirrored_native_input_source_mismatch")

    desired_host = int_value(hs.get("desired_descriptor_hash"), 0)
    desired_sandbox = int_value(ss.get("desired_descriptor_hash"), 0)
    if desired_host == 0 or desired_host != desired_sandbox:
        failures.append("pair_mirrored_descriptor_mismatch")
    if phase == "horse-udp-ready":
        return failures

    observed_host = int_value(hs.get("observed_descriptor_hash"), 0)
    observed_sandbox = int_value(ss.get("observed_descriptor_hash"), 0)
    if observed_host == 0 or observed_host != observed_sandbox:
        failures.append("pair_mirrored_observed_descriptor_mismatch")
    if phase == "mirrored-versus-setup":
        return failures

    for field in ("baseline_frame", "baseline_epoch", "baseline_hash"):
        host_value = int_value(hs.get(field), -1)
        sandbox_value = int_value(ss.get(field), -2)
        if host_value <= 0 and field != "baseline_frame":
            failures.append(f"pair_mirrored_{field}_missing")
        if host_value != sandbox_value:
            failures.append(f"pair_mirrored_{field}_mismatch")
    if phase == "mirrored-versus-battle":
        return failures

    if int_value(hs.get("local_input_hash"), 0) != int_value(
        ss.get("remote_input_hash"), -1
    ):
        failures.append("pair_mirrored_host_local_not_sandbox_remote")
    if int_value(ss.get("local_input_hash"), 0) != int_value(
        hs.get("remote_input_hash"), -1
    ):
        failures.append("pair_mirrored_sandbox_local_not_host_remote")
    for field in ("confirmed_canonical_hash", "corrected_frame", "confirmed_frame"):
        if int_value(hs.get(field), -1) != int_value(ss.get(field), -2):
            failures.append(f"pair_mirrored_{field}_mismatch")
    return failures


def direct_selection_pair_failures(
    results: list[dict[str, Any]],
) -> list[str]:
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host = by_role.get("host")
    sandbox = by_role.get("sandbox")
    if host is None or sandbox is None:
        return ["pair_direct_selection_expected_host_and_sandbox"]
    failures: list[str] = []
    host_stage = host.get("direct_stage") or {}
    sandbox_stage = sandbox.get("direct_stage") or {}
    for field in (
        "setup_stage", "setup_stage_map", "setup_left_chara",
        "setup_right_chara",
    ):
        host_value = int_value(host_stage.get(field), -1)
        sandbox_value = int_value(sandbox_stage.get(field), -2)
        if host_value < 0 or host_value != sandbox_value:
            failures.append(f"pair_direct_selection_{field}_mismatch")
    for role, result in (("host", host), ("sandbox", sandbox)):
        proof = result.get("direct_setup_patch") or {}
        if not proof:
            continue
        for field in (
            "left_readback_ok", "right_readback_ok", "stage_readback_ok",
            "selection_hash_match",
        ):
            if not bool(proof.get(field)):
                failures.append(f"pair_direct_selection_{role}_{field}")
    host_proof = host.get("direct_setup_patch") or {}
    sandbox_proof = sandbox.get("direct_setup_patch") or {}
    if host_proof and sandbox_proof:
        host_hash = int_value(host_proof.get("observed_selection_hash"), 0)
        sandbox_hash = int_value(
            sandbox_proof.get("observed_selection_hash"), 0
        )
        if host_hash == 0 or host_hash != sandbox_hash:
            failures.append("pair_direct_selection_observed_hash_mismatch")
    return failures


def events_named(events: list[dict[str, Any]], name: str) -> list[dict[str, Any]]:
    return [event for event in events if event.get("event") == name]


def is_online_player_match_battle_scene_text(scene_text: str) -> bool:
    if not scene_text:
        return False
    for bad in (
        "PlayerMatchLobbyScene",
        "PlayerMatchSetupScene",
        "PlayerMatchInviteScene",
        "MainMenu",
        "Title",
        "Advertise",
        "Training",
        "Replay",
        "BattleSetup",
        "SetupScene",
    ):
        if bad in scene_text:
            return False
    return "PlayerMatchScene" in scene_text


def observe_gameflow_failures(
    configured: dict[str, Any] | None,
    role_manifest: dict[str, Any] | None,
    latest_observe: dict[str, Any] | None,
    *,
    capture_gameflow: bool = False,
) -> list[str]:
    failures = configured_event_failures(configured, "baseline-oracle")
    if not bool((configured or {}).get("observe_gameflow_requested")):
        failures.append("observe_gameflow_requested")
    if role_manifest is None:
        failures.append("rollback_two_client_role_manifest")
    if latest_observe is None:
        failures.append("rollback_gameflow_observe")
        return failures
    if capture_gameflow:
        return failures
    scene_text = " ".join(
        str(latest_observe.get(key, ""))
        for key in ("current_scene_class", "current_scene_name")
    )
    real_online_battle_scene = (
        bool(latest_observe.get("player_match_scene"))
        and is_online_player_match_battle_scene_text(scene_text)
    )
    if not real_online_battle_scene:
        current = (
            str(latest_observe.get("current_scene_class", ""))
            or str(latest_observe.get("current_scene_name", ""))
            or "unknown"
        )
        failures.append(f"online_battle_scene_not_reached:{current}")
    return failures


def compact_observe_event(event: dict[str, Any]) -> dict[str, Any]:
    keys = (
        "event",
        "client_role",
        "request_id",
        "tick",
        "index",
        "ok",
        "battle_scene",
        "battle_scene_seen",
        "player_match_scene",
        "player_match_lobby_scene",
        "main_menu_scene",
        "current_scene_class",
        "current_scene_name",
        "next_scene_class",
        "next_scene_name",
        "scene_class",
        "scene_name",
        "function_count",
        "property_count",
        "current_scene_name_text",
        "prev_scene_name_text",
        "function",
        "function_signature",
        "context_class",
        "context_name",
        "params_summary",
        "process_event_count",
        "process_event_overflow_count",
        "_trace_file",
    )
    return {key: event.get(key) for key in keys if key in event}


def compact_observe_events(
    events: list[dict[str, Any]],
    *,
    limit: int,
) -> list[dict[str, Any]]:
    if limit <= 0:
        return []
    selected = events[-limit:]
    return [compact_observe_event(event) for event in selected]


PACKET_TIMELINE_OPCODE_21 = 21
PACKET_TIMELINE_OPCODE_21_KEY = "opcode_21_dec_0x15"

PACKET_TIMELINE_STAGES: tuple[dict[str, str], ...] = (
    {
        "key": "route_key_enumerate",
        "event": "rollback_luxor_route_key_enumerate",
        "opcode_field": "timeline_opcode",
    },
    {
        "key": "route_key_list_build",
        "event": "rollback_luxor_route_key_list_build",
        "opcode_field": "opcode",
    },
    {
        "key": "route_writer_source_acquire",
        "event": "rollback_luxor_route_writer_source_acquire",
        "opcode_field": "opcode",
    },
    {
        "key": "route_writer_acquire",
        "event": "rollback_luxor_route_writer_acquire",
        "opcode_field": "opcode",
    },
    {
        "key": "route_writer_assign",
        "event": "rollback_luxor_route_writer_assign",
        "opcode_field": "opcode",
    },
    {
        "key": "queued_work_item_clone",
        "event": "rollback_luxor_queued_work_item_clone",
        "opcode_field": "timeline_opcode",
    },
    {
        "key": "queued_opcode_send",
        "event": "rollback_luxor_queued_opcode_send",
        "opcode_field": "outer_opcode",
    },
    {
        "key": "host_packet_archive_init",
        "event": "rollback_luxor_host_packet_archive_init",
        "opcode_field": "timeline_opcode",
    },
    {
        "key": "packet_routing_tag_copy",
        "event": "rollback_luxor_packet_routing_tag_copy",
        "opcode_field": "timeline_opcode",
    },
    {
        "key": "active_endpoint_send",
        "event": "rollback_luxor_active_endpoint_send",
        "opcode_field": "packet_data_byte0",
    },
    {
        "key": "route_writer_send",
        "event": "rollback_luxor_route_writer_send",
        "opcode_field": "packet_data_byte0",
    },
    {
        "key": "route_writer_owner_tag_match",
        "event": "rollback_luxor_route_writer_owner_tag_match",
        "opcode_field": "timeline_opcode",
    },
    {
        "key": "route_writer_backend_send",
        "event": "rollback_luxor_route_writer_backend_send",
        "opcode_field": "payload_opcode",
    },
    {
        "key": "backend_connection_destination_match",
        "event": "rollback_luxor_backend_connection_destination_match",
        "opcode_field": "timeline_opcode",
    },
    {
        "key": "backend_connection_raw_send",
        "event": "rollback_luxor_backend_connection_raw_send",
        "opcode_field": "payload_opcode",
    },
    {
        "key": "route_channel_append",
        "event": "rollback_luxor_route_channel_append",
        "opcode_field": "payload_opcode",
    },
    {
        "key": "route_output_queue",
        "event": "rollback_luxor_route_output_task_queue",
        "opcode_field": "frame_payload_opcode",
    },
    {
        "key": "active_dispatch_consumer",
        "event": "rollback_luxor_route_output_task_consumer",
        "opcode_field": "frame_payload_opcode",
    },
    {
        "key": "forwarding_consumer",
        "event": "rollback_luxor_forwarding_route_output_task_consumer",
        "opcode_field": "frame_payload_opcode",
    },
    {
        "key": "forwarded_route_opcode_dispatch",
        "event": "rollback_luxor_forwarded_route_opcode_dispatch",
        "opcode_field": "forwarded_opcode",
    },
    {
        "key": "lower_sender_send",
        "event": "rollback_luxor_lower_sender_send",
        "opcode_field": "payload_opcode",
    },
    {
        "key": "lower_transport_send_if_ready",
        "event": "rollback_luxor_lower_transport_send_if_ready",
        "opcode_field": "frame_payload_opcode",
    },
    {
        "key": "backend_packet_stream_receive",
        "event": "rollback_luxor_backend_packet_stream_receive",
        "opcode_field": "frame_payload_opcode",
    },
    {
        "key": "active_packet_dispatch",
        "event": "rollback_luxor_active_packet_dispatch",
        "opcode_field": "packet_byte0",
    },
    {
        "key": "active_opcode15_message",
        "event": "rollback_luxor_active_opcode15_message",
        "opcode_field": "timeline_opcode",
    },
    {
        "key": "ready_keyed_dispatch",
        "event": "rollback_luxor_ready_keyed_dispatch",
        "opcode_field": "timeline_opcode",
    },
)

PACKET_TIMELINE_DECISION_ORDER = (
    "route_key_enumerate",
    "route_key_list_build",
    "route_writer_source_acquire",
    "route_writer_acquire",
    "route_writer_assign",
    "queued_work_item_clone",
    "queued_opcode_send",
    "host_packet_archive_init",
    "packet_routing_tag_copy",
    "active_endpoint_send",
    "route_writer_send",
    "route_writer_owner_tag_match",
    "route_writer_backend_send",
    "backend_connection_destination_match",
    "backend_connection_raw_send",
    "lower_sender_send",
    "lower_transport_send_if_ready",
    "backend_packet_stream_receive",
    "route_channel_append",
    "route_output_queue",
    "active_dispatch_consumer",
    "forwarding_consumer",
    "forwarded_route_opcode_dispatch",
    "active_packet_dispatch",
    "active_opcode15_message",
    "ready_keyed_dispatch",
)

PACKET_TIMELINE_PROJECTION_FIELDS = (
    "_trace_file",
    "ts_qpc",
    "qpc",
    "thread_id",
    "pid",
    "image_base",
    "client_role",
    "phase",
    "caller_rva",
    "callback_pool",
    "dispatch_group",
    "dispatch_code",
    "inner_opcode",
    "listener_count",
    "route",
    "route_vtable",
    "route_key",
    "local_user_slot",
    "result",
    "result_out",
    "result_pair",
    "endpoint",
    "endpoint_vtable",
    "endpoint_local_user_slot",
    "packet",
    "packet_cursor",
    "packet_data",
    "packet_size",
    "packet_capacity",
    "packet_byte0",
    "packet_byte1",
    "packet_byte2",
    "packet_byte3",
    "routing_tag",
    "archive_routing_tag",
    "routing_tag_vtable",
    "archive_routing_tag_vtable",
    "routing_tag_key_object",
    "routing_tag_destination_object",
    "routing_tag_peer_flags",
    "route_tag_global_expected_online_session",
    "route_tag_global_default_packet_routing_tag",
    "route_tag_global_sentinel",
    "route_tag_global_ready_to_connect_channel_key",
    "route_tag_global_replacement",
    "route_tag_global_replacement_route_key_singleton",
    "route_tag_global_replacement_route_key_routing_tag",
    "route_tag_effective",
    "route_tag_matches_global_sentinel",
    "route_tag_matches_global_replacement",
    "route_tag_matches_expected_online_session",
    "route_tag_matches_default_packet_routing_tag",
    "route_tag_matches_ready_to_connect_channel_key",
    "route_tag_effective_matches_replacement_route_key_routing_tag",
    "requested_mode",
    "archive_state",
    "archive_mode",
    "archive_cursor",
    "archive_byte_array",
    "archive_byte_array_ref",
    "archive_data",
    "archive_size",
    "archive_capacity",
    "archive_data_byte0",
    "archive_data_byte1",
    "dest_packet",
    "source_packet",
    "source_route_key_vtable",
    "dest_routing_tag_before",
    "source_routing_tag",
    "dest_routing_tag_after",
    "source_work_item",
    "source_work_item_vtable",
    "source_session_connection",
    "source_inner_opcode",
    "source_channel_id",
    "cloned_work_item",
    "cloned_work_item_vtable",
    "cloned_session_connection",
    "cloned_route_key_vtable",
    "cloned_routing_tag",
    "cloned_inner_opcode",
    "cloned_channel_id",
    "outer_opcode",
    "native_global_default_packet_routing_tag_qword",
    "native_global_sentinel_tag_qword",
    "native_global_replacement_tag_qword",
    "from_queued_opcode_send",
    "timeline_opcode",
    "requested_route_key",
    "requested_routing_tag",
    "requested_routing_tag_effective",
    "route_key",
    "route_key_mode",
    "route_tag",
    "native_route_source",
    "native_route_source_vtable",
    "native_route_source_key_object",
    "native_route_source_destination_object",
    "native_route_source_peer_flags",
    "use_routing_tag_object",
    "writer_owner_pair",
    "out_writer_owner",
    "out_owner",
    "out_owner_ref",
    "selected_writer",
    "writer_mode",
    "registry",
    "candidate_owner",
    "candidate_owner_ref",
    "candidate_owner_vtable",
    "candidate_route_key_accessor_target",
    "candidate_route_key_accessor_rva",
    "candidate_route_key_kind",
    "candidate_route_key_inferred",
    "candidate_routing_tag_inferred",
    "candidate_routing_tag_matches_requested",
    "candidate_accessor_is_replacement",
    "candidate_accessor_is_peer",
    "packet_data_byte0",
    "packet_data_byte1",
    "packet_data_byte2",
    "packet_data_byte3",
    "packet_after_routing_tag",
    "packet_after_native_routing_tag",
    "packet_after_routing_tag_vtable",
    "packet_after_routing_tag_key_object",
    "packet_after_routing_tag_destination_object",
    "packet_after_routing_tag_peer_flags",
    "packet_after_data_byte0",
    "packet_after_data_byte1",
    "packet_after_size",
    "opcode21_route_tag_rewrite_attempted",
    "opcode21_route_tag_rewrite_applied",
    "opcode21_route_tag_restore_ok",
    "opcode21_route_tag_original",
    "opcode21_route_tag_temporary",
    "payload_opcode",
    "payload_byte1",
    "payload_byte2",
    "destination_key_pair",
    "destination_key",
    "destination_ref",
    "destination_key_vtable",
    "destination_key_key_object",
    "destination_key_destination_object",
    "destination_key_peer_flags",
    "candidate_connection_pair",
    "candidate_connection",
    "candidate_connection_ref",
    "candidate_connection_vtable",
    "candidate_raw_send_target",
    "candidate_raw_send_target_rva",
    "candidate_route_kind",
    "match_result",
    "frame_payload_opcode",
    "frame_payload_byte1",
    "frame_payload_byte2",
    "forwarded_opcode",
    "frame_magic",
    "frame_magic_match",
    "frame_size",
    "frame_payload_size",
    "marker",
    "frame_marker",
    "local_user",
    "frame_local_user",
    "active_connect",
    "active_state",
    "active_sub_state",
    "transport",
    "transport_tick",
    "transport_status",
    "transport_ready",
    "transport_is_host",
    "transport_channel_count",
    "transport_channel_capacity",
    "writer",
    "parent",
    "route_writer_registry",
    "route_writer_registry_entries_begin",
    "route_writer_registry_entries_end",
    "route_writer_registry_entries_capacity_end",
    "route_writer_registry_enabled",
    "route_writer_registry_entry_count",
    "route_writer_registry_sample_count",
    "route_writer_registry_selected_index",
    "route_writer_registry_selected_owner",
    "route_writer_registry_selected_owner_ref",
    "route_writer_registry_selected_owner_vtable",
    "route_writer_registry_selected_writer",
    "route_writer_registry_selected_writer_vtable",
    "route_writer_registry_selected_owner_route_key",
    "route_writer_registry_selected_owner_route_key_vtable",
    "route_writer_registry_selected_owner_route_key_routing_tag",
    "route_writer_registry_selected_writer_send_target",
    "route_writer_registry_entry0_owner",
    "route_writer_registry_entry0_owner_ref",
    "route_writer_registry_entry0_owner_vtable",
    "route_writer_registry_entry0_writer",
    "route_writer_registry_entry0_writer_vtable",
    "route_writer_registry_entry0_owner_route_key",
    "route_writer_registry_entry0_owner_route_key_vtable",
    "route_writer_registry_entry0_owner_route_key_routing_tag",
    "route_writer_registry_entry0_writer_send_target",
    "route_writer_registry_entry0_owner_ready_target",
    "route_writer_registry_entry0_owner_identity_target",
    "route_writer_registry_entry0_owner_backend_get_target",
    "route_writer_registry_entry1_owner",
    "route_writer_registry_entry1_owner_ref",
    "route_writer_registry_entry1_owner_vtable",
    "route_writer_registry_entry1_writer",
    "route_writer_registry_entry1_writer_vtable",
    "route_writer_registry_entry1_owner_route_key",
    "route_writer_registry_entry1_owner_route_key_vtable",
    "route_writer_registry_entry1_owner_route_key_routing_tag",
    "route_writer_registry_entry1_writer_send_target",
    "route_writer_registry_entry1_owner_ready_target",
    "route_writer_registry_entry1_owner_identity_target",
    "route_writer_registry_entry1_owner_backend_get_target",
    "route_writer_registry_entry2_owner",
    "route_writer_registry_entry2_owner_ref",
    "route_writer_registry_entry2_owner_vtable",
    "route_writer_registry_entry2_writer",
    "route_writer_registry_entry2_writer_vtable",
    "route_writer_registry_entry2_owner_route_key",
    "route_writer_registry_entry2_owner_route_key_vtable",
    "route_writer_registry_entry2_owner_route_key_routing_tag",
    "route_writer_registry_entry2_writer_send_target",
    "route_writer_registry_entry2_owner_ready_target",
    "route_writer_registry_entry2_owner_identity_target",
    "route_writer_registry_entry2_owner_backend_get_target",
    "route_writer_registry_entry3_owner",
    "route_writer_registry_entry3_owner_ref",
    "route_writer_registry_entry3_owner_vtable",
    "route_writer_registry_entry3_writer",
    "route_writer_registry_entry3_writer_vtable",
    "route_writer_registry_entry3_owner_route_key",
    "route_writer_registry_entry3_owner_route_key_vtable",
    "route_writer_registry_entry3_owner_route_key_routing_tag",
    "route_writer_registry_entry3_writer_send_target",
    "route_writer_registry_entry3_owner_ready_target",
    "route_writer_registry_entry3_owner_identity_target",
    "route_writer_registry_entry3_owner_backend_get_target",
    "route_writer_registry_entry4_owner",
    "route_writer_registry_entry4_owner_ref",
    "route_writer_registry_entry4_owner_vtable",
    "route_writer_registry_entry4_writer",
    "route_writer_registry_entry4_writer_vtable",
    "route_writer_registry_entry4_owner_route_key",
    "route_writer_registry_entry4_owner_route_key_vtable",
    "route_writer_registry_entry4_owner_route_key_routing_tag",
    "route_writer_registry_entry4_writer_send_target",
    "route_writer_registry_entry4_owner_ready_target",
    "route_writer_registry_entry4_owner_identity_target",
    "route_writer_registry_entry4_owner_backend_get_target",
    "route_writer_registry_entry5_owner",
    "route_writer_registry_entry5_owner_ref",
    "route_writer_registry_entry5_owner_vtable",
    "route_writer_registry_entry5_writer",
    "route_writer_registry_entry5_writer_vtable",
    "route_writer_registry_entry5_owner_route_key",
    "route_writer_registry_entry5_owner_route_key_vtable",
    "route_writer_registry_entry5_owner_route_key_routing_tag",
    "route_writer_registry_entry5_writer_send_target",
    "route_writer_registry_entry5_owner_ready_target",
    "route_writer_registry_entry5_owner_identity_target",
    "route_writer_registry_entry5_owner_backend_get_target",
    "route_writer_registry_entry6_owner",
    "route_writer_registry_entry6_owner_ref",
    "route_writer_registry_entry6_owner_vtable",
    "route_writer_registry_entry6_writer",
    "route_writer_registry_entry6_writer_vtable",
    "route_writer_registry_entry6_owner_route_key",
    "route_writer_registry_entry6_owner_route_key_vtable",
    "route_writer_registry_entry6_owner_route_key_routing_tag",
    "route_writer_registry_entry6_writer_send_target",
    "route_writer_registry_entry6_owner_ready_target",
    "route_writer_registry_entry6_owner_identity_target",
    "route_writer_registry_entry6_owner_backend_get_target",
    "route_writer_registry_entry7_owner",
    "route_writer_registry_entry7_owner_ref",
    "route_writer_registry_entry7_owner_vtable",
    "route_writer_registry_entry7_writer",
    "route_writer_registry_entry7_writer_vtable",
    "route_writer_registry_entry7_owner_route_key",
    "route_writer_registry_entry7_owner_route_key_vtable",
    "route_writer_registry_entry7_owner_route_key_routing_tag",
    "route_writer_registry_entry7_writer_send_target",
    "route_writer_registry_entry7_owner_ready_target",
    "route_writer_registry_entry7_owner_identity_target",
    "route_writer_registry_entry7_owner_backend_get_target",
    "route_service",
    "output_array",
    "result_array",
    "entries_begin",
    "entries_end",
    "entries_capacity_end",
    "entry_count",
    "sample_count",
    "service_shared_refs_begin",
    "service_shared_refs_end",
    "service_shared_refs_capacity_end",
    "service_shared_ref_count",
    "replacement_count",
    "nonreplacement_count",
    "default_count",
    "expected_count",
    "entry0_owner",
    "entry0_route_tag",
    "entry0_matches_replacement",
    "entry0_matches_default",
    "entry0_matches_expected",
    "entry1_owner",
    "entry1_route_tag",
    "entry1_matches_replacement",
    "entry1_matches_default",
    "entry1_matches_expected",
    "entry2_owner",
    "entry2_route_tag",
    "entry2_matches_replacement",
    "entry2_matches_default",
    "entry2_matches_expected",
    "entry3_owner",
    "entry3_route_tag",
    "entry3_matches_replacement",
    "entry3_matches_default",
    "entry3_matches_expected",
    "backend",
    "backend_parent",
    "backend_connection_table_owner",
    "backend_connection_table",
    "backend_connection_table_entries_begin",
    "backend_connection_table_entries_end",
    "backend_connection_table_entries_capacity_end",
    "backend_connection_table_entry_count",
    "backend_connection_table_sample_count",
    "backend_connection_table_selected_index",
    "backend_connection_table_selected_connection",
    "backend_connection_table_selected_raw_send_target",
    "backend_connection_table_selected_raw_send_target_rva",
    "backend_connection_table_selected_route_kind",
    *(
        f"backend_connection_table_entry{index}_{suffix}"
        for index in range(8)
        for suffix in (
            "connection",
            "connection_ref",
            "connection_vtable",
            "raw_send_target",
            "raw_send_target_rva",
            "lower_transport",
            "lower_transport_vtable",
            "lower_ready_target",
            "lower_sender",
            "lower_sender_vtable",
            "lower_sender_send_target",
            "lower_sender_send_target_rva",
            "route_selector",
            "route_channel",
            "route_sink",
            "sender_state",
        )
    ),
    "connection",
    "connection_route_kind",
    "connection_lower_transport",
    "connection_route_selector",
    "connection_route_dispatch_channel",
    "destination",
    "destination_key",
    "destination_key_vtable",
    "destination_key_key_object",
    "destination_key_destination_object",
    "destination_key_peer_flags",
    "destination_ref",
    "route_identity",
    "route_identity_vtable",
    "route_identity_key_object",
    "route_identity_destination_object",
    "route_identity_peer_flags",
    "route_channel",
    "route_selector",
    "identity_object",
    "identity_ref",
    "output_slot",
    "slot_index",
    "consumer_layout",
    "consumer_layout_name",
    "consumer",
    "consumer_accept_target_rva",
    "callback_rva",
    "receiver_base",
    "receiver_ref",
    "receiver_adjustment",
    "adjusted_receiver",
    "dispatcher",
    "opcode_tree",
    "handler_found",
    "handler_node",
    "handler_key",
    "handler_storage",
    "lower_transport",
    "lower_sender",
    "raw_send_target",
    "raw_send_target_rva",
    "packet_stream_route_kind",
    "lower_sender_send_target_rva",
    "lower_ready_target_rva",
)


def packet_opcode_key(opcode: int) -> str:
    if opcode == PACKET_TIMELINE_OPCODE_21:
        return PACKET_TIMELINE_OPCODE_21_KEY
    if opcode < 0:
        return "opcode_unknown"
    return f"opcode_{opcode}"


def packet_event_projection(
    event: dict[str, Any],
    opcode_field: str,
) -> dict[str, Any]:
    projected: dict[str, Any] = {
        key: event[key]
        for key in PACKET_TIMELINE_PROJECTION_FIELDS
        if key in event
    }
    projected["opcode_field"] = opcode_field
    projected["opcode"] = int_value(event.get(opcode_field), -1)
    return projected


def trace_jsonl_paths_from_value(value: Any) -> list[Path]:
    paths: list[Path] = []
    seen: set[str] = set()

    def add_path(raw_path: str) -> None:
        if not raw_path.lower().endswith(".jsonl"):
            return
        path = Path(raw_path)
        key = str(path).lower()
        if key in seen:
            return
        seen.add(key)
        paths.append(path)

    def walk(node: Any) -> None:
        if isinstance(node, dict):
            for key, child in node.items():
                if isinstance(child, str) and (
                    key == "_trace_file"
                    or "trace" in str(key).lower()
                ):
                    add_path(child)
                else:
                    walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)

    walk(value)
    return paths


def read_trace_jsonl_file_events(
    path: Path,
    *,
    request_id: str = "",
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    try:
        f = path.open("rb")
    except OSError:
        return events
    with f:
        for raw_line in f:
            try:
                line = raw_line.decode("utf-8", errors="replace")
            except Exception:
                continue
            if request_id and request_id not in line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if request_id and event.get("request_id") != request_id:
                continue
            event["_trace_file"] = str(path)
            events.append(event)
    return events


def summarize_packet_timeline(
    events: list[dict[str, Any]],
    *,
    role: str = "",
    request_id: str = "",
) -> dict[str, Any]:
    stages: dict[str, Any] = {}
    trace_files = sorted(
        {
            str(event.get("_trace_file", ""))
            for event in events
            if event.get("_trace_file")
        }
    )
    image_bases = sorted(
        {
            str(event.get("image_base", ""))
            for event in events
            if event.get("image_base")
        }
    )
    for spec in PACKET_TIMELINE_STAGES:
        stage_events = events_named(events, spec["event"])
        opcode_field = spec["opcode_field"]
        opcode_counts: dict[str, int] = {}
        opcode_21_events: list[dict[str, Any]] = []
        for event in stage_events:
            opcode = int_value(event.get(opcode_field), -1)
            key = packet_opcode_key(opcode)
            opcode_counts[key] = opcode_counts.get(key, 0) + 1
            if opcode == PACKET_TIMELINE_OPCODE_21:
                opcode_21_events.append(event)
        stage: dict[str, Any] = {
            "event": spec["event"],
            "opcode_field": opcode_field,
            "event_count": len(stage_events),
            "opcode_counts": opcode_counts,
            "opcode_21_dec_0x15_count": len(opcode_21_events),
        }
        if opcode_21_events:
            stage["first_opcode_21_dec_0x15"] = packet_event_projection(
                opcode_21_events[0],
                opcode_field,
            )
            stage["last_opcode_21_dec_0x15"] = packet_event_projection(
                opcode_21_events[-1],
                opcode_field,
            )
        elif stage_events:
            stage["last_event"] = packet_event_projection(
                stage_events[-1],
                opcode_field,
            )
        stages[spec["key"]] = stage

    present = [
        stage
        for stage in PACKET_TIMELINE_DECISION_ORDER
        if stages.get(stage, {}).get("opcode_21_dec_0x15_count", 0) > 0
    ]
    absent_after_present: list[str] = []
    post_gap_present: list[str] = []
    first_missing_after_present = ""
    last_present_before_gap = ""
    if present:
        started = False
        for stage in PACKET_TIMELINE_DECISION_ORDER:
            if stage in present:
                if first_missing_after_present:
                    post_gap_present.append(stage)
                else:
                    last_present_before_gap = stage
                started = True
                continue
            if not started:
                continue
            absent_after_present.append(stage)
            if not first_missing_after_present:
                first_missing_after_present = stage
    last_present = last_present_before_gap or (present[-1] if present else "")
    stop_boundary = (
        f"{last_present}_to_{first_missing_after_present}"
        if last_present and first_missing_after_present
        else ("not_observed_in_role" if not present else "")
    )
    return {
        "role": role,
        "request_id": request_id,
        "trace_files": trace_files,
        "image_bases": image_bases,
        "opcode": PACKET_TIMELINE_OPCODE_21_KEY,
        "present_stages": present,
        "first_present_stage": present[0] if present else "",
        "last_present_stage": last_present,
        "first_missing_after_present": first_missing_after_present,
        "absent_after_present": absent_after_present,
        "post_gap_present_stages": post_gap_present,
        "stop_boundary": stop_boundary,
        "stages": stages,
    }


def packet_timeline_from_results(
    results: list[dict[str, Any]],
) -> dict[str, Any]:
    by_role: dict[str, Any] = {}
    for result in results:
        role = str(result.get("root", {}).get("role") or result.get("role") or "")
        timeline = result.get("packet_timeline")
        if not isinstance(timeline, dict):
            request_id = str(result.get("request_id") or "")
            events: list[dict[str, Any]] = []
            for path in trace_jsonl_paths_from_value(result):
                events.extend(
                    read_trace_jsonl_file_events(path, request_id=request_id)
                )
            if events:
                timeline = summarize_packet_timeline(
                    events,
                    role=role,
                    request_id=request_id,
                )
        if role and isinstance(timeline, dict):
            by_role[role] = timeline
    return by_role


def summarize_packet_timeline_results(
    timelines_by_role: dict[str, Any],
) -> dict[str, Any]:
    roles: dict[str, Any] = {}
    for role, timeline in sorted(timelines_by_role.items()):
        if not isinstance(timeline, dict):
            continue
        roles[role] = {
            "present_stages": timeline.get("present_stages", []),
            "first_present_stage": timeline.get("first_present_stage", ""),
            "last_present_stage": timeline.get("last_present_stage", ""),
            "first_missing_after_present": timeline.get(
                "first_missing_after_present", ""
            ),
            "post_gap_present_stages": timeline.get("post_gap_present_stages", []),
            "stop_boundary": timeline.get("stop_boundary", ""),
        }
    host = roles.get("host", {})
    sandbox = roles.get("sandbox", {})
    diagnosis = ""
    host_present = set(host.get("present_stages") or [])
    sandbox_present = set(sandbox.get("present_stages") or [])
    if "lower_sender_send" not in host_present and (
        "backend_connection_raw_send" in host_present
        or "forwarded_route_opcode_dispatch" in host_present
    ):
        diagnosis = "host_opcode_21_dec_0x15_stops_before_lower_sender"
    elif "lower_sender_send" in host_present and not sandbox_present:
        diagnosis = "host_opcode_21_dec_0x15_sent_but_not_seen_by_sandbox"
    elif (
        "backend_packet_stream_receive" in sandbox_present
        and "active_packet_dispatch" not in sandbox_present
    ):
        diagnosis = "sandbox_receives_opcode_21_dec_0x15_before_active_dispatch"
    elif (
        "active_packet_dispatch" in sandbox_present
        and "active_opcode15_message" not in sandbox_present
    ):
        diagnosis = "sandbox_active_dispatch_does_not_reach_opcode15_handler"
    elif (
        "active_opcode15_message" in sandbox_present
        and "ready_keyed_dispatch" not in sandbox_present
    ):
        diagnosis = "sandbox_opcode15_handler_stops_before_route_delegate"
    elif "ready_keyed_dispatch" in sandbox_present:
        diagnosis = "sandbox_opcode15_route_delegate_reached"
    elif "active_packet_dispatch" in sandbox_present:
        diagnosis = "sandbox_active_dispatch_receives_opcode_21_dec_0x15"
    elif not host_present:
        diagnosis = "opcode_21_dec_0x15_not_observed_on_host"
    return {
        "opcode": PACKET_TIMELINE_OPCODE_21_KEY,
        "roles": roles,
        "diagnosis": diagnosis,
    }


def packet_timeline_from_phase_report(report: dict[str, Any]) -> dict[str, Any]:
    results = [
        result for result in report.get("results", [])
        if isinstance(result, dict)
    ]
    return packet_timeline_from_results(results)


def nonzero_unique_hex_values(
    events: list[dict[str, Any]],
    field: str,
) -> set[int]:
    values: set[int] = set()
    for event in events:
        value = int_value(event.get(field), 0)
        if value:
            values.add(value)
    return values


def soak_failures(
    role_manifest: dict[str, Any] | None,
    bind: dict[str, Any] | None,
    handshake: dict[str, Any] | None,
    stock_online: dict[str, Any] | None,
    activation_candidate: dict[str, Any] | None,
    disarm: dict[str, Any] | None,
    cache_write: dict[str, Any] | None,
    correction: dict[str, Any] | None,
    convergence: dict[str, Any] | None,
    events: list[dict[str, Any]],
    expected: dict[str, Any],
) -> list[str]:
    failures: list[str] = []
    failures.extend(role_manifest_failures(role_manifest, expected))
    failures.extend(sidecar_failures(bind, handshake, expected))
    failures.extend(live_traffic_failures(stock_online))

    live_events = events_named(events, "rollback_live_online_capture")
    sidecar_bind_events = events_named(events, "rollback_sidecar_bind")
    sidecar_handshake_events = events_named(events, "rollback_sidecar_handshake")
    activation_events = events_named(events, "rollback_live_activation_candidate")

    if len(live_events) < 2:
        failures.append(f"soak_live_online_event_count={len(live_events)}")
    if len(sidecar_handshake_events) < 2:
        failures.append(
            f"soak_sidecar_handshake_event_count={len(sidecar_handshake_events)}"
        )

    for event in live_events:
        if bool(event.get("boundary_violation")):
            failures.append("soak_boundary_violation")
            break

    session_ptrs = nonzero_unique_hex_values(live_events, "last_session_ptr")
    if not session_ptrs:
        failures.append("soak_session_pointer_missing")
    elif len(session_ptrs) != 1:
        failures.append(
            "soak_session_pointer_changed "
            + ",".join(f"0x{value:X}" for value in sorted(session_ptrs))
        )

    input_logs = nonzero_unique_hex_values(live_events, "last_input_log")
    receive_logs = nonzero_unique_hex_values(live_events, "last_receive_input_log")
    if len(input_logs) > 1:
        failures.append(
            "soak_input_log_changed "
            + ",".join(f"0x{value:X}" for value in sorted(input_logs))
        )
    if len(receive_logs) > 1:
        failures.append(
            "soak_receive_input_log_changed "
            + ",".join(f"0x{value:X}" for value in sorted(receive_logs))
        )

    for event in sidecar_bind_events:
        for field in (
            "wsa_startup_error",
            "socket_error",
            "bind_error",
            "ioctlsocket_error",
            "udp_connreset_error",
        ):
            value = int_value(event.get(field), 0)
            if value:
                failures.append(f"soak_sidecar_bind_error:{field}={value}")
        if bool(event.get("reserved_steam_port_rejected")):
            failures.append("soak_sidecar_reserved_steam_port")

    for event in sidecar_handshake_events:
        for field in ("sendto_error", "recvfrom_error"):
            value = int_value(event.get(field), 0)
            if value:
                failures.append(f"soak_sidecar_handshake_error:{field}={value}")

    if activation_candidate is not None and bool(
        activation_candidate.get("explicit_operator_enable")
    ):
        failures.append("soak_unexpected_activation_arm")
    for event in activation_events:
        if bool(event.get("explicit_operator_enable")):
            failures.append("soak_unexpected_activation_event")
            break

    if disarm is not None:
        failures.append(f"soak_live_disarmed:{disarm.get('reason', 'unknown')}")
    if cache_write is not None:
        failures.append("soak_unexpected_live_cache_write")
    if correction is not None:
        failures.append("soak_unexpected_live_correction")
    if convergence is not None:
        failures.append("soak_unexpected_live_convergence")

    return failures


def direct_soak_failures(
    role_manifest: dict[str, Any] | None,
    bind: dict[str, Any] | None,
    handshake: dict[str, Any] | None,
    direct_stage: dict[str, Any] | None,
    direct_connect: dict[str, Any] | None,
    direct_replay_input: dict[str, Any] | None,
    disarm: dict[str, Any] | None,
    cache_write: dict[str, Any] | None,
    live_correction: dict[str, Any] | None,
    live_convergence: dict[str, Any] | None,
    direct_correction: dict[str, Any] | None,
    events: list[dict[str, Any]],
    expected: dict[str, Any],
) -> list[str]:
    failures: list[str] = []
    failures.extend(role_manifest_failures(role_manifest, expected))
    failures.extend(sidecar_failures(bind, handshake, expected))
    failures.extend(direct_stage_mode_failures(direct_stage, expected))
    failures.extend(direct_connect_failures(direct_connect, expected))
    failures.extend(direct_replay_input_failures(direct_replay_input, expected))

    for event in events_named(events, "rollback_sidecar_bind"):
        for field in (
            "wsa_startup_error",
            "socket_error",
            "bind_error",
            "ioctlsocket_error",
            "udp_connreset_error",
        ):
            value = int_value(event.get(field), 0)
            if value:
                failures.append(f"direct_soak_sidecar_bind_error:{field}={value}")
        if bool(event.get("reserved_steam_port_rejected")):
            failures.append("direct_soak_sidecar_reserved_steam_port")

    for event in events_named(events, "rollback_direct_connect"):
        for field in (
            "sendto_error",
            "recvfrom_error",
            "udp_connreset_error",
            "direct_packets_rejected",
        ):
            value = int_value(event.get(field), 0)
            if value:
                failures.append(f"direct_soak_transport_error:{field}={value}")
        for gate in (
            "wrong_endpoint_rejected",
            "wrong_route_rejected",
            "wrong_token_rejected",
            "wrong_packet_type_rejected",
            "wrong_direct_sequence_rejected",
            "wrong_direct_payload_rejected",
        ):
            if bool(event.get(gate)):
                failures.append(f"direct_soak_{gate}")

    if disarm is not None:
        failures.append(f"direct_soak_live_disarmed:{disarm.get('reason', 'unknown')}")
    if cache_write is not None:
        failures.append("direct_soak_unexpected_live_cache_write")
    if live_correction is not None:
        failures.append("direct_soak_unexpected_live_correction")
    if live_convergence is not None:
        failures.append("direct_soak_unexpected_live_convergence")
    if direct_correction is not None:
        failures.append("direct_soak_unexpected_direct_correction")
    return failures


def direct_release_failures(
    role_manifest: dict[str, Any] | None,
    bind: dict[str, Any] | None,
    handshake: dict[str, Any] | None,
    online_stage: dict[str, Any] | None,
    direct_stage: dict[str, Any] | None,
    direct_connect: dict[str, Any] | None,
    direct_replay_input: dict[str, Any] | None,
    direct_correction: dict[str, Any] | None,
    disarm: dict[str, Any] | None,
    cache_write: dict[str, Any] | None,
    live_correction: dict[str, Any] | None,
    live_convergence: dict[str, Any] | None,
    events: list[dict[str, Any]],
    expected: dict[str, Any],
) -> list[str]:
    failures: list[str] = []
    failures.extend(role_manifest_failures(role_manifest, expected))
    failures.extend(sidecar_failures(bind, handshake, expected))
    if bool(expected.get("skip_online_stage_drive")):
        if online_stage is not None:
            failures.append("direct_release_unexpected_online_stage")
    else:
        failures.extend(direct_release_online_stage_failures(online_stage, expected))
    failures.extend(direct_release_stage_failures(direct_stage, expected))
    failures.extend(direct_connect_failures(direct_connect, expected))
    failures.extend(direct_replay_input_failures(direct_replay_input, expected))
    failures.extend(direct_correction_failures(direct_correction, disarm))

    setup_patches = events_named(events, "rollback_direct_stage_setup_patch")
    native_launches = events_named(events, "rollback_direct_stage_native_launch")
    asset_requests = events_named(events, "rollback_direct_stage_battle_asset_request")
    battle_scene_events = events_named(events, "rollback_direct_stage_battle_scene")
    if setup_patches:
        failures.append(f"direct_release_setup_patch_count={len(setup_patches)}")
    if native_launches:
        failures.append(f"direct_release_native_launch_count={len(native_launches)}")
    if asset_requests:
        failures.append(f"direct_release_asset_request_count={len(asset_requests)}")

    if direct_replay_input is not None:
        refreshes = int_value(direct_replay_input.get("direct_cache_refreshes"), 0)
        if refreshes < 2:
            failures.append(f"direct_release_direct_cache_refreshes={refreshes}")
        master = int_value(direct_replay_input.get("direct_master_clock"), 0)
        last = int_value(direct_replay_input.get("direct_last_frame"), 0)
        if last <= master:
            failures.append(
                f"direct_release_input_cache_not_ahead master={master} last={last}"
            )

    for event in events_named(events, "rollback_direct_connect"):
        for field in (
            "sendto_error",
            "recvfrom_error",
            "udp_connreset_error",
            "direct_packets_rejected",
        ):
            value = int_value(event.get(field), 0)
            if value:
                failures.append(f"direct_release_transport_error:{field}={value}")
        for gate in (
            "wrong_endpoint_rejected",
            "wrong_route_rejected",
            "wrong_token_rejected",
            "wrong_packet_type_rejected",
            "wrong_direct_sequence_rejected",
            "wrong_direct_payload_rejected",
        ):
            if bool(event.get(gate)):
                failures.append(f"direct_release_{gate}")

    correction_ready = bool(
        direct_correction
        and direct_correction.get("ok")
        and direct_correction.get("sidecar_ready")
        and direct_correction.get("direct_input_ready")
    )
    if disarm is not None and not correction_ready:
        failures.append(f"direct_release_live_disarmed:{disarm.get('reason', 'unknown')}")
    if cache_write is not None:
        failures.append("direct_release_unexpected_live_cache_write")
    if live_correction is not None:
        failures.append("direct_release_unexpected_live_correction")
    if live_convergence is not None:
        failures.append("direct_release_unexpected_live_convergence")
    return failures


def sidecar_fault_closed_failures(
    role_manifest: dict[str, Any] | None,
    bind: dict[str, Any] | None,
    handshake: dict[str, Any] | None,
    disarm: dict[str, Any] | None,
    cache_write: dict[str, Any] | None,
    correction: dict[str, Any] | None,
    convergence: dict[str, Any] | None,
) -> list[str]:
    failures: list[str] = []
    if role_manifest is None:
        failures.append("rollback_two_client_role_manifest")
    else:
        if not bool(role_manifest.get("sidecar_requested")):
            failures.append("fault_role_manifest_sidecar_requested")
        if int_value(role_manifest.get("sidecar_local_port"), 0) in (
            0,
            STEAM_UDP_PORT,
        ):
            failures.append("fault_role_manifest_local_port")
        if int_value(role_manifest.get("sidecar_remote_port"), 0) in (
            0,
            STEAM_UDP_PORT,
        ):
            failures.append("fault_role_manifest_remote_port")

    if bind is None:
        failures.append("rollback_sidecar_bind")
    else:
        for gate in (
            "ok",
            "socket_open",
            "bound_loopback",
            "nonblocking",
            "udp_connreset_disabled",
        ):
            if not bool(bind.get(gate)):
                failures.append(f"fault_sidecar_bind_{gate}")
        if bool(bind.get("reserved_steam_port_rejected")):
            failures.append("fault_sidecar_reserved_steam_port")

    if handshake is None:
        failures.append("rollback_sidecar_handshake")
    elif bool(handshake.get("ok")) or bool(handshake.get("validated_peer")):
        failures.append("fault_unexpected_sidecar_handshake")

    if disarm is None:
        failures.append("rollback_live_disarm")
    elif str(disarm.get("reason", "")) not in {
        "sidecar-not-ready",
        "waiting-for-peer",
        "recvfrom-failed",
    }:
        failures.append(f"fault_disarm_reason={disarm.get('reason', 'missing')}")

    if cache_write is not None:
        failures.append("fault_unexpected_live_cache_write")
    if correction is not None:
        failures.append("fault_unexpected_live_correction")
    if convergence is not None:
        failures.append("fault_unexpected_live_convergence")
    return failures


def client_result(
    root: dict[str, Any],
    request_id: str,
    phase: str,
    endpoints: list[dict[str, Any]],
    expected: dict[str, Any],
    *,
    min_mtime: float = 0.0,
    allowed_pids: set[int] | None = None,
    current_pids: set[int] | None = None,
    crash_indicators: list[dict[str, Any]] | None = None,
    process_query: dict[str, Any] | None = None,
    file_offsets: dict[str, int] | None = None,
    capture_gameflow: bool = False,
) -> dict[str, Any]:
    root_path = Path(root["path"])
    events = read_jsonl_events(
        root_path,
        request_id,
        min_mtime=min_mtime,
        allowed_pids=allowed_pids,
        file_offsets=file_offsets,
    )
    packet_timeline = summarize_packet_timeline(
        events,
        role=str(root.get("role", "")),
        request_id=request_id,
    )
    configured = latest_event(events, "rollback_lab_configured")
    role_manifest = latest_event(events, "rollback_two_client_role_manifest")
    gekko_udp = latest_event(events, "rollback_gekko_udp_selftest")
    stock_online = best_stock_online_event(events)
    activation_candidate = latest_event(
        events, "rollback_live_activation_candidate")
    sidecar_bind = latest_event(events, "rollback_sidecar_bind")
    sidecar_handshake = latest_event(events, "rollback_sidecar_handshake")
    live_cache_write = latest_event(events, "rollback_live_cache_write")
    live_correction = latest_event(events, "rollback_live_correction")
    live_convergence = latest_event(events, "rollback_live_convergence")
    live_disarm = latest_event(events, "rollback_live_disarm")
    online_stage = latest_event(events, "rollback_online_stage")
    online_stage_callbacks = [
        e for e in events if e.get("event") == "rollback_online_stage_callback"
    ]
    online_stage = merge_online_stage_callbacks(
        online_stage, online_stage_callbacks
    )
    replay_input_script = latest_event(events, "rollback_replay_input_script")
    direct_stage = latest_event(events, "rollback_direct_stage")
    direct_setup_patch = latest_event(
        events, "rollback_direct_stage_setup_patch"
    )
    direct_connect = latest_event(events, "rollback_direct_connect")
    direct_replay_input = latest_event(events, "rollback_direct_replay_input")
    direct_correction = latest_event(events, "rollback_direct_correction")
    production_status = latest_event(events, "rollback_production_status")
    mirrored_versus_launch = latest_event(
        events, "rollback_mirrored_versus_launch"
    )
    gameflow_observe_events = events_named(events, "rollback_gameflow_observe")
    gameflow_process_events = events_named(
        events,
        "rollback_gameflow_observe_process_event",
    )
    gameflow_scene_reflections = events_named(
        events,
        "rollback_online_stage_scene_reflection",
    )
    ui_input_events = events_named(
        events, "rollback_main_menu_input_navigation"
    )
    ui_input_event = ui_input_events[-1] if ui_input_events else None
    gameflow_observe = (
        gameflow_observe_events[-1] if gameflow_observe_events else None
    )
    gameflow_setup_seen = any(
        "PlayerMatchSetupScene" in " ".join(
            str(event.get(key, ""))
            for key in (
                "current_scene_class",
                "current_scene_name",
                "next_scene_class",
                "next_scene_name",
            )
        )
        for event in gameflow_observe_events
    )
    gameflow_player_match_scene_seen = any(
        bool(event.get("player_match_scene"))
        and is_online_player_match_battle_scene_text(
            " ".join(
                str(event.get(key, ""))
                for key in ("current_scene_class", "current_scene_name")
            )
        )
        for event in gameflow_observe_events
    )
    gameflow_process_event_overflow_count = max(
        (
            int_value(event.get("process_event_overflow_count"), 0)
            for event in gameflow_observe_events
        ),
        default=0,
    )

    pids = sorted(
        {
            int(e["pid"])
            for e in events
            if isinstance(e.get("pid"), int)
        }
    )
    udp_by_pid = {
        str(pid): endpoint_rows_for_pid(endpoints, pid)
        for pid in pids
    }
    current_failures = (
        current_event_pid_failures(
            pids,
            allowed_pids,
            root=root,
            current_pids=current_pids,
        )
        if allowed_pids is not None else []
    )
    current_failures.extend(
        crash_dialog_failures(root, crash_indicators or [])
    )
    if (
        process_query
        and not process_query.get("valid")
        and int_value(process_query.get("unavailable_consecutive"), 0)
        >= PROCESS_MISSING_FAIL_POLLS
    ):
        current_failures.append(
            "process_query_unavailable "
            f"polls={process_query.get('unavailable_consecutive')}"
        )

    result: dict[str, Any] = {
        "root": root,
        "request_id": request_id,
        "phase": phase,
        "pids_from_events": pids,
        "udp_by_event_pid": udp_by_pid,
        "event_pid_mapping_failures": event_pid_mapping_failures(root, pids),
        "current_event_pid_failures": current_failures,
        "current_sc6_pids": sorted(current_pids or []),
        "process_query": process_query or {},
        "crash_indicators": crash_indicators or [],
        "configured": configured,
        "role_manifest": role_manifest,
        "gekko_udp": gekko_udp,
        "stock_online": stock_online,
        "activation_candidate": activation_candidate,
        "sidecar_bind": sidecar_bind,
        "sidecar_handshake": sidecar_handshake,
        "live_cache_write": live_cache_write,
        "live_correction": live_correction,
        "live_convergence": live_convergence,
        "live_disarm": live_disarm,
        "online_stage": online_stage,
        "ui_input_probe": ui_input_event,
        "ui_input_probe_event_count": len(ui_input_events),
        "online_stage_callback": (
            online_stage_callbacks[-1] if online_stage_callbacks else None
        ),
        "online_stage_callback_count": len(online_stage_callbacks),
        "replay_input_script": replay_input_script,
        "direct_stage": direct_stage,
        "direct_setup_patch": direct_setup_patch,
        "direct_connect": direct_connect,
        "direct_replay_input": direct_replay_input,
        "direct_correction": direct_correction,
        "production_status": production_status,
        "mirrored_versus_launch": mirrored_versus_launch,
        "gameflow_observe": gameflow_observe,
        "gameflow_observe_event_count": len(gameflow_observe_events),
        "gameflow_process_event_count": len(gameflow_process_events),
        "gameflow_process_event_overflow_count": (
            gameflow_process_event_overflow_count
        ),
        "gameflow_scene_reflection_count": len(gameflow_scene_reflections),
        "gameflow_setup_scene_seen": gameflow_setup_seen,
        "gameflow_player_match_scene_seen": gameflow_player_match_scene_seen,
        "gameflow_setup_progressed_to_player_match_scene": (
            gameflow_setup_seen and gameflow_player_match_scene_seen
        ),
        "gameflow_observe_events": compact_observe_events(
            gameflow_observe_events,
            limit=120,
        ),
        "gameflow_process_events": compact_observe_events(
            gameflow_process_events,
            limit=250,
        ),
        "gameflow_scene_reflections": compact_observe_events(
            gameflow_scene_reflections,
            limit=40,
        ),
        "packet_timeline": packet_timeline,
        "expected_role": expected,
        "horse_udp_ports": (
            {
                "port_a": int_value(gekko_udp.get("port_a"), 0),
                "port_b": int_value(gekko_udp.get("port_b"), 0),
            }
            if gekko_udp else {}
        ),
        "event_count": len(events),
        "ok": False,
        "missing": [],
    }

    if phase == "identity":
        missing = configured_event_failures(configured, "baseline-oracle")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "role-manifest":
        missing = configured_event_failures(
            configured,
            "production" if str(expected.get("mode", "")) ==
            "mirrored-versus" else "baseline-oracle",
        )
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "horse-udp":
        missing = configured_event_failures(configured, "gekko-udp")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(horse_udp_failures(gekko_udp))
        result["missing"] = missing
        result["ok"] = not missing
    elif (
        phase in MIRRORED_VERSUS_PHASES
        and str(expected.get("mode", "")) == "mirrored-versus"
    ):
        missing = list(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(
            mirrored_versus_failures(
                production_status,
                mirrored_versus_launch,
                expected,
                phase,
            )
        )
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "sidecar":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "stock-online":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        if stock_online is None:
            missing.append("rollback_live_online_capture")
        else:
            missing.extend(
                gate for gate in STOCK_REQUIRED_GATES
                if not bool(stock_online.get(gate))
            )
            if bool(stock_online.get("boundary_violation")):
                    missing.append("no_boundary_violation")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "online-stage":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        cleanup_only_stage = bool(
            online_stage and online_stage.get("online_stage_cleanup_only")
        )
        if not cleanup_only_stage:
            missing.extend(
                sidecar_failures(sidecar_bind, sidecar_handshake, expected)
            )
        missing.extend(online_stage_failures(online_stage, expected))
        if live_disarm is not None:
            missing.append(f"live_disarmed:{live_disarm.get('reason', 'unknown')}")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase in {
        "menu-ready",
        "player-match-nav",
        "player-match-lobby",
        "player-match-battle",
    }:
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        if phase in {"player-match-lobby", "player-match-battle"}:
            missing.extend(
                sidecar_failures(sidecar_bind, sidecar_handshake, expected)
            )
            if phase == "player-match-battle":
                missing.extend(online_stage_failures(online_stage, expected))
            else:
                missing.extend(
                    online_stage_gate_failures(online_stage, expected, phase)
                )
        else:
            missing.extend(
                online_stage_gate_failures(online_stage, expected, phase)
            )
        result.update(online_stage_gate_summary(online_stage, phase))
        if live_disarm is not None:
            missing.append(f"live_disarmed:{live_disarm.get('reason', 'unknown')}")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "observe-gameflow":
        missing = observe_gameflow_failures(
            configured,
            role_manifest,
            gameflow_observe,
            capture_gameflow=capture_gameflow,
        )
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "direct-stage":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        missing.extend(direct_stage_mode_failures(direct_stage, expected))
        if online_stage is not None:
            missing.append("direct_stage_unexpected_online_stage")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "direct-connect":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        missing.extend(direct_stage_mode_failures(direct_stage, expected))
        missing.extend(direct_connect_failures(direct_connect, expected))
        if online_stage is not None:
            missing.append("direct_connect_unexpected_online_stage")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "activation":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        missing.extend(live_traffic_failures(stock_online))
        missing.extend(activation_failures(activation_candidate))
        if live_disarm is not None:
            missing.append(f"live_disarmed:{live_disarm.get('reason', 'unknown')}")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "live-replay-input":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        missing.extend(live_traffic_failures(stock_online))
        missing.extend(activation_failures(activation_candidate))
        missing.extend(replay_input_failures(replay_input_script, expected))
        if live_disarm is not None:
            missing.append(f"live_disarmed:{live_disarm.get('reason', 'unknown')}")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "direct-replay-input":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        missing.extend(direct_stage_mode_failures(direct_stage, expected))
        missing.extend(direct_connect_failures(direct_connect, expected))
        missing.extend(direct_replay_input_failures(direct_replay_input, expected))
        if online_stage is not None:
            missing.append("direct_replay_input_unexpected_online_stage")
        direct_replay_ready = bool(
            direct_connect
            and direct_connect.get("ok")
            and direct_replay_input
            and direct_replay_input.get("ok")
        )
        if live_disarm is not None and not direct_replay_ready:
            missing.append(f"direct_disarmed:{live_disarm.get('reason', 'unknown')}")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "direct-correction":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        missing.extend(direct_stage_mode_failures(direct_stage, expected))
        missing.extend(direct_connect_failures(direct_connect, expected))
        missing.extend(direct_replay_input_failures(direct_replay_input, expected))
        missing.extend(direct_correction_failures(direct_correction, live_disarm))
        if online_stage is not None:
            missing.append("direct_correction_unexpected_online_stage")
        result["missing"] = missing
        result["ok"] = not missing
    elif phase in {"direct-release", "rollback-proof"}:
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(
            direct_release_failures(
                role_manifest,
                sidecar_bind,
                sidecar_handshake,
                online_stage,
                direct_stage,
                direct_connect,
                direct_replay_input,
                direct_correction,
                live_disarm,
                live_cache_write,
                live_correction,
                live_convergence,
                events,
                expected,
            )
        )
        result["direct_release_summary"] = {
            "setup_patch_events": len(
                events_named(events, "rollback_direct_stage_setup_patch")
            ),
            "native_launch_events": len(
                events_named(events, "rollback_direct_stage_native_launch")
            ),
            "battle_asset_request_events": len(
                events_named(events, "rollback_direct_stage_battle_asset_request")
            ),
            "battle_scene_events": len(
                events_named(events, "rollback_direct_stage_battle_scene")
            ),
            "direct_connect_events": len(
                events_named(events, "rollback_direct_connect")
            ),
            "direct_cache_refreshes": (
                int_value(direct_replay_input.get("direct_cache_refreshes"), 0)
                if direct_replay_input else 0
            ),
            "direct_correction_events": len(
                events_named(events, "rollback_direct_correction")
            ),
            "disarm_events": len(events_named(events, "rollback_live_disarm")),
        }
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "live-correction":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(role_manifest_failures(role_manifest, expected))
        missing.extend(sidecar_failures(sidecar_bind, sidecar_handshake, expected))
        missing.extend(live_traffic_failures(stock_online))
        missing.extend(activation_failures(activation_candidate))
        missing.extend(replay_input_failures(replay_input_script, expected))
        missing.extend(
            live_correction_failures(
                live_cache_write,
                live_correction,
                live_convergence,
                live_disarm,
            )
        )
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "soak":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        if str(expected.get("mode", "")) == "direct-connect":
            missing.extend(
                direct_soak_failures(
                    role_manifest,
                    sidecar_bind,
                    sidecar_handshake,
                    direct_stage,
                    direct_connect,
                    direct_replay_input,
                    live_disarm,
                    live_cache_write,
                    live_correction,
                    live_convergence,
                    direct_correction,
                    events,
                    expected,
                )
            )
        else:
            missing.extend(
                soak_failures(
                    role_manifest,
                    sidecar_bind,
                    sidecar_handshake,
                    stock_online,
                    activation_candidate,
                    live_disarm,
                    live_cache_write,
                    live_correction,
                    live_convergence,
                    events,
                    expected,
                )
            )
        result["soak_summary"] = {
            "live_online_events": len(
                events_named(events, "rollback_live_online_capture")
            ),
            "sidecar_bind_events": len(
                events_named(events, "rollback_sidecar_bind")
            ),
            "sidecar_handshake_events": len(
                events_named(events, "rollback_sidecar_handshake")
            ),
            "session_pointers": [
                f"0x{value:X}"
                for value in sorted(
                    nonzero_unique_hex_values(
                        events_named(events, "rollback_live_online_capture"),
                        "last_session_ptr",
                    )
                )
            ],
            "input_logs": [
                f"0x{value:X}"
                for value in sorted(
                    nonzero_unique_hex_values(
                        events_named(events, "rollback_live_online_capture"),
                        "last_input_log",
                    )
                )
            ],
            "unexpected_correction_events": sum(
                len(events_named(events, name))
                for name in (
                    "rollback_live_cache_write",
                    "rollback_live_correction",
                    "rollback_live_convergence",
                    "rollback_direct_correction",
                )
            ),
            "disarm_events": len(events_named(events, "rollback_live_disarm")),
        }
        result["missing"] = missing
        result["ok"] = not missing
    elif phase == "sidecar-fault-closed":
        missing = configured_event_failures(configured, "live-online-capture")
        missing.extend(result["event_pid_mapping_failures"])
        missing.extend(result["current_event_pid_failures"])
        missing.extend(
            sidecar_fault_closed_failures(
                role_manifest,
                sidecar_bind,
                sidecar_handshake,
                live_disarm,
                live_cache_write,
                live_correction,
                live_convergence,
            )
        )
        result["missing"] = missing
        result["ok"] = not missing
    else:
        result["ok"] = True

    config_ack = configured_request_acknowledgement(
        configured,
        request_id=request_id,
        protocol_version=int_value(
            expected.get("request_protocol_version"),
            RUNNER_REQUEST_PROTOCOL_VERSION,
        ),
        generation=int_value(expected.get("request_generation"), 0),
        phase=str(expected.get("request_phase") or phase),
    )
    result["config_ack"] = config_ack
    for failure in config_ack["failures"]:
        if failure not in result["missing"]:
            result["missing"].append(failure)
    role = str(root.get("role") or "")
    result["navigation_diagnostics"] = navigation_diagnostics(
        online_stage, ui_input_event
    )
    result["milestones"] = navigation_milestones(
        role, online_stage, ui_input_event, phase
    )
    terminal = result["navigation_diagnostics"].get("terminal_failure")
    if phase in {
        "player-match-nav",
        "player-match-lobby",
        "player-match-battle",
        "online-stage",
        "direct-release",
    } and terminal:
        marker = f"terminal_navigation_failure:{terminal.get('code')}"
        if marker not in result["missing"]:
            result["missing"].append(marker)
    result["ok"] = not result["missing"]

    return result


def assign_request_ids(
    roots: list[dict[str, Any]],
    run_id: str,
    *,
    protocol_version: int = RUNNER_REQUEST_PROTOCOL_VERSION,
    generation: int = 0,
) -> dict[str, str]:
    sandbox_idx = 0
    out: dict[str, str] = {}
    for root in roots:
        if root["role"] == "host":
            suffix = "host"
        else:
            sandbox_idx += 1
            suffix = "sandbox" if sandbox_idx == 1 else f"sandbox{sandbox_idx}"
        out[root["path"]] = (
            f"two-client-v{protocol_version}-g{generation:016x}-"
            f"{run_id}-{suffix}"
        )
    return out


def phase_request_case(phase: str, mode: str = "") -> tuple[bool, str]:
    if phase in {"identity", "role-manifest"}:
        return False, "baseline-oracle"
    if mode == "mirrored-versus" and phase in MIRRORED_VERSUS_PHASES:
        return True, "production"
    if phase == "horse-udp":
        return True, "gekko-udp"
    if phase == "observe-gameflow":
        return True, "baseline-oracle"
    if phase in {
        "sidecar",
        "online-stage",
        "menu-ready",
        "player-match-nav",
        "player-match-lobby",
        "player-match-battle",
        "direct-stage",
        "direct-connect",
        "direct-replay-input",
        "direct-correction",
        "direct-release",
        "rollback-proof",
        "stock-online",
        "activation",
        "live-replay-input",
        "live-correction",
        "soak",
        "sidecar-fault-closed",
    }:
        return True, "live-online-capture"
    return False, "baseline-oracle"


def online_stage_empty_find_fail_fast_ready(
    results: list[dict[str, Any]],
    *,
    min_empty_attempts: int,
) -> bool:
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host_stage = (by_role.get("host") or {}).get("online_stage") or {}
    sandbox_stage = (by_role.get("sandbox") or {}).get("online_stage") or {}
    if not host_stage or not sandbox_stage:
        return False
    host_created = (
        bool(host_stage.get("host_create_request_ok"))
        and bool(host_stage.get("create_callback_seen"))
        and bool(host_stage.get("create_callback_result"))
    )
    standard_find_attempts = int_value(
        sandbox_stage.get("client_find_attempts"),
        int_value(sandbox_stage.get("attempts"), 0),
    )
    steam_probe_enabled = bool(sandbox_stage.get("debug_steam_probe"))
    steam_target_visible = bool(
        sandbox_stage.get("steam_lobby_probe_target_visible")
    )
    sandbox_standard_empty_find = (
        bool(sandbox_stage.get("client_find_request_ok"))
        and bool(sandbox_stage.get("find_callback_seen"))
        and bool(sandbox_stage.get("find_callback_result"))
        and int_value(sandbox_stage.get("find_result_count"), -1) <= 0
        and standard_find_attempts >= min_empty_attempts
    )
    sandbox_standard_failed_find = (
        bool(sandbox_stage.get("client_find_request_ok"))
        and bool(sandbox_stage.get("find_callback_seen"))
        and not bool(sandbox_stage.get("find_callback_result"))
        and standard_find_attempts >= 1
    )
    sandbox_standard_stalled_visible_find = (
        steam_probe_enabled
        and steam_target_visible
        and bool(sandbox_stage.get("client_find_request_ok"))
        and standard_find_attempts >= 1
        and not bool(sandbox_stage.get("find_callback_seen"))
    )
    native_empty_attempts = max(
        int_value(sandbox_stage.get("native_no_presence_find_empty_attempts"), 0),
        int_value(sandbox_stage.get("native_no_presence_find_attempts"), 0),
    )
    sandbox_no_presence_empty_find = (
        bool(sandbox_stage.get("native_no_presence_find_requested"))
        and bool(sandbox_stage.get("native_no_presence_find_call_ok"))
        and bool(sandbox_stage.get("native_no_presence_find_polled"))
        and int_value(
            sandbox_stage.get("native_no_presence_find_result_count"), -1
        ) <= 0
        and native_empty_attempts >= min_empty_attempts
    )
    no_presence_path_active = (
        bool(sandbox_stage.get("native_no_presence_find_requested"))
        or bool(sandbox_stage.get("native_no_presence_find_call_ok"))
    )
    no_presence_path_configured = (
        bool(sandbox_stage.get("online_stage_no_presence_find"))
        or bool(sandbox_stage.get("debug_steam_filter_probe"))
    )
    sandbox_empty_find = (
        sandbox_no_presence_empty_find
        if no_presence_path_active
        else (False if no_presence_path_configured else (
            sandbox_standard_empty_find
            or sandbox_standard_failed_find
            or sandbox_standard_stalled_visible_find
        ))
    )
    return host_created and sandbox_empty_find and (
        not steam_probe_enabled or steam_target_visible
    )


def online_stage_bad_session_slot_fail_fast_ready(
    results: list[dict[str, Any]],
) -> bool:
    for result in results:
        stage = result.get("online_stage") or {}
        if not bool(stage.get("online_session_probe_attempted")):
            continue
        slot_kind = str(stage.get("online_session_find_sessions_slot_kind", ""))
        if slot_kind and slot_kind != "steam":
            return True
    return False


def direct_release_online_search_blocker_fail_fast_ready(
    results: list[dict[str, Any]],
) -> bool:
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host_stage = (by_role.get("host") or {}).get("online_stage") or {}
    sandbox_stage = (by_role.get("sandbox") or {}).get("online_stage") or {}
    if not host_stage or not sandbox_stage:
        return False
    host_created = (
        bool(host_stage.get("host_create_request_ok"))
        and bool(host_stage.get("create_callback_seen"))
        and bool(host_stage.get("create_callback_result"))
    )
    if not host_created:
        return False
    if bool(sandbox_stage.get("client_join_request_ok")):
        return False
    host_steam_visible = (
        bool(host_stage.get("steam_lobby_probe_completed"))
        and bool(host_stage.get("steam_lobby_probe_target_visible"))
    )
    sandbox_steam_visible = (
        bool(sandbox_stage.get("steam_lobby_probe_completed"))
        and bool(sandbox_stage.get("steam_lobby_probe_target_visible"))
    )
    if not (host_steam_visible or sandbox_steam_visible):
        return False
    standard_find_attempts = int_value(
        sandbox_stage.get("client_find_attempts"),
        int_value(sandbox_stage.get("attempts"), 0),
    )
    if standard_find_attempts < 1:
        return False
    standard_find_failed = (
        bool(sandbox_stage.get("client_find_request_ok"))
        and bool(sandbox_stage.get("find_callback_seen"))
        and not bool(sandbox_stage.get("find_callback_result"))
    )
    standard_find_empty = (
        bool(sandbox_stage.get("client_find_request_ok"))
        and bool(sandbox_stage.get("find_callback_seen"))
        and bool(sandbox_stage.get("find_callback_result"))
        and int_value(sandbox_stage.get("find_result_count"), -1) <= 0
    )
    no_presence_find_empty = (
        bool(sandbox_stage.get("native_no_presence_find_requested"))
        and bool(sandbox_stage.get("native_no_presence_find_polled"))
        and int_value(
            sandbox_stage.get("native_no_presence_find_result_count"), -1
        ) <= 0
    )
    return standard_find_failed or standard_find_empty or no_presence_find_empty


def online_stage_navigation_fail_fast_ready(
    results: list[dict[str, Any]],
    *,
    min_nav_attempts: int = 600,
    min_player_match_attempts: int = 0,
) -> bool:
    if not results:
        return False
    terminal_navigation_blockers = 0
    for result in results:
        stage = result.get("online_stage") or {}
        if not stage:
            return False
        if str(stage.get("failure", "")) != "player-match-scene-not-ready":
            return False
        if bool(stage.get("host_create_request_ok")):
            return False
        if int_value(stage.get("online_nav_attempts"), 0) < min_nav_attempts:
            return False
        if (
            int_value(stage.get("player_match_scene_request_attempts"), 0)
            < min_player_match_attempts
        ):
            return False
        current = (
            str(stage.get("current_scene_class", ""))
            + " "
            + str(stage.get("current_scene_name", ""))
        )
        if "PlayerMatchLobbyScene" in current:
            return False
        terminal_navigation_blockers += 1
    return terminal_navigation_blockers == len(results)


def terminal_navigation_fail_fast_ready(
    results: list[dict[str, Any]],
) -> bool:
    """Stop as soon as one client proves its configured route cannot progress."""
    return any(
        bool(
            (result.get("navigation_diagnostics") or {}).get(
                "terminal_failure"
            )
        )
        for result in results
    )


def online_stage_host_create_callback_fail_fast_ready(
    results: list[dict[str, Any]],
    *,
    min_nav_attempts: int = 240,
) -> bool:
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    host_stage = (by_role.get("host") or {}).get("online_stage") or {}
    if not host_stage:
        return False
    if not bool(host_stage.get("host_create_request_ok")):
        return False
    if bool(host_stage.get("create_callback_seen")):
        return not bool(host_stage.get("create_callback_result"))
    return False


def direct_stage_spawned_without_chara_fail_fast_ready(
    results: list[dict[str, Any]],
    *,
    min_attempts: int = 600,
) -> bool:
    if not results:
        return False
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    for role in ("host", "sandbox"):
        stage = (by_role.get(role) or {}).get("direct_stage") or {}
        if not stage:
            return False
        if bool(stage.get("ok")):
            return False
        if int_value(stage.get("attempts"), 0) < min_attempts:
            return False
        if not bool(stage.get("battle_manager_spawn_ok")):
            return False
        if bool(stage.get("battle_context_ready")):
            return False
        if not bool(stage.get("battle_manager_object_real")):
            return False
        if (
            bool(stage.get("chara_p1_context_live"))
            or bool(stage.get("chara_p2_context_live"))
            or bool(stage.get("chara_p1_object_real"))
            or bool(stage.get("chara_p2_object_real"))
            or bool(stage.get("chara_p1_native_live"))
            or bool(stage.get("chara_p2_native_live"))
            or bool(stage.get("chara_p1_static_live"))
            or bool(stage.get("chara_p2_static_live"))
        ):
            return False
        if str(stage.get("battle_context_failure", "")) != (
            "chara-global-not-live"
        ):
            return False
        if bool(stage.get("debug_direct_stage_begin_play")):
            return False
    return True


def direct_stage_battle_scene_no_queue_fail_fast_ready(
    results: list[dict[str, Any]],
    *,
    min_attempts: int = 12,
) -> bool:
    if not results:
        return False
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    for role in ("host", "sandbox"):
        stage = (by_role.get(role) or {}).get("direct_stage") or {}
        if not stage:
            return False
        if bool(stage.get("ok")) or bool(stage.get("battle_context_ready")):
            return False
        if int_value(stage.get("attempts"), 0) < min_attempts:
            return False
        if not bool(stage.get("battle_scene_requested")):
            return False
        if bool(stage.get("battle_scene_ready")):
            return False
        if "BattleSetup" not in str(stage.get("current_scene_class", "")):
            return False
        if str(stage.get("next_scene_class", "")):
            return False
        reason = str(
            stage.get("battle_scene_reason")
            or stage.get("failure")
            or ""
        )
        if (
            "did not queue battle NextScene" not in reason
            and "failed to queue battle NextScene" not in reason
            and "CurrentSceneStopComplete" not in reason
            and "emulated gameflow" not in reason
        ):
            return False
    return True


def direct_stage_title_navigation_fail_fast_ready(
    results: list[dict[str, Any]],
    *,
    min_attempts: int = 420,
) -> bool:
    if not results:
        return False
    by_role = {
        str(result.get("root", {}).get("role", "")): result
        for result in results
    }
    stalled = False
    for role in ("host", "sandbox"):
        stage = (by_role.get(role) or {}).get("direct_stage") or {}
        if not stage:
            return False
        if bool(stage.get("ok")) or bool(stage.get("battle_context_ready")):
            continue
        if int_value(stage.get("attempts"), 0) < min_attempts:
            return False
        current = (
            str(stage.get("current_scene_class", ""))
            + " "
            + str(stage.get("current_scene_name", ""))
        )
        queued = (
            str(stage.get("next_scene_class", ""))
            + " "
            + str(stage.get("next_scene_name", ""))
        ).strip()
        early_scene = (
            "Title" in current
            or "AdvertiseScene" in current
            or "InitScene" in current
        )
        if early_scene and not queued:
            reason = str(
                stage.get("navigation_reason")
                or stage.get("failure")
                or ""
            )
            if (
                "no scene transition observed" in reason
                or "EmulateTitleDecide" in reason
                or "GameInitialize" in reason
                or "TitleToMainMenu" in reason
            ):
                stalled = True
    return stalled


def phase_has_real_process_failure(results: list[dict[str, Any]]) -> bool:
    return any(
        any(
            str(failure).startswith("sc6_process_exited")
            or str(failure).startswith("event_pid_exited")
            or str(failure).startswith("sc6_crash_dialog")
            or str(failure).startswith("process_query_unavailable")
            for failure in result.get("current_event_pid_failures", [])
        )
        for result in results
    )


def phase_has_process_failure(results: list[dict[str, Any]]) -> bool:
    return any(
        any(
            str(failure).startswith("sc6_process_exited")
            or str(failure).startswith("event_pid_exited")
            or str(failure).startswith("sc6_crash_dialog")
            or str(failure).startswith("sc6_hung_window")
            or str(failure).startswith("process_query_unavailable")
            for failure in result.get("current_event_pid_failures", [])
        )
        for result in results
    )


def direct_release_fail_fast_ready(results: list[dict[str, Any]]) -> bool:
    return (
        direct_release_online_search_blocker_fail_fast_ready(results)
        or online_stage_navigation_fail_fast_ready(
            results,
            min_nav_attempts=150,
            min_player_match_attempts=8,
        )
    )


def wait_for_phase(
    phase: str,
    roots: list[dict[str, Any]],
    request_ids: dict[str, str],
    expected_roles: dict[str, dict[str, Any]],
    watch_seconds: float,
    *,
    min_mtime: float,
    allowed_pids: set[int],
    trace_offsets: dict[str, dict[str, int]],
    capture_gameflow: bool = False,
    fail_fast_online_stage_empty_finds: bool = False,
    fail_fast_empty_find_attempts: int = 3,
) -> list[dict[str, Any]]:
    deadline = time.time() + max(0.1, watch_seconds)
    latest: list[dict[str, Any]] = []
    hung_window_counts: dict[int, int] = {}
    process_missing_counts = {pid: 0 for pid in allowed_pids}
    process_query_unavailable_count = 0
    scene_stability_trackers: dict[str, dict[str, Any]] = {}
    while time.time() < deadline:
        endpoints = enumerate_udp_endpoints()
        process_query = query_current_sc6_processes(allowed_pids)
        if process_query.get("valid"):
            process_query_unavailable_count = 0
        else:
            process_query_unavailable_count += 1
        process_query["unavailable_consecutive"] = (
            process_query_unavailable_count
        )
        live_sc6_pids = update_process_presence(
            process_query,
            allowed_pids,
            process_missing_counts,
        )
        raw_crash_indicators = enumerate_crash_indicators(allowed_pids)
        hung_seen: set[int] = {
            int_value(indicator.get("pid"), -1)
            for indicator in raw_crash_indicators
            if str(indicator.get("source") or "") == "hung-window"
        }
        for pid in list(hung_window_counts):
            if pid not in hung_seen:
                hung_window_counts.pop(pid, None)
        for pid in hung_seen:
            if pid >= 0:
                hung_window_counts[pid] = hung_window_counts.get(pid, 0) + 1
        crash_indicators = [
            indicator
            for indicator in raw_crash_indicators
            if str(indicator.get("source") or "") != "hung-window"
            or hung_window_counts.get(
                int_value(indicator.get("pid"), -1),
                0,
            ) >= HUNG_WINDOW_FAIL_POLLS
        ]
        latest = [
            client_result(
                root,
                request_ids[root["path"]],
                phase,
                endpoints,
                expected_roles[root["path"]],
                min_mtime=min_mtime,
                allowed_pids=allowed_pids,
                current_pids=live_sc6_pids,
                crash_indicators=crash_indicators,
                process_query=process_query,
                file_offsets=trace_offsets.get(root["path"], {}),
                capture_gameflow=capture_gameflow,
            )
            for root in roots
        ]
        apply_active_scene_stability(
            latest, phase, scene_stability_trackers
        )
        if (
            phase == "direct-release"
            and fail_fast_online_stage_empty_finds
        ):
            fail_fast_latest = latest
            if any(
                str(indicator.get("source") or "") == "hung-window"
                for indicator in crash_indicators
            ):
                non_hung_crash_indicators = [
                    indicator
                    for indicator in crash_indicators
                    if str(indicator.get("source") or "") != "hung-window"
                ]
                fail_fast_latest = [
                    client_result(
                        root,
                        request_ids[root["path"]],
                        phase,
                        endpoints,
                        expected_roles[root["path"]],
                        min_mtime=min_mtime,
                        allowed_pids=allowed_pids,
                        current_pids=live_sc6_pids,
                        crash_indicators=non_hung_crash_indicators,
                        process_query=process_query,
                        file_offsets=trace_offsets.get(root["path"], {}),
                    )
                    for root in roots
                ]
            if (
                not phase_has_real_process_failure(fail_fast_latest)
                and direct_release_fail_fast_ready(fail_fast_latest)
            ):
                return fail_fast_latest
        process_failed = (
            phase_has_real_process_failure(latest)
            if phase == "direct-release"
            else phase_has_process_failure(latest)
        )
        if latest and process_failed:
            return latest
        if phase in {
            "player-match-nav",
            "player-match-lobby",
            "player-match-battle",
            "online-stage",
            "direct-release",
        } and terminal_navigation_fail_fast_ready(latest):
            return latest
        pair_failures = (
            mirrored_versus_pair_failures(latest, phase)
            if (
                expected_roles
                and all(
                    str(expected.get("mode", "")) == "mirrored-versus"
                    for expected in expected_roles.values()
                )
                and phase in MIRRORED_VERSUS_PHASES
            )
            else live_correction_pair_failures(latest)
            if phase == "live-correction"
            else direct_correction_pair_failures(latest)
            if phase in {"direct-correction", "direct-release"}
            else online_stage_membership_pair_failures(latest)
            if phase == "player-match-lobby"
            else []
        )
        if (
            phase not in {"soak", "direct-release"}
            and latest
            and all(r.get("ok") for r in latest)
            and not pair_failures
            and not (phase == "observe-gameflow" and capture_gameflow)
        ):
            return latest
        if (
            phase == "online-stage"
            and fail_fast_online_stage_empty_finds
            and (
                online_stage_bad_session_slot_fail_fast_ready(latest)
                or online_stage_navigation_fail_fast_ready(latest)
                or online_stage_host_create_callback_fail_fast_ready(latest)
                or online_stage_empty_find_fail_fast_ready(
                    latest,
                    min_empty_attempts=max(1, fail_fast_empty_find_attempts),
                )
            )
        ):
            return latest
        if (
            phase == "direct-release"
            and fail_fast_online_stage_empty_finds
            and direct_release_fail_fast_ready(latest)
        ):
            return latest
        if (
            phase == "direct-stage"
            and (
                direct_stage_spawned_without_chara_fail_fast_ready(latest)
                or direct_stage_battle_scene_no_queue_fail_fast_ready(latest)
                or direct_stage_title_navigation_fail_fast_ready(latest)
            )
        ):
            return latest
        time.sleep(RUNNER_PHASE_POLL_SECONDS)
    endpoints = enumerate_udp_endpoints()
    process_query = query_current_sc6_processes(allowed_pids)
    if process_query.get("valid"):
        process_query_unavailable_count = 0
    else:
        process_query_unavailable_count += 1
    process_query["unavailable_consecutive"] = process_query_unavailable_count
    live_sc6_pids = update_process_presence(
        process_query,
        allowed_pids,
        process_missing_counts,
    )
    raw_crash_indicators = enumerate_crash_indicators(allowed_pids)
    crash_indicators = [
        indicator
        for indicator in raw_crash_indicators
        if str(indicator.get("source") or "") != "hung-window"
        or hung_window_counts.get(
            int_value(indicator.get("pid"), -1),
            0,
        ) >= HUNG_WINDOW_FAIL_POLLS
    ]
    final_results = [
        client_result(
            root,
            request_ids[root["path"]],
            phase,
            endpoints,
            expected_roles[root["path"]],
            min_mtime=min_mtime,
            allowed_pids=allowed_pids,
            current_pids=live_sc6_pids,
            crash_indicators=crash_indicators,
            process_query=process_query,
            file_offsets=trace_offsets.get(root["path"], {}),
            capture_gameflow=capture_gameflow,
        )
        for root in roots
    ]
    apply_active_scene_stability(
        final_results, phase, scene_stability_trackers
    )
    return final_results


def write_report(report: dict[str, Any], output: Path | None) -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    path = output or REPORT_DIR / f"rollback_two_client_{report['run_id']}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(report, indent=2, sort_keys=True),
        encoding="utf-8",
        newline="\n",
    )
    return path


def write_observe_timeline(report: dict[str, Any]) -> Path | None:
    if report.get("phase") != "observe-gameflow":
        return None
    OBSERVE_REPORT_DIR.mkdir(parents=True, exist_ok=True)
    path = OBSERVE_REPORT_DIR / f"rollback_gameflow_observe_{report['run_id']}.md"
    lines = [
        f"# Rollback Gameflow Observe {report.get('run_id', '')}",
        "",
        f"- ok: {bool(report.get('ok'))}",
        f"- generated_at: {report.get('generated_at', '')}",
        f"- sandbox_query_port: {report.get('sandbox_query_port', '')}",
        "",
    ]
    for result in report.get("results", []):
        if not isinstance(result, dict):
            continue
        root = result.get("root") or {}
        role = root.get("role", "")
        latest = result.get("gameflow_observe") or {}
        lines.extend([
            f"## {role}",
            "",
            (
                "- latest: "
                f"battle_scene={bool(latest.get('battle_scene'))} "
                f"current={latest.get('current_scene_class')}/"
                f"{latest.get('current_scene_name')} "
                f"next={latest.get('next_scene_class')}/"
                f"{latest.get('next_scene_name')}"
            ),
            (
                "- counts: "
                f"observe={result.get('gameflow_observe_event_count', 0)} "
                f"process={result.get('gameflow_process_event_count', 0)} "
                f"overflow={result.get('gameflow_process_event_overflow_count', 0)} "
                f"reflection={result.get('gameflow_scene_reflection_count', 0)}"
            ),
            (
                "- setup: "
                f"seen={bool(result.get('gameflow_setup_scene_seen'))} "
                f"player_match_scene={bool(result.get('gameflow_player_match_scene_seen'))} "
                "progressed="
                f"{bool(result.get('gameflow_setup_progressed_to_player_match_scene'))}"
            ),
            "",
            "### Scene Samples",
            "",
        ])
        for event in result.get("gameflow_observe_events", [])[-40:]:
            lines.append(
                "- "
                f"tick={event.get('tick', '-')} "
                f"battle={event.get('battle_scene', False)} "
                f"current={event.get('current_scene_class', '')}/"
                f"{event.get('current_scene_name', '')} "
                f"next={event.get('next_scene_class', '')}/"
                f"{event.get('next_scene_name', '')}"
            )
        lines.extend(["", "### Process Events", ""])
        for event in result.get("gameflow_process_events", [])[-120:]:
            summary = str(event.get("params_summary", ""))
            if len(summary) > 220:
                summary = summary[:217] + "..."
            lines.append(
                "- "
                f"#{event.get('index', '-')} "
                f"{event.get('context_class', '')}/"
                f"{event.get('context_name', '')}."
                f"{event.get('function', '')} "
                f"{summary}"
            )
        lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Internal phase worker for rollback_two_client_acceptance_run.py. "
            "Use the acceptance runner for normal rollback test runs."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--phase",
        choices=[
            "inventory",
            "identity",
            "role-manifest",
            "horse-udp",
            "horse-udp-ready",
            "mirrored-versus-setup",
            "mirrored-versus-battle",
            "sidecar",
            "observe-gameflow",
            "online-stage",
            "menu-ready",
            "player-match-nav",
            "player-match-lobby",
            "player-match-battle",
            "direct-stage",
            "direct-connect",
            "direct-replay-input",
            "direct-correction",
            "direct-release",
            "rollback-proof",
            "stock-online",
            "activation",
            "live-replay-input",
            "live-correction",
            "soak",
            "sidecar-fault-closed",
        ],
        default="inventory",
    )
    parser.add_argument("--watch-seconds", type=float, default=30.0)
    parser.add_argument(
        "--mode",
        choices=["direct-connect", "mirrored-versus", "steam-online"],
        default="direct-connect",
    )
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--require-two-sc6", action="store_true")
    parser.add_argument("--sandbox-root", type=Path, default=Path(r"C:\Sandbox"))
    parser.add_argument(
        "--sandbox-box",
        default=DEFAULT_SANDBOX_BOX,
        help=(
            "Expected Sandboxie user\\box under --sandbox-root. "
            "Use an empty string to accept all boxes."
        ),
    )
    parser.add_argument("--rollback-window", type=int, default=12)
    parser.add_argument("--seed", default="0x5C6B0001")
    parser.add_argument("--left-character", default=None)
    parser.add_argument("--right-character", default=None)
    parser.add_argument("--stage", default=None)
    parser.add_argument("--host-sidecar-port", type=int, default=HOST_HORSE_UDP_PORT)
    parser.add_argument(
        "--sandbox-sidecar-port", type=int, default=SANDBOX_HORSE_UDP_PORT)
    parser.add_argument("--activation-token", default="")
    parser.add_argument(
        "--native-input-source-slot",
        type=int,
        default=0,
        choices=[0, 1],
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
    parser.add_argument("--debug-steam-probe", action="store_true")
    parser.add_argument(
        "--debug-steam-filter-probe",
        action="store_true",
        help=(
            "Experimental online-stage diagnostic for Steam matchmaking "
            "filter vtable calls; kept separate from --debug-steam-probe."
        ),
    )
    parser.add_argument(
        "--debug-direct-stage-begin-play",
        action="store_true",
        help=(
            "Experimental direct-stage diagnostic: call the native "
            "ALuxBattleManager BeginPlay spawn pipeline after "
            "SpawnBattleManager. Disabled by default because it can crash "
            "SC6 when prerequisites are missing."
        ),
    )
    parser.add_argument("--online-stage-native-session-name", default="")
    parser.add_argument("--online-stage-session-name", default="")
    parser.add_argument("--online-stage-room-name", default="")
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
        "--online-stage-invite-target-id",
        type=lambda value: int(value, 0),
        default=0,
        help=(
            "Diagnostic only: after the host creates a native Steam lobby, "
            "send one InviteUserToLobby call to this SteamID. This does not "
            "satisfy invite-free acceptance gates."
        ),
    )
    parser.add_argument(
        "--online-stage-join-lobby-id",
        type=lambda value: int(value, 0),
        default=0,
        help=(
            "Diagnostic only: make the sandbox Steam client call JoinLobby "
            "for this known lobby ID and verify LobbyEnter."
        ),
    )
    parser.add_argument(
        "--stock-join-route",
        choices=["browser", "invite-fallback"],
        default="browser",
        help=(
            "Enable the authenticated invite fallback after both native "
            "discovery retry budgets are exhausted."
        ),
    )
    parser.add_argument(
        "--online-stage-main-user-id-override",
        type=int,
        default=None,
        help=(
            "Diagnostic override applied to both clients; -1/default keeps "
            "the SC6-reported main user id."
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
    parser.add_argument("--online-stage-cleanup-only", action="store_true")
    parser.add_argument("--online-stage-find-only", action="store_true")
    parser.add_argument("--online-stage-no-presence-find", action="store_true")
    parser.add_argument(
        "--online-stage-goal",
        choices=[
            "main-menu",
            "player-match-nav",
            "player-match-lobby",
            "player-match-battle",
            "proof-only",
        ],
        default="player-match-battle",
        help=(
            "Acceptance layer requested from the C++ online-stage harness. "
            "Reflection diagnostics stay separate from release navigation."
        ),
    )
    parser.add_argument(
        "--online-stage-diagnostic-reflection",
        action="store_true",
        help=(
            "Enable fragile reflected menu/scene commands for diagnostics. "
            "Disabled by default for release-path gates."
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
            "Luxor join-complete handler and record active-connect state. "
            "This is not native-only online evidence."
        ),
    )
    parser.add_argument(
        "--online-stage-transport-ready-compat",
        action="store_true",
        help=(
            "Focused local-two-client compatibility: after the sandbox "
            "join succeeds but SC6 reports session-connect false, call the "
            "native Luxor transport mark-ready helper and record before/after "
            "state. This is not native-only online evidence."
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
            "has a nonreplacement peer tag, send the packet with that peer "
            "tag and report the correction."
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
            "ULuxorSessionHub.JoinSession directly with the selected find "
            "result. Disabled by default because the native MVP should mirror "
            "the manual menu flow."
        ),
    )
    parser.add_argument(
        "--online-stage-fail-fast-empty-finds",
        action="store_true",
        help=(
            "During online-stage/direct-release diagnostics, stop early on "
            "known terminal online-stage blockers: repeated empty SC6 find "
            "results, bad session slots, or Player Match navigation stuck "
            "outside PlayerMatchLobbyScene. With --debug-steam-probe the empty "
            "find path also requires Steam to see the target lobby."
        ),
    )
    parser.add_argument(
        "--online-stage-fail-fast-empty-find-attempts",
        type=int,
        default=3,
    )
    parser.add_argument(
        "--skip-online-stage-drive",
        action="store_true",
        help=(
            "Manual-attach diagnostic: do not drive online menu/create/join "
            "during direct-release; validate the already-running match."
        ),
    )
    parser.add_argument(
        "--capture-gameflow",
        action="store_true",
        help=(
            "For observe-gameflow only: collect the native/manual scene and "
            "ProcessEvent timeline without requiring the clients to reach the "
            "online battle scene. This is diagnostic capture, not rollback "
            "acceptance."
        ),
    )
    parser.add_argument(
        "--observe-gameflow-process-events",
        dest="observe_gameflow_process_events",
        action="store_true",
        default=False,
        help=(
            "Opt-in diagnostic: for observe-gameflow, include the filtered "
            "ProcessEvent timeline."
        ),
    )
    parser.add_argument(
        "--no-observe-gameflow-process-events",
        dest="observe_gameflow_process_events",
        action="store_false",
        help=(
            "For observe-gameflow wait gates, sample scenes only and do not "
            "install/log the ProcessEvent observer."
        ),
    )
    parser.add_argument(
        "--replay-input-file",
        type=Path,
        default=DEFAULT_REPLAY_INPUT_FILE,
    )
    parser.add_argument(
        "--main-menu-player-match-route",
        default=DEFAULT_MAIN_MENU_PLAYER_MATCH_ROUTE,
        help=(
            "Comma-separated UIGameFlowAutomation input route from "
            "MainMenuScene_C to Player Match."
        ),
    )
    parser.add_argument("--replay-divergence-frame", type=int, default=120)
    parser.add_argument("--replay-divergence-window", type=int, default=12)
    parser.add_argument("--require-udp-safety", action="store_true")
    parser.add_argument("--run-id", default="")
    parser.add_argument("--report-output", type=Path)
    parser.add_argument("--cleanup-request-after", action="store_true")
    parser.add_argument("--internal-phase-worker", action="store_true",
                        help=argparse.SUPPRESS)
    args = parser.parse_args()

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
    if args.phase in {
        "horse-udp-ready",
        "mirrored-versus-setup",
        "mirrored-versus-battle",
    } and args.mode != "mirrored-versus":
        parser.error(f"phase {args.phase} requires --mode mirrored-versus")
    if args.mode == "mirrored-versus" and args.phase in MIRRORED_VERSUS_PHASES:
        if args.expected_build_id == 0 or args.expected_schema_id == 0:
            parser.error(
                "mirrored-versus runtime phases require nonzero "
                "--expected-build-id and --expected-schema-id"
            )
    if args.stock_join_route == "invite-fallback":
        if args.mode != "steam-online":
            parser.error("invite-fallback requires --mode steam-online")
        if args.expected_build_id == 0 or args.expected_schema_id == 0:
            parser.error(
                "invite-fallback requires nonzero build and schema ids"
            )
        args.online_stage_no_presence_find = True
    if args.debug_steam_filter_probe:
        args.debug_steam_probe = True

    if (not args.internal_phase_worker
            and os.environ.get("HORSE_ALLOW_DIRECT_ROLLBACK_PHASE_RUNNER") != "1"):
        print(
            "rollback_two_client_test_run.py is an internal phase worker. "
            "Run rollback tests with tools\\rollback_two_client_acceptance_run.py "
            "instead. Set HORSE_ALLOW_DIRECT_ROLLBACK_PHASE_RUNNER=1 only for "
            "deliberate low-level diagnostics.",
            file=sys.stderr,
        )
        return 2

    run_id = args.run_id or datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    mirrored_runtime_phase = (
        args.mode == "mirrored-versus"
        and args.phase in MIRRORED_VERSUS_PHASES
    )
    request_phase = (
        MIRRORED_VERSUS_RUNTIME_PHASE
        if mirrored_runtime_phase else args.phase
    )
    request_generation = fnv1a64(
        f"rollback-runner-v{RUNNER_REQUEST_PROTOCOL_VERSION}|"
        f"{run_id}|{request_phase}"
    )
    snapshots = [snapshot("before")]
    before = snapshots[-1]
    sc6_pids = set(before["sc6_pids"])
    roots = discover_roots(
        sc6_pids,
        args.sandbox_root,
        args.sandbox_box,
        before.get("processes", []),
    )
    request_ids = assign_request_ids(
        roots,
        run_id,
        protocol_version=RUNNER_REQUEST_PROTOCOL_VERSION,
        generation=request_generation,
    )
    activation_token = args.activation_token or f"rollback-two-client-{run_id}"
    online_stage_session_name = args.online_stage_session_name
    online_stage_room_name = args.online_stage_room_name
    online_stage_target_owner_id = args.online_stage_target_owner_id
    online_stage_host_room_ready_marker = host_room_ready_marker_name(run_id)
    host_online_stage_main_user_id_override = choose_role_override(
        args.online_stage_main_user_id_override,
        args.host_online_stage_main_user_id_override,
    )
    sandbox_online_stage_main_user_id_override = choose_role_override(
        args.online_stage_main_user_id_override,
        args.sandbox_online_stage_main_user_id_override,
    )
    expected_roles = {
        root["path"]: expected_role_config(
            root,
            sandbox_root=args.sandbox_root,
            sandbox_box=args.sandbox_box,
            host_sidecar_port=args.host_sidecar_port,
            sandbox_sidecar_port=args.sandbox_sidecar_port,
            activation_token=activation_token,
            host_online_stage_main_user_id_override=(
                host_online_stage_main_user_id_override
            ),
            sandbox_online_stage_main_user_id_override=(
                sandbox_online_stage_main_user_id_override
            ),
            online_stage_native_session_name=args.online_stage_native_session_name,
            online_stage_session_name=online_stage_session_name,
            online_stage_room_name=online_stage_room_name,
            online_stage_target_owner_id=online_stage_target_owner_id,
            online_stage_goal=args.online_stage_goal,
            skip_online_stage_drive=args.skip_online_stage_drive,
            replay_input_file=args.replay_input_file,
            replay_divergence_frame=args.replay_divergence_frame,
            replay_divergence_window=args.replay_divergence_window,
            mode=args.mode,
            native_input_source_slot=args.native_input_source_slot,
            input_delay=args.input_delay,
            network_profile=args.fault_profile,
            fault_seed=args.fault_seed,
            expected_build_id=args.expected_build_id,
            expected_schema_id=args.expected_schema_id,
            launch_left_character=(
                args.resolved_left_character.numeric_id
                if args.resolved_left_character else -1
            ),
            launch_right_character=(
                args.resolved_right_character.numeric_id
                if args.resolved_right_character else -1
            ),
            launch_stage=(
                args.resolved_stage.numeric_id
                if args.resolved_stage else -1
            ),
        )
        for root in roots
    }
    for root in roots:
        expected_roles[root["path"]].update(
            {
                "request_id": request_ids[root["path"]],
                "request_protocol_version": RUNNER_REQUEST_PROTOCOL_VERSION,
                "request_generation": request_generation,
                "request_phase": request_phase,
            }
        )
    failures: list[str] = []
    pair_failures: list[str] = []
    results: list[dict[str, Any]] = []
    requests: list[dict[str, Any]] = []
    host_advertise_wait: dict[str, Any] | None = None
    cleanup_report: dict[str, Any] = {
        "attempted": False,
        "enforced": args.phase != "inventory",
        "ok": True,
        "records": [],
    }
    cleanup_state = {"done": False, "running": False}

    def cleanup_once(wait_seconds: float) -> dict[str, Any]:
        nonlocal cleanup_report
        if cleanup_state["done"] or args.phase == "inventory":
            return cleanup_report
        if cleanup_state["running"]:
            return cleanup_report
        cleanup_state["running"] = True
        try:
            cleanup_report = perform_phase_cleanup(
                roots,
                run_id=run_id,
                phase=args.phase,
                generation=request_generation,
                marker_name=online_stage_host_room_ready_marker,
                allowed_pids=sc6_pids,
                rollback_window=args.rollback_window,
                seed=args.seed,
                wait_seconds=wait_seconds,
            )
        except Exception as exc:
            fallback_records: list[dict[str, Any]] = []
            for root in roots:
                root_path = Path(root["path"])
                remove_request_file(root_path)
                remove_host_room_ready_marker(
                    root_path, online_stage_host_room_ready_marker
                )
                fallback_records.append(
                    {
                        "root": str(root_path),
                        "request_artifact_absent": not (
                            root_path / "rollback_lab_request.txt"
                        ).exists(),
                        "marker_removed": not host_room_ready_marker_path(
                            root_path, online_stage_host_room_ready_marker
                        ).exists(),
                    }
                )
            cleanup_report = {
                "attempted": True,
                "enforced": True,
                "ok": False,
                "artifacts_removed": all(
                    row["request_artifact_absent"] and row["marker_removed"]
                    for row in fallback_records
                ),
                "explicit_disable_error": repr(exc),
                "records": fallback_records,
            }
        finally:
            cleanup_state["running"] = False
            cleanup_state["done"] = True
        return cleanup_report

    def cleanup_at_exit() -> None:
        cleanup_once(2.0)

    if args.phase != "inventory":
        atexit.register(cleanup_at_exit)

    # Keep report generation total even when a non-inventory phase is skipped
    # by its inventory gate. The phase-specific value is refined below only
    # after that gate succeeds.
    phase_online_stage_goal = args.online_stage_goal
    inventory_ok = True
    if args.require_two_sc6 and len(before["sc6_pids"]) != 2:
        inventory_ok = False
        failures.append(f"expected exactly 2 SC6 PIDs, got {len(before['sc6_pids'])}")
    if len(roots) < 2:
        inventory_ok = False
        failures.append(f"expected at least 2 HorseMod Saved roots, got {len(roots)}")
    inventory_failures = validate_inventory(
        roots,
        sc6_pids,
        args.sandbox_box,
        require_trace_pids=False,
    )
    if inventory_failures:
        inventory_ok = False
        failures.extend(inventory_failures)
    launch_configuration_failures = validate_sc6_launch_configuration(
        before,
        roots,
    )
    if launch_configuration_failures and (args.require_two_sc6 or args.strict):
        inventory_ok = False
        failures.extend(launch_configuration_failures)
    udp_safety_failures = validate_udp_safety(
        before,
        host_sidecar_port=args.host_sidecar_port,
        sandbox_sidecar_port=args.sandbox_sidecar_port,
        allow_sc6_sidecar_owners=args.phase != "inventory",
    )
    if udp_safety_failures and (args.require_udp_safety or args.strict):
        inventory_ok = False
        failures.extend(udp_safety_failures)

    if args.phase != "inventory" and not inventory_ok:
        failures.append(f"skipped {args.phase}: inventory gate failed")
    elif args.phase != "inventory":
        enabled, case = phase_request_case(args.phase, args.mode)
        request_start_time = time.time()
        trace_offsets = {
            root["path"]: trace_file_offsets(Path(root["path"]))
            for root in roots
        }
        direct_mode = args.mode == "direct-connect"
        online_stage_goal_by_phase = {
            "menu-ready": "main-menu",
            "player-match-nav": "player-match-nav",
            "player-match-lobby": "player-match-lobby",
            "player-match-battle": "player-match-battle",
            "online-stage": args.online_stage_goal,
            "direct-release": args.online_stage_goal,
        }
        phase_online_stage_goal = online_stage_goal_by_phase.get(
            args.phase,
            args.online_stage_goal,
        )
        for expected in expected_roles.values():
            expected["online_stage_goal"] = phase_online_stage_goal
            if direct_mode and args.phase == "rollback-proof":
                expected["skip_online_stage_drive"] = True
                expected["direct_stage_observe_only"] = True
        sequence_host_room_ready = (
            args.online_stage_host_room_ready_gate
            and not args.online_stage_cleanup_only
            and (
                (
                    args.phase
                    in {
                        "online-stage",
                        "menu-ready",
                        "player-match-nav",
                        "player-match-lobby",
                        "player-match-battle",
                    }
                    and not direct_mode
                )
                or (
                    args.phase
                    in {
                        "direct-release",
                        "menu-ready",
                        "player-match-nav",
                        "player-match-lobby",
                        "player-match-battle",
                    }
                    and direct_mode
                )
            )
        )
        request_payloads: list[dict[str, Any]] = []
        for root in roots:
            root_path = Path(root["path"])
            remove_request_file(root_path)
            remove_host_room_ready_marker(
                root_path, online_stage_host_room_ready_marker)
            request_id = request_ids[root["path"]]
            phase_controlled_role_keys = {
                "activation_token_hash",
                "direct_stage_observe_only",
                "online_stage_goal",
                "request_id",
                "request_protocol_version",
                "request_generation",
                "request_phase",
            }
            request_role = {
                k: v for k, v in expected_roles[root["path"]].items()
                if k not in phase_controlled_role_keys
            }
            role = str(root.get("role", ""))
            request_observe_gameflow = args.phase == "observe-gameflow"
            if request_observe_gameflow:
                request_role.update(
                    {
                        "local_peer_id": 0,
                        "remote_peer_id": 0,
                        "sidecar_local_port": 0,
                        "sidecar_remote_port": 0,
                        "activation_token": "",
                    }
                )
            if args.phase == "sidecar-fault-closed":
                request_role["sidecar_remote_port"] = (
                    int_value(request_role.get("sidecar_remote_port"), 0)
                    + 2000
                )
            request_online_stage = (
                args.phase
                in {
                    "online-stage",
                    "menu-ready",
                    "player-match-nav",
                    "player-match-lobby",
                    "player-match-battle",
                }
                and not direct_mode
            ) or (
                direct_mode
                and args.phase
                in {
                    "direct-release",
                    "menu-ready",
                    "player-match-nav",
                    "player-match-lobby",
                    "player-match-battle",
                }
            )
            if args.skip_online_stage_drive and args.phase == "direct-release":
                request_online_stage = False
            request_direct_stage = direct_mode and args.phase in {
                "direct-stage",
                "direct-connect",
                "direct-replay-input",
                "direct-correction",
                "direct-release",
                "rollback-proof",
                "soak",
            }
            request_direct_connect = direct_mode and args.phase in {
                "direct-connect",
                "direct-replay-input",
                "direct-correction",
                "direct-release",
                "rollback-proof",
                "soak",
            }
            request_direct_replay_input = direct_mode and args.phase in {
                "direct-connect",
                "direct-replay-input",
                "direct-correction",
                "direct-release",
                "rollback-proof",
                "soak",
            }
            request_direct_correction = direct_mode and (
                args.phase in {"direct-correction", "direct-release", "rollback-proof"}
            )
            request_live_replay_input = args.phase in {
                "live-replay-input",
                "live-correction",
            }
            text = request_text(
                enabled=enabled,
                trace=True,
                case=case,
                request_id=request_id,
                request_protocol_version=RUNNER_REQUEST_PROTOCOL_VERSION,
                request_generation=request_generation,
                request_phase=request_phase,
                rollback_window=args.rollback_window,
                seed=args.seed,
                activation_arm=args.phase in {
                    "activation",
                    "live-replay-input",
                    "live-correction",
                    "sidecar-fault-closed",
                },
                force_live_prediction_divergence=args.phase in {
                    "live-correction",
                    "direct-correction",
                    "direct-release",
                    "rollback-proof",
                },
                debug_steam_probe=args.debug_steam_probe,
                debug_steam_filter_probe=args.debug_steam_filter_probe,
                debug_direct_stage_begin_play=(
                    args.debug_direct_stage_begin_play
                ),
                observe_gameflow=request_observe_gameflow,
                observe_gameflow_process_events=(
                    args.observe_gameflow_process_events
                ),
                online_stage_network_check_compat=(
                    args.online_stage_network_check_compat
                ),
                online_stage_join_complete_compat=(
                    args.online_stage_join_complete_compat
                ),
                online_stage_transport_ready_compat=(
                    args.online_stage_transport_ready_compat
                ),
                online_stage_ready_open_compat=(
                    args.online_stage_ready_open_compat
                ),
                online_stage_peer_route_tag_fix=(
                    args.online_stage_peer_route_tag_fix
                ),
                online_stage_in_room_transition_compat=(
                    args.online_stage_in_room_transition_compat
                ),
                online_stage_direct_native_join_diagnostic=(
                    args.online_stage_direct_native_join_diagnostic
                ),
                online_stage=request_online_stage,
                direct_stage=request_direct_stage,
                direct_stage_observe_only=(
                    (direct_mode and args.phase == "rollback-proof")
                    or (
                        args.skip_online_stage_drive
                        and args.phase == "direct-release"
                    )
                ),
                direct_connect=request_direct_connect,
                direct_replay_input=request_direct_replay_input,
                direct_correction=request_direct_correction,
                online_stage_no_presence_find=args.online_stage_no_presence_find,
                online_stage_cleanup_only=args.online_stage_cleanup_only,
                online_stage_find_only=args.online_stage_find_only,
                online_stage_wait_host_room_ready_marker=(
                    sequence_host_room_ready
                    and request_online_stage
                    and role != "host"
                ),
                online_stage_host_room_ready_marker=(
                    online_stage_host_room_ready_marker
                ),
                online_stage_goal=phase_online_stage_goal,
                online_stage_diagnostic_reflection=(
                    args.online_stage_diagnostic_reflection
                ),
                online_stage_invite_target_id=(
                    args.online_stage_invite_target_id
                    if role == "host" else 0
                ),
                online_stage_join_lobby_id=(
                    args.online_stage_join_lobby_id
                    if role == "sandbox" else 0
                ),
                stock_join_route=args.stock_join_route,
                main_menu_player_match_route=(
                    args.main_menu_player_match_route
                ),
                live_replay_input=request_live_replay_input,
                **request_role,
            )
            request_payloads.append(
                {
                    "root": root,
                    "root_path": root_path,
                    "text": text,
                    "record": {
                        "root": root["path"],
                        "request_path": "",
                        "request_id": request_id,
                        "request_protocol_version": (
                            RUNNER_REQUEST_PROTOCOL_VERSION
                        ),
                        "request_generation": request_generation,
                        "request_phase": request_phase,
                        "enabled": enabled,
                        "case": case,
                        "debug_steam_probe": args.debug_steam_probe,
                        "debug_steam_filter_probe": args.debug_steam_filter_probe,
                        "debug_direct_stage_begin_play": (
                            args.debug_direct_stage_begin_play
                        ),
                        "observe_gameflow": request_observe_gameflow,
                        "observe_gameflow_process_events": (
                            args.observe_gameflow_process_events
                        ),
                        "online_stage": request_online_stage,
                        "online_stage_join_complete_compat": (
                            args.online_stage_join_complete_compat
                        ),
                        "online_stage_no_presence_find": (
                            args.online_stage_no_presence_find
                        ),
                        "stock_join_route": args.stock_join_route,
                        "online_stage_transport_ready_compat": (
                            args.online_stage_transport_ready_compat
                        ),
                        "online_stage_ready_open_compat": (
                            args.online_stage_ready_open_compat
                        ),
                        "online_stage_peer_route_tag_fix": (
                            args.online_stage_peer_route_tag_fix
                        ),
                        "online_stage_in_room_transition_compat": (
                            args.online_stage_in_room_transition_compat
                        ),
                        "online_stage_direct_native_join_diagnostic": (
                            args.online_stage_direct_native_join_diagnostic
                        ),
                        "online_stage_cleanup_only": (
                            args.online_stage_cleanup_only
                        ),
                        "online_stage_find_only": args.online_stage_find_only,
                        "online_stage_wait_host_room_ready_marker": (
                            sequence_host_room_ready
                            and request_online_stage
                            and role != "host"
                        ),
                        "online_stage_host_room_ready_marker": (
                            online_stage_host_room_ready_marker
                        ),
                        "online_stage_goal": phase_online_stage_goal,
                        "online_stage_diagnostic_reflection": (
                            args.online_stage_diagnostic_reflection
                        ),
                        "main_menu_player_match_route": (
                            args.main_menu_player_match_route
                        ),
                        "online_stage_host_room_ready_marker_path": str(
                            host_room_ready_marker_path(
                                root_path,
                                online_stage_host_room_ready_marker,
                            )
                        ),
                        "online_stage_host_room_ready_marker_released": (
                            False
                        ),
                        "mode": args.mode,
                        "selection": {
                            "left": (
                                vars(args.resolved_left_character)
                                if args.resolved_left_character else None
                            ),
                            "right": (
                                vars(args.resolved_right_character)
                                if args.resolved_right_character else None
                            ),
                            "stage": (
                                vars(args.resolved_stage)
                                if args.resolved_stage else None
                            ),
                        },
                        "direct_stage": request_direct_stage,
                        "direct_stage_observe_only": (
                            (direct_mode and args.phase == "rollback-proof")
                            or (
                                args.skip_online_stage_drive
                                and args.phase == "direct-release"
                            )
                        ),
                        "direct_connect": request_direct_connect,
                        "direct_replay_input": request_direct_replay_input,
                        "direct_correction": request_direct_correction,
                        "online_stage_native_session_name": (
                            args.online_stage_native_session_name
                        ),
                        "online_stage_main_user_id_override": (
                            request_role.get(
                                "online_stage_main_user_id_override"
                            )
                        ),
                        "live_replay_input": request_live_replay_input,
                        "online_stage_session_name": online_stage_session_name,
                        "online_stage_room_name": online_stage_room_name,
                        "online_stage_target_owner_id": (
                            online_stage_target_owner_id
                        ),
                        "skip_online_stage_drive": args.skip_online_stage_drive,
                        "replay_input_file": str(args.replay_input_file),
                    },
                }
            )

        def write_payload(payload: dict[str, Any], *, gated: bool = False) -> None:
            request_path = write_request_file(
                payload["root_path"],
                str(payload["text"]),
            )
            record = dict(payload["record"])
            record["request_path"] = request_path
            record["host_advertise_gated"] = gated
            record["host_room_ready_gated"] = gated
            requests.append(record)

        if sequence_host_room_ready:
            host_payloads = [
                payload for payload in request_payloads
                if payload["root"].get("role") == "host"
            ]
            sandbox_payloads = [
                payload for payload in request_payloads
                if payload["root"].get("role") != "host"
            ]
            if len(host_payloads) == 1 and sandbox_payloads:
                host_payload = host_payloads[0]
                write_payload(host_payload, gated=False)
                for payload in sandbox_payloads:
                    write_payload(payload, gated=True)
                host_advertise_wait = (
                    wait_for_direct_release_host_advertisement(
                        root=host_payload["root"],
                        request_id=request_ids[host_payload["root"]["path"]],
                        expected=expected_roles[
                            host_payload["root"]["path"]
                        ],
                        min_mtime=request_start_time,
                        allowed_pids=sc6_pids,
                        file_offsets=trace_offsets.get(
                            host_payload["root"]["path"], {}
                        ),
                        require_steam_visible=args.debug_steam_probe,
                        timeout_seconds=(
                            DIRECT_RELEASE_HOST_ADVERTISE_TIMEOUT_SECONDS
                        ),
                        settle_seconds=(
                            DIRECT_RELEASE_HOST_ADVERTISE_SETTLE_SECONDS
                        ),
                    )
                )
                if host_advertise_wait.get("ok"):
                    for payload in sandbox_payloads:
                        marker_paths = write_host_room_ready_markers(
                            [
                                host_payload["root_path"],
                                payload["root_path"],
                            ],
                            online_stage_host_room_ready_marker,
                        )
                        for record in requests:
                            if (
                                record.get("root")
                                == payload["record"]["root"]
                            ):
                                record[
                                    "online_stage_host_room_ready_marker_released"
                                ] = True
                                record[
                                    "online_stage_host_room_ready_marker_path"
                                ] = ";".join(marker_paths)
                else:
                    for payload in sandbox_payloads:
                        for record in requests:
                            if (
                                record.get("root")
                                == payload["record"]["root"]
                            ):
                                record[
                                    "online_stage_host_room_ready_marker_released"
                                ] = False
            else:
                for payload in request_payloads:
                    record = dict(payload["record"])
                    record["host_advertise_gated"] = True
                    record["host_room_ready_gated"] = True
                    record["skipped_by_host_advertise_gate"] = True
                    record["skipped_by_host_room_ready_gate"] = True
                    requests.append(record)
                host_advertise_wait = {
                    "ok": False,
                    "failure": "invalid host/sandbox payload set for host room-ready gate",
                    "host_payload_count": len(host_payloads),
                    "sandbox_payload_count": len(sandbox_payloads),
                }
        else:
            for payload in request_payloads:
                write_payload(payload, gated=False)
        snapshots.append(snapshot("during"))
        if host_advertise_wait is not None and not host_advertise_wait.get("ok"):
            endpoints = enumerate_udp_endpoints()
            process_query = query_current_sc6_processes(sc6_pids)
            initial_missing_counts = {pid: 0 for pid in sc6_pids}
            live_sc6_pids = update_process_presence(
                process_query, sc6_pids, initial_missing_counts
            )
            crash_indicators = enumerate_crash_indicators(sc6_pids)
            results = [
                client_result(
                    root,
                    request_ids[root["path"]],
                    args.phase,
                    endpoints,
                    expected_roles[root["path"]],
                    min_mtime=request_start_time,
                    allowed_pids=sc6_pids,
                    current_pids=live_sc6_pids,
                    crash_indicators=crash_indicators,
                    process_query=process_query,
                    file_offsets=trace_offsets.get(root["path"], {}),
                    capture_gameflow=args.capture_gameflow,
                )
                for root in roots
            ]
        else:
            results = wait_for_phase(
                args.phase,
                roots,
                request_ids,
                expected_roles,
                args.watch_seconds,
                min_mtime=request_start_time,
                allowed_pids=sc6_pids,
                trace_offsets=trace_offsets,
                capture_gameflow=args.capture_gameflow,
                fail_fast_online_stage_empty_finds=(
                    args.online_stage_fail_fast_empty_finds
                ),
                fail_fast_empty_find_attempts=(
                    args.online_stage_fail_fast_empty_find_attempts
                ),
            )
        snapshots.append(snapshot("after"))
        host_gate_failed = (
            host_advertise_wait is not None
            and not host_advertise_wait.get("ok")
        )
        if host_gate_failed:
            ready = host_advertise_wait.get("ready_checks") or {}
            missing = ready.get("missing") or []
            detail = (
                ", ".join(str(item) for item in missing)
                if missing else str(host_advertise_wait.get("failure", "unknown"))
            )
            failures.append(f"host_room_ready_gate: {detail}")
        else:
            for result in results:
                if not result.get("ok"):
                    failures.append(
                        f"{result['root']['role']} {result['request_id']}: "
                        + ", ".join(result.get("missing") or ["unknown"])
                    )
            if args.phase == "live-correction":
                pair_failures = live_correction_pair_failures(results)
                failures.extend(pair_failures)
            if (
                args.mode == "mirrored-versus"
                and args.phase in MIRRORED_VERSUS_PHASES
            ):
                pair_failures = mirrored_versus_pair_failures(
                    results, args.phase
                )
                failures.extend(pair_failures)
            if args.phase in {"direct-correction", "direct-release"}:
                pair_failures = direct_correction_pair_failures(results)
                failures.extend(pair_failures)
            if args.mode == "direct-connect" and args.phase in {
                "direct-stage", "direct-connect", "direct-replay-input",
                "direct-correction", "direct-release", "rollback-proof",
            }:
                selection_pair_failures = direct_selection_pair_failures(
                    results
                )
                pair_failures.extend(selection_pair_failures)
                failures.extend(selection_pair_failures)
            if args.phase == "player-match-lobby":
                pair_failures = online_stage_membership_pair_failures(results)
                failures.extend(pair_failures)

    preserve_mirrored_session = (
        args.mode == "mirrored-versus"
        and args.phase in MIRRORED_VERSUS_PHASES
        and args.phase != "soak"
        and not args.cleanup_request_after
        and bool(results)
        and all(result.get("ok") for result in results)
        and not pair_failures
    )
    if args.phase != "inventory":
        if preserve_mirrored_session:
            atexit.unregister(cleanup_at_exit)
            cleanup_report = {
                "attempted": False,
                "enforced": False,
                "ok": True,
                "preserved_authenticated_session": True,
                "records": [],
            }
        else:
            cleanup_once(10.0)
            atexit.unregister(cleanup_at_exit)
            if not cleanup_report.get("ok"):
                failures.append(
                    "phase_cleanup: disable not acknowledged or artifacts remain"
                )

    phase_ok = inventory_ok
    if args.phase != "inventory":
        phase_ok = (
            inventory_ok
            and bool(results)
            and all(r.get("ok") for r in results)
            and not pair_failures
            and bool(cleanup_report.get("ok"))
        )
    packet_timeline = packet_timeline_from_results(results)
    packet_timeline_summary = summarize_packet_timeline_results(packet_timeline)

    report = {
        "ok": phase_ok,
        "phase": args.phase,
        "mode": args.mode,
        "stock_join_route": args.stock_join_route,
        "strict": args.strict,
        "require_two_sc6": args.require_two_sc6,
        "run_id": run_id,
        "request_protocol_version": RUNNER_REQUEST_PROTOCOL_VERSION,
        "request_generation": request_generation,
        "runner_phase_poll_seconds": RUNNER_PHASE_POLL_SECONDS,
        "generated_at": now_iso(),
        "repo": str(REPO),
        "game_exe": str(GAME_EXE),
        "sandbox_root": str(args.sandbox_root),
        "sandbox_box": args.sandbox_box,
        "steam_udp_port": STEAM_UDP_PORT,
        "host_sidecar_port": args.host_sidecar_port,
        "sandbox_sidecar_port": args.sandbox_sidecar_port,
        "lifecycle_mode": (
            "mirrored-versus"
            if args.mode == "mirrored-versus" else "stock-online-pvp"
        ),
        "native_input_source_slot": args.native_input_source_slot,
        "input_delay": args.input_delay,
        "expected_build_id": f"0x{args.expected_build_id:X}",
        "expected_schema_id": f"0x{args.expected_schema_id:X}",
        "activation_token_hash": fnv1a64(activation_token),
        "debug_steam_probe": args.debug_steam_probe,
        "debug_steam_filter_probe": args.debug_steam_filter_probe,
        "debug_direct_stage_begin_play": args.debug_direct_stage_begin_play,
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
        "online_stage_cleanup_only": args.online_stage_cleanup_only,
        "online_stage_find_only": args.online_stage_find_only,
        "online_stage_host_room_ready_gate": (
            args.online_stage_host_room_ready_gate
        ),
        "online_stage_native_session_name": args.online_stage_native_session_name,
        "host_online_stage_main_user_id_override": (
            host_online_stage_main_user_id_override
        ),
        "sandbox_online_stage_main_user_id_override": (
            sandbox_online_stage_main_user_id_override
        ),
        "online_stage_fail_fast_empty_finds": (
            args.online_stage_fail_fast_empty_finds
        ),
        "online_stage_fail_fast_empty_find_attempts": (
            args.online_stage_fail_fast_empty_find_attempts
        ),
        "online_stage_session_name": online_stage_session_name,
        "online_stage_room_name": online_stage_room_name,
        "online_stage_target_owner_id": online_stage_target_owner_id,
        "online_stage_goal": (
            phase_online_stage_goal if args.phase != "inventory"
            else args.online_stage_goal
        ),
        "online_stage_diagnostic_reflection": (
            args.online_stage_diagnostic_reflection
        ),
        "capture_gameflow": args.capture_gameflow,
        "skip_online_stage_drive": args.skip_online_stage_drive,
        "direct_stage_observe_only": (
            args.skip_online_stage_drive
            or (
                args.mode == "direct-connect"
                and args.phase == "rollback-proof"
            )
        ),
        "replay_input_file": str(args.replay_input_file),
        "main_menu_player_match_route": args.main_menu_player_match_route,
        "replay_input_file_hash": file_fnv1a64(args.replay_input_file),
        "replay_divergence_frame": args.replay_divergence_frame,
        "replay_divergence_window": args.replay_divergence_window,
        "sc6_launch_args": before.get("sc6_launch_args", []),
        "sandbox_query_port": DEFAULT_SANDBOX_QUERY_PORT,
        "launch_configuration_failures": launch_configuration_failures,
        "launch_port_failures": launch_configuration_failures,
        "udp_safety_failures": udp_safety_failures,
        "roots": roots,
        "expected_roles": expected_roles,
        "requests": requests,
        "cleanup_request_after_requested": args.cleanup_request_after,
        "cleanup": cleanup_report,
        "host_advertise_wait": host_advertise_wait,
        "results": results,
        "milestones": aggregate_navigation_milestones(results),
        "packet_timeline": packet_timeline,
        "packet_timeline_summary": packet_timeline_summary,
        "pair_failures": pair_failures,
        "snapshots": snapshots,
        "failures": failures,
    }
    report_output = args.report_output
    if report_output is None and args.phase == "observe-gameflow":
        report_output = (
            OBSERVE_REPORT_DIR
            / f"rollback_gameflow_observe_{report['run_id']}.json"
        )
    timeline_path = write_observe_timeline(report)
    if timeline_path is not None:
        report["timeline_path"] = str(timeline_path)
    report_path = write_report(report, report_output)

    status = "PASS" if phase_ok else "FAIL"
    print(f"rollback two-client {args.phase} {status}")
    print(f"report={report_path}")
    if timeline_path is not None:
        print(f"timeline={timeline_path}")
    print(f"sc6_pids={','.join(str(p) for p in before['sc6_pids'])}")
    print(f"steam_pids={','.join(str(p) for p in before['steam_pids'])}")
    for row in before.get("sc6_launch_args", []):
        print(
            "sc6_launch "
            f"pid={row.get('pid')} "
            f"port={row.get('port') or '-'} "
            f"query_port={row.get('query_port') or '-'}"
        )
    print(f"sandbox_box={args.sandbox_box or '*'}")
    print(f"steam_udp_port={STEAM_UDP_PORT}")
    print(f"host_sidecar_port={args.host_sidecar_port}")
    print(f"sandbox_sidecar_port={args.sandbox_sidecar_port}")
    print(f"roots={len(roots)}")
    for pid, rows in before["steam_udp_27036_by_pid"].items():
        ports = ",".join(
            f"{r.get('local_address')}:{r.get('local_port')}" for r in rows
        )
        print(f"steam_udp_27036 pid={pid} endpoints={ports or '-'}")
    for root in roots:
        print(
            "root "
            f"role={root['role']} box={root['sandbox_box'] or '-'} "
            f"pids={','.join(str(p) for p in root['live_trace_pids']) or '-'} "
            f"path={root['path']}"
        )
    for failure in failures:
        print(f"failure={failure}", file=sys.stderr)
    for failure in pair_failures:
        print(f"pair_failure={failure}", file=sys.stderr)
    for failure in udp_safety_failures:
        print(f"udp_safety_failure={failure}", file=sys.stderr)

    if not phase_ok and (args.strict or args.require_two_sc6):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
