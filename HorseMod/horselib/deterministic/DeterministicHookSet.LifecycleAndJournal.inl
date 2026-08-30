DeterministicHookSet::~DeterministicHookSet()
{
    Uninstall();
}

bool DeterministicHookSet::ValidateInstallationSignatures(
    std::uintptr_t image_base) const noexcept
{
    const auto matches = [image_base](std::uintptr_t rva,
        const auto& signature) noexcept {
        return SafeEqual(reinterpret_cast<const void*>(image_base + rva),
            signature.data(), signature.size());
    };
    return matches(FrameLayout::landing_fencepost_rva,
               FrameLayout::landing_fencepost_signature)
        && matches(FrameLayout::outer_tick_rva, FrameLayout::outer_tick_signature)
        && matches(ReplayLayout::post_tick_rva, ReplayLayout::post_tick_signature)
        && matches(FrameLayout::callback_executor_rva, FrameLayout::callback_executor_signature)
        && matches(FrameLayout::stage_break_wall_handler_rva, FrameLayout::stage_break_wall_handler_signature)
        && matches(FrameLayout::stage_break_barrier_handler_rva, FrameLayout::stage_break_barrier_handler_signature)
        && matches(FrameLayout::stage_break_dispatch_rva, FrameLayout::stage_break_dispatch_signature)
        && matches(FrameLayout::battle_audio_dispatch_rva, FrameLayout::battle_audio_dispatch_signature)
        && matches(FrameLayout::battle_audio_remap_rva, FrameLayout::battle_audio_remap_signature)
        && matches(FrameLayout::battle_audio_contact_handler_rva, FrameLayout::battle_audio_contact_handler_signature)
        && matches(FrameLayout::battle_audio_phase_changed_rva, FrameLayout::battle_audio_phase_changed_signature)
        && matches(FrameLayout::battle_audio_tracking_remove_rva, FrameLayout::battle_audio_tracking_remove_signature)
        && matches(FrameLayout::battle_audio_tracking_insert_rva, FrameLayout::battle_audio_tracking_insert_signature)
        && matches(FrameLayout::battle_audio_tracking_rehash_rva, FrameLayout::battle_audio_tracking_rehash_signature)
        && matches(FrameLayout::battle_audio_blueprint_publish_rva, FrameLayout::battle_audio_blueprint_publish_signature)
        && matches(FrameLayout::battle_audio_register_voice_rva, FrameLayout::battle_audio_register_voice_signature)
        && matches(FrameLayout::battle_audio_append_command_rva, FrameLayout::battle_audio_append_command_signature)
        && matches(FrameLayout::battle_audio_stop_all_rva, FrameLayout::battle_audio_stop_all_signature)
        && matches(FrameLayout::battle_audio_append_parameter_rva, FrameLayout::battle_audio_append_parameter_signature)
        && matches(FrameLayout::battle_audio_append_parameter_owner_rva, FrameLayout::battle_audio_append_parameter_owner_signature)
        && matches(FrameLayout::particle_spawn_rva, FrameLayout::particle_spawn_signature)
        && matches(FrameLayout::particle_finished_bind_rva, FrameLayout::particle_finished_bind_signature)
        && matches(FrameLayout::gameplay_xorshift96_rva, FrameLayout::gameplay_xorshift96_signature)
        && matches(FrameLayout::movevm_evaluate_if_rva, FrameLayout::movevm_evaluate_if_signature)
        && matches(FrameLayout::movevm_transition_author_07_rva, FrameLayout::movevm_transition_author_07_signature);
}

bool DeterministicHookSet::InstallDetour(
    std::unique_ptr<PLH::x64Detour>& storage, std::uintptr_t target,
    std::uintptr_t replacement, std::uint64_t& trampoline,
    std::atomic<std::uint64_t>& published_trampoline) noexcept
{
    auto detour = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(target),
        static_cast<std::uint64_t>(replacement), &trampoline);
    if (!detour->hook()) return false;
    storage = std::move(detour);
    published_trampoline.store(trampoline, std::memory_order_release);
    return true;
}

bool DeterministicHookSet::InstallFrameHooks() noexcept
{
    return InstallDetour(frame_fencepost_detour_,
               image_base_ + FrameLayout::landing_fencepost_rva,
               reinterpret_cast<std::uintptr_t>(&FrameFencepostDetour),
               frame_fencepost_trampoline_, frame_fencepost_trampoline_global_)
        && InstallDetour(replay_post_tick_detour_,
            image_base_ + ReplayLayout::post_tick_rva,
            reinterpret_cast<std::uintptr_t>(&ReplayPostTickDetour),
            replay_post_tick_trampoline_, replay_post_tick_trampoline_global_)
        && InstallDetour(outer_tick_detour_, image_base_ + FrameLayout::outer_tick_rva,
            reinterpret_cast<std::uintptr_t>(&OuterTickDetour),
            outer_tick_trampoline_, outer_tick_trampoline_global_)
        && InstallDetour(callback_executor_detour_,
            image_base_ + FrameLayout::callback_executor_rva,
            reinterpret_cast<std::uintptr_t>(&CallbackExecutorDetour),
            callback_executor_trampoline_, callback_executor_trampoline_global_);
}

bool DeterministicHookSet::InstallStageHooks() noexcept
{
    return InstallDetour(stage_break_wall_detour_,
               image_base_ + FrameLayout::stage_break_wall_handler_rva,
               reinterpret_cast<std::uintptr_t>(&StageBreakWallDetour),
               stage_break_wall_trampoline_, stage_break_wall_trampoline_global_)
        && InstallDetour(stage_break_barrier_detour_,
            image_base_ + FrameLayout::stage_break_barrier_handler_rva,
            reinterpret_cast<std::uintptr_t>(&StageBreakBarrierDetour),
            stage_break_barrier_trampoline_,
            stage_break_barrier_trampoline_global_)
        && InstallDetour(stage_break_dispatch_detour_,
            image_base_ + FrameLayout::stage_break_dispatch_rva,
            reinterpret_cast<std::uintptr_t>(&StageBreakDispatchDetour),
            stage_break_dispatch_trampoline_,
            stage_break_dispatch_trampoline_global_);
}

