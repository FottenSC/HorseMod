from __future__ import annotations

from pathlib import Path

import pytest

from luxformats import parse_mot
from hgmotion_reference import _selector06_quaternion
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


def test_parse_mot_repeated_offsets_are_shared_clip_aliases():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr001.mot").read_bytes())

    assert mot.offsets[0] == mot.offsets[1]
    assert mot.sizes[0] == mot.sizes[1] > 0
    assert mot.section(0) == mot.section(1)


def test_motion_headers_decode_for_representative_cast():
    for cid in ["001", "006", "011", "00b", "012", "003", "017", "0ff"]:
        mot = parse_mot(Path(f"E:/myMods/dump/Battle/mot/chr{cid}.mot").read_bytes())
        idx = next(i for i, size in enumerate(mot.sizes) if size > 0)
        header = decode_motion_clip_header(mot.section(idx))
        assert header.confidence == "confirmed_static_header", (cid, idx, header)
        assert 0 < header.frame_count <= 0xFFFF


def test_long_stock_motion_clip_is_not_rejected_by_obsolete_frame_cap():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr0ff.mot").read_bytes())
    index = next(
        i
        for i in range(mot.count)
        if int.from_bytes(mot.section(i)[:2], "little") > 600
    )

    header = decode_motion_clip_header(mot.section(index))

    assert header.confidence == "confirmed_static_header"
    assert header.frame_count > 600


def test_selector06_interpolates_across_signed_turn_wrap():
    quaternion = _selector06_quaternion([0x7FFF], 0, [-0x8000], 0.5)

    assert quaternion[0:2] == (0.0, 0.0)
    assert quaternion[2] == pytest.approx(1.0)
    assert abs(quaternion[3]) < 1e-4


def test_common_skill_clip_is_not_misdecoded_with_primary_pose_stream():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr0ff.mot").read_bytes())

    curve = decode_root_motion_curve(mot.section(0x0011))

    assert curve.confidence == "failed"
    assert "channel_stream_mismatch" in curve.reason


def test_root_decode_produces_high_confidence_curve_for_representative_backstep():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr001.mot").read_bytes())
    curve = decode_root_motion_curve(mot.section(0x028F))

    assert curve.confidence == "high"
    assert curve.status == "decoded_root_motion"
    assert curve.frames
    assert curve.max_backward > 0
    assert finite_curve(curve)
