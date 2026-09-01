from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

from .artifacts import (
    capture_harness_sha256, offline_evaluator_sha256,
    require_compiled_candidate_manifest, runner_sha256, sha256_file,
    source_identity, source_identity_sha256,
)
from .offline_matrix import evaluate_matrix, load_candidate_cases
from .configuration import contract_sha256, expected_fields, is_exact_contract


REQUIRED_PROFILES = {
    "clean", "latency", "jitter", "loss", "burst_loss", "reorder",
    "duplicate", "corruption", "disconnect_pre", "disconnect_post",
}
REQUIRED_FAILURE_CASES = {
    "preownership_mismatch", "preownership_timeout", "preownership_disconnect",
    "postownership_auth", "postownership_hash", "postownership_restore",
    "postownership_peer", "postownership_disconnect",
}


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"evidence is not a JSON object: {path}")
    return value


def _require(condition: bool, reason: str) -> None:
    if not condition:
        raise RuntimeError(reason)


def _common_report(report: dict[str, Any], case: dict[str, Any],
                   dll_hash: str, schema_hash: str,
                   expected: dict[str, Any] | None = None) -> None:
    _require(report.get("report_schema") == 2, "release evidence is not schema v2")
    _require(report.get("certifying") is True and report.get("result") == "pass",
             "release evidence is not a certifying pass")
    _require(report.get("case_id") == case["case_id"], "release case mismatch")
    _require(report.get("display_map_name") == case["native_display_name"],
             "release display-map mismatch")
    _require(report.get("stage_package_root") == case["stage_package_root"],
             "release authored-package mismatch")
    _require(report.get("fighter_order", case["fighter_order"])
             == case["fighter_order"], "release fighter order mismatch")
    artifacts = report.get("artifacts", {})
    identities = report.get("identities", {})
    observed_dll = artifacts.get("horsemod_dll_sha256", identities.get("horsemod_dll_sha256"))
    observed_schema = artifacts.get("schema_sha256", identities.get("generated_schema_sha256"))
    _require(observed_dll == dll_hash, "release DLL hash mismatch")
    _require(observed_schema == schema_hash, "release schema hash mismatch")
    if expected is not None:
        observed_source = report.get("source", identities.get("source"))
        observed_capture_harness = artifacts.get("capture_harness_sha256")
        if observed_capture_harness is not None:
            # Replay capture provenance remains immutable even if only the
            # offline policy/evaluator layer is repaired afterward.
            _require(isinstance(observed_source, dict),
                     "release capture source provenance missing")
            _require(observed_capture_harness == expected["capture_harness"],
                     "release capture harness hash mismatch")
        else:
            _require(observed_source == expected["source"],
                     "release source identity mismatch")
            _require((artifacts.get("runner_sha256")
                      or identities.get("runner_sha256")) == expected["runner"],
                     "release runner hash mismatch")
        _require((artifacts.get("game_executable", {}).get("sha256")
                  or identities.get("game_executable_sha256"))
                 == expected["executable"], "release executable hash mismatch")
        _require((artifacts.get("replay_qualification_mod", {}).get("sha256")
                  or identities.get("bridge_sha256")) == expected["replay_mod"],
                 "release replay bridge hash mismatch")
        config_identity = (artifacts.get("config", {}).get("sha256")
                           or identities.get("config_sha256"))
        if isinstance(config_identity, dict):
            _require(set(config_identity) == {"host", "sandbox"}
                     and len(set(config_identity.values())) == 1
                     and all(isinstance(value, str) and len(value) == 64
                             for value in config_identity.values()),
                     "release paired config hash binding missing")
            config_fields = identities.get("config_fields")
            online = expected_fields(enabled=False, trace=True)
            _require(isinstance(config_fields, dict)
                     and set(config_fields) == {"host", "sandbox"}
                     and all(is_exact_contract(value, online)
                             for value in config_fields.values()),
                     "release paired config contract mismatch")
            _require(set(config_identity.values()) == {contract_sha256(online)},
                     "release paired config byte contract mismatch")
        else:
            _require(isinstance(config_identity, str) and len(config_identity) == 64,
                     "release config hash binding missing")


