#!/usr/bin/env python3
"""Resolve operator character/stage tokens for controlled SC6 launches.

Character numeric values are the raw SC6 character/setup indices consumed by
GetCharaCodeByCharaIndex. Stage numeric values are packed stage registry IDs.
The catalog is intentionally conservative: entries not proven playable by the
BattleSetup tables are rejected and DLC entries still require the native
runtime profile/registry checks before launch.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Iterable


@dataclass(frozen=True)
class LaunchCatalogEntry:
    numeric_id: int
    canonical_name: str
    canonical_code: str
    aliases: tuple[str, ...] = ()
    map_path: str = ""
    requires_dlc: bool = False


@dataclass(frozen=True)
class ResolvedLaunchToken:
    original_token: str
    numeric_id: int
    canonical_name: str
    canonical_code: str
    map_path: str = ""
    requires_dlc: bool = False


class LaunchSelectionError(ValueError):
    pass


CHARACTERS: tuple[LaunchCatalogEntry, ...] = (
    LaunchCatalogEntry(0, "Mitsurugi", "001", ("mitsu",)),
    LaunchCatalogEntry(1, "Seong Mi-na", "002", ("seongmina", "mina")),
    LaunchCatalogEntry(2, "Taki", "003"),
    LaunchCatalogEntry(3, "Maxi", "004"),
    LaunchCatalogEntry(4, "Voldo", "005"),
    LaunchCatalogEntry(5, "Sophitia", "006", ("sophie",)),
    LaunchCatalogEntry(6, "Siegfried", "007", ("sieg",)),
    LaunchCatalogEntry(7, "Ivy", "00B", ("isabellavalentine",)),
    LaunchCatalogEntry(8, "Kilik", "00C"),
    LaunchCatalogEntry(9, "Xianghua", "00D"),
    LaunchCatalogEntry(10, "Yoshimitsu", "00F", ("yoshi",)),
    LaunchCatalogEntry(11, "Nightmare", "011"),
    LaunchCatalogEntry(12, "Astaroth", "012"),
    LaunchCatalogEntry(13, "Cervantes", "014"),
    LaunchCatalogEntry(14, "Raphael", "015", ("raph",)),
    LaunchCatalogEntry(15, "Talim", "016"),
    LaunchCatalogEntry(16, "Tira", "023"),
    LaunchCatalogEntry(17, "Zasalamel", "024", ("zas",)),
    LaunchCatalogEntry(19, "Groh", "062", ("grøh",)),
    LaunchCatalogEntry(20, "Azwel", "064"),
    LaunchCatalogEntry(21, "Geralt", "065", ("geraltofrivia",)),
    LaunchCatalogEntry(28, "2B", "060", ("yorha2b",), requires_dlc=True),
    LaunchCatalogEntry(29, "Cassandra", "017", ("cass",), requires_dlc=True),
    LaunchCatalogEntry(30, "Amy", "030", requires_dlc=True),
    LaunchCatalogEntry(31, "Hilde", "028", ("hilda",), requires_dlc=True),
    LaunchCatalogEntry(32, "Haohmaru", "061", ("hao",), requires_dlc=True),
    LaunchCatalogEntry(33, "Setsuka", "022", requires_dlc=True),
    LaunchCatalogEntry(34, "Hwang", "009", requires_dlc=True),
)


def _stage(
    packed: int,
    code: str,
    map_path: str,
    *,
    dlc: bool = False,
) -> LaunchCatalogEntry:
    suffix = {
        "106": "STG006_R",
        "111": "STG011_R",
        "115": "STG015_R",
        "201": "STG001_V",
        "206": "STG006_V",
        "217": "STG017_V",
        "311": "STG011_R_V",
    }.get(code, f"STG{code}")
    return LaunchCatalogEntry(
        packed,
        suffix,
        code,
        (code, f"stage{code}"),
        map_path,
        dlc,
    )


STAGES: tuple[LaunchCatalogEntry, ...] = (
    *(_stage(i, f"{i:03X}", f"/Game/Stage/STG{i:03X}/Maps/STG{i:03X}")
      for i in (0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x008, 0x009,
                0x010, 0x011, 0x012)),
    _stage(0x014, "014", "/Game/DLC/01/Stage/STG014/Maps/STG014", dlc=True),
    _stage(0x015, "015", "/Game/DLC/07/Stage/STG015/Maps/STG015", dlc=True),
    _stage(0x016, "016", "/Game/DLC/08/Stage/STG016/Maps/STG016", dlc=True),
    _stage(0x017, "017", "/Game/DLC/11/Stage/STG017/Maps/STG017", dlc=True),
    _stage(0x018, "018", "/Game/DLC/13/Stage/STG018/Maps/STG018", dlc=True),
    _stage(0x106, "106", "/Game/DLC/07/Stage/STG006_R/Maps/STG006_R", dlc=True),
    _stage(0x111, "111", "/Game/DLC/07/Stage/STG011_R/Maps/STG011_R", dlc=True),
    _stage(0x115, "115", "/Game/DLC/07/Stage/STG015_R/Maps/STG015_R", dlc=True),
    _stage(0x201, "201", "/Game/DLC/13/Stage/STG001_V/STG001_V", dlc=True),
    _stage(0x206, "206", "/Game/DLC/13/Stage/STG006_V/STG006_V", dlc=True),
    _stage(0x217, "217", "/Game/DLC/13/Stage/STG017_V/STG017_V", dlc=True),
    _stage(0x311, "311", "/Game/DLC/13/Stage/STG011_R_V/STG011_R_V", dlc=True),
)


def normalize_alias(value: str) -> str:
    return re.sub(r"[\s_-]+", "", value.strip().casefold())


def _aliases(entry: LaunchCatalogEntry, *, stage: bool) -> Iterable[str]:
    yield entry.canonical_name
    yield entry.canonical_code
    yield from entry.aliases
    if stage:
        yield f"stg{entry.canonical_code}"
        yield entry.map_path
    else:
        yield f"chara{entry.canonical_code}"
        yield f"cid{entry.canonical_code}"


def _resolve(token: str, catalog: tuple[LaunchCatalogEntry, ...], *, kind: str) -> ResolvedLaunchToken:
    original = token
    token = token.strip()
    if not token:
        raise LaunchSelectionError(f"empty {kind} selection")
    numeric: int | None = None
    try:
        numeric = int(token, 0)
    except ValueError:
        if token.isdecimal():
            numeric = int(token, 10)
    if numeric is not None:
        matches = [entry for entry in catalog if entry.numeric_id == numeric]
    else:
        wanted = normalize_alias(token)
        matches = [
            entry for entry in catalog
            if wanted in {normalize_alias(alias) for alias in _aliases(
                entry, stage=kind == "stage")}
        ]
    if not matches:
        raise LaunchSelectionError(f"unknown or unavailable {kind}: {original!r}")
    if len(matches) != 1:
        names = ", ".join(entry.canonical_name for entry in matches)
        raise LaunchSelectionError(f"ambiguous {kind} {original!r}: {names}")
    entry = matches[0]
    return ResolvedLaunchToken(
        original, entry.numeric_id, entry.canonical_name,
        entry.canonical_code, entry.map_path, entry.requires_dlc,
    )


def resolve_character(token: str) -> ResolvedLaunchToken:
    return _resolve(token, CHARACTERS, kind="character")


def resolve_stage(token: str) -> ResolvedLaunchToken:
    return _resolve(token, STAGES, kind="stage")


def overlay_direct_selection(
    replay_selection: tuple[int, int, int],
    *,
    left: ResolvedLaunchToken | None = None,
    right: ResolvedLaunchToken | None = None,
    stage: ResolvedLaunchToken | None = None,
) -> tuple[int, int, int]:
    """Apply per-field operator precedence to (left, right, stage)."""
    replay_left, replay_right, replay_stage = replay_selection
    return (
        left.numeric_id if left else replay_left,
        right.numeric_id if right else replay_right,
        stage.numeric_id if stage else replay_stage,
    )
