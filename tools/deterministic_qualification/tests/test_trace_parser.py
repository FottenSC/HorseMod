from pathlib import Path

from tools.deterministic_qualification.trace_parser import (
    capture_log_offset,
    wait_for_boot_evidence,
    wait_for_replay_lifecycle_evidence,
)


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
