"""Native-order Lux raw-input source and transform transaction.

This module lifts the source switch and the two optional transform chains in
``LuxBattle_TickCharaInput @ 0x140312510``.  The two executable-proven linear
provider specializations and the cyclic provider are lifted explicitly; an
active native mode still requires one of these typed implementations.  A
present but empty list performs the native compact or encoded normalization;
``None`` means the caller passed a null list pointer.

Static sources:

* source switch and transforms: ``0x140312543..0x140312C94``
* previous-input publication: ``LuxBattle_PerFrameTick @ 0x1402DBE83``
* active-session list selection: ``LuxBattle_PerFrameTick @ 0x1402DBEF9``
* provider methods and vtables: ``0x14037EC90..0x14037EFF0``
* provider construction closure: ``0x14037D280..0x14037D3DC``
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from enum import IntEnum
import struct
from typing import Protocol

from lux_input_codec import (
    LuxInputCodecTables,
    decode_input_word,
    derive_current_snapshot,
    encode_input_word,
)
from lux_camera_input_side import (
    CameraRelativeInputContext,
    camera_side_matches_player as derive_camera_side_matches_player,
)
from lux_input_history import CurrentInputSnapshot
from lux_reference_engine import StaticResolutionError


INPUT_DELAY_CAPACITY = 0x3D
TRAINING_INPUT_BUFFER_COUNT = 3
TRAINING_INPUT_RECORD_CAPACITY = 0x708
TRAINING_INPUT_RECORD_SIZE = 3
TRAINING_DUMMY_DUAL_RECORD_CAPACITY = 0x1C20
DIRECT_LATEST_MOVE_IDS = frozenset((0x6A, 0x6B, 0x6C))
CLEAR_INPUT_MOVE_IDS = frozenset((0, 0x2B))


def _u32(value: int) -> int:
    return value & 0xFFFFFFFF


def _pack_compact_byte(word: int) -> int:
    """Pack native bits 0..3 and 10..13 into one byte."""
    value = _u32(word)
    return ((value >> 6) & 0xF0) | (value & 0x0F)


def _expand_compact_byte(value: int) -> int:
    compact = value & 0xFF
    return ((compact & 0xF0) << 6) | (compact & 0x0F)


class InputTransformMode(IntEnum):
    DISABLED = 0
    RECORD = 1
    PLAYBACK = 2


class TrainingInputMode(IntEnum):
    DISABLED = 0
    RECORD = 1
    PLAYBACK = 2


@dataclass(frozen=True)
class TrainingInputRecord:
    stick_nibbles: int
    button_nibbles: int
    input_side_flag: int

    @classmethod
    def capture(cls, target: "CharacterInputPipelineState") -> "TrainingInputRecord":
        return cls(
            _pack_compact_byte(target.current_compact_word),
            _pack_compact_byte(target.secondary_compact_word),
            target.input_side_flag & 0xFF,
        )

    def apply(self, target: "CharacterInputPipelineState") -> None:
        target.current_compact_word = _expand_compact_byte(self.stick_nibbles)
        target.secondary_compact_word = _expand_compact_byte(self.button_nibbles)
        target.input_side_flag = self.input_side_flag & 0xFF


@dataclass(frozen=True)
class TrainingInputNotification:
    player_slot: int
    mode: int

    def __post_init__(self) -> None:
        if self.player_slot not in (0, 1):
            raise ValueError("training-input notification player slot must be 0 or 1")
        if int(self.mode) not in (
            TrainingInputMode.DISABLED,
            TrainingInputMode.RECORD,
            TrainingInputMode.PLAYBACK,
        ):
            raise ValueError("training-input notification mode must be 0, 1, or 2")

    def to_native_bytes(self) -> bytes:
        """Encode the two adjacent dwords passed to dispatcher vtable +0x110."""

        return struct.pack("<II", self.player_slot, int(self.mode))


class TrainingInputNotificationSink(Protocol):
    def dispatch_training_input_notification(
        self,
        notification: TrainingInputNotification,
    ) -> None: ...


@dataclass
class TrainingInputNotificationLog:
    notifications: list[TrainingInputNotification] = field(default_factory=list)

    def dispatch_training_input_notification(
        self,
        notification: TrainingInputNotification,
    ) -> None:
        self.notifications.append(notification)


@dataclass(frozen=True)
class TrainingInputTickResult:
    stopped: bool = False
    notification: TrainingInputNotification | None = None

    @property
    def stop_event_player_slot(self) -> int | None:
        """Legacy diagnostic projection; the exact event also carries ``mode``."""
        return None if self.notification is None else self.notification.player_slot


def _empty_training_buffers() -> list[bytearray]:
    size = TRAINING_INPUT_RECORD_CAPACITY * TRAINING_INPUT_RECORD_SIZE
    return [bytearray(size) for _ in range(TRAINING_INPUT_BUFFER_COUNT)]


def _unbound_dummy_dual_buffers() -> list[bytearray | None]:
    return [None for _ in range(TRAINING_INPUT_BUFFER_COUNT)]


def allocate_dummy_dual_training_buffers() -> list[bytearray | None]:
    """Allocate the three native-sized direct record buffers for one character."""
    size = TRAINING_DUMMY_DUAL_RECORD_CAPACITY * TRAINING_INPUT_RECORD_SIZE
    return [bytearray(size) for _ in range(TRAINING_INPUT_BUFFER_COUNT)]


@dataclass
class TrainingInputRecordPlaybackState:
    """Native 0x98-byte per-player training input state at g_...155C0.

    Only typed fields proven in ``LuxBattle_TickCharaInput`` are represented;
    the native buffer pointers become three identity-preserving bytearrays.
    """

    mode: int = TrainingInputMode.DISABLED
    target: "CharacterInputPipelineState | None" = None
    selected_buffer_index: int = 0
    write_cursors: list[int] = field(default_factory=lambda: [0, 0, 0])
    read_cursors: list[int] = field(default_factory=lambda: [0, 0, 0])
    buffers: list[bytearray] = field(default_factory=_empty_training_buffers)
    notification_sink: TrainingInputNotificationSink = field(
        default_factory=TrainingInputNotificationLog
    )

    def __post_init__(self) -> None:
        if len(self.write_cursors) != TRAINING_INPUT_BUFFER_COUNT:
            raise ValueError("training input state requires three write cursors")
        if len(self.read_cursors) != TRAINING_INPUT_BUFFER_COUNT:
            raise ValueError("training input state requires three read cursors")
        if len(self.buffers) != TRAINING_INPUT_BUFFER_COUNT:
            raise ValueError("training input state requires three record buffers")
        if not 0 <= self.selected_buffer_index < TRAINING_INPUT_BUFFER_COUNT:
            raise ValueError("training selected buffer index must be 0, 1, or 2")
        self.write_cursors[:] = [_u32(value) for value in self.write_cursors]
        self.read_cursors[:] = [_u32(value) for value in self.read_cursors]
        expected_size = TRAINING_INPUT_RECORD_CAPACITY * TRAINING_INPUT_RECORD_SIZE
        for buffer in self.buffers:
            if len(buffer) != expected_size:
                raise ValueError(
                    f"training input record buffer must contain exactly 0x{expected_size:X} bytes"
                )

    def _write_record(self, buffer_index: int, record_index: int) -> None:
        if self.target is None:
            return
        record = TrainingInputRecord.capture(self.target)
        offset = record_index * TRAINING_INPUT_RECORD_SIZE
        self.buffers[buffer_index][offset : offset + 3] = bytes(
            (record.stick_nibbles, record.button_nibbles, record.input_side_flag)
        )

    def _read_record(self, buffer_index: int, record_index: int) -> None:
        if self.target is None:
            return
        offset = record_index * TRAINING_INPUT_RECORD_SIZE
        record = TrainingInputRecord(*self.buffers[buffer_index][offset : offset + 3])
        record.apply(self.target)

    def _dispatch_notification(self, mode: TrainingInputMode) -> TrainingInputNotification:
        if self.target is None:
            raise StaticResolutionError(
                "native training-input notification would dereference a null target character"
            )
        if self.target.player_slot not in (0, 1):
            raise StaticResolutionError(
                "native training-input notification requires target player slot 0 or 1, "
                f"got {self.target.player_slot}"
            )
        notification = TrainingInputNotification(
            player_slot=self.target.player_slot,
            mode=mode,
        )
        self.notification_sink.dispatch_training_input_notification(notification)
        return notification

    def start_recording(self, selected_buffer_index: int) -> int:
        """Lift LuxTrainingDummy_StartRecording, including +0x110 publication."""

        if self.mode == TrainingInputMode.RECORD:
            return int(TrainingInputMode.RECORD)
        if self.mode == TrainingInputMode.PLAYBACK:
            return int(TrainingInputMode.PLAYBACK)
        if 0 <= selected_buffer_index < TRAINING_INPUT_BUFFER_COUNT:
            self.selected_buffer_index = selected_buffer_index
        self.write_cursors[self.selected_buffer_index] = 0
        self.mode = TrainingInputMode.RECORD
        self._dispatch_notification(TrainingInputMode.RECORD)
        return 0

    def start_playback(self, selected_buffer_index: int) -> int:
        """Lift LuxTrainingDummy_StartPlayback, including +0x110 publication."""

        if self.mode == TrainingInputMode.RECORD:
            return int(TrainingInputMode.RECORD)
        if 0 <= selected_buffer_index < TRAINING_INPUT_BUFFER_COUNT:
            self.selected_buffer_index = selected_buffer_index
        self.read_cursors[self.selected_buffer_index] = 0
        self.mode = TrainingInputMode.PLAYBACK
        self._dispatch_notification(TrainingInputMode.PLAYBACK)
        return 0

    def _stop(self) -> TrainingInputTickResult:
        self.mode = TrainingInputMode.DISABLED
        notification = self._dispatch_notification(TrainingInputMode.DISABLED)
        return TrainingInputTickResult(True, notification)

    def tick(self) -> TrainingInputTickResult:
        """Run the native post-side, pre-encoded-transform transaction once."""
        index = self.selected_buffer_index
        if not 0 <= index < TRAINING_INPUT_BUFFER_COUNT:
            raise StaticResolutionError(
                f"native training selected buffer index {index} is outside 0..2"
            )
        if self.mode == TrainingInputMode.RECORD:
            cursor = self.write_cursors[index]
            if cursor < TRAINING_INPUT_RECORD_CAPACITY:
                self._write_record(index, cursor)
            cursor = _u32(cursor + 1)
            self.write_cursors[index] = cursor
            if cursor < TRAINING_INPUT_RECORD_CAPACITY:
                return TrainingInputTickResult()
            return self._stop()
        if self.mode == TrainingInputMode.PLAYBACK:
            cursor = self.read_cursors[index]
            if cursor < TRAINING_INPUT_RECORD_CAPACITY:
                self._read_record(index, cursor)
            cursor = _u32(cursor + 1)
            self.read_cursors[index] = cursor
            if (
                cursor < TRAINING_INPUT_RECORD_CAPACITY
                and cursor < self.write_cursors[index]
            ):
                return TrainingInputTickResult()
            return self._stop()
        return TrainingInputTickResult()


@dataclass(frozen=True)
class TrainingDummyDualInputTickResult:
    processed_records: bool = False
    stopped: bool = False


@dataclass
class TrainingDummyDualInputState:
    """Native 0xFC-byte shared dummy dual-input state at ``0x1447154C0``.

    Direct record-buffer pointers are represented independently from their
    process-local shared-ownership identities.  An unbound pointer remains
    ``None`` and faults closed only when native code would dereference it.
    """

    mode: int = TrainingInputMode.DISABLED
    record_gate0: int = 0
    record_gate1: int = 0
    primary_target: "CharacterInputPipelineState | None" = None
    secondary_target: "CharacterInputPipelineState | None" = None
    selected_buffer_index: int = 0
    write_cursors: list[int] = field(default_factory=lambda: [0, 0, 0])
    read_cursors: list[int] = field(default_factory=lambda: [0, 0, 0])
    primary_buffers: list[bytearray | None] = field(
        default_factory=_unbound_dummy_dual_buffers
    )
    secondary_buffers: list[bytearray | None] = field(
        default_factory=_unbound_dummy_dual_buffers
    )

    def __post_init__(self) -> None:
        if not 0 <= self.selected_buffer_index < TRAINING_INPUT_BUFFER_COUNT:
            raise ValueError("dummy dual selected buffer index must be 0, 1, or 2")
        for name, values in (
            ("write cursors", self.write_cursors),
            ("read cursors", self.read_cursors),
            ("primary buffers", self.primary_buffers),
            ("secondary buffers", self.secondary_buffers),
        ):
            if len(values) != TRAINING_INPUT_BUFFER_COUNT:
                raise ValueError(f"dummy dual state requires three {name}")
        self.write_cursors[:] = [_u32(value) for value in self.write_cursors]
        self.read_cursors[:] = [_u32(value) for value in self.read_cursors]
        expected_size = TRAINING_DUMMY_DUAL_RECORD_CAPACITY * TRAINING_INPUT_RECORD_SIZE
        for buffer in (*self.primary_buffers, *self.secondary_buffers):
            if buffer is not None and len(buffer) != expected_size:
                raise ValueError(
                    f"dummy dual record buffer must contain exactly 0x{expected_size:X} bytes"
                )

    def _selected_buffers(self) -> tuple[bytearray, bytearray]:
        primary = self.primary_buffers[self.selected_buffer_index]
        secondary = self.secondary_buffers[self.selected_buffer_index]
        if primary is None or secondary is None:
            raise StaticResolutionError(
                "native dummy dual transaction would dereference an unbound record buffer"
            )
        return primary, secondary

    def _targets(self) -> tuple["CharacterInputPipelineState", "CharacterInputPipelineState"]:
        if self.primary_target is None or self.secondary_target is None:
            raise StaticResolutionError(
                "native dummy dual transaction would dereference an unbound target character"
            )
        return self.primary_target, self.secondary_target

    @staticmethod
    def _write(buffer: bytearray, cursor: int, target: "CharacterInputPipelineState") -> None:
        record = TrainingInputRecord.capture(target)
        offset = cursor * TRAINING_INPUT_RECORD_SIZE
        buffer[offset : offset + TRAINING_INPUT_RECORD_SIZE] = bytes(
            (record.stick_nibbles, record.button_nibbles, record.input_side_flag)
        )

    @staticmethod
    def _read(buffer: bytearray, cursor: int, target: "CharacterInputPipelineState") -> None:
        offset = cursor * TRAINING_INPUT_RECORD_SIZE
        record = TrainingInputRecord(
            *buffer[offset : offset + TRAINING_INPUT_RECORD_SIZE]
        )
        record.apply(target)

    def tick(self) -> TrainingDummyDualInputTickResult:
        """Run the native shared transaction once at its input-tick call site."""
        index = self.selected_buffer_index
        if not 0 <= index < TRAINING_INPUT_BUFFER_COUNT:
            raise StaticResolutionError(
                f"native dummy dual selected buffer index {index} is outside 0..2"
            )
        gates_open = self.record_gate0 != 0 and self.record_gate1 != 0
        processed = False
        if self.mode == TrainingInputMode.RECORD:
            cursor = self.write_cursors[index]
            if cursor < TRAINING_DUMMY_DUAL_RECORD_CAPACITY and gates_open:
                primary_buffer, secondary_buffer = self._selected_buffers()
                primary_target, secondary_target = self._targets()
                self._write(primary_buffer, cursor, primary_target)
                self._write(secondary_buffer, cursor, secondary_target)
                processed = True
            cursor = _u32(cursor + 1)
            self.write_cursors[index] = cursor
            stopped = cursor >= TRAINING_DUMMY_DUAL_RECORD_CAPACITY
            if stopped:
                self.mode = TrainingInputMode.DISABLED
            return TrainingDummyDualInputTickResult(processed, stopped)
        if self.mode == TrainingInputMode.PLAYBACK:
            cursor = self.read_cursors[index]
            if cursor < TRAINING_DUMMY_DUAL_RECORD_CAPACITY and gates_open:
                primary_buffer, secondary_buffer = self._selected_buffers()
                primary_target, secondary_target = self._targets()
                self._read(primary_buffer, cursor, primary_target)
                self._read(secondary_buffer, cursor, secondary_target)
                processed = True
            cursor = _u32(cursor + 1)
            self.read_cursors[index] = cursor
            stopped = not (
                cursor < TRAINING_DUMMY_DUAL_RECORD_CAPACITY
                and cursor < self.write_cursors[index]
            )
            if stopped:
                self.mode = TrainingInputMode.DISABLED
            return TrainingDummyDualInputTickResult(processed, stopped)
        return TrainingDummyDualInputTickResult()


class InputTransformProvider(Protocol):
    """Typed contract for native vtable slots +0x28 through +0x48."""

    def record_word(self, word: int) -> int:
        """Native +0x30: observe and possibly mutate the ushort in place."""

    def can_continue_recording(self) -> bool:
        """Native +0x28 after the cursor increment."""

    def playback_word(self, cursor: int) -> int:
        """Native +0x38: return the recorded ushort at ``cursor``."""

    def is_playback_cursor_valid(self, cursor: int) -> bool:
        """Native +0x40 after the cursor increment."""

    def get_word_count(self) -> int:
        """Native +0x48: signed vector byte-span divided by ``sizeof(ushort)``."""


@dataclass
class LinearRecordBufferProvider:
    """The two 0x28-byte linear CRecordArrayBuffer ushort providers."""

    max_count: int
    words: list[int] = field(default_factory=list)

    def __post_init__(self) -> None:
        if not 0 <= self.max_count <= 0xFFFFFFFF:
            raise ValueError("linear record-buffer max_count must be a uint")
        self.words[:] = [word & 0xFFFF for word in self.words]

    def record_word(self, word: int) -> int:
        if len(self.words) < self.max_count:
            self.words.append(word & 0xFFFF)
        return word & 0xFFFF

    def can_continue_recording(self) -> bool:
        return len(self.words) != self.max_count

    def playback_word(self, cursor: int) -> int:
        if not 0 <= cursor < len(self.words):
            raise StaticResolutionError(
                f"linear input-transform cursor {cursor} is outside {len(self.words)} words"
            )
        return self.words[cursor]

    def is_playback_cursor_valid(self, cursor: int) -> bool:
        return 0 <= cursor < len(self.words)

    def get_word_count(self) -> int:
        return len(self.words)


@dataclass
class CyclicRecordBufferProvider:
    """The 0x30-byte retained-window CRecordBuffer ushort provider."""

    capacity: int
    words: list[int] = field(default_factory=list)
    readable_begin: int = 0
    write_index: int = 0

    def __post_init__(self) -> None:
        for name in ("capacity", "readable_begin", "write_index"):
            value = getattr(self, name)
            if not 0 <= value <= 0xFFFFFFFF:
                raise ValueError(f"cyclic record-buffer {name} must be a uint")
        self.words[:] = [word & 0xFFFF for word in self.words]

    def record_word(self, word: int) -> int:
        value = word & 0xFFFF
        if self.capacity == 0:
            # Native AppendRecordBufferCyclicWord enters its overwrite branch
            # and executes DIV capacity before the continuation predicate can
            # disable the stream. Refuse this malformed state instead of
            # silently inventing a no-op fallback.
            raise StaticResolutionError(
                "cyclic input-transform record with zero capacity reaches native divide-by-zero"
            )
        if self.capacity <= len(self.words) or self.write_index < len(self.words):
            mapped = self.write_index % self.capacity
            if mapped >= len(self.words):
                raise StaticResolutionError(
                    "cyclic input-transform mapped write is outside its backing vector"
                )
            self.words[mapped] = value
            retained_span = (self.write_index - self.readable_begin) & 0xFFFFFFFF
            if self.capacity <= retained_span:
                self.readable_begin = (self.readable_begin + 1) & 0xFFFFFFFF
        else:
            self.words.append(value)
        self.write_index = (self.write_index + 1) & 0xFFFFFFFF
        return value

    def can_continue_recording(self) -> bool:
        return self.capacity != 0

    def playback_word(self, cursor: int) -> int:
        if self.capacity == 0:
            raise StaticResolutionError("cyclic input-transform capacity is zero")
        mapped = (cursor & 0xFFFFFFFF) % self.capacity
        if mapped >= len(self.words):
            raise StaticResolutionError(
                f"cyclic input-transform mapped index {mapped} is outside {len(self.words)} words"
            )
        return self.words[mapped]

    def is_playback_cursor_valid(self, cursor: int) -> bool:
        value = cursor & 0xFFFFFFFF
        return self.readable_begin <= value < self.write_index

    def get_word_count(self) -> int:
        return len(self.words)


@dataclass
class InputTransformState:
    mode: InputTransformMode = InputTransformMode.DISABLED
    cursor: int = 0
    provider: InputTransformProvider | None = None

    def __post_init__(self) -> None:
        self.cursor = _u32(self.cursor)

    def apply(self, word: int) -> int:
        value = word & 0xFFFF
        if self.mode == InputTransformMode.DISABLED:
            return value
        if self.provider is None:
            raise StaticResolutionError(
                f"active input transform mode {int(self.mode)} has no resolved provider"
            )
        if self.mode == InputTransformMode.RECORD:
            value = self.provider.record_word(value) & 0xFFFF
            self.cursor = _u32(self.cursor + 1)
            if not self.provider.can_continue_recording():
                self.mode = InputTransformMode.DISABLED
                self.cursor = 0
            return value
        if self.mode == InputTransformMode.PLAYBACK:
            value = self.provider.playback_word(self.cursor) & 0xFFFF
            self.cursor = _u32(self.cursor + 1)
            if not self.provider.is_playback_cursor_valid(self.cursor):
                self.mode = InputTransformMode.DISABLED
                self.cursor = 0
            return value
        raise StaticResolutionError(f"unresolved input transform mode {int(self.mode)}")


@dataclass
class InputTransformChain:
    states: list[InputTransformState] = field(default_factory=list)

    def apply(self, word: int) -> int:
        value = word & 0xFFFF
        for state in self.states:
            value = state.apply(value)
        return value


@dataclass
class InputDelayRing:
    entries: list[int] = field(default_factory=lambda: [0] * INPUT_DELAY_CAPACITY)
    cursor: int = 0
    base_offset: int = 0

    def __post_init__(self) -> None:
        if len(self.entries) != INPUT_DELAY_CAPACITY:
            raise ValueError("Lux live input-delay ring must contain exactly 0x3D qwords")
        if not 0 <= self.cursor <= 0x7FFFFFFF:
            raise ValueError("input-delay cursor must be a nonnegative native int")
        if not 0 <= self.base_offset < INPUT_DELAY_CAPACITY:
            raise ValueError("input-delay base offset must be in 0..0x3C")
        self.entries[:] = [value & 0xFFFFFFFFFFFFFFFF for value in self.entries]

    def push_and_select(self, latest_qword: int) -> int:
        """Move 1 path: write delayed slot, advance cursor, read old cursor."""
        old_cursor = self.cursor
        self.entries[(self.base_offset + old_cursor) % INPUT_DELAY_CAPACITY] = (
            latest_qword & 0xFFFFFFFFFFFFFFFF
        )
        if old_cursor == 0x7FFFFFFF:
            raise StaticResolutionError("native signed input-delay cursor overflow")
        self.cursor = old_cursor + 1
        return self.entries[old_cursor % INPUT_DELAY_CAPACITY]

    def mirrored_select(self, observing_player_slot: int) -> int:
        if observing_player_slot == 0:
            index = self.cursor
        elif observing_player_slot == 1:
            index = self.cursor + INPUT_DELAY_CAPACITY - 1
        else:
            raise ValueError("player slot must be 0 or 1")
        return self.entries[index % INPUT_DELAY_CAPACITY]


@dataclass
class RawInputSourceState:
    latest_engine_qwords: list[int] = field(default_factory=lambda: [0, 0])
    delay_rings: list[InputDelayRing] = field(
        default_factory=lambda: [InputDelayRing(), InputDelayRing()]
    )
    cpu_current_words: list[int] = field(default_factory=lambda: [0, 0])

    def __post_init__(self) -> None:
        if not (
            len(self.latest_engine_qwords) == len(self.delay_rings) == len(self.cpu_current_words) == 2
        ):
            raise ValueError("Lux input source state must contain exactly two player slots")
        self.latest_engine_qwords[:] = [
            value & 0xFFFFFFFFFFFFFFFF for value in self.latest_engine_qwords
        ]
        self.cpu_current_words[:] = [_u32(value) for value in self.cpu_current_words]


@dataclass(frozen=True)
class SelectedRawInput:
    current_word: int
    secondary_word: int
    source: str


def select_raw_input(
    *,
    move_id: int,
    player_slot: int,
    opponent_slot: int,
    previous_current_word: int,
    sources: RawInputSourceState,
) -> SelectedRawInput:
    """Execute the native move-ID source switch before transform streams."""
    if player_slot not in (0, 1) or opponent_slot not in (0, 1):
        raise ValueError("player and opponent slots must be 0 or 1")
    if move_id in CLEAR_INPUT_MOVE_IDS:
        return SelectedRawInput(0, 0, "clear")
    if move_id == 1 or move_id in DIRECT_LATEST_MOVE_IDS:
        qword = sources.latest_engine_qwords[player_slot]
        source = "latest-engine"
        if move_id == 1:
            qword = sources.delay_rings[player_slot].push_and_select(qword)
            source = "delayed-live-ring"
        return SelectedRawInput(_u32(qword), _u32(qword >> 32), source)
    if move_id == 3:
        qword = sources.latest_engine_qwords[opponent_slot]
        qword = sources.delay_rings[opponent_slot].mirrored_select(player_slot)
        return SelectedRawInput(_u32(qword), _u32(qword >> 32), "opponent-delay-ring")

    current = sources.cpu_current_words[player_slot]
    secondary = (_u32(previous_current_word) ^ current) & current
    return SelectedRawInput(current, secondary, "cpu-scheduler")


def apply_raw_transform_chain(
    current_word: int,
    secondary_word: int,
    chain: InputTransformChain | None,
) -> tuple[int, int]:
    if chain is None:
        return _u32(current_word), _u32(secondary_word)
    packed = _pack_compact_byte(current_word) | (_pack_compact_byte(secondary_word) << 8)
    transformed = chain.apply(packed)
    return _expand_compact_byte(transformed), _expand_compact_byte(transformed >> 8)


def input_side_for_move(move_id: int, camera_side_matches_player: bool) -> int:
    if move_id == 1 or move_id in DIRECT_LATEST_MOVE_IDS:
        return 0 if camera_side_matches_player else 1
    if move_id == 3:
        return 1 if camera_side_matches_player else 0
    return 0


@dataclass
class CharacterInputPipelineState:
    current_compact_word: int = 0
    secondary_compact_word: int = 0
    input_side_flag: int = 0
    player_slot: int = 0
    decoded_high_nibble_input_id: int = 0
    side_decoded_input_id: int = 0
    last_training_stop_event_player_slot: int | None = None
    last_training_notification: TrainingInputNotification | None = None

    def tick(
        self,
        *,
        move_id: int,
        player_slot: int,
        opponent_slot: int,
        sources: RawInputSourceState,
        tables: LuxInputCodecTables,
        raw_transforms: InputTransformChain | None,
        encoded_transforms: InputTransformChain | None,
        camera_side_matches_player: bool | None = None,
        camera_context: CameraRelativeInputContext | None = None,
        training_override_active: bool = False,
        training_states: list[TrainingInputRecordPlaybackState] | None = None,
        dummy_dual_training_mode: int = 0,
        dummy_dual_training_state: TrainingDummyDualInputState | None = None,
    ) -> tuple[CurrentInputSnapshot, SelectedRawInput]:
        """Run the proven source/transform/codec transaction in native order.

        ``camera_context`` is the typed production path. The explicit boolean
        remains a narrow adapter for unit tests that exercise later stages in
        isolation; callers must provide exactly one source of camera-side
        authority.
        """
        if (camera_context is None) == (camera_side_matches_player is None):
            raise StaticResolutionError(
                "provide exactly one of typed camera_context or explicit camera-side result"
            )
        if camera_context is not None:
            camera_side_matches_player = derive_camera_side_matches_player(
                camera_context, player_slot
            )
        if training_override_active and training_states is None:
            raise StaticResolutionError(
                "training input override requested without typed per-player state"
            )
        if dummy_dual_training_mode != 0 and dummy_dual_training_state is None:
            raise StaticResolutionError(
                "shared dummy dual-input mode requested without typed state"
            )
        if dummy_dual_training_mode != 0 and dummy_dual_training_state is not None:
            raise StaticResolutionError(
                "dummy dual mode must be owned by the typed shared state"
            )
        previous = _u32(self.current_compact_word)
        previous_decoded = _u32(self.decoded_high_nibble_input_id)
        previous_side_decoded = _u32(self.side_decoded_input_id)
        selected = select_raw_input(
            move_id=move_id,
            player_slot=player_slot,
            opponent_slot=opponent_slot,
            previous_current_word=previous,
            sources=sources,
        )
        current, secondary = apply_raw_transform_chain(
            selected.current_word, selected.secondary_word, raw_transforms
        )
        assert camera_side_matches_player is not None
        side = input_side_for_move(move_id, camera_side_matches_player)
        self.current_compact_word = current
        self.secondary_compact_word = secondary
        self.input_side_flag = side
        self.player_slot = player_slot
        self.last_training_stop_event_player_slot = None
        self.last_training_notification = None
        if training_states is not None:
            if len(training_states) != 2:
                raise ValueError("training input state must contain exactly two player slots")
            training_result = training_states[player_slot].tick()
            self.last_training_notification = training_result.notification
            self.last_training_stop_event_player_slot = (
                training_result.stop_event_player_slot
            )
            current = self.current_compact_word
            secondary = self.secondary_compact_word
            side = self.input_side_flag
        if dummy_dual_training_state is not None:
            dummy_dual_training_state.tick()
            current = self.current_compact_word
            secondary = self.secondary_compact_word
            side = self.input_side_flag
        if encoded_transforms is not None:
            encoded = encode_input_word(current, secondary, side, tables)
            encoded = encoded_transforms.apply(encoded)
            current, secondary, side = decode_input_word(encoded, tables)
        snapshot_previous = 0 if move_id in CLEAR_INPUT_MOVE_IDS else previous
        snapshot = derive_current_snapshot(
            current,
            secondary,
            side,
            tables,
            previous_compact_word=snapshot_previous,
        )
        snapshot = replace(
            snapshot,
            previous_decoded_high_nibble_input_id=previous_decoded,
            previous_side_decoded_input_id=previous_side_decoded,
        )
        self.current_compact_word = snapshot.current_compact_word
        self.secondary_compact_word = snapshot.secondary_compact_word
        self.input_side_flag = side
        self.player_slot = player_slot
        self.decoded_high_nibble_input_id = snapshot.decoded_high_nibble_input_id
        self.side_decoded_input_id = snapshot.side_decoded_input_id
        return snapshot, selected
