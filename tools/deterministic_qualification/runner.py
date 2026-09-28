from __future__ import annotations

import argparse
import copy
import datetime as dt
import json
import os
import re
import subprocess
import sys
import uuid
from contextlib import contextmanager
from pathlib import Path

from .artifacts import (
    capture_harness_sha256, runner_sha256, sha256_file, source_identity,
)
from .configuration import (
    armed_baseline, canonicalize_contract, read_fields, require_disarmed,
)
from .process_control import (
    close_game,
    find_game_pid,
    force_stop_game_for_cleanup,
    launch_game,
    launch_game_executable,
    list_game_processes,
    require_game_process,
    wait_for_game,
)
from .offline_campaign import run_offline_campaign
from .replay_entry import (
    TemporaryReplayMod,
    create_request,
    remove_request_files,
    require_replay_request_healthy,
    wait_for_replay_entry,
)
from .report import write_report
from .replay_reporting import reported_cli
from .replay_run import ReplayRun
from .replay_fidelity import run_trajectory_comparison
from .trace_parser import (
    LogCursor,
    capture_log_offset,
    wait_for_boot_evidence,
    wait_for_correction_probe_evidence,
    wait_for_forced_qualification_evidence,
    wait_for_final_canonical_evidence,
    wait_for_gameplay_rng_coverage_evidence,
    wait_for_normal_render_rate_evidence,
    parse_qualification_stress_rate_evidence,
    wait_for_presentation_coverage_evidence,
    wait_for_presentation_identity_evidence,
    wait_for_qualification_health_evidence,
    wait_for_replay_lifecycle_evidence,
    wait_for_replay_metadata_evidence,
    wait_for_replay_seek_evidence,
    wait_for_stock_round_outcome_evidence,
)
from .tira_campaign import run_tira_campaign


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64")
DEFAULT_DLL = GAME_ROOT / "ue4ss" / "Mods" / "HorseMod" / "dlls" / "main.dll"
DEFAULT_CONFIG = GAME_ROOT / "ue4ss" / "Mods" / "HorseMod" / "dlls" / "rollback.ini"
DEFAULT_LOG = GAME_ROOT / "ue4ss" / "UE4SS.log"
DEFAULT_SCHEMA = ROOT / "build_cmake_LessEqual421__Shipping__Win64" / "HorseMod" / "generated" / "deterministic_contract.json"
DEFAULT_REPORT = ROOT / "tools" / "deterministic_qualification" / "output" / "boot-report.json"
DEFAULT_REPLAY_MOD = ROOT / "build_cmake_LessEqual421__Shipping__Win64" / "HorseMod" / "ReplayQualificationMod.dll"
DEFAULT_REPLAY_REPORT = ROOT / "tools" / "deterministic_qualification" / "output" / "replay-entry-report.json"
REPLAY_PROBE_TIMEOUT_SECONDS = 120.0
REPLAY_AUTHORED_OUTCOME_TIMEOUT_SECONDS = 300.0


def _default_replay_timeout(args: argparse.Namespace) -> float:
    """Keep short probes bounded while allowing an authored match to finish."""
    if bool(getattr(args, "development_tira_transition_smoke", False)):
        return REPLAY_PROBE_TIMEOUT_SECONDS
    full_authored_outcome = any(bool(getattr(args, field, False)) for field in (
            "stock_round_outcome_control", "require_authored_outcomes",
            "require_tira_stance_change",
            "require_tira_probability_transition"))
    certifying_full_baseline = (
        bool(getattr(args, "certifying", False))
        and not bool(getattr(args, "seek_percentages", ())))
    if full_authored_outcome or certifying_full_baseline:
        return REPLAY_AUTHORED_OUTCOME_TIMEOUT_SECONDS
    return REPLAY_PROBE_TIMEOUT_SECONDS


def _replay_outcome_policy(args: argparse.Namespace) -> tuple[bool, bool]:
    """Separate full-match outcome proof from active-phase strict seeking."""
    seeks = bool(args.seek_percentages)
    if args.require_authored_outcomes and seeks:
        raise RuntimeError(
            "authored outcomes and strict seeks require separate replay lifetimes")
    if (args.development_smoke
            or bool(getattr(args, "development_tira_transition_smoke", False))):
        return False, False
    require_authored = bool(
        args.require_authored_outcomes
        or args.require_tira_stance_change
        or args.require_tira_probability_transition
        or (args.certifying and not seeks)
    )
    # A certifying seek does not wait for the final result in its active replay
    # lifetime, but it remains bound to the exact same-replay stock oracle.
    require_control_artifact = require_authored or (
        args.certifying and seeks)
    return require_authored, require_control_artifact


@contextmanager
def _temporarily_armed_smoke_config(config: Path):
    """Arm observer hooks while preserving the caller's exact full-run config."""
    original = config.read_bytes()
    try:
        with armed_baseline(config):
            yield
    finally:
        temporary = config.with_suffix(config.suffix + ".smoke-restore.tmp")
        temporary.write_bytes(original)
        os.replace(temporary, config)






def required_file(path: Path, label: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"{label} not found: {resolved}")
    return resolved


def load_outcome_control(
    path: Path, replay: Path, dll: Path, replay_mod: Path,
    schema: Path, executable: Path, *, allow_noncertifying: bool = False,
) -> tuple[tuple[int, ...], int, dict[str, object], dict[str, object]]:
    control_path = required_file(path, "same-replay stock outcome control")
    data = json.loads(control_path.read_text(encoding="utf-8"))
    certifying = data.get("certifying") is True
    if (data.get("report_schema") != 2
            or (not certifying and not allow_noncertifying)
            or data.get("result") != "pass" or data.get("renderer") != "normal"):
        raise RuntimeError("stock outcome control is not a schema-v2 normal-render certifying pass")
    artifacts = data.get("artifacts", {})
    replay_artifact = artifacts.get("replay", {})
    if replay_artifact.get("sha256") != sha256_file(replay):
        raise RuntimeError("stock outcome control replay hash mismatch")
    required_identities = {
        "HorseMod DLL": (artifacts.get("horsemod_dll", {}).get("sha256"), sha256_file(dll)),
        "replay qualification mod": (
            artifacts.get("replay_qualification_mod", {}).get("sha256"),
            sha256_file(replay_mod),
        ),
        "generated schema": (
            artifacts.get("generated_schema", {}).get("sha256"), sha256_file(schema)
        ),
        "game executable": (
            artifacts.get("game_executable", {}).get("sha256"),
            sha256_file(executable),
        ),
        "capture harness": (
            artifacts.get("capture_harness_sha256",
                          artifacts.get("runner_sha256")),
            (capture_harness_sha256(ROOT)
             if "capture_harness_sha256" in artifacts else
             runner_sha256(Path(__file__).resolve().parent)),
        ),
    }
    development_mismatches: list[str] = []
    for label, (observed, expected) in required_identities.items():
        if observed != expected:
            if allow_noncertifying and label != "game executable":
                development_mismatches.append(label)
                continue
            raise RuntimeError(f"stock outcome control {label} hash mismatch")
    outcome = data.get("runtime", {}).get("stock_round_outcome")
    if not isinstance(outcome, dict):
        raise RuntimeError("stock outcome control is missing runtime outcomes")
    winners = tuple(int(value) for value in outcome.get("round_winners", ()))
    winner = int(outcome.get("match_winner", -1))
    if not winners or any(value not in (0, 1, 2) for value in winners):
        raise RuntimeError("stock outcome control has invalid round winners")
    if winner not in (0, 1) or outcome.get("rounds") != len(winners):
        raise RuntimeError("stock outcome control has invalid match outcome")
    loaded = artifacts.get("loaded_horsemod", {})
    if (loaded.get("verification") != "owned_process_mapped_file_and_sha256"
            or loaded.get("sha256") != artifacts.get("horsemod_dll", {}).get("sha256")
            or data.get("cleanup", {}).get("deployment_restored") is not True
            or data.get("cleanup", {}).get("game_processes_remaining") != 0):
        raise RuntimeError("stock outcome control lacks loaded-runtime or cleanup proof")
    logs = artifacts.get("raw_logs", {})
    if len(logs) != 1:
        raise RuntimeError("stock outcome control requires one sealed run log")
    raw = next(iter(logs.values()))
    raw_path = Path(raw["path"])
    if sha256_file(raw_path) != raw.get("sha256"):
        raise RuntimeError("stock outcome control raw log is stale")
    from .replay_fidelity import validate_payload_handoff, validate_recorded_outcome
    raw_text = raw_path.read_text(encoding="utf-8", errors="replace")
    run_id = data.get("runtime", {}).get("run_id")
    handoff = validate_payload_handoff(raw_text, run_id)
    recorded = validate_recorded_outcome(raw_text, run_id, winner)
    identity: dict[str, object] = {
        "path": str(control_path), "sha256": sha256_file(control_path)
    }
    if development_mismatches:
        identity["noncertifying_identity_mismatches"] = development_mismatches
    summary: dict[str, object] = {
        "source_commit": outcome.get("source_commit"),
        "rounds": len(winners),
        "match_winner": winner,
        "round_winners": list(winners),
        "payload_handoff": handoff,
        **recorded,
    }
    return winners, winner, identity, summary


def _outcome_proof_report_fields(
    live_outcome: object | None,
    bound_control_outcome: dict[str, object] | None,
    require_live_outcome: bool,
    require_control_artifact: bool,
) -> tuple[dict[str, object] | None, bool]:
    """Keep certifying seek reports bound to their independently run oracle."""
    required = require_live_outcome or require_control_artifact
    if live_outcome is not None:
        summary: dict[str, object] | None = {
            "source_commit": live_outcome.source_commit,
            "rounds": live_outcome.rounds,
            "match_winner": live_outcome.match_winner,
            "round_winners": list(live_outcome.round_winners),
        }
    else:
        summary = (None if bound_control_outcome is None
                   else dict(bound_control_outcome))
    if required and summary is None:
        raise RuntimeError("required authored outcome proof is missing")
    return summary, required




