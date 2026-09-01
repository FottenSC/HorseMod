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
        # UE4SS and Windows can retain the just-unloaded qualification DLL for
        # several seconds after the game process disappears from tasklist.
        # Keep teardown bounded, but allow the loader/AV file handles to drain
        # before treating an otherwise clean run as a lifecycle failure.
        deadline = time.monotonic() + 30.0
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
    stage_terminal: str | None = None,
    stock_round_outcome_control: bool | None = None,
    require_authored_outcomes: bool = False,
    expected_round_winners: tuple[int, ...] = (),
    expected_match_winner: int | None = None,
    development_smoke: bool = False,
    qualification_cycles: tuple[tuple[str, int, int], ...] = (),
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
    if stage_terminal not in (None, "wall", "barrier", "both"):
        raise RuntimeError("stage terminal must be wall, barrier, or both")
    if stock_round_outcome_control is None:
        stock_round_outcome_control = (
            not seek_percentages and not stage_terminal
            and not qualification_cycles
        )
    if any(winner not in (0, 1, 2) for winner in expected_round_winners):
        raise RuntimeError("expected round winners must be P1, P2, or draw")
    if expected_match_winner not in (None, 0, 1):
        raise RuntimeError("expected match winner must be P1 or P2")
    if require_authored_outcomes and not stock_round_outcome_control:
        if not expected_round_winners or expected_match_winner is None:
            raise RuntimeError(
                "outcome verification requires a same-replay stock control oracle")
    if development_smoke:
        if watch_frames < 60 or watch_frames > 120:
            raise RuntimeError("development smoke must watch 60 to 120 replay frames")
        if (seek_percentages or stage_terminal or stock_round_outcome_control
                or require_authored_outcomes):
            raise RuntimeError(
                "development smoke cannot request seeks, terminals, stock control, or outcomes")
    if qualification_cycles:
        if len(qualification_cycles) > 128:
            raise RuntimeError("qualification campaign supports at most 128 cycles")
        seen: set[str] = set()
        for cycle_run_id, depth, location in qualification_cycles:
            if (not cycle_run_id or len(cycle_run_id) > 96
                    or any(not (value.isalnum() or value in "-_.")
                           for value in cycle_run_id)):
                raise RuntimeError("qualification cycle run ID is invalid")
            if cycle_run_id in seen:
                raise RuntimeError("qualification cycle run IDs must be unique")
            if depth not in (1, 6, 7, 11) or location not in (1, 2, 3, 4):
                raise RuntimeError("qualification cycle depth/location is invalid")
            seen.add(cycle_run_id)
        if (development_smoke or seek_percentages or stage_terminal
                or stock_round_outcome_control or require_authored_outcomes):
            raise RuntimeError(
                "persistent qualification cycles cannot combine with other replay modes")
    version = 10 if qualification_cycles else 9
    seek_line = (
        "seek_percentages=" + ",".join(map(str, seek_percentages)) + "\n"
        f"min_resume_tick_rate_milli={round(min_resume_tick_rate * 1000)}\n"
        f"resume_tick_window={resume_tick_window}\n"
        if version >= 6 or seek_percentages or stage_terminal else ""
    )
    terminal_line = f"stage_terminal={stage_terminal or ''}\n"
    outcome_line = (
        "stock_round_outcome_control="
        f"{'true' if stock_round_outcome_control else 'false'}\n"
    )
    authored_outcome_line = (
        "require_authored_outcomes="
        f"{'true' if require_authored_outcomes else 'false'}\n"
    )
    expected_outcome_lines = (
        "expected_round_winners="
        + ",".join(map(str, expected_round_winners)) + "\n"
        + "expected_match_winner="
        + ("" if expected_match_winner is None else str(expected_match_winner))
        + "\n"
    )
    smoke_line = (
        "development_smoke="
        f"{'true' if development_smoke else 'false'}\n"
    )
    cycles_line = (
        "qualification_cycles="
        + ",".join(f"{run_id}:{depth}:{location}"
                   for run_id, depth, location in qualification_cycles)
        + "\n"
        if qualification_cycles else ""
    )
    temporary.write_text(
        f"version={version}\nrun_id={run_id}\nreplay_path={replay_text}\n"
        f"watch_frames={watch_frames}\n{seek_line}{terminal_line}{outcome_line}"
        f"{authored_outcome_line}{expected_outcome_lines}{smoke_line}{cycles_line}",
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


def require_replay_request_healthy(run_id: str) -> None:
    """Fail immediately if the active native request published a terminal error."""
    result = _read_result(qualification_root() / "replay_result.txt", run_id)
    if result is not None and result.result == "failed":
        raise RuntimeError(f"native replay entry failed: {result.reason}")


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
