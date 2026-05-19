"""Community-collected SC6 frame data — parser + loader.

The in-game movelist assets (DA_MovePlayData / DA_MoveListTable /
Game.archive) carry move names, inputs, per-hit attack class and effect
tags — but NO frame data, and no reference to a `.khd` attack cell. The
khd holds the frame data (damage, startup, stun) but has no move-name
link. The two are separate data islands; the game only bridges them
behaviourally, at runtime, by feeding a move's input through the MoveVM.
See memory `project_sc6_mainindex_not_cell_index`.

So per-move frame data cannot be derived from the offline files alone.
The SC6 community hand-measured it in training mode and maintains a
spreadsheet:

    https://docs.google.com/spreadsheets/d/1R3I_LXfqhvFjlHTuj-wSWwwqYmlUf299a3VY9pVyGEw

`community_framedata.xlsx` in this directory is a download of that
sheet. This module parses it into `community_framedata.json` — a clean,
diffable, per-character payload the webui exporter joins against.

Run directly to (re)build the JSON:

    python community_framedata.py            # xlsx -> community_framedata.json

`load()` returns the parsed data for `export_webui_data.py`.
"""

from __future__ import annotations

import json
import os
import re
import sys
from typing import Any

HERE = os.path.dirname(os.path.abspath(__file__))
XLSX_PATH = os.path.join(HERE, "community_framedata.xlsx")
JSON_PATH = os.path.join(HERE, "community_framedata.json")

SHEET_URL = (
    "https://docs.google.com/spreadsheets/d/"
    "1R3I_LXfqhvFjlHTuj-wSWwwqYmlUf299a3VY9pVyGEw"
)

# The sheet's `Character` column uses lowercase community names; map each
# to the SC6 style id (cid) the rest of the toolchain keys on. Verified
# against the cid->name ground-truth table (memory
# `project_sc6_character_id_mapping`).
COMMUNITY_NAME_TO_CID: dict[str, str] = {
    "2b": "060", "amy": "030", "astaroth": "012", "azwel": "064",
    "cassandra": "017", "cervantes": "014", "geralt": "065", "groh": "062",
    "haohmaru": "061", "hilde": "028", "hwang": "009", "inferno": "013",
    "ivy": "00b", "kilik": "00c", "maxi": "004", "mitsurugi": "001",
    "nightmare": "011", "raphael": "015", "seong-mi-na": "002",
    "setsuka": "022", "siegfried": "007", "sophitia": "006", "taki": "003",
    "talim": "016", "tira": "023", "voldo": "005", "xianghua": "00d",
    "yoshimitsu": "00f", "zasalamel": "024",
}

# Their `Hit level` token -> our canonical attack-class name. The same
# vocabulary DA_MoveListTable's AttributeTag uses, so a community move's
# hit-level sequence is directly comparable to our `hitClasses`.
HIT_LEVEL_NAMES: dict[str, str] = {
    "H": "High", "M": "Mid", "L": "Low",
    "SM": "Special Mid", "SL": "Special Low",
    "TH": "Throw", "UB": "Unblockable",
}


def _tokens(raw: str) -> list[str]:
    """Split a community Command / Hit-level cell into tokens.

    The sheet authors two ways (per its own in-cell instructions):
      * colon-wrapped — `:6::A:` , `:H::M:`  -> the `:tok:` form
      * comma-separated — `6,A` , `L,H,L,M`
    A bare value with neither (`6A`) is returned as a single token.
    """
    s = str(raw or "").strip()
    if not s:
        return []
    if ":" in s:
        return [t.strip() for t in re.findall(r":([^:]+):", s) if t.strip()]
    if "," in s:
        return [t.strip() for t in s.split(",") if t.strip()]
    return [s]


def _canon_command(tokens: list[str]) -> str:
    """Join input tokens into a compact display command (`6A`, `236B`)."""
    return "".join(tokens)


def norm_input_key(s: str) -> str:
    """Normalise an input string to a comparison key.

    Collapses our movelist notation (`6A.A`, `236A~B`) and the community
    notation (`:6::A::A:`) to the same shape: lowercase, separators and
    grouping punctuation stripped. `6A.A` and `:6::A::A:` both -> `6aa`.
    """
    s = str(s or "").lower()
    s = re.sub(r"[\s.:~\-_]", "", s)
    s = s.replace("(", "").replace(")", "").replace("[", "").replace("]", "")
    return s


def norm_name(s: str) -> str:
    """Normalise a move name for joining (case/whitespace-insensitive)."""
    return re.sub(r"\s+", " ", str(s or "").strip().lower())


def _to_int(v: Any) -> int | None:
    """First signed integer in `v`, or None. Tolerates `12`, `12~14`,
    `12*`, `-6`, floats."""
    m = re.search(r"-?\d+", str(v if v is not None else ""))
    return int(m.group(0)) if m else None


