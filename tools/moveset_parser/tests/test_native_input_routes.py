from __future__ import annotations

from dataclasses import replace
from pathlib import Path

import pytest

from lux_input_codec import LuxInputCodecTables
from luxformats import parse_auto
from native_frame_analysis import analyze_confirmed_slot_frames
from native_input_routes import (
    _OFFLINE_INPUT_TRANSITION_SUBOPS,
    NativeDispatcherResolver,
    NativeInputState,
    build_definition_input_states,
    cpuai_button_mask_to_compact,
    evaluate_input_if,
    _route_callcond_evaluator,
    resolve_input_timed_attack_route,
    resolve_native_contact_followups,
    resolve_publication_entry_route,
    resolve_unconditional_attack_route,
)
from stackvm_emulate import emulate
from lux_input_history import CurrentInputSnapshot, InputHistoryRing
from native_move_commands import (
    MovePlayButtonStep,
    MovePlayDefinition,
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


def test_if0003_masks_current_side_held_direction_word():
    state = NativeInputState(
        snapshot=CurrentInputSnapshot(side_direction_mask=0x0004),
        history=InputHistoryRing(),
        command_table=None,
        command_age_limit=0,
    )

    assert evaluate_input_if(state, (0x0003, 0x3004)) == 1
    assert evaluate_input_if(state, (0x0003, 0x3008)) == 0
    assert evaluate_input_if(state, (0x0003,)) is None
    assert evaluate_input_if(
        replace(state, snapshot=CurrentInputSnapshot(side_direction_mask=0)),
        (0x0003, 0x3004),
    ) == 0


def test_world_mode_opponent_zero_and_motion_edge_predicates_are_distinct():
    state = NativeInputState(
        snapshot=CurrentInputSnapshot(),
        history=InputHistoryRing(),
        command_table=None,
        command_age_limit=0,
        per_frame_motion_flags=(0,) * 27 + (1,) + (0,) * (0x72 - 28),
    )

    assert evaluate_input_if(state, (0x0015, 2)) == 1
    assert evaluate_input_if(state, (0x0015, 1)) == 0
    assert evaluate_input_if(state, (0x001D, 3)) == 1
    assert evaluate_input_if(state, (0x0066, 27)) == 1
    assert evaluate_input_if(state, (0x0066, 46)) == 0
    opponent_flag = replace(
        state,
        opponent_motion_state_latches=(0,) * 3 + (1,) + (0,) * (0x72 - 4),
    )
    assert evaluate_input_if(opponent_flag, (0x001D, 3)) == 0


def test_if0041_reports_pending_lane_transition_not_movement_input():
    baseline = NativeInputState(
        snapshot=CurrentInputSnapshot(),
        history=InputHistoryRing(),
        command_table=None,
        command_age_limit=0,
    )

    assert evaluate_input_if(baseline, (0x0041,)) == 0
    assert evaluate_input_if(
        replace(baseline, immediate_transition_target=353), (0x0041,)
    ) == 1
    assert evaluate_input_if(
        replace(baseline, deferred_transition_target=354), (0x0041,)
    ) == 1
    assert evaluate_input_if(
        replace(baseline, immediate_transition_target=None), (0x0041,)
    ) is None


def test_dispatcher_meter_profile_and_special_mode_predicates_are_concrete():
    baseline = NativeInputState(
        snapshot=CurrentInputSnapshot(),
        history=InputHistoryRing(),
        command_table=None,
        command_age_limit=0,
        character_profile_id=0x0C,
        meter_state_shorts=(None,) * 10 + (0,) + (None,) * 6,
        special_lethal_match_mode=0,
    )

    assert evaluate_input_if(baseline, (0x138A, 10, 0, 0)) == 1
    assert evaluate_input_if(baseline, (0x138A, 10, 1, 9999)) == 0
    soul_charge = replace(
        baseline,
        meter_state_shorts=(None,) * 10 + (1,) + (None,) * 6,
    )
    assert evaluate_input_if(soul_charge, (0x138A, 10, 1, 9999)) == 1
    assert evaluate_input_if(baseline, (0x13C5, 0x0C)) == 1
    assert evaluate_input_if(baseline, (0x13C5, 0x3C)) == 0
    assert evaluate_input_if(baseline, (0x13DA, 2)) == 0
    assert evaluate_input_if(
        replace(baseline, special_lethal_match_mode=2), (0x13DA, 2)
    ) == 1


def test_alternate_self_matchers_use_native_primary_secondary_and_latch9_mirror():
    snapshot = CurrentInputSnapshot(
        current_compact_word=0x0401,
        secondary_compact_word=0x0802,
        side_decoded_input_id=8,
        side_direction_mask=0x0008,
        side_decoded_secondary_input_id=4,
        side_secondary_direction_mask=0x0004,
    )
    baseline = NativeInputState(
        snapshot=snapshot,
        history=InputHistoryRing(),
        command_table=None,
        command_age_limit=0,
    )

    assert evaluate_input_if(baseline, (0x13AE, 0x3008)) == 1
    assert evaluate_input_if(baseline, (0x13AF, 0x3004)) == 1
    mirrored = replace(
        baseline,
        motion_state_latches=(0,) * 9 + (1,) + (0,) * (0x72 - 10),
    )
    assert evaluate_input_if(mirrored, (0x13AE, 0x3008)) == 0
    assert evaluate_input_if(mirrored, (0x13AE, 0x3004)) == 1


def _character_inputs(cid: str):
    required = (
        REPO_ROOT / f"dump/Battle/cpu/cpuai{cid}.dtp",
        REPO_ROOT / f"dump/Battle/hdr/hdr{cid}.khd",
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


def _astaroth_inputs():
    return _character_inputs("012")


def _mitsurugi_inputs():
    return _character_inputs("001")


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


def test_general_dispatcher_stops_after_bear_tamer_opener_commits():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(634)
    assert definition is not None

    result = NativeDispatcherResolver(khd, command_table, codec).resolve_definition(
        definition
    )

    assert result.slots == (308,)
    assert result.publications == ((0, (308,)),)
    assert not result.truncated
    assert "khd-selector:packed0x304E;tick=31" in result.resolutions
    assert any(
        resolution == "khd-input-publications:frame0->slot308"
        for resolution in result.resolutions
    )


def test_death_bringer_ignores_unreachable_direction_variant_branches():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(635)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd,
        command_table,
        codec,
        character_profile_id=0x0C,
    )

    candidates = resolver.resolve_definition(definition)
    route = resolve_publication_entry_route(
        resolver,
        definition,
        candidates,
        authored_hit_count=1,
    )

    assert route is not None
    assert route.slots == (398,)
    assert route.attack_cells == (175,)
    assert route.startup_player_frame == 36
    assert route.frame_endpoints_resolved
    assert not any(
        "runtime-contact-branch-unresolved" in resolution
        for resolution in route.resolutions
    )


def test_astaroth_4aa_idle_selector_commits_only_the_first_route_entry():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(34)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd,
        command_table,
        codec,
        character_profile_id=0x0C,
    )

    trace = resolver.resolve_live_transition_publications(
        definition,
        meter_state_shorts={10: 0},
        special_lethal_match_mode=0,
    )

    assert trace.publications == ((0, (283,)),)
    assert not trace.truncated
    assert "khd-live-selection:packed0x3049->slot2654" in trace.resolutions


