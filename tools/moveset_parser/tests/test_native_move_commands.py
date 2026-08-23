from __future__ import annotations

import struct
from pathlib import Path

import pytest

from native_move_commands import (
    parse_cpuai_command_data,
    parse_move_play_command_table,
    parse_transition_command_table,
)


def _cpuai_reaction_fixture() -> bytes:
    command = struct.pack("<2I", 0x00050001, 0x00050005)
    definitions = struct.pack("<I8H", 1, 0, 0, 0, 0, 0, 0, 0, 0)
    sections = [command, definitions, b"", b"", bytearray(800 * 8)]
    struct.pack_into("<HHI", sections[4], 7 * 8, 25, 40, 0x12345678)
    header_size = 4 + (len(sections) + 1) * 4
    offsets = [header_size]
    for section in sections:
        offsets.append(offsets[-1] + len(section))
    return (
        struct.pack(f"<{len(offsets) + 1}I", len(sections), *offsets)
        + b"".join(bytes(section) for section in sections)
    )


def _cpuai_fixture(script: list[int]) -> bytes:
    section0 = struct.pack(f"<{len(script)}I", *script)
    definition = struct.pack("<I8H", 1, 0, 0, 0, 0, 0, 0, 0, 0)
    section0_offset = 0x10
    section1_offset = section0_offset + len(section0)
    end_offset = section1_offset + len(definition)
    return (
        struct.pack("<4I", 2, section0_offset, section1_offset, end_offset)
        + section0
        + definition
    )


def test_parse_linear_move_play_definition():
    table = parse_move_play_command_table(
        _cpuai_fixture(
            [
                0x00080000,
                0x00050001,
                0x00010800,
                3,
                0x00010001,
                15,
                0x00010440,
                3,
                0x00050005,
            ]
        )
    )
    definition = table.definition(0)
    assert definition is not None
    assert definition.authored_definition_id == 0
    assert definition.status == "native-linear"
    assert [(step.mask, step.duration_frames) for step in definition.button_steps] == [
        (0x0800, 3),
        (0x0001, 15),
        (0x0440, 3),
    ]


def test_branched_move_play_definition_fails_closed():
    table = parse_move_play_command_table(
        _cpuai_fixture(
            [
                0x00080000,
                0x00050001,
                0x00050006,
                50,
                2,
                0x00010400,
                2,
                0x00050005,
            ]
        )
    )
    definition = table.definition(0)
    assert definition is not None
    assert definition.status == "native-branched"
    assert definition.branch_cells == (2,)


def test_move_play_table_validates_every_section_offset_and_final_sentinel():
    valid = bytearray(_cpuai_fixture([0x00050001, 0x00050005]))
    struct.pack_into("<I", valid, 12, len(valid) - 1)
    with pytest.raises(ValueError, match="sentinel"):
        parse_move_play_command_table(bytes(valid))

    # A third unused section still belongs to the same outer offset table;
    # malformed offsets there must not escape validation merely because the
    # move-play reader consumes only sections zero and one.
    malformed = struct.pack("<5I", 3, 0x14, 0x14, 0x30, 0x20) + bytes(0x1C)
    with pytest.raises(ValueError, match="not monotonic"):
        parse_move_play_command_table(malformed)


def test_parse_cpuai_reaction_weight_banks():
    parsed = parse_cpuai_command_data(_cpuai_reaction_fixture())
    assert len(parsed.reaction_weight_banks) == 1
    assert parsed.reaction_weight_banks[0][7].normal_weight == 25
    assert parsed.reaction_weight_banks[0][7].alternate_weight == 40
    assert parsed.reaction_weight_banks[0][7].flags_or_filter_word == 0x12345678


def test_parse_transition_command_rows():
    # One 8-byte definition and two native condition rows. Row layout is
    # {initial window, repeat count, word A, word B}.
    data = (
        struct.pack("<5I", 1, 0x14, 0x1C, 0, 0)
        + struct.pack("<4H", 0, 2, 7, 9)
        + struct.pack("<hhHH", 8, 1, 0x1008, 0)
        + struct.pack("<hhHH", 4, 2, 0x0002, 0x3004)
    )
    table = parse_transition_command_table(data)
    command = table.definition(0)
    assert command is not None
    assert (command.auxiliary_word_04, command.auxiliary_word_06) == (7, 9)
    assert [
        (
            row.initial_scan_window,
            row.repeat_count,
            row.condition_word_a,
            row.condition_word_b,
        )
        for row in command.rows
    ] == [(8, 1, 0x1008, 0), (4, 2, 0x0002, 0x3004)]


def test_astaroth_bear_tamer_definition_is_exact_native_linear_script():
    path = Path(__file__).parents[3] / "dump" / "Battle" / "cpu" / "cpuai012.dtp"
    if not path.exists():
        pytest.skip("checked-in Astaroth CPUAI asset is unavailable")
    definition = parse_move_play_command_table(path.read_bytes()).definition(634)
    assert definition is not None
    assert definition.authored_definition_id == 634
    assert definition.initial_cell == 18466
    assert definition.script_end_cell == 18483
    assert definition.status == "native-linear"
    assert [(step.mask, step.duration_frames) for step in definition.button_steps] == [
        (0x0800, 3),  # _B
        (0x0001, 15), # _W
        (0x0440, 3),  # _6_A
        (0x0001, 3),  # _W
    ]
