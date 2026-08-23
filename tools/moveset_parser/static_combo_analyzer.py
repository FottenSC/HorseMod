"""Parameterized, asset-only SC6 combo/KHit analyzer.

The first checked-in scenario is Astaroth lethal 6A+B into held (4)[B].
The module deliberately reports an unresolved result until every required
native boundary (timing, pose, body separation, masks and overlap) is present;
observed labels are validation data and never classifier inputs.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
from typing import Iterable

from hgmotion_reference import (
    CollisionPose,
    CompactCollisionSkeleton,
    PoseMotionLane,
    decode_collision_pose,
    decode_four_lane_collision_pose,
    decode_root_movement_frames,
    load_compact_collision_skeleton_from_nmd_manifest,
    make_lux_world_transform,
    transform_point,
)
from lux_lane_playback import simulate_held_canyon_timeline
from luxformats import HitRecord, parse_hit_dat, parse_khd, parse_mot
from locomotion_movement import CHARACTERS
from native_reaction_table import parse_hit_reaction_move_id_table
from stackvm_emulate import Concrete, emulate


@dataclass(frozen=True)
class SpacingSweepPolicy:
    minimum: float
    maximum: float
    step: float
    require_contiguous_agreement: bool = True


@dataclass(frozen=True)
class ComboScenario:
    attacker_id: str
    style_id: str
    opener_slot: int
    forced_contact_classification: str
    followup_entry_slot: int
    followup_held_target_slot: int
    defender_action_slot: int
    defender_local_direction: str
    spacing: SpacingSweepPolicy
    observed_hits: tuple[str, ...] = ()
    observed_escapes: tuple[str, ...] = ()

    @classmethod
    def from_mapping(cls, value: dict) -> "ComboScenario":
        spacing_value = value.get("spacing", {})
        spacing = SpacingSweepPolicy(
            minimum=float(spacing_value["minimum"]),
            maximum=float(spacing_value["maximum"]),
            step=float(spacing_value["step"]),
            require_contiguous_agreement=bool(
                spacing_value.get("require_contiguous_agreement", True)
            ),
        )
        if spacing.step <= 0.0 or spacing.maximum < spacing.minimum:
            raise ValueError("scenario spacing sweep is invalid")
        return cls(
            attacker_id=str(value["attacker_id"]).lower(),
            style_id=str(value["style_id"]).lower(),
            opener_slot=int(value["opener_slot"]),
            forced_contact_classification=str(value["forced_contact_classification"]),
            followup_entry_slot=int(value["followup_entry_slot"]),
            followup_held_target_slot=int(value["followup_held_target_slot"]),
            defender_action_slot=int(value["defender_action_slot"]),
            defender_local_direction=str(value["defender_local_direction"]),
            spacing=spacing,
            observed_hits=tuple(str(x).lower() for x in value.get("observed_hits", ())),
            observed_escapes=tuple(str(x).lower() for x in value.get("observed_escapes", ())),
        )


@dataclass(frozen=True)
class ReactionTimelineState:
    """Native logical-tick landmarks for a direct hit reaction and ukemi.

    Tick zero is the opener contact.  The hitstun counter is consumed after
    all three MoveVM lanes, while lane-0 input selection runs before that
    decrement.  A zero-threshold transition authored by slot 0x78 is then
    committed by ExecuteOpStream's same-invocation post-script check.
    """

    initial_counter: int
    reaction_initial_coordinate: int
    reaction_terminal_coordinate: int
    reaction_terminal_tick: int | None
    first_input_recognition_tick: int
    grounded_dispatch_tick: int
    ukemi_queue_tick: int
    ukemi_commit_tick: int
    ukemi_initial_coordinate: float
    ukemi_blend_duration: float


@dataclass(frozen=True)
class MotionLaneState:
    solver_slot: int
    owner_lane: int
    descriptor_index: int
    packed_motion_id: int | None
    sample_coordinate: float | None
    track: int
    weight: float
    active: bool


@dataclass(frozen=True)
class PoseSolverState:
    lanes: tuple[MotionLaneState, ...]
    auxiliary_records: tuple[MotionLaneState, ...]
    controller_active: bool | None
    spine_ik_active: bool | None
    main_analytic_ik_active: bool
    post_physics_foot_ik_active: bool | None
    complete: bool
    unresolved: tuple[str, ...] = ()


@dataclass(frozen=True)
class OpenerContinuationState:
    """Authored lane-1 continuation used to preserve an opener's pose/root."""

    target_slot: int
    packed_motion_id: int
    author_window_start: int
    author_window_end: int
    target_start_coordinate: int
    transition_threshold: int
    author_tick: int
    commit_tick: int
    final_sample_coordinate: int
    final_sample_tick: int


def build_reaction_timeline(
    reaction_lane_length: int,
    *,
    initial_counter: int = 52,
    reaction_initial_coordinate: int = 1,
) -> ReactionTimelineState:
    """Build the proven row-1037 scheduler timeline.

    ``reaction_lane_length`` is the move slot's authored lane length, not a
    state-window coordinate recovered from its nested reaction helper.
    ``LuxMoveVM_AdvanceLaneFrameStep`` keeps equality active, so the lane
    publishes coordinates 1..length on contact-relative ticks 1..length.
    The exact same-invocation cache/deactivation ordering after the final
    sample remains unresolved.
    A 52-step lethal counter reaches zero only after lanes execute on tick 52,
    so held input is first eligible on tick 53.  Slot 0x78's one-argument
    author call queues and commits 0x8E on that same tick with start coordinate
    zero and no weight ramp.
    """

    terminal_coordinate = int(reaction_lane_length)
    if terminal_coordinate <= reaction_initial_coordinate:
        raise ValueError("reaction lane length does not admit an active sample")
    admission_tick = int(initial_counter) + 1
    return ReactionTimelineState(
        initial_counter=int(initial_counter),
        reaction_initial_coordinate=int(reaction_initial_coordinate),
        reaction_terminal_coordinate=terminal_coordinate,
        reaction_terminal_tick=None,
        first_input_recognition_tick=admission_tick,
        grounded_dispatch_tick=admission_tick,
        ukemi_queue_tick=admission_tick,
        ukemi_commit_tick=admission_tick,
        ukemi_initial_coordinate=0.0,
        ukemi_blend_duration=0.0,
    )


def build_pose_solver_state(
    *,
    tick: int,
    ukemi_slot: object,
    ukemi_coordinate: float,
    reaction_packed_motion: int,
    reaction_coordinate: float,
    reaction_lane_last_tick: int,
) -> PoseSolverState:
    """Describe the four ordered main solve lanes and auxiliary record state.

    MoveVM lane 0 owns solver slots 0/1, lane 1 owns 2/3, and lane 2 owns
    the fifth physical playback record.  ``LuxBattleChara_SolveBonePose``
    iterates only slots 0..3 in its ordered main blend loop; slot 4 is reported
    separately so callers cannot accidentally treat it as a fifth blend lane.
    The row-1037 scenario has no lane-2 motion.  A missing secondary descriptor
    is represented as inactive state instead of an identity clip.
    """

    motion_b = int(getattr(ukemi_slot, "wMotionBId_10"))
    lanes = (
        MotionLaneState(
            0, 0, 0, int(getattr(ukemi_slot, "wAnimationIndex_00")),
            float(ukemi_coordinate), int(getattr(ukemi_slot, "bMotionATrack_06")),
            float(getattr(ukemi_slot, "flMotionAWeightHundredths_08")) / 100.0,
            True,
        ),
        MotionLaneState(
            1, 0, 1, None if motion_b == 0xFFFF else motion_b,
            None if motion_b == 0xFFFF else float(ukemi_coordinate),
            int(getattr(ukemi_slot, "bMotionBTrack_16")),
            float(getattr(ukemi_slot, "flMotionBWeightHundredths_18")) / 100.0,
            motion_b != 0xFFFF,
        ),
        MotionLaneState(
            2, 1, 0, int(reaction_packed_motion), float(reaction_coordinate),
            0, 1.0, tick <= reaction_lane_last_tick,
        ),
        MotionLaneState(3, 1, 1, None, None, 0, 0.0, False),
    )
    auxiliary_records = (
        MotionLaneState(4, 2, 0, None, None, 0, 0.0, False),
    )
    main_analytic = bool(int(getattr(ukemi_slot, "bMotionAFlags_07")) & 0x80)
    unresolved = (
        "controller, spine-IK, and post-physics gate producers are not fully proven",
    )
    if main_analytic:
        unresolved += ("main analytic IK calculation for motion-flag 0x80",)
    return PoseSolverState(
        lanes=lanes,
        auxiliary_records=auxiliary_records,
        controller_active=None,
        spine_ik_active=None,
        main_analytic_ik_active=main_analytic,
        post_physics_foot_ik_active=None,
        complete=False,
        unresolved=unresolved,
    )


@dataclass(frozen=True)
class WorldSphere:
    slot: int
    bone: int
    center: tuple[float, float, float]
    radius: float


@dataclass(frozen=True)
class SphereContact:
    attack_slot: int
    hurt_slot: int
    hurt_bone: int
    center_distance: float
    radius_sum: float
    clearance: float
    overlaps: bool
    attack_center: tuple[float, float, float]
    hurt_center: tuple[float, float, float]


def strict_sphere_contact(attack: WorldSphere, hurt: WorldSphere) -> SphereContact:
    dx = attack.center[0] - hurt.center[0]
    dy = attack.center[1] - hurt.center[1]
    dz = attack.center[2] - hurt.center[2]
    squared = dx * dx + dy * dy + dz * dz
    radius_sum = attack.radius + hurt.radius
    distance = math.sqrt(squared)
    return SphereContact(
        attack_slot=attack.slot,
        hurt_slot=hurt.slot,
        hurt_bone=hurt.bone,
        center_distance=distance,
        radius_sum=radius_sum,
        clearance=distance - radius_sum,
        overlaps=squared < radius_sum * radius_sum,
        attack_center=attack.center,
        hurt_center=hurt.center,
    )


