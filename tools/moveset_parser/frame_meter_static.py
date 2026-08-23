"""Generate the fail-closed, native-keyed SC6 frame-meter handoff.

This module deliberately layers on :mod:`native_frame_analysis`.  It does not
route official movelist rows and it never substitutes a heuristic value for a
missing endpoint.  Its clock is the logical MoveVM simulation tick documented
in Ghidra; shared full-freeze ticks are outside that clock.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

from locomotion_movement import CHARACTERS
from luxformats import decode_packed_slot_id, parse_khd
from native_frame_analysis import analyze_confirmed_slot_frames, analyze_throw_break_frames
from native_reaction_table import parse_hit_reaction_move_id_table


SCHEMA = "frame-meter-static-v1"
EVIDENCE_STATUS = {
    "Numeric",
    "CategoricalKnockdown",
    "CategoricalCinematic",
    "CategoricalThrowSuccess",
    "ContextDependent",
    "UnsupportedStatic",
}
REASON_CODES = {
    "route-ambiguity", "transition-predicate", "attacker-endpoint",
    "reaction-endpoint", "clock-alignment", "posture-column", "dynamic-context",
}
LOOKUP_MODE_ORDER = {
    "Block": 0, "Hit": 1, "CounterHit": 2, "ThrowBreak": 3, "ThrowSuccess": 4,
}
REACTION_CONTEXT_ORDER = {
    "GuardConditionRuntime": 0,
    "StandardGrounded": 1,
    "StandardAirOrCinematic": 2,
    "PromotedGrounded": 3,
    "PromotedAirOrCinematic": 4,
    "RuntimeClassifierRoute": 5,
    "RuntimeThrowBreak": 6,
    "RuntimeThrowSuccess": 7,
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


@dataclass(frozen=True, order=True)
class NativeFrameKey:
    style: str
    bank_kind: int
    packed_move: int
    attack_cell_index: int
    contact_mode: str
    reaction_context: str
    reaction_row_id: int
    contact_coordinate: int


def _native_sort_key(key: dict) -> tuple:
    """Return the schema-defined native-key order, independent of JSON map order."""

    return tuple(key[name] for name in NativeFrameKey.__dataclass_fields__)


def packed_ids_by_slot(khd: object) -> dict[int, tuple[int, ...]]:
    """Invert the native bank-bucket table without guessing a provider."""

    result: dict[int, list[int]] = {}
    for bank_kind, (start, count) in enumerate(khd.slot_buckets):
        for index in range(count):
            slot = start + index
            # Native resolution ignores bit 11. The dataset always emits the
            # canonical clear-bit spelling so runtime aliases cannot miss.
            packed = ((bank_kind << 12) | index) & ~0x0800
            if khd.resolve_packed_slot(packed) != slot:
                raise ValueError(
                    f"native packed-id inversion failed: bank={bank_kind} index={index}"
                )
            result.setdefault(slot, []).append(packed)
    return {slot: tuple(values) for slot, values in result.items()}


def _outcome(
    *,
    mode: str,
    authored_counter: int | None,
    categorical: str | None,
    attacker_coordinate: int | None,
    reaction_row_id: int,
) -> dict:
    if categorical == "THROW_SUCCESS":
        return {
            "status": "CategoricalThrowSuccess",
            "reasonCode": "reaction-endpoint",
            "reason": "successful throw has no single finite release endpoint",
            "defenderFirstActionableTick": None,
            "attackerFirstActionableTick": None,
            "advantage": None,
        }
    if categorical in {"KND", "LNC"}:
        return {
            "status": "CategoricalKnockdown",
            "reasonCode": "reaction-endpoint",
            "reason": f"native reaction family {categorical} has no single finite actionability endpoint",
            "defenderFirstActionableTick": None,
            "attackerFirstActionableTick": None,
            "advantage": None,
        }
    if attacker_coordinate is None:
        return {
            "status": "UnsupportedStatic",
            "reasonCode": "attacker-endpoint",
            "reason": "no audited finite transition/control coordinate for this slot",
            "defenderFirstActionableTick": None,
            "attackerFirstActionableTick": None,
            "advantage": None,
        }
    if mode in {"Hit", "CounterHit"}:
        return {
            "status": "UnsupportedStatic",
            "reasonCode": "reaction-endpoint",
            "reason": (
                f"reaction row {reaction_row_id} selection/exit is not a finite control-release proof"
            ),
            "defenderFirstActionableTick": None,
            "attackerFirstActionableTick": None,
            "advantage": None,
        }
    return {
        "status": "UnsupportedStatic",
        "reasonCode": "clock-alignment",
        "reason": (
            "authored counter/transition coordinates are known, but playback speed, "
            "time dilation, same-tick ordering, and command-release control are not closed"
        ),
        "defenderFirstActionableTick": None,
        "attackerFirstActionableTick": None,
        "advantage": None,
    }


def _record(
    *,
    cid: str,
    packed: int,
    cell_index: int,
    contact_coordinate: int,
    mode: str,
    posture: str,
    reaction_row_id: int,
    active_start: int,
    active_end: int,
    last_locked_coordinate: int | None,
    authored_defender_counter: int | None,
    outcome: dict,
    source_hash: str,
) -> dict:
    decoded = decode_packed_slot_id(packed)
    key = NativeFrameKey(
        style=cid,
        bank_kind=decoded.bank,
        packed_move=packed,
        attack_cell_index=cell_index,
        contact_mode=mode,
        reaction_context=posture,
        reaction_row_id=reaction_row_id,
        contact_coordinate=contact_coordinate,
    )
    return {
        "key": asdict(key),
        "activeIntervals": [[active_start, active_end]],
        # Records are contact-coordinate specific; the enclosing active
        # interval is retained for rectangle drawing while this singleton is
        # the exact reachable tick represented by the native key.
        "authoredContactCoordinates": [contact_coordinate],
        "lastLockedAttackerCoordinate": last_locked_coordinate,
        "authoredDefenderCounter": authored_defender_counter,
        **outcome,
        "hitstop": {
            "classification": "SharedFullFreezeExcluded",
            "sharedFullFreezeTicks": None,
            "asymmetricTicks": None,
            "reason": "advantage uses logical MoveVM ticks; authored asymmetric/per-character dilation is context-dependent",
        },
        "evidence": {
            "status": outcome["status"],
            "sourceKhdSha256": source_hash,
            "runtimeValidated": False,
        },
    }


def build_dataset(*, battle_root: Path, executable: Path) -> dict:
    """Build a deterministic corpus ledger from version-locked static inputs."""

    hdr_root = battle_root / "hdr"
    khd_paths = tuple(hdr_root / f"hdr{cid}.khd" for cid in CHARACTERS)
    missing = [str(path) for path in (*khd_paths, hdr_root / "yarare.dat", executable) if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing frame-meter evidence: " + ", ".join(missing))
    reaction_path = hdr_root / "yarare.dat"
    reaction_table = parse_hit_reaction_move_id_table(reaction_path.read_bytes())
    khds = tuple(parse_khd(path.read_bytes()) for path in khd_paths)
    khd_hashes = tuple(sha256_file(path) for path in khd_paths)
    records: list[dict] = []
    candidate_cell_references = 0
    emitted_cell_references = 0
    exclusions: Counter[str] = Counter()

    for cid, khd, khd_hash in zip(CHARACTERS, khds, khd_hashes):
        inverse = packed_ids_by_slot(khd)
        cells = khd.sections[0].entries if khd.sections else ()
        for slot_index, slot in enumerate(khd.slots):
            packed_ids = inverse.get(slot_index, ())
            if not packed_ids:
                continue
            for cell_index in sorted(set(slot.attack_cell_indices)):
                candidate_cell_references += 1
                if not 0 <= cell_index < len(cells):
                    exclusions["invalid-cell-index"] += 1
                    continue
                cell = cells[cell_index]
                is_strike = cell.cell_role == "Attack"
                is_native_throw = (
                    cell.cell_role == "NonDamaging" and cell.move_type == "Grab"
                )
                if not (is_strike or is_native_throw):
                    exclusions["unsupported-cell-kind"] += 1
                    continue
                if not cell.has_valid_active_window:
                    exclusions["invalid-active-window"] += 1
                    continue
                emitted_cell_references += 1
                proof = (
                    analyze_confirmed_slot_frames(
                        khd,
                        slot_index,
                        cell_index,
                    )
                    if is_strike
                    else analyze_throw_break_frames(khd, slot_index, cell_index)
                )
                active_start = int(cell.wI16MasterWindowStart)
                active_end = int(cell.wI16MasterWindowEnd)
                last_locked = proof.recovery_open_coordinate if proof else None
                # These are authored candidate coordinates, not proof that
                # every integer is visited. Playback speed and subwindow gates
                # can skip/suppress them and remain part of the taxonomy.
                contexts = (
                    ("Block", "GuardConditionRuntime", -1, int(cell.wI16BlockstunFrames)),
                    ("Hit", "StandardGrounded", int(cell.wI16ReactionRowGrounded), int(cell.wI16CounterStandardGrounded)),
                    ("Hit", "StandardAirOrCinematic", int(cell.wI16ReactionRowAirOrCinematic), int(cell.wI16CounterStandardAirOrCinematic)),
                    ("Hit", "PromotedGrounded", int(cell.wI16ReactionRowGrounded), int(cell.wI16CounterPromotedGrounded)),
                    ("Hit", "PromotedAirOrCinematic", int(cell.wI16ReactionRowAirOrCinematic), int(cell.wI16CounterPromotedAirOrCinematic)),
                    ("CounterHit", "RuntimeClassifierRoute", -1, None),
                ) if is_strike else (
                    ("ThrowBreak", "RuntimeThrowBreak", int(cell.wI16ThrowReactionRowId), int(cell.wI16BlockstunFrames)),
                    ("ThrowSuccess", "RuntimeThrowSuccess", int(cell.wI16ThrowReactionRowId), None),
                )
                for packed in packed_ids:
                    for contact_coordinate in range(active_start, active_end + 1):
                        for mode, context, reaction_row, authored_counter in contexts:
                            outcome = _outcome(
                                mode=mode,
                                authored_counter=authored_counter,
                                categorical=("THROW_SUCCESS" if mode == "ThrowSuccess" else None),
                                attacker_coordinate=last_locked,
                                reaction_row_id=reaction_row,
                            )
                            records.append(_record(
                                cid=cid,
                                packed=packed,
                                cell_index=cell_index,
                                contact_coordinate=contact_coordinate,
                                mode=mode,
                                posture=context,
                                reaction_row_id=reaction_row,
                                active_start=active_start,
                                active_end=active_end,
                                last_locked_coordinate=last_locked,
                                authored_defender_counter=(
                                    authored_counter if authored_counter is not None and authored_counter >= 0 else None
                                ),
                                outcome=outcome,
                                source_hash=khd_hash,
                            ))

    records.sort(key=lambda row: _native_sort_key(row["key"]))
    status_counts = Counter(row["status"] for row in records)
    reason_counts = Counter(
        row["reasonCode"] for row in records if row["reasonCode"] is not None
    )
    dataset = {
        "schema": SCHEMA,
        "certification": "StaticOnlyRuntimeUnvalidated",
        "clock": {
            "domain": "unresolved coordinate-to-logical-tick mapping",
            "contactTickZero": "not certified",
            "advantageFormula": "withheld until both command-release endpoints share a proven simulation clock",
            "sharedFullFreeze": "exported separately; asymmetric/per-character scaling remains context-dependent",
        },
        "sources": {
            "executable": {
                "path": str(executable.resolve()),
                "sha256": sha256_file(executable),
            },
            "reactionTable": {
                "path": str(reaction_path.resolve()),
                "sha256": sha256_file(reaction_path),
            },
            "khd": [
                {"style": cid, "path": str(path.resolve()), "sha256": digest}
                for cid, path, digest in zip(CHARACTERS, khd_paths, khd_hashes)
            ],
        },
        "coverage": {
            "recordCount": len(records),
            "candidateCellReferenceCount": candidate_cell_references,
            "emittedCellReferenceCount": emitted_cell_references,
            "excludedCellReferenceCount": sum(exclusions.values()),
            "exclusionReasonCounts": dict(sorted(exclusions.items())),
            "statusCounts": dict(sorted(status_counts.items())),
            "unresolvedReasonCounts": dict(sorted(reason_counts.items())),
            "silentFallbackCount": candidate_cell_references - emitted_cell_references - sum(exclusions.values()),
            "genericUnknownCount": 0,
        },
        "records": records,
    }
    validate_dataset(dataset)
    return dataset


def validate_dataset(dataset: dict) -> None:
    if dataset.get("schema") != SCHEMA:
        raise ValueError("wrong frame-meter schema")
    records = dataset.get("records")
    if not isinstance(records, list):
        raise ValueError("records must be a list")
    coverage = dataset.get("coverage", {})
    if coverage and (
        coverage.get("candidateCellReferenceCount")
        != coverage.get("emittedCellReferenceCount", 0)
        + coverage.get("excludedCellReferenceCount", 0)
    ):
        raise ValueError("coverage candidate reconciliation failed")
    if coverage and coverage.get("silentFallbackCount") != 0:
        raise ValueError("coverage contains silent fallbacks")
    exclusion_counts = coverage.get("exclusionReasonCounts", {})
    if sum(exclusion_counts.values()) != coverage.get("excludedCellReferenceCount"):
        raise ValueError("coverage exclusion-reason reconciliation failed")
    actual_status_counts = dict(sorted(Counter(row.get("status") for row in records).items()))
    actual_reason_counts = dict(sorted(Counter(
        row.get("reasonCode") for row in records if row.get("reasonCode") is not None
    ).items()))
    if coverage.get("recordCount") != len(records):
        raise ValueError("coverage record count does not match records")
    if coverage.get("statusCounts") != actual_status_counts:
        raise ValueError("coverage status counts do not match records")
    if coverage.get("unresolvedReasonCounts") != actual_reason_counts:
        raise ValueError("coverage reason counts do not match records")
    if coverage.get("genericUnknownCount") != 0:
        raise ValueError("coverage contains generic unknown results")
    previous: tuple | None = None
    for index, row in enumerate(records):
        key = row.get("key")
        if not isinstance(key, dict) or set(key) != set(NativeFrameKey.__dataclass_fields__):
            raise ValueError(f"record {index} has an invalid native key")
        sort_key = _native_sort_key(key)
        if previous is not None and sort_key <= previous:
            raise ValueError(f"record {index} is duplicate or nondeterministically ordered")
        previous = sort_key
        status = row.get("status")
        if status not in EVIDENCE_STATUS:
            raise ValueError(f"record {index} has unclassified status {status!r}")
        numeric = status == "Numeric"
        if numeric != all(row.get(name) is not None for name in (
            "attackerFirstActionableTick",
            "defenderFirstActionableTick",
            "advantage",
        )):
            raise ValueError(f"record {index} violates finite-endpoint invariants")
        if numeric and row["advantage"] != (
            row["defenderFirstActionableTick"] - row["attackerFirstActionableTick"]
        ):
            raise ValueError(f"record {index} has a fabricated advantage")
        if not numeric and row.get("reasonCode") is None:
            raise ValueError(f"record {index} lacks a concrete nonnumeric reason")
        if numeric and row.get("reasonCode") is not None:
            raise ValueError(f"record {index} gives a numeric endpoint an unresolved reason")
        if row.get("reasonCode") is not None and row["reasonCode"] not in REASON_CODES:
            raise ValueError(f"record {index} has invalid reason code")
        if row.get("evidence", {}).get("status") != status:
            raise ValueError(f"record {index} evidence status disagrees with result")
        if key["packed_move"] & 0x0800:
            raise ValueError(f"record {index} retains ignored packed-move bit 11")
        if row.get("authoredContactCoordinates") != [key["contact_coordinate"]]:
            raise ValueError(f"record {index} does not represent exactly one authored coordinate")


def write_json(dataset: dict, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(dataset, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )


def _cpp_status(status: str) -> str:
    return {
        "Numeric": "Numeric",
        "CategoricalKnockdown": "Knockdown",
        "CategoricalCinematic": "Cinematic",
        "CategoricalThrowSuccess": "ThrowSuccess",
        "ContextDependent": "ContextDependent",
        "UnsupportedStatic": "UnsupportedStatic",
    }[status]


def write_cpp_table(dataset: dict, path: Path) -> None:
    """Write a compact lookup table; contact-coordinate arithmetic stays live."""

    compact: dict[tuple, dict] = {}
    for row in dataset["records"]:
        key = row["key"]
        compact_key = (
            key["style"], key["bank_kind"], key["packed_move"],
            key["attack_cell_index"], key["contact_mode"],
            key["reaction_context"], key["reaction_row_id"],
        )
        retained = {
            name: row[name] for name in (
                "activeIntervals", "lastLockedAttackerCoordinate",
                "authoredDefenderCounter", "attackerFirstActionableTick",
                "defenderFirstActionableTick", "advantage", "status",
                "reasonCode", "hitstop", "evidence",
            )
        }
        previous = compact.get(compact_key)
        if previous is not None:
            if retained != {name: previous[name] for name in retained}:
                raise ValueError(
                    "contact-coordinate compaction would discard conflicting semantics: "
                    f"{compact_key}"
                )
        else:
            compact[compact_key] = row
    lines = [
        "// Generated by tools/moveset_parser/frame_meter_static.py; do not edit.",
        "// Static-only evidence. Runtime pointer chains remain unvalidated.",
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "namespace HorseMod::FrameMeterStatic {",
        f"inline constexpr std::string_view kSchema = \"{SCHEMA}\";",
        f"inline constexpr std::string_view kExecutableSha256 = \"{dataset['sources']['executable']['sha256']}\";",
        f"inline constexpr std::string_view kReactionTableSha256 = \"{dataset['sources']['reactionTable']['sha256']}\";",
        "enum class EndpointStatus : std::uint8_t { Numeric, Knockdown, Cinematic, ThrowSuccess, ContextDependent, UnsupportedStatic };",
        "enum class StaticLookupMode : std::uint8_t { Block, Hit, CounterHit, ThrowBreak, ThrowSuccess };",
        "enum class ReactionContext : std::uint8_t { GuardConditionRuntime, StandardGrounded, StandardAirOrCinematic, PromotedGrounded, PromotedAirOrCinematic, RuntimeClassifierRoute, RuntimeThrowBreak, RuntimeThrowSuccess };",
        "struct SourceHash { std::uint16_t style; std::string_view sha256; };",
        f"inline constexpr std::array<SourceHash, {len(dataset['sources']['khd'])}> kStyleSourceHashes{{{{",
    ]
    lines.extend(
        f"    SourceHash{{0x{int(source['style'], 16):04X}, \"{source['sha256']}\"}},"
        for source in dataset["sources"]["khd"]
    )
    lines.extend([
        "}};",
        "struct Entry { std::uint16_t style; std::uint8_t bank; std::uint16_t move; std::uint16_t cell; StaticLookupMode mode; ReactionContext context; std::int16_t reaction; std::int16_t activeStart; std::int16_t activeEnd; std::int16_t lastLockedCoordinate; std::int16_t authoredCounter; EndpointStatus status; };",
        "// Sorted by Entry key fields using the numeric enum encodings above; safe for matching binary search.",
        f"inline constexpr std::array<Entry, {len(compact)}> kEntries{{{{",
    ])
    def encoded_key(item: tuple[tuple, dict]) -> tuple:
        style, bank, move, cell, mode, context, reaction = item[0]
        return (
            int(style, 16), bank, move, cell, LOOKUP_MODE_ORDER[mode],
            REACTION_CONTEXT_ORDER[context], reaction,
        )

    for key, row in sorted(compact.items(), key=encoded_key):
        style, bank, move, cell, mode, posture, reaction = key
        lines.append(
            "    Entry{" + ", ".join((
                f"0x{int(style, 16):04X}", str(bank), f"0x{move:04X}", str(cell),
                f"StaticLookupMode::{mode}", f"ReactionContext::{posture}", str(reaction),
                str(row["activeIntervals"][0][0]), str(row["activeIntervals"][0][1]),
                str(row["lastLockedAttackerCoordinate"] if row["lastLockedAttackerCoordinate"] is not None else -1),
                str(row["authoredDefenderCounter"] if row["authoredDefenderCounter"] is not None else -1),
                f"EndpointStatus::{_cpp_status(row['status'])}",
            )) + "},"
        )
    lines.extend(("}};", "} // namespace HorseMod::FrameMeterStatic", ""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--battle-root", type=Path, default=Path("dump/Battle"))
    parser.add_argument(
        "--executable",
        type=Path,
        default=Path(r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"),
    )
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--cpp", type=Path, required=True)
    args = parser.parse_args(list(argv) if argv is not None else None)
    dataset = build_dataset(battle_root=args.battle_root, executable=args.executable)
    write_json(dataset, args.json)
    write_cpp_table(dataset, args.cpp)
    print(json.dumps(dataset["coverage"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
