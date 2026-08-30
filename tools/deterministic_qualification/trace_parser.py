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
REPLAY_METADATA_PATTERN = re.compile(
    r"\[ReplayQualification\] replay metadata stage=(?P<stage>\d+) "
    r"map=(?P<map>\d+) left_character=(?P<left>\d+) "
    r"right_character=(?P<right>\d+) state_reset_records=(?P<records>\d+)"
)
HOOK_ARMED_TEXT = "[HorseMod] deterministic lifecycle hooks armed"
HOOK_FAILED_TEXT = "[HorseMod] frame-fencepost runtime proof unavailable"
FRAME_OBSERVED_TEXT = "[HorseMod] frame-fencepost first observation"
FORCED_SUMMARY_PATTERN = re.compile(
    r"\[HorseMod\] forced (?:depth-7|correction) qualification (?P<result>passed|failed) "
    r"(?:depth=(?P<depth>\d+) location=(?P<location>\d+) )?"
    r"completed=(?P<completed>\d+).*"
    r"stage_wall_suppressed=(?P<stage_wall>\d+) "
    r"stage_barrier_suppressed=(?P<stage_barrier>\d+) "
    r"stage_semantic_dispatches=(?P<stage_dispatch>\d+).*"
    r"particle_spawn_suppressed=(?P<particle>\d+).*"
    r"presentation_failures=(?P<presentation_failures>\d+).*"
    r"canonical_convergence=(?P<canonical>\S+) "
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
NORMAL_RENDER_RATE_PATTERN = re.compile(
    r"\[ReplayQualification\] normal-render battle rate "
    r"frames=(?P<frames>\d+) elapsed_us=(?P<elapsed>\d+) "
    r"tick_rate_milli=(?P<rate>\d+)"
)
NORMAL_RENDER_ACTIVE_RATE_PATTERN = re.compile(
    r"\[ReplayQualification\] normal-render active battle rate "
    r"frames=(?P<frames>\d+) elapsed_us=(?P<elapsed>\d+) "
    r"tick_rate_milli=(?P<rate>\d+)"
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
PRESENTATION_IDENTITY_PATTERN = re.compile(
    r"\[ReplayQualification\] presentation identity batches=(?P<batches>\d+) "
    r"audio_events=(?P<audio_events>\d+) audio_identity=0x(?P<audio>[0-9a-f]{16}) "
    r"order_events=(?P<order_events>\d+) order_identity=0x(?P<order>[0-9a-f]{16}) "
    r"camera_identity=0x(?P<camera>[0-9a-f]{16}) camera_batches=(?P<camera_batches>\d+) "
    r"failures=(?P<failures>\d+) "
    r"journal_committed=(?P<committed>\d+)"
)
QUALIFICATION_HEALTH_PATTERN = re.compile(
    r"\[ReplayQualification\] qualification health "
    r"capacity_failures=(?P<capacity>\d+) "
    r"capacity_growth_events=(?P<growth>\d+) "
    r"timeline_accounting_failures=(?P<accounting>\d+) "
    r"aggregate_owned_bytes=(?P<aggregate>\d+) "
    r"presentation_owned_bytes=(?P<presentation>\d+) "
    r"presentation_duplicate_failures=(?P<duplicates>\d+) "
    r"presentation_publish_failures=(?P<publish>\d+)"
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
    r"tira_targets=0x(?P<tira_targets>[0-9a-f]+) "
    r"xorshift_sequence=0x(?P<xorshift_sequence>[0-9a-f]{16}) "
    r"transition07_sequence=0x(?P<transition07_sequence>[0-9a-f]{16}) "
    r"tira_sequence=0x(?P<tira_sequence>[0-9a-f]{16}) "
    r"tira_stance_batches=(?P<tira_stance_batches>\d+) "
    r"tira_slot_mask=0x(?P<tira_slot_mask>[0-9a-f]+) "
    r"state19_sequence_p0=0x(?P<state19_sequence_p0>[0-9a-f]{16}) "
    r"state19_sequence_p1=0x(?P<state19_sequence_p1>[0-9a-f]{16}) "
    r"state19_initial_p0=(?P<state19_initial_p0>\d+) "
    r"state19_initial_p1=(?P<state19_initial_p1>\d+) "
    r"state19_final_p0=(?P<state19_final_p0>\d+) "
    r"state19_final_p1=(?P<state19_final_p1>\d+) "
    r"xorshift_landing=0x(?P<xorshift0>[0-9a-f]{8}),"
    r"0x(?P<xorshift1>[0-9a-f]{8}),0x(?P<xorshift2>[0-9a-f]{8}) "
    r"state19_at_tira_transition_p0=(?P<state19_transition_p0>\d+) "
    r"state19_at_tira_transition_p1=(?P<state19_transition_p1>\d+) "
    r"state19_initial_valid=(?P<state19_initial_valid>[01]) "
    r"tira_last_target=0x(?P<tira_last_target>[0-9a-f]{4}) "
    r"resolved_hit_calls=(?P<resolved_hit_calls>\d+) "
    r"resolved_hit_sequence=0x(?P<resolved_hit_sequence>[0-9a-f]{16})"
)
STOCK_ROUND_OUTCOME_PATTERN = re.compile(
    r"\[ReplayQualification\] (?:stock round outcome qualification passed|ordered round outcomes verified) "
    r"rounds=(?P<rounds>\d+) match_winner=(?P<winner>-?\d+) "
    r"winners=(?P<winners>[012](?:,[012])*)"
)
CORRECTION_PROBE_PATTERN = re.compile(
    r"\[HorseMod\] owned correction probe passed depth=(?P<depth>\d+) "
    r"base=(?P<base>\d+) final=(?P<final>\d+) batches=(?P<batches>\d+) "
    r"coordinates=(?P<coordinates>\d+).*?total_us=(?P<total>\d+)"
)
FINAL_CANONICAL_PATTERN = re.compile(
    r"\[ReplayQualification\] final canonical state "
    r"generation=(?P<generation>\d+) frame=(?P<frame>\d+) "
    r"sha256=(?P<hash>[0-9a-f]{64})"
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
    depth: int = 7
    location: int = 2
    suppressed_stage_wall: int = 0
    suppressed_stage_barrier: int = 0
    semantic_stage_dispatches: int = 0
    round_terminal_source_stop_all: int = 0
    suppressed_particle_spawn: int = 0
    presentation_failures: int = 0
    cycle_p99_us: int = 0
    cycle_max_us: int = 0
    capture_samples: int = 0
    capture_p99_us: int = 0
    capture_max_us: int = 0
    scratch_capacity_begin: int = 0
    scratch_capacity_end: int = 0
    scratch_growth_events: int = 0
    audio_batches_verified: int = 0
    audio_sequence_mismatches: int = 0
    camera_batches_verified: int = 0
    camera_publication_mismatches: int = 0
    journal_attempted: int = 0
    journal_recorded: int = 0
    journal_discarded: int = 0
    journal_committed: int = 0
    journal_duplicates: int = 0
    journal_capacity_failures: int = 0
    journal_publish_failures: int = 0
    journal_pending: int = 0
    journal_payload_bytes: int = 0


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
class NormalRenderRateEvidence:
    frames: int
    elapsed_us: int
    tick_rate_milli: int
    active_frames: int
    active_elapsed_us: int
    active_tick_rate_milli: int


@dataclass(frozen=True)
class FinalCanonicalEvidence:
    generation: int
    frame: int
    sha256: str


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
class PresentationIdentityEvidence:
    batches: int
    audio_events: int
    audio_identity: int
    order_events: int
    order_identity: int
    camera_identity: int
    camera_batches: int
    failures: int
    journal_committed: int


@dataclass(frozen=True)
class QualificationHealthEvidence:
    capacity_failures: int
    capacity_growth_events: int
    timeline_accounting_failures: int
    aggregate_owned_bytes: int
    presentation_owned_bytes: int
    presentation_duplicate_failures: int
    presentation_publish_failures: int


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
    xorshift_sequence: int
    transition07_sequence: int
    tira_sequence: int
    tira_stance_batches: int
    tira_slot_mask: int
    state19_sequence_p0: int
    state19_sequence_p1: int
    state19_initial_p0: int
    state19_initial_p1: int
    state19_final_p0: int
    state19_final_p1: int
    xorshift_landing: tuple[int, int, int]
    state19_at_tira_transition_p0: int
    state19_at_tira_transition_p1: int
    state19_initial_valid: bool
    tira_last_target: int
    resolved_hit_calls: int
    resolved_hit_sequence: int


@dataclass(frozen=True)
class StockRoundOutcomeEvidence:
    source_commit: str
    rounds: int
    match_winner: int
    round_winners: tuple[int, ...]


@dataclass(frozen=True)
class ReplayMetadataEvidence:
    stage: int
    map: int
    left_character: int
    right_character: int
    state_reset_records: int


@dataclass(frozen=True)
class CorrectionProbeEvidence:
    depth: int
    base: int
    final: int
    batches: int
    coordinates: int
    total_us: int


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


def parse_normal_render_rate_evidence(
    text: str,
) -> NormalRenderRateEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    overall_matches = list(NORMAL_RENDER_RATE_PATTERN.finditer(current_boot))
    active_matches = list(NORMAL_RENDER_ACTIVE_RATE_PATTERN.finditer(current_boot))
    if not overall_matches or not active_matches:
        return None
    overall = overall_matches[-1]
    active = active_matches[-1]
    return NormalRenderRateEvidence(
        frames=int(overall.group("frames")),
        elapsed_us=int(overall.group("elapsed")),
        tick_rate_milli=int(overall.group("rate")),
        active_frames=int(active.group("frames")),
        active_elapsed_us=int(active.group("elapsed")),
        active_tick_rate_milli=int(active.group("rate")),
    )


def wait_for_normal_render_rate_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> NormalRenderRateEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_normal_render_rate_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("normal-render frame/tick-rate evidence did not appear")


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


def parse_presentation_identity_evidence(
    text: str,
) -> PresentationIdentityEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    matches = list(PRESENTATION_IDENTITY_PATTERN.finditer(current_boot))
    if not matches:
        return None
    match = matches[-1]
    return PresentationIdentityEvidence(
        batches=int(match.group("batches")),
        audio_events=int(match.group("audio_events")),
        audio_identity=int(match.group("audio"), 16),
        order_events=int(match.group("order_events")),
        order_identity=int(match.group("order"), 16),
        camera_identity=int(match.group("camera"), 16),
        camera_batches=int(match.group("camera_batches")),
        failures=int(match.group("failures")),
        journal_committed=int(match.group("committed")),
    )


def parse_qualification_health_evidence(
    text: str,
) -> QualificationHealthEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    matches = list(QUALIFICATION_HEALTH_PATTERN.finditer(current_boot))
    if not matches:
        return None
    match = matches[-1]
    return QualificationHealthEvidence(
        capacity_failures=int(match.group("capacity")),
        capacity_growth_events=int(match.group("growth")),
        timeline_accounting_failures=int(match.group("accounting")),
        aggregate_owned_bytes=int(match.group("aggregate")),
        presentation_owned_bytes=int(match.group("presentation")),
        presentation_duplicate_failures=int(match.group("duplicates")),
        presentation_publish_failures=int(match.group("publish")),
    )


def parse_stock_round_outcome_evidence(
    text: str,
) -> StockRoundOutcomeEvidence | None:
    source_matches = list(REPLAY_ENTRY_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    matches = list(STOCK_ROUND_OUTCOME_PATTERN.finditer(current_boot))
    if not matches:
        return None
    match = matches[-1]
    return StockRoundOutcomeEvidence(
        source_commit=source_matches[-1].group("commit"),
        rounds=int(match.group("rounds")),
        match_winner=int(match.group("winner")),
        round_winners=tuple(
            int(value) for value in match.group("winners").split(",")
        ),
    )


def wait_for_stock_round_outcome_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> StockRoundOutcomeEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_stock_round_outcome_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("stock replay ordered round outcomes did not verify")


def parse_replay_metadata_evidence(text: str) -> ReplayMetadataEvidence | None:
    source_matches = list(REPLAY_ENTRY_PATTERN.finditer(text))
    if not source_matches:
        return None
    current_boot = text[source_matches[-1].start():]
    matches = list(REPLAY_METADATA_PATTERN.finditer(current_boot))
    if not matches:
        return None
    match = matches[-1]
    return ReplayMetadataEvidence(
        stage=int(match.group("stage")), map=int(match.group("map")),
        left_character=int(match.group("left")),
        right_character=int(match.group("right")),
        state_reset_records=int(match.group("records")),
    )


def wait_for_replay_metadata_evidence(
    log_path: Path, timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> ReplayMetadataEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_replay_metadata_evidence(
                _read_since(log_path, start_offset))
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("native replay metadata did not verify")


def parse_correction_probe_evidence(
    text: str,
) -> tuple[CorrectionProbeEvidence, ...]:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return ()
    current_boot = text[source_matches[-1].start():]
    return tuple(
        CorrectionProbeEvidence(
            depth=int(match.group("depth")),
            base=int(match.group("base")),
            final=int(match.group("final")),
            batches=int(match.group("batches")),
            coordinates=int(match.group("coordinates")),
            total_us=int(match.group("total")),
        )
        for match in CORRECTION_PROBE_PATTERN.finditer(current_boot)
    )


def wait_for_correction_probe_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> tuple[CorrectionProbeEvidence, ...]:
    deadline = time.monotonic() + timeout_seconds
    expected = (1, 6, 11, 7)
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_correction_probe_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = ()
        if tuple(item.depth for item in evidence) == expected:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("owned correction probes did not pass in order")


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


def wait_for_presentation_identity_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> PresentationIdentityEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_presentation_identity_evidence(
                _read_since(log_path, start_offset))
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("replay presentation identity did not appear")


def wait_for_qualification_health_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> QualificationHealthEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_qualification_health_evidence(
                _read_since(log_path, start_offset))
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("replay qualification health did not appear")


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
        xorshift_sequence=int(match.group("xorshift_sequence"), 16),
        transition07_sequence=int(match.group("transition07_sequence"), 16),
        tira_sequence=int(match.group("tira_sequence"), 16),
        tira_stance_batches=int(match.group("tira_stance_batches")),
        tira_slot_mask=int(match.group("tira_slot_mask"), 16),
        state19_sequence_p0=int(match.group("state19_sequence_p0"), 16),
        state19_sequence_p1=int(match.group("state19_sequence_p1"), 16),
        state19_initial_p0=int(match.group("state19_initial_p0")),
        state19_initial_p1=int(match.group("state19_initial_p1")),
        state19_final_p0=int(match.group("state19_final_p0")),
        state19_final_p1=int(match.group("state19_final_p1")),
        xorshift_landing=(
            int(match.group("xorshift0"), 16),
            int(match.group("xorshift1"), 16),
            int(match.group("xorshift2"), 16),
        ),
        state19_at_tira_transition_p0=int(match.group("state19_transition_p0")),
        state19_at_tira_transition_p1=int(match.group("state19_transition_p1")),
        state19_initial_valid=match.group("state19_initial_valid") == "1",
        tira_last_target=int(match.group("tira_last_target"), 16),
        resolved_hit_calls=int(match.group("resolved_hit_calls")),
        resolved_hit_sequence=int(match.group("resolved_hit_sequence"), 16),
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
        numeric = {
            key: int(value)
            for key, value in re.findall(r"([a-z0-9_]+)=(\d+)", match.group(0))
        }
        capacity = re.search(r"scratch_capacity_bytes=(\d+)->(\d+)", match.group(0))
        candidates.append((match.start(), ForcedQualificationEvidence(
            result=match.group("result"),
            completed=int(match.group("completed")),
            canonical_convergence=match.group("canonical"),
            presentation_terminal_coverage=match.group("presentation"),
            status="none" if match.group("result") == "passed"
                else "qualification_failed",
            line=match.group(0),
            depth=int(match.group("depth") or 7),
            location=int(match.group("location") or 2),
            suppressed_stage_wall=int(match.group("stage_wall")),
            suppressed_stage_barrier=int(match.group("stage_barrier")),
            semantic_stage_dispatches=int(match.group("stage_dispatch")),
            round_terminal_source_stop_all=numeric.get(
                "round_terminal_source_stop_all", 0),
            suppressed_particle_spawn=int(match.group("particle")),
            presentation_failures=int(match.group("presentation_failures")),
            cycle_p99_us=numeric.get("cycle_p99_us", 0),
            cycle_max_us=numeric.get("cycle_max_us", 0),
            capture_samples=numeric.get("capture_samples", 0),
            capture_p99_us=numeric.get("capture_p99_us", 0),
            capture_max_us=numeric.get("capture_max_us", 0),
            scratch_capacity_begin=int(capacity.group(1)) if capacity else 0,
            scratch_capacity_end=int(capacity.group(2)) if capacity else 0,
            scratch_growth_events=numeric.get("scratch_growth_events", 0),
            audio_batches_verified=numeric.get("audio_batches_verified", 0),
            audio_sequence_mismatches=numeric.get("audio_sequence_mismatches", 0),
            camera_batches_verified=numeric.get("camera_batches_verified", 0),
            camera_publication_mismatches=numeric.get("camera_publication_mismatches", 0),
            journal_attempted=numeric.get("journal_attempted", 0),
            journal_recorded=numeric.get("journal_recorded", 0),
            journal_discarded=numeric.get("journal_discarded", 0),
            journal_committed=numeric.get("journal_committed", 0),
            journal_duplicates=numeric.get("journal_duplicates", 0),
            journal_capacity_failures=numeric.get("journal_capacity_failures", 0),
            journal_publish_failures=numeric.get("journal_publish_failures", 0),
            journal_pending=numeric.get("journal_pending", 0),
            journal_payload_bytes=numeric.get("journal_payload_bytes", 0),
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


def parse_final_canonical_evidence(text: str) -> FinalCanonicalEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    matches = list(FINAL_CANONICAL_PATTERN.finditer(text[source_matches[-1].start():]))
    if not matches:
        return None
    match = matches[-1]
    return FinalCanonicalEvidence(
        generation=int(match.group("generation")),
        frame=int(match.group("frame")),
        sha256=match.group("hash"),
    )


def wait_for_final_canonical_evidence(
    log_path: Path,
    timeout_seconds: float,
    progress_guard: Callable[[], None] | None = None,
    start_offset: LogCursor | int = 0,
) -> FinalCanonicalEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if progress_guard is not None:
            progress_guard()
        try:
            evidence = parse_final_canonical_evidence(
                _read_since(log_path, start_offset)
            )
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.25)
    raise TimeoutError("final replay canonical state did not appear")
