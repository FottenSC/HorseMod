from pathlib import Path

from tools.deterministic_qualification.offline_matrix import build_rows, evaluate_row


ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "docs" / "investigations" / "deterministic-production-candidate-manifest.json"


def test_exact_51_row_shape_and_native_names() -> None:
    rows = build_rows(MANIFEST)
    assert len(rows) == 51
    assert {row.display_map_name for row in rows} == {
        "Silver Wolves' Haven", "Snow-Capped Showdown", "Murakumo Shrine Grounds"
    }
    assert sum(row.required_corrections == 0 for row in rows) == 3
    assert sum(row.required_corrections == 600 for row in rows) == 48


def test_evaluator_rejects_weakened_audio_gate() -> None:
    row = next(row for row in build_rows(MANIFEST) if row.required_corrections)
    report = {
        "report_schema": 2, "certifying": True, "result": "pass",
        "case_id": row.case_id, "row_id": row.row_id, "renderer": "normal",
        "display_map_name": row.display_map_name,
        "stage_package_root": row.stage_package_root,
        "artifacts": {"horsemod_dll_sha256": "d", "schema_sha256": "s",
                      "runner_sha256": "r"},
        "runtime": {"canonical_convergence": "exact", "capacity_failures": 0,
                    "capacity_growth_events": 0,
                    "timeline_accounting_failures": 0,
                    "presentation_duplicate_failures": 0,
                    "presentation_publish_failures": 0,
                    "clean_exit": True, "reentry": True, "location": row.location,
                    "depth": row.depth, "consecutive_corrections": 600,
                    "presentation": {"ordered_audio_payload_ids": False,
                                     "ephemeral_exactly_once": True,
                                     "persistent_final_exact": True, "leaks": 0,
                                     "required_activity": 1,
                                     "terminal_coverage": "complete"},
                    "performance": {"capture_p99_us": 500,
                                    "capture_max_us": 1000,
                                    "correction_p99_us": 16000}},
    }
    assert "ordered audio payload-ID identity failed" in evaluate_row(
        row, report, "d", "s", "r")
