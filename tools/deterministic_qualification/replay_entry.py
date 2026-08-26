from __future__ import annotations

import os
import shutil
import time
import uuid
from contextlib import AbstractContextManager
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


@dataclass(frozen=True)
class ReplayEntryResult:
    run_id: str
    result: str
    reason: str


class TemporaryReplayMod(AbstractContextManager["TemporaryReplayMod"]):
    """Deploy the test-only C++ mod without leaving it installed."""

    def __init__(self, source_dll: Path, mods_root: Path) -> None:
        self._source_dll = source_dll
        self._root = mods_root / "ReplayQualificationMod"
        self._dlls = self._root / "dlls"
        self._dll = self._dlls / "main.dll"
        self._enabled = self._root / "enabled.txt"

    def __enter__(self) -> "TemporaryReplayMod":
        if self._root.exists():
            raise RuntimeError(
                f"temporary qualification mod already exists; refusing to overwrite: {self._root}"
            )
        self._dlls.mkdir(parents=True)
        shutil.copy2(self._source_dll, self._dll)
        self._enabled.write_text("", encoding="utf-8")
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        deadline = time.monotonic() + 5.0
        while True:
            try:
                self._enabled.unlink(missing_ok=True)
                self._dll.unlink(missing_ok=True)
                self._dlls.rmdir()
                self._root.rmdir()
                return
            except OSError:
                if time.monotonic() >= deadline:
                    raise RuntimeError(
                        f"failed to remove temporary qualification mod: {self._root}"
                    )
                time.sleep(0.25)


def qualification_root() -> Path:
    local_app_data = os.environ.get("LOCALAPPDATA")
    if not local_app_data:
        raise RuntimeError("LOCALAPPDATA is unavailable")
    return Path(local_app_data) / "HorseMod" / "Qualification"


def create_request(
    replay_path: Path,
    watch_frames: int,
    seek_percentages: tuple[int, ...] = (),
    min_resume_tick_rate: float = 58.0,
    resume_tick_window: int = 120,
) -> str:
    if watch_frames < 1 or watch_frames > 36000:
        raise RuntimeError("watch frames must be between 1 and 36000")
    replay_text = str(replay_path.resolve())
    if "\n" in replay_text or "\r" in replay_text:
        raise RuntimeError("replay path contains a newline")
    run_id = uuid.uuid4().hex
    root = qualification_root()
    root.mkdir(parents=True, exist_ok=True)
    request = root / "replay_request.txt"
    result = root / "replay_result.txt"
    result.unlink(missing_ok=True)
    temporary = request.with_suffix(".tmp")
    for percentage in seek_percentages:
        if percentage <= 0 or percentage >= 100:
            raise RuntimeError("seek percentages must be between 1 and 99")
    if not 1.0 <= min_resume_tick_rate <= 1000.0:
        raise RuntimeError("minimum resume tick rate must be between 1 and 1000")
    if not 1 <= resume_tick_window <= 36000:
        raise RuntimeError("resume tick window must be between 1 and 36000")
    version = 4 if seek_percentages else 2
    seek_line = (
        "seek_percentages=" + ",".join(map(str, seek_percentages)) + "\n"
        f"min_resume_tick_rate_milli={round(min_resume_tick_rate * 1000)}\n"
        f"resume_tick_window={resume_tick_window}\n"
        if seek_percentages else ""
    )
    temporary.write_text(
        f"version={version}\nrun_id={run_id}\nreplay_path={replay_text}\n"
        f"watch_frames={watch_frames}\n{seek_line}",
        encoding="utf-8",
    )
    os.replace(temporary, request)
    return run_id


def _read_result(path: Path, run_id: str) -> ReplayEntryResult | None:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    fields: dict[str, str] = {}
    for line in lines:
        key, separator, value = line.partition("=")
        if not separator or key in fields:
            return None
        fields[key] = value
    if set(fields) != {"version", "run_id", "result", "reason"}:
        return None
    if fields["version"] != "1" or fields["run_id"] != run_id:
        return None
    return ReplayEntryResult(run_id, fields["result"], fields["reason"])


def wait_for_replay_entry(
    run_id: str,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
) -> ReplayEntryResult:
    result_path = qualification_root() / "replay_result.txt"
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        result = _read_result(result_path, run_id)
        if result is not None:
            if result.result == "failed":
                raise RuntimeError(f"native replay entry failed: {result.reason}")
            if result.result == "launch_requested":
                return result
        time.sleep(0.25)
    raise TimeoutError("native replay entry did not request launch before the timeout")


def remove_request_files(run_id: str) -> None:
    root = qualification_root()
    for name in ("replay_request.txt", "replay_result.txt"):
        path = root / name
        result = _read_result(path, run_id) if name.endswith("result.txt") else None
        if name.endswith("result.txt") and result is None:
            continue
        if name.endswith("request.txt"):
            try:
                text = path.read_text(encoding="utf-8")
            except OSError:
                continue
            if f"run_id={run_id}\n" not in text:
                continue
        path.unlink(missing_ok=True)
