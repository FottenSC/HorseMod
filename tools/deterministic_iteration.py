from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build_cmake_LessEqual421__Shipping__Win64"
VCVARS = Path(
    r"E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat"
)
GAME_ROOT = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64"
)


def _developer_command(command: str) -> list[str]:
    return ["cmd", "/d", "/s", "/c", f"call {VCVARS} >NUL && {command}"]


def _run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def build(*, freeze_source_identity: bool, jobs: int) -> None:
    if freeze_source_identity:
        _run(_developer_command(f"cmake -S {ROOT} -B {BUILD}"))
    _run(_developer_command(
        f'cmake --build {BUILD} --config Shipping '
        f'--target DeterministicIterationTargets -j {jobs}'
    ))


def test_parallel(*, jobs: int) -> None:
    native = [
        "ctest", "--test-dir", str(BUILD), "--output-on-failure",
        "--label-regex", "deterministic", "-j", str(jobs),
    ]
    python = [
        os.fspath(Path(os.sys.executable)), "-m", "pytest", "-q",
        "tools/deterministic_qualification/tests",
    ]
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(_run, native), pool.submit(_run, python)]
        for future in futures:
            future.result()


def deploy() -> None:
    if subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq SoulcaliburVI.exe", "/NH"],
        check=True, capture_output=True, text=True,
    ).stdout.casefold().find("soulcaliburvi.exe") >= 0:
        raise RuntimeError("SC6 is running; refusing to replace loaded DLLs")
    horse_source = BUILD / "HorseMod" / "HorseMod.dll"
    horse_destination = GAME_ROOT / "ue4ss" / "Mods" / "HorseMod" / "dlls" / "main.dll"
    replay_source = BUILD / "HorseMod" / "ReplayQualificationMod.dll"
    if not horse_source.is_file() or not replay_source.is_file():
        raise FileNotFoundError("targeted qualification DLL build outputs are missing")
    horse_destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(horse_source, horse_destination)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Incremental deterministic build, parallel tests, and deploy"
    )
    parser.add_argument("--freeze-source-identity", action="store_true",
        help="configure once after committing so embedded source identity changes")
    parser.add_argument("--skip-tests", action="store_true")
    parser.add_argument("--deploy", action="store_true")
    parser.add_argument("-j", "--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    build(freeze_source_identity=args.freeze_source_identity, jobs=args.jobs)
    if not args.skip_tests:
        test_parallel(jobs=args.jobs)
    if args.deploy:
        deploy()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
