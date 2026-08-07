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


TRACE_METADATA = {
    "event",
    "name",
    "ts_qpc",
    "thread_id",
    "pid",
    "process_start_marker",
    "build",
    "image_base",
    "wall_tag",
}
POINTER_FIELD_RE = re.compile(
    r"_(?:chara|sc_provider_table|active_lane_cursor|restore_ptr_[0-9a-f]+)$"
    r"|_(?:primary_attack_cell|opponent_active_attack_cell_copy"
    r"|opponent_non_attack_move_descr_copy|own_active_attack_cell"
    r"|non_attack_move_descr|active_attack_cell)$"
    r"|_ai_palette_(?:active_motion_bank|key_buffer)_rva$"
)
DIAGNOSTIC_FIELD_RE = re.compile(r"_hit_cue_pose_pack_hash$")
ORACLE_SCHEMA_VERSION = 12
PRESENTATION_FIELDS = {
    "oracle_presentation_hash",
    "breakable_presentation_digest",
    "stage_wind_presentation_hash",
    "stage_wind_root_outputs_hash",
    "stage_wind_canonical_hash",
    "stage_wind_combined_rng_hash",
    "stage_wind_emitter_hash",
    "stage_wind_root_scheduler_hash",
    "stage_wind_graph_nodes_hash",
    "stage_wind_graph_hash",
    "stage_wind_output_active",
    "stage_wind_active_bank",
}
LEGACY_FIELDS = {
    "oracle_gameplay_hash_v1",
    "oracle_gameplay_hash_v5",
    "oracle_gameplay_hash",
}
RAW_INPUT_FIELDS = {"p1_input", "p2_input"}


def presentation_field(key: str) -> bool:
    if key in PRESENTATION_FIELDS \
            or key.startswith("stage_wind_output_force_") \
            or re.fullmatch(r"stage_wind_node_\d+_.*", key) \
            or re.fullmatch(r"stage_wind_emitter_\d+_word_\d+", key):
        return True
    return False


def allocator_residue_field(key: str) -> bool:
    node = re.fullmatch(r"stage_wind_node_\d+_word_(\d+)", key)
    if node and 0x34 <= int(node.group(1)) < 0x40:
        return True
    emitter = re.fullmatch(r"stage_wind_emitter_\d+_word_(\d+)", key)
    return bool(emitter and int(emitter.group(1)) in {0x6C, 0x7C})


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


def comparable_key(key: str, authority: str) -> bool:
    if key in TRACE_METADATA:
        return False
    if DIAGNOSTIC_FIELD_RE.search(key):
        return False
    if POINTER_FIELD_RE.search(key) is not None:
        return False
    if allocator_residue_field(key):
        return False
    if authority == "presentation":
        return presentation_field(key) \
            or key == "oracle_schema_version"
    if authority == "gameplay" \
            and (presentation_field(key) or key in LEGACY_FIELDS):
        return False
    return True


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
    authority: str = "gameplay",
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
            if comparable_key(key, authority)
        )
        for key in keys:
            if key not in lhs or key not in rhs:
                mismatches += 1
                if len(examples) < max_examples:
                    side = "normal" if key not in lhs else "fast"
                    examples.append(f"seq {seq}: field {key} missing in {side}")
                continue
            lhs_value = lhs[key]
            rhs_value = rhs[key]
            if authority == "gameplay" and key in RAW_INPUT_FIELDS:
                try:
                    lhs_value = int(lhs_value) & 0xFFFFFFFF
                    rhs_value = int(rhs_value) & 0xFFFFFFFF
                except (TypeError, ValueError):
                    pass
            if values_match(lhs_value, rhs_value, float_tolerance):
                continue
            mismatches += 1
            if len(examples) < max_examples:
                examples.append(
                    "seq "
                    f"{seq}: field {key} "
                    f"normal={describe_value(lhs_value)} "
                    f"fast={describe_value(rhs_value)}"
                )
    return mismatches, examples


def validate_segment_schema(
    path: Path,
    segment: OracleSegment,
) -> None:
    versions = {
        frame.get("oracle_schema_version")
        for frame in segment.frames.values()
    }
    if versions != {ORACLE_SCHEMA_VERSION}:
        raise ValueError(
            f"{path}: oracle schema mismatch; expected "
            f"{ORACLE_SCHEMA_VERSION}, observed {sorted(map(str, versions))}"
        )
    required = {
        "oracle_gameplay_hash_v12",
        "oracle_presentation_hash",
        "breakable_presentation_digest",
        "stage_wind_gameplay_hash",
        "chara_animation_gameplay_digest",
        "stage_wind_presentation_hash",
    }
    for seq, frame in segment.frames.items():
        missing = sorted(required - set(frame))
        if missing:
            raise ValueError(
                f"{path}: seq {seq} missing oracle v2 fields: "
                f"{', '.join(missing)}"
            )