bool DeterministicHookSet::InstallAudioHooks() noexcept
{
    return InstallDetour(battle_audio_dispatch_detour_, image_base_ + FrameLayout::battle_audio_dispatch_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioDispatchDetour), battle_audio_dispatch_trampoline_, battle_audio_dispatch_trampoline_global_)
        && InstallDetour(battle_audio_remap_detour_, image_base_ + FrameLayout::battle_audio_remap_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioRemapDetour), battle_audio_remap_trampoline_, battle_audio_remap_trampoline_global_)
        && InstallDetour(battle_audio_contact_handler_detour_, image_base_ + FrameLayout::battle_audio_contact_handler_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioContactHandlerDetour), battle_audio_contact_handler_trampoline_, battle_audio_contact_handler_trampoline_global_)
        && InstallDetour(battle_audio_phase_changed_detour_, image_base_ + FrameLayout::battle_audio_phase_changed_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioPhaseChangedDetour), battle_audio_phase_changed_trampoline_, battle_audio_phase_changed_trampoline_global_)
        && InstallDetour(battle_audio_tracking_remove_detour_, image_base_ + FrameLayout::battle_audio_tracking_remove_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioTrackingRemoveDetour), battle_audio_tracking_remove_trampoline_, battle_audio_tracking_remove_trampoline_global_)
        && InstallDetour(battle_audio_tracking_insert_detour_, image_base_ + FrameLayout::battle_audio_tracking_insert_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioTrackingInsertDetour), battle_audio_tracking_insert_trampoline_, battle_audio_tracking_insert_trampoline_global_)
        && InstallDetour(battle_audio_tracking_rehash_detour_, image_base_ + FrameLayout::battle_audio_tracking_rehash_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioTrackingRehashDetour), battle_audio_tracking_rehash_trampoline_, battle_audio_tracking_rehash_trampoline_global_)
        && InstallDetour(battle_audio_blueprint_publish_detour_, image_base_ + FrameLayout::battle_audio_blueprint_publish_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioBlueprintPublishDetour), battle_audio_blueprint_publish_trampoline_, battle_audio_blueprint_publish_trampoline_global_)
        && InstallDetour(battle_audio_register_voice_detour_, image_base_ + FrameLayout::battle_audio_register_voice_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioRegisterVoiceDetour), battle_audio_register_voice_trampoline_, battle_audio_register_voice_trampoline_global_)
        && InstallDetour(battle_audio_append_command_detour_, image_base_ + FrameLayout::battle_audio_append_command_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioAppendCommandDetour), battle_audio_append_command_trampoline_, battle_audio_append_command_trampoline_global_)
        && InstallDetour(battle_audio_stop_all_detour_, image_base_ + FrameLayout::battle_audio_stop_all_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioStopAllDetour), battle_audio_stop_all_trampoline_, battle_audio_stop_all_trampoline_global_)
        && InstallDetour(battle_audio_append_parameter_detour_, image_base_ + FrameLayout::battle_audio_append_parameter_rva, reinterpret_cast<std::uintptr_t>(&BattleAudioAppendParameterDetour), battle_audio_append_parameter_trampoline_, battle_audio_append_parameter_trampoline_global_);
}

bool DeterministicHookSet::InstallParticleHooks() noexcept
{
    return InstallDetour(particle_spawn_detour_,
               image_base_ + FrameLayout::particle_spawn_rva,
               reinterpret_cast<std::uintptr_t>(&ParticleSpawnDetour),
               particle_spawn_trampoline_, particle_spawn_trampoline_global_)
        && InstallDetour(particle_finished_bind_detour_,
            image_base_ + FrameLayout::particle_finished_bind_rva,
            reinterpret_cast<std::uintptr_t>(&ParticleFinishedBindDetour),
            particle_finished_bind_trampoline_,
            particle_finished_bind_trampoline_global_);
}

bool DeterministicHookSet::InstallRandomHooks() noexcept
{
    if (ucrt_broker_ != nullptr && !InstallUcrtIatHooks()) return false;
    return InstallDetour(gameplay_xorshift96_detour_,
               image_base_ + FrameLayout::gameplay_xorshift96_rva,
               reinterpret_cast<std::uintptr_t>(&GameplayXorshift96Detour),
               gameplay_xorshift96_trampoline_,
               gameplay_xorshift96_trampoline_global_)
        && InstallDetour(movevm_evaluate_if_detour_,
            image_base_ + FrameLayout::movevm_evaluate_if_rva,
            reinterpret_cast<std::uintptr_t>(&MoveVmEvaluateIfDetour),
            movevm_evaluate_if_trampoline_, movevm_evaluate_if_trampoline_global_)
        && InstallDetour(movevm_transition_author_07_detour_,
            image_base_ + FrameLayout::movevm_transition_author_07_rva,
            reinterpret_cast<std::uintptr_t>(&MoveVmTransitionAuthor07Detour),
            movevm_transition_author_07_trampoline_,
            movevm_transition_author_07_trampoline_global_);
}

Status DeterministicHookSet::AbortInstallation() noexcept
{
    installed_.store(true, std::memory_order_release);
    Uninstall();
    return Status::failure(FailureCode::AdapterUnqualified);
}

Status DeterministicHookSet::Install(
    std::uintptr_t image_base,
    DeterministicHookCallbacks callbacks,
    UcrtRandBroker* ucrt_broker)
{
    if (installed())
        return Status::failure(FailureCode::IllegalTransition);
    if (image_base == 0 || callbacks.frame_fencepost == nullptr
        || callbacks.outer_tick_begin == nullptr
        || callbacks.outer_tick == nullptr || callbacks.replay_exit == nullptr
        || active_.load(std::memory_order_acquire) != nullptr)
        return Status::failure(FailureCode::InvalidConfiguration);
    if (!ValidateInstallationSignatures(image_base))
        return Status::failure(FailureCode::AdapterUnqualified);

    image_base_ = image_base;
    callbacks_ = callbacks;
    ucrt_broker_ = ucrt_broker;
    active_.store(this, std::memory_order_release);
    if (!InstallFrameHooks() || !InstallStageHooks() || !InstallAudioHooks()
        || !InstallParticleHooks() || !InstallRandomHooks())
        return AbortInstallation();

    installed_.store(true, std::memory_order_release);
    return Status::success();
}

void DeterministicHookSet::Uninstall() noexcept
{
    if (!installed_.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }
    // Hooks are removed in the reverse of their installation order.
    if (movevm_transition_author_07_detour_)
        movevm_transition_author_07_detour_->unHook();
    if (movevm_evaluate_if_detour_)
        movevm_evaluate_if_detour_->unHook();
    if (gameplay_xorshift96_detour_)
        gameplay_xorshift96_detour_->unHook();
    UninstallUcrtIatHooks();
    if (particle_finished_bind_detour_)
        particle_finished_bind_detour_->unHook();
    if (particle_spawn_detour_) particle_spawn_detour_->unHook();
    if (battle_audio_append_parameter_detour_)
        battle_audio_append_parameter_detour_->unHook();
    if (battle_audio_stop_all_detour_)
        battle_audio_stop_all_detour_->unHook();
    if (battle_audio_append_command_detour_)
        battle_audio_append_command_detour_->unHook();
    if (battle_audio_register_voice_detour_)
        battle_audio_register_voice_detour_->unHook();
    if (battle_audio_blueprint_publish_detour_)
        battle_audio_blueprint_publish_detour_->unHook();
    if (battle_audio_tracking_rehash_detour_)
        battle_audio_tracking_rehash_detour_->unHook();
    if (battle_audio_tracking_insert_detour_)
        battle_audio_tracking_insert_detour_->unHook();
    if (battle_audio_tracking_remove_detour_)
        battle_audio_tracking_remove_detour_->unHook();
    if (battle_audio_phase_changed_detour_)
        battle_audio_phase_changed_detour_->unHook();
    if (battle_audio_contact_handler_detour_)
        battle_audio_contact_handler_detour_->unHook();
    if (battle_audio_remap_detour_) battle_audio_remap_detour_->unHook();
    if (battle_audio_dispatch_detour_)
        battle_audio_dispatch_detour_->unHook();
    if (stage_break_dispatch_detour_) stage_break_dispatch_detour_->unHook();
    if (stage_break_barrier_detour_) stage_break_barrier_detour_->unHook();
    if (stage_break_wall_detour_) stage_break_wall_detour_->unHook();
    if (callback_executor_detour_)
    {
        callback_executor_detour_->unHook();
    }
    if (outer_tick_detour_)
    {
        outer_tick_detour_->unHook();
    }
    if (replay_post_tick_detour_)
    {
        replay_post_tick_detour_->unHook();
    }
    if (frame_fencepost_detour_)
    {
        frame_fencepost_detour_->unHook();
    }
    active_.store(nullptr, std::memory_order_release);
    while (callbacks_in_flight_.load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
    }
    ClearState();
}