def _merge_open_intervals(intervals: Iterable[tuple[float, float]]) -> tuple[tuple[float, float], ...]:
    merged: list[list[float]] = []
    for lower, upper in sorted(intervals):
        if upper <= lower:
            continue
        if not merged or lower > merged[-1][1]:
            merged.append([lower, upper])
        else:
            merged[-1][1] = max(merged[-1][1], upper)
    return tuple((lower, upper) for lower, upper in merged)


def spacing_samples(policy: SpacingSweepPolicy, *, include: float | None = None) -> tuple[float, ...]:
    """Return deterministic policy samples without cumulative float drift."""

    count = int(math.floor((policy.maximum - policy.minimum) / policy.step + 1.0e-9))
    values = [policy.minimum + index * policy.step for index in range(count + 1)]
    if not values or values[-1] < policy.maximum - 1.0e-9:
        values.append(policy.maximum)
    if include is not None and policy.minimum <= include <= policy.maximum:
        values.append(float(include))
    return tuple(sorted(set(round(value, 12) for value in values)))


def sampled_true_intervals(
    samples: Iterable[tuple[float, bool]],
    *,
    policy_step: float,
) -> tuple[tuple[float, float], ...]:
    """Return conservative contiguous intervals proven by adjacent true samples.

    A singleton is deliberately excluded: the scenario acceptance criterion
    requires an interval containing more than one sampled point. Endpoints are
    sampled coordinates, not interpolated collision roots.
    """

    ordered = sorted(samples)
    runs: list[tuple[float, float]] = []
    start: float | None = None
    previous: float | None = None
    count = 0
    tolerance = policy_step * 1.000001
    for spacing, active in ordered:
        contiguous = previous is not None and spacing - previous <= tolerance
        if active and (start is None or contiguous):
            if start is None:
                start = spacing
                count = 1
            else:
                count += 1
        elif active:
            if start is not None and previous is not None and count > 1:
                runs.append((start, previous))
            start = spacing
            count = 1
        else:
            if start is not None and previous is not None and count > 1:
                runs.append((start, previous))
            start = None
            count = 0
        previous = spacing
    if start is not None and previous is not None and count > 1:
        runs.append((start, previous))
    return tuple(runs)


def overlap_spacing_intervals(
    attack: WorldSphere,
    hurts: Iterable[WorldSphere],
    *,
    sampled_spacing: float,
    spacing_axis_z: float = 1.0,
    minimum: float,
    maximum: float,
) -> tuple[tuple[float, float], ...]:
    """Solve every strict sphere inequality for the initial Z spacing."""

    intervals: list[tuple[float, float]] = []
    for hurt in hurts:
        dx = hurt.center[0] - attack.center[0]
        dy = hurt.center[1] - attack.center[1]
        radius_sum = hurt.radius + attack.radius
        horizontal_square = radius_sum * radius_sum - dx * dx - dy * dy
        if horizontal_square <= 0.0:
            continue
        root = math.sqrt(horizontal_square)
        if spacing_axis_z == 0.0:
            raise ValueError("spacing axis must move the defender")
        base_z = hurt.center[2] - spacing_axis_z * sampled_spacing - attack.center[2]
        endpoint_a = (-base_z - root) / spacing_axis_z
        endpoint_b = (-base_z + root) / spacing_axis_z
        lower = max(minimum, min(endpoint_a, endpoint_b))
        upper = min(maximum, max(endpoint_a, endpoint_b))
        if lower < upper:
            intervals.append((lower, upper))
    return _merge_open_intervals(intervals)


def publish_spheres(
    records: Iterable[HitRecord],
    pose: CollisionPose,
    *,
    active_mask: int = 0x3FFFFF,
    radius_scale: float = 1.0,
    offset_scale: float = 1.0,
    position_offset: tuple[float, float, float] = (0.0, 0.0, 0.0),
    event_records: Iterable[object] | None = None,
    packed_move_id: int | None = None,
    event_interpolation: float = 1.0,
    flat_terrain_height: float | None = None,
    classifier_rows_only: bool = False,
) -> tuple[WorldSphere, ...]:
    spheres: list[WorldSphere] = []
    for record in records:
        if record.tag != 0 or not (active_mask & (1 << record.slot)):
            continue
        # ResolveAttackVsHurtboxMask22 deliberately excludes row 22 and all
        # higher rows from the damaging strike partition even if geometry was
        # accumulated for them.  Keep physical BODY publication unrestricted;
        # this gate belongs only on damage-classifier hurt spheres.
        if classifier_rows_only and record.slot > 21:
            continue
        bone = int(record.bone_index_ue4)
        transform = pose.requested_world.get(bone)
        if transform is None:
            raise ValueError(f"pose omitted KHit bone {bone}")
        record_radius_scale = radius_scale
        applied_position_offset = tuple(value * offset_scale for value in position_offset)
        if event_records is not None and packed_move_id is not None:
            event_radius, event_offset, event_scale = sphere_event_modifier(
                event_records, packed_move_id, int(record.slot),
                interpolation=event_interpolation,
            )
            record_radius_scale *= event_radius
            applied_position_offset = tuple(
                base + extra * event_scale
                for base, extra in zip(applied_position_offset, event_offset)
            )
        center = transform_point(
            transform,
            (
                record.pos_x + applied_position_offset[0],
                record.pos_y + applied_position_offset[1],
                record.pos_z + applied_position_offset[2],
            ),
        )
        radius = record.radius * record_radius_scale
        # UpdateAllKHitWorldCenters applies this after the ordinary local-to-
        # world transform for grounded kind/slot 6 and 7 spheres.  The scoped
        # scenario is an open flat stage and ukemi publishes the grounded
        # flags needed by the caller gate.
        if (
            flat_terrain_height is not None
            and record.slot in (6, 7)
            and center[1] - flat_terrain_height < 0.2
        ):
            center = (center[0], center[1] - 0.1, center[2])
            radius += 0.05
        spheres.append(
            WorldSphere(record.slot, bone, center, radius)
        )
    return tuple(spheres)


def sphere_event_modifier(
    event_records: Iterable[object],
    packed_move_id: int,
    sphere_slot: int,
    *,
    interpolation: float = 1.0,
) -> tuple[float, tuple[float, float, float], float]:
    """Return native first-match sphere radius/offset modification.

    ``KHitSphere_UpdateFromAnimCell`` scans the current MoveBank event records
    in authored order. Event kind 1 addresses sphere nodes; the qword at
    +0x08/+0x0C is a slot mask. Radius is replaced by authored radius times
    +0x28, while the event XYZ offset is multiplied by the live interpolation
    scalar before being added to the authored local centre.
    """

    for event in event_records:
        if int(event.dwPackedMoveId) != packed_move_id or int(event.dwEventKind) != 1:
            continue
        slot_mask = int(event.dwField08) | (int(event.dwShapeFlags) << 32)
        if slot_mask & (1 << sphere_slot):
            return (
                float(event.flRadiusScale),
                (float(event.flOffsetX), float(event.flOffsetY), float(event.flOffsetZ)),
                interpolation,
            )
    return 1.0, (0.0, 0.0, 0.0), 0.0


def solve_body_separation_step(
    p1_pose: CollisionPose,
    p1_records: Iterable[HitRecord],
    p2_pose: CollisionPose,
    p2_records: Iterable[HitRecord],
    *,
    push_angle_turns: float = 0.25,
    p1_radius: float = 0.3,
    p2_radius: float = 0.3,
    separation_scale: float = 1.0,
    p1_weight: float = 1.0,
    p2_weight: float = 1.0,
    p1_body_type: int = 0,
    p2_body_type: int = 0,
    p1_active_body_mask: int = 0xFFFFFFFFFFFFFFFF,
    p2_active_body_mask: int = 0xFFFFFFFFFFFFFFFF,
) -> tuple[tuple[float, float], tuple[float, float], dict]:
    """Port the stock type-0 path of LuxBattle_SolvePhysBodyCollision.

    Returned X/Z deltas apply uniformly to all matrices.  The broad pass is
    followed by a BODY-sphere refresh and the native maximum-force pass.
    """

    angle = push_angle_turns * math.tau
    direction = (math.cos(angle), math.sin(angle))
    total_weight = p1_weight + p2_weight
    if total_weight <= 0.0:
        raise ValueError("physical-body weights must have a positive sum")
    # Native does not use the usual inverse-mass split.  Each side receives
    # its own +0x470 impact-force fraction: P1 moves by P1/(P1+P2), and P2 by
    # P2/(P1+P2).  This matters for knockdown state 6, whose published value
    # is 0.25 rather than the ordinary state-1 value 1.0.
    p1_share = p1_weight / total_weight
    p2_share = p2_weight / total_weight
    p1_delta = [0.0, 0.0]
    p2_delta = [0.0, 0.0]

    p1_root = p1_pose.world[1].translation
    p2_root = p2_pose.world[1].translation
    dx, dz = p1_root[0] - p2_root[0], p1_root[2] - p2_root[2]
    distance = math.hypot(dx, dz)
    broad_force = 0.0
    p1_lower, p1_upper = _body_vertical_interval(p1_pose, p1_body_type)
    p2_lower, p2_upper = _body_vertical_interval(p2_pose, p2_body_type)
    vertical_overlap = p2_lower < p1_upper and p1_lower < p2_upper
    if distance * distance < (p1_radius + p2_radius) ** 2 and vertical_overlap:
        broad_force = (p1_radius + p2_radius - distance) * separation_scale
        p1_delta[0] -= direction[0] * broad_force * p1_share
        p1_delta[1] -= direction[1] * broad_force * p1_share
        p2_delta[0] += direction[0] * broad_force * p2_share
        p2_delta[1] += direction[1] * broad_force * p2_share

    def shifted(sphere: WorldSphere, delta: list[float]) -> WorldSphere:
        return WorldSphere(
            sphere.slot,
            sphere.bone,
            (sphere.center[0] + delta[0], sphere.center[1], sphere.center[2] + delta[1]),
            sphere.radius,
        )

    p1_body = tuple(
        shifted(sphere, p1_delta)
        for sphere in publish_spheres(p1_records, p1_pose, active_mask=p1_active_body_mask)
    )
    p2_body = tuple(
        shifted(sphere, p2_delta)
        for sphere in publish_spheres(p2_records, p2_pose, active_mask=p2_active_body_mask)
    )
    pair_force = 0.0
    pair_slots: tuple[int, int] | None = None
    records1 = {
        record.slot: record
        for record in p1_records
        if record.tag == 0 and p1_active_body_mask & (1 << record.slot)
    }
    records2 = {
        record.slot: record
        for record in p2_records
        if record.tag == 0 and p2_active_body_mask & (1 << record.slot)
    }
    for index1, sphere1 in enumerate(p1_body):
        for index2, sphere2 in enumerate(p2_body):
            contact = strict_sphere_contact(sphere1, sphere2)
            if not contact.overlaps:
                continue
            force = -contact.clearance * min(
                records1[sphere1.slot].contact_impulse_scale,
                records2[sphere2.slot].contact_impulse_scale,
            )
            if force > pair_force:
                pair_force = force
                pair_slots = (sphere1.slot, sphere2.slot)
    if pair_force > 0.0:
        p1_delta[0] -= direction[0] * pair_force * p1_share
        p1_delta[1] -= direction[1] * pair_force * p1_share
        p2_delta[0] += direction[0] * pair_force * p2_share
        p2_delta[1] += direction[1] * pair_force * p2_share
    return tuple(p1_delta), tuple(p2_delta), {
        "broadForce": broad_force,
        "pairForce": pair_force,
        "pairSlots": pair_slots,
    }