def selftest() -> int:
    base = {
        "oracle_schema_version": ORACLE_SCHEMA_VERSION,
        "oracle_gameplay_hash_v1": "0x10",
        "oracle_gameplay_hash_v12": "0x20",
        "stage_wind_gameplay_hash": "0x25",
        "chara_animation_gameplay_digest": "0x24",
        "stage_wind_canonical_hash": "0x26",
        "stage_wind_presentation_hash": "0x50",
        "oracle_presentation_hash": "0x30",
        "breakable_presentation_digest": "0x40",
        "p1_input": 0x400,
        "p2_input": 0x800,
        "rng_lfsr_index": 5,
        "p1_ai_palette_active_motion_bank_rva": "0x100",
        "stage_wind_node_0_word_52": "0xaaaa",
        "stage_wind_emitter_0_word_108": "0xbbbb",
    }
    no_render = dict(base)
    no_render["p1_input"] |= 0x40000000000
    no_render["oracle_gameplay_hash_v1"] = "0x11"
    no_render["oracle_presentation_hash"] = "0x31"
    no_render["breakable_presentation_digest"] = "0x41"
    no_render["stage_wind_canonical_hash"] = "0x27"
    gameplay_mismatches, _ = compare(
        {0: base}, {0: no_render}, 0.0, 8, "gameplay")
    presentation_mismatches, _ = compare(
        {0: base}, {0: no_render}, 0.0, 8, "presentation")
    low_input = dict(no_render)
    low_input["p1_input"] ^= 1
    low_input_mismatches, _ = compare(
        {0: base}, {0: low_input}, 0.0, 8, "gameplay")
    wind_gameplay = dict(no_render)
    wind_gameplay["stage_wind_gameplay_hash"] = "0x28"
    wind_gameplay_mismatches, _ = compare(
        {0: base}, {0: wind_gameplay}, 0.0, 8, "gameplay")
    residue_only = dict(base)
    residue_only["p1_ai_palette_active_motion_bank_rva"] = "0x200"
    residue_only["stage_wind_node_0_word_52"] = "0xcccc"
    residue_only["stage_wind_emitter_0_word_108"] = "0xdddd"
    residue_gameplay_mismatches, _ = compare(
        {0: base}, {0: residue_only}, 0.0, 8, "gameplay")
    residue_presentation_mismatches, _ = compare(
        {0: base}, {0: residue_only}, 0.0, 8, "presentation")
    if gameplay_mismatches != 0 \
            or presentation_mismatches == 0 \
            or low_input_mismatches == 0 \
            or wind_gameplay_mismatches == 0 \
            or residue_gameplay_mismatches != 0 \
            or residue_presentation_mismatches != 0:
        raise AssertionError(
            "timeline authority split negative controls failed")
    try:
        invalid = dict(base)
        invalid["oracle_schema_version"] = 1
        validate_segment_schema(
            Path("invalid"),
            OracleSegment("normal", {0: invalid}),
        )
    except ValueError:
        pass
    else:
        raise AssertionError("mixed oracle schema was accepted")
    print(
        "replay timeline compare self-test passed "
        f"schema={ORACLE_SCHEMA_VERSION}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("normal_trace", type=Path, nargs="?")
    parser.add_argument("fast_trace", type=Path, nargs="?")
    parser.add_argument("--normal-mode", default="normal")
    parser.add_argument("--fast-mode", default="lux-no-render")
    parser.add_argument("--normal-occurrence", default="first")
    parser.add_argument("--fast-occurrence", default="last")
    parser.add_argument("--float-tolerance", type=float, default=1e-5)
    parser.add_argument("--max-examples", type=int, default=20)
    parser.add_argument(
        "--authority",
        choices=("gameplay", "presentation", "all"),
        default="gameplay",
        help="comparison authority; no-render qualification uses gameplay",
    )
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.normal_trace is None or args.fast_trace is None:
        parser.error("normal_trace and fast_trace are required")

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
        validate_segment_schema(args.normal_trace, normal_segment)
        validate_segment_schema(args.fast_trace, fast_segment)
    except ValueError as exc:
        print(f"error: {exc}")
        return 2

    mismatches, examples = compare(
        normal_segment.frames,
        fast_segment.frames,
        args.float_tolerance,
        max(1, args.max_examples),
        args.authority,
    )
    common = len(set(normal_segment.frames) & set(fast_segment.frames))
    print(
        "timeline compare: "
        f"normal_mode={normal_segment.mode} fast_mode={fast_segment.mode} "
        f"authority={args.authority} schema={ORACLE_SCHEMA_VERSION} "
        f"normal_frames={len(normal_segment.frames)} "
        f"fast_frames={len(fast_segment.frames)} "
        f"common={common} mismatch_count={mismatches}"
    )
    for example in examples:
        print(f"  {example}")
    return 0 if mismatches == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
