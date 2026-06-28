from __future__ import annotations

import struct

from export_webui_data import event_record_to_dict, slot_to_dict, throw_to_dict
from luxformats import parse_khd


def _synthetic_kh11() -> bytes:
    attack_off = 0x80
    throw_off = attack_off + 0x70 * 4
    event_off = throw_off + 6 * 2
    data = bytearray(event_off + 0x30 * 2)

    data[:4] = b"KH11"
    struct.pack_into("<HH", data, 0x0C, 1, 2)  # one slot, two event records
    struct.pack_into("<3I", data, 0x10, attack_off, throw_off, event_off)
    struct.pack_into("<8H", data, 0x1C, 0, 1, 1, 0, 1, 0, 1, 0)

    # One FLuxMoveBankSlotView at bank+0x30. Variant refs include one attack
    # cell and two throw-cell refs using the engine's 0x1000 partition bit.
    slot = 0x30
    struct.pack_into("<HHhH", data, slot, 3, 0, 0, 0)
    struct.pack_into("<II", data, slot + 0x10, 0, 0)
    struct.pack_into("<I", data, slot + 0x1C, 0)
    struct.pack_into("<QQ", data, slot + 0x20, 0, 0)
    struct.pack_into("<fHHI", data, slot + 0x30, 120.0, 42, 5, 0)
    struct.pack_into("<6h", data, slot + 0x3C, 1, 0x1000, 0x1001, -1, -1, -1)

    for i in range(4):
        data[attack_off + i * 0x70 + 0x5F] = 0

    struct.pack_into("<Hhh", data, throw_off, 55, -3, 100)
    struct.pack_into("<Hhh", data, throw_off + 6, 70, -3, 80)

    struct.pack_into("<BBHI", data, event_off, 0xD6, 0, 0, 1)
    struct.pack_into("<II", data, event_off + 0x08, 0x1234, 0xA5)
    struct.pack_into("<3f", data, event_off + 0x10, 1.25, -2.5, 3.75)
    struct.pack_into("<3I", data, event_off + 0x1C, 0x1C, 0x20, 0x24)
    struct.pack_into("<fI", data, event_off + 0x28, 0.5, 0x2C)
    struct.pack_into("<BBHI", data, event_off + 0x30, 0x05, 1, 0, 2)
    return bytes(data)


def test_parse_khd_throw_and_event_records():
    k = parse_khd(_synthetic_kh11())

    assert k.move_count == 1
    assert k.event_record_count == 2
    assert k.event_record_table_offset == k.section_offsets[2]

    slot = k.slots[0]
    assert slot.total_frames == 42
    assert slot.flPlaybackSpeed60ths_30 == 120.0
    assert slot.playback_speed_scalar == 2.0
    assert slot.attack_cell_indices == [1]
    assert slot.throw_cell_indices == [0, 1]
    assert k.cell_to_slots == {1: [(0, 0)]}
    assert k.throw_to_slots == {0: [(0, 1)], 1: [(0, 2)]}
    assert k.resolve_packed_slot(0x0800) == 0

    assert [t.wDamage for t in k.sections[1].throw_cells] == [55, 70]
    assert len(k.sections[2].event_records) == 2
    assert k.sections[2].event_records_end == 0x60
    assert [r.dwKey for r in k.sections[2].event_records] == [0xD6, 0x105]
    assert [r.dwPackedMoveId for r in k.sections[2].event_records] == [0xD6, 0x105]
    assert [r.dwEventKind for r in k.sections[2].event_records] == [1, 2]
    assert [r.type_tag for r in k.sections[2].event_records] == [0xD6, 0x05]
    first = k.sections[2].event_records[0]
    assert first.dwField08 == 0x1234
    assert first.dwShapeFlags == 0xA5
    assert first.flOffsetX == 1.25
    assert first.flOffsetY == -2.5
    assert first.flOffsetZ == 3.75
    assert first.dwField1C == 0x1C
    assert first.dwField20 == 0x20
    assert first.dwField24 == 0x24
    assert first.flRadiusScale == 0.5
    assert first.dwField2C == 0x2C


def test_export_helpers_include_throw_slot_and_event_fields():
    k = parse_khd(_synthetic_kh11())

    slot_payload = slot_to_dict(k.slots[0])
    assert slot_payload["animLength"] == 42
    assert slot_payload["totalFrames"] == 42
    assert slot_payload["playbackSpeed60ths"] == 120.0
    assert slot_payload["playbackSpeed"] == 2.0
    assert slot_payload["attackCellRefs"] == [1]
    assert slot_payload["throwCellRefs"] == [0, 1]

    throw_payload = throw_to_dict(k.sections[1].throw_cells[0], 0)
    assert throw_payload == {"idx": 0, "damage": 55, "aux": -3, "scaling": 100}

    event_payload = event_record_to_dict(k.sections[2].event_records[0])
    assert event_payload["idx"] == 0
    assert event_payload["key"] == 0xD6
    assert event_payload["packedMoveId"] == 0xD6
    assert event_payload["resolvedSlot"] is None
    assert event_payload["eventKind"] == 1
    assert event_payload["eventKindName"] == "EventKind_1"
    assert event_payload["field08"] == 0x1234
    assert event_payload["shapeFlags"] == 0xA5
    assert event_payload["offsetX"] == 1.25
    assert event_payload["offsetY"] == -2.5
    assert event_payload["offsetZ"] == 3.75
    assert event_payload["field1C"] == 0x1C
    assert event_payload["field20"] == 0x20
    assert event_payload["field24"] == 0x24
    assert event_payload["radiusScale"] == 0.5
    assert event_payload["field2C"] == 0x2C
    assert event_payload["typeTag"] == 0xD6
    assert event_payload["typeName"] == "Header_CountMarker"
