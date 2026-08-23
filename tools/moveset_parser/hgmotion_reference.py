"""Reference-style HgMotion helpers ported from SC6 Ghidra decompilation.

This module ports the deterministic authored portion of the collision-pose
path needed by static movement and combo analysis:

* MOT clip lookup and frame-group layout.
* The 16-bit bit reader used by LuxMotion_BitStreamReadBits.
* The Huffman keyframe delta decoder used by LuxMotion_DecodeHuffmanKeyframeData.
* LuxMotion_BlendKeyframeTransforms selectors used by compact collision bones
  1..22 (root translation, axis-angle and axial quaternions, and reference-pose
  fallback).
* Reconstruction of the fixed compact collision hierarchy from shipped NMD3
  references, or from the same named reference bones in a shipped Unreal
  skeleton when a DLC NMD overlay is absent.

Runtime controller, spine-IK, and analytic-IK branches are intentionally not
invented here.  Callers must prove those gates inactive for a scenario before
treating :func:`decode_collision_pose` as the native-final pose.
"""

from __future__ import annotations

import json
import math
import struct
from bisect import bisect_right
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

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
    # Selector-0x16's fourth component is a normalized-turn side channel.
    # Native SolveBonePose stores the sampled value in FLuxHitCuePoseLane
    # +0xA4; it is not a translation-W component.
    root_yaw_turns: float = 0.0


Vec3 = tuple[float, float, float]
Mat3 = tuple[float, float, float, float, float, float, float, float, float]


@dataclass(frozen=True)
class PoseTransform:
    """Rigid Lux-space transform used by the compact collision hierarchy."""

    rotation: Mat3
    translation: Vec3


@dataclass(frozen=True)
class CompactCollisionSkeleton:
    """The fixed native matrix profile reconstructed from a shipped skeleton."""

    names: tuple[str, ...]
    parents: tuple[int, ...]
    reference_local: tuple[PoseTransform, ...]
    source: str
    # SolveBonePose scales transform-1's selector-0x16 translation by
    # FLuxCharaVfxEffectAnchorBlock.flReferenceExtentScale1 (BODY*LOWER).
    root_translation_scale: float = 1.0


def _iter_nmd_reference_records(nmd_path: str | Path):
    """Yield accepted native collision-reference records from one NMD3."""

    path = Path(nmd_path)
    raw = path.read_bytes()
    if len(raw) < 0x20 or raw[:4] != b"NMD\x03":
        raise MotionDecodeError("reference_nmd", f"{path} is not an NMD3 file")
    count = struct.unpack_from("<H", raw, 0x0A)[0]
    first_link = struct.unpack_from("<I", raw, 0x0C)[0]
    if first_link + 4 > len(raw):
        raise MotionDecodeError("reference_nmd", "NMD first relocation link is out of range")
    records_offset = struct.unpack_from("<I", raw, first_link)[0]
    if records_offset + count * 0x70 > len(raw):
        raise MotionDecodeError("reference_nmd", "NMD move-entry table is truncated")
    for record_index in range(count):
        offset = records_offset + record_index * 0x70
        type_tag = raw[offset + 0x5F]
        if type_tag & 0xFD:
            continue
        parent_reference, reference_index = struct.unpack_from("<HH", raw, offset + 0x60)
        yield reference_index, parent_reference, struct.unpack_from("<3f", raw, offset + 0x10)


def load_regular_profile_body_scales(profile_path: str | Path) -> tuple[float, float, dict[str, float]]:
    """Return native final upper/lower reference scales from RegularProfile JSON.

    ``CalculateBattleCharaRescaledBoneSizePositions @ 0x1402E9F90`` receives
    BODY*UPPER for references 0..11 and BODY*LOWER for references 12..20.
    Missing enum entries retain the profile default of 1.0.
    """

    path = Path(profile_path)
    profile = json.loads(path.read_text(encoding="utf-8-sig"))
    values = {
        str(item["Type"]).rsplit("::", 1)[-1]: float(item["Scale"])
        for item in profile.get("bodyScales", ())
    }
    body = values.get("EBS_BODY", 1.0)
    return body * values.get("EBS_UPPER", 1.0), body * values.get("EBS_LOWER", 1.0), values


