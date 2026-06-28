#!/usr/bin/env python3
"""Calibrate player-facing move families from community frame-data rows.

This module is intentionally one layer above native KHD slots/cells. It models
the way players group visible command rows: command-string trees, branches,
hold variants, direction alternatives, and stance-transition suffixes. The
output is investigative for now; the parser can later promote the same schema
into exported web UI data once native timelines are stronger.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from community_framedata import load as load_community
from community_framedata import norm_input_key, norm_name


ROOT = Path(__file__).resolve().parent
DEFAULT_DATA_DIR = ROOT / "webui" / "public" / "data"

_BUTTON_CHARS = set("ABKGabkg")
_NEUTRAL_CONTEXTS = {"", "neutral", "standing", "stand", "n/a", "-"}
_DIRECTION_ALTERNATIVE_GROUPS = (
    frozenset({"1", "2", "3"}),
    frozenset({"4", "6"}),
    frozenset({"7", "8", "9"}),
)


@dataclass(frozen=True)
class CommandToken:
    text: str
    kind: str
    held: bool = False

    @property
    def skeleton(self) -> str:
        if self.held:
            return self.text.replace("(", "").replace(")", "")
        return self.text

    def as_json(self) -> dict[str, Any]:
        return {
            "text": self.text,
            "kind": self.kind,
            "held": self.held,
            "skeleton": self.skeleton,
        }


@dataclass
class FamilyRow:
    row_id: str
    cid: str
    order: int
    name: str
    root_name: str
    command: str
    context: str
    category: str
    tokens: tuple[CommandToken, ...]
    startup: int | None
    damage: tuple[int, ...]
    block: str
    hit: str
    counter_hit: str
    guard_burst: int | None
    hit_levels: tuple[str, ...]
    notes: str

    @property
    def token_key(self) -> tuple[str, ...]:
        return tuple(t.text for t in self.tokens)

    @property
    def skeleton_key(self) -> tuple[str, ...]:
        return tuple(t.skeleton for t in self.tokens)

    @property
    def held_count(self) -> int:
        return sum(1 for t in self.tokens if t.held)

    def as_json(self) -> dict[str, Any]:
        return {
            "id": self.row_id,
            "source": "community",
            "order": self.order,
            "name": self.name,
            "rootName": self.root_name,
            "command": self.command,
            "context": self.context,
            "category": self.category,
            "tokens": [t.as_json() for t in self.tokens],
            "startup": self.startup,
            "damage": list(self.damage),
            "block": self.block,
            "hit": self.hit,
            "counterHit": self.counter_hit,
            "guardBurst": self.guard_burst,
            "hitLevels": list(self.hit_levels),
            "notes": self.notes,
        }


def _clean_space(value: Any) -> str:
    return re.sub(r"\s+", " ", str(value or "").strip())


def _root_name(name: str) -> str:
    return _clean_space(re.split(r"\s*~\s*", name, maxsplit=1)[0])


def _context_for_move(move: dict[str, Any]) -> str:
    stance = _clean_space(move.get("stance"))
    if stance.lower() in _NEUTRAL_CONTEXTS:
        return "Neutral"
    return stance


def tokenize_command(command: str) -> tuple[CommandToken, ...]:
    """Tokenize SC6 command notation without raw-string prefix shortcuts.

    Chords such as ``A+B`` stay one token, so ``A`` is not a prefix of
    ``A+B``. Parenthesized buttons become held variants over the same skeleton,
    so ``4A`` and ``4(A)`` can be linked by an explicit hold edge.
    """
    s = _clean_space(command)
    out: list[CommandToken] = []
    i = 0
    while i < len(s):
        ch = s[i]
        if ch.isspace() or ch in ".:,;_/[]{}":
            i += 1
            continue
        if ch == "~":
            i += 1
            continue
        if ch == "(":
            text, i = _read_held_or_chord(s, i)
            out.append(CommandToken(text, _token_kind(text.replace("(", "").replace(")", "")), held=True))
            continue
        if ch.isdigit():
            j = i + 1
            while j < len(s) and s[j].isdigit():
                j += 1
            out.append(CommandToken(s[i:j], "direction"))
            i = j
            continue
        if ch in _BUTTON_CHARS:
            text, i, held = _read_button_or_chord(s, i)
            out.append(CommandToken(text, _token_kind(text.replace("(", "").replace(")", "")), held=held))
            continue
        if ch == "+":
            i += 1
            continue

        j = i + 1
        while j < len(s) and s[j].isalpha() and s[j] not in _BUTTON_CHARS:
            j += 1
        text = s[i:j]
        out.append(CommandToken(text, _token_kind(text)))
        i = j
    return tuple(out)


def _read_held_or_chord(s: str, start: int) -> tuple[str, int]:
    text, i = _read_parenthesized_segment(s, start)
    pieces = [text]
    while i < len(s) and s[i] == "+":
        if i + 1 >= len(s):
            break
        if s[i + 1] == "(":
            next_text, next_i = _read_parenthesized_segment(s, i + 1)
            pieces.extend(["+", next_text])
            i = next_i
            continue
        if s[i + 1] in _BUTTON_CHARS:
            pieces.extend(["+", s[i + 1]])
            i += 2
            continue
        break
    return "".join(pieces), i


def _read_parenthesized_segment(s: str, start: int) -> tuple[str, int]:
    end = s.find(")", start + 1)
    if end == -1:
        return s[start:], len(s)
    return s[start:end + 1], end + 1


def _read_button_or_chord(s: str, start: int) -> tuple[str, int, bool]:
    pieces = [s[start]]
    i = start + 1
    held = False
    while i + 1 < len(s) and s[i] == "+":
        if s[i + 1] in _BUTTON_CHARS:
            pieces.extend([s[i], s[i + 1]])
            i += 2
            continue
        if s[i + 1] == "(":
            next_text, next_i = _read_parenthesized_segment(s, i + 1)
            pieces.extend([s[i], next_text])
            i = next_i
            held = True
            continue
        break
    return "".join(pieces), i, held


def _token_kind(text: str) -> str:
    if not text:
        return "other"
    if text.isdigit():
        return "direction"
    if "+" in text and all(part in _BUTTON_CHARS for part in text.split("+")):
        return "chord"
    if text in _BUTTON_CHARS:
        return "button"
    return "other"


def _row_from_move(cid: str, order: int, move: dict[str, Any]) -> FamilyRow:
    command = _clean_space(move.get("command"))
    name = _clean_space(move.get("name"))
    return FamilyRow(
        row_id=f"community-{cid}-{order:05d}",
        cid=cid,
        order=order,
        name=name,
        root_name=_root_name(name),
        command=command,
        context=_context_for_move(move),
        category=_clean_space(move.get("category")),
        tokens=tokenize_command(command),
        startup=move.get("startup") if isinstance(move.get("startup"), int) else None,
        damage=tuple(int(v) for v in move.get("damage", []) if isinstance(v, int)),
        block=_clean_space(move.get("block")),
        hit=_clean_space(move.get("hit")),
        counter_hit=_clean_space(move.get("counterHit")),
        guard_burst=move.get("guardBurst") if isinstance(move.get("guardBurst"), int) else None,
        hit_levels=tuple(str(v) for v in move.get("hitLevels", []) if v),
        notes=_clean_space(move.get("notes")),
    )


def build_community_families(cid: str, moves: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Build player-facing family trees for one character's community rows."""
    rows = [_row_from_move(cid, i, move) for i, move in enumerate(moves)]
    edges = _build_edges(rows)
    parent = list(range(len(rows)))

    def find(idx: int) -> int:
        while parent[idx] != idx:
            parent[idx] = parent[parent[idx]]
            idx = parent[idx]
        return idx

    def union(a: int, b: int) -> None:
        ra = find(a)
        rb = find(b)
        if ra != rb:
            parent[rb] = ra

    order_by_id = {row.row_id: i for i, row in enumerate(rows)}
    for edge in edges:
        union(order_by_id[edge["parentRowId"]], order_by_id[edge["childRowId"]])

    components: dict[int, list[FamilyRow]] = {}
    for idx, row in enumerate(rows):
        components.setdefault(find(idx), []).append(row)

    edges_by_component: dict[int, list[dict[str, Any]]] = {k: [] for k in components}
    for edge in edges:
        comp = find(order_by_id[edge["parentRowId"]])
        edges_by_component.setdefault(comp, []).append(edge)

    families: list[dict[str, Any]] = []
    for seq, comp_id in enumerate(sorted(components, key=lambda c: min(r.order for r in components[c]))):
        members = sorted(components[comp_id], key=lambda r: r.order)
        root = min(members, key=lambda r: (len(r.skeleton_key), r.held_count, r.order))
        component_edges = sorted(
            edges_by_component.get(comp_id, []),
            key=lambda e: (e["parentOrder"], e["childOrder"], e["relation"]),
        )
        relation_kinds = sorted({edge["relation"] for edge in component_edges})
        confidences = {edge["confidence"] for edge in component_edges}
        family_confidence = _family_confidence(confidences, len(members))
        families.append({
            "id": f"player-family-{cid}-{seq:05d}",
            "cid": cid,
            "kind": "command-tree" if component_edges else "single-row",
            "confidence": family_confidence,
            "context": root.context,
            "rootCommand": root.command,
            "rootName": root.root_name,
            "relations": relation_kinds,
            "rows": [row.as_json() for row in members],
            "edges": component_edges,
        })
    return families


