#!/usr/bin/env python3
from sc6_launch_catalog import (
    LaunchSelectionError,
    normalize_alias,
    overlay_direct_selection,
    resolve_character,
    resolve_stage,
)


def reject(fn, token: str) -> None:
    try:
        fn(token)
    except LaunchSelectionError:
        return
    raise AssertionError(f"expected rejection: {token!r}")


def main() -> int:
    assert normalize_alias("Seong Mi-na") == "seongmina"
    assert resolve_character("MITSU").numeric_id == 0
    assert resolve_character("Seong_Mi-Na").canonical_code == "002"
    assert resolve_character("0x5").canonical_name == "Sophitia"
    assert resolve_character("28").requires_dlc
    assert resolve_stage("STG-009").numeric_id == 0x009
    assert resolve_stage("0x10").canonical_code == "010"
    assert resolve_stage("stage_111").map_path.endswith("STG011_R/Maps/STG011_R")
    replay = (0, 5, 1)
    assert overlay_direct_selection(
        replay, right=resolve_character("Siegfried")
    ) == (0, 6, 1)
    assert overlay_direct_selection(
        replay, left=resolve_character("Taki"), stage=resolve_stage("STG009")
    ) == (2, 5, 9)
    reject(resolve_character, "edge master")
    reject(resolve_character, "0xff")
    reject(resolve_stage, "0")
    reject(resolve_stage, "STG007")
    print("sc6_launch_catalog_selftest: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
