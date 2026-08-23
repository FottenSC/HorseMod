from __future__ import annotations

import math
from pathlib import Path

import pytest

from hgmotion_reference import (
    HuffmanBitReader,
    MotionPlaybackState,
    PoseMotionLane,
    build_huffman_table,
    decode_huffman_keyframe_data,
    decode_root_movement_frames,
    decode_collision_pose,
    decode_four_lane_collision_pose,
    compose_transform,
    frame_group_index,
    frame_in_group,
    parse_motion_clip,
    load_compact_collision_skeleton_from_nmd_manifest,
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


def test_full_core_pose_decode_composes_the_native_nmd_hierarchy():
    root = Path("E:/myMods/dump/Battle")
    mot = parse_mot((root / "mot" / "chr012.mot").read_bytes())
    skeleton = load_compact_collision_skeleton_from_nmd_manifest(
        root / "nmd" / "profile_overlays" / "012" / "manifest.json",
        root / "profile" / "RP_012.json",
    )
    clip_index = 0x0200
    pose = decode_collision_pose(
        mot.section(clip_index), skeleton, 13.0, range(23),
        clip_index=clip_index, offset=mot.offsets[clip_index],
    )
    assert set(pose.requested_world) == set(range(23))
    for index in range(1, 23):
        expected = compose_transform(pose.world[skeleton.parents[index]], pose.local[index])
        assert pose.world[index].translation == pytest.approx(expected.translation, abs=1e-7)
        assert pose.world[index].rotation == pytest.approx(expected.rotation, abs=1e-7)


def test_nmd_overlay_uses_converted_native_core_parents_not_raw_source_links():
    root = Path("E:/myMods/dump/Battle")
    skeleton = load_compact_collision_skeleton_from_nmd_manifest(
        root / "nmd" / "profile_overlays" / "012" / "manifest.json",
        root / "profile" / "RP_012.json",
    )
    # Upper-arm and thigh chains are independent evidence that raw NMD +0x60
    # was not already in the final collision-reference parent domain.
    assert skeleton.parents[8:10] == (7, 8)       # UDE_L -> KATA_L -> TE_L
    assert skeleton.parents[15:19] == (14, 15, 16, 17)


def test_selector06_consumes_words_without_publishing_a_joint_rotation():
    root = Path("E:/myMods/dump/Battle")
    mot = parse_mot((root / "mot" / "chr012.mot").read_bytes())
    skeleton = load_compact_collision_skeleton_from_nmd_manifest(
        root / "nmd" / "profile_overlays" / "012" / "manifest.json",
        root / "profile" / "RP_012.json",
    )
    clip_index = 0x0200
    pose = decode_collision_pose(
        mot.section(clip_index), skeleton, 16.0, (8,),
        clip_index=clip_index, offset=mot.offsets[clip_index],
    )

    # Default-stream logical transform 8 is selector 0x06. The executable
    # consumes its i16 but performs no FTransform48 write or dirty publication.
    assert pose.local[8] == skeleton.reference_local[8]


def test_four_lane_pose_single_full_weight_lane_matches_direct_decode():
    root = Path("E:/myMods/dump/Battle")
    mot = parse_mot((root / "mot" / "chr012.mot").read_bytes())
    skeleton = load_compact_collision_skeleton_from_nmd_manifest(
        root / "nmd" / "profile_overlays" / "012" / "manifest.json",
        root / "profile" / "RP_012.json",
    )
    clip_index = 0x0200
    raw = mot.section(clip_index)
    direct = decode_collision_pose(
        raw, skeleton, 13.0, range(23),
        clip_index=clip_index, offset=mot.offsets[clip_index],
    )
    blended = decode_four_lane_collision_pose(
        (
            PoseMotionLane(raw, 13.0, 1.0, True, clip_index, mot.offsets[clip_index]),
            PoseMotionLane(None, 0.0, 0.0, False),
            PoseMotionLane(None, 0.0, 0.0, False),
            PoseMotionLane(None, 0.0, 0.0, False),
        ),
        skeleton,
        range(23),
    )
    for index in range(23):
        assert blended.world[index].translation == pytest.approx(
            direct.world[index].translation, abs=1e-7
        )
        assert blended.world[index].rotation == pytest.approx(
            direct.world[index].rotation, abs=1e-7
        )


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


def test_left_ukemi_exposes_selector16_yaw_side_channel_separately():
    mot = parse_mot(Path("E:/myMods/dump/Battle/mot/chr000.mot").read_bytes())
    clip_index = 0x0B5  # Packed motion ID 0x10B5.
    _, frames, _ = decode_root_movement_frames(
        mot.section(clip_index), clip_index, mot.offsets[clip_index]
    )

    assert frames[0].root_yaw_turns == 0.0
    assert max(frame.root_yaw_turns for frame in frames) == pytest.approx(
        32767.0 / 65536.0
    )


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