def _damage_list(raw: Any) -> list[int]:
    """Parse the comma-separated per-hit Damage cell into ints. A
    charge-variant segment like `30(45)` contributes its first int."""
    out: list[int] = []
    for seg in str(raw or "").split(","):
        n = _to_int(seg)
        if n is not None:
            out.append(n)
    return out


def _clean_str(v: Any) -> str:
    return re.sub(r"\s+", " ", str(v if v is not None else "").strip())


def parse_xlsx(path: str = XLSX_PATH) -> dict[str, Any]:
    """Parse the community spreadsheet into the per-cid payload.

    Returns ``{"_meta": {...}, "chars": {cid: {...}}}``. Requires
    `openpyxl` (a build-time dependency only — the webui consumes the
    generated JSON, never the xlsx).
    """
    import openpyxl  # local import: only needed at (re)build time

    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    rows = list(wb["FrameData"].iter_rows(values_only=True))
    # The header is a few rows down (the sheet leads with instructions).
    hdr = next((i for i, r in enumerate(rows) if r and r[0] == "Character"),
               None)
    if hdr is None:
        raise ValueError(
            f"{path}: no 'Character' header row in the FrameData sheet — "
            f"the spreadsheet layout may have changed.")
    col = {name: i for i, name in enumerate(rows[hdr]) if name}

    def cell(r: tuple, name: str) -> Any:
        i = col.get(name)
        return r[i] if i is not None and i < len(r) else None

    chars: dict[str, dict[str, Any]] = {}
    unmapped: set[str] = set()
    move_rows = 0
    for r in rows[hdr + 1:]:
        if not r or not r[0]:
            continue
        community_name = str(r[0]).strip().lower()
        cid = COMMUNITY_NAME_TO_CID.get(community_name)
        if cid is None:
            unmapped.add(community_name)
            continue
        tokens = _tokens(cell(r, "Command"))
        hit_tokens = _tokens(cell(r, "Hit level"))
        move = {
            "name": _clean_str(cell(r, "Move Name")),
            "category": _clean_str(cell(r, "Move category")),
            "stance": _clean_str(cell(r, "Stance")),
            "command": _canon_command(tokens),
            "commandRaw": _clean_str(cell(r, "Command")),
            "tokens": tokens,
            "hitLevels": [HIT_LEVEL_NAMES.get(t, t) for t in hit_tokens],
            "hitLevelTokens": hit_tokens,
            "startup": _to_int(cell(r, "Impact")),
            "damage": _damage_list(cell(r, "Damage")),
            "block": _clean_str(cell(r, "Block")),
            "hit": _clean_str(cell(r, "Hit")),
            "counterHit": _clean_str(cell(r, "Counter Hit")),
            "guardBurst": _to_int(cell(r, "Guard Burst")),
            "notes": _clean_str(cell(r, "Notes")),
        }
        if not move["name"]:
            continue
        entry = chars.setdefault(cid, {"communityName": community_name, "moves": []})
        entry["moves"].append(move)
        move_rows += 1

    if unmapped:
        print(f"  WARN: unmapped community character names: {sorted(unmapped)}",
              file=sys.stderr)

    return {
        "_meta": {
            "source": SHEET_URL,
            "note": "Community hand-measured frame data. May contain "
                    "small human-collection errors; largely correct.",
            "characters": len(chars),
            "moveRows": move_rows,
        },
        "chars": chars,
    }


def load() -> dict[str, Any]:
    """Return the community dataset.

    Preferred source is the committed `community_framedata.json`; falls back to
    parsing the xlsx directly if the JSON is absent. Returns an empty
    payload (no crash) if neither is available — the exporter then just
    ignores this reference and keeps parser output unchanged.
    """
    if os.path.isfile(JSON_PATH):
        with open(JSON_PATH, encoding="utf-8") as f:
            return json.load(f)
    if os.path.isfile(XLSX_PATH):
        print("  community_framedata.json missing — parsing xlsx directly",
              file=sys.stderr)
        return parse_xlsx()
    print("  WARN: no community frame data found (no .json, no .xlsx)",
          file=sys.stderr)
    return {
        "_meta": {"source": SHEET_URL, "characters": 0, "moveRows": 0},
        "chars": {},
    }


def main() -> int:
    if not os.path.isfile(XLSX_PATH):
        print(f"ERROR: {XLSX_PATH} not found.\n"
              f"Download the sheet as .xlsx from:\n  {SHEET_URL}\n"
              f"and save it as community_framedata.xlsx", file=sys.stderr)
        return 1
    data = parse_xlsx()
    with open(JSON_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, separators=(",", ":"))
    meta = data["_meta"]
    print(f"Wrote {JSON_PATH}")
    print(f"  {meta['characters']} characters, {meta['moveRows']} move rows")
    for cid in sorted(data["chars"]):
        c = data["chars"][cid]
        print(f"  {cid}  {c['communityName']:<14} {len(c['moves']):>4} moves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
