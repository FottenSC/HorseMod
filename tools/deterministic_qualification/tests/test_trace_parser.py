from pathlib import Path

from tools.deterministic_qualification.trace_parser import (
    capture_log_offset,
    parse_forced_qualification_evidence,
    parse_correction_probe_evidence,
    parse_gameplay_rng_coverage_evidence,
    parse_normal_render_rate_evidence,
    parse_replay_seek_evidence,
    parse_presentation_coverage_evidence,
    parse_presentation_identity_evidence,
    parse_qualification_health_evidence,
    parse_replay_metadata_evidence,
    parse_stock_round_outcome_evidence,
    wait_for_boot_evidence,
    wait_for_replay_lifecycle_evidence,
)


def test_qualification_health_parser_uses_native_counters() -> None:
    text = (
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[ReplayQualification] qualification health capacity_failures=0 "
        "capacity_growth_events=0 timeline_accounting_failures=0 "
        "aggregate_owned_bytes=1234 presentation_owned_bytes=567 "
        "presentation_duplicate_failures=0 presentation_publish_failures=0 "
        "cursor_mismatches=0 batch_accounting_mismatches=0 "
        "round_transition_barriers=4\n"
    )
    evidence = parse_qualification_health_evidence(text)
    assert evidence is not None
    assert evidence.capacity_failures == 0
    assert evidence.capacity_growth_events == 0
    assert evidence.timeline_accounting_failures == 0
    assert evidence.presentation_duplicate_failures == 0
    assert evidence.presentation_publish_failures == 0
    assert evidence.cursor_mismatches == 0
    assert evidence.batch_accounting_mismatches == 0
    assert evidence.round_transition_barriers == 4
    assert evidence.aggregate_owned_bytes == 1234


def test_presentation_identity_parser_preserves_exact_sequences() -> None:
    text = (
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[ReplayQualification] presentation identity batches=600 "
        "audio_events=42 audio_identity=0x1122334455667788 "
        "order_events=51 order_identity=0x8877665544332211 "
        "camera_identity=0x0102030405060708 camera_batches=57 "
        "failures=0 journal_committed=9\n"
    )
    evidence = parse_presentation_identity_evidence(text)
    assert evidence is not None
    assert evidence.audio_identity == 0x1122334455667788
    assert evidence.order_identity == 0x8877665544332211
    assert evidence.camera_identity == 0x0102030405060708
    assert evidence.camera_batches == 57
    assert evidence.failures == 0


def test_replay_metadata_parser_is_bound_to_latest_import() -> None:
    text = (
        "[ReplayQualification] source=" + "a" * 40 + " native_import=ready\n"
        "[ReplayQualification] replay metadata stage=9 map=9 "
        "left_character=1 right_character=2 state_reset_records=2\n"
        "[ReplayQualification] source=" + "b" * 40 + " native_import=ready\n"
        "[ReplayQualification] replay metadata stage=260 map=4 "
        "left_character=35 right_character=11 state_reset_records=3\n"
    )
    evidence = parse_replay_metadata_evidence(text)
    assert evidence is not None
    assert evidence.stage == 260
    assert evidence.map == 4
    assert evidence.left_character == 35
    assert evidence.right_character == 11
    assert evidence.state_reset_records == 3


def test_replay_metadata_parser_supports_explicit_request_local_window() -> None:
    text = (
        "[ReplayQualification] replay metadata stage=273 map=17 "
        "left_character=12 right_character=14 state_reset_records=4\n"
    )
    assert parse_replay_metadata_evidence(text) is None
    evidence = parse_replay_metadata_evidence(text, source_bound=False)
    assert evidence is not None
    assert (evidence.stage, evidence.map) == (273, 17)


