from __future__ import annotations

import math
from pathlib import Path

import pytest

from hgmotion_reference import (
    HuffmanBitReader,
    MotionPlaybackState,
    build_huffman_table,
    decode_huffman_keyframe_data,
    decode_root_movement_frames,
    frame_group_index,
    frame_in_group,
    parse_motion_clip,
)
from luxformats import parse_mot


pytestmark = pytest.mark.needs_dump


def test_bit_reader_reads_swapped_words_across_boundaries():
    reader = HuffmanBitReader(bytes.fromhex("1234abcd"), 0)

    assert reader.read_bits(4) == 0x1
    assert reader.read_bits(4) == 0x2
    assert reader.read_bits(8) == 0x34
    assert reader.read_bits(16) == 0xABCD


def test_frame_group_helpers_match_8_frame_groups():
    assert frame_group_index(0) == 0
    assert frame_group_index(7) == 0
    assert frame_group_index(8) == 1
    assert frame_in_group(0) == 0
    assert frame_in_group(7) == 7
    assert frame_in_group(8) == 0


def test_huffman_table_builder_accepts_real_clip_stream():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr001.mot").read_bytes())
    raw = mot.section(0x028F)
    clip = parse_motion_clip(raw, 0x028F, mot.offsets[0x028F])
    payload = 0x1C + sum(int.from_bytes(raw[0x1C + i * 2 : 0x1E + i * 2], "little", signed=True) for i in range(1))
    reader = HuffmanBitReader(raw, (payload + clip.decoded_word_count * 2) // 2)
    table = build_huffman_table(reader)

    assert table.max_bits > 0
    assert table.entries


def test_decode_frame_words_for_representative_backsteps():
    for cid, idx in [
        ("001", 0x028F),
        ("006", 0x015D),
        ("011", 0x012B),
        ("00B", 0x0272),
        ("012", 0x001F),
        ("003", 0x01AC),
        ("017", 0x04B0),
        ("061", 0x063B),
    ]:
        mot = parse_mot(Path(f"E:/myMods/dump/Battle/mot/chr{cid.lower()}.mot").read_bytes())
        state = MotionPlaybackState(
            bank=mot,
            clip_index=idx,
            current_frame=0.0,
            blend_flags=0,
            motion_flags=0,
        )
        decoded = decode_huffman_keyframe_data(state, 0, want_secondary=True)
        assert decoded.words
        assert all(-0x8000 <= word <= 0x7FFF for word in decoded.words)


def test_root_movement_curve_is_finite_for_representative_backstep():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr006.mot").read_bytes())
    clip, frames, reason = decode_root_movement_frames(mot.section(0x015D), 0x015D, mot.offsets[0x015D])

    assert clip.frame_count == len(frames)
    assert "confirmed channel stream" in reason
    assert max(abs(frame.cumulative_z) for frame in frames) > 1.0
    assert all(
        math.isfinite(value)
        for frame in frames
        for value in (frame.local_x, frame.local_y, frame.local_z)
    )