def run_boot(args: argparse.Namespace) -> int:
    dll = required_file(args.dll, "HorseMod DLL")
    config = required_file(args.config, "deterministic config")
    schema = required_file(args.schema, "generated schema")
    executable = required_file(args.game_executable, "SoulcaliburVI executable")
    identity = source_identity(ROOT)
    if identity["dirty"] and not args.allow_dirty:
        raise RuntimeError("source tree is dirty; boot evidence would not bind an immutable source state")

    existing_pid = find_game_pid()
    if existing_pid is not None:
        raise RuntimeError("SoulcaliburVI is already running; refusing ambiguous boot evidence")
    log_start = capture_log_offset(args.log)
    launch_game()
    pid = wait_for_game(args.timeout)
    try:
        evidence = wait_for_boot_evidence(
            args.log, args.timeout, lambda: require_game_process(pid), log_start
        )
        if evidence.source_commit != identity["commit"]:
            raise RuntimeError(
                f"deployed DLL source {evidence.source_commit} does not match HEAD {identity['commit']}"
            )
    finally:
        if not args.keep_game and find_game_pid() is not None:
            close_game(pid)

    report_data: dict[str, object] = {
        "report_schema": 1,
        "kind": "boot_probe",
        "certifying": False,
        "result": "pass",
        "reason": "boot provenance and hook installation only; no battle or replay was exercised",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source": identity,
        "case_id": args.case_id,
        "artifacts": {
            "horsemod_dll": {"path": str(dll), "sha256": sha256_file(dll)},
            "config": {"path": str(config), "sha256": sha256_file(config)},
            "generated_schema": {"path": str(schema), "sha256": sha256_file(schema)},
            "runner_sha256": runner_sha256(Path(__file__).resolve().parent),
            "game_executable": {"path": str(executable), "sha256": sha256_file(executable)},
        },
        "runtime": {
            "horsemod_version": evidence.version,
            "reported_source_commit": evidence.source_commit,
            "deterministic_lifecycle_hooks_armed": True,
            "clean_exit_requested": not args.keep_game,
        },
    }
    write_report(args.report, report_data)
    print(json.dumps(report_data, indent=2, sort_keys=True))
    print(f"report: {args.report.resolve()}")
    return 0


def _run_replay_entry_once(args: argparse.Namespace) -> int:
    # A source artifact hash is not proof of the module loaded by UE4SS.
    # Keep deployment ownership around each fresh process, including the
    # preliminary smoke, and restore it only after process cleanup.
    with ReplayRun(Path(args.dll), DEFAULT_DLL, Path(args.report)) as run:
        args._replay_run = run
        try:
            result = _capture_replay_entry_once(args)
        finally:
            args._failure_runtime_evidence = run.loaded_module
            args._failure_resource_journal = str(run.journal_path)
            del args._replay_run
    report = json.loads(Path(args.report).read_text(encoding="utf-8"))
    report["cleanup"] = {
        **report.get("cleanup", {}),
        "deployment_restored": run.resources.document["state"] == "clean",
        "game_processes_remaining": len(list_game_processes()),
    }
    write_report(Path(args.report), report)
    return result


