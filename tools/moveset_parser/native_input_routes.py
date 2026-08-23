"""Conservative native CPUAI-input to KHD-slot resolution.

The official move list points at a CPUAI command-player definition.  That
definition publishes compact input words; it is not a KHD move or attack-cell
index.  This module performs the missing static join:

1. lift the command-player BTN mask exactly as
   ``LuxMoveVM_ExecuteAndDumpOpcode @ 0x140365900`` does;
2. commit the transformed snapshots to the native 0x140-entry history ring;
3. abstractly execute the common KHD input dispatcher (packed slot 0x3048),
   resolving input predicates and branching both ways for unavailable battle
   state; and
4. return every concrete local KHD slot still possible.

The result is deliberately inference, not confirmation.  The per-command age
array at character+0x331A is live battle state.  Offline move-play resolution
uses a saturated/sufficient history age after a neutral pre-roll and records
that assumption in the evidence string.  Unknown non-input predicates never
receive a guessed value.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, replace

from lux_input_codec import LuxInputCodecTables
from lux_input_history import (
    CurrentInputSnapshot,
    InputHistoryEntry,
    InputHistoryRing,
    TransitionConditionRow,
    check_history_condition,
    check_motion_condition_flags,
)
from lux_input_pipeline import CharacterInputPipelineState, RawInputSourceState
from native_move_commands import (
    MovePlayDefinition,
    TransitionCommandTable,
)
from lux_movement_vm import (
    LaneTimingState,
    MovementHelperState,
    evaluate_if as evaluate_movement_if,
    evaluate_timing,
    map_timing_index,
)
from lux_reference_engine import StaticResolutionError
from stackvm_emulate import Concrete, StackVal, emulate


Val = frozenset[int] | None
TOP: Val = None
BOOL: Val = frozenset((0, 1))
ZERO: Val = frozenset((0,))
# A dispatcher may legitimately expose a few hundred state-dependent move
# candidates.  Keep them explicit so ambiguity is visible in the export;
# widening to TOP here would throw away the very slot evidence we are after.
MAX_VALUE_SET = 4096


def _one(value: int) -> Val:
    return frozenset((value & 0xFFFF,))


def _union(left: Val, right: Val) -> Val:
    if left is None or right is None:
        return None
    result = left | right
    return result if len(result) <= MAX_VALUE_SET else None


def _singleton(value: Val) -> int | None:
    if value is None or len(value) != 1:
        return None
    return next(iter(value))


def _signed(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def _binary(opcode: int, left: Val, right: Val) -> Val:
    if left is None or right is None:
        return BOOL if 0x1F <= opcode <= 0x24 else None
    if len(left) * len(right) > MAX_VALUE_SET:
        return BOOL if 0x1F <= opcode <= 0x24 else None
    out: set[int] = set()
    for raw_left in left:
        for raw_right in right:
            a, b = _signed(raw_left), _signed(raw_right)
            if opcode == 0x0C:
                value = a + b
            elif opcode == 0x0D:
                value = a - b
            elif opcode == 0x0E:
                value = a * b
            elif opcode == 0x0F:
                if b == 0:
                    return None
                value = int(a / b)
            elif opcode == 0x10:
                if b == 0:
                    return None
                value = a - int(a / b) * b
            elif opcode == 0x14:
                value = raw_left & raw_right
            elif opcode == 0x15:
                value = raw_left | raw_right
            elif opcode == 0x17:
                value = a << (raw_right & 0x1F)
            elif opcode == 0x18:
                value = a >> (raw_right & 0x1F)
            elif opcode == 0x1F:
                value = int(a == b)
            elif opcode == 0x20:
                value = int(a != b)
            elif opcode == 0x21:
                value = int(a < b)
            elif opcode == 0x22:
                value = int(a <= b)
            elif opcode == 0x23:
                value = int(a > b)
            elif opcode == 0x24:
                value = int(a >= b)
            else:
                return None
            out.add(value & 0xFFFF)
            if len(out) > MAX_VALUE_SET:
                return None
    return frozenset(out)


# Exact low-16 BTN mask conversion at 0x140367408.  _W and _NOGUARD are
# command-player annotations and do not contribute to the compact word.
_CPUAI_DIRECTION_TO_COMPACT = (
    (0x0002, 0x1800),  # 1
    (0x0004, 0x1000),  # 2
    (0x0008, 0x1400),  # 3
    (0x0010, 0x0800),  # 4
    (0x0020, 0x0000),  # 5
    (0x0040, 0x0400),  # 6
    (0x0080, 0x2800),  # 7
    (0x0100, 0x2000),  # 8
    (0x0200, 0x2400),  # 9
)


def cpuai_button_mask_to_compact(mask: int) -> int:
    mask &= 0xFFFF
    compact = 0
    for authored_bit, compact_bits in _CPUAI_DIRECTION_TO_COMPACT:
        if mask & authored_bit:
            compact |= compact_bits
    if mask & 0x0400:
        compact |= 0x01
    if mask & 0x0800:
        compact |= 0x02
    if mask & 0x1000:
        compact |= 0x04
    if mask & 0x2000:
        compact |= 0x08
    if mask & 0x8000:
        compact |= 0x20
    return compact


@dataclass(frozen=True)
class NativeInputState:
    snapshot: CurrentInputSnapshot
    history: InputHistoryRing
    command_table: TransitionCommandTable
    command_age_limit: int
    chara_state_shorts: tuple[int, ...] = (1,) + (0,) * 73
    previous_chara_state_shorts: tuple[int, ...] = (1,) + (0,) * 73
    opponent_previous_chara_state_shorts: tuple[int, ...] = (1,) + (0,) * 73
    motion_state_latches: tuple[int, ...] = (0,) * 0x72
    # Zero-based tick within the authored CPUAI move-play program.  The
    # neutral pre-roll is deliberately excluded so this can be compared with
    # MoveVM animation coordinates after the standing dispatcher selects a
    # combat slot.
    move_play_frame: int = -1
    # Optional active-lane context used only when statically walking an
    # already-selected combat move (including its nested common helpers).
    # The neutral dispatcher leaves these unset.
    active_move_id: int | None = None
    animation_frame: int | None = None
    primary_script_running: bool | None = None
    secondary_script_running: bool | None = None
    animation_ended: bool | None = None
    # IF 0x0066 reads byte[index] from the per-frame motion-state edge bank at
    # ALuxBattleChara+0x1826 and compares it with literal 1.  The pre-main
    # snapshot writes +1 when current flag[index] changes from zero to
    # nonzero, -1 when it becomes zero, and 0 when unchanged.  Zero is proven
    # before the current slot's first contact; outcome-specific route walks
    # supply the statically proven rising edges after contact.
    per_frame_motion_flags: tuple[int, ...] | None = (0,) * 0x72
    # Appended to preserve the positional layout used by older timeline
    # builders; new code should prefer replace()/keywords.
    opponent_motion_state_latches: tuple[int, ...] = (0,) * 0x72
    world_mode: int | None = 2
    # IF 0x0013 is the native movement-helper angle/profile predicate.  The
    # ordinary player-facing dispatcher baseline is a head-on opponent angle;
    # the profile id joins ALuxBattleChara+0x24E to the exported character.
    character_profile_id: int | None = None
    opponent_relative_angle_turns: float | None = 0.0
    opponent_facing_delta_turns: float | None = 0.0
    # IF 0x0041 samples the active lane's two transition target words.  Lane
    # initialization/deactivation writes the inactive sentinel 0xFFFF to both;
    # transition authors replace one of them with a concrete target.  These
    # fields are appended to preserve older positional construction.
    immediate_transition_target: int | None = 0xFFFF
    deferred_transition_target: int | None = 0xFFFF
    # IF 0x138A reads the signed MoveVM meter/state bank.  Dispatcher callers
    # must supply the live battle context; assuming the route analyzer's
    # ordinary-move baseline here incorrectly prunes Soul Charge selectors.
    meter_state_shorts: tuple[int | None, ...] = (None,) * 17
    # IF 0x13DA compares against a runtime global whose ordinary-match value
    # is not yet independently proven.  Keep it abstract unless supplied.
    special_lethal_match_mode: int | None = None


def _build_definition_input_states(
    definition: MovePlayDefinition,
    tables: LuxInputCodecTables,
    *,
    neutral_preroll: int = 30,
    retain_every_tick: bool = False,
) -> tuple[NativeInputState, ...]:
    """Publish a linear CPUAI script and retain its observable input edges.

    Move-play scripts commonly end with ``_W``.  Resolving only the final
    snapshot therefore asks the KHD dispatcher about neutral input after the
    attack has already been recognized.  The native consumer runs every tick,
    so retain the first and final publication of every authored BTN span.  The
    pair preserves both press-edge predicates and duration/window predicates
    without repeating identical middle-of-hold snapshots blindly.
    """
    if not definition.is_linear or not definition.button_steps:
        return ()
    pipeline = CharacterInputPipelineState()
    sources = RawInputSourceState()
    history = InputHistoryRing()
    snapshot = CurrentInputSnapshot()

    def tick(compact: int) -> None:
        nonlocal snapshot
        sources.cpu_current_words[0] = compact
        snapshot, _ = pipeline.tick(
            move_id=2,
            player_slot=0,
            opponent_slot=1,
            sources=sources,
            tables=tables,
            raw_transforms=None,
            encoded_transforms=None,
            camera_side_matches_player=True,
        )
        history.commit_snapshot(snapshot)

    for _ in range(neutral_preroll):
        tick(0)
    observations: list[NativeInputState] = []
    move_play_frame = 0
    for step in definition.button_steps:
        compact = cpuai_button_mask_to_compact(step.mask)
        duration = max(0, int(step.duration_frames))
        for tick_index in range(duration):
            tick(compact)
            if not retain_every_tick and tick_index not in (0, duration - 1):
                move_play_frame += 1
                continue
            observations.append(NativeInputState(
                snapshot=snapshot,
                history=InputHistoryRing(
                    entries=list(history.entries),
                    cursor=history.cursor,
                    tick_count=history.tick_count,
                ),
                command_table=TransitionCommandTable(()),
                command_age_limit=min(max(1, history.tick_count), 0x7FFF),
                move_play_frame=move_play_frame,
            ))
            move_play_frame += 1
    return tuple(observations)


def build_definition_input_states(
    definition: MovePlayDefinition,
    tables: LuxInputCodecTables,
    *,
    neutral_preroll: int = 30,
) -> tuple[NativeInputState, ...]:
    """Return edge observations used by the neutral standing dispatcher."""

    return _build_definition_input_states(
        definition,
        tables,
        neutral_preroll=neutral_preroll,
        retain_every_tick=False,
    )


def build_definition_input_timeline(
    definition: MovePlayDefinition,
    tables: LuxInputCodecTables,
    *,
    neutral_preroll: int = 30,
) -> tuple[NativeInputState, ...]:
    """Return every authored input tick for in-move transition evaluation."""

    return _build_definition_input_states(
        definition,
        tables,
        neutral_preroll=neutral_preroll,
        retain_every_tick=True,
    )


def _history_primary_sequence(state: NativeInputState, args: tuple[int, ...]) -> bool | None:
    if len(args) < 2:
        return None
    pattern = args[1]
    current = state.snapshot.current_compact_word & 0xFFFF
    if len(args) == 2:
        return check_motion_condition_flags(pattern, current)
    if not check_motion_condition_flags(pattern, current):
        return False
    required = args[2] & 0xFFFF
    if required <= 1:
        return True
    window = 10 if len(args) == 3 else _signed(args[3])
    if window < 2:
        return False
    # Native IF 0x0005 initializes at cursor-1 and pre-decrements before the
    # first read, so cursor 0/1 first reads 0x13F.
    index = state.history.cursor - 1
    if index < 1:
        index = 0
    matched = 1
    separator = False
    for _ in range(window - 1):
        index = InputHistoryRing.previous(index)
        entry = state.history.entries[index]
        if not separator:
            if (entry.current_compact_word & 0xF) == 0:
                separator = True
        elif check_motion_condition_flags(pattern, entry.current_compact_word):
            matched += 1
            if matched >= required:
                return True
            separator = False
    return False


def _history_secondary(state: NativeInputState, args: tuple[int, ...]) -> bool | None:
    if len(args) < 2:
        return None
    pattern = args[1]
    if len(args) <= 2:
        word = state.snapshot.secondary_compact_word
    else:
        count = args[2] & 0xFFFF
        word = 0
        index = state.history.cursor
        for _ in range(count):
            word |= state.history.entries[index].secondary_compact_word
            index = InputHistoryRing.previous(index)
    return check_motion_condition_flags(pattern, word)


def _history_primary_all(state: NativeInputState, args: tuple[int, ...]) -> bool | None:
    """Mirror IF 0x0020's current-plus-prior all-frames matcher.

    Ghidra case 0x140374248 first checks the current +0x2150 compact word,
    rejects counts <= 1, then ANDs the same motion-pattern result across
    ``count - 1`` older +0x2190 primary words.  Its loop pre-decrements an
    already cursor-minus-one index, so the first ring sample is cursor-2.
    """

    if len(args) < 3:
        return None
    pattern = args[1] & 0xFFFF
    count = args[2] & 0xFFFF
    if not check_motion_condition_flags(
        pattern, state.snapshot.current_compact_word
    ):
        return False
    if count <= 1:
        return False
    index = state.history.cursor - 1
    for _ in range(count - 1):
        index = InputHistoryRing.previous(index)
        if not check_motion_condition_flags(
            pattern, state.history.entries[index].current_compact_word
        ):
            return False
    return True


def _command_condition(state: NativeInputState, command_id: int, mode: int) -> bool | None:
    definition = state.command_table.definition(command_id)
    if definition is None:
        return None
    rows = tuple(
        TransitionConditionRow(
            repeat_count=row.repeat_count,
            condition_word_a=row.condition_word_a,
            condition_word_b=row.condition_word_b,
            initial_scan_window=row.initial_scan_window,
        )
        for row in definition.rows
    )
    return state.history.evaluate_transition(
        rows,
        condition_mode=mode,
        age_limit=state.command_age_limit,
    )


def evaluate_input_if(
    state: NativeInputState,
    args: tuple[int, ...],
    *,
    animation_frame: int | None = None,
) -> int | None:
    if not args:
        return None
    subop = args[0] & 0xFFFF
    result: bool | None
    if subop == 0x0001:
        # LuxMoveVM_EvaluateIfOpcode case 1 @ 0x1403732F0 performs a raw
        # ushort mask test against FLuxCharaCurrentInputSnapshot+0x14
        # (ALuxBattleChara+0x2164), the decoded high input nibble.  This is
        # direction state, not an attack-button mask.
        if len(args) < 2:
            return None
        result = (args[1] & state.snapshot.high_input_nibble) != 0
    elif subop == 0x0003:
        # LuxMoveVM_EvaluateIfOpcode case 3 @ 0x1403732F0 performs an
        # unchecked raw ushort mask test against ALuxBattleChara+0x2178,
        # the current side-held direction mask.  This is deliberately not
        # the decoded direction ID at +0x2170.
        if len(args) < 2:
            return None
        result = (args[1] & state.snapshot.side_direction_mask) != 0
    elif subop == 0x0005:
        result = _history_primary_sequence(state, args)
    elif subop == 0x0006:
        result = _history_secondary(state, args)
    elif subop == 0x0020:
        result = _history_primary_all(state, args)
    elif subop == 0x0024:
        entry = state.snapshot.history_entry()
        result = all(check_history_condition(entry, clause) for clause in args[1:])
    elif subop in (0x13AE, 0x13AF):
        # Alternate self matchers at 0x140375E93/0x140375FF8 use the same
        # nibble-clause format as IF 0x24/0x25.  With motion latch 9 clear
        # (the reset-backed ordinary baseline), 0x13AE reads the primary
        # snapshot and 0x13AF reads the secondary snapshot.  A set latch
        # mirrors the side-relative fields; keep decoded-ID mirroring
        # unresolved until its count-indexed native lookup is lifted, but the
        # side mask's exact 1<->2 / 4<->8 swap is independently proven.
        if len(args) < 2 or len(state.motion_state_latches) <= 9:
            return None
        snapshot = state.snapshot
        secondary = subop == 0x13AF
        entry = InputHistoryEntry(
            current_compact_word=(
                snapshot.secondary_compact_word
                if secondary else snapshot.current_compact_word
            ),
            secondary_compact_word=snapshot.secondary_compact_word,
            side_decoded_input_id=(
                snapshot.side_decoded_secondary_input_id
                if secondary else snapshot.side_decoded_input_id
            ) & 0xFFFF,
            side_direction_mask=(
                snapshot.side_secondary_direction_mask
                if secondary else snapshot.side_direction_mask
            ) & 0xFFFF,
            decoded_high_nibble_input_id=(
                snapshot.decoded_secondary_high_nibble_input_id
                if secondary else snapshot.decoded_high_nibble_input_id
            ) & 0xFFFF,
            high_input_nibble=(
                snapshot.secondary_high_input_nibble
                if secondary else snapshot.high_input_nibble
            ) & 0xFFFF,
        )
        if state.motion_state_latches[9] != 0:
            clauses = tuple(args[1:])
            if any((clause & 0xF000) == 0x1000 for clause in clauses):
                return None
            mask = entry.side_direction_mask
            mirrored_mask = mask
            if mask & 0x3:
                mirrored_mask ^= 0x3
            if mask & 0xC:
                mirrored_mask ^= 0xC
            entry = replace(entry, side_direction_mask=mirrored_mask)
        result = all(check_history_condition(entry, clause) for clause in args[1:])
    elif subop in (0x002B, 0x002C, 0x0084):
        if len(args) < 2:
            return None
        mode = {0x002B: 1, 0x002C: 0, 0x0084: 2}[subop]
        result = _command_condition(state, args[1] & 0xFFFF, mode)
    elif subop == 0x138A:
        if len(args) < 4:
            return None
        selector = _signed(args[1])
        if not 0 <= selector < len(state.meter_state_shorts):
            return None
        value = state.meter_state_shorts[selector]
        if value is None:
            return None
        result = _signed(args[2]) <= int(value) <= _signed(args[3])
    elif subop == 0x13C5:
        if len(args) < 2 or state.character_profile_id is None:
            return None
        result = _signed(args[1]) == int(state.character_profile_id)
    elif subop == 0x13DA:
        if len(args) != 2 or state.special_lethal_match_mode is None:
            return 0 if len(args) != 2 else None
        result = _signed(args[1]) == int(state.special_lethal_match_mode)
    elif subop == 0x0008:
        # LuxMoveVM_EvaluateIfOpcode case 8 samples the active lane's
        # animation frame and delegates to LuxMoveVM_CheckFrameInTimingWindow
        # @ 0x1403012B0.  Exact argc 2 is a point, exact argc 3 is an
        # inclusive window, and raw 0x7FFF disables the corresponding bound.
        # Dynamic timing-map indices (>= 0x6000, other than the sentinel)
        # require live lane/motion state and therefore remain unresolved.
        if animation_frame is None:
            animation_frame = state.animation_frame
        if animation_frame is None or len(args) not in (2, 3):
            return None
        lower_raw = args[1] & 0xFFFF
        if lower_raw != 0x7FFF and lower_raw >= 0x6000:
            return None
        lower = _signed(lower_raw)
        if len(args) == 2:
            result = lower_raw == 0x7FFF or animation_frame == lower
        else:
            upper_raw = args[2] & 0xFFFF
            if upper_raw != 0x7FFF and upper_raw >= 0x6000:
                return None
            upper = _signed(upper_raw)
            result = (
                (lower_raw == 0x7FFF or lower <= animation_frame)
                and (upper_raw == 0x7FFF or animation_frame <= upper)
            )
    elif subop == 0x000E:
        # Exact raw active-lane move-id comparison.  Nested common helpers do
        # not replace the owning lane's move id, so the caller propagates the
        # selected combat slot through the entire recursive script walk.
        if len(args) < 2 or state.active_move_id is None:
            return None
        result = (args[1] & 0xFFFF) == (state.active_move_id & 0xFFFF)
    elif subop == 0x0009:
        if state.animation_ended is None:
            return None
        result = state.animation_ended
    elif subop == 0x0010:
        if state.primary_script_running is None:
            return None
        result = state.primary_script_running
    elif subop == 0x0054:
        if state.secondary_script_running is None:
            return None
        result = state.secondary_script_running
    elif subop == 0x0022:
        # LuxMoveVM_EvaluateIfOpcode case 0x22 @ 0x1403747E3 reads
        # ALuxBattleChara+0x197C[index*2] as ushort and compares it with a
        # sign-extended authored expected word.  Common packed helper 0x3015
        # writes state[0]=1 via CALLCOND 0x14 in every shipped playable bank;
        # that asset-derived standing baseline is carried by NativeInputState.
        if len(args) < 3:
            return None
        index = _signed(args[1])
        if not 0 <= index < len(state.chara_state_shorts):
            return None
        result = (state.chara_state_shorts[index] & 0xFFFF) == _signed(args[2])
    elif subop in (0x0097, 0x0098):
        # Current/opponent previous MoveVM-state mirrors at +0x1AA4.  The
        # native comparison is the same unsigned-state/signed-expected shape
        # as IF 0x22 (cases 0x140374849/0x140374868).
        if len(args) < 3:
            return None
        index = _signed(args[1])
        values = (
            state.previous_chara_state_shorts
            if subop == 0x0097
            else state.opponent_previous_chara_state_shorts
        )
        if not 0 <= index < len(values):
            return None
        result = (values[index] & 0xFFFF) == _signed(args[2])
    elif subop == 0x000B:
        # Raw byte read from the reset-backed motion-state latch array at
        # +0x16D0 (case 0x140373655).
        if len(args) < 2:
            return None
        index = _signed(args[1])
        if not 0 <= index < len(state.motion_state_latches):
            return None
        return state.motion_state_latches[index] & 0xFF
    elif subop == 0x0015:
        # Jump-table case 0x14037448D compares the sign-extended authored
        # operand with g_nLuxBattleWorldModePumpMasterMode.  Normal versus
        # frame-data analysis uses the native standard battle mode, 2.
        if len(args) < 2 or state.world_mode is None:
            return None
        result = _signed(args[1]) == int(state.world_mode)
    elif subop == 0x001D:
        # Case 0x1403736EA is the opponent mirror of current-character
        # IF 0x000C: return opponent current motion-state flag[index] == 0.
        if len(args) < 2:
            return None
        index = _signed(args[1])
        if not 0 <= index < len(state.opponent_motion_state_latches):
            return None
        result = (state.opponent_motion_state_latches[index] & 0xFF) == 0
    elif subop == 0x0013:
        if (
            state.character_profile_id is None
            or state.opponent_relative_angle_turns is None
        ):
            return None
        try:
            result = bool(evaluate_movement_if(
                MovementHelperState(
                    move_table_index=state.character_profile_id,
                    opponent_relative_angle_turns=state.opponent_relative_angle_turns,
                ),
                args,
            ))
        except StaticResolutionError:
            # Unsupported movement-helper variants remain abstract instead of
            # receiving a guessed context value.
            return None
    elif subop == 0x0042:
        # Native case 0x42 wraps both degree operands and
        # ALuxBattleChara::flOpponentFacingDelta into signed half-turn space,
        # then applies the same inclusive wrapped interval test as IF 0x13.
        if len(args) < 3 or state.opponent_facing_delta_turns is None:
            return None
        try:
            result = bool(evaluate_movement_if(
                MovementHelperState(
                    move_table_index=state.character_profile_id or 0,
                    opponent_relative_angle_turns=state.opponent_facing_delta_turns,
                ),
                (0x0013, args[1], args[2]),
            ))
        except StaticResolutionError:
            return None
    elif subop == 0x0041:
        # Jump-table entry 0x14037666C reaches 0x1403737C4.  Native code
        # returns true when either lane+0xB4 (deferred) or lane+0x5A
        # (immediate) is not the inactive signed-word sentinel -1.  It is a
        # pending-transition predicate, not an input/8-way-run predicate.
        if (
            state.immediate_transition_target is None
            or state.deferred_transition_target is None
        ):
            return None
        result = (
            (state.deferred_transition_target & 0xFFFF) != 0xFFFF
            or (state.immediate_transition_target & 0xFFFF) != 0xFFFF
        )
    elif subop == 0x0066:
        if len(args) < 2 or state.per_frame_motion_flags is None:
            return None
        index = _signed(args[1])
        if not 0 <= index < len(state.per_frame_motion_flags):
            return None
        result = (state.per_frame_motion_flags[index] & 0xFF) == 1
    else:
        return None
    return None if result is None else int(result)


@dataclass(frozen=True)
class _State:
    pc: int
    acc: Val = TOP
    stack: tuple[Val, ...] = ()
    globals: tuple[tuple[int, Val], ...] = ()
    locals: tuple[Val, ...] = (TOP,) * 16
    frame_vars: tuple[tuple[int, Val], ...] = ()


@dataclass(frozen=True)
class _Result:
    returns: Val
    globals: tuple[tuple[int, Val], ...]
    known_returns: frozenset[int] = frozenset()
    selected_slots: frozenset[int] = frozenset()
    selected_by_helper: tuple[tuple[int, frozenset[int]], ...] = ()
    truncated: bool = False


@dataclass(frozen=True)
class NativeLiveTransitionTrace:
    publications: tuple[tuple[int, tuple[int, ...]], ...]
    truncated: bool
    resolutions: tuple[str, ...]


def _merge_maps(
    left: tuple[tuple[int, Val], ...],
    right: tuple[tuple[int, Val], ...],
) -> tuple[tuple[int, Val], ...]:
    a, b = dict(left), dict(right)
    # A missing key is TOP, so only values concrete on both paths survive.
    return tuple(
        (key, _union(a[key], b[key]))
        for key in sorted(a.keys() & b.keys())
        if _union(a[key], b[key]) is not None
    )


def _merge_state(left: _State, right: _State) -> _State:
    if len(left.stack) == len(right.stack):
        stack = tuple(_union(a, b) for a, b in zip(left.stack, right.stack))
    else:
        stack = (TOP,) * min(len(left.stack), len(right.stack))
    return _State(
        pc=left.pc,
        acc=_union(left.acc, right.acc),
        stack=stack,
        globals=_merge_maps(left.globals, right.globals),
        locals=tuple(_union(a, b) for a, b in zip(left.locals, right.locals)),
        frame_vars=_merge_maps(left.frame_vars, right.frame_vars),
    )


def _read_var(state: _State, varid: int) -> Val:
    if varid < 0xF0:
        return dict(state.globals).get(varid, TOP)
    if varid < 0x100:
        return state.locals[varid - 0xF0]
    return dict(state.frame_vars).get(varid, TOP)


def _write_var(state: _State, varid: int, value: Val) -> _State:
    if varid < 0xF0:
        values = dict(state.globals)
        if value is None:
            values.pop(varid, None)
        else:
            values[varid] = value
        return _State(state.pc, state.acc, state.stack, tuple(sorted(values.items())), state.locals, state.frame_vars)
    if varid < 0x100:
        values = list(state.locals)
        values[varid - 0xF0] = value
        return _State(state.pc, state.acc, state.stack, state.globals, tuple(values), state.frame_vars)
    values = dict(state.frame_vars)
    if value is None:
        values.pop(varid, None)
    else:
        values[varid] = value
    return _State(state.pc, state.acc, state.stack, state.globals, state.locals, tuple(sorted(values.items())))


def _pop(stack: list[Val]) -> Val:
    return stack.pop() if stack else TOP


class NativeDispatcherResolver:
    """Resolve CPUAI definitions through one character's common KHD dispatcher."""

    def __init__(
        self,
        bank: object,
        command_table: TransitionCommandTable,
        codec_tables: LuxInputCodecTables,
        *,
        character_profile_id: int | None = None,
        max_states: int = 50_000,
        max_depth: int = 12,
    ) -> None:
        self.bank = bank
        self.command_table = command_table
        self.codec_tables = codec_tables
        # ALuxBattleChara+0x24E is initialized from the character-profile
        # record and consumed by IF 0x13C5.  The export's native asset id is
        # the matching profile id; callers without that join leave it unknown.
        self.character_profile_id = character_profile_id
        self.max_states = max_states
        self.max_depth = max_depth
        self._definition_cache: dict[tuple[int, int | None], NativeRouteCandidates] = {}
        self._attack_route_cache: dict[
            tuple[int, int, int | None, tuple[tuple[int, int], ...]], NativeResolvedRoute
        ] = {}
        # Per-top-level dispatcher walk. Common helpers are invoked repeatedly
        # with identical globals/locals along reconverging branches; their
        # abstract result is deterministic for one immutable input snapshot.
        self._slot_run_cache: dict[tuple[object, ...], _Result] = {}

    def resolve_definition(
        self,
        definition: MovePlayDefinition,
        *,
        chara_state0: int | None = None,
    ) -> "NativeRouteCandidates":
        cache_key = (definition.definition_id, chara_state0)
        cached = self._definition_cache.get(cache_key)
        if cached is not None:
            return cached
        input_states = build_definition_input_states(definition, self.codec_tables)
        if not input_states:
            result = NativeRouteCandidates((), True, ("cpuai-definition-not-linear",))
            self._definition_cache[cache_key] = result
            return result
        root = self.bank.resolve_packed_slot(0x3048)
        if root is None:
            result = NativeRouteCandidates((), True, ("khd-common-dispatcher-0x3048-unresolved",))
            self._definition_cache[cache_key] = result
            return result
        candidate_values: set[int] = set()
        publication_sequence: list[tuple[int, tuple[int, ...]]] = []
        active_move_id: int | None = None
        # LuxMoveVM_InitSlotParamTables clears both 240-short per-player
        # global banks before MoveVM use.  The bank is persistent across
        # subsequent script calls, so carry each dispatcher's terminal state
        # through the complete authored CPUAI input program instead of
        # treating every publication as a fresh bank of unknown values.
        globals_state: tuple[tuple[int, Val], ...] = tuple(
            (index, ZERO) for index in range(0xF0)
        )
        selected_helper: int | None = None
        selected_tick: int | None = None
        selected_move_play_frame: int | None = None
        truncated = False
        for input_state_without_commands in input_states:
            chara_state_shorts = input_state_without_commands.chara_state_shorts
            if chara_state0 is not None:
                chara_state_shorts = (
                    int(chara_state0),
                    *chara_state_shorts[1:],
                )
            input_state = NativeInputState(
                input_state_without_commands.snapshot,
                input_state_without_commands.history,
                self.command_table,
                input_state_without_commands.command_age_limit,
                chara_state_shorts,
                input_state_without_commands.previous_chara_state_shorts,
                input_state_without_commands.opponent_previous_chara_state_shorts,
                input_state_without_commands.motion_state_latches,
                input_state_without_commands.move_play_frame,
                active_move_id,
                character_profile_id=self.character_profile_id,
            )
            self._slot_run_cache.clear()
            result = self._run_slot(root, input_state, globals_state, (), 0, ())
            globals_state = result.globals
            truncated = truncated or result.truncated
            # Direction-only and neutral ticks are still required for global
            # bank dataflow, but their selector return is not an attack-slot
            # publication.
            if not (input_state.snapshot.current_compact_word & 0x2F):
                continue
            by_helper = dict(result.selected_by_helper)
            # Packed 0x304E is the idle/standing input selector reached
            # after the common 0x3015 baseline writes MoveVM state[0]=1.  The
            # first accepted target replaces idle slot 0 in lane 0. Later BTN
            # samples are consumed by the selected slot's own transition graph.
            standing = {
                value for value in by_helper.get(0x304E, ())
                if value not in (0, 0xFFFF)
            }
            if standing:
                candidate_values = set(standing)
                publication_sequence.append((
                    input_state.move_play_frame,
                    tuple(sorted(standing)),
                ))
                active_move_id = (
                    next(iter(standing)) if len(standing) == 1 else None
                )
                selected_helper = 0x304E
                selected_tick = input_state.history.tick_count
                selected_move_play_frame = input_state.move_play_frame
                # Packed 0x300B is called by the idle/selection slot with
                # LOCAL1=0, which takes CALLCOND 0x06 and commits this target
                # into lane 0.  The target slot replaces the idle dispatcher;
                # later BTN samples belong to that slot's own transition
                # graph and must not be fed through 0x3048/0x304E again.
                break
            # Fail-open diagnostic fallback for rows whose authored setup does
            # not reach the standing selector.  Keep the first helper result
            # as ambiguous native evidence; do not silently call it standing.
            fallback = {
                value for helper, values in result.selected_by_helper
                if helper != 0x3051
                for value in values
                if value not in (0, 0xFFFF)
            }
            if fallback:
                candidate_values = set(fallback)
                publication_sequence.append((
                    input_state.move_play_frame,
                    tuple(sorted(fallback)),
                ))
                active_move_id = (
                    next(iter(fallback)) if len(fallback) == 1 else None
                )
                selected_helper = -1
                selected_tick = input_state.history.tick_count
                selected_move_play_frame = input_state.move_play_frame
        candidates = tuple(sorted(
            value for value in candidate_values
            if 0 <= value < len(self.bank.slots)
        ))
        last_input_state = input_states[-1]
        resolved = NativeRouteCandidates(
            candidates,
            truncated,
            (
                f"cpuai-definition:{definition.definition_id}@cell{definition.initial_cell}",
                f"native-input-observations:{len(input_states)};"
                f"ticks={last_input_state.history.tick_count};"
                f"age-assumption<={last_input_state.command_age_limit}",
                f"khd-common-input-dispatch:packed0x3048->slot{root}",
                "khd-input-publications:"
                + (
                    ",".join(
                        f"frame{frame}->" + "/".join(f"slot{slot}" for slot in slots)
                        for frame, slots in publication_sequence
                    )
                    if publication_sequence else "none"
                ),
                (
                    f"khd-selector:packed0x{selected_helper:X};tick={selected_tick}"
                    if selected_helper is not None and selected_helper >= 0
                    else f"khd-selector:context-dependent;tick={selected_tick}"
                ),
            ),
            selected_move_play_frame,
            globals_state,
            tuple(publication_sequence),
        )
        self._definition_cache[cache_key] = resolved
        return resolved

    def resolve_live_transition_publications(
        self,
        definition: MovePlayDefinition,
        *,
        chara_state0: int | None = None,
        meter_state_shorts: dict[int, int] | None = None,
        special_lethal_match_mode: int | None = None,
    ) -> NativeLiveTransitionTrace:
        """Execute move-0 selection until its first committed lane-0 target.

        Packed 0x3048 is the observation/bookkeeping pass reached from
        0x300A.  Packed 0x3049 is the transition-selection pass reached later
        from 0x300C; it clears and publishes globals 0x44/0x46/0x47, which
        0x300B subsequently supplies to the transition author.  With idle
        slot 0's actual local arguments ``[0, 0]``, the helper reaches
        CALLCOND 0x06 and authors lane 0.  Once packed 0x300B authors a target,
        replaces lane 0 with that target.  Subsequent input must be evaluated
        by the selected slot's transition graph, not by replaying move 0.
        """

        observations = build_definition_input_timeline(definition, self.codec_tables)
        observation_root = self.bank.resolve_packed_slot(0x3048)
        transition_root = self.bank.resolve_packed_slot(0x3049)
        if not observations or observation_root is None or transition_root is None:
            return NativeLiveTransitionTrace(
                (),
                True,
                ("khd-live-transition-pipeline-unresolved",),
            )
        globals_state: tuple[tuple[int, Val], ...] = tuple(
            (index, ZERO) for index in range(0xF0)
        )
        active_move_id: int | None = None
        publications: list[tuple[int, tuple[int, ...]]] = []
        truncated = False
        for observation in observations:
            chara_state_shorts = observation.chara_state_shorts
            if chara_state0 is not None:
                chara_state_shorts = (int(chara_state0), *chara_state_shorts[1:])
            meter_values = list(observation.meter_state_shorts)
            for index, value in (meter_state_shorts or {}).items():
                if 0 <= int(index) < len(meter_values):
                    meter_values[int(index)] = int(value)
            input_state = replace(
                observation,
                command_table=self.command_table,
                chara_state_shorts=chara_state_shorts,
                meter_state_shorts=tuple(meter_values),
                special_lethal_match_mode=special_lethal_match_mode,
                active_move_id=active_move_id,
                character_profile_id=self.character_profile_id,
            )
            self._slot_run_cache.clear()
            observed = self._run_slot(
                observation_root, input_state, globals_state, (), 0, ()
            )
            globals_state = observed.globals
            self._slot_run_cache.clear()
            selected = self._run_slot(
                transition_root,
                input_state,
                globals_state,
                (ZERO, ZERO, ZERO),
                0,
                (),
            )
            globals_state = selected.globals
            truncated = truncated or observed.truncated or selected.truncated
            targets = tuple(sorted(
                value
                for value in dict(globals_state).get(0x44, ())
                if value not in (0, 0xFFFF, 0xFFFD)
            ))
            if targets:
                publications.append((observation.move_play_frame, targets))
                break
        return NativeLiveTransitionTrace(
            tuple(publications),
            truncated,
            (
                f"khd-observation:packed0x3048->slot{observation_root}",
                f"khd-live-selection:packed0x3049->slot{transition_root}",
                "khd-transition-author:packed0x300B;globals44/46/47;lane0",
            ),
        )

    def resolve_attack_route(
        self,
        definition: MovePlayDefinition,
        start_slot: int,
        *,
        selected_move_play_frame: int | None = None,
        meter_state_shorts: dict[int, int] | None = None,
    ) -> "NativeResolvedRoute":
        """Resolve and cache the in-move route for one CPUAI definition."""

        # MoveVM meter/state slot ten is zero in the ordinary non-Soul-Charge
        # baseline.  Callers explicitly override it for SC-tagged rows. Slot
        # zero remains unknown unless an authored gauge requirement supplies
        # its value.
        effective_meter_state = {10: 0}
        effective_meter_state.update(meter_state_shorts or {})
        meter_state_key = tuple(sorted(effective_meter_state.items()))
        key = (
            definition.definition_id,
            start_slot,
            selected_move_play_frame,
            meter_state_key,
        )
        cached = self._attack_route_cache.get(key)
        if cached is None:
            start_evidence = self.resolve_definition(definition)
            concrete_globals = {
                varid: value
                for varid, values in start_evidence.terminal_globals
                if (value := _singleton(values)) is not None
            }
            cached = resolve_input_timed_attack_route(
                self.bank,
                start_slot,
                definition,
                self.command_table,
                self.codec_tables,
                selected_move_play_frame=selected_move_play_frame,
                initial_variables=concrete_globals,
                character_profile_id=self.character_profile_id,
                meter_state_shorts=dict(meter_state_key),
            )
            self._attack_route_cache[key] = cached
        return cached

    def _run_slot(
        self,
        slot: int,
        input_state: NativeInputState,
        globals_in: tuple[tuple[int, Val], ...],
        local_args: tuple[Val, ...],
        depth: int,
        call_path: tuple[int, ...],
    ) -> _Result:
        key = (
            slot,
            id(input_state),
            globals_in,
            local_args,
            depth,
            call_path,
        )
        cached = self._slot_run_cache.get(key)
        if cached is not None:
            return cached
        result = self._run_slot_uncached(
            slot,
            input_state,
            globals_in,
            local_args,
            depth,
            call_path,
        )
        self._slot_run_cache[key] = result
        return result

    def _run_slot_uncached(
        self,
        slot: int,
        input_state: NativeInputState,
        globals_in: tuple[tuple[int, Val], ...],
        local_args: tuple[Val, ...],
        depth: int,
        call_path: tuple[int, ...],
    ) -> _Result:
        if depth > self.max_depth or slot in call_path or not (0 <= slot < len(self.bank.slots)):
            return _Result(TOP, globals_in, frozenset(), frozenset(), (), True)
        script = self.bank.slots[slot].bytecode
        if script is None:
            return _Result(ZERO, globals_in, frozenset((0,)), frozenset(), (), False)
        locals_values = list(local_args[:16])
        locals_values.extend(ZERO for _ in range(16 - len(locals_values)))
        initial = _State(
            pc=script.bytecode_offset,
            globals=globals_in,
            locals=tuple(locals_values),
        )
        by_pc = {instruction.pc: instruction for instruction in script.instructions}
        static_nested_targets = {
            event.source_pc: event.packed_move_id
            for event in emulate(script, slot).bank_scripts
            if event.packed_move_id is not None
        }
        # Keep distinct abstract states instead of joining everything merely
        # because it reaches the same program counter.  Dispatcher scripts
        # repeatedly branch on one selector and then reconverge; a PC-only
        # join destroys that selector correlation and invents mutually
        # exclusive move slots.  Exact-state deduplication still collapses
        # genuinely identical paths and the global state budget bounds the
        # remaining battle-context fan-out.
        seen: set[_State] = {initial}
        queue = deque((initial,))
        returns: Val = frozenset()
        known_returns: set[int] = set()
        selected_slots: set[int] = set()
        selected_by_helper: dict[int, set[int]] = {}
        exit_globals: tuple[tuple[int, Val], ...] | None = None
        processed = 0
        truncated = False

        def enqueue(incoming: _State) -> None:
            if incoming not in seen:
                seen.add(incoming)
                queue.append(incoming)

        def record_return(value: Val, globals_out: tuple[tuple[int, Val], ...]) -> None:
            nonlocal returns, exit_globals, truncated
            returns = _union(returns, value)
            if value is not None:
                known_returns.update(value)
            else:
                # A reachable terminal with an unresolved return prevents a
                # supposedly unique selector result from being called exact.
                truncated = True
            exit_globals = globals_out if exit_globals is None else _merge_maps(exit_globals, globals_out)

        while queue:
            processed += 1
            if processed > self.max_states:
                truncated = True
                break
            state = queue.popleft()
            instruction = by_pc.get(state.pc)
            if instruction is None:
                truncated = True
                continue
            op = instruction.opcode
            acc = state.acc
            stack = list(state.stack)
            current = state
            explicit_successors: tuple[int, ...] | None = None

            if op == 0x01:
                if instruction.imm_u16:
                    stack.extend(TOP for _ in range(instruction.imm_u16 + 1))
            elif op in (0x03, 0x04, 0x09, 0x0B, 0x2A):
                acc = _one(instruction.imm_u16 or 0)
            elif op == 0x0A:
                acc = _read_var(current, instruction.imm_u16 or 0)
            elif op in (0x12, 0x13):
                varid = instruction.imm_u16 or 0
                acc = _read_var(current, varid)
                delta = _one(1 if op == 0x12 else 0xFFFF)
                current = _write_var(current, varid, _binary(0x0C, acc, delta))
            elif op == 0x19:
                acc = _pop(stack)
                varid = instruction.imm_u16 or 0
                current = _write_var(current, varid, acc)
            elif op in (0x1A, 0x1B, 0x1C, 0x1D, 0x1E):
                varid = instruction.imm_u16 or 0
                rhs = _pop(stack)
                lhs = _read_var(current, varid)
                current = _write_var(current, varid, _binary(op - 0x0E, lhs, rhs))
                acc = TOP
            elif op == 0x26:
                stack.append(acc)
            elif op == 0x27:
                acc = _pop(stack)
            elif op in (0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x14, 0x15, 0x17, 0x18,
                        0x1F, 0x20, 0x21, 0x22, 0x23, 0x24):
                right, left = _pop(stack), _pop(stack)
                acc = _binary(op, left, right)
            elif op == 0x11:
                value = _pop(stack)
                acc = None if value is None else frozenset((-_signed(item)) & 0xFFFF for item in value)
            elif op == 0x16:
                value = _pop(stack)
                acc = BOOL if value is None else frozenset(int(item == 0) for item in value)
            elif op == 0x25:
                function_index = instruction.imm_b0 or 0
                count = instruction.imm_b1 or 0
                args = tuple(reversed([_pop(stack) for _ in range(count)]))
                if function_index in (0x00, 0x01, 0x25):
                    concrete = tuple(_singleton(value) for value in args)
                    if all(value is not None for value in concrete):
                        predicate_words = tuple(int(value) for value in concrete)
                        if function_index == 0x25:
                            predicate_words = (0x0008,) + predicate_words
                        evaluated = evaluate_input_if(
                            input_state,
                            predicate_words,
                        )
                    else:
                        evaluated = None
                    acc = _one(evaluated) if evaluated is not None else BOOL
                elif function_index in (0x05, 0x06, 0x07, 0x08):
                    # TransitionAuthor's first argument is the raw local
                    # target move id.  One argument is a complete immediate
                    # transition (start/threshold both default to zero), as
                    # proven by LuxMoveVM_DecodeVariadicStreamArgs
                    # @ 0x1402FC930.  Preserve every reachable concrete
                    # target from direct and recursively executed helpers.
                    targets = args[0] if args else ZERO
                    if targets is None:
                        truncated = True
                    else:
                        selected_slots.update(
                            value for value in targets
                            if value not in (0xFFFF, 0xFFFD)
                        )
                    acc = ZERO
                elif function_index == 0x0D:
                    targets = args[0] if args else TOP
                    if targets is None and instruction.pc in static_nested_targets:
                        targets = _one(static_nested_targets[instruction.pc])
                    nested_returns: Val = frozenset()
                    nested_selected: set[int] = set()
                    nested_globals: tuple[tuple[int, Val], ...] | None = None
                    if targets is None:
                        acc = TOP
                        truncated = True
                    else:
                        for packed in targets:
                            child_slot = self.bank.resolve_packed_slot(packed)
                            if child_slot is None:
                                nested_returns = _union(nested_returns, TOP)
                                truncated = True
                                continue
                            child = self._run_slot(
                                child_slot,
                                input_state,
                                current.globals,
                                args[1:],
                                depth + 1,
                                call_path + (slot,),
                            )
                            nested_returns = _union(nested_returns, child.returns)
                            nested_selected.update(child.selected_slots)
                            for helper, helper_slots in child.selected_by_helper:
                                selected_by_helper.setdefault(helper, set()).update(helper_slots)
                            # These packed common helpers are the stance/input
                            # selectors whose return value is a local KHD move
                            # slot.  The 0x3048 coordinator itself returns only
                            # a status word, so observing these nested returns
                            # is the native dataflow join we need.
                            if packed in (0x304E, 0x3050, 0x3051, 0x3055, 0x3265):
                                nested_selected.update(child.known_returns)
                                selected_by_helper.setdefault(packed, set()).update(child.known_returns)
                            nested_globals = (
                                child.globals if nested_globals is None
                                else _merge_maps(nested_globals, child.globals)
                            )
                            truncated = truncated or child.truncated
                        acc = nested_returns
                        selected_slots.update(nested_selected)
                        if nested_globals is not None:
                            current = _State(current.pc, current.acc, current.stack, nested_globals, current.locals, current.frame_vars)
                elif function_index in (0x0B, 0x12, 0x13, 0x24):
                    acc = ZERO
                else:
                    acc = TOP
            elif op in (0x28, 0x29):
                condition = _pop(stack)
                target = script.bytecode_offset + (instruction.imm_u16 or 0)
                fallthrough = instruction.pc + instruction.length
                if condition is None:
                    explicit_successors = (fallthrough, target)
                else:
                    has_zero = 0 in condition
                    has_nonzero = any(value != 0 for value in condition)
                    successors: list[int] = []
                    if op == 0x28:
                        if has_nonzero:
                            successors.append(fallthrough)
                        if has_zero:
                            successors.append(target)
                    else:
                        if has_zero:
                            successors.append(fallthrough)
                        if has_nonzero:
                            successors.append(target)
                    explicit_successors = tuple(dict.fromkeys(successors))
            elif op in (0x02, 0x06):
                record_return(ZERO, current.globals)
                continue
            elif op in (0x05, 0x07):
                record_return(_pop(stack), current.globals)
                continue
            elif op == 0x08:
                record_return(ZERO, current.globals)
                continue

            if instruction.push_flag and op not in (0x02, 0x05, 0x06, 0x07, 0x08):
                stack.append(acc)
            next_state = _State(
                current.pc,
                acc,
                tuple(stack),
                current.globals,
                current.locals,
                current.frame_vars,
            )
            if explicit_successors is not None:
                successors = explicit_successors
            elif op in (0x03, 0x04, 0x2A):
                successors = (script.bytecode_offset + (instruction.imm_u16 or 0),)
            else:
                successors = (instruction.pc + instruction.length,)
            for successor in successors:
                enqueue(_State(
                    successor,
                    next_state.acc,
                    next_state.stack,
                    next_state.globals,
                    next_state.locals,
                    next_state.frame_vars,
                ))

        return _Result(
            returns if returns != frozenset() else TOP,
            exit_globals if exit_globals is not None else globals_in,
            frozenset(known_returns),
            frozenset(selected_slots),
            tuple(
                (helper, frozenset(slots))
                for helper, slots in sorted(selected_by_helper.items())
            ),
            truncated,
        )