def test_correction_probe_parser_preserves_depth_order() -> None:
    lines = "".join(
        "[HorseMod] owned correction probe passed "
        f"depth={depth} base=100 final=110 batches=7 coordinates=7 "
        "undo_capture_us=1 restore_us=2 resim_us=3 verify_us=4 "
        f"total_us={depth + 10} restore_phase_us(local=1/1)\n"
        for depth in (1, 6, 11, 7)
    )
    text = "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n" + lines
    evidence = parse_correction_probe_evidence(text)
    assert tuple(item.depth for item in evidence) == (1, 6, 11, 7)
    assert evidence[2].total_us == 21


def test_stock_round_outcome_parser_is_source_bound() -> None:
    text = (
        "[ReplayQualification] source=" + "a" * 40
        + " native_import=ready\n"
        "[ReplayQualification] stock round outcome qualification passed "
        "rounds=5 match_winner=0 winners=0,1,0,1,0\n"
    )
    evidence = parse_stock_round_outcome_evidence(text)
    assert evidence is not None
    assert evidence.source_commit == "a" * 40
    assert evidence.rounds == 5
    assert evidence.match_winner == 0
    assert evidence.round_winners == (0, 1, 0, 1, 0)


def test_gameplay_rng_coverage_parser_is_source_bound() -> None:
    text = (
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[ReplayQualification] gameplay rng coverage xorshift_draws=42 "
        "known_callers=0x123 unknown_callers=0 weighted_draws=3 "
        "if_draws=4 short25_p0=5 short25_p1=6 "
        "probability_transition_batches=2 state_changes_p0=7 "
        "state_changes_p1=8 probability_state_mask_p0=" + "0" * 63 + "1 "
        "probability_state_mask_p1=" + "0" * 62 + "20 "
        "transition07_calls=9 tira_random_transitions=2 "
        "tira_probability_batches=2 tira_targets=0x3 "
        "xorshift_sequence=0x0123456789abcdef "
        "transition07_sequence=0x1023456789abcdef "
        "tira_sequence=0x2023456789abcdef "
        "tira_stance_batches=2 tira_slot_mask=0x1 "
        "state19_sequence_p0=0x3023456789abcdef "
        "state19_sequence_p1=0x4023456789abcdef "
        "state19_initial_p0=0 state19_initial_p1=3 "
        "state19_final_p0=1 state19_final_p1=3 "
        "xorshift_landing=0x12345678,0x23456789,0x3456789a "
        "state19_at_tira_transition_p0=1 "
        "state19_at_tira_transition_p1=3 state19_initial_valid=1 "
        "tira_last_target=0x0205 resolved_hit_calls=11 "
        "resolved_hit_sequence=0x5023456789abcdef tira_writer_calls=3 "
        "tira_writer_sequence=0x6023456789abcdef "
        "tira_writer_slot_mask=0x2 tira_last_writer_move=0x306f\n"
    )
    evidence = parse_gameplay_rng_coverage_evidence(text)
    assert evidence is not None
    assert evidence.known_callers == 0x123
    assert evidence.unknown_callers == 0
    assert evidence.probability_transition_batches == 2
    assert evidence.state_changes_p1 == 8
    assert evidence.probability_state_mask_p0 == 1
    assert evidence.probability_state_mask_p1 == 0x20
    assert evidence.transition07_calls == 9
    assert evidence.tira_random_transitions == 2
    assert evidence.tira_probability_batches == 2
    assert evidence.tira_targets == 3
    assert evidence.xorshift_sequence == 0x0123456789ABCDEF
    assert evidence.tira_stance_batches == 2
    assert evidence.tira_slot_mask == 1
    assert evidence.state19_initial_p0 == 0
    assert evidence.state19_final_p0 == 1
    assert evidence.xorshift_landing == (0x12345678, 0x23456789, 0x3456789A)
    assert evidence.state19_at_tira_transition_p0 == 1
    assert evidence.state19_initial_valid
    assert evidence.tira_last_target == 0x0205
    assert evidence.resolved_hit_calls == 11
    assert evidence.resolved_hit_sequence == 0x5023456789ABCDEF
    assert evidence.tira_writer_calls == 3
    assert evidence.tira_writer_sequence == 0x6023456789ABCDEF
    assert evidence.tira_writer_slot_mask == 2
    assert evidence.tira_last_writer_move == 0x306F


