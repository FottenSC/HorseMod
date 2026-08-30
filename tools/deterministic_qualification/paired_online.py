from __future__ import annotations

import datetime as dt
import json
import os
import re
import shutil
import subprocess
import time
import uuid
from pathlib import Path
from typing import Any

from .artifacts import (
    require_compiled_candidate_manifest, runner_sha256, sha256_file,
    source_identity, source_identity_sha256,
)
from .configuration import disarm_diagnostics, read_fields, write_fields
from .impairment import ClumsyImpairment
from .observer_pair import (
    ObserverPairPaths,
    cleanup_observer_pair,
    create_host_room_request,
    create_host_room_suppression,
    deploy_observer_pair,
    stop_observer_processes,
    validate_host_room_suppression,
    wait_for_host_room,
    wait_for_pair_processes,
)
from .process_control import list_game_processes
from .process_memory import PrivateMemoryTracker
from .report import write_report
from .sandboxie_pair import SandboxiePairSpec


STATUS = re.compile(
    r"\[ReplayQualification\] online qualification run_id=(?P<run>\S+) status=(?P<status>\d+)"
)
FAILURE = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) failed status=(?P<failure>\S+)"
)
HANDSHAKE = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) handshake map=(?P<map>\S+) "
    r"display_map=(?P<display>.*?) fighters=(?P<f0>\d{3})/(?P<f1>\d{3}) "
    r"local_slot=(?P<slot>[01]) loaded_map_sha256=(?P<loaded>[0-9a-f]{64})"
)
CONFIRMED = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) confirmed_hash generation=(?P<generation>\d+) "
    r"frame=(?P<frame>\d+) sha256=(?P<sha256>[0-9a-f]{64}) "
    r"checks=(?P<checks>\d+) corrections=(?P<corrections>\d+) "
    r"max_depth=(?P<depth>\d+) pending_events=(?P<pending>\d+) "
    r"presentation_bytes=(?P<presentation>\d+) checkpoint_bytes=(?P<checkpoint>\d+) "
    r"batch_entry_bytes=(?P<batch>\d+) timeline_owned_bytes=(?P<timeline_owned>\d+) "
    r"forced_snapshot_bytes=(?P<forced_snapshot>\d+) "
    r"presentation_owned_bytes=(?P<presentation_owned>\d+) "
    r"scratch_metadata_bytes=(?P<scratch_metadata>\d+) "
    r"aggregate_owned_bytes=(?P<aggregate_owned>\d+) "
    r"aggregate_limit=(?P<aggregate_limit>\d+) "
    r"post_status4_growth=(?P<post_status4_growth>\d+) "
    r"capacity_failures=(?P<capacity_failures>\d+) "
    r"correction_samples=(?P<correction_samples>\d+) "
    r"correction_p50_ns=(?P<correction_p50_ns>\d+) "
    r"correction_p95_ns=(?P<correction_p95_ns>\d+) "
    r"correction_p99_ns=(?P<correction_p99_ns>\d+) "
    r"correction_max_ns=(?P<correction_max_ns>\d+) "
    r"verified_audio_batches=(?P<verified_audio_batches>\d+) "
    r"audio_sequence_mismatches=(?P<audio_sequence_mismatches>\d+) "
    r"verified_camera_batches=(?P<verified_camera_batches>\d+) "
    r"camera_publication_mismatches=(?P<camera_publication_mismatches>\d+) "
    r"presentation_failures=(?P<presentation_failures>\d+) "
    r"journal_duplicates=(?P<journal_duplicates>\d+) "
    r"journal_publish_failures=(?P<journal_publish_failures>\d+) "
    r"journal_committed=(?P<journal_committed>\d+)"
)
ROUND = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) round_barrier .* rounds=(?P<rounds>\d+) "
    r"corrections=(?P<corrections>\d+)"
)
AUTHENTICATED = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) steam_p2p_authenticated "
    r"local_steamid64=(?P<local>\d+) peer_steamid64=(?P<peer>\d+) "
    r"session_key_established=1 transport=steam_legacy_p2p"
)
CLEANUP_STORAGE = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) cleanup_storage "
    r"pre_match_owned_bytes=(?P<pre>\d+) ending_owned_bytes=(?P<ending>\d+) "
    r"returned=(?P<returned>[01])"
)
EVENT = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) event=(?P<event>\S+) "
    r"generation=(?P<generation>\d+) frame=(?P<frame>\d+)"
)
TAKEOVER_EVENTS = (
    "session_content_resolved",
    "transport_authenticated",
    "local_baseline_ready",
    "bilateral_baseline_target",
    "local_baseline_frozen",
    "bilateral_baseline_acknowledged",
    "prefix_catchup_started",
    "prefix_catchup_completed",
    "first_owned_input",
)
ROUND_TAKEOVER_EVENTS = TAKEOVER_EVENTS[2:]


def _events_for_run(text: str, run_id: str) -> list[str]:
    return [match.group("event") for match in EVENT.finditer(text)
            if match.group("run") == run_id]


def _event_records_for_run(text: str, run_id: str) -> list[dict[str, Any]]:
    return [
        {
            "event": match.group("event"),
            "generation": int(match.group("generation")),
            "frame": int(match.group("frame")),
        }
        for match in EVENT.finditer(text) if match.group("run") == run_id
    ]