def load_compact_collision_skeleton_from_nmd_manifest(
    manifest_path: str | Path,
    profile_path: str | Path,
    compact_count: int = 23,
) -> CompactCollisionSkeleton:
    """Merge a RegularProfile's ordered PARTS NMD overlays like native load.

    Later records replace earlier records with the same reference id.  This
    mirrors the selected-part merge order consumed by InitBoneRefPositions;
    no UE skeleton or character-name fallback participates in the result.
    """

    manifest = Path(manifest_path)
    entries = json.loads(manifest.read_text(encoding="utf-8-sig"))
    upper_scale, lower_scale, _ = load_regular_profile_body_scales(profile_path)
    # Raw NMD +0x60 is an asset-local source-bone link.  The native asset load
    # converts the layered records to the in-memory FLuxMoveDataEntry parent
    # domain before InitBoneRefPositions consumes them.  Treating the raw word
    # as that final parent creates impossible chains (for example UDE_L under
    # ATAMA).  The converted core hierarchy is the fixed native reference map
    # independently recovered from complete r_all records and shipped named
    # skeletons below; raw overlays contribute the final reference vectors.
    merged: dict[int, tuple[int, Vec3, str]] = {}
    for entry in entries:
        local_file = manifest.parent / str(entry["localFile"])
        for reference_index, parent_reference, position in _iter_nmd_reference_records(local_file):
            if reference_index + 2 < compact_count:
                merged[reference_index] = (parent_reference, position, str(local_file))

    required = set(range(21))
    missing = sorted(required - merged.keys())
    if missing:
        raise MotionDecodeError(
            "reference_nmd", "profile NMD overlay set lacks references: " + ", ".join(map(str, missing))
        )

    # InitBoneRefPositions first derives the current extents from the merged
    # unscaled vectors.  CalculateBattleCharaRescaledBoneSizePositions then
    # applies requested/current, not the requested value directly.
    upper_extent = merged[1][1][0] / 0.1300000101327896
    lower_extent = merged[13][1][0] / 0.1600000113248825
    if upper_extent == 0.0 or lower_extent == 0.0:
        raise MotionDecodeError("reference_nmd", "merged reference extent is zero")
    upper_ratio = upper_scale / upper_extent
    lower_ratio = lower_scale / lower_extent

    parents = list(NATIVE_CORE_PARENTS[:compact_count])
    local = [IDENTITY_TRANSFORM for _ in range(compact_count)]
    names = list(NATIVE_CORE_REFERENCE_NAMES[:compact_count])
    for reference_index in sorted(required):
        _raw_source_link, position, _ = merged[reference_index]
        matrix_index = reference_index + 2
        scale = upper_ratio if reference_index <= 11 else lower_ratio
        local[matrix_index] = PoseTransform(
            IDENTITY_MAT3, tuple(value * scale for value in position)  # type: ignore[arg-type]
        )
    if any(parent < 0 or parent >= index for index, parent in enumerate(parents[2:23], start=2)):
        raise MotionDecodeError("reference_nmd", "profile NMD core FK parent is not already published")
    return CompactCollisionSkeleton(
        tuple(names), tuple(parents), tuple(local),
        f"{manifest} + {Path(profile_path)} "
        f"(upper={upper_scale:.9g}/{upper_extent:.9g}, "
        f"lower={lower_scale:.9g}/{lower_extent:.9g})",
        lower_scale,
    )


def load_compact_collision_skeleton_from_nmd(
    nmd_path: str | Path,
    compact_count: int = 32,
    *,
    upper_scale: float = 1.0,
    lower_scale: float = 1.0,
) -> CompactCollisionSkeleton:
    """Recover native collision FK references from a shipped NMD3 overlay.

    ``LuxBattleChara_InitBoneRefPositions @ 0x1402EA810`` accepts records
    whose tag at +0x5F has no bits other than bit 1.  Their reference index is
    +0x62 and Lux local XYZ +0x10/+0x14/+0x18.  The value at +0x60 is the
    parent record inside that particular overlay, not the fixed collision-FK
    parent. Native collision matrix index is reference index + 2.

    A regular ``r_all`` overlay contains the complete core 0..22 reference
    set.  PARTS overlays and non-unit profile scaling must be merged by a
    higher-level caller before claiming native-final pose.
    """

    if compact_count > len(NATIVE_CORE_PARENTS):
        raise MotionDecodeError(
            "reference_nmd", "raw NMD cannot prove parents beyond the native core profile"
        )
    parents = list(NATIVE_CORE_PARENTS[:compact_count])
    local = [IDENTITY_TRANSFORM for _ in range(compact_count)]
    names = ["actor_root", "motion_root"] + [f"reference_{index}" for index in range(compact_count - 2)]
    seen: set[int] = set()
    for reference_index, _raw_source_link, position in _iter_nmd_reference_records(nmd_path):
        matrix_index = reference_index + 2
        if matrix_index >= compact_count:
            continue
        scale = upper_scale if matrix_index < 14 else lower_scale
        local[matrix_index] = PoseTransform(
            IDENTITY_MAT3, tuple(value * scale for value in position)  # type: ignore[arg-type]
        )
        seen.add(matrix_index)
    required = set(range(2, min(compact_count, 23)))
    missing = sorted(required - seen)
    if missing:
        raise MotionDecodeError(
            "reference_nmd", "NMD core reference set is incomplete: " + ", ".join(map(str, missing))
        )
    if any(parent < 0 or parent >= index for index, parent in enumerate(parents[2:23], start=2)):
        raise MotionDecodeError("reference_nmd", "NMD core FK parent is not already published")
    return CompactCollisionSkeleton(
        tuple(names), tuple(parents), tuple(local), str(Path(nmd_path)), lower_scale
    )


