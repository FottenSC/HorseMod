from copy import deepcopy

from tools.deterministic_qualification.tira_campaign import evaluate_tira_reports
from tools.deterministic_qualification.configuration import (
    contract_sha256, expected_fields,
)


def _cases():
    return [
        {
            "case_id": f"case-{index}",
            "native_display_name": "Astral Chaos: Tide of the Damned",
            "stage_package_root": "/Game/Stage/STG004",
            "contains_tira": index < 2,
            "replay_sha256": "replay",
            "replay_metadata_stage": 4,
            "replay_metadata_map": 4,
        }
        for index in range(3)
    ]


def _report(case_id: str, transition: bool):
    expected_config = expected_fields(enabled=False, trace=True)
    coverage = {
        "xorshift_draws": 10,
        "known_callers": 1,
        "unknown_callers": 0,
        "if_draws": 1,
        "xorshift_sequence": "0x11",
        "transition07_sequence": "0x22",
        "tira_sequence": "0x33",
        "tira_random_transitions": 1 if transition else 0,
        "tira_probability_batches": 1 if transition else 0,
        "tira_targets": 1 if transition else 0,
        "tira_last_target": "0x0205" if transition else "0x0000",
        "tira_stance_batches": 1 if transition else 0,
        "tira_slot_mask": 2 if transition else 0,
        "state19_sequence_p0": "0x44",
        "state19_sequence_p1": "0x55",
        "state19_initial_p0": 0,
        "state19_initial_p1": 0,
        "state19_final_p0": 0,
        "state19_final_p1": 1 if transition else 0,
        "xorshift_landing": "0x66",
        "state19_at_tira_transition_p0": 0,
        "state19_at_tira_transition_p1": 1 if transition else 0,
        "state19_initial_valid": True,
    }
    return {
        "case_id": case_id,
        "result": "pass",
        "certifying": True,
        "renderer": "normal",
        "display_map_name": "Astral Chaos: Tide of the Damned",
        "stage_package_root": "/Game/Stage/STG004",
        "artifacts": {
            "horsemod_dll_sha256": "dll",
            "schema_sha256": "schema",
            "runner_sha256": "runner",
            "replay": {"sha256": "replay"},
            "config": {"sha256": contract_sha256(expected_config)},
            "config_fields": expected_config,
        },
        "runtime": {
            "capacity_failures": 0,
            "capacity_growth_events": 0,
            "timeline_accounting_failures": 0,
            "presentation_duplicate_failures": 0,
            "presentation_publish_failures": 0,
            "aggregate_owned_bytes": 1024,
            "replay_metadata": {"stage": 4, "map": 4},
            "stock_round_outcome": {"result": "passed"},
            "final_canonical": {"hash": "same"},
            "gameplay_rng_coverage": coverage,
            "presentation": {
                "ordered_audio_payload_ids": True,
                "ephemeral_exactly_once": True,
                "persistent_final_exact": True,
                "identity": {"audio_identity": "0x11", "camera_identity": "0x22"},
            },
        },
    }


def test_evaluate_tira_reports_requires_and_accepts_exact_repeat_transition():
    cases = _cases()
    reports = []
    for index, case in enumerate(cases):
        report = _report(case["case_id"], index < 2)
        reports.extend((report, deepcopy(report)))
    result = evaluate_tira_reports(cases, reports, "dll", "schema", "runner")
    assert result["certifying"] is True
    assert result["transition_runs"] == 4


def test_evaluate_tira_reports_rejects_repeat_rng_sequence_mismatch():
    cases = _cases()
    reports = []
    for index, case in enumerate(cases):
        report = _report(case["case_id"], index < 2)
        repeated = deepcopy(report)
        reports.extend((report, repeated))
    reports[1]["runtime"]["gameplay_rng_coverage"]["tira_sequence"] = "0x99"
    result = evaluate_tira_reports(cases, reports, "dll", "schema", "runner")
    assert result["certifying"] is False
    assert any("repeat RNG/transition" in failure for failure in result["failures"])


def test_evaluate_tira_reports_rejects_repeat_presentation_mismatch():
    cases = _cases()
    reports = []
    for index, case in enumerate(cases):
        report = _report(case["case_id"], index < 2)
        reports.extend((report, deepcopy(report)))
    reports[1]["runtime"]["presentation"]["identity"]["audio_identity"] = "0x99"
    result = evaluate_tira_reports(cases, reports, "dll", "schema", "runner")
    assert result["certifying"] is False
    assert any("repeat presentation" in failure for failure in result["failures"])
