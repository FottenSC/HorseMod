"""Native-equivalent Lux command-history predicates and ring scanning.

This module models both the six-ushort history entry and the native commit from
``FLuxCharaCurrentInputSnapshot``.  The preceding raw-input-to-snapshot
transaction is lifted in :mod:`lux_input_pipeline`; admission evidence still
requires the resulting typed history entry so a requested input can never be
reported as accepted before the complete source/transform/codec path runs.

Static sources:

* ``LuxBattle_CheckMotionConditionFlags @ 0x140312E30``
* ``LuxBattle_CheckInputHistoryMotionCondition @ 0x140312EE0``
* ``LuxBattle_EvaluateMoveTransitionConditions @ 0x140312F80``
* ``LuxMoveVM_ResetInputHistoryRing @ 0x140312DF0``
* ``LuxBattle_TickCharaInput @ 0x140312510``
* history commit in ``LuxBattle_TickCharaMainSimulation @ 0x14034DD52``
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_reference_engine import StaticResolutionError, u16


INPUT_HISTORY_CAPACITY = 0x140


@dataclass(frozen=True)
class CurrentInputSnapshot:
    """Typed 0x40-byte snapshot projection used by the history commit.

    Only the proven producer/consumer fields are represented.  The compact
    input fields are full native dwords even though the history commit copies
    only their low ushorts.  Reserved and still-unresolved dwords remain
    explicit so callers cannot accidentally infer semantics from adjacency.
    """

    current_compact_word: int = 0
    previous_compact_word: int = 0
    secondary_compact_word: int = 0
    decoded_high_nibble_input_id: int = 0
    previous_decoded_high_nibble_input_id: int = 0
    high_input_nibble: int = 0
    decoded_secondary_high_nibble_input_id: int = 0
    secondary_high_input_nibble: int = 0
    side_decoded_input_id: int = 0
    previous_side_decoded_input_id: int = 0
    side_direction_mask: int = 0
    side_decoded_secondary_input_id: int = 0
    side_secondary_direction_mask: int = 0
    guard_input_gate: int = 0

    def __post_init__(self) -> None:
        ushort_fields = ("high_input_nibble", "side_direction_mask")
        for name in ushort_fields:
            value = getattr(self, name)
            if not 0 <= value <= 0xFFFF:
                raise ValueError(f"{name} must be a ushort, got {value}")
        for name, value in self.__dict__.items():
            if name not in ushort_fields and not 0 <= value <= 0xFFFFFFFF:
                raise ValueError(f"{name} must be a uint, got {value}")

    def history_entry(self) -> "InputHistoryEntry":
        """Project the exact six native ushort loads at 0x14034DD7F..DDC1."""
        return InputHistoryEntry(
            current_compact_word=self.current_compact_word,
            secondary_compact_word=self.secondary_compact_word,
            side_decoded_input_id=u16(self.side_decoded_input_id),
            side_direction_mask=self.side_direction_mask,
            decoded_high_nibble_input_id=u16(self.decoded_high_nibble_input_id),
            high_input_nibble=self.high_input_nibble,
        )


@dataclass(frozen=True)
class InputHistoryEntry:
    current_compact_word: int = 0
    secondary_compact_word: int = 0
    side_decoded_input_id: int = 0
    side_direction_mask: int = 0
    decoded_high_nibble_input_id: int = 0
    high_input_nibble: int = 0

    def __post_init__(self) -> None:
        for name, value in self.__dict__.items():
            if not 0 <= value <= 0xFFFF:
                raise ValueError(f"{name} must be a ushort, got {value}")


@dataclass(frozen=True)
class TransitionConditionRow:
    repeat_count: int
    condition_word_a: int
    condition_word_b: int
    initial_scan_window: int


def check_motion_condition_flags(pattern: int, history_word: int) -> bool:
    pattern = u16(pattern)
    history_word = u16(history_word)
    if pattern == 0x8000:
        return (history_word & 0x000F) == 0
    if pattern == 0x8001:
        return (history_word & 0x000F) != 0
    if pattern == 0x8002:
        return True
    required_all = (pattern >> 8) & 0x000F
    if required_all and (history_word & required_all) != required_all:
        return False
    required_any = pattern & 0x002F
    return not required_any or (history_word & pattern) != 0


def _decoded_id_matches(actual: int, selector: int) -> bool:
    operand = selector & 0x0FFF
    if operand == 0:
        return actual == 0
    if operand == 5:
        return True
    if operand == 10:
        return actual != 0
    return actual == operand


def check_history_condition(entry: InputHistoryEntry, condition_word: int) -> bool:
    condition_word &= 0xFFFFFFFF
    if condition_word == 0:
        return True
    selector = condition_word & 0xF000
    low_word = condition_word & 0xFFFF
    if selector == 0x1000:
        return _decoded_id_matches(entry.side_decoded_input_id, low_word)
    if selector == 0x2000:
        return _decoded_id_matches(entry.decoded_high_nibble_input_id, low_word)
    if selector == 0x3000:
        return (entry.side_direction_mask & low_word) != 0
    if selector == 0x4000:
        return (entry.high_input_nibble & low_word) != 0
    return check_motion_condition_flags(low_word, entry.current_compact_word)


@dataclass
class InputHistoryRing:
    entries: list[InputHistoryEntry] = field(
        default_factory=lambda: [InputHistoryEntry()] * INPUT_HISTORY_CAPACITY
    )
    cursor: int = 0
    tick_count: int = 0

    def __post_init__(self) -> None:
        if len(self.entries) != INPUT_HISTORY_CAPACITY:
            raise ValueError("Lux input-history ring must contain exactly 0x140 entries")
        if not 0 <= self.cursor < INPUT_HISTORY_CAPACITY:
            raise ValueError("Lux input-history cursor must be in 0..0x13F")

    def reset(self) -> None:
        self.entries[:] = [InputHistoryEntry()] * INPUT_HISTORY_CAPACITY
        self.cursor = 0
        self.tick_count = 0

    def commit_snapshot(self, snapshot: CurrentInputSnapshot) -> InputHistoryEntry:
        """Mirror the pre-increment, wrap, count, and six-field native commit."""
        self.cursor += 1
        if self.cursor >= INPUT_HISTORY_CAPACITY:
            self.cursor = 0
        self.tick_count = (self.tick_count + 1) & 0xFFFFFFFF
        entry = snapshot.history_entry()
        self.entries[self.cursor] = entry
        return entry

    @staticmethod
    def previous(index: int) -> int:
        return INPUT_HISTORY_CAPACITY - 1 if index == 0 else index - 1

    def evaluate_transition(
        self,
        rows: tuple[TransitionConditionRow, ...],
        *,
        condition_mode: int,
        age_limit: int,
    ) -> bool:
        """Mirror the native mode 0/1/2 backward row-chain scan."""
        if condition_mode not in (0, 1, 2):
            raise StaticResolutionError(
                f"unresolved transition condition mode {condition_mode}"
            )
        if age_limit < 1:
            return not rows
        history_index = self.cursor
        remaining_age = age_limit
        scan_remaining = rows[0].initial_scan_window if rows else 0
        initial_word_a = rows[0].condition_word_a if rows else 0
        initial_word_b = rows[0].condition_word_b if rows else 0

        for row in rows:
            if condition_mode != 2:
                scan_remaining = row.initial_scan_window
            while True:
                if scan_remaining < 1 or remaining_age < 1:
                    return False
                entry = self.entries[history_index]
                if (
                    check_history_condition(entry, row.condition_word_a)
                    and check_history_condition(entry, row.condition_word_b)
                ):
                    break
                if condition_mode == 1:
                    # Native mode 1 permits a skipped record only while the
                    # *first row's* pair remains true on that same record.
                    if not (
                        check_history_condition(entry, initial_word_a)
                        and check_history_condition(entry, initial_word_b)
                    ):
                        return False
                remaining_age -= 1
                scan_remaining -= 1
                history_index = self.previous(history_index)

            if condition_mode == 2:
                # Consume the contiguous run which still matches this pair;
                # the first nonmatching record becomes the next row's start.
                while scan_remaining > 0 and remaining_age > 0:
                    entry = self.entries[history_index]
                    if not (
                        check_history_condition(entry, row.condition_word_a)
                        and check_history_condition(entry, row.condition_word_b)
                    ):
                        break
                    remaining_age -= 1
                    scan_remaining -= 1
                    history_index = self.previous(history_index)
                if scan_remaining < 1 or remaining_age < 1:
                    return False
            else:
                for _ in range(max(0, row.repeat_count)):
                    entry = self.entries[history_index]
                    if not (
                        check_history_condition(entry, row.condition_word_a)
                        and check_history_condition(entry, row.condition_word_b)
                    ):
                        return False
                    remaining_age -= 1
                    if remaining_age < 1:
                        return False
                    history_index = self.previous(history_index)
        return True


@dataclass(frozen=True)
class InputAdmissionEvidence:
    tick: int
    requested_raw_mask: int
    transformed_entry: InputHistoryEntry | None
    admitted_transition_ids: tuple[int, ...]

    def require_transformed(self) -> InputHistoryEntry:
        if self.transformed_entry is None:
            raise StaticResolutionError(
                "raw-input request has no transformed FLuxCharaCurrentInputSnapshot "
                "evidence; requested input cannot be reported as admitted"
            )
        return self.transformed_entry
