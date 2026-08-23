"""Reusable static attacker-recovery and defender-stun analysis.

The analyzer intentionally accepts a *proven* KHD slot/cell pair; it does not
solve official-row routing.  This separation lets every confirmed route use
the same arithmetic while ambiguous routes remain null.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib

from lux_reference_engine import (
    CallCondResult,
    MoveVMContext,
    MoveVMReference,
    StaticResolutionError,
)
from native_reaction_table import LuxHitReactionMoveIdTable
from stackvm_emulate import Concrete, emulate


ATTACK_SETUP_HELPER = 0x3020
REACTION_TICK_HELPER = 0x305E
REACTION_NOOP_ROOT_AUXILIARY_HELPERS = frozenset({0x3242})
REACTION_ROOT_AUXILIARY_HELPERS = frozenset(
    {0x305C} | REACTION_NOOP_ROOT_AUXILIARY_HELPERS
)
REACTION_STATE_HELPERS = frozenset({0x3105, 0x3115})
REACTION_MOTION_SETUP_HELPERS = frozenset({0x3104, 0x3114})
REACTION_FLAG_WINDOW_HELPER = 0x305D
REACTION_STATE_CLASSIFIER_HELPER = 0x3019

# These indices feed the native current-state aggregate at index 0x0B.  They
# are reaction sources, not a fall set: 0x0C is blockstun, 0x0E is ordinary
# hitstun, and 0x0F is the airborne-reaction subtype.  Flags 0x10/0x11 are
# later reaction-state inputs, but neither is a native knockdown enum.
# Outcome classification must use independently proven native consumers.
REACTION_AGGREGATE_SOURCE_FLAGS = frozenset(
    {0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x25, 0x35}
)
ORDINARY_HITSTUN_FLAG = 0x0E
AIRBORNE_REACTION_FLAG = 0x0F

# Packed helper 0x305D has the same opcode shape in every checked-in fighter
# bank.  Its three local arguments are (state flag, start, end): it latches the
# flag with native CALLCOND 0x09 inside the mapped timing window and clears it
# through CALLCOND 0x0A after end+1.  End 0x7FFF maps to the reaction-lane end,
# so such a window proves a terminal subtype rather than a transient phase.
REACTION_FLAG_WINDOW_OPCODE_SHA256 = (
    "ACDF395AED49EEA9E5259FFD0E21F84407A6213A81F9A922D6B3DAAE5E1839BF"
)

# Packed 0x3242 is the same three-instruction FRAME 0; NOP; RET2 script in
# every playable bank.  Some reaction wrappers call it after their real
# driver.  It is safe to ignore only while the exact bytes remain unchanged.
REACTION_NOOP_ROOT_AUXILIARY_SHA256 = (
    "06DABC1C16AA6BAA394CD5D356B6EAC101811B0BF78CE32A1EE893CAD4B0A83F"
)

# The common v2.31 attack-setup helper has character-relative packed ids, so
# its raw bytes differ per bank.  Its opcode/push-flag shape is identical for
# the 25 normal profiles using the audited 447-instruction closure.
COMMON_ATTACK_SETUP_OPCODE_SHA256 = (
    "8B6682FFD423EC0C50373B5F7CB4C9320036F6BB6D83007188401FD846889EC4"
)

# The matching content dump contains three statically audited revisions of
# packed helper 0x3020.  Their surrounding feature branches differ, but all
# three retain the recovery contract used here: LOCAL1 is published to
# GLOBAL0 before the common lane-end transition path.  Do not accept a new
# shape until that dataflow has been audited as well.
AUDITED_ATTACK_SETUP_OPCODE_SHA256 = frozenset({
    COMMON_ATTACK_SETUP_OPCODE_SHA256,
    "542201C0FAF168F5EBE1C2F802757455315F62D82E58BFDDDA9F97059F0B5B96",
    "1161A41148D748772864C548B89E386AE5B36D65ACF6DE01878B8CBDA49F0E56",
})

# Export visits many official rows that share the same native reaction row.
# Cache immutable proof results per parsed KHD object and common-table digest;
# this avoids repeatedly emulating the same four standing-column wrappers.
_KHD_CACHE_IDENTITIES: dict[int, tuple[object, str]] = {}
_REACTION_ROOT_CACHE: dict[
    tuple[str, int, int | None],
    tuple[
        int,
        int,
        frozenset[int],
        frozenset[int],
        frozenset[int],
        frozenset[int],
        bool,
        frozenset[int],
    ] | None,
] = {}
_REACTION_ROW_CACHE: dict[
    tuple[str, str, int], NativeReactionRouteEvidence | None
] = {}
_REACTION_POPULATION_CACHE: dict[
    tuple[tuple[str, ...], str, int], NativeReactionRouteEvidence | None
] = {}
_REACTION_ROOT_PROFILE_CACHE: dict[
    tuple[str, int], tuple[object, ...] | None
] = {}
_REACTION_HELPER_PROFILE_CACHE: dict[str, tuple[object, ...]] = {}
_REACTION_MOTION_STATE_CACHE: dict[tuple[str, int], int | None] = {}


def _has_reaction_motion_state_contract(script: object) -> bool:
    """Prove the motion-setup helper's state-0 classifier dataflow.

    LOCAL[1] is MoveVM variable 0xF1 because bank-script arguments begin at
    variable 0xF0.  Both shipped motion-setup revisions copy that value to
    GLOBAL[0x51], call packed helper 0x3019 synchronously, preserve its return
    in temporary 0x101, and write that value to character-state slot 0 through
    native CALLCOND 0x14.  Match the dataflow itself instead of accepting a
    whole-script hash so unrelated character branches may differ safely.
    """

    instructions = script.instructions
    publications = [
        index
        for index in range(len(instructions) - 1)
        if (
            instructions[index].mnemonic == "LOAD_VAR"
            and instructions[index].imm_u16 == 0xF1
            and instructions[index].push_flag
            and instructions[index + 1].mnemonic == "STORE_VAR"
            and instructions[index + 1].imm_u16 == 0x51
        )
    ]
    classifier_writes: list[int] = []
    expected = (
        ("SET_ACC_U16", 0x3019, True, b"\x8b\x30\x19"),
        ("SET_ACC_U16", 0, True, b"\x8b\x00\x00"),
        ("SET_ACC_U16", 0, True, b"\x8b\x00\x00"),
        ("CALLCOND", None, True, b"\xa5\x0d\x03"),
        ("STORE_VAR", 0x101, False, b"\x19\x01\x01"),
        ("SET_ACC_U16", 0, True, b"\x8b\x00\x00"),
        ("LOAD_VAR", 0x101, True, b"\x8a\x01\x01"),
        ("CALLCOND", None, False, b"\x25\x14\x02"),
    )
    for start in range(len(instructions) - len(expected) + 1):
        window = instructions[start:start + len(expected)]
        if all(
            instruction.mnemonic == mnemonic
            and instruction.imm_u16 == immediate
            and instruction.push_flag == push_flag
            and instruction.raw == raw
            for instruction, (mnemonic, immediate, push_flag, raw)
            in zip(window, expected)
        ):
            classifier_writes.append(start)
    return (
        len(publications) == 1
        and len(classifier_writes) == 1
        and publications[0] < classifier_writes[0]
    )


def _khd_cache_identity(khd: object) -> str:
    object_id = id(khd)
    cached = _KHD_CACHE_IDENTITIES.get(object_id)
    if cached is not None and cached[0] is khd:
        return cached[1]
    identity = hashlib.sha256(khd.raw).hexdigest().upper()
    # Retain the object alongside its digest so CPython cannot recycle the id
    # for a later character bank while cached proof entries still exist.
    _KHD_CACHE_IDENTITIES[object_id] = (khd, identity)
    return identity


@dataclass(frozen=True)
class NativeReactionRouteEvidence:
    reaction_row_id: int
    raw_move_ids: tuple[int, ...]
    packed_move_ids: tuple[int, ...]
    resolved_slots: tuple[int, ...]
    driver_move_ids: tuple[int, ...]
    move_path_status: str
    outcome: str | None
    numeric_endpoint: bool
    must_latched_motion_flags: tuple[int, ...]
    may_latched_motion_flags: tuple[int, ...]
    terminal_motion_flags: tuple[int, ...]
    motion_state_codes: tuple[int, ...]
    positive_vertical_effect: bool
    defender_profile_count: int = 1
    defender_profiles_confirmed: int = 1
    defender_static_profile_count: int = 1


@dataclass(frozen=True)
class NativeFrameAdvantageEvidence:
    attack_slot: int
    attack_cell: int
    total_frames: int
    cell_window_start_coordinate: int
    cell_window_end_coordinate: int
    recovery_lead: int
    recovery_open_coordinate: int
    inclusive_recovery_frames: int
    block_stun_frames: int
    hit_stun_frames: int | None
    counter_hit_stun_frames: int | None
    block_advantage: int
    hit_advantage: int | None
    counter_hit_advantage: int | None
    hit_reaction: NativeReactionRouteEvidence | None
    counter_hit_reaction: NativeReactionRouteEvidence | None
    hit_outcome: str | None
    counter_hit_outcome: str | None
    resolutions: tuple[str, ...]


@dataclass(frozen=True)
class NativeThrowBreakEvidence:
    attack_slot: int
    attack_cell: int
    total_frames: int
    cell_window_start_coordinate: int
    cell_window_end_coordinate: int
    recovery_lead: int
    recovery_open_coordinate: int
    inclusive_recovery_frames: int
    break_stun_frames: int
    break_advantage: int
    resolutions: tuple[str, ...]


def _opcode_shape_sha256(script: object) -> str:
    signature = bytes(
        instruction.opcode | (0x80 if instruction.push_flag else 0)
        for instruction in script.instructions
    )
    return hashlib.sha256(signature).hexdigest().upper()


def _script_bytes_sha256(script: object) -> str:
    return hashlib.sha256(
        b"".join(instruction.raw for instruction in script.instructions)
    ).hexdigest().upper()


def _resolve_reaction_motion_state_code(
    khd: object,
    motion_setup_script: object,
    motion_id: int,
) -> int | None:
    """Execute the shipped 0x3019 motion-id classifier fail-closed.

    Motion setup helper 0x3114 stores its concrete reaction motion id in
    MoveVM global 0x51, then calls packed helper 0x3019 and publishes that
    helper's return value to chara-state slot 0.  Direct table cases require
    only the independently documented 0x09/0x0A flag-latch handlers; any
    context-dependent CALLCOND raises and remains unknown.
    """

    if not _has_reaction_motion_state_contract(motion_setup_script):
        return None
    cache_key = (_khd_cache_identity(khd), int(motion_id))
    if cache_key in _REACTION_MOTION_STATE_CACHE:
        return _REACTION_MOTION_STATE_CACHE[cache_key]
    slot = khd.resolve_packed_slot(REACTION_STATE_CLASSIFIER_HELPER)
    script = (
        khd.slots[slot].bytecode
        if slot is not None and 0 <= slot < len(khd.slots)
        else None
    )
    if script is None:
        _REACTION_MOTION_STATE_CACHE[cache_key] = None
        return None
    globals_frame = [0] * 0xF0
    globals_frame[0x51] = int(motion_id) & 0xFFFF
    no_result = lambda _context, _arguments: CallCondResult(0)
    context = MoveVMContext(
        globals=globals_frame,
        locals=[0] * 16,
        handlers={0x09: no_result, 0x0A: no_result},
    )
    try:
        result = MoveVMReference().execute(script, context)
        context.coverage.require_complete()
        state_code = int(result.return_value)
    except (StaticResolutionError, IndexError, ValueError, ZeroDivisionError):
        state_code = None
    _REACTION_MOTION_STATE_CACHE[cache_key] = state_code
    return state_code


def _reaction_row_profile_key(
    khd: object,
    reaction_table: LuxHitReactionMoveIdTable,
    reaction_row_id: int,
) -> tuple[object, ...] | None:
    """Fingerprint every script consumed by one reaction-row proof.

    Fighter banks ship many byte-for-byte identical copies of each reaction
    revision.  Grouping is safe only when all facing roots, their concrete
    driver calls, the driver bytecode, and every common helper used by the
    proof are identical.  This reduces roster consensus to one emulation per
    exact static profile without maintaining character-name allowlists.
    """

    row = reaction_table.row(reaction_row_id)
    if row is None:
        return None
    khd_identity = _khd_cache_identity(khd)
    # Published Hit/CH frame data uses the native standing-defender path.
    # ResolveGuardStanceForHitbox returns false for that state, selecting the
    # four-entry base column.  Alternate/crouched reactions are a separate
    # metric and must not be folded into or required by the standing result.
    parts: list[object] = [row.base_move_path_status]
    for raw_move_id in row.base_move_ids_by_facing:
        packed_move_id = raw_move_id & 0x7FFF
        root_cache_key = (khd_identity, packed_move_id)
        if root_cache_key in _REACTION_ROOT_PROFILE_CACHE:
            root_part = _REACTION_ROOT_PROFILE_CACHE[root_cache_key]
            if root_part is None:
                return None
            parts.append((raw_move_id, *root_part))
            continue
        root_slot = khd.resolve_packed_slot(packed_move_id)
        if root_slot is None or khd.slots[root_slot].bytecode is None:
            _REACTION_ROOT_PROFILE_CACHE[root_cache_key] = None
            return None
        root_script = khd.slots[root_slot].bytecode
        root_calls = [
            event
            for event in emulate(root_script, root_slot).bank_scripts
            if event.packed_move_id is not None
        ]
        driver_calls = [
            event
            for event in root_calls
            if event.packed_move_id not in REACTION_ROOT_AUXILIARY_HELPERS
        ]
        if (
            len(driver_calls) != 1
            or any(
                event.packed_move_id not in (
                    REACTION_ROOT_AUXILIARY_HELPERS
                    | {driver_calls[0].packed_move_id}
                )
                for event in root_calls
            )
        ):
            _REACTION_ROOT_PROFILE_CACHE[root_cache_key] = None
            return None
        driver_move_id = int(driver_calls[0].packed_move_id)
        driver_slot = khd.resolve_packed_slot(driver_move_id)
        if driver_slot is None or khd.slots[driver_slot].bytecode is None:
            _REACTION_ROOT_PROFILE_CACHE[root_cache_key] = None
            return None
        root_part = (
            _script_bytes_sha256(root_script),
            tuple(driver_calls[0].concrete_args),
            tuple(
                (event.packed_move_id, tuple(event.concrete_args))
                for event in root_calls
                if event.packed_move_id in REACTION_ROOT_AUXILIARY_HELPERS
            ),
            driver_move_id,
            _script_bytes_sha256(khd.slots[driver_slot].bytecode),
        )
        _REACTION_ROOT_PROFILE_CACHE[root_cache_key] = root_part
        parts.append((raw_move_id, *root_part))
    helper_profile = _REACTION_HELPER_PROFILE_CACHE.get(khd_identity)
    if helper_profile is None:
        helper_parts: list[object] = []
        for helper_move_id in (
            REACTION_STATE_CLASSIFIER_HELPER,
            REACTION_TICK_HELPER,
            *sorted(REACTION_ROOT_AUXILIARY_HELPERS),
            REACTION_FLAG_WINDOW_HELPER,
            *sorted(REACTION_STATE_HELPERS),
            *sorted(REACTION_MOTION_SETUP_HELPERS),
        ):
            helper_slot = khd.resolve_packed_slot(helper_move_id)
            helper_script = (
                khd.slots[helper_slot].bytecode
                if helper_slot is not None else None
            )
            helper_parts.append((
                helper_move_id,
                _script_bytes_sha256(helper_script)
                if helper_script is not None else None,
            ))
        helper_profile = tuple(helper_parts)
        _REACTION_HELPER_PROFILE_CACHE[khd_identity] = helper_profile
    parts.extend(helper_profile)
    return tuple(parts)


def _common_recovery_timeline(
    khd: object,
    attack_slot: int,
    attack_cell: int,
    *,
    allowed_cell_roles: frozenset[str],
) -> tuple[object, object, object, str, int, int, int, int] | None:
    """Resolve the shared 0x3020 attacker recovery convention."""

    if not (0 <= attack_slot < len(khd.slots)) or not khd.sections:
        return None
    cells = khd.sections[0].entries
    if not 0 <= attack_cell < len(cells):
        return None
    slot = khd.slots[attack_slot]
    cell = cells[attack_cell]
    if cell.cell_role not in allowed_cell_roles or slot.bytecode is None:
        return None

    helper_slot = khd.resolve_packed_slot(ATTACK_SETUP_HELPER)
    if helper_slot is None or khd.slots[helper_slot].bytecode is None:
        return None
    helper_script = khd.slots[helper_slot].bytecode
    helper_shape = _opcode_shape_sha256(helper_script)
    if helper_shape not in AUDITED_ATTACK_SETUP_OPCODE_SHA256:
        return None

    calls = [
        event
        for event in emulate(slot.bytecode, attack_slot).bank_scripts
        if event.packed_move_id == ATTACK_SETUP_HELPER
    ]
    if len(calls) != 1:
        return None
    args = calls[0].concrete_args
    if len(args) < 3 or args[0] != ATTACK_SETUP_HELPER or args[2] is None:
        return None
    recovery_lead = int(args[2])
    total_frames = int(slot.wTotalFrames)
    cell_window_start = int(cell.wI16MasterWindowStart)
    cell_window_end = int(cell.wI16MasterWindowEnd)
    if (
        total_frames in (0xFFFE, 0xFFFF)
        or recovery_lead < 0
        or recovery_lead >= total_frames
        or cell_window_start < 0
        or cell_window_end < cell_window_start
    ):
        return None

    recovery_open = total_frames - recovery_lead - 1
    inclusive_recovery = recovery_open - cell_window_start + 1
    if inclusive_recovery <= 0:
        return None
    return (
        slot,
        cell,
        calls[0],
        helper_shape,
        recovery_lead,
        total_frames,
        recovery_open,
        inclusive_recovery,
    )


def _concrete_predicate_args(event: object) -> tuple[int | None, ...]:
    return tuple(getattr(arg, "value", None) for arg in event.args)


def _has_predicate(script_result: object, expected: tuple[int, ...]) -> bool:
    return any(
        _concrete_predicate_args(event) == expected
        for event in script_result.predicates
    )


def _evaluate_reaction_entry_predicate(
    function_index: int, args: tuple[object, ...]
) -> int | None:
    """Resolve lane lifecycle predicates in a synchronous reaction entry.

    A confirmed reaction root calls its driver synchronously from the active
    lane's primary entry script. Native ``LuxMoveVM_TransitionToMove`` sets
    lane+0x26 to one around that script call and clears lane+0x24 before entry.
    ``LuxMoveVM_EvaluateIfOpcode`` exposes those exact words as IF 0x0010 and
    IF 0x0009 respectively. Other predicates remain runtime-dependent.
    """

    if function_index not in {0x00, 0x01, 0x25}:
        return None
    concrete_args = tuple(getattr(arg, "value", None) for arg in args)
    if concrete_args == (0x10,):
        return 1
    if concrete_args == (0x09,):
        return 0
    return None


def _evaluate_reaction_tick_predicate(
    function_index: int, args: tuple[object, ...]
) -> int | None:
    """Resolve only the primary-entry lifecycle word on later executions."""

    if function_index not in {0x00, 0x01, 0x25}:
        return None
    concrete_args = tuple(getattr(arg, "value", None) for arg in args)
    if concrete_args == (0x10,):
        return 0
    return None


def _signed_word(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def _confirm_standard_reaction_root(
    khd: object,
    packed_move_id: int,
    forced_posture_predicate: int | None = None,
) -> tuple[
    int,
    int,
    frozenset[int],
    frozenset[int],
    frozenset[int],
    frozenset[int],
    bool,
    frozenset[int],
] | None:
    """Confirm a shipped ordinary hit-reaction wrapper and counter clock.

    This is deliberately narrow.  It accepts only roots that make one direct
    call to a driver with the audited entry/end/state-helper contract. Special
    launch, wall, ground, and bespoke character routes remain unknown.
    """

    cache_key = (
        _khd_cache_identity(khd),
        packed_move_id,
        forced_posture_predicate,
    )
    if cache_key in _REACTION_ROOT_CACHE:
        return _REACTION_ROOT_CACHE[cache_key]

    def finish(
        value: tuple[
            int,
            int,
            frozenset[int],
            frozenset[int],
            frozenset[int],
            frozenset[int],
            bool,
            frozenset[int],
        ] | None
    ) -> tuple[
        int,
        int,
        frozenset[int],
        frozenset[int],
        frozenset[int],
        frozenset[int],
        bool,
        frozenset[int],
    ] | None:
        _REACTION_ROOT_CACHE[cache_key] = value
        return value

    resolved_slot = khd.resolve_packed_slot(packed_move_id)
    if resolved_slot is None or not 0 <= resolved_slot < len(khd.slots):
        return finish(None)
    root_script = khd.slots[resolved_slot].bytecode
    if root_script is None:
        return finish(None)
    def evaluate_root_predicate(
        fn_idx: int, args: tuple[object, ...]
    ) -> int | None:
        concrete = tuple(
            value.value if isinstance(value, Concrete) else None
            for value in args
        )
        if fn_idx == 0x01 and concrete == (0x0BBE, 1):
            return forced_posture_predicate
        return None

    root_result = emulate(
        root_script,
        resolved_slot,
        callcond_evaluator=evaluate_root_predicate,
    )
    root_calls = [
        event
        for event in root_result.bank_scripts
        if event.packed_move_id is not None
    ]
    driver_calls = [
        event
        for event in root_calls
        if event.packed_move_id not in REACTION_ROOT_AUXILIARY_HELPERS
    ]
    if (
        len(driver_calls) != 1
        or any(
            event.packed_move_id not in (
                REACTION_ROOT_AUXILIARY_HELPERS
                | {driver_calls[0].packed_move_id}
            )
            for event in root_calls
        )
    ):
        return finish(None)
    for event in root_calls:
        if event.packed_move_id not in REACTION_NOOP_ROOT_AUXILIARY_HELPERS:
            continue
        auxiliary_slot = khd.resolve_packed_slot(int(event.packed_move_id))
        auxiliary_script = (
            khd.slots[auxiliary_slot].bytecode
            if auxiliary_slot is not None else None
        )
        if (
            auxiliary_script is None
            or _script_bytes_sha256(auxiliary_script)
            != REACTION_NOOP_ROOT_AUXILIARY_SHA256
        ):
            return finish(None)
    root_call_args = driver_calls[0].concrete_args
    if not root_call_args:
        return finish(None)
    if any(value is None for value in root_call_args[1:]):
        # A small shipped wrapper family selects only concrete left/right
        # posture literals through one native IF 0x0BBE predicate.  Analyze
        # both static branches and retain only facts true in both; do not let
        # the fixed-point join's Unknown erase a finite native choice.
        predicates = [
            event
            for event in root_result.predicates
            if event.callcond_idx == 0x01
            and tuple(value.as_int() for value in event.args) == (0x0BBE, 1)
        ]
        if forced_posture_predicate is not None or len(predicates) != 1:
            return finish(None)
        branches = tuple(
            _confirm_standard_reaction_root(khd, packed_move_id, branch)
            for branch in (0, 1)
        )
        if any(branch is None for branch in branches):
            return finish(None)
        proofs = tuple(branch for branch in branches if branch is not None)
        first = proofs[0]
        if any(
            proof[0] != first[0] or proof[1] != first[1]
            for proof in proofs[1:]
        ):
            return finish(None)
        return finish((
            first[0],
            first[1],
            frozenset.intersection(*(proof[2] for proof in proofs)),
            frozenset.union(*(proof[3] for proof in proofs)),
            frozenset.intersection(*(proof[4] for proof in proofs)),
            frozenset.union(*(proof[5] for proof in proofs)),
            all(proof[6] for proof in proofs),
            frozenset.union(*(proof[7] for proof in proofs)),
        ))
    driver_move_id = int(driver_calls[0].packed_move_id)
    driver_local_args = [
        Concrete(int(value)) for value in root_call_args[1:]
    ]

    driver_slot = khd.resolve_packed_slot(driver_move_id)
    tick_slot = khd.resolve_packed_slot(REACTION_TICK_HELPER)
    if None in (driver_slot, tick_slot):
        return finish(None)
    driver_script = khd.slots[driver_slot].bytecode
    tick_script = khd.slots[tick_slot].bytecode
    if None in (driver_script, tick_script):
        return finish(None)

    entry_result = emulate(
        driver_script,
        driver_slot,
        local_args=driver_local_args,
        callcond_evaluator=_evaluate_reaction_entry_predicate,
    )
    if not _has_predicate(entry_result, (0x10,)):
        return finish(None)
    # MoveVM global variables survive after the synchronous primary-entry
    # invocation. Carry only values that are concrete on every entry return
    # into the later per-frame execution; path-dependent variables stay
    # unknown by construction.
    driver_result = emulate(
        driver_script,
        driver_slot,
        local_args=driver_local_args,
        initial_variables=entry_result.must_final_variables,
        callcond_evaluator=_evaluate_reaction_tick_predicate,
    )
    if not _has_predicate(driver_result, (0x09,)):
        return finish(None)
    state_calls = [
        event for event in driver_result.bank_scripts
        if event.packed_move_id in REACTION_STATE_HELPERS
    ]
    if len(state_calls) != 1:
        return finish(None)
    state_slot = khd.resolve_packed_slot(int(state_calls[0].packed_move_id))
    if state_slot is None:
        return finish(None)
    state_script = khd.slots[state_slot].bytecode
    if state_script is None:
        return finish(None)

    state_result = emulate(state_script, state_slot)
    if REACTION_TICK_HELPER not in {
        event.packed_move_id for event in state_result.bank_scripts
    }:
        return finish(None)

    tick_result = emulate(tick_script, tick_slot)
    if not _has_predicate(tick_result, (0x0B, 0x60)):
        return finish(None)
    if not _has_predicate(tick_result, (0x1A, 0x7FFF, 1)):
        return finish(None)
    if not any(event.concrete_args == [0x12] for event in tick_result.effects):
        return finish(None)

    motion_setup_calls = [
        event
        for event in entry_result.bank_scripts
        if (
            event.packed_move_id in REACTION_MOTION_SETUP_HELPERS
            and len(event.concrete_args) >= 9
        )
    ]
    if len(motion_setup_calls) != 1:
        return finish(None)
    motion_args = motion_setup_calls[0].concrete_args
    if (
        not motion_args
        or motion_args[0] not in REACTION_MOTION_SETUP_HELPERS
        or any(value is None for value in motion_args[1:])
    ):
        return finish(None)
    if len(motion_args) < 3:
        return finish(None)
    motion_setup_slot = khd.resolve_packed_slot(int(motion_args[0]))
    if motion_setup_slot is None:
        return finish(None)
    motion_setup_script = khd.slots[motion_setup_slot].bytecode
    if motion_setup_script is None:
        return finish(None)
    motion_state_code = _resolve_reaction_motion_state_code(
        khd,
        motion_setup_script,
        int(motion_args[2]),
    )
    motion_result = emulate(
        motion_setup_script,
        motion_setup_slot,
        local_args=[Concrete(int(value)) for value in motion_args[1:]],
    )
    must_reaction_flags = (
        motion_result.must_latched_motion_flags
        & REACTION_AGGREGATE_SOURCE_FLAGS
    )
    may_reaction_flags = (
        motion_result.may_latched_motion_flags
        & REACTION_AGGREGATE_SOURCE_FLAGS
    )

    flag_window_slot = khd.resolve_packed_slot(REACTION_FLAG_WINDOW_HELPER)
    if flag_window_slot is None:
        return finish(None)
    flag_window_script = khd.slots[flag_window_slot].bytecode
    if (
        flag_window_script is None
        or _opcode_shape_sha256(flag_window_script)
        != REACTION_FLAG_WINDOW_OPCODE_SHA256
    ):
        return finish(None)
    terminal_flags: set[int] = set()
    for event in driver_result.bank_scripts:
        if event.packed_move_id != REACTION_FLAG_WINDOW_HELPER:
            continue
        args = event.concrete_args
        if len(args) < 4 or any(value is None for value in args[1:4]):
            return finish(None)
        flag_index, _window_start, window_end = map(int, args[1:4])
        if window_end == 0x7FFF:
            terminal_flags.add(flag_index)
    positive_vertical_effect = any(
        len(args) >= 2
        and args[0] in {0x22, 0x23}
        and args[1] is not None
        and _signed_word(int(args[1])) > 0
        for event in driver_result.effects
        for args in (event.concrete_args,)
    )
    terminal_reaction_move_ids = frozenset(
        int(transition.next_move_id_raw) & 0x7FFF
        for transition in driver_result.transitions
        if transition.next_move_id_raw is not None
    )
    return finish((
        resolved_slot,
        driver_move_id,
        must_reaction_flags,
        may_reaction_flags,
        frozenset(terminal_flags),
        (
            frozenset({motion_state_code})
            if motion_state_code is not None else frozenset()
        ),
        positive_vertical_effect,
        terminal_reaction_move_ids,
    ))


def _reaction_proof_reaches_knockdown(
    khd: object,
    proof: tuple[
        int,
        int,
        frozenset[int],
        frozenset[int],
        frozenset[int],
        frozenset[int],
        bool,
        frozenset[int],
    ],
    visiting: frozenset[int] = frozenset(),
) -> bool:
    """Prove a reaction ends in the native state-6/7 KND family.

    Some promoted reactions use classifier state 11 only for an intermediate
    airborne animation, then author a terminal reaction move whose classifier
    is 6 or 7.  Follow only the driver's statically reachable authored targets;
    an absent, cyclic, ambiguous, or unclassified terminal fails closed.
    """

    state_codes = proof[5]
    if state_codes and state_codes <= {6, 7}:
        return True
    if not state_codes or state_codes - {6, 7, 11} or 11 not in state_codes:
        return False
    terminal_moves = proof[7]
    if not terminal_moves:
        return False
    for packed_move_id in terminal_moves:
        if packed_move_id in visiting:
            return False
        terminal_proof = _confirm_standard_reaction_root(khd, packed_move_id)
        if terminal_proof is None or not _reaction_proof_reaches_knockdown(
            khd,
            terminal_proof,
            visiting | {packed_move_id},
        ):
            return False
    return True


def _confirm_reaction_row(
    khd: object,
    reaction_table: LuxHitReactionMoveIdTable,
    reaction_row_id: int,
) -> NativeReactionRouteEvidence | None:
    cache_key = (_khd_cache_identity(khd), reaction_table.sha256, reaction_row_id)
    if cache_key in _REACTION_ROW_CACHE:
        return _REACTION_ROW_CACHE[cache_key]
    row = reaction_table.row(reaction_row_id)
    if row is None:
        _REACTION_ROW_CACHE[cache_key] = None
        return None
    # The lookup's Hit and Counter Hit metrics are standing-defender values.
    # Native ResolveGuardStanceForHitbox selects this base four-facing column;
    # alternate/crouched reactions are intentionally outside this metric.
    raw_move_ids = row.base_move_ids_by_facing
    # A facing-mixed control path cannot be selected without runtime facing.
    if row.base_move_path_status == "mixed":
        _REACTION_ROW_CACHE[cache_key] = None
        return None
    packed_move_ids = tuple(raw & 0x7FFF for raw in raw_move_ids)
    confirmed = [
        _confirm_standard_reaction_root(khd, packed)
        for packed in packed_move_ids
    ]
    if any(item is None for item in confirmed):
        _REACTION_ROW_CACHE[cache_key] = None
        return None
    proofs = tuple(item for item in confirmed if item is not None)
    initial_sets = tuple(item[2] for item in proofs)
    may_initial_sets = tuple(item[3] for item in proofs)
    terminal_sets = tuple(item[4] for item in proofs)
    motion_state_code_sets = tuple(item[5] for item in proofs)
    motion_state_codes = tuple(sorted(frozenset().union(*motion_state_code_sets)))
    vertical_by_facing = tuple(item[6] for item in proofs)
    terminal_reaction_sets = tuple(
        terminal & REACTION_AGGREGATE_SOURCE_FLAGS for terminal in terminal_sets
    )
    every_facing_launch = all(
        AIRBORNE_REACTION_FLAG in initial and vertical
        for initial, vertical in zip(initial_sets, vertical_by_facing)
    )
    every_facing_numeric_endpoint = all(
        ORDINARY_HITSTUN_FLAG in terminal
        or (
            not terminal
            and ORDINARY_HITSTUN_FLAG in initial
        )
        for initial, terminal in zip(initial_sets, terminal_reaction_sets)
    )
    if every_facing_launch:
        outcome = "LNC"
    elif all(
        _reaction_proof_reaches_knockdown(khd, proof)
        for proof in proofs
    ):
        # Native tutorial consumers independently identify state00=6 as the
        # knockdown-yarare endpoint and state00=7 as floor-slam/downed.
        outcome = "KND"
    else:
        outcome = None

    has_conditional_initial_subtype = any(
        possible - definite
        for definite, possible in zip(initial_sets, may_initial_sets)
    )
    numeric_endpoint = (
        outcome is None
        and every_facing_numeric_endpoint
        and not has_conditional_initial_subtype
        and row.base_move_path_status != "promoted"
    )
    shared_initial_flags = (
        frozenset.intersection(*initial_sets) if initial_sets else frozenset()
    )
    possible_initial_flags = frozenset().union(*may_initial_sets)
    shared_terminal_flags = (
        frozenset.intersection(*terminal_sets)
        if terminal_sets
        else frozenset()
    )
    evidence = NativeReactionRouteEvidence(
        reaction_row_id=reaction_row_id,
        raw_move_ids=raw_move_ids,
        packed_move_ids=packed_move_ids,
        resolved_slots=tuple(item[0] for item in proofs),
        driver_move_ids=tuple(item[1] for item in proofs),
        move_path_status=row.base_move_path_status,
        outcome=outcome,
        numeric_endpoint=numeric_endpoint,
        must_latched_motion_flags=tuple(sorted(shared_initial_flags)),
        may_latched_motion_flags=tuple(sorted(possible_initial_flags)),
        terminal_motion_flags=tuple(sorted(shared_terminal_flags)),
        motion_state_codes=motion_state_codes,
        positive_vertical_effect=all(vertical_by_facing),
    )
    _REACTION_ROW_CACHE[cache_key] = evidence
    return evidence


def _confirm_reaction_population(
    reaction_khds: tuple[object, ...],
    reaction_table: LuxHitReactionMoveIdTable,
    reaction_row_id: int,
) -> NativeReactionRouteEvidence | None:
    """Require one defender-independent interpretation across all KHD banks.

    Hit reaction MoveVM transitions execute on the defender.  A proof from the
    attacker's own KHD is therefore insufficient for roster-wide frame data.
    Numeric recovery is accepted only when every supplied playable defender
    proves a numeric endpoint; categorical outcomes likewise require the same
    proven outcome in every bank.  Mixed or incomplete populations fail
    closed while retaining no synthetic consensus.
    """

    if not reaction_khds:
        return None
    population_key = (
        tuple(_khd_cache_identity(khd) for khd in reaction_khds),
        reaction_table.sha256,
        reaction_row_id,
    )
    if population_key in _REACTION_POPULATION_CACHE:
        return _REACTION_POPULATION_CACHE[population_key]
    profiles: dict[tuple[object, ...], object] = {}
    for khd in reaction_khds:
        profile_key = _reaction_row_profile_key(
            khd, reaction_table, reaction_row_id
        )
        if profile_key is None:
            _REACTION_POPULATION_CACHE[population_key] = None
            return None
        profiles.setdefault(profile_key, khd)
    proofs = tuple(
        _confirm_reaction_row(khd, reaction_table, reaction_row_id)
        for khd in profiles.values()
    )
    if any(proof is None for proof in proofs):
        _REACTION_POPULATION_CACHE[population_key] = None
        return None
    confirmed = tuple(proof for proof in proofs if proof is not None)
    first = confirmed[0]
    if any(
        proof.raw_move_ids != first.raw_move_ids
        or proof.packed_move_ids != first.packed_move_ids
        or proof.move_path_status != first.move_path_status
        for proof in confirmed[1:]
    ):
        _REACTION_POPULATION_CACHE[population_key] = None
        return None

    outcomes = {proof.outcome for proof in confirmed}
    numeric_endpoint = all(proof.numeric_endpoint for proof in confirmed)
    outcome = next(iter(outcomes)) if len(outcomes) == 1 else None
    if outcome is None and not numeric_endpoint:
        _REACTION_POPULATION_CACHE[population_key] = None
        return None
    if outcome is not None and numeric_endpoint:
        _REACTION_POPULATION_CACHE[population_key] = None
        return None

    must_sets = tuple(frozenset(proof.must_latched_motion_flags) for proof in confirmed)
    terminal_sets = tuple(frozenset(proof.terminal_motion_flags) for proof in confirmed)
    evidence = NativeReactionRouteEvidence(
        reaction_row_id=reaction_row_id,
        raw_move_ids=first.raw_move_ids,
        packed_move_ids=first.packed_move_ids,
        # Slot numbers and driver ids are bank-relative.  Publish them only
        # when the exact tuple is shared by the entire defender population.
        resolved_slots=(
            first.resolved_slots
            if all(proof.resolved_slots == first.resolved_slots for proof in confirmed)
            else ()
        ),
        driver_move_ids=(
            first.driver_move_ids
            if all(proof.driver_move_ids == first.driver_move_ids for proof in confirmed)
            else ()
        ),
        move_path_status=first.move_path_status,
        outcome=outcome,
        numeric_endpoint=numeric_endpoint,
        must_latched_motion_flags=tuple(sorted(frozenset.intersection(*must_sets))),
        may_latched_motion_flags=tuple(sorted({
            flag
            for proof in confirmed
            for flag in proof.may_latched_motion_flags
        })),
        terminal_motion_flags=tuple(sorted(frozenset.intersection(*terminal_sets))),
        motion_state_codes=tuple(sorted({
            code
            for proof in confirmed
            for code in proof.motion_state_codes
            if code is not None
        })),
        positive_vertical_effect=all(
            proof.positive_vertical_effect for proof in confirmed
        ),
        defender_profile_count=len(reaction_khds),
        defender_profiles_confirmed=len(reaction_khds),
        defender_static_profile_count=len(profiles),
    )
    _REACTION_POPULATION_CACHE[population_key] = evidence
    return evidence


def analyze_confirmed_slot_frames(
    khd: object,
    attack_slot: int,
    attack_cell: int,
    reaction_table: LuxHitReactionMoveIdTable | None = None,
    reaction_khds: tuple[object, ...] | None = None,
) -> NativeFrameAdvantageEvidence | None:
    """Derive frame advantage when the complete common recovery profile fits.

    Returning ``None`` is the normal result for unaudited helper profiles,
    non-attack cells, invalid cells, multiple setup calls, or non-concrete
    timing args. Throw-break recovery uses the narrower dedicated analyzer.
    """

    timeline = _common_recovery_timeline(
        khd,
        attack_slot,
        attack_cell,
        allowed_cell_roles=frozenset({"Attack"}),
    )
    if timeline is None:
        return None
    slot, cell, call, helper_shape, recovery_lead, total_frames, recovery_open, inclusive_recovery = timeline
    cell_window_start = int(cell.wI16MasterWindowStart)
    cell_window_end = int(cell.wI16MasterWindowEnd)
    block_stun = int(cell.wI16BlockstunFrames)
    hit_reaction_row_id = int(cell.wI16ReactionIdBaseContact)
    counter_reaction_row_id = int(cell.wI16ReactionIdSpecialContact)
    hit_row = (
        reaction_table.row(hit_reaction_row_id)
        if reaction_table is not None
        else None
    )
    counter_row = (
        reaction_table.row(counter_reaction_row_id)
        if reaction_table is not None
        else None
    )
    hit_stun = int(
        cell.wI16HitstunAlternatePostureBaseContact
        if hit_row is not None and hit_row.base_move_path_status == "promoted"
        else cell.wI16HitstunBaseContact
    )
    # Primary decompile re-check: chara+0x132C >= 2 contributes to an
    # air/cinematic predicate. It is not a saved Counter-Hit selector. Thus
    # +0x48/+0x4E and reaction row +0x52 are air/cinematic routes and cannot
    # certify Counter-Hit advantage.
    special_contact_stun = int(
        cell.wI16HitstunAlternatePostureSpecialContact
        if counter_row is not None and counter_row.base_move_path_status == "promoted"
        else cell.wI16HitstunSpecialContact
    )
    if block_stun < 0:
        return None
    hit_reaction = (
        (
            _confirm_reaction_population(
                reaction_khds, reaction_table, hit_reaction_row_id
            )
            if reaction_khds is not None
            else _confirm_reaction_row(khd, reaction_table, hit_reaction_row_id)
        )
        if reaction_table is not None and hit_stun >= 0
        else None
    )
    hit_outcome = hit_reaction.outcome if hit_reaction is not None else None
    confirmed_hit_stun = (
        hit_stun
        if (
            hit_reaction is not None
            and hit_outcome is None
            and hit_reaction.numeric_endpoint
        )
        else None
    )
    hit_advantage = (
        confirmed_hit_stun - inclusive_recovery
        if confirmed_hit_stun is not None else None
    )
    # Negative signed values are native sentinels, not frame counts.  Keep
    # ordinary Block/Hit evidence when only the CH-selected column is unset.
    counter_hit_stun = None
    counter_hit_reaction = None
    counter_hit_outcome = None
    counter_hit_advantage = None
    resolutions = [
        f"khd-attacker-recovery:slot{attack_slot}"
        f"@0x{call.source_pc:X}->packed0x3020;LOCAL1={recovery_lead};"
        f"profile={helper_shape}",
        f"khd-attacker-actionable:slot{attack_slot};"
        f"0x7600(end{total_frames})-GLOBAL0({recovery_lead})-1="
        f"coordinate{recovery_open};inclusive{cell_window_start}..{recovery_open}="
        f"{inclusive_recovery}",
        f"khd-defender-stun:cell{attack_cell}[block+0x44={block_stun};"
        f"selected-hit={hit_stun}({'+0x4C' if hit_row is not None and hit_row.base_move_path_status == 'promoted' else '+0x46'};standing-base-reaction-column);"
        f"selected-air-cinematic={special_contact_stun}"
        f"({'+0x4E' if counter_row is not None and counter_row.base_move_path_status == 'promoted' else '+0x48'};not-counter-hit);"
        "ApplyHitReactionMove->counter+0x1364;"
        "EffectOp0x12->delta+0x1370;TickMain->clamped-subtract]",
        f"native-advantage:block={block_stun}-{inclusive_recovery}="
        f"{block_stun - inclusive_recovery}",
    ]
    if hit_outcome is not None and hit_reaction is not None:
        resolutions.append(
            f"native-hit-outcome:{hit_outcome};yarare-row"
            f"{hit_reaction.reaction_row_id};initial-reaction-flags="
            + ",".join(f"0x{flag:02X}" for flag in hit_reaction.must_latched_motion_flags)
            + ";terminal-reaction-flags="
            + ",".join(f"0x{flag:02X}" for flag in hit_reaction.terminal_motion_flags)
            + ";motion-state-codes="
            + ",".join(str(code) for code in hit_reaction.motion_state_codes)
            + f";positive-vertical-effect={int(hit_reaction.positive_vertical_effect)}"
        )
    elif hit_reaction is not None and hit_advantage is not None:
        resolutions.extend((
            f"native-hit-reaction:yarare-row{hit_reaction.reaction_row_id};"
            f"raw={','.join(f'0x{x:04X}' for x in hit_reaction.raw_move_ids)};"
            f"slots={','.join(str(x) for x in hit_reaction.resolved_slots)};"
            "standard-driver->0x3115->0x305E;IF0x0B[0x60];Effect0x12;"
            "IF0x1A[0x7FFF,1]",
            f"native-advantage:hit={confirmed_hit_stun}-{inclusive_recovery}="
            f"{hit_advantage}",
        ))
    else:
        resolutions.append("native-advantage:hit=unknown;reaction-endpoint-unproven")
    resolutions.append(
        "native-counter-advantage:unknown;"
        "counter-hit-does-not-select-air-cinematic-column"
    )
    return NativeFrameAdvantageEvidence(
        attack_slot=attack_slot,
        attack_cell=attack_cell,
        total_frames=total_frames,
        cell_window_start_coordinate=cell_window_start,
        cell_window_end_coordinate=cell_window_end,
        recovery_lead=recovery_lead,
        recovery_open_coordinate=recovery_open,
        inclusive_recovery_frames=inclusive_recovery,
        block_stun_frames=block_stun,
        hit_stun_frames=confirmed_hit_stun,
        counter_hit_stun_frames=counter_hit_stun,
        block_advantage=block_stun - inclusive_recovery,
        hit_advantage=hit_advantage,
        counter_hit_advantage=counter_hit_advantage,
        hit_reaction=hit_reaction,
        counter_hit_reaction=counter_hit_reaction,
        hit_outcome=hit_outcome,
        counter_hit_outcome=counter_hit_outcome,
        resolutions=tuple(resolutions),
    )


def analyze_throw_break_frames(
    khd: object,
    attack_slot: int,
    attack_cell: int,
) -> NativeThrowBreakEvidence | None:
    """Derive recovery after a block/break of an authored grab attempt.

    The caller must separately prove that the official row is authored with
    the game's TH effect tag.  This function deliberately accepts only a
    non-damaging native cell, preventing strikes that merely lead into a throw
    from being re-labelled as throws.
    """

    timeline = _common_recovery_timeline(
        khd,
        attack_slot,
        attack_cell,
        allowed_cell_roles=frozenset({"NonDamaging"}),
    )
    if timeline is None:
        return None
    slot, cell, call, helper_shape, recovery_lead, total_frames, recovery_open, inclusive_recovery = timeline
    cell_window_start = int(cell.wI16MasterWindowStart)
    cell_window_end = int(cell.wI16MasterWindowEnd)
    break_stun = int(cell.wI16BlockstunFrames)
    if break_stun < 0:
        return None
    break_advantage = break_stun - inclusive_recovery
    return NativeThrowBreakEvidence(
        attack_slot=attack_slot,
        attack_cell=attack_cell,
        total_frames=total_frames,
        cell_window_start_coordinate=cell_window_start,
        cell_window_end_coordinate=cell_window_end,
        recovery_lead=recovery_lead,
        recovery_open_coordinate=recovery_open,
        inclusive_recovery_frames=inclusive_recovery,
        break_stun_frames=break_stun,
        break_advantage=break_advantage,
        resolutions=(
            f"khd-throw-break-attacker-recovery:slot{attack_slot}"
            f"@0x{call.source_pc:X}->packed0x3020;LOCAL1={recovery_lead};"
            f"profile={helper_shape}",
            f"khd-throw-break-attacker-actionable:slot{attack_slot};"
            f"end{total_frames}-lead{recovery_lead}-1=coordinate{recovery_open};"
            f"inclusive{cell_window_start}..{recovery_open}={inclusive_recovery}",
            f"khd-throw-break-defender-stun:cell{attack_cell}[block+0x44={break_stun}]",
            f"native-throw-break-advantage:{break_stun}-{inclusive_recovery}={break_advantage}",
        ),
    )