# LuxBattleChara_InitBoneRefPositions publishes NMD reference ids 0..20 as
# matrix indices 2..22.  Comparing every complete shipped r_all_*.nmd against
# its character skeleton closes this name correspondence; it is not the UE
# hierarchy/index order (which varies substantially between characters).
NATIVE_CORE_REFERENCE_NAMES: tuple[str, ...] = (
    "actor_root", "motion_root", "MUNE1", "MUNE2", "KUBI", "ATAMA",
    "SAKOTSU_L", "KATA_L", "UDE_L", "TE_L",
    "SAKOTSU_R", "KATA_R", "UDE_R", "TE_R",
    "KOSHI", "MOMO_L", "HIZA_L", "ASHI_L", "TSUMASAKI_L",
    "MOMO_R", "HIZA_R", "ASHI_R", "TSUMASAKI_R",
)
NATIVE_CORE_PARENTS: tuple[int, ...] = (
    -1, 0, 1, 2, 3, 4, 3, 6, 7, 8, 3, 10, 11, 12,
    1, 14, 15, 16, 17, 14, 19, 20, 21,
)


def load_compact_collision_skeleton_from_named_reference(
    skeleton_path: str | Path | Iterable[str | Path],
) -> CompactCollisionSkeleton:
    """Recover the native 0..22 profile from shipped reference-bone names.

    This is the DLC fallback for characters whose ``r_all`` NMD is not in the
    extraction.  It deliberately ignores the UE parent indices.  The selected
    bones and fixed parents are the native NMD mapping above; exported local
    translations already include the regular-profile proportion adjustment.
    The import basis maps local UE reference XYZ to Lux ``(X,-Y,-Z) / 100``.
    That conversion reproduces the unscaled NMD records where the profile is
    unit-sized and reproduces the profile-adjusted references visible in the
    shipped character skeletons where it is not.
    """

    paths = (
        (Path(skeleton_path),)
        if isinstance(skeleton_path, (str, Path))
        else tuple(Path(path) for path in skeleton_path)
    )
    by_name: dict[str, dict] = {}
    sources: list[str] = []
    for path in paths:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        sources.append(str(data.get("source", path)))
        for bone in data["bones"]:
            by_name.setdefault(str(bone["name"]), bone)
    missing = [name for name in NATIVE_CORE_REFERENCE_NAMES[2:] if name not in by_name]
    if missing:
        raise MotionDecodeError(
            "reference_skeleton", "missing native reference bones: " + ", ".join(missing)
        )
    local = [IDENTITY_TRANSFORM, IDENTITY_TRANSFORM]
    for name in NATIVE_CORE_REFERENCE_NAMES[2:]:
        translation = tuple(float(value) for value in by_name[name]["translation"])
        local.append(PoseTransform(
            IDENTITY_MAT3,
            (translation[0] / 100.0, -translation[1] / 100.0, -translation[2] / 100.0),
        ))
    return CompactCollisionSkeleton(
        NATIVE_CORE_REFERENCE_NAMES,
        NATIVE_CORE_PARENTS,
        tuple(local),
        " + ".join(sources),
    )


@dataclass(frozen=True)
class CollisionPose:
    frame: float
    local: tuple[PoseTransform, ...]
    world: tuple[PoseTransform, ...]
    requested_world: dict[int, PoseTransform]
    confidence: str


@dataclass(frozen=True)
class PoseMotionLane:
    """One ordered KMotFrame input to the native main pose solver."""

    raw: bytes | None
    frame: float
    weight: float
    active: bool
    clip_index: int = 0
    offset: int = 0


