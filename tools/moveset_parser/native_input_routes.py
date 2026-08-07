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
from dataclasses import dataclass

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
from stackvm_emulate import emulate


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


def evaluate_input_if(state: NativeInputState, args: tuple[int, ...]) -> int | None:
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
    elif subop == 0x0005:
        result = _history_primary_sequence(state, args)
    elif subop == 0x0006:
        result = _history_secondary(state, args)
    elif subop == 0x0020:
        result = _history_primary_all(state, args)
    elif subop == 0x0024:
        entry = state.snapshot.history_entry()
        result = all(check_history_condition(entry, clause) for clause in args[1:])
    elif subop in (0x002B, 0x002C, 0x0084):
        if len(args) < 2:
            return None
        mode = {0x002B: 1, 0x002C: 0, 0x0084: 2}[subop]
        result = _command_condition(state, args[1] & 0xFFFF, mode)
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
        max_states: int = 50_000,
        max_depth: int = 12,
    ) -> None:
        self.bank = bank
        self.command_table = command_table
        self.codec_tables = codec_tables
        self.max_states = max_states
        self.max_depth = max_depth
        self._definition_cache: dict[int, NativeRouteCandidates] = {}
        self._attack_route_cache: dict[
            tuple[int, int, int | None], NativeResolvedRoute
        ] = {}

    def resolve_definition(self, definition: MovePlayDefinition) -> "NativeRouteCandidates":
        cached = self._definition_cache.get(definition.definition_id)
        if cached is not None:
            return cached
        input_states = build_definition_input_states(definition, self.codec_tables)
        if not input_states:
            result = NativeRouteCandidates((), True, ("cpuai-definition-not-linear",))
            self._definition_cache[definition.definition_id] = result
            return result
        root = self.bank.resolve_packed_slot(0x3048)
        if root is None:
            result = NativeRouteCandidates((), True, ("khd-common-dispatcher-0x3048-unresolved",))
            self._definition_cache[definition.definition_id] = result
            return result
        candidate_values: set[int] = set()
        selected_helper: int | None = None
        selected_tick: int | None = None
        selected_move_play_frame: int | None = None
        truncated = False
        for input_state_without_commands in input_states:
            # Direction-only setup publications do not start an attack slot;
            # retain them in history but ask the dispatcher only when the
            # authored program publishes an attack/guard button.
            if not (input_state_without_commands.snapshot.current_compact_word & 0x2F):
                continue
            input_state = NativeInputState(
                input_state_without_commands.snapshot,
                input_state_without_commands.history,
                self.command_table,
                input_state_without_commands.command_age_limit,
                input_state_without_commands.chara_state_shorts,
                input_state_without_commands.previous_chara_state_shorts,
                input_state_without_commands.opponent_previous_chara_state_shorts,
                input_state_without_commands.motion_state_latches,
                input_state_without_commands.move_play_frame,
            )
            result = self._run_slot(root, input_state, (), (), 0, ())
            truncated = truncated or result.truncated
            by_helper = dict(result.selected_by_helper)
            # Packed 0x304E is the standing input selector reached after the
            # common 0x3015 baseline writes MoveVM chara-state[0]=1.  Once it
            # selects a slot, later CPUAI publications are inputs *inside that
            # move* and must not be reinterpreted from the neutral root.
            standing = {
                value for value in by_helper.get(0x304E, ())
                if value not in (0, 0xFFFF)
            }
            if standing:
                candidate_values.update(standing)
                selected_helper = 0x304E
                selected_tick = input_state.history.tick_count
                selected_move_play_frame = input_state.move_play_frame
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
                candidate_values.update(fallback)
                selected_helper = -1
                selected_tick = input_state.history.tick_count
                selected_move_play_frame = input_state.move_play_frame
                break
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
                (
                    f"khd-selector:packed0x{selected_helper:X};tick={selected_tick}"
                    if selected_helper is not None and selected_helper >= 0
                    else f"khd-selector:context-dependent;tick={selected_tick}"
                ),
            ),
            selected_move_play_frame,
        )
        self._definition_cache[definition.definition_id] = resolved
        return resolved

    def resolve_attack_route(
        self,
        definition: MovePlayDefinition,
        start_slot: int,
        *,
        selected_move_play_frame: int | None = None,
    ) -> "NativeResolvedRoute":
        """Resolve and cache the in-move route for one CPUAI definition."""

        key = (definition.definition_id, start_slot, selected_move_play_frame)
        cached = self._attack_route_cache.get(key)
        if cached is None:
            cached = resolve_input_timed_attack_route(
                self.bank,
                start_slot,
                definition,
                self.command_table,
                self.codec_tables,
                selected_move_play_frame=selected_move_play_frame,
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
        ordered = sorted(script.instructions, key=lambda instruction: instruction.pc)
        return_var_ids = {
            previous.imm_u16 or 0
            for previous, terminal in zip(ordered, ordered[1:])
            if terminal.opcode in (0x05, 0x07)
            and previous.opcode == 0x0A
            and previous.push_flag
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
            nonlocal returns, exit_globals
            returns = _union(returns, value)
            if value is not None:
                known_returns.update(value)
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
                # Fixed-point joins may later widen the return variable to
                # TOP.  Preserve every concrete value authored into it while
                # the path is still specific; these are conservative known
                # members of the function's return set.
                if varid in return_var_ids and acc is not None:
                    known_returns.update(acc)
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
                        evaluated = evaluate_input_if(input_state, tuple(int(value) for value in concrete))
                    else:
                        evaluated = None
                    acc = _one(evaluated) if evaluated is not None else BOOL
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
                                if depth == 0 and packed == 0x304E and child.known_returns:
                                    selected = frozenset(child.known_returns)
                                    return _Result(
                                        child.returns,
                                        child.globals,
                                        selected,
                                        selected,
                                        ((packed, selected),),
                                        truncated or child.truncated,
                                    )
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
        variant_zero = (
            slot.nCellBoneIndexPerVariant[0]
            if slot.nCellBoneIndexPerVariant else -1
        )
        has_attack_cell = 0 <= variant_zero < 0x1000
        if cells and not has_attack_cell:
            # An automatic recovery/idle destination is not part of the
            # player's attack route.
            break
        slots.append(current)
        if has_attack_cell:
            cells.append(variant_zero)
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
        if (
            bank.slots[slot_index].nCellBoneIndexPerVariant
            and 0 <= bank.slots[slot_index].nCellBoneIndexPerVariant[0] < 0x1000
        )
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
    0x0005,
    0x0006,
    0x0020,
    0x0024,
    0x002B,
    0x002C,
    0x0084,
})


