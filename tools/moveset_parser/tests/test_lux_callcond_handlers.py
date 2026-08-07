from __future__ import annotations

import pytest

from lux_callcond_handlers import (
    MoveVMCallCondState,
    MoveVMLaneSchedulerState,
    clear_motion_input_flag_0a,
    clear_pending_transition_1a,
    commit_lane1_end_10,
    evaluate_timing_25,
    get_rand_weighted_index_23,
    execute_bank_slot_script_0d,
    author_transition_06,
    author_transition_07,
    latch_motion_input_flag_09,
    register_scheduled_effect_19,
    schedule_transition_script_15,
    verified_callcond_handlers,
    write_chara_state_short_14,
    write_indexed_float_params_0c,
)
from lux_chara_state_shorts import (
    MOVEVM_CHARA_STATE_SHORT_COUNT,
    LuxCharaStateShortBank,
)
from lux_gameplay_rng import Xorshift96GameplayState
from lux_motion_input_flags import LuxMotionInputState
from lux_indexed_float_params import LuxIndexedFloatParamBanks
from lux_lane_lifecycle import MoveVMCharacterLifecycleState
from lux_scheduled_effects import LuxScheduledEffectTable
from lux_transition_author import MoveVMTransitionAuthorState
from lux_reference_engine import MoveVMContext, MoveVMReference, StaticResolutionError
from stackvm import walk_stackvm


def test_callcond_1a_clears_only_target_and_commit_fields() -> None:
    lane = MoveVMLaneSchedulerState(
        deferred_transition_target_move_id=0x1234,
        deferred_transition_destination_lane_index=7,
        deferred_transition_commit_flag=9,
    )
    context = MoveVMContext()

    result = clear_pending_transition_1a(
        MoveVMCallCondState(active_lane=lane), context, (0xAAAA, 0xBBBB)
    )

    assert result.value == 1
    assert not result.break_execution
    assert lane.deferred_transition_target_move_id == 0xFFFF
    assert lane.deferred_transition_destination_lane_index == 7
    assert lane.deferred_transition_commit_flag == 0
    assert context.coverage.resolved_functions == {
        "LuxMoveVM_CallCond_ClearPendingTransition_1A@0x1402FCDC0"
    }


def test_callcond_1a_fails_closed_without_native_active_lane() -> None:
    with pytest.raises(StaticResolutionError, match="null active MoveVM lane"):
        clear_pending_transition_1a(MoveVMCallCondState(), MoveVMContext(), ())


def test_callcond_1a_runs_through_instruction_faithful_vm() -> None:
    lane = MoveVMLaneSchedulerState(
        deferred_transition_target_move_id=3,
        deferred_transition_commit_flag=1,
    )
    context = MoveVMContext(
        handlers=verified_callcond_handlers(MoveVMCallCondState(active_lane=lane))
    )
    # CALLCOND+PUSH fn=0x1A argc=0; POP_RET.
    result = MoveVMReference().execute(
        walk_stackvm(bytes((0xA5, 0x1A, 0x00, 0x05)), 0), context
    )

    assert result.return_value == 1
    assert lane.deferred_transition_target_move_id == 0xFFFF
    assert lane.deferred_transition_commit_flag == 0
    assert context.call_log == [(0x1A, (), 1)]


def test_callcond_09_and_0a_share_verified_motion_flag_side_effects() -> None:
    motion = LuxMotionInputState()
    state = MoveVMCallCondState(motion_input=motion)
    context = MoveVMContext()

    assert latch_motion_input_flag_09(state, context, (0x25,)).value == 1
    assert motion.flags[0x25] == 1
    assert motion.flags[0x0B] == 1

    assert clear_motion_input_flag_0a(state, context, (0x25,)).value == 1
    assert motion.flags[0x25] == 0
    assert motion.flags[0x0B] == 0


@pytest.mark.parametrize(
    ("function_index", "handler"),
    [(0x09, latch_motion_input_flag_09), (0x0A, clear_motion_input_flag_0a)],
)
def test_motion_flag_callconds_require_argument_zero_and_state(
    function_index: int, handler
) -> None:
    with pytest.raises(StaticResolutionError, match="requires Lux current"):
        handler(MoveVMCallCondState(), MoveVMContext(), (1,))
    with pytest.raises(
        StaticResolutionError,
        match=f"CALLCOND 0x{function_index:02X} dereferences argument word zero",
    ):
        handler(
            MoveVMCallCondState(motion_input=LuxMotionInputState()),
            MoveVMContext(),
            (),
        )


