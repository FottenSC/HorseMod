"""Reference-style HgMotion helpers ported from SC6 Ghidra decompilation.

The goal of this module is not to emulate the full skeleton solver.  It ports
the narrow path needed for static movement analysis:

* MOT clip lookup and frame-group layout.
* The 16-bit bit reader used by LuxMotion_BitStreamReadBits.
* The Huffman keyframe delta decoder used by LuxMotion_DecodeHuffmanKeyframeData.
* Enough of LuxMotion_BlendKeyframeTransforms to walk the confirmed channel
  streams and extract root-translation channels.
"""

from __future__ import annotations

import math
import struct
from bisect import bisect_right
from dataclasses import dataclass, field

from luxformats import MotionBankFile


LUX_UNITS_PER_INT = 0.0010000000474974513
MAX_CHANNELS = 96
# LuxMotion_BlendKeyframeTransforms @ 0x1402E79C0 proves selector 0x16
# writes logical transform 1, the root transform consumed by the native
# motion-slot/direct-position path.  Selector 0x14 only updates shared
# scratch values and must not be used as authored root translation.
ROOT_CHANNEL_TYPES = {0x16}
KNOWN_CHANNEL_TYPES = {
    0x00,
    0x02,
    0x03,
    0x06,
    0x0D,
    0x0E,
    0x0F,
    0x10,
    0x13,
    0x14,
    0x15,
    0x16,
    0x17,
    0x18,
    0x19,
    0x1A,
    0x1B,
    0x1C,
}

# Confirmed callers:
#   LuxMoveVM_EvaluateBonePose passes DAT_143e83c00 when clip flags bit 0x8000 is clear.
#   It passes DAT_143e83da0 when clip flags bit 0x8000 is set.
DEFAULT_CHANNEL_TYPE_STREAM = bytes(
    [
        0x15,
        0x16,
        0x02,
        0x02,
        0x02,
        0x02,
        0x02,
        0x02,
        0x06,
        0x02,
        0x02,
        0x02,
        0x06,
        0x02,
        0x02,
        0x02,
        0x06,
        0x02,
        0x02,
        0x02,
        0x06,
        0x02,
        0x02,
        0x17,
        0x18,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x14,
        0x00,
    ]
)

ALT_CHANNEL_TYPE_STREAM = bytes(
    [
        0x15,
        0x16,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x03,
        0x19,
        0x14,
        0x00,
    ]
)


class MotionDecodeError(Exception):
    def __init__(self, stage: str, reason: str):
        super().__init__(f"{stage}: {reason}")
        self.stage = stage
        self.reason = reason


@dataclass
class MotionPlaybackState:
    bank: MotionBankFile
    clip_index: int
    current_frame: float
    blend_flags: int
    motion_flags: int
    start_frame: float = 0.0
    end_frame: float | None = None
    frame_scale: float = 1.0


@dataclass
class MotionClip:
    index: int
    offset: int
    raw: bytes
    frame_count: int
    playback_frame_count: int
    encoded_frame_count: int
    effective_frame_count: float
    decoded_word_count: int
    flags: int
    descriptor: int
    group_count: int
    group_size_table_offset: int
    static_data_offset: int


