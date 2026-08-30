bool DeterministicHookSet::IsOwnedPresentationVerification(
    const OuterTickCaptureContext* batch) noexcept
{
    return IsOwnedPresentationSuppressed(batch)
        && batch->owned->request->presentation_mode
            == OwnedBatchPresentationMode::VerifyRecorded;
}

namespace
{
void RecordUnresolvedAudioOwner(OuterTickObservation& observation,
    std::uintptr_t image_base, std::uintptr_t owner,
    std::uintptr_t return_address, const AudioOwnerResolver& resolver) noexcept
{
    if (observation.first_unresolved_audio_owner != 0) return;
    observation.first_unresolved_audio_owner = owner;
    observation.first_unresolved_audio_return_rva =
        return_address >= image_base ? return_address - image_base : 0;
    observation.audio_owner_graph_epoch = resolver.epoch();
    observation.audio_owner_graph_bindings =
        static_cast<std::uint32_t>(resolver.binding_count());
}
}

std::uint32_t __fastcall
DeterministicHookSet::GameplayXorshift96Detour() noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->gameplay_xorshift96_trampoline_
        : gameplay_xorshift96_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<GameplayXorshift96Fn>(trampoline);
    const auto return_address = reinterpret_cast<std::uintptr_t>(
        _ReturnAddress());
    const std::uint32_t result = original != nullptr ? original() : 0;

    auto* batch = active_outer_capture_;
    if (hooks != nullptr && batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto return_rva = return_address >= hooks->image_base_
            ? return_address - hooks->image_base_ : 0;
        ++observation.gameplay_xorshift_draws;
        auto hash = observation.gameplay_xorshift_sequence_hash == 0
            ? std::uint64_t{1469598103934665603ull}
            : observation.gameplay_xorshift_sequence_hash;
        const auto append = [&](const void* data, std::size_t size) noexcept {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
        };
        append(&return_rva, sizeof(return_rva));
        append(&result, sizeof(result));
        observation.gameplay_xorshift_sequence_hash = hash;

        const auto known = std::find(gameplay_xorshift96_return_rvas.begin(),
            gameplay_xorshift96_return_rvas.end(), return_rva);
        if (known == gameplay_xorshift96_return_rvas.end())
        {
            ++observation.gameplay_xorshift_unknown_callers;
        }
        else
        {
            observation.gameplay_xorshift_known_callers |=
                std::uint64_t{1} << std::distance(
                    gameplay_xorshift96_return_rvas.begin(), known);
        }

        const bool weighted =
            return_rva == gameplay_xorshift96_weighted_return_a
            || return_rva == gameplay_xorshift96_weighted_return_b;
        const bool probability_if =
            return_rva == gameplay_xorshift96_if_float_return;
        if (weighted) ++observation.gameplay_xorshift_weighted_draws;
        if (probability_if) ++observation.gameplay_xorshift_if_draws;

        std::uint32_t frame{};
        if (SafeRead(batch->frame_counter_address, frame)
            && frame >= observation.before.frame_counter)
        {
            const auto offset = frame - observation.before.frame_counter;
            if (offset <= Schema::maximum_supported_native_batch_width)
            {
                const auto bit = static_cast<std::uint16_t>(1u << offset);
                if (weighted)
                    observation.gameplay_xorshift_weighted_source_mask |= bit;
                if (probability_if)
                    observation.gameplay_xorshift_if_source_mask |= bit;
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

std::uint64_t __fastcall DeterministicHookSet::MoveVmEvaluateIfDetour(
    void* chara, std::int32_t argument_count,
    std::uint16_t* arguments) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->movevm_evaluate_if_trampoline_
        : movevm_evaluate_if_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<MoveVmEvaluateIfFn>(trampoline);

    auto* batch = active_outer_capture_;
    OuterTickObservation* observation = hooks != nullptr && batch != nullptr
        ? batch->observation : nullptr;
    std::uint16_t opcode{};
    std::uint16_t character_id{};
    const bool arguments_valid = argument_count > 0 && arguments != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(arguments), opcode);
    const bool owner_valid = chara != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(chara) + 0x24c,
            character_id);

    // A successful probability predicate is consumed only by its immediately
    // following authored transition for this owner. Any intervening IF opcode
    // invalidates an older pending join so unrelated same-frame work cannot be
    // credited as Tira RNG-transition coverage.
    if (owner_valid && tira_probability_join.pending
        && tira_probability_join.owner == chara)
    {
        tira_probability_join = {};
    }
    const bool candidate = observation != nullptr && arguments_valid
        && owner_valid && opcode == 0x007f && character_id == 0x23;
    const std::uint64_t draws_before = candidate
        ? observation->gameplay_xorshift_draws : 0;

    const std::uint64_t result = original != nullptr
        ? original(chara, argument_count, arguments) : 0;
    if (candidate)
    {
        if (original == nullptr
            || observation->gameplay_xorshift_draws != draws_before + 1)
        {
            ++observation->movevm_transition_07_signature_failures;
        }
        else if (result != 0)
        {
            std::uint32_t frame{};
            if (!SafeRead(batch->frame_counter_address, frame))
            {
                ++observation->movevm_transition_07_signature_failures;
            }
            else
            {
                tira_probability_join.owner = chara;
                tira_probability_join.batch_id = observation->batch_id;
                tira_probability_join.draw_count_after =
                    observation->gameplay_xorshift_draws;
                tira_probability_join.frame = frame;
                tira_probability_join.pending = true;
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void DeterministicHookSet::ObserveTiraRandomTransition(
    void* chara, std::uint16_t target, OuterTickCaptureContext& batch,
    OuterTickObservation& observation) noexcept
{
    std::uint32_t frame{};
    const bool join_owner = tira_probability_join.pending
        && tira_probability_join.owner == chara
        && tira_probability_join.batch_id == observation.batch_id;
    const bool exact_join = join_owner
        && SafeRead(batch.frame_counter_address, frame)
        && frame == tira_probability_join.frame
        && observation.gameplay_xorshift_draws
            == tira_probability_join.draw_count_after;
    if (join_owner) tira_probability_join = {};
    if (!exact_join)
    {
        if (join_owner) ++observation.movevm_transition_07_signature_failures;
        return;
    }
    if (target != 0x0153 && target != 0x0205) return;

    ++observation.tira_random_transition_calls;
    std::uint8_t fighter_slot_mask{};
    std::uint8_t fighter_slot{};
    if (!SafeRead(reinterpret_cast<std::uintptr_t>(chara) + 0x23c,
            fighter_slot) || fighter_slot > 1)
        ++observation.movevm_transition_07_signature_failures;
    else
    {
        fighter_slot_mask = static_cast<std::uint8_t>(1u << fighter_slot);
        std::uint16_t state19{};
        if (!SafeRead(reinterpret_cast<std::uintptr_t>(chara) + 0x19ae,
                state19))
        {
            ++observation.movevm_transition_07_signature_failures;
            fighter_slot_mask = 0;
        }
        else
            observation.tira_state19_at_transition[fighter_slot] = state19;
    }
    observation.tira_character_slot_mask |= fighter_slot_mask;
    auto hash = observation.tira_random_transition_sequence_hash == 0
        ? std::uint64_t{1469598103934665603ull}
        : observation.tira_random_transition_sequence_hash;
    const auto append = [&](const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
    };
    append(&target, sizeof(target));
    append(&fighter_slot_mask, sizeof(fighter_slot_mask));
    if (fighter_slot_mask != 0)
    {
        const auto fighter = fighter_slot_mask == 1 ? 0u : 1u;
        const auto state19 = observation.tira_state19_at_transition[fighter];
        append(&state19, sizeof(state19));
    }
    observation.tira_random_transition_sequence_hash = hash;
    observation.tira_random_transition_target_mask |=
        static_cast<std::uint8_t>(target == 0x0153 ? 1u : 2u);
    observation.tira_last_transition_target = target;
    if (frame >= observation.before.frame_counter)
    {
        const auto offset = frame - observation.before.frame_counter;
        if (offset <= Schema::maximum_supported_native_batch_width)
            observation.tira_random_transition_source_mask |=
                static_cast<std::uint16_t>(1u << offset);
    }
}

void DeterministicHookSet::ObserveMoveVmTransition(
    void* chara, std::int32_t argument_count, std::uint16_t* arguments,
    OuterTickCaptureContext& batch) noexcept
{
    auto& observation = *batch.observation;
    std::uint16_t target{};
    if (argument_count < 1 || argument_count > 6 || arguments == nullptr
        || !SafeRead(reinterpret_cast<std::uintptr_t>(arguments), target))
    {
        ++observation.movevm_transition_07_signature_failures;
        return;
    }
    ++observation.movevm_transition_07_calls;
    auto hash = observation.movevm_transition_07_sequence_hash == 0
        ? std::uint64_t{1469598103934665603ull}
        : observation.movevm_transition_07_sequence_hash;
    const auto append = [&](const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
    };
    append(&argument_count, sizeof(argument_count));
    append(&target, sizeof(target));
    observation.movevm_transition_07_sequence_hash = hash;

    std::uint16_t character_id{};
    if (chara == nullptr || !SafeRead(
            reinterpret_cast<std::uintptr_t>(chara) + 0x24c, character_id))
    {
        ++observation.movevm_transition_07_signature_failures;
        return;
    }
    // Native fighter/resource ID 0x23 is Tira; replay metadata uses a
    // different zero-based reflected character enum.
    if (character_id == 0x23)
        ObserveTiraRandomTransition(chara, target, batch, observation);
}

void __fastcall DeterministicHookSet::MoveVmTransitionAuthor07Detour(
    void* chara, std::int32_t argument_count,
    std::uint16_t* arguments) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->movevm_transition_author_07_trampoline_
        : movevm_transition_author_07_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original =
        reinterpret_cast<MoveVmTransitionAuthor07Fn>(trampoline);
    if (original != nullptr) original(chara, argument_count, arguments);
    auto* batch = active_outer_capture_;
    if (hooks != nullptr && batch != nullptr && batch->observation != nullptr)
        ObserveMoveVmTransition(chara, argument_count, arguments, *batch);
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::ResolvedHitConsumerDetour() noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->resolved_hit_consumer_trampoline_
        : resolved_hit_consumer_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<ResolvedHitConsumerFn>(trampoline);
    auto* batch = active_outer_capture_;
    if (hooks != nullptr && batch != nullptr && batch->observation != nullptr)
    {
        // Ghidra contract: LuxBattle_ApplyDamageFromPendingHit consumes this
        // one-shot attacker pointer before applying reaction and damage. Read
        // it before the native call clears the latch. Presentation activity is
        // deliberately not part of this confirmed-hit boundary.
        constexpr std::uintptr_t pending_reaction_rva = 0x485e738;
        constexpr std::uintptr_t pending_attacker_rva = 0x485e740;
        constexpr std::uintptr_t pending_flags_rva = 0x485e748;
        constexpr std::uintptr_t fighter_roots_rva = 0x470de90;
        std::uintptr_t attacker{};
        std::array<std::uintptr_t, 2> fighter_roots{};
        std::uint32_t reaction_move{};
        std::uint32_t transition_flags{};
        auto& observation = *batch->observation;
        if (!SafeRead(hooks->image_base_ + pending_attacker_rva, attacker))
            ++observation.resolved_hit_signature_failures;
        else if (attacker != 0)
        {
            if (!SafeRead(hooks->image_base_ + pending_reaction_rva,
                    reaction_move)
                || !SafeRead(hooks->image_base_ + pending_flags_rva,
                    transition_flags)
                || !SafeRead(hooks->image_base_ + fighter_roots_rva,
                    fighter_roots))
            {
                ++observation.resolved_hit_signature_failures;
            }
            else
            {
                const std::uint8_t attacker_slot = attacker == fighter_roots[0]
                    ? 1u : attacker == fighter_roots[1] ? 2u : 0u;
                if (attacker_slot == 0)
                    ++observation.resolved_hit_signature_failures;
                else
                {
                    ++observation.resolved_hit_calls;
                    auto hash = observation.resolved_hit_sequence_hash == 0
                        ? std::uint64_t{1469598103934665603ull}
                        : observation.resolved_hit_sequence_hash;
                    const auto append = [&hash](const auto& value) noexcept {
                        const auto* bytes = reinterpret_cast<const std::uint8_t*>(
                            &value);
                        for (std::size_t index = 0; index < sizeof(value); ++index)
                        {
                            hash ^= bytes[index];
                            hash *= 1099511628211ull;
                        }
                    };
                    append(attacker_slot);
                    append(reaction_move);
                    append(transition_flags);
                    observation.resolved_hit_sequence_hash = hash;
                }
            }
        }
    }
    if (original != nullptr) original();
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::OuterTickDetour(
    void* battle_manager, float delta_seconds) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->outer_tick_trampoline_
        : outer_tick_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<OuterTickFn>(trampoline);
    OuterTickObservation observation{};
    observation.battle_manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    observation.batch_id = hooks != nullptr ? ++hooks->next_outer_batch_id_ : 0;
    observation.thread_id = ::GetCurrentThreadId();
    observation.delta_seconds = delta_seconds;
    observation.fp_before = CaptureFloatingPointEnvironment();
    observation.fp_before_valid = true;
    if (hooks != nullptr)
    {
        hooks->callbacks_.outer_tick_prepare(
            hooks->callbacks_.user, observation);
        // outer_tick_prepare first advances the replay lifetime generation.
        // Only then may a graph be admitted for this exact generation.
        if (!hooks->PrepareAudioOwnerGraph(
                reinterpret_cast<std::uintptr_t>(battle_manager)))
            hooks->ClearAudioOwnerGraph();
        observation.audio_owner_graph_failure_stage =
            hooks->audio_graph_failure_stage_;
        hooks->CaptureOuterTickState(
            battle_manager, observation.before, observation.read_mask,
            0x1, 0x2, 0x4, 0x8);
        hooks->callbacks_.outer_tick_begin(
            hooks->callbacks_.user, observation);
    }
    OuterTickCaptureContext capture_context{&observation};
    if (hooks != nullptr)
    {
        capture_context.frame_counter_address = hooks->image_base_
            + Schema::Sc6FrameLayout::frame_counter_rva;
        capture_context.suppress_speculative_presentation =
            hooks->suppress_presentation_next_outer_tick_.exchange(
                false, std::memory_order_acq_rel);
    }
    OuterTickCaptureContext* previous_capture = active_outer_capture_;
    active_outer_capture_ = &capture_context;
    particle_shadow_pool.Reset();
    if (hooks != nullptr && hooks->callbacks_.outer_tick_source != nullptr)
    {
        hooks->callbacks_.outer_tick_source(
            hooks->callbacks_.user, observation);
    }
    if (original != nullptr)
        observation.authoritative_input_aborted_before_consume =
            InvokeOuterTickWithAbortGuard(
                original, battle_manager, delta_seconds);
    if (hooks == nullptr
        || !CaptureCameraPublicationSignature(
            hooks->image_base_, observation.camera_publication,
            observation.camera_publication_hash))
    {
        ++observation.camera_signature_failures;
    }
    active_outer_capture_ = previous_capture;
    observation.fp_after = CaptureFloatingPointEnvironment();
    observation.fp_after_valid = true;
    if (hooks != nullptr)
    {
        hooks->CaptureOuterTickState(
            battle_manager, observation.after, observation.read_mask,
            0x10, 0x20, 0x40, 0x80);
        DispatchCompletedOuterTick(hooks->callbacks_.user,
            hooks->callbacks_.outer_tick, observation);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

namespace
{
constexpr DWORD authoritative_input_abort_exception = 0xe0484d01u;

int AuthoritativeInputAbortFilter(DWORD code) noexcept
{
    return code == authoritative_input_abort_exception
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
}

bool DeterministicHookSet::InvokeOuterTickWithAbortGuard(
    OuterTickFn original, void* battle_manager, float delta_seconds) noexcept
{
    bool aborted = false;
    __try
    {
        original(battle_manager, delta_seconds);
    }
    __except (AuthoritativeInputAbortFilter(GetExceptionCode()))
    {
        aborted = true;
    }
    return aborted;
}

[[noreturn]] void DeterministicHookSet::AbortActiveOuterTick() noexcept
{
    RaiseException(authoritative_input_abort_exception, 0, 0, nullptr);
    std::terminate();
}

bool DeterministicHookSet::installed() const noexcept
{
    return installed_.load(std::memory_order_acquire);
}

bool DeterministicHookSet::OuterStateMatchesEnvelope(
    const OuterTickState& state,
    const NativeBatchEnvelope& envelope,
    bool before) const noexcept
{
    return state.frame_counter
            == (before ? envelope.native_frame_before
                       : envelope.native_frame_after)
        && state.input_game_round
            == (before ? envelope.input_round_before
                       : envelope.input_round_after)
        && state.input_game_time
            == (before ? envelope.input_time_before
                       : envelope.input_time_after)
        && state.manager_game_round_cursor
            == (before ? envelope.manager_round_cursor_before
                       : envelope.manager_round_cursor_after)
        && state.manager_game_time_cursor
            == (before ? envelope.manager_time_cursor_before
                       : envelope.manager_time_cursor_after)
        && state.main_state
            == (before ? envelope.main_state_before : envelope.main_state_after)
        && state.round_state
            == (before ? envelope.round_state_before : envelope.round_state_after);
}

Status DeterministicHookSet::ValidateOwnedBatchRequest(
    const OwnedBatchReplayRequest& request,
    OwnedBatchReplayResult& output,
    bool& capture_corrected) const noexcept
{
    capture_corrected = request.presentation_mode
        == OwnedBatchPresentationMode::CaptureCorrected;
    if (!installed() || request.battle_manager == 0
        || request.owner_thread_id == 0
        || request.owner_thread_id != ::GetCurrentThreadId()
        || request.envelope == nullptr
        || request.coordinates.size() != request.inputs.size()
        || request.coordinates.size() != request.envelope->coordinate_count
        || request.coordinates.size()
            > Schema::maximum_supported_native_batch_width
        || (request.landing_offset != UINT32_MAX
            && (request.landing_offset >= request.coordinates.size()
                || request.capture_landing == nullptr))
        || (capture_corrected
            && (request.corrected_observation == nullptr
                || request.corrected_inputs.size() != request.inputs.size()))
        || request.envelope->input_generation_changed
        || active_outer_capture_ != nullptr || outer_tick_trampoline_ == 0)
    {
        output.failure = FailureCode::InvalidConfiguration;
        return Status::failure(output.failure);
    }
    for (std::size_t index = 0; index < request.coordinates.size(); ++index)
    {
        if (request.coordinates[index].generation
                != request.envelope->entry_coordinate.generation
            || request.coordinates[index].frame
                != request.envelope->entry_coordinate.frame + index + 1
            || (!capture_corrected
                && !request.inputs[index].post_filter_observed)
            || !request.inputs[index].source_rows_observed)
        {
            output.failure = FailureCode::IdentityMismatch;
            return Status::failure(output.failure);
        }
    }
    if (capture_corrected)
    {
        std::copy(request.inputs.begin(), request.inputs.end(),
            request.corrected_inputs.begin());
        *request.corrected_observation = {};
    }
    return Status::success();
}

Status DeterministicHookSet::PrepareOwnedBatchState(
    const OwnedBatchReplayRequest& request,
    OwnedBatchReplayResult& output) noexcept
{
    std::uint16_t pre_handoff_mask{};
    OuterTickState pre_handoff{};
    CaptureOuterTickState(reinterpret_cast<void*>(request.battle_manager),
        pre_handoff, pre_handoff_mask, 0x1, 0x2, 0x4, 0x8);
    if (pre_handoff_mask != Schema::Sc6FrameLayout::required_outer_tick_pre_read_mask
        || pre_handoff.input_log == 0
        || pre_handoff.frame_counter != request.envelope->native_frame_before
        || pre_handoff.manager_game_round_cursor
            != request.envelope->manager_round_cursor_before
        || pre_handoff.manager_game_time_cursor
            != request.envelope->manager_time_cursor_before
        || pre_handoff.main_state != request.envelope->main_state_before
        || pre_handoff.round_state != request.envelope->round_state_before
        || !SafeWrite(pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_game_round,
            request.envelope->input_round_before)
        || !SafeWrite(pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_game_time,
            request.envelope->input_time_before))
    {
        output.before = pre_handoff;
        output.failure = FailureCode::IdentityMismatch;
        return Status::failure(output.failure);
    }
    for (const auto& input : request.inputs)
    {
        for (std::size_t slot = 0; slot < 2; ++slot)
        {
            const auto& source = input.source_rows[slot];
            if (source.filled == 0) continue;
            const auto row = pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_cache
                + (slot * Schema::Sc6FrameLayout::input_log_cache_rows_per_player
                    + (source.frame_index & 0x1ffu))
                    * Schema::Sc6FrameLayout::input_log_cache_row_stride;
            if (!SafeWrite(row, source.game_round)
                || !SafeWrite(row + 4, source.frame_index)
                || !SafeWrite(row + 8, source.input_value)
                || !SafeWrite(row + 12, source.filled))
            {
                output.failure = FailureCode::RestoreWriteFailed;
                return Status::failure(output.failure);
            }
        }
    }
    if (!request.inputs.empty()
        && !SafeWrite(pre_handoff.input_log
                + Schema::Sc6FrameLayout::input_log_update_time,
            request.inputs.back().input_update_time))
    {
        output.failure = FailureCode::RestoreWriteFailed;
        return Status::failure(output.failure);
    }
    std::uint16_t read_mask{};
    CaptureOuterTickState(
        reinterpret_cast<void*>(request.battle_manager), output.before,
        read_mask, 0x1, 0x2, 0x4, 0x8);
    if (read_mask != Schema::Sc6FrameLayout::required_outer_tick_pre_read_mask
        || !OuterStateMatchesEnvelope(output.before, *request.envelope, true))
    {
        output.failure = FailureCode::IdentityMismatch;
        return Status::failure(output.failure);
    }
    return Status::success();
}

Status DeterministicHookSet::ExecuteQualificationStageTerminalIfRequested(
    const OwnedBatchReplayRequest& request,
    OwnedBatchReplayResult& output) noexcept
{
    const auto mask = request.envelope->qualification_stage_terminal_mask;
    if (mask == 0) return Status::success();
    if ((mask != 1 && mask != 2)
        || (mask == 1 && request.envelope->stage_wall_journal_count != 1)
        || (mask == 2 && request.envelope->stage_barrier_journal_count != 1))
    {
        output.failure = FailureCode::InvalidConfiguration;
        return Status::failure(output.failure);
    }
    const bool wall = mask == 1;
    const auto& event = wall ? request.envelope->stage_wall_journal[0]
                             : request.envelope->stage_barrier_journal[0];
    std::uintptr_t actor{};
    const auto kind = wall ? StageBreakActorKind::Wall
                           : StageBreakActorKind::Barrier;
    if (!stage_break_presentation_identity_.ResolveActorAddress(
            request.envelope->entry_coordinate.generation,
            event.owner_logical_id, kind, actor).ok())
    {
        output.failure = FailureCode::IdentityMismatch;
        return Status::failure(output.failure);
    }
    const Status terminal = ExecuteQualificationStageTerminal(
        event, wall, actor);
    if (!terminal.ok()) output.failure = terminal.code;
    return terminal;
}

void DeterministicHookSet::CopyObservedGameplayIdentity(
    const OuterTickObservation& observation,
    OwnedBatchReplayResult& output) noexcept
{
    output.camera_publication_hash = observation.camera_publication_hash;
    output.camera_publication = observation.camera_publication;
    output.camera_signature_failures = observation.camera_signature_failures;
    output.gameplay_xorshift_draws = observation.gameplay_xorshift_draws;
    output.gameplay_xorshift_sequence_hash =
        observation.gameplay_xorshift_sequence_hash;
    output.gameplay_xorshift_known_callers =
        observation.gameplay_xorshift_known_callers;
    output.gameplay_xorshift_unknown_callers =
        observation.gameplay_xorshift_unknown_callers;
    output.gameplay_xorshift_weighted_draws =
        observation.gameplay_xorshift_weighted_draws;
    output.gameplay_xorshift_if_draws = observation.gameplay_xorshift_if_draws;
    output.gameplay_xorshift_weighted_source_mask =
        observation.gameplay_xorshift_weighted_source_mask;
    output.gameplay_xorshift_if_source_mask =
        observation.gameplay_xorshift_if_source_mask;
    output.movevm_transition_07_calls = observation.movevm_transition_07_calls;
    output.movevm_transition_07_sequence_hash =
        observation.movevm_transition_07_sequence_hash;
    output.movevm_transition_07_signature_failures =
        observation.movevm_transition_07_signature_failures;
    output.resolved_hit_calls = observation.resolved_hit_calls;
    output.resolved_hit_sequence_hash = observation.resolved_hit_sequence_hash;
    output.resolved_hit_signature_failures =
        observation.resolved_hit_signature_failures;
    output.tira_random_transition_calls =
        observation.tira_random_transition_calls;
    output.tira_random_transition_sequence_hash =
        observation.tira_random_transition_sequence_hash;
    output.tira_random_transition_source_mask =
        observation.tira_random_transition_source_mask;
    output.tira_random_transition_target_mask =
        observation.tira_random_transition_target_mask;
    output.tira_last_transition_target = observation.tira_last_transition_target;
    output.tira_character_slot_mask = observation.tira_character_slot_mask;
    output.tira_state19_at_transition = observation.tira_state19_at_transition;
}

bool DeterministicHookSet::OwnedGameplayIdentityMatches(
    const OwnedBatchReplayRequest& request,
    const OwnedBatchReplayResult& output) noexcept
{
    const auto& expected = *request.envelope;
    return output.gameplay_xorshift_draws == expected.gameplay_xorshift_draws
        && output.gameplay_xorshift_sequence_hash
            == expected.gameplay_xorshift_sequence_hash
        && output.gameplay_xorshift_known_callers
            == expected.gameplay_xorshift_known_callers
        && output.gameplay_xorshift_unknown_callers == 0
        && expected.gameplay_xorshift_unknown_callers == 0
        && output.gameplay_xorshift_weighted_draws
            == expected.gameplay_xorshift_weighted_draws
        && output.gameplay_xorshift_if_draws
            == expected.gameplay_xorshift_if_draws
        && output.gameplay_xorshift_weighted_source_mask
            == expected.gameplay_xorshift_weighted_source_mask
        && output.gameplay_xorshift_if_source_mask
            == expected.gameplay_xorshift_if_source_mask
        && output.movevm_transition_07_calls
            == expected.movevm_transition_07_calls
        && output.movevm_transition_07_sequence_hash
            == expected.movevm_transition_07_sequence_hash
        && output.movevm_transition_07_signature_failures == 0
        && expected.movevm_transition_07_signature_failures == 0
        && output.resolved_hit_calls == expected.resolved_hit_calls
        && output.resolved_hit_sequence_hash
            == expected.resolved_hit_sequence_hash
        && output.resolved_hit_signature_failures == 0
        && expected.resolved_hit_signature_failures == 0
        && output.tira_random_transition_calls
            == expected.tira_random_transition_calls
        && output.tira_random_transition_sequence_hash
            == expected.tira_random_transition_sequence_hash
        && output.tira_random_transition_source_mask
            == expected.tira_random_transition_source_mask
        && output.tira_random_transition_target_mask
            == expected.tira_random_transition_target_mask
        && output.tira_last_transition_target
            == expected.tira_last_transition_target
        && output.tira_character_slot_mask == expected.tira_character_slot_mask
        && output.tira_state19_at_transition
            == expected.tira_state19_at_transition;
}

Status DeterministicHookSet::ExecuteOwnedNativeTick(
    const OwnedBatchReplayRequest& request,
    OwnedBatchReplayResult& output,
    OuterTickObservation& observation,
    bool capture_corrected) noexcept
{
    OwnedBatchExecution execution{&request, &output};
    OuterTickCaptureContext capture_context{&observation};
    capture_context.frame_counter_address = image_base_
        + Schema::Sc6FrameLayout::frame_counter_rva;
    capture_context.owned = &execution;
    active_outer_capture_ = &capture_context;
    const auto original = reinterpret_cast<OuterTickFn>(outer_tick_trampoline_);
    __try
    {
        ExecuteQualificationStageTerminalIfRequested(request, output);
        if (output.failure == FailureCode::None)
            original(reinterpret_cast<void*>(request.battle_manager),
                request.envelope->delta_seconds);
        if (!CaptureCameraPublicationSignature(
                image_base_, observation.camera_publication,
                observation.camera_publication_hash))
            ++observation.camera_signature_failures;
        CopyObservedGameplayIdentity(observation, output);
        if (!capture_corrected && output.failure == FailureCode::None
            && !OwnedGameplayIdentityMatches(request, output))
            output.failure = FailureCode::StateHashMismatch;
        if (!capture_corrected && output.failure == FailureCode::None
            && !CompleteBattleAudioJournal(*request.envelope, output))
        {
            ++output.audio_sequence_mismatches;
            ++output.presentation_failures;
            output.presentation_failure_mask |= 1u << 9;
            output.failure = FailureCode::PresentationFailed;
        }
        if (!capture_corrected && output.failure == FailureCode::None
            && !ConsumeBattleAudioJournal(*request.envelope, output))
        {
            ++output.audio_sequence_mismatches;
            ++output.presentation_failures;
            output.presentation_failure_mask |= 1u << 7;
            output.failure = FailureCode::PresentationFailed;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output.failure = FailureCode::AdvanceFailed;
    }
    active_outer_capture_ = nullptr;
    return output.failure == FailureCode::None
        ? Status::success() : Status::failure(output.failure);
}

Status DeterministicHookSet::ValidateOwnedBatchResult(
    const OwnedBatchReplayRequest& request,
    OwnedBatchReplayResult& output,
    OuterTickObservation& observation,
    bool capture_corrected) noexcept
{
    std::uint16_t read_mask{};
    CaptureOuterTickState(
        reinterpret_cast<void*>(request.battle_manager), output.after,
        read_mask, 0x10, 0x20, 0x40, 0x80);
    if (read_mask != Schema::Sc6FrameLayout::required_outer_tick_post_read_mask)
        output.validation_difference_mask |= 1u << 0;
    if (output.observed_coordinates != request.coordinates.size())
        output.validation_difference_mask |= 1u << 1;
    if (output.after.input_log != output.before.input_log)
        output.validation_difference_mask |= 1u << 2;
    if (output.after.frame_counter != request.envelope->native_frame_after)
        output.validation_difference_mask |= 1u << 3;
    if (output.after.input_game_round != request.envelope->input_round_after)
        output.validation_difference_mask |= 1u << 4;
    if (output.after.input_game_time != request.envelope->input_time_after)
        output.validation_difference_mask |= 1u << 5;
    if (output.after.manager_game_round_cursor
        != request.envelope->manager_round_cursor_after)
        output.validation_difference_mask |= 1u << 6;
    if (output.after.manager_game_time_cursor
        != request.envelope->manager_time_cursor_after)
        output.validation_difference_mask |= 1u << 7;
    if (output.after.main_state != request.envelope->main_state_after)
        output.validation_difference_mask |= 1u << 8;
    if (output.after.round_state != request.envelope->round_state_after)
        output.validation_difference_mask |= 1u << 9;
    if (read_mask != Schema::Sc6FrameLayout::required_outer_tick_post_read_mask
        || output.observed_coordinates != request.coordinates.size()
        || output.after.input_log != output.before.input_log
        || !OuterStateMatchesEnvelope(output.after, *request.envelope, false))
    {
        output.failure = FailureCode::RestoreVerificationFailed;
        return Status::failure(output.failure);
    }
    observation.after = output.after;
    if (capture_corrected)
    {
        if (observation.stage_signature_failures != 0
            || observation.battle_audio_signature_failures != 0
            || observation.particle_signature_failures != 0
            || observation.camera_signature_failures != 0
            || observation.presentation_order_failures != 0)
        {
            output.failure = FailureCode::PresentationFailed;
            return Status::failure(output.failure);
        }
        *request.corrected_observation = observation;
    }
    return Status::success();
}

Status DeterministicHookSet::ExecuteOwnedBatch(
    const OwnedBatchReplayRequest& request,
    OwnedBatchReplayResult& output) noexcept
{
    output = {};
    bool capture_corrected{};
    Status status = ValidateOwnedBatchRequest(
        request, output, capture_corrected);
    if (!status.ok()) return status;
    status = PrepareOwnedBatchState(request, output);
    if (!status.ok()) return status;

    OuterTickObservation observation{};
    observation.battle_manager = request.battle_manager;
    observation.batch_id = ++next_outer_batch_id_;
    observation.thread_id = request.owner_thread_id;
    observation.delta_seconds = request.envelope->delta_seconds;
    observation.before = output.before;
    observation.read_mask = 0x0f;
    observation.qualification_stage_terminal_mask =
        request.envelope->qualification_stage_terminal_mask;
    status = RestoreBattleAudioRemapEntry(*request.envelope, output);
    if (!status.ok())
    {
        output.failure = status.code;
        return status;
    }
    status = ExecuteOwnedNativeTick(
        request, output, observation, capture_corrected);
    if (!status.ok()) return status;
    return ValidateOwnedBatchResult(
        request, output, observation, capture_corrected);
}

void __fastcall DeterministicHookSet::FrameFencepostDetour(
    void* battle_manager) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->frame_fencepost_trampoline_
        : frame_fencepost_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<FrameFencepostFn>(trampoline);
    if (original != nullptr)
    {
        original(battle_manager);
        if (hooks != nullptr)
        {
            hooks->EmitFrameFencepost(battle_manager);
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::ReplayPostTickDetour(
    void* replay_state) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    DeterministicHookSet* hooks = active_.load(std::memory_order_acquire);
    const std::uint64_t trampoline = hooks != nullptr
        ? hooks->replay_post_tick_trampoline_
        : replay_post_tick_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<ReplayPostTickFn>(trampoline);
    std::uint32_t exit_guard = 1;
    if (hooks != nullptr && replay_state != nullptr
        && SafeRead(
            reinterpret_cast<std::uintptr_t>(replay_state)
                + Schema::Sc6ReplayLayout::exit_guard,
            exit_guard)
        && exit_guard == 0)
    {
        hooks->EmitReplayExit(replay_state);
    }
    if (original != nullptr)
    {
        original(replay_state);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::CallbackExecutorDetour(
    void* collection, void* callback_argument) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->callback_executor_trampoline_
        : callback_executor_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<CallbackExecutorFn>(trampoline);
    auto* batch = active_outer_capture_;
    const bool is_input_filter = hooks != nullptr && batch != nullptr
        && batch->observation != nullptr
        && reinterpret_cast<std::uintptr_t>(collection)
            == batch->observation->battle_manager
                + Schema::Sc6FrameLayout::manager_input_filter_callbacks;
    PlayerInput before[2]{};
    bool before_valid = is_input_filter
        && CaptureInputPairArray(callback_argument, before);
    if (before_valid && batch->owned != nullptr)
    {
        auto& execution = *batch->owned;
        const auto index = execution.result->observed_coordinates;
        if (index >= execution.request->inputs.size()
            || execution.invocations_for_coordinate != 0
            || !PublishInputPairArray(callback_argument,
                execution.request->inputs[index].players))
        {
            execution.result->failure = FailureCode::AdvanceFailed;
            before_valid = false;
        }
        else
        {
            before[0] = execution.request->inputs[index].players[0];
            before[1] = execution.request->inputs[index].players[1];
            ++execution.invocations_for_coordinate;
        }
    }
    else if (is_input_filter && hooks != nullptr
        && hooks->callbacks_.authoritative_input != nullptr)
    {
        PlayerInput authoritative[2]{};
        const auto disposition = hooks->callbacks_.authoritative_input(
            hooks->callbacks_.user, *batch->observation, before_valid, before,
            authoritative);
        const auto publish = [](void* context,
            const PlayerInput (&input)[2]) noexcept {
                return PublishInputPairArray(context, input);
            };
        const auto commit = [](void* context) noexcept {
                auto* active_hooks = static_cast<DeterministicHookSet*>(context);
                return active_hooks->callbacks_.authoritative_input_commit
                    != nullptr
                    && active_hooks->callbacks_.authoritative_input_commit(
                        active_hooks->callbacks_.user);
            };
        const auto gated = ApplyAuthoritativeInputGate(
            disposition, before_valid, before, authoritative,
            publish, callback_argument, commit, hooks);
        before[0] = gated.before[0];
        before[1] = gated.before[1];
        before_valid = gated.before_valid;
        batch->observation->authoritative_input_requested = gated.requested;
        batch->observation->authoritative_input_round_barrier =
            gated.round_barrier;
        batch->observation->authoritative_input_applied = gated.applied;
        batch->observation->authoritative_input_failed_closed =
            gated.failed_closed;
    }
    const bool abort_before_consume = batch != nullptr
        && batch->observation != nullptr
        && batch->observation->authoritative_input_failed_closed;
    if (abort_before_consume)
    {
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        AbortActiveOuterTick();
    }
    const bool suppress_stock_callback = batch != nullptr
        && batch->observation != nullptr
        && batch->observation->authoritative_input_requested
        && (!before_valid
            || batch->observation->authoritative_input_failed_closed);
    if (original != nullptr && !suppress_stock_callback)
        original(collection, callback_argument);
    PlayerInput after[2]{};
    const bool after_valid = before_valid
        && CaptureInputPairArray(callback_argument, after);
    if (after_valid)
    {
        std::copy(std::begin(before), std::end(before), batch->pre_filter_inputs);
        std::copy(std::begin(after), std::end(after), batch->post_filter_inputs);
        ++batch->input_filter_invocations;
        batch->input_filter_observed = true;
        if (batch->owned != nullptr)
        {
            auto& execution = *batch->owned;
            const auto index = execution.result->observed_coordinates;
            ++execution.result->filter_invocations;
            const bool capture_corrected = execution.request->presentation_mode
                == OwnedBatchPresentationMode::CaptureCorrected;
            if (index >= execution.request->inputs.size())
            {
                execution.result->failure = FailureCode::AdvanceFailed;
            }
            else if (capture_corrected)
            {
                auto& corrected = execution.request->corrected_inputs[index];
                corrected.players[0] = before[0];
                corrected.players[1] = before[1];
                corrected.post_filter_players[0] = after[0];
                corrected.post_filter_players[1] = after[1];
                corrected.post_filter_observed = true;
            }
            else if (after[0]
                    != execution.request->inputs[index].post_filter_players[0]
                || after[1]
                    != execution.request->inputs[index].post_filter_players[1])
            {
                execution.result->failure = FailureCode::AdvanceFailed;
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::StageBreakWallDetour(
    void* actor, bool immediately) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr ? hooks->stage_break_wall_trampoline_
        : stage_break_wall_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<StageBreakWallFn>(trampoline);
    auto* batch = active_outer_capture_;
    std::int32_t actor_id{};
    const std::uint8_t immediate_value = immediately ? 1 : 0;
    const bool signature_ok = actor != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x450, actor_id);
    StagePresentationJournalEntry semantic{};
    std::uint8_t break_state{};
    float fade_timer{};
    float fade_rate{};
    const bool payload_ok = signature_ok
        && CaptureStageSemantic(actor_id, &immediate_value,
            sizeof(immediate_value), semantic);
    const bool identity_ok = payload_ok && hooks != nullptr
        && hooks->stage_break_presentation_identity_.ResolveActor(
            hooks->stage_break_presentation_identity_.generation(),
            reinterpret_cast<std::uintptr_t>(actor),
            semantic.owner_logical_id).ok();
    const bool canonical_ok = signature_ok
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x468, break_state)
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x46c, fade_timer)
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x470, fade_rate);
    const bool semantic_ok = payload_ok && identity_ok && canonical_ok;
    if (semantic_ok)
    {
        std::memcpy(semantic.canonical_before.data(), &break_state,
            sizeof(break_state));
        std::memcpy(semantic.canonical_before.data() + 4, &fade_timer,
            sizeof(fade_timer));
        std::memcpy(semantic.canonical_before.data() + 8, &fade_rate,
            sizeof(fade_rate));
        semantic.canonical_before_size = 12;
        semantic.first_particle = batch != nullptr && batch->observation != nullptr
            ? batch->observation->particle_spawn_journal_count : 0;
    }
    std::size_t observed_stage_index = maximum_stage_presentation_journal_events;
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto family_index = observation.stage_wall_calls;
        ++observation.stage_wall_calls;
        if (!AppendObservedPresentationOrder(batch->observation,
                batch->frame_counter_address, PresentationEventFamily::StageWall,
                family_index))
            ++observation.presentation_order_failures;
        if (!semantic_ok || !AppendStageSemantic(
                semantic, observation.stage_wall_hash)
            || observation.stage_wall_journal_count
                >= observation.stage_wall_journal.size())
        {
            ++batch->observation->stage_signature_failures;
        }
        else
        {
            observed_stage_index = observation.stage_wall_journal_count;
            observation.stage_wall_journal[
                observation.stage_wall_journal_count++] = semantic;
        }
    }
    const bool suppress = IsPresentationSuppressed(batch);
    if (!suppress)
    {
        if (original != nullptr) original(actor, immediately);
    }
    else
    {
        SuppressStageWall(
            actor, immediately, original, batch, semantic, semantic_ok);
    }
    if (batch != nullptr && batch->observation != nullptr
        && observed_stage_index < batch->observation->stage_wall_journal_count)
    {
        auto& entry = batch->observation->stage_wall_journal[observed_stage_index];
        // Keep the journal as the typed pre-call value captured above; only the
        // independently observed nested particle extent is completed after the
        // native terminal returns.
        entry = semantic;
        const auto count = batch->observation->particle_spawn_journal_count
            - semantic.first_particle;
        if (count > UINT8_MAX)
            ++batch->observation->stage_signature_failures;
        else
            entry.particle_count = static_cast<std::uint8_t>(count);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void DeterministicHookSet::SuppressStageWall(void* actor, bool immediately,
    StageBreakWallFn original, OuterTickCaptureContext* batch,
    const StagePresentationJournalEntry& semantic, bool semantic_ok) noexcept
{
        FailureCode speculative_failure = FailureCode::None;
        auto fail_presentation = [&](std::uint32_t mask) noexcept
        {
            if (batch->owned != nullptr)
            {
                ++batch->owned->result->presentation_failures;
                batch->owned->result->presentation_failure_mask |= mask;
                batch->owned->result->failure = FailureCode::PresentationFailed;
            }
            else
            {
                ++batch->observation->stage_signature_failures;
                speculative_failure = FailureCode::PresentationFailed;
            }
        };
        if (batch->owned != nullptr)
        {
            auto& replay = *batch->owned->result;
            const auto& envelope = *batch->owned->request->envelope;
            const bool verify = batch->owned->request->presentation_mode
                == OwnedBatchPresentationMode::VerifyRecorded;
            const auto index = replay.suppressed_stage_wall_calls++;
            if (verify && !VerifyPresentationOrder(
                    PresentationEventFamily::StageWall, index, envelope, replay,
                    batch->observation, batch->frame_counter_address))
            {
                replay.stage_journal_failure_mask |= 1u << 8;
                fail_presentation(1u << 12);
            }
            if (verify)
            {
                std::uint32_t mismatch{};
                if (!semantic_ok) mismatch |= 1u << 0;
                if (index >= envelope.stage_wall_journal_count)
                    mismatch |= 1u << 1;
                if (index < envelope.stage_wall_journal_count)
                {
                    const auto& expected = envelope.stage_wall_journal[index];
                    if (expected.payload_size != semantic.payload_size)
                        mismatch |= 1u << 2;
                    if (expected.semantic != semantic.semantic)
                        mismatch |= 1u << 3;
                    if (expected.owner_logical_id != semantic.owner_logical_id)
                        mismatch |= 1u << 4;
                    if (expected.canonical_before_size
                        != semantic.canonical_before_size)
                        mismatch |= 1u << 5;
                    if (expected.canonical_before != semantic.canonical_before)
                        mismatch |= 1u << 6;
                }
                if (mismatch == 0
                    && !AppendStageSemantic(semantic, replay.stage_wall_hash))
                    mismatch |= 1u << 7;
                if (mismatch != 0)
                {
                    replay.stage_journal_failure_mask |= mismatch;
                    ++replay.stage_signature_failures;
                }
            }
        }
        std::array<std::array<std::byte, 8>, wall_presentation_fields.size()> saved{};
        std::size_t written{};
        if (!CaptureAndZeroFields(actor, wall_presentation_fields, saved, written))
        {
            RestoreFields(actor, wall_presentation_fields, saved, written);
            fail_presentation(1u << 0);
        }
        else
        {
            PresentationMaskContext context{actor,
                wall_presentation_fields.data(), saved.data(), written, true,
                batch->owned != nullptr ? &batch->owned->result->failure
                                        : &speculative_failure};
            auto* previous_mask = active_presentation_mask;
            active_presentation_mask = &context;
            __try { if (original != nullptr) original(actor, immediately); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                if (batch->owned != nullptr)
                {
                    if (batch->owned->result->failure == FailureCode::None)
                        batch->owned->result->failure = FailureCode::AdvanceFailed;
                }
                else
                {
                    ++batch->observation->stage_signature_failures;
                    speculative_failure = FailureCode::AdvanceFailed;
                }
            }
            active_presentation_mask = previous_mask;
            if (!RestoreFields(actor, wall_presentation_fields, saved, written))
            {
                fail_presentation(1u << 0);
            }
        }
}

void __fastcall DeterministicHookSet::StageBreakBarrierDetour(
    void* actor, void* direction) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr ? hooks->stage_break_barrier_trampoline_
        : stage_break_barrier_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<StageBreakBarrierFn>(trampoline);
    auto* batch = active_outer_capture_;
    std::int32_t actor_id{};
    const bool signature_ok = actor != nullptr && direction != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x420, actor_id);
    StagePresentationJournalEntry semantic{};
    std::int32_t hit_count{};
    const bool payload_ok = signature_ok
        && CaptureStageSemantic(actor_id, direction, 12, semantic);
    const bool identity_ok = payload_ok && hooks != nullptr
        && hooks->stage_break_presentation_identity_.ResolveActor(
            hooks->stage_break_presentation_identity_.generation(),
            reinterpret_cast<std::uintptr_t>(actor),
            semantic.owner_logical_id).ok();
    const bool canonical_ok = signature_ok
        && SafeRead(reinterpret_cast<std::uintptr_t>(actor) + 0x468, hit_count);
    const bool semantic_ok = payload_ok && identity_ok && canonical_ok;
    if (semantic_ok)
    {
        std::memcpy(semantic.canonical_before.data(), &hit_count,
            sizeof(hit_count));
        semantic.canonical_before_size = sizeof(hit_count);
        semantic.first_particle = batch != nullptr && batch->observation != nullptr
            ? batch->observation->particle_spawn_journal_count : 0;
    }
    std::size_t observed_stage_index = maximum_stage_presentation_journal_events;
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto family_index = observation.stage_barrier_calls;
        ++observation.stage_barrier_calls;
        if (!AppendObservedPresentationOrder(batch->observation,
                batch->frame_counter_address,
                PresentationEventFamily::StageBarrier, family_index))
            ++observation.presentation_order_failures;
        if (!semantic_ok || !AppendStageSemantic(
                semantic, observation.stage_barrier_hash)
            || observation.stage_barrier_journal_count
                >= observation.stage_barrier_journal.size())
        {
            ++observation.stage_signature_failures;
        }
        else
        {
            observed_stage_index = observation.stage_barrier_journal_count;
            observation.stage_barrier_journal[
                observation.stage_barrier_journal_count++] = semantic;
        }
    }
    const bool suppress = IsPresentationSuppressed(batch);
    if (!suppress)
    {
        if (original != nullptr) original(actor, direction);
    }
    else
    {
        SuppressStageBarrier(
            actor, direction, original, batch, semantic, semantic_ok);
    }
    if (batch != nullptr && batch->observation != nullptr
        && observed_stage_index
            < batch->observation->stage_barrier_journal_count)
    {
        auto& entry = batch->observation->stage_barrier_journal[
            observed_stage_index];
        // Match the wall contract: the journal owns the typed pre-call value,
        // while only its nested particle extent is completed after the native
        // terminal returns.
        entry = semantic;
        const auto count = batch->observation->particle_spawn_journal_count
            - semantic.first_particle;
        if (count > UINT8_MAX)
            ++batch->observation->stage_signature_failures;
        else
            entry.particle_count = static_cast<std::uint8_t>(count);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}
