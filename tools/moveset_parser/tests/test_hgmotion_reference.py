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


def test_half_speed_common_grounded_clip_uses_effective_group_count():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr0ff.mot").read_bytes())
    raw = mot.section(0x0011)
    clip = parse_motion_clip(raw, 0x0011, mot.offsets[0x0011])

    assert clip.flags & 0x04
    assert clip.frame_count == 0x118
    assert clip.group_count == 0x12
    assert clip.static_data_offset == 0x40
    decoded = decode_huffman_keyframe_data(
        MotionPlaybackState(mot, 0x0011, 0.0, 0, 0),
        0,
        want_secondary=True,
    )
    assert len(decoded.words) == clip.decoded_word_count


def test_half_speed_root_curve_samples_and_interpolates_full_playback_timeline():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr0ff.mot").read_bytes())
    clip, frames, _ = decode_root_movement_frames(
        mot.section(0x0011), 0x0011, mot.offsets[0x0011]
    )

    assert len(frames) == clip.playback_frame_count == 0x118
    # Playback frame 5 samples stored keyframe 2.5.
    for axis in ("local_x", "local_y", "local_z"):
        assert getattr(frames[5], axis) == pytest.approx(
            (getattr(frames[4], axis) + getattr(frames[6], axis)) * 0.5
        )


def test_extra_terminal_frame_is_present_in_compressed_group_table():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr005.mot").read_bytes())
    raw = mot.section(502)
    clip = parse_motion_clip(raw, 502, mot.offsets[502])

    assert clip.flags & 0x10
    assert clip.frame_count == 80
    assert clip.playback_frame_count == clip.encoded_frame_count == 81
    assert clip.group_count == 11
    decoded = decode_huffman_keyframe_data(
        MotionPlaybackState(mot, 502, 80.0, 0, 0), 80
    )
    assert len(decoded.words) == 68
    assert "group=10" in decoded.trace


def test_selector16_full_precision_still_applies_native_units_scale():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr00c.mot").read_bytes())
    clip, frames, _ = decode_root_movement_frames(
        mot.section(456), 456, mot.offsets[456]
    )

    assert clip.flags & (1 << 28)
    assert clip.flags & (1 << 14)
    assert max(
        abs(value)
        for frame in frames
        for value in (frame.local_x, frame.local_y, frame.local_z)
    ) < 3.0


def test_selector16_applies_clip_authored_quarter_turn():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr001.mot").read_bytes())
    _, frames, _ = decode_root_movement_frames(
        mot.section(614), 614, mot.offsets[614]
    )

    # Raw selector-0x16 frame zero is (-0.903, 0.956, 0.024). The clip's
    # high-nibble 0x4000 base turn rotates it around native Y.
    assert frames[0].local_x == pytest.approx(0.024, abs=1e-6)
    assert frames[0].local_y == pytest.approx(0.956, abs=1e-6)
    assert frames[0].local_z == pytest.approx(0.903, abs=1e-6)


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