@dataclass
class HuffmanBitReader:
    raw: bytes
    word_offset: int
    bits_remaining: int = 16

    def clone(self) -> "HuffmanBitReader":
        return HuffmanBitReader(self.raw, self.word_offset, self.bits_remaining)

    def _word(self, word_offset: int) -> int:
        byte_offset = word_offset * 2
        if byte_offset + 2 > len(self.raw):
            raise MotionDecodeError("huffman_decode", "bitstream word read exceeds clip")
        w = struct.unpack_from("<H", self.raw, byte_offset)[0]
        return ((w >> 8) | ((w & 0xFF) << 8)) & 0xFFFF

    def read_bits(self, n_bits: int) -> int:
        if n_bits < 0 or n_bits > 31:
            raise MotionDecodeError("huffman_decode", f"unsupported bit read width {n_bits}")
        if n_bits == 0:
            return 0
        word_offset = self.word_offset
        current_word = self._word(word_offset)
        result = 0
        remaining_in_word = self.bits_remaining
        need = n_bits
        if remaining_in_word < need:
            while remaining_in_word < need:
                word_offset += 1
                need -= remaining_in_word
                shift = 32 - remaining_in_word
                remaining_in_word = 16
                result |= (current_word << (shift & 0x1F)) >> ((shift - need) & 0x1F)
                current_word = self._word(word_offset)
                if need <= 16:
                    break
        next_remaining = remaining_in_word - need
        if next_remaining < 1:
            word_offset += 1
            next_remaining = 16
        self.bits_remaining = next_remaining
        self.word_offset = word_offset
        result |= (current_word << ((32 - remaining_in_word) & 0x1F)) >> ((32 - need) & 0x1F)
        return result & ((1 << n_bits) - 1 if n_bits < 32 else 0xFFFFFFFF)

    def unread_bits(self, n_bits: int) -> None:
        if n_bits < 0:
            raise MotionDecodeError("huffman_decode", "cannot unread negative bits")
        self.bits_remaining += n_bits
        while self.bits_remaining > 16:
            self.word_offset -= 1
            self.bits_remaining -= 16
        if self.word_offset < 0:
            raise MotionDecodeError("huffman_decode", "bitstream rewind before clip")


@dataclass
class HuffmanEntry:
    code: int
    length: int
    symbol: int


@dataclass
class HuffmanTable:
    max_bits: int
    entries: list[HuffmanEntry]
    codes: list[int] = field(init=False)

    def __post_init__(self) -> None:
        self.entries.sort(key=lambda e: e.code)
        self.codes = [e.code for e in self.entries]

    def decode_symbol(self, reader: HuffmanBitReader) -> int:
        if self.max_bits <= 0 or not self.entries:
            raise MotionDecodeError("huffman_decode", "empty Huffman table")
        peek = reader.read_bits(self.max_bits)
        pos = bisect_right(self.codes, peek) - 1
        if pos < 0:
            raise MotionDecodeError("huffman_decode", f"no Huffman entry for code {peek}")
        entry = self.entries[pos]
        reader.unread_bits(self.max_bits - entry.length)
        return entry.symbol


@dataclass
class DecodedFrame:
    frame: int
    words: list[int]
    secondary_words: list[int] | None
    confidence: str
    trace: list[str]


@dataclass
class ChannelValue:
    channel_type: int
    component_offset_words: int
    x: float | None = None
    y: float | None = None
    z: float | None = None
    raw_components: tuple[int | float, ...] = ()


@dataclass
class MovementFrame:
    frame: int
    local_x: float
    local_y: float
    local_z: float
    delta_x: float
    delta_y: float
    delta_z: float
    cumulative_x: float
    cumulative_y: float
    cumulative_z: float
    backward_distance: float
    lateral_distance: float
    forward_distance: float
    source_channel: str
    confidence: str


def _u16(raw: bytes, offset: int) -> int:
    if offset + 2 > len(raw):
        raise MotionDecodeError("invalid_clip_header", f"u16 at 0x{offset:X} exceeds clip")
    return struct.unpack_from("<H", raw, offset)[0]


def _u32(raw: bytes, offset: int) -> int:
    if offset + 4 > len(raw):
        raise MotionDecodeError("invalid_clip_header", f"u32 at 0x{offset:X} exceeds clip")
    return struct.unpack_from("<I", raw, offset)[0]


def _u64(raw: bytes, offset: int) -> int:
    if offset + 8 > len(raw):
        raise MotionDecodeError("invalid_clip_header", f"u64 at 0x{offset:X} exceeds clip")
    return struct.unpack_from("<Q", raw, offset)[0]


