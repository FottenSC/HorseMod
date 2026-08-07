#!/usr/bin/env python3
"""Produce or validate one trace-derived rollback qualification segment."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile
import time
from typing import Any

from rollback_beta_config import parse_bool, parse_profile_text, read_profile


REPORT_SCHEMA_VERSION = 2
TRACE_CONTRACT_VERSION = 1
CANDIDATE_SCHEMA_VERSION = 5
MATRIX_SCHEMA_VERSION = 1
HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")
TAG_KEYS = (
    "qualification_contract_version",
    "qualification_run_id",
    "qualification_case_id",
    "qualification_segment_id",
    "qualification_schedule_hash",
    "runtime_profile",
    "qualification_seed",
    "protocol_version",
    "snapshot_version",
    "qualification_role",
)
MONOTONIC_COUNTERS = (
    "fault_submitted", "fault_queued", "fault_delivered", "fault_dropped",
    "fault_duplicated", "fault_reordered", "fault_corrupted",
    "fault_spiked", "fault_burst_dropped", "test_worker_stalls_started",
    "test_worker_stalls_completed", "test_worker_stall_actual_ms",
    "network_packets_sent", "network_packets_received",
    "network_packets_authenticated", "network_packets_rejected",
    "network_packets_decode_rejected", "network_packets_route_rejected",
    "network_packets_replay_rejected", "round_generation",
    "stock_round_terminal_candidate_matches",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_identity(path: Path) -> dict[str, Any]:
    return {
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def load_json(path: Path, description: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"{description} is unreadable: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{description} must be a JSON object")
    return value


def load_matrix(path: Path) -> dict[str, Any]:
    matrix = load_json(path, "physical case matrix")
    if matrix.get("schema_version") != MATRIX_SCHEMA_VERSION:
        raise ValueError("physical case matrix schema is unsupported")
    if matrix.get("classification") != "rollback-physical-case-policy":
        raise ValueError("physical case matrix classification is invalid")
    if matrix.get("trace_contract_version") != TRACE_CONTRACT_VERSION:
        raise ValueError("physical case matrix trace contract is unsupported")
    cases = matrix.get("cases")
    if not isinstance(cases, dict) or not cases:
        raise ValueError("physical case matrix has no cases")
    for case_id, policy in cases.items():
        if not isinstance(case_id, str) or not isinstance(policy, dict):
            raise ValueError("physical case matrix contains an invalid case")
        if not isinstance(policy.get("runtime_profile"), str):
            raise ValueError(f"matrix case {case_id} has no runtime profile")
        segments = policy.get("segments")
        if not isinstance(segments, list) or not segments \
                or len(set(segments)) != len(segments) \
                or any(not isinstance(item, str) or not item for item in segments):
            raise ValueError(f"matrix case {case_id} has invalid segments")
    return matrix


def candidate_bindings(candidate_path: Path, dll: Path, profile: Path,
                       matrix: Path) -> dict[str, Any]:
    candidate = load_json(candidate_path, "candidate manifest")
    if candidate.get("schema_version") != CANDIDATE_SCHEMA_VERSION \
            or candidate.get("classification") != "rollback-beta-candidate":
        raise ValueError("candidate manifest schema/classification is invalid")
    dll_identity = candidate.get("dll")
    contract = candidate.get("qualification_contract")
    if not isinstance(dll_identity, dict) or not isinstance(contract, dict):
        raise ValueError("candidate manifest is missing DLL/contract identity")
    expected_dll = str(dll_identity.get("sha256", "")).lower()
    profile_identity = contract.get("beta_config_profile")
    matrix_identity = contract.get("physical_case_matrix")
    if not isinstance(profile_identity, dict) or not isinstance(matrix_identity, dict):
        raise ValueError("candidate contract is missing profile/matrix identity")
    expected_profile = str(profile_identity.get("sha256", "")).lower()
    expected_matrix = str(matrix_identity.get("sha256", "")).lower()
    actual = {
        "dll": sha256_file(dll),
        "profile": sha256_file(profile),
        "matrix": sha256_file(matrix),
    }
    if expected_dll != actual["dll"]:
        raise ValueError("candidate DLL hash does not match built DLL")
    if expected_profile != actual["profile"]:
        raise ValueError("candidate profile hash does not match exact profile")
    if expected_matrix != actual["matrix"]:
        raise ValueError("candidate matrix hash does not match case policy")
    values = parse_profile_text(read_profile(profile))
    if values.get("trace", "").strip().lower() not in {
            "1", "true", "yes", "on"}:
        raise ValueError("candidate beta profile must enable trace=true")
    if not parse_bool(values["enabled"]):
        raise ValueError("candidate beta profile is disabled")
    commit = str(candidate.get("git", {}).get("commit", "")).lower()
    if re.fullmatch(r"[0-9a-f]{40,64}", commit) is None:
        raise ValueError("candidate source commit is invalid")
    return {
        "candidate_manifest_sha256": sha256_file(candidate_path),
        "candidate_dll_sha256": actual["dll"],
        "beta_profile_sha256": actual["profile"],
        "physical_case_matrix_sha256": actual["matrix"],
        "source_commit": commit,
        "protocol_version": int(candidate.get("protocol_version", -1)),
        "snapshot_version": int(candidate.get("snapshot_version", -1)),
        "qualification_contract_sha256": str(contract.get("sha256", "")),
    }


def read_trace(path: Path) -> list[dict[str, Any]]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise ValueError(f"trace is unreadable: {exc}") from exc
    if not text or not text.endswith("\n"):
        raise ValueError("trace is empty or truncated")
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"trace line {line_number} is malformed: {exc}") from exc
        if not isinstance(event, dict):
            raise ValueError(f"trace line {line_number} is not an object")
        events.append(event)
    if not events:
        raise ValueError("trace has no events")
    identities = {(event.get("pid"), event.get("process_start_marker"))
                  for event in events}
    if len(identities) != 1 or None in next(iter(identities)):
        raise ValueError("trace mixes PID/start markers")
    previous_qpc = -1
    for event in events:
        qpc = event.get("ts_qpc")
        if not isinstance(qpc, int) or isinstance(qpc, bool) or qpc < previous_qpc:
            raise ValueError("trace timestamps are missing or non-monotonic")
        previous_qpc = qpc
    return events


def _integer(event: dict[str, Any], key: str) -> int:
    value = event.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ValueError(f"trace field {key} is missing or invalid")
    return value


def _delta(first: dict[str, Any], last: dict[str, Any], key: str) -> int:
    return _integer(last, key) - _integer(first, key)


def _tags(event: dict[str, Any]) -> dict[str, Any]:
    return {key: event.get(key) for key in TAG_KEYS}


def qualification_schedule_hash(case_id: str, segment_id: str,
                                seed: int, policy: dict[str, Any]) -> str:
    """Hash the immutable matrix schedule instead of accepting an operator claim."""
    value = {
        "case_id": case_id,
        "segment_id": segment_id,
        "seed": seed,
        "runtime_profile": policy["runtime_profile"],
        "requirements": policy["requirements"],
    }
    payload = json.dumps(value, sort_keys=True, separators=(",", ":")) \
        .encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _policy_failures(policy: dict[str, Any], segment_id: str,
                     evidence: dict[str, Any], terminal: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    if evidence["duration_seconds"] < float(policy["minimum_duration_seconds"]):
        failures.append("minimum-duration")
    if evidence["confirmed_frame_progress"] < int(policy["minimum_confirmed_frames"]):
        failures.append("minimum-confirmed-frames")
    if evidence["measured_rtt_us"] < int(policy.get("minimum_rtt_us", 0)):
        failures.append("minimum-measured-rtt")
    if evidence["measured_jitter_us"] < int(policy.get(
            "minimum_jitter_us", 0)):
        failures.append("minimum-measured-jitter")
    checks = {
        "clean_fault_counters": evidence["fault_total"] == 0,
        "measured_rtt": evidence["measured_rtt_us"] > 0,
        "measured_jitter": evidence["measured_jitter_us"] > 0,
        "queued_traffic": evidence["fault_queued"] > 0,
        "reordered_traffic": evidence["fault_reordered"] > 0,
        "dropped_traffic": evidence["fault_dropped"] > 0,
        "burst_drops": evidence["fault_burst_dropped"] > 0,
        "duplicated_traffic": evidence["fault_duplicated"] > 0,
        "corrupted_traffic": evidence["fault_corrupted"] > 0,
        "authenticated_rejection": evidence["network_packets_rejected"] > 0
            and evidence["network_packets_authenticated"] > 0,
        "completed_worker_stall": evidence["worker_stalls_completed"] > 0
            and evidence["worker_stalls_completed"] == evidence["worker_stalls_started"],
        "confirmed_progress": evidence["confirmed_frame_progress"] > 0,
        "two_rounds": evidence["round_generation_progress"] >= 2,
        "completed_match": evidence["round_terminals_matched"] >= 1,
        "content_identity": bool(terminal.get("content_sha256")),
        "fail_closed_then_distinct_recovery": (
            (segment_id == "fail-closed"
             and terminal.get("fail_closed") is True
             and terminal.get("fatal_failure") is True)
            or (segment_id == "clean-lobby-recovery"
                and terminal.get("fatal_failure") is False
                and evidence["confirmed_frame_progress"] > 0)),
    }
    if "duplicate_only" in policy.get("requirements", []):
        unrelated = (evidence["fault_dropped"] + evidence["fault_reordered"]
                     + evidence["fault_corrupted"] + evidence["fault_spiked"]
                     + evidence["fault_burst_dropped"])
        checks["duplicate_only"] = unrelated == 0
    for requirement in policy.get("requirements", []):
        if not checks.get(requirement, False):
            failures.append(f"requirement:{requirement}")
    return failures


def analyze_trace(trace_path: Path, *, case_id: str, segment_id: str,
                  role: str, run_id: str, schedule_hash: str, seed: int,
                  policy: dict[str, Any], bindings: dict[str, Any]) -> dict[str, Any]:
    derived_schedule_hash = qualification_schedule_hash(
        case_id, segment_id, seed, policy)
    if schedule_hash != derived_schedule_hash:
        raise ValueError("schedule hash is not derived from matrix policy and seed")
    events = read_trace(trace_path)
    session = [event for event in events if event.get("event") == "session_start"]
    activations = [event for event in events
                   if event.get("event") == "rollback_qualification_activation"]
    terminals = [event for event in events
                 if event.get("event") == "rollback_qualification_terminal"]
    if len(session) != 1 or len(activations) != 1:
        raise ValueError("trace requires exactly one session and activation event")
    if len(terminals) != 1 or terminals[0] is not events[-1]:
        raise ValueError("trace requires exactly one final terminal event")
    activation, terminal = activations[0], terminals[0]
    activation_index = events.index(activation)
    terminal_index = len(events) - 1
    if activation_index >= terminal_index:
        raise ValueError("qualification activation does not precede terminal")
    statuses = [event for event in events[activation_index + 1:terminal_index]
                if event.get("event") == "rollback_production_status"]
    if len(statuses) < 2:
        raise ValueError("trace requires at least two in-segment status events")
    expected_tags: dict[str, Any] = {
        "qualification_contract_version": TRACE_CONTRACT_VERSION,
        "qualification_run_id": run_id,
        "qualification_case_id": case_id,
        "qualification_segment_id": segment_id,
        "qualification_schedule_hash": schedule_hash,
        "runtime_profile": policy["runtime_profile"],
        "qualification_seed": seed,
        "protocol_version": bindings["protocol_version"],
        "snapshot_version": bindings["snapshot_version"],
        "qualification_role": role,
    }
    for event in [activation, *statuses, terminal]:
        if _tags(event) != expected_tags:
            raise ValueError("qualification tags are missing or changed within trace")
    qpc_frequency = _integer(activation, "qpc_frequency")
    if qpc_frequency == 0:
        raise ValueError("qualification QPC frequency is zero")
    prior_tick = -1
    prior_generation = -1
    prior_confirmed = -1
    prior_counters = {key: -1 for key in MONOTONIC_COUNTERS}
    for status in statuses:
        tick = _integer(status, "service_tick")
        generation = _integer(status, "round_generation")
        confirmed = status.get("confirmed_frame")
        if not isinstance(confirmed, int) or isinstance(confirmed, bool):
            raise ValueError("confirmed_frame is missing or invalid")
        if tick < prior_tick or generation < prior_generation:
            raise ValueError("status ticks/round generations regress")
        if generation == prior_generation and confirmed >= 0 \
                and prior_confirmed >= 0 and confirmed < prior_confirmed:
            raise ValueError("confirmed frame regresses within a round")
        prior_tick, prior_generation = tick, generation
        prior_confirmed = confirmed if confirmed >= 0 else prior_confirmed
        for key in MONOTONIC_COUNTERS:
            value = _integer(status, key)
            if value < prior_counters[key]:
                raise ValueError(f"production counter {key} regresses")
            prior_counters[key] = value
    first, last = statuses[0], statuses[-1]
    for key in (*MONOTONIC_COUNTERS, "steam_route_current_rtt_us",
                "steam_route_jitter_us"):
        if _integer(terminal, key) != _integer(last, key):
            raise ValueError(f"terminal production counter {key} disagrees")
    duration = (terminal["ts_qpc"] - activation["ts_qpc"]) / qpc_frequency
    confirmed_progress = max(0, _integer(terminal, "confirmed_frames_total")
                             - _integer(activation, "confirmed_frames_total"))
    delta_keys = {
        "fault_queued": "fault_queued",
        "fault_dropped": "fault_dropped",
        "fault_duplicated": "fault_duplicated",
        "fault_reordered": "fault_reordered",
        "fault_corrupted": "fault_corrupted",
        "fault_spiked": "fault_spiked",
        "fault_burst_dropped": "fault_burst_dropped",
        "worker_stalls_started": "test_worker_stalls_started",
        "worker_stalls_completed": "test_worker_stalls_completed",
        "network_packets_authenticated": "network_packets_authenticated",
        "network_packets_rejected": "network_packets_rejected",
        "round_terminals_matched": "stock_round_terminal_candidate_matches",
    }
    evidence = {name: _delta(first, last, key)
                for name, key in delta_keys.items()}
    evidence.update({
        "duration_seconds": duration,
        "confirmed_frame_progress": confirmed_progress,
        "round_generation_progress": _integer(last, "round_generation")
            - _integer(first, "round_generation"),
        "measured_rtt_us": max(_integer(item, "steam_route_current_rtt_us")
                               for item in statuses),
        "measured_jitter_us": max(_integer(item, "steam_route_jitter_us")
                                  for item in statuses),
    })
    evidence["fault_total"] = sum(evidence[key] for key in (
        "fault_queued", "fault_dropped", "fault_duplicated",
        "fault_reordered", "fault_corrupted", "fault_spiked",
        "fault_burst_dropped"))
    failures = _policy_failures(policy, segment_id, evidence, terminal)
    expected_fail_closed = (
        case_id == "disconnect-reconnect" and segment_id == "fail-closed"
        and terminal.get("fail_closed") is True
        and terminal.get("fatal_failure") is True)
    if terminal.get("clean_shutdown") is not True and not expected_fail_closed:
        failures.append("terminal-not-clean")
    if terminal.get("fatal_failure") is True and not expected_fail_closed:
        failures.append("fatal-failure")
    if terminal.get("canonical_mismatches") != 0:
        failures.append("canonical-mismatch")
    pid, marker = events[0]["pid"], events[0]["process_start_marker"]
    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "classification": "rollback-physical-machine-segment",
        "ok": not failures,
        "failures": failures,
        "bindings": {
            **bindings,
            "physical_runner_sha256": sha256_file(Path(__file__).resolve()),
        },
        "qualification": {
            "run_id": run_id,
            "case_id": case_id,
            "segment_id": segment_id,
            "role": role,
            "seed": seed,
            "schedule_hash": schedule_hash,
            "runtime_profile": policy["runtime_profile"],
        },
        "process": {"pid": pid, "start_marker": marker,
                    "qpc_frequency": qpc_frequency},
        "session_identity": {key: terminal.get(key) for key in (
            "steam_lobby_id", "session_contract_hash",
            "steam_identity_accepted_selection_hash", "launch_stage_identity",
            "session_epoch", "steam_local_id", "steam_remote_id",
            "steam_owner_id", "local_player_slot")},
        "trace": file_identity(trace_path),
        "trace_bounds": {"first_qpc": events[0]["ts_qpc"],
                         "last_qpc": events[-1]["ts_qpc"],
                         "event_count": len(events),
                         "status_event_count": len(statuses)},
        "derived_evidence": evidence,
        "terminal": {key: terminal.get(key) for key in (
            "clean_shutdown", "fatal_failure", "canonical_mismatches",
            "fail_closed", "clean_lobby_recovery", "content_sha256",
            "terminal_failure")},
    }


def validate_report(report: object, trace_path: Path, **expected: Any) -> list[str]:
    if not isinstance(report, dict):
        return ["report-not-object"]
    if report.get("schema_version") != REPORT_SCHEMA_VERSION:
        return ["report-schema"]
    try:
        recomputed = analyze_trace(trace_path, **expected)
    except (OSError, UnicodeError, ValueError, KeyError) as exc:
        return [f"trace-analysis:{exc}"]
    return [] if report == recomputed else ["report-not-exactly-reproduced"]


def wait_for_terminal(path: Path, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file():
            try:
                events = read_trace(path)
                if events[-1].get("event") == "rollback_qualification_terminal":
                    return
            except ValueError:
                pass
        time.sleep(0.25)
    raise ValueError("timed out waiting for qualification terminal event")


def tag_request_file(path: Path, *, case_id: str, segment_id: str,
                     role: str, run_id: str, schedule_hash: str, seed: int,
                     policy: dict[str, Any], bindings: dict[str, Any]) -> None:
    """Atomically add analyzer-owned qualification tags to one request."""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise ValueError(f"qualification request template is unreadable: {exc}") \
            from exc
    keys = {
        line.split("=", 1)[0].strip().lower()
        for line in text.splitlines() if "=" in line
    }
    owned = {
        "qualification_contract_version", "qualification_run_id",
        "qualification_case_id", "qualification_segment_id",
        "qualification_schedule_hash", "qualification_content_sha256",
        "qualification_protocol_version", "qualification_snapshot_version",
    }
    if keys & owned:
        raise ValueError("request already contains qualification-owned tags")
    if "trace" in keys and not re.search(
            r"(?mi)^\s*trace\s*=\s*(1|true|yes|on)\s*$", text):
        raise ValueError("qualification request disables trace")
    suffix = (
        "\n# Trace-derived physical qualification tags.\n"
        f"qualification_contract_version={TRACE_CONTRACT_VERSION}\n"
        f"qualification_run_id={run_id}\n"
        f"qualification_case_id={case_id}\n"
        f"qualification_segment_id={segment_id}\n"
        f"qualification_schedule_hash={schedule_hash}\n"
        f"qualification_protocol_version={bindings['protocol_version']}\n"
        f"qualification_snapshot_version={bindings['snapshot_version']}\n"
        f"client_role={role}\n"
        f"network_profile={policy['runtime_profile']}\n"
        f"fault_seed={seed}\n"
        "trace=1\n"
    )
    temporary = path.with_suffix(path.suffix + ".qualification.tmp")
    temporary.write_text(text.rstrip() + suffix, encoding="utf-8")
    temporary.replace(path)


def _selftest_trace(tags: dict[str, Any], profile: str) -> str:
    base = {"pid": 41, "process_start_marker": 99, "build": "selftest"}
    counters = {key: 0 for key in MONOTONIC_COUNTERS}
    counters.update({"confirmed_frame": 0, "steam_route_current_rtt_us": 0,
                     "steam_route_jitter_us": 0})
    events = [
        {**base, "ts_qpc": 100, "event": "session_start"},
        {**base, **tags, "ts_qpc": 200,
         "event": "rollback_qualification_activation", "qpc_frequency": 10},
        {**base, **tags, **counters, "ts_qpc": 210,
         "event": "rollback_production_status", "service_tick": 1},
    ]
    counters.update({"confirmed_frame": 130,
                     "network_packets_authenticated": 2})
    events.append({**base, **tags, **counters, "ts_qpc": 520,
                   "event": "rollback_production_status", "service_tick": 2})
    events.append({**base, **tags, **counters, "ts_qpc": 530,
                   "event": "rollback_qualification_terminal",
                   "confirmed_frames_total": 130, "clean_shutdown": True,
                   "fatal_failure": False, "canonical_mismatches": 0,
                   "steam_lobby_id": "0x1", "session_contract_hash": "0x2",
                   "steam_identity_accepted_selection_hash": "0x3",
                   "launch_stage_identity": "0x4", "session_epoch": "0x5",
                   "steam_local_id": "0x6", "steam_remote_id": "0x7",
                   "steam_owner_id": "0x6", "local_player_slot": 0})
    events[1]["confirmed_frames_total"] = 0
    return "".join(json.dumps(event, sort_keys=True) + "\n" for event in events)


def selftest() -> int:
    with tempfile.TemporaryDirectory(prefix="rollback-physical-run-") as raw:
        root = Path(raw)
        policy = {"runtime_profile": "clean_0ms", "minimum_duration_seconds": 30,
                  "minimum_confirmed_frames": 120,
                  "requirements": ["clean_fault_counters"]}
        schedule_hash = qualification_schedule_hash(
            "clean", "active", 7, policy)
        tags = {
            "qualification_contract_version": TRACE_CONTRACT_VERSION,
            "qualification_run_id": "run-selftest",
            "qualification_case_id": "clean",
            "qualification_segment_id": "active",
            "qualification_schedule_hash": schedule_hash,
            "runtime_profile": "clean_0ms",
            "qualification_seed": 7,
            "protocol_version": 2,
            "snapshot_version": 38,
            "qualification_role": "host",
        }
        trace = root / "trace.jsonl"
        trace.write_text(_selftest_trace(tags, "clean_0ms"), encoding="utf-8")
        bindings = {"protocol_version": 2, "snapshot_version": 38,
                    "candidate_manifest_sha256": "b" * 64,
                    "candidate_dll_sha256": "c" * 64,
                    "beta_profile_sha256": "d" * 64,
                    "physical_case_matrix_sha256": "e" * 64,
                    "source_commit": "f" * 40,
                    "qualification_contract_sha256": "1" * 64}
        report = analyze_trace(trace, case_id="clean", segment_id="active",
                               role="host", run_id="run-selftest",
                               schedule_hash=schedule_hash, seed=7,
                               policy=policy, bindings=bindings)
        valid = report["ok"] and not validate_report(
            report, trace, case_id="clean", segment_id="active", role="host",
            run_id="run-selftest", schedule_hash=schedule_hash, seed=7,
            policy=policy, bindings=bindings)
        altered_report = json.loads(json.dumps(report))
        altered_report["derived_evidence"]["confirmed_frame_progress"] += 1
        altered_report_rejected = bool(validate_report(
            altered_report, trace, case_id="clean", segment_id="active",
            role="host", run_id="run-selftest",
            schedule_hash=schedule_hash, seed=7, policy=policy,
            bindings=bindings))
        one_line = root / "one-line.jsonl"
        one_line.write_text(json.dumps({"ok": True}) + "\n", encoding="utf-8")
        try:
            analyze_trace(one_line, case_id="clean", segment_id="active",
                          role="host", run_id="run-selftest",
                          schedule_hash=schedule_hash, seed=7, policy=policy,
                          bindings=bindings)
            one_line_rejected = False
        except ValueError:
            one_line_rejected = True
        truncated = root / "truncated.jsonl"
        truncated.write_text(trace.read_text(encoding="utf-8").rstrip("\n"),
                             encoding="utf-8")
        try:
            read_trace(truncated)
            truncated_rejected = False
        except ValueError:
            truncated_rejected = True
        mixed = root / "mixed.jsonl"
        mixed_text = trace.read_text(encoding="utf-8").replace(
            '"pid": 41', '"pid": 42', 1)
        mixed.write_text(mixed_text, encoding="utf-8")
        try:
            read_trace(mixed)
            mixed_rejected = False
        except ValueError:
            mixed_rejected = True
        changed_tags = root / "changed-tags.jsonl"
        changed_tags.write_text(
            trace.read_text(encoding="utf-8").replace(
                '"qualification_run_id": "run-selftest"',
                '"qualification_run_id": "changed"', 1),
            encoding="utf-8")
        try:
            analyze_trace(
                changed_tags, case_id="clean", segment_id="active",
                role="host", run_id="run-selftest",
                schedule_hash=schedule_hash, seed=7, policy=policy,
                bindings=bindings)
            changed_tags_rejected = False
        except ValueError:
            changed_tags_rejected = True
        matrix = load_matrix(Path(__file__).with_name(
            "rollback_physical_case_matrix.json"))
        threshold_cases_rejected = all(
            bool(_policy_failures(case_policy, case_policy["segments"][0], {
                "duration_seconds": -1,
                "confirmed_frame_progress": -1,
                "fault_total": 1,
                "measured_rtt_us": 0,
                "measured_jitter_us": 0,
                "fault_queued": 0,
                "fault_reordered": 0,
                "fault_dropped": 0,
                "fault_burst_dropped": 0,
                "fault_duplicated": 0,
                "fault_corrupted": 0,
                "fault_spiked": 0,
                "network_packets_rejected": 0,
                "network_packets_authenticated": 0,
                "worker_stalls_started": 0,
                "worker_stalls_completed": 0,
                "round_generation_progress": 0,
                "round_terminals_matched": 0,
            }, {}))
            for case_policy in matrix["cases"].values())
    ok = (valid and altered_report_rejected and one_line_rejected
          and truncated_rejected and mixed_rejected
          and changed_tags_rejected and threshold_cases_rejected)
    print("rollback physical case self-test "
          f"valid={int(valid)} altered_report_rejected="
          f"{int(altered_report_rejected)} "
          f"one_line_rejected={int(one_line_rejected)} "
          f"truncated_rejected={int(truncated_rejected)} "
          f"mixed_rejected={int(mixed_rejected)} "
          f"changed_tags_rejected={int(changed_tags_rejected)} "
          f"threshold_cases_rejected={int(threshold_cases_rejected)}")
    return 0 if ok else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--candidate-manifest", type=Path)
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--beta-profile", type=Path)
    parser.add_argument("--matrix", type=Path,
                        default=Path(__file__).with_name(
                            "rollback_physical_case_matrix.json"))
    parser.add_argument("--trace", type=Path)
    parser.add_argument(
        "--tag-request", type=Path,
        help="atomically add analyzer-owned tags before the game consumes "
             "this request file")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--case")
    parser.add_argument("--segment")
    parser.add_argument("--role", choices=("host", "guest"))
    parser.add_argument("--run-id")
    parser.add_argument("--seed", type=lambda value: int(value, 0))
    parser.add_argument("--wait", action="store_true")
    parser.add_argument("--timeout", type=float, default=900.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.selftest:
        return selftest()
    required = (args.candidate_manifest, args.dll, args.beta_profile,
                args.trace, args.report, args.case, args.segment, args.role,
                args.run_id, args.seed)
    if any(value is None for value in required):
        print("runner requires candidate, DLL, profile, trace, report, case, "
              "segment, role, run ID, and seed")
        return 2
    try:
        matrix = load_matrix(args.matrix.resolve())
        policy = matrix["cases"].get(args.case)
        if not isinstance(policy, dict) or args.segment not in policy["segments"]:
            raise ValueError("case/segment is not permitted by the matrix")
        bindings = candidate_bindings(
            args.candidate_manifest.resolve(), args.dll.resolve(),
            args.beta_profile.resolve(), args.matrix.resolve())
        schedule_hash = qualification_schedule_hash(
            args.case, args.segment, args.seed, policy)
        if args.tag_request is not None:
            tag_request_file(
                args.tag_request.resolve(), case_id=args.case,
                segment_id=args.segment, role=args.role, run_id=args.run_id,
                schedule_hash=schedule_hash, seed=args.seed,
                policy=policy, bindings=bindings)
        if args.wait:
            wait_for_terminal(args.trace.resolve(), args.timeout)
        report = analyze_trace(
            args.trace.resolve(), case_id=args.case, segment_id=args.segment,
            role=args.role, run_id=args.run_id,
            schedule_hash=schedule_hash, seed=args.seed,
            policy=policy, bindings=bindings)
    except (OSError, UnicodeError, ValueError, KeyError) as exc:
        print(f"physical qualification failed: {exc}")
        return 1
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"physical qualification report: {args.report} ok={report['ok']}")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