def _require_ordered_takeover(events: list[str], label: str) -> None:
    ownership_events = [event for event in events
                        if event in TAKEOVER_EVENTS]
    required = list(TAKEOVER_EVENTS) + list(ROUND_TAKEOVER_EVENTS)
    if ownership_events[:len(required)] != required:
        raise RuntimeError(
            f"{label} lacked two exact ordered ownership generations")


def _require_two_owned_generations(
    records: list[dict[str, Any]], confirmed: list[dict[str, Any]], label: str,
) -> None:
    first_owned = [record for record in records
                   if record["event"] == "first_owned_input"]
    if len(first_owned) < 2:
        raise RuntimeError(f"{label} lacked second-generation owned input")
    generations = [record["generation"] for record in first_owned[:2]]
    if generations[0] == 0 or generations[1] <= generations[0]:
        raise RuntimeError(f"{label} ownership generations did not advance")
    confirmed_generations = {record["generation"] for record in confirmed}
    if any(generation not in confirmed_generations for generation in generations):
        raise RuntimeError(
            f"{label} lacked a confirmed hash in each owned generation")
    first_generation_corrections = max(
        record["corrections"] for record in confirmed
        if record["generation"] == generations[0])
    second_generation_corrections = max(
        record["corrections"] for record in confirmed
        if record["generation"] == generations[1])
    if second_generation_corrections <= first_generation_corrections:
        raise RuntimeError(
            f"{label} lacked a real correction in the second owned generation")


def _qualification_failure_plan(
    impairment_profile: str, explicit_failure_case: str,
) -> tuple[str, dict[str, int]]:
    profile_failure_cases = {
        "corruption": "preownership_mismatch",
        "disconnect_pre": "preownership_disconnect",
        "disconnect_post": "postownership_disconnect",
    }
    failure_case = explicit_failure_case or profile_failure_cases.get(
        impairment_profile, "")
    native_faults = {
        "": {"host": 0, "sandbox": 0},
        "preownership_mismatch": {"host": 1, "sandbox": 0},
        "preownership_timeout": {"host": 2, "sandbox": 2},
        "preownership_disconnect": {"host": 0, "sandbox": 0},
        "postownership_auth": {"host": 3, "sandbox": 3},
        "postownership_hash": {"host": 4, "sandbox": 0},
        "postownership_restore": {"host": 5, "sandbox": 5},
        "postownership_peer": {"host": 6, "sandbox": 6},
        "postownership_disconnect": {"host": 0, "sandbox": 0},
    }
    if failure_case not in native_faults:
        raise RuntimeError(f"unsupported qualification failure case: {failure_case}")
    if explicit_failure_case and impairment_profile != "clean":
        raise RuntimeError("explicit native failure cases require the clean profile")
    if (failure_case == "preownership_disconnect"
            and impairment_profile != "disconnect_pre"):
        raise RuntimeError("preownership disconnect requires disconnect_pre")
    if (failure_case == "postownership_disconnect"
            and impairment_profile != "disconnect_post"):
        raise RuntimeError("postownership disconnect requires disconnect_post")
    faults = (native_faults[failure_case] if explicit_failure_case
              else {"host": 0, "sandbox": 0})
    return failure_case, faults


def _atomic_online_request(path: Path, run_id: str, not_before_ms: int,
                           qualification_fault: int) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".publish.tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(
            f"version=2\nrun_id={run_id}\n"
            f"not_before_unix_ms={not_before_ms}\n"
            f"qualification_fault={qualification_fault}\narm=true\n"
        )
        stream.flush()
        os.fsync(stream.fileno())
    return temporary


def _publish_pair(paths: ObserverPairPaths, run_ids: dict[str, str],
                  faults: dict[str, int]) -> None:
    not_before = int(time.time() * 1000) + 3000
    host = paths.host.qualification_root / "online_request.txt"
    sandbox = paths.sandbox.qualification_root / "online_request.txt"
    temporaries = [
        _atomic_online_request(host, run_ids["host"], not_before, faults["host"]),
        _atomic_online_request(
            sandbox, run_ids["sandbox"], not_before, faults["sandbox"]),
    ]
    try:
        os.replace(temporaries[0], host)
        os.replace(temporaries[1], sandbox)
    except BaseException:
        host.unlink(missing_ok=True)
        sandbox.unlink(missing_ok=True)
        raise


def _read_since(path: Path, offset: int) -> str:
    with path.open("rb") as stream:
        stream.seek(offset)
        return stream.read().decode("utf-8", errors="replace")


