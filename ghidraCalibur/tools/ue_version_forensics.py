#!/usr/bin/env python3
"""Rank UE source baselines against retained SC6 shipping-binary evidence.

This is deliberately conservative: source-only markers are evidence candidates,
not proof of an exact proprietary engine commit.  A baseline label requires
multiple independent binary-visible discriminators.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any


RUNTIME_ROOT = Path("Engine/Source/Runtime")
BUILD_VERSION = Path("Engine/Build/Build.version")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".inl"}
STRING_RE = re.compile(r'"((?:\\.|[^"\\]){10,180})"')
SKIP_PATH_PARTS = {"editor", "tests", "test", "programs", "developer"}
RENDERER_OR_PLATFORM_MODULES = {
    "d3d11rhi", "d3d12rhi", "metalrhi", "opengldrv", "rendercore", "renderer",
    "rhi", "shadercore", "vulkanrhi",
}
OPTIONAL_OR_NON_TARGET_MODULES = {
    "android", "audiomixerxaudio2", "headmounteddisplay", "ios", "linux", "mac",
    "networkfilesystem", "ps4", "switch", "xboxone",
}
VERSION_MARKERS = ("++UE4+Release-4.17", "4.17.2.0")


class ForensicsError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args], text=True, capture_output=True, check=False
    )
    if result.returncode:
        return ""
    return result.stdout.strip()


def read_build_version(root: Path) -> dict[str, Any]:
    path = root / BUILD_VERSION
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ForensicsError(f"Invalid Build.version at {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ForensicsError(f"Build.version at {path} is not an object")
    return value


def candidate_metadata(label: str, root: Path) -> dict[str, Any]:
    return {
        "label": label,
        "root": str(root),
        "commit": run_git(root, "rev-parse", "HEAD") or "unavailable",
        "commit_date": run_git(root, "show", "-s", "--format=%cI", "HEAD") or "unavailable",
        "subject": run_git(root, "show", "-s", "--format=%s", "HEAD") or "unavailable",
        "parents": (run_git(root, "show", "-s", "--format=%P", "HEAD").split() or ["unavailable"]),
        "build_version": read_build_version(root),
    }


def is_shipping_relevant(path: Path) -> bool:
    lowered = {part.lower() for part in path.parts}
    return not lowered.intersection(
        SKIP_PATH_PARTS | RENDERER_OR_PLATFORM_MODULES | OPTIONAL_OR_NON_TARGET_MODULES
    )


def clean_source_string(value: str) -> str | None:
    if "\\" in value or "\n" in value or "\r" in value or "\t" in value:
        return None
    if any(ord(char) < 0x20 or ord(char) > 0x7E for char in value):
        return None
    if value.endswith((".h", ".hpp", ".cpp", ".inl")) or "/" in value or "\\" in value:
        return None
    if value.count("%") > 2 or len(re.findall(r"[A-Za-z]{3,}", value)) < 2:
        return None
    return value


def source_strings(root: Path) -> dict[str, str]:
    runtime = root / RUNTIME_ROOT
    if not runtime.is_dir():
        raise ForensicsError(f"Missing UE runtime source tree: {runtime}")
    found: dict[str, str] = {}
    for path in sorted(runtime.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(root)
        if not is_shipping_relevant(relative):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError as exc:
            raise ForensicsError(f"Cannot read {path}: {exc}") from exc
        for match in STRING_RE.finditer(text):
            marker = clean_source_string(match.group(1))
            if marker:
                found.setdefault(marker, relative.as_posix())
    return found


def runtime_file_hashes(root: Path) -> dict[str, str]:
    runtime = root / RUNTIME_ROOT
    result: dict[str, str] = {}
    for path in sorted(runtime.rglob("*")):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES and is_shipping_relevant(path.relative_to(root)):
            result[path.relative_to(root).as_posix()] = sha256_file(path)
    return result


def changed_file_count(left: dict[str, str], right: dict[str, str]) -> int:
    return sum(left.get(path) != right.get(path) for path in set(left) | set(right))


def marker_quality(marker: str) -> tuple[int, int, str]:
    # Prefer descriptive shipping diagnostics over short, generic phrases.
    words = len(re.findall(r"[A-Za-z]{3,}", marker))
    return (words, min(len(marker), 120), marker)


def select_markers(
    release: dict[str, str], staging: dict[str, str], limit: int
) -> list[dict[str, str]]:
    selected: list[dict[str, str]] = []
    per_side = max(1, limit // 2)
    for side, own, other in (
        ("staging-only", staging, release),
        ("release-only", release, staging),
    ):
        values = sorted((value for value in own if value not in other), key=marker_quality, reverse=True)
        for value in values[:per_side]:
            selected.append({"baseline": side, "marker": value, "source_path": own[value]})
    return selected[:limit]


def find_binary_offsets(blob: bytes, marker: str, maximum: int = 8) -> dict[str, list[int]]:
    encoded = marker.encode("ascii")
    variants = {"ascii": encoded, "utf16le": marker.encode("utf-16le")}
    found: dict[str, list[int]] = {}
    for encoding, needle in variants.items():
        offsets: list[int] = []
        offset = blob.find(needle)
        while offset >= 0 and len(offsets) < maximum:
            offsets.append(offset)
            offset = blob.find(needle, offset + 1)
        found[encoding] = offsets
    return found


def baseline_score(markers: list[dict[str, Any]]) -> dict[str, Any]:
    counts = Counter()
    for marker in markers:
        if marker["status"] == "present" and marker["corroboration_status"] == "confirmed":
            counts[marker["baseline"]] += 1
    staging = counts["staging-only"]
    release = counts["release-only"]
    if staging >= 3 and staging >= release + 2:
        conclusion = "staging-4.17-era baseline favored"
    elif release >= 3 and release >= staging + 2:
        conclusion = "final-4.17.2-release baseline favored"
    else:
        conclusion = "inconclusive; evidence supports UE 4.17.2 but not a source lineage"
    return {
        "conclusion": conclusion,
        "matched_staging_only": staging,
        "matched_release_only": release,
        "minimum_independent_matches": 3,
        "unconfirmed_binary_string_matches": sum(
            marker["status"] == "present" and marker["corroboration_status"] != "confirmed"
            for marker in markers
        ),
    }


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_report(path: Path, manifest: dict[str, Any], markers: list[dict[str, Any]], score: dict[str, Any]) -> None:
    lines = [
        "# SC6 Unreal Engine Source-Baseline Forensics",
        "",
        "## Conclusion",
        "",
        f"**{score['conclusion']}**",
        "",
        f"- staging-only matches: {score['matched_staging_only']}",
        f"- release-only matches: {score['matched_release_only']}",
        f"- required independently corroborated matches: {score['minimum_independent_matches']}",
        f"- unconfirmed source/binary string matches: {score['unconfirmed_binary_string_matches']}",
        "",
        "A result below the threshold is deliberately inconclusive; it is not evidence for stock UE or staging lineage.",
        "",
        "## Candidates",
        "",
        "| Candidate | Commit | Build version |",
        "| --- | --- | --- |",
    ]
    for candidate in manifest["candidates"]:
        version = candidate["build_version"]
        lines.append(
            f"| {candidate['label']} | `{candidate['commit']}` | "
            f"{version.get('MajorVersion')}.{version.get('MinorVersion')}.{version.get('PatchVersion')} |"
        )
    lines.extend(["", "## Binary evidence", ""])
    for name, evidence in manifest["sc6_binary"]["version_markers"].items():
        lines.append(f"- `{name}`: ASCII={evidence['ascii']}, UTF-16LE={evidence['utf16le']}")
    lines.extend(["", "## Discriminators", "", "| Baseline-only marker | Source path | SC6 result |", "| --- | --- | --- |"])
    for marker in markers:
        display = marker["marker"].replace("|", "\\|")
        lines.append(
            f"| {marker['baseline']}: `{display}` | `{marker['source_path']}` | "
            f"{marker['status']} ({marker['corroboration_status']}) |"
        )
    lines.extend([
        "",
        "## Limits",
        "",
        "- SC6 reports `CL-0`; version metadata cannot identify a private source commit.",
        "- Source strings can be removed from Shipping builds. A source/binary string match is provisional until a Ghidra control-flow, layout, serializer, or reflection check confirms it; only confirmed markers affect the baseline score.",
    ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def load_corroboration(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ForensicsError(f"Invalid corroboration file {path}: {exc}") from exc
    entries = value.get("markers") if isinstance(value, dict) else None
    if not isinstance(entries, list):
        raise ForensicsError("Corroboration file must contain a 'markers' array")
    result: dict[str, dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("marker"), str):
            raise ForensicsError("Every corroboration entry requires a string marker")
        if entry.get("verified") is not True:
            raise ForensicsError(f"Corroboration entry for {entry['marker']!r} must set verified=true")
        if not isinstance(entry.get("kind"), str) or not isinstance(entry.get("location"), str):
            raise ForensicsError(f"Corroboration entry for {entry['marker']!r} requires kind and location")
        result[entry["marker"]] = entry
    return result


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    release_root = args.release_root.resolve()
    staging_root = args.staging_pre_root.resolve()
    staging_start_root = args.staging_start_root.resolve()
    executable = args.sc6_executable.resolve()
    if not executable.is_file():
        raise ForensicsError(f"SC6 executable is not readable: {executable}")

    release_strings = source_strings(release_root)
    staging_strings = source_strings(staging_root)
    file_sets = {
        "release": runtime_file_hashes(release_root),
        "staging_pre_4_18": runtime_file_hashes(staging_root),
        "staging_4_17_start": runtime_file_hashes(staging_start_root),
    }
    blob = executable.read_bytes()
    corroboration = load_corroboration(getattr(args, "corroboration", None))
    version_markers = {marker: find_binary_offsets(blob, marker) for marker in VERSION_MARKERS}
    raw_markers = select_markers(release_strings, staging_strings, args.marker_limit)
    markers: list[dict[str, Any]] = []
    for marker in raw_markers:
        offsets = find_binary_offsets(blob, marker["marker"])
        markers.append({
            **marker,
            "binary_offsets": offsets,
            "status": "present" if offsets["ascii"] or offsets["utf16le"] else "absent",
            "corroboration_status": "confirmed" if marker["marker"] in corroboration else "unverified",
            "corroboration": corroboration.get(marker["marker"]),
        })
    score = baseline_score(markers)
    manifest = {
        "schema": "sc6-ue-source-forensics/v1",
        "candidates": [
            candidate_metadata("final-4.17.2-release", release_root),
            candidate_metadata("staging-4.17-pre-4.18", staging_root),
            candidate_metadata("staging-4.17-start", staging_start_root),
        ],
        "comparisons": {
            "staging_4_17_historical_window": {
                "branch": "staging-4.17",
                "start_candidate": "staging-4.17-start",
                "end_candidate": "staging-4.17-pre-4.18",
                "boundary": "The current staging-4.17 ref advanced to 4.18 and is intentionally excluded.",
            },
            "release_vs_staging_pre_4_18": {
                "left": "final-4.17.2-release",
                "right": "staging-4.17-pre-4.18",
            },
            "release_vs_staging_pre_4_18_changed_runtime_files": changed_file_count(file_sets["release"], file_sets["staging_pre_4_18"]),
            "staging_start_vs_pre_4_18_changed_runtime_files": changed_file_count(file_sets["staging_4_17_start"], file_sets["staging_pre_4_18"]),
        },
        "sc6_binary": {
            "path": str(executable),
            "sha256": sha256_file(executable),
            "version_markers": version_markers,
        },
        "scoring": score,
    }
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    write_json(output / "ue_baseline_candidates.json", manifest)
    write_json(output / "ue_discriminators.json", {"schema": manifest["schema"], "markers": markers, "scoring": score})
    write_report(output / "ue_baseline_report.md", manifest, markers, score)
    return {"manifest": manifest, "markers": markers, "score": score}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-root", required=True, type=Path)
    parser.add_argument("--staging-pre-root", required=True, type=Path)
    parser.add_argument("--staging-start-root", required=True, type=Path)
    parser.add_argument("--sc6-executable", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--corroboration", type=Path, help="read-only Ghidra/layout evidence JSON")
    parser.add_argument("--marker-limit", type=int, default=30)
    args = parser.parse_args()
    if args.marker_limit < 6:
        parser.error("--marker-limit must be at least 6")
    try:
        result = analyze(args)
    except ForensicsError as exc:
        print(f"UE source forensics failed: {exc}")
        return 2
    print(json.dumps(result["score"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
