import json
from pathlib import Path

from tools.deterministic_qualification.observer_pair import (
    ObserverPairPaths, ObserverPeerPaths,
)
from tools.deterministic_qualification.runner import (
    DEFAULT_SANDBOX_ROOT, _paired_observer_paths, build_parser,
)
from tools.deterministic_qualification.paired_online import (
    FAILURE, NativeOnlineTerminal, ROUND_TAKEOVER_EVENTS, TAKEOVER_EVENTS,
    _atomic_online_request,
    _confirmed_convergence, _development_session_latch,
    _development_smoke_complete,
    _completed_teardown_reports,
    _cycle_teardown_run_ids,
    _first_correction_evidence,
    _correction_stimulus_sequence_evidence,
    _qualification_failure_plan, _read_since,
    _repeated_correction_evidence,
    _required_correction_stimulus,
    _require_ordered_takeover, _require_two_owned_generations,
    _raise_on_native_terminal, _root_failure_evidence,
    _wait_development_setup_smoke,
)
from tools.deterministic_qualification.paired_online_evaluation import (
    reevaluate_paired_correction_capture,
)
from tools.deterministic_qualification.trace_parser import capture_log_offset

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
    assert arguments.development_setup_smoke is False


def test_paired_online_exposes_noncertifying_automated_setup_smoke():
    arguments = build_parser().parse_args([
        "paired-online", "--case-manifest", "cases.json", "--case", "case-a",
        "--dll", "HorseMod.dll", "--output-dir", "evidence",
        "--report", "report.json", "--development-setup-smoke",
    ])
    assert arguments.development_setup_smoke is True


def test_paired_online_exposes_noncertifying_repeated_correction_smoke():
    arguments = build_parser().parse_args([
        "paired-online", "--case-manifest", "cases.json", "--case", "case-a",
        "--dll", "HorseMod.dll", "--output-dir", "evidence",
        "--report", "report.json", "--development-correction-smoke",
    ])
    assert arguments.development_correction_smoke is True


def test_paired_online_exposes_two_cycle_same_process_reentry_smoke():
    arguments = build_parser().parse_args([
        "paired-online", "--case-manifest", "cases.json", "--case", "case-a",
        "--dll", "HorseMod.dll", "--output-dir", "evidence",
        "--report", "report.json", "--development-reentry-smoke",
        "--match-cycles", "2",
    ])
    assert arguments.development_reentry_smoke is True
    assert arguments.match_cycles == 2
    assert not _development_smoke_complete(arguments, 1)
    assert _development_smoke_complete(arguments, 2)


def test_paired_online_exposes_hash_bound_capture_reevaluation():
    arguments = build_parser().parse_args([
        "paired-online-evaluate", "--input-report", "capture.json",
        "--report", "evaluation.json",
    ])
    assert arguments.input_report == Path("capture.json")
    assert arguments.report == Path("evaluation.json")


def test_same_process_reentry_requires_bilateral_lobby_reports(tmp_path):
    paths = {label: tmp_path / f"{label}.json"
             for label in ("host", "sandbox")}
    run_ids = {"host": "h-return", "sandbox": "s-return"}
    report = {
        "schema_version": 2, "kind": "online_match_teardown",
        "state": "complete", "detail": "returned_to_player_match_lobby",
    }
    paths["host"].write_text(json.dumps({**report, "run_id": "h-return"}))
    assert _completed_teardown_reports(paths, run_ids) is None
    paths["sandbox"].write_text(json.dumps({**report, "run_id": "s-return"}))
    completed = _completed_teardown_reports(paths, run_ids)
    assert completed is not None
    assert set(completed) == {"host", "sandbox"}


def test_cycle_teardown_ids_stay_inside_room_request_contract():
    run_id = "paired-" + "a" * 32
    first = _cycle_teardown_run_ids(run_id, 1)
    second = _cycle_teardown_run_ids(run_id, 2)
    assert first != second
    assert all(len(value) <= 63 for value in (*first.values(), *second.values()))