@pytest.mark.parametrize("held_frames", (2, 3, 8, 20))
def test_astaroth_held_4ak_does_not_reenter_the_idle_selector(held_frames):
    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    definition = MovePlayDefinition(
        definition_id=0x7200 + held_frames,
        authored_definition_id=0x7200 + held_frames,
        initial_cell=0,
        raw_words=(),
        region_end_cell=2,
        script_end_cell=2,
        status="native-linear",
        button_steps=(MovePlayButtonStep(0, 0x1410, held_frames),),
        branch_cells=(),
    )

    trace = resolver.resolve_live_transition_publications(
        definition,
        meter_state_shorts={10: 0},
        special_lethal_match_mode=0,
    )
    route = resolve_input_timed_attack_route(
        khd,
        283,
        definition,
        command_table,
        codec,
        selected_move_play_frame=0,
        character_profile_id=0x0C,
    )

    assert trace.publications == ((0, (283,)),)
    assert route.slots[:2] == (283, 284)
    assert route.slots.count(283) == 1


@pytest.mark.parametrize(
    ("held_mask", "expected_slot"),
    (
        (0x2C10, 482),  # 4A+B+G -> 4A+G
        (0x3410, 482),  # 4A+K+G -> 4A+G
        (0x3810, 408),  # 4B+K+G -> B+G
    ),
)
def test_astaroth_three_button_nonmove_retains_idle_until_frame_one(
    held_mask, expected_slot
):
    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    definition = MovePlayDefinition(
        definition_id=0x7300 + expected_slot,
        authored_definition_id=0x7300 + expected_slot,
        initial_cell=0,
        raw_words=(),
        region_end_cell=2,
        script_end_cell=2,
        status="native-linear",
        button_steps=(MovePlayButtonStep(0, held_mask, 2),),
        branch_cells=(),
    )

    trace = resolver.resolve_live_transition_publications(definition)

    assert trace.publications == ((1, (expected_slot,)),)


