from __future__ import annotations

import csv
import json
from pathlib import Path

from render_movement_player_report import FORBIDDEN_MAIN_TERMS, render, write_audit


def _write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames: list[str] = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def test_player_report_is_rendered_from_generated_values(tmp_path: Path):
    generated = tmp_path / "generated"
    generated.mkdir()
    summary = {
        "static_totals": {
            "cross_bank_direction_edge_count": 2,
            "cross_bank_resolved_count": 0,
            "recovery_unknown_count": 1,
        },
        "movement_quality_summary": {
            "trusted_basic_route_count": 1,
            "ranked_count": 1,
        },
    }
    (generated / "movement_static_summary.json").write_text(json.dumps(summary), encoding="utf-8")
    basic_rows = [
        {
            "cid": "001",
            "character": "Mitsurugi",
            "movement_type": "backstep_candidate",
            "trust_status": "trusted_basic",
            "distance_f4": "0.027",
            "distance_f8": "0.048",
            "distance_f12": "0.127",
            "distance_f16": "0.167",
            "total_distance": "0.180",
            "trust_reason": "test",
            "caveat": "",
        },
        {
            "cid": "002",
            "character": "Seong Mi-na",
            "movement_type": "backstep_candidate",
            "trust_status": "unresolved",
            "distance_f4": "0.100",
            "distance_f8": "0.452",
            "distance_f12": "0.700",
            "distance_f16": "0.918",
            "total_distance": "",
            "trust_reason": "offensive cells exist and no static return/recovery frame is known",
            "caveat": "offensive cells exist and no static return/recovery frame is known",
        },
        {
            "cid": "001",
            "character": "Mitsurugi",
            "movement_type": "sidestep_down_candidate",
            "trust_status": "trusted_basic",
            "distance_f8": "0.191",
            "distance_f16": "0.190",
            "total_distance": "0.197",
            "trust_reason": "test",
            "caveat": "",
        },
    ]
    _write_csv(generated / "basic_movement_routes.csv", basic_rows)
    _write_csv(generated / "movement_quality_static.csv", [])
    _write_csv(
        generated / "backstep_quality_static.csv",
        [
            {"character": "Mitsurugi", "quality_grade": "B"},
            {"character": "Seong Mi-na", "quality_grade": "Unranked"},
        ],
    )

    out = tmp_path / "player.md"
    audit = render(generated, out)
    write_audit(generated / "report_value_audit.csv", audit)
    text = out.read_text(encoding="utf-8")

    assert "| Mitsurugi | B | 0.027 | 0.048 | 0.127 | 0.167 | 0.180 |" in text
    assert "Seong Mi-na | Unknown | 0.452 | 0.918" in text
    assert all(term not in text.lower() for term in FORBIDDEN_MAIN_TERMS)
    assert audit
    assert all(row.status == "match" for row in audit)