std::uintptr_t DeterministicHookSet::ObservedBattleAudioHandler(
    std::size_t index) noexcept
{
    return index < observed_battle_audio_handlers_.size()
        ? observed_battle_audio_handlers_[index].load(std::memory_order_acquire)
        : 0;
}

bool DeterministicHookSet::BattleAudioHandlerOverflowed() noexcept
{
    return battle_audio_handler_overflow_.load(std::memory_order_acquire);
}

Status DeterministicHookSet::BindStageBreakPresentationIdentity(
    std::uint64_t generation,
    std::span<const StageBreakActorRef> actors,
    const StageBreakListenerTopology& topology,
    std::span<const StageBreakParticleAssetRef> assets) noexcept
{
    if (!installed()) return Status::failure(FailureCode::IllegalTransition);
    return stage_break_presentation_identity_.Bind(
        generation, actors, topology, assets);
}

void DeterministicHookSet::InvalidateStageBreakPresentationIdentity() noexcept
{
    stage_break_presentation_identity_.Invalidate();
}

void DeterministicHookSet::InvalidateBattleAudioPresentationIdentity() noexcept
{
    for (auto& handler : observed_battle_audio_handlers_)
        handler.store(0, std::memory_order_release);
    battle_audio_handler_overflow_.store(false, std::memory_order_release);
    audio_owner_resolver_.Clear();
    audio_playback_map_.Clear();
}

Status DeterministicHookSet::MarkQualificationStageTerminal(
    std::uint32_t operation) noexcept
{
    auto* capture = active_outer_capture_;
    if (!installed() || capture == nullptr || capture->owned != nullptr
        || capture->observation == nullptr || (operation != 1 && operation != 2))
        return Status::failure(FailureCode::IllegalTransition);
    const auto mask = static_cast<std::uint8_t>(1u << (operation - 1));
    if (capture->observation->qualification_stage_terminal_mask != 0)
        return Status::failure(FailureCode::IllegalTransition);
    capture->observation->qualification_stage_terminal_mask = mask;
    return Status::success();
}

Status DeterministicHookSet::ResolveQualificationStageActor(
    std::uintptr_t actor, std::uint64_t& owner_logical_id) const noexcept
{
    owner_logical_id = 0;
    if (!installed() || active_outer_capture_ == nullptr || actor == 0)
        return Status::failure(FailureCode::IllegalTransition);
    const auto resolved = stage_break_presentation_identity_.ResolveActor(
        stage_break_presentation_identity_.generation(), actor,
        owner_logical_id);
    if (!resolved.ok() || owner_logical_id == 0)
        return Status::failure(resolved.ok()
            ? FailureCode::IdentityMismatch : resolved.code);
    return Status::success();
}

Status DeterministicHookSet::ExecuteQualificationStageTerminal(
    const StagePresentationJournalEntry& event,
    bool wall, std::uintptr_t actor) noexcept
{
    if (!installed() || active_outer_capture_ == nullptr || actor == 0)
        return Status::failure(FailureCode::IllegalTransition);

    if (wall)
    {
        if (event.canonical_before_size != 12 || event.payload_size != 1)
            return Status::failure(FailureCode::InvalidConfiguration);
        std::uint8_t undo_break{};
        float undo_fade_timer{};
        float undo_fade_rate{};
        std::uint8_t source_break{};
        float source_fade_timer{};
        float source_fade_rate{};
        std::memcpy(&source_break, event.canonical_before.data(), 1);
        std::memcpy(&source_fade_timer,
            event.canonical_before.data() + 4, sizeof(source_fade_timer));
        std::memcpy(&source_fade_rate,
            event.canonical_before.data() + 8, sizeof(source_fade_rate));
        if (!SafeRead(actor + 0x468, undo_break)
            || !SafeRead(actor + 0x46c, undo_fade_timer)
            || !SafeRead(actor + 0x470, undo_fade_rate))
            return Status::failure(FailureCode::CaptureFailed);
        if (!SafeWrite(actor + 0x468, source_break)
            || !SafeWrite(actor + 0x46c, source_fade_timer)
            || !SafeWrite(actor + 0x470, source_fade_rate))
        {
            static_cast<void>(SafeWrite(actor + 0x468, undo_break));
            static_cast<void>(SafeWrite(actor + 0x46c, undo_fade_timer));
            static_cast<void>(SafeWrite(actor + 0x470, undo_fade_rate));
            return Status::failure(FailureCode::RestoreWriteFailed);
        }
        bool restored{};
        __try
        {
            const bool immediately = event.semantic[4] != std::byte{};
            reinterpret_cast<StageBreakWallFn>(image_base_
                + Schema::Sc6FrameLayout::stage_break_wall_handler_rva)(
                    reinterpret_cast<void*>(actor), immediately);
        }
        __finally
        {
            restored = SafeWrite(actor + 0x468, undo_break)
                && SafeWrite(actor + 0x46c, undo_fade_timer)
                && SafeWrite(actor + 0x470, undo_fade_rate);
        }
        return restored ? Status::success()
            : Status::failure(FailureCode::UndoFailed);
    }

    if (event.canonical_before_size != 4 || event.payload_size != 12)
        return Status::failure(FailureCode::InvalidConfiguration);
    std::int32_t undo_hit_count{};
    std::int32_t source_hit_count{};
    std::memcpy(&source_hit_count, event.canonical_before.data(),
        sizeof(source_hit_count));
    if (!SafeRead(actor + 0x468, undo_hit_count))
        return Status::failure(FailureCode::CaptureFailed);
    if (!SafeWrite(actor + 0x468, source_hit_count))
        return Status::failure(FailureCode::RestoreWriteFailed);
    std::array<std::byte, 12> direction{};
    std::copy_n(event.semantic.begin() + 4, direction.size(),
        direction.begin());
    bool restored{};
    __try
    {
        reinterpret_cast<StageBreakBarrierFn>(image_base_
            + Schema::Sc6FrameLayout::stage_break_barrier_handler_rva)(
                reinterpret_cast<void*>(actor), direction.data());
    }
    __finally
    {
        restored = SafeWrite(actor + 0x468, undo_hit_count);
    }
    return restored ? Status::success()
        : Status::failure(FailureCode::UndoFailed);
}

