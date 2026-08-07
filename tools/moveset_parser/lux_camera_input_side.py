"""Native-equivalent camera-relative input-side selection.

This module lifts the camera branch in ``LuxBattle_TickCharaInput`` and the
ordered scalar-SSE cross product in ``LuxMoveVM_IsPositionLeftOfCameraLine``.
The active camera coordinates are scheduler-owned inputs to this transaction;
the surrounding camera producer remains a separate subsystem.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import math
from typing import Sequence

from lux_numeric import float32, mul_f32, sub_f32


class LuxEffectCameraClassId(IntEnum):
    """Concrete vtable +0xE0 results recovered from SC6 RTTI-backed classes."""

    SYNTHESIS = 0
    FREE_CAMERA = 1
    GAME = 2
    EBV = 3
    PLAYER_WATCH = 4
    STARE = 5
    STAY = 6
    GREAT = 7
    MOTION = 8


@dataclass(frozen=True)
class EffectCameraComponentState:
    class_id: int
    weight: float

    def __post_init__(self) -> None:
        object.__setattr__(self, "class_id", int(self.class_id))
        object.__setattr__(self, "weight", float32(self.weight))


@dataclass(frozen=True)
class CameraRelativeInputContext:
    """Typed inputs consumed by the native camera-side transaction."""

    components: tuple[EffectCameraComponentState | None, ...]
    active_camera_x: float
    active_camera_z: float
    player1_x: float
    player1_z: float
    player2_x: float
    player2_z: float

    def __post_init__(self) -> None:
        if len(self.components) != 16:
            raise ValueError("native effect-camera component list must contain exactly 16 slots")
        object.__setattr__(self, "components", tuple(self.components))
        for name in (
            "active_camera_x",
            "active_camera_z",
            "player1_x",
            "player1_z",
            "player2_x",
            "player2_z",
        ):
            object.__setattr__(self, name, float32(getattr(self, name)))


def select_highest_weight_component(
    components: Sequence[EffectCameraComponentState | None],
) -> EffectCameraComponentState | None:
    """Model COMISS candidate,best followed by JBE across exactly 16 slots."""

    if len(components) != 16:
        raise ValueError("native effect-camera component list must contain exactly 16 slots")
    best: EffectCameraComponentState | None = None
    best_weight = float32(0.0)
    for component in components:
        if component is None:
            continue
        candidate = float32(component.weight)
        # COMISS/JBE rejects <= and unordered. Strict comparison also retains
        # the earliest slot when two positive components have equal weights.
        if not math.isnan(candidate) and not math.isnan(best_weight) and candidate > best_weight:
            best = component
            best_weight = candidate
    return best


def camera_cross_product_f32(context: CameraRelativeInputContext) -> float:
    """Evaluate the native SUBSS/MULSS/SUBSS sequence without binary64 fusion."""

    camera_x_delta = sub_f32(context.active_camera_x, context.player2_x)
    camera_z_delta = sub_f32(context.active_camera_z, context.player2_z)
    fighter_x_delta = sub_f32(context.player1_x, context.player2_x)
    first_product = mul_f32(fighter_x_delta, camera_z_delta)
    fighter_z_delta = sub_f32(context.player1_z, context.player2_z)
    second_product = mul_f32(fighter_z_delta, camera_x_delta)
    return sub_f32(first_product, second_product)


def resolve_camera_side_bit(context: CameraRelativeInputContext) -> int:
    """Return the ECX side bit published by TickCharaInput before slot comparison."""

    component = select_highest_weight_component(context.components)
    if component is None:
        return 0
    if component.class_id in (
        LuxEffectCameraClassId.FREE_CAMERA,
        LuxEffectCameraClassId.PLAYER_WATCH,
    ):
        return 0
    cross = camera_cross_product_f32(context)
    # COMISS cross,0 followed by SETA: zero, negative, and unordered are false.
    return int(not math.isnan(cross) and cross > float32(0.0))


def camera_side_matches_player(
    context: CameraRelativeInputContext,
    player_slot: int,
) -> bool:
    if player_slot not in (0, 1):
        raise ValueError("native player slot must be 0 or 1")
    return resolve_camera_side_bit(context) == player_slot