def test_first_correction_requires_bilateral_identical_canonical_proof():
    digest = "42" * 32
    line = (
        "[HorseMod] online qualification run_id={run} confirmed_hash "
        "generation=2 frame=153 sha256={digest} checks=1 corrections=1 "
        "max_depth=4 pending_events=0 presentation_bytes=0 checkpoint_bytes=0 "
        "batch_entry_bytes=0 timeline_owned_bytes=0 forced_snapshot_bytes=0 "
        "presentation_owned_bytes=0 scratch_metadata_bytes=0 "
        "aggregate_owned_bytes=0 aggregate_limit=1 post_status4_growth=0 "
        "capacity_failures=0 correction_samples=1 correction_p50_ns=1 "
        "correction_p95_ns=1 correction_p99_ns=1 correction_max_ns=1 "
        "verified_audio_batches=1 audio_sequence_mismatches=0 "
        "verified_camera_batches=1 camera_publication_mismatches=0 "
        "presentation_failures=0 journal_duplicates=0 "
        "journal_publish_failures=0 journal_committed=1\n"
    )
    evidence = _first_correction_evidence({
        "host": line.format(run="h", digest=digest),
        "sandbox": line.format(run="s", digest=digest),
    }, {"host": "h", "sandbox": "s"})
    assert evidence == {
        "generation": 2, "frame": 153, "sha256": digest,
        "host_corrections": 1, "sandbox_corrections": 1,
        "host_max_depth": 4, "sandbox_max_depth": 4,
    }
    with pytest.raises(RuntimeError, match="did not converge"):
        _first_correction_evidence({
            "host": line.format(run="h", digest=digest),
            "sandbox": line.format(run="s", digest="24" * 32),
        }, {"host": "h", "sandbox": "s"})


def test_correction_smoke_requires_bilateral_armed_stimulus_at_status_five():
    line = (
        "[HorseMod] online qualification run_id={run} armed authenticated "
        "correction stimulus depth=11 trigger_frame=89 "
        "after_confirmed_gekko_frame=29\n"
    )
    proof = _required_correction_stimulus({
        "host": line.format(run="h"),
        "sandbox": line.format(run="s"),
    }, {"host": "h", "sandbox": "s"}, 11)
    assert proof["host"] == {
        "depth": 11, "trigger_frame": 89, "confirmed_gekko_frame": 29}
    with pytest.raises(RuntimeError, match="sandbox reached status 5"):
        _required_correction_stimulus({
            "host": line.format(run="h"), "sandbox": "",
        }, {"host": "h", "sandbox": "s"}, 11)


