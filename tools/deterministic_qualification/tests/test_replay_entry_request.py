from pathlib import Path

import pytest

from tools.deterministic_qualification.replay_entry import create_request


def test_seek_request_uses_bounded_version_three_contract(
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
    assert request.startswith("version=3\n")
    assert "watch_frames=600\n" in request
    assert request.endswith("seek_percentages=10,25,50,75\n")


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
