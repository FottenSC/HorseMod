"""Minimal read-only PE virtual-address mapper for static executable evidence."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct


@dataclass(frozen=True)
class PeSection:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int


class PeStaticImage:
    def __init__(self, data: bytes) -> None:
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise ValueError("not a DOS/PE image")
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise ValueError("missing PE signature")
        coff = pe_offset + 4
        section_count = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, coff + 16)[0]
        optional = coff + 20
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic != 0x20B:
            raise ValueError(f"expected PE32+ optional header, got 0x{magic:04X}")
        self.image_base = struct.unpack_from("<Q", data, optional + 24)[0]
        section_table = optional + optional_size
        sections = []
        for index in range(section_count):
            row = section_table + index * 40
            name = data[row : row + 8].split(b"\0", 1)[0].decode("ascii", "replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, row + 8
            )
            sections.append(
                PeSection(name, virtual_address, virtual_size, raw_offset, raw_size)
            )
        self.data = data
        self.sections = tuple(sections)

    @classmethod
    def from_path(cls, path: Path) -> "PeStaticImage":
        return cls(path.resolve(strict=True).read_bytes())

    def read_va(self, virtual_address: int, size: int) -> bytes:
        if size < 0:
            raise ValueError("size must be nonnegative")
        rva = virtual_address - self.image_base
        if rva < 0:
            raise ValueError("virtual address precedes image base")
        for section in self.sections:
            span = max(section.virtual_size, section.raw_size)
            if section.virtual_address <= rva and rva + size <= section.virtual_address + span:
                relative = rva - section.virtual_address
                if relative + size > section.raw_size:
                    raise ValueError("requested virtual bytes are not backed by file data")
                begin = section.raw_offset + relative
                end = begin + size
                if end > len(self.data):
                    raise ValueError("section raw range exceeds file size")
                return self.data[begin:end]
        raise ValueError(f"VA 0x{virtual_address:X}+0x{size:X} is outside PE sections")
