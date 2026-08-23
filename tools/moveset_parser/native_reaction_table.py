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

    @staticmethod
    def _path_status(move_ids: tuple[int, ...]) -> str:
        promoted = tuple(bool(move_id & 0x8000) for move_id in move_ids)
        if all(promoted):
            return "promoted"
        if any(promoted):
            return "mixed"
        return "ordinary"

    @property
    def base_move_path_status(self) -> str:
        """Bit-15 path for the native standing/base reaction column."""

        return self._path_status(self.base_move_ids_by_facing)

    @property
    def alternate_move_path_status(self) -> str:
        """Bit-15 path for the crouched/alternate reaction column."""

        return self._path_status(self.alternate_move_ids_by_facing)

    @property
    def move_path_status(self) -> str:
        """Classify the native bit-15 reaction-control path across facings.

        ``LuxBattle_ComputeHitReactionParams @ 0x140343B90`` tests bit 15
        before clearing it from the MoveVM id.  Clear ids use the ordinary
        reaction setup; set ids use the promoted classifier-5 path and the
        alternate-posture stun columns.  Mixed rows are kept explicit rather
        than assuming a facing sector during an offline export.
        """

        statuses = {
            self.base_move_path_status,
            self.alternate_move_path_status,
        }
        if len(statuses) != 1:
            return "mixed"
        return next(iter(statuses))


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