def test_repeated_correction_smoke_requires_11_1_6_and_three_convergences():
    def stimulus_text(delays):
        return "".join(
        "[HorseMod] online qualification run_id={run} armed authenticated "
        f"correction stimulus depth={depth} trigger_frame={43 + index * 30} "
        f"after_confirmed_gekko_frame={29 + index * 30} "
        f"ordinal={index + 1} total=3 lead_frames=14 "
        f"transport_delay={delays[index]}\n"
        for index, depth in enumerate((11, 1, 6)))
    host_stimuli = stimulus_text((12, 2, 7))
    sandbox_stimuli = stimulus_text((12, 2, 7))
    stimulus_evidence = _correction_stimulus_sequence_evidence({
        "host": host_stimuli.format(run="h"),
        "sandbox": sandbox_stimuli.format(run="s"),
    }, {"host": "h", "sandbox": "s"}, (11, 1, 6))
    assert stimulus_evidence is not None
    assert [row["depth"] for row in stimulus_evidence["host"]] == [11, 1, 6]
    assert [row["transport_delay"] for row in stimulus_evidence["host"]] == [12, 2, 7]
    assert [row["transport_delay"] for row in stimulus_evidence["sandbox"]] == [12, 2, 7]

    def combined(run, correction_counts, stimuli):
        stimulus_rows = stimuli.splitlines(keepends=True)
        rows = [stimulus_rows[0].format(run=run)]
        for index, corrections in enumerate(correction_counts):
            corrections_before = 0 if index == 0 else correction_counts[index - 1]
            confirmed = (
                f"[HorseMod] online qualification run_id={run} confirmed_hash "
                f"generation=1 frame={243 + index * 90} "
                f"sha256={str(index + 1) * 64} checks={index + 4} "
                f"corrections={corrections} max_depth=11 pending_events=0 "
                "presentation_bytes=0 checkpoint_bytes=0 batch_entry_bytes=0 "
                "timeline_owned_bytes=0 forced_snapshot_bytes=0 "
                "presentation_owned_bytes=0 scratch_metadata_bytes=0 "
                "aggregate_owned_bytes=0 aggregate_limit=1 "
                "post_status4_growth=0 capacity_failures=0 "
                f"correction_samples={corrections} correction_p50_ns=1 "
                "correction_p95_ns=1 correction_p99_ns=1 correction_max_ns=1 "
                "verified_audio_batches=1 audio_sequence_mismatches=0 "
                "verified_camera_batches=1 camera_publication_mismatches=0 "
                "presentation_failures=0 journal_duplicates=0 "
                "journal_publish_failures=0 journal_committed=1\n")
            if index + 1 < len(stimulus_rows):
                stimulus = stimulus_rows[index + 1].replace(
                    " transport_delay=",
                    f" corrections_before={corrections} transport_delay=")
                rows.append(stimulus.format(run=run))
            rows.append(confirmed)
        return "".join(rows)
    correction_evidence = _repeated_correction_evidence({
        "host": combined("h", (1, 2, 3), host_stimuli),
        "sandbox": combined("s", (1, 2, 3), sandbox_stimuli),
    }, {"host": "h", "sandbox": "s"}, 3)
    assert correction_evidence is not None
    assert [row["ordinal"] for row in correction_evidence] == [1, 2, 3]
    assert correction_evidence[1]["host_corrections"] == 2
    assert correction_evidence[1]["sandbox_corrections"] == 2
    # One peer advancing cannot certify the other peer's restore path and
    # must not allow teardown to race the next confirmed-hash exchange.
    with pytest.raises(RuntimeError, match="correction 2 was not bilateral"):
        _repeated_correction_evidence({
            "host": combined("h", (1, 2, 3), host_stimuli),
            "sandbox": combined("s", (1, 1, 2), sandbox_stimuli),
        }, {"host": "h", "sandbox": "s"}, 3)


def test_paired_online_exposes_typed_authoritative_failure_case():
    arguments = build_parser().parse_args([
        "paired-online", "--case-manifest", "cases.json", "--case", "case-a",
        "--dll", "HorseMod.dll", "--output-dir", "evidence",
        "--report", "report.json", "--failure-case", "postownership_restore",
    ])
    assert arguments.failure_case == "postownership_restore"


def test_development_setup_requires_matching_native_session_latch():
    text = (
        "[HorseMod] online qualification run_id=run-a "
        "session_identity_latched lobby_id=42 local_slot=1 session_state=3\n"
    )
    assert _development_session_latch(text, "run-a", 42) == {
        "lobby_id": 42, "local_slot": 1, "session_state": 3,
    }
    assert _development_session_latch(text, "another-run", 42) is None
    with pytest.raises(RuntimeError, match="does not match"):
        _development_session_latch(text, "run-a", 43)


def test_development_setup_surfaces_unresolved_native_lobby_diagnostics():
    text = (
        "[HorseMod] online qualification run_id=run-a "
        "session_lobby_pending lobby_id=42 status=identity_mismatch "
        "mask=0x77 count=2 local=10 members=10/20 discriminator=196686\n"
    )
    with pytest.raises(RuntimeError, match=r"mask=0x77.*members=10/20"):
        _development_session_latch(text, "run-a", 42)


def test_paired_log_cursor_detects_ue4ss_log_replacement(tmp_path):
    log = tmp_path / "UE4SS.log"
    log.write_bytes(b"old boot\n" + b"x" * 128)
    cursor = capture_log_offset(log)
    replacement = b"new boot\nnative latch\n" + b"y" * 160
    log.write_bytes(replacement)
    assert _read_since(log, cursor).encode() == replacement


def test_native_failure_line_remains_terminal_after_status_seven_cleanup():
    text = (
        "[HorseMod] online qualification run_id=run-a failed "
        "status=identity_mismatch\n"
        "[ReplayQualification] online qualification run_id=run-a status=7\n"
    )
    match = FAILURE.search(text)
    assert match is not None
    assert match.group("run") == "run-a"
    assert match.group("failure") == "identity_mismatch"


