import json
from pathlib import Path

import pytest

from tools.deterministic_qualification.artifacts import runner_sha256, sha256_file
from tools.deterministic_qualification.runner import load_outcome_control


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