@dataclass(frozen=True)
class NativeRouteCandidates:
    slots: tuple[int, ...]
    truncated: bool
    resolutions: tuple[str, ...]
    selected_move_play_frame: int | None = None
    terminal_globals: tuple[tuple[int, Val], ...] = ()
    # The first standing-selector publication committed by the authored
    # CPUAI definition. It is navigation/input evidence, never a contact;
    # later BTN steps belong to the selected combat lane's transition graph.
    publications: tuple[tuple[int, tuple[int, ...]], ...] = ()


@dataclass(frozen=True)
class NativeResolvedRoute:
    slots: tuple[int, ...]
    cells: tuple[int, ...]
    ambiguous: bool
    resolutions: tuple[str, ...]
    # Slots/cells which actually contribute contact metrics.  Replacement
    # transitions retain their source in ``slots``/``cells`` as navigation
    # evidence without fabricating an extra hit or damage entry.
    attack_slots: tuple[int, ...] = ()
    attack_cells: tuple[int, ...] = ()
    startup_impact_coordinate: int | None = None
    startup_player_frame: int | None = None
    startup_timing_resolved: bool = True
    frame_endpoints_resolved: bool = True
    unresolved_frame_outcomes: tuple[str, ...] = ()

    def frame_endpoint_resolved(self, outcome: str) -> bool:
        return outcome not in self.unresolved_frame_outcomes


