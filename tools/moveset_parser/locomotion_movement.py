"""Static SC6 side-step, backstep, and forward-run evidence.

The basic locomotion routes live in bucket zero of every character KHD.  The
route identity is not guessed from animation appearance: the selected slots
must call the shared 0x3000 locomotion helper with its proven mode/timing
tuple, and the 0x3004 helper with the expected direction selector.

This module reports authored open-space root movement.  The game tests attacks
against bone-attached KHit body spheres, so root travel is only one component
of evasion.  Collision, terrain, opponent push, and stage boundaries can also
shorten the observed endpoint.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import argparse
import hashlib
import json
import math
from pathlib import Path

from hgmotion_reference import decode_root_movement_frames
from lux_movement_vm import execute_velocity_helper, required_character_state_slots
from luxformats import KhdFile, MotionBankFile, parse_hit_dat, parse_khd, parse_mot
from stackvm_emulate import (
    BankScriptEvent,
    SlotTransitions,
    decode_lux_fp16_literal,
    emulate,
)


LOCOMOTION_SETUP_SCRIPT = 0x3000
LOCOMOTION_DIRECTION_SCRIPT = 0x3004

# Proven bucket-zero routes.  The helper arguments distinguish the one-shot
# tech-step path (mode 124, timing 200/20) from continuous-run progression.
BACKSTEP_SLOT = 0x25
# A one-shot backstep does not run slot 0x25 to its authored end.  The
# 0x3000 -> 0x3063 setup path maps mode 200 to release route 0x59 with timing
# base 170.  Utility 0x3061 admits that route at ``0x7600 - 20`` and authors
# it with two arguments, so DecodeVariadicStreamArgs publishes threshold zero
# and ExecuteOpStream commits the new move in the same lane tick.  The target
# begins at timing index ``170 - 20 + 1``.  That release/neutral animation is
# a different route: its cyclic idle sway is not backstep root contribution
# and must not be replaced by the unused tail of slot 0x25.
BACKSTEP_RELEASE_TARGET_SLOT = 0x59
BACKSTEP_RELEASE_TIMING_BASE = 170
BACKWALK_START_SLOT = 0x21
BACKWALK_CONTINUE_SLOT = 0x22
BACKWALK_LOOP_SLOT = 0x23
BACKWALK_STOP_SLOT = 0x24
SIDE_STEP_NEGATIVE_SLOT = 0x2B
SIDE_STEP_POSITIVE_SLOT = 0x3D
FORWARD_RUN_LOOP_SLOT = 0x1A
FORWARD_RUN_START_SLOT = 0x18
FORWARD_RUN_CONTINUE_SLOT = 0x19

UKEMI_VARIANT_SLOTS: dict[str, tuple[int, ...]] = {
    # Dispatcher predicate 0x13 is true when the opponent is nearer the
    # grounded fighter's head, and the fallback is the feet-nearer route.
    # The unused companion leaves 0x7E/0x84 and 0x80/0x82 have no MoveVM
    # transition references and are not selectable Ukemi variants.
    "forward": (0x7D, 0x83),
    "backward": (0x81, 0x7F),
    "right": (0x8D,),
    "left": (0x8E,),
}
GROUND_ROLL_VARIANT_SLOTS: dict[str, tuple[int, ...]] = {
    # These groups follow the four native input dispatchers 0x79..0x7C.
    # Pairing leaves by apparent animation direction is wrong because the
    # grounded orientation predicate deliberately swaps the local-body route.
    "forward": (0x85, 0x8B),
    "backward": (0x89, 0x87),
    "right": (0x8F,),
    "left": (0x90,),
}
_EXPECTED_UKEMI_DISPATCH: dict[int, tuple[int, ...]] = {
    0x75: (0x7D, 0x83),
    0x76: (0x81, 0x7F),
    0x77: (0x8D,),
    0x78: (0x8E,),
}
_EXPECTED_ROLL_DISPATCH: dict[int, tuple[int, ...]] = {
    0x79: (0x85, 0x8B),
    0x7A: (0x89, 0x87),
    0x7B: (0x8F,),
    0x7C: (0x90,),
}

ONE_SHOT_SETUP = (124, 200, 20)
RUN_LOOP_SETUP = (122, 212, 0)
BACKWALK_START_SETUP = (120, 212, 0)
BACKWALK_CONTINUE_SETUP = (121, 212, 0)
BACKWALK_STOP_SETUP = (128, 200, 10)

# Direction selectors passed to utility 0x3004.  The utility maps them to the
# runtime movement-direction code at ALuxBattleChara+0x19CA.
BACKSTEP_DIRECTION_SELECTOR = 4
SIDE_STEP_NEGATIVE_DIRECTION_SELECTOR = 1
SIDE_STEP_POSITIVE_DIRECTION_SELECTOR = 7
FORWARD_DIRECTION_SELECTOR = 6

# Grounded helper 0x3007 changes only the side-route transition lead for two
# stable move-table indices.  IF 0x006B reads ALuxBattleChara+0x250; native
# MapLuxFightStyleToMoveTableIndex maps Taki's fight style to 2 and Astaroth's
# to 12.  The helper changes the ordinary 10-frame lead to 6 and 1.
GROUND_SIDE_LEAD_BY_CID = {"003": 6, "012": 1}

# Nested utility scripts which can author effect 0x04.  The dispatcher writes
# facing-relative X/Z effect velocity to ALuxBattleChara+0x140/+0x148, outside
# the MOT root channel.  Both 0x30C1 and 0x30C6 select backward/forward speeds
# from authored tables; 0x30FA is the conditional grounded-movement helper.
LOCOMOTION_VELOCITY_HELPER = 0x30C1
GROUNDED_VELOCITY_HELPER = 0x30FA
BACKWALK_START_VELOCITY_HELPER = 0x30C6

# Standard fight-style/move-table indices from ELuxFightStyle.  The executable
# calls Haohmaru's style RAKSHA (0x20).  IF 0x006B compares these indices with
# ALuxBattleChara+0x250; helper 0x30C6 gives index 0x22 (Hwang) an additional
# authored 70 speed words.  Index 0x64 is a non-roster/custom-table special.
MOVE_TABLE_INDEX_BY_CID = {
    "001": 0x00, "002": 0x01, "003": 0x02, "004": 0x03,
    "005": 0x04, "006": 0x05, "007": 0x06, "00b": 0x07,
    "00c": 0x08, "00d": 0x09, "00f": 0x0A, "011": 0x0B,
    "012": 0x0C, "014": 0x0D, "015": 0x0E, "016": 0x0F,
    "023": 0x10, "024": 0x11, "062": 0x13, "064": 0x14,
    "065": 0x15, "060": 0x1C, "017": 0x1D, "030": 0x1E,
    "028": 0x1F, "061": 0x20, "022": 0x21, "009": 0x22,
}

EFFECT_VELOCITY_DIVISOR = 1000.0
BACKWALK_EFFECT_ANGLE_WORD = 180
BACKWALK_EFFECT_FIRST_FRAME = 3
STATIC_GRAPH_FRAMES = 181


CHARACTERS: dict[str, tuple[str, str, float, float]] = {
    "001": ("Mitsurugi", "Mitsurugi.webp", 1.00, 1.00),
    "002": ("Seong Mi-na", "Seong-mi-na.webp", 0.97, 1.00),
    "003": ("Taki", "Taki.webp", 1.00, 1.00),
    "004": ("Maxi", "Maxi.webp", 1.02, 1.00),
    "005": ("Voldo", "Voldo.webp", 1.05, 1.00),
    "006": ("Sophitia", "Sophitia.webp", 0.96, 1.00),
    "007": ("Siegfried", "Siegfried.webp", 1.00, 1.00),
    "009": ("Hwang", "Hwang.webp", 1.01, 1.00),
    "00b": ("Ivy", "Ivy.webp", 1.03, 1.00),
    "00c": ("Kilik", "Kilik.webp", 1.00, 1.00),
    "00d": ("Xianghua", "Xianghua.webp", 0.94, 0.99),
    "00f": ("Yoshimitsu", "Yoshimitsu.webp", 1.00, 1.00),
    "011": ("Nightmare", "Nightmare.webp", 1.04, 1.00),
    "012": ("Astaroth", "Astaroth.webp", 1.13, 1.00),
    "014": ("Cervantes", "Cervantes.webp", 1.03, 1.00),
    "015": ("Raphael", "Raphael.webp", 1.01, 1.00),
    "016": ("Talim", "Talim.webp", 0.94, 0.98),
    "017": ("Cassandra", "Cassandra.webp", 0.95, 1.00),
    "022": ("Setsuka", "Setsuka.webp", 0.98, 1.00),
    "023": ("Tira", "Tira.webp", 0.96, 1.00),
    "024": ("Zasalamel", "Zasalamel.webp", 1.05, 1.00),
    "028": ("Hilde", "Hilde.webp", 0.96, 1.00),
    "030": ("Amy", "Amy.webp", 0.915, 0.98),
    "060": ("2B", "2b.webp", 1.01, 1.00),
    "061": ("Haohmaru", "Haohmaru.webp", 1.00, 1.00),
    "062": ("Grøh", "Groh.webp", 1.00, 1.00),
    "064": ("Azwel", "Azwel.webp", 1.04, 1.00),
    "065": ("Geralt", "Geralt.webp", 1.02, 1.00),
}


@dataclass(frozen=True)
class MotionMeasurement:
    slot: int
    packed_motion_id: int
    clip_frames: int
    endpoint_metres: float
    peak_metres: float
    frame4_metres: float
    frame8_metres: float
    frame12_metres: float
    curve_metres: tuple[float, ...]
    curve_x_metres: tuple[float, ...]
    curve_z_metres: tuple[float, ...]
    effective_curve_metres: tuple[float, ...]
    effective_curve_x_metres: tuple[float, ...]
    effective_curve_z_metres: tuple[float, ...]
    effective_endpoint_metres: float
    route_distance_metres: float
    route_x_metres: float
    route_z_metres: float
    motion_route_length_frames: float
    slot_route_length_frames: float
    motion_route_frames: int
    slot_route_frames: int
    transition_lead_frames: int | None
    transition_open_frame: int | None
    last_outgoing_root_frame: int | None
    runtime_root_scalar: float
    horizontal_velocity_commands: tuple[tuple[int, int], ...]
    has_conditional_effect_velocity: bool
    conditional_effect_curve_metres: tuple[float, ...] | None
    conditional_effect_angle_word: int | None
    root_curve_complete: bool


@dataclass(frozen=True)
class BodyCollisionProfile:
    """Authored base spheres; animated world centres are deliberately absent.

    Native KHit refreshes each sphere from the current collision-pose matrix
    bank every frame.  These values characterize the static input only and
    must not be presented as an effective backstep hurtbox envelope.
    """

    sphere_count: int
    mean_base_radius: float
    max_base_radius: float
    bone_indices_ue4: tuple[int, ...]


@dataclass(frozen=True)
class LocomotionMeasurement:
    cid: str
    character: str
    image: str
    body_scale: float
    lower_scale: float
    root_scale: float
    body_collision: BodyCollisionProfile
    backstep: MotionMeasurement
    backstep_one_tap_curve_metres: tuple[float, ...]
    backstep_one_tap_endpoint_metres: float
    backstep_one_tap_last_root_frame: int
    backwalk_start: MotionMeasurement
    backwalk_start_combined_curve_metres: tuple[float, ...]
    backwalk_start_effect_angle_word: int | None
    backwalk_start_effect_speed_word: int
    backwalk_start_effect_first_frame: int | None
    backwalk_initial_metres_per_second: float
    backwalk_held_curve_metres: tuple[float, ...]
    backwalk_loop: MotionMeasurement
    backwalk_stop: MotionMeasurement
    backwalk_entry_route_frames: int
    backwalk_metres_per_second: float
    sidestep_negative: MotionMeasurement
    sidestep_positive: MotionMeasurement
    forward_run_start: MotionMeasurement
    forward_run: MotionMeasurement
    forward_run_entry_route_frames: int
    forward_run_curve_metres: tuple[float, ...]
    forward_run_effect_speed_word: int
    forward_run_horizontal_velocity_commands: tuple[tuple[int, int], ...]
    forward_run_has_conditional_effect_velocity: bool
    forward_run_root_curve_complete: bool
    forward_run_metres_per_second: float
    ukemi: dict[str, tuple[MotionMeasurement, ...]]
    ground_roll: dict[str, tuple[MotionMeasurement, ...]]


@dataclass(frozen=True)
class BackstepTiming:
    authored_frames: int
    transition_lead_frames: int
    transition_open_frame: int
    last_outgoing_root_frame: int
    release_target_slot: int
    release_target_start_frame: int


def _find_script_call(bank: KhdFile, slot: int, packed_script: int) -> BankScriptEvent:
    events = emulate(bank.slots[slot].bytecode, slot).bank_scripts
    matches = [event for event in events if event.packed_move_id == packed_script]
    if len(matches) != 1:
        raise ValueError(
            f"slot 0x{slot:X}: expected one 0x{packed_script:04X} helper call, "
            f"found {len(matches)}"
        )
    return matches[0]


def _validate_route(
    bank: KhdFile,
    slot: int,
    setup: tuple[int, int, int],
    direction_selector: int,
) -> None:
    if slot >= bank.slot_buckets[0][1]:
        raise ValueError(f"slot 0x{slot:X} is not in character bucket zero")

    setup_call = _find_script_call(bank, slot, LOCOMOTION_SETUP_SCRIPT)
    expected_setup = [LOCOMOTION_SETUP_SCRIPT, *setup]
    if setup_call.concrete_args != expected_setup:
        raise ValueError(
            f"slot 0x{slot:X}: setup fingerprint changed: "
            f"{setup_call.concrete_args} != {expected_setup}"
        )

    direction_call = _find_script_call(bank, slot, LOCOMOTION_DIRECTION_SCRIPT)
    expected_direction = [LOCOMOTION_DIRECTION_SCRIPT, direction_selector]
    if direction_call.concrete_args != expected_direction:
        raise ValueError(
            f"slot 0x{slot:X}: direction fingerprint changed: "
            f"{direction_call.concrete_args} != {expected_direction}"
        )


def backstep_timing(bank: KhdFile) -> BackstepTiming:
    """Return the authored re-entry timing for the one-shot backstep route.

    ``ONE_SHOT_SETUP[2]`` is passed to shared utility 0x3000 and later used by
    utility 0x3061 as ``0x7600 - lead``.  Native timing mapping resolves the
    0x7600 bucket relative to the lane's authored animation end frame.  This
    is the earliest frame at which the route can admit its next movement
    transition; it is not a claim that a second root-motion clip has already
    started on that frame.
    """
    _validate_route(
        bank,
        BACKSTEP_SLOT,
        ONE_SHOT_SETUP,
        BACKSTEP_DIRECTION_SELECTOR,
    )
    authored_frames = bank.slots[BACKSTEP_SLOT].total_frames
    transition_lead_frames = ONE_SHOT_SETUP[2]
    if authored_frames in (0xFFFE, 0xFFFF) or transition_lead_frames > authored_frames:
        raise ValueError(
            "backstep route no longer has a finite, valid early-transition window: "
            f"frames={authored_frames}, lead={transition_lead_frames}"
        )
    return BackstepTiming(
        authored_frames=authored_frames,
        transition_lead_frames=transition_lead_frames,
        transition_open_frame=authored_frames - transition_lead_frames,
        # The transition is committed inside ExecuteOpStream before its
        # pre-advance playback-frame latch and later root-motion evaluation.
        # Therefore the outgoing clip's sample immediately before the open
        # frame is the last one that can contribute to a one-tap route.
        last_outgoing_root_frame=authored_frames - transition_lead_frames - 1,
        release_target_slot=BACKSTEP_RELEASE_TARGET_SLOT,
        release_target_start_frame=(
            BACKSTEP_RELEASE_TIMING_BASE - transition_lead_frames + 1
        ),
    )


def _grounded_transition_lead(cid: str, bank: KhdFile, slot: int) -> int:
    helper = _find_script_call(bank, slot, 0x3007)
    args = helper.concrete_args
    if len(args) != 5 or args[0] != 0x3007:
        raise ValueError(
            f"slot 0x{slot:X}: malformed grounded helper arguments {args}"
        )
    authored_lead = args[3]
    if slot in {0x8D, 0x8E, 0x8F, 0x90}:
        return GROUND_SIDE_LEAD_BY_CID.get(cid, authored_lead)
    return authored_lead


def validate_locomotion_routes(bank: KhdFile) -> None:
    """Reject data that no longer matches the proven basic-route identities."""
    _validate_route(bank, BACKSTEP_SLOT, ONE_SHOT_SETUP, BACKSTEP_DIRECTION_SELECTOR)
    _validate_route(
        bank,
        BACKWALK_START_SLOT,
        BACKWALK_START_SETUP,
        BACKSTEP_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        BACKWALK_CONTINUE_SLOT,
        BACKWALK_CONTINUE_SETUP,
        BACKSTEP_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        BACKWALK_LOOP_SLOT,
        RUN_LOOP_SETUP,
        BACKSTEP_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        BACKWALK_STOP_SLOT,
        BACKWALK_STOP_SETUP,
        BACKSTEP_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        SIDE_STEP_NEGATIVE_SLOT,
        ONE_SHOT_SETUP,
        SIDE_STEP_NEGATIVE_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        SIDE_STEP_POSITIVE_SLOT,
        ONE_SHOT_SETUP,
        SIDE_STEP_POSITIVE_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        FORWARD_RUN_START_SLOT,
        BACKWALK_START_SETUP,
        FORWARD_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        FORWARD_RUN_CONTINUE_SLOT,
        BACKWALK_CONTINUE_SETUP,
        FORWARD_DIRECTION_SELECTOR,
    )
    _validate_route(
        bank,
        FORWARD_RUN_LOOP_SLOT,
        RUN_LOOP_SETUP,
        FORWARD_DIRECTION_SELECTOR,
    )
    for dispatcher, expected in {
        **_EXPECTED_UKEMI_DISPATCH,
        **_EXPECTED_ROLL_DISPATCH,
    }.items():
        actual = tuple(
            event.next_move_slot
            for event in emulate(bank.slots[dispatcher].bytecode, dispatcher).transitions
        )
        if actual != expected:
            raise ValueError(
                f"grounded dispatcher 0x{dispatcher:X}: expected {expected}, got {actual}"
            )


def _resolve_motion_bank(
    packed_motion_id: int,
    character_motion: MotionBankFile,
    common_motion: MotionBankFile,
) -> tuple[MotionBankFile, int]:
    bank_index = (packed_motion_id >> 12) & 0xF
    clip_index = packed_motion_id & 0x7FF
    if bank_index == 0:
        bank = character_motion
    elif bank_index == 1:
        bank = common_motion
    else:
        raise ValueError(
            f"unsupported locomotion motion bank {bank_index} in 0x{packed_motion_id:04X}"
        )
    if clip_index >= bank.count:
        raise ValueError(
            f"motion 0x{packed_motion_id:04X} resolves past bank count {bank.count}"
        )
    return bank, clip_index


def _distance(frame: object) -> float:
    return math.hypot(frame.cumulative_x, frame.cumulative_z)


def _at_frame(frames: list, frame_index: int) -> float:
    return _distance(frames[min(frame_index, len(frames) - 1)])


def _horizontal_velocity_commands(
    script: SlotTransitions, slot: int
) -> tuple[tuple[int, int], ...]:
    commands: list[tuple[int, int]] = []
    for effect in script.effects:
        if effect.opcode != 0x04:
            continue
        args = effect.concrete_args
        if len(args) != 3 or args[1] is None or args[2] is None:
            raise ValueError(
                f"slot 0x{slot:X}: indirect or malformed horizontal effect velocity "
                "cannot be described"
            )
        commands.append((args[1], args[2]))
    return tuple(commands)


def _nested_effect_velocity_possible(script: SlotTransitions) -> bool:
    """Return whether a reachable nested helper can write effect velocity.

    This is deliberately conservative for branch-dependent helpers.  The
    command is not folded into distance until its runtime predicates and
    admission frames are known.
    """
    for call in script.bank_scripts:
        args = call.concrete_args
        if call.packed_move_id == GROUNDED_VELOCITY_HELPER:
            return True
        if call.packed_move_id == BACKWALK_START_VELOCITY_HELPER:
            return True
        if call.packed_move_id == LOCOMOTION_VELOCITY_HELPER:
            if len(args) > 1 and args[1] in (
                BACKSTEP_DIRECTION_SELECTOR,
                FORWARD_DIRECTION_SELECTOR,
            ):
                return True
    return False


def _route_horizontal_velocity_call(
    bank: KhdFile, script: SlotTransitions
) -> BankScriptEvent:
    """Resolve the authored nested helper by behavior, not a guessed slot ID.

    A route's horizontal-velocity helper is the single direct nested script
    whose reachable effect stream contains native effect 0x04 or its 0x06
    clear.  Packed helper IDs are character-bank addresses and are therefore
    evidence, not stable semantic names.
    """
    candidates: list[BankScriptEvent] = []
    for call in script.bank_scripts:
        packed = call.packed_move_id
        # ExecuteBankSlotScript passes every word after the packed ID as the
        # nested local frame.  The horizontal helper ABI has exactly one
        # local direction operand; the four-word 0x3000 route initializer can
        # also reach effect 0x04 and must not be confused with it.
        if packed is None or len(call.concrete_args) != 2:
            continue
        helper_slot = bank.resolve_packed_slot(packed)
        if helper_slot is None or bank.slots[helper_slot].bytecode is None:
            continue
        helper = emulate(bank.slots[helper_slot].bytecode, helper_slot)
        if any(effect.opcode in (0x04, 0x06) for effect in helper.effects):
            candidates.append(call)
    if len(candidates) != 1:
        packed = [call.packed_move_id for call in candidates]
        raise ValueError(
            "route must contain exactly one semantically identified "
            f"horizontal-velocity helper; found {packed}"
        )
    return candidates[0]


def _base_locomotion_effect_speed_word(
    cid: str,
    bank: KhdFile,
    script: SlotTransitions,
    direction_selector: int,
) -> int:
    """Execute the route's authored helper for the ordinary stat profile.

    This deliberately returns zero when the selected branch performs no
    effect-velocity write.  Inventing a table result in that case was the old
    model's central error for customized helpers such as Voldo's.
    """
    call = _route_horizontal_velocity_call(bank, script)
    args = call.concrete_args
    if call.packed_move_id is None or len(args) != 2 or args[1] != direction_selector:
        raise ValueError(
            f"{cid}: helper 0x{call.packed_move_id:04X} direction changed: {args}"
        )
    state = execute_velocity_helper(
        bank,
        call.packed_move_id,
        direction_selector=direction_selector,
        move_table_index=MOVE_TABLE_INDEX_BY_CID[cid],
        frame=3.0,
        # Compatibility lane for the existing page: this is now an explicit
        # all-zero authored-state profile.  The context explorer will expose
        # every nonzero authored branch separately; none are averaged here.
        chara_state_shorts={
            index: 0
            for index in required_character_state_slots(bank, call.packed_move_id)
        },
    )
    writes = [effect for effect in state.effects if effect.opcode == 0x04]
    if not writes:
        return 0
    if direction_selector == BACKSTEP_DIRECTION_SELECTOR and state.effect_angle_word != 180:
        raise ValueError(
            f"{cid}: backwalk helper authored unexpected angle {state.effect_angle_word}"
        )
    if direction_selector == FORWARD_DIRECTION_SELECTOR and state.effect_angle_word != 0:
        raise ValueError(
            f"{cid}: forward helper authored unexpected angle {state.effect_angle_word}"
        )
    return state.effect_speed_word


def _base_backwalk_effect_speed_word(
    cid: str, bank: KhdFile, script: SlotTransitions
) -> int:
    """Compatibility wrapper for tests and page-generation callers."""
    return _base_locomotion_effect_speed_word(
        cid, bank, script, BACKSTEP_DIRECTION_SELECTOR
    )


def _combine_root_and_delayed_effect_velocity(
    root_curve: tuple[float, ...], speed_word: int, first_frame: int
) -> tuple[float, ...]:
    """Add the native base-integrator channel to a cumulative root curve.

    0x30C6 uses timing window [0x7FFF, 2].  Native IF 0x0008 treats 0x7FFF
    as an unbounded lower limit, so frames 0..2 clear effect velocity and
    frame 3 onward publish effect 0x04.  IntegratePhysics consumes the newly
    published velocity in the same tick, before direct MOT root publication.
    """
    per_frame = speed_word / EFFECT_VELOCITY_DIVISOR
    return tuple(
        round(root + max(0, frame - first_frame + 1) * per_frame, 6)
        for frame, root in enumerate(root_curve)
    )


def _effect_velocity_vector(angle_word: int, speed_word: int) -> tuple[float, float]:
    turns = angle_word / 360.0
    radians = turns * math.tau
    speed = speed_word / EFFECT_VELOCITY_DIVISOR
    return math.sin(radians) * speed, math.cos(radians) * speed


def _motion_descriptor(
    slot: object, descriptor_index: int
) -> tuple[int, int, int, float]:
    if descriptor_index == 0:
        return (
            slot.wAnimationIndex_00,
            slot.nMotionAStartFrame_02,
            slot.nMotionAEndFrame_04,
            slot.flMotionABlendHundredths_0C,
        )
    return (
        slot.wMotionBId_10,
        slot.nMotionBStartFrame_12,
        slot.nMotionBEndFrame_14,
        slot.flMotionBBlendHundredths_1C,
    )


def _descriptor_route_frames(
    slot: object,
    descriptor_index: int,
    character_motion: MotionBankFile,
    common_motion: MotionBankFile,
) -> tuple[float, float, int, int]:
    """Mirror InitMotionPlayback's segment/rate duration for one descriptor."""
    packed_motion_id, start_frame, end_frame, blend_hundredths = _motion_descriptor(
        slot, descriptor_index
    )
    if packed_motion_id == 0xFFFF:
        return 0, 0.0, start_frame, end_frame
    motion_bank, clip_index = _resolve_motion_bank(
        packed_motion_id, character_motion, common_motion
    )
    clip_frames = int.from_bytes(motion_bank.section(clip_index)[:2], "little")
    segment_end = end_frame if 0 < end_frame <= clip_frames else clip_frames
    segment_start = max(start_frame, 0)
    span = segment_end - segment_start
    blend = blend_hundredths / 100.0
    if span <= 0 or blend <= 0.0 or blend_hundredths == -2.0:
        raise ValueError(
            f"slot 0x{slot.slot_index:X} descriptor {descriptor_index}: "
            f"unsupported segment/rate span={span}, blend={blend_hundredths}"
        )
    # flAnimLength is a float in the native lane.  Do not round it: several
    # authored descriptors (Hilde's backwalk stop is one) end between service
    # ticks.  Callers keep the exact duration and separately derive the count
    # of observable integer-tick samples.
    return span / blend, blend, start_frame, segment_end


