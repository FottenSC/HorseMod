from __future__ import annotations

from copy import deepcopy
import threading

import pytest

from tools.deterministic_qualification.observer_pair import (
    ObserverPeerPaths,
    create_host_room_suppression,
    create_match_setup_request,
    stop_observer_processes,
    validate_host_room_suppression,
    validate_observer_reports,
)
from tools.deterministic_qualification.process_control import GameProcess


HOST_ID = 76561198070521860
CLIENT_ID = 76561198201141039


def test_paired_teardown_starts_both_graceful_closes_together(monkeypatch):
    rendezvous = threading.Barrier(2)
    started: set[int] = set()

    def close(pid: int) -> None:
        started.add(pid)
        rendezvous.wait(timeout=1)

    monkeypatch.setattr(
        "tools.deterministic_qualification.observer_pair.close_game", close)
    monkeypatch.setattr(
        "tools.deterministic_qualification.observer_pair.list_game_processes",
        lambda: (),
    )
    assert stop_observer_processes(
        (GameProcess(1, "host"), GameProcess(2, "sandbox"))) is True
    assert started == {1, 2}


def test_development_teardown_records_emergency_cleanup(monkeypatch):
    forced: list[int] = []

    def fail_close(_pid: int) -> None:
        raise TimeoutError("still active")

    monkeypatch.setattr(
        "tools.deterministic_qualification.observer_pair.close_game", fail_close)
    monkeypatch.setattr(
        "tools.deterministic_qualification.observer_pair.force_stop_game_for_cleanup",
        forced.append,
    )
    monkeypatch.setattr(
        "tools.deterministic_qualification.observer_pair.list_game_processes",
        lambda: (),
    )
    assert stop_observer_processes(
        (GameProcess(7, "host"),), require_graceful=False) is False
    assert forced == [7]


def test_sandbox_room_suppression_shadows_host_request_without_arming(tmp_path):
    peer = ObserverPeerPaths(
        mods_root=tmp_path / "mods",
        horsemod_dll=tmp_path / "horsemod.dll",
        config=tmp_path / "rollback.ini",
        qualification_root=tmp_path / "qualification",
        log=tmp_path / "UE4SS.log",
    )
    create_host_room_suppression(peer, "observer-test")
    request = (peer.qualification_root / "online_room_request.txt").read_text(
        encoding="utf-8")
    assert "request_type=host_room_create\n" in request
    assert "run_id=observer-test\n" in request
    assert request.endswith("arm=false\n")
    validate_host_room_suppression(peer, "observer-test")

    (peer.qualification_root / "online_room_report.json").write_text(
        "{}", encoding="utf-8")
    with pytest.raises(RuntimeError, match="forbidden host-room automation"):
        validate_host_room_suppression(peer, "observer-test")


def test_match_setup_request_binds_exact_pair_and_content(tmp_path):
    peer = ObserverPeerPaths(
        mods_root=tmp_path / "mods",
        horsemod_dll=tmp_path / "horsemod.dll",
        config=tmp_path / "rollback.ini",
        qualification_root=tmp_path / "qualification",
        log=tmp_path / "UE4SS.log",
    )
    create_match_setup_request(
        peer, "setup-test", "sandbox", 123456, CLIENT_ID, HOST_ID,
        ["012", "015"], "273", "111", "STG011_R",
        "Silver Wolves' Haven",
    )
    assert (peer.qualification_root / "online_room_request.txt").read_text(
        encoding="utf-8") == (
            "version=2\nrequest_type=match_setup\nrun_id=setup-test\n"
            "arm=true\nrole=sandbox\nlobby_id=123456\n"
            f"local_steam_id={CLIENT_ID}\npeer_steam_id={HOST_ID}\n"
            "fighter_left=012\nfighter_right=015\nstage_code=273\n"
            "authored_stage_code=111\n"
            "ui_stage_code=STG011_R\n"
            "display_map_name=Silver Wolves' Haven\n"
        )


def report(role: int, local_id: int) -> dict[str, object]:
    return {
        "schema_version": 1,
        "kind": "online_observer_only",
        "run_id": "observer-test",
        "state": 2,
        "failure": "none",
        "session": {
            "role": role,
            "virtual_state": 4,
            "local_slot": role,
            "lobby_id": 1097752412345,
            "session_name": 101,
            "session_interface": 102,
            "active_connect": 103,
            "online_session": 104,
            "named_session": 105,
            "session_info": 106,
        },
        "lobby": {
            "local_steam_id": local_id,
            "members": [HOST_ID, CLIENT_ID],
            "member_count": 2,
            "casual_player_match": True,
        },
        "content": {
            "fighters": ["rap", "max"],
            "stage_code": "009",
            "stage_package": "/Game/Stage/STG009",
            "stage_display_name": "Snow-Capped Showdown",
            "loaded_package_identity": "12" * 32,
            "battle_sync_object": 201,
            "characters_received": True,
            "stage_received": True,
        },
    }


def test_observer_pair_requires_complete_complementary_native_contracts() -> None:
    validated = validate_observer_reports(
        report(0, HOST_ID),
        report(1, CLIENT_ID),
        HOST_ID,
        CLIENT_ID,
        "/Game/Stage/STG009",
        "Snow-Capped Showdown",
    )
    assert validated["map"]["display_name"] == "Snow-Capped Showdown"
    assert {peer["role"] for peer in validated["peers"]} == {0, 1}


@pytest.mark.parametrize(
    ("section", "field", "value"),
    [
        ("session", "online_session", 0),
        ("session", "virtual_state", 5),
        ("lobby", "members", [HOST_ID, HOST_ID]),
        ("content", "characters_received", False),
        ("content", "stage_display_name", "009"),
        ("content", "loaded_package_identity", "0" * 64),
    ],
)
def test_observer_pair_fails_closed_on_incomplete_identity(
    section: str, field: str, value: object
) -> None:
    host = report(0, HOST_ID)
    sandbox = report(1, CLIENT_ID)
    changed = deepcopy(host)
    changed[section][field] = value
    with pytest.raises(RuntimeError):
        validate_observer_reports(
            changed,
            sandbox,
            HOST_ID,
            CLIENT_ID,
            "/Game/Stage/STG009",
            "Snow-Capped Showdown",
        )
