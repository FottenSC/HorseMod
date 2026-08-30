std::uint32_t __fastcall DeterministicHookSet::BattleAudioRegisterVoiceDetour(
    void* active_voice_owner, std::uint32_t cue_sheet_id,
    std::int32_t cue_id, std::uint32_t playback_flags) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->battle_audio_register_voice_trampoline_
        : battle_audio_register_voice_trampoline_global_.load(
            std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioRegisterVoiceFn>(
        trampoline);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch);
    const auto return_address = reinterpret_cast<std::uintptr_t>(
        _ReturnAddress());
    const auto return_rva = hooks != nullptr && hooks->image_base_ != 0
            && return_address >= hooks->image_base_
        ? static_cast<std::uint32_t>(return_address - hooks->image_base_)
        : 0;
    AudioOwnerSelector owner{};
    std::uint32_t frame{};
    const bool owner_resolved = batch != nullptr
        && hooks != nullptr
        && hooks->ResolveAudioOwner(
            reinterpret_cast<std::uintptr_t>(active_voice_owner), owner);
    if (batch != nullptr && hooks != nullptr && !owner_resolved)
        RecordUnresolvedAudioOwner(*batch->observation, hooks->image_base_,
            reinterpret_cast<std::uintptr_t>(active_voice_owner),
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
            hooks->audio_owner_resolver_);
    const bool owned_terminal = owner_resolved
        && SafeRead(batch->frame_counter_address, frame)
        && batch->observation != nullptr
        && batch->observation->audio_terminal_calls < audio_ordinals_per_frame;
    const bool verify_recorded = IsOwnedPresentationVerification(batch);
    std::uint32_t terminal_ordinal{};
    if (owned_terminal)
    {
        terminal_ordinal = verify_recorded
            ? batch->owned->result->suppressed_audio_terminal_calls
            : batch->observation->audio_terminal_calls;
    }
    const auto logical_id = owned_terminal
        ? MakeLogicalAudioPlaybackId(frame, terminal_ordinal)
        : audio_invalid_playback_id;
    if (suppress && (!owned_terminal
            || logical_id == audio_invalid_playback_id))
    {
        ++batch->observation->battle_audio_signature_failures;
        batch->observation->battle_audio_signature_failure_mask |= 1u << 12;
        if (batch->owned != nullptr)
            batch->owned->result->failure = FailureCode::PresentationFailed;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return audio_invalid_playback_id;
    }
    if (suppress)
    {
        const auto admission = InspectAudioCreateAdmission(
            hooks->image_base_,
            reinterpret_cast<std::uintptr_t>(active_voice_owner),
            cue_sheet_id, cue_id);
        if (admission == AudioCreateAdmission::Rejected)
        {
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return audio_invalid_playback_id;
        }
        if (admission != AudioCreateAdmission::Admitted)
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 12;
            if (batch->owned != nullptr)
                batch->owned->result->failure = FailureCode::PresentationFailed;
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return audio_invalid_playback_id;
        }
        const AudioTerminalEvent event{AudioTerminalOperation::Create, owner,
            logical_id, cue_sheet_id, cue_id, playback_flags};
        if (!RecordAudioTerminal(batch, event, return_rva)
            && batch->owned != nullptr)
            batch->owned->result->failure = FailureCode::PresentationFailed;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return logical_id;
    }
    const auto result = original != nullptr
        ? original(active_voice_owner, cue_sheet_id, cue_id, playback_flags)
        : audio_invalid_playback_id;
    if (result != audio_invalid_playback_id && batch != nullptr)
    {
        const AudioTerminalEvent event{AudioTerminalOperation::Create, owner,
            logical_id, cue_sheet_id, cue_id, playback_flags};
        bool mapped = owned_terminal && event.valid()
            && hooks->audio_playback_map_.Insert(
                hooks->audio_owner_resolver_.epoch(), owner, logical_id, result);
        if (!mapped && owned_terminal && event.valid())
        {
            // Logical/native mappings outlive the corresponding native voices.
            // When the fixed-capacity map fills, consult each owner's embedded
            // active-voice set and retire only entries the native lifecycle has
            // already removed, then retry this exact mapping once.
            using FindActiveVoiceFn = void* (__fastcall*)(void*, std::int32_t);
            const auto find_active = reinterpret_cast<FindActiveVoiceFn>(
                hooks->image_base_
                + Schema::Sc6FrameLayout::battle_audio_find_active_voice_rva);
            const auto epoch = hooks->audio_owner_resolver_.epoch();
            hooks->audio_playback_map_.PruneInactive(epoch,
                [&](AudioOwnerSelector mapped_owner,
                    std::uint32_t native_id) noexcept
                {
                    std::uintptr_t mapped_owner_address{};
                    return hooks->audio_owner_resolver_.ResolveOwner(
                            epoch, mapped_owner, mapped_owner_address)
                        && find_active(reinterpret_cast<void*>(
                                mapped_owner_address + 0x38),
                            static_cast<std::int32_t>(native_id)) != nullptr;
                });
            mapped = hooks->audio_playback_map_.Insert(
                epoch, owner, logical_id, result);
        }
        if (!mapped
            || !RecordAudioTerminal(batch, event, return_rva))
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask |= 1u << 12;
        }
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __fastcall DeterministicHookSet::BattleAudioAppendCommandDetour(
    void* active_voice_owner, void* command_record) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch);
    struct CommandRecord
    {
        std::uint32_t operation{};
        std::uint32_t playback_id{};
        std::uint32_t immediate{};
        std::uint32_t reserved{};
        std::uint64_t value{};
    };
    static_assert(sizeof(CommandRecord) == 0x18);
    CommandRecord command{};
    AudioOwnerSelector owner{};
    bool represented = true;
    AudioTerminalEvent event{};
    if (batch != nullptr && hooks != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(command_record), command)
        && hooks->ResolveAudioOwner(
            reinterpret_cast<std::uintptr_t>(active_voice_owner), owner))
    {
        if (command.operation == 1)
        {
            event = {AudioTerminalOperation::StopAll, owner,
                audio_invalid_playback_id, 0, -1, command.immediate};
        }
        else if (command.operation == 2)
        {
            auto logical = command.playback_id;
            if (IsNativeAudioPlaybackId(logical))
            {
                std::uint32_t mapped{};
                if (hooks->audio_playback_map_.LogicalForNative(
                        hooks->audio_owner_resolver_.epoch(), owner,
                        logical, mapped))
                    logical = mapped;
                else
                {
                    std::uint32_t frame{};
                    const bool verify_recorded =
                        IsOwnedPresentationVerification(batch);
                    const auto ordinal = verify_recorded
                        ? batch->owned->result->suppressed_audio_terminal_calls
                        : batch->observation->audio_terminal_calls;
                    const auto adopted = SafeRead(
                            batch->frame_counter_address, frame)
                        ? MakeLogicalAudioPlaybackId(frame, ordinal)
                        : audio_invalid_playback_id;
                    if (adopted == audio_invalid_playback_id
                        || !hooks->audio_playback_map_.Insert(
                            hooks->audio_owner_resolver_.epoch(), owner,
                            adopted, logical))
                        represented = false;
                    else
                        logical = adopted;
                }
            }
            event = {AudioTerminalOperation::StopOne, owner, logical,
                0, -1, command.immediate};
        }
        else if (command.operation != 0)
        {
            represented = false;
        }
        if (command.operation != 0
            && (!represented || !event.valid()
                || !RecordAudioTerminal(batch, event)))
        {
            ++batch->observation->battle_audio_signature_failures;
            batch->observation->battle_audio_signature_failure_mask
                |= 1u << 13;
            if (suppress)
            {
                if (batch->owned != nullptr)
                    batch->owned->result->failure =
                        FailureCode::PresentationFailed;
            }
        }
        if (!suppress && represented && event.valid()
            && command.operation == 1)
        {
            hooks->audio_playback_map_.RemoveOwner(
                hooks->audio_owner_resolver_.epoch(), owner);
        }
        else if (!suppress && represented && event.valid()
            && command.operation == 2)
        {
            static_cast<void>(hooks->audio_playback_map_.RemoveOne(
                hooks->audio_owner_resolver_.epoch(), owner,
                event.logical_playback_id));
        }
    }
    else if (batch != nullptr && command_record != nullptr)
    {
        if (hooks != nullptr)
            RecordUnresolvedAudioOwner(*batch->observation,
                hooks->image_base_,
                reinterpret_cast<std::uintptr_t>(active_voice_owner),
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
                hooks->audio_owner_resolver_);
        ++batch->observation->battle_audio_signature_failures;
        batch->observation->battle_audio_signature_failure_mask |= 1u << 13;
        if (suppress)
        {
            if (batch->owned != nullptr)
                batch->owned->result->failure = FailureCode::PresentationFailed;
        }
    }
    if (!suppress)
    {
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_append_command_trampoline_
            : battle_audio_append_command_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioAppendCommandFn>(
            trampoline);
        if (original != nullptr) original(active_voice_owner, command_record);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::BattleAudioStopAllDetour(
    void* active_voice_owner, std::uint8_t control) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const auto owner_identity = reinterpret_cast<std::uintptr_t>(
        active_voice_owner);
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto owner_slot = ResolveBatchOwnerSlot(owner_identity,
            observation.battle_audio_stop_all_owner_identities,
            observation.battle_audio_stop_all_owner_identity_count);
        const BattleAudioStopAllJournalEntry semantic{
            static_cast<std::uint8_t>(owner_slot), control};
        const bool semantic_ok =
            owner_slot < observation.battle_audio_stop_all_owner_identities.size();
        const auto family_index = observation.battle_audio_stop_all_calls;
        ++observation.battle_audio_stop_all_calls;
        if (!AppendObservedPresentationOrder(batch->observation,
                batch->frame_counter_address,
                PresentationEventFamily::BattleAudioStopAll, family_index))
            ++observation.presentation_order_failures;
        if (!semantic_ok
            || observation.battle_audio_stop_all_journal_count
                >= observation.battle_audio_stop_all_journal.size()
            || !AppendBattleAudioStopAllSemantic(
                semantic, observation.battle_audio_stop_all_hash))
        {
            ++observation.battle_audio_signature_failures;
            observation.battle_audio_signature_failure_mask |= 1u << 11;
        }
        else
        {
            observation.battle_audio_stop_all_journal[
                observation.battle_audio_stop_all_journal_count++] = semantic;
        }
    }
    const bool suppress = IsPresentationSuppressed(batch);
    AudioOwnerSelector stable_owner{};
    const AudioTerminalEvent terminal{
        AudioTerminalOperation::StopAll, stable_owner,
        audio_invalid_playback_id, 0, -1, control};
    const bool stable_owner_ok = hooks != nullptr
        && hooks->ResolveAudioOwner(owner_identity, stable_owner);
    AudioTerminalEvent stable_terminal = terminal;
    stable_terminal.owner = stable_owner;
    // The normal terminal reaches AppendCommandRecord inside the native
    // StopAll implementation, where the generic command detour records it.
    // Suppressed resimulation skips that native call, so record the equivalent
    // terminal here only on the suppressed path.
    if (suppress
        && (!stable_owner_ok || !RecordAudioTerminal(batch, stable_terminal)))
    {
        if (hooks != nullptr && !stable_owner_ok)
            RecordUnresolvedAudioOwner(*batch->observation,
                hooks->image_base_, owner_identity,
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
                hooks->audio_owner_resolver_);
        ++batch->observation->battle_audio_signature_failures;
        batch->observation->battle_audio_signature_failure_mask |= 1u << 12;
        if (batch->owned != nullptr)
            batch->owned->result->failure = FailureCode::PresentationFailed;
    }
    if (IsOwnedPresentationVerification(batch))
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        constexpr bool verify = true;
        const auto owner_slot = ResolveBatchOwnerSlot(owner_identity,
            replay.suppressed_audio_stop_all_owner_identities,
            replay.suppressed_audio_stop_all_owner_identity_count);
        const BattleAudioStopAllJournalEntry semantic{
            static_cast<std::uint8_t>(owner_slot), control};
        const bool semantic_ok = owner_slot
            < replay.suppressed_audio_stop_all_owner_identities.size();
        const auto index = replay.suppressed_audio_stop_all_calls++;
        if (verify && !VerifyPresentationOrder(
                PresentationEventFamily::BattleAudioStopAll,
                index, envelope, replay, batch->observation,
                batch->frame_counter_address))
        {
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 12;
            replay.failure = FailureCode::PresentationFailed;
        }
        if (verify && (!semantic_ok
            || index >= envelope.battle_audio_stop_all_journal_count
            || envelope.battle_audio_stop_all_journal[index].owner_slot
                != semantic.owner_slot
            || envelope.battle_audio_stop_all_journal[index].control
                != semantic.control
            || !AppendBattleAudioStopAllSemantic(
                semantic, replay.suppressed_audio_stop_all_hash)))
        {
            ++replay.audio_sequence_mismatches;
            replay.audio_journal_failure_mask |= 1u << 5;
            ++replay.presentation_failures;
            replay.presentation_failure_mask |= 1u << 11;
            replay.failure = FailureCode::PresentationFailed;
        }
    }
    else if (!suppress)
    {
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_stop_all_trampoline_
            : battle_audio_stop_all_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioStopAllFn>(trampoline);
        if (original != nullptr) original(active_voice_owner, control);
        if (hooks != nullptr && stable_owner_ok)
            hooks->audio_playback_map_.RemoveOwner(
                hooks->audio_owner_resolver_.epoch(), stable_owner);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void __fastcall DeterministicHookSet::BattleAudioAppendParameterDetour(
    void* shared_player, void* parameter_name, float value) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    auto* batch = active_outer_capture_;
    const bool suppress = IsPresentationSuppressed(batch);
    struct FStringView
    {
        std::uintptr_t data{};
        std::int32_t length{};
        std::int32_t capacity{};
    };
    std::uintptr_t owner_identity{};
    AudioOwnerSelector owner{};
    FStringView requested{};
    std::uint32_t parameter_index = UINT32_MAX;
    constexpr std::uintptr_t parameter_table_rva = 0x406f060;
    if (hooks != nullptr && shared_player != nullptr
        && SafeRead(reinterpret_cast<std::uintptr_t>(shared_player),
            owner_identity)
        && hooks->ResolveAudioOwner(owner_identity, owner)
        && SafeRead(reinterpret_cast<std::uintptr_t>(parameter_name), requested)
        && requested.length >= 0 && requested.length <= 26
        && requested.data != 0)
    {
        for (std::uint32_t index = 0; index < 25; ++index)
        {
            FStringView candidate{};
            if (!SafeRead(hooks->image_base_ + parameter_table_rva
                    + static_cast<std::uintptr_t>(index) * 0x10,
                    candidate))
                break;
            if (candidate.length == requested.length && candidate.data != 0
                && SafeEqual(reinterpret_cast<const void*>(candidate.data),
                    reinterpret_cast<const void*>(requested.data),
                    // Native FString ArrayNum includes the terminating NUL.
                    // Comparing one additional wchar_t reads beyond both
                    // logical strings and rejects otherwise identical names.
                    static_cast<std::size_t>(requested.length)
                        * sizeof(wchar_t)))
            {
                parameter_index = index;
                break;
            }
        }
    }
    std::uint32_t value_bits{};
    std::memcpy(&value_bits, &value, sizeof(value_bits));
    const AudioTerminalEvent event{AudioTerminalOperation::SetParameter,
        owner, audio_invalid_playback_id, parameter_index, -1, value_bits};
    if (batch != nullptr
        && (parameter_index == UINT32_MAX || !event.valid()
            || !RecordAudioTerminal(batch, event)))
    {
        if (hooks != nullptr && !owner.valid())
            RecordUnresolvedAudioOwner(*batch->observation,
                hooks->image_base_, owner_identity,
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
                hooks->audio_owner_resolver_);
        ++batch->observation->battle_audio_signature_failures;
        batch->observation->battle_audio_signature_failure_mask |= 1u << 14;
        if (batch->owned != nullptr)
            batch->owned->result->failure = FailureCode::PresentationFailed;
    }
    if (!suppress)
    {
        const auto trampoline = hooks != nullptr
            ? hooks->battle_audio_append_parameter_trampoline_
            : battle_audio_append_parameter_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<BattleAudioAppendParameterFn>(
            trampoline);
        if (original != nullptr) original(shared_player, parameter_name, value);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

void* __fastcall DeterministicHookSet::ParticleSpawnDetour(
    void* world_context, void* particle_system, const void* location,
    const void* rotation, const void* scale, bool auto_activate) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto trampoline = hooks != nullptr
        ? hooks->particle_spawn_trampoline_
        : particle_spawn_trampoline_global_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<ParticleSpawnFn>(trampoline);
    auto* batch = active_outer_capture_;
    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto image_base = hooks != nullptr ? hooks->image_base_ : 0;
    const auto route = ClassifyParticleRoute(image_base, return_address);
    ParticleSpawnJournalEntry semantic{};
    const bool semantic_ok = CaptureParticleSpawnSemantic(route,
        world_context, particle_system, location, rotation, scale,
        auto_activate,
        hooks != nullptr ? &hooks->stage_break_presentation_identity_ : nullptr,
        semantic);
    if (active_stage_commit != nullptr)
    {
        auto& commit = *active_stage_commit;
        if (!semantic_ok || commit.expected == nullptr
            || commit.particle_index >= commit.expected->particle_count
            || commit.expected->particles[commit.particle_index].semantic
                != semantic.semantic)
        {
            commit.failure = FailureCode::PresentationFailed;
            callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return nullptr;
        }
        ++commit.particle_index;
        void* result = original != nullptr
            ? original(world_context, particle_system, location, rotation,
                scale, auto_activate)
            : nullptr;
        if (result == nullptr) commit.failure = FailureCode::PresentationFailed;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }
    if (batch != nullptr && batch->observation != nullptr)
    {
        auto& observation = *batch->observation;
        const auto family_index = observation.particle_spawn_calls;
        ++observation.particle_spawn_calls;
        if (!AppendObservedPresentationOrder(batch->observation,
                batch->frame_counter_address,
                PresentationEventFamily::ParticleSpawn, family_index))
            ++observation.presentation_order_failures;
        if (!semantic_ok || !AppendParticleSpawnSemantic(
                semantic, observation.particle_spawn_hash)
            || observation.particle_spawn_journal_count
                >= observation.particle_spawn_journal.size())
        {
            ++observation.particle_signature_failures;
        }
        else
        {
            observation.particle_spawn_journal[
                observation.particle_spawn_journal_count++] = semantic;
        }
    }
    const bool suppress = IsPresentationSuppressed(batch);
    if (!suppress)
    {
        void* result = original != nullptr
            ? original(world_context, particle_system, location, rotation,
                scale, auto_activate)
            : nullptr;
        callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    if (batch->owned != nullptr)
    {
        auto& result = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        const bool verify = batch->owned->request->presentation_mode
            == OwnedBatchPresentationMode::VerifyRecorded;
        const auto index = result.suppressed_particle_spawn_calls++;
        if (verify && !VerifyPresentationOrder(
                PresentationEventFamily::ParticleSpawn, index, envelope,
                result, batch->observation, batch->frame_counter_address))
        {
            ++result.presentation_failures;
            result.presentation_failure_mask |= 1u << 12;
            result.failure = FailureCode::PresentationFailed;
        }
        const bool sequence_ok = !verify || (semantic_ok
            && index < envelope.particle_spawn_journal_count
            && envelope.particle_spawn_journal[index].semantic
                == semantic.semantic
            && AppendParticleSpawnSemantic(
                semantic, result.suppressed_particle_spawn_hash));
        if (!sequence_ok || route == 0 || route == 4)
        {
            ++result.unknown_particle_routes;
            ++result.presentation_failures;
            result.presentation_failure_mask |= 1u << 5;
            result.failure = FailureCode::PresentationFailed;
        }
    }
    else if (!semantic_ok || route == 0 || route == 4)
    {
        ++batch->observation->particle_signature_failures;
    }
    void* shadow = particle_shadow_pool.Acquire();
    if (shadow == nullptr)
    {
        if (batch->owned != nullptr)
        {
            auto& result = *batch->owned->result;
            ++result.presentation_failures;
            result.presentation_failure_mask |= 1u << 6;
            result.failure = FailureCode::CapacityExceeded;
        }
        else
        {
            ++batch->observation->particle_signature_failures;
        }
        // Preserve the non-null native contract while the owned batch fails
        // closed; slot zero is already initialized after pool exhaustion.
        shadow = particle_shadow_pool.slots[0].bytes.data();
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return shadow;
}

void __fastcall DeterministicHookSet::ParticleFinishedBindDetour(
    void* delegate, void* owner, void* callback,
    std::uint64_t callback_name) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* batch = active_outer_capture_;
    const bool shadow = particle_shadow_pool.ContainsDelegate(delegate);
    if (shadow && IsPresentationSuppressed(batch))
    {
        if (batch->owned != nullptr)
            ++batch->owned->result->suppressed_particle_finished_binds;
    }
    else
    {
        auto* hooks = active_.load(std::memory_order_acquire);
        const auto trampoline = hooks != nullptr
            ? hooks->particle_finished_bind_trampoline_
            : particle_finished_bind_trampoline_global_.load(
                std::memory_order_acquire);
        const auto original = reinterpret_cast<ParticleFinishedBindFn>(trampoline);
        if (original != nullptr) original(delegate, owner, callback, callback_name);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

int __cdecl DeterministicHookSet::UcrtRandDetour() noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto original = hooks != nullptr ? hooks->original_rand_ : nullptr;
    int result{};
    if (hooks != nullptr && hooks->ucrt_broker_ != nullptr)
    {
        const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto return_rva = return_address >= hooks->image_base_
            ? return_address - hooks->image_base_ : 0;
        result = hooks->ucrt_broker_->HandleRand(
            ::GetCurrentThreadId(), return_rva, original);
    }
    else if (original != nullptr)
    {
        result = original();
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

void __cdecl DeterministicHookSet::UcrtSrandDetour(unsigned int seed) noexcept
{
    callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    auto* hooks = active_.load(std::memory_order_acquire);
    const auto original = hooks != nullptr ? hooks->original_srand_ : nullptr;
    if (hooks != nullptr && hooks->ucrt_broker_ != nullptr)
    {
        const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const auto return_rva = return_address >= hooks->image_base_
            ? return_address - hooks->image_base_ : 0;
        hooks->ucrt_broker_->HandleSrand(
            ::GetCurrentThreadId(), return_rva, seed, original);
    }
    else if (original != nullptr)
    {
        original(seed);
    }
    callbacks_in_flight_.fetch_sub(1, std::memory_order_acq_rel);
}

bool DeterministicHookSet::InstallUcrtIatHooks() noexcept
{
    rand_iat_slot_ = image_base_ + Schema::Sc6UcrtLayout::rand_iat_rva;
    srand_iat_slot_ = image_base_ + Schema::Sc6UcrtLayout::srand_iat_rva;
    if (!SafeRead(rand_iat_slot_, original_rand_)
        || !SafeRead(srand_iat_slot_, original_srand_)
        || original_rand_ == nullptr || original_srand_ == nullptr)
    {
        return false;
    }
    DWORD old_protect{};
    if (!::VirtualProtect(reinterpret_cast<void*>(rand_iat_slot_), sizeof(void*),
            PAGE_READWRITE, &old_protect))
        return false;
    auto* rand_slot = reinterpret_cast<void* volatile*>(rand_iat_slot_);
    const auto prior_rand = ::InterlockedCompareExchangePointer(
        rand_slot, reinterpret_cast<void*>(&UcrtRandDetour),
        reinterpret_cast<void*>(original_rand_));
    DWORD ignored{};
    ::VirtualProtect(reinterpret_cast<void*>(rand_iat_slot_), sizeof(void*),
        old_protect, &ignored);
    if (prior_rand != reinterpret_cast<void*>(original_rand_)) return false;

    if (!::VirtualProtect(reinterpret_cast<void*>(srand_iat_slot_), sizeof(void*),
            PAGE_READWRITE, &old_protect))
    {
        UninstallUcrtIatHooks();
        return false;
    }
    auto* srand_slot = reinterpret_cast<void* volatile*>(srand_iat_slot_);
    const auto prior_srand = ::InterlockedCompareExchangePointer(
        srand_slot, reinterpret_cast<void*>(&UcrtSrandDetour),
        reinterpret_cast<void*>(original_srand_));
    ::VirtualProtect(reinterpret_cast<void*>(srand_iat_slot_), sizeof(void*),
        old_protect, &ignored);
    if (prior_srand != reinterpret_cast<void*>(original_srand_))
    {
        UninstallUcrtIatHooks();
        return false;
    }
    return true;
}

void DeterministicHookSet::UninstallUcrtIatHooks() noexcept
{
    const auto restore = [](std::uintptr_t slot, void* hook, void* original) {
        if (slot == 0 || original == nullptr) return;
        DWORD old_protect{};
        if (!::VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*),
                PAGE_READWRITE, &old_protect)) return;
        ::InterlockedCompareExchangePointer(
            reinterpret_cast<void* volatile*>(slot), original, hook);
        DWORD ignored{};
        ::VirtualProtect(reinterpret_cast<void*>(slot), sizeof(void*),
            old_protect, &ignored);
    };
    restore(srand_iat_slot_, reinterpret_cast<void*>(&UcrtSrandDetour),
        reinterpret_cast<void*>(original_srand_));
    restore(rand_iat_slot_, reinterpret_cast<void*>(&UcrtRandDetour),
        reinterpret_cast<void*>(original_rand_));
    srand_iat_slot_ = 0;
    rand_iat_slot_ = 0;
}

void DeterministicHookSet::EmitFrameFencepost(void* battle_manager) noexcept
{
    FrameFencepostObservation observation{};
    observation.battle_manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    observation.thread_id = ::GetCurrentThreadId();
    CaptureFencepostManagerState(battle_manager, observation);
    CaptureFencepostInputState(observation);
    FinalizeFrameFencepost(observation);
}

void DeterministicHookSet::CaptureFencepostManagerState(
    void* battle_manager, FrameFencepostObservation& observation) noexcept
{
    if (SafeRead(
            image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
            observation.frame_counter))
    {
        observation.read_mask |= 0x1;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6ReplayLayout::manager_status,
            observation.round_state))
    {
        observation.read_mask |= 0x2;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_repeat_pending,
            observation.repeat_pending))
    {
        observation.read_mask |= 0x4;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_pending_move_state,
            observation.pending_move_state))
    {
        observation.read_mask |= 0x80;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_game_round_cursor,
            observation.manager_game_round_cursor))
    {
        observation.read_mask |= 0x100;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_game_time_cursor,
            observation.manager_game_time_cursor))
    {
        observation.read_mask |= 0x200;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_round_state_frame,
            observation.round_state_frame))
    {
        observation.read_mask |= 0x400;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_unpause_countdown,
            observation.unpause_countdown))
    {
        observation.read_mask |= 0x800;
    }
    if (battle_manager != nullptr
        && SafeRead(
            observation.battle_manager + Schema::Sc6FrameLayout::manager_input_log,
            observation.input_log)
        && observation.input_log != 0
        && SafeRead(
            observation.input_log + Schema::Sc6FrameLayout::input_log_game_round,
            observation.game_round)
        && SafeRead(
            observation.input_log + Schema::Sc6FrameLayout::input_log_game_time,
            observation.game_time))
    {
        observation.read_mask |= 0x8;
    }
}

