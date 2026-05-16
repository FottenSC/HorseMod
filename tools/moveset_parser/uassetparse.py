"""Minimal UE4 .uasset / .uexp parser, tuned for the SC6 DataAsset files
in `Content/Style/<cid>/DA_MovePlayData_<cid>.uasset` + .uexp.

This is NOT a general-purpose UE4 asset parser. It handles only what
DA_MovePlayData_* needs:
  * FPackageFileSummary (UE4 4.20+ layout, LegacyFileVersion = -7)
  * The package's FName table
  * The export table (enough to identify the data export's tag stream
    location in the .uexp)
  * Tagged FProperty stream containing IntProperty / StructProperty /
    ArrayProperty / BoolProperty / FloatProperty

We DO NOT cover:
  * .uasset texture/mesh/material assets
  * Custom property serialization that doesn't go through tag stream
  * Non-trivial UStruct that uses native serialization

If a future SC6 file uses a feature outside this set, the parser will
raise a clear error rather than silently corrupting data.

Format references (UE4 4.20-ish, since SC6 used UE4 ~4.20):
  * Engine/Source/Runtime/CoreUObject/Public/UObject/PackageFileSummary.h
  * Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectResource.h
"""
from __future__ import annotations

import io
import struct
from dataclasses import dataclass, field
from typing import Any, BinaryIO, Optional


# ---------------------------------------------------------------------------
# Low-level helpers
# ---------------------------------------------------------------------------

PACKAGE_FILE_TAG = 0x9E2A83C1


def _read_i32(f: BinaryIO) -> int:
    return struct.unpack("<i", f.read(4))[0]


def _read_u32(f: BinaryIO) -> int:
    return struct.unpack("<I", f.read(4))[0]


def _read_i64(f: BinaryIO) -> int:
    return struct.unpack("<q", f.read(8))[0]


def _read_u16(f: BinaryIO) -> int:
    return struct.unpack("<H", f.read(2))[0]


def _read_fstring(f: BinaryIO) -> str:
    """Read a UE4 FString: int32 length + N bytes data.
    Length > 0 means ASCII (with trailing null included in length).
    Length < 0 means UTF-16LE (|length| includes the trailing null wide char)."""
    n = _read_i32(f)
    if n == 0:
        return ""
    if n > 0:
        raw = f.read(n)
        return raw.rstrip(b"\x00").decode("ascii", errors="replace")
    raw = f.read(-n * 2)
    return raw.rstrip(b"\x00\x00").decode("utf-16-le", errors="replace")


def _read_fname_index(f: BinaryIO) -> tuple[int, int]:
    """An FName is `(int32 NameIndex, int32 Number)` in the tag stream."""
    name_idx = _read_i32(f)
    number = _read_i32(f)
    return name_idx, number


# ---------------------------------------------------------------------------
# Package summary + tables
# ---------------------------------------------------------------------------

@dataclass
class UAssetPackage:
    name_table: list[str] = field(default_factory=list)
    exports: list["UAssetExport"] = field(default_factory=list)
    imports: list["UAssetImport"] = field(default_factory=list)
    total_header_size: int = 0

    def name(self, idx: int, number: int = 0) -> str:
        """Resolve a name-table reference. `number != 0` is suffixed (UE4
        appends `_<number-1>` for collision-disambiguation, but we don't
        bother since our schemas don't use it)."""
        if 0 <= idx < len(self.name_table):
            return self.name_table[idx]
        return f"<bad-name-{idx}>"


@dataclass
class UAssetExport:
    class_index: int      # signed; references import (-1-N) or export (1+N)
    object_name_idx: int
    object_name_number: int
    serial_size: int      # bytes in .uexp
    serial_offset: int    # offset relative to start of .uexp


@dataclass
class UAssetImport:
    class_package_idx: int
    class_name_idx: int
    outer_index: int
    object_name_idx: int


def parse_uasset(path: str) -> UAssetPackage:
    """Parse the .uasset header + name + import + export tables.
    Tested against SC6 DA_MovePlayData_*.uasset only — other UE4 4.20 era
    .uasset files SHOULD also work but YMMV."""
    with open(path, "rb") as f:
        return _parse_uasset_stream(f)


