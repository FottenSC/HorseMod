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
