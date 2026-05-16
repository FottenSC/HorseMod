"""Shared pytest fixtures.

The test data is the actual SC6 dump at `E:\\myMods\\dump\\Battle`.
Tests that need a parsed KHD use the `mitsurugi_bank` fixture, which
parses `hdr001.khd` once per session.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

# Make the project root importable so tests can `from stackvm import ...`
# regardless of pytest's cwd.
PROJECT_ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

DUMP_ROOT = Path("E:/myMods/dump/Battle/hdr")


def _khd_path(cid: str) -> Path:
    return DUMP_ROOT / f"hdr{cid}.khd"


def pytest_collection_modifyitems(config, items):
    """Skip tests that need the SC6 dump if it's not present."""
    if not DUMP_ROOT.exists():
        skip = pytest.mark.skip(reason="SC6 dump not present at " + str(DUMP_ROOT))
        for it in items:
            if "needs_dump" in it.keywords:
                it.add_marker(skip)


@pytest.fixture(scope="session")
def mitsurugi_bytes() -> bytes:
    path = _khd_path("001")
    if not path.exists():
        pytest.skip(f"{path} missing")
    return path.read_bytes()


@pytest.fixture(scope="session")
def mitsurugi_bank(mitsurugi_bytes):
    import luxformats as lf
    return lf.parse_khd(mitsurugi_bytes)


@pytest.fixture(scope="session")
def astaroth_bytes() -> bytes:
    path = _khd_path("00b")
    if not path.exists():
        pytest.skip(f"{path} missing")
    return path.read_bytes()


@pytest.fixture(scope="session")
def astaroth_bank(astaroth_bytes):
    import luxformats as lf
    return lf.parse_khd(astaroth_bytes)


@pytest.fixture(scope="session")
def mitsurugi_graph(mitsurugi_bank, mitsurugi_bytes):
    from move_graph import build_slot_graph
    return build_slot_graph(mitsurugi_bank, mitsurugi_bytes)
