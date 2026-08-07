from __future__ import annotations

import pytest

from lux_indexed_float_params import (
    MOVEVM_INDEXED_FLOAT_PARAM_COUNT,
    LuxIndexedFloatParamBanks,
    decode_lux_packed_float,
)


@pytest.mark.parametrize(
    ("word", "expected"),
    [
        (0x0000, 0.0),
        (0x3C00, 1.0),
        (0x4000, 2.0),
        (0xC400, -1.0),
        (0x5640, 100.0),
    ],
)
def test_packed_float_decode_matches_native_bit_rebuild(word: int, expected: float) -> None:
    assert decode_lux_packed_float(word) == expected


def test_all_three_indexed_float_banks_have_exact_native_spacing_count() -> None:
    params = LuxIndexedFloatParamBanks()
    assert MOVEVM_INDEXED_FLOAT_PARAM_COUNT == 14
    assert len(params.fallback_values) == 14
    assert len(params.weights) == 14
    assert len(params.weighted_values) == 14


def test_optional_negative_weight_clamps_to_zero_and_selects_weighted_bank() -> None:
    params = LuxIndexedFloatParamBanks()
    params.write_from_callcond((5, 0x3C00, 0xC400))
    assert params.weights[5] == 0.0
    assert params.weighted_values[5] == pytest.approx(0.01)
    assert params.fallback_values[5] == 0.0
