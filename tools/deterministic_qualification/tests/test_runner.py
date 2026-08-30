import json
from pathlib import Path

import pytest

from tools.deterministic_qualification.artifacts import runner_sha256, sha256_file
from tools.deterministic_qualification.runner import (
    _read_bounded_log_since,
    _temporarily_armed_smoke_config,
    load_outcome_control,
)
from tools.deterministic_qualification.trace_parser import capture_log_offset


def _write(path, value: bytes) -> None:
    path.write_bytes(value)


def test_outcome_control_binds_every_executable_artifact(tmp_path):
    replay = tmp_path / "replay.bin"
    dll = tmp_path / "HorseMod.dll"
    replay_mod = tmp_path / "ReplayQualificationMod.dll"
    schema = tmp_path / "schema.json"
    executable = tmp_path / "SoulcaliburVI.exe"
    for path, value in (
        (replay, b"replay"), (dll, b"dll"), (replay_mod, b"bridge"),
        (schema, b"schema"), (executable, b"game"),
    ):
        _write(path, value)
    artifacts = {
        "replay": {"sha256": sha256_file(replay)},
        "horsemod_dll": {"sha256": sha256_file(dll)},
        "replay_qualification_mod": {"sha256": sha256_file(replay_mod)},
        "generated_schema": {"sha256": sha256_file(schema)},
        "game_executable": {"sha256": sha256_file(executable)},
        "runner_sha256": runner_sha256(Path(__file__).resolve().parents[1]),
    }
    control = tmp_path / "control.json"
    control.write_text(json.dumps({
        "report_schema": 2, "certifying": True, "renderer": "normal",
        "result": "pass", "artifacts": artifacts,
        "runtime": {"stock_round_outcome": {
            "round_winners": [1, 1], "match_winner": 1, "rounds": 2,
        }},
    }), encoding="utf-8")

    winners, winner, identity = load_outcome_control(
        control, replay, dll, replay_mod, schema, executable)
    assert winners == (1, 1)
    assert winner == 1
    assert identity["sha256"] == sha256_file(control)

    _write(dll, b"changed")
    with pytest.raises(RuntimeError, match="HorseMod DLL hash mismatch"):
        load_outcome_control(control, replay, dll, replay_mod, schema, executable)

    _write(dll, b"dll")
    data = json.loads(control.read_text(encoding="utf-8"))
    data["certifying"] = False
    control.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(RuntimeError, match="certifying pass"):
        load_outcome_control(control, replay, dll, replay_mod, schema, executable)
    winners, winner, _ = load_outcome_control(
        control, replay, dll, replay_mod, schema, executable,
        allow_noncertifying=True,
    )
    assert winners == (1, 1)
    assert winner == 1


def test_bounded_failure_log_restarts_after_log_rotation(tmp_path):
    log = tmp_path / "UE4SS.log"
    log.write_bytes(b"old boot\n" + b"x" * 1024)
    cursor = capture_log_offset(log)
    failure = b"[ReplayQualification] fail-fast health frame=965\n"
    log.write_bytes(b"new boot\n" + failure + b"y" * 2048)

    captured = _read_bounded_log_since(log, cursor, maximum_bytes=4096)

    assert captured.startswith(b"new boot\n")
    assert failure in captured


def test_smoke_config_arms_hooks_and_restores_exact_bytes(tmp_path):
    config = tmp_path / "rollback.ini"
    original = (
        b"config_version=1\r\nenabled=true\r\nrollback_window=9\r\n"
        b"input_delay=3\r\ntrace=false\r\ncorrection_probe=true\r\n"
        b"forced_depth7_qualification=true\r\nqualification_depth=6\r\n"
        b"qualification_location=4\r\n"
    )
    config.write_bytes(original)

    with _temporarily_armed_smoke_config(config):
        armed = config.read_text(encoding="utf-8")
        assert "enabled=false\n" in armed
        assert "trace=true\n" in armed
        assert "correction_probe=false\n" in armed
        assert "forced_depth7_qualification=false\n" in armed

    assert config.read_bytes() == original
