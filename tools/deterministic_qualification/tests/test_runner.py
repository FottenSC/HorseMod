import json
from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.deterministic_qualification.artifacts import (
    capture_harness_sha256, sha256_file,
)
from tools.deterministic_qualification.runner import (
    _read_bounded_log_since,
    _temporarily_armed_smoke_config,
    _write_compact_replay_failure,
    load_outcome_control,
    run_replay_entry,
)
from tools.deterministic_qualification.trace_parser import capture_log_offset


ROOT = Path(__file__).resolve().parents[3]


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
        "capture_harness_sha256": capture_harness_sha256(ROOT),
        # Aggregate package/evaluator identity is not a stock-capture input.
        "runner_sha256": "older-evaluator-package",
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


def test_bounded_failure_log_keeps_late_terminal_line(tmp_path):
    log = tmp_path / "UE4SS.log"
    log.write_bytes(b"old run\n")
    cursor = capture_log_offset(log)
    failure = (
        b"[HorseMod] owned replay seek request failed target=1024 "
        b"component_mask=0x1 native_mask=0x40000\n"
    )
    log.write_bytes(b"old run\n" + b"startup\n" * 2048 + failure)

    captured = _read_bounded_log_since(log, cursor, maximum_bytes=4096)

    assert failure in captured


def test_compact_failure_records_terminal_fields_and_restores_flags(
    tmp_path, monkeypatch,
):
    log = tmp_path / "UE4SS.log"
    log.write_bytes(b"startup\n" * 300000)
    cursor = capture_log_offset(log)
    failure = (
        b"[HorseMod] owned replay seek request failed target=1024 "
        b"component_mask=0x1 native_mask=0x40000 "
        b"owner_selector=general owner_pointer=0x1234 "
        b"caller_rva=0x519789 graph_provenance=0xabcd phase=seek\n"
    )
    with log.open("ab") as stream:
        stream.write(failure)
    config = tmp_path / "rollback.ini"
    config.write_text(
        "enabled=false\ntrace=true\ncorrection_probe=true\n"
        "forced_depth7_qualification=true\n",
        encoding="utf-8",
    )
    report = tmp_path / "failure.json"
    args = SimpleNamespace(
        command="replay-entry", config=config, log=log, report=report,
        case_id="case", row_id="row", display_map_name="Test Map",
        stage_package_root="/Game/Test", _failure_log_start=cursor,
    )
    monkeypatch.setattr(
        "tools.deterministic_qualification.runner.find_game_pid",
        lambda: None,
    )

    _write_compact_replay_failure(args, RuntimeError("intentional failure"))

    document = json.loads(report.read_text(encoding="utf-8"))
    details = document["failure"]
    assert details["first_failing_frame"] == "1024"
    assert details["field_or_mask"] == "0x1"
    assert details["owner_selector"] == "general"
    assert details["owner_pointer"] == "0x1234"
    assert details["return_rva"] == "0x519789"
    assert details["graph_provenance"] == "0xabcd"
    assert details["lifecycle_phase"] == "seek"
    assert failure.decode().strip() in Path(details["bounded_log"]).read_text(
        encoding="utf-8")
    restored = config.read_text(encoding="utf-8")
    assert "enabled=false\n" in restored
    assert "trace=false\n" in restored
    assert "correction_probe=false\n" in restored
    assert "forced_depth7_qualification=false\n" in restored


def test_compact_failure_prefers_terminal_over_earlier_diagnostic(
    tmp_path, monkeypatch,
):
    log = tmp_path / "UE4SS.log"
    cursor = capture_log_offset(log)
    log.write_text(
        "[HorseMod] frame-fencepost observation failed frame=7 mask=0x1\n"
        "[HorseMod] forced depth-7 qualification failed completed=165 "
        "frame=1845 status=presentation_failed component_mask=0x40\n",
        encoding="utf-8",
    )
    config = tmp_path / "rollback.ini"
    config.write_text(
        "enabled=false\ntrace=true\ncorrection_probe=true\n"
        "forced_depth7_qualification=true\n",
        encoding="utf-8",
    )
    report = tmp_path / "failure.json"
    args = SimpleNamespace(
        command="replay-entry", config=config, log=log, report=report,
        case_id="case", row_id="row", display_map_name="Test Map",
        stage_package_root="/Game/Test", _failure_log_start=cursor,
    )
    monkeypatch.setattr(
        "tools.deterministic_qualification.runner.find_game_pid",
        lambda: None,
    )

    _write_compact_replay_failure(args, RuntimeError("qualification failed"))

    details = json.loads(report.read_text(encoding="utf-8"))["failure"]
    assert "forced depth-7 qualification failed" in details["first_failure_line"]
    assert details["first_failing_frame"] == "1845"
    assert details["field_or_mask"] == "0x40"


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


def test_baseline_wrapper_arms_smoke_and_full_run_then_restores(
    tmp_path, monkeypatch,
):
    config = tmp_path / "rollback.ini"
    original = (
        b"config_version=1\nenabled=false\nrollback_window=12\n"
        b"input_delay=1\ntrace=false\ncorrection_probe=false\n"
        b"forced_depth7_qualification=false\nqualification_depth=7\n"
        b"qualification_location=2\n"
    )
    config.write_bytes(original)
    observed = []

    def capture(args):
        observed.append((
            args.development_smoke,
            config.read_text(encoding="utf-8"),
        ))
        return 0

    monkeypatch.setattr(
        "tools.deterministic_qualification.runner._run_replay_entry_once",
        capture,
    )
    args = SimpleNamespace(
        certifying=False,
        skip_development_smoke=False,
        smoke_frames=120,
        deterministic_baseline=True,
        development_smoke=False,
        stock_round_outcome_control=False,
        require_authored_outcomes=True,
        require_tira_probability_transition=False,
        require_presentation_coverage=False,
        outcome_control_report=tmp_path / "stock.json",
        seek_percentages=[],
        stage_terminal=None,
        watch_frames=600,
        report=tmp_path / "baseline.json",
        config=config,
    )

    assert run_replay_entry(args) == 0
    assert [smoke for smoke, _ in observed] == [True, False]
    assert all("trace=true\n" in text for _, text in observed)
    assert all("enabled=false\n" in text for _, text in observed)
    assert config.read_bytes() == original


def test_baseline_smoke_skip_still_arms_full_run_then_restores(
    tmp_path, monkeypatch,
):
    config = tmp_path / "rollback.ini"
    original = (
        b"config_version=1\nenabled=false\nrollback_window=12\n"
        b"input_delay=1\ntrace=false\ncorrection_probe=false\n"
        b"forced_depth7_qualification=false\nqualification_depth=7\n"
        b"qualification_location=2\n"
    )
    config.write_bytes(original)
    observed = []

    def capture(args):
        observed.append(config.read_text(encoding="utf-8"))
        return 0

    monkeypatch.setattr(
        "tools.deterministic_qualification.runner._run_replay_entry_once",
        capture,
    )
    args = SimpleNamespace(
        certifying=False,
        skip_development_smoke=True,
        smoke_frames=120,
        deterministic_baseline=True,
        development_smoke=False,
        config=config,
    )

    assert run_replay_entry(args) == 0
    assert len(observed) == 1
    assert "enabled=false\n" in observed[0]
    assert "trace=true\n" in observed[0]
    assert config.read_bytes() == original