void DeterministicHookSet::CaptureFencepostInputState(
    FrameFencepostObservation& observation) noexcept
{
    std::int32_t input_delay{};
    if (observation.input_log != 0
        && SafeRead(observation.input_log
                + Schema::Sc6FrameLayout::input_log_input_delay,
            input_delay)
        && SafeRead(observation.input_log
                + Schema::Sc6FrameLayout::input_log_update_time,
            observation.input_update_time))
    {
        const std::int32_t source_frame =
            observation.game_time - input_delay - 1;
        if (source_frame < 0)
        {
            observation.source_rows_observed = true;
            observation.read_mask |= 0x3000;
        }
        else
        {
            bool complete = true;
            for (std::size_t slot = 0; slot < 2; ++slot)
            {
                const auto row = observation.input_log
                    + Schema::Sc6FrameLayout::input_log_cache
                    + (slot * Schema::Sc6FrameLayout::input_log_cache_rows_per_player
                        + (static_cast<std::uint32_t>(source_frame) & 0x1ffu))
                        * Schema::Sc6FrameLayout::input_log_cache_row_stride;
                complete = SafeRead(row, observation.source_rows[slot])
                    && observation.source_rows[slot].filled == 1
                    && observation.source_rows[slot].game_round
                        == observation.game_round
                    && observation.source_rows[slot].frame_index
                        == static_cast<std::uint32_t>(source_frame)
                    && complete;
            }
            if (complete)
            {
                observation.source_rows_observed = true;
                observation.read_mask |= 0x3000;
            }
        }
    }
    std::int32_t player_count{};
    if (observation.battle_manager != 0
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_input_pair_array,
            observation.input_pair_array)
        && SafeRead(
            observation.battle_manager
                + Schema::Sc6FrameLayout::manager_active_player_count,
            player_count)
        && observation.input_pair_array != 0 && player_count == 2)
    {
        observation.read_mask |= 0x10;
        if (SafeRead(observation.input_pair_array, observation.inputs[0]))
        {
            observation.read_mask |= 0x20;
        }
        if (SafeRead(
                observation.input_pair_array + sizeof(PlayerInput),
                observation.inputs[1]))
        {
            observation.read_mask |= 0x40;
        }
    }
}

