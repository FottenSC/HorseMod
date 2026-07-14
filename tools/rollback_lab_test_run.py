#!/usr/bin/env python3
"""Rollback lab launcher/log watcher.

This is the first automation shell for the rollback plan. It verifies that the
HorseMod rollback lab can be enabled explicitly and remains dormant otherwise.
Future phases should extend this script to regenerate the full review bundle:
baseline oracle, correction compare, cache ownership trace, side-effect ledger,
and strict replay logs.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import winreg
from datetime import datetime
from pathlib import Path

try:
    from replay_seek_test_run import (
        process_ids_by_image,
        terminate_process_ids,
        wait_for_image_exit,
    )
except ModuleNotFoundError:
    from .replay_seek_test_run import (
        process_ids_by_image,
        terminate_process_ids,
        wait_for_image_exit,
    )


REPO = Path(__file__).resolve().parents[1]
GAME_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
)
UE4SS_LOG = GAME_EXE.parent / "ue4ss" / "UE4SS.log"
STEAM_APPID = "544750"
SAVED_DIR = GAME_EXE.parent / "ue4ss" / "Mods" / "HorseMod" / "Saved"
REQUEST_FILE = SAVED_DIR / "rollback_lab_request.txt"
TRACE_DIR = SAVED_DIR / "ReplayTrace"


def parse_line_fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        key = key.strip()
        if not key:
            continue
        fields[key] = value.rstrip(",;")
    return fields


def line_has_field(line: str, field: str, value: str) -> bool:
    return parse_line_fields(line).get(field) == value


def line_field_int(line: str, field: str, default: int = 0) -> int:
    raw = parse_line_fields(line).get(field)
    if raw is None:
        return default
    try:
        return int(raw, 0)
    except ValueError:
        return default


def line_field_str(line: str, field: str, default: str = "") -> str:
    return parse_line_fields(line).get(field, default)


def parse_int_text(value: str, default: int = 0) -> int:
    try:
        return int(str(value), 0)
    except (TypeError, ValueError):
        return default


def line_matches_request_id(line: str, request_id: str) -> bool:
    if not request_id:
        return True
    return line_field_str(line, "request_id") == request_id


def parse_live_online_capture_line(line: str) -> dict[str, object]:
    return {
        "ok": line_field_int(line, "ok"),
        "ready": line_field_int(line, "ready"),
        "live": line_field_int(line, "live"),
        "observe_only": line_field_int(line, "observe_only"),
        "stock_ok": line_field_int(line, "stock_ok"),
        "stock_hooks": line_field_int(line, "stock_hooks"),
        "stock_active": line_field_int(line, "stock_active"),
        "boundary_hooks": line_field_int(line, "boundary_hooks"),
        "boundary_active": line_field_int(line, "boundary_active"),
        "acquire": line_field_int(line, "acquire"),
        "nonnull": line_field_int(line, "nonnull"),
        "input": line_field_int(line, "input"),
        "battle": line_field_int(line, "battle"),
        "recv": line_field_int(line, "recv"),
        "drain_enter": line_field_int(line, "drain_enter"),
        "drain_exit": line_field_int(line, "drain_exit"),
        "consumer": line_field_int(line, "consumer"),
        "live_order": line_field_int(line, "live_order"),
        "boundary_violation": line_field_int(line, "boundary_violation"),
        "total": line_field_int(line, "total"),
        "last_session": line_field_int(line, "last_session"),
        "last_input_log": line_field_int(line, "last_input_log"),
        "last_recv_packet": line_field_int(line, "last_recv_packet"),
        "last_bm": line_field_int(line, "last_bm"),
        "failure": line_field_str(line, "failure", "missing"),
        "request_id": line_field_str(line, "request_id", ""),
        "line": line,
    }


def parse_live_activation_candidate_line(line: str) -> dict[str, object]:
    return {
        "ok": line_field_int(line, "ok"),
        "ready": line_field_int(line, "ready"),
        "operator": line_field_int(line, "operator"),
        "capture": line_field_int(line, "capture"),
        "observe_only": line_field_int(line, "observe_only"),
        "stock": line_field_int(line, "stock"),
        "boundary": line_field_int(line, "boundary"),
        "live": line_field_int(line, "live"),
        "no_violation": line_field_int(line, "no_violation"),
        "stock_send": line_field_int(line, "stock_send"),
        "receive": line_field_int(line, "receive"),
        "drain_consumer": line_field_int(line, "drain_consumer"),
        "live_order": line_field_int(line, "live_order"),
        "session_ptr": line_field_int(line, "session_ptr"),
        "input_log": line_field_int(line, "input_log"),
        "hrg1": line_field_int(line, "hrg1"),
        "provenance": line_field_int(line, "provenance"),
        "strict_identity": line_field_int(line, "strict_identity"),
        "horse_route": line_field_int(line, "horse_route"),
        "stock_reject": line_field_int(line, "stock_reject"),
        "peer_identity": line_field_int(line, "peer_identity"),
        "session_id": line_field_int(line, "session_id"),
        "route_identity": line_field_int(line, "route_identity"),
        "source": line_field_int(line, "source"),
        "dest": line_field_int(line, "dest"),
        "session": line_field_str(line, "session", ""),
        "status": line_field_int(line, "status"),
        "surface": line_field_int(line, "surface"),
        "failure": line_field_str(line, "failure", "missing"),
        "line": line,
    }


def missing_live_online_gates(selected: dict[str, object]) -> list[str]:
    if not selected:
        return ["live_online_capture_event"]
    checks = [
        ("live_capture_complete", "live"),
        ("session_acquired", "nonnull"),
        ("stock_input_send", "input"),
        ("battle_sync_send", "battle"),
        ("receive_enqueue", "recv"),
        ("stock_drain_enter", "drain_enter"),
        ("stock_drain_exit", "drain_exit"),
        ("cache_consumer", "consumer"),
        ("live_order", "live_order"),
    ]
    return [
        name for name, key in checks
        if parse_int_text(selected.get(key), 0) <= 0
    ]


def missing_live_activation_gates(
    activation_candidate: dict[str, object],
    expected_source_peer: int,
    expected_destination_peer: int,
    expected_session_id: int,
) -> list[str]:
    if not activation_candidate:
        return ["live_activation_candidate_event"]
    missing: list[str] = []
    checks = [
        ("operator_arm", "operator"),
        ("capture_ready", "capture"),
        ("observe_only", "observe_only"),
        ("stock_observe_ready", "stock"),
        ("boundary_ready", "boundary"),
        ("live_capture_complete", "live"),
        ("no_boundary_violation", "no_violation"),
        ("stock_send_observed", "stock_send"),
        ("receive_observed", "receive"),
        ("drain_consumer_observed", "drain_consumer"),
        ("live_order", "live_order"),
        ("session_pointer_bound", "session_ptr"),
        ("input_log_bound", "input_log"),
        ("hrg1_payload", "hrg1"),
        ("route_provenance", "provenance"),
        ("strict_identity", "strict_identity"),
        ("horse_route_allowed", "horse_route"),
        ("peer_identity_bound", "peer_identity"),
        ("session_id_bound", "session_id"),
        ("route_identity", "route_identity"),
    ]
    for name, key in checks:
        if parse_int_text(activation_candidate.get(key), 0) <= 0:
            missing.append(name)
    if parse_int_text(activation_candidate.get("stock_reject"), 1) != 0:
        missing.append("stock_surface_rejected")
    if parse_int_text(activation_candidate.get("source"), 0) != (
        expected_source_peer):
        missing.append("activation_source_peer")
    if parse_int_text(activation_candidate.get("dest"), 0) != (
        expected_destination_peer):
        missing.append("activation_destination_peer")
    if parse_int_text(activation_candidate.get("session"), 0) != (
        expected_session_id):
        missing.append("activation_session_id")
    if activation_candidate.get("failure", "missing") != "ok":
        missing.append("activation_status_ok")
    return missing


def line_has_strong_resim_policy(line: str) -> bool:
    return (
        line_has_field(line, "hgcpu_policy", "1")
        and line_has_field(line, "motion_bank_control", "1")
        and line_has_field(line, "motion_tail", "1")
        and line_has_field(line, "unignored", "0")
    )


def line_has_strong_cache_policy(line: str) -> bool:
    return (
        line_has_field(line, "full_hash", "1")
        and line_has_field(line, "cache_hash", "1")
        and line_has_field(line, "drain", "1")
        and line_has_field(line, "resim_policy", "1")
        and line_has_field(line, "resim_motion_bank_control", "1")
        and line_has_field(line, "resim_motion_tail", "1")
        and line_has_field(line, "resim_motion_bank_mismatches", "0")
        and line_has_field(line, "resim_unignored", "0")
    )


def line_has_online_session_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "ack", "1")
        and line_has_field(line, "predict", "1")
        and line_has_field(line, "no_correction", "1")
        and line_has_field(line, "correction", "1")
        and line_has_field(line, "reorder", "1")
        and line_has_field(line, "duplicate", "1")
        and line_has_field(line, "conflict", "1")
        and line_has_field(line, "late", "1")
        and line_has_field(line, "reorder_seed", "1")
        and line_has_field(line, "no_future_seed", "1")
        and line_has_field(line, "cache_write", "1")
        and line_has_field(line, "stock_drain", "1")
        and line_has_field(line, "bypass", "1")
        and line_has_field(line, "cache_provenance", "1")
        and line_has_field(line, "hash_enforced", "1")
        and line_has_field(line, "hash_warn", "1")
    )


def line_has_live_transport_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "enqueue", "1")
        and line_has_field(line, "bad", "1")
        and line_has_field(line, "wrong_source", "1")
        and line_has_field(line, "wrong_dest", "1")
        and line_has_field(line, "wrong_session", "1")
        and line_has_field(line, "queued_only", "1")
        and line_has_field(line, "stock_drain", "1")
        and line_has_field(line, "drain", "1")
        and line_has_field(line, "correction", "1")
        and line_has_field(line, "duplicate", "1")
        and line_has_field(line, "late", "1")
        and line_has_field(line, "bypass", "1")
        and line_has_field(line, "capacity", "1")
        and line_field_int(line, "enqueued") > 0
        and line_field_int(line, "drained") > 0
        and line_field_int(line, "rejected") > 0
    )


def line_has_live_peer_pipeline_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "enqueue", "1")
        and line_has_field(line, "queued_only", "1")
        and line_has_field(line, "stock_drain", "1")
        and line_has_field(line, "metadata", "1")
        and line_has_field(line, "payload_not_cache", "1")
        and line_has_field(line, "predict_cache", "1")
        and line_has_field(line, "confirm_replace", "1")
        and line_has_field(line, "consume_confirmed", "1")
        and line_has_field(line, "duplicate", "1")
        and line_has_field(line, "pred_over_confirmed", "1")
        and line_has_field(line, "wrong_identity", "1")
        and line_has_field(line, "late_no_cache", "1")
        and line_has_field(line, "net_cache_reject", "1")
        and line_has_field(line, "bypass", "1")
        and line_field_int(line, "enqueued") > 0
        and line_field_int(line, "drained") > 0
        and line_field_int(line, "rejected") > 0
        and line_field_int(line, "cache_writes") > 0
    )


def line_has_end_to_end_policy(line: str) -> bool:
    final_a = line_field_int(line, "final_a")
    final_b = line_field_int(line, "final_b")
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "decode", "1")
        and line_has_field(line, "bridge", "1")
        and line_has_field(line, "predict", "1")
        and line_has_field(line, "predicted_diff", "1")
        and line_has_field(line, "adapter_receive", "1")
        and line_has_field(line, "adapter_free", "1")
        and line_has_field(line, "metadata", "1")
        and line_has_field(line, "correction", "1")
        and line_has_field(line, "metadata_not_gameplay", "1")
        and line_has_field(line, "confirm_apply", "1")
        and line_has_field(line, "confirm_consume", "1")
        and line_has_field(line, "baseline", "1")
        and line_has_field(line, "state", "1")
        and line_has_field(line, "wrong_identity", "1")
        and line_field_int(line, "enqueued") > 0
        and line_field_int(line, "drained") > 0
        and line_field_int(line, "decoded_inputs") > 0
        and final_a != 0
        and final_a == final_b
        and line_field_int(line, "save_events") > 0
        and line_field_int(line, "load_events") > 0
        and line_field_int(line, "advance_events") > 0
        and line_field_int(line, "rollback_advances") > 0
        and line_has_field(line, "failure", "ok")
    )


def line_has_live_activation_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "ready", "1")
        and line_has_field(line, "readiness_only", "1")
        and line_has_field(line, "stock", "1")
        and line_has_field(line, "identity", "1")
        and line_has_field(line, "boundary", "1")
        and line_has_field(line, "session", "1")
        and line_has_field(line, "input_log", "1")
        and line_has_field(line, "self_peer", "1")
        and line_has_field(line, "zero_session", "1")
        and line_has_field(line, "operator", "1")
        and line_has_field(line, "receive", "1")
        and line_has_field(line, "non_hrg1", "1")
        and line_has_field(line, "route_provenance", "1")
        and line_has_field(line, "direct_ready", "1")
        and line_has_field(line, "route_identity", "1")
    )


def line_has_live_activation_candidate_policy(
    line: str,
    expected_source_peer: int,
    expected_destination_peer: int,
    expected_session_id: int,
) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "ready", "1")
        and expected_source_peer > 0
        and expected_destination_peer > 0
        and expected_source_peer != expected_destination_peer
        and expected_session_id > 0
        and line_has_field(line, "operator", "1")
        and line_has_field(line, "capture", "1")
        and line_has_field(line, "observe_only", "1")
        and line_has_field(line, "stock", "1")
        and line_has_field(line, "boundary", "1")
        and line_has_field(line, "live", "1")
        and line_has_field(line, "no_violation", "1")
        and line_has_field(line, "stock_send", "1")
        and line_has_field(line, "receive", "1")
        and line_has_field(line, "drain_consumer", "1")
        and line_has_field(line, "live_order", "1")
        and line_has_field(line, "session_ptr", "1")
        and line_has_field(line, "input_log", "1")
        and line_has_field(line, "hrg1", "1")
        and line_has_field(line, "provenance", "1")
        and line_has_field(line, "strict_identity", "1")
        and line_has_field(line, "horse_route", "1")
        and line_has_field(line, "stock_reject", "0")
        and line_has_field(line, "peer_identity", "1")
        and line_has_field(line, "session_id", "1")
        and line_has_field(line, "route_identity", "1")
        and line_field_int(line, "source") == expected_source_peer
        and line_field_int(line, "dest") == expected_destination_peer
        and line_field_int(line, "session") == expected_session_id
        and line_has_field(line, "failure", "ok")
    )


def has_stable_live_online_readiness(
    saw_live_online_capture_ok: bool,
    saw_live_online_boundary_violation: bool,
    saw_live_online_readiness_regression: bool,
) -> bool:
    return (
        saw_live_online_capture_ok
        and not saw_live_online_boundary_violation
        and not saw_live_online_readiness_regression
    )


def live_activation_candidate_strict_failure(
    saw_live_online_capture_ok: bool,
    saw_live_online_boundary_violation: bool,
    saw_live_online_readiness_regression: bool,
    saw_live_activation_candidate: bool,
    saw_live_activation_candidate_ok: bool,
) -> str:
    if not saw_live_activation_candidate:
        return "rollback live activation candidate not observed"
    if saw_live_online_boundary_violation or saw_live_online_readiness_regression:
        return (
            "rollback live activation candidate saw a later capture "
            "violation/regression"
        )
    if not has_stable_live_online_readiness(
        saw_live_online_capture_ok,
        saw_live_online_boundary_violation,
        saw_live_online_readiness_regression,
    ):
        return (
            "rollback live activation candidate did not prove stable "
            "live-online readiness"
        )
    if not saw_live_activation_candidate_ok:
        return "rollback live activation candidate did not report all gates"
    return ""


def line_has_live_activation_executor_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "activation_required", "1")
        and line_has_field(line, "readiness_only", "1")
        and line_has_field(line, "stock", "1")
        and line_has_field(line, "provenance", "1")
        and line_has_field(line, "route_identity", "1")
        and line_has_field(line, "ready", "1")
        and line_has_field(line, "enqueue", "1")
        and line_has_field(line, "queued_only", "1")
        and line_has_field(line, "stock_drain", "1")
        and line_has_field(line, "metadata", "1")
        and line_has_field(line, "metadata_not_gameplay", "1")
        and line_has_field(line, "predict", "1")
        and line_has_field(line, "apply", "1")
        and line_has_field(line, "consume", "1")
        and line_has_field(line, "net_cache_reject", "1")
        and line_has_field(line, "wrong_source", "1")
        and line_has_field(line, "wrong_dest", "1")
        and line_has_field(line, "wrong_session", "1")
        and line_has_field(line, "decoded_route", "1")
        and line_field_int(line, "enqueued") == 1
        and line_field_int(line, "drained") == 1
        and line_field_int(line, "rejected") == 3
        and line_field_int(line, "cache_writes") == 2
    )


def line_has_stock_transport_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "failure", "ok")
        and line_has_field(line, "shared_ptr", "1")
        and line_has_field(line, "slots", "1")
        and line_has_field(line, "channels", "1")
        and line_has_field(line, "input_reject", "1")
        and line_has_field(line, "battle_reject", "1")
        and line_has_field(line, "kv_reject", "1")
        and line_has_field(line, "unknown_reject", "1")
        and line_has_field(line, "provenance", "1")
        and line_has_field(line, "identity", "1")
        and line_has_field(line, "identity_values", "1")
        and line_has_field(line, "horse_allow", "1")
        and line_has_field(line, "stock_native", "1")
        and line_has_field(line, "stock_no_hrg1", "1")
        and line_has_field(line, "flag_override", "1")
        and line_has_field(line, "bridge_v2", "1")
    )


def line_has_stock_observe_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "observe_only", "1")
        and line_has_field(line, "hooks", "1")
        and line_has_field(line, "active", "1")
        and line_has_field(line, "acquire_hook", "1")
        and line_has_field(line, "opcode0_hook", "1")
        and line_has_field(line, "opcode1_hook", "1")
        and line_has_field(line, "battle_hook", "1")
        and line_has_field(line, "recv_hook", "1")
    )


def line_has_stock_observe_live_policy(
    line: str,
    require_session: bool,
    require_input: bool,
    require_battle_sync: bool,
    require_receive: bool,
) -> bool:
    input_count = line_field_int(line, "opcode0") + line_field_int(line, "opcode1")
    return (
        line_has_stock_observe_policy(line)
        and line_field_int(line, "total") > 0
        and (not require_session or line_field_int(line, "nonnull") > 0)
        and (not require_input or input_count > 0)
        and (not require_battle_sync or line_field_int(line, "battle") > 0)
        and (not require_receive or line_field_int(line, "recv") > 0)
    )


def line_has_live_online_capture_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "ready", "1")
        and line_has_field(line, "observe_only", "1")
        and line_has_field(line, "stock_hooks", "1")
        and line_has_field(line, "stock_active", "1")
        and line_has_field(line, "boundary_hooks", "1")
        and line_has_field(line, "boundary_active", "1")
        and line_has_field(line, "boundary_violation", "0")
    )


def line_has_live_online_traffic_policy(line: str) -> bool:
    return (
        line_has_live_online_capture_policy(line)
        and line_has_field(line, "live", "1")
        and line_field_int(line, "nonnull") > 0
        and line_field_int(line, "input") > 0
        and line_field_int(line, "battle") > 0
        and line_field_int(line, "recv") > 0
        and line_field_int(line, "drain_enter") > 0
        and line_field_int(line, "drain_exit") > 0
        and line_field_int(line, "consumer") > 0
        and line_has_field(line, "live_order", "1")
    )


def bool_int(value: object) -> int:
    if isinstance(value, bool):
        return 1 if value else 0
    if isinstance(value, (int, float)):
        return 1 if value else 0
    if isinstance(value, str):
        return 1 if value.lower() in {"1", "true", "yes", "on"} else 0
    return 0


def int_event_field(event: dict[str, object], key: str) -> int:
    value = event.get(key)
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


def live_online_capture_event_line(event: dict[str, object]) -> str:
    fields = {
        "ok": bool_int(event.get("ok")),
        "ready": bool_int(event.get("capture_ready")),
        "live": bool_int(event.get("live_capture_complete")),
        "request_id": str(event.get("request_id", "")),
        "observe_only": bool_int(event.get("observe_only")),
        "stock_ok": bool_int(event.get("stock_observe_ok")),
        "stock_hooks": bool_int(event.get("stock_hooks_installed")),
        "stock_active": bool_int(event.get("stock_trace_active")),
        "boundary_hooks": bool_int(event.get("boundary_hooks_installed")),
        "boundary_active": bool_int(event.get("boundary_trace_active")),
        "acquire": int_event_field(event, "acquire_count"),
        "nonnull": int_event_field(event, "acquire_nonnull_session_count"),
        "input": int_event_field(event, "input_send_count"),
        "battle": int_event_field(event, "battle_sync_request_stage_count"),
        "recv": int_event_field(event, "receive_enqueue_count"),
        "drain_enter": int_event_field(event, "drain_enter_count"),
        "drain_exit": int_event_field(event, "drain_exit_count"),
        "consumer": int_event_field(event, "consumer_count"),
        "live_order": bool_int(event.get("live_order_proven")),
        "boundary_violation": bool_int(event.get("boundary_violation")),
        "total": int_event_field(event, "total_observed_calls"),
        "last_session": str(event.get("last_session_ptr", "0x0")),
        "last_input_log": str(event.get("last_input_log", "0x0")),
        "last_recv_packet": str(event.get("last_receive_packet_wrapper", "0x0")),
        "last_bm": str(event.get("last_battle_manager", "0x0")),
        "failure": str(event.get("failure", "missing")),
    }
    return "[RollbackLab] live_online_capture " + " ".join(
        f"{key}={value}" for key, value in fields.items())


def live_activation_candidate_event_line(event: dict[str, object]) -> str:
    fields = {
        "ok": bool_int(event.get("ok")),
        "ready": bool_int(event.get("activation_ready")),
        "operator": bool_int(event.get("explicit_operator_enable")),
        "capture": bool_int(event.get("capture_ready")),
        "observe_only": bool_int(event.get("observe_only")),
        "stock": bool_int(event.get("stock_observe_ready")),
        "boundary": bool_int(event.get("boundary_ready")),
        "live": bool_int(event.get("live_capture_complete")),
        "no_violation": bool_int(event.get("no_boundary_violation")),
        "stock_send": bool_int(event.get("stock_send_observed")),
        "receive": bool_int(event.get("receive_observed")),
        "drain_consumer": bool_int(event.get("drain_consumer_observed")),
        "live_order": bool_int(event.get("live_order_proven")),
        "session_ptr": bool_int(event.get("session_pointer_bound")),
        "input_log": bool_int(event.get("input_log_bound")),
        "hrg1": bool_int(event.get("hrg1_payload")),
        "provenance": bool_int(event.get("route_provenance_valid")),
        "strict_identity": bool_int(event.get("strict_identity")),
        "horse_route": bool_int(event.get("horse_route_allowed")),
        "stock_reject": bool_int(event.get("stock_surface_rejected")),
        "peer_identity": bool_int(event.get("peer_identity_bound")),
        "session_id": bool_int(event.get("session_id_bound")),
        "route_identity": bool_int(event.get("route_identity_matches")),
        "source": int_event_field(event, "activation_source_peer"),
        "dest": int_event_field(event, "activation_destination_peer"),
        "session": int_event_field(event, "activation_session_id"),
        "request_id": str(event.get("request_id", "")),
        "failure": str(event.get("failure", "missing")),
    }
    return "[RollbackLab] live_activation_candidate " + " ".join(
        f"{key}={value}" for key, value in fields.items())


def merge_live_online_trace_observations(
    trace_file: Path,
    *,
    request_id: str,
    expected_activation_source_peer: int,
    expected_activation_destination_peer: int,
    expected_activation_session_id: int,
) -> dict[str, object]:
    result: dict[str, object] = {
        "capture": False,
        "capture_ok": False,
        "traffic_ok": False,
        "boundary_violation": False,
        "readiness_regression": False,
        "activation_candidate": False,
        "activation_candidate_ok": False,
        "bad_lines": [],
        "last_line": "",
        "readiness_line": "",
        "traffic_line": "",
        "last_activation_line": "",
    }
    readiness_seen = False
    bad_lines: list[str] = []
    try:
        lines = trace_file.read_text(
            encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return result
    for raw in lines:
        if "rollback_live_online_capture" in raw:
            try:
                event = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if event.get("event") != "rollback_live_online_capture":
                continue
            if request_id and event.get("request_id") != request_id:
                continue
            line = live_online_capture_event_line(event)
            result["capture"] = True
            result["last_line"] = line
            if int_event_field(event, "boundary_violation") != 0:
                result["boundary_violation"] = True
                bad_lines.append(line)
            line_ready = line_has_live_online_capture_policy(line)
            line_live = line_has_live_online_traffic_policy(line)
            if readiness_seen and not line_ready:
                result["readiness_regression"] = True
                bad_lines.append(line)
            if line_ready:
                readiness_seen = True
                result["capture_ok"] = True
                result["readiness_line"] = line
            if line_live:
                result["traffic_ok"] = True
                result["traffic_line"] = line
        elif "rollback_live_activation_candidate" in raw:
            try:
                event = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if event.get("event") != "rollback_live_activation_candidate":
                continue
            if request_id and event.get("request_id") != request_id:
                continue
            line = live_activation_candidate_event_line(event)
            result["activation_candidate"] = True
            result["last_activation_line"] = line
            if line_has_live_activation_candidate_policy(
                line,
                expected_activation_source_peer,
                expected_activation_destination_peer,
                expected_activation_session_id,
            ):
                result["activation_candidate_ok"] = True
    result["bad_lines"] = bad_lines
    return result


def write_live_online_summary(
    path: Path,
    *,
    args: argparse.Namespace,
    saw_live_online_capture: bool,
    saw_live_online_capture_ok: bool,
    saw_live_online_traffic_ok: bool,
    saw_live_online_boundary_violation: bool,
    saw_live_online_readiness_regression: bool,
    saw_live_activation_candidate: bool,
    saw_live_activation_candidate_ok: bool,
    last_live_activation_candidate_line: str,
    live_online_bad_lines: list[str],
    last_line: str,
    readiness_line: str,
    traffic_line: str,
) -> None:
    selected_line = traffic_line or readiness_line or last_line
    selected = (
        parse_live_online_capture_line(selected_line)
        if selected_line else {}
    )
    activation_candidate = (
        parse_live_activation_candidate_line(
            last_live_activation_candidate_line)
        if last_live_activation_candidate_line else {}
    )
    stable_readiness_ok = has_stable_live_online_readiness(
        saw_live_online_capture_ok,
        saw_live_online_boundary_violation,
        saw_live_online_readiness_regression,
    )
    stable_live_traffic_ok = saw_live_online_traffic_ok and stable_readiness_ok

    has_explicit_requirement = (
        args.require_live_online_capture
        or args.require_live_online_traffic
        or args.require_live_activation_candidate
    )
    required_ok = True
    if args.require_live_online_capture:
        required_ok = required_ok and stable_readiness_ok
    if args.require_live_online_traffic:
        required_ok = required_ok and stable_live_traffic_ok
    if args.require_live_activation_candidate:
        required_ok = (
            required_ok
            and stable_readiness_ok
            and saw_live_activation_candidate_ok
        )
    if not has_explicit_requirement:
        required_ok = stable_readiness_ok

    failure = selected.get("failure", "missing")
    if saw_live_online_boundary_violation:
        failure = "boundary-violation-seen"
    elif saw_live_online_readiness_regression:
        failure = "readiness-regression-seen"
    elif args.require_live_activation_candidate and not stable_readiness_ok:
        failure = "readiness-not-proven"
    elif (args.require_live_activation_candidate
          and not saw_live_activation_candidate_ok):
        failure = activation_candidate.get(
            "failure", "activation-candidate-not-ready")
    elif args.require_live_online_traffic and not stable_live_traffic_ok:
        failure = "live-traffic-not-proven"
    elif args.require_live_online_capture and not stable_readiness_ok:
        failure = "readiness-not-proven"
    elif not has_explicit_requirement:
        if not stable_readiness_ok:
            failure = "readiness-not-proven"
    expected_activation_source_peer = parse_int_text(
        getattr(args, "activation_source_peer", "0"), 0)
    expected_activation_destination_peer = parse_int_text(
        getattr(args, "activation_destination_peer", "0"), 0)
    expected_activation_session_id = parse_int_text(
        getattr(args, "activation_session_id", "0"), 0)

    summary = {
        "ok": bool(required_ok),
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "case": args.case,
        "request_id": args.request_id,
        "require_live_online_capture": bool(args.require_live_online_capture),
        "require_live_online_traffic": bool(args.require_live_online_traffic),
        "require_live_activation_candidate": bool(
            args.require_live_activation_candidate),
        "observed": bool(saw_live_online_capture),
        "readiness_ok": bool(saw_live_online_capture_ok),
        "live_traffic_ok": bool(saw_live_online_traffic_ok),
        "activation_candidate_observed": bool(
            saw_live_activation_candidate),
        "activation_candidate_ok": bool(
            saw_live_activation_candidate_ok),
        "activation_candidate": activation_candidate,
        "stable_readiness_ok": bool(stable_readiness_ok),
        "stable_live_traffic_ok": bool(stable_live_traffic_ok),
        "missing_live_traffic_gates": missing_live_online_gates(selected),
        "missing_activation_candidate_gates": missing_live_activation_gates(
            activation_candidate,
            expected_activation_source_peer,
            expected_activation_destination_peer,
            expected_activation_session_id,
        ),
        "boundary_violation_seen": bool(saw_live_online_boundary_violation),
        "readiness_regression_seen": bool(saw_live_online_readiness_regression),
        "bad_line_count": len(live_online_bad_lines),
        "bad_lines": live_online_bad_lines[:5],
        "selected_source": (
            "traffic" if traffic_line
            else "readiness" if readiness_line
            else "last" if last_line
            else "missing"
        ),
        "selected": selected,
        "failure": failure,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"live_online_summary={path}")


def line_has_gekko_session_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "enabled", "1")
        and line_has_field(line, "create", "1")
        and line_has_field(line, "start", "1")
        and line_has_field(line, "actors", "1")
        and line_has_field(line, "save", "1")
        and line_has_field(line, "load", "1")
        and line_has_field(line, "advance", "1")
        and line_has_field(line, "rollback_advance", "1")
        and line_has_field(line, "no_desync", "1")
        and line_has_field(line, "checksum_expected", "1")
        and line_has_field(line, "destroy", "1")
        and line_field_int(line, "frames") >= 16
        and line_field_int(line, "saves") > 0
        and line_field_int(line, "loads") > 0
        and line_field_int(line, "advances") > 0
        and line_field_int(line, "rollback_advances") > 0
    )


def line_has_gekko_adapter_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "enabled", "1")
        and line_has_field(line, "create", "1")
        and line_has_field(line, "adapter_set", "1")
        and line_has_field(line, "start", "1")
        and line_has_field(line, "actors", "1")
        and line_has_field(line, "connected", "1")
        and line_has_field(line, "session_started", "1")
        and line_has_field(line, "save", "1")
        and line_has_field(line, "load", "1")
        and line_has_field(line, "advance", "1")
        and line_has_field(line, "rollback_advance", "1")
        and line_has_field(line, "no_desync", "1")
        and line_has_field(line, "sent", "1")
        and line_has_field(line, "received", "1")
        and line_has_field(line, "freed", "1")
        and line_has_field(line, "bidirectional", "1")
        and line_has_field(line, "bridge", "1")
        and line_has_field(line, "bridge_meta", "1")
        and line_has_field(line, "bridge_reject", "1")
        and line_has_field(line, "gameplay_decode", "1")
        and line_has_field(line, "gameplay_slots", "1")
        and line_has_field(line, "gameplay_state", "1")
        and line_has_field(line, "checksums", "1")
        and line_has_field(line, "destroy", "1")
        and line_field_int(line, "frames") >= 48
        and line_field_int(line, "packets_sent") > 0
        and line_field_int(line, "packets_recv") > 0
        and line_field_int(line, "bridge_encoded") == line_field_int(line, "packets_sent")
        and line_field_int(line, "bridge_decoded") == line_field_int(line, "packets_recv")
        and line_field_int(line, "bridge_bad") == 0
        and line_field_int(line, "frees") >= line_field_int(line, "packets_recv") * 3
        and line_field_int(line, "gameplay_events") > 0
        and line_field_int(line, "gameplay_inputs") == line_field_int(line, "advances") * 2
    )


def line_has_gekko_udp_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "enabled", "1")
        and line_has_field(line, "wsa", "1")
        and line_has_field(line, "sockets", "1")
        and line_has_field(line, "loopback", "1")
        and line_has_field(line, "nonblocking", "1")
        and line_has_field(line, "manual", "1")
        and line_has_field(line, "wrong_endpoint", "1")
        and line_has_field(line, "wrong_source", "1")
        and line_has_field(line, "wrong_dest", "1")
        and line_has_field(line, "wrong_session", "1")
        and line_has_field(line, "create", "1")
        and line_has_field(line, "adapter_set", "1")
        and line_has_field(line, "start", "1")
        and line_has_field(line, "actors", "1")
        and line_has_field(line, "connected", "1")
        and line_has_field(line, "session_started", "1")
        and line_has_field(line, "save", "1")
        and line_has_field(line, "load", "1")
        and line_has_field(line, "advance", "1")
        and line_has_field(line, "rollback_advance", "1")
        and line_has_field(line, "no_desync", "1")
        and line_has_field(line, "sent", "1")
        and line_has_field(line, "received", "1")
        and line_has_field(line, "freed", "1")
        and line_has_field(line, "bidirectional", "1")
        and line_has_field(line, "bridge", "1")
        and line_has_field(line, "bridge_meta", "1")
        and line_has_field(line, "gameplay_decode", "1")
        and line_has_field(line, "gameplay_slots", "1")
        and line_has_field(line, "gameplay_state", "1")
        and line_has_field(line, "checksums", "1")
        and line_has_field(line, "destroy", "1")
        and line_field_int(line, "frames") >= 48
        and line_field_int(line, "packets_sent") > 0
        and line_field_int(line, "packets_recv") > 0
        and line_field_int(line, "bridge_encoded") == line_field_int(line, "packets_sent")
        and line_field_int(line, "bridge_decoded") == line_field_int(line, "packets_recv")
        and line_field_int(line, "bridge_bad") == 0
        and line_field_int(line, "endpoint_bad") == 0
        and line_field_int(line, "port_a") > 0
        and line_field_int(line, "port_b") > 0
        and line_field_int(line, "port_a") != line_field_int(line, "port_b")
        and line_field_int(line, "frees") >= line_field_int(line, "packets_recv") * 3
        and line_field_int(line, "gameplay_events") > 0
        and line_field_int(line, "gameplay_inputs") == line_field_int(line, "advances") * 2
    )


def line_has_gekko_gameplay_input_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "enabled", "1")
        and line_has_field(line, "raw", "1")
        and line_has_field(line, "raw_p0", "1")
        and line_has_field(line, "raw_p1", "1")
        and line_has_field(line, "null", "1")
        and line_has_field(line, "bad_frame", "1")
        and line_has_field(line, "bad_size", "1")
        and line_has_field(line, "bad_players", "1")
        and line_has_field(line, "bad_slot", "1")
        and line_has_field(line, "pipeline", "1")
        and line_has_field(line, "payload_separate", "1")
        and line_has_field(line, "create", "1")
        and line_has_field(line, "start", "1")
        and line_has_field(line, "actors", "1")
        and line_has_field(line, "advance_decode", "1")
        and line_has_field(line, "rollback_decode", "1")
        and line_has_field(line, "no_desync", "1")
        and line_has_field(line, "destroy", "1")
        and line_has_field(line, "checksum_expected", "1")
        and line_field_int(line, "decoded_events") > 0
        and line_field_int(line, "decoded_inputs") > 0
        and line_field_int(line, "frames") >= 16
        and line_field_int(line, "advances") > 0
        and line_field_int(line, "rollback_advances") > 0
    )


def line_has_live_boundary_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "hooks", "1")
        and line_field_int(line, "consumer") > 0
        and (
            line_has_field(line, "live_order", "1")
            or line_has_field(line, "offline_boundary", "1")
        )
        and line_has_field(line, "consumer_during_drain", "0")
        and line_has_field(line, "unbalanced_drain", "0")
    )


def line_has_cache_injection_policy(line: str) -> bool:
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "hooks", "1")
        and line_has_field(line, "attempted", "1")
        and line_has_field(line, "context", "1")
        and line_has_field(line, "source", "1")
        and line_has_field(line, "wrote", "1")
        and line_has_field(line, "observed", "1")
        and line_has_field(line, "restored", "1")
        and line_has_field(line, "idempotent", "1")
    )


def line_has_cache_prediction_policy(line: str) -> bool:
    original = line_field_int(line, "original")
    injected = line_field_int(line, "injected")
    observed_input = line_field_int(line, "observed_input")
    restored_input = line_field_int(line, "restored_input")
    return (
        line_has_field(line, "ok", "1")
        and line_has_field(line, "hooks", "1")
        and line_has_field(line, "attempted", "1")
        and line_has_field(line, "context", "1")
        and line_has_field(line, "source", "1")
        and line_has_field(line, "wrote", "1")
        and line_has_field(line, "observed", "1")
        and line_has_field(line, "restored", "1")
        and line_has_field(line, "restored_cur", "1")
        and line_has_field(line, "restored_pair", "1")
        and line_has_field(line, "non_idempotent", "1")
        and line_has_field(line, "differs", "1")
        and line_has_field(line, "pair_observed", "1")
        and injected != original
        and observed_input == injected
        and restored_input == original
    )


def rollback_game_args(args: argparse.Namespace) -> list[str]:
    cmd = [
        "--horsemod-rollback-lab",
        f"--horsemod-rollback-case={args.case}",
        f"--horsemod-rollback-window={args.rollback_window}",
        f"--horsemod-rollback-seed={args.seed}",
    ]
    if args.request_id:
        cmd.append(f"--horsemod-rollback-request-id={args.request_id}")
    if args.trace:
        cmd.append("--horsemod-rollback-trace")
    if args.arm_live_activation:
        cmd.append("--horsemod-rollback-live-activation-arm")
    cmd.append(
        f"--horsemod-rollback-live-source-peer={args.activation_source_peer}")
    cmd.append(
        "--horsemod-rollback-live-destination-peer="
        f"{args.activation_destination_peer}")
    cmd.append(
        f"--horsemod-rollback-live-session-id={args.activation_session_id}")
    if args.client_role:
        cmd.append(f"--horsemod-rollback-client-role={args.client_role}")
    if args.sandbox_root:
        cmd.append(f"--horsemod-rollback-sandbox-root={args.sandbox_root}")
    if args.sandbox_box:
        cmd.append(f"--horsemod-rollback-sandbox-box={args.sandbox_box}")
    if args.local_peer_id:
        cmd.append(f"--horsemod-rollback-local-peer={args.local_peer_id}")
    if args.remote_peer_id:
        cmd.append(f"--horsemod-rollback-remote-peer={args.remote_peer_id}")
    if args.sidecar_local_port:
        cmd.append(
            "--horsemod-rollback-sidecar-local-port="
            f"{args.sidecar_local_port}")
    if args.sidecar_remote_port:
        cmd.append(
            "--horsemod-rollback-sidecar-remote-port="
            f"{args.sidecar_remote_port}")
    if args.sidecar_remote_addr:
        cmd.append(
            "--horsemod-rollback-sidecar-remote-addr="
            f"{args.sidecar_remote_addr}")
    if args.activation_token:
        cmd.append(
            f"--horsemod-rollback-activation-token={args.activation_token}")
    if args.force_live_prediction_divergence:
        cmd.append(
            "--horsemod-rollback-force-live-prediction-divergence")
    if args.output:
        cmd.append(f"--horsemod-rollback-output={args.output}")
    return cmd


def use_request_file(args: argparse.Namespace) -> bool:
    return args.launch_via_steam or not args.force_command_line_args


def find_steam_exe() -> Path | None:
    for root, subkey in (
        (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam"),
    ):
        try:
            with winreg.OpenKey(root, subkey) as key:
                value, _ = winreg.QueryValueEx(key, "SteamExe")
        except OSError:
            continue
        path = Path(os.path.normpath(value))
        if path.exists():
            return path
    for raw in (
        r"C:\Program Files (x86)\Steam\steam.exe",
        r"C:\Program Files\Steam\steam.exe",
    ):
        path = Path(raw)
        if path.exists():
            return path
    return None


def request_file_text(args: argparse.Namespace) -> str:
    lines = [
        "enabled=1",
        f"trace={1 if args.trace else 0}",
        f"case={args.case}",
        f"window={args.rollback_window}",
        f"seed={args.seed}",
    ]
    if args.request_id:
        lines.append(f"request_id={args.request_id}")
    lines.append(f"activation_arm={1 if args.arm_live_activation else 0}")
    lines.append(f"activation_source_peer={args.activation_source_peer}")
    lines.append(
        f"activation_destination_peer={args.activation_destination_peer}")
    lines.append(f"activation_session_id={args.activation_session_id}")
    if args.client_role:
        lines.append(f"client_role={args.client_role}")
    if args.sandbox_root:
        lines.append(f"sandbox_root={args.sandbox_root}")
    if args.sandbox_box:
        lines.append(f"sandbox_box={args.sandbox_box}")
    if args.local_peer_id:
        lines.append(f"local_peer_id={args.local_peer_id}")
    if args.remote_peer_id:
        lines.append(f"remote_peer_id={args.remote_peer_id}")
    if args.sidecar_local_port:
        lines.append(f"sidecar_local_port={args.sidecar_local_port}")
    if args.sidecar_remote_port:
        lines.append(f"sidecar_remote_port={args.sidecar_remote_port}")
    if args.sidecar_remote_addr:
        lines.append(f"sidecar_remote_addr={args.sidecar_remote_addr}")
    if args.activation_token:
        lines.append(f"activation_token={args.activation_token}")
    lines.append(
        "force_live_prediction_divergence="
        f"{1 if args.force_live_prediction_divergence else 0}")
    if args.output:
        lines.append(f"output={args.output}")
    return "\n".join(lines) + "\n"


def write_request_file(args: argparse.Namespace) -> None:
    SAVED_DIR.mkdir(parents=True, exist_ok=True)
    tmp = REQUEST_FILE.with_name(
        f".{REQUEST_FILE.name}.{os.getpid()}.{time.time_ns()}.tmp")
    try:
        with tmp.open("w", encoding="utf-8", newline="\n") as f:
            f.write(request_file_text(args))
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, REQUEST_FILE)
    finally:
        try:
            tmp.unlink()
        except FileNotFoundError:
            pass


def cleanup_request_file() -> None:
    try:
        REQUEST_FILE.unlink()
    except FileNotFoundError:
        pass


def reset_launch_log() -> None:
    try:
        UE4SS_LOG.unlink()
    except FileNotFoundError:
        pass
    except PermissionError:
        # If another process still owns the file, the reader below falls back
        # to normal truncation detection once the next SC6 launch rewrites it.
        pass


def kill_game() -> bool:
    image_name = "SoulcaliburVI.exe"
    pids = set(process_ids_by_image(image_name))
    if not pids:
        return True
    terminate_process_ids(pids)
    return wait_for_image_exit(image_name, pids, 5.0)


def launch_game(args: argparse.Namespace) -> subprocess.Popen[str] | None:
    if args.launch_via_steam:
        os.startfile(f"steam://rungameid/{STEAM_APPID}")  # type: ignore[attr-defined]
        return None
    game_args = [] if use_request_file(args) else rollback_game_args(args)
    cmd = [str(GAME_EXE), *game_args]
    return subprocess.Popen(cmd, cwd=str(GAME_EXE.parent))


def read_new_log(pos: int) -> tuple[int, str]:
    if not UE4SS_LOG.exists():
        return pos, ""
    data = UE4SS_LOG.read_text(encoding="utf-8", errors="replace")
    if pos > len(data):
        pos = 0
    return len(data), data[pos:]


def latest_trace_file(since: float) -> Path | None:
    if not TRACE_DIR.exists():
        return None
    candidates = [
        path for path in TRACE_DIR.glob("replay_trace_*.jsonl")
        if path.stat().st_mtime >= since - 5.0
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda path: path.stat().st_mtime)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--kill-game", action="store_true")
    p.add_argument("--launch-game", action="store_true")
    p.add_argument("--launch-via-steam", action="store_true")
    p.add_argument("--force-command-line-args", action="store_true")
    p.add_argument("--kill-after", action="store_true")
    p.add_argument("--cleanup-request-after", action="store_true")
    p.add_argument("--case", default="baseline-oracle")
    p.add_argument("--rollback-window", type=int, default=12)
    p.add_argument("--seed", default="0x5C6B0001")
    p.add_argument("--request-id", default="")
    p.add_argument("--trace", action="store_true")
    p.add_argument("--output", default="")
    p.add_argument("--arm-live-activation", action="store_true")
    p.add_argument("--activation-source-peer", default="0xA0")
    p.add_argument("--activation-destination-peer", default="0xB0")
    p.add_argument("--activation-session-id", default="0x4C495645414354")
    p.add_argument("--client-role", default="")
    p.add_argument("--sandbox-root", default="")
    p.add_argument("--sandbox-box", default="")
    p.add_argument("--local-peer-id", default="")
    p.add_argument("--remote-peer-id", default="")
    p.add_argument("--sidecar-local-port", type=int, default=0)
    p.add_argument("--sidecar-remote-port", type=int, default=0)
    p.add_argument("--sidecar-remote-addr", default="127.0.0.1")
    p.add_argument("--activation-token", default="")
    p.add_argument(
        "--force-live-prediction-divergence",
        action="store_true")
    p.add_argument("--watch-seconds", type=float, default=30.0)
    p.add_argument("--strict", action="store_true")
    p.add_argument("--require-service-tick", action="store_true")
    p.add_argument("--require-snapshot-roundtrip", action="store_true")
    p.add_argument("--require-hgcpu-roundtrip", action="store_true")
    p.add_argument("--require-resim-window", action="store_true")
    p.add_argument("--require-resim-matrix", action="store_true")
    p.add_argument("--require-cache-ownership", action="store_true")
    p.add_argument("--require-online-session", action="store_true")
    p.add_argument("--require-live-transport", action="store_true")
    p.add_argument("--require-live-peer-pipeline", action="store_true")
    p.add_argument("--require-end-to-end", action="store_true")
    p.add_argument("--require-live-activation", action="store_true")
    p.add_argument("--require-live-activation-executor", action="store_true")
    p.add_argument("--require-stock-transport", action="store_true")
    p.add_argument("--require-stock-observe", action="store_true")
    p.add_argument("--require-stock-observe-live", action="store_true")
    p.add_argument("--require-stock-observe-session", action="store_true")
    p.add_argument("--require-stock-observe-input", action="store_true")
    p.add_argument("--require-stock-observe-battle-sync", action="store_true")
    p.add_argument("--require-stock-observe-receive", action="store_true")
    p.add_argument("--require-live-online-capture", action="store_true")
    p.add_argument("--require-live-online-traffic", action="store_true")
    p.add_argument("--require-live-activation-candidate", action="store_true")
    p.add_argument("--live-online-summary-output", type=Path)
    p.add_argument("--require-gekko-session", action="store_true")
    p.add_argument("--require-gekko-adapter", action="store_true")
    p.add_argument("--require-gekko-udp", action="store_true")
    p.add_argument("--require-gekko-gameplay-input", action="store_true")
    p.add_argument("--require-online-boundary", action="store_true")
    p.add_argument("--require-cache-injection", action="store_true")
    p.add_argument("--require-cache-prediction", action="store_true")
    p.add_argument("--require-prediction-diff", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()
    expected_activation_source_peer = parse_int_text(
        args.activation_source_peer)
    expected_activation_destination_peer = parse_int_text(
        args.activation_destination_peer)
    expected_activation_session_id = parse_int_text(
        args.activation_session_id)

    if args.launch_via_steam:
        cmd_preview = [f"steam://rungameid/{STEAM_APPID}"]
    else:
        cmd_preview = [
            str(GAME_EXE),
            *([] if use_request_file(args) else rollback_game_args(args)),
        ]
    print(" ".join(cmd_preview))
    if use_request_file(args):
        print(f"request_file={REQUEST_FILE}")
        print(request_file_text(args), end="")
    if args.dry_run:
        return 0

    if args.kill_game:
        if not kill_game():
            print("failed to terminate existing SoulcaliburVI.exe", file=sys.stderr)
            return 1
        time.sleep(1.0)

    trace_since = time.time()
    if args.launch_game:
        reset_launch_log()

    if use_request_file(args):
        cleanup_request_file()
        write_request_file(args)
    else:
        cleanup_request_file()

    start_pos = 0 if args.launch_game else (
        UE4SS_LOG.stat().st_size if UE4SS_LOG.exists() else 0
    )
    proc: subprocess.Popen[str] | None = None
    if args.launch_game:
        proc = launch_game(args)

    deadline = time.time() + args.watch_seconds
    saw_config = False
    saw_tick = False
    saw_snapshot_roundtrip = False
    saw_snapshot_roundtrip_ok = False
    saw_hgcpu_roundtrip = False
    saw_hgcpu_roundtrip_ok = False
    saw_resim_window = False
    saw_resim_window_ok = False
    saw_resim_window_strong_ok = False
    saw_prediction_diff = False
    saw_resim_matrix = False
    saw_resim_matrix_ok = False
    saw_cache_ownership = False
    saw_cache_ownership_ok = False
    saw_online_session = False
    saw_online_session_ok = False
    saw_live_transport = False
    saw_live_transport_ok = False
    saw_live_peer_pipeline = False
    saw_live_peer_pipeline_ok = False
    saw_end_to_end = False
    saw_end_to_end_ok = False
    saw_live_activation = False
    saw_live_activation_ok = False
    saw_live_activation_executor = False
    saw_live_activation_executor_ok = False
    saw_stock_transport = False
    saw_stock_transport_ok = False
    saw_stock_observe = False
    saw_stock_observe_ok = False
    saw_stock_observe_live_ok = False
    saw_live_online_capture = False
    saw_live_online_capture_ok = False
    saw_live_online_traffic_ok = False
    saw_live_online_boundary_violation = False
    saw_live_online_readiness_regression = False
    saw_live_activation_candidate = False
    saw_live_activation_candidate_ok = False
    live_online_bad_lines: list[str] = []
    last_live_online_capture_line = ""
    last_live_online_readiness_line = ""
    last_live_online_traffic_line = ""
    last_live_activation_candidate_line = ""
    saw_gekko_session = False
    saw_gekko_session_ok = False
    saw_gekko_adapter = False
    saw_gekko_adapter_ok = False
    saw_gekko_udp = False
    saw_gekko_udp_ok = False
    saw_gekko_gameplay_input = False
    saw_gekko_gameplay_input_ok = False
    saw_live_boundary = False
    saw_live_boundary_ok = False
    saw_cache_injection = False
    saw_cache_injection_ok = False
    saw_cache_prediction = False
    saw_cache_prediction_ok = False
    matrix_ok: set[tuple[str, int]] = set()
    matrix_predicted_diff: set[int] = set()

    def strict_gates_satisfied() -> bool:
        if not saw_config:
            return False
        if not args.strict:
            return True
        if (
            args.require_live_online_capture
            or args.require_live_online_traffic
            or args.require_live_activation_candidate
        ):
            return False
        if args.require_service_tick and not saw_tick:
            return False
        if args.require_snapshot_roundtrip and not saw_snapshot_roundtrip_ok:
            return False
        if args.require_hgcpu_roundtrip and not saw_hgcpu_roundtrip_ok:
            return False
        if args.require_resim_window and not saw_resim_window_strong_ok:
            return False
        if args.require_resim_matrix:
            expected = {
                ("baseline-oracle", 1),
                ("baseline-oracle", 2),
                ("baseline-oracle", 8),
                ("baseline-oracle", 15),
                ("baseline-oracle", 60),
                ("delayed-input", 1),
                ("delayed-input", 2),
                ("delayed-input", 8),
                ("delayed-input", 15),
                ("delayed-input", 60),
            }
            if not saw_resim_matrix_ok or expected - matrix_ok:
                return False
            if {1, 2, 8, 15, 60} - matrix_predicted_diff:
                return False
        if args.require_cache_ownership and not saw_cache_ownership_ok:
            return False
        if args.require_online_session and not saw_online_session_ok:
            return False
        if args.require_live_transport and not saw_live_transport_ok:
            return False
        if args.require_live_peer_pipeline and not saw_live_peer_pipeline_ok:
            return False
        if args.require_end_to_end and not saw_end_to_end_ok:
            return False
        if args.require_live_activation and not saw_live_activation_ok:
            return False
        if (
            args.require_live_activation_executor
            and not saw_live_activation_executor_ok
        ):
            return False
        if args.require_stock_transport and not saw_stock_transport_ok:
            return False
        if args.require_stock_observe and not saw_stock_observe_ok:
            return False
        stock_observe_live_required = (
            args.require_stock_observe_live
            or args.require_stock_observe_session
            or args.require_stock_observe_input
            or args.require_stock_observe_battle_sync
            or args.require_stock_observe_receive
        )
        if stock_observe_live_required and not saw_stock_observe_live_ok:
            return False
        if args.require_gekko_session and not saw_gekko_session_ok:
            return False
        if args.require_gekko_adapter and not saw_gekko_adapter_ok:
            return False
        if args.require_gekko_udp and not saw_gekko_udp_ok:
            return False
        if (
            args.require_gekko_gameplay_input
            and not saw_gekko_gameplay_input_ok
        ):
            return False
        if args.require_online_boundary and not saw_live_boundary_ok:
            return False
        if args.require_cache_injection and not saw_cache_injection_ok:
            return False
        if args.require_cache_prediction and not saw_cache_prediction_ok:
            return False
        if args.require_prediction_diff and not saw_prediction_diff:
            return False
        return True

    pos = start_pos
    while time.time() < deadline:
        pos, chunk = read_new_log(pos)
        if "[RollbackLab] configured enabled=1" in chunk:
            saw_config = True
        if "rollback_lab_service_tick" in chunk or "[RollbackLab]" in chunk:
            saw_tick = saw_tick or "service_tick" in chunk
        if "[RollbackLab] snapshot_roundtrip" in chunk:
            saw_snapshot_roundtrip = True
            if "snapshot_roundtrip ok=1" in chunk:
                saw_snapshot_roundtrip_ok = True
        if "[RollbackLab] hgcpu_roundtrip" in chunk:
            saw_hgcpu_roundtrip = True
            if "hgcpu_roundtrip ok=1" in chunk:
                saw_hgcpu_roundtrip_ok = True
        if "[RollbackLab] resim_window" in chunk:
            saw_resim_window = True
            if "resim_window ok=1" in chunk:
                saw_resim_window_ok = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] resim_window" in line
                    and "resim_window ok=1" in line
                    and line_has_strong_resim_policy(line)
                ):
                    saw_resim_window_strong_ok = True
            if "predicted_diff=1" in chunk:
                saw_prediction_diff = True
            for line in chunk.splitlines():
                if "[RollbackLab] resim_window" not in line:
                    continue
                window = None
                if "window=" in line:
                    raw = line.split("window=", 1)[1].split(None, 1)[0]
                    try:
                        window = int(raw)
                    except ValueError:
                        window = None
                if window not in {1, 2, 8, 15, 60}:
                    continue
                if "resim_window ok=1" not in line:
                    continue
                if not line_has_strong_resim_policy(line):
                    continue
                if "case=baseline-oracle" in line:
                    matrix_ok.add(("baseline-oracle", window))
                if "case=delayed-input" in line:
                    matrix_ok.add(("delayed-input", window))
                    if "predicted_diff=1" in line:
                        matrix_predicted_diff.add(window)
        if "[RollbackLab] resim_matrix" in chunk:
            saw_resim_matrix = True
            if "resim_matrix ok=1" in chunk:
                saw_resim_matrix_ok = True
        if "[RollbackLab] cache_ownership" in chunk:
            saw_cache_ownership = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] cache_ownership" in line
                    and "cache_ownership ok=1" in line
                    and line_has_strong_cache_policy(line)
                ):
                    saw_cache_ownership_ok = True
        if "[RollbackLab] online_session" in chunk:
            saw_online_session = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] online_session" in line
                    and line_has_online_session_policy(line)
                ):
                    saw_online_session_ok = True
        if "[RollbackLab] live_transport" in chunk:
            saw_live_transport = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] live_transport" in line
                    and line_has_live_transport_policy(line)
                ):
                    saw_live_transport_ok = True
        if "[RollbackLab] live_peer_pipeline" in chunk:
            saw_live_peer_pipeline = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] live_peer_pipeline" in line
                    and line_has_live_peer_pipeline_policy(line)
                ):
                    saw_live_peer_pipeline_ok = True
        if "[RollbackLab] end_to_end" in chunk:
            saw_end_to_end = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] end_to_end" in line
                    and line_has_end_to_end_policy(line)
                ):
                    saw_end_to_end_ok = True
        if "[RollbackLab] live_activation " in chunk:
            saw_live_activation = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] live_activation " in line
                    and line_has_live_activation_policy(line)
                ):
                    saw_live_activation_ok = True
        if "[RollbackLab] live_activation_executor" in chunk:
            saw_live_activation_executor = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] live_activation_executor" in line
                    and line_has_live_activation_executor_policy(line)
                ):
                    saw_live_activation_executor_ok = True
        if "[RollbackLab] stock_transport" in chunk:
            saw_stock_transport = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] stock_transport" in line
                    and line_has_stock_transport_policy(line)
                ):
                    saw_stock_transport_ok = True
        if "[RollbackLab] stock_observe" in chunk:
            saw_stock_observe = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] stock_observe" in line
                    and line_has_stock_observe_policy(line)
                ):
                    saw_stock_observe_ok = True
                if (
                    "[RollbackLab] stock_observe" in line
                    and line_has_stock_observe_live_policy(
                        line,
                        args.require_stock_observe_session,
                        args.require_stock_observe_input,
                        args.require_stock_observe_battle_sync,
                        args.require_stock_observe_receive,
                    )
                ):
                    saw_stock_observe_live_ok = True
        if "[RollbackLab] live_online_capture" in chunk:
            for line in chunk.splitlines():
                if "[RollbackLab] live_online_capture" not in line:
                    continue
                if not line_matches_request_id(line, args.request_id):
                    continue
                saw_live_online_capture = True
                parsed = parse_live_online_capture_line(line)
                last_live_online_capture_line = line
                if parsed.get("boundary_violation") != 0:
                    saw_live_online_boundary_violation = True
                    if line not in live_online_bad_lines:
                        live_online_bad_lines.append(line)
                line_ready = line_has_live_online_capture_policy(line)
                line_live = line_has_live_online_traffic_policy(line)
                if saw_live_online_capture_ok and not line_ready:
                    saw_live_online_readiness_regression = True
                    if line not in live_online_bad_lines:
                        live_online_bad_lines.append(line)
                if line_ready:
                    saw_live_online_capture_ok = True
                    last_live_online_readiness_line = line
                if line_live:
                    saw_live_online_traffic_ok = True
                    last_live_online_traffic_line = line
        if "[RollbackLab] live_activation_candidate" in chunk:
            for line in chunk.splitlines():
                if "[RollbackLab] live_activation_candidate" not in line:
                    continue
                if not line_matches_request_id(line, args.request_id):
                    continue
                saw_live_activation_candidate = True
                last_live_activation_candidate_line = line
                if line_has_live_activation_candidate_policy(
                    line,
                    expected_activation_source_peer,
                    expected_activation_destination_peer,
                    expected_activation_session_id,
                ):
                    saw_live_activation_candidate_ok = True
        if "[RollbackLab] gekko_session" in chunk:
            saw_gekko_session = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] gekko_session" in line
                    and line_has_gekko_session_policy(line)
                ):
                    saw_gekko_session_ok = True
        if "[RollbackLab] gekko_adapter" in chunk:
            saw_gekko_adapter = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] gekko_adapter" in line
                    and line_has_gekko_adapter_policy(line)
                ):
                    saw_gekko_adapter_ok = True
        if "[RollbackLab] gekko_udp" in chunk:
            saw_gekko_udp = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] gekko_udp" in line
                    and line_has_gekko_udp_policy(line)
                ):
                    saw_gekko_udp_ok = True
        if "[RollbackLab] gekko_gameplay_input" in chunk:
            saw_gekko_gameplay_input = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] gekko_gameplay_input" in line
                    and line_has_gekko_gameplay_input_policy(line)
                ):
                    saw_gekko_gameplay_input_ok = True
        if "[RollbackLab] live_boundary" in chunk:
            saw_live_boundary = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] live_boundary" in line
                    and line_has_live_boundary_policy(line)
                ):
                    saw_live_boundary_ok = True
        if "[RollbackLab] cache_injection" in chunk:
            saw_cache_injection = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] cache_injection" in line
                    and line_has_cache_injection_policy(line)
                ):
                    saw_cache_injection_ok = True
        if "[RollbackLab] cache_prediction" in chunk:
            saw_cache_prediction = True
            for line in chunk.splitlines():
                if (
                    "[RollbackLab] cache_prediction" in line
                    and line_has_cache_prediction_policy(line)
                ):
                    saw_cache_prediction_ok = True
        if strict_gates_satisfied():
            break
        time.sleep(0.5)

    if proc is not None and proc.poll() is not None and not args.launch_via_steam:
        print(f"game exited early with code {proc.returncode}")

    kill_after_ok = True
    if args.kill_after:
        kill_after_ok = kill_game()
        if not kill_after_ok:
            print("failed to terminate SoulcaliburVI.exe after lab", file=sys.stderr)
        if args.launch_via_steam:
            cleanup_request_file()

    trace_file: Path | None = None
    if args.trace:
        trace_file = latest_trace_file(trace_since)
        if trace_file is not None:
            print(f"trace_file={trace_file}")

    if trace_file is not None and (
        args.require_live_online_capture
        or args.require_live_online_traffic
        or args.require_live_activation_candidate
        or args.live_online_summary_output
    ):
        trace_live = merge_live_online_trace_observations(
            trace_file,
            request_id=args.request_id,
            expected_activation_source_peer=expected_activation_source_peer,
            expected_activation_destination_peer=(
                expected_activation_destination_peer),
            expected_activation_session_id=expected_activation_session_id,
        )
        if bool(trace_live.get("capture")):
            saw_live_online_capture = True
            last_live_online_capture_line = str(trace_live.get("last_line") or "")
        if bool(trace_live.get("capture_ok")):
            saw_live_online_capture_ok = True
            last_live_online_readiness_line = str(
                trace_live.get("readiness_line") or "")
        if bool(trace_live.get("traffic_ok")):
            saw_live_online_traffic_ok = True
            last_live_online_traffic_line = str(
                trace_live.get("traffic_line") or "")
        if bool(trace_live.get("boundary_violation")):
            saw_live_online_boundary_violation = True
        if bool(trace_live.get("readiness_regression")):
            saw_live_online_readiness_regression = True
        if bool(trace_live.get("activation_candidate")):
            saw_live_activation_candidate = True
            last_live_activation_candidate_line = str(
                trace_live.get("last_activation_line") or "")
        if bool(trace_live.get("activation_candidate_ok")):
            saw_live_activation_candidate_ok = True
        for bad_line in trace_live.get("bad_lines") or []:
            if bad_line not in live_online_bad_lines:
                live_online_bad_lines.append(str(bad_line))

    if args.live_online_summary_output:
        write_live_online_summary(
            args.live_online_summary_output,
            args=args,
            saw_live_online_capture=saw_live_online_capture,
            saw_live_online_capture_ok=saw_live_online_capture_ok,
            saw_live_online_traffic_ok=saw_live_online_traffic_ok,
            saw_live_online_boundary_violation=saw_live_online_boundary_violation,
            saw_live_online_readiness_regression=saw_live_online_readiness_regression,
            saw_live_activation_candidate=saw_live_activation_candidate,
            saw_live_activation_candidate_ok=saw_live_activation_candidate_ok,
            last_live_activation_candidate_line=last_live_activation_candidate_line,
            live_online_bad_lines=live_online_bad_lines,
            last_line=last_live_online_capture_line,
            readiness_line=last_live_online_readiness_line,
            traffic_line=last_live_online_traffic_line,
        )

    if args.cleanup_request_after and use_request_file(args):
        cleanup_request_file()

    if not kill_after_ok:
        return 1

    if args.strict and not saw_config:
        print("rollback lab configuration line not observed", file=sys.stderr)
        return 1
    if args.strict and args.require_service_tick and not saw_tick:
        print("rollback lab service activity not observed", file=sys.stderr)
        return 1
    if args.strict and args.require_snapshot_roundtrip:
        if not saw_snapshot_roundtrip:
            print("rollback snapshot roundtrip not observed", file=sys.stderr)
            return 1
        if not saw_snapshot_roundtrip_ok:
            print("rollback snapshot roundtrip did not report ok=1", file=sys.stderr)
            return 1
    if args.strict and args.require_hgcpu_roundtrip:
        if not saw_hgcpu_roundtrip:
            print("rollback HgCpuDirect roundtrip not observed", file=sys.stderr)
            return 1
        if not saw_hgcpu_roundtrip_ok:
            print("rollback HgCpuDirect roundtrip did not report ok=1", file=sys.stderr)
            return 1
    if args.strict and args.require_resim_window:
        if not saw_resim_window:
            print("rollback resim window not observed", file=sys.stderr)
            return 1
        if not saw_resim_window_ok:
            print("rollback resim window did not report ok=1", file=sys.stderr)
            return 1
        if not saw_resim_window_strong_ok:
            print(
                "rollback resim window did not report strong state policy",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_resim_matrix:
        expected = {
            ("baseline-oracle", 1),
            ("baseline-oracle", 2),
            ("baseline-oracle", 8),
            ("baseline-oracle", 15),
            ("baseline-oracle", 60),
            ("delayed-input", 1),
            ("delayed-input", 2),
            ("delayed-input", 8),
            ("delayed-input", 15),
            ("delayed-input", 60),
        }
        missing = sorted(expected - matrix_ok)
        missing_pred = sorted({1, 2, 8, 15, 60} - matrix_predicted_diff)
        if not saw_resim_matrix:
            print("rollback resim matrix summary not observed", file=sys.stderr)
            return 1
        if not saw_resim_matrix_ok:
            print("rollback resim matrix did not report ok=1", file=sys.stderr)
            return 1
        if missing:
            print(f"rollback resim matrix missing ok windows: {missing}", file=sys.stderr)
            return 1
        if missing_pred:
            print(
                f"rollback resim matrix missing delayed prediction diff windows: {missing_pred}",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_cache_ownership:
        if not saw_cache_ownership:
            print("rollback cache ownership probe not observed", file=sys.stderr)
            return 1
        if not saw_cache_ownership_ok:
            print(
                "rollback cache ownership probe did not report ok=1 with strong state policy",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_online_session:
        if not saw_online_session:
            print("rollback online-session self-test not observed", file=sys.stderr)
            return 1
        if not saw_online_session_ok:
            print(
                "rollback online-session self-test did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_live_transport:
        if not saw_live_transport:
            print("rollback live-transport self-test not observed", file=sys.stderr)
            return 1
        if not saw_live_transport_ok:
            print(
                "rollback live-transport self-test did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_live_peer_pipeline:
        if not saw_live_peer_pipeline:
            print("rollback live-peer-pipeline self-test not observed", file=sys.stderr)
            return 1
        if not saw_live_peer_pipeline_ok:
            print(
                "rollback live-peer-pipeline did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_end_to_end:
        if not saw_end_to_end:
            print("rollback end-to-end self-test not observed", file=sys.stderr)
            return 1
        if not saw_end_to_end_ok:
            print(
                "rollback end-to-end self-test did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_live_activation:
        if not saw_live_activation:
            print("rollback live-activation self-test not observed", file=sys.stderr)
            return 1
        if not saw_live_activation_ok:
            print(
                "rollback live-activation self-test did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_live_activation_executor:
        if not saw_live_activation_executor:
            print(
                "rollback live-activation-executor self-test not observed",
                file=sys.stderr,
            )
            return 1
        if not saw_live_activation_executor_ok:
            print(
                "rollback live-activation-executor self-test did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_stock_transport:
        if not saw_stock_transport:
            print("rollback stock-transport self-test not observed", file=sys.stderr)
            return 1
        if not saw_stock_transport_ok:
            print(
                "rollback stock-transport self-test did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_stock_observe:
        if not saw_stock_observe:
            print("rollback stock-observe trace not observed", file=sys.stderr)
            return 1
        if not saw_stock_observe_ok:
            print(
                "rollback stock-observe trace did not report hook gates",
                file=sys.stderr,
            )
            return 1
    stock_observe_live_required = (
        args.require_stock_observe_live
        or args.require_stock_observe_session
        or args.require_stock_observe_input
        or args.require_stock_observe_battle_sync
        or args.require_stock_observe_receive
    )
    if args.strict and stock_observe_live_required:
        if not saw_stock_observe:
            print("rollback stock-observe trace not observed", file=sys.stderr)
            return 1
        if not saw_stock_observe_live_ok:
            print(
                "rollback stock-observe did not report required live traffic",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_live_online_capture:
        if not saw_live_online_capture:
            print("rollback live-online capture not observed", file=sys.stderr)
            return 1
        if not saw_live_online_capture_ok:
            print(
                "rollback live-online capture did not report readiness gates",
                file=sys.stderr,
            )
            return 1
        if saw_live_online_boundary_violation or saw_live_online_readiness_regression:
            print(
                "rollback live-online capture reported a later boundary violation/regression",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_live_online_traffic:
        if not saw_live_online_capture:
            print("rollback live-online capture not observed", file=sys.stderr)
            return 1
        if not saw_live_online_traffic_ok:
            print(
                "rollback live-online capture did not report required live traffic/order",
                file=sys.stderr,
            )
            return 1
        if saw_live_online_boundary_violation or saw_live_online_readiness_regression:
            print(
                "rollback live-online capture reported a later boundary violation/regression",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_live_activation_candidate:
        activation_failure = live_activation_candidate_strict_failure(
            saw_live_online_capture_ok,
            saw_live_online_boundary_violation,
            saw_live_online_readiness_regression,
            saw_live_activation_candidate,
            saw_live_activation_candidate_ok,
        )
        if activation_failure:
            print(activation_failure, file=sys.stderr)
            return 1
    if args.strict and args.require_gekko_session:
        if not saw_gekko_session:
            print("rollback Gekko session self-test not observed", file=sys.stderr)
            return 1
        if not saw_gekko_session_ok:
            print(
                "rollback Gekko session self-test did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_gekko_adapter:
        if not saw_gekko_adapter:
            print("rollback Gekko adapter self-test not observed", file=sys.stderr)
            return 1
        if not saw_gekko_adapter_ok:
            print(
                "rollback Gekko adapter self-test did not report callback/checksum gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_gekko_udp:
        if not saw_gekko_udp:
            print("rollback Gekko UDP self-test not observed", file=sys.stderr)
            return 1
        if not saw_gekko_udp_ok:
            print(
                "rollback Gekko UDP self-test did not report socket/identity/checksum gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_gekko_gameplay_input:
        if not saw_gekko_gameplay_input:
            print(
                "rollback Gekko gameplay-input bridge self-test not observed",
                file=sys.stderr,
            )
            return 1
        if not saw_gekko_gameplay_input_ok:
            print(
                "rollback Gekko gameplay-input bridge did not report all gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_online_boundary:
        if not saw_live_boundary:
            print("rollback live-boundary probe not observed", file=sys.stderr)
            return 1
        if not saw_live_boundary_ok:
            print(
                "rollback live-boundary probe did not report ordering gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_cache_injection:
        if not saw_cache_injection:
            print("rollback cache-injection probe not observed", file=sys.stderr)
            return 1
        if not saw_cache_injection_ok:
            print(
                "rollback cache-injection probe did not report write/read/restore gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_cache_prediction:
        if not saw_cache_prediction:
            print(
                "rollback cache-prediction probe not observed "
                "(requires an active replay/battle cache consumer; "
                "use replay_seek_test_run.py for automated validation)",
                file=sys.stderr,
            )
            return 1
        if not saw_cache_prediction_ok:
            print(
                "rollback cache-prediction probe did not report "
                "non-idempotent write/read/restore gates",
                file=sys.stderr,
            )
            return 1
    if args.strict and args.require_prediction_diff and not saw_prediction_diff:
        print("rollback resim did not report predicted_diff=1", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