def _capture_replay_entry_once(args: argparse.Namespace) -> int:
    dll = required_file(args.dll, "HorseMod DLL")
    replay_mod = required_file(args.replay_mod, "replay qualification mod")
    replay = required_file(args.replay, "replay payload")
    config = required_file(args.config, "deterministic config")
    schema = required_file(args.schema, "generated schema")
    executable = required_file(args.game_executable, "SoulcaliburVI executable")
    identity = source_identity(ROOT)
    if args.certifying:
        # Certification binds the complete native config contract. Remove
        # legacy/unknown keys that LoadConfig explicitly ignores so reports
        # describe exactly the configuration that can affect the runtime.
        canonicalize_contract(config)
    config_fields = {
        key.strip().casefold(): value.strip().casefold()
        for line in config.read_text(encoding="utf-8").splitlines()
        if (separator := line.partition("="))[1]
        for key, value in [(separator[0], separator[2])]
    }
    forced_depth7_requested = any(
        line.strip().casefold() == "forced_depth7_qualification=true"
        for line in config.read_text(encoding="utf-8").splitlines()
    )
    correction_probe_requested = any(
        line.strip().casefold() == "correction_probe=true"
        for line in config.read_text(encoding="utf-8").splitlines()
    )
    if forced_depth7_requested:
        _require_complete_forced_qualification_window(args)
    if args.development_smoke and args.certifying:
        raise RuntimeError("development smoke is non-certifying")
    if args.development_smoke and not 60 <= args.watch_frames <= 120:
        raise RuntimeError("development smoke must watch 60 to 120 replay frames")
    if identity["dirty"] and (args.certifying or not args.allow_dirty):
        raise RuntimeError(
            "source tree is dirty; replay-entry evidence would not bind an immutable source state"
        )
    if find_game_pid() is not None:
        raise RuntimeError("SoulcaliburVI is already running; refusing ambiguous replay evidence")

    run_id = ""
    pid: int | None = None
    forced = None
    seeks = ()
    boot = None
    lifecycle = None
    presentation_coverage = None
    presentation_identity = None
    qualification_health = None
    gameplay_rng_coverage = None
    normal_render_rate = None
    stock_round_outcome = None
    replay_metadata = None
    correction_probes = ()
    stock_round_outcome_control = args.stock_round_outcome_control or (
        not args.development_smoke
        and
        not args.deterministic_baseline
        and not forced_depth7_requested and not correction_probe_requested
        and not args.seek_percentages and args.stage_terminal is None
        and not args.require_authored_outcomes
        and not args.require_tira_stance_change
        and not args.require_tira_probability_transition)
    (require_authored_outcomes,
     require_outcome_control_artifact) = _replay_outcome_policy(args)
    expected_round_winners: tuple[int, ...] = ()
    expected_match_winner: int | None = None
    outcome_control_artifact: dict[str, object] | None = None
    bound_control_outcome: dict[str, object] | None = None
    if require_outcome_control_artifact and not stock_round_outcome_control:
        if args.outcome_control_report is None:
            raise RuntimeError(
                "deterministic outcome verification requires "
                "--outcome-control-report from the same replay")
        (expected_round_winners, expected_match_winner,
         outcome_control_artifact, bound_control_outcome) = load_outcome_control(
            args.outcome_control_report, replay, dll, replay_mod, schema, executable,
            allow_noncertifying=args.allow_dirty and not args.certifying,
        )
    final_canonical = None
    mods_root = GAME_ROOT / "ue4ss" / "Mods"
    graceful_exit_observed = False
    process_absent_after_exit = False
    with TemporaryReplayMod(replay_mod, mods_root):
        try:
            run_id = create_request(
                replay, args.watch_frames, tuple(args.seek_percentages),
                args.min_resume_tick_rate,
                args.watch_frames if args.development_smoke
                else args.resume_tick_window,
                args.stage_terminal,
                stock_round_outcome_control,
                require_authored_outcomes,
                expected_round_winners,
                expected_match_winner,
                args.development_smoke,
            )
            log_start = capture_log_offset(args.log)
            args._failure_log_start = log_start
            pid = args._replay_run.launch(executable)
            args._replay_run.wait_for_loaded_module(pid, args.timeout)
            def guard() -> None:
                args._replay_run.require_process(pid)
                require_replay_request_healthy(run_id)
            if not stock_round_outcome_control:
                boot = wait_for_boot_evidence(
                    args.log, args.timeout, guard, log_start
                )
            entry = wait_for_replay_entry(run_id, args.timeout, guard)
            args._replay_run.verify_loaded_module(pid)
            # Strict seeks still begin with a normal-render active window. Keep
            # that independent FPS/TPS proof in the report before requesting a
            # seek; resume-rate evidence measures a different boundary.
            normal_render_rate = wait_for_normal_render_rate_evidence(
                args.log, args.timeout, guard, log_start,
                require_active=not bool(args.seek_percentages),
            )
            minimum_rate_milli = round(args.min_resume_tick_rate * 1000)
            if (not normal_render_rate.independent_clocks
                    or normal_render_rate.fps_milli < minimum_rate_milli
                    or normal_render_rate.tick_rate_milli < minimum_rate_milli
                    or (normal_render_rate.active_window_observed
                        and (normal_render_rate.active_fps_milli
                                < minimum_rate_milli
                             or normal_render_rate.active_tick_rate_milli
                                < minimum_rate_milli))):
                raise RuntimeError(
                    "normal-render frame/tick rate was below the required "
                    f"{args.min_resume_tick_rate:.3f} Hz"
                )
            replay_metadata = wait_for_replay_metadata_evidence(
                args.log, args.timeout, guard, log_start)
            if not stock_round_outcome_control and not args.development_smoke:
                lifecycle = wait_for_replay_lifecycle_evidence(
                    args.log, args.timeout, guard, log_start
                )
            if stock_round_outcome_control or require_authored_outcomes:
                stock_round_outcome = wait_for_stock_round_outcome_evidence(
                    args.log, args.timeout, guard, log_start
                )
            if not stock_round_outcome_control and not args.development_smoke:
                presentation_coverage = wait_for_presentation_coverage_evidence(
                    args.log, args.timeout, guard, log_start
                )
                presentation_identity = wait_for_presentation_identity_evidence(
                    args.log, args.timeout, guard, log_start
                )
                qualification_health = wait_for_qualification_health_evidence(
                    args.log, args.timeout, guard, log_start
                )
                if (presentation_identity.failures != 0
                        or presentation_identity.audio_events == 0
                        or presentation_identity.order_events == 0
                        or presentation_identity.camera_batches == 0):
                    raise RuntimeError(
                        "replay presentation identity is empty or contains failures")
                gameplay_rng_coverage = wait_for_gameplay_rng_coverage_evidence(
                    args.log, args.timeout, guard, log_start
                )
                if gameplay_rng_coverage.unknown_callers != 0:
                    raise RuntimeError("unverified gameplay xorshift caller observed")
                if args.require_tira_probability_transition:
                    if (gameplay_rng_coverage.tira_probability_batches == 0
                            or gameplay_rng_coverage.tira_random_transitions == 0
                            or gameplay_rng_coverage.tira_targets == 0
                            or gameplay_rng_coverage.tira_stance_batches == 0
                            or gameplay_rng_coverage.tira_slot_mask == 0
                            or gameplay_rng_coverage.tira_writer_calls == 0
                            or gameplay_rng_coverage.tira_writer_sequence == 0
                            or gameplay_rng_coverage.tira_helper_attempts == 0
                            or gameplay_rng_coverage.tira_helper_exact_draws == 0
                            or gameplay_rng_coverage.tira_helper_writes == 0
                            or gameplay_rng_coverage.tira_helper_signature_failures != 0
                            or gameplay_rng_coverage.tira_writer_slot_mask == 0
                            or not gameplay_rng_coverage.state19_initial_valid):
                        raise RuntimeError(
                            "authored Tira helper 0x3250/0x3251 RNG/state19 "
                            "transition "
                            "was not observed"
                        )
                    transitioned_states = []
                    if gameplay_rng_coverage.tira_slot_mask & 1:
                        transitioned_states.append(
                            gameplay_rng_coverage.state19_at_tira_transition_p0)
                    if gameplay_rng_coverage.tira_slot_mask & 2:
                        transitioned_states.append(
                            gameplay_rng_coverage.state19_at_tira_transition_p1)
                    if (not transitioned_states
                            or any(state not in (0, 1)
                                   for state in transitioned_states)):
                        raise RuntimeError(
                            "Tira transition did not land in exact native "
                            "Gloomy/Jolly state19"
                        )
                if (args.require_tira_stance_change
                        and (gameplay_rng_coverage.tira_writer_calls == 0
                             or gameplay_rng_coverage.tira_writer_sequence == 0
                             or gameplay_rng_coverage.tira_writer_slot_mask == 0)):
                    raise RuntimeError(
                        "authored Tira state19 stance change was not observed"
                    )
                if correction_probe_requested:
                    correction_probes = wait_for_correction_probe_evidence(
                        args.log, args.timeout, guard, log_start
                    )
                final_canonical = wait_for_final_canonical_evidence(
                    args.log, args.timeout, guard, log_start
                )
            if args.seek_percentages:
                seeks = wait_for_replay_seek_evidence(
                    args.log, tuple(args.seek_percentages), args.timeout,
                    guard, log_start,
                )
                for seek in seeks:
                    if seek.resimulation_coordinates > 29:
                        raise RuntimeError("strict replay seek exceeded 29 coordinates")
                    if seek.validation_us > 500_000:
                        raise RuntimeError("strict replay seek validation exceeded 0.5 seconds")
                    if seek.resume_window < args.resume_tick_window:
                        raise RuntimeError("strict replay seek resume window was too short")
                    if seek.resume_tick_rate_milli < round(
                            args.min_resume_tick_rate * 1000):
                        raise RuntimeError("strict replay seek resume rate was too slow")
            if forced_depth7_requested:
                forced = wait_for_forced_qualification_evidence(
                    args.log, args.timeout, guard, log_start
                )
                if (forced.result != "passed" or forced.completed < 600
                        or forced.canonical_convergence != "exact"):
                    raise RuntimeError(
                        "forced depth-7 qualification failed: "
                        f"result={forced.result} completed={forced.completed} "
                        f"status={forced.status}"
                    )
                expected_depth = int(config_fields.get("qualification_depth", "7"))
                expected_location = int(config_fields.get("qualification_location", "2"))
                if forced.depth != expected_depth or forced.location != expected_location:
                    raise RuntimeError("forced correction depth/location identity mismatch")
                _require_forced_qualification_timing(forced)
                if args.require_presentation_coverage:
                    require_wall = args.stage_terminal in ("wall", "both")
                    require_barrier = args.stage_terminal in ("barrier", "both")
                    wall_ok = (not require_wall
                        or (presentation_coverage.stage_wall != 0
                            and presentation_coverage.stage_dispatch != 0
                            and forced.suppressed_stage_wall != 0
                            and forced.semantic_stage_dispatches != 0))
                    barrier_ok = (not require_barrier
                        or (presentation_coverage.stage_barrier != 0
                            and forced.suppressed_stage_barrier != 0))
                    # HorseMod cannot start a location=3 qualification until
                    # its native timeline has consumed a resolved hit.  The
                    # replay-mod RNG line is emitted at the end of the initial
                    # watch window and can legitimately precede that later
                    # start barrier, so it is not authoritative for this gate.
                    # The parsed forced result also binds the configured
                    # location below, proving the native barrier was admitted.
                    round_end_ok = (args.correction_location != "round_end"
                        or forced.round_terminal_source_stop_all != 0)
                    if (not wall_ok or not barrier_ok or not round_end_ok
                            or forced.presentation_failures != 0
                            or forced.presentation_terminal_coverage != "complete"):
                        raise RuntimeError(
                            "forced correction native/presentation coverage "
                            "is incomplete"
                        )
            args._replay_run.verify_loaded_module(pid)
            if boot is not None and boot.source_commit != identity["commit"]:
                raise RuntimeError(
                    f"deployed HorseMod source {boot.source_commit} does not match HEAD {identity['commit']}"
                )
            if (stock_round_outcome is not None
                    and stock_round_outcome.source_commit != identity["commit"]):
                raise RuntimeError(
                    "stock replay qualification mod source does not match "
                    "the current source commit"
                )
            if (lifecycle is not None
                    and lifecycle.source_commit != identity["commit"]):
                raise RuntimeError(
                    "replay qualification mod source does not match the current source commit"
                )
            if lifecycle is not None and not lifecycle.native_import_ready:
                raise RuntimeError("replay qualification native import contract was blocked")
        finally:
            if pid is not None and find_game_pid() is not None:
                try:
                    close_game(pid, timeout_seconds=30)
                    graceful_exit_observed = True
                    process_absent_after_exit = find_game_pid() is None
                except (RuntimeError, TimeoutError):
                    # Preserve the graceful-teardown failure as the run result,
                    # but first release the process-owned DLL handle so the
                    # context manager can remove its temporary mod exactly.
                    force_stop_game_for_cleanup(pid)
                    raise
            if run_id:
                remove_request_files(run_id)
    temporary_mod_removed = not (mods_root / "ReplayQualificationMod").exists()
    raw_path = Path(args.report).with_suffix(".UE4SS.log")
    raw_path.parent.mkdir(parents=True, exist_ok=True)
    # The process has exited. Preserve the actual run bytes before another
    # launch truncates UE4SS.log. A truncated log cannot support re-evaluation.
    raw = _read_bounded_log_since(Path(args.log), log_start, 64 * 1024 * 1024)
    if len(raw) >= 64 * 1024 * 1024:
        raise RuntimeError("replay raw log exceeds the capture bound")
    raw_path.write_bytes(raw)

    outcome_summary, outcome_proof_required = _outcome_proof_report_fields(
        stock_round_outcome, bound_control_outcome,
        require_authored_outcomes, require_outcome_control_artifact,
    )
    if stock_round_outcome is not None:
        from .replay_fidelity import validate_payload_handoff, validate_recorded_outcome
        raw_text = raw.decode("utf-8", errors="replace")
        outcome_summary["payload_handoff"] = validate_payload_handoff(raw_text, run_id)
        outcome_summary.update(validate_recorded_outcome(
            raw_text, run_id, stock_round_outcome.match_winner))
    report_data: dict[str, object] = {
        "report_schema": 2,
        "kind": ("replay_development_tira_transition_smoke"
                 if bool(getattr(
                     args, "development_tira_transition_smoke", False))
                 else "replay_development_smoke" if args.development_smoke
                 else "replay_entry_probe"),
        "certifying": bool(args.certifying),
        "result": "pass",
        "reason": ("bounded 600-frame normal-render Tira RNG transition smoke"
                   if bool(getattr(
                       args, "development_tira_transition_smoke", False))
                   else "bounded normal-render authored replay audio/ownership smoke"
                   if args.development_smoke else
                   "native replay import, stock launch request, and bounded "
                   "normal-play frame observation"),
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source": identity,
        "case_id": args.case_id,
        "row_id": args.row_id,
        "renderer": "normal",
        "display_map_name": args.display_map_name,
        "stage_package_root": args.stage_package_root,
        "artifacts": {
            "loaded_horsemod": args._replay_run.loaded_module,
            "raw_logs": {"game": {"path": str(raw_path.resolve()),
                "sha256": sha256_file(raw_path), "size": len(raw)}},
            "horsemod_dll": {"path": str(dll), "sha256": sha256_file(dll)},
            "replay_qualification_mod": {
                "path": str(replay_mod), "sha256": sha256_file(replay_mod)
            },
            "replay": {"path": str(replay), "sha256": sha256_file(replay)},
            "config": {"path": str(config), "sha256": sha256_file(config)},
            "config_fields": config_fields,
            "generated_schema": {"path": str(schema), "sha256": sha256_file(schema)},
            "horsemod_dll_sha256": sha256_file(dll),
            "schema_sha256": sha256_file(schema),
            "capture_harness_sha256": capture_harness_sha256(ROOT),
            "runner_sha256": runner_sha256(Path(__file__).resolve().parent),
            "game_executable": {"path": str(executable), "sha256": sha256_file(executable)},
            "stock_outcome_control": outcome_control_artifact,
        },
        "runtime": {
            "run_id": entry.run_id,
            "replay_metadata": None if replay_metadata is None else {
                "stage": replay_metadata.stage,
                "map": replay_metadata.map,
                "left_character": replay_metadata.left_character,
                "right_character": replay_metadata.right_character,
                "state_reset_records": replay_metadata.state_reset_records,
            },
            "canonical_convergence": (
                "exact" if forced is None else forced.canonical_convergence
            ),
            "final_canonical": None if final_canonical is None else {
                "generation": final_canonical.generation,
                "frame": final_canonical.frame,
                "sha256": final_canonical.sha256,
            },
            "capacity_failures": (None if qualification_health is None else
                                  qualification_health.capacity_failures),
            "capacity_growth_events": (None if qualification_health is None else
                                       qualification_health.capacity_growth_events),
            "timeline_accounting_failures": (
                None if qualification_health is None else
                qualification_health.timeline_accounting_failures),
            "cursor_mismatches": (
                None if qualification_health is None else
                qualification_health.cursor_mismatches),
            "batch_accounting_mismatches": (
                None if qualification_health is None else
                qualification_health.batch_accounting_mismatches),
            "round_transition_barriers": (
                None if qualification_health is None else
                qualification_health.round_transition_barriers),
            "presentation_duplicate_failures": (
                None if qualification_health is None else
                qualification_health.presentation_duplicate_failures),
            "presentation_publish_failures": (
                None if qualification_health is None else
                qualification_health.presentation_publish_failures),
            "aggregate_owned_bytes": (None if qualification_health is None else
                                      qualification_health.aggregate_owned_bytes),
            "presentation_owned_bytes": (None if qualification_health is None else
                                         qualification_health.presentation_owned_bytes),
            "clean_exit": (graceful_exit_observed
                           and process_absent_after_exit
                           and temporary_mod_removed),
            "graceful_exit_observed": graceful_exit_observed,
            "process_absent_after_exit": process_absent_after_exit,
            "reentry": False,
            "corrections": 0 if forced is None else forced.completed,
            "consecutive_corrections": 0 if forced is None else forced.completed,
            "depth": 0 if forced is None else forced.depth,
            "location": args.correction_location,
            "presentation": {
                "ordered_audio_payload_ids": (
                    presentation_identity is not None
                    and presentation_identity.audio_events > 0
                    and presentation_identity.failures == 0
                ) if forced is None else (
                    forced.audio_batches_verified > 0
                    and forced.audio_sequence_mismatches == 0
                ),
                "ephemeral_exactly_once": (
                    presentation_identity is not None
                    and presentation_identity.order_events > 0
                    and presentation_identity.failures == 0
                ) if forced is None else (
                    forced.journal_duplicates == 0
                    and forced.journal_publish_failures == 0
                ),
                "persistent_final_exact": (
                    presentation_identity is not None
                    and presentation_identity.camera_identity != 0
                    and presentation_identity.camera_batches > 0
                    and presentation_identity.failures == 0
                ) if forced is None else (
                    forced.camera_batches_verified > 0
                    and forced.camera_publication_mismatches == 0
                    and forced.presentation_failures == 0
                ),
                "leaks": 0 if forced is None else (
                    forced.journal_pending + forced.journal_payload_bytes
                ),
                "required_activity": 0 if forced is None else (
                    forced.audio_batches_verified
                    + forced.suppressed_particle_spawn
                    + forced.suppressed_stage_wall
                    + forced.suppressed_stage_barrier
                ),
                "terminal_coverage": "not_applicable" if forced is None
                    else forced.presentation_terminal_coverage,
                "identity": None if presentation_identity is None else {
                    "batches": presentation_identity.batches,
                    "audio_events": presentation_identity.audio_events,
                    "audio_identity": f"0x{presentation_identity.audio_identity:016x}",
                    "order_events": presentation_identity.order_events,
                    "order_identity": f"0x{presentation_identity.order_identity:016x}",
                    "camera_identity": f"0x{presentation_identity.camera_identity:016x}",
                    "camera_batches": presentation_identity.camera_batches,
                    "failures": presentation_identity.failures,
                    "journal_committed": presentation_identity.journal_committed,
                },
            },
            "performance": {
                "capture_p99_us": 0 if forced is None else forced.capture_p99_us,
                "capture_max_us": 0 if forced is None else forced.capture_max_us,
                "correction_p99_us": 0 if forced is None else forced.cycle_p99_us,
                "correction_max_us": 0 if forced is None else forced.cycle_max_us,
                "normal_render_frames": (
                    None if normal_render_rate is None else normal_render_rate.frames
                ),
                "normal_render_elapsed_us": (
                    None if normal_render_rate is None else normal_render_rate.elapsed_us
                ),
                "normal_render_fps": (
                    None if normal_render_rate is None else
                    normal_render_rate.fps_milli / 1000.0
                ),
                "normal_render_tick_rate": (
                    None if normal_render_rate is None else
                    normal_render_rate.tick_rate_milli / 1000.0
                ),
                "independent_clocks": (
                    None if normal_render_rate is None else
                    normal_render_rate.independent_clocks
                ),
                "normal_render_forward_ticks": (
                    None if normal_render_rate is None else
                    normal_render_rate.forward_ticks
                ),
                "normal_render_owned_resim_ticks": (
                    None if normal_render_rate is None else
                    normal_render_rate.owned_ticks
                ),
                "active_battle_frames": (
                    None if (normal_render_rate is None
                             or not normal_render_rate.active_window_observed) else
                    normal_render_rate.active_frames
                ),
                "active_battle_elapsed_us": (
                    None if (normal_render_rate is None
                             or not normal_render_rate.active_window_observed) else
                    normal_render_rate.active_elapsed_us
                ),
                "active_battle_fps": (
                    None if (normal_render_rate is None
                             or not normal_render_rate.active_window_observed) else
                    normal_render_rate.active_fps_milli / 1000.0
                ),
                "active_battle_tick_rate": (
                    None if (normal_render_rate is None
                             or not normal_render_rate.active_window_observed) else
                    normal_render_rate.active_tick_rate_milli / 1000.0
                ),
            },
            "horsemod_version": None if boot is None else boot.version,
            "reported_source_commit": (
                identity["commit"] if boot is None else boot.source_commit
            ),
            "native_replay_import_ready": (
                None if lifecycle is None else lifecycle.native_import_ready
            ),
            "launch_requested": True,
            "watch_frames": args.watch_frames,
            "development_smoke": bool(args.development_smoke),
            "development_tira_transition_smoke": bool(getattr(
                args, "development_tira_transition_smoke", False)),
            "stage_terminal": args.stage_terminal,
            "seek_percentages": args.seek_percentages,
            "min_resume_tick_rate": args.min_resume_tick_rate,
            "resume_tick_window": args.resume_tick_window,
            "seeks": [
                {
                    "percentage": seek.percentage,
                    "target": seek.target,
                    "source_end": seek.source_end,
                    "history_verified": seek.history_verified,
                    "live_resumed": seek.live_resumed,
                    "resume_total": seek.resume_total,
                    "resimulation_coordinates": seek.resimulation_coordinates,
                    "validation_us": seek.validation_us,
                    "resume_window": seek.resume_window,
                    "resume_elapsed_us": seek.resume_elapsed_us,
                    "resume_tick_rate": seek.resume_tick_rate_milli / 1000.0,
                }
                for seek in seeks
            ],
            "stock_round_outcome": outcome_summary,
            "authored_outcomes_required": outcome_proof_required,
            "correction_probes": [
                {
                    "depth": probe.depth,
                    "base": probe.base,
                    "final": probe.final,
                    "batches": probe.batches,
                    "coordinates": probe.coordinates,
                    "total_us": probe.total_us,
                }
                for probe in correction_probes
            ],
            "presentation_source_coverage": None if presentation_coverage is None else {
                "stage_wall": presentation_coverage.stage_wall,
                "stage_barrier": presentation_coverage.stage_barrier,
                "stage_dispatch": presentation_coverage.stage_dispatch,
                "audio": presentation_coverage.audio,
                "audio_direct": presentation_coverage.audio_direct,
                "audio_remap": presentation_coverage.audio_remap,
                "audio_source": presentation_coverage.audio_source,
                "audio_stop_all": presentation_coverage.audio_stop_all,
                "audio_blueprint": presentation_coverage.audio_blueprint,
                "particle_spawn": presentation_coverage.particle_spawn,
            },
            "gameplay_rng_coverage": None if gameplay_rng_coverage is None else {
                "xorshift_draws": gameplay_rng_coverage.xorshift_draws,
                "known_callers": f"0x{gameplay_rng_coverage.known_callers:x}",
                "unknown_callers": gameplay_rng_coverage.unknown_callers,
                "weighted_draws": gameplay_rng_coverage.weighted_draws,
                "if_draws": gameplay_rng_coverage.if_draws,
                "short25_p0": gameplay_rng_coverage.short25_p0,
                "short25_p1": gameplay_rng_coverage.short25_p1,
                "probability_transition_batches":
                    gameplay_rng_coverage.probability_transition_batches,
                "state_changes_p0": gameplay_rng_coverage.state_changes_p0,
                "state_changes_p1": gameplay_rng_coverage.state_changes_p1,
                "probability_state_mask_p0":
                    f"0x{gameplay_rng_coverage.probability_state_mask_p0:060x}",
                "probability_state_mask_p1":
                    f"0x{gameplay_rng_coverage.probability_state_mask_p1:060x}",
                "transition07_calls": gameplay_rng_coverage.transition07_calls,
                "tira_random_transitions":
                    gameplay_rng_coverage.tira_random_transitions,
                "tira_rng_stance_changes":
                    gameplay_rng_coverage.tira_random_transitions,
                "tira_probability_batches":
                    gameplay_rng_coverage.tira_probability_batches,
                "tira_targets": f"0x{gameplay_rng_coverage.tira_targets:x}",
                "tira_last_target":
                    f"0x{gameplay_rng_coverage.tira_last_target:04x}",
                "xorshift_sequence":
                    f"0x{gameplay_rng_coverage.xorshift_sequence:016x}",
                "transition07_sequence":
                    f"0x{gameplay_rng_coverage.transition07_sequence:016x}",
                "resolved_hit_calls": gameplay_rng_coverage.resolved_hit_calls,
                "resolved_hit_sequence":
                    f"0x{gameplay_rng_coverage.resolved_hit_sequence:016x}",
                "tira_writer_calls": gameplay_rng_coverage.tira_writer_calls,
                "tira_stance_changes": gameplay_rng_coverage.tira_writer_calls,
                "tira_writer_sequence":
                    f"0x{gameplay_rng_coverage.tira_writer_sequence:016x}",
                "tira_writer_slot_mask":
                    f"0x{gameplay_rng_coverage.tira_writer_slot_mask:x}",
                "tira_last_writer_move":
                    f"0x{gameplay_rng_coverage.tira_last_writer_move:04x}",
                "tira_helper_attempts":
                    gameplay_rng_coverage.tira_helper_attempts,
                "tira_helper_exact_draws":
                    gameplay_rng_coverage.tira_helper_exact_draws,
                "tira_helper_writes":
                    gameplay_rng_coverage.tira_helper_writes,
                "tira_helper_no_write":
                    gameplay_rng_coverage.tira_helper_no_write,
                "tira_helper_no_change":
                    gameplay_rng_coverage.tira_helper_no_change,
                "tira_helper_signature_failures":
                    gameplay_rng_coverage.tira_helper_signature_failures,
                "tira_helper_last_enclosing_move":
                    f"0x{gameplay_rng_coverage.tira_helper_last_enclosing_move:04x}",
                "tira_helper_last_chance":
                    gameplay_rng_coverage.tira_helper_last_chance,
                "tira_helper_last_result":
                    gameplay_rng_coverage.tira_helper_last_result,
                "tira_helper_last_rejection_mask":
                    f"0x{gameplay_rng_coverage.tira_helper_last_rejection_mask:x}",
                "tira_sequence":
                    f"0x{gameplay_rng_coverage.tira_sequence:016x}",
                "tira_stance_batches": gameplay_rng_coverage.tira_stance_batches,
                "tira_slot_mask": f"0x{gameplay_rng_coverage.tira_slot_mask:x}",
                "state19_sequence_p0":
                    f"0x{gameplay_rng_coverage.state19_sequence_p0:016x}",
                "state19_sequence_p1":
                    f"0x{gameplay_rng_coverage.state19_sequence_p1:016x}",
                "state19_initial_p0": gameplay_rng_coverage.state19_initial_p0,
                "state19_initial_p1": gameplay_rng_coverage.state19_initial_p1,
                "state19_final_p0": gameplay_rng_coverage.state19_final_p0,
                "state19_final_p1": gameplay_rng_coverage.state19_final_p1,
                "xorshift_landing": [
                    f"0x{word:08x}" for word in gameplay_rng_coverage.xorshift_landing
                ],
                "state19_at_tira_transition_p0":
                    gameplay_rng_coverage.state19_at_tira_transition_p0,
                "state19_at_tira_transition_p1":
                    gameplay_rng_coverage.state19_at_tira_transition_p1,
                "state19_initial_valid":
                    gameplay_rng_coverage.state19_initial_valid,
            },
            "frame_fencepost_observed": lifecycle is not None,
            "temporary_mod_removed": temporary_mod_removed,
            "clean_exit_requested": True,
            "forced_depth7": None if forced is None else {
                "result": forced.result,
                "completed": forced.completed,
                "qualification_location": forced.location,
                "native_location_barrier_admitted": True,
                "canonical_convergence": forced.canonical_convergence,
                "presentation_terminal_coverage":
                    forced.presentation_terminal_coverage,
                "status": forced.status,
                "suppressed_stage_wall": forced.suppressed_stage_wall,
                "suppressed_stage_barrier": forced.suppressed_stage_barrier,
                "semantic_stage_dispatches": forced.semantic_stage_dispatches,
                "round_terminal_source_stop_all":
                    forced.round_terminal_source_stop_all,
                "suppressed_particle_spawn": forced.suppressed_particle_spawn,
                "presentation_failures": forced.presentation_failures,
            },
        },
    }
    write_report(args.report, report_data)
    print(json.dumps(report_data, indent=2, sort_keys=True))
    print(f"report: {args.report.resolve()}")
    return 0


