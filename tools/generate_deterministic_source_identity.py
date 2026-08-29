from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import subprocess
from pathlib import Path


PROTECTED = {
    "tools/moveset_parser/hgmotion_reference.py",
    "tools/moveset_parser/luxformats.py",
    "tools/moveset_parser/motion_decode.py",
    "tools/moveset_parser/tests/test_hgmotion_reference.py",
    "tools/moveset_parser/tests/test_motion_decode.py",
    "tools/moveset_parser/tests/test_uassetparse.py",
    "tools/moveset_parser/uassetparse.py",
}

EXACT_INPUTS = {
    "CMakeLists.txt",
    "HorseMod/CMakeLists.txt",
    "docs/investigations/deterministic-production-candidate-manifest.json",
    "docs/investigations/deterministic-production-region-manifest.json",
    "docs/investigations/deterministic-tira-qualification-manifest.json",
    "tools/deterministic_qualification.py",
}

INPUT_PREFIXES = (
    "HorseMod/horselib/deterministic/",
    "tools/deterministic_qualification/",
    "tools/replay_qualification_mod/",
)

TOOL_PREFIXES = (
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


def _run(root: Path, *command: str, text: bool = True) -> str | bytes:
    result = subprocess.run(
        list(command), cwd=root, check=True, capture_output=True, text=text
    )
    return result.stdout


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _is_input(name: str) -> bool:
    if name in PROTECTED:
        return False
    if name in EXACT_INPUTS or name in {
        "HorseMod/dllmain.cpp", "HorseMod/horselib/deterministic/Schema.hpp"
    }:
        return True
    return name.startswith(INPUT_PREFIXES) or name.startswith(TOOL_PREFIXES)


def _cache_value(cache: Path, key: str) -> str:
    if not cache.is_file():
        return ""
    prefix = key + ":"
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix):
            return line.partition("=")[2]
    return ""


def generate(root: Path, build_dir: Path, output: Path,
             build_command: str) -> dict[str, object]:
    root = root.resolve()
    build_dir = build_dir.resolve()
    output = output.resolve()
    all_names = str(_run(
        root, "git", "ls-files", "--cached", "--others",
        "--exclude-standard", "-z"
    )).split("\0")
    inputs = sorted(name for name in all_names if name and _is_input(name))
    files: list[dict[str, object]] = []
    for name in inputs:
        path = root / name
        if not path.is_file():
            continue
        files.append({
            "path": name,
            "size": path.stat().st_size,
            "sha256": _sha256_file(path),
        })

    tracked = set(str(_run(root, "git", "ls-files", "-z")).split("\0"))
    tracked_inputs = [name for name in inputs if name in tracked]
    patch = b""
    if tracked_inputs:
        patch = bytes(_run(
            root, "git", "diff", "--binary", "--no-ext-diff", "--",
            *tracked_inputs, text=False
        ))
    patch_path = output.with_suffix(".tracked.patch")
    patch_path.parent.mkdir(parents=True, exist_ok=True)
    patch_path.write_bytes(patch)

    cache = build_dir / "CMakeCache.txt"
    compiler_text = _cache_value(cache, "CMAKE_CXX_COMPILER")
    compiler = Path(compiler_text) if compiler_text else None
    cmake_path = Path(str(_run(root, "where.exe", "cmake")).splitlines()[0])
    status = str(_run(
        root, "git", "status", "--porcelain=v2", "-z",
        "--untracked-files=all"
    ))
    status_entries = [item for item in status.split("\0") if item]
    input_names = {str(entry["path"]) for entry in files}
    status_entries = [item for item in status_entries if any(
        token in input_names for token in item.split()
    )]
    untracked = [entry for entry in files if entry["path"] not in tracked]
    document: dict[str, object] = {
        "schema_version": 1,
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "superproject_head": str(_run(root, "git", "rev-parse", "HEAD")).strip(),
        "recursive_submodules": str(_run(
            root, "git", "submodule", "status", "--recursive"
        )).splitlines(),
        "status_porcelain_v2_z": status_entries,
        "deterministic_inputs": files,
        "untracked_deterministic_inputs": untracked,
        "tracked_binary_patch": {
            "path": patch_path.name,
            "size": len(patch),
            "sha256": _sha256_bytes(patch),
        },
        "toolchain": {
            "cmake_path": str(cmake_path),
            "cmake_sha256": _sha256_file(cmake_path),
            "cmake_version": str(_run(root, str(cmake_path), "--version")).strip(),
            "cxx_compiler_path": "" if compiler is None else str(compiler),
            "cxx_compiler_sha256": "" if compiler is None or not compiler.is_file()
                else _sha256_file(compiler),
            "cxx_compiler_version": _cache_value(
                cache, "CMAKE_CXX_COMPILER_VERSION"
            ),
        },
        "build_directory": str(build_dir),
        "build_command": build_command,
    }
    encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
    output.write_text(encoded, encoding="utf-8", newline="\n")
    return document


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate the bounded deterministic evidence-build identity"
    )
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-command", required=True)
    args = parser.parse_args()
    generate(args.root, args.build_dir, args.output, args.build_command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