def _i16(raw: bytes, offset: int) -> int:
    if offset + 2 > len(raw):
        raise MotionDecodeError("huffman_decode", f"i16 at 0x{offset:X} exceeds clip")
    return struct.unpack_from("<h", raw, offset)[0]


def _i16_to_signed(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def _add_i16(a: int, b: int) -> int:
    return _i16_to_signed((a + b) & 0xFFFF)


def _motion_frame_counts(frame_count: int, flags: int) -> tuple[int, float, int]:
    """Return native playback, effective, and stored-keyframe counts."""
    playback_frame_count = frame_count + (1 if flags & 0x10 else 0)
    if flags & 0x04:
        effective_frame_count = (playback_frame_count + 1) * 0.5
    elif flags & 0x08:
        effective_frame_count = (playback_frame_count + 3) * 0.25
    else:
        effective_frame_count = float(playback_frame_count)
    return playback_frame_count, effective_frame_count, int(effective_frame_count)


def parse_motion_clip(raw: bytes, clip_index: int = 0, offset: int = 0) -> MotionClip:
    if len(raw) < 0x20:
        raise MotionDecodeError("invalid_clip_header", "clip smaller than 0x20")
    frame_count = _u16(raw, 0)
    decoded_word_count = _u16(raw, 2) >> 1
    flags = _u32(raw, 4)
    descriptor = _u64(raw, 8)
    if frame_count == 0 or frame_count > 600:
        raise MotionDecodeError("invalid_clip_header", f"implausible frame count {frame_count}")
    if decoded_word_count == 0 or decoded_word_count > 0x1000:
        raise MotionDecodeError(
            "invalid_clip_header", f"implausible decoded word count {decoded_word_count}"
        )
    # LuxMotion_SampleKeyframeTransforms adds the optional final frame before
    # applying half/quarter-rate sampling.  Its compressed table contains the
    # integer number of keyframes addressable after that transform.
    playback_frame_count, effective_frame_count, encoded_frame_count = (
        _motion_frame_counts(frame_count, flags)
    )
    group_count = (encoded_frame_count + 7) // 8
    static_data_offset = 0x1C + group_count * 2
    if static_data_offset > len(raw):
        raise MotionDecodeError("invalid_clip_header", "frame-group table exceeds clip")
    return MotionClip(
        index=clip_index,
        offset=offset,
        raw=raw,
        frame_count=frame_count,
        playback_frame_count=playback_frame_count,
        encoded_frame_count=encoded_frame_count,
        effective_frame_count=effective_frame_count,
        decoded_word_count=decoded_word_count,
        flags=flags,
        descriptor=descriptor,
        group_count=group_count,
        group_size_table_offset=0x1C,
        static_data_offset=static_data_offset,
    )


def frame_group_index(frame: int) -> int:
    return frame // 8


def frame_in_group(frame: int) -> int:
    return frame % 8


def group_payload_offset(clip: MotionClip, group_index: int) -> int:
    if group_index < 0 or group_index >= clip.group_count:
        raise MotionDecodeError("frame_group_offset", f"group {group_index} outside clip")
    total = 0
    for i in range(group_index + 1):
        total += _i16(clip.raw, clip.group_size_table_offset + i * 2)
    off = clip.group_size_table_offset + total
    if off < clip.static_data_offset or off >= len(clip.raw):
        raise MotionDecodeError(
            "frame_group_offset",
            f"group {group_index} payload offset 0x{off:X} outside clip",
        )
    return off


def build_huffman_table(reader: HuffmanBitReader) -> HuffmanTable:
    lengths = [reader.read_bits(4) for _ in range(18)]
    max_bits = max(lengths, default=0)
    if max_bits == 0:
        raise MotionDecodeError("huffman_table", "all Huffman code lengths are zero")
    if max_bits > 16:
        raise MotionDecodeError("huffman_table", f"implausible max code length {max_bits}")

    buckets: dict[int, list[int]] = {i: [] for i in range(1, max_bits + 1)}
    for symbol, length in enumerate(lengths):
        if length:
            buckets.setdefault(length, []).append(symbol)

    entries: list[HuffmanEntry] = []
    code = 0
    step = 1
    for length in range(max_bits, 0, -1):
        for symbol in buckets.get(length, []):
            entries.append(HuffmanEntry(code=code, length=length, symbol=symbol))
            code += step
        step *= 2
    if not entries:
        raise MotionDecodeError("huffman_table", "Huffman table has no symbols")
    return HuffmanTable(max_bits=max_bits, entries=entries)


def _decode_huffman_delta(symbol: int, reader: HuffmanBitReader) -> int:
    base = symbol - 2
    if base <= 1:
        return _i16_to_signed(base)
    n_bits = base
    raw = reader.read_bits(n_bits)
    sign_mask = 1 << (n_bits - 1)
    if raw & sign_mask:
        return raw
    return raw ^ (-1 << n_bits)


def _decode_frame_words(clip: MotionClip, frame_index: int) -> tuple[list[int], list[str]]:
    if frame_index < 0 or frame_index >= clip.encoded_frame_count:
        raise MotionDecodeError("huffman_decode", f"frame {frame_index} outside clip")
    group = frame_group_index(frame_index)
    in_group = frame_in_group(frame_index)
    payload = group_payload_offset(clip, group)
    words_end = payload + clip.decoded_word_count * 2
    if words_end > len(clip.raw):
        raise MotionDecodeError("huffman_decode", "base keyframe words exceed clip")
    primary = [_i16(clip.raw, payload + i * 2) for i in range(clip.decoded_word_count)]
    trace = [
        f"group={group}",
        f"in_group={in_group}",
        f"payload=0x{payload:X}",
        f"decoded_words={clip.decoded_word_count}",
    ]
    if in_group == 0:
        return primary, trace

    residual = [0] * clip.decoded_word_count
    reader = HuffmanBitReader(clip.raw, words_end // 2, 16)
    table = build_huffman_table(reader)
    trace.append(f"huffman_max_bits={table.max_bits}")
    for _ in range(in_group):
        for i in range(clip.decoded_word_count):
            delta = _decode_huffman_delta(table.decode_symbol(reader), reader)
            residual[i] = _add_i16(residual[i], delta)
            primary[i] = _add_i16(primary[i], residual[i])
    return primary, trace


def decode_huffman_keyframe_data(
    state: MotionPlaybackState,
    frame_index: int,
    want_secondary: bool = False,
) -> DecodedFrame:
    if state.clip_index < 0 or state.clip_index >= state.bank.count:
        raise MotionDecodeError("invalid_motion_bank", f"clip index {state.clip_index} outside bank")
    raw = state.bank.section(state.clip_index)
    clip = parse_motion_clip(raw, state.clip_index, state.bank.offsets[state.clip_index])
    words, trace = _decode_frame_words(clip, frame_index)
    secondary: list[int] | None = None
    if want_secondary and frame_index + 1 < clip.encoded_frame_count:
        secondary, secondary_trace = _decode_frame_words(clip, frame_index + 1)
        trace.extend(f"secondary:{line}" for line in secondary_trace)
    return DecodedFrame(
        frame=frame_index,
        words=words,
        secondary_words=secondary,
        confidence="confirmed_static_decode",
        trace=trace,
    )


def _stream_for_clip(clip: MotionClip) -> tuple[bytes, str]:
    if (clip.flags & 0x8000) == 0:
        return DEFAULT_CHANNEL_TYPE_STREAM, "DAT_143e83c00"
    return ALT_CHANNEL_TYPE_STREAM, "DAT_143e83da0"


def _consume_bytes_for_channel(channel_type: int, flags: int) -> int:
    full_precision = (flags >> 0x1C) & 1
    if channel_type in {0x02, 0x03, 0x0D, 0x13, 0x14, 0x19, 0x1A}:
        return 6
    if channel_type in {0x06, 0x10}:
        return 2
    if channel_type in {0x0E, 0x0F}:
        return 12
    if channel_type in {0x17, 0x18}:
        return 12 if full_precision else 6
    if channel_type == 0x16:
        return 14 if full_precision else 8
    return 0


def _read_vec3(
    words: list[int],
    byte_offset: int,
    flags: int,
    channel_type: int,
) -> tuple[float, float, float, tuple[int | float, ...]]:
    full_precision = channel_type in {0x17, 0x18} and ((flags >> 0x1C) & 1)
    scale = LUX_UNITS_PER_INT
    if full_precision:
        word_idx = byte_offset // 2
        if word_idx + 5 >= len(words):
            raise MotionDecodeError("channel_stream_walk", "full-precision root exceeds decoded words")
        packed = b"".join(struct.pack("<h", words[word_idx + i]) for i in range(6))
        x, y, z = struct.unpack_from("<fff", packed, 0)
        return x, y, z, (x, y, z)
    word_idx = byte_offset // 2
    if word_idx + 2 >= len(words):
        raise MotionDecodeError("channel_stream_walk", "root channel exceeds decoded words")
    raw = (words[word_idx], words[word_idx + 1], words[word_idx + 2])
    return raw[0] * scale, raw[1] * scale, raw[2] * scale, raw


def _read_selector16_root(
    words: list[int],
    byte_offset: int,
    flags: int,
) -> tuple[float, float, float, tuple[int | float, ...]]:
    """Decode selector 0x16's authored logical-root translation.

    Native selector 0x16 consumes XYZ followed by a separate signed-short
    normalized-turn side channel.  Flag bit 28 selects float XYZ; flag bit 14
    selects 1/16000 units instead of the ordinary 0.001 Lux-unit scale.  The
    side channel is retained in diagnostics but is not a translation axis.
    """

    full_precision = ((flags >> 0x1C) & 1) != 0
    scale = (1.0 / 16000.0) if ((flags >> 14) & 1) != 0 else LUX_UNITS_PER_INT
    word_idx = byte_offset // 2
    if full_precision:
        if word_idx + 6 >= len(words):
            raise MotionDecodeError(
                "channel_stream_walk", "full-precision selector 0x16 exceeds decoded words"
            )
        packed = b"".join(struct.pack("<h", words[word_idx + i]) for i in range(6))
        x, y, z = struct.unpack_from("<fff", packed, 0)
        facing_turn = words[word_idx + 6]
        return x * scale, y * scale, z * scale, (x, y, z, facing_turn)

    if word_idx + 3 >= len(words):
        raise MotionDecodeError(
            "channel_stream_walk", "selector 0x16 exceeds decoded words"
        )
    raw_x, raw_y, raw_z, facing_turn = words[word_idx : word_idx + 4]
    return (
        raw_x * scale,
        raw_y * scale,
        raw_z * scale,
        (raw_x, raw_y, raw_z, facing_turn),
    )


def extract_root_channel(clip: MotionClip, words: list[int]) -> ChannelValue:
    stream, stream_name = _stream_for_clip(clip)
    mask = clip.descriptor
    bit = 1
    byte_offset = 0
    channel_index = 0
    fallback_root: ChannelValue | None = None
    for channel_type in stream:
        if channel_type == 0:
            break
        if channel_type not in KNOWN_CHANNEL_TYPES:
            raise MotionDecodeError(
                "channel_stream_walk", f"unknown channel type 0x{channel_type:02X}"
            )
        has_primary = (mask & bit) != 0
        if has_primary and channel_type in ROOT_CHANNEL_TYPES:
            x, y, z, raw_components = _read_selector16_root(
                words, byte_offset, clip.flags
            )
            root_value = ChannelValue(
                channel_type=channel_type,
                component_offset_words=byte_offset // 2,
                x=x,
                y=y,
                z=z,
                raw_components=raw_components,
            )
            return root_value
        if has_primary:
            byte_offset += _consume_bytes_for_channel(channel_type, clip.flags)
        if channel_type == 0x1B:
            bit >>= 1
        else:
            bit <<= 1
        channel_index += 1
        if channel_index > MAX_CHANNELS:
            raise MotionDecodeError("channel_stream_walk", f"{stream_name} did not terminate")
    raise MotionDecodeError("root_channel_missing", "no active root channel in decoded stream")


def decode_root_movement_frames(raw: bytes, clip_index: int = 0, offset: int = 0) -> tuple[MotionClip, list[MovementFrame], str]:
    clip = parse_motion_clip(raw, clip_index, offset)
    frames: list[MovementFrame] = []
    prev_x = prev_y = prev_z = 0.0
    sample_scale = 0.5 if clip.flags & 0x04 else 0.25 if clip.flags & 0x08 else 1.0
    max_sample = clip.effective_frame_count - 1.0
    base_turn = (_u16(clip.raw, 0x10) & 0xF000) / 65536.0
    angle = base_turn * math.tau
    cos_angle = math.cos(angle)
    sin_angle = math.sin(angle)
    for frame_no in range(clip.playback_frame_count):
        sample = min(max(frame_no * sample_scale, 0.0), max_sample)
        primary_index = int(sample)
        blend = sample - primary_index
        primary_words, _trace = _decode_frame_words(clip, primary_index)
        primary = extract_root_channel(clip, primary_words)
        if primary.x is None or primary.y is None or primary.z is None:
            raise MotionDecodeError("root_channel_missing", "root channel did not produce XYZ")
        x, y, z = primary.x, primary.y, primary.z
        if blend and primary_index + 1 < clip.encoded_frame_count:
            secondary_words, _ = _decode_frame_words(clip, primary_index + 1)
            secondary = extract_root_channel(clip, secondary_words)
            if secondary.x is None or secondary.y is None or secondary.z is None:
                raise MotionDecodeError("root_channel_missing", "root channel did not produce XYZ")
            x += (secondary.x - x) * blend
            y += (secondary.y - y) * blend
            z += (secondary.z - z) * blend

        # Selector 0x16 rotates the interpolated translation by the clip's
        # authored base turn using a Y-axis quaternion.
        rotated_x = cos_angle * x + sin_angle * z
        rotated_z = -sin_angle * x + cos_angle * z
        dx = rotated_x - prev_x
        dy = y - prev_y
        dz = rotated_z - prev_z
        prev_x, prev_y, prev_z = rotated_x, y, rotated_z
        frames.append(
            MovementFrame(
                frame=frame_no,
                local_x=rotated_x,
                local_y=y,
                local_z=rotated_z,
                delta_x=dx,
                delta_y=dy,
                delta_z=dz,
                cumulative_x=rotated_x,
                cumulative_y=y,
                cumulative_z=rotated_z,
                backward_distance=abs(rotated_z),
                lateral_distance=abs(rotated_x),
                forward_distance=abs(rotated_z),
                source_channel=f"0x{primary.channel_type:02X}",
                confidence="high",
            )
        )
    if not all(
        math.isfinite(v)
        for frame in frames
        for v in (
            frame.local_x,
            frame.local_y,
            frame.local_z,
            frame.delta_x,
            frame.delta_y,
            frame.delta_z,
        )
    ):
        raise MotionDecodeError("channel_stream_walk", "decoded root curve contains non-finite values")
    max_abs = max(
        (abs(v) for frame in frames for v in (frame.local_x, frame.local_y, frame.local_z)),
        default=0.0,
    )
    if max_abs > 500.0:
        raise MotionDecodeError("movement_axis_unresolved", f"decoded movement too large: {max_abs}")
    stream_name = _stream_for_clip(clip)[1]
    return clip, frames, f"decoded with confirmed channel stream {stream_name}"
