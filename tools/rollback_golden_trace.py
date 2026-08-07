#!/usr/bin/env python3
"""Approve and validate immutable rollback replay golden traces.

Approval is an explicit maintenance operation. Ordinary validation only reads
the manifest and never updates it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 4
ORACLE_SCHEMA_VERSION = 12
RAW_ORACLE_CANONICALIZATION = "trusted-common-gameplay-json-sha256-v10"
UNSTABLE_RAW_ORACLE_CANONICALIZATION = \
    "trusted-common-gameplay-json-sha256-v5"
LEGACY_CANONICALIZATION = "normalized-oracle-gameplay-hash-v1"
DEFAULT_MANIFEST = (
    Path(__file__).resolve().parent / "rollback_goldens" / "manifest.json"
)

RAW_ORACLE_TOP_LEVEL_FIELDS = (
    "oracle_schema_version",
    "oracle_gameplay_hash_v12",
    "stage_wind_gameplay_hash",
    "chara_animation_gameplay_digest",
    "round",
    "master",
    "last_round_result",
    "rng_state",
    "rng_readable",
    "rng_lfsr_hash",
    "rng_lfsr_index",
    "rng_gameplay_crt_present",
    "rng_gameplay_crt_state",
    "rng_gameplay_crt_seed",
    "rng_gameplay_crt_draw_ordinal",
    "valid",
)
RAW_ORACLE_PLAYER_FIELDS = (
    "chara",
    "readable",
    "primary_header_state8c",
    "active_lane_cursor",
    "pos_x", "pos_y", "pos_z",
    "vel_x", "vel_y", "vel_z",
    "ground_vel_x", "ground_vel_y", "ground_vel_z",
    "one_shot_x", "one_shot_z",
    "expected_motion_x", "expected_motion_z",
    "frame_delta_x", "frame_delta_z",
    "hit_slide_slot", "latched_hit_slide_input_dir",
    "hit_pushback_x", "hit_pushback_z", "hit_pushback_decay_scale",
    "root_motion_blend_weight",
    "move_time_scale_a", "move_time_scale_b",
    "movevm_time_dilation_scalar",
    "facing", "facing_retrack_readable", "facing_retrack_hash",
    "facing_retrack_mode", "facing_retrack_frames_remain",
    "facing_retrack_current_weight", "facing_retrack_target_weight",
    "facing_retrack_step_per_frame",
    "vm_decay", "vital_scale", "vital_candidate", "vital_ko_gate",
    "vital_displayed", "vital_category_bits", "vital_state",
    "move_id", "move_frame", "clip_frame", "camera_range_active",
    "move_transition_state", "hit_slide_state", "match_state_sub_a",
    "hit_slide_frame_timer", "hit_slide_frame_timer_mirror",
    "hit_reaction_result", "damped_motion_mode_16fb", "airborne_16e2",
    "look_at_blend_frame_counter", "hit_root_motion_gate",
    "hitcue0_active_cue", "hitcue1_active_cue",
    "hitcue2_active_cue", "hitcue3_active_cue",
    "hit_state",
    "non_attack_move_descr",
    "opponent_non_attack_move_descr_copy",
    "primary_attack_cell_mask_readable", "primary_attack_cell_mask",
    "opponent_active_attack_cell_mask_readable",
    "opponent_active_attack_cell_mask",
    "own_active_attack_cell_mask_readable", "own_active_attack_cell_mask",
    "replay_mode_timeout", "replay_sample_period",
    "sc_kind_group", "sc_match_counter", "sc_mode",
    "sc_provider_i", "sc_provider_j", "sc_provider_table",
    "sc_state", "sc_trigger_bits",
    "input",
)


class GoldenTraceError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _manifest_path_value(path: Path, manifest_path: Path) -> str:
    resolved = path.resolve()
    try:
        return os.path.relpath(resolved, manifest_path.parent.resolve())
    except ValueError:
        return str(resolved)


def _write_manifest_atomic(
    manifest_path: Path,
    manifest: dict[str, Any],
) -> None:
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = manifest_path.with_suffix(manifest_path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(manifest_path)


def _verify_producer_provenance(
    *,
    manifest_path: Path,
    producer_worktree: Path,
    producer_report: Path,
    source_commit: str,
    dll: Path,
    producer_deployed_dll: Path,
    trace: Path,
    replay: Path,
) -> dict[str, Any]:
    worktree = producer_worktree.resolve()
    if not worktree.is_dir():
        raise GoldenTraceError("producer worktree does not exist")
    try:
        head = subprocess.run(
            ["git", "-C", str(worktree), "rev-parse", "HEAD"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
        ).stdout.strip().lower()
        tracked_status = subprocess.run(
            [
                "git", "-C", str(worktree), "status", "--porcelain",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise GoldenTraceError(
            f"cannot verify producer worktree: {exc}") from exc
    if head != source_commit.lower():
        raise GoldenTraceError("producer worktree HEAD is not source commit")
    if tracked_status:
        raise GoldenTraceError("producer worktree has tracked changes")
    try:
        dll.resolve().relative_to(worktree)
    except ValueError as exc:
        raise GoldenTraceError(
            "producer DLL is not inside producer worktree") from exc
    built_dll_sha256 = sha256_file(dll)
    deployed_dll_sha256 = sha256_file(producer_deployed_dll)
    if deployed_dll_sha256 != built_dll_sha256:
        raise GoldenTraceError(
            "producer built/deployed DLL SHA-256 mismatch")
    try:
        report = json.loads(producer_report.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise GoldenTraceError(f"producer report is invalid: {exc}") from exc
    if not isinstance(report, dict) \
            or report.get("final_passed") is not True \
            or report.get("summary_passed") is not True \
            or int(report.get("analyzer_exit_code", -1)) != 0:
        raise GoldenTraceError("producer report is not passing")
    report_trace = Path(str(report.get("trace_path") or "")).resolve()
    if report_trace != trace.resolve():
        raise GoldenTraceError("producer report is not bound to trace")
    replay_result = report.get("replay_start_result", {})
    report_replay = Path(
        str(replay_result.get("resolved_path") or "")).resolve()
    if report_replay != replay.resolve():
        raise GoldenTraceError("producer report is not bound to replay")
    return {
        "producer_worktree_commit": head,
        "producer_worktree_clean": True,
        "producer_deployed_dll_sha256": deployed_dll_sha256,
        "producer_report_path": _manifest_path_value(
            producer_report, manifest_path),
        "producer_report_sha256": sha256_file(producer_report),
    }


def _latest_generation(
    path: Path,
    *,
    approval: bool,
) -> list[dict[str, Any]]:
    current_start: dict[str, Any] | None = None
    current: list[dict[str, Any]] | None = None
    latest: tuple[
        dict[str, Any], list[dict[str, Any]], dict[str, Any]
    ] | None = None
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            event_name = event.get("event")
            if event_name == "generate_start":
                current_start = event
                current = []
            elif event_name == "oracle_frame" and current is not None:
                current.append(event)
            elif event_name == "generate_complete":
                if current_start is not None and current is not None:
                    latest = current_start, current, event
                current_start = None
                current = None
    if current_start is not None and current is not None:
        latest = current_start, current, {}
    if latest is None:
        raise GoldenTraceError("trace has no closed generation")

    start, frames, complete = latest
    start_mode = str(start.get("mode") or "")
    complete_mode = str(complete.get("mode") or "")
    if start_mode != complete_mode:
        raise GoldenTraceError("generation mode changed before completion")
    # Goldens and candidate traces are correctness evidence.  No-render output
    # remains useful for diagnostics, but it is not authoritative enough to
    # create or validate a trusted replay baseline.
    allowed_modes = {"normal"}
    if start_mode not in allowed_modes:
        raise GoldenTraceError("latest generation is not normal mode")
    if complete.get("oracle_ok") is not True \
            or complete.get("integrity_ok") is not True:
        raise GoldenTraceError("latest generation failed integrity")
    if int(complete.get("frames", -1)) != len(frames):
        raise GoldenTraceError("generation frame count mismatch")
    return frames


def _canonical_window(
    trace: Path,
    *,
    round_index: int,
    frame_start: int,
    frame_count: int,
    canonicalization: str,
    approval: bool,
) -> list[dict[str, Any]]:
    if round_index < 0 or frame_start < 0 or frame_count <= 0:
        raise GoldenTraceError("invalid golden frame window")
    frames = [
        event for event in _latest_generation(trace, approval=approval)
        if int(event.get("round", -1)) == round_index
    ]
    frames.sort(key=lambda event: int(event.get("seq", -1)))
    if len(frames) < frame_start + frame_count:
        raise GoldenTraceError(
            f"round {round_index} has {len(frames)} frames; "
            f"{frame_start + frame_count} required"
        )
    selected = frames[frame_start:frame_start + frame_count]
    result: list[dict[str, Any]] = []
    previous_seq: int | None = None
    for frame in selected:
        seq = int(frame.get("seq", -1))
        canonical_hash = _canonical_frame_hash(
            frame, canonicalization=canonicalization)
        if seq < 0 or not canonical_hash:
            raise GoldenTraceError(
                f"round {round_index} contains an invalid canonical frame"
            )
        if previous_seq is not None and seq != previous_seq + 1:
            raise GoldenTraceError(
                f"round {round_index} golden window is not contiguous"
            )
        previous_seq = seq
        result.append({"seq": seq, "canonical_hash": canonical_hash})
    return result


def _canonical_frame_hash(
    frame: dict[str, Any],
    *,
    canonicalization: str,
) -> str:
    if canonicalization in {
            RAW_ORACLE_CANONICALIZATION,
            UNSTABLE_RAW_ORACLE_CANONICALIZATION}:
        required = ("seq", *RAW_ORACLE_TOP_LEVEL_FIELDS)
        if any(field not in frame for field in required):
            return ""
        if int(frame.get("oracle_schema_version", -1)) \
                != ORACLE_SCHEMA_VERSION:
            return ""
        if frame.get("rng_gameplay_crt_present") is not True:
            return ""
        payload: dict[str, Any] = {
            field: frame[field] for field in RAW_ORACLE_TOP_LEVEL_FIELDS
        }
        for prefix in ("p1", "p2"):
            for field in RAW_ORACLE_PLAYER_FIELDS:
                key = f"{prefix}_{field}"
                if key not in frame:
                    return ""
                payload[key] = frame[key]
        if canonicalization == RAW_ORACLE_CANONICALIZATION:
            try:
                image_base = int(str(frame.get("image_base", "0")), 0)
                for prefix in ("p1", "p2"):
                    for field in ("chara", "active_lane_cursor"):
                        key = f"{prefix}_{field}"
                        pointer = int(str(payload[key]), 0)
                        if pointer == 0:
                            payload[key] = "rva:0x0"
                        elif image_base > 0 and pointer >= image_base:
                            payload[key] = (
                                f"rva:0x{pointer - image_base:X}")
                        else:
                            raise ValueError(
                                f"{key} is not relative to image base")
            except (TypeError, ValueError):
                return ""
        try:
            canonical_json = json.dumps(
                payload,
                allow_nan=False,
                ensure_ascii=True,
                separators=(",", ":"),
                sort_keys=True,
            ).encode("ascii")
        except (TypeError, ValueError, UnicodeEncodeError):
            return ""
        return "sha256:" + hashlib.sha256(canonical_json).hexdigest()

    if canonicalization == LEGACY_CANONICALIZATION:
        explicit = frame.get("canonical_hash")
        if explicit:
            return str(explicit).lower()
        value = str(frame.get("oracle_gameplay_hash") or "").lower()
        if not value.startswith("0x"):
            return ""
        try:
            return value if int(value, 16) != 0 else ""
        except ValueError:
            return ""
    if canonicalization != RAW_ORACLE_CANONICALIZATION:
        raise GoldenTraceError(
            f"unsupported canonical hash field: {canonicalization}")
    return ""


def _content_metadata(trace: Path) -> dict[str, Any]:
    candidate: dict[str, Any] | None = None
    with trace.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if event.get("event") == "native_replay_payload_imported" \
                    and event.get("ok") is True \
                    and event.get("native_replay_metadata_valid") is True:
                candidate = event
    if candidate is None:
        raise GoldenTraceError("trace has no verified replay content metadata")
    event = candidate
    metadata = {
        "stage": int(event.get("stage", -1)),
        "stage_map": int(event.get("stage_map", -1)),
        "left_character": str(event.get("left_chara_label") or ""),
        "right_character": str(event.get("right_chara_label") or ""),
        "left_character_id": int(event.get("left_chara", -1)),
        "right_character_id": int(event.get("right_chara", -1)),
        "random_seed": str(event.get("random_seed") or ""),
    }
    if metadata["stage"] < 0 or metadata["stage_map"] < 0 \
            or not metadata["left_character"] \
            or not metadata["right_character"]:
        raise GoldenTraceError("verified replay content metadata is incomplete")
    return metadata


def _content_metadata_matches(
    expected: dict[str, Any],
    actual: dict[str, Any],
) -> bool:
    stable_fields = (
        "stage",
        "stage_map",
        "left_character",
        "right_character",
        "left_character_id",
        "right_character_id",
    )
    if any(expected.get(field) != actual.get(field)
           for field in stable_fields):
        return False
    expected_seed = str(expected.get("random_seed") or "")
    return not expected_seed \
        or expected_seed == str(actual.get("random_seed") or "")


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GoldenTraceError(f"cannot read golden manifest: {exc}") from exc
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise GoldenTraceError("unsupported golden manifest schema")
    cases = manifest.get("cases")
    if not isinstance(cases, list):
        raise GoldenTraceError("golden manifest cases must be a list")
    identifiers = [str(case.get("id") or "") for case in cases]
    if not all(identifiers) or len(set(identifiers)) != len(identifiers):
        raise GoldenTraceError("golden manifest case IDs are invalid")
    required_provenance = (
        "producer_worktree_commit",
        "producer_worktree_clean",
        "producer_deployed_dll_sha256",
        "producer_report_path",
        "producer_report_sha256",
        "runner_path",
        "runner_sha256",
        "oracle_schema_version",
    )
    for case in cases:
        if any(field not in case for field in required_provenance):
            raise GoldenTraceError(
                f"golden case {case.get('id')} lacks producer provenance")
        if case.get("producer_worktree_clean") is not True \
                or case.get("producer_worktree_commit") \
                != case.get("source_commit"):
            raise GoldenTraceError(
                f"golden case {case.get('id')} has invalid worktree provenance")
        if case.get("producer_deployed_dll_sha256") \
                != case.get("dll_sha256"):
            raise GoldenTraceError(
                f"golden case {case.get('id')} has invalid DLL provenance")
        if case.get("oracle_schema_version") != ORACLE_SCHEMA_VERSION:
            raise GoldenTraceError(
                f"golden case {case.get('id')} has incompatible oracle schema")
        for field in (
            "dll_sha256", "replay_sha256", "producer_trace_sha256",
            "producer_deployed_dll_sha256", "producer_report_sha256",
            "runner_sha256",
        ):
            if re.fullmatch(
                r"[0-9a-fA-F]{64}", str(case.get(field) or "")
            ) is None:
                raise GoldenTraceError(
                    f"golden case {case.get('id')} has invalid {field}")
        report_value = str(case.get("producer_report_path") or "").strip()
        if not report_value:
            raise GoldenTraceError(
                f"golden case {case.get('id')} has no producer report path")
        report_path = Path(report_value)
        if not report_path.is_absolute():
            report_path = path.parent / report_path
        if not report_path.is_file():
            raise GoldenTraceError(
                f"golden case {case.get('id')} producer report is missing")
        if sha256_file(report_path).lower() \
                != str(case["producer_report_sha256"]).lower():
            raise GoldenTraceError(
                f"golden case {case.get('id')} producer report hash mismatch")
        runner_value = str(case.get("runner_path") or "").strip()
        runner_path = Path(runner_value)
        if not runner_path.is_absolute():
            runner_path = path.parent / runner_path
        if not runner_path.is_file() \
                or sha256_file(runner_path).lower() \
                != str(case["runner_sha256"]).lower():
            raise GoldenTraceError(
                f"golden case {case.get('id')} runner identity mismatch")
    return manifest


def validate_case(
    manifest_path: Path,
    case_id: str,
    trace: Path,
    replay: Path,
) -> dict[str, Any]:
    manifest = load_manifest(manifest_path)
    case = next(
        (item for item in manifest["cases"] if item["id"] == case_id),
        None,
    )
    if case is None:
        raise GoldenTraceError(f"golden case does not exist: {case_id}")
    metadata = case.get("content_metadata")
    if not isinstance(metadata, dict) \
            or int(metadata.get("stage", -1)) < 0 \
            or not metadata.get("left_character") \
            or not metadata.get("right_character"):
        raise GoldenTraceError("golden content metadata is incomplete")
    actual_metadata = _content_metadata(trace)
    if not _content_metadata_matches(metadata, actual_metadata):
        raise GoldenTraceError("candidate trace content metadata mismatch")
    expected_replay = str(case.get("replay_sha256") or "").lower()
    actual_replay = sha256_file(replay)
    if expected_replay != actual_replay:
        raise GoldenTraceError("golden replay SHA-256 mismatch")

    expected_frames = case.get("expected_frames")
    if not isinstance(expected_frames, list) or not expected_frames:
        raise GoldenTraceError("golden case has no expected frames")
    actual_frames = _canonical_window(
        trace,
        round_index=int(case["round_index"]),
        frame_start=int(case["frame_start"]),
        frame_count=int(case["frame_count"]),
        canonicalization=str(
            case.get("canonical_hash_field")
            or LEGACY_CANONICALIZATION),
        approval=False,
    )
    if actual_frames != expected_frames:
        mismatch = next(
            (
                index for index, (actual, expected)
                in enumerate(zip(actual_frames, expected_frames))
                if actual != expected
            ),
            min(len(actual_frames), len(expected_frames)),
        )
        raise GoldenTraceError(
            f"golden canonical mismatch at window index {mismatch}"
        )
    return {
        "ok": True,
        "case_id": case_id,
        "manifest": str(manifest_path),
        "trace": str(trace),
        "trace_sha256": sha256_file(trace),
        "replay_sha256": actual_replay,
        "trusted_source_commit": case["source_commit"],
        "trusted_dll_sha256": case["dll_sha256"],
        "trusted_runner_sha256": case["runner_sha256"],
        "oracle_schema_version": case["oracle_schema_version"],
        "round_index": case["round_index"],
        "frame_start": case["frame_start"],
        "frame_count": case["frame_count"],
    }


def approve_case(
    manifest_path: Path,
    *,
    case_id: str,
    trace: Path,
    replay: Path,
    dll: Path,
    producer_deployed_dll: Path,
    producer_worktree: Path,
    producer_report: Path,
    runner: Path,
    source_commit: str,
    content_case: str,
    round_index: int,
    frame_start: int,
    frame_count: int,
    replace: bool,
) -> dict[str, Any]:
    if re.fullmatch(r"[0-9a-fA-F]{40,64}", source_commit.strip()) is None \
            or not content_case.strip():
        raise GoldenTraceError("source commit and content case are required")
    if not runner.is_file():
        raise GoldenTraceError("approval runner does not exist")
    provenance = _verify_producer_provenance(
        manifest_path=manifest_path,
        producer_worktree=producer_worktree,
        producer_report=producer_report,
        source_commit=source_commit,
        dll=dll,
        producer_deployed_dll=producer_deployed_dll,
        trace=trace,
        replay=replay,
    )
    expected_frames = _canonical_window(
        trace,
        round_index=round_index,
        frame_start=frame_start,
        frame_count=frame_count,
        canonicalization=RAW_ORACLE_CANONICALIZATION,
        approval=True,
    )
    case = {
        "id": case_id,
        "content_case": content_case,
        "content_metadata": _content_metadata(trace),
        "canonical_hash_field": RAW_ORACLE_CANONICALIZATION,
        "source_commit": source_commit,
        "dll_sha256": sha256_file(dll),
        "replay_path": _manifest_path_value(replay, manifest_path),
        "replay_sha256": sha256_file(replay),
        "producer_trace_sha256": sha256_file(trace),
        "runner_path": _manifest_path_value(runner, manifest_path),
        "runner_sha256": sha256_file(runner),
        "oracle_schema_version": ORACLE_SCHEMA_VERSION,
        **provenance,
        "round_index": round_index,
        "frame_start": frame_start,
        "frame_count": frame_count,
        "expected_frames": expected_frames,
    }
    if manifest_path.exists():
        manifest = load_manifest(manifest_path)
    else:
        manifest = {"schema_version": SCHEMA_VERSION, "cases": []}
    existing = next(
        (item for item in manifest["cases"] if item["id"] == case_id),
        None,
    )
    if existing is not None and not replace:
        raise GoldenTraceError(
            f"golden case already exists: {case_id}; use --replace explicitly"
        )
    manifest["cases"] = [
        item for item in manifest["cases"] if item["id"] != case_id
    ]
    manifest["cases"].append(case)
    manifest["cases"].sort(key=lambda item: item["id"])
    _write_manifest_atomic(manifest_path, manifest)
    return case


def _write_synthetic_trace(
        path: Path, *, corrupt_index: int | None = None,
        image_base: int = 0x140000000,
        mode: str = "normal") -> None:
    events: list[dict[str, Any]] = [
        {
            "event": "native_replay_payload_imported",
            "ok": True,
            "native_replay_metadata_valid": True,
            "stage": 1,
            "stage_map": 1,
            "left_chara": 2,
            "right_chara": 3,
            "left_chara_label": "Synthetic Left",
            "right_chara_label": "Synthetic Right",
            "random_seed": "0x1234",
        },
        {"event": "generate_start", "mode": mode},
    ]
    for index in range(6):
        frame: dict[str, Any] = {
            "event": "oracle_frame",
            "round": 0,
            "seq": 100 + index,
            "image_base": hex(image_base),
            # A producer-supplied hash must not override raw
            # canonicalization. Keeping this fixed makes the negative control
            # prove that raw state, rather than this field, is authoritative.
            "canonical_hash": "0xfeedface",
        }
        for field_index, field in enumerate(RAW_ORACLE_TOP_LEVEL_FIELDS):
            frame[field] = (
                0 if field == "round"
                else index * 1000 + field_index
            )
        frame["oracle_schema_version"] = ORACLE_SCHEMA_VERSION
        frame["oracle_gameplay_hash_v12"] = hex(0x1000 + index)
        frame["rng_gameplay_crt_present"] = True
        for player_index, prefix in enumerate(("p1", "p2")):
            for field_index, field in enumerate(RAW_ORACLE_PLAYER_FIELDS):
                frame[f"{prefix}_{field}"] = (
                    index * 10000 + player_index * 1000 + field_index
                )
            for field in ("chara", "active_lane_cursor"):
                key = f"{prefix}_{field}"
                frame[key] = hex(image_base + int(frame[key]))
        if corrupt_index == index:
            frame["p1_pos_x"] ^= 0x55
        events.append(frame)
    events.append({
        "event": "generate_complete",
        "mode": mode,
        "oracle_ok": True,
        "integrity_ok": True,
        "frames": 6,
    })
    path.write_text(
        "".join(json.dumps(event) + "\n" for event in events),
        encoding="utf-8",
    )


def selftest() -> int:
    with tempfile.TemporaryDirectory(prefix="rollback-golden-") as root:
        root_path = Path(root)
        manifest = root_path / "manifest.json"
        trace = root_path / "trace.jsonl"
        replay = root_path / "replay.bin"
        producer = root_path / "producer"
        producer.mkdir()
        subprocess.run(
            ["git", "init", str(producer)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        tracked = producer / "producer-source.txt"
        tracked.write_text("trusted producer source\n", encoding="utf-8")
        (producer / ".gitignore").write_text(
            "/build/\n", encoding="utf-8")
        subprocess.run(
            [
                "git", "-C", str(producer), "add",
                tracked.name, ".gitignore",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        subprocess.run(
            [
                "git", "-C", str(producer),
                "-c", "user.name=Rollback Selftest",
                "-c", "user.email=rollback-selftest.invalid",
                "commit", "-m", "trusted producer fixture",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        source_commit = subprocess.run(
            ["git", "-C", str(producer), "rev-parse", "HEAD"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        ).stdout.strip()
        dll = producer / "build" / "HorseMod.dll"
        dll.parent.mkdir()
        producer_report = root_path / "producer-report.json"
        runner = root_path / "runner.py"
        replay.write_bytes(b"trusted replay fixture")
        dll.write_bytes(b"trusted HorseMod fixture")
        runner.write_text("print('trusted runner')\n", encoding="utf-8")
        _write_synthetic_trace(trace)
        producer_report.write_text(
            json.dumps({
                "final_passed": True,
                "summary_passed": True,
                "analyzer_exit_code": 0,
                "trace_path": str(trace.resolve()),
                "replay_start_result": {
                    "resolved_path": str(replay.resolve()),
                },
            }) + "\n",
            encoding="utf-8",
        )
        approve_case(
            manifest,
            case_id="synthetic",
            trace=trace,
            replay=replay,
            dll=dll,
            producer_deployed_dll=dll,
            producer_worktree=producer,
            producer_report=producer_report,
            runner=runner,
            source_commit=source_commit,
            content_case="synthetic negative-control fixture",
            round_index=0,
            frame_start=1,
            frame_count=4,
            replace=False,
        )
        # Image rebasing must not change the canonical gameplay state.
        _write_synthetic_trace(trace, image_base=0x180000000)
        validate_case(manifest, "synthetic", trace, replay)
        _write_synthetic_trace(
            trace, corrupt_index=3, image_base=0x180000000)
        try:
            validate_case(manifest, "synthetic", trace, replay)
        except GoldenTraceError as exc:
            if "golden canonical mismatch" not in str(exc):
                raise
        else:
            raise GoldenTraceError("negative control did not fail")
        _write_synthetic_trace(trace, mode="lux-no-render")
        try:
            validate_case(manifest, "synthetic", trace, replay)
        except GoldenTraceError as exc:
            if "not normal mode" not in str(exc):
                raise
        else:
            raise GoldenTraceError(
                "lux-no-render correctness negative control did not fail")
    print(
        "rollback golden trace self-test passed "
        "negative_control=1 normal_renderer_required=1")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--case", required=False)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--replay", type=Path)
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--producer-deployed-dll", type=Path)
    parser.add_argument("--producer-worktree", type=Path)
    parser.add_argument("--producer-report", type=Path)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--source-commit")
    parser.add_argument("--content-case")
    parser.add_argument("--round-index", type=int, default=0)
    parser.add_argument("--frame-start", type=int, default=0)
    parser.add_argument("--frame-count", type=int, default=600)
    parser.add_argument("--approve", action="store_true")
    parser.add_argument("--replace", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.selftest:
        return selftest()
    if not args.case or not args.trace or not args.replay:
        raise GoldenTraceError("--case, --trace, and --replay are required")
    if args.approve:
        if not args.dll or not args.producer_deployed_dll \
                or not args.producer_worktree \
                or not args.producer_report \
                or not args.runner \
                or not args.source_commit or not args.content_case:
            raise GoldenTraceError(
                "approval requires --dll, --producer-deployed-dll, "
                "--producer-worktree, "
                "--producer-report, --runner, --source-commit, and "
                "--content-case"
            )
        result = approve_case(
            args.manifest,
            case_id=args.case,
            trace=args.trace,
            replay=args.replay,
            dll=args.dll,
            producer_deployed_dll=args.producer_deployed_dll,
            producer_worktree=args.producer_worktree,
            producer_report=args.producer_report,
            runner=args.runner,
            source_commit=args.source_commit,
            content_case=args.content_case,
            round_index=args.round_index,
            frame_start=args.frame_start,
            frame_count=args.frame_count,
            replace=args.replace,
        )
        result = {
            "ok": True,
            "operation": "replace" if args.replace else "approve",
            "case_id": result["id"],
            "manifest": str(args.manifest),
            "source_commit": result["source_commit"],
            "dll_sha256": result["dll_sha256"],
            "replay_sha256": result["replay_sha256"],
            "producer_trace_sha256": result["producer_trace_sha256"],
            "round_index": result["round_index"],
            "frame_start": result["frame_start"],
            "frame_count": result["frame_count"],
        }
    else:
        if args.replace:
            raise GoldenTraceError("--replace is valid only with --approve")
        result = validate_case(
            args.manifest, args.case, args.trace, args.replay
        )
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GoldenTraceError as exc:
        print(f"rollback-golden-trace:{exc}", flush=True)
        raise SystemExit(2)
