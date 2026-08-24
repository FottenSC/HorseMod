from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path


DEFAULT_SANDBOXIE_START = Path(r"C:\Program Files\Sandboxie-Plus\Start.exe")
DEFAULT_STEAM_EXECUTABLE = Path(r"C:\Program Files (x86)\Steam\steam.exe")
DEFAULT_STEAM_APP_ID = "544750"
_BOX_NAME = re.compile(r"^[A-Za-z0-9_.-]{1,64}$")


@dataclass(frozen=True)
class SandboxiePairSpec:
    box_name: str
    sandboxie_start: Path = DEFAULT_SANDBOXIE_START
    steam_executable: Path = DEFAULT_STEAM_EXECUTABLE
    steam_app_id: str = DEFAULT_STEAM_APP_ID
    sandbox_query_port: int = 27012

    def validate(self) -> None:
        if not _BOX_NAME.fullmatch(self.box_name):
            raise ValueError("Sandboxie box name must be a simple 1-64 character name")
        if not self.steam_app_id.isdecimal() or int(self.steam_app_id) <= 0:
            raise ValueError("Steam app ID must be a positive decimal integer")
        if not 1 <= self.sandbox_query_port <= 65535:
            raise ValueError("sandbox query port must be in 1..65535")

    def host_command(self) -> tuple[str, ...]:
        self.validate()
        return (
            str(self.steam_executable),
            "-applaunch",
            self.steam_app_id,
        )

    def sandbox_command(self) -> tuple[str, ...]:
        self.validate()
        return (
            str(self.sandboxie_start),
            f"/box:{self.box_name}",
            str(self.steam_executable),
            "-applaunch",
            self.steam_app_id,
            f"-QueryPort={self.sandbox_query_port}",
        )


@dataclass(frozen=True)
class SandboxiePairProcesses:
    host_pid: int
    sandbox_pid: int


def parse_sandbox_pid_listing(output: str) -> tuple[int, ...]:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if not lines or not all(line.isdecimal() for line in lines):
        raise RuntimeError("sandbox process list is malformed")
    declared = int(lines[0])
    pids = tuple(int(line) for line in lines[1:])
    if declared != len(pids) or any(pid <= 0 for pid in pids):
        raise RuntimeError("sandbox process list is malformed")
    if len(set(pids)) != len(pids):
        raise RuntimeError("sandbox process list contains duplicate PIDs")
    return pids


def list_sandbox_pids(spec: SandboxiePairSpec) -> tuple[int, ...]:
    spec.validate()
    result = subprocess.run(
        [str(spec.sandboxie_start), f"/box:{spec.box_name}", "/listpids"],
        check=False,
        capture_output=True,
        text=True,
        timeout=10,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Sandboxie process listing failed with exit code {result.returncode}"
        )
    return parse_sandbox_pid_listing(result.stdout)


def classify_game_processes(
    game_pids: set[int], sandbox_pids: set[int]
) -> SandboxiePairProcesses:
    if any(pid <= 0 for pid in game_pids | sandbox_pids):
        raise ValueError("process IDs must be positive")
    sandbox_games = game_pids & sandbox_pids
    host_games = game_pids - sandbox_pids
    if len(host_games) != 1 or len(sandbox_games) != 1:
        raise RuntimeError(
            "qualification requires exactly one normal and one sandboxed SC6 process"
        )
    return SandboxiePairProcesses(
        host_pid=next(iter(host_games)),
        sandbox_pid=next(iter(sandbox_games)),
    )


def require_isolated_paths(
    host_root: Path,
    sandbox_root: Path,
    host_log: Path,
    sandbox_log: Path,
) -> None:
    normalized = {
        str(path.resolve(strict=False)).casefold()
        for path in (host_root, sandbox_root, host_log, sandbox_log)
    }
    if len(normalized) != 4:
        raise RuntimeError("host and Sandboxie writable roots/logs must be distinct")
