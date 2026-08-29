from tools.deterministic_qualification.runner import (
    DEFAULT_SANDBOX_ROOT, _paired_observer_paths, build_parser,
)
from tools.deterministic_qualification.paired_online import (
    ROUND_TAKEOVER_EVENTS, TAKEOVER_EVENTS, _atomic_online_request,
    _confirmed_convergence, _qualification_failure_plan,
    _require_ordered_takeover, _require_two_owned_generations,
)

import pytest


def test_paired_online_exposes_explicit_same_process_cycle_controls():
    arguments = build_parser().parse_args([
        "paired-online",
        "--case-manifest", "cases.json",
        "--case", "case-a",
        "--dll", "HorseMod.dll",
        "--output-dir", "evidence",
        "--report", "report.json",
        "--match-cycles", "3",
        "--cycling-soak-seconds", "3600",
    ])
    assert arguments.match_cycles == 3
    assert arguments.cycling_soak_seconds == 3600


def test_paired_online_cycle_defaults_are_one_bounded_match():
    arguments = build_parser().parse_args([
        "paired-online",
        "--case-manifest", "cases.json",
        "--case", "case-a",
        "--dll", "HorseMod.dll",
        "--output-dir", "evidence",
        "--report", "report.json",
    ])
    assert arguments.match_cycles == 1
    assert arguments.cycling_soak_seconds == 0


def test_paired_online_exposes_typed_authoritative_failure_case():
    arguments = build_parser().parse_args([
        "paired-online", "--case-manifest", "cases.json", "--case", "case-a",
        "--dll", "HorseMod.dll", "--output-dir", "evidence",
        "--report", "report.json", "--failure-case", "postownership_restore",
    ])
    assert arguments.failure_case == "postownership_restore"


def test_online_request_binds_qualification_fault(tmp_path):
    target = tmp_path / "online_request.txt"
    temporary = _atomic_online_request(target, "run-a", 123456, 5)
    assert temporary.read_text(encoding="utf-8") == (
        "version=2\nrun_id=run-a\nnot_before_unix_ms=123456\n"
        "qualification_fault=5\narm=true\n"
    )


def test_qualification_failure_plan_is_asymmetric_where_identity_requires_it():
    assert _qualification_failure_plan("clean", "preownership_mismatch") == (
        "preownership_mismatch", {"host": 1, "sandbox": 0})
    assert _qualification_failure_plan("clean", "postownership_hash") == (
        "postownership_hash", {"host": 4, "sandbox": 0})
    assert _qualification_failure_plan("disconnect_post", "") == (
        "postownership_disconnect", {"host": 0, "sandbox": 0})


def test_native_failure_case_rejects_an_external_profile():
    with pytest.raises(RuntimeError, match="clean profile"):
        _qualification_failure_plan("latency", "postownership_restore")


def test_fresh_box_derives_a_matching_isolated_sandbox_root():
    arguments = build_parser().parse_args([
        "paired-online", "--case-manifest", "cases.json", "--case", "case-a",
        "--dll", "HorseMod.dll", "--output-dir", "evidence",
        "--report", "report.json", "--sandbox-box", "sc67-release-fresh",
    ])
    paths = _paired_observer_paths(arguments)
    assert paths.sandbox.mods_root.parts[:4] == (
        DEFAULT_SANDBOX_ROOT.parent.parts + ("sc67-release-fresh", "drive"))[:4]
    assert "sc67-release-fresh" in paths.sandbox.mods_root.parts


def _history(*hashes):
    return [
        {"generation": 7, "frame": 30 * (index + 1), "sha256": value}
        for index, value in enumerate(hashes)
    ]


def test_confirmed_convergence_requires_equal_peer_hashes_and_cadence():
    digest_a = "11" * 32
    digest_b = "22" * 32
    proof = _confirmed_convergence({
        "host": {"confirmed_history": _history(digest_a, digest_b)},
        "sandbox": {"confirmed_history": _history(digest_a, digest_b)},
    })
    assert proof == {
        "matched_checks": 2, "last_generation": 7, "last_frame": 60,
        "last_sha256": digest_b, "cadence_frames": 30,
    }

    with pytest.raises(RuntimeError, match="diverged"):
        _confirmed_convergence({
            "host": {"confirmed_history": _history(digest_a, digest_b)},
            "sandbox": {"confirmed_history": _history(digest_a, digest_a)},
        })


def test_confirmed_convergence_rejects_missing_30_frame_cadence():
    digest = "33" * 32
    bad = _history(digest, digest)
    bad[1]["frame"] = 61
    with pytest.raises(RuntimeError, match="cadence"):
        _confirmed_convergence({
            "host": {"confirmed_history": bad},
            "sandbox": {"confirmed_history": _history(digest, digest)},
        })


def test_confirmed_convergence_rejects_unmatched_trailing_checks():
    digest = "44" * 32
    with pytest.raises(RuntimeError, match="unmatched trailing"):
        _confirmed_convergence({
            "host": {"confirmed_history": _history(digest, digest, digest)},
            "sandbox": {"confirmed_history": _history(digest, digest)},
        })


def test_takeover_lifecycle_requires_exact_event_order_before_ownership():
    exact = list(TAKEOVER_EVENTS) + list(ROUND_TAKEOVER_EVENTS)
    _require_ordered_takeover(exact, "host")
    reordered = exact.copy()
    reordered[3], reordered[4] = reordered[4], reordered[3]
    with pytest.raises(RuntimeError, match="exact ordered"):
        _require_ordered_takeover(reordered, "host")


def test_two_owned_generations_require_hashes_and_second_generation_correction():
    records = [
        {"event": "first_owned_input", "generation": 7, "frame": 12},
        {"event": "first_owned_input", "generation": 8, "frame": 9},
    ]
    confirmed = [
        {"generation": 7, "frame": 30, "corrections": 2},
        {"generation": 8, "frame": 30, "corrections": 3},
    ]
    _require_two_owned_generations(records, confirmed, "host")
    confirmed[1]["corrections"] = 2
    with pytest.raises(RuntimeError, match="second owned generation"):
        _require_two_owned_generations(records, confirmed, "host")