def test_astaroth_bkg_delays_only_reversal_edge_controller_selection():
    """B+K+G delays entry into the RE controller, not necessarily its strike.

    B+G normally enters slot 408 on frame 0. Extra K leaves idle lane 0
    without a target for that sample, and the held chord enters slot 408 on
    frame 1. Slot 408 owns separate charge/release control flow, so this test
    must not promote the selector offset into a one-frame attack-delay claim.
    """

    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )

    def definition(definition_id: int, mask: int) -> MovePlayDefinition:
        return MovePlayDefinition(
            definition_id=definition_id,
            authored_definition_id=definition_id,
            initial_cell=0,
            raw_words=(),
            region_end_cell=4,
            script_end_cell=4,
            status="native-linear",
            button_steps=(
                MovePlayButtonStep(0, mask, 2),
                MovePlayButtonStep(2, 0x0001, 5),
            ),
            branch_cells=(),
        )

    ordinary = definition(0x7400, 0x2820)  # neutral B+G
    delayed = definition(0x7401, 0x3820)   # neutral B+K+G
    ordinary_trace = resolver.resolve_live_transition_publications(ordinary)
    delayed_trace = resolver.resolve_live_transition_publications(delayed)
    assert ordinary_trace.publications == ((0, (408,)),)
    assert delayed_trace.publications == ((1, (408,)),)


def test_astaroth_preheld_guard_then_bk_does_not_produce_delayed_reversal_edge():
    """The B+K+G trick needs G's edge in the three-button sample."""

    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    definition = MovePlayDefinition(
        definition_id=0x7402,
        authored_definition_id=0x7402,
        initial_cell=0,
        raw_words=(),
        region_end_cell=4,
        script_end_cell=4,
        status="native-linear",
        button_steps=(
            MovePlayButtonStep(0, 0x2020, 3),  # hold neutral G first
            MovePlayButtonStep(2, 0x3820, 4),  # then add B+K
        ),
        branch_cells=(),
    )

    assert resolver.resolve_live_transition_publications(
        definition,
        meter_state_shorts={10: 0},
        special_lethal_match_mode=0,
    ).publications == ()


def test_astaroth_all_button_chord_delays_4a_until_next_sample_bg_release():
    """An over-complete first sample preserves A for one release sample.

    4A+B+K+G has no idle-selector publication. Releasing B+G while retaining
    4A+K on the immediately following sample publishes ordinary slot-283 4A.
    """

    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    definition = MovePlayDefinition(
        definition_id=0x7411,
        authored_definition_id=0x7411,
        initial_cell=0,
        raw_words=(),
        region_end_cell=4,
        script_end_cell=4,
        status="native-linear",
        button_steps=(
            MovePlayButtonStep(0, 0x3C10, 1),              # 4A+B+K+G
            MovePlayButtonStep(2, 0x1410, 3),              # release B+G -> 4A+K
        ),
        branch_cells=(),
    )

    trace = resolver.resolve_live_transition_publications(
        definition,
        meter_state_shorts={10: 0},
        special_lethal_match_mode=0,
    )
    route = resolve_input_timed_attack_route(
        khd,
        283,
        definition,
        command_table,
        codec,
        selected_move_play_frame=1,
        character_profile_id=0x0C,
    )

    assert trace.publications == ((1, (283,)),)
    assert route.slots[0] == 283
    assert route.attack_cells[0] == 45
    assert route.startup_impact_coordinate == 11


@pytest.mark.parametrize("held_frames", (2, 8))
def test_astaroth_all_button_4a_release_expires_after_one_sample(held_frames):
    """The all-button construction is a +1 window, not an indefinite store."""

    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    definition = MovePlayDefinition(
        definition_id=0x7411 + held_frames,
        authored_definition_id=0x7411 + held_frames,
        initial_cell=0,
        raw_words=(),
        region_end_cell=4,
        script_end_cell=4,
        status="native-linear",
        button_steps=(
            MovePlayButtonStep(0, 0x3C10, held_frames),
            MovePlayButtonStep(2, 0x1410, 3),
        ),
        branch_cells=(),
    )

    assert resolver.resolve_live_transition_publications(
        definition,
        meter_state_shorts={10: 0},
        special_lethal_match_mode=0,
    ).publications == ()


