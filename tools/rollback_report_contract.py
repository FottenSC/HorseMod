#!/usr/bin/env python3
"""Shared fail-closed report contract for rollback automation."""

from __future__ import annotations

import hashlib
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 2
VERDICTS = {"pass", "fail", "incomplete", "setup-ready"}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def artifact(path: Path) -> dict[str, Any]:
    return {
        "path": str(path),
        "exists": path.is_file(),
        "sha256": sha256_file(path) if path.is_file() else "",
        "size": path.stat().st_size if path.is_file() else 0,
    }


def coverage(
    required: Iterable[str], observed: Iterable[str]
) -> dict[str, Any]:
    required_list = list(dict.fromkeys(str(item) for item in required))
    observed_list = list(dict.fromkeys(str(item) for item in observed))
    observed_set = set(observed_list)
    missing = [item for item in required_list if item not in observed_set]
    return {
        "required": required_list,
        "observed": observed_list,
        "missing": missing,
        "complete": not missing,
    }


def verdict(*, workflow_ok: bool, coverage_complete: bool,
            setup_only: bool = False) -> str:
    if not workflow_ok:
        return "fail"
    if setup_only:
        return "setup-ready"
    return "pass" if coverage_complete else "incomplete"


def contract_fields(
    *, workflow_kind: str, workflow_ok: bool,
    coverage_result: dict[str, Any], setup_only: bool = False,
    acceptance_workflow: bool = False,
) -> dict[str, Any]:
    complete = bool(coverage_result.get("complete"))
    result = verdict(
        workflow_ok=workflow_ok,
        coverage_complete=complete,
        setup_only=setup_only,
    )
    acceptance_executed = acceptance_workflow and complete and not setup_only
    return {
        "schema_version": SCHEMA_VERSION,
        "workflow_kind": workflow_kind,
        "verdict": result,
        "workflow_ok": bool(workflow_ok),
        "coverage_complete": complete,
        "coverage": coverage_result,
        "acceptance_executed": acceptance_executed,
        "acceptance_ok": bool(workflow_ok) if acceptance_executed else None,
    }


def validate_v2(report: Any, *, workflow_kind: str | None = None) -> list[str]:
    failures: list[str] = []
    if not isinstance(report, dict):
        return ["report is not a JSON object"]
    if report.get("schema_version") != SCHEMA_VERSION:
        failures.append(f"schema_version must be {SCHEMA_VERSION}")
    if workflow_kind and report.get("workflow_kind") != workflow_kind:
        failures.append(f"workflow_kind must be {workflow_kind}")
    if report.get("verdict") not in VERDICTS:
        failures.append("verdict is missing or invalid")
    if not isinstance(report.get("coverage_complete"), bool):
        failures.append("coverage_complete must be boolean")
    if not isinstance(report.get("workflow_ok"), bool):
        failures.append("workflow_ok must be boolean")
    if "acceptance_executed" not in report or "acceptance_ok" not in report:
        failures.append("acceptance fields are missing")
    coverage_value = report.get("coverage")
    if not isinstance(coverage_value, dict):
        failures.append("coverage must be an object")
        return failures
    required = coverage_value.get("required")
    observed = coverage_value.get("observed")
    missing = coverage_value.get("missing")
    if not all(isinstance(value, list)
               for value in (required, observed, missing)):
        failures.append("coverage required/observed/missing must be arrays")
        return failures
    recomputed = coverage(required, observed)
    if missing != recomputed["missing"]:
        failures.append("coverage missing list is inconsistent")
    if coverage_value.get("complete") != recomputed["complete"]:
        failures.append("coverage complete flag is inconsistent")
    if report.get("coverage_complete") != recomputed["complete"]:
        failures.append("top-level coverage_complete is inconsistent")
    workflow_ok = report.get("workflow_ok")
    if isinstance(workflow_ok, bool):
        setup_only = bool(report.get("setup_only"))
        if (report.get("verdict") == "setup-ready") != setup_only:
            failures.append("setup-ready verdict is inconsistent")
        if setup_only and report.get("workflow_kind") != "release-gate":
            failures.append("setup-only is valid only for release-gate")
        expected_verdict = verdict(
            workflow_ok=workflow_ok,
            coverage_complete=recomputed["complete"],
            setup_only=setup_only,
        )
        if report.get("verdict") != expected_verdict:
            failures.append("verdict is inconsistent with workflow/coverage")
        acceptance_workflow = report.get("workflow_kind") in {
            "two-client-acceptance", "release-gate"
        }
        expected_executed = (
            acceptance_workflow
            and recomputed["complete"]
            and not setup_only
        )
        if report.get("acceptance_executed") is not expected_executed:
            failures.append("acceptance_executed is inconsistent")
        expected_acceptance_ok = workflow_ok if expected_executed else None
        if report.get("acceptance_ok") is not expected_acceptance_ok:
            failures.append("acceptance_ok is inconsistent")
    return failures