def _body_vertical_interval(
    pose: CollisionPose, body_type: int,
) -> tuple[float, float]:
    """Recover the vertical interval consumed by the broad BODY pass."""

    ys = [transform.translation[1] for transform in pose.world[:23]]
    if body_type == 0:
        lower, upper = pose.world[0].translation[1] - 1.0, max(ys)
    elif body_type == 1:
        lower, upper = min(ys[15], ys[16], ys[20]), max(ys)
    elif body_type == 2:
        lower, upper = min(ys), ys[3]
    elif body_type == 3:
        lower, upper = ys[15], max(ys)
    else:
        raise ValueError(f"unsupported physical body type {body_type}")
    return (min(lower, upper), max(lower, upper))


def solve_broad_body_from_roots(
    p1_root: tuple[float, float],
    p2_root: tuple[float, float],
    *,
    vertical_overlap: bool,
    push_angle_turns: float,
    p1_radius: float,
    p2_radius: float,
    separation_scale: float,
    p1_share: float,
    p2_share: float,
) -> tuple[tuple[float, float], tuple[float, float], float]:
    """Fast native broad BODY pass used by the nonlinear spacing sweep."""

    dx = p1_root[0] - p2_root[0]
    dz = p1_root[1] - p2_root[1]
    distance = math.hypot(dx, dz)
    radius_sum = p1_radius + p2_radius
    if distance * distance >= radius_sum * radius_sum or not vertical_overlap:
        return (0.0, 0.0), (0.0, 0.0), 0.0
    force = (radius_sum - distance) * separation_scale
    angle = push_angle_turns * math.tau
    direction = (math.cos(angle), math.sin(angle))
    return (
        (-direction[0] * force * p1_share, -direction[1] * force * p1_share),
        (direction[0] * force * p2_share, direction[1] * force * p2_share),
        force,
    )


def _resolve_motion(packed: int, character_bank, common_bank):
    bank_index = (packed >> 12) & 0xF
    clip_index = packed & 0x7FF
    if bank_index == 0:
        bank = character_bank
    elif bank_index == 1:
        bank = common_bank
    else:
        raise ValueError(f"motion bank {bank_index} is outside the proven combo subset")
    return bank, clip_index


def _root_at(bank, clip_index: int, frame: float) -> tuple[float, float, float]:
    # One scenario asks for the same clip root on every simulation tick.
    # Keep the decoded frames on this parsed bank instance; reparsing and
    # Huffman-decoding the complete clip for each lookup made a spacing sweep
    # needlessly quadratic without changing any arithmetic.
    cache = getattr(bank, "_static_combo_root_frames", None)
    if cache is None:
        cache = {}
        setattr(bank, "_static_combo_root_frames", cache)
    frames = cache.get(clip_index)
    if frames is None:
        _clip, frames, _confidence = decode_root_movement_frames(
            bank.section(clip_index), clip_index, bank.offsets[clip_index]
        )
        cache[clip_index] = frames
    sample_frame = min(max(float(frame), 0.0), len(frames) - 1.0)
    lower_index = int(sample_frame)
    upper_index = min(lower_index + 1, len(frames) - 1)
    alpha = sample_frame - lower_index
    lower = frames[lower_index]
    upper = frames[upper_index]
    return tuple(
        a + (b - a) * alpha
        for a, b in zip(
            (lower.cumulative_x, lower.cumulative_y, lower.cumulative_z),
            (upper.cumulative_x, upper.cumulative_y, upper.cumulative_z),
        )
    )


def _horizontal_delta(
    current: tuple[float, float, float],
    origin: tuple[float, float, float],
    facing_turns: float,
    root_scale: float = 1.0,
) -> tuple[float, float]:
    x = (current[0] - origin[0]) * root_scale
    z = (current[2] - origin[2]) * root_scale
    angle = facing_turns * math.tau
    return (
        math.cos(angle) * x + math.sin(angle) * z,
        -math.sin(angle) * x + math.cos(angle) * z,
    )


def decode_reaction_move_velocity(
    attack_cell: object,
    *,
    reaction_phase: int,
    opponent_facing_turns: float,
    time_scale: float = 1.0,
) -> tuple[float, float, float, dict]:
    """Decode the attack-cell move-offset slot used by hit-reaction setup.

    ``ApplyMoveOffsetToChara`` indexes six signed triples at cell +0x08 with
    ``FLuxHitReactionParams.nReactionPhase``. ``ResolveRangeAndAngleOffset``
    then converts range/1000 and authored degree angles to a Lux-space vector,
    adding the attacker's facing snapshot to the horizontal angle.
    """

    raw = attack_cell.raw
    if not 0 <= reaction_phase < 6:
        raise ValueError(f"reaction phase {reaction_phase} is outside move-offset slots")
    offset = 0x08 + reaction_phase * 6
    range_raw = int.from_bytes(raw[offset:offset + 2], "little", signed=True)
    angle_a = int.from_bytes(raw[offset + 2:offset + 4], "little", signed=True)
    angle_b = int.from_bytes(raw[offset + 4:offset + 6], "little", signed=True)
    magnitude = range_raw / 1000.0 * time_scale
    horizontal = angle_a / 360.0 + opponent_facing_turns
    vertical = angle_b / 360.0
    horizontal_radians = horizontal * math.tau
    vertical_radians = vertical * math.tau
    planar = magnitude * math.cos(vertical_radians)
    velocity = (
        planar * math.sin(horizontal_radians),
        magnitude * math.sin(vertical_radians),
        planar * math.cos(horizontal_radians),
    )
    return velocity[0], velocity[1], velocity[2], {
        "reactionPhase": reaction_phase,
        "rangeRaw": range_raw,
        "angleADegrees": angle_a,
        "angleBDegrees": angle_b,
        "opponentFacingTurns": opponent_facing_turns,
        "velocity": velocity,
    }


def _concrete_event_args(event: object) -> tuple[int, ...] | None:
    values: list[int] = []
    for value in getattr(event, "args", ()):
        if not isinstance(value, Concrete):
            return None
        values.append(int(value.value) & 0xFFFF)
    return tuple(values)


def _resolve_attack_recovery_entry(
    khd: object,
    opener_slot: int,
    contact_coordinate: int,
) -> tuple[int, int, int]:
    """Resolve the common 0x3020 recovery-open coordinate and entry tick.

    The helper's third concrete word is the recovery lead. Native timing maps
    ``0x7600-lead`` to ``wTotalFrames-lead``. ExecuteOpStream commits a
    satisfied transition before the pre-advance pose latch, so the preceding
    coordinate is the final outgoing sample and the target occupies the open
    coordinate's tick.
    """

    opener = khd.slots[opener_slot]
    if opener.bytecode is None:
        raise ValueError(f"opener slot {opener_slot} has no MoveVM bytecode")
    setup_calls = tuple(
        event
        for event in emulate(opener.bytecode, opener_slot).bank_scripts
        if _concrete_event_args(event)
        and _concrete_event_args(event)[0] == 0x3020
    )
    if len(setup_calls) != 1:
        raise ValueError(
            f"opener slot {opener_slot} has {len(setup_calls)} concrete 0x3020 setup calls"
        )
    setup_args = _concrete_event_args(setup_calls[0])
    assert setup_args is not None and len(setup_args) >= 3
    recovery_lead = setup_args[2]
    recovery_open_coordinate = int(opener.wTotalFrames) - recovery_lead
    entry_tick = recovery_open_coordinate - int(contact_coordinate)
    if entry_tick < 1:
        raise ValueError("attack recovery opens no later than the contact coordinate")
    return recovery_lead, recovery_open_coordinate, entry_tick


