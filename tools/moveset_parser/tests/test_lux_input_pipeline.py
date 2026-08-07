from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import pytest

from lux_input_codec import LuxInputCodecTables, decode_input_word, encode_input_word
from lux_camera_input_side import (
    CameraRelativeInputContext,
    EffectCameraComponentState,
    LuxEffectCameraClassId,
)
from lux_input_pipeline import (
    CharacterInputPipelineState,
    CyclicRecordBufferProvider,
    InputDelayRing,
    InputTransformChain,
    InputTransformMode,
    InputTransformState,
    LinearRecordBufferProvider,
    RawInputSourceState,
    TrainingInputMode,
    TrainingInputNotification,
    TrainingInputNotificationLog,
    TrainingInputRecordPlaybackState,
    TrainingDummyDualInputState,
    allocate_dummy_dual_training_buffers,
    apply_raw_transform_chain,
    input_side_for_move,
    select_raw_input,
)
from lux_reference_engine import StaticResolutionError


SC6_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
)


@pytest.fixture(scope="module")
def tables() -> LuxInputCodecTables:
    if not SC6_EXE.exists():
        pytest.skip("exact SC6 executable is not available")
    return LuxInputCodecTables.from_executable(SC6_EXE)


@dataclass
class ScriptedTransformProvider:
    record_xor: int = 0
    playback: list[int] = field(default_factory=list)
    record_remaining: bool = True

    def record_word(self, word: int) -> int:
        return word ^ self.record_xor

    def can_continue_recording(self) -> bool:
        return self.record_remaining

    def playback_word(self, cursor: int) -> int:
        return self.playback[cursor]

    def is_playback_cursor_valid(self, cursor: int) -> bool:
        return cursor < len(self.playback)


def test_move_id_source_switch_clear_live_direct_mirror_and_cpu() -> None:
    rings = [InputDelayRing(), InputDelayRing()]
    rings[0].entries[60] = 0x2222222211111111
    rings[1].entries[60] = 0x4444444433333333
    sources = RawInputSourceState(
        latest_engine_qwords=[0xBBBBBBBBAAAAAAAA, 0xDDDDDDDDCCCCCCCC],
        delay_rings=rings,
        cpu_current_words=[0x00000408, 0x00000804],
    )

    assert select_raw_input(move_id=0, player_slot=0, opponent_slot=1, previous_current_word=7, sources=sources).source == "clear"
    direct = select_raw_input(move_id=0x6A, player_slot=0, opponent_slot=1, previous_current_word=7, sources=sources)
    assert (direct.current_word, direct.secondary_word) == (0xAAAAAAAA, 0xBBBBBBBB)
    mirror = select_raw_input(move_id=3, player_slot=1, opponent_slot=0, previous_current_word=7, sources=sources)
    assert (mirror.current_word, mirror.secondary_word) == (0x11111111, 0x22222222)
    cpu = select_raw_input(move_id=2, player_slot=0, opponent_slot=1, previous_current_word=0x8, sources=sources)
    assert cpu.current_word == 0x408
    assert cpu.secondary_word == 0x400


def test_move_one_writes_base_plus_cursor_then_reads_old_cursor() -> None:
    ring = InputDelayRing(cursor=2, base_offset=3)
    ring.entries[2] = 0x2222222211111111
    sources = RawInputSourceState(
        latest_engine_qwords=[0xBBBBBBBBAAAAAAAA, 0],
        delay_rings=[ring, InputDelayRing()],
    )
    selected = select_raw_input(move_id=1, player_slot=0, opponent_slot=1, previous_current_word=0, sources=sources)
    assert (selected.current_word, selected.secondary_word) == (0x11111111, 0x22222222)
    assert ring.entries[5] == 0xBBBBBBBBAAAAAAAA
    assert ring.cursor == 3


def test_input_delay_cursor_is_not_artificially_wrapped_at_0x3d() -> None:
    ring = InputDelayRing(cursor=0x3C)
    ring.entries[0x3C] = 0x1234
    assert ring.push_and_select(0x5678) == 0x5678
    assert ring.cursor == 0x3D