def _sample_root_vector(
    frames: list,
    sample_frame: float,
    base_frame: float,
    applied_root_scale: float,
) -> tuple[float, float]:
    def sample(position: float) -> tuple[float, float]:
        clamped = min(max(position, 0.0), float(len(frames) - 1))
        lo = int(math.floor(clamped))
        hi = min(lo + 1, len(frames) - 1)
        t = clamped - lo
        x = frames[lo].cumulative_x + (frames[hi].cumulative_x - frames[lo].cumulative_x) * t
        z = frames[lo].cumulative_z + (frames[hi].cumulative_z - frames[lo].cumulative_z) * t
        return x, z

    base_x, base_z = sample(base_frame)
    x, z = sample(sample_frame)
    return (
        (x - base_x) * applied_root_scale,
        (z - base_z) * applied_root_scale,
    )


def _sample_root_distance(
    frames: list,
    sample_frame: float,
    base_frame: float,
    applied_root_scale: float,
) -> float:
    x, z = _sample_root_vector(
        frames, sample_frame, base_frame, applied_root_scale
    )
    return math.hypot(x, z)


def _measure_slot(
    bank: KhdFile,
    slot: int,
    character_motion: MotionBankFile,
    common_motion: MotionBankFile,
    root_scale: float,
    transition_lead_frames: int | None = None,
) -> MotionMeasurement:
    script = emulate(bank.slots[slot].bytecode, slot)
    scalar_values: list[float] = []
    for effect in script.effects:
        args = effect.concrete_args
        if effect.opcode == 0x13:
            if len(args) != 2 or args[1] is None:
                raise ValueError(
                    f"slot 0x{slot:X}: indirect or lane-masked root-motion scalar "
                    "cannot be measured"
                )
            scalar_values.append(decode_lux_fp16_literal(args[1]) / 100.0)
    horizontal_velocity_commands = _horizontal_velocity_commands(script, slot)

    unique_scalars = set(scalar_values)
    if len(unique_scalars) > 1:
        raise ValueError(
            f"slot 0x{slot:X}: branch-dependent root-motion scalars are not supported: "
            f"{sorted(unique_scalars)}"
        )
    runtime_root_scalar = unique_scalars.pop() if unique_scalars else 1.0
    applied_root_scale = root_scale * runtime_root_scalar

    slot_view = bank.slots[slot]
    packed_motion_id = slot_view.wAnimationIndex_00
    motion_bank, clip_index = _resolve_motion_bank(
        packed_motion_id, character_motion, common_motion
    )
    clip, frames, _reason = decode_root_movement_frames(
        motion_bank.section(clip_index), clip_index, motion_bank.offsets[clip_index]
    )
    if len(frames) < 2:
        raise ValueError(f"slot 0x{slot:X}: motion 0x{packed_motion_id:04X} has no step")
    if clip.flags & 0x0C:
        raise ValueError(
            f"slot 0x{slot:X}: half/quarter-speed MOT flag 0x{clip.flags & 0x0C:X} "
            "requires a separately characterized logical sampler"
        )
    lane_speed = slot_view.playback_speed_scalar
    if lane_speed <= 0.0:
        raise ValueError(f"slot 0x{slot:X}: nonpositive lane playback speed {lane_speed}")
    motion_route_lane_length, motion_rate, motion_start, _motion_end = _descriptor_route_frames(
        slot_view, 0, character_motion, common_motion
    )
    logical_motion_length = motion_route_lane_length / lane_speed
    logical_motion_frames = max(1, math.ceil(logical_motion_length - 0.000001))
    if slot_view.total_frames == 0xFFFE:
        descriptor_durations = [
            _descriptor_route_frames(slot_view, descriptor, character_motion, common_motion)[0]
            for descriptor in (0, 1)
            if _motion_descriptor(slot_view, descriptor)[0] != 0xFFFF
        ]
        slot_route_lane_frames = max(descriptor_durations)
    elif slot_view.total_frames == 0xFFFF:
        raise ValueError(f"slot 0x{slot:X}: invalid total-frame sentinel 0xFFFF")
    else:
        slot_route_lane_frames = slot_view.total_frames
    slot_route_length = slot_route_lane_frames / lane_speed
    slot_route_frames = max(1, math.ceil(slot_route_length - 0.000001))

    curve_vectors = tuple(
        _sample_root_vector(
            frames,
            motion_start + frame * motion_rate * lane_speed,
            motion_start,
            applied_root_scale,
        )
        for frame in range(logical_motion_frames)
    )
    curve_x = tuple(round(value[0], 6) for value in curve_vectors)
    curve_z = tuple(round(value[1], 6) for value in curve_vectors)
    curve = tuple(round(math.hypot(*value), 6) for value in curve_vectors)
    route_x, route_z = _sample_root_vector(
        frames,
        motion_start + logical_motion_length * motion_rate * lane_speed,
        motion_start,
        applied_root_scale,
    )
    route_x, route_z = round(route_x, 6), round(route_z, 6)
    route_distance_metres = round(math.hypot(route_x, route_z), 6)
    transition_open_frame: int | None = None
    last_outgoing_root_frame: int | None = None
    effective_curve = curve
    effective_curve_x = curve_x
    effective_curve_z = curve_z
    if transition_lead_frames is not None:
        transition_open_tick = (
            slot_route_lane_frames - transition_lead_frames
        ) / lane_speed
        transition_open_frame = round(transition_open_tick)
        if not math.isclose(
            transition_open_tick, transition_open_frame, abs_tol=0.00001
        ):
            raise ValueError(
                f"slot 0x{slot:X}: transition opens between service ticks at "
                f"{transition_open_tick}; route requires native admission tracing"
            )
        last_outgoing_root_frame = transition_open_frame - 1
        if not 0 <= last_outgoing_root_frame < len(curve):
            raise ValueError(
                f"slot 0x{slot:X}: invalid handoff frame {transition_open_frame} "
                f"for {len(curve)} motion samples"
            )
        effective_curve = tuple(
            curve[min(frame, last_outgoing_root_frame)] for frame in range(len(curve))
        )
        effective_curve_x = tuple(
            curve_x[min(frame, last_outgoing_root_frame)] for frame in range(len(curve_x))
        )
        effective_curve_z = tuple(
            curve_z[min(frame, last_outgoing_root_frame)] for frame in range(len(curve_z))
        )
    has_nested_velocity = _nested_effect_velocity_possible(script)
    grounded_calls = [
        call for call in script.bank_scripts
        if call.packed_move_id == GROUNDED_VELOCITY_HELPER
    ]
    conditional_curve: tuple[float, ...] | None = None
    conditional_angle: int | None = None
    if grounded_calls:
        if len(grounded_calls) != 1:
            raise ValueError(f"slot 0x{slot:X}: multiple grounded velocity helpers")
        args = grounded_calls[0].concrete_args
        if len(args) != 2 or args[1] is None:
            raise ValueError(f"slot 0x{slot:X}: malformed grounded velocity helper {args}")
        conditional_angle = args[1]
        velocity_x, velocity_z = _effect_velocity_vector(conditional_angle, 90)
        cumulative_x = cumulative_z = 0.0
        values: list[float] = []
        for frame in range(len(effective_curve)):
            admitted = last_outgoing_root_frame is None or frame <= last_outgoing_root_frame
            if admitted and 4 <= frame <= 22:
                cumulative_x += velocity_x
                cumulative_z += velocity_z
            values.append(round(math.hypot(
                effective_curve_x[frame] + cumulative_x,
                effective_curve_z[frame] + cumulative_z,
            ), 6))
        conditional_curve = tuple(values)
    return MotionMeasurement(
        slot=slot,
        packed_motion_id=packed_motion_id,
        clip_frames=clip.frame_count,
        endpoint_metres=route_distance_metres,
        peak_metres=max((*curve, route_distance_metres)),
        frame4_metres=curve[min(4, len(curve) - 1)],
        frame8_metres=curve[min(8, len(curve) - 1)],
        frame12_metres=curve[min(12, len(curve) - 1)],
        curve_metres=curve,
        curve_x_metres=curve_x,
        curve_z_metres=curve_z,
        effective_curve_metres=effective_curve,
        effective_curve_x_metres=effective_curve_x,
        effective_curve_z_metres=effective_curve_z,
        effective_endpoint_metres=effective_curve[-1],
        route_distance_metres=route_distance_metres,
        route_x_metres=route_x,
        route_z_metres=route_z,
        motion_route_length_frames=logical_motion_length,
        slot_route_length_frames=slot_route_length,
        motion_route_frames=logical_motion_frames,
        slot_route_frames=slot_route_frames,
        transition_lead_frames=transition_lead_frames,
        transition_open_frame=transition_open_frame,
        last_outgoing_root_frame=last_outgoing_root_frame,
        runtime_root_scalar=runtime_root_scalar,
        horizontal_velocity_commands=horizontal_velocity_commands,
        has_conditional_effect_velocity=has_nested_velocity,
        conditional_effect_curve_metres=conditional_curve,
        conditional_effect_angle_word=conditional_angle,
        root_curve_complete=(
            not horizontal_velocity_commands
            and (not has_nested_velocity or conditional_curve is not None)
        ),
    )