void DeterministicHookSet::FinalizeFrameFencepost(
    FrameFencepostObservation& observation) noexcept
{
    OuterTickCaptureContext* batch = active_outer_capture_;
    if (batch != nullptr && batch->observation != nullptr
        && batch->observation->battle_manager == observation.battle_manager)
    {
        observation.outer_batch_id = batch->observation->batch_id;
        observation.input_filter_observed = batch->input_filter_observed;
        observation.input_filter_invocations = batch->input_filter_invocations;
        observation.authoritative_input_requested =
            batch->observation->authoritative_input_requested;
        observation.authoritative_input_applied =
            batch->observation->authoritative_input_applied;
        observation.authoritative_input_round_barrier =
            batch->observation->authoritative_input_round_barrier;
        observation.authoritative_input_failed_closed =
            batch->observation->authoritative_input_failed_closed;
        std::copy(std::begin(batch->pre_filter_inputs),
            std::end(batch->pre_filter_inputs), observation.pre_filter_inputs);
        if (batch->input_filter_observed
            && (batch->post_filter_inputs[0] != observation.inputs[0]
                || batch->post_filter_inputs[1] != observation.inputs[1]))
        {
            observation.input_filter_observed = false;
        }
        if (batch->has_previous_coordinate
            && batch->previous_game_round == observation.game_round
            && batch->previous_game_time == observation.game_time)
        {
            ++batch->observation->same_input_time_coordinates;
        }
        batch->previous_game_round = observation.game_round;
        batch->previous_game_time = observation.game_time;
        batch->has_previous_coordinate = true;
        ++batch->observation->observed_coordinates;
        if (observation.repeat_pending != 0)
            ++batch->observation->repeat_pending_coordinates;
    }
    if (batch != nullptr && batch->owned != nullptr)
    {
        auto& execution = *batch->owned;
        const auto index = execution.result->observed_coordinates;
        if (execution.result->failure == FailureCode::None
            && (index >= execution.request->coordinates.size()
                || execution.invocations_for_coordinate != 1
                || observation.frame_counter
                    != execution.request->coordinates[index].frame
                || !observation.input_filter_observed))
        {
            execution.result->failure = FailureCode::AdvanceFailed;
        }
        if (execution.result->failure == FailureCode::None
            && index == execution.request->landing_offset)
        {
            const Status captured = execution.request->capture_landing(
                execution.request->landing_user,
                execution.request->coordinates[index]);
            if (!captured.ok()) execution.result->failure = captured.code;
            else execution.result->landing_captured = true;
        }
        if (execution.result->failure == FailureCode::None
            && execution.request->capture_coordinate != nullptr)
        {
            const Status captured = execution.request->capture_coordinate(
                execution.request->coordinate_capture_user,
                execution.request->coordinates[index], index);
            if (!captured.ok()) execution.result->failure = captured.code;
        }
        ++execution.result->observed_coordinates;
        execution.invocations_for_coordinate = 0;
    }
    else
    {
        callbacks_.frame_fencepost(callbacks_.user, observation);
    }
}

