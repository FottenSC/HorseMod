from __future__ import annotations

import datetime as dt
import json
from pathlib import Path
from typing import Any

from .artifacts import runner_sha256, sha256_file
from .paired_online import (
    _correction_stimulus_sequence_evidence,
    _raise_on_native_terminal,
    _repeated_correction_evidence,
)
from .report import write_report


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def _bound_pair_logs(report: dict[str, Any]) -> dict[str, str]:
    artifacts = report.get("raw_log_artifacts")
    _require(isinstance(artifacts, dict), "raw log artifact bindings are missing")
    logs: dict[str, str] = {}
    for peer in ("host", "sandbox"):
        matches = [value for label, value in artifacts.items()
                   if label == peer or label.endswith(f"-{peer}")]
        _require(len(matches) == 1,
                 f"expected one bound {peer} raw log, found {len(matches)}")
        artifact = matches[0]
        _require(isinstance(artifact, dict), f"{peer} raw log binding is invalid")
        path_value = artifact.get("path")
        _require(isinstance(path_value, str), f"{peer} raw log path is missing")
        path = Path(path_value)
        _require(path.is_file(), f"{peer} raw log is missing: {path}")
        _require(artifact.get("size") == path.stat().st_size,
                 f"{peer} raw log size changed")
        _require(artifact.get("sha256") == sha256_file(path),
                 f"{peer} raw log hash changed")
        logs[peer] = path.read_text(encoding="utf-8", errors="replace")
    return logs


def reevaluate_paired_correction_capture(
    input_report: Path, output_report: Path, root: Path,
) -> int:
    """Evaluate immutable paired raw logs without replaying the live capture."""
    try:
        original = json.loads(input_report.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"paired capture report is invalid: {error}") from error
    _require(original.get("report_schema") == 2,
             "paired capture report schema is not v2")
    _require(original.get("kind") == "paired_online_case",
             "input is not a paired online capture report")
    _require(original.get("certifying") is False,
             "only non-certifying development captures may be re-evaluated")
    _require(original.get("result") == "fail",
             "input capture was not a failed evaluator result")
    _require(original.get("failure", {}).get("native_root") is None,
             "a native terminal failure cannot be re-evaluated as a pass")

    cleanup = original.get("cleanup", {})
    for field, expected in (
        ("requests_disarmed", True),
        ("diagnostic_flags_false", True),
        ("game_processes_remaining", 0),
        ("graceful_process_teardown", True),
        ("emergency_cleanup_used", False),
    ):
        _require(cleanup.get(field) == expected,
                 f"original runner cleanup proof failed: {field}")

    cycle_run_ids = original.get("peer_run_ids")
    _require(isinstance(cycle_run_ids, list) and len(cycle_run_ids) == 1,
             "correction capture must contain exactly one paired cycle")
    run_ids = cycle_run_ids[0]
    _require(isinstance(run_ids, dict)
             and all(isinstance(run_ids.get(peer), str)
                     for peer in ("host", "sandbox")),
             "paired peer run identities are missing")
    logs = _bound_pair_logs(original)
    _raise_on_native_terminal(logs, run_ids, "preserved capture evaluation")
    stimulus = _correction_stimulus_sequence_evidence(
        logs, run_ids, (11, 1, 6))
    _require(stimulus is not None,
             "preserved capture lacks the bilateral 11-1-6 stimulus sequence")
    corrections = _repeated_correction_evidence(logs, run_ids, 3)
    _require(corrections is not None and len(corrections) == 3,
             "preserved capture lacks three bilateral correction convergences")

    identities = original.get("identities")
    _require(isinstance(identities, dict), "capture producer identities are missing")
    output = {
        "report_schema": 2,
        "kind": "paired_online_development_correction_evaluation",
        "certifying": False,
        "result": "pass",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "case_id": original.get("case_id"),
        "display_map_name": original.get("display_map_name"),
        "stage_package_root": original.get("stage_package_root"),
        "fighter_order": original.get("fighter_order"),
        "renderer": original.get("renderer"),
        "peer_run_ids": cycle_run_ids,
        "evidence_reuse": {
            "input_report": str(input_report.resolve()),
            "input_report_sha256": sha256_file(input_report),
            "raw_log_artifacts": original["raw_log_artifacts"],
            "producer_identities": identities,
            "evaluator_runner_sha256": runner_sha256(
                root / "tools" / "deterministic_qualification"),
        },
        "runtime": {
            "correction_stimulus_sequence": stimulus,
            "bilateral_correction_convergence": corrections,
            "canonical_divergences": 0,
        },
        "cleanup": cleanup,
    }
    output_report.parent.mkdir(parents=True, exist_ok=True)
    write_report(output_report, output)
    return 0


def run_paired_correction_evaluation(args: Any, root: Path) -> int:
    return reevaluate_paired_correction_capture(
        args.input_report, args.report, root)