def test_astaroth_one_frame_stored_4a_has_same_first_contact_one_frame_later():
    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )

    ordinary = MovePlayDefinition(
        0x7419, 0x7419, 0, (), 2, 2, "native-linear",
        (MovePlayButtonStep(0, 0x0410, 3),), (),
    )
    stored = MovePlayDefinition(
        0x741A, 0x741A, 0, (), 4, 4, "native-linear",
        (
            MovePlayButtonStep(0, 0x3C10, 1),
            MovePlayButtonStep(2, 0x1410, 3),
        ),
        (),
    )
    ordinary_trace = resolver.resolve_live_transition_publications(
        ordinary, meter_state_shorts={10: 0}, special_lethal_match_mode=0
    )
    stored_trace = resolver.resolve_live_transition_publications(
        stored, meter_state_shorts={10: 0}, special_lethal_match_mode=0
    )
    ordinary_route = resolve_input_timed_attack_route(
        khd, 283, ordinary, command_table, codec,
        selected_move_play_frame=0, character_profile_id=0x0C,
    )
    stored_route = resolve_input_timed_attack_route(
        khd, 283, stored, command_table, codec,
        selected_move_play_frame=1, character_profile_id=0x0C,
    )

    assert ordinary_trace.publications == ((0, (283,)),)
    assert stored_trace.publications == ((1, (283,)),)
    assert ordinary_route.attack_cells[0] == stored_route.attack_cells[0] == 45
    assert ordinary_route.startup_impact_coordinate == 11
    assert stored_route.startup_impact_coordinate == 11
    assert (
        stored_trace.publications[0][0] + stored_route.startup_impact_coordinate
        == ordinary_trace.publications[0][0]
        + ordinary_route.startup_impact_coordinate
        + 1
    )


def test_astaroth_release_only_plus_one_strike_families_cover_all_directions():
    """Lock the non-throw results from the 729-case release-only matrix."""

    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    directions = {
        "5": 0x0020, "1": 0x0002, "2": 0x0004, "3": 0x0008,
        "4": 0x0010, "6": 0x0040, "7": 0x0080, "8": 0x0100,
        "9": 0x0200,
    }
    families = (
        # First chord, release result, expected slots by 5/1/2/3/4/6/7/8/9.
        (0x3C00, 0x0C00, (371, 376, 376, 376, 377, 372, 375, 375, 375)),
        (0x3C00, 0x1400, (264, 272, 271, 270, 283, 269, 427, 426, 425)),
        (0x3C00, 0x1800, (380, 398, 398, 398, 389, 383, 402, 404, 406)),
    )

    definition_id = 0x7420
    for first_buttons, second_buttons, expected_slots in families:
        for (direction, direction_mask), expected_slot in zip(
            directions.items(), expected_slots
        ):
            ordinary = MovePlayDefinition(
                definition_id, definition_id, 0, (), 2, 2, "native-linear",
                (MovePlayButtonStep(0, direction_mask | second_buttons, 3),),
                (),
            )
            definition_id += 1
            delayed = MovePlayDefinition(
                definition_id, definition_id, 0, (), 4, 4, "native-linear",
                (
                    MovePlayButtonStep(0, direction_mask | first_buttons, 1),
                    MovePlayButtonStep(2, direction_mask | second_buttons, 3),
                ),
                (),
            )
            definition_id += 1

            assert resolver.resolve_live_transition_publications(
                ordinary,
                meter_state_shorts={10: 0},
                special_lethal_match_mode=0,
            ).publications == ((0, (expected_slot,)),), direction
            assert resolver.resolve_live_transition_publications(
                delayed,
                meter_state_shorts={10: 0},
                special_lethal_match_mode=0,
            ).publications == ((1, (expected_slot,)),), direction