def _family_confidence(confidences: set[str], member_count: int) -> str:
    if member_count < 2:
        return "single-row"
    if "weak" in confidences:
        return "weak"
    if "medium" in confidences:
        return "community-calibrated"
    return "strong-community"


def _build_edges(rows: list[FamilyRow]) -> list[dict[str, Any]]:
    edges: dict[tuple[str, str, str], dict[str, Any]] = {}

    def add_edge(parent: FamilyRow, child: FamilyRow, relation: str, reasons: list[str],
                 confidence: str) -> None:
        if parent.row_id == child.row_id:
            return
        key = (parent.row_id, child.row_id, relation)
        edge = {
            "id": f"edge-{parent.row_id}-{child.row_id}-{relation}",
            "parentRowId": parent.row_id,
            "childRowId": child.row_id,
            "parentOrder": parent.order,
            "childOrder": child.order,
            "relation": relation,
            "confidence": confidence,
            "reasons": reasons,
            "source": "community-calibration",
        }
        old = edges.get(key)
        if old is None or _confidence_rank(confidence) > _confidence_rank(old["confidence"]):
            edges[key] = edge

    by_context: dict[str, list[FamilyRow]] = {}
    for row in rows:
        by_context.setdefault(row.context, []).append(row)

    for context_rows in by_context.values():
        _add_prefix_edges(context_rows, add_edge)
        _add_hold_edges(context_rows, add_edge)
        _add_direction_alternative_edges(context_rows, add_edge)
        _add_stance_transition_edges(context_rows, add_edge)

    return list(edges.values())