def test_present_empty_raw_chain_normalizes_but_null_chain_does_not() -> None:
    assert apply_raw_transform_chain(0xFFFF0408, 0xAAAA0804, None) == (0xFFFF0408, 0xAAAA0804)
    assert apply_raw_transform_chain(0xFFFF0408, 0xAAAA0804, InputTransformChain()) == (0x0408, 0x0804)


def test_transform_modes_increment_then_reset_exactly() -> None:
    recorder = ScriptedTransformProvider(record_xor=0x00FF, record_remaining=False)
    record_state = InputTransformState(InputTransformMode.RECORD, 7, recorder)
    assert record_state.apply(0x1234) == 0x12CB
    assert (record_state.mode, record_state.cursor) == (InputTransformMode.DISABLED, 0)

    player = ScriptedTransformProvider(playback=[0xABCD])
    playback_state = InputTransformState(InputTransformMode.PLAYBACK, 0, player)
    assert playback_state.apply(0x1234) == 0xABCD
    assert (playback_state.mode, playback_state.cursor) == (InputTransformMode.DISABLED, 0)


def test_linear_native_provider_records_until_exact_max_and_plays_by_index() -> None:
    provider = LinearRecordBufferProvider(max_count=2)
    state = InputTransformState(InputTransformMode.RECORD, 0, provider)
    assert state.apply(0x1111) == 0x1111
    assert (state.mode, state.cursor, provider.words) == (InputTransformMode.RECORD, 1, [0x1111])
    assert state.apply(0x2222) == 0x2222
    assert (state.mode, state.cursor, provider.words) == (InputTransformMode.DISABLED, 0, [0x1111, 0x2222])

    playback = InputTransformState(InputTransformMode.PLAYBACK, 0, provider)
    assert playback.apply(0) == 0x1111
    assert playback.apply(0) == 0x2222
    assert (playback.mode, playback.cursor) == (InputTransformMode.DISABLED, 0)
    assert provider.get_word_count() == 2


def test_cyclic_native_provider_retains_absolute_index_window() -> None:
    provider = CyclicRecordBufferProvider(capacity=2)
    state = InputTransformState(InputTransformMode.RECORD, 0, provider)
    for word in (0x1111, 0x2222, 0x3333):
        assert state.apply(word) == word
    assert state.mode == InputTransformMode.RECORD
    assert provider.words == [0x3333, 0x2222]
    assert (provider.readable_begin, provider.write_index) == (1, 3)

    playback = InputTransformState(InputTransformMode.PLAYBACK, 1, provider)
    assert playback.apply(0) == 0x2222
    assert playback.apply(0) == 0x3333
    assert (playback.mode, playback.cursor) == (InputTransformMode.DISABLED, 0)
    assert provider.get_word_count() == 2


def test_cyclic_zero_capacity_fails_at_native_record_boundary() -> None:
    state = InputTransformState(
        InputTransformMode.RECORD,
        0,
        CyclicRecordBufferProvider(capacity=0),
    )
    with pytest.raises(StaticResolutionError, match="native divide-by-zero"):
        state.apply(0x1234)


def test_cyclic_overfull_backing_state_is_not_normalized() -> None:
    provider = CyclicRecordBufferProvider(
        capacity=2,
        words=[0x1111, 0x2222, 0x3333],
        readable_begin=0,
        write_index=3,
    )
    assert provider.record_word(0x4444) == 0x4444
    assert provider.words == [0x1111, 0x4444, 0x3333]
    assert (provider.readable_begin, provider.write_index) == (1, 4)


def test_transform_cursor_increment_uses_native_u32_storage() -> None:
    provider = CyclicRecordBufferProvider(
        capacity=2,
        words=[0xAAAA, 0xBBBB],
        readable_begin=0,
        write_index=0xFFFFFFFF,
    )
    state = InputTransformState(InputTransformMode.PLAYBACK, 0xFFFFFFFF, provider)
    assert state.apply(0) == 0xBBBB
    assert state.cursor == 0
    assert state.mode == InputTransformMode.PLAYBACK


