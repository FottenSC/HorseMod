#!/usr/bin/env python3
"""Find the first normal-replay versus online rollback state divergence."""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import math
import struct
import tempfile
from pathlib import Path
from typing import Any


EXACT_FIELDS = (
    "rng_lcg_state",
    "rng_lfsr_hash",
    "rng_lfsr_index",
    "secondary_event_previous_variant_readable",
    "round_result_count",
    "round_result_limit",
    "round_current_index",
    "round_applied_index",
    "round_result_flow",
    "breakable_gameplay_digest",
    "stage_wind_gameplay_hash",
    "chara_animation_gameplay_digest",
    "fp_mxcsr_control",
    "fp_x87_control_readable",
    "fp_x87_control",
)
STAGE_WIND_DIAGNOSTIC_FIELDS = (
    "stage_wind_canonical_hash",
    "stage_wind_combined_rng_hash",
    "stage_wind_emitter_hash",
    "stage_wind_root_scheduler_hash",
    "stage_wind_graph_nodes_hash",
    "stage_wind_graph_hash",
    "stage_wind_emitter_count",
    "stage_wind_graph_count",
    "stage_wind_output_active",
    "stage_wind_pending_count",
    "stage_wind_schedule_state",
    "stage_wind_effect_pair_scheduled",
)
FIGHTER_EXACT_SUFFIXES = (
    "readable",
    "move_id",
    "move_frame",
    "vital_category_bits",
    "vital_state",
    "hitstun",
    "blockstun",
    "hit_reaction_result",
    "hit_state",
    "sc_mode",
    "sc_state",
    "sc_trigger_bits",
    "sc_kind_group",
    "sc_match_counter",
    "hit_slide_slot",
    "latched_hit_slide_input_dir",
    "root_motion_carry_mode",
    "pose_finalize_tick_counter",
    "root_bone_matrices_readable",
) + (
    "hit_cue_pose_pack_readable",
) + tuple(
    f"hitcue_pose_lane{lane}_{suffix}"
    for lane in range(4)
    for suffix in (
        "readable",
        "motion_clip_index",
        "active_flags",
    )
) + tuple(
    f"hitcue{slot}_{suffix}"
    for slot in range(4)
    for suffix in (
        "active_cue",
        "multiplier_index",
        "pose_mode",
        "loop_flag",
        "end_reached",
        "cache_readable",
        "lane_readable",
        "lane_gate",
    )
)
FIGHTER_FLOAT_SUFFIXES = (
    "pos_x",
    "pos_y",
    "pos_z",
    "position_target_x",
    "position_target_y",
    "position_target_z",
    "position_target_lerp_weight",
    "vel_x",
    "vel_y",
    "vel_z",
    "ground_vel_x",
    "ground_vel_y",
    "ground_vel_z",
    "one_shot_x",
    "one_shot_z",
    "expected_motion_x",
    "expected_motion_z",
    "root_motion_carry_x",
    "root_motion_carry_z",
    "smoothed_root_motion_x",
    "smoothed_root_motion_z",
    "raw_root_motion_x",
    "raw_root_motion_z",
    "frame_delta_x",
    "frame_delta_z",
    "hit_pushback_x",
    "hit_pushback_z",
    "hit_pushback_decay_scale",
    "root_motion_blend_weight",
    "move_time_scale_a",
    "move_time_scale_b",
    "movevm_time_dilation_scalar",
    "hit_cue_root_weight",
    "facing",
    "pose_pos_x",
    "pose_pos_y",
    "pose_pos_z",
    "render_pos_x",
    "render_pos_y",
    "render_pos_z",
    "vital_scale",
    "vital_candidate",
    "vital_ko_gate",
    "vital_displayed",
) + tuple(
    f"root_bone_{phase}_m{row}{column}"
    for phase in ("current", "previous")
    for row in range(4)
    for column in range(4)
) + tuple(
    f"hitcue_pose_lane{lane}_{suffix}"
    for lane in range(4)
    for suffix in (
        "sample_frame",
        "active_weight",
        "sampler_yaw",
        "sampler_pitch",
        "scale_x",
        "scale_y",
        "scale_z",
        "scale_w",
    )
) + tuple(
    f"hitcue{slot}_{suffix}"
    for slot in range(4)
    for suffix in (
        "node_frame",
        "node_segment_end",
        "node_loop_span",
        "node_prev_frame",
        "weight_gate",
        "node_blend",
        "node_blend_target",
        "node_blend_step",
        "node_blend_rate",
        "node_blend_timer",
        "cached_local_x",
        "cached_local_y",
        "cached_local_z",
        "cached_world_x",
        "cached_world_y",
        "cached_world_z",
        "lane_rate",
    )
)

HIT_CUE_SEMANTIC_LAYOUT = (
    ("active_cue", 2),
    ("multiplier_index", 2),
    ("pose_mode", 2),
    ("frame_counter", 4),
    ("loop_flag", 2),
    ("end_reached", 2),
    ("node_frame", 4),
    ("node_segment_end", 4),
    ("node_loop_span", 4),
    ("node_prev_frame", 4),
    ("weight_gate", 4),
    ("node_blend", 4),
    ("node_blend_target", 4),
    ("node_blend_step", 4),
    ("node_blend_rate", 4),
    ("node_blend_timer", 4),
    ("cached_local_x", 4),
    ("cached_local_y", 4),
    ("cached_local_z", 4),
    ("cached_local_w", 4),
    ("cached_world_x", 4),
    ("cached_world_y", 4),
    ("cached_world_z", 4),
    ("cached_world_w", 4),
    ("lane_gate", 4),
    ("lane_rate", 4),
    ("cache_readable", 1),
    ("lane_readable", 1),
)


def hit_cue_semantic_byte_difference(
    expected: Any,
    actual: Any,
    float_tolerance: float = 1.0e-5,
) -> dict[str, Any] | None:
    try:
        expected_bytes = bytes.fromhex(str(expected))
        actual_bytes = bytes.fromhex(str(actual))
    except ValueError:
        return {
            "field": "semantic_bytes",
            "primitive": "invalid-semantic-byte-image",
            "expected": expected,
            "actual": actual,
        }
    if len(expected_bytes) != 96 or len(actual_bytes) != 96:
        return {
            "field": "semantic_bytes",
            "primitive": "wrong-semantic-byte-image-size",
            "expected_size": len(expected_bytes),
            "actual_size": len(actual_bytes),
        }
    offset = 0
    for name, size in HIT_CUE_SEMANTIC_LAYOUT:
        expected_value = expected_bytes[offset:offset + size]
        actual_value = actual_bytes[offset:offset + size]
        if expected_value != actual_value:
            # HgCpu peer canonical policy deliberately omits this
            # increment-only absolute progress representation. The authored
            # frame, previous frame, blend, and sampler fields below remain
            # authoritative.
            # Cached W is a homogeneous scratch lane. Ghidra confirms
            # ApplyMotionSlotRootMotion subtracts it only into a dead W
            # scratch value; authoritative X/Z use cached XYZ exclusively.
            # Preserve W in the raw byte image, but do not classify it as a
            # gameplay primitive.
            if name in {
                "frame_counter",
                "cached_local_w",
                "cached_world_w",
            }:
                offset += size
                continue
            # The normal renderer and rollback renderer can differ by a
            # handful of IEEE-754 ULPs while producing the same gameplay
            # value.  Apply the same cross-mode tolerance used by the named
            # float fields instead of letting the packed semantic image turn
            # those harmless representation differences into an exact-byte
            # failure.  Integer/flag fields remain exact, and peer canonical
            # hashes are still compared bit-for-bit elsewhere.
            if size == 4 and name not in {
                "lane_gate",
                "active_cue",
                "multiplier_index",
                "pose_mode",
                "loop_flag",
                "end_reached",
                "cache_readable",
                "lane_readable",
            }:
                expected_float = struct.unpack("<f", expected_value)[0]
                actual_float = struct.unpack("<f", actual_value)[0]
                if math.isfinite(expected_float) \
                        and math.isfinite(actual_float) \
                        and abs(expected_float - actual_float) \
                        <= float_tolerance:
                    offset += size
                    continue
            # IEEE +0 and -0 are numerically identical. Preserve their raw
            # images in the trace, but do not call a sign-bit-only change a
            # primitive animation divergence.
            if size == 4 and name not in {
                "lane_gate",
            }:
                expected_bits = int.from_bytes(
                    expected_value, "little", signed=False
                )
                actual_bits = int.from_bytes(
                    actual_value, "little", signed=False
                )
                if (expected_bits & 0x7FFFFFFF) == 0 \
                        and (actual_bits & 0x7FFFFFFF) == 0:
                    offset += size
                    continue
            return {
                "field": name,
                "primitive": name,
                "byte_offset": offset,
                "byte_size": size,
                "expected_bits": expected_value.hex().upper(),
                "actual_bits": actual_value.hex().upper(),
            }
        offset += size
    return None
ORACLE_SCHEMA_VERSION = 13
AGGREGATE_FIELDS = ("oracle_gameplay_hash_v12",)
PRESENTATION_FIELDS = (
    "oracle_presentation_hash",
    "breakable_presentation_digest",
    # The delayed secondary-event variant cursor is peer-canonical rollback
    # state, but not normal-replay versus online gameplay authority.  The two
    # modes can queue different presentation events before logical frame zero.
    # Shared-LFSR fields remain exact and expose any later RNG-consumption
    # consequence.
    "p1_secondary_event_previous_variant",
    "p2_secondary_event_previous_variant",
    "stage_wind_presentation_hash",
    "stage_wind_root_outputs_hash",
)
PEER_ONLY_FIELDS = ("canonical_hash",)
RNG_FIELDS = (
    "rng_lcg_state",
    "rng_lfsr_hash",
    "rng_lfsr_index",
    "rng_gameplay_crt_present",
    "rng_gameplay_crt_state",
    "rng_gameplay_crt_seed",
    "rng_gameplay_crt_draw_ordinal",
)
RNG_CHECKPOINT_EVENT = "rollback_rng_checkpoint"
RNG_LFSR_CALLERS_EVENT = "rng_u32_callers"
RNG_XORSHIFT96_CALLERS_EVENT = "rng_xorshift96_callers"
RNG_CRT_CALLERS_EVENT = "rng_crt_import_callers"
ROOT_MOTION_SAMPLE_EVENT = "native_root_motion_delta_sample"
POSE_PRODUCER_CHECKPOINT_EVENT = "native_pose_producer_checkpoint"
PRE_MAIN_MOTION_CHECKPOINT_EVENT = "native_pre_main_motion_checkpoint"
COLLISION_CHECKPOINT_EVENT = "native_collision_checkpoint"
RNG_CALLERS_EVENTS = {
    RNG_LFSR_CALLERS_EVENT: "lfsr-25-word",
    RNG_XORSHIFT96_CALLERS_EVENT: "xorshift96-gameplay",
    RNG_CRT_CALLERS_EVENT: "crt-rand-import",
}
INVALID_FRAME_COORDINATE = 0xFFFFFFFF

ROOT_MOTION_MATRIX_FIELDS = tuple(
    f"{boundary}_{matrix}_m{row}{column}"
    for boundary in ("before", "after")
    for matrix in ("current", "previous")
    for row in range(4)
    for column in range(4)
)
ROOT_MOTION_FLOAT_FIELDS = (
    "vm_freeze_out_blend_w0",
    *(
        f"{boundary}_{field}"
        for boundary in ("before", "after")
        for field in (
            "raw_x",
            "raw_y",
            "raw_z",
            "smoothed_x",
            "smoothed_y",
            "smoothed_z",
            "carry_x",
            "carry_z",
        )
    ),
    *ROOT_MOTION_MATRIX_FIELDS,
)
ROOT_MOTION_EXACT_FIELDS = (
    "vm_freeze_out_mode",
    *(
        f"{boundary}_{field}"
        for boundary in ("before", "after")
        for field in (
            "current_readable",
            "previous_readable",
            "current_hash",
            "previous_hash",
            "carry_mode",
        )
    ),
)
ROOT_MOTION_FRAME_FLOAT_SUFFIXES = (
    "raw_root_motion_x",
    "raw_root_motion_z",
    "smoothed_root_motion_x",
    "smoothed_root_motion_z",
    "root_motion_carry_x",
    "root_motion_carry_z",
)
ROOT_MOTION_CALL_FLOAT_SUFFIXES = (
    "raw_x",
    "raw_z",
    "smoothed_x",
    "smoothed_z",
    "carry_x",
    "carry_z",
)
POSE_PRODUCER_MATRIX_FIELDS = tuple(
    f"bone1_{matrix}_m{row}{column}"
    for matrix in ("current", "previous")
    for row in range(4)
    for column in range(4)
)
POSE_PRODUCER_FLOAT_FIELDS = (
    "clip_frame",
    "pose_root_pitch",
    "pose_root_yaw",
    "pose_root_roll",
    "pose_translation_x",
    "pose_translation_y",
    "pose_translation_z",
    *POSE_PRODUCER_MATRIX_FIELDS,
)
POSE_PRODUCER_EXACT_FIELDS = (
    "producer_checkpoint_ordinal",
    "producer_sequence_valid",
    "producer_sequence_complete",
    "move_id",
    "pose_finalize_tick",
    "post_finalize_pose_mode",
    "pose_root_readable",
    "pose_root_hash",
    "pose_translation_readable",
    "pose_translation_hash",
    "bone1_current_readable",
    "bone1_previous_readable",
    "bone1_current_hash",
    "bone1_previous_hash",
    "pose_matrix_bank_current_readable",
    "pose_matrix_bank_previous_readable",
    "pose_solver_current_matches_matrix_bank",
    "pose_matrix_bank_current_hash",
    "pose_matrix_bank_previous_hash",
)
PRE_MAIN_MOTION_EXACT_FIELDS = tuple(
    f"{player}_{suffix}"
    for player in ("p1", "p2")
    for suffix in (
        "current_move_id",
        "pose_finalize_tick",
        "linked_motion_readable",
        "linked_motion_hash",
        "pose_publication_readable",
        "pose_publication_hash",
        "clip_player_readable",
        "clip_player_scalar_hash",
        "clip_runtime_readable",
        "clip_runtime_present",
        "clip_runtime_scalar_hash",
        "clip_cached_frame_bits",
        "clip_owner_frame_bits",
        "matrix_bank_current_readable",
        "matrix_bank_previous_readable",
        "matrix_bank_current_hash",
        "matrix_bank_previous_hash",
        "matrix_bank_current_bone_11_hash",
        "matrix_bank_previous_bone_11_hash",
        "matrix_bank_current_bone_11_m30_bits",
        "matrix_bank_previous_bone_11_m30_bits",
    )
) + tuple(
    f"rng_{family}_{suffix}"
    for family in ("lfsr", "xorshift96")
    for suffix in (
        "total",
        "overflow",
        "caller_count",
        "histogram_hash",
    )
)
COLLISION_CHECKPOINT_SEQUENCE = (
    ("TickHitResolutionAndBodyCollision", "enter"),
    ("SolvePhysBodyCollision", "enter"),
    ("SolvePhysBodyCollision", "exit"),
    ("TickBothCharaCollisionPhysics", "enter"),
    ("TickBothCharaCollisionPhysics", "exit"),
    ("TickHitResolutionAndBodyCollision", "exit"),
)
COLLISION_CHECKPOINT_EXACT_FIELDS = (
    "transaction_active",
    "has_solver_inputs",
    "fp_mxcsr_control",
    "fp_x87_control_readable",
    "fp_x87_control",
    "body_solve_result",
    "p1_collision_result",
    "p2_collision_result",
) + tuple(
    f"{player}_{suffix}"
    for player in ("p1", "p2")
    for suffix in (
        "collision_result",
        "orientation_preserve",
        "pose_finalize_tick",
        "bone1_current_readable",
        "bone1_previous_readable",
        "bone1_current_hash",
        "bone1_previous_hash",
        "bone1_carry_mode",
        "matrix_bank_current_readable",
        "matrix_bank_previous_readable",
        "solver_current_matches_matrix_bank",
        "matrix_bank_current_hash",
        "matrix_bank_previous_hash",
        "khit_runtime_readable",
        "khit_body_list_readable",
        "khit_body_list_complete",
        "khit_body_list_cycle",
        "khit_body_node_count",
        "khit_body_node_hash_count",
        "khit_body_scratch_bytes",
        "khit_body_max_slot_exclusive",
        "khit_phys_body_type",
        "khit_body_list_semantic_hash",
    )
)
COLLISION_CHECKPOINT_FLOAT_FIELDS = tuple(
    f"{player}_{suffix}"
    for player in ("p1", "p2")
    for suffix in (
        "khit_phys_body_radius",
        "khit_phys_body_separation_scale",
        "khit_phys_body_upper_offset",
        "khit_phys_body_lower_offset",
        "khit_phys_body_impact_force_scale",
        "sim_pos_x",
        "sim_pos_z",
        "facing",
        "impact_force_scale",
        "bone1_raw_x",
        "bone1_raw_y",
        "bone1_raw_z",
        "bone1_smoothed_x",
        "bone1_smoothed_y",
        "bone1_smoothed_z",
        "bone1_carry_x",
        "bone1_carry_z",
    )
) + tuple(
    f"{player}_bone1_{matrix}_m{row}{column}"
    for player in ("p1", "p2")
    for matrix in ("current", "previous")
    for row in range(4)
    for column in range(4)
) + ("push_angle_turns",)


def checkpoint_dynamic_semantic_hash_fields(
    expected: dict[str, Any],
    actual: dict[str, Any],
) -> tuple[str, ...]:
    fields = set(expected) | set(actual)
    return tuple(sorted(
        field for field in fields
        if (
            (
                "_matrix_bank_current_bone_" in field
                or "_matrix_bank_previous_bone_" in field
            )
            and (
                field.endswith("_hash")
                or field.endswith("_bits")
            )
        ) or "_khit_body_node_" in field
    ))


def require_current_oracle_event(
    event: dict[str, Any],
    context: str,
) -> None:
    version = trace_int(event.get("oracle_schema_version"), -1)
    if version != ORACLE_SCHEMA_VERSION:
        raise ValueError(
            f"{context} oracle schema mismatch: "
            f"expected={ORACLE_SCHEMA_VERSION} actual={version}"
        )
    for field in (
        *AGGREGATE_FIELDS,
        *PRESENTATION_FIELDS,
        "stage_wind_gameplay_hash",
    ):
        if field not in event:
            raise ValueError(f"{context} is missing {field}")