void DeterministicHookSet::CaptureOuterTickState(
    void* battle_manager,
    OuterTickState& state,
    std::uint16_t& read_mask,
    std::uint16_t frame_bit,
    std::uint16_t state_bit,
    std::uint16_t input_bit,
    std::uint16_t cursor_bit) noexcept
{
    const auto manager = reinterpret_cast<std::uintptr_t>(battle_manager);
    if (SafeRead(
            image_base_ + Schema::Sc6FrameLayout::frame_counter_rva,
            state.frame_counter))
    {
        read_mask |= frame_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_main_state,
            state.main_state)
        && SafeRead(
            manager + Schema::Sc6ReplayLayout::manager_status,
            state.round_state))
    {
        read_mask |= state_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_input_log,
            state.input_log)
        && state.input_log != 0
        && SafeRead(
            state.input_log + Schema::Sc6FrameLayout::input_log_game_round,
            state.input_game_round)
        && SafeRead(
            state.input_log + Schema::Sc6FrameLayout::input_log_game_time,
            state.input_game_time))
    {
        read_mask |= input_bit;
    }
    if (manager != 0
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_game_round_cursor,
            state.manager_game_round_cursor)
        && SafeRead(
            manager + Schema::Sc6FrameLayout::manager_game_time_cursor,
            state.manager_game_time_cursor))
    {
        read_mask |= cursor_bit;
    }
}