def _confirmed_convergence(latest: dict[str, dict[str, Any]]) -> dict[str, Any] | None:
    histories = {
        label: latest.get(label, {}).get("confirmed_history", [])
        for label in ("host", "sandbox")
    }
    if any(len(history) < 2 for history in histories.values()):
        return None
    if len(histories["host"]) != len(histories["sandbox"]):
        raise RuntimeError("peer confirmed-hash histories have unmatched trailing checks")
    for label, history in histories.items():
        coordinates = [(row["generation"], row["frame"]) for row in history]
        if len(set(coordinates)) != len(coordinates):
            raise RuntimeError(f"{label} repeated a confirmed canonical coordinate")
        for prior, current in zip(coordinates, coordinates[1:]):
            if current <= prior:
                raise RuntimeError(f"{label} confirmed canonical coordinates regressed")
            if current[0] == prior[0] and current[1] - prior[1] != 30:
                raise RuntimeError(f"{label} confirmed hash cadence was not 30 frames")
    host = histories["host"]
    sandbox = histories["sandbox"]
    shared = len(host)
    for index in range(shared):
        left, right = host[index], sandbox[index]
        if ((left["generation"], left["frame"])
                != (right["generation"], right["frame"])):
            raise RuntimeError("peer confirmed-hash coordinate sequences differ")
        if left["sha256"] != right["sha256"]:
            raise RuntimeError("peer confirmed canonical hashes diverged")
    return {
        "matched_checks": shared,
        "last_generation": host[shared - 1]["generation"],
        "last_frame": host[shared - 1]["frame"],
        "last_sha256": host[shared - 1]["sha256"],
        "cadence_frames": 30,
    }


def _wait_online(paths: ObserverPairPaths, run_ids: dict[str, str], case: dict[str, Any],
                 offsets: dict[str, int], timeout: float, phase_timeout: float,
                 expected_steam_ids: dict[str, tuple[int, int]],
                 guard: Any, minimum_active_seconds: float = 0.0,
                 on_active: Any | None = None,
                 ) -> tuple[dict[str, Any], dict[str, str]]:
    deadline = time.monotonic() + timeout
    latest: dict[str, dict[str, Any]] = {}
    highest_status = {"host": 0, "sandbox": 0}
    status_changed_at = {"host": time.monotonic(), "sandbox": time.monotonic()}
    active_started_at: float | None = None
    while time.monotonic() < deadline:
        guard()
        for label, peer in (("host", paths.host), ("sandbox", paths.sandbox)):
            text = _read_since(peer.log, offsets[label])
            run_id = run_ids[label]
            statuses = [int(m.group("status")) for m in STATUS.finditer(text)
                        if m.group("run") == run_id]
            handshakes = [m for m in HANDSHAKE.finditer(text)
                          if m.group("run") == run_id]
            confirmed = [m for m in CONFIRMED.finditer(text)
                         if m.group("run") == run_id]
            rounds = [m for m in ROUND.finditer(text)
                      if m.group("run") == run_id]
            authenticated = [m for m in AUTHENTICATED.finditer(text)
                             if m.group("run") == run_id]
            events = _events_for_run(text, run_id)
            event_records = _event_records_for_run(text, run_id)
            if 6 in statuses:
                raise RuntimeError(f"{label} online qualification entered failure status 6")
            prior = 0
            for status in statuses:
                if status < prior:
                    raise RuntimeError(f"{label} online status regressed")
                if status > prior + 1:
                    raise RuntimeError(f"{label} online status skipped required phase")
                prior = status
            if prior > highest_status[label]:
                highest_status[label] = prior
                status_changed_at[label] = time.monotonic()
            if (highest_status[label] in (2, 3, 4)
                    and time.monotonic() - status_changed_at[label] > phase_timeout):
                raise TimeoutError(
                    f"{label} online phase {highest_status[label]} exceeded "
                    f"{phase_timeout:g} seconds"
                )
            if handshakes:
                match = handshakes[-1]
                expected = (
                    match.group("map") == case["stage_package_root"]
                    and match.group("display") == case["native_display_name"]
                    and [match.group("f0"), match.group("f1")] == case["fighter_order"]
                )
                if not expected:
                    raise RuntimeError(f"{label} selected content does not match exact case")
            latest[label] = {
                "statuses": statuses,
                "handshake": None if not handshakes else handshakes[-1].groupdict(),
                "confirmed": None if not confirmed else {
                    key: (value if key == "sha256" else int(value))
                    for key, value in confirmed[-1].groupdict().items()
                    if key != "run"
                },
                "confirmed_history": [
                    {
                        key: (value if key == "sha256" else int(value))
                        for key, value in match.groupdict().items()
                        if key != "run"
                    }
                    for match in confirmed
                ],
                "rounds": 0 if not rounds else int(rounds[-1].group("rounds")),
                "authenticated": None if not authenticated
                    else authenticated[-1].groupdict(),
                "events": events,
                "event_records": event_records,
            }
            if authenticated:
                expected_local, expected_peer = expected_steam_ids[label]
                proof = authenticated[-1]
                if (int(proof.group("local")) != expected_local
                        or int(proof.group("peer")) != expected_peer):
                    raise RuntimeError(f"{label} Steam P2P identity proof mismatch")
        if all(5 in latest.get(label, {}).get("statuses", ())
               for label in ("host", "sandbox")):
            for label in ("host", "sandbox"):
                _require_ordered_takeover(latest[label]["events"], label)
            if active_started_at is None:
                active_started_at = time.monotonic()
                if on_active is not None:
                    on_active()
            if (minimum_active_seconds > 0
                    and any(7 in latest[label]["statuses"]
                            for label in ("host", "sandbox"))
                    and time.monotonic() - active_started_at
                        < minimum_active_seconds):
                raise RuntimeError("continuous soak returned to lobby before duration")
            metrics = [latest[label].get("confirmed") for label in ("host", "sandbox")]
            authentication_complete = all(
                latest[label].get("authenticated") is not None
                for label in ("host", "sandbox")
            )
            correction_complete = all(
                metric and metric["corrections"] > 0 and metric["checks"] > 0
                for metric in metrics
            )
            convergence = _confirmed_convergence(latest)
            presentation_complete = all(
                metric
                and metric["verified_audio_batches"] > 0
                and metric["audio_sequence_mismatches"] == 0
                and metric["verified_camera_batches"] > 0
                and metric["camera_publication_mismatches"] == 0
                and metric["presentation_failures"] == 0
                and metric["journal_duplicates"] == 0
                and metric["journal_publish_failures"] == 0
                and metric["journal_committed"] > 0
                for metric in metrics
            )
            ceilings_complete = all(
                metric
                and metric["timeline_owned"] <= 512 * 1024**2
                and metric["forced_snapshot"] <= 16 * 1024**2
                and metric["presentation"] <= 2 * 1024**2
                and metric["aggregate_owned"] <= 576 * 1024**2
                and metric["aggregate_limit"] == 576 * 1024**2
                and metric["post_status4_growth"] == 0
                and metric["capacity_failures"] == 0
                and metric["correction_samples"] == metric["corrections"]
                and metric["correction_p99_ns"] < 16_670_000
                for metric in metrics
            )
            rounds_complete = all(
                latest[label]["rounds"] >= 2 for label in ("host", "sandbox")
            )
            generations_complete = True
            for label in ("host", "sandbox"):
                try:
                    _require_two_owned_generations(
                        latest[label]["event_records"],
                        latest[label]["confirmed_history"], label)
                except RuntimeError:
                    generations_complete = False
            duration_complete = (
                time.monotonic() - active_started_at >= minimum_active_seconds)
            if (authentication_complete and correction_complete
                    and convergence is not None and presentation_complete
                    and ceilings_complete and rounds_complete
                    and generations_complete
                    and duration_complete):
                latest["convergence"] = convergence
                break
        time.sleep(0.25)
    else:
        raise TimeoutError("paired online match did not prove multi-round real corrections")
    print(
        f"{case['native_display_name']} passed owned multi-round correction activity. "
        "Return both games visibly to the Player Match lobby for teardown verification.",
        flush=True,
    )
    teardown_deadline = time.monotonic() + min(300.0, timeout)
    while time.monotonic() < teardown_deadline:
        guard()
        ready = True
        for label, peer in (("host", paths.host), ("sandbox", paths.sandbox)):
            text = _read_since(peer.log, offsets[label])
            run_id = run_ids[label]
            statuses = [int(m.group("status")) for m in STATUS.finditer(text)
                        if m.group("run") == run_id]
            events = _events_for_run(text, run_id)
            if 6 in statuses:
                raise RuntimeError(f"{label} failed during lobby teardown")
            cleanup = [match for match in CLEANUP_STORAGE.finditer(text)
                       if match.group("run") == run_id]
            if cleanup:
                proof = cleanup[-1]
                latest[label]["cleanup_storage"] = {
                    key: int(value) for key, value in proof.groupdict().items()
                    if key != "run"
                }
            ready = (ready and 7 in statuses and bool(cleanup)
                     and cleanup[-1].group("returned") == "1"
                     and cleanup[-1].group("pre") == cleanup[-1].group("ending")
                     and events[-2:] == ["cleanup_started", "cleanup_completed"])
        if ready:
            return latest, {
                "host": _read_since(paths.host.log, offsets["host"]),
                "sandbox": _read_since(paths.sandbox.log, offsets["sandbox"]),
            }
        time.sleep(0.25)
    raise TimeoutError("paired online teardown did not reach clear-for-stock status 7")