def trace_int(value: Any, default: int = -1) -> int:
    try:
        return int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError):
        return default


def load_jsonl(path: Path, byte_end: int | None = None) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    with path.open("rb") as stream:
        data = stream.read() if byte_end is None else stream.read(byte_end)
    for line in data.decode("utf-8", errors="replace").splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            events.append(value)
    return events


def bounded_sha256(path: Path, byte_end: int) -> str:
    digest = hashlib.sha256()
    remaining = byte_end
    with path.open("rb") as stream:
        while remaining:
            chunk = stream.read(min(1024 * 1024, remaining))
            if not chunk:
                raise ValueError(f"trace shorter than retained range: {path}")
            digest.update(chunk)
            remaining -= len(chunk)
    return digest.hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_prefix_end(path: Path, expected_hash: str) -> int:
    expected = expected_hash.lower()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while line := stream.readline():
            digest.update(line)
            if digest.copy().hexdigest() == expected:
                return stream.tell()
    raise ValueError("recorded SHA-256 is not a prefix of the oracle trace")


def latest_oracle_rounds(
    path: Path,
    byte_end: int,
) -> dict[int, list[dict[str, Any]]]:
    start: dict[str, Any] | None = None
    frames: list[dict[str, Any]] | None = None
    latest: tuple[
        dict[str, Any], list[dict[str, Any]], dict[str, Any]
    ] | None = None
    for event in load_jsonl(path, byte_end):
        name = event.get("event")
        if name == "generate_start":
            start = event
            frames = []
        elif name == "oracle_frame" and frames is not None:
            frames.append(event)
        elif name == "generate_complete":
            if start is not None and frames is not None:
                latest = start, frames, event
            start = None
            frames = None
    if start is not None and frames is not None:
        latest = start, frames, {}
    if latest is None:
        raise ValueError("normal oracle generation is missing")
    generation_start, generation_frames, complete = latest
    mode = str(generation_start.get("mode") or "")
    if mode != "normal" or str(complete.get("mode") or mode) != "normal":
        raise ValueError("latest oracle generation is not normal mode")
    if complete.get("oracle_ok") is not True \
            or complete.get("integrity_ok") is not True \
            or trace_int(complete.get("frames")) != len(generation_frames):
        raise ValueError("latest normal oracle generation is incomplete")
    for frame in generation_frames:
        require_current_oracle_event(frame, "normal oracle frame")
    rounds: dict[int, list[dict[str, Any]]] = {}
    for frame in generation_frames:
        rounds.setdefault(trace_int(frame.get("round")), []).append(frame)
    for values in rounds.values():
        values.sort(key=lambda item: trace_int(item.get("seq")))
    return rounds


