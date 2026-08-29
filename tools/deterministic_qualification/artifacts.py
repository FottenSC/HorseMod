from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path


PROTECTED_UNRELATED_PATHS = {
    "tools/moveset_parser/hgmotion_reference.py",
    "tools/moveset_parser/luxformats.py",
    "tools/moveset_parser/motion_decode.py",
    "tools/moveset_parser/tests/test_hgmotion_reference.py",
    "tools/moveset_parser/tests/test_motion_decode.py",
    "tools/moveset_parser/tests/test_uassetparse.py",
    "tools/moveset_parser/uassetparse.py",
}

DETERMINISTIC_EXACT_PATHS = {
    "CMakeLists.txt",
    "HorseMod/CMakeLists.txt",
    "HorseMod/dllmain.cpp",
    "docs/investigations/deterministic-production-candidate-manifest.json",
    "docs/investigations/deterministic-production-region-manifest.json",
    "docs/investigations/deterministic-tira-qualification-manifest.json",
    "tools/deterministic_qualification.py",
}

DETERMINISTIC_PREFIXES = (
    "HorseMod/horselib/deterministic/",
    "tools/deterministic_qualification/",
    "tools/replay_qualification_mod/",
    "tools/deterministic_",
    "tools/generate_compiled_release_identities.py",
    "tools/generate_production_candidate_manifest.py",
    "tools/generate_production_regions.py",
    "tools/gekko_rollback_session_selftest.cpp",
    "tools/native_candidate_regions_selftest.cpp",
    "tools/online_coordinator_selftest.cpp",
    "tools/production_release_loader_selftest.cpp",
    "tools/sc6_online_",
    "tools/steam_p2p_transport_selftest.cpp",
)


def _deterministic_path(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return (normalized in DETERMINISTIC_EXACT_PATHS
            or normalized.startswith(DETERMINISTIC_PREFIXES))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_compiled_candidate_manifest(schema: Path, manifest: Path) -> str:
    try:
        document = json.loads(schema.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"generated deterministic schema is invalid: {error}") from error
    expected = document.get("production_candidate_manifest_sha256")
    actual = sha256_file(manifest)
    if not isinstance(expected, str) or expected.casefold() != actual:
        raise RuntimeError(
            "case manifest hash does not match the manifest compiled into the DLL schema")
    return actual


def git_text(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def submodule_status(root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "submodule", "status", "--recursive"], cwd=root,
        check=False, capture_output=True, text=True,
    )
    if result.returncode == 0:
        return result.stdout.strip().splitlines()

    rows: list[str] = []

    def collect(repository: Path, prefix: str = "") -> None:
        staged = subprocess.run(
            ["git", "ls-files", "--stage"], cwd=repository, check=True,
            capture_output=True, text=True,
        ).stdout.splitlines()
        for line in staged:
            metadata, separator, relative = line.partition("\t")
            fields = metadata.split()
            if not separator or len(fields) != 3 or fields[0] != "160000":
                continue
            expected = fields[1]
            module = repository / relative
            display = f"{prefix}/{relative}" if prefix else relative
            if not module.is_dir():
                rows.append(f"-{expected} {display}")
                continue
            actual = git_text(module, "rev-parse", "HEAD")
            rows.append(f"{' ' if actual == expected else '+'}{actual} {display}")
            collect(module, display)

    collect(root)
    return rows


def source_identity(root: Path) -> dict[str, object]:
    result = subprocess.run(
        ["git", "status", "--porcelain=v2", "-z", "--untracked-files=all"],
        cwd=root, check=True, capture_output=True, text=True,
    )
    status = [entry for entry in result.stdout.split("\0") if entry]
    status = [entry for entry in status if any(
        _deterministic_path(token)
        and token not in PROTECTED_UNRELATED_PATHS
        for token in entry.split()
    )]
    return {
        "commit": git_text(root, "rev-parse", "HEAD"),
        "dirty": bool(status),
        "status_porcelain_v2": status,
        "submodules": submodule_status(root),
    }


def source_identity_sha256(root: Path) -> str:
    encoded = json.dumps(
        source_identity(root), sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def runner_sha256(package_dir: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(package_dir.glob("*.py"), key=lambda item: item.name):
        digest.update(path.name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()
