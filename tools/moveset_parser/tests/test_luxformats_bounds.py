from __future__ import annotations

import struct

import pytest

from luxformats import (
    parse_hit_dat,
    parse_khd,
    parse_lpb,
    parse_lpd,
    parse_offset_table,
    parse_vtb,
)


def _lpb(*, total_len: int = 0x3C, sub_lens: tuple[int, ...] = (0,) * 7) -> bytes:
    data = bytearray(0x3C)
    data[:4] = b"lpb\0"
    struct.pack_into("<I", data, 0x10, 0x201)
    struct.pack_into("<I", data, 0x18, total_len)
    struct.pack_into("<7I", data, 0x20, *sub_lens)
    return bytes(data)


def test_offset_table_rejects_truncation_nonmonotonic_offsets_and_bad_sentinel():
    with pytest.raises(ValueError, match="too small"):
        parse_offset_table(b"\x01\x00")
    with pytest.raises(ValueError, match="exceeds file"):
        parse_offset_table(struct.pack("<I", 2))
    with pytest.raises(ValueError, match="not monotonic"):
        parse_offset_table(struct.pack("<4I", 2, 16, 15, 16))
    with pytest.raises(ValueError, match="sentinel"):
        parse_offset_table(struct.pack("<3I", 1, 12, 12) + b"\0")


def test_vtb_rejects_truncated_header_and_entry_array():
    with pytest.raises(ValueError, match="too small"):
        parse_vtb(b"vtb")
    header = struct.pack(
        "<6I", int.from_bytes(b"vtb\0", "little"), 0, 0x1002, 1, 0x18, 0
    )
    with pytest.raises(ValueError, match="entries exceed file"):
        parse_vtb(header)
    assert len(parse_vtb(header + bytes(0x84)).entries[0]) == 0x84


def test_lpd_rejects_truncated_or_out_of_order_offsets():
    with pytest.raises(ValueError, match="too small"):
        parse_lpd(b"\0" * 12)
    with pytest.raises(ValueError, match="invalid LPD offsets"):
        parse_lpd(struct.pack("<4I", 3, 0x20, 0x18, 0x30) + bytes(0x20))

    inner = _lpb()
    payload = struct.pack("<4I", 3, 0x10, 0x10, 0x10 + len(inner)) + inner
    assert parse_lpd(payload).inner is not None


def test_lpb_rejects_truncation_invalid_total_and_overlong_subblocks():
    with pytest.raises(ValueError, match="too small"):
        parse_lpb(b"lpb")
    with pytest.raises(ValueError, match="invalid LPB total length"):
        parse_lpb(_lpb(total_len=0x20))
    with pytest.raises(ValueError, match="sub-block lengths exceed total"):
        parse_lpb(_lpb(sub_lens=(0x3D, 0, 0, 0, 0, 0, 0)))


def test_khd_rejects_truncated_header_and_invalid_section_layout():
    with pytest.raises(ValueError, match="too small"):
        parse_khd(b"KH11")

    data = bytearray(0x100)
    data[:4] = b"KH11"
    struct.pack_into("<3I", data, 0x10, 0x80, 0x70, 0x90)
    with pytest.raises(ValueError, match="not strictly increasing"):
        parse_khd(bytes(data))


def test_khd_rejects_slot_table_that_overlaps_sections():
    data = bytearray(0x100)
    data[:4] = b"KH11"
    struct.pack_into("<3I", data, 0x10, 0x80, 0x90, 0xA0)
    struct.pack_into("<HH", data, 0x1C, 0, 2)
    with pytest.raises(ValueError, match="slot table overlaps"):
        parse_khd(bytes(data))


def test_hit_data_rejects_unknown_truncated_and_unterminated_streams():
    with pytest.raises(ValueError, match="unknown hit-data tag"):
        parse_hit_dat(struct.pack("<h", 3) + bytes(8))
    with pytest.raises(ValueError, match="truncated hit-data record"):
        parse_hit_dat(struct.pack("<h", 0) + bytes(8))
    with pytest.raises(ValueError, match="no negative sentinel"):
        parse_hit_dat(bytes(0x20))

    parsed = parse_hit_dat(struct.pack("<h", -1) + b"trailer")
    assert parsed.records == []
    assert parsed.trailer == struct.pack("<h", -1) + b"trailer"
