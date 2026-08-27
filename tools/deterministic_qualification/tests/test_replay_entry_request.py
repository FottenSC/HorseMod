from pathlib import Path

import pytest

from tools.deterministic_qualification.replay_entry import create_request


def test_seek_request_uses_bounded_version_four_contract(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "sample.bin"
    replay.write_bytes(b"ULX1test")

    run_id = create_request(replay, 600, (10, 25, 50, 75))
    request = (
        tmp_path / "HorseMod" / "Qualification" / "replay_request.txt"
    ).read_text(encoding="utf-8")

    assert f"run_id={run_id}\n" in request
    assert request.startswith("version=4\n")
    assert "watch_frames=600\n" in request
    assert "seek_percentages=10,25,50,75\n" in request
    assert "min_resume_tick_rate_milli=58000\n" in request
    assert request.endswith("resume_tick_window=120\n")


def test_seek_request_rejects_endpoint_percentages(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "sample.bin"
    replay.write_bytes(b"ULX1test")

    with pytest.raises(RuntimeError, match="between 1 and 99"):
        create_request(replay, 600, (0,))
    with pytest.raises(RuntimeError, match="between 1 and 99"):
        create_request(replay, 600, (100,))


def test_stage_terminal_request_uses_typed_version_five_contract(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "stage9.bin"
    replay.write_bytes(b"ULX1test")

    create_request(replay, 600, stage_terminal="wall")
    request = (
        tmp_path / "HorseMod" / "Qualification" / "replay_request.txt"
    ).read_text(encoding="utf-8")

    assert request.startswith("version=5\n")
    assert "seek_percentages=\n" in request
    assert request.endswith("stage_terminal=wall\n")