IDENTITY_MAT3: Mat3 = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)
IDENTITY_TRANSFORM = PoseTransform(IDENTITY_MAT3, (0.0, 0.0, 0.0))


def _mat_mul(a: Mat3, b: Mat3) -> Mat3:
    return tuple(
        sum(a[row * 3 + k] * b[k * 3 + col] for k in range(3))
        for row in range(3)
        for col in range(3)
    )  # type: ignore[return-value]


def _mat_transpose(a: Mat3) -> Mat3:
    return (a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8])


def _mat_vec(a: Mat3, v: Vec3) -> Vec3:
    return (
        a[0] * v[0] + a[1] * v[1] + a[2] * v[2],
        a[3] * v[0] + a[4] * v[1] + a[5] * v[2],
        a[6] * v[0] + a[7] * v[1] + a[8] * v[2],
    )


def _vec_add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _vec_sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def compose_transform(parent: PoseTransform, local: PoseTransform) -> PoseTransform:
    return PoseTransform(
        _mat_mul(parent.rotation, local.rotation),
        _vec_add(parent.translation, _mat_vec(parent.rotation, local.translation)),
    )


def inverse_compose_transform(parent_world: PoseTransform, child_world: PoseTransform) -> PoseTransform:
    inverse_rotation = _mat_transpose(parent_world.rotation)
    return PoseTransform(
        _mat_mul(inverse_rotation, child_world.rotation),
        _mat_vec(inverse_rotation, _vec_sub(child_world.translation, parent_world.translation)),
    )


def transform_point(transform: PoseTransform, point: Vec3) -> Vec3:
    return _vec_add(transform.translation, _mat_vec(transform.rotation, point))


def _quat_to_mat3(q: Iterable[float]) -> Mat3:
    x, y, z, w = (float(value) for value in q)
    length = math.sqrt(x * x + y * y + z * z + w * w)
    if length == 0.0:
        return IDENTITY_MAT3
    x, y, z, w = x / length, y / length, z / length, w / length
    return (
        1.0 - 2.0 * (y * y + z * z),
        2.0 * (x * y - z * w),
        2.0 * (x * z + y * w),
        2.0 * (x * y + z * w),
        1.0 - 2.0 * (x * x + z * z),
        2.0 * (y * z - x * w),
        2.0 * (x * z - y * w),
        2.0 * (y * z + x * w),
        1.0 - 2.0 * (x * x + y * y),
    )


def _mat3_to_quat(matrix: Mat3) -> tuple[float, float, float, float]:
    """Convert an orthonormal row-major matrix for ordered lane slerp."""

    trace = matrix[0] + matrix[4] + matrix[8]
    if trace > 0.0:
        scale = math.sqrt(trace + 1.0) * 2.0
        return (
            (matrix[7] - matrix[5]) / scale,
            (matrix[2] - matrix[6]) / scale,
            (matrix[3] - matrix[1]) / scale,
            0.25 * scale,
        )
    if matrix[0] > matrix[4] and matrix[0] > matrix[8]:
        scale = math.sqrt(1.0 + matrix[0] - matrix[4] - matrix[8]) * 2.0
        return (
            0.25 * scale,
            (matrix[1] + matrix[3]) / scale,
            (matrix[2] + matrix[6]) / scale,
            (matrix[7] - matrix[5]) / scale,
        )
    if matrix[4] > matrix[8]:
        scale = math.sqrt(1.0 + matrix[4] - matrix[0] - matrix[8]) * 2.0
        return (
            (matrix[1] + matrix[3]) / scale,
            0.25 * scale,
            (matrix[5] + matrix[7]) / scale,
            (matrix[2] - matrix[6]) / scale,
        )
    scale = math.sqrt(1.0 + matrix[8] - matrix[0] - matrix[4]) * 2.0
    return (
        (matrix[2] + matrix[6]) / scale,
        (matrix[5] + matrix[7]) / scale,
        0.25 * scale,
        (matrix[3] - matrix[1]) / scale,
    )