def test_native_failure_aborts_polling_with_authoritative_code():
    logs = {
        "host": (
            "[HorseMod] online qualification run_id=host failed "
            "status=missing_snapshot lifecycle_phase=owned "
            "coordinator_phase=active local_slot=0 generation=2 frame=152 "
            "owns=1\n"
        ),
        "sandbox": "",
    }
    with pytest.raises(NativeOnlineTerminal) as captured:
        _raise_on_native_terminal(
            logs, {"host": "host", "sandbox": "sandbox"}, "teardown")
    assert captured.value.peer == "host"
    assert captured.value.failure == "missing_snapshot"
    assert "teardown" in str(captured.value)


def test_native_status_six_is_terminal_without_waiting_for_status_seven():
    logs = {
        "host": "",
        "sandbox": (
            "[ReplayQualification] online qualification run_id=sandbox "
            "status=6\n"
        ),
    }
    with pytest.raises(NativeOnlineTerminal, match="status=6"):
        _raise_on_native_terminal(
            logs, {"host": "host", "sandbox": "sandbox"}, "active match")


def test_native_terminal_prefers_root_over_peer_disconnect():
    logs = {
        "host": (
            "[2026-09-03 00:00:00.0200000] [HorseMod] online "
            "qualification run_id=host failed status=peer_disconnected "
            "lifecycle_phase=owned coordinator_phase=failed local_slot=0 "
            "generation=2 frame=152 owns=1\n"
        ),
        "sandbox": (
            "[2026-09-03 00:00:00.0100000] [HorseMod] online "
            "qualification run_id=sandbox failed status=missing_snapshot "
            "lifecycle_phase=owned coordinator_phase=active local_slot=1 "
            "generation=2 frame=152 owns=1\n"
        ),
    }
    with pytest.raises(NativeOnlineTerminal) as captured:
        _raise_on_native_terminal(
            logs, {"host": "host", "sandbox": "sandbox"}, "active match")
    assert captured.value.peer == "sandbox"
    assert captured.value.failure == "missing_snapshot"


def test_setup_smoke_teardown_aborts_immediately_on_native_failure(tmp_path):
    def peer(label: str) -> ObserverPeerPaths:
        root = tmp_path / label
        root.mkdir()
        return ObserverPeerPaths(
            mods_root=root / "mods", horsemod_dll=root / "main.dll",
            config=root / "rollback.ini", qualification_root=root / "q",
            log=root / "UE4SS.log",
        )

    paths = ObserverPairPaths(host=peer("host"), sandbox=peer("sandbox"))
    online_ids = {"host": "run-host", "sandbox": "run-sandbox"}
    automation_ids = {"host": "auto-host", "sandbox": "auto-sandbox"}
    case = {
        "fighter_order": ["012", "015"],
        "stage_selection_code": "273",
        "stage_package_root": "/Game/DLC/07/Stage/STG011_R",
        "native_display_name": "Silver Wolves' Haven",
    }
    for label, current_peer in (("host", paths.host), ("sandbox", paths.sandbox)):
        current_peer.qualification_root.mkdir()
        current_peer.log.write_text(
            f"[HorseMod] online qualification run_id={online_ids[label]} "
            f"session_identity_latched lobby_id=42 local_slot="
            f"{0 if label == 'host' else 1} session_state=3\n"
            f"[HorseMod] online qualification run_id={online_ids[label]} "
            "handshake map=/Game/DLC/07/Stage/STG011_R "
            "display_map=Silver Wolves' Haven fighters=012/015 "
            f"local_slot={0 if label == 'host' else 1} "
            f"loaded_map_sha256={'11' * 32} session_state=3\n"
            f"[ReplayQualification] online qualification "
            f"run_id={online_ids[label]} status=5\n",
            encoding="utf-8",
        )
        (current_peer.qualification_root / "online_room_report.json").write_text(
            json.dumps({
                "schema_version": 2, "kind": "online_match_setup",
                "run_id": automation_ids[label], "state": "complete",
                "detail": (
                    "exact_match_content_verified:012/015:stage=273:"
                    "map=Silver Wolves' Haven"),
                "lobby_id": 42,
            }),
            encoding="utf-8",
        )

    return_requests: list[str] = []

    def request_return() -> str:
        return_requests.append("return")
        with paths.host.log.open("a", encoding="utf-8") as stream:
            stream.write(
                "[HorseMod] online qualification run_id=run-host failed "
                "status=missing_snapshot lifecycle_phase=owned "
                "coordinator_phase=active local_slot=0 generation=2 "
                "frame=152 owns=1\n"
                "[ReplayQualification] online qualification "
                "run_id=run-host status=6\n"
            )
        return "teardown-run"

    with pytest.raises(NativeOnlineTerminal, match="missing_snapshot"):
        _wait_development_setup_smoke(
            paths, online_ids, automation_ids, case,
            {"host": 0, "sandbox": 0}, 2.0, lambda: None,
            request_return,
        )
    assert return_requests == ["return"]


