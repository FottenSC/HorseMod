"""Flask web viewer for SC6 moveset data.

Usage:
    python viewer.py
    # then open http://localhost:5000

Set BATTLE_ROOT env var to override the dump directory.
"""

from __future__ import annotations

import glob
import os
import struct
import sys
from collections import Counter

from flask import Flask, abort, render_template_string, request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from luxformats import (
    FLuxMoveBankSlotView, HitFile, KhdCRecord, KhdFile, LuxBattleAttackCell,
    LuxBattleNonAttackMoveDescr, OffsetTableFile, MOVE_TYPE_NAMES,
    KHIT_TAG_NAMES, MOVE_CATEGORY, MOVE_EFFECT_TYPE, HIT_REACTION_STATE,
    attack_flags_to_str, parse_auto, parse_dtp, parse_hit_dat, parse_khd,
    parse_mot, yarare_name,
)
from stackvm import CALLCOND_NAMES

BATTLE_ROOT = os.environ.get("BATTLE_ROOT", r"E:\myMods\dump\Battle")
HIT_KINDS = ("atkhit", "bodyhit", "yararehit")

# Optional: chara id -> display name. Fill in as known; unknowns fall back to id.
CHARA_NAMES: dict[str, str] = {
    "001": "Mitsurugi", "002": "Taki", "003": "Sophitia", "004": "Siegfried",
    "005": "Nightmare", "006": "Ivy", "007": "Voldo", "00b": "Astaroth",
    "00c": "Maxi", "00d": "Kilik", "00f": "Xianghua", "011": "Yoshimitsu",
    "012": "Cervantes", "014": "Raphael", "015": "Talim", "016": "Zasalamel",
    "023": "Cassandra", "024": "Geralt", "030": "Tira",
    # 060/062/064/065/066 — DLC slots (Amy, 2B, Haohmaru, Setsuka, Hilde — uncertain)
}


# --------------------------------------------------------------------------
# Filesystem discovery
# --------------------------------------------------------------------------

def discover_chars() -> dict[str, dict[str, str]]:
    """char_id -> {'khd': path, 'mot': path, 'dtp': path, 'atkhit': path, ...}"""
    chars: dict[str, dict[str, str]] = {}

    def add(cid: str, key: str, path: str):
        chars.setdefault(cid, {})[key] = path

    # Three families with a unique prefix per directory
    for sub, prefix, key in [
        ("hdr", "hdr", "khd"),
        ("mot", "chr", "mot"),
        ("cpu", "cpuai", "dtp"),
    ]:
        for p in sorted(glob.glob(os.path.join(BATTLE_ROOT, sub, f"{prefix}*"))):
            base = os.path.basename(p)
            cid = base.removeprefix(prefix).split(".")[0].lower()
            add(cid, key, p)

    for kind in HIT_KINDS:
        for p in sorted(glob.glob(os.path.join(BATTLE_ROOT, "hit", f"{kind}*.dat"))):
            base = os.path.basename(p)
            cid = base.removeprefix(kind).removesuffix(".dat").lower()
            add(cid, kind, p)

    return chars


def char_name(cid: str) -> str:
    return CHARA_NAMES.get(cid, f"chara_{cid}")


# --------------------------------------------------------------------------
# Flask app
# --------------------------------------------------------------------------

app = Flask(__name__)


# Inline CSS — functional, monospace, scannable.
BASE_CSS = """
* { box-sizing: border-box; }
body { font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 13px;
       margin: 0; padding: 1em 1.5em; max-width: 1400px; color: #111; background: #fafafa; }
h1 { font-size: 18px; margin: 0.5em 0; }
h2 { font-size: 15px; margin: 1.2em 0 0.5em 0; color: #444; }
h3 { font-size: 13px; margin: 1em 0 0.4em 0; color: #666; }
nav { margin-bottom: 1em; padding-bottom: 0.5em; border-bottom: 1px solid #ddd; }
nav a { margin-right: 1em; }
table { border-collapse: collapse; margin: 0.5em 0; background: #fff; }
th, td { padding: 3px 8px; border: 1px solid #ddd; text-align: left; vertical-align: top; }
th { background: #efefef; font-weight: 600; }
td.num { text-align: right; }
td.hex { font-family: ui-monospace, monospace; color: #555; font-size: 12px; }
tr.empty td { color: #aaa; background: #fafafa; }
tr.sentinel td { color: #c44; background: #fff5f5; }
a { color: #06c; text-decoration: none; }
a:hover { text-decoration: underline; }
.kvp { display: inline-block; min-width: 14em; }
.kvp b { display: inline-block; min-width: 8em; color: #888; }
.tag { display: inline-block; padding: 1px 6px; border-radius: 2px; font-size: 11px; }
.tag-sphere { background: #d4f1d4; color: #060; }
.tag-area { background: #cfe3ff; color: #024; }
.tag-fixarea { background: #ffe0b3; color: #630; }
.tag-bootstrap { background: #eee; color: #555; }
.tag-sentinel { background: #fdd; color: #800; }
.hex-block { font-family: ui-monospace, monospace; font-size: 12px; line-height: 1.35;
             background: #fff; border: 1px solid #ddd; padding: 0.5em 0.8em; margin: 0.5em 0;
             white-space: pre; overflow-x: auto; }
.small { color: #888; font-size: 12px; }
.warn { color: #b40; background: #ffeecc; padding: 0.4em 0.6em; border-left: 3px solid #b40;
        margin: 0.5em 0; }
"""

BASE_TPL = """<!doctype html>
<html><head><meta charset="utf-8"><title>{{ title }}</title>
<style>{{ css }}</style></head>
<body>
<nav>
  <a href="/">Home</a>
  {% if cid %}
  <a href="/c/{{ cid }}">{{ cid }} ({{ name }})</a>
  {% endif %}
  {% if extra_nav %}
    {% for label, href in extra_nav %}
      <a href="{{ href }}">{{ label }}</a>
    {% endfor %}
  {% endif %}
</nav>
<h1>{{ heading }}</h1>
{{ body|safe }}
</body></html>
"""


def render(title: str, heading: str, body: str, *, cid: str = "", extra_nav=None) -> str:
    return render_template_string(
        BASE_TPL,
        css=BASE_CSS, title=title, heading=heading, body=body,
        cid=cid, name=char_name(cid) if cid else "",
        extra_nav=extra_nav or [],
    )


