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
from pathlib import Path
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from luxformats import (
    KhdFile, LuxBattleAttackCell, attack_flags_to_str, decode_button_mask, parse_auto,
)
from move_graph import (
    build_slot_graph, identify_stance_roots,
    build_flat_moves,
    serialize_edge, serialize_effect, serialize_root, serialize_flat_move,
    USER_INPUT_KINDS,
)
import uassetparse
import locales
from native_route_evidence import resolve_native_route
from native_frame_analysis import analyze_confirmed_slot_frames, analyze_throw_break_frames
from native_startup_analysis import analyze_player_startup, serialize_startup_evidence
from native_reaction_table import (
    LuxHitReactionMoveIdTable,
    parse_hit_reaction_move_id_table,
)
from native_input_routes import (
    NativeDispatcherResolver,
    resolve_unconditional_attack_route,
)
from native_move_commands import (
    MovePlayCommandTable,
    parse_move_play_command_table,
    parse_transition_command_table,
)
from lux_input_codec import LuxInputCodecTables

SCHEMA_VERSION = 2

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
BATTLE_ROOT_DEFAULT = os.path.join(UE4_DUMP_ROOT, "Battle")
if not os.path.isdir(BATTLE_ROOT_DEFAULT):
    # Binary-only fallback for machines without the complete UE content dump.
    # Movelist names are intentionally unavailable in this configuration.
    BATTLE_ROOT_DEFAULT = r"E:\myMods\dump\Battle"
STYLE_ROOT = os.path.join(UE4_DUMP_ROOT, "Style")
ARCHIVE_PATH = os.path.join(
    UE4_DUMP_ROOT, "Localization", "Game", "Steam", "en", "Game.archive"
)
HAVE_UE4_DATA = os.path.isdir(STYLE_ROOT) and os.path.isfile(ARCHIVE_PATH)
SC6_EXE_PATH = Path(os.environ.get(
    "SC6_EXE_PATH",
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe",
))
_CODEC_TABLES: LuxInputCodecTables | None = None


def _load_codec_tables() -> LuxInputCodecTables | None:
    global _CODEC_TABLES
    if _CODEC_TABLES is None and SC6_EXE_PATH.is_file():
        _CODEC_TABLES = LuxInputCodecTables.from_executable(SC6_EXE_PATH)
    return _CODEC_TABLES


def _require_coherent_content_roots(battle_root: str) -> None:
    """Keep movelist, CPUAI definition, command, and KHD assets coherent.

    ``MainIndex`` is resolved through the matching ``cpuaiNNN.dtp`` definition
    table.  Mixing it with a different KHD/content revision can create a valid
    definition script beside stale combat data, so production export refuses
    split roots.
    """
    if not HAVE_UE4_DATA:
        return
    expected_battle_root = os.path.join(UE4_DUMP_ROOT, "Battle")
    actual = os.path.normcase(os.path.realpath(battle_root))
    expected = os.path.normcase(os.path.realpath(expected_battle_root))
    if actual != expected:
        raise SystemExit(
            "Refusing to resolve MoveVM definitions with UE "
            "movelist assets from a different content root. Use "
            f"--root {expected_battle_root!r}, or set SC6_DUMP_ROOT to the "
            "matching extracted SoulcaliburVI/Content directory."
        )


def load_movelist_for_chara(
    cid: str,
    khd: KhdFile | None,
    slot_graph: Any = None,
    move_play_commands: MovePlayCommandTable | None = None,
    native_dispatcher: NativeDispatcherResolver | None = None,
    reaction_table: LuxHitReactionMoveIdTable | None = None,
) -> dict[str, Any] | None:
    """Parse a character's UE4 DataAsset + localization. Returns a dict
    with `categories` (the in-game movelist ordering) and `moves` (an
    item-level dict keyed by MoveListID with name / command / note /
    ordered fighter/opponent CommandSets and their MoveVM definition IDs).
    Returns None if UE4 dump isn't available or the character's
    MovePlayData file is missing.

    `khd` and `slot_graph` are accepted for the surrounding native diagnostic
    payload, but are never used to reinterpret definition IDs as cells.
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

    return _build_movelist_payload(
        cid,
        data,
        movelist_idx,
        khd,
        slot_graph,
        move_meta,
        move_play_commands,
        native_dispatcher,
        reaction_table,
    )


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


def _legacy_main_index_navigation_candidate(
    main_index: int,
    khd: KhdFile | None,
) -> dict[str, int]:
    """Produce a legacy navigation candidate from raw ``MainIndex``.

    Native code proves ``MainIndex`` is a MoveVM definition ID, so this helper
    must never feed production metrics or links. It is retained only for
    offline navigation experiments: it tests the same raw number as a cell or
    slot candidate and explicitly labels the result heuristic.
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