def resolve_publication_entry_route(
    resolver: NativeDispatcherResolver,
    definition: MovePlayDefinition,
    candidates: NativeRouteCandidates,
    authored_hit_count: int,
    *,
    meter_state_shorts: dict[int, int] | None = None,
) -> NativeResolvedRoute | None:
    """Resolve contacts owned by the first committed combat lane.

    The MovePlay pump publishes inputs; its first accepted standing target
    replaces idle lane zero. Later BTN steps are consumed by that combat
    lane's own transition graph and must never be replayed as independent
    standing publications.
    """

    if authored_hit_count <= 0:
        return None
    if len(candidates.publications) != 1:
        return None
    publication_frame, published_slots = candidates.publications[0]
    if len(published_slots) != 1:
        return None
    start_slot = int(published_slots[0])
    route = resolver.resolve_attack_route(
        definition,
        start_slot,
        selected_move_play_frame=publication_frame,
        meter_state_shorts=meter_state_shorts,
    )
    if route.ambiguous or len(route.attack_cells) != authored_hit_count:
        return None
    return replace(
        route,
        resolutions=tuple(sorted({
            *route.resolutions,
            f"khd-route-entry-publication:frame{publication_frame}->slot{start_slot};"
            f"game-authored-contacts={authored_hit_count}",
        })),
    )


