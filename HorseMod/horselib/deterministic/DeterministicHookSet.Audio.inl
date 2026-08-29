void DeterministicHookSet::SuppressStageBarrier(void* actor, void* direction,
    StageBreakBarrierFn original, OuterTickCaptureContext* batch,
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
            const auto index = replay.suppressed_stage_barrier_calls++;
            if (verify && !VerifyPresentationOrder(
                    PresentationEventFamily::StageBarrier, index, envelope,
                    replay, batch->observation, batch->frame_counter_address))
            {
                replay.stage_journal_failure_mask |= 1u << 8;
                fail_presentation(1u << 12);
            }
            if (verify)
            {
                std::uint32_t mismatch{};
                if (!semantic_ok) mismatch |= 1u << 0;
                if (index >= envelope.stage_barrier_journal_count)
                    mismatch |= 1u << 1;
                if (index < envelope.stage_barrier_journal_count)
                {
                    const auto& expected = envelope.stage_barrier_journal[index];
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
                    && !AppendStageSemantic(semantic, replay.stage_barrier_hash))
                    mismatch |= 1u << 7;
                if (mismatch != 0)
                {
                    replay.stage_journal_failure_mask |= mismatch;
                    ++replay.stage_signature_failures;
                }
            }
        }
        std::array<std::array<std::byte, 8>, barrier_presentation_fields.size()> saved{};
        std::size_t written{};
        if (!CaptureAndZeroFields(actor, barrier_presentation_fields, saved, written))
        {
            RestoreFields(actor, barrier_presentation_fields, saved, written);
            fail_presentation(1u << 1);
        }
        else
        {
            PresentationMaskContext context{actor,
                barrier_presentation_fields.data(), saved.data(), written, true,
                batch->owned != nullptr ? &batch->owned->result->failure
                                        : &speculative_failure};
            auto* previous_mask = active_presentation_mask;
            active_presentation_mask = &context;
            __try { if (original != nullptr) original(actor, direction); }
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
            if (!RestoreFields(actor, barrier_presentation_fields, saved, written))
            {
                fail_presentation(1u << 1);
            }
        }
}

