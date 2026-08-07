"""Native-equivalent implementations of individually verified MoveVM CALLCONDs.

Handlers enter this module only after their native leaf/helper closure and the
complete authored argument-count domain have been checked. Unknown lane or
character identities fail closed instead of manufacturing state.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from lux_attack_cell_variant import (
    MoveVMAttackCellVariantState,
    set_active_move_slot_variant,
)
from lux_chara_state_shorts import LuxCharaStateShortBank
from lux_effect_dispatch_subset import LuxEffectDispatch02State, dispatch_effect_02
from lux_gameplay_rng import Xorshift96GameplayState, u32
from lux_indexed_float_params import LuxIndexedFloatParamBanks
from lux_lane_lifecycle import MoveVMCharacterLifecycleState, commit_move_end
from lux_motion_input_flags import LuxMotionInputState, set_motion_input_flag
from lux_numeric import cvttss2si, float32_from_bits, mul_f32, signed_low_i16
from lux_scheduled_effects import LuxScheduledEffectTable
from lux_transition_author import (
    MoveVMLaneSchedulerState,
    MoveVMTransitionAuthorState,
    author_lane0_transition_06,
    author_lane1_transition_07,
    evaluate_active_lane_timing,
    map_bank_slot_timing_index,
)
from lux_reference_engine import (
    CallCondHandler,
    CallCondResult,
    MoveVMContext,
    StaticResolutionError,
)


RAND_NORMALIZE_FACTOR_BITS = 0x34000000
RAND_WEIGHTED_INDEX_SCALE_BITS = 0x3F7FFF58


@dataclass
class MoveVMCallCondState:
    attack_cell_variant: MoveVMAttackCellVariantState | None = None
    active_lane: MoveVMLaneSchedulerState | None = None
    chara_state_shorts: LuxCharaStateShortBank | None = None
    effect_dispatch_02: LuxEffectDispatch02State | None = None
    gameplay_rng: Xorshift96GameplayState | None = None
    indexed_float_params: LuxIndexedFloatParamBanks | None = None
    lifecycle: MoveVMCharacterLifecycleState | None = None
    motion_input: LuxMotionInputState | None = None
    scheduled_effects: LuxScheduledEffectTable | None = None
    nested_bank_slot_executor: (
        Callable[[MoveVMContext, tuple[int, ...]], CallCondResult] | None
    ) = None
    transition_author: MoveVMTransitionAuthorState | None = None


def _require_transition_author(
    state: MoveVMCallCondState, function_index: int
) -> MoveVMTransitionAuthorState:
    transition = state.transition_author
    if transition is None:
        raise StaticResolutionError(
            f"native CALLCOND 0x{function_index:02X} requires MoveVM transition state"
        )
    return transition


def author_transition_06(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    transition = _require_transition_author(state, 0x06)
    result = author_lane0_transition_06(transition, arguments)
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_OpcodeIf_06_TransitionAuthor@0x1402FCB90",
            "LuxMoveVM_DecodeVariadicStreamArgs@0x1402FC930",
            "LuxMoveVM_MapBankSlotTimingIndex@0x1403002B0",
        }
    )
    return CallCondResult(result)


def author_transition_07(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    transition = _require_transition_author(state, 0x07)
    result = author_lane1_transition_07(transition, arguments)
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_OpcodeIf_07_TransitionAuthor@0x1402FCC10",
            "LuxMoveVM_DecodeVariadicStreamArgs@0x1402FC930",
            "LuxMoveVM_MapBankSlotTimingIndex@0x1403002B0",
        }
    )
    return CallCondResult(result)


def _require_first_argument(function_index: int, arguments: tuple[int, ...]) -> int:
    if not arguments:
        raise StaticResolutionError(
            f"native CALLCOND 0x{function_index:02X} dereferences argument word zero"
        )
    return arguments[0]


def dispatch_effect_subset_02(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    effect_state = state.effect_dispatch_02
    if effect_state is None:
        raise StaticResolutionError(
            "native CALLCOND 0x02 requires the bounded effect-dispatch state"
        )
    return dispatch_effect_02(effect_state, context, arguments)


def latch_motion_input_flag_09(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x09 at ``0x1402FD720``: set one current-state flag."""

    motion_input = state.motion_input
    if motion_input is None:
        raise StaticResolutionError(
            "native CALLCOND 0x09 requires Lux current motion-input state"
        )
    flag_index = _require_first_argument(0x09, arguments)
    set_motion_input_flag(
        motion_input, flag_index, 1, motion_input.active_lane_mask
    )
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_OpcodeIf_09_LatchCharaStateFlag@0x1402FD720",
            "LuxBattleChara_SetMotionInputFlag@0x140304C00",
        }
    )
    return CallCondResult(1)