def resolve_unconditional_attack_route(
    bank: object,
    start_slot: int,
    *,
    max_slots: int = 12,
) -> NativeResolvedRoute:
    """Follow only game-authored unconditional attack continuations.

    This deliberately does not guess frame-, stance-, or input-gated edges.
    Default variant zero is used only when it directly names an attack cell;
    alternate cell variants require their own native selector proof.
    """
    slots: list[int] = []
    cells: list[int] = []
    resolutions: list[str] = []
    seen: set[int] = set()
    current = start_slot
    ambiguous = False
    while len(slots) < max_slots and 0 <= current < len(bank.slots):
        if current in seen:
            ambiguous = True
            resolutions.append(f"khd-unconditional-cycle:slot{current}")
            break
        seen.add(current)
        slot = bank.slots[current]
        variant_zero = _slot_default_cell(bank, current)
        has_attack_cell = variant_zero is not None
        if cells and not has_attack_cell:
            # An automatic recovery/idle destination is not part of the
            # player's attack route.
            break
        slots.append(current)
        if has_attack_cell:
            cells.append(int(variant_zero))
            resolutions.append(
                f"khd-default-cell-variant:slot{current}[0]->cell{variant_zero}"
            )
        if slot.bytecode is None:
            break
        unconditional = []
        for transition in emulate(slot.bytecode, current).transitions:
            if transition.predicate is not None or transition.next_move_id_raw is None:
                continue
            target = bank.resolve_packed_slot(transition.next_move_id_raw)
            if target is not None:
                unconditional.append((target, transition.source_pc))
        unique_targets = sorted({target for target, _ in unconditional})
        if not unique_targets:
            break
        if len(unique_targets) != 1:
            ambiguous = True
            resolutions.append(
                f"khd-unconditional-branch:slot{current}->"
                + ",".join(f"slot{target}" for target in unique_targets)
            )
            break
        target = unique_targets[0]
        source_pc = next(pc for candidate, pc in unconditional if candidate == target)
        resolutions.append(
            f"khd-unconditional-followup:slot{current}@0x{source_pc:X}->slot{target}"
        )
        current = target
    else:
        if len(slots) >= max_slots:
            ambiguous = True
            resolutions.append(f"khd-route-limit:{max_slots}")
    attack_slots = tuple(
        slot_index
        for slot_index in slots
        if _slot_default_cell(bank, slot_index) is not None
    )
    startup_coordinate: int | None = None
    if cells and getattr(bank, "sections", ()):
        attack_cells = bank.sections[0].entries
        if 0 <= cells[0] < len(attack_cells):
            startup_coordinate = int(attack_cells[cells[0]].wI16MasterWindowStart)
    return NativeResolvedRoute(
        tuple(slots),
        tuple(cells),
        ambiguous,
        tuple(resolutions),
        attack_slots=attack_slots,
        attack_cells=tuple(cells),
        startup_impact_coordinate=startup_coordinate,
        startup_player_frame=(
            startup_coordinate + 1 if startup_coordinate is not None else None
        ),
    )


