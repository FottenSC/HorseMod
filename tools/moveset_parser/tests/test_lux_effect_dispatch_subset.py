import pytest

from lux_callcond_handlers import MoveVMCallCondState, verified_callcond_handlers
from lux_effect_dispatch_subset import LuxEffectDispatch02State, dispatch_effect_02
from lux_imported_math import (
    APPROVED_UCRTBASE_SHA256,
    WindowsUcrtMath,
)
from lux_numeric import float32_bits
from lux_reference_engine import MoveVMContext, StaticResolutionError


@pytest.fixture(scope="module")
def imported_math() -> WindowsUcrtMath:
    return WindowsUcrtMath.load_verified()


def test_imported_math_is_bound_to_approved_ucrt(imported_math: WindowsUcrtMath) -> None:
    assert imported_math.sha256 == APPROVED_UCRTBASE_SHA256
    assert imported_math.evidence()["api_contract"] == "api-ms-win-crt-math-l1-1-0.dll"


def test_effect_04_uses_distinct_native_sine_and_cosine_scales(
    imported_math: WindowsUcrtMath,
) -> None:
    state = LuxEffectDispatch02State(imported_math=imported_math)
    result = dispatch_effect_02(state, MoveVMContext(), (0x0004, 0xFFF6, 30))
    assert result.value == 0
    assert float32_bits(state.effect_velocity_x) == 0xBBAAB3FF
    assert float32_bits(state.effect_velocity_z) == 0x3CF206BE


def test_effect_04_zero_angle_has_exact_float32_velocity(
    imported_math: WindowsUcrtMath,
) -> None:
    state = LuxEffectDispatch02State(imported_math=imported_math)
    dispatch_effect_02(state, MoveVMContext(), (0x0004, 0, 20))
    assert float32_bits(state.effect_velocity_x) == 0x00000000
    assert float32_bits(state.effect_velocity_z) == 0x3CA3D70A


def test_effect_06_clears_all_three_velocity_channels() -> None:
    state = LuxEffectDispatch02State(
        effect_velocity_x=1.0,
        effect_velocity_y=-2.0,
        effect_velocity_z=3.0,
    )
    dispatch_effect_02(state, MoveVMContext(), (0x0006,))
    assert (
        state.effect_velocity_x,
        state.effect_velocity_y,
        state.effect_velocity_z,
    ) == (0.0, 0.0, 0.0)


def test_effect_0e_fills_scale_tween_and_caches_previous_root_identity() -> None:
    root_identity = object()
    state = LuxEffectDispatch02State(
        pose_finalize_tick_counter=1,
        body_scale_gate=1,
        body_part_scales=[2.0] * 32,
        previous_root_matrix_identity=root_identity,
    )
    dispatch_effect_02(state, MoveVMContext(), (0x000E, 5))
    assert state.body_part_scales == [1.0] * 32
    assert state.body_scale_valid_mask == 0xFFFFFFFF
    assert float32_bits(state.body_scale_tween_rate) == 0x3E4CCCCD
    assert state.cached_previous_root_matrix_identity is root_identity


def test_effect_0e_clear_path_preserves_scale_array_and_rate() -> None:
    root_identity = object()
    state = LuxEffectDispatch02State(
        pose_finalize_tick_counter=0,
        body_scale_gate=1,
        body_part_scales=[2.0] * 32,
        body_scale_valid_mask=0xFFFFFFFF,
        body_scale_tween_rate=0.25,
        previous_root_matrix_identity=root_identity,
    )
    dispatch_effect_02(state, MoveVMContext(), (0x000E, 5))
    assert state.body_part_scales == [2.0] * 32
    assert state.body_scale_valid_mask == 0
    assert state.body_scale_tween_rate == 0.25
    assert state.cached_previous_root_matrix_identity is root_identity


@pytest.mark.parametrize("arguments", [(), (0x0004, 1), (0x0006, 1), (0x000E,)])
def test_callcond_02_rejects_missing_or_wrong_argument_shapes(
    arguments: tuple[int, ...],
) -> None:
    with pytest.raises(StaticResolutionError):
        dispatch_effect_02(LuxEffectDispatch02State(), MoveVMContext(), arguments)


def test_callcond_02_rejects_unreviewed_effect_opcode() -> None:
    with pytest.raises(StaticResolutionError, match="unreviewed effect opcode"):
        dispatch_effect_02(LuxEffectDispatch02State(), MoveVMContext(), (0x1234,))


def test_verified_handler_requires_typed_effect_state() -> None:
    handler = verified_callcond_handlers(MoveVMCallCondState())[0x02]
    with pytest.raises(StaticResolutionError, match="bounded effect-dispatch state"):
        handler(MoveVMContext(), (0x0006,))