void DeterministicHookSet::EmitReplayExit(void* replay_state) noexcept
{
    const ReplayExitObservation observation{
        reinterpret_cast<std::uintptr_t>(replay_state),
        ::GetCurrentThreadId()};
    callbacks_.replay_exit(callbacks_.user, observation);
}

bool DeterministicHookSet::IsObservedBattleAudioTrackingSet(
    const void* tracking_set) noexcept
{
    constexpr std::uintptr_t tracking_lanes_offset = 0x3D0;
    constexpr std::uintptr_t tracking_lane_count_offset = 0x3D8;
    constexpr std::uintptr_t tracking_lane_stride = 0x50;
    constexpr std::int32_t maximum_tracking_lanes = 16;
    const auto candidate = reinterpret_cast<std::uintptr_t>(tracking_set);
    if (candidate == 0) return false;
    for (const auto& observed : observed_battle_audio_handlers_)
    {
        const auto handler = observed.load(std::memory_order_acquire);
        std::uintptr_t lanes{};
        std::int32_t count{};
        if (handler == 0 || !SafeRead(handler + tracking_lanes_offset, lanes)
            || !SafeRead(handler + tracking_lane_count_offset, count)
            || lanes == 0 || count <= 0 || count > maximum_tracking_lanes
            || candidate < lanes)
        {
            continue;
        }
        const auto delta = candidate - lanes;
        if (delta % tracking_lane_stride == 0
            && delta / tracking_lane_stride < static_cast<std::uintptr_t>(count))
        {
            return true;
        }
    }
    return false;
}