def _held_route_root_vector(
    start: MotionMeasurement,
    loop: MotionMeasurement,
    entry_frames: int,
    frame: int,
) -> tuple[float, float]:
    if frame < entry_frames:
        index = min(frame, len(start.curve_x_metres) - 1)
        return start.curve_x_metres[index], start.curve_z_metres[index]
    start_index = min(entry_frames - 1, len(start.curve_x_metres) - 1)
    elapsed = frame - entry_frames
    complete_loops, loop_frame = divmod(elapsed, loop.motion_route_frames)
    loop_index = min(loop_frame, len(loop.curve_x_metres) - 1)
    return (
        start.curve_x_metres[start_index]
        + complete_loops * loop.route_x_metres
        + loop.curve_x_metres[loop_index],
        start.curve_z_metres[start_index]
        + complete_loops * loop.route_z_metres
        + loop.curve_z_metres[loop_index],
    )


def _build_backwalk_held_curve(
    start: MotionMeasurement,
    loop: MotionMeasurement,
    entry_frames: int,
    speed_word: int,
) -> tuple[float, ...]:
    velocity_x, velocity_z = _effect_velocity_vector(180, speed_word)
    effect_x = effect_z = 0.0
    output: list[float] = []
    for frame in range(STATIC_GRAPH_FRAMES):
        # 0x30C1/0x30C6 clears through inclusive animation frame two and
        # publishes the selected velocity from frame three.  Continue/loop
        # do not overwrite it, so the native integrator keeps consuming it.
        if frame >= 3:
            effect_x += velocity_x
            effect_z += velocity_z
        root_x, root_z = _held_route_root_vector(start, loop, entry_frames, frame)
        output.append(round(math.hypot(root_x + effect_x, root_z + effect_z), 6))
    return tuple(output)


