from __future__ import annotations

import time
from dataclasses import dataclass, field

import psutil


def private_bytes(pid: int) -> int:
    memory = psutil.Process(pid).memory_full_info()
    value = getattr(memory, "private", None)
    if value is None:
        value = getattr(memory, "private_bytes", None)
    if value is None:
        raise RuntimeError("process private-byte accounting is unavailable")
    return int(value)


@dataclass
class PrivateMemoryTracker:
    pids: dict[str, int]
    warmup_seconds: float
    started_at: float = field(default_factory=time.monotonic)
    initial: dict[str, int] = field(default_factory=dict)
    baseline: dict[str, int] = field(default_factory=dict)
    maximum_after_warmup: dict[str, int] = field(default_factory=dict)
    latest: dict[str, int] = field(default_factory=dict)
    cycle_initial: dict[str, int] = field(default_factory=dict)
    cycle_maximum: dict[str, int] = field(default_factory=dict)
    _last_sample_at: float = 0.0

    def sample(self, *, force: bool = False) -> None:
        now = time.monotonic()
        if (not force and self._last_sample_at
                and now - self._last_sample_at < 1.0):
            return
        self._last_sample_at = now
        elapsed = now - self.started_at
        for label, pid in self.pids.items():
            value = private_bytes(pid)
            self.latest[label] = value
            self.initial.setdefault(label, value)
            if self.cycle_initial:
                self.cycle_maximum[label] = max(
                    self.cycle_maximum.get(label, value), value)
            if elapsed >= self.warmup_seconds:
                self.baseline.setdefault(label, value)
                self.maximum_after_warmup[label] = max(
                    self.maximum_after_warmup.get(label, value), value)

    def begin_cycle(self) -> None:
        self.sample(force=True)
        self.cycle_initial = dict(self.latest)
        self.cycle_maximum = dict(self.latest)

    def end_cycle(self) -> dict[str, dict[str, int]]:
        if not self.cycle_initial:
            raise RuntimeError("private-memory cycle was not started")
        self.sample(force=True)
        report = {
            "initial_private_bytes": dict(self.cycle_initial),
            "maximum_private_bytes": dict(self.cycle_maximum),
            "ending_private_bytes": dict(self.latest),
            "growth_bytes": {
                label: self.latest[label] - initial
                for label, initial in self.cycle_initial.items()
                if label in self.latest
            },
        }
        self.cycle_initial.clear()
        self.cycle_maximum.clear()
        return report

    def restart_warmup(self) -> None:
        self.started_at = time.monotonic()
        self._last_sample_at = 0.0
        self.baseline.clear()
        self.maximum_after_warmup.clear()

    def report(self) -> dict[str, object]:
        return {
            "warmup_seconds": self.warmup_seconds,
            "initial_private_bytes": self.initial,
            "baseline_private_bytes": self.baseline,
            "maximum_private_bytes_after_warmup": self.maximum_after_warmup,
            "ending_private_bytes": self.latest,
            "ending_growth_bytes": {
                label: self.latest[label] - baseline
                for label, baseline in self.baseline.items()
                if label in self.latest
            },
        }