def _parse_uasset_stream(f: BinaryIO) -> UAssetPackage:
    pkg = UAssetPackage()

    # FPackageFileSummary — UE4 4.20-ish layout.
    tag = _read_u32(f)
    if tag != PACKAGE_FILE_TAG:
        raise ValueError(f"Not a UE4 package: tag 0x{tag:08X} != PACKAGE_FILE_TAG")

    legacy_file_version = _read_i32(f)
    if legacy_file_version != -7:
        raise NotImplementedError(
            f"LegacyFileVersion {legacy_file_version} not supported "
            f"(this parser handles UE4 4.20-ish, -7)"
        )

    # Skip LegacyUE3Version (4), FileVersionUE4 (4), FileVersionLicenseeUE4 (4),
    # CustomVersionArray (4 = empty in SC6's data assets):
    f.read(4)            # LegacyUE3Version
    f.read(4)            # FileVersionUE4
    f.read(4)            # FileVersionLicenseeUE4
    # CustomVersionArray starts here. In SC6's MovePlayData it's zero-count.
    custom_count = _read_i32(f)
    if custom_count != 0:
        # Each entry: FGuid (16 bytes) + int32 version
        f.read(custom_count * (16 + 4))

    pkg.total_header_size = _read_i32(f)
    _ = _read_fstring(f)  # FolderName ("None")
    _ = _read_u32(f)      # PackageFlags

    name_count = _read_i32(f)
    name_offset = _read_i32(f)
    _ = _read_i32(f)      # GatherableTextDataCount
    _ = _read_i32(f)      # GatherableTextDataOffset
    export_count = _read_i32(f)
    export_offset = _read_i32(f)
    import_count = _read_i32(f)
    import_offset = _read_i32(f)
    # We don't need the rest of the header.

    # Name table
    f.seek(name_offset)
    for _ in range(name_count):
        s = _read_fstring(f)
        # In UE4 4.12+, each name is followed by two uint16 hashes.
        f.read(4)
        pkg.name_table.append(s)

    # Import table
    f.seek(import_offset)
    for _ in range(import_count):
        class_pkg_idx = _read_i32(f); _read_i32(f)  # FName: idx + number
        class_name_idx = _read_i32(f); _read_i32(f)
        outer_idx = _read_i32(f)
        object_name_idx = _read_i32(f); _read_i32(f)
        pkg.imports.append(UAssetImport(
            class_package_idx=class_pkg_idx,
            class_name_idx=class_name_idx,
            outer_index=outer_idx,
            object_name_idx=object_name_idx,
        ))

    # Export table
    f.seek(export_offset)
    for _ in range(export_count):
        class_idx = _read_i32(f)
        super_idx = _read_i32(f)        # noqa: F841
        template_idx = _read_i32(f)     # noqa: F841
        outer_idx = _read_i32(f)        # noqa: F841
        object_name_idx = _read_i32(f)
        object_name_number = _read_i32(f)
        object_flags = _read_u32(f)     # noqa: F841
        serial_size = _read_i64(f)
        serial_offset = _read_i64(f)
        # Plenty more fields — but we only need the above. Skip the rest
        # of the export entry. UE4 4.20 ExportTable entry size is 0x68
        # bytes. We've consumed 4+4+4+4+4+4+4+8+8 = 44; remaining = 0x68-44 = 60.
        f.read(0x68 - 44)
        pkg.exports.append(UAssetExport(
            class_index=class_idx,
            object_name_idx=object_name_idx,
            object_name_number=object_name_number,
            serial_size=serial_size,
            serial_offset=serial_offset,
        ))

    return pkg


# ---------------------------------------------------------------------------
# Tagged FProperty stream (the .uexp body)
# ---------------------------------------------------------------------------

@dataclass
class FProperty:
    name: str
    type: str
    raw: Any = None              # decoded value (int, str, dict, list)

    def __repr__(self) -> str:
        return f"{self.name}={self.raw!r}"


def parse_uexp(uexp_path: str, pkg: UAssetPackage,
               export_idx: int = 0) -> dict[str, Any]:
    """Parse the tagged FProperty stream for a single export from .uexp.

    Returns a dict { property_name -> value } where values are:
      * int / float / bool — primitives
      * str — name references or FStrings
      * list[Any] — array contents
      * dict[str, Any] — nested struct
    """
    with open(uexp_path, "rb") as f:
        export = pkg.exports[export_idx]
        # .uexp offsets in the export table are RELATIVE to the start of
        # .uexp (UE4 4.20+ stores them with total_header_size as the base
        # for backwards-compat; we subtract that).
        rel_offset = export.serial_offset - pkg.total_header_size
        f.seek(rel_offset)
        end_offset = rel_offset + export.serial_size
        return _read_property_block(f, pkg, end_offset=end_offset)


