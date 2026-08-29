from __future__ import annotations

import csv
import ctypes
import json
import subprocess
import time
from dataclasses import dataclass
from io import StringIO


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


def wait_for_game(timeout_seconds: float) -> int:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        pid = find_game_pid()
        if pid is not None:
            return pid
        time.sleep(0.5)
    raise TimeoutError("SoulcaliburVI did not start before the timeout")


def focus_game_window(pid: int, timeout_seconds: float = 60.0) -> None:
    """Make the exact SC6 top-level window foreground before qualification.

    SC6 is externally frame-throttled while its window is in the background.
    A normal-render run therefore cannot silently proceed until Windows proves
    that the launched process owns the foreground window.
    """
    user32 = ctypes.windll.user32
    user32.GetForegroundWindow.restype = ctypes.c_void_p
    user32.GetWindowThreadProcessId.argtypes = (
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)
    )
    user32.IsWindowVisible.argtypes = (ctypes.c_void_p,)
    user32.ShowWindowAsync.argtypes = (ctypes.c_void_p, ctypes.c_int)
    user32.BringWindowToTop.argtypes = (ctypes.c_void_p,)
    user32.SetForegroundWindow.argtypes = (ctypes.c_void_p,)
    user32.GetWindowRect.argtypes = (ctypes.c_void_p, ctypes.c_void_p)
    user32.AttachThreadInput.argtypes = (
        ctypes.c_ulong, ctypes.c_ulong, ctypes.c_bool
    )
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        require_game_process(pid)
        candidates: list[tuple[int, int]] = []

        class Rect(ctypes.Structure):
            _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                        ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def visit(window: int, _unused: int) -> bool:
            window_pid = ctypes.c_ulong()
            user32.GetWindowThreadProcessId(window, ctypes.byref(window_pid))
            if window_pid.value == pid and user32.IsWindowVisible(window):
                rect = Rect()
                user32.GetWindowRect(window, ctypes.byref(rect))
                area = max(0, rect.right - rect.left) * max(
                    0, rect.bottom - rect.top
                )
                candidates.append((area, window))
            return True

        user32.EnumWindows(visit, 0)
        for _area, window in sorted(candidates, reverse=True):
            foreground = user32.GetForegroundWindow()
            foreground_pid = ctypes.c_ulong()
            foreground_thread = (
                user32.GetWindowThreadProcessId(
                    foreground, ctypes.byref(foreground_pid)
                ) if foreground else 0
            )
            target_pid = ctypes.c_ulong()
            target_thread = user32.GetWindowThreadProcessId(
                window, ctypes.byref(target_pid)
            )
            current_thread = ctypes.windll.kernel32.GetCurrentThreadId()
            attached_foreground = bool(
                foreground_thread and foreground_thread != current_thread
                and user32.AttachThreadInput(
                    current_thread, foreground_thread, True
                )
            )
            attached_target = bool(
                target_thread and target_thread != current_thread
                and target_thread != foreground_thread
                and user32.AttachThreadInput(current_thread, target_thread, True)
            )
            user32.ShowWindowAsync(window, 9)  # SW_RESTORE
            user32.BringWindowToTop(window)
            # A synthetic Alt press allows a foreground transition under the
            # same Windows rule used by task switching, without sending a
            # gameplay key to SC6.
            user32.keybd_event(0x12, 0, 0, 0)
            user32.SetForegroundWindow(window)
            user32.keybd_event(0x12, 0, 2, 0)
            if attached_target:
                user32.AttachThreadInput(current_thread, target_thread, False)
            if attached_foreground:
                user32.AttachThreadInput(current_thread, foreground_thread, False)
            foreground = user32.GetForegroundWindow()
            foreground_pid = ctypes.c_ulong()
            if foreground:
                user32.GetWindowThreadProcessId(
                    foreground, ctypes.byref(foreground_pid)
                )
            if foreground_pid.value == pid:
                return
        time.sleep(0.25)
    raise TimeoutError(
        f"SoulcaliburVI process {pid} did not own the foreground window"
    )


def is_game_foreground(pid: int) -> bool:
    user32 = ctypes.windll.user32
    user32.GetForegroundWindow.restype = ctypes.c_void_p
    user32.GetWindowThreadProcessId.argtypes = (
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)
    )
    foreground = user32.GetForegroundWindow()
    if not foreground:
        return False
    foreground_pid = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(foreground, ctypes.byref(foreground_pid))
    return foreground_pid.value == pid


def require_foreground_game_process(pid: int) -> None:
    require_game_process(pid)
    if not is_game_foreground(pid):
        focus_game_window(pid, timeout_seconds=5.0)


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
