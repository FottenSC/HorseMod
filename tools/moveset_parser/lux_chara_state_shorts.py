"""Exact current MoveVM character-state short bank.

``LuxMoveVM_CallCond_WriteCharaStateShort_14 @ 0x1402FDA30`` writes a
signed 16-bit value to ``ALuxBattleChara + 0x197C + index * 2``.  The next
independently typed field begins at ``+0x1A10``, proving a 74-entry bank.
Individual entries have semantic typed views elsewhere; this module models
the shared storage contract without guessing names for unresolved entries.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_numeric import signed_low_i16
from lux_reference_engine import StaticResolutionError


MOVEVM_CHARA_STATE_SHORT_COUNT = 74


@dataclass
class LuxCharaStateShortBank:
    values: list[int] = field(
        default_factory=lambda: [0] * MOVEVM_CHARA_STATE_SHORT_COUNT
    )

    def __post_init__(self) -> None:
        if len(self.values) != MOVEVM_CHARA_STATE_SHORT_COUNT:
            raise ValueError(
                "MoveVM character-state short bank must contain exactly "
                f"{MOVEVM_CHARA_STATE_SHORT_COUNT} entries"
            )
        self.values[:] = [signed_low_i16(value) for value in self.values]

    def write(self, index: int, value: int) -> None:
        if not 0 <= index < MOVEVM_CHARA_STATE_SHORT_COUNT:
            raise StaticResolutionError(
                "CALLCOND 0x14 character-state short index "
                f"{index} is outside 0..{MOVEVM_CHARA_STATE_SHORT_COUNT - 1}"
            )
        self.values[index] = signed_low_i16(value)