Status DeterministicHookSet::CommitAudioTerminal(
    const AudioTerminalEvent& event) noexcept
{
    if (!installed() || active_outer_capture_ != nullptr || !event.valid())
        return Status::failure(FailureCode::IllegalTransition);
    const auto epoch = audio_owner_resolver_.epoch();
    std::uintptr_t owner{};
    std::uint64_t runtime_handle{};
    if (!audio_owner_resolver_.ResolveOwner(epoch, event.owner, owner)
        || !SafeRead(owner, runtime_handle) || runtime_handle == 0)
        return Status::failure(FailureCode::IdentityMismatch);

    struct CommandRecord
    {
        std::uint32_t operation{};
        std::uint32_t playback_id{};
        std::uint32_t immediate{};
        std::uint32_t reserved{};
        std::uint64_t value{};
    };
    static_assert(sizeof(CommandRecord) == 0x18);

    __try
    {
        switch (event.operation)
        {
        case AudioTerminalOperation::Create:
        {
            std::uint32_t existing{};
            if (audio_playback_map_.NativeForLogical(
                    epoch, event.owner, event.logical_playback_id, existing))
                return Status::success();
            if (!audio_playback_map_.CanInsert(
                    epoch, event.owner, event.logical_playback_id))
            {
                // Native voices may finish without an explicit StopOne
                // terminal. Retire only mappings whose voice has disappeared
                // from the owner's native active-voice set, matching the
                // authoritative register path below. The fixed map remains a
                // hard bound when all mapped voices are genuinely active.
                using FindActiveVoiceFn = void* (__fastcall*)(void*, std::int32_t);
                const auto find_active = reinterpret_cast<FindActiveVoiceFn>(
                    image_base_ + Schema::Sc6FrameLayout::
                        battle_audio_find_active_voice_rva);
                audio_playback_map_.PruneInactive(epoch,
                    [&](AudioOwnerSelector mapped_owner,
                        std::uint32_t native_id) noexcept
                    {
                        std::uintptr_t mapped_owner_address{};
                        return audio_owner_resolver_.ResolveOwner(
                                epoch, mapped_owner, mapped_owner_address)
                            && find_active(reinterpret_cast<void*>(
                                    mapped_owner_address + 0x38),
                                static_cast<std::int32_t>(native_id)) != nullptr;
                    });
                if (!audio_playback_map_.CanInsert(
                        epoch, event.owner, event.logical_playback_id))
                    return Status::failure(FailureCode::CapacityExceeded);
            }
            const auto original = reinterpret_cast<BattleAudioRegisterVoiceFn>(
                battle_audio_register_voice_trampoline_);
            if (original == nullptr)
                return Status::failure(FailureCode::IllegalTransition);
            std::uint32_t cue_sheet_slot = event.cue_sheet_id;
            if (IsAudioCueFamilyIdentity(event.cue_sheet_id)
                && !ResolveBattleCharaCueSheetSlot(
                    event.cue_sheet_id, cue_sheet_slot))
                return Status::failure(FailureCode::IdentityMismatch);
            const auto native_id = original(reinterpret_cast<void*>(owner),
                cue_sheet_slot, event.cue_id, event.value);
            if (native_id == audio_invalid_playback_id)
                return Status::failure(FailureCode::PresentationFailed);
            if (!audio_playback_map_.Insert(epoch, event.owner,
                    event.logical_playback_id, native_id))
                return Status::failure(FailureCode::PresentationFailed);
            return Status::success();
        }
        case AudioTerminalOperation::StopOne:
        {
            std::uint32_t native_id{};
            if (!audio_playback_map_.NativeForLogical(epoch, event.owner,
                    event.logical_playback_id, native_id))
                return Status::failure(FailureCode::IdentityMismatch);
            const auto original = reinterpret_cast<BattleAudioAppendCommandFn>(
                battle_audio_append_command_trampoline_);
            if (original == nullptr)
                return Status::failure(FailureCode::IllegalTransition);
            CommandRecord command{2, native_id, event.value, 0, 0};
            original(reinterpret_cast<void*>(owner), &command);
            static_cast<void>(audio_playback_map_.RemoveOne(
                epoch, event.owner, event.logical_playback_id));
            return Status::success();
        }
        case AudioTerminalOperation::StopAll:
        {
            const auto original = reinterpret_cast<BattleAudioStopAllFn>(
                battle_audio_stop_all_trampoline_);
            if (original == nullptr)
                return Status::failure(FailureCode::IllegalTransition);
            original(reinterpret_cast<void*>(owner),
                static_cast<std::uint8_t>(event.value));
            audio_playback_map_.RemoveOwner(epoch, event.owner);
            return Status::success();
        }
        case AudioTerminalOperation::SetParameter:
        {
            const auto original =
                reinterpret_cast<BattleAudioAppendOwnerParameterFn>(
                    image_base_ + Schema::Sc6FrameLayout::
                        battle_audio_append_parameter_owner_rva);
            std::uint32_t value_bits = event.value;
            float value{};
            std::memcpy(&value, &value_bits, sizeof(value));
            original(reinterpret_cast<void*>(owner),
                reinterpret_cast<void*>(image_base_ + 0x406f060
                    + static_cast<std::uintptr_t>(event.cue_sheet_id) * 0x10),
                value);
            return Status::success();
        }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return Status::failure(FailureCode::PresentationFailed);
    }
    return Status::failure(FailureCode::InvalidConfiguration);
}

Status DeterministicHookSet::CommitAudioBlueprint(
    const AudioBlueprintPresentationValue& value) noexcept
{
    if (!installed() || active_outer_capture_ != nullptr
        || value.handler_slot >= observed_battle_audio_handlers_.size())
        return Status::failure(FailureCode::IllegalTransition);
    const auto handler = observed_battle_audio_handlers_[value.handler_slot].load(
        std::memory_order_acquire);
    const auto original = reinterpret_cast<BattleAudioBlueprintPublishFn>(
        battle_audio_blueprint_publish_trampoline_);
    if (handler == 0 || original == nullptr)
        return Status::failure(FailureCode::IdentityMismatch);
    __try
    {
        auto semantic = value.semantic;
        original(reinterpret_cast<void*>(handler), semantic.data());
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return Status::failure(FailureCode::PresentationFailed);
    }
}

Status DeterministicHookSet::CommitStagePresentation(
    const StagePresentationValue& value) noexcept
{
    if (!installed() || active_outer_capture_ != nullptr
        || active_stage_commit != nullptr)
        return Status::failure(FailureCode::IllegalTransition);
    const auto kind = value.operation == StagePresentationOperation::WallBroken
        ? StageBreakActorKind::Wall
        : value.operation == StagePresentationOperation::BarrierHit
            ? StageBreakActorKind::Barrier
            : StageBreakActorKind{};
    std::uintptr_t actor{};
    if (kind == StageBreakActorKind{}
        || !stage_break_presentation_identity_.ResolveActorAddress(
            value.coordinate.generation, value.owner_logical_id, kind,
            actor).ok())
        return Status::failure(FailureCode::IdentityMismatch);
    std::int32_t expected_actor_id{};
    std::memcpy(&expected_actor_id, value.source_semantic.data(),
        sizeof(expected_actor_id));
    std::int32_t observed_actor_id{};
    const auto actor_id_offset = kind == StageBreakActorKind::Wall
        ? std::uintptr_t{0x450} : std::uintptr_t{0x420};
    if (!SafeRead(actor + actor_id_offset, observed_actor_id)
        || observed_actor_id != expected_actor_id)
        return Status::failure(FailureCode::IdentityMismatch);
    for (std::size_t index = 0; index < value.particle_count; ++index)
    {
        const auto& semantic = value.particles[index].semantic;
        const auto route_byte = std::to_integer<std::uint8_t>(semantic[0]);
        const auto route = route_byte == 1 ? ParticleRoute::WallBreak
            : route_byte == 2 ? ParticleRoute::BarrierHit
            : route_byte == 3 ? ParticleRoute::BarrierBreak
            : ParticleRoute{};
        std::uint64_t owner_id{};
        std::uint64_t asset_id{};
        std::memcpy(&owner_id, semantic.data() + 1, sizeof(owner_id));
        std::memcpy(&asset_id, semantic.data() + 9, sizeof(asset_id));
        std::uintptr_t particle_actor{};
        std::uintptr_t asset{};
        if (route == ParticleRoute{} || owner_id != value.owner_logical_id
            || !stage_break_presentation_identity_.ResolveAssetAddress(
                value.coordinate.generation, owner_id, asset_id, route,
                particle_actor, asset).ok()
            || particle_actor != actor || asset == 0)
            return Status::failure(FailureCode::IdentityMismatch);
    }

    StagePresentationCommitContext context{&value};
    Status status = Status::success();
    __try
    {
        active_stage_commit = &context;
        if (kind == StageBreakActorKind::Wall)
        {
            std::uint8_t current_state{};
            float current_timer{};
            float current_rate{};
            std::uint8_t before_state{};
            float before_timer{};
            float before_rate{};
            std::memcpy(&before_state, value.canonical_before.data(), 1);
            std::memcpy(&before_timer, value.canonical_before.data() + 4, 4);
            std::memcpy(&before_rate, value.canonical_before.data() + 8, 4);
            const bool immediate = value.source_semantic[4] != std::byte{};
            const auto original = reinterpret_cast<StageBreakWallFn>(
                stage_break_wall_trampoline_);
            if (value.source_payload_size != 1
                || value.canonical_before_size != 12 || before_state != 0
                || original == nullptr
                || !SafeRead(actor + 0x468, current_state)
                || !SafeRead(actor + 0x46c, current_timer)
                || !SafeRead(actor + 0x470, current_rate))
                status = Status::failure(FailureCode::RestorePreflightFailed);
            else
            {
                const bool staged = SafeWrite(actor + 0x468, before_state)
                    && SafeWrite(actor + 0x46c, before_timer)
                    && SafeWrite(actor + 0x470, before_rate);
                if (!staged)
                {
                    static_cast<void>(SafeWrite(actor + 0x468, current_state));
                    static_cast<void>(SafeWrite(actor + 0x46c, current_timer));
                    static_cast<void>(SafeWrite(actor + 0x470, current_rate));
                    status = Status::failure(FailureCode::RestoreWriteFailed);
                }
                else
                {
                    __try { original(reinterpret_cast<void*>(actor), immediate); }
                    __finally
                    {
                        if (!SafeWrite(actor + 0x468, current_state)
                            || !SafeWrite(actor + 0x46c, current_timer)
                            || !SafeWrite(actor + 0x470, current_rate))
                            status = Status::failure(
                                FailureCode::RestoreWriteFailed);
                    }
                }
            }
        }
        else
        {
            std::int32_t current_count{};
            std::int32_t before_count{};
            std::array<std::byte, 12> direction{};
            std::memcpy(&before_count, value.canonical_before.data(), 4);
            std::copy_n(value.source_semantic.begin() + 4, direction.size(),
                direction.begin());
            const auto original = reinterpret_cast<StageBreakBarrierFn>(
                stage_break_barrier_trampoline_);
            if (value.source_payload_size != 12
                || value.canonical_before_size != 4 || before_count < 0
                || original == nullptr
                || !SafeRead(actor + 0x468, current_count))
                status = Status::failure(FailureCode::RestorePreflightFailed);
            else if (!SafeWrite(actor + 0x468, before_count))
            {
                static_cast<void>(SafeWrite(actor + 0x468, current_count));
                status = Status::failure(FailureCode::RestoreWriteFailed);
            }
            else
            {
                __try { original(reinterpret_cast<void*>(actor), direction.data()); }
                __finally
                {
                    if (!SafeWrite(actor + 0x468, current_count))
                        status = Status::failure(FailureCode::RestoreWriteFailed);
                }
            }
        }
        active_stage_commit = nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        active_stage_commit = nullptr;
        status = Status::failure(FailureCode::PresentationFailed);
    }
    if (!status.ok()) return status;
    if (context.failure != FailureCode::None
        || context.particle_index != value.particle_count)
        return Status::failure(context.failure != FailureCode::None
            ? context.failure : FailureCode::PresentationFailed);
    return Status::success();
}

Status DeterministicHookSet::ArmPresentationCaptureForNextOuterTick() noexcept
{
    if (!installed() || active_outer_capture_ != nullptr)
        return Status::failure(FailureCode::IllegalTransition);
    bool expected = false;
    if (!suppress_presentation_next_outer_tick_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel,
            std::memory_order_acquire))
        return Status::failure(FailureCode::IllegalTransition);
    return Status::success();
}

