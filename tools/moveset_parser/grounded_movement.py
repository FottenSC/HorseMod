"""Static evidence model for SC6 Ukemi and ordinary grounded rolls.

The authored grounded dispatcher passes ``0x00D0`` for the Ukemi family and
``0x00D1`` for the later ordinary-roll family.  The player-relative direction
is converted to one of slots 0x75..0x7C, then the forward/back dispatchers use
MoveVM IF 0x13 to choose the animation for the character's head-end or
feet-end orientation relative to the opponent.

This module deliberately reports open-space authored root travel only.  It
does not model collision, walls, hurtboxes, safety, or the time before the
grounded input becomes available.
"""

from __future__ import annotations

from dataclasses import dataclass
import math

from hgmotion_reference import decode_root_movement_frames
from luxformats import KhdFile, MotionBankFile
from stackvm_emulate import Concrete, emulate


@dataclass(frozen=True)
class GroundedRouteLeaf:
    dispatcher_slot: int
    leaf_slot: int


@dataclass(frozen=True)
class GroundedTravel:
    direction: str
    head_end: float
    feet_end: float
    head_motion_id: int
    feet_motion_id: int

    @property
    def minimum(self) -> float:
        return min(self.head_end, self.feet_end)

    @property
    def maximum(self) -> float:
        return max(self.head_end, self.feet_end)


# Player-relative ordinary-roll routes proven from the D1 branch of the
# shared grounded dispatcher.  The dispatcher swaps 0x79/0x7A and 0x7B/0x7C
# when the opponent moves from the character's head end to the feet end.
GROUND_ROLL_ROUTES: dict[str, dict[str, GroundedRouteLeaf]] = {
    "forward": {
        "head": GroundedRouteLeaf(0x79, 0x85),
        "feet": GroundedRouteLeaf(0x7A, 0x87),
    },
    "backward": {
        "head": GroundedRouteLeaf(0x7A, 0x89),
        "feet": GroundedRouteLeaf(0x79, 0x8B),
    },
    "right": {
        "head": GroundedRouteLeaf(0x7C, 0x90),
        "feet": GroundedRouteLeaf(0x7B, 0x8F),
    },
    "left": {
        "head": GroundedRouteLeaf(0x7B, 0x8F),
        "feet": GroundedRouteLeaf(0x7C, 0x90),
    },
}


# Leaf choices made inside the four D1 dispatcher slots.  IF 0x13 true means
# the opponent is at the character's local head end; fallback means feet end.
_EXPECTED_ROLL_DISPATCH: dict[int, tuple[int, ...]] = {
    0x79: (0x85, 0x8B),
    0x7A: (0x89, 0x87),
    0x7B: (0x8F,),
    0x7C: (0x90,),
}


def validate_ground_roll_dispatchers(bank: KhdFile) -> None:
    """Reject a KHD whose ordinary-roll leaf routing differs from the model."""
    for dispatcher, expected in _EXPECTED_ROLL_DISPATCH.items():
        events = emulate(bank.slots[dispatcher].bytecode, dispatcher).transitions
        actual = tuple(event.next_move_slot for event in events)
        if actual != expected:
            raise ValueError(
                f"roll dispatcher 0x{dispatcher:X}: expected {expected}, got {actual}"
            )
        if dispatcher in (0x79, 0x7A):
            predicate = events[0].predicate
            args = (
                tuple(arg.value for arg in predicate.args if isinstance(arg, Concrete))
                if predicate is not None
                else ()
            )
            if args != (0x13, 0xFFA6, 0x005A):
                raise ValueError(
                    f"roll dispatcher 0x{dispatcher:X}: unexpected orientation predicate {args}"
                )


def resolve_ground_motion_bank(
    packed_motion_id: int,
    character_motion: MotionBankFile,
    common_body_motion: MotionBankFile,
) -> tuple[MotionBankFile, int]:
    """Resolve the bank/clip packing used by InitMotionPlayback.

    Grounded clips use bank 0 for a character-local override and bank 1 for
    the shared body-motion bank (``chr000.mot``).  Other banks are rejected
    rather than guessed.
    """
    bank_index = (packed_motion_id >> 12) & 0xF
    clip_index = packed_motion_id & 0x7FF
    if bank_index == 0:
        bank = character_motion
    elif bank_index == 1:
        bank = common_body_motion
    else:
        raise ValueError(
            f"unsupported grounded motion bank {bank_index} in 0x{packed_motion_id:04X}"
        )
    if clip_index >= bank.count:
        raise ValueError(
            f"motion 0x{packed_motion_id:04X} resolves past bank count {bank.count}"
        )
    return bank, clip_index


def authored_endpoint_distance(
    packed_motion_id: int,
    character_motion: MotionBankFile,
    common_body_motion: MotionBankFile,
) -> float:
    bank, clip_index = resolve_ground_motion_bank(
        packed_motion_id, character_motion, common_body_motion
    )
    _clip, frames, _reason = decode_root_movement_frames(
        bank.section(clip_index), clip_index, bank.offsets[clip_index]
    )
    endpoint = frames[-1]
    return math.hypot(endpoint.cumulative_x, endpoint.cumulative_z)


def analyze_ground_rolls(
    bank: KhdFile,
    character_motion: MotionBankFile,
    common_body_motion: MotionBankFile,
) -> dict[str, GroundedTravel]:
    validate_ground_roll_dispatchers(bank)
    result: dict[str, GroundedTravel] = {}
    for direction, orientations in GROUND_ROLL_ROUTES.items():
        head_motion = bank.slots[orientations["head"].leaf_slot].wAnimationIndex_00
        feet_motion = bank.slots[orientations["feet"].leaf_slot].wAnimationIndex_00
        result[direction] = GroundedTravel(
            direction=direction,
            head_end=authored_endpoint_distance(
                head_motion, character_motion, common_body_motion
            ),
            feet_end=authored_endpoint_distance(
                feet_motion, character_motion, common_body_motion
            ),
            head_motion_id=head_motion,
            feet_motion_id=feet_motion,
        )
    return result