_OFFLINE_INPUT_TRANSITION_SUBOPS = frozenset({
    0x0001,
    0x0003,
    0x0005,
    0x0006,
    0x0008,
    0x0009,
    0x0010,
    0x0020,
    0x0024,
    0x002B,
    0x002C,
    0x0084,
    0x0054,
    0x13AE,
    0x13AF,
})


def _transition_reachable_with_static_predicates(
    script: object,
    slot_index: int,
    state: NativeInputState,
    animation_frame: int,
    transition: object,
    cache: dict[
        tuple[int, int, tuple[tuple[int, int | None], ...]],
        tuple[object, frozenset[int]],
    ] | None = None,
) -> bool:
    """Evaluate the transition's complete reachable CFG, not its last IF.

    ``stackvm_emulate`` retains a nearest-predicate annotation for display,
    but authored scripts routinely lower conjunctions into several JZ blocks.
    Treating only the last IF as the gate loses earlier button/direction tests
    (Astaroth slot 269 is a concrete A+B/A+G/A+B+K example).  Supplying every
    statically supported predicate to the emulator preserves those complete
    branch conditions while unknown battle-state predicates still fork.
    """

    def evaluate(fn_idx: int, values: tuple[object, ...]) -> int | None:
        if fn_idx not in (0x00, 0x01, 0x25):
            return None
        words = _event_words(values)
        if words is None:
            return None
        if fn_idx == 0x25:
            words = (0x0008,) + words
        return evaluate_input_if(
            state,
            words,
            animation_frame=animation_frame,
        )

    # Reachability depends on the truth values observed at authored predicate
    # call sites, not on the identity of the large 320-entry input-history
    # snapshot. Many timeline coordinates collapse to the same truth vector.
    # Cache that semantic vector so the CFG fixed point runs once per distinct
    # branch outcome instead of once per transition candidate.
    static = emulate(script, slot_index)
    predicate_signature = tuple(
        (
            int(event.source_pc),
            evaluate(int(event.callcond_idx), tuple(event.args)),
        )
        for event in sorted(static.predicates, key=lambda item: item.source_pc)
    )
    cache_key = (id(script), slot_index, predicate_signature)
    cached = cache.get(cache_key) if cache is not None else None
    if cached is not None and cached[0] is script:
        visited_pcs = cached[1]
    else:
        reachable = emulate(
            script,
            slot_index,
            callcond_evaluator=evaluate,
        )
        visited_pcs = reachable.visited_pcs
        if cache is not None:
            # Retain the identity-keyed script so a recycled id cannot alias
            # another parsed bank during a long export.
            cache[cache_key] = (script, visited_pcs)
    return int(transition.source_pc) in visited_pcs


def _event_words(values: object) -> tuple[int, ...] | None:
    words: list[int] = []
    for value in values:
        as_int = getattr(value, "as_int", None)
        word = as_int() if callable(as_int) else None
        if word is None:
            return None
        words.append(int(word) & 0xFFFF)
    return tuple(words)


@dataclass(frozen=True)
class _NestedRouteTransition:
    """A TransitionAuthor reached through nested bank-script calls."""

    owner_slot: int
    call_path: tuple[int, ...]
    transition: object
    static_predicates_resolved: bool


_NESTED_TRANSITION_CAPABILITY_CACHE: dict[
    int, tuple[object, dict[int, bool]]
] = {}


def _slot_can_author_nested_transition(
    bank: object,
    slot_index: int,
    visiting: set[int] | None = None,
) -> bool:
    """Whether a slot/helper's synchronous 0x0D closure can author a transition."""

    object_id = id(bank)
    cached_owner = _NESTED_TRANSITION_CAPABILITY_CACHE.get(object_id)
    if cached_owner is None or cached_owner[0] is not bank:
        cached_owner = (bank, {})
        _NESTED_TRANSITION_CAPABILITY_CACHE[object_id] = cached_owner
    cache = cached_owner[1]
    if slot_index in cache:
        return cache[slot_index]
    if not 0 <= slot_index < len(bank.slots):
        cache[slot_index] = False
        return False
    visiting = set() if visiting is None else visiting
    if slot_index in visiting:
        return False
    visiting.add(slot_index)
    script = bank.slots[slot_index].bytecode
    if script is None:
        result = False
    else:
        extracted = emulate(script, slot_index)
        result = bool(extracted.transitions)
        if not result:
            for call in extracted.bank_scripts:
                if call.callcond_idx != 0x0D or call.packed_move_id is None:
                    continue
                child = bank.resolve_packed_slot(call.packed_move_id)
                if child is not None and _slot_can_author_nested_transition(
                    bank, child, visiting
                ):
                    result = True
                    break
    visiting.remove(slot_index)
    cache[slot_index] = result
    return result


def _route_callcond_evaluator(
    function_index: int,
    args: tuple[StackVal, ...],
    *,
    input_state: NativeInputState,
    animation_coordinate: int,
    lane_start_coordinate: int,
    lane_total_frames: int,
    motion_start_frame: int,
    motion_end_frame: int,
    playback_speed: float,
    character_profile_id: int | None,
    meter_state_shorts: dict[int, int],
) -> int | None:
    """Evaluate only native predicates with a complete static route state."""

    words = _event_words(args)
    if words is None:
        return None
    if function_index == 0x25:
        return evaluate_timing(
            LaneTimingState(
                current_frame=float(animation_coordinate),
                frame_delta=1,
                timing_frame_10=float(lane_total_frames),
                timing_frame_14=float(lane_start_coordinate),
                motion_start_frame=float(motion_start_frame),
                motion_end_frame=float(motion_end_frame),
                playback_speed=float(playback_speed),
            ),
            words,
        )
    if function_index not in (0x00, 0x01):
        # TransitionAuthor wrappers return zero.  Other CALLCOND return
        # contracts remain unresolved here; branch both ways if consumed.
        return 0 if function_index in (0x05, 0x06, 0x07, 0x08) else None
    if not words:
        return None
    subopcode = words[0]
    if subopcode in (0x0009, 0x0010, 0x0054):
        return evaluate_input_if(
            input_state,
            words,
            animation_frame=animation_coordinate,
        )
    if subopcode == 0x13C5:
        if character_profile_id is None or len(words) < 2:
            return None
        return int(_signed(words[1]) == int(character_profile_id))
    if subopcode == 0x138A:
        if len(words) < 4:
            return None
        selector = _signed(words[1])
        if selector not in meter_state_shorts:
            return None
        value = _signed(meter_state_shorts[selector])
        return int(_signed(words[2]) <= value <= _signed(words[3]))
    if subopcode == 0x002A:
        if len(words) < 2:
            return None
        # LuxBattleChara_UpdateOpponentRelativeAngles_PerTick publishes four
        # facing sectors at opponent+0x15C0.  Player-facing move data uses the
        # ordinary head-on baseline (native sector zero).  The VM's authored
        # selector remap is 0->0, 1->1, 2->3, 3->2.
        expected_sector = {0: 0, 1: 1, 2: 3, 3: 2}.get(int(words[1]))
        return int(expected_sector == 0) if expected_sector is not None else 0
    if subopcode == 0x0013:
        try:
            return evaluate_movement_if(
                MovementHelperState(
                    move_table_index=character_profile_id or 0,
                    # Player-facing frame data uses the ordinary facing
                    # baseline: opponent straight ahead (zero turns).
                    opponent_relative_angle_turns=0.0,
                ),
                words,
            )
        except StaticResolutionError:
            return None
    return evaluate_input_if(
        input_state,
        words,
        animation_frame=animation_coordinate,
    )