def parse_datatable(uexp_path: str, pkg: UAssetPackage,
                    export_idx: int = 0) -> dict[str, dict[str, Any]]:
    """Parse a UE4 `UDataTable` export from .uexp.

    A DataTable export serialises as:
      1. The UObject's tagged FProperty stream (`RowStruct` ObjectProperty
         etc.) terminated by a "None" FName — read by `_read_property_block`.
      2. `int32 NumRows`
      3. For each row: `FName RowName`, then the row struct's tagged
         FProperty stream terminated by "None".

    Returns ``{ rowName -> { fieldName: value } }``. SC6's
    DA_MoveCategoryTable_* / DA_MoveListTable_* use this format; their
    row names are decimal strings ("1", "2", ...).
    """
    with open(uexp_path, "rb") as f:
        export = pkg.exports[export_idx]
        rel_offset = export.serial_offset - pkg.total_header_size
        f.seek(rel_offset)
        end_offset = rel_offset + export.serial_size
        # (1) UObject tagged properties — RowStruct etc.
        _read_property_block(f, pkg, end_offset=end_offset)
        # The gap + NumRows fields need 8 bytes. If the export ends
        # right after the tag stream this is NOT a DataTable (e.g. a
        # plain UDataAsset like DA_MovePlayData) — fail clearly.
        if f.tell() + 8 > end_offset:
            raise ValueError(
                "not a DataTable: no room for gap + NumRows after the "
                "property stream"
            )
        # (2) A 4-byte gap follows the "None" terminator before the row
        # count. Empirically zero in every SC6 DataTable (it's the
        # standard post-SerializeScriptProperties slack UE4 emits for
        # UObject exports). Assert it so a future asset that differs
        # fails loudly rather than mis-parsing the row count.
        gap = _read_i32(f)
        if gap != 0:
            raise ValueError(
                f"DataTable: expected 0 gap before NumRows, got {gap}"
            )
        # (3) NumRows. Bound-check it: a wild value means we are not
        # actually positioned at a DataTable row count (rather than
        # silently looping millions of times reading garbage rows).
        num_rows = _read_i32(f)
        if not (0 <= num_rows <= 100000):
            raise ValueError(
                f"DataTable: implausible NumRows {num_rows} — input is "
                f"probably not a UDataTable export"
            )
        rows: dict[str, dict[str, Any]] = {}
        # (3) per-row { FName RowName, tagged-property block }
        for _ in range(num_rows):
            if f.tell() + 8 > end_offset:
                break
            name_idx, name_number = _read_fname_index(f)
            row_name = pkg.name(name_idx, name_number)
            row = _read_property_block(f, pkg, end_offset=end_offset)
            rows[row_name] = row
        return rows


def _read_property_block(f: BinaryIO, pkg: UAssetPackage,
                         end_offset: Optional[int] = None) -> dict[str, Any]:
    """Read a sequence of FProperty tags until "None" is encountered.
    If `end_offset` is given, stop also when the cursor can't fit a full
    FName (8 bytes) before the bound — some exports trail their data
    with up to 7 bytes of zero padding for alignment, with no terminating
    "None" FName."""
    out: dict[str, Any] = {}
    while True:
        if end_offset is not None and f.tell() + 8 > end_offset:
            break
        prop = _read_property_tag(f, pkg)
        if prop is None:
            break
        out[prop.name] = prop.raw
    return out


