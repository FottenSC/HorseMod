#!/usr/bin/env python3
"""Analyze HorseMod replay seek test JSONL traces."""

from __future__ import annotations

import argparse
import ctypes
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


DEFAULT_TRACE_DIR = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace"
)
HITCUE_FIELD_RE = re.compile(r"^hitcue(\d+)-")
DEFAULT_MIN_RESUME_TICK_RATE = 58.0
DEFAULT_RESUME_TICK_WINDOW = 120
DEFAULT_MAX_SEEK_VALIDATION_SECONDS = 0.5
DEFAULT_MAX_RESUME_TICK_GAP_SECONDS = 0.100
DEFAULT_MAX_FIRST_RESUME_TICK_SECONDS = 0.200
DEFAULT_MAX_SEEK_QUEUE_SECONDS = 0.250
DEFAULT_MAX_SEEK_LAND_SECONDS = 0.500
DEFAULT_MAX_SEEK_RESUME_HANDOFF_SECONDS = 0.250
DEFAULT_MAX_SEEK_TOTAL_RESUME_SECONDS = 1.000
PER_TICK_ASSIST_EVENTS = {
    "resume_motion_provider_cache_restore",
    "resume_movement_scalar_restore",
    "resume_hit_reaction_state_restore",
    "resume_hit_cue_state_restore",
    "resume_vital_state_restore",
    "resume_oracle_playback_overlay",
}
UI_STEP_ACTIONS = {
    "ui_step_forward_one",
    "ui-step-forward-one",
    "step_forward_one",
    "step-forward-one",
    "+1",
    "ui_step_relative",
    "ui-step-relative",
    "step_relative",
    "step-relative",
    "ui_step_many",
    "ui-step-many",
    "ui_step_backward_many",
    "ui-step-backward-many",
    "step_backward_many",
    "step-backward-many",
    "ui_step_back_many",
    "ui-step-back-many",
    "ui_step_then_play",
    "ui-step-then-play",
    "ui_step_play",
    "ui-step-play",
    "ui_step_backward_many_play",
    "ui-step-backward-many-play",
    "ui_step_back_many_play",
    "ui-step-back-many-play",
}


def ui_step_expected_delta_count(result: dict[str, Any]) -> tuple[int, int]:
    action = str(result.get("action") or "")
    if action in {
        "ui_step_forward_one",
        "ui-step-forward-one",
        "step_forward_one",
        "step-forward-one",
        "+1",
    }:
        return 1, 1
    delta = int_field(result.get("ui_step_delta"))
    if delta == 0:
        delta = int_field(result.get("step_delta"))
    if delta == 0:
        delta = -1 if "back" in action or "backward" in action else 1
    count = int_field(result.get("ui_step_count"))
    if count <= 0:
        count = int_field(result.get("step_count"))
    if count <= 0:
        count = 1
    return delta, count


def active_case_seek_label(current_case: str | None, label: str) -> str:
    if current_case and label in {"USER", "PLAY_BUTTON", "DRAG_SCRUB", "", "?"}:
        return current_case
    return label


def qpc_frequency() -> int | None:
    if sys.platform != "win32":
        return None
    value = ctypes.c_longlong()
    try:
        ok = ctypes.windll.kernel32.QueryPerformanceFrequency(
            ctypes.byref(value)
        )
    except Exception:
        return None
    if not ok or value.value <= 0:
        return None
    return int(value.value)


