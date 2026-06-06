#!/usr/bin/env python3
"""Compare community frame-data rows against parsed KHD move payloads.

The parser can resolve a single move/command pair in a few ways. This
script reports where community rows cannot be paired, are ambiguously
paired, or where parsed cell metrics diverge from the community sheet.

It is intentionally investigative (diagnostic output first, not an
assertion that one data source is authoritative).
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

from community_framedata import load as load_community, norm_input_key, norm_name


ROOT = Path(__file__).resolve().parent
DEFAULT_DATA_DIR = ROOT / "webui" / "public" / "data"

# Short-hand tokens used by the community sheet are often abbreviations of the
# in-game `condition` phrases exported from the movelist payload.
_COMMUNITY_STANCE_ABBREVIATIONS: dict[str, str] = {
    "AG": "arriere gambit",
    "AGS": "aggression shift",
    "AL": "aerial leap",
    "ANY STANCE": "in a special stance",
    "AT": "angelic twirl",
    "AVN": "in avenger stance",
    "AX": "in ax mode",
    "BHH": "bea her hua",
    "BKN": "bare knuckles",
    "BL": "behind lower",
    "BOB": "beauty of balance",
    "BP": "back parry",
    "BS": "facing away",
    "BT": "facing away",
    "COE": "comedy of errors",
    "CR": "caliostro rush",
    "DC": "dread charge",
    "DF": "divine force",
    "DGF": "manji dragonfly",
    "DL": "dark legacy",
    "FJ": "flames of justice",
    "FLE": "flea",
    "GS": "gloomy",
    "JS": "jolly",
    "LO": "left outer",
    "LI": "left inner",
    "MCFT": "mantis crawl with feet toward opponent",
    "MCHT": "mantis crawl with head toward opponent",
    "MST": "mist",
    "MO": "monument",
    "NG": "neutral guard",
    "NBS": "night behind stance",
    "NLS": "night lower stance",
    "PO": "possession",
    "PR": "preparation",
    "RC": "right cross",
    "RE": "reversal edge hits",
    "RE 2ND ROUND": "reversal edge hits",
    "RE SECOND ROUND": "reversal edge hits",
    "RLC": "relic",
    "RRP": "with red rose perception at max",
    "RO": "right outer",
    "SCH": "chief hold",
    "SC": "soul charged",
    "SC AVN": "soul charged & in avenger stance",
    "SC FC": "soul charged & crouching",
    "SC PO": "possession while soul charged",
    "SC RE": "soul charged after reversal edge hits",
    "SC WR": "soul charged & rising",
    "SE": "serpent's embrace",
    "SGDF": "super dragonfly",
    "SKY STAGE": "shrouded sky",
    "SRSH": "reverse side hold",
    "SS": "base hold",
    "STK": "stalker",
    "SWR": "side hold",
    "SXS": "silent xia sheng",
    "TAS": "twin angel step",
    "TS1": "inflicting a curse",
    "TS2": "inflicting a curse",
    "TS3": "inflicting a curse",
    "WNC": "wind charmer",
    "WNF": "wind fury",
    "WNS": "wind sault",
    "WOH": "wings of heaven",
    "WR": "rising",
    "WRP": "with white rose perception at max",
    "WF": "warrior's focus",
    "WRO": "wind roll",
    "BS WR": "rising & facing away",
}


def _read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _to_int(raw: Any) -> int | None:
    """Extract first integer from a value, or None."""
    s = str(raw or "").strip()
    if not s:
        return None
    m = re.search(r"-?\d+", s)
    return int(m.group(0)) if m else None


def _is_valid_startup(value: Any) -> bool:
    return isinstance(value, int) and 0 <= value <= 499


def _is_valid_active_window(start: Any, end: Any) -> bool:
    return (
        _is_valid_startup(start)
        and _is_valid_startup(end)
        and end >= start
    )


def _to_nonneg_int(raw: Any) -> int:
    value = _to_int(raw)
    return value if value is not None and value >= 0 else -1

def _sum_damage(raw: Any) -> int | None:
    values = _damage_segments(raw)
    return sum(values) if values else None


def _tokenize_command(raw: Any) -> tuple[str, ...]:
    s = str(raw or "").strip().upper()
    if not s:
        return ()
    s = re.sub(r"[\s:~_|-]", "", s)
    # Keep direction/button runs but remove grouping punctuation that is cosmetic in this
    # context.
    s = s.replace("(", "").replace(")", "").replace("[", "").replace("]", "")
    if not s:
        return ()
    segments = [seg for seg in re.split(r"\.+", s) if seg]
    tokens: list[str] = []
    for seg in segments:
        for part in seg.split("+"):
            part = part.strip()
            if not part:
                continue
            # Direction chains are authored as numeric runs (e.g. 236A -> 236 + A).
            m = re.fullmatch(r"([0-9]+)([A-Z]+)", part)
            if m:
                digits, letters = m.groups()
                if digits:
                    tokens.append(digits)
                if letters:
                    tokens.extend(list(letters))
                continue
            if re.fullmatch(r"[0-9]+", part):
                tokens.append(part)
                continue
            # Community data often drops separator dots and repeats buttons as BB / AAA.
            if re.fullmatch(r"[A-Z]{2,}", part):
                tokens.extend(list(part))
            else:
                tokens.append(part)
    return tuple(tokens)


def _is_throw_command(raw: Any) -> bool:
    if raw is None:
        return False
    s = str(raw).upper()
    if "G" not in s or "+" not in s:
        return False
    cleaned = re.sub(r"[()\[\]]", "", s)
    cleaned = re.sub(r"\s+", "", cleaned)
    return bool(re.search(r"(?:[ABK]\+G|G\+[ABK])", cleaned))


def _parsed_metrics(
    rec: dict[str, Any] | None,
) -> dict[str, Any]:
    cell = rec.get("cell") if rec else None
    if not cell:
        return {
            "found": False,
            "damage": None,
            "startup": None,
            "onBlock": None,
            "onHit": None,
            "activeFrames": None,
            "isThrow": False,
            "resolution": "none",
            "candidateCount": 0,
            "candidateBestRank": 0,
            "candidateScore": 0,
        }
    startup = cell.get("activeStart")
    active_end = cell.get("activeEnd")
    if not _is_valid_active_window(startup, active_end):
        startup = None
    return {
        "found": True,
        "damage": cell.get("damage"),
        "startup": startup,
        "onBlock": cell.get("onBlock"),
        "onHit": cell.get("onHitStanding"),
        "activeFrames": cell.get("activeFrames"),
        "isThrow": _is_throw_command(rec.get("input")),
        "resolution": rec.get("resolution", "none"),
        "candidateCount": rec.get("candidateCount", 0),
        "candidateBestRank": rec.get("candidateBestRank", 0),
        "candidateScore": rec.get("candidateScore", 0),
    }


def _community_metrics(comm: dict[str, Any]) -> dict[str, Any]:
    """Normalized community values used for candidate scoring."""
    damage_values = _damage_segments(comm.get("damage"))
    comm_condition = _norm_condition(comm.get("stance") or comm.get("condition") or "")
    startup = _to_int(comm.get("startup"))
    if not _is_valid_startup(startup):
        startup = None
    return {
        "startup": startup,
        "damage": sum(damage_values) if damage_values else None,
        "damageValues": damage_values,
        "damageCount": len(damage_values),
        "commandHitCount": _count_command_hits(comm.get("command")),
        "isMultiHit": len(damage_values) > 1,
        "isThrow": _is_throw_command(comm.get("command")),
        "onBlock": _to_adv(comm.get("block")),
        "onHit": _to_adv(comm.get("hit")),
        "hitLevelCount": len(comm.get("hitLevels") or []),
        "condition": comm_condition,
        "conditionTerms": _condition_terms(comm_condition),
    }


def _commands_match(comm_tokens: tuple[str, ...], parsed_tokens: tuple[str, ...]) -> bool:
    if not comm_tokens or not parsed_tokens:
        return False
    # Strict equality across token streams.
    # Prefix matching caused false positives where shorthand entries from
    # the sheet (e.g. "A", "AA") matched longer move-command chains
    # like "A.A.A" and inflated confidence in the wrong mapping.
    return comm_tokens == parsed_tokens


def _damage_segments(raw: Any) -> list[int]:
    if raw is None:
        return []
    if isinstance(raw, list):
        values = raw
    else:
        values = str(raw).split(",")
    return [n for n in [ _to_int(seg) for seg in values ] if n is not None]


def _count_command_hits(raw: Any) -> int:
    return max(1, len(_tokenize_command(raw)))


def _to_adv(raw: Any) -> int | str | None:
    """Community stun values are sometimes numeric, sometimes states.

    Keep non-numeric tokens for richer mismatch analysis.
    """
    s = str(raw or "").strip()
    if not s:
        return None
    n = _to_int(s)
    if n is not None:
        return n
    return s


def _norm_condition(s: str) -> str:
    s = str(s or "").strip().lower()
    if not s:
        return ""
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"^(while|during|after|if|on|at)\s+", "", s)
    return s


_CONDITION_CANONICAL_TERMS = {
    "while crouching": "while crouching",
    "while rising": "while rising",
    "while soul charged": "while soul charged",
    "after reversal edge hits": "reversal edge",
    "facing away": "facing away",
    "during jump": "during jump",
    "after running": "after running",
    "while jolly": "while jolly",
    "while gloomy": "while gloomy",
    "while down": "while down",
    "with red rose perception at max": "with red rose perception at max",
    "with white rose perception at max": "with white rose perception at max",
}

_CONDITION_STOPWORDS = {
    "during", "while", "after", "if", "on", "at", "the", "in", "and", "with", "into", "from",
    "after", "before", "facing", "toward", "towards", "is", "are", "a", "an", "is", "or",
}


def _condition_terms(s: str) -> tuple[str, ...]:
    """Split a normalized condition into a stable set of comparable terms."""
    if not s:
        return ()
    c = _norm_condition(s)
    if not c:
        return ()
    terms: set[str] = set()
    for part in re.split(r"\s*&\s*", c):
        part = part.strip()
        if not part:
            continue
        lower = part.lower()
        matched = False
        for src, dst in _CONDITION_CANONICAL_TERMS.items():
            if src in lower:
                terms.add(dst)
                lower = lower.replace(src, " ")
                matched = True
        tokens = [t for t in re.split(r"\s+", lower.strip()) if t and t not in _CONDITION_STOPWORDS]
        terms.update(tokens)
        if matched and not tokens:
            continue
        if tokens:
            continue
    return tuple(sorted(terms))


def _combine_condition_terms(parts: list[str]) -> tuple[str, ...]:
    terms: list[str] = []
    for part in parts:
        terms.extend(_condition_terms(part))
    return tuple(sorted(set(terms)))


def _normalize_community_condition(
    raw: str,
    parsed_records: list[dict[str, Any]] | None = None,
) -> tuple[str, ...]:
    """Map spreadsheet stances to parsed condition terms.

    Known abbreviations (`WR`, `FC`, `SC`, ...) are expanded first.
    If the value is still short and all candidate parsed records are aligned
    on the same non-empty condition, we infer that one condition.
    """
    if raw is None:
        return ()
    raw = str(raw).strip()
    if not raw:
        return ()

    norm = re.sub(r"\s+", " ", raw).strip()
    norm_upper = norm.upper()

    direct = _COMMUNITY_STANCE_ABBREVIATIONS.get(norm_upper)
    if direct:
        return _condition_terms(direct)

    # Compose known abbreviation tokens: `SC WR` -> `soul charged & rising`
    tokens = norm_upper.split()
    if len(tokens) > 1:
        mapped_tokens: list[str] = []
        for token in tokens:
            value = _COMMUNITY_STANCE_ABBREVIATIONS.get(token)
            if value is None:
                mapped_tokens = []
                break
            mapped_tokens.append(value)
        if mapped_tokens:
            return _combine_condition_terms(mapped_tokens)

    # Heuristic: short labels like `WR` may appear in rare rows with no
    # dictionary entry. Only trust this path when parsed candidates are
    # unanimously one condition.
    if parsed_records and len(norm.replace(" ", "")) <= 6:
        parsed_condition_terms = {
            _condition_terms(rec.get("conditionNorm", ""))
            for rec in parsed_records
            if rec.get("conditionNorm")
        }
        if len(parsed_condition_terms) == 1:
            return next(iter(parsed_condition_terms))

    return _condition_terms(norm)


def _build_movelist_index(
    char_payload: dict[str, Any],
) -> tuple[
    list[dict[str, Any]],
    dict[tuple[str, str], list[dict[str, Any]]],
    dict[str, list[dict[str, Any]]],
    dict[tuple[str, str, str], list[dict[str, Any]]],
    dict[tuple[str, str], list[dict[str, Any]]],
]:
    moves = ((char_payload.get("movelist") or {}).get("moves") or [])
    cells = (char_payload.get("khd") or {}).get("cells") or []

    records: list[dict[str, Any]] = []
    by_key: dict[tuple[str, str], list[dict[str, Any]]] = {}
    by_name: dict[str, list[dict[str, Any]]] = {}
    by_key_condition: dict[tuple[str, str, str], list[dict[str, Any]]] = {}
    by_name_condition: dict[tuple[str, str], list[dict[str, Any]]] = {}

    for move in moves:
        name = str(move.get("name") or "").strip()
        name_norm = norm_name(name)
        input_norm = norm_input_key(move.get("input") or "")
        command_tokens = _tokenize_command(move.get("input"))

        cs_list = move.get("commandSets") or []
        if not cs_list:
            cs_list = [None]

        condition = move.get("condition") or ""
        condition_norm = _norm_condition(condition)
        condition_terms = _condition_terms(condition_norm)
        condition_key = "|".join(condition_terms)

        for command_set_index, cs in enumerate(cs_list):
            cs_map = cs or {}
            cell_idx = _to_nonneg_int(cs_map.get("cellIdx", -1))
            slot_idx = _to_nonneg_int(cs_map.get("slotIdx", -1))
            role = None
            cell = None
            if isinstance(cell_idx, int) and 0 <= cell_idx < len(cells):
                cell = cells[cell_idx]
                role = cell.get("role")

            rec = {
                "name": name,
                "nameNorm": name_norm,
                "input": move.get("input") or "",
                "inputNorm": input_norm,
                "commandTokens": command_tokens,
                "commandTokenCount": len(command_tokens),
                "moveId": move.get("moveId"),
                "order": move.get("order"),
                "category": move.get("category"),
                "condition": condition,
                "conditionNorm": condition_norm,
                "conditionTerms": condition_terms,
                "conditionKey": condition_key,
                "commandSets": [] if cs is None else cs_list,
                "commandSetIndex": command_set_index,
                "commandSetCount": 1 if cs is None else len(cs_list),
                "mainIndex": cs_map.get("mainIndex", -1),
                "cellIdx": cell_idx,
                "slotIdx": slot_idx,
                "resolution": cs_map.get("resolution", "none"),
                "candidateCount": cs_map.get("candidateCount", 0),
                "candidateBestRank": cs_map.get("candidateBestRank", 0),
                "candidateScore": cs_map.get("candidateScore", 0),
                "cell": cell,
                "role": role,
                "hitClasses": move.get("hitClasses") or [],
                "commandHitCount": _count_command_hits(move.get("input")),
                "isMovementOnly": move.get("isMovementOnly", False),
            }

            records.append(rec)
            by_key.setdefault((name_norm, input_norm), []).append(rec)
            by_name.setdefault(name_norm, []).append(rec)
            by_key_condition.setdefault((name_norm, input_norm, condition_key), []).append(rec)
            by_name_condition.setdefault((name_norm, condition_key), []).append(rec)

    return records, by_key, by_name, by_key_condition, by_name_condition



def _metric_equal(
    key: str,
    parsed_value: Any,
    comm_value: Any,
    startup_tolerance: int,
    compare_raw_stun: bool,
) -> tuple[bool | None, int | None]:
    if parsed_value is None or comm_value is None:
        return None, None
    if key in {"onBlock", "onHit"} and not compare_raw_stun:
        return None, None
    if key == "startup" and not (
        _is_valid_startup(parsed_value) and _is_valid_startup(comm_value)
    ):
        return None, None
    if not isinstance(parsed_value, int) or not isinstance(comm_value, int):
        return None, None
    # Community on-hit / on-block columns are frequently signed frame advantage,
    # while parsed payload exposes raw stun. When community values look like
    # classical advantage and parsed appears stun-scale, skip direct compare.
    if key in {"onBlock", "onHit"} and compare_raw_stun:
        if comm_value < 0:
            return None, None
        if abs(comm_value) <= 40 and parsed_value > 28:
            return None, None
    if key == "startup":
        candidates = (parsed_value, parsed_value + 1)
        deltas = [abs(parsed_start - comm_value) for parsed_start in candidates]
        best_delta = min(deltas)
        return best_delta <= startup_tolerance, best_delta
    return parsed_value == comm_value, abs(parsed_value - comm_value)


def _summarize_diff(
    c_comm: dict[str, Any],
    c_parse: dict[str, Any],
    startup_tolerance: int,
    compare_raw_stun: bool,
) -> dict[str, Any]:
    comm_metrics = _community_metrics(c_comm)
    comm_is_throw = bool(comm_metrics.get("isThrow"))
    parse_is_throw = bool(c_parse.get("isThrow"))
    comm = {
        "startup": comm_metrics["startup"],
        "damage": _sum_damage(c_comm.get("damage")),
        "damageCount": comm_metrics["damageCount"],
        "isMultiHit": bool(comm_metrics["isMultiHit"]),
        "onBlock": _to_adv(c_comm.get("block")),
        "onHit": _to_adv(c_comm.get("hit")),
        "counterHit": _to_adv(c_comm.get("counterHit")),
    }

    parse = {
        "startup": c_parse.get("startup"),
        "damage": c_parse.get("damage"),
        "onBlock": c_parse.get("onBlock"),
        "onHit": c_parse.get("onHit"),
        "activeFrames": c_parse.get("activeFrames"),
    }

    diffs = {}
    for key in ("startup", "damage", "onBlock", "onHit"):
        if key == "startup" and (comm_is_throw or parse_is_throw):
            continue
        if key in {"onBlock", "onHit"} and (comm_is_throw or parse_is_throw):
            continue
        cv = comm.get(key)
        pv = parse.get(key)
        if key == "damage" and comm.get("isMultiHit"):
            continue
        equal, delta = _metric_equal(key, pv, cv, startup_tolerance, compare_raw_stun)
        if cv is None or pv is None or equal is None:
            continue
        diffs[key] = {
            "community": cv,
            "parsed": pv,
            "equal": equal,
            "delta": delta,
        }
    return diffs


def _score_candidates(
    matches: list[dict[str, Any]],
    comm_metrics: dict[str, Any],
    startup_tolerance: int,
    compare_raw_stun: bool,
) -> list[dict[str, Any]]:
    scored: list[dict[str, Any]] = []
    community_condition_terms = comm_metrics.get("conditionTerms", ()) or ()
    community_is_throw = bool(comm_metrics.get("isThrow"))
    for rec in matches:
        parsed = _parsed_metrics(rec)
        parsed_is_throw = bool(parsed.get("isThrow"))
        is_attack = parsed["found"] and rec.get("role") == "Attack"

        metric_distance = 0
        metric_matches = 0
        community_is_multi_hit = bool(comm_metrics.get("isMultiHit"))
        for key in ("startup", "damage", "onBlock", "onHit"):
            if key == "startup" and (community_is_throw or parsed_is_throw):
                continue
            if key == "damage" and community_is_multi_hit:
                continue
            if key in {"onBlock", "onHit"} and (community_is_throw or parsed_is_throw):
                continue
            p_val = parsed.get(key)
            c_val = comm_metrics.get(key)
            equal, delta = _metric_equal(key, p_val, c_val, startup_tolerance, compare_raw_stun)
            if equal is None:
                continue
            if equal:
                metric_matches += 1
            elif isinstance(delta, int):
                metric_distance += delta
            else:
                metric_distance += 4

        resolution = str(rec.get("resolution") or "")
        resolution_rank = {
            "cell-direct": 5,
            "slot-overrides-direct": 4,
            "cell-direct-invalid-startup": 3,
            "cell": 3,
            "slot": 2,
            "slot-no-cell": 1,
            "movement-only": 0,
            "none": 0,
        }.get(resolution, 1)
        candidate_best_rank = int(rec.get("candidateBestRank", 0) or 0)
        candidate_count = int(rec.get("candidateCount", 0) or 0)
        candidate_score = int(rec.get("candidateScore", 0) or 0)
        command_set_index = int(rec.get("commandSetIndex") or 0)
        main_index = int(rec.get("mainIndex") or 0)
        hit_len = len(rec.get("hitClasses") or [])
        comm_hit_len = comm_metrics.get("hitLevelCount")
        hit_len_delta = abs(hit_len - (comm_hit_len if comm_hit_len is not None else hit_len))
        # Heuristic: prefer earlier command-sets and lower mainIndex unless data
        # explicitly points elsewhere (conditions, metrics, and exact match).
        if not comm_metrics.get("conditionTerms"):
            metric_distance += 20 * command_set_index
        parsed_condition_terms = (
            rec.get("conditionTerms")
            or _condition_terms(rec.get("conditionNorm") or "")
        )
        if community_condition_terms:
            parsed_cond_set = set(parsed_condition_terms)
            community_cond_set = set(community_condition_terms)
            condition_overlap = len(parsed_cond_set.intersection(community_cond_set))
            condition_match = condition_overlap > 0
        else:
            condition_overlap = 0
            condition_match = True

        scored.append(
            {
                "record": rec,
                "parsed": parsed,
                "score": (
                    1 if is_attack else 0,
                    1 if community_is_throw == parsed_is_throw else -5,
                    metric_matches,
                    -metric_distance,
                    -(abs(len(community_condition_terms) - len(parsed_condition_terms))
                      if community_condition_terms
                    else 0),
                    1 if condition_match else 0,
                    condition_overlap,
                    1 if resolution.startswith("cell") else 0,
                    resolution_rank,
                    -candidate_best_rank,
                    candidate_count,
                    candidate_score,
                    -(main_index if main_index >= 0 else 1_000_000),
                    1 if not rec.get("isMovementOnly") else 0,
                    parsed["damage"] if isinstance(parsed["damage"], int) else -1,
                    -hit_len_delta,
                    -command_set_index,
                    -cell_idx if (cell_idx := rec.get("cellIdx", -1)) >= 0 else -9999,
                    -(parsed["startup"] if isinstance(parsed["startup"], int) else 9999),
                    -parsed["onHit"] if isinstance(parsed["onHit"], int) else 9999,
                    -parsed["onBlock"] if isinstance(parsed["onBlock"], int) else 9999,
                    -abs(
                        (rec.get("commandHitCount") or 1) - (comm_metrics.get("commandHitCount") or 1)
                    ),
                ),
                "metric_distance": metric_distance,
                "metric_matches": metric_matches,
                "is_attack": is_attack,
                "conditionOverlap": condition_overlap,
            }
        )

    scored.sort(key=lambda item: item["score"], reverse=True)
    return scored


def _pick_records(
    matches: list[dict[str, Any]],
    comm: dict[str, Any],
    community_condition_terms: tuple[str, ...] = (),
    exact_input: bool = False,
    startup_tolerance: int = 0,
    compare_raw_stun: bool = False,
) -> tuple[dict[str, Any] | None, str, dict[str, Any]]:
    if not matches:
        return None, "unmatched", {"found": False}

    if community_condition_terms:
        if len(matches) > 1:
            community_cond_set = set(community_condition_terms)
            exact = [
                rec
                for rec in matches
                if (
                    set(rec.get("conditionTerms") or _condition_terms(rec.get("conditionNorm") or ""))
                    == community_cond_set
                )
            ]
            if exact:
                matches = exact
            else:
                filtered = [
                    rec
                    for rec in matches
                    if (
                        set(rec.get("conditionTerms") or _condition_terms(rec.get("conditionNorm") or ""))
                        & community_cond_set
                    )
                ]
                if filtered:
                    matches = filtered

    comm_metrics = _community_metrics(comm)
    scored = _score_candidates(
        matches, comm_metrics, startup_tolerance=startup_tolerance, compare_raw_stun=compare_raw_stun
    )
    if not scored:
        return None, "unmatched", {"found": False}

    best = scored[0]
    picked = best["record"]
    parsed = best["parsed"]
    best_resolution = str(picked.get("resolution") or "none")
    best_cell = (
        parsed["found"]
        and picked.get("role") == "Attack"
        and best_resolution.startswith("cell")
    )

    if exact_input:
        if best_cell and len(matches) == 1:
            status = "exact"
        else:
            status = "attack-cell" if best_cell else "name+input-no-cell"
    else:
        status = "name-only"

    if len(scored) > 1:
        second = scored[1]
        if best["score"] == second["score"] and best["metric_distance"] == second["metric_distance"]:
            status = "ambiguous"

    return picked, status, parsed


def compare_character(
    cid: str,
    char_payload: dict[str, Any],
    comm_payload: dict[str, Any],
    limit: int,
    startup_tolerance: int,
    compare_raw_stun: bool,
    include_missing_reference: bool,
) -> dict[str, Any]:
    moves, by_key, by_name, by_key_condition, by_name_condition = _build_movelist_index(char_payload)
    comm_chars = (comm_payload.get("chars") or {})
    comm_moves = comm_chars.get(cid, {}).get("moves", [])

    report = {
        "cid": cid,
        "communityMoves": len(comm_moves),
        "movelistMoves": len(moves),
        "matched": 0,
        "matchedWithCell": 0,
        "unmatched": 0,
        "ambiguous": 0,
        "noCell": 0,
        "missingParsedReference": 0,
        "matchStatuses": {
            "exact": 0,
            "name-only": 0,
            "attack-cell": 0,
            "name+input-no-cell": 0,
            "ambiguous": 0,
            "missingReference": 0,
            "other": 0,
        },
        "differences": {
            "startup": 0,
            "damage": 0,
            "onBlock": 0,
            "onHit": 0,
        },
        "details": [],
    }

    if not moves:
        if include_missing_reference:
            report["unmatched"] = len(comm_moves)
        else:
            report["missingParsedReference"] = len(comm_moves)
            report["matchStatuses"]["missingReference"] = len(comm_moves)
            for comm in comm_moves[:limit]:
                report["details"].append(
                    {
                        "status": "missingReference",
                        "community": {
                            "name": comm.get("name"),
                            "input": comm.get("command"),
                            "startup": comm.get("startup"),
                            "damage": comm.get("damage"),
                            "block": comm.get("block"),
                            "hit": comm.get("hit"),
                            "stance": comm.get("stance"),
                        },
                        "parsed": None,
                        "diffs": {},
                    }
                )
        return report

    for comm in comm_moves:
        cname = str(comm.get("name") or "")
        cname_norm = norm_name(cname)
        cinput_norm = norm_input_key(comm.get("command") or "")
        ccommand_tokens = _tokenize_command(comm.get("command"))
        ccondition = comm.get("stance") or comm.get("condition") or ""
        community_condition_terms = _normalize_community_condition(ccondition)
        community_condition_key = "|".join(community_condition_terms)

        matches = by_key.get((cname_norm, cinput_norm), [])
        exact_input = bool(matches)
        if not matches and ccondition:
            matches = by_key_condition.get(
                (cname_norm, cinput_norm, community_condition_key),
                [],
            )
            if matches:
                exact_input = True

        status = "unmatched"
        if not matches:
            # Fallback: same name, any input.
            matches = by_name.get(cname_norm, [])
            if ccommand_tokens:
                token_matches = [
                    rec
                    for rec in matches
                    if _commands_match(
                        ccommand_tokens,
                        tuple(rec.get("commandTokens") or ()),
                    )
                ]
                matches = token_matches
            if not matches and ccondition:
                matches = by_name_condition.get(
                    (cname_norm, community_condition_key),
                    [],
                )
            if matches:
                status = "name-only"

        # Re-normalize with candidate context when available, to allow
        # single-value consensus inference when the shorthand is unknown.
        if not community_condition_terms:
            community_condition_terms = _normalize_community_condition(ccondition, matches)
            community_condition_key = "|".join(community_condition_terms)

        if matches:
            picked, pick_kind, parsed = _pick_records(
                matches,
                comm,
                community_condition_terms,
                status != "name-only" and exact_input,
                startup_tolerance=startup_tolerance,
                compare_raw_stun=compare_raw_stun,
            )
            status = pick_kind
            report["matched"] += 1
            report["matchStatuses"][status] = report["matchStatuses"].get(status, 0) + 1

            if parsed["found"]:
                report["matchedWithCell"] += 1
            else:
                report["noCell"] += 1

            if status == "ambiguous":
                report["ambiguous"] += 1

            diffs = _summarize_diff(comm, parsed, startup_tolerance, compare_raw_stun)
            changed = [
                k for k in report["differences"] if k in diffs and diffs[k]["equal"] is False
            ]
            for key in changed:
                report["differences"][key] += 1

            if changed and (len(report["details"]) < limit):
                report["details"].append(
                    {
                        "status": status,
                        "community": {
                            "name": cname,
                            "input": comm.get("command"),
                            "startup": comm.get("startup"),
                            "damage": comm.get("damage"),
                            "block": comm.get("block"),
                            "hit": comm.get("hit"),
                            "stance": comm.get("stance"),
                        },
                        "parsed": {
                            "name": picked.get("name"),
                            "input": picked.get("input"),
                            "condition": picked.get("condition"),
                            "moveId": picked.get("moveId"),
                            "resolution": picked.get("resolution"),
                            "commandSetIndex": picked.get("commandSetIndex"),
                            "commandSetCount": picked.get("commandSetCount"),
                            "cellIdx": picked.get("cellIdx"),
                            "slotIdx": picked.get("slotIdx"),
                            "metrics": {
                                "startup": parsed.get("startup"),
                                "damage": parsed.get("damage"),
                                "onBlock": parsed.get("onBlock"),
                                "onHit": parsed.get("onHit"),
                                "activeFrames": parsed.get("activeFrames"),
                            },
                            "candidateCount": parsed.get("candidateCount"),
                            "candidateBestRank": parsed.get("candidateBestRank"),
                            "candidateScore": parsed.get("candidateScore"),
                        },
                        "diffs": diffs,
                    }
                )
        else:
            report["unmatched"] += 1
            if len(report["details"]) < limit:
                report["details"].append(
                    {
                        "status": status,
                        "community": {
                            "name": cname,
                            "input": comm.get("command"),
                            "startup": comm.get("startup"),
                            "damage": comm.get("damage"),
                            "block": comm.get("block"),
                            "hit": comm.get("hit"),
                            "stance": comm.get("stance"),
                        },
                        "parsed": None,
                        "diffs": {},
                    }
                )

    return report


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--data-dir", default=str(DEFAULT_DATA_DIR), help="Directory containing webui/public/data")
    p.add_argument("--community-json", help="Optional parsed community frame-data JSON")
    p.add_argument("--community-xlsx", help="Optional downloaded community frame-data spreadsheet")
    p.add_argument("--cid", action="append", help="CID(s) to compare (default: all with both sides)")
    p.add_argument(
        "--limit", type=int, default=60, help="Number of mismatch examples to keep per CID"
    )
    p.add_argument("--startup-tolerance", type=int, default=0, help="Startup tolerance when matching metrics")
    p.add_argument("--compare-on-block-raw", action="store_true", help="Compare parsed on-block raw stun")
    p.add_argument("--compare-on-hit-raw", action="store_true", help="Compare parsed on-hit raw stun")
    p.add_argument(
        "--include-missing-reference",
        action="store_true",
        help="Treat characters without parsed move lists as regular unmatched rows",
    )
    p.add_argument("--json", action="store_true", help="Emit JSON summary")
    args = p.parse_args()

    data_dir = Path(args.data_dir).resolve()
    roster_path = data_dir / "roster.json"
    chars_dir = data_dir / "chars"

    if not roster_path.exists():
        raise SystemExit(f"Missing roster file: {roster_path}")
    if not chars_dir.exists():
        raise SystemExit(f"Missing chars directory: {chars_dir}")

    roster = _read_json(roster_path)
    cids = [str(c["cid"]) for c in roster.get("chars", [])]
    if args.cid:
        cids = [c for c in args.cid if c in cids]

    comm = load_community(
        json_path=args.community_json or str(ROOT / "community_framedata.json"),
        xlsx_path=args.community_xlsx or str(ROOT / "community_framedata.xlsx"),
    )
    compare_raw_stun = args.compare_on_block_raw or args.compare_on_hit_raw

    if not comm.get("chars"):
        empty_totals = {
            "chars": 0,
            "communityMoves": 0,
            "movelistMoves": 0,
            "matched": 0,
            "matchedWithCell": 0,
            "unmatched": 0,
            "ambiguous": 0,
            "noCell": 0,
            "missingReference": 0,
            "diff": {"startup": 0, "damage": 0, "onBlock": 0, "onHit": 0},
        }
        if args.json:
            print(json.dumps({"totals": empty_totals, "characters": []}, indent=2))
        else:
            print("No community frame data supplied; pass --community-xlsx or --community-json.")
        return 0

    all_reports = []
    global_totals = {
        "chars": 0,
        "communityMoves": 0,
        "movelistMoves": 0,
        "matched": 0,
        "matchedWithCell": 0,
        "unmatched": 0,
        "ambiguous": 0,
        "noCell": 0,
        "missingReference": 0,
        "diff": {"startup": 0, "damage": 0, "onBlock": 0, "onHit": 0},
    }

    for cid in cids:
        payload_path = chars_dir / f"{cid}.json"
        if not payload_path.exists():
            continue
        payload = _read_json(payload_path)
        rep = compare_character(
            cid,
            payload,
            comm,
            args.limit,
            startup_tolerance=args.startup_tolerance,
            compare_raw_stun=compare_raw_stun,
            include_missing_reference=args.include_missing_reference,
        )
        all_reports.append(rep)
        global_totals["chars"] += 1
        global_totals["communityMoves"] += rep["communityMoves"]
        global_totals["movelistMoves"] += rep["movelistMoves"]
        global_totals["matched"] += rep["matched"]
        global_totals["matchedWithCell"] += rep["matchedWithCell"]
        global_totals["unmatched"] += rep["unmatched"]
        global_totals["ambiguous"] += rep["ambiguous"]
        global_totals["noCell"] += rep["noCell"]
        global_totals["missingReference"] += rep["missingParsedReference"]
        for key in global_totals["diff"]:
            global_totals["diff"][key] += rep["differences"][key]

    if args.json:
        print(
            json.dumps(
                {
                    "totals": global_totals,
                    "characters": all_reports,
                },
                ensure_ascii=False,
                indent=2,
            )
        )
        return 0

    print("Community vs parsed comparison")
    print(
        f"chars: {global_totals['chars']} | community moves: {global_totals['communityMoves']} | "
        f"movelist moves: {global_totals['movelistMoves']}"
    )
    print(
        f"matched: {global_totals['matched']} (with cell: {global_totals['matchedWithCell']}), "
        f"unmatched: {global_totals['unmatched']}, ambiguous: {global_totals['ambiguous']}, "
        f"no-cell: {global_totals['noCell']}, missing-parse-reference: {global_totals['missingReference']}"
    )
    print("differences:")
    print(f"  startup: {global_totals['diff']['startup']}")
    print(f"  damage : {global_totals['diff']['damage']}")
    print(f"  onBlock: {global_totals['diff']['onBlock']}")
    print(f"  onHit  : {global_totals['diff']['onHit']}")
    print("match status:")
    for status in (
        "exact",
        "name-only",
        "attack-cell",
        "name+input-no-cell",
        "ambiguous",
        "missingReference",
        "other",
    ):
        total = sum(r["matchStatuses"].get(status, 0) for r in all_reports)
        if total:
            print(f"  {status}: {total}")

    for rep in all_reports:
        mismatch = rep["unmatched"] + sum(rep["differences"].values()) + rep["missingParsedReference"]
        if mismatch == 0:
            continue
        cmeta = comm.get("chars", {}).get(rep["cid"], {})
        cname = cmeta.get("communityName", rep["cid"])
        print(f"\n[{rep['cid']}] {cname}")
        print(
            "  moves: community="
            f"{rep['communityMoves']} matched={rep['matched']} unmatched={rep['unmatched']} "
            f"with-cell={rep['matchedWithCell']} ambiguous={rep['ambiguous']} no-cell={rep['noCell']} "
            f"missing-reference={rep['missingParsedReference']}"
        )
        for item in rep["details"]:
            st = item["status"]
            if st == "unmatched" or st == "missingReference":
                print(
                    f"  - {st}: {item['community']['name']} / {item['community']['input']}"
                )
                continue
            if not item["diffs"]:
                continue
            dif = item["diffs"]
            parts = [
                f"{k}:{v['community']} vs {v['parsed']} (delta {v['delta']})"
                for k, v in dif.items()
                if v["equal"] is False
            ]
            if not parts:
                continue
            p_name = item["parsed"]["name"] if item.get("parsed") else "-"
            p_input = item["parsed"]["input"] if item.get("parsed") else "-"
            print(
                f"  - {st} {item['community']['name']} / {item['community']['input']} -> "
                f"{p_name} / {p_input}: {', '.join(parts)}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
