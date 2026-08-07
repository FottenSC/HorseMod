"""Static HgMotion/MOT helpers for SC6 movement analysis.

This module intentionally separates "valid clip header" from "decoded root
motion".  The root channel format is only partially understood from Ghidra, so
distance values are emitted only when a decoder can prove the curve.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass

from hgmotion_reference import MotionDecodeError, decode_root_movement_frames, parse_motion_clip


LUX_UNITS_PER_INT = 0.0010000000474974513
# Native LuxMotion_BlendKeyframeTransforms writes logical root transform 1
# from selector 0x16.  Keep this exported marker aligned with the reference
# decoder; selector 0x14 is scratch-only.
ROOT_CHANNEL_TYPES = {0x16}


@dataclass
class MotionClipHeader:
    frame_count: int
    channel_count_words: int
    flags: int
    header_size: int
    raw_header_hex: str
    confidence: str
    reason: str


@dataclass
class RootMotionFrame:
    frame: int
    x: float
    y: float
    z: float
    cumulative_x: float
    cumulative_y: float
    cumulative_z: float
    source_channel: str


@dataclass
class RootMotionCurve:
    status: str
    confidence: str
    frame_count: int
    frames: list[RootMotionFrame]
    total_x: float
    total_y: float
    total_z: float
    max_backward: float
    first_backward_8: int | None
    first_backward_16: int | None
    first_backward_30: int | None
    reason: str


def _unknown_curve(frame_count: int, confidence: str, reason: str) -> RootMotionCurve:
    return RootMotionCurve(
        status="unknown_without_root_motion_decode",
        confidence=confidence,
        frame_count=frame_count,
        frames=[],
        total_x=0.0,
        total_y=0.0,
        total_z=0.0,
        max_backward=0.0,
        first_backward_8=None,
        first_backward_16=None,
        first_backward_30=None,
        reason=reason,
    )


def decode_motion_clip_header(raw: bytes) -> MotionClipHeader:
    if len(raw) < 0x20:
        return MotionClipHeader(
            frame_count=0,
            channel_count_words=0,
            flags=0,
            header_size=0,
            raw_header_hex=raw[:0x20].hex(),
            confidence="failed",
            reason="motion section is smaller than 0x20 bytes",
        )

    frame_count, channel_count_words, flags = struct.unpack_from("<HHI", raw, 0)
    if frame_count == 0 or frame_count > 600:
        return MotionClipHeader(
            frame_count=frame_count,
            channel_count_words=channel_count_words,
            flags=flags,
            header_size=0x1C,
            raw_header_hex=raw[:0x20].hex(),
            confidence="failed",
            reason=f"implausible frame count {frame_count}",
        )

    decoded_word_count = channel_count_words >> 1
    if decoded_word_count == 0 or decoded_word_count > 0x1000:
        return MotionClipHeader(
            frame_count=frame_count,
            channel_count_words=channel_count_words,
            flags=flags,
            header_size=0x1C,
            raw_header_hex=raw[:0x20].hex(),
            confidence="failed",
            reason=f"implausible decoded word count {decoded_word_count}",
        )

    try:
        table_end = parse_motion_clip(raw).static_data_offset
    except MotionDecodeError as exc:
        return MotionClipHeader(
            frame_count=frame_count,
            channel_count_words=channel_count_words,
            flags=flags,
            header_size=0x1C,
            raw_header_hex=raw[:0x20].hex(),
            confidence="failed",
            reason=f"{exc.stage}: {exc.reason}",
        )

    return MotionClipHeader(
        frame_count=frame_count,
        channel_count_words=channel_count_words,
        flags=flags,
        header_size=table_end,
        raw_header_hex=raw[: min(len(raw), 0x40)].hex(),
        confidence="confirmed_static_header",
        reason="clip header matches LuxMotion_SampleKeyframeTransforms/LuxMotion_DecodeHuffmanKeyframeData layout",
    )


def decode_root_motion_curve(raw: bytes) -> RootMotionCurve:
    """Decode authored root translation from one MOT motion section."""

    header = decode_motion_clip_header(raw)
    if header.confidence == "failed":
        return _unknown_curve(header.frame_count, "failed", header.reason)

    try:
        clip, movement_frames, reason = decode_root_movement_frames(raw)
    except MotionDecodeError as exc:
        return _unknown_curve(header.frame_count, "failed", f"{exc.stage}: {exc.reason}")

    frames = [
        RootMotionFrame(
            frame=f.frame,
            x=f.local_x,
            y=f.local_y,
            z=f.local_z,
            cumulative_x=f.cumulative_x,
            cumulative_y=f.cumulative_y,
            cumulative_z=f.cumulative_z,
            source_channel=f.source_channel,
        )
        for f in movement_frames
    ]
    max_backward = max((abs(f.cumulative_z) for f in frames), default=0.0)

    def first_crossing(threshold: float) -> int | None:
        for f in frames:
            if abs(f.cumulative_z) >= threshold:
                return f.frame
        return None

    return RootMotionCurve(
        status="decoded_root_motion",
        confidence="high",
        frame_count=clip.playback_frame_count,
        frames=frames,
        total_x=frames[-1].cumulative_x if frames else 0.0,
        total_y=frames[-1].cumulative_y if frames else 0.0,
        total_z=frames[-1].cumulative_z if frames else 0.0,
        max_backward=max_backward,
        first_backward_8=first_crossing(8.0),
        first_backward_16=first_crossing(16.0),
        first_backward_30=first_crossing(30.0),
        reason=reason,
    )


def finite_curve(curve: RootMotionCurve) -> bool:
    values = [curve.total_x, curve.total_y, curve.total_z, curve.max_backward]
    for frame in curve.frames:
        values.extend(
            [
                frame.x,
                frame.y,
                frame.z,
                frame.cumulative_x,
                frame.cumulative_y,
                frame.cumulative_z,
            ]
        )
    return all(math.isfinite(v) for v in values)