def _add_prefix_edges(rows: list[FamilyRow], add_edge: Any) -> None:
    for child in rows:
        if not child.skeleton_key:
            continue
        candidates = [
            parent for parent in rows
            if parent.order != child.order
            and _is_strict_prefix(parent.skeleton_key, child.skeleton_key)
            and _prefix_guardrail_allows(parent, child)
        ]
        if not candidates:
            continue
        parent = max(candidates, key=lambda r: (len(r.skeleton_key), -r.held_count, -r.order))
        reasons = ["command-token-prefix"]
        confidence = _confirmed_confidence(parent, child, reasons)
        add_edge(parent, child, "prefix", reasons, confidence)


def _add_hold_edges(rows: list[FamilyRow], add_edge: Any) -> None:
    by_skeleton: dict[tuple[str, ...], list[FamilyRow]] = {}
    for row in rows:
        if row.skeleton_key:
            by_skeleton.setdefault(row.skeleton_key, []).append(row)
    for members in by_skeleton.values():
        if len(members) < 2 or len({m.token_key for m in members}) < 2:
            continue
        members = sorted(members, key=lambda r: (r.held_count, r.order))
        base = members[0]
        for variant in members[1:]:
            if base.held_count == variant.held_count:
                continue
            if not _name_compatible(base, variant):
                continue
            reasons = ["same-command-skeleton", "hold-parentheses-differ"]
            confidence = _confirmed_confidence(base, variant, reasons)
            add_edge(base, variant, "hold-variant", reasons, confidence)