def _strict_seek_gate(report: dict[str, Any], case: dict[str, Any],
                      dll_hash: str, schema_hash: str,
                      expected: dict[str, Any] | None = None) -> None:
    _common_report(report, case, dll_hash, schema_hash, expected)
    _require(report.get("artifacts", {}).get("replay", {}).get("sha256")
             == case["replay_sha256"], "strict seek replay hash mismatch")
    runtime = report.get("runtime", {})
    metadata = runtime.get("replay_metadata", {})
    _require(metadata.get("stage") == case["replay_metadata_stage"]
             and metadata.get("map") == case["replay_metadata_map"]
             and (metadata.get("left_character"), metadata.get("right_character"))
                == tuple(case["replay_metadata_fighters"]),
             "strict seek native replay metadata mismatch")
    expected_config = expected_fields(enabled=False, trace=True)
    _require(is_exact_contract(
        report.get("artifacts", {}).get("config_fields"), expected_config),
        "strict seek config contract mismatch")
    _require(report.get("artifacts", {}).get("config", {}).get("sha256")
             == contract_sha256(expected_config),
             "strict seek config byte contract mismatch")
    _require(report.get("renderer") == "normal", "strict seek renderer is not normal")
    _require(runtime.get("canonical_convergence") == "exact",
             "strict seek canonical convergence failed")
    _require(runtime.get("authored_outcomes_required") is True
             and runtime.get("stock_round_outcome") is not None,
             "strict seek authored/vanilla outcome proof missing")
    seeks = runtime.get("seeks", [])
    _require([row.get("percentage") for row in seeks] == [10, 25, 50, 75],
             "strict seeks are not exactly 10/25/50/75")
    for row in seeks:
        _require(row.get("live_resumed", 0) >= 600, "strict seek resumed fewer than 600 frames")
        _require(row.get("resume_window", 0) >= 120, "strict seek rate window too short")
        _require(row.get("resume_tick_rate", 0) >= 58.0, "strict seek rate below 58 Hz")
        _require(row.get("validation_us", 10**12) <= 500_000,
                 "strict seek validation exceeded 0.5 seconds")


def _paired_gate(reports: list[dict[str, Any]], case: dict[str, Any],
                 dll_hash: str, schema_hash: str,
                 expected: dict[str, Any] | None = None) -> str:
    profiles: set[str] = set()
    failures: set[str] = set()
    cycling = continuous = False
    fresh = False
    loaded_map = ""
    for report in reports:
        _common_report(report, case, dll_hash, schema_hash, expected)
        _require(report.get("kind") == "paired_online_case",
                 "paired release evidence has the wrong kind")
        identities = report["identities"]
        runtime = report.get("runtime", {})
        cleanup = report.get("cleanup", {})
        profile = report.get("impairment", {}).get("profile")
        if isinstance(profile, str):
            profiles.add(profile)
        failure_case = runtime.get("failure_case")
        if isinstance(failure_case, str):
            failures.add(failure_case)
        _require(cleanup.get("requests_disarmed") is True
                 and cleanup.get("diagnostic_flags_false") is True
                 and cleanup.get("game_processes_remaining") == 0,
                 "paired cleanup proof is incomplete")
        if not isinstance(failure_case, str):
            _require(runtime.get("real_corrections") is True,
                     "paired functional gate has no measured correction")
            _require(runtime.get("multi_round") is True,
                     "paired functional gate is not multi-round")
            _require(runtime.get("presentation_reconciliation") == "exact",
                     "paired presentation did not reconcile exactly")
            convergence = runtime.get("confirmed_convergence")
            _require(isinstance(convergence, dict)
                     and convergence.get("matched_checks", 0) > 0
                     and convergence.get("cadence_frames") == 30,
                     "paired peer hash convergence/cadence proof missing")
        cycling |= runtime.get("cycling_soak_seconds", 0) >= 3600
        continuous |= runtime.get("continuous_soak_seconds", 0) >= 3600
        fresh |= (runtime.get("fresh_box") is True
                  and report.get("processes", {}).get("sandbox_box") != "sc67"
                  and runtime.get("real_corrections") is True)
        current_map = identities.get("loaded_map_sha256", "")
        _require(isinstance(current_map, str) and len(current_map) == 64,
                 "loaded map package hash missing")
        if loaded_map and loaded_map != current_map:
            raise RuntimeError("loaded map hash changed across paired release gates")
        loaded_map = current_map
    _require(REQUIRED_PROFILES <= profiles,
             f"paired impairment profiles missing: {sorted(REQUIRED_PROFILES - profiles)}")
    _require(REQUIRED_FAILURE_CASES <= failures,
             f"paired failure cases missing: {sorted(REQUIRED_FAILURE_CASES - failures)}")
    _require(cycling, "one-hour same-process cycling soak missing")
    _require(continuous, "one-hour continuous corrected-play soak missing")
    _require(fresh, "fresh-box release qualification missing")
    clean = [report for report in reports
             if report.get("impairment", {}).get("profile") == "clean"]
    _require(any(row.get("runtime", {}).get("same_process_match_cycles", 0) >= 2
                 for row in clean), "second same-lobby match proof missing")
    return loaded_map


