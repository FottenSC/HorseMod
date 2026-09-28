from __future__ import annotations
import pytest

import threading
from subprocess import CompletedProcess

from tools.deterministic_qualification import process_control


@pytest.mark.workflow
def test_process_exit_witness_retains_terminated_process_and_closes():
    import os
    import subprocess
    import sys
    import pytest
    if os.name != "nt":
        pytest.skip("Windows process-handle contract")
    # Handshake prevents the child from exiting before the witness opens it.
    for code in (0, 259, 37):
        child = subprocess.Popen([sys.executable, "-c",
            f"import sys; sys.stdin.read(1); sys.exit({code})"], stdin=subprocess.PIPE)
        witness = None
        try:
            witness = process_control.ProcessExitWitness(child.pid)
            assert witness.exit_code() is None
            child.communicate(b"x", timeout=10)
            assert witness.exit_code() == code
            assert witness.exit_code() == code
            witness.close()
            witness.close()
            with pytest.raises(RuntimeError, match="closed"):
                witness.exit_code()
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=10)
            if witness is not None:
                witness.close()


@pytest.mark.workflow
def test_pid_only_enumeration_uses_tasklist_without_powershell(monkeypatch):
    commands: list[list[str]] = []

    def run(command, **_kwargs):
        commands.append(command)
        return CompletedProcess(
            command, 0,
            stdout='"SoulcaliburVI.exe","41","Console","1","1,000 K"\n',
            stderr="",
        )

    monkeypatch.setattr(process_control.subprocess, "run", run)
    assert process_control.list_game_processes() == (
        process_control.GameProcess(41, ""),
    )
    assert len(commands) == 1
    assert commands[0][0] == "tasklist"


@pytest.mark.workflow
def test_command_line_enumeration_is_explicit_and_identity_bound(monkeypatch):
    commands: list[list[str]] = []

    def run(command, **_kwargs):
        commands.append(command)
        if command[0] == "tasklist":
            return CompletedProcess(
                command, 0,
                stdout='"SoulcaliburVI.exe","41","Console","1","1,000 K"\n',
                stderr="",
            )
        return CompletedProcess(
            command, 0,
            stdout='[{"ProcessId":41,"CommandLine":"game -queryport=27016"}]',
            stderr="",
        )

    monkeypatch.setattr(process_control.subprocess, "run", run)
    assert process_control.list_game_processes(include_command_lines=True) == (
        process_control.GameProcess(41, "game -queryport=27016"),
    )
    assert [command[0] for command in commands] == ["tasklist", "powershell.exe"]


@pytest.mark.workflow
def test_paired_teardown_starts_both_graceful_closes_together(monkeypatch):
    rendezvous = threading.Barrier(2)
    started: set[int] = set()

    def close(pid: int, timeout_seconds: float) -> None:
        assert timeout_seconds == 30.0
        started.add(pid)
        rendezvous.wait(timeout=1)

    monkeypatch.setattr(
        "tools.deterministic_qualification.process_control.close_game", close)
    monkeypatch.setattr(
        "tools.deterministic_qualification.process_control.list_game_processes",
        lambda: (),
    )
    assert process_control.stop_game_processes(
        (process_control.GameProcess(1, "host"), process_control.GameProcess(2, "sandbox"))) is True
    assert started == {1, 2}


@pytest.mark.workflow
def test_development_teardown_records_emergency_cleanup(monkeypatch):
    forced: list[int] = []
    observed_timeouts: list[float] = []

    def fail_close(_pid: int, timeout_seconds: float) -> None:
        observed_timeouts.append(timeout_seconds)
        raise TimeoutError("still active")

    monkeypatch.setattr(
        "tools.deterministic_qualification.process_control.close_game", fail_close)
    monkeypatch.setattr(
        "tools.deterministic_qualification.process_control.force_stop_game_for_cleanup",
        forced.append,
    )
    monkeypatch.setattr(
        "tools.deterministic_qualification.process_control.list_game_processes",
        lambda: (),
    )
    assert process_control.stop_game_processes(
        (process_control.GameProcess(7, "host"),), require_graceful=False,
        graceful_timeout_seconds=5.0) is False
    assert forced == [7]
    assert observed_timeouts == [5.0]
