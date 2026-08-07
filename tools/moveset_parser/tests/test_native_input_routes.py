from __future__ import annotations

from pathlib import Path

import pytest

from lux_input_codec import LuxInputCodecTables
from luxformats import parse_auto
from native_input_routes import (
    NativeDispatcherResolver,
    NativeInputState,
    build_definition_input_states,
    cpuai_button_mask_to_compact,
    evaluate_input_if,
    resolve_input_timed_attack_route,
    resolve_unconditional_attack_route,
)
from lux_input_history import CurrentInputSnapshot, InputHistoryRing
from native_move_commands import (
    parse_move_play_command_table,
    parse_transition_command_table,
)


REPO_ROOT = Path(__file__).resolve().parents[3]
SC6_EXE = Path(
    r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
)


def test_cpuai_btn_mask_conversion_matches_native_compact_words():
    assert cpuai_button_mask_to_compact(0x0800) == 0x0002  # B
    assert cpuai_button_mask_to_compact(0x0440) == 0x0401  # 6A
    assert cpuai_button_mask_to_compact(0x0001) == 0x0000  # _W annotation


def test_if0001_masks_decoded_high_input_nibble_not_attack_buttons():
    state = NativeInputState(
        snapshot=CurrentInputSnapshot(high_input_nibble=0x0009),
        history=InputHistoryRing(),
        command_table=None,  # IF0001 does not consume the command table.
        command_age_limit=0,
    )

    assert evaluate_input_if(state, (0x0001, 0x0001)) == 1
    assert evaluate_input_if(state, (0x0001, 0x0008)) == 1
    assert evaluate_input_if(state, (0x0001, 0x0006)) == 0
    assert evaluate_input_if(state, (0x0001,)) is None


def _astaroth_inputs():
    required = (
        REPO_ROOT / "dump/Battle/cpu/cpuai012.dtp",
        REPO_ROOT / "dump/Battle/hdr/hdr012.khd",
        REPO_ROOT / "dump/Battle/hdr/command.dat",
        SC6_EXE,
    )
    if not all(path.exists() for path in required):
        pytest.skip("checked-in native inputs or exact SC6 executable unavailable")
    move_table = parse_move_play_command_table(required[0].read_bytes())
    khd = parse_auto(str(required[1]))
    command_table = parse_transition_command_table(required[2].read_bytes())
    codec = LuxInputCodecTables.from_executable(required[3])
    return move_table, khd, command_table, codec


def test_move_play_observations_retain_press_edges_not_only_final_neutral():
    move_table, _, _, codec = _astaroth_inputs()
    definition = move_table.definition(634)
    assert definition is not None

    states = build_definition_input_states(definition, codec)

    assert states[0].snapshot.current_compact_word == 0x0002
    assert states[0].snapshot.secondary_compact_word == 0x0002
    assert any(
        state.snapshot.current_compact_word == 0x0401
        and state.snapshot.secondary_compact_word == 0x0401
        for state in states
    )
    assert states[-1].snapshot.current_compact_word == 0


def test_general_dispatcher_resolves_audited_bear_tamer_start_slot():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(634)
    assert definition is not None

    result = NativeDispatcherResolver(khd, command_table, codec).resolve_definition(
        definition
    )

    assert result.slots == (308,)
    assert not result.truncated
    assert "khd-selector:packed0x304E;tick=31" in result.resolutions


def test_general_unconditional_route_reproduces_audited_two_hit_chain():
    _, khd, _, _ = _astaroth_inputs()

    route = resolve_unconditional_attack_route(khd, 308)

    assert route.slots == (308, 310)
    assert route.cells == (67, 71)
    assert not route.ambiguous


def test_held_definition_follows_input_timed_replacement_and_fails_closed_on_contact_branch():
    move_table, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(khd, command_table, codec)
    pressed = move_table.definition(182)
    held = move_table.definition(183)
    assert pressed is not None and held is not None

    pressed_start = resolver.resolve_definition(pressed)
    held_start = resolver.resolve_definition(held)
    pressed_route = resolve_input_timed_attack_route(
        khd,
        pressed_start.slots[0],
        pressed,
        command_table,
        codec,
        selected_move_play_frame=pressed_start.selected_move_play_frame,
    )
    held_route = resolve_input_timed_attack_route(
        khd,
        held_start.slots[0],
        held,
        command_table,
        codec,
        selected_move_play_frame=held_start.selected_move_play_frame,
    )

    assert pressed_route.slots == (377,)
    assert pressed_route.attack_cells == (153,)
    assert pressed_route.frame_endpoints_resolved

    assert held_route.slots == (377, 378)
    assert held_route.cells == (153, 154)
    assert held_route.attack_slots == (378,)
    assert held_route.attack_cells == (154,)
    assert not held_route.startup_timing_resolved
    assert not held_route.frame_endpoints_resolved
    assert any(
        resolution.startswith("khd-runtime-contact-branch-unresolved:slot378")
        for resolution in held_route.resolutions
    )