def test_astaroth_one_tick_prefix_matrix_has_no_automatic_4a_delay_token():
    """Exhaust the physical direction x A/B/K/G prefix namespace.

    A no-publication prefix can occupy a sampled tick, but the live selector
    carries no deferred 4A token: the following 4A is selected only on its own
    authored tick.  Prefixes which already contain A also consume its rising
    edge and therefore suppress that following 4A.
    """

    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    directions = {
        "5": 0x0020,
        "1": 0x0002,
        "2": 0x0004,
        "3": 0x0008,
        "4": 0x0010,
        "6": 0x0040,
        "7": 0x0080,
        "8": 0x0100,
        "9": 0x0200,
    }
    button_bits = (0x0400, 0x0800, 0x1000, 0x2000)
    delayed_only_on_its_own_tick = set()
    no_publication_on_either_tick = set()

    for direction_index, (direction, direction_mask) in enumerate(directions.items()):
        for button_mask_index in range(16):
            button_mask = sum(
                bit
                for index, bit in enumerate(button_bits)
                if button_mask_index & (1 << index)
            )
            definition_id = 0x7000 + direction_index * 16 + button_mask_index
            definition = MovePlayDefinition(
                definition_id=definition_id,
                authored_definition_id=definition_id,
                initial_cell=0,
                raw_words=(),
                region_end_cell=4,
                script_end_cell=4,
                status="native-linear",
                button_steps=(
                    MovePlayButtonStep(0, direction_mask | button_mask, 1),
                    MovePlayButtonStep(2, 0x0410, 1),  # authored 4_A
                ),
                branch_cells=(),
            )
            publications = resolver.resolve_live_transition_publications(
                definition
            ).publications
            frame_zero = tuple(targets for frame, targets in publications if frame == 0)
            frame_one = tuple(targets for frame, targets in publications if frame == 1)
            key = (direction, button_mask_index)
            if not frame_zero and frame_one == ((283,),):
                delayed_only_on_its_own_tick.add(key)
            if not frame_zero and not frame_one:
                no_publication_on_either_tick.add(key)

    # No-button prefixes work in every direction. G and B+K+G also publish no
    # move except at 6, where extra-button matching reaches the 6B+G family.
    expected_own_tick = {(direction, 0x0) for direction in directions}
    expected_own_tick |= {
        (direction, buttons)
        for direction in directions
        if direction != "6"
        for buttons in (0x8, 0xE)  # G; B+K+G
    }
    assert delayed_only_on_its_own_tick == expected_own_tick

    # A+G, A+K+G, and A+B+K+G publish neither the prefix nor the following 4A:
    # A was already down, so the second tick has no A rising edge. At direction
    # 6 only the all-four-buttons case remains in this class.
    expected_suppressed = {
        (direction, buttons)
        for direction in directions
        if direction != "6"
        for buttons in (0xB, 0xD, 0xF)
    }
    expected_suppressed.add(("6", 0xF))
    assert no_publication_on_either_tick == expected_suppressed


def test_astaroth_input_cancels_have_no_one_to_three_frame_literal_exit():
    """Reject attack-cancel spacers, not only invalid standing prefixes.

    A literal transition start close to its destination's total coordinate
    would be an authored short recovery segment.  Scan every input-conditioned
    transition in Astaroth's bank so a hidden A/B/K/G cancel cannot silently
    become a one-frame spacer.
    """

    _, khd, _, _ = _astaroth_inputs()
    short_exits = []

    def concrete_words(values):
        words = []
        for value in values:
            as_int = getattr(value, "as_int", None)
            word = as_int() if callable(as_int) else None
            if word is None:
                return None
            words.append(int(word) & 0xFFFF)
        return tuple(words)

    for source_slot, slot in enumerate(khd.slots):
        if slot.bytecode is None:
            continue
        for transition in emulate(slot.bytecode, source_slot).transitions:
            predicate = transition.predicate
            if predicate is None:
                continue
            predicate_words = concrete_words(predicate.args)
            transition_words = concrete_words(transition.args)
            if (
                not predicate_words
                or predicate_words[0] not in _OFFLINE_INPUT_TRANSITION_SUBOPS
                or not transition_words
            ):
                continue
            target_slot = khd.resolve_packed_slot(transition.next_move_id_raw)
            if target_slot is None:
                continue
            target_start = transition_words[1] if len(transition_words) >= 2 else 0
            if target_start >= 0x6000:
                continue  # Native timing-index token, not a literal coordinate.
            remaining = int(khd.slots[target_slot].wTotalFrames) - target_start
            if remaining <= 3:
                short_exits.append(
                    (source_slot, target_slot, target_start, remaining)
                )

    assert short_exits == []


def test_astaroth_slot244_is_not_a_one_frame_attack_spacer():
    """The tempting ``(target, 1, 1)`` tuple is not a one-tick timer.

    Slot 244 reaches that author only under CALLCOND 0x25's timing predicate
    at coordinate 5.  Since threshold 1 is already in the past, the native
    decoder commits slot 371 immediately; slot 371's first strike is still at
    coordinate 33.  Treating the predicate word ``5`` as IF subopcode 5 would
    invent a false one-frame cancel route.
    """

    _, khd, _, _ = _astaroth_inputs()
    transitions = emulate(khd.slots[244].bytecode, 244).transitions
    candidate = next(
        transition
        for transition in transitions
        if khd.resolve_packed_slot(transition.next_move_id_raw) == 371
    )

    assert candidate.predicate is not None
    assert candidate.predicate.callcond_idx == 0x25
    assert tuple(value.as_int() for value in candidate.predicate.args) == (5,)
    assert tuple(value.as_int() for value in candidate.args) == (371, 1, 1)
    assert min(
        khd.sections[0].entries[cell_index].wI16MasterWindowStart
        for cell_index in khd.slots[371].nCellBoneIndexPerVariant
        if cell_index >= 0
    ) == 33