def _wait_expected_impairment_failure(
    paths: ObserverPairPaths, run_ids: dict[str, str], offsets: dict[str, int],
    timeout: float, guard: Any, profile: str, case: dict[str, Any],
    expected_steam_ids: dict[str, tuple[int, int]],
    start_post_ownership: Any | None = None,
) -> tuple[dict[str, Any], dict[str, str]]:
    deadline = time.monotonic() + timeout
    started_post = False
    latest: dict[str, Any] = {}
    while time.monotonic() < deadline:
        guard()
        for label, peer in (("host", paths.host), ("sandbox", paths.sandbox)):
            text = _read_since(peer.log, offsets[label])
            statuses = [int(match.group("status")) for match in STATUS.finditer(text)
                        if match.group("run") == run_ids[label]]
            failures = [match.group("failure") for match in FAILURE.finditer(text)
                        if match.group("run") == run_ids[label]]
            authentication = [match.groupdict() for match in AUTHENTICATED.finditer(text)
                              if match.group("run") == run_ids[label]]
            handshakes = [match.groupdict() for match in HANDSHAKE.finditer(text)
                          if match.group("run") == run_ids[label]]
            events = _events_for_run(text, run_ids[label])
            cleanup = [match for match in CLEANUP_STORAGE.finditer(text)
                       if match.group("run") == run_ids[label]]
            latest[label] = {
                "statuses": statuses, "failures": failures,
                "authenticated": authentication,
                "handshake": None if not handshakes else handshakes[-1],
                "cleanup_storage": None if not cleanup else {
                    key: int(value) for key, value in cleanup[-1].groupdict().items()
                    if key != "run"
                },
                "events": events,
            }
        if not started_post and all(
                5 in latest.get(label, {}).get("statuses", ())
                for label in ("host", "sandbox")):
            started_post = True
            if start_post_ownership is not None:
                start_post_ownership()
        if all(6 in latest.get(label, {}).get("statuses", ())
               for label in ("host", "sandbox")) and all(
                7 in latest.get(label, {}).get("statuses", ())
                for label in ("host", "sandbox")) and all(
                latest[label]["cleanup_storage"]
                and latest[label]["cleanup_storage"]["returned"] == 1
                and latest[label]["cleanup_storage"]["pre"]
                    == latest[label]["cleanup_storage"]["ending"]
                for label in ("host", "sandbox")):
            break
        time.sleep(0.25)
    else:
        raise TimeoutError(f"{profile} did not fail closed and return both peers to lobby")
    if profile.startswith("preownership_"):
        if any(5 in latest[label]["statuses"] for label in ("host", "sandbox")):
            raise RuntimeError(f"{profile} reached ownership before expected failure")
    if profile.startswith("postownership_") and not started_post:
        raise RuntimeError(f"{profile} was never activated after ownership")
    expected_codes = {
        "preownership_mismatch": "identity_mismatch",
        "preownership_timeout": "timeout",
        "postownership_auth": "authentication_failed",
        "postownership_hash": "state_hash_mismatch",
        "postownership_restore": "restore_write_failed",
        "postownership_peer": "peer_disconnected",
    }
    expected_code = expected_codes.get(profile)
    if expected_code is not None and not any(
            expected_code in latest[label]["failures"]
            for label in ("host", "sandbox")):
        raise RuntimeError(
            f"{profile} did not fail at its authoritative {expected_code} boundary")
    if any(latest[label]["handshake"] is None
           for label in ("host", "sandbox")):
        raise RuntimeError(f"{profile} failed before exact content identity was logged")
    for label in ("host", "sandbox"):
        statuses = latest[label]["statuses"]
        if statuses.index(6) >= statuses.index(7):
            raise RuntimeError(f"{label} terminal cleanup status was not ordered 6 then 7")
        if any(current < prior for prior, current in zip(statuses, statuses[1:])):
            raise RuntimeError(f"{label} failure status regressed")
        events = latest[label]["events"]
        if events[-2:] != ["cleanup_started", "cleanup_completed"]:
            raise RuntimeError(f"{label} failure cleanup lifecycle was incomplete")
        if 5 in statuses:
            _require_ordered_takeover(events, label)
        handshake = latest[label]["handshake"]
        if (handshake["map"] != case["stage_package_root"]
                or handshake["display"] != case["native_display_name"]
                or [handshake["f0"], handshake["f1"]] != case["fighter_order"]):
            raise RuntimeError(f"{label} failure case used the wrong exact content")
        expected_local, expected_peer = expected_steam_ids[label]
        for proof in latest[label]["authenticated"]:
            if (int(proof["local"]) != expected_local
                    or int(proof["peer"]) != expected_peer):
                raise RuntimeError(f"{label} failure case Steam identity mismatch")
    return latest, {
        "host": _read_since(paths.host.log, offsets["host"]),
        "sandbox": _read_since(paths.sandbox.log, offsets["sandbox"]),
    }


