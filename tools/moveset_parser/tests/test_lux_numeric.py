import math

from lux_numeric import (
    INT32_MIN,
    add_f32,
    cvttss2si,
    float32,
    float32_bits,
    float32_from_bits,
    mul_f32,
)


def test_float32_rounds_at_each_operation_boundary() -> None:
    # 2**24 is the first integer whose binary32 ULP is two.
    assert add_f32(16_777_216.0, 1.0) == 16_777_216.0
    assert add_f32(16_777_216.0, 2.0) == 16_777_218.0


def test_float32_bit_round_trip() -> None:
    assert float32_bits(float32_from_bits(0x3F800001)) == 0x3F800001
    assert float32(1.0) == 1.0


def test_multiply_is_not_implicitly_binary64() -> None:
    left = float32_from_bits(0x3F800001)
    result = mul_f32(left, left)
    assert float32_bits(result) == 0x3F800002


def test_cvttss2si_matches_sse_invalid_sentinel_and_truncation() -> None:
    assert cvttss2si(3.9) == 3
    assert cvttss2si(-3.9) == -3
    assert cvttss2si(float("nan")) == INT32_MIN
    assert cvttss2si(float("inf")) == INT32_MIN
    assert cvttss2si(2**31) == INT32_MIN
