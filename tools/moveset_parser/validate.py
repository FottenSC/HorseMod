"""Validate the SC6 moveset parsers against every dumped file.

Runs `luxformats.parse_auto` over each file in `dump/Battle/{hdr,mot,cpu,hit}`,
asserts structural invariants, and reports anomalies.
"""

from __future__ import annotations

import argparse
import math
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import Callable, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from luxformats import (
    KhdFile, LuxBattleAttackCell, OffsetTableFile, HitFile,
    parse_khd, parse_mot, parse_dtp, parse_hit_dat,
    MOVE_TYPE_NAMES,
)


def validate_khd(path: str, data: bytes) -> tuple[bool, list[str], dict]:
    issues: list[str] = []
    stats: dict = {}
    if data[:4] != b"KH11":
        issues.append(f"bad magic: {data[:4]!r}")
        return False, issues, stats
    try:
        k = parse_khd(data)
    except Exception as e:
        issues.append(f"parse failed: {e}")
        return False, issues, stats

    if len(k.section_offsets) != 3:
        issues.append(f"expected 3 section offsets, got {len(k.section_offsets)}")
    stats["move_count"] = k.move_count
    stats["movelist_id"] = k.movelist_id
    stats["event_record_count"] = k.event_record_count
    stats["slot_count"] = len(k.slots)
    stats["first_cancel_offset"] = k.first_cancel_offset
    if k.move_count and len(k.slots) != k.move_count:
        issues.append(f"slot count {len(k.slots)} != header move count {k.move_count}")
    if k.section_offsets != sorted(k.section_offsets):
        issues.append(f"section offsets not monotonic: {k.section_offsets}")
    for i, off in enumerate(k.section_offsets):
        if off >= len(data):
            issues.append(f"section {i} offset 0x{off:X} >= file size 0x{len(data):X}")
        if off < 0x1C:
            issues.append(f"section {i} offset 0x{off:X} overlaps header")

    sec_a = k.sections[0]
    stats["section_A_size"] = sec_a.size
    stats["section_A_entries"] = sec_a.entry_count
    if sec_a.size % 0x70 != 0:
        issues.append(f"section A size 0x{sec_a.size:X} not multiple of 0x70")
    if sec_a.entry_count == 0 and sec_a.size > 0:
        issues.append("section A non-empty but no entry array detected")

    # Section A = LuxBattleAttackCell[]. Validate via canonical fields.
    real_cells = [e for e in sec_a.entries if not e.is_cleared_sentinel]
    cleared = sec_a.entry_count - len(real_cells)
    stats["section_A_real_cells"] = len(real_cells)
    stats["section_A_cleared_sentinels"] = cleared
    if real_cells:
        damages = [e.wI16BaseDamage for e in real_cells]
        stats["section_A_damage_min"] = min(damages)
        stats["section_A_damage_max"] = max(damages)
        # Damage should fit in a reasonable range (negative damages are healers
        # or recoil, observed up to ~120 for big moves)
        if max(damages) > 500:
            issues.append(f"section A: max damage {max(damages)} is implausible")
        if min(damages) < -100:
            issues.append(f"section A: min damage {min(damages)} is implausible")
        # Active-frame windows must be ordered (start <= end)
        bad_window = [
            e for e in real_cells
            if e.cell_role == "Attack"
            # Setsuka hdr022 cell 166 is authored as 26..25 (one frame before
            # start). Treat that as a zero-length/special window, not parser
            # corruption; larger inversions are suspicious.
            if e.wI16MasterWindowEnd + 1 < e.wI16MasterWindowStart
            and e.wI16MasterWindowStart != 0
        ]
        if bad_window:
            issues.append(
                f"section A: {len(bad_window)} cells have window_end < window_start"
            )
        # Attack-class distribution
        from collections import Counter as _C
        stats["section_A_attack_classes"] = dict(
            _C(e.attack_class for e in real_cells).most_common()
        )

    sec_b = k.sections[1]
    stats["section_B_size"] = sec_b.size
    if sec_b.size % 6 != 0:
        issues.append(f"section B size 0x{sec_b.size:X} not multiple of 6")
    else:
        n_b = sec_b.size // 6
        stats["section_B_records"] = n_b
        stats["section_B_throw_records"] = len(sec_b.throw_cells)
        if n_b > 0:
            field2 = Counter(
                struct.unpack_from("<H", sec_b.raw, i * 6 + 2)[0] for i in range(n_b)
            )
            stats["section_B_field2_distribution"] = dict(field2.most_common(3))
            max_throw_damage = max((t.wDamage for t in sec_b.throw_cells), default=0)
            stats["section_B_throw_damage_max"] = max_throw_damage
            if max_throw_damage > 500:
                issues.append(f"section B: max throw damage {max_throw_damage} is implausible")

    if k.throw_to_slots:
        max_throw_ref = max(k.throw_to_slots)
        if len(k.sections) > 1 and max_throw_ref >= len(k.sections[1].throw_cells):
            issues.append(f"throw refs exceed Section B count: {max_throw_ref}")

    sec_c = k.sections[2]
    stats["section_C_size"] = sec_c.size
    stats["section_C_event_records"] = len(sec_c.event_records)
    stats["section_C_event_record_bytes"] = sec_c.event_records_end
    if len(sec_c.event_records) != k.event_record_count:
        issues.append(
            f"section C event record count {len(sec_c.event_records)} != header {k.event_record_count}"
        )
    if sec_c.event_records_end > sec_c.size:
        issues.append("section C event records exceed section size")
    if sec_c.event_records:
        stats["section_C_event_tag_distribution"] = dict(
            Counter(r.type_tag for r in sec_c.event_records)
        )
        event_kinds = Counter(r.dwEventKind for r in sec_c.event_records)
        stats["section_C_event_kind_distribution"] = dict(event_kinds)
        bad_kinds = sorted(k for k in event_kinds if k not in range(1, 7))
        if bad_kinds:
            issues.append(f"section C: unexpected event kinds {bad_kinds}")
        unresolved = [
            r.dwPackedMoveId for r in sec_c.event_records
            if k.resolve_packed_slot(r.dwPackedMoveId) is None
        ]
        stats["section_C_unresolved_packed_move_ids"] = len(unresolved)
        if unresolved:
            sample = ", ".join(f"0x{x:X}" for x in unresolved[:5])
            issues.append(f"section C: {len(unresolved)} packed move ids do not resolve ({sample})")
        bad_float_count = sum(
            1 for r in sec_c.event_records
            if not all(math.isfinite(v) for v in (r.flOffsetX, r.flOffsetY, r.flOffsetZ, r.flRadiusScale))
        )
        if bad_float_count:
            issues.append(f"section C: {bad_float_count} records have non-finite float fields")
    stats["section_C_legacy_prefix_records"] = len(sec_c.c_prefix_records)
    stats["section_C_legacy_prefix_bytes"] = sec_c.c_prefix_end
    if sec_c.c_prefix_records:
        tag_set = Counter(r.type_tag for r in sec_c.c_prefix_records)
        # Every tag must be in the known valid set
        from luxformats import KHD_SECTION_C_VALID_TAGS
        bad = [t for t in tag_set if t not in KHD_SECTION_C_VALID_TAGS]
        if bad:
            issues.append(f"section C: prefix contains unknown tags {bad}")
        # Prefix MUST start with a D6 marker (count header)
        if sec_c.c_prefix_records[0].type_tag != 0xD6:
            issues.append(
                f"section C prefix doesn't start with D6 marker "
                f"(starts with 0x{sec_c.c_prefix_records[0].type_tag:02X})"
            )
        stats["section_C_legacy_prefix_tag_distribution"] = dict(tag_set)

    sum_sections = sum(s.size for s in k.sections)
    expected = sum_sections + (k.section_offsets[0])
    if expected != len(data):
        issues.append(f"section sizes don't sum to file size: {expected} != {len(data)}")

    return (len(issues) == 0), issues, stats


