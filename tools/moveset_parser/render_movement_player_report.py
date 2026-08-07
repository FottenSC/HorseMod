#!/usr/bin/env python3
"""Render the player-readable SC6 movement report from generated evidence."""

from __future__ import annotations

import argparse
import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MOVEMENT_LABELS = {
    "backstep_candidate": "Backstep",
    "sidestep_up_candidate": "Sidestep up-side",
    "sidestep_down_candidate": "Sidestep down-side",
    "forward_step_candidate": "Forward movement",
}

TRUST_LABELS = {
    "trusted_basic": "Trusted basic",
    "trusted_basic_with_late_followup": "Trusted late follow-up",
    "trusted_stance_basic": "Trusted stance movement",
    "measured_but_not_basic": "Measured, route unclear",
    "attack_or_special": "Special or attack movement",
    "unresolved": "Unknown",
}

FORBIDDEN_MAIN_TERMS = (
    "candidate found",
    "payload",
    "byte",
    "mot_sha1",
    "animation_hex",
    "dst_slot",
    "src_slot",
)


@dataclass
class AuditRow:
    section: str
    character: str
    movement_type: str
    field: str
    csv_value: str
    rendered_value: str
    status: str


def _read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open("r", newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def _read_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def _num(value: Any) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _fmt(value: Any) -> str:
    n = _num(value)
    if n is None:
        return "-"
    return f"{n:.3f}"


def _intfmt(value: Any) -> str:
    n = _num(value)
    if n is None:
        return "-"
    return str(int(n))


def _audit(
    audit_rows: list[AuditRow],
    *,
    section: str,
    row: dict[str, str],
    movement_type: str,
    field: str,
    rendered: str,
) -> None:
    csv_value = row.get(field, "")
    expected = _fmt(csv_value) if field.startswith("distance_") or field == "total_distance" else str(csv_value)
    audit_rows.append(
        AuditRow(
            section=section,
            character=row.get("character", ""),
            movement_type=movement_type,
            field=field,
            csv_value=expected,
            rendered_value=rendered,
            status="match" if expected == rendered else "mismatch",
        )
    )


def _player_reason(row: dict[str, str], movement_type: str) -> str:
    trust = row.get("trust_status", "")
    if trust == "trusted_basic":
        if movement_type == "backstep_candidate":
            f8 = _num(row.get("distance_f8")) or 0.0
            total = _num(row.get("total_distance")) or 0.0
            if f8 >= 0.45:
                return "Creates space early, which is the part most likely to make fast attacks miss."
            if total >= 0.75:
                return "The space gain builds later, so it is more useful for resetting range than beating fast active frames."
            if total <= 0.1:
                return "Trusted route, but this selected motion barely retreats."
            return "Clean route with modest retreat."
        if movement_type.startswith("sidestep"):
            return "Clean side movement; compare the early value to judge evasiveness."
        return "Clean route; frame 8 shows how quickly it gains ground."
    if trust == "trusted_stance_basic":
        return "Measured as stance movement, not universal neutral movement."
    if trust == "trusted_basic_with_late_followup":
        return "Looks like movement first, but the offensive follow-up is still kept out of rankings."
    if trust == "measured_but_not_basic":
        return "The movement curve decodes, but the source state is not proven as the normal movement state."
    if trust == "attack_or_special":
        return "The route has offensive behavior during the movement window."
    raw_reason = (row.get("caveat") or row.get("trust_reason") or "").lower()
    if "invalid active window" in raw_reason:
        return "The route points at attack timing with an invalid time window, so we cannot prove when movement ends."
    if "offensive cells exist" in raw_reason or "offensive cell" in raw_reason:
        return "The route includes attack timing, and static recovery is not proven yet."
    if "cross-bank" in raw_reason:
        return "The route leaves the local move table, and that target bank is not proven yet."
    return "Static evidence does not yet prove this as basic movement."


def _sort_rows(rows: list[dict[str, str]], key_field: str = "distance_f8") -> list[dict[str, str]]:
    return sorted(
        rows,
        key=lambda r: (
            0 if r.get("trust_status") == "trusted_basic" else 1,
            -(_num(r.get(key_field)) or -999.0),
            r.get("character", ""),
        ),
    )


def _render_backsteps(
    basic_rows: list[dict[str, str]],
    backstep_quality: list[dict[str, str]],
    audit_rows: list[AuditRow],
) -> list[str]:
    lines: list[str] = []
    quality_by_char = {row["character"]: row for row in backstep_quality}
    rows = [r for r in basic_rows if r.get("movement_type") == "backstep_candidate"]
    trusted = [r for r in rows if r.get("trust_status") in {"trusted_basic", "trusted_stance_basic"}]
    other = [r for r in rows if r not in trusted]

    lines.append("## Trusted Basic Backsteps")
    lines.append("")
    lines.append(
        "Frame 4 and frame 8 matter most for making attacks miss. Total retreat matters for resetting range, but late movement is less valuable if the attack is already active."
    )
    lines.append("")
    lines.append("| Character | Grade | Frame 4 | Frame 8 | Frame 12 | Frame 16 | Total | Player read |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---|")
    for row in _sort_rows(trusted):
        q = quality_by_char.get(row["character"], {})
        grade = q.get("quality_grade") or "Unranked"
        f4 = _fmt(row.get("distance_f4"))
        f8 = _fmt(row.get("distance_f8"))
        f12 = _fmt(row.get("distance_f12"))
        f16 = _fmt(row.get("distance_f16"))
        total = _fmt(row.get("total_distance"))
        lines.append(
            f"| {row['character']} | {grade} | {f4} | {f8} | {f12} | {f16} | {total} | {_player_reason(row, 'backstep_candidate')} |"
        )
        for field, rendered in {
            "distance_f4": f4,
            "distance_f8": f8,
            "distance_f12": f12,
            "distance_f16": f16,
            "total_distance": total,
        }.items():
            _audit(audit_rows, section="trusted_basic_backsteps", row=row, movement_type="backstep_candidate", field=field, rendered=rendered)

    lines.append("")
    lines.append("## Backsteps We Can Measure But Not Rank Yet")
    lines.append("")
    lines.append(
        "These are not a tier list. They stay out of ranking because the route is not proven as plain neutral movement, or recovery and offensive timing are not proven enough."
    )
    lines.append("")
    lines.append("| Character | Label | Frame 8 | Frame 16 | Why it is not ranked |")
    lines.append("|---|---|---:|---:|---|")
    for row in _sort_rows(other):
        f8 = _fmt(row.get("distance_f8"))
        f16 = _fmt(row.get("distance_f16"))
        label = TRUST_LABELS.get(row.get("trust_status", ""), row.get("trust_status", "Unknown"))
        lines.append(f"| {row['character']} | {label} | {f8} | {f16} | {_player_reason(row, 'backstep_candidate')} |")
        for field, rendered in {"distance_f8": f8, "distance_f16": f16}.items():
            _audit(audit_rows, section="unranked_backsteps", row=row, movement_type="backstep_candidate", field=field, rendered=rendered)
    lines.append("")
    return lines


def _render_movement_table(
    *,
    title: str,
    movement_types: set[str],
    rows: list[dict[str, str]],
    audit_rows: list[AuditRow],
) -> list[str]:
    selected = [r for r in rows if r.get("movement_type") in movement_types]
    trusted = [r for r in selected if r.get("trust_status") in {"trusted_basic", "trusted_stance_basic"}]
    unresolved = [r for r in selected if r not in trusted]
    trusted = _sort_rows(trusted)
    unresolved = _sort_rows(unresolved)

    lines = [f"## {title}", ""]
    if trusted:
        lines.append("| Character | Route | Label | Frame 8 | Frame 16 | Total | Player read |")
        lines.append("|---|---|---|---:|---:|---:|---|")
        for row in trusted:
            f8 = _fmt(row.get("distance_f8"))
            f16 = _fmt(row.get("distance_f16"))
            total = _fmt(row.get("total_distance"))
            route = MOVEMENT_LABELS.get(row.get("movement_type", ""), row.get("movement_type", "Movement"))
            label = TRUST_LABELS.get(row.get("trust_status", ""), row.get("trust_status", "Unknown"))
            lines.append(
                f"| {row['character']} | {route} | {label} | {f8} | {f16} | {total} | {_player_reason(row, row.get('movement_type', ''))} |"
            )
            for field, rendered in {"distance_f8": f8, "distance_f16": f16, "total_distance": total}.items():
                _audit(audit_rows, section=title.lower().replace(" ", "_"), row=row, movement_type=row.get("movement_type", ""), field=field, rendered=rendered)
    else:
        lines.append("No route in this group is trusted enough for ranking yet.")
    lines.append("")
    if unresolved:
        unknown_count = sum(1 for r in unresolved if r.get("trust_status") == "unresolved")
        measured_count = sum(1 for r in unresolved if r.get("trust_status") == "measured_but_not_basic")
        lines.append(
            f"Not ranked in this group: {len(unresolved)} rows ({unknown_count} unknown, {measured_count} measured but route unclear)."
        )
        lines.append("")
        lines.append("| Character | Route | Label | Frame 8 | Frame 16 | Why it is not ranked |")
        lines.append("|---|---|---|---:|---:|---|")
        for row in unresolved:
            f8 = _fmt(row.get("distance_f8"))
            f16 = _fmt(row.get("distance_f16"))
            route = MOVEMENT_LABELS.get(row.get("movement_type", ""), row.get("movement_type", "Movement"))
            label = TRUST_LABELS.get(row.get("trust_status", ""), row.get("trust_status", "Unknown"))
            lines.append(
                f"| {row['character']} | {route} | {label} | {f8} | {f16} | {_player_reason(row, row.get('movement_type', ''))} |"
            )
            for field, rendered in {"distance_f8": f8, "distance_f16": f16}.items():
                _audit(audit_rows, section=title.lower().replace(" ", "_") + "_unranked", row=row, movement_type=row.get("movement_type", ""), field=field, rendered=rendered)
    lines.append("")
    return lines


def render(generated: Path, out: Path) -> list[AuditRow]:
    summary = _read_json(generated / "movement_static_summary.json")
    basic_rows = _read_csv(generated / "basic_movement_routes.csv")
    movement_quality = _read_csv(generated / "movement_quality_static.csv")
    backstep_quality = _read_csv(generated / "backstep_quality_static.csv")

    audit_rows: list[AuditRow] = []
    totals = summary.get("static_totals", {})
    movement_summary = summary.get("movement_quality_summary", {})
    trusted_count = movement_summary.get("trusted_basic_route_count", 0)
    ranked_count = movement_summary.get("ranked_count", 0)
    cross_bank = totals.get("cross_bank_direction_edge_count", 0)
    cross_bank_resolved = totals.get("cross_bank_resolved_count", 0)

    lines: list[str] = [
        "# SC6 movement strength - player-readable report",
        "",
        "Latest static analyzer run: 2026-05-23.",
        "",
        "## Short Answer",
        "",
        (
            "The analyzer now measures authored root movement from the game files and shows every character, "
            "but it only ranks movement when the static route is trusted as basic movement."
        ),
        "",
        (
            f"Current trusted-and-ranked movement rows: {ranked_count}. "
            f"Current trusted selected routes: {trusted_count}. "
            f"Internal move-bank bucket links audited: {cross_bank}; resolved bucket links: {cross_bank_resolved}."
        ),
        "",
        (
            "For backsteps, the trusted group is still small. Xianghua has the strongest early retreat in that group, "
            "Sophitia creates the most total space, and Siegfried/Nightmare share the same clean retreat curve."
        ),
        "",
        "## What The Labels Mean",
        "",
        "Trusted basic means direct movement from a neutral-like state with decoded movement and no active offensive timing on the route.",
        "",
        "Trusted stance movement means the movement is trusted inside a stance or mode, not as universal neutral movement.",
        "",
        "Measured, route unclear means the movement curve decodes, but the route source is not proven as the normal movement state.",
        "",
        "Unknown means the static data does not yet prove whether this is basic movement, usually because recovery or offensive timing is unresolved.",
        "",
        "Special or attack movement means the route has offensive behavior during the movement window.",
        "",
    ]

    lines.extend(_render_backsteps(basic_rows, backstep_quality, audit_rows))
    lines.extend(
        _render_movement_table(
            title="Sidestep Read",
            movement_types={"sidestep_up_candidate", "sidestep_down_candidate"},
            rows=basic_rows,
            audit_rows=audit_rows,
        )
    )
    lines.extend(
        _render_movement_table(
            title="Forward Movement Read",
            movement_types={"forward_step_candidate"},
            rows=basic_rows,
            audit_rows=audit_rows,
        )
    )
    lines.extend(
        [
            "## How To Use This",
            "",
            "If you are asking who escapes fastest, read frame 4 and frame 8 first.",
            "",
            "If you are asking who creates the most space, read total distance, but remember that late movement may not save you from fast active frames.",
            "",
            "If a character is unknown, do not read that as bad movement. It means the movement is mixed with state, recovery, or offensive timing that needs stronger proof before ranking.",
            "",
            "## Why Distance Is Not The Whole Answer",
            "",
            "The curve tells us how much space the route tries to create. It does not prove a real whiff by itself.",
            "",
            "Practical evasiveness also depends on hurtbox pose, the attacker's reach, body collision with the opponent, pushback, walls, ring edge, terrain, and when the defender can block or punish.",
            "",
            "## Information We Do Not Currently Have",
            "",
            f"We do not yet have unresolved internal move-bank bucket routing; {cross_bank_resolved} of {cross_bank} audited bucket links are resolved.",
            "",
            f"We do not yet have confirmed static recovery for {totals.get('recovery_unknown_count', 0)} selected movement rows.",
            "",
            "We do not yet have complete guard, cancel, and punish timing for every movement route.",
            "",
            "We do not yet have complete left-versus-right sidestep trust for the whole cast.",
            "",
            "We do not yet have hurtbox movement over time during every movement option.",
            "",
            "We do not yet have wall, ring edge, terrain, and opponent collision adjustment modeled statically.",
            "",
            "We do not yet have matchup-specific whiff results against common attacks.",
            "",
        ]
    )

    text = "\n".join(lines)
    lowered = text.lower()
    found = [term for term in FORBIDDEN_MAIN_TERMS if term in lowered]
    if found:
        raise ValueError(f"player report contains raw analyzer terms: {', '.join(found)}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text, encoding="utf-8")
    return audit_rows


def write_audit(path: Path, rows: list[AuditRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "section",
                "character",
                "movement_type",
                "field",
                "csv_value",
                "rendered_value",
                "status",
            ],
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, default=Path("docs/investigations/generated"))
    parser.add_argument("--out", type=Path, default=Path("docs/investigations/generated/sc6-character-movement-player-readable.md"))
    args = parser.parse_args()

    audit_rows = render(args.generated, args.out)
    write_audit(args.generated / "report_value_audit.csv", audit_rows)
    mismatches = [row for row in audit_rows if row.status != "match"]
    if mismatches:
        print(f"Rendered report with {len(mismatches)} value mismatches")
        return 1
    print(f"Rendered {args.out} from {args.generated}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
