"""Export parsed SC6 moveset data to JSON for the webui/ TanStack Start app.

Reads every .khd in BATTLE_ROOT, walks slots + cells, and writes:

    webui/public/data/roster.json        — overview of all characters
    webui/public/data/chars/<cid>.json   — full per-character payload

Re-run any time the parser changes. The webui reads the JSON via
TanStack Router loaders (no Python at runtime).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from luxformats import (
    KhdFile, LuxBattleAttackCell, attack_flags_to_str, parse_auto,
)
from move_graph import (
    build_slot_graph, identify_stance_roots,
    build_flat_moves,
    serialize_edge, serialize_effect, serialize_root, serialize_flat_move,
    USER_INPUT_KINDS,
)
import uassetparse
import locales
import community_framedata
from player_move_families import build_community_families

BATTLE_ROOT_DEFAULT = r"E:\myMods\dump\Battle"
OUT_DIR_DEFAULT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "webui", "public", "data"
)

# Optional UE4 asset / localization data. If present, gives us per-move
# canonical names + input notation + category ordering directly from the
# in-game movelist. Pass via `SC6_DUMP_ROOT` env var to override; defaults
# to the maintainer's path.
UE4_DUMP_ROOT = os.environ.get(
    "SC6_DUMP_ROOT",
    r"C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump\SoulcaliburVI\Content",
)
STYLE_ROOT = os.path.join(UE4_DUMP_ROOT, "Style")
ARCHIVE_PATH = os.path.join(
    UE4_DUMP_ROOT, "Localization", "Game", "Steam", "en", "Game.archive"
)
HAVE_UE4_DATA = os.path.isdir(STYLE_ROOT) and os.path.isfile(ARCHIVE_PATH)
_COMMUNITY_FRAME_DATA: dict[str, Any] | None = None
COMMUNITY_JSON_PATH: str | None = None
COMMUNITY_XLSX_PATH: str | None = None


def load_movelist_for_chara(
    cid: str,
    khd: KhdFile | None,
    slot_graph: Any = None,
) -> dict[str, Any] | None:
    """Parse a character's UE4 DataAsset + localization. Returns a dict
    with `categories` (the in-game movelist ordering) and `moves` (an
    item-level dict keyed by MoveListID with name / command / note /
    list of CommandSets, plus resolved cell/slot indices for the UI).
    Returns None if UE4 dump isn't available or the character's
    MovePlayData file is missing.

    `slot_graph` (optional) is the precomputed slot-transition graph
    from `build_slot_graph`. When present, multi-input moves (those whose
    `command` field has `|`-separated alternatives) get dispatcher-sibling
    variant cells attached — see `_find_dispatcher_variants` for the
    heuristic and motivation.
    """
    if not HAVE_UE4_DATA:
        return None
    # The dump directory uses lowercase cids; uppercase fallback for any
    # future char that might break the pattern. Lowercase first so that
    # we still find the data on case-sensitive filesystems.
    asset_path = os.path.join(STYLE_ROOT, cid, f"DA_MovePlayData_{cid}.uasset")
    if not os.path.isfile(asset_path):
        cid_upper = cid.upper()
        asset_path = os.path.join(STYLE_ROOT, cid_upper, f"DA_MovePlayData_{cid_upper}.uasset")
        if not os.path.isfile(asset_path):
            return None
    uexp_path = asset_path.replace(".uasset", ".uexp")
    try:
        pkg = uassetparse.parse_uasset(asset_path)
        data = uassetparse.parse_uexp(uexp_path, pkg)
    except Exception as e:
        # Return None (not a partial error dict) so the UI's `if (!movelist)`
        # guard correctly hides the tab when the data is unusable. We
        # still log so a re-run with --verbose can investigate.
        print(f"  WARN: failed to parse DA_MovePlayData_{cid}: {e}", file=sys.stderr)
        return None
    archive = locales.get_archive(ARCHIVE_PATH)
    movelist_idx = locales.build_movelist_index(archive, cid)
    move_meta = _load_move_table_metadata(cid, archive)

    community_index = _community_frame_index(cid)
    return _build_movelist_payload(cid, data, movelist_idx, khd, slot_graph, move_meta, community_index)


def _load_community_frame_data() -> dict[str, Any]:
    global _COMMUNITY_FRAME_DATA
    if _COMMUNITY_FRAME_DATA is None:
        if COMMUNITY_JSON_PATH or COMMUNITY_XLSX_PATH:
            _COMMUNITY_FRAME_DATA = community_framedata.load(
                json_path=COMMUNITY_JSON_PATH or community_framedata.JSON_PATH,
                xlsx_path=COMMUNITY_XLSX_PATH or community_framedata.XLSX_PATH,
            )
        else:
            _COMMUNITY_FRAME_DATA = community_framedata.load()
    return _COMMUNITY_FRAME_DATA


def _community_frame_index(cid: str) -> dict[tuple[str, str], list[dict[str, Any]]]:
    data = _load_community_frame_data()
    moves = data.get("chars", {}).get(cid, {}).get("moves", [])
    out: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for move in moves:
        key = (
            community_framedata.norm_name(move.get("name", "")),
            community_framedata.norm_input_key(move.get("command", "")),
        )
        out.setdefault(key, []).append(move)
    return out


def _community_frame_for_move(
    name: str,
    button_input: str,
    condition: str,
    community_index: dict[tuple[str, str], list[dict[str, Any]]],
) -> dict[str, Any] | None:
    key = (
        community_framedata.norm_name(name),
        community_framedata.norm_input_key(button_input),
    )
    candidates = community_index.get(key, [])
    if not candidates:
        return None

    cond_key = community_framedata.norm_name(condition)
    def score(move: dict[str, Any]) -> int:
        stance_key = community_framedata.norm_name(move.get("stance", ""))
        if not cond_key and not stance_key:
            return 3
        if cond_key == stance_key:
            return 3
        if stance_key and stance_key in cond_key:
            return 2
        if cond_key and cond_key in stance_key:
            return 1
        return 0

    best = max(candidates, key=score)
    return {
        "source": "community",
        "startup": best.get("startup"),
        "damage": best.get("damage", []),
        "onBlock": best.get("block", ""),
        "onHit": best.get("hit", ""),
        "onCounterHit": best.get("counterHit", ""),
        "guardBurst": best.get("guardBurst"),
        "notes": best.get("notes", ""),
    }


# Per-hit attack-class abbreviations used in DA_MoveListTable's
# `AttributeTag` field (dot-separated, one token per hit of the move).
_ATTRIBUTE_CLASS_NAMES: dict[str, str] = {
    "H": "High",
    "M": "Mid",
    "L": "Low",
    "SM": "Special Mid",
    "SL": "Special Low",
}

# Move-property abbreviations used in DA_MoveListTable's `EffectTag`
# field (dot-separated). These are the AUTHORITATIVE property tags the
# in-game movelist shows as icons — far more reliable than deriving
# class from cell flags.
_EFFECT_TAG_NAMES: dict[str, str] = {
    "LH":  "Lethal Hit",
    "BA":  "Break Attack",
    "GI":  "Guard Impact",
    "UA":  "Unblockable Attack",
    "RE":  "Reversal Edge",
    "TH":  "Throw",
    "SC":  "Soul Charge",       # move tied to the Soul Charge state
    "SS":  "Stance Shift",      # move transitions into / is part of a stance
    "SGF": "Soul Gauge: Full",
    "SGH": "Soul Gauge: Half",
    "SGQ": "Soul Gauge: Quarter",
}


def _load_move_table_metadata(
    cid: str, archive: dict[str, str],
) -> dict[int, dict[str, Any]]:
    """Parse `DA_MoveListTable_<cid>` — the UE4 DataTable that carries
    per-move metadata the in-game movelist UI shows but DA_MovePlayData
    omits: per-hit attack-class sequence (`AttributeTag`), property tags
    (`EffectTag` — LH/BA/GI/UA/...), a gameplay tip (`MainMovesTextID`),
    and the Lethal Hit trigger condition (`RethalHitTextID`).

    Returns ``{ moveId -> metadata-dict }``. The DataTable's rows are
    keyed by decimal strings "1".."N" and row N corresponds to MoveListID
    N (verified: row 1's NameTextID is `ID_CMD_<cid>_0010_name`).

    Returns ``{}`` if the file is missing or unparseable — the rest of
    the movelist still works without this enrichment.
    """
    if not HAVE_UE4_DATA:
        return {}
    path = os.path.join(STYLE_ROOT, cid, f"DA_MoveListTable_{cid}.uasset")
    if not os.path.isfile(path):
        path = os.path.join(
            STYLE_ROOT, cid.upper(), f"DA_MoveListTable_{cid.upper()}.uasset")
        if not os.path.isfile(path):
            return {}
    try:
        pkg = uassetparse.parse_uasset(path)
        rows = uassetparse.parse_datatable(path.replace(".uasset", ".uexp"), pkg)
    except Exception as e:
        print(f"  WARN: failed to parse DA_MoveListTable_{cid}: {e}", file=sys.stderr)
        return {}

    out: dict[int, dict[str, Any]] = {}
    for row_name, row in rows.items():
        if not row_name.isdigit():
            continue
        move_id = int(row_name)
        attr_tag = row.get("AttributeTag", "") or ""
        effect_tag = row.get("EffectTag", "") or ""
        hit_classes = [
            _ATTRIBUTE_CLASS_NAMES.get(t, t)
            for t in attr_tag.split(".") if t
        ]
        effect_tags = [
            {"code": t, "label": _EFFECT_TAG_NAMES.get(t, t)}
            for t in effect_tag.split(".") if t
        ]
        # Resolve the localized gameplay tip + Lethal Hit condition.
        main_key = row.get("MainMovesTextID", "") or ""
        rethal_key = row.get("RethalHitTextID", "") or ""
        out[move_id] = {
            "attributeTag": attr_tag,
            "hitClasses": hit_classes,
            "effectTags": effect_tags,
            "mainTip": (
                locales.clean_markup(archive.get(main_key, ""))
                if main_key else ""
            ),
            "lethalHitCondition": (
                locales.clean_markup(archive.get(rethal_key, ""))
                if rethal_key else ""
            ),
        }
    return out


def _is_revenge_attack_note(note: str) -> bool:
    """Derived movelist marker for the localized "Revenge attack" note.

    No dedicated DA_MoveListTable EffectTag or parsed KHD cell flag has been
    found for this property; keep it separate from `RE` (Reversal Edge).
    """
    return "revenge attack" in note.casefold()


def _resolve_main_index(
    main_index: int,
    khd: KhdFile | None,
) -> dict[str, int]:
    """Resolve a MovePlayData `MainIndex` value to the cell-and-slot the
    UI should join against. Empirically this field is hybrid:

      * For ~70% of moves: a DIRECT index into the Attack-cell table
        (i.e. it IS the cell number).
      * For the remaining ~30%: an index into the slot table; the slot's
        first valid cell variant is the move's hit data.

    Both interpretations are valid and there's no flag distinguishing
    them at this layer. Heuristic: prefer the cell interpretation if
    `cells[mainIndex]` is an Attack-role cell with a valid active
    window; otherwise fall back to the slot.
    """
    out = {
        "cellIdx": -1,
        "slotIdx": -1,
        "resolution": "none",
        "candidateCount": 0,
        "candidateBestRank": 0,
        "candidateScore": 0,
    }
    if khd is None or main_index <= 0:
        return out

    cells = khd.sections[0].entries if khd.sections else []
    slots = khd.slots

    def _score_cell(cell: LuxBattleAttackCell, variant_index: int = 0) -> int:
        """Heuristic score for selecting the best candidate attack cell."""
        if (
            cell.cell_role != "Attack"
            or cell.wI16BaseDamage <= 0
            or not cell.has_valid_active_window
        ):
            return -1_000_000
        score = 50
        score += min(cell.wI16BaseDamage, 80)
        active_len = cell.wI16MasterWindowEnd - cell.wI16MasterWindowStart
        if active_len > 0:
            score += min(active_len, 30)
        if cell.wI16MasterWindowStart >= 0:
            score += 2
        # Prefers earlier slot variants when otherwise tied.
        score += max(0, 3 - variant_index)
        return score

    def _best_slot_attack_candidate(slot_idx: int) -> tuple[int, int, int] | None:
        if slot_idx < 0 or slot_idx >= len(slots):
            return None
        slot = slots[slot_idx]
        best: tuple[int, int, int] | None = None
        for variant_idx, c in enumerate(slot.nCellBoneIndexPerVariant):
            if not (0 <= c < len(cells)):
                continue
            cell = cells[c]
            score = _score_cell(cell, variant_idx)
            if score < 0:
                continue
            cand = (score, c, variant_idx)
            if best is None or cand > best:
                best = cand
        return best

    def _best_slot_candidates(slot_indices: list[int]) -> tuple[int, int, int] | None:
        best: tuple[int, int, int] | None = None
        for slot_idx in slot_indices:
            slot_best = _best_slot_attack_candidate(slot_idx)
            if slot_best is None:
                continue
            if best is None or slot_best > best:
                best = slot_best
        return best

    # Try as cell index first.
    if 0 <= main_index < len(cells):
        cell = cells[main_index]
        if cell.cell_role == "Attack":
            direct_score = _score_cell(cell, 0)
            # Reverse-resolve to a slot that USES this cell (for navigation).
            matching_slots = [
                s_idx for s_idx, s in enumerate(slots)
                if main_index in s.nCellBoneIndexPerVariant
            ]
            matching_slot = matching_slots[0] if matching_slots else -1

            # Direct-cell selection is only trusted when the active window
            # is in a normal range. If not, try to recover via associated
            # slot variants (same cell may be referenced by a slot whose
            # other variant carries the real startup).
            if direct_score >= 0:
                out.update(
                    {
                        "cellIdx": main_index,
                        "slotIdx": matching_slot,
                        "resolution": "cell-direct",
                        "candidateCount": 1,
                        "candidateBestRank": 1,
                        "candidateScore": direct_score,
                    }
                )
                if matching_slots:
                    replacement = None
                    replacement_slot = -1
                    for slot_idx in matching_slots:
                        slot_best = _best_slot_attack_candidate(slot_idx)
                        if slot_best is None:
                            continue
                        slot_score, slot_cell_idx, _slot_variant = slot_best
                        if replacement is None or slot_score > replacement[0]:
                            replacement = (slot_score, slot_cell_idx)
                            replacement_slot = slot_idx
                    if replacement is not None:
                        slot_score, slot_cell_idx = replacement
                        out["candidateCount"] = 2
                        out["candidateScore"] = direct_score
                        if slot_score - direct_score >= 16:
                            out.update(
                                {
                                    "cellIdx": slot_cell_idx,
                                    "slotIdx": replacement_slot,
                                    "resolution": "slot-overrides-direct",
                                    "candidateBestRank": 2,
                                    "candidateScore": slot_score,
                                }
                            )
                            return out
                return out

            # Direct index was an Attack role but had invalid window values.
            # Prefer a matching-slot replacement if one has a valid startup.
            slot_replacement = _best_slot_candidates(matching_slots)
            if slot_replacement is not None:
                slot_score, slot_cell_idx, _slot_variant = slot_replacement
                out.update(
                    {
                        "cellIdx": slot_cell_idx,
                        "slotIdx": matching_slot,
                        "resolution": "slot-overrides-direct",
                        "candidateCount": 1,
                        "candidateBestRank": 1,
                        "candidateScore": slot_score,
                    }
                )
                return out

            # main_index may also be a slot index with different valid variants.
            if 0 <= main_index < len(slots):
                slot_best = _best_slot_attack_candidate(main_index)
                if slot_best is not None:
                    slot_score, slot_cell_idx, _slot_variant = slot_best
                    out.update(
                        {
                            "cellIdx": slot_cell_idx,
                            "slotIdx": main_index,
                            "resolution": "slot-overrides-direct",
                            "candidateCount": 1,
                            "candidateBestRank": 1,
                            "candidateScore": slot_score,
                        }
                    )
                    return out

            out.update(
                {
                    "cellIdx": main_index,
                    "slotIdx": matching_slot,
                    "resolution": "cell-direct-invalid-startup",
                    "candidateCount": 1,
                    "candidateBestRank": 1,
                    "candidateScore": direct_score,
                }
            )
            return out

        # main_index is non-attack as cell; try the slot interpretation
        # and return the best attack variant available.
        if 0 <= main_index < len(slots):
            slot_best = _best_slot_attack_candidate(main_index)
            if slot_best is not None:
                slot_score, slot_cell_idx, _slot_variant = slot_best
                out.update(
                    {
                        "cellIdx": slot_cell_idx,
                        "slotIdx": main_index,
                        "resolution": "slot",
                        "candidateCount": 1,
                        "candidateBestRank": 1,
                        "candidateScore": slot_score,
                    }
                )
                return out

    # Fall back to slot.
    if 0 <= main_index < len(slots):
        slot_best = _best_slot_attack_candidate(main_index)
        if slot_best is not None:
            slot_score, cell_idx, _slot_variant = slot_best
            out.update(
                {
                    "cellIdx": cell_idx,
                    "slotIdx": main_index,
                    "resolution": "slot",
                    "candidateCount": 1,
                    "candidateBestRank": 1,
                    "candidateScore": slot_score,
                }
            )
            return out
        # Slot exists but has no attack cell — still a valid navigation target.
        out["slotIdx"] = main_index
        out["resolution"] = "slot-no-cell"

    return out



_THROW_INPUT_RE = re.compile(r"\+G\b|\bG\+|\bA\+G|\bB\+G|\bK\+G|\bG\+A|\bG\+B|\bG\+K")


def _is_throw_input(button_input: str) -> bool:
    """True when the move's canonical input contains an `+G` partnership
    — i.e. it's a grab-style input. SC6 throws use `A+G`, `B+G`, `K+G`
    (often direction-modified like `4A+G`, `6A+G`, `46A+G`).

    Why we flag these: most `+G` moves resolve to the STRIKE-PHASE cell
    (the whiff/connect-detect cell, ~10-20 dmg) rather than the actual
    throw damage cell (~40-60 dmg). The throw damage cell is reached via
    an engine-mediated state transition (yarareId stamp in
    ResolveAttackVsHurtboxMask22) that isn't visible in the slot's
    static bytecode edges, so we can't reliably auto-resolve it. The
    flag lets the UI surface a "Throw" hint so users don't read the
    strike-phase stats as the throw's actual damage.
    """
    if not button_input:
        return False
    return _THROW_INPUT_RE.search(button_input) is not None


# Set of characters that can appear in a "pure direction" SC6 input —
# numpad digits, the press-and-hold paren, the held bracket, and the
# alternatives separator. A button-bearing input would have an A/B/K/G
# letter; the absence of those is the marker.
_DIRECTION_CHARS = set("123456789|[]() ")


def _is_pure_direction_input(button_input: str) -> bool:
    """True when the canonical input is only directions / charge brackets,
    with no A/B/K/G button. These moves are stance entries, sidesteps,
    backsteps, runs, etc. — pure movement / state transitions.

    Why this matters: the DA_MovePlayData's MainIndex for these moves
    typically points at a slot whose cell variants are AUTHORED for some
    OTHER move that shares the same animation slot. So `_resolve_main_index`
    happily resolves a cellIdx, but that cell is a hit for the SIBLING
    move, not for this movement — leading to entries like
    'Astaroth Side Step (2|8) — 14dmg High Grab' which is obviously
    not what side-stepping does.

    Pure-direction inputs are the easiest reliable signal that the move
    is movement-only. Pattern coverage check on the full roster: 102 of
    130 pure-direction moves had bogus damage; the remaining 28 already
    resolved to `slot-no-cell` so were fine.
    """
    if not button_input:
        return False
    return all(ch in _DIRECTION_CHARS for ch in button_input)


def _command_set_sort_key(
    cs: dict[str, Any],
    cells: list[LuxBattleAttackCell],
) -> tuple[int, int, int, int, int]:
    """Score command-sets for canonical selection.

    Higher tuples are better in sort order. Preference is:
    1) concrete attack-cell hits over slot-only/navigation-only entries
    2) concrete resolution quality
    3) higher candidate score
    4) earlier authoring index (lower commandSetIndex)
    5) lower cell index as stable tie-breaker
    """
    cell_idx = cs.get("cellIdx", -1)
    resolution = str(cs.get("resolution") or "none")
    candidate_score = int(cs.get("candidateScore", 0) or 0)
    command_set_index = int(cs.get("commandSetIndex", 0) or 0)

    resolution_rank = {
        "cell-direct": 6,
        "slot-overrides-direct": 5,
        "cell-direct-invalid-startup": 4,
        "cell": 4,
        "slot": 3,
        "slot-no-cell": 2,
        "movement-only": 1,
        "none": 0,
    }.get(resolution, 1)

    if 0 <= cell_idx < len(cells):
        c = cells[cell_idx]
        has_attack = 1 if (c.cell_role == "Attack" and c.wI16BaseDamage > 0 and c.has_valid_active_window) else 0
    else:
        has_attack = 0

    return (
        has_attack,
        resolution_rank,
        candidate_score,
        -command_set_index,
        -cell_idx if cell_idx >= 0 else -9999,
    )


def _find_dispatcher_variants(
    primary_slot: int,
    primary_cell: int,
    khd: KhdFile,
    slot_graph: Any,
) -> list[dict[str, Any]]:
    """Find alternate attack cells reachable from the same parent
    "dispatcher" slot via direction- or button-tagged edges.

    Why this exists: Bandai's DA_MovePlayData often collapses several
    input variants (e.g. "B+K | 6B+K | 4B+K") into ONE MoveListItem with
    a single MainIndex. But the engine's stance dispatcher routes each
    directional variant to a DIFFERENT child slot with different stats.
    Concrete case (verified 2026-05-16): Astaroth "Stinging Souls"
      - B+K (neutral)     -> slot 525, cell 317  (Mid, 32 dmg, i37, BA+GIimm+GB)
      - back+forward B+K  -> slot 528, cell 319  (Mid, 60 dmg, i73, plain)
    Both are listed under one localization entry but they're plainly
    different moves on the same stance dispatcher.

    Heuristic: from the move's primary slot, walk up to the unique
    dispatcher slot (the slot with the most outgoing edges among the
    incoming-edge sources). Then list that dispatcher's OTHER outgoing
    edges whose destination has an Attack cell. Filter to direction /
    button / motion / unconditional edge kinds — these are the variant
    paths a player can take. Excludes the primary slot itself so the
    variants don't include the cell we're already showing.

    The result is a list of `{cellIdx, slotIdx, hint}` records the UI
    can render under a "Direction variants" panel.
    """
    if khd is None or slot_graph is None:
        return []
    cells = khd.sections[0].entries if khd.sections else []
    if not cells or primary_slot < 0 or primary_slot >= len(khd.slots):
        return []

    # Find the dispatcher: pick the incoming-edge source with the
    # highest fan-out (most outgoing edges). For a stance like SE this
    # is the stance "router" slot.
    incoming = [
        e for e in slot_graph.edges_by_dst.get(primary_slot, [])
        if e.dst_bank == 0
    ]
    if not incoming:
        return []
    best_dispatcher = -1
    best_fanout = -1
    for e in incoming:
        fanout = len(slot_graph.edges_by_src.get(e.src_slot, []))
        if fanout > best_fanout:
            best_fanout = fanout
            best_dispatcher = e.src_slot
    # A real dispatcher fans out to many destinations (stance routers
    # typically have 10+ outgoing edges). If fan-out is low we're
    # probably looking at a sequential chain rather than a router.
    if best_dispatcher < 0 or best_fanout < 4:
        return []

    sibling_edges = slot_graph.edges_by_src.get(best_dispatcher, [])
    variants: list[dict[str, Any]] = []
    seen_cells: set[int] = {primary_cell}
    for e in sibling_edges:
        if e.dst_slot == primary_slot or e.dst_bank != 0:
            continue
        # For "B+K | 6B+K | 4B+K"-style entries the meaningful variants
        # are the DIRECTION-modified sibling slots — pressing 4 vs 6 vs
        # neutral routes the same button pair to different cells. Button
        # edges (`hold:G`, `hold:K`) from the dispatcher are unrelated
        # sibling moves (e.g. SE K is a different move from SE B+K) and
        # would muddy the variant list, so we exclude them. `command`
        # edges (236-style motions) are kept since they're direction
        # patterns too.
        if e.predicate_kind not in ("direction", "command"):
            continue
        if e.dst_slot >= len(khd.slots):
            continue
        sibling = khd.slots[e.dst_slot]
        for cv in sibling.nCellBoneIndexPerVariant:
            if cv < 0 or cv >= len(cells):
                continue
            c = cells[cv]
            if c.cell_role != "Attack" or c.wI16BaseDamage <= 0:
                continue
            if cv in seen_cells:
                continue
            seen_cells.add(cv)
            # `hint` is the predicate label from the bytecode walker
            # (e.g. "hold:K", "(back+forward)", "(any:up)"). Useful as a
            # contextual annotation but NOT precise enough to declare
            # "this is the 6B+K variant" — the predicates are too loose.
            variants.append({
                "cellIdx": cv,
                "slotIdx": e.dst_slot,
                "hint": e.predicate_text,
                "kind": e.predicate_kind,
            })
            break  # one cell per sibling slot is enough
    return variants


def _tracking_for_slot(slot_idx: int, slot_graph: Any = None) -> dict[str, Any]:
    """Facing/tracking MoveVM effect events authored on one slot's bytecode."""
    if slot_idx < 0 or slot_graph is None:
        events = []
    else:
        events = [
            serialize_effect(e)
            for e in slot_graph.effects_by_src.get(slot_idx, [])
            if e.is_facing_related
        ]
    weights = [e["targetWeight"] for e in events if e.get("targetWeight") is not None]
    return {
        "hasFacingCommit": any(e.get("opcode") == 0x1A for e in events),
        "hasRetrackRamp": any(e.get("opcode") in (0x3B, 0x3C) for e in events),
        "maxTargetWeight": max(weights) if weights else None,
        "events": events,
    }


