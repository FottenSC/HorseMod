from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from .artifacts import sha256_file
from .configuration import contract_sha256, is_exact_contract
from .offline_spec import OfflineMatrixRow
from .report import write_report
from .trace_parser import LogCursor, capture_log_offset


def _read_current_run_tail(
    log: Path,
    cursor: LogCursor,
    *,
    maximum_bytes: int = 2 * 1024 * 1024,
    maximum_lines: int = 512,
) -> list[str]:
    """Read a bounded tail without inheriting prior-process log history."""
    if not log.is_file():
        return []
    with log.open("rb") as stream:
        size = stream.seek(0, 2)
        start_offset = 0
        if cursor.offset <= size:
            stream.seek(0)
            prefix_matches = stream.read(len(cursor.prefix)) == cursor.prefix
            stream.seek(cursor.sentinel_offset)
            tail_matches = stream.read(len(cursor.sentinel)) == cursor.sentinel
            if prefix_matches and tail_matches:
                start_offset = cursor.offset
        start_offset = max(start_offset, size - maximum_bytes)
        stream.seek(start_offset)
        lines = stream.read(maximum_bytes).decode(
            "utf-8", errors="replace").splitlines()
    return lines[-maximum_lines:]


def capture_log_artifact_is_intact(
    report: dict[str, Any], fallback_path: Path | None = None,
) -> bool:
    artifact = report.get("artifacts", {}).get("bounded_log", {})
    stored_path = artifact.get("path")
    path = Path(stored_path) if isinstance(stored_path, str) else fallback_path
    if path is None:
        return False
    try:
        return bool(
            path.is_file()
            and artifact.get("sha256") == sha256_file(path)
            and artifact.get("size") == path.stat().st_size
        )
    except OSError:
        return False


def load_reusable_capture(
    path: Path,
    row: OfflineMatrixRow,
    expected_artifacts: dict[str, Any],
    expected_config: dict[str, str],
    *,
    stock: bool,
    outcome_control: Path | None = None,
    seek_percentages: tuple[int, ...] = (),
) -> dict[str, Any] | None:
    """Return an immutable raw capture only when every producer input matches."""
    if not path.is_file():
        return None
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    artifacts = report.get("artifacts", {})
    runtime = report.get("runtime", {})
    metadata = runtime.get("replay_metadata", {})
    bounded_log_valid = capture_log_artifact_is_intact(
        report, path.with_suffix(".log"))
    valid = (
        report.get("report_schema") == 2
        and report.get("certifying") is True
        and report.get("result") == "pass"
        and report.get("case_id") == row.case_id
        and report.get("row_id") == row.row_id
        and report.get("renderer") == "normal"
        and report.get("display_map_name") == row.display_map_name
        and report.get("stage_package_root") == row.stage_package_root
        and artifacts.get("horsemod_dll_sha256") == expected_artifacts["dll"]
        and artifacts.get("schema_sha256") == expected_artifacts["schema"]
        and artifacts.get("capture_harness_sha256")
            == expected_artifacts["capture_harness"]
        and artifacts.get("replay", {}).get("sha256") == row.replay_sha256
        and artifacts.get("replay_qualification_mod", {}).get("sha256")
            == expected_artifacts["replay_mod"]
        and artifacts.get("game_executable", {}).get("sha256")
            == expected_artifacts["executable"]
        and bounded_log_valid
        and is_exact_contract(artifacts.get("config_fields"), expected_config)
        and artifacts.get("config", {}).get("sha256")
            == contract_sha256(expected_config)
        and metadata.get("stage") == row.replay_metadata_stage
        and metadata.get("map") == row.replay_metadata_map
        and (metadata.get("left_character"), metadata.get("right_character"))
            == row.replay_metadata_fighters
        and runtime.get("watch_frames") == 600
        and runtime.get("seek_percentages") == list(seek_percentages)
        and runtime.get("clean_exit") is True
        and runtime.get("process_absent_after_exit") is True
        and runtime.get("temporary_mod_removed") is True
        and (stock or runtime.get("native_replay_import_ready") is True)
    )
    if outcome_control is not None:
        control = artifacts.get("stock_outcome_control", {})
        valid = (valid and outcome_control.is_file()
                 and control.get("sha256") == sha256_file(outcome_control))
    return report if valid else None


def invoke_replay(
    root: Path,
    row: OfflineMatrixRow,
    report: Path,
    dll: Path,
    config: Path,
    schema: Path,
    replay_mod: Path,
    game_executable: Path,
    log: Path,
    *,
    certifying: bool,
    stock: bool = False,
    baseline: bool = False,
    stage_terminal: str | None = None,
    outcome_control: Path | None = None,
    require_authored_outcomes: bool = False,
    seek_percentages: tuple[int, ...] = (),
    timeout: float,
) -> dict[str, Any]:
    """Run one raw replay capture under the immutable producer protocol."""
    replay_path = root / row.replay
    if sha256_file(replay_path) != row.replay_sha256:
        raise RuntimeError(f"frozen replay hash mismatch: {row.row_id}")
    command = [
        sys.executable, str(root / "tools" / "deterministic_qualification.py"),
        "replay-entry", "--replay", str(replay_path),
        "--replay-mod", str(replay_mod), "--dll", str(dll),
        "--config", str(config), "--schema", str(schema),
        "--game-executable", str(game_executable), "--log", str(log),
        "--report", str(report), "--timeout", str(timeout),
        "--watch-frames", "600" if certifying else "1",
        "--case-id", row.case_id, "--row-id", row.row_id,
        "--display-map-name", row.display_map_name,
        "--stage-package-root", row.stage_package_root,
    ]
    if certifying:
        command.extend([
            "--certifying",
            # Use the full workload after the authored replay is active.
            "--min-resume-tick-rate", "58",
            "--resume-tick-window", "600",
        ])
    else:
        command.append("--allow-dirty")
    if stock:
        command.append("--stock-round-outcome-control")
    if baseline:
        command.append("--deterministic-baseline")
    if stage_terminal is not None:
        command.extend(["--stage-terminal", stage_terminal])
    if outcome_control is not None:
        command.extend(["--outcome-control-report", str(outcome_control)])
    if require_authored_outcomes:
        command.append("--require-authored-outcomes")
    if seek_percentages:
        command.extend(["--seek-percentages", *map(str, seek_percentages)])
    if row.required_corrections:
        command.append("--require-presentation-coverage")
    if row.location is not None:
        command.extend(["--correction-location", row.location])
    log_cursor = capture_log_offset(log)
    result = subprocess.run(command, cwd=root, text=True)
    bounded_log = report.with_suffix(".log")
    bounded_lines = _read_current_run_tail(log, log_cursor)
    bounded_log.write_text(
        "\n".join(bounded_lines) + ("\n" if bounded_lines else ""),
        encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(
            f"offline row subprocess failed ({result.returncode}): {row.row_id}")
    document = json.loads(report.read_text(encoding="utf-8"))
    if document.get("result") != "pass":
        raise RuntimeError(f"offline row report failed: {row.row_id}")
    document.setdefault("artifacts", {})["bounded_log"] = {
        "path": str(bounded_log.resolve()),
        "sha256": sha256_file(bounded_log),
        "size": bounded_log.stat().st_size,
    }
    write_report(report, document)
    return document