def _add_direction_alternative_edges(rows: list[FamilyRow], add_edge: Any) -> None:
    for i, left in enumerate(rows):
        for right in rows[i + 1:]:
            if not _is_direction_alternative(left, right):
                continue
            parent, child = sorted((left, right), key=lambda r: r.order)
            reasons = ["same-root-name", "direction-token-alternative"]
            confidence = _confirmed_confidence(parent, child, reasons)
            add_edge(parent, child, "direction-alternative", reasons, confidence)


def _add_stance_transition_edges(rows: list[FamilyRow], add_edge: Any) -> None:
    plain_rows = [row for row in rows if "~" not in row.name]
    transition_rows = [row for row in rows if "~" in row.name]
    for transition in transition_rows:
        candidates = [
            row for row in plain_rows
            if row.root_name == transition.root_name
            and (
                row.skeleton_key == transition.skeleton_key
                or _is_strict_prefix(row.skeleton_key, transition.skeleton_key)
                or _is_strict_prefix(transition.skeleton_key, row.skeleton_key)
            )
        ]
        if not candidates:
            continue
        parent = max(candidates, key=lambda r: (len(r.skeleton_key), -r.order))
        reasons = ["same-root-name-before-tilde", "compatible-command-skeleton"]
        confidence = _confirmed_confidence(parent, transition, reasons)
        add_edge(parent, transition, "stance-transition", reasons, confidence)


def _confirmed_confidence(left: FamilyRow, right: FamilyRow, reasons: list[str]) -> str:
    if left.root_name and left.root_name == right.root_name:
        reasons.append("same-root-name")
        return "strong"
    if _is_prefix(left.damage, right.damage) or _is_prefix(left.hit_levels, right.hit_levels):
        reasons.append("damage-or-hit-level-prefix")
        return "strong"
    if left.damage and right.damage and left.damage == right.damage:
        reasons.append("same-damage")
        return "medium"
    return "medium"


def _confidence_rank(confidence: str) -> int:
    return {"weak": 0, "medium": 1, "strong": 2}.get(confidence, 0)


def _prefix_guardrail_allows(parent: FamilyRow, child: FamilyRow) -> bool:
    # Same stance/context is enforced before this point. These additional
    # guardrails prevent the common false positives called out in the study.
    if not parent.skeleton_key or not child.skeleton_key:
        return False
    if parent.skeleton_key[0] != child.skeleton_key[0]:
        return False
    if parent.tokens[-1].kind == "chord":
        return False
    if len(parent.skeleton_key) == 1 and parent.tokens[0].kind == "direction":
        return False
    return True


def _name_compatible(left: FamilyRow, right: FamilyRow) -> bool:
    if left.root_name == right.root_name:
        return True
    if not left.root_name or not right.root_name:
        return False
    return False


def _is_direction_alternative(left: FamilyRow, right: FamilyRow) -> bool:
    if left.context != right.context or left.root_name != right.root_name:
        return False
    if len(left.skeleton_key) != len(right.skeleton_key):
        return False
    if left.skeleton_key == right.skeleton_key:
        return False
    differing_positions = [
        i for i, (a, b) in enumerate(zip(left.skeleton_key, right.skeleton_key))
        if a != b
    ]
    if not differing_positions:
        return False
    for pos in differing_positions:
        lt = left.tokens[pos]
        rt = right.tokens[pos]
        if lt.kind != "direction" or rt.kind != "direction":
            return False
        if not _directions_are_alternatives(lt.skeleton, rt.skeleton):
            return False
    return True


def _directions_are_alternatives(left: str, right: str) -> bool:
    if left == right:
        return False
    for group in _DIRECTION_ALTERNATIVE_GROUPS:
        if left in group and right in group:
            return True
    return False


def _is_strict_prefix(short: tuple[Any, ...], long: tuple[Any, ...]) -> bool:
    return len(short) < len(long) and long[:len(short)] == short


