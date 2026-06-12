#!/usr/bin/env python3
"""Analyze HorseMod replay seek test JSONL traces."""

from __future__ import annotations

import argparse
import json
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


def summarize_native_step_boundaries(events: list[dict[str, Any]]) -> int:
    observed_names = {
        "captured_seek_validation_step_observed",
        "sc6_native_step_observed",
    }
    observations = [e for e in events if event_name(e) in observed_names]
    if not observations:
        print("native-step boundary: no native-step observations")
        return 0

    has_drain_marker = any(
        e.get("native_step_drain_event") is True
        or e.get("build") == "replay-accuracy-v13a"
        for e in events
    )
    latest_drained_total: int | None = None
    drain_events = 0
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

    print(
        "native-step boundary: "
        f"observations={len(observations)} drains={drain_events} "
        f"issues={len(issues)}"
    )
    for issue in issues[:5]:
        print(f"  boundary issue: {issue}")
    if len(issues) > 5:
        print(f"  ... {len(issues) - 5} more")

    if issues and has_drain_marker:
        return 1
    if observations and has_drain_marker and drain_events == 0:
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
    for r in results:
        label = r.get("label", "?")
        ok = bool(r.get("passed"))
        raw = str(r.get("raw_mismatch_first_offsets") or "none")
        if raw not in ("", "none", "None"):
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
            f"resume={r.get('resume_frames_observed', 0)}/"
            f"{r.get('resume_frames_requested', 0)} "
            f"reason={r.get('pass_fail_reason', '?')} "
            f"failure={r.get('failure', '?')}"
        )
        if raw not in ("", "none", "None"):
            print(f"  raw diagnostic: {raw}")

    print(f"cases: passed={passed} failed={failed} raw_diagnostics={raw_diag}")
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
    boundary_status = summarize_native_step_boundaries(events)
    test_status = summarize_test_events(events)
    if test_status is not None:
        return test_status or boundary_status
    legacy_status = summarize_legacy(events, args.require_tests)
    return legacy_status or boundary_status


if __name__ == "__main__":
    raise SystemExit(main())