def hex_block(data: bytes, base_offset: int = 0, max_bytes: int = 4096) -> str:
    """Render bytes as a hex+ASCII block."""
    out = []
    truncated = len(data) > max_bytes
    if truncated:
        data = data[:max_bytes]
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        hex_part = " ".join(f"{b:02x}" for b in chunk).ljust(48)
        ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        out.append(f"{base_offset + i:08X}  {hex_part}  {ascii_part}")
    if truncated:
        out.append(f"... (truncated; total {max_bytes:,} bytes shown of more)")
    return '<div class="hex-block">' + "\n".join(out) + "</div>"


def fmt_size(n: int) -> str:
    if n >= 1 << 20:
        return f"{n / (1 << 20):.2f} MB"
    if n >= 1 << 10:
        return f"{n / (1 << 10):.2f} KB"
    return f"{n} B"


# --------------------------------------------------------------------------
# Routes
# --------------------------------------------------------------------------

@app.route("/")
def index():
    chars = discover_chars()
    rows = []
    for cid in sorted(chars):
        present = chars[cid]
        flags = [
            "X" if "khd" in present else ".",
            "X" if "mot" in present else ".",
            "X" if "dtp" in present else ".",
            "X" if "atkhit" in present else ".",
            "X" if "bodyhit" in present else ".",
            "X" if "yararehit" in present else ".",
        ]
        cells = "".join(f"<td>{f}</td>" for f in flags)
        rows.append(
            f'<tr><td><a href="/c/{cid}">{cid}</a></td>'
            f"<td>{char_name(cid)}</td>{cells}</tr>"
        )
    body = f"""
<p class="small">Battle data root: <code>{BATTLE_ROOT}</code> &mdash; {len(chars)} characters</p>
<table>
<tr><th>cid</th><th>name</th><th>khd</th><th>mot</th><th>dtp</th>
<th>atkhit</th><th>bodyhit</th><th>yararehit</th></tr>
{''.join(rows)}
</table>
"""
    return render("SC6 Moveset Viewer", "Characters", body)


@app.route("/c/<cid>")
def char_detail(cid: str):
    chars = discover_chars()
    if cid not in chars:
        abort(404)
    files = chars[cid]

    parts = []
    if "khd" in files:
        try:
            k = parse_khd(open(files["khd"], "rb").read())
            parts.append(
                f'<h2><a href="/c/{cid}/khd">KHD</a> &mdash; '
                f'<span class="small">{os.path.basename(files["khd"])}</span></h2>'
                f'<p>field_0C=0x{k.field_0c:08X}, '
                f'section offsets: '
                + ", ".join(f"0x{o:X}" for o in k.section_offsets) + "</p>"
                + "<ul>"
                + "".join(
                    f'<li><a href="/c/{cid}/khd/{i}">Section {i}</a>: '
                    f"offset 0x{s.offset:X}, size {fmt_size(s.size)} "
                    + (f"&mdash; <b>{s.entry_count} entries</b> (0x70 stride)"
                       if s.entry_count else (
                           f"&mdash; {s.detected_count} records (stride 0x{s.detected_stride:X})"
                           if s.detected_stride
                           else "&mdash; raw"))
                    + "</li>"
                    for i, s in enumerate(k.sections)
                )
                + "</ul>"
                # Slot table summary
                + f'<p><a href="/c/{cid}/slots"><b>{len(k.slots)} move slots</b></a> '
                f'(stack-VM bytecode at bank+0x30..)</p>'
            )
        except Exception as e:
            parts.append(f'<div class="warn">KHD parse error: {e}</div>')

    if "mot" in files:
        try:
            m = parse_mot(open(files["mot"], "rb").read())
            empty = sum(1 for s in m.sizes if s == 0)
            parts.append(
                f'<h2><a href="/c/{cid}/mot">MOT</a> &mdash; '
                f'<span class="small">{os.path.basename(files["mot"])}</span></h2>'
                f"<p>{m.count} motions ({empty} empty = {empty*100//m.count}%), "
                f"file size {fmt_size(len(m.raw))}</p>"
            )
        except Exception as e:
            parts.append(f'<div class="warn">MOT parse error: {e}</div>')

    if "dtp" in files:
        try:
            d = parse_dtp(open(files["dtp"], "rb").read())
            parts.append(
                f'<h2><a href="/c/{cid}/dtp">DTP (AI personality)</a> &mdash; '
                f'<span class="small">{os.path.basename(files["dtp"])}</span></h2>'
                f"<p>{d.count} sections, file size {fmt_size(len(d.raw))}</p>"
                + "<ul>"
                + "".join(
                    f'<li><a href="/c/{cid}/dtp/{i}">Section {i}</a>: '
                    f"offset 0x{o:X}, size {sz} bytes"
                    + (" <i>(known: personality custom table)</i>" if i == 3 else "")
                    + (" <i>(known: alternate weights)</i>" if i == 6 else "")
                    + (' <i>(known: "PSNL" tag)</i>' if i == 8 else "")
                    + "</li>"
                    for i, (o, sz) in enumerate(zip(d.offsets, d.sizes))
                )
                + "</ul>"
            )
        except Exception as e:
            parts.append(f'<div class="warn">DTP parse error: {e}</div>')

    for kind in HIT_KINDS:
        if kind not in files:
            continue
        try:
            h = parse_hit_dat(open(files[kind], "rb").read())
            tag_counts = Counter(r.tag for r in h.records)
            counts_str = ", ".join(
                f"{k_name}={tag_counts.get(k_idx, 0)}"
                for k_name, k_idx in [("sphere", 0), ("area", 1), ("fixarea", 2)]
            )
            parts.append(
                f'<h2><a href="/c/{cid}/hit/{kind}">{kind}</a> &mdash; '
                f'<span class="small">{os.path.basename(files[kind])}</span></h2>'
                f"<p>{len(h.records)} records ({counts_str}), "
                f"stream end 0x{h.stream_end:X}, trailer {len(h.trailer)}B</p>"
            )
        except Exception as e:
            parts.append(f'<div class="warn">{kind} parse error: {e}</div>')

    return render(
        f"{cid} ({char_name(cid)})",
        f"{cid} &mdash; {char_name(cid)}",
        "\n".join(parts),
        cid=cid,
    )