def _is_prefix(short: Iterable[Any], long: Iterable[Any]) -> bool:
    short_t = tuple(short)
    long_t = tuple(long)
    return bool(short_t) and len(short_t) < len(long_t) and long_t[:len(short_t)] == short_t


def summarize_community_families(
    community: dict[str, Any],
    parsed_data_dir: Path | None = None,
    example_limit: int = 8,
) -> dict[str, Any]:
    chars = community.get("chars", {})
    parser_keys, parser_group_counts = _load_parser_keys(parsed_data_dir)
    all_families: dict[str, list[dict[str, Any]]] = {}
    totals = {
        "communityRows": 0,
        "playerFamilies": 0,
        "multiRowFamilies": 0,
        "rowsInsideMultiRowFamilies": 0,
        "directParserRows": 0,
        "familiesWithParserAnchor": 0,
        "rowsInsideAnchoredFamilies": 0,
        "nonDirectRowsAttachableThroughAnchoredFamily": 0,
    }
    size_distribution: dict[str, int] = {}
    examples: list[dict[str, Any]] = []
    per_character: dict[str, dict[str, Any]] = {}

    for cid in sorted(chars):
        moves = chars[cid].get("moves", [])
        families = build_community_families(cid, moves)
        all_families[cid] = families
        direct_row_ids = _direct_parser_row_ids(cid, families, parser_keys)
        char_stats = {
            "communityName": chars[cid].get("communityName", cid),
            "communityRows": len(moves),
            "playerFamilies": len(families),
            "multiRowFamilies": 0,
            "rowsInsideMultiRowFamilies": 0,
            "directParserRows": len(direct_row_ids),
            "familiesWithParserAnchor": 0,
            "rowsInsideAnchoredFamilies": 0,
            "nonDirectRowsAttachableThroughAnchoredFamily": 0,
        }
        totals["communityRows"] += len(moves)
        totals["playerFamilies"] += len(families)
        totals["directParserRows"] += len(direct_row_ids)
        for family in families:
            size = len(family["rows"])
            bucket = "10+" if size >= 10 else str(size)
            size_distribution[bucket] = size_distribution.get(bucket, 0) + 1
            if size > 1:
                totals["multiRowFamilies"] += 1
                totals["rowsInsideMultiRowFamilies"] += size
                char_stats["multiRowFamilies"] += 1
                char_stats["rowsInsideMultiRowFamilies"] += size
            row_ids = {row["id"] for row in family["rows"]}
            anchored_ids = row_ids.intersection(direct_row_ids)
            if anchored_ids:
                totals["familiesWithParserAnchor"] += 1
                totals["rowsInsideAnchoredFamilies"] += size
                totals["nonDirectRowsAttachableThroughAnchoredFamily"] += size - len(anchored_ids)
                char_stats["familiesWithParserAnchor"] += 1
                char_stats["rowsInsideAnchoredFamilies"] += size
                char_stats["nonDirectRowsAttachableThroughAnchoredFamily"] += size - len(anchored_ids)
                if size > 1 and len(examples) < example_limit:
                    examples.append(_family_example(family, anchored_ids))
        per_character[cid] = char_stats

    return {
        "totals": totals,
        "perCharacter": per_character,
        "familySizeDistribution": dict(sorted(size_distribution.items(), key=lambda kv: _bucket_sort(kv[0]))),
        "parserMoveGroupCounts": parser_group_counts,
        "examples": examples,
        "families": all_families,
    }


def _bucket_sort(bucket: str) -> int:
    if bucket.endswith("+"):
        return int(bucket[:-1])
    return int(bucket)