def test_presentation_coverage_parser_is_source_bound() -> None:
    text = (
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[ReplayQualification] presentation source coverage stage_wall=1 "
        "stage_barrier=2 stage_dispatch=3 audio=4 audio_direct=5 "
        "audio_remap=6 audio_source=7 audio_stop_all=8 "
        "audio_blueprint=9 particle_spawn=10\n"
    )
    evidence = parse_presentation_coverage_evidence(text)
    assert evidence is not None
    assert evidence.stage_barrier == 2
    assert evidence.audio_stop_all == 8
    assert evidence.audio_blueprint == 9
    assert evidence.particle_spawn == 10


def test_replay_seek_parser_requires_structured_rate_window() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[ReplayQualification] strict seek passed percent=10 target=1025 "
        "source_end=1575 history_verified=550 live_resumed=120 "
        "resume_total=670 resim=8 validation_us=4966 resume_window=120 "
        "resume_elapsed_us=2050000 resume_tick_rate_milli=58536 index=0\n"
    )
    evidence = parse_replay_seek_evidence(text)
    assert len(evidence) == 1
    assert evidence[0].percentage == 10
    assert evidence[0].resimulation_coordinates == 8
    assert evidence[0].resume_window == 120
    assert evidence[0].resume_tick_rate_milli == 58536


def test_normal_render_rate_parser_requires_full_and_active_windows() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[ReplayQualification] normal-render active battle rate "
        "frames=120 elapsed_us=2020000 tick_rate_milli=59405\n"
        "[ReplayQualification] normal-render battle rate "
        "frames=600 elapsed_us=10050000 tick_rate_milli=59701\n"
    )
    evidence = parse_normal_render_rate_evidence(text)
    assert evidence is not None
    assert evidence.frames == 600
    assert evidence.tick_rate_milli == 59701
    assert evidence.active_frames == 120
    assert evidence.active_tick_rate_milli == 59405


def test_normal_render_rate_parser_supports_explicit_request_local_window() -> None:
    text = (
        "[ReplayQualification] normal-render battle rate "
        "frames=120 elapsed_us=2040000 tick_rate_milli=58823\n"
        "[ReplayQualification] normal-render active battle rate "
        "frames=120 elapsed_us=2041000 tick_rate_milli=58794\n"
    )
    assert parse_normal_render_rate_evidence(text) is None
    evidence = parse_normal_render_rate_evidence(text, source_bound=False)
    assert evidence is not None
    assert evidence.frames == 120
    assert evidence.active_tick_rate_milli == 58794


def test_forced_qualification_parser_prefers_terminal_failure() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[HorseMod] forced depth-7 qualification started generation=6\n"
        "[HorseMod] forced depth-7 qualification failed completed=367 "
        "frame=1355 status=presentation_failed primary=presentation_failed\n"
    )
    evidence = parse_forced_qualification_evidence(text)
    assert evidence is not None
    assert evidence.result == "failed"
    assert evidence.completed == 367
    assert evidence.status == "presentation_failed"


def test_forced_qualification_parser_requires_exact_summary_fields() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[HorseMod] forced depth-7 qualification passed completed=600 "
        "generations=6-7 stage_wall_suppressed=7 "
        "stage_barrier_suppressed=0 stage_semantic_dispatches=7 "
        "particle_spawn_suppressed=7 particle_bind_suppressed=0 "
        "presentation_failures=0 journal_attempted=10 "
        "canonical_convergence=exact "
        "presentation_terminal_coverage=incomplete\n"
    )
    evidence = parse_forced_qualification_evidence(text)
    assert evidence is not None
    assert evidence.result == "passed"
    assert evidence.completed == 600
    assert evidence.canonical_convergence == "exact"
    assert evidence.presentation_terminal_coverage == "incomplete"
    assert evidence.suppressed_stage_wall == 7
    assert evidence.suppressed_stage_barrier == 0
    assert evidence.semantic_stage_dispatches == 7
    assert evidence.suppressed_particle_spawn == 7
    assert evidence.presentation_failures == 0


