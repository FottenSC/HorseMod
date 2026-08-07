"""Native MoveVM indexed packed-float parameter banks.

The implementation mirrors
``LuxMoveVM_CallCond_WriteIndexedFloatParams_0C @ 0x1402FDA50``.  The three
adjacent banks begin at character offsets ``+0x1B7C``, ``+0x1BB4``, and
``+0x1BEC``.  Their 0x38-byte spacing proves 14 binary32 entries per bank;
the matching authored corpus reaches indices 3, 4, 5, and 13.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_numeric import div_f32, float32, float32_from_bits, signed_low_i16
from lux_reference_engine import StaticResolutionError, u16


MOVEVM_INDEXED_FLOAT_PARAM_COUNT = 14
PACKED_FLOAT_DIVISOR = float32(100.0)


def decode_lux_packed_float(word: int) -> float:
    """Rebuild the exact binary32 value produced from one authored word."""

    raw = u16(word)
    if raw == 0:
        return float32(0.0)
    signed = signed_low_i16(raw)
    sign_bits = 0x80000000 if signed < 0 else 0
    magnitude = u16(-signed) if signed < 0 else raw
    bits = (
        ((magnitude & 0x7C00) << 13)
        + 0x38000000
        | ((magnitude & 0x03FF) << 13)
        | sign_bits
    )
    return float32_from_bits(bits)


@dataclass
class LuxIndexedFloatParamBanks:
    fallback_values: list[float] = field(
        default_factory=lambda: [0.0] * MOVEVM_INDEXED_FLOAT_PARAM_COUNT
    )
    weights: list[float] = field(
        default_factory=lambda: [0.0] * MOVEVM_INDEXED_FLOAT_PARAM_COUNT
    )
    weighted_values: list[float] = field(
        default_factory=lambda: [0.0] * MOVEVM_INDEXED_FLOAT_PARAM_COUNT
    )

    def __post_init__(self) -> None:
        for name in ("fallback_values", "weights", "weighted_values"):
            values = getattr(self, name)
            if len(values) != MOVEVM_INDEXED_FLOAT_PARAM_COUNT:
                raise ValueError(
                    f"{name} must contain exactly {MOVEVM_INDEXED_FLOAT_PARAM_COUNT} entries"
                )
            values[:] = [float32(value) for value in values]

    def write_from_callcond(self, arguments: tuple[int, ...]) -> None:
        if len(arguments) < 2:
            raise StaticResolutionError(
                "native CALLCOND 0x0C dereferences argument words zero and one"
            )
        index = arguments[0]
        if not 0 <= index < MOVEVM_INDEXED_FLOAT_PARAM_COUNT:
            raise StaticResolutionError(
                f"CALLCOND 0x0C float-bank index {index} is outside 0..13"
            )

        weight = float32(-1.0)
        if len(arguments) > 2:
            decoded_weight = div_f32(
                decode_lux_packed_float(arguments[2]), PACKED_FLOAT_DIVISOR
            )
            # Native MAXSS 0,value followed by MINSS 1,result. Packed input
            # cannot construct NaN, so ordinary comparisons preserve the
            # exact finite result.
            weight = float32(min(1.0, max(0.0, decoded_weight)))

        value = div_f32(
            decode_lux_packed_float(arguments[1]), PACKED_FLOAT_DIVISOR
        )
        self.weights[index] = weight
        if weight >= 0.0:
            self.weighted_values[index] = value
        else:
            self.fallback_values[index] = value
