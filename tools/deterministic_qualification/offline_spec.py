from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


LOCATIONS = ("near_round_start", "active_combat", "confirmed_hit", "round_end")
# Exercise the widest rollback exposure first so a structural defect stops the
# matrix before the lower-risk depth rows consume qualification time.
MODES = (("depth_11", 11), ("depth_1", 1), ("depth_6", 6))


@dataclass(frozen=True)
class OfflineMatrixRow:
    row_id: str
    case_id: str
    replay: str
    replay_sha256: str
    replay_metadata_stage: int
    replay_metadata_map: int
    replay_metadata_fighters: tuple[int, int]
    display_map_name: str
    stage_package_root: str
    location: str | None
    mode: str
    depth: int
    required_corrections: int


def load_candidate_cases(path: Path) -> list[dict[str, Any]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    cases = document.get("cases")
    if (document.get("schema_version") != 1 or not isinstance(cases, list)
            or len(cases) != 3):
        raise RuntimeError(
            "candidate manifest must contain exactly three schema-v1 cases")
    required = {"case_id", "replay", "replay_sha256",
                "replay_metadata_stage", "replay_metadata_map",
                "replay_metadata_fighters", "fighter_order",
                "stage_package_root", "native_display_name"}
    for case in cases:
        if (not required <= set(case) or len(case["fighter_order"]) != 2
                or len(case["replay_metadata_fighters"]) != 2):
            raise RuntimeError("candidate manifest case is incomplete")
    return cases


def build_rows(candidate_manifest: Path) -> tuple[OfflineMatrixRow, ...]:
    rows: list[OfflineMatrixRow] = []
    for case in load_candidate_cases(candidate_manifest):
        case_id = case["case_id"]
        common = dict(case_id=case_id, replay=case["replay"],
                      replay_sha256=case["replay_sha256"],
                      replay_metadata_stage=case["replay_metadata_stage"],
                      replay_metadata_map=case["replay_metadata_map"],
                      replay_metadata_fighters=tuple(
                          int(value) for value in case["replay_metadata_fighters"]),
                      display_map_name=case["native_display_name"],
                      stage_package_root=case["stage_package_root"])
        rows.append(OfflineMatrixRow(
            row_id=f"{case_id}__baseline", location=None,
            mode="same_build_no_correction", depth=0,
            required_corrections=0, **common))
        for location in LOCATIONS:
            for mode, depth in MODES:
                rows.append(OfflineMatrixRow(
                    row_id=f"{case_id}__{location}__{mode}",
                    location=location, mode=mode, depth=depth,
                    required_corrections=600, **common))
    if len(rows) != 39 or len({row.row_id for row in rows}) != 39:
        raise RuntimeError("offline matrix must contain exactly 39 unique rows")
    return tuple(rows)
