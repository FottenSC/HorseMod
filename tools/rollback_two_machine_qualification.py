#!/usr/bin/env python3
"""Create, ingest, finalize, or validate trace-derived physical evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
from typing import Any

from rollback_physical_case_run import (
    CANDIDATE_SCHEMA_VERSION,
    REPORT_SCHEMA_VERSION,
    analyze_trace,
    candidate_bindings,
    load_json,
    load_matrix,
    sha256_file,
)


SCHEMA_VERSION = 5
HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")
STEAM_ENDPOINT = re.compile(r"^steam:([1-9][0-9]{0,19})$")


def _artifact(path: Path) -> dict[str, Any]:
    return {"path": path.name, "bytes": path.stat().st_size,
            "sha256": sha256_file(path)}


def _safe_name(value: str) -> str:
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", value) is None:
        raise ValueError(f"unsafe evidence identifier: {value!r}")
    return value


def _steam_endpoint_id(value: object) -> int:
    match = STEAM_ENDPOINT.fullmatch(str(value))
    if match is None:
        raise ValueError("endpoint must use canonical steam:<ID64> form")
    result = int(match.group(1), 10)
    if result <= 0 or result > 0xFFFFFFFFFFFFFFFF:
        raise ValueError("Steam endpoint is outside ID64 range")
    return result


def _trace_identity_int(value: object) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError("trace identity is not an integer")


def _load_candidate_paths(candidate_path: Path) -> tuple[Path, Path, Path]:
    candidate = load_json(candidate_path, "candidate manifest")
    if candidate.get("schema_version") != CANDIDATE_SCHEMA_VERSION:
        raise ValueError("candidate manifest schema is unsupported")
    contract = candidate.get("qualification_contract")
    if not isinstance(contract, dict):
        raise ValueError("candidate qualification contract is missing")
    try:
        dll = Path(candidate["dll"]["path"]).resolve()
        profile = Path(contract["beta_config_profile"]["path"]).resolve()
        matrix = Path(contract["physical_case_matrix"]["path"]).resolve()
    except (KeyError, TypeError) as exc:
        raise ValueError("candidate paths are incomplete") from exc
    if not dll.is_file() or not profile.is_file() or not matrix.is_file():
        raise ValueError("candidate DLL/profile/matrix path is unavailable")
    return dll, profile, matrix


def new_manifest(candidate_path: Path, host_machine: str,
                 guest_machine: str, host_endpoint: str,
                 guest_endpoint: str) -> dict[str, Any]:
    host_steam_id = _steam_endpoint_id(host_endpoint)
    guest_steam_id = _steam_endpoint_id(guest_endpoint)
    if host_machine == guest_machine or host_steam_id == guest_steam_id:
        raise ValueError("physical machines and endpoints must be distinct")
    dll, profile, matrix_path = _load_candidate_paths(candidate_path)
    matrix = load_matrix(matrix_path)
    bindings = candidate_bindings(
        candidate_path, dll, profile, matrix_path)
    runner = Path(__file__).resolve()
    analyzer = runner.with_name("rollback_physical_case_run.py")
    return {
        "schema_version": SCHEMA_VERSION,
        "classification": "physical-two-machine-release-qualification",
        "status": "pending",
        "candidate": {
            "path": str(candidate_path),
            "sha256": sha256_file(candidate_path),
            **bindings,
        },
        "tooling": {
            "manifest_runner_sha256": sha256_file(runner),
            "physical_runner_analyzer_sha256": sha256_file(analyzer),
            "matrix_sha256": sha256_file(matrix_path),
        },
        "machines": {
            "host": {"id": host_machine, "endpoint": host_endpoint,
                     "identity_authority": "steam-id64-trace-bound",
                     "machine_claim": "operator-attested",
                     "profile_sha256": bindings["beta_profile_sha256"]},
            "guest": {"id": guest_machine, "endpoint": guest_endpoint,
                      "identity_authority": "steam-id64-trace-bound",
                      "machine_claim": "operator-attested",
                      "profile_sha256": bindings["beta_profile_sha256"]},
        },
        "cases": {case_id: {"status": "pending", "segments": {}}
                  for case_id in matrix["cases"]},
    }


def _report_expected(report: dict[str, Any], policy: dict[str, Any],
                     bindings: dict[str, Any]) -> dict[str, Any]:
    qualification = report.get("qualification")
    if not isinstance(qualification, dict):
        raise ValueError("segment report qualification binding is missing")
    return {
        "case_id": qualification.get("case_id"),
        "segment_id": qualification.get("segment_id"),
        "role": qualification.get("role"),
        "run_id": qualification.get("run_id"),
        "schedule_hash": qualification.get("schedule_hash"),
        "seed": qualification.get("seed"),
        "policy": policy,
        "bindings": bindings,
    }


def _pair_failures(host: dict[str, Any], guest: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    hq, gq = host.get("qualification", {}), guest.get("qualification", {})
    for key in ("run_id", "case_id", "segment_id", "seed",
                "schedule_hash", "runtime_profile"):
        if hq.get(key) != gq.get(key):
            failures.append(f"pair-qualification-{key}")
    if hq.get("role") != "host" or gq.get("role") != "guest":
        failures.append("pair-role")
    hi, gi = host.get("session_identity", {}), guest.get("session_identity", {})
    for key in ("steam_lobby_id", "session_contract_hash",
                "steam_identity_accepted_selection_hash",
                "launch_stage_identity", "session_epoch",
                "steam_owner_id"):
        if not hi.get(key) or hi.get(key) != gi.get(key):
            failures.append(f"pair-session-{key}")
    if hi.get("steam_local_id") != gi.get("steam_remote_id") \
            or hi.get("steam_remote_id") != gi.get("steam_local_id"):
        failures.append("pair-inverse-steam-identities")
    if hi.get("local_player_slot") not in (0, 1) \
            or gi.get("local_player_slot") not in (0, 1) \
            or hi.get("local_player_slot") == gi.get("local_player_slot"):
        failures.append("pair-inverse-player-slots")
    return failures


def _machine_endpoint_failures(manifest: dict[str, Any],
                               host: dict[str, Any],
                               guest: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    machines = manifest.get("machines", {})
    try:
        expected_host = _steam_endpoint_id(
            machines.get("host", {}).get("endpoint"))
        expected_guest = _steam_endpoint_id(
            machines.get("guest", {}).get("endpoint"))
        actual_host = _trace_identity_int(
            host.get("session_identity", {}).get("steam_local_id"))
        actual_guest = _trace_identity_int(
            guest.get("session_identity", {}).get("steam_local_id"))
        if actual_host != expected_host:
            failures.append("host-steam-endpoint-binding")
        if actual_guest != expected_guest:
            failures.append("guest-steam-endpoint-binding")
    except (TypeError, ValueError):
        failures.append("machine-steam-endpoint-invalid")
    return failures


def _candidate_context(manifest: dict[str, Any]) -> tuple[dict[str, Any],
                                                            dict[str, Any],
                                                            Path, Path, Path]:
    candidate_value = manifest.get("candidate")
    if not isinstance(candidate_value, dict):
        raise ValueError("qualification candidate binding is missing")
    candidate_path = Path(str(candidate_value.get("path", ""))).resolve()
    if not candidate_path.is_file() \
            or sha256_file(candidate_path) != candidate_value.get("sha256"):
        raise ValueError("qualification candidate manifest changed")
    dll, profile, matrix_path = _load_candidate_paths(candidate_path)
    matrix = load_matrix(matrix_path)
    bindings = candidate_bindings(candidate_path, dll, profile, matrix_path)
    for key, value in bindings.items():
        if candidate_value.get(key) != value:
            raise ValueError(f"qualification candidate binding changed: {key}")
    return bindings, matrix, dll, profile, matrix_path


def record_segment(manifest_path: Path, host_trace: Path, guest_trace: Path,
                   host_report_path: Path, guest_report_path: Path) -> None:
    manifest = load_json(manifest_path, "physical manifest")
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise ValueError("physical manifest schema is unsupported")
    bindings, matrix, _, _, _ = _candidate_context(manifest)
    host_report = load_json(host_report_path, "host segment report")
    guest_report = load_json(guest_report_path, "guest segment report")
    if host_report.get("schema_version") != REPORT_SCHEMA_VERSION \
            or guest_report.get("schema_version") != REPORT_SCHEMA_VERSION:
        raise ValueError("old or unsupported per-machine report schema")
    case_id = str(host_report.get("qualification", {}).get("case_id", ""))
    segment_id = str(host_report.get("qualification", {}).get("segment_id", ""))
    policy = matrix["cases"].get(case_id)
    if not isinstance(policy, dict) or segment_id not in policy["segments"]:
        raise ValueError("report case/segment is outside candidate matrix")
    host_expected = _report_expected(host_report, policy, bindings)
    guest_expected = _report_expected(guest_report, policy, bindings)
    if host_expected["role"] != "host" or guest_expected["role"] != "guest":
        raise ValueError("segment reports do not have host/guest roles")
    recomputed_host = analyze_trace(host_trace, **host_expected)
    recomputed_guest = analyze_trace(guest_trace, **guest_expected)
    if host_report != recomputed_host or guest_report != recomputed_guest:
        raise ValueError("segment report is not exactly reproduced by trace")
    pair_failures = (_pair_failures(host_report, guest_report)
                     + _machine_endpoint_failures(
                         manifest, host_report, guest_report))
    if not host_report.get("ok") or not guest_report.get("ok") or pair_failures:
        raise ValueError("segment evidence failed: " + ",".join(
            host_report.get("failures", []) + guest_report.get("failures", [])
            + pair_failures))
    case_record = manifest["cases"][case_id]
    if segment_id in case_record["segments"]:
        raise ValueError("qualification segment is already recorded")
    evidence_dir = manifest_path.parent / "qualification-evidence"
    evidence_dir.mkdir(parents=True, exist_ok=True)
    prefix = f"{_safe_name(case_id)}--{_safe_name(segment_id)}"
    sources = {
        "host_trace": host_trace, "guest_trace": guest_trace,
        "host_report": host_report_path, "guest_report": guest_report_path,
    }
    copied: dict[str, dict[str, Any]] = {}
    for label, source in sources.items():
        extension = ".jsonl" if label.endswith("trace") else ".json"
        target = evidence_dir / f"{prefix}--{label}{extension}"
        if target.exists():
            raise ValueError(f"evidence target already exists: {target}")
        shutil.copy2(source, target)
        identity = _artifact(target)
        identity["path"] = target.relative_to(manifest_path.parent).as_posix()
        copied[label] = identity
    case_record["segments"][segment_id] = {
        "status": "passed", "run_id": host_expected["run_id"],
        "seed": host_expected["seed"],
        "schedule_hash": host_expected["schedule_hash"],
        "runtime_profile": policy["runtime_profile"], "artifacts": copied,
    }
    case_record["status"] = "pending"
    manifest["status"] = "pending"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")


def _segment_failures(manifest: dict[str, Any], matrix: dict[str, Any],
                      bindings: dict[str, Any], base: Path,
                      case_id: str, segment_id: str,
                      segment: object) -> list[str]:
    prefix = f"{case_id}:{segment_id}"
    if not isinstance(segment, dict):
        return [f"{prefix}:not-object"]
    artifacts = segment.get("artifacts")
    if not isinstance(artifacts, dict) or set(artifacts) != {
            "host_trace", "guest_trace", "host_report", "guest_report"}:
        return [f"{prefix}:artifact-set"]
    resolved: dict[str, Path] = {}
    seen: set[Path] = set()
    failures: list[str] = []
    for label, identity in artifacts.items():
        if not isinstance(identity, dict):
            failures.append(f"{prefix}:{label}-identity")
            continue
        path = (base / str(identity.get("path", ""))).resolve()
        try:
            path.relative_to(base.resolve())
        except ValueError:
            failures.append(f"{prefix}:{label}-outside-bundle")
            continue
        if path in seen:
            failures.append(f"{prefix}:duplicate-artifact-path")
        seen.add(path)
        if not path.is_file() or path.stat().st_size != identity.get("bytes") \
                or sha256_file(path) != identity.get("sha256"):
            failures.append(f"{prefix}:{label}-changed")
        resolved[label] = path
    if failures:
        return failures
    try:
        host = load_json(resolved["host_report"], "host segment report")
        guest = load_json(resolved["guest_report"], "guest segment report")
        policy = matrix["cases"][case_id]
        host_expected = _report_expected(host, policy, bindings)
        guest_expected = _report_expected(guest, policy, bindings)
        if host != analyze_trace(resolved["host_trace"], **host_expected):
            failures.append(f"{prefix}:host-not-reproduced")
        if guest != analyze_trace(resolved["guest_trace"], **guest_expected):
            failures.append(f"{prefix}:guest-not-reproduced")
        failures.extend(f"{prefix}:{item}" for item in _pair_failures(host, guest))
        failures.extend(f"{prefix}:{item}" for item in
                        _machine_endpoint_failures(manifest, host, guest))
        if not host.get("ok") or not guest.get("ok"):
            failures.append(f"{prefix}:machine-report-failed")
    except (OSError, UnicodeError, ValueError, KeyError, json.JSONDecodeError) as exc:
        failures.append(f"{prefix}:revalidation:{exc}")
    return failures


def case_failures(manifest: dict[str, Any], matrix: dict[str, Any],
                  bindings: dict[str, Any], base: Path,
                  case_id: str) -> list[str]:
    record = manifest.get("cases", {}).get(case_id)
    if not isinstance(record, dict):
        return [f"{case_id}:missing"]
    required = matrix["cases"][case_id]["segments"]
    segments = record.get("segments")
    if not isinstance(segments, dict) or set(segments) != set(required):
        return [f"{case_id}:segment-policy"]
    failures: list[str] = []
    for segment_id in required:
        failures.extend(_segment_failures(
            manifest, matrix, bindings, base, case_id, segment_id,
            segments[segment_id]))
    if case_id == "disconnect-reconnect" and not failures:
        first = segments["fail-closed"]["artifacts"]["host_trace"]["sha256"]
        second = segments["clean-lobby-recovery"]["artifacts"]["host_trace"]["sha256"]
        if first == second:
            failures.append(f"{case_id}:recovery-trace-not-distinct")
        try:
            identities = []
            for recovery_segment in ("fail-closed", "clean-lobby-recovery"):
                report_path = base / segments[recovery_segment]["artifacts"] \
                    ["host_report"]["path"]
                report = load_json(report_path, "disconnect segment report")
                identity = report["session_identity"]
                identities.append(tuple(_trace_identity_int(identity[key])
                                        for key in ("steam_lobby_id",
                                                    "session_contract_hash",
                                                    "session_epoch")))
            if any(value == 0 for identity in identities for value in identity):
                failures.append(f"{case_id}:recovery-session-identity-zero")
            if any(identities[0][index] == identities[1][index]
                   for index in range(3)):
                failures.append(f"{case_id}:recovery-session-not-distinct")
        except (KeyError, TypeError, ValueError, OSError) as exc:
            failures.append(f"{case_id}:recovery-session-identity:{exc}")
    if case_id == "representative-content-matrix" and not failures:
        try:
            candidate_path = Path(manifest["candidate"]["path"])
            candidate = load_json(candidate_path, "candidate manifest")
            contract = candidate["qualification_contract"]
            golden_path = Path(contract["trusted_golden_manifest"]["path"])
            golden = load_json(golden_path, "trusted golden manifest")
            expected = {
                str(case["id"]): str(case["replay_sha256"]).lower()
                for case in golden["cases"]
            }
            golden_hashes = set(expected.values())
            remaining = sorted(
                str(item["sha256"]).lower()
                for item in contract["replay_corpus"]["replays"]
                if str(item["sha256"]).lower() not in golden_hashes)
            if not remaining:
                failures.append(f"{case_id}:remaining-corpus-empty")
            else:
                expected["lowest-sha-remaining-corpus"] = remaining[0]
            for content_segment, expected_hash in expected.items():
                report_identity = segments[content_segment]["artifacts"] \
                    ["host_report"]
                report_path = base / report_identity["path"]
                report = load_json(report_path, "content segment report")
                if str(report.get("terminal", {}).get(
                        "content_sha256") or "").lower() != expected_hash:
                    failures.append(
                        f"{case_id}:{content_segment}:content-sha256")
        except (OSError, UnicodeError, ValueError, KeyError, TypeError) as exc:
            failures.append(f"{case_id}:content-policy:{exc}")
    return failures


def finalize_case(manifest_path: Path, case_id: str) -> None:
    manifest = load_json(manifest_path, "physical manifest")
    bindings, matrix, _, _, _ = _candidate_context(manifest)
    if case_id not in matrix["cases"]:
        raise ValueError("unknown physical case")
    failures = case_failures(
        manifest, matrix, bindings, manifest_path.parent, case_id)
    manifest["cases"][case_id]["status"] = "failed" if failures else "passed"
    manifest["cases"][case_id]["failures"] = failures
    manifest["status"] = "pending"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")
    if failures:
        raise ValueError("case failed: " + ",".join(failures))


def validation_failures(manifest: object, base: Path) -> list[str]:
    if not isinstance(manifest, dict):
        return ["manifest-not-object"]
    if manifest.get("schema_version") != SCHEMA_VERSION:
        return ["manifest-schema"]
    if manifest.get("classification") \
            != "physical-two-machine-release-qualification":
        return ["manifest-classification"]
    try:
        bindings, matrix, _, _, matrix_path = _candidate_context(manifest)
    except (OSError, UnicodeError, ValueError, KeyError) as exc:
        return [f"candidate:{exc}"]
    failures: list[str] = []
    tooling = manifest.get("tooling", {})
    expected_tooling = {
        "manifest_runner_sha256": sha256_file(Path(__file__).resolve()),
        "physical_runner_analyzer_sha256": sha256_file(
            Path(__file__).with_name("rollback_physical_case_run.py")),
        "matrix_sha256": sha256_file(matrix_path),
    }
    if tooling != expected_tooling:
        failures.append("tooling-binding")
    machines = manifest.get("machines", {})
    expected_profile = bindings["beta_profile_sha256"]
    if not isinstance(machines, dict) \
            or machines.get("host", {}).get("profile_sha256") != expected_profile \
            or machines.get("guest", {}).get("profile_sha256") != expected_profile:
        failures.append("machine-profile-binding")
    try:
        host_machine = machines["host"]
        guest_machine = machines["guest"]
        if host_machine.get("id") == guest_machine.get("id") \
                or _steam_endpoint_id(host_machine.get("endpoint")) \
                == _steam_endpoint_id(guest_machine.get("endpoint")) \
                or host_machine.get("identity_authority") \
                != "steam-id64-trace-bound" \
                or guest_machine.get("identity_authority") \
                != "steam-id64-trace-bound" \
                or host_machine.get("machine_claim") != "operator-attested" \
                or guest_machine.get("machine_claim") != "operator-attested":
            failures.append("machine-identity-policy")
    except (KeyError, TypeError, ValueError):
        failures.append("machine-identity-policy")
    cases = manifest.get("cases")
    if not isinstance(cases, dict) or set(cases) != set(matrix["cases"]):
        failures.append("case-inventory")
        return failures
    for case_id in matrix["cases"]:
        failures.extend(case_failures(
            manifest, matrix, bindings, base, case_id))
        if cases[case_id].get("status") != "passed":
            failures.append(f"{case_id}:not-finalized")
    return failures


def selftest() -> int:
    # The analyzer owns the synthetic trace controls; this layer verifies that
    # old schemas and malformed manifests fail closed without local evidence.
    old = {"schema_version": 4,
           "classification": "physical-two-machine-release-qualification"}
    old_rejected = validation_failures(old, Path.cwd()) == ["manifest-schema"]
    malformed_rejected = bool(validation_failures({}, Path.cwd()))
    print("rollback two-machine qualification self-test "
          f"old_schema_rejected={int(old_rejected)} "
          f"malformed_rejected={int(malformed_rejected)}")
    return 0 if old_rejected and malformed_rejected else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--initialize", type=Path)
    parser.add_argument("--candidate-manifest", type=Path)
    parser.add_argument("--host-machine")
    parser.add_argument("--guest-machine")
    parser.add_argument("--host-endpoint")
    parser.add_argument("--guest-endpoint")
    parser.add_argument("--record-segment", type=Path, metavar="MANIFEST")
    parser.add_argument("--finalize-case", type=Path, metavar="MANIFEST")
    parser.add_argument("--case")
    parser.add_argument("--host-trace", type=Path)
    parser.add_argument("--guest-trace", type=Path)
    parser.add_argument("--host-report", type=Path)
    parser.add_argument("--guest-report", type=Path)
    parser.add_argument("--validate", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.selftest:
            return selftest()
        if args.initialize:
            required = (args.candidate_manifest, args.host_machine,
                        args.guest_machine, args.host_endpoint,
                        args.guest_endpoint)
            if any(value is None for value in required):
                raise ValueError("initialize requires candidate manifest, two "
                                 "machine IDs, and two endpoints")
            manifest = new_manifest(
                args.candidate_manifest.resolve(), args.host_machine,
                args.guest_machine, args.host_endpoint, args.guest_endpoint)
            args.initialize.parent.mkdir(parents=True, exist_ok=True)
            args.initialize.write_text(json.dumps(manifest, indent=2) + "\n",
                                       encoding="utf-8")
            print(f"initialized physical qualification: {args.initialize}")
            return 0
        if args.record_segment:
            required = (args.host_trace, args.guest_trace,
                        args.host_report, args.guest_report)
            if any(value is None for value in required):
                raise ValueError("record-segment requires paired traces/reports")
            record_segment(args.record_segment.resolve(),
                           args.host_trace.resolve(), args.guest_trace.resolve(),
                           args.host_report.resolve(), args.guest_report.resolve())
            print("recorded paired trace-derived segment")
            return 0
        if args.finalize_case:
            if not args.case:
                raise ValueError("finalize-case requires --case")
            finalize_case(args.finalize_case.resolve(), args.case)
            print(f"finalized physical case: {args.case}")
            return 0
        if args.validate:
            manifest = load_json(args.validate.resolve(), "physical manifest")
            failures = validation_failures(manifest, args.validate.resolve().parent)
            if failures:
                print("physical qualification failed: " + ",".join(failures))
                return 1
            print("physical qualification passed")
            return 0
    except (OSError, UnicodeError, ValueError, KeyError,
            json.JSONDecodeError) as exc:
        print(f"physical qualification failed: {exc}")
        return 1
    print("choose --initialize, --record-segment, --finalize-case, "
          "--validate, or --selftest")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