def _nested_route_transitions_at_coordinate(
    bank: object,
    root_slot_index: int,
    input_state: NativeInputState,
    animation_coordinate: int,
    lane_start_coordinate: int,
    *,
    initial_variables: dict[int, int | StackVal] | None,
    character_profile_id: int | None,
    meter_state_shorts: dict[int, int],
    max_depth: int = 12,
) -> tuple[_NestedRouteTransition, ...]:
    """Execute reachable common helpers and retain their transition writes.

    ``LuxMoveVM_ExecuteBankSlotScript @ 0x1402FCC30`` keeps the caller's
    active lane and persistent global bank while replacing only the sixteen
    local words.  Therefore timing predicates inside a common helper belong
    to the owning combat lane, not to the helper's sentinel slot record.
    """

    if not 0 <= root_slot_index < len(bank.slots):
        return ()
    lane_slot = bank.slots[root_slot_index]
    lane_total = int(lane_slot.wTotalFrames)
    found: list[_NestedRouteTransition] = []

    def visit(
        slot_index: int,
        local_args: tuple[StackVal, ...] | None,
        variables: dict[int, int | StackVal],
        call_path: tuple[int, ...],
    ) -> None:
        if (
            len(call_path) > max_depth
            or slot_index in call_path
            or not 0 <= slot_index < len(bank.slots)
        ):
            return
        slot = bank.slots[slot_index]
        if slot.bytecode is None:
            return

        def evaluate(function_index: int, args: tuple[StackVal, ...]) -> int | None:
            return _route_callcond_evaluator(
                function_index,
                args,
                input_state=input_state,
                animation_coordinate=animation_coordinate,
                lane_start_coordinate=lane_start_coordinate,
                lane_total_frames=lane_total,
                motion_start_frame=int(lane_slot.nMotionAStartFrame_02),
                motion_end_frame=int(lane_slot.nMotionAEndFrame_04),
                playback_speed=float(lane_slot.playback_speed_scalar),
                character_profile_id=character_profile_id,
                meter_state_shorts=meter_state_shorts,
            )

        result = emulate(
            slot.bytecode,
            slot_index,
            local_args=local_args,
            initial_variables=variables,
            callcond_evaluator=evaluate,
        )
        next_path = call_path + (slot_index,)
        if call_path:
            for transition in result.transitions:
                predicate = transition.predicate
                predicate_resolved = (
                    predicate is None
                    or evaluate(predicate.callcond_idx, tuple(predicate.args)) is not None
                )
                found.append(_NestedRouteTransition(
                    slot_index,
                    next_path,
                    transition,
                    predicate_resolved,
                ))
        for call in result.bank_scripts:
            if call.callcond_idx != 0x0D or call.packed_move_id is None:
                continue
            child_slot = bank.resolve_packed_slot(call.packed_move_id)
            if (
                child_slot is None
                or not _slot_can_author_nested_transition(bank, child_slot)
            ):
                continue
            visit(
                child_slot,
                tuple(call.args[1:]),
                dict(call.variables),
                next_path,
            )

    visit(root_slot_index, None, dict(initial_variables or {}), ())
    return tuple(found)


def _nested_route_timing_coordinates(
    bank: object,
    root_slot_index: int,
    lane_start_coordinate: int,
    *,
    max_depth: int = 12,
) -> frozenset[int]:
    """Return finite lane coordinates at which nested helpers can branch."""

    if not 0 <= root_slot_index < len(bank.slots):
        return frozenset()
    lane_slot = bank.slots[root_slot_index]
    timing = LaneTimingState(
        current_frame=0.0,
        frame_delta=1,
        timing_frame_10=float(int(lane_slot.wTotalFrames)),
        timing_frame_14=float(lane_start_coordinate),
        motion_start_frame=float(int(lane_slot.nMotionAStartFrame_02)),
        motion_end_frame=float(int(lane_slot.nMotionAEndFrame_04)),
        playback_speed=float(lane_slot.playback_speed_scalar),
    )
    # A nested helper is synchronously invoked by the owning slot's ordinary
    # per-tick script.  Predicates without an explicit timing wrapper (meter,
    # character profile, input/state gates) are therefore observable on the
    # slot's entry coordinate.  Omitting entry here made optimization of the
    # finite coordinate scan skip real immediate replacements such as the
    # full-gauge Fiendish Assault route.
    coordinates: set[int] = {max(0, int(lane_start_coordinate))}
    seen: set[int] = set()

    def visit(slot_index: int, depth: int) -> None:
        if depth > max_depth or slot_index in seen or not 0 <= slot_index < len(bank.slots):
            return
        seen.add(slot_index)
        script = bank.slots[slot_index].bytecode
        if script is None:
            return
        result = emulate(script, slot_index)
        for predicate in result.predicates:
            if predicate.callcond_idx != 0x25:
                continue
            words = _event_words(predicate.args)
            if not words:
                continue
            for authored in words[:2]:
                mapped = int(map_timing_index(timing, authored))
                if 0 <= mapped <= int(lane_slot.wTotalFrames):
                    coordinates.add(mapped)
        for call in result.bank_scripts:
            if call.callcond_idx != 0x0D or call.packed_move_id is None:
                continue
            child = bank.resolve_packed_slot(call.packed_move_id)
            if child is not None and _slot_can_author_nested_transition(bank, child):
                visit(child, depth + 1)

    visit(root_slot_index, 0)
    return frozenset(coordinates)


def _predicate_words(event: object) -> tuple[int, ...] | None:
    """Return the exact operand stream consumed by EvaluateIfOpcode.

    CALLCOND 0x25 targets LuxMoveVM_EvaluateIfOpcodeWithHeader
    @ 0x1402E5830, which prepends subopcode 0x0008. Its asset arguments are
    timing operands, not a subopcode.
    """

    words = _event_words(getattr(event, "args", ()))
    if words is None:
        return None
    return (0x0008,) + words if getattr(event, "callcond_idx", None) == 0x25 else words


def _predicate_subop(event: object) -> int | None:
    words = _predicate_words(event)
    return words[0] if words else None


