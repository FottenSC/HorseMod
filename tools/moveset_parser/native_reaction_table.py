"""Parser for the native common ``Battle/hdr/yarare.dat`` table.

The table is bound to both battle characters by
``LuxBattle_BindCommandDirectoryToCharaSlots``.  Each 0x14-byte row maps a
KHD reaction-row id to four facing-sector MoveVM ids for the base posture and
four for the alternate posture.  The leading two words in each row are kept
opaque until their consumers are identified.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct


@dataclass(frozen=True)
class LuxHitReactionMoveIdRow:
    metadata_words: tuple[int, int]
    base_move_ids_by_facing: tuple[int, int, int, int]
    alternate_move_ids_by_facing: tuple[int, int, int, int]

    @property
    def all_move_ids(self) -> tuple[int, ...]:
        return self.base_move_ids_by_facing + self.alternate_move_ids_by_facing


@dataclass(frozen=True)
class LuxHitReactionMoveIdTable:
    rows: tuple[LuxHitReactionMoveIdRow, ...]
    sha256: str

    def row(self, reaction_row_id: int) -> LuxHitReactionMoveIdRow | None:
        if not 0 <= reaction_row_id < len(self.rows):
            return None
        return self.rows[reaction_row_id]


def parse_hit_reaction_move_id_table(data: bytes) -> LuxHitReactionMoveIdTable:
    """Parse the exact count-header plus 0x14-byte row format.

    Rejecting trailing or truncated bytes prevents a mismatched common asset
    from silently shifting every reaction-row lookup.
    """

    if len(data) < 4:
        raise ValueError("yarare.dat is shorter than its uint count header")
    count = struct.unpack_from("<I", data, 0)[0]
    expected_size = 4 + count * 0x14
    if len(data) != expected_size:
        raise ValueError(
            f"yarare.dat size mismatch: count={count} requires "
            f"{expected_size} bytes, got {len(data)}"
        )
    rows: list[LuxHitReactionMoveIdRow] = []
    for row_index in range(count):
        words = struct.unpack_from("<10H", data, 4 + row_index * 0x14)
        rows.append(
            LuxHitReactionMoveIdRow(
                metadata_words=(words[0], words[1]),
                base_move_ids_by_facing=tuple(words[2:6]),
                alternate_move_ids_by_facing=tuple(words[6:10]),
            )
        )
    return LuxHitReactionMoveIdTable(
        rows=tuple(rows),
        sha256=hashlib.sha256(data).hexdigest().upper(),
    )