def run_paired_online(args: Any, root: Path, paths: ObserverPairPaths) -> int:
    if not args.schema.is_file():
        raise FileNotFoundError(f"generated schema not found: {args.schema}")
    require_compiled_candidate_manifest(args.schema, args.case_manifest)
    document = json.loads(args.case_manifest.read_text(encoding="utf-8"))
    cases = {case["case_id"]: case for case in document["cases"]}
    if args.case not in cases:
        raise RuntimeError(f"unknown exact content case: {args.case}")
    case = cases[args.case]
    failure_case, faults = _qualification_failure_plan(
        args.impairment_profile, args.failure_case)
    if args.match_cycles < 1 or args.cycling_soak_seconds < 0:
        raise RuntimeError("match cycles must be positive and cycling soak non-negative")
    if ((args.match_cycles > 1 or args.cycling_soak_seconds > 0)
            and args.impairment_profile != "clean"):
        raise RuntimeError("same-process cycling is a clean-profile qualification")
    if args.cycling_soak_seconds > 0 and args.soak_seconds > 0:
        raise RuntimeError("cycling and continuous-play soaks are separate gates")
    if (args.impairment_profile != "clean"
            and (args.impairment_tool is None or not args.impairment_tool.is_file())):
        raise RuntimeError(
            "non-clean paired qualification requires an existing reviewed "
            "impairment tool"
        )
    spec = SandboxiePairSpec(
        box_name=args.sandbox_box, sandboxie_start=args.sandboxie_start,
        steam_executable=args.steam_executable,
        game_executable=args.game_executable,
        sandbox_query_port=args.sandbox_query_port,
    )
    spec.validate()
    if list_game_processes():
        raise RuntimeError("SC6 must be closed before paired deployment")
    identity = source_identity(root)
    if identity["dirty"]:
        raise RuntimeError("paired certification requires frozen deterministic sources")
    fresh_roots_initially_absent = not any((
        paths.sandbox.mods_root.exists(), paths.sandbox.qualification_root.exists(),
        paths.sandbox.log.exists(),
    ))
    if args.fresh_box:
        if args.sandbox_box == "sc67":
            raise RuntimeError("fresh-box release gate cannot reuse sc67")
        if not fresh_roots_initially_absent:
            raise RuntimeError(
                "fresh-box UE4SS/log/qualification roots were not initially absent")
    if shutil.disk_usage(root).free < 10 * 1024**3:
        raise RuntimeError("less than 10 GiB free; refusing paired qualification")
    run_id = "paired-" + uuid.uuid4().hex
    run_ids = {
        "host": run_id + "-host",
        "sandbox": run_id + "-sandbox",
    }
    pair = None
    process_rows = None
    metrics = None
    raw_logs = None
    cycle_metrics: list[dict[str, Any]] = []
    cycle_raw_logs: dict[str, str] = {}
    cycle_run_ids: list[dict[str, str]] = [dict(run_ids)]
    log_offsets: dict[str, int] | None = None
    initial_log_offsets: dict[str, int] | None = None
    config_hashes: dict[str, str] = {}
    config_fields: dict[str, dict[str, str]] = {}
    hashes: dict[str, str] | None = None
    cleanup_errors: list[str] = []
    impairment = ClumsyImpairment(
        args.impairment_tool or Path(), args.impairment_profile,
        args.impairment_seed)
    impairment_evidence: dict[str, Any] = {
        "profile": args.impairment_profile, "active": False,
    }
    memory_tracker: PrivateMemoryTracker | None = None
    memory_evidence: dict[str, object] | None = None
    cycling_elapsed_seconds = 0.0
    primary: BaseException | None = None
    try:
        hashes = deploy_observer_pair(paths, args.dll, args.replay_mod)
        for peer in (paths.host, paths.sandbox):
            write_fields(peer.config, {"enabled": "false", "trace": "true"})
        config_hashes = {
            "host": sha256_file(paths.host.config),
            "sandbox": sha256_file(paths.sandbox.config),
        }
        config_fields = {
            "host": read_fields(paths.host.config),
            "sandbox": read_fields(paths.sandbox.config),
        }
        log_offsets = {
            "host": paths.host.log.stat().st_size if paths.host.log.exists() else 0,
            "sandbox": paths.sandbox.log.stat().st_size
                if paths.sandbox.log.exists() else 0,
        }
        initial_log_offsets = dict(log_offsets)
        _publish_pair(paths, run_ids, faults)
        create_host_room_suppression(paths.sandbox, run_ids["host"])
        create_host_room_request(paths.host, run_ids["host"])
        subprocess.Popen(spec.host_command(), close_fds=True)
        subprocess.Popen(spec.sandbox_command(), close_fds=True)
        pair, process_rows = wait_for_pair_processes(spec, args.launch_timeout)
        memory_tracker = PrivateMemoryTracker(
            {"host": pair.host_pid, "sandbox": pair.sandbox_pid},
            args.memory_warmup_seconds)
        def guard() -> None:
            if {p.pid for p in list_game_processes()} != {
                    pair.host_pid, pair.sandbox_pid}:
                raise RuntimeError("paired SC6 process identity changed")
            memory_tracker.sample()
        wait_for_host_room(paths.host, run_ids["host"], args.launch_timeout, guard)
        validate_host_room_suppression(paths.sandbox, run_ids["host"])
        process_ids = (pair.host_pid, pair.sandbox_pid)
        if args.impairment_profile not in ("clean", "disconnect_post"):
            impairment_evidence = impairment.start(process_ids)
        cycling_started_at = time.monotonic()
        cycle_index = 0
        if failure_case:
            print(
                "Fotten's Player Match room is ready. Join visibly as ulvunge1, "
                f"then use normal character select for {case['fighter_names'][0]} "
                f"versus {case['fighter_names'][1]} on "
                f"{case['native_display_name']}. No character-select automation "
                "is active.", flush=True,
            )
            post_start = None
            if args.impairment_profile == "disconnect_post":
                def post_start() -> None:
                    nonlocal impairment_evidence
                    impairment_evidence = impairment.start(process_ids)
            metrics, raw_logs = _wait_expected_impairment_failure(
                paths, run_ids, log_offsets, args.match_timeout, guard,
                failure_case, case,
                {"host": (args.host_steamid64, args.client_steamid64),
                 "sandbox": (args.client_steamid64, args.host_steamid64)},
                post_start)
        else:
            while True:
                print(
                    "Fotten's Player Match room is ready for cycle "
                    f"{cycle_index + 1}. Join visibly as ulvunge1, then use normal "
                    f"character select for {case['fighter_names'][0]} versus "
                    f"{case['fighter_names'][1]} on "
                    f"{case['native_display_name']}. No character-select automation "
                    "is active.", flush=True,
                )
                metrics, raw_logs = _wait_online(
                    paths, run_ids, case, log_offsets, args.match_timeout,
                    args.phase_timeout,
                    {"host": (args.host_steamid64, args.client_steamid64),
                     "sandbox": (args.client_steamid64, args.host_steamid64)},
                    guard,
                    (args.memory_warmup_seconds + args.soak_seconds
                     if args.soak_seconds > 0 else 0.0),
                    memory_tracker.restart_warmup if cycle_index == 0 else None,
                )
                cycle_metrics.append({
                    "cycle": cycle_index + 1,
                    "run_ids": dict(run_ids),
                    "log_offsets": dict(log_offsets),
                    "peers": metrics,
                })
                for label, text in raw_logs.items():
                    cycle_raw_logs[f"cycle-{cycle_index + 1:03d}-{label}"] = text
                cycle_index += 1
                enough_cycles = cycle_index >= args.match_cycles
                enough_duration = (time.monotonic() - cycling_started_at
                    >= args.cycling_soak_seconds)
                if enough_cycles and enough_duration:
                    cycling_elapsed_seconds = time.monotonic() - cycling_started_at
                    break
                run_ids = {
                    "host": f"{run_id}-cycle-{cycle_index + 1:03d}-host",
                    "sandbox": f"{run_id}-cycle-{cycle_index + 1:03d}-sandbox",
                }
                cycle_run_ids.append(dict(run_ids))
                log_offsets = {
                    "host": paths.host.log.stat().st_size,
                    "sandbox": paths.sandbox.log.stat().st_size,
                }
                _publish_pair(paths, run_ids, faults)
        memory_tracker.sample()
        memory_evidence = memory_tracker.report()
        if args.soak_seconds > 0 or args.cycling_soak_seconds > 0:
            growth = memory_evidence["ending_growth_bytes"]
            if set(growth) != {"host", "sandbox"}:
                raise RuntimeError("soak ended without a post-warmup private-byte baseline")
            if any(value > 64 * 1024**2 for value in growth.values()):
                raise RuntimeError("soak private bytes exceeded 64 MiB growth ceiling")
    except BaseException as error:
        primary = error
    finally:
        errors: list[str] = []
        try:
            impairment.stop()
        except RuntimeError as error:
            errors.append(str(error))
        if log_offsets is not None:
            try:
                raw_logs = {
                    "host": _read_since(paths.host.log, log_offsets["host"]),
                    "sandbox": _read_since(
                        paths.sandbox.log, log_offsets["sandbox"]),
                }
                if primary is not None and cycle_raw_logs:
                    for label, text in raw_logs.items():
                        cycle_raw_logs.setdefault(
                            f"cycle-{len(cycle_metrics) + 1:03d}-{label}", text)
            except OSError as error:
                errors.append(f"raw log capture failed: {error}")
        try:
            processes = list_game_processes()
            if processes:
                stop_observer_processes(processes)
        except (RuntimeError, TimeoutError) as error:
            errors.append(str(error))
        for peer in (paths.host, paths.sandbox):
            try:
                (peer.qualification_root / "online_request.txt").unlink(missing_ok=True)
                disarm_diagnostics(peer.config)
            except OSError as error:
                errors.append(str(error))
        try:
            cleanup_observer_pair(paths, None)
        except RuntimeError as error:
            errors.append(str(error))
        if list_game_processes():
            errors.append("SC6 processes remain after paired cleanup")
        cleanup_errors = errors
        if errors:
            primary = RuntimeError(f"{primary}; cleanup: {'; '.join(errors)}")
    raw_dir = args.output_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    report_logs = cycle_raw_logs or (raw_logs or {})
    if report_logs:
        for label, text in report_logs.items():
            (raw_dir / f"{run_id}-{label}.log").write_text(
                text, encoding="utf-8")
    if primary is not None:
        failure_report = {
            "report_schema": 2,
            "kind": "paired_online_case",
            "certifying": False,
            "result": "fail",
            "run_id": run_id,
            "peer_run_ids": cycle_run_ids,
            "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "case_id": case["case_id"],
            "display_map_name": case["native_display_name"],
            "stage_package_root": case["stage_package_root"],
            "fighter_order": case["fighter_order"],
            "renderer": "normal",
            "impairment": impairment_evidence,
            "failure": {
                "type": type(primary).__name__,
                "message": str(primary),
                "cleanup_errors": cleanup_errors,
            },
            "identities": None if hashes is None else {
                "source": identity,
                "horsemod_dll_sha256": hashes["horsemod"],
                "bridge_sha256": hashes["observer_bridge"],
                "generated_schema_sha256": sha256_file(args.schema),
                "config_sha256": config_hashes,
                "config_fields": config_fields,
                "game_executable_sha256": sha256_file(args.game_executable),
                "runner_sha256": runner_sha256(
                    root / "tools" / "deterministic_qualification"),
            },
            "runtime": {"peers": metrics, "cycles": cycle_metrics},
            "writable_roots": {
                "host_mods": str(paths.host.mods_root),
                "sandbox_mods": str(paths.sandbox.mods_root),
                "host_qualification": str(paths.host.qualification_root),
                "sandbox_qualification": str(paths.sandbox.qualification_root),
            },
            "initial_log_offsets": initial_log_offsets,
            "process_memory": memory_evidence,
            "cleanup": {
                "diagnostic_flags_false": not cleanup_errors,
                "game_processes_remaining": len(list_game_processes()),
            },
            "raw_logs": None if not report_logs else {
                label: str((raw_dir / f"{run_id}-{label}.log").resolve())
                for label in report_logs
            },
        }
        args.output_dir.mkdir(parents=True, exist_ok=True)
        write_report(args.report, failure_report)
        raise primary
    assert (pair is not None and process_rows is not None and metrics is not None
            and hashes is not None and raw_logs is not None)
    expected_failure = bool(failure_case)
    report = {
        "report_schema": 2, "kind": "paired_online_case",
        "certifying": True, "certification_scope": "single_profile_gate",
        "result": "pass", "run_id": run_id,
        "peer_run_ids": cycle_run_ids,
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "case_id": case["case_id"], "display_map_name": case["native_display_name"],
        "stage_package_root": case["stage_package_root"],
        "fighter_order": case["fighter_order"], "renderer": "normal",
        "protocol_version": 2, "snapshot_schema_version": 47,
        "impairment": impairment_evidence,
        "identities": {
            "source": identity,
            "host_steamid64": args.host_steamid64,
            "client_steamid64": args.client_steamid64,
            "horsemod_dll_sha256": hashes["horsemod"],
            "bridge_sha256": hashes["observer_bridge"],
            "generated_schema_sha256": sha256_file(args.schema),
            "config_sha256": config_hashes,
            "config_fields": config_fields,
            "game_executable_sha256": sha256_file(args.game_executable),
            "source_identity_sha256": source_identity_sha256(root),
            "runner_sha256": runner_sha256(root / "tools" / "deterministic_qualification"),
            "loaded_map_sha256": metrics["host"]["handshake"]["loaded"],
        },
        "processes": {
            "host_pid": pair.host_pid, "sandbox_pid": pair.sandbox_pid,
            "sandbox_box": args.sandbox_box,
            "sandbox_query_port": args.sandbox_query_port,
            "host_command_line": process_rows[pair.host_pid].command_line,
            "sandbox_command_line": process_rows[pair.sandbox_pid].command_line,
        },
        "writable_roots": {
            "host_mods": str(paths.host.mods_root),
            "sandbox_mods": str(paths.sandbox.mods_root),
            "host_qualification": str(paths.host.qualification_root),
            "sandbox_qualification": str(paths.sandbox.qualification_root),
            "host_log": str(paths.host.log),
            "sandbox_log": str(paths.sandbox.log),
        },
        "initial_log_offsets": initial_log_offsets,
        "runtime": {"peers": metrics, "cycles": cycle_metrics,
                    "same_process_match_cycles": len(cycle_metrics),
                    "cycling_soak_seconds": cycling_elapsed_seconds,
                    "continuous_soak_seconds": args.soak_seconds,
                    "fresh_box": bool(args.fresh_box),
                    "fresh_roots_initially_absent": fresh_roots_initially_absent,
                    "expected_fail_closed_case": expected_failure,
                    "failure_case": failure_case or None,
                    "authenticated_steam_p2p": (
                        all(metrics[label].get("authenticated")
                            for label in ("host", "sandbox"))),
                    "confirmed_convergence": None if expected_failure
                        else metrics.get("convergence"),
                    "canonical_divergences": None if expected_failure else 0,
                    "cleanup_status": 7,
                    "real_corrections": False if expected_failure else all(
                        metrics[label]["confirmed"]["corrections"] > 0
                        for label in ("host", "sandbox")),
                    "multi_round": False if expected_failure else all(
                        metrics[label]["rounds"] >= 2
                        for label in ("host", "sandbox")),
                    "presentation_reconciliation": (
                        "not_owned" if expected_failure else (
                            "exact" if all(
                                metrics[label]["confirmed"]["verified_audio_batches"] > 0
                                and metrics[label]["confirmed"]["audio_sequence_mismatches"] == 0
                                and metrics[label]["confirmed"]["verified_camera_batches"] > 0
                                and metrics[label]["confirmed"]["camera_publication_mismatches"] == 0
                                and metrics[label]["confirmed"]["presentation_failures"] == 0
                                and metrics[label]["confirmed"]["journal_duplicates"] == 0
                                and metrics[label]["confirmed"]["journal_publish_failures"] == 0
                                for label in ("host", "sandbox"))
                            else "failed"))},
        "evaluation": {
            "passed_reasons": [
                "authenticated production Steam P2P identities matched",
                "ordered online statuses were monotonic without skipped phases",
                "exact native display map/package and fighter order matched",
                "confirmed canonical hashes remained converged",
                "real corrections and multi-round barriers were observed",
                "timing and allocator-accounted storage ceilings passed",
                "lobby teardown reached status 7 with exact storage return",
            ] if not expected_failure else [
                "expected fault failed closed",
                "both peers exposed status 6 before status 7 cleanup",
                "owned storage returned exactly to the pre-match value",
            ],
            "failed_reasons": [],
        },
        "process_memory": memory_evidence,
        "cleanup": {"requests_disarmed": True, "diagnostic_flags_false": True,
                    "game_processes_remaining": 0},
    }
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_report(args.report, report)
    return 0