def validate_offset_table(
    path: str,
    data: bytes,
    expected_min_count: int = 1,
    expected_max_count: int = 4096,
    parser: Callable[[bytes], OffsetTableFile] = parse_dtp,
) -> tuple[bool, list[str], dict]:
    issues: list[str] = []
    stats: dict = {}
    if len(data) < 8:
        issues.append("file too small")
        return False, issues, stats
    try:
        t = parser(data)
    except Exception as e:
        issues.append(f"parse failed: {e}")
        return False, issues, stats
    stats["count"] = t.count
    stats["file_size"] = len(data)
    stats["empty_sections"] = sum(1 for sz in t.sizes if sz == 0)
    stats["nonempty_sections"] = t.count - stats["empty_sections"]
    if not expected_min_count <= t.count <= expected_max_count:
        issues.append(f"count {t.count} outside expected range [{expected_min_count}, {expected_max_count}]")
    if any(o > len(data) for o in t.offsets):
        bad = [o for o in t.offsets if o > len(data)]
        issues.append(f"offsets exceed file size: {len(bad)} entries")
    if t.offsets != sorted(t.offsets):
        issues.append("offsets not monotonic")
    last_end = t.offsets[-1] + t.sizes[-1]
    stats["trailer_after_offset_table_end"] = len(data) - last_end
    if last_end > len(data):
        issues.append(f"last section end 0x{last_end:X} exceeds file size 0x{len(data):X}")
    return (len(issues) == 0), issues, stats


