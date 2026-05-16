"""CLI for the SC6 moveset parsers in `luxformats.py`."""

from __future__ import annotations

import argparse
import os
import sys
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from luxformats import (
    KhdFile, OffsetTableFile, VtbFile, LpdFile, LpbBlock, HitFile,
    parse_auto, parse_khd, parse_mot, parse_dtp, parse_vtb, parse_lpd,
    parse_lpb, parse_hit_dat, MOVE_TYPE_NAMES,
)


def fmt_size(n: int) -> str:
    if n >= 1 << 20:
        return f"{n / (1 << 20):.2f} MB"
    if n >= 1 << 10:
        return f"{n / (1 << 10):.2f} KB"
    return f"{n} B"


def dump_khd(k: KhdFile, *, show_entries: int = 16, show_raw: bool = False) -> None:
    print(f"KHD file  magic={k.magic.decode()!r}  field_0C=0x{k.field_0c:08X}")
    print(f"  section offsets: " + ", ".join(f"0x{o:08X}" for o in k.section_offsets))
    print(f"  trailer (+0x1C..0x{0x1C + len(k.trailer_data):X}): {len(k.trailer_data)} bytes")
    for s in k.sections:
        if s.entry_count > 0:
            kind = f"entries={s.entry_count} (0x70 stride)"
        elif s.detected_stride:
            kind = f"records={s.detected_count} (stride 0x{s.detected_stride:X}, format TBD)"
        else:
            kind = "unidentified layout"
        print(f"\n  Section {s.section_index}: offset=0x{s.offset:08X} size=0x{s.size:X}  {kind}")
        for i, e in enumerate(s.entries[:show_entries]):
            name = f"  name={e.name!r}" if e.name else ""
            extra = ""
            if e.type_tag in (0x0B, 0x1E):
                extra = f"  set1={e.motion_ids_set1}  set2={e.motion_ids_set2}"
            elif e.type_tag == 0x06:
                extra = f"  motion(+0x38)={e.field_38}"
            elif e.type_tag in (0x07, 0x1B):
                extra = f"  motion(+0x3E)={e.field_3e}"
            elif e.type_tag in (0x08, 0x09, 0x1C, 0x1D):
                extra = f"  motion(+0x3C)={e.field_3c}  new_val={e.new_value}"
            elif e.type_tag == 0x10:
                m = "(skip)" if e.field_3c == 0xFFFF else f"={e.field_3c}"
                extra = f"  motion{m}"
            elif e.type_tag == 0x0E:
                extra = f"  remap_writer slot={e.slot_index}"
            print(f"    [{i:3d}] type=0x{e.type_tag:02X} ({e.type_name}){name}{extra}")
        if s.entry_count > show_entries:
            print(f"    ... +{s.entry_count - show_entries} more entries")
        if show_raw and s.entry_count == 0:
            if s.detected_stride:
                for i in range(min(3, s.detected_count)):
                    o = i * s.detected_stride
                    print(f"    rec[{i}] @+0x{o:X}: {s.raw[o:o + s.detected_stride].hex(' ')}")
            else:
                print(f"    raw[:64]: {s.raw[:64].hex(' ')}")


def dump_offset_table(t: OffsetTableFile, *, label: str) -> None:
    print(f"{label}  count={t.count}  file_size={fmt_size(len(t.raw))}")
    empty = sum(1 for sz in t.sizes if sz == 0)
    print(f"  sections: {len(t.offsets)} ({empty} empty)")
    for i, (o, sz) in enumerate(zip(t.offsets, t.sizes)):
        mark = " (empty)" if sz == 0 else ""
        print(f"  [{i:3d}] offset=0x{o:08X}  size={sz}{mark}")
        if i == 15 and len(t.offsets) > 16:
            print(f"  ... +{len(t.offsets) - 16} more")
            break


def dump_vtb(v: VtbFile) -> None:
    print(f"VTB file  version=0x{v.version:X}  entries={v.entry_count}  data_off=0x{v.data_offset:X}")


def dump_lpd(l: LpdFile) -> None:
    print(f"LPD file  n_sections={l.n_sections}  off_magic=0x{l.off_magic:X}  off_data=0x{l.off_data:X}  off_end=0x{l.off_data_end:X}")
    if l.inner:
        b = l.inner
        print(f"  Inner LPB: version=0x{b.version:X}  total_len=0x{b.total_len:X}")


def dump_lpb(b: LpbBlock) -> None:
    print(f"LPB block  version=0x{b.version:X}  total_len=0x{b.total_len:X}")


def dump_hit(h: HitFile) -> None:
    from collections import Counter
    tags = Counter(r.tag for r in h.records)
    summary = " ".join(f"tag{t}={c}" for t, c in sorted(tags.items()))
    print(f"HIT file  records={len(h.records)}  stream_end=0x{h.stream_end:X}  trailer={len(h.trailer)}B  ({summary})")
    for i, r in enumerate(h.records[:16]):
        kind = {0: "Sphere ", 1: "Area   ", 2: "FixArea"}.get(r.tag, "??")
        if r.tag == 0:
            extra = (f"  x={r.pos_x:7.3f} y={r.pos_y:7.3f} z={r.pos_z:7.3f} "
                     f"r={r.radius:6.3f} id=0x{r.id_link:08X}")
        else:
            extra = f"  raw[8:24]={r.raw[8:24].hex(' ')}..."
        print(f"  [{i:3d}] @0x{r.offset_in_stream:04X} {kind}  slot={r.slot:>3} flags=0x{r.flags:08X}{extra}")
    if len(h.records) > 16:
        print(f"  ... +{len(h.records) - 16} more records")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description="SC6 moveset binary file parser (KH11 / MOT / DTP / VTB / LPD / hit DAT)"
    )
    ap.add_argument("paths", nargs="+", help="files to parse (extension dispatches)")
    ap.add_argument("--entries", "-n", type=int, default=16,
                    help="how many entries / records to print per section (default 16)")
    ap.add_argument("--raw", action="store_true",
                    help="dump raw bytes for empty-section heuristics")
    args = ap.parse_args(argv)

    for p in args.paths:
        if len(args.paths) > 1:
            print(f"\n========== {p} ==========")
        else:
            print(f"# {p}")
        ext = os.path.splitext(p)[1].lower()
        try:
            obj: Any = parse_auto(p)
        except Exception as e:
            print(f"  ERROR: {e}")
            continue

        if isinstance(obj, KhdFile):
            dump_khd(obj, show_entries=args.entries, show_raw=args.raw)
        elif isinstance(obj, OffsetTableFile):
            dump_offset_table(obj, label=f"{ext.upper()[1:]} file")
        elif isinstance(obj, VtbFile):
            dump_vtb(obj)
        elif isinstance(obj, LpdFile):
            dump_lpd(obj)
        elif isinstance(obj, LpbBlock):
            dump_lpb(obj)
        elif isinstance(obj, HitFile):
            dump_hit(obj)
        else:
            print(f"  (unprintable: {type(obj).__name__})")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
