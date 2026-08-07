"""Instruction-faithful Lux compact-input codec and snapshot derivation.

The tables are read from the exact PE instead of copied into source.  Native
assembly source is ``LuxBattle_TickCharaInput @ 0x140312AC3..0x140312D4A``.
Optional transform streams operate between :func:`encode_input_word` and
:func:`decode_input_word`; callers must supply their resulting encoded word
explicitly rather than silently skipping a configured transform.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct

from lux_input_history import CurrentInputSnapshot
from pe_static_image import PeStaticImage


@dataclass(frozen=True)
class LuxInputCodecTables:
    compact_to_7bit: bytes
    seven_bit_to_compact: bytes
    direction_remap: bytes
    nibble_to_decoded_id: bytes
    direction_mask_by_decoded_id: tuple[int, ...]

    @classmethod
    def from_executable(cls, path: Path) -> "LuxInputCodecTables":
        image = PeStaticImage.from_path(path)
        if image.image_base != 0x140000000:
            raise ValueError(f"unexpected SC6 image base 0x{image.image_base:X}")
        mask_bytes = image.read_va(0x143E84348, 20)
        return cls(
            compact_to_7bit=image.read_va(0x143E83ED0, 256),
            seven_bit_to_compact=image.read_va(0x143E83DD0, 128),
            direction_remap=image.read_va(0x143E843F0, 16),
            nibble_to_decoded_id=image.read_va(0x143E84400, 16),
            direction_mask_by_decoded_id=struct.unpack("<10H", mask_bytes),
        )

    def __post_init__(self) -> None:
        expected = (256, 128, 16, 16, 10)
        actual = (
            len(self.compact_to_7bit),
            len(self.seven_bit_to_compact),
            len(self.direction_remap),
            len(self.nibble_to_decoded_id),
            len(self.direction_mask_by_decoded_id),
        )
        if actual != expected:
            raise ValueError(f"invalid Lux input codec table dimensions {actual}")


def encode_input_word(
    current_compact_word: int,
    secondary_compact_word: int,
    input_side: int,
    tables: LuxInputCodecTables,
) -> int:
    current = current_compact_word & 0xFFFFFFFF
    secondary = secondary_compact_word & 0xFFFFFFFF
    if input_side not in (0, 1):
        raise ValueError("input_side must be 0 or 1")

    primary_high_code = tables.compact_to_7bit[(current >> 6) & 0xF0] & 0x7F
    primary_low_code = tables.compact_to_7bit[(current & 0xFF) << 4 & 0xFF] & 0x7F

    high_compact = tables.seven_bit_to_compact[primary_high_code]
    high_codec_index = high_compact ^ ((high_compact ^ (secondary >> 10)) & 0x0F)
    encoded_high = tables.compact_to_7bit[high_codec_index] & 0x7F

    low_compact = tables.seven_bit_to_compact[primary_low_code]
    low_codec_index = low_compact ^ ((low_compact ^ secondary) & 0x0F)
    encoded_low = tables.compact_to_7bit[low_codec_index] & 0x7F
    return encoded_high | (encoded_low << 7) | (input_side << 14)


def decode_input_word(encoded_word: int, tables: LuxInputCodecTables) -> tuple[int, int, int]:
    encoded = encoded_word & 0xFFFF
    high_compact = tables.seven_bit_to_compact[encoded & 0x7F]
    low_compact = tables.seven_bit_to_compact[(encoded >> 7) & 0x7F]
    current = ((high_compact & 0xF0) << 6) | (low_compact >> 4)
    secondary = ((high_compact & 0x0F) << 10) | (low_compact & 0x0F)
    return current, secondary, (encoded >> 14) & 1


def derive_current_snapshot(
    current_compact_word: int,
    secondary_compact_word: int,
    input_side: int,
    tables: LuxInputCodecTables,
    *,
    previous_compact_word: int = 0,
) -> CurrentInputSnapshot:
    current = current_compact_word & 0xFFFFFFFF
    secondary = secondary_compact_word & 0xFFFFFFFF
    high_nibble = (current >> 10) & 0x0F
    secondary_high_nibble = (secondary >> 10) & 0x0F
    decoded = tables.nibble_to_decoded_id[high_nibble]
    secondary_decoded = tables.nibble_to_decoded_id[secondary_high_nibble]
    for _ in range(2 if input_side == 0 else 6):
        decoded = tables.direction_remap[decoded]
        secondary_decoded = tables.direction_remap[secondary_decoded]
    if decoded >= len(tables.direction_mask_by_decoded_id):
        raise ValueError(f"decoded primary direction ID {decoded} is outside mask table")
    if secondary_decoded >= len(tables.direction_mask_by_decoded_id):
        raise ValueError(f"decoded secondary direction ID {secondary_decoded} is outside mask table")
    return CurrentInputSnapshot(
        current_compact_word=current,
        previous_compact_word=previous_compact_word & 0xFFFFFFFF,
        secondary_compact_word=secondary,
        decoded_high_nibble_input_id=tables.nibble_to_decoded_id[high_nibble],
        high_input_nibble=high_nibble,
        decoded_secondary_high_nibble_input_id=tables.nibble_to_decoded_id[
            secondary_high_nibble
        ],
        secondary_high_input_nibble=secondary_high_nibble,
        side_decoded_input_id=decoded,
        side_direction_mask=tables.direction_mask_by_decoded_id[decoded],
        side_decoded_secondary_input_id=secondary_decoded,
        side_secondary_direction_mask=tables.direction_mask_by_decoded_id[
            secondary_decoded
        ],
    )