def test_active_transform_without_provider_fails_closed() -> None:
    with pytest.raises(StaticResolutionError, match="no resolved provider"):
        InputTransformState(InputTransformMode.PLAYBACK).apply(0)


@pytest.mark.parametrize(
    "move_id,matches,expected",
    [(1, True, 0), (1, False, 1), (0x6C, False, 1), (3, True, 1), (3, False, 0), (2, False, 0)],
)
def test_native_move_specific_camera_side_mapping(move_id: int, matches: bool, expected: int) -> None:
    assert input_side_for_move(move_id, matches) == expected


def test_pipeline_derives_camera_side_from_typed_native_context(
    tables: LuxInputCodecTables,
) -> None:
    context = CameraRelativeInputContext(
        components=(
            EffectCameraComponentState(LuxEffectCameraClassId.GAME, 1.0),
        )
        + (None,) * 15,
        active_camera_x=0.0,
        active_camera_z=1.0,
        player1_x=1.0,
        player1_z=0.0,
        player2_x=0.0,
        player2_z=0.0,
    )
    state = CharacterInputPipelineState()
    snapshot, _ = state.tick(
        move_id=1,
        player_slot=1,
        opponent_slot=0,
        camera_context=context,
        sources=RawInputSourceState(),
        tables=tables,
        raw_transforms=None,
        encoded_transforms=None,
    )
    assert state.input_side_flag == 0


def test_pipeline_rejects_ambiguous_or_missing_camera_side_authority(
    tables: LuxInputCodecTables,
) -> None:
    context = CameraRelativeInputContext(
        components=(None,) * 16,
        active_camera_x=0.0,
        active_camera_z=0.0,
        player1_x=0.0,
        player1_z=0.0,
        player2_x=0.0,
        player2_z=0.0,
    )
    common = dict(
        move_id=1,
        player_slot=0,
        opponent_slot=1,
        sources=RawInputSourceState(),
        tables=tables,
        raw_transforms=None,
        encoded_transforms=None,
    )
    with pytest.raises(StaticResolutionError, match="exactly one"):
        CharacterInputPipelineState().tick(**common)
    with pytest.raises(StaticResolutionError, match="exactly one"):
        CharacterInputPipelineState().tick(
            **common,
            camera_side_matches_player=True,
            camera_context=context,
        )


def test_pipeline_publishes_previous_word_and_full_current_dword(tables: LuxInputCodecTables) -> None:
    state = CharacterInputPipelineState(
        current_compact_word=0x00000008,
        decoded_high_nibble_input_id=7,
        side_decoded_input_id=9,
    )
    sources = RawInputSourceState(cpu_current_words=[0xFFFF0408, 0])
    snapshot, selected = state.tick(
        move_id=2,
        player_slot=0,
        opponent_slot=1,
        camera_side_matches_player=True,
        sources=sources,
        tables=tables,
        raw_transforms=None,
        encoded_transforms=None,
    )
    assert selected.current_word == 0xFFFF0408
    assert snapshot.previous_compact_word == 8
    assert snapshot.previous_decoded_high_nibble_input_id == 7
    assert snapshot.previous_side_decoded_input_id == 9
    assert snapshot.current_compact_word == 0xFFFF0408
    assert snapshot.secondary_compact_word == 0xFFFF0400


def test_pipeline_refuses_unmodeled_training_override(tables: LuxInputCodecTables) -> None:
    with pytest.raises(StaticResolutionError, match="typed per-player state"):
        CharacterInputPipelineState().tick(
            move_id=1,
            player_slot=0,
            opponent_slot=1,
            camera_side_matches_player=True,
            sources=RawInputSourceState(),
            tables=tables,
            raw_transforms=InputTransformChain(),
            encoded_transforms=InputTransformChain(),
            training_override_active=True,
        )