def test_callcond_09_executes_through_vm() -> None:
    motion = LuxMotionInputState()
    context = MoveVMContext(
        handlers=verified_callcond_handlers(
            MoveVMCallCondState(motion_input=motion)
        )
    )
    result = MoveVMReference().execute(
        walk_stackvm(bytes((0x89, 0x00, 0x35, 0xA5, 0x09, 0x01, 0x05)), 0),
        context,
    )
    assert result.return_value == 1
    assert motion.flags[0x35] == 1
    assert motion.flags[0x0B] == 1


def test_transition_callconds_require_transition_state() -> None:
    with pytest.raises(StaticResolutionError, match="CALLCOND 0x06 requires"):
        author_transition_06(MoveVMCallCondState(), MoveVMContext(), (1,))
    with pytest.raises(StaticResolutionError, match="CALLCOND 0x07 requires"):
        author_transition_07(MoveVMCallCondState(), MoveVMContext(), (1,))


def test_callcond_07_executes_through_vm_and_returns_decoder_zero() -> None:
    lane0 = MoveVMLaneSchedulerState(lane_index=0)
    lane1 = MoveVMLaneSchedulerState(lane_index=1)
    transition = MoveVMTransitionAuthorState(
        lanes=(lane0, lane1), active_lane_index=0
    )
    context = MoveVMContext(
        handlers=verified_callcond_handlers(
            MoveVMCallCondState(transition_author=transition)
        )
    )
    result = MoveVMReference().execute(
        walk_stackvm(bytes((0x89, 0x12, 0x34, 0xA5, 0x07, 0x01, 0x05)), 0),
        context,
    )
    assert result.return_value == 0
    assert lane1.queued_target_move_id == 0x1234
    assert context.call_log == [(0x07, (0x1234,), 0)]


def test_callcond_23_matches_native_weighted_float32_order() -> None:
    rng = Xorshift96GameplayState(0x12345678, 0x9ABCDEF0, 0x0FEDCBA9)
    context = MoveVMContext()

    result = get_rand_weighted_index_23(
        MoveVMCallCondState(gameplay_rng=rng), context, (9,)
    )

    assert result.value == 7
    assert rng.tuple == (0xE08FDBCE, 0xDEF7827D, 0xF2265066)
    assert context.coverage.resolved_functions == {
        "LuxMoveVM_GetRandXorshift96Gameplay@0x14034F1F0",
        "LuxMoveVM_GetRandWeightedIndex@0x1402E58B0",
    }


@pytest.mark.parametrize(
    ("operand", "expected"),
    [
        (-32768, 0xFFFFA1F8),
        (-1, 0),
        (0, 0),
        (1, 1),
        (3, 2),
        (32767, 24073),
    ],
)
def test_callcond_23_signed_operand_boundaries(operand: int, expected: int) -> None:
    result = get_rand_weighted_index_23(
        MoveVMCallCondState(
            gameplay_rng=Xorshift96GameplayState(
                0x12345678, 0x9ABCDEF0, 0x0FEDCBA9
            )
        ),
        MoveVMContext(),
        (operand,),
    )
    assert result.value == expected


def test_callcond_23_non_single_argument_form_returns_raw_draw() -> None:
    state = MoveVMCallCondState(
        gameplay_rng=Xorshift96GameplayState(0x12345678, 0x9ABCDEF0, 0x0FEDCBA9)
    )
    assert get_rand_weighted_index_23(state, MoveVMContext(), ()).value == 0xCC5E09D5


def test_callcond_23_fails_closed_without_shared_rng_state() -> None:
    with pytest.raises(StaticResolutionError, match="shared gameplay xorshift96"):
        get_rand_weighted_index_23(MoveVMCallCondState(), MoveVMContext(), (9,))


def test_callcond_23_runs_through_instruction_faithful_vm_and_narrows_to_i16() -> None:
    state = MoveVMCallCondState(
        gameplay_rng=Xorshift96GameplayState(0x12345678, 0x9ABCDEF0, 0x0FEDCBA9)
    )
    context = MoveVMContext(handlers=verified_callcond_handlers(state))
    # SET_ACC 9 + PUSH; CALLCOND+PUSH fn=0x23 argc=1; POP_RET.
    result = MoveVMReference().execute(
        walk_stackvm(bytes((0x89, 0x00, 0x09, 0xA5, 0x23, 0x01, 0x05)), 0), context
    )

    assert result.return_value == 7
    assert context.call_log == [(0x23, (9,), 7)]


