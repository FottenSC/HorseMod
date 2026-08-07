import pytest

from lux_attack_cell_variant import (
    KHitAttackNodeState,
    LuxHitReactionSubWindow,
    MoveVMAttackCellVariantState,
    classify_hitbox_frame_state,
    set_active_move_slot_variant,
    update_lane2_attack_window_state,
)
from lux_callcond_handlers import (
    MoveVMCallCondState,
    set_active_move_slot_variant_26,
    verified_callcond_handlers,
)
from lux_lane_lifecycle import (
    LuxAttackCellLifecycleView,
    MoveVMCharacterLifecycleState,
)
from lux_reference_engine import MoveVMContext, StaticResolutionError
from lux_transition_author import MoveVMLaneSchedulerState


def make_state(*, active_lane: int = 0) -> tuple[
    MoveVMAttackCellVariantState,
    MoveVMLaneSchedulerState,
]:
    lanes = tuple(
        MoveVMLaneSchedulerState(
            lane_index=index,
            current_move_id=0x31 if index == active_lane else 0x20 + index,
            variant_index=4,
        )
        for index in range(3)
    )
    cells = tuple(
        LuxAttackCellLifecycleView(
            master_window_start=index,
            master_window_end=index + 2,
            slot_mask=1 << index,
            hitbox_group_bitfield=0x1200 + index,
            passthrough_tag_a=0x2200 + index,
            passthrough_tag_c=0x3200 + index,
        )
        for index in range(8)
    )
    lifecycle = MoveVMCharacterLifecycleState(
        lanes=lanes,
        active_vm_lane_index=active_lane,
        own_active_attack_cell=cells[0],
    )
    state = MoveVMAttackCellVariantState(
        lifecycle=lifecycle,
        slot_cell_references={0x31: (0, 1, 2, 3, 4, 5)},
        attack_cells=cells,
    )
    return state, lanes[active_lane]


def test_variant_binds_cell_updates_khit_gates_and_mirrors_metadata() -> None:
    state, lane = make_state()
    state.hurt_classifier_gate = 1
    state.attack_nodes = [
        KHitAttackNodeState(1, 9),
        KHitAttackNodeState(2, 9),
        KHitAttackNodeState(20, 9),
    ]
    set_active_move_slot_variant(state, 3)

    cell = state.attack_cells[3]
    assert lane.variant_index == 3
    assert state.lifecycle.own_active_attack_cell is cell
    assert [node.active_gate for node in state.attack_nodes] == [0, 1, 0]
    assert state.damage_pool_group_state_20f6 == 0x1203
    assert state.damage_pool_group_mirror_20fc == 0x3203
    assert state.nonattack_passthrough_tag_210a == 0x2203


def test_variant_six_and_negative_signed_word_fall_back_to_variant_zero_cell() -> None:
    for variant in (6, -1):
        state, lane = make_state()
        set_active_move_slot_variant(state, variant)
        assert lane.variant_index == variant & 0xFFFFFFFF
        assert state.lifecycle.own_active_attack_cell is state.attack_cells[0]


def test_negative_cell_reference_restores_previous_variant_without_classifier() -> None:
    state, lane = make_state()
    state.slot_cell_references = {0x31: (0, 1, 2, -1, 4, 5)}
    previous_cell = state.lifecycle.own_active_attack_cell

    set_active_move_slot_variant(state, 3)

    assert lane.variant_index == 4
    assert state.lifecycle.own_active_attack_cell is previous_cell


def test_null_active_cell_is_a_noop_for_non_lane_two() -> None:
    state, lane = make_state()
    state.lifecycle.own_active_attack_cell = None
    set_active_move_slot_variant(state, 2)
    assert lane.variant_index == 4


def test_lane_two_writes_variant_and_preserves_flags_when_bank_is_unavailable() -> None:
    state, lane = make_state(active_lane=2)
    state.lifecycle.lane2_state_1725_1728 = b"\x01\x01\x00\x00"
    set_active_move_slot_variant(state, 5)
    assert lane.variant_index == 5
    assert state.lifecycle.lane2_state_1725_1728 == b"\x01\x01\x00\x00"

    set_active_move_slot_variant(state, 1)
    assert lane.variant_index == 1


def test_publication_runs_exact_classifier_instead_of_external_refresh_callback() -> None:
    state, lane = make_state()
    state.motion_state_16e5 = 1
    state.spawn_attack_window_wind_effect = lambda _state, _cell, _source: None
    lane.animation_frame_current = 2.0
    set_active_move_slot_variant(state, 2)
    assert lane.variant_index == 2
    assert state.lifecycle.own_active_attack_cell is state.attack_cells[2]
    assert state.frame_window_phase_1980 == 2


def _reaction_banks() -> tuple[tuple[LuxHitReactionSubWindow, ...], ...]:
    return tuple(
        tuple(LuxHitReactionSubWindow(-2 - bank, 3 + bank) for _ in range(16))
        for bank in range(4)
    )


