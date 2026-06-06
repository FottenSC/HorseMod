from __future__ import annotations

from pathlib import Path

import pytest

from bank_resolver import BankResolutionContext
from luxformats import FLuxMoveBankSlotView, KhdFile, parse_khd, parse_mot
from move_graph import SlotEdge, SlotGraph
from recovery_trust import classify_recovery_from_slot


pytestmark = pytest.mark.needs_dump


def _edge(src: int, dst_bank: int, dst: int, first: int) -> SlotEdge:
    return SlotEdge(
        src_slot=src,
        dst_slot=dst,
        dst_bank=dst_bank,
        raw_move_id=(dst_bank << 12) | dst,
        predicate_text=f"frame [{first}..{first}]",
        predicate_kind="frame",
        predicate_sub_op=None,
        predicate_args=[first, first],
        is_indirect=False,
        source_pc=0,
        callcond_idx=0,
    )


def _ctx(bank: KhdFile) -> BankResolutionContext:
    return BankResolutionContext(
        khd_by_cid={"001": bank},
        mot_by_cid={"001": parse_mot(Path("E:/myMods/dump/Battle/mot/chr001.mot").read_bytes())},
        character_names={"001": "Mitsurugi"},
        khd_paths_by_cid={"001": Path("E:/myMods/dump/Battle/hdr/hdr001.khd")},
        mot_paths_by_cid={"001": Path("E:/myMods/dump/Battle/mot/chr001.mot")},
        confirmed_bank_map={},
    )


def test_frame_edge_to_neutral_no_cell_target_confirms_recovery():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr001.khd").read_bytes())
    graph = SlotGraph(edges_by_src={263: [_edge(263, 0, 0, 18)]})

    trust = classify_recovery_from_slot(
        cid="001",
        character="Mitsurugi",
        movement_type="backstep_candidate",
        slot_index=263,
        bank=bank,
        graph=graph,
        neutral_sources={0},
        stance_sources=set(),
        bank_ctx=_ctx(bank),
    )

    assert trust.status == "confirmed_static_recovery"
    assert trust.earliest_return_or_guard_frame == 18
    assert trust.edges[0].recovery_role == "return_to_neutral"


def test_frame_edge_to_stance_root_confirms_stance_recovery():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr001.khd").read_bytes())
    graph = SlotGraph(edges_by_src={263: [_edge(263, 0, 10, 21)]})

    trust = classify_recovery_from_slot(
        cid="001",
        character="Mitsurugi",
        movement_type="sidestep_up_candidate",
        slot_index=263,
        bank=bank,
        graph=graph,
        neutral_sources=set(),
        stance_sources={10},
        bank_ctx=_ctx(bank),
    )

    assert trust.status == "confirmed_static_stance_recovery"
    assert trust.earliest_stance_return_frame == 21


def test_cross_bank_recovery_edge_stays_unresolved():
    bank = parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr001.khd").read_bytes())
    graph = SlotGraph(edges_by_src={263: [_edge(263, 4, 0, 12)]})

    trust = classify_recovery_from_slot(
        cid="001",
        character="Mitsurugi",
        movement_type="backstep_candidate",
        slot_index=263,
        bank=bank,
        graph=graph,
        neutral_sources={0},
        stance_sources=set(),
        bank_ctx=_ctx(bank),
    )

    assert trust.status == "unresolved_cell_semantics"
    assert trust.earliest_unresolved_frame == 12


def test_no_frame_edges_reports_unknown():
    bank = KhdFile(
        magic=b"KHD\x00",
        field_0c=0,
        move_count=0,
        movelist_id=0,
        section_offsets=[],
        trailer_data=b"",
        sections=[],
        raw=b"",
        slots=[FLuxMoveBankSlotView(slot_index=0, bank_offset=0)],
    )
    graph = SlotGraph(edges_by_src={0: []})

    trust = classify_recovery_from_slot(
        cid="001",
        character="Mitsurugi",
        movement_type="backstep_candidate",
        slot_index=0,
        bank=bank,
        graph=graph,
        neutral_sources={0},
        stance_sources=set(),
        bank_ctx=_ctx(parse_khd(Path("E:/myMods/dump/Battle/hdr/hdr001.khd").read_bytes())),
    )

    assert trust.status == "unknown_from_static_bytecode"
    assert trust.confidence == "unknown"