def _resolve_opener_continuation(
    khd: object,
    opener_slot: int,
    contact_coordinate: int,
) -> OpenerContinuationState:
    """Find the opener's same-motion, cell-less lane-1 continuation route."""

    opener = khd.slots[opener_slot]
    if opener.bytecode is None:
        raise ValueError(f"opener slot {opener_slot} has no MoveVM bytecode")
    trace = emulate(opener.bytecode, opener_slot)
    candidates: list[tuple[object, tuple[int, ...]]] = []
    for event in trace.transitions:
        args = _concrete_event_args(event)
        if args is None or len(args) < 3:
            continue
        target_slot = event.next_move_slot
        if target_slot is None or not 0 <= target_slot < len(khd.slots):
            continue
        target = khd.slots[target_slot]
        if (
            target.wAnimationIndex_00 == opener.wAnimationIndex_00
            and all(index < 0 for index in target.nCellBoneIndexPerVariant)
            and args[1] == args[2]
        ):
            candidates.append((event, args))
    if len(candidates) != 1:
        raise ValueError(
            f"opener slot {opener_slot} has {len(candidates)} same-motion continuation routes"
        )
    event, args = candidates[0]
    timing_events = tuple(
        predicate
        for predicate in trace.predicates
        if predicate.callcond_idx == 0x25
        and predicate.source_pc < event.source_pc
        and _concrete_event_args(predicate) is not None
    )
    if not timing_events:
        raise ValueError("opener continuation has no preceding concrete timing window")
    timing_args = _concrete_event_args(timing_events[-1])
    assert timing_args is not None and len(timing_args) == 2
    target_slot = int(event.next_move_slot)
    target = khd.slots[target_slot]
    author_tick = max(1, timing_args[0] - int(contact_coordinate))
    commit_tick = args[2] - int(contact_coordinate)
    final_sample_coordinate = int(target.wTotalFrames) - 1
    final_sample_tick = commit_tick + final_sample_coordinate - args[1]
    return OpenerContinuationState(
        target_slot=target_slot,
        packed_motion_id=int(target.wAnimationIndex_00),
        author_window_start=timing_args[0],
        author_window_end=timing_args[1],
        target_start_coordinate=args[1],
        transition_threshold=args[2],
        author_tick=author_tick,
        commit_tick=commit_tick,
        final_sample_coordinate=final_sample_coordinate,
        final_sample_tick=final_sample_tick,
    )


ASTAROTH_LEFT_UKEMI = ComboScenario(
    attacker_id="012",
    style_id="012",
    opener_slot=372,
    forced_contact_classification="lethal_hit",
    followup_entry_slot=341,
    followup_held_target_slot=342,
    defender_action_slot=0x8E,
    defender_local_direction="left",
    spacing=SpacingSweepPolicy(0.0, 2.0, 0.0025),
    observed_hits=("001", "005", "007", "00f", "011", "012", "014", "016", "028"),
    observed_escapes=(
        "002", "003", "006", "009", "00b", "00c", "00d", "015", "017",
        "023", "024", "030", "060", "061", "062", "064", "065",
    ),
)


SCENARIO_CHARACTERS = ASTAROTH_LEFT_UKEMI.observed_hits + ASTAROTH_LEFT_UKEMI.observed_escapes


# LuxMoveVM effect 0x13 publishes this value into the playback track's
# +0x2084 scalar.  ApplyBattleCharaMotionSlotRootMotionDirectPositionWrite
# multiplies the sampled selector-0x16 vector before subtracting the slot's
# cached local vector.  The stock side-ukemi/ground-roll helper authors 70%.
STOCK_GROUNDED_ROOT_SCALAR = 0.7


# Unsigned style-size words read from the executable's character-profile
# pointer table at 0x143E83FD0 (record +0x04).  They are physical simulation
# inputs, not outcome labels.  LuxBattleChara_InitRoundState derives the two
# +0x1B98 ratios as 2*S/(S+O); ComputeImpactForceScale then multiplies each
# fighter's state factor by the opponent-owned ratio.
NATIVE_STYLE_SIZE_WORDS = {
    "001": 71, "002": 48, "003": 53, "005": 79, "006": 55,
    "007": 70, "009": 62, "00b": 58, "00c": 63, "00d": 42,
    "00f": 57, "011": 90, "012": 90, "014": 89, "015": 72,
    "016": 39, "017": 54, "023": 48, "024": 81, "028": 50,
    "030": 35, "060": 98, "061": 76, "062": 82, "064": 65,
    "065": 80,
}


def native_body_impact_weights(attacker_id: str, defender_id: str,
                               defender_state_factor: float) -> tuple[float, float]:
    """Return the exact +0x470 BODY weights for one simulation tick."""

    attacker_size = NATIVE_STYLE_SIZE_WORDS[attacker_id]
    defender_size = NATIVE_STYLE_SIZE_WORDS[defender_id]
    size_sum = float(attacker_size + defender_size)
    defender_owned_ratio = 2.0 * defender_size / size_sum
    attacker_owned_ratio = 2.0 * attacker_size / size_sum
    return defender_owned_ratio, defender_state_factor * attacker_owned_ratio


def _resolve_reaction_motion(root: Path, defender_khd, reaction_row_id: int) -> tuple[int, int]:
    """Resolve the native standing, face-sector-zero reaction route."""

    table = parse_hit_reaction_move_id_table((root / "hdr" / "yarare.dat").read_bytes())
    row = table.row(reaction_row_id)
    if row is None:
        raise ValueError(f"reaction row {reaction_row_id} is absent")
    packed_move_id = row.base_move_ids_by_facing[0] & 0x7FFF
    slot_index = defender_khd.resolve_packed_slot(packed_move_id)
    if slot_index is None:
        raise ValueError(f"reaction move 0x{packed_move_id:04X} does not resolve")
    return defender_khd.slots[slot_index].wAnimationIndex_00, slot_index


def prove_training_route_equivalence(
    root: Path,
    scenario: ComboScenario,
    training_slot: int = 374,
) -> dict:
    """Compare authored attack cells/events without treating training input as real."""

    khd = parse_khd((root / "hdr" / f"hdr{scenario.style_id}.khd").read_bytes())
    ordinary_slot = khd.slots[scenario.opener_slot]
    training = khd.slots[training_slot]
    ordinary_cell_id = ordinary_slot.nCellBoneIndexPerVariant[0]
    training_cell_id = training.nCellBoneIndexPerVariant[0]

    def normalized_cell(cell_id: int) -> dict:
        value = asdict(khd.sections[0].entries[cell_id])
        value.pop("offset_in_file", None)
        return value

    def normalized_events(slot: int) -> list[dict]:
        result = []
        for event in khd.sections[2].event_records:
            if event.dwPackedMoveId != slot:
                continue
            value = asdict(event)
            for key in ("record_index", "byte_offset", "dwPackedMoveId", "raw"):
                value.pop(key, None)
            result.append(value)
        return result

    cells_equal = normalized_cell(ordinary_cell_id) == normalized_cell(training_cell_id)
    events_equal = normalized_events(scenario.opener_slot) == normalized_events(training_slot)
    return {
        "ordinarySlot": scenario.opener_slot,
        "ordinaryCell": ordinary_cell_id,
        "trainingSlot": training_slot,
        "trainingCell": training_cell_id,
        "attackCellsEquivalent": cells_equal,
        "moveBankEventsEquivalent": events_equal,
        "authoredEquivalent": cells_equal and events_equal,
        "trainingRouteUsedAsInput": False,
    }


