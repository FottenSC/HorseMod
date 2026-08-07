from pathlib import Path

import pytest

from lux_input_transform_vtables import (
    discover_input_transform_vtables,
    discover_provider_constructions,
)


SC6_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
)


def test_exact_executable_has_only_the_three_verified_transform_vtables() -> None:
    if not SC6_EXE.exists():
        pytest.skip("exact SC6 executable is not available")
    found = discover_input_transform_vtables(SC6_EXE)
    assert [(item.address, item.provider_kind, item.scalar_destructor) for item in found] == [
        (0x143E89470, "cyclic", 0x14037F440),
        (0x143E894C8, "linear", 0x14037F3C0),
        (0x143E89578, "linear", 0x14037F4C0),
    ]
    assert [item.method_slots_28_to_48 for item in found] == [
        (
            0x14037EFF0,
            0x14037EEC0,
            0x14037EE80,
            0x14037EE70,
            0x14037EC90,
        ),
        (
            0x14037ED20,
            0x14037ED00,
            0x14037ECC0,
            0x14037ECA0,
            0x14037EC90,
        ),
        (
            0x14037ED20,
            0x14037ED00,
            0x14037ECC0,
            0x14037ECA0,
            0x14037EC90,
        ),
    ]


def test_serialization_owner_constructs_the_exact_provider_closure() -> None:
    if not SC6_EXE.exists():
        pytest.skip("exact SC6 executable is not available")
    found = discover_provider_constructions(SC6_EXE)
    assert [
        (
            item.constructor,
            item.vtable,
            item.provider_kind,
            item.authored_record_kind,
            item.object_size,
            item.vector_stride,
            item.instance_count,
            item.owner_offset,
        )
        for item in found
    ] == [
        (0x14037EE20, 0x143E894C8, "linear", "battle-fixed-key", 0x24, 0x28, 18, 0x30),
        (0x14037F310, 0x143E89578, "linear", "header-parity", 0x24, 0x28, 9, 0x300),
        (0x14037F2E0, 0x143E89470, "cyclic", "battle-fixed-key", 0x2C, 0x30, 2, 0xF0538),
    ]