def _certificate(case: dict[str, Any], paired: bool, hashes: dict[str, str],
                 loaded_map: str) -> bytes:
    value: dict[str, Any] = {
        "report_schema": 2,
        "kind": "paired_online_release_case" if paired else "offline_release_case",
        "certifying": True,
        "result": "pass",
        "protocol_version": 2,
        "snapshot_schema_version": 47,
        "case_id": case["case_id"],
        "fighter_order": case["fighter_order"],
        "stage_selection_code": case["stage_selection_code"],
        "authored_stage_code": case["authored_stage_code"],
        "stage_package_root": case["stage_package_root"],
        "map_path": case["map_path"],
        "native_display_name": case["native_display_name"],
        "rng_policy": "authored_stage_only_random_selection_forbidden",
        "renderer": "normal",
        "game_executable_sha256": hashes["executable"],
        "horsemod_dll_sha256": hashes["dll"],
        "source_identity_sha256": hashes["source"],
        "schema_sha256": hashes["schema"],
        "candidate_manifest_sha256": hashes["candidate"],
        "region_manifest_sha256": hashes["regions"],
        "runner_sha256": hashes["runner"],
        "capture_harness_sha256": hashes["capture_harness"],
        "offline_evaluator_sha256": hashes["offline_evaluator"],
        "replay_qualification_mod_sha256": hashes["replay_mod"],
        "loaded_map_sha256": loaded_map,
        "canonical_divergences": 0,
        "ordered_audio_payload_ids": True,
        "presentation_reconciliation": "exact",
    }
    if paired:
        value.update(authenticated_steam_p2p=True,
                     multi_round_real_corrections=True,
                     impairments_and_failures_complete=True)
    else:
        value.update(normal_render_matrix_rows=17,
                     strict_replay_gates_complete=True)
    value["qualification_complete"] = True
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def _atomic_write(path: Path, value: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".publish.tmp")
    with temporary.open("wb") as stream:
        stream.write(value)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def publish_release(args: Any, root: Path) -> int:
    identity = source_identity(root)
    _require(identity["dirty"] is False, "release publication requires frozen sources")
    index = _load(args.release_index.resolve())
    _require(index.get("schema_version") == 1, "release index is not schema v1")
    manifest = args.case_manifest.resolve()
    schema = args.schema.resolve()
    cases = load_candidate_cases(manifest)
    candidate_hash = require_compiled_candidate_manifest(schema, manifest)
    hashes = {
        "executable": sha256_file(args.game_executable),
        "dll": sha256_file(args.dll),
        "source": source_identity_sha256(root),
        "schema": sha256_file(schema),
        "candidate": candidate_hash,
        "regions": sha256_file(args.region_manifest),
        "replay_mod": sha256_file(args.replay_mod),
        "runner": runner_sha256(
            root / "tools" / "deterministic_qualification"),
        "capture_harness": capture_harness_sha256(root),
        "offline_evaluator": offline_evaluator_sha256(root),
    }
    expected = {
        "source": identity,
        "runner": hashes["runner"],
        "capture_harness": hashes["capture_harness"],
        "executable": hashes["executable"],
        "replay_mod": hashes["replay_mod"],
    }
    offline_dir = Path(index["offline_output_dir"]).resolve()
    runner_hash = expected["runner"]
    matrix = evaluate_matrix(manifest, offline_dir, hashes["dll"], hashes["schema"],
                             runner_hash, expected,
                             hashes["offline_evaluator"])
    _require(matrix["certifying"] is True, "51-row offline matrix is incomplete")
    tira = _load(Path(index["tira_report"]).resolve())
    _require(tira.get("certifying") is True and tira.get("transition_runs", 0) > 0,
             "actual Tira helper 0x321B RNG/state19 transition gate is incomplete")
    _require(tira.get("artifacts", {}).get("horsemod_dll_sha256") == hashes["dll"]
             and tira.get("artifacts", {}).get("schema_sha256") == hashes["schema"]
             and tira.get("artifacts", {}).get("runner_sha256") == runner_hash,
             "Tira gate does not bind the frozen release artifacts")
    _require(tira.get("source") == identity,
             "Tira gate source identity does not match the release")
    _require(tira.get("artifacts", {}).get("tira_manifest_sha256")
             == sha256_file(args.tira_manifest),
             "Tira gate manifest identity does not match the release")
    _require(tira.get("artifacts", {}).get("replay_qualification_mod_sha256")
             == hashes["replay_mod"],
             "Tira gate replay bridge identity does not match the release")
    _require(tira.get("artifacts", {}).get("game_executable_sha256")
             == hashes["executable"],
             "Tira gate executable identity does not match the release")
    tira_reports = tira.get("artifacts", {}).get("evidence_reports")
    _require(isinstance(tira_reports, list) and len(tira_reports) == 6,
             "Tira gate raw-report identity set is incomplete")
    for evidence in tira_reports:
        evidence_path = Path(evidence.get("path", "")).resolve()
        _require(evidence_path.is_file()
                 and evidence.get("sha256") == sha256_file(evidence_path),
                 "Tira gate raw-report hash mismatch")
    indexed_cases = {row["case_id"]: row for row in index.get("cases", [])}
    _require(set(indexed_cases) == {case["case_id"] for case in cases},
             "release index does not contain the exact three cases")
    pending_certificates: list[tuple[Path, bytes, Path, bytes, str]] = []
    for position, case in enumerate(cases):
        row = indexed_cases[case["case_id"]]
        _strict_seek_gate(_load(Path(row["strict_report"]).resolve()),
                          case, hashes["dll"], hashes["schema"], expected)
        paired_reports = [_load(Path(path).resolve()) for path in row["paired_reports"]]
        loaded_map = _paired_gate(
            paired_reports, case, hashes["dll"], hashes["schema"], expected)
        offline_path = args.output_dir / f"{case['case_id']}-offline-release.json"
        paired_path = args.output_dir / f"{case['case_id']}-paired-release.json"
        pending_certificates.append((
            offline_path.resolve(), _certificate(case, False, hashes, loaded_map),
            paired_path.resolve(), _certificate(case, True, hashes, loaded_map),
            loaded_map,
        ))
    for offline_path, offline_bytes, paired_path, paired_bytes, _ in pending_certificates:
        _atomic_write(offline_path, offline_bytes)
        _atomic_write(paired_path, paired_bytes)
    lines = [
        "version=1", f"source_commit={identity['commit']}",
        f"game_executable_sha256={hashes['executable']}",
        f"horsemod_dll_sha256={hashes['dll']}",
        f"source_identity_sha256={hashes['source']}",
        f"schema_path={schema}", f"schema_sha256={hashes['schema']}",
        f"candidate_manifest_path={manifest}",
        f"candidate_manifest_sha256={hashes['candidate']}",
        f"region_manifest_path={args.region_manifest.resolve()}",
        f"region_manifest_sha256={hashes['regions']}",
    ]
    for position, (offline, _, paired, _, loaded_map) in enumerate(pending_certificates):
        lines.extend([
            f"case{position}_offline_report_path={offline}",
            f"case{position}_offline_report_sha256={sha256_file(offline)}",
            f"case{position}_paired_report_path={paired}",
            f"case{position}_paired_report_sha256={sha256_file(paired)}",
            f"case{position}_loaded_map_sha256={loaded_map}",
        ])
    _require(len(lines) == 26, "release allowlist field count is not exact")
    _atomic_write(args.allowlist.resolve(), ("\n".join(lines) + "\n").encode("utf-8"))
    return 0