@app.route("/c/<cid>/khd")
def khd_overview(cid: str):
    chars = discover_chars()
    path = chars.get(cid, {}).get("khd")
    if not path:
        abort(404)
    k = parse_khd(open(path, "rb").read())
    rows = []
    for i, s in enumerate(k.sections):
        if s.entry_count > 0:
            kind = f"{s.entry_count} entries (0x70)"
        elif s.detected_stride:
            kind = f"{s.detected_count} records (stride 0x{s.detected_stride:X})"
        else:
            kind = "unidentified"
        rows.append(
            f'<tr><td><a href="/c/{cid}/khd/{i}">Section {i}</a></td>'
            f"<td>0x{s.offset:08X}</td><td>{fmt_size(s.size)}</td>"
            f"<td>{kind}</td></tr>"
        )
    body = f"""
<p class="kvp"><b>file:</b> {os.path.basename(path)}</p>
<p class="kvp"><b>size:</b> {fmt_size(len(k.raw))}</p>
<p class="kvp"><b>field_0C:</b> 0x{k.field_0c:08X}</p>
<p class="kvp"><b>section offsets:</b> {", ".join(f"0x{o:X}" for o in k.section_offsets)}</p>
<p class="kvp"><b>trailer (+0x1C..0x{0x1C + len(k.trailer_data):X}):</b> {len(k.trailer_data)} bytes</p>
<table>
<tr><th>section</th><th>offset</th><th>size</th><th>contents</th></tr>
{''.join(rows)}
</table>
<p><a href="/c/{cid}/khd/raw">View raw header bytes</a></p>
"""
    return render(f"KHD {cid}", f"KHD &mdash; {cid} ({char_name(cid)})", body, cid=cid)


@app.route("/c/<cid>/khd/raw")
def khd_raw(cid: str):
    chars = discover_chars()
    path = chars.get(cid, {}).get("khd")
    if not path:
        abort(404)
    data = open(path, "rb").read()
    offset = int(request.args.get("offset", "0"), 0)
    length = int(request.args.get("length", "512"), 0)
    body = (
        f'<p>Showing 0x{offset:X}..0x{offset+length:X} of 0x{len(data):X}</p>'
        f'<form method="get">offset 0x<input name="offset" value="0x{offset:X}" size="10"> '
        f'length <input name="length" value="{length}" size="6">'
        f'<input type="submit" value="Go"></form>'
        + hex_block(data[offset : offset + length], base_offset=offset, max_bytes=length)
    )
    return render(f"KHD raw {cid}", f"KHD raw &mdash; {cid}", body, cid=cid)


