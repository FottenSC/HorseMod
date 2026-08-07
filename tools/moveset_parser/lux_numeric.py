"""Explicit numeric operations used by the offline Lux reference engine.

Python evaluates ordinary floating-point expressions as binary64.  Lux battle
code predominantly uses scalar SSE binary32 instructions, so every modeled
operation must round at the same producer boundary instead of relying on a
final cast.
"""

from __future__ import annotations

import math
import struct


INT32_MIN = -0x80000000
INT32_MAX = 0x7FFFFFFF


def float32(value: float | int) -> float:
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def float32_bits(value: float | int) -> int:
    return struct.unpack("<I", struct.pack("<f", float32(value)))[0]


def float32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def add_f32(left: float, right: float) -> float:
    return float32(float32(left) + float32(right))


def sub_f32(left: float, right: float) -> float:
    return float32(float32(left) - float32(right))


def mul_f32(left: float, right: float) -> float:
    return float32(float32(left) * float32(right))


def div_f32(left: float, right: float) -> float:
    left, right = float32(left), float32(right)
    if right == 0.0:
        if left == 0.0 or math.isnan(left):
            return float32(float("nan"))
        sign = math.copysign(1.0, left) * math.copysign(1.0, right)
        return float32(math.copysign(float("inf"), sign))
    return float32(left / right)


def cvttss2si(value: float) -> int:
    """Model the 32-bit SSE CVTTSS2SI result used by Lux predicates."""
    value = float32(value)
    if not math.isfinite(value) or value < INT32_MIN or value >= 0x80000000:
        return INT32_MIN
    return int(value)


def signed_low_i16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value