def test_callcond_25_runs_exact_timing_adapter_through_vm() -> None:
    lane = MoveVMLaneSchedulerState(
        lane_index=0,
        animation_frame_current=7.0,
        animation_frame_delta_this_tick=3,
    )
    transition = MoveVMTransitionAuthorState(lanes=(lane,), active_lane_index=0)
    context = MoveVMContext(
        handlers=verified_callcond_handlers(
            MoveVMCallCondState(transition_author=transition)
        )
    )
    bytecode = bytes(
        (0x89, 0x00, 0x05, 0x89, 0x00, 0x06, 0xA5, 0x25, 0x02, 0x05)
    )
    result = MoveVMReference().execute(walk_stackvm(bytecode, 0), context)

    assert result.return_value == 1
    assert context.call_log == [(0x25, (5, 6), 1)]
    assert "LuxMoveVM_CheckFrameInTimingWindow@0x1403012B0" in (
        context.coverage.resolved_functions
    )


def test_callcond_25_fails_closed_without_timing_state() -> None:
    with pytest.raises(StaticResolutionError, match="CALLCOND 0x25 requires"):
        evaluate_timing_25(MoveVMCallCondState(), MoveVMContext(), (3,))


def test_callcond_19_registers_and_deduplicates_exact_payload() -> None:
    table = LuxScheduledEffectTable()
    state = MoveVMCallCondState(scheduled_effects=table)
    context = MoveVMContext()

    assert register_scheduled_effect_19(state, context, (10, 0x44, -2)).value == 1
    assert register_scheduled_effect_19(state, context, (10, 0x44, -2)).value == 0
    assert table.entries[0].trigger_frame == 10
    assert table.entries[0].payload_words[:2] == [0x44, -2]
    assert context.coverage.resolved_functions == {
        "LuxMoveVM_CallCond_RegisterScheduledEffectOp@0x1402FD4A0"
    }


def test_callcond_19_fails_closed_without_table_or_trigger() -> None:
    with pytest.raises(StaticResolutionError, match="scheduled-effect table"):
        register_scheduled_effect_19(MoveVMCallCondState(), MoveVMContext(), (1, 2))
    with pytest.raises(StaticResolutionError, match="trigger argument word zero"):
        register_scheduled_effect_19(
            MoveVMCallCondState(scheduled_effects=LuxScheduledEffectTable()),
            MoveVMContext(),
            (),
        )


def test_callcond_19_runs_through_instruction_faithful_vm() -> None:
    table = LuxScheduledEffectTable()
    context = MoveVMContext(
        handlers=verified_callcond_handlers(
            MoveVMCallCondState(scheduled_effects=table)
        )
    )
    bytecode = bytes(
        (0x89, 0x00, 0x0C, 0x89, 0x12, 0x34, 0xA5, 0x19, 0x02, 0x05)
    )
    result = MoveVMReference().execute(walk_stackvm(bytecode, 0), context)

    assert result.return_value == 1
    assert context.call_log == [(0x19, (12, 0x1234), 1)]
    assert table.entries[0].payload_words[0] == 0x1234


def test_callcond_10_commits_lane1_and_breaks_when_it_is_vm_active() -> None:
    lane0 = MoveVMLaneSchedulerState(lane_index=0)
    lane1 = MoveVMLaneSchedulerState(lane_index=1, current_move_id=4)
    lifecycle = MoveVMCharacterLifecycleState(
        lanes=(lane0, lane1),
        active_vm_lane_index=1,
        secondary_script_executor=lambda state, lane: None,
    )
    context = MoveVMContext(
        handlers=verified_callcond_handlers(
            MoveVMCallCondState(lifecycle=lifecycle)
        )
    )
    result = MoveVMReference().execute(
        walk_stackvm(bytes((0xA5, 0x10, 0x00, 0x05)), 0), context
    )

    assert result.return_value == 1
    assert result.broke
    assert lane1.current_move_id == 0xFFFF
    assert lifecycle.vm_break_flag == -1


def test_callcond_10_requires_lifecycle_and_reviewed_argument_shape() -> None:
    with pytest.raises(StaticResolutionError, match="requires complete"):
        commit_lane1_end_10(MoveVMCallCondState(), MoveVMContext(), ())
    lifecycle = MoveVMCharacterLifecycleState(
        lanes=(MoveVMLaneSchedulerState(lane_index=1),),
        active_vm_lane_index=0,
    )
    with pytest.raises(StaticResolutionError, match="outside the reviewed"):
        commit_lane1_end_10(
            MoveVMCallCondState(lifecycle=lifecycle), MoveVMContext(), (3,)
        )