void __fastcall DeterministicHookSet::StageBreakDispatchDetour(
    void* emitter, std::int32_t actor_id, void* location) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->stage_break_dispatch_trampoline_
        : stage_break_dispatch_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<StageBreakDispatchFn>(trampoline);
    auto* batch = active_outer_capture_;
    StagePresentationJournalEntry semantic{};
    const bool semantic_ok = CaptureStageSemantic(
        actor_id, location, 12, semantic);
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto family_index = observation.stage_dispatch_calls;
        ++observation.stage_dispatch_calls;
        if (!AppendObservedPresentationOrder(batch->observation,
                batch->frame_counter_address,
                PresentationEventFamily::StageDispatch, family_index))
            ++observation.presentation_order_failures;
        if (!semantic_ok || !AppendStageSemantic(
                semantic, observation.stage_dispatch_hash)
            || observation.stage_dispatch_journal_count
                >= observation.stage_dispatch_journal.size())
        {
            ++observation.stage_signature_failures;
        }
        else
        {
            observation.stage_dispatch_journal[
                observation.stage_dispatch_journal_count++] = semantic;
        }
    }
    if (active_stage_commit != nullptr)
    {
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    auto* context = active_presentation_mask;
    if (context == nullptr || !context->masked)
    {
        if (original != nullptr) original(emitter, actor_id, location);
    }
    else if (!RestoreMaskContext(*context))
    {
        if (batch != nullptr && batch->owned != nullptr)
        {
            ++batch->owned->result->presentation_failures;
            batch->owned->result->presentation_failure_mask |= 1u << 2;
        }
        if (context->failure != nullptr)
            *context->failure = FailureCode::PresentationFailed;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        ::RaiseException(presentation_mask_exception, 0, 0, nullptr);
    }
    else
    {
        if (batch != nullptr && batch->owned != nullptr)
        {
            auto& replay = *batch->owned->result;
            const auto& envelope = *batch->owned->request->envelope;
            const bool verify = batch->owned->request->presentation_mode
                == OwnedBatchPresentationMode::VerifyRecorded;
            const auto index = replay.semantic_stage_dispatch_calls++;
            if (verify && !VerifyPresentationOrder(PresentationEventFamily::StageDispatch,
                    index, envelope, replay, batch->observation,
                    batch->frame_counter_address))
            {
                ++replay.presentation_failures;
                replay.presentation_failure_mask |= 1u << 12;
                replay.failure = FailureCode::PresentationFailed;
            }
            if (verify && (!semantic_ok || index >= envelope.stage_dispatch_journal_count
                || envelope.stage_dispatch_journal[index].payload_size
                    != semantic.payload_size
                || envelope.stage_dispatch_journal[index].semantic
                    != semantic.semantic
                || !AppendStageSemantic(semantic, replay.stage_dispatch_hash)))
            {
                ++replay.stage_signature_failures;
            }
        }
        if (original != nullptr) original(emitter, actor_id, location);
        if (!ZeroMaskContext(*context))
        {
            RestoreMaskContext(*context);
            if (batch != nullptr && batch->owned != nullptr)
            {
                ++batch->owned->result->presentation_failures;
                batch->owned->result->presentation_failure_mask |= 1u << 2;
            }
            if (context->failure != nullptr)
                *context->failure = FailureCode::PresentationFailed;
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            ::RaiseException(presentation_mask_exception, 0, 0, nullptr);
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

std::int32_t __fastcall DeterministicHookSet::BattleAudioDispatchDetour(
    void* battle_manager, void* event_record, bool alternate_route) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_dispatch_trampoline_
        : battle_audio_dispatch_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioDispatchFn>(trampoline);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch);
    const bool verify_recorded = IsOwnedPresentationVerification(batch);
    const bool capture_corrected = suppress && !verify_recorded;

    // Preserve only the verified success/failure contract. A successful owned
    // call returns synthetic token zero; it never exposes an authoritative live
    // voice ID, and the downstream tracking/command terminals remain suppressed.
    std::int32_t result = -1;
    std::size_t observed_journal_index = maximum_battle_audio_journal_dispatches;
    std::int32_t expected_success = -1;
    if (verify_recorded)
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        std::array<std::byte, 18> semantic{};
        const bool direct = active_battle_audio_source_depth == 0;
        const std::size_t index = replay.suppressed_audio_calls;
        const bool captured = CaptureBattleAudioSemantic(
            event_record, alternate_route, semantic);
        if (index >= envelope.battle_audio_journal_count)
        {
            // A restored gameplay checkpoint may be paired with newer
            // presentation-local queues. This call has no source-frame journal
            // identity for the replayed batch, so discard it before mixed
            // dispatcher work and leave the ordered admitted sequence intact.
            ++replay.discarded_audio_calls;
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return -1;
        }
        if (!captured
            || envelope.battle_audio_journal[index].semantic != semantic
            || envelope.battle_audio_journal[index].direct != (direct ? 1 : 0)
            || !MatchesNextPresentationOrder(
                PresentationEventFamily::BattleAudioDispatch,
                static_cast<std::uint32_t>(index), envelope, replay,
                batch->observation, batch->frame_counter_address))
        {
            // Presentation-local dispatch queues survive gameplay checkpoint
            // restore. A semantic match alone is insufficient because a stale
            // direct call can match a later source-owned dispatch. Leave both
            // cursors untouched unless this is the exact next source-frame
            // presentation event.
            ++replay.discarded_audio_calls;
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return -1;
        }
        else
        {
            expected_success = envelope.battle_audio_journal[index].succeeded;
        }
        if (!VerifyPresentationOrder(
                PresentationEventFamily::BattleAudioDispatch,
                static_cast<std::uint32_t>(index), envelope, replay,
                batch->observation, batch->frame_counter_address))
        {
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 12;
            replay.failure = FailureCode::PresentationFailed;
        }
        ++replay.suppressed_audio_calls;
        if (!captured || !AppendBattleAudioSemantic(semantic,
                replay.suppressed_audio_sequence_hash,
                replay.suppressed_audio_route_hash,
                replay.suppressed_audio_payload_hash,
                replay.suppressed_audio_position_hash))
        {
            replay.audio_journal_failure_mask |= 1u << 1;
        }
        if (direct)
        {
            ++replay.suppressed_audio_direct_dispatches;
            if (!captured || !AppendBattleAudioSemantic(semantic,
                    replay.suppressed_audio_direct_sequence_hash,
                    replay.suppressed_audio_direct_route_hash,
                    replay.suppressed_audio_direct_payload_hash,
                    replay.suppressed_audio_direct_position_hash))
            {
                replay.audio_journal_failure_mask |= 1u << 2;
            }
        }
    }
    else if (batch != nullptr && batch->observation != nullptr)
    {
        observed_journal_index = ObserveBattleAudioDispatch(
            batch, event_record, alternate_route);
    }
    if (original != nullptr)
        result = original(battle_manager, event_record, alternate_route);
    if (verify_recorded && expected_success >= 0)
    {
        // Re-enter the native dispatcher so deterministic source/remap logic
        // and every ordered terminal hook still execute. Those terminal hooks
        // suppress presentation-local allocation and queue mutation. The live
        // dispatcher can consequently compute a different success value, so
        // expose the admitted source-frame result to its simulation caller
        // only after the complete nested route has been verified.
        result = expected_success != 0 ? 0 : -1;
    }
    if (verify_recorded && expected_success >= 0
        && (result >= 0 ? 1 : 0) != expected_success)
    {
        auto& replay = *batch->owned->result;
        ++replay.audio_sequence_mismatches;
        replay.audio_journal_failure_mask |= 1u << 1;
    }
    if ((!suppress || capture_corrected)
        && batch != nullptr && batch->observation != nullptr
        && observed_journal_index < batch->observation->battle_audio_journal_count)
    {
        batch->observation->battle_audio_journal[observed_journal_index].succeeded
            = result >= 0 ? 1 : 0;
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

std::size_t DeterministicHookSet::ObserveBattleAudioDispatch(
    OuterTickCaptureContext* batch, void* event_record,
    bool alternate_route) noexcept
{
    auto& observation = *batch->observation;
    const auto family_index = observation.battle_audio_dispatches;
    if (!AppendObservedPresentationOrder(batch->observation,
            batch->frame_counter_address,
            PresentationEventFamily::BattleAudioDispatch, family_index))
        ++observation.presentation_order_failures;
    std::size_t observed_index = maximum_battle_audio_journal_dispatches;
    if (observation.battle_audio_journal_count
        >= observation.battle_audio_journal.size())
    {
        ++observation.battle_audio_signature_failures;
        observation.battle_audio_signature_failure_mask |= 1u << 0;
    }
    else if (!CaptureBattleAudioSemantic(event_record, alternate_route,
        observation.battle_audio_journal[
            observation.battle_audio_journal_count].semantic))
    {
        ++observation.battle_audio_signature_failures;
        observation.battle_audio_signature_failure_mask |= 1u << 1;
    }
    else
    {
        observed_index = observation.battle_audio_journal_count;
        observation.battle_audio_journal[observed_index].direct =
            active_battle_audio_source_depth == 0 ? 1 : 0;
        ++observation.battle_audio_journal_count;
    }
    ++observation.battle_audio_dispatches;
    if (!AppendBattleAudioSignature(event_record, alternate_route,
            observation.battle_audio_sequence_hash,
            observation.battle_audio_route_hash,
            observation.battle_audio_payload_hash,
            observation.battle_audio_position_hash))
    {
        ++observation.battle_audio_signature_failures;
        observation.battle_audio_signature_failure_mask |= 1u << 2;
    }
    if (active_battle_audio_source_depth == 0)
    {
        ++observation.battle_audio_direct_dispatches;
        if (!AppendBattleAudioSignature(event_record, alternate_route,
                observation.battle_audio_direct_sequence_hash,
                observation.battle_audio_direct_route_hash,
                observation.battle_audio_direct_payload_hash,
                observation.battle_audio_direct_position_hash))
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 3;
        }
    }
    return observed_index;
}

std::int32_t __fastcall DeterministicHookSet::BattleAudioRemapDetour(
    void* handler, std::int32_t contact_type) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_remap_trampoline_
        : battle_audio_remap_trampoline_global_.load(
            std::memory_order_acquire);
    const std::size_t handler_slot = FindOrRegisterBattleAudioHandler(handler);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch);
    const bool verify_recorded = IsOwnedPresentationVerification(batch);
    const bool capture_corrected = suppress && !verify_recorded;
    const bool mutates_selector = contact_type >= 8 && contact_type <= 11;
    const auto handler_bit = handler_slot < maximum_battle_audio_handlers
        ? std::uint8_t{1} << handler_slot : std::uint8_t{};
    if (verify_recorded && mutates_selector && handler_bit != 0)
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        if ((replay.suppressed_audio_remap_entry_mask & handler_bit) == 0
            && (envelope.battle_audio_remap_entry_mask & handler_bit) != 0)
        {
            const auto desired = static_cast<std::int32_t>(
                envelope.battle_audio_remap_entry_values[handler_slot]);
            std::int32_t observed{};
            if (!SafeWrite(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0,
                    desired)
                || !SafeRead(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0,
                    observed)
                || observed != desired)
            {
                ++replay.presentation_failures;
                replay.presentation_failure_mask |= 1u << 8;
                replay.failure = FailureCode::PresentationFailed;
                callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
                return 0;
            }
        }
    }
    std::int32_t before{};
    const bool before_valid = handler != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0, before);
    const auto original = reinterpret_cast<BattleAudioRemapFn>(trampoline);
    std::int32_t result = original != nullptr
        ? original(handler, contact_type) : 0;
    std::int32_t after{};
    bool after_valid = handler != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(handler) + 0x3E0, after);
    if (verify_recorded)
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        const std::size_t index = replay.suppressed_audio_remap_calls;
        const BattleAudioRemapJournalEntry observed{
            static_cast<std::uint8_t>(handler_slot), contact_type,
            before, result, after};
        if (!VerifyPresentationOrder(PresentationEventFamily::BattleAudioRemap,
                static_cast<std::uint32_t>(index), envelope, replay,
                batch->observation, batch->frame_counter_address))
        {
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 12;
            replay.failure = FailureCode::PresentationFailed;
        }
        if (index >= envelope.battle_audio_remap_journal_count
            || envelope.battle_audio_remap_journal[index].handler_slot
                != observed.handler_slot
            || envelope.battle_audio_remap_journal[index].contact_type != observed.contact_type
            || envelope.battle_audio_remap_journal[index].before != observed.before
            || envelope.battle_audio_remap_journal[index].result != observed.result
            || envelope.battle_audio_remap_journal[index].after != observed.after)
        {
            ++replay.audio_sequence_mismatches;
            replay.audio_journal_failure_mask |= 1u << 4;
        }
        ++replay.suppressed_audio_remap_calls;
        if (mutates_selector && before_valid
            && handler_slot < maximum_battle_audio_handlers
            && (replay.suppressed_audio_remap_entry_mask & handler_bit) == 0)
        {
            replay.suppressed_audio_remap_entry_mask |= handler_bit;
            replay.suppressed_audio_remap_entry_values[handler_slot] =
                static_cast<std::uint8_t>(before);
        }
        if (!before_valid || !after_valid
            || !AppendBattleAudioRemapSignature(
                static_cast<std::uint8_t>(handler_slot), contact_type, before,
                result, after, replay.suppressed_audio_remap_hash))
        {
            replay.audio_journal_failure_mask |= 1u << 4;
        }
    }
    else if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto family_index = observation.battle_audio_remap_calls;
        if (!AppendObservedPresentationOrder(batch->observation,
                batch->frame_counter_address,
                PresentationEventFamily::BattleAudioRemap, family_index))
            ++observation.presentation_order_failures;
        if (observation.battle_audio_remap_journal_count
            >= observation.battle_audio_remap_journal.size())
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 4;
        }
        else
        {
            auto& entry = observation.battle_audio_remap_journal[
                observation.battle_audio_remap_journal_count++];
            entry.handler_slot = static_cast<std::uint8_t>(handler_slot);
            entry.contact_type = contact_type;
            entry.before = before;
            entry.result = result;
            entry.after = after;
        }
        ++batch->observation->battle_audio_remap_calls;
        if (mutates_selector && before_valid
            && handler_slot < maximum_battle_audio_handlers
            && (batch->observation->battle_audio_remap_entry_mask
                & (std::uint8_t{1} << handler_slot)) == 0)
        {
            batch->observation->battle_audio_remap_entry_mask
                |= std::uint8_t{1} << handler_slot;
            batch->observation->battle_audio_remap_entry_values[handler_slot]
                = static_cast<std::uint8_t>(before);
        }
        if (!before_valid || !after_valid
            || !AppendBattleAudioRemapSignature(
                static_cast<std::uint8_t>(handler_slot), contact_type, before,
                result, after, batch->observation->battle_audio_remap_hash))
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 5;
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

