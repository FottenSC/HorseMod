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
FORCED_SUMMARY_PATTERN = re.compile(
    r"\[HorseMod\] forced depth-7 qualification (?P<result>passed|failed) "
    r"completed=(?P<completed>\d+).*canonical_convergence=(?P<canonical>\S+) "
    r"presentation_terminal_coverage=(?P<presentation>\S+)"
)
FORCED_FAILURE_PATTERN = re.compile(
    r"\[HorseMod\] forced depth-7 qualification failed "
    r"completed=(?P<completed>\d+).*status=(?P<status>\S+)"
)
REPLAY_SEEK_PATTERN = re.compile(
    r"\[ReplayQualification\] strict seek passed percent=(?P<percent>\d+) "
    r"target=(?P<target>\d+) source_end=(?P<source>\d+) "
    r"history_verified=(?P<history>\d+) live_resumed=(?P<live>\d+) "
    r"resume_total=(?P<total>\d+) resim=(?P<resim>\d+) "
    r"validation_us=(?P<validation>\d+) resume_window=(?P<window>\d+) "
    r"resume_elapsed_us=(?P<elapsed>\d+) "
    r"resume_tick_rate_milli=(?P<rate>\d+) index=(?P<index>\d+)"
)
PRESENTATION_COVERAGE_PATTERN = re.compile(
    r"\[ReplayQualification\] presentation source coverage "
    r"stage_wall=(?P<stage_wall>\d+) stage_barrier=(?P<stage_barrier>\d+) "
    r"stage_dispatch=(?P<stage_dispatch>\d+) audio=(?P<audio>\d+) "
    r"audio_direct=(?P<audio_direct>\d+) audio_remap=(?P<audio_remap>\d+) "
    r"audio_source=(?P<audio_source>\d+) audio_stop_all=(?P<audio_stop>\d+) "
    r"audio_blueprint=(?P<audio_blueprint>\d+) "
    r"particle_spawn=(?P<particle>\d+)"
)
GAMEPLAY_RNG_COVERAGE_PATTERN = re.compile(
    r"\[ReplayQualification\] gameplay rng coverage "
    r"xorshift_draws=(?P<draws>\d+) known_callers=0x(?P<known>[0-9a-f]+) "
    r"unknown_callers=(?P<unknown>\d+) weighted_draws=(?P<weighted>\d+) "
    r"if_draws=(?P<if_draws>\d+) short25_p0=(?P<short0>\d+) "
    r"short25_p1=(?P<short1>\d+) "
    r"probability_transition_batches=(?P<transitions>\d+) "
    r"state_changes_p0=(?P<state0>\d+) state_changes_p1=(?P<state1>\d+) "
    r"probability_state_mask_p0=(?P<mask0>[0-9a-f]{64}) "
    r"probability_state_mask_p1=(?P<mask1>[0-9a-f]{64}) "
    r"transition07_calls=(?P<transition07>\d+) "
    r"tira_random_transitions=(?P<tira_transitions>\d+) "
    r"tira_probability_batches=(?P<tira_batches>\d+) "
    r"tira_targets=0x(?P<tira_targets>[0-9a-f]+)"
)


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
class ForcedQualificationEvidence:
    result: str
    completed: int
    canonical_convergence: str
    presentation_terminal_coverage: str
    status: str
    line: str


@dataclass(frozen=True)
class ReplaySeekEvidence:
    percentage: int
    target: int
    source_end: int
    history_verified: int
    live_resumed: int
    resume_total: int
    resimulation_coordinates: int
    validation_us: int
    resume_window: int
    resume_elapsed_us: int
    resume_tick_rate_milli: int
    index: int


@dataclass(frozen=True)
class PresentationCoverageEvidence:
    stage_wall: int
    stage_barrier: int
    stage_dispatch: int
    audio: int
    audio_direct: int
    audio_remap: int
    audio_source: int
    audio_stop_all: int
    audio_blueprint: int
    particle_spawn: int


@dataclass(frozen=True)
class GameplayRngCoverageEvidence:
    xorshift_draws: int
    known_callers: int
    unknown_callers: int
    weighted_draws: int
    if_draws: int
    short25_p0: int
    short25_p1: int
    probability_transition_batches: int
    state_changes_p0: int
    state_changes_p1: int
    probability_state_mask_p0: int
    probability_state_mask_p1: int
    transition07_calls: int
    tira_random_transitions: int
    tira_probability_batches: int
    tira_targets: int


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


