from __future__ import annotations

import ctypes
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import psutil

from .artifacts import sha256_file


PROFILE_ARGUMENTS: dict[str, tuple[str, ...]] = {
    "latency": ("--lag", "ON", "--lag-time", "80"),
    "jitter": (
        "--lag", "ON", "--lag-time", "60", "--lag-jitter", "40",
    ),
    "loss": ("--drop", "ON", "--drop-chance", "5.0"),
    "burst_loss": (
        "--throttle", "ON", "--throttle-chance", "25.0",
        "--throttle-frame", "250", "--throttle-drop", "ON",
    ),
    "reorder": ("--ood", "ON", "--ood-chance", "10.0"),
    "duplicate": (
        "--duplicate", "ON", "--duplicate-chance", "5.0",
        "--duplicate-count", "1",
    ),
    "corruption": (
        "--tamper", "ON", "--tamper-chance", "100.0",
        "--tamper-checksum", "ON",
    ),
    "disconnect_pre": ("--drop", "ON", "--drop-chance", "100.0"),
    "disconnect_post": ("--drop", "ON", "--drop-chance", "100.0"),
}


def _is_administrator() -> bool:
    return bool(ctypes.windll.shell32.IsUserAnAdmin())


def _udp_ports(pids: tuple[int, ...]) -> tuple[int, ...]:
    ports: set[int] = set()
    for pid in pids:
        process = psutil.Process(pid)
        for connection in process.net_connections(kind="udp"):
            if connection.laddr and connection.laddr.port:
                ports.add(int(connection.laddr.port))
    if not ports:
        raise RuntimeError("SC6 processes own no UDP endpoints for scoped impairment")
    return tuple(sorted(ports))


def _port_filter(ports: tuple[int, ...]) -> str:
    clauses = " or ".join(
        f"udp.SrcPort == {port} or udp.DstPort == {port}" for port in ports
    )
    return f"udp and ({clauses})"


@dataclass
class ClumsyImpairment:
    executable: Path
    profile: str
    seed: int
    process: subprocess.Popen[bytes] | None = None
    evidence: dict[str, Any] | None = None

    def start(self, pids: tuple[int, ...]) -> dict[str, Any]:
        if self.profile == "clean":
            self.evidence = {"profile": "clean", "active": False}
            return self.evidence
        if self.profile not in PROFILE_ARGUMENTS:
            raise RuntimeError(f"unsupported impairment profile: {self.profile}")
        if not _is_administrator():
            raise RuntimeError(
                "non-clean impairment requires an administrator runner so "
                "WinDivert cannot escape process tracking through UAC relaunch"
            )
        executable = self.executable.resolve()
        if not executable.is_file():
            raise FileNotFoundError(f"impairment tool not found: {executable}")
        ports = _udp_ports(pids)
        packet_filter = _port_filter(ports)
        command = [
            str(executable), "--filter", packet_filter,
            "--seed", str(self.seed), *PROFILE_ARGUMENTS[self.profile],
        ]
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        self.process = subprocess.Popen(
            command, cwd=executable.parent, close_fds=True,
            startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW,
        )
        time.sleep(1.0)
        if self.process.poll() is not None:
            raise RuntimeError(
                f"impairment tool exited during activation: {self.process.returncode}"
            )
        self.evidence = {
            "profile": self.profile,
            "active": True,
            "tool": {"path": str(executable), "sha256": sha256_file(executable)},
            "seed": self.seed,
            "filter": packet_filter,
            "udp_ports": list(ports),
            "command": command,
            "pid": self.process.pid,
            "target_pids": list(pids),
        }
        return self.evidence

    def stop(self) -> dict[str, Any]:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=10)
        executable = self.executable.resolve()
        remaining = []
        for process in psutil.process_iter(("pid", "exe")):
            try:
                if process.info["exe"] and Path(process.info["exe"]).resolve() == executable:
                    remaining.append(process.info["pid"])
            except (OSError, psutil.Error):
                continue
        if remaining:
            raise RuntimeError(f"impairment processes remain after cleanup: {remaining}")
        cleanup = {
            "processes_remaining": 0,
            "rules_removed": True,
            "proof": "WinDivert handle owner exited; kernel filter handle closed",
        }
        if self.evidence is None:
            self.evidence = {"profile": self.profile, "active": False}
        self.evidence["cleanup"] = cleanup
        return cleanup