def _signed_word(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def _slot_default_cell(bank: object, slot_index: int) -> int | None:
    if not 0 <= slot_index < len(bank.slots):
        return None
    refs = bank.slots[slot_index].nCellBoneIndexPerVariant
    if not refs or not 0 <= int(refs[0]) < 0x1000:
        return None
    cell_index = int(refs[0])
    sections = getattr(bank, "sections", ())
    if not sections or not 0 <= cell_index < len(sections[0].entries):
        return None
    cell = sections[0].entries[cell_index]
    # Cleared timing sentinels are authored in the slot's variant field but
    # never bind a usable collision descriptor.  Treating them as attacks
    # blocks wrapper routes from reaching their real nested-helper contact.
    if getattr(cell, "cell_role", None) == "Sentinel":
        return None
    return cell_index


_MOTION_FLAG_REACHABILITY_CACHE: dict[
    tuple[int, int, int],
    tuple[object, frozenset[tuple[int, int | None]], frozenset[tuple[int, int | None]]],
] = {}


def _transition_requires_motion_flag(
    script: object,
    slot_index: int,
    transition: object,
    flag_index: int,
) -> bool | None:
    """Return the IF66 flag value required to reach one transition.

    The shipped compiler commonly folds several EvaluateIfOpcode results into
    a temporary boolean before its final jump. Merely retaining the nearest
    predicate therefore loses earlier conjunction terms. Re-emulating twice
    with only one native predicate fixed preserves the real CFG and answers
    the narrower question without guessing a source-order association.
    """

    cache_key = (id(script), slot_index, flag_index)

    def reachable(forced: int) -> frozenset[tuple[int, int | None]]:
        def evaluate(fn_idx: int, args: tuple[object, ...]) -> int | None:
            words = _event_words(args)
            if (
                fn_idx in (0x00, 0x01, 0x25)
                and words is not None
                and len(words) >= 2
                and words[0] == 0x66
                and words[1] == flag_index
            ):
                return forced
            return None

        return frozenset(
            (int(candidate.source_pc), candidate.next_move_id_raw)
            for candidate in emulate(
                script,
                slot_index,
                callcond_evaluator=evaluate,
            ).transitions
        )

    cached = _MOTION_FLAG_REACHABILITY_CACHE.get(cache_key)
    if cached is not None and cached[0] is script:
        _, false_transitions, true_transitions = cached
    else:
        false_transitions = reachable(0)
        true_transitions = reachable(1)
        # Retain the script so Python cannot recycle its identity while this
        # export-wide cache is live.
        _MOTION_FLAG_REACHABILITY_CACHE[cache_key] = (
            script, false_transitions, true_transitions
        )
    transition_key = (int(transition.source_pc), transition.next_move_id_raw)
    reachable_when_false = transition_key in false_transitions
    reachable_when_true = transition_key in true_transitions
    if reachable_when_true and not reachable_when_false:
        return True
    if reachable_when_false and not reachable_when_true:
        return False
    return None


@dataclass(frozen=True)
class NativeContactFollowup:
    """A post-contact state proven from the attack slot's complete CFG."""

    source_slot: int
    target_slot: int
    outcome_flag: int
    target_attack_cell: int | None = None
    target_non_attack_cell: int | None = None
    damage: int | None = None
    resolutions: tuple[str, ...] = ()


def resolve_native_contact_followups(
    bank: object,
    attack_slot: int,
    *,
    standard_world_mode: int = 2,
) -> tuple[NativeContactFollowup, ...]:
    """Resolve hit-gated followups without treating them as extra startup hits.

    SC6 commonly binds a zero-damage contact cell to one slot, then moves to
    a cinematic/damage slot only after motion flag 27 is published by the hit
    path.  The compiler can fold that flag together with IF 0x0015's battle
    world-mode check, so the nearest predicate alone is insufficient.
    """

    if not 0 <= attack_slot < len(bank.slots):
        return ()
    slot = bank.slots[attack_slot]
    if slot.bytecode is None:
        return ()
    result = emulate(slot.bytecode, attack_slot)
    followups: list[NativeContactFollowup] = []
    for transition in result.transitions:
        if _transition_requires_motion_flag(
            slot.bytecode, attack_slot, transition, 27
        ) is not True:
            continue
        predicate_words = (
            _predicate_words(transition.predicate)
            if transition.predicate is not None else None
        )
        if predicate_words not in {
            None,
            (0x0015, int(standard_world_mode)),
        }:
            continue
        target = (
            bank.resolve_packed_slot(transition.next_move_id_raw)
            if transition.next_move_id_raw is not None else None
        )
        if target is None or not 0 <= int(target) < len(bank.slots):
            continue
        target_slot = bank.slots[int(target)]
        target_attack_cell: int | None = None
        target_non_attack_cell: int | None = None
        damage: int | None = None
        refs = target_slot.nCellBoneIndexPerVariant
        if refs:
            raw_ref = int(refs[0])
            if 0 <= raw_ref < 0x1000:
                target_attack_cell = _slot_default_cell(bank, int(target))
                if (
                    target_attack_cell is not None
                    and getattr(bank, "sections", ())
                    and 0 <= target_attack_cell < len(bank.sections[0].entries)
                ):
                    damage = int(
                        bank.sections[0].entries[target_attack_cell].wI16BaseDamage
                    )
            elif 0x1000 <= raw_ref < 0x2000 and len(getattr(bank, "sections", ())) > 1:
                candidate = raw_ref - 0x1000
                descriptors = bank.sections[1].non_attack_descriptors
                if 0 <= candidate < len(descriptors):
                    target_non_attack_cell = candidate
                    damage = int(descriptors[candidate].nSDamageMultiplier)
        followups.append(NativeContactFollowup(
            source_slot=attack_slot,
            target_slot=int(target),
            outcome_flag=27,
            target_attack_cell=target_attack_cell,
            target_non_attack_cell=target_non_attack_cell,
            damage=damage,
            resolutions=(
                f"khd-hit-gated-followup:slot{attack_slot}->slot{int(target)};"
                "motion-flag27=true",
                (
                    f"khd-standard-battle-world-mode:{standard_world_mode}"
                    if predicate_words is not None else
                    "khd-hit-followup-no-additional-predicate"
                ),
            ),
        ))
    return tuple(followups)


def _state_with_command_table(
    state: NativeInputState,
    command_table: TransitionCommandTable,
) -> NativeInputState:
    # Preserve every battle/lane field when swapping only the command table.
    # Positional reconstruction silently reset fields added to NativeInputState
    # and previously erased post-contact evidence.
    return replace(state, command_table=command_table)


def _extend_with_neutral_ticks(
    timeline: list[NativeInputState],
    through_frame: int,
) -> None:
    """Advance the authored final neutral state through delayed move checks."""

    if not timeline or timeline[-1].move_play_frame >= through_frame:
        return
    last = timeline[-1]
    history = InputHistoryRing(
        entries=list(last.history.entries),
        cursor=last.history.cursor,
        tick_count=last.history.tick_count,
    )
    for frame in range(last.move_play_frame + 1, through_frame + 1):
        history.commit_snapshot(last.snapshot)
        timeline.append(replace(
            last,
            history=InputHistoryRing(
                entries=list(history.entries),
                cursor=history.cursor,
                tick_count=history.tick_count,
            ),
            command_age_limit=min(max(1, history.tick_count), 0x7FFF),
            move_play_frame=frame,
        ))


def resolve_input_timed_attack_route(
    bank: object,
    start_slot: int,
    definition: MovePlayDefinition,
    command_table: TransitionCommandTable,
    codec_tables: LuxInputCodecTables,
    *,
    selected_move_play_frame: int | None = None,
    initial_variables: dict[int, int | StackVal] | None = None,
    character_profile_id: int | None = None,
    meter_state_shorts: dict[int, int] | None = None,
    max_slots: int = 12,
) -> NativeResolvedRoute:
    """Follow input predicates using the official move-play tick timeline.

    The standing dispatcher may publish more than one move while consuming an
    authored string; ``selected_move_play_frame`` identifies the publication
    that owns ``start_slot``. A transition which fires no later than the source cell's first
    active coordinate replaces that cell; it is not exported as an extra hit.

    Runtime predicates remain unresolved.  When one can schedule a transition
    at or after the selected cell becomes active, ordinary attacker recovery
    is not a proven endpoint and frame advantage must fail closed.
    """

    timeline = list(
        _state_with_command_table(state, command_table)
        for state in build_definition_input_timeline(definition, codec_tables)
    )
    if not timeline:
        return resolve_unconditional_attack_route(bank, start_slot, max_slots=max_slots)

    move_start = (
        selected_move_play_frame
        if selected_move_play_frame is not None
        else next(
            (
                state.move_play_frame
                for state in timeline
                if state.snapshot.current_compact_word & 0x2F
            ),
            timeline[0].move_play_frame,
        )
    )
    slots: list[int] = []
    cells: list[int] = []
    attack_slots: list[int] = []
    attack_cells: list[int] = []
    resolutions: list[str] = []
    seen: set[int] = set()
    current = start_slot
    # Absolute authored CPUAI frame on which the current slot becomes active,
    # plus the animation coordinate assigned to the destination lane.
    entered_at = move_start
    start_coordinate = 0
    startup_impact: int | None = None
    startup_timing_resolved = True
    ambiguous = False
    unresolved_frame_outcomes: set[str] = set()
    reachability_cache: dict[
        tuple[int, int, tuple[tuple[int, int | None], ...]],
        tuple[object, frozenset[int]],
    ] = {}

    while len(slots) < max_slots and 0 <= current < len(bank.slots):
        if current in seen:
            ambiguous = True
            resolutions.append(f"khd-input-route-cycle:slot{current}")
            break
        seen.add(current)
        slot = bank.slots[current]
        cell_index = _slot_default_cell(bank, current)
        cell_start_coordinate: int | None = None
        slots.append(current)
        if cell_index is not None:
            cells.append(cell_index)
            if not attack_cells:
                attack_slots.append(current)
                attack_cells.append(cell_index)
            resolutions.append(
                f"khd-default-cell-variant:slot{current}[0]->cell{cell_index}"
            )
            if (
                getattr(bank, "sections", ())
                and 0 <= cell_index < len(bank.sections[0].entries)
            ):
                cell_start_coordinate = int(
                    bank.sections[0].entries[cell_index].wI16MasterWindowStart
                )
        if slot.bytecode is None:
            break

        total_frames = int(slot.wTotalFrames)
        if 0 <= total_frames < 500:
            _extend_with_neutral_ticks(timeline, entered_at + total_frames + 1)

        script_result = emulate(slot.bytecode, current)
        transitions = script_result.transitions
        known_outcome_flags = {21, 26, 27, 28, 46}
        authored_outcome_flags = {
            words[1]
            for predicate in script_result.predicates
            if (words := _predicate_words(predicate)) is not None
            and words[0] == 0x66
            and len(words) >= 2
            and words[1] in known_outcome_flags
        }
        selected: list[tuple[int, int, int, int, int, int, int, bool, str]] = []
        unresolved_nested: list[tuple[int, int, str]] = []
        post_contact_nested: list[tuple[int, int, str, str, bool]] = []
        # input fire frame, target, target start coordinate, source PC,
        # threshold, matched animation coordinate, clocks decoupled, provenance

        # CALLCOND 0x0D helpers execute synchronously in the caller's lane and
        # may own its only TransitionAuthor writes.  Evaluate those helpers at
        # each authored lane coordinate before examining direct transitions.
        # This closes the general wrapper->common-helper->contact-state gap
        # without treating every statically present helper branch as live.
        nested_timing_coordinates = _nested_route_timing_coordinates(
            bank, current, start_coordinate
        )
        for state in timeline:
            if state.move_play_frame < entered_at:
                continue
            animation_coordinate = start_coordinate + state.move_play_frame - entered_at
            if animation_coordinate > total_frames:
                break
            if animation_coordinate not in nested_timing_coordinates:
                continue
            before_contact = (
                cell_start_coordinate is None
                or animation_coordinate < cell_start_coordinate
            )
            route_state = replace(
                state,
                active_move_id=current,
                animation_frame=animation_coordinate,
                primary_script_running=False,
                secondary_script_running=False,
                animation_ended=False,
                per_frame_motion_flags=(0,) * 0x72,
            )
            if before_contact:
                outcome_states = (((None,), route_state),)
            else:
                # ProcessHitReactionState publishes motion flag 46 only for a
                # blocked hit and flag 27 for the shared ordinary-hit tail
                # (normal and counter hit). PreTickStateSnapshot then exposes
                # their zero->nonzero changes as IF66 edge value +1. Evaluate
                # the complete nested helper CFG separately for each outcome
                # so a hit-only lane replacement cannot invalidate Block.
                def with_rising_edge(index: int) -> NativeInputState:
                    edges = [0] * 0x72
                    edges[index] = 1
                    return replace(
                        route_state,
                        per_frame_motion_flags=tuple(edges),
                    )

                # Ordinary Hit and Counter Hit both publish the same native
                # motion-flag-27 rising edge.  Their defender stun columns are
                # selected later by contact mode; the attacker's post-contact
                # MoveVM route is identical here.  Evaluate that state once and
                # apply the resulting route hazard to both outcomes.
                outcome_states = (
                    (("block",), with_rising_edge(46)),
                    (("hit", "counterHit"), with_rising_edge(27)),
                )
            for outcomes, outcome_state in outcome_states:
                nested_routes = _nested_route_transitions_at_coordinate(
                    bank,
                    current,
                    outcome_state,
                    animation_coordinate,
                    start_coordinate,
                    initial_variables=initial_variables,
                    character_profile_id=character_profile_id,
                    meter_state_shorts=dict(meter_state_shorts or {}),
                )
                for nested in nested_routes:
                    transition = nested.transition
                    transition_args = _event_words(transition.args)
                    target = (
                        bank.resolve_packed_slot(transition.next_move_id_raw)
                        if transition.next_move_id_raw is not None else None
                    )
                    if transition_args is None or target is None:
                        continue
                    target_start_raw = (
                        _signed_word(transition_args[1])
                        if len(transition_args) >= 2 else 0
                    )
                    threshold_raw = (
                        _signed_word(transition_args[2])
                        if len(transition_args) >= 3 else 0
                    )
                    if target_start_raw >= 0x6000 or threshold_raw >= 0x6000:
                        continue
                    threshold_frame = entered_at + max(
                        0, threshold_raw - start_coordinate
                    )
                    transition_provenance = "nested:" + "->".join(
                        f"slot{slot_index}" for slot_index in nested.call_path
                    )
                    if outcomes != (None,):
                        post_contact_nested.extend(
                            (
                                max(state.move_play_frame, threshold_frame),
                                int(target),
                                transition_provenance,
                                outcome,
                                nested.static_predicates_resolved,
                            )
                            for outcome in outcomes
                        )
                        continue
                    if not nested.static_predicates_resolved:
                        unresolved_nested.append((
                            max(state.move_play_frame, threshold_frame),
                            int(target),
                            transition_provenance,
                        ))
                        continue
                    selected.append((
                        max(state.move_play_frame, threshold_frame),
                        state.move_play_frame,
                        int(target),
                        target_start_raw,
                        int(transition.source_pc),
                        threshold_raw,
                        animation_coordinate,
                        False,
                        transition_provenance,
                    ))
        if post_contact_nested:
            # The edge bank is observable for one pre-tick, but evaluating
            # every authored helper coordinate is intentionally conservative
            # for reachability.  Collapse identical outcome destinations to
            # their earliest observation so production evidence does not
            # repeat the same branch once per later coordinate.
            earliest_post_contact: dict[
                tuple[int, str, str, bool], tuple[int, int, str, str, bool]
            ] = {}
            for item in post_contact_nested:
                frame, target, provenance, outcome, resolved = item
                key = (target, provenance, outcome, resolved)
                previous = earliest_post_contact.get(key)
                if previous is None or frame < previous[0]:
                    earliest_post_contact[key] = item
            post_contact_nested = list(earliest_post_contact.values())
            affected = {item[3] for item in post_contact_nested}
            unresolved_frame_outcomes.update(affected)
            resolutions.append(
                f"khd-post-contact-outcome-branch:slot{current}->"
                + ",".join(
                    f"slot{target}@frame{frame}[{outcome};"
                    f"{'resolved-cfg' if resolved else 'predicate-unresolved'};"
                    f"{provenance}]"
                    for frame, target, provenance, outcome, resolved
                    in sorted(set(post_contact_nested))
                )
            )
            break
        for transition in transitions:
            predicate = transition.predicate
            if (
                predicate is not None
                and _predicate_subop(predicate) not in _OFFLINE_INPUT_TRANSITION_SUBOPS
            ):
                continue
            predicate_args = _predicate_words(predicate) if predicate is not None else None
            transition_args = _event_words(transition.args)
            target = (
                bank.resolve_packed_slot(transition.next_move_id_raw)
                if transition.next_move_id_raw is not None else None
            )
            if transition_args is None or target is None:
                continue
            target_start = _signed_word(transition_args[1]) if len(transition_args) >= 2 else 0
            threshold = _signed_word(transition_args[2]) if len(transition_args) >= 3 else 0
            # Dynamic timing-index buckets require lane/motion state and stay
            # unresolved.  Literal coordinates are the only offline-safe form.
            if target_start >= 0x6000 or threshold >= 0x6000:
                continue
            transition_matched = False
            for state in timeline:
                # Entry bytecode runs synchronously under lane+0x26. This
                # route walk models the ordinary per-tick continuation pass;
                # do not replay the selector publication as that later pass.
                if state.move_play_frame <= entered_at:
                    continue
                animation_coordinate = (
                    start_coordinate + state.move_play_frame - entered_at
                )
                if animation_coordinate > int(slot.wTotalFrames):
                    break
                predicate_coordinate = animation_coordinate
                route_state = replace(
                    state,
                    active_move_id=current,
                    animation_frame=predicate_coordinate,
                    primary_script_running=False,
                    secondary_script_running=False,
                    animation_ended=False,
                    per_frame_motion_flags=(
                        (0,) * 0x72
                        if (
                            cell_start_coordinate is None
                            or predicate_coordinate < cell_start_coordinate
                        )
                        else None
                    ),
                )
                if predicate_args is None:
                    # The current route model does not yet retain the native
                    # scheduler's source-order/default selection state.  Only
                    # consume a default author once its threshold is current;
                    # admitting it earlier would race preceding conditional
                    # authors and invent mutually exclusive destinations.
                    if predicate_coordinate < threshold:
                        continue
                elif evaluate_input_if(
                        route_state,
                        predicate_args,
                        animation_frame=predicate_coordinate,
                    ) != 1:
                        continue
                if not _transition_reachable_with_static_predicates(
                    slot.bytecode,
                    current,
                    route_state,
                    predicate_coordinate,
                    transition,
                    reachability_cache,
                ):
                    continue
                threshold_frame = entered_at + max(0, threshold - start_coordinate)
                fire_frame = max(state.move_play_frame, threshold_frame)
                selected.append((
                    fire_frame,
                    state.move_play_frame,
                    int(target),
                    target_start,
                    int(transition.source_pc),
                    threshold,
                    predicate_coordinate,
                    False,
                    f"slot{current}",
                ))
                transition_matched = True
                break

            if transition_matched or predicate_args is None:
                continue

            # The CPUAI BTN driver and the combat lane do not share a clock.
            # LuxMoveVM_TickDriver can keep publishing a BTN mask while its
            # duration countdown is gated by an active attack-cell lane, while
            # LuxMoveVM_AdvanceLaneFrameStep advances the lane independently.
            # If the one-to-one alignment fails, search the finite authored
            # input observations against literal lane coordinates. This proves
            # route reachability but deliberately does not prove player-frame
            # alignment/startup timing.
            coordinate_limit = min(max(0, int(slot.wTotalFrames)), 499)
            literal_timing_coordinates = {
                max(0, start_coordinate),
                max(0, threshold),
            }
            for authored_predicate in script_result.predicates:
                timing_words = _predicate_words(authored_predicate)
                if not timing_words or timing_words[0] != 0x0008:
                    continue
                for raw_coordinate in timing_words[1:]:
                    if raw_coordinate == 0x7FFF or raw_coordinate >= 0x6000:
                        continue
                    literal_timing_coordinates.add(max(0, _signed_word(raw_coordinate)))
            for predicate_coordinate in sorted(literal_timing_coordinates):
                if predicate_coordinate >= coordinate_limit:
                    continue
                if predicate_coordinate < threshold:
                    continue
                for state in timeline:
                    # This is explicitly a clock-decoupled reachability
                    # search: the selector publication may be the input later
                    # observed at an authored lane coordinate.  It is not
                    # replayed as the ordinary entry-tick VM pass above.
                    if state.move_play_frame < entered_at:
                        continue
                    if evaluate_input_if(
                        state,
                        predicate_args,
                        animation_frame=predicate_coordinate,
                    ) != 1:
                        continue
                    if not _transition_reachable_with_static_predicates(
                        slot.bytecode,
                        current,
                        state,
                        predicate_coordinate,
                        transition,
                        reachability_cache,
                    ):
                        continue
                    threshold_frame = entered_at + max(
                        0, threshold - start_coordinate
                    )
                    selected.append((
                        max(state.move_play_frame, threshold_frame),
                        state.move_play_frame,
                        int(target),
                        target_start,
                        int(transition.source_pc),
                        threshold,
                        predicate_coordinate,
                        True,
                        f"slot{current}",
                    ))
                    transition_matched = True
                    break
                if transition_matched:
                    break

        if unresolved_nested:
            earliest_unresolved = min(item[0] for item in unresolved_nested)
            earliest_selected = min((item[0] for item in selected), default=None)
            contact_frame = (
                entered_at + max(0, cell_start_coordinate - start_coordinate)
                if cell_start_coordinate is not None else None
            )
            if (
                contact_frame is not None
                and earliest_unresolved >= contact_frame
                and (earliest_selected is None or earliest_unresolved <= earliest_selected)
            ):
                unresolved_frame_outcomes.update(("block", "hit", "counterHit"))
                resolutions.append(
                    f"khd-post-contact-transition-unresolved:slot{current}"
                    f"@frame{earliest_unresolved}->"
                    + ",".join(
                        f"slot{target}[{provenance}]"
                        for frame, target, provenance in sorted(set(unresolved_nested))
                        if frame == earliest_unresolved
                    )
                )
                break
            if earliest_selected is None or earliest_unresolved <= earliest_selected:
                ambiguous = True
                resolutions.append(
                    f"khd-nested-transition-predicate-unresolved:slot{current}"
                    f"@frame{earliest_unresolved}->"
                    + ",".join(
                        f"slot{target}[{provenance}]"
                        for frame, target, provenance in sorted(set(unresolved_nested))
                        if frame == earliest_unresolved
                    )
                )
                break

        if selected:
            earliest_frame = min(item[0] for item in selected)
            earliest = [item for item in selected if item[0] == earliest_frame]
            # TransitionAuthor stores deferred lane state before the shared
            # threshold commit.  A later author therefore replaces an earlier
            # author for that same commit.  Authors on the commit tick occur
            # after the pending transition check, so prefer the latest author
            # strictly before commit whenever one exists.
            before_commit = [item for item in earliest if item[1] < earliest_frame]
            contenders = before_commit or earliest
            latest_author = max(item[1] for item in contenders)
            earliest = [item for item in contenders if item[1] == latest_author]
            unique_targets = {item[2] for item in earliest}
            if len(unique_targets) != 1:
                ambiguous = True
                resolutions.append(
                    f"khd-input-transition-ambiguous:slot{current}@frame{earliest_frame}->"
                    + ",".join(f"slot{target}" for target in sorted(unique_targets))
                )
                break
            (
                fire_frame,
                author_frame,
                target,
                target_start,
                source_pc,
                threshold,
                matched_coordinate,
                clocks_decoupled,
                transition_provenance,
            ) = earliest[0]
            target_cell = _slot_default_cell(bank, target)
            # TransitionAuthor's literal threshold belongs to the source
            # lane's animation clock.  A threshold no later than the source
            # cell window is an authored pre-contact replacement even when
            # the CPUAI/history publication clock cannot yet be aligned to
            # that lane clock without runtime state.
            source_cell_coordinate = (
                int(bank.sections[0].entries[cell_index].wI16MasterWindowStart)
                if cell_index is not None
                and getattr(bank, "sections", ())
                and 0 <= cell_index < len(bank.sections[0].entries)
                else None
            )
            replacement = (
                source_cell_coordinate is None
                or threshold <= source_cell_coordinate
            )
            if replacement and target_cell is not None:
                # Replace only the current segment's not-yet-active contact.
                # Earlier contacts in a string remain authored hits. Wiping
                # the whole route here discarded valid openers whenever a
                # held/timing variant replaced a later segment.
                if attack_slots and attack_slots[-1] == current:
                    attack_slots[-1] = target
                    attack_cells[-1] = target_cell
                else:
                    attack_slots.append(target)
                    attack_cells.append(target_cell)
            elif target_cell is not None:
                attack_slots.append(target)
                attack_cells.append(target_cell)
            # A clock-decoupled replacement can change the first contact and
            # therefore invalidates startup.  A later followup cannot move an
            # already-proven first active frame; treating every later string
            # transition as startup uncertainty blanked otherwise valid i-data.
            if clocks_decoupled and replacement:
                startup_timing_resolved = False
            resolutions.append(
                f"khd-input-timed-transition:slot{current}@0x{source_pc:X};"
                f"input-frame={author_frame};commit-frame={fire_frame};"
                f"predicate-coordinate={matched_coordinate};"
                f"threshold={threshold};"
                f"target-start={target_start}->slot{target};"
                f"kind={'replacement' if replacement else 'followup'};"
                f"clock-alignment={'unproven' if clocks_decoupled else 'one-to-one'};"
                f"source={transition_provenance}"
            )
            current = target
            entered_at = fire_frame
            start_coordinate = target_start
            continue

        # A runtime-gated transition at/after the attack window can replace
        # ordinary recovery on contact. Native motion flags 21/26/27/28 and
        # 46 are outcome publications: ProcessHitReactionState queues the
        # former only on hit-reaction paths and 46 only on the block path;
        # DrainPerFrameMotionFlagBuffer publishes them to the bank read by
        # IF66. Other predicates remain unknown and affect every outcome.
        cell_start: int | None = None
        if cell_index is not None and getattr(bank, "sections", ()):
            attack_table = bank.sections[0].entries
            if 0 <= cell_index < len(attack_table):
                cell_start = int(attack_table[cell_index].wI16MasterWindowStart)
        endpoint_hazards: list[tuple[int, int, tuple[str, ...]]] = []
        if cell_start is not None:
            # A statically listed TransitionAuthor is not automatically live
            # for this official input route.  Test its complete CFG with the
            # authored CPUAI observations and native timing literals first.
            # This is intentionally transition-specific: the old script-wide
            # `has_runtime_predicate` flag made an unrelated unknown branch
            # invalidate every later transition in the slot.
            hazard_coordinates = {
                max(0, start_coordinate),
                max(0, cell_start),
            }
            for authored_predicate in script_result.predicates:
                timing_words = _predicate_words(authored_predicate)
                if not timing_words or timing_words[0] != 0x0008:
                    continue
                for raw_coordinate in timing_words[1:]:
                    if raw_coordinate == 0x7FFF or raw_coordinate >= 0x6000:
                        continue
                    hazard_coordinates.add(max(0, _signed_word(raw_coordinate)))
            for transition in transitions:
                args = _event_words(transition.args)
                if args is None or len(args) < 3:
                    continue
                threshold = _signed_word(args[2])
                if threshold < 0x6000 and threshold >= cell_start:
                    target = (
                        bank.resolve_packed_slot(transition.next_move_id_raw)
                        if transition.next_move_id_raw is not None else None
                    )
                    if target is not None:
                        transition_may_run = False
                        for predicate_coordinate in sorted({
                            *hazard_coordinates,
                            max(0, threshold),
                        }):
                            if predicate_coordinate > total_frames:
                                continue
                            before_contact = predicate_coordinate < cell_start
                            for state in timeline:
                                route_state = replace(
                                    state,
                                    active_move_id=current,
                                    animation_frame=predicate_coordinate,
                                    primary_script_running=False,
                                    secondary_script_running=False,
                                    animation_ended=False,
                                    per_frame_motion_flags=(
                                        (0,) * 0x72 if before_contact else None
                                    ),
                                )
                                if _transition_reachable_with_static_predicates(
                                    slot.bytecode,
                                    current,
                                    route_state,
                                    predicate_coordinate,
                                    transition,
                                    reachability_cache,
                                ):
                                    transition_may_run = True
                                    break
                            if transition_may_run:
                                break
                        if not transition_may_run:
                            continue
                        possible = {"block", "hit", "counterHit"}
                        recognized = False
                        hit_only_flags = {21, 26, 27, 28}
                        outcome_flag_routes = [
                            (index, {"hit", "counterHit"})
                            for index in sorted(hit_only_flags & authored_outcome_flags)
                        ]
                        if 46 in authored_outcome_flags:
                            outcome_flag_routes.append((46, {"block"}))
                        for flag_index, true_outcomes in outcome_flag_routes:
                            required = _transition_requires_motion_flag(
                                slot.bytecode, current, transition, flag_index
                            )
                            # A false edge is not an exhaustive opposite
                            # outcome: special hits can omit flag 27 and
                            # nonstandard guards can omit flag 46.
                            if required is not True:
                                continue
                            recognized = True
                            possible &= true_outcomes
                        affected = possible if recognized else {"block", "hit", "counterHit"}
                        endpoint_hazards.append(
                            (int(target), threshold, tuple(sorted(affected)))
                        )
        if endpoint_hazards:
            for _, _, outcomes in endpoint_hazards:
                unresolved_frame_outcomes.update(outcomes)
            resolutions.append(
                f"khd-runtime-contact-branch-unresolved:slot{current}->"
                + ",".join(
                    f"slot{target}@{threshold}[{'/'.join(outcomes)}]"
                    for target, threshold, outcomes in sorted(set(endpoint_hazards))
                )
            )
            break

        unconditional = []
        if not any(transition.predicate is not None for transition in transitions):
            for transition in transitions:
                if transition.next_move_id_raw is None:
                    continue
                target = bank.resolve_packed_slot(transition.next_move_id_raw)
                args = _event_words(transition.args)
                if target is not None and args is not None:
                    unconditional.append((int(target), int(transition.source_pc), args))
        unique_targets = {target for target, _, _ in unconditional}
        if not unique_targets:
            break
        if len(unique_targets) != 1:
            ambiguous = True
            resolutions.append(
                f"khd-unconditional-branch:slot{current}->"
                + ",".join(f"slot{target}" for target in sorted(unique_targets))
            )
            break
        target = next(iter(unique_targets))
        _, source_pc, args = next(item for item in unconditional if item[0] == target)
        target_start = _signed_word(args[1]) if len(args) >= 2 else 0
        threshold = _signed_word(args[2]) if len(args) >= 3 else 0
        entered_at += max(0, threshold - start_coordinate)
        start_coordinate = target_start
        resolutions.append(
            f"khd-unconditional-followup:slot{current}@0x{source_pc:X}->slot{target}"
        )
        current = target
    else:
        if len(slots) >= max_slots:
            ambiguous = True
            resolutions.append(f"khd-route-limit:{max_slots}")

    if startup_timing_resolved and attack_cells and getattr(bank, "sections", ()):
        attack_table = bank.sections[0].entries
        first_slot = attack_slots[0]
        first_cell = attack_cells[0]
        if 0 <= first_cell < len(attack_table):
            if first_slot == current and entered_at >= move_start:
                first_entered_at = entered_at
                first_start_coordinate = start_coordinate
            else:
                # For a retained source hit, its lane starts at move_start/0.
                first_entered_at = move_start
                first_start_coordinate = 0
            startup_impact = first_entered_at + max(
                0,
                int(attack_table[first_cell].wI16MasterWindowStart)
                - first_start_coordinate,
            )
    relative_startup = (
        startup_impact - move_start if startup_impact is not None else None
    )
    return NativeResolvedRoute(
        tuple(slots),
        tuple(cells),
        ambiguous,
        tuple(resolutions),
        attack_slots=tuple(attack_slots),
        attack_cells=tuple(attack_cells),
        startup_impact_coordinate=relative_startup,
        startup_player_frame=(
            relative_startup + 1 if relative_startup is not None else None
        ),
        startup_timing_resolved=startup_timing_resolved,
        frame_endpoints_resolved=not unresolved_frame_outcomes,
        unresolved_frame_outcomes=tuple(sorted(unresolved_frame_outcomes)),
    )