def test_classifier_publishes_phase_and_exact_bank_outputs() -> None:
    state, lane = make_state()
    cell = LuxAttackCellLifecycleView(
        master_window_start=5,
        master_window_end=7,
        range_stand_min=0,
        range_stand_max=1,
        hitbox_group_bitfield=(2 << 14) | (5 << 11) | 0x12,
    )
    state.lifecycle.own_active_attack_cell = cell
    state.damage_pool_group_state_20f6 = cell.hitbox_group_bitfield
    state.hit_reaction_subwindow_banks = _reaction_banks()
    state.motion_state_16e5 = 1
    state.spawn_attack_window_wind_effect = lambda _state, _cell, _source: None
    state.reaction_tier_adjustment_2118 = 3
    lane.animation_frame_current = 5.75
    lane.animation_frame_previous = 4.0

    classify_hitbox_frame_state(state)

    assert state.frame_window_phase_1980 == 2
    assert state.in_master_window_16ea == 1
    assert state.reaction_aux_high2_20f0 == 2
    assert state.reaction_aux_middle3_20f2 == 5
    assert state.reaction_outputs_i16[0x20BE] == 2
    assert state.reaction_outputs_i16[0x20C6] == 5
    assert state.reaction_outputs_i16[0x20CE] == 5
    assert state.reaction_outputs_i16[0x20E6] == 5
    assert state.reaction_outputs_i16[0x20DE] == 5
    assert state.reaction_outputs_i16[0x20D6] == 5
    assert state.reaction_outputs_i16[0x20EC] == 5


def test_reaction_selector_addition_wraps_as_native_ushort_before_clamp() -> None:
    state, lane = make_state()
    cell = LuxAttackCellLifecycleView(
        master_window_start=0,
        master_window_end=2,
        range_stand_min=0,
        range_stand_max=1,
        hitbox_group_bitfield=1,
    )
    state.lifecycle.own_active_attack_cell = cell
    state.damage_pool_group_state_20f6 = 1
    state.hit_reaction_subwindow_banks = _reaction_banks()
    state.reaction_tier_adjustment_2118 = -2
    lane.animation_frame_current = 1.0

    classify_hitbox_frame_state(state)

    assert state.reaction_outputs_i16[0x20C4] == 0xF


def test_classifier_requires_exact_wind_transaction_only_on_entry_edge() -> None:
    state, lane = make_state()
    state.motion_state_16e5 = 1
    lane.animation_frame_current = 0.0
    lane.animation_frame_previous = -1.0

    with pytest.raises(StaticResolutionError, match="shock-wave wind/RNG"):
        classify_hitbox_frame_state(state)

    observed: list[str] = []
    state.spawn_attack_window_wind_effect = (
        lambda _state, _cell, source: observed.append(source)
    )
    state.in_master_window_16ea = 0
    classify_hitbox_frame_state(state)
    assert observed == ["active-lane"]


def test_lane_two_counter_window_uses_variant_zero_and_suppression_flags() -> None:
    state, lane = make_state(active_lane=2)
    state.lifecycle.move_bank_available = True
    state.lifecycle.lane2_state_1725_1728 = b"\x01\x00\x00\x00"
    lane.animation_frame_current = 0.0
    lane.animation_frame_previous = -1.0
    observed: list[str] = []
    state.spawn_attack_window_wind_effect = (
        lambda _state, _cell, source: observed.append(source)
    )

    update_lane2_attack_window_state(state)
    assert state.lifecycle.lane2_state_1725_1728[1] == 1
    assert observed == ["lane2"]

    state.lifecycle.lane2_state_1725_1728 = b"\x01\x00\x01\x00"
    update_lane2_attack_window_state(state)
    assert state.lifecycle.lane2_state_1725_1728[1] == 0
    assert state.lifecycle.lane2_state_1729 == 0


def test_callcond_adapter_sign_extends_word_and_returns_zero() -> None:
    state, lane = make_state()
    context = MoveVMContext()
    result = set_active_move_slot_variant_26(
        MoveVMCallCondState(attack_cell_variant=state), context, (0xFFFF,)
    )
    assert result.value == 0
    assert lane.variant_index == 0xFFFFFFFF
    assert state.lifecycle.own_active_attack_cell is state.attack_cells[0]
    assert "LuxMoveVM_SetActiveMoveSlot@0x140300C70" in context.coverage.resolved_functions


def test_callcond_26_stays_out_of_verified_registry_until_closure_is_lifted() -> None:
    state, _lane = make_state()
    assert 0x26 not in verified_callcond_handlers(
        MoveVMCallCondState(attack_cell_variant=state)
    )


def test_callcond_26_rejects_missing_state_and_wrong_argument_shape() -> None:
    with pytest.raises(StaticResolutionError, match="active attack-cell state"):
        set_active_move_slot_variant_26(MoveVMCallCondState(), MoveVMContext(), (1,))

    state, _lane = make_state()
    with pytest.raises(StaticResolutionError, match="exactly one variant word"):
        set_active_move_slot_variant_26(
            MoveVMCallCondState(attack_cell_variant=state), MoveVMContext(), ()
        )
