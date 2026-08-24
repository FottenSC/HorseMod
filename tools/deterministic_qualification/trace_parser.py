from __future__ import annotations

import re
import time
from dataclasses import dataclass
from pathlib import Path


SOURCE_PATTERN = re.compile(r"\[HorseMod\] ctor v(?P<version>\S+) source=(?P<commit>[0-9a-f]{40})")
HOOK_ARMED_TEXT = "[HorseMod] frame-fencepost runtime proof armed"
HOOK_FAILED_TEXT = "[HorseMod] frame-fencepost runtime proof unavailable"


@dataclass(frozen=True)
class BootEvidence:
    version: str
    source_commit: str
    hook_armed_line: str


def parse_boot_evidence(text: str) -> BootEvidence | None:
    source_matches = list(SOURCE_PATTERN.finditer(text))
    if not source_matches:
        return None
    source = source_matches[-1]
    current_boot = text[source.start():]
    armed_lines = [line for line in current_boot.splitlines() if HOOK_ARMED_TEXT in line]
    if not armed_lines or HOOK_FAILED_TEXT in current_boot:
        return None
    return BootEvidence(
        version=source.group("version"),
        source_commit=source.group("commit"),
        hook_armed_line=armed_lines[-1],
    )


def wait_for_boot_evidence(log_path: Path, timeout_seconds: float) -> BootEvidence:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            evidence = parse_boot_evidence(log_path.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            evidence = None
        if evidence is not None:
            return evidence
        time.sleep(0.5)
    raise TimeoutError("HorseMod source identity and hook-arm evidence did not appear")
