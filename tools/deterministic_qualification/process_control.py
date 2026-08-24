from __future__ import annotations

import csv
import ctypes
import subprocess
import time
from io import StringIO


PROCESS_NAME = "SoulcaliburVI.exe"
WM_CLOSE = 0x0010


def find_game_pid() -> int | None:
    result = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {PROCESS_NAME}", "/FO", "CSV", "/NH"],
        check=False,
        capture_output=True,
        text=True,
    )
    for row in csv.reader(StringIO(result.stdout)):
        if len(row) >= 2 and row[0].casefold() == PROCESS_NAME.casefold():
            return int(row[1])
    return None


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


def close_game(pid: int, timeout_seconds: float = 15.0) -> None:
    if not _post_close_to_process(pid):
        raise RuntimeError("no visible SoulcaliburVI window accepted WM_CLOSE")
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if find_game_pid() is None:
            return
        time.sleep(0.25)
    raise TimeoutError("SoulcaliburVI did not exit after WM_CLOSE; refusing a forced stop")

