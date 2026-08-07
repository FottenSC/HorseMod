from __future__ import annotations

from lux_scheduled_effects import (
    MOVEVM_SCHEDULED_EFFECT_PAYLOAD_WORDS,
    LuxScheduledEffectOp,
    LuxScheduledEffectTable,
)


def test_register_preserves_signed_words_and_resets_fired_flag() -> None:
    table = LuxScheduledEffectTable()
    table.entries[0].fired = 7

    assert table.register((0xFFFF, 0x8000, 0x7FFF)) == 1
    entry = table.entries[0]
    assert entry.trigger_frame == -1
    # A trigger of -1 is the native free sentinel, so authored data must not
    # use it as a persistent registration. Exercise storage with a real frame.
    assert table.register((9, 0x8000, 0x7FFF)) == 1
    entry = table.entries[0]
    assert (entry.trigger_frame, entry.argument_count, entry.fired) == (9, 2, 0)
    assert entry.payload_words[:2] == [-32768, 32767]


def test_duplicate_is_rejected_without_consuming_another_slot() -> None:
    table = LuxScheduledEffectTable()
    arguments = (12, 0x1234, -2, 7)
    assert table.register(arguments) == 1
    assert table.register(arguments) == 0
    assert sum(not entry.is_free for entry in table.entries) == 1


def test_same_frame_with_different_payload_uses_next_slot() -> None:
    table = LuxScheduledEffectTable()
    assert table.register((12, 1)) == 1
    assert table.register((12, 2)) == 1
    assert [entry.payload_words[0] for entry in table.entries[:2]] == [1, 2]


def test_full_table_and_oversized_payload_fail_without_mutation() -> None:
    table = LuxScheduledEffectTable(
        entries=[
            LuxScheduledEffectOp(trigger_frame=index, argument_count=1)
            for index in range(16)
        ]
    )
    assert table.register((99, 3)) == 0
    before = [entry.trigger_frame for entry in table.entries]
    oversized = (20,) + tuple(range(MOVEVM_SCHEDULED_EFFECT_PAYLOAD_WORDS + 1))
    assert table.register(oversized) == 0
    assert [entry.trigger_frame for entry in table.entries] == before
