#!/usr/bin/env python3
"""Analyze HorseMod replay seek test JSONL traces."""

from __future__ import annotations

import argparse
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
    if field.startswith("hit-cue-"):
        return True
    if not field.startswith("hitcue"):
        return False
    if "cached-local" in field or "cached-world" in field:
        return False
    return True


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


def summarize_test_events(events: list[dict[str, Any]]) -> int | None:
    results = [e for e in events if event_name(e) == "replay_seek_test_case_result"]
    summaries = [e for e in events if event_name(e) == "replay_seek_test_summary"]
    starts = [e for e in events if event_name(e) == "replay_seek_test_start"]
    if not results and not summaries and not starts:
        return None

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
    for r in results:
        label = r.get("label", "?")
        ok = bool(r.get("passed"))
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


def strict_failures(events: list[dict[str, Any]]) -> list[str]:
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
    for result in results:
        label = result.get("label", "?")
        if not bool_field(result.get("passed")):
            failures.append(f"case {label} failed")
        if raw_mismatch_is_strict_failure(result):
            failures.append(f"case {label} has raw_mismatch_first_offsets")
        resume_requested = int_field(result.get("resume_frames_requested"))
        resume_observed = int_field(result.get("resume_frames_observed"))
        state_compares = int_field(result.get("resume_state_compares"))
        state_mismatches = int_field(result.get("resume_state_mismatches"))
        if state_mismatches > 0:
            field = result.get("resume_state_first_mismatch_field", "?")
            seq = result.get("resume_state_first_mismatch_seq", "?")
            failures.append(
                f"case {label} has resume state mismatch at seq {seq} field {field}"
            )
        if resume_requested > 0 and resume_observed > 0 and state_compares <= 0:
            failures.append(f"case {label} advanced without resume state compares")

    mismatch_events = [
        e for e in events
        if event_name(e) == "replay_seek_test_resume_state_mismatch"
    ]
    if mismatch_events:
        failures.append(f"resume state mismatch events: {len(mismatch_events)}")

    observation_count, drain_events, has_drain_marker, boundary_issues = (
        collect_native_step_boundary_issues(events)
    )
    if boundary_issues:
        failures.append(f"native-step boundary issues: {len(boundary_issues)}")
    if observation_count and has_drain_marker and drain_events == 0:
        failures.append("native-step observations without drain events")

    visual_diagnostics = [
        e for e in events if is_visual_oracle_diagnostic_failure(e)
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
    test_status = summarize_test_events(events)
    if args.strict:
        failures = strict_failures(events)
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
