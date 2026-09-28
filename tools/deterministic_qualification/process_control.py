from __future__ import annotations

import csv
import concurrent.futures
import ctypes
import json
import subprocess
import time
from dataclasses import dataclass
from io import StringIO
from pathlib import Path


PROCESS_NAME = "SoulcaliburVI.exe"
WM_CLOSE = 0x0010


class ProcessExitWitness:
    """Keep the launched process object alive for exit diagnostics, without control rights."""
    def __init__(self, pid: int):
        from ctypes import wintypes
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.WaitForSingleObject.restype = wintypes.DWORD
        self.api.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.api.GetExitCodeProcess.restype = wintypes.BOOL
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.api.CloseHandle.restype = wintypes.BOOL
        self.handle = self.api.OpenProcess(0x1000 | 0x100000, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def exit_code(self):
        from ctypes import wintypes
        if not self.handle:
            raise RuntimeError("process exit witness is closed")
        wait = self.api.WaitForSingleObject(self.handle, 0)
        if wait == 258:  # WAIT_TIMEOUT: still running; exit code 259 can also be a real exit.
            return None
        if wait != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        code = wintypes.DWORD()
        if not self.api.GetExitCodeProcess(self.handle, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        return code.value

    def close(self):
        if self.handle:
            if not self.api.CloseHandle(self.handle):
                raise ctypes.WinError(ctypes.get_last_error())
            self.handle = None


@dataclass(frozen=True)
class GameProcess:
    pid: int
    command_line: str


def _list_game_process_ids() -> tuple[int, ...]:
    """Enumerate SC6 cheaply without loading PowerShell's automation runtime."""
    result = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {PROCESS_NAME}",
         "/FO", "CSV", "/NH"],
        check=False,
        capture_output=True,
        text=True,
        timeout=15,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"failed to enumerate SoulcaliburVI processes: {detail}")
    pids: list[int] = []
    for row in csv.reader(StringIO(result.stdout)):
        if len(row) < 2 or row[0].casefold() != PROCESS_NAME.casefold():
            continue
        try:
            pid = int(row[1])
        except ValueError as error:
            raise RuntimeError(
                "SoulcaliburVI tasklist enumeration was malformed") from error
        if pid <= 0:
            raise RuntimeError(
                "SoulcaliburVI process enumeration contained an invalid PID")
        pids.append(pid)
    return tuple(sorted(pids))


def list_game_processes(
    *, include_command_lines: bool = False,
) -> tuple[GameProcess, ...]:
    """Return every SC6 process, querying command lines only when requested.

    Match polling and cleanup only need stable PIDs.  Avoiding a PowerShell/CIM
    process on every poll matters when two game processes are near the machine's
    commit limit.  Paired launch still requests command lines once to prove that
    only the Sandboxie peer owns the alternate Steam query port.
    """
    pids = _list_game_process_ids()
    if not include_command_lines or not pids:
        return tuple(GameProcess(pid, "") for pid in pids)

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
    processes = sorted(processes, key=lambda process: process.pid)
    if tuple(process.pid for process in processes) != pids:
        raise RuntimeError(
            "SoulcaliburVI process identity changed during command-line enumeration")
    return tuple(processes)


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


def launch_game_executable(executable: Path, extra_args: tuple[str, ...] = ()) -> subprocess.Popen:
    """Launch SC6 directly without shell/UI automation or focus operations."""
    resolved = executable.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"SoulcaliburVI executable not found: {resolved}")
    return subprocess.Popen(
        [str(resolved), *extra_args], cwd=resolved.parent,
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


def stop_game_processes(
    processes: tuple[GameProcess, ...], require_graceful: bool = True,
    graceful_timeout_seconds: float = 30.0,
) -> bool:
    failures: list[str] = []
    # Close owned games together within the same teardown deadline.
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=max(1, len(processes))) as executor:
        closing = {
            executor.submit(
                close_game, process.pid, graceful_timeout_seconds): process
            for process in processes
        }
        for future, process in closing.items():
            try:
                future.result()
            except (RuntimeError, TimeoutError) as error:
                failures.append(f"PID {process.pid}: {error}")
                force_stop_game_for_cleanup(process.pid)
    owned_pids = {process.pid for process in processes}
    survivors = tuple(process for process in list_game_processes() if process.pid in owned_pids)
    if survivors:
        for process in survivors:
            force_stop_game_for_cleanup(process.pid)
        failures.append("SC6 survived graceful teardown")
    if failures:
        if require_graceful:
            raise RuntimeError("; ".join(failures))
        return False
    return True