Status DeterministicHookSet::RestoreBattleAudioRemapEntry(
    const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult&) noexcept
{
    // This is deliberately a preflight only.  The journal entry is the value
    // observed immediately before a mutating remap call, not necessarily the
    // value at the outer-batch entry.  Writing it here can alter earlier
    // semantic-listener work in the same native batch.  BattleAudioRemapDetour
    // applies it at the independently observed first-mutating-call boundary.
    const auto mask = envelope.battle_audio_remap_entry_mask;
    if ((mask >> maximum_battle_audio_handlers) != 0
        || battle_audio_handler_overflow_.load(std::memory_order_acquire))
        return Status::failure(FailureCode::CapacityExceeded);
    for (std::size_t index = 0; index < maximum_battle_audio_handlers; ++index)
    {
        if ((mask & (std::uint8_t{1} << index)) == 0) continue;
        if (envelope.battle_audio_remap_entry_values[index] > 1)
            return Status::failure(FailureCode::RestorePreflightFailed);
        const auto handler = observed_battle_audio_handlers_[index].load(
            std::memory_order_acquire);
        std::uintptr_t vtable{};
        std::int32_t current{};
        if (handler == 0
            || !SafeRead(handler, vtable)
            || vtable != image_base_ + 0x326A6C8
            || !SafeRead(handler + 0x3E0, current)
            || current < 0 || current > 1)
            return Status::failure(FailureCode::IdentityMismatch);
        (void)current;
    }
    return Status::success();
}