def _merge_tracking(command_sets: list[dict[str, Any]]) -> dict[str, Any]:
    events: list[dict[str, Any]] = []
    for cs in command_sets:
        events.extend(cs.get("tracking", {}).get("events", []))
    weights = [e["targetWeight"] for e in events if e.get("targetWeight") is not None]
    return {
        "hasFacingCommit": any(e.get("opcode") == 0x1A for e in events),
        "hasRetrackRamp": any(e.get("opcode") in (0x3B, 0x3C) for e in events),
        "maxTargetWeight": max(weights) if weights else None,
        "events": events,
    }


def _is_input_family_extension(long_input: str, short_input: str) -> bool:
    """True when `long_input` extends `short_input` at a string boundary."""
    return (
        len(long_input) > len(short_input)
        and long_input.startswith(short_input)
        and long_input[len(short_input)] in ".~"
    )


def _build_move_groups(moves: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Build player-facing grouping hints for movelist rows.

    This intentionally does not delete or merge moves. It exports grouping
    metadata so consumers can choose whether to show raw rows, duplicate-folded
    rows, or string/follow-up families.
    """
    for move in moves:
        move["groupIds"] = []

    groups: list[dict[str, Any]] = []
    group_ids_by_order: dict[int, list[str]] = {}

    def attach_group(kind: str, members: list[dict[str, Any]], reason: str) -> None:
        if len(members) < 2:
            return
        root = min(members, key=lambda m: (len(m.get("input") or ""), m.get("order", 0)))
        group_id = f"{kind}-{len(groups)}"
        orders = sorted(int(m["order"]) for m in members)
        groups.append({
            "id": group_id,
            "kind": kind,
            "reason": reason,
            "rootOrder": int(root["order"]),
            "orders": orders,
            "moveIds": sorted({int(m.get("moveId", 0) or 0) for m in members}),
            "condition": root.get("condition", ""),
            "baseInput": root.get("input", ""),
            "displayName": root.get("name", ""),
        })
        for order in orders:
            group_ids_by_order.setdefault(order, []).append(group_id)

    by_move_id: dict[int, list[dict[str, Any]]] = {}
    for move in moves:
        move_id = int(move.get("moveId", 0) or 0)
        if move_id:
            by_move_id.setdefault(move_id, []).append(move)
    for members in by_move_id.values():
        attach_group(
            "duplicate-move-id",
            members,
            "same DA_MovePlayData MoveListID appears in multiple movelist rows",
        )

    # Connected components over same-condition inputs. Exact duplicates and
    # input extensions at "."/"~" boundaries belong to the same string family.
    parent = list(range(len(moves)))

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

    for i, a in enumerate(moves):
        input_a = a.get("input", "") or ""
        if not input_a:
            continue
        for j in range(i + 1, len(moves)):
            b = moves[j]
            if a.get("condition", "") != b.get("condition", ""):
                continue
            input_b = b.get("input", "") or ""
            if not input_b:
                continue
            if (
                input_a == input_b
                or _is_input_family_extension(input_a, input_b)
                or _is_input_family_extension(input_b, input_a)
            ):
                union(i, j)

    components: dict[int, list[dict[str, Any]]] = {}
    for i, move in enumerate(moves):
        components.setdefault(find(i), []).append(move)
    for members in components.values():
        attach_group(
            "input-family",
            members,
            "same condition and exact/extended input at '.' or '~' continuation boundary",
        )

    for move in moves:
        move["groupIds"] = group_ids_by_order.get(int(move["order"]), [])
    return groups


def _split_move_inputs(raw_input: str) -> list[str]:
    inputs = [part.strip() for part in (raw_input or "").split("|") if part.strip()]
    return inputs or ([raw_input] if raw_input else [])


def _move_native_refs(move: dict[str, Any]) -> tuple[list[int], list[int]]:
    slots: set[int] = set()
    cells: set[int] = set()
    for cs in move.get("commandSets", []):
        raw_slot_idx = cs.get("slotIdx", -1)
        raw_cell_idx = cs.get("cellIdx", -1)
        slot_idx = int(raw_slot_idx) if raw_slot_idx is not None else -1
        cell_idx = int(raw_cell_idx) if raw_cell_idx is not None else -1
        if slot_idx >= 0:
            slots.add(slot_idx)
        if cell_idx >= 0:
            cells.add(cell_idx)
    return sorted(slots), sorted(cells)


def _move_metrics(move: dict[str, Any], khd: KhdFile | None) -> dict[str, Any]:
    community_frame = move.get("communityFrame")
    if community_frame:
        return {
            "startup": community_frame.get("startup"),
            "damage": community_frame.get("damage", []),
            "block": community_frame.get("onBlock") or None,
            "hit": community_frame.get("onHit") or None,
            "counterHit": community_frame.get("onCounterHit") or None,
            "hitLevels": move.get("hitClasses", []),
        }

    cell = None
    if khd and khd.sections:
        cells = khd.sections[0].entries
        for cs in move.get("commandSets", []):
            raw_cell_idx = cs.get("cellIdx", -1)
            cell_idx = int(raw_cell_idx) if raw_cell_idx is not None else -1
            if 0 <= cell_idx < len(cells):
                cell = cells[cell_idx]
                break
    if cell is None:
        return {
            "startup": None,
            "damage": [],
            "block": None,
            "hit": None,
            "counterHit": None,
            "hitLevels": move.get("hitClasses", []),
        }
    return {
        "startup": int(cell.wI16MasterWindowStart),
        "damage": [int(cell.wI16BaseDamage)] if cell.wI16BaseDamage else [],
        "block": int(cell.wI16BlockstunFrames),
        "hit": int(cell.wI16HitstunStandingNormal),
        "counterHit": None,
        "hitLevels": move.get("hitClasses", []) or [cell.attack_class],
    }


def _community_confidence(value: str, has_parser_anchor: bool) -> str:
    if has_parser_anchor:
        return "mixed-supported"
    if value in {"strong-community", "community-calibrated", "single-row"}:
        return "community-confirmed"
    if value == "weak":
        return "weak"
    return "community-confirmed"


def _edge_confidence(value: str) -> str:
    if value == "strong":
        return "community-confirmed"
    if value == "medium":
        return "community-confirmed"
    if value == "weak":
        return "weak"
    return "unknown"


def _family_confidence(rows: list[dict[str, Any]], fallback: str = "unknown") -> str:
    confidences = {row.get("confidence") for row in rows}
    if "conflict" in confidences:
        return "conflict"
    if "mixed-supported" in confidences:
        return "mixed-supported"
    if "community-confirmed" in confidences:
        return "community-confirmed"
    if "native-inferred" in confidences:
        return "native-inferred"
    if "weak" in confidences:
        return "weak"
    return fallback


def _parser_match_index(moves: list[dict[str, Any]]) -> dict[tuple[str, str], list[dict[str, Any]]]:
    index: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for move in moves:
        name_key = community_framedata.norm_name(move.get("name", ""))
        for raw_input in _split_move_inputs(move.get("input", "")):
            key = (name_key, community_framedata.norm_input_key(raw_input))
            index.setdefault(key, []).append(move)
    return index


def _context_score(context: str, move: dict[str, Any]) -> int:
    context_key = community_framedata.norm_name(context)
    condition_key = community_framedata.norm_name(move.get("condition", ""))
    if context_key in {"", "neutral"} and not condition_key:
        return 3
    if context_key == condition_key:
        return 3
    if context_key and context_key in condition_key:
        return 2
    if condition_key and condition_key in context_key:
        return 1
    return 0


def _matching_parser_moves(
    row: dict[str, Any],
    parser_index: dict[tuple[str, str], list[dict[str, Any]]],
) -> list[dict[str, Any]]:
    key = (
        community_framedata.norm_name(row.get("name", "")),
        community_framedata.norm_input_key(row.get("command", "")),
    )
    candidates = parser_index.get(key, [])
    if not candidates:
        return []
    best_score = max(_context_score(row.get("context", ""), move) for move in candidates)
    return [move for move in candidates if _context_score(row.get("context", ""), move) == best_score]


def _player_row_from_community(
    row: dict[str, Any],
    matches: list[dict[str, Any]],
    family_confidence: str,
) -> dict[str, Any]:
    parser_orders = sorted({int(move["order"]) for move in matches})
    slots: set[int] = set()
    cells: set[int] = set()
    hit_levels: list[str] = list(row.get("hitLevels", []))
    for move in matches:
        move_slots, move_cells = _move_native_refs(move)
        slots.update(move_slots)
        cells.update(move_cells)
        if not hit_levels and move.get("hitClasses"):
            hit_levels = list(move.get("hitClasses", []))
    has_anchor = bool(matches)
    return {
        "id": row["id"],
        "displayCommand": row.get("command", ""),
        "displayName": row.get("name", ""),
        "rootName": row.get("rootName", row.get("name", "")),
        "context": row.get("context", "Neutral") or "Neutral",
        "category": row.get("category", ""),
        "tokens": row.get("tokens", []),
        "source": "mixed" if has_anchor else "community",
        "confidence": _community_confidence(family_confidence, has_anchor),
        "parserMoveOrders": parser_orders,
        "nativeSlots": sorted(slots),
        "nativeCells": sorted(cells),
        "metrics": {
            "startup": row.get("startup"),
            "damage": list(row.get("damage", [])),
            "block": row.get("block") or None,
            "hit": row.get("hit") or None,
            "counterHit": row.get("counterHit") or None,
            "hitLevels": hit_levels,
        },
        "notes": row.get("notes", ""),
        "guardBurst": row.get("guardBurst"),
        "timelineStatus": "partial" if has_anchor else "unresolved",
    }


def _player_row_from_parser_move(
    cid: str,
    move: dict[str, Any],
    khd: KhdFile | None,
    source: str = "movelist",
    confidence: str = "native-inferred",
) -> dict[str, Any]:
    slots, cells = _move_native_refs(move)
    return {
        "id": f"movelist-{cid}-{int(move['order']):05d}",
        "displayCommand": move.get("input", ""),
        "displayName": move.get("name", ""),
        "rootName": re.split(r"\s*~\s*", move.get("name", ""), maxsplit=1)[0],
        "context": move.get("condition", "") or "Neutral",
        "category": str(move.get("category", "")),
        "tokens": [],
        "source": source,
        "confidence": confidence,
        "parserMoveOrders": [int(move["order"])],
        "nativeSlots": slots,
        "nativeCells": cells,
        "metrics": _move_metrics(move, khd),
        "notes": move.get("note", ""),
        "guardBurst": move.get("communityFrame", {}).get("guardBurst") if move.get("communityFrame") else None,
        "timelineStatus": "partial" if move.get("communityFrame") else "native-cell-only",
    }


def _normalize_export_family(family: dict[str, Any], rows: list[dict[str, Any]]) -> dict[str, Any]:
    edge_order = {row["id"]: i for i, row in enumerate(rows)}
    edges = []
    for edge in family.get("edges", []):
        if edge.get("parentRowId") not in edge_order or edge.get("childRowId") not in edge_order:
            continue
        edges.append({
            "id": edge.get("id", ""),
            "parentRowId": edge.get("parentRowId", ""),
            "childRowId": edge.get("childRowId", ""),
            "relation": edge.get("relation", ""),
            "confidence": _edge_confidence(edge.get("confidence", "")),
            "reasons": edge.get("reasons", []),
            "source": edge.get("source", "community-calibration"),
        })
    return {
        "id": family["id"],
        "cid": family.get("cid", ""),
        "kind": family.get("kind", "command-tree"),
        "rootCommand": family.get("rootCommand", rows[0].get("displayCommand", "") if rows else ""),
        "rootName": family.get("rootName", rows[0].get("displayName", "") if rows else ""),
        "context": family.get("context", rows[0].get("context", "Neutral") if rows else "Neutral"),
        "confidence": _family_confidence(rows),
        "relations": sorted({edge["relation"] for edge in edges}),
        "rows": rows,
        "edges": edges,
    }


def _parser_fallback_family(
    cid: str,
    seq: int,
    members: list[dict[str, Any]],
    khd: KhdFile | None,
    relation: str,
    reason: str,
) -> dict[str, Any]:
    members = sorted(members, key=lambda move: int(move.get("order", 0)))
    rows = [_player_row_from_parser_move(cid, move, khd) for move in members]
    root = min(rows, key=lambda row: (len(row.get("displayCommand", "")), row["parserMoveOrders"][0]))
    edges = []
    for row in rows:
        if row["id"] == root["id"]:
            continue
        edges.append({
            "id": f"edge-{root['id']}-{row['id']}-{relation}",
            "parentRowId": root["id"],
            "childRowId": row["id"],
            "relation": relation,
            "confidence": "native-inferred",
            "reasons": [reason],
            "source": "parser-fallback",
        })
    return {
        "id": f"player-family-{cid}-fallback-{seq:05d}",
        "cid": cid,
        "kind": "parser-fallback" if len(rows) > 1 else "single-row",
        "rootCommand": root.get("displayCommand", ""),
        "rootName": root.get("rootName", root.get("displayName", "")),
        "context": root.get("context", "Neutral"),
        "confidence": _family_confidence(rows, "native-inferred"),
        "relations": sorted({edge["relation"] for edge in edges}),
        "rows": rows,
        "edges": edges,
    }


def _build_parser_fallback_families(
    cid: str,
    moves: list[dict[str, Any]],
    move_groups: list[dict[str, Any]],
    khd: KhdFile | None,
    covered_orders: set[int],
    start_seq: int,
) -> list[dict[str, Any]]:
    moves_by_order = {int(move["order"]): move for move in moves}
    assigned: set[int] = set()
    families: list[dict[str, Any]] = []

    def add_group(group: dict[str, Any], relation: str, reason: str) -> None:
        nonlocal start_seq
        orders = [int(order) for order in group.get("orders", [])]
        orders = [order for order in orders if order not in covered_orders and order not in assigned and order in moves_by_order]
        if len(orders) < 2:
            return
        members = [moves_by_order[order] for order in orders]
        families.append(_parser_fallback_family(cid, start_seq, members, khd, relation, reason))
        assigned.update(orders)
        start_seq += 1

    for group in move_groups:
        if group.get("kind") == "input-family":
            add_group(group, "prefix", "parser input-family moveGroup")
    for group in move_groups:
        if group.get("kind") == "duplicate-move-id":
            add_group(group, "duplicate-listing", "same MoveListID appears in multiple parser rows")

    for move in moves:
        order = int(move["order"])
        if order in covered_orders or order in assigned:
            continue
        families.append(_parser_fallback_family(
            cid,
            start_seq,
            [move],
            khd,
            "single-row",
            "parser movelist row without community-calibrated family",
        ))
        assigned.add(order)
        start_seq += 1
    return families


def _build_player_move_families(
    cid: str,
    moves: list[dict[str, Any]],
    move_groups: list[dict[str, Any]],
    khd: KhdFile | None,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    community_data = _load_community_frame_data()
    community_moves = community_data.get("chars", {}).get(cid, {}).get("moves", [])
    parser_index = _parser_match_index(moves)
    families: list[dict[str, Any]] = []
    covered_orders: set[int] = set()

    if community_moves:
        for family in build_community_families(cid, community_moves):
            export_rows = []
            for row in family.get("rows", []):
                matches = _matching_parser_moves(row, parser_index)
                covered_orders.update(int(move["order"]) for move in matches)
                export_rows.append(_player_row_from_community(row, matches, family.get("confidence", "")))
            families.append(_normalize_export_family(family, export_rows))

    families.extend(_build_parser_fallback_families(
        cid,
        moves,
        move_groups,
        khd,
        covered_orders,
        len(families),
    ))

    source_counts = Counter()
    confidence_counts = Counter()
    timeline_counts = Counter()
    player_row_count = 0
    for family in families:
        confidence_counts[family["confidence"]] += 1
        player_row_count += len(family["rows"])
        for row in family["rows"]:
            source_counts[row["source"]] += 1
            timeline_counts[row["timelineStatus"]] += 1

    summary = {
        "rawMoveRows": len(moves),
        "playerFamilies": len(families),
        "playerRows": player_row_count,
        "communityRows": len(community_moves),
        "communityCoveredParserRows": len(covered_orders),
        "parserFallbackFamilies": sum(1 for family in families if family.get("kind") == "parser-fallback"),
        "sourceCounts": dict(source_counts),
        "confidenceCounts": dict(confidence_counts),
        "timelineStatusCounts": dict(timeline_counts),
    }
    return families, summary


def _build_movelist_payload(
    cid: str,
    data: dict[str, Any],
    movelist_idx: dict[int, Any],
    khd: KhdFile | None,
    slot_graph: Any = None,
    move_meta: dict[int, dict[str, Any]] | None = None,
    community_index: dict[tuple[str, str], list[dict[str, Any]]] | None = None,
) -> dict[str, Any]:
    move_meta = move_meta or {}
    community_index = community_index or {}
    categories: list[dict[str, Any]] = []
    moves: list[dict[str, Any]] = []
    item_order = 0
    for cat_idx, cat in enumerate(data.get("CategoryPlayList", [])):
        cat_items: list[int] = []
        for item in cat.get("Items", []):
            move_id = item.get("MoveListID", 0)
            param = item.get("Param", {}) or {}
            command_sets = []
            for command_set_index, cs in enumerate(param.get("CommandSets", [])):
                mi = cs.get("MainIndex", 0)
                if not mi:
                    continue
                resolved = _resolve_main_index(mi, khd)
                # Drop entries we can't resolve to anything — they'd be
                # un-clickable + statless in the UI, just noise.
                if resolved["resolution"] == "none":
                    continue
                command_sets.append({
                    "commandSetIndex": command_set_index,
                    "mainIndex": mi,
                    "introIndex": cs.get("IntroIndex", 0),
                    "cellIdx": resolved["cellIdx"],
                    "slotIdx": resolved["slotIdx"],
                    "resolution": resolved["resolution"],
                    "candidateCount": resolved["candidateCount"],
                    "candidateBestRank": resolved["candidateBestRank"],
                    "candidateScore": resolved["candidateScore"],
                    "tracking": _tracking_for_slot(resolved["slotIdx"], slot_graph),
                })
            if not command_sets:
                continue
            khd_cells = khd.sections[0].entries if khd and khd.sections else []
            if len(command_sets) > 1 and khd_cells:
                command_sets.sort(
                    key=lambda cs: _command_set_sort_key(cs, khd_cells),
                    reverse=True,
                )
            for i, cs in enumerate(command_sets):
                cs["commandSetIndex"] = i
            entry = movelist_idx.get(move_id)
            full_cmd = entry.command if entry else ""
            note = entry.note if entry else ""
            condition, button_input = locales.split_condition_and_input(full_cmd)
            # Pure-direction inputs (sidesteps, stance entries, runs)
            # don't actually hit — null out the resolved cellIdx so the
            # UI doesn't pretend the slot's borrowed cell is the move's
            # hit. The slotIdx is preserved so users can still navigate
            # to the slot's bytecode + transitions.
            movement_only = _is_pure_direction_input(button_input)
            if movement_only:
                for cs in command_sets:
                    cs["cellIdx"] = -1
                    cs["resolution"] = "movement-only"
            # Multi-input moves (Bandai collapses several user-facing
            # inputs into one MoveListItem). Look up sibling slots from
            # the parent stance dispatcher so the UI can surface the
            # directional variants the player would actually feel
            # in-game.
            has_input_alternatives = "|" in button_input and not movement_only
            input_variants: list[dict[str, Any]] = []
            if has_input_alternatives and command_sets and slot_graph is not None and khd is not None:
                primary = command_sets[0]
                input_variants = _find_dispatcher_variants(
                    primary.get("slotIdx", -1),
                    primary.get("cellIdx", -1),
                    khd,
                    slot_graph,
                )
            # DA_MoveListTable per-move metadata, keyed by MoveListID.
            _meta = move_meta.get(move_id, {})
            community_frame = _community_frame_for_move(
                entry.name if entry else "",
                button_input,
                condition,
                community_index,
            )
            moves.append({
                "moveId": move_id,
                "category": cat_idx,
                "order": item_order,
                "name": entry.name if entry else "",
                # Split the command into a stance/condition prefix and the
                # button-input portion so the UI can show them in separate
                # columns and filter on each independently.
                "condition": condition,
                "input": button_input,
                # Keep the full command + raw markup for users who want
                # the original combined string.
                "fullCommand": full_cmd,
                "inputMarkup": entry.command_markup if entry else "",
                "note": note,
                # Derived from localized NoteTextID text. This is not a
                # DA_MoveListTable EffectTag; `RE` remains Reversal Edge.
                "isRevengeAttack": _is_revenge_attack_note(note),
                # Flag for the UI — movement-only moves should render
                # without the frame-data columns rather than showing
                # the borrowed-cell stats as if they belonged to the move.
                "isMovementOnly": movement_only,
                # True when Bandai's localization lists multiple `|`-
                # separated input variants under one entry. UI should
                # surface this + the variant cells found via dispatcher
                # sibling lookup.
                "hasInputAlternatives": has_input_alternatives,
                "inputVariants": input_variants,
                "tracking": _merge_tracking(command_sets),
                "communityFrame": community_frame,
                # Throw-input marker — input contains `+G`. The resolved
                # cell is usually the STRIKE-PHASE / whiff cell, not the
                # actual throw cinematic damage cell. UI should show a
                # "Throw" hint so users don't misread the strike stats
                # as the throw's full damage. See `_is_throw_input` for
                # the rationale.
                "isThrowInput": _is_throw_input(button_input),
                # DA_MoveListTable metadata (authoritative per-move data
                # the in-game movelist UI shows). `effectTags` is the
                # property-icon set — LH/BA/GI/UA/RE/TH/SC/SS/SG* — far
                # more reliable than deriving class from cell flags.
                # `hitClasses` is the per-hit attack-class sequence
                # (e.g. ["Mid","Mid","Mid"] for a 3-hit move). `mainTip`
                # is a gameplay hint; `lethalHitCondition` is the LH
                # trigger text. Empty when the table row is missing.
                "attributeTag": _meta.get("attributeTag", ""),
                "hitClasses": _meta.get("hitClasses", []),
                "effectTags": _meta.get("effectTags", []),
                "mainTip": _meta.get("mainTip", ""),
                "lethalHitCondition": _meta.get("lethalHitCondition", ""),
                "commandSets": command_sets,
            })
            cat_items.append(item_order)
            item_order += 1
        categories.append({
            "index": cat_idx,
            # The 11 CategoryPlayList entries are positional — index N is
            # always the same localized movelist tab (see
            # locales.MOVELIST_CATEGORY_NAMES). "Main Attacks" (index 0)
            # is a curated highlight list re-listing moves from the
            # type-specific tabs.
            "name": locales.movelist_category_name(cat_idx),
            "itemOrders": cat_items,
        })
    move_groups = _build_move_groups(moves)
    player_families, player_summary = _build_player_move_families(
        cid,
        moves,
        move_groups,
        khd,
    )
    return {
        "ryuuhaType": data.get("RyuuhaType", 0) or 0,
        "categories": categories,
        "moves": moves,
        "moveGroups": move_groups,
        "playerMoveFamilies": player_families,
        "playerMoveSummary": player_summary,
    }

# Canonical SC6 chara id -> display name + roster kind.
#
# NAMES are GROUND TRUTH: the game's own character display-name string,
# `ID_CMN_Char_D_<cid>` in Game.archive (extracted + verified 2026-05-16).
# This REPLACES the earlier stance-signature guessing, which still had 6
# cids wrong: it put "Sophitia" on BOTH 002 and 006 (002 is actually
# Seong Mi-na), swapped Amy<->Tira (023/030), and mislabelled the
# base-roster newcomers Grøh/Azwel (062/064) as the DLC guests
# Cassandra/Haohmaru. The duplicate Sophitia was the tell. The `cid` is
# the style id shared by hdr<cid>.khd, Style/<cid>/, ID_CMD_<cid>_* and
# ID_CMN_Char_D_<cid>; cross-checked against the ELuxCharacter enum order
# and the movelists (e.g. cid 030 = French rapier moves => Amy).
#
# `kind` ∈ {base, dlc, boss, shared, unknown}. dlc = the style cid is
# gated in SC6.exe's BuildDlcGatedRuntimeConditionList @ 0x140130d60 or
# is a DLC-pack entry in BuildCharaSelectRosterTable @ 0x1405d3870. The
# 8 dlc cids: 009 Hwang, 017 Cassandra, 022 Setsuka, 023 Tira, 028
# Hilde, 030 Amy, 060 2B, 061 Haohmaru. Grøh (062)
# and Azwel (064) are NEW base-roster characters and Geralt (065) is a
# base-game guest — all "base". Zasalamel (024) is base. boss = Inferno
# (013). cid 066 is "unknown" — it has moveset data but is absent from
# the engine's playable roster (see its entry below).
CHARA_NAMES: dict[str, dict[str, Any]] = {
    "000": {"name": "Shared Motion",   "kind": "shared"},
    "001": {"name": "Mitsurugi",       "kind": "base"},     # ID_CMN_Char_D_001
    "002": {"name": "Seong Mi-na",     "kind": "base"},     # ID_CMN_Char_D_002 (CORRECTED 2026-05-16 — was wrongly "Sophitia"; that duplicated 006)
    "003": {"name": "Taki",            "kind": "base"},     # ID_CMN_Char_D_003
    "004": {"name": "Maxi",            "kind": "base"},     # ID_CMN_Char_D_004
    "005": {"name": "Voldo",           "kind": "base"},     # ID_CMN_Char_D_005
    "006": {"name": "Sophitia",        "kind": "base"},     # ID_CMN_Char_D_006 (CORRECTED 2026-05-16 — was "Sophitia (alt)"; this is the real & only Sophitia, 002 is Mi-na)
    "007": {"name": "Siegfried",       "kind": "base"},     # ID_CMN_Char_D_007
    "009": {"name": "Hwang",           "kind": "dlc"},      # ID_CMN_Char_D_009 — DLC (roster entry ID_DLC13_CMN_Char_D_009)
    "00b": {"name": "Ivy",             "kind": "base"},     # ID_CMN_Char_D_00B
    "00c": {"name": "Kilik",           "kind": "base"},     # ID_CMN_Char_D_00C
    "00d": {"name": "Xianghua",        "kind": "base"},     # ID_CMN_Char_D_00D
    "00f": {"name": "Yoshimitsu",      "kind": "base"},     # ID_CMN_Char_D_00F
    "011": {"name": "Nightmare",       "kind": "base"},     # ID_CMN_Char_D_011
    "012": {"name": "Astaroth",        "kind": "base"},     # ID_CMN_Char_D_012
    "013": {"name": "Inferno",         "kind": "boss"},     # ID_CMN_Char_D_013 (final boss)
    "014": {"name": "Cervantes",       "kind": "base"},     # ID_CMN_Char_D_014
    "015": {"name": "Raphael",         "kind": "base"},     # ID_CMN_Char_D_015
    "016": {"name": "Talim",           "kind": "base"},     # ID_CMN_Char_D_016
    "017": {"name": "Cassandra",       "kind": "dlc"},      # ID_CMN_Char_D_017 — DLC (roster entry ID_DLC6_CMN_Char_D_017)
    "022": {"name": "Setsuka",         "kind": "dlc"},      # DLC (roster ID_DLC11_CMN_Char_D_022). No ID_CMN_Char_D_022 key in this archive; identified by its move "Setsuka Stomp" + DLC11 = last season-2 character.
    "023": {"name": "Tira",            "kind": "dlc"},      # ID_CMN_Char_D_023 (CORRECTED 2026-05-16 — was wrongly "Amy"; Amy/Tira were swapped)
    "024": {"name": "Zasalamel",       "kind": "base"},     # ID_CMN_Char_D_024 — BASE (ELC/EFS_ZASALAMEL=0x11, base playable block; absent from BuildDlcGatedRuntimeConditionList @ 0x140130d60)
    "028": {"name": "Hilde",           "kind": "dlc"},      # DLC (roster ID_DLC7_CMN_Char_D_028). No ID_CMN_Char_D_028 key; identified by royal-themed moves ("Royal Blade North Star") + DLC7 = first season-2 character.
    "030": {"name": "Amy",             "kind": "dlc"},      # ID_CMN_Char_D_030 (CORRECTED 2026-05-16 — was wrongly "Tira"; cid 030's French rapier moves "Laurier"/"Rose de Rage" confirm Amy)
    "060": {"name": "2B",              "kind": "dlc"},      # ID_DLC2_CMN_Char_D_060
    "061": {"name": "Haohmaru",        "kind": "dlc"},      # DLC (roster ID_DLC9_CMN_Char_D_061). No ID_CMN_Char_D_061 key; identified by ronin-themed moves ("Vagrant Slash", "Violent Ruffian") + DLC9.
    "062": {"name": "Grøh",            "kind": "base"},     # ID_CMN_Char_D_062 (CORRECTED 2026-05-16 — was wrongly "Cassandra"/dlc; Grøh is a NEW base-roster character, "Steed of the Night" stance)
    "064": {"name": "Azwel",           "kind": "base"},     # ID_CMN_Char_D_064 (CORRECTED 2026-05-16 — was wrongly "Haohmaru"/dlc; Azwel is the base-roster final-story antagonist, "...of Humanity" moves)
    "065": {"name": "Geralt",          "kind": "base"},     # ID_CMN_Char_D_065 — BASE (base-game guest; ELC/EFS_GERALT=0x15, last base playable-block entry)
    "066": {"name": "Unknown (cid 066)", "kind": "unknown", "uncertain": True},
                                                            # cid 066 has moveset data (hdr066.khd,
                                                            # DA_MovePlayData_066) and a RUNTIME_CHAR_066
                                                            # availability flag, but is ABSENT from the
                                                            # engine's playable-roster table
                                                            # (BuildCharaSelectRosterTable @ 0x1405d3870)
                                                            # and has no ID_CMN_Char_D_066 name key.
                                                            # The season-2 DLC fighters occupy cids 022 /
                                                            # 028 / 061 (DLC11/7/9) + 009 Hwang (DLC13) —
                                                            # so 066 is NOT Setsuka/Haohmaru/Hilde. It was
                                                            # previously mis-guessed "Setsuka". Likely a
                                                            # boss / story-mode / cut entity; identity
                                                            # unresolved.
    "0ff": {"name": "Common Motion",   "kind": "shared"},
}


def cell_to_dict(c: LuxBattleAttackCell, idx: int) -> dict[str, Any]:
    """Compact JSON-serializable record for one attack cell.

    Skips the raw bytes (UI doesn't need 0x70 bytes per cell on the wire).
    """
    return {
        "idx": idx,
        "role": c.cell_role,
        "class": c.attack_class,
        "moveType": c.move_type,
        "animKind": c.anim_kind,
        "damage": c.wI16BaseDamage,
        "activeStart": c.wI16MasterWindowStart,
        "activeEnd": c.wI16MasterWindowEnd,
        "activeFrames": c.active_frame_count,
        "hasValidActiveWindow": c.has_valid_active_window,
        "onBlock": c.wI16BlockstunFrames,
        "onHitStanding": c.wI16HitstunStandingNormal,
        "onHitStandingAir": c.wI16HitstunStandingAir,
        "onHitCrouchNormal": c.wI16HitstunCrouchNormal,
        "onHitCrouchAir": c.wI16HitstunCrouchAir,
        "reactionIdStanding": c.wI16ReactionIdStanding,
        "reactionIdAir": c.wI16ReactionIdAir,
        "throwEscapeId": c.wI16ThrowEscapeId,
        "rangeStandMin": c.cI8RangeStandMin,
        "rangeStandMax": c.cI8RangeStandMax,
        "rangeCrouchMin": c.cI8RangeCrouchMin,
        "rangeCrouchMax": c.cI8RangeCrouchMax,
        "reachExtraGate": c.nI16ReachExtraGate,
        "attackFlags": c.wU16AttackFlags,
        "attackFlagsDecoded": attack_flags_to_str(c.wU16AttackFlags),
        "isBreakAttack": c.is_break_attack,
        "isGiImmune": c.is_gi_immune,
        "isGuardBypass": c.is_guard_bypass,
        "extraStateFlags": c.wU16ExtraStateFlags,
        "stunRecoil": c.wI16StunRecoil,
        "inputCond": c.wU16InputCond,
        "hitboxGroup": c.wU16HitboxGroupBitfield,
        "passthroughA": c.wU16PassthroughTagA,
        "passthroughC": c.wU16PassthroughTagC,
        "slotMask": str(c.u64SlotMask),  # avoid JS BigInt issues — string
    }


def throw_to_dict(t, idx: int) -> dict[str, Any]:
    """Compact JSON record for one Section-B throw damage cell."""
    return {
        "idx": idx,
        "damage": t.wDamage,
        "aux": t.nAux,
        "scaling": t.nScaling,
    }


def event_record_to_dict(r, resolved_slot: int | None = None) -> dict[str, Any]:
    """Compact JSON record for one Section-C event-tree record."""
    return {
        "idx": r.record_index,
        "offset": r.byte_offset,
        "packedMoveId": r.dwPackedMoveId,
        "resolvedSlot": resolved_slot,
        "eventKind": r.dwEventKind,
        "eventKindName": r.event_kind_name,
        "field08": r.dwField08,
        "shapeFlags": r.dwShapeFlags,
        "offsetX": round(r.flOffsetX, 6),
        "offsetY": round(r.flOffsetY, 6),
        "offsetZ": round(r.flOffsetZ, 6),
        "field1C": r.dwField1C,
        "field20": r.dwField20,
        "field24": r.dwField24,
        "radiusScale": round(r.flRadiusScale, 6),
        "field2C": r.dwField2C,
        # Compatibility aliases retained for existing reports/UI consumers.
        "key": r.dwKey,
        "typeTag": r.type_tag,
        "typeName": r.type_name,
    }


def slot_to_dict(s, effect_events: list[Any] | None = None) -> dict[str, Any]:
    """Compact slot record. Bytecode is left as opcode counts + length only
    (full disassembly is power-user, fetched separately if needed)."""
    bc = s.bytecode
    if bc is not None:
        callconds = bc.callcond_summary
        facing_effects = [
            serialize_effect(e)
            for e in (effect_events or [])
            if e.is_facing_related
        ]
        bc_summary = {
            "offset": bc.bytecode_offset,
            "instructionCount": len(bc.instructions),
            "lengthBytes": bc.length_bytes,
            "truncated": bc.truncated,
            "callconds": {f"0x{k:02X}": v for k, v in sorted(callconds.items())},
            "facingEffects": facing_effects,
        }
    else:
        bc_summary = None
    return {
        "idx": s.slot_index,
        "animationIndex": s.wAnimationIndex_00,
        "animLength": s.total_frames,
        "totalFrames": s.total_frames,
        "playbackSpeed60ths": round(s.flPlaybackSpeed60ths_30, 3),
        "playbackSpeed": round(s.playback_speed_scalar, 6),
        "hitWindowStart": s.nHitWindowStart_36,
        "cellVariants": list(s.nCellBoneIndexPerVariant),
        "attackCellRefs": s.attack_cell_indices,
        "throwCellRefs": s.throw_cell_indices,
        "bytecodeOffset": s.dwBytecodeOffset_38,
        "bytecode": bc_summary,
    }


def char_summary(cid: str, paths: dict[str, str]) -> dict[str, Any]:
    """Header / roster card for one character."""
    name_info = CHARA_NAMES.get(cid, {"name": f"chara_{cid}", "kind": "unknown"})
    has = {k: (k in paths) for k in ("khd", "mot", "dtp", "atkhit", "bodyhit", "yararehit")}
    out = {
        "cid": cid,
        "name": name_info["name"],
        "kind": name_info.get("kind", "unknown"),
        "uncertain": name_info.get("uncertain", False),
        "files": has,
    }
    # Compute headline stats if KHD exists.
    if "khd" in paths:
        try:
            k = parse_auto(paths["khd"])
            attack_cells = [c for c in k.sections[0].entries if c.cell_role == "Attack"]
            class_dist = Counter(c.attack_class for c in attack_cells)
            out["attackCount"] = len(attack_cells)
            out["slotCount"] = len(k.slots)
            out["topDamage"] = max((c.wI16BaseDamage for c in attack_cells), default=0)
            out["classDistribution"] = dict(class_dist.most_common())
        except Exception as e:
            out["error"] = str(e)
    return out


def export_char(cid: str, paths: dict[str, str], out_path: str) -> None:
    """Write the full per-character JSON."""
    payload: dict[str, Any] = {
        "cid": cid,
        **{k: v for k, v in CHARA_NAMES.get(cid, {"name": f"chara_{cid}"}).items()},
        "files": {k: (k in paths) for k in
                  ("khd", "mot", "dtp", "atkhit", "bodyhit", "yararehit")},
    }
    if "khd" in paths:
        try:
            k = parse_auto(paths["khd"])
            cells = [cell_to_dict(c, i) for i, c in enumerate(k.sections[0].entries)]
            throws = [throw_to_dict(t, i) for i, t in enumerate(k.sections[1].throw_cells)] if len(k.sections) > 1 else []
            event_records = [
                event_record_to_dict(r, k.resolve_packed_slot(r.dwPackedMoveId))
                for r in k.sections[2].event_records
            ] if len(k.sections) > 2 else []
            # Build the slot transition graph + user-facing move trees.
            with open(paths["khd"], "rb") as f:
                khd_bytes = f.read()
            graph = build_slot_graph(k, khd_bytes)
            slots = [slot_to_dict(s, graph.effects_by_src.get(s.slot_index, [])) for s in k.slots]
            all_edges = []
            for src, edges in graph.edges_by_src.items():
                for e in edges:
                    all_edges.append(serialize_edge(e))
            roots = identify_stance_roots(k, graph)
            # Flat moves list: one row per attack-cell-bearing slot, with
            # the shortest user-input path to reach it from any stance root.
            flat_moves = build_flat_moves(k, graph, roots, max_input_steps=5)
            payload["khd"] = {
                "magic": k.magic.decode("ascii", errors="replace"),
                "moveCount": k.move_count,
                "movelistId": k.movelist_id,
                "sectionOffsets": k.section_offsets,
                "attackBlockOffset": k.section_offsets[0],
                "throwBlockOffset": k.section_offsets[1],
                "eventRecordTableOffset": k.event_record_table_offset,
                "eventRecordCount": k.event_record_count,
                "parsedEventRecordCount": len(event_records),
                "eventRecordPrefixBytes": k.sections[2].event_records_end if len(k.sections) > 2 else 0,
                # Compatibility alias for older UI/report consumers.
                "miscBlockOffset": k.section_offsets[2],
                "firstCancelOffset": k.first_cancel_offset,
                "totalCells": len(cells),
                "throwCount": len(throws),
                "attackCount": sum(1 for c in cells if c["role"] == "Attack"),
                "headerCount": sum(1 for c in cells if c["role"] == "Header"),
                "sentinelCount": sum(1 for c in cells if c["role"] == "Sentinel"),
                "nonDamagingCount": sum(1 for c in cells if c["role"] == "NonDamaging"),
                "cells": cells,
                "throws": throws,
                "eventRecords": event_records,
                "slotCount": len(slots),
                "slots": slots,
                "cellToSlots": {
                    str(cell_idx): refs for cell_idx, refs in k.cell_to_slots.items()
                },
                "throwToSlots": {
                    str(throw_idx): refs for throw_idx, refs in k.throw_to_slots.items()
                },
                "slotEdges": all_edges,
                "stanceRoots": [serialize_root(r) for r in roots],
                "flatMoves": [serialize_flat_move(m) for m in flat_moves],
            }
            # Attach the canonical in-game movelist (names + inputs +
            # category ordering) if the UE4 dump is available. The data
            # is independent of the .khd extraction — it joins on the
            # commandSets[].mainIndex (resolved to a cell+slot pair).
            ml = load_movelist_for_chara(cid, k, slot_graph=graph)
            if ml is not None:
                payload["movelist"] = ml
        except Exception as e:
            payload["khdError"] = str(e)
    # Hit data
    for kind in ("atkhit", "bodyhit", "yararehit"):
        if kind in paths:
            try:
                h = parse_auto(paths[kind])
                payload[kind] = {
                    "recordCount": len(h.records),
                    "records": [
                        {
                            "idx": i,
                            "tag": r.tag,
                            "tagName": {0: "Sphere", 1: "Area", 2: "FixArea"}.get(r.tag, "?"),
                            "slot": r.slot,
                            "flags": r.flags,
                            "x": r.pos_x, "y": r.pos_y, "z": r.pos_z,
                            "radius": r.radius,
                            "idLink": r.id_link,
                        }
                        for i, r in enumerate(h.records)
                    ],
                }
            except Exception as e:
                payload[f"{kind}Error"] = str(e)
    # Motion / DTP summaries (counts only — heavy data lives in original files)
    for kind in ("mot", "dtp"):
        if kind in paths:
            try:
                t = parse_auto(paths[kind])
                payload[kind] = {
                    "count": t.count,
                    "fileSize": len(t.raw),
                    "emptySections": sum(1 for sz in t.sizes if sz == 0),
                }
            except Exception:
                pass

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, separators=(",", ":"))


def discover_chars(root: str) -> dict[str, dict[str, str]]:
    """Same logic as viewer.discover_chars."""
    chars: dict[str, dict[str, str]] = {}
    import glob

    def add(cid: str, key: str, path: str):
        chars.setdefault(cid, {})[key] = path

    for sub, prefix, key in [
        ("hdr", "hdr", "khd"),
        ("mot", "chr", "mot"),
        ("cpu", "cpuai", "dtp"),
    ]:
        for p in sorted(glob.glob(os.path.join(root, sub, f"{prefix}*"))):
            base = os.path.basename(p)
            cid = base.removeprefix(prefix).split(".")[0].lower()
            add(cid, key, p)
    for kind in ("atkhit", "bodyhit", "yararehit"):
        for p in sorted(glob.glob(os.path.join(root, "hit", f"{kind}*.dat"))):
            base = os.path.basename(p)
            cid = base.removeprefix(kind).removesuffix(".dat").lower()
            add(cid, kind, p)
    return chars


def main() -> int:
    global COMMUNITY_JSON_PATH, COMMUNITY_XLSX_PATH, _COMMUNITY_FRAME_DATA
    ap = argparse.ArgumentParser(description="Export SC6 moveset data to JSON for the webui")
    ap.add_argument("--root", default=BATTLE_ROOT_DEFAULT, help="Battle data root")
    ap.add_argument("--out-dir", "--out", dest="out_dir", default=OUT_DIR_DEFAULT, help="Output directory")
    ap.add_argument("--community-json", help="Optional parsed community frame-data JSON")
    ap.add_argument("--community-xlsx", help="Optional downloaded community frame-data spreadsheet")
    args = ap.parse_args()

    COMMUNITY_JSON_PATH = args.community_json
    COMMUNITY_XLSX_PATH = args.community_xlsx
    _COMMUNITY_FRAME_DATA = None

    chars = discover_chars(args.root)
    print(f"Discovered {len(chars)} characters under {args.root}")
    os.makedirs(args.out_dir, exist_ok=True)
    os.makedirs(os.path.join(args.out_dir, "chars"), exist_ok=True)

    roster = []
    for cid in sorted(chars):
        summary = char_summary(cid, chars[cid])
        roster.append(summary)
        export_char(cid, chars[cid], os.path.join(args.out_dir, "chars", f"{cid}.json"))
        ac = summary.get("attackCount", "-")
        td = summary.get("topDamage", "-")
        print(f"  {cid}  {summary['name']:<18}  attacks={ac:>4}  topDmg={td}")

    with open(os.path.join(args.out_dir, "roster.json"), "w", encoding="utf-8") as f:
        json.dump({"chars": roster}, f, indent=2)
    print(f"\nWrote roster + {len(roster)} char files to {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