def test_callcond_14_writes_exact_signed_short_bank_boundaries() -> None:
    bank = LuxCharaStateShortBank()
    state = MoveVMCallCondState(chara_state_shorts=bank)
    context = MoveVMContext()

    assert len(bank.values) == MOVEVM_CHARA_STATE_SHORT_COUNT
    assert write_chara_state_short_14(state, context, (0, 0x8000)).value == 1
    assert write_chara_state_short_14(state, context, (73, 0x7FFF)).value == 1
    assert bank.values[0] == -32768
    assert bank.values[73] == 32767
    assert context.coverage.resolved_functions == {
        "LuxMoveVM_CallCond_WriteCharaStateShort_14@0x1402FDA30"
    }


@pytest.mark.parametrize("value", (-32768, -1, 0, 32767, 0xFFFF))
def test_chara_state_short_bank_preserves_native_i16(value: int) -> None:
    bank = LuxCharaStateShortBank()
    bank.write(17, value)
    expected = value & 0xFFFF
    if expected & 0x8000:
        expected -= 0x10000
    assert bank.values[17] == expected


@pytest.mark.parametrize("index", (-1, 74))
def test_callcond_14_fails_closed_outside_verified_bank(index: int) -> None:
    with pytest.raises(StaticResolutionError, match="outside 0..73"):
        write_chara_state_short_14(
            MoveVMCallCondState(chara_state_shorts=LuxCharaStateShortBank()),
            MoveVMContext(),
            (index, 3),
        )


def test_callcond_14_requires_bank_and_two_argument_words() -> None:
    with pytest.raises(StaticResolutionError, match="requires the current"):
        write_chara_state_short_14(MoveVMCallCondState(), MoveVMContext(), (0, 1))
    with pytest.raises(StaticResolutionError, match="words zero and one"):
        write_chara_state_short_14(
            MoveVMCallCondState(chara_state_shorts=LuxCharaStateShortBank()),
            MoveVMContext(),
            (0,),
        )


def test_callcond_14_runs_through_instruction_faithful_vm() -> None:
    bank = LuxCharaStateShortBank()
    context = MoveVMContext(
        handlers=verified_callcond_handlers(
            MoveVMCallCondState(chara_state_shorts=bank)
        )
    )
    # SET_ACC 73 + PUSH; SET_ACC -1 + PUSH; CALLCOND+PUSH 0x14/argc2; POP_RET.
    bytecode = bytes(
        (0x89, 0x00, 0x49, 0x89, 0xFF, 0xFF, 0xA5, 0x14, 0x02, 0x05)
    )
    result = MoveVMReference().execute(walk_stackvm(bytecode, 0), context)

    assert result.return_value == 1
    assert bank.values[73] == -1
    assert context.call_log == [(0x14, (73, -1), 1)]


def test_callcond_15_scopes_default_deferred_transition_around_nested_script() -> None:
    lane = MoveVMLaneSchedulerState(lane_index=0)
    transition = MoveVMTransitionAuthorState(lanes=(lane,), active_lane_index=0)
    observed: list[tuple[int, float, int]] = []

    def execute_nested(context: MoveVMContext, arguments: tuple[int, ...]):
        observed.append(
            (
                transition.deferred_schedule_flag,
                transition.deferred_schedule_frame,
                transition.deferred_commit_flag,
            )
        )
        context.coverage.resolved_functions.add(
            "LuxMoveVM_ExecuteBankSlotScript@0x1402FCC30"
        )
        from lux_reference_engine import CallCondResult
        return CallCondResult(7, True)

    state = MoveVMCallCondState(
        transition_author=transition,
        nested_bank_slot_executor=execute_nested,
    )
    context = MoveVMContext()
    result = schedule_transition_script_15(state, context, (0x1234,))

    assert (result.value, result.break_execution) == (7, True)
    assert observed == [(1, -1.0, 0)]
    assert transition.deferred_schedule_flag == 0
    assert context.coverage.resolved_functions == {
        "LuxMoveVM_CallCond_ScheduleTransitionScript_15@0x1402FCD30",
        "LuxMoveVM_ExecuteBankSlotScript@0x1402FCC30",
    }