def clear_motion_input_flag_0a(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x0A at ``0x1402FD7D0``: clear one current-state flag."""

    motion_input = state.motion_input
    if motion_input is None:
        raise StaticResolutionError(
            "native CALLCOND 0x0A requires Lux current motion-input state"
        )
    flag_index = _require_first_argument(0x0A, arguments)
    set_motion_input_flag(
        motion_input, flag_index, 0, motion_input.active_lane_mask
    )
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_CallCond_ClearMotionInputFlag_0A@0x1402FD7D0",
            "LuxBattleChara_SetMotionInputFlag@0x140304C00",
        }
    )
    return CallCondResult(1)


def execute_bank_slot_script_0d(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x0D / ``LuxMoveVM_ExecuteBankSlotScript @ 0x1402FCC30``."""

    executor = state.nested_bank_slot_executor
    if executor is None:
        raise StaticResolutionError(
            "native CALLCOND 0x0D requires an exact nested bank-slot executor"
        )
    if not arguments:
        raise StaticResolutionError(
            "native CALLCOND 0x0D dereferences nested packed move argument zero"
        )
    result = executor(context, arguments)
    context.coverage.resolved_functions.add(
        "LuxMoveVM_ExecuteBankSlotScript@0x1402FCC30"
    )
    return result


def write_indexed_float_params_0c(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x0C at ``0x1402FDA50``."""

    params = state.indexed_float_params
    if params is None:
        raise StaticResolutionError(
            "native CALLCOND 0x0C requires indexed MoveVM float parameter banks"
        )
    params.write_from_callcond(arguments)
    context.coverage.resolved_functions.add(
        "LuxMoveVM_CallCond_WriteIndexedFloatParams_0C@0x1402FDA50"
    )
    return CallCondResult(1)


def clear_pending_transition_1a(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x1A at ``0x1402FCDC0``.

    Native code ignores the operand count/stream, writes ``0xFFFF`` to active
    lane +0xB4, writes zero to +0xB8, and returns one. It deliberately leaves
    destination lane +0xB6 and timing fields untouched.
    """

    del arguments
    lane = state.active_lane
    if lane is None:
        raise StaticResolutionError(
            "native CALLCOND 0x1A would dereference a null active MoveVM lane"
        )
    lane.deferred_transition_target_move_id = 0xFFFF
    lane.deferred_transition_commit_flag = 0
    context.coverage.resolved_functions.add(
        "LuxMoveVM_CallCond_ClearPendingTransition_1A@0x1402FCDC0"
    )
    return CallCondResult(1)


def write_chara_state_short_14(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x14 at ``0x1402FDA30``.

    Native code reads argument words zero and one, then writes the second as
    a signed short at ``ALuxBattleChara + 0x197C + arguments[0] * 2``.
    """

    bank = state.chara_state_shorts
    if bank is None:
        raise StaticResolutionError(
            "native CALLCOND 0x14 requires the current character-state short bank"
        )
    if len(arguments) < 2:
        raise StaticResolutionError(
            "native CALLCOND 0x14 dereferences argument words zero and one"
        )
    bank.write(arguments[0], arguments[1])
    context.coverage.resolved_functions.add(
        "LuxMoveVM_CallCond_WriteCharaStateShort_14@0x1402FDA30"
    )
    return CallCondResult(1)


def schedule_transition_script_15(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x15 at ``0x1402FCD30``.

    This creates a synchronous dynamic scope consumed by transition-author
    CALLCONDs reached by the nested bank-slot script.  The native wrapper
    leaves the nested script's return value in AX and clears only the active
    scope flag after the call returns.
    """

    transition = _require_transition_author(state, 0x15)
    executor = state.nested_bank_slot_executor
    if executor is None:
        raise StaticResolutionError(
            "native CALLCOND 0x15 requires an exact nested bank-slot executor"
        )
    if not arguments:
        raise StaticResolutionError(
            "native CALLCOND 0x15 dereferences nested packed move argument zero"
        )

    transition.deferred_schedule_frame = -1.0
    transition.deferred_schedule_flag = 1
    transition.deferred_commit_flag = 0
    if len(arguments) > 1:
        if u32(arguments[1]) & 0xFFFF == 0xFFFE:
            transition.deferred_commit_flag = 1
        else:
            transition.deferred_schedule_frame = map_bank_slot_timing_index(
                transition, transition.active_lane, arguments[1]
            )

    context.coverage.resolved_functions.add(
        "LuxMoveVM_CallCond_ScheduleTransitionScript_15@0x1402FCD30"
    )
    if len(arguments) > 1 and (u32(arguments[1]) & 0xFFFF) != 0xFFFE:
        context.coverage.resolved_functions.add(
            "LuxMoveVM_MapBankSlotTimingIndex@0x1403002B0"
        )
    try:
        return execute_bank_slot_script_0d(state, context, arguments)
    finally:
        transition.deferred_schedule_flag = 0


def get_rand_weighted_index_23(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x23 at ``0x1402E58B0``.

    Exactly one argument selects weighted mode.  Every other argument count
    returns the raw draw.  The authored 28-fighter corpus uses only the
    one-argument form, but preserving the raw branch makes the lifted native
    contract complete.
    """

    rng = state.gameplay_rng
    if rng is None:
        raise StaticResolutionError(
            "native CALLCOND 0x23 requires the shared gameplay xorshift96 state"
        )

    random_value = rng.draw_u32()
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_GetRandXorshift96Gameplay@0x14034F1F0",
            "LuxMoveVM_GetRandWeightedIndex@0x1402E58B0",
        }
    )
    if len(arguments) != 1:
        return CallCondResult(random_value)

    # 0x1402E58D5..0x1402E58FA.  Each MULSS rounds independently.
    normalized_random = mul_f32(
        float(random_value & 0x7FFFFF),
        float32_from_bits(RAND_NORMALIZE_FACTOR_BITS),
    )
    weighted = mul_f32(float(arguments[0] + 1), normalized_random)
    weighted = mul_f32(
        weighted,
        float32_from_bits(RAND_WEIGHTED_INDEX_SCALE_BITS),
    )
    return CallCondResult(u32(cvttss2si(weighted)))


def evaluate_timing_25(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x25 adapter to the shared IF 0x0008 timing predicate."""

    transition = _require_transition_author(state, 0x25)
    result = evaluate_active_lane_timing(transition, arguments)
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_CallCond_EvaluateTiming_25@0x1402E5830",
            "LuxMoveVM_EvaluateIfOpcode@0x1403732F0",
            "LuxMoveVM_CheckFrameInTimingWindow@0x1403012B0",
            "LuxMoveVM_MapBankSlotTimingIndex@0x1403002B0",
        }
    )
    return CallCondResult(result)


def register_scheduled_effect_19(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x19: deduplicate/insert one active-lane effect trigger."""

    table = state.scheduled_effects
    if table is None:
        raise StaticResolutionError(
            "native CALLCOND 0x19 requires the active lane scheduled-effect table"
        )
    if not arguments:
        raise StaticResolutionError(
            "native CALLCOND 0x19 dereferences trigger argument word zero"
        )
    result = table.register(arguments)
    context.coverage.resolved_functions.add(
        "LuxMoveVM_CallCond_RegisterScheduledEffectOp@0x1402FD4A0"
    )
    return CallCondResult(result)


def commit_lane1_end_10(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """CALLCOND 0x10 for its complete authored zero-argument domain."""

    if arguments:
        raise StaticResolutionError(
            "CALLCOND 0x10 nonzero-argument scheduling is outside the reviewed corpus contract"
        )
    lifecycle = state.lifecycle
    if lifecycle is None:
        raise StaticResolutionError(
            "native CALLCOND 0x10 requires complete MoveVM lane lifecycle state"
        )
    commit_move_end(lifecycle, lifecycle.lane(1))
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_CallCond_10_CommitOrScheduleLane1@0x1402FD3E0",
            "LuxMoveVM_CallCond_CommitOrScheduleLaneEnd@0x1402FD190",
            "LuxMoveVM_CommitMoveEnd@0x1402FCFB0",
            "LuxMoveVM_RunSecondaryLaneScript@0x1402FE1C0",
            "LuxMoveVM_DeactivateLane@0x1402FDD00",
        }
    )
    return CallCondResult(1, lifecycle.vm_break_flag != 0)


def set_active_move_slot_variant_26(
    state: MoveVMCallCondState,
    context: MoveVMContext,
    arguments: tuple[int, ...],
) -> CallCondResult:
    """Lift CALLCOND 0x26 without certifying its unresolved classifier closure."""

    variant_state = state.attack_cell_variant
    if variant_state is None:
        raise StaticResolutionError(
            "native CALLCOND 0x26 requires complete active attack-cell state"
        )
    if len(arguments) != 1:
        raise StaticResolutionError(
            "native CALLCOND 0x26 dereferences exactly one variant word"
        )
    set_active_move_slot_variant(variant_state, signed_low_i16(arguments[0]))
    context.coverage.resolved_functions.update(
        {
            "LuxMoveVM_CallCond_SetActiveMoveSlotVariant_26@0x140300DF0",
            "LuxMoveVM_SetActiveMoveSlot@0x140300C70",
        }
    )
    return CallCondResult(0)


def verified_callcond_handlers(state: MoveVMCallCondState) -> dict[int, CallCondHandler]:
    """Return only handlers whose complete native contract is implemented."""

    def clear_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return clear_pending_transition_1a(state, context, arguments)

    def dispatch_effect_02_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return dispatch_effect_subset_02(state, context, arguments)

    def transition_06_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return author_transition_06(state, context, arguments)

    def transition_07_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return author_transition_07(state, context, arguments)

    def latch_motion_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return latch_motion_input_flag_09(state, context, arguments)

    def clear_motion_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return clear_motion_input_flag_0a(state, context, arguments)

    def write_indexed_float_params_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return write_indexed_float_params_0c(state, context, arguments)

    def execute_bank_slot_script_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return execute_bank_slot_script_0d(state, context, arguments)

    def weighted_random_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return get_rand_weighted_index_23(state, context, arguments)

    def write_chara_state_short_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return write_chara_state_short_14(state, context, arguments)

    def schedule_transition_script_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return schedule_transition_script_15(state, context, arguments)

    def evaluate_timing_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return evaluate_timing_25(state, context, arguments)

    def register_scheduled_effect_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return register_scheduled_effect_19(state, context, arguments)

    def commit_lane1_end_handler(
        context: MoveVMContext, arguments: tuple[int, ...]
    ) -> CallCondResult:
        return commit_lane1_end_10(state, context, arguments)

    return {
        0x02: dispatch_effect_02_handler,
        0x06: transition_06_handler,
        0x07: transition_07_handler,
        0x09: latch_motion_handler,
        0x0A: clear_motion_handler,
        0x0C: write_indexed_float_params_handler,
        0x0D: execute_bank_slot_script_handler,
        0x10: commit_lane1_end_handler,
        0x14: write_chara_state_short_handler,
        0x15: schedule_transition_script_handler,
        0x19: register_scheduled_effect_handler,
        0x1A: clear_handler,
        0x23: weighted_random_handler,
        0x25: evaluate_timing_handler,
    }
