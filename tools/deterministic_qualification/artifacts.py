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
    "HorseMod/OnlineQualificationService.inl",
    "HorseMod/rollback.ini.example",
    "docs/investigations/deterministic-production-candidate-manifest.json",
    "docs/investigations/deterministic-production-region-manifest.json",
    "docs/investigations/deterministic-tira-qualification-manifest.json",
    "tools/deterministic_qualification.py",
}

DETERMINISTIC_PREFIXES = (
    "HorseMod/HorseModService.",
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

# Files which can change how a replay request is launched, observed, parsed,
# or reported.  Offline policy/evaluation code is intentionally excluded: a
# policy-only fix may re-evaluate immutable raw captures, but any change here
# invalidates them.  The native qualification bridge is hashed as source here
# and as an exact DLL in every capture report.
CAPTURE_HARNESS_PATHS = (
    "tools/deterministic_qualification.py",
    "tools/deterministic_qualification/__init__.py",
    "tools/deterministic_qualification/artifacts.py",
    "tools/deterministic_qualification/configuration.py",
    "tools/deterministic_qualification/process_control.py",
    "tools/deterministic_qualification/replay_entry.py",
    "tools/deterministic_qualification/report.py",
    "tools/deterministic_qualification/runner.py",
    "tools/deterministic_qualification/trace_parser.py",
    "tools/replay_qualification_mod/OnlineRoomAutomation.cpp",
    "tools/replay_qualification_mod/OnlineRoomAutomation.hpp",
    "tools/replay_qualification_mod/ReplayPayloadImporter.cpp",
    "tools/replay_qualification_mod/ReplayPayloadImporter.hpp",
    "tools/replay_qualification_mod/ReplayQualificationMod.cpp",
    "tools/replay_qualification_mod/ReplaySceneNavigator.cpp",
    "tools/replay_qualification_mod/ReplaySceneNavigator.hpp",
)

OFFLINE_EVALUATOR_PATHS = (
    "tools/deterministic_qualification/offline_campaign.py",
    "tools/deterministic_qualification/offline_matrix.py",
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


def _git_paths(root: Path, *args: str) -> list[str]:
    result = subprocess.run(
        ["git", *args, "-z"], cwd=root, check=True,
        capture_output=True, text=True,
    )
    return [item.replace("\\", "/")
            for item in result.stdout.split("\0") if item]


def _tracked_inputs(root: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for relative in sorted(_git_paths(root, "ls-files")):
        if not _deterministic_path(relative):
            continue
        path = root / relative
        if not path.is_file():
            continue
        records.append({
            "path": relative,
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
        })
    return records


def _untracked_inputs(root: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for relative in sorted(_git_paths(
            root, "ls-files", "--others", "--exclude-standard")):
        if (not _deterministic_path(relative)
                or relative in PROTECTED_UNRELATED_PATHS):
            continue
        path = root / relative
        if path.is_file():
            records.append({
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            })
    return records


def _binary_deterministic_patch(root: Path) -> str:
    changed = set(_git_paths(root, "diff", "--name-only", "HEAD"))
    selected = sorted(path for path in changed
                      if _deterministic_path(path)
                      and path not in PROTECTED_UNRELATED_PATHS)
    patches: list[str] = []
    for relative in selected:
        result = subprocess.run(
            ["git", "diff", "--binary", "HEAD", "--", relative],
            cwd=root, check=True, capture_output=True,
        )
        patches.append(result.stdout.decode("utf-8", errors="surrogateescape"))
    return "".join(patches)


def _build_identity(root: Path) -> dict[str, object]:
    build = root / "build_cmake_LessEqual421__Shipping__Win64"
    cache = build / "CMakeCache.txt"
    fields: dict[str, str] = {}
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            key, separator, value = line.partition("=")
            if not separator:
                continue
            normalized = key.partition(":")[0]
            if normalized in {
                "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_VERSION",
                "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM",
            }:
                fields[normalized.casefold()] = value
    cmake = subprocess.run(
        ["cmake", "--version"], cwd=root, check=True,
        capture_output=True, text=True,
    ).stdout.splitlines()[0]
    compiler_path = Path(fields.get("cmake_cxx_compiler", ""))
    compiler_artifact: dict[str, object] = {}
    if compiler_path.is_file():
        probe = subprocess.run(
            [str(compiler_path)], cwd=root, check=False,
            capture_output=True, text=True,
        )
        banner_lines = (probe.stdout + probe.stderr).splitlines()
        compiler_artifact = {
            "path": str(compiler_path),
            "size": compiler_path.stat().st_size,
            "sha256": sha256_file(compiler_path),
            "banner": banner_lines[0] if banner_lines else "",
        }
    return {
        "cmake": cmake,
        "compiler": fields,
        "compiler_artifact": compiler_artifact,
        "configure_command": (
            "cmake -S E:\\myMods -B "
            "E:\\myMods\\build_cmake_LessEqual421__Shipping__Win64"
        ),
        "build_command": (
            "cmake --build "
            "E:\\myMods\\build_cmake_LessEqual421__Shipping__Win64 "
            "--config Shipping -j 4"
        ),
    }


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
    binary_patch = _binary_deterministic_patch(root)
    return {
        "commit": git_text(root, "rev-parse", "HEAD"),
        "dirty": bool(status),
        "status_porcelain_v2": status,
        "submodules": submodule_status(root),
        "tracked_inputs": _tracked_inputs(root),
        "untracked_inputs": _untracked_inputs(root),
        "deterministic_binary_patch": binary_patch,
        "deterministic_binary_patch_sha256": hashlib.sha256(
            binary_patch.encode("utf-8", errors="surrogateescape")).hexdigest(),
        "deterministic_binary_patch_bytes": len(binary_patch.encode(
            "utf-8", errors="surrogateescape")),
        "build": _build_identity(root),
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


def _named_files_sha256(root: Path, relative_paths: tuple[str, ...]) -> str:
    digest = hashlib.sha256()
    for relative in relative_paths:
        path = root / relative
        if not path.is_file():
            raise FileNotFoundError(f"identity input is missing: {path}")
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def capture_harness_sha256(root: Path) -> str:
    """Hash only code capable of changing raw replay capture semantics."""
    return _named_files_sha256(root, CAPTURE_HARNESS_PATHS)


def offline_evaluator_sha256(root: Path) -> str:
    """Hash the current policy/composition layer independently of capture."""
    return _named_files_sha256(root, OFFLINE_EVALUATOR_PATHS)