def _native_input_timing_signature(
    move: dict[str, Any],
) -> tuple[tuple[int, ...], tuple[int, ...]] | None:
    """Return one exact CPUAI mask/duration sequence for a canonical move.

    Category-specific preview variants may legitimately use different native
    definitions. Those fail closed here; a timing-family edge is emitted only
    when every authored listing agrees on the ordered masks and durations.
    """
    signatures: set[tuple[tuple[int, ...], tuple[int, ...]]] = set()
    variants = move.get("authoredVariants", []) or [{"nativeLink": move.get("nativeLink", {})}]
    for variant in variants:
        link = variant.get("nativeLink", {}) or {}
        definitions = link.get("definitions", []) or []
        if not definitions:
            return None
        definition = definitions[0].get("mainDefinition") or definitions[0].get("fallbackDefinition")
        if not isinstance(definition, dict) or definition.get("controlFlow") != "native-linear":
            return None
        steps = definition.get("buttonSteps", []) or []
        if not steps:
            return None
        signatures.add((
            tuple(int(step.get("mask", 0)) for step in steps),
            tuple(int(step.get("durationFrames", 0)) for step in steps),
        ))
    return next(iter(signatures)) if len(signatures) == 1 else None


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

    # Alternative commands with the same official name, context, and exact
    # statically resolved effective attack route are one player-facing move family.  The
    # name/context guard is load-bearing: a shared dispatcher entry by itself
    # is not enough (throws and strings often share an initial KHD route).
    by_native_route: dict[tuple[Any, ...], list[dict[str, Any]]] = {}
    for move in moves:
        link = move.get("nativeLink", {}) or {}
        attack_slots, attack_cells = _native_attack_refs(link)
        slots = tuple(attack_slots)
        cells = tuple(attack_cells)
        name = str(move.get("name", ""))
        if (
            link.get("status") not in {"confirmed", "heuristic"}
            or not slots
            or not cells
            or not name
        ):
            continue
        key = (name, str(move.get("condition", "")), slots, cells)
        by_native_route.setdefault(key, []).append(move)
    for members in by_native_route.values():
        attach_group(
            "native-route-alternative",
            members,
            "same official name, context, and statically resolved effective attack route",
        )

    # Same native input masks with timing-only differences are one move
    # family. This captures authored variants such as Titan Bomb and its fast
    # input without relying on the localized "(fast)" suffix.
    timing_parent = list(range(len(moves)))

    def timing_find(idx: int) -> int:
        while timing_parent[idx] != idx:
            timing_parent[idx] = timing_parent[timing_parent[idx]]
            idx = timing_parent[idx]
        return idx

    def timing_union(a: int, b: int) -> None:
        ra, rb = timing_find(a), timing_find(b)
        if ra != rb:
            timing_parent[rb] = ra

    timing_signatures = [_native_input_timing_signature(move) for move in moves]
    for i, move_a in enumerate(moves):
        signature_a = timing_signatures[i]
        if signature_a is None:
            continue
        for j in range(i + 1, len(moves)):
            move_b = moves[j]
            signature_b = timing_signatures[j]
            if (
                signature_b is not None
                and move_a.get("moveId") != move_b.get("moveId")
                and move_a.get("name", "") == move_b.get("name", "")
                and move_a.get("condition", "") == move_b.get("condition", "")
                and signature_a[0] == signature_b[0]
                and signature_a[1] != signature_b[1]
            ):
                timing_union(i, j)
    timing_components: dict[int, list[dict[str, Any]]] = {}
    for i, move in enumerate(moves):
        timing_components.setdefault(timing_find(i), []).append(move)
    for members in timing_components.values():
        attach_group(
            "native-timing-variant",
            members,
            "same native CPUAI input masks with different authored durations",
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


def _canonicalize_category_listings(
    listings: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Collapse menu-category repetitions to one move per MoveListID.

    Category-specific MovePlay parameters are retained as authored variants,
    but categories never create additional player-facing moves.
    """
    by_move_id: dict[int, list[dict[str, Any]]] = {}
    for listing in listings:
        by_move_id.setdefault(int(listing.get("moveId", 0)), []).append(listing)

    identity_fields = (
        "name", "condition", "input", "fullCommand", "inputMarkup", "note",
        "isRevengeAttack", "isMovementOnly", "hasInputAlternatives",
        "attributeTag", "hitClasses", "effectTags", "mainTip",
        "lethalHitCondition",
    )
    canonical: list[dict[str, Any]] = []
    for canonical_order, (move_id, members) in enumerate(by_move_id.items()):
        first = members[0]
        for field in identity_fields:
            expected = first.get(field)
            if any(member.get(field) != expected for member in members[1:]):
                raise ValueError(
                    f"MoveListID {move_id} changes player-facing field {field!r} "
                    "between category listings"
                )

        variant_groups: dict[str, list[dict[str, Any]]] = {}
        for member in members:
            key = json.dumps(member.get("commandSets", []), sort_keys=True, separators=(",", ":"))
            variant_groups.setdefault(key, []).append(member)
        authored_variants = []
        for variant_index, variant_members in enumerate(variant_groups.values()):
            representative = variant_members[0]
            authored_variants.append({
                "variantIndex": variant_index,
                "listingOrders": [int(member["order"]) for member in variant_members],
                "categoryMemberships": sorted({int(member["category"]) for member in variant_members}),
                "commandSets": representative.get("commandSets", []),
                "nativeLink": representative.get("nativeLink", {}),
            })

        move = dict(first)
        move["order"] = canonical_order
        move["category"] = -1
        move["listingOrders"] = [int(member["order"]) for member in members]
        move["categoryMemberships"] = sorted({int(member["category"]) for member in members})
        move["authoredVariants"] = authored_variants
        if len(authored_variants) > 1:
            links = [variant["nativeLink"] for variant in authored_variants]
            definitions: list[dict[str, Any]] = []
            seen_definitions: set[str] = set()
            for variant in authored_variants:
                for definition in variant["nativeLink"].get("definitions", []) or []:
                    key = json.dumps(definition, sort_keys=True, separators=(",", ":"))
                    if key not in seen_definitions:
                        seen_definitions.add(key)
                        definitions.append(definition)
            move["nativeLink"] = {
                "status": "ambiguous",
                "resolutions": sorted({
                    "multiple-authored-moveplay-variants",
                    *(str(resolution) for link in links for resolution in link.get("resolutions", [])),
                }),
                "definitions": definitions,
                "slots": [],
                "cells": [],
                "authoredVariants": [
                    {
                        "variantIndex": variant["variantIndex"],
                        "listingOrders": variant["listingOrders"],
                        "categoryMemberships": variant["categoryMemberships"],
                    }
                    for variant in authored_variants
                ],
            }
        canonical.append(move)
    return canonical


def _ordered_nonnegative_ints(values: Any) -> list[int]:
    """Normalize native references without destroying authored path order."""

    result: list[int] = []
    for value in values or []:
        parsed = int(value)
        # Repeated slot/cell references can represent repeated contacts.  A
        # path is a sequence, not a set, so retaining only unique references
        # silently under-counts legitimate multi-hit routes.
        if parsed >= 0:
            result.append(parsed)
    return result


def _native_attack_refs(native_link: dict[str, Any]) -> tuple[list[int], list[int]]:
    """Return effective contact refs, falling back only for old link schemas.

    Presence is deliberately tested separately from truthiness.  In the
    current schema an explicit empty ``attackCells``/``attackSlots`` array
    means static routing proved navigation but no effective attack.  Falling
    back to ``cells``/``slots`` in that case would turn navigation evidence
    into fabricated damage, startup, frame data, or family identity.
    """

    slots_key = "attackSlots" if "attackSlots" in native_link else "slots"
    cells_key = "attackCells" if "attackCells" in native_link else "cells"
    return (
        _ordered_nonnegative_ints(native_link.get(slots_key, [])),
        _ordered_nonnegative_ints(native_link.get(cells_key, [])),
    )


def _move_attack_refs(move: dict[str, Any]) -> tuple[list[int], list[int]]:
    """Return effective refs from a link, with command-set support for v1 data."""

    native_link = move.get("nativeLink", {}) or {}
    if native_link:
        slots, cells = _native_attack_refs(native_link)
        if (
            "attackSlots" in native_link
            or "attackCells" in native_link
            or slots
            or cells
        ):
            return slots, cells

    slots: list[int] = []
    cells: list[int] = []
    for command_set in move.get("commandSets", []) or []:
        raw_slot = command_set.get("slotIdx", -1)
        raw_cell = command_set.get("cellIdx", -1)
        slot = int(raw_slot) if raw_slot is not None else -1
        cell = int(raw_cell) if raw_cell is not None else -1
        if slot >= 0 and slot not in slots:
            slots.append(slot)
        if cell >= 0 and cell not in cells:
            cells.append(cell)
    return slots, cells


def _move_play_definition_evidence(
    definition_id: int,
    move_play_commands: MovePlayCommandTable | None,
) -> dict[str, Any] | None:
    if move_play_commands is None or definition_id < 0:
        return None
    definition = move_play_commands.definition(definition_id)
    if definition is None:
        return {
            "status": "unresolved",
            "definitionId": definition_id,
            "resolution": "cpuai-definition-out-of-range",
        }
    return {
        "status": (
            "native-confirmed" if definition.status == "native-linear" else
            "ambiguous" if definition.status == "native-branched" else
            "unresolved"
        ),
        "definitionId": definition_id,
        "initialCell": definition.initial_cell,
        "scriptEndCell": definition.script_end_cell,
        "controlFlow": definition.status,
        "branchCells": list(definition.branch_cells),
        "buttonSteps": [
            {
                "cell": step.cell_index,
                "mask": step.mask,
                "notation": decode_button_mask(step.mask),
                "durationFrames": step.duration_frames,
            }
            for step in definition.button_steps
        ],
        "resolution": (
            f"cpuai-main-definition:{definition_id}"
            f"@cell{definition.initial_cell}:{definition.status}"
        ),
    }


def _native_link(
    move: dict[str, Any],
    move_play_commands: MovePlayCommandTable | None = None,
    native_dispatcher: NativeDispatcherResolver | None = None,
) -> dict[str, Any]:
    """Describe only the native command-player relationship proven in code.

    The two command sets are ordered VM lanes. ``MainIndex`` selects the
    lane's primary MoveVM definition and ``IntroIndex`` its fallback
    definition. Neither integer is an attack-cell index. KHD slots/cells stay
    empty until a statically reachable definition-to-cell path is proven.
    """
    command_sets = move.get("commandSets", []) or []
    resolutions = sorted({
        str(cs.get("resolution", "inactive-lane")) for cs in command_sets
    })
    definitions = []
    for cs in command_sets:
        main_definition_id = int(cs.get("mainIndex", 0) or 0)
        fallback_definition_id = int(cs.get("introIndex", 0) or 0)
        # An all-zero command set is an inactive lane, not definition zero.
        # Definition zero may exist in the CPUAI table, but native callers do
        # not select it for an inactive lane.
        main_evidence = (
            _move_play_definition_evidence(main_definition_id, move_play_commands)
            if main_definition_id > 0 else None
        )
        fallback_evidence = (
            _move_play_definition_evidence(fallback_definition_id, move_play_commands)
            if fallback_definition_id > 0 else None
        )
        definition_record = {
            "lane": str(cs.get("lane", "unknown")),
            "mainDefinitionId": main_definition_id,
            "fallbackDefinitionId": fallback_definition_id,
        }
        if main_evidence is not None:
            definition_record["mainDefinition"] = main_evidence
        if fallback_evidence is not None:
            definition_record["fallbackDefinition"] = fallback_evidence
        definitions.append(definition_record)
        for evidence in (main_evidence, fallback_evidence):
            if evidence is not None and evidence.get("resolution"):
                resolutions.append(str(evidence["resolution"]))
    # A definition ID is confirmed navigation evidence, not a proven path to
    # a combat KHD slot/cell.  Only an audited static route may upgrade this
    # link to `confirmed` below.
    evidence_statuses = {
        str(evidence.get("status", "unresolved"))
        for definition in definitions
        for key in ("mainDefinition", "fallbackDefinition")
        if isinstance((evidence := definition.get(key)), dict)
    }
    if "unresolved" in evidence_statuses:
        status = "unresolved"
    elif "ambiguous" in evidence_statuses:
        status = "ambiguous"
    elif evidence_statuses:
        # The CPUAI definition and its straight-line button script are exact,
        # but the later definition-to-KHD combat route is not yet proven.
        status = "heuristic"
    elif any(
        definition["mainDefinitionId"] > 0
        or definition["fallbackDefinitionId"] > 0
        for definition in definitions
    ):
        # The caller may deliberately omit the CPUAI table in a diagnostic or
        # unit-level build. The native definition ID is still navigation
        # evidence, but it cannot be validated further in that context.
        status = "heuristic"
    else:
        status = "unresolved"
    link = {
        "status": status,
        "resolutions": sorted(set(resolutions)),
        "definitions": definitions,
        "slots": [],
        "cells": [],
    }
    if native_dispatcher is None or move_play_commands is None:
        return link

    candidates: set[int] = set()
    candidate_definitions: list[tuple[Any, Any]] = []
    route_resolutions: set[str] = set()
    route_truncated = False
    for definition_record in definitions:
        if definition_record.get("lane") != "primary-fighter":
            continue
        definition_id = int(definition_record.get("mainDefinitionId", 0) or 0)
        definition = move_play_commands.definition(definition_id)
        if definition is None or definition_id <= 0:
            continue
        resolved = native_dispatcher.resolve_definition(definition)
        candidates.update(resolved.slots)
        if resolved.slots:
            candidate_definitions.append((definition, resolved))
        route_resolutions.update(resolved.resolutions)
        route_truncated = route_truncated or resolved.truncated
    if not candidates:
        link["resolutions"] = sorted({*link["resolutions"], *route_resolutions})
        return link
    if len(candidates) != 1:
        link.update({
            "status": "ambiguous",
            "slots": sorted(candidates),
            "resolutions": sorted({
                *link["resolutions"],
                *route_resolutions,
                "native-dispatch-candidates:" + ",".join(
                    f"slot{slot}" for slot in sorted(candidates)
                ),
            }),
        })
        return link

    start_slot = next(iter(candidates))
    if len(candidate_definitions) == 1:
        definition, initial_route = candidate_definitions[0]
        route = native_dispatcher.resolve_attack_route(
            definition,
            start_slot,
            selected_move_play_frame=initial_route.selected_move_play_frame,
        )
    else:
        route = resolve_unconditional_attack_route(native_dispatcher.bank, start_slot)
    link.update({
        "status": "ambiguous" if route_truncated or route.ambiguous else "heuristic",
        "slots": list(route.slots),
        "cells": list(route.cells),
        "attackSlots": list(route.attack_slots),
        "attackCells": list(route.attack_cells),
        "startupTimingStatus": (
            "resolved" if route.startup_timing_resolved else "unresolved"
        ),
        "frameEndpointStatus": (
            "resolved" if route.frame_endpoints_resolved else "unresolved"
        ),
        "resolutions": sorted({
            *link["resolutions"],
            *route_resolutions,
            *route.resolutions,
            "native-route-link:statically-inferred",
        }),
    })
    return link


def _metric_evidence(source: str, status: str) -> dict[str, str]:
    return {"source": source, "status": status}


def _has_game_authored_throw_tag(move: dict[str, Any]) -> bool:
    return any(
        isinstance(tag, dict) and str(tag.get("code", "")).upper() == "TH"
        for tag in move.get("effectTags", []) or []
    )


def _is_native_throw_attempt(move: dict[str, Any], khd: KhdFile | None) -> bool:
    """Require the authored TH tag and a linked non-damaging attempt cell.

    TH alone is insufficient: the game also tags ordinary attacks that can
    transition into a throw follow-up.  The native attempt cell is the
    distinguishing evidence available in the static assets.
    """

    if not _has_game_authored_throw_tag(move) or khd is None or not khd.sections:
        return False
    native_link = move.get("nativeLink", {}) or {}
    if native_link.get("status") not in {"confirmed", "heuristic"}:
        return False
    _, cells = _native_attack_refs(native_link)
    if not cells:
        return False
    cell_idx = int(cells[-1])
    attack_cells = khd.sections[0].entries
    return 0 <= cell_idx < len(attack_cells) and attack_cells[cell_idx].cell_role == "NonDamaging"


def _throw_break_proof_payload(frame: Any, status: str) -> dict[str, Any]:
    return {
        "status": status,
        "attackSlot": frame.attack_slot,
        "attackCell": frame.attack_cell,
        "totalFrames": frame.total_frames,
        "cellWindowStartCoordinate": frame.cell_window_start_coordinate,
        "cellWindowEndCoordinate": frame.cell_window_end_coordinate,
        "recoveryLead": frame.recovery_lead,
        "recoveryOpenCoordinate": frame.recovery_open_coordinate,
        "inclusiveRecoveryFrames": frame.inclusive_recovery_frames,
        "defenderBreakStunFrames": frame.break_stun_frames,
        "advantage": frame.break_advantage,
    }


def _attach_throw_break_proof(move: dict[str, Any], khd: KhdFile | None) -> None:
    """Attach break recovery only to native non-damaging TH attempt cells."""

    if not _has_game_authored_throw_tag(move) or khd is None:
        return
    native_link = move.get("nativeLink", {}) or {}
    status = native_link.get("status")
    slots, cells = _native_attack_refs(native_link)
    if status not in {"confirmed", "heuristic"} or not slots or not cells:
        return
    frame = analyze_throw_break_frames(
        khd,
        attack_slot=int(slots[-1]),
        attack_cell=int(cells[-1]),
    )
    if frame is None:
        return
    proof_status = "confirmed" if status == "confirmed" else "heuristic"
    native_link["throwBreakProof"] = _throw_break_proof_payload(frame, proof_status)
    native_link["resolutions"] = sorted({
        *native_link.get("resolutions", []),
        *frame.resolutions,
        (
            "native-throw-break-endpoints:confirmed"
            if proof_status == "confirmed"
            else "native-throw-break-endpoints:confirmed;route-link=inferred"
        ),
    })


def _move_metrics(move: dict[str, Any], khd: KhdFile | None) -> tuple[dict[str, Any], dict[str, Any]]:
    """Return player-facing metrics and per-metric static provenance.

    KHD block/hit fields are defender stun counters, not frame advantage.
    Until both defender stun and attacker recovery endpoints are statically
    proven under the same native counter convention, advantage stays null.
    """
    hit_levels = list(move.get("hitClasses", []) or [])
    hit_level_evidence = _metric_evidence(
        "game-movelist-table" if hit_levels else "unknown",
        "game-authored" if hit_levels else "unknown",
    )
    unknown = _metric_evidence("unknown", "unknown")
    metrics = {
        "startup": None,
        "damage": [],
        "block": None,
        "hit": None,
        "counterHit": None,
        "guardBurst": None,
        "hitLevels": hit_levels,
    }
    evidence = {
        "startup": dict(unknown),
        "damage": dict(unknown),
        "block": dict(unknown),
        "hit": dict(unknown),
        "counterHit": dict(unknown),
        "guardBurst": dict(unknown),
        "hitLevels": hit_level_evidence,
    }
    if move.get("isMovementOnly"):
        return metrics, evidence

    # Throw success follows authored cinematic/damage paths that are not
    # reconstructed here.  The break path is narrower: a TH-tagged official
    # row plus a non-damaging grab-attempt cell and a proven recovery timeline.
    # Publish that one value in the familiar Block column as requested.
    if move.get("isThrowInput"):
        native_link = move.get("nativeLink", {}) or {}
        proof = native_link.get("throwBreakProof")
        if isinstance(proof, dict) and proof.get("status") in {"confirmed", "heuristic"}:
            value = proof.get("advantage")
            if isinstance(value, int) and not isinstance(value, bool):
                metrics["block"] = value
                evidence["block"] = _metric_evidence(
                    "khd-static-timeline",
                    "native-confirmed" if proof.get("status") == "confirmed" else "native-inferred",
                )
        return metrics, evidence

    # Explicit cell order is exported only by an audited static route.  Do
    # not infer this from MainIndex or from arbitrary slot/cell coincidences.
    native_link = move.get("nativeLink", {}) or {}
    route_slots, route_cells = _native_attack_refs(native_link)
    link_status = native_link.get("status")
    if link_status in {"confirmed", "heuristic"} and route_cells and khd is not None:
        if not isinstance(native_link.get("startupProof"), dict):
            _attach_startup_proof(native_link, khd)
        metric_status = "native-confirmed" if link_status == "confirmed" else "native-inferred"
        attack_cells = khd.sections[0].entries if khd.sections else []
        metric_cell_indices = list(route_cells)
        startup_proof = native_link.get("startupProof")
        if (
            isinstance(startup_proof, dict)
            and route_slots
            and startup_proof.get("attackSlot") == route_slots[0]
            and isinstance(startup_proof.get("effectiveCell"), int)
        ):
            metric_cell_indices[0] = int(startup_proof["effectiveCell"])
        resolved = []
        for cell_idx in metric_cell_indices:
            if not (0 <= cell_idx < len(attack_cells)):
                return metrics, evidence
            cell = attack_cells[cell_idx]
            if cell.cell_role != "Attack":
                return metrics, evidence
            resolved.append(cell)
        if (
            isinstance(startup_proof, dict)
            and isinstance(startup_proof.get("playerImpactFrame"), int)
        ):
            metrics["startup"] = int(startup_proof["playerImpactFrame"])
            evidence["startup"] = _metric_evidence("khd-static-timeline", metric_status)
        # A route whose contact count contradicts the game's authored
        # per-hit list is still usable for independently proven startup, but
        # not as a complete damage sequence or final-contact recovery proof.
        if native_link.get("hitSequenceStatus") == "unresolved":
            return metrics, evidence
        metrics["damage"] = [cell.wI16BaseDamage for cell in resolved]
        evidence["damage"] = _metric_evidence("khd-attack-cell", metric_status)
        if not metrics["hitLevels"]:
            metrics["hitLevels"] = [cell.attack_class for cell in resolved]
            evidence["hitLevels"] = _metric_evidence("khd-attack-cell", metric_status)

        # Advantage is intentionally opt-in.  A confirmed cell route alone is
        # insufficient: the native proof must include both defender stun and
        # the attacker's actionable recovery endpoint under the same counter
        # convention.
        frame_proof = native_link.get("frameProof")
        if isinstance(frame_proof, dict) and frame_proof.get("status") in {"confirmed", "heuristic"}:
            advantages = frame_proof.get("advantages", {}) or {}
            for metric in ("block", "hit", "counterHit"):
                value = advantages.get(metric)
                if isinstance(value, int) and not isinstance(value, bool):
                    metrics[metric] = value
                    evidence[metric] = _metric_evidence(
                        "khd-static-timeline", metric_status
                    )

    return metrics, evidence


def _parse_frame_value(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return int(value)
    if value is None:
        return None
    match = re.search(r"[+-]?\d+", str(value))
    return int(match.group(0)) if match else None


def _damage_total(values: Any) -> int | None:
    if not isinstance(values, list) or not values:
        return None
    total = 0
    seen = False
    for value in values:
        if isinstance(value, bool):
            continue
        if isinstance(value, (int, float)):
            total += int(value)
            seen = True
    return total if seen else None


def _row_has_confirmed_reaction(row: dict[str, Any]) -> bool:
    metrics = row.get("metrics", {}) or {}
    evidence = row.get("evidence", {}) or {}
    if not any(
        (evidence.get(key, {}) or {}).get("status") == "native-confirmed"
        for key in ("hit", "counterHit")
    ):
        return False
    blob = f"{metrics.get('hit', '')} {metrics.get('counterHit', '')}".upper()
    return re.search(r"\b(LNC|KND|STN|LAUNCH|KNOCK)", blob) is not None


def _family_metric_summary(family: dict[str, Any]) -> dict[str, Any]:
    rows = family.get("rows", []) or []
    startups = [
        value
        for value in (row.get("metrics", {}).get("startup") for row in rows)
        if isinstance(value, (int, float)) and not isinstance(value, bool)
    ]
    damages = [
        total
        for total in (_damage_total(row.get("metrics", {}).get("damage")) for row in rows)
        if total is not None
    ]
    blocks = [
        (row.get("metrics", {}).get("block"), parsed)
        for row in rows
        for parsed in [_parse_frame_value(row.get("metrics", {}).get("block"))]
        if parsed is not None
        and not row.get("isThrowInput", False)
        and (row.get("evidence", {}).get("block", {}) or {}).get("status") == "native-confirmed"
    ]
    hits = [
        (row.get("metrics", {}).get("hit"), parsed)
        for row in rows
        for parsed in [_parse_frame_value(row.get("metrics", {}).get("hit"))]
        if parsed is not None
        and (row.get("evidence", {}).get("hit", {}) or {}).get("status") == "native-confirmed"
    ]
    unsafe_count = sum(1 for _, parsed in blocks if parsed <= -10)
    plus_count = sum(1 for _, parsed in blocks if parsed > 0)
    blocks.sort(key=lambda item: item[1])
    hits.sort(key=lambda item: item[1], reverse=True)
    return {
        "startup": min(startups) if startups else None,
        "damage": max(damages) if damages else None,
        "block": blocks[0][0] if blocks else None,
        "hit": hits[0][0] if hits else None,
        "rowCount": len(rows),
        "unsafeCount": unsafe_count,
        "plusCount": plus_count,
        "launcherCount": sum(1 for row in rows if _row_has_confirmed_reaction(row)),
    }


def _normalize_command_key(value: str) -> str:
    return (
        (value or "")
        .upper()
        .replace(" ", "")
        .replace(",", "")
        .replace(">", "")
        .replace(".", "")
        .replace("(", "")
        .replace(")", "")
        .replace("-", "")
    )


def _family_search_text(char_name: str, family: dict[str, Any]) -> str:
    parts: list[str] = [
        char_name,
        family.get("cid", ""),
        family.get("rootCommand", ""),
        family.get("rootName", ""),
        family.get("context", ""),
        family.get("confidence", ""),
        " ".join(family.get("relations", []) or []),
    ]
    for row in family.get("rows", []) or []:
        metrics = row.get("metrics", {}) or {}
        parts.extend([
            row.get("displayCommand", ""),
            row.get("displayName", ""),
            row.get("context", ""),
            row.get("source", ""),
            row.get("confidence", ""),
            (row.get("nativeLink", {}) or {}).get("status", ""),
            " ".join(metrics.get("hitLevels", []) or []),
            row.get("notes", ""),
        ])
    return " ".join(str(part) for part in parts if part).lower()


def _family_command_keys(family: dict[str, Any]) -> list[str]:
    values = [family.get("rootCommand", "")]
    values.extend(row.get("displayCommand", "") for row in family.get("rows", []) or [])
    return sorted({key for key in (_normalize_command_key(value) for value in values) if key})


def _link_status_counts_for_family(family: dict[str, Any]) -> dict[str, int]:
    counts = Counter(
        (row.get("nativeLink", {}) or {}).get("status", "unresolved")
        for row in family.get("rows", []) or []
    )
    return dict(counts)


def _build_player_dashboard(families: list[dict[str, Any]]) -> dict[str, Any]:
    stats_by_family = {family["id"]: _family_metric_summary(family) for family in families}

    def family_stats(family: dict[str, Any]) -> dict[str, Any]:
        return stats_by_family.get(family["id"], {})

    fastest = sorted(
        (family for family in families if family_stats(family).get("startup") is not None),
        key=lambda family: (family_stats(family).get("startup") or 9999, family.get("rootCommand", "")),
    )[:8]
    unsafe = sorted(
        (family for family in families if family_stats(family).get("unsafeCount", 0) > 0),
        key=lambda family: (_parse_frame_value(family_stats(family).get("block")) or 9999, family.get("rootCommand", "")),
    )[:8]
    plus = sorted(
        (family for family in families if family_stats(family).get("plusCount", 0) > 0),
        key=lambda family: (_parse_frame_value(family_stats(family).get("block")) or -9999, family.get("rootCommand", "")),
        reverse=True,
    )[:8]
    launchers = [
        family for family in families
        if family_stats(family).get("launcherCount", 0) > 0
    ][:8]
    return {
        "statsByFamily": stats_by_family,
        "fastestFamilyIds": [family["id"] for family in fastest],
        "unsafeFamilyIds": [family["id"] for family in unsafe],
        "plusFamilyIds": [family["id"] for family in plus],
        "launcherFamilyIds": [family["id"] for family in launchers],
    }


def build_v2_player_payload(payload: dict[str, Any]) -> dict[str, Any]:
    """Slim character payload for normal v2 browsing.

    It intentionally excludes heavyweight native KHD arrays (`slots`,
    `cells`, `slotEdges`, and event records). Those remain in the legacy
    full JSON for explicit debug/evidence workflows.
    """
    movelist = payload.get("movelist") or {}
    families = movelist.get("playerMoveFamilies", []) or []
    khd = payload.get("khd") or {}
    native_summary = {
        key: khd.get(key)
        for key in (
            "moveCount",
            "movelistId",
            "totalCells",
            "throwCount",
            "attackCount",
            "headerCount",
            "sentinelCount",
            "nonDamagingCount",
            "slotCount",
            "eventRecordCount",
            "parsedEventRecordCount",
        )
        if key in khd
    }
    return {
        "schemaVersion": SCHEMA_VERSION,
        "cid": payload.get("cid", ""),
        "name": payload.get("name", ""),
        "kind": payload.get("kind", "unknown"),
        "uncertain": payload.get("uncertain", False),
        "files": payload.get("files", {}),
        "nativeSummary": native_summary,
        "playerMoveFamilies": families,
        "playerMoveSummary": movelist.get("playerMoveSummary") or {
            "officialRows": len(movelist.get("moves", []) or []),
            "playerFamilies": len(families),
            "playerRows": sum(len(family.get("rows", []) or []) for family in families),
            "nativeLinkedRows": 0,
            "nativeUnlinkedRows": len(movelist.get("moves", []) or []),
            "linkStatusCounts": {},
            "groupingConfidenceCounts": {},
            "metricCoverage": {},
        },
        "dashboard": _build_player_dashboard(families),
    }


def _move_metrics_from_payload(
    move: dict[str, Any], payload: dict[str, Any]
) -> tuple[dict[str, Any], dict[str, Any]]:
    if move.get("metrics") is not None and move.get("evidence") is not None:
        return move["metrics"], move["evidence"]
    metrics, evidence = _move_metrics(move, None)
    if move.get("isMovementOnly") or move.get("isThrowInput"):
        return metrics, evidence
    cells = payload.get("khd", {}).get("cells", []) or []
    native_link = move.get("nativeLink", {}) or {}
    if native_link.get("status") not in {"confirmed", "heuristic"}:
        return metrics, evidence
    startup_proof = native_link.get("startupProof")
    _, native_cells = _move_attack_refs(move)
    if (
        native_link.get("startupTimingStatus") != "unresolved"
        and isinstance(startup_proof, dict)
        and native_cells
    ):
        effective_cell = startup_proof.get("effectiveCell")
        if isinstance(effective_cell, int):
            native_cells[0] = effective_cell
    if not native_cells:
        return metrics, evidence
    resolved_cells: list[dict[str, Any]] = []
    for idx in native_cells:
        if not (0 <= idx < len(cells)) or cells[idx].get("role") != "Attack":
            return metrics, evidence
        resolved_cells.append(cells[idx])

    player_impact = (
        startup_proof.get("playerImpactFrame")
        if (
            native_link.get("startupTimingStatus") != "unresolved"
            and isinstance(startup_proof, dict)
        )
        else None
    )
    if isinstance(player_impact, int) and not isinstance(player_impact, bool):
        metrics["startup"] = player_impact
        evidence["startup"] = _metric_evidence(
            "khd-static-timeline", "native-inferred"
        )
    if native_link.get("hitSequenceStatus") == "unresolved":
        return metrics, evidence
    metrics["damage"] = [
        cell.get("damage") for cell in resolved_cells
        if isinstance(cell.get("damage"), (int, float))
        and not isinstance(cell.get("damage"), bool)
    ]
    if metrics["damage"]:
        evidence["damage"] = _metric_evidence("khd-attack-cell", "native-inferred")
    if not metrics["hitLevels"]:
        hit_levels = [cell.get("class") for cell in resolved_cells if cell.get("class")]
        if hit_levels:
            metrics["hitLevels"] = hit_levels
            evidence["hitLevels"] = _metric_evidence("khd-attack-cell", "native-inferred")
    return metrics, evidence


def build_v2_raw_movelist_payload(payload: dict[str, Any]) -> dict[str, Any]:
    movelist = payload.get("movelist") or {}
    rows = []
    for move in movelist.get("moves", []) or []:
        metrics, evidence = _move_metrics_from_payload(move, payload)
        rows.append({
            "order": move.get("order"),
            "moveId": move.get("moveId"),
            "category": move.get("category"),
            "categoryMemberships": move.get("categoryMemberships", []),
            "listingOrders": move.get("listingOrders", [move.get("order")]),
            "name": move.get("name", ""),
            "condition": move.get("condition", ""),
            "input": move.get("input", ""),
            "fullCommand": move.get("fullCommand", ""),
            "note": move.get("note", ""),
            "isMovementOnly": move.get("isMovementOnly", False),
            "isThrowInput": move.get("isThrowInput", False),
            "hasInputAlternatives": move.get("hasInputAlternatives", False),
            "hitClasses": move.get("hitClasses", []),
            "effectTags": move.get("effectTags", []),
            "mainTip": move.get("mainTip", ""),
            "lethalHitCondition": move.get("lethalHitCondition", ""),
            "groupIds": move.get("groupIds", []),
            "metrics": metrics,
            "evidence": evidence,
            "nativeLink": move.get("nativeLink") or _native_link(move),
        })
    return {
        "schemaVersion": SCHEMA_VERSION,
        "cid": payload.get("cid", ""),
        "name": payload.get("name", ""),
        "kind": payload.get("kind", "unknown"),
        "categories": movelist.get("categories", []),
        "moveGroups": movelist.get("moveGroups", []),
        "rows": rows,
    }


def build_v2_lookup_index(player_payloads: list[dict[str, Any]]) -> dict[str, Any]:
    chars = []
    families = []
    for player in player_payloads:
        chars.append({
            "cid": player.get("cid", ""),
            "name": player.get("name", ""),
            "kind": player.get("kind", "unknown"),
            "uncertain": player.get("uncertain", False),
        })
        stats_by_family = player.get("dashboard", {}).get("statsByFamily", {})
        for family in player.get("playerMoveFamilies", []) or []:
            families.append({
                "cid": player.get("cid", ""),
                "charName": player.get("name", ""),
                "kind": player.get("kind", "unknown"),
                "familyId": family.get("id", ""),
                "rootCommand": family.get("rootCommand", ""),
                "rootName": family.get("rootName", ""),
                "context": family.get("context", "Neutral"),
                "confidence": family.get("confidence", "unknown"),
                "relations": family.get("relations", []),
                "rowCount": len(family.get("rows", []) or []),
                "metrics": stats_by_family.get(family.get("id"), _family_metric_summary(family)),
                "linkStatusCounts": _link_status_counts_for_family(family),
                "commandKeys": _family_command_keys(family),
                "searchText": _family_search_text(player.get("name", ""), family),
            })
    return {
        "schemaVersion": SCHEMA_VERSION,
        "chars": chars,
        "families": families,
    }


def _player_row_from_official_move(
    cid: str,
    move: dict[str, Any],
    khd: KhdFile | None,
    confidence: str,
) -> dict[str, Any]:
    if move.get("metrics") is not None and move.get("evidence") is not None:
        metrics, evidence = move["metrics"], move["evidence"]
    else:
        metrics, evidence = _move_metrics(move, khd)
    return {
        "id": f"movelist-{cid}-{int(move['order']):05d}",
        "displayCommand": move.get("input", ""),
        "displayName": move.get("name", ""),
        "rootName": re.split(r"\s*~\s*", move.get("name", ""), maxsplit=1)[0],
        "context": move.get("condition", "") or "Neutral",
        "source": "game-movelist-table",
        "confidence": confidence,
        "isThrowInput": bool(move.get("isThrowInput", False)),
        "parserMoveOrders": [
            int(order) for order in move.get("listingOrders", [move["order"]])
        ],
        "nativeLink": move.get("nativeLink") or _native_link(move),
        "metrics": metrics,
        "evidence": evidence,
        "notes": move.get("note", ""),
    }


def _official_family(
    cid: str,
    members: list[dict[str, Any]],
    khd: KhdFile | None,
    relation: str,
    reason: str,
) -> dict[str, Any]:
    members = sorted(members, key=lambda move: int(move.get("order", 0)))
    grouping_confidence = "game-authored" if relation in {"duplicate-listing", "single-row"} else "native-inferred"
    rows = [_player_row_from_official_move(cid, move, khd, grouping_confidence) for move in members]
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
            "confidence": grouping_confidence,
            "reasons": [reason],
            "source": "game-movelist-table",
        })
    root_order = int(root["parserMoveOrders"][0])
    return {
        # Root official order is stable when unrelated families are regrouped;
        # a sequential family index would make bookmarked URLs drift.
        "id": f"player-family-{cid}-official-{root_order:05d}",
        "cid": cid,
        "kind": "official-group" if len(rows) > 1 else "single-row",
        "rootCommand": root.get("displayCommand", ""),
        "rootName": root.get("rootName", root.get("displayName", "")),
        "context": root.get("context", "Neutral"),
        "confidence": grouping_confidence,
        "relations": sorted({edge["relation"] for edge in edges}),
        "rows": rows,
        "edges": edges,
    }


def _build_official_families(
    cid: str,
    moves: list[dict[str, Any]],
    move_groups: list[dict[str, Any]],
    khd: KhdFile | None,
) -> list[dict[str, Any]]:
    moves_by_order = {int(move["order"]): move for move in moves}
    assigned: set[int] = set()
    families: list[dict[str, Any]] = []

    def add_group(group: dict[str, Any], relation: str, reason: str) -> None:
        orders = [int(order) for order in group.get("orders", [])]
        orders = [order for order in orders if order not in assigned and order in moves_by_order]
        if len(orders) < 2:
            return
        members = [moves_by_order[order] for order in orders]
        families.append(_official_family(cid, members, khd, relation, reason))
        assigned.update(orders)

    # Category duplicates are canonicalized before this layer. Relationship
    # groups now describe distinct move identities only.
    for group in move_groups:
        if group.get("kind") == "duplicate-move-id":
            add_group(group, "duplicate-listing", "same game-authored MoveListID")
    for group in move_groups:
        if group.get("kind") == "native-route-alternative":
            add_group(
                group,
                "native-route-alternative",
                "same official name, context, and statically resolved native slot/cell route",
            )
    for group in move_groups:
        if group.get("kind") == "native-timing-variant":
            add_group(
                group,
                "timing-variant",
                "same native input masks with different authored durations",
            )
    for group in move_groups:
        if group.get("kind") == "input-family":
            add_group(group, "prefix", "inferred command continuation boundary")

    for move in moves:
        order = int(move["order"])
        if order in assigned:
            continue
        families.append(_official_family(
            cid,
            [move],
            khd,
            "single-row",
            "one authored movelist row",
        ))
        assigned.add(order)
    return families


def _build_player_move_families(
    cid: str,
    moves: list[dict[str, Any]],
    move_groups: list[dict[str, Any]],
    khd: KhdFile | None,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    families = _build_official_families(cid, moves, move_groups, khd)
    grouping_counts = Counter()
    link_counts = Counter()
    metric_coverage = Counter()
    player_rows: list[dict[str, Any]] = []
    for family in families:
        grouping_counts[family["confidence"]] += 1
        for row in family["rows"]:
            player_rows.append(row)
            link_counts[(row.get("nativeLink", {}) or {}).get("status", "unresolved")] += 1
            for metric, value in (row.get("metrics", {}) or {}).items():
                if value is not None and value != [] and value != "":
                    metric_coverage[metric] += 1

    linked_rows = sum(
        count for status, count in link_counts.items()
        if status in {"confirmed", "heuristic"}
    )
    summary = {
        "officialRows": len(moves),
        "playerFamilies": len(families),
        "playerRows": len(player_rows),
        "nativeLinkedRows": linked_rows,
        "nativeUnlinkedRows": len(player_rows) - linked_rows,
        "linkStatusCounts": dict(link_counts),
        "groupingConfidenceCounts": dict(grouping_counts),
        "metricCoverage": dict(metric_coverage),
    }
    return families, summary


def _serialize_reaction_route(route: Any | None) -> dict[str, Any] | None:
    if route is None:
        return None
    return {
        "reactionRowId": route.reaction_row_id,
        "rawMoveIds": list(route.raw_move_ids),
        "packedMoveIds": list(route.packed_move_ids),
        "resolvedSlots": list(route.resolved_slots),
        "driverMoveIds": list(route.driver_move_ids),
    }


def _serialize_frame_proof(frame: Any, status: str) -> dict[str, Any]:
    return {
        "status": status,
        "attackSlot": frame.attack_slot,
        "attackCell": frame.attack_cell,
        "totalFrames": frame.total_frames,
        "cellWindowStartCoordinate": frame.cell_window_start_coordinate,
        "cellWindowEndCoordinate": frame.cell_window_end_coordinate,
        "recoveryLead": frame.recovery_lead,
        "recoveryOpenCoordinate": frame.recovery_open_coordinate,
        "inclusiveRecoveryFrames": frame.inclusive_recovery_frames,
        "defenderStunFrames": {
            "block": frame.block_stun_frames,
            "hit": frame.hit_stun_frames,
            "counterHit": frame.counter_hit_stun_frames,
        },
        "reactionRoutes": {
            "hit": _serialize_reaction_route(frame.hit_reaction),
            "counterHit": _serialize_reaction_route(frame.counter_hit_reaction),
        },
        "advantages": {
            "block": frame.block_advantage,
            "hit": frame.hit_advantage,
            "counterHit": frame.counter_hit_advantage,
        },
    }


def _attach_startup_proof(native_link: dict[str, Any], khd: KhdFile | None) -> None:
    """Attach zero-based/variant-aware startup evidence to one native link."""

    if khd is None or native_link.get("status") not in {"confirmed", "heuristic"}:
        return
    if native_link.get("startupTimingStatus") == "unresolved":
        return
    slots, cells = _native_attack_refs(native_link)
    if not slots or not cells:
        return
    startup = analyze_player_startup(khd, slots[0], cells[0])
    if startup is None:
        return
    native_link["startupProof"] = serialize_startup_evidence(startup)
    native_link["resolutions"] = sorted({
        *native_link.get("resolutions", []),
        startup.resolution,
    })


def _annotate_native_hit_sequence(move: dict[str, Any]) -> None:
    """Reject metrics when native contact count contradicts authored hits.

    ``DA_MoveListTable.hitClasses`` is a game-authored per-hit sequence.  A
    different number of effective native attack cells proves that the static
    route is incomplete or overlong; it cannot represent a completed damage
    sequence or the final contact used for Block/Hit/CH recovery.  Startup is
    kept separate because a proven first-contact endpoint remains useful.
    """

    native_link = move.get("nativeLink", {}) or {}
    native_link.pop("hitSequenceStatus", None)
    if (
        move.get("isMovementOnly")
        or move.get("isThrowInput")
        or native_link.get("status") not in {"confirmed", "heuristic"}
    ):
        return
    authored_hits = list(move.get("hitClasses", []) or [])
    if not authored_hits:
        return
    _, attack_cells = _native_attack_refs(native_link)
    if len(attack_cells) == len(authored_hits):
        native_link["hitSequenceStatus"] = "resolved"
        return

    native_link["hitSequenceStatus"] = "unresolved"
    native_link["frameEndpointStatus"] = "unresolved"
    native_link.pop("frameProof", None)
    native_link["resolutions"] = sorted({
        *native_link.get("resolutions", []),
        (
            "native-hit-sequence-count-mismatch:"
            f"game-authored={len(authored_hits)};native-cells={len(attack_cells)}"
        ),
    })


def _frame_proof_cell(native_link: dict[str, Any]) -> int | None:
    """Use the effective startup cell for a single-hit frame proof."""

    slots, cells = _native_attack_refs(native_link)
    if not cells:
        return None
    startup = native_link.get("startupProof")
    if (
        len(cells) == 1
        and len(slots) == 1
        and isinstance(startup, dict)
        and startup.get("attackSlot") == slots[0]
        and isinstance(startup.get("effectiveCell"), int)
    ):
        return int(startup["effectiveCell"])
    return cells[-1]


def _build_movelist_payload(
    cid: str,
    data: dict[str, Any],
    movelist_idx: dict[int, Any],
    khd: KhdFile | None,
    slot_graph: Any = None,
    move_meta: dict[int, dict[str, Any]] | None = None,
    move_play_commands: MovePlayCommandTable | None = None,
    native_dispatcher: NativeDispatcherResolver | None = None,
    reaction_table: LuxHitReactionMoveIdTable | None = None,
) -> dict[str, Any]:
    move_meta = move_meta or {}
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
                mi = int(cs.get("MainIndex", 0) or 0)
                intro = int(cs.get("IntroIndex", 0) or 0)
                # Drop entries we can't resolve to anything — they'd be
                # un-clickable + statless in the UI, just noise.
                command_sets.append({
                    "commandSetIndex": command_set_index,
                    "lane": "primary-fighter" if command_set_index == 0 else "paired-opponent",
                    "mainIndex": mi,
                    "introIndex": intro,
                    "cellIdx": -1,
                    "slotIdx": -1,
                    "resolution": (
                        "movevm-main-definition-navigation" if mi else
                        "movevm-fallback-definition-navigation" if intro else
                        "inactive-lane"
                    ),
                    "tracking": _tracking_for_slot(-1, None),
                })
            entry = movelist_idx.get(move_id)
            full_cmd = entry.command if entry else ""
            note = entry.note if entry else ""
            condition, button_input = locales.split_condition_and_input(full_cmd)
            # Direction-only inputs remain official rows but have no metrics.
            movement_only = _is_pure_direction_input(button_input)
            # Preserve Bandai's authored alternative-input marker. Static
            # KHD sibling guesses are not exported as player-facing variants.
            has_input_alternatives = "|" in button_input and not movement_only
            input_variants: list[dict[str, Any]] = []
            # DA_MoveListTable per-move metadata, keyed by MoveListID.
            _meta = move_meta.get(move_id, {})
            effect_tags = _meta.get("effectTags", []) or []
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
                # Movement-only moves render without frame metrics.
                "isMovementOnly": movement_only,
                # True when Bandai's localization lists `|`-separated inputs.
                "hasInputAlternatives": has_input_alternatives,
                "inputVariants": input_variants,
                "tracking": _merge_tracking(command_sets),
                # Filled after native linking.  TH alone is not enough because
                # SC6 also applies it to strikes with throw follow-ups.
                "isThrowInput": False,
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
                "effectTags": effect_tags,
                "mainTip": _meta.get("mainTip", ""),
                "lethalHitCondition": _meta.get("lethalHitCondition", ""),
                "commandSets": command_sets,
            })
            moves[-1]["nativeLink"] = _native_link(
                moves[-1], move_play_commands, native_dispatcher
            )
            inferred_link = moves[-1]["nativeLink"]
            _attach_startup_proof(inferred_link, khd)
            moves[-1]["isThrowInput"] = _is_native_throw_attempt(moves[-1], khd)
            _annotate_native_hit_sequence(moves[-1])
            if (
                inferred_link.get("status") == "heuristic"
                and inferred_link.get("frameEndpointStatus") != "unresolved"
                and all(_native_attack_refs(inferred_link))
                and khd is not None
            ):
                attack_slots, _ = _native_attack_refs(inferred_link)
                attack_slot = int(attack_slots[-1])
                attack_cell = _frame_proof_cell(inferred_link)
                frame = analyze_confirmed_slot_frames(
                    khd,
                    attack_slot=attack_slot,
                    attack_cell=int(attack_cell),
                    reaction_table=reaction_table,
                )
                if frame is not None:
                    inferred_link["frameProof"] = _serialize_frame_proof(
                        frame, "heuristic"
                    )
                    inferred_link["resolutions"] = sorted({
                        *inferred_link.get("resolutions", []),
                        *frame.resolutions,
                        "native-frame-endpoints:confirmed;route-link=inferred",
                    })
            native_route = resolve_native_route(
                cid, moves[-1], khd, reaction_table=reaction_table
            )
            if native_route is not None:
                frame = native_route.frame_advantage
                moves[-1]["nativeLink"] = {
                    "status": "confirmed",
                    "resolutions": sorted({
                        *moves[-1]["nativeLink"]["resolutions"],
                        *native_route.resolutions,
                    }),
                    "definitions": moves[-1]["nativeLink"]["definitions"],
                    # These retain path order.  Unlike the definition ID,
                    # each value is backed by the audited MoveVM route.
                    "slots": list(native_route.slots),
                    "cells": list(native_route.cells),
                    "attackSlots": list(native_route.slots),
                    "attackCells": list(native_route.cells),
                    "startupTimingStatus": "resolved",
                    "frameEndpointStatus": "resolved",
                }
                _attach_startup_proof(moves[-1]["nativeLink"], khd)
                moves[-1]["isThrowInput"] = _is_native_throw_attempt(moves[-1], khd)
                _annotate_native_hit_sequence(moves[-1])
                proof_cell = _frame_proof_cell(moves[-1]["nativeLink"])
                if (
                    proof_cell is not None
                    and len(native_route.slots) == 1
                    and len(native_route.cells) == 1
                    and proof_cell != native_route.cells[-1]
                ):
                    frame = analyze_confirmed_slot_frames(
                        khd,
                        attack_slot=int(native_route.slots[-1]),
                        attack_cell=int(proof_cell),
                        reaction_table=reaction_table,
                    )
                if (
                    frame is not None
                    and moves[-1]["nativeLink"].get("hitSequenceStatus") != "unresolved"
                ):
                    moves[-1]["nativeLink"]["frameProof"] = _serialize_frame_proof(
                        frame, "confirmed"
                    )
                    moves[-1]["nativeLink"]["resolutions"] = sorted({
                        *moves[-1]["nativeLink"].get("resolutions", []),
                        *frame.resolutions,
                        "native-frame-endpoints:confirmed",
                    })
            moves[-1]["isThrowInput"] = _is_native_throw_attempt(moves[-1], khd)
            _annotate_native_hit_sequence(moves[-1])
            _attach_throw_break_proof(moves[-1], khd)
            metrics, evidence = _move_metrics(moves[-1], khd)
            moves[-1]["metrics"] = metrics
            moves[-1]["evidence"] = evidence
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
    moves = _canonicalize_category_listings(moves)
    # Native throw classification belongs to the canonical move identity,
    # not to an individual in-game menu listing.  Authored category variants
    # can carry different MovePlay navigation; when canonicalization marks
    # those links ambiguous we intentionally publish no break frame.
    for move in moves:
        move["isThrowInput"] = _is_native_throw_attempt(move, khd)
        native_link = move.get("nativeLink", {}) or {}
        native_link.pop("throwBreakProof", None)
        _annotate_native_hit_sequence(move)
        _attach_throw_break_proof(move, khd)
        metrics, evidence = _move_metrics(move, khd)
        move["metrics"] = metrics
        move["evidence"] = evidence
    for category in categories:
        category_index = int(category["index"])
        category["itemOrders"] = [
            int(move["order"])
            for move in moves
            if category_index in move.get("categoryMemberships", [])
        ]
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
        # Native animation coordinates. These are deliberately not named
        # startup/impact frames: a cell window may be a setup variant, and
        # coordinate N occurs on the (N+1)th player-counted frame.
        "activeStartCoordinate": c.wI16MasterWindowStart,
        "activeEndCoordinate": c.wI16MasterWindowEnd,
        "activeFrames": c.active_frame_count,
        "hasValidActiveWindow": c.has_valid_active_window,
        "blockStunFrames": c.wI16BlockstunFrames,
        "baseHitStunFrames": c.wI16HitstunBaseContact,
        "specialHitStunFrames": c.wI16HitstunSpecialContact,
        "alternatePostureBaseHitStunFrames": c.wI16HitstunAlternatePostureBaseContact,
        "alternatePostureSpecialHitStunFrames": c.wI16HitstunAlternatePostureSpecialContact,
        "reactionIdBaseContact": c.wI16ReactionIdBaseContact,
        "reactionIdSpecialContact": c.wI16ReactionIdSpecialContact,
        "throwEscapeId": c.wI16ThrowReactionRowId,
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


def export_char(cid: str, paths: dict[str, str], out_path: str) -> dict[str, Any]:
    """Write the full per-character JSON."""
    payload: dict[str, Any] = {
        "schemaVersion": SCHEMA_VERSION,
        "cid": cid,
        **{k: v for k, v in CHARA_NAMES.get(cid, {"name": f"chara_{cid}"}).items()},
        "files": {k: (k in paths) for k in
                  ("khd", "mot", "dtp", "atkhit", "bodyhit", "yararehit")},
    }
    move_play_commands: MovePlayCommandTable | None = None
    if "dtp" in paths:
        try:
            with open(paths["dtp"], "rb") as f:
                move_play_commands = parse_move_play_command_table(f.read())
        except Exception as exc:
            payload["movePlayCommandError"] = str(exc)
    if "khd" in paths:
        try:
            k = parse_auto(paths["khd"])
            native_dispatcher: NativeDispatcherResolver | None = None
            command_path = Path(paths["khd"]).with_name("command.dat")
            reaction_table: LuxHitReactionMoveIdTable | None = None
            reaction_table_path = Path(paths["khd"]).with_name("yarare.dat")
            if reaction_table_path.is_file():
                reaction_table = parse_hit_reaction_move_id_table(
                    reaction_table_path.read_bytes()
                )
            codec_tables = _load_codec_tables()
            if (
                move_play_commands is not None
                and command_path.is_file()
                and codec_tables is not None
            ):
                transition_commands = parse_transition_command_table(
                    command_path.read_bytes()
                )
                native_dispatcher = NativeDispatcherResolver(
                    k, transition_commands, codec_tables
                )
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
            # is independent of KHD cell extraction.
            # Command-set indices are exported only as their proven MoveVM
            # definition references, never as inferred cells or slots.
            ml = load_movelist_for_chara(
                cid,
                k,
                slot_graph=graph,
                move_play_commands=move_play_commands,
                native_dispatcher=native_dispatcher,
                reaction_table=reaction_table,
            )
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
                            "contactImpulseScale": r.contact_impulse_scale,
                            "boneIndexUe4": r.bone_index_ue4,
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
    return payload


def write_v2_char_shards(payload: dict[str, Any], out_dir: str) -> dict[str, Any]:
    player_payload = build_v2_player_payload(payload)
    raw_payload = build_v2_raw_movelist_payload(payload)
    char_dir = os.path.join(out_dir, "v2", "chars", str(payload.get("cid", "")))
    os.makedirs(char_dir, exist_ok=True)
    with open(os.path.join(char_dir, "player.json"), "w", encoding="utf-8") as f:
        json.dump(player_payload, f, separators=(",", ":"))
    with open(os.path.join(char_dir, "raw-movelist.json"), "w", encoding="utf-8") as f:
        json.dump(raw_payload, f, separators=(",", ":"))
    return player_payload


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
    ap = argparse.ArgumentParser(description="Export SC6 moveset data to JSON for the webui")
    ap.add_argument("--root", default=BATTLE_ROOT_DEFAULT, help="Battle data root")
    ap.add_argument("--out-dir", "--out", dest="out_dir", default=OUT_DIR_DEFAULT, help="Output directory")
    args = ap.parse_args()

    _require_coherent_content_roots(args.root)
    discovered = discover_chars(args.root)
    # Production Move Lookup is the normal playable roster. Shared, boss,
    # unknown, and hitbox-only helper banks remain parser inputs/diagnostics,
    # but do not become empty characters in either UI's lookup index.
    chars = {
        cid: paths for cid, paths in discovered.items()
        if CHARA_NAMES.get(cid, {}).get("kind") in {"base", "dlc"}
    }
    print(f"Discovered {len(chars)} characters under {args.root}")
    os.makedirs(args.out_dir, exist_ok=True)
    os.makedirs(os.path.join(args.out_dir, "chars"), exist_ok=True)

    roster = []
    v2_player_payloads: list[dict[str, Any]] = []
    for cid in sorted(chars):
        summary = char_summary(cid, chars[cid])
        roster.append(summary)
        char_path = os.path.join(args.out_dir, "chars", f"{cid}.json")
        payload = export_char(cid, chars[cid], char_path)
        if payload is None:
            try:
                with open(char_path, "r", encoding="utf-8") as f:
                    payload = json.load(f)
            except Exception:
                payload = None
        if payload is not None:
            player_payload = write_v2_char_shards(payload, args.out_dir)
            v2_player_payloads.append(player_payload)
        ac = summary.get("attackCount", "-")
        td = summary.get("topDamage", "-")
        print(f"  {cid}  {summary['name']:<18}  attacks={ac:>4}  topDmg={td}")

    with open(os.path.join(args.out_dir, "roster.json"), "w", encoding="utf-8") as f:
        json.dump({"chars": roster}, f, indent=2)
    os.makedirs(os.path.join(args.out_dir, "v2"), exist_ok=True)
    with open(os.path.join(args.out_dir, "v2", "lookup-index.json"), "w", encoding="utf-8") as f:
        json.dump(build_v2_lookup_index(v2_player_payloads), f, separators=(",", ":"))
    print(f"\nWrote roster + {len(roster)} char files + v2 shards to {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
