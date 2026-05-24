"""Integration tests on real character KHDs.

These lock in the empirical behaviour we depend on: BFS coverage,
stance-root identification, the fallback "unknown" enumeration. Several
prior bugs (`?A`/`?B` fake labels, the 11-moves-Astaroth regression,
the missing slot-401 chain) would have been caught here.
"""
from __future__ import annotations

import pytest

from move_graph import (
    USER_INPUT_KINDS,
    build_flat_moves,
    build_slot_graph,
    identify_stance_roots,
)

pytestmark = pytest.mark.needs_dump


# ---------------------------------------------------------------------------
# Slot graph
# ---------------------------------------------------------------------------

def test_mitsurugi_slot_graph_has_edges(mitsurugi_graph, mitsurugi_bank):
    g = mitsurugi_graph
    # We expect ~1200+ extracted edges across Mitsurugi's 2899 slots.
    total = sum(len(es) for es in g.edges_by_src.values())
    assert total > 1000, f"only {total} edges extracted — emulator regression?"


def test_packed_bucket_edges_resolve_before_user_in_count(mitsurugi_graph):
    # Ghidra's LuxMoveVM_ResolveBankSlot proves dst_bank is an internal
    # FLuxMoveBank bucket, not an external KHD file. Bucketed edges can
    # contribute to the resolved local slot's incoming count.
    g = mitsurugi_graph
    for slot_idx, count in g.user_in_count.items():
        in_edges = [e for e in g.edges_by_dst.get(slot_idx, [])
                    if e.predicate_kind in USER_INPUT_KINDS]
        assert len(in_edges) == count
    assert any(e.dst_bank != 0 for edges in g.edges_by_dst.values() for e in edges)


# ---------------------------------------------------------------------------
# Stance roots
# ---------------------------------------------------------------------------

def test_stance_roots_skip_sentinels(mitsurugi_bank, mitsurugi_graph):
    roots = identify_stance_roots(mitsurugi_bank, mitsurugi_graph)
    # No identified root should have anim 0xFFFF — sentinels would pollute
    # the stance picker UI.
    for r in roots:
        assert r.anim_index != 0xFFFF, f"sentinel slot {r.slot_idx} leaked"


def test_stance_roots_find_neutral(mitsurugi_bank, mitsurugi_graph):
    roots = identify_stance_roots(mitsurugi_bank, mitsurugi_graph)
    # Mitsurugi's neutral standing is slot 406 (anim 186). It should
    # rank in the top few candidates by distinct user inputs.
    top_slots = [r.slot_idx for r in roots[:5]]
    assert 406 in top_slots, f"slot 406 (Mitsurugi neutral) not in top roots: {top_slots}"


# ---------------------------------------------------------------------------
# Flat moves enumeration — coverage targets
# ---------------------------------------------------------------------------

def test_mitsurugi_flat_moves_count(mitsurugi_bank, mitsurugi_graph):
    roots = identify_stance_roots(mitsurugi_bank, mitsurugi_graph)
    moves = build_flat_moves(mitsurugi_bank, mitsurugi_graph, roots)
    # Mitsurugi has ~160-170 distinct attack-cell-bearing slots.
    # If we drop below 130 something broke (likely the cell-role filter).
    assert 130 <= len(moves) <= 200, f"unexpected move count {len(moves)}"


def test_no_synthetic_fallback_inputs(mitsurugi_bank, mitsurugi_graph):
    """Regression: we used to fabricate '?A' / '?B' labels from the
    button-class enum on cell.wU16InputCond. After learning that the
    field is NOT a button mask (it's a move-class enum), fallback moves
    should have empty input lists."""
    roots = identify_stance_roots(mitsurugi_bank, mitsurugi_graph)
    moves = build_flat_moves(mitsurugi_bank, mitsurugi_graph, roots)
    for m in moves:
        if m.kind_path and m.kind_path[0] == "unknown":
            assert m.input_path == [], (
                f"fallback move slot {m.slot_idx} still has synthetic input "
                f"{m.input_path!r}"
            )


def test_slot_401_reaches_via_orphan_seed(mitsurugi_bank, mitsurugi_graph):
    """Regression: slot 401 (Mitsurugi mid 60dmg jump-launcher) is reached
    only through sentinel-anim 'trampoline' slot 2892. Without orphan
    seeding, BFS misses it entirely and the move shows as 'unknown'."""
    roots = identify_stance_roots(mitsurugi_bank, mitsurugi_graph)
    moves = build_flat_moves(mitsurugi_bank, mitsurugi_graph, roots)
    by_slot = {m.slot_idx: m for m in moves}
    assert 401 in by_slot, "slot 401 should be in flat moves"
    m = by_slot[401]
    assert m.input_path, f"slot 401 should have a known input chain, got {m}"
    # The expected chain is up-then-forward (numpad 9)
    assert any("up" in s for s in m.input_path), f"missing 'up' step: {m.input_path}"
    assert any("forward" in s for s in m.input_path), f"missing 'forward' step: {m.input_path}"


def test_fallback_moves_are_attack_role_only(mitsurugi_bank, mitsurugi_graph):
    """Regression: step 3 used to enumerate Header/Sentinel role cells.
    Only Attack-role cells should be fallback-emitted as moves."""
    roots = identify_stance_roots(mitsurugi_bank, mitsurugi_graph)
    moves = build_flat_moves(mitsurugi_bank, mitsurugi_graph, roots)
    for m in moves:
        if m.kind_path[0] != "unknown":
            continue
        cell = mitsurugi_bank.sections[0].entries[m.cell_idx]
        assert cell.cell_role == "Attack", (
            f"fallback move slot {m.slot_idx} cell #{m.cell_idx} has role "
            f"{cell.cell_role!r}, only Attack should be emitted"
        )


def test_astaroth_coverage_floor(astaroth_bank, astaroth_bytes):
    """Astaroth was the canary character — at one point he showed only
    11 moves. Pin the floor at 200 (it's currently ~238)."""
    g = build_slot_graph(astaroth_bank, astaroth_bytes)
    roots = identify_stance_roots(astaroth_bank, g)
    moves = build_flat_moves(astaroth_bank, g, roots)
    assert len(moves) >= 200, f"Astaroth move count regressed to {len(moves)}"


def test_known_input_rate(mitsurugi_bank, mitsurugi_graph):
    """At least 30% of moves should have a BFS-derived input. Was 14% at
    one point; should now be ~40%."""
    roots = identify_stance_roots(mitsurugi_bank, mitsurugi_graph)
    moves = build_flat_moves(mitsurugi_bank, mitsurugi_graph, roots)
    known = sum(1 for m in moves if m.kind_path[0] != "unknown")
    rate = known / len(moves)
    assert rate >= 0.30, (
        f"known-input rate dropped to {rate*100:.0f}% — BFS coverage "
        f"regression?"
    )