def parse_replay_seek_evidence(text: str) -> tuple[ReplaySeekEvidence, ...]:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return ()
    current_boot = text[source_matches[-1].start():]
    return tuple(
        ReplaySeekEvidence(
            percentage=int(match.group("percent")),
            target=int(match.group("target")),
            source_end=int(match.group("source")),
            history_verified=int(match.group("history")),
            live_resumed=int(match.group("live")),
            resume_total=int(match.group("total")),
            resimulation_coordinates=int(match.group("resim")),
            validation_us=int(match.group("validation")),
            resume_window=int(match.group("window")),
            resume_elapsed_us=int(match.group("elapsed")),
            resume_tick_rate_milli=int(match.group("rate")),
            index=int(match.group("index")),
        )
        for match in REPLAY_SEEK_PATTERN.finditer(current_boot)
    )


def wait_for_replay_seek_evidence(
    log_path: Path,
    expected_percentages: tuple[int, ...],
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> tuple[ReplaySeekEvidence, ...]:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_replay_seek_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = ()
        if tuple(item.percentage for item in evidence) == expected_percentages \
                and tuple(item.index for item in evidence) == tuple(
                    range(len(expected_percentages))
                ):
            return evidence
        time.sleep(0.25)
    raise TimeoutError("strict replay seeks did not produce complete ordered evidence")


def parse_presentation_coverage_evidence(
    text: str,
) -> PresentationCoverageEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    matches = list(PRESENTATION_COVERAGE_PATTERN.finditer(current_boot))
    if not matches:
        return None
    match = matches[-1]
    return PresentationCoverageEvidence(
        stage_wall=int(match.group("stage_wall")),
        stage_barrier=int(match.group("stage_barrier")),
        stage_dispatch=int(match.group("stage_dispatch")),
        audio=int(match.group("audio")),
        audio_direct=int(match.group("audio_direct")),
        audio_remap=int(match.group("audio_remap")),
        audio_source=int(match.group("audio_source")),
        audio_stop_all=int(match.group("audio_stop")),
        audio_blueprint=int(match.group("audio_blueprint")),
        particle_spawn=int(match.group("particle")),
    )


def wait_for_presentation_coverage_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> PresentationCoverageEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_presentation_coverage_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("replay presentation source coverage did not appear")


def parse_gameplay_rng_coverage_evidence(
    text: str,
) -> GameplayRngCoverageEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    matches = list(GAMEPLAY_RNG_COVERAGE_PATTERN.finditer(current_boot))
    if not matches:
        return None
    match = matches[-1]
    return GameplayRngCoverageEvidence(
        xorshift_draws=int(match.group("draws")),
        known_callers=int(match.group("known"), 16),
        unknown_callers=int(match.group("unknown")),
        weighted_draws=int(match.group("weighted")),
        if_draws=int(match.group("if_draws")),
        short25_p0=int(match.group("short0")),
        short25_p1=int(match.group("short1")),
        probability_transition_batches=int(match.group("transitions")),
        state_changes_p0=int(match.group("state0")),
        state_changes_p1=int(match.group("state1")),
        probability_state_mask_p0=int(match.group("mask0"), 16),
        probability_state_mask_p1=int(match.group("mask1"), 16),
        transition07_calls=int(match.group("transition07")),
        tira_random_transitions=int(match.group("tira_transitions")),
        tira_probability_batches=int(match.group("tira_batches")),
        tira_targets=int(match.group("tira_targets"), 16),
    )


def wait_for_gameplay_rng_coverage_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> GameplayRngCoverageEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_gameplay_rng_coverage_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("replay gameplay RNG coverage did not appear")


def parse_forced_qualification_evidence(
    text: str,
) -> ForcedQualificationEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    candidates: list[tuple[int, ForcedQualificationEvidence]] = []
    for match in FORCED_SUMMARY_PATTERN.finditer(current_boot):
        candidates.append((match.start(), ForcedQualificationEvidence(
            result=match.group("result"),
            completed=int(match.group("completed")),
            canonical_convergence=match.group("canonical"),
            presentation_terminal_coverage=match.group("presentation"),
            status="none" if match.group("result") == "passed"
                else "qualification_failed",
            line=match.group(0),
        )))
    for match in FORCED_FAILURE_PATTERN.finditer(current_boot):
        candidates.append((match.start(), ForcedQualificationEvidence(
            result="failed",
            completed=int(match.group("completed")),
            canonical_convergence="not_reached",
            presentation_terminal_coverage="not_reached",
            status=match.group("status"),
            line=match.group(0),
        )))
    return max(candidates, key=lambda item: item[0])[1] if candidates else None


def wait_for_forced_qualification_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> ForcedQualificationEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_forced_qualification_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("forced depth-7 qualification did not reach a terminal result")