@app.route("/c/<cid>/khd/<int:sec>")
def khd_section(cid: str, sec: int):
    chars = discover_chars()
    path = chars.get(cid, {}).get("khd")
    if not path:
        abort(404)
    k = parse_khd(open(path, "rb").read())
    if sec < 0 or sec >= len(k.sections):
        abort(404)
    s = k.sections[sec]

    body_parts = [
        f'<p class="kvp"><b>offset:</b> 0x{s.offset:08X}</p>'
        f'<p class="kvp"><b>size:</b> {fmt_size(s.size)} ({s.size} bytes)</p>',
    ]

    if s.entry_count > 0:
        # Section A — LuxBattleAttackCell array (0x70 stride)
        from collections import Counter as _C
        real_cells = [e for e in s.entries if not e.is_cleared_sentinel]
        class_dist = _C(e.attack_class for e in real_cells)
        kind_dist = _C(e.anim_kind for e in real_cells)
        mt_dist = _C(e.move_type for e in real_cells)
        dist_html = (
            "<p class='small'>"
            f"<b>{len(real_cells)} real cells</b> "
            f"({s.entry_count - len(real_cells)} cleared sentinels). "
            f"<b>attack_class:</b> "
            + ", ".join(f"{k}={v}" for k, v in class_dist.most_common())
            + " &nbsp; <b>anim_kind:</b> "
            + ", ".join(f"{k}={v}" for k, v in kind_dist.most_common())
            + " &nbsp; <b>move_type:</b> "
            + ", ".join(f"{k}={v}" for k, v in mt_dist.most_common())
            + "</p>"
        )

        rows = []
        for i, e in enumerate(s.entries):
            role = e.cell_role
            if role == "Sentinel":
                rows.append(
                    f'<tr class="sentinel">'
                    f'<td><a href="/c/{cid}/khd/{sec}/{i}">{i}</a></td>'
                    f'<td colspan="11"><i>cleared sentinel '
                    f'(wU16HitboxGroupBitfield = 0xFFFF)</i></td>'
                    f'</tr>'
                )
                continue
            if role in ("Header", "NonDamaging"):
                # Non-attack cell — most fields don't carry attack semantics.
                # Show role, raw flags, and skip the per-attack columns.
                role_class = "tag-sentinel" if role == "Header" else "tag-bootstrap"
                rows.append(
                    f'<tr>'
                    f'<td><a href="/c/{cid}/khd/{sec}/{i}">{i}</a></td>'
                    f'<td><span class="tag {role_class}">{role}</span></td>'
                    f'<td colspan="9"><i>damage=0 cell &mdash; flags 0x{e.wU16AttackFlags:04X}, '
                    f'reactID={e.wI16ReactionIdBaseContact}, '
                    f'window {e.active_frames}</i></td>'
                    f'<td class="hex">0x{e.wU16AttackFlags:04X}</td>'
                    f'</tr>'
                )
                continue
            class_tag_class = {
                "Low": "tag-area",
                "Mid": "tag-sphere",
                "High": "tag-fixarea",
                "Throw": "tag-bootstrap",
                "Unblockable": "tag-sentinel",
            }.get(e.attack_class, "tag-bootstrap")
            mt_tag_class = {
                "Strike": "tag-sphere",
                "Grab": "tag-bootstrap",
            }.get(e.move_type, "tag-area")
            rows.append(
                f'<tr>'
                f'<td><a href="/c/{cid}/khd/{sec}/{i}">{i}</a></td>'
                f'<td><span class="tag {class_tag_class}">{e.attack_class}</span></td>'
                f'<td><span class="tag {mt_tag_class}">{e.move_type}</span></td>'
                f'<td>{e.anim_kind}</td>'
                f'<td class="num">{e.wI16BaseDamage}</td>'
                f'<td>{e.active_frames} <span class="small">({e.active_frame_count}f)</span></td>'
                f'<td class="num">{e.wI16BlockstunFrames}</td>'
                f'<td class="num">{e.wI16HitstunBaseContact}</td>'
                f'<td class="num">{e.wI16HitstunAlternatePostureBaseContact}</td>'
                f'<td class="num">{e.wI16ReactionIdBaseContact}</td>'
                f'<td>{e.range_stand}</td>'
                f'<td class="hex" title="{attack_flags_to_str(e.wU16AttackFlags)}">0x{e.wU16AttackFlags:04X}</td>'
                f'</tr>'
            )
        body_parts.append(
            f"<h2>LuxBattleAttackCell array ({s.entry_count} cells)</h2>"
            + dist_html
            + f"<table>"
            f'<tr>'
            f'<th>idx</th>'
            f'<th title="From wAttackFlags bits (HighBlockable/LowBlockable/Unblockable)">block class</th>'
            f'<th title="From u64SlotMask bit pattern (Strike/Grab/Other)">move type</th>'
            f'<th title="From u64SlotMask high/mid/low encoding">anim kind</th>'
            f'<th title="wI16BaseDamage @ cell+0x3A">dmg</th>'
            f'<th title="wI16MasterWindowStart..End (60ths)">active</th>'
            f'<th title="Raw defender counter; not frame advantage">blockstun</th>'
            f'<th title="Raw defender counter; not frame advantage">hitstun (base)</th>'
            f'<th title="Raw defender counter; not frame advantage">hitstun (alternate posture)</th>'
            f'<th title="wI16ReactionIdBaseContact @ cell+0x50, index into chara+0x43DD8">reactID</th>'
            f'<th title="cI8RangeStandMin..Max @ cell+0x62..63 (-127=∞)">range</th>'
            f'<th title="wU16AttackFlags @ cell+0x32 (raw hex)">flags</th>'
            f'</tr>'
            + "".join(rows) + "</table>"
        )
    elif s.non_attack_descriptors:
        # Section B: native non-attack short[3] descriptors (6-byte stride).
        # throw_cells remains only as a compatibility view for older reports.
        rows = []
        for i, d in enumerate(s.non_attack_descriptors):
            tag_str = (
                "<span class=\"tag tag-bootstrap\">default 0xFFFD</span>"
                if d.nSPassthroughTag == -3
                else f"0x{d.nSPassthroughTag & 0xFFFF:04X}"
            )
            rows.append(
                f'<tr><td>{i}</td>'
                f'<td class="num">{d.nSDamageMultiplier}</td>'
                f'<td>{tag_str}</td>'
                f'<td class="num">{d.nSDuration60ths}</td></tr>'
            )
        body_parts.append(
            f"<h2>LuxBattleNonAttackMoveDescr array ({len(s.non_attack_descriptors)} entries)</h2>"
            f"<table><tr><th>idx</th>"
            f'<th>damage multiplier</th><th>passthrough tag</th><th>duration / 60</th></tr>'
            + "".join(rows) + "</table>"
        )
    elif s.event_records:
        # Section C — FLuxMoveBankEventRecord[] (0x30-byte stride), validated
        # against LuxMoveVM_BuildMoveBankEventRecordTree.
        rows = []
        for r in s.event_records:
            tag_class = (
                "tag-sentinel" if r.type_tag == 0xFF else "tag-bootstrap"
            )
            rows.append(
                f'<tr><td>{r.record_index}</td>'
                f'<td>0x{r.byte_offset:X}</td>'
                f'<td>0x{r.dwPackedMoveId:08X}</td>'
                f'<td>{r.dwEventKind}</td>'
                f'<td>0x{r.dwShapeFlags:08X}</td>'
                f'<td>{r.flOffsetX:.3f}, {r.flOffsetY:.3f}, {r.flOffsetZ:.3f}</td>'
                f'<td>{r.flRadiusScale:.3f}</td>'
                f'<td><span class="tag {tag_class}">0x{r.type_tag:02X}</span> {r.type_name}</td>'
                f'<td class="hex">{r.raw.hex(" ")}</td>'
                f'</tr>'
            )
        payload_size = s.size - s.event_records_end
        body_parts.append(
            f"<h2>FLuxMoveBankEventRecord array ({len(s.event_records)} records, "
            f"{s.event_records_end} bytes)</h2>"
            f"<p>Legacy typed-prefix scan matched {len(s.c_prefix_records)} records. "
            f"The full Ghidra-validated event table uses the header count at +0x0E.</p>"
            f"<table><tr><th>idx</th><th>offset</th><th>packed move</th><th>event kind</th>"
            f"<th>shape flags</th><th>offset xyz</th><th>radius scale</th>"
            f"<th>legacy low byte</th><th>raw record</th></tr>"
            + "".join(rows) + "</table>"
            f"<h3>Per-slot cancel / MoveVM bytecode after event-record table</h3>"
            f"<p>{payload_size:,} bytes ({payload_size*100//s.size}% of section). "
            f"The first bytecode offset observed in slot+0x38 aligns with this boundary "
            f"across shipped KH11 files.</p>"
            + hex_block(s.raw[s.event_records_end : s.event_records_end + 512],
                        base_offset=s.offset + s.event_records_end, max_bytes=512)
        )
    elif s.detected_stride:
        # Sections B / C — show records as hex rows
        rows = []
        for i in range(s.detected_count):
            o = i * s.detected_stride
            chunk = s.raw[o : o + s.detected_stride]
            rows.append(
                f"<tr><td>{i}</td><td>+0x{o:X}</td>"
                f'<td class="hex">{chunk.hex(" ")}</td></tr>'
            )
        body_parts.append(
            f"<h2>{s.detected_count} records (stride 0x{s.detected_stride:X})</h2>"
            f"<table><tr><th>idx</th><th>offset</th><th>bytes</th></tr>"
            + "".join(rows[:500]) + "</table>"
            + (f"<p><i>truncated to first 500 of {s.detected_count}</i></p>"
               if s.detected_count > 500 else "")
        )
    else:
        body_parts.append("<h2>Raw bytes (no record stride detected)</h2>")
        body_parts.append(hex_block(s.raw, base_offset=s.offset, max_bytes=2048))

    return render(
        f"KHD {cid} sec{sec}",
        f"KHD &mdash; {cid} &mdash; Section {sec}",
        "\n".join(body_parts),
        cid=cid,
    )


