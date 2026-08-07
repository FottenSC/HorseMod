"""Verified attack-cell selection and frame classification for CALLCOND 0x26.

The lifted transaction includes active-cell publication, KHit gate updates,
the four authored reaction-subwindow banks, and lane-2 attack-window state.
The inactive-to-active edge deliberately delegates the native shock-wave
allocation/RNG transaction to a required exact callback; CALLCOND 0x26 remains
outside the static-complete registry until that callback's implementation is
available.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Mapping, Sequence

from lux_lane_lifecycle import (
    LuxAttackCellLifecycleView,
    MoveVMCharacterLifecycleState,
)
from lux_numeric import cvttss2si, float32, signed_low_i16
from lux_reference_engine import StaticResolutionError


@dataclass
class KHitAttackNodeState:
    kind_tag: int
    active_gate: int = 0

    def __post_init__(self) -> None:
        self.kind_tag &= 0xFF
        self.active_gate &= 0xFFFF


@dataclass(frozen=True)
class LuxHitReactionSubWindow:
    start_offset: int
    end_offset: int

    def __post_init__(self) -> None:
        object.__setattr__(self, "start_offset", signed_low_i16(self.start_offset))
        object.__setattr__(self, "end_offset", signed_low_i16(self.end_offset))


AttackWindowWindSpawner = Callable[
    ["MoveVMAttackCellVariantState", LuxAttackCellLifecycleView, str], None
]


REACTION_OUTPUT_OFFSETS: tuple[int, ...] = (
    0x20BC,
    0x20BE,
    0x20C0,
    0x20C2,
    0x20C4,
    0x20C6,
    0x20C8,
    0x20CA,
    0x20CC,
    0x20CE,
    0x20D0,
    0x20D2,
    0x20D4,
    0x20D6,
    0x20D8,
    0x20DA,
    0x20DC,
    0x20DE,
    0x20E0,
    0x20E2,
    0x20E4,
    0x20E6,
    0x20E8,
    0x20EA,
    0x20EC,
    0x20EE,
)


@dataclass
class MoveVMAttackCellVariantState:
    lifecycle: MoveVMCharacterLifecycleState
    slot_cell_references: Mapping[int, Sequence[int]]
    attack_cells: Sequence[LuxAttackCellLifecycleView]
    attack_nodes: list[KHitAttackNodeState] = field(default_factory=list)
    hurt_classifier_gate: int = 0
    damage_pool_group_state_20f6: int = 0
    damage_pool_group_mirror_20fc: int = 0
    nonattack_passthrough_tag_210a: int = 0
    motion_state_16e5: int = 0
    in_master_window_16ea: int = 0
    past_master_window_16ec: int = 0
    frame_window_phase_1980: int = 0
    reaction_tier_adjustment_2118: int = 0
    reaction_outputs_i16: dict[int, int] = field(
        default_factory=lambda: {offset: -1 for offset in REACTION_OUTPUT_OFFSETS}
    )
    reaction_aux_high2_20f0: int = 0
    reaction_aux_middle3_20f2: int = 0
    hit_reaction_subwindow_banks: tuple[tuple[LuxHitReactionSubWindow, ...], ...] = ()
    spawn_attack_window_wind_effect: AttackWindowWindSpawner | None = None

    def __post_init__(self) -> None:
        if self.hit_reaction_subwindow_banks:
            if len(self.hit_reaction_subwindow_banks) != 4 or any(
                len(bank) != 16 for bank in self.hit_reaction_subwindow_banks
            ):
                raise ValueError("reaction subwindow table must contain four 16-entry banks")
        self.reaction_outputs_i16 = {
            offset: signed_low_i16(self.reaction_outputs_i16.get(offset, -1))
            for offset in REACTION_OUTPUT_OFFSETS
        }


def _spawn_attack_window_wind(
    state: MoveVMAttackCellVariantState,
    cell: LuxAttackCellLifecycleView,
    source: str,
) -> None:
    spawner = state.spawn_attack_window_wind_effect
    if spawner is None:
        raise StaticResolutionError(
            "attack-window edge requires exact shock-wave wind/RNG transaction"
        )
    spawner(state, cell, source)


def _adjust_reaction_selector(state: MoveVMAttackCellVariantState, selector: int) -> int:
    if selector == 0:
        return 0
    adjusted = (
        selector + signed_low_i16(state.reaction_tier_adjustment_2118)
    ) & 0xFFFF
    return 0xF if adjusted > 0xE else adjusted


def update_lane2_attack_window_state(state: MoveVMAttackCellVariantState) -> None:
    """Mirror ``LuxMoveVM_UpdateLane2AttackWindowState @ 0x140300B60``."""

    lifecycle = state.lifecycle
    lane = lifecycle.lane(2)
    move_id = signed_low_i16(lane.current_move_id)
    if move_id < 0 or not lifecycle.move_bank_available:
        return
    cell = _resolve_cell(state, move_id, 0)
    if cell is None or lifecycle.lane2_state_1725_1728[0] == 0:
        return

    lane2 = bytearray(lifecycle.lane2_state_1725_1728)
    was_active = lane2[1]
    lane2[1] = 0
    lifecycle.lane2_state_1729 = 0
    frame = cvttss2si(lane.animation_frame_current)
    if cell.master_window_start <= frame:
        if cell.master_window_end < frame:
            if float32(cell.master_window_start) <= lane.animation_frame_previous:
                lifecycle.lane2_state_1729 = 1
            else:
                lane2[1] = 1
        else:
            lane2[1] = 1
    if lane2[2] != 0 or lane2[3] != 0:
        lane2[1] = 0
        lifecycle.lane2_state_1729 = 0
    lifecycle.lane2_state_1725_1728 = bytes(lane2)
    if was_active == 0 and lane2[1] != 0:
        _spawn_attack_window_wind(state, cell, "lane2")


def classify_hitbox_frame_state(state: MoveVMAttackCellVariantState) -> None:
    """Mirror ``LuxMoveVM_ClassifyHitboxFrameState @ 0x140300620``."""

    lifecycle = state.lifecycle
    lane = lifecycle.lane(lifecycle.active_vm_lane_index)
    cell = lifecycle.own_active_attack_cell
    if state.motion_state_16e5 == 0:
        state.frame_window_phase_1980 = 0
    else:
        was_active = state.in_master_window_16ea
        state.in_master_window_16ea = 0
        state.past_master_window_16ec = 0
        if cell is not None:
            frame = cvttss2si(lane.animation_frame_current)
            if frame < cell.master_window_start:
                state.frame_window_phase_1980 = 1
            elif cell.master_window_end < frame:
                if float32(cell.master_window_start) <= lane.animation_frame_previous:
                    state.past_master_window_16ec = 1
                    state.frame_window_phase_1980 = 3
                else:
                    state.in_master_window_16ea = 1
                    state.frame_window_phase_1980 = 2
            else:
                state.in_master_window_16ea = 1
                state.frame_window_phase_1980 = 2
            if lifecycle.live_attack_flag_16eb or lifecycle.live_attack_flag_16fe:
                state.in_master_window_16ea = 0
                state.past_master_window_16ec = 0
            if was_active == 0 and state.in_master_window_16ea != 0:
                _spawn_attack_window_wind(state, cell, "active-lane")

    state.reaction_aux_high2_20f0 = 0
    state.reaction_aux_middle3_20f2 = 0
    for offset in REACTION_OUTPUT_OFFSETS:
        state.reaction_outputs_i16[offset] = -1

    if cell is not None and not (
        cell.range_stand_min == -1 and cell.range_stand_max == -1
    ):
        if len(state.hit_reaction_subwindow_banks) != 4:
            raise StaticResolutionError(
                "hitbox-frame classification requires four authored reaction banks"
            )
        group = state.damage_pool_group_state_20f6 & 0xFFFF
        state.reaction_aux_high2_20f0 = group >> 14
        state.reaction_aux_middle3_20f2 = (group >> 11) & 7
        selector = group & 0x7FF
        if selector < 0x40:
            bank_index = selector >> 4
            raw_selector = selector & 0xF
            subwindow = state.hit_reaction_subwindow_banks[bank_index][raw_selector]
            adjusted = _adjust_reaction_selector(state, raw_selector)
            raw_offsets = (0x20BC, 0x20BE, 0x20C0, 0x20C2)
            adjusted_offsets = (0x20C4, 0x20C6, 0x20C8, 0x20CA)
            effective_offsets = (0x20CC, 0x20CE, 0x20D0, 0x20D2)
            active_offset_groups = (
                (0x20E4, 0x20DC, 0x20D4),
                (0x20E6, 0x20DE, 0x20D6),
                (0x20E8, 0x20E0, 0x20D8),
                (0x20EA, 0x20E2, 0x20DA),
            )
            state.reaction_outputs_i16[raw_offsets[bank_index]] = raw_selector
            state.reaction_outputs_i16[adjusted_offsets[bank_index]] = adjusted
            frame = cvttss2si(lane.animation_frame_current)
            if not lifecycle.live_attack_flag_16fe and not lifecycle.live_attack_flag_16eb:
                if bank_index in (1, 3):
                    state.reaction_outputs_i16[0x20EC if bank_index == 1 else 0x20EE] = adjusted
                effective_start = cell.master_window_start + min(subwindow.start_offset, 0)
                effective_end = cell.master_window_start + max(subwindow.end_offset, 0)
                if effective_start <= frame <= effective_end:
                    state.reaction_outputs_i16[effective_offsets[bank_index]] = adjusted
                if cell.master_window_start <= frame <= cell.master_window_end:
                    for offset in active_offset_groups[bank_index]:
                        state.reaction_outputs_i16[offset] = adjusted

    update_lane2_attack_window_state(state)


def _resolve_cell(
    state: MoveVMAttackCellVariantState,
    packed_move_id: int,
    variant: int,
) -> LuxAttackCellLifecycleView | None:
    try:
        references = state.slot_cell_references[packed_move_id & 0xFFFF]
    except KeyError as error:
        raise StaticResolutionError(
            f"active move 0x{packed_move_id & 0xFFFF:04X} has no resolved bank slot"
        ) from error
    if len(references) < 6:
        raise StaticResolutionError(
            "resolved bank slot lacks six authored attack-cell references"
        )
    selected = variant if 0 <= variant < 6 else 0
    cell_index = signed_low_i16(references[selected])
    if cell_index < 0:
        return None
    if cell_index >= len(state.attack_cells):
        raise StaticResolutionError(
            f"attack-cell index {cell_index} exceeds resolved table"
        )
    return state.attack_cells[cell_index]


def set_active_move_slot_variant(
    state: MoveVMAttackCellVariantState,
    authored_variant: int,
) -> None:
    """Mirror ``LuxMoveVM_SetActiveMoveSlot @ 0x140300C70``."""

    lifecycle = state.lifecycle
    lane_index = lifecycle.active_vm_lane_index
    lane = lifecycle.lane(lane_index)
    variant = authored_variant & 0xFFFFFFFF

    if lane_index == 2:
        lane.variant_index = variant
        update_lane2_attack_window_state(state)
        return

    if lifecycle.own_active_attack_cell is None:
        return

    previous_variant = lane.variant_index
    lane.variant_index = variant
    cell = _resolve_cell(state, lane.current_move_id, variant)
    if cell is None:
        lane.variant_index = previous_variant
        return

    lifecycle.own_active_attack_cell = cell
    if state.hurt_classifier_gate != 0:
        effective_mask = cell.slot_mask | 0x3FFFD
        for node in state.attack_nodes:
            node.active_gate = (effective_mask >> (node.kind_tag & 0x3F)) & 1

    state.damage_pool_group_state_20f6 = cell.hitbox_group_bitfield
    state.damage_pool_group_mirror_20fc = cell.passthrough_tag_c
    state.nonattack_passthrough_tag_210a = cell.passthrough_tag_a

    classify_hitbox_frame_state(state)