def test_native_training_record_then_playback_overwrites_target_before_codec(
    tables: LuxInputCodecTables,
) -> None:
    target = CharacterInputPipelineState(
        current_compact_word=0x0408,
        secondary_compact_word=0x0804,
        input_side_flag=1,
        player_slot=0,
    )
    record_state = TrainingInputRecordPlaybackState(
        mode=TrainingInputMode.RECORD,
        target=target,
    )
    assert not record_state.tick().stopped
    assert record_state.write_cursors == [1, 0, 0]
    assert record_state.buffers[0][:3] == bytes((0x18, 0x24, 1))

    target.current_compact_word = 0
    target.secondary_compact_word = 0
    target.input_side_flag = 0
    record_state.mode = TrainingInputMode.PLAYBACK
    record_state.read_cursors[0] = 0
    result = record_state.tick()
    assert result.stopped
    assert result.stop_event_player_slot == 0
    assert target.current_compact_word == 0x0408
    assert target.secondary_compact_word == 0x0804
    assert target.input_side_flag == 1


def test_training_record_stops_at_native_0x708_boundary() -> None:
    target = CharacterInputPipelineState(player_slot=1)
    state = TrainingInputRecordPlaybackState(
        mode=TrainingInputMode.RECORD,
        target=target,
        write_cursors=[0x707, 0, 0],
    )
    result = state.tick()
    assert result.stopped
    assert result.notification is not None
    assert result.notification.player_slot == 1
    assert result.notification.mode == TrainingInputMode.DISABLED
    assert state.mode == TrainingInputMode.DISABLED
    assert state.write_cursors[0] == 0x708


def test_training_notification_has_exact_two_dword_native_layout() -> None:
    assert TrainingInputNotification(1, TrainingInputMode.PLAYBACK).to_native_bytes() == bytes(
        (1, 0, 0, 0, 2, 0, 0, 0)
    )


def test_training_start_record_playback_and_stop_dispatch_exact_modes() -> None:
    sink = TrainingInputNotificationLog()
    target = CharacterInputPipelineState(player_slot=1)
    state = TrainingInputRecordPlaybackState(target=target, notification_sink=sink)

    state.write_cursors[:] = [7, 8, 9]
    assert state.start_recording(2) == 0
    assert state.mode == TrainingInputMode.RECORD
    assert state.selected_buffer_index == 2
    assert state.write_cursors == [7, 8, 0]
    assert state.start_recording(0) == TrainingInputMode.RECORD

    state.mode = TrainingInputMode.DISABLED
    state.read_cursors[:] = [4, 5, 6]
    assert state.start_playback(1) == 0
    assert state.mode == TrainingInputMode.PLAYBACK
    assert state.selected_buffer_index == 1
    assert state.read_cursors == [4, 0, 6]
    state.write_cursors[1] = 0
    result = state.tick()
    assert result.stopped
    assert [(item.player_slot, item.mode) for item in sink.notifications] == [
        (1, TrainingInputMode.RECORD),
        (1, TrainingInputMode.PLAYBACK),
        (1, TrainingInputMode.DISABLED),
    ]


def test_invalid_start_buffer_retains_previous_native_selection() -> None:
    sink = TrainingInputNotificationLog()
    state = TrainingInputRecordPlaybackState(
        target=CharacterInputPipelineState(player_slot=0),
        selected_buffer_index=1,
        write_cursors=[3, 4, 5],
        notification_sink=sink,
    )
    assert state.start_recording(99) == 0
    assert state.selected_buffer_index == 1
    assert state.write_cursors == [3, 0, 5]


def test_training_playback_stops_at_written_cursor_without_reading_past_it() -> None:
    target = CharacterInputPipelineState(player_slot=0)
    state = TrainingInputRecordPlaybackState(
        mode=TrainingInputMode.PLAYBACK,
        target=target,
        write_cursors=[1, 0, 0],
    )
    state.buffers[0][:3] = bytes((0x12, 0x34, 1))
    result = state.tick()
    assert result.stopped
    assert target.current_compact_word == _expand_compact_byte_for_test(0x12)
    assert target.secondary_compact_word == _expand_compact_byte_for_test(0x34)


def test_training_stop_rejects_target_with_non_player_slot() -> None:
    target = CharacterInputPipelineState(player_slot=2)
    state = TrainingInputRecordPlaybackState(
        mode=TrainingInputMode.PLAYBACK,
        target=target,
        write_cursors=[1, 0, 0],
    )
    state.buffers[0][:3] = bytes((0x12, 0x34, 0))

    with pytest.raises(StaticResolutionError, match="target player slot 0 or 1"):
        state.tick()

    assert state.mode == TrainingInputMode.DISABLED