bool DeterministicHookSet::ConsumeBattleAudioSource(
    const NativeBatchEnvelope& envelope,
    const BattleAudioSourceJournalEntry& source,
    OwnedBatchReplayResult& output) noexcept
{
        const auto dispatch_end = static_cast<std::size_t>(source.first_dispatch)
            + source.dispatch_count;
        const auto remap_end = static_cast<std::size_t>(source.first_remap)
            + source.remap_count;
        const auto blueprint_end =
            static_cast<std::size_t>(source.first_blueprint)
            + source.blueprint_count;
        const auto terminal_end =
            static_cast<std::size_t>(source.first_terminal)
            + source.terminal_count;
        if (source.first_dispatch != output.suppressed_audio_calls
            || source.first_remap != output.suppressed_audio_remap_calls
            || source.first_blueprint
                != output.suppressed_audio_blueprint_calls
            || source.first_terminal
                != output.suppressed_audio_terminal_calls
            || dispatch_end > envelope.battle_audio_journal_count
            || remap_end > envelope.battle_audio_remap_journal_count
            || blueprint_end
                > envelope.battle_audio_blueprint_journal_count
            || terminal_end > envelope.audio_terminal_journal_count)
            return false;
        AppendBattleAudioSourceSemantic(
            source.semantic, output.suppressed_audio_source_hash);
        ++output.suppressed_audio_source_calls;
        std::int32_t source_contact_type{};
        std::memcpy(&source_contact_type, source.semantic.data() + 1,
            sizeof(source_contact_type));
        for (std::size_t remap_index = source.first_remap;
             remap_index < remap_end; ++remap_index)
        {
            const auto& entry = envelope.battle_audio_remap_journal[remap_index];
            if (!ValidateJournaledBattleAudioRemap(entry)
                || entry.contact_type != source_contact_type)
                return false;
            const auto handler_slot = entry.handler_slot;
            if (handler_slot >= maximum_battle_audio_handlers) return false;
            const auto handler = observed_battle_audio_handlers_[handler_slot].load(
                std::memory_order_acquire);
            if (handler == 0) return false;
            const auto handler_bit = std::uint8_t{1} << handler_slot;
            const bool mutates = entry.contact_type >= 8
                && entry.contact_type <= 11;
            std::int32_t current{};
            if (!SafeRead(handler + 0x3E0, current)
                || current < 0 || current > 1)
                return false;
            if (mutates
                && (output.suppressed_audio_remap_entry_mask & handler_bit) == 0)
            {
                if ((envelope.battle_audio_remap_entry_mask & handler_bit) == 0
                    || envelope.battle_audio_remap_entry_values[handler_slot]
                        != entry.before)
                    return false;
                output.suppressed_audio_remap_entry_mask |= handler_bit;
                output.suppressed_audio_remap_entry_values[handler_slot]
                    = static_cast<std::uint8_t>(entry.before);
                if (current != entry.before
                    && (!SafeWrite(handler + 0x3E0, entry.before)
                        || !SafeRead(handler + 0x3E0, current)
                        || current != entry.before))
                    return false;
            }
            else if (current != entry.before
                && (!SafeWrite(handler + 0x3E0, entry.before)
                    || !SafeRead(handler + 0x3E0, current)
                    || current != entry.before))
            {
                return false;
            }
            if (!SafeWrite(handler + 0x3E0, entry.after)
                || !SafeRead(handler + 0x3E0, current)
                || current != entry.after
                || !AppendBattleAudioRemapSignature(
                    entry.handler_slot, entry.contact_type,
                    entry.before, entry.result, entry.after,
                    output.suppressed_audio_remap_hash))
                return false;
            ++output.suppressed_audio_remap_calls;
        }
        for (std::size_t dispatch_index = source.first_dispatch;
             dispatch_index < dispatch_end; ++dispatch_index)
        {
            const auto& entry = envelope.battle_audio_journal[dispatch_index];
            if (entry.direct != 0
                || !AppendBattleAudioSemantic(entry.semantic,
                    output.suppressed_audio_sequence_hash,
                    output.suppressed_audio_route_hash,
                    output.suppressed_audio_payload_hash,
                    output.suppressed_audio_position_hash))
                return false;
            ++output.suppressed_audio_calls;
        }
        for (std::size_t blueprint_index = source.first_blueprint;
             blueprint_index < blueprint_end; ++blueprint_index)
        {
            const auto& entry =
                envelope.battle_audio_blueprint_journal[blueprint_index];
            if (entry.direct != 0
                || !AppendBattleAudioBlueprintSemantic(
                    entry, output.suppressed_audio_blueprint_hash))
                return false;
            ++output.suppressed_audio_blueprint_calls;
        }
        for (std::size_t terminal_index = source.first_terminal;
             terminal_index < terminal_end; ++terminal_index)
        {
            if (!AppendAudioTerminalSemantic(
                    envelope.audio_terminal_journal[terminal_index],
                    output.suppressed_audio_terminal_hash))
                return false;
            ++output.suppressed_audio_terminal_calls;
        }
        return true;
}

bool DeterministicHookSet::ConsumeDirectAudioBlueprintsUntil(
    const NativeBatchEnvelope& envelope,
    std::size_t target,
    OwnedBatchReplayResult& output) noexcept
{
        if (target > envelope.battle_audio_blueprint_journal_count
            || target < output.suppressed_audio_blueprint_calls)
            return false;
        while (output.suppressed_audio_blueprint_calls < target)
        {
            const auto& entry = envelope.battle_audio_blueprint_journal[
                output.suppressed_audio_blueprint_calls];
            if (entry.direct != 1
                || !AppendBattleAudioBlueprintSemantic(
                    entry, output.suppressed_audio_blueprint_hash))
                return false;
            ++output.suppressed_audio_blueprint_calls;
        }
        return true;
}

bool DeterministicHookSet::ConsumeAudioTerminalsUntil(
    const NativeBatchEnvelope& envelope,
    std::size_t target,
    OwnedBatchReplayResult& output) noexcept
{
        if (target > envelope.audio_terminal_journal_count
            || target < output.suppressed_audio_terminal_calls)
            return false;
        while (output.suppressed_audio_terminal_calls < target)
        {
            const auto& entry = envelope.audio_terminal_journal[
                output.suppressed_audio_terminal_calls];
            if (!AppendAudioTerminalSemantic(
                    entry, output.suppressed_audio_terminal_hash))
                return false;
            ++output.suppressed_audio_terminal_calls;
        }
        return true;
}