def authored_geometry_probe(
    root: Path,
    cid: str,
    spacing: float,
    *,
    scenario: ComboScenario = ASTAROTH_LEFT_UKEMI,
    follow_entry_tick: int | None = None,
    b_hold_start_tick: int = 0,
    held_target_start_coordinate: int = 10,
    attack_slots: tuple[int, ...] | None = None,
    active_follow_frames: tuple[int, ...] | None = None,
    body_push_angle_turns: float = 0.25,
    attack_pose_frame_offset: int = 0,
    defender_pose_frame_offset: int = 0,
    diagnostic_reaction_pose_release_tick: int | None = None,
    diagnostic_reaction_motion_override: int | None = None,
) -> dict:
    """Evaluate the authored pose/KHit boundary before body separation.

    This function is intentionally exposed in reports as a probe, not a final
    prediction.  The reaction-motion mapping is recovered from row 1037's
    face-sector-zero route and shipped KHD slots; it is not an observed-label
    classifier.
    """
    attacker_khd = parse_khd((root / "hdr" / f"hdr{scenario.style_id}.khd").read_bytes())
    attacker_mot = parse_mot((root / "mot" / f"chr{scenario.style_id}.mot").read_bytes())
    common_mot = parse_mot((root / "mot" / "chr000.mot").read_bytes())
    opener = attacker_khd.slots[scenario.opener_slot]
    follow = attacker_khd.slots[scenario.followup_held_target_slot]
    opener_bank, opener_clip = _resolve_motion(opener.wAnimationIndex_00, attacker_mot, common_mot)
    follow_bank, follow_clip = _resolve_motion(follow.wAnimationIndex_00, attacker_mot, common_mot)
    attacker_root_scale = CHARACTERS[scenario.attacker_id][2] * CHARACTERS[scenario.attacker_id][3]
    defender_root_scale = CHARACTERS[cid][2] * CHARACTERS[cid][3]

    defender_khd = parse_khd((root / "hdr" / f"hdr{cid}.khd").read_bytes())
    defender_mot = parse_mot((root / "mot" / f"chr{cid}.mot").read_bytes())
    opener_cell_index = opener.nCellBoneIndexPerVariant[0]
    if opener_cell_index < 0:
        raise ValueError("scenario opener has no base attack cell")
    opener_cell = attacker_khd.sections[0].entries[opener_cell_index]
    opener_contact_coordinate = int(opener_cell.wI16MasterWindowStart)
    recovery_lead, recovery_open_coordinate, native_follow_entry_tick = (
        _resolve_attack_recovery_entry(
            attacker_khd, scenario.opener_slot, opener_contact_coordinate
        )
    )
    if follow_entry_tick is None:
        follow_entry_tick = native_follow_entry_tick
    continuation = _resolve_opener_continuation(
        attacker_khd, scenario.opener_slot, opener_contact_coordinate
    )
    continuation_slot = attacker_khd.slots[continuation.target_slot]
    continuation_bank, continuation_clip = _resolve_motion(
        continuation_slot.wAnimationIndex_00, attacker_mot, common_mot
    )
    if scenario.forced_contact_classification == "lethal_hit":
        reaction_row_id = int(opener_cell.wI16ReactionIdSpecialContact)
    elif scenario.forced_contact_classification == "ordinary_hit":
        reaction_row_id = int(opener_cell.wI16ReactionIdBaseContact)
    else:
        raise ValueError(
            f"unsupported forced contact classification {scenario.forced_contact_classification!r}"
        )
    reaction_packed, reaction_slot = _resolve_reaction_motion(
        root, defender_khd, reaction_row_id
    )
    authored_reaction_packed = reaction_packed
    if diagnostic_reaction_motion_override is not None:
        reaction_packed = diagnostic_reaction_motion_override & 0xFFFF
    reaction_bank, reaction_clip = _resolve_motion(reaction_packed, defender_mot, common_mot)
    reaction_slot_view = defender_khd.slots[reaction_slot]
    reaction_timeline = build_reaction_timeline(reaction_slot_view.wTotalFrames)
    ukemi_admission_tick = reaction_timeline.ukemi_commit_tick
    # ApplyHitReactionMove transitions the selected character reaction on
    # lane 1 with flStartFrame=1.0.  Its 100% descriptor blend and unit frame
    # scale make InitMotionPlayback seed the sampler at clip frame 1.
    reaction_initial_frame = 1
    # ApplyHitReactionMove seeds lane 1 at coordinate 1 on contact. Native
    # advancement keeps equality with flAnimLength active; therefore ticks
    # 1..wTotalFrames publish those same coordinates. The later
    # same-invocation CommitMoveEnd/cache reset is still tracked as unresolved,
    # but it occurs after every follow-up active tick in this scenario.
    reaction_lane_last_coordinate = int(reaction_slot_view.wTotalFrames)
    reaction_lane_last_tick = reaction_lane_last_coordinate
    reaction_endpoint_frame = reaction_lane_last_coordinate
    # The two standard-reaction drivers used by row 1037 publish physical
    # stance state[10]=6 for motions 13DB/149A (BODY type 1) and state[10]=1
    # for 1477/1478 (BODY type 0).  Slot 0x8E's concrete shared-helper path
    # writes state[10]=1 when ukemi begins, returning every defender to type 0.
    defender_reaction_body_type = 1 if reaction_packed in {0x13DB, 0x149A} else 0
    # The ordinary grounded lethal route reaches classifier-4's standard
    # reaction transaction. ComputeHitReactionParams preserves initialized
    # phase zero, unit time/XZ/Y scales, resolve-scale -1, and no opponent-ring
    # override. ApplyHitReactionMove publishes this velocity after contact;
    # the normal per-tick integrator consumes it beginning on tick one.
    (
        defender_move_velocity_x,
        _defender_move_velocity_y,
        defender_move_velocity_z,
        reaction_move_offset,
    ) = decode_reaction_move_velocity(
        opener_cell,
        reaction_phase=0,
        opponent_facing_turns=0.0,
    )

    hurt_records = parse_hit_dat((root / "hit" / f"yararehit{cid}.dat").read_bytes()).records
    attack_records = parse_hit_dat(
        (root / "hit" / f"atkhit{scenario.style_id}.dat").read_bytes()
    ).records
    attacker_body_records = tuple(
        parse_hit_dat((root / "hit" / f"bodyhit{scenario.attacker_id}.dat").read_bytes()).records
    )
    defender_body_records = tuple(
        parse_hit_dat((root / "hit" / f"bodyhit{cid}.dat").read_bytes()).records
    )
    follow_cell_index = follow.nCellBoneIndexPerVariant[0]
    if follow_cell_index < 0:
        raise ValueError("held follow-up has no base attack cell")
    follow_cell = attacker_khd.sections[0].entries[follow_cell_index]
    follow_variant_cell_index = follow.nCellBoneIndexPerVariant[1]
    follow_variant_cell = (
        attacker_khd.sections[0].entries[follow_variant_cell_index]
        if follow_variant_cell_index >= 0 else None
    )
    if active_follow_frames is None:
        active_follow_frames = tuple(
            range(follow_cell.wI16MasterWindowStart, follow_cell.wI16MasterWindowEnd + 1)
        )
    follow_cell_route: dict[int, tuple[int, object]] = {}
    active_attack_records_by_frame: dict[int, tuple[HitRecord, ...]] = {}
    attack_slots_by_frame: dict[int, tuple[int, ...]] = {}
    for frame in active_follow_frames:
        # TransitionToMove clears suppress flags +0x16EB/+0x16FE. Slot 342's
        # coordinate-15 SetActiveMoveSlot(1) therefore always switches from
        # base cell 112 to variant cell 113 on the ordinary held route.
        if (
            follow_variant_cell is not None
            and follow_variant_cell.wI16MasterWindowStart <= frame
            <= follow_variant_cell.wI16MasterWindowEnd
        ):
            cell_index, cell = follow_variant_cell_index, follow_variant_cell
        else:
            cell_index, cell = follow_cell_index, follow_cell
        frame_slots = attack_slots or tuple(
            slot for slot in range(64) if int(cell.u64SlotMask) & (1 << slot)
        )
        follow_cell_route[frame] = (cell_index, cell)
        attack_slots_by_frame[frame] = tuple(frame_slots)
        active_attack_records_by_frame[frame] = tuple(
            record
            for record in attack_records
            if record.tag == 0 and record.slot in frame_slots
        )
    hurt_bones = {record.bone_index_ue4 for record in hurt_records if record.tag == 0}
    skeleton = load_compact_collision_skeleton_from_nmd_manifest(
        root / "nmd" / "profile_overlays" / cid.upper() / "manifest.json",
        root / "profile" / f"RP_{cid.upper()}.json",
    )
    attacker_skeleton = load_compact_collision_skeleton_from_nmd_manifest(
        root / "nmd" / "profile_overlays" / scenario.attacker_id.upper() / "manifest.json",
        root / "profile" / f"RP_{scenario.attacker_id.upper()}.json",
    )
    ukemi_packed = defender_khd.slots[scenario.defender_action_slot].wAnimationIndex_00
    ukemi_bank, ukemi_clip = _resolve_motion(ukemi_packed, defender_mot, common_mot)
    # The shared grounded-action helper opens its transition/cancel window
    # before the end of slot 0x8E.  Opening that window does not itself queue
    # or commit another move.  With only the scenario's left-ukemi action
    # authored, playback therefore remains on 0x8E through its full clip.
    ukemi_total_frames = defender_khd.slots[scenario.defender_action_slot].wTotalFrames

    # Tick 0 is opener contact. Native timing maps 0x7600-recoveryLead
    # to totalFrames-recoveryLead. For slot 372 that is coordinate 57,
    # so slot 341 occupies contact+40; coordinate 56 is only the final
    # outgoing sample. The lane-1 slot-373 continuation commits at
    # contact+5 and preserves opener clip 407 through its final sample.
    # Slot 341's first crossed speed checkpoint is coordinate 6. Its
    # zero-duration effect-0x18 snap changes speed to 0.5 for one lane step;
    # GLOBAL[0x25] then resets speed to 1.0 on the next script invocation.
    # The fractional clock makes CALLCOND 0x25 sample point 10 at local tick
    # 11. IF 0x20 requires current plus ring ages 2..32 to contain B, so a
    # hold beginning no later than contact tick 19 authors lane-1 slot 342 on
    # global tick 51. Cell 112 is active at target coordinates 13..14, then
    # SetActiveMoveSlot(1) selects cell 113 at coordinates 15..16.
    sweep_values = spacing_samples(scenario.spacing, include=spacing)
    # Each sample carries accumulated native BODY displacement independently.
    # Motion decoding is shared because actor translation cannot change local
    # bone matrices or the vertical-overlap interval on a flat stage.
    sweep_corrections = {
        value: {"p1": [0.0, 0.0], "p2": [0.0, 0.0]}
        for value in sweep_values
    }
    attacker_effect_displacement = [0.0, 0.0]
    attacker_effect_speed_word = 0
    defender_separation_scale = 0.375 if defender_reaction_body_type == 1 else 1.0
    # Both row-1037 reaction entries and the concrete slot-0x8E entry execute
    # effect 0x27 with argc==1. LuxMoveVM_DispatchEffectOp therefore writes a
    # zero active mask to every live BODY node. The slot-0x8E tick route has
    # no later effect-0x27 write before held (4)[B]'s active coordinates, so
    # the native broad BODY solver has no pair to separate in this window.
    body_collision_active = False
    active_positions: dict[
        int,
        tuple[
            dict[float, tuple[tuple[float, float, float], tuple[float, float, float], dict]],
            tuple[PoseMotionLane, ...],
            tuple[MotionLaneState, ...],
        ],
    ] = {}
    attacker_body_bones = {record.bone_index_ue4 for record in attacker_body_records if record.tag == 0}
    defender_body_bones = {record.bone_index_ue4 for record in defender_body_records if record.tag == 0}
    held_timeline = simulate_held_canyon_timeline(
        entry_tick=follow_entry_tick,
        b_hold_start_tick=b_hold_start_tick,
        source_slot=scenario.followup_entry_slot,
        target_slot=scenario.followup_held_target_slot,
        source_animation_length=float(
            attacker_khd.slots[scenario.followup_entry_slot].wTotalFrames
        ),
        target_start_coordinate=float(held_target_start_coordinate),
        active_coordinates=active_follow_frames,
    )
    if held_timeline.transition_tick is None:
        raise ValueError(
            "held Canyon Creation input never authors slot 342 under the supplied hold timeline"
        )
    held_transition_tick = held_timeline.transition_tick
    source_coordinate_by_tick = {
        tick.global_tick: tick.script_coordinate for tick in held_timeline.ticks
    }
    target_tick_by_frame = {
        frame: frame - held_target_start_coordinate for frame in active_follow_frames
    }
    first_active_tick = held_transition_tick + target_tick_by_frame[min(active_follow_frames)]
    final_tick = held_transition_tick + target_tick_by_frame[max(active_follow_frames)]
    attacker_base_dx = 0.0
    attacker_base_dz = 0.0

    def attacker_lanes_at(
        tick: int,
    ) -> tuple[tuple[PoseMotionLane, ...], tuple[MotionLaneState, ...]]:
        if tick < follow_entry_tick:
            lane0_bank, lane0_clip = opener_bank, opener_clip
            lane0_motion = int(opener.wAnimationIndex_00)
            lane0_coordinate = float(opener_contact_coordinate + tick)
        else:
            lane0_bank, lane0_clip = follow_bank, follow_clip
            lane0_motion = int(
                attacker_khd.slots[scenario.followup_entry_slot].wAnimationIndex_00
            )
            lane0_coordinate = float(source_coordinate_by_tick[tick])

        lane1_active = False
        lane1_bank = continuation_bank
        lane1_clip = continuation_clip
        lane1_motion: int | None = None
        lane1_coordinate = 0.0
        if tick >= held_transition_tick:
            lane1_active = True
            lane1_bank, lane1_clip = follow_bank, follow_clip
            lane1_motion = int(follow.wAnimationIndex_00)
            lane1_coordinate = float(
                held_target_start_coordinate + tick - held_transition_tick
            )
        elif continuation.commit_tick <= tick <= continuation.final_sample_tick:
            lane1_active = True
            lane1_motion = continuation.packed_motion_id
            lane1_coordinate = float(
                continuation.target_start_coordinate
                + tick - continuation.commit_tick
            )

        lanes = (
            PoseMotionLane(
                lane0_bank.section(lane0_clip), lane0_coordinate, 1.0, True,
                lane0_clip, lane0_bank.offsets[lane0_clip],
            ),
            PoseMotionLane(None, 0.0, 0.0, False),
            PoseMotionLane(
                lane1_bank.section(lane1_clip) if lane1_active else None,
                lane1_coordinate, 1.0, lane1_active,
                lane1_clip, lane1_bank.offsets[lane1_clip],
            ),
            PoseMotionLane(None, 0.0, 0.0, False),
        )
        states = (
            MotionLaneState(0, 0, 0, lane0_motion, lane0_coordinate, 0, 1.0, True),
            MotionLaneState(1, 0, 1, None, None, 0, 0.0, False),
            MotionLaneState(
                2, 1, 0, lane1_motion,
                lane1_coordinate if lane1_active else None,
                0, 1.0 if lane1_active else 0.0, lane1_active,
            ),
            MotionLaneState(3, 1, 1, None, None, 0, 0.0, False),
        )
        return lanes, states

    def advance_attacker_direct_root(tick: int) -> None:
        nonlocal attacker_base_dx, attacker_base_dz
        if tick >= held_transition_tick:
            bank, clip = follow_bank, follow_clip
            current = float(held_target_start_coordinate + tick - held_transition_tick)
            previous = current - 1.0
            transition_tick = held_transition_tick
        elif continuation.commit_tick <= tick <= continuation.final_sample_tick:
            bank, clip = continuation_bank, continuation_clip
            current = float(
                continuation.target_start_coordinate + tick - continuation.commit_tick
            )
            previous = current - 1.0
            transition_tick = continuation.commit_tick
        elif tick >= follow_entry_tick:
            bank, clip = follow_bank, follow_clip
            current = float(source_coordinate_by_tick[tick])
            previous = float(source_coordinate_by_tick[tick - 1])
            transition_tick = follow_entry_tick
        else:
            bank, clip = opener_bank, opener_clip
            current = float(opener_contact_coordinate + tick)
            previous = current - 1.0
            transition_tick = -1
        # A newly committed full-weight direct-root lane anchors at its target
        # start coordinate. It contributes no source-to-target jump on that
        # tick; subsequent ticks consume only its incremental local delta.
        if tick == transition_tick:
            return
        dx, dz = _horizontal_delta(
            _root_at(bank, clip, current),
            _root_at(bank, clip, previous),
            0.0,
            attacker_root_scale,
        )
        attacker_base_dx += dx
        attacker_base_dz += dz

    for tick in range(1, final_tick + 1):
        advance_attacker_direct_root(tick)
        attacker_pose_lanes, attacker_lane_states = attacker_lanes_at(tick)

        # Lane scripts run in lane order and effect 0x04 writes one persistent
        # facing-relative velocity channel. Slot 341 remains live on lane 0;
        # slot 342 begins on lane 1 in the transition tick, so a lane-1 write
        # later in the same tick overrides lane 0. Physics consumes the final
        # publication after MoveVM and before pose/KHit finalization.
        if tick >= follow_entry_tick:
            source_coordinate = source_coordinate_by_tick[tick]
            source_integer = int(source_coordinate)
            if source_integer in (10, 11):
                attacker_effect_speed_word = 70
            elif source_integer == 12:
                attacker_effect_speed_word = 60
            elif source_integer >= 13:
                attacker_effect_speed_word = 0
        if tick >= held_transition_tick:
            target_integer = held_target_start_coordinate + tick - held_transition_tick
            if target_integer in (11, 12):
                attacker_effect_speed_word = 60
            elif target_integer >= 13:
                attacker_effect_speed_word = 0
        if attacker_effect_speed_word:
            attacker_effect_displacement[1] += attacker_effect_speed_word / 1000.0
        reaction_frame_now = min(max(tick, reaction_initial_frame), reaction_lane_last_coordinate)
        reaction_now_dx, reaction_now_dz = _horizontal_delta(
            _root_at(reaction_bank, reaction_clip, reaction_frame_now),
            _root_at(reaction_bank, reaction_clip, reaction_initial_frame),
            0.5,
            defender_root_scale,
        )
        if tick < ukemi_admission_tick:
            defender_bank_now, defender_clip_now = reaction_bank, reaction_clip
            defender_frame = min(
                max(tick, reaction_initial_frame), reaction_lane_last_coordinate
            )
            defender_base_dx = reaction_now_dx + defender_move_velocity_x * tick
            defender_base_dz = reaction_now_dz + defender_move_velocity_z * tick
        else:
            defender_bank_now, defender_clip_now = ukemi_bank, ukemi_clip
            defender_frame = tick - ukemi_admission_tick
            ukemi_dx, ukemi_dz = _horizontal_delta(
                _root_at(
                    ukemi_bank, ukemi_clip,
                    min(defender_frame, ukemi_total_frames - 1),
                ),
                (
                    _root_at(
                        ukemi_bank,
                        ukemi_clip,
                        min(
                            max(-1, reaction_lane_last_tick - ukemi_admission_tick),
                            ukemi_total_frames - 1,
                        ),
                    )
                    if reaction_lane_last_tick >= ukemi_admission_tick
                    else (0.0, 0.0, 0.0)
                ),
                0.5,
                defender_root_scale * STOCK_GROUNDED_ROOT_SCALAR,
            )
            if tick <= reaction_lane_last_tick:
                # Direct-root slot 0 is multiplied by (1-weight(slot 2));
                # lane-1 reaction has weight one while live.  The slot-0
                # local cache still advances, so this suppressed travel is
                # not replayed when lane 1 deactivates.
                ukemi_dx = ukemi_dz = 0.0
            defender_base_dx = (
                reaction_now_dx + ukemi_dx + defender_move_velocity_x * tick
            )
            defender_base_dz = (
                reaction_now_dz + ukemi_dz + defender_move_velocity_z * tick
            )
        reference_state = sweep_corrections[round(spacing, 12)]
        attacker_position_now = (
            attacker_base_dx + attacker_effect_displacement[0] + reference_state["p1"][0],
            0.0,
            attacker_base_dz + attacker_effect_displacement[1] + reference_state["p1"][1],
        )
        defender_position_now = (
            defender_base_dx + reference_state["p2"][0],
            0.0,
            spacing + defender_base_dz + reference_state["p2"][1],
        )
        attacker_body_pose = decode_four_lane_collision_pose(
            attacker_pose_lanes,
            attacker_skeleton,
            attacker_body_bones,
            actor_world=make_lux_world_transform(attacker_position_now, 0.0),
        )
        reaction_pose_active = (
            tick <= reaction_lane_last_tick
            and (
                diagnostic_reaction_pose_release_tick is None
                or tick < diagnostic_reaction_pose_release_tick
            )
        )
        ukemi_pose_active = tick >= ukemi_admission_tick
        defender_main_analytic = bool(
            ukemi_pose_active
            and defender_khd.slots[scenario.defender_action_slot].bMotionAFlags_07 & 0x80
        )
        if not defender_main_analytic:
            defender_body_pose = decode_four_lane_collision_pose(
                (
                    PoseMotionLane(
                        ukemi_bank.section(ukemi_clip) if ukemi_pose_active else None,
                        float(max(0, tick - ukemi_admission_tick)),
                        1.0,
                        ukemi_pose_active,
                        ukemi_clip,
                        ukemi_bank.offsets[ukemi_clip],
                    ),
                    PoseMotionLane(None, 0.0, 0.0, False),
                    PoseMotionLane(
                        reaction_bank.section(reaction_clip) if reaction_pose_active else None,
                        float(reaction_frame_now),
                        1.0,
                        reaction_pose_active,
                        reaction_clip,
                        reaction_bank.offsets[reaction_clip],
                    ),
                    PoseMotionLane(None, 0.0, 0.0, False),
                ),
                skeleton,
                defender_body_bones,
                actor_world=make_lux_world_transform(defender_position_now, 0.5),
            )
        else:
            # Preserve an explicit incomplete boundary for Seong Mi-na's
            # motion-flag 0x80 route; the authored lane remains useful for
            # diagnostics but is not promoted to native-final collision.
            defender_body_pose = decode_collision_pose(
                defender_bank_now.section(defender_clip_now), skeleton, defender_frame,
                defender_body_bones, clip_index=defender_clip_now,
                offset=defender_bank_now.offsets[defender_clip_now],
                actor_world=make_lux_world_transform(defender_position_now, 0.5),
            )
        defender_body_type = (
            defender_reaction_body_type if tick < ukemi_admission_tick else 0
        )
        # SolvePhysBodyCollision ramps type 0 by 0.125 before broad overlap,
        # caps at one, and forces type 1 to 0.375 after the broad pass.
        if defender_body_type == 0:
            defender_separation_scale = min(1.0, defender_separation_scale + 0.125)
        defender_state_factor = 0.25 if defender_body_type == 1 else 1.0
        attacker_weight, defender_weight = native_body_impact_weights(
            scenario.attacker_id, cid, defender_state_factor
        )
        total_weight = attacker_weight + defender_weight
        attacker_share = attacker_weight / total_weight
        defender_share = defender_weight / total_weight
        p1_lower, p1_upper = _body_vertical_interval(attacker_body_pose, 0)
        p2_lower, p2_upper = _body_vertical_interval(
            defender_body_pose, defender_body_type
        )
        vertical_overlap = p2_lower < p1_upper and p1_lower < p2_upper
        p1_local_root = (
            attacker_body_pose.world[1].translation[0] - attacker_position_now[0],
            attacker_body_pose.world[1].translation[2] - attacker_position_now[2],
        )
        p2_local_root = (
            defender_body_pose.world[1].translation[0] - defender_position_now[0],
            defender_body_pose.world[1].translation[2] - defender_position_now[2],
        )
        tick_positions: dict[
            float,
            tuple[tuple[float, float, float], tuple[float, float, float], dict],
        ] = {}
        for sweep_spacing, corrections in sweep_corrections.items():
            p1_before = (
                attacker_base_dx + attacker_effect_displacement[0] + corrections["p1"][0],
                attacker_base_dz + attacker_effect_displacement[1] + corrections["p1"][1],
            )
            p2_before = (
                defender_base_dx + corrections["p2"][0],
                sweep_spacing + defender_base_dz + corrections["p2"][1],
            )
            if body_collision_active:
                p1_delta, p2_delta, broad_force = solve_broad_body_from_roots(
                    (p1_before[0] + p1_local_root[0], p1_before[1] + p1_local_root[1]),
                    (p2_before[0] + p2_local_root[0], p2_before[1] + p2_local_root[1]),
                    vertical_overlap=vertical_overlap,
                    push_angle_turns=body_push_angle_turns,
                    p1_radius=0.3,
                    p2_radius=0.3,
                    separation_scale=defender_separation_scale,
                    p1_share=attacker_share,
                    p2_share=defender_share,
                )
            else:
                p1_delta = (0.0, 0.0)
                p2_delta = (0.0, 0.0)
                broad_force = 0.0
            corrections["p1"][0] += p1_delta[0]
            corrections["p1"][1] += p1_delta[1]
            corrections["p2"][0] += p2_delta[0]
            corrections["p2"][1] += p2_delta[1]
            tick_positions[sweep_spacing] = (
                (p1_before[0] + p1_delta[0], 0.0, p1_before[1] + p1_delta[1]),
                (p2_before[0] + p2_delta[0], 0.0, p2_before[1] + p2_delta[1]),
                {
                    "broadForce": broad_force,
                    "pairForce": 0.0,
                    "pairSlots": None,
                    "p1ImpactWeight": attacker_weight,
                    "p2ImpactWeight": defender_weight,
                    "p2BodyType": defender_body_type,
                    "p2SeparationScale": defender_separation_scale,
                    "verticalOverlap": vertical_overlap,
                    "bodyCollisionActive": body_collision_active,
                },
            )
        if defender_body_type == 1:
            defender_separation_scale = 0.375
        target_frame = held_target_start_coordinate + tick - held_transition_tick
        active_integer_frame = next(
            (frame for frame, frame_tick in target_tick_by_frame.items()
             if tick == held_transition_tick + frame_tick),
            None,
        )
        if tick >= held_transition_tick and active_integer_frame is not None:
            active_positions[active_integer_frame] = (
                tick_positions,
                attacker_pose_lanes,
                attacker_lane_states,
            )

    frame_results: list[dict] = []
    all_contacts: list[SphereContact] = []
    sweep_overlaps = {value: False for value in sweep_values}
    first_attack: WorldSphere | None = None
    for follow_frame in active_follow_frames:
        (
            tick_positions,
            attacker_pose_lanes,
            attacker_lane_states,
        ) = active_positions[follow_frame]
        attacker_position, defender_position, body_evidence = tick_positions[
            round(spacing, 12)
        ]
        contact_tick = held_transition_tick + target_tick_by_frame[follow_frame]
        if contact_tick < ukemi_admission_tick:
            hurt_bank, hurt_clip = reaction_bank, reaction_clip
            hurt_frame = reaction_initial_frame + contact_tick
            defender_state = "reaction"
        else:
            action_frame = contact_tick - ukemi_admission_tick
            hurt_bank, hurt_clip = ukemi_bank, ukemi_clip
            hurt_frame = min(action_frame, ukemi_total_frames - 1)
            defender_state = "left_ukemi"
        solver_state = build_pose_solver_state(
            tick=contact_tick,
            ukemi_slot=defender_khd.slots[scenario.defender_action_slot],
            ukemi_coordinate=max(0.0, float(contact_tick - ukemi_admission_tick)),
            reaction_packed_motion=reaction_packed,
            reaction_coordinate=float(min(
                max(contact_tick, reaction_initial_frame),
                reaction_lane_last_coordinate,
            )),
            reaction_lane_last_tick=reaction_lane_last_tick,
        )
        attack_pose_frame = float(follow_frame)
        active_attack_records = active_attack_records_by_frame[follow_frame]
        attack_pose_lanes = tuple(
            PoseMotionLane(
                lane.raw,
                lane.frame + attack_pose_frame_offset if lane.active else lane.frame,
                lane.weight,
                lane.active,
                lane.clip_index,
                lane.offset,
            )
            for lane in attacker_pose_lanes
        )
        attack_pose = decode_four_lane_collision_pose(
            attack_pose_lanes,
            attacker_skeleton,
            {record.bone_index_ue4 for record in active_attack_records},
            actor_world=make_lux_world_transform(attacker_position, 0.0),
        )
        reaction_pose_active = (
            contact_tick <= reaction_lane_last_tick
            and (
                diagnostic_reaction_pose_release_tick is None
                or contact_tick < diagnostic_reaction_pose_release_tick
            )
        )
        ukemi_pose_active = contact_tick >= ukemi_admission_tick
        if not solver_state.main_analytic_ik_active:
            # SolveBonePose consumes ordered slots 0..3. A full-weight lane-1
            # reaction descriptor overwrites the earlier lane-0 ukemi pose
            # while both clips continue advancing. KHit world-center
            # publication must consume this same final pose; selecting the
            # ukemi clip merely because slot 0x8E committed produced a pose
            # that contradicted the reported lane state and the BODY solve.
            hurt_pose = decode_four_lane_collision_pose(
                (
                    PoseMotionLane(
                        ukemi_bank.section(ukemi_clip) if ukemi_pose_active else None,
                        float(max(0, contact_tick - ukemi_admission_tick)),
                        1.0,
                        ukemi_pose_active,
                        ukemi_clip,
                        ukemi_bank.offsets[ukemi_clip],
                    ),
                    PoseMotionLane(None, 0.0, 0.0, False),
                    PoseMotionLane(
                        reaction_bank.section(reaction_clip)
                        if reaction_pose_active else None,
                        float(min(
                            max(contact_tick, reaction_initial_frame),
                            reaction_lane_last_coordinate,
                        )),
                        1.0,
                        reaction_pose_active,
                        reaction_clip,
                        reaction_bank.offsets[reaction_clip],
                    ),
                    PoseMotionLane(None, 0.0, 0.0, False),
                ),
                skeleton,
                hurt_bones,
                actor_world=make_lux_world_transform(defender_position, 0.5),
            )
        else:
            hurt_pose = decode_collision_pose(
                hurt_bank.section(hurt_clip), skeleton,
                hurt_frame + defender_pose_frame_offset, hurt_bones,
                clip_index=hurt_clip, offset=hurt_bank.offsets[hurt_clip],
                actor_world=make_lux_world_transform(defender_position, 0.5),
            )
        attacks = publish_spheres(
            active_attack_records,
            attack_pose,
            event_records=attacker_khd.sections[2].event_records,
            packed_move_id=scenario.followup_held_target_slot,
        )
        hurts = publish_spheres(
            hurt_records,
            hurt_pose,
            flat_terrain_height=0.0 if defender_state != "reaction" else None,
            classifier_rows_only=True,
        )
        contacts = sorted(
            (strict_sphere_contact(attack, hurt) for attack in attacks for hurt in hurts),
            key=lambda contact: contact.clearance,
        )
        for sweep_spacing, (
            sweep_attacker_position,
            sweep_defender_position,
            _sweep_body_evidence,
        ) in tick_positions.items():
            attack_delta = (
                sweep_attacker_position[0] - attacker_position[0],
                sweep_attacker_position[1] - attacker_position[1],
                sweep_attacker_position[2] - attacker_position[2],
            )
            hurt_delta = (
                sweep_defender_position[0] - defender_position[0],
                sweep_defender_position[1] - defender_position[1],
                sweep_defender_position[2] - defender_position[2],
            )
            sweep_attacks = tuple(
                WorldSphere(
                    sphere.slot,
                    sphere.bone,
                    tuple(a + b for a, b in zip(sphere.center, attack_delta)),
                    sphere.radius,
                )
                for sphere in attacks
            )
            sweep_hurts = tuple(
                WorldSphere(
                    sphere.slot,
                    sphere.bone,
                    tuple(a + b for a, b in zip(sphere.center, hurt_delta)),
                    sphere.radius,
                )
                for sphere in hurts
            )
            if any(
                strict_sphere_contact(attack, hurt).overlaps
                for attack in sweep_attacks
                for hurt in sweep_hurts
            ):
                sweep_overlaps[sweep_spacing] = True
        first_attack = first_attack or attacks[0]
        all_contacts.extend(contacts)
        frame_results.append({
            "followupFrame": follow_frame,
            "attackPoseFrame": attack_pose_frame,
            "attackCell": follow_cell_route[follow_frame][0],
            "attackSlotMask": int(follow_cell_route[follow_frame][1].u64SlotMask),
            "attackerPoseLanes": [asdict(lane) for lane in attacker_lane_states],
            "defenderState": defender_state,
            "defenderFrame": hurt_frame,
            "attackSpheres": [asdict(attack) for attack in attacks],
            "nearestHurtSphere": asdict(contacts[0]),
            "overlap": any(contact.overlaps for contact in contacts),
            "bodySeparation": body_evidence,
            "poseSolver": asdict(solver_state),
        })
    nearest = min(all_contacts, key=lambda contact: contact.clearance)
    spacing_intervals = sampled_true_intervals(
        sweep_overlaps.items(), policy_step=scenario.spacing.step
    )
    assert first_attack is not None
    observed_outcome = (
        "hit" if cid in scenario.observed_hits
        else "escape" if cid in scenario.observed_escapes
        else None
    )
    return {
        "character": cid,
        "spacing": spacing,
        "reactionMotion": f"0x{reaction_packed:04X}",
        "authoredReactionMotion": f"0x{authored_reaction_packed:04X}",
        "diagnosticReactionMotionOverride": (
            f"0x{diagnostic_reaction_motion_override & 0xFFFF:04X}"
            if diagnostic_reaction_motion_override is not None
            else None
        ),
        "reactionMoveSlot": reaction_slot,
        "reactionRowId": reaction_row_id,
        "reactionTimeline": asdict(reaction_timeline),
        "ukemiAdmissionTick": ukemi_admission_tick,
        "ukemiTickSource": "native-counter-and-same-tick-transition-order",
        "reactionFrameBeforeUkemi": reaction_endpoint_frame,
        "reactionMoveOffset": reaction_move_offset,
        "diagnosticReactionPoseReleaseTick": diagnostic_reaction_pose_release_tick,
        "attackSlotMask": int(follow_cell.u64SlotMask),
        "attackCellRoute": [
            {
                "coordinate": frame,
                "cell": follow_cell_route[frame][0],
                "slotMask": int(follow_cell_route[frame][1].u64SlotMask),
                "attackSlots": list(attack_slots_by_frame[frame]),
            }
            for frame in active_follow_frames
        ],
        "openerRecovery": {
            "contactCoordinate": opener_contact_coordinate,
            "recoveryLead": recovery_lead,
            "recoveryOpenCoordinate": recovery_open_coordinate,
            "lastOutgoingCoordinate": recovery_open_coordinate - 1,
            "followEntryTick": follow_entry_tick,
            "nativeFollowEntryTick": native_follow_entry_tick,
        },
        "openerContinuation": asdict(continuation),
        "attackerHeldTimeline": asdict(held_timeline),
        "heldTransitionTick": held_transition_tick,
        "firstActiveTick": first_active_tick,
        "ukemiMotion": f"0x{ukemi_packed:04X}",
        "ukemiTotalFrames": ukemi_total_frames,
        "poseSolverComplete": all(
            frame["poseSolver"]["complete"] for frame in frame_results
        ),
        "collisionSkeletonSource": skeleton.source,
        "attackerCollisionSkeletonSource": attacker_skeleton.source,
        "attackSphere": asdict(first_attack),
        "nearestHurtSphere": asdict(nearest),
        "contactSpacingIntervals": spacing_intervals,
        "spacingSweep": {
            "kind": "native-order-nonlinear-sampled",
            "minimum": scenario.spacing.minimum,
            "maximum": scenario.spacing.maximum,
            "step": scenario.spacing.step,
            "sampleCount": len(sweep_values),
            "overlapSampleCount": sum(sweep_overlaps.values()),
        },
        "frames": frame_results,
        "authoredGeometryOverlap": any(frame["overlap"] for frame in frame_results),
        "observedOutcome": observed_outcome,
        "predictedOutcome": None,
        "agreement": None,
        "status": "static-native-subset-probe",
    }


