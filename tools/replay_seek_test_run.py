#!/usr/bin/env python3
"""End-to-end runner for HorseMod replay seek tests.

This can kill/relaunch SC6, build/deploy HorseMod, request native replay
startup through HorseMod, write the replay seek request, wait for the in-game
harness to emit a summary, then run the analyzer. By default, the generated
cases include static seek landmarks plus watch-back sections that seek to
different timeline percentages and let playback run forward for real frames.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import subprocess
import sys
import time
from collections import Counter
from datetime import datetime
from pathlib import Path
from typing import Any, Callable


REPO_ROOT = Path(r"E:\myMods")
BUILD_BAT = REPO_ROOT / "build_and_deploy.bat"
DEFAULT_SAVED_DIR = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\ue4ss\Mods\HorseMod\Saved"
)
ANALYZER = REPO_ROOT / "tools" / "replay_seek_test_analyze.py"
DEFAULT_GAME_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\SoulcaliburVI.exe"
)
DEFAULT_STEAM_APPID = "544750"
DEFAULT_STATIC_SECTIONS = "0.02,0.10,0.25,0.50,0.75,0.90,0.97"
DEFAULT_WATCH_SECTIONS = "0.03,0.10,0.25,0.50,0.75,0.90"
DEFAULT_WATCH_FRAMES = 600


CrashCheck = Callable[[], str | None]


VK = {
    "BACKSPACE": 0x08,
    "TAB": 0x09,
    "ENTER": 0x0D,
    "SHIFT": 0x10,
    "CTRL": 0x11,
    "ALT": 0x12,
    "ESC": 0x1B,
    "SPACE": 0x20,
    "PAGEUP": 0x21,
    "PAGEDOWN": 0x22,
    "END": 0x23,
    "HOME": 0x24,
    "LEFT": 0x25,
    "UP": 0x26,
    "RIGHT": 0x27,
    "DOWN": 0x28,
    "INSERT": 0x2D,
    "DELETE": 0x2E,
}
for i in range(10):
    VK[str(i)] = 0x30 + i
for i, ch in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
    VK[ch] = 0x41 + i
for i in range(1, 13):
    VK[f"F{i}"] = 0x70 + i - 1


def trace_dir(saved_dir: Path) -> Path:
    return saved_dir / "ReplayTrace"


def seek_request_path(saved_dir: Path) -> Path:
    return saved_dir / "replay_seek_test_request.json"


def start_request_path(saved_dir: Path) -> Path:
    return saved_dir / "replay_file_start_request.json"


def state_request_path(saved_dir: Path) -> Path:
    return saved_dir / "sc6_state_request.json"


def state_status_path(saved_dir: Path) -> Path:
    return saved_dir / "sc6_state_status.json"


def parse_percent_list(text: str) -> list[float]:
    values: list[float] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        value = float(part)
        if value > 1.0:
            value /= 100.0
        if value < 0.0 or value > 1.0:
            raise ValueError(f"section percent out of range: {part}")
        values.append(value)
    if not values:
        raise ValueError("at least one section percent is required")
    return values


def percent_label(percent: float) -> str:
    return f"{int(round(percent * 100)):02d}pct"


def static_seek_cases(section_text: str) -> list[dict[str, Any]]:
    names = {
        0.02: "near_start",
        0.10: "early",
        0.25: "quarter",
        0.50: "mid",
        0.75: "late",
        0.90: "near_end",
        0.97: "tail_safe",
    }
    cases: list[dict[str, Any]] = []
    for percent in parse_percent_list(section_text):
        label = names.get(round(percent, 2), f"static_{percent_label(percent)}")
        cases.append(
            {
                "label": label,
                "percent": percent,
                "resume_frames": 0,
                "validation_mode": "static_target",
            }
        )
    return cases


def watch_back_cases(
    section_text: str,
    resume_frames: int,
    validation_mode: str,
) -> list[dict[str, Any]]:
    frames = max(1, resume_frames)
    cases: list[dict[str, Any]] = []
    for percent in parse_percent_list(section_text):
        cases.append(
            {
                "label": f"watch_{percent_label(percent)}_{frames}f",
                "percent": percent,
                "resume_frames": frames,
                "validation_mode": validation_mode,
            }
        )
    return cases


def default_cases(args: argparse.Namespace) -> list[dict[str, Any]]:
    if args.case_preset == "static":
        return static_seek_cases(args.static_sections)
    if args.case_preset == "watch":
        return watch_back_cases(
            args.watch_sections,
            args.watch_frames,
            args.watch_validation_mode,
        )
    return static_seek_cases(args.static_sections) + watch_back_cases(
        args.watch_sections,
        args.watch_frames,
        args.watch_validation_mode,
    )


def make_request(args: argparse.Namespace, run_id: str) -> dict[str, Any]:
    cases = getattr(args, "generated_cases", None) or default_cases(args)
    return {
        "enabled": True,
        "run_id": run_id,
        "generate_mode": args.generate_mode,
        "timeout_seconds": args.case_timeout,
        "cases": cases,
    }


def write_json_atomic(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    last_error: OSError | None = None
    for _ in range(40):
        try:
            os.replace(tmp, path)
            return
        except PermissionError as exc:
            last_error = exc
            time.sleep(0.05)
    try:
        tmp.unlink()
    except OSError:
        pass
    if last_error:
        raise last_error


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(obj, dict):
                    events.append(obj)
    except OSError:
        return []
    return events


def load_json_file(path: Path) -> dict[str, Any] | None:
    try:
        obj = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return obj if isinstance(obj, dict) else None


def event_name(event: dict[str, Any]) -> str:
    return str(event.get("event") or event.get("name") or "")


def recent_traces(root: Path, since: float, limit: int = 12) -> list[Path]:
    if not root.exists():
        return []
    traces = sorted(
        root.glob("replay_trace_*.jsonl"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    out: list[Path] = []
    for path in traces:
        try:
            if path.stat().st_mtime + 5 < since:
                continue
        except OSError:
            continue
        out.append(path)
        if len(out) >= limit:
            break
    return out


def find_run_event(
    root: Path,
    run_id: str,
    since: float,
    wanted: set[str],
) -> tuple[Path | None, dict[str, Any] | None]:
    for path in recent_traces(root, since):
        for event in load_jsonl(path):
            if event_name(event) in wanted and event.get("run_id") == run_id:
                return path, event
    return None, None


def find_last_run_event(
    root: Path,
    run_id: str,
    since: float,
    wanted: set[str],
) -> tuple[Path | None, dict[str, Any] | None]:
    last_trace: Path | None = None
    last_event: dict[str, Any] | None = None
    for path in reversed(recent_traces(root, since)):
        for event in load_jsonl(path):
            if event_name(event) in wanted and event.get("run_id") == run_id:
                last_trace = path
                last_event = event
    return last_trace, last_event


def find_replay_start_result(
    root: Path,
    run_id: str,
    since: float,
) -> tuple[Path | None, dict[str, Any] | None]:
    return find_run_event(root, run_id, since, {"replay_file_start_result"})


def find_state_snapshot(
    root: Path,
    run_id: str,
    since: float,
) -> tuple[Path | None, dict[str, Any] | None]:
    return find_last_run_event(root, run_id, since, {"sc6_state_snapshot"})


def read_state_status(
    saved_dir: Path,
    run_id: str,
    since: float,
) -> dict[str, Any] | None:
    path = state_status_path(saved_dir)
    try:
        if path.stat().st_mtime + 5 < since:
            return None
    except OSError:
        return None
    status = load_json_file(path)
    if not status or status.get("run_id") != run_id:
        return None
    return status


def process_exists_by_image(image_name: str) -> bool:
    completed = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {image_name}", "/FO", "CSV", "/NH"],
        capture_output=True,
        text=True,
    )
    output = (completed.stdout or "") + (completed.stderr or "")
    return image_name.lower() in output.lower()


class GameProcessMonitor:
    def __init__(
        self,
        game_exe: Path,
        process: subprocess.Popen[Any] | None,
        expect_running: bool,
    ) -> None:
        self.image_name = game_exe.name or "SoulcaliburVI.exe"
        self.process = process
        self.seen_running = process is not None or expect_running

    def crash_reason(self) -> str | None:
        if self.process is not None:
            code = self.process.poll()
            if code is None:
                self.seen_running = True
                return None
            if process_exists_by_image(self.image_name):
                self.seen_running = True
                return None
            return f"{self.image_name} exited with code {code}"

        running = process_exists_by_image(self.image_name)
        if running:
            self.seen_running = True
            return None
        if self.seen_running:
            return f"{self.image_name} is no longer running"
        return None


def request_state_snapshot(
    saved_dir: Path,
    trace_root: Path,
    run_id: str,
    reason: str,
    timeout: int,
    console_command: str = "",
    crash_check: CrashCheck | None = None,
) -> tuple[Path | None, dict[str, Any] | None]:
    request = {
        "enabled": True,
        "run_id": run_id,
        "reason": reason,
    }
    if console_command:
        request["console_command"] = console_command
    since = time.time()
    path = state_request_path(saved_dir)
    status_path = state_status_path(saved_dir)
    try:
        status_path.unlink()
    except OSError:
        pass
    write_json_atomic(path, request)
    print(f"wrote SC6 state request: {path}")
    deadline = since + max(1, timeout)
    last_status: dict[str, Any] | None = None
    while time.time() < deadline:
        if crash_check is not None:
            crash = crash_check()
            if crash:
                return None, {
                    "run_id": run_id,
                    "ok": False,
                    "failure": "game_crashed",
                    "reason": crash,
                }
        status = read_state_status(saved_dir, run_id, since)
        if status is not None:
            last_status = status
            trace_path = status.get("trace_path")
            return Path(trace_path) if trace_path else None, status
        trace, event = find_state_snapshot(trace_root, run_id, since)
        if event is not None:
            return trace, event
        time.sleep(1.0)
    try:
        path.unlink()
    except OSError:
        pass
    return None, last_status


def normalize_presence(value: object) -> str:
    return str(value or "").strip().lower().replace(" ", "")


def parse_presence_set(text: str) -> set[str]:
    return {
        normalize_presence(part)
        for part in text.split(",")
        if part.strip()
    }


def state_presence_known(snapshot: dict[str, Any] | None) -> bool:
    if not snapshot:
        return False
    presence = normalize_presence(snapshot.get("presence"))
    return presence not in {"", "unknown"}


def state_presence_matches(
    snapshot: dict[str, Any] | None,
    expected: set[str],
) -> bool:
    if not snapshot:
        return False
    if not expected:
        return True
    return normalize_presence(snapshot.get("presence")) in expected


def state_identity_summary(snapshot: dict[str, Any] | None) -> str:
    if not snapshot:
        return ""
    parts: list[str] = []
    for label, key in (
        ("world", "uworld_path"),
        ("gi", "game_instance_class_name"),
        ("pc", "player_controller_class_name"),
        ("bm", "battle_manager_class_name"),
        ("rp", "replay_player_class_name"),
    ):
        value = snapshot.get(key)
        if value:
            parts.append(f"{label}={value}")
    return " ".join(parts)


def wait_for_state_snapshot(
    saved_dir: Path,
    trace_root: Path,
    run_id: str,
    reason: str,
    timeout: int,
    expected_presence: set[str],
    require_known_presence: bool,
    console_command: str = "",
    crash_check: CrashCheck | None = None,
) -> tuple[Path | None, dict[str, Any] | None, str]:
    deadline = time.time() + max(1, timeout)
    attempt = 0
    last_trace: Path | None = None
    last_snapshot: dict[str, Any] | None = None
    def wait_next_probe() -> None:
        if time.time() < deadline:
            time.sleep(min(1.0, max(0.0, deadline - time.time())))

    while time.time() < deadline:
        if crash_check is not None:
            crash = crash_check()
            if crash:
                return last_trace, {
                    "run_id": run_id,
                    "ok": False,
                    "failure": "game_crashed",
                    "reason": crash,
                }, "game_crashed"
        attempt += 1
        remaining = max(1, int(deadline - time.time()))
        last_trace, last_snapshot = request_state_snapshot(
            saved_dir,
            trace_root,
            run_id,
            f"{reason}:{attempt}",
            min(5, remaining),
            console_command,
            crash_check,
        )
        if last_snapshot is not None and last_snapshot.get("failure") == "game_crashed":
            return last_trace, last_snapshot, "game_crashed"
        if last_snapshot is None:
            wait_next_probe()
            continue
        if not bool(last_snapshot.get("ok")):
            print(
                "SC6 state pending: "
                f"ok=False gengine={last_snapshot.get('gengine_ok')} "
                f"world={last_snapshot.get('uworld_ok')} "
                f"gi={last_snapshot.get('game_instance_ok')}"
            )
            wait_next_probe()
            continue
        if require_known_presence and not state_presence_known(last_snapshot):
            detail = state_identity_summary(last_snapshot)
            suffix = f" ({detail})" if detail else ""
            print(f"SC6 state pending: presence is still Unknown{suffix}")
            wait_next_probe()
            continue
        if not state_presence_matches(last_snapshot, expected_presence):
            expected = ",".join(sorted(expected_presence))
            print(
                "SC6 state pending: "
                f"presence={last_snapshot.get('presence')} expected={expected}"
            )
            wait_next_probe()
            continue
        return last_trace, last_snapshot, "ok"
    if last_snapshot is None:
        return last_trace, None, "timeout"
    if not bool(last_snapshot.get("ok")):
        return last_trace, last_snapshot, "state_not_ok"
    if require_known_presence and not state_presence_known(last_snapshot):
        return last_trace, last_snapshot, "presence_unknown"
    if not state_presence_matches(last_snapshot, expected_presence):
        return last_trace, last_snapshot, "presence_mismatch"
    return last_trace, last_snapshot, "timeout"


def find_last_replay_start_event(
    root: Path,
    run_id: str,
    since: float,
) -> tuple[Path | None, dict[str, Any] | None]:
    last_trace: Path | None = None
    last_event: dict[str, Any] | None = None
    for path in reversed(recent_traces(root, since)):
        for event in load_jsonl(path):
            if event.get("run_id") != run_id:
                continue
            if event_name(event).startswith("replay_file_start_"):
                last_trace = path
                last_event = event
    return last_trace, last_event


def find_run_summary(
    root: Path,
    run_id: str,
    since: float,
) -> tuple[Path | None, dict[str, Any] | None]:
    return find_run_event(root, run_id, since, {"replay_seek_test_summary"})


def latest_trace_for_run(root: Path, run_id: str, since: float) -> Path | None:
    for path in recent_traces(root, since):
        for event in load_jsonl(path):
            if event.get("run_id") == run_id:
                return path
    return None


def run_build() -> int:
    print(f"building: {BUILD_BAT}")
    completed = subprocess.run([str(BUILD_BAT)], cwd=str(REPO_ROOT))
    return completed.returncode


def kill_game() -> None:
    print("killing game: taskkill /IM SoulcaliburVI.exe /F")
    completed = subprocess.run(
        ["taskkill", "/IM", "SoulcaliburVI.exe", "/F"],
        capture_output=True,
        text=True,
    )
    if completed.stdout:
        print(completed.stdout.strip())
    if completed.returncode != 0 and completed.stderr:
        print(f"taskkill warning: {completed.stderr.strip()}")


def launch_game(game_exe: Path, steam_appid: str) -> subprocess.Popen[Any] | None:
    if game_exe.exists():
        print(f"launching game: {game_exe}")
        return subprocess.Popen([str(game_exe)], cwd=str(game_exe.parent))
    uri = f"steam://rungameid/{steam_appid}"
    print(f"launching game through Steam: {uri}")
    os.startfile(uri)  # type: ignore[attr-defined]
    return None


def find_window(title_contains: str) -> int | None:
    user32 = ctypes.windll.user32
    matches: list[int] = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def enum_proc(hwnd: int, _lparam: int) -> bool:
        if not user32.IsWindowVisible(hwnd):
            return True
        buf = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(hwnd, buf, len(buf))
        title = buf.value
        if title_contains.lower() in title.lower():
            matches.append(hwnd)
            return False
        return True

    user32.EnumWindows(enum_proc, 0)
    return matches[0] if matches else None


def focus_game_window(title_contains: str, timeout: int) -> bool:
    user32 = ctypes.windll.user32
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = find_window(title_contains)
        if hwnd:
            user32.ShowWindow(hwnd, 5)  # SW_SHOW
            user32.SetForegroundWindow(hwnd)
            print(f"focused window containing: {title_contains}")
            return True
        time.sleep(1.0)
    print(f"warning: could not find window containing {title_contains!r}")
    return False


def key_code(name: str) -> int:
    upper = name.upper()
    if upper not in VK:
        raise ValueError(f"unknown key {name!r}; add it to VK map")
    return VK[upper]


def press_key(name: str, hold_seconds: float = 0.05) -> None:
    user32 = ctypes.windll.user32
    vk = key_code(name)
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(hold_seconds)
    user32.keybd_event(vk, 0, 2, 0)  # KEYEVENTF_KEYUP


def run_menu_script(path: Path) -> None:
    script = json.loads(path.read_text(encoding="utf-8"))
    steps = script.get("steps", script if isinstance(script, list) else [])
    if not isinstance(steps, list):
        raise ValueError("menu script must be a list or an object with steps")
    print(f"running menu script: {path}")
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise ValueError(f"step {index} is not an object")
        wait = float(step.get("wait", step.get("sleep", 0.0)) or 0.0)
        if wait > 0:
            time.sleep(wait)
        key = step.get("key")
        if key:
            repeat = int(step.get("repeat", 1) or 1)
            delay = float(step.get("delay", 0.15) or 0.15)
            hold = float(step.get("hold", 0.05) or 0.05)
            for _ in range(max(1, repeat)):
                press_key(str(key), hold)
                time.sleep(delay)
        text = step.get("text")
        if text:
            for ch in str(text):
                if ch == " ":
                    press_key("SPACE")
                elif ch.isalnum():
                    press_key(ch.upper())
                else:
                    raise ValueError(f"text character {ch!r} is not supported")
                time.sleep(float(step.get("delay", 0.05) or 0.05))


def run_analyzer(trace: Path, run_id: str, strict: bool) -> tuple[int, str]:
    cmd = [sys.executable, str(ANALYZER), str(trace), "--require-tests"]
    if run_id:
        cmd += ["--run-id", run_id]
    if strict:
        cmd += ["--strict"]
    print("analyzing:", " ".join(cmd))
    completed = subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
    )
    output = ""
    if completed.stdout:
        print(completed.stdout, end="")
        output += completed.stdout
    if completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)
        output += completed.stderr
    return completed.returncode, output


def collect_failure_artifacts(
    trace: Path | None,
    run_id: str,
) -> tuple[list[str], dict[str, int]]:
    if trace is None:
        return [], {}
    failed_labels: list[str] = []
    groups: Counter[str] = Counter()
    for event in load_jsonl(trace):
        if event_name(event) != "replay_seek_test_case_result":
            continue
        if event.get("run_id") != run_id:
            continue
        if bool(event.get("passed")):
            continue
        label = str(event.get("label") or "?")
        failed_labels.append(label)
        group = str(
            event.get("failure")
            or event.get("pass_fail_reason")
            or event.get("reason")
            or "unknown"
        )
        groups[group] += 1
    return failed_labels, dict(groups)


def write_reports(
    report_dir: Path | None,
    run_id: str,
    report: dict[str, Any],
) -> tuple[Path | None, Path | None]:
    if report_dir is None:
        return None, None
    report_dir.mkdir(parents=True, exist_ok=True)
    json_path = report_dir / f"replay_seek_e2e_{run_id}.json"
    txt_path = report_dir / f"replay_seek_e2e_{run_id}.txt"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    lines = [
        f"run_id: {run_id}",
        f"trace: {report.get('trace_path') or ''}",
        f"final_passed: {report.get('final_passed')}",
        f"exit_code: {report.get('exit_code')}",
        f"preflight_state_trace: {report.get('preflight_state_trace') or ''}",
        f"preflight_state_status: {report.get('preflight_state_status') or ''}",
        f"preflight_state_snapshot: {report.get('preflight_state_snapshot')}",
        f"replay_start_result: {report.get('replay_start_result')}",
        f"last_replay_start_event: {report.get('last_replay_start_event')}",
        f"summary: {report.get('summary')}",
        f"crash_reason: {report.get('crash_reason') or ''}",
        f"failed_case_labels: {', '.join(report.get('failed_case_labels') or [])}",
        f"failure_groups: {report.get('failure_groups')}",
        "",
        "analyzer stdout:",
        str(report.get("analyzer_stdout") or ""),
    ]
    txt_path.write_text("\n".join(lines), encoding="utf-8")
    return json_path, txt_path


def finish_run(
    report_dir: Path | None,
    run_id: str,
    replay_start_result: dict[str, Any] | None,
    last_replay_start_event: dict[str, Any] | None,
    trace: Path | None,
    analyzer_stdout: str,
    analyzer_code: int,
    summary: dict[str, Any] | None,
    exit_code: int,
    preflight_state_snapshot: dict[str, Any] | None = None,
    preflight_state_trace: Path | None = None,
    preflight_state_status: str = "",
    crash_reason: str = "",
) -> int:
    failed_labels, failure_groups = collect_failure_artifacts(trace, run_id)
    final_passed = exit_code == 0
    report = {
        "run_id": run_id,
        "preflight_state_snapshot": preflight_state_snapshot,
        "preflight_state_trace": str(preflight_state_trace) if preflight_state_trace else None,
        "preflight_state_status": preflight_state_status,
        "replay_start_result": replay_start_result,
        "last_replay_start_event": last_replay_start_event,
        "trace_path": str(trace) if trace else None,
        "analyzer_stdout": analyzer_stdout,
        "analyzer_exit_code": analyzer_code,
        "summary": summary,
        "crash_reason": crash_reason,
        "summary_passed": bool(summary.get("passed")) if summary else False,
        "failed_case_labels": failed_labels,
        "failure_groups": failure_groups,
        "final_passed": final_passed,
        "exit_code": exit_code,
    }
    json_report, txt_report = write_reports(report_dir, run_id, report)
    if json_report and txt_report:
        print(f"report json: {json_report}")
        print(f"report text: {txt_report}")
    print(
        f"final: {'PASS' if final_passed else 'FAIL'} "
        f"run_id={run_id} trace={trace} failed_cases={failed_labels}"
    )
    return exit_code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kill-game", action="store_true", help="Kill SoulcaliburVI.exe first")
    parser.add_argument("--build", action="store_true", help="Run build_and_deploy.bat first")
    parser.add_argument("--launch-game", action="store_true", help="Launch SC6 before writing request")
    parser.add_argument("--game-exe", default=str(DEFAULT_GAME_EXE))
    parser.add_argument("--steam-appid", default=DEFAULT_STEAM_APPID)
    parser.add_argument("--window-title", default="SoulcaliburVI")
    parser.add_argument("--focus-timeout", type=int, default=90)
    parser.add_argument(
        "--legacy-focus-launch",
        action="store_true",
        help="Legacy only: focus the SC6 window after launching it",
    )
    parser.add_argument(
        "--focus-game",
        action="store_true",
        help="Focus an already-running SC6 window before writing requests",
    )
    parser.add_argument(
        "--state-timeout",
        type=int,
        default=90,
        help="Seconds to wait for HorseMod sc6_state_snapshot preflight",
    )
    parser.add_argument(
        "--skip-state-preflight",
        action="store_true",
        help="Do not request HorseMod state before launch/start automation",
    )
    parser.add_argument(
        "--state-only",
        action="store_true",
        help="Request one HorseMod SC6 state snapshot and exit",
    )
    parser.add_argument(
        "--state-console-command",
        default="",
        help="Allowlisted diagnostic command to execute with state snapshots",
    )
    parser.add_argument(
        "--allow-unknown-presence",
        action="store_true",
        help="Allow state preflight to pass before SC6 emits SetPresence",
    )
    parser.add_argument(
        "--expect-presence",
        default="",
        help="Comma-separated required presence names, e.g. MainMenu or Replay",
    )
    parser.add_argument("--saved-dir", default=str(DEFAULT_SAVED_DIR))
    parser.add_argument("--start-replay", metavar="PATH", help="Replay file to launch through HorseMod")
    parser.add_argument("--start-timeout", type=int, default=180)
    parser.add_argument(
        "--native-audit-seconds",
        type=int,
        default=0,
        help=(
            "After --start-replay succeeds, wait this many seconds without "
            "writing a seek-test request. Used to audit untouched native "
            "playback outcome events."
        ),
    )
    parser.add_argument(
        "--force-native-launch",
        action="store_true",
        help="Diagnostic only: call ManualLaunchBattle even if CanLaunchBattleManually is false",
    )
    parser.add_argument("--strict", action="store_true", help="Use strict analyzer checks")
    parser.add_argument("--report-dir", help="Directory for E2E JSON/TXT report files")
    parser.add_argument(
        "--menu-script",
        help="Legacy JSON key macro to navigate menus/start a replay",
    )
    parser.add_argument("--wait", action="store_true", help="Wait for replay_seek_test_summary")
    parser.add_argument("--analyze", action="store_true", help="Run analyzer after summary")
    parser.add_argument("--analyze-only", metavar="TRACE", help="Only analyze a trace")
    parser.add_argument("--run-id", help="Run id to write or analyze")
    parser.add_argument(
        "--generate-mode",
        default="normal",
        choices=["normal", "experimental", "battle_step"],
    )
    parser.add_argument(
        "--case-preset",
        default="both",
        choices=["static", "watch", "both"],
        help=(
            "Default seek-test cases to emit when --request is omitted. "
            "'watch' seeks to timeline sections and plays forward; 'both' "
            "keeps static seek checks and adds watch-back playback checks."
        ),
    )
    parser.add_argument(
        "--static-sections",
        default=DEFAULT_STATIC_SECTIONS,
        help=(
            "Comma-separated timeline percents for static seek checks. "
            "Values may be fractions or percents."
        ),
    )
    parser.add_argument(
        "--watch-sections",
        default=DEFAULT_WATCH_SECTIONS,
        help=(
            "Comma-separated timeline percents for watch-back checks. "
            "Values may be fractions or percents."
        ),
    )
    parser.add_argument(
        "--watch-frames",
        type=int,
        default=DEFAULT_WATCH_FRAMES,
        help="Replay frames to watch after each watch-back seek case",
    )
    parser.add_argument(
        "--watch-validation-mode",
        default="static_target",
        choices=["static_target", "target_to_next"],
        help=(
            "Seek validation used before watch-back playback. static_target "
            "lands on the section then watches real playback; target_to_next "
            "also requires one captured frame-step validation before playback."
        ),
    )
    parser.add_argument("--case-timeout", type=int, default=600)
    parser.add_argument("--timeout", type=int, default=900, help="Wait timeout seconds")
    parser.add_argument("--request", help="Path to custom seek request JSON")
    args = parser.parse_args()

    if args.analyze_only:
        run_id = args.run_id or ""
        code, _ = run_analyzer(Path(args.analyze_only), run_id, args.strict)
        return code

    if args.strict and not args.wait:
        print("error: --strict requires --wait", file=sys.stderr)
        return 2

    if not args.request:
        try:
            args.generated_cases = default_cases(args)
        except ValueError as exc:
            print(f"error: invalid case section list: {exc}", file=sys.stderr)
            return 2

    saved_dir = Path(args.saved_dir)
    trace_root = trace_dir(saved_dir)
    report_dir = Path(args.report_dir) if args.report_dir else None
    run_id = args.run_id or datetime.now().strftime("%Y%m%d-%H%M%S-seek")
    expected_presence = parse_presence_set(args.expect_presence)
    require_known_presence = not args.allow_unknown_presence
    game_exe = Path(args.game_exe)
    game_process: subprocess.Popen[Any] | None = None

    if args.kill_game:
        kill_game()

    if args.build:
        code = run_build()
        if code != 0:
            return code

    if args.launch_game:
        game_process = launch_game(game_exe, args.steam_appid)
        if args.legacy_focus_launch or args.focus_game:
            focus_game_window(args.window_title, args.focus_timeout)
        else:
            print("not focusing SC6 window; use --legacy-focus-launch for old behavior")
    elif args.focus_game:
        if not focus_game_window(args.window_title, args.focus_timeout):
            return 4

    game_monitor = GameProcessMonitor(
        game_exe,
        game_process,
        expect_running=bool(args.start_replay and not args.launch_game),
    )

    if args.menu_script:
        print("warning: --menu-script is legacy keyboard automation and will focus SC6")
        if not focus_game_window(args.window_title, args.focus_timeout):
            return 4
        run_menu_script(Path(args.menu_script))

    preflight_state_trace: Path | None = None
    preflight_state_snapshot: dict[str, Any] | None = None
    preflight_state_status = ""
    if args.state_only or (
        not args.skip_state_preflight and (args.launch_game or args.start_replay)
    ):
        preflight_state_trace, preflight_state_snapshot, preflight_state_status = wait_for_state_snapshot(
            saved_dir,
            trace_root,
            run_id,
            "state_only" if args.state_only else "preflight",
            args.state_timeout,
            expected_presence,
            require_known_presence,
            args.state_console_command,
            game_monitor.crash_reason,
        )
        if preflight_state_status != "ok":
            print(
                "SC6 state preflight failed: "
                f"status={preflight_state_status} run_id={run_id} "
                f"snapshot={preflight_state_snapshot}"
            )
            crash_reason = ""
            if preflight_state_status == "game_crashed" and preflight_state_snapshot:
                crash_reason = str(preflight_state_snapshot.get("reason") or "game crashed")
            return finish_run(
                report_dir,
                run_id,
                None,
                None,
                preflight_state_trace,
                "",
                0,
                None,
                6 if preflight_state_status == "game_crashed" else 5,
                preflight_state_snapshot,
                preflight_state_trace,
                preflight_state_status,
                crash_reason,
            )
        print(
            "SC6 state: "
            f"trace={preflight_state_trace} ok={preflight_state_snapshot.get('ok')} "
            f"presence={preflight_state_snapshot.get('presence')} "
            f"gi_ok={preflight_state_snapshot.get('game_instance_ok')} "
            f"battle_pending={preflight_state_snapshot.get('battle_request_pending')} "
            f"manual_ready={preflight_state_snapshot.get('manual_launch_ready')} "
            f"console_command_ok={preflight_state_snapshot.get('console_command_ok')} "
            f"identity={state_identity_summary(preflight_state_snapshot)}"
        )
        if args.state_only:
            return finish_run(
                report_dir,
                run_id,
                None,
                None,
                preflight_state_trace,
                "",
                0,
                None,
                0 if bool(preflight_state_snapshot.get("ok")) else 1,
                preflight_state_snapshot,
                preflight_state_trace,
                preflight_state_status,
            )

    replay_start_result: dict[str, Any] | None = None
    last_replay_start_event: dict[str, Any] | None = None
    start_trace: Path | None = None
    request_override: dict[str, Any] | None = None
    if args.request:
        request_override = json.loads(Path(args.request).read_text(encoding="utf-8"))
    if args.start_replay:
        start_generate_mode = ""
        if args.wait:
            start_generate_mode = str(
                (request_override or {}).get("generate_mode") or args.generate_mode
            )
        request = {
            "enabled": True,
            "run_id": run_id,
            "path": args.start_replay,
            "timeout_seconds": args.start_timeout,
            "force_native_launch": args.force_native_launch,
        }
        if start_generate_mode:
            request["generate_mode"] = start_generate_mode
        start_since = time.time()
        path = start_request_path(saved_dir)
        write_json_atomic(path, request)
        print(f"wrote replay start request: {path}")
        print(f"run_id: {run_id}")

        deadline = start_since + max(1, args.start_timeout)
        while time.time() < deadline:
            crash_reason = game_monitor.crash_reason()
            if crash_reason:
                start_trace = latest_trace_for_run(trace_root, run_id, start_since)
                print(f"game crashed while waiting for replay_file_start_result: {crash_reason}")
                if start_trace:
                    print(f"partial trace: {start_trace}")
                return finish_run(report_dir, run_id, None,
                                  last_replay_start_event, start_trace, "", 0,
                                  None, 6, preflight_state_snapshot,
                                  preflight_state_trace, preflight_state_status,
                                  crash_reason)
            start_trace, replay_start_result = find_replay_start_result(
                trace_root, run_id, start_since
            )
            _, last_replay_start_event = find_last_replay_start_event(
                trace_root, run_id, start_since
            )
            if replay_start_result is not None:
                break
            time.sleep(2.0)

        if replay_start_result is None:
            # HorseMod can emit the timeout result at nearly the same moment
            # as this loop expires, and the trace writer may flush just after
            # our first scan. Poll briefly so the report captures the native
            # timeout result instead of only a partial trace.
            grace_deadline = time.time() + 8.0
            while time.time() < grace_deadline:
                crash_reason = game_monitor.crash_reason()
                if crash_reason:
                    start_trace = latest_trace_for_run(trace_root, run_id, start_since)
                    print(f"game crashed while waiting for replay_file_start_result: {crash_reason}")
                    if start_trace:
                        print(f"partial trace: {start_trace}")
                    return finish_run(report_dir, run_id, None,
                                      last_replay_start_event, start_trace, "", 0,
                                      None, 6, preflight_state_snapshot,
                                      preflight_state_trace, preflight_state_status,
                                      crash_reason)
                time.sleep(1.0)
                start_trace, replay_start_result = find_replay_start_result(
                    trace_root, run_id, start_since
                )
                _, last_replay_start_event = find_last_replay_start_event(
                    trace_root, run_id, start_since
                )
                if replay_start_result is not None:
                    break
        if replay_start_result is None:
            start_trace = latest_trace_for_run(trace_root, run_id, start_since)
            print(f"timeout waiting for replay_file_start_result run_id={run_id}")
            if start_trace:
                print(f"partial trace: {start_trace}")
            return finish_run(report_dir, run_id, None,
                              last_replay_start_event, start_trace, "", 0,
                              None, 3, preflight_state_snapshot,
                              preflight_state_trace, preflight_state_status)

        print(
            "replay start result: "
            f"trace={start_trace} ok={replay_start_result.get('ok')} "
            f"reason={replay_start_result.get('reason')} "
            f"presence={replay_start_result.get('presence')}"
        )
        if not bool(replay_start_result.get("ok")):
            return finish_run(report_dir, run_id, replay_start_result,
                              last_replay_start_event, start_trace, "", 0,
                              None, 1, preflight_state_snapshot,
                              preflight_state_trace, preflight_state_status)
        if args.native_audit_seconds > 0:
            deadline = time.time() + args.native_audit_seconds
            while time.time() < deadline:
                crash_reason = game_monitor.crash_reason()
                if crash_reason:
                    trace = latest_trace_for_run(trace_root, run_id, start_since)
                    print(f"game crashed during native playback audit: {crash_reason}")
                    if trace:
                        print(f"partial trace: {trace}")
                    return finish_run(report_dir, run_id, replay_start_result,
                                      last_replay_start_event, trace, "", 0,
                                      None, 6, preflight_state_snapshot,
                                      preflight_state_trace,
                                      preflight_state_status, crash_reason)
                time.sleep(1.0)
            trace = latest_trace_for_run(trace_root, run_id, start_since)
            print(
                "native playback audit complete: "
                f"seconds={args.native_audit_seconds} trace={trace}"
            )
            return finish_run(report_dir, run_id, replay_start_result,
                              last_replay_start_event, trace, "", 0, None, 0,
                              preflight_state_snapshot, preflight_state_trace,
                              preflight_state_status)
    else:
        print("game prerequisite: SC6 must already be running with HorseMod loaded and in a replay")

    request = (
        request_override
        if request_override is not None
        else make_request(args, run_id)
    )
    request["run_id"] = run_id
    request.setdefault("enabled", True)

    cases = request.get("cases") if isinstance(request, dict) else None
    if isinstance(cases, list):
        watch_cases = 0
        for case in cases:
            if not isinstance(case, dict):
                continue
            try:
                resume_frames = int(case.get("resume_frames") or 0)
            except (TypeError, ValueError):
                resume_frames = 0
            if resume_frames > 0:
                watch_cases += 1
        static_cases = len(cases) - watch_cases
        print(
            "seek case plan: "
            f"preset={args.case_preset if request_override is None else 'custom'} "
            f"cases={len(cases)} static={static_cases} watch={watch_cases} "
            f"watch_frames={args.watch_frames if request_override is None else 'custom'} "
            f"watch_validation={args.watch_validation_mode if request_override is None else 'custom'}"
        )

    saved_dir.mkdir(parents=True, exist_ok=True)
    seek_path = seek_request_path(saved_dir)
    summary_since = time.time()
    write_json_atomic(seek_path, request)
    print(f"wrote seek request: {seek_path}")
    print(f"run_id: {run_id}")

    if not args.wait:
        return 0

    deadline = summary_since + args.timeout
    trace: Path | None = None
    summary: dict[str, Any] | None = None
    while time.time() < deadline:
        crash_reason = game_monitor.crash_reason()
        if crash_reason:
            trace = latest_trace_for_run(trace_root, run_id, summary_since)
            if trace is None:
                trace = latest_trace_for_run(trace_root, run_id, start_since if args.start_replay else summary_since)
            print(f"game crashed while waiting for replay_seek_test_summary: {crash_reason}")
            if trace:
                print(f"partial trace: {trace}")
            return finish_run(report_dir, run_id, replay_start_result,
                              last_replay_start_event, trace, "", 0, None, 6,
                              preflight_state_snapshot, preflight_state_trace,
                              preflight_state_status, crash_reason)
        trace, summary = find_run_summary(trace_root, run_id, summary_since)
        if summary is not None:
            break
        time.sleep(2.0)

    if summary is None:
        trace = latest_trace_for_run(trace_root, run_id, summary_since)
        print(f"timeout waiting for replay_seek_test_summary run_id={run_id}")
        if trace:
            print(f"partial trace: {trace}")
        return finish_run(report_dir, run_id, replay_start_result,
                          last_replay_start_event, trace, "", 0, None, 3,
                          preflight_state_snapshot, preflight_state_trace,
                          preflight_state_status)

    print(
        "summary observed: "
        f"trace={trace} passed={summary.get('passed')} "
        f"passed_cases={summary.get('passed_cases')} "
        f"failed_cases={summary.get('failed_cases')} "
        f"reason={summary.get('reason')}"
    )

    analyzer_stdout = ""
    analyzer_code = 0
    if args.analyze or args.strict:
        if trace is None:
            return 3
        analyzer_code, analyzer_stdout = run_analyzer(trace, run_id, args.strict)

    exit_code = analyzer_code or (0 if summary.get("passed") else 1)
    return finish_run(report_dir, run_id, replay_start_result,
                      last_replay_start_event, trace, analyzer_stdout,
                      analyzer_code, summary, exit_code,
                      preflight_state_snapshot, preflight_state_trace,
                      preflight_state_status)


if __name__ == "__main__":
    raise SystemExit(main())
