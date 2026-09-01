from pathlib import Path

import pytest

from tools.deterministic_qualification.replay_entry import (
    create_request,
    require_replay_request_healthy,
)


def test_seek_request_uses_explicit_version_nine_mode_contract(
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
    assert request.startswith("version=9\n")
    assert "require_authored_outcomes=false\n" in request
    assert "watch_frames=600\n" in request
    assert "seek_percentages=10,25,50,75\n" in request
    assert "min_resume_tick_rate_milli=58000\n" in request
    assert "resume_tick_window=120\n" in request
    assert "stage_terminal=\n" in request
    assert "stock_round_outcome_control=false\n" in request


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


def test_stage_terminal_request_uses_typed_version_nine_contract(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "stage9.bin"
    replay.write_bytes(b"ULX1test")

    create_request(replay, 600, stage_terminal="wall")
    request = (
        tmp_path / "HorseMod" / "Qualification" / "replay_request.txt"
    ).read_text(encoding="utf-8")

    assert request.startswith("version=9\n")
    assert "seek_percentages=\n" in request
    assert "stage_terminal=wall\n" in request
    assert "stock_round_outcome_control=false\n" in request


def test_both_stage_terminals_use_one_bounded_request(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "astral-chaos.bin"
    replay.write_bytes(b"ULX1test")

    create_request(replay, 600, stage_terminal="both")
    request = (
        tmp_path / "HorseMod" / "Qualification" / "replay_request.txt"
    ).read_text(encoding="utf-8")

    assert request.startswith("version=9\n")
    assert "stage_terminal=both\n" in request
    assert "stock_round_outcome_control=false\n" in request


def test_no_seek_request_explicitly_enables_stock_outcome_control(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "stock.bin"
    replay.write_bytes(b"ULX1test")

    create_request(replay, 1)
    request = (
        tmp_path / "HorseMod" / "Qualification" / "replay_request.txt"
    ).read_text(encoding="utf-8")

    assert request.startswith("version=9\n")
    assert "stock_round_outcome_control=true\n" in request


def test_development_smoke_is_bounded_and_disables_outcome_control(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "smoke.bin"
    replay.write_bytes(b"ULX1test")

    create_request(
        replay, 120, stock_round_outcome_control=False,
        development_smoke=True,
    )
    request = (
        tmp_path / "HorseMod" / "Qualification" / "replay_request.txt"
    ).read_text(encoding="utf-8")

    assert "development_smoke=true\n" in request
    assert "stock_round_outcome_control=false\n" in request
    with pytest.raises(RuntimeError, match="60 to 120"):
        create_request(
            replay, 121, stock_round_outcome_control=False,
            development_smoke=True,
        )


def test_outcome_verification_requires_and_serializes_control_oracle(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "control.bin"
    replay.write_bytes(b"ULX1test")

    with pytest.raises(RuntimeError, match="stock control oracle"):
        create_request(
            replay, 600, stock_round_outcome_control=False,
            require_authored_outcomes=True,
        )
    create_request(
        replay, 600, stock_round_outcome_control=False,
        require_authored_outcomes=True,
        expected_round_winners=(1, 0, 2), expected_match_winner=1,
    )
    request = (
        tmp_path / "HorseMod" / "Qualification" / "replay_request.txt"
    ).read_text(encoding="utf-8")
    assert "expected_round_winners=1,0,2\n" in request
    assert "expected_match_winner=1\n" in request


def test_active_request_guard_surfaces_native_terminal_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    root = tmp_path / "HorseMod" / "Qualification"
    root.mkdir(parents=True)
    (root / "replay_result.txt").write_text(
        "version=1\nrun_id=current\nresult=failed\n"
        "reason=horsemod_presentation_coverage_api_unavailable\n",
        encoding="utf-8",
    )

    with pytest.raises(RuntimeError, match="presentation_coverage"):
        require_replay_request_healthy("current")
    require_replay_request_healthy("stale")


def test_persistent_cycles_use_grouped_version_and_unique_runtime_ids(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    replay = tmp_path / "cycles.bin"
    replay.write_bytes(b"ULX1test")
    cycles = (("run11", 11, 1), ("run1", 1, 1), ("run6", 6, 1))

    create_request(replay, 120, qualification_cycles=cycles,
                   qualification_anchors=2, qualification_repeats=3)
    request = (tmp_path / "HorseMod" / "Qualification"
               / "replay_request.txt").read_text(encoding="utf-8")

    assert request.startswith("version=11\n")
    assert "stock_round_outcome_control=false\n" in request
    assert "qualification_cycles=run11:11:1,run1:1:1,run6:6:1\n" in request
    assert "qualification_anchors=2\n" in request
    assert "qualification_repeats=3\n" in request
    with pytest.raises(RuntimeError, match="must be unique"):
        create_request(replay, 120,
                       qualification_cycles=(("same", 11, 1),
                                             ("same", 1, 1),
                                             ("run6", 6, 1)))

    with pytest.raises(RuntimeError, match="depth 11, 1, 6"):
        create_request(replay, 120,
                       qualification_cycles=(("run11", 11, 1),
                                             ("run6", 6, 1),
                                             ("run1", 1, 1)))

    with pytest.raises(RuntimeError, match="depth/location"):
        create_request(replay, 120,
                       qualification_cycles=(("run11", 11, 1),
                                             ("run1", 1, 1),
                                             ("run7", 7, 1)))