def _require_complete_forced_qualification_window(
    args: argparse.Namespace,
) -> None:
    """Reject a rate sample that can terminate before the 600-correction gate."""
    if args.watch_frames < 600 or args.resume_tick_window < 600:
        raise RuntimeError(
            "forced correction qualification requires at least 600 authored "
            "watch frames and a 600-frame active FPS/TPS window"
        )


def _require_forced_qualification_timing(forced: object) -> None:
    """Apply the published production-path correction and capture ceilings."""
    if getattr(forced, "cycle_p99_us", 10**9) >= 16_670:
        raise RuntimeError("forced correction p99 exceeded 16.67 ms")
    if getattr(forced, "cycle_max_us", 10**9) >= 33_340:
        raise RuntimeError("forced correction maximum exceeded 33.34 ms")
    if getattr(forced, "capture_p99_us", 10**9) > 500:
        raise RuntimeError("forced checkpoint capture p99 exceeded 0.5 ms")
    if getattr(forced, "capture_max_us", 10**9) > 1_000:
        raise RuntimeError("forced checkpoint capture maximum exceeded 1 ms")


def _run_independent_seek_entries(
    args: argparse.Namespace, config: Path,
) -> int:
    percentages = tuple(args.seek_percentages)
    if len(percentages) < 2:
        raise RuntimeError("independent seek set requires multiple percentages")
    child_reports: list[tuple[int, Path, dict[str, object]]] = []
    for percentage in percentages:
        child_args = copy.copy(args)
        child_args.seek_percentages = [percentage]
        child_args.row_id = f"{args.row_id}__seek-{percentage}"
        child_args.report = args.report.with_name(
            f"{args.report.stem}.seek-{percentage}{args.report.suffix}")
        with armed_baseline(config):
            _run_replay_entry_once(child_args)
        data = json.loads(child_args.report.read_text(encoding="utf-8"))
        runtime = data.get("runtime", {})
        seeks = runtime.get("seeks", []) if isinstance(runtime, dict) else []
        if (data.get("result") != "pass" or len(seeks) != 1
                or seeks[0].get("percentage") != percentage
                or runtime.get("clean_exit") is not True):
            raise RuntimeError(
                f"independent strict seek {percentage}% did not pass cleanly")
        child_reports.append((percentage, child_args.report.resolve(), data))

    aggregate = copy.deepcopy(child_reports[0][2])
    def capture_identity(document):
        artifacts = document.get("artifacts", {})
        loaded = artifacts.get("loaded_horsemod", {})
        if (loaded.get("sha256") != artifacts.get("horsemod_dll", {}).get("sha256")
                or loaded.get("verification") != "owned_process_mapped_file_and_sha256"):
            raise RuntimeError("independent seek lacks verified loaded-runtime identity")
        return {key: value for key, value in artifacts.items()
                if key not in ("raw_logs", "loaded_horsemod")}
    first_artifacts = capture_identity(aggregate)
    first_metadata = aggregate.get("runtime", {}).get("replay_metadata")
    first_outcome = aggregate.get("runtime", {}).get("stock_round_outcome")
    for percentage, _, data in child_reports[1:]:
        if (capture_identity(data) != first_artifacts
                or data.get("runtime", {}).get("replay_metadata") != first_metadata
                or data.get("runtime", {}).get("stock_round_outcome") != first_outcome):
            raise RuntimeError(
                f"independent strict seek {percentage}% identity drifted")
    aggregate["artifacts"]["raw_logs"] = {
        f"seek-{percentage}-{label}": artifact
        for percentage, _, document in child_reports
        for label, artifact in document["artifacts"]["raw_logs"].items()}
    aggregate["artifacts"]["loaded_runtimes"] = [
        document["artifacts"]["loaded_horsemod"]
        for _, _, document in child_reports]

    runtime = aggregate["runtime"]
    runtime["run_id"] = None
    runtime["run_ids"] = [
        data["runtime"]["run_id"] for _, _, data in child_reports
    ]
    runtime["seek_percentages"] = list(percentages)
    runtime["seeks"] = [
        data["runtime"]["seeks"][0] for _, _, data in child_reports
    ]
    runtime["independent_seek_entries"] = True
    runtime["clean_exit"] = all(
        data["runtime"]["clean_exit"] is True for _, _, data in child_reports)
    runtime["graceful_exit_observed"] = all(
        data["runtime"]["graceful_exit_observed"] is True
        for _, _, data in child_reports)
    runtime["process_absent_after_exit"] = all(
        data["runtime"]["process_absent_after_exit"] is True
        for _, _, data in child_reports)
    runtime["temporary_mod_removed"] = all(
        data["runtime"]["temporary_mod_removed"] is True
        for _, _, data in child_reports)
    aggregate["kind"] = "replay_strict_seek_set"
    aggregate["reason"] = (
        "independent exact-map authored replay entries for every strict seek")
    aggregate["created_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    aggregate["row_id"] = args.row_id
    aggregate["artifacts"]["independent_seek_reports"] = [
        {
            "percentage": percentage,
            "path": str(path),
            "sha256": sha256_file(path),
        }
        for percentage, path, _ in child_reports
    ]
    write_report(args.report, aggregate)
    print(json.dumps(aggregate, indent=2, sort_keys=True))
    print(f"report: {args.report.resolve()}")
    return 0


def _run_baseline_payload(args: argparse.Namespace, config: Path) -> int:
    if len(getattr(args, "seek_percentages", ())) > 1:
        return _run_independent_seek_entries(args, config)
    with armed_baseline(config):
        return _run_replay_entry_once(args)


def run_replay_entry(args: argparse.Namespace) -> int:
    timeout_was_defaulted = getattr(args, "timeout", None) is None
    if timeout_was_defaulted:
        args.timeout = _default_replay_timeout(args)
    # Strict seeking consumes canonical/checkpoint history and cannot run from
    # the safe trace=false production config. Always use the reversible
    # deterministic-baseline arm scope, even if the caller omitted the flag.
    if getattr(args, "seek_percentages", ()):
        args.deterministic_baseline = True
    if bool(getattr(args, "development_tira_transition_smoke", False)):
        if (args.certifying or args.development_smoke
                or args.watch_frames != 600
                or not args.require_tira_probability_transition
                or args.stock_round_outcome_control
                or args.require_authored_outcomes
                or args.seek_percentages or args.stage_terminal):
            raise RuntimeError(
                "Tira transition smoke requires one non-certifying 600-frame "
                "probability-transition probe without outcomes, seeks, or terminals")
        args.deterministic_baseline = True
        args.skip_development_smoke = True
    if args.certifying and args.skip_development_smoke:
        raise RuntimeError("certifying deterministic baselines require the smoke preflight")
    if not 60 <= args.smoke_frames <= 120:
        raise RuntimeError("smoke frames must be between 60 and 120")
    if args.development_smoke:
        config = required_file(args.config, "deterministic config")
        with _temporarily_armed_smoke_config(config):
            return _run_replay_entry_once(args)
    if not args.deterministic_baseline:
        return _run_replay_entry_once(args)
    config = required_file(args.config, "deterministic config")
    if args.skip_development_smoke:
        return _run_baseline_payload(args, config)
    smoke_args = copy.copy(args)
    smoke_args.development_smoke = True
    smoke_args.deterministic_baseline = False
    smoke_args.certifying = False
    smoke_args.stock_round_outcome_control = False
    smoke_args.require_authored_outcomes = False
    smoke_args.require_tira_stance_change = False
    smoke_args.require_tira_probability_transition = False
    smoke_args.require_presentation_coverage = False
    smoke_args.outcome_control_report = None
    smoke_args.seek_percentages = []
    smoke_args.stage_terminal = None
    smoke_args.watch_frames = args.smoke_frames
    smoke_args.report = args.report.with_suffix(".smoke.json")
    if timeout_was_defaulted:
        smoke_args.timeout = REPLAY_PROBE_TIMEOUT_SECONDS
    # One lifecycle scope owns both processes. Direct CLI callers normally
    # start from the safe production config, while offline orchestration may
    # already hold an outer armed scope; armed_baseline is reversible in both
    # cases. The smoke helper preserves the exact full-run armed bytes around
    # its narrower request.
    with armed_baseline(config):
        with _temporarily_armed_smoke_config(config):
            _run_replay_entry_once(smoke_args)
    return _run_baseline_payload(args, config)


def run_replay_development_campaign(args: argparse.Namespace) -> int:
    replay_mod = required_file(args.replay_mod, "replay qualification mod")
    replay = required_file(args.replay, "replay payload")
    config = required_file(args.config, "deterministic config")
    require_disarmed(config)
    if find_game_pid() is not None:
        raise RuntimeError("SC6 is already running; persistent campaign requires clean entry")
    if not 60 <= args.watch_frames <= 120:
        raise RuntimeError("persistent smoke must watch 60 to 120 replay frames")
    if args.reentry_count < 2 or args.reentry_count > 32:
        raise RuntimeError("persistent re-entry count must be between 2 and 32")
    cycles: list[dict[str, object]] = []
    pid: int | None = None
    active_run_id = ""
    mods_root = GAME_ROOT / "ue4ss" / "Mods"
    with _temporarily_armed_smoke_config(config):
        with TemporaryReplayMod(replay_mod, mods_root):
            try:
                for cycle in range(args.reentry_count):
                    log_start = capture_log_offset(args.log)
                    args._failure_log_start = log_start
                    active_run_id = create_request(
                        replay, args.watch_frames, (), args.min_resume_tick_rate,
                        args.watch_frames, None, False, False, (), None, True,
                    )
                    if pid is None:
                        launch_game()
                        pid = wait_for_game(args.timeout)
                    def guard() -> None:
                        require_game_process(pid)
                        require_replay_request_healthy(active_run_id)
                    entry = wait_for_replay_entry(active_run_id, args.timeout, guard)
                    rate = wait_for_normal_render_rate_evidence(
                        args.log, args.timeout, guard, log_start,
                        source_bound=False)
                    metadata = wait_for_replay_metadata_evidence(
                        args.log, args.timeout, guard, log_start,
                        source_bound=False)
                    cycles.append({
                        "cycle": cycle + 1,
                        "run_id": entry.run_id,
                        "stage": metadata.stage,
                        "map": metadata.map,
                        "normal_render_frames": rate.frames,
                        "normal_render_tick_rate": rate.tick_rate_milli / 1000.0,
                        "active_battle_tick_rate":
                            rate.active_tick_rate_milli / 1000.0,
                    })
                    remove_request_files(active_run_id)
                    active_run_id = ""
            finally:
                if pid is not None and find_game_pid() is not None:
                    try:
                        close_game(pid)
                    except (RuntimeError, TimeoutError):
                        force_stop_game_for_cleanup(pid)
                        raise
                if active_run_id:
                    remove_request_files(active_run_id)
    report = {
        "report_schema": 2,
        "kind": "persistent_replay_development_campaign",
        "certifying": False,
        "result": "pass",
        "reason": "same-process normal-render authored replay re-entry smoke",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "display_map_name": args.display_map_name,
        "stage_package_root": args.stage_package_root,
        "reentry_count": len(cycles),
        "cycles": cycles,
        "cleanup": {
            "process_absent": find_game_pid() is None,
            "temporary_mod_removed": not (mods_root / "ReplayQualificationMod").exists(),
        },
    }
    write_report(args.report, report)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


def _qualification_log_text(log: Path, cursor: LogCursor) -> str:
    with log.open("rb") as stream:
        size = stream.seek(0, 2)
        start_offset = 0
        if cursor.offset <= size:
            stream.seek(0)
            prefix_matches = stream.read(len(cursor.prefix)) == cursor.prefix
            stream.seek(cursor.sentinel_offset)
            tail_matches = stream.read(len(cursor.sentinel)) == cursor.sentinel
            if prefix_matches and tail_matches:
                start_offset = cursor.offset
        stream.seek(start_offset)
        return stream.read().decode("utf-8", errors="replace")


def _qualification_cycle_lines(log: Path, cursor: LogCursor) -> list[dict[str, object]]:
    text = _qualification_log_text(log, cursor)
    terminal: dict[str, dict[str, object]] = {}
    cleanup: dict[str, dict[str, object]] = {}
    for line in text.splitlines():
        if "[ReplayQualification] qualification cycle terminal " in line:
            target = terminal
        elif "[ReplayQualification] qualification cycle cleanup passed " in line:
            target = cleanup
        else:
            continue
        fields: dict[str, object] = {}
        for key, value in re.findall(r"([a-z_][a-z0-9_]*)=([^ ]+)", line):
            try:
                fields[key] = int(value, 16 if value.startswith("0x") else 10)
            except ValueError:
                fields[key] = value
        run_id = str(fields.get("run_id", ""))
        if run_id:
            target[run_id] = fields
    result: list[dict[str, object]] = []
    for run_id, fields in terminal.items():
        combined = dict(fields)
        combined["cleanup"] = cleanup.get(run_id)
        result.append(combined)
    return result


def _log_text_since(log: Path, cursor: LogCursor) -> str:
    return _qualification_log_text(log, cursor)


def run_replay_qualification_campaign(args: argparse.Namespace) -> int:
    dll = required_file(args.dll, "HorseMod DLL")
    replay_mod = required_file(args.replay_mod, "replay qualification mod")
    replay = required_file(args.replay, "replay payload")
    config = required_file(args.config, "deterministic config")
    schema = required_file(args.schema, "generated schema")
    executable = required_file(args.game_executable, "SoulcaliburVI executable")
    require_disarmed(config)
    event_locked = (args.cycle in (
        [[11, 5], [1, 5], [6, 5]],
        [[11, 6], [1, 6], [6, 6]],
        [(11, 5), (1, 5), (6, 5)],
        [(11, 6), (1, 6), (6, 6)],
    ))
    event_shape = ((args.cycle and args.cycle[0][1] == 5
                    and args.anchors == 1 and args.repeats == 15)
                   or (args.cycle and args.cycle[0][1] == 6
                       and args.anchors == 15 and args.repeats == 1))
    if (args.certifying
            and (args.anchors != 40 or args.repeats != 15)
            and not (event_locked and event_shape)):
        raise RuntimeError(
            "certifying qualification requires 40x15, Tira same-event "
            "stress 1x15, or Tira production cadence 15x1")
    identity = source_identity(ROOT)
    if identity["dirty"] and (args.certifying or not args.allow_dirty):
        raise RuntimeError("qualification campaign requires immutable source or --allow-dirty")
    if find_game_pid() is not None:
        raise RuntimeError("SC6 is already running; campaign requires clean process entry")
    requested_cycles = args.cycle or [(11, 1), (1, 1), (6, 1)]
    cycle_specs = tuple((uuid.uuid4().hex, depth, location)
                        for depth, location in requested_cycles)
    if not cycle_specs:
        raise RuntimeError("at least one --cycle DEPTH LOCATION is required")
    if len(cycle_specs) % 3:
        raise RuntimeError(
            "qualification cycles must be depth 11, 1, 6 triplets")
    for index in range(0, len(cycle_specs), 3):
        group = cycle_specs[index:index + 3]
        if ([item[1] for item in group] != [11, 1, 6]
                or len({item[2] for item in group}) != 1):
            raise RuntimeError(
                "qualification cycles must be depth 11, 1, 6 at one location")
    pid: int | None = None
    parent_run_id = ""
    parent_run_ids: list[str] = []
    cycles: list[dict[str, object]] = []
    rates = []
    stress_rates = []
    metadata_entries = []
    boot = None
    evidence_lines: list[str] = []
    mods_root = GAME_ROOT / "ue4ss" / "Mods"
    armed_config_sha256 = ""
    armed_config_fields: dict[str, str] = {}
    try:
        with _temporarily_armed_smoke_config(config):
            armed_config_sha256 = sha256_file(config)
            armed_config_fields = read_fields(config)
            with TemporaryReplayMod(replay_mod, mods_root):
                try:
                    groups = [cycle_specs]
                    for replay_entry_index, entry_specs in enumerate(groups, 1):
                        log_start = capture_log_offset(args.log)
                        args._failure_log_start = log_start
                        parent_run_id = create_request(
                            replay, 120, (), args.min_resume_tick_rate,
                            args.performance_window,
                            stock_round_outcome_control=False,
                            qualification_cycles=entry_specs,
                            qualification_anchors=args.anchors,
                            qualification_repeats=args.repeats,
                        )
                        parent_run_ids.append(parent_run_id)
                        if pid is None:
                            launch_game_executable(executable)
                            pid = wait_for_game(args.timeout)

                        def guard() -> None:
                            require_game_process(pid)
                            require_replay_request_healthy(parent_run_id)

                        if boot is None:
                            boot = wait_for_boot_evidence(
                                args.log, args.timeout, guard, log_start)
                        entry = wait_for_replay_entry(
                            parent_run_id, args.timeout, guard)
                        rate = wait_for_normal_render_rate_evidence(
                            args.log, args.timeout, guard, log_start,
                            source_bound=replay_entry_index == 1)
                        minimum_rate_milli = round(
                            args.min_resume_tick_rate * 1000)
                        if (not rate.independent_clocks
                                or min(rate.fps_milli,
                                       rate.tick_rate_milli,
                                       rate.active_fps_milli,
                                       rate.active_tick_rate_milli)
                                    < minimum_rate_milli):
                            raise RuntimeError(
                                "persistent qualification independent "
                                "FPS/TPS canary failed")
                        metadata = wait_for_replay_metadata_evidence(
                            args.log, args.timeout, guard, log_start,
                            source_bound=replay_entry_index == 1)
                        entry_cycles = _qualification_cycle_lines(
                            args.log, log_start)
                        run_text = _log_text_since(args.log, log_start)
                        stress_rates.extend(
                            parse_qualification_stress_rate_evidence(
                                run_text,
                                source_bound=replay_entry_index == 1,
                            )
                        )
                        expected_world = (
                            "authored map world=World "
                            f"{args.stage_package_root}/Maps/"
                        )
                        if expected_world not in run_text:
                            raise RuntimeError(
                                "loaded authored map does not match the requested "
                                f"{args.display_map_name} package")
                        expected_ids = [run_id for run_id, _, _ in entry_specs]
                        if ([str(cycle.get("run_id", ""))
                             for cycle in entry_cycles] != expected_ids):
                            raise RuntimeError(
                                "cycle evidence is missing, duplicated, or out of order")
                        for cycle, (_, depth, location) in zip(
                                entry_cycles, entry_specs):
                            cleanup = cycle.get("cleanup")
                            activity = str(cycle.get(
                                "presentation_activity", "")).split("/")
                            activity_valid = (len(activity) == 3
                                and all(value.isdigit() for value in activity)
                                and int(activity[0]) > 0)
                            expected_corrections = args.anchors * args.repeats
                            if (cycle.get("depth") != depth
                                    or cycle.get("location") != location
                                    or cycle.get("status") != 3
                                    or cycle.get("completed")
                                        != (f"{expected_corrections}/"
                                            f"{expected_corrections}")
                                    or cycle.get("anchors")
                                        != f"{args.anchors}/{args.anchors}"
                                    or cycle.get("repeats") != args.repeats
                                    or not isinstance(
                                        cycle.get("anchor_hash"), int)
                                    or cycle.get("anchor_hash") == 0
                                    or cycle.get("failure") != 0
                                    or cycle.get("capacity_growth") != 0
                                    or cycle.get("duplicates") != 0
                                    or cycle.get("publish_failures") != 0
                                    or cycle.get("cycle_p99_us", 10**9) >= 16_670
                                    or cycle.get("cycle_max_us", 10**9) >= 33_340
                                    or cycle.get("pending") != "0/0"
                                    or cycle.get("terminal_coverage") != 1
                                    or not activity_valid
                                    or not isinstance(cleanup, dict)
                                    or cleanup.get("stale_mask") != 0
                                    or cleanup.get("pending") != "0/0"):
                                raise RuntimeError(
                                    "qualification cycle failed closed: "
                                    f"{cycle.get('run_id')}")
                            cycle["replay_entry"] = replay_entry_index
                            cycles.append(cycle)
                        for index in range(0, len(entry_cycles), 3):
                            if entry_cycles[index].get("location") == 6:
                                continue
                            hashes = {
                                cycle.get("anchor_hash")
                                for cycle in entry_cycles[index:index + 3]
                            }
                            if len(hashes) != 1:
                                raise RuntimeError(
                                    "grouped depth rows did not use identical anchors")
                        if entry.reason != "qualification_groups_passed":
                            raise RuntimeError(
                                "persistent qualification did not reach terminal pass")
                        rates.append(rate)
                        metadata_entries.append(metadata)
                        evidence_lines.extend(line for line in run_text.splitlines()
                            if ("[ReplayQualification] authored map world=" in line
                                or "[ReplayQualification] normal-render" in line
                                or "[ReplayQualification] qualification cycle" in line
                                or "[HorseMod] qualification cycle" in line
                                or "[HorseMod] forced correction qualification" in line))
                        remove_request_files(parent_run_id)
                        parent_run_id = ""
                    if boot is None or boot.source_commit != identity["commit"]:
                        raise RuntimeError(
                            "deployed DLL source identity does not match HEAD")
                finally:
                    if pid is not None and find_game_pid() is not None:
                        try:
                            close_game(pid)
                        except (RuntimeError, TimeoutError):
                            force_stop_game_for_cleanup(pid)
                            raise
                    if parent_run_id:
                        remove_request_files(parent_run_id)
    finally:
        if find_game_pid() is not None:
            force_stop_game_for_cleanup(find_game_pid())
    if not rates or not metadata_entries:
        raise RuntimeError("qualification campaign produced no replay evidence")
    metadata = metadata_entries[0]
    if any((item.stage, item.map, item.left_character, item.right_character)
           != (metadata.stage, metadata.map, metadata.left_character,
               metadata.right_character) for item in metadata_entries[1:]):
        raise RuntimeError("native replay metadata changed between same-process entries")
    evidence_log = args.report.with_suffix(".log")
    evidence_log.parent.mkdir(parents=True, exist_ok=True)
    evidence_log.write_text("\n".join(evidence_lines) + "\n", encoding="utf-8")
    report = {
        "report_schema": 2,
        "kind": "persistent_replay_qualification_campaign",
        "certifying": bool(args.certifying),
        "result": "pass",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source": identity,
        "case_id": args.case_id,
        "display_map_name": args.display_map_name,
        "stage_package_root": args.stage_package_root,
        "renderer": "normal",
        "parent_run_ids": parent_run_ids,
        "depth_order": [depth for _, depth, _ in cycle_specs],
        "qualification_anchors": args.anchors,
        "qualification_repeats_per_anchor": args.repeats,
        "qualification_performance_window": args.performance_window,
        "cycles": cycles,
        "runtime": {
            "process_launches": 1,
            "process_restarts": 0,
            "replay_entries": len(parent_run_ids),
            "normal_render_tick_rate": min(
                item.tick_rate_milli for item in rates) / 1000.0,
            "normal_render_fps": min(
                item.fps_milli for item in rates) / 1000.0,
            "active_battle_tick_rate": min(
                item.active_tick_rate_milli for item in rates) / 1000.0,
            "active_battle_fps": min(
                item.active_fps_milli for item in rates) / 1000.0,
            "independent_performance_clocks": all(
                item.independent_clocks for item in rates),
            "qualification_stress_windows": [
                {
                    "frames": item.frames,
                    "forward_ticks": item.forward_ticks,
                    "owned_resim_ticks": item.owned_ticks,
                    "elapsed_us": item.elapsed_us,
                    "fps": item.fps_milli / 1000.0,
                    "tick_rate": item.tick_rate_milli / 1000.0,
                }
                for item in stress_rates
            ],
            "native_stage": metadata.stage,
            "native_map": metadata.map,
            "native_left_character": metadata.left_character,
            "native_right_character": metadata.right_character,
        },
        "artifacts": {
            "horsemod_dll_sha256": sha256_file(dll),
            "schema_sha256": sha256_file(schema),
            "replay_qualification_mod": {
                "path": str(replay_mod), "sha256": sha256_file(replay_mod)},
            "runner_sha256": runner_sha256(Path(__file__).resolve().parent),
            "capture_harness_sha256": capture_harness_sha256(ROOT),
            "replay": {"path": str(replay), "sha256": sha256_file(replay)},
            "game_executable": {
                "path": str(executable), "sha256": sha256_file(executable)},
            "config": {"path": str(config), "sha256": armed_config_sha256},
            "config_fields": armed_config_fields,
            "armed_config_sha256": armed_config_sha256,
            "bounded_log": {
                "path": str(evidence_log.resolve()),
                "sha256": sha256_file(evidence_log),
                "size": evidence_log.stat().st_size,
            },
        },
        "cleanup": {
            "process_absent": find_game_pid() is None,
            "temporary_mod_removed": not (mods_root / "ReplayQualificationMod").exists(),
            "config_disarmed": True,
        },
    }
    require_disarmed(config)
    write_report(args.report, report)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Fail-closed HorseMod deterministic qualification runner"
    )
    parser.add_argument("--output", choices=("compact", "json", "full"), default="compact")
    subcommands = parser.add_subparsers(dest="command", required=True)
    boot = subcommands.add_parser(
        "boot", help="collect non-certifying DLL provenance and hook-install evidence"
    )
    boot.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    boot.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    boot.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    boot.add_argument("--log", type=Path, default=DEFAULT_LOG)
    boot.add_argument("--game-executable", type=Path, default=GAME_ROOT / "SoulcaliburVI.exe")
    boot.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    boot.add_argument("--timeout", type=float, default=60.0)
    boot.add_argument("--keep-game", action="store_true")
    boot.add_argument(
        "--allow-dirty",
        action="store_true",
        help="permit explicitly non-certifying diagnostic evidence from a dirty tree",
    )
    boot.set_defaults(handler=run_boot)
    replay = subcommands.add_parser(
        "replay-entry",
        help="collect non-certifying native replay-import and first-frame evidence",
    )
    replay.add_argument("--replay", type=Path, required=True)
    replay.add_argument("--replay-mod", type=Path, default=DEFAULT_REPLAY_MOD)
    replay.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    replay.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    replay.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    replay.add_argument("--log", type=Path, default=DEFAULT_LOG)
    replay.add_argument("--game-executable", type=Path, default=GAME_ROOT / "SoulcaliburVI.exe")
    replay.add_argument("--report", type=Path, default=DEFAULT_REPLAY_REPORT)
    replay.add_argument(
        "--timeout", type=float, default=None,
        help=("terminal wait budget; defaults to 120 seconds for bounded probes "
              "and 300 seconds for full authored-outcome runs"),
    )
    replay.add_argument("--watch-frames", type=int, default=1)
    replay.add_argument("--certifying", action="store_true")
    replay.add_argument("--deterministic-baseline", action="store_true")
    replay.add_argument(
        "--development-smoke", action="store_true",
        help=("run a non-certifying 60-120 frame normal-render gate after "
              "the authored replay becomes active"),
    )
    replay.add_argument(
        "--development-tira-transition-smoke", action="store_true",
        help=("run one non-certifying 600-frame normal-render Tira RNG helper "
              "transition gate without waiting for the full match outcome"),
    )
    replay.add_argument(
        "--smoke-frames", type=int, default=120,
        help="normal-render authored replay frames used by baseline preflight",
    )
    replay.add_argument(
        "--skip-development-smoke", action="store_true",
        help="development-only escape hatch; rejected for certifying baselines",
    )
    replay.add_argument("--stock-round-outcome-control", action="store_true")
    replay.add_argument(
        "--outcome-control-report",
        type=Path,
        help=("same-replay normal-render stock control report used as the "
              "ordered round/match outcome oracle"),
    )
    replay.add_argument(
        "--require-authored-outcomes",
        action="store_true",
        help=("verify every round winner and the final match winner against "
              "--outcome-control-report before collecting deterministic coverage"),
    )
    replay.add_argument("--case-id", default="")
    replay.add_argument("--row-id", default="")
    replay.add_argument("--display-map-name", default="")
    replay.add_argument("--stage-package-root", default="")
    replay.add_argument("--correction-location", default=None)
    replay.add_argument(
        "--seek-percentages",
        type=int,
        nargs="*",
        default=[],
        help="after the baseline, strictly seek to these percentages in order",
    )
    replay.add_argument(
        "--min-resume-tick-rate",
        type=float,
        default=58.0,
        help="minimum live native replay tick rate after every seek",
    )
    replay.add_argument(
        "--resume-tick-window",
        type=int,
        default=120,
        help="minimum live native-frame window measured after every seek",
    )
    replay.add_argument(
        "--stage-terminal",
        choices=("wall", "barrier", "both"),
        help=("arm one or both qualification-only native stage terminals at "
              "authoritative source-frame boundaries"),
    )
    replay.add_argument(
        "--allow-dirty",
        action="store_true",
        help="permit explicitly non-certifying diagnostic evidence from a dirty tree",
    )
    replay.add_argument(
        "--require-presentation-coverage",
        action="store_true",
        help=("require forced-qualification coverage for the requested "
              "authored stage terminal"),
    )
    replay.add_argument(
        "--require-tira-stance-change",
        action="store_true",
        help=("require at least one exact native Tira state19 0<->1 write; "
              "this includes deterministic authored routes such as 0x306F "
              "and RNG-owned helper routes"),
    )
    replay.add_argument(
        "--require-tira-probability-transition",
        action="store_true",
        help=("require Tira helper 0x3250 or 0x3251 to consume exactly one gameplay "
              "RNG draw and write an exact state19 0<->1 transition, attributed "
              "to its enclosing move on the same native source frame"),
    )
    replay.set_defaults(handler=run_replay_entry)
    development = subcommands.add_parser(
        "replay-development-campaign",
        help="run repeated non-certifying authored replay smoke in one SC6 process",
    )
    development.add_argument("--replay", type=Path, required=True)
    development.add_argument("--replay-mod", type=Path, default=DEFAULT_REPLAY_MOD)
    development.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    development.add_argument("--log", type=Path, default=DEFAULT_LOG)
    development.add_argument("--report", type=Path, required=True)
    development.add_argument("--timeout", type=float, default=120.0)
    development.add_argument("--watch-frames", type=int, default=120)
    development.add_argument("--reentry-count", type=int, default=3)
    development.add_argument("--min-resume-tick-rate", type=float, default=58.0)
    development.add_argument("--display-map-name", required=True)
    development.add_argument("--stage-package-root", required=True)
    development.set_defaults(handler=run_replay_development_campaign)
    qualification_campaign = subcommands.add_parser(
        "replay-qualification-campaign",
        help="run depth/location correction cycles in one SC6 process",
    )
    qualification_campaign.add_argument("--replay", type=Path, required=True)
    qualification_campaign.add_argument("--replay-mod", type=Path,
                                        default=DEFAULT_REPLAY_MOD)
    qualification_campaign.add_argument("--dll", type=Path, default=DEFAULT_DLL)
    qualification_campaign.add_argument("--config", type=Path,
                                        default=DEFAULT_CONFIG)
    qualification_campaign.add_argument("--schema", type=Path,
                                        default=DEFAULT_SCHEMA)
    qualification_campaign.add_argument("--log", type=Path, default=DEFAULT_LOG)
    qualification_campaign.add_argument("--game-executable", type=Path,
        default=GAME_ROOT / "SoulcaliburVI.exe")
    qualification_campaign.add_argument("--report", type=Path, required=True)
    qualification_campaign.add_argument("--timeout", type=float, default=600.0)
    qualification_campaign.add_argument("--min-resume-tick-rate", type=float,
                                        default=58.0)
    qualification_campaign.add_argument("--performance-window", type=int,
        default=600, choices=range(120, 3601), metavar="FRAMES",
        help="post-arm normal-render FPS/TPS recovery window")
    qualification_campaign.add_argument("--display-map-name", required=True)
    qualification_campaign.add_argument("--stage-package-root", required=True)
    qualification_campaign.add_argument("--case-id", default="")
    qualification_campaign.add_argument("--cycle", type=int, nargs=2,
        action="append", metavar=("DEPTH", "LOCATION"),
        help="repeatable; defaults to 11/1, 1/1, 6/1")
    qualification_campaign.add_argument("--anchors", type=int, default=40,
        choices=range(1, 41), metavar="N",
        help="authoritative replay anchors per depth/location group")
    qualification_campaign.add_argument("--repeats", type=int, default=15,
        choices=range(1, 16), metavar="N",
        help="repeated restores at each exact anchor")
    qualification_campaign.add_argument("--certifying", action="store_true")
    qualification_campaign.add_argument("--allow-dirty", action="store_true")
    qualification_campaign.set_defaults(
        handler=run_replay_qualification_campaign)
    offline = subcommands.add_parser(
        "offline-matrix",
        help="run the complete 39-row normal-render offline qualification campaign",
    )
    offline.add_argument("--case-manifest", type=Path,
        default=ROOT / "docs" / "investigations" / "deterministic-production-candidate-manifest.json")
    offline.add_argument("--dll", type=Path, required=True)
    offline.add_argument("--deployed-dll", type=Path, default=DEFAULT_DLL)
    offline.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    offline.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    offline.add_argument("--replay-mod", type=Path, default=DEFAULT_REPLAY_MOD)
    offline.add_argument("--game-executable", type=Path,
        default=GAME_ROOT / "SoulcaliburVI.exe")
    offline.add_argument("--log", type=Path, default=DEFAULT_LOG)
    offline.add_argument("--output-dir", type=Path, required=True)
    offline.add_argument("--report", type=Path, required=True)
    offline.add_argument("--timeout", type=float, default=1800.0)
    offline.set_defaults(handler=lambda args: run_offline_campaign(args, ROOT))
    tira = subcommands.add_parser(
        "tira-campaign",
        help="run the repeated exact-map authored Tira RNG qualification campaign",
    )
    tira.add_argument("--case-manifest", type=Path, required=True)
    tira.add_argument("--dll", type=Path, required=True)
    tira.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    tira.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    tira.add_argument("--replay-mod", type=Path, default=DEFAULT_REPLAY_MOD)
    tira.add_argument("--game-executable", type=Path,
        default=GAME_ROOT / "SoulcaliburVI.exe")
    tira.add_argument("--log", type=Path, default=DEFAULT_LOG)
    tira.add_argument("--output-dir", type=Path, required=True)
    tira.add_argument("--report", type=Path, required=True)
    tira.add_argument("--timeout", type=float, default=1800.0)
    tira.set_defaults(handler=lambda args: run_tira_campaign(args, ROOT))
    trajectory = subcommands.add_parser("replay-compare-trajectory")
    trajectory.add_argument("--reference", type=Path, required=True)
    trajectory.add_argument("--candidate", type=Path, required=True)
    trajectory.add_argument("--report", type=Path, required=True)
    trajectory.set_defaults(handler=run_trajectory_comparison)
    control = subcommands.add_parser("replay-trajectory-control")
    control.add_argument("--mode", choices=("stock", "runtime"), required=True)
    control.add_argument("--dll", type=Path, required=True)
    control.add_argument("--deployed-dll", type=Path, default=DEFAULT_DLL)
    control.add_argument("--replay-mod", type=Path, default=DEFAULT_REPLAY_MOD)
    control.add_argument("--replay", type=Path, required=True)
    control.add_argument("--game-executable", type=Path, default=GAME_ROOT / "SoulcaliburVI.exe")
    control.add_argument("--log", type=Path, default=DEFAULT_LOG)
    control.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    control.add_argument("--watch-frames", type=int, default=1200)
    control.add_argument("--timeout", type=float, default=300)
    control.add_argument("--report", type=Path, required=True)
    from .replay_control import run_replay_control
    control.set_defaults(handler=run_replay_control)
    for child in subcommands.choices.values():
        child.add_argument("--output", choices=("compact", "json", "full"), default=argparse.SUPPRESS)

    return parser