def render_markdown_report(payload: dict) -> str:
    scenario = payload["scenario"]
    lines = [
        "# Static combo analysis",
        "",
        f"Attacker/style: `{scenario['attacker_id']}` / `{scenario['style_id']}`  ",
        f"Route: slot `{scenario['opener_slot']}` → `{scenario['followup_entry_slot']}` → "
        f"`{scenario['followup_held_target_slot']}`; defender slot "
        f"`{scenario['defender_action_slot']}` ({scenario['defender_local_direction']}).",
        "",
        f"Status: **{'complete' if payload['complete'] else 'incomplete'}**",
        "",
        "| Character | Reaction | First active tick | Nearest clearance | Contact spacing | Observed | Prediction |",
        "|---|---:|---:|---:|---|---|---|",
    ]
    training = payload.get("trainingRouteEvidence")
    if training is not None:
        lines[7:7] = [
            f"Training evidence: slot `{training['trainingSlot']}` / cell "
            f"`{training['trainingCell']}` is authored-equivalent to ordinary slot "
            f"`{training['ordinarySlot']}` / cell `{training['ordinaryCell']}`: "
            f"**{str(training['authoredEquivalent']).lower()}**. The training route was "
            "not used as scenario input.",
            "",
        ]
    for result in payload["results"]:
        clearance = result["nearestHurtSphere"]["clearance"]
        intervals = result.get("contactSpacingIntervals", ())
        interval_text = (
            ", ".join(f"({lower:.4f}, {upper:.4f})" for lower, upper in intervals)
            if intervals else "none"
        )
        lines.append(
            f"| `{result['character']}` | `{result['reactionMotion']}` | "
            f"{result['firstActiveTick']} | {clearance:.6f} | {interval_text} | "
            f"{result.get('observedOutcome') or 'unlabelled'} | "
            f"{result.get('predictedOutcome') or 'unresolved'} |"
        )
    partition = payload.get("reactionPartition")
    if partition is not None:
        lines.extend((
            "",
            "## Reaction-selection partition",
            "",
            "Reported catches use only `0x13DB`/`0x1477`; reported escapes use only "
            "`0x149A`/`0x1478`. This is the first causal boundary, but it is not "
            "accepted as the final classifier until native KHit overlap reproduces the split.",
        ))
    if payload.get("unresolved"):
        lines.extend(("", "## Unresolved native boundaries", ""))
        lines.extend(f"- {item}" for item in payload["unresolved"])
    return "\n".join(lines) + "\n"