def _build_forward_run_curve(
    cid: str,
    start: MotionMeasurement,
    loop: MotionMeasurement,
    entry_frames: int,
    speed_word: int,
    direct_commands: tuple[tuple[int, int], ...],
) -> tuple[float, ...]:
    # These are the only direct horizontal commands present in the 28 shipped
    # route scripts.  Keeping the exact fingerprints beside their timing
    # prevents an authored change from silently using the wrong schedule.
    expected_direct = {
        "00d": ((0, 115),),
        "061": ((0, 20), (0, 56), (0, 38)),
    }.get(cid, ())
    if direct_commands != expected_direct:
        raise ValueError(
            f"{cid}: forward direct-velocity fingerprint changed: "
            f"{direct_commands} != {expected_direct}"
        )

    velocity_angle = 0
    current_speed = 0
    effect_x = effect_z = 0.0
    output: list[float] = []
    loop_frames = loop.motion_route_frames
    for frame in range(STATIC_GRAPH_FRAMES):
        if frame <= 2:
            current_speed = 0
        elif frame == 3:
            current_speed = speed_word

        if cid == "061" and 11 <= frame < entry_frames:
            current_speed = 20

        if frame >= entry_frames:
            loop_index, loop_frame = divmod(frame - entry_frames, loop_frames)
            # The loop-end script clears effect velocity before the next
            # same-slot entry.  Xianghua/Haohmaru then author a replacement;
            # other characters remain on MOT root alone after loop one.
            if loop_frame == 0:
                if loop_index > 0:
                    current_speed = 0
                if cid == "00d":
                    current_speed = 115
                elif cid == "061":
                    current_speed = 56
            if cid == "061" and loop_frame == 1:
                current_speed = 38

        velocity_x, velocity_z = _effect_velocity_vector(
            velocity_angle, current_speed
        )
        effect_x += velocity_x
        effect_z += velocity_z
        root_x, root_z = _held_route_root_vector(start, loop, entry_frames, frame)
        output.append(round(math.hypot(root_x + effect_x, root_z + effect_z), 6))
    return tuple(output)