def test_astaroth_dynamic_timing_tokens_hide_no_one_frame_strike_transition():
    """Audit the transitions excluded by the literal-coordinate scan.

    Every dynamic-token transition into a damaging Astaroth slot either has
    no authored threshold (immediate package) or uses the same token for start
    and threshold.  There is therefore no dynamic ``threshold = start + 1``
    strike route hiding behind the 0x6000+ timing mapper.
    """

    _, khd, _, _ = _astaroth_inputs()
    suspicious = []

    def concrete_words(values):
        words = []
        for value in values:
            word = value.as_int()
            if word is None:
                return None
            words.append(word & 0xFFFF)
        return tuple(words)

    for source_slot, slot in enumerate(khd.slots):
        if slot.bytecode is None:
            continue
        for transition in emulate(slot.bytecode, source_slot).transitions:
            words = concrete_words(transition.args)
            if not words or transition.next_move_id_raw is None:
                continue
            target_slot = khd.resolve_packed_slot(transition.next_move_id_raw)
            if target_slot is None:
                continue
            damaging_target = any(
                cell_index >= 0
                and cell_index < len(khd.sections[0].entries)
                and khd.sections[0].entries[cell_index].wI16BaseDamage > 0
                for cell_index in khd.slots[target_slot].nCellBoneIndexPerVariant
            )
            if not damaging_target:
                continue
            start = words[1] if len(words) >= 2 else 0
            threshold = words[2] if len(words) >= 3 else None
            has_dynamic_token = any(
                word >= 0x6000 and word not in (0x7FFF, 0xFFFF)
                for word in words[1:3]
            )
            if has_dynamic_token and threshold is not None and threshold != start:
                suspicious.append(
                    (source_slot, target_slot, start, threshold, transition.source_pc)
                )

    assert suspicious == []


def test_astaroth_6k_then_g_correction_requires_special_mode_two():
    """The apparent 6K~G restart is unavailable in normal battle mode zero.

    Standing ``6K`` publishes slot 353.  If G is added on the next authored
    sample, common helper 3192's coordinate-0..1 correction block replaces it
    with slot 354.  The two slots use the same animation, duration, damage,
    and active coordinates, so restarting slot 354 one sample later shifts the
    complete strike by one frame, but only when IF 0x13DA observes global
    mode value two. A live normal replay observes value zero.
    """

    _, khd, command_table, codec = _astaroth_inputs()
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    source = khd.slots[353]

    # Releasing K while adding G is sufficient; retaining K also works.  The
    # correction tests the two history samples, not a requirement that K stay
    # held on the second sample.
    for definition_id, second_sample_mask in (
        (0x7403, 0x2040),  # 6K -> 6G
        (0x7404, 0x2840),  # 6K -> 6B+G
        (0x7405, 0x3040),  # 6K -> 6K+G
        (0x7406, 0x3840),  # 6K -> 6B+K+G
    ):
        definition = MovePlayDefinition(
            definition_id=definition_id,
            authored_definition_id=definition_id,
            initial_cell=0,
            raw_words=(),
            region_end_cell=5,
            script_end_cell=5,
            status="native-linear",
            button_steps=(
                MovePlayButtonStep(0, 0x1040, 1),  # 6K
                MovePlayButtonStep(1, second_sample_mask, 3),
                MovePlayButtonStep(3, 0x0001, 1),
            ),
            branch_cells=(),
        )
        assert resolver.resolve_live_transition_publications(
            definition
        ).publications == ((0, (353,)),)

        input_state = replace(
            build_definition_input_states(definition, codec)[1],
            active_move_id=353,
            animation_frame=1,
            primary_script_running=False,
            secondary_script_running=False,
            animation_ended=False,
            character_profile_id=0x0C,
            command_table=command_table,
            special_lethal_match_mode=2,
        )

        def evaluate(function_index, args):
            return _route_callcond_evaluator(
                function_index,
                args,
                input_state=input_state,
                animation_coordinate=1,
                lane_start_coordinate=0,
                lane_total_frames=source.wTotalFrames,
                motion_start_frame=source.nMotionAStartFrame_02,
                motion_end_frame=source.nMotionAEndFrame_04,
                playback_speed=source.playback_speed_scalar,
                character_profile_id=0x0C,
                meter_state_shorts={10: 0},
            )

        correction = emulate(
            khd.slots[3192].bytecode,
            3192,
            initial_variables={0x0F: 1},
            callcond_evaluator=evaluate,
        )
        assert [
            khd.resolve_packed_slot(event.next_move_id_raw)
            for event in correction.transitions
            if event.next_move_id_raw is not None
        ] == [354]

        normal_input_state = replace(input_state, special_lethal_match_mode=0)

        def evaluate_normal(function_index, args):
            return _route_callcond_evaluator(
                function_index,
                args,
                input_state=normal_input_state,
                animation_coordinate=1,
                lane_start_coordinate=0,
                lane_total_frames=source.wTotalFrames,
                motion_start_frame=source.nMotionAStartFrame_02,
                motion_end_frame=source.nMotionAEndFrame_04,
                playback_speed=source.playback_speed_scalar,
                character_profile_id=0x0C,
                meter_state_shorts={10: 0},
            )

        normal = emulate(
            khd.slots[3192].bytecode,
            3192,
            initial_variables={0x0F: 1},
            callcond_evaluator=evaluate_normal,
        )
        assert normal.transitions == []

    target = khd.slots[354]
    assert source.wAnimationIndex_00 == target.wAnimationIndex_00 == 235
    assert source.wTotalFrames == target.wTotalFrames == 70
    source_cell = khd.sections[0].entries[source.nCellBoneIndexPerVariant[0]]
    target_cell = khd.sections[0].entries[target.nCellBoneIndexPerVariant[0]]
    assert {
        name: getattr(source_cell, name)
        for name in source_cell.__dataclass_fields__
        if name != "offset_in_file"
    } == {
        name: getattr(target_cell, name)
        for name in target_cell.__dataclass_fields__
        if name != "offset_in_file"
    }
    assert (
        source_cell.wI16BaseDamage,
        source_cell.wI16MasterWindowStart,
        source_cell.wI16MasterWindowEnd,
    ) == (
        target_cell.wI16BaseDamage,
        target_cell.wI16MasterWindowStart,
        target_cell.wI16MasterWindowEnd,
    ) == (26, 17, 19)


