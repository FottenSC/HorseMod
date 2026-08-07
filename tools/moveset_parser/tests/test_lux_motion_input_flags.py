from __future__ import annotations

import pytest

from lux_motion_input_flags import (
    FALL_MASTER_FLAG,
    FALL_SOURCE_FLAGS,
    LuxMotionInputState,
    set_motion_input_flag,
)
from lux_numeric import float32_bits
from lux_reference_engine import StaticResolutionError


def test_motion_input_bank_has_verified_native_extent() -> None:
    assert len(LuxMotionInputState().flags) == 0x72
    with pytest.raises(ValueError, match="exactly 0x72 bytes"):
        LuxMotionInputState(flags=bytearray(0x71))


@pytest.mark.parametrize("source_index", sorted(FALL_SOURCE_FLAGS))
def test_each_native_fall_source_recomputes_master(source_index: int) -> None:
    state = LuxMotionInputState()
    assert set_motion_input_flag(state, source_index, 1, 8)
    assert state.flags[source_index] == 1
    assert state.flags[FALL_MASTER_FLAG] == 1

    assert set_motion_input_flag(state, source_index, 0, 8)
    assert state.flags[FALL_MASTER_FLAG] == 0


def test_flag_29_is_not_part_of_fall_source_mask() -> None:
    state = LuxMotionInputState()
    state.flags[FALL_MASTER_FLAG] = 0x7A
    state.flags[0x21] = 1
    state.flags[0x30] = 1

    set_motion_input_flag(state, 0x29, 0, state.active_lane_mask)

    assert state.flags[FALL_MASTER_FLAG] == 0x7A
    assert state.flags[0x21] == 0
    assert state.flags[0x30] == 0


def test_fall_master_is_bytewise_or_not_booleanized() -> None:
    state = LuxMotionInputState()
    state.flags[0x0C] = 0x20
    state.flags[0x25] = 0x04
    set_motion_input_flag(state, 0x35, 0x80, 8)
    assert state.flags[FALL_MASTER_FLAG] == 0xA4


def test_flag_12_clear_snaps_nearby_vertical_position_and_consumes_anchor() -> None:
    state = LuxMotionInputState(
        active_lane_mask=1,
        pose_base_anchor_identity=0x1234,
        simulated_y=10.25,
        terrain_probe_height=11.75,
    )
    set_motion_input_flag(state, 0x12, 0, state.active_lane_mask)
    assert float32_bits(state.simulated_y) == float32_bits(11.75)
    assert state.pose_base_anchor_identity is None


@pytest.mark.parametrize(
    "state",
    [
        LuxMotionInputState(
            active_lane_mask=0,
            pose_base_anchor_identity=1,
            simulated_y=10.0,
            terrain_probe_height=11.0,
        ),
        LuxMotionInputState(
            active_lane_mask=1,
            terrain_path_blocked=True,
            pose_base_anchor_identity=1,
            simulated_y=10.0,
            terrain_probe_height=11.0,
        ),
        LuxMotionInputState(
            active_lane_mask=1,
            pose_base_anchor_identity=1,
            simulated_y=10.0,
            terrain_probe_height=12.0,
        ),
    ],
)
def test_flag_12_clear_respects_each_native_snap_gate(state: LuxMotionInputState) -> None:
    original_bits = float32_bits(state.simulated_y)
    set_motion_input_flag(state, 0x12, 0, state.active_lane_mask)
    assert float32_bits(state.simulated_y) == original_bits
    assert state.pose_base_anchor_identity is None


def test_flag_2a_copies_exact_dword_only_when_gate_is_clear() -> None:
    state = LuxMotionInputState(
        cached_attack_state_word=0x7FC12345,
        published_attack_state_word=0xDEADBEEF,
    )
    set_motion_input_flag(state, 0x2A, 1, state.active_lane_mask)
    assert state.published_attack_state_word == 0x7FC12345

    state.terrain_ringout_copy_gate = True
    state.cached_attack_state_word = 0x11223344
    set_motion_input_flag(state, 0x2A, 1, state.active_lane_mask)
    assert state.published_attack_state_word == 0x7FC12345


@pytest.mark.parametrize("index", (-1, 0x72, 0x7FFF))
def test_unverified_native_out_of_bank_write_fails_closed(index: int) -> None:
    with pytest.raises(StaticResolutionError, match="outside the verified"):
        set_motion_input_flag(LuxMotionInputState(), index, 1, 0)