@app.route("/c/<cid>/khd/<int:sec>/<int:idx>")
def khd_entry(cid: str, sec: int, idx: int):
    chars = discover_chars()
    path = chars.get(cid, {}).get("khd")
    if not path:
        abort(404)
    k = parse_khd(open(path, "rb").read())
    if sec < 0 or sec >= len(k.sections):
        abort(404)
    s = k.sections[sec]
    if idx < 0 or idx >= len(s.entries):
        abort(404)
    e = s.entries[idx]
    nav_prev = f'<a href="/c/{cid}/khd/{sec}/{idx-1}">&larr; prev</a> ' if idx > 0 else ""
    nav_next = f'<a href="/c/{cid}/khd/{sec}/{idx+1}">next &rarr;</a>' if idx + 1 < len(s.entries) else ""

    role = e.cell_role
    if role == "Sentinel":
        role_note = (
            '<div class="warn"><b>Role: Sentinel</b> &mdash; cleared slot '
            '(wU16HitboxGroupBitfield = 0xFFFF). Engine treats as inactive. '
            'The fields below are uninitialised; do not interpret them as attack data.</div>'
        )
    elif role == "Header":
        role_note = (
            '<div class="warn"><b>Role: Header / fallback cell</b> &mdash; '
            f'damage = 0 and wU16AttackFlags = 0x{e.wU16AttackFlags:04X} has '
            'an incoherent set of bits (e.g. both HighAttack and Unblockable, '
            'or 6+ of the low 9 bits). These cells exist as the bank-slot table\'s '
            'fallback when the active variant doesn\'t map to a real attack '
            '(see <code>FLuxMoveBankSlotView.nCellBoneIndexPerVariant</code>). '
            'The attack-specific fields don\'t carry their normal meaning here.</div>'
        )
    elif role == "NonDamaging":
        role_note = (
            '<div class="warn"><b>Role: Non-damaging cell</b> &mdash; '
            'damage = 0 but the cell is otherwise structurally normal '
            '(stance entry, transition, GI / parry payload, etc.). '
            'Frame data may still be meaningful; damage / hitstun aren\'t.</div>'
        )
    else:
        role_note = ''
    sentinel_note = role_note

    # Reverse references: which slots reference this cell?
    refs = k.cell_to_slots.get(idx, [])
    if refs:
        ref_links = " ".join(
            f'<a href="/c/{cid}/slots/{slot}">{slot}</a>'
            f'<span class="small">(v{variant})</span>'
            for slot, variant in refs[:20]
        )
        more = f' <span class="small">+{len(refs)-20} more</span>' if len(refs) > 20 else ''
        refs_html = (
            f'<h2>Referenced by slots ({len(refs)})</h2>'
            f'<p>{ref_links}{more}</p>'
            f'<p class="small">Each tag = slot index (variant 0..5 within '
            f'<code>nCellBoneIndexPerVariant</code>).</p>'
        )
    else:
        refs_html = ''

    summary_box = (
        ""
        if e.is_cleared_sentinel
        else f"""
<h2>Summary</h2>
<table>
<tr><th>derived</th><th>value</th><th>source</th></tr>
<tr><td>block class</td><td><b>{e.attack_class}</b></td>
    <td>wU16AttackFlags bits 0x001/0x002/0x200 (HighBlockable/LowBlockable/Unblockable)</td></tr>
<tr><td>move type</td><td><b>{e.move_type}</b></td>
    <td>u64SlotMask bits: Strike=(&amp;0x7FF0003F800000)!=0, Grab=(&amp;0x33F0C0)!=0</td></tr>
<tr><td>anim kind</td><td><b>{e.anim_kind}</b></td>
    <td>u64SlotMask high/mid/low bit pattern (see hitbox-system.md)</td></tr>
<tr><td>active frames</td><td><b>{e.active_frames}</b> ({e.active_frame_count}f)</td>
    <td>wI16MasterWindowStart..End (60ths-of-a-second)</td></tr>
<tr><td>raw blockstun / base hitstun</td><td><b>{e.blockstun_frames:d}</b> / <b>{e.hitstun_base_contact_frames:d}</b></td>
    <td>wI16BlockstunFrames / wI16HitstunBaseContact</td></tr>
<tr><td>range (stand)</td><td>{e.range_stand}</td>
    <td>cI8RangeStandMin..Max (-127 / 0x81 = no constraint)</td></tr>
<tr><td>range (crouch)</td><td>{e.range_crouch}</td>
    <td>cI8RangeCrouchMin..Max</td></tr>
<tr><td>passthrough A</td><td>0x{e.wU16PassthroughTagA:04X} <span class='small'>({e.passthrough_a_name})</span></td>
    <td>wU16PassthroughTagA — mirrored to chara+0x210A on slot transition</td></tr>
</table>
"""
    )

    body = f"""
<p>{nav_prev}<a href="/c/{cid}/khd/{sec}">back to section {sec}</a> {nav_next}</p>
{sentinel_note}
{refs_html}
{summary_box}
<h2>Decoded fields</h2>
<table>
<tr><th>field</th><th>offset</th><th>value</th><th>meaning</th></tr>
<tr><td>u64SlotMask</td><td>+0x00</td><td>0x{e.u64SlotMask:016X}</td>
    <td>Per-attacker bit assignment. Bits 31/55 set = throw partition.</td></tr>
<tr><td>wU16AttackFlags</td><td>+0x32</td><td>0x{e.wU16AttackFlags:04X} = {attack_flags_to_str(e.wU16AttackFlags)}</td>
    <td>High/low/mid/unblockable classification. (Attack class: <b>{e.attack_class}</b>)</td></tr>
<tr><td>wU16InputCond</td><td>+0x34</td><td>0x{e.wU16InputCond:04X}</td>
    <td>Input precondition mask</td></tr>
<tr><td>wI16MasterWindowStart</td><td>+0x36</td><td>{e.wI16MasterWindowStart}</td>
    <td>Active-frame start (60ths of a second)</td></tr>
<tr><td>wI16MasterWindowEnd</td><td>+0x38</td><td>{e.wI16MasterWindowEnd}</td>
    <td>Active-frame end (60ths)</td></tr>
<tr><td>wI16BaseDamage</td><td>+0x3A</td><td><b>{e.wI16BaseDamage}</b></td>
    <td>Damage applied on hit</td></tr>
<tr><td>wI16StunRecoil</td><td>+0x3C</td><td>{e.wI16StunRecoil}</td>
    <td>Block recoil</td></tr>
<tr><td>wU16ExtraStateFlags</td><td>+0x3E</td><td>0x{e.wU16ExtraStateFlags:04X}</td>
    <td>Extra state flags (BA / soul-charge / etc. — bit map TBD)</td></tr>
<tr><td>wI16BlockstunFrames</td><td>+0x44</td><td>{e.wI16BlockstunFrames}</td>
    <td>Raw defender blockstun counter; frame advantage also requires attacker recovery</td></tr>
<tr><td>wI16HitstunBaseContact</td><td>+0x46</td><td>{e.wI16HitstunBaseContact}</td>
    <td>Base-posture stun for normal contact mode 1</td></tr>
<tr><td>wI16HitstunSpecialContact</td><td>+0x48</td><td>{e.wI16HitstunSpecialContact}</td>
    <td>Base-posture stun for saved contact modes &gt;=2, including mode-11 Counter Hit</td></tr>
<tr><td>wI16HitstunAlternatePostureBaseContact</td><td>+0x4C</td><td>{e.wI16HitstunAlternatePostureBaseContact}</td>
    <td>Alternate-posture stun for normal contact mode 1</td></tr>
<tr><td>wI16HitstunAlternatePostureSpecialContact</td><td>+0x4E</td><td>{e.wI16HitstunAlternatePostureSpecialContact}</td>
    <td>Alternate-posture stun for saved contact modes &gt;=2</td></tr>
<tr><td>wI16ReactionIdBaseContact</td><td>+0x50</td><td>{e.wI16ReactionIdBaseContact}</td>
    <td>Reaction id for normal contact mode 1</td></tr>
<tr><td>wI16ReactionIdSpecialContact</td><td>+0x52</td><td>{e.wI16ReactionIdSpecialContact}</td>
    <td>Reaction id for saved contact modes &gt;=2</td></tr>
<tr><td>wI16ThrowReactionRowId</td><td>+0x54</td><td>{e.wI16ThrowReactionRowId}</td>
    <td>Classifier-7 throw-reaction row id</td></tr>
<tr><td>wU16PassthroughTagA</td><td>+0x5A</td><td>0x{e.wU16PassthroughTagA:04X}</td>
    <td>Pass-through tag (usually 0xFFFD = default reaction)</td></tr>
<tr><td>wU16HitboxGroupBitfield</td><td>+0x5E</td><td>0x{e.wU16HitboxGroupBitfield:04X}</td>
    <td>Hitbox-group bitmask; 0xFFFF means cleared sentinel</td></tr>
<tr><td>wU16PassthroughTagC</td><td>+0x60</td><td>0x{e.wU16PassthroughTagC:04X}</td>
    <td>Secondary passthrough tag</td></tr>
<tr><td>Range (stand)</td><td>+0x62..+0x63</td><td>{e.cI8RangeStandMin} .. {e.cI8RangeStandMax}</td>
    <td>Min/max range when defender is standing</td></tr>
<tr><td>Range (crouch)</td><td>+0x64..+0x65</td><td>{e.cI8RangeCrouchMin} .. {e.cI8RangeCrouchMax}</td>
    <td>Min/max range when defender is crouching</td></tr>
<tr><td>nI16ReachExtraGate</td><td>+0x66</td><td>{e.nI16ReachExtraGate}</td>
    <td>Extra-reach gate</td></tr>
<tr><td>wU16RuntimePropagateField</td><td>+0x6A</td><td>0x{e.wU16RuntimePropagateField:04X}</td>
    <td>Runtime-propagated field</td></tr>
</table>
<h2>Raw bytes (0x70)</h2>
{hex_block(e.raw)}
"""
    return render(
        f"KHD {cid} {sec}/{idx}",
        f"KHD &mdash; {cid} &mdash; Section {sec} &mdash; Cell {idx}",
        body, cid=cid,
    )