def test_forced_qualification_parser_keeps_timing_and_capacity_metrics() -> None:
    text = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[HorseMod] forced correction qualification passed depth=7 location=2 "
        "completed=600 cycle_p99_us=1234 cycle_max_us=2345 capture_samples=601 "
        "capture_p99_us=400 capture_max_us=900 scratch_capacity_bytes=10->10 "
        "scratch_growth_events=0 stage_wall_suppressed=0 stage_barrier_suppressed=0 "
        "stage_semantic_dispatches=0 round_terminal_source_stop_all=1 "
        "particle_spawn_suppressed=0 "
        "audio_batches_verified=600 audio_sequence_mismatches=0 "
        "camera_batches_verified=600 camera_publication_mismatches=0 "
        "presentation_failures=0 journal_attempted=12 journal_recorded=12 "
        "journal_discarded=0 journal_committed=12 journal_duplicates=0 "
        "journal_capacity_failures=0 journal_publish_failures=0 "
        "journal_pending=0 journal_payload_bytes=0 canonical_convergence=exact "
        "presentation_terminal_coverage=complete\n"
    )
    evidence = parse_forced_qualification_evidence(text)
    assert evidence is not None
    assert evidence.cycle_p99_us == 1234
    assert evidence.capture_p99_us == 400
    assert evidence.capture_max_us == 900
    assert evidence.scratch_capacity_begin == evidence.scratch_capacity_end == 10
    assert evidence.round_terminal_source_stop_all == 1


def test_waiters_ignore_complete_stale_sessions(tmp_path: Path) -> None:
    log = tmp_path / "UE4SS.log"
    log.write_text(
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[HorseMod] deterministic lifecycle hooks armed\n"
        "[ReplayQualification] source=" + "a" * 40
        + " native_import=ready\n"
        "[HorseMod] frame-fencepost first observation\n",
        encoding="utf-8",
    )
    start = capture_log_offset(log)
    with log.open("a", encoding="utf-8") as stream:
        stream.write(
            "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
            "[HorseMod] deterministic lifecycle hooks armed\n"
            "[ReplayQualification] source=" + "b" * 40
            + " native_import=ready\n"
            "[HorseMod] frame-fencepost first observation\n"
        )

    boot = wait_for_boot_evidence(log, 0.1, start_offset=start)
    lifecycle = wait_for_replay_lifecycle_evidence(
        log, 0.1, start_offset=start
    )
    assert boot.source_commit == "b" * 40
    assert lifecycle.source_commit == "b" * 40


def test_waiters_restart_at_zero_when_log_is_truncated_and_regrown(
    tmp_path: Path,
) -> None:
    log = tmp_path / "UE4SS.log"
    stale = (
        "[HorseMod] ctor v1.0 source=" + "a" * 40 + "\n"
        "[HorseMod] deterministic lifecycle hooks armed\n"
        "[ReplayQualification] source=" + "a" * 40
        + " native_import=ready\n"
        "[HorseMod] frame-fencepost first observation\n"
    )
    log.write_text(stale, encoding="utf-8")
    start = capture_log_offset(log)

    current = (
        "[HorseMod] ctor v2.0 source=" + "b" * 40 + "\n"
        "[HorseMod] deterministic lifecycle hooks armed\n"
        "[ReplayQualification] source=" + "b" * 40
        + " native_import=ready\n"
        "[HorseMod] frame-fencepost first observation\n"
        + "new-session-padding\n" * 20
    )
    assert len(current.encode("utf-8")) > len(stale.encode("utf-8"))
    log.write_text(current, encoding="utf-8")

    boot = wait_for_boot_evidence(log, 0.1, start_offset=start)
    lifecycle = wait_for_replay_lifecycle_evidence(
        log, 0.1, start_offset=start
    )
    assert boot.source_commit == "b" * 40
    assert lifecycle.source_commit == "b" * 40