def analyze_character(
    cid: str,
    khd_path: Path,
    mot_path: Path,
    common_mot_path: Path,
    bodyhit_path: Path,
) -> LocomotionMeasurement:
    name, image, body_scale, lower_scale = CHARACTERS[cid]
    root_scale = body_scale * lower_scale
    bank = parse_khd(khd_path.read_bytes())
    character_motion = parse_mot(mot_path.read_bytes())
    common_motion = parse_mot(common_mot_path.read_bytes())
    body_spheres = [
        record
        for record in parse_hit_dat(bodyhit_path.read_bytes()).records
        if record.tag == 0
    ]
    if not body_spheres:
        raise ValueError(f"{bodyhit_path}: no KHit sphere records")
    body_collision = BodyCollisionProfile(
        sphere_count=len(body_spheres),
        mean_base_radius=sum(record.radius for record in body_spheres)
        / len(body_spheres),
        max_base_radius=max(record.radius for record in body_spheres),
        bone_indices_ue4=tuple(record.bone_index_ue4 for record in body_spheres),
    )
    validate_locomotion_routes(bank)

    backstep = _measure_slot(
        bank,
        BACKSTEP_SLOT,
        character_motion,
        common_motion,
        root_scale,
        transition_lead_frames=ONE_SHOT_SETUP[2],
    )
    backstep_route_timing = backstep_timing(bank)
    backstep_release_script = emulate(
        bank.slots[backstep_route_timing.release_target_slot].bytecode,
        backstep_route_timing.release_target_slot,
    )
    if (
        bank.slots[backstep_route_timing.release_target_slot].total_frames != 0xFFFE
        or _horizontal_velocity_commands(
            backstep_release_script, backstep_route_timing.release_target_slot
        )
    ):
        raise ValueError(
            f"{name}: backstep release route 0x{backstep_route_timing.release_target_slot:X} "
            "is no longer the proven automatic-length route without authored velocity commands"
        )
    last_root_frame = backstep_route_timing.last_outgoing_root_frame
    if backstep.last_outgoing_root_frame != last_root_frame:
        raise ValueError(
            f"{name}: parser/native backstep handoff disagreement: "
            f"{backstep.last_outgoing_root_frame} != {last_root_frame}"
        )
    backstep_one_tap_curve_metres = backstep.effective_curve_metres
    backwalk_start = _measure_slot(
        bank, BACKWALK_START_SLOT, character_motion, common_motion, root_scale
    )
    backwalk_start_script = emulate(
        bank.slots[BACKWALK_START_SLOT].bytecode, BACKWALK_START_SLOT
    )
    backwalk_start_effect_speed_word = _base_backwalk_effect_speed_word(
        cid, bank, backwalk_start_script
    )
    if backwalk_start_effect_speed_word:
        backwalk_start_combined_curve_metres = (
            _combine_root_and_delayed_effect_velocity(
                backwalk_start.curve_metres,
                backwalk_start_effect_speed_word,
                BACKWALK_EFFECT_FIRST_FRAME,
            )
        )
        backwalk_start_effect_angle_word: int | None = BACKWALK_EFFECT_ANGLE_WORD
        backwalk_start_effect_first_frame: int | None = BACKWALK_EFFECT_FIRST_FRAME
    else:
        backwalk_start_combined_curve_metres = backwalk_start.curve_metres
        backwalk_start_effect_angle_word = None
        backwalk_start_effect_first_frame = None
    backwalk_continue = _measure_slot(
        bank, BACKWALK_CONTINUE_SLOT, character_motion, common_motion, root_scale
    )
    backwalk_loop = _measure_slot(
        bank, BACKWALK_LOOP_SLOT, character_motion, common_motion, root_scale
    )
    backwalk_stop = _measure_slot(
        bank, BACKWALK_STOP_SLOT, character_motion, common_motion, root_scale
    )
    backwalk_entry_route_frames = backwalk_continue.slot_route_frames
    backwalk_held_curve_metres = _build_backwalk_held_curve(
        backwalk_start,
        backwalk_loop,
        backwalk_entry_route_frames,
        backwalk_start_effect_speed_word,
    )
    backwalk_initial_metres_per_second = (
        backwalk_held_curve_metres[backwalk_entry_route_frames - 1]
        / backwalk_entry_route_frames
        * 60.0
    )
    backwalk_metres_per_second = (
        backwalk_held_curve_metres[-1] - backwalk_held_curve_metres[-61]
    )
    sidestep_negative = _measure_slot(
        bank,
        SIDE_STEP_NEGATIVE_SLOT,
        character_motion,
        common_motion,
        root_scale,
        transition_lead_frames=ONE_SHOT_SETUP[2],
    )
    sidestep_positive = _measure_slot(
        bank,
        SIDE_STEP_POSITIVE_SLOT,
        character_motion,
        common_motion,
        root_scale,
        transition_lead_frames=ONE_SHOT_SETUP[2],
    )
    forward_run = _measure_slot(
        bank, FORWARD_RUN_LOOP_SLOT, character_motion, common_motion, root_scale
    )
    forward_run_start = _measure_slot(
        bank, FORWARD_RUN_START_SLOT, character_motion, common_motion, root_scale
    )
    forward_run_continue = _measure_slot(
        bank, FORWARD_RUN_CONTINUE_SLOT, character_motion, common_motion, root_scale
    )
    forward_run_metres_per_second = (
        forward_run.route_distance_metres
        / forward_run.motion_route_length_frames
        * 60.0
    )
    forward_run_scripts = (
        emulate(bank.slots[FORWARD_RUN_CONTINUE_SLOT].bytecode, FORWARD_RUN_CONTINUE_SLOT),
        emulate(bank.slots[FORWARD_RUN_LOOP_SLOT].bytecode, FORWARD_RUN_LOOP_SLOT),
    )
    forward_run_horizontal_velocity_commands = tuple(
        command
        for slot, script in zip(
            (FORWARD_RUN_CONTINUE_SLOT, FORWARD_RUN_LOOP_SLOT),
            forward_run_scripts,
        )
        for command in _horizontal_velocity_commands(script, slot)
    )
    forward_start_script = emulate(
        bank.slots[FORWARD_RUN_START_SLOT].bytecode, FORWARD_RUN_START_SLOT
    )
    forward_run_effect_speed_word = _base_locomotion_effect_speed_word(
        cid, bank, forward_start_script, FORWARD_DIRECTION_SELECTOR
    )
    forward_run_curve_metres = _build_forward_run_curve(
        cid,
        forward_run_start,
        forward_run,
        forward_run_continue.slot_route_frames,
        forward_run_effect_speed_word,
        forward_run_horizontal_velocity_commands,
    )
    forward_run_metres_per_second = (
        forward_run_curve_metres[-1] - forward_run_curve_metres[-61]
    )
    ukemi = {}
    for direction, slots in UKEMI_VARIANT_SLOTS.items():
        ukemi[direction] = tuple(
            _measure_slot(
                bank,
                slot,
                character_motion,
                common_motion,
                root_scale,
                transition_lead_frames=_grounded_transition_lead(cid, bank, slot),
            )
            for slot in slots
        )
    ground_roll = {}
    for direction, slots in GROUND_ROLL_VARIANT_SLOTS.items():
        ground_roll[direction] = tuple(
            _measure_slot(
                bank,
                slot,
                character_motion,
                common_motion,
                root_scale,
                transition_lead_frames=_grounded_transition_lead(cid, bank, slot),
            )
            for slot in slots
        )
    return LocomotionMeasurement(
        cid=cid,
        character=name,
        image=image,
        body_scale=body_scale,
        lower_scale=lower_scale,
        root_scale=root_scale,
        body_collision=body_collision,
        backstep=backstep,
        backstep_one_tap_curve_metres=backstep_one_tap_curve_metres,
        backstep_one_tap_endpoint_metres=backstep_one_tap_curve_metres[-1],
        backstep_one_tap_last_root_frame=last_root_frame,
        backwalk_start=backwalk_start,
        backwalk_start_combined_curve_metres=backwalk_start_combined_curve_metres,
        backwalk_start_effect_angle_word=backwalk_start_effect_angle_word,
        backwalk_start_effect_speed_word=backwalk_start_effect_speed_word,
        backwalk_start_effect_first_frame=backwalk_start_effect_first_frame,
        backwalk_initial_metres_per_second=backwalk_initial_metres_per_second,
        backwalk_held_curve_metres=backwalk_held_curve_metres,
        backwalk_loop=backwalk_loop,
        backwalk_stop=backwalk_stop,
        backwalk_entry_route_frames=backwalk_entry_route_frames,
        backwalk_metres_per_second=backwalk_metres_per_second,
        sidestep_negative=sidestep_negative,
        sidestep_positive=sidestep_positive,
        forward_run_start=forward_run_start,
        forward_run=forward_run,
        forward_run_entry_route_frames=forward_run_continue.slot_route_frames,
        forward_run_curve_metres=forward_run_curve_metres,
        forward_run_effect_speed_word=forward_run_effect_speed_word,
        forward_run_horizontal_velocity_commands=forward_run_horizontal_velocity_commands,
        forward_run_has_conditional_effect_velocity=(
            forward_run_start.has_conditional_effect_velocity
            or forward_run_continue.has_conditional_effect_velocity
            or forward_run.has_conditional_effect_velocity
        ),
        # The composite curve above explicitly models the startup helper,
        # direct overrides, loop-end clear, and persistent integrator state.
        forward_run_root_curve_complete=True,
        forward_run_metres_per_second=forward_run_metres_per_second,
        ukemi=ukemi,
        ground_roll=ground_roll,
    )