def _selector02_quaternion(words: list[int], byte_offset: int) -> tuple[float, float, float, float]:
    """Port selector 0x02/0x19's three-i16 axis-angle conversion."""

    word_index = byte_offset // 2
    if word_index + 2 >= len(words):
        raise MotionDecodeError("channel_stream_walk", "selector 0x02 exceeds decoded words")
    angle_word, azimuth_word, elevation_word = words[word_index : word_index + 3]
    azimuth = (azimuth_word / 65536.0) * math.tau
    elevation = (elevation_word / 65536.0) * math.tau
    cos_elevation = math.cos(elevation)
    axis_x = math.cos(azimuth) * cos_elevation
    axis_y = math.sin(elevation)
    axis_z = -math.sin(azimuth) * cos_elevation
    half_angle = (angle_word / 65536.0) * math.pi
    sine = math.sin(half_angle)
    return (sine * axis_x, sine * axis_y, sine * axis_z, math.cos(half_angle))


def _selector02_rotation(words: list[int], byte_offset: int) -> Mat3:
    return _quat_to_mat3(_selector02_quaternion(words, byte_offset))


def _quat_slerp(
    a: tuple[float, float, float, float],
    b: tuple[float, float, float, float],
    alpha: float,
) -> tuple[float, float, float, float]:
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0.0:
        b = tuple(-value for value in b)  # type: ignore[assignment]
        dot = -dot
    dot = min(1.0, max(-1.0, dot))
    if dot > 0.9995:
        blended = tuple(x + (y - x) * alpha for x, y in zip(a, b))
        length = math.sqrt(sum(value * value for value in blended))
        return tuple(value / length for value in blended)  # type: ignore[return-value]
    angle = math.acos(dot)
    denominator = math.sin(angle)
    left = math.sin((1.0 - alpha) * angle) / denominator
    right = math.sin(alpha * angle) / denominator
    return tuple(left * x + right * y for x, y in zip(a, b))  # type: ignore[return-value]


def _ue_world_to_lux(world: PoseTransform) -> PoseTransform:
    # UE(X,Y,Z) = Lux(X,Z,Y) * 100.  The axis swap changes handedness, so
    # conjugate the matrix instead of trying to represent it as a quaternion.
    swap: Mat3 = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0)
    return PoseTransform(
        _mat_mul(_mat_mul(swap, world.rotation), swap),
        (world.translation[0] / 100.0, world.translation[2] / 100.0, world.translation[1] / 100.0),
    )


def load_compact_collision_skeleton(
    skeleton_path: str | Path | Iterable[str | Path],
    canonical_path: str | Path,
    compact_count: int = 32,
    required_bones: Iterable[int] | None = None,
) -> CompactCollisionSkeleton:
    """Map a variable Unreal hierarchy onto the native fixed matrix profile.

    CUE4Parse's exported translations already carry the character's reference
    proportion adjustment.  Native pose destinations are initialized with
    unit scale, so the Unreal reference ``scale`` lanes are deliberately not
    composed a second time here.
    """

    skeleton_paths = (
        (Path(skeleton_path),)
        if isinstance(skeleton_path, (str, Path))
        else tuple(Path(path) for path in skeleton_path)
    )
    if not skeleton_paths:
        raise MotionDecodeError("reference_skeleton", "no skeleton sources supplied")
    canonical_data = json.loads(Path(canonical_path).read_text(encoding="utf-8-sig"))
    canonical_bones = canonical_data["bones"][:compact_count]
    if len(canonical_bones) != compact_count:
        raise MotionDecodeError("reference_skeleton", "canonical compact profile is truncated")

    worlds_by_name: dict[str, PoseTransform] = {}
    sources: list[str] = []
    for source_path in skeleton_paths:
        skeleton_data = json.loads(source_path.read_text(encoding="utf-8-sig"))
        sources.append(str(skeleton_data.get("source", source_path)))
        raw_world: list[PoseTransform] = []
        for index, bone in enumerate(skeleton_data["bones"]):
            local = PoseTransform(
                _quat_to_mat3(bone["rotation"]),
                tuple(float(v) for v in bone["translation"]),  # type: ignore[arg-type]
            )
            parent = int(bone["parent"])
            if parent >= index:
                raise MotionDecodeError(
                    "reference_skeleton", f"bone {index} has invalid parent {parent}"
                )
            raw_world.append(compose_transform(raw_world[parent], local) if parent >= 0 else local)
            worlds_by_name.setdefault(str(bone["name"]), _ue_world_to_lux(raw_world[-1]))

    names = tuple(str(bone["name"]) for bone in canonical_bones)
    parents = tuple(int(bone["parent"]) for bone in canonical_bones)
    required = set(range(compact_count) if required_bones is None else required_bones)
    missing_required = [names[index] for index in sorted(required) if names[index] not in worlds_by_name]
    if missing_required:
        raise MotionDecodeError(
            "reference_skeleton", "missing required compact bones: " + ", ".join(missing_required)
        )
    # Missing non-required leaves (MUNE1plus on four shipped skeletons) cannot
    # affect the requested KHit matrices.  Give them the canonical local below
    # rather than rejecting an otherwise complete collision profile.
    canonical = None
    if any(name not in worlds_by_name for name in names):
        canonical = load_compact_collision_skeleton(
            canonical_path, canonical_path, compact_count, range(compact_count)
        ) if skeleton_paths != (Path(canonical_path),) else None
    # Matrix 0 is the Lux actor root.  The UE import-root orientation is not a
    # collision bone at runtime, so bake that basis into compact transform 1.
    reference_local: list[PoseTransform] = [IDENTITY_TRANSFORM]
    if names[1] in worlds_by_name:
        reference_local.append(worlds_by_name[names[1]])
    elif canonical is not None:
        reference_local.append(canonical.reference_local[1])
    else:
        raise MotionDecodeError("reference_skeleton", f"missing compact bone {names[1]}")
    for index in range(2, compact_count):
        parent = parents[index]
        if not 0 <= parent < index:
            raise MotionDecodeError(
                "reference_skeleton", f"compact bone {index} has invalid parent {parent}"
            )
        if names[index] in worlds_by_name and names[parent] in worlds_by_name:
            reference_local.append(
                inverse_compose_transform(worlds_by_name[names[parent]], worlds_by_name[names[index]])
            )
        elif canonical is not None:
            reference_local.append(canonical.reference_local[index])
        else:
            raise MotionDecodeError("reference_skeleton", f"missing compact bone {names[index]}")
    return CompactCollisionSkeleton(
        names=names,
        parents=parents,
        reference_local=tuple(reference_local),
        source=" + ".join(sources),
    )


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