@app.route("/c/<cid>/mot")
def mot_overview(cid: str):
    chars = discover_chars()
    path = chars.get(cid, {}).get("mot")
    if not path:
        abort(404)
    m = parse_mot(open(path, "rb").read())

    # Group consecutive empty motions for readability
    rows = []
    i = 0
    show_empty = request.args.get("show_empty") == "1"
    while i < m.count:
        if m.sizes[i] == 0 and not show_empty:
            j = i
            while j < m.count and m.sizes[j] == 0:
                j += 1
            if j > i:
                rows.append(
                    f'<tr class="empty"><td colspan="3"><i>'
                    f'... {j - i} empty motions ({i}..{j-1})'
                    f'</i></td></tr>'
                )
                i = j
                continue
        rows.append(
            f'<tr><td><a href="/c/{cid}/mot/{i}">{i}</a></td>'
            f"<td>0x{m.offsets[i]:08X}</td>"
            f"<td>{m.sizes[i]}</td></tr>"
        )
        i += 1
    body = f"""
<p class="kvp"><b>count:</b> {m.count}</p>
<p class="kvp"><b>file size:</b> {fmt_size(len(m.raw))}</p>
<p class="kvp"><b>empty:</b> {sum(1 for s in m.sizes if s == 0)}</p>
<p><a href="/c/{cid}/mot?show_empty=1">[show empty]</a>
   <a href="/c/{cid}/mot">[hide empty]</a></p>
<table>
<tr><th>motion idx</th><th>offset</th><th>size (bytes)</th></tr>
{''.join(rows)}
</table>
"""
    return render(f"MOT {cid}", f"MOT &mdash; {cid} &mdash; {m.count} motions", body, cid=cid)


@app.route("/c/<cid>/mot/<int:idx>")
def mot_motion(cid: str, idx: int):
    chars = discover_chars()
    path = chars.get(cid, {}).get("mot")
    if not path:
        abort(404)
    m = parse_mot(open(path, "rb").read())
    if idx < 0 or idx >= m.count:
        abort(404)
    data = m.section(idx)
    body = f"""
<p>Motion {idx} of {m.count}</p>
<p class="kvp"><b>offset:</b> 0x{m.offsets[idx]:08X}</p>
<p class="kvp"><b>size:</b> {len(data)} bytes</p>
{hex_block(data) if data else '<p><i>(empty motion)</i></p>'}
"""
    return render(
        f"MOT {cid} #{idx}",
        f"MOT &mdash; {cid} &mdash; Motion {idx}",
        body, cid=cid,
    )


