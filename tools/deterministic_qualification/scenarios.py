"""Versioned online scenario inventory shared by planning and evidence readers.

Legacy executor switches are an implementation detail of argv(), not evidence.
Resolved records bind content, lifecycle boundary and stimulus explicitly.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any

NATIVE_FAILURES = (
    "preownership_mismatch", "preownership_timeout", "postownership_auth",
    "postownership_hash", "postownership_restore", "postownership_peer",
)
WIRE_PROFILES = (
    "latency", "jitter", "loss", "burst_loss", "reorder", "duplicate",
    "corruption", "disconnect_pre", "disconnect_post", "combined",
)


@dataclass(frozen=True)
class Scenario:
    id: str
    version: int = 2
    required_combat_round: int = 2
    lifecycle_stage: str = "round-replacement"
    profile: str = "clean"
    failure: str = ""
    cycles: int = 1
    correction_depths: tuple[int, ...] = (7, 12, 1)
    prerequisites: tuple[str, ...] = ("certificate-interop", "native-python-tests")
    observations: tuple[str, ...] = (
        "canonical-agreement", "observed-correction-depth", "correction-local-presentation",
        "owned-storage-return",
    )
    setup_seconds: float = 300
    progress_seconds: float = 10
    cleanup_seconds: float = 30
    cleanup: tuple[str, ...] = ("requests", "watchers", "impairment", "processes", "deployment")
    dependencies: tuple[str, ...] = (
        "runtime", "capture_bridge", "capture_harness_sha256", "stimulus_spec_sha256", "schema_sha256",
    )

    def resolve(self, case_id: str) -> dict[str, Any]:
        # JSON-compatible immutable-by-value record used in reports/journals.
        import json
        return json.loads(json.dumps({**asdict(self), "case_id": case_id}))

    def argv(self) -> list[str]:
        arguments = ["--scenario", self.id, "--match-cycles", str(self.cycles),
                     "--launch-timeout", str(self.setup_seconds),
                     "--match-timeout", str(self.setup_seconds),
                     "--phase-timeout", str(self.progress_seconds)]
        if self.failure:
            arguments += ["--development-failure-smoke", "--failure-case", self.failure]
        elif self.profile != "clean":
            arguments += ["--development-profile-smoke", "--impairment-profile", self.profile]
        else:
            arguments += ["--development-multiround-correction-smoke"]
        return arguments


SCENARIOS = {value.id: value for value in (
    Scenario("online-round2-reentry", cycles=2),
    *(Scenario("native-" + failure, failure=failure,
        required_combat_round=1 if failure == "postownership_restore" else 0,
        lifecycle_stage="pre-ownership" if failure.startswith("pre") else "owned",
        correction_depths=(12,) if failure == "postownership_restore" else (),
        observations=("expected-native-failure", "failure-disposition", "owned-storage-return"))
      for failure in NATIVE_FAILURES),
    *(Scenario("wire-" + profile, profile=profile,
        required_combat_round=0 if profile.startswith("disconnect") else 2,
        lifecycle_stage=("pre-ownership" if profile == "disconnect_pre" else "round-replacement"),
        correction_depths=() if profile.startswith("disconnect") else (7, 12, 1),
        prerequisites=("certificate-interop", "native-python-tests", "controlled-traffic-effect"),
        observations=("authenticated-gameplay-effect", "canonical-or-bounded-failure", "owned-storage-return"),
        dependencies=("runtime", "capture_bridge", "capture_harness_sha256", "stimulus_spec_sha256",
                      "schema_sha256", "external_impairment_tool"))
      for profile in WIRE_PROFILES),
)}


def online_inventory(case_ids: list[str]) -> list[dict[str, Any]]:
    if len(case_ids) != 3 or len(set(case_ids)) != 3:
        raise ValueError("limited beta requires the exact three distinct content cases")
    return [SCENARIOS["online-round2-reentry"].resolve(case) for case in case_ids] + [
        scenario.resolve(case_ids[0]) for scenario in SCENARIOS.values()
        if scenario.id != "online-round2-reentry"
    ] + [SCENARIOS["wire-combined"].resolve(case) for case in case_ids[1:]]


def validate_scenario(args: Any) -> dict[str, Any] | None:
    identifier = getattr(args, "scenario", None)
    if identifier is None:
        return None  # Legacy diagnostic invocation cannot claim inventory coverage.
    scenario = SCENARIOS[identifier]
    if (args.match_cycles != scenario.cycles or args.impairment_profile != scenario.profile
            or args.failure_case != scenario.failure):
        raise ValueError(f"arguments disagree with scenario {identifier}")
    return scenario.resolve(args.case)
