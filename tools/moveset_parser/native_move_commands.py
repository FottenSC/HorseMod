"""Static parsers for SC6's shipped move-play and command-input tables.

``cpuaiNNN.dtp`` section zero is the 32-bit move-play command stream and
section one begins with a count followed by 0x10-byte definitions indexed by
``DA_MovePlayData.CommandSets[*].MainIndex``.  The layout is bound by
``LuxMoveVM_InitCharaFromMoveTable @ 0x140309B20`` and consumed by
``LuxMoveSystem_StartMoveForChara @ 0x14031C610``.

``Battle/hdr/command.dat`` contains the transition-condition rows consumed by
``LuxBattle_EvaluateMoveTransitionConditions @ 0x140312F80``.  Parsing it here
keeps both sides of the eventual static input-to-KHD join native-only.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct


CPUAI_DEFINITION_SIZE = 0x10
CONTROL_BRANCH_OPCODES = frozenset((0x00050003, 0x00050006, 0x00050008))


@dataclass(frozen=True)
class MovePlayButtonStep:
    cell_index: int
    mask: int
    duration_frames: int


@dataclass(frozen=True)
class MovePlayDefinition:
    definition_id: int
    authored_definition_id: int
    initial_cell: int
    raw_words: tuple[int, ...]
    region_end_cell: int
    script_end_cell: int | None
    status: str
    button_steps: tuple[MovePlayButtonStep, ...]
    branch_cells: tuple[int, ...]

    @property
    def is_linear(self) -> bool:
        return self.status == "native-linear"


@dataclass(frozen=True)
class MovePlayCommandTable:
    cells: tuple[int, ...]
    definitions: tuple[MovePlayDefinition, ...]

    def definition(self, index: int) -> MovePlayDefinition | None:
        if not 0 <= index < len(self.definitions):
            return None
        return self.definitions[index]


def parse_move_play_command_table(data: bytes) -> MovePlayCommandTable:
    """Parse the two CPUAI sections used by the move-play command driver.

    A definition's conservative region ends at the next distinct authored
    initial-cell cursor.  Only a region with one start, one end, and no native
    branch opcode is labelled ``native-linear``; all other shapes fail closed.
    """

    if len(data) < 12:
        raise ValueError("CPUAI move table is too small")
    section_count = struct.unpack_from("<I", data, 0)[0]
    if section_count < 2:
        raise ValueError("CPUAI move table requires sections 0 and 1")
    header_end = 4 + (section_count + 1) * 4
    if header_end > len(data):
        raise ValueError("CPUAI section-offset table exceeds the file")
    offsets = struct.unpack_from(f"<{section_count + 1}I", data, 4)
    if offsets[0] < header_end:
        raise ValueError("CPUAI first section starts inside the offset table")
    if any(a > b for a, b in zip(offsets, offsets[1:])):
        raise ValueError("CPUAI section offsets are not monotonic")
    if any(offset > len(data) for offset in offsets):
        raise ValueError("CPUAI section offset exceeds the file")
    if offsets[-1] != len(data):
        raise ValueError("CPUAI final section sentinel does not equal file size")
    command_start, definition_start, definition_end = offsets[:3]
    if not (
        header_end <= command_start <= definition_start <= definition_end <= len(data)
    ):
        raise ValueError("CPUAI move-command sections are not monotonic")
    command_size = definition_start - command_start
    if command_size % 4:
        raise ValueError("CPUAI command-cell section is not uint-aligned")
    cells = struct.unpack_from(f"<{command_size // 4}I", data, command_start)

    if definition_end - definition_start < 4:
        raise ValueError("CPUAI move-definition section is truncated")
    definition_count = struct.unpack_from("<I", data, definition_start)[0]
    expected_end = definition_start + 4 + definition_count * CPUAI_DEFINITION_SIZE
    if expected_end > definition_end:
        raise ValueError("CPUAI move-definition array exceeds section 1")
    raw_definitions = tuple(
        struct.unpack_from("<8H", data, definition_start + 4 + index * 0x10)
        for index in range(definition_count)
    )
    cursors = sorted({words[1] for words in raw_definitions if words[1] < len(cells)})
    next_cursor = {
        cursor: cursors[index + 1] if index + 1 < len(cursors) else len(cells)
        for index, cursor in enumerate(cursors)
    }

    definitions: list[MovePlayDefinition] = []
    for index, words in enumerate(raw_definitions):
        cursor = words[1]
        if cursor >= len(cells):
            definitions.append(
                MovePlayDefinition(
                    index, words[0], cursor, words, cursor, None,
                    "cursor-out-of-range", (), ()
                )
            )
            continue
        region_end = next_cursor[cursor]
        region = cells[cursor:region_end]
        starts = [cursor + i for i, value in enumerate(region) if value == 0x00050001]
        ends = [cursor + i for i, value in enumerate(region) if value == 0x00050005]
        branches = tuple(
            cursor + i for i, value in enumerate(region) if value in CONTROL_BRANCH_OPCODES
        )
        script_end = ends[0] if ends else None
        if words[0] != index:
            status = "definition-id-mismatch"
            scan_end = region_end
        elif len(starts) == 1 and len(ends) == 1 and not branches and starts[0] < ends[0]:
            status = "native-linear"
            scan_end = ends[0]
        elif starts and ends:
            status = "native-branched"
            scan_end = ends[0]
        else:
            status = "malformed"
            scan_end = region_end

        steps: list[MovePlayButtonStep] = []
        cell_index = cursor
        while cell_index < scan_end:
            value = cells[cell_index]
            if value & 0xFFFF0000 == 0x00010000:
                if cell_index + 1 >= region_end:
                    status = "malformed"
                    break
                duration = cells[cell_index + 1]
                steps.append(MovePlayButtonStep(cell_index, value & 0xFFFF, duration))
                cell_index += 2
                continue
            cell_index += 1
        definitions.append(
            MovePlayDefinition(
                definition_id=index,
                authored_definition_id=words[0],
                initial_cell=cursor,
                raw_words=words,
                region_end_cell=region_end,
                script_end_cell=script_end,
                status=status,
                button_steps=tuple(steps),
                branch_cells=branches,
            )
        )
    return MovePlayCommandTable(tuple(cells), tuple(definitions))


@dataclass(frozen=True)
class TransitionConditionRow:
    initial_scan_window: int
    repeat_count: int
    condition_word_a: int
    condition_word_b: int


@dataclass(frozen=True)
class TransitionCommandDefinition:
    command_id: int
    first_row: int
    row_count: int
    auxiliary_word_04: int
    auxiliary_word_06: int
    rows: tuple[TransitionConditionRow, ...]


@dataclass(frozen=True)
class TransitionCommandTable:
    definitions: tuple[TransitionCommandDefinition, ...]

    def definition(self, command_id: int) -> TransitionCommandDefinition | None:
        if not 0 <= command_id < len(self.definitions):
            return None
        return self.definitions[command_id]


def parse_transition_command_table(data: bytes) -> TransitionCommandTable:
    """Parse ``Battle/hdr/command.dat`` without interpreting auxiliary data."""

    if len(data) < 0x14:
        raise ValueError("command.dat is too small")
    count, definition_offset, row_offset = struct.unpack_from("<III", data, 0)
    if not (0x14 <= definition_offset <= row_offset <= len(data)):
        raise ValueError("command.dat table offsets are invalid")
    if definition_offset + count * 8 > row_offset:
        raise ValueError("command.dat definition array overlaps condition rows")
    definitions: list[TransitionCommandDefinition] = []
    for command_id in range(count):
        first_row, row_count, aux04, aux06 = struct.unpack_from(
            "<4H", data, definition_offset + command_id * 8
        )
        rows_end = row_offset + (first_row + row_count) * 8
        if rows_end > len(data):
            raise ValueError(f"command.dat command {command_id} rows exceed the file")
        rows = tuple(
            TransitionConditionRow(
                *struct.unpack_from("<hhHH", data, row_offset + (first_row + i) * 8)
            )
            for i in range(row_count)
        )
        definitions.append(
            TransitionCommandDefinition(
                command_id, first_row, row_count, aux04, aux06, rows
            )
        )
    return TransitionCommandTable(tuple(definitions))