@app.route("/c/<cid>/dtp")
def dtp_overview(cid: str):
    chars = discover_chars()
    path = chars.get(cid, {}).get("dtp")
    if not path:
        abort(404)
    d = parse_dtp(open(path, "rb").read())
    section_roles = {
        3: "personality custom table (u16 slot count at +0x08, 18-byte rows)",
        6: "alternate weights (often empty)",
        8: '"PSNL" tag check',
    }
    rows = []
    for i, (o, sz) in enumerate(zip(d.offsets, d.sizes)):
        role = section_roles.get(i, "")
        rows.append(
            f'<tr><td><a href="/c/{cid}/dtp/{i}">{i}</a></td>'
            f"<td>0x{o:X}</td><td>{sz}</td><td>{role}</td></tr>"
        )
    body = f"""
<p class="kvp"><b>count:</b> {d.count}</p>
<p class="kvp"><b>file size:</b> {fmt_size(len(d.raw))}</p>
<table>
<tr><th>section</th><th>offset</th><th>size</th><th>role</th></tr>
{''.join(rows)}
</table>
"""
    return render(f"DTP {cid}", f"DTP &mdash; {cid}", body, cid=cid)


@app.route("/c/<cid>/dtp/<int:sec>")
def dtp_section(cid: str, sec: int):
    chars = discover_chars()
    path = chars.get(cid, {}).get("dtp")
    if not path:
        abort(404)
    d = parse_dtp(open(path, "rb").read())
    if sec < 0 or sec >= d.count:
        abort(404)
    data = d.section(sec)
    body = f"""
<p>Section {sec} of {d.count}</p>
<p class="kvp"><b>offset:</b> 0x{d.offsets[sec]:08X}</p>
<p class="kvp"><b>size:</b> {len(data)} bytes</p>
{hex_block(data) if data else '<p><i>(empty section)</i></p>'}
"""
    return render(
        f"DTP {cid} sec{sec}",
        f"DTP &mdash; {cid} &mdash; Section {sec}",
        body, cid=cid,
    )


@app.route("/c/<cid>/hit/<kind>")
def hit_view(cid: str, kind: str):
    if kind not in HIT_KINDS:
        abort(404)
    chars = discover_chars()
    path = chars.get(cid, {}).get(kind)
    if not path:
        abort(404)
    h = parse_hit_dat(open(path, "rb").read())

    tag_filter = request.args.get("tag", "")
    tag_count = Counter(r.tag for r in h.records)

    rows = []
    for i, r in enumerate(h.records):
        if tag_filter != "" and str(r.tag) != tag_filter:
            continue
        tag_name = {0: "Sphere", 1: "Area", 2: "FixArea"}.get(r.tag, "?")
        tag_class = {0: "tag-sphere", 1: "tag-area", 2: "tag-fixarea"}.get(r.tag, "")
        if r.tag == 0:
            payload = (
                f"x={r.pos_x:.3f} y={r.pos_y:.3f} z={r.pos_z:.3f} "
                f"r={r.radius:.3f} impulse={r.contact_impulse_scale:.3f} "
                f"bone={r.bone_index_ue4}"
            )
        else:
            payload = (
                f'<span class="hex">tail:{r.raw[8:].hex(" ")}</span>'
            )
        rows.append(
            f"<tr><td>{i}</td><td>0x{r.offset_in_stream:04X}</td>"
            f'<td><span class="tag {tag_class}">{tag_name}</span></td>'
            f"<td>{r.slot}</td><td>0x{r.flags:08X}</td>"
            f"<td>{payload}</td></tr>"
        )
    counts_html = ", ".join(
        f"{name} = {tag_count.get(idx, 0)}"
        for name, idx in (("sphere", 0), ("area", 1), ("fixarea", 2))
    )
    filter_links = (
        '<a href="?">all</a> | '
        + " | ".join(
            f'<a href="?tag={idx}">{name}</a>'
            for name, idx in (("sphere", 0), ("area", 1), ("fixarea", 2))
            if tag_count.get(idx, 0) > 0
        )
    )
    body = f"""
<p class="kvp"><b>file:</b> {os.path.basename(path)}</p>
<p class="kvp"><b>records:</b> {len(h.records)} ({counts_html})</p>
<p class="kvp"><b>stream end:</b> 0x{h.stream_end:X}</p>
<p class="kvp"><b>trailer:</b> {len(h.trailer)} bytes {h.trailer.hex(' ')}</p>
<p>Filter: {filter_links}</p>
<table>
<tr><th>idx</th><th>offset</th><th>tag</th><th>slot</th><th>flags</th><th>fields</th></tr>
{''.join(rows)}
</table>
"""
    return render(
        f"{kind} {cid}",
        f"{kind} &mdash; {cid} ({char_name(cid)})",
        body, cid=cid,
    )


@app.route("/c/<cid>/slots")
def slots_overview(cid: str):
    chars = discover_chars()
    path = chars.get(cid, {}).get("khd")
    if not path:
        abort(404)
    k = parse_khd(open(path, "rb").read())

    from collections import Counter
    # Aggregate CALLCOND usage across all slot bytecodes
    agg = Counter()
    total_instr = 0
    for s in k.slots:
        if s.bytecode:
            total_instr += len(s.bytecode.instructions)
            for idx, n in s.bytecode.callcond_summary.items():
                agg[idx] += n

    agg_rows = []
    for idx, count in sorted(agg.most_common()):
        name = CALLCOND_NAMES.get(idx, "?")
        agg_rows.append(
            f"<tr><td>0x{idx:02X}</td><td>{name}</td><td class='num'>{count}</td></tr>"
        )

    # Slot table — show first 200 then a "show more" link
    show_all = request.args.get("all") == "1"
    slots_to_show = k.slots if show_all else k.slots[:200]
    slot_rows = []
    for s in slots_to_show:
        callcond_fingerprint = ", ".join(
            f"0x{i:02X}({c})" for i, c in
            sorted(s.bytecode.callcond_summary.items(), key=lambda x: -x[1])[:4]
        ) if s.bytecode else ""
        slot_rows.append(
            f'<tr>'
            f'<td><a href="/c/{cid}/slots/{s.slot_index}">{s.slot_index}</a></td>'
            f'<td class="num">{s.wAnimationIndex_00}</td>'
            f'<td class="hex">0x{s.dwBytecodeOffset_38:X}</td>'
            f'<td class="num">{len(s.bytecode.instructions) if s.bytecode else 0}</td>'
            f'<td class="num">{s.total_frames}</td>'
            f'<td class="num">{s.playback_speed_scalar:.3f}x</td>'
            f'<td>{s.nCellBoneIndexPerVariant}</td>'
            f'<td>{callcond_fingerprint}</td>'
            f'</tr>'
        )

    show_more = (
        f'<p><a href="?all=1">Show all {len(k.slots)} slots</a></p>'
        if not show_all and len(k.slots) > 200 else ""
    )

    body = f"""
<p><b>{len(k.slots)} move slots</b> (slot table at bank+0x30, stride 0x48).
Total decoded bytecode instructions across all slots: <b>{total_instr:,}</b>.</p>
<h2>CALLCOND aggregate usage</h2>
<table>
<tr><th>idx</th><th>name</th><th>calls</th></tr>
{''.join(agg_rows)}
</table>
<h2>Slot table</h2>
<p><span class='small'>Showing {len(slot_rows)} of {len(k.slots)} slots.
Click a slot index to see its full bytecode listing.</span></p>
<table>
<tr><th>slot</th><th>anim</th><th>bc_off</th><th>#ops</th>
    <th>frames</th><th>speed</th><th>cellVariants[6]</th><th>top CALLCONDs</th></tr>
{''.join(slot_rows)}
</table>
{show_more}
"""
    return render(f"Slots {cid}", f"Move Slots &mdash; {cid} ({char_name(cid)})",
                  body, cid=cid)


