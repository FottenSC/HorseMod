#!/usr/bin/env python3
"""Discover rollback C++ self-tests from CTest's authoritative registry."""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


ROLLBACK_LABEL = "rollback"


@dataclass(frozen=True)
class RollbackCTest:
    name: str
    command: tuple[str, ...]
    labels: frozenset[str]


class RollbackCTestError(RuntimeError):
    """Raised when the configured CTest registry is missing or malformed."""


def _property_value(test: dict[str, Any], name: str) -> Any:
    for prop in test.get("properties", []):
        if prop.get("name") == name:
            return prop.get("value")
    return None


def _as_labels(value: Any) -> frozenset[str]:
    if value is None:
        return frozenset()
    if isinstance(value, str):
        return frozenset(item for item in value.split(";") if item)
    if isinstance(value, list):
        return frozenset(str(item) for item in value)
    raise RollbackCTestError(f"unexpected CTest LABELS value: {value!r}")


def discover_rollback_selftests(
    build_dir: Path,
    *,
    configuration: str | None = None,
    ctest_executable: str = "ctest",
) -> list[RollbackCTest]:
    """Return every CTest entry labeled ``rollback`` in declaration order."""

    command = [
        ctest_executable,
        "--test-dir",
        str(build_dir),
        "--show-only=json-v1",
    ]
    if configuration:
        command.extend(["-C", configuration])
    proc = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        output = "\n".join(part for part in (proc.stdout, proc.stderr) if part)
        raise RollbackCTestError(
            f"CTest discovery failed with exit code {proc.returncode}:\n{output}"
        )

    try:
        document = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RollbackCTestError(f"CTest returned invalid JSON: {exc}") from exc

    discovered: list[RollbackCTest] = []
    names: set[str] = set()
    for raw_test in document.get("tests", []):
        labels = _as_labels(_property_value(raw_test, "LABELS"))
        if ROLLBACK_LABEL not in labels:
            continue

        name = str(raw_test.get("name", "")).strip()
        raw_command = raw_test.get("command")
        if not name or not isinstance(raw_command, list) or not raw_command:
            raise RollbackCTestError(
                f"malformed rollback CTest entry: {raw_test!r}"
            )
        if name in names:
            raise RollbackCTestError(f"duplicate rollback CTest entry: {name}")
        names.add(name)
        discovered.append(
            RollbackCTest(
                name=name,
                command=tuple(str(item) for item in raw_command),
                labels=labels,
            )
        )

    if not discovered:
        raise RollbackCTestError(
            f"no tests labeled {ROLLBACK_LABEL!r} in {build_dir}"
        )
    return discovered


def names(tests: Iterable[RollbackCTest]) -> list[str]:
    return [test.name for test in tests]