def test_astaroth_4a_then_k_is_not_a_standard_context_correction():
    """Resolve the predicates which previously made ``4A~K`` look viable."""

    _, khd, command_table, codec = _astaroth_inputs()
    definition = MovePlayDefinition(
        definition_id=0x7407,
        authored_definition_id=0x7407,
        initial_cell=0,
        raw_words=(),
        region_end_cell=5,
        script_end_cell=5,
        status="native-linear",
        button_steps=(
            MovePlayButtonStep(0, 0x0410, 1),  # 4A
            MovePlayButtonStep(1, 0x1410, 3),  # add K
            MovePlayButtonStep(3, 0x0001, 1),
        ),
        branch_cells=(),
    )
    input_state = replace(
        build_definition_input_states(definition, codec)[1],
        active_move_id=283,
        animation_frame=1,
        primary_script_running=False,
        secondary_script_running=False,
        animation_ended=False,
        character_profile_id=0x0C,
        command_table=command_table,
    )
    source = khd.slots[283]

    def evaluate(function_index, args):
        return _route_callcond_evaluator(
            function_index,
            args,
            input_state=input_state,
            animation_coordinate=1,
            lane_start_coordinate=0,
            lane_total_frames=source.wTotalFrames,
            motion_start_frame=source.nMotionAStartFrame_02,
            motion_end_frame=source.nMotionAEndFrame_04,
            playback_speed=source.playback_speed_scalar,
            character_profile_id=0x0C,
            meter_state_shorts={10: 0},
        )

    correction = emulate(
        khd.slots[3192].bytecode,
        3192,
        initial_variables={0x0F: 1},
        callcond_evaluator=evaluate,
    )
    assert 301 not in {
        khd.resolve_packed_slot(event.next_move_id_raw)
        for event in correction.transitions
        if event.next_move_id_raw is not None
    }


def test_publications_are_not_synthesized_into_contacts():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(348)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd,
        command_table,
        codec,
        character_profile_id=0x0C,
    )

    candidates = resolver.resolve_definition(definition)
    latest_route = resolver.resolve_attack_route(
        definition,
        candidates.slots[0],
        selected_move_play_frame=candidates.selected_move_play_frame,
        meter_state_shorts={10: 1},
    )
    sequence = resolve_publication_entry_route(
        resolver,
        definition,
        candidates,
        authored_hit_count=2,
        meter_state_shorts={10: 1},
    )

    assert candidates.publications == ((1, (353,)),)
    assert latest_route.attack_cells == (125,)
    assert sequence is None


def test_publication_route_accepts_only_the_contacts_owned_by_first_committed_lane():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(348)
    assert definition is not None
    resolver = NativeDispatcherResolver(khd, command_table, codec)
    candidates = resolver.resolve_definition(definition)

    one_contact = resolve_publication_entry_route(
        resolver, definition, candidates, authored_hit_count=1
    )
    assert one_contact is not None
    assert one_contact.attack_slots == (353,)
    assert one_contact.attack_cells == (125,)
    assert resolve_publication_entry_route(
        resolver, definition, candidates, authored_hit_count=3
    ) is None