bool DeterministicHookSet::CompleteBattleAudioJournal(
    const NativeBatchEnvelope& envelope,
    OwnedBatchReplayResult& output) noexcept
{
    if (output.audio_journal_failure_mask != 0
        || output.suppressed_audio_calls > envelope.battle_audio_journal_count
        || output.suppressed_audio_source_calls
            > envelope.battle_audio_source_journal_count
        || output.suppressed_audio_remap_calls
            > envelope.battle_audio_remap_journal_count
        || output.suppressed_audio_blueprint_calls
            > envelope.battle_audio_blueprint_journal_count
        || output.suppressed_audio_terminal_calls
            > envelope.audio_terminal_journal_count)
        return false;
    while (output.suppressed_audio_calls < envelope.battle_audio_journal_count
        || output.suppressed_audio_source_calls
            < envelope.battle_audio_source_journal_count)
    {
        if (output.suppressed_audio_source_calls
            < envelope.battle_audio_source_journal_count)
        {
            const auto& source = envelope.battle_audio_source_journal[
                output.suppressed_audio_source_calls];
            if (source.first_dispatch == output.suppressed_audio_calls)
            {
                if (!ConsumeDirectAudioBlueprintsUntil(envelope, source.first_blueprint, output)
                    || !ConsumeAudioTerminalsUntil(envelope, source.first_terminal, output)
                    || !ConsumeBattleAudioSource(envelope, source, output))
                    return false;
                continue;
            }
            if (source.first_dispatch < output.suppressed_audio_calls)
                return false;
        }
        if (output.suppressed_audio_calls >= envelope.battle_audio_journal_count)
            return false;
        const auto& entry =
            envelope.battle_audio_journal[output.suppressed_audio_calls];
        if (entry.direct != 1
            || !AppendBattleAudioSemantic(entry.semantic,
                output.suppressed_audio_sequence_hash,
                output.suppressed_audio_route_hash,
                output.suppressed_audio_payload_hash,
                output.suppressed_audio_position_hash)
            || !AppendBattleAudioSemantic(entry.semantic,
                output.suppressed_audio_direct_sequence_hash,
                output.suppressed_audio_direct_route_hash,
                output.suppressed_audio_direct_payload_hash,
                output.suppressed_audio_direct_position_hash))
            return false;
        ++output.suppressed_audio_calls;
        ++output.suppressed_audio_direct_dispatches;
    }
    if (!ConsumeDirectAudioBlueprintsUntil(envelope, 
            envelope.battle_audio_blueprint_journal_count, output)
        || !ConsumeAudioTerminalsUntil(envelope, envelope.audio_terminal_journal_count, output))
        return false;
    while (output.suppressed_presentation_order_events
        < envelope.presentation_order_journal_count)
    {
        if (!ReplayExpectedPresentationOrder(envelope, output))
            return false;
    }
    return output.suppressed_audio_remap_calls
            == envelope.battle_audio_remap_journal_count
        && output.suppressed_audio_blueprint_calls
            == envelope.battle_audio_blueprint_journal_count
        && output.suppressed_audio_terminal_calls
            == envelope.audio_terminal_journal_count
        && output.suppressed_audio_terminal_hash
            == envelope.audio_terminal_hash;
}

bool DeterministicHookSet::PrepareAudioOwnerGraph(
    std::uintptr_t battle_manager) noexcept
{
    constexpr std::uintptr_t cri_manager_slot_rva = 0x41492e8;
    constexpr std::size_t maximum_battle_players = 64;
    audio_graph_failure_stage_ = 1;
    if (image_base_ == 0 || battle_manager == 0) return false;

    std::uintptr_t cri_manager{};
    std::uintptr_t bgm_state{};
    std::uintptr_t active_context{};
    std::uintptr_t battle_audio_manager{};
    if (!SafeRead(image_base_ + cri_manager_slot_rva, cri_manager)
        || cri_manager == 0
        || !SafeRead(cri_manager + 0x90, bgm_state) || bgm_state == 0
        || !SafeRead(cri_manager + 0xa0, active_context)
        || active_context == 0
        || !SafeRead(battle_manager + 0x520, battle_audio_manager)
        || battle_audio_manager == 0)
        return false;
    audio_graph_failure_stage_ = 2;

    const std::uint64_t epoch = audio_graph_epoch_counter_ + 1;
    AudioOwnerResolver candidate;
    if (!candidate.BeginEpoch(epoch)) return false;
    audio_graph_failure_stage_ = 3;
    std::array<std::uintptr_t, maximum_audio_owner_bindings> bound{};
    std::size_t bound_count{};
    const auto bind_unique = [&](std::uintptr_t owner,
                                 AudioOwnerSelector selector) noexcept {
        if (owner == 0) return true;
        for (std::size_t index = 0; index < bound_count; ++index)
            if (bound[index] == owner) return true;
        if (bound_count >= bound.size()
            || !candidate.Bind(epoch, owner, selector))
            return false;
        bound[bound_count++] = owner;
        return true;
    };
    const auto read_owner = [](std::uintptr_t shared_player,
                               std::uintptr_t& owner) noexcept {
        owner = 0;
        return shared_player != 0 && SafeRead(shared_player, owner);
    };
    const auto bind_shared = [&](std::uintptr_t shared_player,
                                 AudioOwnerSelector selector) noexcept {
        if (shared_player == 0) return true;
        std::uintptr_t owner{};
        return read_owner(shared_player, owner) && bind_unique(owner, selector);
    };

    std::uintptr_t bgm_pairs{};
    std::int32_t bgm_count{};
    if (!SafeRead(bgm_state, bgm_pairs) || bgm_pairs == 0
        || !SafeRead(bgm_state + 8, bgm_count)
        || bgm_count < 2 || bgm_count > 16)
        return false;
    audio_graph_failure_stage_ = 4;
    for (std::uint8_t lane = 0; lane < 2; ++lane)
    {
        std::uintptr_t shared{};
        if (!SafeRead(bgm_pairs + static_cast<std::uintptr_t>(lane) * 0x10,
                shared)
            || !bind_shared(shared, {AudioOwnerDomain::BgmLane, lane, 0}))
            return false;
    }
    audio_graph_failure_stage_ = 5;
    std::uintptr_t shared{};
    if (!SafeRead(bgm_state + 0x10, shared)
        || !bind_shared(shared, {AudioOwnerDomain::Jingle, 0, 0})
        || !SafeRead(bgm_state + 0x60, shared)
        || !bind_shared(shared, {AudioOwnerDomain::BgmDirect, 0, 0})
        || !SafeRead(active_context, shared)
        || !bind_shared(shared, {AudioOwnerDomain::ActiveContextSe, 0, 0})
        || !SafeRead(active_context + 0x10, shared)
        || !bind_shared(shared, {AudioOwnerDomain::ActiveContextVoice, 0, 0}))
        return false;
    audio_graph_failure_stage_ = 6;

    std::uintptr_t class_pairs{};
    std::int32_t class_count{};
    std::uintptr_t chara_pairs{};
    std::int32_t chara_count{};
    if (!SafeRead(battle_audio_manager + 0x400, class_pairs)
        || !SafeRead(battle_audio_manager + 0x408, class_count)
        || !SafeRead(battle_audio_manager + 0x410, chara_pairs)
        || !SafeRead(battle_audio_manager + 0x418, chara_count)
        || class_count < 0 || class_count > maximum_battle_players
        || chara_count < 0 || chara_count > maximum_battle_players
        || (class_count != 0 && class_pairs == 0)
        || (chara_count != 0 && chara_pairs == 0))
        return false;
    audio_graph_failure_stage_ = 7;
    for (std::int32_t index = 0; index < class_count; ++index)
    {
        if (!SafeRead(class_pairs + static_cast<std::uintptr_t>(index) * 0x10,
                shared)
            || !bind_shared(shared, {AudioOwnerDomain::BattleClassPlayer,
                static_cast<std::uint8_t>(index), 0}))
            return false;
    }
    audio_graph_failure_stage_ = 8;
    for (std::int32_t index = 0; index < chara_count; ++index)
    {
        if (!SafeRead(chara_pairs + static_cast<std::uintptr_t>(index) * 0x10,
                shared)
            || !bind_shared(shared, {AudioOwnerDomain::BattleCharaPlayer,
                static_cast<std::uint8_t>(index), 0}))
            return false;
    }
    audio_graph_failure_stage_ = 9;
    if (!SafeRead(battle_audio_manager + 0x420, shared)
        || !bind_shared(shared,
            {AudioOwnerDomain::BattleSharedPlayer, 0, 0})
        || !candidate.Seal(epoch))
        return false;
    audio_graph_failure_stage_ = 10;

    if (audio_owner_resolver_.SameBindings(candidate))
    {
        audio_graph_battle_manager_ = battle_manager;
        audio_graph_failure_stage_ = 0;
        return true;
    }

    audio_owner_resolver_ = candidate;
    if (!audio_playback_map_.BeginEpoch(epoch))
    {
        audio_owner_resolver_.Clear();
        return false;
    }
    audio_graph_epoch_counter_ = epoch;
    audio_graph_battle_manager_ = battle_manager;
    audio_graph_failure_stage_ = 0;
    return true;
}

