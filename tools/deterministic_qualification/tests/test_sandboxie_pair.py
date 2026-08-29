from pathlib import Path

import pytest

from tools.deterministic_qualification.sandboxie_pair import (
    SandboxiePairSpec,
    classify_game_processes,
    parse_sandbox_pid_listing,
    require_isolated_paths,
)


def test_pair_commands_preserve_production_steam_launch() -> None:
    spec = SandboxiePairSpec(
        box_name="sc6_peer",
        sandboxie_start=Path(r"C:\Sandboxie\Start.exe"),
        steam_executable=Path(r"C:\Steam\steam.exe"),
        game_executable=Path(r"E:\SC6\SoulcaliburVI.exe"),
    )
    assert spec.host_command() == (
        r"C:\Steam\steam.exe",
        "-applaunch",
        "544750",
    )
    assert spec.sandbox_command() == (
        r"C:\Sandboxie\Start.exe",
        "/box:sc6_peer",
        r"E:\SC6\SoulcaliburVI.exe",
        "-QueryPort=27012",
    )


@pytest.mark.parametrize("box_name", ["", "owner\\box", "box name", "../box"])
def test_pair_rejects_unsafe_box_names(box_name: str) -> None:
    with pytest.raises(ValueError):
        SandboxiePairSpec(box_name=box_name).validate()


def test_sandbox_listing_and_role_classification_are_exact() -> None:
    sandbox_pids = set(parse_sandbox_pid_listing("3\n20\n30\n40\n"))
    pair = classify_game_processes({10, 30}, sandbox_pids)
    assert pair.host_pid == 10
    assert pair.sandbox_pid == 30


@pytest.mark.parametrize(
    "listing",
    ["", "two\n10\n20\n", "2\n10\n", "2\n10\n10\n", "1\n0\n"],
)
def test_sandbox_listing_rejects_malformed_results(listing: str) -> None:
    with pytest.raises(RuntimeError):
        parse_sandbox_pid_listing(listing)


def test_isolation_requires_four_distinct_paths(tmp_path: Path) -> None:
    require_isolated_paths(
        tmp_path / "host",
        tmp_path / "sandbox",
        tmp_path / "host.log",
        tmp_path / "sandbox.log",
    )
    with pytest.raises(RuntimeError):
        require_isolated_paths(
            tmp_path / "host",
            tmp_path / "sandbox",
            tmp_path / "shared.log",
            tmp_path / "shared.log",
        )