def _load_parser_keys(parsed_data_dir: Path | None) -> tuple[set[tuple[str, str, str]], dict[str, int]]:
    if parsed_data_dir is None:
        return set(), {}
    chars_dir = parsed_data_dir / "chars"
    if not chars_dir.is_dir():
        return set(), {}
    keys: set[tuple[str, str, str]] = set()
    group_counts: dict[str, int] = {}
    for path in sorted(chars_dir.glob("*.json")):
        cid = path.stem
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            continue
        movelist = data.get("movelist") or {}
        for move in movelist.get("moves", []):
            name_key = norm_name(move.get("name", ""))
            condition_key = norm_name(move.get("condition", ""))
            for raw_input in _parser_inputs_for_move(move):
                keys.add((cid, name_key, norm_input_key(raw_input)))
                keys.add((cid, name_key, f"{condition_key}|{norm_input_key(raw_input)}"))
        for group in movelist.get("moveGroups", []):
            kind = str(group.get("kind", "unknown"))
            group_counts[kind] = group_counts.get(kind, 0) + 1
    return keys, group_counts


def _parser_inputs_for_move(move: dict[str, Any]) -> list[str]:
    raw = _clean_space(move.get("input"))
    inputs = [part.strip() for part in raw.split("|") if part.strip()]
    if not inputs and raw:
        inputs = [raw]
    return inputs


def _direct_parser_row_ids(
    cid: str,
    families: list[dict[str, Any]],
    parser_keys: set[tuple[str, str, str]],
) -> set[str]:
    out: set[str] = set()
    for family in families:
        for row in family["rows"]:
            name_key = norm_name(row["name"])
            input_key = norm_input_key(row["command"])
            context_key = norm_name(row["context"])
            if (
                (cid, name_key, input_key) in parser_keys
                or (cid, name_key, f"{context_key}|{input_key}") in parser_keys
            ):
                out.add(row["id"])
    return out


def _family_example(family: dict[str, Any], anchored_ids: set[str]) -> dict[str, Any]:
    return {
        "cid": family["cid"],
        "id": family["id"],
        "context": family["context"],
        "rootCommand": family["rootCommand"],
        "rootName": family["rootName"],
        "relations": family["relations"],
        "rows": [
            {
                "command": row["command"],
                "name": row["name"],
                "directParserAnchor": row["id"] in anchored_ids,
            }
            for row in family["rows"]
        ],
        "edges": [
            {
                "parentOrder": edge["parentOrder"],
                "childOrder": edge["childOrder"],
                "relation": edge["relation"],
                "confidence": edge["confidence"],
                "reasons": edge["reasons"],
            }
            for edge in family["edges"]
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--community-json", help="Parsed community frame-data JSON")
    parser.add_argument("--community-xlsx", help="Downloaded community frame-data spreadsheet")
    parser.add_argument(
        "--parsed-data-dir",
        default=str(DEFAULT_DATA_DIR),
        help="Generated webui/public/data directory used for parser anchor counts",
    )
    parser.add_argument("--summary-json", help="Optional path for the full JSON report")
    parser.add_argument("--examples", type=int, default=8, help="Number of anchored family examples to print")
    args = parser.parse_args()

    community = load_community(
        json_path=args.community_json or str(ROOT / "community_framedata.json"),
        xlsx_path=args.community_xlsx or str(ROOT / "community_framedata.xlsx"),
    )
    parsed_data_dir = Path(args.parsed_data_dir) if args.parsed_data_dir else None
    report = summarize_community_families(community, parsed_data_dir, args.examples)
    totals = report["totals"]

    print(
        "community rows: {communityRows} | player families: {playerFamilies} | "
        "multi-row families: {multiRowFamilies} | rows in multi-row families: "
        "{rowsInsideMultiRowFamilies}".format(**totals)
    )
    if report["parserMoveGroupCounts"]:
        print(f"current parser moveGroups: {report['parserMoveGroupCounts']}")
    if totals["directParserRows"]:
        print(
            "direct parser rows: {directParserRows} | anchored families: "
            "{familiesWithParserAnchor} | non-direct rows attachable through "
            "anchored family: {nonDirectRowsAttachableThroughAnchoredFamily}".format(**totals)
        )
    print(f"family size distribution: {report['familySizeDistribution']}")
    for example in report["examples"][:args.examples]:
        rows = ", ".join(
            f"{row['command']}{'*' if row['directParserAnchor'] else ''}"
            for row in example["rows"]
        )
        print(
            f"  {example['cid']} {example['rootCommand']} / {example['rootName']}: {rows}"
        )

    if args.summary_json:
        out_path = Path(args.summary_json)
        out_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