bool DeterministicHookSet::ResolveAudioOwner(
    std::uintptr_t owner, AudioOwnerSelector& selector) noexcept
{
    const auto epoch = audio_owner_resolver_.epoch();
    if (audio_owner_resolver_.Resolve(epoch, owner, selector)) return true;
    auto* batch = active_outer_capture_;
    if (batch == nullptr || batch->observation == nullptr
        || !PrepareAudioOwnerGraph(batch->observation->battle_manager))
        return false;
    return audio_owner_resolver_.Resolve(
        audio_owner_resolver_.epoch(), owner, selector);
}

bool DeterministicHookSet::ResolveBattleCharaCueFamilyIdentity(
    std::uint32_t cue_sheet_slot, std::uint32_t& identity) noexcept
{
    identity = 0;
    constexpr std::int32_t maximum_cue_families = 64;
    std::uintptr_t battle_audio_manager{};
    std::uintptr_t entries{};
    std::int32_t count{};
    if (audio_graph_battle_manager_ == 0
        || !SafeRead(audio_graph_battle_manager_ + 0x520,
            battle_audio_manager)
        || battle_audio_manager == 0
        || !SafeRead(battle_audio_manager + 0x430, entries)
        || !SafeRead(battle_audio_manager + 0x438, count)
        || count <= 0 || count > maximum_cue_families || entries == 0)
        return false;

    bool found{};
    for (std::int32_t index = 0; index < count; ++index)
    {
        const auto entry = entries + static_cast<std::uintptr_t>(index) * 0x10;
        std::uint8_t family{};
        std::uint32_t slot{};
        if (!SafeRead(entry, family) || !SafeRead(entry + 4, slot))
            return false;
        if (slot != cue_sheet_slot) continue;
        if (found) return false;
        identity = MakeAudioCueFamilyIdentity(family);
        found = true;
    }
    return found;
}

bool DeterministicHookSet::ResolveBattleCharaCueSheetSlot(
    std::uint32_t identity, std::uint32_t& cue_sheet_slot) noexcept
{
    cue_sheet_slot = 0;
    if (!IsAudioCueFamilyIdentity(identity)) return false;
    constexpr std::int32_t maximum_cue_families = 64;
    std::uintptr_t battle_audio_manager{};
    std::uintptr_t entries{};
    std::int32_t count{};
    if (audio_graph_battle_manager_ == 0
        || !SafeRead(audio_graph_battle_manager_ + 0x520,
            battle_audio_manager)
        || battle_audio_manager == 0
        || !SafeRead(battle_audio_manager + 0x430, entries)
        || !SafeRead(battle_audio_manager + 0x438, count)
        || count <= 0 || count > maximum_cue_families || entries == 0)
        return false;

    const auto wanted = AudioCueFamilyFromIdentity(identity);
    bool found{};
    for (std::int32_t index = 0; index < count; ++index)
    {
        const auto entry = entries + static_cast<std::uintptr_t>(index) * 0x10;
        std::uint8_t family{};
        std::uint32_t slot{};
        if (!SafeRead(entry, family) || !SafeRead(entry + 4, slot))
            return false;
        if (family != wanted) continue;
        if (found) return false;
        cue_sheet_slot = slot;
        found = true;
    }
    return found;
}

bool DeterministicHookSet::RecordAudioTerminal(
    OuterTickCaptureContext* batch,
    const AudioTerminalEvent& event,
    std::uint32_t return_rva,
    std::uint32_t raw_cue_sheet_id) noexcept
{
    if (batch == nullptr || batch->observation == nullptr || !event.valid())
        return false;
    const bool verify = IsOwnedPresentationVerification(batch);
    if (verify)
    {
        auto& replay = *batch->owned->result;
        const auto& envelope = *batch->owned->request->envelope;
        const auto replay_index = replay.suppressed_audio_terminal_calls;
        // Presentation-local audio queues survive checkpoint restore. Admit
        // only an exact next source-frame terminal; stale calls are discarded
        // without consuming either ordered cursor. CompleteBattleAudioJournal
        // later supplies a missing suffix only from independently verified
        // source spans, and ConsumeBattleAudioJournal rechecks the full hash.
        if (replay_index >= envelope.audio_terminal_journal_count
            || envelope.audio_terminal_journal[replay_index] != event
            || !MatchesNextPresentationOrder(
                PresentationEventFamily::AudioTerminal, replay_index,
                envelope, replay, batch->observation,
                batch->frame_counter_address))
            return true;
    }
    auto& observation = *batch->observation;
    const auto observed_index = observation.audio_terminal_calls++;
    if (!AppendObservedPresentationOrder(&observation,
            batch->frame_counter_address,
            PresentationEventFamily::AudioTerminal, observed_index)
        || observation.audio_terminal_journal_count
            >= observation.audio_terminal_journal.size()
        || !AppendAudioTerminalSemantic(event,
            observation.audio_terminal_hash))
    {
        ++observation.battle_audio_signature_failures;
        observation.battle_audio_signature_failure_mask |= 1u << 12;
        return false;
    }
    const auto journal_index = observation.audio_terminal_journal_count++;
    observation.audio_terminal_journal[journal_index] = event;
    observation.audio_terminal_return_rvas[journal_index] = return_rva;
    observation.audio_terminal_raw_cue_sheet_ids[journal_index] =
        raw_cue_sheet_id;

    if (batch->owned == nullptr
        || !batch->owned->request->suppress_ephemeral_presentation)
        return true;
    auto& replay = *batch->owned->result;
    const auto& envelope = *batch->owned->request->envelope;
    const auto replay_index = replay.suppressed_audio_terminal_calls++;
    if (!AppendAudioTerminalSemantic(event,
            replay.suppressed_audio_terminal_hash)
        || (verify && (!VerifyPresentationOrder(
                PresentationEventFamily::AudioTerminal, replay_index,
                envelope, replay, &observation,
                batch->frame_counter_address)
            || replay_index >= envelope.audio_terminal_journal_count
            || envelope.audio_terminal_journal[replay_index] != event)))
    {
        ++replay.audio_sequence_mismatches;
        replay.audio_journal_failure_mask |= 1u << 9;
        ++replay.presentation_failures;
        replay.presentation_failure_mask |= 1u << 13;
        replay.failure = FailureCode::PresentationFailed;
        return false;
    }
    return true;
}

bool DeterministicHookSet::IsOwnedPresentationSuppressed(
    const OuterTickCaptureContext* batch) noexcept
{
    return batch != nullptr && batch->owned != nullptr
        && batch->owned->request->suppress_ephemeral_presentation;
}

bool DeterministicHookSet::IsPresentationSuppressed(
    const OuterTickCaptureContext* batch) noexcept
{
    return IsOwnedPresentationSuppressed(batch)
        || (batch != nullptr
            && batch->suppress_speculative_presentation);
}