def test_bear_tamer_selects_unique_native_route_entry_and_latest_deferred_author():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(634)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    candidates = resolver.resolve_definition(definition)

    route = resolve_publication_entry_route(
        resolver, definition, candidates, authored_hit_count=2
    )

    assert route is not None
    assert route.slots == (308, 309)
    assert route.attack_cells == (67, 70)
    assert any(
        "input-frame=18;commit-frame=22" in resolution
        for resolution in route.resolutions
    )


@pytest.mark.parametrize("definition_id", (604, 606, 608))
def test_post_reversal_edge_choice_is_not_replayed_through_standing_dispatcher(
    definition_id,
):
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(definition_id)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0x0C
    )
    candidates = resolver.resolve_definition(definition)

    route = resolve_publication_entry_route(
        resolver, definition, candidates, authored_hit_count=1
    )

    assert route is not None
    assert candidates.slots == (408,)
    assert candidates.publications == ((1, (408,)),)
    assert route.attack_slots == (409,)
    assert route.attack_cells == (189,)
    assert not any(
        resolution.startswith("khd-conditioned-choice-selector:")
        for resolution in route.resolutions
    )


def test_reversal_edge_followup_is_not_synthesized_as_later_standing_publication():
    move_table, khd, command_table, codec = _mitsurugi_inputs()
    definition = move_table.definition(370)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd, command_table, codec, character_profile_id=0
    )
    candidates = resolver.resolve_definition(definition)

    setup_route = resolver.resolve_attack_route(
        definition,
        candidates.slots[0],
        selected_move_play_frame=candidates.selected_move_play_frame,
    )
    official_route = resolve_publication_entry_route(
        resolver, definition, candidates, authored_hit_count=3
    )

    assert candidates.slots == (319,)
    assert candidates.publications == ((0, (319,)),)
    assert setup_route.attack_slots == (320,)
    assert setup_route.attack_cells == (103,)
    assert official_route is None


@pytest.mark.parametrize("definition_id", (290, 292))
def test_tornado_spike_keeps_directional_a_opener_without_synthesizing_standing_b(
    definition_id,
):
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(definition_id)
    assert definition is not None
    resolver = NativeDispatcherResolver(khd, command_table, codec)

    start = resolver.resolve_definition(definition)
    route = resolver.resolve_attack_route(
        definition,
        start.slots[0],
        selected_move_play_frame=start.selected_move_play_frame,
    )

    assert start.slots == (269,)
    assert start.publications == ((1, (269,)),)
    assert start.selected_move_play_frame == 1
    assert route.slots == (269,)
    assert route.attack_cells == (28,)
    assert route.frame_endpoints_resolved


def test_general_unconditional_route_reproduces_audited_two_hit_chain():
    _, khd, _, _ = _astaroth_inputs()

    route = resolve_unconditional_attack_route(khd, 308)

    assert route.slots == (308, 310)
    assert route.cells == (67, 71)
    assert not route.ambiguous


def test_fiendish_assault_resolves_nested_full_meter_contact_and_hit_damage():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(629)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd,
        command_table,
        codec,
        character_profile_id=0x0C,
    )

    start = resolver.resolve_definition(definition)
    route = resolver.resolve_attack_route(
        definition,
        start.slots[0],
        selected_move_play_frame=start.selected_move_play_frame,
        meter_state_shorts={0: 120},
    )

    assert start.slots == (601,)
    assert route.slots == (601, 602, 603)
    assert route.attack_cells == (352,)
    assert not route.ambiguous
    followups = resolve_native_contact_followups(khd, 603)
    assert len(followups) == 1
    assert followups[0].target_slot == 604
    assert followups[0].target_non_attack_cell == 65
    assert followups[0].damage == 85


def test_nested_runtime_predicate_fails_closed_without_meter_state():
    move_table, khd, command_table, codec = _astaroth_inputs()
    definition = move_table.definition(629)
    assert definition is not None
    resolver = NativeDispatcherResolver(
        khd,
        command_table,
        codec,
        character_profile_id=0x0C,
    )
    start = resolver.resolve_definition(definition)

    route = resolver.resolve_attack_route(
        definition,
        start.slots[0],
        selected_move_play_frame=start.selected_move_play_frame,
    )

    assert route.ambiguous
    assert route.attack_cells == ()
    assert any(
        resolution.startswith("khd-nested-transition-predicate-unresolved")
        for resolution in route.resolutions
    )


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

    assert held_route.slots == (377, 378, 379)
    assert held_route.cells == (153, 154)
    assert held_route.attack_slots == (378,)
    assert held_route.attack_cells == (154,)
    assert not held_route.startup_timing_resolved
    assert any(
        "clock-alignment=unproven" in resolution
        for resolution in held_route.resolutions
    )