def _event_words(values: object) -> tuple[int, ...] | None:
    words: list[int] = []
    for value in values:
        as_int = getattr(value, "as_int", None)
        word = as_int() if callable(as_int) else None
        if word is None:
            return None
        words.append(int(word) & 0xFFFF)
    return tuple(words)


def _signed_word(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def _slot_default_cell(bank: object, slot_index: int) -> int | None:
    if not 0 <= slot_index < len(bank.slots):
        return None
    refs = bank.slots[slot_index].nCellBoneIndexPerVariant
    if not refs or not 0 <= int(refs[0]) < 0x1000:
        return None
    return int(refs[0])


def _state_with_command_table(
    state: NativeInputState,
    command_table: TransitionCommandTable,
) -> NativeInputState:
    return NativeInputState(
        state.snapshot,
        state.history,
        command_table,
        state.command_age_limit,
        state.chara_state_shorts,
        state.previous_chara_state_shorts,
        state.opponent_previous_chara_state_shorts,
        state.motion_state_latches,
        state.move_play_frame,
    )


def resolve_input_timed_attack_route(
    bank: object,
    start_slot: int,
    definition: MovePlayDefinition,
    command_table: TransitionCommandTable,
    codec_tables: LuxInputCodecTables,
    *,
    selected_move_play_frame: int | None = None,
    max_slots: int = 12,
) -> NativeResolvedRoute:
    """Follow input predicates using the official move-play tick timeline.

    The standing dispatcher consumes the first attack publication.  Later
    publications belong to the selected move and are tested against that
    slot's authored predicates rather than being dispatched again from
    neutral.  A transition which fires no later than the source cell's first
    active coordinate replaces that cell; it is not exported as an extra hit.

    Runtime predicates remain unresolved.  When one can schedule a transition
    at or after the selected cell becomes active, ordinary attacker recovery
    is not a proven endpoint and frame advantage must fail closed.
    """

    timeline = tuple(
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
    frame_endpoints_resolved = True

    while len(slots) < max_slots and 0 <= current < len(bank.slots):
        if current in seen:
            ambiguous = True
            resolutions.append(f"khd-input-route-cycle:slot{current}")
            break
        seen.add(current)
        slot = bank.slots[current]
        cell_index = _slot_default_cell(bank, current)
        slots.append(current)
        if cell_index is not None:
            cells.append(cell_index)
            if not attack_cells:
                attack_slots.append(current)
                attack_cells.append(cell_index)
            resolutions.append(
                f"khd-default-cell-variant:slot{current}[0]->cell{cell_index}"
            )
        if slot.bytecode is None:
            break

        transitions = emulate(slot.bytecode, current).transitions
        selected: list[tuple[int, int, int, int, int]] = []
        # fire frame, target, target start coordinate, source PC, threshold
        for transition in transitions:
            predicate = transition.predicate
            if predicate is None or predicate.sub_opcode not in _OFFLINE_INPUT_TRANSITION_SUBOPS:
                continue
            predicate_args = _event_words(predicate.args)
            transition_args = _event_words(transition.args)
            target = (
                bank.resolve_packed_slot(transition.next_move_id_raw)
                if transition.next_move_id_raw is not None else None
            )
            if predicate_args is None or transition_args is None or target is None:
                continue
            target_start = _signed_word(transition_args[1]) if len(transition_args) >= 2 else 0
            threshold = _signed_word(transition_args[2]) if len(transition_args) >= 3 else 0
            # Dynamic timing-index buckets require lane/motion state and stay
            # unresolved.  Literal coordinates are the only offline-safe form.
            if target_start >= 0x6000 or threshold >= 0x6000:
                continue
            for state in timeline:
                if state.move_play_frame < entered_at:
                    continue
                animation_coordinate = (
                    start_coordinate + state.move_play_frame - entered_at
                )
                if animation_coordinate >= int(slot.wTotalFrames):
                    break
                if evaluate_input_if(state, predicate_args) != 1:
                    continue
                threshold_frame = entered_at + max(0, threshold - start_coordinate)
                fire_frame = max(state.move_play_frame, threshold_frame)
                selected.append((
                    fire_frame,
                    int(target),
                    target_start,
                    int(transition.source_pc),
                    threshold,
                ))
                break

        if selected:
            earliest_frame = min(item[0] for item in selected)
            earliest = [item for item in selected if item[0] == earliest_frame]
            unique_targets = {item[1] for item in earliest}
            if len(unique_targets) != 1:
                ambiguous = True
                resolutions.append(
                    f"khd-input-transition-ambiguous:slot{current}@frame{earliest_frame}->"
                    + ",".join(f"slot{target}" for target in sorted(unique_targets))
                )
                break
            fire_frame, target, target_start, source_pc, threshold = earliest[0]
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
                attack_slots[:] = [target]
                attack_cells[:] = [target_cell]
                startup_timing_resolved = False
            elif target_cell is not None:
                attack_slots.append(target)
                attack_cells.append(target_cell)
            resolutions.append(
                f"khd-input-timed-transition:slot{current}@0x{source_pc:X};"
                f"predicate-frame={fire_frame};threshold={threshold};"
                f"target-start={target_start}->slot{target};"
                f"kind={'replacement' if replacement else 'followup'}"
            )
            current = target
            entered_at = fire_frame
            start_coordinate = target_start
            continue

        # A runtime-gated transition at/after the attack window can replace
        # ordinary recovery on contact.  The unannotated partner in an
        # if/else block is equally unsafe, so the entire timed branch fails
        # closed without guessing which outcome is block/hit/counterhit.
        cell_start: int | None = None
        if cell_index is not None and getattr(bank, "sections", ()):
            attack_table = bank.sections[0].entries
            if 0 <= cell_index < len(attack_table):
                cell_start = int(attack_table[cell_index].wI16MasterWindowStart)
        has_runtime_predicate = any(
            transition.predicate is not None
            and transition.predicate.sub_opcode not in _OFFLINE_INPUT_TRANSITION_SUBOPS
            for transition in transitions
        )
        endpoint_hazards: list[tuple[int, int]] = []
        if cell_start is not None and has_runtime_predicate:
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
                        endpoint_hazards.append((int(target), threshold))
        if endpoint_hazards:
            frame_endpoints_resolved = False
            resolutions.append(
                f"khd-runtime-contact-branch-unresolved:slot{current}->"
                + ",".join(
                    f"slot{target}@{threshold}"
                    for target, threshold in sorted(set(endpoint_hazards))
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
        frame_endpoints_resolved=frame_endpoints_resolved,
    )
