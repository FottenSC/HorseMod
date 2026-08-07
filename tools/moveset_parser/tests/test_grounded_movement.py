from pathlib import Path

import pytest

from grounded_movement import analyze_ground_rolls
from luxformats import parse_khd, parse_mot


pytestmark = pytest.mark.needs_dump


def _load(cid: str):
    root = Path("E:/myMods/dump/Battle")
    return (
        parse_khd((root / "hdr" / f"hdr{cid}.khd").read_bytes()),
        parse_mot((root / "mot" / f"chr{cid}.mot").read_bytes()),
        parse_mot((root / "mot" / "chr000.mot").read_bytes()),
    )


def test_standard_ground_roll_routes_and_distances():
    routes = analyze_ground_rolls(*_load("001"))

    assert routes["forward"].head_end == pytest.approx(1.7, abs=0.001)
    assert routes["forward"].feet_end == pytest.approx(1.7, abs=0.001)
    assert routes["backward"].head_end == pytest.approx(2.0, abs=0.001)
    assert routes["backward"].feet_end == pytest.approx(1.829, abs=0.001)
    assert routes["right"].head_end == pytest.approx(1.3, abs=0.001)
    assert routes["left"].feet_end == pytest.approx(1.3, abs=0.001)


def test_voldo_uses_the_long_back_roll_in_both_orientations():
    routes = analyze_ground_rolls(*_load("005"))

    assert routes["backward"].head_end == pytest.approx(2.0, abs=0.001)
    assert routes["backward"].feet_end == pytest.approx(2.0, abs=0.001)


def test_character_local_and_alternate_common_side_rolls_keep_1_3m_travel():
    mina = analyze_ground_rolls(*_load("002"))
    astaroth = analyze_ground_rolls(*_load("012"))

    for routes in (mina, astaroth):
        assert routes["right"].head_end == pytest.approx(1.3, abs=0.001)
        assert routes["right"].feet_end == pytest.approx(1.3, abs=0.001)
        assert routes["left"].head_end == pytest.approx(1.3, abs=0.001)
        assert routes["left"].feet_end == pytest.approx(1.3, abs=0.001)
