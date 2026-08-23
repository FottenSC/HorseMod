"""Native-equivalent Lux current motion-input flag state.

This models ``LuxBattleChara_SetMotionInputFlag @ 0x140304C00`` and the
0x72-byte current-state bank at ``ALuxBattleChara + 0x16D0``.  The bank is
not ordinary input history: selected flags publish derived reaction, terrain,
and attack state used by later MoveVM conditions.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_numeric import add_f32, float32, sub_f32
from lux_reference_engine import StaticResolutionError


MOTION_INPUT_FLAG_COUNT = 0x72
# Native code publishes index 0x0B as the OR of this source set.  The sources
# include blockstun (0x0C), ordinary hitstun (0x0E), airborne reaction (0x0F),
# and terminal/down-recovery state (0x10), so the aggregate must not be treated
# as a categorical fall/knockdown predicate.
REACTION_AGGREGATE_FLAG = 0x0B
REACTION_AGGREGATE_SOURCE_FLAGS = frozenset(
    (*range(0x0C, 0x12), 0x25, 0x35)
)


@dataclass
class LuxMotionInputState:
    """Typed mutable fields consumed by the verified native setter."""

    flags: bytearray = field(
        default_factory=lambda: bytearray(MOTION_INPUT_FLAG_COUNT)
    )
    active_lane_mask: int = 0
    terrain_path_blocked: bool = False
    terrain_ringout_copy_gate: bool = False
    pose_base_anchor_identity: int | None = None
    simulated_y: float = 0.0
    terrain_probe_height: float = 0.0
    cached_attack_state_word: int = 0
    published_attack_state_word: int = 0

    def __post_init__(self) -> None:
        self.flags = bytearray(self.flags)
        if len(self.flags) != MOTION_INPUT_FLAG_COUNT:
            raise ValueError(
                f"Lux current motion-input bank must be exactly "
                f"0x{MOTION_INPUT_FLAG_COUNT:X} bytes"
            )
        self.active_lane_mask &= 0xFFFFFFFF
        self.simulated_y = float32(self.simulated_y)
        self.terrain_probe_height = float32(self.terrain_probe_height)
        self.cached_attack_state_word &= 0xFFFFFFFF
        self.published_attack_state_word &= 0xFFFFFFFF


def _require_flag_index(index: int) -> None:
    if not 0 <= index < MOTION_INPUT_FLAG_COUNT:
        raise StaticResolutionError(
            f"native motion-input flag index {index} is outside the verified "
            f"0x00..0x{MOTION_INPUT_FLAG_COUNT - 1:02X} state bank"
        )


def set_motion_input_flag(
    state: LuxMotionInputState,
    flag_index: int,
    new_value: int,
    active_lane_mask: int,
) -> bool:
    """Mirror ``LuxBattleChara_SetMotionInputFlag @ 0x140304C00``."""

    _require_flag_index(flag_index)
    new_value &= 0xFF
    state.flags[flag_index] = new_value

    if flag_index == 0x12:
        if new_value == 0:
            if (
                active_lane_mask & 0x7
                and not state.terrain_path_blocked
                and state.pose_base_anchor_identity is not None
            ):
                height_delta = sub_f32(
                    state.terrain_probe_height, state.simulated_y
                )
                if abs(height_delta) < float32(2.0):
                    state.simulated_y = add_f32(state.simulated_y, height_delta)
            # Native always consumes the anchor on a FLAG_12 clear.
            state.pose_base_anchor_identity = None
        return True

    if flag_index in REACTION_AGGREGATE_SOURCE_FLAGS:
        derived = 0
        for source_index in REACTION_AGGREGATE_SOURCE_FLAGS:
            derived |= state.flags[source_index]
        set_motion_input_flag(state, REACTION_AGGREGATE_FLAG, derived, 8)
        return True

    if flag_index == 0x2A:
        if new_value and not state.terrain_ringout_copy_gate:
            state.published_attack_state_word = state.cached_attack_state_word
        return True

    if flag_index == 0x29 and new_value == 0:
        set_motion_input_flag(state, 0x21, 0, 8)
        set_motion_input_flag(state, 0x30, 0, 8)

    return True
