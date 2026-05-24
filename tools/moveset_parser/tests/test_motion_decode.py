from __future__ import annotations

from pathlib import Path

import pytest

from luxformats import parse_mot
from motion_decode import decode_motion_clip_header, decode_root_motion_curve, finite_curve


pytestmark = pytest.mark.needs_dump


def test_parse_mot_uses_engine_offset_layout():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr001.mot").read_bytes())

    assert mot.reserved_04 == 0
    assert mot.offsets[0] >= 8 + mot.count * 4
    assert all(a <= b for a, b in zip(mot.offsets, mot.offsets[1:]))
    assert all(off > 0 for off in mot.offsets)

    # Regression for the old parser: anim 0x01D6 used to resolve one entry
    # early and start with 0x2B. Ghidra's +0x08 table resolves it to 0x26.
    raw = mot.section(0x01D6)
    assert raw[:2] == b"\x26\x00"


def test_motion_headers_decode_for_representative_cast():
    for cid in ["001", "006", "011", "00b", "012", "003", "017", "0ff"]:
        mot = parse_mot(Path(f"E:/myMods/dump/Battle/mot/chr{cid}.mot").read_bytes())
        idx = next(i for i, size in enumerate(mot.sizes) if size > 0)
        header = decode_motion_clip_header(mot.section(idx))
        assert header.confidence == "confirmed_static_header", (cid, idx, header)
        assert 0 < header.frame_count <= 600


def test_root_decode_produces_high_confidence_curve_for_representative_backstep():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr001.mot").read_bytes())
    curve = decode_root_motion_curve(mot.section(0x028F))

    assert curve.confidence == "high"
    assert curve.status == "decoded_root_motion"
    assert curve.frames
    assert curve.max_backward > 0
    assert finite_curve(curve)
