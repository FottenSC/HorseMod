"""Instruction-faithful Lux gameplay xorshift96 primitives.

The implementation mirrors ``LuxMoveVM_GetRandXorshift96Gameplay`` at
``0x14034F1F0``.  This is the three-word RNG family shared by gameplay,
effect, and camera consumers; it is deliberately separate from the 25-word
animation LFSR and the stage-wind RNG.
"""

from __future__ import annotations

from dataclasses import dataclass


UINT32_MASK = 0xFFFFFFFF


def u32(value: int) -> int:
    return value & UINT32_MASK


def _transform_state0(value: int) -> int:
    value = u32(value)
    shifted_left = u32(value << 12)
    return u32(((shifted_left ^ (value >> 6)) & 0x1FFF) ^ (value >> 19) ^ shifted_left)


def _transform_state1(value: int) -> int:
    value = u32(value)
    shifted_left = u32(value << 4)
    return u32((((value >> 23) ^ shifted_left) & 0x7F) ^ (value >> 25) ^ shifted_left)


def _transform_state2(value: int) -> int:
    value = u32(value)
    shifted_left = u32(value << 17)
    return u32(((shifted_left ^ (value >> 8)) & 0x1FFFFF) ^ shifted_left ^ (value >> 11))


def _advance_four(value: int, transform) -> int:
    for _ in range(4):
        value = transform(value)
    return value


@dataclass
class Xorshift96GameplayState:
    """The 12-byte ``FLuxBattleXorshift96State`` native state."""

    state0: int
    state1: int
    state2: int

    def __post_init__(self) -> None:
        self.state0 = u32(self.state0)
        self.state1 = u32(self.state1)
        self.state2 = u32(self.state2)

    def draw_u32(self) -> int:
        """Advance every word by four native transforms and xor the results."""

        self.state0 = _advance_four(self.state0, _transform_state0)
        self.state1 = _advance_four(self.state1, _transform_state1)
        self.state2 = _advance_four(self.state2, _transform_state2)
        return u32(self.state0 ^ self.state1 ^ self.state2)

    @property
    def tuple(self) -> tuple[int, int, int]:
        return self.state0, self.state1, self.state2