def retained_traces(report_client: dict[str, Any]) -> list[tuple[Path, int]]:
    artifacts = report_client.get("trace_artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise ValueError("client trace artifact list is missing")
    result: list[tuple[Path, int]] = []
    for artifact in artifacts:
        if not isinstance(artifact, dict):
            raise ValueError("client trace artifact is invalid")
        path = Path(str(artifact.get("path") or ""))
        byte_range = artifact.get("byte_range")
        if not path.is_file() or not isinstance(byte_range, list) \
                or len(byte_range) != 2 or trace_int(byte_range[0]) != 0:
            raise ValueError(f"invalid retained trace artifact: {path}")
        byte_end = trace_int(byte_range[1])
        expected_hash = str(artifact.get("sha256") or "").lower()
        if byte_end <= 0 or len(expected_hash) != 64:
            raise ValueError(f"invalid retained trace identity: {path}")
        if bounded_sha256(path, byte_end) != expected_hash:
            raise ValueError(f"retained trace SHA-256 mismatch: {path}")
        result.append((path, byte_end))
    return result


def confirmed_frames(
    report_client: dict[str, Any],
    selected_epochs: dict[int, int],
) -> dict[tuple[int, int], dict[str, Any]]:
    all_events, expected_pid, expected_marker, expected_request, \
        request_start_qpc = scoped_client_events(report_client)
    wind_nodes: dict[tuple[int, int, int, int], dict[str, Any]] = {}
    for event in all_events:
        if event.get("event") != "rollback_stage_wind_phase" \
                or event.get("component") != "stage-wind-node" \
                or event.get("phase") != "logical-frame-post-advance":
            continue
        if not event_matches_client_scope(
                event, expected_pid, expected_marker, expected_request,
                request_start_qpc):
            continue
        epoch = trace_int(event.get("round_epoch"), 0)
        logical_frame = trace_int(event.get("logical_frame"), -1)
        node_index = trace_int(event.get("node_index"), -1)
        node_hash = trace_int(event.get("node_hash"), 0)
        if epoch == 0 or logical_frame < 0 or node_index < 0 \
                or node_hash == 0:
            continue
        wind_nodes[(epoch, logical_frame, node_index, node_hash)] = event
    frames: dict[tuple[int, int], dict[str, Any]] = {}
    for event in all_events:
        if event.get("event") != "rollback_replay_trace_frame" \
                or event.get("pair_confirmed") is not True \
                or event.get("speculative") is True:
            continue
        if not event_matches_client_scope(
                event, expected_pid, expected_marker, expected_request,
                request_start_qpc):
            continue
        replay_round = trace_int(event.get("replay_round"))
        epoch = trace_int(event.get("round_epoch"), 0)
        logical_frame = trace_int(event.get("logical_frame"))
        if selected_epochs.get(replay_round) != epoch \
                or logical_frame < 0:
            continue
        require_current_oracle_event(event, "confirmed rollback frame")
        key = replay_round, logical_frame
        event = dict(event)
        graph_count = max(
            0, trace_int(event.get("stage_wind_graph_count"), 0))
        for node_index in range(graph_count):
            prefix = f"stage_wind_node_{node_index}"
            node_hash = trace_int(event.get(f"{prefix}_hash"), 0)
            checkpoint = wind_nodes.get(
                (epoch, logical_frame, node_index, node_hash))
            if checkpoint is None:
                continue
            for source, destination in (
                ("common_hash", f"{prefix}_common_hash"),
                ("body_hash", f"{prefix}_body_hash"),
                ("tail_hash", f"{prefix}_tail_hash"),
                ("life_bits", f"{prefix}_life_bits"),
                ("reset_life_bits", f"{prefix}_reset_life_bits"),
                ("frame_step_bits", f"{prefix}_frame_step_bits"),
                ("repeat_remaining", f"{prefix}_repeat_remaining"),
                ("oscillator_tick", f"{prefix}_oscillator_tick"),
                ("prepared", f"{prefix}_prepared"),
                ("active", f"{prefix}_active"),
            ):
                if source in checkpoint:
                    event[destination] = checkpoint[source]
            for field, value in checkpoint.items():
                if field.startswith("common_word_"):
                    event[
                        f"{prefix}_word_{field.removeprefix('common_word_')}"
                    ] = value
                elif field.startswith("semantic_word_"):
                    event[
                        f"{prefix}_semantic_word_"
                        f"{field.removeprefix('semantic_word_')}"
                    ] = value
        previous = frames.get(key)
        if previous is not None and previous != event:
            raise ValueError(
                f"conflicting confirmed frame: round={replay_round} "
                f"logical={logical_frame}"
            )
        frames[key] = event
    return frames


def scoped_client_events(
    report_client: dict[str, Any],
) -> tuple[list[dict[str, Any]], int, int, str, int]:
    expected_pid = trace_int(report_client.get("pid"), 0)
    status = report_client.get("status", {})
    expected_marker = trace_int(status.get("process_start_marker"), 0)
    expected_request = str(status.get("request_id") or "")
    if expected_pid <= 0 or expected_marker <= 0 or not expected_request:
        raise ValueError("client process/request identity is incomplete")
    all_events: list[dict[str, Any]] = []
    for path, byte_end in retained_traces(report_client):
        all_events.extend(load_jsonl(path, byte_end))
    request_start_qpc = min(
        (
            trace_int(event.get("ts_qpc"), 0)
            for event in all_events
            if event.get("event") == "rollback_production_status"
            and str(event.get("request_id") or "") == expected_request
            and trace_int(event.get("pid"), 0) == expected_pid
            and trace_int(event.get("process_start_marker"), 0)
                == expected_marker
            and trace_int(event.get("ts_qpc"), 0) > 0
        ),
        default=0,
    )
    if request_start_qpc <= 0:
        raise ValueError("request-scoped production status is missing")
    return (
        all_events, expected_pid, expected_marker, expected_request,
        request_start_qpc,
    )


def event_matches_client_scope(
    event: dict[str, Any],
    expected_pid: int,
    expected_marker: int,
    expected_request: str,
    request_start_qpc: int,
) -> bool:
    if trace_int(event.get("pid"), 0) != expected_pid \
            or trace_int(event.get("process_start_marker"), 0) != \
                expected_marker \
            or trace_int(event.get("ts_qpc"), 0) < request_start_qpc:
        return False
    event_request = event.get("request_id")
    return event_request is None or str(event_request) == expected_request


def root_motion_transactions(
    events: list[dict[str, Any]],
    *,
    scope: tuple[int, int, str, int] | None = None,
    ownership: str | None = None,
) -> list[dict[str, Any]]:
    grouped: dict[int, list[dict[str, Any]]] = {}
    for event in events:
        if event.get("event") != ROOT_MOTION_SAMPLE_EVENT:
            continue
        if ownership is not None \
                and str(event.get("simulation_ownership") or "") \
                != ownership:
            continue
        if scope is not None and not event_matches_client_scope(
                event, *scope):
            continue
        transaction_id = trace_int(event.get("transaction_id"), 0)
        if transaction_id <= 0 \
                or event.get("transaction_active") is not True:
            continue
        grouped.setdefault(transaction_id, []).append(event)

    transactions: list[dict[str, Any]] = []
    for transaction_id, calls in grouped.items():
        calls.sort(key=lambda event: (
            trace_int(event.get("player"), 0),
            trace_int(event.get("call_ordinal"), 0),
            trace_int(event.get("ts_qpc"), 0),
        ))
        completion_qpc = max(
            trace_int(event.get("ts_qpc"), 0) for event in calls
        )
        if completion_qpc <= 0:
            continue
        transactions.append({
            "transaction_id": transaction_id,
            "completion_qpc": completion_qpc,
            "calls": calls,
        })
    transactions.sort(key=lambda value: (
        value["completion_qpc"], value["transaction_id"]
    ))
    return transactions


def root_motion_transaction_before(
    transactions: list[dict[str, Any]],
    qpc: int,
) -> dict[str, Any] | None:
    if qpc <= 0 or not transactions:
        return None
    qpcs = [value["completion_qpc"] for value in transactions]
    index = bisect.bisect_right(qpcs, qpc) - 1
    return transactions[index] if index >= 0 else None


def root_motion_transaction_output_key(
    transaction: dict[str, Any],
) -> tuple[Any, ...] | None:
    calls = {
        (trace_int(event.get("player"), 0),
         str(event.get("callsite_phase") or ""),
         trace_int(event.get("call_ordinal"), 0)): event
        for event in transaction.get("calls", [])
    }
    values: list[Any] = []
    for player in (1, 2):
        event = calls.get((player, f"common-p{player}", 1))
        if event is None:
            return None
        for suffix in ROOT_MOTION_CALL_FLOAT_SUFFIXES:
            value = event.get(f"after_{suffix}")
            if value is None:
                return None
            values.append(float(value))
        values.append(trace_int(event.get("after_carry_mode"), -1))
    return tuple(values)


def root_motion_frame_output_key(
    frame: dict[str, Any],
) -> tuple[Any, ...] | None:
    values: list[Any] = []
    for player in ("p1", "p2"):
        for suffix in ROOT_MOTION_FRAME_FLOAT_SUFFIXES:
            value = frame.get(f"{player}_{suffix}")
            if value is None:
                return None
            values.append(float(value))
        mode = frame.get(f"{player}_root_motion_carry_mode")
        if mode is None:
            return None
        values.append(trace_int(mode, -1))
    return tuple(values)


def root_motion_transaction_output_index(
    transactions: list[dict[str, Any]],
) -> dict[tuple[Any, ...], tuple[list[int], list[dict[str, Any]]]]:
    grouped: dict[tuple[Any, ...], list[dict[str, Any]]] = {}
    for transaction in transactions:
        key = root_motion_transaction_output_key(transaction)
        if key is not None:
            grouped.setdefault(key, []).append(transaction)
    return {
        key: (
            [trace_int(value.get("completion_qpc"), 0)
             for value in values],
            values,
        )
        for key, values in grouped.items()
    }


def root_motion_transaction_for_frame(
    index: dict[
        tuple[Any, ...], tuple[list[int], list[dict[str, Any]]]
    ],
    frame: dict[str, Any],
) -> dict[str, Any] | None:
    key = root_motion_frame_output_key(frame)
    qpc = trace_int(frame.get("ts_qpc"), 0)
    if key is None or qpc <= 0:
        return None
    entry = index.get(key)
    if entry is None:
        return None
    qpcs, transactions = entry
    position = bisect.bisect_right(qpcs, qpc) - 1
    return transactions[position] if position >= 0 else None


def root_motion_call_key(event: dict[str, Any]) -> tuple[int, str, int]:
    return (
        trace_int(event.get("player"), 0),
        str(event.get("callsite_phase") or "unknown"),
        trace_int(event.get("call_ordinal"), 0),
    )


def pose_producer_transactions(
    events: list[dict[str, Any]],
    *,
    scope: tuple[int, int, str, int] | None = None,
    ownership: str | None = None,
) -> list[dict[str, Any]]:
    grouped: dict[int, list[dict[str, Any]]] = {}
    for event in events:
        if event.get("event") != POSE_PRODUCER_CHECKPOINT_EVENT:
            continue
        if ownership is not None \
                and str(event.get("simulation_ownership") or "") \
                != ownership:
            continue
        if scope is not None and not event_matches_client_scope(
                event, *scope):
            continue
        transaction_id = trace_int(
            event.get("producer_transaction_id"), 0
        )
        if transaction_id <= 0:
            continue
        grouped.setdefault(transaction_id, []).append(event)

    transactions: list[dict[str, Any]] = []
    for transaction_id, checkpoints in grouped.items():
        checkpoints.sort(key=lambda event: trace_int(
            event.get("ts_qpc"), 0
        ))
        exit_events = [
            event for event in checkpoints
            if event.get("stage") == "TickCharaMainSimulation"
            and event.get("phase") == "exit"
        ]
        if len(exit_events) != 1:
            continue
        completion_qpc = trace_int(
            exit_events[0].get("ts_qpc"), 0
        )
        if completion_qpc <= 0:
            continue
        transactions.append({
            "transaction_id": transaction_id,
            "completion_qpc": completion_qpc,
            "player": trace_int(exit_events[0].get("player"), 0),
            "checkpoints": checkpoints,
            "output": exit_events[0],
        })
    transactions.sort(key=lambda value: (
        value["completion_qpc"], value["transaction_id"]
    ))
    return transactions


def pre_main_motion_transactions(
    events: list[dict[str, Any]],
    *,
    scope: tuple[int, int, str, int] | None = None,
    ownership: str | None = None,
) -> dict[int, dict[str, Any]]:
    """Group the pre-main pipeline by its causal P2 pose transaction.

    Every checkpoint describes both characters.  P2 is used as the key because
    the focused defect is first visible in P2's immediately following native
    producer and because no pre-main callback occurs between the P1 and P2
    producer transactions.
    """
    grouped: dict[int, list[dict[str, Any]]] = {}
    for event in events:
        if event.get("event") != PRE_MAIN_MOTION_CHECKPOINT_EVENT:
            continue
        if ownership is not None \
                and str(event.get("simulation_ownership") or "") \
                != ownership:
            continue
        if scope is not None and not event_matches_client_scope(
                event, *scope):
            continue
        transaction_id = trace_int(
            event.get("causal_p2_transaction_id"), 0
        )
        if transaction_id <= 0:
            continue
        grouped.setdefault(transaction_id, []).append(event)

    transactions: dict[int, dict[str, Any]] = {}
    for transaction_id, checkpoints in grouped.items():
        checkpoints.sort(key=lambda event: (
            trace_int(event.get("ts_qpc"), 0),
            trace_int(event.get("checkpoint_sequence"), 0),
        ))
        transactions[transaction_id] = {
            "transaction_id": transaction_id,
            "checkpoints": checkpoints,
            "sequence": [
                (str(event.get("stage") or ""),
                 str(event.get("phase") or ""))
                for event in checkpoints
            ],
        }
    return transactions


def pre_main_dynamic_rng_fields(
    expected: dict[str, Any],
    actual: dict[str, Any],
) -> list[str]:
    prefixes = ("rng_lfsr_caller_", "rng_xorshift96_caller_")
    return sorted(
        key for key in set(expected) | set(actual)
        if key.startswith(prefixes)
        and (key.endswith("_rva") or key.endswith("_count"))
    )


def pre_main_motion_transaction_differences(
    expected: dict[str, Any] | None,
    actual: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    if expected is None and actual is None:
        return []
    if expected is None or actual is None:
        return [{
            "category": "pre-main-motion-transaction",
            "field": "transaction_presence",
            "expected": expected is not None,
            "actual": actual is not None,
        }]
    expected_sequence = expected.get("sequence", [])
    actual_sequence = actual.get("sequence", [])
    if expected_sequence != actual_sequence:
        return [{
            "category": "pre-main-motion-order",
            "field": "checkpoint_sequence",
            "expected": [list(value) for value in expected_sequence],
            "actual": [list(value) for value in actual_sequence],
        }]

    differences: list[dict[str, Any]] = []
    for checkpoint_index, (expected_event, actual_event) in enumerate(zip(
            expected.get("checkpoints", []),
            actual.get("checkpoints", []))):
        checkpoint = list(expected_sequence[checkpoint_index])
        fields = (
            *PRE_MAIN_MOTION_EXACT_FIELDS,
            *pre_main_dynamic_rng_fields(expected_event, actual_event),
        )
        for field in fields:
            if not equivalent_exact(
                    expected_event.get(field), actual_event.get(field)):
                differences.append({
                    "category": "pre-main-motion-primitive",
                    "checkpoint": checkpoint,
                    "field": field,
                    "expected": expected_event.get(field),
                    "actual": actual_event.get(field),
                })
    return differences


def pose_producer_matrix_key(
    event: dict[str, Any],
    prefix: str,
) -> tuple[float, ...] | None:
    values: list[float] = []
    for row in range(4):
        for column in range(4):
            value = event.get(f"{prefix}_m{row}{column}")
            if value is None:
                return None
            values.append(float(value))
    return tuple(values)


def pose_producer_output_index(
    transactions: list[dict[str, Any]],
) -> dict[
    tuple[int, tuple[float, ...]],
    tuple[list[int], list[dict[str, Any]]],
]:
    grouped: dict[
        tuple[int, tuple[float, ...]],
        list[dict[str, Any]],
    ] = {}
    for transaction in transactions:
        key = pose_producer_matrix_key(
            transaction["output"], "bone1_current"
        )
        player = trace_int(transaction.get("player"), 0)
        if key is not None and player in (1, 2):
            grouped.setdefault((player, key), []).append(transaction)
    return {
        key: (
            [trace_int(value.get("completion_qpc"), 0)
             for value in values],
            values,
        )
        for key, values in grouped.items()
    }


def pose_producer_for_root_call(
    index: dict[
        tuple[int, tuple[float, ...]],
        tuple[list[int], list[dict[str, Any]]],
    ],
    root_call: dict[str, Any] | None,
) -> dict[str, Any] | None:
    if root_call is None:
        return None
    player = trace_int(root_call.get("player"), 0)
    key = pose_producer_matrix_key(root_call, "before_current")
    qpc = trace_int(root_call.get("ts_qpc"), 0)
    if key is None or qpc <= 0:
        return None
    entry = index.get((player, key))
    if entry is None:
        return None
    qpcs, transactions = entry
    position = bisect.bisect_right(qpcs, qpc) - 1
    return transactions[position] if position >= 0 else None


def causal_pose_producer_for_root_call(
    transactions: list[dict[str, Any]],
    root_call: dict[str, Any] | None,
) -> dict[str, Any] | None:
    if root_call is None:
        return None
    player = trace_int(root_call.get("player"), 0)
    qpc = trace_int(root_call.get("ts_qpc"), 0)
    coordinate = trace_int(
        root_call.get("native_applied_coordinate"),
        INVALID_FRAME_COORDINATE,
    )
    if player not in (1, 2) or qpc <= 0:
        return None
    candidates = [
        transaction for transaction in transactions
        if trace_int(transaction.get("player"), 0) == player
        and trace_int(transaction.get("completion_qpc"), 0) <= qpc
    ]
    if coordinate != INVALID_FRAME_COORDINATE:
        same_coordinate = [
            transaction for transaction in candidates
            if trace_int(
                transaction.get("output", {}).get(
                    "native_applied_coordinate"
                ),
                INVALID_FRAME_COORDINATE,
            ) == coordinate
        ]
        if same_coordinate:
            candidates = same_coordinate
    if not candidates:
        return None
    return max(
        candidates,
        key=lambda transaction: (
            trace_int(transaction.get("completion_qpc"), 0),
            trace_int(transaction.get("transaction_id"), 0),
        ),
    )


def collision_transactions(
    events: list[dict[str, Any]],
    *,
    scope: tuple[int, int, str, int] | None = None,
    ownership: str | None = None,
) -> dict[int, dict[str, Any]]:
    grouped: dict[int, list[dict[str, Any]]] = {}
    for event in events:
        if event.get("event") != COLLISION_CHECKPOINT_EVENT:
            continue
        if ownership is not None \
                and str(event.get("simulation_ownership") or "") \
                != ownership:
            continue
        if scope is not None and not event_matches_client_scope(
                event, *scope):
            continue
        transaction_id = trace_int(event.get("transaction_id"), 0)
        if transaction_id <= 0:
            continue
        grouped.setdefault(transaction_id, []).append(event)

    transactions: dict[int, dict[str, Any]] = {}
    for transaction_id, checkpoints in grouped.items():
        checkpoints.sort(key=lambda event: trace_int(
            event.get("ts_qpc"), 0
        ))
        if not checkpoints:
            continue
        sequence = [
            (str(event.get("stage") or ""),
             str(event.get("phase") or ""))
            for event in checkpoints
        ]
        complete = sequence[-1] == (
            "TickHitResolutionAndBodyCollision", "exit"
        )
        transactions[transaction_id] = {
            "transaction_id": transaction_id,
            "checkpoints": checkpoints,
            "sequence": sequence,
            "complete": complete,
        }
    return transactions


def root_motion_common_call(
    transaction: dict[str, Any] | None,
    player: int,
) -> dict[str, Any] | None:
    if transaction is None:
        return None
    for event in transaction.get("calls", []):
        if trace_int(event.get("player"), 0) == player \
                and event.get("callsite_phase") == f"common-p{player}" \
                and trace_int(event.get("call_ordinal"), 0) == 1:
            return event
    return None


def collision_transaction_differences(
    expected: dict[str, Any] | None,
    actual: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    if expected is None and actual is None:
        return []
    if expected is None or actual is None:
        return [{
            "category": "collision-transaction",
            "field": "transaction_presence",
            "expected": expected is not None,
            "actual": actual is not None,
        }]
    expected_sequence = expected.get("sequence", [])
    actual_sequence = actual.get("sequence", [])
    if expected_sequence != actual_sequence:
        return [{
            "category": "collision-order",
            "field": "checkpoint_sequence",
            "expected": [list(value) for value in expected_sequence],
            "actual": [list(value) for value in actual_sequence],
        }]

    differences: list[dict[str, Any]] = []
    for checkpoint_index, (expected_event, actual_event) in enumerate(
            zip(
                expected.get("checkpoints", []),
                actual.get("checkpoints", []))):
        checkpoint = list(expected_sequence[checkpoint_index])
        for field in checkpoint_dynamic_semantic_hash_fields(
                expected_event, actual_event):
            if not equivalent_exact(
                    expected_event.get(field), actual_event.get(field)):
                differences.append({
                    "category": "collision-primitive",
                    "checkpoint": checkpoint,
                    "field": field,
                    "expected": expected_event.get(field),
                    "actual": actual_event.get(field),
                })
        for field in COLLISION_CHECKPOINT_EXACT_FIELDS:
            if not equivalent_exact(
                    expected_event.get(field), actual_event.get(field)):
                differences.append({
                    "category": "collision-primitive",
                    "checkpoint": checkpoint,
                    "field": field,
                    "expected": expected_event.get(field),
                    "actual": actual_event.get(field),
                })
        for field in COLLISION_CHECKPOINT_FLOAT_FIELDS:
            expected_value = expected_event.get(field)
            actual_value = actual_event.get(field)
            if expected_value is None or actual_value is None \
                    or float(expected_value) != float(actual_value):
                differences.append({
                    "category": "collision-primitive",
                    "checkpoint": checkpoint,
                    "field": field,
                    "expected": expected_value,
                    "actual": actual_value,
                })
    return differences


def pose_producer_transaction_differences(
    expected: dict[str, Any] | None,
    actual: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    if expected is None and actual is None:
        return []
    if expected is None or actual is None:
        return [{
            "category": "pose-producer-transaction",
            "field": "transaction_presence",
            "expected": expected is not None,
            "actual": actual is not None,
        }]
    expected_checkpoints = expected.get("checkpoints", [])
    actual_checkpoints = actual.get("checkpoints", [])
    expected_sequence = [
        (str(event.get("stage") or ""),
         str(event.get("phase") or ""))
        for event in expected_checkpoints
    ]
    actual_sequence = [
        (str(event.get("stage") or ""),
         str(event.get("phase") or ""))
        for event in actual_checkpoints
    ]
    if expected_sequence != actual_sequence:
        return [{
            "category": "pose-producer-order",
            "field": "checkpoint_sequence",
            "expected": [list(value) for value in expected_sequence],
            "actual": [list(value) for value in actual_sequence],
        }]

    differences: list[dict[str, Any]] = []
    for checkpoint_index, (expected_event, actual_event) in enumerate(
            zip(expected_checkpoints, actual_checkpoints)):
        stage = list(expected_sequence[checkpoint_index])
        for field in checkpoint_dynamic_semantic_hash_fields(
                expected_event, actual_event):
            if not equivalent_exact(
                    expected_event.get(field), actual_event.get(field)):
                differences.append({
                    "category": "pose-producer-primitive",
                    "checkpoint": stage,
                    "field": field,
                    "expected": expected_event.get(field),
                    "actual": actual_event.get(field),
                })
        for field in POSE_PRODUCER_EXACT_FIELDS:
            if not equivalent_exact(
                    expected_event.get(field), actual_event.get(field)):
                differences.append({
                    "category": "pose-producer-primitive",
                    "checkpoint": stage,
                    "field": field,
                    "expected": expected_event.get(field),
                    "actual": actual_event.get(field),
                })
        for field in POSE_PRODUCER_FLOAT_FIELDS:
            expected_value = expected_event.get(field)
            actual_value = actual_event.get(field)
            if expected_value is None or actual_value is None \
                    or float(expected_value) != float(actual_value):
                differences.append({
                    "category": "pose-producer-primitive",
                    "checkpoint": stage,
                    "field": field,
                    "expected": expected_value,
                    "actual": actual_value,
                })
    return differences


def root_motion_transaction_differences(
    expected: dict[str, Any] | None,
    actual: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    if expected is None and actual is None:
        return []
    if expected is None or actual is None:
        return [{
            "category": "root-motion-transaction",
            "field": "transaction_presence",
            "expected": expected is not None,
            "actual": actual is not None,
        }]
    expected_calls = {
        root_motion_call_key(event): event
        for event in expected.get("calls", [])
    }
    actual_calls = {
        root_motion_call_key(event): event
        for event in actual.get("calls", [])
    }
    if set(expected_calls) != set(actual_calls):
        return [{
            "category": "root-motion-call-order",
            "field": "call_sequence",
            "expected": [list(key) for key in expected_calls],
            "actual": [list(key) for key in actual_calls],
        }]

    differences: list[dict[str, Any]] = []
    for key in expected_calls:
        expected_call = expected_calls[key]
        actual_call = actual_calls[key]
        for field in ROOT_MOTION_EXACT_FIELDS:
            if not equivalent_exact(
                    expected_call.get(field), actual_call.get(field)):
                differences.append({
                    "category": "root-motion-primitive",
                    "call": list(key),
                    "field": field,
                    "expected": expected_call.get(field),
                    "actual": actual_call.get(field),
                })
        for field in ROOT_MOTION_FLOAT_FIELDS:
            expected_value = expected_call.get(field)
            actual_value = actual_call.get(field)
            if expected_value is None or actual_value is None \
                    or float(expected_value) != float(actual_value):
                differences.append({
                    "category": "root-motion-primitive",
                    "call": list(key),
                    "field": field,
                    "expected": expected_value,
                    "actual": actual_value,
                })
    return differences


def root_motion_frame_evidence(
    oracle_path: Path,
    oracle_byte_end: int,
    rounds: dict[int, list[dict[str, Any]]],
    clients: dict[str, dict[tuple[int, int], dict[str, Any]]],
    report_clients: dict[str, dict[str, Any]],
    origins: dict[int, int],
    seq_origins: dict[int, int],
    common_keys: list[tuple[int, int]],
) -> dict[str, Any]:
    normal_events = load_jsonl(oracle_path, oracle_byte_end)
    normal_transactions = root_motion_transactions(
        normal_events, ownership="stock"
    )
    normal_transaction_index = root_motion_transaction_output_index(
        normal_transactions
    )
    normal_pose_producers = pose_producer_transactions(
        normal_events, ownership="stock"
    )
    normal_pre_main_motion = pre_main_motion_transactions(
        normal_events, ownership="stock"
    )
    normal_collision_transactions = collision_transactions(
        normal_events, ownership="stock"
    )
    client_transactions: dict[str, list[dict[str, Any]]] = {}
    client_transaction_indices: dict[
        str,
        dict[tuple[Any, ...], tuple[list[int], list[dict[str, Any]]]],
    ] = {}
    client_pose_producers: dict[str, list[dict[str, Any]]] = {}
    client_pre_main_motion: dict[str, dict[int, dict[str, Any]]] = {}
    client_collision_transactions: dict[
        str, dict[int, dict[str, Any]]
    ] = {}
    for role, report_client in report_clients.items():
        events, pid, marker, request, request_start_qpc = \
            scoped_client_events(report_client)
        scope = (pid, marker, request, request_start_qpc)
        client_transactions[role] = root_motion_transactions(
            events,
            scope=scope,
            ownership="rollback-owned",
        )
        client_transaction_indices[role] = \
            root_motion_transaction_output_index(
                client_transactions[role]
            )
        client_pose_producers[role] = pose_producer_transactions(
            events,
            scope=scope,
            ownership="rollback-owned",
        )
        client_pre_main_motion[role] = pre_main_motion_transactions(
            events,
            scope=scope,
            ownership="rollback-owned",
        )
        client_collision_transactions[role] = collision_transactions(
            events,
            scope=scope,
            ownership="rollback-owned",
        )

    first_mismatch: dict[str, Any] | None = None
    compared = 0
    frames_without_matching_transaction = 0
    for replay_round, logical_frame in common_keys:
        normal_frame = oracle_frame(
            rounds, origins, seq_origins,
            replay_round, logical_frame
        )
        if normal_frame is None:
            continue
        expected = root_motion_transaction_for_frame(
            normal_transaction_index, normal_frame
        )
        actual_frames = {
            role: clients[role].get((replay_round, logical_frame))
            for role in ("host", "sandbox")
        }
        actual_transactions = {
            role: root_motion_transaction_for_frame(
                client_transaction_indices[role],
                actual_frames[role] or {},
            )
            for role in ("host", "sandbox")
        }
        if expected is None and all(
                value is None for value in actual_transactions.values()):
            frames_without_matching_transaction += 1
            continue
        per_role: dict[str, Any] = {}
        mismatch = False
        for role in ("host", "sandbox"):
            actual = actual_transactions[role]
            differences = root_motion_transaction_differences(
                expected, actual
            )
            expected_collision = normal_collision_transactions.get(
                trace_int(
                    expected.get("transaction_id"), 0
                ) if expected else 0
            )
            actual_collision = client_collision_transactions[role].get(
                trace_int(
                    actual.get("transaction_id"), 0
                ) if actual else 0
            )
            collision_differences = collision_transaction_differences(
                expected_collision, actual_collision
            )
            producer_evidence: dict[str, Any] = {}
            producer_mismatch = False
            expected_p2_producer: dict[str, Any] | None = None
            actual_p2_producer: dict[str, Any] | None = None
            for player in (1, 2):
                expected_producer = causal_pose_producer_for_root_call(
                    normal_pose_producers,
                    root_motion_common_call(expected, player),
                )
                actual_producer = causal_pose_producer_for_root_call(
                    client_pose_producers[role],
                    root_motion_common_call(actual, player),
                )
                producer_differences = pose_producer_transaction_differences(
                    expected_producer,
                    actual_producer,
                )
                producer_mismatch = (
                    producer_mismatch or bool(producer_differences)
                )
                producer_evidence[f"p{player}"] = {
                    "expected_transaction_id": (
                        expected_producer.get("transaction_id")
                        if expected_producer else None
                    ),
                    "actual_transaction_id": (
                        actual_producer.get("transaction_id")
                        if actual_producer else None
                    ),
                    "differences": producer_differences[:96],
                }
                if player == 2:
                    expected_p2_producer = expected_producer
                    actual_p2_producer = actual_producer
            expected_pre_main_id = trace_int(
                expected_p2_producer.get("transaction_id"), 0
            ) if expected_p2_producer else 0
            actual_pre_main_id = trace_int(
                actual_p2_producer.get("transaction_id"), 0
            ) if actual_p2_producer else 0
            expected_pre_main = normal_pre_main_motion.get(
                expected_pre_main_id
            )
            actual_pre_main = client_pre_main_motion[role].get(
                actual_pre_main_id
            )
            pre_main_differences = pre_main_motion_transaction_differences(
                expected_pre_main, actual_pre_main
            )
            mismatch = (
                mismatch
                or bool(differences)
                or bool(collision_differences)
                or producer_mismatch
                or bool(pre_main_differences)
            )
            per_role[role] = {
                "expected_transaction_id": (
                    expected.get("transaction_id")
                    if expected else None
                ),
                "actual_transaction_id": (
                    actual.get("transaction_id")
                    if actual else None
                ),
                "selected_by_confirmed_output": actual is not None,
                "differences": differences[:64],
                "pose_producer": producer_evidence,
                "pre_main_motion": {
                    "expected_causal_p2_transaction_id": (
                        expected_pre_main_id or None
                    ),
                    "actual_causal_p2_transaction_id": (
                        actual_pre_main_id or None
                    ),
                    "differences": pre_main_differences[:128],
                },
                "collision": {
                    "expected_transaction_id": (
                        expected_collision.get("transaction_id")
                        if expected_collision else None
                    ),
                    "actual_transaction_id": (
                        actual_collision.get("transaction_id")
                        if actual_collision else None
                    ),
                    "differences": collision_differences[:128],
                },
            }
        compared += 1
        if mismatch:
            first_mismatch = {
                "replay_round": replay_round,
                "logical_frame": logical_frame,
                "normal_oracle_seq": trace_int(
                    normal_frame.get("seq"), -1
                ),
                "clients": per_role,
            }
            break
    return {
        "normal_transaction_count": len(normal_transactions),
        "client_transaction_counts": {
            role: len(values)
            for role, values in client_transactions.items()
        },
        "normal_pose_producer_transaction_count":
            len(normal_pose_producers),
        "client_pose_producer_transaction_counts": {
            role: len(values)
            for role, values in client_pose_producers.items()
        },
        "normal_pre_main_motion_transaction_count":
            len(normal_pre_main_motion),
        "client_pre_main_motion_transaction_counts": {
            role: len(values)
            for role, values in client_pre_main_motion.items()
        },
        "normal_collision_transaction_count":
            len(normal_collision_transactions),
        "client_collision_transaction_counts": {
            role: len(values)
            for role, values in client_collision_transactions.items()
        },
        "compared_frames": compared,
        "frames_without_matching_transaction":
            frames_without_matching_transaction,
        "first_mismatch": first_mismatch,
    }


def rng_tuple(event: dict[str, Any], prefix: str = "") -> dict[str, int]:
    return {
        field: trace_int(event.get(f"{prefix}{field}"))
        for field in RNG_FIELDS
    }


def normalized_coordinate(value: Any) -> int | None:
    coordinate = trace_int(value)
    return None if coordinate in (-1, INVALID_FRAME_COORDINATE) \
        else coordinate


def normalized_rng_callers(event: dict[str, Any]) -> dict[str, Any]:
    caller_count = max(0, trace_int(event.get("caller_count"), 0))
    callers: list[dict[str, Any]] = []
    for index in range(caller_count):
        count = trace_int(event.get(f"caller{index}_count"), 0)
        rva = event.get(f"caller{index}_rva")
        if count <= 0 or rva is None:
            continue
        callers.append({
            "rva": rva,
            "count": count,
            "function": event.get(f"caller{index}_fn"),
        })
    return {
        "ts_qpc": trace_int(event.get("ts_qpc"), 0),
        "phase": str(event.get("phase") or ""),
        "label": str(event.get("label") or ""),
        "rng_family": str(event.get("rng_family") or
                          RNG_CALLERS_EVENTS.get(
                              str(event.get("event") or ""), "unknown")),
        "total_calls": trace_int(event.get("total_calls"), 0),
        "overflow_calls": trace_int(event.get("overflow_calls"), 0),
        "seed_calls": trace_int(event.get("seed_calls"), 0),
        "last_seed": trace_int(event.get("last_seed"), 0),
        "event_count": trace_int(event.get("event_count"), 0),
        "overflow_events": trace_int(event.get("overflow_events"), 0),
        "sequence_hash": event.get("sequence_hash"),
        "replay_round": normalized_coordinate(event.get("replay_round")),
        "logical_frame": normalized_coordinate(event.get("logical_frame")),
        "source_index": normalized_coordinate(event.get("source_index")),
        "round_epoch": trace_int(event.get("round_epoch"), 0),
        "round_generation": trace_int(event.get("round_generation"), 0),
        "callers": callers,
    }


def ring_in_five_call_fingerprint(
    expected: dict[str, Any],
    actual: dict[str, Any],
) -> dict[str, Any]:
    expected_counts = {
        trace_int(value.get("rva")): trace_int(value.get("count"), 0)
        for value in expected.get("callers", [])
    }
    actual_counts = {
        trace_int(value.get("rva")): trace_int(value.get("count"), 0)
        for value in actual.get("callers", [])
    }
    keys = set(expected_counts) | set(actual_counts)
    delta = {
        rva: actual_counts.get(rva, 0) - expected_counts.get(rva, 0)
        for rva in keys
        if actual_counts.get(rva, 0) != expected_counts.get(rva, 0)
    }
    expected_delta = {
        0x33371F: 1,
        0x3337A5: 2,
        0x3337F2: 2,
    }
    return {
        "matched": delta == expected_delta,
        "caller_delta": {
            hex(rva): count for rva, count in sorted(delta.items())
        },
        "attribution": (
            "one-extra-IwWind_UpdateRingInOscillation-iteration"
            if delta == expected_delta else None
        ),
    }


def normal_rng_caller_windows(
    path: Path,
    byte_end: int,
    rounds: dict[int, list[dict[str, Any]]],
) -> dict[tuple[int, int], dict[str, Any]]:
    events = load_jsonl(path, byte_end)
    result: dict[tuple[int, int], dict[str, Any]] = {}
    for event_name, family in RNG_CALLERS_EVENTS.items():
        candidates = [
            event for event in events
            if event.get("event") == event_name
            and str(event.get("phase") or "") == "generation"
            and str(event.get("label") or "") == "oracle-frame"
        ]
        candidates.sort(key=lambda event: trace_int(event.get("ts_qpc"), 0))
        candidate_qpcs = [
            trace_int(event.get("ts_qpc"), 0) for event in candidates
        ]
        for replay_round, frames in rounds.items():
            ordered_frames = sorted(
                frames, key=lambda frame: trace_int(
                    frame.get("ts_qpc"), 0
                )
            )
            for frame_index, frame in enumerate(ordered_frames):
                seq = trace_int(frame.get("seq"))
                frame_qpc = trace_int(frame.get("ts_qpc"), 0)
                if frame_qpc <= 0:
                    continue
                candidate_index = bisect.bisect_left(
                    candidate_qpcs, frame_qpc
                )
                if candidate_index >= len(candidates):
                    continue
                event = candidates[candidate_index]
                normalized = normalized_rng_callers(event)
                next_qpc = (
                    trace_int(
                        ordered_frames[frame_index + 1].get(
                            "ts_qpc"), 0
                    )
                    if frame_index + 1 < len(ordered_frames)
                    else 0
                )
                if next_qpc > frame_qpc \
                        and normalized["ts_qpc"] >= next_qpc:
                    continue
                result.setdefault(
                    (replay_round, seq), {}
                )[family] = normalized
    return result


def rng_caller_delta(
    expected: dict[str, Any] | None,
    actual: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    if expected is None or actual is None:
        return []
    expected_counts = {
        str(value.get("rva")): trace_int(value.get("count"), 0)
        for value in expected.get("callers", [])
    }
    actual_counts = {
        str(value.get("rva")): trace_int(value.get("count"), 0)
        for value in actual.get("callers", [])
    }
    functions = {
        str(value.get("rva")): value.get("function")
        for value in (
            list(expected.get("callers", []))
            + list(actual.get("callers", []))
        )
    }
    return [
        {
            "rva": rva,
            "expected_count": expected_counts.get(rva, 0),
            "actual_count": actual_counts.get(rva, 0),
            "delta": actual_counts.get(rva, 0)
                - expected_counts.get(rva, 0),
            "function": functions.get(rva),
        }
        for rva in sorted(set(expected_counts) | set(actual_counts))
        if expected_counts.get(rva, 0) != actual_counts.get(rva, 0)
    ]


def rng_checkpoint_evidence(
    report_client: dict[str, Any],
    selected_epochs: dict[int, int],
) -> dict[str, Any]:
    events, pid, marker, request, request_start_qpc = scoped_client_events(
        report_client
    )
    callers = []
    for event in events:
        if event.get("event") not in RNG_CALLERS_EVENTS \
                or str(event.get("phase") or "") not in {
                    "rollback-frame-zero", "rollback-logical-frame"} \
                or not event_matches_client_scope(
                    event, pid, marker, request, request_start_qpc):
            continue
        normalized = normalized_rng_callers(event)
        replay_round = normalized["replay_round"]
        if replay_round is not None \
                and replay_round in selected_epochs \
                and selected_epochs[replay_round] \
                    != normalized["round_epoch"]:
            continue
        callers.append(normalized)
    checkpoints: list[dict[str, Any]] = []
    for event in events:
        if event.get("event") != RNG_CHECKPOINT_EVENT \
                or not event_matches_client_scope(
                    event, pid, marker, request, request_start_qpc):
            continue
        replay_round = normalized_coordinate(event.get("replay_round"))
        round_epoch = trace_int(event.get("round_epoch"), 0)
        if replay_round is not None \
                and replay_round in selected_epochs \
                and selected_epochs[replay_round] != round_epoch:
            continue
        actual = rng_tuple(event)
        expected = rng_tuple(event, "expected_")
        expected_match = event.get("expected_match")
        expected_complete = all(
            f"expected_{field}" in event for field in RNG_FIELDS
        )
        mismatch = expected_match is False or (
            expected_complete and actual != expected
        )
        checkpoint = {
            "ts_qpc": trace_int(event.get("ts_qpc"), 0),
            "capture_phase": str(event.get("capture_phase") or ""),
            "lifecycle_epoch": event.get("lifecycle_epoch"),
            "round_epoch": event.get("round_epoch"),
            "round_generation": event.get("round_generation"),
            "replay_round": replay_round,
            "logical_frame": normalized_coordinate(
                event.get("logical_frame")
            ),
            "source_index": normalized_coordinate(event.get("source_index")),
            "actual": actual,
            "expected": expected,
            "expected_match": expected_match,
            "canonical_hash": event.get("canonical_hash"),
            "mismatch": mismatch,
        }
        if mismatch and callers:
            checkpoint["nearest_caller_window"] = min(
                callers,
                key=lambda value: abs(
                    value["ts_qpc"] - checkpoint["ts_qpc"]
                ),
            )
        checkpoints.append(checkpoint)
    checkpoints.sort(key=lambda value: value["ts_qpc"])
    callers.sort(key=lambda value: value["ts_qpc"])
    return {
        "checkpoint_count": len(checkpoints),
        "caller_window_count": len(callers),
        "first_mismatch": next(
            (value for value in checkpoints if value["mismatch"]), None
        ),
        "checkpoints": checkpoints,
        "caller_windows": callers,
    }


def equivalent_exact(expected: Any, actual: Any) -> bool:
    if isinstance(expected, bool) or isinstance(actual, bool):
        return bool(expected) == bool(actual)
    return trace_int(expected, -1) == trace_int(actual, -2)


def field_value(event: dict[str, Any] | None, field: str) -> Any:
    if event is None:
        return None
    if field == "p1_gekko_input":
        return event.get("p1_gekko_input", event.get("p1_input"))
    if field == "p2_gekko_input":
        return event.get("p2_gekko_input", event.get("p2_input"))
    return event.get(field)


def inactive_hit_cue_suffix(
    expected: dict[str, Any],
    actual: dict[str, Any],
    player: str,
    suffix: str,
) -> bool:
    """Return true for residue that native code cannot consume this frame.

    ApplyBattleCharaMotionSlotRootMotionDirectPositionWrite checks the
    corresponding slot's active-cue sentinel before evaluating its pose lane
    or consuming node/blend state.  When both sides are -1, those values are
    dormant residue; the active-cue field itself remains exact and a live
    slot continues to compare every semantic field.
    """
    for slot in range(4):
        slot_prefix = f"hitcue{slot}_"
        lane_prefix = f"hitcue_pose_lane{slot}_"
        if not suffix.startswith((slot_prefix, lane_prefix)):
            continue
        if suffix == f"hitcue{slot}_active_cue":
            return False
        field = f"{player}_hitcue{slot}_active_cue"
        return trace_int(expected.get(field), -2) == -1 \
            and trace_int(actual.get(field), -3) == -1
    return False


def frame_differences(
    expected: dict[str, Any],
    actual: dict[str, Any],
    float_tolerance: float,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    aggregate: list[dict[str, Any]] = []
    primitive: list[dict[str, Any]] = []
    for field in AGGREGATE_FIELDS:
        if not equivalent_exact(expected.get(field), actual.get(field)):
            aggregate.append({
                "field": field,
                "expected": expected.get(field),
                "actual": actual.get(field),
            })
    for field in EXACT_FIELDS:
        if not equivalent_exact(expected.get(field), actual.get(field)):
            primitive.append({
                "category": field.split("_", 1)[0],
                "field": field,
                "expected": expected.get(field),
                "actual": actual.get(field),
            })
    for player in ("p1", "p2"):
        input_field = f"{player}_gekko_input"
        expected_input = trace_int(
            expected.get(f"{player}_input", expected.get(input_field))
        ) & 0xFFFFFFFF
        actual_input = trace_int(actual.get(input_field))
        if expected_input != actual_input:
            primitive.append({
                "category": "input",
                "field": input_field,
                "expected": hex(expected_input),
                "actual": actual.get(input_field),
            })
        for slot in range(4):
            field = f"{player}_hitcue{slot}_semantic_bytes"
            if field not in expected and field not in actual:
                continue
            if inactive_hit_cue_suffix(
                    expected, actual, player, f"hitcue{slot}_semantic_bytes"):
                continue
            if expected.get(field) == actual.get(field):
                continue
            detail = hit_cue_semantic_byte_difference(
                expected.get(field), actual.get(field), float_tolerance
            )
            if detail is None:
                continue
            primitive.append({
                "category": f"{player}-hitcue{slot}-semantic-byte",
                "field": (
                    f"{player}_hitcue{slot}_"
                    f"{detail.get('field', 'semantic_bytes')}"
                ),
                "expected": expected.get(field),
                "actual": actual.get(field),
                **{
                    key: value for key, value in detail.items()
                    if key != "field"
                },
            })
        for suffix in FIGHTER_EXACT_SUFFIXES:
            field = f"{player}_{suffix}"
            if field not in expected and field not in actual:
                continue
            if inactive_hit_cue_suffix(
                    expected, actual, player, suffix):
                continue
            if not equivalent_exact(expected.get(field), actual.get(field)):
                primitive.append({
                    "category": player,
                    "field": field,
                    "expected": expected.get(field),
                    "actual": actual.get(field),
                })
        for suffix in FIGHTER_FLOAT_SUFFIXES:
            field = f"{player}_{suffix}"
            if field not in expected and field not in actual:
                continue
            if inactive_hit_cue_suffix(
                    expected, actual, player, suffix):
                continue
            try:
                expected_float = float(expected[field])
                actual_float = float(actual[field])
                error = abs(expected_float - actual_float)
            except (KeyError, TypeError, ValueError):
                error = math.inf
            if error > float_tolerance:
                primitive.append({
                    "category": player,
                    "field": field,
                    "expected": expected.get(field),
                    "actual": actual.get(field),
                    "absolute_error": error,
                })
    return aggregate, primitive


def stage_wind_differences(
    expected: dict[str, Any],
    actual: dict[str, Any],
    float_tolerance: float,
) -> list[dict[str, Any]]:
    """Compare broad wind diagnostics, excluding allocator residue.

    These fields include pose/presentation-derived oscillator and force state.
    Gameplay correctness is established separately by
    stage_wind_gameplay_hash.
    """
    differences: list[dict[str, Any]] = []
    priority_fields = (
        "stage_wind_graph_count",
        "stage_wind_emitter_count",
        "stage_wind_output_active",
        "stage_wind_pending_count",
        "stage_wind_schedule_state",
        "stage_wind_effect_pair_scheduled",
    )
    for field in priority_fields:
        if field not in expected and field not in actual:
            continue
        if not equivalent_exact(expected.get(field), actual.get(field)):
            differences.append({
                "category": "stage-wind",
                "field": field,
                "expected": expected.get(field),
                "actual": actual.get(field),
            })
    expected_nodes = max(
        0, trace_int(expected.get("stage_wind_graph_count"), 0))
    actual_nodes = max(
        0, trace_int(actual.get("stage_wind_graph_count"), 0))
    for index in range(max(expected_nodes, actual_nodes)):
        for suffix in ("vtable_rva",):
            field = f"stage_wind_node_{index}_{suffix}"
            if field not in expected or field not in actual:
                continue
            if not equivalent_exact(expected.get(field), actual.get(field)):
                differences.append({
                    "category": "stage-wind-node",
                    "field": field,
                    "expected": expected.get(field),
                    "actual": actual.get(field),
                })
        for suffix in ("common_hash", "body_hash", "tail_hash"):
            field = f"stage_wind_node_{index}_{suffix}"
            if field not in expected or field not in actual:
                continue
            if not equivalent_exact(expected.get(field), actual.get(field)):
                differences.append({
                    "category": "stage-wind-node-component",
                    "field": field,
                    "expected": expected.get(field),
                    "actual": actual.get(field),
                })
        # +0x34..+0x3F (decimal words 52, 56, and 60) are native
        # allocator residue and deliberately absent from this comparison.
        for offset, label in (
            (48, "life"),
            (96, "oscillator_tick"),
            (100, "state_64"),
            (104, "prepared"),
            (108, "active"),
        ):
            field = f"stage_wind_node_{index}_word_{offset}"
            if field not in expected or field not in actual:
                continue
            if not equivalent_exact(expected.get(field), actual.get(field)):
                differences.append({
                    "category": "stage-wind-node-primitive",
                    "primitive": label,
                    "field": field,
                    "expected": expected.get(field),
                    "actual": actual.get(field),
                })
        field = f"stage_wind_node_{index}_hash"
        if ((field in expected and field in actual)
            and not equivalent_exact(
                expected.get(field), actual.get(field))):
            differences.append({
                "category": "stage-wind-node",
                "field": field,
                "expected": expected.get(field),
                "actual": actual.get(field),
            })
        vtable_rva = trace_int(
            expected.get(f"stage_wind_node_{index}_vtable_rva"),
            trace_int(actual.get(
                f"stage_wind_node_{index}_vtable_rva"), 0),
        )
        ring_in_semantic_ranges = (
            (0x70, 0xF4),
            (0xF8, 0x10C),
            (0x110, 0x11C),
            (0x130, 0x134),
            (0x148, 0x14C),
            (0x150, 0x15C),
        )
        parallel_semantic_ranges = (
            (0x70, 0xE0),
            (0x120, 0x12C),
        )
        shock_wave_semantic_ranges = (
            (0x70, 0xE4),
            (0xF0, 0x110),
            (0x120, 0x12C),
            (0x130, 0x180),
        )
        for offset in range(0x70, 0x1E0, 4):
            # Iw_WindForStage (vtable RVA 0x3E88CE8) is allocated
            # without a full clear. Compare only the same verified semantic
            # fields used by the C++ canonical/authority table.
            if vtable_rva == 0x3E88CE8 and not any(
                begin <= offset < end
                for begin, end in ring_in_semantic_ranges
            ):
                continue
            if vtable_rva in (0x3E88C88, 0x3E88CB8) and not any(
                begin <= offset < end
                for begin, end in parallel_semantic_ranges
            ):
                continue
            # Shock-wave currentAngles is a float3. +0x12C is an isolated
            # unwritten allocation word; orientation state resumes at +0x130.
            if vtable_rva == 0x3E88D18 and not any(
                begin <= offset < end
                for begin, end in shock_wave_semantic_ranges
            ):
                continue
            field = f"stage_wind_node_{index}_semantic_word_{offset}"
            if field not in expected or field not in actual:
                continue
            if not equivalent_exact(expected.get(field), actual.get(field)):
                differences.append({
                    "category": "stage-wind-node-semantic-word",
                    "field": field,
                    "offset": offset,
                    "expected": expected.get(field),
                    "actual": actual.get(field),
                })
    for offset in range(0x50, 0xA8, 4):
        # basis.w and spawnPosition.w are uninitialized source residue.
        if offset in (0x6C, 0x7C):
            continue
        field = f"stage_wind_emitter_0_word_{offset}"
        if field not in expected or field not in actual:
            continue
        if not equivalent_exact(expected.get(field), actual.get(field)):
            differences.append({
                "category": "stage-wind-emitter-word",
                "field": field,
                "offset": offset,
                "expected": expected.get(field),
                "actual": actual.get(field),
            })
    for field in STAGE_WIND_DIAGNOSTIC_FIELDS:
        if field in priority_fields:
            continue
        if field not in expected and field not in actual:
            continue
        if not equivalent_exact(expected.get(field), actual.get(field)):
            differences.append({
                "category": "stage-wind",
                "field": field,
                "expected": expected.get(field),
                "actual": actual.get(field),
            })
    return differences


def normal_control_reproducibility(
    reference_path: Path,
    candidate_path: Path,
    float_tolerance: float = 1.0e-5,
    max_differences: int = 32,
) -> dict[str, Any]:
    """Prove that the normal control reproduces semantic gameplay evidence."""
    reference_rounds = latest_oracle_rounds(
        reference_path, reference_path.stat().st_size
    )
    candidate_rounds = latest_oracle_rounds(
        candidate_path, candidate_path.stat().st_size
    )

    def indexed(
        rounds: dict[int, list[dict[str, Any]]],
    ) -> dict[tuple[int, int], dict[str, Any]]:
        values: dict[tuple[int, int], dict[str, Any]] = {}
        for replay_round, frames in rounds.items():
            for frame in frames:
                require_current_oracle_event(
                    frame, f"normal-control round={replay_round}"
                )
                key = (replay_round, trace_int(frame.get("seq")))
                if key in values:
                    raise ValueError(
                        "duplicate normal-control oracle coordinate: "
                        f"round={key[0]} seq={key[1]}"
                    )
                values[key] = frame
        return values

    reference_frames = indexed(reference_rounds)
    candidate_frames = indexed(candidate_rounds)
    reference_callers = normal_rng_caller_windows(
        reference_path, reference_path.stat().st_size, reference_rounds
    )
    candidate_callers = normal_rng_caller_windows(
        candidate_path, candidate_path.stat().st_size, candidate_rounds
    )
    reference_keys = set(reference_frames)
    candidate_keys = set(candidate_frames)
    caller_reference_keys = set(reference_callers)
    caller_candidate_keys = set(candidate_callers)
    coverage_mismatch = {
        "missing_frames": sorted(reference_keys - candidate_keys),
        "unexpected_frames": sorted(candidate_keys - reference_keys),
        "reference_missing_caller_windows":
            sorted(reference_keys - caller_reference_keys),
        "candidate_missing_caller_windows":
            sorted(candidate_keys - caller_candidate_keys),
        "missing_candidate_caller_windows":
            sorted(caller_reference_keys - caller_candidate_keys),
        "unexpected_candidate_caller_windows":
            sorted(caller_candidate_keys - caller_reference_keys),
        "reference_incomplete_caller_families": sorted(
            key for key, value in reference_callers.items()
            if set(value) != set(RNG_CALLERS_EVENTS.values())
        ),
        "candidate_incomplete_caller_families": sorted(
            key for key, value in candidate_callers.items()
            if set(value) != set(RNG_CALLERS_EVENTS.values())
        ),
    }
    coverage_ok = all(not value for value in coverage_mismatch.values())
    first_gameplay_rng_mismatch: dict[str, Any] | None = None
    first_caller_mismatch: dict[str, Any] | None = None
    first_stage_wind_diagnostic: dict[str, Any] | None = None
    compared = 0

    def caller_identity(value: dict[str, Any]) -> dict[str, Any]:
        return {
            family: {
                "total_calls": family_value.get("total_calls"),
                "overflow_calls": family_value.get("overflow_calls"),
                "seed_calls": family_value.get("seed_calls"),
                "last_seed": family_value.get("last_seed"),
                "event_count": family_value.get("event_count"),
                "overflow_events": family_value.get("overflow_events"),
                "sequence_hash": family_value.get("sequence_hash"),
                "callers": sorted(
                    (
                        {
                            "rva": caller.get("rva"),
                            "count": caller.get("count"),
                        }
                        for caller in family_value.get("callers", [])
                    ),
                    key=lambda caller: (
                        str(caller.get("rva")),
                        trace_int(caller.get("count"), 0),
                    ),
                ),
            }
            for family, family_value in sorted(value.items())
        }

    for key in sorted(reference_keys & candidate_keys):
        compared += 1
        reference = reference_frames[key]
        candidate = candidate_frames[key]
        if first_gameplay_rng_mismatch is None:
            differences = [
                {
                    "field": field,
                    "expected": reference.get(field),
                    "actual": candidate.get(field),
                }
                for field in (*AGGREGATE_FIELDS, *RNG_FIELDS)
                if not equivalent_exact(
                    reference.get(field), candidate.get(field)
                )
            ]
            if differences:
                first_gameplay_rng_mismatch = {
                    "replay_round": key[0],
                    "seq": key[1],
                    "differences": differences[:max_differences],
                }
        if first_stage_wind_diagnostic is None:
            differences = stage_wind_differences(
                reference, candidate, float_tolerance
            )
            if differences:
                first_stage_wind_diagnostic = {
                    "replay_round": key[0],
                    "seq": key[1],
                    "differences": differences[:max_differences],
                }
        if first_caller_mismatch is None \
                and key in reference_callers \
                and key in candidate_callers:
            expected_callers = caller_identity(reference_callers[key])
            actual_callers = caller_identity(candidate_callers[key])
            if expected_callers != actual_callers:
                first_caller_mismatch = {
                    "replay_round": key[0],
                    "seq": key[1],
                    "expected": expected_callers,
                    "actual": actual_callers,
                    "ring_in_five_call_fingerprint":
                        ring_in_five_call_fingerprint(
                            expected_callers.get("lfsr-25-word", {}),
                            actual_callers.get("lfsr-25-word", {})
                        ),
                }

    classification = (
        "normal-control-coverage-divergence"
        if not coverage_ok
        else "normal-control-gameplay-rng-divergence"
        if first_gameplay_rng_mismatch is not None
        else "normal-control-rng-caller-divergence"
        if first_caller_mismatch is not None
        else "normal-control-stage-wind-divergence"
        if first_stage_wind_diagnostic is not None
        else "normal-control-reproducible"
    )
    return {
        "classification": classification,
        "reference": {
            "path": str(reference_path.resolve()),
            "sha256": file_sha256(reference_path),
            "frames": len(reference_frames),
            "caller_windows": len(reference_callers),
        },
        "candidate": {
            "path": str(candidate_path.resolve()),
            "sha256": file_sha256(candidate_path),
            "frames": len(candidate_frames),
            "caller_windows": len(candidate_callers),
        },
        "compared_frames": compared,
        "coverage": coverage_mismatch,
        "first_gameplay_rng_mismatch": first_gameplay_rng_mismatch,
        "first_rng_caller_mismatch": first_caller_mismatch,
        "first_stage_wind_diagnostic": first_stage_wind_diagnostic,
    }


def presentation_differences(
    expected: dict[str, Any],
    actual: dict[str, Any],
) -> list[dict[str, Any]]:
    differences = [
        {
            "field": field,
            "expected": expected.get(field),
            "actual": actual.get(field),
        }
        for field in PRESENTATION_FIELDS
        if not equivalent_exact(expected.get(field), actual.get(field))
    ]
    for index in range(12):
        field = f"stage_wind_output_force_{index}"
        try:
            error = abs(float(expected[field]) - float(actual[field]))
        except (KeyError, TypeError, ValueError):
            error = math.inf
        if error > 1.0e-5:
            differences.append({
                "field": field,
                "expected": expected.get(field),
                "actual": actual.get(field),
                "absolute_error": error,
            })
    node_count = max(
        trace_int(expected.get("stage_wind_graph_count"), 0),
        trace_int(actual.get("stage_wind_graph_count"), 0),
    )
    for index in range(max(0, node_count)):
        for offset in range(0x40, 0x60, 4):
            field = f"stage_wind_node_{index}_word_{offset}"
            if field not in expected and field not in actual:
                continue
            if not equivalent_exact(expected.get(field), actual.get(field)):
                differences.append({
                    "field": field,
                    "expected": expected.get(field),
                    "actual": actual.get(field),
                })
    return differences


def peer_differences(
    host: dict[str, Any],
    sandbox: dict[str, Any],
    float_tolerance: float,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    aggregate, primitive = frame_differences(
        host, sandbox, float_tolerance
    )
    primitive.extend(stage_wind_differences(
        host, sandbox, float_tolerance
    ))
    aggregate.extend(presentation_differences(host, sandbox))
    for field in PEER_ONLY_FIELDS:
        if not equivalent_exact(host.get(field), sandbox.get(field)):
            aggregate.append({
                "field": field,
                "expected": host.get(field),
                "actual": sandbox.get(field),
            })
    return aggregate, primitive


def source_origins(report: dict[str, Any]) -> dict[int, int]:
    comparison = report.get("replay_oracle_comparison", {})
    validation = comparison.get("oracle_validation", {})
    coordinates = validation.get("round_coordinates", {})
    result: dict[int, int] = {}
    for key, value in coordinates.items():
        if isinstance(value, dict):
            result[trace_int(key)] = trace_int(
                value.get("control_source_origin")
            )
    if not result:
        raise ValueError("report has no replay source origins")
    return result


def round_seq_origins(report: dict[str, Any]) -> dict[int, int]:
    validation = report.get("replay_oracle_comparison", {}).get(
        "oracle_validation", {}
    )
    values = validation.get("round_seq_origins", {})
    result = {trace_int(key): trace_int(value)
              for key, value in values.items()}
    if not result:
        raise ValueError("report has no oracle round sequence origins")
    return result


def selected_round_epochs(report: dict[str, Any]) -> dict[int, int]:
    values = report.get("replay_oracle_comparison", {}).get(
        "selected_round_epochs", {}
    )
    result = {trace_int(key): trace_int(value, 0)
              for key, value in values.items()}
    if not result or any(value <= 0 for value in result.values()):
        raise ValueError("report has no selected round epochs")
    return result


def oracle_frame(
    rounds: dict[int, list[dict[str, Any]]],
    origins: dict[int, int],
    seq_origins: dict[int, int],
    replay_round: int,
    logical_frame: int,
) -> dict[str, Any] | None:
    values = rounds.get(replay_round, [])
    expected_seq = seq_origins.get(replay_round, -1) \
        + origins.get(replay_round, -1) + logical_frame
    matches = [
        frame for frame in values
        if trace_int(frame.get("seq")) == expected_seq
    ]
    if len(matches) > 1:
        raise ValueError(
            f"duplicate oracle sequence: round={replay_round} "
            f"seq={expected_seq}"
        )
    return matches[0] if matches else None


def transition(
    field: str,
    replay_round: int,
    logical_frame: int,
    rounds: dict[int, list[dict[str, Any]]],
    origins: dict[int, int],
    seq_origins: dict[int, int],
    clients: dict[str, dict[tuple[int, int], dict[str, Any]]],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name in ("normal", *clients.keys()):
        if name == "normal":
            previous = oracle_frame(
                rounds, origins, seq_origins,
                replay_round, logical_frame - 1
            )
            current = oracle_frame(
                rounds, origins, seq_origins,
                replay_round, logical_frame
            )
        else:
            previous = clients[name].get((replay_round, logical_frame - 1))
            current = clients[name].get((replay_round, logical_frame))
        result[name] = {
            "previous": field_value(previous, field),
            "current": field_value(current, field),
        }
    return result


def load_bound_case(
    corpus_report_path: Path,
    case_sha256: str | None,
) -> tuple[dict[str, Any], Path, int, str]:
    corpus = json.loads(corpus_report_path.read_text(encoding="utf-8"))
    cases = corpus.get("cases", {})
    if not isinstance(cases, dict) or not cases:
        raise ValueError("corpus report has no cases")
    if case_sha256:
        matches = [
            (digest, value) for digest, value in cases.items()
            if digest.lower().startswith(case_sha256.lower())
        ]
    else:
        matches = list(cases.items())
    if len(matches) != 1:
        raise ValueError(
            "select exactly one case with --case-sha256"
        )
    digest, case = matches[0]
    stages = {stage.get("name"): stage
              for stage in case.get("stages", [])}
    normal = stages.get("normal-replay-oracle", {})
    online = stages.get("two-client-clean", {})
    oracle_path = Path(str(normal.get("trace") or ""))
    online_report_path = Path(str(online.get("report") or ""))
    oracle_hash = str(case.get("oracle_trace_sha256") or "").lower()
    online_hash = str(case.get("online_report_sha256") or "").lower()
    if not oracle_path.is_file() or len(oracle_hash) != 64:
        raise ValueError("bound normal oracle identity mismatch")
    oracle_byte_end = sha256_prefix_end(oracle_path, oracle_hash)
    if not online_report_path.is_file() or len(online_hash) != 64 \
            or file_sha256(online_report_path) != online_hash:
        raise ValueError("bound online report identity mismatch")
    report = json.loads(
        online_report_path.read_text(encoding="utf-8")
    )
    comparison_oracle = Path(str(
        report.get("replay_oracle_comparison", {}).get(
            "oracle_path"
        ) or ""
    ))
    if comparison_oracle.resolve() != oracle_path.resolve():
        raise ValueError("online comparison used a different oracle")
    return report, oracle_path, oracle_byte_end, digest


def analyze_report(
    corpus_report_path: Path,
    case_sha256: str | None = None,
    float_tolerance: float = 1.0e-5,
    max_differences: int = 32,
    required_logical_prefix_frames: int = 0,
) -> dict[str, Any]:
    if required_logical_prefix_frames < 0:
        raise ValueError("required logical prefix cannot be negative")
    report, oracle_path, oracle_byte_end, selected_case = load_bound_case(
        corpus_report_path, case_sha256
    )
    if trace_int(report.get("oracle_schema_version"), -1) \
            != ORACLE_SCHEMA_VERSION:
        raise ValueError("online report oracle schema mismatch")
    rounds = latest_oracle_rounds(oracle_path, oracle_byte_end)
    origins = source_origins(report)
    seq_origins = round_seq_origins(report)
    epochs = selected_round_epochs(report)
    clients = {
        role: confirmed_frames(value, epochs)
        for role, value in report.get("clients", {}).items()
    }
    if set(clients) != {"host", "sandbox"}:
        raise ValueError("report must contain host and sandbox clients")
    rng_evidence = {
        role: rng_checkpoint_evidence(value, epochs)
        for role, value in report.get("clients", {}).items()
    }
    normal_rng_evidence = normal_rng_caller_windows(
        oracle_path, oracle_byte_end, rounds
    )
    comparison = report.get("replay_oracle_comparison", {})
    correction_start = trace_int(
        comparison.get("logical_frame_start"), -1
    )
    correction_end = trace_int(comparison.get("logical_frame_end"), -1)
    correction_range = (
        (correction_start, correction_end)
        if correction_start >= 0 and correction_end >= correction_start
        else None
    )

    common = set(clients["host"]) & set(clients["sandbox"])
    coverage: dict[str, Any] = {}
    common_keys: list[tuple[int, int]] = []
    coverage_incomplete = (
        required_logical_prefix_frames > 0
        and correction_range is None
    )
    for replay_round in sorted(epochs):
        logical_values = sorted(
            logical for round_index, logical in common
            if round_index == replay_round
        )
        logical_set = set(logical_values)
        contiguous = 0
        while contiguous in logical_set:
            contiguous += 1
        maximum = logical_values[-1] if logical_values else -1
        missing = [
            logical for logical in range(maximum + 1)
            if logical not in logical_set
        ]
        if contiguous == 0 or missing:
            coverage_incomplete = True
        prefix_missing = [
            logical for logical in range(required_logical_prefix_frames)
            if logical not in logical_set
        ]
        correction_missing = (
            [
                logical
                for logical in range(correction_start, correction_end + 1)
                if logical not in logical_set
            ]
            if required_logical_prefix_frames > 0
            and correction_range is not None
            else []
        )
        if prefix_missing or correction_missing:
            coverage_incomplete = True
        coverage[str(replay_round)] = {
            "selected_round_epoch": f"{epochs[replay_round]:#x}",
            "common_pair_confirmed_frames": len(logical_values),
            "minimum_logical_frame": (
                logical_values[0] if logical_values else None
            ),
            "maximum_logical_frame": (
                maximum if logical_values else None
            ),
            "contiguous_frames_from_zero": contiguous,
            "missing_before_maximum": missing[:64],
            "required_logical_prefix_frames":
                required_logical_prefix_frames,
            "missing_required_logical_prefix": prefix_missing[:64],
            "required_correction_window": (
                [correction_start, correction_end]
                if required_logical_prefix_frames > 0
                and correction_range is not None
                else None
            ),
            "missing_required_correction_window":
                correction_missing[:64],
        }
        selected_logical = set(range(contiguous))
        if required_logical_prefix_frames > 0:
            selected_logical.update(
                logical for logical in range(
                    required_logical_prefix_frames
                )
                if logical in logical_set
            )
            if correction_range is not None:
                selected_logical.update(
                    logical for logical in range(
                        correction_start, correction_end + 1
                    )
                    if logical in logical_set
                )
        common_keys.extend(
            (replay_round, logical)
            for logical in sorted(selected_logical)
        )
    root_motion_evidence = root_motion_frame_evidence(
        oracle_path,
        oracle_byte_end,
        rounds,
        clients,
        report.get("clients", {}),
        origins,
        seq_origins,
        common_keys,
    )
    first_any: dict[str, Any] | None = None
    first_primitive: dict[str, Any] | None = None
    first_rng: dict[str, Any] | None = None
    first_peer: dict[str, Any] | None = None
    first_presentation: dict[str, Any] | None = None
    first_stage_wind_diagnostic: dict[str, Any] | None = None
    compared = 0
    for replay_round, logical_frame in common_keys:
        expected = oracle_frame(
            rounds, origins, seq_origins, replay_round, logical_frame
        )
        if expected is None:
            raise ValueError(
                f"oracle sequence missing inside contiguous coverage: "
                f"round={replay_round} logical={logical_frame}"
            )
        compared += 1
        per_role: dict[str, Any] = {}
        for role, frames in clients.items():
            actual = frames[(replay_round, logical_frame)]
            aggregate, primitive = frame_differences(
                expected, actual,
                float_tolerance,
            )
            presentation = presentation_differences(expected, actual)
            stage_wind_diagnostic = stage_wind_differences(
                expected, actual, float_tolerance
            )
            expected_source = origins[replay_round] + logical_frame
            actual_source = trace_int(actual.get("source_index"))
            if actual_source != expected_source:
                primitive.insert(0, {
                    "category": "coordinate",
                    "field": "source_index",
                    "expected": expected_source,
                    "actual": actual.get("source_index"),
                })
            per_role[role] = {
                "aggregate": aggregate[:max_differences],
                "primitive": primitive[:max_differences],
                "presentation": presentation[:max_differences],
                "stage_wind_diagnostic":
                    stage_wind_diagnostic[:max_differences],
            }
        if first_any is None and any(
            value["aggregate"] or value["primitive"]
            for value in per_role.values()
        ):
            first_any = {
                "replay_round": replay_round,
                "logical_frame": logical_frame,
                "source_index": origins[replay_round] + logical_frame,
                "clients": per_role,
            }
        if first_primitive is None and any(
            value["primitive"] for value in per_role.values()
        ):
            first_primitive = {
                "replay_round": replay_round,
                "logical_frame": logical_frame,
                "source_index": origins[replay_round] + logical_frame,
                "clients": per_role,
            }
            fields = [
                difference["field"]
                for value in per_role.values()
                for difference in value["primitive"]
            ]
            if fields:
                first_primitive["transition"] = transition(
                    fields[0], replay_round, logical_frame,
                    rounds, origins, seq_origins, clients,
                )
        if first_presentation is None and any(
            value["presentation"] for value in per_role.values()
        ):
            first_presentation = {
                "replay_round": replay_round,
                "logical_frame": logical_frame,
                "source_index": origins[replay_round] + logical_frame,
                "clients": {
                    role: value["presentation"]
                    for role, value in per_role.items()
                },
            }
        if first_stage_wind_diagnostic is None and any(
            value["stage_wind_diagnostic"]
            for value in per_role.values()
        ):
            first_stage_wind_diagnostic = {
                "replay_round": replay_round,
                "logical_frame": logical_frame,
                "source_index": origins[replay_round] + logical_frame,
                "clients": {
                    role: value["stage_wind_diagnostic"]
                    for role, value in per_role.items()
                },
            }
        if first_rng is None:
            rng_differences = {
                role: [
                    difference
                    for difference in value["primitive"]
                    if difference["field"] in RNG_FIELDS
                ]
                for role, value in per_role.items()
            }
            if any(rng_differences.values()):
                expected_seq = (
                    seq_origins[replay_round]
                    + origins[replay_round]
                    + logical_frame
                )
                normal_callers = normal_rng_evidence.get(
                    (replay_round, expected_seq)
                )
                client_callers = {
                    role: next(
                        (
                            window for window in
                            rng_evidence[role]["caller_windows"]
                            if window.get("replay_round") == replay_round
                            and window.get("logical_frame") == logical_frame
                            and window.get("label")
                                in {"native-advance",
                                    "native-advance-0"}
                        ),
                        None,
                    )
                    for role in per_role
                }
                first_rng = {
                    "replay_round": replay_round,
                    "logical_frame": logical_frame,
                    "source_index":
                        origins[replay_round] + logical_frame,
                    "clients": rng_differences,
                    "transitions": {
                        field: transition(
                            field, replay_round, logical_frame,
                            rounds, origins, seq_origins, clients,
                        )
                        for field in RNG_FIELDS
                        if any(
                            difference["field"] == field
                            for differences in rng_differences.values()
                            for difference in differences
                        )
                    },
                    "caller_windows": {
                        "normal": normal_callers,
                        **client_callers,
                    },
                    "unmatched_callers": {
                        role: rng_caller_delta(
                            normal_callers, window
                        )
                        for role, window in client_callers.items()
                    },
                }
        peer_aggregate, peer_primitive = peer_differences(
            clients["host"][(replay_round, logical_frame)],
            clients["sandbox"][(replay_round, logical_frame)],
            float_tolerance,
        )
        if first_peer is None and (peer_aggregate or peer_primitive):
            first_peer = {
                "replay_round": replay_round,
                "logical_frame": logical_frame,
                "aggregate": peer_aggregate[:max_differences],
                "primitive": peer_primitive[:max_differences],
            }

    checkpoint_mismatches = [
        {"role": role, **evidence["first_mismatch"]}
        for role, evidence in sorted(rng_evidence.items())
        if evidence["first_mismatch"] is not None
    ]
    first_checkpoint_mismatch = min(
        checkpoint_mismatches,
        key=lambda value: (value["ts_qpc"], value["role"]),
        default=None,
    )
    return {
        "corpus_report": str(corpus_report_path.resolve()),
        "case_sha256": selected_case,
        "oracle": str(oracle_path.resolve()),
        "oracle_retained_byte_range": [0, oracle_byte_end],
        "oracle_schema_version": ORACLE_SCHEMA_VERSION,
        "float_tolerance": float_tolerance,
        "required_logical_prefix_frames":
            required_logical_prefix_frames,
        "required_correction_window": (
            [correction_start, correction_end]
            if required_logical_prefix_frames > 0
            and correction_range is not None
            else None
        ),
        "required_correction_window_metadata_present": (
            correction_range is not None
        ),
        "common_pair_confirmed_frames": len(common),
        "compared_contiguous_frames": compared,
        "coverage": coverage,
        "source_origins": {str(key): value for key, value in origins.items()},
        "first_any_mismatch": first_any,
        "first_primitive_mismatch": first_primitive,
        "first_rng_mismatch": first_rng,
        "first_peer_mismatch": first_peer,
        "first_presentation_mismatch": first_presentation,
        "first_stage_wind_diagnostic":
            first_stage_wind_diagnostic,
        "rng_checkpoint_evidence": rng_evidence,
        "first_rng_checkpoint_mismatch": first_checkpoint_mismatch,
        "root_motion_native_evidence": root_motion_evidence,
        "classification": (
            "rng-checkpoint-divergence"
            if first_checkpoint_mismatch is not None
            else "primitive-state-divergence"
            if first_primitive is not None
            # The aggregate hashes the raw IEEE-754 encodings of fields that
            # are all compared individually above.  A raw aggregate-only
            # mismatch is diagnostic cross-mode encoding evidence, not a
            # gameplay divergence, when every constituent passes its exact
            # or bounded-float comparison.
            else "peer-state-divergence"
            if first_peer is not None
            else "presentation-only-divergence"
            if first_presentation is not None
                or first_stage_wind_diagnostic is not None
            else "coverage-incomplete" if coverage_incomplete
            else "no-divergence"
        ),
    }


def print_summary(result: dict[str, Any]) -> None:
    print(
        f"classification={result['classification']} "
        f"compared_contiguous_frames="
        f"{result['compared_contiguous_frames']}"
    )
    for label in (
        "first_any_mismatch",
        "first_primitive_mismatch",
        "first_rng_mismatch",
        "first_peer_mismatch",
        "first_presentation_mismatch",
    ):
        value = result.get(label)
        if value is None:
            print(f"{label}=none")
            continue
        print(
            f"{label}=round:{value['replay_round']} "
            f"logical:{value['logical_frame']}"
        )
        if label == "first_peer_mismatch":
            differences = value["primitive"] or value["aggregate"]
        elif label in {
            "first_rng_mismatch",
            "first_presentation_mismatch",
        }:
            differences = [
                item
                for role in ("host", "sandbox")
                for item in value["clients"][role]
            ]
        else:
            differences = [
                item
                for role in ("host", "sandbox")
                for item in (
                    value["clients"][role]["primitive"]
                    or value["clients"][role]["aggregate"]
                )
            ]
        for item in differences[:8]:
            print(
                f"  {item['field']}: expected={item.get('expected')} "
                f"actual={item.get('actual')}"
            )
    checkpoint = result.get("first_rng_checkpoint_mismatch")
    if checkpoint is None:
        print("first_rng_checkpoint_mismatch=none")
    else:
        print(
            "first_rng_checkpoint_mismatch="
            f"role:{checkpoint['role']} "
            f"phase:{checkpoint['capture_phase']} "
            f"round:{checkpoint['replay_round']} "
            f"logical:{checkpoint['logical_frame']} "
            f"source:{checkpoint['source_index']}"
        )
        caller = checkpoint.get("nearest_caller_window")
        if caller is not None:
            print(
                "  rng_callers="
                f"label:{caller['label']} "
                f"total:{caller['total_calls']} "
                f"histogram:{caller['callers']}"
            )
    stage_wind = result.get("first_stage_wind_diagnostic")
    if stage_wind is None:
        print("first_stage_wind_diagnostic=none")
    else:
        print(
            "first_stage_wind_diagnostic="
            f"round:{stage_wind['replay_round']} "
            f"logical:{stage_wind['logical_frame']}"
        )
        differences = [
            item
            for role in ("host", "sandbox")
            for item in stage_wind["clients"][role]
        ]
        for item in differences[:8]:
            print(
                f"  {item['field']}: expected={item.get('expected')} "
                f"actual={item.get('actual')}"
            )
    root_motion = result.get("root_motion_native_evidence", {})
    root_mismatch = root_motion.get("first_mismatch")
    if root_mismatch is None:
        print(
            "first_root_motion_native_mismatch=none "
            f"compared={root_motion.get('compared_frames', 0)}"
        )
    else:
        print(
            "first_root_motion_native_mismatch="
            f"round:{root_mismatch['replay_round']} "
            f"logical:{root_mismatch['logical_frame']} "
            f"oracle_seq:{root_mismatch['normal_oracle_seq']}"
        )
        for role in ("host", "sandbox"):
            for item in root_mismatch["clients"][role][
                    "differences"][:4]:
                print(
                    f"  {role} {item['field']}: "
                    f"expected={item.get('expected')} "
                    f"actual={item.get('actual')}"
                )


def result_exit_code(result: dict[str, Any]) -> int:
    return 0 if result.get("classification") in {
        "no-divergence",
        "presentation-only-divergence",
    } else 1


def selftest() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        hitcue_base = bytearray(96)
        hitcue_offsets: dict[str, int] = {}
        hitcue_cursor = 0
        for hitcue_name, hitcue_size in HIT_CUE_SEMANTIC_LAYOUT:
            hitcue_offsets[hitcue_name] = hitcue_cursor
            hitcue_cursor += hitcue_size
        assert hitcue_cursor == 96
        hitcue_w_only = bytearray(hitcue_base)
        hitcue_w_only[
            hitcue_offsets["cached_local_w"]:
            hitcue_offsets["cached_local_w"] + 4
        ] = b"\xff\xff\xff\xff"
        assert hit_cue_semantic_byte_difference(
            hitcue_base.hex(), hitcue_w_only.hex()
        ) is None
        hitcue_x = bytearray(hitcue_base)
        hitcue_x[
            hitcue_offsets["cached_local_x"]:
            hitcue_offsets["cached_local_x"] + 4
        ] = b"\x00\x00\x80\x3f"
        assert hit_cue_semantic_byte_difference(
            hitcue_base.hex(), hitcue_x.hex()
        )["primitive"] == "cached_local_x"
        hitcue_ulp_a = bytearray(hitcue_base)
        hitcue_ulp_b = bytearray(hitcue_base)
        hitcue_ulp_a[
            hitcue_offsets["cached_world_z"]:
            hitcue_offsets["cached_world_z"] + 4
        ] = struct.pack("<I", 0xBD892CC4)
        hitcue_ulp_b[
            hitcue_offsets["cached_world_z"]:
            hitcue_offsets["cached_world_z"] + 4
        ] = struct.pack("<I", 0xBD892CC5)
        assert hit_cue_semantic_byte_difference(
            hitcue_ulp_a.hex(), hitcue_ulp_b.hex()
        ) is None
        inactive_slot = {"p1_hitcue2_active_cue": -1}
        active_slot = {"p1_hitcue2_active_cue": 7}
        assert inactive_hit_cue_suffix(
            inactive_slot, inactive_slot,
            "p1", "hitcue_pose_lane2_motion_clip_index",
        )
        assert not inactive_hit_cue_suffix(
            inactive_slot, active_slot,
            "p1", "hitcue_pose_lane2_motion_clip_index",
        )

        def fixture(
            name: str,
            *,
            gap: bool = False,
            source_mismatch: bool = False,
            oracle_gap: bool = False,
            canonical_peer_mismatch: bool = False,
            active_bank_peer_difference: bool = False,
            stage_wind_control_difference: bool = False,
            stage_wind_peer_difference: bool = False,
            frame_divergence: bool = True,
            rng_frame_mismatch: bool = False,
            rng_checkpoint_mismatch: bool = False,
            root_motion_mutation: bool = False,
            missing_schema: bool = False,
            mixed_schema: bool = False,
        ) -> Path:
            case_root = root / name
            case_root.mkdir()
            oracle = case_root / "oracle.jsonl"
            host = case_root / "host.jsonl"
            sandbox = case_root / "sandbox.jsonl"
            child_report = case_root / "online.json"
            seqs = [100, 101, 103, 104] if oracle_gap \
                else [100, 101, 102, 103]
            frames = [
                {
                    "event": "oracle_frame",
                    "seq": seq,
                    "round": 0,
                    "beta_oracle_readable": True,
                    "oracle_schema_version": ORACLE_SCHEMA_VERSION,
                    "oracle_gameplay_hash_v12": hex(1000 + seq),
                    "oracle_presentation_hash": "0x2000",
                    "stage_wind_presentation_hash": "0x2002",
                    "rng_lcg_state": "0x1",
                    "rng_lfsr_hash": "0x2",
                    "rng_lfsr_index": 3,
                    "rng_gameplay_crt_present": True,
                    "rng_gameplay_crt_state": "0x3",
                    "rng_gameplay_crt_seed": "0x4",
                    "rng_gameplay_crt_draw_ordinal": seq,
                    "secondary_event_previous_variant_readable": True,
                    "p1_secondary_event_previous_variant": "0x4",
                    "p2_secondary_event_previous_variant": "0x8",
                    "round_result_count": 0,
                    "round_result_limit": 1,
                    "round_current_index": 1,
                    "round_applied_index": 0,
                    "round_result_flow": 0,
                    "breakable_gameplay_digest": "0x4",
                    "chara_animation_gameplay_digest": "0x41",
                    "breakable_presentation_digest": "0x0",
                    "stage_wind_gameplay_hash": "0x4f00",
                    "stage_wind_canonical_hash": "0x5000",
                    "stage_wind_combined_rng_hash": "0x5001",
                    "stage_wind_emitter_hash": "0x5002",
                    "stage_wind_root_scheduler_hash": "0x5003",
                    "stage_wind_root_outputs_hash": "0x5004",
                    "stage_wind_graph_nodes_hash": "0x5005",
                    "stage_wind_graph_hash": "0x5006",
                    "stage_wind_emitter_count": 1,
                    "stage_wind_graph_count": 1,
                    "stage_wind_output_active": 0,
                    "stage_wind_active_bank": 0,
                    "stage_wind_pending_count": 0,
                    "stage_wind_schedule_state": -1,
                    "stage_wind_effect_pair_scheduled": 0,
                    "fp_mxcsr_control": "0x1F80",
                    "fp_x87_control_readable": True,
                    "fp_x87_control": "0x27F",
                    "stage_wind_node_0_vtable_rva": "0x1234",
                    "stage_wind_node_0_hash": "0x5007",
                    **{
                        f"stage_wind_output_force_{index}": 0.0
                        for index in range(12)
                    },
                    "p1_input": 0,
                    "p2_input": 0,
                    **{
                        f"{player}_{suffix}": (
                            1.0 if suffix in FIGHTER_FLOAT_SUFFIXES else 0
                        )
                        for player in ("p1", "p2")
                        for suffix in (
                            *FIGHTER_EXACT_SUFFIXES,
                            *FIGHTER_FLOAT_SUFFIXES,
                        )
                    },
                }
                for seq in seqs
            ]
            if missing_schema:
                frames[0].pop("oracle_schema_version")
            if mixed_schema:
                frames[0]["oracle_schema_version"] = 1
            oracle_events = [
                {"event": "generate_start", "mode": "normal"},
                *frames,
                {
                    "event": "generate_complete",
                    "mode": "normal",
                    "oracle_ok": True,
                    "integrity_ok": True,
                    "frames": len(frames),
                },
            ]
            oracle.write_text(
                "".join(json.dumps(item) + "\n"
                        for item in oracle_events),
                encoding="utf-8",
            )

            def online_events(pid: int, canonical_delta: int) \
                    -> list[dict[str, Any]]:
                marker = 10000 + pid
                values: list[dict[str, Any]] = [{
                    "event": "rollback_production_status",
                    "pid": pid,
                    "process_start_marker": marker,
                    "request_id": "request-1",
                    "ts_qpc": 100,
                }]
                if rng_checkpoint_mismatch and pid == 10:
                    identity = {
                        "pid": pid,
                        "process_start_marker": marker,
                        "request_id": "request-1",
                    }
                    values.extend([
                        {
                            "event": RNG_LFSR_CALLERS_EVENT,
                            **identity,
                            "ts_qpc": 104,
                            "phase": "rollback-frame-zero",
                            "label": "frozen-pre-update",
                            "total_calls": 3,
                            "overflow_calls": 0,
                            "caller_count": 1,
                            "caller0_rva": "0x1234",
                            "caller0_count": 3,
                        },
                        {
                            "event": RNG_CHECKPOINT_EVENT,
                            **identity,
                            "ts_qpc": 105,
                            "capture_phase": "gekko-save-minus-one",
                            "lifecycle_epoch": "0x1",
                            "round_epoch": "0x10",
                            "round_generation": 1,
                            "replay_round": 0,
                            "logical_frame": INVALID_FRAME_COORDINATE,
                            "source_index": INVALID_FRAME_COORDINATE,
                            "rng_lcg_state": "0x1",
                            "rng_lfsr_hash": "0x2",
                            "rng_lfsr_index": 8,
                            "expected_rng_lcg_state": "0x1",
                            "expected_rng_lfsr_hash": "0x2",
                            "expected_rng_lfsr_index": 5,
                            "expected_match": False,
                            "canonical_hash": "0x1000",
                        },
                    ])
                logical_values = [1] if gap else [0, 1]
                for logical_frame in logical_values:
                    expected = frames[2 + logical_frame].copy()
                    expected.update({
                        "event": "rollback_replay_trace_frame",
                        "pid": pid,
                        "process_start_marker": marker,
                        "ts_qpc": 110 + logical_frame,
                        "replay_round": 0,
                        "round_epoch": "0x10",
                        "logical_frame": logical_frame,
                        "source_index": (
                            999 if source_mismatch
                            and logical_frame == 0
                            else 2 + logical_frame
                        ),
                        "pair_confirmed": True,
                        "speculative": False,
                        "canonical_hash": hex(0x500 + canonical_delta),
                        "p1_gekko_input": 0,
                        "p2_gekko_input": 0,
                    })
                    if frame_divergence and logical_frame == 1:
                        expected["oracle_gameplay_hash_v12"] = "0x999"
                        expected["p1_pos_x"] = 2.0
                    if rng_frame_mismatch and logical_frame == 0:
                        expected["rng_lfsr_index"] = 8
                    if root_motion_mutation and pid == 10 \
                            and logical_frame == 0:
                        expected["p1_hitcue0_node_frame"] = 2.0
                    if active_bank_peer_difference and pid == 11 \
                            and logical_frame == 0:
                        # The raw selector can differ while selecting an
                        # equivalent empty/canonical callback bank. It is
                        # intentionally absent from the stage-wind canonical
                        # hash and is diagnostic rather than simulation state.
                        expected["stage_wind_active_bank"] = 1
                    if stage_wind_control_difference \
                            and logical_frame == 0:
                        expected["stage_wind_canonical_hash"] = "0x6000"
                        expected["stage_wind_node_0_hash"] = "0x6001"
                    if stage_wind_peer_difference and pid == 11 \
                            and logical_frame == 0:
                        expected["stage_wind_canonical_hash"] = "0x7000"
                    values.append(expected)
                stale = values[-1].copy()
                stale["round_epoch"] = "0x99"
                stale["logical_frame"] = 0
                stale["canonical_hash"] = "0xDEAD"
                values.append(stale)
                pre_request = values[1].copy()
                pre_request["ts_qpc"] = 50
                pre_request["logical_frame"] = 0
                pre_request["canonical_hash"] = "0xBAD"
                pre_request["p1_pos_x"] = 999.0
                values.append(pre_request)
                return values

            host_events = online_events(10, 0)
            sandbox_events = online_events(
                11, 1 if canonical_peer_mismatch else 0
            )
            host.write_text(
                "".join(json.dumps(item) + "\n"
                        for item in host_events),
                encoding="utf-8",
            )
            sandbox.write_text(
                "".join(json.dumps(item) + "\n"
                        for item in sandbox_events),
                encoding="utf-8",
            )

            def artifact(path: Path) -> list[dict[str, Any]]:
                data = path.read_bytes()
                return [{
                    "path": str(path),
                    "byte_range": [0, len(data)],
                    "sha256": hashlib.sha256(data).hexdigest(),
                }]

            child_report.write_text(json.dumps({
                "oracle_schema_version": ORACLE_SCHEMA_VERSION,
                "replay_oracle_comparison": {
                    "oracle_path": str(oracle),
                    "logical_frame_start": 0,
                    "logical_frame_end": 1,
                    "selected_round_epochs": {"0": "0x10"},
                    "oracle_validation": {
                        "round_seq_origins": {"0": 100},
                        "round_coordinates": {
                            "0": {"control_source_origin": 2}
                        },
                    },
                },
                "clients": {
                    "host": {
                        "pid": 10,
                        "status": {
                            "request_id": "request-1",
                            "process_start_marker": 10010,
                        },
                        "trace_artifacts": artifact(host),
                    },
                    "sandbox": {
                        "pid": 11,
                        "status": {
                            "request_id": "request-1",
                            "process_start_marker": 10011,
                        },
                        "trace_artifacts": artifact(sandbox),
                    },
                },
            }), encoding="utf-8")
            digest = "a" * 64
            corpus_report = case_root / "corpus.json"
            corpus_report.write_text(json.dumps({
                "cases": {
                    digest: {
                        "oracle_trace_sha256": file_sha256(oracle),
                        "online_report_sha256": file_sha256(child_report),
                        "stages": [
                            {
                                "name": "normal-replay-oracle",
                                "trace": str(oracle),
                            },
                            {
                                "name": "two-client-clean",
                                "report": str(child_report),
                            },
                        ],
                    }
                }
            }), encoding="utf-8")
            with oracle.open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({
                    "event": "post_hash_append_only_tail",
                }) + "\n")
            return corpus_report

        def root_call(
            player: int,
            raw_x: float,
            raw_z: float,
        ) -> dict[str, Any]:
            return {
                "player": player,
                "callsite_phase": f"common-p{player}",
                "call_ordinal": 1,
                "after_raw_x": raw_x,
                "after_raw_z": raw_z,
                "after_smoothed_x": raw_x / 2.0,
                "after_smoothed_z": raw_z / 2.0,
                "after_carry_x": raw_x / 2.0,
                "after_carry_z": raw_z / 2.0,
                "after_carry_mode": 1,
            }

        producing_transaction = {
            "transaction_id": 7,
            "completion_qpc": 100,
            "calls": [
                root_call(1, 1.0, 2.0),
                root_call(2, 3.0, 4.0),
            ],
        }
        later_speculative_transaction = {
            "transaction_id": 8,
            "completion_qpc": 110,
            "calls": [
                root_call(1, 10.0, 20.0),
                root_call(2, 30.0, 40.0),
            ],
        }
        confirmed_frame = {
            "ts_qpc": 120,
            "p1_raw_root_motion_x": 1.0,
            "p1_raw_root_motion_z": 2.0,
            "p1_smoothed_root_motion_x": 0.5,
            "p1_smoothed_root_motion_z": 1.0,
            "p1_root_motion_carry_x": 0.5,
            "p1_root_motion_carry_z": 1.0,
            "p1_root_motion_carry_mode": 1,
            "p2_raw_root_motion_x": 3.0,
            "p2_raw_root_motion_z": 4.0,
            "p2_smoothed_root_motion_x": 1.5,
            "p2_smoothed_root_motion_z": 2.0,
            "p2_root_motion_carry_x": 1.5,
            "p2_root_motion_carry_z": 2.0,
            "p2_root_motion_carry_mode": 1,
        }
        transaction_index = root_motion_transaction_output_index([
            producing_transaction,
            later_speculative_transaction,
        ])
        assert root_motion_transaction_for_frame(
            transaction_index, confirmed_frame
        )["transaction_id"] == 7
        corrupted_frame = dict(confirmed_frame)
        corrupted_frame["p1_raw_root_motion_x"] = 1.25
        assert root_motion_transaction_for_frame(
            transaction_index, corrupted_frame
        ) is None
        assert root_motion_transaction_differences(None, None) == []

        def producer_checkpoint(
            transaction_id: int,
            player: int,
            stage: str,
            phase: str,
            qpc: int,
            matrix_x: float,
        ) -> dict[str, Any]:
            event: dict[str, Any] = {
                "producer_transaction_id": transaction_id,
                "player": player,
                "stage": stage,
                "phase": phase,
                "ts_qpc": qpc,
            }
            for field in POSE_PRODUCER_FLOAT_FIELDS:
                event[field] = 0.0
            for field in POSE_PRODUCER_EXACT_FIELDS:
                event[field] = 0
            event["bone1_current_m30"] = matrix_x
            return event

        producer_events = [
            producer_checkpoint(
                10, 1, "TickCharaMainSimulation", "enter",
                80, 0.0,
            ),
            producer_checkpoint(
                10, 1, "FinalizeTickPoseAndState", "enter",
                81, 0.0,
            ),
            producer_checkpoint(
                10, 1, "FinalizeTickPoseAndState", "exit",
                82, 1.0,
            ),
            producer_checkpoint(
                10, 1, "EvaluateBonePose", "enter",
                83, 1.0,
            ),
            producer_checkpoint(
                10, 1, "EvaluateBonePose", "exit",
                84, 2.0,
            ),
            producer_checkpoint(
                10, 1, "TickCharaMainSimulation", "exit",
                85, 2.0,
            ),
            # A later speculative producer with a different output must not
            # be attributed to the earlier confirmed collision call.
            producer_checkpoint(
                11, 1, "TickCharaMainSimulation", "enter",
                101, 0.0,
            ),
            producer_checkpoint(
                11, 1, "TickCharaMainSimulation", "exit",
                105, 9.0,
            ),
        ]
        for event in producer_events:
            event["event"] = POSE_PRODUCER_CHECKPOINT_EVENT
            event["simulation_ownership"] = "stock"
        producer_transactions = pose_producer_transactions(
            producer_events, ownership="stock"
        )
        producer_index = pose_producer_output_index(
            producer_transactions
        )
        producer_root_call = producer_checkpoint(
            0, 1, "", "", 100, 2.0
        )
        for row in range(4):
            for column in range(4):
                producer_root_call[f"before_current_m{row}{column}"] = \
                    producer_root_call[
                        f"bone1_current_m{row}{column}"
                    ]
        selected_producer = pose_producer_for_root_call(
            producer_index, producer_root_call
        )
        assert selected_producer is not None
        assert selected_producer["transaction_id"] == 10
        causal_producer = causal_pose_producer_for_root_call(
            producer_transactions, producer_root_call
        )
        assert causal_producer is not None
        assert causal_producer["transaction_id"] == 10
        assert pose_producer_transaction_differences(
            selected_producer, selected_producer
        ) == []
        reordered_producer = dict(selected_producer)
        reordered_producer["checkpoints"] = list(reversed(
            selected_producer["checkpoints"]
        ))
        assert pose_producer_transaction_differences(
            selected_producer, reordered_producer
        )[0]["category"] == "pose-producer-order"
        original_detailed_pose_checkpoint = next(
            event for event in selected_producer["checkpoints"]
            if event["stage"] == "FinalizeTickPoseAndState"
            and event["phase"] == "exit"
        )
        original_detailed_pose_checkpoint[
            "pose_matrix_bank_current_bone_07_hash"
        ] = 0x111
        original_detailed_pose_checkpoint[
            "pose_matrix_bank_current_bone_11_m30_bits"
        ] = 0x3F800000
        mutated_pose_bank = dict(selected_producer)
        mutated_pose_bank["checkpoints"] = [
            dict(event)
            for event in selected_producer["checkpoints"]
        ]
        mutated_detailed_pose_checkpoint = next(
            event for event in mutated_pose_bank["checkpoints"]
            if event["stage"] == "FinalizeTickPoseAndState"
            and event["phase"] == "exit"
        )
        mutated_detailed_pose_checkpoint[
            "pose_matrix_bank_current_bone_07_hash"
        ] = 0x222
        pose_bank_diff = pose_producer_transaction_differences(
            selected_producer, mutated_pose_bank
        )
        assert pose_bank_diff[0]["field"] == \
            "pose_matrix_bank_current_bone_07_hash"
        mutated_pose_bank_bits = dict(selected_producer)
        mutated_pose_bank_bits["checkpoints"] = [
            dict(event)
            for event in selected_producer["checkpoints"]
        ]
        mutated_pose_bits_checkpoint = next(
            event for event in mutated_pose_bank_bits["checkpoints"]
            if event["stage"] == "FinalizeTickPoseAndState"
            and event["phase"] == "exit"
        )
        mutated_pose_bits_checkpoint[
            "pose_matrix_bank_current_bone_11_m30_bits"
        ] = 0x3F800001
        pose_bank_bits_diff = pose_producer_transaction_differences(
            selected_producer, mutated_pose_bank_bits
        )
        assert pose_bank_bits_diff[0]["field"] == \
            "pose_matrix_bank_current_bone_11_m30_bits"

        def pre_main_checkpoint(
            transaction_id: int,
            stage: str,
            phase: str,
            qpc: int,
        ) -> dict[str, Any]:
            event: dict[str, Any] = {
                "event": PRE_MAIN_MOTION_CHECKPOINT_EVENT,
                "causal_p2_transaction_id": transaction_id,
                "simulation_ownership": "stock",
                "stage": stage,
                "phase": phase,
                "ts_qpc": qpc,
                "checkpoint_sequence": qpc,
            }
            for field in PRE_MAIN_MOTION_EXACT_FIELDS:
                event[field] = 0
            event["rng_lfsr_caller_00_rva"] = "0x33371F"
            event["rng_lfsr_caller_00_count"] = 1
            event["rng_xorshift96_caller_00_rva"] = "0x123456"
            event["rng_xorshift96_caller_00_count"] = 2
            return event

        pre_main_events = [
            pre_main_checkpoint(
                10, "MoveSystemPumpVMSlots", "enter", 60
            ),
            pre_main_checkpoint(
                10, "MoveSystemPumpVMSlots", "exit", 61
            ),
            pre_main_checkpoint(
                10, "PreTickStateSnapshotAndRoundDecision", "enter", 62
            ),
            pre_main_checkpoint(
                10, "PreTickStateSnapshotAndRoundDecision", "exit", 63
            ),
        ]
        pre_main_transaction = pre_main_motion_transactions(
            pre_main_events, ownership="stock"
        )[10]
        assert pre_main_motion_transaction_differences(
            pre_main_transaction, pre_main_transaction
        ) == []

        mutated_pre_main_events = [dict(event) for event in pre_main_events]
        mutated_pre_main_events[1][
            "p2_matrix_bank_current_bone_11_m30_bits"
        ] = "0x4029C540"
        mutated_pre_main = pre_main_motion_transactions(
            mutated_pre_main_events, ownership="stock"
        )[10]
        pre_main_diff = pre_main_motion_transaction_differences(
            pre_main_transaction, mutated_pre_main
        )
        assert pre_main_diff[0]["checkpoint"] == [
            "MoveSystemPumpVMSlots", "exit"
        ]
        assert pre_main_diff[0]["field"] == \
            "p2_matrix_bank_current_bone_11_m30_bits"

        mutated_rng_events = [dict(event) for event in pre_main_events]
        mutated_rng_events[2]["rng_xorshift96_caller_00_count"] = 3
        mutated_rng = pre_main_motion_transactions(
            mutated_rng_events, ownership="stock"
        )[10]
        rng_diff = pre_main_motion_transaction_differences(
            pre_main_transaction, mutated_rng
        )
        assert rng_diff[0]["checkpoint"] == [
            "PreTickStateSnapshotAndRoundDecision", "enter"
        ]
        assert rng_diff[0]["field"] == \
            "rng_xorshift96_caller_00_count"

        reordered_pre_main = pre_main_motion_transactions(
            [pre_main_events[1], pre_main_events[0],
             *pre_main_events[2:]],
            ownership="stock",
        )[10]
        # Timestamp sorting makes input order irrelevant; a genuine native
        # order defect is represented by changed stage/phase coordinates.
        reordered_pre_main["sequence"] = list(reversed(
            reordered_pre_main["sequence"]
        ))
        assert pre_main_motion_transaction_differences(
            pre_main_transaction, reordered_pre_main
        )[0]["category"] == "pre-main-motion-order"
        omitted_pre_main = pre_main_motion_transactions(
            pre_main_events[:-1], ownership="stock"
        )[10]
        assert pre_main_motion_transaction_differences(
            pre_main_transaction, omitted_pre_main
        )[0]["category"] == "pre-main-motion-order"

        def collision_checkpoint(
            transaction_id: int,
            stage: str,
            phase: str,
            qpc: int,
        ) -> dict[str, Any]:
            event: dict[str, Any] = {
                "event": COLLISION_CHECKPOINT_EVENT,
                "transaction_id": transaction_id,
                "transaction_active": True,
                "stage": stage,
                "phase": phase,
                "ts_qpc": qpc,
            }
            for field in COLLISION_CHECKPOINT_EXACT_FIELDS:
                event[field] = 0
            event["transaction_active"] = True
            for field in COLLISION_CHECKPOINT_FLOAT_FIELDS:
                event[field] = 0.0
            return event

        collision_events = [
            collision_checkpoint(
                20, stage, phase, 200 + index
            )
            for index, (stage, phase)
            in enumerate(COLLISION_CHECKPOINT_SEQUENCE)
        ]
        solve_enter_event = next(
            event for event in collision_events
            if event["stage"] == "SolvePhysBodyCollision"
            and event["phase"] == "enter"
        )
        solve_enter_event[
            "p1_matrix_bank_current_bone_07_hash"
        ] = 0x111
        solve_enter_event[
            "p1_khit_body_node_003_semantic_hash"
        ] = 0x333
        solve_enter_event[
            "p1_khit_body_node_003_live_local_center_x_bits"
        ] = 0x3F800000
        collision_transaction = collision_transactions(
            collision_events
        )[20]
        assert collision_transaction["complete"]
        assert collision_transaction_differences(
            collision_transaction, collision_transaction
        ) == []
        mutated_collision = dict(collision_transaction)
        mutated_collision["checkpoints"] = [
            dict(event)
            for event in collision_transaction["checkpoints"]
        ]
        mutated_collision["checkpoints"][2][
            "p1_bone1_current_m30"
        ] = 1.0
        collision_diff = collision_transaction_differences(
            collision_transaction, mutated_collision
        )
        assert collision_diff[0]["checkpoint"] == [
            "SolvePhysBodyCollision", "exit"
        ]
        assert collision_diff[0]["field"] == \
            "p1_bone1_current_m30"

        mutated_collision_bank = dict(collision_transaction)
        mutated_collision_bank["checkpoints"] = [
            dict(event)
            for event in collision_transaction["checkpoints"]
        ]
        mutated_collision_bank["checkpoints"][1][
            "p1_matrix_bank_current_bone_07_hash"
        ] = 0x222
        collision_bank_diff = collision_transaction_differences(
            collision_transaction, mutated_collision_bank
        )
        assert collision_bank_diff[0]["checkpoint"] == [
            "SolvePhysBodyCollision", "enter"
        ]
        assert collision_bank_diff[0]["field"] == \
            "p1_matrix_bank_current_bone_07_hash"

        mutated_collision_khit = dict(collision_transaction)
        mutated_collision_khit["checkpoints"] = [
            dict(event)
            for event in collision_transaction["checkpoints"]
        ]
        mutated_collision_khit["checkpoints"][1][
            "p1_khit_body_node_003_semantic_hash"
        ] = 0x444
        collision_khit_diff = collision_transaction_differences(
            collision_transaction, mutated_collision_khit
        )
        assert collision_khit_diff[0]["field"] == \
            "p1_khit_body_node_003_semantic_hash"

        mutated_collision_khit_field = dict(collision_transaction)
        mutated_collision_khit_field["checkpoints"] = [
            dict(event)
            for event in collision_transaction["checkpoints"]
        ]
        mutated_collision_khit_field["checkpoints"][1][
            "p1_khit_body_node_003_live_local_center_x_bits"
        ] = 0x3F800001
        collision_khit_field_diff = collision_transaction_differences(
            collision_transaction, mutated_collision_khit_field
        )
        assert collision_khit_field_diff[0]["field"] == \
            "p1_khit_body_node_003_live_local_center_x_bits"

        mutated_collision_fp = dict(collision_transaction)
        mutated_collision_fp["checkpoints"] = [
            dict(event)
            for event in collision_transaction["checkpoints"]
        ]
        mutated_collision_fp["checkpoints"][1][
            "fp_mxcsr_control"
        ] = 0x1F80
        collision_fp_diff = collision_transaction_differences(
            collision_transaction, mutated_collision_fp
        )
        assert collision_fp_diff[0]["field"] == "fp_mxcsr_control"

        primary = analyze_report(
            fixture("primary", canonical_peer_mismatch=True)
        )
        assert primary["classification"] == "primitive-state-divergence"
        assert primary["first_any_mismatch"]["logical_frame"] == 1
        assert primary["first_primitive_mismatch"]["logical_frame"] == 1
        assert primary["first_primitive_mismatch"]["clients"]["host"][
            "primitive"
        ][0]["field"] == "p1_pos_x"
        assert primary["first_peer_mismatch"]["logical_frame"] == 0
        assert primary["first_peer_mismatch"]["aggregate"][0][
            "field"
        ] == "canonical_hash"

        bad_source = analyze_report(
            fixture("source", source_mismatch=True)
        )
        assert bad_source["first_primitive_mismatch"][
            "logical_frame"
        ] == 0
        assert bad_source["first_primitive_mismatch"]["clients"]["host"][
            "primitive"
        ][0]["field"] == "source_index"

        missing_zero = analyze_report(fixture("coverage", gap=True))
        assert missing_zero["classification"] == "coverage-incomplete"
        assert missing_zero["compared_contiguous_frames"] == 0

        clean = analyze_report(
            fixture("clean", frame_divergence=False),
            required_logical_prefix_frames=2,
        )
        assert clean["classification"] == "no-divergence"
        assert clean["required_logical_prefix_frames"] == 2
        assert clean["required_correction_window"] == [0, 1]
        assert clean["coverage"]["0"][
            "missing_required_logical_prefix"
        ] == []
        assert clean["coverage"]["0"][
            "missing_required_correction_window"
        ] == []
        root_motion = analyze_report(
            fixture(
                "root-motion",
                root_motion_mutation=True,
                frame_divergence=False,
            ),
            required_logical_prefix_frames=2,
        )
        assert root_motion["classification"] \
            == "primitive-state-divergence"
        assert root_motion["first_primitive_mismatch"]["clients"]["host"][
            "primitive"
        ][0]["field"] == "p1_hitcue0_node_frame"
        active_bank_only = analyze_report(
            fixture(
                "active-bank-only",
                active_bank_peer_difference=True,
                frame_divergence=False,
            ),
            required_logical_prefix_frames=2,
        )
        assert active_bank_only["classification"] == "no-divergence"
        assert active_bank_only["first_peer_mismatch"] is None
        wind_diagnostic = analyze_report(
            fixture(
                "wind-diagnostic",
                stage_wind_control_difference=True,
                frame_divergence=False,
            ),
            required_logical_prefix_frames=2,
        )
        assert wind_diagnostic["classification"] \
            == "presentation-only-divergence"
        assert result_exit_code(wind_diagnostic) == 0
        assert wind_diagnostic["first_stage_wind_diagnostic"][
            "logical_frame"
        ] == 0
        wind_peer = analyze_report(
            fixture(
                "wind-peer",
                stage_wind_peer_difference=True,
                frame_divergence=False,
            ),
            required_logical_prefix_frames=2,
        )
        assert wind_peer["classification"] == "peer-state-divergence"
        assert wind_peer["first_peer_mismatch"]["primitive"][0][
            "field"
        ] == "stage_wind_canonical_hash"
        for name, keyword in (
            ("missing-schema", {"missing_schema": True}),
            ("mixed-schema", {"mixed_schema": True}),
        ):
            try:
                analyze_report(fixture(
                    name, frame_divergence=False, **keyword
                ))
            except ValueError as exc:
                assert "oracle schema mismatch" in str(exc)
            else:
                raise AssertionError(
                    f"{name} unexpectedly accepted incompatible oracle"
                )
        assert result_exit_code(clean) == 0
        assert result_exit_code(primary) == 1
        presentation_only = dict(clean)
        presentation_only["classification"] = \
            "presentation-only-divergence"
        assert result_exit_code(presentation_only) == 0

        prefix_gap = analyze_report(
            fixture(
                "prefix-gap", gap=True, frame_divergence=False
            ),
            required_logical_prefix_frames=2,
        )
        assert prefix_gap["classification"] == "coverage-incomplete"
        assert prefix_gap["coverage"]["0"][
            "missing_required_logical_prefix"
        ] == [0]

        rng_frame = analyze_report(fixture(
            "rng-frame",
            frame_divergence=False,
            rng_frame_mismatch=True,
        ))
        assert rng_frame["classification"] == "primitive-state-divergence"
        assert rng_frame["first_rng_mismatch"]["logical_frame"] == 0
        assert rng_frame["first_rng_mismatch"]["clients"]["host"][0][
            "field"
        ] == "rng_lfsr_index"
        assert rng_frame["first_rng_mismatch"]["transitions"][
            "rng_lfsr_index"
        ]["normal"]["current"] == 3
        assert rng_frame["first_rng_mismatch"]["transitions"][
            "rng_lfsr_index"
        ]["host"]["current"] == 8

        rng_checkpoint = analyze_report(fixture(
            "rng-checkpoint",
            frame_divergence=False,
            rng_checkpoint_mismatch=True,
        ))
        assert rng_checkpoint["classification"] == \
            "rng-checkpoint-divergence"
        checkpoint = rng_checkpoint["first_rng_checkpoint_mismatch"]
        assert checkpoint["role"] == "host"
        assert checkpoint["capture_phase"] == "gekko-save-minus-one"
        assert checkpoint["logical_frame"] is None
        assert checkpoint["actual"]["rng_lfsr_index"] == 8
        assert checkpoint["expected"]["rng_lfsr_index"] == 5
        assert checkpoint["nearest_caller_window"]["callers"][0] == {
            "rva": "0x1234",
            "count": 3,
            "function": None,
        }

        def write_normal_control(
            path: Path,
            *,
            wind_hash: str,
            gameplay_delta: int = 0,
            caller_delta: int = 0,
        ) -> None:
            events: list[dict[str, Any]] = [{
                "event": "generate_start",
                "mode": "normal",
            }]
            for seq in range(2):
                events.extend([
                    {
                        "event": "oracle_frame",
                        "ts_qpc": 100 + seq * 100,
                        "seq": seq,
                        "round": 0,
                        "oracle_schema_version": ORACLE_SCHEMA_VERSION,
                        "oracle_gameplay_hash_v12":
                            hex(0x1000 + seq + gameplay_delta),
                        "stage_wind_gameplay_hash": "0x1800",
                        "oracle_presentation_hash": "0x2000",
                        "breakable_presentation_digest": "0x2001",
                        "p1_secondary_event_previous_variant": "0x4",
                        "p2_secondary_event_previous_variant": "0x8",
                        "stage_wind_presentation_hash": "0x2002",
                        "rng_lcg_state": "0x1",
                        "rng_lfsr_hash": "0x2",
                        "rng_lfsr_index": 3,
                        "rng_gameplay_crt_present": True,
                        "rng_gameplay_crt_state": "0x3",
                        "rng_gameplay_crt_seed": "0x4",
                        "rng_gameplay_crt_draw_ordinal": seq,
                        "stage_wind_canonical_hash": wind_hash,
                        "stage_wind_combined_rng_hash": "0x3001",
                        "stage_wind_emitter_hash": wind_hash,
                        "stage_wind_root_scheduler_hash": "0x3003",
                        "stage_wind_root_outputs_hash": wind_hash,
                        "stage_wind_graph_nodes_hash": wind_hash,
                        "stage_wind_graph_hash": wind_hash,
                        "stage_wind_emitter_count": 1,
                        "stage_wind_graph_count": 0,
                        "stage_wind_output_active": 1,
                        "stage_wind_pending_count": 0,
                        "stage_wind_schedule_state": -1,
                        "stage_wind_effect_pair_scheduled": 0,
                        **{
                            f"stage_wind_output_force_{index}": (
                                float(seq) if index == 0 else 0.0
                            )
                            for index in range(12)
                        },
                    },
                    {
                        "event": RNG_LFSR_CALLERS_EVENT,
                        "ts_qpc": 110 + seq * 100,
                        "phase": "generation",
                        "label": "oracle-frame",
                        "rng_family": "lfsr-25-word",
                        "total_calls": 1 + caller_delta,
                        "overflow_calls": 0,
                        "caller_count": 1,
                        "caller0_rva": "0x1234",
                        "caller0_count": 1 + caller_delta,
                    },
                    {
                        "event": RNG_XORSHIFT96_CALLERS_EVENT,
                        "ts_qpc": 111 + seq * 100,
                        "phase": "generation",
                        "label": "oracle-frame",
                        "rng_family": "xorshift96-gameplay",
                        "total_calls": 1,
                        "overflow_calls": 0,
                        "caller_count": 1,
                        "caller0_rva": "0x2345",
                        "caller0_count": 1,
                    },
                    {
                        "event": RNG_CRT_CALLERS_EVENT,
                        "ts_qpc": 112 + seq * 100,
                        "phase": "generation",
                        "label": "oracle-frame",
                        "rng_family": "crt-rand-import",
                        "total_calls": 0,
                        "overflow_calls": 0,
                        "seed_calls": 0,
                        "last_seed": 0,
                        "event_count": 0,
                        "overflow_events": 0,
                        "sequence_hash": "0xcbf29ce484222325",
                        "caller_count": 0,
                    },
                ])
            events.append({
                "event": "generate_complete",
                "mode": "normal",
                "oracle_ok": True,
                "integrity_ok": True,
                "frames": 2,
            })
            path.write_text(
                "".join(json.dumps(event) + "\n" for event in events),
                encoding="utf-8",
            )

        normal_reference = root / "normal-reference.jsonl"
        normal_wind_variant = root / "normal-wind-variant.jsonl"
        normal_gameplay_bad = root / "normal-gameplay-bad.jsonl"
        normal_caller_bad = root / "normal-caller-bad.jsonl"
        write_normal_control(normal_reference, wind_hash="0x4000")
        write_normal_control(normal_wind_variant, wind_hash="0x5000")
        write_normal_control(
            normal_gameplay_bad,
            wind_hash="0x5000",
            gameplay_delta=1,
        )
        write_normal_control(
            normal_caller_bad,
            wind_hash="0x5000",
            caller_delta=1,
        )
        normal_repro = normal_control_reproducibility(
            normal_reference, normal_wind_variant
        )
        assert normal_repro["classification"] \
            == "normal-control-stage-wind-divergence"
        assert normal_repro["first_stage_wind_diagnostic"] is not None
        assert normal_control_reproducibility(
            normal_reference, normal_gameplay_bad
        )["classification"] == "normal-control-gameplay-rng-divergence"
        assert normal_control_reproducibility(
            normal_reference, normal_caller_bad
        )["classification"] == "normal-control-rng-caller-divergence"

        wind_expected = {
            "stage_wind_graph_count": 1,
            "stage_wind_emitter_count": 1,
            "stage_wind_node_0_vtable_rva": "0x3e88ce8",
            "stage_wind_node_0_hash": "0x100",
            "stage_wind_node_0_semantic_word_112": "0x1",
            "stage_wind_node_0_semantic_word_244": "0x2",
            "stage_wind_node_0_semantic_word_312": "0x3",
            "stage_wind_emitter_0_word_80": "0x4",
            "stage_wind_emitter_0_word_108": "0x1111",
            "stage_wind_emitter_0_word_124": "0x2222",
            **{
                f"stage_wind_output_force_{index}": 0.0
                for index in range(12)
            },
        }
        wind_residue_only = dict(wind_expected)
        wind_residue_only.update({
            # Node +0xF4/+0x138 and emitter +0x6C/+0x7C are raw
            # diagnostics, not semantic comparison fields.
            "stage_wind_node_0_semantic_word_244": "0x9999",
            "stage_wind_node_0_semantic_word_312": "0xAAAA",
            "stage_wind_emitter_0_word_108": "0xBBBB",
            "stage_wind_emitter_0_word_124": "0xCCCC",
        })
        assert stage_wind_differences(
            wind_expected, wind_residue_only, 1.0e-5
        ) == []
        assert any(
            difference["field"]
            == "p1_secondary_event_previous_variant"
            for difference in presentation_differences(
                {
                    "p1_secondary_event_previous_variant": "0x4",
                    "p2_secondary_event_previous_variant": "0x8",
                },
                {
                    "p1_secondary_event_previous_variant": "0x1",
                    "p2_secondary_event_previous_variant": "0x8",
                },
            )
        )
        wind_semantic_change = dict(wind_expected)
        wind_semantic_change[
            "stage_wind_node_0_semantic_word_112"
        ] = "0x2"
        wind_semantic_change["stage_wind_emitter_0_word_80"] = "0x5"
        semantic_differences = stage_wind_differences(
            wind_expected, wind_semantic_change, 1.0e-5
        )
        assert [difference["offset"] for difference in semantic_differences
                if "offset" in difference] == [0x70, 0x50]

        shock_expected = {
            "stage_wind_graph_count": 1,
            "stage_wind_emitter_count": 0,
            "stage_wind_node_0_vtable_rva": "0x3e88d18",
            "stage_wind_node_0_semantic_word_288": "0x1",
            "stage_wind_node_0_semantic_word_296": "0x2",
            "stage_wind_node_0_semantic_word_300": "0x6",
            "stage_wind_node_0_semantic_word_304": "0x3",
        }
        shock_residue_only = dict(shock_expected)
        shock_residue_only["stage_wind_node_0_semantic_word_300"] = "0x0"
        assert stage_wind_differences(
            shock_expected, shock_residue_only, 1.0e-5
        ) == []
        shock_semantic_change = dict(shock_expected)
        shock_semantic_change["stage_wind_node_0_semantic_word_296"] = "0x9"
        assert [
            difference["offset"]
            for difference in stage_wind_differences(
                shock_expected, shock_semantic_change, 1.0e-5
            )
            if "offset" in difference
        ] == [0x128]

        ring_in_fingerprint = ring_in_five_call_fingerprint(
            {
                "callers": [
                    {"rva": "0x33371f", "count": 4},
                    {"rva": "0x3337a5", "count": 8},
                    {"rva": "0x3337f2", "count": 8},
                ],
            },
            {
                "callers": [
                    {"rva": "0x33371f", "count": 5},
                    {"rva": "0x3337a5", "count": 10},
                    {"rva": "0x3337f2", "count": 10},
                ],
            },
        )
        assert ring_in_fingerprint["matched"]
        assert ring_in_fingerprint["attribution"] == (
            "one-extra-IwWind_UpdateRingInOscillation-iteration"
        )
        assert not ring_in_five_call_fingerprint(
            {"callers": []},
            {
                "callers": [
                    {"rva": "0x33371f", "count": 1},
                    {"rva": "0x3337a5", "count": 2},
                    {"rva": "0x3337f2", "count": 1},
                ],
            },
        )["matched"]

        try:
            analyze_report(fixture("oracle-gap", oracle_gap=True))
        except ValueError as error:
            assert "oracle sequence missing" in str(error)
        else:
            raise AssertionError("oracle sequence gap was accepted")
    print("rollback replay state diff self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-report", type=Path)
    parser.add_argument("--case-sha256")
    parser.add_argument(
        "--normal-control-reference",
        type=Path,
        help="first exact-artifact full normal trace",
    )
    parser.add_argument(
        "--normal-control-candidate",
        type=Path,
        help="second exact-artifact full normal trace",
    )
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--float-tolerance", type=float, default=1.0e-5)
    parser.add_argument("--max-differences", type=int, default=32)
    parser.add_argument(
        "--require-logical-prefix-frames",
        type=int,
        default=0,
        metavar="COUNT",
        help=(
            "require and compare logical frames 0..COUNT-1 in addition "
            "to the report's correction window"
        ),
    )
    parser.add_argument("--selftest", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.selftest:
        return selftest()
    normal_repro_requested = (
        args.normal_control_reference is not None
        or args.normal_control_candidate is not None
    )
    if normal_repro_requested:
        if args.normal_control_reference is None \
                or args.normal_control_candidate is None:
            raise SystemExit(
                "normal-control reproducibility requires both traces"
            )
        if args.corpus_report is not None:
            raise SystemExit(
                "normal-control reproducibility is a standalone mode"
            )
        result = normal_control_reproducibility(
            args.normal_control_reference.resolve(),
            args.normal_control_candidate.resolve(),
            args.float_tolerance,
            args.max_differences,
        )
        print(
            f"classification={result['classification']} "
            f"compared_frames={result['compared_frames']} "
            f"stage_wind_diagnostic="
            f"{'present' if result['first_stage_wind_diagnostic'] else 'none'}"
        )
        if args.json_out:
            if args.json_out.exists():
                raise SystemExit(f"output already exists: {args.json_out}")
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(
                json.dumps(result, indent=2) + "\n",
                encoding="utf-8",
            )
            print(f"report={args.json_out}")
        return 0 if result["classification"] \
            == "normal-control-reproducible" else 1
    if args.corpus_report is None:
        raise SystemExit("--corpus-report is required")
    if args.float_tolerance < 0 or args.max_differences <= 0 \
            or args.require_logical_prefix_frames < 0:
        raise SystemExit("invalid comparison limits")
    result = analyze_report(
        args.corpus_report,
        args.case_sha256,
        args.float_tolerance,
        args.max_differences,
        args.require_logical_prefix_frames,
    )
    print_summary(result)
    if args.json_out:
        if args.json_out.exists():
            raise SystemExit(f"output already exists: {args.json_out}")
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(result, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"report={args.json_out}")
    return result_exit_code(result)


if __name__ == "__main__":
    raise SystemExit(main())
