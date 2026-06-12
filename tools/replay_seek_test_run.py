#!/usr/bin/env python3
"""End-to-end runner for HorseMod replay seek tests.

This can build/deploy, write the deployed request file, wait for the in-game
harness to emit a summary, then run the analyzer. The game still needs to be
running and in a replay unless future replay-load automation is added.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any


REPO_ROOT = Path(r"E:\myMods")
BUILD_BAT = REPO_ROOT / "build_and_deploy.bat"
SAVED_DIR = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\ue4ss\Mods\HorseMod\Saved"
)
TRACE_DIR = SAVED_DIR / "ReplayTrace"
REQUEST_PATH = SAVED_DIR / "replay_seek_test_request.json"
ANALYZER = REPO_ROOT / "tools" / "replay_seek_test_analyze.py"
DEFAULT_GAME_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI"
    r"\Binaries\Win64\SoulcaliburVI.exe"
)
DEFAULT_STEAM_APPID = "544750"


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


def default_cases() -> list[dict[str, Any]]:
    return [
        {"label": "early", "percent": 0.10, "resume_frames": 120},
        {"label": "mid", "percent": 0.50, "resume_frames": 120},
        {"label": "late", "percent": 0.85, "resume_frames": 120},
    ]


def make_request(args: argparse.Namespace) -> dict[str, Any]:
    run_id = args.run_id or datetime.now().strftime("%Y%m%d-%H%M%S-seek")
    return {
        "enabled": True,
        "run_id": run_id,
        "generate_mode": args.generate_mode,
        "timeout_seconds": args.case_timeout,
        "cases": default_cases(),
    }


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


def event_name(event: dict[str, Any]) -> str:
    return str(event.get("event") or event.get("name") or "")


def find_run_summary(run_id: str, since: float) -> tuple[Path | None, dict[str, Any] | None]:
    if not TRACE_DIR.exists():
        return None, None
    traces = sorted(
        TRACE_DIR.glob("replay_trace_*.jsonl"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for path in traces[:8]:
        try:
            if path.stat().st_mtime + 5 < since:
                continue
        except OSError:
            continue
        for event in load_jsonl(path):
            if (
                event_name(event) == "replay_seek_test_summary"
                and event.get("run_id") == run_id
            ):
                return path, event
    return None, None


def latest_trace_for_run(run_id: str, since: float) -> Path | None:
    if not TRACE_DIR.exists():
        return None
    traces = sorted(
        TRACE_DIR.glob("replay_trace_*.jsonl"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for path in traces[:8]:
        try:
            if path.stat().st_mtime + 5 < since:
                continue
        except OSError:
            continue
        for event in load_jsonl(path):
            if event.get("run_id") == run_id:
                return path
    return None


def run_build() -> int:
    print(f"building: {BUILD_BAT}")
    completed = subprocess.run([str(BUILD_BAT)], cwd=str(REPO_ROOT))
    return completed.returncode


def launch_game(game_exe: Path, steam_appid: str) -> None:
    if game_exe.exists():
        print(f"launching game: {game_exe}")
        subprocess.Popen([str(game_exe)], cwd=str(game_exe.parent))
        return
    uri = f"steam://rungameid/{steam_appid}"
    print(f"launching game through Steam: {uri}")
    os.startfile(uri)  # type: ignore[attr-defined]


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


def run_analyzer(trace: Path, run_id: str) -> int:
    cmd = [sys.executable, str(ANALYZER), str(trace), "--run-id", run_id, "--require-tests"]
    print("analyzing:", " ".join(cmd))
    completed = subprocess.run(cmd, cwd=str(REPO_ROOT))
    return completed.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true", help="Run build_and_deploy.bat first")
    parser.add_argument("--launch-game", action="store_true", help="Launch SC6 before writing request")
    parser.add_argument("--game-exe", default=str(DEFAULT_GAME_EXE))
    parser.add_argument("--steam-appid", default=DEFAULT_STEAM_APPID)
    parser.add_argument("--window-title", default="SoulcaliburVI")
    parser.add_argument("--focus-timeout", type=int, default=90)
    parser.add_argument(
        "--menu-script",
        help="JSON key macro to navigate menus/start a replay before waiting",
    )
    parser.add_argument("--wait", action="store_true", help="Wait for replay_seek_test_summary")
    parser.add_argument("--analyze", action="store_true", help="Run analyzer after summary")
    parser.add_argument("--analyze-only", metavar="TRACE", help="Only analyze a trace")
    parser.add_argument("--run-id", help="Run id to write or analyze")
    parser.add_argument(
        "--generate-mode",
        default="battle_step",
        choices=["normal", "experimental", "battle_step"],
    )
    parser.add_argument("--case-timeout", type=int, default=600)
    parser.add_argument("--timeout", type=int, default=900, help="Wait timeout seconds")
    parser.add_argument("--request", help="Path to custom request JSON")
    args = parser.parse_args()

    if args.analyze_only:
        run_id = args.run_id or ""
        cmd = [sys.executable, str(ANALYZER), args.analyze_only]
        if run_id:
            cmd += ["--run-id", run_id]
        return subprocess.run(cmd, cwd=str(REPO_ROOT)).returncode

    if args.build:
        code = run_build()
        if code != 0:
            return code

    if args.launch_game:
        launch_game(Path(args.game_exe), args.steam_appid)
        focus_game_window(args.window_title, args.focus_timeout)

    if args.menu_script:
        if not focus_game_window(args.window_title, args.focus_timeout):
            return 4
        run_menu_script(Path(args.menu_script))

    request = json.loads(Path(args.request).read_text(encoding="utf-8")) if args.request else make_request(args)
    run_id = str(request.get("run_id") or args.run_id or datetime.now().strftime("%Y%m%d-%H%M%S-seek"))
    request["run_id"] = run_id
    request.setdefault("enabled", True)

    SAVED_DIR.mkdir(parents=True, exist_ok=True)
    REQUEST_PATH.write_text(json.dumps(request, indent=2) + "\n", encoding="utf-8")
    print(f"wrote request: {REQUEST_PATH}")
    print(f"run_id: {run_id}")
    print("game prerequisite: SC6 must be running with HorseMod loaded and in a replay")

    if not args.wait:
        return 0

    since = time.time()
    deadline = since + args.timeout
    trace: Path | None = None
    summary: dict[str, Any] | None = None
    while time.time() < deadline:
        trace, summary = find_run_summary(run_id, since)
        if summary is not None:
            break
        time.sleep(2.0)

    if summary is None:
        trace = latest_trace_for_run(run_id, since)
        print(f"timeout waiting for replay_seek_test_summary run_id={run_id}")
        if trace:
            print(f"partial trace: {trace}")
        return 3

    print(
        "summary observed: "
        f"trace={trace} passed={summary.get('passed')} "
        f"passed_cases={summary.get('passed_cases')} "
        f"failed_cases={summary.get('failed_cases')} "
        f"reason={summary.get('reason')}"
    )

    if args.analyze:
        if trace is None:
            return 3
        return run_analyzer(trace, run_id)
    return 0 if summary.get("passed") else 1


if __name__ == "__main__":
    raise SystemExit(main())
