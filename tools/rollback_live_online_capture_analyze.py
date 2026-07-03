#!/usr/bin/env python3
"""Analyze rollback live-online-capture JSONL trace events.

This is the post-run companion to rollback_lab_test_run.py. It verifies the
durable ReplayTrace JSONL artifact after an online capture attempt, keeping the
same readiness/live-traffic distinction and sticky violation/regression policy
used by the live log watcher.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Any


def as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.lower() in {"1", "true", "yes", "on"}
    return False


def as_int(value: Any) -> int:
    if isinstance(value, bool):
        return 1 if value else 0
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return 0
    return 0


DEFAULT_ACTIVATION_SOURCE_PEER = 0xA0
DEFAULT_ACTIVATION_DESTINATION_PEER = 0xB0
DEFAULT_ACTIVATION_SESSION_ID = 0x4C495645414354


def event_is_readiness(event: dict[str, Any]) -> bool:
    return (
        as_bool(event.get("ok"))
        and as_bool(event.get("capture_ready"))
        and as_bool(event.get("observe_only"))
        and as_bool(event.get("stock_hooks_installed"))
        and as_bool(event.get("stock_trace_active"))
        and as_bool(event.get("boundary_hooks_installed"))
        and as_bool(event.get("boundary_trace_active"))
        and not as_bool(event.get("boundary_violation"))
    )


def event_is_live_traffic(event: dict[str, Any]) -> bool:
    return (
        event_is_readiness(event)
        and as_bool(event.get("live_capture_complete"))
        and as_int(event.get("acquire_nonnull_session_count")) > 0
        and as_int(event.get("input_send_count")) > 0
        and as_int(event.get("battle_sync_request_stage_count")) > 0
        and as_int(event.get("receive_enqueue_count")) > 0
        and as_int(event.get("drain_enter_count")) > 0
        and as_int(event.get("drain_exit_count")) > 0
        and as_int(event.get("consumer_count")) > 0
        and as_bool(event.get("live_order_proven"))
    )


def missing_live_traffic_gates(event: dict[str, Any]) -> list[str]:
    if not event:
        return ["live_online_capture_event"]
    checks = [
        ("live_capture_complete",
         as_bool(event.get("live_capture_complete"))),
        ("session_acquired",
         as_int(event.get("acquire_nonnull_session_count")) > 0),
        ("stock_input_send", as_int(event.get("input_send_count")) > 0),
        ("battle_sync_send",
         as_int(event.get("battle_sync_request_stage_count")) > 0),
        ("receive_enqueue", as_int(event.get("receive_enqueue_count")) > 0),
        ("stock_drain_enter", as_int(event.get("drain_enter_count")) > 0),
        ("stock_drain_exit", as_int(event.get("drain_exit_count")) > 0),
        ("cache_consumer", as_int(event.get("consumer_count")) > 0),
        ("live_order", as_bool(event.get("live_order_proven"))),
    ]
    return [name for name, ok in checks if not ok]


def event_is_activation_candidate(
    event: dict[str, Any],
    *,
    expected_source_peer: int = 0,
    expected_destination_peer: int = 0,
    expected_session_id: int = 0,
) -> bool:
    source_peer = as_int(event.get("activation_source_peer"))
    destination_peer = as_int(event.get("activation_destination_peer"))
    session_id = as_int(event.get("activation_session_id"))
    route_values_valid = (
        source_peer > 0
        and destination_peer > 0
        and source_peer != destination_peer
        and session_id > 0
    )
    expected_route_matches = True
    if expected_source_peer or expected_destination_peer or expected_session_id:
        expected_route_matches = (
            source_peer == expected_source_peer
            and destination_peer == expected_destination_peer
            and session_id == expected_session_id
        )
    return (
        as_bool(event.get("ok"))
        and as_bool(event.get("activation_ready"))
        and as_bool(event.get("explicit_operator_enable"))
        and as_bool(event.get("capture_ready"))
        and as_bool(event.get("observe_only"))
        and as_bool(event.get("stock_observe_ready"))
        and as_bool(event.get("boundary_ready"))
        and as_bool(event.get("live_capture_complete"))
        and as_bool(event.get("no_boundary_violation"))
        and as_bool(event.get("stock_send_observed"))
        and as_bool(event.get("receive_observed"))
        and as_bool(event.get("drain_consumer_observed"))
        and as_bool(event.get("live_order_proven"))
        and as_bool(event.get("session_pointer_bound"))
        and as_bool(event.get("input_log_bound"))
        and as_bool(event.get("hrg1_payload"))
        and as_bool(event.get("route_provenance_valid"))
        and as_bool(event.get("strict_identity"))
        and as_bool(event.get("horse_route_allowed"))
        and not as_bool(event.get("stock_surface_rejected"))
        and as_bool(event.get("peer_identity_bound"))
        and as_bool(event.get("session_id_bound"))
        and as_bool(event.get("route_identity_matches"))
        and route_values_valid
        and expected_route_matches
        and event.get("failure") == "ok"
    )


def missing_activation_candidate_gates(
    event: dict[str, Any],
    *,
    expected_source_peer: int = 0,
    expected_destination_peer: int = 0,
    expected_session_id: int = 0,
) -> list[str]:
    if not event:
        return ["live_activation_candidate_event"]
    missing: list[str] = []
    checks = [
        ("operator_arm", as_bool(event.get("explicit_operator_enable"))),
        ("capture_ready", as_bool(event.get("capture_ready"))),
        ("observe_only", as_bool(event.get("observe_only"))),
        ("stock_observe_ready", as_bool(event.get("stock_observe_ready"))),
        ("boundary_ready", as_bool(event.get("boundary_ready"))),
        ("live_capture_complete", as_bool(event.get("live_capture_complete"))),
        ("no_boundary_violation", as_bool(event.get("no_boundary_violation"))),
        ("stock_send_observed", as_bool(event.get("stock_send_observed"))),
        ("receive_observed", as_bool(event.get("receive_observed"))),
        ("drain_consumer_observed",
         as_bool(event.get("drain_consumer_observed"))),
        ("live_order", as_bool(event.get("live_order_proven"))),
        ("session_pointer_bound",
         as_bool(event.get("session_pointer_bound"))),
        ("input_log_bound", as_bool(event.get("input_log_bound"))),
        ("hrg1_payload", as_bool(event.get("hrg1_payload"))),
        ("route_provenance", as_bool(event.get("route_provenance_valid"))),
        ("strict_identity", as_bool(event.get("strict_identity"))),
        ("horse_route_allowed", as_bool(event.get("horse_route_allowed"))),
        ("stock_surface_not_rejected",
         not as_bool(event.get("stock_surface_rejected"))),
        ("peer_identity_bound", as_bool(event.get("peer_identity_bound"))),
        ("session_id_bound", as_bool(event.get("session_id_bound"))),
        ("route_identity", as_bool(event.get("route_identity_matches"))),
    ]
    for name, ok in checks:
        if not ok:
            missing.append(name)
    if (
        expected_source_peer
        or expected_destination_peer
        or expected_session_id
    ):
        if as_int(event.get("activation_source_peer")) != expected_source_peer:
            missing.append("activation_source_peer")
        if as_int(event.get("activation_destination_peer")) != (
            expected_destination_peer):
            missing.append("activation_destination_peer")
        if as_int(event.get("activation_session_id")) != expected_session_id:
            missing.append("activation_session_id")
    if event.get("failure") != "ok":
        missing.append("activation_status_ok")
    return missing


def filter_events_by_case(
    events: list[dict[str, Any]],
    required_case: str | None,
) -> tuple[list[dict[str, Any]], int]:
    if not required_case:
        return events, 0
    filtered = [
        event for event in events if event.get("case") == required_case
    ]
    return filtered, len(events) - len(filtered)


def filter_events_by_request_id(
    events: list[dict[str, Any]],
    required_request_id: str | None,
) -> tuple[list[dict[str, Any]], int]:
    if not required_request_id:
        return events, 0
    filtered = [
        event for event in events
        if event.get("request_id") == required_request_id
    ]
    return filtered, len(events) - len(filtered)


def load_capture_events(paths: list[Path]) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for line_no, line in enumerate(f, 1):
                if "rollback_live_online_capture" not in line:
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("event") != "rollback_live_online_capture":
                    continue
                event["_source_path"] = str(path)
                event["_source_line"] = line_no
                events.append(event)
    return events


def load_activation_candidate_events(paths: list[Path]) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for line_no, line in enumerate(f, 1):
                if "rollback_live_activation_candidate" not in line:
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("event") != "rollback_live_activation_candidate":
                    continue
                event["_source_path"] = str(path)
                event["_source_line"] = line_no
                events.append(event)
    return events


def summarize_activation_candidates(
    events: list[dict[str, Any]],
    *,
    expected_source_peer: int = 0,
    expected_destination_peer: int = 0,
    expected_session_id: int = 0,
) -> dict[str, Any]:
    ready_event: dict[str, Any] | None = None
    last_event = events[-1] if events else None

    for event in events:
        if event_is_activation_candidate(
            event,
            expected_source_peer=expected_source_peer,
            expected_destination_peer=expected_destination_peer,
            expected_session_id=expected_session_id,
        ):
            ready_event = event

    selected = ready_event or last_event or {}
    selected_source_peer = as_int(selected.get("activation_source_peer"))
    selected_destination_peer = as_int(
        selected.get("activation_destination_peer"))
    selected_session_id = as_int(selected.get("activation_session_id"))
    expected_route_required = (
        expected_source_peer != 0
        or expected_destination_peer != 0
        or expected_session_id != 0
    )
    selected_route_matches = (
        selected_source_peer == expected_source_peer
        and selected_destination_peer == expected_destination_peer
        and selected_session_id == expected_session_id
    ) if expected_route_required else True
    selected_source = (
        "ready" if ready_event is not None else
        "last" if last_event is not None else
        "missing"
    )
    return {
        "observed": bool(events),
        "event_count": len(events),
        "ready": ready_event is not None,
        "selected_source": selected_source,
        "selected": selected,
        "expected_source_peer": expected_source_peer,
        "expected_destination_peer": expected_destination_peer,
        "expected_session_id": (
            f"0x{expected_session_id:X}" if expected_session_id else ""),
        "selected_route_matches": selected_route_matches,
        "missing_activation_candidate_gates": (
            missing_activation_candidate_gates(
                selected,
                expected_source_peer=expected_source_peer,
                expected_destination_peer=expected_destination_peer,
                expected_session_id=expected_session_id,
            )
        ),
        "failure": selected.get(
            "failure",
            "missing" if not events else "activation-candidate-not-ready",
        ),
    }


def summarize(events: list[dict[str, Any]]) -> dict[str, Any]:
    observed = bool(events)
    readiness_event: dict[str, Any] | None = None
    live_event: dict[str, Any] | None = None
    last_event = events[-1] if events else None
    bad_events: list[dict[str, Any]] = []
    readiness_seen = False
    boundary_violation_seen = False
    readiness_regression_seen = False

    for event in events:
        ready = event_is_readiness(event)
        live = event_is_live_traffic(event)
        if as_bool(event.get("boundary_violation")):
            boundary_violation_seen = True
            bad_events.append(event)
        if readiness_seen and not ready:
            readiness_regression_seen = True
            bad_events.append(event)
        if ready:
            readiness_seen = True
            readiness_event = event
        if live:
            live_event = event

    stable_readiness_ok = (
        readiness_seen
        and not boundary_violation_seen
        and not readiness_regression_seen
    )
    live_traffic_ok = live_event is not None
    stable_live_traffic_ok = live_traffic_ok and stable_readiness_ok

    selected = live_event or readiness_event or last_event or {}
    selected_source = (
        "traffic" if live_event is not None else
        "readiness" if readiness_event is not None else
        "last" if last_event is not None else
        "missing"
    )
    failure = selected.get("failure", "missing")
    if boundary_violation_seen:
        failure = "boundary-violation-seen"
    elif readiness_regression_seen:
        failure = "readiness-regression-seen"
    elif not stable_readiness_ok:
        failure = "readiness-not-proven"

    return {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "observed": observed,
        "event_count": len(events),
        "readiness_ok": readiness_seen,
        "stable_readiness_ok": stable_readiness_ok,
        "live_traffic_ok": live_traffic_ok,
        "stable_live_traffic_ok": stable_live_traffic_ok,
        "missing_live_traffic_gates": missing_live_traffic_gates(selected),
        "boundary_violation_seen": boundary_violation_seen,
        "readiness_regression_seen": readiness_regression_seen,
        "bad_event_count": len(bad_events),
        "bad_events": bad_events[:5],
        "selected_source": selected_source,
        "selected": selected,
        "failure": failure,
    }


def run_self_test() -> int:
    ready = {
        "event": "rollback_live_online_capture",
        "case": "live-online-capture",
        "request_id": "selftest-ready",
        "ok": True,
        "capture_ready": True,
        "live_capture_complete": False,
        "observe_only": True,
        "stock_hooks_installed": True,
        "stock_trace_active": True,
        "boundary_hooks_installed": True,
        "boundary_trace_active": True,
        "boundary_violation": False,
        "failure": "waiting-for-live-online-traffic",
    }
    live = {
        **ready,
        "live_capture_complete": True,
        "acquire_nonnull_session_count": 1,
        "input_send_count": 1,
        "battle_sync_request_stage_count": 1,
        "receive_enqueue_count": 1,
        "drain_enter_count": 1,
        "drain_exit_count": 1,
        "consumer_count": 1,
        "live_order_proven": True,
        "failure": "ok",
    }
    bad = {
        **ready,
        "ok": False,
        "boundary_violation": True,
        "failure": "boundary-order-violation",
    }
    wrong_case = {
        **ready,
        "case": "stock-observe",
    }
    wrong_request = {
        **ready,
        "request_id": "previous-run",
    }
    activation_ready = {
        "event": "rollback_live_activation_candidate",
        "case": "live-online-capture",
        "request_id": "selftest-ready",
        "ok": True,
        "activation_ready": True,
        "explicit_operator_enable": True,
        "capture_ready": True,
        "observe_only": True,
        "stock_observe_ready": True,
        "boundary_ready": True,
        "live_capture_complete": True,
        "no_boundary_violation": True,
        "stock_send_observed": True,
        "receive_observed": True,
        "drain_consumer_observed": True,
        "live_order_proven": True,
        "session_pointer_bound": True,
        "input_log_bound": True,
        "hrg1_payload": True,
        "route_provenance_valid": True,
        "strict_identity": True,
        "horse_route_allowed": True,
        "stock_surface_rejected": False,
        "peer_identity_bound": True,
        "session_id_bound": True,
        "route_identity_matches": True,
        "activation_source_peer": 0xA0,
        "activation_destination_peer": 0xB0,
        "activation_session_id": 0x4C495645414354,
        "failure": "ok",
    }
    activation_refused = {
        **activation_ready,
        "ok": False,
        "activation_ready": False,
        "live_capture_complete": False,
        "failure": "live-traffic-not-proven",
    }
    activation_wrong_route = {
        **activation_ready,
        "activation_source_peer": 0xC1,
        "activation_destination_peer": 0xC2,
        "activation_session_id": 0x1234,
    }

    ready_summary = summarize([ready])
    live_summary = summarize([ready, live])
    bad_summary = summarize([ready, bad])
    activation_summary = summarize_activation_candidates(
        [activation_refused, activation_ready],
        expected_source_peer=DEFAULT_ACTIVATION_SOURCE_PEER,
        expected_destination_peer=DEFAULT_ACTIVATION_DESTINATION_PEER,
        expected_session_id=DEFAULT_ACTIVATION_SESSION_ID,
    )
    activation_refused_summary = summarize_activation_candidates(
        [activation_refused],
        expected_source_peer=DEFAULT_ACTIVATION_SOURCE_PEER,
        expected_destination_peer=DEFAULT_ACTIVATION_DESTINATION_PEER,
        expected_session_id=DEFAULT_ACTIVATION_SESSION_ID,
    )
    activation_wrong_route_summary = summarize_activation_candidates(
        [activation_wrong_route],
        expected_source_peer=DEFAULT_ACTIVATION_SOURCE_PEER,
        expected_destination_peer=DEFAULT_ACTIVATION_DESTINATION_PEER,
        expected_session_id=DEFAULT_ACTIVATION_SESSION_ID,
    )
    filtered_case, filtered_case_count = filter_events_by_case(
        [ready, wrong_case], "live-online-capture")
    filtered_request, filtered_request_count = filter_events_by_request_id(
        [ready, wrong_request], "selftest-ready")
    if not ready_summary["stable_readiness_ok"]:
        print("self-test failed: readiness summary", file=sys.stderr)
        return 1
    if ready_summary["stable_live_traffic_ok"]:
        print("self-test failed: readiness claimed live traffic", file=sys.stderr)
        return 1
    if not live_summary["stable_live_traffic_ok"]:
        print("self-test failed: live traffic summary", file=sys.stderr)
        return 1
    if bad_summary["stable_readiness_ok"]:
        print("self-test failed: bad event stayed ready", file=sys.stderr)
        return 1
    if bad_summary["failure"] != "boundary-violation-seen":
        print("self-test failed: bad failure reason", file=sys.stderr)
        return 1
    if len(filtered_case) != 1 or filtered_case_count != 1:
        print("self-test failed: case filter", file=sys.stderr)
        return 1
    if len(filtered_request) != 1 or filtered_request_count != 1:
        print("self-test failed: request-id filter", file=sys.stderr)
        return 1
    if not activation_summary["ready"]:
        print("self-test failed: activation candidate ready", file=sys.stderr)
        return 1
    if activation_refused_summary["ready"]:
        print("self-test failed: activation refusal passed", file=sys.stderr)
        return 1
    if activation_wrong_route_summary["ready"]:
        print("self-test failed: activation wrong route passed",
              file=sys.stderr)
        return 1
    if activation_wrong_route_summary["selected_route_matches"]:
        print("self-test failed: activation wrong route matched",
              file=sys.stderr)
        return 1
    print("rollback live-online capture analyzer self-test passed")
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("trace", nargs="*", type=Path)
    p.add_argument("--require-readiness", action="store_true")
    p.add_argument("--require-live-traffic", action="store_true")
    p.add_argument("--require-live-activation-candidate", action="store_true")
    p.add_argument("--require-case")
    p.add_argument("--require-request-id")
    p.add_argument(
        "--activation-source-peer",
        default=f"0x{DEFAULT_ACTIVATION_SOURCE_PEER:X}",
    )
    p.add_argument(
        "--activation-destination-peer",
        default=f"0x{DEFAULT_ACTIVATION_DESTINATION_PEER:X}",
    )
    p.add_argument(
        "--activation-session-id",
        default=f"0x{DEFAULT_ACTIVATION_SESSION_ID:X}",
    )
    p.add_argument("--output", type=Path)
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()

    if args.self_test:
        return run_self_test()

    if not args.trace:
        print("no trace files supplied", file=sys.stderr)
        return 2

    missing = [str(path) for path in args.trace if not path.exists()]
    if missing:
        print(f"missing trace file(s): {missing}", file=sys.stderr)
        return 2

    events, filtered_case_count = filter_events_by_case(
        load_capture_events(args.trace), args.require_case)
    events, filtered_request_count = filter_events_by_request_id(
        events, args.require_request_id)
    activation_events, activation_filtered_case_count = filter_events_by_case(
        load_activation_candidate_events(args.trace), args.require_case)
    activation_events, activation_filtered_request_count = (
        filter_events_by_request_id(
            activation_events, args.require_request_id))
    expected_activation_source_peer = as_int(args.activation_source_peer)
    expected_activation_destination_peer = as_int(
        args.activation_destination_peer)
    expected_activation_session_id = as_int(args.activation_session_id)
    summary = summarize(events)
    activation_summary = summarize_activation_candidates(
        activation_events,
        expected_source_peer=expected_activation_source_peer,
        expected_destination_peer=expected_activation_destination_peer,
        expected_session_id=expected_activation_session_id,
    )
    ok = True
    if args.require_readiness:
        ok = ok and bool(summary["stable_readiness_ok"])
    if args.require_live_traffic:
        ok = ok and bool(summary["stable_live_traffic_ok"])
    if args.require_live_activation_candidate:
        ok = (
            ok
            and bool(summary["stable_readiness_ok"])
            and bool(activation_summary["ready"])
        )
    if (
        not args.require_readiness
        and not args.require_live_traffic
        and not args.require_live_activation_candidate
    ):
        ok = bool(summary["stable_readiness_ok"])
    summary["ok"] = ok
    summary["require_readiness"] = bool(args.require_readiness)
    summary["require_live_traffic"] = bool(args.require_live_traffic)
    summary["require_live_activation_candidate"] = bool(
        args.require_live_activation_candidate)
    summary["expected_activation_source_peer"] = (
        expected_activation_source_peer)
    summary["expected_activation_destination_peer"] = (
        expected_activation_destination_peer)
    summary["expected_activation_session_id"] = (
        f"0x{expected_activation_session_id:X}"
        if expected_activation_session_id else "")
    summary["activation_candidate"] = activation_summary
    summary["require_case"] = args.require_case or ""
    summary["require_request_id"] = args.require_request_id or ""
    summary["filtered_case_count"] = filtered_case_count
    summary["filtered_request_count"] = filtered_request_count
    summary["activation_filtered_case_count"] = (
        activation_filtered_case_count)
    summary["activation_filtered_request_count"] = (
        activation_filtered_request_count)
    summary["filtered_event_count"] = (
        filtered_case_count + filtered_request_count
        + activation_filtered_case_count
        + activation_filtered_request_count)
    summary["trace_files"] = [str(path) for path in args.trace]

    text = json.dumps(summary, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
        print(f"live_online_trace_summary={args.output}")
    else:
        print(text)

    if not ok:
        if args.require_live_activation_candidate:
            print("live online trace did not prove activation candidate",
                  file=sys.stderr)
        elif args.require_live_traffic:
            print("live online trace did not prove required live traffic/order",
                  file=sys.stderr)
        elif args.require_readiness:
            print("live online trace did not prove stable readiness",
                  file=sys.stderr)
        else:
            print("live online trace did not prove stable readiness",
                  file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