def _read_property_tag(f: BinaryIO, pkg: UAssetPackage) -> Optional[FProperty]:
    """Read one FPropertyTag header + its value. Returns None at "None"
    terminator. UE4 FPropertyTag layout:
        FName Name
        FName Type
        int32 Size
        int32 ArrayIndex
        // type-specific tag data (e.g. inner-type FName for arrays)
        // value follows
    """
    name_idx, name_number = _read_fname_index(f)
    name = pkg.name(name_idx, name_number)
    if name == "None":
        return None
    type_idx, _ = _read_fname_index(f)
    prop_type = pkg.name(type_idx)
    size = _read_i32(f)
    _array_index = _read_i32(f)

    # Type-specific tag-data and value parsing
    if prop_type == "IntProperty":
        f.read(1)  # HasGuid byte
        value = _read_i32(f)
        return FProperty(name=name, type=prop_type, raw=value)

    if prop_type == "BoolProperty":
        bool_value = f.read(1)[0]
        f.read(1)  # HasGuid byte
        return FProperty(name=name, type=prop_type, raw=bool_value != 0)

    if prop_type == "FloatProperty":
        f.read(1)  # HasGuid byte
        value = struct.unpack("<f", f.read(4))[0]
        return FProperty(name=name, type=prop_type, raw=value)

    if prop_type == "NameProperty":
        f.read(1)  # HasGuid byte
        idx, num = _read_fname_index(f)
        return FProperty(name=name, type=prop_type, raw=pkg.name(idx, num))

    if prop_type == "StrProperty":
        f.read(1)
        s = _read_fstring(f)
        return FProperty(name=name, type=prop_type, raw=s)

    if prop_type == "ByteProperty":
        # Tag includes the enum-name FName even for raw bytes
        enum_idx, _ = _read_fname_index(f)
        f.read(1)  # HasGuid
        if size == 1:
            value: Any = f.read(1)[0]
        else:
            # enum value as FName
            v_idx, v_num = _read_fname_index(f)
            value = pkg.name(v_idx, v_num)
        return FProperty(name=name, type=prop_type, raw=value)

    if prop_type == "ObjectProperty":
        # FObjectProperty value is an FPackageIndex (int32): >0 export ref,
        # <0 import ref, 0 = null. We don't dereference it — DataTable
        # assets carry an ObjectProperty `RowStruct` we only need to skip.
        f.read(1)  # HasGuid byte
        value = _read_i32(f)
        return FProperty(name=name, type=prop_type, raw=value)

    if prop_type == "StructProperty":
        struct_type_idx, _ = _read_fname_index(f)
        struct_type = pkg.name(struct_type_idx)
        f.read(16)  # Guid (often zero)
        f.read(1)   # HasGuid
        body = _read_property_block(f, pkg)
        body["__struct__"] = struct_type
        return FProperty(name=name, type=prop_type, raw=body)

    if prop_type == "ArrayProperty":
        inner_type_idx, _ = _read_fname_index(f)
        inner_type = pkg.name(inner_type_idx)
        f.read(1)   # HasGuid
        count = _read_i32(f)
        values: list[Any] = []
        if inner_type == "StructProperty":
            if count == 0:
                # Empty struct-array. No inner tag, no element bodies.
                # (Earlier the parser ALWAYS consumed an inner tag here,
                # which over-read past the end of the array by 1 tag-
                # header, knocking the rest of the stream out of sync.)
                pass
            else:
                # The first element's struct property tag (with struct-type
                # metadata) precedes the `count` property-block bodies.
                elem_tag = _read_property_tag(f, pkg)
                if elem_tag is None or elem_tag.type != "StructProperty":
                    raise ValueError(
                        f"ArrayProperty<{name}>: expected leading StructProperty tag"
                    )
                # elem_tag.raw already contains the FIRST element's body.
                values.append(elem_tag.raw)
                for _ in range(count - 1):
                    body = _read_property_block(f, pkg)
                    body["__struct__"] = elem_tag.raw.get("__struct__")
                    values.append(body)
        elif inner_type == "IntProperty":
            for _ in range(count):
                values.append(_read_i32(f))
        elif inner_type == "FloatProperty":
            for _ in range(count):
                values.append(struct.unpack("<f", f.read(4))[0])
        elif inner_type == "NameProperty":
            for _ in range(count):
                idx, num = _read_fname_index(f)
                values.append(pkg.name(idx, num))
        elif inner_type == "ByteProperty":
            for _ in range(count):
                values.append(f.read(1)[0])
        else:
            raise NotImplementedError(
                f"ArrayProperty<{name}> with inner type {inner_type!r} "
                f"not supported"
            )
        return FProperty(name=name, type=prop_type, raw=values)

    raise NotImplementedError(
        f"FProperty type {prop_type!r} (name={name!r}) at offset 0x{f.tell():X} "
        f"is not implemented"
    )
