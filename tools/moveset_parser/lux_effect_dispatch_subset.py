"""Native-equivalent effect subsets authored through MoveVM CALLCOND 0x02/0x03.

The complete 28-character corpus reaches CALLCOND 0x02 only with effect
opcodes 0x04, 0x06, and 0x0E.  CALLCOND 0x03 is the general dispatcher; this
module lifts only individually verified operations needed by static clients.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_imported_math import WindowsUcrtMath
from lux_numeric import (
    add_f32,
    div_f32,
    float32,
    float32_from_bits,
    mul_f32,
    signed_low_i16,
)
from lux_reference_engine import CallCondResult, MoveVMContext, StaticResolutionError


ANGLE_DIVISOR_360_BITS = 0x43B40000
SPEED_DIVISOR_1000_BITS = 0x447A0000
SIN_SCALE_2PI_BITS = 0x40C90FDB
COS_SCALE_2PI_BITS = 0x40C90FDA


@dataclass
class LuxEffectDispatch02State:
    imported_math: WindowsUcrtMath | None = None
    facing_turns: float = 0.0
    effect_velocity_x: float = 0.0
    effect_velocity_y: float = 0.0
    effect_velocity_z: float = 0.0
    pose_finalize_tick_counter: int = 0
    body_scale_gate: int = 0
    body_part_scales: list[float] = field(default_factory=lambda: [0.0] * 32)
    body_scale_valid_mask: int = 0
    body_scale_tween_rate: float = 0.0
    previous_root_matrix_identity: object | None = None
    cached_previous_root_matrix_identity: object | None = None
    # Effect 0x13AC publishes a 23-bit *disable* mask.  KHit tests the
    # complementary active mask when selecting defender yarare spheres.
    hurt_sphere_disable_mask: int = 0

    def __post_init__(self) -> None:
        if len(self.body_part_scales) != 32:
            raise ValueError("Lux body-part scale state must contain exactly 32 floats")
        self.facing_turns = float32(self.facing_turns)
        self.effect_velocity_x = float32(self.effect_velocity_x)
        self.effect_velocity_y = float32(self.effect_velocity_y)
        self.effect_velocity_z = float32(self.effect_velocity_z)
        self.body_part_scales[:] = [float32(value) for value in self.body_part_scales]
        self.body_scale_tween_rate = float32(self.body_scale_tween_rate)
        self.hurt_sphere_disable_mask &= 0x7FFFFF


def _require_argument_count(opcode: int, arguments: tuple[int, ...], count: int) -> None:
    if len(arguments) != count:
        raise StaticResolutionError(
            f"CALLCOND 0x02 effect 0x{opcode:04X} requires exactly {count} words"
        )


def _write_horizontal_effect_velocity(
    state: LuxEffectDispatch02State, arguments: tuple[int, ...]
) -> None:
    _require_argument_count(0x0004, arguments, 3)
    imported_math = state.imported_math
    if imported_math is None:
        raise StaticResolutionError(
            "effect 0x0004 requires the hash-verified SC6 UCRT sinf/cosf provider"
        )

    speed = div_f32(
        float32(signed_low_i16(arguments[2])),
        float32_from_bits(SPEED_DIVISOR_1000_BITS),
    )
    angle_turns = div_f32(
        float32(signed_low_i16(arguments[1])),
        float32_from_bits(ANGLE_DIVISOR_360_BITS),
    )
    facing_angle = add_f32(angle_turns, state.facing_turns)
    sin_argument = mul_f32(facing_angle, float32_from_bits(SIN_SCALE_2PI_BITS))
    state.effect_velocity_x = mul_f32(imported_math.sinf(sin_argument), speed)
    cos_argument = mul_f32(facing_angle, float32_from_bits(COS_SCALE_2PI_BITS))
    state.effect_velocity_z = mul_f32(imported_math.cosf(cos_argument), speed)


def _reset_body_part_scale_tween(
    state: LuxEffectDispatch02State, arguments: tuple[int, ...]
) -> None:
    _require_argument_count(0x000E, arguments, 2)
    duration = signed_low_i16(arguments[1])
    if (
        state.pose_finalize_tick_counter == 0
        or duration == 0
        or state.body_scale_gate == 0
    ):
        state.body_scale_valid_mask = 0
    else:
        state.body_part_scales[:] = [float32(1.0)] * 32
        state.body_scale_valid_mask = 0xFFFFFFFF
        state.body_scale_tween_rate = div_f32(float32(1.0), float32(duration))
    state.cached_previous_root_matrix_identity = state.previous_root_matrix_identity


def dispatch_effect_02(
    state: LuxEffectDispatch02State,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    if not arguments:
        raise StaticResolutionError("native CALLCOND 0x02 dereferences effect opcode word zero")
    opcode = arguments[0] & 0xFFFF
    if opcode == 0x0004:
        _write_horizontal_effect_velocity(state, arguments)
        resolved = {"LuxMoveVM_DispatchEffectOp@0x140376B20"}
    elif opcode == 0x0006:
        _require_argument_count(opcode, arguments, 1)
        state.effect_velocity_x = float32(0.0)
        state.effect_velocity_y = float32(0.0)
        state.effect_velocity_z = float32(0.0)
        resolved = {"LuxMoveVM_DispatchEffectOp@0x140376B20"}
    elif opcode == 0x000E:
        _reset_body_part_scale_tween(state, arguments)
        resolved = {
            "LuxMoveVM_DispatchEffectOp@0x140376B20",
            "LuxMoveVM_ResetBodyPartScaleTween@0x1403114C0",
            "LuxBattle_FillBodyPartScaleBuffer@0x140311210",
            "CMatrixBank_GetPreviousBoneMatrix@0x14030B790",
        }
    else:
        raise StaticResolutionError(
            f"CALLCOND 0x02 reached unreviewed effect opcode 0x{opcode:04X}"
        )
    context.coverage.resolved_functions.update(resolved)
    return CallCondResult(0)


def _decode_hurt_sphere_disable_mask(arguments: tuple[int, ...]) -> int:
    """Decode effect 0x13AC exactly as ``LuxMoveVM_DispatchEffectOp``.

    Native argc includes the opcode word.  The two payload words are signed
    before assembly; narrowing to the 23 authored KHit slots preserves the
    native result for negative words as well as ordinary positive masks.
    """

    if len(arguments) == 1:
        return 0
    if len(arguments) == 2:
        return signed_low_i16(arguments[1]) & 0x7FFFFF
    low = signed_low_i16(arguments[1])
    high = signed_low_i16(arguments[2])
    return ((high << 15) | low) & 0x7FFFFF


def dispatch_effect_03_hurt_mask(
    state: LuxEffectDispatch02State,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """Bounded CALLCOND 0x03 lift for KHit hurt-mask effect 0x13AC."""

    if not arguments:
        raise StaticResolutionError("native CALLCOND 0x03 dereferences effect opcode word zero")
    opcode = arguments[0] & 0xFFFF
    if opcode != 0x13AC:
        raise StaticResolutionError(
            f"hurt-mask execution reached unrelated effect 0x{opcode:04X}"
        )
    if len(arguments) > 3:
        raise StaticResolutionError("effect 0x13AC accepts at most two payload words")
    state.hurt_sphere_disable_mask = _decode_hurt_sphere_disable_mask(arguments)
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_DispatchEffectOp@0x140376B20",
            "LuxMoveVM_SetHurtboxSlotsActiveMask@0x140308D70",
        }
    )
    return CallCondResult(0)