std::size_t DeterministicHookSet::FindOrRegisterBattleAudioHandler(
    void* handler) noexcept
{
    if (handler == nullptr) return maximum_battle_audio_handlers;
    const auto identity = reinterpret_cast<std::uintptr_t>(handler);
    for (std::size_t index = 0;
         index < observed_battle_audio_handlers_.size(); ++index)
    {
        auto& slot = observed_battle_audio_handlers_[index];
        auto observed = slot.load(std::memory_order_acquire);
        if (observed == identity) return index;
        if (observed == 0
            && slot.compare_exchange_strong(observed, identity,
                std::memory_order_acq_rel, std::memory_order_acquire))
            return index;
    }
    battle_audio_handler_overflow_.store(true, std::memory_order_release);
    return maximum_battle_audio_handlers;
}

void __fastcall DeterministicHookSet::BattleAudioContactHandlerDetour(
    void* handler, void* event_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_contact_handler_trampoline_
        : battle_audio_contact_handler_trampoline_global_.load(
            std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch);
    const bool verify_recorded = IsOwnedPresentationVerification(batch);
    const bool capture_corrected = suppress && !verify_recorded;
    std::size_t observed_source_index = maximum_battle_audio_journal_sources;
    if (verify_recorded)
    {
        ReplayRecordedBattleAudioSource(batch, handler, event_record);
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    if (batch != nullptr && batch->observation != nullptr
        && (!suppress || capture_corrected))
    {
        auto& observation = *batch->observation;
        const auto source_index = observation.battle_audio_source_journal_count;
        if (observation.battle_audio_source_journal_count
            >= observation.battle_audio_source_journal.size())
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 6;
        }
        else if (!CaptureBattleAudioSourceSemantic(event_record,
            observation.battle_audio_source_journal[
                observation.battle_audio_source_journal_count].semantic))
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 7;
        }
        else
        {
            auto& source = observation.battle_audio_source_journal[source_index];
            source.first_presentation_order =
                observation.presentation_order_journal_count;
            if (!AppendObservedPresentationOrder(batch->observation,
                    batch->frame_counter_address,
                    PresentationEventFamily::BattleAudioSource,
                    observation.battle_audio_source_calls))
                ++observation.presentation_order_failures;
            source.first_dispatch = observation.battle_audio_journal_count;
            source.first_remap = observation.battle_audio_remap_journal_count;
            source.first_blueprint =
                observation.battle_audio_blueprint_journal_count;
            source.first_terminal = observation.audio_terminal_journal_count;
            observed_source_index = source_index;
            ++observation.battle_audio_source_journal_count;
        }
        ++batch->observation->battle_audio_source_calls;
        if (!AppendBattleAudioSourceSignature(event_record,
                batch->observation->battle_audio_source_hash))
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 8;
        }
    }
    if (verify_recorded)
    {
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    const auto original = reinterpret_cast<BattleAudioContactHandlerFn>(
        trampoline);
    if (original != nullptr)
    {
        ++active_battle_audio_source_depth;
        original(handler, event_record);
        --active_battle_audio_source_depth;
    }
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        if (observed_source_index < observation.battle_audio_source_journal_count)
        {
            auto& source = observation.battle_audio_source_journal[
                observed_source_index];
            const auto dispatch_count = observation.battle_audio_journal_count
                - source.first_dispatch;
            const auto remap_count = observation.battle_audio_remap_journal_count
                - source.first_remap;
            const auto blueprint_count =
                observation.battle_audio_blueprint_journal_count
                - source.first_blueprint;
            const auto terminal_count =
                observation.audio_terminal_journal_count
                - source.first_terminal;
            const auto presentation_order_count =
                observation.presentation_order_journal_count
                - source.first_presentation_order;
            if (dispatch_count > UINT8_MAX || remap_count > UINT8_MAX
                || blueprint_count > UINT8_MAX
                || terminal_count > UINT8_MAX
                || presentation_order_count > UINT8_MAX)
            {
                ++observation.battle_audio_signature_failures;
                observation.battle_audio_signature_failure_mask |= 1u << 9;
            }
            else
            {
                source.dispatch_count = static_cast<std::uint8_t>(dispatch_count);
                source.remap_count = static_cast<std::uint8_t>(remap_count);
                source.blueprint_count =
                    static_cast<std::uint8_t>(blueprint_count);
                source.terminal_count =
                    static_cast<std::uint8_t>(terminal_count);
                source.presentation_order_count =
                    static_cast<std::uint8_t>(presentation_order_count);
            }
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

bool DeterministicHookSet::ConsumeRecordedBattleAudioRemaps(
    OuterTickCaptureContext* batch, std::uintptr_t handler_identity,
    std::size_t handler_slot, std::int32_t source_contact_type,
    const BattleAudioSourceJournalEntry& source) noexcept
{
    auto& replay = *batch->owned->result;
    const auto& envelope = *batch->owned->request->envelope;
    const auto remap_end = static_cast<std::size_t>(source.first_remap)
        + source.remap_count;
    bool span_valid = true;
for (std::size_t remap_index = source.first_remap;
     span_valid && remap_index < remap_end; ++remap_index)
{
    const auto& entry =
        envelope.battle_audio_remap_journal[remap_index];
    const bool mutates = entry.contact_type >= 8
        && entry.contact_type <= 11;
    const auto handler_bit =
        handler_slot < maximum_battle_audio_handlers
        ? std::uint8_t{1} << handler_slot : std::uint8_t{};
    std::int32_t current{};
    span_valid = entry.handler_slot == handler_slot
        && entry.contact_type == source_contact_type
        && ValidateJournaledBattleAudioRemap(entry)
        && handler_bit != 0
        && SafeRead(handler_identity + 0x3E0, current)
        && current >= 0 && current <= 1;
    if (!span_valid) break;
    if (mutates
        && (replay.suppressed_audio_remap_entry_mask
            & handler_bit) == 0)
    {
        span_valid =
            (envelope.battle_audio_remap_entry_mask
                & handler_bit) != 0
            && envelope.battle_audio_remap_entry_values[
                handler_slot] == entry.before;
        if (!span_valid) break;
        replay.suppressed_audio_remap_entry_mask |= handler_bit;
        replay.suppressed_audio_remap_entry_values[handler_slot]
            = static_cast<std::uint8_t>(entry.before);
        if (current != entry.before)
        {
            span_valid = SafeWrite(handler_identity + 0x3E0,
                    entry.before)
                && SafeRead(handler_identity + 0x3E0, current)
                && current == entry.before;
        }
    }
    else
    {
        if (current != entry.before)
        {
            span_valid = SafeWrite(handler_identity + 0x3E0,
                    entry.before)
                && SafeRead(handler_identity + 0x3E0, current)
                && current == entry.before;
        }
    }
    if (!span_valid
        || !SafeWrite(handler_identity + 0x3E0, entry.after)
        || !SafeRead(handler_identity + 0x3E0, current)
        || current != entry.after
        || !AppendBattleAudioRemapSignature(
            static_cast<std::uint8_t>(handler_slot),
            entry.contact_type, entry.before, entry.result,
            entry.after, replay.suppressed_audio_remap_hash))
    {
        span_valid = false;
        break;
    }
    ++replay.suppressed_audio_remap_calls;
}
    return span_valid;
}

void DeterministicHookSet::ConsumeRecordedBattleAudioSourceSpan(
    OuterTickCaptureContext* batch, void* handler,
    const std::array<std::byte, 18>& semantic,
    const BattleAudioSourceJournalEntry& source) noexcept
{
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        const auto dispatch_end =
            static_cast<std::size_t>(source.first_dispatch)
            + source.dispatch_count;
        const auto remap_end = static_cast<std::size_t>(source.first_remap)
            + source.remap_count;
        const auto blueprint_end =
            static_cast<std::size_t>(source.first_blueprint)
            + source.blueprint_count;
        const auto order_end =
            static_cast<std::size_t>(source.first_presentation_order)
            + source.presentation_order_count;
        const auto terminal_end =
            static_cast<std::size_t>(source.first_terminal)
            + source.terminal_count;
        bool span_valid = source.first_dispatch
                == replay.suppressed_audio_calls
            && source.first_remap == replay.suppressed_audio_remap_calls
            && source.first_blueprint
                == replay.suppressed_audio_blueprint_calls
            && dispatch_end <= envelope.battle_audio_journal_count
            && remap_end <= envelope.battle_audio_remap_journal_count
            && blueprint_end
                <= envelope.battle_audio_blueprint_journal_count
            && terminal_end <= envelope.audio_terminal_journal_count
            && source.presentation_order_count != 0
            && order_end <= envelope.presentation_order_journal_count
            && source.presentation_order_count
                == 1u + source.dispatch_count + source.remap_count
                    + source.blueprint_count + source.terminal_count;
        std::int32_t source_contact_type{};
        std::memcpy(&source_contact_type, semantic.data() + 1,
            sizeof(source_contact_type));
        std::size_t handler_slot = maximum_battle_audio_handlers;
        const auto handler_identity =
            reinterpret_cast<std::uintptr_t>(handler);
        for (std::size_t slot = 0;
             slot < maximum_battle_audio_handlers; ++slot)
        {
            if (observed_battle_audio_handlers_[slot].load(
                    std::memory_order_acquire) == handler_identity)
            {
                handler_slot = slot;
                break;
            }
        }
        span_valid = span_valid && ConsumeRecordedBattleAudioRemaps(
            batch, handler_identity, handler_slot, source_contact_type, source);
        for (std::size_t dispatch_index = source.first_dispatch;
             span_valid && dispatch_index < dispatch_end;
             ++dispatch_index)
        {
            const auto& entry =
                envelope.battle_audio_journal[dispatch_index];
            span_valid = entry.direct == 0;
            if (!span_valid
                || !AppendBattleAudioSemantic(entry.semantic,
                    replay.suppressed_audio_sequence_hash,
                    replay.suppressed_audio_route_hash,
                    replay.suppressed_audio_payload_hash,
                    replay.suppressed_audio_position_hash))
            {
                span_valid = false;
                break;
            }
            ++replay.suppressed_audio_calls;
        }
        for (std::size_t blueprint_index = source.first_blueprint;
             span_valid && blueprint_index < blueprint_end;
             ++blueprint_index)
        {
            const auto& entry =
                envelope.battle_audio_blueprint_journal[blueprint_index];
            span_valid = entry.direct == 0
                && AppendBattleAudioBlueprintSemantic(
                    entry, replay.suppressed_audio_blueprint_hash);
            if (!span_valid) break;
            ++replay.suppressed_audio_blueprint_calls;
        }
        for (std::size_t terminal_index = source.first_terminal;
             span_valid && terminal_index < terminal_end;
             ++terminal_index)
        {
            span_valid = AppendAudioTerminalSemantic(
                envelope.audio_terminal_journal[terminal_index],
                replay.suppressed_audio_terminal_hash);
            if (!span_valid) break;
            ++replay.suppressed_audio_terminal_calls;
        }
        for (std::size_t order_index =
                 static_cast<std::size_t>(
                     source.first_presentation_order) + 1;
             span_valid && order_index < order_end; ++order_index)
        {
            const auto& entry =
                envelope.presentation_order_journal[order_index];
            bool member{};
            switch (entry.family)
            {
            case PresentationEventFamily::BattleAudioDispatch:
                member = entry.family_index >= source.first_dispatch
                    && entry.family_index < dispatch_end;
                break;
            case PresentationEventFamily::BattleAudioRemap:
                member = entry.family_index >= source.first_remap
                    && entry.family_index < remap_end;
                break;
            case PresentationEventFamily::BattleAudioBlueprint:
                member = entry.family_index >= source.first_blueprint
                    && entry.family_index < blueprint_end;
                break;
            case PresentationEventFamily::AudioTerminal:
                member = entry.family_index >= source.first_terminal
                    && entry.family_index < terminal_end;
                break;
            default:
                break;
            }
            span_valid = member && VerifyPresentationOrder(entry.family,
                entry.family_index, envelope, replay,
                batch->observation, batch->frame_counter_address);
        }
        if (!span_valid)
        {
            ++replay.audio_sequence_mismatches;
            replay.audio_journal_failure_mask |= 1u << 0;
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 8;
            replay.failure = FailureCode::PresentationFailed;
        }
}

void DeterministicHookSet::ReplayRecordedBattleAudioSource(
    OuterTickCaptureContext* batch, void* handler, void* event_record) noexcept
{
    auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        std::array<std::byte, 18> semantic{};
        const std::size_t index = replay.suppressed_audio_source_calls;
        const bool captured = CaptureBattleAudioSourceSemantic(
            event_record, semantic);
        if (index >= envelope.battle_audio_source_journal_count)
        {
            ++replay.discarded_audio_calls;
            return;
        }
        const bool source_valid = captured
            && envelope.battle_audio_source_journal[index].semantic == semantic;
        if (!source_valid)
        {
            ++replay.discarded_audio_calls;
            return;
        }
        const auto& source = envelope.battle_audio_source_journal[index];
        const auto order_begin = replay.suppressed_presentation_order_events;
        if (source.first_presentation_order != order_begin
            || source.first_dispatch != replay.suppressed_audio_calls
            || source.first_remap != replay.suppressed_audio_remap_calls
            || source.first_blueprint
                != replay.suppressed_audio_blueprint_calls
            || source.first_terminal
                != replay.suppressed_audio_terminal_calls
            || !MatchesNextPresentationOrder(
                PresentationEventFamily::BattleAudioSource,
                static_cast<std::uint32_t>(index), envelope, replay,
                batch->observation, batch->frame_counter_address))
        {
            ++replay.discarded_audio_calls;
            return;
        }
        if (!VerifyPresentationOrder(
                PresentationEventFamily::BattleAudioSource,
                static_cast<std::uint32_t>(index), envelope, replay,
                batch->observation, batch->frame_counter_address))
        {
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 12;
            replay.failure = FailureCode::PresentationFailed;
            return;
        }
        ++replay.suppressed_audio_source_calls;
        if (!captured || !AppendBattleAudioSourceSignature(event_record,
                replay.suppressed_audio_source_hash))
        {
            replay.audio_journal_failure_mask |= 1u << 3;
        }
        ConsumeRecordedBattleAudioSourceSpan(
            batch, handler, semantic, source);
}

void __fastcall DeterministicHookSet::BattleAudioPhaseChangedDetour(
    void* handler, void* phase_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_phase_changed_trampoline_
        : battle_audio_phase_changed_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioPhaseChangedFn>(
        trampoline);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch);
    const auto address = reinterpret_cast<std::uintptr_t>(handler);
    std::uint8_t deferred_log_requested{};
    std::int32_t deferred_frame_counter{};
    const bool captured = !suppress || (address != 0
        && SafeRead(address + 0x3E4, deferred_log_requested)
        && SafeRead(address + 0x3E8, deferred_frame_counter));
    if (original != nullptr) original(handler, phase_record);
    if (suppress && (!captured
        || !SafeWrite(address + 0x3E4, deferred_log_requested)
        || !SafeWrite(address + 0x3E8, deferred_frame_counter)))
    {
        if (batch->owned != nullptr)
        {
            ++batch->owned->result->presentation_failures;
            batch->owned->result->presentation_failure_mask |= 1u << 3;
            batch->owned->result->failure = FailureCode::PresentationFailed;
        }
        else if (batch->observation != nullptr)
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 15;
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

std::uint64_t __fastcall DeterministicHookSet::BattleAudioTrackingRemoveDetour(
    void* tracking_set, std::uint32_t key) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_tracking_remove_trampoline_
        : battle_audio_tracking_remove_trampoline_global_.load(
            std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch)
        && (IsObservedBattleAudioTrackingSet(tracking_set)
            || active_owned_audio_registration_depth != 0);
    if (suppress)
    {
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return 0;
    }
    const auto original = reinterpret_cast<BattleAudioTrackingRemoveFn>(
        trampoline);
    const auto result = original != nullptr ? original(tracking_set, key) : 0;
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

std::int32_t* __fastcall DeterministicHookSet::BattleAudioTrackingInsertDetour(
    void* tracking_set, std::int32_t* index, void* pair,
    std::uint8_t* replaced) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_tracking_insert_trampoline_
        : battle_audio_tracking_insert_trampoline_global_.load(
            std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch)
        && IsObservedBattleAudioTrackingSet(tracking_set);
    if (suppress)
    {
        if (index != nullptr) *index = -1;
        if (replaced != nullptr) *replaced = 0;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return index;
    }
    const auto original = reinterpret_cast<BattleAudioTrackingInsertFn>(
        trampoline);
    auto* result = original != nullptr
        ? original(tracking_set, index, pair, replaced) : index;
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __fastcall DeterministicHookSet::BattleAudioTrackingRehashDetour(
    void* tracking_set) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch)
        && IsObservedBattleAudioTrackingSet(tracking_set);
    if (!suppress)
    {
        auto* hooks = active_.load(std::memory_order_acquire);
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_tracking_rehash_trampoline_
            : battle_audio_tracking_rehash_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioTrackingRehashFn>(
            trampoline);
        if (original != nullptr) original(tracking_set);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::BattleAudioBlueprintPublishDetour(
    void* handler, void* event_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_blueprint_publish_trampoline_
        : battle_audio_blueprint_publish_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioBlueprintPublishFn>(
        trampoline);
    auto* batch = active_outer_capture_;
    std::size_t handler_slot = maximum_battle_audio_handlers;
    if (handler != nullptr)
    {
        const auto identity = reinterpret_cast<std::uintptr_t>(handler);
        for (std::size_t index = 0;
             index < observed_battle_audio_handlers_.size(); ++index)
        {
            auto& slot = observed_battle_audio_handlers_[index];
            auto observed = slot.load(std::memory_order_acquire);
            if (observed == identity)
            {
                handler_slot = index;
                break;
            }
            if (observed == 0
                && slot.compare_exchange_strong(observed, identity,
                    std::memory_order_acq_rel, std::memory_order_acquire))
            {
                handler_slot = index;
                break;
            }
        }
        if (handler_slot == maximum_battle_audio_handlers)
            battle_audio_handler_overflow_.store(
                true, std::memory_order_release);
    }
    BattleAudioBlueprintJournalEntry semantic{};
    semantic.handler_slot = static_cast<std::uint8_t>(handler_slot);
    semantic.direct = active_battle_audio_source_depth == 0 ? 1 : 0;
    const bool captured = handler_slot < maximum_battle_audio_handlers
        && CaptureBattleAudioBlueprintSemantic(
            event_record, semantic.semantic);
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto family_index = observation.battle_audio_blueprint_calls;
        ++observation.battle_audio_blueprint_calls;
        if (!AppendObservedPresentationOrder(batch->observation,
                batch->frame_counter_address,
                PresentationEventFamily::BattleAudioBlueprint, family_index))
            ++observation.presentation_order_failures;
        if (!captured || observation.battle_audio_blueprint_journal_count
                >= observation.battle_audio_blueprint_journal.size()
            || !AppendBattleAudioBlueprintSemantic(
                semantic, observation.battle_audio_blueprint_hash))
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 10;
        }
        else
        {
            observation.battle_audio_blueprint_journal[
                observation.battle_audio_blueprint_journal_count++] = semantic;
        }
    }
    const bool suppress = IsPresentationSuppressed(batch);
    if (suppress)
    {
        const bool verify = IsOwnedPresentationVerification(batch);
        if (!verify)
        {
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        const auto index = replay.suppressed_audio_blueprint_calls;
        if (verify && (!captured
                || index >= envelope.battle_audio_blueprint_journal_count
                || envelope.battle_audio_blueprint_journal[index].semantic
                    != semantic.semantic
                || envelope.battle_audio_blueprint_journal[index].handler_slot
                    != semantic.handler_slot
                || envelope.battle_audio_blueprint_journal[index].direct
                    != semantic.direct
                || !MatchesNextPresentationOrder(
                    PresentationEventFamily::BattleAudioBlueprint,
                    index, envelope, replay, batch->observation,
                    batch->frame_counter_address)))
        {
            // As with direct dispatches, discard a stale presentation-local
            // callback without advancing either journal cursor.
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        ++replay.suppressed_audio_blueprint_calls;
        if (verify && !VerifyPresentationOrder(
                PresentationEventFamily::BattleAudioBlueprint,
                index, envelope, replay, batch->observation,
                batch->frame_counter_address))
        {
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 12;
            replay.failure = FailureCode::PresentationFailed;
        }
        if (verify && (!captured
            || index >= envelope.battle_audio_blueprint_journal_count
            || envelope.battle_audio_blueprint_journal[index].semantic
                != semantic.semantic
            || envelope.battle_audio_blueprint_journal[index].handler_slot
                != semantic.handler_slot
            || envelope.battle_audio_blueprint_journal[index].direct
                != semantic.direct
            || !AppendBattleAudioBlueprintSemantic(
                semantic, replay.suppressed_audio_blueprint_hash)))
        {
            ++replay.audio_sequence_mismatches;
            replay.audio_journal_failure_mask |= 1u << 8;
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 10;
            replay.failure = FailureCode::PresentationFailed;
        }
    }
    else if (original != nullptr)
    {
        original(handler, event_record);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