void DeterministicHookSet::ClearState() noexcept
{
    suppress_presentation_next_outer_tick_.store(
        false, std::memory_order_release);
    stage_break_presentation_identity_.Invalidate();
    audio_owner_resolver_.Clear();
    audio_playback_map_.Clear();
    audio_graph_battle_manager_ = 0;
    audio_graph_epoch_counter_ = 0;
    audio_graph_failure_stage_ = 0;
    gameplay_xorshift96_detour_.reset();
    movevm_evaluate_if_detour_.reset();
    movevm_transition_author_07_detour_.reset();
    battle_audio_append_parameter_detour_.reset();
    particle_finished_bind_detour_.reset();
    particle_spawn_detour_.reset();
    battle_audio_stop_all_detour_.reset();
    battle_audio_append_command_detour_.reset();
    battle_audio_register_voice_detour_.reset();
    battle_audio_blueprint_publish_detour_.reset();
    battle_audio_tracking_rehash_detour_.reset();
    battle_audio_tracking_insert_detour_.reset();
    battle_audio_tracking_remove_detour_.reset();
    battle_audio_contact_handler_detour_.reset();
    battle_audio_phase_changed_detour_.reset();
    battle_audio_remap_detour_.reset();
    battle_audio_dispatch_detour_.reset();
    stage_break_dispatch_detour_.reset();
    stage_break_barrier_detour_.reset();
    stage_break_wall_detour_.reset();
    callback_executor_detour_.reset();
    outer_tick_detour_.reset();
    replay_post_tick_detour_.reset();
    frame_fencepost_detour_.reset();
    replay_post_tick_trampoline_ = 0;
    frame_fencepost_trampoline_ = 0;
    outer_tick_trampoline_ = 0;
    callback_executor_trampoline_ = 0;
    stage_break_wall_trampoline_ = 0;
    stage_break_barrier_trampoline_ = 0;
    stage_break_dispatch_trampoline_ = 0;
    battle_audio_dispatch_trampoline_ = 0;
    battle_audio_remap_trampoline_ = 0;
    battle_audio_contact_handler_trampoline_ = 0;
    battle_audio_phase_changed_trampoline_ = 0;
    battle_audio_tracking_remove_trampoline_ = 0;
    battle_audio_tracking_insert_trampoline_ = 0;
    battle_audio_tracking_rehash_trampoline_ = 0;
    battle_audio_blueprint_publish_trampoline_ = 0;
    battle_audio_register_voice_trampoline_ = 0;
    battle_audio_append_command_trampoline_ = 0;
    battle_audio_stop_all_trampoline_ = 0;
    battle_audio_append_parameter_trampoline_ = 0;
    particle_spawn_trampoline_ = 0;
    particle_finished_bind_trampoline_ = 0;
    gameplay_xorshift96_trampoline_ = 0;
    movevm_evaluate_if_trampoline_ = 0;
    movevm_transition_author_07_trampoline_ = 0;
    next_outer_batch_id_ = 0;
    replay_post_tick_trampoline_global_.store(0, std::memory_order_release);
    frame_fencepost_trampoline_global_.store(0, std::memory_order_release);
    outer_tick_trampoline_global_.store(0, std::memory_order_release);
    callback_executor_trampoline_global_.store(0, std::memory_order_release);
    stage_break_wall_trampoline_global_.store(0, std::memory_order_release);
    stage_break_barrier_trampoline_global_.store(0, std::memory_order_release);
    stage_break_dispatch_trampoline_global_.store(0, std::memory_order_release);
    battle_audio_dispatch_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_remap_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_contact_handler_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_phase_changed_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_tracking_remove_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_tracking_insert_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_tracking_rehash_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_blueprint_publish_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_register_voice_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_append_command_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_stop_all_trampoline_global_.store(
        0, std::memory_order_release);
    battle_audio_append_parameter_trampoline_global_.store(
        0, std::memory_order_release);
    particle_spawn_trampoline_global_.store(0, std::memory_order_release);
    particle_finished_bind_trampoline_global_.store(
        0, std::memory_order_release);
    gameplay_xorshift96_trampoline_global_.store(
        0, std::memory_order_release);
    movevm_evaluate_if_trampoline_global_.store(
        0, std::memory_order_release);
    movevm_transition_author_07_trampoline_global_.store(
        0, std::memory_order_release);
    tira_probability_join = {};
    for (auto& handler : observed_battle_audio_handlers_)
        handler.store(0, std::memory_order_release);
    battle_audio_handler_overflow_.store(false, std::memory_order_release);
    rand_iat_slot_ = 0;
    srand_iat_slot_ = 0;
    image_base_ = 0;
    original_rand_ = nullptr;
    original_srand_ = nullptr;
    ucrt_broker_ = nullptr;
    callbacks_ = {};
}
