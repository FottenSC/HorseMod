#!/usr/bin/env python3
from sc6_launch_catalog import (
    LaunchSelectionError,
    normalize_alias,
    overlay_direct_selection,
    resolve_character,
    resolve_stage,
)
from rollback_two_client_test_run import request_text


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
    request = request_text(
        enabled=True, trace=True, case="live-online-capture",
        request_id="selection-selftest", rollback_window=12,
        seed="0x5C6B0001", launch_left_character=2,
        launch_right_character=6, launch_stage=9,
    )
    assert "launch_left_character=2\n" in request
    assert "launch_right_character=6\n" in request
    assert "launch_stage=9\n" in request
    reject(resolve_character, "edge master")
    reject(resolve_character, "0xff")
    reject(resolve_stage, "0")
    reject(resolve_stage, "STG007")
    print("sc6_launch_catalog_selftest: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
