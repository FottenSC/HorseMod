from __future__ import annotations

import os
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator


DIAGNOSTIC_DEFAULTS = {
    "trace": "false",
    "correction_probe": "false",
    "forced_depth7_qualification": "false",
    "qualification_depth": "7",
    "qualification_location": "2",
}

CONFIG_FIELD_ORDER = (
    "config_version", "enabled", "rollback_window", "input_delay", "trace",
    "correction_probe", "forced_depth7_qualification",
    "qualification_depth", "qualification_location",
)


def expected_fields(*, enabled: bool, trace: bool,
                    forced_depth7: bool = False, depth: int = 7,
                    location: int = 2) -> dict[str, str]:
    return {
        "config_version": "1",
        "enabled": str(enabled).lower(),
        "rollback_window": "12",
        "input_delay": "1",
        "trace": str(trace).lower(),
        "correction_probe": "false",
        "forced_depth7_qualification": str(forced_depth7).lower(),
        "qualification_depth": str(depth),
        "qualification_location": str(location),
    }


def is_exact_contract(fields: object, expected: dict[str, str]) -> bool:
    return (isinstance(fields, dict) and tuple(fields) == CONFIG_FIELD_ORDER
            and fields == expected)


def read_fields(path: Path) -> dict[str, str]:
    lines, positions = _parse(path)
    return {
        key: lines[index].partition("=")[2].strip().casefold()
        for key, index in positions.items()
    }


def require_disarmed(path: Path) -> None:
    fields = read_fields(path)
    expected = {"enabled": "false", **DIAGNOSTIC_DEFAULTS}
    mismatches = {
        key: fields.get(key) for key, value in expected.items()
        if fields.get(key) != value
    }
    if mismatches:
        raise RuntimeError(
            f"rollback config did not verify disarmed: {mismatches}")


def _parse(path: Path) -> tuple[list[str], dict[str, int]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    positions: dict[str, int] = {}
    for index, line in enumerate(lines):
        key, separator, _ = line.partition("=")
        if separator:
            folded = key.strip().casefold()
            if folded in positions:
                raise RuntimeError(f"duplicate rollback config key: {folded}")
            positions[folded] = index
    return lines, positions


def write_fields(path: Path, values: dict[str, str]) -> None:
    lines, positions = _parse(path)
    for key, value in values.items():
        if "\n" in value or "\r" in value:
            raise RuntimeError("rollback config value contains a newline")
        folded = key.casefold()
        row = f"{folded}={value}"
        if folded in positions:
            lines[positions[folded]] = row
        else:
            positions[folded] = len(lines)
            lines.append(row)
    temporary = path.with_suffix(path.suffix + ".qualification.tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def disarm_diagnostics(path: Path) -> None:
    write_fields(path, {"enabled": "false", **DIAGNOSTIC_DEFAULTS})
    require_disarmed(path)


@contextmanager
def armed_correction(path: Path, depth: int, location: int) -> Iterator[None]:
    if depth not in (1, 6, 7, 11) or location not in (1, 2, 3, 4):
        raise RuntimeError("invalid correction depth/location")
    write_fields(path, {
        "enabled": "false",
        "trace": "true",
        "correction_probe": "false",
        "forced_depth7_qualification": "true",
        "qualification_depth": str(depth),
        "qualification_location": str(location),
    })
    try:
        yield
    finally:
        disarm_diagnostics(path)


@contextmanager
def armed_baseline(path: Path) -> Iterator[None]:
    write_fields(path, {
        # Trace installs the deterministic replay hooks without arming the
        # production online coordinator. Production remains disabled until
        # immutable release publication has admitted the exact allowlist.
        "enabled": "false",
        "trace": "true",
        "correction_probe": "false",
        "forced_depth7_qualification": "false",
        "qualification_depth": "7",
        "qualification_location": "2",
    })
    try:
        yield
    finally:
        disarm_diagnostics(path)