def test_callcond_15_handles_commit_sentinel_and_mapped_timing_scope() -> None:
    lane = MoveVMLaneSchedulerState(lane_index=0)
    transition = MoveVMTransitionAuthorState(lanes=(lane,), active_lane_index=0)
    observed: list[tuple[float, int]] = []

    def execute_nested(context: MoveVMContext, arguments: tuple[int, ...]):
        observed.append(
            (
                transition.deferred_schedule_frame,
                transition.deferred_commit_flag,
            )
        )
        from lux_reference_engine import CallCondResult
        return CallCondResult(0)

    state = MoveVMCallCondState(
        transition_author=transition,
        nested_bank_slot_executor=execute_nested,
    )
    schedule_transition_script_15(state, MoveVMContext(), (0x1234, -2))
    schedule_transition_script_15(state, MoveVMContext(), (0x1234, 37))

    assert observed == [(-1.0, 1), (37.0, 0)]
    assert transition.deferred_schedule_flag == 0


def test_callcond_15_fails_closed_without_required_context() -> None:
    lane = MoveVMLaneSchedulerState(lane_index=0)
    transition = MoveVMTransitionAuthorState(lanes=(lane,), active_lane_index=0)
    with pytest.raises(StaticResolutionError, match="CALLCOND 0x15 requires"):
        schedule_transition_script_15(MoveVMCallCondState(), MoveVMContext(), (1,))
    with pytest.raises(StaticResolutionError, match="nested bank-slot executor"):
        schedule_transition_script_15(
            MoveVMCallCondState(transition_author=transition),
            MoveVMContext(),
            (1,),
        )


def test_callcond_15_clears_dynamic_scope_when_offline_nested_execution_fails() -> None:
    lane = MoveVMLaneSchedulerState(lane_index=0)
    transition = MoveVMTransitionAuthorState(lanes=(lane,), active_lane_index=0)

    def fail_nested(context: MoveVMContext, arguments: tuple[int, ...]):
        raise StaticResolutionError("nested failure")

    state = MoveVMCallCondState(
        transition_author=transition,
        nested_bank_slot_executor=fail_nested,
    )
    with pytest.raises(StaticResolutionError, match="nested failure"):
        schedule_transition_script_15(state, MoveVMContext(), (1,))
    assert transition.deferred_schedule_flag == 0


def test_callcond_0c_writes_fallback_or_weighted_bank_from_optional_operand() -> None:
    params = LuxIndexedFloatParamBanks()
    state = MoveVMCallCondState(indexed_float_params=params)

    assert write_indexed_float_params_0c(
        state, MoveVMContext(), (3, 0x3C00)
    ).value == 1
    assert params.weights[3] == -1.0
    assert params.fallback_values[3] == pytest.approx(0.01)

    write_indexed_float_params_0c(
        state, MoveVMContext(), (13, 0x4000, 0x5A40)
    )
    assert params.weights[13] == 1.0
    assert params.weighted_values[13] == pytest.approx(0.02)


@pytest.mark.parametrize("index", (-1, 14))
def test_callcond_0c_fails_closed_outside_proven_float_bank(index: int) -> None:
    with pytest.raises(StaticResolutionError, match="outside 0..13"):
        write_indexed_float_params_0c(
            MoveVMCallCondState(indexed_float_params=LuxIndexedFloatParamBanks()),
            MoveVMContext(),
            (index, 0),
        )


def test_callcond_0d_preserves_nested_arguments_result_and_break() -> None:
    observed: list[tuple[int, ...]] = []

    def execute_nested(context: MoveVMContext, arguments: tuple[int, ...]):
        observed.append(arguments)
        from lux_reference_engine import CallCondResult
        return CallCondResult(-7, True)

    state = MoveVMCallCondState(nested_bank_slot_executor=execute_nested)
    context = MoveVMContext()
    result = execute_bank_slot_script_0d(
        state, context, (0x1234, 1, 2, 3, 4, 5, 6, 7, 8, 9)
    )

    assert (result.value, result.break_execution) == (-7, True)
    assert observed == [(0x1234, 1, 2, 3, 4, 5, 6, 7, 8, 9)]
    assert context.coverage.resolved_functions == {
        "LuxMoveVM_ExecuteBankSlotScript@0x1402FCC30"
    }


def test_callcond_0d_fails_closed_without_executor_or_packed_move() -> None:
    with pytest.raises(StaticResolutionError, match="exact nested bank-slot executor"):
        execute_bank_slot_script_0d(MoveVMCallCondState(), MoveVMContext(), (1,))

    def execute_nested(context: MoveVMContext, arguments: tuple[int, ...]):
        from lux_reference_engine import CallCondResult
        return CallCondResult(0)

    with pytest.raises(StaticResolutionError, match="argument zero"):
        execute_bank_slot_script_0d(
            MoveVMCallCondState(nested_bank_slot_executor=execute_nested),
            MoveVMContext(),
            (),
        )