def _selector16_yaw_turns(raw_components: tuple[int | float, ...]) -> float:
    """Convert selector-0x16's signed-short yaw lane to normalized turns."""

    return float(raw_components[3]) / 65536.0


def _lerp_wrapped_turns(current: float, following: float, alpha: float) -> float:
    """Native shortest-arc interpolation followed by [-0.5, 0.5) wrapping."""

    delta = following - current
    if delta > 0.5:
        following -= 1.0
    elif delta < -0.5:
        following += 1.0
    value = current + (following - current) * alpha
    return (value + 0.5) % 1.0 - 0.5


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


def make_lux_world_transform(
    position: Vec3 = (0.0, 0.0, 0.0), facing_turns: float = 0.0
) -> PoseTransform:
    angle = facing_turns * math.tau
    cosine, sine = math.cos(angle), math.sin(angle)
    # Same native Y-axis convention used by selector 0x16 root publication.
    rotation: Mat3 = (
        cosine, 0.0, sine,
        0.0, 1.0, 0.0,
        -sine, 0.0, cosine,
    )
    return PoseTransform(rotation, position)


def _core_selector_layout(clip: MotionClip) -> dict[int, tuple[int, int, bool]]:
    """Return compact transform -> (selector, decoded-byte-offset, active)."""

    stream, stream_name = _stream_for_clip(clip)
    mask = clip.descriptor
    bit = 1
    byte_offset = 0
    transform_index = 0
    result: dict[int, tuple[int, int, bool]] = {}
    for stream_index, channel_type in enumerate(stream):
        if channel_type == 0:
            break
        if channel_type not in KNOWN_CHANNEL_TYPES:
            raise MotionDecodeError(
                "channel_stream_walk", f"unknown channel type 0x{channel_type:02X}"
            )
        if channel_type in {0x16, 0x02, 0x06}:
            transform_index += 1
            if transform_index <= 22:
                result[transform_index] = (channel_type, byte_offset, bool(mask & bit))
        if mask & bit:
            byte_offset += _consume_bytes_for_channel(channel_type, clip.flags)
        bit = bit >> 1 if channel_type == 0x1B else bit << 1
        if stream_index > MAX_CHANNELS:
            raise MotionDecodeError("channel_stream_walk", f"{stream_name} did not terminate")
        if transform_index >= 22:
            break
    if transform_index != 22:
        raise MotionDecodeError(
            "channel_stream_walk", f"{stream_name} exposes {transform_index} core transforms"
        )
    return result