def validate_hit(path: str, data: bytes) -> tuple[bool, list[str], dict]:
    issues: list[str] = []
    stats: dict = {}
    try:
        h = parse_hit_dat(data)
    except Exception as e:
        issues.append(f"parse failed: {e}")
        return False, issues, stats
    stats["records"] = len(h.records)
    stats["stream_end"] = h.stream_end
    stats["trailer_bytes"] = len(h.trailer)

    tag_counts = Counter(r.tag for r in h.records)
    stats["tag_counts"] = dict(tag_counts)

    # Trailer should be a small sentinel (typically 0xFFFF or padding < 16 bytes)
    if len(h.trailer) > 16:
        issues.append(f"trailer suspiciously large: {len(h.trailer)} bytes")

    # The stream MUST have consumed the file modulo the trailer.
    if h.stream_end + len(h.trailer) != len(data):
        issues.append(
            f"stream_end (0x{h.stream_end:X}) + trailer ({len(h.trailer)}) != file size ({len(data)})"
        )

    # Sphere records: defender bone slots are 0..63
    sphere_records = [r for r in h.records if r.tag == 0]
    bad_slot = [r.slot for r in sphere_records if r.slot > 0x3F]
    if bad_slot:
        issues.append(f"{len(bad_slot)} sphere records have slot > 0x3F (max {max(bad_slot)})")
    # Sphere radii should be positive
    bad_radius = [r for r in sphere_records if r.radius <= 0]
    if bad_radius:
        issues.append(f"{len(bad_radius)} sphere records have radius <= 0")
    # Sphere positions in a sensible range (chara local space, ~|x|<2)
    bad_pos = sum(
        1 for r in sphere_records
        if abs(r.pos_x) > 10 or abs(r.pos_y) > 10 or abs(r.pos_z) > 10
    )
    if bad_pos:
        issues.append(f"{bad_pos} sphere records have |pos| > 10")
    return (len(issues) == 0), issues, stats


@dataclass
class CharData:
    char_id: str
    khd: Optional[dict] = None
    mot: Optional[dict] = None
    dtp: Optional[dict] = None
    atkhit: Optional[dict] = None
    bodyhit: Optional[dict] = None
    yararehit: Optional[dict] = None


def extract_char_id(path: str) -> Optional[str]:
    base = os.path.basename(path)
    m = re.search(r"([0-9a-fA-F]{3})\.(?:khd|mot|dtp|dat)$", base)
    return m.group(1).lower() if m else None