def _expand_compact_byte_for_test(value: int) -> int:
    return ((value & 0xF0) << 6) | (value & 0x0F)


def test_pipeline_applies_training_alias_before_encoded_transform(
    tables: LuxInputCodecTables,
) -> None:
    pipeline = CharacterInputPipelineState(player_slot=0)
    playback = TrainingInputRecordPlaybackState(
        mode=TrainingInputMode.PLAYBACK,
        target=pipeline,
        write_cursors=[2, 0, 0],
    )
    playback.buffers[0][:3] = bytes((0x18, 0x24, 1))
    encoded_provider = LinearRecordBufferProvider(max_count=1)
    encoded_chain = InputTransformChain(
        [InputTransformState(InputTransformMode.RECORD, 0, encoded_provider)]
    )
    snapshot, _ = pipeline.tick(
        move_id=2,
        player_slot=0,
        opponent_slot=1,
        camera_side_matches_player=True,
        sources=RawInputSourceState(),
        tables=tables,
        raw_transforms=None,
        encoded_transforms=encoded_chain,
        training_states=[playback, TrainingInputRecordPlaybackState()],
    )
    expected_encoded = encode_input_word(0x0408, 0x0804, 1, tables)
    assert encoded_provider.words == [expected_encoded]
    expected_current, expected_secondary, expected_side = decode_input_word(
        expected_encoded, tables
    )
    assert snapshot.current_compact_word == expected_current
    assert snapshot.secondary_compact_word == expected_secondary
    assert pipeline.input_side_flag == expected_side


def test_pipeline_refuses_shared_dummy_mode_without_typed_state(
    tables: LuxInputCodecTables,
) -> None:
    with pytest.raises(StaticResolutionError, match="without typed state"):
        CharacterInputPipelineState().tick(
            move_id=2,
            player_slot=0,
            opponent_slot=1,
            camera_side_matches_player=True,
            sources=RawInputSourceState(),
            tables=tables,
            raw_transforms=None,
            encoded_transforms=None,
            dummy_dual_training_mode=1,
        )


def test_dummy_dual_records_both_targets_and_advances_one_shared_cursor() -> None:
    primary = CharacterInputPipelineState(
        current_compact_word=0x0408,
        secondary_compact_word=0x0804,
        input_side_flag=1,
        player_slot=0,
    )
    secondary = CharacterInputPipelineState(
        current_compact_word=0x0C02,
        secondary_compact_word=0x1001,
        input_side_flag=0,
        player_slot=1,
    )
    state = TrainingDummyDualInputState(
        mode=TrainingInputMode.RECORD,
        record_gate0=1,
        record_gate1=1,
        primary_target=primary,
        secondary_target=secondary,
        primary_buffers=allocate_dummy_dual_training_buffers(),
        secondary_buffers=allocate_dummy_dual_training_buffers(),
    )

    result = state.tick()

    assert result.processed_records
    assert not result.stopped
    assert state.write_cursors == [1, 0, 0]
    assert state.primary_buffers[0][:3] == bytes((0x18, 0x24, 1))
    assert state.secondary_buffers[0][:3] == bytes((0x32, 0x41, 0))


def test_dummy_dual_closed_gate_skips_dereference_but_advances_cursor() -> None:
    state = TrainingDummyDualInputState(
        mode=TrainingInputMode.RECORD,
        record_gate0=0,
        record_gate1=1,
    )

    result = state.tick()

    assert not result.processed_records
    assert not result.stopped
    assert state.write_cursors == [1, 0, 0]


