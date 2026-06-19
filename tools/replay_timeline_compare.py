#!/usr/bin/env python3
"""Compare generated HorseMod replay oracle timelines.

The normal-vs-lux-no-render validation wants gameplay-authoritative state to
match frame-for-frame, while ignoring trace metadata and run-specific pointers.
This script compares `oracle_frame` JSONL events by `seq` and exits non-zero on
the first semantic drift.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
import re
import sys
from pathlib import Path
from typing import Any


TRACE_METADATA = {"event", "name", "ts_qpc", "thread_id", "build", "image_base"}
POINTER_FIELD_RE = re.compile(
    r"_(?:chara|sc_provider_table|active_lane_cursor|restore_ptr_[0-9a-f]+)$"
    r"|_(?:primary_attack_cell|opponent_active_attack_cell_copy"
    r"|opponent_non_attack_move_descr_copy|own_active_attack_cell"
    r"|non_attack_move_descr|active_attack_cell)$"
)


@dataclass
class OracleSegment:
    mode: str
    frames: dict[int, dict[str, Any]]
    complete: dict[str, Any] | None = None


def event_name(event: dict[str, Any]) -> str:
    return str(event.get("event") or event.get("name") or "")


def load_oracle_segments(path: Path) -> list[OracleSegment]:
    segments: list[OracleSegment] = []
    current: OracleSegment | None = None
    loose_frames: dict[int, dict[str, Any]] = {}
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                print(f"warning: {path}:{line_no}: skipped malformed JSON: {exc}")
                continue
            if not isinstance(event, dict):
                continue
            name = event_name(event)
            if name == "generate_start":
                if current is not None and current.frames:
                    segments.append(current)
                current = OracleSegment(
                    mode=str(event.get("mode") or "?"),
                    frames={},
                )
                continue
            if name == "generate_complete":
                if current is not None:
                    current.complete = event
                    segments.append(current)
                    current = None
                continue
            if name != "oracle_frame":
                continue
            try:
                seq = int(event["seq"])
            except (KeyError, TypeError, ValueError):
                print(f"warning: {path}:{line_no}: oracle_frame missing seq")
                continue
            if current is not None:
                current.frames[seq] = event
            else:
                loose_frames[seq] = event
    if current is not None and current.frames:
        segments.append(current)
    if not segments and loose_frames:
        segments.append(OracleSegment(mode="unknown", frames=loose_frames))
    return segments


def choose_segment(
    path: Path,
    segments: list[OracleSegment],
    mode: str | None,
    occurrence: str,
) -> OracleSegment:
    candidates = segments
    if mode:
        candidates = [segment for segment in segments if segment.mode == mode]
    if not candidates:
        known = ", ".join(
            f"{idx}:{segment.mode}/{len(segment.frames)}"
            for idx, segment in enumerate(segments)
        )
        raise ValueError(
            f"{path}: no oracle segment for mode {mode!r}; segments: {known}"
        )
    if occurrence == "first":
        return candidates[0]
    if occurrence == "last":
        return candidates[-1]
    try:
        return candidates[int(occurrence)]
    except (ValueError, IndexError) as exc:
        raise ValueError(
            f"{path}: invalid occurrence {occurrence!r} for mode {mode!r}"
        ) from exc


def comparable_key(key: str) -> bool:
    if key in TRACE_METADATA:
        return False
    return POINTER_FIELD_RE.search(key) is None


def canonical(value: Any) -> Any:
    if isinstance(value, str) and value.startswith("0x"):
        return value.lower()
    return value


def values_match(a: Any, b: Any, float_tolerance: float) -> bool:
    a = canonical(a)
    b = canonical(b)
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        if isinstance(a, bool) or isinstance(b, bool):
            return bool(a) == bool(b)
        if isinstance(a, float) or isinstance(b, float):
            if not math.isfinite(float(a)) or not math.isfinite(float(b)):
                return a == b
            return abs(float(a) - float(b)) <= float_tolerance
        return int(a) == int(b)
    return a == b


def describe_value(value: Any) -> str:
    text = repr(canonical(value))
    return text if len(text) <= 120 else text[:117] + "..."


def compare(
    normal: dict[int, dict[str, Any]],
    fast: dict[int, dict[str, Any]],
    float_tolerance: float,
    max_examples: int,
) -> tuple[int, list[str]]:
    mismatches = 0
    examples: list[str] = []

    normal_seqs = set(normal)
    fast_seqs = set(fast)
    for seq in sorted(normal_seqs - fast_seqs):
        mismatches += 1
        if len(examples) < max_examples:
            examples.append(f"seq {seq}: missing from fast timeline")
    for seq in sorted(fast_seqs - normal_seqs):
        mismatches += 1
        if len(examples) < max_examples:
            examples.append(f"seq {seq}: extra in fast timeline")

    for seq in sorted(normal_seqs & fast_seqs):
        lhs = normal[seq]
        rhs = fast[seq]
        keys = sorted(
            key for key in (set(lhs) | set(rhs))
            if comparable_key(key)
        )
        for key in keys:
            if key not in lhs or key not in rhs:
                mismatches += 1
                if len(examples) < max_examples:
                    side = "normal" if key not in lhs else "fast"
                    examples.append(f"seq {seq}: field {key} missing in {side}")
                continue
            if values_match(lhs[key], rhs[key], float_tolerance):
                continue
            mismatches += 1
            if len(examples) < max_examples:
                examples.append(
                    "seq "
                    f"{seq}: field {key} normal={describe_value(lhs[key])} "
                    f"fast={describe_value(rhs[key])}"
                )
    return mismatches, examples


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("normal_trace", type=Path)
    parser.add_argument("fast_trace", type=Path)
    parser.add_argument("--normal-mode", default="normal")
    parser.add_argument("--fast-mode", default="lux-no-render")
    parser.add_argument("--normal-occurrence", default="first")
    parser.add_argument("--fast-occurrence", default="last")
    parser.add_argument("--float-tolerance", type=float, default=1e-5)
    parser.add_argument("--max-examples", type=int, default=20)
    args = parser.parse_args()

    normal_segments = load_oracle_segments(args.normal_trace)
    fast_segments = load_oracle_segments(args.fast_trace)
    if not normal_segments:
        print(f"error: no oracle_frame segments in normal trace {args.normal_trace}")
        return 2
    if not fast_segments:
        print(f"error: no oracle_frame segments in fast trace {args.fast_trace}")
        return 2

    try:
        normal_segment = choose_segment(
            args.normal_trace,
            normal_segments,
            args.normal_mode,
            args.normal_occurrence,
        )
        fast_segment = choose_segment(
            args.fast_trace,
            fast_segments,
            args.fast_mode,
            args.fast_occurrence,
        )
    except ValueError as exc:
        print(f"error: {exc}")
        return 2

    mismatches, examples = compare(
        normal_segment.frames,
        fast_segment.frames,
        args.float_tolerance,
        max(1, args.max_examples),
    )
    common = len(set(normal_segment.frames) & set(fast_segment.frames))
    print(
        "timeline compare: "
        f"normal_mode={normal_segment.mode} fast_mode={fast_segment.mode} "
        f"normal_frames={len(normal_segment.frames)} "
        f"fast_frames={len(fast_segment.frames)} "
        f"common={common} mismatch_count={mismatches}"
    )
    for example in examples:
        print(f"  {example}")
    return 0 if mismatches == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