def run(root: str, verbose: bool = False) -> int:
    chars: dict[str, CharData] = {}

    def get_or_make(cid: str) -> CharData:
        if cid not in chars:
            chars[cid] = CharData(char_id=cid)
        return chars[cid]

    n_ok = 0
    n_err = 0
    all_issues: list[tuple[str, list[str]]] = []

    def process(path: str, validator, target_attr: str, **kw):
        nonlocal n_ok, n_err
        with open(path, "rb") as f:
            data = f.read()
        ok, issues, stats = validator(path, data, **kw)
        if ok:
            n_ok += 1
        else:
            n_err += 1
            all_issues.append((path, issues))
        cid = extract_char_id(path)
        if cid:
            setattr(get_or_make(cid), target_attr, stats)
        if verbose:
            tag = "OK" if ok else "FAIL"
            print(f"  [{tag}] {os.path.basename(path)}  {stats}")
            for iss in issues:
                print(f"         - {iss}")

    print(f"=== Validating files under {root} ===\n")

    print("[1/4] Validating .khd files...")
    for name in sorted(os.listdir(os.path.join(root, "hdr"))):
        if name.endswith(".khd"):
            process(os.path.join(root, "hdr", name), validate_khd, "khd")

    print("[2/4] Validating .mot files...")
    for name in sorted(os.listdir(os.path.join(root, "mot"))):
        if name.endswith(".mot"):
            process(os.path.join(root, "mot", name), validate_offset_table, "mot",
                    # Auxiliary chrc*/chrd* banks legitimately contain only
                    # one to three clips.  Structural validity belongs in
                    # parse_mot; a guessed content-size floor is not a format
                    # invariant.
                    expected_min_count=1, expected_max_count=4096, parser=parse_mot)

    print("[3/4] Validating .dtp files...")
    for name in sorted(os.listdir(os.path.join(root, "cpu"))):
        if name.endswith(".dtp"):
            process(os.path.join(root, "cpu", name), validate_offset_table, "dtp",
                    expected_min_count=2, expected_max_count=50, parser=parse_dtp)

    print("[4/4] Validating .dat files (atkhit / bodyhit / yararehit)...")
    for name in sorted(os.listdir(os.path.join(root, "hit"))):
        if name.endswith(".dat"):
            path = os.path.join(root, "hit", name)
            if name.startswith("atkhit"):
                attr = "atkhit"
            elif name.startswith("bodyhit"):
                attr = "bodyhit"
            elif name.startswith("yararehit"):
                attr = "yararehit"
            else:
                continue
            process(path, validate_hit, attr)

    print()
    print(f"=== Per-file validation: {n_ok} ok, {n_err} fail ===\n")
    if all_issues:
        print("Per-file issues:")
        for path, iss in all_issues[:30]:
            print(f"  {os.path.basename(path)}:")
            for x in iss:
                print(f"    - {x}")
        if len(all_issues) > 30:
            print(f"  ... +{len(all_issues) - 30} more files with issues")

    print()
    print("=== Cross-character consistency ===\n")
    all_chars = sorted(chars.keys())
    print(f"Found {len(all_chars)} chara ids: {', '.join(all_chars)}\n")

    print("KHD section A (LuxBattleAttackCell[]) per chara:")
    counts_a = []
    for cid in all_chars:
        c = chars[cid]
        if c.khd and "section_A_entries" in c.khd:
            n = c.khd["section_A_entries"]
            real = c.khd.get("section_A_real_cells", 0)
            cleared = c.khd.get("section_A_cleared_sentinels", 0)
            dmin = c.khd.get("section_A_damage_min", 0)
            dmax = c.khd.get("section_A_damage_max", 0)
            counts_a.append(n)
            print(f"  {cid}: {n:>4} cells "
                  f"({real} real + {cleared} cleared)  "
                  f"damage range [{dmin}..{dmax}]")
    if counts_a:
        print(f"  -> min={min(counts_a)}, max={max(counts_a)}, "
              f"avg={sum(counts_a)//len(counts_a)}")
    print()

    # Aggregate attack-class distribution across all charas
    agg_classes: Counter = Counter()
    for cid in all_chars:
        c = chars[cid]
        if c.khd and "section_A_attack_classes" in c.khd:
            for cls, ct in c.khd["section_A_attack_classes"].items():
                agg_classes[cls] += ct
    print("Aggregate attack-class distribution across all chars:")
    for cls, ct in agg_classes.most_common():
        print(f"  {cls:<20}: {ct}")
    print()

    print("KHD section C event records per chara:")
    event_counts = []
    agg_c_tags: Counter = Counter()
    agg_event_kinds: Counter = Counter()
    unresolved_total = 0
    for cid in all_chars:
        c = chars[cid]
        if c.khd and "section_C_event_records" in c.khd:
            n = c.khd["section_C_event_records"]
            nb = c.khd["section_C_event_record_bytes"]
            event_counts.append(n)
            unresolved_total += c.khd.get("section_C_unresolved_packed_move_ids", 0)
            print(f"  {cid}: {n:>3} records ({nb:>5} bytes / "
                  f"{nb*100//c.khd['section_C_size']:>2}% of section C)")
            for t, ct in c.khd.get("section_C_event_tag_distribution", {}).items():
                agg_c_tags[t] += ct
            for k, ct in c.khd.get("section_C_event_kind_distribution", {}).items():
                agg_event_kinds[k] += ct
    if event_counts:
        print(f"  -> min={min(event_counts)}, max={max(event_counts)}, "
              f"avg={sum(event_counts)//len(event_counts)}")
        print(f"  -> unresolved packed move ids={unresolved_total}")
    print()
    print("Section C event records - aggregate event-kind distribution:")
    for kind, ct in sorted(agg_event_kinds.items()):
        print(f"  {kind}: {ct}")
    print()
    print("Section C event records - aggregate low-byte tag distribution:")
    for tag, ct in sorted(agg_c_tags.items()):
        name = "Header_CountMarker" if tag == 0xD6 else MOVE_TYPE_NAMES.get(tag, f"Unknown_0x{tag:02X}")
        print(f"  0x{tag:02X} ({name:<22}): {ct}")
    print()

    print("KHD section A - aggregate type-tag distribution:")
    agg_tags: Counter = Counter()
    for cid in all_chars:
        c = chars[cid]
        if c.khd and "section_A_tag_distribution" in c.khd:
            for tag, n in c.khd["section_A_tag_distribution"].items():
                agg_tags[tag] += n
    for tag, count in agg_tags.most_common(10):
        name = MOVE_TYPE_NAMES.get(tag, f"Unknown_0x{tag:02X}")
        print(f"  0x{tag:02X} ({name:<22}): {count}")
    print()

    print("Motion counts per chara:")
    mot_counts = []
    for cid in all_chars:
        c = chars[cid]
        if c.mot:
            n = c.mot["count"]
            empty = c.mot["empty_sections"]
            mot_counts.append(n)
            trailer = c.mot.get("trailer_after_offset_table_end", 0)
            print(f"  {cid}: {n:4d} motions ({empty} empty = {empty*100//n}%)  trailer={trailer}B")
    if mot_counts:
        print(f"  -> min={min(mot_counts)}, max={max(mot_counts)}, avg={sum(mot_counts)//len(mot_counts)}")
    print()

    print("AI personality (.dtp) section counts:")
    dtp_counts = Counter()
    for cid in all_chars:
        c = chars[cid]
        if c.dtp:
            dtp_counts[c.dtp["count"]] += 1
    for n, cnt in sorted(dtp_counts.items()):
        print(f"  count={n}: {cnt} charas")
    print()

    print("Hit-data record counts per chara (atk / body / yarare):")
    print("  cid  atk_total (sphere/area/fix)  body_total                yarare_total")
    for cid in all_chars:
        c = chars[cid]
        def fmt(stats):
            if not stats:
                return "-"
            tc = stats.get("tag_counts", {})
            return f"{stats['records']:>3} ({tc.get(0, 0):>2}/{tc.get(1, 0):>2}/{tc.get(2, 0):>2})"
        a = fmt(c.atkhit)
        b = fmt(c.bodyhit)
        y = fmt(c.yararehit)
        print(f"  {cid}: atk={a:<18} body={b:<18} yarare={y}")
    print()

    print("File presence by chara (X = present, . = missing):")
    print("        khd mot dtp atk body yar")
    incomplete = 0
    for cid in all_chars:
        c = chars[cid]
        flags = []
        for attr in ("khd", "mot", "dtp", "atkhit", "bodyhit", "yararehit"):
            flags.append("X" if getattr(c, attr) else ".")
        line = "   ".join(flags)
        missing = flags.count(".")
        marker = "  <- incomplete" if missing else ""
        if missing:
            incomplete += 1
        print(f"  {cid}:   {line}{marker}")
    print()
    print(f"  Complete charas: {len(all_chars) - incomplete}/{len(all_chars)}")
    if incomplete:
        print(f"  Incomplete charas: {incomplete} (likely DLC or shared/special slots)")

    return 0 if n_err == 0 else 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Validate the SC6 moveset parser against all dumped Battle data")
    ap.add_argument("--root", default=r"E:\myMods\dump\Battle", help="root path containing hdr/, mot/, cpu/, hit/")
    ap.add_argument("--verbose", "-v", action="store_true", help="print per-file results")
    args = ap.parse_args()
    sys.exit(run(args.root, verbose=args.verbose))
