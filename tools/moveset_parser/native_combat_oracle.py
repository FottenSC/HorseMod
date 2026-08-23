"""Canonicalise and compare SC6 native combat oracle JSONL traces.

The game trace contains clocks, process identifiers, ASLR addresses, and QPC
timestamps which are intentionally not simulation state.  This tool strips
only those process-local fields, preserves event order, and assigns every
remaining field to a first-divergence partition.  Unknown fields fail into the
``semantic_events`` partition rather than disappearing.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping


SCHEMA = "sc6-native-combat-oracle-v1"
REQUIRED_ARTIFACT_FIELDS = (
    "oracle_executable_sha256",
    "oracle_p1_asset_sha256",
    "oracle_p2_asset_sha256",
)
VOLATILE_FIELDS = frozenset(
    {
        "ts_qpc",
        "pid",
        "process_start_marker",
        "image_base",
        "thread_id",
        "return_address",
    }
)


PARTITION_TOKENS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("artifact_scope", ("oracle_", "build", "scope_")),
    ("input", ("input", "command", "history")),
    ("movevm_lane", ("movevm", "lane", "slot", "script", "transition", "subvm", "sched")),
    ("collision_pose", ("pose", "bone", "matrix", "clip", "collision", "khit", "hurtbox", "weapon")),
    ("health_damage_reaction", ("health", "vital", "damage", "reaction", "guard", "hit_state", "stun")),
    ("kinematics", ("position", "velocity", "facing", "root_motion", "one_shot", "step_", "coord_")),
    ("gameplay_rng", ("rng", "lfsr", "xorshift")),
)


@dataclass(frozen=True)
class Divergence:
    record_index: int
    event_left: str | None
    event_right: str | None
    partition: str
    field: str
    left: object
    right: object


def read_jsonl(path: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        value = json.loads(line)
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{line_number}: oracle record is not an object")
        records.append(value)
    return records


def partition_for(field: str) -> str:
    lowered = field.lower()
    for partition, tokens in PARTITION_TOKENS:
        if any(token in lowered for token in tokens):
            return partition
    return "semantic_events"


def canonical_record(record: Mapping[str, object]) -> dict[str, object]:
    return {
        key: value
        for key, value in sorted(record.items())
        if key not in VOLATILE_FIELDS
    }


def canonical_bytes(records: Iterable[Mapping[str, object]]) -> bytes:
    return b"".join(
        (
            json.dumps(
                canonical_record(record),
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=True,
            )
            + "\n"
        ).encode("ascii")
        for record in records
    )


def validate_oracle_boundary(records: Iterable[Mapping[str, object]]) -> None:
    roots = [
        record
        for record in records
        if record.get("event") == "native_battle_tick_root_transaction"
    ]
    if not roots:
        raise ValueError("trace has no native battle tick-root transactions")
    for index, record in enumerate(roots):
        missing = [field for field in REQUIRED_ARTIFACT_FIELDS if not record.get(field)]
        if missing:
            raise ValueError(f"root record {index} lacks artifact binding: {missing}")
        if record.get("oracle_scope_admitted") is not True:
            raise ValueError(f"root record {index} is outside the frozen oracle scope")


def first_divergence(
    left: list[Mapping[str, object]], right: list[Mapping[str, object]]
) -> Divergence | None:
    for index in range(max(len(left), len(right))):
        if index >= len(left) or index >= len(right):
            return Divergence(
                index,
                left[index].get("event") if index < len(left) else None,
                right[index].get("event") if index < len(right) else None,
                "semantic_events",
                "record_count",
                len(left),
                len(right),
            )
        a = canonical_record(left[index])
        b = canonical_record(right[index])
        if a == b:
            continue
        for field in sorted(set(a) | set(b)):
            if a.get(field) != b.get(field):
                return Divergence(
                    index,
                    a.get("event") if isinstance(a.get("event"), str) else None,
                    b.get("event") if isinstance(b.get("event"), str) else None,
                    partition_for(field),
                    field,
                    a.get(field),
                    b.get(field),
                )
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path, nargs="?")
    parser.add_argument("--canonical-output", type=Path)
    args = parser.parse_args()

    left = read_jsonl(args.left)
    validate_oracle_boundary(left)
    if args.canonical_output:
        args.canonical_output.write_bytes(canonical_bytes(left))
    if args.right is None:
        print(json.dumps({"schema": SCHEMA, "records": len(left), "valid": True}))
        return 0

    right = read_jsonl(args.right)
    validate_oracle_boundary(right)
    difference = first_divergence(left, right)
    result = {
        "schema": SCHEMA,
        "byte_identical": canonical_bytes(left) == canonical_bytes(right),
        "first_divergence": None if difference is None else difference.__dict__,
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if difference is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