def test_paired_root_failure_does_not_mask_cause_as_peer_disconnect():
    root = _root_failure_evidence({
        "cycle-001-host": (
            "[HorseMod] online qualification run_id=host failed "
            "status=peer_disconnected lifecycle_phase=preownership "
            "coordinator_phase=freezing_baseline local_slot=0 "
            "generation=4 frame=220 owns=0\n"
        ),
        "cycle-001-sandbox": (
            "[HorseMod] online qualification run_id=sandbox failed "
            "status=state_hash_mismatch lifecycle_phase=preownership "
            "coordinator_phase=freezing_baseline local_slot=1 "
            "generation=4 frame=220 owns=0\n"
        ),
    })
    assert root is not None
    assert root["responsible_peer"] == "sandbox"
    assert root["earliest_root_failure"]["status"] == "state_hash_mismatch"
    assert root["earliest_root_failure"]["lifecycle_phase"] == "preownership"


def test_bilateral_same_coordinate_mismatch_identifies_both_peers():
    line = (
        "[HorseMod] online qualification run_id={run} failed "
        "status=state_hash_mismatch lifecycle_phase=preownership "
        "coordinator_phase=freezing_baseline local_slot={slot} "
        "generation=7 frame=340 owns=0\n"
    )
    root = _root_failure_evidence({
        "host": line.format(run="host", slot=0),
        "sandbox": line.format(run="sandbox", slot=1),
    })
    assert root is not None
    assert root["responsible_peer"] == "both"


def test_paired_root_failure_uses_cross_peer_logger_timestamps():
    root = _root_failure_evidence({
        "host": (
            "[2026-09-02 19:01:08.7326500] [HorseMod] online "
            "qualification run_id=host failed status=illegal_transition "
            "lifecycle_phase=preownership coordinator_phase=active "
            "local_slot=0 generation=4 frame=224 owns=0\n"
        ),
        "sandbox": (
            "[2026-09-02 19:01:08.7273811] [HorseMod] online "
            "qualification run_id=sandbox failed status=restore_write_failed "
            "lifecycle_phase=owned coordinator_phase=active "
            "local_slot=1 generation=4 frame=223 owns=1\n"
        ),
    })
    assert root is not None
    assert root["responsible_peer"] == "sandbox"
    assert root["earliest_root_failure"]["status"] == "restore_write_failed"
    assert root["earliest_root_failure"]["owned"] is True
    assert root["cross_peer_ordering"] == "logger_timestamp"


def test_online_request_binds_qualification_fault(tmp_path):
    target = tmp_path / "online_request.txt"
    temporary = _atomic_online_request(target, "run-a", 123456, 5)
    assert temporary.read_text(encoding="utf-8") == (
        "version=4\nrun_id=run-a\nnot_before_unix_ms=123456\n"
        "qualification_fault=5\ncorrection_stimulus_depths=\narm=true\n"
    )


def test_online_request_binds_11_1_6_correction_stimulus(tmp_path):
    target = tmp_path / "online_request.txt"
    temporary = _atomic_online_request(
        target, "run-correction", 123456, 0, (11, 1, 6))
    assert "qualification_fault=0\ncorrection_stimulus_depths=11,1,6\n" in (
        temporary.read_text(encoding="utf-8"))


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
