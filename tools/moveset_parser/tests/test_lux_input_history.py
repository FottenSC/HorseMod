from __future__ import annotations

import pytest

from lux_input_history import (
    INPUT_HISTORY_CAPACITY,
    CurrentInputSnapshot,
    InputAdmissionEvidence,
    InputHistoryEntry,
    InputHistoryRing,
    TransitionConditionRow,
    check_history_condition,
    check_motion_condition_flags,
)
from lux_reference_engine import StaticResolutionError


def test_native_motion_sentinels_and_generic_masks() -> None:
    assert check_motion_condition_flags(0x8000, 0x0000)
    assert not check_motion_condition_flags(0x8000, 0x0001)
    assert check_motion_condition_flags(0x8001, 0x0008)
    assert not check_motion_condition_flags(0x8001, 0x0020)
    assert check_motion_condition_flags(0x8002, 0)
    assert check_motion_condition_flags(0x0308, 0x000B)
    assert not check_motion_condition_flags(0x0308, 0x0003)


def test_each_history_selector_uses_its_proven_field() -> None:
    entry = InputHistoryEntry(
        current_compact_word=0x0003,
        side_decoded_input_id=7,
        side_direction_mask=0x20,
        decoded_high_nibble_input_id=9,
        high_input_nibble=0x40,
    )
    assert check_history_condition(entry, 0x1007)
    assert check_history_condition(entry, 0x1005)  # wildcard
    assert check_history_condition(entry, 0x100A)  # nonzero
    assert check_history_condition(entry, 0x2009)
    assert check_history_condition(entry, 0x3020)
    assert check_history_condition(entry, 0x4040)
    assert check_history_condition(entry, 0x0301)


def test_ring_reset_is_exactly_0x140_zero_rows() -> None:
    ring = InputHistoryRing(
        entries=[InputHistoryEntry(current_compact_word=1)] * INPUT_HISTORY_CAPACITY,
        cursor=0x13F,
        tick_count=123,
    )
    ring.reset()
    assert ring.cursor == 0
    assert ring.tick_count == 0
    assert len(ring.entries) == 0x140
    assert all(entry == InputHistoryEntry() for entry in ring.entries)


def test_snapshot_commit_preincrements_cursor_and_projects_exact_six_fields() -> None:
    ring = InputHistoryRing()
    snapshot = CurrentInputSnapshot(
        current_compact_word=0x1234,
        previous_compact_word=0xDEADBEEF,
        secondary_compact_word=0x5678,
        decoded_high_nibble_input_id=0xAAAA0009,
        previous_decoded_high_nibble_input_id=0x11111111,
        high_input_nibble=0x0040,
        decoded_secondary_high_nibble_input_id=0x22222222,
        secondary_high_input_nibble=0x33333333,
        side_decoded_input_id=0xBBBB0007,
        previous_side_decoded_input_id=0x44444444,
        side_direction_mask=0x0020,
        side_decoded_secondary_input_id=0x55555555,
        side_secondary_direction_mask=0x66666666,
        guard_input_gate=0x77777777,
    )

    entry = ring.commit_snapshot(snapshot)

    assert ring.cursor == 1
    assert ring.tick_count == 1
    assert ring.entries[0] == InputHistoryEntry()
    assert entry == InputHistoryEntry(
        current_compact_word=0x1234,
        secondary_compact_word=0x5678,
        side_decoded_input_id=7,
        side_direction_mask=0x20,
        decoded_high_nibble_input_id=9,
        high_input_nibble=0x40,
    )
    assert ring.entries[1] == entry


def test_snapshot_commit_wraps_0x13f_to_zero_and_uint_tick_count() -> None:
    ring = InputHistoryRing(cursor=0x13F, tick_count=0xFFFFFFFF)
    entry = ring.commit_snapshot(CurrentInputSnapshot(current_compact_word=8))
    assert ring.cursor == 0
    assert ring.tick_count == 0
    assert ring.entries[0] == entry


def test_transition_scan_wraps_from_zero_to_0x13f() -> None:
    entries = [InputHistoryEntry()] * INPUT_HISTORY_CAPACITY
    entries[0] = InputHistoryEntry(current_compact_word=0)
    entries[-1] = InputHistoryEntry(current_compact_word=1)
    ring = InputHistoryRing(entries=entries, cursor=0)
    row = TransitionConditionRow(0, 0x0001, 0, 2)
    assert ring.evaluate_transition((row,), condition_mode=0, age_limit=2)


def test_mode_one_uses_first_row_pair_while_seeking_later_rows() -> None:
    entries = [InputHistoryEntry()] * INPUT_HISTORY_CAPACITY
    entries[4] = InputHistoryEntry(current_compact_word=0)
    entries[3] = InputHistoryEntry(current_compact_word=1)
    ring = InputHistoryRing(entries=entries, cursor=4)
    rows = (
        TransitionConditionRow(0, 0x8000, 0, 2),
        TransitionConditionRow(0, 0x0001, 0, 2),
    )
    assert ring.evaluate_transition(rows, condition_mode=1, age_limit=4)


def test_untransformed_requested_input_is_not_called_admitted() -> None:
    evidence = InputAdmissionEvidence(4, 0x0400, None, ())
    with pytest.raises(StaticResolutionError, match="cannot be reported as admitted"):
        evidence.require_transformed()