_TERMINAL_FAILURE_MARKERS = (
    "[ReplayQualification] fail-fast health",
    "[ReplayQualification] normal-render active battle rate failed",
    "owned replay seek request failed",
    "owned replay seek resume failed",
    "[HorseMod] forced depth-7 qualification failed",
    "authoritative battle-audio capture failed",
    "canonical divergence",
    "presentation publish",
    "lifecycle failure",
)

_DIAGNOSTIC_FAILURE_MARKERS = (
    "frame-fencepost observation failed",
    "canonical capture",
)


def _find_failure_line(lines: list[str]) -> tuple[int | None, str]:
    """Prefer a latched terminal failure over earlier recoverable diagnostics."""
    for markers in (_TERMINAL_FAILURE_MARKERS, _DIAGNOSTIC_FAILURE_MARKERS):
        for index, line in enumerate(lines):
            if any(marker.casefold() in line.casefold() for marker in markers):
                return index, line
    return None, ""


def _read_bounded_log_since(
    log: Path, cursor: LogCursor | int, maximum_bytes: int = 2 * 1024 * 1024,
) -> bytes:
    with log.open("rb") as stream:
        size = stream.seek(0, 2)
        if isinstance(cursor, int):
            start_offset = cursor if cursor <= size else 0
        elif cursor.offset <= size:
            stream.seek(0)
            prefix_matches = stream.read(len(cursor.prefix)) == cursor.prefix
            stream.seek(cursor.sentinel_offset)
            tail_matches = stream.read(len(cursor.sentinel)) == cursor.sentinel
            start_offset = cursor.offset if prefix_matches and tail_matches else 0
        else:
            start_offset = 0
        # UE4SS startup reflection output can exceed the read ceiling before a
        # late replay failure occurs. Keep the current run's tail in that case;
        # reading the first maximum_bytes silently discarded the terminal line.
        start_offset = max(start_offset, size - maximum_bytes)
        stream.seek(start_offset)
        return stream.read(maximum_bytes)


