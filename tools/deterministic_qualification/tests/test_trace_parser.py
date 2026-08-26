from pathlib import Path

from tools.deterministic_qualification.trace_parser import (
    capture_log_offset,
    parse_forced_qualification_evidence,
    parse_replay_seek_evidence,
    parse_presentation_coverage_evidence,
    wait_for_boot_evidence,
    wait_for_replay_lifecycle_evidence,
)


def test_presentation_coverage_parser_is_source_bound() -> None:
    text = (
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[ReplayQualification] presentation source coverage stage_wall=1 "
        "stage_barrier=2 stage_dispatch=3 audio=4 audio_direct=5 "
        "audio_remap=6 audio_source=7 audio_stop_all=8 "
        "audio_blueprint=9 particle_spawn=10\n"
    )
    evidence = parse_presentation_coverage_evidence(text)
    assert evidence is not None
    assert evidence.stage_barrier == 2
    assert evidence.audio_stop_all == 8
    assert evidence.audio_blueprint == 9
    assert evidence.particle_spawn == 10


def test_replay_seek_parser_requires_structured_rate_window() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[ReplayQualification] strict seek passed percent=10 target=1025 "
        "source_end=1575 history_verified=550 live_resumed=120 "
        "resume_total=670 resim=8 validation_us=4966 resume_window=120 "
        "resume_elapsed_us=2050000 resume_tick_rate_milli=58536 index=0\n"
    )
    evidence = parse_replay_seek_evidence(text)
    assert len(evidence) == 1
    assert evidence[0].percentage == 10
    assert evidence[0].resimulation_coordinates == 8
    assert evidence[0].resume_window == 120
    assert evidence[0].resume_tick_rate_milli == 58536


def test_forced_qualification_parser_prefers_terminal_failure() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[HorseMod] forced depth-7 qualification started generation=6\n"
        "[HorseMod] forced depth-7 qualification failed completed=367 "
        "frame=1355 status=presentation_failed primary=presentation_failed\n"
    )
    evidence = parse_forced_qualification_evidence(text)
    assert evidence is not None
    assert evidence.result == "failed"
    assert evidence.completed == 367
    assert evidence.status == "presentation_failed"


def test_forced_qualification_parser_requires_exact_summary_fields() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[HorseMod] forced depth-7 qualification passed completed=600 "
        "generations=6-7 canonical_convergence=exact "
        "presentation_terminal_coverage=incomplete\n"
    )
    evidence = parse_forced_qualification_evidence(text)
    assert evidence is not None
    assert evidence.result == "passed"
    assert evidence.completed == 600
    assert evidence.canonical_convergence == "exact"
    assert evidence.presentation_terminal_coverage == "incomplete"


def test_waiters_ignore_complete_stale_sessions(tmp_path: Path) -> None:
    log = tmp_path / "UE4SS.log"
    log.write_text(
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[HorseMod] deterministic lifecycle hooks armed\n"
        "[ReplayQualification] source=" + "a" * 40
        + " native_import=ready\n"
        "[HorseMod] frame-fencepost first observation\n",
        encoding="utf-8",
    )
    start = capture_log_offset(log)
    with log.open("a", encoding="utf-8") as stream:
        stream.write(
            "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
            "[HorseMod] deterministic lifecycle hooks armed\n"
            "[ReplayQualification] source=" + "b" * 40
            + " native_import=ready\n"
            "[HorseMod] frame-fencepost first observation\n"
        )

    boot = wait_for_boot_evidence(log, 0.1, start_offset=start)
    lifecycle = wait_for_replay_lifecycle_evidence(
        log, 0.1, start_offset=start
    )
    assert boot.source_commit == "b" * 40
    assert lifecycle.source_commit == "b" * 40


def test_waiters_restart_at_zero_when_log_is_truncated_and_regrown(
    tmp_path: Path,
) -> None:
    log = tmp_path / "UE4SS.log"
    stale = (
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[HorseMod] deterministic lifecycle hooks armed\n"
        "[ReplayQualification] source=" + "a" * 40
        + " native_import=ready\n"
        "[HorseMod] frame-fencepost first observation\n"
    )
    log.write_text(stale, encoding="utf-8")
    start = capture_log_offset(log)

    current = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[HorseMod] deterministic lifecycle hooks armed\n"
        "[ReplayQualification] source=" + "b" * 40
        + " native_import=ready\n"
        "[HorseMod] frame-fencepost first observation\n"
        + "new-session-padding\n" * 20
    )
    assert len(current.encode("utf-8")) > len(stale.encode("utf-8"))
    log.write_text(current, encoding="utf-8")

    boot = wait_for_boot_evidence(log, 0.1, start_offset=start)
    lifecycle = wait_for_replay_lifecycle_evidence(
        log, 0.1, start_offset=start
    )
    assert boot.source_commit == "b" * 40
    assert lifecycle.source_commit == "b" * 40
