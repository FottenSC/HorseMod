from __future__ import annotations

import math

import pytest

from lux_camera_input_side import (
    CameraRelativeInputContext,
    EffectCameraComponentState,
    LuxEffectCameraClassId,
    camera_cross_product_f32,
    camera_side_matches_player,
    resolve_camera_side_bit,
    select_highest_weight_component,
)
from lux_numeric import float32_bits


def _slots(*components: EffectCameraComponentState | None):
    return tuple(components) + (None,) * (16 - len(components))


def _context(
    components=_slots(EffectCameraComponentState(LuxEffectCameraClassId.GAME, 1.0)),
    *,
    camera_x: float = 0.0,
    camera_z: float = 1.0,
    player1_x: float = 1.0,
    player1_z: float = 0.0,
    player2_x: float = 0.0,
    player2_z: float = 0.0,
) -> CameraRelativeInputContext:
    return CameraRelativeInputContext(
        components=components,
        active_camera_x=camera_x,
        active_camera_z=camera_z,
        player1_x=player1_x,
        player1_z=player1_z,
        player2_x=player2_x,
        player2_z=player2_z,
    )


def test_highest_weight_selection_requires_strict_positive_and_keeps_first_tie() -> None:
    first = EffectCameraComponentState(LuxEffectCameraClassId.GAME, 0.75)
    tied = EffectCameraComponentState(LuxEffectCameraClassId.STAY, 0.75)
    slots = _slots(
        None,
        EffectCameraComponentState(LuxEffectCameraClassId.MOTION, -1.0),
        EffectCameraComponentState(LuxEffectCameraClassId.GREAT, 0.0),
        EffectCameraComponentState(LuxEffectCameraClassId.STARE, math.nan),
        first,
        tied,
    )
    assert select_highest_weight_component(slots) is first


def test_no_positive_component_publishes_side_zero() -> None:
    context = _context(
        _slots(
            EffectCameraComponentState(LuxEffectCameraClassId.GAME, 0.0),
            EffectCameraComponentState(LuxEffectCameraClassId.MOTION, -0.5),
        )
    )
    assert resolve_camera_side_bit(context) == 0


@pytest.mark.parametrize(
    "class_id",
    [LuxEffectCameraClassId.FREE_CAMERA, LuxEffectCameraClassId.PLAYER_WATCH],
)
def test_free_and_player_watch_cameras_force_side_zero(class_id: int) -> None:
    context = _context(_slots(EffectCameraComponentState(class_id, 1.0)))
    assert camera_cross_product_f32(context) > 0.0
    assert resolve_camera_side_bit(context) == 0


@pytest.mark.parametrize(
    "class_id",
    [
        LuxEffectCameraClassId.SYNTHESIS,
        LuxEffectCameraClassId.GAME,
        LuxEffectCameraClassId.EBV,
        LuxEffectCameraClassId.STARE,
        LuxEffectCameraClassId.STAY,
        LuxEffectCameraClassId.GREAT,
        LuxEffectCameraClassId.MOTION,
    ],
)
def test_other_camera_classes_use_ordered_cross_product(class_id: int) -> None:
    assert resolve_camera_side_bit(
        _context(_slots(EffectCameraComponentState(class_id, 1.0)))
    ) == 1


def test_cross_product_zero_negative_and_unordered_are_false() -> None:
    assert resolve_camera_side_bit(_context(camera_z=0.0)) == 0
    assert resolve_camera_side_bit(_context(camera_z=-1.0)) == 0
    assert resolve_camera_side_bit(_context(camera_z=math.nan)) == 0


def test_cross_product_rounds_at_each_native_scalar_sse_operation() -> None:
    context = _context(
        camera_x=-11029.162109375,
        camera_z=-46351.8515625,
        player1_x=-92815.1328125,
        player1_z=-94511.03125,
        player2_x=-7021.2275390625,
        player2_z=-36306.97265625,
    )
    assert float32_bits(camera_cross_product_f32(context)) == 0x4E15D949


def test_side_bit_is_compared_directly_with_player_slot() -> None:
    positive = _context()
    assert not camera_side_matches_player(positive, 0)
    assert camera_side_matches_player(positive, 1)


def test_context_requires_exact_native_slot_count() -> None:
    with pytest.raises(ValueError, match="exactly 16"):
        _context((None,) * 15)
