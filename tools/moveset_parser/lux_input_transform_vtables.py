"""Discover the complete SC6 ushort input-transform record-buffer vtables.

The transform interface is indirect in ``LuxBattle_TickCharaInput``.  This
read-only scanner identifies every vtable whose slots +0x28..+0x48 match the
Ghidra-verified linear or cyclic ushort record-buffer methods.  The exact set
is part of the executable-bound static evidence instead of a handwritten
assumption.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct

from pe_static_image import PeStaticImage


LINEAR_METHODS = (
    0x14037ED20,
    0x14037ED00,
    0x14037ECC0,
    0x14037ECA0,
    0x14037EC90,
)
CYCLIC_METHODS = (
    0x14037EFF0,
    0x14037EEC0,
    0x14037EE80,
    0x14037EE70,
    0x14037EC90,
)


@dataclass(frozen=True)
class InputTransformVtable:
    address: int
    provider_kind: str
    scalar_destructor: int
    method_slots_28_to_48: tuple[int, ...]


@dataclass(frozen=True)
class InputTransformProviderConstruction:
    """One executable-proven provider array owned by the battle serializer."""

    constructor: int
    vtable: int
    provider_kind: str
    authored_record_kind: str
    object_size: int
    vector_stride: int
    instance_count: int
    owner_offset: int


SERIALIZATION_SYSTEM_INITIALIZER = 0x14037D280


def _require_bytes(image: PeStaticImage, address: int, expected: bytes, label: str) -> None:
    actual = image.read_va(address, len(expected))
    if actual != expected:
        raise ValueError(
            f"{label} byte contract changed at 0x{address:X}: "
            f"expected {expected.hex()}, got {actual.hex()}"
        )


def _rip_relative_target(image: PeStaticImage, address: int, prefix: bytes) -> int:
    encoded = image.read_va(address, len(prefix) + 4)
    if encoded[: len(prefix)] != prefix:
        raise ValueError(
            f"RIP-relative instruction changed at 0x{address:X}: "
            f"expected prefix {prefix.hex()}, got {encoded.hex()}"
        )
    displacement = struct.unpack_from("<i", encoded, len(prefix))[0]
    return address + len(encoded) + displacement


def discover_provider_constructions(
    path: Path,
) -> tuple[InputTransformProviderConstruction, ...]:
    """Decode the sole native owner and exact array geometry of each provider.

    The vtable signature scan proves which concrete method families exist.  The
    constructor closure independently proves how many objects are instantiated,
    their object/stride sizes, their record specialization, and where the battle
    serialization system owns them.  This deliberately fails closed if the
    executable's instruction sequence changes.
    """

    image = PeStaticImage.from_path(path)

    # The owner establishes a shared player count of two in R14D.  The cyclic
    # vector constructor later passes that register as its element count.
    _require_bytes(image, 0x14037D2A1, bytes.fromhex("41BE02000000"), "player count")

    # Linear BattleFixedKey array: RCX = owner+0x30, stride 0x28, count
    # 0x28-0x16 = 0x12.  Decode both immediates rather than storing the count.
    _require_bytes(image, 0x14037D2D1, bytes.fromhex("488D7930"), "fixed-key owner offset")
    fixed_ctor = _rip_relative_target(image, 0x14037D2E1, bytes.fromhex("4C8D0D"))
    fixed_stride = struct.unpack("<I", image.read_va(0x14037D2E9, 4))[0]
    _require_bytes(image, 0x14037D2E8, b"\xBA", "fixed-key stride opcode")
    fixed_count_disp = struct.unpack("<b", image.read_va(0x14037D2F0, 1))[0]
    _require_bytes(image, 0x14037D2ED, bytes.fromhex("448D42"), "fixed-key count expression")
    fixed_count = fixed_stride + fixed_count_disp

    # Linear HeaderParity array: owner+0x300, same 0x28 stride, count
    # 0x28-0x1F = 9.
    _require_bytes(
        image, 0x14037D2FA, bytes.fromhex("498D8C2400030000"), "parity owner offset"
    )
    parity_ctor = _rip_relative_target(image, 0x14037D316, bytes.fromhex("4C8D0D"))
    _require_bytes(image, 0x14037D31D, b"\xBA", "parity stride opcode")
    parity_stride = struct.unpack("<I", image.read_va(0x14037D31E, 4))[0]
    _require_bytes(image, 0x14037D322, bytes.fromhex("448D42"), "parity count expression")
    parity_count_disp = struct.unpack("<b", image.read_va(0x14037D325, 1))[0]
    parity_count = parity_stride + parity_count_disp

    # Cyclic BattleFixedKey array: owner+0xF0538, 0x30 stride, R14 count (two).
    _require_bytes(
        image,
        0x14037D3B1,
        bytes.fromhex("498D8C2438050F00"),
        "cyclic owner offset",
    )
    cyclic_ctor = _rip_relative_target(image, 0x14037D3CD, bytes.fromhex("4C8D0D"))
    _require_bytes(image, 0x14037D3D4, bytes.fromhex("4D8BC6"), "cyclic count source")
    _require_bytes(image, 0x14037D3D7, b"\xBA", "cyclic stride opcode")
    cyclic_stride = struct.unpack("<I", image.read_va(0x14037D3D8, 4))[0]

    # Each constructor independently writes its class vtable.  This links the
    # owner scan to the method-family scan without trusting symbol names.
    fixed_vtable = _rip_relative_target(image, 0x14037EE35, bytes.fromhex("488D05"))
    parity_vtable = _rip_relative_target(image, 0x14037F325, bytes.fromhex("488D05"))
    cyclic_vtable = _rip_relative_target(image, 0x14037F2E2, bytes.fromhex("488D05"))

    result = (
        InputTransformProviderConstruction(
            fixed_ctor,
            fixed_vtable,
            "linear",
            "battle-fixed-key",
            0x24,
            fixed_stride,
            fixed_count,
            0x30,
        ),
        InputTransformProviderConstruction(
            parity_ctor,
            parity_vtable,
            "linear",
            "header-parity",
            0x24,
            parity_stride,
            parity_count,
            0x300,
        ),
        InputTransformProviderConstruction(
            cyclic_ctor,
            cyclic_vtable,
            "cyclic",
            "battle-fixed-key",
            0x2C,
            cyclic_stride,
            2,
            0xF0538,
        ),
    )
    expected = (
        (0x14037EE20, 0x143E894C8, 0x28, 0x12),
        (0x14037F310, 0x143E89578, 0x28, 9),
        (0x14037F2E0, 0x143E89470, 0x30, 2),
    )
    actual = tuple(
        (item.constructor, item.vtable, item.vector_stride, item.instance_count)
        for item in result
    )
    if actual != expected:
        raise ValueError(f"input-transform provider construction closure changed: {actual!r}")
    return result


def discover_input_transform_vtables(path: Path) -> tuple[InputTransformVtable, ...]:
    image = PeStaticImage.from_path(path)
    output: list[InputTransformVtable] = []
    for section in image.sections:
        if section.name not in (".rdata", ".data"):
            continue
        raw = image.data[section.raw_offset : section.raw_offset + section.raw_size]
        # Need slot zero through slot nine. Vtables are qword aligned.
        for offset in range(0, max(0, len(raw) - 0x50 + 1), 8):
            entries = struct.unpack_from("<10Q", raw, offset)
            methods = tuple(entries[5:10])
            kind = None
            if methods == LINEAR_METHODS:
                kind = "linear"
            elif methods == CYCLIC_METHODS:
                kind = "cyclic"
            if kind is not None:
                output.append(
                    InputTransformVtable(
                        image.image_base + section.virtual_address + offset,
                        kind,
                        entries[0],
                        methods,
                    )
                )
    return tuple(output)
