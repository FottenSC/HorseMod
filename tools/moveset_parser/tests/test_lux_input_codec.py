from __future__ import annotations

from pathlib import Path
import struct

import pytest

from lux_input_codec import (
    LuxInputCodecTables,
    decode_input_word,
    derive_current_snapshot,
    encode_input_word,
)
from pe_static_image import PeStaticImage


REPO_ROOT = Path(__file__).resolve().parents[3]
SC6_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
)


@pytest.fixture(scope="module")
def tables() -> LuxInputCodecTables:
    if not SC6_EXE.exists():
        pytest.skip("exact SC6 executable is not available")
    return LuxInputCodecTables.from_executable(SC6_EXE)


def test_exact_executable_tables_have_verified_boundaries(tables: LuxInputCodecTables) -> None:
    assert tuple(tables.direction_remap) == (0, 2, 3, 6, 1, 5, 9, 4, 7, 8, 0, 0, 255, 255, 255, 255)
    assert tuple(tables.nibble_to_decoded_id) == (0, 6, 4, 0, 2, 3, 1, 0, 8, 9, 7, 0, 0, 0, 0, 0)
    assert tables.direction_mask_by_decoded_id == (0, 6, 4, 5, 2, 0, 1, 10, 8, 9)


@pytest.mark.parametrize("current,secondary,side", [
    (0, 0, 0),
    (8, 8, 0),
    (0x0400, 0x0400, 0),
    (0x2408, 0x0408, 1),
    (0x3C0F, 0x1003, 1),
])
def test_native_codec_round_trip(current: int, secondary: int, side: int, tables: LuxInputCodecTables) -> None:
    encoded = encode_input_word(current, secondary, side, tables)
    decoded_current, decoded_secondary, decoded_side = decode_input_word(encoded, tables)
    assert decoded_current == (current & 0x3C0F)
    assert decoded_secondary == (secondary & 0x3C0F)
    assert decoded_side == side


def test_snapshot_derives_masks_not_held_frame_counts(tables: LuxInputCodecTables) -> None:
    snapshot = derive_current_snapshot(0x0400, 0, 0, tables)
    assert snapshot.high_input_nibble == 1
    assert snapshot.decoded_high_nibble_input_id == 6
    assert snapshot.side_decoded_input_id == 8
    assert snapshot.side_direction_mask == 8


def test_pe_mapper_refuses_virtual_zero_fill() -> None:
    data = bytearray(0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    coff = 0x84
    struct.pack_into("<H", data, coff + 2, 1)
    struct.pack_into("<H", data, coff + 16, 0xF0)
    optional = coff + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    section = optional + 0xF0
    data[section:section+8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x100, 0x2000, 0x20, 0x300)
    data[0x300:0x320] = bytes(range(0x20))
    image = PeStaticImage(bytes(data))
    assert image.read_va(0x140002000, 4) == b"\x00\x01\x02\x03"
    with pytest.raises(ValueError, match="not backed"):
        image.read_va(0x140002020, 1)