def test_dummy_dual_playback_applies_both_records_and_stops_at_written_end() -> None:
    primary = CharacterInputPipelineState(player_slot=0)
    secondary = CharacterInputPipelineState(player_slot=1)
    primary_buffers = allocate_dummy_dual_training_buffers()
    secondary_buffers = allocate_dummy_dual_training_buffers()
    primary_buffers[0][:3] = bytes((0x18, 0x24, 1))
    secondary_buffers[0][:3] = bytes((0x32, 0x41, 0))
    state = TrainingDummyDualInputState(
        mode=TrainingInputMode.PLAYBACK,
        record_gate0=1,
        record_gate1=1,
        primary_target=primary,
        secondary_target=secondary,
        write_cursors=[1, 0, 0],
        primary_buffers=primary_buffers,
        secondary_buffers=secondary_buffers,
    )

    result = state.tick()

    assert result.processed_records
    assert result.stopped
    assert state.mode == TrainingInputMode.DISABLED
    assert state.read_cursors == [1, 0, 0]
    assert (primary.current_compact_word, primary.secondary_compact_word, primary.input_side_flag) == (
        0x0408,
        0x0804,
        1,
    )
    assert (
        secondary.current_compact_word,
        secondary.secondary_compact_word,
        secondary.input_side_flag,
    ) == (0x0C02, 0x1001, 0)


def test_dummy_dual_open_gate_fails_closed_on_unbound_native_identities() -> None:
    state = TrainingDummyDualInputState(
        mode=TrainingInputMode.PLAYBACK,
        record_gate0=1,
        record_gate1=1,
        primary_target=CharacterInputPipelineState(player_slot=0),
        secondary_target=CharacterInputPipelineState(player_slot=1),
        write_cursors=[1, 0, 0],
    )

    with pytest.raises(StaticResolutionError, match="unbound record buffer"):
        state.tick()


def test_training_record_preserves_full_native_input_side_byte() -> None:
    target = CharacterInputPipelineState(input_side_flag=0xA5, player_slot=0)
    state = TrainingInputRecordPlaybackState(
        mode=TrainingInputMode.RECORD,
        target=target,
    )
    state.tick()
    assert state.buffers[0][2] == 0xA5

    target.input_side_flag = 0
    state.mode = TrainingInputMode.PLAYBACK
    state.read_cursors[0] = 0
    state.tick()
    assert target.input_side_flag == 0xA5


def test_pipeline_applies_dummy_dual_playback_before_encoded_transform(
    tables: LuxInputCodecTables,
) -> None:
    primary = CharacterInputPipelineState(player_slot=0)
    secondary = CharacterInputPipelineState(player_slot=1)
    primary_buffers = allocate_dummy_dual_training_buffers()
    secondary_buffers = allocate_dummy_dual_training_buffers()
    primary_buffers[0][:3] = bytes((0x18, 0x24, 1))
    secondary_buffers[0][:3] = bytes((0x32, 0x41, 0))
    dummy_state = TrainingDummyDualInputState(
        mode=TrainingInputMode.PLAYBACK,
        record_gate0=1,
        record_gate1=1,
        primary_target=primary,
        secondary_target=secondary,
        write_cursors=[2, 0, 0],
        primary_buffers=primary_buffers,
        secondary_buffers=secondary_buffers,
    )
    provider = LinearRecordBufferProvider(max_count=1)

    snapshot, _ = primary.tick(
        move_id=2,
        player_slot=0,
        opponent_slot=1,
        camera_side_matches_player=True,
        sources=RawInputSourceState(),
        tables=tables,
        raw_transforms=None,
        encoded_transforms=InputTransformChain(
            [InputTransformState(InputTransformMode.RECORD, 0, provider)]
        ),
        dummy_dual_training_state=dummy_state,
    )

    expected = encode_input_word(0x0408, 0x0804, 1, tables)
    assert provider.words == [expected]
    assert snapshot.current_compact_word == decode_input_word(expected, tables)[0]


def test_clear_move_also_clears_previous_input_publication(tables: LuxInputCodecTables) -> None:
    state = CharacterInputPipelineState(current_compact_word=0x0408)
    snapshot, _ = state.tick(
        move_id=0x2B,
        player_slot=0,
        opponent_slot=1,
        camera_side_matches_player=True,
        sources=RawInputSourceState(),
        tables=tables,
        raw_transforms=None,
        encoded_transforms=None,
    )
    assert snapshot.current_compact_word == 0
    assert snapshot.previous_compact_word == 0