def summarize_reaction_partition(results: Iterable[dict]) -> dict:
    """Describe the label-free reaction-motion split, then validate labels."""

    catches = {"0x13DB", "0x1477"}
    escapes = {"0x149A", "0x1478"}
    rows = tuple(results)
    return {
        "catchReactionMotions": sorted(catches),
        "escapeReactionMotions": sorted(escapes),
        "allObservedCatchesInCatchMotions": all(
            row.get("observedOutcome") != "hit" or row["reactionMotion"] in catches
            for row in rows
        ),
        "allObservedEscapesInEscapeMotions": all(
            row.get("observedOutcome") != "escape" or row["reactionMotion"] in escapes
            for row in rows
        ),
        "acceptedAsFinalClassifier": False,
        "reason": "reaction selection is the first causal boundary; native KHit overlap is not yet reproduced",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--battle-root", type=Path, default=Path(__file__).parents[2] / "dump" / "Battle")
    parser.add_argument("--character")
    parser.add_argument("--spacing", type=float, default=1.0)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--markdown-output", type=Path)
    parser.add_argument("--scenario", type=Path)
    args = parser.parse_args()
    scenario = (
        ComboScenario.from_mapping(json.loads(args.scenario.read_text(encoding="utf-8")))
        if args.scenario else ASTAROTH_LEFT_UKEMI
    )
    scenario_characters = scenario.observed_hits + scenario.observed_escapes
    characters = (args.character.lower(),) if args.character else scenario_characters
    results = [
        authored_geometry_probe(args.battle_root, cid, args.spacing, scenario=scenario)
        for cid in characters
    ]
    payload = {
        "schemaVersion": 2,
        "scenario": asdict(scenario),
        "trainingRouteEvidence": (
            prove_training_route_equivalence(args.battle_root, scenario)
            if scenario == ASTAROTH_LEFT_UKEMI else None
        ),
        "results": results,
        "reactionPartition": summarize_reaction_partition(results),
        "complete": False,
        "unresolved": [
            "exact playback-cache and lane-end state through the overlapping lane-1 reaction and lane-0 ukemi root writers",
            "main analytic IK for Seong Mi-na's motion-flag-0x80 left-ukemi descriptor",
            "remaining controller/IK gate producers needed to prove every other final KHit matrix branch inactive",
            "the displayed contact-spacing intervals linearize spacing after one BODY solve and are diagnostic, not a certified nonlinear close-contact sweep",
        ],
    }
    rendered = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    if args.markdown_output:
        args.markdown_output.write_text(render_markdown_report(payload), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
