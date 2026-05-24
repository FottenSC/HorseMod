#!/usr/bin/env python3
"""Consistency checks for generated SC6 movement evidence."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


RANKABLE_STATUSES = {"trusted_basic", "trusted_stance_basic"}


def _read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise FileNotFoundError(path)
    with path.open("r", newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def _has_score(value: str | None) -> bool:
    return value not in (None, "")


def validate(generated: Path) -> list[str]:
    errors: list[str] = []
    basic = _read_csv(generated / "basic_movement_routes.csv")
    audit = _read_csv(generated / "movement_route_audit.csv")
    quality = _read_csv(generated / "movement_quality_static.csv")
    cross_edges = _read_csv(generated / "cross_bank_direction_edges.csv")
    cross_resolution = _read_csv(generated / "cross_bank_route_resolution.csv")
    report_audit = _read_csv(generated / "report_value_audit.csv") if (generated / "report_value_audit.csv").exists() else []

    if not (len(basic) == len(audit) == len(quality)):
        errors.append(
            f"row count mismatch: basic={len(basic)} audit={len(audit)} quality={len(quality)}"
        )

    audit_by_key = {(r["cid"], r["movement_type"]): r for r in audit}
    quality_by_key = {(r["cid"], r["movement_type"]): r for r in quality}
    for row in basic:
        key = (row["cid"], row["movement_type"])
        audit_row = audit_by_key.get(key)
        quality_row = quality_by_key.get(key)
        if audit_row is None:
            errors.append(f"missing audit row for {key}")
            continue
        if quality_row is None:
            errors.append(f"missing quality row for {key}")
            continue
        for field in ("src_slot", "dst_slot", "animation_hex"):
            if row.get(field, "") != audit_row.get(field, ""):
                errors.append(f"basic/audit {field} mismatch for {key}")
        if row.get("trust_status") != audit_row.get("route_kind"):
            errors.append(f"basic/audit trust mismatch for {key}")
        if row.get("trust_status") != quality_row.get("route_kind"):
            errors.append(f"basic/quality trust mismatch for {key}")
        if _has_score(quality_row.get("quality_score")):
            if quality_row.get("route_kind") not in RANKABLE_STATUSES:
                errors.append(f"score on non-rankable status for {key}")
            if quality_row.get("root_decode_confidence") != "high":
                errors.append(f"score without high-confidence decode for {key}")
        if (
            row.get("recovery_trust_status", "").startswith("unresolved")
            and row.get("trust_status") == "trusted_basic_with_late_followup"
        ):
            errors.append(f"late-followup trusted despite unresolved recovery for {key}")

    if len(cross_edges) != len(cross_resolution):
        errors.append(
            f"cross-bank coverage mismatch: edges={len(cross_edges)} resolution={len(cross_resolution)}"
        )

    report_mismatches = [r for r in report_audit if r.get("status") != "match"]
    if report_mismatches:
        errors.append(f"report value mismatches: {len(report_mismatches)}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, default=Path("docs/investigations/generated"))
    args = parser.parse_args()
    errors = validate(args.generated)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print(f"Movement outputs validated: {args.generated}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