def latest_trace(trace_dir: Path = DEFAULT_TRACE_DIR) -> Path | None:
    traces = sorted(
        trace_dir.glob("replay_trace_*.jsonl"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    return traces[0] if traces else None


def load_events(path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError as exc:
                print(f"warning: skipped malformed line {line_no}: {exc}")
                continue
            if isinstance(obj, dict):
                events.append(obj)
    return events


def event_name(event: dict[str, Any]) -> str:
    return str(event.get("event") or event.get("name") or "")


def bool_field(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "ok"}
    return False


def int_field(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def float_field(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def qpc_field(event: dict[str, Any]) -> int | None:
    try:
        value = int(event.get("ts_qpc"))
    except (TypeError, ValueError):
        return None
    return value if value > 0 else None


def raw_mismatch_is_empty(value: Any) -> bool:
    if value is None:
        return True
    if isinstance(value, (list, tuple, dict, set)):
        return len(value) == 0
    return str(value).strip() in {"", "none", "None", "[]"}


def raw_mismatch_is_strict_failure(result: dict[str, Any]) -> bool:
    if raw_mismatch_is_empty(result.get("raw_mismatch_first_offsets")):
        return False
    return not bool_field(result.get("passed"))


def is_fallback_display_name(value: Any) -> bool:
    if not isinstance(value, str):
        return False
    return re.fullmatch(r"Steam \d+", value.strip()) is not None


def is_visual_oracle_diagnostic_failure(event: dict[str, Any]) -> bool:
    """Return true for diagnostic drift that is visible in playback.

    Cached hit-cue local/world transforms are still noisy derived values, but
    slot, cache, lane, active-cue, and frame fields have proven visual impact.
    Vital fields are HUD/gameplay state and must match the captured timeline.
    Replay 10929583780367153341 exposed this gap: seek/watchback passed while
    these USER-phase diagnostics diverged badly enough to be visible in-game.
    """
    if event_name(event) != "restore_integrity_oracle_diagnostic":
        return False
    if str(event.get("label") or "") != "USER":
        return False

    field = str(event.get("field") or "")
    if field.startswith("vital-"):
        return True
    if bool_field(event.get("natural_resume_playback")):
        # During released playback SC6's native replay/event pipeline owns
        # animation, hit-cue, and HUD advancement.  Cache hashes and derived
        # transform fields can drift from the generated oracle without meaning
        # the replay is playing the wrong move.  Keep vital state strict above,
        # and keep clearly semantic hit-cue fields strict below.
        noisy_hitcue_terms = (
            "cached-local",
            "cached-world",
            "cache-hash",
            "slot-hash",
            "lane-header-hash",
        )
        if field.startswith("hitcue"):
            return not any(term in field for term in noisy_hitcue_terms)
        if field.startswith("hit-cue-"):
            return False
        return False
    if field.startswith("hit-cue-"):
        return True
    if not field.startswith("hitcue"):
        return False
    if "cached-local" in field or "cached-world" in field:
        return False
    return True


def hitcue_slot_from_field(field: str) -> int | None:
    match = HITCUE_FIELD_RE.match(field)
    if not match:
        return None
    return int_field(match.group(1), -1)


def is_repaired_pre_overlay_hitcue_diagnostic(
    events: list[dict[str, Any]],
    index: int,
) -> bool:
    """Return true when a strict hitcue diagnostic is repaired before release.

    The USER restore path may emit restore_integrity_oracle_diagnostic before
    applying the semantic overlay.  Those diagnostics are still useful, but they
    are not strict failures when the later overlay writes the same hitcue slot
    or scalar field and readback proves it matches the oracle before the final
    overlay restore event.
    """
    event = events[index]
    if not bool_field(event.get("natural_resume_playback")):
        return False

    field = str(event.get("field") or "")
    slot = hitcue_slot_from_field(field)
    if slot is None:
        return False

    label = str(event.get("label") or "")
    player = int_field(event.get("player"), -1)
    if not label or player < 0:
        return False

    saw_slot_repair = False
    saw_field_repair = False
    for later in events[index + 1:]:
        if str(later.get("label") or "") != label:
            continue

        name = event_name(later)
        later_player = int_field(later.get("player"), -1)
        if (
            name == "oracle_semantic_overlay_hitcue_slot_write"
            and later_player == player
            and int_field(later.get("slot"), -1) == slot
            and bool_field(later.get("live_matches_oracle"))
        ):
            saw_slot_repair = True
        elif (
            name == "oracle_semantic_overlay_write"
            and later_player == player
            and str(later.get("field") or "") == field
            and bool_field(later.get("live_matches_oracle"))
        ):
            saw_field_repair = True
        elif name == "oracle_semantic_overlay_restore":
            return bool_field(later.get("ok")) and (
                saw_slot_repair or saw_field_repair
            )

    return False


def classify_failure(result: dict[str, Any]) -> str:
    failure = str(result.get("failure") or "")
    reason = str(result.get("pass_fail_reason") or result.get("reason") or "")
    oracle = str(result.get("oracle_failure") or "")
    combined = f"{failure} {reason} {oracle}"

    if "CapturedSnapshotCompareFailed" in combined:
        return "raw-only drift"
    if "BattleManagerStatusNotActive" in combined or "target_not_playable" in combined:
        return "BM inactive"
    if "SemanticMismatch" in combined or "Oracle" in combined or oracle not in ("", "None"):
        return "semantic/oracle failure"
    if "timeout" in combined or "NotLanded" in combined or "no result" in combined:
        return "timeout/no result"
    restore_terms = (
        "Restore", "Sc6", "CallFaulted", "ResetDispatch", "CursorWrite",
        "ExecFinalizeAndPost", "CapturedGameplayStepFailed",
    )
    if any(term in combined for term in restore_terms):
        return "restore failure"
    return "other failure"


def summarize_generation(events: list[dict[str, Any]]) -> None:
    complete = [e for e in events if event_name(e) == "generate_complete"]
    seekability = [e for e in events if event_name(e) == "generate_seekability_summary"]
    if not complete and not seekability:
        print("generation: no generation events found")
        return
    if complete:
        e = complete[-1]
        print(
            "generation: "
            f"frames={e.get('frames', '?')} rounds={e.get('rounds', '?')} "
            f"integrity_ok={e.get('integrity_ok', '?')} "
            f"oracle_ok={e.get('oracle_ok', '?')} "
            f"last_seq={e.get('last_seq', '?')}"
        )
    if seekability:
        e = seekability[-1]
        print(
            "seekability: "
            f"seekable={e.get('seekable', '?')} "
            f"reason={e.get('reason', '?')}"
        )


def collect_native_step_boundary_issues(
    events: list[dict[str, Any]],
) -> tuple[int, int, bool, list[str]]:
    observed_names = {
        "captured_seek_validation_step_observed",
        "sc6_native_step_observed",
    }
    observations = [e for e in events if event_name(e) in observed_names]
    if not observations:
        return 0, 0, False, []

    has_drain_marker = any(
        e.get("native_step_drain_event") is True
        or e.get("build") == "replay-accuracy-v13a"
        for e in events
    )
    latest_drained_total: int | None = None
    drain_events = 0
    gate_observations = 0
    issues: list[str] = []

    for index, e in enumerate(events, 1):
        name = event_name(e)
        if name == "sc6_native_step_drained":
            drain_events += 1
            try:
                latest_drained_total = int(e.get("drained_total"))
            except (TypeError, ValueError):
                issues.append(f"line#{index}: drain event missing drained_total")
        elif name in observed_names:
            if bool_field(e.get("direct_step")):
                if not bool_field(e.get("step_ok")):
                    issues.append(f"line#{index}: {name} direct step failed")
                try:
                    direct_master_after = int(e.get("master_after"))
                    direct_compare_master = int(e.get("compare_master"))
                except (TypeError, ValueError):
                    issues.append(
                        f"line#{index}: {name} direct step missing master"
                    )
                    continue
                if direct_master_after != direct_compare_master:
                    issues.append(
                        f"line#{index}: {name} direct master_after="
                        f"{direct_master_after} compare_master="
                        f"{direct_compare_master}"
                    )
                continue

            gate_observations += 1
            try:
                observed_drained_total = int(e.get("drained_total_after"))
            except (TypeError, ValueError):
                try:
                    observed_drained_total = int(e.get("drained_total"))
                except (TypeError, ValueError):
                    issues.append(
                        f"line#{index}: {name} missing drained total"
                    )
                    continue
            if latest_drained_total is None:
                issues.append(f"line#{index}: {name} before any drain event")
            elif latest_drained_total < observed_drained_total:
                issues.append(
                    f"line#{index}: {name} drained_total={observed_drained_total} "
                    f"but latest drain={latest_drained_total}"
                )

    if gate_observations == 0:
        has_drain_marker = False

    return len(observations), drain_events, has_drain_marker, issues


def summarize_native_step_boundaries(events: list[dict[str, Any]], strict: bool) -> int:
    observation_count, drain_events, has_drain_marker, issues = (
        collect_native_step_boundary_issues(events)
    )
    if observation_count == 0:
        print("native-step boundary: no native-step observations")
        return 0

    print(
        "native-step boundary: "
        f"observations={observation_count} drains={drain_events} "
        f"issues={len(issues)}"
    )
    for issue in issues[:5]:
        print(f"  boundary issue: {issue}")
    if len(issues) > 5:
        print(f"  ... {len(issues) - 5} more")

    if strict and issues:
        return 1
    if issues and has_drain_marker:
        return 1
    if observation_count and has_drain_marker and drain_events == 0:
        return 1
    return 0


def estimate_tick_rate(
    samples: list[dict[str, Any]],
    field: str,
    window_ticks: int,
    frequency: int,
) -> dict[str, Any] | None:
    valid: list[tuple[int, int]] = []
    for sample in samples:
        qpc = qpc_field(sample)
        value = int_field(sample.get(field), -1)
        if qpc is not None and value >= 0:
            valid.append((qpc, value))

    if len(valid) < 2:
        return None

    first_qpc, first_value = valid[0]
    prev_value = first_value
    end_qpc = first_qpc
    end_value = first_value
    tick_delta = 0
    used_samples = 1
    target_ticks = max(1, window_ticks)

    for qpc, value in valid[1:]:
        delta = value - prev_value
        prev_value = value
        if delta < 0:
            # Round transitions can reset round-local counters. Keep the
            # window alive and use the next positive segment if needed.
            continue
        used_samples += 1
        if delta == 0:
            continue
        tick_delta += delta
        end_qpc = qpc
        end_value = value
        if tick_delta >= target_ticks:
            break

    if tick_delta <= 0 or end_qpc <= first_qpc:
        return None

    elapsed = (end_qpc - first_qpc) / frequency
    if elapsed <= 0.0:
        return None

    return {
        "rate": tick_delta / elapsed,
        "ticks": tick_delta,
        "elapsed_seconds": elapsed,
        "samples": used_samples,
        "first_value": first_value,
        "last_value": end_value,
    }


def percentile_value(values: list[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    clamped = min(1.0, max(0.0, percentile))
    index = int(round((len(ordered) - 1) * clamped))
    return ordered[index]


def collect_tick_gap_stats(
    samples: list[dict[str, Any]],
    field: str,
    frequency: int,
) -> dict[str, Any] | None:
    valid: list[tuple[int, int, dict[str, Any]]] = []
    for sample in samples:
        qpc = qpc_field(sample)
        value = int_field(sample.get(field), -1)
        if qpc is not None and value >= 0:
            valid.append((qpc, value, sample))

    if len(valid) < 2:
        return None

    tick_gaps: list[float] = []
    sample_gaps: list[float] = []
    max_tick_gap = 0.0
    max_sample_gap = 0.0
    max_tick_gap_before: int | None = None
    max_tick_gap_after: int | None = None
    max_sample_gap_before: int | None = None
    max_sample_gap_after: int | None = None
    max_tick_gap_delta = 0
    max_sample_gap_delta = 0
    max_tick_gap_qpc_delta = 0.0
    max_sample_gap_qpc_delta = 0.0

    prev_qpc, prev_value, _prev_sample = valid[0]
    for qpc, value, _sample in valid[1:]:
        qpc_delta = qpc - prev_qpc
        value_delta = value - prev_value
        if qpc_delta <= 0:
            prev_qpc, prev_value = qpc, value
            continue
        if value_delta < 0:
            # Round-local counters can reset across round boundaries. Treat the
            # next positive segment as a fresh timing run.
            prev_qpc, prev_value = qpc, value
            continue
        if value_delta == 0:
            prev_qpc, prev_value = qpc, value
            continue

        elapsed = qpc_delta / frequency
        per_tick = elapsed / value_delta
        tick_gaps.append(per_tick)
        sample_gaps.append(elapsed)

        if per_tick > max_tick_gap:
            max_tick_gap = per_tick
            max_tick_gap_before = prev_value
            max_tick_gap_after = value
            max_tick_gap_delta = value_delta
            max_tick_gap_qpc_delta = elapsed
        if elapsed > max_sample_gap:
            max_sample_gap = elapsed
            max_sample_gap_before = prev_value
            max_sample_gap_after = value
            max_sample_gap_delta = value_delta
            max_sample_gap_qpc_delta = elapsed

        prev_qpc, prev_value = qpc, value

    if not tick_gaps:
        return None

    return {
        "samples": len(valid),
        "gap_count": len(tick_gaps),
        "max_tick_gap_seconds": max_tick_gap,
        "p95_tick_gap_seconds": percentile_value(tick_gaps, 0.95),
        "avg_tick_gap_seconds": sum(tick_gaps) / len(tick_gaps),
        "max_sample_gap_seconds": max_sample_gap,
        "p95_sample_gap_seconds": percentile_value(sample_gaps, 0.95),
        "max_tick_gap_before": max_tick_gap_before,
        "max_tick_gap_after": max_tick_gap_after,
        "max_tick_gap_delta": max_tick_gap_delta,
        "max_tick_gap_qpc_delta_seconds": max_tick_gap_qpc_delta,
        "max_sample_gap_before": max_sample_gap_before,
        "max_sample_gap_after": max_sample_gap_after,
        "max_sample_gap_delta": max_sample_gap_delta,
        "max_sample_gap_qpc_delta_seconds": max_sample_gap_qpc_delta,
    }


def collect_resume_tick_stats(
    events: list[dict[str, Any]],
    frequency: int | None,
    window_ticks: int = DEFAULT_RESUME_TICK_WINDOW,
) -> dict[str, dict[str, Any]]:
    if frequency is None or frequency <= 0:
        return {}

    starts: dict[str, dict[str, Any]] = {}
    stats: dict[str, dict[str, Any]] = {}

    for event in events:
        name = event_name(event)
        label = str(event.get("label") or "")
        qpc = qpc_field(event)

        if name == "replay_seek_test_resume_start" and label and qpc:
            starts.setdefault(label, {
                "label": label,
                "start_qpc": qpc,
                "play_qpc": qpc,
                "target_seq": event.get("target_seq"),
                "target_round": event.get("target_round"),
                "target_master": event.get("target_master"),
                "progress": [],
            })
            continue

        if (
            name == "replay_seek_test_resume_command_serviced"
            and label
            and qpc
        ):
            starts.setdefault(label, {
                "label": label,
                "start_qpc": qpc,
                "target_seq": event.get("target_seq"),
                "progress": [],
            })
            starts[label]["play_qpc"] = qpc
            continue

        if name == "native_playback_progress" and qpc:
            for start in starts.values():
                play_qpc = int_field(start.get("play_qpc"))
                if play_qpc and qpc >= play_qpc:
                    start.setdefault("progress", []).append(event)
            continue

        if name != "replay_seek_test_case_result" or not label:
            continue
        if label not in starts:
            continue

        start = starts[label]
        end_qpc = qpc
        play_qpc = int_field(start.get("play_qpc"))
        observed = int_field(event.get("resume_frames_observed"))
        requested = int_field(event.get("resume_frames_requested"))
        native_samples = [
            p for p in start.get("progress", [])
            if (pq := qpc_field(p)) is not None and play_qpc <= pq <= end_qpc
            and int_field(p.get("replay_player_playing"), -1) == 1
        ]
        bm_rate = estimate_tick_rate(
            native_samples,
            "bm_frame_advance",
            window_ticks,
            frequency,
        )
        master_rate = estimate_tick_rate(
            native_samples,
            "master",
            window_ticks,
            frequency,
        )
        chosen_source = "bm_frame_advance" if bm_rate else "master"
        chosen_rate = bm_rate or master_rate
        bm_gap = collect_tick_gap_stats(
            native_samples,
            "bm_frame_advance",
            frequency,
        )
        master_gap = collect_tick_gap_stats(
            native_samples,
            "master",
            frequency,
        )
        chosen_gap = bm_gap if bm_rate else master_gap
        first_tick_latency: float | None = None
        if native_samples:
            first_qpc = qpc_field(native_samples[0])
            if first_qpc is not None and play_qpc and first_qpc >= play_qpc:
                first_tick_latency = (first_qpc - play_qpc) / frequency

        stats[label] = {
            "label": label,
            "requested": requested,
            "observed": observed,
            "tick_source": chosen_source if chosen_rate else "none",
            "tick_rate": chosen_rate.get("rate") if chosen_rate else None,
            "tick_elapsed_seconds": (
                chosen_rate.get("elapsed_seconds") if chosen_rate else None
            ),
            "tick_delta": chosen_rate.get("ticks") if chosen_rate else 0,
            "tick_samples": chosen_rate.get("samples") if chosen_rate else 0,
            "native_samples": len(native_samples),
            "first_tick_latency_seconds": first_tick_latency,
            "bm_tick_rate": bm_rate.get("rate") if bm_rate else None,
            "master_tick_rate": master_rate.get("rate") if master_rate else None,
            "max_tick_gap_seconds": (
                chosen_gap.get("max_tick_gap_seconds") if chosen_gap else None
            ),
            "p95_tick_gap_seconds": (
                chosen_gap.get("p95_tick_gap_seconds") if chosen_gap else None
            ),
            "avg_tick_gap_seconds": (
                chosen_gap.get("avg_tick_gap_seconds") if chosen_gap else None
            ),
            "max_sample_gap_seconds": (
                chosen_gap.get("max_sample_gap_seconds") if chosen_gap else None
            ),
            "p95_sample_gap_seconds": (
                chosen_gap.get("p95_sample_gap_seconds") if chosen_gap else None
            ),
            "max_tick_gap_before": (
                chosen_gap.get("max_tick_gap_before") if chosen_gap else None
            ),
            "max_tick_gap_after": (
                chosen_gap.get("max_tick_gap_after") if chosen_gap else None
            ),
            "max_tick_gap_delta": (
                chosen_gap.get("max_tick_gap_delta") if chosen_gap else None
            ),
            "max_tick_gap_qpc_delta_seconds": (
                chosen_gap.get("max_tick_gap_qpc_delta_seconds")
                if chosen_gap else None
            ),
            "bm_max_tick_gap_seconds": (
                bm_gap.get("max_tick_gap_seconds") if bm_gap else None
            ),
            "master_max_tick_gap_seconds": (
                master_gap.get("max_tick_gap_seconds") if master_gap else None
            ),
        }

    return stats


def collect_resume_tick_failures(
    events: list[dict[str, Any]],
    min_tick_rate: float,
    window_ticks: int = DEFAULT_RESUME_TICK_WINDOW,
    max_tick_gap_seconds: float = DEFAULT_MAX_RESUME_TICK_GAP_SECONDS,
    max_first_tick_seconds: float = DEFAULT_MAX_FIRST_RESUME_TICK_SECONDS,
) -> tuple[dict[str, dict[str, Any]], list[str]]:
    frequency = qpc_frequency()
    stats = collect_resume_tick_stats(events, frequency, window_ticks)
    failures: list[str] = []
    if frequency is None:
        failures.append("resume tick-rate check unavailable: no QPC frequency")
        return stats, failures

    for label, stat in stats.items():
        requested = int_field(stat.get("requested"))
        if requested <= 0:
            continue
        rate_value = stat.get("tick_rate")
        if rate_value is None:
            failures.append(
                f"case {label} has no native replay tick-rate samples"
            )
            continue
        rate = float_field(rate_value)
        if rate < min_tick_rate:
            failures.append(
                f"case {label} native replay tick rate too slow: "
                f"{rate:.1f} t/s < {min_tick_rate:.1f} t/s "
                f"(source={stat.get('tick_source', '?')} "
                f"window_ticks={window_ticks})"
            )
        first_tick = stat.get("first_tick_latency_seconds")
        if (
            max_first_tick_seconds > 0.0
            and first_tick is not None
            and float_field(first_tick) > max_first_tick_seconds
        ):
            failures.append(
                f"case {label} first native replay tick too late: "
                f"{float_field(first_tick):.3f}s > "
                f"{max_first_tick_seconds:.3f}s"
            )
        max_gap = stat.get("max_tick_gap_seconds")
        if (
            max_tick_gap_seconds > 0.0
            and max_gap is not None
            and float_field(max_gap) > max_tick_gap_seconds
        ):
            before = stat.get("max_tick_gap_before", "?")
            after = stat.get("max_tick_gap_after", "?")
            failures.append(
                f"case {label} native replay tick gap spike: "
                f"{float_field(max_gap):.3f}s > "
                f"{max_tick_gap_seconds:.3f}s "
                f"(source={stat.get('tick_source', '?')} "
                f"values={before}->{after})"
            )
    return stats, failures


def collect_seek_validation_stats(
    events: list[dict[str, Any]],
    frequency: int | None,
) -> list[dict[str, Any]]:
    if frequency is None or frequency <= 0:
        return []

    pending: list[dict[str, Any]] = []
    stats: list[dict[str, Any]] = []
    for event in events:
        name = event_name(event)
        label = str(event.get("label") or "")
        qpc = qpc_field(event)

        if name == "captured_seek_queued" and qpc is not None:
            pending.append({
                "label": label,
                "target_seq": event.get("target_seq"),
                "target_master": event.get("target_master"),
                "origin_master": event.get("origin_master"),
                "validation_mode": str(event.get("validation_mode") or ""),
                "native_step_warmup_to_target": bool_field(
                    event.get("native_step_warmup_to_target")
                ),
                "queued_qpc": qpc,
                "native_steps": 0,
                "direct_warmup_ok": False,
                "direct_warmup_steps": 0,
            })
            continue

        if name == "sc6_native_step_observed" and pending:
            for item in reversed(pending):
                if item.get("label") == label:
                    step_count = int_field(event.get("drained_credits"))
                    if step_count <= 0:
                        step_count = int_field(event.get("credits"))
                    if step_count <= 0:
                        step_count = 1
                    item["native_steps"] = int_field(
                        item.get("native_steps")
                    ) + step_count
                    break
            continue

        if name == "prev_to_target_direct_warmup" and pending:
            for item in reversed(pending):
                if item.get("label") == label:
                    item["direct_warmup_ok"] = bool_field(event.get("ok"))
                    item["direct_warmup_steps"] = int_field(
                        event.get("steps")
                    )
                    break
            continue

        if name != "captured_seek_landed" or qpc is None:
            continue

        match_index = None
        for index in range(len(pending) - 1, -1, -1):
            if pending[index].get("label") == label:
                match_index = index
                break
        if match_index is None:
            continue

        item = pending.pop(match_index)
        queued_qpc = int_field(item.get("queued_qpc"))
        if queued_qpc > 0 and qpc >= queued_qpc:
            item["elapsed_seconds"] = (qpc - queued_qpc) / frequency
        else:
            item["elapsed_seconds"] = None
        item["frames_advanced"] = int_field(event.get("frames_advanced"))
        item["landed"] = True
        stats.append(item)

    return stats


def collect_seek_validation_failures(
    events: list[dict[str, Any]],
    max_seconds: float = DEFAULT_MAX_SEEK_VALIDATION_SECONDS,
) -> tuple[list[dict[str, Any]], list[str]]:
    frequency = qpc_frequency()
    stats = collect_seek_validation_stats(events, frequency)
    failures: list[str] = []
    if frequency is None:
        failures.append("seek validation duration check unavailable: no QPC frequency")
        return stats, failures

    for stat in stats:
        elapsed = stat.get("elapsed_seconds")
        if elapsed is None:
            continue
        mode = str(stat.get("validation_mode") or "")
        native_steps = int_field(stat.get("native_steps"))
        direct_ok = bool_field(stat.get("direct_warmup_ok"))
        warmup = bool_field(stat.get("native_step_warmup_to_target"))
        if (
            mode in {"prev_to_target", "previous_to_target"}
            and warmup
            and not direct_ok
            and native_steps > 1
            and float_field(elapsed) > max_seconds
        ):
            failures.append(
                "visible seek validation warmup too slow: "
                f"label={stat.get('label', '?')} "
                f"target_seq={stat.get('target_seq', '?')} "
                f"steps={native_steps} "
                f"elapsed={float_field(elapsed):.2f}s > {max_seconds:.2f}s"
            )
    return stats, failures


def collect_seek_lifecycle_stats(
    events: list[dict[str, Any]],
    frequency: int | None,
) -> dict[str, dict[str, Any]]:
    if frequency is None or frequency <= 0:
        return {}

    stats: dict[str, dict[str, Any]] = {}
    current_case: str | None = None

    def elapsed(start: Any, end: Any) -> float | None:
        start_qpc = int_field(start)
        end_qpc = int_field(end)
        if start_qpc <= 0 or end_qpc < start_qpc:
            return None
        return (end_qpc - start_qpc) / frequency

    for event in events:
        name = event_name(event)
        label = str(event.get("label") or "")
        qpc = qpc_field(event)

        if name == "replay_seek_test_case_start" and label and qpc:
            current_case = label
            stat = stats.setdefault(label, {"label": label})
            stat["case_start_qpc"] = qpc
            stat["target_seq"] = event.get("target_seq")
            stat["target_round"] = event.get("target_round")
            stat["target_master"] = event.get("target_master")
            stat["resume_frames_requested"] = int_field(
                event.get("resume_frames_requested")
            )
            continue

        if name == "captured_seek_queued" and qpc:
            seek_label = active_case_seek_label(current_case, label)
            if not seek_label:
                continue
            stat = stats.setdefault(seek_label, {"label": seek_label})
            stat["target_seq"] = event.get(
                "target_seq", stat.get("target_seq")
            )
            stat["target_round"] = event.get(
                "target_round", stat.get("target_round")
            )
            stat["target_master"] = event.get(
                "target_master", stat.get("target_master")
            )
            stat["validation_mode"] = str(event.get("validation_mode") or "")
            stat["reset_context"] = bool_field(event.get("reset_context"))
            stat["cross_round_reset"] = bool_field(
                event.get("cross_round_reset")
            )
            if stat.get("first_queued_qpc") is None:
                stat["first_queued_qpc"] = qpc
                stat["case_start_to_queued_seconds"] = elapsed(
                    stat.get("case_start_qpc"), qpc
                )
            stat["queued_qpc"] = qpc
            continue

        if name == "captured_seek_landed" and qpc:
            seek_label = active_case_seek_label(current_case, label)
            if not seek_label:
                continue
            stat = stats.setdefault(seek_label, {"label": seek_label})
            stat["landed_qpc"] = qpc
            stat["queue_to_landed_seconds"] = elapsed(
                stat.get("queued_qpc"), qpc
            )
            stat["case_start_to_landed_seconds"] = elapsed(
                stat.get("case_start_qpc"), qpc
            )
            stat["frames_advanced"] = int_field(event.get("frames_advanced"))
            continue

        if name == "replay_seek_test_resume_start" and label and qpc:
            stat = stats.setdefault(label, {"label": label})
            stat["resume_start_qpc"] = qpc
            stat["landed_to_resume_seconds"] = elapsed(
                stat.get("landed_qpc"), qpc
            )
            stat["queue_to_resume_seconds"] = elapsed(
                stat.get("queued_qpc"), qpc
            )
            stat["case_start_to_resume_seconds"] = elapsed(
                stat.get("case_start_qpc"), qpc
            )
            stat["resume_start_seq"] = event.get("resume_start_seq")
            stat["resume_start_master"] = event.get("resume_start_master")
            continue

        if name == "replay_seek_test_case_result" and label:
            stat = stats.setdefault(label, {"label": label})
            stat["result_seen"] = True
            if current_case == label:
                current_case = None

    return {
        label: stat
        for label, stat in stats.items()
        if stat.get("case_start_qpc") is not None
        or stat.get("resume_frames_requested") is not None
    }


def collect_seek_lifecycle_failures(
    events: list[dict[str, Any]],
    max_queue_seconds: float = DEFAULT_MAX_SEEK_QUEUE_SECONDS,
    max_land_seconds: float = DEFAULT_MAX_SEEK_LAND_SECONDS,
    max_resume_handoff_seconds: float = DEFAULT_MAX_SEEK_RESUME_HANDOFF_SECONDS,
    max_total_resume_seconds: float = DEFAULT_MAX_SEEK_TOTAL_RESUME_SECONDS,
) -> tuple[dict[str, dict[str, Any]], list[str]]:
    frequency = qpc_frequency()
    stats = collect_seek_lifecycle_stats(events, frequency)
    failures: list[str] = []
    if frequency is None:
        failures.append("seek lifecycle check unavailable: no QPC frequency")
        return stats, failures

    for label, stat in stats.items():
        requested = int_field(stat.get("resume_frames_requested"))
        if requested <= 0:
            continue
        queued = stat.get("case_start_to_queued_seconds")
        if (
            max_queue_seconds > 0.0
            and queued is not None
            and float_field(queued) > max_queue_seconds
        ):
            failures.append(
                f"case {label} seek queue handoff too slow: "
                f"{float_field(queued):.3f}s > "
                f"{max_queue_seconds:.3f}s "
                f"(target_seq={stat.get('target_seq', '?')})"
            )
        land = stat.get("queue_to_landed_seconds")
        if (
            max_land_seconds > 0.0
            and land is not None
            and float_field(land) > max_land_seconds
        ):
            failures.append(
                f"case {label} seek landing too slow: "
                f"{float_field(land):.3f}s > {max_land_seconds:.3f}s "
                f"(target_seq={stat.get('target_seq', '?')})"
            )
        handoff = stat.get("landed_to_resume_seconds")
        if (
            max_resume_handoff_seconds > 0.0
            and handoff is not None
            and float_field(handoff) > max_resume_handoff_seconds
        ):
            failures.append(
                f"case {label} seek resume handoff too slow: "
                f"{float_field(handoff):.3f}s > "
                f"{max_resume_handoff_seconds:.3f}s "
                f"(target_seq={stat.get('target_seq', '?')})"
            )
        total = stat.get("case_start_to_resume_seconds")
        if (
            max_total_resume_seconds > 0.0
            and total is not None
            and float_field(total) > max_total_resume_seconds
        ):
            failures.append(
                f"case {label} automated seek lifecycle too slow: "
                f"{float_field(total):.3f}s > "
                f"{max_total_resume_seconds:.3f}s "
                f"(target_seq={stat.get('target_seq', '?')})"
            )

    return stats, failures


def summarize_test_events(
    events: list[dict[str, Any]],
    min_resume_tick_rate: float = DEFAULT_MIN_RESUME_TICK_RATE,
    resume_tick_window: int = DEFAULT_RESUME_TICK_WINDOW,
    max_seek_validation_seconds: float = DEFAULT_MAX_SEEK_VALIDATION_SECONDS,
    max_resume_tick_gap_seconds: float = DEFAULT_MAX_RESUME_TICK_GAP_SECONDS,
    max_first_resume_tick_seconds: float = DEFAULT_MAX_FIRST_RESUME_TICK_SECONDS,
    max_seek_queue_seconds: float = DEFAULT_MAX_SEEK_QUEUE_SECONDS,
    max_seek_land_seconds: float = DEFAULT_MAX_SEEK_LAND_SECONDS,
    max_seek_resume_handoff_seconds: float = (
        DEFAULT_MAX_SEEK_RESUME_HANDOFF_SECONDS
    ),
    max_seek_total_resume_seconds: float = DEFAULT_MAX_SEEK_TOTAL_RESUME_SECONDS,
) -> int | None:
    results = [e for e in events if event_name(e) == "replay_seek_test_case_result"]
    summaries = [e for e in events if event_name(e) == "replay_seek_test_summary"]
    starts = [e for e in events if event_name(e) == "replay_seek_test_start"]
    if not results and not summaries and not starts:
        return None

    tick_stats, tick_failures = collect_resume_tick_failures(
        events,
        min_resume_tick_rate,
        resume_tick_window,
        max_resume_tick_gap_seconds,
        max_first_resume_tick_seconds,
    )
    validation_stats, validation_failures = collect_seek_validation_failures(
        events,
        max_seek_validation_seconds,
    )
    lifecycle_stats, lifecycle_failures = collect_seek_lifecycle_failures(
        events,
        max_seek_queue_seconds,
        max_seek_land_seconds,
        max_seek_resume_handoff_seconds,
        max_seek_total_resume_seconds,
    )

    if starts:
        start = starts[-1]
        print(
            "test run: "
            f"run_id={start.get('run_id', '?')} "
            f"build={start.get('build', '?')} "
            f"cases={start.get('case_count', '?')} "
            f"mode={start.get('generate_mode', '?')}"
        )

    failures: defaultdict[str, list[dict[str, Any]]] = defaultdict(list)
    raw_diag = 0
    passed = 0
    failed = 0
    watch_cases = 0
    watch_passed = 0
    watch_requested_frames = 0
    watch_observed_frames = 0
    watch_state_compares = 0
    watch_state_mismatches = 0
    watch_state_unchecked = 0
    ui_step_cases = 0
    ui_step_passed = 0
    for r in results:
        label = str(r.get("label", "?"))
        ok = bool(r.get("passed"))
        action = str(r.get("action") or "")
        is_ui_step = action in UI_STEP_ACTIONS
        if is_ui_step:
            ui_step_cases += 1
            if ok:
                ui_step_passed += 1
        try:
            resume_requested = int(r.get("resume_frames_requested") or 0)
        except (TypeError, ValueError):
            resume_requested = 0
        try:
            resume_observed = int(r.get("resume_frames_observed") or 0)
        except (TypeError, ValueError):
            resume_observed = 0
        terminal = bool_field(r.get("resume_terminal_reached"))
        terminal_reason = str(r.get("resume_terminal_reason") or "")
        state_compares = int_field(r.get("resume_state_compares"))
        state_mismatches = int_field(r.get("resume_state_mismatches"))
        if resume_requested > 0:
            watch_cases += 1
            watch_requested_frames += resume_requested
            watch_observed_frames += resume_observed
            watch_state_compares += state_compares
            watch_state_mismatches += state_mismatches
            if resume_observed > 0 and state_compares <= 0:
                watch_state_unchecked += 1
            if ok:
                watch_passed += 1
        raw_value = r.get("raw_mismatch_first_offsets")
        raw = str(raw_value or "none")
        if not raw_mismatch_is_empty(raw_value):
            raw_diag += 1
        if ok:
            passed += 1
            status = "PASS"
        else:
            failed += 1
            status = "FAIL"
            failures[classify_failure(r)].append(r)
        print(
            f"case {label}: {status} "
            f"target_seq={r.get('target_seq', '?')} "
            f"round={r.get('target_round', '?')} "
            f"master={r.get('target_master', '?')} "
            f"landed={r.get('landed', '?')} "
            f"resume={resume_observed}/{resume_requested} "
            f"state={state_compares}/{state_mismatches} "
            f"terminal={terminal_reason if terminal else 'no'} "
            f"reason={r.get('pass_fail_reason', '?')} "
            f"failure={r.get('failure', '?')}"
        )
        if is_ui_step:
            step_delta, step_count = ui_step_expected_delta_count(r)
            print(
                "  ui step: "
                f"source={r.get('ui_step_source_seq', '?')}"
                f"/r{r.get('ui_step_source_round', '?')}"
                f"/m{r.get('ui_step_source_master', '?')} -> "
                f"target={r.get('ui_step_target_seq', '?')}"
                f"/r{r.get('ui_step_target_round', '?')}"
                f"/m{r.get('ui_step_target_master', '?')} "
                f"delta={step_delta} count={step_count} "
                f"requested={r.get('ui_step_requested', '?')} "
                f"landed={r.get('ui_step_landed', '?')}"
            )
        if not raw_mismatch_is_empty(raw_value):
            print(f"  raw diagnostic: {raw}")
        if state_mismatches > 0:
            print(
                "  state mismatch: "
                f"seq={r.get('resume_state_first_mismatch_seq', '?')} "
                f"round={r.get('resume_state_first_mismatch_round', '?')} "
                f"master={r.get('resume_state_first_mismatch_master', '?')} "
                f"player={r.get('resume_state_first_mismatch_player', '?')} "
                f"field={r.get('resume_state_first_mismatch_field', '?')} "
                f"reason={r.get('resume_state_first_mismatch_reason', '?')} "
                f"expected={r.get('resume_state_first_expected_u64', '?')} "
                f"live={r.get('resume_state_first_live_u64', '?')}"
            )
        tick_stat = tick_stats.get(label)
        if resume_requested > 0 and tick_stat:
            bm_rate = tick_stat.get("bm_tick_rate")
            master_rate = tick_stat.get("master_tick_rate")
            bm_part = ""
            if bm_rate is not None:
                bm_part = f" bm={float_field(bm_rate):.1f}t/s"
            master_part = ""
            if master_rate is not None:
                master_part = f" master={float_field(master_rate):.1f}t/s"
            first_tick = tick_stat.get("first_tick_latency_seconds")
            latency_part = ""
            if first_tick is not None:
                latency_part = f" first_tick={float_field(first_tick):.3f}s"
            max_gap = tick_stat.get("max_tick_gap_seconds")
            gap_part = ""
            if max_gap is not None:
                gap_part = f" max_gap={float_field(max_gap):.3f}s"
            p95_gap = tick_stat.get("p95_tick_gap_seconds")
            p95_gap_part = ""
            if p95_gap is not None:
                p95_gap_part = f" p95_gap={float_field(p95_gap):.3f}s"
            sample_gap = tick_stat.get("max_sample_gap_seconds")
            sample_gap_part = ""
            if sample_gap is not None:
                sample_gap_part = (
                    f" sample_gap={float_field(sample_gap):.3f}s"
                )
            gap_before = tick_stat.get("max_tick_gap_before")
            gap_after = tick_stat.get("max_tick_gap_after")
            gap_at_part = ""
            if gap_before is not None and gap_after is not None:
                gap_at_part = f" gap_at={gap_before}->{gap_after}"
            print(
                "  native ticks: "
                f"source={tick_stat.get('tick_source', '?')} "
                f"rate={float_field(tick_stat.get('tick_rate')):.1f}t/s "
                f"ticks={int_field(tick_stat.get('tick_delta'))} "
                f"elapsed={float_field(tick_stat.get('tick_elapsed_seconds')):.2f}s"
                f"{bm_part}{master_part}{latency_part}"
                f"{gap_part}{p95_gap_part}{sample_gap_part}{gap_at_part} "
                f"samples={int_field(tick_stat.get('native_samples'))}"
            )
        lifecycle_stat = lifecycle_stats.get(label)
        if resume_requested > 0 and lifecycle_stat:
            queued = lifecycle_stat.get("case_start_to_queued_seconds")
            land = lifecycle_stat.get("queue_to_landed_seconds")
            handoff = lifecycle_stat.get("landed_to_resume_seconds")
            total = lifecycle_stat.get("case_start_to_resume_seconds")
            queue_part = ""
            if queued is not None:
                queue_part = (
                    f" start_to_queue={float_field(queued):.3f}s"
                )
            land_part = ""
            if land is not None:
                land_part = f" queue_to_landed={float_field(land):.3f}s"
            handoff_part = ""
            if handoff is not None:
                handoff_part = (
                    f" landed_to_resume={float_field(handoff):.3f}s"
                )
            total_part = ""
            if total is not None:
                total_part = f" total_to_resume={float_field(total):.3f}s"
            print(
                "  seek lifecycle:"
                f"{queue_part}{land_part}{handoff_part}{total_part} "
                f"reset={bool_field(lifecycle_stat.get('reset_context'))} "
                f"cross_round="
                f"{bool_field(lifecycle_stat.get('cross_round_reset'))}"
            )

    print(f"cases: passed={passed} failed={failed} raw_diagnostics={raw_diag}")
    if watch_cases:
        print(
            "watchback: "
            f"cases={watch_cases} passed={watch_passed} "
            f"observed_frames={watch_observed_frames}/"
            f"{watch_requested_frames} "
            f"state_compares={watch_state_compares} "
            f"state_mismatches={watch_state_mismatches} "
            f"state_unchecked={watch_state_unchecked}"
        )
    if ui_step_cases:
        print(
            "ui-step: "
            f"cases={ui_step_cases} passed={ui_step_passed} "
            f"failed={ui_step_cases - ui_step_passed}"
        )
        rates = [
            float_field(stat.get("tick_rate"))
            for stat in tick_stats.values()
            if int_field(stat.get("requested")) > 0
            and stat.get("tick_rate") is not None
        ]
        gap_values = [
            float_field(stat.get("max_tick_gap_seconds"))
            for stat in tick_stats.values()
            if int_field(stat.get("requested")) > 0
            and stat.get("max_tick_gap_seconds") is not None
        ]
        first_tick_values = [
            float_field(stat.get("first_tick_latency_seconds"))
            for stat in tick_stats.values()
            if int_field(stat.get("requested")) > 0
            and stat.get("first_tick_latency_seconds") is not None
        ]
        spike_cases = sum(
            1 for gap in gap_values
            if max_resume_tick_gap_seconds > 0.0
            and gap > max_resume_tick_gap_seconds
        )
        late_cases = sum(
            1 for first_tick in first_tick_values
            if max_first_resume_tick_seconds > 0.0
            and first_tick > max_first_resume_tick_seconds
        )
        if rates:
            gap_part = ""
            if gap_values:
                gap_part = (
                    f" max_gap={max(gap_values):.3f}s "
                    f"gap_threshold={max_resume_tick_gap_seconds:.3f}s "
                    f"spike_cases={spike_cases}"
                )
            first_tick_part = ""
            if first_tick_values:
                first_tick_part = (
                    f" max_first_tick={max(first_tick_values):.3f}s "
                    f"first_tick_threshold="
                    f"{max_first_resume_tick_seconds:.3f}s "
                    f"late_cases={late_cases}"
                )
            print(
                "watchback ticks: "
                f"min={min(rates):.1f}t/s "
                f"avg={sum(rates) / len(rates):.1f}t/s "
                f"threshold={min_resume_tick_rate:.1f}t/s "
                f"window={resume_tick_window}ticks "
                f"failed_cases={len(tick_failures)}"
                f"{gap_part}{first_tick_part}"
            )
        elif tick_failures:
            print(
                "watchback ticks: "
                f"unavailable threshold={min_resume_tick_rate:.1f}t/s "
                f"window={resume_tick_window}ticks "
                f"failed_cases={len(tick_failures)}"
            )
        lifecycle_watch_stats = [
            stat for stat in lifecycle_stats.values()
            if int_field(stat.get("resume_frames_requested")) > 0
        ]
        if lifecycle_watch_stats:
            queue_values = [
                float_field(stat.get("case_start_to_queued_seconds"))
                for stat in lifecycle_watch_stats
                if stat.get("case_start_to_queued_seconds") is not None
            ]
            land_values = [
                float_field(stat.get("queue_to_landed_seconds"))
                for stat in lifecycle_watch_stats
                if stat.get("queue_to_landed_seconds") is not None
            ]
            handoff_values = [
                float_field(stat.get("landed_to_resume_seconds"))
                for stat in lifecycle_watch_stats
                if stat.get("landed_to_resume_seconds") is not None
            ]
            total_values = [
                float_field(stat.get("case_start_to_resume_seconds"))
                for stat in lifecycle_watch_stats
                if stat.get("case_start_to_resume_seconds") is not None
            ]
            queue_part = (
                f" max_queue={max(queue_values):.3f}s "
                f"queue_threshold={max_seek_queue_seconds:.3f}s"
                if queue_values else ""
            )
            land_part = (
                f" max_land={max(land_values):.3f}s "
                f"land_threshold={max_seek_land_seconds:.3f}s"
                if land_values else ""
            )
            handoff_part = (
                f" max_handoff={max(handoff_values):.3f}s "
                f"handoff_threshold="
                f"{max_seek_resume_handoff_seconds:.3f}s"
                if handoff_values else ""
            )
            total_part = (
                f" max_total={max(total_values):.3f}s "
                f"total_threshold={max_seek_total_resume_seconds:.3f}s"
                if total_values else ""
            )
            print(
                "seek lifecycle: "
                f"cases={len(lifecycle_watch_stats)}"
                f"{queue_part}{land_part}{handoff_part}{total_part} "
                f"failed_cases={len(lifecycle_failures)}"
            )
    if validation_stats:
        elapsed_values = [
            float_field(s.get("elapsed_seconds"))
            for s in validation_stats
            if s.get("elapsed_seconds") is not None
        ]
        visible_steps = sum(
            int_field(s.get("native_steps")) for s in validation_stats
        )
        max_elapsed = max(elapsed_values) if elapsed_values else 0.0
        direct_ok = sum(
            1 for s in validation_stats
            if bool_field(s.get("direct_warmup_ok"))
        )
        print(
            "seek validation: "
            f"cases={len(validation_stats)} "
            f"visible_steps={visible_steps} "
            f"direct_warmups={direct_ok} "
            f"max_elapsed={max_elapsed:.2f}s "
            f"threshold={max_seek_validation_seconds:.2f}s "
            f"slow_cases={len(validation_failures)}"
        )
    for group, items in failures.items():
        labels = ", ".join(str(i.get("label", "?")) for i in items)
        print(f"failure group: {group}: {len(items)} ({labels})")

    if summaries:
        summary = summaries[-1]
        print(
            "summary: "
            f"passed={summary.get('passed', '?')} "
            f"reason={summary.get('reason', '?')} "
            f"passed_cases={summary.get('passed_cases', '?')} "
            f"failed_cases={summary.get('failed_cases', '?')}"
        )
    return 1 if failed else 0


def summarize_legacy(events: list[dict[str, Any]], require_tests: bool) -> int:
    print("test run: no replay_seek_test_* events found; legacy diagnostic summary")
    names = Counter(event_name(e) for e in events)
    raw_mismatches = names["captured_snapshot_mismatch_detail"]

    hard_groups: defaultdict[str, list[dict[str, Any]]] = defaultdict(list)
    for e in events:
        name = event_name(e)
        failure = str(e.get("failure") or "")
        reason = str(e.get("reason") or "")
        if "CapturedSnapshotCompareFailed" in failure:
            hard_groups["raw-only drift"].append(e)
        elif name == "captured_seek_target_not_playable" or "BattleManagerStatusNotActive" in failure:
            hard_groups["BM inactive"].append(e)
        elif name == "captured_seek_play_blocked" or "SemanticMismatch" in failure or "oracle" in reason.lower():
            hard_groups["semantic/oracle failure"].append(e)
        elif name in {"captured_seek_failed", "sc6_failed"} and failure:
            bucket = classify_failure(e)
            hard_groups[bucket].append(e)

    print(f"legacy raw mismatch details: {raw_mismatches}")
    for group, items in hard_groups.items():
        print(f"legacy group: {group}: {len(items)}")
        for e in items[:5]:
            print(
                f"  event={event_name(e)} label={e.get('label', '?')} "
                f"seq={e.get('requested_seq', e.get('target_seq', '?'))} "
                f"failure={e.get('failure', '?')} reason={e.get('reason', '?')}"
            )
        if len(items) > 5:
            print(f"  ... {len(items) - 5} more")

    if require_tests:
        return 2
    return 0


def strict_failures(
    events: list[dict[str, Any]],
    min_resume_tick_rate: float = DEFAULT_MIN_RESUME_TICK_RATE,
    resume_tick_window: int = DEFAULT_RESUME_TICK_WINDOW,
    max_seek_validation_seconds: float = DEFAULT_MAX_SEEK_VALIDATION_SECONDS,
    max_resume_tick_gap_seconds: float = DEFAULT_MAX_RESUME_TICK_GAP_SECONDS,
    max_first_resume_tick_seconds: float = DEFAULT_MAX_FIRST_RESUME_TICK_SECONDS,
    max_seek_queue_seconds: float = DEFAULT_MAX_SEEK_QUEUE_SECONDS,
    max_seek_land_seconds: float = DEFAULT_MAX_SEEK_LAND_SECONDS,
    max_seek_resume_handoff_seconds: float = (
        DEFAULT_MAX_SEEK_RESUME_HANDOFF_SECONDS
    ),
    max_seek_total_resume_seconds: float = DEFAULT_MAX_SEEK_TOTAL_RESUME_SECONDS,
) -> list[str]:
    failures: list[str] = []
    complete = [e for e in events if event_name(e) == "generate_complete"]
    if not complete:
        failures.append("missing generate_complete")
    else:
        latest_complete = complete[-1]
        if not bool_field(latest_complete.get("integrity_ok")):
            failures.append("generate_complete.integrity_ok is false")
        if not bool_field(latest_complete.get("oracle_ok")):
            failures.append("generate_complete.oracle_ok is false")

    seekability = [e for e in events if event_name(e) == "generate_seekability_summary"]
    if not seekability:
        failures.append("missing generate_seekability_summary")
    elif not bool_field(seekability[-1].get("seekable")):
        failures.append("generate_seekability_summary.seekable is false")

    start_results = [
        e for e in events if event_name(e) == "replay_file_start_result"
    ]
    if start_results:
        start = start_results[-1]
        if not bool_field(start.get("ok")):
            failures.append("replay_file_start_result.ok is false")
        if str(start.get("reason") or "") != "generated timeline ready":
            failures.append(
                "replay_file_start_result.reason is not generated timeline ready"
            )
        if not bool_field(start.get("native_replay_metadata_valid")):
            failures.append("native replay metadata was not valid")
        if not bool_field(start.get("native_profile_applied")):
            failures.append("native replay profile was not applied")

        match_data = [
            e for e in events
            if event_name(e) == "native_replay_match_data_summary_applied"
            and bool_field(e.get("ok"))
        ]
        if not match_data:
            failures.append("missing native replay match-data summary")
        else:
            latest_match = match_data[-1]
            if latest_match.get("left_rank") != latest_match.get("left_rank_readback"):
                failures.append("left rank readback mismatch")
            if latest_match.get("right_rank") != latest_match.get("right_rank_readback"):
                failures.append("right rank readback mismatch")
            if latest_match.get("left_player_points") != latest_match.get("left_points_readback"):
                failures.append("left RP readback mismatch")
            if latest_match.get("right_player_points") != latest_match.get("right_points_readback"):
                failures.append("right RP readback mismatch")
            if latest_match.get("left_chara") != latest_match.get("left_style_readback"):
                failures.append("left style readback mismatch")
            if latest_match.get("right_chara") != latest_match.get("right_style_readback"):
                failures.append("right style readback mismatch")
            if is_fallback_display_name(latest_match.get("left_display_name")):
                failures.append("left display name is Steam fallback")
            if is_fallback_display_name(latest_match.get("right_display_name")):
                failures.append("right display name is Steam fallback")

    results = [e for e in events if event_name(e) == "replay_seek_test_case_result"]
    has_watch_cases = False
    for result in results:
        label = result.get("label", "?")
        action = str(result.get("action") or "")
        is_ui_step = action in UI_STEP_ACTIONS
        if not bool_field(result.get("passed")):
            failures.append(f"case {label} failed")
        if raw_mismatch_is_strict_failure(result):
            failures.append(f"case {label} has raw_mismatch_first_offsets")
        resume_requested = int_field(result.get("resume_frames_requested"))
        resume_observed = int_field(result.get("resume_frames_observed"))
        state_compares = int_field(result.get("resume_state_compares"))
        state_mismatches = int_field(result.get("resume_state_mismatches"))
        if resume_requested > 0:
            has_watch_cases = True
        if state_mismatches > 0:
            field = result.get("resume_state_first_mismatch_field", "?")
            seq = result.get("resume_state_first_mismatch_seq", "?")
            failures.append(
                f"case {label} has resume state mismatch at seq {seq} field {field}"
            )
        if resume_requested > 0 and resume_observed > 0 and state_compares <= 0:
            failures.append(f"case {label} advanced without resume state compares")
        if is_ui_step:
            source_seq = int_field(result.get("ui_step_source_seq"))
            target_seq = int_field(result.get("ui_step_target_seq"))
            source_master = int_field(result.get("ui_step_source_master"))
            target_master = int_field(result.get("ui_step_target_master"))
            source_round = int_field(result.get("ui_step_source_round"))
            target_round = int_field(result.get("ui_step_target_round"))
            step_delta, step_count = ui_step_expected_delta_count(result)
            expected_delta = step_delta * step_count
            if not bool_field(result.get("ui_step_requested")):
                failures.append(f"case {label} did not request ui-step")
            if not bool_field(result.get("ui_step_landed")):
                failures.append(f"case {label} did not land ui-step")
            if target_seq != source_seq + expected_delta:
                failures.append(
                    f"case {label} ui-step target_seq is not source"
                    f"{expected_delta:+d} ({source_seq}->{target_seq})"
                )
            if target_round != source_round:
                failures.append(
                    f"case {label} ui-step crossed rounds "
                    f"({source_round}->{target_round})"
                )
            if target_master != source_master + expected_delta:
                failures.append(
                    f"case {label} ui-step target_master is not source"
                    f"{expected_delta:+d} "
                    f"({source_master}->{target_master})"
                )

    ui_step_results = [
        r for r in results
        if str(r.get("action") or "") in UI_STEP_ACTIONS
    ]
    if ui_step_results:
        posted = [
            e for e in events
            if event_name(e) == "replay_seek_test_ui_step_posted"
        ]
        landed = [
            e for e in events
            if event_name(e) == "replay_seek_test_ui_step_landed"
        ]
        if len(posted) < len(ui_step_results):
            failures.append(
                "missing replay_seek_test_ui_step_posted events "
                f"({len(posted)}/{len(ui_step_results)})"
            )
        if len(landed) < len(ui_step_results):
            failures.append(
                "missing replay_seek_test_ui_step_landed events "
                f"({len(landed)}/{len(ui_step_results)})"
            )

    if has_watch_cases:
        _, tick_failures = collect_resume_tick_failures(
            events,
            min_resume_tick_rate,
            resume_tick_window,
            max_resume_tick_gap_seconds,
            max_first_resume_tick_seconds,
        )
        failures.extend(tick_failures)
        _, validation_failures = collect_seek_validation_failures(
            events,
            max_seek_validation_seconds,
        )
        failures.extend(validation_failures)
        _, lifecycle_failures = collect_seek_lifecycle_failures(
            events,
            max_seek_queue_seconds,
            max_seek_land_seconds,
            max_seek_resume_handoff_seconds,
            max_seek_total_resume_seconds,
        )
        failures.extend(lifecycle_failures)

    mismatch_events = [
        e for e in events
        if event_name(e) == "replay_seek_test_resume_state_mismatch"
    ]
    if mismatch_events:
        failures.append(f"resume state mismatch events: {len(mismatch_events)}")

    overlay_events = [
        e for e in events if event_name(e) in PER_TICK_ASSIST_EVENTS
    ]
    if overlay_events:
        counts = Counter(event_name(e) for e in overlay_events)
        detail = ", ".join(
            f"{name}={count}" for name, count in sorted(counts.items())
        )
        failures.append(
            "per-tick resume assist writer ran "
            f"({len(overlay_events)} events"
            + (f": {detail}" if detail else "")
            + ")"
        )

    observation_count, drain_events, has_drain_marker, boundary_issues = (
        collect_native_step_boundary_issues(events)
    )
    if boundary_issues:
        failures.append(f"native-step boundary issues: {len(boundary_issues)}")
    if observation_count and has_drain_marker and drain_events == 0:
        failures.append("native-step observations without drain events")

    visual_diagnostics = [
        e for i, e in enumerate(events)
        if is_visual_oracle_diagnostic_failure(e)
        and not is_repaired_pre_overlay_hitcue_diagnostic(events, i)
    ]
    if visual_diagnostics:
        field_counts = Counter(
            str(e.get("field") or "?") for e in visual_diagnostics
        )
        top_fields = ", ".join(
            f"{field}={count}"
            for field, count in field_counts.most_common(5)
        )
        failures.append(
            "visual/gameplay oracle diagnostics: "
            f"{len(visual_diagnostics)} USER mismatches"
            + (f" ({top_fields})" if top_fields else "")
        )

    summaries = [e for e in events if event_name(e) == "replay_seek_test_summary"]
    if not summaries:
        failures.append("missing replay_seek_test_summary")
    elif not bool_field(summaries[-1].get("passed")):
        failures.append("replay_seek_test_summary.passed is false")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", nargs="?", help="Trace JSONL path")
    parser.add_argument("--latest-dir", default=str(DEFAULT_TRACE_DIR))
    parser.add_argument("--run-id", help="Only summarize this run_id")
    parser.add_argument(
        "--require-tests",
        action="store_true",
        help="Exit nonzero if no replay_seek_test_* events are present",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail on generation, seekability, case, boundary, or summary issues",
    )
    parser.set_defaults(min_resume_tick_rate=DEFAULT_MIN_RESUME_TICK_RATE)
    parser.add_argument(
        "--min-resume-tick-rate",
        dest="min_resume_tick_rate",
        type=float,
        default=argparse.SUPPRESS,
        help=(
            "Minimum native replay ticks per second during the post-seek "
            f"watch window. Default: {DEFAULT_MIN_RESUME_TICK_RATE:.1f} t/s"
        ),
    )
    parser.add_argument(
        "--resume-tick-window",
        type=int,
        default=DEFAULT_RESUME_TICK_WINDOW,
        help=(
            "Native replay ticks to measure immediately after resume. "
            f"Default: {DEFAULT_RESUME_TICK_WINDOW}"
        ),
    )
    parser.add_argument(
        "--max-seek-validation-seconds",
        type=float,
        default=DEFAULT_MAX_SEEK_VALIDATION_SECONDS,
        help=(
            "Maximum visible validation warmup time before playback. "
            f"Default: {DEFAULT_MAX_SEEK_VALIDATION_SECONDS:.2f}s"
        ),
    )
    parser.add_argument(
        "--max-seek-land-seconds",
        type=float,
        default=DEFAULT_MAX_SEEK_LAND_SECONDS,
        help=(
            "Maximum time from captured seek queue to captured seek landing "
            "for watched cases. Use 0 to disable. "
            f"Default: {DEFAULT_MAX_SEEK_LAND_SECONDS:.3f}s"
        ),
    )
    parser.add_argument(
        "--max-seek-queue-seconds",
        type=float,
        default=DEFAULT_MAX_SEEK_QUEUE_SECONDS,
        help=(
            "Maximum time from seek-test case start to captured seek queue. "
            "Use 0 to disable. "
            f"Default: {DEFAULT_MAX_SEEK_QUEUE_SECONDS:.3f}s"
        ),
    )
    parser.add_argument(
        "--max-seek-resume-handoff-seconds",
        type=float,
        default=DEFAULT_MAX_SEEK_RESUME_HANDOFF_SECONDS,
        help=(
            "Maximum time from captured seek landing to automated resume "
            "start. Use 0 to disable. "
            f"Default: {DEFAULT_MAX_SEEK_RESUME_HANDOFF_SECONDS:.3f}s"
        ),
    )
    parser.add_argument(
        "--max-seek-total-resume-seconds",
        type=float,
        default=DEFAULT_MAX_SEEK_TOTAL_RESUME_SECONDS,
        help=(
            "Maximum time from seek-test case start to automated resume "
            "start. Use 0 to disable. "
            f"Default: {DEFAULT_MAX_SEEK_TOTAL_RESUME_SECONDS:.3f}s"
        ),
    )
    parser.add_argument(
        "--max-resume-tick-gap-seconds",
        type=float,
        default=DEFAULT_MAX_RESUME_TICK_GAP_SECONDS,
        help=(
            "Maximum wall-clock gap per native replay tick during resumed "
            "watchback. Use 0 to disable. "
            f"Default: {DEFAULT_MAX_RESUME_TICK_GAP_SECONDS:.3f}s"
        ),
    )
    parser.add_argument(
        "--max-first-resume-tick-seconds",
        type=float,
        default=DEFAULT_MAX_FIRST_RESUME_TICK_SECONDS,
        help=(
            "Maximum delay from resume command service to the first native "
            "playback tick. Use 0 to disable. "
            f"Default: {DEFAULT_MAX_FIRST_RESUME_TICK_SECONDS:.3f}s"
        ),
    )
    args = parser.parse_args()

    path = Path(args.trace) if args.trace else latest_trace(Path(args.latest_dir))
    if path is None:
        print("error: no replay_trace_*.jsonl found", file=sys.stderr)
        return 2
    if not path.exists():
        print(f"error: trace not found: {path}", file=sys.stderr)
        return 2

    events = load_events(path)
    if args.run_id:
        events = [
            e for e in events
            if e.get("run_id") in (None, args.run_id)
            or event_name(e) not in {
                "replay_seek_test_start",
                "replay_seek_test_case_start",
                "replay_seek_test_case_result",
                "replay_seek_test_summary",
            }
        ]

    print(f"trace: {path}")
    print(f"events: {len(events)}")
    summarize_generation(events)
    boundary_status = summarize_native_step_boundaries(events, args.strict)
    test_status = summarize_test_events(
        events,
        args.min_resume_tick_rate,
        args.resume_tick_window,
        args.max_seek_validation_seconds,
        args.max_resume_tick_gap_seconds,
        args.max_first_resume_tick_seconds,
        args.max_seek_queue_seconds,
        args.max_seek_land_seconds,
        args.max_seek_resume_handoff_seconds,
        args.max_seek_total_resume_seconds,
    )
    if args.strict:
        failures = strict_failures(
            events,
            args.min_resume_tick_rate,
            args.resume_tick_window,
            args.max_seek_validation_seconds,
            args.max_resume_tick_gap_seconds,
            args.max_first_resume_tick_seconds,
            args.max_seek_queue_seconds,
            args.max_seek_land_seconds,
            args.max_seek_resume_handoff_seconds,
            args.max_seek_total_resume_seconds,
        )
        if failures:
            print("strict: FAIL")
            for failure in failures:
                print(f"  strict failure: {failure}")
            return 1
        print("strict: PASS")
        return 0
    if test_status is not None:
        return test_status or boundary_status
    legacy_status = summarize_legacy(events, args.require_tests)
    return legacy_status or boundary_status


if __name__ == "__main__":
    raise SystemExit(main())
