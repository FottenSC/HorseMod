"""Native-equivalent MoveVM lane scheduled-effect registration.

The native lane owns sixteen fixed 0x24-byte entries.  Registration is a
deduplicating insert; it does not execute the effect or advance lane time.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lux_numeric import signed_low_i16


MOVEVM_SCHEDULED_EFFECT_SLOT_COUNT = 16
MOVEVM_SCHEDULED_EFFECT_PAYLOAD_WORDS = 16


@dataclass
class LuxScheduledEffectOp:
    trigger_frame: int = -1
    argument_count: int = 0
    fired: int = 0
    payload_words: list[int] = field(
        default_factory=lambda: [0] * MOVEVM_SCHEDULED_EFFECT_PAYLOAD_WORDS
    )

    def __post_init__(self) -> None:
        self.trigger_frame = signed_low_i16(self.trigger_frame)
        self.argument_count &= 0xFF
        self.fired &= 0xFF
        if len(self.payload_words) != MOVEVM_SCHEDULED_EFFECT_PAYLOAD_WORDS:
            raise ValueError("scheduled-effect payload must contain 16 shorts")
        self.payload_words[:] = [signed_low_i16(word) for word in self.payload_words]

    @property
    def is_free(self) -> bool:
        return self.trigger_frame == -1


@dataclass
class LuxScheduledEffectTable:
    entries: list[LuxScheduledEffectOp] = field(
        default_factory=lambda: [
            LuxScheduledEffectOp()
            for _ in range(MOVEVM_SCHEDULED_EFFECT_SLOT_COUNT)
        ]
    )

    def __post_init__(self) -> None:
        if len(self.entries) != MOVEVM_SCHEDULED_EFFECT_SLOT_COUNT:
            raise ValueError("scheduled-effect table must contain 16 entries")

    def register(self, arguments: tuple[int, ...]) -> int:
        """Mirror ``LuxMoveVM_CallCond_RegisterScheduledEffectOp``.

        ``arguments[0]`` is the trigger frame.  All later words, beginning
        with the effect opcode, are copied into the fixed inline payload.
        """

        stored_count = len(arguments) - 1
        if stored_count < 0:
            raise ValueError("scheduled-effect registration requires a trigger word")
        if stored_count > MOVEVM_SCHEDULED_EFFECT_PAYLOAD_WORDS:
            return 0

        trigger = signed_low_i16(arguments[0])
        payload = tuple(signed_low_i16(word) for word in arguments[1:])
        free_entry: LuxScheduledEffectOp | None = None

        for entry in self.entries:
            if entry.is_free and free_entry is None:
                free_entry = entry
            if (
                not entry.is_free
                and entry.trigger_frame == trigger
                and entry.argument_count == stored_count
                and tuple(entry.payload_words[:stored_count]) == payload
            ):
                return 0

        if free_entry is None:
            return 0
        free_entry.trigger_frame = trigger
        free_entry.argument_count = stored_count
        free_entry.fired = 0
        free_entry.payload_words[:stored_count] = payload
        return 1
