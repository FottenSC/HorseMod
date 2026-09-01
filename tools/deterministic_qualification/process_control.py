from __future__ import annotations

import csv
import ctypes
import json
import subprocess
import time
from dataclasses import dataclass
from io import StringIO
from pathlib import Path


PROCESS_NAME = "SoulcaliburVI.exe"
WM_CLOSE = 0x0010


@dataclass(frozen=True)
class GameProcess:
    pid: int
    command_line: str


def list_game_processes() -> tuple[GameProcess, ...]:
    """Return every SC6 process and its native command line.

    Qualification needs the command line to prove that only the Sandboxie peer
    owns the alternate Steam query port.  A tasklist-only first match is
    deliberately insufficient once paired execution is possible.
    """
    script = (
        "$p=Get-CimInstance Win32_Process -Filter \"Name='SoulcaliburVI.exe'\" | "
        "Select-Object ProcessId,CommandLine; "
        "if($null -eq $p){'[]'}else{@($p)|ConvertTo-Json -Compress}"
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
        check=False,
        capture_output=True,
        text=True,
        timeout=15,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"failed to enumerate SoulcaliburVI processes: {detail}")
    try:
        rows = json.loads(result.stdout.strip() or "[]")
    except json.JSONDecodeError as error:
        raise RuntimeError("SoulcaliburVI process enumeration was malformed") from error
    if isinstance(rows, dict):
        rows = [rows]
    if not isinstance(rows, list):
        raise RuntimeError("SoulcaliburVI process enumeration was malformed")
    processes: list[GameProcess] = []
    for row in rows:
        if not isinstance(row, dict):
            raise RuntimeError("SoulcaliburVI process enumeration was malformed")
        try:
            pid = int(row["ProcessId"])
        except (KeyError, TypeError, ValueError) as error:
            raise RuntimeError("SoulcaliburVI process enumeration was malformed") from error
        if pid <= 0:
            raise RuntimeError("SoulcaliburVI process enumeration contained an invalid PID")
        command_line = row.get("CommandLine")
        processes.append(GameProcess(pid, "" if command_line is None else str(command_line)))
    return tuple(sorted(processes, key=lambda process: process.pid))


def find_game_pid() -> int | None:
    processes = list_game_processes()
    if len(processes) > 1:
        raise RuntimeError("multiple SoulcaliburVI processes are running")
    return None if not processes else processes[0].pid


def is_game_process_alive(pid: int) -> bool:
    result = subprocess.run(
        ["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"],
        check=False,
        capture_output=True,
        text=True,
    )
    for row in csv.reader(StringIO(result.stdout)):
        if len(row) >= 2 and row[0].casefold() == PROCESS_NAME.casefold():
            return int(row[1]) == pid
    return False


def require_game_process(pid: int) -> None:
    if not is_game_process_alive(pid):
        raise RuntimeError(
            f"SoulcaliburVI process {pid} exited before qualification evidence completed"
        )


def launch_game() -> None:
    subprocess.run(
        ["cmd", "/d", "/c", "start", "", "steam://rungameid/544750"],
        check=True,
        capture_output=True,
    )


def launch_game_executable(executable: Path) -> None:
    """Launch SC6 directly without shell/UI automation or focus operations."""
    resolved = executable.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"SoulcaliburVI executable not found: {resolved}")
    subprocess.Popen(
        [str(resolved)], cwd=resolved.parent,
        creationflags=(subprocess.CREATE_NEW_PROCESS_GROUP
                       | subprocess.DETACHED_PROCESS),
        close_fds=True,
    )


def wait_for_game(timeout_seconds: float) -> int:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        pid = find_game_pid()
        if pid is not None:
            return pid
        time.sleep(0.5)
    raise TimeoutError("SoulcaliburVI did not start before the timeout")


def _post_close_to_process(pid: int) -> bool:
    user32 = ctypes.windll.user32
    posted = False

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def visit(window: int, _unused: int) -> bool:
        nonlocal posted
        window_pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(window, ctypes.byref(window_pid))
        if window_pid.value == pid and user32.IsWindowVisible(window):
            posted = bool(user32.PostMessageW(window, WM_CLOSE, 0, 0)) or posted
        return True

    user32.EnumWindows(visit, 0)
    return posted


def close_game(pid: int, timeout_seconds: float = 60.0) -> None:
    if not _post_close_to_process(pid):
        raise RuntimeError("no visible SoulcaliburVI window accepted WM_CLOSE")
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if not is_game_process_alive(pid):
            return
        time.sleep(0.25)
    raise TimeoutError("SoulcaliburVI did not exit after WM_CLOSE; refusing a forced stop")


def force_stop_game_for_cleanup(pid: int, timeout_seconds: float = 20.0) -> None:
    """Bounded emergency cleanup after graceful shutdown has already failed.

    This is not qualifying evidence for clean teardown. It exists only so a
    failed run cannot leave SC6 alive with the temporary qualification DLL
    locked and diagnostic state stranded on disk.
    """
    if not is_game_process_alive(pid):
        return
    result = subprocess.run(
        ["taskkill", "/PID", str(pid), "/T", "/F"],
        check=False,
        capture_output=True,
        text=True,
    )
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if not is_game_process_alive(pid):
            return
        time.sleep(0.25)
    detail = (result.stderr or result.stdout).strip()
    raise TimeoutError(
        f"SoulcaliburVI process {pid} survived emergency cleanup: {detail}"
    )
