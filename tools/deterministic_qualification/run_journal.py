"""Atomic stage completion and recovery barriers for interrupted development runs."""
from __future__ import annotations

import json
import os
import time
import sys
from pathlib import Path
from typing import Any, Callable

from .artifacts import sha256_file


def atomic_json(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(document, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    # The deployment lock already excludes another runner. Windows readers
    # (including file scanners) may briefly deny delete sharing on a journal.
    # Retain both the old durable record and the flushed replacement while
    # retrying only those specific errors; never unlink the recovery barrier.
    for attempt in range(6):
        try:
            os.replace(temporary, path)
            if attempt:
                print(f"journal replacement recovered after {attempt + 1} attempts: {path}", file=sys.stderr)
            return
        except OSError as error:
            if getattr(error, "winerror", None) not in (5, 32, 33) or attempt == 5:
                raise
            time.sleep(0.02 * 2 ** attempt)


def require_sealed_report(path: Path) -> dict[str, Any]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("result") != "pass":
        raise RuntimeError(f"stage did not pass: {path}")
    logs = document.get("raw_log_artifacts", document.get("artifacts", {}).get("raw_logs", {}))
    if not logs:
        # Paired reports store the capture inventory under identities.
        logs = document.get("identities", {}).get("raw_log_artifacts", {})
    if not isinstance(logs, dict) or not logs:
        raise RuntimeError("stage has no sealed raw-log artifacts")
    for row in logs.values():
        artifact = Path(row.get("path", ""))
        if not artifact.is_file() or sha256_file(artifact) != row.get("sha256"):
            raise RuntimeError("stage raw-log artifact is missing or stale")
    cleanup = document.get("cleanup", {})
    if (cleanup.get("requests_disarmed") is not True
            or cleanup.get("deployment_restored") is not True
            or cleanup.get("diagnostic_flags_false") is not True
            or cleanup.get("game_processes_remaining") != 0
            or cleanup.get("cleanup_errors")):
        raise RuntimeError("stage cleanup is incomplete")
    return document


class StageJournal:
    def __init__(self, path: Path):
        self.path = path
        self.document = (json.loads(path.read_text(encoding="utf-8")) if path.exists()
                         else {"schema_version": 1, "stages": {}})
        if self.document.get("schema_version") != 1:
            raise RuntimeError("unsupported run journal")

    def save(self) -> None:
        atomic_json(self.path, self.document)

    def recover(self, cleanup: Callable[[], None], *, deployment_journal: Path | None = None,
                additional_journals: tuple[Path, ...] = ()) -> None:
        pending = [row for row in self.document["stages"].values()
                   if row["state"] in ("running", "cleanup_pending")]
        if deployment_journal is not None:
            from .run_resources import acquire_deployment_lock, RunResources
            # Legacy stage journals describe the same installation. Their old
            # output-directory locks cannot exclude a current deployment owner.
            with acquire_deployment_lock(deployment_journal):
                paths = [deployment_journal, *additional_journals,
                         *(Path(row["resource_journal"]) for row in pending
                           if row.get("resource_journal"))]
                for path in dict.fromkeys(path.resolve() for path in paths):
                    RunResources(path).recover()
                cleanup()
                for row in pending:
                    row["state"] = "interrupted"
                if pending:
                    self.save()
            return
        if not pending:
            return
        from .run_resources import recover_resources
        for row in pending:
            if row.get("resource_journal"):
                recover_resources(Path(row["resource_journal"]))
        # Recovery is a barrier. No new stage can start if cleanup cannot prove
        # the interrupted owner has released its resources.
        cleanup()
        for row in pending:
            row["state"] = "interrupted"
        self.save()

    def run(self, name: str, specification: dict[str, Any],
            dependencies: dict[str, Any], report: Path,
            execute: Callable[[], None], cleanup: Callable[[], None],
            resource_journal: Path | None = None,
            experiment: dict[str, Any] | None = None) -> bool:
        stages = self.document["stages"]
        if any(row["state"] in ("running", "cleanup_pending") for row in stages.values()):
            raise RuntimeError("interrupted stage requires cleanup recovery first")
        previous = stages.get(name, {})
        if experiment is not None:
            if any(not experiment.get(key) for key in (
                    "first_divergent_boundary", "competing_explanations", "new_evidence", "stop_condition")):
                raise RuntimeError("experiment must name the boundary, explanations, new evidence and stop condition")
        if (previous.get("state") == "failed"
                and previous.get("specification") == specification
                and previous.get("dependencies") == dependencies
                and (not experiment or previous.get("experiment") == experiment)):
            raise RuntimeError("unchanged failed live stage requires a fix or a new discriminating experiment")
        if (previous.get("state") == "completed"
                and previous.get("specification") == specification
                and previous.get("dependencies") == dependencies
                and report.is_file() and previous.get("report_sha256") == sha256_file(report)):
            require_sealed_report(report)
            cleanup()
            return False
        row = {"specification": specification, "dependencies": dependencies,
               "report": str(report.resolve()), "state": "running",
               "resource_journal": str(resource_journal.resolve()) if resource_journal else None,
               "experiment": experiment}
        stages[name] = row
        self.save()
        error: BaseException | None = None
        try:
            execute()
        except BaseException as caught:
            error = caught
            row["failure"] = str(caught)
        finally:
            row["state"] = "cleanup_pending"
            self.save()
            cleanup()  # A failure here intentionally preserves recovery state.
        if error is not None:
            row["state"] = "failed"
            self.save()
            raise error
        try:
            require_sealed_report(report)
        except BaseException as caught:
            row.update(state="failed", failure=str(caught))
            self.save()
            raise
        row.update(state="completed", report_sha256=sha256_file(report))
        self.save()
        return True
