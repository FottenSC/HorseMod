from __future__ import annotations

import re
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


SOURCE_PATTERN = re.compile(r"\[HorseMod\] ctor v(?P<version>\S+) source=(?P<commit>[0-9a-f]{40})")
REPLAY_ENTRY_PATTERN = re.compile(
    r"\[ReplayQualification\] source=(?P<commit>[0-9a-f]{40}) "
    r"native_import=(?P<status>ready|blocked)"
)
HOOK_ARMED_TEXT = "[HorseMod] deterministic lifecycle hooks armed"
HOOK_FAILED_TEXT = "[HorseMod] frame-fencepost runtime proof unavailable"
FRAME_OBSERVED_TEXT = "[HorseMod] frame-fencepost first observation"


@dataclass(frozen=True)
class BootEvidence:
    version: str
    source_commit: str
    hook_armed_line: str


@dataclass(frozen=True)
class ReplayLifecycleEvidence:
    source_commit: str
    native_import_ready: bool
    frame_observed_line: str


@dataclass(frozen=True)
class LogCursor:
    offset: int
    sentinel_offset: int
    sentinel: bytes
    prefix: bytes


def capture_log_offset(log_path: Path) -> LogCursor:
    try:
        with log_path.open("rb") as stream:
            size = stream.seek(0, 2)
            stream.seek(0)
            prefix = stream.read(min(size, 64))
            sentinel_offset = max(0, size - 64)
            stream.seek(sentinel_offset)
            return LogCursor(size, sentinel_offset, stream.read(), prefix)
    except OSError:
        return LogCursor(0, 0, b"", b"")


def _read_since(log_path: Path, cursor: LogCursor | int) -> str:
    with log_path.open("rb") as stream:
        size = stream.seek(0, 2)
        if isinstance(cursor, int):
            start_offset = cursor if cursor <= size else 0
        elif cursor.offset <= size:
            stream.seek(0)
            prefix_matches = stream.read(len(cursor.prefix)) == cursor.prefix
            stream.seek(cursor.sentinel_offset)
            tail_matches = stream.read(len(cursor.sentinel)) == cursor.sentinel
            start_offset = cursor.offset if prefix_matches and tail_matches else 0
        else:
            start_offset = 0
        stream.seek(start_offset)
        return stream.read().decode("utf-8", errors="replace")


def parse_boot_evidence(text: str) -> BootEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    source = source_matches[-1]
    current_boot = text[source.start():]
    armed_lines = [line for line in current_boot.splitlines() if HOOK_ARMED_TEXT in line]
    if not armed_lines or HOOK_FAILED_TEXT in current_boot:
        return None
    return BootEvidence(
        version=source.group("version"),
        source_commit=source.group("commit"),
        hook_armed_line=armed_lines[-1],
    )


def wait_for_boot_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> BootEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_boot_evidence(_read_since(log_path, start_offset))
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.5)
    raise TimeoutError("HorseMod source identity and hook-arm evidence did not appear")


def parse_replay_lifecycle_evidence(text: str) -> ReplayLifecycleEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    entry_matches = list(REPLAY_ENTRY_PATTERN.finditer(current_boot))
    observed_lines = [
        line for line in current_boot.splitlines() if FRAME_OBSERVED_TEXT in line
    ]
    if not entry_matches or not observed_lines:
        return None
    entry = entry_matches[-1]
    return ReplayLifecycleEvidence(
        source_commit=entry.group("commit"),
        native_import_ready=entry.group("status") == "ready",
        frame_observed_line=observed_lines[-1],
    )


def wait_for_replay_lifecycle_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> ReplayLifecycleEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_replay_lifecycle_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.5)
    raise TimeoutError("replay entry and frame-fencepost evidence did not appear")