def analyze_all(battle_dir: Path) -> list[LocomotionMeasurement]:
    common_mot_path = battle_dir / "mot" / "chr000.mot"
    return [
        analyze_character(
            cid,
            battle_dir / "hdr" / f"hdr{cid}.khd",
            battle_dir / "mot" / f"chr{cid}.mot",
            common_mot_path,
            battle_dir / "hit" / f"bodyhit{cid}.dat",
        )
        for cid in CHARACTERS
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--battle-dir",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "dump" / "Battle",
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--output-js",
        type=Path,
        help="Write the payload as window.SC6LocomotionData for the HTML viewer.",
    )
    parser.add_argument(
        "--executable",
        type=Path,
        default=Path(
            r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
        ),
        help="Exact executable whose static native model is being used.",
    )
    args = parser.parse_args()

    measurements = analyze_all(args.battle_dir)
    timing_rows = {
        backstep_timing(
            parse_khd((args.battle_dir / "hdr" / f"hdr{cid}.khd").read_bytes())
        )
        for cid in CHARACTERS
    }
    if len(timing_rows) != 1:
        raise ValueError(f"character backstep timing routes disagree: {timing_rows}")
    timing = timing_rows.pop()

    executable = args.executable.resolve(strict=True)
    executable_sha256 = hashlib.sha256(executable.read_bytes()).hexdigest()
    payload = {
        "schema": "sc6-locomotion-static-v11",
        "qualification": "static-incomplete",
        "executable": {
            "path": str(executable),
            "sha256": executable_sha256,
        },
        "known_blockers": [
            "raw_input_to_current_snapshot",
            "complete_move_scheduler_and_lane_lifecycle",
            "pose_skeleton_and_blending",
            "body_weapon_and_attack_volumes",
            "khit_intersection",
            "exact_context_query_engine",
        ],
        "routes": {
            "backstep_slot": BACKSTEP_SLOT,
            "backstep_authored_frames": timing.authored_frames,
            "backstep_transition_lead_frames": timing.transition_lead_frames,
            "backstep_transition_open_frame": timing.transition_open_frame,
            "backstep_last_outgoing_root_frame": timing.last_outgoing_root_frame,
            "backstep_release_target_slot": timing.release_target_slot,
            "backstep_release_target_start_frame": timing.release_target_start_frame,
            "backwalk_start_slot": BACKWALK_START_SLOT,
            "backwalk_continue_slot": BACKWALK_CONTINUE_SLOT,
            # The start/continuation pair preserves playback phase, but the
            # continuation's authored handoff is character-specific (24 for
            # Haohmaru, 32 for the rest of the measured roster).  It is stored
            # on each character instead of being flattened into this table.
            "backwalk_loop_slot": BACKWALK_LOOP_SLOT,
            "backwalk_stop_slot": BACKWALK_STOP_SLOT,
            "sidestep_negative_slot": SIDE_STEP_NEGATIVE_SLOT,
            "sidestep_positive_slot": SIDE_STEP_POSITIVE_SLOT,
            "forward_run_loop_slot": FORWARD_RUN_LOOP_SLOT,
            "forward_run_start_slot": FORWARD_RUN_START_SLOT,
            "forward_run_continue_slot": FORWARD_RUN_CONTINUE_SLOT,
            "ukemi_variant_slots": UKEMI_VARIANT_SLOTS,
            "ground_roll_variant_slots": GROUND_ROLL_VARIANT_SLOTS,
        },
        "characters": [asdict(row) for row in measurements],
    }
    rendered = json.dumps(payload, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    elif args.output_js:
        args.output_js.write_text(
            "window.SC6LocomotionData="
            + json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
            + ";\n",
            encoding="utf-8",
        )
    else:
        print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