def _read_replay_diagnostic_cleanup(config: Path) -> dict[str, bool]:
    """Report cleanup state; resource owners perform mutations before reporting."""
    restored = {name: False for name in (
        "trace", "correction_probe", "forced_depth7_qualification")}
    try:
        lines = config.read_text(encoding="utf-8").splitlines()
    except OSError:
        return restored
    for line in lines:
        key, separator, value = line.partition("=")
        normalized = key.strip().casefold()
        if separator and normalized in restored:
            restored[normalized] = value.strip().casefold() == "false"
    return restored


def _write_compact_replay_failure(args: argparse.Namespace, error: BaseException) -> None:
    if getattr(args, "command", None) not in (
            "replay-entry", "replay-development-campaign",
            "replay-qualification-campaign"):
        return
    # This reporter also handles preflight failures before a run acquired any
    # resources. Never stop a process or modify config merely because it exists.
    restored = _read_replay_diagnostic_cleanup(Path(args.config))
    log = Path(args.log)
    bounded_lines: list[str] = []
    try:
        cursor = getattr(args, "_failure_log_start", 0)
        run_lines = _read_bounded_log_since(log, cursor).decode(
            "utf-8", errors="replace").splitlines()
        failure_index, first_failure = _find_failure_line(run_lines)
        if failure_index is None:
            bounded_lines = run_lines[-256:]
        else:
            bounded_lines = run_lines[
                max(0, failure_index - 64):failure_index + 192]
    except OSError:
        first_failure = ""
    diagnostic_fields = {
        key: value for key, value in re.findall(
            r"\b([A-Za-z][A-Za-z0-9_]*)=([^\s,]+)", first_failure)
    }
    log_artifact = Path(args.report).with_suffix(".failure.log")
    log_artifact.parent.mkdir(parents=True, exist_ok=True)
    log_artifact.write_text("\n".join(bounded_lines) + "\n", encoding="utf-8")
    report = {
        "report_schema": 2,
        "kind": "replay_entry_failure",
        "certifying": False,
        "result": "fail",
        "reason": str(error),
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "case_id": getattr(args, "case_id", ""),
        "row_id": getattr(args, "row_id", ""),
        "display_map_name": getattr(args, "display_map_name", ""),
        "stage_package_root": getattr(args, "stage_package_root", ""),
        "failure": {
            "first_failure_line": first_failure,
            "fields": diagnostic_fields,
            # Requested target and source frontier are not the observed
            # failing boundary. Older captures may legitimately lack it.
            "first_failing_frame": (
                diagnostic_fields.get("failure_frame")
                if diagnostic_fields.get("failure_frame") not in (None, "0")
                else diagnostic_fields.get("frame", diagnostic_fields.get("coordinate"))),
            "requested_target": diagnostic_fields.get("target"),
            "field_or_mask": diagnostic_fields.get(
                "field", diagnostic_fields.get(
                    "mask", diagnostic_fields.get(
                        "component_mask", diagnostic_fields.get(
                            "native_mask", diagnostic_fields.get(
                                "wind_mask", diagnostic_fields.get(
                                    "identity_issue")))))),
            "owner_selector": diagnostic_fields.get("owner_selector"),
            "owner_pointer": diagnostic_fields.get(
                "owner_pointer", diagnostic_fields.get("unresolved_owner")),
            "return_rva": diagnostic_fields.get(
                "return_rva", diagnostic_fields.get("caller_rva")),
            "graph_provenance": diagnostic_fields.get(
                "graph_provenance", diagnostic_fields.get("owner_stage")),
            "lifecycle_phase": diagnostic_fields.get(
                "lifecycle_phase", diagnostic_fields.get(
                    "phase", diagnostic_fields.get("owner_stage"))),
            "bounded_log": str(log_artifact.resolve()),
            "bounded_log_lines": len(bounded_lines),
        },
        "cleanup": {
            "process_absent": find_game_pid() is None,
            "diagnostic_flags_restored_false": restored,
        },
    }
    try:
        raw = _read_bounded_log_since(log,
            getattr(args, "_failure_log_start", 0), 64 * 1024 * 1024)
        raw_path = Path(args.report).with_suffix(".UE4SS.log")
        raw_path.write_bytes(raw)
        report["artifacts"] = {
            "loaded_horsemod": getattr(args, "_failure_runtime_evidence", None),
            "raw_log": {"path": str(raw_path.resolve()), "sha256": sha256_file(raw_path),
                        "complete_within_bound": len(raw) < 64 * 1024 * 1024},
        }
        journal_path = getattr(args, "_failure_resource_journal", None)
        if journal_path:
            journal = json.loads(Path(journal_path).read_text(encoding="utf-8"))
            report["cleanup"]["deployment_restored"] = journal["state"] == "clean"
    except OSError as seal_error:
        report["artifact_failure"] = str(seal_error)
    write_report(Path(args.report), report)


@reported_cli
def main() -> int:
    args: argparse.Namespace | None = None
    try:
        args = build_parser().parse_args()
        return int(args.handler(args))
    except (FileNotFoundError, RuntimeError, TimeoutError, subprocess.SubprocessError) as error:
        if args is not None:
            _write_compact_replay_failure(args, error)
        print(f"qualification failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
