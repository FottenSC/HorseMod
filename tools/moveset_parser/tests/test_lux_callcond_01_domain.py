from pathlib import Path

from lux_callcond_01_domain import build_pair_domain


ROOT = Path(__file__).resolve().parents[3]


def test_raphael_maxi_callcond_01_domain_is_closed() -> None:
    domain = build_pair_domain(
        (
            ROOT / "dump/Battle/hdr/hdr014.khd",
            ROOT / "dump/Battle/hdr/hdr003.khd",
        )
    )

    assert domain["qualification"] == "static-incomplete"
    assert domain["first_word_qualification"] == "authored-domain-complete"
    assert domain["pair"]["site_count"] == 23_288
    assert domain["pair"]["argument_counts"] == {
        1: 9_825,
        2: 9_505,
        3: 3_638,
        4: 196,
        5: 124,
    }
    assert len(domain["pair"]["concrete_first_words"]) == 137
    assert 0x139C in domain["pair"]["concrete_first_words"]
    assert domain["pair"]["first_word_blockers"] == []
    assert domain["pair"]["blockers"]

    dynamic = [site for bank in domain["banks"] for site in bank["dynamic_sites"]]
    assert len(dynamic) == 2
    assert {tuple(site["concrete_first_words"]) for site in dynamic} == {
        (0x138D, 0x139C)
    }
    assert {len(site["callers"]) for site in dynamic} == {3}