def decode_collision_pose(
    raw: bytes,
    skeleton: CompactCollisionSkeleton,
    frame: float,
    requested_bones: Iterable[int],
    *,
    clip_index: int = 0,
    offset: int = 0,
    actor_world: PoseTransform = IDENTITY_TRANSFORM,
) -> CollisionPose:
    """Decode and compose requested native collision matrices 0..22.

    Selector 0x02 replaces rotation and preserves the character's rescaled
    reference translation. Selector 0x06 consumes one decoded word but marks
    no output dirty and therefore leaves its logical transform unchanged.
    Transform 1's
    horizontal selector-0x16 translation is cleared by ordinary solve mode;
    actor/root travel belongs in ``actor_world`` and is not fed into pose a
    second time.
    """

    requested = tuple(sorted(set(int(index) for index in requested_bones)))
    if any(index < 0 or index > 22 for index in requested):
        raise MotionDecodeError("collision_pose", "requested bone lies outside proven core 0..22")
    if len(skeleton.reference_local) < 23:
        raise MotionDecodeError("reference_skeleton", "compact profile has fewer than 23 bones")

    clip = parse_motion_clip(raw, clip_index, offset)
    sample_scale = 0.5 if clip.flags & 0x04 else 0.25 if clip.flags & 0x08 else 1.0
    sample = min(
        max(frame * sample_scale, 0.0),
        max(0.0, clip.effective_frame_count - 1.0),
    )
    primary_index = int(sample)
    alpha = sample - primary_index
    primary_words, _ = _decode_frame_words(clip, primary_index)
    secondary_words = None
    if alpha and primary_index + 1 < clip.encoded_frame_count:
        secondary_words, _ = _decode_frame_words(clip, primary_index + 1)

    layout = _core_selector_layout(clip)
    local = list(skeleton.reference_local)
    # Native ordinary solve mode clears selector-0x16 X/Z feedback after
    # publishing horizontal root motion, but preserves selector-0x16 Y as the
    # collision skeleton's logical-root height.  Omitting Y leaves knockdown
    # poses standing roughly half a metre too high.
    root_selector, root_byte_offset, root_active = layout[1]
    root_y = local[1].translation[1]
    if root_active:
        if root_selector != 0x16:
            raise MotionDecodeError(
                "collision_pose", f"transform 1 uses selector 0x{root_selector:02X}"
            )
        _root_x, root_y, _root_z, _raw = _read_selector16_root(
            primary_words, root_byte_offset, clip.flags
        )
        if secondary_words is not None:
            _next_x, next_y, _next_z, _next_raw = _read_selector16_root(
                secondary_words, root_byte_offset, clip.flags
            )
            root_y += (next_y - root_y) * alpha
    local[1] = PoseTransform(
        local[1].rotation, (0.0, root_y * skeleton.root_translation_scale, 0.0)
    )
    for bone_index in range(2, 23):
        selector, byte_offset, active = layout[bone_index]
        if not active:
            continue
        if selector == 0x06:
            # Native advances both decoded-word cursors but performs no
            # FTransform48 write and does not set this channel's dirty bit.
            continue
        if selector == 0x02:
            quaternion = _selector02_quaternion(primary_words, byte_offset)
            if secondary_words is not None:
                quaternion = _quat_slerp(
                    quaternion,
                    _selector02_quaternion(secondary_words, byte_offset),
                    alpha,
                )
        else:
            raise MotionDecodeError(
                "collision_pose", f"unsupported core selector 0x{selector:02X}"
            )
        local[bone_index] = PoseTransform(
            _quat_to_mat3(quaternion), local[bone_index].translation
        )

    # Selector 0x16 also publishes transform 1's authored base-turn
    # quaternion from the clip header.  SolveBonePose left-multiplies that
    # quaternion into transforms 2 and 14, then resets transform 1's rotation
    # to identity before matrix conversion.  This is part of the ordinary
    # authored pose path, not a runtime IK/controller branch.
    if root_active:
        base_turn = (_u16(clip.raw, 0x10) & 0xF000) / 65536.0
        half_angle = base_turn * math.pi
        root_rotation = _quat_to_mat3(
            (0.0, math.sin(half_angle), 0.0, math.cos(half_angle))
        )
        for bone_index in (2, 14):
            local[bone_index] = PoseTransform(
                _mat_mul(root_rotation, local[bone_index].rotation),
                local[bone_index].translation,
            )

    world = [actor_world]
    for bone_index in range(1, len(local)):
        parent = skeleton.parents[bone_index]
        if parent < 0 or parent >= bone_index:
            raise MotionDecodeError(
                "collision_pose", f"compact bone {bone_index} has invalid parent {parent}"
            )
        world.append(compose_transform(world[parent], local[bone_index]))
    return CollisionPose(
        frame=frame,
        local=tuple(local),
        world=tuple(world),
        requested_world={index: world[index] for index in requested},
        confidence="native_authored_core_pose_runtime_controllers_excluded",
    )