@app.route("/c/<cid>/slots/<int:slot_idx>")
def slot_detail(cid: str, slot_idx: int):
    chars = discover_chars()
    path = chars.get(cid, {}).get("khd")
    if not path:
        abort(404)
    k = parse_khd(open(path, "rb").read())
    if slot_idx < 0 or slot_idx >= len(k.slots):
        abort(404)
    s = k.slots[slot_idx]

    nav = (
        (f'<a href="/c/{cid}/slots/{slot_idx-1}">&larr; prev</a> '
         if slot_idx > 0 else '') +
        f'<a href="/c/{cid}/slots">all slots</a>' +
        (f' <a href="/c/{cid}/slots/{slot_idx+1}">next &rarr;</a>'
         if slot_idx + 1 < len(k.slots) else '')
    )

    # Field table
    field_rows = [
        ('wAnimationIndex_00',  f'{s.wAnimationIndex_00}'),
        ('nMotionAStartFrame_02', f'{s.nMotionAStartFrame_02}'),
        ('nMotionAEndFrame_04', f'{s.nMotionAEndFrame_04}'),
        ('bMotionATrack_06', f'{s.bMotionATrack_06}'),
        ('bMotionAFlags_07', f'0x{s.bMotionAFlags_07:02X}'),
        ('flMotionAWeightHundredths_08', f'{s.flMotionAWeightHundredths_08:.3f}'),
        ('flMotionABlendHundredths_0C', f'{s.flMotionABlendHundredths_0C:.3f}'),
        ('wMotionBId_10', f'{s.wMotionBId_10}'),
        ('nMotionBStartFrame_12', f'{s.nMotionBStartFrame_12}'),
        ('nMotionBEndFrame_14', f'{s.nMotionBEndFrame_14}'),
        ('bMotionBTrack_16', f'{s.bMotionBTrack_16}'),
        ('bMotionBFlags_17', f'0x{s.bMotionBFlags_17:02X}'),
        ('flMotionBWeightHundredths_18', f'{s.flMotionBWeightHundredths_18:.3f}'),
        ('flMotionBBlendHundredths_1C', f'{s.flMotionBBlendHundredths_1C:.3f}'),
        ('qwInputMask_20',      f'0x{s.qwInputMask_20:016X}'),
        ('qwInputMask_28',      f'0x{s.qwInputMask_28:016X}'),
        ('flPlaybackSpeedHundredths_30', f'{s.flPlaybackSpeedHundredths_30:.3f}'),
        ('playbackSpeedScalar', f'{s.playback_speed_scalar:.6f}x'),
        ('wTotalFrames',        f'{s.wTotalFrames}'),
        ('nHitWindowStart_36',  f'{s.nHitWindowStart_36}'),
        ('dwBytecodeOffset_38', f'0x{s.dwBytecodeOffset_38:08X}'),
        ('nCellBoneIndexPerVariant', f'{s.nCellBoneIndexPerVariant}'),
    ]
    fields_html = "".join(
        f'<tr><td>{k}</td><td>{v}</td></tr>' for k, v in field_rows
    )

    # Bytecode listing
    if s.bytecode and s.bytecode.instructions:
        bc_rows = []
        for inst in s.bytecode.instructions:
            cls = ""
            if inst.opcode == 0x25:  # CALLCOND — highlight
                cls = " tag-area" if (inst.imm_b0 or 0) in (0x05, 0x06, 0x07, 0x08) else ""
            bc_rows.append(
                f'<tr><td class="hex">+0x{inst.pc:06X}</td>'
                f'<td class="num">{inst.length}</td>'
                f'<td class="hex">{inst.raw.hex(" ")}</td>'
                f'<td><span class="tag{cls}">{inst.render()}</span></td></tr>'
            )
        bc_html = (
            f"<p>bytecode @0x{s.bytecode.bytecode_offset:X}, "
            f"<b>{len(s.bytecode.instructions)} instructions</b> "
            f"({s.bytecode.length_bytes} bytes), "
            f"{'truncated' if s.bytecode.truncated else 'clean exit'}</p>"
            f'<table><tr><th>pc</th><th>len</th><th>bytes</th><th>opcode</th></tr>'
            + "".join(bc_rows) + "</table>"
        )
        # CALLCOND breakdown
        bc_html += "<h3>CALLCOND fingerprint</h3><ul>"
        for idx, n in sorted(s.bytecode.callcond_summary.items()):
            name = CALLCOND_NAMES.get(idx, "?")
            bc_html += f"<li>0x{idx:02X} {name}: {n}</li>"
        bc_html += "</ul>"
    else:
        bc_html = "<p><i>(no decoded bytecode)</i></p>"

    body = f"""
<p>{nav}</p>
<h2>Slot {slot_idx} fields</h2>
<table><tr><th>field</th><th>value</th></tr>{fields_html}</table>
<h2>Stack-VM bytecode</h2>
{bc_html}
"""
    return render(f"Slot {cid}/{slot_idx}",
                  f"Slot {slot_idx} &mdash; {cid}", body, cid=cid)


@app.errorhandler(404)
def not_found(e):
    return render("404", "Not found",
                  "<p>That path doesn't exist. <a href='/'>Back to home</a>.</p>"), 404


if __name__ == "__main__":
    print(f"Battle data root: {BATTLE_ROOT}")
    print(f"Discovered {len(discover_chars())} characters.")
    print("Starting server at http://localhost:5000")
    app.run(host="127.0.0.1", port=5000, debug=False)