def decode_four_lane_collision_pose(
    lanes: Iterable[PoseMotionLane],
    skeleton: CompactCollisionSkeleton,
    requested_bones: Iterable[int],
    *,
    actor_world: PoseTransform = IDENTITY_TRANSFORM,
    controller_active: bool = False,
    spine_ik_active: bool = False,
    main_analytic_ik_active: bool = False,
    post_physics_foot_ik_active: bool = False,
) -> CollisionPose:
    """Sample and blend the four ordered main KMotFrame lanes.

    Each active selector updates only its authored transform.  Rotation lanes
    use shortest-arc quaternion interpolation and transform 1's retained
    vertical translation uses the same lane weight.  Runtime controller/IK
    branches are explicit preconditions: callers may not silently replace an
    active native branch with identity work.
    """

    ordered = tuple(lanes)
    if len(ordered) != 4:
        raise MotionDecodeError("four_lane_pose", "exactly four ordered lanes are required")
    active_gates = tuple(
        name for name, enabled in (
            ("controller", controller_active),
            ("spine_ik", spine_ik_active),
            ("main_analytic_ik", main_analytic_ik_active),
            ("post_physics_foot_ik", post_physics_foot_ik_active),
        ) if enabled
    )
    if active_gates:
        raise MotionDecodeError(
            "four_lane_pose", "unported native gates active: " + ", ".join(active_gates)
        )

    requested = tuple(sorted(set(int(index) for index in requested_bones)))
    local = list(skeleton.reference_local)
    sampled_any = False
    last_frame = 0.0
    for lane in ordered:
        if not lane.active or lane.raw is None or lane.weight <= 0.0:
            continue
        weight = min(1.0, max(0.0, float(lane.weight)))
        sampled = decode_collision_pose(
            lane.raw,
            skeleton,
            lane.frame,
            range(23),
            clip_index=lane.clip_index,
            offset=lane.offset,
        )
        clip = parse_motion_clip(lane.raw, lane.clip_index, lane.offset)
        layout = _core_selector_layout(clip)
        for bone_index in range(1, 23):
            selector, _byte_offset, authored = layout[bone_index]
            if not authored or selector == 0x06:
                continue
            current = local[bone_index]
            incoming = sampled.local[bone_index]
            rotation = _quat_to_mat3(_quat_slerp(
                _mat3_to_quat(current.rotation),
                _mat3_to_quat(incoming.rotation),
                weight,
            ))
            translation = current.translation
            if bone_index == 1:
                translation = tuple(
                    a + (b - a) * weight
                    for a, b in zip(current.translation, incoming.translation)
                )
            local[bone_index] = PoseTransform(rotation, translation)  # type: ignore[arg-type]
        sampled_any = True
        last_frame = lane.frame
    if not sampled_any:
        raise MotionDecodeError("four_lane_pose", "no active authored lane")

    world = [actor_world]
    for bone_index in range(1, len(local)):
        parent = skeleton.parents[bone_index]
        if parent < 0 or parent >= bone_index:
            raise MotionDecodeError(
                "reference_skeleton", f"compact bone {bone_index} has invalid parent {parent}"
            )
        world.append(compose_transform(world[parent], local[bone_index]))
    return CollisionPose(
        frame=last_frame,
        local=tuple(local),
        world=tuple(world),
        requested_world={index: world[index] for index in requested},
        confidence="native_ordered_four_lane_authored_pose_gates_proven_inactive",
    )


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
        root_yaw_turns = _selector16_yaw_turns(primary.raw_components)
        if blend and primary_index + 1 < clip.encoded_frame_count:
            secondary_words, _ = _decode_frame_words(clip, primary_index + 1)
            secondary = extract_root_channel(clip, secondary_words)
            if secondary.x is None or secondary.y is None or secondary.z is None:
                raise MotionDecodeError("root_channel_missing", "root channel did not produce XYZ")
            x += (secondary.x - x) * blend
            y += (secondary.y - y) * blend
            z += (secondary.z - z) * blend
            root_yaw_turns = _lerp_wrapped_turns(
                root_yaw_turns,
                _selector16_yaw_turns(secondary.raw_components),
                blend,
            )

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
                root_yaw_turns=root_yaw_turns,
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
